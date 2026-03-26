BITS 64
extern isr64_handler

section .text

%macro ISR_NOERR 1
global isr64_stub%1
isr64_stub%1:
    push 0
    push %1
    jmp isr64_common
%endmacro

%macro ISR_ERR 1
global isr64_stub%1
isr64_stub%1:
    push %1
    jmp isr64_common
%endmacro

isr64_common:
    cli
    ; Save all registers
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    push rbx
    push rbp

    mov rdi, rsp
    extern isr64_handler
    call isr64_handler

    ; Restore registers (if handler returns)
    pop rbp
    pop rbx
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    
    add rsp, 16 ; Remove vec and err
    iretq

; Exceptions 0-31
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31
