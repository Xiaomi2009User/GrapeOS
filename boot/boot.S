/* First BIOS stage: load eight sectors of GrapeLoad at physical 0x8000.
 * GrapeLoad waits for the boot choice before reading any kernel sectors.
 */

.section .boot, "ax"
.code16
.global boot_start
boot_start:
    ljmp $0x0000, $real_start
real_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    cld
    movb %dl, boot_drive
    sti

    /* A known VGA text mode also makes BIOS error messages readable. */
    pushw %ds
    pushw %es
    movw $0x0003, %ax
    int $0x10
    popw %es
    popw %ds
    movw $0x0800, %ax
    movw %ax, %es
    xorw %bx, %bx
    movw $8, %si

next_sector:
    movw $3, %di
retry_sector:
    /* Preserve the loading state across firmware calls, including errors. */
    pushaw
    pushw %ds
    pushw %es
    movw $0x0201, %ax
    movb disk_cylinder, %ch
    movb disk_sector, %cl
    movb disk_head, %dh
    movb boot_drive, %dl
    int $0x13
    popw %es
    popw %ds
    popaw
    jnc sector_loaded

    pushaw
    pushw %ds
    pushw %es
    xorw %ax, %ax
    movb boot_drive, %dl
    int $0x13
    popw %es
    popw %ds
    popaw
    decw %di
    jnz retry_sector
    movw $disk_error, %si
    cld
print_error:
    lodsb
    testb %al, %al
    jz halt_boot
    movb $0x0e, %ah
    movw $0x0007, %bx
    pushaw
    pushw %ds
    int $0x10
    popw %ds
    popaw
    jmp print_error
halt_boot:
    cli
    hlt
    jmp halt_boot

sector_loaded:
    addw $512, %bx
    decw %si
    jz enter_grapeload
    incb disk_sector
    cmpb $19, disk_sector
    jb next_sector
    movb $1, disk_sector
    xorb $1, disk_head
    jnz next_sector
    incb disk_cylinder
    jmp next_sector

enter_grapeload:
    movb boot_drive, %dl
    ljmp $0x0000, $0x8000

boot_drive:   .byte 0
disk_cylinder: .byte 0
disk_head:    .byte 0
disk_sector:  .byte 2
disk_error:   .asciz "GrapeLoad disk read failed\r\n"

.org 510
.word 0xaa55

.section .note.GNU-stack, "", @progbits
