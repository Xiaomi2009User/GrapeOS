#include "internal.h"
#include "memory.h"
#include "firmware.h"
#include "gfx.h"
#include "photo.h"
#include "../assets/sample-image.h"

int abs(int value) { return value < 0 ? (value == (-2147483647 - 1) ? 2147483647 : -value) : value; }

/* Keep decoder allocations bounded and entirely in firmware-owned RAM. */
struct allocation { usize bytes, padding; };
static usize allocated;
static void *image_alloc(usize bytes)
{
    if (!bytes || bytes > 32 * 1024 * 1024 || allocated + bytes > 48 * 1024 * 1024) return NULL;
    struct allocation *block = NULL;
    if (EFI_ERROR(grape_st->BootServices->AllocatePool(EfiLoaderData, bytes + sizeof(*block), (void **)&block))) return NULL;
    block->bytes = bytes; allocated += bytes; return block + 1;
}
static void image_free(void *memory)
{
    if (!memory) return;
    struct allocation *block = (struct allocation *)memory - 1;
    allocated -= block->bytes; grape_st->BootServices->FreePool(block);
}
static void *image_resize(void *memory, usize bytes)
{
    if (!memory) return image_alloc(bytes);
    void *next = image_alloc(bytes); if (!next) return NULL;
    usize previous = ((struct allocation *)memory - 1)->bytes;
    memcpy(next, memory, previous < bytes ? previous : bytes); image_free(memory); return next;
}
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 4096
#define STBI_ASSERT(x) ((void)sizeof(x))
#define STBI_MALLOC(n) image_alloc(n)
#define STBI_REALLOC(p,n) image_resize(p,n)
#define STBI_FREE(p) image_free(p)
#include "../third_party/stb/stb_image.h"

static struct photo_file catalog[32];
static u32 count, width, height;
static u8 *picture, *art;
static int art_width, art_height;
static bool same(const char *a, const char *b)
{
    while (*a && *b) { char x = *a++, y = *b++; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return false; }
    return *a == *b;
}
void photo_refresh(void)
{
    count = firmware_photo_list(catalog, 31);
    bool sample = false;
    for (u32 i = 0; i < count; ++i) if (same(catalog[i].name, "images.png")) sample = true;
    if (!sample) { memcpy(catalog[count].name, "images.png", 11); catalog[count++].bytes = sizeof(sample_png); }
    if (!art) { int channels; art = stbi_load_from_memory(sample_png, sizeof(sample_png), &art_width, &art_height, &channels, 4); }
}
u32 photo_count(void) { return count; }
const char *photo_name(u32 index) { return index < count ? catalog[index].name : ""; }
const char *photo_open(u32 index)
{
    if (index >= count) return "No photo selected";
    usize length = 0;
    u8 *data = image_alloc(4 * 1024 * 1024);
    if (!data) return "Not enough memory to open the photo";
    bool read = firmware_file_read(catalog[index].name, data, 4 * 1024 * 1024, &length);
    const u8 *source = data;
    if (!read && same(catalog[index].name, "images.png")) { source = sample_png; length = sizeof(sample_png); read = true; }
    if (!read) { image_free(data); return "Cannot read photo from USB"; }
    int w, h, channels;
    if (!stbi_info_from_memory(source, (int)length, &w, &h, &channels) || w < 1 || h < 1 || w > 4096 || h > 4096 || (usize)w * h > 4 * 1024 * 1024) {
        image_free(data); return "Unsupported or damaged photo (max 4 megapixels)";
    }
    u8 *next = stbi_load_from_memory(source, (int)length, &w, &h, &channels, 4);
    image_free(data);
    if (!next) return "Cannot decode photo or memory limit reached";
    image_free(picture); picture = next; width = (u32)w; height = (u32)h;
    return NULL;
}
u32 photo_width(void) { return width; }
u32 photo_height(void) { return height; }
static void fit(u32 x, u32 y, u32 max_width, u32 max_height, const u8 *pixels, u32 w, u32 h)
{
    if (!pixels || !w || !h) return;
    u32 draw_width = max_width, draw_height = max_width * h / w;
    if (draw_height > max_height) { draw_height = max_height; draw_width = max_height * w / h; }
    if (!draw_width) draw_width = 1;
    if (!draw_height) draw_height = 1;
    gfx_rgba(x + (max_width - draw_width) / 2, y + (max_height - draw_height) / 2, draw_width, draw_height, pixels, w, h);
}
void photo_draw(u32 x, u32 y, u32 w, u32 h) { fit(x, y, w, h, picture, width, height); }
void photo_art(u32 x, u32 y, u32 w, u32 h) { fit(x, y, w, h, art, (u32)art_width, (u32)art_height); }
