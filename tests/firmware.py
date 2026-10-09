#!/usr/bin/env python3
"""Exercise the UEFI services desktop on a disposable FAT USB image."""
import os
import shutil
import tempfile
from pathlib import Path
from smoke import Guest, USERNAME_PROMPT, HOSTNAME_PROMPT, THEME_PROMPT, SAVE_PROMPT, PROMPT, check_contains
import time
import re
from fat32 import Volume
ROOT = Path(__file__).resolve().parents[1]

def new_guest(image, **kwargs):
    return Guest(image,
        os.environ.get('QEMU', str(ROOT / '.runtime/qemu/usr/bin/qemu-system-x86_64')),
        os.environ.get('QEMU_BIOS_DIR'), 35, boot_media='uefi', attach_data=False,
        ovmf=os.environ.get('OVMF_CODE', str(ROOT / '.runtime/qemu/usr/share/edk2/x64/OVMF_CODE.4m.fd')), **kwargs)

def key(guest, name):
    guest.ps2_key(name)
    time.sleep(.25)

def terminal(guest):
    key(guest, 'ctrl-t')
    guest.read_until(b'[GUI] Terminal opened.'); guest.read_until(PROMPT)

def boot(guest, first=False):
    guest.read_until(b'Press Enter to boot GrapeOS')
    key(guest, 'ret')
    if first:
        guest.read_until(USERNAME_PROMPT)
        for marker in (HOSTNAME_PROMPT, THEME_PROMPT, SAVE_PROMPT):
            key(guest, 'ret'); guest.read_until(marker)
        key(guest, 'ret')
    guest.read_until(b'[GUI] Desktop ready.')

def main():
    guest = new_guest(ROOT / 'build/grapeos-desktop.img')
    temporary = tempfile.TemporaryDirectory(prefix='grape-real-files-')
    saved = Path(temporary.name) / 'saved.img'
    try:
        boot(guest, first=True)
        # Buffered input should produce one final console state without losing
        # characters while frames are coalesced. Exercise text mode as well.
        key(guest, 'esc'); guest.read_until(b'GrapeOS text shell.'); guest.read_until(PROMPT)
        check_contains(guest, 'echo ' + 'smooth console ' * 6, 'smooth console ' * 5)
        check_contains(guest, 'calc 17 * 19', '323')
        guest.send(b'gui\r'); guest.read_until(b'[GUI] Desktop ready.')
        key(guest, 'f8'); guest.read_until(b'[GUI] Keyboard cursor on.')
        guest.send(b'\x1b[C' * 24 + b'\x1b[A' * 2 + b'\r'); guest.read_until(b'[GUI] Files opened.')
        key(guest, 'esc')
        key(guest, 'f8'); guest.read_until(b'[GUI] Keyboard cursor off.')
        terminal(guest)
        check_contains(guest, 'neofetch', 'GrapeLoad / UEFI services')
        time.sleep(.3); guest.screenshot(ROOT / 'build/grapeos-neofetch-gui.png')
        key(guest, 'ctrl-g'); guest.read_until(b'Snake started')
        guest.send(b'p'); guest.read_until(b'Snake paused')
        time.sleep(.3); guest.screenshot(ROOT / 'build/grapeos-snake-gui.png')
        guest.send(b'r'); guest.read_until(b'Snake restarted')
        guest.send(b'q'); guest.read_until(b'Snake closed'); guest.read_until(PROMPT)
        check_contains(guest, 'grapemarket -I guess', 'Installed guess')
        guest.send(b'guess\r'); guest.read_until(b'Guess> ')
        guest.send(b'0\r'); guest.read_until(b'Enter a number from 1 to 100.')
        guest.send(b'q'); guest.read_until(b'Guess closed'); guest.read_until(PROMPT)
        key(guest, 'ctrl-p'); guest.read_until(b'[GUI] Photos opened.')
        time.sleep(.3); guest.screenshot(ROOT / 'build/grapeos-photos.png')
        key(guest, 'esc'); terminal(guest)
        guest.send(b'date\r')
        date_output = guest.read_until(PROMPT)
        assert re.search(r'Firmware date: \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}', date_output), date_output
        check_contains(guest, 'theme mint', 'Desktop theme saved to USB.')
        key(guest, 'ctrl-y'); guest.read_until(b'[GUI] Theme: midnight')
        check_contains(guest, 'theme', 'Desktop theme: midnight')
        check_contains(guest, 'theme missing', 'Unknown theme.')
        check_contains(guest, 'theme grape extra', 'Usage: theme')
        check_contains(guest, 'theme', 'Desktop theme: midnight')
        check_contains(guest, 'write real.txt Hello Grape', 'saved to USB')
        check_contains(guest, 'cat real.txt', 'Hello Grape')
        check_contains(guest, 'cp real.txt copy.txt', 'saved to USB')
        check_contains(guest, 'mv copy.txt moved.txt', 'saved to USB')
        check_contains(guest, 'write moved.txt X', 'saved to USB')
        check_contains(guest, 'touch empty.txt', 'saved to USB')
        check_contains(guest, 'rm empty.txt', 'saved to USB')
        check_contains(guest, 'write ../escape.txt bad', 'must not begin with a dot')
        check_contains(guest, 'write NOTE.TXT bad', 'protected')
        key(guest, 'ctrl-f'); guest.read_until(b'[GUI] Files opened.')
        key(guest, 'ctrl-a'); guest.read_until(b'[GUI] New file name:')
        guest.screenshot(ROOT / 'build/grapeos-file-dialog.png')
        guest.send(b'gui.txt\r'); guest.read_until(b'[GUI] File operation completed.')
        key(guest, 'ret'); guest.read_until(b'[GUI] Notepad opened.')
        guest.send(b'Created inside GrapeOS'); time.sleep(.5)
        key(guest, 'ctrl-s'); guest.read_until(b'[GUI] USB file saved.')
        key(guest, 'ctrl-f'); guest.read_until(b'[GUI] Files opened.')
        key(guest, 'ctrl-r'); guest.read_until(b'[GUI] Rename file:')
        key(guest, 'ctrl-u'); guest.send(b'renamed.txt\r')
        guest.read_until(b'[GUI] File operation completed.')
        key(guest, 'esc')
        key(guest, 'ctrl-f'); guest.read_until(b'[GUI] Files opened.')
        for _ in range(4): key(guest, 'up')
        key(guest, 'ret'); guest.read_until(b'[GUI] Notepad opened.')
        key(guest, 'ctrl-u')
        # The diagnostic UART shares OVMF's bounded serial-console queue.
        # Pace a multi-KiB transfer; retain unpaced command bursts above.
        for offset in range(0, 4100, 32):
            guest.send(b'Z' * min(32, 4100 - offset)); time.sleep(.05)
        guest.send(b'\x13')
        guest.read_until(b'[GUI] Note saved to local disk.')
        key(guest, 'esc')
        guest.screenshot(ROOT / 'build/grapeos-desktop.png')
        time.sleep(.3)
        disk = guest.boot_disk.read_bytes()
        fat = Volume(disk)
        assert fat.read('/GRAPE/images.png') == (ROOT / 'assets/images.png').read_bytes()
        assert fat.read('/GRAPE/.desktop-theme') == (2).to_bytes(4, 'little')
        assert fat.read('/GRAPE/real.txt') == b'Hello Grape'
        assert fat.read('/GRAPE/moved.txt') == b'X', 'overwrite did not truncate'
        assert fat.read('/GRAPE/renamed.txt') == b'Created inside GrapeOS'
        saved_note = fat.read('/GRAPE/note.txt')
        assert saved_note == b'Z' * 4095, f'note size limit or multi-cluster write failed: {len(saved_note)} bytes, {saved_note[:30]!r}'
        entries = fat.read('/GRAPE')
        assert 'empty.txt' not in entries and 'gui.txt' not in entries and 'copy.txt' not in entries
        assert not any(name.endswith(('.~bak', '.~new')) for name in entries)
        assert fat.read('/EFI/BOOT/BOOTX64.EFI') == (ROOT / 'build/BOOTX64-desktop.EFI').read_bytes()
        shutil.copyfile(guest.boot_disk, saved)
        guest.close(); guest = new_guest(saved)
        boot(guest)
        terminal(guest)
        check_contains(guest, 'theme', 'Desktop theme: midnight')
        check_contains(guest, 'cat renamed.txt', 'Created inside GrapeOS')
        check_contains(guest, 'cat moved.txt', 'X')
        print('PASS: cursor, GUI create/edit/rename, real FAT contents, truncation, reboot persistence.')
        print('PASS: firmware date, GUI theme shortcut, invalid theme rejection and theme persistence.')
        guest.close()
        internal = Path(temporary.name) / 'internal.img'
        shutil.copyfile(ROOT / 'build/grapeos-desktop.img', internal)
        original = internal.read_bytes()
        guest = new_guest(ROOT / 'build/grapeos-desktop.img', extra_args=(
            '-drive', f'if=none,id=internal-test,format=raw,file={internal}',
            '-device', 'ide-hd,drive=internal-test,bootindex=0'))
        guest.read_until(b'Press Enter to boot GrapeOS'); key(guest, 'ret')
        output = guest.read_until(USERNAME_PROMPT)
        assert '[WARN] USB filesystem unavailable' in output, 'internal ESP allowed for writes'
        assert internal.read_bytes() == original, 'internal test disk was modified'
        print('PASS: booting from an internal test disk refuses filesystem writes.')
    finally:
        guest.close()
        temporary.cleanup()

if __name__ == '__main__': main()
