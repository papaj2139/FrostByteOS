ENTRY(start)

PHDRS
{
    text   PT_LOAD FLAGS(4 | 1);     /* PF_R | PF_X */
    rodata PT_LOAD FLAGS(4);         /* PF_R       */
    data   PT_LOAD FLAGS(4 | 2);     /* PF_R | PF_W */
}

SECTIONS
{
    . = 1M;

    /* export start of kernel image */
    kernel_start = .;

    /* multiboot header must be in the first loadable segment */
    .multiboot :
    {
        *(.multiboot)
    } :text

    . = ALIGN(4K);
    .text :
    {
        *(.text*)
    } :text

    . = ALIGN(4K);
    .rodata :
    {
        *(.rodata*)
    } :rodata

    . = ALIGN(4K);
    .data :
    {
        *(.data*)
    } :data

    .bss :
    {
        *(COMMON)
        *(.bss*)
    } :data

    /* export end of kernel image */
    kernel_end = .;
}
