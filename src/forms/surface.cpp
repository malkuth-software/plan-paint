#include "localization.hpp"
#include "forms/editor.hpp"
#include "raster.hpp"
#include <algorithm>
#include <cmath>
namespace paint::forms {
namespace gf = gui_forms;
Point PaintCanvas::visible_paste_origin(int width, int height) {
    const gf::Rect viewport = client_rectangle();
    if (width < 1 || height < 1 || viewport.width < 1 || viewport.height < 1) {
        return {};
    }
    const double margin = std::min({12.0, viewport.width / 4, viewport.height / 4});
    std::vector<gf::Point> polygon;
    const gui_drawing::PointF origin = view_origin();
    for (const Point corner : {Point{0, 0}, Point{static_cast<double>(width), 0},
                               Point{static_cast<double>(width), static_cast<double>(height)},
                               Point{0, static_cast<double>(height)}}) {
        const gui_drawing::PointF point = view_point({corner.x, corner.y});
        polygon.push_back({(point.x - origin.x) * zoom(), (point.y - origin.y) * zoom()});
    }
    // Clip the rotated document to the viewport, including room for its handles.
    // Clamping an inverse-transformed corner alone can put it off screen again.
    for (int edge = 0; edge < 4 && !polygon.empty(); ++edge) {
        std::vector<gf::Point> clipped;
        const double boundary = edge == 0   ? margin
                                : edge == 1 ? viewport.width - margin
                                : edge == 2 ? margin
                                            : viewport.height - margin;
        gf::Point previous = polygon.back();
        double previous_distance = (edge < 2 ? previous.x : previous.y) - boundary;
        if (edge == 1 || edge == 3) {
            previous_distance = -previous_distance;
        }
        for (const gf::Point current : polygon) {
            double distance = (edge < 2 ? current.x : current.y) - boundary;
            if (edge == 1 || edge == 3) {
                distance = -distance;
            }
            if ((distance >= 0) != (previous_distance >= 0)) {
                const double fraction = previous_distance / (previous_distance - distance);
                clipped.push_back({previous.x + fraction * (current.x - previous.x),
                                   previous.y + fraction * (current.y - previous.y)});
            }
            if (distance >= 0) {
                clipped.push_back(current);
            }
            previous = current;
            previous_distance = distance;
        }
        polygon = std::move(clipped);
    }
    if (polygon.empty()) {
        const gui_drawing::PointF center = view_point({width * 0.5, height * 0.5});
        set_view_origin(
            {center.x - viewport.width / (2 * zoom()), center.y - viewport.height / (2 * zoom())});
        return visible_paste_origin(width, height);
    }
    gf::Point corner = polygon.front();
    for (const gf::Point candidate : polygon) {
        if (candidate.x + candidate.y < corner.x + corner.y) {
            corner = candidate;
        }
    }
    const gui_drawing::PointF point = client_to_bitmap(corner);
    const Point result{std::clamp(std::round(point.x), 0.0, static_cast<double>(width - 1)),
                       std::clamp(std::round(point.y), 0.0, static_cast<double>(height - 1))};
    const gui_drawing::PointF projected = view_point({result.x, result.y});
    const double screen_x = (projected.x - origin.x) * zoom();
    const double screen_y = (projected.y - origin.y) * zoom();
    if (screen_x < 0 || screen_x >= viewport.width || screen_y < 0 || screen_y >= viewport.height) {
        set_view_origin(
            {origin.x + (screen_x - std::clamp(screen_x, margin, viewport.width - margin)) / zoom(),
             origin.y + (screen_y - std::clamp(screen_y, margin, viewport.height - margin)) / zoom()});
    }
    return result;
}
gui_drawing::PointF PaintCanvas::view_point(gui_drawing::PointF point) const noexcept {
    const double c = std::cos(view_angle), s = std::sin(view_angle);
    return {c * point.x - s * point.y, s * point.x + c * point.y};
}
gui_drawing::PointF PaintCanvas::document_point(gui_drawing::PointF point) const noexcept {
    const double c = std::cos(view_angle), s = std::sin(view_angle);
    return {c * point.x + s * point.y, -s * point.x + c * point.y};
}
gui_drawing::PointF PaintCanvas::client_to_bitmap(gf::Point client) const noexcept {
    return document_point(RasterCanvas::client_to_bitmap(client));
}
gf::Rect PaintCanvas::bitmap_to_client(gui_drawing::RectI pixels) const noexcept {
    const gui_drawing::PointF origin = view_origin();
    double left = 1e30, top = 1e30, right = -1e30, bottom = -1e30;
    for (int i = 0; i < 4; ++i) {
        const gui_drawing::PointF point =
            view_point({static_cast<double>(pixels.x + (i & 1 ? pixels.width : 0)),
                        static_cast<double>(pixels.y + (i & 2 ? pixels.height : 0))});
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    return {(left - origin.x) * zoom(), (top - origin.y) * zoom(), (right - left) * zoom(),
            (bottom - top) * zoom()};
}
gf::Rect PaintCanvas::view_bounds() const noexcept {
    const gf::LiveSurfaceFrame image = display_surface_ ? (*display_surface_).acquire_latest() : gf::LiveSurfaceFrame{};
    if (!image) {
        return {};
    }
    const gf::Rect bounds =
        bitmap_to_client({0, 0, static_cast<int>(image.width()), static_cast<int>(image.height())});
    return {bounds.x / zoom() + view_origin().x, bounds.y / zoom() + view_origin().y, bounds.width / zoom(),
            bounds.height / zoom()};
}
gf::Point PaintCanvas::rotation_handle() const noexcept {
    const gf::LiveSurfaceFrame image = display_surface_ ? (*display_surface_).acquire_latest() : gf::LiveSurfaceFrame{};
    if (!image) {
        return {-100, -100};
    }
    const gf::Rect viewport = client_rectangle();
    const double width = image.width(), height = image.height();
    // Prefer the upper-right corner, then another visible corner. Never cover
    // artwork when panning puts that corner beyond the viewport.
    for (int corner = 0; corner < 4; ++corner) {
        const bool right = corner == 0 || corner == 3, bottom = corner >= 2;
        const gui_drawing::PointF point = view_point({right ? width : 0, bottom ? height : 0});
        const gui_drawing::PointF offset = view_point({right ? 20.0 : -20.0, bottom ? 20.0 : -20.0});
        const gf::Point candidate{std::clamp((point.x - view_origin().x) * zoom() + offset.x, 14.0,
                                             std::max(14.0, viewport.width - 14)),
                                  std::clamp((point.y - view_origin().y) * zoom() + offset.y, 14.0,
                                             std::max(14.0, viewport.height - 14))};
        const gui_drawing::PointF document = client_to_bitmap(candidate);
        const double dx = std::max({-document.x, document.x - width, 0.0});
        const double dy = std::max({-document.y, document.y - height, 0.0});
        if (std::hypot(dx, dy) * zoom() >= 14) {
            return candidate;
        }
    }
    // There is no surround to host the grip when artwork fills the viewport.
    return {-100, -100};
}
void PaintCanvas::stroke_outline(gf::Painter& painter, gui_drawing::RectI pixels, gf::Color color,
                                 double width) const {
    if (std::abs(view_angle) < 1e-10) {
        painter.stroke_rect(bitmap_to_client(pixels), color, width);
        return;
    }
    gf::Point points[4];
    for (int i = 0; i < 4; ++i) {
        const gui_drawing::PointF point =
            view_point({static_cast<double>(pixels.x + (i == 1 || i == 2 ? pixels.width : 0)),
                        static_cast<double>(pixels.y + (i >= 2 ? pixels.height : 0))});
        points[i] = {(point.x - view_origin().x) * zoom(), (point.y - view_origin().y) * zoom()};
    }
    for (int i = 0; i < 4; ++i) {
        painter.draw_line(points[i], points[(i + 1) % 4], color, width);
    }
}
void PaintCanvas::on_attached_to_window() {
    RasterCanvas::on_attached_to_window();
    update_backing();
}
void PaintCanvas::update_backing() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const CanvasBacking choice = editor ? (*editor).settings.canvas_backing : CanvasBacking::PaleFelt;
    if (!attached_window() || (backing_.value && choice == loaded_backing_)) {
        return;
    }
    const Image texture = canvas_backing_texture(choice);
    std::vector<std::byte> pixels(texture.pixels.size() * 4);
    for (std::size_t index = 0; index < texture.pixels.size(); ++index) {
        const Color color = texture.pixels[index];
        pixels[index * 4] = static_cast<std::byte>(color.b);
        pixels[index * 4 + 1] = static_cast<std::byte>(color.g);
        pixels[index * 4 + 2] = static_cast<std::byte>(color.r);
        pixels[index * 4 + 3] = std::byte{255};
    }
    const gf::ImageLoadResult result =
        backing_.value
            ? (*attached_window())
                  .replace_bgra32_premultiplied(backing_, texture.width, texture.height, texture.width * 4,
                                                pixels, *this)
            : (*attached_window())
                  .load_bgra32_premultiplied(texture.width, texture.height, texture.width * 4, pixels);
    if (result) {
        backing_ = result.image;
        loaded_backing_ = choice;
    }
}
void PaintCanvas::on_detaching_from_window(gf::Window& former_window) noexcept {
    if (backing_.value) {
        static_cast<void>(former_window.remove_image(backing_));
    }
    if (repeated_.value) {
        static_cast<void>(former_window.remove_image(repeated_));
    }
    if (reference_.value) {
        static_cast<void>(former_window.remove_image(reference_));
    }
    if (rotated_.value && attached_window()) {
        static_cast<void>((*attached_window()).remove_image(rotated_));
    }
    rotated_ = {};
    view_field_.reset();
    prepared_generation_ = rendered_generation_ = 0;
    repeated_ = {};
    reference_ = {};
    atlas_revision_ = 0;
    backing_ = {};
    loaded_backing_ = CanvasBacking::Count;
    display_request_.disconnect();
    preparation_request_.disconnect();
    viewport_surface_.reset();
    viewport_source_.reset();
    viewport_generation_ = 0;
    RasterCanvas::on_detaching_from_window(former_window);
}
void PaintCanvas::on_dispose() noexcept {
    if (backing_.value && window()) {
        static_cast<void>((*window()).remove_image(backing_));
    }
    if (window() && repeated_.value) {
        static_cast<void>((*window()).remove_image(repeated_));
    }
    if (window() && reference_.value) {
        static_cast<void>((*window()).remove_image(reference_));
    }
    if (rotated_.value && attached_window()) {
        static_cast<void>((*attached_window()).remove_image(rotated_));
    }
    rotated_ = {};
    view_field_.reset();
    prepared_generation_ = rendered_generation_ = 0;
    repeated_ = {};
    reference_ = {};
    atlas_revision_ = 0;
    backing_ = {};
    loaded_backing_ = CanvasBacking::Count;
    display_surface_.reset();
    viewport_surface_.reset();
    viewport_source_.reset();
    RasterCanvas::on_dispose();
}
void PaintCanvas::on_paint(gf::Painter& painter, gf::Rect damage) {
    gf::Rect bounds = client_rectangle();
    const std::shared_ptr<Editor> editor = editor_.lock();
    update_backing();
    if (backing_.value) {
        const double tile = canvas_backing_tile_size(loaded_backing_);
        const double pixels = loaded_backing_ == CanvasBacking::PaleFelt ? 128 : 512;
        painter.fill_image_pattern(backing_, {pixels, pixels}, bounds, {tile, tile});
    } else {
        painter.fill_rect(bounds, gf::Color::rgba(208, 218, 226));
    }
    const gf::GradientStop light[] = {{0, gf::Color::rgba(255, 255, 255, 22)},
                                      {1, gf::Color::rgba(60, 77, 95, 15)}};
    painter.fill_linear_gradient(bounds, {0, 0}, {0, bounds.height}, light);
    if (editor && std::abs(view_angle) > 1e-10) {
        paint_rotated(painter, *editor);
        return;
    }
    const gf::LiveSurfaceFrame image = display_surface_ ? (*display_surface_).acquire_latest() : gf::LiveSurfaceFrame{};
    if (image) {
        gf::Rect sheet = RasterCanvas::bitmap_to_client(
            {0, 0, static_cast<int>(image.width()), static_cast<int>(image.height())});
        painter.draw_box_shadow(sheet, 0, {1.5, 2.5}, 6, 0, gf::Color::rgba(42, 53, 67, 75));
        painter.stroke_rect({sheet.x - 0.5, sheet.y - 0.5, sheet.width + 1, sheet.height + 1},
                            gf::Color::rgba(93, 111, 130, 155), 1);
    }
    paint_display(painter);
    static_cast<void>(damage);
    if (editor) {
        paint_atlas_context(painter, *editor);
    }
}
namespace {
gf::ImageId update_atlas_image(gf::Window& window, gf::Control& owner, gf::ImageId previous,
                               const Image& image) {
    if (image.pixels.empty()) {
        if (previous.value) {
            static_cast<void>(window.remove_image(previous));
        }
        return {};
    }
    std::vector<std::byte> pixels(image.pixels.size() * 4);
    for (std::size_t index = 0; index < image.pixels.size(); ++index) {
        const Color color = image.pixels[index];
        const std::size_t output = index;
        pixels[output * 4] = static_cast<std::byte>((color.b * color.a + 127) / 255);
        pixels[output * 4 + 1] = static_cast<std::byte>((color.g * color.a + 127) / 255);
        pixels[output * 4 + 2] = static_cast<std::byte>((color.r * color.a + 127) / 255);
        pixels[output * 4 + 3] = static_cast<std::byte>(color.a);
    }
    const int width = image.width, height = image.height;
    const gf::ImageLoadResult result =
        previous.value
            ? window.replace_bgra32_premultiplied(previous, width, height, width * 4, pixels, owner)
            : window.load_bgra32_premultiplied(width, height, width * 4, pixels);
    return result ? result.image : previous;
}
} // namespace
void PaintCanvas::paint_atlas_context(gf::Painter& painter, const Editor& editor) {
    if (editor.document.atlas.kind == AtlasKind::None || editor.document.atlas.active < 0) {
        return;
    }
    if (atlas_revision_ != editor.canvas_revision) {
        repeated_ = update_atlas_image(*window(), *this, repeated_,
                                       editor.atlas_wrap ? editor.document.image : Image{});
        reference_ = update_atlas_image(*window(), *this, reference_, editor.atlas_reference);
        atlas_revision_ = editor.canvas_revision;
    }
    const int width = editor.document.image.width, height = editor.document.image.height;
    if (repeated_.value) {
        painter.save();
        painter.clip_rect(client_rectangle());
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                if (x == 0 && y == 0) {
                    continue;
                }
                const gf::Rect bounds = bitmap_to_client({x * width, y * height, width, height});
                painter.fill_rect(bounds, gf::Color::rgba(242, 242, 242));
                painter.draw_image(repeated_, bounds);
            }
        }
        painter.stroke_rect(bitmap_to_client({0, 0, width, height}), gf::Color::rgba(34, 107, 172, 155), 1);
        painter.restore();
    }
    if (reference_.value) {
        painter.draw_image(reference_, bitmap_to_client({0, 0, width, height}), 0.5);
    }
}
void Editor::paint_tool_preview(gf::Painter& painter) {
    if (cursor_client_ && document.tool == Tool::Brush && brush_family == BrushFamily::Heal) {
        const gf::Point center = *cursor_client_;
        const double radius = std::max(2.0, document.ink.size * canvas().zoom() * 0.5);
        const gf::Color white = gf::Color::rgba(255, 255, 255), blue = gf::Color::rgba(25, 98, 162);
        painter.stroke_rounded_rect({center.x - radius, center.y - radius, radius * 2, radius * 2}, radius,
                                    white, 3);
        painter.stroke_rounded_rect({center.x - radius, center.y - radius, radius * 2, radius * 2}, radius,
                                    blue, 1);
        if (healing_brush_.has_source()) {
            const gui_drawing::PointF point = canvas().client_to_bitmap(center);
            const gf::Point source =
                screen(!dragging_ && !heal_options.aligned ? healing_brush_.source_origin()
                                                           : healing_brush_.source_for({point.x, point.y}));
            painter.draw_line({source.x - 7, source.y}, {source.x + 7, source.y}, white, 3);
            painter.draw_line({source.x, source.y - 7}, {source.x, source.y + 7}, white, 3);
            painter.draw_line({source.x - 7, source.y}, {source.x + 7, source.y}, blue, 1);
            painter.draw_line({source.x, source.y - 7}, {source.x, source.y + 7}, blue, 1);
        }
        return;
    }
    if (!cursor_client_ ||
        (document.tool != Tool::Pencil && document.tool != Tool::Eraser && document.tool != Tool::Magnifier &&
         !(document.tool == Tool::Picker && picker_magnifier))) {
        return;
    }
    gui_drawing::PointF point = (*canvas_).client_to_bitmap(*cursor_client_);
    int x = static_cast<int>(std::floor(point.x)), y = static_cast<int>(std::floor(point.y));
    if (!document.image.contains(x, y)) {
        return;
    }
    if (document.tool == Tool::Magnifier || document.tool == Tool::Picker) {
        const double radius = 64;
        const gf::Point center = *cursor_client_;
        const gf::Rect lens{center.x - radius, center.y - radius, radius * 2, radius * 2};
        painter.draw_box_shadow(lens, radius, {1, 2}, 5, 0, gf::Color::rgba(30, 40, 55, 90));
        // Scanline intersections with the circle clip every enlarged pixel,
        // including edge pixels, without an off-center rectangular lens.
        for (int row = -64; row < 64; ++row) {
            const double half = std::sqrt(radius * radius - (row + 0.5) * (row + 0.5));

            for (int column = -64; column < 64; column += 1) {
                const double left = std::max(static_cast<double>(column), -half);
                const double right = std::min(static_cast<double>(column + 1), half);
                if (right <= left) {
                    continue;
                }
                const gui_drawing::PointF offset =
                    canvas().document_point({(column + 0.5) / 8, (row + 0.5) / 8});
                const int px = static_cast<int>(std::floor(point.x + offset.x));
                const int py = static_cast<int>(std::floor(point.y + offset.y));
                Color color = document.image.get(px, py);
                const int checker = ((px / 4 + py / 4) & 1) ? 225 : 255;
                const double alpha = color.a / 255.0;
                if (document.selection.active && !document.selection.on_canvas) {
                    const Color selected =
                        document.selection.image.get(px - document.selection.x, py - document.selection.y);
                    const double sa = selected.a / 255.0;
                    color.r = static_cast<std::uint8_t>(selected.r * sa +
                                                        (color.r * alpha + checker * (1 - alpha)) * (1 - sa));
                    color.g = static_cast<std::uint8_t>(selected.g * sa +
                                                        (color.g * alpha + checker * (1 - alpha)) * (1 - sa));
                    color.b = static_cast<std::uint8_t>(selected.b * sa +
                                                        (color.b * alpha + checker * (1 - alpha)) * (1 - sa));
                } else {
                    color.r = static_cast<std::uint8_t>(color.r * alpha + checker * (1 - alpha));
                    color.g = static_cast<std::uint8_t>(color.g * alpha + checker * (1 - alpha));
                    color.b = static_cast<std::uint8_t>(color.b * alpha + checker * (1 - alpha));
                }
                painter.fill_rect({center.x + left, center.y + row, right - left, 1},
                                  gf::Color::rgba(color.r, color.g, color.b));
            }
        }
        painter.stroke_rounded_rect(lens, radius, gf::Color::rgba(45, 90, 140), 1.5);
        painter.draw_line({center.x - 5, center.y}, {center.x + 5, center.y},
                          gf::Color::rgba(255, 255, 255, 200), 1);
        painter.draw_line({center.x, center.y - 5}, {center.x, center.y + 5},
                          gf::Color::rgba(30, 40, 55, 200), 1);
        painter.draw_text_utf8({center.x - 8, center.y + 51}, "8×", {gf::FontRole::control, 12, 600, false},
                               gf::Color::rgba(35, 60, 90));
        return;
    }
    double scale = (*canvas_).zoom();
    painter.save();
    painter.clip_rect((*canvas_).bitmap_to_client({0, 0, document.image.width, document.image.height}));
    if (document.tool == Tool::Pencil && !dragging_) {
        Color color = patterned(pencil_ink(document.primary_ink()), x, y);
        gf::Rect pixel = (*canvas_).bitmap_to_client({x, y, 1, 1});
        if (std::abs(canvas().view_angle) < 1e-10) {
            painter.fill_rect(pixel, gf::Color::rgba(color.r, color.g, color.b, color.a));
        }
        if (scale >= 4) {
            canvas().stroke_outline(painter, {x, y, 1, 1}, gf::Color::rgba(30, 30, 30, 220), 3);
            canvas().stroke_outline(painter, {x, y, 1, 1}, gf::Color::rgba(255, 255, 255, 230), 1);
        }
    } else if (document.tool == Tool::Eraser) {
        double radius = std::max(0.5, document.ink.size * 0.5) * scale;
        gf::Point center = *cursor_client_;
        gf::Rect circle{center.x - radius, center.y - radius, radius * 2, radius * 2};
        if (eraser_soft) {
            double previous = 0;
            for (int ring = 32; ring > 0; --ring) {
                double fraction = (ring - 0.5) / 32;
                double alpha = 0.42 * std::sqrt(1 - fraction * fraction);
                int layer = static_cast<int>(255 * (alpha - previous) / (1 - previous));
                double r = radius * ring / 32;
                painter.fill_rounded_rect({center.x - r, center.y - r, r * 2, r * 2}, r,
                                          gf::Color::rgba(245, 65, 118, static_cast<std::uint8_t>(layer)));
                previous = alpha;
            }
        } else {
            painter.fill_rounded_rect(circle, radius, gf::Color::rgba(245, 65, 118, 100));
        }
        painter.stroke_rounded_rect(circle, radius, gf::Color::rgba(199, 37, 89, 220), 1.25);
    }
    painter.restore();
}

namespace {
struct DisplayDeadline {
    std::weak_ptr<PaintCanvas> canvas;
    void operator()(gf::FrameTime now) const {
        const std::shared_ptr<PaintCanvas> owner = canvas.lock();
        if (owner) {
            (*owner).display_frame(now);
        }
    }
};
struct PreparationDeadline {
    std::weak_ptr<PaintCanvas> canvas;
    void operator()(gf::FrameTime now) const {
        const std::shared_ptr<PaintCanvas> owner = canvas.lock();
        if (owner) {
            (*owner).preparation_frame(now);
        }
    }
};
struct ViewDelivery {
    std::weak_ptr<PaintCanvas> canvas;
    void operator()() const {
        const std::shared_ptr<PaintCanvas> owner = canvas.lock();
        if (owner) {
            (*owner).poll_view();
        }
    }
};
struct ViewCompletion {
    PaintCanvas* canvas;
    std::weak_ptr<PaintCanvas> owner;
    void operator()() const {
        static_cast<void>((*canvas).begin_invoke(ViewDelivery{owner}));
    }
};
} // namespace
PaintCanvas::~PaintCanvas() {
    view_worker_.cancel();
    view_worker_.wait();
}
bool PaintCanvas::view_busy() const {
    return view_worker_.busy() || display_request_.connected() || preparation_request_.connected();
}
void PaintCanvas::publish_source(const Image& source, Rect damage) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !(*editor).settings.rotate_view || std::abs(view_angle) < 1e-10) {
        view_worker_.cancel();
        display_request_.disconnect();
        preparation_request_.disconnect();
        if (rotated_.value && window()) {
            static_cast<void>((*window()).remove_image(rotated_));
        }
        rotated_ = {};
        view_source_ = {};
        view_field_.reset();
        ++source_generation_;
        return;
    }
    bool changed = source.width != view_source_.width || source.height != view_source_.height;
    if (changed) {
        view_source_ = source;
    } else {
        if (damage.w <= 0 || damage.h <= 0) {
            damage = {0, 0, source.width, source.height};
        }
        const int right = std::min(source.width, damage.x + damage.w),
                  bottom = std::min(source.height, damage.y + damage.h);
        for (int y = std::max(0, damage.y); y < bottom; ++y) {
            for (int x = std::max(0, damage.x); x < right; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * source.width + x;
                if (!equal(source.pixels[i], view_source_.pixels[i])) {
                    view_source_.pixels[i] = source.pixels[i];
                    changed = true;
                }
            }
        }
    }
    if (changed) {
        ++source_generation_;
        preparation_deadline_ = gf::FrameClock::now() + std::chrono::milliseconds(120);
        preparation_request_.disconnect();
        view_error.clear();
        view_worker_.cancel();
    }
    prepare_view();
    prepare_display();
}
void PaintCanvas::prepare_view(gf::FrameTime now) {
    if (std::abs(view_angle) < 1e-10 || !attached_window() || view_worker_.busy() ||
        view_source_.pixels.empty() || prepared_generation_ == source_generation_ || !view_error.empty()) {
        return;
    }
    if (now < preparation_deadline_) {
        if (!preparation_request_.connected()) {
            preparation_request_ = (*window()).schedule_ui_timer(
                *this, std::chrono::milliseconds(120), preparation_deadline_,
                PreparationDeadline{std::static_pointer_cast<PaintCanvas>(shared_from_this())});
        }
        return;
    }
    preparation_request_.disconnect();
    if (!view_worker_initialized_) {
        view_worker_.set_completion(
            ViewCompletion{this, std::static_pointer_cast<PaintCanvas>(shared_from_this())});
        view_worker_initialized_ = true;
    }
    ++work_statistics_.view_preparations;
    view_worker_.compile(WarpTask::CompileRotation, view_source_, source_generation_);
}
void PaintCanvas::poll_view() {
    WarpResult result;
    if (!view_worker_.take(result)) {
        return;
    }
    if (result.generation == source_generation_) {
        if (result.error.empty()) {
            view_field_ = std::move(result.field);
            prepared_generation_ = result.generation;
            rendered_generation_ = 0;
        } else {
            view_error = result.error;
        }
    }
    prepare_view();
    prepare_display();
    invalidate(gf::Dirty::paint);
}
void PaintCanvas::set_zoom(double scale) {
    RasterCanvas::set_zoom(scale);
    prepare_display();
}
void PaintCanvas::set_view_origin(gui_drawing::PointF origin) {
    RasterCanvas::set_view_origin(origin);
    prepare_display();
}
void PaintCanvas::set_view(double scale, gui_drawing::PointF origin) {
    RasterCanvas::set_view(scale, origin);
    prepare_display();
}
void PaintCanvas::arrange(gf::Rect bounds) {
    RasterCanvas::arrange(bounds);
    prepare_display();
}
void PaintCanvas::preparation_frame(gf::FrameTime now) {
    preparation_request_.disconnect();
    prepare_view(now);
}
void PaintCanvas::settle_view() {
    preparation_deadline_ = gf::FrameClock::now();
    preparation_request_.disconnect();
    prepare_view();
}
void PaintCanvas::prepare_display() {
    if (!window() || std::abs(view_angle) < 1e-10) {
        return;
    }
    // The first image must exist before native paint can synchronize resources.
    if (!rotated_.value && !view_source_.pixels.empty()) {
        render_display();
        next_display_ = gf::FrameClock::now() + std::chrono::milliseconds(16);
    }
    if (!display_request_.connected()) {
        display_request_ = (*window()).schedule_ui_timer(
            *this, std::chrono::milliseconds(16), std::max(gf::FrameClock::now(), next_display_),
            DisplayDeadline{std::static_pointer_cast<PaintCanvas>(shared_from_this())});
    }
}
void PaintCanvas::display_frame(gf::FrameTime now) {
    display_request_.disconnect();
    next_display_ = now + std::chrono::milliseconds(16);
    render_display();
    const std::shared_ptr<Editor> owner = editor_.lock();
    if (owner) {
        (*owner).render_stamp_view();
    }
}
void PaintCanvas::render_display() {
    const std::shared_ptr<Editor> owner = editor_.lock();
    if (!owner || !window() || std::abs(view_angle) < 1e-10 || view_source_.pixels.empty()) {
        return;
    }
    const Editor& editor = *owner;
    const gf::Rect viewport = client_rectangle();
    const gui_drawing::PointF origin = view_origin();
    const int width = std::max(1, static_cast<int>(std::ceil(viewport.width)));
    const int height = std::max(1, static_cast<int>(std::ceil(viewport.height)));
    const std::uint64_t generation =
        source_generation_ * 2 + (prepared_generation_ == source_generation_ ? 1 : 0);
    if (!rotated_.value || rendered_generation_ != generation ||
        rendered_revision_ != editor.canvas_revision || rendered_angle_ != view_angle ||
        rendered_zoom_ != zoom() || rendered_origin_.x != origin.x || rendered_origin_.y != origin.y ||
        rendered_size_.width != width || rendered_size_.height != height) {
        ++work_statistics_.view_renders;
        Image result;
        const double c = std::cos(view_angle), s = std::sin(view_angle), scale = zoom();
        const bool prepared = view_field_ && prepared_generation_ == source_generation_;
        if (prepared) {
            // Image-field coordinates address pixel centres; canvas coordinates address pixel edges.
            const AffineMap map{c * scale, -s * scale, (0.5 * c - 0.5 * s - origin.x) * scale - 0.5,
                                s * scale, c * scale,  (0.5 * s + 0.5 * c - origin.y) * scale - 0.5};
            render_affine(*view_field_, map, width, height, result, WarpSampling::Point);
        } else {
            result.reset(width, height, {0, 0, 0, 0});
        }
        std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double vx = (x + 0.5) / scale + origin.x;
                const double vy = (y + 0.5) / scale + origin.y;
                const gui_drawing::PointF point{c * vx + s * vy, -s * vx + c * vy};
                const int sx = static_cast<int>(std::floor(point.x)),
                          sy = static_cast<int>(std::floor(point.y));
                const std::size_t index = static_cast<std::size_t>(y) * width + x;
                Color color = result.pixels[index];
                const bool inside = view_source_.contains(sx, sy);
                const bool wrapped = !inside && editor.atlas_wrap && editor.document.atlas.active >= 0 &&
                                     sx >= -view_source_.width && sx < 2 * view_source_.width &&
                                     sy >= -view_source_.height && sy < 2 * view_source_.height;
                if (inside || wrapped) {
                    // Source edits appear at once while the new CONV field prepares in the worker.
                    if (wrapped) {
                        color = editor.document.image.get(
                            (sx % view_source_.width + view_source_.width) % view_source_.width,
                            (sy % view_source_.height + view_source_.height) % view_source_.height);
                    } else if (!prepared) {
                        color = view_source_.get(sx, sy);
                    }
                    const int grid = ((static_cast<int>(std::floor(point.x * scale / 12)) +
                                       static_cast<int>(std::floor(point.y * scale / 12))) &
                                      1)
                                         ? 240
                                         : 255;
                    const Color background =
                        editor.settings.solid_transparency
                            ? editor.settings.transparency_color
                            : Color{static_cast<std::uint8_t>(grid), static_cast<std::uint8_t>(grid),
                                    static_cast<std::uint8_t>(grid), 255};
                    color.r = static_cast<std::uint8_t>(
                        (color.r * color.a + background.r * (255 - color.a) + 127) / 255);
                    color.g = static_cast<std::uint8_t>(
                        (color.g * color.a + background.g * (255 - color.a) + 127) / 255);
                    color.b = static_cast<std::uint8_t>(
                        (color.b * color.a + background.b * (255 - color.a) + 127) / 255);
                    color.a = 255;
                    if (inside && editor.document.atlas.active >= 0) {
                        const Color reference = editor.atlas_reference.get(sx, sy);
                        const double alpha = reference.a / 510.0;
                        color.r = static_cast<std::uint8_t>(
                            std::lround(color.r * (1 - alpha) + reference.r * alpha));
                        color.g = static_cast<std::uint8_t>(
                            std::lround(color.g * (1 - alpha) + reference.g * alpha));
                        color.b = static_cast<std::uint8_t>(
                            std::lround(color.b * (1 - alpha) + reference.b * alpha));
                        if (editor.document.atlas.kind == AtlasKind::Icon ||
                            editor.document.atlas.kind == AtlasKind::Cursor) {
                            const IconFrame& frame =
                                editor.document.atlas.icons[editor.document.atlas.active];
                            const std::size_t pixel = static_cast<std::size_t>(sy) * view_source_.width + sx;
                            if (frame.image.width == view_source_.width &&
                                frame.image.height == view_source_.height &&
                                pixel < frame.xor_pixels.size() && legacy_xor_pixel(frame, pixel) &&
                                equal(editor.document.image.get(sx, sy), {255, 255, 255, 0})) {
                                const Color mask = frame.xor_pixels[pixel];
                                color.r ^= mask.r;
                                color.g ^= mask.g;
                                color.b ^= mask.b;
                            }
                        }
                    }
                }
                pixels[index * 4] = static_cast<std::byte>(color.b * color.a / 255);
                pixels[index * 4 + 1] = static_cast<std::byte>(color.g * color.a / 255);
                pixels[index * 4 + 2] = static_cast<std::byte>(color.r * color.a / 255);
                pixels[index * 4 + 3] = static_cast<std::byte>(color.a);
            }
        }
        const gf::ImageLoadResult uploaded =
            rotated_.value
                ? (*window()).replace_bgra32_premultiplied(rotated_, width, height, width * 4, pixels, *this)
                : (*window()).load_bgra32_premultiplied(width, height, width * 4, pixels);
        if (!uploaded) {
            view_error = tr("Unable to allocate the rotated view.");
            return;
        }
        rotated_ = uploaded.image;
        rendered_generation_ = generation;
        rendered_revision_ = editor.canvas_revision;
        rendered_angle_ = view_angle;
        rendered_zoom_ = zoom();
        rendered_origin_ = origin;
        rendered_size_ = {static_cast<double>(width), static_cast<double>(height)};
    }
}
void PaintCanvas::paint_rotated(gf::Painter& painter, const Editor&) {
    // Native hosts synchronize image resources before entering paint. Publish
    // replacements from input/layout/worker delivery, never from this callback.
    const gf::Rect viewport = client_rectangle();
    if (rotated_.value) {
        painter.draw_image(rotated_, {0, 0, rendered_size_.width, rendered_size_.height});
    }
    stroke_outline(painter, {0, 0, view_source_.width, view_source_.height},
                   gf::Color::rgba(93, 111, 130, 155), 1);
    if (view_worker_.busy() || !view_error.empty()) {
        painter.draw_text_utf8({12, viewport.height - 14},
                               view_error.empty() ? tr("Preparing CONV view…") : view_error,
                               {gf::FontRole::control, 12, 400, false}, gf::Color::rgba(45, 63, 81));
    }
}

void PaintCanvas::set_transparency_colors(gf::Color first, gf::Color second) {
    if (transparency_first_ == first && transparency_second_ == second) { return; }
    viewport_generation_ = 0;
    transparency_first_ = first;
    transparency_second_ = second;
    invalidate(gf::Dirty::paint);
}
void PaintCanvas::paint_display(gf::Painter& painter) {
    if (!display_surface_) { return; }
    const gf::LiveSurfaceFrame frame = (*display_surface_).acquire_latest();
    if (!frame) { return; }
    const gf::Rect sheet = RasterCanvas::bitmap_to_client(
        {0, 0, static_cast<int>(frame.width()), static_cast<int>(frame.height())});
    const gf::Rect visible = gf::Rect::intersection(sheet, client_rectangle());
    if (visible.empty()) { return; }
    const double device_scale = window() ? (*window()).scale() : 1.0;
    const gf::Point window_origin = point_to_window({0, 0});
    const double sheet_x = (window_origin.x + sheet.x) * device_scale;
    const double sheet_y = (window_origin.y + sheet.y) * device_scale;
    if (frame.opaque() && std::abs(zoom() * device_scale - 1.0) < 1e-10 &&
        std::abs(sheet_x - std::round(sheet_x)) < 1e-10 &&
        std::abs(sheet_y - std::round(sheet_y)) < 1e-10) {
        // Native-order, pixel-aligned opaque artwork needs no viewport conversion.
        painter.save();
        painter.clip_rect(visible);
        painter.draw_live_surface(display_surface_, sheet);
        painter.restore();
        return;
    }
    const double left = std::ceil((window_origin.x + visible.x) * device_scale);
    const double top = std::ceil((window_origin.y + visible.y) * device_scale);
    const int width = static_cast<int>(std::floor((window_origin.x + visible.right()) * device_scale) - left);
    const int height = static_cast<int>(std::floor((window_origin.y + visible.bottom()) * device_scale) - top);
    if (width <= 0 || height <= 0) { return; }
    const gf::Rect destination{left / device_scale - window_origin.x, top / device_scale - window_origin.y,
        width / device_scale, height / device_scale};
    const gf::LiveSurfacePixelFormat format = gf::native_live_surface_pixel_format();
    if (!viewport_surface_ || viewport_generation_ != frame.generation() || viewport_source_ != display_surface_ ||
        viewport_bounds_ != destination || viewport_zoom_ != zoom() || viewport_origin_.x != view_origin().x ||
        viewport_origin_.y != view_origin().y || viewport_scale_ != device_scale) {
        if (!viewport_surface_ || (*viewport_surface_).snapshot().description.width != static_cast<unsigned>(width) ||
            (*viewport_surface_).snapshot().description.height != static_cast<unsigned>(height)) {
            viewport_surface_ = gf::LiveSurface::create({static_cast<unsigned>(width), static_cast<unsigned>(height),
                format, gf::default_live_surface_buffer_count, true});
        }
        gf::LiveSurfaceWriteLease write = (*viewport_surface_).try_acquire_write();
        if (!write) {
            viewport_surface_ = gf::LiveSurface::create({static_cast<unsigned>(width), static_cast<unsigned>(height),
                format, gf::default_live_surface_buffer_count, true});
            write = (*viewport_surface_).try_acquire_write();
        }
        const bool bgra = format == gf::LiveSurfacePixelFormat::bgra32_premultiplied_srgb;
        const double tile = transparency_cell_size() > 0 ? transparency_cell_size() : 12.0;
        for (int y = 0; y < height; ++y) {
            const double cy = destination.y + (y + 0.5) / device_scale;
            const int sy = std::clamp(static_cast<int>(std::floor(cy / zoom() + view_origin().y)),
                0, static_cast<int>(frame.height()) - 1);
            std::byte* row = write.pixels().data() + y * write.row_bytes();
            for (int x = 0; x < width; ++x) {
                const double cx = destination.x + (x + 0.5) / device_scale;
                const int sx = std::clamp(static_cast<int>(std::floor(cx / zoom() + view_origin().x)),
                    0, static_cast<int>(frame.width()) - 1);
                const std::byte* pixel = frame.pixels().data() + sy * frame.row_bytes() + sx * 4;
                const bool second = ((static_cast<int>((cx - visible.x) / tile) +
                                      static_cast<int>((cy - visible.y) / tile)) & 1) == 0;
                const gf::Color background = second ? transparency_second_ : transparency_first_;
                const unsigned inverse = 255U - std::to_integer<unsigned>(pixel[3]);
                row[x * 4] = static_cast<std::byte>(std::to_integer<unsigned>(pixel[0]) +
                    ((bgra ? background.blue : background.red) * inverse + 127U) / 255U);
                row[x * 4 + 1] = static_cast<std::byte>(std::to_integer<unsigned>(pixel[1]) +
                    (background.green * inverse + 127U) / 255U);
                row[x * 4 + 2] = static_cast<std::byte>(std::to_integer<unsigned>(pixel[2]) +
                    ((bgra ? background.red : background.blue) * inverse + 127U) / 255U);
                row[x * 4 + 3] = std::byte{255};
            }
        }
        static_cast<void>(write.publish());
        viewport_generation_ = frame.generation();
        viewport_source_ = display_surface_;
        viewport_bounds_ = destination;
        viewport_zoom_ = zoom();
        viewport_origin_ = view_origin();
        viewport_scale_ = device_scale;
    }
    painter.draw_live_surface(viewport_surface_, destination);
}
} // namespace paint::forms
