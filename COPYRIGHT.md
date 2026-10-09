# Copyright and licenses

Copyright (C) 2026 Xiaomi2009User and GrapeOS contributors.

Unless separately marked below or in a file, GrapeOS source code is licensed
under the GNU General Public License version 3 only (`GPL-3.0-only`).
The full license is in [LICENSE](LICENSE).

GrapeOS is distributed without any warranty. You may use, modify and redistribute
it under GPL v3. When distributing a modified version, keep GPL v3 and provide
the corresponding source code as required by that license.

Third-party components keep their own licenses:

- `assets/framebuffer-font.h` and `assets/terminal-font.h`: Terminus font by
  Dimitar Toshkov Zhekov, SIL Open Font License 1.1.
  See `assets/framebuffer-font.OFL.txt` and the font file headers.
- `third_party/stb/stb_image.h`: stb_image v2.30, used under the MIT license
  included in that file. See `third_party/stb/README.md` for the upstream revision.
- The UEFI binary links GNU-EFI 4.0.4 startup/relocation code. Its complete
  unmodified source archive and license notices are in `third_party/gnu-efi`.
  Its startup code is dual licensed GPL-2.0-or-later OR BSD-3-Clause;
  GrapeOS uses the BSD alternative. Other GNU-EFI notices remain in the archive.

The ready-to-boot ZIP includes this notice and the relevant licenses.
