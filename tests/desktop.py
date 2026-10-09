"""Shared end-to-end checks for RAM files, editor, calculator and PS/2 mouse."""
import time
from pathlib import Path
from smoke import PROMPT, require, check_contains

def commands(guest):
    check_contains(guest, "pwd", "/ram")
    check_contains(guest, "cpu", "CPU")
    check_contains(guest, "touch draft.txt", "OK")
    check_contains(guest, "write draft.txt Hello Grape", "OK")
    check_contains(guest, "touch draft.txt", "OK")
    check_contains(guest, "cat draft.txt", "Hello Grape")
    check_contains(guest, "wc draft.txt", "1 lines, 2 words, 11 bytes")
    check_contains(guest, "cp draft.txt copy.txt", "OK")
    check_contains(guest, "cp draft.txt copy.txt", "destination already exists")
    check_contains(guest, "mv copy.txt moved.txt", "OK")
    check_contains(guest, "cat moved.txt", "Hello Grape")
    check_contains(guest, "write note.txt unsafe", "protected")
    check_contains(guest, "rm welcome.txt", "protected")
    check_contains(guest, "touch ../bad", "use letters")
    check_contains(guest, "touch " + "a" * 24, "1..23")
    for i in range(6): check_contains(guest, f"touch f{i}.txt", "OK")
    check_contains(guest, "touch overflow.txt", "folder is full")
    check_contains(guest, "mv f0.txt rename.txt", "OK")
    check_contains(guest, "cp draft.txt overflow.txt", "folder is full")
    for name in ["draft.txt", "moved.txt", "rename.txt"] + [f"f{i}.txt" for i in range(1, 6)]:
        check_contains(guest, "rm " + name, "OK")
    check_contains(guest, "write edit.txt abc", "OK")
    print("PASS: RAM file CRUD, copy/rename, bounds, protected built-in files, word counts")

def click(guest):
    guest.monitor_command("mouse_button 1")
    time.sleep(.1)
    guest.monitor_command("mouse_button 0")
    time.sleep(.1)

def move(guest, dx, dy):
    # Stay within signed PS/2 packet deltas; wait for each packet to drain.
    while dx or dy:
        x = max(-80, min(80, dx)); y = max(-80, min(80, dy))
        guest.monitor_command(f"mouse_move {x} {y}")
        dx -= x; dy -= y
        time.sleep(.06)

def capture(guest, name):
    # GUI output is capped at 20 Hz; serial completion precedes presentation.
    time.sleep(.2)
    guest.screenshot(Path(__file__).resolve().parents[1] / "build" / name)

def apps(guest):
    guest.ps2_key("ctrl-f"); guest.read_until(b"[GUI] Files opened.")
    guest.ps2_key("down"); guest.ps2_key("ret")
    guest.read_until(b"[GUI] Notepad opened.")
    guest.ps2_key("left"); guest.ps2_type("X")
    guest.ps2_key("home"); guest.ps2_key("delete")
    guest.ps2_key("end"); guest.ps2_type("d")
    guest.ps2_key("ctrl-s"); guest.read_until(b"[GUI] RAM file saved.")
    capture(guest, "grapeos-editor.png")
    guest.ps2_key("ctrl-t"); guest.read_until(b"[GUI] Terminal opened."); guest.read_until(PROMPT)
    check_contains(guest, "cat edit.txt", "bXcd")
    guest.ps2_type("echo draft-kept")
    guest.ps2_key("ctrl-k"); guest.read_until(b"[GUI] Calculator opened.")
    guest.ps2_type("7 * 8"); guest.ps2_key("ret")
    guest.read_until(b"[GUI] Calculator: = 56")
    capture(guest, "grapeos-calculator.png")
    guest.ps2_key("ctrl-u"); guest.ps2_type("9 / 0"); guest.ps2_key("ret")
    guest.read_until(b"[GUI] Calculator: calc: division by zero")
    guest.ps2_key("ctrl-t"); guest.read_until(b"[GUI] Terminal opened."); guest.read_until(PROMPT)
    guest.ps2_key("ret")
    require("draft-kept" in guest.read_until(PROMPT), "App switching discarded terminal input")
    check_contains(guest, "echo terminal-survives-switch", "terminal-survives-switch")
    guest.ps2_key("ctrl-f"); guest.read_until(b"[GUI] Files opened.")
    capture(guest, "grapeos-files.png")
    guest.ps2_key("up"); guest.ps2_key("ret"); guest.read_until(b"[GUI] Notepad opened.")
    guest.ps2_key("esc"); guest.read_until(b"[GUI] Desktop ready.")
    # Open via the dock, drag the title bar, then minimize via its button.
    move(guest, -100, 220); click(guest)
    guest.read_until(b"[GUI] Terminal opened."); guest.read_until(PROMPT)
    move(guest, -140, -370)
    guest.monitor_command("mouse_button 1"); time.sleep(.1)
    move(guest, 30, -20)
    guest.monitor_command("mouse_button 0"); time.sleep(.1)
    move(guest, 496, -4); click(guest)
    guest.read_until(b"[GUI] Desktop ready.")
    guest.ps2_key("ctrl-k"); guest.read_until(b"[GUI] Calculator opened.")
    guest.ps2_key("ctrl-b"); guest.read_until(b"[GUI] Desktop ready.")
    print("PASS: cursor insert/delete, RAM editor save, calculator/errors, mouse dock/drag/minimize")
