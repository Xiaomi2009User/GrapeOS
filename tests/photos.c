#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../firmware/photos.c"
EFI_SYSTEM_TABLE *grape_st;
EFI_HANDLE grape_image;
static EFI_STATUS EFIAPI pool(EFI_MEMORY_TYPE type, UINTN bytes, void **out)
{ (void)type; *out = malloc(bytes); return *out ? EFI_SUCCESS : EFI_OUT_OF_RESOURCES; }
static EFI_STATUS EFIAPI release(void *memory) { free(memory); return EFI_SUCCESS; }
u32 firmware_photo_list(struct photo_file *out, u32 capacity)
{
    static const char *names[] = {"images.png", "photo.jpg", "photo.bmp", "broken.png"};
    assert(capacity >= 4);
    for (u32 i = 0; i < 4; ++i) { strcpy(out[i].name, names[i]); out[i].bytes = 1; }
    return 4;
}
bool firmware_file_read(const char *name, void *out, usize capacity, usize *length)
{
    char path[128]; snprintf(path, sizeof(path), "%s/%s", same(name, "images.png") ? "assets" : "tests/fixtures", name);
    FILE *file = fopen(path, "rb");
    if (!file) { if (same(name, "broken.png")) { memcpy(out, "bad!", 4); *length = 4; return true; } return false; }
    *length = fread(out, 1, capacity, file); fclose(file); return true;
}
void gfx_rgba(u32 x, u32 y, u32 w, u32 h, const u8 *rgba, u32 sw, u32 sh)
{ (void)x; (void)y; assert(w && h && rgba && sw && sh); }
int main(void)
{
    EFI_BOOT_SERVICES services = {.AllocatePool = pool, .FreePool = release};
    EFI_SYSTEM_TABLE table = {.BootServices = &services}; grape_st = &table;
    photo_refresh(); assert(photo_count() == 4 && art);
    for (u32 i = 0; i < 3; ++i) {
        assert(!photo_open(i)); assert(photo_width() && photo_height());
        assert(picture[3] == 255); photo_draw(0, 0, 340, 236);
    }
    u8 *previous = picture; usize before = allocated;
    assert(photo_open(3)); assert(picture == previous && allocated == before);
    assert(photo_open(99)); assert(allocated == before);
    assert(!image_alloc(49 * 1024 * 1024));
    image_free(picture); image_free(art); assert(allocated == 0);
    puts("PASS: real PNG/JPEG/BMP decoding, malformed input, preserved previous image, allocation bounds and cleanup.");
}
