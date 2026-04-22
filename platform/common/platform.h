#ifndef XV6_PLATFORM_H
#define XV6_PLATFORM_H

#include "types.h"
#include "riscv.h"

struct buf;

/*
 * Thin platform boundary for board and emulator specific code.
 * The current tree still uses the legacy in-kernel implementation;
 * these declarations define the intended seam for later refactoring.
 */
void platform_early_init(void);

void platform_irq_init(void);
void platform_irq_init_hart(void);
int  platform_irq_claim(void);
void platform_irq_complete(int irq);

void platform_timer_init(void);
void platform_timer_ack(void);

void platform_uart_init(void);
int  platform_uart_getc(void);
void platform_uart_putc_sync(int c);

void platform_disk_init(void);
void platform_disk_read(uint blockno, uchar *buf);
void platform_disk_write(uint blockno, uchar *buf);

void platform_kvmmap(pagetable_t kpgtbl);

#endif
