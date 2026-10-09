#ifndef GRAPE_EFI_INTERNAL_H
#define GRAPE_EFI_INTERNAL_H
#include <efi.h>
#include <eficonex.h>
#include <efipoint.h>
#include "firmware.h"
extern EFI_SYSTEM_TABLE *grape_st;
extern EFI_HANDLE grape_image;
extern EFI_FILE_HANDLE grape_folder;
#endif
