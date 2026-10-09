/* GRUB's x86_64 EFI loader exits firmware services and enters this i386
 * Multiboot2 entry. Install our own GDT and stack before using C or IRQs.
 */
.section .multiboot, "a"
.balign 8
header:
    .long 0xe85250d6, 0, header_end - header
    .long -(0xe85250d6 + (header_end - header))
    /* Require an RGB framebuffer; the loader selects a supported mode. */
    .word 5, 0
    .long 20, 0, 0, 32
    .balign 8
    .word 0, 0
    .long 8
header_end:

.section .text.start, "ax"
.code32
.global multiboot_start
.extern __bss_start, __bss_end, boot_multiboot_init, kernel_main
multiboot_start:
    cli
    cld
    movl %eax, %esi
    movl %ebx, %ebp
    lgdt gdt_pointer
    ljmp $0x08, $own_segments
own_segments:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $stack_top, %esp
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb
    pushl %ebp
    pushl %esi
    call boot_multiboot_init
    addl $8, %esp
    testb %al, %al
    jz stopped
    call kernel_main
stopped:
    cli
    hlt
    jmp stopped

.section .rodata
.balign 8
gdt:
    .quad 0, 0x00cf9a000000ffff, 0x00cf92000000ffff
gdt_pointer:
    .word 23
    .long gdt
.section .bss
.balign 16
    .skip 32768
stack_top:
.section .note.GNU-stack, "", @progbits
