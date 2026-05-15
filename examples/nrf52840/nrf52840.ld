OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx) : ORIGIN = 0x0,          LENGTH = 0x100000
    RAM (rwx) :  ORIGIN = 0x20000000,   LENGTH = 0x40000
}

SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))
        *(.text*)

        KEEP(*(.init))
        KEEP(*(.fini))

        /* .ctors */
        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)

        /* .dtors */
        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)

        *(.rodata*)

        KEEP(*(.eh_frame*))
    } > FLASH

    .mem_section_dummy_rom :
    {
    }

    .log_const_data :
    {
        PROVIDE(__start_log_const_data = .);
        KEEP(*(SORT(.log_const_data*)))
        PROVIDE(__stop_log_const_data = .);
    } > FLASH

    .log_backends :
    {
        PROVIDE(__start_log_backends = .);
        KEEP(*(SORT(.log_backends*)))
        PROVIDE(__stop_log_backends = .);
    } > FLASH

    .nrf_balloc :
    {
        PROVIDE(__start_nrf_balloc = .);
        KEEP(*(.nrf_balloc))
        PROVIDE(__stop_nrf_balloc = .);
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH

    __exidx_end = .;

    . = ALIGN(4);
    __etext = .;

    .data : AT (__etext)
    {
        __data_start__ = .;
        *(vtable)
        *(.data*)

        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(4);
        /* init data */
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);


        . = ALIGN(4);
        /* finit data */
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);

        KEEP(*(.jcr*))
        . = ALIGN(4);
        /* All data end */
        __data_end__ = .;

    } > RAM

    . = ALIGN(4);
    .mem_section_dummy_ram :
    {
    }
    .log_dynamic_data :
    {
        PROVIDE(__start_log_dynamic_data = .);
        KEEP(*(SORT(.log_dynamic_data*)))
        PROVIDE(__stop_log_dynamic_data = .);
    } > RAM
    .log_filter_data :
    {
        PROVIDE(__start_log_filter_data = .);
        KEEP(*(SORT(.log_filter_data*)))
        PROVIDE(__stop_log_filter_data = .);
    } > RAM

    .bss :
    {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /* XXX check if needed */
    .heap (COPY):
    {
        __HeapBase = .;
        __end__ = .;
        PROVIDE(end = .);
        KEEP(*(.heap*))
        __HeapLimit = .;
    } > RAM

    /* .stack_dummy section doesn't contains any symbols. It is only
     * used for linker to calculate size of stack sections, and assign
     * values to stack symbols later */
    .stack_dummy (COPY):
    {
        KEEP(*(.stack*))
    } > RAM

    /* Set stack top to end of RAM, and stack limit move down by
     * size of stack_dummy section */
    __StackTop = ORIGIN(RAM) + LENGTH(RAM);
    __StackLimit = __StackTop - SIZEOF(.stack_dummy);
    PROVIDE(__stack = __StackTop);

    /* XXX Check if data + heap + stack exceeds RAM limit */
    ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed with stack")
}
