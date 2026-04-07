BITS 64
global user64_enter

section .text
user64_enter:
    ; rdi=rip, rsi=rsp, rdx=rflags, rcx=fs_base, r8=gs_base
    
    ; Preserve critical values in registers not touched by wrmsr/rdmsr
    mov r9, rdi   ; rip
    mov r10, rsi  ; rsp
    mov r15, rdx  ; rflags (r11 will be used for sysret)
    mov r12, rcx  ; fs_base
    mov r13, r8   ; gs_base
    
    ; --- Write FS.Base (MSR 0xC0000100) ---
    mov rax, r12  ; fs_base
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
    mov rax, r13
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000101
    wrmsr

    ; --- Load user data segments ---
    mov ax, 0x1B
    mov ds, ax
    mov es, ax

    cli
    ; New process entry should not inherit stale kernel GPR values.
    xor eax, eax
    xor ebx, ebx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    ; --- Prepare SYSRET Registers ---
    mov rcx, r9     ; User RIP
    mov r11, r15    ; User RFLAGS
    mov rsp, r10    ; User Stack Pointer

    o64 sysret
