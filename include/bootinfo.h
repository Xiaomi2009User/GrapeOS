#ifndef GRAPE_BOOTINFO_H
#define GRAPE_BOOTINFO_H

/* Eight bytes below the boot sector, outside the kernel and BIOS data area.
 * Shared by the real-mode loader and C; do not add C declarations here.
 */
#define BOOT_INFO_ADDRESS 0x7000
#define BOOT_INFO_MAGIC 0x47525042
#define BOOT_INFO_LIVE_USB 1

#endif
