# NEMU is the only supported platform for now.

PLATFORM_DEFAULT_GOAL := run
PLATFORM_EXTRA_PHONY += run check-nemu-home gdb image
PLATFORM_KERNEL_SRCS += platform/nemu/platform.c
PLATFORM_KERNEL_SRCS += platform/nemu/timervec.S
PLATFORM_CFLAGS += -DXV6_PLATFORM_NEMU

NEMU_IMG ?= $(KERNEL_BIN)
NEMU_ELF ?= $(KERNEL_ELF)
NEMU_LOG ?= $(BUILD_DIR)/nemu-log.txt

NEMUFLAGS += -l $(NEMU_LOG)
NEMUFLAGS += -f $(NEMU_ELF)
NEMUFLAGS += -b
NEMUFLAGS += --disk-path=$(FS_IMG)

image: fs.img

check-nemu-home:
	@test -n "$(NEMU_HOME)" || { echo "NEMU_HOME is not set" >&2; exit 1; }

run: check-nemu-home bin fs.img
	$(MAKE) -C $(NEMU_HOME) NEMU_HOME=$(NEMU_HOME) ISA=riscv32 run ARGS="$(NEMUFLAGS)" IMG=$(NEMU_IMG)

gdb: bin fs.img
	$(MAKE) -C $(NEMU_HOME) NEMU_HOME=$(NEMU_HOME) ISA=riscv32 gdb ARGS="$(NEMUFLAGS)" IMG=$(NEMU_IMG)
