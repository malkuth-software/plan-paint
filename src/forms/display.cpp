#include "localization.hpp"
#include "forms/display.hpp"
#include "forms/editor.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace paint::forms {
static void publish_pixels(const Image& source, gui_forms::RasterCanvas& canvas, Rect damage) {
    const int width = source.width, height = source.height;
    std::shared_ptr<gui_drawing::Bitmap> bitmap = canvas.bitmap.read();
    bool replacement = !bitmap || (*bitmap).width() != static_cast<unsigned>(width) ||
                       (*bitmap).height() != static_cast<unsigned>(height);
    if (replacement) {
        bitmap = std::make_shared<gui_drawing::Bitmap>(width, height);
    }
    if (replacement || damage.w <= 0 || damage.h <= 0) {
        damage = {0, 0, width, height};
    }
    int right = std::clamp(damage.x + damage.w, 0, width);
    int bottom = std::clamp(damage.y + damage.h, 0, height);
    damage.x = std::clamp(damage.x, 0, width);
    damage.y = std::clamp(damage.y, 0, height);
    damage.w = right - damage.x;
    damage.h = bottom - damage.y;
    if (damage.w <= 0 || damage.h <= 0) {
        return;
    }
    gui_drawing::BitmapEditView edit = (*bitmap).begin_edit({damage.x, damage.y, damage.w, damage.h});
    for (int y = 0; y < damage.h; ++y) {
        std::byte* row = edit.writable_data + static_cast<std::size_t>(y) * edit.row_bytes;
        for (int x = 0; x < damage.w; ++x) {
            const int sx = x + damage.x;
            const int sy = y + damage.y;
            Color color = source.pixels[static_cast<std::size_t>(sy) * source.width + sx];
            // Rounded premultiplication p = (channel * alpha + 127) / 255.
            row[x * 4] = static_cast<std::byte>((color.b * color.a + 127U) / 255U);
            row[x * 4 + 1] = static_cast<std::byte>((color.g * color.a + 127U) / 255U);
            row[x * 4 + 2] = static_cast<std::byte>((color.r * color.a + 127U) / 255U);
            row[x * 4 + 3] = static_cast<std::byte>(color.a);
        }
    }
    static_cast<void>((*bitmap).commit_edit(edit.token));
    if (replacement) {
        canvas.bitmap = bitmap;
        if (canvas.last_resource_error() != gui_forms::ImageResourceError::none) {
            throw std::runtime_error(tr("The image exceeds the window's display resource budget"));
        }
    } else if (!canvas.synchronize_bitmap()) {
        throw std::runtime_error(tr("Canvas resource synchronization failed"));
    }
}
void publish_image(const Image& source, gui_forms::RasterCanvas& canvas, Rect damage) {
    publish_pixels(source, canvas, damage);
}
void publish_image(const Image& source, PaintCanvas& canvas, Rect damage) {
    canvas.publish_pixels(source, damage);
    canvas.publish_source(source, damage);
}
void PaintCanvas::publish_pixels(const Image& source, Rect damage) {
    if (source.width <= 0 || source.height <= 0) { return; }
    const gui_forms::LiveSurfacePixelFormat format = gui_forms::native_live_surface_pixel_format();
    gui_forms::LiveSurfaceFrame previous = display_surface_ ? (*display_surface_).acquire_latest()
                                                          : gui_forms::LiveSurfaceFrame{};
    bool replacement = !previous || previous.width() != static_cast<unsigned>(source.width) ||
                       previous.height() != static_cast<unsigned>(source.height);
    if (replacement || damage.w <= 0 || damage.h <= 0) {
        damage = {0, 0, source.width, source.height};
    }
    const int right = std::clamp(damage.x + damage.w, 0, source.width);
    const int bottom = std::clamp(damage.y + damage.h, 0, source.height);
    damage.x = std::clamp(damage.x, 0, source.width);
    damage.y = std::clamp(damage.y, 0, source.height);
    damage.w = right - damage.x;
    damage.h = bottom - damage.y;
    if (damage.w <= 0 || damage.h <= 0) { return; }
    std::size_t translucent = replacement ? 0 : translucent_pixels_;
    const bool bgra = format == gui_forms::LiveSurfacePixelFormat::bgra32_premultiplied_srgb;
    bool changed = replacement;
    // The bounds of the pixels that really changed (a caller may name the
    // whole picture, as refresh does): only they are written, published and
    // repainted, so a brush stroke damages its own few pixels.
    int changed_left = right;
    int changed_top = bottom;
    int changed_right = damage.x;
    int changed_bottom = damage.y;
    for (int y = damage.y; y < bottom; ++y) {
        for (int x = damage.x; x < right; ++x) {
            const Color color = source.get(x, y);
            if (!replacement) {
                const std::byte* pixel = previous.pixels().data() + y * previous.row_bytes() + x * 4;
                if (pixel[3] != std::byte{255}) { --translucent; }
                const bool differs = pixel[0] != static_cast<std::byte>(((bgra ? color.b : color.r) * color.a + 127U) / 255U) ||
                    pixel[1] != static_cast<std::byte>((color.g * color.a + 127U) / 255U) ||
                    pixel[2] != static_cast<std::byte>(((bgra ? color.r : color.b) * color.a + 127U) / 255U) ||
                    pixel[3] != static_cast<std::byte>(color.a);
                if (differs) {
                    changed = true;
                    changed_left = std::min(changed_left, x);
                    changed_right = std::max(changed_right, x + 1);
                    changed_top = std::min(changed_top, y);
                    changed_bottom = std::max(changed_bottom, y + 1);
                }
            }
            if (color.a != 255) { ++translucent; }
        }
    }
    if (!changed) { return; }
    if (!replacement) {
        damage = {changed_left, changed_top, changed_right - changed_left, changed_bottom - changed_top};
    }
    const bool opaque = translucent == 0;
    if (replacement || previous.opaque() != opaque) {
        display_surface_ = gui_forms::LiveSurface::create({static_cast<unsigned>(source.width),
            static_cast<unsigned>(source.height), format, gui_forms::default_live_surface_buffer_count, opaque});
        replacement = true;
        damage = {0, 0, source.width, source.height};
    }
    gui_forms::LiveSurfaceWriteLease write = (*display_surface_).try_acquire_write(!replacement);
    // A held native read lease must never make an editing update disappear.
    if (!write) {
        display_surface_ = gui_forms::LiveSurface::create({static_cast<unsigned>(source.width),
            static_cast<unsigned>(source.height), format, gui_forms::default_live_surface_buffer_count, opaque});
        write = (*display_surface_).try_acquire_write();
        damage = {0, 0, source.width, source.height};
    }
    if (!write) { throw std::runtime_error(tr("Canvas resource synchronization failed")); }
    for (int y = damage.y; y < damage.y + damage.h; ++y) {
        std::byte* row = write.pixels().data() + y * write.row_bytes();
        for (int x = damage.x; x < damage.x + damage.w; ++x) {
            const Color color = source.get(x, y);
            row[x * 4] = static_cast<std::byte>(((bgra ? color.b : color.r) * color.a + 127U) / 255U);
            row[x * 4 + 1] = static_cast<std::byte>((color.g * color.a + 127U) / 255U);
            row[x * 4 + 2] = static_cast<std::byte>(((bgra ? color.r : color.b) * color.a + 127U) / 255U);
            row[x * 4 + 3] = static_cast<std::byte>(color.a);
        }
    }
    static_cast<void>(write.publish({static_cast<double>(damage.x), static_cast<double>(damage.y),
        static_cast<double>(damage.w), static_cast<double>(damage.h)}));
    translucent_pixels_ = translucent;
    // The viewport conversion redoes only what changed since it last ran.
    if (replacement || viewport_dirty_.w <= 0 || viewport_dirty_.h <= 0) {
        viewport_dirty_ = replacement ? Rect{0, 0, source.width, source.height} : damage;
    } else {
        const int dirty_right = std::max(viewport_dirty_.x + viewport_dirty_.w, damage.x + damage.w);
        const int dirty_bottom = std::max(viewport_dirty_.y + viewport_dirty_.h, damage.y + damage.h);
        viewport_dirty_.x = std::min(viewport_dirty_.x, damage.x);
        viewport_dirty_.y = std::min(viewport_dirty_.y, damage.y);
        viewport_dirty_.w = dirty_right - viewport_dirty_.x;
        viewport_dirty_.h = dirty_bottom - viewport_dirty_.y;
    }
    invalidate(gui_forms::Dirty::semantics);
    if (replacement || std::abs(view_angle) > 1e-10) {
        invalidate(gui_forms::Dirty::paint);
        return;
    }
    // The changed pixels on screen, a unit wider for the edges' rounding.
    const gui_forms::Rect shown = RasterCanvas::bitmap_to_client({damage.x, damage.y, damage.w, damage.h});
    invalidate(gui_forms::Rect{shown.x - 1, shown.y - 1, shown.width + 2, shown.height + 2});
}
} // namespace paint::forms
