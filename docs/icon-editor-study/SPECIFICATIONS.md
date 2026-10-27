# Independent behavioral contracts

Status: **CANDIDATE**. These contracts specify proposed Plan Paint behavior. They do not assert the reference application's internal algorithms. Evidence anchors identify the need; parameters, failure rules and acceptance cases below are independently chosen.

## S01 — Icon document and image identity

Inputs: a decoded ICO or explicit New Icon command. The document owns an ordered collection of representations. Each has a stable image ID, width, height, working pixel data, source/target depth, transparency mode, storage encoding and revision. Current image and selected set are separate properties.

The UI label is at least `32 × 32 · RGBA · PNG` or `32 × 32 · 16 colors · DIB`. Unsupported/unknown metadata is shown as unknown, not invented. Duplicate dimensions are permitted; size alone is not identity. Switching the current image changes the canvas and status without editing pixels. Closing an icon hides its contextual tab and invalidates its worker completions.

Import retains all decodable representations; unsupported ones produce an explicit import report. Do not silently save an incomplete replacement over the source after partial decode. Candidate policy: require Save As until the user accepts the dropped entries.

Acceptance: open a file with 16/32/48 RGBA and indexed entries; count and distinguish each. Reorder without changing image identity. Undo image removal restores pixels, position, metadata and selection. Never save an icon with zero images. Evidence: O01–03, P01.

## S02 — Multi-size generation

Inputs: immutable source bitmap(s), target list `(width,height,depth,encoding)`, source mapping, sampling options, palette policy and replace choices. Existing targets default to Keep. A preview shows Add/Replace/Keep and warns when source resolution is smaller than target.

Candidate first preset: 16, 24, 32, 48, 64, 128, 256 RGBA; a compact alternative is 16/32/48/256. These are editable presets, not universal platform requirements. Generate each target from its mapped source, never by repeatedly shrinking the previous output.

Compute all replacement pixels before committing. A successful operation is one Undo transaction. Cancellation, validation failure or allocation failure leaves the document unchanged. A stale source revision prevents commit and offers regeneration; it does not overwrite newer art.

Retain existing Paint numerical behavior unless a separately reviewed resampler change is approved. Specify pixel-center mapping, boundary extension, transfer function, premultiplication, ringing/clamping and final rounding in the chosen kernel's test contract. Use double precision; no fast-math. Preview and commit use the same kernel.

Acceptance: a 16-pixel hand-edited image survives Keep; selected replacement changes only that ID; all new sizes originate from the selected source; cancel after preview leaves Undo and dirty state unchanged. Evidence: O02–03, P02.

## S03 — Palette conversion and legacy transparency

Input RGBA and a target of 1/4/8-bit indexed output. Show palette generation policy, dithering method, transparent-index/mask policy and matte/alpha threshold. No “256 colors” setting is allowed to imply continuous alpha where the target cannot preserve it.

Proposed deterministic threshold: transparent when alpha is less than the configured threshold; default threshold 128. Thus 127 is transparent and 128 is opaque at default. This is a proposed rule, not an observed Axialis threshold. A matte option composites first, then quantizes; a binary-transparency option preserves a separately defined mask.

Palette count includes reserved entries. Quantization algorithm, tie-breaking, color space, traversal and dithering error diffusion must be named and versioned before pixel-exact tests are approved. “Adaptive” alone is insufficient specification. Shared palette across a batch is optional and must be selected before processing.

Acceptance: alpha values 0/1/127/128/254/255; palette capacity boundaries; opaque color equal to the transparency color; duplicate palette entries; fully transparent input; one-pixel image. No unwanted opacity change to the source document. Evidence: O02, A03, A05.

## S04 — Alpha inspection and editing

View Alpha renders current alpha as grayscale, white for opaque and black for transparent, without changing stored pixels. Copy Alpha produces an ordinary grayscale bitmap. Paste Alpha requires matching dimensions or an explicit resize choice, uses a documented grayscale conversion, replaces alpha only, and commits once. Keep hidden RGB values where the existing image model can represent them.

“Preserve transparency” while painting means the existing Paint alpha-lock behavior; “Preserve alpha” during export means retain transparency in output. Labels and help must distinguish these.

Candidate alpha-only painting mode must show a persistent mode indicator and use the same document Undo; no separate layer is created. Leaving the view does not discard alpha edits. A view-only mode must never pretend to be editable.

Acceptance: copy/paste known alpha ramp preserves all values; RGB stays identical for alpha-only operations; cancel mismatch dialog has no effect; preview background changes never dirty the document. Evidence: A10, O06, P01.

## S05 — Effects and preview transactions

An effect dialog captures current image ID, revision, selection mask, parameters and scope. The preview is derived from that snapshot, not from the previous preview. Changing a slider supersedes older work. Cancel revokes callbacks and restores the unchanged document/view. Apply commits exactly once if the snapshot is current.

Scope defaults to selection if a nonempty selection exists, otherwise current image; the dialog spells this out. Applying to several representations requires an explicit target list. Unsupported depth/mode prompts conversion or records a skip; it cannot silently claim success.

Every effect needs a numerical contract before implementation: color space, alpha treatment, boundary behavior, valid ranges, rounding and empty-image result. Blur radius is not automatically Gaussian sigma; name the quantity actually implemented. Sharpen/emboss operate on defined channels and preserve alpha by default. Repeat Effect reuses parameters but revalidates the current scope.

Acceptance: slider A→B→A produces the same preview as A; Cancel adds no Undo entry; Apply/Undo/Redo restores exact pixels; closing the document during work cannot commit; changing selection invalidates or explicitly resnapshots the preview. O04 demonstrates the visual Cancel behavior; it does not establish exact pixel equality.

## S06 — Flattened shadow

Input: current bitmap/selection, shadow color, opacity, angle, distance and blur size. UI units are degrees, pixels and percent. Proposed angle convention: 0° right, 90° down in image coordinates, positive clockwise. State this near the angle control; do not infer the reference convention from its displayed −45°.

Create a shadow mask from source alpha, apply the specified blur and offset, color it, composite behind the source and flatten once. Use a named source-over composition contract. Selected-region behavior includes a defined selection boundary, not an implicit full-image shadow.

Default keeps dimensions. Preview indicates clipping; Fit artwork is a separate operation. Fractional offsets require a specified sampling rule. Exact blur kernel and defaults await effect design; O05's values are evidence, not adopted constants.

Acceptance: zero opacity is a no-op; zero distance is centered; fully transparent source remains transparent; no unrequested canvas growth; clipping warning matches affected bounds; one Undo. Evidence: O05, A13.

## S07 — State variants

Input: selected images/icons and named Normal/Hot/Disabled recipes. Output is separate documents/files with deterministic suffixes. Current document pixels are unchanged unless an explicit Replace action is chosen.

Candidate Disabled recipe is adjustable desaturation, brightness and opacity with preview. Candidate Hot recipe is adjustable saturation/brightness; avoid presenting an unspecified vendor formula as a standard. Recipes have stable version IDs. Indexed representations are regenerated through the chosen palette policy or explicitly skipped; report the result per representation.

Acceptance: a ten-image mixed-depth source produces ten reported decisions; none disappears silently. Same input/recipe yields the same pixels. Alpha changes only when the recipe requests it. Existing output collision handled by S10. Evidence: O11.

## S08 — Stamp composition

Asset input uses ordinary decoded pixels or explicit SVG rasterization. A single temporary placement owns its transform, opacity and clipping preview. Apply flattens into the current bitmap; Cancel leaves it untouched; no persistent layer stack or hidden editable objects.

Batch stamping accepts one fixed image plus a source list, foreground/background choice, anchor, offset, scaling rule, output dimensions and alpha/matte. Candidate default is centered, natural size, clipped to each source canvas. Because reference alignment was not measured, this is an independent explicit rule.

The implemented batch contract is documented in [fixed-image batch stamping](../ICON_EDITING.md#fixed-image-batch-stamping). It keeps each source's dimensions, exposes nine anchors, offsets, natural/fit scaling, opacity and optional matte, and reports clipping for every representation. The before/after proof is for the preferred image of source #1. These are independently specified Plan Paint behaviors, not measurements of Axialis formulas or owner answers to this interview.

Acceptance: transparent stamp is a no-op; foreground/background produce known source-over results; repeated preview does not accumulate; Apply is one Undo for document editing. Disk jobs follow S10. Evidence: O12, A06–09.

## S09 — Representation export and normalization

Export modes are distinct:

- **One requested image:** exact match by default; optionally use a displayed fallback.
- **All sizes:** one selected depth policy per distinct dimension pair.
- **All representations:** every image, with depth and duplicate ordinal in filenames.

Candidate fallback order: exact dimensions/depth; exact dimensions with convertible depth; smallest image at least as large in both axes; otherwise largest available. Resolve remaining ties by selected preferred depth, then original order. The manifest names the actual source. Do not claim this is Axialis's unmeasured “nearest” algorithm.

Normalization offers Add Missing or Exact Profile. Exact Profile lists retained, added, replaced and removed IDs. Empty profile is invalid. Missing generation can be disabled, in which case unsatisfied files are reported instead of called normalized.

Acceptance: duplicate 32-pixel representations never overwrite each other in All Representations; strict mode reports absent targets; exact normalization shows removals; preview and committed profile agree. Evidence: O09–10, A04–05.

## S10 — Batch jobs and filesystem results

Inputs: explicit source list, immutable operation plan, profile version, destination and collision policy. Preflight resolves the complete output manifest and validates write locations, naming and duplicate targets. Candidate default is a new destination folder and no replacement.

Each entry has source, operation, actual representation, destination, pending/running/completed/skipped/failed/cancelled state and diagnostic. Run remains unavailable while required choices or name collisions are unresolved. Recheck each destination immediately before committing because the filesystem may change after preflight.

Write each output to a temporary sibling, finish/close it successfully, then publish using the host's supported replacement semantics. On failure preserve the prior destination; delete only job-owned incomplete temporary files. Never imply global atomicity. Cancellation stops new work and cleans uncommitted job temporaries; completed outputs remain listed. A report can be saved as open JSON/CSV plus a human-readable summary.

Use bounded worker memory, stable input ordering and immutable job inputs. UI updates use the public Threading boundary. Window close offers background continuation only if an independent job owner exists; otherwise cancel and wait for safe shutdown.

Acceptance: permission-denied output, disk-full simulation, duplicate filename, source disappearing, cancellation after one completion, externally created collision and stale input. Verify truthful counts and preserved pre-existing files. Document Undo is not a batch rollback. Evidence: O09–12; these safety/recovery details are independent proposals.

## S11 — Asset browser

Owned popup with location, folder/bookmark selection, search, type filters, thumbnail grid/list and details for the selected item. Ordinary files are authoritative. Show filename, dimensions or image count, depth/alpha where known, file type and readable failure state. Decoding a thumbnail must not block navigation.

Selection supports keyboard equivalents for open, preview, stamp and add-to-batch. Pending thumbnail results carry folder generation IDs; stale results cannot populate a different folder. Optional recursive search is explicit. Missing/unreadable files are distinguishable from no search matches.

Bookmarks do not copy or modify assets. Optional metadata index is open, local and rebuildable. No vendor pack or proprietary library container is required. Browser operations route through the same document/job commands as menus.

Acceptance: fast folder changes reject stale thumbnails; corrupt image displays a diagnostic tile; focus returns to the launching command; multi-selection survives a view-mode change; high-DPI labels do not overlap artwork. Evidence: O08, P03, A01.

## S12 — Actual-size preview and contact sheet

Preview displays multiple representations with full identity labels. Actual-size mode maps one source pixel to one physical output pixel where the host supports it; otherwise label the scale explicitly. Light/dark/checker/custom backgrounds affect display only. Inspection magnification uses a named sampling mode.

A contact sheet is a newly generated ordinary bitmap with explicit ordering, filters, label choice, padding, background and output dimensions. Candidate padding is 12 pixels, row gap 16 pixels, label font size 12 pixels at output scale; all are editable. Compute dimensions with checked arithmetic before allocation.

Transparent output keeps empty sheet areas transparent; labels may be opaque glyph pixels. Checker output bakes the checker only into the new sheet. Source images remain untouched. Empty selection gives a useful error, not a zero-sized file.

Acceptance: duplicate sizes labeled distinctly; odd dimensions aligned without resampling in native-size mode; title and labels remain legible; alpha and opaque export are visibly different; extreme output size rejected before allocation. Evidence: O07, A15–18.

## S13 — Toolbar strip

Reuse Atlas cells with explicit order, common cell size, row/column count, spacing and output alpha/matte. Source images can be smaller than cells; placement and resampling are separately chosen. Default preserves pixels and centers within transparent cells.

Do not infer strip order from ICO representation size order. Reordering cells changes sequence; editing a cell changes only that cell. Export plain PNG/BMP using the existing codec contract. Alpha-to-chroma-key conversion is explicit and checks whether opaque pixels use the reserved color.

Acceptance: reorder three distinct cells; blank trailing cells; nonzero spacing; a cell larger than the chosen extent; chroma-key collision; Save/reopen with matching grid. Evidence: A12; not exercised locally.

## S14 — Interchange and output validation

First-release compatibility profile: ICO with documented DIB/PNG payloads at verified dimensions, PNG, and existing supported Paint raster exports. SVG import creates pixels; do not imply vector editing. ICNS is a separate optional adapter with independently tested consumers.

Before destructive save, report representation loss, palette reduction and alpha flattening. Preserve source files on failed output. External-file changes require explicit reload/overwrite resolution. Proprietary vendor project, template, object-library and plug-in protocol outputs are excluded.

Acceptance: save/reopen supported representations; all-zero alpha with meaningful RGB; binary masks; PNG-compressed 256 image; malformed dimensions/counts; duplicate entries; Unicode paths; target consumer image selection. Validate Windows, macOS and Linux independently where shipped. Evidence: P01 proves import only; A01/A05 motivate interchange.

## S15 — Numerical and editing regression corpus

Use independently authored fixtures, not copied vendor art: alpha ramp; saturated transparent border; one-pixel diagonal; checker impulse; sharp circle; 16-pixel hand-tuned glyph; asymmetric arrow for transform orientation; duplicate size/depth entries; intentionally malformed containers; opaque image with magenta content.

Record fixture construction, expected invariant, codec version, kernel version and tolerances. Exact assertions cover Undo, no-op operations, alpha-only RGB preservation and integer transforms. Resampling/effect tolerances must follow an approved numerical contract. Visual review covers dark/light fringes, small-size legibility, checker contamination and clipping. A visually plausible screenshot does not replace data tests.
