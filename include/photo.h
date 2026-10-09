#ifndef GRAPE_PHOTO_H
#define GRAPE_PHOTO_H
#include "types.h"
struct photo_file { char name[24]; u32 bytes; };
u32 firmware_photo_list(struct photo_file *out, u32 capacity);
void photo_refresh(void);
u32 photo_count(void);
const char *photo_name(u32 index);
const char *photo_open(u32 index);
u32 photo_width(void);
u32 photo_height(void);
void photo_draw(u32 x, u32 y, u32 width, u32 height);
void photo_art(u32 x, u32 y, u32 width, u32 height);
#endif
