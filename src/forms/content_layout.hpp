#pragma once
// Plan Paint's canvas text layout as GUI.Forms' content layout (the content
// editing mode, gui_forms::ContentTextEditor). Plan Paint keeps laying out
// and rendering its canvas text with its own engine (layout_text, stb_truetype:
// content, not interface); this conversion hands GUI.Forms the positions of
// its lines, graphemes and carets, so the editor's caret moves, pointer
// placement and accessibility publication follow Plan Paint's pixels.
#include "text_session.hpp"
#include <gui_forms/content/editing/content_text_layout.hpp>
#include <cstdint>
#include <string>
namespace paint::forms {
// The layout of `text` held by `session` (TextSession::layout, in text-box
// pixels, laid out by TextSession::refresh from that same text) for revision
// `revision` of the editor's text, in the local coordinates of an editor whose
// bounds are the session's box at `zoom` client points per bitmap pixel.
//
// - Lines: one per row of the layout. A line break ends a line and is in
//   none; a wrapped row starts a new line at its first grapheme. An empty
//   text, and the row after a final line break, are empty lines.
// - Clusters: one per grapheme (gui_forms::TextStore's boundaries), the
//   union of its code points' glyph boxes; a tab or a space is a cluster.
//   Plan Paint's engine has no bidirectional pass, so every cluster runs
//   left to right.
// - Caret stops: one per grapheme boundary, from TextLayout::carets.
// - Boxes: each text-box rectangle is mapped through
//   TextSession::display_point (the note inset, or skew, perspective and
//   warp) and scaled by `zoom`; a transformed rectangle becomes the bounding
//   box of its mapped corners, so hit testing and the published boxes are
//   approximate on transformed text.
//
// A layout that does not belong to `text` (its caret table is not one entry
// per byte plus one) yields a layout with no lines, which the editor refuses
// and works on without.
[[nodiscard]] gui_forms::ContentTextLayout content_layout(const TextSession& session, const std::string& text,
                                                           std::uint64_t revision, double zoom);
} // namespace paint::forms
