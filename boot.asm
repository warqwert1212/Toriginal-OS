; boot.asm - Bootloader file

org 0x7C00  ; Origin at bootable sector

start:
    ; Initialize the system
    xor ax, ax      ; Clear AX register
    mov ds, ax      ; Set DS to 0

    ; Jump to the kernel entry point
    jmp 0x0000:kernel_entry

kernel_entry:
    ; Kernel code starts here
    mov ah, 0x0E    ; Teletype output function
    mov si, message
.loop:
    lodsb           ; Load byte at DS:SI into AL
    cmp al, 0       ; Check for null terminator
    je .done        ; If zero, we are done
    int 0x10       ; Call BIOS interrupt
    jmp .loop       ; Repeat
.done:
    cli             ; Disable interrupts
    hlt             ; Halt the CPU

message db 'Hello from Bootloader!', 0

times 510-($-$$) db 0 ; Fill the rest of the sector with zeros
 dw 0xAA55          ; Boot signature