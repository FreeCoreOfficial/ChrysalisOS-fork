BITS 64
global user64_enter

section .text
user64_enter:
    ; rdi = pointer to user64_context
    mov rax, [rdi + 0]   ; rip
    mov rcx, rax
    mov rax, [rdi + 8]   ; rsp
    mov r11, [rdi + 16]  ; rflags

    mov rsp, rax

    ; sysretq will:
    ;   rip <- rcx
    ;   rflags <- r11
    ;   cs <- STAR[63:48] + 16, ss <- STAR[63:48] + 8
    sysretq
