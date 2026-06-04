# ==============================================================================
# GDT_FLUSH.S - Low-Level Segment Registers and Privilege Overrides
# ==============================================================================
.intel_syntax noprefix
.global flush_gdt
.global jump_to_user

flush_gdt:
    lgdt [rdi]
    # Reload data segment structures
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    # Force Code Segment reload via a far return sequence
    push 0x10           # SS Selector
    push rsp            # Current RSP
    pushf               # Current RFLAGS
    push 0x08           # CS Selector
    lea rax, [rip + .reload_cs]
    push rax
    iretq
.reload_cs:
    # Load Task Register pointing to TSS segment index 0x28
    mov ax, 0x28
    ltr ax
    ret

# void jump_to_user(uint64_t user_entry, uint64_t user_stack)
jump_to_user:
    cli                 # Clear interrupts during transition context swap
    
    # Push data tracking configuration matching Ring 3 segment layouts
    push 0x23           # User Data Segment Selector (0x20 | Request Privilege Level 3)
    push rsi            # User Space Target Stack Address Pointer (RSP)
    push 0x202          # RFLAGS Interfacing state (Interrupts enabled in Ring 3)
    push 0x1B           # User Code Segment Selector (0x18 | Request Privilege Level 3)
    push rdi            # User Space Executable Entry Target Point (RIP)
    
    # Clear general registers to prevent register data leaking from Ring 0 to Ring 3
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rbp, rbp
    xor r8,  r8
    xor r9,  r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    
    iretq               # Burn privilege barriers down into Ring 3