BITS 64
global isr64_stub
extern isr64_handler

section .text
isr64_stub:
    cli
    mov rdi, rsp
    call isr64_handler
.halt:
    hlt
    jmp .halt
