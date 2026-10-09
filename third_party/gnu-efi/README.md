# GNU-EFI 4.0.4

Unmodified upstream source archive, included for the startup/relocation code
linked into the ready-to-boot GrapeOS EFI binary.

Source: https://github.com/ncroxon/gnu-efi/tree/4.0.4

Download: https://codeload.github.com/ncroxon/gnu-efi/tar.gz/refs/tags/4.0.4

SHA-256 of `gnu-efi-4.0.4.tar.gz`:

```text
40b61e842a4efcbf80f3e53b2f220c044e8cfe46eb4dd6396c83b751240b1c0d
```

The archive's `LICENSE` explains the component licenses. The `gnuefi` startup
code is dual licensed GPL-2.0-or-later OR BSD-3-Clause; GrapeOS uses its BSD
alternative. Copyright/license notices supplied by the local GNU-EFI package
are also included here.

To build the matching dependency locally in Linux:

```sh
mkdir -p build/gnu-efi-source .runtime/gnu-efi
tar -xzf third_party/gnu-efi/gnu-efi-4.0.4.tar.gz -C build/gnu-efi-source
make -C build/gnu-efi-source/gnu-efi-4.0.4 ARCH=x86_64
make -C build/gnu-efi-source/gnu-efi-4.0.4 ARCH=x86_64 install PREFIX=/usr DESTDIR="$PWD/.runtime/gnu-efi"
make desktop EFI_ROOT="$PWD/.runtime/gnu-efi/usr"
```
