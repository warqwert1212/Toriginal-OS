# ── freeNT build system ──────────────────────────────────────────────────────
CC      := x86_64-linux-gnu-gcc
LD      := x86_64-linux-gnu-ld
AS      := x86_64-linux-gnu-as
MKISO   := grub-mkrescue

SRC     := kernel
BOOT    := kernel/boot
OBJ     := obj
BIN     := bin
ISO_DIR := isodir

CFLAGS  := -ffreestanding -nostdlib -nostdinc -mno-red-zone \
           -O2 -Wall -Wextra -I$(SRC)/include

LDFLAGS := -T kernel64.ld --no-undefined -z noexecstack

# ── Collect sources ──────────────────────────────────────────────────────────
C_SRCS    := $(wildcard $(SRC)/*.c)
S_SRCS    := $(wildcard $(BOOT)/*.s) $(wildcard $(BOOT)/*.S)

C_OBJS    := $(patsubst $(SRC)/%.c,    $(OBJ)/%.o,        $(C_SRCS))
S_OBJS    := $(patsubst $(BOOT)/%.s,   $(OBJ)/boot/%.o,   \
             $(patsubst $(BOOT)/%.S,   $(OBJ)/boot/%.o,   $(S_SRCS)))

# boot64 must link first so _start is at 0x200000
OBJS := $(OBJ)/boot/boot64.o \
        $(filter-out $(OBJ)/boot/boot64.o, $(S_OBJS)) \
        $(C_OBJS)

# ── Rules ────────────────────────────────────────────────────────────────────
.PHONY: all iso run clean tools

all: iso

# Create output dirs
$(OBJ)/boot $(BIN) $(ISO_DIR)/boot/grub:
	mkdir -p $@

# Compile C
$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble .s (lowercase — GAS, no preprocessor)
$(OBJ)/boot/%.o: $(BOOT)/%.s | $(OBJ)/boot
	$(AS) --64 $< -o $@

# Assemble .S (uppercase — GAS + C preprocessor)
$(OBJ)/boot/%.o: $(BOOT)/%.S | $(OBJ)/boot
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# Link
$(BIN)/kernel.elf: $(OBJS) kernel64.ld | $(BIN)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "── Verify ELF header ──"
	@x86_64-linux-gnu-readelf -h $@ | grep -E "Class|Machine|Entry"

# grub.cfg
$(ISO_DIR)/boot/grub/grub.cfg: | $(ISO_DIR)/boot/grub
	@printf 'set timeout=3\nset default=0\n\nmenuentry "freeNT" {\n\tmultiboot2 /boot/kernel.elf\n\tboot\n}\n' > $@

# Copy kernel into ISO tree
$(ISO_DIR)/boot/kernel.elf: $(BIN)/kernel.elf | $(ISO_DIR)/boot/grub
	cp $< $@

# Pack ISO
freeNT.iso: $(ISO_DIR)/boot/kernel.elf $(ISO_DIR)/boot/grub/grub.cfg
	$(MKISO) -o $@ $(ISO_DIR) -d /usr/lib/grub/i386-pc

iso: freeNT.iso

# Boot in QEMU — serial_puts() output appears in your terminal
run: iso
	qemu-system-x86_64 \
	    -cdrom freeNT.iso \
	    -m 256M \
	    -serial stdio \
	    -display none \
	    -no-reboot

# Install all required tools
tools:
	sudo apt update && sudo apt install -y \
	    gcc-x86-64-linux-gnu \
	    binutils-x86-64-linux-gnu \
	    nasm xorriso grub-pc-bin mtools \
	    qemu-system-x86

clean:
	rm -rf $(OBJ) $(BIN) $(ISO_DIR) freeNT.iso
