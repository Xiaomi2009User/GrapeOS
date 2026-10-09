#ifndef GRAPE_GFX_H
#define GRAPE_GFX_H
#include "types.h"
#define GFX_WIDTH 640u
#define GFX_HEIGHT 480u
void gfx_enter(void);
void gfx_leave(void);
void gfx_rect(u32 x, u32 y, u32 width, u32 height, u8 color);
void gfx_char(u32 x, u32 y, char character, u8 color);
void gfx_text(u32 x, u32 y, const char *text, u8 color);
void gfx_present(void);
#ifdef GRAPE_FIRMWARE
void gfx_wallpaper(void);
void gfx_rgba(u32 x, u32 y, u32 width, u32 height, const u8 *pixels, u32 source_width, u32 source_height);
void gfx_terminal_char(u32 x, u32 y, char character, u8 fg, u8 bg);
#endif
#endif
