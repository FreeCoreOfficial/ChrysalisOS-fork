BITS 64
global _start

section .text
_start:
    mov rax, 1          ; SYS_write (Linux ABI)
    mov rdi, 1          ; fd=stdout
    lea rsi, [rel msg]
    mov rdx, msg_len
    syscall

    mov rax, 60         ; SYS_exit
    xor rdi, rdi
    syscall

section .rodata
msg: db "hello64: usermode ok", 10
msg_len: equ $ - msg
