#!/usr/bin/env python3
"""Exercise the real PAE renderer with a synthetic framebuffer above 4 GiB.

At the Multiboot entry, redirect the framebuffer tag to a high RAM DIMM.
Inspect actual rendered bytes there; this test doesn't emulate a GPU driver.
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import time
from smoke import Guest, BOOT_READY, require
from usb import defaults

ROOT = Path(__file__).resolve().parents[1]

class Debugger:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(25)
        self.socket.connect(str(path))
    def packet(self):
        while self.socket.recv(1) != b"$": pass
        data = bytearray()
        while True:
            char = self.socket.recv(1)
            if char == b"#": break
            if not char: raise RuntimeError("GDB disconnected")
            data.extend(char)
        checksum = self.socket.recv(2)
        require(int(checksum, 16) == sum(data) % 256, "Invalid GDB checksum")
        self.socket.sendall(b"+")
        return bytes(data)
    def request(self, command, wait=True):
        data = command.encode("ascii")
        self.socket.sendall(b"$"+data+b"#"+f"{sum(data)%256:02x}".encode())
        require(self.socket.recv(1) == b"+", "GDB did not acknowledge request")
        return self.packet() if wait else None

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    args = parser.parse_args()
    symbols = subprocess.check_output(["nm", str(ROOT / "build/grapeos-uefi.elf")], text=True)
    entry = int(re.search(r"^([0-9a-f]+) T multiboot_start$", symbols, re.M)[1], 16)
    with tempfile.TemporaryDirectory(prefix="grapeos-highfb-") as temp:
        path = Path(temp) / "gdb.sock"
        extra = ["-m", "256M,slots=1,maxmem=384M", "-object", "memory-backend-ram,id=highmem,size=128M",
                 "-device", "pc-dimm,memdev=highmem,id=high-dimm",
                 "-gdb", f"unix:{path},server=on,wait=off"]
        guest = Guest(ROOT / "build/grapeos-uefi.img", args.qemu, os.environ.get("QEMU_BIOS_DIR"), 25,
                      boot_media="uefi", attach_data=False,
                      ovmf=os.environ.get("OVMF_CODE", str(ROOT / ".runtime/qemu/usr/share/edk2/x64/OVMF_CODE.4m.fd")),
                      extra_args=extra)
        debugger = None
        try:
            guest.read_until(b"Press Enter to boot GrapeOS")
            time.sleep(.7)
            devices = guest.monitor_command("info memory-devices")
            physical = int(re.search(r"addr:\s*(0x[0-9a-f]+|[0-9]+)", devices)[1], 0)
            require(physical >= 0x100000000, "Test DIMM is not above 4 GiB")
            debugger = Debugger(path)
            require(debugger.request(f"Z1,{entry:x},1") == b"OK", "Cannot set entry breakpoint")
            debugger.request("c", wait=False)
            guest.ps2_key("ret")
            require(debugger.packet()[:1] in (b"T", b"S"), "Kernel did not stop at entry")
            registers = guest.monitor_command("info registers")
            information = int(re.search(r"[ER]BX=([0-9a-fA-F]+)", registers)[1], 16)
            total = struct.unpack("<I", bytes.fromhex(debugger.request(f"m{information:x},4").decode()))[0]
            tags = bytes.fromhex(debugger.request(f"m{information:x},{total:x}").decode())
            offset = 8
            while offset < total:
                kind, length = struct.unpack_from("<II", tags, offset)
                if kind == 8: break
                require(kind != 0 and length >= 8, "No framebuffer tag in test boot")
                offset += (length + 7) & ~7
            pitch, width, height = struct.unpack_from("<III", tags, offset + 16)
            address_bytes = struct.pack("<Q", physical).hex()
            require(debugger.request(f"M{information+offset+8:x},8:{address_bytes}") == b"OK",
                    "Cannot inject high framebuffer tag")
            debugger.request(f"z1,{entry:x},1")
            debugger.request("c", wait=False)
            guest.read_until(BOOT_READY)
            defaults(guest)
            guest.send(b"gui\r")
            guest.read_until(b"[GUI] Desktop ready.")
            time.sleep(.7)
            guest.monitor_command("stop")
            output = Path(temp) / "pixels.bin"
            guest.monitor_command(f"pmemsave {physical:#x} {pitch*height} {json.dumps(str(output))}")
            data = output.read_bytes()
            colors = set(struct.iter_unpack("<I", data))
            require(len(colors) >= 6, f"High framebuffer has no GUI colors: {colors}")
            require(sum(value != (0,) for value in struct.iter_unpack("<I", data)) > 100000,
                    "High framebuffer contains too few drawn pixels")
            print(f"PASS: i386 PAE renders {width}x{height} GOP GUI at physical {physical:#x} above 4 GiB")
        finally:
            if debugger: debugger.socket.close()
            guest.close()

if __name__ == "__main__": main()
