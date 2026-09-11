/* =============================================================================
 * BOOTLOADER_BLOBS.S — embeds the built stage1.bin/stage2.bin flat
 * binaries into the kernel image as plain data.
 *
 * installer.c's write_bootloader_to_disk() writes these bytes
 * directly to the disk's reserved boot region (see ata.h's
 * ATA_BOOT_RESERVED_SECTORS) during first-boot staging - this is how
 * a disk installed by this OS becomes bootable on its own, without
 * GRUB or the install CD. Assembled with the same 64-bit toolchain as
 * the rest of the kernel (Makefile/build.sh's normal .s rule) -
 * .incbin doesn't care what CPU mode the *embedded* bytes represent,
 * only the code around it does, and there's no code here, only data.
 *
 * The actual stage1.bin/stage2.bin build step (real-mode/protected-
 * mode assembly + link, entirely separate from the kernel's own
 * toolchain invocation) happens in kernel/boot/loader/build_loader.sh,
 * run by Makefile/build.sh before this file is assembled - see
 * BUILD ORDER NOTE in both.
 * ============================================================================= */

.section .kernel_blob, "a"
.global g_stage1_blob
.global g_stage1_blob_size
.global g_stage2_blob
.global g_stage2_blob_size
.global g_kernel_elf_blob
.global g_kernel_elf_blob_size

/* STAGE1_BIN_PATH/STAGE2_BIN_PATH/KERNEL_ELF_PATH are supplied on the
 * assembler command line as absolute paths (-D..., via -x
 * assembler-with-cpp) by whichever build script is running (Makefile
 * or build.sh) - see BUILD ORDER NOTE in both. GAS resolves .incbin
 * relative to its OWN current working directory, not the .s file's
 * location and not any -I search path (-I only affects .include, not
 * .incbin) - Makefile and build.sh invoke the assembler from two
 * different working directories, so a plain relative path here would
 * work for one and silently fail (or worse, silently pick up a stale
 * file) for the other. Absolute paths remove that ambiguity entirely.
 *
 * KERNEL_ELF_PATH specifically points at the PASS 1 kernel build (see
 * Makefile/build.sh's TWO-PASS BUILD sections) - a completely
 * ordinary kernel.elf with no bootloader blob embedded in it. This
 * file (bootloader_blobs.s) is only ever assembled as part of PASS 2,
 * which embeds PASS 1's exact output and relinks - so the disk-
 * embedded copy installer.c later writes is byte-identical to a
 * plain, from-scratch build, never a copy-of-a-copy or anything
 * self-referential in a way that could recurse. */
#ifndef STAGE1_BIN_PATH
#error "STAGE1_BIN_PATH must be defined (absolute path to stage1.bin) - see Makefile/build.sh"
#endif
#ifndef STAGE2_BIN_PATH
#error "STAGE2_BIN_PATH must be defined (absolute path to stage2.bin) - see Makefile/build.sh"
#endif
#ifndef KERNEL_ELF_PATH
#error "KERNEL_ELF_PATH must be defined (absolute path to the PASS 1 kernel.elf) - see Makefile/build.sh"
#endif

.align 16
g_stage1_blob:
    .incbin STAGE1_BIN_PATH
g_stage1_blob_end:

.align 16
g_stage2_blob:
    .incbin STAGE2_BIN_PATH
g_stage2_blob_end:

.align 16
g_kernel_elf_blob:
    .incbin KERNEL_ELF_PATH
g_kernel_elf_blob_end:

.section .kernel_blob, "a"
.align 8
g_stage1_blob_size:
    .quad g_stage1_blob_end - g_stage1_blob
g_stage2_blob_size:
    .quad g_stage2_blob_end - g_stage2_blob
g_kernel_elf_blob_size:
    .quad g_kernel_elf_blob_end - g_kernel_elf_blob
