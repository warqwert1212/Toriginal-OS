#!/usr/bin/env bash
# =============================================================================
# build.sh — Toriginal OS full build script
# Run from the ROOT of your repository (the folder containing root/)
#
# Requirements (install with the commands at the bottom of this file):
#   gcc, ld, as (binutils), grub-mkrescue, xorriso
#
# Usage:
#   chmod +x build.sh
#   ./build.sh          — clean build + ISO
#   ./build.sh run      — build + launch in QEMU
#   ./build.sh clean    — remove all build artefacts
# =============================================================================

set -e   # exit on first error

# ── Configurable paths (edit these to match your repo layout) ───────────────
ROOT="$(cd "$(dirname "$0")" && pwd)"   # script location = repo root

KERNEL_SRC="$ROOT/root/freeNT/kernel"
INCLUDE_DIR="$ROOT/root/freeNT/include"
INSTALLER_SRC="$ROOT/root/installer"
FILE_FORMATS_SRC="$ROOT/root/file_formats"
GRUB_CFG="$ROOT/root/freeNT/grub/grub.cfg"

BUILD_DIR="$ROOT/build"
ISO_ROOT="$BUILD_DIR/iso_root"
OBJ_DIR="$BUILD_DIR/obj"
KERNEL_BIN="$BUILD_DIR/kernel.bin"
ISO_FILE="$ROOT/ToriginalOS.iso"

# ── Toolchain ────────────────────────────────────────────────────────────────
CC="gcc"
AS="gcc"      # gcc is used to assemble .s files (passes to GNU as internally)
LD="ld"

CFLAGS=(
    -m64
    -ffreestanding
    -fno-stack-protector
    -fno-pic
    -fno-pie
    -mno-red-zone
    -mno-mmx
    -mno-sse
    -mno-sse2
    -mcmodel=kernel
    -Wall
    -Wextra
    -O2
    -I"$INCLUDE_DIR"
    -I"$KERNEL_SRC"
    -I"$INSTALLER_SRC"
    -I"$FILE_FORMATS_SRC"
)

LDFLAGS=(
    -m elf_x86_64
    -nostdlib
    -no-pie
    -z noexecstack
    -T "$KERNEL_SRC/boot/linker.ld"
)

# =============================================================================
# Helper: compile one C file -> .o
# compile <src.c> <out.o>
# =============================================================================
compile() {
    local src="$1" obj="$2"
    echo "  CC  $src"
    mkdir -p "$(dirname "$obj")"
    $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
}

# =============================================================================
# Helper: assemble one .s file -> .o
# =============================================================================
assemble() {
    local src="$1" obj="$2"
    echo "  AS  $src"
    mkdir -p "$(dirname "$obj")"
    $AS -c -m64 "$src" -o "$obj"
}

# =============================================================================
# clean
# =============================================================================
do_clean() {
    echo "[clean] removing $BUILD_DIR and $ISO_FILE"
    rm -rf "$BUILD_DIR"
    rm -f  "$ISO_FILE"
}

# =============================================================================
# build
# =============================================================================
do_build() {
    echo "========================================"
    echo "  Toriginal OS — build"
    echo "========================================"

    mkdir -p "$OBJ_DIR" "$ISO_ROOT/boot/grub"

    # ── 1. boot stub (must come first in link order) ────────────────────────
    assemble "$KERNEL_SRC/boot/boot64.s"        "$OBJ_DIR/boot64.o"
    assemble "$KERNEL_SRC/boot/keyboard_isr.s"  "$OBJ_DIR/keyboard_isr.o"

    # ── 2. kernel C sources ─────────────────────────────────────────────────
    compile "$KERNEL_SRC/kernel.c"              "$OBJ_DIR/kernel.o"
    compile "$KERNEL_SRC/serial.c"              "$OBJ_DIR/serial.o"
    compile "$KERNEL_SRC/vga.c"                 "$OBJ_DIR/vga.o"
    compile "$KERNEL_SRC/string.c"              "$OBJ_DIR/string.o"
    compile "$KERNEL_SRC/mm/memory.c"           "$OBJ_DIR/memory.o"       2>/dev/null || \
    compile "$KERNEL_SRC/memory.c"              "$OBJ_DIR/memory.o"
    compile "$KERNEL_SRC/Pmm.c"                 "$OBJ_DIR/pmm.o"          2>/dev/null || \
    compile "$KERNEL_SRC/pmm.c"                 "$OBJ_DIR/pmm.o"
    compile "$KERNEL_SRC/vmm.c"                 "$OBJ_DIR/vmm.o"
    compile "$KERNEL_SRC/interrups.c"           "$OBJ_DIR/interrupts.o"
    compile "$KERNEL_SRC/pic.c"                 "$OBJ_DIR/pic.o"
    compile "$KERNEL_SRC/pit.c"                 "$OBJ_DIR/pit.o"
    compile "$KERNEL_SRC/timer.c"               "$OBJ_DIR/timer.o"
    compile "$KERNEL_SRC/panic.c"               "$OBJ_DIR/panic.o"
    compile "$KERNEL_SRC/keybord.c"             "$OBJ_DIR/keyboard.o"
    compile "$KERNEL_SRC/keyboard_wire.c"       "$OBJ_DIR/keyboard_wire.o"
    compile "$KERNEL_SRC/syscall.c"             "$OBJ_DIR/syscall.o"
    compile "$KERNEL_SRC/stubs.c"               "$OBJ_DIR/stubs.o"
    compile "$KERNEL_SRC/runtime_stubs.c"       "$OBJ_DIR/runtime_stubs.o"
    compile "$KERNEL_SRC/process.c"             "$OBJ_DIR/process.o"
    compile "$KERNEL_SRC/loader_enhaced.c"      "$OBJ_DIR/loader_enhanced.o"
    compile "$KERNEL_SRC/trploader.c"           "$OBJ_DIR/trploader.o"
    compile "$KERNEL_SRC/isr.c"                 "$OBJ_DIR/isr.o"

    # trp_manifest.c has a space in the filename — handle carefully
    TRP_MANIFEST_SRC="$KERNEL_SRC/trp manifest.c"
    if [ -f "$TRP_MANIFEST_SRC" ]; then
        compile "$TRP_MANIFEST_SRC"             "$OBJ_DIR/trp_manifest.o"
    fi

    # ── 3. filesystem layer ─────────────────────────────────────────────────
    compile "$ROOT/root/file_formats/trpfs,c"   "$OBJ_DIR/trpfs.o"        2>/dev/null || \
    compile "$ROOT/root/file_formats/trpfs.c"   "$OBJ_DIR/trpfs.o"
    compile "$ROOT/root/file_formats/ntfs.c"    "$OBJ_DIR/ntfs.o"

    # ── 4. installer ────────────────────────────────────────────────────────
    compile "$INSTALLER_SRC/installer.c"        "$OBJ_DIR/installer.o"
    compile "$INSTALLER_SRC/string.c"           "$OBJ_DIR/installer_string.o"

    # ── 5. shell (kernel-mode C shell) ─────────────────────────────────────
    SHELL_C="$ROOT/root/sys/shell/shell.c"
    if [ -f "$SHELL_C" ]; then
        compile "$SHELL_C"                      "$OBJ_DIR/shell.o"
    fi

    # ── 6. link ─────────────────────────────────────────────────────────────
    echo "  LD  $KERNEL_BIN"
    $LD "${LDFLAGS[@]}" \
        "$OBJ_DIR/boot64.o" \
        "$OBJ_DIR/keyboard_isr.o" \
        "$OBJ_DIR/kernel.o" \
        "$OBJ_DIR/serial.o" \
        "$OBJ_DIR/vga.o" \
        "$OBJ_DIR/string.o" \
        "$OBJ_DIR/memory.o" \
        "$OBJ_DIR/pmm.o" \
        "$OBJ_DIR/vmm.o" \
        "$OBJ_DIR/interrupts.o" \
        "$OBJ_DIR/pic.o" \
        "$OBJ_DIR/pit.o" \
        "$OBJ_DIR/timer.o" \
        "$OBJ_DIR/panic.o" \
        "$OBJ_DIR/keyboard.o" \
        "$OBJ_DIR/keyboard_wire.o" \
        "$OBJ_DIR/syscall.o" \
        "$OBJ_DIR/stubs.o" \
        "$OBJ_DIR/runtime_stubs.o" \
        "$OBJ_DIR/process.o" \
        "$OBJ_DIR/loader_enhanced.o" \
        "$OBJ_DIR/trploader.o" \
        "$OBJ_DIR/isr.o" \
        "$OBJ_DIR/trp_manifest.o" \
        "$OBJ_DIR/trpfs.o" \
        "$OBJ_DIR/ntfs.o" \
        "$OBJ_DIR/installer.o" \
        "$OBJ_DIR/installer_string.o" \
        $([ -f "$OBJ_DIR/shell.o" ] && echo "$OBJ_DIR/shell.o") \
        -o "$KERNEL_BIN"

    echo "  Kernel: $KERNEL_BIN  ($(du -h "$KERNEL_BIN" | cut -f1))"

    # ── 7. assemble ISO ─────────────────────────────────────────────────────
    cp "$KERNEL_BIN" "$ISO_ROOT/boot/kernel.bin"
    cp "$GRUB_CFG"   "$ISO_ROOT/boot/grub/grub.cfg"

    echo "  ISO building..."
    grub-mkrescue -o "$ISO_FILE" "$ISO_ROOT" 2>&1

    echo ""
    echo "========================================"
    echo "  ISO ready: $ISO_FILE"
    echo "  Size:      $(du -h "$ISO_FILE" | cut -f1)"
    echo "========================================"
}

# =============================================================================
# run (requires QEMU)
# =============================================================================
do_run() {
    do_build
    echo ""
    echo "[QEMU] Launching ToriginalOS.iso ..."
    qemu-system-x86_64 \
        -cdrom "$ISO_FILE" \
        -serial stdio \
        -m 256M \
        -vga std \
        -no-reboot
}

# =============================================================================
# entry point
# =============================================================================
case "${1:-build}" in
    clean)  do_clean ;;
    run)    do_run   ;;
    build|"") do_build ;;
    *)
        echo "Usage: $0 [build|run|clean]"
        exit 1
        ;;
esac