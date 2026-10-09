#ifndef GRAPEOS_CONSOLE_H
#define GRAPEOS_CONSOLE_H

#include "types.h"

void console_init(void);
void console_clear(void);
void console_putc(char character);
void console_write(const char *text);
void console_u32(u32 value);
void console_i32(i32 value);
void console_hex(u32 value);
void console_color(u8 foreground, u8 background);
u8 console_get_color(void);
void console_draw(u32 x, u32 y, char character, u8 foreground, u8 background);
void console_cursor_visible(bool visible);
void console_serial_write(const char *text);
int serial_poll(void);

#endif
