#include "console.h"
#include "io.h"
#include "gui.h"
#include "framebuffer.h"

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u
#define COM1 0x3f8u

static volatile u16 *const video = (volatile u16 *)0xb8000;
static u32 column;
static u32 row;
static u8 attribute = 0x07;
static int serial_present;
static u16 cells[VGA_WIDTH * VGA_HEIGHT];

static void cell_write(u32 index, u16 value)
{
    if (fb_active()) {
        cells[index] = value;
        fb_cell(index % VGA_WIDTH, index / VGA_WIDTH, (char)value, value >> 8, value >> 12);
    } else video[index] = value;
}

static void serial_putc(char character)
{
    if (!serial_present)
        return;
    /* A missing or broken UART must never freeze the screen console. */
    for (u32 retry = 0; retry < 65536u; ++retry) {
        if (inb(COM1 + 5) & 0x20) {
            outb(COM1, (u8)character);
            return;
        }
    }
}

static void cursor_update(void)
{
    if (fb_active()) return;
    u16 position = (u16)(row * VGA_WIDTH + column);
    outb(0x3d4, 0x0f);
    outb(0x3d5, (u8)position);
    outb(0x3d4, 0x0e);
    outb(0x3d5, (u8)(position >> 8));
}

static void scroll(void)
{
    if (row < VGA_HEIGHT)
        return;
    for (u32 index = 0; index < (VGA_HEIGHT - 1) * VGA_WIDTH; ++index)
        cell_write(index, fb_active() ? cells[index + VGA_WIDTH] : video[index + VGA_WIDTH]);
    for (u32 index = (VGA_HEIGHT - 1) * VGA_WIDTH;
         index < VGA_HEIGHT * VGA_WIDTH; ++index)
        cell_write(index, ((u16)attribute << 8) | ' ');
    row = VGA_HEIGHT - 1;
}

void console_color(u8 foreground, u8 background)
{
    attribute = (u8)((background << 4) | foreground);
}

u8 console_get_color(void)
{
    return attribute;
}

void console_draw(u32 x, u32 y, char character, u8 foreground, u8 background)
{
    if (gui_active()) { gui_console_draw(x, y, character, foreground, background); return; }
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT)
        return;
    u8 color = (u8)(((background & 7) << 4) | (foreground & 15));
    cell_write(y * VGA_WIDTH + x, ((u16)color << 8) | (u8)character);
}

void console_cursor_visible(bool visible)
{
    if (gui_active() || fb_active()) return;
    outb(0x3d4, 0x0a);
    u8 start = inb(0x3d5);
    outb(0x3d5, visible ? (u8)(start & ~0x20) : (u8)(start | 0x20));
}

void console_serial_write(const char *text)
{
    while (*text) {
        if (*text == '\n')
            serial_putc('\r');
        serial_putc(*text++);
    }
}

void console_clear(void)
{
    if (gui_active()) {
        gui_console_clear();
        console_serial_write("\033[2J\033[H");
        return;
    }
    if (fb_active()) fb_clear();
    for (u32 index = 0; index < VGA_WIDTH * VGA_HEIGHT; ++index)
        cell_write(index, ((u16)attribute << 8) | ' ');
    column = 0;
    row = 0;
    cursor_update();
    const char *escape = "\033[2J\033[H";
    while (*escape)
        serial_putc(*escape++);
}

void console_init(void)
{
    outb(COM1 + 1, 0x00); /* Polling mode: no UART interrupts. */
    outb(COM1 + 3, 0x80);
    outb(COM1, 0x01);     /* 115200 baud. */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* Eight data bits, no parity, one stop bit. */
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
    serial_present = inb(COM1 + 5) != 0xff;
    console_clear();
}

int serial_poll(void)
{
    if (serial_present && (inb(COM1 + 5) & 1))
        return inb(COM1);
    return -1;
}

void console_putc(char character)
{
    if (gui_active()) {
        gui_console_putc(character);
        if (character == '\n') serial_putc('\r');
        if (character == '\b') {
            serial_putc('\b'); serial_putc(' '); serial_putc('\b');
        } else serial_putc(character);
        return;
    }
    if (character == '\n') {
        serial_putc('\r');
        serial_putc('\n');
        column = 0;
        ++row;
    } else if (character == '\r') {
        serial_putc('\r');
        column = 0;
    } else if (character == '\b') {
        if (column == 0 && row == 0)
            return;
        if (column == 0) {
            --row;
            column = VGA_WIDTH;
        }
        --column;
        cell_write(row * VGA_WIDTH + column, ((u16)attribute << 8) | ' ');
        serial_putc('\b');
        serial_putc(' ');
        serial_putc('\b');
    } else if (character == '\t') {
        do {
            console_putc(' ');
        } while (column % 4 != 0);
        return;
    } else if ((u8)character >= 32 && (u8)character < 127) {
        serial_putc(character);
        cell_write(row * VGA_WIDTH + column, ((u16)attribute << 8) | (u8)character);
        if (++column == VGA_WIDTH) {
            column = 0;
            ++row;
        }
    }
    scroll();
    cursor_update();
}

void console_write(const char *text)
{
    while (*text)
        console_putc(*text++);
}

void console_u32(u32 value)
{
    char digits[10];
    u32 length = 0;
    do {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (length)
        console_putc(digits[--length]);
}

void console_i32(i32 value)
{
    if (value < 0) {
        console_putc('-');
        console_u32(0u - (u32)value);
    } else {
        console_u32((u32)value);
    }
}

void console_hex(u32 value)
{
    static const char digits[] = "0123456789abcdef";
    console_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        console_putc(digits[(value >> shift) & 0x0f]);
}
