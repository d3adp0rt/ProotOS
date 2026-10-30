OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
SECTIONS
{
    . = 0x8000;
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
