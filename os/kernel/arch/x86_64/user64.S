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

    ; --- Write GS.Base (MSR 0xC0000101) ---
    mov rax, [rdi + 32]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000101
    wrmsr

    ; --- Load user data segments ---
    ; (SYSRET sets CS and SS, but DS/ES should be set to safety)
    mov ax, 0x1B        ; user data selector (ring 3)
    mov ds, ax
    mov es, ax

    ; Disable interrupts fully before context switch
    cli

    ; --- Prepare SYSRET Registers ---
    mov rcx, [rdi + 0]   ; RCX = User RIP
    mov r11, [rdi + 16]  ; R11 = User RFLAGS
    mov rsp, [rdi + 8]   ; RSP = User Stack Pointer

    ; Swap GS to put the user's GS base into effect
    swapgs

    ; Return to user mode via SYSRET 
    ; It reads CS from STAR[63:48]+16 (0x23)
    ; It reads SS from STAR[63:48]+8  (0x1B)
    o64 sysret
