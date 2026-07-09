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

    .bss : {
        *(COMMON)
        *(.bss*)
    }
}