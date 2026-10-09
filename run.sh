#!/usr/bin/env bash
# Build and boot GrapeOS with either an installed or the local temporary QEMU.
set -euo pipefail

usage() {
    printf '%s\n' \
        'Використання: ./run.sh [--usb | --uefi | --desktop] [--headless | --test] [-- додаткові параметри QEMU]' \
        '  Без параметрів — вікно GrapeOS.' \
        '  --headless     — консоль у цьому терміналі (вихід: Ctrl+A, потім X).' \
        '  --test         — перевірки ядра у QEMU.' \
        '  --usb          — тест USB-образу для реального ПК (дані лише на сесію).' \
        '  --uefi         — UEFI x64 USB-образ для ASUS; потрібен OVMF.' \
        '  --desktop      — новий UEFI GUI, курсор і справжні FAT-файли; потрібен OVMF.' \
        '  QEMU та QEMU_BIOS_DIR можна задати через змінні середовища.'
}

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

mode=window
media=floppy
while (($#)); do
    case "$1" in
        --headless) mode=headless ;;
        --test) mode=test ;;
        --usb) media=usb ;;
        --uefi) media=uefi ;;
        --desktop) media=desktop ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        *) usage >&2; fail "Невідомий параметр: $1" ;;
    esac
    shift
done

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
runtime="$project_dir/.runtime/qemu"

if [[ -n ${QEMU:-} ]]; then
    qemu="$(command -v -- "$QEMU")" || fail "Не знайдено QEMU: $QEMU"
elif command -v qemu-system-i386 >/dev/null 2>&1; then
    qemu="$(command -v qemu-system-i386)"
elif command -v qemu-system-x86_64 >/dev/null 2>&1; then
    qemu="$(command -v qemu-system-x86_64)"
elif [[ -x "$runtime/usr/bin/qemu-system-i386" ]]; then
    qemu="$runtime/usr/bin/qemu-system-i386"
elif [[ -x /tmp/grapeos-qemu/root/usr/bin/qemu-system-i386 ]]; then
    runtime=/tmp/grapeos-qemu/root
    qemu="$runtime/usr/bin/qemu-system-i386"
else
    fail 'Не знайдено QEMU. В Arch Linux встанови: sudo pacman -S qemu-system-x86 qemu-ui-gtk'
fi

if [[ $media == uefi || $media == desktop ]]; then
    if [[ $qemu == */qemu-system-i386 && -x ${qemu%qemu-system-i386}qemu-system-x86_64 ]]; then
        qemu="${qemu%qemu-system-i386}qemu-system-x86_64"
    fi
    ovmf_code="${OVMF_CODE:-}"
    if [[ -z $ovmf_code ]]; then
        for candidate in "$runtime/usr/share/edk2/x64/OVMF_CODE.4m.fd" \
            /usr/share/edk2/x64/OVMF_CODE.4m.fd /usr/share/OVMF/OVMF_CODE_4M.fd; do
            if [[ -f $candidate ]]; then ovmf_code="$candidate"; break; fi
        done
    fi
    [[ -f $ovmf_code ]] || fail 'Не знайдено OVMF. В Arch: sudo pacman -S edk2-ovmf. Або задай OVMF_CODE.'
    ovmf_code="$(realpath -- "$ovmf_code")"
    ovmf_vars="${OVMF_VARS:-${ovmf_code%/*}/OVMF_VARS.4m.fd}"
    [[ -f $ovmf_vars ]] || ovmf_vars="${ovmf_code%/*}/OVMF_VARS_4M.fd"
    [[ -f $ovmf_vars ]] || fail 'Не знайдено шаблон OVMF_VARS; задай OVMF_VARS.'
    ovmf_vars="$(realpath -- "$ovmf_vars")"
    export OVMF_CODE="$ovmf_code" OVMF_VARS="$ovmf_vars"
fi

# Resolve caller-relative overrides before changing to the project directory.
[[ $qemu == /* ]] || qemu="$PWD/$qemu"
if [[ -n ${QEMU_BIOS_DIR:-} && $QEMU_BIOS_DIR != /* ]]; then
    export QEMU_BIOS_DIR="$PWD/$QEMU_BIOS_DIR"
fi

case "$qemu" in
    "$project_dir"/.runtime/qemu/usr/bin/qemu-system-*|/tmp/grapeos-qemu/root/usr/bin/qemu-system-*)
        runtime="${qemu%/usr/bin/*}"
        export LD_LIBRARY_PATH="$runtime/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        export QEMU_MODULE_DIR="${QEMU_MODULE_DIR:-$runtime/usr/lib/qemu}"
        export QEMU_BIOS_DIR="${QEMU_BIOS_DIR:-$runtime/usr/share/qemu}"
        ;;
esac

cd -- "$project_dir"
if [[ $mode == test ]]; then
    if [[ $media == desktop ]]; then
        exec make --no-print-directory test-desktop QEMU="$qemu"
    fi
    if [[ $media == uefi ]]; then
        exec make --no-print-directory test-uefi QEMU="$qemu"
    fi
    exec make --no-print-directory test QEMU="$qemu"
fi

display=none
if [[ $mode == window ]]; then
    [[ -n ${DISPLAY:-} || -n ${WAYLAND_DISPLAY:-} ]] || \
        fail 'Графічна сесія недоступна. Запусти ./run.sh --headless у терміналі.'
    displays="$("$qemu" -display help 2>&1)" || fail "QEMU не запускається: $displays"
    case $'\n'"$displays"$'\n' in
        *$'\ngtk\n'*) display=gtk ;;
        *$'\nsdl\n'*) display=sdl ;;
        *) fail 'QEMU не має графічного модуля. В Arch Linux: sudo pacman -S qemu-ui-gtk. Або запусти ./run.sh --headless.' ;;
    esac
fi

if [[ $media == desktop ]]; then make --no-print-directory desktop
elif [[ $media == uefi ]]; then make --no-print-directory uefi
else make --no-print-directory all; fi

args=(-name GrapeOS -m 32M -nic none -display "$display" -serial mon:stdio -no-shutdown)
if [[ $media == uefi || $media == desktop ]]; then
    # Each emulator run has private firmware variables; no host boot entries.
    variables_file="$(mktemp --suffix=.fd /tmp/grapeos-ovmf-XXXXXX)"
    cp -- "$ovmf_vars" "$variables_file"
    trap 'rm -f -- "$variables_file"' EXIT
    live_image="$project_dir/build/grapeos-uefi.img"
    live_readonly=on
    if [[ $media == desktop ]]; then
        live_image="$project_dir/build/grapeos-desktop.img"
        live_readonly=off
    fi
    args+=(-m 256M -cpu max -machine q35
        -drive "if=pflash,format=raw,unit=0,readonly=on,file=$ovmf_code"
        -drive "if=pflash,format=raw,unit=1,file=$variables_file"
        -device qemu-xhci,id=grape-usb
        -drive "file=$live_image,format=raw,if=none,id=grape-live,readonly=$live_readonly"
        -device usb-storage,drive=grape-live,bootindex=1)
elif [[ $media == usb ]]; then
    args+=(-device piix3-usb-uhci,id=grape-usb
        -drive "file=$project_dir/build/grapeos-usb.img,format=raw,if=none,id=grape-live,readonly=on"
        -device usb-storage,drive=grape-live,bootindex=1)
else
    args+=(-boot a
        -drive "file=$project_dir/build/grapeos.img,format=raw,if=floppy,index=0"
        -drive "file=$project_dir/data/grapeos-data.img,format=raw,if=ide,index=0")
fi
if [[ -n ${QEMU_BIOS_DIR:-} ]]; then
    args+=(-L "$QEMU_BIOS_DIR")
fi
if [[ -n ${QEMU_FLAGS:-} ]]; then
    # Flags in this optional variable are whitespace-separated; use arguments
    # after -- when an individual value contains spaces.
    read -r -a extra_flags <<< "$QEMU_FLAGS"
    args+=("${extra_flags[@]}")
fi
if [[ $media == uefi || $media == desktop ]]; then "$qemu" "${args[@]}" "$@"
else exec "$qemu" "${args[@]}" "$@"; fi
