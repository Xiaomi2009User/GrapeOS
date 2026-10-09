OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(boot_start)
SECTIONS
{
    . = 0x7c00;
    .boot : { KEEP(*(.boot)) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
ASSERT(SIZEOF(.boot) == 512, "BIOS boot sector must be exactly 512 bytes")
