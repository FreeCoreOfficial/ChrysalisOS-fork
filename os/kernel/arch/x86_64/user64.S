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

    ; set FS base (MSR 0xC0000100)
    mov rax, [rdi + 24]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000100
    wrmsr

    ; set GS base (MSR 0xC0000101)
    mov rax, [rdi + 32]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000101
    wrmsr

    ; sysretq will:
    ;   rip <- rcx
    ;   rflags <- r11
    ;   cs <- STAR[63:48] + 16, ss <- STAR[63:48] + 8
    sysretq
