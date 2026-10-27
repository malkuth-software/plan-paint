# Bundled Linux runtime sources

- libX11: https://www.x.org/releases/individual/lib/libX11-1.8.13.tar.xz
- libXau: https://www.x.org/releases/individual/lib/libXau-1.0.12.tar.xz
- libXdmcp: https://www.x.org/releases/individual/lib/libXdmcp-1.1.5.tar.xz
- libxcb: https://xorg.freedesktop.org/archive/individual/lib/libxcb-1.17.0.tar.xz
- libbsd: https://libbsd.freedesktop.org/releases/libbsd-0.12.2.tar.xz
- libmd: https://archive.hadrons.org/software/libmd/libmd-1.2.0.tar.xz
- musl: https://musl.libc.org/releases/musl-1.2.6.tar.gz

GCC 15.2.0 runtime source: https://gcc.gnu.org/pub/gcc/releases/gcc-15.2.0/
Alpine build recipes: https://gitlab.alpinelinux.org/alpine/aports/-/tree/3.24-stable/main/gcc

The Linux package includes the musl loader and the complete shared-library closure.
GCC runtime libraries retain the GCC Runtime Library Exception.

Windows MinGW-w64 14.0.0 sources: https://sourceforge.net/projects/mingw-w64/files/mingw-w64/mingw-w64-release/mingw-w64-v14.0.0.tar.bz2
Windows GCC 16.1.0 runtime sources: https://gcc.gnu.org/pub/gcc/releases/gcc-16.1.0/

The locally cross-compiled Windows package uses the pinned Chromium zlib source
from GUI.Forms: https://chromium.googlesource.com/chromium/src/third_party/zlib/+/646b7f569718921d7d4b5b8e22572ff6c76f2596

Accessibility dependency sources (dynamically linked; installed versions are recorded by the platform build):

- ATK/AT-SPI: https://gitlab.gnome.org/GNOME/at-spi2-core
- GLib/GObject/GIO: https://gitlab.gnome.org/GNOME/glib
- D-Bus: https://gitlab.freedesktop.org/dbus/dbus
- libffi: https://github.com/libffi/libffi
- PCRE2: https://github.com/PCRE2Project/pcre2

Corresponding source can be obtained from these upstreams and the exact Alpine package versions recorded in the package build receipt. LGPL libraries remain separate, replaceable shared objects. The package adds no private static copy of GLib or AT-SPI.
