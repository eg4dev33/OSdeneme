ENTRY(start)

SECTIONS
{
    . = 1M;

    .boot : { KEEP(*(.multiboot_header)) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(COMMON) *(.bss .bss.*) }

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}
