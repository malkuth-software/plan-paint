# Icon production: custodial interview and behavioral portfolio

Status: **CANDIDATE design, with GIVEN scope and OBSERVED behavior.** Research date: 20 October 2026.

This portfolio proposes professional icon production inside Plan Paint's existing painting model. It preserves the classic Paint ribbon, path, stamp, pattern and reshape tools. It introduces no persistent layer stack. The requested scope includes batch production and asset browsing.

Start with the [scrolling HTML custodial interview](mockups.html). All 28 questions have named multiple-choice answers and comments. Twenty visual questions place the actual radio choice directly above its preview, with the proposed command location and consequences in the same card. Wide screens show the three alternatives side by side; narrow screens stack them in A/B/C order. A single radio group controls the answer and its highlighted card, without a duplicate toggle button. The **Enlarge preview** action opens an inspection window without selecting an answer. Close and Escape restore focus to its launch button; the enlarged view can also select the same underlying choice. Shadow geometry is illustrated inside the three selectable alternatives. Additional comparisons explain alpha inspection, state variants and proofing backgrounds. These specimens illustrate proposed behavior; they are not reference-application output.

Each visual question leads directly into its complete A/B/C preview cards. There is no duplicate link list between the question and its previews, so the artwork is visible sooner in a narrow browser panel. Palette editing, palette conversion, alpha tools, state generation and numerical transforms show artwork outcomes inside all fifteen corresponding choice cards. These diagrams explain the proposed workflow and its consequences; colors and geometry are illustrative, not measured codec or filter results. Recommendations and enlarging a preview do not select an answer.

The eight nonvisual questions use full-width multiple-choice rows followed by a comments field. Each of their 24 alternatives includes an “In practice” consequence that spells out what happens to artwork, files or release scope. Every question allows discussion or a combined direction, shows the selected answer, and links to the next question. A bordered comments area immediately follows each question's choices. Longer behavioral specifications are expandable below it; the questions, previews and answers remain visible. Section navigation and the question map are available in the introductory disclosure. The question map shows answered decisions, and **Continue** jumps to the next unanswered question. Recommendations are never preselected. Answers save in the current browser. **Save answer sheet** downloads readable HTML; **Export JSON** in the review section produces a structured decision record. Both include unanswered questions and comments. The page works as a local HTML file or through a local web server; browser storage is specific to the origin used to open it. Export answers before switching origins or clearing browser data.

Each question leads directly to its answer choices. Narrow layouts stack complete preview-and-answer cards; intermediate widths place a specimen beside its consequence. The ribbon alternatives show the same document under three command arrangements, and the proofing alternatives show their different use of canvas space. The asset-library comparison shows a folder browser and a managed-copy workflow; the batch comparison shows actual input/output rows, a collision and unavailable actions. Ribbon treatments include rest, hover, pressed, selected, focus and unavailable specimens. Six questions also expose candidate custodial contracts beside the comments: ribbon anatomy, stamp placement, proofing focus, asset-browser roles, batch lifecycle and theme-state distinctions. These details are proposals the owner can qualify, not recorded approvals. Existing answer IDs and browser storage remain compatible.

The [capability inventory](CAPABILITIES.md) maps the reference product's capabilities to proposed locations and dispositions. The [behavioral contracts](SPECIFICATIONS.md) define independently implementable behavior and acceptance cases. The [evidence and source register](EVIDENCE.md) separates direct application observations from documentation and historical reports. The [control conservation ledger](CONSERVATION.md) specifies the GUI.Forms ownership, styling, interaction and verification obligations. The [text question reference](INTERVIEW.md) supports the HTML interview; it is not the answer-entry interface.

The [implemented icon workflows](../ICON_EDITING.md) document the current Atlas commands, including state-family previews and separate ICO/CUR export. Implementation records do not turn candidate interview recommendations into owner decisions.

The proposed first release centers on:

1. An **Icon** contextual ribbon for ICO documents, using the existing Atlas representation model.
2. Explicit image identity: dimensions, color depth, transparency and storage encoding.
3. Generation and replacement of selected sizes, with protection for hand-tuned small images.
4. Alpha and palette inspection, reversible effect previews, and true-size comparison.
5. A folder-based asset browser in an owned popup.
6. A batch production window with an output manifest, collision handling and per-file results.
7. PNG export, contact sheets, toolbar strips and reusable flattened stamps.

These are recommendations to answer in the interview, not owner decisions. The HTML remains a design interview. Implemented size generation, comparison/contact sheets, representation export, asset browsing, alpha tools and palette conversion are documented separately in [icon and cursor production](../ICON_EDITING.md). The remaining proposals are not claims of implemented features.

## What was actually exercised

The installed IconWorkshop **6.97, 64-bit Professional Edition, 30-day trial** was launched and used through its normal interface. Plan Paint's own artwork was imported, converted into a ten-image ICO, saved, exported into four PNG size folders, and used to produce hot and disabled variants. Blur and shadow previews were inspected and cancelled. A contact sheet was generated. Batch normalization and compositing options were inspected without applying them.

The ICO was opened in Plan Paint. All ten images appeared in Atlas; its size-generation dialog and Gallery popup were inspected. This establishes a useful baseline, not complete codec equivalence or a round-trip guarantee.

Application binaries, private implementation details and proprietary file formats were not inspected. The specifications concern observable behavior and independently chosen contracts. The proposed UI is Plan Paint's own composition; the reference application's arrangement, branding and artwork are not design assets.

## Reading the status labels

| Label | Meaning |
|---|---|
| GIVEN | Explicit owner requirement: broad production, no layers, existing Paint identity, no proprietary replication. |
| OBSERVED | Seen in the installed application's interface or in an actual operation. Observation scope is stated. |
| MEASURED | A reproducible quantitative result. File counts and UI-reported dimensions qualify; visual similarity does not establish pixel equality. |
| DOCUMENTED | Public vendor documentation; recorded as evidence, not promoted to a locally verified result. |
| HISTORICAL REPORT | A dated user's report. Useful for test design, not proof of current defects or capabilities. |
| CANDIDATE | Proposed independent Plan Paint behavior awaiting the owner's choice. |
| REJECTED | Outside the explicit study scope, such as layers or proprietary output replication. |
| DECIDED | Reserved for an explicit accepted interview answer. Recommendations do not acquire this status automatically. |

## Coverage boundary

The inventory covers the major production, editing, browsing, interchange and integration families found in the installed UI and the linked public material. It does **not** claim to have read every website or forum, every historical manual page, or every possible state of the application. Exact proprietary filter kernels, nearest-format tie breaking, collision behavior for every command, malformed-file handling, large-job cancellation, clipboard interoperability and non-Windows platform behavior remain unmeasured. Those gaps are named alongside proposed tests instead of being filled with guesses.

The source register includes conflicting and dated claims. For example, the current application exposes more than 256-pixel ICO options, while the proposed interoperability profile is limited to 256 pixels until target consumers are tested independently.

## Implementation discipline

Use the pinned standalone GUI.Forms dependency and the public Application/Threading targets described in [the native build guide](../GUI_FORMS_PORT.md). First-party C++ remains C++20 with explicit types, named callbacks, dot member access, visible ownership, double-precision numerical kernels and no fast-math. Do not modify the shared toolkit to implement Paint-specific behavior.

Before releasing an implementation, run the consumer `check` target, CTest and `python3 scripts/check-style.py`, inspect native rendered output, and validate each released platform independently. The HTML mockups demonstrate composition and choices; they are not native rendering evidence.
