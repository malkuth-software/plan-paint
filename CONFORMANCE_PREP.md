# Conformance preparation (claude/conformance-prep)

This branch gets Plan Paint ready for the GUI.Forms conformance rebase, so the rebase only moves pins. Nothing on it
has been compiled.

- **Base:** `claude/combined-build` 6909ad1 (the superset of PRs #48 and #49; never built at 6909ad1).
- **GUI.Forms target:** the integrator's `claude/batch-cp33` db7ce9306 (package version still 0.6.0). Items 1 to 7
  below were made against main 96d36969 (cp29); item 8 brings the branch to cp33. The batch was rebuilt since
  3b76c159a, which item 8 was first read against; the public C++ headers are the same at both.
- **Pins:** not moved. `third_party/gui-forms.lock.json` and `harness.lock` carry a PLACEHOLDER marker. The
  conformance rebase writes the final values.

## What was adapted

1. **Flat control headers** (`tools/control_header_moves.json` in GUI.Forms; the old spellings do not compile):
   - src: 18 lines in `src/forms/{atlas.hpp, dialog.hpp, editor.hpp, help.hpp, icon_sheet_dialog.cpp,
     pattern_canvas.hpp, ribbon.hpp}`;
   - tests: 3 lines in `tests/forms_native.cpp` and `tests/forms_tests.cpp`;
   - CMake: the `check_cxx_source_compiles` probe in `cmake/GUIFormsFrontend.cmake`.
   - Not changed: `experiments/lagging-tools/snapshot` and `third_party/gui-forms-patches/*.patch`, which no build
     reads.
2. **Package version:** `find_package(GUIForms 0.6 ...)` (was 0.2, which 0.6.0 refuses under SameMinorVersion).
   The probe now also checks the cp29 standard: it overrides `on_mouse_down(MouseEventArgs&)`, so an older SDK
   fails at configure with the pin message, not deep in the build.
3. **Theme include:** `control.hpp` no longer reaches `theme.hpp`. `src/forms/interface_theme.hpp` includes
   `<gui_forms/theme.hpp>`; its `.cpp` uses `Theme::create`, `definition()` and `theme_ptr()`.
   `src/cursors/tool_cursors.hpp` was checked: it uses only `CursorImagesPtr` and `CursorKind`, which `control.hpp`
   still reaches, so it needs nothing.
4. **Target-phase raisers** (GUI.Forms 56b6627ea: `Control::on_pointer` and `Control::on_key` are gone). Every
   override moved:
   - `PaintCanvas`, `ColorPlane`, `PatternCanvas` and the gradient `GradientStrip`:
     - the old body is now a private `handle_pointer(PointerEvent&)`;
     - `on_mouse_down`, `on_mouse_up`, `on_mouse_move` and `on_mouse_wheel` build the event with `pointer_event_of`,
       copy `handled` back, then call the base raiser.
     - `PaintCanvas::on_mouse_leave` feeds a leave event with the last window position, because the hover
       tracking now raises leave without a position.
   - `EditorDialog`: each mouse raiser calls the base, then sets `handled`, so presses never reach the editor
     behind the dialog.
   - `AtlasThumbnail` and `BusinessCard` (both Buttons):
     - secondary-button events are taken before `Button`'s raisers and end the route, as before;
     - other events go on to `Button::on_mouse_*`;
     - the card's name-band hover clears in `on_mouse_leave`;
     - `AtlasThumbnail::on_key` became `on_key_down` and `on_key_up`, which record the sequence modifier and then
       call `Button`'s.
   - The `_preview` overrides are unchanged; that phase still exists.
   - One behaviour change: the controls' MouseDown and MouseUp subscribers and rules now run, because each override
     calls its base raiser. Before, the window raised those separately.
5. **ControlStyles::opaque** (removed in cp29, audit A17: the core never read it). `PaintCanvas`'s constructor set
   it; the call is gone, so behaviour is unchanged. The 0.6 way to declare opacity is
   `Control::report_opaque_cover(painter, local)` from a paint callback after an opaque fill. The canvas paints
   over a transparent `canvas_background`, so it reports none.
6. **Removed and renamed API sweep** (static, after the opaque miss; src, tests, cmake, forms and data files):
   - `ControlStyles`/`set_style`: the one use above. The enum members left are none, resize_redraw,
     standard_click, selectable, standard_double_click, double_buffer and optimized_double_buffer.
   - `set_child_index` (now `set_child_index_from_top`), `layout_direction`/`layout-direction` (now
     `right_to_left`), `BorrowedSpan`, `c_api.h`, the controls moved to GUIForms::Lab (CorrespondenceView,
     ReviewCard, InstrumentRack, EasingPreview, TypographySpecimen): no uses.
   - `Application::run`: `src/forms/main.cpp` calls `application.run(std::move(window), std::move(options))` on
     the instance from `Application::create`, which matches 96d3696; the `ApplicationWindowOptions` fields it sets
     (title, initial_size, minimum_size, print_metrics_on_close, ready, closing) and the `ready`/`closing`
     signatures match.
   - Default theme and `minimum_size` default {0,0}: Plan Paint already sets `ThemeBase::gui_forms` and its own
     minimum sizes.
   - `&&`-deleted accessors: no call on a temporary found (`stable_id().value()` is on a const reference).
   - The matches in `experiments/lagging-tools/snapshot` and `third_party/gui-forms-patches/*.patch` are in
     sources no build reads.
7. **API drift:** two earlier checks found nothing else to change.
   - Identifier check: every identifier that exists in GUI.Forms' headers at the old pin (2d0f5200, cp22) but not at
     96d3696 was searched for in Plan Paint. Apart from `on_key`/`on_pointer`, the matches were Plan Paint's own
     names (`FrameTick`, `handle_`) or matches in vendored code. It missed `ControlStyles::opaque` because the word survives in header comments;
     the sweep in item 6 strips comments and checks enum members.
   - Export check: the `gf::` names Plan Paint uses were compared with `cmake/exports/gui_forms_application.exports`.
     Every name missing from the list is header-only: an enumerator, a nested aggregate or a template.
   - The cp28 calls that 6909ad1 needs are on main: `Control::scroll_content`,
     `RasterCanvas::declare_paint_follows_view` and `SurfaceRoot::set_scroll_copies_supported`.

8. **cp33** (`claude/batch-cp33` db7ce9306), in three commits:
   - **XML forms** (d13e863): `forms/{about,english_help,tool_size}.form` converted with GUI.Forms'
     `tools/convert_form_to_xml.py`.
   - **The unified business card** (d13e863):
     - Plan Paint's own `src/forms/business_card.*` is gone. `src/forms/identity.*` installs the program identity
       (`gui_forms::set_program_identity`: product, version, vendor, website, the About box's copyright line, the
       credits) right after `Application::create`.
     - The Help page shows GUI.Forms' page card. Its open request shows the hand card; the hand card's website
       request goes to Plan Paint's link opener (`set_website_opener`, a test seam).
     - The D-held secondary press on the vendor name now toggles GUI.Forms' diagnostic overlay; the editor no longer
       feeds key previews to a card. The Settings row reads `gui_forms::diagnostic_overlay()`.
     - The two retired strings ("Invalidation overlay: on until Plan Paint closes" and the card's accessible name)
       are replaced by "Diagnostic overlay: on" in all 22 language files. The translations are derived from each
       file's existing overlay term plus its word for "on"; `scripts/check-languages.py` passes (1016 entries).
   - **Font pack stage 4, STXlib's state, LC_CTYPE** (a44327a):
     - GUI.Forms' interface faces are compiled into its libraries; `GUIForms_FONT_DIR` holds only their licences.
       `packaging/fonts.txt` is gone. Bundles and packages carry those licences in `licenses/fonts`, and `fonts/`
       holds only the canvas text tool's own faces (`assets/fonts/cairo-unicode`, `assets/fonts/poster`).
     - The toolkit faces the text tool used to list (Carlito, Cousine, the Noto faces, Portsmouth Rapids) leave its
       font list. Its default faces are still the embedded Portsmouth ones.
     - The Linux launcher no longer exports `GUI_FORMS_FONT_DIR`, and `installed_fonts()` no longer reads it.
     - One module (GUI.Forms 66f82629b, folded): GUI.Forms, its threading library and STXlib's state link
       statically into each executable. The Windows build-tree copy of the GUI.Forms DLLs is gone; the package
       scripts follow the dependency closure, which now holds no first-party library.
     - D15: `main` sets `LC_CTYPE` from the environment on Linux (the host no longer sets the locale), with the
       `std-rules: allow(locale-change)` marker.
   - **Checked, nothing to change:**
     - final classes: no Plan Paint class derives from one of cp33's 363 final classes, and no override is of a
       function cp33 made final;
     - `WheelPhase` (appended last to the pointer and mouse arguments): Plan Paint builds no `MouseEventArgs`
       by aggregate, and its wheel handlers (canvas zoom and pan, the colour plane, the dialogs, the gradient
       strip) treat every phase alike;
     - the static libc++ Linux link (66556d959) comes through the toolchain file, `template_harness`'s
       `harness/cmake/llvm22.cmake`, which `scripts/native-build.sh` and `scripts/smoke-build.py` pass. Its
       Linux block is added on template_harness's prep branch. Plan Paint's Linux package keeps its musl closure;
       `scripts/package-linux.py` copies whatever the loader resolves, so a static C++ runtime simply leaves
       libc++ out of it.
   - **The canvas text tool on the content editing mode** (the plan below; the layout conversion was d448e08):
     the gaps came back (GUI.Forms `claude/content-editor-gaps` 3f996bff4, folded into cp33), and the tool now
     edits through a `gui_forms::ContentTextEditor`:
     - **The control** (`Editor::canvas_text()`, stable id `canvas-text`): a child of the Editor above the canvas
       and below the ribbon, multiline, `accepts_tab`, `draws_selection` false, transparent to hits, shown while
       a session is open. `start_canvas_text` empties it, clears its history, names it ("Canvas text" or "Sticky
       note") and focuses it; `finish_text` hides and empties it and gives the focus back to the canvas.
     - **The layout:** `sync_canvas_text` (from `refresh` and from every view change, `PaintCanvas::apply_view`)
       mirrors the editor's text into `TextSession`, places the editor over the box (`bitmap_to_client`, offset
       by the canvas in `Editor::arrange`) and submits `content_layout` for the current revision.
       `content_layout` now takes the view as a `ContentLayoutTransform`: where the display mapping is affine (a
       note, or skew without perspective or warp) the boxes stay in text-box pixels under the layout's transform
       (display mapping, then zoom, view rotation and pan), so the editor's hit testing, quads and published
       boxes follow skewed and rotated text exactly. Perspective and warp keep bounding boxes.
     - **Keys:** `text_key` keeps Escape and Control/Command+Enter ahead of the editor; everything else is the
       editor's (grapheme and line moves, Tab, undo and redo, the clipboard, input methods). The ribbon's Undo,
       Redo, Select all, Copy, Cut, Paste and Delete call the editor (`replace_selection`/`delete_selection`
       through `delete_selection`; Delete with nothing selected removes the next grapheme).
     - **Pointer:** the eight handles, the move bar and the click outside stay Plan Paint's; a press in the box
       places the caret by Plan Paint's own layout (exact on perspective and warp too), moved to a grapheme
       boundary, and sets the editor's selection.
     - **Drawing:** Plan Paint keeps drawing the selection and the blinking caret along its rows through
       `display_point` (exact for every mapping), from the editor's selection, mirrored on `selection_changed`;
       the input method's marked text (`composition_range`) is underlined the same way. The blink follows the
       editor's focus.
     - **Accessibility:** the editor's node (text, selection, caret, the layout query). The canvas's node no
       longer turns into a text box, and its `set_value` path is gone.
     - **Kept:** `TextSession::replace`, `history` and `TextEditState::caret`/`anchor` stay in paint_core, because
       the retired SDL frontend (`src/text_canvas.cpp`, `RAINSTAR_LEGACY_UI`) still uses them; the GUI.Forms
       frontend no longer calls `replace` or `history`, and keeps caret and anchor only as the mirror it draws
       from. They are kept for the retired frontend for now (coordinator, 2026-11-15) and are removable when that
       frontend goes.
   - **Curves through GUI.Drawing's flattening** (GUI.Forms `claude/gd-flatten-raster` 0d7983c25): in the GUI.Forms
     build `CurveGeometry::samples` builds a `GraphicsPath` (the cubic, or the circular arc as an arc of its
     circle's bounds) and flattens it with `flattened_size` and `flatten` into its own arrays, keeping the exact
     end points. A tolerance past `maximum_flattened_points` is doubled until it fits. paint_core links
     `GUIForms::Application` privately and compiles with `RAINSTAR_GUI_DRAWING=1` (`cmake/GUIFormsFrontend.cmake`);
     the frontend-free build keeps its own flattening. `execute_recorder` is not adopted (decision D1 below, option 1).

## Superimpose (claude/superimpose, stacked on 2a1182c)

The owner's new feature: an image laid over the work as a tracing aid, so the picture can be drawn underneath it.
Code only, not compiled.

- **The option:** Paste's drop-down gains a check row, Superimpose (`superimpose-paste`). It is a mode of Paste:
  a clipboard paste, a clipboard file and an image file dropped on the window all follow it.
- **Placing it:** with the option on, `Document::paste(..., superimpose = true)` makes a floating selection marked
  `superimpose`, moved and scaled like any paste. It records no undo step and never enlarges the canvas.
  Committing it (`Document::commit_selection`, so Escape or anything else that commits a paste) makes
  `Document::superimposed` of it instead of merging it. `FloatingSelection::composite_onto` never draws a
  superimposed paste into a picture.
- **Drawing:** the view draws it. `publish_image` for the canvas goes through `PaintCanvas::superimposed_source`,
  which blends the image (or the paste being placed) over the composed picture in a scratch image the canvas owns.
  That covers only the published damage when it is partial, so a brush stroke recomposes only its own pixels. It
  lies in document pixels, so it pans, zooms and tilts with the canvas. Tools draw on the picture underneath.
  `superimpose_onto` (`src/superimpose.*`, in paint_core) blends source over destination: the image's colour,
  adjusted by the brightness, at its alpha times (1 - transparency), over the work.
- **The tab:** a contextual tab set, `superimpose-tools` (tab `superimpose-tab`, "Superimpose"). It is available
  while an image is superimposed or being placed, and selected after a superimposed paste or drop
  (`Ribbon::show_superimpose`). It holds Transparency (0 to 95), Brightness (-100 to +100) and Remove. The
  numbers change only `Editor::superimpose_look`, then the view.
- **Removing it:** Escape when nothing else is in progress (`escape_reaches_superimposed`: no selection, text,
  stamp, warp, path or curve), and the tab's Remove. A right-click does not remove it: right-button drawing (Alt
  colour) works over it as usual (owner, 2026-11-15). With a
  selection active, the first Escape ends the selection and the second removes the image. A superimposed paste
  still being placed is a selection, so a paste needs two Escapes as well.
- **Strings:** "Superimpose" and "Brightness" are new in the 22 language packs. "Transparency" and "Remove"
  already existed.
- **GUI.Forms seams:** the ribbon's contextual tab sets and hosted numbers, a check row in a split button's
  drop-down (`MenuItemKind::check` with the row's Command), drag and drop, and the clipboard. No GUI.Forms gap
  found. Plan Paint composes all the pixels.
- **Defaults chosen, open owner questions:**
  1. one superimposed image at a time (a new superimposed paste replaces it);
  2. it is placed with the normal paste placement (move and scale) before it is committed. While placed it is
     already drawn see-through, at the tab's look;
  3. it is a view aid: not saved, not exported, not on the undo stack, and kept across picture edits, new pictures
     and opened files until removed;
  4. brightness from -100% to +100%, transparency from 0 to 95% (default 50%), so it never vanishes;
  5. the option and the look last for the session and are not saved in the settings;
  6. decided (owner, 2026-11-15): right-click draws; only Escape, Escape removes it (and the tab's Remove);
  7. the help pages do not describe it yet.
- **Tests:**
  - `paint-forms` `superimpose_paste_tab_drawing_and_removal`: the option and the tab, no undo step, the first
    Escape placing it, the composite at the default look, the pencil drawing under it and showing through, the
    controls changing the composite and not the picture, right-button drawing under it (the image stays), the
    two-Escape rule with and without a selection, and Paste with the option off;
  - `paint-core` `test_superimpose`: the paste and commit in Document, and the blend arithmetic (50%, the 95% cap,
    brightness -100%).
- **Risks (not checked without a build):**
  - whether a ribbon drop-down shows a check row's mark from its Command;
  - the pencil pixel the test reads (a one-pixel stroke from a drag along a row);
  - a damage-limited publish after the image moved without a full refresh would leave stale composite pixels
    outside the damage. Every placement change refreshes in full today;
  - an undo during placement restores the selection from its snapshot and drops the paste being placed.

## Tests that should cover it

- The full Plan Paint suite (68 tests at 33c065d), in particular:
  - `paint-forms` canvas, atlas, business-card, colour-plane, pattern and gradient-strip cases (all pointer paths
    now go through the raisers);
  - `canvas_pan_by_wheel_and_damage`;
  - `--pan-probe` for 6909ad1 (queue/claude__canvas-scroll-copy.md).
- Configure against a 0.2 or cp28 SDK must stop at the probe with the pin message.
- The canvas text on the editor and the curve flattening:
  - `paint-forms` `canvas_text_editing_and_commit` (focus on the editor, its node, typing, the layout current,
    the selection mirror, Backspace, the ribbon's Undo and Delete, Tab, place and cancel),
    `sticky_note_contracts`' inline note case (the editor focused and named, typing through the window),
    `text_caret_deadlines_damage_only_the_transformed_caret` and `canvas_text_content_layout` (the zoom transform,
    the skew transform against `display_point`, bounding boxes for warp);
  - `paint-core` curve cases in `core_tests.cpp` (the new tolerance and end-point check over three tolerances, the
    collinear reversal, the arc's circle and sagitta checks), in both the GUI.Forms and the frontend-free build;
  - `paint-forms-native` lettering and sticky reviews.
- cp33 (item 8):
  - `paint-forms` `business_card_and_hidden_overlay_switch` (the identity, the gesture on and off, the Settings
    row, the hand card, the website press) and `canvas_text_content_layout`;
  - the forms generation of the three XML forms (`gui_forms_add_form`);
  - `paint-forms-native` text cases with the poster faces from `fonts/`;
  - the packaging smoke runs on macOS, Windows and Linux: `licenses/fonts` present, no face taken from the SDK;
  - a Linux run with an input method (LC_CTYPE).

## What still waits

- **The final pins:**
  - the GUI.Forms commit, cache release and the four cache digests;
  - the template_harness main commit.
  - Both move once, at the conformance rebase (template_harness's CONFORMANCE_PREP.md).
- **D1, `execute_recorder` for Plan Paint's strokes: decided, option 1 (coordinator, 2026-11-15).** Plan Paint
  keeps its own ink (`polygon(image, samples, ink, ...)`: brush heads, materials, patterns, smoothing,
  supersampling, organic contours) for every stroke and uses GUI.Drawing for geometry only (the curve flattening).
  This follows the content split: the customer owns layout and pixels, GUI.Forms owns editing. The alternatives
  not taken: plain strokes through `execute_recorder` (two rasterizers for one tool), and brush and material
  hooks in the recorder raster.
- Settled by cp33 and the gaps (3f996bff4): no-frame paint draws nothing (question 1), measuring outside paint is
  not needed, Tab (`accepts_tab`), programmatic edits (`replace_selection`, `delete_selection`), the composition
  range, and transformed text (an affine layout transform, quads).
- **cp33's STXlib row-index switch** does not touch Plan Paint directly.
- **STXlib boundary types** (the swap-in, Sequence step 4): Plan Paint's `std::string`, `std::vector` and
  `std::span` crossings of the GUI.Forms API will move to the owned boundary types when they land. Nothing to do
  before their headers exist.
- **Font family:** the bundled body face selection (AGENTS.md "GIVEN forward direction"). Plan Paint's interface
  uses the GUI.Forms theme package. By the interface/content split its canvas text stays on its own engine.
- **Open GUI.Forms capabilities** (track file):
  - Apple Events odoc for Finder opens;
  - Linux dialog filter extension case.
- **Unverified at 6909ad1** (queue/claude__combined-build.md): items 4 and 9 still need a hand check.

## Canvas text tool on the content editing mode (plan; cp32)

### Finding

- The GUI.Forms frontend does not link Dear ImGui.
  - ImGui is built only for the retired SDL frontend: `RAINSTAR_LEGACY_UI`, OFF by default, with
    `src/text_canvas.cpp`, `vendor/imgui` and `paint-ui-tests`.
- The GUI.Forms frontend's text tool is Plan Paint's own port of that frontend's editing behaviour. It has two parts:
  - **`TextSession`** (`src/text_session.*`, in paint_core):
    - the text, caret and anchor (UTF-8 byte offsets);
    - its own undo and redo snapshots;
    - `replace` and `caret_at`;
    - the cached preview.
  - **`Editor::text_*`** (`src/forms/text.cpp`):
    - `text_key`: the editing keys;
    - `text_input`: committed text and UTF-16 replacement ranges;
    - `text_command`: select all, clipboard, undo and redo;
    - `text_pointer`;
    - the caret blink;
    - `paint_text_overlay`;
    - the text-box branch of `PaintCanvas::semantic_descriptor` and `on_semantic_action`.
- The layout is `layout_text` (`src/text.cpp`, stb_truetype). It returns a `TextLayout`: one glyph per code point with
  its byte range and position, one caret position per byte offset, the line height and the ascent.
- Moving to `gui_forms::ContentTextEditor` replaces the hand-written editing and keeps Plan Paint's layout and
  pixels. Retiring the legacy frontend and `vendor/imgui` is a separate owner choice.

### What moves to GUI.Forms, and what Plan Paint keeps

| Concern | Today (Plan Paint) | With ContentTextEditor |
|---|---|---|
| Text, caret, selection | `TextSession::edit` | The editor; `TextSession` mirrors `text()` on `text_changed` |
| Editing keys | `Editor::text_key` (arrows, Home/End, Backspace/Delete, Enter, Tab) | The editor's engine: grapheme and word moves, line moves by the submitted layout |
| Place and cancel | `text_key`: Escape, Control/Command+Enter | Stays Plan Paint's, in `Editor::on_key_preview`, before the editor |
| Clipboard, select all | `text_command` | `select_all`, `copy`, `cut`, `paste` (TextBoxBase); ribbon commands call them |
| Undo while typing | `TextSession` `undo_`/`redo_` | The editor's `undo`/`redo`/`can_undo`/`can_redo`; the document undo still records the placed text as one step |
| IME | `text_input` maps UTF-16 replacement ranges by hand | The editor handles composition; `text_input_caret` places the candidate window from the layout |
| Layout and glyph pixels | `layout_text`, `render_text`, `TextSession::refresh` | Unchanged; the layout is also converted and submitted (below) |
| Caret and selection drawing | `paint_text_overlay`, blink timer | Plan Paint keeps drawing them, through `display_point`, from `caret_rectangle()` and `selection_rectangles()`, with `set_draws_selection(false)`, because skew, perspective and warp bend them |
| Accessibility | Hand-built `text_box` node, `set_value` only | The editor's node: text, selection, caret, and the text layout query built from Plan Paint's clusters |
| Box handles, move bar, resize | `text_pointer` | Stays Plan Paint's: the handles lie outside the editor's region |

### Steps

1. **The control.**
   - One `ContentTextEditor` (stable id `canvas-text`), a child of `PaintCanvas`, visible while `text.active`, with
     `set_multiline(true)` and `set_draws_selection(false)`.
   - Its bounds are the text box in client coordinates (`bitmap_to_client(text.bounds)` less the inset). They are
     re-arranged on zoom, pan, view rotation, box resize and style change.
   - `begin` requests focus for it.
   - `finish_text(place)` reads `text()`, places or discards it, hides the editor and clears it with `set_text({})`.
2. **The layout protocol.** On `layout_requested(revision)`, and again whenever the view or style changes while the
   text revision holds:
   - `TextSession::refresh` lays out `text()` with stb_truetype at the session's style and wrap width;
   - the result is converted to a `ContentTextLayout` for `revision`, then passed to `submit_layout`;
   - a result other than `accepted` is logged with `content_layout_result_name`, and Plan Paint keeps working on
     the editor's fallback (keys still edit; Up and Down do nothing).
3. **The conversion** (a free function in paint_core, `content_layout(const TextLayout&, const std::string&,
   ViewMapping)`, with unit tests):
   - **Lines:** glyphs grouped by row. Each range runs from the row's first byte to its last, without the line break.
     Bounds use `line_height` and the row's extent. An empty text, or a trailing newline, gets an empty line at the
     caret's row.
   - **Clusters:** one per grapheme, not per code point.
     - Merge the glyphs of a grapheme (combining marks, emoji sequences) using `gui_forms::TextStore` grapheme
       ranges. Otherwise the layout is refused with `refused_boundary`.
     - Tab and space glyphs are clusters.
     - Every byte of a line must be covered contiguously.
     - `right_to_left` is false: `layout_text` has no bidirectional pass. That is a known limit of Plan Paint's
       engine, not something to add here.
   - **Caret stops:** from `TextLayout::carets`, at grapheme boundaries only, in ascending order.
   - **Coordinates:**
     - Plan Paint's layout is in text-box pixels. The editor's local coordinates are client points:
       `(inset + glyph position) * zoom` for an unrotated, untransformed view.
     - With skew, perspective, warp or view rotation, each box is the bounding box of the glyph's quad mapped through
       `display_point`. Hit testing and the accessibility boxes are then approximate (question 5).
4. **Keys.**
   - `Editor::on_key_preview` keeps Escape (cancel) and Control/Command+Enter (place) ahead of the editor.
   - `text_key`'s editing branches and `text_command`'s select, clipboard and undo branches are deleted.
   - The ribbon's Undo, Redo, Cut, Copy, Paste and Select all route to the editor while `text.active`.
5. **Pointer.**
   - Presses inside the region go to the editor, which places and drags the selection by the layout.
   - `text_pointer` keeps the eight handles, the move bar above the box, and a click outside, which places the text.
6. **Accessibility.**
   - The editor's `accessible_name` is "Canvas text" or "Sticky note". Its description keeps the place and cancel
     help.
   - The text branch of `PaintCanvas::semantic_descriptor` and the `set_value` path of `on_semantic_action` are
     deleted; the editor answers both.
7. **Delete:**
   - `TextSession::replace`, `history`, `undo_`, `redo_` and `TextEditState::caret`/`anchor`;
   - `Editor::text_input`'s replacement mapping;
   - `PaintCanvas::on_text_input`.
   - Sticky notes use the same editor (`TextSession::note`).
8. **Tests:**
   - The conversion: graphemes with combining marks, an emoji sequence, a tab, wrapped lines, an empty text, a
     trailing newline and a transformed view. Each must be accepted by `validate_content_text_layout`.
   - A stale revision must be refused, and resubmitting after a zoom must be accepted.
   - The existing paint-forms text cases (typing, IME commit with a replacement range, selection, clipboard, undo,
     place, cancel), driven through the window so the editor is the focused control.
   - The semantic node: text, selection and the layout query.

### Questions for GUI.Forms (to settle when cp32 is reviewed)

1. **Frames.** Plan Paint shows the text through the canvas's own composite (`TextSession::preview`), not through the
   editor. Can the editor have no frame channel, and paint nothing in its region (no opaque placeholder)? Or should
   Plan Paint publish the preview through `open_frames` and stop compositing it itself?
2. **Tab.** Plan Paint inserts a tab. Does ContentTextEditor take Tab as input (an `accepts_tab`, or
   `is_input_key`)?
3. **Composition range.** With `set_draws_selection(false)` the consumer draws everything. Is the open composition
   range public, so Plan Paint can underline marked text?
4. **Programmatic insert.** `TextBox::replace_selection` has no counterpart on TextBoxBase or ContentTextEditor.
   Plan Paint needs one for inserting text from its own commands.
5. **Transformed text.** The layout boxes are axis-aligned. Warped or rotated text needs either a per-cluster quad
   or transform, or acceptance that hit testing and accessibility boxes are bounding boxes there.

## GUI.Forms primitives Plan Paint could source

Plan Paint rasterizes into its own `Image` (`src/raster.cpp`, `curve.cpp`, `document_curve.cpp`, `document_path.cpp`,
`gradient.cpp`, `material.cpp`, `paint_tools.cpp`). GUI.Drawing (GUI.Forms' `include/gui_forms/drawing/`) offers:

- **Curve paths:** `GraphicsPath`:
  - lines, quadratic and cubic Béziers, arcs, ellipses, pies, rounded rectangles and polygons;
  - figures, alternate and winding fill;
  - `transform(Matrix)`, `bounds()`, `is_visible(point)`.
  - It could replace Plan Paint's hand-written shape outlines (`shape_points`, `stamp_outline`, `polygon`,
    `inside_polygon`) and the Bézier evaluation in the curve and path tools.
- **Strokes:** `Pen` (width, joins, caps, dashes), for `stroke`, `pixel_line` and the outline widths.
- **Brushes:**
  - `LinearGradientBrush`, `ConicGradientBrush` and `PathGradientBrush`, for Plan Paint's gradients (`gradient.cpp`);
  - `HatchBrush`, `TextureBrush` and `CheckerBrush`, for patterns and the transparency checker.
- **Transforms and areas:** `Matrix` (transform tool, view rotation); `Region` (selection masks).
- **Colour:** `ColorBlend`, `LinearSrgb` and `SrgbConversion`, for blending and dithering in linear light.
- **Already sourced:** PNG through GUI.Forms' image layer (stx_png); resampling through stx_conv.

The missing atomic: rasterizing a path, pen or brush into caller-owned pixels.
- Plan Paint needs a `Graphics` over a caller-owned `Bitmap` (or over a `BitmapEditView` of Plan Paint's buffer).
- Today GUI.Drawing paints controls through `Painter` and captures windows through `OffscreenRaster`. Main has no
  public "fill this path into my buffer" entry.
- This belongs to the gd_* inventory and the drawing ABI seal (conformance Sequence, step 3). It is the one request to
  record there for Plan Paint.

**Rechecked against cp33's sealed drawing ABI (1.0; curves 0.6, rotated text 0.7).** Curve paths are not adopted
yet, because two pieces are still missing:
- **The C++ seam has no rasterizer.** `gui_drawing::GraphicsPath` (C++) stores lines, quadratic and cubic Béziers,
  arcs and pies, but nothing in the public C++ headers executes a `GraphicsRecorder` into a `Bitmap`. That path
  exists only in the C ABI: `recorder_execute` into an owned `gd` bitmap, through the separate raster module
  (`gdr_initialize`), then `bitmap_lock` to copy out. `recorder_paint_target` draws into lent pixels, but only from
  a paint callback.
- **There is no flattening query.** `GraphicsPath::path_points` returns the stored points, not a flattened polyline
  at a tolerance. Plan Paint strokes its curves with its own ink (brush heads, smoothing, supersampling and
  patterns: `polygon(image, samples, ink, ...)`), which GUI.Drawing's `Pen` does not express. So the useful piece is
  the curve geometry: `GraphicsPath` plus a public `flatten(tolerance)` returning the polyline, which would replace
  `CurveGeometry::samples` and `flatten_bezier`.
- Requests for GUI.Forms (reported to the coordinator): a public path flattening at a stated tolerance, and a C++
  entry that rasterizes a recording into a caller-owned bitmap outside paint. Routing Plan Paint's C++ through the C
  ABI and a copy would be a workaround, so it is not done.
- Both requests are met since (GUI.Forms 0d7983c25, folded into cp33): the flattening is adopted for the curve
  tools, and the raster entry (`gui_drawing::execute_recorder`) is not adopted: decision D1 under "What still waits".

Stays Plan Paint's: flood fill, dithering, the warp mesh, the spirograph, icon codecs, and canvas text layout and
rendering (stb_truetype: content, not interface).

## How the integrator builds it

Against a local GUI.Forms SDK of the target commit, on the M4 (LLVM 22, the GUI.Forms build recipe):

```sh
# 1. The SDK, from a GUI.Forms checkout at 96d36969 (or the conformance release):
python3 -B tools/build_sdk.py --target macos-arm64 --prefix <sdk> --features all
# 2. Plan Paint against it (GUIFormsFrontend.cmake takes the find_package path):
cmake -S <plan-paint> -B <plan-paint>/build -G Ninja \
  -DCMAKE_PREFIX_PATH=<sdk> -DCMAKE_CXX_COMPILER=<llvm22>/bin/clang++
cmake --build <plan-paint>/build -j5
ctest --test-dir <plan-paint>/build --output-on-failure
```

- Or build GUI.Forms in-tree: `-DRAINSTAR_TOOLKIT_SOURCE_DIR=<gui_forms checkout>`.
- Do not run `scripts/fetch-gui-forms.py` or `scripts/restore-toolkit-cache.py` on this branch. They read the
  placeholder lock (cp22) and would fetch the old SDK.

## Risks not checked without a build

The canvas text on the editor and the curve flattening:
- Focus: `start_canvas_text` asks for the editor's focus from inside the canvas's mouse-down. If the window moves
  focus to the clicked control after the handler, the canvas would keep it; the tests check the focus.
- Event order: the code assumes the editor raises `layout_requested` synchronously with each text change (the
  mirror and preview follow it) and `selection_changed` after it; a selection mirrored before the text is only
  drawn once the next layout arrives (the caret index is bounds-checked).
- The editor's region may lie partly under the ribbon or outside the canvas; it paints nothing and takes no hits,
  so only its published boxes extend there.
- `GraphicsPath::flatten` must keep the collinear-reversal case (`core_tests`); the arc's start angle and sweep
  sign follow the y-down parameter angles the header states.
- paint_core now links the Application archive in the GUI.Forms build, so `paint-benchmark` and the core tests
  link it too (their link lines grow; nothing else changes).

cp33 (item 8):
- The derived translations of "Diagnostic overlay: on" need a native reader's check.
- The business card's layout on the Help page (its measure in `arrange`) has not been seen.
- `canvas_text_content_layout` assumes the embedded Portsmouth face lays out "Plan the next step..." in more than
  one row at 120 pixels; a much narrower face would make the wrap check vacuous, not wrong.

Earlier:

- Calls on temporaries to the 51 accessors that now delete their `&&` overload.
- Signature drift in calls whose names survived (the identifier check works on names only).
- A partially exported class (only a link proves exports).
- The behaviour change in point 4 (MouseDown/MouseUp subscribers now run).

## Owner findings on the 7c9c931 build

1. **Canvas pans (fixed, 3734f6c).** The rotation handle smeared along a pan: it keeps to the viewport's edge, so
   the scroll copy moved its old pixels and nothing damaged them (`Control::scroll_content` leaves stationary paint
   to the caller). `PaintCanvas::apply_view` now damages it where the copy moved it and where it is now; the
   editor's pans go through `set_view_origin`. A tilted canvas re-rendered its whole frame on every pan step; its
   paint now follows the view (the last frame drawn moved by the pan) and a whole-unit pan shifts the kept pixels
   and renders only the exposed bands.
2. **Selected Pencil (tool) tab drawn as a plain bordered box (open).** The look matches the stock selected
   menu-item recipe, as if no `ribbon::part(tab)` rule applied. Neither Plan Paint's theme package nor its tab spec
   gives the tool tab anything the Materials tab lacks (it differs only in `modes` and in being selected by
   `select_tab` from `show_tool_context`). Not found statically.
3. **Settings dialog: OK and Cancel do not close it (GUI.Forms macOS host defect).** It fails only after the canvas
   surround (background material) was changed. Routed to a GUI.Forms coder as a macOS host defect
   (`claude/cp32-macos-modal-wake`): posted work, or the idle-frame tile work a material change starts, makes no
   progress inside the modal session. OK, Cancel and Escape all close through `Editor::close_editor_dialog`, which
   posts `DestroyDialogWindow` (`begin_invoke`) while `show_modal`'s nested loop runs.
   Plan Paint's share, found statically: the combo applies nothing live; OK stores `settings.canvas_backing` and
   closes. The next canvas paint then runs `PaintCanvas::update_backing` inside `on_paint`, on the UI thread:
   `canvas_backing_texture` synchronously renders a carpet tile (`render_carpet_tile`, a procedural fibre render,
   cached per material) and the canvas loads or replaces its image resource from within the paint, against the
   rule `paint_rotated` states (publish replacements from input, layout or worker delivery, never from paint).
   That is long, blocking work in a paint inside the modal loop. It does not by itself explain a loop that never
   ends, but it is the work the material change starts; moving the tile render to a worker and the image
   replacement out of `on_paint` (to the settings change) is done in 3eac5ec: `request_backing` renders on a
   worker, `deliver_backing` loads the image outside paint, and paint shows the plain colour until then.

## Dogfood start fix (owner report on 3df9631)

The dogfood build exited at start with "Ribbon contextual tabs come together, in their sets' order": the
Superimpose tab stood between the gradient and icon tabs while its set was declared after icon-tools. The tab
now follows the icon tab, in the declared order (text, spirograph, gradient, icon, superimpose);
`ribbon_accepts_every_contextual_tab_set` builds the themed ribbon and checks every set is declared, has a
tab, comes after the core tabs and keeps the order. The 18 W201 warnings from `plan-paint.theme.css` are gone:
the edge, keyline, taper, edge-shadow, swatch and dialog-text colours are application roles (`color-roles` in
`:root`, values unchanged), and zero-width transparent borders are `border: none`;
`theme_package_compiles_without_warnings` requires a finding-free compile. Risk (unbuilt): an application role
read in `taper`, `edge-shadow` or `keylines`, or an alpha role colour, if the compiler refuses either there.

## Test framebuffers (M4 run of 27ec4cc)

paint-forms stopped at "a test framebuffer is available" (rotated_pan_shifts_the_frame_and_damages_the_handle;
canvas_pan_by_wheel_and_damage has the same check). Cause: `Window::create_framebuffer` asks the window's
framebuffer painter, which only a host (the native hosts, the surface manager) or an `OffscreenAttachment`
installs; the Fixture's window has neither, so it returned null. Neither test had been built before (3734f6c
and the wheel-pan copy case were written unbuilt); this is no API change in the one-module SDK. Fix: both
tests attach the window to one `gf::OffscreenRaster` (embedded font pack, made once per process,
`test_raster()`), the wheel-pan test from its start so all its measurements use one set of text metrics.
Risk: the rotated test attaches mid-way; the provider change relays the window out before the copied-pan
half, which paints and clears damage first.
