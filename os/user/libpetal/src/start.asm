[BITS 32]
global _start
extern main
extern p_exit

section .text._start
_start:
    ; Terminate stack frame
    xor ebp, ebp
    
    ; Stack alignment to 16 bytes (ABI requirement)
    ; We push 2 arguments (argc, argv) = 8 bytes.
    ; So ESP should be 8 mod 16 before push.
    ; Or simpler: and esp, -16 -> push dummy -> push args -> call.
    
    and esp, 0xFFFFFFF0
    
    ; Prepare arguments for main(int argc, char **argv)
    ; distinct from stack alignment padding
    push 0 ; padding/NULL?
    push 0 ; padding/NULL?
    
    ; Actually, simpler:
    ; push argv (NULL)
    ; push argc (0)
    push 0
    push 0
    
    call main
    
    ; Access return value
    push eax
    call p_exit
    
    hlt
