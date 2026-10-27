# Evidence, provenance and coverage

Study date: 20 October 2026. Local application identity was read from Help > About: IconWorkshop 6.97 (64-bit), Professional Edition, 30-day trial. Experiments used Plan Paint's existing application artwork and disposable output. No installed program files were read for implementation information.

## Direct application experiments

| ID | Procedure | Observed result | Limits |
|---|---|---|---|
| O01 | Open New Windows Icon. | Name, first image size, custom dimensions/square, five color-depth alternatives, PNG compression and defaults controls. Sizes include 16 through 768, with non-power-of-two choices. | No claim that every offered combination is accepted by every consumer. |
| O02 | Open generation of several formats from the current image. | A size/depth matrix, extra sizes, a smooth-to-sharp resizing control, dithering and PNG compression options. | Kernel, slider mapping and compression threshold not measured. |
| O03 | Open Plan Paint's 1024-square PNG and create a Windows icon. Select the default nine 48/32/16 images across RGBA/256/16 colors, plus 256 RGBA. Save ICO. | Ten representations created. File browser reports ten formats and about 79.4 KB. | No binary comparison or palette-error measurement. |
| O04 | Open Gaussian Blur on the 256 RGBA image. | Radius field initially 8, slider markings 2/25/50, checked Preview; canvas and current thumbnail visibly change. Cancel restores sharp rendering and Undo remains disabled. | One cancellation case; exact bounds and kernel unspecified. |
| O05 | Open Drop Shadow. | Color, opacity, angle, distance, size and live Preview. Displayed defaults: black, 60%, −45°, 6 px, 5 px. Cancelled. | Values are observed dialog defaults in this session, not a universal preset. |
| O06 | Inspect Draw filters and colors menus. | Blur/sharpen/edge/emboss families; brightness/contrast, hue/saturation, gamma, levels, grayscale, inversion, opacity, shadow and hot/disabled commands. | Presence of commands, not numerical verification. |
| O07 | Generate icon snapshot with default depth filters. | Four RGBA and three 256-color images form a labeled contact sheet. New image reports 624 × 361, RGB24. Options include sort direction, depths, title, labels and checker/solid/transparent/custom backgrounds. | This result uses checker background; transparent export was not tested. |
| O08 | Browse the disposable folder in File Browser. | Thumbnail, format count, size, type badge, folder navigation and context-menu production commands. | Search performance and file management not exercised. |
| O09 | Inspect Batch Normalize. | Size/depth matrix; removal of unchosen formats; optional creation of missing formats; overwrite, new-name or new-folder destination. Cancelled. | No normalization run, overwrite or failure experiment. |
| O10 | Run Batch Create Images from Icons: RGBA, All Sizes, PNG, Preserve Transparency. | Ten PNGs across four folders: three each at 16/32/48 and one at 256. Same-size files receive probe.png, probe (2).png and probe (3).png names. The browser confirms all three 16-pixel outputs are 32-bit; the 256 output is also labeled 32-bit. | In this run “All Sizes” exported same-size alternatives despite the selected RGBA radio option. Exact mapping of numbered output to source depth, missing-format fallback and collisions with pre-existing files remain unmeasured. |
| O11 | Run Batch Make Hot/Disabled on the disposable ICO. | Creates probe_hot.ico and probe_disabled.ico; each browser item reports ten formats. Disabled RGBA thumbnails appear gray; indexed thumbnails remain colored. Dialog explicitly limits processing to RGB/RGBA and warns of unconditional collision overwrite. | Visual result, not an exact color or alpha formula. No existing output was overwritten. |
| O12 | Inspect Batch Merge Images on the exported PNG. | One fixed foreground/background image, preserve/resize dimensions, output type, alpha or matte, and three destination naming modes. Cancelled. | This is compositing, not strip assembly. Alignment/scaling details remain unmeasured. |
| P01 | Open generated ICO in Plan Paint using File > Open. | Ten Atlas frames appear; 256-square image renders. Same-size alternatives have ordinal/dimension labels without depth labels. | Import demonstrated; save/reopen preservation not tested. |
| P02 | Open Plan Paint Icon sizes. | 16, 24, 32, 48, 64, 96, 128, 256 choices; 16/32/48/256 checked in this session. Cancelled. | Generation not executed in Paint during this study. |
| P03 | Open Plan Paint Gallery from Atlas. | Owned “Atlas frames” popup presents the ten thumbnails and existing Atlas controls. Closed with no edit. | Native keyboard/accessibility coverage not complete. |

Machine-specific accessibility receipts and disposable outputs belong in ignored `astra/icon-capability-study/`. They are working evidence, not public dependencies of these specifications.

## Public source register

Links below were consulted for the indicated topic. Documentation describes the vendor product; it is not an implementation specification for Plan Paint.

| ID | Source | Use and qualification |
|---|---|---|
| G01 | [GUI.Forms custodial protocol](https://github.com/falseywinchnet/gui_forms/blob/75da57674365fb3fd5597cde14b2ada9378a82e1/planning/MICROFEATURE_CONSERVATION_PROTOCOL.md) | Anatomy, authority, state, theme and failure questions; conservation ledger and layered verification. |
| G02 | [GUI.Forms design language](https://github.com/falseywinchnet/gui_forms/blob/75da57674365fb3fd5597cde14b2ada9378a82e1/planning/DESIGN_LANGUAGE_SPECIFICATION.md) | Latest owner decisions take precedence over earlier proposals: ribbon identity, neutral stock, gold selection, separate focus, material icons, theme addressability. |
| G03 | [GUI.Forms programming style](https://github.com/falseywinchnet/gui_forms/blob/75da57674365fb3fd5597cde14b2ada9378a82e1/planning/PROGRAMMING_HOUSE_STYLE.md) | Explicit computation, ownership, named callbacks, numeric and failure contracts. |
| G04 | [Future application consumer profile](https://github.com/falseywinchnet/gui_forms/blob/75da57674365fb3fd5597cde14b2ada9378a82e1/planning/FUTURE_APPLICATION_CONSUMER_PROFILE.md) | Consumer composition and ADR-013 context; proposed Paint auxiliary tools use owned popups. |
| A01 | [Product features and changelog](https://axialis.com/iconworkshop/#features) | Broad capability map, SVG input, WebP update, plug-ins, platform exports. Marketing compatibility assertions need independent consumer tests. |
| A02 | [Batch processing](https://www.axialis.com/docs/iw/Batch_Processing_Icons.htm) | Entry points to conversion, normalization and compositing workflows. |
| A03 | [Batch icon creation lesson](https://www.axialis.com/docs/iw/Lesson11.htm) | Image-to-icon workflow, matrix, resizing and dithering. |
| A04 | [Normalize icons](https://www.axialis.com/docs/iw/Normalize_several_icons.htm) | Missing selected formats can be generated from the largest, most colorful source; unselected formats removed. |
| A05 | [Export icons to images](https://www.axialis.com/docs/iw/Convert_several_icons_to_images.htm) | Extraction, nearest available representation, alpha/matte, GIF optimization. The old text describes one size; O10 confirms an additional all-sizes option in 6.97. |
| A06 | [Batch merge](https://www.axialis.com/docs/iw/Batch_merge_images.htm) | Foreground/background stamping, naming and optional resize. |
| A07 | [Image objects](https://www.axialis.com/docs/iw/Creating_Icons_from_Image_Objects.htm) | Reusable artwork composition. Plan Paint adaptation uses flattened stamps. |
| A08 | [Using image objects](https://www.axialis.com/docs/iw/How_to_create_attractive_icons_using_image_objects.htm) | Placement workflow; no vendor objects or asset packs are adopted. |
| A09 | [Import image into icon](https://www.axialis.com/docs/iw/Import_an_image_into_an_icon.htm) | Floating selection, transformations and adjustments. |
| A10 | [Copy/paste alpha](https://www.axialis.com/docs/iw/Copy_Paste_alpha_channels.htm) | Grayscale alpha interchange. A final instruction appears to repeat “Copy” where the operation would require Paste; do not turn this typo into a contract. |
| A11 | [Editor options](https://www.axialis.com/docs/iw/Editor_options.htm) | Checker/grid configuration, paste options, image ordering and legacy interoperability. |
| A12 | [Image-strip lesson](https://www.axialis.com/docs/iw/Lesson16.htm) | Per-cell editing, adding/reordering/removing cells and alpha/chroma-key strips. |
| A13 | [Drop shadow](https://www.axialis.com/docs/iw/Add_a_drop_shadow.htm) | Scope, alpha requirement and parameters; supported by O05. |
| A14 | [Editing lesson](https://www.axialis.com/docs/iw/Lesson3.htm) | Selection adjustments and placement. |
| A15 | [Icon snapshot](https://www.axialis.com/docs/iw/Create_an%20Icon_Snapshop_Picture.htm) | Multi-size proof image, background and labeling; supported by O07. |
| A16 | [Library contact sheet](https://www.axialis.com/docs/iw/Create_a_snapshot_picture_of_icons_in_a_library.htm) | Collection proofing and thumbnail presentation. |
| A17 | [Capture an icon](https://www.axialis.com/docs/iw/Capture_an_icon.htm) | Screen capture loses underlying alpha; not exercised. |
| A18 | [Preview lesson](https://www.axialis.com/docs/iw/Lesson6.htm) | Light/dark preview and export. |
| A19 | [Icons in program files](https://www.axialis.com/docs/iw/How_to_modify_or_extract_icons_included_in_program_files.htm) | Resource extraction/editing exists as a separate capability family. Not performed in this study. |
| A20 | [Illustrator bridge](https://www.axialis.com/docs/iw/Install_the_Illustrator_plug-in.htm) | Direct transfer integration; not installed or tested. |
| A21 | [Photoshop templates](https://axialis.com/tutorials/tutorial-create-your-photoshop-templates.html) | Vendor template conventions; excluded from replication. |
| A22 | [Historical version 5 changes](https://www.axialis.com/docs/iw/What%27s_New_in_Version_5.0_.htm) | Historical feature discovery only; not a current compatibility promise. |
| A23 | [Product FAQ](https://www.axialis.com/products/faq.html) | Product distinctions; bitmap icon production must not be confused with vector authoring in another product. |
| A24 | [Macintosh archive handling](https://www.axialis.com/docs/iw/How_to_Use_Stuffit_Expander_to_Read_Macintosh_Icons.htm) | Legacy packaging context, not recommended first-release scope. |
| M01 | [Microsoft icon construction](https://learn.microsoft.com/en-us/windows/apps/design/iconography/app-icon-construction) | Independent Windows delivery guidance; use the current page when shipping presets. |
| M02 | [Microsoft classic icon guidance](https://learn.microsoft.com/en-us/windows/win32/uxguide/vis-icons) | Historical small-size retouching and multi-size context, not current requirements for every Windows app. |

## Forum evidence and resulting tests

Forum posts are primary evidence of the writer's experience or request. They are not authoritative codec specifications, current product documentation, or measured defects in 6.97.

| Source | Finding | Consequence for our study |
|---|---|---|
| [Neowin PNG-to-ICO discussion, page 5](https://www.neowin.net/forum/topic/77026-how-to-convert-png-to-ico/page/5/) | Historical discussion of multiple sizes, batch conversion and transparency/quality. Posts span different tools and versions. | Test every representation, small-size legibility and soft alpha. Do not attribute another tool's claims to IconWorkshop. |
| [XnView: multipage icon snapshot](https://newsgroup.xnview.com/viewtopic.php?f=34&t=18751) | Request to see multiple icon images together, referencing IconWorkshop. | Provide a comparison/contact-sheet workflow rather than only a single thumbnail. |
| [Add-in Express: ribbon-button icons](https://www.add-in-express.com/forum/read.php?FID=5&TID=7171) | Alpha images and transparent-color settings discussed in a host integration problem. | Distinguish alpha export from chroma-key/matte export and test the actual consumer. |
| [Access discussion](https://groups.google.com/g/comp.databases.ms-access/c/TBdpQoKPXic) | Historical image/toolbar integration discussion. | Keep legacy matte workflows explicit; never silently flatten transparency. |
| [Hardware Upgrade archive](https://www.hwupgrade.it/forum/archive/index.php/t-434473-p-20.html) | Old batch-export commentary predates current documented capabilities. | Do not treat an old missing-feature report as a present limitation. |

Searches also returned repack/download sites, unrelated products, and discussion actually about other editors. They were not used as capability evidence. Searches included IconWorkshop with alpha/transparency, batch, image strips, SVG, forums, and target ICO guidance. This is a traceable survey, not an assertion of exhaustive internet coverage.

## Outstanding observation queue

1. Repeat generation with alpha ramps, sharp diagonals, transparent colored borders and 16-pixel hand-tuned source art.
2. Compare indexed variants before/after state generation numerically using documented image decoders, if output analysis is desired.
3. Test export “nearest” behavior with missing sizes/depths, duplicate dimensions and both equidistant source sizes.
4. Test collisions separately for export, state variants, normalization and merge; current commands need not share a policy.
5. Exercise indexed palette editing, alpha paste, clipboard interchange, strip reorder and SVG rasterization.
6. Exercise batch cancellation and partial failure on a disposable multi-file set.
7. Independently test Windows icon consumers and each released Plan Paint host. No macOS/Linux verification is claimed.

These are explicitly open research items. Proposed behavior in SPECIFICATIONS.md is sufficient to discuss implementation choices; it does not pretend these unknowns were observed in the reference application.
