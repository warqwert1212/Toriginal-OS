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
# FIX (overlap bug): the repo-root linker.ld is a stale reference-only
# stub (see its own header comment - it says as much) with no
# __kernel_end/PHDRS/.asset_blobs support at all. The real linker
# script the Makefile actually uses is kernel64.ld; build.sh was
# pointed at the wrong file, which is why kernel.c's __kernel_end
# reference failed to link and, once that's fixed too, is the file
# that actually needs the PT_LOAD-overlap fix (see kernel64.ld itself).
LINKER_LD="$KERNEL_SRC/boot/kernel64.ld"
ASSET_BLOB_SCRIPT="$KERNEL_SRC/boot/build_asset_blob.py"
TRBL_LOGO_PATH="$KERNEL_SRC/boot/assets/trbl_logo.png"

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

    # ── 0. build the custom bootloader (stage1.bin/stage2.bin) BEFORE
    #      anything else - bootloader_blobs.s below .incbin's them, so
    #      they must exist on disk first. This is a completely separate
    #      build (16/32-bit real/protected-mode code, its own linker
    #      script) from the 64-bit kernel build the rest of this
    #      function does - see build_loader.sh's own header comment. ──
    echo "  LOADER  stage1/stage2"
    "$KERNEL_SRC/boot/loader/build_loader.sh"

    # FIX (overlap bug follow-on): asset_blob.s/trbl_logo_blob.s were
    # never assembled at all by this script (only boot64.s,
    # interrupts.s, keyboard_isr.s, bootloader_blobs.s were
    # hand-listed), which is why kernel.c's g_asset_blob/
    # g_trbl_logo_png references failed to link. Regenerate the
    # packed asset archive up front, same as the Makefile's
    # $(BIN)/asset_blob.bin rule.
    echo "  ASSETS  packing wallpapers/startmenu/cursors"
    ASSET_BLOB_BIN="$BUILD_DIR/asset_blob.bin"
    mkdir -p "$BUILD_DIR"
    python3 "$ASSET_BLOB_SCRIPT" "$ASSET_BLOB_BIN"

    # ── TWO-PASS BUILD ────────────────────────────────────────────────
    # installer.c's write_bootloader_to_disk() needs the exact built
    # kernel.elf file embedded in itself (as real bytes, not a runtime
    # memory reconstruction - the running kernel's loaded segments
    # don't preserve the original file layout, see installer.c's own
    # comment on this for the full reasoning) so it can write a working
    # copy to disk during install. That's a genuine chicken-and-egg
    # problem: the kernel can't embed a file that doesn't exist until
    # the kernel itself finishes building. Solved the same way
    # self-referential payloads always are (e.g. Linux's own bzImage):
    # build twice.
    #
    #   PASS 1 (build_pass PASS1_BUILD "$OBJ_DIR_PASS1" "$KERNEL_PASS1_ELF"):
    #     ordinary kernel build, installer.c compiled with -DPASS1_BUILD
    #     (stub write_bootloader_to_disk(), see that #ifndef guard in
    #     installer.c), bootloader_blobs.o excluded entirely. This is
    #     the exact binary that gets embedded and shipped to disk.
    #
    #   PASS 2 (build_pass "" "$OBJ_DIR" "$KERNEL_ELF"):
    #     real installer.o (no -DPASS1_BUILD), PLUS bootloader_blobs.o,
    #     which .incbin's PASS 1's kernel.elf output alongside
    #     stage1.bin/stage2.bin. This is what actually ships as
    #     ToriginalOS.iso - byte-identical to PASS 1 except for the
    #     added .rodata blob data.
    build_pass() {
        local pass1_define="$1" obj_dir="$2" out_elf="$3"

        rm -rf "$obj_dir"
        mkdir -p "$obj_dir"
        : > "$obj_dir/.objlist"

        local local_assemble
        local_assemble() {
            local src="$1" obj="$2"
            echo "  AS  ${src#"$ROOT"/}"
            mkdir -p "$(dirname "$obj")"
            $AS -c -m64 "$src" -o "$obj"
            echo "$obj" >> "$obj_dir/.objlist"
        }
        local local_compile
        local_compile() {
            local src="$1" extra_defines="$2"
            local rel="${src#"$KERNEL_SRC"/}"
            local obj="$obj_dir/${rel%.c}.o"
            mkdir -p "$(dirname "$obj")"
            echo "  CC  $rel"
            # shellcheck disable=SC2086
            $CC "${CFLAGS[@]}" $extra_defines -c "$src" -o "$obj"
            echo "$obj" >> "$obj_dir/.objlist"
        }

        local_assemble "$KERNEL_SRC/boot/boot64.s"        "$obj_dir/boot/boot64.o"
        local_assemble "$KERNEL_SRC/boot/interrupts.s"    "$obj_dir/boot/interrupts_asm.o"
        local_assemble "$KERNEL_SRC/boot/keyboard_isr.s"  "$obj_dir/boot/keyboard_isr.o"

        # FIX (overlap bug follow-on): both blobs are referenced directly
        # by kernel.c (g_asset_blob/g_trbl_logo_png), not just PASS 2 like
        # bootloader_blobs.s, so assemble them unconditionally in every pass.
        echo "  AS  boot/asset_blob.s"
        mkdir -p "$obj_dir/boot"
        $CC -m64 -x assembler-with-cpp \
            -DASSET_BLOB_PATH="\"$ASSET_BLOB_BIN\"" \
            -c "$KERNEL_SRC/boot/asset_blob.s" -o "$obj_dir/boot/asset_blob.o"
        echo "$obj_dir/boot/asset_blob.o" >> "$obj_dir/.objlist"

        echo "  AS  boot/trbl_logo_blob.s"
        $CC -m64 -x assembler-with-cpp \
            -DTRBL_LOGO_PATH="\"$TRBL_LOGO_PATH\"" \
            -c "$KERNEL_SRC/boot/trbl_logo_blob.s" -o "$obj_dir/boot/trbl_logo_blob.o"
        echo "$obj_dir/boot/trbl_logo_blob.o" >> "$obj_dir/.objlist"

        if [ -z "$pass1_define" ]; then
            # PASS 2 only: bootloader_blobs.s needs real C-preprocessor
            # handling (for the STAGE1_BIN_PATH/STAGE2_BIN_PATH/
            # KERNEL_ELF_PATH .incbin paths - a plain relative .incbin
            # path can't work here, see that file's header comment).
            echo "  AS  boot/bootloader_blobs.s"
            mkdir -p "$obj_dir/boot"
            $CC -m64 -x assembler-with-cpp \
                -DSTAGE1_BIN_PATH="\"$KERNEL_SRC/boot/loader/stage1.bin\"" \
                -DSTAGE2_BIN_PATH="\"$KERNEL_SRC/boot/loader/stage2.bin\"" \
                -DKERNEL_ELF_PATH="\"$KERNEL_PASS1_ELF\"" \
                -c "$KERNEL_SRC/boot/bootloader_blobs.s" -o "$obj_dir/boot/bootloader_blobs.o"
            echo "$obj_dir/boot/bootloader_blobs.o" >> "$obj_dir/.objlist"
        fi

        # ── every other kernel .c, auto-discovered ──────────────────
        while IFS= read -r -d '' src; do
            if [[ "$(basename "$src")" == "graphics_3d.c" ]]; then
                local rel="${src#"$KERNEL_SRC"/}"
                local obj="$obj_dir/${rel%.c}.o"
                mkdir -p "$(dirname "$obj")"
                echo "  CC  $rel (with -msse2, see build.sh comment)"
                $CC "${CFLAGS[@]}" -mmmx -msse -msse2 -c "$src" -o "$obj"
                echo "$obj" >> "$obj_dir/.objlist"
            elif [[ "$(basename "$src")" == "installer.c" ]]; then
                # The one file whose compilation genuinely differs
                # between passes - see the TWO-PASS BUILD comment above.
                local_compile "$src" "$pass1_define"
            else
                local_compile "$src" ""
            fi
        done < <(find "$KERNEL_SRC" -maxdepth 1 -iname '*.c' -print0 | sort -z)

        # ── sys/shell/shell.c (see original comment: two files share
        #    the basename shell.c, this is the CLI dispatcher one) ────
        local SYS_SHELL_C="$ROOT/root/sys/shell/shell.c"
        if [ -f "$SYS_SHELL_C" ]; then
            echo "  CC  sys/shell/shell.c"
            mkdir -p "$obj_dir/sys_shell"
            $CC "${CFLAGS[@]}" -c "$SYS_SHELL_C" -o "$obj_dir/sys_shell/shell.o"
            echo "$obj_dir/sys_shell/shell.o" >> "$obj_dir/.objlist"
        else
            echo "  !!  $SYS_SHELL_C not found - kernel_os_shell() will fail to" \
                 "link against sys_shell_dispatch()"
            exit 1
        fi

        echo "  LD  $out_elf"
        mkdir -p "$(dirname "$out_elf")"
        mapfile -t ALL_OBJS < "$obj_dir/.objlist"
        $LD "${LDFLAGS[@]}" "${ALL_OBJS[@]}" -o "$out_elf"
        echo "  Kernel: $out_elf  ($(du -h "$out_elf" | cut -f1))"
    }

    KERNEL_PASS1_ELF="$BUILD_DIR/kernel_pass1.elf"
    OBJ_DIR_PASS1="$BUILD_DIR/obj_pass1"

    echo ""
    echo "── PASS 1 (no bootloader blob) ──"
    build_pass "-DPASS1_BUILD" "$OBJ_DIR_PASS1" "$KERNEL_PASS1_ELF"

    echo ""
    echo "── PASS 2 (with embedded bootloader + kernel copy) ──"
    mkdir -p "$ISO_ROOT/boot/grub"
    build_pass "" "$OBJ_DIR" "$KERNEL_ELF"

    # ── assemble ISO ─────────────────────────────────────────────────
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
