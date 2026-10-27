#include "localization.hpp"
#include "forms/editor.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
namespace paint::forms {
namespace gf = gui_forms;
void PaintCanvas::on_text_input(gf::TextInputEvent& event) {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (editor) {
        (*editor).text_input(event);
    }
}
void PaintCanvas::on_focus_changed(bool focused) {
    RasterCanvas::on_focus_changed(focused);
    std::shared_ptr<Editor> editor = editor_.lock();
    if (editor) {
        (*editor).text_focus(focused);
    }
}
void PaintCanvas::on_frame(gf::FrameTime) {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (editor) {
        (*editor).text_frame();
        (*editor).selection_frame();
    }
}
gf::SemanticDescriptor PaintCanvas::semantic_descriptor() const {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !(*editor).text.active) {
        gf::SemanticDescriptor descriptor = RasterCanvas::semantic_descriptor();
        if (display_surface_) {
            const gf::LiveSurfaceSnapshot snapshot = (*display_surface_).snapshot();
            descriptor.value = std::to_string(snapshot.description.width) + " x " +
                std::to_string(snapshot.description.height) + " at " + std::to_string(zoom() * 100) + "%";
            descriptor.exposed = true;
        }
        return descriptor;
    }
    gf::SemanticDescriptor descriptor;
    descriptor.role = gf::SemanticRole::text_box;
    descriptor.name = (*editor).text.note ? tr("Sticky note") : tr("Canvas text");
    descriptor.value = (*editor).text.edit.content;
    descriptor.description = tr("Editable text box. Control or Command + Enter places it; Escape cancels.");
    descriptor.actions = {gf::SemanticAction::focus, gf::SemanticAction::set_value};
    descriptor.exposed = true;
    return descriptor;
}
bool PaintCanvas::on_semantic_action(gf::SemanticAction action, std::string_view value) {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (editor && (*editor).text.active && action == gf::SemanticAction::set_value &&
        gf::validate_utf8(value).valid()) {
        (*editor).text.edit.anchor = 0;
        (*editor).text.edit.caret = (*editor).text.edit.content.size();
        gf::TextInputEvent event{std::string(value)};
        (*editor).text_input(event);
        return event.handled;
    }
    return RasterCanvas::on_semantic_action(action, value);
}
void Editor::reset_text_caret() {
    text_caret_visible_ = true;
    text_caret_frame_.disconnect();
    if (text.active && window() && (*window()).focused_control() == canvas_) {
        text_caret_frame_ =
            (*window()).schedule_paint(canvas_, gf::FrameClock::now() + std::chrono::milliseconds(530));
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
    if (!text.active || !window() || (*window()).focused_control() != canvas_) {
        return;
    }
    text_caret_visible_ = !text_caret_visible_;
    (*canvas_).invalidate(text_caret_damage());
    text_caret_frame_ =
        (*window()).schedule_paint(canvas_, gf::FrameClock::now() + std::chrono::milliseconds(530));
}
void Editor::text_input(gf::TextInputEvent& event) {
    if (!text.active || !gf::validate_utf8(event.text_utf8).valid()) {
        return;
    }
    if (event.replacement_start >= 0 && event.replacement_length >= 0) {
        try {
            gf::TextStore store(text.edit.content);
            text.edit.anchor =
                store.utf8_offset(gf::Utf16Offset(static_cast<std::size_t>(event.replacement_start))).value();
            text.edit.caret = store
                                  .utf8_offset(gf::Utf16Offset(static_cast<std::size_t>(
                                      event.replacement_start + event.replacement_length)))
                                  .value();
        } catch (const std::out_of_range&) {
            return;
        }
    }
    event.handled = text.replace(event.text_utf8);
    reset_text_caret();
    refresh();
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
    if (window()) { static_cast<void>((*window()).request_focus(canvas_)); }
    reset_text_caret();
    (*ribbon_).show_tool_context();
    refresh();
}
void Editor::finish_text(bool place) {
    if (!text.active) {
        return;
    }
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
    } else if (command == "undo" || command == "redo") {
        text.history(command == "redo");
        reset_text_caret();
        refresh();
    } else if (command == "select-all") {
        text.edit.anchor = 0;
        text.edit.caret = text.edit.content.size();
        reset_text_caret();
    } else if (command == "delete") {
        if (text.edit.caret == text.edit.anchor) {
            text.edit.anchor = next_text_boundary(text.edit.content, text.edit.caret);
        }
        text.replace("");
        reset_text_caret();
        refresh();
    } else if (command == "copy" || command == "cut") {
        std::size_t first = std::min(text.edit.caret, text.edit.anchor),
                    last = std::max(text.edit.caret, text.edit.anchor);
        if (first < last &&
            services().write_clipboard_text(text.edit.content.substr(first, last - first)).accepted() &&
            command == "cut") {
            text.replace("");
            reset_text_caret();
            refresh();
        }
    } else if (command == "paste") {
        gf::HostClipboardTextResult result = services().read_clipboard_text();
        if (result.status.accepted() && result.has_text && gf::validate_utf8(result.text_utf8).valid()) {
            text.replace(result.text_utf8);
            reset_text_caret();
            refresh();
        }
    } else {
        return false;
    }
    return true;
}
bool Editor::text_key(gf::KeyEvent& event) {
    bool shortcut = gf::has_modifier(event.modifiers, gf::Modifier::control) ||
                    gf::has_modifier(event.modifiers, gf::Modifier::meta);
    bool shift = gf::has_modifier(event.modifiers, gf::Modifier::shift);
    const std::uint32_t key = event.physical_key;
    if (key == gf::PhysicalKey::escape) {
        finish_text(false);
    } else if (key == gf::PhysicalKey::enter && shortcut) {
        finish_text(true);
    } else if (shortcut &&
               (key == gf::PhysicalKey::a || key == gf::PhysicalKey::c || key == gf::PhysicalKey::v ||
                key == gf::PhysicalKey::x || key == gf::PhysicalKey::z || key == gf::PhysicalKey::y)) {
        text_command(key == gf::PhysicalKey::a            ? "select-all"
                     : key == gf::PhysicalKey::c          ? "copy"
                     : key == gf::PhysicalKey::v          ? "paste"
                     : key == gf::PhysicalKey::x          ? "cut"
                     : key == gf::PhysicalKey::y || shift ? "redo"
                                                          : "undo");
    } else if (key == gf::PhysicalKey::left || key == gf::PhysicalKey::right || key == gf::PhysicalKey::up ||
               key == gf::PhysicalKey::down || key == gf::PhysicalKey::home || key == gf::PhysicalKey::end) {
        if (key == gf::PhysicalKey::left) {
            text.edit.caret = !shift && text.edit.caret != text.edit.anchor
                                  ? std::min(text.edit.caret, text.edit.anchor)
                                  : previous_text_boundary(text.edit.content, text.edit.caret);
        }
        if (key == gf::PhysicalKey::right) {
            text.edit.caret = !shift && text.edit.caret != text.edit.anchor
                                  ? std::max(text.edit.caret, text.edit.anchor)
                                  : next_text_boundary(text.edit.content, text.edit.caret);
        }
        Point caret = text.layout.carets[text.edit.caret];
        if (key == gf::PhysicalKey::up || key == gf::PhysicalKey::down) {
            text.edit.caret = text.caret_at(
                {caret.x, caret.y + (key == gf::PhysicalKey::up ? -1 : 1) * text.layout.line_height});
        }
        if (key == gf::PhysicalKey::home || key == gf::PhysicalKey::end) {
            text.edit.caret = shortcut
                                  ? (key == gf::PhysicalKey::home ? 0 : text.edit.content.size())
                                  : text.caret_at({key == gf::PhysicalKey::home ? -1.0 : 100000.0, caret.y});
        }
        if (!shift) {
            text.edit.anchor = text.edit.caret;
        }
        reset_text_caret();
    } else if (key == gf::PhysicalKey::backspace || key == gf::PhysicalKey::delete_forward) {
        if (text.edit.anchor == text.edit.caret) {
            text.edit.anchor = key == gf::PhysicalKey::backspace
                                   ? previous_text_boundary(text.edit.content, text.edit.caret)
                                   : next_text_boundary(text.edit.content, text.edit.caret);
        }
        text.replace("");
        reset_text_caret();
        refresh();
    } else if (!shortcut && (key == gf::PhysicalKey::enter || key == gf::PhysicalKey::tab)) {
        text.replace(key == gf::PhysicalKey::enter ? "\n" : "\t");
        reset_text_caret();
        refresh();
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
            static_cast<void>((*window()).request_focus(canvas_));
        }
        text_drag_start_ = point;
        text_drag_bounds_ = text.bounds;
        text_drag_ = -3;
        double scale = (*canvas_).zoom();
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
            text.edit.caret =
                text.caret_at(text.source_point({point.x - text.bounds.x, point.y - text.bounds.y}));
            if (!shift_) {
                text.edit.anchor = text.edit.caret;
            }
            if (event.click_count >= 2) {
                text.edit.anchor = 0;
                text.edit.caret = text.edit.content.size();
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
            text.edit.caret =
                text.caret_at(text.source_point({point.x - text.bounds.x, point.y - text.bounds.y}));
            reset_text_caret();
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
    double scale = (*canvas_).zoom();
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
    if (text_caret_visible_ && !text.layout.carets.empty() && window() &&
        (*window()).focused_control() == canvas_) {
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
