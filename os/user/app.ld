ENTRY(_start)

SECTIONS
{
    /* Mapped address for apps */
    . = 0x08048000;

    .text : ALIGN(4K)
    {
        /* Force _start to the very beginning of the usable code */
        KEEP(*(.text._start))
        *(.text*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }

    .data : ALIGN(4K)
    {
        *(.data*)
    }

    .bss : ALIGN(4K)
    {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
    }
}
