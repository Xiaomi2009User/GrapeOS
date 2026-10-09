#!/usr/bin/env python3
"""Extract ASCII glyphs from a native 6x12 Terminus PSF2 console font."""
import argparse
import gzip
from pathlib import Path
import struct

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = args.source.read_bytes()
    if args.source.suffix == '.gz': data = gzip.decompress(data)
    magic, version, header, flags, count, stride, height, width = struct.unpack_from('<8I', data)
    if magic != 0x864ab572 or version != 0 or (width, height, stride) != (6, 12, 12) or not flags & 1:
        raise ValueError('Expected a 6x12 PSF2 font with a Unicode table')
    glyphs = {}
    table = data[header + count * stride:].split(b'\xff')
    for index in range(count):
        names = table[index].split(b'\xfe', 1)[0].decode('utf-8')
        for character in names:
            if ord(character) < 128:
                glyphs[ord(character)] = data[header + index * stride:header + (index + 1) * stride]
    if not all(code in glyphs for code in range(32, 127)):
        raise ValueError('ASCII glyphs are missing')
    lines = ['/* Native Terminus Font 4.49.1 (6x12 normal), without resampling.',
             ' * Copyright (c) Dimitar Toshkov Zhekov; SIL OFL 1.1.',
             ' * See framebuffer-font.OFL.txt. */',
             'static const u8 terminal_font[128][12] = {']
    for code in range(128):
        bitmap = glyphs.get(code, bytes(12))
        lines.append('    {' + ','.join(f'0x{byte:02x}' for byte in bitmap) + '},')
    lines.append('};')
    args.output.write_text('\n'.join(lines) + '\n')

if __name__ == '__main__': main()
