[BITS 32]
global tss_flush

tss_flush:
    mov ax, 0x30      ; Index 6 in GDT (6 * 8 = 48 = 0x30)
    ltr ax            ; Load Task Register
    ret