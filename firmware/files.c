#include "internal.h"
#include "memory.h"
#include "ramfs.h"
#include "store.h"
#include "setup.h"
#include "photo.h"

EFI_FILE_HANDLE grape_folder;
static EFI_GUID info_guid = EFI_FILE_INFO_ID;
static struct ramfile files[RAMFS_FILES];
static struct store_state profile;
static u32 profile_generation;
static bool profile_slot;
static bool equal(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return *a == *b;
}
static usize size(const char *s) { usize n = 0; while (s[n]) ++n; return n; }
static void copy(char *to, const char *from) { do { *to++ = *from; } while (*from++); }
static void wide(CHAR16 out[64], const char *in)
{
    usize n = 0; while (in[n] && n < 63) { out[n] = (u8)in[n]; ++n; } out[n] = 0;
}
static const char *valid(const char *name)
{
    if (!*name || size(name) >= RAMFS_NAME) return "name must have 1..23 characters";
    if (name[0] == '.') return "filename must not begin with a dot";
    for (const char *p = name; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-'))
            return "use letters, digits, dot, dash or underscore";
    static const char *const reserved[] = {"welcome.txt", "commands.txt", "profile.txt", "catalog.txt", "note.txt"};
    for (u32 i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
        if (equal(name, reserved[i])) return "built-in file is protected; edit note.txt in Notepad";
    return NULL;
}
static EFI_FILE_HANDLE open_file(const char *name, UINT64 mode)
{
    if (!grape_folder) return NULL;
    CHAR16 path[64]; wide(path, name);
    EFI_FILE_HANDLE file = NULL;
    EFI_STATUS status = grape_folder->Open(grape_folder, &file, path, mode, 0);
    return EFI_ERROR(status) ? NULL : file;
}
static bool remove_file(const char *name)
{
    EFI_FILE_HANDLE file = open_file(name, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE);
    if (!file) return false;
    /* Delete closes its handle on success and failure. */
    return file->Delete(file) == EFI_SUCCESS;
}
static bool exists(const char *name)
{
    EFI_FILE_HANDLE file = open_file(name, EFI_FILE_MODE_READ);
    if (!file) return false;
    file->Close(file); return true;
}
static bool rename_file(const char *old, const char *next)
{
    EFI_FILE_HANDLE file = open_file(old, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE);
    if (!file) return false;
    UINT64 info_buffer[128]; UINTN count = sizeof(info_buffer);
    EFI_STATUS status = file->GetInfo(file, &info_guid, &count, info_buffer);
    if (!EFI_ERROR(status)) {
        EFI_FILE_INFO *info = (EFI_FILE_INFO *)info_buffer;
        CHAR16 path[64]; wide(path, next);
        count = SIZE_OF_EFI_FILE_INFO + (size(next) + 1) * sizeof(CHAR16);
        info->Size = count;
        memcpy(info->FileName, path, (size(next) + 1) * sizeof(CHAR16));
        status = file->SetInfo(file, &info_guid, count, info);
        if (!EFI_ERROR(status)) status = file->Flush(file);
    }
    file->Close(file); return !EFI_ERROR(status);
}
bool firmware_file_read(const char *name, void *data, usize capacity, usize *length)
{
    EFI_FILE_HANDLE file = open_file(name, EFI_FILE_MODE_READ);
    if (!file) return false;
    UINT64 buffer[128]; UINTN info_size = sizeof(buffer);
    EFI_STATUS status = file->GetInfo(file, &info_guid, &info_size, buffer);
    EFI_FILE_INFO *info = (EFI_FILE_INFO *)buffer;
    UINTN count = 0;
    if (!EFI_ERROR(status) && !(info->Attribute & EFI_FILE_DIRECTORY) && info->FileSize <= capacity) {
        count = info->FileSize;
        UINTN expected = count;
        status = file->Read(file, &count, data);
        if (!EFI_ERROR(status) && count != expected) status = EFI_DEVICE_ERROR;
    } else status = EFI_BAD_BUFFER_SIZE;
    file->Close(file);
    if (EFI_ERROR(status)) return false;
    *length = count; return true;
}
static bool raw_write(const char *name, const void *data, usize length)
{
    EFI_FILE_HANDLE file = open_file(name, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE);
    if (!file) return false;
    UINT64 info_buffer[128]; UINTN count = sizeof(info_buffer);
    EFI_STATUS status = file->GetInfo(file, &info_guid, &count, info_buffer);
    if (!EFI_ERROR(status)) {
        EFI_FILE_INFO *info = (EFI_FILE_INFO *)info_buffer;
        info->FileSize = length;
        status = file->SetInfo(file, &info_guid, count, info);
    }
    if (!EFI_ERROR(status)) status = file->SetPosition(file, 0);
    count = length;
    if (!EFI_ERROR(status)) status = file->Write(file, &count, (void *)data);
    if (!EFI_ERROR(status) && count != length) status = EFI_DEVICE_ERROR;
    if (!EFI_ERROR(status)) status = file->Flush(file);
    file->Close(file);
    return !EFI_ERROR(status);
}
/* Each file has independent temporary/backup names. Flush new contents before
 * renaming; retain the old version until the replacement is committed. */
bool firmware_file_write(const char *name, const void *data, usize length)
{
    if (!grape_folder || size(name) > 40 || length > RAMFS_TEXT + 256u) return false;
    char temporary[64], backup[64];
    copy(temporary, name); copy(temporary + size(name), ".~new");
    copy(backup, name); copy(backup + size(name), ".~bak");
    if (exists(backup)) {
        if (!exists(name)) { if (!rename_file(backup, name)) return false; }
        else if (!remove_file(backup)) return false;
    }
    if (!raw_write(temporary, data, length)) return false;
    static u8 verify[RAMFS_TEXT + 256]; usize read = 0;
    if (!firmware_file_read(temporary, verify, sizeof(verify), &read) || read != length || memcmp(verify, data, length))
        return false;
    bool old = exists(name);
    if (old && !rename_file(name, backup)) return false;
    if (!rename_file(temporary, name)) {
        if (old) (void)rename_file(backup, name);
        return false;
    }
    if (old) (void)remove_file(backup);
    return true;
}
static void recover(void)
{
    /* Collect names before changing the directory being enumerated. */
    char pending[64][64]; u32 used = 0;
    grape_folder->SetPosition(grape_folder, 0);
    for (u32 attempts = 0; attempts < 256; ++attempts) {
        UINT64 buffer[128]; UINTN count = sizeof(buffer);
        EFI_STATUS status = grape_folder->Read(grape_folder, &count, buffer);
        if (EFI_ERROR(status) || !count) break;
        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buffer;
        char name[64]; u32 n = 0;
        while (n < 63 && info->FileName[n] && info->FileName[n] < 128) { name[n] = (char)info->FileName[n]; ++n; }
        name[n] = 0;
        if (n > 5 && n < 63 && equal(name + n - 5, ".~bak") && used < 64) copy(pending[used++], name);
    }
    for (u32 i = 0; i < used; ++i) {
        char original[64]; copy(original, pending[i]); original[size(original) - 5] = 0;
        if (!exists(original)) (void)rename_file(pending[i], original);
        else (void)remove_file(pending[i]);
    }
    grape_folder->SetPosition(grape_folder, 0);
}
void firmware_files_init(void)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID, fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *image = NULL; EFI_DEVICE_PATH *path = NULL;
    if (EFI_ERROR(grape_st->BootServices->HandleProtocol(grape_image, &loaded_guid, (void **)&image))) return;
    if (EFI_ERROR(grape_st->BootServices->HandleProtocol(image->DeviceHandle, &path_guid, (void **)&path))) return;
    bool usb = false;
    for (u32 scanned = 0; scanned < 4096;) {
        u32 length = path->Length[0] | ((u32)path->Length[1] << 8);
        if (length < 4 || length > 4096 - scanned) return;
        if (path->Type == 0x7f) break;
        if (path->Type == 3 && (path->SubType == 5 || path->SubType == 15 || path->SubType == 16)) usb = true;
        scanned += length; path = (EFI_DEVICE_PATH *)((u8 *)path + length);
    }
    if (!usb) return; /* Never open an internal ESP for writing. */
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL; EFI_FILE_HANDLE root = NULL;
    if (EFI_ERROR(grape_st->BootServices->HandleProtocol(image->DeviceHandle, &fs_guid, (void **)&fs))) return;
    if (EFI_ERROR(fs->OpenVolume(fs, &root))) return;
    EFI_STATUS status = root->Open(root, &grape_folder, L"GRAPE", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
    root->Close(root);
    if (EFI_ERROR(status)) { grape_folder = NULL; return; }
    recover();
}
bool firmware_files_ready(void) { return grape_folder != NULL; }
u32 firmware_photo_list(struct photo_file *out, u32 capacity)
{
    if (!grape_folder) return 0;
    grape_folder->SetPosition(grape_folder, 0); u32 used = 0;
    for (u32 attempts = 0; attempts < 256 && used < capacity; ++attempts) {
        UINT64 buffer[128]; UINTN count = sizeof(buffer);
        if (EFI_ERROR(grape_folder->Read(grape_folder, &count, buffer)) || !count) break;
        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buffer;
        if (info->Attribute & EFI_FILE_DIRECTORY || info->FileSize > 4 * 1024 * 1024) continue;
        char name[24]; u32 n = 0;
        while (n < 23 && info->FileName[n] && info->FileName[n] < 128) { name[n] = (char)info->FileName[n]; ++n; }
        if (info->FileName[n] || n < 5) continue;
        name[n] = 0;
        if (valid(name)) continue;
        bool supported = equal(name + n - 4, ".png") || equal(name + n - 4, ".jpg") || equal(name + n - 4, ".bmp") ||
                         (n >= 6 && equal(name + n - 5, ".jpeg"));
        if (!supported) continue;
        copy(out[used].name, name); out[used++].bytes = (u32)info->FileSize;
    }
    return used;
}
bool ramfs_persistent(void) { return firmware_files_ready(); }
static void reload(void)
{
    memset(files, 0, sizeof(files));
    if (!grape_folder) return;
    grape_folder->SetPosition(grape_folder, 0); u32 slot = 0;
    for (u32 attempts = 0; attempts < 256 && slot < RAMFS_FILES; ++attempts) {
        UINT64 buffer[128]; UINTN count = sizeof(buffer);
        if (EFI_ERROR(grape_folder->Read(grape_folder, &count, buffer)) || !count) break;
        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buffer;
        if ((info->Attribute & EFI_FILE_DIRECTORY) || info->FileSize >= RAMFS_TEXT) continue;
        char name[RAMFS_NAME]; u32 n = 0;
        while (n < RAMFS_NAME - 1 && info->FileName[n] && info->FileName[n] < 128) { name[n] = (char)info->FileName[n]; ++n; }
        if (info->FileName[n]) continue;
        name[n] = 0; if (valid(name)) continue;
        usize length = 0;
        if (!firmware_file_read(name, files[slot].text, RAMFS_TEXT - 1, &length)) continue;
        bool text = true;
        for (usize i = 0; i < length; ++i) {
            u8 c = (u8)files[slot].text[i];
            if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c > 126) { text = false; break; }
        }
        if (!text) { files[slot].text[0] = 0; continue; }
        files[slot].text[length] = 0; copy(files[slot].name, name); ++slot;
    }
}
const struct ramfile *ramfs_at(u32 index) { return index < RAMFS_FILES ? &files[index] : NULL; }
const struct ramfile *ramfs_find(const char *name)
{
    for (u32 i = 0; i < RAMFS_FILES; ++i) if (*files[i].name && equal(files[i].name, name)) return &files[i];
    return NULL;
}
const char *ramfs_write(const char *name, const char *text)
{
    const char *error = valid(name); if (error) return error;
    if (size(text) >= RAMFS_TEXT) return "maximum file size is 4095 bytes";
    if (!ramfs_find(name)) {
        u32 i = 0; while (i < RAMFS_FILES && *files[i].name) ++i;
        if (i == RAMFS_FILES) return "folder is full (32 editable files)";
        if (exists(name)) return "existing file cannot be edited (size or format)";
    }
    /* Copy before reload invalidates any cache-backed input pointer. */
    static char content[RAMFS_TEXT]; copy(content, text);
    if (!firmware_file_write(name, content, size(content))) return "USB save failed; check the flash drive";
    reload(); return NULL;
}
const char *ramfs_remove(const char *name)
{
    const char *error = valid(name); if (error) return error;
    if (!ramfs_find(name)) return "no such file";
    if (!remove_file(name)) return "USB delete failed";
    reload(); return NULL;
}
const char *ramfs_copy(const char *source, const char *destination, bool move)
{
    const char *error = valid(destination); if (error) return error;
    const struct ramfile *file = ramfs_find(source); if (!file) return "no such file";
    if (equal(source, destination)) return NULL;
    if (exists(destination)) return "destination already exists";
    if (move) {
        if (!rename_file(source, destination)) return "USB rename failed";
        reload(); return NULL;
    }
    return ramfs_write(destination, file->text);
}
struct profile_record { u32 magic, generation; struct store_state state; u32 checksum; };
static u32 hash(const void *data, usize length)
{
    const u8 *p = data; u32 h = 2166136261u;
    for (usize i = 0; i < length; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}
static bool valid_profile(const struct store_state *state)
{
    if (state->configured > 1 || state->foreground > 15 || state->background > 7 ||
        state->installed_apps & ~7u || state->username[23] || state->hostname[23] || state->note[4095]) return false;
    return setup_valid_username(state->username) && setup_valid_hostname(state->hostname);
}
void store_init(void)
{
    memset(&profile, 0, sizeof(profile)); copy(profile.username, "grape"); copy(profile.hostname, "grapeos");
    profile.foreground = 13;
    for (u32 i = 0; i < 2; ++i) {
        struct profile_record record; usize count;
        if (!firmware_file_read(i ? ".profile-b" : ".profile-a", &record, sizeof(record), &count) || count != sizeof(record)) continue;
        if (record.magic != 0x47524632 || hash(&record, sizeof(record) - 4) != record.checksum || !valid_profile(&record.state)) continue;
        if (!profile_generation || (i32)(record.generation - profile_generation) > 0) {
            profile = record.state; profile_generation = record.generation; profile_slot = i;
        }
    }
    usize count;
    if (firmware_file_read("note.txt", profile.note, sizeof(profile.note) - 1, &count)) profile.note[count] = 0;
    reload();
}
const struct store_state *store_get(void) { return &profile; }
bool store_available(void) { return firmware_files_ready(); }
bool store_save(const struct store_state *candidate)
{
    if (!candidate || !valid_profile(candidate)) return false;
    if (!grape_folder) { profile = *candidate; return false; }
    struct profile_record record; memset(&record, 0, sizeof(record));
    record.magic = 0x47524632; record.generation = profile_generation + 1; record.state = *candidate;
    record.checksum = hash(&record, sizeof(record) - 4);
    if (!firmware_file_write(profile_slot ? ".profile-a" : ".profile-b", &record, sizeof(record))) return false;
    if (memcmp(profile.note, candidate->note, sizeof(profile.note)) &&
        !firmware_file_write("note.txt", candidate->note, size(candidate->note))) return false;
    profile = *candidate; profile_generation = record.generation; profile_slot = !profile_slot;
    return true;
}
