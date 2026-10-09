#include "console.h"
#include "gui.h"
#include "io.h"
#include "platform.h"
#include "framebuffer.h"
#include "mouse.h"

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8 zero;
    u8 attributes;
    u16 offset_high;
} __attribute__((packed));

struct idt_pointer {
    u16 limit;
    u32 base;
} __attribute__((packed));

/* This order matches the saves in isr.S, including the two stub words. */
struct interrupt_frame {
    u32 gs, fs, es, ds;
    u32 edi, esi, ebp, saved_esp, ebx, edx, ecx, eax;
    u32 vector, error, eip, cs, eflags;
};

extern void (*isr_stub_table[48])(void);
extern void isr_unhandled(void);
extern void keyboard_init(void);
extern void keyboard_handle_irq(void);
extern void cpu_clock_init(void);
extern u32 cpu_clock_ticks(void);
static bool polling;

static struct idt_entry idt[256];
static volatile u32 ticks;

static void set_gate(u32 vector, void (*handler)(void))
{
    u32 address = (u32)handler;
    idt[vector].offset_low = (u16)address;
    idt[vector].selector = 0x08;
    idt[vector].zero = 0;
    idt[vector].attributes = 0x8e;
    idt[vector].offset_high = (u16)(address >> 16);
}

static void pic_remap(void)
{
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
    outb(0x20, 0x11);
    io_wait();
    outb(0xa0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xa1, 0x28);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xa1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xa1, 0x01);
    io_wait();
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
}

static void pit_init(void)
{
    u16 divisor = 1193182 / TIMER_HZ;
    outb(0x43, 0x36);
    outb(0x40, (u8)divisor);
    outb(0x40, (u8)(divisor >> 8));
}

void platform_init(void)
{
    struct idt_pointer pointer;
    u32 vector;

    __asm__ volatile("cli" : : : "memory");
    ticks = 0;
    for (vector = 0; vector < 256; ++vector)
        set_gate(vector, isr_unhandled);
    for (vector = 0; vector < 48; ++vector)
        set_gate(vector, isr_stub_table[vector]);
    pointer.limit = sizeof(idt) - 1;
    pointer.base = (u32)idt;
    __asm__ volatile("lidt %0" : : "m"(pointer) : "memory");
    pic_remap();
    keyboard_init();
    bool has_mouse = mouse_init();
    polling = fb_active();
    if (polling) {
        cpu_clock_init();
        /* Modern UEFI machines may not route IRQ0/IRQ1 to the legacy PIC.
         * Keep external interrupts masked and poll i8042 with a TSC clock.
         * Exceptions still use the installed IDT. No HLT can trap this mode.
         */
        return;
    }
    pit_init();
    outb(0x21, has_mouse ? 0xf8 : 0xfc);
    outb(0xa1, has_mouse ? 0xef : 0xff);
    __asm__ volatile("sti" : : : "memory");
}

u32 timer_ticks(void)
{
    return polling ? cpu_clock_ticks() : ticks;
}

bool platform_polling(void) { return polling; }
void platform_idle(void)
{
    if (polling) __asm__ volatile("pause");
    else __asm__ volatile("hlt");
}

static void exception_halt(const struct interrupt_frame *frame)
{
    static const char *const names[32] = {
        "divide error", "debug", "NMI", "breakpoint", "overflow",
        "bound range", "invalid opcode", "device unavailable",
        "double fault", "coprocessor overrun", "invalid TSS",
        "segment absent", "stack fault", "general protection", "page fault",
        "reserved", "x87 error", "alignment check", "machine check",
        "SIMD error", "virtualization", "control protection", "reserved",
        "reserved", "reserved", "reserved", "reserved", "reserved",
        "hypervisor injection", "VMM communication", "security", "reserved"
    };
    __asm__ volatile("cli" : : : "memory");
    gui_stop();
    console_write("\nFATAL EXCEPTION ");
    console_u32(frame->vector);
    if (frame->vector < 32) {
        console_write(" (");
        console_write(names[frame->vector]);
        console_write(")");
    }
    console_write("\nEIP=");
    console_hex(frame->eip);
    console_write("  error=");
    console_hex(frame->error);
    console_write("  CS=");
    console_hex(frame->cs);
    if (frame->vector == 14) {
        u32 fault_address;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_address));
        console_write("\nFault address=");
        console_hex(fault_address);
    }
    console_write("\nCPU halted. Restart the machine to continue.\n");
    for (;;)
        __asm__ volatile("hlt");
}

void interrupt_dispatch(const struct interrupt_frame *frame)
{
    u32 irq;
    if (frame->vector < 32 || frame->vector >= 48)
        exception_halt(frame);
    irq = frame->vector - 32;

    /* Spurious IRQ7 gets no EOI; a spurious IRQ15 only a master EOI. */
    if (irq == 7) {
        outb(0x20, 0x0b);
        if (!(inb(0x20) & 0x80))
            return;
    } else if (irq == 15) {
        outb(0xa0, 0x0b);
        if (!(inb(0xa0) & 0x80)) {
            outb(0x20, 0x20);
            return;
        }
    }
    if (irq == 0)
        ++ticks;
    else if (irq == 1 || irq == 12)
        keyboard_handle_irq();
    if (irq >= 8)
        outb(0xa0, 0x20);
    outb(0x20, 0x20);
}
