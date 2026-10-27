#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "icon_palette.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace paint::forms {
namespace gf = gui_forms;
bool Editor::icon_palette_available() {
    if ((document.atlas.kind != AtlasKind::Icon && document.atlas.kind != AtlasKind::Cursor) ||
        document.atlas.active < 0) {
        return false;
    }
    document.sync_atlas();
    return has_icon_palette(document.atlas.icons[document.atlas.active]);
}
void Editor::choose_icon_index(unsigned entry, bool secondary) {
    if (!icon_palette_available() ||
        entry >= (*document.atlas.icons[document.atlas.active].source).palette.size()) {
        throw std::runtime_error("Choose an entry from the current indexed image's palette.");
    }
    const Color color = (*document.atlas.icons[document.atlas.active].source).palette[entry];
    if (secondary) {
        icon_alt_index = entry;
        document.ink.secondary = color;
    } else {
        icon_primary_index = entry;
        document.ink.primary = color;
    }
    Ink& material = secondary ? document.alt_ink : document.ink;
    if (material.pattern == Pattern::None) {
        select_pattern(material, Pattern::Solid);
    }
    icon_index_paint = true;
}
bool Editor::begin_indexed_paint(Point point, bool secondary) {
    if (!icon_index_paint || (document.tool != Tool::Pencil && document.tool != Tool::Fill &&
                              document.tool != Tool::Eraser && document.tool != Tool::Picker)) {
        return false;
    }
    if (document.selection.active && !document.selection.canvas_selection && document.tool != Tool::Picker) {
        throw std::runtime_error("Place the floating selection before using Index paint.");
    }
    if (document.tool != Tool::Picker) {
        document.settle_selection();
    }
    if (!icon_palette_available()) {
        throw std::runtime_error(
            "Index paint needs an indexed image. Convert its palette or turn Index paint off.");
    }
    const IconFrame& frame = document.atlas.icons[document.atlas.active];
    const IconSource& source = *frame.source;
    if (document.tool == Tool::Picker) {
        const std::size_t pixel = static_cast<std::size_t>(std::floor(point.y)) * frame.image.width +
                                  static_cast<std::size_t>(std::floor(point.x));
        choose_icon_index(source.indices.at(pixel), secondary);
        refresh();
        return true;
    }
    if (document.selection.active &&
        (!document.selection.canvas_selection || !document.selection.on_canvas)) {
        throw std::runtime_error("Place the floating selection before using Index paint.");
    }
    indexed_entry_ = secondary ? icon_alt_index : icon_primary_index;
    if (indexed_entry_ >= source.palette.size()) {
        throw std::runtime_error("Choose an entry from the current indexed image's palette.");
    }
    if (document.tool == Tool::Eraser) {
        std::size_t black = 0;
        while (black < source.palette.size() && !equal(source.palette[black], {0, 0, 0, 255})) {
            ++black;
        }
        if (black == source.palette.size()) {
            throw std::runtime_error(
                "Indexed erasing needs a black palette entry for the transparency mask.");
        }
        indexed_entry_ = static_cast<unsigned>(black);
    }
    paint_base_ = document.image;
    indexed_allowed_.assign(document.image.pixels.size(), 1);
    const FloatingSelection& selection = document.selection;
    for (int y = 0; y < document.image.height; ++y) {
        for (int x = 0; x < document.image.width; ++x) {
            const std::size_t pixel = static_cast<std::size_t>(y) * document.image.width + x;
            bool allowed = guide.blocked(x, y) < 0.5;
            if (selection.active) {
                const int sx = x - selection.x, sy = y - selection.y;
                if (!selection.image.contains(sx, sy)) {
                    allowed = false;
                } else {
                    const std::size_t selected = static_cast<std::size_t>(sy) * selection.image.width + sx;
                    const unsigned coverage = selection.coverage.empty() ? selection.image.pixels[selected].a
                                                                         : selection.coverage[selected];
                    const bool feathered = selection.source && (*selection.source).feathered;
                    allowed = allowed && (feathered ? coverage >= 128 : coverage != 0);
                }
            }
            if (atlas_preserve_alpha && (document.tool == Tool::Eraser || !paint_base_.pixels[pixel].a)) {
                allowed = false;
            }
            indexed_allowed_[pixel] = allowed ? 1 : 0;
        }
    }
    indexed_gesture_ = true;
    indexed_checkpoint_ = false;
    dragging_ = true;
    (*canvas_).set_pointer_capture(true);
    move_indexed_paint(point);
    if (document.tool == Tool::Fill) {
        release_gesture();
        refresh();
    }
    return true;
}
void Editor::move_indexed_paint(Point point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > 100000 ||
        std::abs(point.y) > 100000) {
        throw std::runtime_error("Indexed stroke position is outside the supported canvas range.");
    }
    IconFrame& frame = document.atlas.icons[document.atlas.active];
    if (!has_icon_palette(frame)) {
        throw std::runtime_error("The indexed image changed during this stroke.");
    }
    const int width = frame.image.width, height = frame.image.height;
    std::vector<std::size_t> pixels;
    if (document.tool == Tool::Fill) {
        const std::size_t seed = static_cast<std::size_t>(std::floor(start_.y)) * width +
                                 static_cast<std::size_t>(std::floor(start_.x));
        const IconSource& source = *frame.source;
        std::vector<std::uint8_t> visited(frame.image.pixels.size(), 0);
        if (indexed_allowed_[seed]) {
            pixels.push_back(seed);
            visited[seed] = 1;
        }
        for (std::size_t cursor = 0; cursor < pixels.size(); ++cursor) {
            const int x = static_cast<int>(pixels[cursor] % width),
                      y = static_cast<int>(pixels[cursor] / width);
            const int dx[] = {-1, 1, 0, 0}, dy[] = {0, 0, -1, 1};
            for (int direction = 0; direction < 4; ++direction) {
                int nx = x + dx[direction], ny = y + dy[direction];
                if (atlas_wrap) {
                    nx = (nx + width) % width;
                    ny = (ny + height) % height;
                }
                if (!frame.image.contains(nx, ny)) {
                    continue;
                }
                const std::size_t next = static_cast<std::size_t>(ny) * width + nx;
                if (!visited[next] && indexed_allowed_[next] &&
                    source.indices[next] == source.indices[seed] &&
                    frame.image.pixels[next].a == frame.image.pixels[seed].a) {
                    pixels.push_back(next);
                    visited[next] = 1;
                }
            }
        }
    } else {
        // Draw a coverage mask with the existing discrete pencil/hard eraser.
        // Pixel colors never determine which palette index the gesture writes.
        const bool erase = document.tool == Tool::Eraser;
        Image mask;
        mask.reset(width, height, {0, 0, 0, static_cast<std::uint8_t>(erase ? 255 : 0)});
        const double radius = erase ? gesture_ink_.size * 0.5 + 2 : 1;
        const int min_x =
            atlas_wrap ? static_cast<int>(std::ceil((-std::max(last_.x, point.x) - radius) / width)) : 0;
        const int max_x =
            atlas_wrap ? static_cast<int>(std::floor((width - std::min(last_.x, point.x) + radius) / width))
                       : 0;
        const int min_y =
            atlas_wrap ? static_cast<int>(std::ceil((-std::max(last_.y, point.y) - radius) / height)) : 0;
        const int max_y =
            atlas_wrap ? static_cast<int>(std::floor((height - std::min(last_.y, point.y) + radius) / height))
                       : 0;
        Ink ink = pencil_ink(gesture_ink_);
        ink.primary = {255, 255, 255, 255};
        ink.pattern = Pattern::Solid;
        EraserStroke eraser;
        for (int row = min_y; row <= max_y; ++row) {
            for (int column = min_x; column <= max_x; ++column) {
                const Point a{last_.x + column * width, last_.y + row * height};
                const Point b{point.x + column * width, point.y + row * height};
                if (erase) {
                    eraser.segment(mask, a, b, gesture_ink_.size, false);
                } else {
                    pixel_line(mask, a, b, ink);
                }
            }
        }
        for (std::size_t pixel = 0; pixel < mask.pixels.size(); ++pixel) {
            if (indexed_allowed_[pixel] &&
                (erase ? mask.pixels[pixel].a < 128 : mask.pixels[pixel].a >= 128)) {
                pixels.push_back(pixel);
            }
        }
    }
    IconFrame result = paint_icon_indices(frame, pixels, indexed_entry_, document.tool == Tool::Eraser);
    if (result.source != frame.source) {
        if (!indexed_checkpoint_) {
            document.checkpoint();
            indexed_checkpoint_ = true;
        }
        document.image = result.image;
        frame = std::move(result);
        ++document.atlas_epoch;
        ++canvas_revision;
        publish_image(document.image, *canvas_);
    }
    last_ = current_ = point;
    update_status();
}
} // namespace paint::forms
