#ifndef GRAPE_FRAMEBUFFER_H
#define GRAPE_FRAMEBUFFER_H
#include "types.h"
bool boot_multiboot_init(u32 magic, u32 information);
bool fb_active(void);
const u8 *fb_glyph(char character);
void fb_clear(void);
void fb_cell(u32 x, u32 y, char character, u8 foreground, u8 background);
void fb_present(const u8 *planes, u32 plane_bytes);
#endif
