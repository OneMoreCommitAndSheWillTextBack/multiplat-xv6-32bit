K=kernel
U=user
XV6_HOME := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

ALLOWED_PLATFORMS := nemu

ifneq ($(MAKECMDGOALS),platform)
ifndef PLATFORM
$(error PLATFORM is not set. Run 'make platform' to list supported platforms)
endif
ifeq ($(filter $(PLATFORM),$(ALLOWED_PLATFORMS)),)
$(error Unsupported PLATFORM '$(PLATFORM)'. Run 'make platform' to list supported platforms)
endif
endif

PLATFORM_DIR := platform/$(PLATFORM)
PLATFORM_MK := $(PLATFORM_DIR)/platform.mk

BUILD_ROOT ?= $(XV6_HOME)/build
BUILD_DIR ?= $(BUILD_ROOT)/$(PLATFORM)
KBUILD := $(BUILD_DIR)/kernel
UBUILD := $(BUILD_DIR)/user
MKFSBUILD := $(BUILD_DIR)/mkfs

PLATFORM_KERNEL_SRCS ?=
PLATFORM_CFLAGS ?=
PLATFORM_OBJS ?=
PLATFORM_DEFAULT_GOAL ?= bin
PLATFORM_EXTRA_PHONY ?=

KERNEL_ELF := $(KBUILD)/kernel
KERNEL_ASM := $(KBUILD)/kernel.asm
KERNEL_SYM := $(KBUILD)/kernel.sym
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
KERNEL_TXT := $(BUILD_DIR)/kernel.txt

NEMU_IMG := $(KERNEL_BIN)
NEMU_ELF := $(KERNEL_ELF)
NEMU_LOG := $(BUILD_DIR)/nemu-log.txt

INITCODE_OBJ := $(UBUILD)/initcode.o
INITCODE_OUT := $(UBUILD)/initcode.out
INITCODE_BIN := $(UBUILD)/initcode
INITCODE_ASM := $(UBUILD)/initcode.asm
USYS_SRC := $(UBUILD)/usys.S
USYS_OBJ := $(UBUILD)/usys.o

MKFS := $(MKFSBUILD)/mkfs
README_COPY := $(BUILD_DIR)/README
XV6_ELF_COPY := $(BUILD_DIR)/xv6.elf
FS_IMG := $(BUILD_DIR)/fs.img

ifdef PLATFORM
ifneq ($(filter $(PLATFORM),$(ALLOWED_PLATFORMS)),)
ifeq ($(wildcard $(PLATFORM_MK)),)
$(error Missing platform config: $(PLATFORM_MK))
endif
include $(PLATFORM_MK)
endif
endif

KERNEL_SRCS = \
  $K/entry.S \
  $K/start.c \
  $K/console.c \
  $K/printf.c \
  $K/uart.c \
  $K/kalloc.c \
  $K/spinlock.c \
  $K/string.c \
  $K/main.c \
  $K/vm.c \
  $K/proc.c \
  $K/swtch.S \
  $K/trampoline.S \
  $K/trap.c \
  $K/syscall.c \
  $K/sysproc.c \
  $K/bio.c \
  $K/fs.c \
  $K/log.c \
  $K/sleeplock.c \
  $K/file.c \
  $K/pipe.c \
  $K/exec.c \
  $K/sysfile.c \
  $K/kernelvec.S \
  $K/plic.c \
  $K/sdcard.c \
  $(PLATFORM_KERNEL_SRCS)

USER_PROG_NAMES = \
  cat \
  echo \
  forktest \
  grep \
  init \
  kill \
  ln \
  ls \
  mkdir \
  rm \
  sh \
  stressfs \
  usertests \
  grind \
  wc \
  zombie

src_to_obj = $(BUILD_DIR)/$(basename $(1)).o

KERNEL_OBJS = \
  $(foreach src,$(KERNEL_SRCS),$(call src_to_obj,$(src))) \
  $(PLATFORM_OBJS)

ULIB = \
  $(call src_to_obj,$U/ulib.c) \
  $(USYS_OBJ) \
  $(call src_to_obj,$U/printf.c) \
  $(call src_to_obj,$U/umalloc.c)

USER_PROG_OBJS = $(foreach prog,$(USER_PROG_NAMES),$(call src_to_obj,$U/$(prog).c))
UPROGS = $(addprefix $(UBUILD)/_,$(USER_PROG_NAMES))
UPROGS_REL = $(patsubst $(BUILD_DIR)/%,%,$(UPROGS))

# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
TOOLPREFIX = /usr/local/riscv/bin/riscv64-unknown-linux-gnu-
LIBGCC = /usr/local/riscv/lib/gcc/riscv64-unknown-linux-gnu/15.2.0/lib32/ilp32

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump
MAKE = make

CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer # -ggdb -gdwarf-2
CFLAGS += -march=rv32ia_zicsr
CFLAGS += -mabi=ilp32
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fcommon -nostdlib -mno-relax
CFLAGS += -I.
CFLAGS += -Ikernel
CFLAGS += -Iplatform/common
CFLAGS += -I$(PLATFORM_DIR)
CFLAGS += $(PLATFORM_CFLAGS)
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

LDFLAGS = -z max-page-size=4096 -melf32lriscv

.DEFAULT_GOAL := $(PLATFORM_DEFAULT_GOAL)

.PHONY: platform bin fs.img clean tags $(PLATFORM_EXTRA_PHONY)

platform:
	@echo "Supported PLATFORM values:"
	@for p in $(ALLOWED_PLATFORMS); do echo "  $$p"; done

bin: $(KERNEL_BIN) $(KERNEL_TXT)

$(KERNEL_TXT): $(KERNEL_ELF)
	mkdir -p $(dir $@)
	$(OBJDUMP) -d $< > $@

$(KERNEL_BIN): $(KERNEL_ELF)
	mkdir -p $(dir $@)
	@echo + OBJCOPY "->" $@
	@$(OBJCOPY) -S --set-section-flags .bss=alloc,contents -O binary $< $@

$(KERNEL_ELF): $(KERNEL_OBJS) $K/kernel.ld $(INITCODE_BIN)
	mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $@ $(KERNEL_OBJS) -L$(LIBGCC) -lgcc
	$(OBJDUMP) -S $@ > $(KERNEL_ASM)
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(KERNEL_SYM)

$(INITCODE_OUT): $(INITCODE_OBJ)
	mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $@ $< -L$(LIBGCC) -lgcc

$(INITCODE_BIN): $(INITCODE_OUT) $(INITCODE_OBJ)
	mkdir -p $(dir $@)
	$(OBJCOPY) -S -O binary $(INITCODE_OUT) $@
	$(OBJDUMP) -S $(INITCODE_OBJ) > $(INITCODE_ASM)

tags: $(KERNEL_OBJS) $(UBUILD)/_init
	etags $K/*.[cS] $U/*.[cS] mkfs/*.c $(wildcard platform/common/*.h platform/*/*.[cSh])

$(USYS_SRC): $U/usys.pl
	mkdir -p $(dir $@)
	perl $< > $@

$(USYS_OBJ): $(USYS_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.S
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(UBUILD)/_%: $(UBUILD)/%.o $(ULIB)
	mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^ -L$(LIBGCC) -lgcc -s
	$(OBJDUMP) -S $@ > $(basename $@).asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(basename $@).sym

$(UBUILD)/_forktest: $(UBUILD)/forktest.o $(call src_to_obj,$U/ulib.c) $(USYS_OBJ)
	mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^ -L$(LIBGCC) -lgcc
	$(OBJDUMP) -S $@ > $(basename $@).asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(basename $@).sym

$(MKFS): mkfs/mkfs.c $K/fs.h $K/param.h
	mkdir -p $(dir $@)
	gcc -Werror -Wall -I. -o $@ $<

$(README_COPY): README
	mkdir -p $(dir $@)
	cp $< $@

$(XV6_ELF_COPY): $(KERNEL_ELF)
	mkdir -p $(dir $@)
	cp $< $@

fs.img: $(FS_IMG)

$(FS_IMG): $(MKFS) $(README_COPY) $(XV6_ELF_COPY) $(UPROGS)
	mkdir -p $(dir $@)
	cd $(BUILD_DIR) && ./mkfs/mkfs $(notdir $@) $(notdir $(XV6_ELF_COPY)) $(notdir $(README_COPY)) $(UPROGS_REL)

# Prevent deletion of intermediate files, e.g. objects from chained builds.
.PRECIOUS: $(BUILD_DIR)/%.o

-include \
  $(KERNEL_OBJS:.o=.d) \
  $(ULIB:.o=.d) \
  $(USER_PROG_OBJS:.o=.d) \
  $(INITCODE_OBJ:.o=.d)

clean:
	rm -rf $(BUILD_ROOT)
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	  $K/*.o $K/*.d $K/*.asm $K/*.sym $K/kernel \
	  $U/*.o $U/*.d $U/*.asm $U/*.sym $U/initcode $U/initcode.out $U/usys.S \
	  xv6.elf fs.img mkfs/mkfs .gdbinit

ifndef CPUS
CPUS := 1
endif
