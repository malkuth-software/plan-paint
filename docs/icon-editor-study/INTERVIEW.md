# Custodial interview — text reference

Use the [scrolling HTML interview](mockups.html) to compare previews, select answers, add comments, save progress and export the decision record. This document is the supporting text reference. “Recommended” records the study's recommendation only. All unanswered options remain CANDIDATE.

Already GIVEN: broad icon production including batches and asset browsing; no layers; preserve Plan Paint's classic ribbon and existing creative tools; no copied application appearance, binary inspection or proprietary output replication.

The HTML interview shows alternatives beside their answers, including the Icon ribbon, palette and alpha tools, preview surfaces, asset browsing and batch production. It starts without selected answers. Choices outside the given no-layers and proprietary-output boundaries are visible but unavailable; comments can request a separate scope discussion.

## Document and representation model

**Q01 — Where should icon commands live?**
Evidence: Plan Paint already opens ICO into Atlas (P01), and Gallery already uses a popup (P03).

- **A — Recommended:** contextual **Icon** tab for ICO; reuse Atlas data and Gallery machinery. Generic drawing stays on Home/Pencil.
- B — Extend Atlas with icon groups; no additional tab.
- C — Mostly menus, with a small Icon tab containing only image selection and preview.

Specific: the tab becomes available after successful ICO decode; opening a PNG does not silently convert it to ICO. File > New > Icon and File > Icon tools remain discoverable without an icon open. Choosing a painting tool must not lose the current icon image.

**Q02 — How should multiple sizes relate?** (C02, C06–07)

- **A — Recommended:** independent bitmap representations. Explicit Generate/Replace updates only chosen targets.
- B — A designated source plus linked generated outputs until the user detaches one.
- C — Always regenerate all smaller sizes when the largest changes.

A protects hand-tuned 16-pixel artwork and fits the no-layers model. B requires persistent provenance and stale-state rules; C risks discarding deliberate edits.

**Q03 — What does indexed-color editing mean?** (C10, C13–15)

- **A — Recommended first implementation:** edit RGBA pixels; show target palette/depth as export settings with a preview. Preserve imported originals until explicitly converted or edited.
- B — True palette/index editing, including changing a palette entry to recolor every pixel using it.
- C — RGBA-only authoring; legacy palette images are importable but cannot be emitted.

A must not imply byte-preserving save after edits. B needs index-aware tools and exact palette/mask round trips. Choose B if palette craft is a core goal rather than a compatibility feature.

**Q04 — How much palette control?**

- **A — Recommended:** adaptive per-image palette, named dithering choice, transparent-index policy and side-by-side preview.
- B — A plus a shared palette across a batch and palette file import/export.
- C — Automatic conversion only.

B improves consistency across toolbar families; it increases the need to show color loss before committing.

**Q05 — Which ICO delivery profiles?**

- **A — Recommended:** modern RGBA sizes 16/24/32/48/64/128/256; optional legacy 4/8-bit variants; custom dimensions within the verified profile.
- B — Minimal 16/32/48/256 RGBA preset, advanced sizes hidden behind More.
- C — Also experimental 512/768 images, visibly separated from tested compatibility.

The offered Axialis sizes are not themselves evidence that every Windows consumer accepts them. Profile names must say what was tested.

**Q06 — How should generation choose a source?**

- **A — Recommended:** explicit source per target; default to the selected source; show any upscaling.
- B — Automatically choose the largest/highest-depth image, with an override.
- C — Import a folder using dimension-bearing filenames and infer the mapping.

Allowing A and C together is possible: inferred mappings are reviewed before Generate. Duplicate dimensions/depths must never be silently collapsed.

**Q07 — What protects manually edited sizes?**

- **A — Recommended:** “Keep existing” default; replacements individually checked in a before/after list.
- B — A visible “hand-tuned” flag that blocks replacement unless cleared.
- C — Replace all chosen dimensions, relying on Undo.

Even A needs one Undo for the whole generation transaction, not one per image.

## Transparency, painting and finishing

**Q08 — Which alpha tools belong in the first release?** (C16–20)

- **A — Recommended:** alpha view, alpha-preserving paint, opacity, copy/paste alpha, threshold/matte export preview.
- B — A plus numeric alpha histogram and editable mask painting mode.
- C — Preserve existing transparency tools only.

Alpha view is a rendering mode of the current bitmap, not a persistent layer/channel document. Its keyboard mode and saved-pixel consequences must be unmistakable.

**Q09 — How should reusable objects work without layers?** (C25, C42–44)

- **A — Recommended:** existing stamp tool plus one temporary placement preview; Apply flattens into the current image, Escape cancels.
- B — Paste immediately at the chosen point, then use normal Undo.
- C — Keep several editable placed objects until Save.

C conflicts with the intended simple flattened model and is not recommended. If chosen, it needs a separate owner decision about retained objects rather than being introduced under another name.

**Q10 — Which effects should be promoted?** (C31–40)

- **A — Recommended:** brightness/contrast, hue/saturation, grayscale, opacity, blur, sharpen and shadow; remaining filters under Advanced.
- B — All observed color/filter families receive equal menu prominence.
- C — Only size conversion, alpha and palette work; effects stay outside the icon feature.

Effect controls show scope: selection, current image, or explicitly selected images. A checked live Preview must not create Undo entries.

**Q11 — How should shadows handle clipping?**

- **A — Recommended:** keep icon dimensions; preview clipped pixels and offer “Fit artwork within image” as a separate explicit operation.
- B — Automatically shrink artwork to make room for the shadow.
- C — Automatically enlarge the image.

For an ICO representation, C changes its identity and export compatibility. No option should silently move artwork differently at each size.

**Q12 — What should “disabled” and “hot” mean?** (C39)

- **A — Recommended:** named adjustable presets, previewed across all selected sizes/depths, exported to separate files.
- B — Fixed one-click defaults modeled on the visual intent of the reference.
- C — Only hand-authored state artwork; no generated states.

Observed reference behavior leaves indexed images visually colored in the disabled icon (O11). Proposed A should either process those consistently or report every skipped representation. Exact Axialis color formulas are unknown.

**Q13 — Should generic transforms gain precise controls?**

- **A — Recommended:** numeric offset, scale, angle and sampling, using existing Paint transform commands.
- B — Add only icon-specific alignment/centering and padding.
- C — Keep current transform UI unchanged.

Centering should distinguish canvas center from visible-alpha bounds; empty images need a defined result.

## Preview and proofing

**Q14 — Which comparison view?** (C22–24)

- **A — Recommended:** owned popup with actual-size rows on light, dark, checker and chosen color; optional enlarged pixel inspection.
- B — Persistent preview region beside the canvas.
- C — Contact-sheet export only.

A follows the proposed auxiliary-window composition and preserves canvas space. A preview at 100% must identify whether one source pixel maps to one physical display pixel.

**Q15 — How should contact sheets be delivered?**

- **A — Recommended:** create a new ordinary image with labels; optional direct PNG export.
- B — Export PNG plus HTML index of the source assets.
- C — Print-oriented page layout only.

Show whether the background is opaque or alpha. Labels and checker squares become pixels only in the generated sheet, never in the source icon.

**Q16 — Which validation feedback is useful?**

- **A — Recommended:** missing profile sizes, duplicate size/depth entries, upscaling, clipped effects, transparent/opaque output change and consumer compatibility notes.
- B — A plus small-size visual-quality heuristics.
- C — Structural validity only.

Heuristics must be advisory; do not mark an artist's deliberate pixel design “invalid.”

## Asset browsing and production

**Q17 — What is the asset library?** (C49–54)

- **A — Recommended:** ordinary folders, bookmarks and an optional open metadata index; owned browser popup.
- B — A managed collection that copies imported assets into a chosen root.
- C — A proprietary-style packaged library container.

C is outside the requested output scope. A permits source files to stay useful in other tools. Metadata must be rebuildable and missing files must remain distinguishable from empty folders.

**Q18 — How much file management belongs here?**

- **A — Recommended:** open, preview, copy path, reveal in file manager, select for jobs and import as stamp.
- B — A plus rename, copy/move and delete.
- C — Full Explorer replacement.

A keeps browsing focused on artwork. B requires collision, Undo/recycle and external-change contracts unrelated to image editing.

**Q19 — What should a batch window expose?** (C55–59)

- **A — Recommended:** source list, operation, profile, per-target manifest, warnings, destination, collision policy, progress and report.
- B — A short wizard with those details in expandable sections.
- C — One settings dialog that starts immediately on OK.

Mockup A shows an unresolved collision preventing Run. Preview must identify every generated file before writes begin.

**Q20 — Default destination and collisions?**

- **A — Recommended:** a new output folder; skip or rename collisions unless replace is explicitly selected.
- B — Beside the source, deterministic suffixes.
- C — Replace source files by default.

Atomicity is per output file, not a promise to roll back a whole batch. Cancellation leaves completed outputs and a report. Document Undo does not pretend to reverse filesystem jobs.

**Q21 — How should normalization behave?**

- **A — Recommended:** “Add missing” and “Exact profile” are separate modes; Exact profile lists removals before running.
- B — Always match the exact profile, removing everything unselected.
- C — Generate missing sizes only; never remove.

An empty target profile is an error. If creation of missing images is off, show which files cannot satisfy the profile.

**Q22 — Which batch operations are first-class?**

- **A — Recommended:** images to ICO, representation export, normalization, state variants and fixed-image stamping.
- B — A plus contact-sheet and strip assembly jobs.
- C — Start with conversion/export, defer all others.

This interview assumes broad production as GIVEN; A or B best matches that choice. Large batches need bounded memory and cancellation between outputs.

**Q23 — How should toolbar strips integrate?**

- **A — Recommended:** reuse Atlas cell editing; explicit ordering, cell size, spacing, alpha/matte and PNG/BMP export.
- B — Separate toolbar-strip document mode.
- C — Defer strips.

An icon's different resolutions are not successive strip cells by default. Users explicitly select the images that form the strip.

## Interchange, scope and appearance

**Q24 — Platform coverage?**

- **A — Recommended:** ICO and general PNG bundles first; optional ICNS later after native macOS validation.
- B — ICO and ICNS together in the first release.
- C — Include legacy Mac packaging and all mobile bundles immediately.

Mobile presets require current target rules and clear mask/background expectations, not a generic “works everywhere” badge.

**Q25 — SVG input?**

- **A — Recommended:** rasterize source independently at each requested size, then edit ordinary pixels.
- B — Rasterize once at a selected large size, then resample.
- C — Defer SVG-specific production UI.

A can improve small-size geometry but still needs optical retouching. No option introduces vector layers or a vector document editor.

**Q26 — External-app integration?**

- **A — Recommended:** ordinary files, external-change detection and explicit reload; clipboard tested where available.
- B — Add IDE integration after file workflows are stable.
- C — Reproduce Adobe template/plug-in workflows.

The study excludes proprietary bridge/template replication. Independently designed integration would be a separate project.

**Q27 — Resource extraction and application patching?**

- **A — Recommended:** defer both from this icon release.
- B — Later read-only extraction from explicitly selected user-owned resources.
- C — Executable resource replacement.

No reference-application binary was inspected. C introduces a separate artifact-modification workflow and is not part of the proposed portfolio.

**Q28 — Which ribbon rendering variant should the study show?**

- **A — Recommended:** preserve current Paint proportions and blue/gold identity; leave flat/smooth/dithered command rendering to the selected GUI.Forms theme.
- B — Choose smooth cloth-bulge commands as this application's explicit theme preference.
- C — Choose dithered bulge commands.

The toolkit's latest source leaves the default choice open. The HTML interview compares theme-selected, smooth and dithered treatments as composition studies; native GUI.Forms rendering remains to be validated.

## Custodial acceptance round

For every admitted surface, the [conservation ledger](CONSERVATION.md) answers the 24 protocol questions with proposed anatomy, ownership and behavior. Before implementation, the owner should specifically settle:

1. Whether **Icon** replaces the icon-related Atlas presentation or appears alongside it.
2. Whether indexed images are editable palettes or export targets.
3. Whether auxiliary tools are modal or modeless owned popups.
4. Whether selected images form a persistent multi-selection while the canvas has a separate current image.
5. Whether any proposed generic effects exceed the desired Paint scope.

No answer is inferred from opening the interview. No choices are preselected.
