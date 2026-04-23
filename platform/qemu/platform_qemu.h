#ifndef XV6_PLATFORM_QEMU_IMPL_H
#define XV6_PLATFORM_QEMU_IMPL_H

#include "types.h"

/*
 * QEMU virt board details that should stay out of generic kernel headers.
 * These helpers are intentionally private to the QEMU platform layer.
 */

#define QEMU_UART0 0x10000000L
#define QEMU_UART0_IRQ 10

#define QEMU_VIRTIO0 0x10001000L
#define QEMU_VIRTIO0_IRQ 1

#define QEMU_CLINT 0x02000000L
#define QEMU_CLINT_MTIMECMP(hartid) (QEMU_CLINT + 0x4000 + 8 * (hartid))
#define QEMU_CLINT_MTIME (QEMU_CLINT + 0xBFF8)
#define QEMU_TIMER_INTERVAL 3200000

#define QEMU_PLIC 0x0c000000L
#define QEMU_PLIC_SENABLE(hart) (QEMU_PLIC + 0x2080 + (hart) * 0x100)
#define QEMU_PLIC_SPRIORITY(hart) (QEMU_PLIC + 0x201000 + (hart) * 0x2000)
#define QEMU_PLIC_SCLAIM(hart) (QEMU_PLIC + 0x201004 + (hart) * 0x2000)

#endif
