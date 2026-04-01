; x86-64 Assembly Interface for System Calls

section .text
extern syscall

; Define syscall numbers
%define SYS_READ 0
%define SYS_WRITE 1
%define SYS_OPEN 2
%define SYS_CLOSE 3

; Utility for making system calls
; rax = syscall number
; rdi = first argument
; rsi = second argument
; rdx = third argument
;
; Example: Using write syscall

; Write to stdout (file descriptor 1)

global _start
_start:
    mov rax, SYS_WRITE     ; syscall number for write
    mov rdi, 1            ; file descriptor (1 = stdout)
    mov rsi, msg          ; pointer to message
    mov rdx, msg_len      ; message length
    syscall                ; invoke syscall
    mov rax, SYS_EXIT     ; syscall number for exit
    xor rdi, rdi          ; exit code 0
    syscall                ; invoke syscall

section .data
msg db 'Hello, World!', 0xa   ; message to write
msg_len equ $ - msg              ; length of message
