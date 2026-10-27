# Third-party notices

Plan Paint's original code is MIT, Copyright (c) 2026 joshuah.rainstar@gmail.com.

- PlaySuite paper generator, `falseywinchnet/games` commit `c5ea5a658c99042d4eed0aa800e910abadd9e131`: MIT License - Modified, Copyright (c) 2026 Joshuah Rainstar. The unmodified source and license are in `vendor/playsuite-paper`; packages include `licenses/PlaySuite-paper-LICENSE.txt`.

- Dear ImGui (optional comparison frontend) v1.91.9b, commit f5befd2d29e66809cd1110a152e375a7f1981f06: MIT, Copyright Omar Cornut. See vendor/imgui/LICENSE.txt.
- stb, commit 2c980bb59875b0d32144a71867fbdebb2f77cd20: MIT option selected. See vendor/stb/LICENSE.
- SDL3 (optional comparison frontend): zlib license. https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt
- libtiff 4.7.2: permissive TIFF license. https://gitlab.com/libtiff/libtiff/-/blob/master/LICENSE.md
- libavif 1.4.2: BSD-2-Clause. See packaging/licenses/libavif-LICENSE.
- dav1d AV1 decoder: BSD-2-Clause. See packaging/licenses/dav1d-COPYING.
- LunaSVG 3.5.0, commit 83c58df8103dc7dca423dfd824992af94d49bed6, and its bundled PlutoVG 1.3.1: MIT. PlutoVG includes FreeType rasterizer code and stb components under their permissive terms. See packaging/licenses/svg. This software uses portions of the FreeType Project (https://freetype.org), copyright David Turner, Robert Wilhelm and Werner Lemberg.
- TinyXML-2 11.0.0: zlib license. Used to inspect SVG structure before rasterization. See packaging/licenses/svg/tinyxml2-LICENSE.txt.
- libwebp: BSD-style license. https://chromium.googlesource.com/webm/libwebp/+/HEAD/COPYING
- Portsmouth Regular, Bold, Italic, Bold Italic and Mono: embedded font family. Original source-font copyright and license notices are preserved in [packaging/licenses/Portsmouth-source-notices.txt](packaging/licenses/Portsmouth-source-notices.txt). System fonts selected for artwork are not redistributed.
- Cairo Unicode dingbat font: freeware by Clark T. Riley, with Unicode mappings by ChristTrekker. Its three original notices are bundled with the unmodified font in `assets/fonts/cairo-unicode/`. Personal distribution must include those notices; commercial distribution requires the author's written permission. Plan Paint's own license does not relicense this font.
- The vendored Dear ImGui source tree also contains Droid Sans under Apache License 2.0; it is not the application interface font.

Microsoft, Windows and Paint names identify the reference application and belong to their respective owner. Plan Paint is independent and uses newly drawn icons.

Binary packages also carry the notices for their bundled codec dependencies. macOS bundles zstd under its BSD option, liblzma under 0BSD, and libjpeg-turbo under the IJG/BSD terms. This software is based in part on the work of the Independent JPEG Group. The native release uses GUI.Forms under its retained MIT license. The GUI.Forms package carries the notices for its Skia, HarfBuzz, FreeType, Unicode, PNG/zlib and bundled font resources. Windows uses the native Win32 CPU renderer. macOS and Linux use the private CPU Skia/text adapter.

Portable Linux packages include musl and X11 client libraries, with their notices in `licenses/runtime`; X11 locale data comes from libX11. Linux and MinGW Windows packages carry GCC runtime notices and the GCC Runtime Library Exception. MinGW-w64 and winpthreads notices are also included. Runtime source locations are listed in [packaging/licenses/runtime/SOURCES.md](packaging/licenses/runtime/SOURCES.md).

The released GUI.Forms application does not link SDL, Dear ImGui or GTK. Those sources and notices remain for the optional comparison frontend. Linux printing uses the configured operating-system CUPS service; acquisition and wallpaper commands use the relevant desktop services.

## OKHSL color picker

The OKHSL conversion in `vendor/ok_color/ok_color.h` is Björn Ottosson's 2021
reference implementation, used under its included MIT license. Scalar arithmetic
is promoted to double precision. Source and adaptation notes are in
`vendor/ok_color/README.md`. The perceptual palette clustering and sampling code
in Plan Paint are original implementations.

## Poster fonts

DynaPuff, Anton and Titan One are distributed from the Google Fonts source
repository under the SIL Open Font License 1.1. Bubble Sans 1.01 is by the
Bubble Sans Project Authors (Abay Emes / QR Type), also under OFL 1.1.
Unmodified fonts and their separate copyright/license files are in
`assets/fonts/poster/` and are copied into application packages. Sources:
https://github.com/google/fonts/tree/main/ofl/dynapuff,
https://github.com/google/fonts/tree/main/ofl/anton,
https://github.com/google/fonts/tree/main/ofl/titanone,
https://www.dafont.com/bubble-sans.font,
https://github.com/abayemes/bubblesans.

## Language and accessibility dependencies

SheenBidi 2.9.0 (commit 829d99918adf05761e9f35611dc13e76e6810f80) supplies Unicode bidirectional ordering under Apache License 2.0. Its unchanged source and license are included in the GUI.Forms source patch and SDK notices.

Noto Sans Arabic, Hebrew, Devanagari, Bengali and Gurmukhi, plus Noto Sans CJK, provide interface fallback coverage under the SIL Open Font License 1.1. GUI.Forms carries the original font files, OFL notices and source hashes. These fonts do not alter saved artwork unless explicitly selected for text.

Linux accessibility uses ATK and the AT-SPI bridge from at-spi2-core, dynamically linked under LGPL 2.1 or later. Portable packages must retain their license notices and permit replacement of the shared libraries. Upstream source: https://gitlab.gnome.org/GNOME/at-spi2-core . Windows accessibility uses the operating system's Oleacc/COM service; macOS uses AppKit.
