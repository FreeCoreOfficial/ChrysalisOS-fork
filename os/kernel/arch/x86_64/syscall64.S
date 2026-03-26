BITS 64
global syscall64_entry
extern __syscall_handler

section .text

; -----------------------------------------------------------------------------
; SYSCALL Entry Point
; -----------------------------------------------------------------------------
; SYSCALL does NOT automatically switch the stack pointer (unlike an interrupt
; gate for an exception/IRQ). It only:
;   1. Saves RIP to RCX
;   2. Saves RFLAGS to R11
;   3. Loads CS from STAR[47:32] (kernel code, 0x08)
;   4. Loads SS from STAR[47:32]+8 (kernel data, 0x10)
;   5. Masks RFLAGS according to FMASK (clears IF, so interrupts are disabled)
;   6. Jumps to LSTAR (this function)
;
; Because RSP is NOT switched by the CPU, it still points to the user stack.
; We MUST manually save the user stack pointer and switch to a valid kernel
; stack BEFORE enabling interrupts or calling any C code.
;
; If we use RSP=0 or an invalid RSP, any push or memory access will cause a
; Double Fault (#DF, VEC=8).
; -----------------------------------------------------------------------------
syscall64_entry:
    ; Swap GS to access kernel task data (GS.Base contains &task->gs)
    swapgs

    ; Save user stack pointer to task state
    mov [gs:8], rsp

    ; Switch to kernel stack for this task
    mov rsp, [gs:0]

    ; Safety: if kernel stack is null, panic.
    test rsp, rsp
    jz syscall64_panic_no_stack

    ; Align stack for ABI before calling C (ensure 16-byte alignment).
    ; We are entering with an aligned stack top, but after pushing registers
    ; we need to be aligned.
    sub rsp, 8

    ; Save registers into syscall64_state_t layout:
    ;  ds, es, r15..r8, rbp, rdi, rsi, rdx, rcx, rbx, rax
    ; Note: RCX contains user RIP. R11 contains user RFLAGS.
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

    ; Set kernel data segments
    cld
    mov eax, 0x10
    mov ds, eax
    mov es, eax

    ; Call the C handler.
    ; RDI receives a pointer to the syscall64_state_t struct on the stack.
    xor rbp, rbp
    mov rdi, rsp
    call __syscall_handler

    ; Sanity check: RCX must be canonical before SYSRET
    mov rax, [rsp + 112]
    sar rax, 47
    cmp rax, 0
    je .rcx_ok
    cmp rax, -1
    je .rcx_ok
    jmp syscall64_panic_no_stack
.rcx_ok:
    
global syscall64_exit
syscall64_exit:
    ; Restore segments and registers in strictly reverse order.
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

    add rsp, 8

    ; Restore user stack pointer
    mov rsp, [gs:8]

    ; Swap GS back to user level
    swapgs

    ; Return to user mode.
    ; SYSRET will:
    ;  1. Load user CS from STAR[63:48]+16
    ;  2. Load user SS from STAR[63:48]+8
    ;  3. Restore RIP from RCX
    ;  4. Restore RFLAGS from R11
    o64 sysret

syscall64_panic_no_stack:
    ; We arrived at syscall entry with a null kernel stack pointer.
    ; Halt to avoid a double/triple fault looping.
    cli
.hlt_loop:
    hlt
    jmp .hlt_loop


