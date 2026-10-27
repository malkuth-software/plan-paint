# Rainstar Paint file and codec security audit

**Audit date:** 2026-09-19  
**Audited baseline:** Rainstar Paint 0.2.3 (`d0a4dab`) and its GUI.Forms host  
**Integration target:** Rainstar Paint 0.3.0 feature revision (`c05b235`)
**Threat model:** an attacker can persuade a user to open, paste, drop or save a crafted image. Some users may intentionally or accidentally launch Paint through `sudo`, as root, set-id, with Linux capabilities or from an elevated Windows token. An exploit in that process would inherit its authority. A second local process may race predictable files in a directory that both processes can write.

This review covered the two Rainstar Paint frontends, file reads and writes, every advertised image and container format, clipboard transfer, settings and recent-file persistence, the GUI.Forms file/clipboard boundary, and the decoder libraries reachable from those paths. It combined manual source review, dependency and linked-symbol inventories, focused regression tests, malformed-input mutation tests, and AddressSanitizer plus UndefinedBehaviorSanitizer runs. It is a bounded review of this revision, not a proof that every third-party decoder is free of undiscovered defects.

## Result

The patch reduces elevated-input exploitability and closes the identified file-race, format-confusion, resource-exhaustion and known reachable memory-safety paths. It deliberately narrows import and export compatibility where the available decoder or encoder should not remain reachable. GIF is unavailable, while WebP and AVIF are import-only. BMP import accepts direct 24- and 32-bit pixels. TIFF import accepts one uncompressed, contiguous 8-bit RGB or RGBA image. SVG raster image elements and filters are disabled. Arbitrary user-selected font files are no longer accepted by the length-unaware stb_truetype path; text uses the bundled and administrator-installed font inventory. The other retained formats receive bounded memory input, decoded-dimension limits, animation rejection where applicable, and stricter profile checks.

Both application entry points remain usable with root, `sudo`, set-id, Linux capabilities, or an elevated Windows token. The patch reduces the likelihood that hostile input can turn that authority into code execution by removing vulnerable parser profiles, bounding every retained input path, validating decoded geometry and replacing race-prone writes. It does not claim to isolate an already elevated process from a future native-code decoder defect.

## Findings and disposition

| Severity | Finding | Reachability before the patch | Disposition |
| --- | --- | --- | --- |
| Critical under elevation | The complete native decoder closure runs with inherited administrator authority. | Opening, pasting or dropping any supported image after launching Paint elevated. | Elevated execution remains supported. Reduce exploitability by removing broad and known-vulnerable decoder paths, enforcing byte/pixel/frame/profile limits, validating geometry at copy boundaries, and using only bounded memory input. A successful native-code exploit would still inherit the process authority. |
| High under elevation; medium otherwise | Saves and preference writes used truncating path opens or predictable temporary names. A writable-directory adversary could substitute a symlink, clobber another file, or leave a partial destination after interruption. | Image/container saves, settings, recent files, custom colors and the legacy idle report. | One same-directory atomic writer now uses exclusive, non-following temporary creation, complete-write loops, `fsync`/`FlushFileBuffers`, and atomic replacement. A regression proves that a destination symlink is replaced without modifying its referent. |
| High | Broad `stb_image` reachability exposed reported integer overflows, out-of-bounds/uninitialized reads, a progressive-JPEG error-path bug and multiple GIF defects. The upstream report documents the reachable 16-bit conversion, PNG palette/IDAT, progressive JPEG, BMP palette and animated GIF faults. | PNG, JPEG, BMP, GIF, PSD, PNM, HDR, PIC and TGA were accepted through one dispatcher. | Compile only PNG, JPEG, BMP and TGA; disable stdio; reject GIF before dispatch; reject paletted BMP; remove PSD, PNM, HDR and PIC from the UI; patch the reachable PNG, JPEG and 16-bit allocation faults; enforce 16,384 pixels per axis and 64 megapixels. See [upstream stb issue 1928](https://github.com/nothings/stb/issues/1928) and the independently reported [GIF out-of-bounds read](https://github.com/nothings/stb/issues/1915). |
| High | TIFF used `TIFFReadRGBAImageOriented`, which enters a large conversion surface and accepted compressed, tiled, multi-page and unusual sample layouts. Current libtiff reports include allocation and overflow faults in those broad paths. | Every `.tif` or `.tiff` file. | Pin bundled/release builds to libtiff 4.7.2; disable its compression codec modules; decode only one uncompressed, scanline, contiguous 8-bit RGB/RGBA image; validate alpha declaration, orientation, scanline geometry and dimensions; read through bounded memory callbacks. The removed RGBA conversion path includes the families named in [issue 781](https://gitlab.com/libtiff/libtiff/-/issues/781), [issue 786](https://gitlab.com/libtiff/libtiff/-/issues/786) and [issue 808](https://gitlab.com/libtiff/libtiff/-/issues/808). The [libtiff tag list](https://gitlab.com/libtiff/libtiff/-/tags) identifies 4.7.2 as the current release at audit time. |
| High | SVG `<image>` could read a local path and feed embedded content to LunaSVG's older private image decoder. Complex unbounded XML also increased denial-of-service exposure. | Any SVG opened from disk. | Reject every raster `<image>` and filter, cap the source at 16 MB, cap element count at 100,000, attributes at 200,000 and nesting at 128, then serialize the inspected tree before LunaSVG sees it. The retained LunaSVG 3.5.0 renderer has prior public crash reports, including [issue 209](https://github.com/sammycage/lunasvg/issues/209), so SVG remains a larger parser surface than ordinary PNG. |
| High | GUI.Forms clipboard geometry was trusted immediately before row `memcpy`. An inconsistent width, height, stride and vector length could read out of bounds. | Paste from a compromised or defective host service implementation. | Revalidate nonzero dimensions, 16,384-axis and 64-megapixel limits, row width, multiplication bounds and backing-vector length at the application boundary. A hostile-geometry regression verifies rejection before document mutation. |
| High | “Open font…” accepted any `.ttf` or `.otf` and passed its bytes to stb_truetype, whose API has no buffer-length parameter for subsequent table access. | The 0.3 text ribbon's arbitrary font picker. | Remove arbitrary font-file selection. Text faces come from bundled resources and administrator-installed system font directories; reads are regular-file-only and capped at 32 MB, with the embedded face as the failure fallback. |
| Medium | Ordinary file input accepted non-regular files. A FIFO could block, while devices and changing pseudo-files have semantics the decoders do not expect. | All file-backed imports. | Open once with close-on-exec and nonblocking flags where available, validate the opened handle as a regular nonempty file, cap bytes before allocation, and read exactly the validated size. A FIFO regression verifies immediate rejection. Regular-file symlinks remain usable for normal desktop workflows. |
| Medium | Settings, recent-file and custom-color reads still used ordinary blocking streams even after their writes became atomic. A same-user process could replace one with a FIFO for a startup denial of service. | Preference loading at startup or first use. | Route all three readers through the bounded regular-file helper with small format-specific limits and fail closed to defaults. The FIFO regression exercises each reader. |
| Medium | Decompression bombs and header/decode disagreement could allocate excessive memory or make the destination smaller than the decoded copy. | stb, WebP, AVIF, TIFF, SVG and native HEIF paths had uneven limits. | Apply a 256 MB encoded-file ceiling, format-specific 16 MB SVG ceiling, 16,384-axis and 64-megapixel decoded ceiling, checked arithmetic, post-decode dimension consistency, a 512 MB encoded-output ceiling, and bounded codec thread/count settings. |
| Medium | Animated WebP and multi-image AVIF could enter work and allocation paths outside Paint's single-canvas contract. | WebP and AVIF import. | Reject animated WebP, set AVIF image count to one, disable progressive decode, cap dimensions and pixels, ignore EXIF/XMP, and update the pinned libavif archive from 1.4.0 to the current signed 1.4.2 release with SHA-256 verification. See the [libavif 1.4.2 release](https://github.com/AOMediaCodec/libavif/releases). |
| Medium | A non-HEIF file renamed `.heic` was handed to the broad macOS ImageIO type dispatcher. | macOS HEIC/HEIF import. | Require an ISO BMFF `ftyp` box containing a recognized HEVC still-image brand, validate the nested box structure and bounded `ispe` extents before ImageIO receives the bytes, then check decoded dimensions again before raster allocation. |
| Low to medium | GIF and WebP export added native encoder allocation and temporary-file ownership paths that were unnecessary for the required save formats. | Every GIF or WebP save. | Remove both export paths and the bundled GIF encoder. Keep GIF rejected and WebP import-only. |

## Dependency reachability after the patch

| Input | Decoder path | Accepted profile |
| --- | --- | --- |
| PNG | patched `stb_image` from memory | Still image; APNG rejected; 64 MP maximum |
| JPEG | patched `stb_image` from memory | Baseline/progressive still image; 64 MP maximum |
| BMP | patched `stb_image` from memory | Direct 24/32-bit only |
| TGA | patched `stb_image` from memory | 64 MP maximum |
| GIF | none | Rejected on import and export |
| WebP | libwebp from memory | Static only; 64 MP maximum |
| AVIF | libavif 1.4.2 plus system dav1d | One image, no progressive decode or metadata, 64 MP maximum |
| TIFF | libtiff 4.7.2 in release builds, through memory callbacks | One uncompressed contiguous 8-bit RGB/RGBA scanline image |
| SVG | TinyXML-2 inspection, then LunaSVG 3.5.0 | Vector elements only; no `<image>` or filters; bounded bytes/tree/depth |
| ICO/CUR | Rainstar Paint's checked container parser; PNG entries use the PNG path above | Existing entry/count/dimension bounds plus the global canvas limits |
| HEIC/HEIF on macOS | ImageIO after Rainstar signature/profile gate | Recognized HEVC still-image brands; 64 MP maximum |

The ordinary integrated Debug build used the installed libtiff 4.7.1 because the developer configuration uses `find_package(TIFF)`. The separate C/C++ sanitizer build and fresh Windows cross-build set `RAINSTAR_BUNDLED_TIFF=ON` and used the reduced libtiff 4.7.2 configuration. Release evidence must still verify the dependencies in each actual package rather than treating a development or cross-build inventory as proof of the shipped artifact.

## Networking review

No direct networking implementation was found in Rainstar Paint, the linked GUI.Forms host, or the directly linked codec closure. The review searched first-party and reachable dependency source for socket creation, address resolution, HTTP client APIs, WinHTTP/WinINet, libcurl, CFNetwork, URL-session clients, WebSockets and equivalent entry points. Undefined-symbol scans of the built `paint-forms-tests` and `paint-codec-security-tests` executables found no socket, connect, DNS, send/receive, WinHTTP, WinINet or curl imports. The linked-image inventory contains libtiff, libwebp, dav1d, ImageIO/CoreGraphics/CoreFoundation, GUI.Forms and normal platform GUI/runtime libraries; it contains no network client library.

There are explicit operating-system service boundaries that can use a network because the user configured them to do so:

- Native file dialogs can show mounted network volumes and cloud-backed filesystem providers. Paint still receives an ordinary path and performs no network protocol itself.
- Linux printing uses GTK/CUPS or invokes `lp`/`lpstat` in the GUI.Forms integration. CUPS may send a selected job to a configured network printer.
- Linux acquisition launches `simple-scan` or `xsane`; those applications may talk to a configured network scanner.
- macOS acquisition opens the local Image Capture application by bundle identifier. The `NSURL` symbols in that path and GUI.Forms are file/application URLs, not HTTP clients.

Those actions are visible user commands delegated to the operating system or another application. They are not background telemetry, update checks, remote-control logic or decoder-initiated networking. This audit therefore supports the narrower, testable statement: **Rainstar Paint and GUI.Forms contain no direct network client code or direct network API imports in the reviewed build.** It cannot prove that arbitrary platform services, filesystem providers, printer/scanner drivers or future versions of system frameworks never communicate.

## Verification performed

- Integrated Debug build with warnings enabled; all six CTest targets passed.
- Core file-security and codec tests, including destination-symlink replacement, regular-file symlink reading, FIFO rejection, GIF dispatcher exclusion, paletted-BMP rejection, SVG external-resource rejection and HEIF format-confusion rejection.
- GUI.Forms clipboard test with malicious dimensions/stride/backing storage.
- Preference and font-fallback tests with a named pipe in place of the selected file.
- Deterministic malformed-input mutations for PNG, JPEG, BMP, TGA, WebP, GIF, AVIF, TIFF, SVG, HEIF, ICO and CUR-relevant container code.
- AddressSanitizer and UndefinedBehaviorSanitizer instrumentation for both C and C++; all four non-GUI test targets passed. LeakSanitizer is unavailable on this macOS host.
- Fresh Windows cross-build of the application and tests with the reduced libtiff 4.7.2 configuration. This verifies compilation and linking, not execution on Windows.
- Source and linked-symbol networking scans.
- Repository style check and `git diff --check`.

## Residual risk and release requirements

Image and font parsing cannot be made memory-safe by validation wrappers alone. LunaSVG, ImageIO, libtiff, libwebp, dav1d, libavif, stb and operating-system clipboard decoders remain native-code components. The format/profile restrictions reduce reachability and sanitizer mutation tests add evidence, but neither provides privilege isolation or a formal proof of parser correctness. If one of the retained native paths permits code execution while Paint is elevated, that code can inherit the process authority.

Before shipping this patch:

1. Build the release configuration with bundled libtiff 4.7.2 and the pinned libavif 1.4.2 archive on each supported platform.
2. Run the normal CTest suite, the malformed codec corpus and platform-native open/save/clipboard exercises under the release candidate.
3. Verify the actual packaged dynamic-library inventory. A development binary linked to a package-manager library is not evidence for a statically bundled release.
4. Keep the reduced format list in release help and file-dialog filters. Restoring GIF, paletted BMP, broad TIFF, SVG raster images, PSD, PNM, HDR or PIC requires a new decoder review rather than removing the gate.
5. Treat new image or clipboard parsers as hostile-input boundaries and preserve the same byte, pixel, frame and arithmetic limits.
