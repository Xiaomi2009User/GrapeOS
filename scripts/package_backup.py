#!/usr/bin/env python3
"""Package clean boot media and a source backup without local user data."""
import hashlib
from pathlib import Path
import subprocess
import zipfile

from build_uefi import assemble, DESKTOP_README

ROOT = Path(__file__).resolve().parents[1]


def main():
    downloads = ROOT / 'downloads'
    downloads.mkdir(exist_ok=True)
    efi = (ROOT / 'build/BOOTX64-desktop.EFI').read_bytes()
    image = assemble(efi, desktop=True)
    instructions = (
        'GrapeOS Desktop - UEFI x86-64\n'
        'Disable Secure Boot. Enter in GrapeLoad starts GrapeOS.\n'
        'grapeos-desktop.img: clean full USB image; writing it replaces the target USB contents.\n'
        'Existing GrapeOS USB: replace only EFI/BOOT/BOOTX64.EFI; keep your GRAPE folder.\n'
        'The image has no personal profile or documents.\n\n'
    ).encode() + DESKTOP_README
    entries = {
        'grapeos-desktop.img': image,
        'EFI/BOOT/BOOTX64.EFI': efi,
        'GRAPE/images.png': (ROOT / 'assets/images.png').read_bytes(),
        'README.TXT': instructions,
    }
    entries['LICENSE'] = (ROOT / 'LICENSE').read_bytes()
    entries['COPYRIGHT.md'] = (ROOT / 'COPYRIGHT.md').read_bytes()
    entries['licenses/Terminus-OFL.txt'] = (ROOT / 'assets/framebuffer-font.OFL.txt').read_bytes()
    entries['licenses/stb_image.h'] = (ROOT / 'third_party/stb/stb_image.h').read_bytes()
    for path in sorted((ROOT / 'third_party/gnu-efi').iterdir()):
        if path.is_file(): entries['licenses/gnu-efi/' + path.name] = path.read_bytes()
    entries['SHA256SUMS'] = ''.join(
        f'{hashlib.sha256(data).hexdigest()}  {name}\n' for name, data in entries.items()
    ).encode()
    binary = downloads / 'grapeos-desktop.zip'
    with zipfile.ZipFile(binary, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in entries.items():
            archive.writestr(name, data)
    (downloads / 'SHA256SUMS').write_text(f'{hashlib.sha256(binary.read_bytes()).hexdigest()}  {binary.name}\n')
    with zipfile.ZipFile(binary) as archive:
        assert archive.testzip() is None
        assert archive.read('EFI/BOOT/BOOTX64.EFI') == efi

    backup = ROOT / 'backup'
    backup.mkdir(exist_ok=True)
    names = subprocess.check_output(
        ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT
    ).decode().split('\0')
    source = backup / 'grapeos-project.zip'
    included = sorted({name for name in names if name and (ROOT / name).is_file()})
    assert not any(name.startswith(('.git/', '.runtime/', 'build/', 'data/', 'backup/')) for name in included)
    with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in included:
            archive.write(ROOT / name, 'grapeos/' + name)
    with zipfile.ZipFile(source) as archive:
        assert archive.testzip() is None
        assert 'grapeos/downloads/grapeos-desktop.zip' in archive.namelist()
    print(f'Boot ZIP: {binary} ({binary.stat().st_size} bytes)')
    print(f'Project ZIP: {source} ({source.stat().st_size} bytes, {len(included)} files)')


if __name__ == '__main__':
    main()
