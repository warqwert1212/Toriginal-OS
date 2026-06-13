# =============================================================================
# Toriginal OS - freeNT Build System
# =============================================================================

CC  = gcc
AS  = gcc
LD  = ld

KDIR = root/freeNT/kernel
INC  = root/freeNT/include

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
	-I$(INC)

ASFLAGS = -c -m64

LDFLAGS = \
	-m elf_x86_64 \
	-T $(KDIR)/boot/linker.ld \
	-z noexecstack

# =============================================================================
# Object files
# =============================================================================

OBJS = \
	boot64.o \
	kernel.o \
	serial.o \
	vga.o \
	string.o \
	memory.o \
	Pmm.o \
	vmm.o \
	interrups.o \
	isr.o \
	pic.o \
	pit.o \
	timer.o \
	panic.o \
	keybord.o \
	keyboard_wire.o \
	keyboard_isr.o \
	syscall.o \
	stubs.o \
	runtim_stubs.o \
	trploader.o \
	loader_enhaced.o \
	process.o

# =============================================================================
# Default target
# =============================================================================

all: kernel.bin

# =============================================================================
# Assembly
# =============================================================================

boot64.o: $(KDIR)/boot/boot64.s
	$(AS) $(ASFLAGS) $(KDIR)/boot/boot64.s -o boot64.o

keyboard_isr.o: $(KDIR)/boot/keyboard_isr.s
	$(AS) $(ASFLAGS) $(KDIR)/boot/keyboard_isr.s -o keyboard_isr.o

# =============================================================================
# C files
# =============================================================================

kernel.o: $(KDIR)/kernel.c
	$(CC) $(CFLAGS) -c $(KDIR)/kernel.c -o kernel.o

serial.o: $(KDIR)/serial.c
	$(CC) $(CFLAGS) -c $(KDIR)/serial.c -o serial.o

vga.o: $(KDIR)/vga.c
	$(CC) $(CFLAGS) -c $(KDIR)/vga.c -o vga.o

string.o: $(KDIR)/string.c
	$(CC) $(CFLAGS) -c $(KDIR)/string.c -o string.o

memory.o: $(KDIR)/memory.c
	$(CC) $(CFLAGS) -c $(KDIR)/memory.c -o memory.o

Pmm.o: $(KDIR)/Pmm.c
	$(CC) $(CFLAGS) -c $(KDIR)/Pmm.c -o Pmm.o

vmm.o: $(KDIR)/vmm.c
	$(CC) $(CFLAGS) -c $(KDIR)/vmm.c -o vmm.o

interrups.o: $(KDIR)/interrups.c
	$(CC) $(CFLAGS) -c $(KDIR)/interrups.c -o interrups.o

isr.o: $(KDIR)/isr.c
	$(CC) $(CFLAGS) -c $(KDIR)/isr.c -o isr.o

pic.o: $(KDIR)/pic.c
	$(CC) $(CFLAGS) -c $(KDIR)/pic.c -o pic.o

pit.o: $(KDIR)/pit.c
	$(CC) $(CFLAGS) -c $(KDIR)/pit.c -o pit.o

timer.o: $(KDIR)/timer.c
	$(CC) $(CFLAGS) -c $(KDIR)/timer.c -o timer.o

panic.o: $(KDIR)/panic.c
	$(CC) $(CFLAGS) -c $(KDIR)/panic.c -o panic.o

keybord.o: $(KDIR)/keybord.c
	$(CC) $(CFLAGS) -c $(KDIR)/keybord.c -o keybord.o

keyboard_wire.o: $(KDIR)/keyboard_wire.c
	$(CC) $(CFLAGS) -c $(KDIR)/keyboard_wire.c -o keyboard_wire.o

syscall.o: $(KDIR)/syscall.c
	$(CC) $(CFLAGS) -c $(KDIR)/syscall.c -o syscall.o

stubs.o: $(KDIR)/stubs.c
	$(CC) $(CFLAGS) -c $(KDIR)/stubs.c -o stubs.o

runtim_stubs.o: $(KDIR)/runtime_stubs.c
	$(CC) $(CFLAGS) -c $(KDIR)/runtime_stubs.c -o runtim_stubs.o

trploader.o: $(KDIR)/trploader.c
	$(CC) $(CFLAGS) -c $(KDIR)/trploader.c -o trploader.o

loader_enhaced.o: $(KDIR)/loader_enhaced.c
	$(CC) $(CFLAGS) -c $(KDIR)/loader_enhaced.c -o loader_enhaced.o

process.o: $(KDIR)/process.c
	$(CC) $(CFLAGS) -c $(KDIR)/process.c -o process.o

KERNEL_OBJS += kernel/trp_manifest.o
# =============================================================================
# Link
# =============================================================================

kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o kernel.bin

# =============================================================================
# ISO
# =============================================================================

ISO_DIR = iso_root

iso: kernel.bin
	mkdir -p $(ISO_DIR)/boot/grub
	cp kernel.bin $(ISO_DIR)/boot/kernel.bin
	cp root/freeNT/grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o ToriginalOS.iso $(ISO_DIR)

# =============================================================================
# Run
# =============================================================================

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

# =============================================================================
# Clean
# =============================================================================

clean:
	rm -f *.o kernel.bin ToriginalOS.iso

.PHONY: all iso run run-debug clean

