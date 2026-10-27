# Tool cursors

The cursor sheet in `assets/cursors/tool-sheet.png` was drawn and filled in
Plan Paint. It is a transparent 128 × 128 PNG, arranged as sixteen 32 × 32
cells in `Tool` order. Edit this master in Paint, then run
`python3 scripts/embed-cursors.py` to regenerate the embedded bytes. No runtime
asset lookup or image file is required. Embedded decoding failure uses a stock
cursor for the tool.

The contact is deliberately part of the artwork:

| Tools | Contact |
| --- | --- |
| Pencil | graphite nib at (10,24) |
| Brush | lower-left bristle corner at (10,25) |
| Picker | dropper tip at (8,26) |
| Eraser | leading erasing corner at (11,20) |
| Text | I-beam center at (16,16) |
| Magnifier, Stamp | transparent native cursor; the canvas preview supplies the pointer |
| Select, Lasso, Fill, Shape, Path, Reshape, Guide, Freehand, Spirograph | dark corner triangle tip at (3,3) |

Coordinates are relative to each 32-pixel cell. The pointer is upright in screen
space; view rotation continues to inverse-map the contact position through
Paint's existing canvas transform. The cursor never changes document pixels,
brush size or stroke coordinates.

Magnifier and Stamp use a transparent native cursor while their preview is
available. Outside the image, or before loading a stamp, a stock crosshair
remains visible. Their original sheet cells are retained as editable artwork.

`src/cursors/tool_cursors.cpp` builds immutable 16/24/32/48/64-pixel
representations once. A one-pixel white outline is added for dark backgrounds.
Sampling is anchored on each contact point. The toolkit then selects and scales
representations using one normalized hotspot. The nominal size is 32 logical
pixels. Existing canvas brush-size previews remain independent of this fixed
pointer artwork. Resize handles and apparatus interaction retain their stock
resize/hand overrides. Picker dragging and middle-button panning display a hand
until release, then restore the tool cursor.

`apply_tool_cursor(control, tool)` is the narrow consumer integration point.
The ribbon is unchanged. Unsupported future Tool values safely return the
crosshair fallback; when adding a tool, add its artwork, hotspot and mapping
rather than borrowing an unrelated existing symbol. Text uses the stock I-beam
fallback. The backend lives in GUI.Forms and contains no Paint-specific tools.

## Building

The checksum-pinned toolkit patch in `third_party/gui-forms.lock.json` extends
the existing SDK with `CursorImages`, `Control::set_custom_cursor` and native
AppKit, Win32 and Xcursor adapters. Follow `docs/GUI_FORMS_PORT.md` to fetch,
build and install that SDK into a separate prefix, then configure Paint against
it. An older SDK is rejected at configure time. Do not mix old GUI.Forms headers
or binaries with the extended SDK: the public C++ class layouts have changed.

## Validation

`paint-cursors` checks the fourteen visible artwork contacts and transparency of both preview cursors at eight scale factors,
including fractional DPI, and requires each hotspot to land on an opaque dark
contact pixel. The Forms fixture checks tool routing, resize cursor precedence,
and restoring the pencil after leaving a handle. Run CTest and the house-style
check after artwork or integration changes.

GUI.Forms' cursor tests cover validation, inheritance, thread/shutdown policy,
fallback after native failure, handle reuse, native scale/coordinates, and
resource destruction. Native macOS, Win32 under Wine, and Linux/X11 under Xvfb
are different evidence categories. Direct Wayland is not an implemented host;
Linux uses the existing X11/XWayland route.
