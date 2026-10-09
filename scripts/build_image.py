#!/usr/bin/env python3
"""Assemble and validate a bootable 1.44 MiB floppy without third-party tools."""

import argparse
import os
from pathlib import Path
import sys
import tempfile

SECTOR_BYTES = 512
FLOPPY_BYTES = 1440 * 1024
MAX_KERNEL_BYTES = 192 * 1024
STAGE2_BYTES = 8 * SECTOR_BYTES


def kernel_sectors(kernel: bytes) -> int:
    if not kernel:
        raise ValueError("kernel binary is empty")
    if len(kernel) > MAX_KERNEL_BYTES:
        raise ValueError(
            f"kernel is {len(kernel)} bytes; the BIOS loader supports at most "
            f"{MAX_KERNEL_BYTES} bytes"
        )
    return (len(kernel) + SECTOR_BYTES - 1) // SECTOR_BYTES


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-sectors", type=Path, metavar="KERNEL")
    parser.add_argument("--boot", type=Path)
    parser.add_argument("--stage2", type=Path)
    parser.add_argument("--kernel", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    try:
        if args.kernel_sectors is not None:
            print(kernel_sectors(args.kernel_sectors.read_bytes()))
            return 0
        if args.boot is None or args.stage2 is None or args.kernel is None or args.output is None:
            parser.error("--boot, --stage2, --kernel, and --output are required")
        boot = args.boot.read_bytes()
        stage2 = args.stage2.read_bytes()
        kernel = args.kernel.read_bytes()
        sectors = kernel_sectors(kernel)
        if len(boot) != SECTOR_BYTES:
            raise ValueError(f"boot sector is {len(boot)} bytes; expected 512")
        if boot[510:512] != b"\x55\xaa":
            raise ValueError("boot sector lacks the BIOS 0x55AA signature")
        if len(stage2) != STAGE2_BYTES:
            raise ValueError(f"GrapeLoad stage is {len(stage2)} bytes; expected {STAGE2_BYTES}")
        expected = boot + stage2 + kernel + bytes(
            FLOPPY_BYTES - len(boot) - len(stage2) - len(kernel)
        )
        if args.check:
            if args.output.read_bytes() != expected:
                raise ValueError("disk image contents or size differ from the built payload")
            verb = "Validated"
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            # Replace the completed build atomically so a running VM's old
            # floppy inode remains intact while the next image is rebuilt.
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(
                    dir=args.output.parent, prefix=f".{args.output.name}.", delete=False
                ) as disk:
                    temporary = Path(disk.name)
                    disk.write(expected)
                os.replace(temporary, args.output)
            finally:
                if temporary is not None and temporary.exists():
                    temporary.unlink()
            verb = "Created"
        print(
            f"{verb} {args.output}: {FLOPPY_BYTES} bytes, "
            f"GrapeLoad {len(stage2)} bytes, kernel {len(kernel)} bytes ({sectors} sectors)"
        )
        return 0
    except (OSError, ValueError) as error:
        print(f"build_image: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
