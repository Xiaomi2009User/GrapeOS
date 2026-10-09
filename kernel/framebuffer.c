#include "bootinfo.h"
#include "console.h"
#include "framebuffer.h"
#include "gfx.h"
#include "memory.h"
#include "../assets/framebuffer-font.h"

typedef unsigned long long u64;
#define FB_VIRTUAL 0xe0000000u
static u64 directory[4][512] __attribute__((aligned(4096)));
static u64 directory_pointer[4] __attribute__((aligned(32)));
static volatile u8 *screen;
static u32 width, height, pitch, bytes_per_pixel;
static u8 red_shift, green_shift, blue_shift;
static u32 gui_colors[16];
static const u8 vga_colors[16][3] = {
    {0,0,0},{0,0,170},{0,170,0},{0,170,170},
    {170,0,0},{170,0,170},{170,85,0},{170,170,170},
    {85,85,85},{85,85,255},{85,255,85},{85,255,255},
    {255,85,85},{255,85,255},{255,255,85},{255,255,255}
};
static const u8 gui_rgb[16][3] = {
    {0,0,0},{68,36,113},{40,24,68},{153,68,222},
    {222,129,255},{242,238,255},{182,174,210},{214,206,230},
    {48,48,68},{230,226,250},{129,210,190},{93,52,145},
    {255,85,85},{255,226,129},{255,255,255},{153,129,182}
};

bool fb_active(void) { return screen != NULL; }
const u8 *fb_glyph(char character)
{
    return framebuffer_font[(u8)character < 128 ? (u8)character : '?'];
}

static u32 rgb(u8 red, u8 green, u8 blue)
{
    return ((u32)red << red_shift) | ((u32)green << green_shift) | ((u32)blue << blue_shift);
}

static void map_framebuffer(u64 physical)
{
    u32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    u32 flags = 0x93; /* present, writable, 2 MiB page, uncached */
    if (d & (1u << 16)) {
        u32 low, high;
        __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x277));
        low = (low & ~0xff00u) | 0x0100u; /* PAT slot 1: write combining */
        __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0x277));
        flags = 0x8b; /* PWT selects PAT slot 1 on a large page. */
    }
    for (u32 bank = 0; bank < 4; ++bank) {
        directory_pointer[bank] = (u32)directory[bank] | 1u;
        for (u32 page = 0; page < 512; ++page)
            directory[bank][page] = ((u64)(bank * 512 + page) << 21) | 0x83u;
    }
    u64 base = physical & ~0x1fffffull;
    /* A 128 MiB virtual window covers the largest accepted framebuffer
     * plus its offset, including physical addresses beyond 4 GiB. */
    for (u32 page = 0; page < 64; ++page)
        directory[3][256 + page] = (base + ((u64)page << 21)) | flags;
    u32 cr0, cr4;
    __asm__ volatile("movl %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x20u;
    __asm__ volatile("movl %0, %%cr4" : : "r"(cr4) : "memory");
    __asm__ volatile("movl %0, %%cr3" : : "r"((u32)directory_pointer) : "memory");
    __asm__ volatile("movl %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile("movl %0, %%cr0" : : "r"(cr0) : "memory");
    screen = (volatile u8 *)(FB_VIRTUAL + ((u32)physical & 0x1fffffu));
}

bool boot_multiboot_init(u32 magic, u32 information)
{
    if (magic != 0x36d76289u || !information || (information & 7u)) return false;
    const u8 *data = (const u8 *)information;
    u32 total = *(const u32 *)data;
    if (total < 16 || total > 1024 * 1024) return false;
    for (u32 offset = 8; offset <= total - 8;) {
        const u8 *tag = data + offset;
        u32 type = *(const u32 *)tag, size = *(const u32 *)(tag + 4);
        if (size < 8 || size > total - offset) return false;
        if (type == 8 && size >= 38 && tag[29] == 1) {
            u64 physical;
            memcpy(&physical, tag + 8, 8);
            pitch = *(const u32 *)(tag + 16);
            width = *(const u32 *)(tag + 20);
            height = *(const u32 *)(tag + 24);
            bytes_per_pixel = tag[28] / 8u;
            red_shift = tag[32]; green_shift = tag[34]; blue_shift = tag[36];
            if ((tag[28] != 24 && tag[28] != 32) || !physical ||
                width < 640 || height < 480 || width > 4096 || height > 4096 ||
                pitch < width * bytes_per_pixel || pitch > 16384 ||
                tag[33] != 8 || tag[35] != 8 || tag[37] != 8 ||
                red_shift > tag[28] - 8u || green_shift > tag[28] - 8u || blue_shift > tag[28] - 8u ||
                red_shift == green_shift || red_shift == blue_shift || green_shift == blue_shift)
                return false;
            map_framebuffer(physical);
            volatile u32 *boot_info = (volatile u32 *)BOOT_INFO_ADDRESS;
            boot_info[0] = BOOT_INFO_MAGIC;
            boot_info[1] = BOOT_INFO_LIVE_USB;
            for (u32 i = 0; i < 16; ++i)
                gui_colors[i] = rgb(gui_rgb[i][0], gui_rgb[i][1], gui_rgb[i][2]);
            return true;
        }
        if (type == 0) break;
        offset += (size + 7u) & ~7u;
    }
    return false;
}

static void pixel(u32 x, u32 y, u32 color)
{
    volatile u8 *destination = screen + y * pitch + x * bytes_per_pixel;
    if (bytes_per_pixel == 4) *(volatile u32 *)destination = color;
    else {
        destination[0] = (u8)color;
        destination[1] = (u8)(color >> 8);
        destination[2] = (u8)(color >> 16);
    }
}

void fb_clear(void)
{
    for (u32 y = 0; y < height; ++y)
        for (u32 x = 0; x < width; ++x) pixel(x, y, 0);
}

void fb_cell(u32 x, u32 y, char character, u8 foreground, u8 background)
{
    if (x >= 80 || y >= 25) return;
    u32 scale = width / 640;
    if (scale > height / 400) scale = height / 400;
    u32 left = (width - 640 * scale) / 2 + x * 8 * scale;
    u32 top = (height - 400 * scale) / 2 + y * 16 * scale;
    foreground &= 15; background &= 15;
    u32 fg = rgb(vga_colors[foreground][0], vga_colors[foreground][1], vga_colors[foreground][2]);
    u32 bg = rgb(vga_colors[background][0], vga_colors[background][1], vga_colors[background][2]);
    const u8 *glyph = fb_glyph(character);
    for (u32 row = 0; row < 16; ++row)
        for (u32 column = 0; column < 8; ++column) {
            u32 color = glyph[row] & (0x80u >> column) ? fg : bg;
            for (u32 dy = 0; dy < scale; ++dy)
                for (u32 dx = 0; dx < scale; ++dx)
                    pixel(left + column * scale + dx, top + row * scale + dy, color);
        }
}

void fb_present(const u8 *planes, u32 plane_bytes)
{
    u32 scale = width / GFX_WIDTH;
    if (scale > height / GFX_HEIGHT) scale = height / GFX_HEIGHT;
    u32 left = (width - GFX_WIDTH * scale) / 2;
    u32 top = (height - GFX_HEIGHT * scale) / 2;
    for (u32 y = 0; y < GFX_HEIGHT; ++y)
        for (u32 x = 0; x < GFX_WIDTH; ++x) {
            u32 offset = y * (GFX_WIDTH / 8) + x / 8, index = 0;
            u8 mask = (u8)(0x80u >> (x & 7u));
            for (u32 plane = 0; plane < 4; ++plane)
                if (planes[plane * plane_bytes + offset] & mask) index |= 1u << plane;
            for (u32 dy = 0; dy < scale; ++dy)
                for (u32 dx = 0; dx < scale; ++dx)
                    pixel(left + x * scale + dx, top + y * scale + dy, gui_colors[index]);
        }
    __asm__ volatile("sfence" : : : "memory");
}
