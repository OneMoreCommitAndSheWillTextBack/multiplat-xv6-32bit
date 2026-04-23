# QEMU launch scaffold for the xv6 rv32 build.

PLATFORM_DEFAULT_GOAL := run
PLATFORM_EXTRA_PHONY += run gdb
PLATFORM_KERNEL_SRCS += kernel/virtio_disk.c
PLATFORM_CFLAGS += -DXV6_PLATFORM_QEMU

QEMU ?= qemu-system-riscv32
QEMU_MACHINE ?= virt
QEMU_BIOS ?= none
QEMU_MEM ?= 128M
GDBARCH ?= riscv:rv32
GDBINIT_TMPL ?= $(XV6_HOME)/.gdbinit.tmpl-riscv
GDBINIT ?= $(XV6_HOME)/.gdbinit

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

QEMUOPTS = -machine $(QEMU_MACHINE)
QEMUOPTS += -bios $(QEMU_BIOS)
QEMUOPTS += -kernel $(KERNEL_ELF)
QEMUOPTS += -m $(QEMU_MEM)
QEMUOPTS += -smp $(CPUS)
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -nographic
QEMUOPTS += -drive file=$(FS_IMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

run: bin fs.img
	$(QEMU) $(QEMUOPTS)

$(GDBINIT): $(GDBINIT_TMPL)
	@sed \
	  -e "s|:1234|:$(GDBPORT)|" \
	  -e "s|riscv:rv64|$(GDBARCH)|" \
	  -e "s|kernel/kernel|$(KERNEL_ELF)|" \
	  < $< > $@

gdb: bin fs.img $(GDBINIT)
	@echo "*** Now run 'gdb' in another window." 1>&2
	@echo "*** GDB port: $(GDBPORT)" 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)
