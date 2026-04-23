Platform support for xv6 lives here.

The current goal of this layer is:
- keep `kernel/` on the `platform_*` API in `platform/common/platform.h`
- move board/emulator MMIO details into `platform/<name>/`
- make a new port mostly a matter of adding `platform/<name>/platform.c`
  and `platform/<name>/platform.mk`

Layout
- `common/`: shared platform-facing API
- `qemu/`: QEMU `virt` implementation
- `nemu/`: NEMU implementation

Build entry
- normal builds must pass `PLATFORM=<name>`
- run `make platform` to list supported values
- each platform must provide `platform/<name>/platform.mk`

Common API

All generic kernel code should only call the API below:

```c
void platform_early_init(void);

void platform_irq_init(void);
void platform_irq_init_hart(void);
int  platform_irq_claim(void);
void platform_irq_complete(int irq);
int  platform_handle_irq(void);

void platform_timer_init(void);
void platform_timer_ack(void);

void platform_uart_init(void);
int  platform_uart_getc(void);
void platform_uart_putc(int c);
void platform_uart_putc_sync(int c);

void platform_disk_init(void);
void platform_disk_read(uint blockno, uchar *buf);
void platform_disk_write(uint blockno, uchar *buf);

void platform_kvmmap(pagetable_t kpgtbl);
```

API responsibilities

- `platform_early_init()`
  Runs in `kernel/start.c` before entering the scheduler path.
  Use this for board-specific early machine-mode setup if the generic
  startup code is not enough.

- `platform_irq_init()`
  Global interrupt-controller setup.
  Typical work: set device interrupt priority, enable global interrupt
  sources in the platform interrupt controller.

- `platform_irq_init_hart()`
  Per-hart interrupt setup.
  Typical work: enable UART/disk IRQ lines for the current hart and set
  per-hart threshold registers.

- `platform_irq_claim()` / `platform_irq_complete()`
  Low-level claim/complete helpers for external device interrupts.
  These are mainly useful inside the platform implementation.

- `platform_handle_irq()`
  Main interrupt dispatcher for the platform.
  This function must:
  - inspect `scause`
  - handle external device interrupts
  - handle timer/software interrupts
  - call device-side handlers as needed
  - acknowledge/complete the interrupt
  - return one of:
    - `PLATFORM_INTR_NONE`
    - `PLATFORM_INTR_DEV`
    - `PLATFORM_INTR_TIMER`

- `platform_timer_init()`
  Machine-mode timer setup.
  In the current tree this is expected to:
  - program the next timer event
  - populate `timer_scratch[NCPU][6]`
  - install `timervec`
  - enable machine timer interrupts

- `platform_timer_ack()`
  Acknowledge the timer interrupt after it has been forwarded to S-mode.
  On the current RISC-V path this usually means clearing `SSIP`.

- `platform_uart_init()`
  Initialize the console UART.
  It must leave the device ready for:
  - polling input via `platform_uart_getc()`
  - buffered output via `platform_uart_putc()`
  - synchronous panic/printf output via `platform_uart_putc_sync()`

- `platform_uart_getc()`
  Return one input byte if available, otherwise `-1`.

- `platform_uart_putc(int c)`
  Normal console output path.
  This should support the current console semantics used by
  `kernel/console.c`, which may sleep and use interrupt-driven transmit.

- `platform_uart_putc_sync(int c)`
  Synchronous output path used by `printf`/panic and console echo.
  This must not depend on the normal buffered transmit path.

- `platform_disk_init()`
  Initialize the block device used as `ROOTDEV`.

- `platform_disk_read(uint blockno, uchar *buf)`
  Read one `BSIZE` block into `buf`.
  Today `kernel/bio.c` calls this with buffer-cache memory.
  If the platform DMA engine cannot safely access arbitrary virtual
  addresses, the platform implementation must handle that internally.

- `platform_disk_write(uint blockno, uchar *buf)`
  Write one `BSIZE` block from `buf`.

- `platform_kvmmap(pagetable_t kpgtbl)`
  Add platform MMIO mappings to the kernel page table.
  Typical work: map UART, disk MMIO window, interrupt controller registers.

Required platform-private items

There is no single globally-required macro set in `kernel/` anymore.
Instead, each platform should keep its hardware constants in a private
header such as `platform/<name>/platform_<name>.h`.

At minimum, a real RISC-V machine/platform implementation usually needs
macros for:
- UART base address
- UART IRQ number
- disk/MMIO base address
- disk IRQ number
- timer/CLINT base address
- timer compare and time register helpers
- interrupt controller base address
- per-hart enable / priority / claim-complete register helpers
- timer interval constant

QEMU example

`platform/qemu/platform_qemu.h` currently defines:
- `QEMU_UART0`
- `QEMU_UART0_IRQ`
- `QEMU_VIRTIO0`
- `QEMU_VIRTIO0_IRQ`
- `QEMU_CLINT`
- `QEMU_CLINT_MTIMECMP(hartid)`
- `QEMU_CLINT_MTIME`
- `QEMU_TIMER_INTERVAL`
- `QEMU_PLIC`
- `QEMU_PLIC_SENABLE(hart)`
- `QEMU_PLIC_SPRIORITY(hart)`
- `QEMU_PLIC_SCLAIM(hart)`

These names are not mandatory for other platforms; they are only an
example of the kind of hardware macros that must exist somewhere.

Required `platform.mk` settings

Each platform must provide `platform/<name>/platform.mk`.

The minimum required settings are:
- `PLATFORM_KERNEL_SRCS += platform/<name>/platform.c`

Useful optional settings are:
- `PLATFORM_DEFAULT_GOAL := run`
- `PLATFORM_EXTRA_PHONY += run`
- `PLATFORM_CFLAGS += ...`
- platform-specific launcher variables such as `QEMU`, `QEMUOPTS`,
  `NEMU_HOME`, `NEMUFLAGS`

For the current tree, a new platform should not rely on `kernel/`
checking a platform-specific compile-time macro such as
`XV6_PLATFORM_QEMU`; the intention is that behavior differences stay in
the platform implementation instead.

Porting checklist

To add a new platform `foo`:
- add `platform/foo/platform.c`
- optionally add `platform/foo/platform_foo.h` for MMIO/IRQ macros
- add `platform/foo/platform.mk`
- implement every function in `platform/common/platform.h`
- register `foo` in `ALLOWED_PLATFORMS` in the top-level `Makefile`
- verify:
  - boot reaches `init`
  - console input/output works
  - timer interrupts advance `ticks`
  - root filesystem mounts
  - `usertests` runs

Current notes

- `platform_disk_read()` / `platform_disk_write()` are intentionally simple,
  but some DMA-backed platforms may eventually prefer a higher-level
  request interface.
- `platform_handle_irq()` is the key API that keeps `kernel/trap.c`
  platform-neutral.
