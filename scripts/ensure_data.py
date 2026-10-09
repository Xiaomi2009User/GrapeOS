#!/usr/bin/env python3
"""Create a persistent ATA disk only when absent; never overwrite saved data."""

import argparse
from pathlib import Path
import sys

DISK_BYTES = 1024 * 1024


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path, default=Path("data/grapeos-data.img"))
    args = parser.parse_args()
    try:
        args.path.parent.mkdir(parents=True, exist_ok=True)
        try:
            with args.path.open("xb") as disk:
                disk.truncate(DISK_BYTES)
        except FileExistsError:
            if not args.path.is_file() or args.path.stat().st_size != DISK_BYTES:
                raise ValueError("existing data disk must be exactly 1 MiB; it was left unchanged")
        else:
            print(f"Created persistent data disk: {args.path} ({DISK_BYTES} bytes)")
        return 0
    except (OSError, ValueError) as error:
        print(f"ensure_data: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
