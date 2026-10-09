#!/usr/bin/env python3
"""Boot the raw BIOS image in QEMU and exercise the real kernel shell.

Uses only Python's standard library. QEMU and its BIOS data can be overridden
with --qemu / --bios-dir or QEMU / QEMU_BIOS_DIR environment variables.
"""

import argparse
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import socket
import subprocess
import sys
import tempfile
import time


PROMPT = b"grape> "
BANNER = b"GrapeOS 0.1"
BOOT_MENU_PROMPT = b"Press Enter to boot GrapeOS"
BOOT_READY = b"[ OK ] GrapeOS ready"
USERNAME_PROMPT = b"Username [grape]: "
HOSTNAME_PROMPT = b"Computer name [grapeos]: "
THEME_PROMPT = b"Theme [1 purple / 2 green / 3 classic]: "
SAVE_PROMPT = b"Save setup? [Y/n]: "


class Guest:
    def __init__(self, image, qemu, bios_dir, timeout, data_disk=None,
                 boot_media="floppy", attach_data=True, ovmf=None, video_device=None, extra_args=()):
        self.timeout = timeout
        self.temporary = tempfile.TemporaryDirectory(prefix="grapeos-smoke-")
        self.monitor_path = os.path.join(self.temporary.name, "monitor.sock")
        self.buffer = bytearray()
        boot_disk = Path(self.temporary.name) / "boot.img"
        shutil.copyfile(image, boot_disk)
        self.boot_disk = boot_disk
        self.session_mode = boot_media != "floppy" or not attach_data
        self.data_disk = Path(data_disk or Path(self.temporary.name) / "data.img")
        if not self.data_disk.exists():
            self.data_disk.write_bytes(bytes(1024 * 1024))
        command = [
            qemu, "-accel", "tcg", "-m", "256M" if boot_media == "uefi" else "32M", "-display", "none",
            "-serial", "stdio", "-monitor",
            f"unix:{self.monitor_path},server=on,wait=off", "-nic", "none",
        ]
        if boot_media == "uefi":
            firmware = Path(ovmf)
            variables = Path(self.temporary.name) / "OVMF_VARS.fd"
            shutil.copyfile(os.environ.get("OVMF_VARS", str(firmware.parent / "OVMF_VARS.4m.fd")), variables)
            command += ["-cpu", "max", "-machine", "q35",
                        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={firmware}",
                        "-drive", f"if=pflash,format=raw,unit=1,file={variables}",
                        "-device", "qemu-xhci,id=grape-usb",
                        "-drive", f"file={boot_disk},format=raw,if=none,id=grape-live",
                        "-device", "usb-storage,drive=grape-live,bootindex=1"]
            if video_device:
                command += ["-vga", "none", "-device", video_device]
        elif boot_media == "usb":
            command += ["-device", "piix3-usb-uhci,id=grape-usb",
                        "-drive", f"file={boot_disk},format=raw,if=none,id=grape-live",
                        "-device", "usb-storage,drive=grape-live,bootindex=1"]
        elif boot_media == "ide":
            command += ["-drive", f"file={boot_disk},format=raw,if=ide,index=0", "-boot", "c"]
            if attach_data:
                raise ValueError("IDE boot test occupies the primary master")
        elif boot_media == "floppy":
            command += ["-drive", f"file={boot_disk},format=raw,if=floppy,index=0", "-boot", "a"]
        else:
            raise ValueError(f"Unknown boot medium: {boot_media}")
        if attach_data:
            command += ["-drive", f"file={self.data_disk},format=raw,if=ide,index=0"]
        if bios_dir:
            command += ["-L", bios_dir]
        command += list(extra_args)
        self.process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, bufsize=0,
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.monitor = None

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)
        if self.monitor:
            self.monitor.close()
        self.selector.close()
        self.process.stdin.close()
        self.process.stdout.close()
        self.process.stderr.close()
        self.temporary.cleanup()

    def read_until(self, expected, timeout=None):
        deadline = time.monotonic() + (timeout or self.timeout)
        while True:
            position = self.buffer.find(expected)
            if position >= 0:
                end = position + len(expected)
                output = bytes(self.buffer[:end])
                del self.buffer[:end]
                return output.decode("ascii", errors="replace").replace("\r", "")
            if self.process.poll() is not None:
                error = self.process.stderr.read().decode(errors="replace")
                raise AssertionError(f"QEMU exited ({self.process.returncode}): {error}")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError(
                    f"Timed out waiting for {expected!r}; received {bytes(self.buffer)!r}"
                )
            for key, _ in self.selector.select(min(remaining, 0.2)):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if chunk:
                    self.buffer.extend(chunk)

    def send(self, data):
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def command(self, text):
        self.send(text.encode("ascii") + b"\r")
        return self.read_until(PROMPT)

    def monitor_command(self, command):
        if self.monitor is None:
            self.monitor = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.monitor.settimeout(self.timeout)
            deadline = time.monotonic() + self.timeout
            while True:
                try:
                    self.monitor.connect(self.monitor_path)
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    if time.monotonic() >= deadline:
                        raise AssertionError("QEMU monitor socket did not appear")
                    time.sleep(0.05)
            self._read_monitor()
        self.monitor.sendall(command.encode("utf-8") + b"\n")
        return self._read_monitor()

    def _read_monitor(self):
        output = bytearray()
        while b"(qemu) " not in output:
            chunk = self.monitor.recv(65536)
            if not chunk:
                raise AssertionError("QEMU monitor disconnected")
            output.extend(chunk)
        return output.decode("ascii", errors="replace")

    def ps2_type(self, text):
        # HMP sends actual emulated PS/2 scan codes into the guest's IRQ handler.
        names = {" ": "spc", "\n": "ret", "-": "minus", "/": "slash",
                 "*": "shift-8", "+": "shift-equal", "=": "equal", ".": "dot"}
        for char in text:
            self.ps2_key("shift-" + char.lower() if char.isupper() else names.get(char, char))

    def ps2_key(self, key):
        self.monitor_command(f"sendkey {key} 20")
        time.sleep(0.035)

    def vga_cells(self):
        destination = Path(self.temporary.name) / "vga.bin"
        response = self.monitor_command(f"pmemsave 0xb8000 4000 {json.dumps(str(destination))}")
        require(destination.is_file(), f"Could not inspect VGA memory: {response}")
        contents = destination.read_bytes()
        require(len(contents) == 4000, f"Incomplete VGA memory dump: {len(contents)} bytes")
        return contents[::2], contents[1::2]

    def screenshot(self, destination):
        destination = Path(destination).resolve()
        destination.parent.mkdir(parents=True, exist_ok=True)
        response = self.monitor_command(
            f"screendump {json.dumps(str(destination), ensure_ascii=False)} -f png"
        )
        require(destination.is_file() and destination.stat().st_size > 0,
                f"Could not capture VGA screenshot: {response}")
        print(f"VGA screenshot: {destination}")

    def boot_from_menu(self, physical=False, menu_screenshot=None, log_screenshot=None, selection=b"\r"):
        menu = self.read_until(BOOT_MENU_PROMPT)
        require("GrapeLoad" in menu and "Welcome to GrapeOS" not in menu and "Console ready" not in menu,
                f"OS ran before the GrapeLoad selection: {menu!r}")
        self.send(b"x ?\t")
        try:
            self.read_until(b"[ OK ] Console ready", timeout=0.3)
        except AssertionError as error:
            require("Timed out" in str(error), f"Unexpected boot menu state: {error}")
            require(b"Welcome to GrapeOS" not in self.buffer and b"[ OK ]" not in self.buffer,
                    f"Invalid keys or idle time booted the OS: {bytes(self.buffer)!r}")
        else:
            raise AssertionError("GrapeLoad booted without Enter")
        if menu_screenshot:
            self.screenshot(menu_screenshot)
        if physical:
            self.ps2_key("ret")
        else:
            self.send(selection)
        logs = self.read_until(BOOT_READY)
        logs += self.read_until(b"Starting GrapeOS...")
        expected = (
            "[ OK ] Console ready",
            "[ OK ] 32-bit protected mode entered; kernel memory initialized",
            "[ OK ] Interrupt handlers installed; PIC remapped",
            "[ OK ] PIT programmed at 100 Hz; timer and keyboard IRQs enabled",
            ("[WARN] Local data disk unavailable; session mode" if self.session_mode
             else "[ OK ] Local data disk ready"), "[ OK ] Offline applications ready",
            "[ OK ] GrapeOS ready",
        )
        positions = [logs.find(message) for message in expected]
        require(all(position >= 0 for position in positions) and positions == sorted(positions),
                f"Initialization logs missing or out of order: {logs!r}")
        require("Welcome to GrapeOS" not in logs and "grape> " not in logs,
                f"Shell or setup preceded initialization logs: {logs!r}")
        require("[ OK ] First-run setup ready" in logs or "[ OK ] Saved profile loaded" in logs,
                f"Missing actual profile/setup initialization result: {logs!r}")
        if log_screenshot:
            time.sleep(0.05)
            self.screenshot(log_screenshot)
        print(f"PASS: GrapeLoad waits for {'PS/2' if physical else 'serial'} Enter, then ordered boot logs")
        return logs


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def result_lines(output):
    return output.splitlines()[1:-1]


def check_contains(guest, command, expected):
    output = guest.command(command)
    require(expected in "\n".join(result_lines(output)), f"{command!r}: {output!r}")
    return output


def check_calc(guest, expression, value):
    output = guest.command("calc " + expression)
    require(
        any(re.search(rf"(?<![0-9]){re.escape(str(value))}(?![0-9])", line)
            for line in result_lines(output)),
        f"calc {expression!r} expected {value}: {output!r}",
    )


def first_setup(guest, screenshot=None, menu_screenshot=None, log_screenshot=None):
    guest.boot_from_menu(physical=True, menu_screenshot=menu_screenshot,
                         log_screenshot=log_screenshot)
    startup = guest.read_until(USERNAME_PROMPT)
    require("Welcome to GrapeOS" in startup, f"Missing first-boot wizard: {startup!r}")
    if screenshot:
        guest.screenshot(screenshot)
    guest.send(b"\x03")
    guest.read_until(USERNAME_PROMPT)
    guest.send(b"a" * 200 + b"\r")
    output = guest.read_until(USERNAME_PROMPT)
    require("Input too long" in output, f"Wizard accepted an overflowing line: {output!r}")
    for invalid in (b"9invalid", b"a" * 24):
        guest.send(invalid + b"\r")
        output = guest.read_until(USERNAME_PROMPT)
        require("Use 1..23" in output, f"Invalid username accepted: {output!r}")
    guest.send(b"testerx\x08\r")
    guest.read_until(HOSTNAME_PROMPT)
    for invalid in (b"-invalid-", b"a" * 24):
        guest.send(invalid + b"\r")
        output = guest.read_until(HOSTNAME_PROMPT)
        require(re.search(r"1\.\.23|invalid|letters", output, re.I),
                f"Invalid hostname accepted: {output!r}")
    guest.send(b"grape-box\r")
    guest.read_until(THEME_PROMPT)
    guest.send(b"9\r")
    guest.read_until(THEME_PROMPT)
    guest.send(b"1\r")
    guest.read_until(SAVE_PROMPT)
    guest.send(b"maybe\r")
    guest.read_until(SAVE_PROMPT)
    guest.send(b"n\r")
    guest.read_until(b"Username [tester]: ")
    require(not any(guest.data_disk.read_bytes()), "Canceled setup wrote a profile to disk")
    guest.send(b"tester\r")
    guest.read_until(b"Computer name [grape-box]: ")
    guest.send(b"grape-box\r")
    guest.read_until(THEME_PROMPT)
    guest.send(b"1\r")
    guest.read_until(SAVE_PROMPT)
    guest.send(b"y\r")
    startup += guest.read_until(PROMPT)
    require(BANNER.decode() in startup, f"Missing post-setup neofetch: {startup!r}")
    require(any(guest.data_disk.read_bytes()), f"Confirmed setup was not saved to disk: {startup!r}")
    print("PASS: first-boot validation, editing, canceled setup, confirmed profile")
    return startup


def check_profile(guest):
    check_contains(guest, "whoami", "tester")
    check_contains(guest, "hostname", "grape-box")
    output = guest.command("settings")
    for value in ("User:", "tester", "Computer:", "grape-box", "Theme:", "Storage:"):
        require(value in "\n".join(result_lines(output)), f"settings omitted {value}: {output!r}")
    require("Local data disk" in output and "Purple (13 on 0)" in output,
            f"Saved storage/theme settings were not restored: {output!r}")
    check_contains(guest, "ver", "GrapeOS 0.1")


def cancel_profile_edit(guest):
    saved_disk = guest.data_disk.read_bytes()
    guest.send(b"setup\r")
    guest.read_until(b"Username [")
    guest.send(b"other\r")
    guest.read_until(b"Computer name [")
    guest.send(b"\x03")
    guest.read_until(PROMPT)
    require(guest.data_disk.read_bytes() == saved_disk, "Canceled profile edit changed disk")
    check_profile(guest)
    print("PASS: canceling profile edits retains the saved profile")


def cancel_custom_theme_edit(guest):
    check_contains(guest, "color 12 4", "VGA color saved.")
    guest.send(b"setup\r")
    guest.read_until(b"Username [")
    guest.send(b"\r")
    guest.read_until(b"Computer name [")
    guest.send(b"\r")
    output = guest.read_until(THEME_PROMPT)
    require("Current: Custom" in output, f"Setup misidentified custom colors: {output!r}")
    guest.send(b"\x03")
    guest.read_until(PROMPT)
    check_contains(guest, "settings", "Custom (12 on 4)")
    check_contains(guest, "color 13 0", "VGA color saved.")
    check_profile(guest)
    print("PASS: canceled setup retains custom colors")


def check_apps(guest, snake_screenshot=None):
    for name in ("snake", "notes", "guess"):
        output = guest.command(name)
        require("not installed" in output.lower(), f"Uninstalled {name} ran: {output!r}")
    output = guest.command("Grapemarket -L")
    for name in ("snake", "notes", "guess"):
        require(name in "\n".join(result_lines(output)), f"Catalog omitted {name}: {output!r}")
    for command in ("Grapemarket -Z", "Grapemarket -I missing", "Grapemarket -I snake extra"):
        output = guest.command(command)
        require(re.search(r"usage|unknown|expected|invalid|available|package", output, re.I),
                f"Invalid market arguments accepted: {output!r}")
    guest.command("Grapemarket -I snake")
    guest.command("Grapemarket -I notes")
    guest.command("Grapemarket -I guess")
    check_contains(guest, "Grapemarket -I snake", "already installed")
    output = guest.command("Grapemarket -S")
    for name in ("snake", "notes", "guess"):
        require(name in "\n".join(result_lines(output)), f"Installed list omitted {name}: {output!r}")
    check_contains(guest, "notes", "Notes are empty.")
    check_contains(guest, "notes set temporary-note", "Note saved.")
    check_contains(guest, "notes", "temporary-note")
    check_contains(guest, "notes clear extra", "notes: expected")
    check_contains(guest, "notes", "temporary-note")
    check_contains(guest, "notes clear", "Notes cleared.")
    check_contains(guest, "notes", "Notes are empty.")
    check_contains(guest, "notes set persistent-smoke-note", "Note saved.")
    check_contains(guest, "guess nope", "guess: no arguments expected.")
    guest.send(b"guess\r")
    guest.read_until(b"Guess> ")
    for number in (b"0", b"101", b"1000"):
        guest.send(number + b"\r")
        output = guest.read_until(b"Guess> ")
        require("Enter a number from 1 to 100." in output, f"Guess accepted invalid number: {output!r}")
    guest.send(b"999\x15")
    minimum, maximum = 1, 100
    for _ in range(7):
        number = (minimum + maximum) // 2
        guest.send(str(number).encode() + b"\r")
        output = guest.read_until(b"Guess> ")
        if "Correct!" in output:
            break
        if "Too low." in output:
            minimum = number + 1
        elif "Too high." in output:
            maximum = number - 1
        else:
            raise AssertionError(f"Guess feedback missing: {output!r}")
    else:
        raise AssertionError("Guess hints did not find the target in seven attempts")
    guest.send(b"r")
    output = guest.read_until(b"Guess> ")
    require("Guess started" in output, f"Guess did not restart: {output!r}")
    guest.send(b"q")
    output = guest.read_until(PROMPT)
    require("Guess closed" in output, f"Guess did not close: {output!r}")
    guest.command("Grapemarket -R guess")
    check_contains(guest, "Grapemarket -R guess", "not installed")
    output = guest.command("guess")
    require("not installed" in output.lower(), f"Removed app remained runnable: {output!r}")
    check_contains(guest, "snake nope", "snake: no arguments expected.")
    guest.send(b"snake\r")
    guest.read_until(b"Snake started")
    guest.send(b"p")
    guest.read_until(b"Snake paused")
    time.sleep(0.05)
    paused_cells, _ = guest.vga_cells()
    require(b"Paused." in paused_cells and b"@" in paused_cells,
            "Snake did not draw the paused board")
    if snake_screenshot:
        guest.screenshot(snake_screenshot)
    time.sleep(0.25)
    same_cells, _ = guest.vga_cells()
    require(paused_cells == same_cells, "Snake moved while paused")
    guest.send(b"p")
    guest.read_until(b"Snake resumed")
    guest.read_until(b"Snake score: 1", timeout=3)
    guest.send(b"q")
    output = guest.read_until(PROMPT)
    require("Snake closed" in output and "Snake high score saved." in output,
            f"Snake did not save the score and return to shell: {output!r}")
    print("PASS: offline apps, notes CRUD, Guess hints/restart, Snake pause/food/score")


def check_saved_apps(guest):
    check_contains(guest, "notes", "persistent-smoke-note")
    output = guest.command("Grapemarket -S")
    for name in ("snake", "notes"):
        require(name in "\n".join(result_lines(output)), f"Saved app missing {name}: {output!r}")
    output = guest.command("guess")
    require("not installed" in output.lower(), f"Removed Guess returned after reboot: {output!r}")
    guest.send(b"snake\r")
    guest.read_until(b"Snake started")
    guest.send(b"p")
    guest.read_until(b"Snake paused")
    time.sleep(0.05)
    characters, _ = guest.vga_cells()
    require(b"Best: 1" in characters, "Snake high score was not restored")
    guest.send(b"q")
    output = guest.read_until(PROMPT)
    require("Snake closed" in output, f"Persisted Snake did not close: {output!r}")


def cold_boot_checks(guest):
    guest.boot_from_menu()
    startup = guest.read_until(PROMPT)
    require(BANNER.decode() in startup and "Welcome to GrapeOS" not in startup,
            f"Cold boot lost saved setup: {startup!r}")
    check_profile(guest)
    cancel_profile_edit(guest)
    cancel_custom_theme_edit(guest)
    check_saved_apps(guest)
    print("PASS: new VM restores profile, apps, removal, note, and earned high score")


def menu_crlf_check(guest):
    guest.boot_from_menu(selection=b"\r\n")
    guest.send(b"\r\n\r\nnot-a-profile\r\n")
    guest.ps2_key("ret")
    startup = guest.read_until(USERNAME_PROMPT)
    require("Welcome to GrapeOS" in startup, f"Missing virgin setup after CRLF selection: {startup!r}")
    try:
        guest.read_until(HOSTNAME_PROMPT, timeout=0.2)
    except AssertionError as error:
        require("Timed out" in str(error), f"Unexpected wizard state after boot selection: {error}")
    else:
        raise AssertionError("Boot-menu/log-pause input silently submitted the default username")
    characters, _ = guest.vga_cells()
    require(b"not-a-profile" not in characters, "Input during boot logs prefilled setup")
    require(not any(guest.data_disk.read_bytes()), "Boot choice prematurely saved a setup profile")
    print("PASS: menu CRLF and serial/PS2 input during logs cannot prefill setup")


def check_cpu_halted(guest, reason):
    time.sleep(0.1)
    registers = guest.monitor_command("info registers")
    require("HLT=1" in registers, f"{reason} did not stop the CPU: {registers}")
    flags = re.search(r"EFL=([0-9a-fA-F]+)", registers)
    require(flags and not (int(flags.group(1), 16) & 0x200),
            f"{reason} left CPU interrupts enabled: {registers}")
    guest.send(b"\r")
    try:
        guest.read_until(PROMPT, timeout=0.4)
    except AssertionError as error:
        require("Timed out" in str(error), f"Unexpected {reason} state: {error}")
    else:
        raise AssertionError(f"{reason} returned to the shell")


def reset_to_saved_desktop(guest):
    guest.monitor_command("system_reset")
    guest.boot_from_menu()
    startup = guest.read_until(PROMPT)
    require(BANNER.decode() in startup and "Welcome to GrapeOS" not in startup,
            f"Monitor reset lost the saved desktop: {startup!r}")
    check_profile(guest)
    check_saved_apps(guest)


def check_simulated_panic(guest, screenshot=None):
    reset_to_saved_desktop(guest)
    saved_disk = guest.data_disk.read_bytes()
    for command in ("del", "del disk", "del ram extra", "del RAM"):
        output = guest.command(command)
        require("KERNEL PANIC" not in output and "NO_USABLE_RAM" not in output,
                f"Invalid del command caused a panic: {output!r}")
        require(re.search(r"usage|expected|only|simulate", "\n".join(result_lines(output)), re.I),
                f"Invalid del command was not rejected: {output!r}")
    guest.send(b"de\t\r")
    output = guest.read_until(PROMPT)
    require("del " in output.splitlines()[0] and "KERNEL PANIC" not in output,
            f"Tab did not safely complete del: {output!r}")
    guest.send(b"del ram\r")
    output = guest.read_until(b"CPU halted. Restart the virtual machine to continue.")
    for marker in ("KERNEL PANIC", "NO_USABLE_RAM", "No usable RAM (simulated)"):
        require(marker in output, f"Simulated panic omitted {marker}: {output!r}")
    require("grape> " not in output, f"Panic returned a shell prompt: {output!r}")
    check_cpu_halted(guest, "simulated panic")
    require(guest.data_disk.read_bytes() == saved_disk, "Simulated RAM panic modified stored data")
    if screenshot:
        guest.screenshot(screenshot)
    reset_to_saved_desktop(guest)
    print("PASS: strict del arguments, Tab completion, simulated panic halt, reset recovery")


def run_checks(guest, screenshot=None, screenshot_command="help", setup_screenshot=None, snake_screenshot=None,
               menu_screenshot=None, log_screenshot=None, panic_screenshot=None):
    startup = first_setup(guest, setup_screenshot, menu_screenshot, log_screenshot)
    require(BANNER.decode() in startup, f"Missing boot banner: {startup!r}")
    check_profile(guest)
    print("PASS: BIOS boot, protected-mode kernel, serial prompt")

    output = guest.command("help")
    for name in ("help", "clear", "echo", "info", "neofetch", "mem", "uptime", "calc", "color", "history", "reboot", "halt", "setup", "settings", "whoami", "hostname", "ver", "Grapemarket", "apps", "snake", "guess", "notes", "ls", "cat", "fortune", "del"):
        require(name.lower() in "\n".join(result_lines(output)).lower(), f"help omitted {name}: {output!r}")
    check_contains(guest, "echo kernel-smoke-ok", "kernel-smoke-ok")
    guest.command("clear")
    check_contains(guest, "echo after-clear", "after-clear")
    check_contains(guest, "info", "GrapeOS")
    output = guest.command("mem")
    require(re.search(r"[0-9]+", "\n".join(result_lines(output))), f"Missing memory size: {output!r}")
    output = guest.command("ls")
    for name in ("welcome.txt", "commands.txt", "profile.txt", "catalog.txt", "note.txt"):
        require(name in "\n".join(result_lines(output)), f"ls omitted {name}: {output!r}")
    check_contains(guest, "cat welcome.txt", "Welcome to GrapeOS!")
    check_contains(guest, "cat missing.txt", "cat: no such file:")
    output = guest.command("fortune")
    require(result_lines(output), f"fortune did not return a thought: {output!r}")
    check_contains(guest, "hostname -invalid-", "hostname: use 1..23")
    check_contains(guest, "hostname", "grape-box")
    print("PASS: help, echo, info, kernel memory layout")

    guest.command("color 10 2")
    guest.command("clear")
    output = guest.command("neofetch")
    for field in ("GrapeOS 0.1", "Kernel:", "CPU:", "Uptime:", "Shell:", "Resolution:"):
        require(field in "\n".join(result_lines(output)), f"neofetch omitted {field}: {output!r}")
    check_contains(guest, "echo theme-restored", "theme-restored")
    characters, attributes = guest.vga_cells()
    index = characters.rfind(b"theme-restored")
    require(index >= 0 and all(attribute == 0x2a for attribute in attributes[index:index + 14]),
            "neofetch did not restore the chosen VGA foreground/background")
    check_contains(guest, "neofetch nope", "neofetch: no arguments expected.")
    guest.command("color 7 0")
    guest.command("clear")
    print("PASS: neofetch system fields, argument rejection, VGA theme restoration")

    for expression, result in (
        ("7 + 5", 12), ("7 - 12", -5), ("-6 * 7", -42), ("-19 / 4", -4),
        ("-19 % 4", -3), ("2147483647 + 0", 2147483647),
        ("-2147483648 + 0", -2147483648),
        ("-2147483648 / 2", -1073741824), ("-2147483648 % 2", 0),
        ("+10 + +2", 12), ("   -9    +   4   ", -5),
    ):
        check_calc(guest, expression, result)
    for expression in (
        "1 / 0", "1 % 0", "2147483647 + 1", "-2147483648 - 1",
        "2147483647 - -1", "46341 * 46341", "-2147483648 * -1",
        "-2147483648 / -1", "-2147483648 % -1", "2147483648 + 0",
        "-2147483649 + 0", "2 ^ 3", "1 +",
    ):
        output = guest.command("calc " + expression)
        require(re.search(r"error|invalid|overflow|zero|usage|expected", "\n".join(result_lines(output)), re.I),
                f"calc {expression!r} should fail: {output!r}")
    print("PASS: signed arithmetic, limits, malformed input, division by zero")

    output = guest.command("not-a-command")
    require(re.search(r"unknown|not found", "\n".join(result_lines(output)), re.I), f"Unknown command: {output!r}")
    for command in ("color 16 0", "color 7 8", "color bad"):
        output = guest.command(command)
        require(re.search(r"error|invalid|usage|range|0\.\.15", "\n".join(result_lines(output)), re.I), f"Invalid color: {output!r}")
    guest.command("color 10 0")
    guest.command("color 7 0")
    guest.send(b"echo serial-badx\x08\x08ok\r")
    output = guest.read_until(PROMPT)
    require("serial-baok" in "\n".join(result_lines(output)), f"Backspace: {output!r}")
    guest.send(b"discard this\x15echo ctrl-u-ok\r")
    output = guest.read_until(PROMPT)
    require("ctrl-u-ok" in "\n".join(result_lines(output)), f"Ctrl+U: {output!r}")
    check_contains(guest, "echo history-ok", "history-ok")
    guest.send(b"\x1b[A\r")
    output = guest.read_until(PROMPT)
    require("history-ok" in "\n".join(result_lines(output)), f"History up: {output!r}")
    output = guest.command("history")
    require("echo history-ok" in "\n".join(result_lines(output)), f"history: {output!r}")
    guest.send(b"echo draft-ok\x1b[A\x1b[B\r")
    output = guest.read_until(PROMPT)
    require("draft-ok" in result_lines(output), f"History down lost draft: {output!r}")
    guest.send(b"ec\tcompletion-ok\r")
    output = guest.read_until(PROMPT)
    require("completion-ok" in result_lines(output), f"Tab completion: {output!r}")
    output = guest.command("   echo    whitespace-ok")
    require("whitespace-ok" in result_lines(output), f"Whitespace parsing: {output!r}")
    output = guest.command("   ")
    require(not result_lines(output), f"Whitespace-only line ran a command: {output!r}")
    guest.send(b"echo crlf-ok\r\n")
    output = guest.read_until(PROMPT)
    require("crlf-ok" in result_lines(output), f"CRLF command: {output!r}")
    try:
        guest.read_until(PROMPT, timeout=0.2)
    except AssertionError as error:
        require("Timed out" in str(error), f"Unexpected CRLF state: {error}")
    else:
        raise AssertionError("CRLF emitted a second prompt")
    guest.send(b"echo canceled\x03")
    output = guest.read_until(PROMPT)
    require("^C" in output and "canceled" not in result_lines(output),
            f"Ctrl+C did not cancel input: {output!r}")
    check_contains(guest, "echo after-cancel", "after-cancel")
    guest.send(b"echo clear-kept\x0c")
    guest.read_until(PROMPT)
    guest.send(b"\r")
    output = guest.read_until(PROMPT)
    require("clear-kept" in result_lines(output), f"Ctrl+L lost the input line: {output!r}")
    check_contains(guest, "echo " + "x" * 122, "x" * 122)
    output = guest.command("echo " + "x" * 300)
    require("Input too long (maximum 127 characters)." in result_lines(output),
            f"Overlong input was not rejected: {output!r}")
    guest.send(b"echo " + b"x" * 128 + b"\x1b[A\x1b[B\r")
    output = guest.read_until(PROMPT)
    require("Input too long (maximum 127 characters)." in result_lines(output),
            f"History roundtrip lost overlong draft state: {output!r}")
    check_contains(guest, "echo after-long-input", "after-long-input")
    print("PASS: color bounds, editing, history draft, completion, CRLF, input bounds")

    def uptime():
        output = guest.command("uptime")
        numbers = re.findall(r"[0-9]+", "\n".join(result_lines(output)))
        require(numbers, f"uptime is not numeric: {output!r}")
        return tuple(map(int, numbers))
    first = uptime()
    time.sleep(1.15)
    second = uptime()
    require(second > first, f"PIT uptime did not advance: {first} -> {second}")
    print("PASS: PIT interrupts advance uptime")

    guest.ps2_type("echo ps2-smoke-ok\n")
    output = guest.read_until(PROMPT)
    require("ps2-smoke-ok" in "\n".join(result_lines(output)), f"PS/2 keyboard: {output!r}")
    check_contains(guest, "echo mixed-input-ok", "mixed-input-ok")
    guest.ps2_key("up")
    guest.ps2_key("ret")
    output = guest.read_until(PROMPT)
    require("mixed-input-ok" in result_lines(output),
            f"Serial CR swallowed PS/2 Enter after history recall: {output!r}")
    guest.send(b"echo ")
    for key in ("shift-a", "shift-1", "ret"):
        guest.ps2_key(key)
    output = guest.read_until(PROMPT)
    require("A!" in result_lines(output), f"PS/2 Shift and punctuation: {output!r}")
    guest.send(b"echo ")
    for key in ("caps_lock", "a", "shift-a", "caps_lock", "ret"):
        guest.ps2_key(key)
    output = guest.read_until(PROMPT)
    require("Aa" in result_lines(output), f"PS/2 Caps Lock/Shift: {output!r}")
    guest.send(b"echo ps2-draft")
    for key in ("up", "down", "ret"):
        guest.ps2_key(key)
    output = guest.read_until(PROMPT)
    require("ps2-draft" in result_lines(output), f"PS/2 history keys: {output!r}")
    guest.send(b"echo discard")
    guest.ps2_key("ctrl-u")
    check_contains(guest, "echo ps2-control-ok", "ps2-control-ok")
    print("PASS: PS/2 interrupts, Shift/Caps Lock, punctuation, history, Control")

    guest.command("color 13 0")
    check_apps(guest, snake_screenshot)

    guest.send(b"reboot\r")
    guest.boot_from_menu()
    output = guest.read_until(PROMPT)
    require(BANNER.decode() in output, f"reboot did not boot the kernel again: {output!r}")
    require("Welcome to GrapeOS" not in output, "Saved setup was repeated after reboot")
    check_profile(guest)
    check_saved_apps(guest)
    if screenshot:
        guest.command("clear")
        guest.command(screenshot_command)
        guest.screenshot(screenshot)
    check_contains(guest, "echo reboot-ok", "reboot-ok")
    print("PASS: reboot returns to a working shell")

    guest.send(b"halt\r")
    guest.read_until(b"CPU halted.", timeout=3)
    check_cpu_halted(guest, "halt")
    print("PASS: halt stops the guest CPU")
    check_simulated_panic(guest, panic_screenshot)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", nargs="?", default=str(Path(__file__).resolve().parents[1] / "build/grapeos.img"))
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    parser.add_argument("--bios-dir", default=os.environ.get("QEMU_BIOS_DIR"))
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--screenshot", help="save a PNG of the rebooted VGA console")
    parser.add_argument("--screenshot-command", default="help", choices=("help", "neofetch"),
                        help="command to display on a fresh screen for the screenshot")
    parser.add_argument("--setup-screenshot", help="save a PNG of the first-boot welcome screen")
    parser.add_argument("--snake-screenshot", help="save a PNG of the paused Snake board")
    parser.add_argument("--boot-menu-screenshot", help="save a PNG of GrapeLoad before OS selection")
    parser.add_argument("--boot-log-screenshot", help="save a PNG of the initialization logs")
    parser.add_argument("--panic-screenshot", help="save a PNG of the simulated RAM panic")
    args = parser.parse_args()
    if not Path(args.image).is_file():
        parser.error(f"image not found: {args.image}; build it with make first")
    qemu = shutil.which(args.qemu)
    if qemu is None:
        parser.error(f"QEMU not found: {args.qemu}; install QEMU or set QEMU")
    with tempfile.TemporaryDirectory(prefix="grapeos-smoke-storage-") as storage:
        data_disk = Path(storage) / "data.img"
        guest = Guest(str(Path(args.image).resolve()), qemu, args.bios_dir, args.timeout, data_disk)
        try:
            run_checks(guest, args.screenshot, args.screenshot_command,
                       args.setup_screenshot, args.snake_screenshot,
                       args.boot_menu_screenshot, args.boot_log_screenshot,
                       args.panic_screenshot)
        except (AssertionError, OSError, subprocess.SubprocessError) as error:
            print(f"FAIL: {error}", file=sys.stderr)
            return 1
        finally:
            guest.close()
        guest = Guest(str(Path(args.image).resolve()), qemu, args.bios_dir, args.timeout)
        try:
            menu_crlf_check(guest)
        except (AssertionError, OSError, subprocess.SubprocessError) as error:
            print(f"FAIL: {error}", file=sys.stderr)
            return 1
        finally:
            guest.close()
        guest = Guest(str(Path(args.image).resolve()), qemu, args.bios_dir, args.timeout, data_disk)
        try:
            cold_boot_checks(guest)
        except (AssertionError, OSError, subprocess.SubprocessError) as error:
            print(f"FAIL: {error}", file=sys.stderr)
            return 1
        finally:
            guest.close()
    print("All kernel smoke checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
