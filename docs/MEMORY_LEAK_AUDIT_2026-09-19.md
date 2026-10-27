# Rainstar Paint memory-leak audit — 2026-09-19

## Scope and standard

This audit covered Rainstar Paint's retained GUI.Forms application, the SDL/ImGui comparison frontend, core image and document code, codec error paths, worker lifetime, event subscriptions, clipboard ownership, printing, desktop integration, and the platform adapters for macOS, Linux and Windows. It looked for memory that grows while the program remains open, resources lost on exceptions or rejected input, reference cycles, background work that can outlive its owner, and process-exit ownership that masks repeated-use leaks.

The review combined a manual ownership inventory with compiler analysis and runtime measurement. A passing sanitizer or process-exit scan is evidence for the exercised paths, not a proof about every native dependency or operating-system service.

## Result

The reachable Rainstar-owned leaks found by this audit are closed. The malformed-codec corpus, document and warp suites, the complete headless GUI.Forms editor lifecycle, and the noninteractive macOS test executables now finish with zero leaked objects under Apple's `leaks` tool. A native Linux Clang 18 build passes the four core suites under AddressSanitizer, LeakSanitizer and UndefinedBehaviorSanitizer with leak detection enabled and a nonzero leak exit code. The Windows x86_64 build passes the four core suites and native-host startup/teardown under Wine; GCC's Windows-target analyzer reports no leak, double-free, use-after-free or mismatched-deallocation diagnostics in the 39 Rainstar translation units.

One platform-owned process-exit result remains: opening and closing the native macOS window leaves 288 objects totaling 18,816 bytes in three `NSXPCConnection` root cycles. Every allocation stack begins in Apple's AppIntents and LinkServices frameworks and continues through Foundation, XPC and libdispatch. No Rainstar or GUI.Forms frame appears in those cycles. The same native-host test completes its Rainstar and GUI.Forms shutdown checks before measurement. This is recorded as a macOS 26.5 framework lifetime, not counted as a Rainstar-owned clean-up success.

## Findings and repairs

| Finding | Reachability | Repair and evidence |
| --- | --- | --- |
| The stb decode buffer was released manually after image allocation and dimension validation. A dimension disagreement or later exception could bypass the release. | PNG, JPEG, direct-color BMP and TGA imports. | Own the returned buffer with `std::unique_ptr` and `stbi_image_free`. Core, malformed-input and cross-platform analyzer lanes pass. |
| TIFF read and memory-encode handles depended on paired `TIFFClose` calls around throwing code. | TIFF import and save. | Give both handles `std::unique_ptr` ownership with `TIFFClose`. Native macOS and Linux malformed TIFF corpora pass with zero reported application leaks. |
| Legacy screenshot capture owned two SDL surfaces manually around image allocation and file saving. | SDL/ImGui screenshot and demo automation. | Give the readback and converted surfaces `std::unique_ptr` ownership with `SDL_DestroySurface`. |
| GIF export owned a temporary `FILE*` and heap buffer; WebP export owned a native encoder buffer. These paths were unnecessary for the retained save contract. | Every GIF or WebP save attempt. | Remove GIF and WebP export, remove the bundled gif-h dependency, and remove both formats from save dialogs. GIF is rejected; WebP and AVIF remain import-only. Tests assert that GIF, WebP and AVIF cannot be written. |
| Mutated HEIF input caused `CGImageSourceCopyPropertiesAtIndex` to retain two ImageIO object cycles: 64 objects and 9,408 bytes in the full malformed corpus. Cache removal did not break the cycles. | Repeated malformed HEIC/HEIF import on macOS. | Stop using the leaking property query. Parse bounded ISO-BMFF `meta` / `iprp` / `ipco` / `ispe` extents before ImageIO, cap every declared extent, decode with a maximum dimension, and check the decoded dimensions again before canvas allocation. The identical malformed corpus now reports zero leaks. |
| Windows' standard-library regular-expression implementation rejected the SVG filter expression before the intended unsafe-raster-element check ran. | Every SVG import in the MinGW Windows build. | Replace the expression with a bounded ASCII CSS-property scan. The Windows core and SVG security checks now pass. |
| Windows printer `HGLOBAL` selections and GTK page/print settings were retained in raw process-global pointers. | Repeated page setup and printing, plus process shutdown. | Put the remembered settings in state objects whose destructors call `GlobalFree` or `g_object_unref`. The Windows and Linux ownership analyzers report no findings in these paths. |

GIF, WebP and AVIF export are absent from the encoder dispatcher, writable-extension predicate, SDL save filters, GUI.Forms save filters, help, README, dependency list and tests. WebP and AVIF decoding remain reachable and are included in the deterministic malformed-input corpus.

## Ownership review

- GUI controls keep their editor owners through `weak_ptr`; dialog, ribbon, atlas, canvas and queued warp callbacks do not form owner cycles. Subscription tokens are members of their owning controls and are destroyed with those controls. The full headless GUI.Forms editor test finishes with zero leaks.
- `WarpWorker` owns a `std::jthread`; stop and join happen during destruction. Parallel CONV and warp batches also use scoped `jthread` vectors.
- Windows acquisition balances COM initialization, `VARIANT` contents, automation exceptions and BSTR values. The Windows-target analyzer includes `desktop_windows.cpp`.
- Clipboard payloads transfer ownership only after SDL accepts the provider and use SDL's cleanup callback. Paste buffers are released on both success and decode failure.
- Linux GUI.Forms process launching closes both pipe ends on success and failure and reaps asynchronous acquisition children. The Linux-specific process and print translation units pass Clang's leak checkers.
- Retained theme, font-registration and color-mosaic data are bounded process-wide caches. They do not grow per document, command or frame.

## Platform evidence

### macOS 26.5, Apple silicon

- Native GUI.Forms debug build: all six CTest entries pass.
- Bundled-TIFF ASan/UBSan build: all four core suites pass with halt-on-error enabled.
- Clang static analyzer with `cplusplus.NewDeleteLeaks` and `unix.Malloc`: 38 first-party translation units, no diagnostics.
- Apple `leaks --atExit`:

| Executable | Malloc total at exit | Leaked objects / bytes |
| --- | ---: | ---: |
| `paint-tests` | 231 KB | 0 / 0 |
| `paint-warp-tests` | 104 KB | 0 / 0 |
| `paint-format-tests` | 1,715 KB | 0 / 0 |
| `paint-codec-security-tests` | 1,732 KB | 0 / 0 |
| `paint-forms-tests` | 134 KB | 0 / 0 |
| native GUI.Forms host smoke | 3,898 KB | 288 / 18,816, all in three Apple AppIntents/LinkServices `NSXPCConnection` cycles |

### Ubuntu 24.04 ARM64 VM

- Native Clang 18 debug build with `-fsanitize=address,undefined,leak` and frame pointers.
- `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`, `LSAN_OPTIONS=exitcode=23:report_objects=1`, and UBSan halt-on-error.
- Core, warp, format and deterministic malformed-codec suites all pass; no sanitizer or leak report is emitted.
- Linux GUI.Forms desktop/process integration and the GTK comparison-frontend print adapter pass Clang's `cplusplus.NewDeleteLeaks` and `unix.Malloc` analysis.

This Linux sanitizer configuration exercises the shared core and codecs. It does not dynamically open a Linux GUI.Forms window or a GTK print dialog.

### Windows x86_64 cross-target

- Release cross-build completes for the GUI.Forms application and all tests.
- GCC `-fanalyzer` checks all 39 Windows-target Rainstar translation units, including COM acquisition and Win32 printing: zero ownership diagnostics in the leak, file/fd leak, use-after-free, double-free, free-of-non-heap and mismatched-deallocation classes.
- Core, warp, format and malformed-codec Windows executables pass under Wine 11.4.
- The native GUI.Forms Windows host smoke executable opens, exercises input/capture/document behavior and closes successfully under Wine.
- The larger headless GUI.Forms editor suite reaches an existing SDK mismatch in nested popup focus containment under Wine. The selected Windows SDK predates the repository's `nested-popup-focus.patch`; this is not a leak finding, and the four core suites and native-host smoke complete independently of it.

Wine is a compatibility run on macOS, not native Windows heap instrumentation. A native Windows run with Application Verifier, UMDH, Visual Studio's heap diagnostics or an equivalent allocator tracer remains required before making a zero-runtime-leak claim for Windows system services and the GUI host.

## Commands used for the final dynamic lanes

```sh
cmake --build build-forms --parallel 8
ctest --test-dir build-forms --output-on-failure

ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-security-full-sanitize --output-on-failure

leaks --atExit -- build-forms/paint-codec-security-tests
leaks --atExit -- build-forms/paint-forms-tests

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
LSAN_OPTIONS=exitcode=23:report_objects=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-leak-linux --output-on-failure
```

The remaining macOS framework cycles and the absence of native Windows heap instrumentation are explicit limits of this report. They do not conceal a known Rainstar-owned leak.
