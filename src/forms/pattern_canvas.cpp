#include "localization.hpp"
#include "forms/pattern_canvas.hpp"
#include "forms/editor.hpp"
#include "safe_file.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
namespace paint::forms {
namespace gf = gui_forms;
PatternCanvas::PatternCanvas(gf::StableId id, std::weak_ptr<Editor> editor, std::shared_ptr<Image> tile)
    : Control(std::move(id)), editor_(std::move(editor)), tile_(std::move(tile)) {
    set_focusable(true);
    set_accessible_name(tr("Custom pattern canvas — black and white pencil"));
}
void PatternCanvas::initialize_control_tree() {
    const char* ids[] = {"pattern-return", "pattern-resize", "pattern-undo", "pattern-redo"};
    const std::string labels[] = {tr("Return to main canvas"), tr("Resize tile"), tr("Undo"), tr("Redo")};
    const gf::Rect positions[] = {
        {12, 12, 180, 30}, {470, 12, 110, 30}, {592, 12, 70, 30}, {670, 12, 70, 30}};
    for (int i = 0; i < 4; ++i) {
        const std::shared_ptr<gf::Button> button =
            gf::make_control<gf::Button>(gf::StableId(ids[i]), labels[i]);
        (*button).set_requested_bounds(positions[i]);
        subscriptions_.push_back((*button).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<PatternCanvas, &PatternCanvas::clicked>(*this)));
        add_child(button);
    }
    width_ = gf::make_control<gf::NumericUpDown>(gf::StableId("pattern-width"));
    height_ = gf::make_control<gf::NumericUpDown>(gf::StableId("pattern-height"));
    for (const std::shared_ptr<gf::NumericUpDown>& field : {width_, height_}) {
        (*field).set_range(1, 128);
        (*field).set_decimal_places(0);
        (*field).set_increment(1);
        (*field).set_value(8);
        add_child(field);
    }
    (*width_).set_value((*tile_).width);
    (*height_).set_value((*tile_).height);
    (*width_).set_requested_bounds({250, 12, 75, 30});
    (*height_).set_requested_bounds({383, 12, 75, 30});
    (*width_).set_accessible_name(tr("Pattern width in pixels"));
    (*height_).set_accessible_name(tr("Pattern height in pixels"));
    const std::string texts[] = {
        tr("Width"), tr("Height"),
        tr("Custom pattern · Pencil only · Left: black / Right: white · 1–128 pixels per side"),
        tr("Repeating preview")};
    const gf::Rect bounds[] = {{202, 15, 48, 24}, {335, 15, 48, 24}, {14, 53, 850, 28}, {14, 88, 200, 24}};
    for (int i = 0; i < 4; ++i) {
        const std::shared_ptr<gf::Label> label =
            gf::make_control<gf::Label>(gf::StableId("pattern-label-" + std::to_string(i)), texts[i]);
        (*label).set_requested_bounds(bounds[i]);
        add_child(label);
    }
}
void PatternCanvas::arrange(gf::Rect bounds) {
    Control::arrange(bounds);
    cell_ = std::max(1.0, std::floor(std::min({48.0, (bounds.width - 270) / (*tile_).width,
                                               (bounds.height - 155) / (*tile_).height})));
    origin_ = {250 + std::max(0.0, (bounds.width - 270 - cell_ * (*tile_).width) / 2),
               125 + std::max(0.0, (bounds.height - 145 - cell_ * (*tile_).height) / 2)};
}
gf::Rect PatternCanvas::tile_bounds() const {
    return {origin_.x, origin_.y, cell_ * (*tile_).width, cell_ * (*tile_).height};
}
void PatternCanvas::on_paint(gf::Painter& painter, gf::Rect) {
    painter.fill_rect(client_rectangle(), gf::Color::rgba(0, 128, 0));
    painter.fill_rect({0, 0, client_rectangle().width, 115}, gf::Color::rgba(232, 240, 232));
    const Image& tile = *tile_;
    for (int y = 0; y < tile.height; ++y) {
        for (int x = 0; x < tile.width; ++x) {
            const Color c = tile.get(x, y);
            painter.fill_rect({origin_.x + x * cell_, origin_.y + y * cell_, cell_, cell_},
                              gf::Color::rgba(c.r, c.r, c.r));
        }
    }
    if (window() && (*window()).focused_control().get() == this) {
        keyboard_cell_.x = std::clamp(keyboard_cell_.x, 0.0, static_cast<double>(tile.width - 1));
        keyboard_cell_.y = std::clamp(keyboard_cell_.y, 0.0, static_cast<double>(tile.height - 1));
        const double x = origin_.x + keyboard_cell_.x * cell_, y = origin_.y + keyboard_cell_.y * cell_;
        const gf::Color marker = gf::Color::rgba(255, 90, 20);
        painter.draw_line({x, y}, {x + cell_, y}, marker, 2);
        painter.draw_line({x + cell_, y}, {x + cell_, y + cell_}, marker, 2);
        painter.draw_line({x + cell_, y + cell_}, {x, y + cell_}, marker, 2);
        painter.draw_line({x, y + cell_}, {x, y}, marker, 2);
    }
    if (cell_ >= 6) {
        const gf::Color grid = gf::Color::rgba(140, 155, 140);
        for (int x = 0; x <= tile.width; ++x) {
            painter.draw_line({origin_.x + x * cell_, origin_.y},
                              {origin_.x + x * cell_, origin_.y + tile.height * cell_}, grid, 1);
        }
        for (int y = 0; y <= tile.height; ++y) {
            painter.draw_line({origin_.x, origin_.y + y * cell_},
                              {origin_.x + tile.width * cell_, origin_.y + y * cell_}, grid, 1);
        }
    }
    for (int y = 0; y < 192; ++y) {
        for (int x = 0; x < 192; ++x) {
            const Color c = tile.get(x % tile.width, y % tile.height);
            painter.fill_rect({16.0 + x, 130.0 + y, 1, 1}, gf::Color::rgba(c.r, c.r, c.r));
        }
    }
}
void PatternCanvas::checkpoint() {
    if (undo_.size() >= 64) {
        undo_.erase(undo_.begin());
    }
    undo_.push_back(*tile_);
    redo_.clear();
}
void PatternCanvas::changed() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (editor) {
        (*editor).store_custom_pattern();
    }
    (*width_).set_value((*tile_).width);
    (*height_).set_value((*tile_).height);
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void PatternCanvas::resize_tile(int width, int height) {
    width = std::clamp(width, 1, 128);
    height = std::clamp(height, 1, 128);
    if (width == (*tile_).width && height == (*tile_).height) {
        return;
    }
    Image replacement;
    replacement.reset(width, height);
    for (int y = 0; y < std::min(height, (*tile_).height); ++y) {
        for (int x = 0; x < std::min(width, (*tile_).width); ++x) {
            replacement.set(x, y, (*tile_).get(x, y));
        }
    }
    checkpoint();
    *tile_ = std::move(replacement);
    changed();
}
void PatternCanvas::undo(bool redo) {
    std::vector<Image>& source = redo ? redo_ : undo_;
    std::vector<Image>& destination = redo ? undo_ : redo_;
    if (source.empty()) {
        return;
    }
    destination.push_back(*tile_);
    *tile_ = std::move(source.back());
    source.pop_back();
    changed();
}
void PatternCanvas::clicked(gf::ButtonBase& button) {
    const std::string id(button.stable_id().value());
    if (id == "pattern-return") {
        const std::shared_ptr<Editor> editor = editor_.lock();
        if (editor) {
            (*editor).toggle_pattern_canvas();
        }
    } else if (id == "pattern-resize") {
        resize_tile(static_cast<int>((*width_).value()), static_cast<int>((*height_).value()));
    } else {
        undo(id == "pattern-redo");
    }
}
void PatternCanvas::pencil(Point point) {
    // Integer line traversal fills every crossed cell, independent of pointer event spacing.
    int x = static_cast<int>(last_.x), y = static_cast<int>(last_.y);
    const int end_x = static_cast<int>(point.x), end_y = static_cast<int>(point.y);
    const int dx = std::abs(end_x - x), dy = -std::abs(end_y - y);
    const int sx = x < end_x ? 1 : -1, sy = y < end_y ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        (*tile_).set(x, y, black_ ? Color{0, 0, 0, 255} : Color{255, 255, 255, 255});
        if (x == end_x && y == end_y) {
            break;
        }
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x += sx;
        }
        if (twice <= dx) {
            error += dx;
            y += sy;
        }
    }
    last_ = point;
    invalidate(gf::Dirty::paint);
}
void PatternCanvas::on_pointer(gf::PointerEvent& event) {
    const gf::Point p = point_from_window(event.position);
    const Point cell{
        std::clamp(std::floor((p.x - origin_.x) / cell_), -1.0, static_cast<double>((*tile_).width)),
        std::clamp(std::floor((p.y - origin_.y) / cell_), -1.0, static_cast<double>((*tile_).height))};
    if (event.action == gf::PointerAction::down && tile_bounds().contains(p) &&
        (event.button == gf::PointerButton::primary || event.button == gf::PointerButton::secondary)) {
        checkpoint();
        drawing_ = true;
        black_ = event.button == gf::PointerButton::primary;
        last_ = cell;
        set_pointer_capture(true);
        if (window()) {
            static_cast<void>((*window()).request_focus(shared_from_this()));
        }
        pencil(cell);
    } else if (drawing_ &&
               (event.action == gf::PointerAction::move || event.action == gf::PointerAction::up)) {
        pencil(cell);
        if (event.action == gf::PointerAction::up) {
            drawing_ = false;
            set_pointer_capture(false);
            const std::shared_ptr<Editor> editor = editor_.lock();
            if (editor) {
                (*editor).store_custom_pattern();
            }
        }
    }
    event.handled = true;
}
void PatternCanvas::on_key_preview(gf::KeyEvent& event) {
    if (event.action != gf::KeyAction::down) {
        return;
    }
    if (window() && (*window()).focused_control().get() == this && event.modifiers == gf::Modifier::none) {
        bool moved = true;
        if (event.physical_key == gf::PhysicalKey::left) { keyboard_cell_.x -= 1; }
        else if (event.physical_key == gf::PhysicalKey::right) { keyboard_cell_.x += 1; }
        else if (event.physical_key == gf::PhysicalKey::up) { keyboard_cell_.y -= 1; }
        else if (event.physical_key == gf::PhysicalKey::down) { keyboard_cell_.y += 1; }
        else { moved = false; }
        keyboard_cell_.x = std::clamp(keyboard_cell_.x, 0.0, static_cast<double>((*tile_).width - 1));
        keyboard_cell_.y = std::clamp(keyboard_cell_.y, 0.0, static_cast<double>((*tile_).height - 1));
        if (event.physical_key == gf::PhysicalKey::space || event.physical_key == gf::PhysicalKey::enter) {
            checkpoint(); black_ = event.physical_key == gf::PhysicalKey::space;
            last_ = keyboard_cell_; pencil(keyboard_cell_); changed(); event.handled = true;
        } else if (moved) { invalidate(gf::Dirty::paint); event.handled = true; }
        if (event.handled) { return; }
    }
    const bool command = gf::has_modifier(event.modifiers, gf::Modifier::control) ||
                         gf::has_modifier(event.modifiers, gf::Modifier::meta);
    if (command && (event.physical_key == gf::PhysicalKey::z || event.physical_key == gf::PhysicalKey::y)) {
        undo(event.physical_key == gf::PhysicalKey::y ||
             gf::has_modifier(event.modifiers, gf::Modifier::shift));
        event.handled = true;
    }
}
void Editor::load_custom_pattern() {
    pattern_storage_path_ = preference_directory() + "custom-pattern.bin";
    if (!std::filesystem::exists(pattern_storage_path_)) {
        return;
    }
    try {
        const std::vector<std::uint8_t> bytes =
            read_regular_file_bounded(pattern_storage_path_, 16390, tr("Cannot read custom pattern").c_str());
        if (bytes.size() < 6 || bytes[0] != 'R' || bytes[1] != 'P' || bytes[2] != 'T' || bytes[3] != 1) {
            return;
        }
        const int width = bytes[4], height = bytes[5];
        if (width < 1 || width > 128 || height < 1 || height > 128 ||
            bytes.size() != 6 + static_cast<std::size_t>(width * height)) {
            return;
        }
        Image tile;
        tile.reset(width, height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (bytes[6 + y * width + x] != 0) {
                    tile.set(x, y, {0, 0, 0, 255});
                }
            }
        }
        *custom_pattern = std::move(tile);
    } catch (const std::exception&) {
        // A malformed preference must not prevent the main document from opening.
    }
}
void Editor::store_custom_pattern() {
    ++custom_pattern_revision;
    if (pattern_storage_path_.empty()) {
        return;
    }
    const Image& tile = *custom_pattern;
    std::vector<std::uint8_t> bytes{
        'R', 'P', 'T', 1, static_cast<std::uint8_t>(tile.width), static_cast<std::uint8_t>(tile.height)};
    for (const Color pixel : tile.pixels) {
        bytes.push_back(pixel.r < 128 ? 1 : 0);
    }
    try {
        write_file_atomic(bytes, pattern_storage_path_, tr("Could not save custom pattern").c_str());
    } catch (const std::exception& error) {
        (*status_).set_text(error.what());
    }
}
void Editor::toggle_pattern_canvas() {
    if (!pattern_canvas_) {
        pattern_canvas_ = gf::make_control<PatternCanvas>(
            gf::StableId("custom-pattern-canvas"), std::static_pointer_cast<Editor>(shared_from_this()),
            custom_pattern);
        add_child(pattern_canvas_);
    }
    pattern_editing = !pattern_editing;
    (*pattern_canvas_).set_visible(pattern_editing);
    (*menu_).set_visible(!pattern_editing);
    (*ribbon_).set_visible(!pattern_editing);
    (*canvas_).set_visible(!pattern_editing);
    if (window()) {
        static_cast<void>(
            (*window()).request_focus(pattern_editing ? std::static_pointer_cast<gf::Control>(pattern_canvas_)
                                                      : std::static_pointer_cast<gf::Control>(canvas_)));
    }
    if (!pattern_editing) {
        for (std::size_t i = 0; i < spiro.pegs.size(); ++i) {
            spiro.refresh_peg(static_cast<int>(i));
        }
        refresh();
    }
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
} // namespace paint::forms
