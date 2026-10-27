# Package, startup and memory measurements

## Current packaging and font ownership

The multilingual application includes CJK, Arabic, Hebrew and Indic fallback
fonts alongside its UI fonts. The macOS and Linux renderer maps bundled font
files and shares their immutable storage with the text shaper, avoiding a
full-file heap copy for each window. Font tables, glyph caches and rendered
surfaces still allocate memory. The byte-span registration API retains its
copying contract for callers whose source storage is temporary.

Release packaging strips unneeded symbols from copied executables and shared
libraries before signing or recording package hashes. macOS uses the latest
installer compression compatible with the application's declared minimum OS;
Windows ZIP packaging uses maximum deflate compression. Fonts and language
catalogs retain their complete content.

## Historical 0.2.0 to 0.2.1 comparison

These measurements compare the macOS ARM64 0.2.0 package with the 0.2.1
build on macOS 26.5. They describe this configuration, not Windows or
Linux, and do not establish a minimum system requirement.

| Measurement | 0.2.0 package | 0.2.1 build |
| --- | ---: | ---: |
| Main executable, decimal MB | 8.10 | 2.81 |
| Bundled fonts and their notices, decimal MB | 21.20 | 3.46 |
| Complete application bundle, decimal MB | 42.76 | 19.73 |
| Median process launch to OS-visible window | 200.6 ms | 192.7 ms |
| Median RSS, sampled 0.5 s after window visibility | 190.0 MiB | 127.8 MiB |
| Blank 960 × 640 canvas: physical footprint snapshot | 96.8 MiB | 50.1 MiB |

Startup medians use seven consecutive launches of each packaged executable.
The OS-visible-window timestamp is not a measurement of the first fully painted
frame. These runs used the normal filesystem cache, with other applications
running; they are not cold-boot benchmarks. The small timing difference is not
strong evidence of a startup-speed improvement. Idle sampled CPU usage was 0%.

The physical-footprint snapshots came from `vmmap -summary`. RSS came from `ps`.
They are different accounting measures. Shared and file-backed mappings make
RSS larger than the private physical cost; adding every resident mapped library
reported by `vmmap` substantially overcounts the memory uniquely owned by Paint.
The final build opened on a 256 × 256 blank PNG measured 47.0 MiB of physical
footprint after two seconds. Window visibility, allocator reuse and system
pressure can move these snapshots. No Windows 7 Paint comparison was run.

## What changed

The default frontend excludes the SDL/ImGui comparison interface. Optimized
packaging now requires a Release, MinSizeRel or RelWithDebInfo build; supported
compilers use interprocedural optimization and unused-section removal for the
first-party application targets. macOS packaging strips debug symbols from its
copy. This is a size reduction from build configuration and elimination of
unused sections, not a claim that the old executable consisted entirely of dead
code.

Mach-O inspection separates that result further: `__TEXT` decreased from
3,899,392 to 2,408,448 bytes, while `__LINKEDIT` decreased from 4,145,152 to
344,064 bytes. Stripping only debug symbols from a copy of the old executable
reduced it from 8,104,512 to 6,738,736 bytes. Thus a substantial part of the old
8 MB was link/symbol metadata; optimization and removal of unused code account
for additional savings.

The font pack explicitly contains Portsmouth regular/bold, four Carlito faces,
Cousine regular and Cairo Unicode. Cairo is a 66,044-byte dingbat font; its
original distribution notices are included. The large CJK face, emoji face and
unused monospace variants no longer ship in Paint. GUI.Forms accepts these as
optional resources, and its rasterizer and text shaper share immutable font
bytes instead of retaining two encoded copies. The SDK can still supply the
larger optional fonts to other consumers.

System font discovery waits until Text tools are first opened. Ribbon icons are
decoded only in the sizes actually used. Identical ribbon and tab themes share
immutable instances. Unchanged stamps copy their source pixels directly and do
not allocate a CONV transform field.

## Remaining memory costs

Allocation-stack inspection identified the following substantial owners:

- A 960 × 640 RGBA image requires 2.34 MiB. The editing document, display bitmap,
  transactional edit backup, resource-registry image and renderer image can each
  hold a full-sized buffer. Their roles preserve document colors, rollback and
  retained rendering, but the copies are a useful next optimization target.
- The CPU window raster requires about 4.0 MiB for the measured 1280 × 820 window
  at backing scale 1. Its memory scales with the square of the backing scale.
  AppKit/CoreGraphics and the compositor retain additional window surfaces.
- The selected UI font files require about 3.3 MiB of shared encoded storage,
  plus font tables, glyph caches and shaping objects.
- Controls, event subscriptions, layout/display lists, icon images, native
  window services and Objective-C/Swift runtime state account for further
  allocations. A blank document has no undo snapshots or prepared CONV field.

These are ownership observations, not additive process-accounting categories.
For example, the renderer raster and font allocations also appear inside the
heap totals. The final blank-canvas heap regions held about 35.4 MiB resident.

Opening a small file currently creates the default blank document first. The
allocator can retain its freed larger buffers: the 256 × 256 run still showed
about 7 MiB in empty-but-dirty large allocation regions. Avoiding that initial
canvas and consolidating display copies are more promising next steps than
removing editing features. Large pictures and undo history will naturally use
more memory; the configured history budget remains approximately 256 MiB.

## Validation

The refinement passes all five Paint CTest targets and the first-party style
check. The updated toolkit passes 91 native tests, including cached exposure,
minimize/restore and hide/show without mouse input. The shared-font ownership
tests also pass AddressSanitizer and UndefinedBehaviorSanitizer with the changed
font engine and test source instrumented; the linked vendor libraries are not
instrumented. Native visual checks cover Path anchors, the magnifier, stamp
masks and Help controls. The timing and memory measurements above cover macOS only. Per-platform
release checks are recorded in the release asset `verification.json`.
