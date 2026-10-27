# Plan Paint native build

Plan Paint consumes the standalone [GUI.Forms repository](https://github.com/falseywinchnet/gui_forms)
at the revision in `third_party/gui-forms.lock.json`. This lock also records the
provider's tested cache release and the SHA-256 of each platform archive.
Application sources link `GUIForms::Application`; its shared `GUIForms::Threading`
dependency is shipped alongside it. Never also link static Core, Controls or
Drawing into the native application.

## Toolchain and source

Use CMake 3.25+, Ninja, Python 3.11+ and LLVM **22.1.8**. Install libtiff,
libwebp and dav1d development packages for the same toolchain. Paint builds
pinned libavif, LunaSVG/PlutoVG and TinyXML-2 from source. The native scripts
also build the reduced TIFF codec profile from source. On macOS install Meson;
the recipe builds pinned libwebp 1.6.0 and dav1d 1.5.4 for macOS 14 into
`.build/codecs-macos14`, ahead of Homebrew's libraries. CI caches that audited
prefix by compiler/runtime environment and recipe content.

| Platform | Compiler distribution | Build/runtime contract |
| --- | --- | --- |
| Windows x64 | MSYS2 CLANG64 | Windows 10, clang/libc++ and winpthreads |
| macOS arm64 | Homebrew llvm@22 | macOS 14 target with GUI.Forms' matched LLVM runtime |
| Linux x64 / arm64 | apt.llvm.org LLVM 22 on Ubuntu 24.04 | Ubuntu 24.04 system ABI, X11/XWayland |

Put the selected compiler first in PATH and use `clang`/`clang++` for C,
C++ and Objective-C++. A new compiler requires a fresh build directory.
Do not mix MINGW64 GCC headers with CLANG64 libc++ headers.

Windows CI restores the hash-locked GUI.Forms toolchain archive with
`scripts/restore-windows-toolchain.py` (bootstrap Python 3.14), reusing the
harness's extraction and MSYS2 shell wrapper. Codec installation does not
upgrade the compiler. Rolling MSYS2 compiler packages may have moved beyond
the required LLVM version and must not replace this pinned toolchain.

The build tools shared with the other Malkuth products (the Windows toolchain
extraction, the macOS runtime builder and validators, and the LLVM 22 CMake
toolchain file) come from `template_harness` at the commit `harness.lock`
pins, checked out at `template_harness/` inside the checkout:

```sh
git clone https://github.com/malkuth-software/template_harness.git template_harness
python3 template_harness/harness/lock.py checkout template_harness
```

```sh
python3 scripts/fetch-gui-forms.py
python3 scripts/restore-toolkit-cache.py windows-x64
sh scripts/native-build.sh configure windows-x64
cmake --build .build/native/app --parallel 2
cmake --build .build/native/app --target check
cmake --build .build/native/app --target paint-package
python3 scripts/smoke-package.py dist/PlanPaint/plan-paint.exe
```

The source fetcher refuses an existing checkout with a different revision or
tracked modifications. The cache importer verifies both the locked archive
digest and its source revision before extraction. A missing release or GitHub
CLI is a cache miss; a corrupt or mismatched payload is rejected. The toolkit
source is still compiled and Paint's own tests always run.

The shared `scripts/native-build.sh` recipe accepts `configure`, `build`,
`test`, `package` or `all`, followed by a platform name from the table
(`windows-x64`, `macos-arm64`, `linux-x64`, `linux-arm64`). `PAINT_BUILD_DIR`
selects an alternative build directory and `BUILD_JOBS` controls concurrency.
The `ci-*.sh` wrappers call this same recipe. The workflow supplies dependencies,
imports the provider cache, caches fetched dependency sources and Skia, then
saves compiler objects immediately after a successful build.

Before configuring macOS or Linux, fetch and build the toolkit's CPU Skia
archive into `gui_forms/third_party/skia/out/paint`, using
`third_party/build_skia_cpu.sh` or `third_party/build_skia_cpu_linux.sh`.
The [workflow](../.github/workflows/build.yml) contains the complete commands.
Linux tests and the packaged startup check run under Xvfb.

On macOS set `GUI_FORMS_LLVM_RUNTIME` to
`$PWD/.build/toolchain/llvm-22.1.8-macos14` before importing the cache.
The importer uses the harness's runtime validator and rebuilds from its
pinned LLVM source when necessary. Homebrew's prebuilt libc++ is not the
application runtime. Packaging follows the dependency closure, copies the
runtime notices and rejects binaries whose minimum exceeds macOS 14.
Codec libraries must also satisfy that minimum; a successful build on a
newer machine alone does not establish macOS 14 compatibility.

Compiler caches live in `.ccache`, with `CCACHE_BASEDIR` at the workspace and
`CCACHE_COMPILERCHECK=content`. All C/C++/Objective-C++ launchers use ccache.
Headers, compiler content and options retain normal validation. Compiler or
SDK differences legitimately miss. No cache hit substitutes for platform tests.

For an already installed matching SDK, omit `RAINSTAR_TOOLKIT_SOURCE_DIR` and
set `GUIForms_DIR` to its `lib/cmake/GUIForms` directory. Use its same LLVM
22 toolchain and runtime. Configuration checks the required LiveSurface and
native control APIs. Source builds use public toolkit targets directly and
do not rebuild its gallery or proving tests. Toolkit tests are the provider's
responsibility; Paint's editing, numerical and native-host tests are consumer gates.

Windows build outputs stage both toolkit DLLs beside Paint and its UI tests.
Packaging computes the transitive runtime closure. Fonts are selected from
`packaging/fonts.txt`, with their notices, from either toolkit source or SDK.
`RAINSTAR_LEGACY_UI=ON` retains the deprecated SDL/ImGui frontend for historical
comparison; it is excluded from release packages.

## Platform behavior

| Platform | Window and input host | Desktop integration |
| --- | --- | --- |
| macOS ARM64 | AppKit, native fullscreen, native file dialogs and clipboard | Native print/page setup, Image Capture and desktop background services |
| Windows x64 | Win32, GDI presentation, native dialogs, DIB/private RGBA clipboard | Native print/page setup, acquisition and desktop background services; Wine runtime checks also cover the application |
| Linux x64 / ARM64 | X11; works in XWayland sessions, including Weston; dwm and twm checked | Retained GUI.Forms file dialogs, cross-process PNG/RGBA/text clipboard, Xdnd file drops; optional CUPS printing and scanner/desktop services |

The Linux host currently uses one X screen at scale 1. It does not implement a
native Wayland protocol backend, RandR monitor changes, mixed-DPI displays or
Linux accessibility publication. A Wayland desktop must provide XWayland.
Linux print setup submits a fitted, alpha-flattened PostScript page to the
configured `lp` service. Tests rasterize the output with Ghostscript and verify
orientation, margins and pixel quadrants. Physical printer output and every
scanner/desktop combination have not been verified. Missing optional desktop
services produce an error rather than reporting success.

New Linux builds target the GUI.Forms Ubuntu 24.04 ABI. Their archives bundle
application libraries, fonts and X11 locale data; glibc and its loader remain
system dependencies. Run the top-level `Plan Paint` launcher and retain the
complete directory. Previously released 1.1.4 musl archives are unchanged. No package manager or network connection is needed to draw, use help,
open or save pictures. The Mac application is signed ad hoc; the installer is
unsigned and not Developer ID notarized.

## Document and display ownership

Paint retains straight-alpha RGBA pixels, meaningful RGB under zero alpha,
selection state, tool transactions, undo history and format metadata. Its display cache uses GUI.Forms `LiveSurface` leases in the native channel
order. Small strokes update only the changed source region, preserving the
previous frame. An opaque viewport buffer composites transparency and samples
zoom at physical pixel resolution, preserving nearest-neighbor pixel edges
on Windows, macOS and Linux. Unchanged repaints reuse this buffer.
The canvas declares opaque control coverage because it paints its entire
background. Document alpha and hidden RGB remain unchanged. Rotated views
and selection overlays retain registered transparent images: the pinned
Windows live presenter copies instead of blending these surfaces.
Selections and shape previews publish the composed image. While a selection is
rotating or resizing, a separate viewport-bounded overlay shows the transformed
pixels immediately. Its temporary bilinear display samples never enter document
history; release commits the CONV result from the unchanged original samples.

The derived canvas uses normal routed pointer input, focus, capture, and clipped
paint overlays. Compound controls establish children in `initialize_control_tree`.
CONV preparation runs outside the UI thread, posts completion through the toolkit
dispatcher, and rejects stale generations. Closing or cancelling an operation
preserves lifetime boundaries.

The ribbon provides Home, View, Materials, Atlas and context-sensitive tool pages.
Selection includes mesh, rotation, placement and cancellation. Stamp Scale and
Angle labels support scrubbing. The help sidebar is bundled locally. Printing
remains application code over native window ownership; it does not introduce a
second toolkit or renderer.

Zoom-in preserves the image coordinate under the focus. Zoom-out then clamps each
screen translation to `[viewport - image * scale, 0]` while the image exceeds
that viewport dimension. Once an axis fits, its translation is
`(viewport - image * scale) / 2`. Wheel, ribbon commands, status buttons and the
slider share this rule.

## Verification

CTest covers document editing and formats, numerical transforms, RGBA
preservation, input routing and capture, undo, editable curves, branching paths,
selection transforms, stamp reset, pointer-anchored zoom, context ribbon settings,
text placement, modal transactions, asynchronous cancellation, Atlas sequences,
and icon/cursor round trips. The native fixture opens an actual application,
drives canvas input, checks tool/history state, and closes through the host.

`paint-clipboard-native` starts two native Paint processes on the same desktop.
One copies a four-pixel RGBA specimen; the other pastes it and checks exact bytes,
including hidden RGB and partial alpha. The producer then copies its alpha as an
opaque grayscale image, and the consumer verifies the transferred mask. This
exercises Paint's commands and native host services across a process boundary.
The four-platform CI matrix enables it with `PAINT_CLIPBOARD_TEST_ISOLATED=1`.
Ordinary local CTest runs report this test as skipped; enable it only on a test
desktop whose clipboard is available for the exchange. Both processes remain
alive while data is transferred, including X11 selection-owner requests.

The toolkit's Linux integration tests exchange large UTF-8 and PNG clipboard
payloads with an external X11 process, exercise INCR transfers and file drops,
and accept actual open/save dialogs. Native app screenshots and interactive
checks complement these tests; a headless test result alone is not a claim that
every desktop service or window-manager configuration was exercised.


Author: Astra
Sponsor: Rainstar

## Automatic publication

The workflow follows PlaySuite's tested-release contract. A prepare job chooses
one version for the application, bundle metadata and every package. Main builds
use `major.minor.(baseline patch + workflow run number)`; reruns retain their
version. Explicit `v*` tags must match the baseline series and cannot predate it.
PRs and ordinary manual runs build the baseline without publishing. A manual
run with `publish=true` can publish from main.

After all four platform jobs pass, the release job verifies the exact package
set, writes SHA256SUMS, tags the tested commit and uploads into a draft before
publishing. It rejects conflicting tags, leaves existing public releases
unchanged, and skips a main run superseded by a newer commit. Only this job
receives repository write permission. The latest release link always leads to
the current published downloads.
