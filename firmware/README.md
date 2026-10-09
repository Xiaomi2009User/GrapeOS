# GrapeOS desktop through UEFI services

This optional x86_64 build keeps UEFI boot services active. It shares the
GrapeOS GUI, shell, setup and games with the i386 kernel but uses firmware GOP,
keyboard/pointer protocols and Simple File System for the boot USB volume.
It does not replace the original Multiboot2 bare metal build.

Only the loaded image's USB device handle may be opened for writing. User
files are regular FAT text files in `GRAPE`, readable by other operating
systems. No filesystem on an internal disk is enumerated or written.

GNU-EFI supplies headers and relocation startup; these are installed locally
under `.runtime/qemu` in this workspace. On another machine install GNU-EFI
or set EFI_ROOT to its prefix. The font is the existing OFL Terminus font.
