; 64-bit kernel entry stub for Multiboot2 (starts in 32-bit protected mode)
BITS 32

section .text
global _start64
extern kernel_main64

_start64:
    cli
    cld
    mov esp, stack32_top
    mov ax, ds
    mov es, ax
    mov ss, ax

    mov [mb_magic], eax
    mov [mb_info], ebx

    ; Debug marker A
    mov edi, 0xB8000
    mov ax, 0x1F41            ; 'A'
    mov [edi], ax

    ; Check for long mode support (CPUID.80000001H:EDX[29])
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode

    ; Clear page tables (3 * 4096 bytes)
    xor eax, eax
    mov edi, pml4
    mov ecx, (4096 * 3) / 4
    rep stosd

    ; Debug marker B
    mov edi, 0xB8000
    mov ax, 0x1F42            ; 'B'
    mov [edi + 2], ax

    ; Build identity map for first 1 GiB using 2 MiB pages
    mov eax, pdpt
    or eax, 0x03
    mov [pml4], eax
    mov dword [pml4 + 4], 0

    mov eax, pd
    or eax, 0x03
    mov [pdpt], eax
    mov dword [pdpt + 4], 0

    xor ecx, ecx
.pd_fill:
    mov eax, ecx
    shl eax, 21                 ; 2 MiB * index
    or eax, 0x83                ; PS | RW | P
    mov [pd + ecx*8], eax
    mov dword [pd + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jl .pd_fill

    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Debug marker C
    mov edi, 0xB8000
    mov ax, 0x1F43            ; 'C'
    mov [edi + 4], ax

    ; Load PML4
    mov eax, pml4
    mov cr3, eax

    ; Enable long mode (EFER.LME)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Debug marker D
    mov edi, 0xB8000
    mov ax, 0x1F44            ; 'D'
    mov [edi + 6], ax

    ; Enable paging (CR0.PG)
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; Debug marker E
    mov edi, 0xB8000
    mov ax, 0x1F45            ; 'E'
    mov [edi + 8], ax

    ; Load 64-bit GDT and jump
    lgdt [gdt64_ptr]
    jmp 0x08:long_mode_entry

.no_long_mode:
    mov edi, 0xB8000
    mov eax, 0x4F4E4C00        ; "LON" (visible partial marker)
    mov [edi], eax
.halt_nolm:
    hlt
    jmp .halt_nolm

BITS 64
long_mode_entry:
    mov rdi, 0xB8000
    mov ax, 0x1F46            ; 'F'
    mov [rdi + 10], ax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, stack64_top

    mov rdi, 0xB8000
    mov ax, 0x1F47            ; 'G'
    mov [rdi + 12], ax

    mov edi, [mb_magic]
    mov esi, [mb_info]
    call kernel_main64

.halt:
    hlt
    jmp .halt

section .rodata
align 8
gdt64:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF ; kernel code (0x08)
    dq 0x00AF92000000FFFF ; kernel data (0x10)
    dq 0x00AFF2000000FFFF ; user data   (0x18)
    dq 0x00AFFA000000FFFF ; user code   (0x20)
gdt64_end:
gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dd gdt64

section .bss
alignb 16
stack32_bottom:
    resb 4096
stack32_top:

alignb 16
stack64_bottom:
    resb 16384
stack64_top:

alignb 8
mb_magic:
    resd 1
mb_info:
    resd 1

alignb 4096
global pml4
pml4:
    resq 512
alignb 4096
global pdpt
pdpt:
    resq 512
alignb 4096
global pd
pd:
    resq 512
