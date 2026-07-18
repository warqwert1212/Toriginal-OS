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
#
# -----------------------------------------------------------------------------
# FIX (this update): the previous version of this script hand-listed every
# kernel .c/.o file by name, and had drifted badly out of sync with what's
# actually in root/freeNT/kernel - it referenced paths that don't exist
# (root/installer, root/file_formats, mm/memory.c, Pmm.c, vmm.c), misspelled
# filenames that only ever existed as those misspellings in the script itself
# (interrups.c, keybord.c, loader_enhaced.c - the real files are
# interrupts.c, keyboard.c, loader_enhanced.c), and - most importantly -
# never compiled more than half the kernel's real source files at all
# (desktop.c, gfx_terminal.c, cursor.c, mouse.c, apic.c, acpi.c, ata.c,
# ahci.c, uhci.c, usb.c, usb_hid.c, ps2.c, png.c, deflate.c, sha256.c,
# graphics_core.c/2d.c/3d.c, font8x16.c, trp_manifest.c, installer.c,
# net.c/tcp.c/udp.c/ip.c/arp.c/icmp.c/dns.c/http.c/rtl8139.c, sced.c,
# boot_detect.c, auth.c, and — critically for this update — the new
# wm.c/desktop_menu.c were never being linked in at all). A hand-maintained
# file list is exactly the kind of thing that silently rots the moment one
# new file is added and the script isn't updated in the same commit - so
# this version auto-discovers every .c under root/freeNT/kernel (recursively,
# picking up boot/ separately for .s files) instead of naming them one by one.
# The only files still named explicitly are the two whose link ORDER matters
# (the multiboot-header-carrying boot64.o must be first) or which have a
# real naming ambiguity to resolve (shell.c exists in two different
# directories with two different jobs - see shell.h's own comment on that).
# -----------------------------------------------------------------------------

set -e   # exit on first error

# ── Configurable paths ───────────────────────────────────────────────────────
ROOT="$(cd "$(dirname "$0")" && pwd)"   # script location = repo root

KERNEL_SRC="$ROOT/root/freeNT/kernel"
INCLUDE_DIR="$ROOT/root/freeNT/include"
GRUB_CFG="$ROOT/root/freeNT/isodir/boot/grub/grub.cfg"
LINKER_LD="$ROOT/linker.ld"

BUILD_DIR="$ROOT/build"
ISO_ROOT="$BUILD_DIR/iso_root"
OBJ_DIR="$BUILD_DIR/obj"
KERNEL_ELF="$BUILD_DIR/kernel.elf"
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
)

LDFLAGS=(
    -m elf_x86_64
    -nostdlib
    -no-pie
    -z noexecstack
    -T "$LINKER_LD"
)

# =============================================================================
# Helper: compile one C file -> .o (mirrors source tree under OBJ_DIR so two
# same-named files in different directories - e.g. the two shell.c's - don't
# collide on one flat output filename).
# =============================================================================
compile() {
    local src="$1"
    local rel="${src#"$KERNEL_SRC"/}"
    local obj="$OBJ_DIR/${rel%.c}.o"
    mkdir -p "$(dirname "$obj")"
    echo "  CC  $rel"
    $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
    echo "$obj" >> "$OBJ_DIR/.objlist"
}

assemble() {
    local src="$1" obj="$2"
    echo "  AS  ${src#"$ROOT"/}"
    mkdir -p "$(dirname "$obj")"
    $AS -c -m64 "$src" -o "$obj"
    echo "$obj" >> "$OBJ_DIR/.objlist"
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

    rm -rf "$OBJ_DIR"
    mkdir -p "$OBJ_DIR" "$ISO_ROOT/boot/grub"
    : > "$OBJ_DIR/.objlist"

    # ── 1. boot stubs (order matters: boot64.o's multiboot header must be
    #      first in the final link, see linker.ld's KEEP(*(.multiboot))) ────
    assemble "$KERNEL_SRC/boot/boot64.s"        "$OBJ_DIR/boot/boot64.o"
    assemble "$KERNEL_SRC/boot/interrupts.s"    "$OBJ_DIR/boot/interrupts_asm.o"
    assemble "$KERNEL_SRC/boot/keyboard_isr.s"  "$OBJ_DIR/boot/keyboard_isr.o"

    # ── 2. every other kernel .c, auto-discovered ───────────────────────────
    while IFS= read -r -d '' src; do
        # FIX: graphics_3d.c returns small float structs (vec3_t, mat4_t)
        # by value - the x86-64 SysV ABI passes/returns those in XMM
        # registers, which -mno-sse (used kernel-wide, since nothing else
        # touches floats and interrupt handlers don't save/restore SSE
        # state on every entry) makes flatly impossible to compile
        # ("SSE register return with SSE disabled"). This was never caught
        # before because the old build.sh never actually compiled this
        # file at all (see this script's top-of-file comment) - discovered
        # for the first time by this rewrite actually building everything.
        # Nothing in the kernel calls into graphics_3d.c yet (it's staged,
        # unused code for a future 3D pipeline), so enabling SSE2 for just
        # this one translation unit is safe today; the moment something
        # calls these functions from interrupt/ISR context, that call site
        # needs its own fxsave/fxrstor around the call (see process.c's
        # fxsave_state/fxrstor_state for the existing pattern) since this
        # flag alone doesn't make ISRs SSE-safe.
        if [[ "$(basename "$src")" == "graphics_3d.c" ]]; then
            rel="${src#"$KERNEL_SRC"/}"
            obj="$OBJ_DIR/${rel%.c}.o"
            mkdir -p "$(dirname "$obj")"
            echo "  CC  $rel (with -msse2, see build.sh comment)"
            $CC "${CFLAGS[@]}" -mmmx -msse -msse2 -c "$src" -o "$obj"
            echo "$obj" >> "$OBJ_DIR/.objlist"
        else
            compile "$src"
        fi
    done < <(find "$KERNEL_SRC" -maxdepth 1 -iname '*.c' -print0 | sort -z)

    # ── 3. sys/shell/shell.c — the command dispatcher (kernel_os_shell,
    #      defined in $KERNEL_SRC/shell.c, calls sys_shell_dispatch, defined
    #      here - see shell.h's comment on why these are two separate files
    #      with the same base name). Compiled into its own object path
    #      (sys_shell/shell.o) so it never collides with $OBJ_DIR/shell.o
    #      from step 2 above. ──────────────────────────────────────────────
    SYS_SHELL_C="$ROOT/root/sys/shell/shell.c"
    if [ -f "$SYS_SHELL_C" ]; then
        echo "  CC  sys/shell/shell.c"
        mkdir -p "$OBJ_DIR/sys_shell"
        $CC "${CFLAGS[@]}" -c "$SYS_SHELL_C" -o "$OBJ_DIR/sys_shell/shell.o"
        echo "$OBJ_DIR/sys_shell/shell.o" >> "$OBJ_DIR/.objlist"
    else
        echo "  !!  $SYS_SHELL_C not found - kernel_os_shell() will fail to" \
             "link against sys_shell_dispatch()"
        exit 1
    fi

    # ── 4. link everything the .objlist collected ───────────────────────────
    echo "  LD  $KERNEL_ELF"
    mapfile -t ALL_OBJS < "$OBJ_DIR/.objlist"
    $LD "${LDFLAGS[@]}" "${ALL_OBJS[@]}" -o "$KERNEL_ELF"

    echo "  Kernel: $KERNEL_ELF  ($(du -h "$KERNEL_ELF" | cut -f1))"

    # ── 5. assemble ISO ──────────────────────────────────────────────────────
    cp "$KERNEL_ELF" "$ISO_ROOT/boot/kernel.elf"
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
