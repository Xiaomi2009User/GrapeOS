#!/usr/bin/env python3
"""Boot the GPT image through OVMF, then exercise the actual kernel and GOP."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import time
from smoke import Guest, PROMPT, BOOT_READY, require, check_contains, check_cpu_halted
from usb import defaults
from gui import terminal, leave, capture
from desktop import commands as file_commands, apps as desktop_apps

ROOT = Path(__file__).resolve().parents[1]

def boot(guest):
    guest.read_until(b"Press Enter to boot GrapeOS")
    time.sleep(.7)
    require(b"Console ready" not in guest.buffer, "UEFI booted before menu selection")
    guest.screenshot(ROOT / "build/grapeos-uefi-menu.png")
    guest.ps2_key("ret")
    logs = guest.read_until(BOOT_READY)
    require("UEFI framebuffer mapped" in logs, f"No UEFI framebuffer: {logs!r}")
    require("session mode" in logs, f"UEFI touched data storage: {logs!r}")
    require("no legacy IRQ dependency" in logs, f"UEFI did not select polling mode: {logs!r}")
    guest.read_until(b"Starting GrapeOS...")
    defaults(guest)
    flags = int(re.search(r"EFL=([0-9a-fA-F]+)", guest.monitor_command("info registers"))[1], 16)
    require(not flags & 0x200, "UEFI polling test must run with legacy interrupts disabled")

def pixels(guest):
    time.sleep(.25)
    path = Path(guest.temporary.name) / "uefi.ppm"
    guest.monitor_command(f"screendump {json.dumps(str(path))}")
    header, dimensions, maximum, data = path.read_bytes().split(b"\n", 3)
    w, h = map(int, dimensions.split())
    require(header == b"P6" and maximum == b"255" and w >= 640 and h >= 480,
            f"Invalid UEFI framebuffer {dimensions!r}")
    return data

def checks(guest):
    original = hashlib.sha256(guest.boot_disk.read_bytes()).digest()
    guard = guest.data_disk.read_bytes()
    boot(guest)
    check_contains(guest, "info", "UEFI x64 / GRUB Multiboot2")
    check_contains(guest, "neofetch", "UEFI x64 / GRUB Multiboot2")
    check_contains(guest, "calc 7 * 8", "56")
    file_commands(guest)
    start = guest.command("uptime")
    time.sleep(1.1)
    require(guest.command("uptime") != start, "UEFI CPU clock is not advancing with IRQs disabled")
    check_contains(guest, "clock", "legacy IRQs disabled")
    baseline = pixels(guest)
    guest.send(b"gui\r")
    guest.read_until(b"[GUI] Desktop ready.")
    desktop = pixels(guest)
    require(desktop != baseline, "UEFI GUI did not draw")
    require(len(set(zip(desktop[0::3], desktop[1::3], desktop[2::3]))) >= 6,
            "UEFI GUI has no icons/text")
    capture(guest, "grapeos-uefi-gui.png")
    desktop_apps(guest)
    guest.ps2_key("ctrl-n")
    guest.read_until(b"[GUI] Notepad opened.")
    guest.ps2_type("asus tuf uefi\nhello grape")
    guest.ps2_key("ctrl-s")
    guest.read_until(b"[GUI] Note saved for this session only.")
    require(pixels(guest) != desktop, "UEFI Notepad did not render")
    capture(guest, "grapeos-uefi-notepad.png")
    terminal(guest)
    check_contains(guest, "cat note.txt", "asus tuf uefi\nhello grape")
    check_contains(guest, "calc -6 * 7", "-42")
    capture(guest, "grapeos-uefi-terminal.png")
    leave(guest)
    require(pixels(guest) != desktop, "UEFI GUI exit did not restore console")
    check_contains(guest, "Grapemarket -I snake", "snake")
    guest.send(b"snake\r")
    guest.read_until(b"Snake started")
    time.sleep(.15)
    guest.ps2_key("q")
    guest.read_until(PROMPT)
    require(guest.data_disk.read_bytes() == guard, "UEFI changed internal disk")
    require(hashlib.sha256(guest.boot_disk.read_bytes()).digest() == original,
            "UEFI changed USB image")
    guest.send(b"reboot\r")
    boot(guest)
    require("asus tuf uefi" not in guest.command("cat note.txt"), "UEFI live note persisted")
    check_contains(guest, "cat edit.txt", "no such file")
    guest.send(b"gui\r")
    guest.read_until(b"[GUI] Desktop ready.")
    terminal(guest)
    guest.send(b"del ram\r")
    guest.read_until(b"CPU halted. Restart the virtual machine to continue.")
    check_cpu_halted(guest, "UEFI kernel panic")
    capture(guest, "grapeos-uefi-panic.png")
    require(guest.data_disk.read_bytes() == guard, "UEFI panic changed internal disk")
    print("PASS: native UEFI USB boot, GOP text/GUI, PS/2 and TSC with IRQs disabled, apps, storage, reboot, panic")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    parser.add_argument("--ovmf", default=os.environ.get("OVMF_CODE",
        str(ROOT / ".runtime/qemu/usr/share/edk2/x64/OVMF_CODE.4m.fd")))
    parser.add_argument("--video-device")
    args = parser.parse_args()
    guest = Guest(ROOT / "build/grapeos-uefi.img", args.qemu, os.environ.get("QEMU_BIOS_DIR"),
                  25, boot_media="uefi", attach_data=True, ovmf=args.ovmf,
                  video_device=args.video_device)
    try:
        checks(guest)
    finally:
        guest.close()

if __name__ == "__main__": main()
