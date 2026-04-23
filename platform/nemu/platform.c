#include "platform.h"

#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "sdcard.h"

extern uint32 timer_scratch[NCPU][6];
extern void timervec(void);

void
platform_early_init(void)
{
}

void
platform_irq_init(void)
{
  plicinit();
}

void
platform_irq_init_hart(void)
{
  plicinithart();
}

int
platform_irq_claim(void)
{
  return plic_claim();
}

void
platform_irq_complete(int irq)
{
  plic_complete(irq);
}

int
platform_handle_irq(void)
{
  uint32 scause = r_scause();

  if((scause & 0x80000000L) && (scause & 0xff) == 9){
    int irq = platform_irq_claim();

    if(irq == UART0_IRQ){
      uartintr();
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

void
platform_timer_init(void)
{
  int id = r_mhartid();
  uint32 *scratch = &timer_scratch[id][0];

  *(uint64 *)CLINT_MTIMECMP(id) = *(uint64 *)CLINT_MTIME + 3200000;
  scratch[4] = CLINT_MTIMECMP(id);
  scratch[5] = 3200000;
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
platform_uart_init(void)
{
  uartinit();
}

int
platform_uart_getc(void)
{
  return uartgetc();
}

void
platform_uart_putc(int c)
{
  uartputc(c);
}

void
platform_uart_putc_sync(int c)
{
  uartputc_sync(c);
}

void
platform_disk_init(void)
{
  spi_init();
}

void
platform_disk_read(uint blockno, uchar *buf)
{
  spi_rb(blockno, buf);
}

void
platform_disk_write(uint blockno, uchar *buf)
{
  spi_wb(blockno, buf);
}

void
platform_kvmmap(pagetable_t kpgtbl)
{
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, SDCARD, SDCARD, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
}
