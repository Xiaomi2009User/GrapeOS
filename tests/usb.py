#!/usr/bin/env python3
"""Boot the BIOS image as USB mass storage and IDE; verify live isolation."""

import argparse
import hashlib
import os
from pathlib import Path
import tempfile

from smoke import Guest, PROMPT, USERNAME_PROMPT, HOSTNAME_PROMPT, THEME_PROMPT, SAVE_PROMPT
from smoke import require, check_contains
from gui import frame, open_gui, terminal, leave, capture

ROOT = Path(__file__).resolve().parent.parent


def defaults(guest):
    guest.read_until(USERNAME_PROMPT)
    for expected in (HOSTNAME_PROMPT, THEME_PROMPT, SAVE_PROMPT, PROMPT):
        guest.ps2_key("ret")
        guest.read_until(expected)


def checks(guest, medium):
    original_boot = hashlib.sha256(guest.boot_disk.read_bytes()).digest()
    original_disk = guest.data_disk.read_bytes()
    guest.boot_from_menu(physical=True)
    defaults(guest)
    check_contains(guest, "calc 123 * 4", "492")
    check_contains(guest, "Grapemarket -I snake", "snake")
    check_contains(guest, "apps", "snake")
    open_gui(guest)
    frame(guest)
    guest.ps2_key("ctrl-n")
    guest.read_until(b"[GUI] Notepad opened.")
    guest.ps2_type("live usb note")
    guest.ps2_key("ctrl-s")
    guest.read_until(b"[GUI] Note saved for this session only.")
    if medium == "usb":
        capture(guest, "grapeos-usb-notepad.png")
    terminal(guest)
    check_contains(guest, "cat note.txt", "live usb note")
    leave(guest)
    require(guest.data_disk.read_bytes() == original_disk, "Live mode changed the internal test disk")
    require(hashlib.sha256(guest.boot_disk.read_bytes()).digest() == original_boot,
            "Live mode changed the boot USB image")
    guest.send(b"reboot\r")
    guest.boot_from_menu()
    defaults(guest)
    output = guest.command("cat note.txt")
    require("live usb note" not in output, "Live note unexpectedly persisted across restart")
    output = guest.command("apps")
    require("snake -" not in output, "Live app installation unexpectedly persisted")
    require(guest.data_disk.read_bytes() == original_disk, "Reboot changed the internal test disk")
    require(hashlib.sha256(guest.boot_disk.read_bytes()).digest() == original_boot,
            "Reboot changed the USB image")
    print(f"PASS: {medium} EDD boot, PS/2, 640x480 GUI, live apps/notes, restart, unchanged disks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="grapeos-usb-data-") as temporary:
        data = Path(temporary) / "internal.img"
        # Deliberately matches the old store driver's accepted size: the
        # explicit live flag must prevent using this disk even then.
        data.write_bytes(b"\xa5" * (1024 * 1024))
        for medium in ("usb", "ide"):
            guest = Guest(ROOT / "build/grapeos-usb.img", args.qemu,
                          os.environ.get("QEMU_BIOS_DIR"), 10, data,
                          boot_media=medium, attach_data=medium == "usb")
            try:
                checks(guest, medium)
            finally:
                guest.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
