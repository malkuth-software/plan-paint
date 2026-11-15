#include "localization.hpp"
#include "forms/content_layout.hpp"
#include "forms/editor.hpp"
#include <gui_forms/text/text_store/text_store.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
// `offset` moved back to the grapheme boundary at or before it: Plan Paint's
// caret table has a stop per code point, the editor's selection one per
// grapheme.
gf::Utf8Offset grapheme_floor(const std::string& text, std::size_t offset) {
    const gf::TextStore store(text);
    const gf::Utf8Offset position(std::min(offset, text.size()));
    if (store.is_grapheme_boundary(position)) {
        return position;
    }
    return store.previous_grapheme_boundary(position);
}
} // namespace
// The canvas's own node: the picture's size and zoom. The text box being
// edited is the content editor's node (Editor::canvas_text).
gf::SemanticDescriptor PaintCanvas::semantic_descriptor() const {
    gf::SemanticDescriptor descriptor = RasterCanvas::semantic_descriptor();
    if (display_surface_) {
        const gf::LiveSurfaceSnapshot snapshot = (*display_surface_).snapshot();
        descriptor.value = std::to_string(snapshot.description.width) + " x " +
            std::to_string(snapshot.description.height) + " at " + std::to_string(zoom.read() * 100) + "%";
        descriptor.exposed = true;
    }
    return descriptor;
}
gf::ContentLayoutTransform Editor::canvas_text_view() const {
    // screen() is affine (zoom, view rotation and pan), so three points fix it.
    const gf::Point origin = screen({static_cast<double>(text.bounds.x), static_cast<double>(text.bounds.y)});
    const gf::Point unit_x = screen({text.bounds.x + 1.0, static_cast<double>(text.bounds.y)});
    const gf::Point unit_y = screen({static_cast<double>(text.bounds.x), text.bounds.y + 1.0});
    gf::ContentLayoutTransform view;
    view.xx = unit_x.x - origin.x;
    view.yx = unit_x.y - origin.y;
    view.xy = unit_y.x - origin.x;
    view.yy = unit_y.y - origin.y;
    view.dx = origin.x - canvas_text_region_.x;
    view.dy = origin.y - canvas_text_region_.y;
    return view;
}
void Editor::sync_canvas_text() {
    if (!text.active || !canvas_text_) {
        return;
    }
    std::string current = (*canvas_text_).text();
    if (current != text.edit.content) {
        text.edit.content = std::move(current);
    }
    text.refresh(document.ink.primary, document.ink.secondary);
    const gf::Rect region =
        (*canvas_).bitmap_to_client({text.bounds.x, text.bounds.y, text.bounds.w, text.bounds.h});
    if (!(region == canvas_text_region_)) {
        canvas_text_region_ = region;
        invalidate(gf::Dirty::layout);
    }
    gf::ContentTextLayout layout =
        content_layout(text, text.edit.content, (*canvas_text_).text_revision(), canvas_text_view());
    // A refused layout leaves the editor on its fallback (keys still edit;
    // Up and Down do nothing); the result is kept for the tests.
    canvas_text_layout_result_ = (*canvas_text_).submit_layout(std::move(layout));
}
void Editor::start_canvas_text() {
    (*canvas_text_).set_text({});
    (*canvas_text_).clear_undo_history();
    (*canvas_text_).accessible_name = text.note ? tr("Sticky note") : tr("Canvas text");
    (*canvas_text_).visible = true;
    sync_canvas_text();
    if (window()) {
        static_cast<void>((*window()).request_focus(canvas_text_));
    }
    reset_text_caret();
}
void Editor::canvas_text_layout_requested(std::uint64_t) {
    if (!text.active) {
        return;
    }
    // A text change repaints the preview (refresh syncs first); a change of
    // the region's size only needs the layout again.
    if ((*canvas_text_).text() != text.edit.content) {
        refresh();
    } else {
        sync_canvas_text();
    }
}
void Editor::canvas_text_selection_changed(const gf::TextSelection& selection) {
    if (!text.active) {
        return;
    }
    text.edit.anchor = selection.anchor.value();
    text.edit.caret = selection.caret.value();
    reset_text_caret();
}
void Editor::canvas_text_focus_changed(bool focused) {
    text_focus(focused);
}
void Editor::reset_text_caret() {
    text_caret_visible_ = true;
    text_caret_frame_.disconnect();
    if (text.active && window() && (*window()).focused_control() == canvas_text_) {
        const gf::FrameInterval blink = std::chrono::milliseconds(530);
        text_caret_frame_ = (*window()).schedule_ui_timer(*this, blink, gf::FrameClock::now() + blink,
                                                          FrameTick{this, &Editor::text_frame});
    }
    (*canvas_).invalidate(gf::Dirty::paint | gf::Dirty::semantics);
}
void Editor::text_focus(bool focused) {
    if (focused) {
        reset_text_caret();
    } else {
        text_caret_frame_.disconnect();
        text_caret_visible_ = false;
        (*canvas_).invalidate(text_caret_damage());
    }
}
gf::Rect Editor::text_caret_damage() const {
    if (!text.active || text.edit.caret >= text.layout.carets.size()) {
        return {};
    }
    const Point caret = text.layout.carets[text.edit.caret];

    const Point first = text.display_point(caret);
    double left = first.x, right = first.x, top = first.y, bottom = first.y;
    // Match every endpoint painted by the transformed caret, including bends.
    for (int row = 1; row <= text.layout.line_height; ++row) {
        const Point point = text.display_point({caret.x, caret.y + row});
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    const gf::Rect mapped =
        (*canvas_).bitmap_to_client({text.bounds.x + static_cast<int>(std::floor(left)),
                                     text.bounds.y + static_cast<int>(std::floor(top)),
                                     static_cast<int>(std::ceil(right) - std::floor(left)),
                                     static_cast<int>(std::ceil(bottom) - std::floor(top))});
    const gf::Rect bounds{mapped.x - 2, mapped.y - 2, mapped.width + 4, mapped.height + 4};
    const gf::Rect box =
        (*canvas_).bitmap_to_client({text.bounds.x, text.bounds.y, text.bounds.w, text.bounds.h});
    return gf::Rect::intersection(bounds, box);
}
void Editor::text_frame() {
    if (!text.active || !window() || (*window()).focused_control() != canvas_text_) {
        text_caret_frame_.disconnect();
        return;
    }
    text_caret_visible_ = !text_caret_visible_;
    (*canvas_).invalidate(text_caret_damage());
}
void Editor::insert_sticky_note() {
    finish_controls();
    choose_tool(Tool::Text);
    const Point origin = canvas().visible_paste_origin(sticky_note_settings.width, sticky_note_settings.height);
    text.begin(origin);
    text.note = sticky_note_settings;
    text.style = sticky_note_settings.font;
    text.resize({static_cast<int>(origin.x), static_cast<int>(origin.y),
                 sticky_note_settings.width, sticky_note_settings.height});
    start_canvas_text();
    (*ribbon_).show_tool_context();
    refresh();
}
void Editor::finish_text(bool place) {
    if (!text.active) {
        return;
    }
    text.edit.content = (*canvas_text_).text();
    if (text.note) {
        sticky_note_settings = *text.note;
        sticky_note_settings.width = text.bounds.w; sticky_note_settings.height = text.bounds.h;
        sticky_note_settings.font = text.style;
    }
    if (place && (!text.edit.content.empty() || text.note)) {
        text.refresh(document.ink.primary, document.ink.secondary);
        document.settle_selection();
        document.checkpoint();
        const Image base = document.image;
        composite(document.image, text.preview, text.bounds.x, text.bounds.y);
        document.constrain_selection(document.image, base);
    }
    text.clear();
    text_drag_ = -3;
    text_caret_frame_.disconnect();
    (*canvas_).set_pointer_capture(false);
    // The session is closed first, so the editor's notices of the cleared
    // text find no session and change nothing.
    if (window() && (*window()).focused_control() == canvas_text_) {
        static_cast<void>((*window()).request_focus(canvas_));
    }
    (*canvas_text_).visible = false;
    (*canvas_text_).set_text({});
    (*canvas_text_).clear_undo_history();
    canvas_text_region_ = {};
    refresh();
}
bool Editor::text_command(const std::string& command) {
    if (!text.active) {
        return false;
    }
    if (command == "text-place") {
        finish_text(true);
    } else if (command == "text-cancel" || command == "release") {
        finish_text(false);
    } else if (command == "text-fit") {
        text.refresh(document.ink.primary, document.ink.secondary);
        text.resize({text.bounds.x, text.bounds.y, text.bounds.w, text.layout.height + 2 * text.inset() + 4});
        refresh();
    } else if (command == "undo") {
        static_cast<void>((*canvas_text_).undo());
    } else if (command == "redo") {
        static_cast<void>((*canvas_text_).redo());
    } else if (command == "select-all") {
        (*canvas_text_).select_all();
    } else if (command == "delete") {
        // Delete with nothing selected removes the grapheme after the caret.
        const gf::TextSelection selection = (*canvas_text_).selection();
        if (selection.anchor == selection.caret) {
            const std::string current = (*canvas_text_).text();
            if (selection.caret.value() < current.size()) {
                const gf::TextStore store(current);
                (*canvas_text_).select(selection.caret, store.next_grapheme_boundary(selection.caret));
            }
        }
        static_cast<void>((*canvas_text_).delete_selection());
    } else if (command == "copy") {
        static_cast<void>((*canvas_text_).copy());
    } else if (command == "cut") {
        static_cast<void>((*canvas_text_).cut());
    } else if (command == "paste") {
        static_cast<void>((*canvas_text_).paste());
    } else {
        return false;
    }
    return true;
}
bool Editor::text_key(gf::KeyEvent& event) {
    const bool shortcut = gf::has_modifier(event.modifiers, gf::Modifier::control) ||
                          gf::has_modifier(event.modifiers, gf::Modifier::meta);
    const std::uint32_t key = event.physical_key;
    if (key == gf::PhysicalKey::escape) {
        finish_text(false);
    } else if (key == gf::PhysicalKey::enter && shortcut) {
        finish_text(true);
    } else {
        return false;
    }
    event.handled = true;
    return true;
}
void Editor::text_pointer(const gf::PointerEvent& event, Point point) {
    const double hx[] = {0, .5, 1, 1, 1, .5, 0, 0}, hy[] = {0, 0, 0, .5, 1, 1, 1, .5};
    if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary) {
        if (window()) {
            static_cast<void>((*window()).request_focus(canvas_text_));
        }
        text_drag_start_ = point;
        text_drag_bounds_ = text.bounds;
        text_drag_ = -3;
        double scale = (*canvas_).zoom.read();
        for (int i = 0; i < 8; ++i) {
            if (std::hypot(point.x - text.bounds.x - hx[i] * text.bounds.w,
                           point.y - text.bounds.y - hy[i] * text.bounds.h) *
                    scale <
                8) {
                text_drag_ = i;
                break;
            }
        }
        const gf::Point client = screen(point);
        const gf::Rect box =
            canvas().bitmap_to_client({text.bounds.x, text.bounds.y, text.bounds.w, text.bounds.h});
        if (text_drag_ == -3 && client.x >= box.x && client.x <= box.x + 92 && client.y >= box.y - 23 &&
            client.y < box.y) {
            text_drag_ = -1;
        }
        if (text_drag_ == -3 && point.x >= text.bounds.x && point.x < text.bounds.x + text.bounds.w &&
            point.y >= text.bounds.y && point.y < text.bounds.y + text.bounds.h) {
            text_drag_ = -2;
            // Plan Paint's layout places the caret exactly on skewed,
            // perspective and warped text; the editor takes the selection.
            const gf::Utf8Offset caret = grapheme_floor(
                text.edit.content,
                text.caret_at(text.source_point({point.x - text.bounds.x, point.y - text.bounds.y})));
            const gf::Utf8Offset anchor = shift_ ? (*canvas_text_).selection().anchor : caret;
            if (event.click_count >= 2) {
                (*canvas_text_).select_all();
            } else {
                (*canvas_text_).select(anchor, caret);
            }
        }
        if (text_drag_ == -3) {
            const bool note = text.note.has_value();
            finish_text(true);
            if (!note) { begin(point, false); }
            return;
        }
        (*canvas_).set_pointer_capture(true);
        reset_text_caret();
    } else if (event.action == gf::PointerAction::move && text_drag_ != -3) {
        int dx = static_cast<int>(std::round(point.x - text_drag_start_.x)),
            dy = static_cast<int>(std::round(point.y - text_drag_start_.y));
        Rect bounds = text_drag_bounds_;
        if (text_drag_ == -2) {
            const gf::Utf8Offset caret = grapheme_floor(
                text.edit.content,
                text.caret_at(text.source_point({point.x - text.bounds.x, point.y - text.bounds.y})));
            (*canvas_text_).select((*canvas_text_).selection().anchor, caret);
            return;
        }
        if (text_drag_ == -1) {
            bounds.x += dx;
            bounds.y += dy;
        } else {
            int i = text_drag_;
            if (i == 0 || i == 6 || i == 7) {
                bounds.x += dx;
                bounds.w -= dx;
            }
            if (i == 2 || i == 3 || i == 4) {
                bounds.w += dx;
            }
            if (i == 0 || i == 1 || i == 2) {
                bounds.y += dy;
                bounds.h -= dy;
            }
            if (i == 4 || i == 5 || i == 6) {
                bounds.h += dy;
            }
            const int maximum = text.note ? 4096 : 8192;
            bounds.w = std::clamp(bounds.w, 24, maximum);
            bounds.h = std::clamp(bounds.h, 24, std::min(maximum, 16000000 / bounds.w));
            if (i == 0 || i == 6 || i == 7) {
                bounds.x = text_drag_bounds_.x + text_drag_bounds_.w - bounds.w;
            }
            if (i == 0 || i == 1 || i == 2) {
                bounds.y = text_drag_bounds_.y + text_drag_bounds_.h - bounds.h;
            }
        }
        text.resize(bounds);
        refresh();
    } else if (event.action == gf::PointerAction::up) {
        text_drag_ = -3;
        (*canvas_).set_pointer_capture(false);
    }
}
void Editor::paint_text_overlay(gf::Painter& painter) {
    if (!text.active) {
        return;
    }
    double scale = (*canvas_).zoom.read();
    gf::Rect box = canvas().bitmap_to_client({text.bounds.x, text.bounds.y, text.bounds.w, text.bounds.h});
    gf::Color blue = gf::Color::rgba(29, 103, 180);
    painter.fill_rect({box.x, box.y - 22, 92, 20}, gf::Color::rgba(223, 236, 250));
    painter.draw_text_utf8({box.x + 7, box.y - 8}, tr("Move text"), {gf::FontRole::control, 12, 400, false},
                           blue);
    canvas().stroke_outline(painter, {text.bounds.x, text.bounds.y, text.bounds.w, text.bounds.h}, blue, 1);
    const double hx[] = {0, .5, 1, 1, 1, .5, 0, 0}, hy[] = {0, 0, 0, .5, 1, 1, 1, .5};
    for (int i = 0; i < 8; ++i) {
        const gf::Point handle =
            screen({text.bounds.x + hx[i] * text.bounds.w, text.bounds.y + hy[i] * text.bounds.h});
        double x = handle.x, y = handle.y;
        painter.fill_rect({x - 3, y - 3, 6, 6}, gf::Color::rgba(255, 255, 255));
        painter.stroke_rect({x - 3, y - 3, 6, 6}, blue, 1);
    }
    painter.save();
    painter.clip_rect(box);
    std::size_t first = std::min(text.edit.caret, text.edit.anchor),
                last = std::max(text.edit.caret, text.edit.anchor);
    for (const TextGlyph& glyph : text.layout.glyphs) {
        if (glyph.begin >= first && glyph.begin < last) {
            for (int row = 0; row < text.layout.line_height; ++row) {
                Point a =
                    text.display_point({static_cast<double>(glyph.x), static_cast<double>(glyph.y + row)});
                Point b = text.display_point({static_cast<double>(glyph.x + std::max(2, glyph.advance)),
                                              static_cast<double>(glyph.y + row)});
                painter.draw_line(screen({text.bounds.x + a.x, text.bounds.y + a.y}),
                                  screen({text.bounds.x + b.x, text.bounds.y + b.y}),
                                  gf::Color::rgba(60, 140, 240, 70), scale);
            }
        }
    }
    // The input method's marked text, underlined along its rows.
    const std::optional<gf::Utf8Range> composition = (*canvas_text_).composition_range();
    if (composition) {
        const std::size_t marked_first = (*composition).start.value();
        const std::size_t marked_last = (*composition).end.value();
        for (const TextGlyph& glyph : text.layout.glyphs) {
            if (glyph.begin >= marked_first && glyph.begin < marked_last) {
                const double row = static_cast<double>(glyph.y + text.layout.line_height - 1);
                Point a = text.display_point({static_cast<double>(glyph.x), row});
                Point b = text.display_point({static_cast<double>(glyph.x + std::max(2, glyph.advance)), row});
                painter.draw_line(screen({text.bounds.x + a.x, text.bounds.y + a.y}),
                                  screen({text.bounds.x + b.x, text.bounds.y + b.y}), blue, 1);
            }
        }
    }
    if (text_caret_visible_ && text.edit.caret < text.layout.carets.size() && window() &&
        (*window()).focused_control() == canvas_text_) {
        Point caret = text.layout.carets[text.edit.caret];
        for (int row = 0; row < text.layout.line_height; ++row) {
            Point a = text.display_point({caret.x, caret.y + row});
            Point b = text.display_point({caret.x, caret.y + row + 1});
            painter.draw_line(screen({text.bounds.x + a.x, text.bounds.y + a.y}),
                              screen({text.bounds.x + b.x, text.bounds.y + b.y}), blue, 1);
        }
    }
    painter.restore();
}
} // namespace paint::forms
