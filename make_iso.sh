#!/bin/bash
# run this from the ROOT of your repo (where the root/ folder is)
# Requirements: sudo apt install build-essential gcc binutils grub-pc-bin grub-efi-amd64-bin xorriso mtools

set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"
OBJ="$BUILD/obj"
ISO_ROOT="$BUILD/iso_root"

mkdir -p "$OBJ" "$ISO_ROOT/boot/grub"

CC="gcc"
LD="ld"
INC="-I$ROOT/root/freeNT/include -I$ROOT/root/freeNT/kernel -I$ROOT/root/installer -I$ROOT/root/file_formats"
CFLAGS="-m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel -O2 $INC"

echo "==> Assembling..."
gcc -c -m64 "$ROOT/root/freeNT/kernel/boot/boot64.s"       -o "$OBJ/boot64.o"
gcc -c -m64 "$ROOT/root/freeNT/kernel/boot/keyboard_isr.s" -o "$OBJ/keyboard_isr.o"

echo "==> Compiling kernel..."
for f in \
    kernel serial vga string pit pic panic \
    syscall stubs runtime_stubs process isr \
    keyboard_wire keybord timer vmm
do
    src="$ROOT/root/freeNT/kernel/$f.c"
    [ -f "$src" ] && gcc $CFLAGS -c "$src" -o "$OBJ/$f.o" && echo "    $f.c"
done

# memory - try subdir first
if [ -f "$ROOT/root/freeNT/kernel/mm/memory.c" ]; then
    gcc $CFLAGS -c "$ROOT/root/freeNT/kernel/mm/memory.c" -o "$OBJ/memory.o"
else
    gcc $CFLAGS -c "$ROOT/root/freeNT/kernel/memory.c" -o "$OBJ/memory.o"
fi

# Pmm - capital P
if [ -f "$ROOT/root/freeNT/kernel/Pmm.c" ]; then
    gcc $CFLAGS -c "$ROOT/root/freeNT/kernel/Pmm.c" -o "$OBJ/pmm.o"
else
    gcc $CFLAGS -c "$ROOT/root/freeNT/kernel/pmm.c" -o "$OBJ/pmm.o"
fi

# interrupts (typo in filename)
gcc $CFLAGS -c "$ROOT/root/freeNT/kernel/interrups.c" -o "$OBJ/interrupts.o"

# loader (typo in filename)
gcc $CFLAGS -c "$ROOT/root/freeNT/kernel/loader_enhaced.c" -o "$OBJ/loader.o"

# trploader
gcc $CFLAGS -c "$ROOT/root/freeNT/kernel/trploader.c" -o "$OBJ/trploader.o"

# trp manifest (space in filename)
gcc $CFLAGS -c "$ROOT/root/freeNT/kernel/trp manifest.c" -o "$OBJ/trp_manifest.o"

echo "==> Compiling filesystem..."
# trpfs has a comma in the filename
gcc $CFLAGS -c "$ROOT/root/file_formats/trpfs,c" -o "$OBJ/trpfs.o"
gcc $CFLAGS -c "$ROOT/root/file_formats/ntfs.c"  -o "$OBJ/ntfs.o"

echo "==> Compiling installer..."
gcc $CFLAGS -c "$ROOT/root/installer/installer.c" -o "$OBJ/installer.o"
gcc $CFLAGS -c "$ROOT/root/installer/string.c"    -o "$OBJ/installer_string.o"

echo "==> Compiling shell..."
gcc $CFLAGS -c "$ROOT/root/sys/shell/shell.c" -o "$OBJ/shell.o"

echo "==> Linking kernel.bin..."
ld -m elf_x86_64 -nostdlib -no-pie -z noexecstack \
   -T "$ROOT/root/freeNT/kernel/boot/linker.ld" \
   "$OBJ/boot64.o" \
   "$OBJ/keyboard_isr.o" \
   "$OBJ/kernel.o" \
   "$OBJ/serial.o" \
   "$OBJ/vga.o" \
   "$OBJ/string.o" \
   "$OBJ/memory.o" \
   "$OBJ/pmm.o" \
   "$OBJ/vmm.o" \
   "$OBJ/interrupts.o" \
   "$OBJ/pic.o" \
   "$OBJ/pit.o" \
   "$OBJ/timer.o" \
   "$OBJ/panic.o" \
   "$OBJ/keyboard_wire.o" \
   "$OBJ/keybord.o" \
   "$OBJ/keyboard_isr.o" \
   "$OBJ/syscall.o" \
   "$OBJ/stubs.o" \
   "$OBJ/runtime_stubs.o" \
   "$OBJ/process.o" \
   "$OBJ/loader.o" \
   "$OBJ/trploader.o" \
   "$OBJ/trp_manifest.o" \
   "$OBJ/isr.o" \
   "$OBJ/trpfs.o" \
   "$OBJ/ntfs.o" \
   "$OBJ/installer.o" \
   "$OBJ/installer_string.o" \
   "$OBJ/shell.o" \
   -o "$BUILD/kernel.bin"

echo "==> Building ISO..."
cp "$BUILD/kernel.bin"                        "$ISO_ROOT/boot/kernel.bin"
cp "$ROOT/root/freeNT/grub/grub.cfg"         "$ISO_ROOT/boot/grub/grub.cfg"
grub-mkrescue -o "$ROOT/ToriginalOS.iso" "$ISO_ROOT"

echo ""
echo "Done! ToriginalOS.iso is ready."
echo "Open VirtualBox → New → Other/Unknown 64-bit → use ToriginalOS.iso as the optical disk."