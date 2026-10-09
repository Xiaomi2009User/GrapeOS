#include "console.h"
#include "fetch.h"
#include "framebuffer.h"
#include "platform.h"
#include "system.h"
#include "store.h"

#define LOGO_WIDTH 32
#define LOGO_HEIGHT 19

extern char __kernel_start[];
extern char __kernel_end[];

struct grape {
    int x;
    int y;
    int radius_x;
    int radius_y;
};

/* Text-mode interpretation of the supplied purple grape cluster. */
static const struct grape grapes[] = {
    {15, 10, 10, 5}, /* Connect the overlapping berries into one cluster. */
    {9, 6, 5, 3}, {15, 5, 5, 3}, {21, 6, 5, 3},
    {5, 9, 5, 3}, {25, 9, 5, 3},
    {11, 12, 5, 3}, {19, 12, 5, 3}, {15, 15, 5, 3}
};

static int inside(const struct grape *grape, int x, int y)
{
    int dx = x - grape->x;
    int dy = y - grape->y;
    int rx_squared = grape->radius_x * grape->radius_x;
    int ry_squared = grape->radius_y * grape->radius_y;
    return dx * dx * ry_squared + dy * dy * rx_squared
           <= rx_squared * ry_squared;
}

static int berry_at(int x, int y)
{
    int berry = -1;
    for (u32 index = 0; index < sizeof(grapes) / sizeof(grapes[0]); ++index)
        if (inside(&grapes[index], x, y))
            berry = (int)index;
    return berry;
}

static void logo_cell(int x, int y, u8 background)
{
    /* A bent stem, matching the reference without a leaf. */
    if ((y == 0 && x == 13) || (y == 1 && x == 14)
        || (y == 2 && (x == 14 || x == 15))) {
        console_color(13, background);
        console_putc('/');
        return;
    }
    int berry = berry_at(x, y);
    if (berry < 0) {
        console_putc(' ');
        return;
    }
    int dx = x - grapes[berry].x;
    int dy = y - grapes[berry].y;
    int highlight = berry > 0 && (dx == -2 || dx == -1) && dy == -1;
    int edge = berry_at(x - 1, y) < 0 || berry_at(x + 1, y) < 0
               || berry_at(x, y - 1) < 0 || berry_at(x, y + 1) < 0;
    console_color(highlight ? 15 : edge ? 5 : 13, background);
    console_putc(highlight ? 'o' : edge ? '#' : '@');
}

static void field(const char *label, const char *value, u8 background)
{
    console_color(13, background);
    console_write(label);
    console_color(7, background);
    console_write(value);
}

static void uptime(u8 background)
{
    u32 ticks = timer_ticks();
    field("Uptime: ", "", background);
    console_u32(ticks / TIMER_HZ / 3600);
    console_write("h ");
    console_u32((ticks / TIMER_HZ / 60) % 60);
    console_write("m ");
    console_u32((ticks / TIMER_HZ) % 60);
    console_write("s");
}

void fetch_show(void)
{
    u8 saved_color = console_get_color();
    u8 background = saved_color >> 4;
    char cpu[49];
    system_cpu_name(cpu);
    /* Keep the longest CPUID brand within the 80-column console. */
    cpu[37] = '\0';

    for (int row = 0; row < LOGO_HEIGHT; ++row) {
        for (int column = 0; column < LOGO_WIDTH; ++column)
            logo_cell(column, row, background);
        console_write("  ");
        switch (row) {
        case 1:
            console_color(15, background);
            const struct store_state *profile = store_get();
            /* The two profile names together must fit the right-hand column. */
            for (u32 index = 0; profile->username[index] && index < 20; ++index)
                console_putc(profile->username[index]);
            console_putc('@');
            for (u32 index = 0; profile->hostname[index] && index < 23; ++index)
                console_putc(profile->hostname[index]);
            break;
        case 2:
            console_color(5, background);
            console_write("-----------------------------");
            break;
        case 3:
#ifdef GRAPE_FIRMWARE
            field("OS: ", "GrapeOS Desktop 0.3", background); break;
#else
            field("OS: ", "GrapeOS 0.1", background); break;
#endif
#ifdef GRAPE_FIRMWARE
        case 4: field("Kernel: ", "Grape UEFI desktop 0.3", background); break;
        case 5: field("Arch: ", "x86_64 / firmware services", background); break;
#else
        case 4: field("Kernel: ", "Grape bare-metal 0.1", background); break;
        case 5: field("Arch: ", "i386 (32-bit protected mode)", background); break;
#endif
        case 6: field("CPU: ", cpu, background); break;
        case 7: uptime(background); break;
        case 8: field("Shell: ", "grape-shell 0.1", background); break;
        case 9: field("Resolution: ", fb_active() ? "80x25 GOP text" : "80x25 VGA text", background); break;
        case 10: field("Terminal: ", fb_active() ? "GOP / COM1 115200" : "VGA / COM1 115200", background); break;
        case 11:
#ifdef GRAPE_FIRMWARE
            field("Boot: ", "GrapeLoad / UEFI services", background);
#else
            field("Boot: ", fb_active() ? "UEFI x64 / GRUB Multiboot2" : "Custom BIOS loader", background);
#endif
            break;
        case 12:
            field("Kernel size: ", "", background);
            console_u32(system_kernel_bytes());
            console_write(" bytes");
            break;
        case 14:
        case 15:
            for (u8 color = 0; color < 8; ++color) {
                console_color(row == 15 ? (u8)(color + 8) : color, background);
                console_write("###");
            }
            break;
        default: break;
        }
        console_putc('\n');
    }
    console_color(saved_color & 0x0f, background);
}
