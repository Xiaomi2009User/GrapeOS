#!/usr/bin/env python3
"""Create a 64 MiB GPT USB image with FAT32 ESP and EFI/BOOT/BOOTX64.EFI."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import tempfile
import uuid
import zlib

SECTOR = 512
SECTORS = 131072
START = 2048
LAST = SECTORS - 34
TOTAL = LAST - START + 1
LABEL = b"GRAPEUEFI  "
README = b"GrapeOS UEFI x64 live USB\r\nSecure Boot must be disabled. Use the built-in laptop keyboard.\r\nSelect GrapeOS, finish setup, then type gui.\r\nCtrl+N: Notepad, Ctrl+T: Terminal, Escape: back.\r\nSettings and notes last until reboot. Internal disks are not used.\r\nUEFI loader backend: GNU GRUB; kernel: GrapeOS.\r\n"
DESKTOP_README = b"GrapeOS Desktop 0.3 - UEFI firmware services\r\nSecure Boot must be disabled. Enter in GrapeLoad starts setup/desktop.\r\nF8: keyboard cursor. Arrows: move. Enter/Space: click. F8: exit cursor mode.\r\nCtrl+F: Files. Ctrl+A: new file. Ctrl+R: rename. Enter: edit. Ctrl+S: save.\r\nCtrl+P: Photos. Copy PNG/JPG/BMP to GRAPE; R refreshes the photo list.\r\nCtrl+G: Snake. Ctrl+E: neofetch. Games also work in the GUI Terminal.\r\nTheme button / Ctrl+Y: grape, mint, midnight (saved on USB).\r\nTerminal: date (BIOS/UEFI time), theme [grape|mint|midnight].\r\nReal text files and profile are saved in GRAPE on this USB.\r\nUp to 32 text files, 4095 bytes each, names up to 23 ASCII characters.\r\nMouse/touchpad support depends on firmware pointer protocols.\r\nOnly the boot USB is opened for writing; internal disks are not used.\r\n"

def gpt_header(current, backup, entries_lba, entries_crc):
    header = bytearray(SECTOR)
    struct.pack_into("<8sIIIIQQQQ16sQIII", header, 0, b"EFI PART", 0x10000,
                     92, 0, 0, current, backup, 34, LAST,
                     uuid.UUID("948f7c42-c323-4454-b648-0210cbd9571a").bytes_le,
                     entries_lba, 128, 128, entries_crc)
    struct.pack_into("<I", header, 16, zlib.crc32(header[:92]))
    return header

def directory_entry(name, attribute, cluster, length=0):
    entry = bytearray(32)
    entry[:11] = name
    entry[11] = attribute
    struct.pack_into("<H", entry, 20, cluster >> 16)
    struct.pack_into("<HHI", entry, 24, 33, cluster & 65535, length)
    return entry

def assemble(efi, desktop=False):
    readme_content = DESKTOP_README if desktop else README
    if efi[:2] != b"MZ" or len(efi) > 32 * 1024 * 1024:
        raise ValueError("EFI executable is missing or too large")
    pe = struct.unpack_from("<I", efi, 60)[0]
    if efi[pe:pe+4] != b"PE\0\0" or struct.unpack_from("<H", efi, pe+4)[0] != 0x8664:
        raise ValueError("BOOTX64.EFI must be a PE x86_64 image")
    disk = bytearray(SECTORS * SECTOR)
    disk[446:462] = b"\0\0\x02\0\xee\xff\xff\xff" + struct.pack("<II", 1, SECTORS - 1)
    disk[510:512] = b"\x55\xaa"
    entries = bytearray(128 * 128)
    struct.pack_into("<16s16sQQQ", entries, 0,
                     uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b").bytes_le,
                     uuid.UUID("e65073bb-8f26-4114-91d4-30d811832b17").bytes_le,
                     START, LAST, 0)
    name = "GrapeOS UEFI".encode("utf-16le")
    entries[56:56+len(name)] = name
    crc = zlib.crc32(entries)
    disk[SECTOR:2*SECTOR] = gpt_header(1, SECTORS - 1, 2, crc)
    disk[2*SECTOR:34*SECTOR] = entries
    backup_entries = (SECTORS - 33) * SECTOR
    disk[backup_entries:backup_entries+len(entries)] = entries
    disk[-SECTOR:] = gpt_header(SECTORS - 1, 1, SECTORS - 33, crc)

    fat_sectors = 1
    while True:
        clusters = TOTAL - 32 - 2 * fat_sectors
        required = ((clusters + 2) * 4 + SECTOR - 1) // SECTOR
        if required <= fat_sectors: break
        fat_sectors = required
    if clusters < 65525: raise ValueError("ESP is too small for FAT32")
    base = START * SECTOR
    boot = bytearray(SECTOR)
    boot[:11] = b"\xeb\x58\x90GRAPEOS "
    struct.pack_into("<HBHBHHBHHHII", boot, 11,
                     SECTOR, 1, 32, 2, 0, 0, 0xf8, 0, 63, 255, START, TOTAL)
    struct.pack_into("<IHHIHH", boot, 36, fat_sectors, 0, 0, 2, 1, 6)
    boot[64:67] = b"\x80\x00\x29"
    struct.pack_into("<I", boot, 67, 0x47525032)
    boot[71:82] = LABEL
    boot[82:90] = b"FAT32   "
    boot[90:94] = b"\xfa\xf4\xeb\xfd"
    boot[510:] = b"\x55\xaa"
    disk[base:base+SECTOR] = boot
    disk[base+6*SECTOR:base+7*SECTOR] = boot
    fat = bytearray(fat_sectors * SECTOR)
    def fat_set(cluster, value): struct.pack_into("<I", fat, cluster * 4, value)
    fat_set(0, 0x0ffffff8)
    fat_set(1, 0x0fffffff)
    for cluster in (2, 3, 4): fat_set(cluster, 0x0fffffff)
    data_start = base + (32 + 2 * fat_sectors) * SECTOR
    def cluster_offset(cluster): return data_start + (cluster - 2) * SECTOR
    next_free = 5
    def allocate(contents):
        nonlocal next_free
        first = next_free
        count = (len(contents) + SECTOR - 1) // SECTOR
        if next_free + count > clusters + 2: raise ValueError("ESP contents do not fit")
        for cluster in range(first, first + count):
            fat_set(cluster, cluster + 1 if cluster + 1 < first + count else 0x0fffffff)
        position = cluster_offset(first)
        disk[position:position+len(contents)] = contents
        next_free += count
        return first
    executable = allocate(efi)
    readme = allocate(readme_content)
    def write_directory(cluster, entries):
        position = cluster_offset(cluster)
        contents = b"".join(entries)
        disk[position:position+len(contents)] = contents
    root_entries = [directory_entry(LABEL, 8, 0),
                        directory_entry(b"EFI        ", 16, 3),
                        directory_entry(b"README  TXT", 32, readme, len(readme_content))]
    if desktop:
        photo = (Path(__file__).resolve().parents[1] / "assets/images.png").read_bytes()
        folder = allocate(bytes(SECTOR)); picture = allocate(photo)
        root_entries.append(directory_entry(b"GRAPE      ", 16, folder))
        write_directory(folder, [directory_entry(b".          ", 16, folder), directory_entry(b"..         ", 16, 0),
                                 directory_entry(b"IMAGES  PNG", 32, picture, len(photo))])
    write_directory(2, root_entries)
    write_directory(3, [directory_entry(b".          ", 16, 3),
                        directory_entry(b"..         ", 16, 0),
                        directory_entry(b"BOOT       ", 16, 4)])
    write_directory(4, [directory_entry(b".          ", 16, 4),
                        directory_entry(b"..         ", 16, 3),
                        directory_entry(b"BOOTX64 EFI", 32, executable, len(efi))])
    for copy in range(2):
        position = base + (32 + copy * fat_sectors) * SECTOR
        disk[position:position+len(fat)] = fat
    fsinfo = bytearray(SECTOR)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<IIII", fsinfo, 484, 0x61417272, clusters - (next_free - 2), next_free, 0)
    struct.pack_into("<I", fsinfo, 508, 0xaa550000)
    for sector in (1, 7): disk[base+sector*SECTOR:base+(sector+1)*SECTOR] = fsinfo
    return disk

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--efi", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--desktop", action="store_true")
    args = parser.parse_args()
    expected = assemble(args.efi.read_bytes(), args.desktop)
    if args.check:
        if args.output.read_bytes() != expected: raise ValueError("UEFI image differs from build")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=args.output.parent, delete=False) as f:
                temporary = Path(f.name)
                f.write(expected)
            os.replace(temporary, args.output)
        finally:
            if temporary is not None and temporary.exists(): temporary.unlink()
        args.output.with_suffix(".img.sha256").write_text(
            hashlib.sha256(expected).hexdigest()+"  "+args.output.name+"\n", encoding="ascii")
    print(f"{'Validated' if args.check else 'Created'} {args.output}: 64 MiB, GPT, FAT32 ESP, BOOTX64.EFI")

if __name__ == "__main__": main()
