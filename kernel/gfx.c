#include "gfx.h"
#include "framebuffer.h"
#include "io.h"

#define PLANE_BYTES (GFX_WIDTH * GFX_HEIGHT / 8)
static u8 pixels[4][PLANE_BYTES];
static const u8 color_index[24] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,14,1,2,3,4,5,8,6,10
};
static u8 font[8192], palette[768];
static u8 sequencer[5], crtc[25], graphics[9], attribute[21], miscellaneous;
static volatile u8 *const vram = (volatile u8 *)0xa0000;

static void indexed(u16 port, u8 index, u8 value)
{
    outb(port, index);
    outb(port + 1, value);
}

static u8 read_indexed(u16 port, u8 index)
{
    outb(port, index);
    return inb(port + 1);
}

static void font_access(void)
{
    indexed(0x3c4, 0, 1);
    indexed(0x3c4, 2, 4);
    indexed(0x3c4, 4, 7);
    indexed(0x3c4, 0, 3);
    indexed(0x3ce, 4, 2);
    indexed(0x3ce, 5, 0);
    indexed(0x3ce, 6, 4);
    /* Font bytes require byte addressing even if the previous mode used dwords. */
    indexed(0x3d4, 0x14, read_indexed(0x3d4, 0x14) & ~0x40u);
}

static void mode(u8 misc, const u8 seq[5], const u8 crt[25],
                 const u8 gc[9], const u8 ac[21])
{
    outb(0x3c2, misc);
    indexed(0x3c4, 0, 1);
    for (u8 i = 1; i < 5; ++i) indexed(0x3c4, i, seq[i]);
    indexed(0x3c4, 0, seq[0]);
    indexed(0x3d4, 0x11, read_indexed(0x3d4, 0x11) & 0x7f);
    for (u8 i = 0; i < 25; ++i) indexed(0x3d4, i, crt[i]);
    for (u8 i = 0; i < 9; ++i) indexed(0x3ce, i, gc[i]);
    for (u8 i = 0; i < 21; ++i) {
        (void)inb(0x3da);
        outb(0x3c0, i);
        outb(0x3c0, ac[i]);
    }
    (void)inb(0x3da);
    outb(0x3c0, 0x20);
}

void gfx_enter(void)
{
    if (fb_active()) {
        for (u32 character = 0; character < 256; ++character) {
            const u8 *glyph = fb_glyph((char)character);
            for (u32 row = 0; row < 32; ++row)
                font[character * 32 + row] = row < 16 ? glyph[row] : 0;
        }
        fb_clear();
        return;
    }
    /* Hardware mode-12h values checked against SeaBIOS stdvgamodes.c:
     * https://github.com/coreboot/seabios/blob/master/vgasrc/stdvgamodes.c */
    static const u8 seq[5] = {3, 1, 15, 0, 6};
    static const u8 crt[25] = {
        0x5f,0x4f,0x50,0x82,0x54,0x80,0x0b,0x3e,0,0x40,0,0,0,0,0,0,
        0xea,0x8c,0xdf,0x28,0,0xe7,4,0xe3,0xff
    };
    static const u8 gc[9] = {0,0,0,0,0,0,5,15,255};
    static const u8 ac[21] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,1,0,15,0,0};
    static const u8 colors[16][3] = {
        {0,0,0},{17,9,28},{10,6,17},{38,17,55},
        {55,32,63},{60,59,63},{45,43,52},{53,51,57},
        {12,12,17},{57,56,62},{32,52,47},{23,13,36},
        {63,21,21},{63,56,32},{63,63,63},{38,32,45}
    };
    miscellaneous = inb(0x3cc);
    for (u8 i = 0; i < 5; ++i) sequencer[i] = read_indexed(0x3c4, i);
    for (u8 i = 0; i < 25; ++i) crtc[i] = read_indexed(0x3d4, i);
    for (u8 i = 0; i < 9; ++i) graphics[i] = read_indexed(0x3ce, i);
    for (u8 i = 0; i < 21; ++i) {
        (void)inb(0x3da);
        outb(0x3c0, i);
        attribute[i] = inb(0x3c1);
    }
    outb(0x3c7, 0);
    for (u32 i = 0; i < sizeof(palette); ++i) palette[i] = inb(0x3c9);
    font_access();
    for (u32 i = 0; i < sizeof(font); ++i) font[i] = vram[i];
    mode(0xe3, seq, crt, gc, ac);
    outb(0x3c8, 0);
    for (u32 i = 0; i < sizeof(colors); ++i) outb(0x3c9, ((const u8 *)colors)[i]);
}

void gfx_leave(void)
{
    if (fb_active()) { fb_clear(); return; }
    /* Restore the text font as well as registers: graphics overwrites plane 2. */
    font_access();
    for (u32 i = 0; i < sizeof(font); ++i) vram[i] = font[i];
    mode(miscellaneous, sequencer, crtc, graphics, attribute);
    outb(0x3c8, 0);
    for (u32 i = 0; i < sizeof(palette); ++i) outb(0x3c9, palette[i]);
}

void gfx_rect(u32 x, u32 y, u32 width, u32 height, u8 color)
{
    if (x >= GFX_WIDTH || y >= GFX_HEIGHT || !width || !height) return;
    if (width > GFX_WIDTH - x) width = GFX_WIDTH - x;
    if (height > GFX_HEIGHT - y) height = GFX_HEIGHT - y;
    color = color < 24 ? color_index[color] : 0;
    u32 first = x / 8, last = (x + width - 1) / 8;
    for (u32 plane = 0; plane < 4; ++plane) {
        u8 fill = color & (1u << plane) ? 255 : 0;
        for (u32 row = y; row < y + height; ++row) {
            for (u32 byte = first; byte <= last; ++byte) {
                u8 mask = 255;
                if (byte == first) mask &= (u8)(255u >> (x & 7));
                if (byte == last) mask &= (u8)(255u << (7 - ((x + width - 1) & 7)));
                u32 offset = row * (GFX_WIDTH / 8) + byte;
                pixels[plane][offset] = (pixels[plane][offset] & ~mask) | (fill & mask);
            }
        }
    }
}

static void glyph_mask(u32 offset, u8 mask, u8 color)
{
    for (u32 plane = 0; plane < 4; ++plane)
        pixels[plane][offset] = (pixels[plane][offset] & ~mask) |
                               (color & (1u << plane) ? mask : 0);
}

void gfx_char(u32 x, u32 y, char character, u8 color)
{
    if (x >= GFX_WIDTH || y >= GFX_HEIGHT) return;
    color = color < 24 ? color_index[color] : 0;
    u32 shift = x & 7;
    for (u32 row = 0; row < 16 && y + row < GFX_HEIGHT; ++row) {
        u8 bits = font[(u8)character * 32u + row];
        u32 offset = (y + row) * (GFX_WIDTH / 8) + x / 8;
        glyph_mask(offset, bits >> shift, color);
        if (shift && x / 8 + 1 < GFX_WIDTH / 8)
            glyph_mask(offset + 1, (u8)(bits << (8 - shift)), color);
    }
}

void gfx_text(u32 x, u32 y, const char *text, u8 color)
{
    while (*text && x <= GFX_WIDTH - 8) {
        gfx_char(x, y, *text++, color);
        x += 8;
    }
}

void gfx_present(void)
{
    if (fb_active()) { fb_present(&pixels[0][0], PLANE_BYTES); return; }
    for (u8 plane = 0; plane < 4; ++plane) {
        indexed(0x3c4, 2, (u8)(1u << plane));
        for (u32 i = 0; i < PLANE_BYTES; ++i) vram[i] = pixels[plane][i];
    }
}
