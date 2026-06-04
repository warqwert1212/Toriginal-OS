# ==============================================================================
# ENTRY.S - Strict Production-Grade 32 to 64-bit Trampoline
# ==============================================================================
.global _start
.intel_syntax noprefix

.section .text.boot
.align 4
multiboot_header:
    .long 0x1BADB002              # Magic
    .long 0x00000003              # Flags (ALIGN + MEMINFO)
    .long -(0x1BADB002 + 0x00000003)

_start:
    cli
    # Initialize bootstrap stack pointer aligned to 16-bytes for System V ABI
    mov esp, offset boot_stack_top
    
    # Save multiboot information registers before modifying eax/ebx
    mov edx, eax                  # Save Magic
    mov ecx, ebx                  # Save Multiboot Info Structure Pointer

    # Verify Multiboot Magic
    cmp edx, 0x2BADB002
    jne .panic

    # Clear page tables out systematically (3 tables * 4096 bytes = 12288 bytes)
    mov edi, offset p4_table
    xor eax, eax
    mov ecx, 3072                 # 12288 / 4
    rep stosd

    # Link P4 to P3
    mov eax, offset p3_table
    or eax, 0x3                   # Present + Writable (Kernel Level)
    mov dword ptr [p4_table], eax                 # Identity map low memory
    mov dword ptr [p4_table + 256 * 8], eax       # Higher half map (0xFFFF800000000000)
    mov dword ptr [p4_table + 511 * 8], offset p4_table # Recursive paging entry (Slot 511)
    
    # Link P3 to P2
    mov eax, offset p2_table
    or eax, 0x3
    mov dword ptr [p3_table], eax                 # Low mapping window
    mov dword ptr [p3_table + 511 * 8], eax       # High mapping window index match

    # Map P2 to 2MB Huge Pages (Identity mapping first 16MB of physical memory)
    mov ecx, 0
.map_p2_loop:
    mov eax, 0x200000             # Multiply factor: 2MB
    mul ecx
    or eax, 0x83                  # Present + Writable + Huge Page Attribute (Bit 7)
    mov dword ptr [p2_table + ecx * 8], eax
    inc ecx
    cmp ecx, 8                    # 8 * 2MB = 16MB mapped physical memory allocation
    jne .map_p2_loop

    # Load top level page table directory to CR3
    mov eax, offset p4_table
    mov cr3, eax

    # Enable PAE (Physical Address Extension)
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax

    # Set Long Mode Enable bit in EFER MSR
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100                 # LME bit
    wrmsr

    # Enable Paging (CR0 Bit 31)
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    # Load Hardware 64-bit Global Descriptor Table Pointer
    lgdt [gdt64_pointer]
    
    # Push the multiboot registers to the stack prior to long mode transition
    push ecx                      # Multiboot structure reference location
    push edx                      # Multiboot magic reference validation

    # Long jump directly to the 64-bit flat code selector segment
    jmp 0x08:offset long_mode_entry

.code64
long_mode_entry:
    # Reload selectors with 64-bit kernel data space
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    # Pop structural parameters into correct AMD64 ABI registers
    pop rdi                       # Argument 1: Magic Number
    pop rsi                       # Argument 2: Multiboot structure reference pointer

    # Ensure stack pointer alignment constraints remain satisfied
    mov rsp, offset boot_stack_top

    # Clean absolute jump out of identity space straight into higher-half virtual layout
    mov rax, offset kernel_main
    jmp rax

.panic:
    mov edi, 0xB8000
    mov dword ptr [rdi], 0x4F524F45 # Out "ER" flashing red to standard VGA fallback text output
    cli
.hlt_loop:
    hlt
    jmp .hlt_loop

.section .data.boot
.align 16
gdt64:
    .quad 0x0000000000000000      # Null Descriptor
    .quad 0x00209A0000000000      # Ring 0 - 64-bit Code Segment (Kernel)
    .quad 0x0000920000000000      # Ring 0 - 64-bit Data Segment (Kernel)
    .quad 0x0020FA0000000000      # Ring 3 - 64-bit Code Segment (User Space Application Execution)
    .quad 0x0000F20000000000      # Ring 3 - 64-bit Data Segment (User Space Application Execution)
gdt64_pointer:
    .word . - gdt64 - 1
    .quad offset gdt64

.section .bss.boot
.align 4096
p4_table:
    .skip 4096
p3_table:
    .skip 4096
p2_table:
    .skip 4096
.align 16
boot_stack_bottom:
    .skip 16384                   # Fully dedicated 16KB execution bounds area
boot_stack_top: