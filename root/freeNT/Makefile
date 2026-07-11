CC      := x86_64-linux-gnu-gcc
LD      := x86_64-linux-gnu-ld
AS      := x86_64-linux-gnu-as
MKISO   := grub-mkrescue

SRC     := kernel
SYS     := ../sys/shell
BOOT    := kernel/boot
OBJ     := obj
BIN     := bin
ISO_DIR := isodir

# FIX: was hardcoded "-I/usr/lib/gcc/x86_64-linux-gnu/13/include", which
# breaks the moment the Codespaces image ships a different GCC major
# version. Ask gcc itself where its freestanding headers (stdint.h,
# stddef.h, stdarg.h, etc — NOT full libc) live.
GCC_FREESTANDING_INCLUDE := $(shell $(CC) -print-file-name=include)

CFLAGS  := -ffreestanding -nostdlib -nostdinc -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -O2 -Wall -Wextra -Iinclude -Ikernel -I$(GCC_FREESTANDING_INCLUDE)
LDFLAGS := -T kernel/boot/kernel64.ld --no-undefined -z noexecstack

C_SRCS  := $(wildcard $(SRC)/*.c)
S_SRCS  := $(wildcard $(BOOT)/*.s) $(wildcard $(BOOT)/*.S)
SYS_SRCS := $(wildcard $(SYS)/*.c)

C_OBJS  := $(patsubst $(SRC)/%.c,  $(OBJ)/%.o,      $(C_SRCS))
S_OBJS  := $(patsubst $(BOOT)/%.s, $(OBJ)/boot/%.o, $(S_SRCS))
SYS_OBJS := $(patsubst $(SYS)/%.c, $(OBJ)/sys/%.o,  $(SYS_SRCS))

OBJS := $(OBJ)/boot/boot64.o \
        $(filter-out $(OBJ)/boot/boot64.o, $(S_OBJS)) \
        $(C_OBJS) \
        $(SYS_OBJS)

.PHONY: all iso run clean tools

all: iso

$(OBJ)/boot $(BIN) $(ISO_DIR)/boot/grub:
	mkdir -p $@

$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)/boot
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# graphics_3d.c is the one file in this kernel that uses float/vec3_t
# math (matrix transforms, perspective projection for the software
# rasterizer). Every other file is compiled with -mno-sse -mno-sse2
# because the x86-64 SysV ABI returns float/struct-of-float values in
# XMM registers - with SSE disabled, GCC has nowhere to put them and
# errors out at compile time (verified: this is a hard compile error,
# not a soft fallback to x87).
#
# This explicit rule (Make prefers an explicit rule over the generic
# %.o pattern above for the same target) overrides CFLAGS for just
# this one object file, re-enabling -msse2 -mfpmath=sse. This is only
# safe because process.c's scheduler_yield() now does a real
# fxsave/fxrstor of each process's SSE/FPU state on every context
# switch (see process.c and boot64.s's CR4.OSFXSR enablement) - using
# SSE registers in a kernel that doesn't save/restore FPU state across
# preemption would silently corrupt whichever process next touched
# the FPU. If you're reading this because a future file also needs
# float math, add it as another explicit rule below rather than
# loosening the global CFLAGS - keeping SSE opt-in per-file makes it
# obvious, from the Makefile alone, exactly which code paths depend on
# the FPU-safe context switch being correct.
$(OBJ)/graphics_3d.o: $(SRC)/graphics_3d.c | $(OBJ)/boot
	mkdir -p $(dir $@)
	$(CC) $(filter-out -mno-sse -mno-sse2,$(CFLAGS)) -msse -msse2 -mfpmath=sse -c $< -o $@

$(OBJ)/sys/%.o: $(SYS)/%.c | $(OBJ)/boot
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/boot/%.o: $(BOOT)/%.s | $(OBJ)/boot
	$(AS) --64 $< -o $@

$(OBJ)/boot/%.o: $(BOOT)/%.S | $(OBJ)/boot
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

$(BIN)/kernel.elf: $(OBJS) kernel/boot/kernel64.ld | $(BIN)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "── ELF OK ──"
	@x86_64-linux-gnu-readelf -h $@ | grep -E "Class|Machine|Entry"

$(ISO_DIR)/boot/grub/grub.cfg: | $(ISO_DIR)/boot/grub
	@printf 'set timeout=3\nset default=0\n\ninsmod vbe\ninsmod video_bochs\ninsmod video_cirrus\ninsmod efi_gop\ninsmod all_video\nset gfxpayload=keep\n\nmenuentry "toriginal_OS" {\n\tmultiboot2 /boot/kernel.elf\n\tboot\n}\n' > $@

$(ISO_DIR)/boot/kernel.elf: $(BIN)/kernel.elf | $(ISO_DIR)/boot/grub
	cp $< $@

toriginal_OS.iso: $(ISO_DIR)/boot/kernel.elf $(ISO_DIR)/boot/grub/grub.cfg
	$(MKISO) -o $@ $(ISO_DIR) -d /usr/lib/grub/i386-pc

iso: toriginal_OS.iso

run: iso
	qemu-system-x86_64 -cdrom toriginal_OS.iso -m 256M -serial stdio -no-reboot

# run-headless: useful for CI or Codespaces where there is no display.
# All shell I/O goes through the serial port (stdin/stdout).
run-headless: iso
	qemu-system-x86_64 -cdrom toriginal_OS.iso -m 256M -serial stdio -display none -no-reboot

tools:
	sudo apt update && sudo apt install -y \
	    gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu \
	    nasm xorriso grub-pc-bin mtools qemu-system-x86

clean:
	rm -rf $(OBJ) $(BIN) $(ISO_DIR) toriginal_OS.iso
