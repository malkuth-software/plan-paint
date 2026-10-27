# GUI.Forms conservation ledger

Status: CANDIDATE. The [custodial protocol](https://github.com/falseywinchnet/gui_forms/blob/75da57674365fb3fd5597cde14b2ada9378a82e1/planning/MICROFEATURE_CONSERVATION_PROTOCOL.md) governs the proposed surfaces. Existing Paint controls and toolkit contracts take precedence over names invented by this study.

The following part names describe **proposed authored anatomy**, not APIs claimed to exist today. Missing toolkit capabilities must be reported and addressed through the public dependency process; do not invent a Paint-specific rendering fork in the shared toolkit.

## The 24 questions applied

| Protocol question | Candidate answer |
|---|---|
| 1. Recognition | Icon is a document-specific ribbon page. A representation tile shows actual artwork plus dimensions, depth and encoding. Batch is an explicit source-to-output operation. |
| 2. Silhouette | Preserve Paint's joined tab/command-plane silhouette. Tiles are rectangular with a distinct selected plate; dialogs have a calm body and clear action row. |
| 3. Separate parts | Tab face/cap/label/seam; tile artwork/label/metadata/selection/focus; popup frame/title/body/footer; batch source list/output list/status/progress. |
| 4. Affordance | Labeled commands, disclosure indicators, visible hover/press states, clear disabled explanations. Metadata text must not masquerade as an action. |
| 5. Essential vs ornament | Identity, focus, selection, hierarchy and command grouping are essential. Flat/smooth/dithered relief is a theme choice. |
| 6. Parent authority | Document owns image collection; ribbon owns tab/group layout; popup owner owns focus/dismissal; job controller owns worker state. |
| 7. Membership/order | Stable image IDs and ordered collection; explicit current image plus selected set. UI sorting is not file reordering unless requested. |
| 8. Joins/overlap | Ribbon parent owns selected-tab seam; popup owns its overlap, shadow and clipping; tile artwork clips inside its image well. |
| 9. Draw/hit order | Popup receives input above owner; no click-through. Focus/selection overlays do not steal the artwork's semantic target. |
| 10. Boundaries | Parent owns group dividers and overflow; popup owns traversal/dismissal; no child double-draws shared seams. |
| 11. States | Rest, hover, press, capture, focus, current, selected, disabled, inactive, busy, error, high contrast and reduced motion are separate. |
| 12. Local vs parent | Hover is local. Current-image change updates document/canvas/status. Selection changes target scope. Tab selection changes the joined seam. |
| 13. Program handles | Typed document/image/job IDs; named commands for Generate, Preview, Apply, Cancel, Export, Select and Activate. No raw index identity across asynchronous work. |
| 14. Lifecycle | Opening records focus origin; close/cancel restores it if valid. Apply commits once. Document disposal revokes previews. Job ownership determines safe close behavior. |
| 15. Input equivalence | Keyboard, pointer and accessibility invoke the same named command. Drag/drop is an alternate import path, never the only path. |
| 16. Authored structure | Every functional part has a source element/role. Artwork is an asset; button faces remain procedural. |
| 17. Styling | Admitted style selectors/materials express chrome; no product-specific C++ paint branch for the icon feature. |
| 18. Theme substitution | Same anatomy in Pearl-derived, dark, Classic and high-contrast themes. Actual document pixels are never tinted by the theme. |
| 19. Compatibility names | After acceptance, part roles and semantic names are stable contracts. Display wording can localize without changing identity. |
| 20. Fail closed | Unsupported overlap/hit relationships or missing semantic roles fail source validation; do not silently flatten controls into pictures. |
| 21. Scaling/layout | Test narrow width, long labels, RTL, 100/150/200% scale and enlarged text. Group overflow preserves full command names and keyboard access. |
| 22. Failure/disposal | Reject stale revisions, cancel/retire workers, explain unavailable commands, preserve prior data on failed edits. Inactive selection remains visible. |
| 23. Minimum interaction | Candidate 28 logical-pixel minimum control height, enlarged as needed for text/host conventions; verify actual hit geometry. Do not shrink click targets merely to fit every group. |
| 24. Truthful fallback | Collapse to labeled menus, display unknown metadata, report unsupported outputs. Never fake a successful preview or silently omit representations. |

## Surface ledger

Each row inherits the cross-cutting rules above.

| Rule | Status/source | Structural / style owner | Runtime owner / program seam | Visual oracle | Interaction and inspection oracle | Native evidence / exclusions |
|---|---|---|---|---|---|---|
| U01 Icon page | CANDIDATE; P01, G01–02 | Ribbon/tab/group; theme tab seam and command materials | Ribbon region / icon-context command set | Selected page joins command plane; no extra identity band | Tab keyboard activation and pointer agree; contextual availability inspectable | Not implemented; no native specimen yet. |
| U02 Image tile | CANDIDATE; O03, P01 | Representation tile, artwork well, metadata, selection, focus | Collection / typed ImageId | Gold selection differs from dotted focus; dimensions/depth/encoding legible | Arrow navigation, Ctrl-selection, activation; current and selected exposed separately | P01 establishes current missing depth labels; proposed control untested. |
| U03 Gallery | CANDIDATE; P03 | Owned popup with tile collection and actions | Popup / image collection view | Inset content region; no persistent dock beside canvas | Escape/Close restore launch focus; no click-through; collection identity preserved | Existing Gallery observed; new labels and actions unimplemented. |
| U04 Effect preview | CANDIDATE; O04–05 | Parameter fields, scope label, Preview/Apply/Cancel | Preview transaction / named parameter and completion callbacks | Preview state clear; field validation local; source artwork untinted | Cancel leaves history unchanged; stale completion rejected; pending revision inspectable | One reference Cancel observed; Paint contract unimplemented. |
| U05 Asset browser | CANDIDATE; O08, G04 | Location/search/filter/grid/details/footer | Owned popup / asset selection and import commands | Artwork and metadata outrank decoration; failed items visibly distinct | Keyboard parity, focus return, generation-based thumbnail rejection | No proposed native browser tested; not a full file manager. |
| U06 Batch window | CANDIDATE; O09–12 | Sources, options, manifest, progress, results, action row | Job owner / preflight/run/cancel/report | Source and destination never ambiguous; progress accompanied by counts | No Run with unresolved collision; inspect per-file terminal state and job generation | Reference small jobs exercised; large/error/cancel behavior unmeasured. |
| U07 Comparison | CANDIDATE; O07, A15 | Preview grid, backdrop choices, labels | Owned popup / view-only projection | Actual-size and zoom labels accurate; no checker baked into source | Background switches do not dirty; selected image can be activated in document | Proposed native surface untested. |
| U08 Alpha mode | CANDIDATE; A10 | View mode indicator, canvas projection, alpha commands | Document view/edit boundary / mode-specific command | View-only and editable mode unmistakable; grayscale alpha is labeled | RGB unchanged under alpha-only edit; mode exposed semantically | Documentation evidence; no local alpha-paste experiment. |
| U09 Theme/state | CANDIDATE; G02 | All chrome parts; theme selectors | GUI.Forms retained state / no separate Paint state machine | Neutral #F4F4F5 stock, upper-left light, material-color icons, no idle motion | Focus differs from selection; host high-contrast wins; inactive state readable | HTML composition only; no claim of exact native parity. |

## Ownership and callback implementation plan

`IconDocument` owns representation data and revision history. `IconPreviewSession` owns immutable snapshots and temporary results. `IconBatchJob` owns a frozen plan and per-file outcomes. `AssetBrowserSession` owns navigation state, selection and a disposable thumbnail cache. These are conceptual names to reconcile with existing Paint classes before coding.

Bind commands to named callbacks with explicit context. Revoke subscriptions before owner disposal. Workers receive immutable plans and private scratch storage; they cannot retain borrowed mutable pixels across a document edit. UI completion carries owner identity, job generation and source revision. The UI rejects stale completions before publishing.

Separate computation from commit. Allocation, validation and conversion happen before a document replacement. Batch file publication follows S10 and does not share document Undo. Failure results say unchanged, partial job completion, or unsupported explicitly.

## Native verification before admission

Use the toolkit's source/profile, generated-tree, retained-authority, display-trace, raster, native dogfood and theme-substitution layers where applicable. For each proposed control capture normal, hover, pressed, keyboard-focus, selected, disabled, inactive and error states at the named scales. Test modal ownership, Alt/keyboard menu access, Escape, focus restoration, RTL labels and narrow overflow.

HTML screenshots are composition review evidence only. A good mockup does not establish GUI.Forms lowering, hit testing, accessibility or native rendering correctness. Consumer CTest/style checks and real native output remain release gates.
