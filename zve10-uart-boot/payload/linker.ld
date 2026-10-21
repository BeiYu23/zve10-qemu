ENTRY(_start)
MEMORY { ESRAM (rwx) : ORIGIN = 0xFE021000, LENGTH = 64K }
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text .text.*) } > ESRAM
    .rodata : ALIGN(8) { *(.rodata .rodata.*) } > ESRAM
    .data : ALIGN(8) { *(.data .data.*) } > ESRAM
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > ESRAM
    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) *(.ARM.attributes) }
}
