ENTRY(_start)

SECTIONS
{
    /* Mapped address for apps */
    . = 0x08048000;

    .text : ALIGN(4096)
    {
        *(.text._start)
        *(.text*)
        *(.gnu.linkonce.t*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata*)
        *(.gnu.linkonce.r*)
    }

    .data : ALIGN(4096)
    {
        *(.data*)
        *(.gnu.linkonce.d*)
    }

    .bss : ALIGN(4096)
    {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.gnu.build-id)
    }
}
