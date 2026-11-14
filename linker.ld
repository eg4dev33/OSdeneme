ENTRY(start)

SECTIONS
{
    . = 1M;

    .boot : { KEEP(*(.multiboot_header)) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(COMMON) *(.bss .bss.*) }

    . = ALIGN(4096);
    __kernel_end = .;

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}
