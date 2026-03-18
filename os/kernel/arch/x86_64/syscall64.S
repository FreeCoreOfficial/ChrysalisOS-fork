BITS 64
global syscall64_entry
extern syscall64_dispatch

section .text
syscall64_entry:
    ; Syscall ABI: rax=num, rdi=a1, rsi=a2, rdx=a3, r10=a4, r8=a5, r9=a6
    push rcx
    push r11

    mov rcx, r10

    push r9
    push r8
    push rcx
    push rdx
    push rsi
    push rdi
    push rax

    call syscall64_dispatch

    add rsp, 56
    pop r11
    pop rcx
    o64 sysret
