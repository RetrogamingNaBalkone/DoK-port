ENTRY(_rom_entry)
__iwram_reserved_bytes = DEFINED(__iwram_reserved_override) ? __iwram_reserved_override : 1024;
MEMORY {
    ROM (rx) : ORIGIN = 0x08000000, LENGTH = 32M
    EWRAM (rw) : ORIGIN = 0x02000000, LENGTH = 256K
    /* Default G01/G02 reserve is 1 KiB; larger G03 views request 8 KiB. */
    IWRAM (rwx) : ORIGIN = 0x03000000, LENGTH = 32K - __iwram_reserved_bytes
}
SECTIONS {
    .header : { KEEP(*(.header)) } > ROM
    .text : ALIGN(4) {
        *(.text.startup) *(.text .text.*)
        KEEP(*(.rodata.memory_probe))
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > ROM
    .ARM.exidx : { *(.ARM.exidx*) } > ROM
    .data : ALIGN(4) {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > EWRAM AT> ROM
    __data_load = LOADADDR(.data);
    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        KEEP(*(.bss.diag))
        KEEP(*(.bss.runtime_probe))
        KEEP(*(.bss.future_reserves))
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > EWRAM
    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.attributes) }
}
ASSERT(SIZEOF(.header) == 192, "GBA header must be 192 bytes")
ASSERT(__bss_end <= ORIGIN(EWRAM) + LENGTH(EWRAM), "EWRAM overflow")
