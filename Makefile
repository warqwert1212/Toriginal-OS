# ============================================================================
# Toriginal OS - freeNT Build System
# ============================================================================

CC      = gcc
AS      = gcc
LD      = ld

KERNEL_DIR = root/freeNT/kernel\ 
INCLUDE_DIR = root/freeNT/include
BOOT_DIR = $(KERNEL_DIR)boot
ISO_DIR = iso_root
GRUB_CFG = $(ISO_DIR)/boot/grub/grub.cfg

CFLAGS = \
	-m64 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pic \
	-fno-pie \
	-mno-red-zone \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-mcmodel=kernel \
	-Wall \
	-Wextra \
	-O2 \
	-I"$(INCLUDE_DIR)"

ASFLAGS = \
	-c \
	-m64

LDFLAGS = \
	-m elf_x86_64 \
	-T "$(BOOT_DIR)/linker.ld" \
	-z noexecstack

# ============================================================================
# Object files — every .c and .s in your kernel dir
# ============================================================================

OBJS = \
	boot64.o \
	kernel.o \
	serial.o \
	vga.o \
	string.o \
	memory.o \
	Pmm.o \
	vmm.o \
	idt.o \
	interrups.o \
	isr.o \
	pic.o \
	pit.o \
	timer.o \
	panic.o \
	keybord.o \
	keyboard_wire.o \
	keyboard_isr.o \
	process.o \
	syscall.o \
	stubs.o \
	trploader.o \
	loader_enhaced.o \
	shell.o

# ============================================================================
# Default target
# ============================================================================

all: kernel.bin

# ============================================================================
# Assembly files
# ============================================================================

boot64.o: "$(BOOT_DIR)/boot64.s"
	$(AS) $(ASFLAGS) "$(BOOT_DIR)/boot64.s" -o boot64.o

interrupts_asm.o: "$(BOOT_DIR)/interrupts.s"
	$(AS) $(ASFLAGS) "$(BOOT_DIR)/interrupts.s" -o interrupts_asm.o

keyboard_isr.o: "$(BOOT_DIR)/keyboard isr.s"
	$(AS) $(ASFLAGS) "$(BOOT_DIR)/keyboard isr.s" -o keyboard_isr.o

# ============================================================================
# C files
# ============================================================================

kernel.o: "$(KERNEL_DIR)kernel.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)kernel.c" -o kernel.o

serial.o: "$(KERNEL_DIR)serial.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)serial.c" -o serial.o

vga.o: "$(KERNEL_DIR)vga.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)vga.c" -o vga.o

string.o: "$(KERNEL_DIR)string.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)string.c" -o string.o

memory.o: "$(KERNEL_DIR)memory.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)memory.c" -o memory.o

Pmm.o: "$(KERNEL_DIR)Pmm.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)Pmm.c" -o Pmm.o

vmm.o: "$(KERNEL_DIR)vmm.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)vmm.c" -o vmm.o

idt.o: "$(KERNEL_DIR)idt.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)idt.c" -o idt.o

interrups.o: "$(KERNEL_DIR)interrups.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)interrups.c" -o interrups.o

isr.o: "$(KERNEL_DIR)isr.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)isr.c" -o isr.o

pic.o: "$(KERNEL_DIR)pic.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)pic.c" -o pic.o

pit.o: "$(KERNEL_DIR)pit.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)pit.c" -o pit.o

timer.o: "$(KERNEL_DIR)timer.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)timer.c" -o timer.o

panic.o: "$(KERNEL_DIR)panic.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)panic.c" -o panic.o

keybord.o: "$(KERNEL_DIR)keybord.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)keybord.c" -o keybord.o

keyboard_wire.o: "$(KERNEL_DIR)keyboard wire.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)keyboard wire.c" -o keyboard_wire.o

process.o: "$(KERNEL_DIR)process.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)process.c" -o process.o

syscall.o: "$(KERNEL_DIR)syscall.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)syscall.c" -o syscall.o

stubs.o: "$(KERNEL_DIR)stubs.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)stubs.c" -o stubs.o

trploader.o: "$(KERNEL_DIR)trploader.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)trploader.c" -o trploader.o

loader_enhaced.o: "$(KERNEL_DIR)loader_enhaced.c"
	$(CC) $(CFLAGS) -c "$(KERNEL_DIR)loader_enhaced.c" -o loader_enhaced.o

shell.o: "$(BOOT_DIR)/shell.c"
	$(CC) $(CFLAGS) -c "$(BOOT_DIR)/shell.c" -o shell.o

# ============================================================================
# Link
# ============================================================================

kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o kernel.bin

# ============================================================================
# ISO
# ============================================================================

iso: kernel.bin
	mkdir -p $(ISO_DIR)/boot/grub
	cp kernel.bin $(ISO_DIR)/boot/kernel.bin
	cp "$(ISO_DIR)/boot/grub/grub.cfg" "$(ISO_DIR)/boot/grub/grub.cfg" 2>/dev/null || true
	@echo 'set timeout=5'                          > $(GRUB_CFG)
	@echo 'set default=0'                         >> $(GRUB_CFG)
	@echo ''                                       >> $(GRUB_CFG)
	@echo 'menuentry "Toriginal OS" {'             >> $(GRUB_CFG)
	@echo '    multiboot2 /boot/kernel.bin'        >> $(GRUB_CFG)
	@echo '    boot'                               >> $(GRUB_CFG)
	@echo '}'                                      >> $(GRUB_CFG)
	@echo ''                                       >> $(GRUB_CFG)
	@echo 'menuentry "Toriginal OS (serial debug)" {' >> $(GRUB_CFG)
	@echo '    multiboot2 /boot/kernel.bin shell'  >> $(GRUB_CFG)
	@echo '    boot'                               >> $(GRUB_CFG)
	@echo '}'                                      >> $(GRUB_CFG)
	grub-mkrescue -o ToriginalOS.iso $(ISO_DIR)

# ============================================================================
# Run in QEMU
# ============================================================================

run: ToriginalOS.iso
	qemu-system-x86_64 \
		-cdrom ToriginalOS.iso \
		-serial stdio \
		-m 256M \
		-vga std

run-debug: ToriginalOS.iso
	qemu-system-x86_64 \
		-cdrom ToriginalOS.iso \
		-serial stdio \
		-m 256M \
		-vga std \
		-d int,cpu_reset \
		-no-reboot

# ============================================================================
# Clean
# ============================================================================

clean:
	rm -f *.o
	rm -f kernel.bin
	rm -f ToriginalOS.iso

.PHONY: all iso run run-debug clean