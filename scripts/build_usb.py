#!/usr/bin/env python3
"""Build a 32 MiB BIOS USB-HDD image with an active FAT16 partition."""

import argparse
import hashlib
import os
from pathlib import Path
import struct
import sys
import tempfile

from build_image import SECTOR_BYTES, STAGE2_BYTES, kernel_sectors

DISK_SECTORS = 65536
PARTITION_START = 2048
PARTITION_SECTORS = DISK_SECTORS - PARTITION_START
ROOT_ENTRIES = 512
CLUSTER_SECTORS = 2
LABEL = b"GRAPEUSB   "
README = (
    "GrapeOS - BIOS / Legacy live USB\r\n"
    "Boot this USB in BIOS / Legacy / CSM mode. Select GrapeOS and press Enter.\r\n"
    "Native UEFI boot is not supported. VGA and PS/2-compatible keyboard required.\r\n"
    "Type gui for Notepad and Terminal. Ctrl+N / Ctrl+T switch apps.\r\n"
    "Escape returns to the desktop, then the text shell.\r\n"
    "This live image keeps settings and notes in RAM until restart.\r\n"
    "The kernel does not access internal disks in live USB mode.\r\n"
    "All applications are built in; no network is needed.\r\n"
).encode("ascii")


def chs(lba):
    cylinder, remainder = divmod(lba, 255 * 63)
    head, sector = divmod(remainder, 63)
    if cylinder > 1023:
        return b"\xfe\xff\xff"
    return bytes((head, (sector + 1) | ((cylinder >> 2) & 0xc0), cylinder & 255))


def assemble(boot, stage2, kernel):
    kernel_sectors(kernel)
    if len(boot) != SECTOR_BYTES or boot[510:] != b"\x55\xaa":
        raise ValueError("USB boot sector must be 512 bytes with the BIOS signature")
    if boot[:3] != b"\xeb\x3c\x90" or any(boot[3:62]) or any(boot[446:510]):
        raise ValueError("USB boot sector must reserve the BPB and MBR partition table")
    if len(stage2) != STAGE2_BYTES:
        raise ValueError("GrapeLoad must occupy exactly eight sectors")
    if SECTOR_BYTES + len(stage2) + len(kernel) > PARTITION_START * SECTOR_BYTES:
        raise ValueError("boot payload overlaps the FAT16 partition")

    root_sectors = ROOT_ENTRIES * 32 // SECTOR_BYTES
    fat_sectors = 1
    while True:
        clusters = (PARTITION_SECTORS - 1 - 2 * fat_sectors - root_sectors) // CLUSTER_SECTORS
        required = ((clusters + 2) * 2 + SECTOR_BYTES - 1) // SECTOR_BYTES
        if required <= fat_sectors:
            break
        fat_sectors = required
    if not 4085 <= clusters < 65525:
        raise ValueError("partition does not have a valid FAT16 cluster count")
    if len(README) > CLUSTER_SECTORS * SECTOR_BYTES:
        raise ValueError("README exceeds the allocated cluster")

    disk = bytearray(DISK_SECTORS * SECTOR_BYTES)
    disk[:SECTOR_BYTES] = boot
    disk[SECTOR_BYTES:SECTOR_BYTES + STAGE2_BYTES] = stage2
    kernel_start = SECTOR_BYTES + STAGE2_BYTES
    disk[kernel_start:kernel_start + len(kernel)] = kernel
    entry = b"\x80" + chs(PARTITION_START) + b"\x0e" + chs(DISK_SECTORS - 1)
    entry += struct.pack("<II", PARTITION_START, PARTITION_SECTORS)
    disk[446:462] = entry
    # A repeat of the loader as the VBR also handles firmware that starts
    # from the active partition instead of executing the MBR first.
    volume = bytearray(boot)
    volume[3:11] = b"GRAPEOS "
    struct.pack_into("<HBHBHHBHHHII", volume, 11,
                     SECTOR_BYTES, CLUSTER_SECTORS, 1, 2, ROOT_ENTRIES,
                     PARTITION_SECTORS, 0xf8, fat_sectors, 63, 255,
                     PARTITION_START, 0)
    volume[36:39] = b"\x80\x00\x29"
    struct.pack_into("<I", volume, 39, 0x47525031)
    volume[43:54] = LABEL
    volume[54:62] = b"FAT16   "
    partition = PARTITION_START * SECTOR_BYTES
    disk[partition:partition + SECTOR_BYTES] = volume
    fat = bytearray(fat_sectors * SECTOR_BYTES)
    struct.pack_into("<HHH", fat, 0, 0xfff8, 0xffff, 0xffff)
    for copy in range(2):
        start = partition + (1 + copy * fat_sectors) * SECTOR_BYTES
        disk[start:start + len(fat)] = fat
    root = partition + (1 + 2 * fat_sectors) * SECTOR_BYTES
    disk[root:root + 11] = LABEL
    disk[root + 11] = 0x08
    root += 32
    disk[root:root + 11] = b"README  TXT"
    disk[root + 11] = 0x20
    struct.pack_into("<HHI", disk, root + 24, 33, 2, len(README))
    data_start = partition + (1 + 2 * fat_sectors + root_sectors) * SECTOR_BYTES
    disk[data_start:data_start + len(README)] = README
    return disk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("boot", "stage2", "kernel", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        expected = assemble(args.boot.read_bytes(), args.stage2.read_bytes(), args.kernel.read_bytes())
        if args.check:
            if args.output.read_bytes() != expected:
                raise ValueError("USB image contents or size differ from the built payload")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=args.output.parent, delete=False) as output:
                    temporary = Path(output.name)
                    output.write(expected)
                os.replace(temporary, args.output)
            finally:
                if temporary is not None and temporary.exists():
                    temporary.unlink()
            checksum = hashlib.sha256(expected).hexdigest()
            args.output.with_suffix(".img.sha256").write_text(
                f"{checksum}  {args.output.name}\n", encoding="ascii")
        print(f"{'Validated' if args.check else 'Created'} {args.output}: "
              f"32 MiB, BIOS EDD, active FAT16 partition, live session mode")
        return 0
    except (OSError, ValueError) as error:
        print(f"build_usb: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
