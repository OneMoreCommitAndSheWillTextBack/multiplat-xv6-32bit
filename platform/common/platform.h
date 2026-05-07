#ifndef XV6_PLATFORM_H
#define XV6_PLATFORM_H

#include "types.h"
#include "riscv.h"

struct buf;

/*
 * Thin platform boundary for board and emulator specific code.
 * Kernel code should use this interface instead of reaching into
 * board/emulator-specific MMIO layouts directly.
 */
void platform_early_init(void);

enum {
  PLATFORM_INTR_NONE = 0,
  PLATFORM_INTR_DEV = 1,
  PLATFORM_INTR_TIMER = 2,
};

void platform_irq_init(void);
void platform_irq_init_hart(void);
int  platform_irq_claim(void);
void platform_irq_complete(int irq);
int  platform_handle_irq(void);

void platform_timer_init(void);
void platform_timer_ack(void);
void timervec(void);

void platform_uart_init(void);
int  platform_uart_getc(void);
void platform_uart_putc(int c);
void platform_uart_putc_sync(int c);

void platform_disk_init(void);
void platform_disk_read(uint blockno, uchar *buf);
void platform_disk_write(uint blockno, uchar *buf);

void platform_kvmmap(pagetable_t kpgtbl);

#endif
