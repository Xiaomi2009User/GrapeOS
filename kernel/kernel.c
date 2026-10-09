#include "console.h"
#include "apps.h"
#include "bootlog.h"
#include "fetch.h"
#include "framebuffer.h"
#include "input.h"
#include "gui.h"
#include "platform.h"
#include "setup.h"
#include "shell.h"
#include "store.h"
#include "system.h"

static void desktop(void)
{
    const struct store_state *profile = store_get();
    console_color(profile->foreground, profile->background);
    console_cursor_visible(true);
    console_clear();
    fetch_show();
    console_write("Welcome, ");
    console_write(profile->username);
    console_write("! Type help, gui or Grapemarket -L.\n");
    if (!store_available())
        console_write("Session mode: settings cannot survive reboot without a data disk.\n");
    shell_resume();
}

void kernel_main(void)
{
    console_init();
    bootlog_begin();
    bootlog_ok("Console ready");
    if (fb_active())
        bootlog_ok("UEFI framebuffer mapped; live mode; internal disks disabled");
    bootlog_ok("32-bit protected mode entered; kernel memory initialized");
    char cpu[49];
    system_cpu_name(cpu);
    bootlog_info(cpu);
    bootlog_info("Configuring interrupts, timer and keyboard input...");
    platform_init();
    bootlog_ok("Interrupt handlers installed; PIC remapped");
    if (platform_polling()) {
        bootlog_ok("UEFI hardware mode: CPU clock and polled keyboard; no legacy IRQ dependency");
        bootlog_info(platform_clock_source());
    } else
        bootlog_ok("PIT programmed at 100 Hz; timer and keyboard IRQs enabled");
    bootlog_info("Reading local data disk...");
    store_init();
    if (store_available())
        bootlog_ok("Local data disk ready");
    else
        bootlog_warn("Local data disk unavailable; session mode");
    bootlog_ok("Offline applications ready");
    bootlog_ok(store_get()->configured ? "Saved profile loaded" : "First-run setup ready");
    bootlog_finish();
    console_write("[BOOT] Opening welcome setup / desktop...\n");
    if (store_get()->configured)
        desktop();
    else
        setup_begin(true);

    for (;;) {
        bool was_setup = setup_active();
        bool was_app = apps_active();
        bool was_gui = gui_active();
        int key = input_poll();
        if (key >= 0) {
            if (was_gui)
                gui_input(key);
            else if (was_setup)
                setup_input(key);
            else if (was_app)
                apps_input(key);
            else
                shell_input(key);
        }
        apps_tick();
        gui_tick();
        if (was_gui && !gui_active())
            desktop();
        if (was_setup && !setup_active())
            desktop();
        if (was_app && !apps_active())
            shell_resume();
        if (key < 0)
            platform_idle();
    }
}
