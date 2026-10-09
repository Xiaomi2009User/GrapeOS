#include "ramfs.h"
static struct ramfile files[RAMFS_FILES];
bool ramfs_persistent(void) { return false; }
static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static u32 size(const char *s) { u32 n = 0; while (s[n]) ++n; return n; }
static void copy(char *to, const char *from) { do { *to++ = *from; } while (*from++); }
static const char *valid(const char *name)
{
    if (!*name || size(name) >= RAMFS_NAME) return "name must have 1..23 characters";
    if (equal(name, ".") || equal(name, "..")) return "invalid filename";
    for (const char *p = name; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-'))
            return "use letters, digits, dot, dash or underscore";
    static const char *const reserved[] = {
        "welcome.txt", "commands.txt", "profile.txt", "catalog.txt", "note.txt"
    };
    for (u32 i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
        if (equal(name, reserved[i])) return "built-in file is protected; edit note.txt in Notepad";
    return NULL;
}
const struct ramfile *ramfs_at(u32 index) { return index < RAMFS_FILES ? &files[index] : NULL; }
const struct ramfile *ramfs_find(const char *name)
{
    for (u32 i = 0; i < RAMFS_FILES; ++i)
        if (*files[i].name && equal(files[i].name, name)) return &files[i];
    return NULL;
}
const char *ramfs_write(const char *name, const char *text)
{
    const char *error = valid(name);
    if (error) return error;
    if (size(text) >= RAMFS_TEXT) return "maximum file size is 255 characters";
    struct ramfile *target = (struct ramfile *)ramfs_find(name);
    if (!target)
        for (u32 i = 0; i < RAMFS_FILES; ++i)
            if (!*files[i].name) { target = &files[i]; break; }
    if (!target) return "RAM folder is full (8 files)";
    copy(target->name, name);
    copy(target->text, text);
    return NULL;
}
const char *ramfs_remove(const char *name)
{
    const char *error = valid(name);
    if (error) return error;
    struct ramfile *target = (struct ramfile *)ramfs_find(name);
    if (!target) return "no such RAM file";
    target->name[0] = target->text[0] = 0;
    return NULL;
}
const char *ramfs_copy(const char *source, const char *destination, bool move)
{
    const struct ramfile *file = ramfs_find(source);
    if (!file) return "no such RAM file";
    if (equal(source, destination)) return NULL;
    if (ramfs_find(destination)) return "destination already exists";
    const char *error = valid(destination);
    if (error) return error;
    if (move) copy(((struct ramfile *)file)->name, destination);
    else error = ramfs_write(destination, file->text);
    return error;
}
