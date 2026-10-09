#ifndef GRAPE_RAMFS_H
#define GRAPE_RAMFS_H
#include "types.h"
#ifdef GRAPE_FIRMWARE
#define RAMFS_FILES 32u
#define RAMFS_TEXT 4096u
#else
#define RAMFS_FILES 8u
#define RAMFS_TEXT 256u
#endif
#define RAMFS_NAME 24u
bool ramfs_persistent(void);
struct ramfile { char name[RAMFS_NAME]; char text[RAMFS_TEXT]; };
const struct ramfile *ramfs_at(u32 index);
const struct ramfile *ramfs_find(const char *name);
const char *ramfs_write(const char *name, const char *text);
const char *ramfs_remove(const char *name);
const char *ramfs_copy(const char *source, const char *destination, bool move);
#endif
