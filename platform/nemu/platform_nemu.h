#ifndef XV6_PLATFORM_NEMU_IMPL_H
#define XV6_PLATFORM_NEMU_IMPL_H

#include "types.h"

/*
 * NEMU board details that should stay out of generic kernel headers.
 * These helpers are intentionally private to the NEMU platform layer.
 */

// NEMU MMIO device page.
#define NEMU_MMIO 0xa0000000

// NEMU serial (16550A-compatible minimal subset at CONFIG_SERIAL_MMIO)
#define NEMU_UART0 0xa00003f8
#define NEMU_UART0_IRQ 10

// NEMU i8042 keyboard data register (CONFIG_I8042_DATA_MMIO)
#define NEMU_KBD 0xa0000060

/*
 * NEMU's current riscv32 timer path is host-driven and injects periodic
 * machine timer interrupts directly, so the guest does not rely on a
 * CLINT mtime/mtimecmp programming model here.
 */

// Platform-Level Interrupt Controller (PLIC)
#define NEMU_PLIC 0x0c000000L
#define NEMU_PLIC_SENABLE(hart) (NEMU_PLIC + 0x2080 + (hart) * 0x100)
#define NEMU_PLIC_SPRIORITY(hart) (NEMU_PLIC + 0x201000 + (hart) * 0x2000)
#define NEMU_PLIC_SCLAIM(hart) (NEMU_PLIC + 0x201004 + (hart) * 0x2000)

// NEMU disk controller (CONFIG_DISK_CTL_MMIO)
#define NEMU_DISK 0xa0000300
#define NEMU_DISK_IRQ 1

#endif
