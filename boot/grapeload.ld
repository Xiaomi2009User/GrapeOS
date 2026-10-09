OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(grapeload_start)
SECTIONS
{
    . = 0x8000;
    .stage2 : { KEEP(*(.stage2)) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
ASSERT(grapeload_start == 0x8000, "GrapeLoad must start at physical 0x8000")
ASSERT(SIZEOF(.stage2) == 4096, "GrapeLoad must occupy exactly eight floppy sectors")
ASSERT(ADDR(.stage2) + SIZEOF(.stage2) <= 0x10000, "GrapeLoad overlaps the kernel")
