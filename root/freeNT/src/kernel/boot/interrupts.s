# ==============================================================================
# INTERRUPTS.S - Low-Level Interrupt Stubs
# ==============================================================================
.intel_syntax noprefix
.global load_idt
.global isr_stub
.extern isr_handler

.text
# void load_idt(void* idt_ptr)
load_idt:
    lidt [rdi]
    ret

# Generic stub to catch panics and pass to C
isr_stub:
    cli
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    
    # Call the C panic handler
    call isr_handler
    
    # (We will never reach here because the handler halts, but this is proper structure)
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    sti
    iretq