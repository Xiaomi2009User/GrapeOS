.section .text
.code32
.extern interrupt_dispatch

.macro ISR_NO_ERROR number
.global isr_\number
.type isr_\number, @function
isr_\number:
    pushl $0
    pushl $\number
    jmp isr_common
.endm

.macro ISR_ERROR number
.global isr_\number
.type isr_\number, @function
isr_\number:
    pushl $\number
    jmp isr_common
.endm

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR 8
ISR_NO_ERROR 9
ISR_ERROR 10
ISR_ERROR 11
ISR_ERROR 12
ISR_ERROR 13
ISR_ERROR 14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR 17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_ERROR 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_ERROR 29
ISR_ERROR 30
ISR_NO_ERROR 31
ISR_NO_ERROR 32
ISR_NO_ERROR 33
ISR_NO_ERROR 34
ISR_NO_ERROR 35
ISR_NO_ERROR 36
ISR_NO_ERROR 37
ISR_NO_ERROR 38
ISR_NO_ERROR 39
ISR_NO_ERROR 40
ISR_NO_ERROR 41
ISR_NO_ERROR 42
ISR_NO_ERROR 43
ISR_NO_ERROR 44
ISR_NO_ERROR 45
ISR_NO_ERROR 46
ISR_NO_ERROR 47

.global isr_unhandled
.type isr_unhandled, @function
isr_unhandled:
    pushl $0
    pushl $255
    jmp isr_common

.type isr_common, @function
isr_common:
    cld
    pushal
    xorl %eax, %eax
    movw %ds, %ax
    pushl %eax
    movw %es, %ax
    pushl %eax
    movw %fs, %ax
    pushl %eax
    movw %gs, %ax
    pushl %eax
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    /* EBX is callee-saved; preserve the frame across an aligned C call. */
    movl %esp, %ebx
    andl $-16, %esp
    subl $12, %esp
    pushl %ebx
    call interrupt_dispatch
    movl %ebx, %esp
    popl %eax
    movw %ax, %gs
    popl %eax
    movw %ax, %fs
    popl %eax
    movw %ax, %es
    popl %eax
    movw %ax, %ds
    popal
    addl $8, %esp
    iret

.section .rodata
.align 4
.global isr_stub_table
.type isr_stub_table, @object
isr_stub_table:
.long isr_0, isr_1, isr_2, isr_3, isr_4, isr_5, isr_6, isr_7
.long isr_8, isr_9, isr_10, isr_11, isr_12, isr_13, isr_14, isr_15
.long isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23
.long isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31
.long isr_32, isr_33, isr_34, isr_35, isr_36, isr_37, isr_38, isr_39
.long isr_40, isr_41, isr_42, isr_43, isr_44, isr_45, isr_46, isr_47
.size isr_stub_table, .-isr_stub_table

.section .note.GNU-stack,"",@progbits
