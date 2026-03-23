BITS 64
global user64_enter

section .text
user64_enter:
    ; rdi = pointer to user64_context
    ; Layout: [rdi+0]=rip, [rdi+8]=rsp, [rdi+16]=rflags,
    ;         [rdi+24]=fs_base, [rdi+32]=gs_base
    
    ; --- Write FS.Base (MSR 0xC0000100) ---
    mov rax, [rdi + 24]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000100
    wrmsr

    ; --- Sync GS Bases ---
    ; At this point, MSR_GS_BASE (0xC0000101) contains &task->gs (from scheduler).
    ; We need to move it to MSR_KERNEL_GS_BASE (0xC0000102) and put user GS
    ; into MSR_GS_BASE.

    ; 1. Read current kernel GS pointer from active GS Base
    mov ecx, 0xC0000101
    rdmsr
    ; 2. Write it into the hidden KERNEL_GS_BASE
    mov ecx, 0xC0000102
    wrmsr
    ; 3. Overwrite active GS Base with user pointer
    mov rax, [rdi + 32]   ; user gs_base from context
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000101
    wrmsr

    ; --- Load user data segments ---
    ; (SYSRET sets CS and SS, but DS/ES should be set for user mode)
    mov ax, 0x1B        ; user data selector (ring 3, RPL 3)
    mov ds, ax
    mov es, ax

    ; Disable interrupts fully before context switch
    cli

    ; --- Prepare SYSRET Registers ---
    mov rcx, [rdi + 0]   ; RCX = User RIP
    mov r11, [rdi + 16]  ; R11 = User RFLAGS
    mov rsp, [rdi + 8]   ; RSP = User Stack Pointer

    ; Return to user mode via SYSRET 
    ; It reads CS from STAR[63:48]+16 (0x23)
    ; It reads SS from STAR[63:48]+8  (0x1B)
    o64 sysret
