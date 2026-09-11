

.code32
.section .text
.global elf_load_segments
.global build_multiboot2_info

/* FIX: was 0x20000. Real mode (stage2.s's load_kernel_elf) can only
 * ask the BIOS to place a disk read at a plain segment:offset
 * address, which tops out just over 1 MiB - nowhere near enough once
 * the kernel started embedding its own boot assets (many MiB now,
 * budgeted up to 32 MiB - see ata.h's ATA_BOOT_RESERVED_SECTORS
 * comment). stage2.s now reads each chunk into a small low buffer and
 * copies it up here via "unreal mode" (see load_kernel_elf's comment
 * there) - this symbol just needs to name the same high, flat address
 * both files agree on. Kept well clear of the kernel's own
 * 0x200000-0x2000000 load window (kernel64.ld) so this staging copy
 * can never overlap the segments being copied out of it. */
.set ELF_STAGE_ADDR, 0x4000000
.set MB2_INFO_ADDR,  0x30000

/* Same link-origin-vs-runtime-load mismatch as stage2.ld documents
 * for gdt32_pointer: this file links at address 0 (see stage2.ld)
 * but stage2 actually runs loaded at linear 0x9000. fb_found/
 * fb_addr/fb_pitch/fb_width/fb_height/fb_bpp are real-mode
 * variables stage2.s's vbe_set_mode wrote using DS=0x0900 segment
 * addressing (so their real runtime location is 0x9000+offset) -
 * reading them here as bare symbols would use the link-time
 * offset-from-0 value instead, landing on unrelated low memory and
 * picking up zeros/garbage instead of the real VBE-computed values.
 * Every read below adds STAGE2_LINEAR_BASE for exactly that reason. */
.set STAGE2_LINEAR_BASE, 0x9000

/* Elf64_Ehdr field offsets used here */
.set EHDR_E_ENTRY, 24
.set EHDR_E_PHOFF, 32
.set EHDR_E_PHENTSIZE, 54
.set EHDR_E_PHNUM, 56

/* Elf64_Phdr field offsets */
.set PHDR_P_TYPE,   0
.set PHDR_P_OFFSET, 8
.set PHDR_P_VADDR,  16
.set PHDR_P_FILESZ, 32
.set PHDR_P_MEMSZ,  40
.set PT_LOAD, 1

elf_load_segments:
    pushal

    /* ESI = ELF file base (real mode staged it here). */
    movl    $ELF_STAGE_ADDR, %esi

    /* EDI = pointer to first program header = base + e_phoff (low 32
     * bits only - see file header comment). */
    movl    ELF_STAGE_ADDR + EHDR_E_PHOFF, %edi
    addl    %esi, %edi

    /* ECX = e_phnum (loop counter), EBX = e_phentsize (stride). */
    movzwl  ELF_STAGE_ADDR + EHDR_E_PHNUM, %ecx
    movzwl  ELF_STAGE_ADDR + EHDR_E_PHENTSIZE, %ebx

phdr_loop:
    cmpl    $0, %ecx
    je      elf_load_done

    movl    (%edi), %eax               /* p_type */
    cmpl    $PT_LOAD, %eax
    jne     phdr_skip

    /* Save the loop state we still need after this segment (EDI =
     * current phdr pointer, EBX = phentsize, ECX = remaining count)
     * in callee-saved-style registers instead of the stack, so there
     * is no offset arithmetic to get wrong: EBP/loop regs untouched
     * by the copy below, which only uses EAX/EDX/ESI/EDI/ECX. */
    pushl   %ebx                       /* [esp+8] = phentsize (restored after) */
    pushl   %ecx                       /* [esp+4] = remaining phdr count */
    pushl   %edi                       /* [esp+0] = this phdr's pointer */

    movl    PHDR_P_FILESZ(%edi), %ebx  /* EBX = filesz, kept past the copy */
    movl    PHDR_P_MEMSZ(%edi), %edx   /* EDX = memsz, kept past the copy */
    movl    PHDR_P_OFFSET(%edi), %eax
    addl    %esi, %eax                 /* EAX = source ptr = ELF base + p_offset */
    movl    PHDR_P_VADDR(%edi), %edi   /* EDI = dest ptr = p_vaddr (overwrites
                                         * the phdr pointer in EDI - the copy
                                         * we just pushed to the stack is the
                                         * one we'll restore afterward) */
    movl    %eax, %esi                 /* ESI = source ptr (was ELF base -
                                         * no longer needed as such once every
                                         * phdr's file offset is already
                                         * resolved to an absolute address) */
    movl    %ebx, %ecx                 /* ECX = filesz = copy length */
    cld
    rep     movsb                      /* copy filesz bytes: [[esp+0]].p_offset+base -> [[esp+0]].p_vaddr */

    /* EDI now sits right after the copied bytes (movsb advanced it
     * dest-side). Zero (memsz - filesz) more bytes right there. */
    subl    %ebx, %edx                 /* EDX = memsz - filesz */
    jle     zero_tail_done             /* <= 0: nothing to zero */
    movl    %edx, %ecx
    xorl    %eax, %eax
    cld
    rep     stosb
zero_tail_done:

    popl    %edi                       /* restore this phdr's pointer */
    popl    %ecx                       /* restore remaining count */
    popl    %ebx                       /* restore phentsize */

    /* ESI is no longer the ELF base after the copy above - restore it
     * from EDI (still a valid phdr pointer) is wrong too, so instead
     * recompute it the same way it was first set: ELF base is a fixed
     * constant, not something that needs preserving across the loop. */
    movl    $ELF_STAGE_ADDR, %esi

phdr_skip:
    addl    %ebx, %edi                 /* advance to next program header */
    decl    %ecx
    jmp     phdr_loop

elf_load_done:
    popal
    ret

/* ── multiboot2 info block builder ─────────────────────────────────────────
 * Layout (matches kernel.c's mb2_tag_t/mb2_tag_fb_t exactly):
 *   [0]  total_size (u32)   - filled in last, once the real size is known
 *   [4]  reserved (u32)     = 0
 *   [8]  framebuffer tag (only emitted if fb_found != 0):
 *          type=8 size=32 (padded from 27 to the next 8-byte boundary)
 *          framebuffer_addr (u64), pitch (u32), width (u32),
 *          height (u32), bpp (u8), fb_type=1 (u8), reserved (u8),
 *          + 5 bytes padding to reach the 32-byte aligned tag size
 *   [..] end tag: type=0 size=8
 */
build_multiboot2_info:
    pushal

    movl    $MB2_INFO_ADDR, %edi
    addl    $8, %edi                   /* leave room for total_size+reserved */

    cmpb    $0, fb_found + STAGE2_LINEAR_BASE
    je      no_fb_tag

    movl    $8, (%edi)                 /* type = MB2_TAG_FB */
    movl    $32, 4(%edi)               /* size = 32 (27 rounded to 8) */
    movl    fb_addr + STAGE2_LINEAR_BASE, %eax
    movl    %eax, 8(%edi)              /* framebuffer_addr low 32 bits */
    movl    $0, 12(%edi)               /* framebuffer_addr high 32 bits */
    movl    fb_pitch + STAGE2_LINEAR_BASE, %eax
    movl    %eax, 16(%edi)
    movl    fb_width + STAGE2_LINEAR_BASE, %eax
    movl    %eax, 20(%edi)
    movl    fb_height + STAGE2_LINEAR_BASE, %eax
    movl    %eax, 24(%edi)
    movb    fb_bpp + STAGE2_LINEAR_BASE, %al
    movb    %al, 28(%edi)              /* framebuffer_bpp */
    movb    $1, 29(%edi)               /* framebuffer_type = 1 (direct RGB) */
    movb    $0, 30(%edi)               /* reserved */
    movb    $0, 31(%edi)               /* padding to 32-byte tag size */
    addl    $32, %edi

no_fb_tag:
    /* End tag: type=0, size=8 */
    movl    $0, (%edi)
    movl    $8, 4(%edi)
    addl    $8, %edi

    /* total_size = EDI - MB2_INFO_ADDR */
    movl    %edi, %eax
    subl    $MB2_INFO_ADDR, %eax
    movl    $MB2_INFO_ADDR, %edi
    movl    %eax, (%edi)
    movl    $0, 4(%edi)

    popal
    ret

/* fb_* variables are defined in stage2.s (vbe_set_mode fills them in
 * real mode); referenced here by name since both files are assembled
 * and linked together into one flat binary. */
.extern fb_found
.extern fb_addr
.extern fb_pitch
.extern fb_width
.extern fb_height
.extern fb_bpp
