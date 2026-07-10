ENTRY(kmain)

SECTIONS {
    . = 0xffffffff80000000;

    .text : {
        *(.limine_requests_start)
        *(.limine_requests)
        *(.limine_requests_end)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .init_array : ALIGN(8) {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))
        __init_array_end = .;
    }

    .bss : {
        *(COMMON)
        *(.bss*)
    }
}
