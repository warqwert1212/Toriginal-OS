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
	@printf 'set timeout=5\nset default=0\n\ninsmod vbe\ninsmod video_bochs\ninsmod video_cirrus\ninsmod efi_gop\ninsmod all_video\nset gfxpayload=keep\n\nmenuentry "toriginal_OS" {\n\tmultiboot2 /boot/kernel.elf\n\tboot\n}\n\nmenuentry "toriginal_OS (start in GUI)" {\n\tmultiboot2 /boot/kernel.elf gui\n\tboot\n}\n' > $@

$(ISO_DIR)/boot/kernel.elf: $(BIN)/kernel.elf | $(ISO_DIR)/boot/grub
	cp $< $@

toriginal_OS.iso: $(ISO_DIR)/boot/kernel.elf $(ISO_DIR)/boot/grub/grub.cfg
	$(MKISO) -o $@ $(ISO_DIR) -d /usr/lib/grub/i386-pc

iso: toriginal_OS.iso

run: iso
	qemu-system-x86_64 -cdrom toriginal_OS.iso -m 256M -serial stdio -no-reboot


run-headless: iso
	qemu-system-x86_64 -cdrom toriginal_OS.iso -m 256M -serial stdio -display none -no-reboot

tools:
	sudo apt update && sudo apt install -y \
	    gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu \
	    nasm xorriso grub-pc-bin mtools qemu-system-x86

clean:
	rm -rf $(OBJ) $(BIN) $(ISO_DIR) toriginal_OS.iso
