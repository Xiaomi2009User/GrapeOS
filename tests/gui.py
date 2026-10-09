#!/usr/bin/env python3
"""Exercise the graphical VGA desktop on copied boot and data disks."""
import argparse
import json
import os
from pathlib import Path
import tempfile
import time
from smoke import Guest, PROMPT, first_setup, check_contains, check_cpu_halted, require
from desktop import commands as file_commands, apps as desktop_apps

ROOT = Path(__file__).resolve().parents[1]

def frame(guest):
    time.sleep(0.12)
    path = Path(guest.temporary.name) / 'graphics.ppm'
    guest.monitor_command(f'screendump {json.dumps(str(path))}')
    header, dimensions, maximum, pixels = path.read_bytes().split(b'\n', 3)
    require(header == b'P6' and maximum == b'255' and dimensions == b'640 480',
            f'GUI is not rendering native 640x480 pixels: {dimensions!r}')
    return pixels

def capture(guest, name):
    time.sleep(0.12)
    guest.screenshot(ROOT / 'build' / name)

def text_pixels(guest):
    time.sleep(0.12)
    path = Path(guest.temporary.name) / 'text.ppm'
    guest.monitor_command(f'screendump {json.dumps(str(path))}')
    header, dimensions, maximum, pixels = path.read_bytes().split(b'\n', 3)
    require(header == b'P6' and maximum == b'255', 'Unexpected QEMU screenshot format')
    width, height = map(int, dimensions.split())
    require((width, height) == (720, 400), 'Text VGA geometry was not restored')
    # Compare two text rows, excluding the blinking cursor on row three.
    return pixels[:width * 32 * 3]

def open_gui(guest):
    guest.send(b'gui\r')
    guest.read_until(b'[GUI] Desktop ready.')
    pixels = frame(guest)
    require(len(pixels) == 640 * 480 * 3, 'Incomplete graphical framebuffer')
    colors = set(zip(pixels[0::3], pixels[1::3], pixels[2::3]))
    require(len(colors) >= 6, 'GUI icons or labels were not rendered')
    return pixels

def desktop(guest):
    guest.ps2_key('esc')
    guest.read_until(b'[GUI] Desktop ready.')

def terminal(guest):
    guest.ps2_key('ctrl-t')
    guest.read_until(b'[GUI] Terminal opened.')
    guest.read_until(PROMPT)

def leave(guest):
    desktop(guest)
    guest.ps2_key('esc')
    guest.read_until(PROMPT)

def checks(guest):
    first_setup(guest)
    file_commands(guest)
    check_contains(guest, 'gui extra', 'no arguments expected')
    guest.command('clear')
    check_contains(guest, 'echo text-font-restored', 'text-font-restored')
    baseline_font = text_pixels(guest)
    original = guest.data_disk.read_bytes()
    initial = open_gui(guest)
    capture(guest, 'grapeos-gui.png')
    desktop_apps(guest)
    guest.ps2_key('tab')
    require(frame(guest) != initial, 'Tab did not change the selected icon')
    guest.ps2_key('ret')
    guest.read_until(b'[GUI] Terminal opened.')
    guest.read_until(PROMPT)
    check_contains(guest, 'calc -6 * 7', '-42')
    check_contains(guest, 'whoami', 'tester')
    check_contains(guest, 'echo graphical-terminal-ok', 'graphical-terminal-ok')
    capture(guest, 'grapeos-gui-terminal.png')
    for command in ('setup', 'snake', 'neofetch', 'gui'):
        check_contains(guest, command, 'text shell')
    guest.ps2_key('ctrl-n')
    guest.read_until(b'[GUI] Notepad opened.')
    guest.ps2_type('hello grape\nsecond linex')
    guest.ps2_key('backspace')
    guest.ps2_key('ctrl-s')
    guest.read_until(b'[GUI] Note saved to local disk.')
    capture(guest, 'grapeos-gui-notepad.png')
    require(guest.data_disk.read_bytes() != original, 'Notepad did not persist the note')
    terminal(guest)
    check_contains(guest, 'cat note.txt', 'hello grape\nsecond line')
    guest.command('clear')
    check_contains(guest, 'echo after-clear', 'after-clear')
    leave(guest)
    check_contains(guest, 'cat note.txt', 'hello grape\nsecond line')
    guest.command('clear')
    check_contains(guest, 'echo text-font-restored', 'text-font-restored')
    require(text_pixels(guest) == baseline_font, 'Restored font does not render the original text pixels')
    capture(guest, 'grapeos-gui-exit.png')
    chars, _ = guest.vga_cells()
    require(b'text-font-restored' in chars, 'Text VGA console was not restored')

    open_gui(guest)
    guest.ps2_key('ctrl-n')
    guest.read_until(b'[GUI] Notepad opened.')
    guest.ps2_key('ctrl-u')
    # Pace UART input at blocks small enough to fit the hardware FIFO.
    for _ in range(20):
        guest.send(b'x' * 16)
        time.sleep(0.06)
    guest.ps2_key('ctrl-s')
    guest.read_until(b'[GUI] Note saved to local disk.')
    terminal(guest)
    output = guest.command('cat note.txt')
    require('x' * 255 in output and 'x' * 256 not in output, 'Notepad exceeded 255-byte limit')
    guest.ps2_key('ctrl-n')
    guest.read_until(b'[GUI] Notepad opened.')
    guest.ps2_key('ctrl-u')
    guest.ps2_type('persisted gui note')
    guest.ps2_key('ctrl-s')
    guest.read_until(b'[GUI] Note saved to local disk.')
    guest.ps2_type(' UNSAVED')
    leave(guest)
    check_contains(guest, 'cat note.txt', 'persisted gui note')
    output = guest.command('cat note.txt')
    require('UNSAVED' not in output, 'Notepad saved changes without Ctrl+S')

    open_gui(guest)
    terminal(guest)
    saved = guest.data_disk.read_bytes()
    guest.send(b'del ram\r')
    guest.read_until(b'CPU halted. Restart the virtual machine to continue.')
    check_cpu_halted(guest, 'GUI terminal panic')
    chars, _ = guest.vga_cells()
    require(b'KERNEL PANIC' in chars, 'Panic did not restore text mode from GUI')
    require(guest.data_disk.read_bytes() == saved, 'GUI panic changed saved data')
    capture(guest, 'grapeos-gui-panic.png')
    print('PASS: graphical pixels, PS/2 shortcuts, terminal commands, Notepad save/limit, mode restore, panic')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default=os.environ.get('QEMU', 'qemu-system-i386'))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='grapeos-gui-data-') as storage:
        data = Path(storage) / 'data.img'
        guest = Guest(ROOT / 'build/grapeos.img', args.qemu, os.environ.get('QEMU_BIOS_DIR'), 8, data)
        try:
            checks(guest)
        finally:
            guest.close()
        guest = Guest(ROOT / 'build/grapeos.img', args.qemu, os.environ.get('QEMU_BIOS_DIR'), 8, data)
        try:
            guest.boot_from_menu()
            guest.read_until(PROMPT)
            check_contains(guest, 'cat note.txt', 'persisted gui note')
            open_gui(guest)
            guest.ps2_key('ctrl-n')
            guest.read_until(b'[GUI] Notepad opened.')
            guest.ps2_key('ctrl-s')
            guest.read_until(b'[GUI] Note saved to local disk.')
            leave(guest)
            check_contains(guest, 'cat note.txt', 'persisted gui note')
            print('PASS: fresh VM restores the saved graphical Notepad document')
        finally:
            guest.close()

if __name__ == '__main__':
    main()
