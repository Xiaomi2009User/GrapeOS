#ifndef GRAPE_FIRMWARE_H
#define GRAPE_FIRMWARE_H
#include "types.h"
_Noreturn void firmware_reset(void);
bool firmware_files_ready(void);
bool firmware_file_read(const char *name, void *data, usize capacity, usize *length);
bool firmware_file_write(const char *name, const void *data, usize length);
void firmware_files_init(void);
/* BIOS/UEFI clock as configured by the computer; no timezone conversion. */
bool firmware_time_text(char date[11], char time[9]);
const char *firmware_theme_name(void);
const char *firmware_theme_set(const char *name);
const char *firmware_theme_next(void);
#endif
