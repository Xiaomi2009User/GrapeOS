#ifndef GRAPE_GUI_H
#define GRAPE_GUI_H
#include "types.h"
void gui_start(void);
void gui_stop(void);
bool gui_active(void);
bool gui_terminal_active(void);
void gui_input(int key);
void gui_tick(void);
void gui_console_putc(char character);
void gui_console_clear(void);
void gui_console_draw(u32 x, u32 y, char character, u8 fg, u8 bg);
#endif
