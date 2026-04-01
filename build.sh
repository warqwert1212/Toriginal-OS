#!/bin/bash

# Toriganal-OS Build Script - Compile to ISO

set -e

# Create build directory
mkdir -p build
cd build

# Clean previous builds
rm -f *.o *.bin *.iso kernel

# Step 1: Compile Assembly (Bootloader)
echo "Compiling bootloader (boot.asm)..."
nasm -f bin ../boot.asm -o boot.bin

# Step 2: Compile Kernel Assembly (if exists)
if [ -f ../kernel_entry.asm ]; then
    echo "Compiling kernel entry (kernel_entry.asm)..."
    nasm -f elf32 ../kernel_entry.asm -o kernel_entry.o
fi

# Step 3: Compile C++ Kernel (if exists)
if [ -f ../main.cpp ]; then
    echo "Compiling kernel (main.cpp)..."
    i686-elf-g++ -ffreestanding -m32 -c ../main.cpp -o kernel.o
fi

# Step 4: Link kernel
echo "Linking kernel..."
if [ -f kernel_entry.o ] && [ -f kernel.o ]; then
    i686-elf-ld -T ../linker.ld -m elf_i386 -o kernel kernel_entry.o kernel.o
elif [ -f kernel.o ]; then
    i686-elf-ld -m elf_i386 -o kernel kernel.o
fi

# Step 5: Create ISO image
echo "Creating ISO image..."
mkdir -p iso/boot/grub

# Copy bootloader and kernel to ISO
cp boot.bin iso/boot/
if [ -f kernel ]; then
    cp kernel iso/boot/
fi

# Create GRUB configuration
cat > iso/boot/grub/grub.cfg << 'EOF'
menuentry 'Toriganal OS' {
    multiboot /boot/kernel
}
EOF

# Create the ISO
grub-mkrescue -o toriganal-os.iso iso/

echo "✓ Build complete!"
echo "✓ ISO image: build/toriganal-os.iso"