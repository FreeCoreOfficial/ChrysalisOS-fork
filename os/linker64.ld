OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start64)

SECTIONS
{
    . = 1M;

    .text :
    {
        KEEP(*(.multiboot))
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data : { *(.data*) }

    .bss (NOLOAD) :
    {
        *(.bss*)
        *(COMMON)
    }

    kernel64_end = .;

    /DISCARD/ : { *(.eh_frame) }
}
