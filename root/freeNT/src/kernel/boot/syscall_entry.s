# ==============================================================================
# SYSCALL_ENTRY.S - Safe Ring Transition Trapping Matrix
# ==============================================================================
.intel_syntax noprefix
.global syscall_stub
.extern native_syscall_handler

syscall_stub:
    # 1. Swap user stack pointer safely out of user space bounds into Kernel Heap allocations
    mov [gs:0x10], rsp           # Cache user stack pointer layout trace
    mov rsp, [gs:0x08]           # Extract active safe tracking Kernel Stack Pointer context

    # 2. Preserve caller contexts on secure boundaries
    push rcx                     # User Space RIP tracking register
    push r11                     # User Space RFLAGS register tracking layout state
    push rbp
    push rbx
    push rdi
    push rsi
    push rdx
    push r12
    push r13
    push r14
    push r15

    # 3. Route arguments into C-level parameters securely
    # System V ABI expectations: arg0 -> rdi, arg1 -> rsi, arg2 -> rdx
    # Source mappings from user: call_id = rax, arg1 = rdi, arg2 = rsi
    mov rdx, rsi                 # Move user arg2 into C param2 (rdx) first
    mov rsi, rdi                 # Move user arg1 into C param1 (rsi) second
    mov rdi, rax                 # Move user call_id into C param0 (rdi) third
    call native_syscall_handler

    # 4. Restore state variables sequentially
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdx
    pop rsi
    pop rdi
    pop rbx
    pop rbp                      # Fixed typo: changed rprev to rbp
    pop r11                      # Restore target system flags
    pop rcx                      # Restore user execution target return pointer (RIP)

    mov rsp, [gs:0x10]           # Recover original user execution space stack tracking pointer
    sysretq                      # Move execution boundaries backwards into User Ring-3 contexts