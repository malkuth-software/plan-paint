# Capability inventory and proposed placement

All Plan Paint dispositions below are **CANDIDATE** unless marked GIVEN/REJECTED. “Include” means recommended, not approved or implemented. Evidence IDs resolve in [EVIDENCE.md](EVIDENCE.md). An observed menu command establishes availability, not every parameter or algorithm.

The Icon tab appears for an ICO document. General painting remains on Home/Pencil/Materials; generic effects remain accessible from an Image menu. File-level production is also available without an ICO open through File > Icon tools. Atlas remains the general sprite/cursor mechanism; Icon is a specialization of its image collection, not a second independent document.

| ID | Capability and reference behavior | Evidence | Plan Paint disposition and location |
|---|---|---|---|
| C01 | Create named Windows icon with a first size/depth. | O01 | Include: File > New > Icon; same creation dialog as Icon > Add image. |
| C02 | Several sizes and depths in one ICO. | O03, P01 | Include: Icon > Images; expose depth and encoding alongside dimensions. |
| C03 | Add a blank representation. | O01, O06 | Include: Icon > Images > Add. |
| C04 | Duplicate a representation. | General collection need; not exercised | Include as independent proposal: Images > Duplicate; preserve identity until explicit replacement. |
| C05 | Remove a representation. | O06 | Include: Images > Remove; one document undo; prohibit accidental empty saved icon. |
| C06 | Generate several sizes/depths from one source. | O02–03 | Include: Images > Generate sizes; preview exact additions/replacements. |
| C07 | Generate from several supplied source images. | Draw menu O06 | Include: Images > Import sizes; explicit source-to-target mapping. |
| C08 | Custom dimensions and square constraint. | O01 | Include for imports; first-release ICO export profile 1–256 per axis pending consumer tests. |
| C09 | 512/768-pixel ICO options. | O01–02, A01 | Defer as experimental target profile; do not present as universal ICO compatibility. |
| C10 | RGBA, RGB24, 256-color, 16-color and monochrome representations. | O01–03 | Include RGBA and legacy indexed export; editing semantics require Q03. |
| C11 | PNG compression per representation. | O01–02, A01 | Include: Images > Properties / export profile. Compression is not color depth. |
| C12 | Resizing filter with smooth/sharp adjustment. | O02 | Include explicit existing resampler choices; avoid claiming Axialis kernel equivalence. |
| C13 | Dithering for indexed/monochrome output. | O02 | Include: Palette > Quantize; named method and preview. |
| C14 | Optimized palette generation. | A03 | Include: conversion options; shared-vs-per-image palette is Q04. |
| C15 | Palette load/save/edit. | O06, A11 | Include independently specified public palette interchange; menu disabled when inapplicable. |
| C16 | Preserve transparency while painting. | O06, P01 | Preserve existing Paint control; distinguish alpha lock from export alpha preservation. |
| C17 | Smooth drawing edges toggle. | O06 | Preserve Paint antialias semantics; keep on relevant drawing tool page. |
| C18 | Alpha copy/paste through grayscale image. | A10 | Include: Icon > Transparency > Copy/Paste alpha; no persistent channel panel. |
| C19 | Opacity adjustment. | O06 | Include: Transparency > Opacity; explicit current-image/selection scope. |
| C20 | Transparent color for legacy imagery. | A03, A12 | Include as export/import choice with color, threshold and preview. |
| C21 | Transparency checker and configurable grid. | A11, O06 | Include in View; backdrop never modifies document pixels. |
| C22 | Actual-size light/dark preview. | A18, visible preview O03 | Include: Icon > Preview > Actual size; owned popup. |
| C23 | Compare all icon images simultaneously. | O07, A15 | Include: Preview > Compare images; native size and labeled scaled inspection. |
| C24 | Snapshot/contact-sheet image generation. | O07, A15–16 | Include: Preview > Contact sheet; new document or explicit export. |
| C25 | Selection import and floating placement. | A09, A14 | Include through existing Paint selection and stamp semantics. |
| C26 | Move/scale/rotate imported artwork. | O06, A09 | Preserve existing transform tools; add precise numeric fields if missing. |
| C27 | Flip horizontal/vertical and orthogonal rotation. | O06 | Preserve existing commands; no separate icon-only duplicate. |
| C28 | Arbitrary rotation. | O06 | Include existing transform with sampling choice and preview. |
| C29 | Text rasterization. | O06 | Preserve Paint text; commit to current bitmap with normal Undo. |
| C30 | Pencil/brush/line/shape/gradient/fill tools. | Editor UI O03, O06 | Reuse Paint tools. No wholesale replacement with reference drawing UI. |
| C31 | Brightness/contrast. | O06, A14 | Include shared Image > Adjust; current bitmap/selection. |
| C32 | Hue/saturation. | O06 | Include shared Image > Adjust. |
| C33 | Gamma and levels. | O06 | Include shared Image > Adjust; defined color-space contract required. |
| C34 | Grayscale and inversion. | O06 | Include shared Image > Adjust; preserve alpha unless explicitly changed. |
| C35 | Blur and Gaussian blur. | O04, O06 | Include shared Image > Effects; reversible preview transaction. |
| C36 | Sharpen/details. | O06 | Include shared Image > Effects; clipping and alpha behavior specified. |
| C37 | Edge find/enhance/contour/emboss. | O06 | Optional advanced effects submenu; lower production priority. |
| C38 | Drop shadow. | O05, A13 | Include: Image > Effects, shortcut from Icon > Finish; flattened application. |
| C39 | Hot/disabled variants. | O11 | Include: Icon > Production > State variants; explicit preset and output names. |
| C40 | Last-effect repeat. | O06 | Include shared Repeat effect, with scope shown and parameters retained. |
| C41 | Layer list/PSD layer workflow. | O06, A05 | **REJECTED** persistent layers; flattened imports only if admitted separately. |
| C42 | Reusable “image objects.” | A07–09 | Include as owned/user-supplied PNG/SVG-derived stamps. No vendor packs copied. |
| C43 | Foreground/background placement of a floating object. | O06, A09 | Choice Q09: single pending selection or flattened stamp; no hidden retained stack. |
| C44 | Batch fixed-image foreground/background merge. | O12, A06 | Include: Production > Batch stamp; separate independent job inputs. |
| C45 | Per-size hand-tuned artwork via templates. | A21 | Include underlying need via Import sizes; **reject proprietary template/barcode conventions**. |
| C46 | Image-strip cell editing. | A12 | Include through Atlas > Sprite grid; shared frame operations. |
| C47 | Strip cell add/copy/reorder/remove. | A12 | Include in Atlas Gallery; export plain PNG/BMP. |
| C48 | Strip alpha or fixed background color. | A12 | Include explicit strip export profile; no silent transparency conversion. |
| C49 | File browser thumbnails and metadata. | O08, O10 | Include File > Browse assets and Icon > Assets; owned popup. |
| C50 | Favorites and reusable library organization. | UI O08, A01 | Include folder bookmarks and optional open metadata index; ordinary files remain authoritative. |
| C51 | Browse multiple icon images within an asset. | O08, O03 | Include image-count badge and expandable representation details. |
| C52 | File selection/filtering and search. | A02, A04 | Include scoped folder search; recursive option off until selected. |
| C53 | Copy/move/rename/delete asset files. | Browser menu O08 | Open/reveal/copy-path first; rename optional. Defer full file-manager parity. |
| C54 | Icon library containers. | O08, A16 | Use folders plus optional open manifest; proprietary library output is out of scope. |
| C55 | Batch images to ICO. | A03, O08 | Include Production > Batch create; one output icon per source input by default. |
| C56 | Batch normalization. | O09, A04 | Include Production > Normalize; distinguish add-missing from exact-set replacement. |
| C57 | Batch icon-to-raster export; All Sizes produced ten PNGs in four folders, numbering same-size alternatives. | O10, A05 | Include Production > Export images; independently distinguish all sizes from all representations and label output identity. |
| C58 | Missing-format fallback during extraction. | A05, O10 dialog | Include optional deterministic fallback with actual selected source in manifest; strict default. |
| C59 | Batch naming/destination modes. | O09–12 | Include new-folder default, templates, preflight collisions and no implicit overwrite. |
| C60 | ICO/ICNS conversion. | A01–02 | Optional later ICNS adapter; independently documented format and native consumer tests. |
| C61 | Legacy Mac RSC/BIN packaging. | A01, A24 | Defer; no first-release need established. |
| C62 | PNG/BMP/JPEG/GIF/TIFF/TGA and other raster interchange. | A01, A05 | Preserve supported Paint formats; new formats require explicit alpha/metadata tests. |
| C63 | WebP read/write and quality options. | A01 changelog | Candidate where Paint already supports codec; verify native export UI and alpha. |
| C64 | SVG input rasterization. | A01, A23 | Include explicit rasterize-at-target-sizes workflow; not an SVG object editor. |
| C65 | HDR/PFM/RAW/legacy formats. | A01, A22 | Defer expansion beyond existing Paint support; little icon-specific value. |
| C66 | GIF/JP2 optimized export preview. | A05 | Optional generic export enhancements; PNG/ICO correctness first. |
| C67 | Screen capture to icon. | A17 | Optional using existing acquisition; label result opaque unless alpha supplied separately. |
| C68 | Clipboard alpha interchange. | A09–10 | Include tests against real consumers; preserve existing platform clipboard contract. |
| C69 | Direct Adobe plug-ins. | A20–21 | Defer; ordinary PNG/SVG import and explicit reload cover the core need. |
| C70 | Visual Studio integration. | A01 | Defer dedicated add-in; external-file reload and ordinary Save first. |
| C71 | Executable resource extraction/replacement. | A19 | Exclude from this study's execution; recommend deferring executable patching entirely. |
| C72 | Mobile/platform export presets. | A01, M01 | Include named, versioned size bundles as a choice; validate each target's current rules. |
| C73 | Help/tutorial discovery. | A01, UI O06 | Include contextual explanations of image sizes, alpha, palette and encoding. |
| C74 | Drag/drop asset composition. | A07–09 | Include as shortcut to the same import/stamp command used by keyboard. |
| C75 | Multi-document workspace. | O03–12 | Preserve Paint's existing document/window policy; no borrowed MDI shell. |
| C76 | Theme/high-DPI UI. | A01, G02 | Follow GUI.Forms; inspect all new surfaces at scale and text-size combinations. |

## Proposed command map

| Surface | Groups/commands | Availability |
|---|---|---|
| File > New | Icon… | Always; opens creation dialog. |
| File > Icon tools | Browse assets…, Batch create…, Normalize…, Export images… | Available without a document; inputs selected explicitly. |
| Icon contextual tab | Images: Gallery, Add, Import, Generate, Remove, Properties | ICO document. Operations scoped to explicit image selection. |
| Icon contextual tab | Transparency: Alpha view, Copy/Paste alpha, Opacity, Export mask | ICO document; capability-specific disabled explanations. |
| Icon contextual tab | Preview: Actual size, Compare, Background, Contact sheet | ICO document; preview state is not a pixel edit. |
| Icon contextual tab | Production: Export images, State variants, Batch tools, Assets | Current document can seed a job without forcing Save or overwriting it. |
| Image menu | Adjust, Effects, Transform, Repeat | General bitmap and current representation. |
| Atlas | Sprite grid, strip assembly/reordering, cursor metadata | Preserve general workflows; no duplicate icon document storage. |
| View | Grid, checker, zoom, theme | General view state; never baked into saved pixels. |

Narrow windows collapse groups into labeled menus in this order: Production, Transparency, Preview. Keep the current image selector and Images group accessible. This order is a candidate layout rule to validate, not a measured breakpoint specification.
