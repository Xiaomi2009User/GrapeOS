/* Compare partial presentation with an independent complete-screen renderer.
 * Include the backend to exercise its real scaling and tile comparison. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../firmware/runtime.c"

static void reference(u32 *out, u32 width, u32 height, u32 pitch)
{
    for (u32 y = 0; y < height; ++y)
        for (u32 x = 0; x < width; ++x) {
            u32 value = colors[16];
            if (x >= viewport_left && x < viewport_left + viewport_width &&
                y >= viewport_top && y < viewport_top + viewport_height) {
                u32 cx = (x - viewport_left) * 640 / viewport_width;
                u32 cy = (y - viewport_top) * 480 / viewport_height;
                value = canvas[cy * 640 + cx];
            }
            out[y * pitch + x] = value;
        }
}
static void run(u32 width, u32 height, u32 padding)
{
    u32 pitch = width + padding;
    usize bytes = (usize)pitch * height * sizeof(u32);
    u32 *screen = malloc(bytes), *expected = malloc(bytes);
    assert(screen && expected);
    memset(screen, 0x7b, bytes); memset(expected, 0x7b, bytes);
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info = {.HorizontalResolution = width,
        .VerticalResolution = height, .PixelsPerScanLine = pitch};
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode = {.Info = &info,
        .FrameBufferBase = (EFI_PHYSICAL_ADDRESS)(usize)screen};
    EFI_GRAPHICS_OUTPUT_PROTOCOL protocol = {.Mode = &mode}; gop = &protocol;
    if (width * 480 > height * 640) { viewport_height = height; viewport_width = height * 640 / 480; }
    else { viewport_width = width; viewport_height = width * 480 / 640; }
    viewport_left = (width - viewport_width) / 2; viewport_top = (height - viewport_height) / 2;
    for (u32 x = 0; x < width; ++x)
        xmap[x] = x < viewport_left || x >= viewport_left + viewport_width ? 0xffffffffu : (x - viewport_left) * 640 / viewport_width;
    for (u32 y = 0; y < height; ++y)
        ymap[y] = y < viewport_top || y >= viewport_top + viewport_height ? 0xffffffffu : ((y - viewport_top) * 480 / viewport_height) * 640;
    for (u32 i = 0; i < 640 * 480; ++i) canvas[i] = i * 13;
    frame_valid = false; gfx_present(); reference(expected, width, height, pitch);
    assert(!memcmp(screen, expected, bytes));

    /* A sentinel proves that an identical frame writes no aperture pixels. */
    memset(screen, 0x7b, bytes); gfx_present();
    for (usize i = 0; i < bytes / sizeof(u32); ++i) assert(screen[i] == 0x7b7b7b7bu);
    canvas[240 * 640 + 320] ^= 0x12345; gfx_present();
    usize touched = 0;
    for (usize i = 0; i < bytes / sizeof(u32); ++i) touched += screen[i] != 0x7b7b7b7bu;
    assert(touched > 0 && touched < (usize)width * height / 100);
    printf("PASS: %ux%u stride %u; a changed cursor tile writes %lu of %u pixels\n",
           width, height, pitch, touched, width * height);
    reference(expected, width, height, pitch); memcpy(screen, expected, bytes);
    /* Disjoint damage, tile boundaries, clipping, and restoring old pixels. */
    for (u32 frame = 0; frame < 12; ++frame) {
        u32 x = frame * 53, y = frame * 41;
        gfx_rect(x, y, 23, 28, frame % 24);
        gfx_char(x, y, 'G', (frame + 5) % 24);
        gfx_rect(639 - x, 479 - y, 37, 19, (frame + 2) % 24);
        gfx_present(); reference(expected, width, height, pitch);
        assert(!memcmp(screen, expected, bytes));
    }
    free(screen); free(expected);
}
int main(void)
{
    const char *letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@#$%";
    for (const char *letter = letters; *letter; ++letter) {
        gfx_terminal_char(10, 10, *letter, 15, 0);
        u32 foreground = 0;
        for (u32 y = 10; y < 22; ++y)
            for (u32 x = 10; x < 17; ++x) {
                u32 pixel = canvas[y * 640 + x];
                assert(pixel == colors[15] || pixel == colors[0]);
                foreground += pixel == colors[15];
                if (x == 16) assert(pixel == colors[0]);
            }
        assert(foreground > 0);
    }
    puts("PASS: native terminal glyphs have full contrast, no blended shades, and a clear spacing column.");
    run(640, 480, 0); run(800, 600, 9); run(1024, 768, 0);
    run(1920, 1080, 16); run(800, 800, 0);
    puts("PASS: initial/full/partial output, unchanged frames, fractional scaling and pitch padding.");
}
