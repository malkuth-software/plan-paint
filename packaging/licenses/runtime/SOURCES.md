# Runtime sources

Linux packages carry the program only: the desktop's glibc, X11 (or XWayland),
ATK and GLib are used at run time, so no Linux runtime library or notice for
one is included. Previously released 1.1.4 musl archives carried musl and the
X11 client libraries with their notices; those archives are unchanged.

Windows MinGW-w64 14.0.0 sources: https://sourceforge.net/projects/mingw-w64/files/mingw-w64/mingw-w64-release/mingw-w64-v14.0.0.tar.bz2
Windows GCC 16.1.0 runtime sources: https://gcc.gnu.org/pub/gcc/releases/gcc-16.1.0/
GCC runtime libraries retain the GCC Runtime Library Exception.

The locally cross-compiled Windows package uses the pinned Chromium zlib source
from GUI.Forms: https://chromium.googlesource.com/chromium/src/third_party/zlib/+/646b7f569718921d7d4b5b8e22572ff6c76f2596
