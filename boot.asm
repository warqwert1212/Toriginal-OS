[BITS 16]
org 0x7C00

; Bootloader entry point
start:
    cli                         ; Disable interrupts
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00              ; Set stack pointer
    
    ; Print bootloader message
    mov si, boot_msg
    call print_string
    
    ; Enable A20 line
    call enable_a20
    
    ; Load GDT
    lgdt [gdt_descriptor]
    
    ; Set PE (Protection Enable) bit in CR0
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Far jump to 32-bit code
    jmp 0x08:protected_mode

; Print string subroutine
print_string:
    .loop:
        lodsb
        cmp al, 0
        je .done
        mov ah, 0x0E
        int 0x10
        jmp .loop
    .done:
        ret

; Enable A20 line
enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

; Messages
boot_msg db 'Toriganal OS Bootloader v1.0', 13, 10, 'Entering Protected Mode...', 13, 10, 0

; GDT Table
gdt_start:
    ; Null descriptor
    dq 0x0
    
    ; Code descriptor
    dw 0xFFFF           ; Limit
    dw 0x0000           ; Base (low)
    db 0x00             ; Base (mid)
    db 10011010b        ; Access byte
    db 11001111b        ; Granularity
    db 0x00             ; Base (high)
    
    ; Data descriptor
    dw 0xFFFF           ; Limit
    dw 0x0000           ; Base (low)
    db 0x00             ; Base (mid)
    db 10010010b        ; Access byte
    db 11001111b        ; Granularity
    db 0x00             ; Base (high)

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

[BITS 32]
protected_mode:
    ; Set data segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    
    ; Set up stack
    mov esp, 0x90000
    
    ; Call kernel main
    call kernel_main
    
    ; Halt
    cli
    hlt

extern kernel_main

; Boot signature
times 510-($-$$) db 0
dw 0xAA55
