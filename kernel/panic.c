#include "console.h"
#include "gui.h"
#include "panic.h"
#include "platform.h"

static void panic_line(const char *status, u8 color, const char *text)
{
    console_color(color, 0);
    console_write(status);
    console_color(7, 0);
    console_write(text);
    console_putc('\n');
}

_Noreturn void kernel_panic(const char *code, const char *reason, const char *trigger)
{
    u32 entry_flags, stack_pointer, control_register;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(entry_flags) : : "memory");
    __asm__ volatile ("movl %%esp, %0" : "=r"(stack_pointer));
    __asm__ volatile ("movl %%cr0, %0" : "=r"(control_register));
    u32 caller = (u32)__builtin_return_address(0);
    u32 ticks = timer_ticks();
    gui_stop();

    console_color(7, 0);
    console_clear();
    console_cursor_visible(false);
    console_color(12, 0);
    console_write("\n  GrapeOS / KERNEL PANIC\n");
    console_write("  ============================================================\n\n");
    panic_line("[ INFO ] Trigger: ", 14, trigger);
    panic_line("[ FAIL ] Code: ", 12, code);
    panic_line("[ FAIL ] ", 12, reason);
    panic_line("[ PANIC ] ", 12, "The kernel cannot continue.");

    console_write("\nDiagnostic log:\n");
    console_write("  Kernel: GrapeOS 0.1 / i386\n  Uptime: ");
    console_u32(ticks / TIMER_HZ);
    console_write(" seconds (");
    console_u32(ticks);
    console_write(" ticks)\n  Caller: ");
    console_hex(caller);
    console_write("\n  Panic ESP: ");
    console_hex(stack_pointer);
    console_write("\n  CR0: ");
    console_hex(control_register);
    console_write("  Entry EFLAGS: ");
    console_hex(entry_flags);
    console_write("\n\nReset or reopen QEMU to start GrapeLoad again.\n");
    console_color(12, 0);
    console_write("CPU halted. Restart the virtual machine to continue.\n");
    for (;;)
        __asm__ volatile ("hlt" : : : "memory");
}
