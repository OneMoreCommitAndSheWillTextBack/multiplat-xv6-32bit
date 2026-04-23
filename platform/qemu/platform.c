#include "platform.h"
#include "platform_qemu.h"

#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "virtio.h"

/*
 * QEMU virt board implementation of the common platform seam.
 */

extern volatile int panicked;
extern uint32 timer_scratch[NCPU][6];
extern void timervec(void);

//
// QEMU UART (16550A)
//

#define UART_REG(reg) ((volatile unsigned char *)(QEMU_UART0 + (reg)))

#define UART_RHR 0
#define UART_THR 0
#define UART_IER 1
#define UART_IER_RX_ENABLE (1 << 0)
#define UART_IER_TX_ENABLE (1 << 1)
#define UART_FCR 2
#define UART_ISR 2
#define UART_LCR 3
#define UART_LCR_BAUD_LATCH (1 << 7)
#define UART_LSR 5
#define UART_LSR_RX_READY (1 << 0)
#define UART_LSR_TX_IDLE (1 << 5)

#define UART_READ(reg) (*(UART_REG(reg)))
#define UART_WRITE(reg, v) (*(UART_REG(reg)) = (v))

struct {
  struct spinlock lock;
  char tx_buf[32];
  uint32 tx_w;
  uint32 tx_r;
} qemu_uart;

static void
platform_qemu_uart_start(void)
{
  while(1){
    if(qemu_uart.tx_w == qemu_uart.tx_r){
      UART_READ(UART_ISR);
      return;
    }

    if((UART_READ(UART_LSR) & UART_LSR_TX_IDLE) == 0)
      return;

    int c = qemu_uart.tx_buf[qemu_uart.tx_r % sizeof(qemu_uart.tx_buf)];
    qemu_uart.tx_r += 1;
    wakeup(&qemu_uart.tx_r);
    UART_WRITE(UART_THR, c);
  }
}

void
platform_uart_init(void)
{
  UART_WRITE(UART_IER, 0x00);
  UART_WRITE(UART_LCR, UART_LCR_BAUD_LATCH);
  UART_WRITE(0, 32000000 / 115200 & 0xff);
  UART_WRITE(1, 32000000 / 115200 >> 8);
  UART_WRITE(UART_LCR, 0x00);
  UART_WRITE(UART_FCR, 0x07);
  UART_WRITE(UART_IER, UART_IER_TX_ENABLE | UART_IER_RX_ENABLE);

  initlock(&qemu_uart.lock, "platform_uart");
}

int
platform_uart_getc(void)
{
  if(UART_READ(UART_LSR) & UART_LSR_RX_READY)
    return UART_READ(UART_RHR);
  return -1;
}

void
platform_uart_putc(int c)
{
  acquire(&qemu_uart.lock);

  if(panicked){
    for(;;)
      ;
  }

  while(qemu_uart.tx_w == qemu_uart.tx_r + sizeof(qemu_uart.tx_buf))
    sleep(&qemu_uart.tx_r, &qemu_uart.lock);

  qemu_uart.tx_buf[qemu_uart.tx_w % sizeof(qemu_uart.tx_buf)] = c;
  qemu_uart.tx_w += 1;
  platform_qemu_uart_start();

  release(&qemu_uart.lock);
}

void
platform_uart_putc_sync(int c)
{
  push_off();

  if(panicked){
    for(;;)
      ;
  }

  while((UART_READ(UART_LSR) & UART_LSR_TX_IDLE) == 0)
    ;
  UART_WRITE(UART_THR, c);

  pop_off();
}

static void
platform_qemu_uart_intr(void)
{
  while(1){
    int c = platform_uart_getc();
    if(c == -1)
      break;
    consoleintr(c);
  }

  acquire(&qemu_uart.lock);
  platform_qemu_uart_start();
  release(&qemu_uart.lock);
}

//
// QEMU virtio-mmio block device
//

#define VIRTIO_REG(reg) ((volatile uint32 *)(QEMU_VIRTIO0 + (reg)))

struct {
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;

  char free[NUM];
  uint16 used_idx;

  struct {
    char status;
    int done;
  } info[NUM];

  struct virtio_blk_req ops[NUM];
  struct spinlock lock;
} qemu_disk;

static int
platform_qemu_alloc_desc(void)
{
  for(int i = 0; i < NUM; i++){
    if(qemu_disk.free[i]){
      qemu_disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

static void
platform_qemu_free_desc(int i)
{
  if(i >= NUM)
    panic("platform_qemu_free_desc 1");
  if(qemu_disk.free[i])
    panic("platform_qemu_free_desc 2");

  qemu_disk.desc[i].addr = 0;
  qemu_disk.desc[i].len = 0;
  qemu_disk.desc[i].flags = 0;
  qemu_disk.desc[i].next = 0;
  qemu_disk.free[i] = 1;
  wakeup(&qemu_disk.free[0]);
}

static void
platform_qemu_free_chain(int i)
{
  while(1){
    int flags = qemu_disk.desc[i].flags;
    int next = qemu_disk.desc[i].next;
    platform_qemu_free_desc(i);
    if(flags & VRING_DESC_F_NEXT)
      i = next;
    else
      break;
  }
}

static int
platform_qemu_alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = platform_qemu_alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        platform_qemu_free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

static void
platform_qemu_disk_rw(uint blockno, uchar *buf, int write)
{
  uint32 sector = blockno * (BSIZE / 512);
  int idx[3];

  acquire(&qemu_disk.lock);

  while(1){
    if(platform_qemu_alloc3_desc(idx) == 0)
      break;
    sleep(&qemu_disk.free[0], &qemu_disk.lock);
  }

  struct virtio_blk_req *hdr = &qemu_disk.ops[idx[0]];
  hdr->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
  hdr->reserved = 0;
  hdr->sector = sector;

  qemu_disk.desc[idx[0]].addr = (uint64)(uint32)hdr;
  qemu_disk.desc[idx[0]].len = sizeof(*hdr);
  qemu_disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  qemu_disk.desc[idx[0]].next = idx[1];

  qemu_disk.desc[idx[1]].addr = (uint64)(uint32)buf;
  qemu_disk.desc[idx[1]].len = BSIZE;
  qemu_disk.desc[idx[1]].flags = write ? 0 : VRING_DESC_F_WRITE;
  qemu_disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  qemu_disk.desc[idx[1]].next = idx[2];

  qemu_disk.info[idx[0]].status = 0xff;
  qemu_disk.info[idx[0]].done = 0;
  qemu_disk.desc[idx[2]].addr = (uint64)(uint32)&qemu_disk.info[idx[0]].status;
  qemu_disk.desc[idx[2]].len = 1;
  qemu_disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
  qemu_disk.desc[idx[2]].next = 0;

  qemu_disk.avail->ring[qemu_disk.avail->idx % NUM] = idx[0];

  __sync_synchronize();
  qemu_disk.avail->idx += 1;
  __sync_synchronize();

  *VIRTIO_REG(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  while(qemu_disk.info[idx[0]].done == 0)
    sleep(&qemu_disk.info[idx[0]], &qemu_disk.lock);

  platform_qemu_free_chain(idx[0]);

  release(&qemu_disk.lock);

  if(qemu_disk.info[idx[0]].status != 0)
    panic("platform_qemu_disk_rw status");
}

void
platform_disk_init(void)
{
  uint32 status = 0;

  initlock(&qemu_disk.lock, "platform_disk");

  if(*VIRTIO_REG(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *VIRTIO_REG(VIRTIO_MMIO_VERSION) != 2 ||
     *VIRTIO_REG(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *VIRTIO_REG(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551)
    panic("platform_disk_init: no virtio disk");

  *VIRTIO_REG(VIRTIO_MMIO_STATUS) = status;

  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *VIRTIO_REG(VIRTIO_MMIO_STATUS) = status;

  status |= VIRTIO_CONFIG_S_DRIVER;
  *VIRTIO_REG(VIRTIO_MMIO_STATUS) = status;

  uint32 features = *VIRTIO_REG(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *VIRTIO_REG(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *VIRTIO_REG(VIRTIO_MMIO_STATUS) = status;

  status = *VIRTIO_REG(VIRTIO_MMIO_STATUS);
  if((status & VIRTIO_CONFIG_S_FEATURES_OK) == 0)
    panic("platform_disk_init: FEATURES_OK unset");

  *VIRTIO_REG(VIRTIO_MMIO_QUEUE_SEL) = 0;
  if(*VIRTIO_REG(VIRTIO_MMIO_QUEUE_READY))
    panic("platform_disk_init: queue already ready");

  uint32 max = *VIRTIO_REG(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0)
    panic("platform_disk_init: no queue 0");
  if(max < NUM)
    panic("platform_disk_init: short queue");

  qemu_disk.desc = kalloc();
  qemu_disk.avail = kalloc();
  qemu_disk.used = kalloc();
  if(qemu_disk.desc == 0 || qemu_disk.avail == 0 || qemu_disk.used == 0)
    panic("platform_disk_init: kalloc");

  memset(qemu_disk.desc, 0, PGSIZE);
  memset(qemu_disk.avail, 0, PGSIZE);
  memset(qemu_disk.used, 0, PGSIZE);

  *VIRTIO_REG(VIRTIO_MMIO_QUEUE_NUM) = NUM;
  *VIRTIO_REG(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint32)qemu_disk.desc;
  *VIRTIO_REG(VIRTIO_MMIO_QUEUE_DESC_HIGH) = 0;
  *VIRTIO_REG(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint32)qemu_disk.avail;
  *VIRTIO_REG(VIRTIO_MMIO_DRIVER_DESC_HIGH) = 0;
  *VIRTIO_REG(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint32)qemu_disk.used;
  *VIRTIO_REG(VIRTIO_MMIO_DEVICE_DESC_HIGH) = 0;
  *VIRTIO_REG(VIRTIO_MMIO_QUEUE_READY) = 1;

  for(int i = 0; i < NUM; i++)
    qemu_disk.free[i] = 1;

  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *VIRTIO_REG(VIRTIO_MMIO_STATUS) = status;
}

void
platform_disk_read(uint blockno, uchar *buf)
{
  platform_qemu_disk_rw(blockno, buf, 0);
}

void
platform_disk_write(uint blockno, uchar *buf)
{
  platform_qemu_disk_rw(blockno, buf, 1);
}

static void
platform_qemu_disk_intr(void)
{
  acquire(&qemu_disk.lock);

  *VIRTIO_REG(VIRTIO_MMIO_INTERRUPT_ACK) =
    *VIRTIO_REG(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  while(qemu_disk.used_idx != qemu_disk.used->idx){
    __sync_synchronize();

    int id = qemu_disk.used->ring[qemu_disk.used_idx % NUM].id;
    if(qemu_disk.info[id].status != 0)
      panic("platform_qemu_disk_intr: bad status");

    qemu_disk.info[id].done = 1;
    wakeup(&qemu_disk.info[id]);
    qemu_disk.used_idx += 1;
  }

  release(&qemu_disk.lock);
}

//
// IRQ, timer, and page-table mapping
//

void
platform_early_init(void)
{
  /*
   * QEMU does not need extra board-specific work beyond the generic
   * machine-mode setup that still lives in kernel/start.c.
   */
}

void
platform_irq_init(void)
{
  *(uint32 *)(QEMU_PLIC + QEMU_UART0_IRQ * 4) = 1;
  *(uint32 *)(QEMU_PLIC + QEMU_VIRTIO0_IRQ * 4) = 1;
}

void
platform_irq_init_hart(void)
{
  int hart = cpuid();
  uint32 enables = (1 << QEMU_UART0_IRQ) | (1 << QEMU_VIRTIO0_IRQ);

  *(uint32 *)QEMU_PLIC_SENABLE(hart) = enables;
  *(uint32 *)QEMU_PLIC_SPRIORITY(hart) = 0;
}

int
platform_irq_claim(void)
{
  return *(uint32 *)QEMU_PLIC_SCLAIM(cpuid());
}

void
platform_irq_complete(int irq)
{
  *(uint32 *)QEMU_PLIC_SCLAIM(cpuid()) = irq;
}

void
platform_timer_init(void)
{
  int id = r_mhartid();
  uint32 *scratch = &timer_scratch[id][0];

  *(uint64 *)QEMU_CLINT_MTIMECMP(id) =
    *(uint64 *)QEMU_CLINT_MTIME + QEMU_TIMER_INTERVAL;

  scratch[4] = QEMU_CLINT_MTIMECMP(id);
  scratch[5] = QEMU_TIMER_INTERVAL;
  w_mscratch((uint32)scratch);

  w_mtvec((uint32)timervec);
  w_mstatus(r_mstatus() | MSTATUS_MIE);
  w_mie(r_mie() | MIE_MTIE);
}

void
platform_timer_ack(void)
{
  w_sip(r_sip() & ~2);
}

void
platform_kvmmap(pagetable_t kpgtbl)
{
  kvmmap(kpgtbl, QEMU_UART0, QEMU_UART0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, QEMU_VIRTIO0, QEMU_VIRTIO0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, QEMU_PLIC, QEMU_PLIC, 0x400000, PTE_R | PTE_W);
}

int
platform_handle_irq(void)
{
  uint32 scause = r_scause();

  if((scause & 0x80000000L) && (scause & 0xff) == 9){
    int irq = platform_irq_claim();

    if(irq == QEMU_UART0_IRQ){
      platform_qemu_uart_intr();
    } else if(irq == QEMU_VIRTIO0_IRQ){
      platform_qemu_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    if(irq)
      platform_irq_complete(irq);

    return PLATFORM_INTR_DEV;
  }

  if(scause == 0x80000001L){
    platform_timer_ack();
    return PLATFORM_INTR_TIMER;
  }

  return PLATFORM_INTR_NONE;
}
