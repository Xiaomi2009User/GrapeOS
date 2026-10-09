#include "bootlog.h"
#include "console.h"
#include "input.h"
#include "platform.h"

static void log_line(const char *status, u8 color, const char *message)
{
    console_color(color, 0);
    console_write(status);
    console_color(7, 0);
    console_write(message);
    console_putc('\n');
}

void bootlog_begin(void)
{
    console_color(13, 0);
    console_write("\n  GrapeLoad / Booting GrapeOS\n");
    console_write("  ================================================\n\n");
    console_color(7, 0);
}

void bootlog_info(const char *message)
{
    log_line("[ .. ] ", 13, message);
}

void bootlog_ok(const char *message)
{
    log_line("[ OK ] ", 10, message);
}

void bootlog_warn(const char *message)
{
    log_line("[WARN] ", 14, message);
}

void bootlog_finish(void)
{
    bootlog_ok("GrapeOS ready");
    console_write("\nStarting GrapeOS...\n");
    /* Keep completed initialization messages readable before changing screens. */
    u32 started = timer_ticks();
    while ((u32)(timer_ticks() - started) < 2u * TIMER_HZ) {
        input_discard();
        platform_idle();
    }
    input_discard();
}
