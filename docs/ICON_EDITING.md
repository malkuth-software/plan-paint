# Icon and cursor production

## External edits and explicit reload

Opening or saving an image establishes its disk baseline. Plan Paint checks its
file size and modification time once per second in a background task. A change
shows a notice below the ribbon, with **Reload from disk…**. Missing files and
unavailable paths have distinct notices; their reload button is disabled. The
notice also works with the status bar hidden. This applies to ordinary images,
icons and cursors; an external edit never replaces the working document by itself.

**File → Reload from disk…** is also available without a notice. Reload replaces
the complete document, including every icon representation and cursor hotspot.
If there are unsaved edits or an active text, selection or reshape session, it
asks before discarding them. The default is No. Save a copy first to preserve
local work. A canceled reload retains editing state. A missing, corrupt or
changing source fails before the working document is replaced. A successful
reload starts a clean document and clears its previous undo history.

Save checks the baseline again before replacing the current source. If the
source changed, disappeared or became unavailable, replacement requires explicit
confirmation, defaulting to No. Declining keeps the document dirty and the disk
file intact. Saving to another path keeps the external source intact. A
successful save or reload establishes a new baseline; opening or creating a
different document retires outstanding checks for the previous file.

These are metadata checks, not file locking or content hashes. A change that
preserves both size and timestamp cannot be detected. Another application can
still change the file between the last check and the read or write. No source
application plug-in, proprietary bridge or private file format is involved.

## Validate an icon or cursor family

**Icon → Validate** audits every current representation without editing the
document. The default required square sizes are 16, 24, 32, 48, 64, 128 and 256
at 32 bits per pixel. These are Plan Paint's selected production profile, not
a claim that every consumer requires every size. Enter 1–32 distinct,
comma-separated sizes in the 1–256 range, and choose 32 bpp, 8 bpp, 4 bpp or
**Any depth**. A rectangular image does not satisfy a required square size.

Each representation is encoded with the same ICO/CUR writer used by Save, then
decoded in memory. This tests the actual retained or newly generated payload.
The audit records the decoded output storage and depth, malformed image data,
invalid hotspots, and encoding/decoding failures. A failed representation cannot
satisfy the profile. Missing sizes are computed at the requested saved depth;
changing an indexed image to ordinary RGBA may therefore make an indexed profile
incomplete. Validation does not add missing sizes or change depth automatically.

Duplicate groups contain original one-based ordinals sharing saved width, height
and bit depth. Different depths at the same dimensions remain distinct. PNG and
DIB representations with the same dimensions and depth are in the same duplicate
group. Duplicates are advisory and are never deleted automatically.

For each valid representation, the report shows imported encoding when available,
current save encoding, and pixel counts for alpha zero, partial alpha (1–254),
and opaque alpha (255). It keeps three distributions separate: the immutable
source snapshot, current pixels, and pixels decoded from the saved payload. The
alpha-change count compares current and decoded alpha at every pixel, so a change
is visible even when aggregate counts happen to match. Legacy XOR pixels are
counted separately: their zero-alpha canvas sentinels do not mean that the
original icon is transparent on every background.

The compatibility notes call out PNG-aware consumers, partial alpha, legacy XOR,
custom dimensions and cursor hotspots. They describe conditions to verify in the
target application; they are not a certification of that application. Microsoft's
[icon resource overview](https://learn.microsoft.com/en-us/windows/win32/menurc/about-icons)
describes multiple sizes and depths, while its
[PNG icon format notes](https://devblogs.microsoft.com/oldnewthing/20101022-00/?p=12473)
describe PNG entries and their alpha-derived masks.

Upscaling is reported where sources and targets are selected in generation and
strip previews. Shadow clipping is reported in the effect preview. The family
audit does not invent that history from saved pixels.

Changing the profile invalidates the report. **Validate** captures a fresh current
family and runs the audit on a background worker; closing cancels it. **Save
report** writes the reviewed snapshot as version-1 JSON. Its `requiredSizes` and
`requiredDepth` describe the profile (`0` means any depth); `missingSizes`,
`duplicateOrdinals`, and `frames` retain the findings. Frame ordinals are
one-based. Alpha arrays follow the explicit `alphaBuckets` order of zero,
partial, opaque. `valid` means that this representation encoded and decoded;
it does not mean profile completeness or universal consumer compatibility.

## Toolbar strips from Atlas cells

For a sprite sheet, **Atlas → Toolbar strip** uses the current Atlas cell grid.
For an ICO or CUR, **Icon → Export → Toolbar strip** uses its representations.
The owned popup captures the current frames, including edits synchronized from
the active cell. Export does not change the source document, selected sequence,
cell grid or Undo history. The source file and its existing filesystem aliases
cannot be replaced by this export.

The **Image order** field lists one-based frame numbers separated by commas.
It starts with the Atlas selected sequence, or the first 256 frames when no
sequence is selected. Change the list to reorder, omit or repeat frames; each
occurrence produces one output cell. Empty entries, unavailable ordinals and
more than 256 output cells are rejected.

Cell width and height accept 1–4096 pixels; Columns accepts 1–256. A horizontal
strip uses at least as many columns as listed images; one column produces a
vertical strip. Other values produce a row-major grid. Horizontal and vertical
gaps independently accept 0–256 pixels and occur only between cells. Incomplete
last-row cells retain the selected output background. The output is bounded by
32768 pixels per axis and 100 megapixels before allocation.

By default, artwork keeps its original pixel dimensions and is centered in each
cell, with an odd spare pixel on the right or bottom. Oversized artwork is
rejected rather than clipped. Explicit **Fit image** scales each source to fit
its cell while preserving aspect ratio, rounds the dimensions to the nearest
integer and uses CONV area resizing. The manifest names every source ordinal,
its destination position and dimensions, and marks any upscaling. Without
resizing, transparent PNG output copies exact RGBA values, including RGB under
zero alpha.

Choose **PNG** with transparency or an opaque **Matte RGB** background. **BMP**
requires opaque matte output: selecting it disables transparency and invalidates
any existing proof. The editable hexadecimal matte defaults to the alternate
color; its alpha is ignored. Artwork is composited onto the same matte shown in
the preview and encoded in the file. Gaps and empty cells receive that matte.
Legacy background-dependent XOR representations are rejected for both formats.

**Preview** renders on a background worker and shows a fitted checkerboard proof
plus the output manifest. Changing any input disables **Export** until another
preview succeeds. Closing cancels the job. Export uses the reviewed pixels and
requires a filename matching the reviewed PNG or BMP format; ordinary file-dialog
overwrite confirmation and atomic publication apply. The checkerboard is a
preview aid and is not written into transparent PNG files.

## Numeric transforms

**Icon → Transform** and **Home → Rotate and flip → Transform** open the same
before/after dialog. An active selection is the source; otherwise the current
image is the source. Other icon representations and cursor hotspots are unchanged.

Offsets accept −16384 to +16384 pixels; X and Y scales independently accept
1–1600%; the clockwise angle accepts −360° to +360°. Controls retain two decimal
places. Scale is applied about the source pixel-center midpoint
`((width−1)/2, (height−1)/2)`, then clockwise rotation, then the offset. Positive
X moves right and positive Y moves down. Scaling does not resize the document.
The whole-image result is clipped to the existing canvas, with transparent pixels
where the transformed image has no coverage. A transformed selection retains its
floating bounds and remains editable, including portions outside the canvas.

Sampling offers **CONV* point**, **CONV* minification** (default), and **CONV* area**,
using the same continuous premultiplied material as Reshape. Point evaluates the
material at output pixel centers; minification integrates a footprint when
shrinking; area integrates the output pixel footprint even at unit scale.
Integer translation with point or minification sampling copies exact RGBA bytes,
including RGB under zero alpha. Neutral settings with these two modes create no
edit. Area sampling can change pixel values at unit scale.

**Preview** computes from the captured original on a background worker. Editing
a control invalidates the previous result and disables Apply until another
preview completes. Closing cancels outstanding work. Preview panes composite
on a transparency checkerboard; small images use integer enlargement for
inspection. **Apply** creates one Undo step. Undo restores both pixels and
selection state. A newer image, representation or selection invalidates a stale
preview before any edit. Unsupported background-dependent legacy XOR remains
protected by the existing RGBA transformation guard.

## Contextual Icon ribbon

Opening an ICO or CUR selects the **Icon** tab. Ordinary bitmap and sprite-sheet
documents do not show that tab. Switching to Home or a painting tool retains the
current representation; switching back to Icon does not change pixels or create
an Undo entry. Selecting the active tab collapses or restores the ribbon using
the same behavior as the other Paint tabs. Leaving the icon document returns an
active Icon page to Home.

The command plane groups **Images** (sizes, Gallery and cursor Hotspot), **Color
& alpha** (Palette and Alpha tools), and **Production** (States, Compare and
Export). For a CUR, Sizes opens cursor generation and Hotspot is enabled. For an
ICO, Sizes opens icon generation and Hotspot remains unavailable. The horizontal
representation strip uses the same selection, Ctrl-click sequence, reference
and keyboard behavior as Atlas. Previous and Next step through the images or
selected sequence. The status row shows the current image number, dimensions,
imported format and current save encoding.

**Home**, its tool-specific settings, and **Atlas** remain available. Atlas still
owns sprite-grid editing and provides the original icon command routes. The
workflows below are also available through their corresponding Icon commands.
Asset browsing stays under **File → Browse assets…**.

## Representation formats and preservation

The Icon and Atlas strips identify each image by its ordinal, dimensions and imported
format, such as **1 bpp DIB** or **2 bpp PNG**. Same-size images remain separate
representations. The strip status distinguishes **Source** from **Save**: Source
describes the imported encoding; Save describes the encoding that the current
image would use if saved now. Newly generated images show RGBA as their source.
Home tools normally paint RGBA. Index paint preserves stored palette indices for
Pencil, Fill, Eraser and Picker; the Palette window edits retained DIB entries.

An untouched imported representation retains its original standard PNG or DIB
payload when saved to ICO or CUR. Editing a different representation does not
convert it. A cursor hotspot-only change updates the directory coordinates while
preserving the image payload. The container directory is rebuilt, so this is
payload preservation rather than a whole-file byte-identity guarantee.

Changing pixels, hidden RGB under transparent pixels, dimensions or legacy XOR
data causes that representation to be encoded from its current pixels. Edited
and generated images use 32-bit DIB, except 256-by-256 ICO images without legacy
XOR data, which use PNG. Other representations keep their imported payloads.
Undoing the edit restores original-payload eligibility when the complete image
and XOR data match again. Source describes the import in the current session;
reopening a saved file reads the formats actually written to that file.

## Generating sizes

Choose **Icon → Icon sizes…** (or **Cursor sizes…** for CUR). The checked size
presets propose additions; existing images are kept by default. The target list
identifies each existing image by its one-based ordinal, so same-size variants
remain distinct. The manifest lists every keep, addition, replacement and removal
before anything changes.

Select a target to choose its **Source image** and **Color depth**. Each target
starts with the current image as its source, but its choice is independent of
all other targets. Source numbers refer to the document at dialog opening.
Generated rows identify their source and flag **Upscaling** whenever either
output dimension exceeds that source. Before and After show the selected target
against a checkerboard; a new target has an empty Before preview. Previews fit
inside their panels and are not a physical-pixel comparison.

Existing targets require **Replace this image** to be checked individually.
Choosing the replacement policy alone never selects all same-size variants.
Keeping one variant while replacing another of identical dimensions preserves
the kept image's pixels and imported payload. **Exact profile** explicitly removes
existing images whose dimensions are not among the checked square presets;
its removals remain visible in the target list and manifest. Changing a preset
retains source/depth choices for surviving rows and all custom additions.

Use **Add custom dimensions or another depth variant** to enter width and height
independently, from 1 to 256 pixels, then **Add image**. The appended target can
use RGBA, 4-bit DIB or 8-bit DIB, including another representation with the same
size and a different depth. **Discard custom target** removes that proposed
addition. All preset checkboxes can be cleared to build a custom-only profile.
The final container must contain 1–256 representations.

RGBA targets preserve continuous alpha. Indexed targets use the palette
conversion described below: alpha threshold 128, white matte and no dithering.
This policy is visible in the dialog whenever an indexed target is proposed.
For different quantization settings, use Palette conversion after generation.
Indexed targets save as their selected DIB depth, including 256-pixel images.
RGBA save encoding follows the ordinary representation output rules above.

Resizing uses Paint's CONV resampler, independently along the two axes; rectangular
sources are stretched to the requested dimensions. Choose proportional target
dimensions to retain their aspect ratio. Cursor hotspots follow the chosen
source's pixel centers on each axis, round to the nearest target pixel and clamp
inside the target. Sources from ordinary images or ICO start CUR hotspots at
zero. Legacy XOR sources cannot generate new artwork, but can be kept unchanged.

**Generate** validates the complete request and finishes every result before
publishing one document transaction. Every target reads the same original
source snapshot, even if another target replaces or removes that source. An
intervening document edit requires reopening the dialog. Errors do not leave
partial results. **Cancel** leaves the document and history untouched.

One Undo restores all representations and the source session; Redo restores the
result. Keeping everything without generation or removal creates no Undo entry.
The current image remains selected if it survives; otherwise the final output
image becomes current. Batch normalization uses the separate size policies described under batch
production below.

## SVG sources at each target size

Opening an SVG through File or the asset browser retains a validated vector
snapshot for that import session, alongside its initial raster. In **Atlas → Icon
sizes…**, or the size review shown when saving as ICO/CUR, eligible source choices
and target rows carry **SVG**. Every generated target is rendered directly from
that captured vector source at its own width and height, then optionally converted
to its chosen indexed depth. Targets never resize another generated representation.
Vector targets do not receive a bitmap-upscaling warning.

As with bitmap generation, explicit rectangular targets stretch to their requested
dimensions. The generated results are ordinary independent pixel images. Editing
the imported raster makes subsequent generation use those edited pixels; the SVG
mark disappears and bitmap upscaling warnings apply. Undoing back to the exact
imported pixels restores vector eligibility. This includes pixel dimensions, alpha
and hidden RGB. The imported vector is never substituted for changed artwork.

Interactive generation uses the captured source even if the original file is
subsequently moved or edited. Reopen the SVG to import a newer version. The source
snapshot is session-only: ICO, CUR and PNG contain their normal raster payloads,
not embedded SVG, and recovery restores raster artwork without vector provenance.
A new or newly opened document releases the previous import's source.

**Batch → Convert to ICO** also rasterizes SVG separately for every requested
size. It fits each rendering proportionally using the SVG's intrinsic dimensions,
including fractional dimensions, rounds the rendered pixel dimensions, and centers
it on transparent square padding. The manifest identifies independent SVG
rasterization. Review and execution each load one consistent vector snapshot;
the existing source size/time checks require a fresh review after ordinary file
changes and prevent publishing results from a changed source.

The existing SVG import restrictions remain in force, including rejection of
unsupported filters and embedded raster image elements. Vector parsing limits,
bundled font fallback, and the renderer are the same as ordinary SVG import.

## Effects and explicit artwork fitting

Choose **Icon → Effects** for the current ICO/CUR image. The window shows the
original and the prepared result. Small images use matching integer enlargement
with sharp pixel edges. Choosing an effect starts with its default
parameters; **Reset** restores those defaults. Changing controls never changes
the document. **Apply** commits exactly the prepared pixels as one Undo step;
Close or Escape cancels. A neutral result disables Apply, and a stale preview
cannot overwrite a changed document or another representation. Cursor hotspots
and all other representations stay unchanged. Indexed images become RGBA when
their pixels change; background-dependent XOR images must be converted explicitly
before these effects can run.

| Effect | Controls and numerical behavior |
| --- | --- |
| Brightness / contrast | Brightness −100…100 adds `brightness × 255 / 100` to encoded RGB. Contrast −100…100 scales distance from 127.5 by `2^(contrast/25)`. Both default to zero. Alpha is retained. |
| Hue / saturation | Hue −180…180 degrees rotates HSV hue. Saturation 0…200% multiplies HSV saturation and clamps it to 1. Defaults are 0 degrees and 100%. Value and alpha stay unchanged. |
| Grayscale | Rounded encoded RGB weights 0.2126 red + 0.7152 green + 0.0722 blue. Alpha stays unchanged. |
| Opacity | 0…100%, default 100%. Multiplies alpha; RGB, including hidden transparent RGB, is retained. |
| Blur | Integer radius 0…32 pixels, default 2. Separable Gaussian with sigma `max(0.5, radius/3)`, normalized over the finite radius. Samples beyond the image are transparent. Filtering uses double-precision premultiplied encoded RGB and alpha, with no byte rounding between passes. Radius zero is exact identity. |
| Sharpen | The same blur radius, plus strength 0…300%, default 100%. Adds `strength/100 × (original RGB − blurred RGB)` and clamps; original alpha is retained. Zero strength or zero radius is exact identity. |
| Shadow | Gaussian radius 0…32, X/Y offsets −128…128 pixels, opacity 0…100%, and a hex color. Defaults: radius 2, offset (+2,+2), black at 55%. Source alpha shapes the shadow; artwork is composited over it. |
| Fit artwork within image | Explicit separate operation, with integer padding 0…127 pixels, default 2. Crops the nonzero-alpha bounds, scales proportionally with CONV* into the available rectangle, and centers on transparent padding. It may enlarge small artwork. Padding must leave at least one pixel on both axes; completely transparent artwork is unchanged. |

All effects keep the current image dimensions. Color adjustments skip fully
transparent pixels, preserving hidden RGB. Numerical channels are rounded to the
nearest byte after clamping. Blur and shadow produce transparent black where no
coverage remains. The effects window is bounded to the supported 1…256-pixel icon
dimensions.

The **Shadow** preview includes its entire overflow. A pink border marks the image
bounds, and the status counts nontransparent pixels outside them. Those pixels
are clipped from the applied image; the pink border is only a preview annotation.
To make room, close the shadow preview, explicitly apply **Fit artwork within
image**, then reopen Shadow. Shadow never automatically shrinks or expands the
artwork or canvas.

### Advanced adjustments and filters

Check **Advanced** in Effects to extend the effect chooser with Gamma, Levels,
Invert, Find edges, Enhance edges, Contour and Emboss. Unchecking it while an
advanced effect is selected restores neutral Brightness / contrast. It never
applies the dismissed preview. These filters use the same fixed current-image
scope, preview, cancellation and single-Undo Apply transaction as the basic set.

**Repeat effect…** recalls the last successfully applied effect, including its
parameters and shadow color, into a preview of the current image. It enables
Advanced when needed. Review and press Apply to repeat it; recalling settings
alone changes no pixels. The saved settings survive Undo and document changes
within the running editor. Canceled or invalid previews do not replace them,
and they are not persisted across application restarts.

| Advanced effect | Controls and independently defined numerical behavior |
| --- | --- |
| Gamma | 0.10…10.00 in steps of 0.01, default 1.00. Each encoded channel maps to `255 × (channel/255)^(1/gamma)`. Values above one brighten intermediate tones. |
| Levels | Input black 0…254, input white 1…255, gamma 0.10…10.00, output black and white 0…255. Defaults are 0, 255, 1, 0, 255. Input black must be less than input white; output black must not exceed output white. Normalize and clamp `v = (channel − inputBlack)/(inputWhite − inputBlack)` to 0…1, then map to `outputBlack + (outputWhite − outputBlack) × v^(1/gamma)`. Equal output endpoints produce a constant tone. |
| Invert | Each nontransparent pixel's RGB maps to `255 − channel`; alpha is retained. Applying twice restores the original pixels exactly. |
| Find edges | Encoded luminance `0.2126R + 0.7152G + 0.0722B`, Sobel horizontal kernel `[-1,0,1; -2,0,2; -1,0,1]` and its transpose vertically. The grayscale target is `hypot(gx,gy)/4`, clamped to 0…255. Flat regions are black at 100% strength. |
| Enhance edges | For each RGB channel, add `strength/100 × (4 × center − north − south − east − west)`. A flat field is unchanged. |
| Contour | Grayscale target `255 − abs(4 × center luminance − north − south − east − west luminance)`, clamped to 0…255. At 100%, transitions are dark on a white field. |
| Emboss | Grayscale target `128 + southeast luminance − northwest luminance`, clamped to 0…255. At 100%, flat regions are gray; increasing brightness toward the southeast produces a lighter relief. |

All four neighborhood filters expose strength 0…300%, default 100%. Find edges,
Contour and Emboss interpolate each original RGB channel toward their clamped
grayscale target by `strength/100`; values above 100% extrapolate before final
clamping. Zero strength is exact identity. The image border repeats the nearest
pixel. A neighboring sample contributes `sampleRGB × sampleAlpha/255 +
centerRGB × (1 − sampleAlpha/255)`, so hidden RGB in transparent neighbors cannot
create colored fringes. Each output keeps the original center alpha byte;
fully transparent center pixels retain their stored RGB unchanged. Arithmetic
uses double precision, and channels are rounded to bytes only at the output.

These formulas specify Plan Paint's implementation. Observing another editor's
menu entries does not establish identical algorithms or pixel output.

## Comparison and contact sheets

Open an ICO or CUR and choose **Atlas → Compare…**. The owned comparison window
uses a snapshot of the document at opening. Choose all representations, the
current image, or the sequence selected in Atlas. Sequence order and repeated
entries are retained; an empty sequence reports an error. Labels retain the
original one-based image numbers, dimensions and imported format. Newly generated
images are labeled RGBA. Imported format describes source provenance, as in Atlas.

Choose a light, dark, checkerboard, transparent or primary-color background.
The primary color is captured when the window opens and used as an opaque
background. Columns, padding, spacing, label visibility and text size determine
the generated sheet. Images keep their original pixel dimensions, centered in
uniform cells; the sheet renderer does not resize the artwork. Layout is checked
before allocation and rejects outputs larger than 16 megapixels.

**Fit sheet** fits the result into the preview area. **Actual pixels** maps one
image pixel to one physical display pixel, accounting for the host's display
scale. **4× pixels** maps each image pixel to four display pixels. Enlarged
previews use nearest-pixel sampling; scroll when the sheet exceeds the viewport.
The status reports both output dimensions and the display-to-image pixel ratio.

**Open as new image** turns the completed preview into an untitled ordinary
bitmap in the editor. The pixels match direct PNG export, including the labels,
background and transparency. Normal painting tools work on it; it has fresh Undo
history and is marked unsaved immediately. Undoing its first paint stroke still
leaves the generated document unsaved.

Opening the sheet follows Paint's normal Save / Don't Save / Cancel choice for
a modified source document. Save completes before replacement, including any
required icon-size review. Cancel preserves the source document and discards the
pending replacement. The new image has no source filename or icon representations,
so saving it requests a destination. Open and Export are unavailable while the
preview is being prepared or when the requested sheet cannot be rendered.

**Export PNG…** saves the prepared sheet through the normal save dialog. Background
and labels become part of that PNG; Transparent preserves alpha outside the
artwork and labels. Legacy background-dependent XOR pixels are evaluated against
the chosen opaque background. Transparent output is unavailable for those images
because ordinary PNG alpha cannot represent a background-dependent XOR operation.

Changing settings prepares a new preview and disables export until it is ready.
Closing cancels pending work. Preview settings and exported sheets do not change
the source document, its filename or Undo history. This command exports one
assembled sheet. Separate-file representation export is described below;
multi-file jobs are available through Batch production.

## Exporting separate PNG files

Open an ICO or CUR and choose **Atlas → Export…**. Choose all representations,
the current image, or the selected Atlas sequence. Enter a filename prefix and
choose an existing destination folder. The window takes a document snapshot when
opened and prepares the complete output manifest before enabling **Export PNG…**.
Review every name and status before running. Merely reviewing creates no files.

Each representation becomes a separate RGBA PNG at its original dimensions.
No resampling or palette conversion is requested. Names include dimensions,
imported source depth when available, and the original one-based image number:
`sample-32x32-src8bpp-003.png`. Here `src8bpp` describes imported provenance;
the exported PNG contains current RGBA pixels. New representations use `rgba`.
Same-size variants remain distinct. Repeated sequence entries get an additional
copy suffix. PNG does not carry cursor hotspots or background-dependent XOR
operations; XOR images produce a preflight error. An opaque contact sheet can
show their appearance against a chosen background.

Choose how to handle existing output names:

| Policy | Reviewed behavior |
|---|---|
| Skip existing | Default. Existing destinations are listed as skipped and remain unchanged. |
| Rename outputs | Resolve numeric suffixes during review. If a reviewed name becomes occupied before publication, report failure and leave that file unchanged; review again for a new name. |
| Replace existing | Explicitly permit replacement of regular files. Replacements are marked in the manifest. Directories and symbolic links are not valid replacement targets. |

Skip and Rename use an atomic publication step that cannot overwrite an existing
destination, including a file created after review. Each completed PNG is first
written and closed as a temporary sibling, then published. An output failure
preserves the earlier destination and is reported individually; other outputs
can still complete. Filesystems that cannot support the required publication
operation report failure rather than falling back to a non-atomic overwrite.

**Cancel** stops further outputs and keeps the results window open. A file already
being published may finish; completed files remain on disk and in the report.
Closing the window also cancels pending work and waits for the worker to stop.
There is no whole-job rollback. **Save report…** exports JSON with each source
ordinal, dimensions, source depth, destination name, state and diagnostic.
**Review outputs** prepares a fresh plan after a job or filesystem change.
Document pixels, filename and Undo history remain unchanged.

This command exports the representations that actually exist in one open
document. Requested-size fallback, choosing one depth per distinct size, and
multi-document batch conversion remain separate work.

## Browsing source artwork and stamps

Choose **File → Browse assets…** from any document. The browser uses ordinary
folders; it neither imports copies into a private library nor modifies files
while browsing. Enter a folder and filename search, then press **Search /
refresh**, or use **Choose folder…**, **Up**, and folder activation. Search
matches a substring of the current folder's filenames with ASCII case folding;
other UTF-8 characters match literally. Type filters distinguish ICO, CUR,
bitmap images and SVG. Folders remain available through type filters.

The list includes PNG, JPEG, BMP, TGA, TIFF, WebP, AVIF, HEIF, SVG/SVGZ, ICO and
CUR extensions. Import support and limits are the same as opening these files
normally, including platform-dependent HEIF support and the restricted TIFF
profile. Corrupt or unsupported image contents produce a visible error.
GIF, PSD, HDR and portable-map formats are not advertised as supported inputs.
Each scan is confined to one folder and stops after 8,192 matching entries or
50,000 inspected entries. A limited listing is explicitly labeled; narrow the
search or choose a subfolder. Results are sorted folders first, then by name.
An inaccessible or missing folder is an error, not an empty result.

Select a file to decode a preview in the background. ICO/CUR files retain all
representations, including duplicate dimensions, and the representation chooser
identifies each ordinal, dimensions and imported depth. The largest image is
selected initially. The preview fits inside its well using nearest sampling and
does not enlarge small images. It composites alpha over a checkerboard and
evaluates legacy XOR against that background without modifying source pixels.
Cursor hotspots appear in the details. Preview loading initially refuses files
larger than 32 MB; larger images can still be opened through the ordinary Open
command, subject to the normal decoder limits.

**Open** opens the complete image or icon container and honors the existing
unsaved-document prompt. **Use as stamp** loads the chosen representation into
the existing Stamp tool at its original dimensions and alpha, with one temporary
placement preview. Click or drag to position it. Moving the pointer away after
release leaves the preview anchored in bitmap coordinates, including in a rotated
view. Scale, angle and hardness remain adjustable in the Stamp context ribbon.
**Apply** (or Enter with the canvas focused) composites the final preview once
into the current bitmap and creates one Undo entry. **Cancel** or Escape discards
the preview without changing pixels or history. Leaving the tool, switching
representations or making another document edit discards a pending placement.
Saving before Apply saves the existing bitmap; the temporary placement is not
part of a file or recovery image.

The preview uses the same alpha composition, canvas clipping, guide constraints,
selection constraints and atlas wrap/preserve-alpha settings as Apply. Placement
uses integer top-left coordinates obtained by truncating the center minus half
the transformed stamp dimensions. Apply is disabled until the latest requested
scale or rotation finishes rendering; a late worker result cannot restore a
cancelled placement. Apply and Cancel clear the imported stamp. Stamps captured
from the canvas retain their existing continuous repeat-painting behavior.
Background-dependent XOR images cannot
be used as ordinary alpha stamps, and that action is visibly unavailable.
**Copy path** copies the selected file or folder path.

**Reveal in file manager** requests that the desktop file manager select the
chosen file or folder in its parent directory. It does not open the artwork in
another editor or change the Paint document. The action also works on a corrupt
image that cannot be previewed. An item deleted since the folder scan produces
an error before any desktop request. Symlinks retain their own identity rather
than being resolved to their targets.

Windows uses the public
[SHOpenFolderAndSelectItems API](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shopenfolderandselectitems)
and macOS uses
[Finder selection through NSWorkspace](https://developer.apple.com/documentation/appkit/nsworkspace/activatefileviewerselecting(_:)).
Linux first requests
[FileManager1.ShowItems](https://wiki.freedesktop.org/www/Specifications/file-manager-interface/)
through `dbus-send`, with a three-second reply timeout. If it is unavailable or
fails, `xdg-open` receives the containing folder's file URI. The browser explicitly
reports that fallback as a folder request with file selection unavailable. If
neither service accepts the request, the failure remains visible in the browser.

Requests run off the UI thread. Each uses the immutable selected path captured
at the click; navigating elsewhere prevents its completion status from replacing
the new selection's status. Closing cancels pending work and joins the request
worker. A request already handed to the desktop cannot be recalled, so an opened
file-manager window remains independent of Paint. Paths are passed as native
filesystem paths or percent-encoded UTF-8 file URIs, never shell commands.

Size and modification-time checks before/after decoding and before using a
preview detect ordinary external changes. A changed or missing file requires a
fresh preview. These checks are not a cryptographic guarantee against replacing
content while preserving both metadata values. Navigation keeps only the latest
pending request; completed work carries a generation that must still match the
window. Closing revokes queued results and waits for the active decode to finish.

**Bookmark folder** and **Remove bookmark** maintain explicit bookmarks in
`asset-folders.txt` beside the application preferences. The open UTF-8 format
starts with `Plan Paint asset folders v1` followed by a newline, then one absolute
path per line. Percent signs and control bytes are escaped as uppercase `%HH`.
Missing folders remain in the bookmark list. There are at most 64 bookmarks;
failed or malformed storage is reported and is not silently replaced.

This browser currently provides a filename list and a selected-image preview.
Thumbnail grids, multiple-asset job selection and metadata indexing remain
part of the broader production work. **Add to batch** adds the selected file to
the batch source list; multiple files can also be selected in Batch production's
ordinary file picker.

## Inspecting and transferring opacity

For an ICO or CUR document, choose **Atlas → Alpha tools…**. Like other Atlas
commands, opening the tool settles the current painting gesture and editable
geometry. The tool operates on the **entire current representation**, even if a
selection outline is present. Other sizes and depths are not targets.

The owned window shows the original and proposed result. **Alpha** displays
opacity as opaque grayscale: black is transparent, white is opaque and gray is
partial opacity. **Color** composites the actual RGBA image over a checkerboard.
Both previews fit without enlarging small artwork, using nearest sampling.
Statistics show the minimum and maximum alpha and counts for alpha 0, 1–254,
and 255. Changing the view does not change pixels or create history.

**Copy alpha** copies the original representation's opacity as an ordinary,
opaque grayscale clipboard image. **Paste alpha** reads an image from the
clipboard and prepares a replacement opacity mask. The conversion is defined
as `(R + G + B + 1) / 3` using integer division on encoded 8-bit channels. The
mask's own alpha is ignored. Grayscale values therefore transfer exactly;
colored masks use a rounded arithmetic mean, not perceptual luminance.

Mismatched dimensions leave Apply unavailable until **Resize mask to image**
is explicitly enabled. Resizing samples nearest pixel centers independently
on each axis; it can change aspect ratio and does not interpolate. The original
and mask dimensions remain visible. Disabling resizing restores the mismatch
error without changing the document. **Reset** discards the staged mask.

**Apply** changes alpha only, retaining every RGB byte, including hidden colors
under fully transparent pixels. It creates one Undo entry and closes the window.
An identical mask leaves Apply disabled. **Close** or Escape discards the preview.
An intervening document or representation change rejects Apply and requires
reopening the tool. Cursor hotspots and other representations remain unchanged.

Legacy background-dependent XOR artwork cannot be treated as an ordinary alpha
mask; the command reports that limitation before opening the tool. Clipboard
geometry and host errors are validated before preparing a result. This workflow
adds no layer or persistent channel document. Painting directly into an alpha
mask and viewing alpha on the main canvas remain separate work.

## Indexed palette conversion

Open an ICO or CUR and choose **Atlas → Palette…**. The owned window compares
the current image with a 1-, 4-, or 8-bit indexed result. Set the alpha threshold
(1–255, initially 128), an RGB matte color (initially white), and either no
dithering or Floyd–Steinberg error diffusion. The preview reports palette entries,
transparent pixels, and the number of partially transparent source pixels that
will be converted. The displayed threshold rule identifies the resulting binary
alpha. Previews cover the entire current representation, including when a
selection outline is present.

**Apply** replaces that representation with the displayed indexed image in one
Undo step. Other representations, their encoded payloads, and cursor hotspots
remain intact. Saving the unchanged converted representation emits an indexed
DIB at the chosen depth, including for a 256 × 256 image. Subsequent painting
invalidates that encoded snapshot and saves the edited image as RGBA; conversion
can be run again when the artwork is ready. This is explicit pixel conversion,
not an index-aware painting mode or a permanently linked export recipe.

Changing preview settings does not alter the document or its Undo history.
**Close** and Escape discard the preview. Invalid matte text disables Apply.
A document change after opening the tool prevents applying stale pixels. A
conversion with the same encoded payload leaves Apply disabled. The tool rejects
background-dependent legacy XOR pixels rather than silently flattening them.

The independently specified conversion is:

1. Source pixels with alpha below the threshold become transparent black. With
   the default threshold, 127 is transparent and 128 is opaque. Pixels at or
   above the threshold are composited against the matte in encoded RGB byte
   space, using `round(channel * alpha/255 + matte * (1-alpha/255))`; their output
   alpha is 255. Matte alpha is ignored. Hidden RGB is intentionally discarded
   for transparent output pixels. The original source is untouched until Apply.
2. A histogram counts every distinct opaque RGB color after matting. No sampling
   is used. When transparency requires a black palette entry, opaque black pixels
   use that entry and are excluded from the color histogram. Weighted median-cut
   splits the box with the greatest
   `population × largest channel range`; ties keep the earlier box. Channel-range
   ties prefer red, then green, then blue. Samples sort by that channel, then
   ascending packed `0xRRGGBB`. The split is the first cumulative count reaching
   half the population, clamped to keep both boxes nonempty. The left box keeps
   its position and the right box is appended. Each representative is the
   population-weighted encoded-RGB mean, rounded to the nearest byte.
3. Palette matching minimizes squared encoded-RGB distance, with lower palette
   index winning ties. With dithering, pixels scan left to right, top to bottom.
   Clamp the error-adjusted sample to 0–255 before matching; distribute the
   difference to right, lower-left, lower, and lower-right with weights
   7/16, 3/16, 5/16, and 1/16. Error that reaches a transparent pixel or crosses
   the image boundary is discarded. Calculations use double precision.
4. If any pixel is transparent, palette index zero is reserved as black, leaving
   1, 15, or 255 entries for additional colors at the three depths. Opaque black
   shares index zero, distinguished from transparency by its clear AND bit.
   The reserved black entry participates in opaque color matching; a 1-bit image
   can therefore contain black, white, and transparent pixels together. A completely
   transparent image has only that reserved entry in the active palette. Without transparency, all 2, 16, or 256 entries
   are available for opaque colors. Unused encoded palette slots are padded with
   black; the file carries the full table for its depth.
5. Output uses an uncompressed 40-byte DIB header, bottom-up color rows and AND
   mask rows, DWORD-aligned row strides, BGR0 palette entries, and most-significant
   bits first for packed indices. Transparent color pixels use the reserved
   black index and a set AND bit, producing ordinary transparency without XOR
   effects. The standard ICO/CUR directory retains representation order and
   cursor hotspots.

The public Windows bitmap layout is described by Microsoft's
[BITMAPINFOHEADER documentation](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapinfoheader)
and [bitmap header types](https://learn.microsoft.com/en-us/windows/win32/gdi/bitmap-header-types).
Palette construction and dithering above are Plan Paint behavior, not a claim
to reproduce another editor's numerical output. Shared batch palettes and file
interchange and index-aware canvas painting are described below.

## Editing indexed palette entries

For an imported or converted 1-, 4-, or 8-bit DIB representation, **Icon → Palette…**
(or **Atlas → Palette…**) opens in **Palette entries** mode. Choose the numbered
entry, enter its RGB color, inspect the original and recolored previews, then
**Apply**. The status reports the selected index and the number of pixels using it.
Choose **Palette conversion** in Operation to change depth or quantize again.

The decoder retains exact table order and pixel indices. Two entries with the
same RGB remain distinct: editing entry 7 affects only pixels whose stored index
is 7. Applying a palette edit retains bit depth, dimensions, mask, representation
identity and cursor hotspot, with one Undo entry. Unused entries can also be
edited. An unchanged RGB leaves Apply disabled and preserves the original bytes.
Changed results use the same documented indexed DIB layout as conversion above.

Palette RGB is opaque; transparency uses the separate ICO/CUR AND mask. A black
entry referenced by transparent pixels must remain black, because making its
masked color nonblack would create background-dependent XOR behavior. Such a
change is rejected before Apply. Existing legacy XOR representations require an
explicit conversion to ordinary artwork before entry editing. Indexed PNG
representations and images changed through ordinary RGBA painting require
explicit palette conversion before using this mode; indices are never guessed
from equal pixel colors. Changing a preview leaves the document untouched, and
an intervening document edit prevents a stale Apply.

## Painting stored palette indices

For a retained 1-, 4- or 8-bit DIB representation, **Icon → Index paint** enables
index painting and selects Pencil. In **Palette → Palette entries**, choose an
entry and press **Primary · Index paint** or **Alt · Index paint** to assign its
exact index. These choices do not edit pixels. Apply a pending RGB entry change
before choosing it for painting; the paint buttons are disabled while that change
is previewed. The Icon status identifies the primary and alternate index numbers.

Pencil writes a one-pixel line of the chosen index. Secondary-click uses the
alternate index. Fill follows four-connected pixels with the same stored index
and transparency bit: two entries with identical RGB remain distinct fill regions.
Picker reads the exact stored index, including duplicate colors. Eraser uses a
hard footprint at the current size and writes the first black palette entry with
the transparent mask bit; it fails if the palette has no black entry. These tools
write discrete indices rather than blending or introducing new colors.

Wrapping applies to pencil/eraser footprints and fill connectivity. Selection and
guide boundaries are binary in this mode: binary selections retain their inclusion mask, feathered selection coverage
of at least 128 is included, and guide blocking of at least 50% is excluded. Alpha protection keeps
transparent pixels untouched and prevents erasing. A floating pasted selection
must be placed first. Each changed gesture creates one Undo step, even when only
an index changes and the visible RGB stays the same. A no-op creates no Undo step.
Saving preserves the edited indexed depth, indices, mask and cursor hotspots.

Other painting tools continue to use RGBA and may invalidate retained indices.
A subsequent indexed stroke then asks for palette conversion or for Index paint
to be turned off. It does not guess index identity from the remaining RGB pixels.

## Palette files and shared batch colors

**Icon → Palette → Import palette…** reads standard GIMP RGB palette (`.gpl`)
files and selects **Shared palette** conversion. The smallest supported depth
that fits the imported entries is selected initially. You can change depth,
dithering, alpha threshold and matte before applying the preview. Switching back
to Palette conversion builds an image-specific palette again; Palette entries
edits the original retained indices. Importing or switching modes never changes
the document until Apply.

The file preserves ordered RGB colors, including duplicate entries. Names and
display-column metadata are not retained. Inputs are limited to 64 KiB and
1–256 colors. Both the original and named-header forms are accepted, with LF or
CRLF line endings. **Export palette…** saves the current preview's full encoded
palette, including unused padding entries, without applying its pixels. It also
works when a preview would make no change. The format follows the public
[GIMP RGB palette specification](https://developer.gimp.org/core/standards/gpl/).

Shared conversion never sorts, reduces or adds colors to the supplied list.
Nearest-color mapping uses squared distance in encoded RGB, choosing the first
entry on a tie. Dithering uses the existing Floyd–Steinberg option. A palette
larger than the selected depth is rejected. The file colors are opaque RGB;
transparency still uses the separate ICO/CUR mask. If any pixels fall below the
alpha threshold, the palette must already contain black. The first black entry
is used for those pixels, wherever it appears in the list. This prevents creating
legacy XOR pixels or shifting the owner's palette indices. Entries unused by
mapping remain in their original positions. The encoded table is padded with
black to the selected depth's capacity.

**File → Batch production → Shared palette** applies one palette to every
representation of every selected file. Choose the palette file, depth, dithering,
threshold and matte, then review the output manifest. ICO/CUR retain image order,
dimensions and hotspots. An ordinary image becomes a one-image ICO at its existing
size. Dimensions outside 1–256 pixels are rejected; use size conversion first.
No representation is silently omitted.

Review captures the exact palette colors. Run uses that captured list even if
the palette file subsequently changes; review again to import a newer version.
The manifest reports per-image partial-alpha conversion and transparent pixel
counts. The JSON report contains the palette path, exact ordered colors, depth,
threshold, matte and dither setting. Names end in `-palette.ico` or `-palette.cur`.
The existing batch collision rules, source-change checks, cancellation and
per-file atomic publication apply.

## Normal, hot and disabled icon families

Open an ICO or CUR and choose **Atlas → States…**. The window captures the
current document as a preview snapshot. Choose all representations, the current
image, or the selected Atlas sequence. Sequence order and repeated entries are
preserved. Select a representation in the Preview list to compare its original
pixels with its generated state.

The presets are starting recipes with adjustable saturation, brightness and
opacity. **Reset** restores the selected preset's numbers while retaining the
chosen scope and output policy.

| Preset | Saturation | Brightness | Opacity |
| --- | ---: | ---: | ---: |
| Normal | 100% | 0% | 100% |
| Hot | 120% | +10% | 100% |
| Disabled | 0% | 0% | 55% |

Saturation accepts 0–200%, brightness −100–100%, and opacity 0–100%. These are
Plan Paint recipes, independently defined in encoded RGB byte space. Version
`plan-paint-icon-state-v1` computes gray as `0.2126R + 0.7152G + 0.0722B`.
Each color channel becomes
`gray + saturation/100 × (channel − gray) + brightness × 255/100`, clamped to
0–255 and rounded to the nearest byte. Alpha becomes
`round(alpha × opacity/100)`. Fully transparent source pixels retain their hidden
RGB under the RGBA output policy. Calculations use double precision.

**RGBA** emits the transformed pixels with continuous opacity. **Indexed sources
→ DIB** converts 1-, 4- and 8-bit sources back to their corresponding indexed DIB
depths; 2-bit indexed sources become 4-bit DIB. Other source depths produce RGBA.
Indexed conversion uses the palette algorithm above, an alpha threshold of 128,
a white matte and no dithering. The policy note remains visible, and the report
identifies each representation's source and output format and counts partially
transparent pixels converted by this policy.

Every requested representation receives a result. Background-dependent legacy
XOR pixels cannot be transformed by these recipes; a failed representation stays
in the report and disables export of the requested family. Choosing a scope that
excludes it can produce a complete export. The tool does not silently omit failed
images. Images must fit the supported 1–256 pixel ICO/CUR dimensions, and cursor
hotspots must remain within their image.

**Export…** writes a separate standard ICO or CUR, retaining dimensions, order
and cursor hotspots. The suggested filename adds `-normal`, `-hot` or `-disabled`
to the source stem. A missing extension is added; a mismatched extension is
rejected. The source path, its existing filesystem aliases, and the current
document's path cannot be replaced by a state export. Replacement of another
existing output follows the save dialog's overwrite confirmation and atomic
per-file writing behavior.

**Save report…** writes JSON containing the versioned recipe, output policy and
per-image ready, failed or cancelled decisions. It requires a JSON filename.
Preview changes and exports do not modify the document or its Undo history.
Changing the document after opening the window does not change the captured
snapshot: reopen States to capture newer artwork. Closing the window cancels
pending work, and late results cannot replace a newer recipe preview.

## Multi-file batch production

Open **File → Batch production…** or **Icon → Batch…**. **Add files…** uses the
native multi-file picker. **Add to batch** in the asset browser queues the selected
file for this same window. The source list stays available while Plan Paint is
open. It identifies duplicate selections by ordinal; selecting a file twice
deliberately produces two separately named outputs.

Choose one of six operations:

- **Convert to ICO** creates one ICO per source, with the checked sizes. It uses
  the largest-area source image, choosing the first representation on an area tie.
  Rectangular artwork is scaled proportionally and centered on a transparent
  square. The manifest identifies the chosen source, padding and any upscaling.
- **Normalize sizes** accepts ICO and CUR. Add missing keeps existing images;
  Replace chosen replaces matching images while retaining other sizes; Exact
  profile lists and removes images outside the profile. Retained images keep their
  original payloads, including duplicate representations. Generated CUR images
  scale their hotspots with the artwork and include the centering offset.
- **Icon state variants** creates a separate normal, hot or disabled ICO/CUR
  family using the displayed saturation, brightness and opacity recipe. It
  processes every representation into RGBA output. A failed representation blocks
  that family's export rather than silently producing an incomplete file.
- **Export representations** writes each decoded image to its own PNG. Source
  and representation ordinals keep duplicate dimensions and repeated inputs
  distinct. Background-dependent legacy XOR cannot be converted to PNG.
- **Shared palette** converts every representation to the same ordered RGB palette,
  with the controls and review behavior described above.
- **Stamp** composites one fixed image onto every source. Ordinary images produce
  PNG; ICO and CUR retain their container kind, representation order, dimensions
  and cursor hotspots. Its placement and transparency controls are described below.

Choose a destination folder. The default **New output folder** mode proposes a
child named `Plan Paint output`, or the next free numbered name such as
`Plan Paint output 2`. The destination initially follows the first source's folder;
the folder picker or path field can change it. The review shows the full proposed
child path. It creates no directories or files. **Run** creates that exact folder
only when the first output is ready to publish. If the name becomes occupied after
review, run fails and requires another review instead of reusing the folder or
silently changing its name. Cancellation or stale input before publication leaves
no new folder. Completed outputs remain in their folder if later work fails or is
cancelled.

Choose **Skip existing**, **Rename outputs** or **Replace existing** to write
directly into the selected existing destination, then select **Review outputs**.
Review decodes and
validates the sources and prepares names without writing files. The manifest
shows each input-to-output relationship, state and diagnostic. Changing any job
setting invalidates the review. **Run** is enabled only for a review with at least
one ready output and no failed or cancelled entries; Enter in a settings field
does not run the job.

Names include the source ordinal: `open-1-icon.ico`,
`open-1-normalized.cur`, `open-1-disabled.ico`, or `open-1-image-2.png`.
Rename appends `-copy2`, `-copy3`, and so on. The review discloses replacement of
existing regular files. Outputs cannot replace any batch source or an existing
filesystem alias of one. Newly occupied destinations are skipped under Skip or
reported as failed under the other policies; Run never silently chooses a new
name that was absent from the review.

Processing reads one source at a time. A job accepts up to 256 source files and
uses the asset decoder's 32 MiB encoded-file limit. File size and modification
time are checked against the review and after processing; this detects ordinary
external edits but is not a content hash. Changing a source requires a new
review. Changing the canonical destination path also fails the affected output.

**Cancel** preserves completed files and records unstarted outputs as cancelled.
Publication is atomic per output file; a batch is not a filesystem-wide
transaction. The document and its Undo history are unchanged. Closing the window
cancels and joins its worker, and late results cannot reopen it. Settings are
unavailable during processing. After completion, another run requires review.

**Save report…** writes JSON with the operation, destination, collision policy,
size policy, requested sizes, state recipe and per-output identity, result and
diagnostic. Ordinals are one-based. `representationOrdinal` is null for whole
container output. Full source paths are retained in the report even when the
compact manifest shows only the filename.

### Fixed-image batch stamping

Choose **Stamp** in the batch operation list and use **Stamp image…** to select
an ordinary supported image, ICO or CUR. A multi-image stamp uses its largest-area
representation, with the first representation winning an area tie. The manifest
identifies that representation and its dimensions. The stamp is loaded once per
review or execution; source files are processed one at a time.

Choose one of nine anchors, **Foreground** or **Background**, **Original size**
or **Fit image**, X/Y offsets in pixels, scale percentage and opacity. Original
size starts with the stamp's pixel dimensions. Fit image scales proportionally
to fit each source representation before applying the percentage. Percentages
range from 1 to 800; opacity ranges from 0 to 100. X/Y offsets range from −8192
to 8192 and are applied after anchoring; positive offsets move right/down.

The output always keeps the source canvas dimensions. Left/top anchors start at
zero; right/bottom align the opposite edges; center anchoring floors half the
remaining space, including negative offsets for oversized stamps. Scaled dimensions
round to the nearest integer with a minimum of one pixel per axis. Resizing uses
Plan Paint's CONV kernel. Scaled stamps are limited to 8192 pixels per axis and
16,777,216 pixels in total.

Composition uses Plan Paint's double-precision, straight-alpha source-over
operation in encoded RGB. Stamp alpha is multiplied by opacity and rounded to
the nearest byte before composition. Foreground paints the stamp over the
source; Background paints the source over the stamp. Zero-alpha stamp pixels
are a no-op, including preservation of hidden source RGB. **Opaque** composites
the complete result onto the chosen **Matte RGB** color, making every output
pixel fully opaque. Matte alpha is ignored. No persistent layers are created.

**Review outputs** displays a bounded before/after proof of source **#1**, using
its preferred representation. Proofs are scaled for inspection, not a claim of
physical-pixel size. The manifest reports placement, scaled dimensions, upscaling
and the number of nonzero-alpha stamp pixels clipped outside each representation.
It lists all representations even when dimensions repeat. Changing any setting
clears the proof and requires another review. The proof does not accumulate
successive stamps.

Changed ICO/CUR representations use the ordinary RGBA output policy. Completely
unchanged representations retain their original encoded payload. Background-dependent
legacy XOR in either a source or the chosen stamp image prevents export of the
affected family. There is no silent omission of unsupported representations.

Output names include `open-1-stamped.png`, `open-1-stamped.ico` or
`open-1-stamped.cur`. Both the fixed stamp and every source are protected from
replacement, including existing filesystem aliases. The stamp's size and modification
time are rechecked before execution and publication, with the same ordinary-edit
detection limits as batch sources. Cancellation and per-file atomicity follow
the batch rules above.

The JSON report's version-1 `stamp` object records the fixed image path, reviewed
byte count, one-based image ordinal, scale mode, scale/opacity percentages,
offsets, foreground/background order and matte settings. `anchor` is zero-based
in a row-major 3 × 3 grid, from top-left (0) to bottom-right (8), with center (4).

The broader [icon production interview](https://github.com/malkuth-software/notes/tree/plan-paint/interviews/icon-editor) covers
additional palette, alpha, batch and asset-browsing capabilities. It documents the broader design scope; this page describes the currently
implemented behavior.
