# ==============================================================================
# MAKEFILE - Production Strict Freestanding Cross-Compilation Toolchain
# ==============================================================================
CC = gcc
AS = as
LD = ld

# Integrated structural include search matrices
CFLAGS = -m64 -mcmodel=kernel -ffreestanding -fno-stack-protector \
         -fno-pic -fno-pie -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -Wall -Wextra -O2 -IfreeNT/src/kernel/include -IfreeNT/src/kernel/boot

SRC_DIR = freeNT/src/kernel
BOOT_DIR = freeNT/src/kernel/boot

# Synchronized to target exactly your underlying physical filesystem binaries
OBJECTS = boot64.o gdt_flush.o syscall_entry.o boot_detect.o kernel.o io.o \
          string.o graphics.o memory.o interrupts.o process.o filesystem.o \
          syscall.o elf_loader.o

all: kernel.bin

# --- Assembly Files (Boot & Low-Level Privilege Traps) ---
boot64.o: $(BOOT_DIR)/boot64.S
	$(CC) -m64 -c $(BOOT_DIR)/boot64.S -o boot64.o

gdt_flush.o: $(BOOT_DIR)/gdt_flush.s
	$(AS) --64 -o gdt_flush.o $(BOOT_DIR)/gdt_flush.s

syscall_entry.o: $(BOOT_DIR)/syscall_entry.s
	$(AS) --64 -o syscall_entry.o $(BOOT_DIR)/syscall_entry.s

# --- Core Kernel and Utility Subsystems ---
boot_detect.o: $(BOOT_DIR)/boot_detect.c
	$(CC) $(CFLAGS) -c $(BOOT_DIR)/boot_detect.c -o boot_detect.o

kernel.o: $(SRC_DIR)/kernel.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/kernel.c -o kernel.o

io.o: $(SRC_DIR)/io.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/io.c -o io.o

string.o: $(SRC_DIR)/string.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/string.c -o string.o

graphics.o: $(SRC_DIR)/graphics.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/graphics.c -o graphics.o

# --- Memory, Executive & File Infrastructure Elements ---
memory.o: $(SRC_DIR)/memory.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/memory.c -o memory.o

interrupts.o: $(SRC_DIR)/interrupts.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/interrupts.c -o interrupts.o

process.o: $(SRC_DIR)/process.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/process.c -o process.o

filesystem.o: $(SRC_DIR)/filesystem.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/filesystem.c -o filesystem.o

# --- User Space Isolation Gateways ---
syscall.o: $(SRC_DIR)/syscall.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/syscall.c -o syscall.o

elf_loader.o: $(SRC_DIR)/elf_loader.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/elf_loader.c -o elf_loader.o

# --- Linker Target Output Generation ---
kernel.bin: $(OBJECTS)
	$(LD) $(LDFLAGS) $(OBJECTS) -o kernel.bin

clean:
	rm -rf *.o kernel.bin isodir ToriginalOS.iso freeNT.iso