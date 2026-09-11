#!/bin/bash
# =============================================================================
# build_loader.sh — builds stage1.bin and stage2.bin (the custom
# bootloader that replaces GRUB for an on-disk install; see
# installer.c's write_bootloader_to_disk()).
#
# Uses the same GAS/binutils toolchain as the rest of this project
# (x86_64-linux-gnu-as/-ld/-objcopy) - no new dependency (e.g. NASM)
# beyond what Makefile/build.sh already require, even though NASM is
# listed in the Makefile's 'tools:' target for unrelated reasons.
#
# MUST run before kernel/boot/bootloader_blobs.s is assembled -
# that file .incbin's the two flat binaries this script produces.
# Both Makefile and build.sh invoke this script first for that reason
# - see the BUILD ORDER NOTE comment in each.
# =============================================================================
set -e

AS=x86_64-linux-gnu-as
LD=x86_64-linux-gnu-ld
OBJCOPY=x86_64-linux-gnu-objcopy

cd "$(dirname "$0")"

echo "[TRBL] assembling stage1..."
$AS --32 -o stage1.o stage1.s
$LD -m elf_i386 -Ttext 0x7C00 --oformat binary -o stage1.bin stage1.o

STAGE1_SIZE=$(stat -c%s stage1.bin 2>/dev/null || stat -f%z stage1.bin)
if [ "$STAGE1_SIZE" -ne 512 ]; then
    echo "[TRBL] FATAL: stage1.bin is $STAGE1_SIZE bytes, must be exactly 512"
    exit 1
fi

echo "[TRBL] assembling stage2..."
$AS --32 -o stage2.o stage2.s
$AS --32 -o stage2_pm.o stage2_pm.s
$LD -m elf_i386 -T stage2.ld -o stage2.elf stage2.o stage2_pm.o
$OBJCOPY -O binary stage2.elf stage2.bin

STAGE2_SIZE=$(stat -c%s stage2.bin 2>/dev/null || stat -f%z stage2.bin)
STAGE2_BUDGET=$((64 * 512))
if [ "$STAGE2_SIZE" -gt "$STAGE2_BUDGET" ]; then
    echo "[TRBL] FATAL: stage2.bin is $STAGE2_SIZE bytes, exceeds the" \
         "$STAGE2_BUDGET-byte (64-sector) budget stage1 loads - grow" \
         "STAGE2_SECTORS in stage1.s and KERNEL_LOAD_LBA in stage2.s" \
         "(and installer.c's matching layout) together if stage2 ever" \
         "needs to grow past this."
    exit 1
fi

echo "[TRBL] stage1.bin: $STAGE1_SIZE bytes, stage2.bin: $STAGE2_SIZE bytes (budget $STAGE2_BUDGET)"

# stage2_offsets.h no longer exists / is no longer generated: TRBL's
# boot-time settings (resolution, network, boot flags) now live in
# their own dedicated on-disk sector (see include/trbl_settings.h and
# kernel/trbl_settings.c) that stage2.s reads at every boot, instead
# of being byte-patched into this compiled binary at install time.
# stage1.bin/stage2.bin are therefore identical across every install -
# installer.c writes them verbatim and only the settings sector
# differs per-machine.

echo "[TRBL] OK"
