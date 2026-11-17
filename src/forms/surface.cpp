#include "localization.hpp"
#include "forms/editor.hpp"
#include "raster.hpp"
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
namespace paint::forms {
namespace gf = gui_forms;
Point PaintCanvas::visible_paste_origin(int width, int height) {
    const gf::Rect viewport = client_rectangle();
    if (width < 1 || height < 1 || viewport.width < 1 || viewport.height < 1) {
        return {};
    }
    const double margin = std::min({12.0, viewport.width / 4, viewport.height / 4});
    std::vector<gf::Point> polygon;
    const gui_drawing::PointF origin = view_origin.read();
    for (const Point corner : {Point{0, 0}, Point{static_cast<double>(width), 0},
                               Point{static_cast<double>(width), static_cast<double>(height)},
                               Point{0, static_cast<double>(height)}}) {
        const gui_drawing::PointF point = view_point({corner.x, corner.y});
        polygon.push_back({(point.x - origin.x) * zoom.read(), (point.y - origin.y) * zoom.read()});
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
            {center.x - viewport.width / (2 * zoom.read()), center.y - viewport.height / (2 * zoom.read())});
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
    const double screen_x = (projected.x - origin.x) * zoom.read();
    const double screen_y = (projected.y - origin.y) * zoom.read();
    if (screen_x < 0 || screen_x >= viewport.width || screen_y < 0 || screen_y >= viewport.height) {
        set_view_origin(
            {origin.x + (screen_x - std::clamp(screen_x, margin, viewport.width - margin)) / zoom.read(),
             origin.y + (screen_y - std::clamp(screen_y, margin, viewport.height - margin)) / zoom.read()});
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
gf::Point PaintCanvas::bitmap_point_to_client(gui_drawing::PointF point) const noexcept {
    // RasterCanvas::client_to_bitmap is (client - offset) / zoom, so the
    // client origin maps to -offset / zoom: the placed origin, which in a
    // binary view differs from view_origin by the snap (up to 2^-(23 -
    // reach) device pixels).
    const gui_drawing::PointF mapped = view_point(point);
    const gui_drawing::PointF placed = RasterCanvas::client_to_bitmap({0, 0});
    const double scale = zoom.read();
    const gf::Point client{(mapped.x - placed.x) * scale, (mapped.y - placed.y) * scale};
    return client;
}
gf::Rect PaintCanvas::bitmap_to_client(gui_drawing::RectI pixels) const noexcept {
    const gui_drawing::PointF origin = view_origin.read();
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
    return {(left - origin.x) * zoom.read(), (top - origin.y) * zoom.read(), (right - left) * zoom.read(),
            (bottom - top) * zoom.read()};
}
gf::Rect PaintCanvas::view_bounds() const noexcept {
    const gf::LiveSurfaceFrame image = display_surface_ ? (*display_surface_).acquire_latest() : gf::LiveSurfaceFrame{};
    if (!image) {
        return {};
    }
    const gf::Rect bounds =
        bitmap_to_client({0, 0, static_cast<int>(image.width()), static_cast<int>(image.height())});
    return {bounds.x / zoom.read() + view_origin.read().x, bounds.y / zoom.read() + view_origin.read().y, bounds.width / zoom.read(),
            bounds.height / zoom.read()};
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
        const gf::Point candidate{std::clamp((point.x - view_origin.read().x) * zoom.read() + offset.x, 14.0,
                                             std::max(14.0, viewport.width - 14)),
                                  std::clamp((point.y - view_origin.read().y) * zoom.read() + offset.y, 14.0,
                                             std::max(14.0, viewport.height - 14))};
        const gui_drawing::PointF document = client_to_bitmap(candidate);
        const double dx = std::max({-document.x, document.x - width, 0.0});
        const double dy = std::max({-document.y, document.y - height, 0.0});
        if (std::hypot(dx, dy) * zoom.read() >= 14) {
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
        points[i] = {(point.x - view_origin.read().x) * zoom.read(), (point.y - view_origin.read().y) * zoom.read()};
    }
    for (int i = 0; i < 4; ++i) {
        painter.draw_line(points[i], points[(i + 1) % 4], color, width);
    }
}
void PaintCanvas::on_attached_to_window() {
    RasterCanvas::on_attached_to_window();
    request_backing();
}
// One surround tile render: the material asked for, and the worker's result
// (the tile, or the error that ended it). The worker writes image and error
// before setting done; the UI thread reads them after joining.
struct BackingRenderJob {
    CanvasBacking backing = CanvasBacking::PaleFelt;
    Image image;
    std::string error;
    std::atomic<bool> cancel{false}, done{false};
};
namespace {
struct BackingDelivery {
    std::weak_ptr<PaintCanvas> canvas;
    void operator()() const {
        const std::shared_ptr<PaintCanvas> owner = canvas.lock();
        if (owner) {
            (*owner).deliver_backing();
        }
    }
};
// The worker: renders the tile and posts its delivery to the canvas's
// window. The canvas joins it before it detaches (stop_backing), so the
// window it posts to is live until the worker ends.
struct BackingWork {
    std::shared_ptr<BackingRenderJob> job;
    PaintCanvas* dispatcher = nullptr;
    std::weak_ptr<PaintCanvas> owner;
    void operator()() const {
        try {
            (*job).image = canvas_backing_texture((*job).backing, &(*job).cancel);
        } catch (const std::exception& error) {
            (*job).error = error.what();
        }
        (*job).done.store(true, std::memory_order_release);
        if (!(*job).cancel.load()) {
            static_cast<void>((*dispatcher).begin_invoke(BackingDelivery{owner}));
        }
    }
};
} // namespace
CanvasBacking PaintCanvas::chosen_backing() const noexcept {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const CanvasBacking choice = editor ? (*editor).settings.canvas_backing : CanvasBacking::PaleFelt;
    return choice;
}
bool PaintCanvas::backing_ready() const noexcept {
    const bool ready = backing_.value != 0 && loaded_backing_ == chosen_backing();
    return ready;
}
void PaintCanvas::stop_backing() noexcept {
    if (backing_job_) {
        (*backing_job_).cancel.store(true);
    }
    if (backing_worker_.joinable()) {
        backing_worker_.join();
    }
    backing_job_.reset();
}
void PaintCanvas::request_backing() {
    if (!attached_window()) {
        return;
    }
    const CanvasBacking choice = chosen_backing();
    if (backing_ready()) {
        // A render for an earlier choice is no longer wanted.
        stop_backing();
        return;
    }
    if (backing_job_ && (*backing_job_).backing == choice) {
        return;
    }
    stop_backing();
    backing_job_ = std::make_shared<BackingRenderJob>();
    (*backing_job_).backing = choice;
    backing_worker_ = std::thread(
        BackingWork{backing_job_, this, std::static_pointer_cast<PaintCanvas>(shared_from_this())});
}
void PaintCanvas::deliver_backing() {
    if (!attached_window() || !backing_job_ || !(*backing_job_).done.load(std::memory_order_acquire)) {
        return;
    }
    if (backing_worker_.joinable()) {
        backing_worker_.join();
    }
    const std::shared_ptr<BackingRenderJob> job = std::move(backing_job_);
    backing_job_.reset();
    if (!(*job).error.empty()) {
        // The plain colour stays; the next change of setting asks again.
        return;
    }
    if ((*job).backing != chosen_backing()) {
        request_backing();
        return;
    }
    const Image& texture = (*job).image;
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
        loaded_backing_ = (*job).backing;
    }
    invalidate(gf::Dirty::paint);
}
void PaintCanvas::on_detaching_from_window(gf::SurfaceRoot& former_window) noexcept {
    stop_backing();
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
    rendered_build_ = 0;
    repeated_ = {};
    reference_ = {};
    atlas_revision_ = 0;
    backing_ = {};
    loaded_backing_ = CanvasBacking::Count;
    display_request_.disconnect();
    settling_display_ = false;
    viewport_surface_.reset();
    viewport_source_.reset();
    viewport_generation_ = 0;
    RasterCanvas::on_detaching_from_window(former_window);
}
void PaintCanvas::on_dispose() noexcept {
    stop_backing();
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
    rendered_build_ = 0;
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
    // Everything below moves with the view, so a pan copies the painted
    // canvas (RasterCanvas::set_view). A turned view draws its last frame
    // moved by the pan until the next frame (paint_rotated); only its
    // status line keeps to the viewport, so while one shows, a pan repaints.
    const bool turned = std::abs(view_angle) > 1e-10;
    const bool status_line = turned && !view_error.empty();
    declare_paint_follows_view(!status_line);
    // Paint only reads what is ready: the chosen material's tile once the
    // worker delivered it (request_backing), its plain colour until then.
    const CanvasBacking choice = chosen_backing();
    if (backing_ready()) {
        // The felt's tiles are counted from the sheet's corner, so that the
        // surround moves with the sheet: the pattern starts whole tiles left
        // of and above the canvas and is clipped to it.
        const double tile = canvas_backing_tile_size(loaded_backing_);
        const double pixels = loaded_backing_ == CanvasBacking::PaleFelt ? 128 : 512;
        const gf::Rect corner = RasterCanvas::bitmap_to_client({0, 0, 1, 1});
        const double anchor_x = corner.x - tile * std::ceil((corner.x - bounds.x) / tile);
        const double anchor_y = corner.y - tile * std::ceil((corner.y - bounds.y) / tile);
        const gf::Rect pattern{anchor_x, anchor_y, bounds.right() - anchor_x, bounds.bottom() - anchor_y};
        painter.save();
        painter.clip_rect(bounds);
        painter.fill_image_pattern(backing_, {pixels, pixels}, pattern, {tile, tile});
        painter.restore();
    } else {
        const Color plain = canvas_backing_color(choice);
        painter.fill_rect(bounds, gf::Color::rgba(plain.r, plain.g, plain.b));
    }
    // The surround is opaque, so a pan may copy the canvas's pixels.
    report_opaque_cover(painter, bounds);
    if (editor && turned) {
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
double Editor::tool_preview_reach() const {
    if (document.curve.line_set || document.tool == Tool::Path || document.tool == Tool::Stamp) {
        return 0;
    }
    const double zoom = (*canvas_).zoom.read();
    double reach = 0;
    if (document.tool == Tool::Magnifier || document.tool == Tool::Picker) {
        // The 64-point lens, its shadow and its "8x" caption.
        reach = 64 + 10;
    } else if (document.tool == Tool::Eraser) {
        reach = std::max(0.5, document.ink.size * 0.5) * zoom + 3;
    } else if (document.tool == Tool::Pencil) {
        // The pixel under the cursor (turned with the view, up to its
        // diagonal away) and its three-point outline.
        reach = zoom * 1.5 + 4;
    } else if (document.tool == Tool::Brush && brush_family != BrushFamily::Heal) {
        reach = 2;
    }
    return reach;
}
void Editor::invalidate_tool_preview(std::optional<gf::Point> previous) {
    const double reach = tool_preview_reach();
    if (reach <= 0) {
        (*canvas_).invalidate(gf::Dirty::paint);
        return;
    }
    if (previous) {
        (*canvas_).invalidate(gf::Rect{(*previous).x - reach, (*previous).y - reach, reach * 2, reach * 2});
    }
    if (cursor_client_) {
        (*canvas_).invalidate(
            gf::Rect{(*cursor_client_).x - reach, (*cursor_client_).y - reach, reach * 2, reach * 2});
    }
}
void Editor::paint_tool_preview(gf::Painter& painter) {
    if (cursor_client_ && document.tool == Tool::Brush && brush_family == BrushFamily::Heal) {
        const gf::Point center = *cursor_client_;
        const double radius = std::max(2.0, document.ink.size * canvas().zoom.read() * 0.5);
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
    double scale = (*canvas_).zoom.read();
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
} // namespace
PaintCanvas::~PaintCanvas() {
    stop_backing();
}
bool PaintCanvas::view_busy() const {
    return display_request_.connected();
}
void PaintCanvas::build_view_surface() {
    view_error.clear();
    view_damage_ = {};
    ++surface_build_;
    ++work_statistics_.view_preparations;
    try {
        view_surface_.build(view_source_);
    } catch (const std::length_error& error) {
        view_error = error.what();
    } catch (const std::invalid_argument&) {
        // Under 2 x 2 pixels: the frame shows the nearest source pixel.
    } catch (const std::bad_alloc&) {
        view_error = tr("Unable to allocate the rotated view.");
    }
}
void PaintCanvas::publish_source(const Image& source, Rect damage) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !(*editor).settings.rotate_view || std::abs(view_angle) < 1e-10) {
        display_request_.disconnect();
        settling_display_ = false;
        if (rotated_.value && window()) {
            static_cast<void>((*window()).remove_image(rotated_));
        }
        rotated_ = {};
        view_source_ = {};
        view_surface_.clear();
        view_damage_ = {};
        ++surface_build_;
        return;
    }
    if (source.width != view_source_.width || source.height != view_source_.height) {
        // A new or resized picture (the view's first turn): the whole surface.
        view_source_ = source;
        build_view_surface();
    } else {
        // An edit: the surface recompiles only the cells that read a changed
        // pixel, at once, and the next frame resamples only the view over them.
        if (damage.w <= 0 || damage.h <= 0) {
            damage = {0, 0, source.width, source.height};
        }
        const int right = std::min(source.width, damage.x + damage.w),
                  bottom = std::min(source.height, damage.y + damage.h);
        int changed_left = source.width, changed_top = source.height, changed_right = -1, changed_bottom = -1;
        for (int y = std::max(0, damage.y); y < bottom; ++y) {
            for (int x = std::max(0, damage.x); x < right; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * source.width + x;
                if (!equal(source.pixels[i], view_source_.pixels[i])) {
                    view_source_.pixels[i] = source.pixels[i];
                    changed_left = std::min(changed_left, x);
                    changed_top = std::min(changed_top, y);
                    changed_right = std::max(changed_right, x);
                    changed_bottom = std::max(changed_bottom, y);
                }
            }
        }
        if (changed_right >= 0) {
            const Rect changed{changed_left, changed_top, changed_right - changed_left + 1,
                               changed_bottom - changed_top + 1};
            if (view_surface_.ready()) {
                view_surface_.update(view_source_, changed);
            }
            if (view_damage_.w > 0 && view_damage_.h > 0) {
                const int left = std::min(view_damage_.x, changed.x), top = std::min(view_damage_.y, changed.y);
                const int union_right = std::max(view_damage_.x + view_damage_.w, changed.x + changed.w);
                const int union_bottom = std::max(view_damage_.y + view_damage_.h, changed.y + changed.h);
                view_damage_ = {left, top, union_right - left, union_bottom - top};
            } else {
                view_damage_ = changed;
            }
        }
    }
    prepare_display();
}
void PaintCanvas::apply_view(double scale, gui_drawing::PointF origin) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const bool handle = editor && (*editor).settings.rotate_view;
    const gf::Point handle_before = handle ? rotation_handle() : gf::Point{};
    const gf::Point anchor_before = view_anchor();
    RasterCanvas::set_view(scale, origin);
    if (editor && (*editor).text.active) {
        // The canvas text's editor follows the box on screen.
        (*editor).sync_canvas_text();
    }
    if (!handle) {
        return;
    }
    // The handle (Editor::paint_canvas_overlay) is clamped to the
    // viewport, so it does not follow the view that the scroll copy moves:
    // damage it where the copy put its old pixels and where it is now
    // (Control::scroll_content leaves stationary paint to the caller). Its
    // face is 24 units round with a one-unit outline.
    constexpr double reach = 14;
    const gf::Point anchor_after = view_anchor();
    const gf::Point copied{handle_before.x + (anchor_after.x - anchor_before.x),
                           handle_before.y + (anchor_after.y - anchor_before.y)};
    const gf::Point handle_after = rotation_handle();
    invalidate(gf::Rect{copied.x - reach, copied.y - reach, 2 * reach, 2 * reach});
    invalidate(gf::Rect{handle_after.x - reach, handle_after.y - reach, 2 * reach, 2 * reach});
}
void PaintCanvas::set_zoom(double scale) {
    apply_view(scale, view_origin.read());
    prepare_display();
}
void PaintCanvas::set_view_origin(gui_drawing::PointF origin) {
    apply_view(zoom.read(), origin);
    prepare_display();
}
void PaintCanvas::set_view(double scale, gui_drawing::PointF origin) {
    apply_view(scale, origin);
    prepare_display();
}
void PaintCanvas::arrange(gf::Rect bounds) {
    RasterCanvas::arrange(bounds);
    request_backing();
    prepare_display();
}
void PaintCanvas::prepare_display() {
    if (!window() || std::abs(view_angle) < 1e-10) {
        return;
    }
    // The first image must exist before native paint can synchronize resources.
    if (!rotated_.value && !view_source_.pixels.empty()) {
        render_display(false);
        next_display_ = gf::FrameClock::now() + std::chrono::milliseconds(16);
    }
    if (settling_display_) {
        // A change of view comes before the rest render: the next frame.
        display_request_.disconnect();
        settling_display_ = false;
    }
    if (!display_request_.connected()) {
        display_request_ = (*window()).schedule_ui_timer(
            *this, std::chrono::milliseconds(16), std::max(gf::FrameClock::now(), next_display_),
            DisplayDeadline{std::static_pointer_cast<PaintCanvas>(shared_from_this())});
    }
}
void PaintCanvas::display_frame(gf::FrameTime now) {
    display_request_.disconnect();
    const bool settle = settling_display_;
    settling_display_ = false;
    next_display_ = now + std::chrono::milliseconds(16);
    render_display(settle);
    const std::shared_ptr<Editor> owner = editor_.lock();
    if (owner) {
        (*owner).render_stamp_view();
    }
}
void PaintCanvas::render_display(bool exact) {
    const std::shared_ptr<Editor> owner = editor_.lock();
    if (!owner || !window() || std::abs(view_angle) < 1e-10 || view_source_.pixels.empty()) {
        return;
    }
    const Editor& editor = *owner;
    const gf::Rect viewport = client_rectangle();
    const gui_drawing::PointF origin = view_origin.read();
    const double scale = zoom.read();
    const int width = std::max(1, static_cast<int>(std::ceil(viewport.width)));
    const int height = std::max(1, static_cast<int>(std::ceil(viewport.height)));
    const std::size_t frame_pixels = static_cast<std::size_t>(width) * height;
    // The frame's CONV samples are kept apart from its composited pixels: an
    // edit resamples only the view over the pixels it changed, and a change
    // of the canvas's other content (its revision) composites again.
    const bool same_view = rotated_.value && rendered_build_ == surface_build_ && rendered_angle_ == view_angle &&
                           rendered_zoom_ == scale && rendered_size_.width == width &&
                           rendered_size_.height == height && rotated_pixels_.size() == frame_pixels * 4 &&
                           view_samples_.size() == frame_pixels;
    const bool damaged = view_damage_.w > 0 && view_damage_.h > 0;
    const bool recomposite = rendered_revision_ != editor.canvas_revision;
    if (same_view && !damaged && !recomposite && rendered_origin_.x == origin.x && rendered_origin_.y == origin.y) {
        return;
    }
    const gf::Point anchor = view_anchor();
    const bool same_size = rotated_.value && rendered_size_.width == width && rendered_size_.height == height;
    // A pan alone moves the frame by the whole units nearest to it: the kept
    // pixels keep their samples (the frame's origin moves by those units),
    // so they are exactly what a whole render on that origin would write,
    // and only the exposed bands are rendered. A pan by a fraction of a unit
    // (points on a scaled display, a trackpad) leaves the frame that
    // fraction (residual) from the view; paint_rotated draws it moved by as
    // much, and once the view rests an exact render replaces it. Anything
    // else renders the whole frame.
    const double sample_x = (rendered_origin_.x - origin.x) * scale;
    const double sample_y = (rendered_origin_.y - origin.y) * scale;
    const double shift_x = std::round(sample_x), shift_y = std::round(sample_y);
    constexpr double tolerance = 1e-6;
    const bool whole = std::abs(sample_x - shift_x) < tolerance && std::abs(sample_y - shift_y) < tolerance;
    const bool frame_shift =
        same_view && (whole || !exact) && std::abs(shift_x) < width && std::abs(shift_y) < height;
    gf::Point residual{0, 0};
    bool moved = true;
    if (frame_shift) {
        if (whole) {
            rendered_origin_ = origin;
        } else {
            rendered_origin_ = {rendered_origin_.x - shift_x / scale, rendered_origin_.y - shift_y / scale};
            residual = {(rendered_origin_.x - origin.x) * scale, (rendered_origin_.y - origin.y) * scale};
        }
        moved = shift_x != 0 || shift_y != 0;
        if (moved) {
            shift_rotated_frame(editor, static_cast<int>(shift_x), static_cast<int>(shift_y));
        }
        if (recomposite) {
            composite_rotated_area(editor, 0, 0, width, height);
            moved = true;
        }
    } else {
        ++work_statistics_.view_renders;
        rendered_zoom_ = scale;
        rendered_angle_ = view_angle;
        rendered_origin_ = origin;
        rendered_size_ = {static_cast<double>(width), static_cast<double>(height)};
        rendered_build_ = surface_build_;
        rotated_pixels_.assign(frame_pixels * 4, std::byte{0});
        view_samples_.assign(frame_pixels, Color{0, 0, 0, 0});
        view_damage_ = {};
        sample_rotated_area(0, 0, width, height);
        composite_rotated_area(editor, 0, 0, width, height);
    }
    if (view_damage_.w > 0 && view_damage_.h > 0) {
        // The frame pixels whose samples can read an edited pixel: the
        // edit's box widened by the surface's reach (four pixels), mapped
        // to the frame and rounded out.
        const double c = std::cos(view_angle), s = std::sin(view_angle);
        double left = 1e300, top = 1e300, right = -1e300, bottom = -1e300;
        for (int corner = 0; corner < 4; ++corner) {
            const double px = (corner & 1) ? view_damage_.x + view_damage_.w + 4.0 : view_damage_.x - 4.0;
            const double py = (corner & 2) ? view_damage_.y + view_damage_.h + 4.0 : view_damage_.y - 4.0;
            const double fx = (c * px - s * py - rendered_origin_.x) * scale;
            const double fy = (s * px + c * py - rendered_origin_.y) * scale;
            left = std::min(left, fx);
            right = std::max(right, fx);
            top = std::min(top, fy);
            bottom = std::max(bottom, fy);
        }
        const int area_left = std::max(0, static_cast<int>(std::floor(left)) - 1);
        const int area_top = std::max(0, static_cast<int>(std::floor(top)) - 1);
        const int area_right = std::min(width, static_cast<int>(std::ceil(right)) + 1);
        const int area_bottom = std::min(height, static_cast<int>(std::ceil(bottom)) + 1);
        if (area_right > area_left && area_bottom > area_top) {
            sample_rotated_area(area_left, area_top, area_right - area_left, area_bottom - area_top);
            composite_rotated_area(editor, area_left, area_top, area_right - area_left, area_bottom - area_top);
            moved = true;
        }
        view_damage_ = {};
    }
    if (moved) {
        // A frame of the same size is written in place (a live update keeps
        // the image and its native texture); replacing it re-registered and
        // re-hashed the whole frame on every pan step.
        gf::ImageLoadResult uploaded = same_size ? (*window()).update_bgra32_premultiplied(
                                                       rotated_, width, height, width * 4, rotated_pixels_, *this)
                                                 : gf::ImageLoadResult{};
        if (!uploaded) {
            uploaded = rotated_.value ? (*window()).replace_bgra32_premultiplied(rotated_, width, height, width * 4,
                                                                                 rotated_pixels_, *this)
                                      : (*window()).load_bgra32_premultiplied(width, height, width * 4,
                                                                              rotated_pixels_);
        }
        if (!uploaded) {
            // The next render starts afresh: the kept pixels no longer match an
            // uploaded frame.
            rendered_build_ = 0;
            view_error = tr("Unable to allocate the rotated view.");
            return;
        }
        rotated_ = uploaded.image;
    }
    rendered_revision_ = editor.canvas_revision;
    rendered_anchor_ = {anchor.x - residual.x, anchor.y - residual.y};
    if ((residual.x != 0 || residual.y != 0) && !display_request_.connected()) {
        // The rest render: exact samples once the view stops moving.
        settling_display_ = true;
        display_request_ = (*window()).schedule_ui_timer(
            *this, std::chrono::milliseconds(120), gf::FrameClock::now() + std::chrono::milliseconds(120),
            DisplayDeadline{std::static_pointer_cast<PaintCanvas>(shared_from_this())});
    }
}
void PaintCanvas::shift_rotated_frame(const Editor& editor, int shift_x, int shift_y) {
    const int width = static_cast<int>(rendered_size_.width), height = static_cast<int>(rendered_size_.height);
    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    // The kept columns: source [kept_left, kept_left + kept_columns) lands
    // at kept_left + shift_x. Rows are moved in the order that never reads
    // a row already overwritten. The samples (one Color, four bytes, per
    // pixel) move with the composited pixels.
    const int kept_left = std::max(0, -shift_x);
    const int kept_columns = width - std::abs(shift_x);
    const int kept_rows = height - std::abs(shift_y);
    const std::size_t kept_bytes = static_cast<std::size_t>(kept_columns) * 4;
    std::byte* const samples = reinterpret_cast<std::byte*>(view_samples_.data());
    for (int step = 0; step < kept_rows; ++step) {
        const int source_row = shift_y > 0 ? kept_rows - 1 - step : std::max(0, -shift_y) + step;
        const int target_row = source_row + shift_y;
        const std::size_t target = static_cast<std::size_t>(target_row) * stride +
                                   static_cast<std::size_t>(kept_left + shift_x) * 4;
        const std::size_t source = static_cast<std::size_t>(source_row) * stride + static_cast<std::size_t>(kept_left) * 4;
        std::memmove(rotated_pixels_.data() + target, rotated_pixels_.data() + source, kept_bytes);
        std::memmove(samples + target, samples + source, kept_bytes);
    }
    // The bands below are rendered on the frame's origin (render_display
    // moved it with the shift).
    const int band_rows = std::abs(shift_y);
    const int band_top = shift_y > 0 ? 0 : height - band_rows;
    const int rest_top = shift_y > 0 ? band_rows : 0;
    const int band_columns = std::abs(shift_x);
    const int band_left = shift_x > 0 ? 0 : width - band_columns;
    if (band_rows > 0) {
        sample_rotated_area(0, band_top, width, band_rows);
        composite_rotated_area(editor, 0, band_top, width, band_rows);
    }
    if (band_columns > 0 && kept_rows > 0) {
        sample_rotated_area(band_left, rest_top, band_columns, kept_rows);
        composite_rotated_area(editor, band_left, rest_top, band_columns, kept_rows);
    }
    ++work_statistics_.view_shifts;
    work_statistics_.view_shift_pixels += static_cast<std::uint64_t>(band_rows) * width +
                                          static_cast<std::uint64_t>(band_columns) * kept_rows;
}
void PaintCanvas::sample_rotated_area(int left, int top, int columns, int rows) {
    const int width = static_cast<int>(rendered_size_.width);
    const gui_drawing::PointF origin = rendered_origin_;
    const double c = std::cos(view_angle), s = std::sin(view_angle), scale = zoom.read();
    Color* const target = view_samples_.data() + static_cast<std::size_t>(top) * width + left;
    work_statistics_.view_resampled_pixels += static_cast<std::uint64_t>(columns) * rows;
    if (view_surface_.ready()) {
        // Frame pixel centre (x + 0.5, y + 0.5) is the view point
        // ((x + 0.5) / zoom + origin); turned back it is a canvas point, whose
        // pixel-centre coordinates are half a unit less.
        const CanvasSurface::TargetMap map{c / scale,  s / scale, c * origin.x + s * origin.y - 0.5,
                                           -s / scale, c / scale, -s * origin.x + c * origin.y - 0.5};
        view_surface_.render(map, left, top, columns, rows, target, static_cast<std::size_t>(width));
        return;
    }
    // No surface (a picture under 2 x 2 pixels, or over the surface's
    // limit): the nearest source pixel.
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const double vx = (left + x + 0.5) / scale + origin.x;
            const double vy = (top + y + 0.5) / scale + origin.y;
            const int sx = static_cast<int>(std::floor(c * vx + s * vy)),
                      sy = static_cast<int>(std::floor(-s * vx + c * vy));
            target[static_cast<std::size_t>(y) * width + x] =
                view_source_.contains(sx, sy) ? view_source_.get(sx, sy) : Color{0, 0, 0, 0};
        }
    }
}
void PaintCanvas::composite_rotated_area(const Editor& editor, int left, int top, int columns, int rows) {
    const int width = static_cast<int>(rendered_size_.width);
    const gui_drawing::PointF origin = rendered_origin_;
    const double c = std::cos(view_angle), s = std::sin(view_angle), scale = zoom.read();
    std::byte* const pixels = rotated_pixels_.data();
    for (int y = top; y < top + rows; ++y) {
        for (int x = left; x < left + columns; ++x) {
            const double vx = (x + 0.5) / scale + origin.x;
            const double vy = (y + 0.5) / scale + origin.y;
            const gui_drawing::PointF point{c * vx + s * vy, -s * vx + c * vy};
            const int sx = static_cast<int>(std::floor(point.x)),
                      sy = static_cast<int>(std::floor(point.y));
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            Color color = view_samples_[index];
            const bool inside = view_source_.contains(sx, sy);
            const bool wrapped = !inside && editor.atlas_wrap && editor.document.atlas.active >= 0 &&
                                 sx >= -view_source_.width && sx < 2 * view_source_.width &&
                                 sy >= -view_source_.height && sy < 2 * view_source_.height;
            if (inside || wrapped) {
                if (wrapped) {
                    color = editor.document.image.get(
                        (sx % view_source_.width + view_source_.width) % view_source_.width,
                        (sy % view_source_.height + view_source_.height) % view_source_.height);
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
}
gf::Point PaintCanvas::view_anchor() const noexcept {
    const gf::Rect corner = RasterCanvas::bitmap_to_client({0, 0, 1, 1});
    const gf::Point anchor{corner.x, corner.y};
    return anchor;
}
void PaintCanvas::paint_rotated(gf::Painter& painter, const Editor&) {
    // Native hosts synchronize image resources before entering paint. Publish
    // replacements from input/layout/worker delivery, never from this callback.
    const gf::Rect viewport = client_rectangle();
    if (rotated_.value) {
        // The frame was rendered with the bitmap's origin at
        // rendered_anchor_; a pan since moved the painted canvas by the
        // anchor's change, and the frame is drawn moved by as much.
        const gf::Point anchor = view_anchor();
        const gf::Rect frame{anchor.x - rendered_anchor_.x, anchor.y - rendered_anchor_.y, rendered_size_.width,
                             rendered_size_.height};
        painter.draw_image(rotated_, frame);
    }
    stroke_outline(painter, {0, 0, view_source_.width, view_source_.height},
                   gf::Color::rgba(93, 111, 130, 155), 1);
    if (!view_error.empty()) {
        painter.draw_text_utf8({12, viewport.height - 14}, view_error, {gf::FontRole::control, 12, 400, false},
                               gf::Color::rgba(45, 63, 81));
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
    if (frame.opaque() && std::abs(zoom.read() * device_scale - 1.0) < 1e-10 &&
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
    const unsigned surface_width = viewport_surface_ ? (*viewport_surface_).snapshot().description.width : 0U;
    const unsigned surface_height = viewport_surface_ ? (*viewport_surface_).snapshot().description.height : 0U;
    // The same view as the last conversion: only the picture changed. A
    // generation of zero is a forced redo (the checker colours changed).
    const bool same_view = viewport_surface_ && viewport_generation_ != 0 && viewport_source_ == display_surface_ &&
        viewport_bounds_ == destination && viewport_zoom_ == zoom.read() && viewport_origin_.x == view_origin.read().x &&
        viewport_origin_.y == view_origin.read().y && viewport_scale_ == device_scale &&
        surface_width == static_cast<unsigned>(width) && surface_height == static_cast<unsigned>(height) &&
        viewport_columns_.size() == static_cast<std::size_t>(width);
    if (!same_view || viewport_generation_ != frame.generation()) {
        const bool bgra = format == gf::LiveSurfacePixelFormat::bgra32_premultiplied_srgb;
        const double tile = transparency_cell_size.read() > 0 ? transparency_cell_size.read() : 12.0;
        const double scale = zoom.read();
        const int source_width = static_cast<int>(frame.width());
        const int source_height = static_cast<int>(frame.height());
        // Only the picture changed: convert just the output pixels showing
        // the source pixels published since the last conversion, over the
        // previous contents, and publish that rectangle alone.
        bool partial = same_view && viewport_dirty_.w > 0 && viewport_dirty_.h > 0;
        gf::LiveSurfaceWriteLease write;
        if (partial) {
            write = (*viewport_surface_).try_acquire_write(true);
            partial = static_cast<bool>(write);
        }
        // A pan alone (the same picture, zoom, scale and output rectangle)
        // by whole device pixels: the kept pixels move by the pan and only
        // the band it exposed is converted. Sampling and checker cells are
        // counted from the sheet's corner, so the moved pixels equal a fresh
        // conversion.
        const double shift_x = (sheet.x - viewport_sheet_.x) * device_scale;
        const double shift_y = (sheet.y - viewport_sheet_.y) * device_scale;
        bool moved = !partial && viewport_surface_ && viewport_generation_ != 0 &&
            viewport_generation_ == frame.generation() && viewport_source_ == display_surface_ &&
            viewport_bounds_ == destination && viewport_zoom_ == scale && viewport_scale_ == device_scale &&
            surface_width == static_cast<unsigned>(width) && surface_height == static_cast<unsigned>(height) &&
            viewport_columns_.size() == static_cast<std::size_t>(width) && shift_x == std::round(shift_x) &&
            shift_y == std::round(shift_y) && std::abs(shift_x) < width && std::abs(shift_y) < height;
        if (moved) {
            write = (*viewport_surface_).try_acquire_write(true);
            moved = static_cast<bool>(write);
        }
        const int move_x = moved ? static_cast<int>(shift_x) : 0;
        const int move_y = moved ? static_cast<int>(shift_y) : 0;
        if (moved) {
            // Rows are moved in the order that never overwrites a row still
            // to move; within a row, memmove handles the overlap.
            const std::size_t row_bytes = write.row_bytes();
            const std::size_t kept_bytes = static_cast<std::size_t>(width - std::abs(move_x)) * 4;
            const std::size_t to_column = static_cast<std::size_t>(std::max(0, move_x)) * 4;
            const std::size_t from_column = static_cast<std::size_t>(std::max(0, -move_x)) * 4;
            std::byte* const pixels = write.pixels().data();
            if (move_y > 0) {
                for (int y = height - 1; y >= move_y; --y) {
                    std::memmove(pixels + static_cast<std::size_t>(y) * row_bytes + to_column,
                                 pixels + static_cast<std::size_t>(y - move_y) * row_bytes + from_column, kept_bytes);
                }
            } else {
                for (int y = 0; y < height + move_y; ++y) {
                    std::memmove(pixels + static_cast<std::size_t>(y) * row_bytes + to_column,
                                 pixels + static_cast<std::size_t>(y - move_y) * row_bytes + from_column, kept_bytes);
                }
            }
        }
        if (!partial) {
            if (!moved) {
                if (!viewport_surface_ || surface_width != static_cast<unsigned>(width) ||
                    surface_height != static_cast<unsigned>(height)) {
                    viewport_surface_ = gf::LiveSurface::create({static_cast<unsigned>(width),
                        static_cast<unsigned>(height), format, gf::default_live_surface_buffer_count, true});
                }
                write = (*viewport_surface_).try_acquire_write();
                if (!write) {
                    viewport_surface_ = gf::LiveSurface::create({static_cast<unsigned>(width),
                        static_cast<unsigned>(height), format, gf::default_live_surface_buffer_count, true});
                    write = (*viewport_surface_).try_acquire_write();
                }
            }
            // Each output column samples one source column and lies in one
            // checker column; both are worked out once per view, not per
            // pixel (a pan at device scale 2 converts some three million pixels).
            viewport_columns_.resize(static_cast<std::size_t>(width));
            viewport_cells_.resize(static_cast<std::size_t>(width));
            for (int x = 0; x < width; ++x) {
                const double cx = destination.x + (x + 0.5) / device_scale;
                const int sx = std::clamp(static_cast<int>(std::floor((cx - sheet.x) / scale)), 0, source_width - 1);
                viewport_columns_[static_cast<std::size_t>(x)] = static_cast<std::size_t>(sx) * 4;
                viewport_cells_[static_cast<std::size_t>(x)] = static_cast<int>((cx - sheet.x) / tile);
            }
        }
        // After a moved pan: the exposed columns, converted on every row,
        // and the exposed rows, converted whole.
        const int band_begin = move_x > 0 ? 0 : width + move_x;
        const int band_end = move_x > 0 ? move_x : width;
        // The output columns and rows to convert: all, or those whose source
        // column and row lie in the published damage (the tables are monotonic).
        int column_begin = 0;
        int column_end = width;
        if (partial) {
            const std::size_t dirty_left = static_cast<std::size_t>(std::max(0, viewport_dirty_.x)) * 4;
            const std::size_t dirty_right =
                static_cast<std::size_t>(std::max(0, viewport_dirty_.x + viewport_dirty_.w)) * 4;
            column_begin = width;
            column_end = 0;
            for (int x = 0; x < width; ++x) {
                const std::size_t offset = viewport_columns_[static_cast<std::size_t>(x)];
                if (offset >= dirty_left && offset < dirty_right) {
                    column_begin = std::min(column_begin, x);
                    column_end = x + 1;
                }
            }
        }
        // The two checker colours in output byte order (blue first for BGRA).
        const unsigned first[3] = {bgra ? transparency_first_.blue : transparency_first_.red, transparency_first_.green,
                                   bgra ? transparency_first_.red : transparency_first_.blue};
        const unsigned second_colour[3] = {bgra ? transparency_second_.blue : transparency_second_.red,
                                           transparency_second_.green,
                                           bgra ? transparency_second_.red : transparency_second_.blue};
        int row_begin = height;
        int row_end = 0;
        std::uint64_t converted = 0;
        for (int y = 0; y < height && column_begin < column_end; ++y) {
            const double cy = destination.y + (y + 0.5) / device_scale;
            const int sy = std::clamp(static_cast<int>(std::floor((cy - sheet.y) / scale)), 0, source_height - 1);
            if (partial && (sy < viewport_dirty_.y || sy >= viewport_dirty_.y + viewport_dirty_.h)) {
                continue;
            }
            int row_first = column_begin;
            int row_last = column_end;
            if (moved) {
                const bool exposed_row = move_y > 0 ? y < move_y : y >= height + move_y;
                if (!exposed_row) {
                    row_first = band_begin;
                    row_last = band_end;
                }
                if (row_first >= row_last) {
                    continue;
                }
            }
            row_begin = std::min(row_begin, y);
            row_end = y + 1;
            converted += static_cast<std::uint64_t>(row_last - row_first);
            const int row_cell = static_cast<int>((cy - sheet.y) / tile);
            const std::byte* source_row = frame.pixels().data() + static_cast<std::size_t>(sy) * frame.row_bytes();
            std::byte* row = write.pixels().data() + static_cast<std::size_t>(y) * write.row_bytes();
            for (int x = row_first; x < row_last; ++x) {
                const std::byte* pixel = source_row + viewport_columns_[static_cast<std::size_t>(x)];
                const bool second = ((viewport_cells_[static_cast<std::size_t>(x)] + row_cell) & 1) == 0;
                const unsigned* background = second ? second_colour : first;
                const unsigned inverse = 255U - std::to_integer<unsigned>(pixel[3]);
                row[x * 4] = static_cast<std::byte>(std::to_integer<unsigned>(pixel[0]) +
                    (background[0] * inverse + 127U) / 255U);
                row[x * 4 + 1] = static_cast<std::byte>(std::to_integer<unsigned>(pixel[1]) +
                    (background[1] * inverse + 127U) / 255U);
                row[x * 4 + 2] = static_cast<std::byte>(std::to_integer<unsigned>(pixel[2]) +
                    (background[2] * inverse + 127U) / 255U);
                row[x * 4 + 3] = std::byte{255};
            }
        }
        if (!partial) {
            static_cast<void>(write.publish());
        } else if (row_begin < row_end) {
            static_cast<void>(write.publish({static_cast<double>(column_begin), static_cast<double>(row_begin),
                static_cast<double>(column_end - column_begin), static_cast<double>(row_end - row_begin)}));
        }
        ++work_statistics_.viewport_conversions;
        work_statistics_.viewport_converted_pixels += converted;
        viewport_dirty_ = {};
        viewport_generation_ = frame.generation();
        viewport_source_ = display_surface_;
        viewport_bounds_ = destination;
        viewport_zoom_ = zoom.read();
        viewport_origin_ = view_origin.read();
        viewport_scale_ = device_scale;
        viewport_sheet_ = {sheet.x, sheet.y};
    }
    painter.draw_live_surface(viewport_surface_, destination);
}
} // namespace paint::forms
