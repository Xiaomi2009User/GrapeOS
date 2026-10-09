#include "internal.h"
#include "console.h"
#include "framebuffer.h"
#include "gfx.h"
#include "gui.h"
#include "input.h"
#include "io.h"
#include "memory.h"
#include "mouse.h"
#include "panic.h"
#include "platform.h"
#include "setup.h"
#include "shell.h"
#include "store.h"
#include "apps.h"
#include "../assets/framebuffer-font.h"
#include "../assets/terminal-font.h"

EFI_SYSTEM_TABLE *grape_st;
EFI_HANDLE grape_image;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *keys;
static EFI_SIMPLE_POINTER_PROTOCOL *relative[8];
static EFI_ABSOLUTE_POINTER_PROTOCOL *absolute[8];
static u32 relative_count, absolute_count;
static struct mouse_state pointer = {320, 240, 0, false};
static unsigned long long clock_base;
static u32 clock_khz;
static u32 canvas[640 * 480], console_col, console_row;
static u32 presented[640 * 480], scanline[4096];
static bool frame_valid, text_dirty = true, caret_drawn;
static u32 caret_col, caret_row;
static u8 color = 7;
static bool cursor_visible = true, serial_present;
static u32 viewport_left, viewport_top, viewport_width, viewport_height;
static u32 xmap[4096], ymap[4096];
static u32 colors[24] = {
    0x101019,0x342553,0x183b32,0x286273,0x9c394e,0xa271c8,0xdac478,0xccc6dc,
    0x4b465f,0xa98cfa,0x8cd6be,0x89c8e4,0xf48794,0xc7a1ff,0xf4d788,0xf3edff,
    0x161121,0x251c37,0x8c55df,0xc7a1ff,0xf4f0fc,0x30263f,0x4a3c60,0x9cdec9
};
static u32 desktop_theme;
static bool wallpaper_ready;
static const char *const theme_names[] = {"grape", "mint", "midnight"};
static unsigned long long stamp(void)
{
    u32 low, high; __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((unsigned long long)high << 32) | low;
}
void platform_init(void)
{
    unsigned long long begin = stamp();
    grape_st->BootServices->Stall(50000);
    clock_khz = (u32)((stamp() - begin) / 50);
    if (!clock_khz) clock_khz = 1000000;
    clock_base = stamp();
}
u32 timer_ticks(void) { return (u32)((stamp() - clock_base) / ((unsigned long long)clock_khz * 10)); }
bool platform_polling(void) { return true; }
void platform_idle(void) { grape_st->BootServices->Stall(1000); }
const char *platform_clock_source(void) { return "CPU TSC / UEFI Stall calibration"; }
u32 platform_clock_khz(void) { return clock_khz; }
bool firmware_time_text(char date[11], char time[9])
{
    static EFI_TIME cached;
    static u32 sampled;
    static bool ready, valid;
    u32 second = timer_ticks() / TIMER_HZ;
    if (!ready || second != sampled) {
        ready = true; sampled = second;
        valid = !EFI_ERROR(grape_st->RuntimeServices->GetTime(&cached, NULL));
        valid = valid && cached.Year >= 1900 && cached.Year <= 9999 &&
            cached.Month >= 1 && cached.Month <= 12 && cached.Day >= 1 && cached.Day <= 31 &&
            cached.Hour < 24 && cached.Minute < 60 && cached.Second < 60;
    }
    if (!valid) { memcpy(date, "---- -- --", 11); memcpy(time, "--:--:--", 9); return false; }
    u32 values[] = {cached.Year, cached.Month, cached.Day, cached.Hour, cached.Minute, cached.Second};
    memcpy(date, "0000-00-00", 11); memcpy(time, "00:00:00", 9);
    date[0] = '0' + values[0] / 1000; date[1] = '0' + values[0] / 100 % 10;
    date[2] = '0' + values[0] / 10 % 10; date[3] = '0' + values[0] % 10;
    for (u32 i = 1; i < 6; ++i) {
        char *p = i < 3 ? date + (i == 1 ? 5 : 8) : time + (i - 3) * 3;
        p[0] = '0' + values[i] / 10; p[1] = '0' + values[i] % 10;
    }
    return true;
}
_Noreturn void firmware_reset(void)
{
    grape_st->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    for (;;) platform_idle();
}
static void initialize_input(void)
{
    EFI_GUID key_guid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
    grape_st->BootServices->HandleProtocol(grape_st->ConsoleInHandle, &key_guid, (void **)&keys);
    EFI_GUID guids[2] = {EFI_SIMPLE_POINTER_PROTOCOL_GUID, EFI_ABSOLUTE_POINTER_PROTOCOL_GUID};
    for (u32 type = 0; type < 2; ++type) {
        EFI_HANDLE *handles = NULL; UINTN count = 0;
        if (EFI_ERROR(grape_st->BootServices->LocateHandleBuffer(ByProtocol, &guids[type], NULL, &count, &handles))) continue;
        for (UINTN i = 0; i < count && i < 8; ++i) {
            void *device = NULL;
            if (EFI_ERROR(grape_st->BootServices->HandleProtocol(handles[i], &guids[type], &device))) continue;
            if (!type) relative[relative_count++] = device;
            else absolute[absolute_count++] = device;
        }
        grape_st->BootServices->FreePool(handles);
    }
    pointer.available = relative_count || absolute_count;
}
int keyboard_poll(void)
{
    EFI_INPUT_KEY key; UINT32 shifts = 0;
    if (keys) {
        EFI_KEY_DATA data = {0};
        if (EFI_ERROR(keys->ReadKeyStrokeEx(keys, &data))) return -1;
        key = data.Key;
        if (data.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID)
            shifts = data.KeyState.KeyShiftState;
    } else if (EFI_ERROR(grape_st->ConIn->ReadKeyStroke(grape_st->ConIn, &key))) return -1;
    switch (key.ScanCode) {
    case 1: return KEY_UP; case 2: return KEY_DOWN; case 3: return KEY_RIGHT; case 4: return KEY_LEFT;
    case 5: return KEY_HOME; case 6: return KEY_END; case 8: return KEY_DELETE;
    case 18: return KEY_POINTER;
    case 23: return 0x1b;
    default: break;
    }
    u32 c = key.UnicodeChar;
    if (c == '\r') return '\n';
    if (c >= 'A' && c <= 'Z' && (shifts & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED))) c -= 'A' - 1;
    else if (c >= 'a' && c <= 'z' && (shifts & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED))) c -= 'a' - 1;
    return c && c < 127 ? (int)c : -1;
}
bool mouse_init(void) { return pointer.available; }
void mouse_byte(u8 byte) { (void)byte; }
void mouse_poll(struct mouse_state *out)
{
    for (u32 i = 0; i < relative_count; ++i) {
        EFI_SIMPLE_POINTER_STATE state;
        if (EFI_ERROR(relative[i]->GetState(relative[i], &state))) continue;
        pointer.x += state.RelativeMovementX;
        pointer.y += state.RelativeMovementY;
        pointer.buttons = state.LeftButton ? 1 : 0;
    }
    for (u32 i = 0; i < absolute_count; ++i) {
        EFI_ABSOLUTE_POINTER_STATE state;
        if (EFI_ERROR(absolute[i]->GetState(absolute[i], &state))) continue;
        EFI_ABSOLUTE_POINTER_MODE *mode = absolute[i]->Mode;
        if (mode->AbsoluteMaxX > mode->AbsoluteMinX && mode->AbsoluteMaxY > mode->AbsoluteMinY) {
            UINT64 px = (state.CurrentX - mode->AbsoluteMinX) * gop->Mode->Info->HorizontalResolution /
                        (mode->AbsoluteMaxX - mode->AbsoluteMinX);
            UINT64 py = (state.CurrentY - mode->AbsoluteMinY) * gop->Mode->Info->VerticalResolution /
                        (mode->AbsoluteMaxY - mode->AbsoluteMinY);
            pointer.x = ((i32)px - (i32)viewport_left) * 640 / (i32)viewport_width;
            pointer.y = ((i32)py - (i32)viewport_top) * 480 / (i32)viewport_height;
        }
        pointer.buttons = state.ActiveButtons & 1;
    }
    if (pointer.x < 0) pointer.x = 0;
    if (pointer.y < 0) pointer.y = 0;
    if (pointer.x > 639) pointer.x = 639;
    if (pointer.y > 479) pointer.y = 479;
    *out = pointer;
}
bool fb_active(void) { return gop != NULL; }
const u8 *fb_glyph(char c) { return framebuffer_font[(u8)c < 128 ? (u8)c : '?']; }
void gfx_rect(u32 x, u32 y, u32 width, u32 height, u8 ink)
{
    if (x >= 640 || y >= 480 || ink >= 24) return;
    if (width > 640 - x) width = 640 - x;
    if (height > 480 - y) height = 480 - y;
    for (u32 r = y; r < y + height; ++r)
        for (u32 c = x; c < x + width; ++c) canvas[r * 640 + c] = colors[ink];
}
void gfx_char(u32 x, u32 y, char c, u8 ink)
{
    if (x >= 640 || y >= 480 || ink >= 24) return;
    const u8 *glyph = fb_glyph(c);
    u32 height = y > 464 ? 480 - y : 16, width = x > 632 ? 640 - x : 8;
    for (u32 r = 0; r < height; ++r)
        for (u32 col = 0; col < width; ++col)
            if (glyph[r] & (0x80 >> col)) canvas[(y + r) * 640 + x + col] = colors[ink];
}
void gfx_text(u32 x, u32 y, const char *text, u8 ink)
{
    while (*text && x <= 632) { gfx_char(x, y, *text++, ink); x += 8; }
}
static u32 channel(u32 value, u32 mask);
static u32 pixel_rgb(u32 value)
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;
    if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
        return (value & 0xff00) | ((value & 255) << 16) | (value >> 16);
    if (info->PixelFormat == PixelBitMask) {
        EFI_PIXEL_BITMASK masks = info->PixelInformation;
        return channel(value >> 16, masks.RedMask) | channel((value >> 8) & 255, masks.GreenMask) | channel(value & 255, masks.BlueMask);
    }
    return value;
}
static bool theme_equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void theme_apply(u32 index)
{
    static const u32 palette[3][8] = {
        {0x161121,0x251c37,0x8c55df,0xc7a1ff,0xf4f0fc,0x30263f,0x4a3c60,0x9cdec9},
        {0x101e1c,0x19332e,0x277e68,0x9cdec9,0xebfcf5,0x20483d,0x315b4f,0x9cdec9},
        {0x0e1626,0x17263d,0x346ba8,0xa5ceff,0xecf4ff,0x20334e,0x324965,0x8cc8e8}
    };
    desktop_theme = index;
    for (u32 i = 0; i < 8; ++i) colors[16+i] = pixel_rgb(palette[index][i]);
    wallpaper_ready = false; frame_valid = false;
}
const char *firmware_theme_name(void) { return theme_names[desktop_theme]; }
const char *firmware_theme_set(const char *name)
{
    u32 i = 0; while (i < 3 && !theme_equal(name, theme_names[i])) ++i;
    if (i == 3) return "Unknown theme. Use grape, mint or midnight.";
    if (firmware_files_ready() && !firmware_file_write(".desktop-theme", &i, sizeof(i)))
        return "USB save failed; theme was not changed.";
    theme_apply(i);
    return firmware_files_ready() ? "Desktop theme saved to USB." : "Desktop theme changed for this session.";
}
const char *firmware_theme_next(void)
{
    return firmware_theme_set(theme_names[(desktop_theme + 1) % 3]);
}
static void theme_load(void)
{
    u32 index; usize length;
    if (firmware_file_read(".desktop-theme", &index, sizeof(index), &length) && length == sizeof(index) && index < 3)
        theme_apply(index);
}
void gfx_wallpaper(void)
{
    static u32 wallpaper[640 * 480];
    if (!wallpaper_ready) {
        for (u32 y = 0; y < 480; ++y)
            for (u32 x = 0; x < 640; ++x) {
                i32 dx = (i32)x - 470, dy = (i32)y - 120;
                u32 distance = (u32)(dx * dx + dy * dy), glow = distance < 160000 ? (160000 - distance) / 6000 : 0;
                u32 r = 13 + y / 32 + glow, g = 18 + x / 80 + glow / 3, b = 34 + x / 28 + glow * 2;
                if (desktop_theme == 1) { r = 10 + y / 64 + glow / 3; g = 25 + x / 32 + glow; b = 26 + x / 64 + glow / 2; }
                if (desktop_theme == 2) { r = 9 + y / 64 + glow / 4; g = 17 + x / 64 + glow / 2; b = 34 + x / 32 + glow * 2; }
                wallpaper[y * 640 + x] = pixel_rgb((r << 16) | (g << 8) | b);
            }
        wallpaper_ready = true;
    }
    memcpy(canvas, wallpaper, sizeof(canvas));
}
void gfx_rgba(u32 x, u32 y, u32 width, u32 height, const u8 *pixels, u32 sw, u32 sh)
{
    struct raster { const u8 *source; u32 w, h, sw, sh, used; u32 pixels[340 * 256]; };
    static struct raster cache[3]; static u32 sequence;
    if (!pixels || !sw || !sh || !width || !height || width > 340 || height > 256 || x >= 640 || y >= 480) return;
    struct raster *image = NULL;
    for (u32 i = 0; i < 3; ++i)
        if (cache[i].source == pixels && cache[i].w == width && cache[i].h == height && cache[i].sw == sw && cache[i].sh == sh) image = &cache[i];
    if (!image) {
        image = &cache[0];
        for (u32 i = 1; i < 3; ++i) if (cache[i].used < image->used) image = &cache[i];
        image->source = pixels; image->w = width; image->h = height; image->sw = sw; image->sh = sh;
        for (u32 dy = 0; dy < height; ++dy) {
            u32 sy = height > 1 ? dy * (sh - 1) * 256 / (height - 1) : 0, fy = sy & 255; sy >>= 8;
            u32 ny = sy + 1 < sh ? sy + 1 : sy;
            for (u32 dx = 0; dx < width; ++dx) {
                u32 sx = width > 1 ? dx * (sw - 1) * 256 / (width - 1) : 0, fx = sx & 255; sx >>= 8;
                u32 nx = sx + 1 < sw ? sx + 1 : sx;
                const u8 *a = pixels + (sy * sw + sx) * 4, *b = pixels + (sy * sw + nx) * 4;
                const u8 *c = pixels + (ny * sw + sx) * 4, *d = pixels + (ny * sw + nx) * 4;
                u32 rgba[4];
                for (u32 k = 0; k < 4; ++k) rgba[k] = (a[k] * (256-fx) * (256-fy) + b[k] * fx * (256-fy) + c[k] * (256-fx) * fy + d[k] * fx * fy + 32768) >> 16;
                u32 rgb = 0;
                for (u32 k = 0; k < 3; ++k) rgb = (rgb << 8) | (rgba[k] * rgba[3] + 255 * (255-rgba[3]) + 127) / 255;
                image->pixels[dy * width + dx] = pixel_rgb(rgb);
            }
        }
    }
    image->used = ++sequence;
    u32 rows = height < 480-y ? height : 480-y, cols = width < 640-x ? width : 640-x;
    for (u32 dy = 0; dy < rows; ++dy) memcpy(canvas + (y+dy)*640+x, image->pixels+dy*width, cols * sizeof(u32));
}
void gfx_terminal_char(u32 x, u32 y, char c, u8 fg, u8 bg)
{
    /* A native 6x12 glyph plus one spacing column fits the existing 80x25
     * terminal. No font compression, alpha blending or intermediate shades. */
    const u8 *glyph = terminal_font[(u8)c < 128 ? (u8)c : '?'];
    for (u32 row = 0; row < 12 && y+row < 480; ++row)
        for (u32 col = 0; col < 7 && x+col < 640; ++col)
            canvas[(y+row)*640+x+col] = colors[col < 6 && (glyph[row] & (0x80 >> col)) ? fg & 15 : bg & 15];
}
static u32 channel(u32 value, u32 mask)
{
    if (!mask) return 0;
    u32 shift = 0; while (!(mask & 1)) { mask >>= 1; ++shift; }
    return ((value * mask / 255) << shift);
}
static void copy_scanline(u32 *destination, u32 count)
{
    const u32 *source = scanline;
    usize words = count;
    /* Contiguous writes from ordinary RAM; never read the GOP aperture. */
    __asm__ volatile("rep movsl" : "+D"(destination), "+S"(source), "+c"(words) : : "memory");
}
void gfx_present(void)
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;
    u32 *screen = (u32 *)(usize)gop->Mode->FrameBufferBase;
    u8 damage[30][40]; bool changed = !frame_valid;
    if (!frame_valid) {
        for (u32 x = 0; x < info->HorizontalResolution; ++x) scanline[x] = colors[16];
        for (u32 y = 0; y < info->VerticalResolution; ++y)
            copy_scanline(screen + y * info->PixelsPerScanLine, info->HorizontalResolution);
    }
    /* Compare final pixels in cached RAM. Clearing/repainting a window does
     * not force writes for pixels whose final appearance stayed the same. */
    for (u32 ty = 0; ty < 30; ++ty)
        for (u32 tx = 0; tx < 40; ++tx) {
            bool different = !frame_valid;
            for (u32 y = ty * 16; !different && y < ty * 16 + 16; ++y)
                for (u32 x = tx * 16; x < tx * 16 + 16; ++x)
                    if (canvas[y * 640 + x] != presented[y * 640 + x]) { different = true; break; }
            damage[ty][tx] = different; changed |= different;
        }
    if (!changed) return;
    for (u32 ty = 0; ty < 30; ++ty) {
        u32 top = viewport_top + (ty * 16 * viewport_height + 479) / 480;
        u32 bottom = viewport_top + ((ty + 1) * 16 * viewport_height + 479) / 480;
        for (u32 tx = 0; tx < 40;) {
            if (!damage[ty][tx]) { ++tx; continue; }
            u32 first = tx;
            while (tx < 40 && damage[ty][tx]) ++tx;
            u32 left = viewport_left + (first * 16 * viewport_width + 639) / 640;
            u32 right = viewport_left + (tx * 16 * viewport_width + 639) / 640;
            u32 previous_row = 0xffffffffu;
            for (u32 y = top; y < bottom; ++y) {
                if (ymap[y] != previous_row) {
                    for (u32 x = left; x < right; ++x) scanline[x - left] = canvas[ymap[y] + xmap[x]];
                    previous_row = ymap[y];
                }
                copy_scanline(screen + y * info->PixelsPerScanLine + left, right - left);
            }
        }
    }
    for (u32 i = 0; i < 640 * 480; ++i) presented[i] = canvas[i];
    frame_valid = true;
}
void gfx_enter(void) { }
void gfx_leave(void) { gfx_rect(0, 0, 640, 480, 0); }
void fb_clear(void) { gfx_rect(0, 0, 640, 480, color >> 4); text_dirty = true; }
void fb_cell(u32 x, u32 y, char c, u8 fg, u8 bg)
{
    if (x >= 80 || y >= 25) return;
    text_dirty = true;
    gfx_rect(x * 8, 40 + y * 16, 8, 16, bg & 15);
    gfx_char(x * 8, 40 + y * 16, c, fg & 15);
}
static char text_cells[25][80];
void console_color(u8 fg, u8 bg) { color = (bg << 4) | fg; }
u8 console_get_color(void) { return color; }
void console_draw(u32 x, u32 y, char c, u8 fg, u8 bg)
{
    if (gui_active()) { gui_console_draw(x, y, c, fg, bg); return; }
    if (x < 80 && y < 25) { text_cells[y][x] = c; fb_cell(x, y, c, fg, bg); }
}
void console_cursor_visible(bool show) { cursor_visible = show; text_dirty = true; }
static void uart(char c)
{
    if (!serial_present) return;
    for (u32 tries = 0; tries < 256; ++tries)
        if (inb(0x3fd) & 0x20) { outb(0x3f8, (u8)c); return; }
    serial_present = false; /* A stalled diagnostic UART must not stall the desktop. */
}
void console_serial_write(const char *s) { while (*s) { if (*s == '\n') uart('\r'); uart(*s++); } }
int serial_poll(void) { return serial_present && (inb(0x3fd) & 1) ? inb(0x3f8) : -1; }
void console_clear(void)
{
    if (gui_active()) { gui_console_clear(); return; }
    console_col = console_row = 0; memset(text_cells, ' ', sizeof(text_cells));
    caret_drawn = false;
    fb_clear(); console_serial_write("\033[2J\033[H");
}
void console_putc(char c)
{
    if (c == '\n') uart('\r');
    uart(c);
    if (gui_active()) { gui_console_putc(c); return; }
    if (c == '\n') { console_col = 0; ++console_row; }
    else if (c == '\r') console_col = 0;
    else if (c == '\b') {
        if (console_col) --console_col;
        else if (console_row) { --console_row; console_col = 79; }
        text_cells[console_row][console_col] = ' ';
        fb_cell(console_col, console_row, ' ', color & 15, color >> 4);
    } else if (c >= 32 && c < 127) {
        text_cells[console_row][console_col] = c;
        fb_cell(console_col, console_row, c, color & 15, color >> 4);
        if (++console_col == 80) { console_col = 0; ++console_row; }
    }
    if (console_row >= 25) {
        memmove(text_cells, text_cells[1], 24 * 80); memset(text_cells[24], ' ', 80); console_row = 24;
        for (u32 y = 0; y < 25; ++y)
            for (u32 x = 0; x < 80; ++x) fb_cell(x, y, text_cells[y][x], color & 15, color >> 4);
    }
}
void console_write(const char *text) { while (*text) console_putc(*text++); }
void console_u32(u32 value)
{
    char text[10]; u32 n = 0;
    do { text[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) console_putc(text[--n]);
}
void console_i32(i32 value)
{
    if (value < 0) { console_putc('-'); console_u32(0u - (u32)value); } else console_u32((u32)value);
}
void console_hex(u32 value)
{
    console_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4) console_putc("0123456789abcdef"[(value >> shift) & 15]);
}
void console_init(void)
{
    /* Some laptop chipsets return zero rather than 0xff for absent COM1.
     * Verify register decoding before enabling optional diagnostic output. */
    serial_present = false;
    if (inb(0x3fd) != 0xff) {
        u8 saved = inb(0x3ff);
        outb(0x3ff, 0x5a);
        if (inb(0x3ff) == 0x5a) {
            outb(0x3ff, 0xa5); serial_present = inb(0x3ff) == 0xa5;
        }
        outb(0x3ff, saved);
    }
    if (serial_present) {
        outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 1); outb(0x3f9, 0);
        outb(0x3fb, 3); outb(0x3fa, 0xc7); outb(0x3fc, 0x0b);
    }
    console_clear();
}
_Noreturn void kernel_panic(const char *code, const char *reason, const char *trigger)
{
    gui_stop(); console_clear();
    console_write("KERNEL PANIC (simulation)\n"); console_write(code); console_putc('\n');
    console_write(reason); console_write("\nTrigger: "); console_write(trigger);
    console_write("\nPress Escape to restart.\n"); gfx_present();
    for (;;) { if (input_poll() == 0x1b) firmware_reset(); platform_idle(); }
}
static bool initialize_graphics(void)
{
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    if (EFI_ERROR(grape_st->BootServices->LocateProtocol(&guid, NULL, (void **)&gop))) return false;
    u32 selected = gop->Mode->Mode; u32 best = 0;
    for (u32 i = 0; i < gop->Mode->MaxMode; ++i) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL; UINTN count = 0;
        if (EFI_ERROR(gop->QueryMode(gop, i, &count, &info))) continue;
        u32 w = info->HorizontalResolution, h = info->VerticalResolution;
        if (info->PixelFormat != PixelBltOnly && w >= 640 && h >= 480 && w <= 1920 && h <= 1080 && w * h > best) {
            selected = i; best = w * h;
        }
        grape_st->BootServices->FreePool(info);
    }
    if (selected != gop->Mode->Mode && EFI_ERROR(gop->SetMode(gop, selected))) return false;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;
    if (info->PixelFormat == PixelBltOnly || !gop->Mode->FrameBufferBase) return false;
    u32 w = info->HorizontalResolution, h = info->VerticalResolution;
    if (w > 4096 || h > 4096 || info->PixelsPerScanLine < w ||
        gop->Mode->FrameBufferSize < (UINT64)info->PixelsPerScanLine * h * 4) return false;
    if (w * 480 > h * 640) { viewport_height = h; viewport_width = h * 640 / 480; }
    else { viewport_width = w; viewport_height = w * 480 / 640; }
    viewport_left = (w - viewport_width) / 2; viewport_top = (h - viewport_height) / 2;
    for (u32 x = 0; x < w; ++x)
        xmap[x] = x < viewport_left || x >= viewport_left + viewport_width ? 0xffffffffu : (x - viewport_left) * 640 / viewport_width;
    for (u32 y = 0; y < h; ++y)
        ymap[y] = y < viewport_top || y >= viewport_top + viewport_height ? 0xffffffffu : ((y - viewport_top) * 480 / viewport_height) * 640;
    for (u32 i = 0; i < 24; ++i) {
        u32 value = colors[i];
        if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
            colors[i] = (value & 0xff00) | ((value & 255) << 16) | (value >> 16);
        else if (info->PixelFormat == PixelBitMask) {
            EFI_PIXEL_BITMASK masks = info->PixelInformation;
            colors[i] = channel(value >> 16, masks.RedMask) | channel((value >> 8) & 255, masks.GreenMask) | channel(value & 255, masks.BlueMask);
        }
    }
    return true;
}
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    grape_image = image; grape_st = system;
    if (!initialize_graphics()) return EFI_UNSUPPORTED;
    console_init(); platform_init(); initialize_input();
    gfx_rect(0, 0, 640, 480, 16);
    gfx_text(224, 116, "GrapeLoad", 19);
    gfx_rect(128, 186, 384, 58, 18); gfx_text(152, 206, "GrapeOS - USB desktop", 15);
    gfx_text(104, 290, "Enter: start    F8: keyboard cursor in GUI", 7); gfx_present();
    console_serial_write("GrapeLoad Desktop - Press Enter to boot GrapeOS\n");
    while (input_poll() != '\n') platform_idle();
    console_clear();
    console_write("[ OK ] Starting GrapeOS UEFI desktop\n");
    console_write("[ OK ] GOP graphics and firmware keyboard/pointer\n");
    firmware_files_init(); store_init(); theme_load();
    console_write(firmware_files_ready() ? "[ OK ] Real USB files: /GRAPE\n" : "[WARN] USB filesystem unavailable\n");
    gfx_present();
    grape_st->BootServices->Stall(2000000);
    input_discard();
    if (!store_get()->configured) setup_begin(true);
    else { gui_start(); }
    bool previous_setup = setup_active(); u32 last_text = 0, text_blink = 0xffffffffu;
    for (;;) {
        /* Drain queued keys before rendering. Fast typing/paste must not wait
         * for one framebuffer transfer per character. Bound work per turn. */
        for (u32 count = 0; count < 64; ++count) {
            int key = input_poll();
            if (key < 0) break;
            bool was_gui = gui_active(), was_app = apps_active();
            if (gui_active()) gui_input(key);
            else if (setup_active()) setup_input(key);
            else if (apps_active()) apps_input(key);
            else shell_input(key);
            if (previous_setup && !setup_active()) gui_start();
            previous_setup = setup_active();
            if (was_gui && !gui_active()) { console_clear(); console_write("GrapeOS text shell. Type gui to return.\n"); shell_resume(); }
            if (was_app && !apps_active() && !(was_gui && !gui_active())) shell_resume();
        }
        apps_tick(); gui_tick();
        u32 now = timer_ticks(), blink = now / 50;
        if (!gui_active() && (text_dirty || blink != text_blink) && now - last_text >= 2) {
            last_text = now; text_blink = blink;
            if (caret_drawn) fb_cell(caret_col, caret_row, text_cells[caret_row][caret_col], color & 15, color >> 4);
            caret_drawn = false;
            if (cursor_visible) {
                fb_cell(console_col, console_row, text_cells[console_row][console_col], color & 15, color >> 4);
                if (!(blink & 1)) {
                    gfx_rect(console_col * 8, 54 + console_row * 16, 8, 2, 19);
                    caret_drawn = true; caret_col = console_col; caret_row = console_row;
                }
            }
            gfx_present();
            text_dirty = false;
        }
        platform_idle();
    }
}

/* GNU-EFI 4 uses _entry; older startup objects call efi_main directly. */
EFI_STATUS _entry(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    return efi_main(image, system);
}
