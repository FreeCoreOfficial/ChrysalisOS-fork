; kernel/arch/i386/syscall.S
BITS 32
global syscall_handler
extern syscall_dispatch

section .text
syscall_handler:
    ; ABI on entry from userspace/kernel task:
    ;   eax=num, ebx=a1, ecx=a2, edx=a3, esi=a4, edi=a5, ebp=a6
    ; Keep full 32-bit state so stack stays compatible with iretd frame.
    pushad

    ; push syscall_dispatch(num, a1, a2, a3, a4, a5, a6)
    ; pushad layout at [esp]:
    ;   0: edi, 4: esi, 8: ebp, 12: old esp, 16: ebx, 20: edx, 24: ecx, 28: eax
    mov eax, [esp + 28]    ; num
    mov ebx, [esp + 16]    ; a1
    mov ecx, [esp + 24]    ; a2
    mov edx, [esp + 20]    ; a3
    mov esi, [esp + 4]     ; a4
    mov edi, [esp + 0]     ; a5
    mov ebp, [esp + 8]     ; a6

    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax

    call syscall_dispatch
    add esp, 28

    ; Return value must be seen as EAX after popad.
    mov [esp + 28], eax

    popad
    iretd
