#include "platform.h"
#include "platform_nemu.h"

#include "defs.h"
#include "memlayout.h"
#include "param.h"

/*
 * NEMU implementation of the common platform seam.
 *
 * Device layout (from NEMU autoconf.h):
 *   serial  : CONFIG_SERIAL_MMIO  = 0xa00003f8
 *   keyboard: CONFIG_I8042_DATA_MMIO = 0xa0000060
 *   rtc     : CONFIG_RTC_MMIO     = 0xa0000048
 *   disk    : CONFIG_DISK_CTL_MMIO = 0xa0000300
 *   plic    : PLIC_BASE           = 0x0c000000
 *
 * Timer model:
 *   NEMU may deliver timer interrupts directly as supervisor timer
 *   interrupts (STIP, scause=5), or via the older machine-timer ->
 *   SSIP forwarding path (scause=1).  The guest accepts both.
 */

extern uint32 timer_scratch[NCPU][6];
extern void timervec(void);

//
// NEMU UART (minimal serial, byte-write to offset 0 only)
//

static volatile char *nemu_uart = (volatile char *)NEMU_UART0;

void platform_uart_init(void) {
  /*
   * NEMU serial requires no initialization; the device is ready
   * as soon as NEMU starts.  Only byte writes to offset 0 are
   * supported (THR).  Console input is polled from NEMU keyboard.
   */
}

void platform_uart_putc(int c) { *nemu_uart = (char)c; }

void platform_uart_putc_sync(int c) { *nemu_uart = (char)c; }

//
// NEMU keyboard, used as console input because NEMU serial is write-only.
//

#define NEMU_KEYDOWN_MASK 0x8000

#define NEMU_KEYS(_)                                                         \
  _(ESCAPE) _(F1) _(F2) _(F3) _(F4) _(F5) _(F6) _(F7) _(F8) _(F9) _(F10)     \
    _(F11) _(F12) _(GRAVE) _(1) _(2) _(3) _(4) _(5) _(6) _(7) _(8) _(9)      \
    _(0) _(MINUS) _(EQUALS) _(BACKSPACE) _(TAB) _(Q) _(W) _(E) _(R) _(T)     \
    _(Y) _(U) _(I) _(O) _(P) _(LEFTBRACKET) _(RIGHTBRACKET) _(BACKSLASH)     \
    _(CAPSLOCK) _(A) _(S) _(D) _(F) _(G) _(H) _(J) _(K) _(L) _(SEMICOLON)    \
    _(APOSTROPHE) _(RETURN) _(LSHIFT) _(Z) _(X) _(C) _(V) _(B) _(N) _(M)     \
    _(COMMA) _(PERIOD) _(SLASH) _(RSHIFT) _(LCTRL) _(APPLICATION) _(LALT)    \
    _(SPACE) _(RALT) _(RCTRL) _(UP) _(DOWN) _(LEFT) _(RIGHT) _(INSERT)       \
    _(DELETE) _(HOME) _(END) _(PAGEUP) _(PAGEDOWN)

#define NEMU_KEY_NAME(k) NEMU_KEY_##k,
enum {
  NEMU_KEY_NONE = 0,
  NEMU_KEYS(NEMU_KEY_NAME)
};
#undef NEMU_KEY_NAME
#undef NEMU_KEYS

static volatile uint32 *nemu_kbd = (volatile uint32 *)NEMU_KBD;
static int nemu_shift_down;
static int nemu_ctrl_down;
static int nemu_caps_lock;

static int platform_nemu_key_letter(int key) {
  switch (key) {
  case NEMU_KEY_A:
    return 'a';
  case NEMU_KEY_B:
    return 'b';
  case NEMU_KEY_C:
    return 'c';
  case NEMU_KEY_D:
    return 'd';
  case NEMU_KEY_E:
    return 'e';
  case NEMU_KEY_F:
    return 'f';
  case NEMU_KEY_G:
    return 'g';
  case NEMU_KEY_H:
    return 'h';
  case NEMU_KEY_I:
    return 'i';
  case NEMU_KEY_J:
    return 'j';
  case NEMU_KEY_K:
    return 'k';
  case NEMU_KEY_L:
    return 'l';
  case NEMU_KEY_M:
    return 'm';
  case NEMU_KEY_N:
    return 'n';
  case NEMU_KEY_O:
    return 'o';
  case NEMU_KEY_P:
    return 'p';
  case NEMU_KEY_Q:
    return 'q';
  case NEMU_KEY_R:
    return 'r';
  case NEMU_KEY_S:
    return 's';
  case NEMU_KEY_T:
    return 't';
  case NEMU_KEY_U:
    return 'u';
  case NEMU_KEY_V:
    return 'v';
  case NEMU_KEY_W:
    return 'w';
  case NEMU_KEY_X:
    return 'x';
  case NEMU_KEY_Y:
    return 'y';
  case NEMU_KEY_Z:
    return 'z';
  default:
    return -1;
  }
}

static int platform_nemu_key_char(int key, int shifted) {
  switch (key) {
  case NEMU_KEY_1:
    return shifted ? '!' : '1';
  case NEMU_KEY_2:
    return shifted ? '@' : '2';
  case NEMU_KEY_3:
    return shifted ? '#' : '3';
  case NEMU_KEY_4:
    return shifted ? '$' : '4';
  case NEMU_KEY_5:
    return shifted ? '%' : '5';
  case NEMU_KEY_6:
    return shifted ? '^' : '6';
  case NEMU_KEY_7:
    return shifted ? '&' : '7';
  case NEMU_KEY_8:
    return shifted ? '*' : '8';
  case NEMU_KEY_9:
    return shifted ? '(' : '9';
  case NEMU_KEY_0:
    return shifted ? ')' : '0';
  case NEMU_KEY_GRAVE:
    return shifted ? '~' : '`';
  case NEMU_KEY_MINUS:
    return shifted ? '_' : '-';
  case NEMU_KEY_EQUALS:
    return shifted ? '+' : '=';
  case NEMU_KEY_LEFTBRACKET:
    return shifted ? '{' : '[';
  case NEMU_KEY_RIGHTBRACKET:
    return shifted ? '}' : ']';
  case NEMU_KEY_BACKSLASH:
    return shifted ? '|' : '\\';
  case NEMU_KEY_SEMICOLON:
    return shifted ? ':' : ';';
  case NEMU_KEY_APOSTROPHE:
    return shifted ? '"' : '\'';
  case NEMU_KEY_COMMA:
    return shifted ? '<' : ',';
  case NEMU_KEY_PERIOD:
    return shifted ? '>' : '.';
  case NEMU_KEY_SLASH:
    return shifted ? '?' : '/';
  default:
    return -1;
  }
}

static int platform_nemu_keyboard_event(uint32 scancode) {
  int keydown = (scancode & NEMU_KEYDOWN_MASK) != 0;
  int key = scancode & ~NEMU_KEYDOWN_MASK;

  switch (key) {
  case NEMU_KEY_LSHIFT:
  case NEMU_KEY_RSHIFT:
    nemu_shift_down = keydown;
    return -1;
  case NEMU_KEY_LCTRL:
  case NEMU_KEY_RCTRL:
    nemu_ctrl_down = keydown;
    return -1;
  case NEMU_KEY_CAPSLOCK:
    if (keydown)
      nemu_caps_lock = !nemu_caps_lock;
    return -1;
  }

  if (!keydown)
    return -1;

  int c = platform_nemu_key_letter(key);
  if (c != -1) {
    if (nemu_ctrl_down)
      return c - 'a' + 1;
    if (nemu_shift_down ^ nemu_caps_lock)
      return c - 'a' + 'A';
    return c;
  }

  switch (key) {
  case NEMU_KEY_ESCAPE:
    return 0x1b;
  case NEMU_KEY_BACKSPACE:
  case NEMU_KEY_DELETE:
    return 0x7f;
  case NEMU_KEY_TAB:
    return '\t';
  case NEMU_KEY_RETURN:
    return '\r';
  case NEMU_KEY_SPACE:
    return ' ';
  }

  return platform_nemu_key_char(key, nemu_shift_down);
}

int platform_uart_getc(void) {
  while (1) {
    uint32 scancode = *nemu_kbd;

    if ((scancode & ~NEMU_KEYDOWN_MASK) == NEMU_KEY_NONE)
      return -1;

    int c = platform_nemu_keyboard_event(scancode);
    if (c != -1)
      return c;
  }
}

static void platform_nemu_keyboard_intr(void) {
  while (1) {
    int c = platform_uart_getc();
    if (c == -1)
      break;
    consoleintr(c);
  }
}

//
// NEMU disk controller (command-based MMIO at 0xa0000300)
//
// Register layout (each uint32):
//   Write path:  [0]=cmd, [1]=buf(GPA), [2]=blkno, [3]=blkcnt, [4]=valid
//   Read path:   [0]=present, [1]=blksz, [2]=blkcnt, [3]=ready, [4]=reserved
//
// cmd values:  0=none, 1=write, 2=read
//

enum {
  DISK_REG_CMD = 0,
  DISK_REG_BUF = 1,
  DISK_REG_BLKNO = 2,
  DISK_REG_BLKCNT = 3,
  DISK_REG_VALID = 4,
};

enum {
  DISK_CMD_NONE = 0,
  DISK_CMD_WRITE = 1,
  DISK_CMD_READ = 2,
};

static volatile uint32 *nemu_disk = (volatile uint32 *)NEMU_DISK;

void platform_disk_init(void) {
  /*
   * NEMU disk controller is initialized by NEMU itself.
   * No additional setup needed on the guest side.
   */
}

void platform_disk_read(uint blockno, uchar *buf) {
  nemu_disk[DISK_REG_CMD] = DISK_CMD_READ;
  nemu_disk[DISK_REG_BUF] = (uint32)buf;
  nemu_disk[DISK_REG_BLKNO] = blockno;
  nemu_disk[DISK_REG_BLKCNT] = 1;
  nemu_disk[DISK_REG_VALID] = 1;
}

void platform_disk_write(uint blockno, uchar *buf) {
  nemu_disk[DISK_REG_CMD] = DISK_CMD_WRITE;
  nemu_disk[DISK_REG_BUF] = (uint32)buf;
  nemu_disk[DISK_REG_BLKNO] = blockno;
  nemu_disk[DISK_REG_BLKCNT] = 1;
  nemu_disk[DISK_REG_VALID] = 1;
}

//
// IRQ, timer, and page-table mapping
//

void platform_early_init(void) {}

void platform_irq_init(void) {
  // Set UART IRQ priority in PLIC.
  *(uint32 *)(NEMU_PLIC + NEMU_UART0_IRQ * 4) = 1;
}

void platform_irq_init_hart(void) {
  int hart = cpuid();
  uint32 enables = (1 << NEMU_UART0_IRQ);

  *(uint32 *)NEMU_PLIC_SENABLE(hart) = enables;
  *(uint32 *)NEMU_PLIC_SPRIORITY(hart) = 0;
}

int platform_irq_claim(void) { return *(uint32 *)NEMU_PLIC_SCLAIM(cpuid()); }

void platform_irq_complete(int irq) {
  *(uint32 *)NEMU_PLIC_SCLAIM(cpuid()) = irq;
}

int platform_handle_irq(void) {
  uint32 scause = r_scause();

  if ((scause & 0x80000000L) && (scause & 0xff) == 9) {
    int irq = platform_irq_claim();

    if (irq == NEMU_UART0_IRQ) {
      // NEMU serial does not generate interrupts, but handle it for
      // future extensions.
    } else if (irq) {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    if (irq)
      platform_irq_complete(irq);

    return PLATFORM_INTR_DEV;
  }

  if (scause == 0x80000001L || scause == 0x80000005L) {
    platform_timer_ack();
    platform_nemu_keyboard_intr();
    return PLATFORM_INTR_TIMER;
  }

  return PLATFORM_INTR_NONE;
}

void platform_timer_init(void) {
  int id = r_mhartid();
  uint32 *scratch = &timer_scratch[id][0];

  /*
   * NEMU's timer device injects periodic machine timer interrupts on its
   * own. The platform-specific timervec only uses this scratch area as a
   * tiny register save buffer before forwarding the interrupt to S-mode.
   */
  scratch[4] = 0;
  scratch[5] = 0;
  w_mscratch((uint32)scratch);

  w_mtvec((uint32)timervec);
  w_mstatus(r_mstatus() | MSTATUS_MIE);
  w_mie(r_mie() | MIE_MTIE);
}

void platform_timer_ack(void) {
  uint32 scause = r_scause();
  uint32 sip = r_sip();

  if (scause == 0x80000001L) {
    sip &= ~SIE_SSIE;
  } else if (scause == 0x80000005L) {
    sip &= ~SIE_STIE;
  }

  w_sip(sip);
}

void platform_kvmmap(pagetable_t kpgtbl) {
  kvmmap(kpgtbl, NEMU_MMIO, NEMU_MMIO, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, NEMU_PLIC, NEMU_PLIC, 0x400000, PTE_R | PTE_W);
}
