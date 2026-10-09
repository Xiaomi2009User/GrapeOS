/* USB-HDD BIOS boot sector. EDD reads use absolute disk LBAs, so neither
 * USB emulation nor hard-disk geometry has to match the floppy image.
 * Keep the FAT16 BPB area clear: this code also works as the partition VBR.
 */
.section .boot, "ax"
.code16
.global boot_start
boot_start:
    jmp normalize
    nop
    .org 62
normalize:
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
    pushw %ds
    pushw %es
    movw $0x0003, %ax
    int $0x10
    popw %es
    popw %ds

    movw $0x55aa, %bx
    movb $0x41, %ah
    movb boot_drive, %dl
    pushw %ds
    int $0x13
    popw %ds
    jc no_edd
    cmpw $0xaa55, %bx
    jne no_edd
    testw $1, %cx
    jz no_edd

    movw $3, %di
retry_read:
    /* BIOS may change the count in the packet, including on failure. */
    movw $8, disk_packet + 2
    pushaw
    pushw %ds
    pushw %es
    movw $disk_packet, %si
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    popw %es
    popw %ds
    popaw
    jnc enter_grapeload
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
    jnz retry_read
    movw $disk_error, %si
    jmp print_error
no_edd:
    movw $edd_error, %si
print_error:
    cld
    lodsb
    testb %al, %al
    jz halt_boot
    movb $0x0e, %ah
    movw $7, %bx
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
enter_grapeload:
    movb boot_drive, %dl
    ljmp $0x0000, $0x8000

.balign 16
disk_packet:
    .byte 16, 0
    .word 8, 0, 0x0800
    .quad 1
boot_drive: .byte 0
edd_error: .asciz "GrapeLoad needs BIOS EDD / USB-HDD mode\r\n"
disk_error: .asciz "GrapeLoad USB read failed\r\n"

/* The builder fills the MBR partition table; code stays below it. */
.org 446
.space 64
.word 0xaa55
.section .note.GNU-stack, "", @progbits
