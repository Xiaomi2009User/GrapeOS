/* Second BIOS stage: menu, kernel disk loading, and protected-mode entry.
 * LBA 0 is boot.S or usb.S; LBAs 1..8 contain this fixed 4096-byte stage.
 * The kernel therefore starts at LBA 9 (cylinder 0, head 0, sector 10).
 */
#include "bootinfo.h"
#ifndef KERNEL_SECTORS
#error KERNEL_SECTORS must be provided by the build
#endif
#if KERNEL_SECTORS < 1 || KERNEL_SECTORS > 384
#error The kernel must fit in the 192 KiB loading window
#endif

.section .stage2, "ax"
.code16
.global grapeload_start
grapeload_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    cld
    movb %dl, boot_drive
    sti
    call serial_init

    movw $title, %si
    movw $0x031e, %dx
    movb $0x0d, %bl
    call menu_line
    movw $border, %si
    movw $0x0510, %dx
    call menu_line
    movw $heading, %si
    movw $0x0610, %dx
    movb $0x07, %bl
    call menu_line
    movw $empty_line, %si
    movw $0x0710, %dx
    call menu_line
    movw $choice, %si
    movw $0x0810, %dx
    movb $0x5f, %bl
    call menu_line
    movw $empty_line, %si
    movw $0x0910, %dx
    movb $0x07, %bl
    call menu_line
    movw $border, %si
    movw $0x0a10, %dx
    movb $0x0d, %bl
    call menu_line
    movw $hint, %si
    movw $0x0c17, %dx
    movb $0x07, %bl
    call menu_line

wait_for_choice:
    /* BIOS handles physical PS/2 keyboard scan codes in real mode. */
    movb $0x01, %ah
    int $0x16
    jz check_serial
    xorw %ax, %ax
    int $0x16
    cmpb $13, %al
    je selected
check_serial:
    cmpb $0, serial_present
    je wait_for_choice
    movw $0x3fd, %dx
    inb %dx, %al
    testb $1, %al
    jz wait_for_choice
    movw $0x3f8, %dx
    inb %dx, %al
    cmpb $13, %al
    je selected
    cmpb $10, %al
    je selected
    jmp wait_for_choice

selected:
    movw $selected_message, %si
    movw $0x0f10, %dx
    movb $0x0a, %bl
    call menu_line
    movw $loading_message, %si
    movw $0x1010, %dx
    movb $0x07, %bl
    call menu_line
    /* Discard any second CR/LF byte before the kernel receives input. */
    movw $0x3fa, %dx
    movb $0xc7, %al
    outb %al, %dx
    movw $0x1000, %ax
    movw %ax, %es
    xorw %bx, %bx
    movw $KERNEL_SECTORS, %si

next_sector:
    movw $3, %di
retry_sector:
    pushaw
    pushw %ds
    pushw %es
#ifdef USB_BOOT
    movw $1, disk_packet + 2
    movw %bx, disk_packet + 4
    movw %es, disk_packet + 6
    movw $disk_packet, %si
    movb $0x42, %ah
    movb boot_drive, %dl
#else
    movw $0x0201, %ax
    movb disk_cylinder, %ch
    movb disk_sector, %cl
    movb disk_head, %dh
    movb boot_drive, %dl
#endif
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
    xorw %ax, %ax
    movw %ax, %es
    movw $disk_error, %si
    movw $0x1110, %dx
    movb $0x0c, %bl
    call menu_line
halt_boot:
    cli
    hlt
    jmp halt_boot

sector_loaded:
    addw $512, %bx
    jnc segment_ready
    movw %es, %ax
    addw $0x1000, %ax
    movw %ax, %es
segment_ready:
    decw %si
    jz kernel_loaded
#ifdef USB_BOOT
    incl disk_packet + 8
#else
    incb disk_sector
    cmpb $19, disk_sector
    jb next_sector
    movb $1, disk_sector
    xorb $1, disk_head
    jnz next_sector
    incb disk_cylinder
#endif
    jmp next_sector

kernel_loaded:
    movl $BOOT_INFO_MAGIC, BOOT_INFO_ADDRESS
#ifdef USB_BOOT
    movl $BOOT_INFO_LIVE_USB, BOOT_INFO_ADDRESS + 4
#else
    movl $0, BOOT_INFO_ADDRESS + 4
#endif
    xorw %ax, %ax
    movw %ax, %es
    movw $loaded_message, %si
    movw $0x1110, %dx
    movb $0x0a, %bl
    call menu_line
    cli
    /* Fast A20 gate: enable address bit 20 without asserting reset. */
    inb $0x92, %al
    orb $0x02, %al
    andb $0xfe, %al
    outb %al, $0x92
    lgdt gdt_pointer
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x08, $protected_start

.code32
protected_start:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x90000, %esp
    xorl %ebp, %ebp
    ljmp $0x08, $0x10000

.code16
/* SI = zero-terminated text, DH/DL = row/column, BL = VGA attribute. */
menu_line:
    call vga_line
    call serial_line
    ret

vga_line:
    pushaw
    pushw %ds
    pushw %es
    xorw %ax, %ax
    movw %ax, %es
    movw %si, %bp
    xorw %cx, %cx
1:
    cmpb $0, (%si)
    je 2f
    incw %si
    incw %cx
    jmp 1b
2:
    movw $0x1301, %ax
    xorb %bh, %bh
    int $0x10
    popw %es
    popw %ds
    popaw
    ret

serial_init:
    pushaw
    movw $0x3f9, %dx
    xorb %al, %al
    outb %al, %dx
    movw $0x3fb, %dx
    movb $0x80, %al
    outb %al, %dx
    movw $0x3f8, %dx
    movb $1, %al
    outb %al, %dx
    movw $0x3f9, %dx
    xorb %al, %al
    outb %al, %dx
    movw $0x3fb, %dx
    movb $3, %al
    outb %al, %dx
    movw $0x3fa, %dx
    movb $0xc7, %al
    outb %al, %dx
    movw $0x3fc, %dx
    movb $0x0b, %al
    outb %al, %dx
    movw $0x3fd, %dx
    inb %dx, %al
    cmpb $0xff, %al
    je 1f
    movb $1, serial_present
1:
    popaw
    ret

serial_line:
    pushaw
    cld
1:
    lodsb
    testb %al, %al
    jz 2f
    call serial_putc
    jmp 1b
2:
    movb $13, %al
    call serial_putc
    movb $10, %al
    call serial_putc
    popaw
    ret

serial_putc:
    pushaw
    cmpb $0, serial_present
    je 3f
    movb %al, %bl
    movw $0x3fd, %dx
    movw $0xffff, %cx
1:
    inb %dx, %al
    testb $0x20, %al
    jnz 2f
    loop 1b
    jmp 3f
2:
    movw $0x3f8, %dx
    movb %bl, %al
    outb %al, %dx
3:
    popaw
    ret

.balign 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt_end:
gdt_pointer:
    .word gdt_end - gdt - 1
    .long gdt

boot_drive:      .byte 0
serial_present:  .byte 0
disk_cylinder:   .byte 0
disk_head:       .byte 0
disk_sector:     .byte 10
#ifdef USB_BOOT
.balign 16
disk_packet:
    .byte 16, 0
    .word 1, 0, 0x1000
    .quad 9
#endif
title:            .asciz "GrapeLoad"
border:           .asciz "+----------------------------------------------+"
heading:          .asciz "|              Choose your system              |"
empty_line:       .asciz "|                                              |"
choice:           .asciz "|  > GrapeOS                                   |"
hint:             .asciz "Press Enter to boot GrapeOS"
selected_message: .asciz "[ OK ] GrapeOS selected"
loading_message:  .asciz "[ .. ] Loading GrapeOS kernel..."
loaded_message:   .asciz "[ OK ] Kernel image loaded"
disk_error:       .asciz "[FAIL] Kernel disk read failed"

/* The fixed stage size defines the kernel's first floppy sector. */
.org 4096
.section .note.GNU-stack, "", @progbits
