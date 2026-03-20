BITS 64
global syscall64_entry
extern __syscall_handler

section .text
syscall64_entry:
    ; Syscall ABI: rax=num, rdi=a1, rsi=a2, rdx=a3, r10=a4, r8=a5, r9=a6
    ; Switch to kernel stack via GS base.
    swapgs
    mov [gs:8], rsp
    mov rsp, [gs:0]

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov eax, es
    push rax
    mov eax, ds
    push rax

    cld
    mov eax, 0x10
    mov ds, eax
    mov es, eax

    xor rbp, rbp
    mov rdi, rsp
    call __syscall_handler

    pop rax
    mov ds, eax
    pop rax
    mov es, eax

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; set FS base from task GS data
    mov rax, [gs:16]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000100
    wrmsr

    mov rsp, [gs:8]
    swapgs
    o64 sysret
