BITS 64
global user64_enter

section .text
user64_enter:
    ; rdi=rip, rsi=rsp, rdx=rflags, rcx=fs_base, r8=gs_base
    
    ; Preserve critical values in registers not touched by wrmsr/rdmsr
    mov r9, rdi   ; rip
    mov r10, rsi  ; rsp
    mov r12, rdx  ; rflags (r11 will be used for sysret)
    
    ; --- Write FS.Base (MSR 0xC0000100) ---
    mov rax, rcx  ; fs_base
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000100
    wrmsr

    ; --- Sync GS Bases ---
    mov ecx, 0xC0000101
    rdmsr
    mov ecx, 0xC0000102
    wrmsr
    ; Write user GS base (r8)
    mov rax, r8
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000101
    wrmsr

    ; --- Load user data segments ---
    mov ax, 0x1B
    mov ds, ax
    mov es, ax

    cli
    ; --- Prepare SYSRET Registers ---
    mov rcx, r9     ; User RIP
    mov r11, r12    ; User RFLAGS
    mov rsp, r10    ; User Stack Pointer

    o64 sysret
