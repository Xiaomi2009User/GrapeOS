OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x10000;
    __kernel_start = .;
    .text :
    {
        KEEP(*(.text.start))
        *(.text .text.*)
    } :text
    .rodata ALIGN(16) : { *(.rodata .rodata.*) } :text
    .data ALIGN(16) : { *(.data .data.*) } :data
    .bss ALIGN(16) (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data
    __kernel_end = .;
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
ASSERT(_start == 0x10000, "Kernel entry must be at the BIOS loading address")
ASSERT(__kernel_end <= 0x80000, "Kernel data overlaps the reserved stack area")
