OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(multiboot_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS
{
    . = 0x100000;
    __kernel_start = .;
    .multiboot : { KEEP(*(.multiboot)) } :text
    .text ALIGN(16) : { KEEP(*(.text.start)) *(.text .text.*) } :text
    .rodata ALIGN(16) : { *(.rodata .rodata.*) } :text
    .data ALIGN(4096) : { *(.data .data.*) } :data
    .bss ALIGN(4096) (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        __bss_end = .;
    } :data
    __kernel_end = .;
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
ASSERT(SIZEOF(.multiboot) <= 32768, "Multiboot2 header is too large")
ASSERT(__kernel_end < 0x200000, "UEFI kernel exceeds its reserved low-memory region")
