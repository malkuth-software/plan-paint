#include "localization.hpp"
#include "conv.hpp"
#include "forms/editor.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
struct WarpDelivery {
    std::weak_ptr<Editor> editor;
    void operator()() const {
        std::shared_ptr<Editor> owner = editor.lock();
        if (owner) {
            (*owner).poll_warp();
        }
    }
};
struct WarpCompletion {
    Editor* editor;
    std::weak_ptr<Editor> owner;
    void operator()() const {
        // Editor joins its worker before destroying the Control dispatcher.
        // The queued callback acquires ownership only on the UI thread.
        static_cast<void>((*editor).begin_invoke(WarpDelivery{owner}));
    }
};
struct PreviewDelivery {
    std::weak_ptr<Editor> editor;
    void operator()() const {
        const std::shared_ptr<Editor> owner = editor.lock();
        if (owner) {
            (*owner).poll_transform_preview();
        }
    }
};
struct PreviewCompletion {
    Editor* editor;
    std::weak_ptr<Editor> owner;
    void operator()() const {
        static_cast<void>((*editor).begin_invoke(PreviewDelivery{owner}));
    }
};
static Rect mesh_bounds(const ReshapeMesh& mesh) {
    double left = std::numeric_limits<double>::infinity(), top = left;
    double right = -left, bottom = -left;
    for (const MeshNode& node : mesh.nodes) {
        left = std::min(left, node.target.x);
        right = std::max(right, node.target.x);
        top = std::min(top, node.target.y);
        bottom = std::max(bottom, node.target.y);
    }
    int x = static_cast<int>(std::floor(left - 0.5));
    int y = static_cast<int>(std::floor(top - 0.5));
    Rect bounds{x, y, std::max(1, static_cast<int>(std::ceil(right + 0.5)) - x + 1),
                std::max(1, static_cast<int>(std::ceil(bottom + 0.5)) - y + 1)};
    return bounds;
}
static double segment_distance(Point point, Point first, Point last) {
    double dx = last.x - first.x, dy = last.y - first.y;
    double squared = dx * dx + dy * dy;
    double t = squared > 0 ? ((point.x - first.x) * dx + (point.y - first.y) * dy) / squared : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(point.x - first.x - t * dx, point.y - first.y - t * dy);
}
static void simplify_segment(const std::vector<Point>& input, std::size_t first, std::size_t last,
                             double tolerance, std::vector<Point>& output) {
    if (last <= first + 1) {
        output.push_back(input[first]);
        return;
    }
    double farthest = 0;
    std::size_t split = first;
    for (std::size_t i = first + 1; i < last; ++i) {
        double distance = segment_distance(input[i], input[first], input[last]);
        if (distance > farthest) {
            farthest = distance;
            split = i;
        }
    }
    if (farthest > tolerance) {
        simplify_segment(input, first, split, tolerance, output);
        simplify_segment(input, split, last, tolerance, output);
    } else {
        output.push_back(input[first]);
    }
}
} // namespace
Editor::~Editor() {
    if (recovery_job.valid()) { recovery_job.wait(); }
    transform_preview_worker_.cancel();
    transform_preview_worker_.wait();
    warp_worker_.wait();
}
void Editor::initialize_warp() {
    warp_worker_.set_completion(WarpCompletion{this, std::static_pointer_cast<Editor>(shared_from_this())});
    transform_preview_worker_.set_completion(
        PreviewCompletion{this, std::static_pointer_cast<Editor>(shared_from_this())});
}
bool Editor::background_busy() const {
    return warp_worker_.busy() || stamp_pending_ || transform_preview_worker_.busy();
}
bool Editor::warp_active() const {
    return warp_mode_ != WarpMode::none;
}
void Editor::cancel_warp() {
    if (warp_active()) {
        document.selection = std::move(warp_original_);
    }
    warp_mode_ = WarpMode::none;
    warp_coverage_.clear();
    warp_field_.reset();
    reshape_mesh_ = {};
    warp_pending_ = warp_commit_ = rotation_dragging_ = warp_whole_image_ = false;
    mesh_node_ = -1;
    ++warp_generation_;
    release_gesture();
}
void Editor::start_reshape() {
    finish_text(true);
    finish_warp(false);
    document.commit_path();
    document.commit_curve();
    if (!document.selection.active) {
        throw std::runtime_error(tr("Select or lasso an object before reshaping it."));
    }
    warp_worker_.wait();
    poll_warp();
    if (warp_worker_.busy()) {
        throw std::runtime_error(tr("The stamp is still preparing. Try reshape after it finishes."));
    }
    document.lift_selection();
    std::vector<Point> outline = document.selection.outline;
    if (outline.size() >= 3) {
        if (std::hypot(outline.back().x - outline.front().x, outline.back().y - outline.front().y) > 0.1) {
            outline.push_back(outline.front());
        }
        std::vector<Point> simplified;
        simplify_segment(outline, 0, outline.size() - 1, std::max(1.0, mesh_spacing * 0.08), simplified);
        if (simplified.size() >= 3) {
            outline = std::move(simplified);
        }
        reshape_mesh_ = make_reshape_mesh(outline, mesh_spacing);
    } else {
        reshape_mesh_ = make_reshape_mesh(document.selection.image, mesh_spacing);
    }
    warp_original_ = document.selection;
    warp_mode_ = WarpMode::mesh;
    warp_field_.reset();
    warp_pending_ = warp_commit_ = false;
    ++warp_generation_;
    warp_worker_.compile(WarpTask::CompileSelection, warp_original_.image, warp_generation_);
    (*ribbon_).show_transforms();
}
AffineMap Editor::rotation_map() const {
    double radians = rotation_angle * std::numbers::pi / 180;
    double cosine = std::cos(radians), sine = std::sin(radians);
    double x = (warp_original_.image.width - 1) * 0.5;
    double y = (warp_original_.image.height - 1) * 0.5;
    return {cosine, -sine, x - cosine * x + sine * y, sine, cosine, y - sine * x - cosine * y};
}
void Editor::start_rotation() {
    if (!document.selection.active || warp_active() || warp_worker_.busy()) {
        throw std::runtime_error(tr("Select an object and finish the current transform before rotating."));
    }
    document.lift_selection();
    warp_original_ = document.selection;
    warp_mode_ = WarpMode::rotation;
    warp_field_.reset();
    rotation_angle = 0;
    warp_pending_ = warp_commit_ = warp_whole_image_ = false;
    ++warp_generation_;
    warp_worker_.compile(WarpTask::CompileRotation, warp_original_.image, warp_generation_);
    refresh();
}
void Editor::request_rotation(double degrees) {
    if (!std::isfinite(degrees)) {
        throw std::runtime_error(tr("Enter a finite rotation angle."));
    }
    finish_text(true);
    finish_warp(false);
    document.commit_curve();
    document.commit_path();
    bool whole_image = !document.selection.active;
    if (whole_image) {
        document.select_all();
    }
    start_rotation();
    warp_whole_image_ = whole_image;
    rotation_angle = std::remainder(degrees, 360.0);
    ++warp_generation_;
    commit_warp();
}
void Editor::request_skew(int width, int height, bool scale, double horizontal_degrees,
                          double vertical_degrees) {
    if (!std::isfinite(horizontal_degrees) || !std::isfinite(vertical_degrees) ||
        std::abs(horizontal_degrees) > 80 || std::abs(vertical_degrees) > 80) {
        throw std::runtime_error(tr("Skew angles must be between -80 and 80 degrees."));
    }
    finish_text(true);
    finish_warp(false);
    document.require_rgba_transform();
    if (warp_worker_.busy()) {
        throw std::runtime_error(tr("Finish the current stamp transform first."));
    }
    document.commit_curve();
    document.commit_path();
    document.lift_selection();
    const Image& input = document.selection.active ? document.selection.image : document.image;
    Image source;
    if (scale) {
        source = input;
    } else {
        source.reset(width, height, document.ink.secondary);
        composite(source, input, 0, 0);
    }
    double horizontal = std::tan(horizontal_degrees * std::numbers::pi / 180.0);
    double vertical = std::tan(vertical_degrees * std::numbers::pi / 180.0);
    if (std::abs(1 - horizontal * vertical) < 0.05) {
        throw std::runtime_error(tr("This skew collapses the picture. Choose smaller angles."));
    }
    const double sx = scale ? static_cast<double>(width) / input.width : 1;
    const double sy = scale ? static_cast<double>(height) / input.height : 1;
    AffineMap map{sx,
                  horizontal * sy,
                  (sx - 1 + horizontal * (sy - 1)) * 0.5,
                  vertical * sx,
                  sy,
                  (sy - 1 + vertical * (sx - 1)) * 0.5};
    Point corners[4] = {{-0.5, -0.5},
                        {source.width - 0.5, -0.5},
                        {source.width - 0.5, source.height - 0.5},
                        {-0.5, source.height - 0.5}};
    double left = 0, right = 0, top = 0, bottom = 0;
    for (int i = 0; i < 4; ++i) {
        double x = map.xx * corners[i].x + map.xy * corners[i].y + map.tx,
               y = map.yx * corners[i].x + map.yy * corners[i].y + map.ty;
        if (i == 0) {
            left = x;
            right = x;
            top = y;
            bottom = y;
        } else {
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    int x = static_cast<int>(std::floor(left + 0.5)), y = static_cast<int>(std::floor(top + 0.5));
    Rect bounds{x, y, std::max(1, static_cast<int>(std::ceil(right + 0.5)) - x),
                std::max(1, static_cast<int>(std::ceil(bottom + 0.5)) - y)};
    warp_coverage_.clear();
    if (document.selection.active) {
        warp_coverage_ = document.selection.transformed_mask(map, bounds);
    }
    warp_original_ = document.selection;
    warp_whole_image_ = !document.selection.active;
    warp_mode_ = WarpMode::transform;
    warp_commit_ = true;
    ++warp_generation_;
    try {
        warp_worker_.transform(source, map, bounds, warp_generation_);
    } catch (...) {
        cancel_warp();
        throw;
    }
}
void Editor::commit_warp() {
    if (warp_active()) {
        if (warp_mode_ == WarpMode::rotation) {
            update_transform_preview();
        }
        warp_commit_ = warp_pending_ = true;
        mesh_node_ = -1;
        rotation_dragging_ = false;
        poll_warp();
    }
}
void Editor::finish_warp(bool place) {
    if (!warp_active()) {
        return;
    }
    commit_warp();
    while (warp_active() && warp_worker_.busy()) {
        warp_worker_.wait();
        poll_warp();
    }
    if (place) {
        document.commit_selection();
    }
}
void Editor::poll_warp() {
    try {
        WarpResult result;
        if (warp_worker_.take(result)) {
            bool stamp_compile = result.task == WarpTask::CompileStamp;
            bool stamp_render = result.task == WarpTask::Stamp;
            bool compile =
                result.task == WarpTask::CompileRotation || result.task == WarpTask::CompileSelection;
            bool stale = stamp_compile ? result.generation != stamp_source_generation_
                         : stamp_render
                             ? result.generation != stamp_generation_
                             : !warp_active() || (!compile && result.generation != warp_generation_);
            if (!stale && !result.error.empty()) {
                if (stamp_compile || stamp_render) {
                    reset_stamp();
                } else {
                    cancel_warp();
                }
                error(result.error);
                refresh();
                return;
            }
            if (!stale) {
                if (stamp_compile) {
                    stamp_field_ = std::move(result.field);
                } else if (stamp_render) {
                    stamp_preview_ = std::move(result.image);
                    publish_stamp_preview();
                } else if (compile) {
                    warp_field_ = std::move(result.field);
                    if (warp_mode_ == WarpMode::rotation) {
                        update_transform_preview();
                    }
                } else if (!(warp_commit_ && (result.task == WarpTask::PreviewMesh ||
                                              result.task == WarpTask::PreviewRotation))) {
                    bool committed = result.task == WarpTask::Transform ||
                                     result.task == WarpTask::CommitMesh ||
                                     result.task == WarpTask::CommitRotation;
                    if (committed && warp_whole_image_) {
                        if (result.task == WarpTask::Transform) {
                            document.checkpoint();
                        }
                        document.assign_canvas(std::move(result.image));
                        document.selection = {};
                    } else {
                        if (result.task == WarpTask::CommitRotation) {
                            warp_coverage_ = warp_original_.transformed_mask(rotation_map(), result.bounds);
                        }
                        document.selection.image = std::move(result.image);
                        document.selection.x = warp_original_.x + result.bounds.x;
                        document.selection.y = warp_original_.y + result.bounds.y;
                        document.selection.coverage = std::move(warp_coverage_);
                        document.selection.outline.clear();
                        document.selection.source.reset();
                    }
                    if (committed) {
                        clear_transform_preview();
                        if (warp_mode_ == WarpMode::mesh) {
                            document.commit_selection();
                        }
                        warp_mode_ = WarpMode::none;
                        warp_original_ = {};
                        warp_field_.reset();
                        reshape_mesh_ = {};
                        warp_pending_ = warp_commit_ = warp_whole_image_ = false;
                    }
                }
                refresh();
            }
        }
        if (warp_worker_.busy()) {
            return;
        }
        if (warp_active() && warp_field_ && warp_pending_) {
            if (warp_mode_ == WarpMode::mesh) {
                warp_worker_.mesh(warp_commit_ ? WarpTask::CommitMesh : WarpTask::PreviewMesh, warp_field_,
                                  reshape_mesh_, mesh_bounds(reshape_mesh_), warp_generation_);
            } else {
                AffineMap map = rotation_map();
                warp_worker_.affine(warp_commit_ ? WarpTask::CommitRotation : WarpTask::PreviewRotation,
                                    warp_field_, map, affine_bounds(*warp_field_, map), warp_generation_);
            }
            warp_pending_ = false;
        } else if (stamp_pending_ && !document.stamp.pixels.empty()) {
            if (!stamp_field_) {
                warp_worker_.compile(WarpTask::CompileStamp, document.stamp, stamp_source_generation_);
            } else {
                double angle = stamp_angle * std::numbers::pi / 180;
                double cosine = std::cos(angle) * stamp_scale, sine = std::sin(angle) * stamp_scale;
                AffineMap map{cosine, -sine, 0, sine, cosine, 0};
                warp_worker_.affine(WarpTask::Stamp, stamp_field_, map, affine_bounds(*stamp_field_, map),
                                    stamp_generation_);
                stamp_pending_ = false;
            }
        }
    } catch (const std::exception& exception) {
        cancel_warp();
        error(exception.what());
    }
}
void Editor::publish_stamp_preview() {
    stamp_preview_generation_ = stamp_generation_;
    ++stamp_pixels_revision_;
    stamp_boundary_.clear();
    for (int y = 0; y < stamp_preview_.height; ++y) {
        for (int x = 0; x < stamp_preview_.width; ++x) {
            if (!stamp_preview_.get(x, y).a) {
                continue;
            }
            if (!stamp_preview_.get(x, y - 1).a) {
                stamp_boundary_.push_back({static_cast<double>(x), static_cast<double>(y)});
                stamp_boundary_.push_back({x + 1.0, static_cast<double>(y)});
            }
            if (!stamp_preview_.get(x, y + 1).a) {
                stamp_boundary_.push_back({static_cast<double>(x), y + 1.0});
                stamp_boundary_.push_back({x + 1.0, y + 1.0});
            }
            if (!stamp_preview_.get(x - 1, y).a) {
                stamp_boundary_.push_back({static_cast<double>(x), static_cast<double>(y)});
                stamp_boundary_.push_back({static_cast<double>(x), y + 1.0});
            }
            if (!stamp_preview_.get(x + 1, y).a) {
                stamp_boundary_.push_back({x + 1.0, static_cast<double>(y)});
                stamp_boundary_.push_back({x + 1.0, y + 1.0});
            }
        }
    }
    if (!window() || stamp_preview_.pixels.empty()) {
        return;
    }
    std::vector<std::byte> pixels(stamp_preview_.pixels.size() * 4);
    for (std::size_t i = 0; i < stamp_preview_.pixels.size(); ++i) {
        Color color = stamp_preview_.pixels[i];
        pixels[i * 4] = static_cast<std::byte>((color.b * color.a + 127U) / 255U);
        pixels[i * 4 + 1] = static_cast<std::byte>((color.g * color.a + 127U) / 255U);
        pixels[i * 4 + 2] = static_cast<std::byte>((color.r * color.a + 127U) / 255U);
        pixels[i * 4 + 3] = static_cast<std::byte>(color.a);
    }
    gf::ImageLoadResult result =
        stamp_image_.value == 0
            ? (*window()).load_bgra32_premultiplied(stamp_preview_.width, stamp_preview_.height,
                                                    stamp_preview_.width * 4, pixels)
            : (*window()).replace_bgra32_premultiplied(stamp_image_, stamp_preview_.width,
                                                       stamp_preview_.height, stamp_preview_.width * 4,
                                                       pixels, *canvas_);
    if (!result) {
        throw std::runtime_error(tr("The stamp preview exceeds the display resource budget."));
    }
    stamp_image_ = result.image;
}
void Editor::reset_stamp() {
    stamp_placement_ = false;
    if (transform_preview_stamp_) {
        end_transform_preview();
    }
    stamp_rendered_revision_ = 0;
    if (stamp_view_image_.value && window()) {
        static_cast<void>((*window()).remove_image(stamp_view_image_));
    }
    stamp_view_image_ = {};
    if (stamp_image_.value != 0 && window()) {
        static_cast<void>((*window()).remove_image(stamp_image_));
    }
    stamp_image_ = {};

    document.stamp = {};
    hard_stamp_ = {};
    stamp_basis_ = {};
    adding_stamp_material_ = false;
    stamp_preview_ = {};
    stamp_boundary_.clear();
    stamp_scale = 1;
    stamp_angle = 0;
    stamp_field_.reset();
    stamp_pending_ = false;
    ++stamp_generation_;
    ++stamp_source_generation_;
}
void Editor::use_asset_stamp(Image image) {
    if (image.pixels.empty()) {
        return;
    }
    document.require_rgba_transform();
    Image original = image;
    finish_controls();
    choose_tool(Tool::Stamp);
    reset_stamp();
    document.stamp = std::move(image);
    hard_stamp_ = std::move(original);
    stamp_hardness = 1;
    const Point origin = canvas().visible_paste_origin(document.stamp.width, document.stamp.height);
    stamp_placement_center_ = {origin.x + document.stamp.width * 0.5,
                               origin.y + document.stamp.height * 0.5};
    stamp_placement_revision_ = document.revision;
    stamp_placement_epoch_ = document.atlas_epoch;
    pick_hotspot = false;
    stamp_placement_ = true;
    regenerate_stamp();
    (*ribbon_).show_tool_context();
    refresh();
}
bool Editor::can_apply_stamp_placement() const {
    return stamp_placement_ && document.revision == stamp_placement_revision_ &&
           document.atlas_epoch == stamp_placement_epoch_ && stamp_preview_generation_ == stamp_generation_ &&
           !stamp_pending_ && !stamp_preview_.pixels.empty();
}
Image Editor::stamp_placement_image() const {
    Image result = document.image;
    composite_stamp(result, document.image, stamp_placement_center_);
    return result;
}
void Editor::apply_stamp_placement() {
    if (!can_apply_stamp_placement()) {
        return;
    }
    Image result = stamp_placement_image();
    document.checkpoint();
    document.image = std::move(result);
    release_gesture();
    reset_stamp();
    refresh();
}
void Editor::cancel_stamp_placement() {
    if (stamp_placement_) {
        release_gesture();
        reset_stamp();
        refresh();
    }
}
void Editor::add_stamp_material() {
    if (stamp_placement_ || document.stamp.pixels.empty()) {
        return;
    }
    choose_tool(Tool::Stamp);
    stamp_basis_ = hard_stamp_.pixels.empty() ? document.stamp : hard_stamp_;
    adding_stamp_material_ = true;
    document.stamp = {};
    stamp_pending_ = false;
    ++stamp_generation_;
    ++stamp_source_generation_;
    refresh();
}
void Editor::update_stamp_hardness() {
    if (hard_stamp_.pixels.empty()) {
        return;
    }
    document.stamp = hard_stamp_;
    if (stamp_hardness < 0.999) {
        const std::vector<Point> outline =
            stamp_outline(document.stamp_shape, document.stamp.width, document.stamp.height);
        const double feather =
            std::max(0.5, std::min(document.stamp.width, document.stamp.height) * 0.5 * (1 - stamp_hardness));
        for (int y = 0; y < document.stamp.height; ++y) {
            for (int x = 0; x < document.stamp.width; ++x) {
                double distance = feather;
                for (std::size_t i = 0; i < outline.size(); ++i) {
                    const Point a = outline[i], b = outline[(i + 1) % outline.size()];
                    const double dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
                    const double t =
                        length > 0
                            ? std::clamp(((x + 0.5 - a.x) * dx + (y + 0.5 - a.y) * dy) / length, 0.0, 1.0)
                            : 0;
                    distance = std::min(distance, std::hypot(x + 0.5 - a.x - t * dx, y + 0.5 - a.y - t * dy));
                }
                const double t = distance / feather;
                Color& color = document.stamp.pixels[static_cast<std::size_t>(y) * document.stamp.width + x];
                color.a = static_cast<std::uint8_t>(std::lround(color.a * t * t * (3 - 2 * t)));
            }
        }
    }
    stamp_field_.reset();
    ++stamp_source_generation_;
    regenerate_stamp();
}
void Editor::regenerate_stamp() {
    if (document.stamp.pixels.empty()) {
        return;
    }
    ++stamp_generation_;
    if (stamp_scale == 1 && std::remainder(stamp_angle, 360.0) == 0) {
        stamp_pending_ = false;
        stamp_preview_ = document.stamp;
        publish_stamp_preview();
        return;
    }
    stamp_pending_ = true;
    poll_warp();
}
void Editor::stamp_at(Point point, bool checkpoint) {
    if (document.stamp.pixels.empty()) {
        int height = document.stamp_shape == StampShape::Circle || document.stamp_shape == StampShape::Square
                         ? stamp_width
                         : stamp_height;
        document.stamp = make_stamp(document.image,
                                    {static_cast<int>(point.x - stamp_width / 2.0),
                                     static_cast<int>(point.y - height / 2.0), stamp_width, height},
                                    document.stamp_shape, document.stamp_transparent, document.ink.secondary);
        if (adding_stamp_material_) {
            document.stamp = heal_stamp_material(stamp_basis_, document.stamp);
            adding_stamp_material_ = false;
            stamp_basis_ = {};
        }
        hard_stamp_ = document.stamp;
        stamp_scale = 1;
        stamp_angle = 0;
        update_stamp_hardness();
    } else if (!stamp_pending_ && !stamp_preview_.pixels.empty()) {
        if (checkpoint) {
            document.checkpoint();
        }
        composite_stamp(document.image, paint_base_, point);
    }
}
void Editor::composite_stamp(Image& target, const Image& base, Point point) const {
    const int left = static_cast<int>(point.x - stamp_preview_.width / 2.0),
              top = static_cast<int>(point.y - stamp_preview_.height / 2.0);
    if (atlas_painting() && atlas_wrap) {
        for (int y = 0; y < stamp_preview_.height; ++y) {
            for (int x = 0; x < stamp_preview_.width; ++x) {
                const int px =
                    ((left + x) % target.width + target.width) % target.width;
                const int py =
                    ((top + y) % target.height + target.height) % target.height;
                target.blend(px, py, stamp_preview_.get(x, y));
            }
        }
    } else {
        composite(target, stamp_preview_, left, top);
    }
    constrain_paint(target, base, guide, atlas_painting() && atlas_preserve_alpha);
    document.constrain_selection(target, base);
}

gf::Point Editor::rotation_handle() const {
    const FloatingSelection& selection =
        warp_mode_ == WarpMode::rotation ? warp_original_ : document.selection;
    gf::Point center =
        screen({selection.x + selection.image.width * 0.5, selection.y + selection.image.height * 0.5});
    gf::Point corner =
        screen({static_cast<double>(selection.x + selection.image.width), static_cast<double>(selection.y)});
    double x = corner.x + 18 - center.x, y = corner.y - 18 - center.y;
    double angle = warp_mode_ == WarpMode::rotation ? rotation_angle * std::numbers::pi / 180 : 0;
    return {center.x + std::cos(angle) * x - std::sin(angle) * y,
            center.y + std::sin(angle) * x + std::cos(angle) * y};
}
bool Editor::warp_pointer(const gf::PointerEvent& event, Point point) {
    if (event.button == gf::PointerButton::middle || panning_ ||
        (document.tool == Tool::Lasso && (control_ || alt_))) {
        return false;
    }
    if (warp_commit_) {
        return true;
    }
    if (warp_mode_ == WarpMode::mesh) {
        if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary) {
            Point local{point.x - warp_original_.x, point.y - warp_original_.y};
            for (std::size_t i = 0; i < reshape_mesh_.nodes.size(); ++i) {
                Point target = reshape_mesh_.nodes[i].target;
                if (std::hypot(local.x - target.x, local.y - target.y) * (*canvas_).zoom() < 10) {
                    mesh_node_ = static_cast<int>(i);
                    (*canvas_).set_pointer_capture(true);
                    break;
                }
            }
        } else if ((event.action == gf::PointerAction::move || event.action == gf::PointerAction::up) &&
                   mesh_node_ >= 0) {
            if (move_reshape_node(reshape_mesh_, static_cast<std::size_t>(mesh_node_),
                                  {point.x - warp_original_.x, point.y - warp_original_.y})) {
                ++warp_generation_;
                warp_pending_ = true;
                poll_warp();
            }
            if (event.action == gf::PointerAction::up) {
                mesh_node_ = -1;
                (*canvas_).set_pointer_capture(false);
            }
        }
        (*canvas_).invalidate(gf::Dirty::paint);
        return true;
    }
    if (!document.selection.active || dragging_ || text.active ||
        (document.selection.on_canvas && document.tool != Tool::Select && document.tool != Tool::Lasso)) {
        return false;
    }
    gf::Point pointer = screen(point), handle = rotation_handle();
    bool hit = std::hypot(pointer.x - handle.x, pointer.y - handle.y) <= 11;
    if (hit && !warp_active() && !warp_worker_.busy() && event.action == gf::PointerAction::down &&
        event.button == gf::PointerButton::primary) {
        start_rotation();
        rotation_dragging_ = true;
        Point center{warp_original_.x + warp_original_.image.width * 0.5,
                     warp_original_.y + warp_original_.image.height * 0.5};
        rotation_pointer_start_ = std::atan2(point.y - center.y, point.x - center.x);
        (*canvas_).set_pointer_capture(true);
    }
    if (rotation_dragging_ &&
        (event.action == gf::PointerAction::move || event.action == gf::PointerAction::up)) {
        Point center{warp_original_.x + warp_original_.image.width * 0.5,
                     warp_original_.y + warp_original_.image.height * 0.5};
        rotation_angle =
            std::remainder((std::atan2(point.y - center.y, point.x - center.x) - rotation_pointer_start_) *
                               180 / std::numbers::pi,
                           360.0);
        if (shift_) {
            rotation_angle = std::round(rotation_angle / 15) * 15;
        }
        ++warp_generation_;
        update_transform_preview();
        if (event.action == gf::PointerAction::up) {
            (*canvas_).set_pointer_capture(false);
            commit_warp();
        } else {
            poll_warp();
        }
        (*canvas_).invalidate(gf::Dirty::paint);
    }
    return warp_active();
}
void Editor::prepare_stamp_view() {
    if (!stamp_placement_ && document.tool == Tool::Stamp && cursor_client_ && std::abs(canvas().view_angle) > 1e-10) {
        canvas().prepare_display();
    }
}
void Editor::render_stamp_view() {
    if (stamp_placement_ || document.tool != Tool::Stamp || !cursor_client_ || std::abs(canvas().view_angle) < 1e-10 ||
        stamp_preview_.pixels.empty() || !window()) {
        stamp_view_destination_ = {};
        return;
    }
    const gui_drawing::PointF center = canvas().client_to_bitmap(*cursor_client_);
    const Point origin{std::round(center.x - stamp_preview_.width * 0.5),
                       std::round(center.y - stamp_preview_.height * 0.5)};
    if (!stamp_view_generation_ || stamp_view_generation_ != stamp_pixels_revision_) {
        begin_transform_preview(true);
        stamp_view_generation_ = stamp_pixels_revision_;
    }
    const gui_drawing::PointF view_origin = canvas().view_origin();
    const gf::Rect viewport = canvas().client_rectangle();
    const bool prepared = static_cast<bool>(transform_preview_field_);
    if (stamp_rendered_revision_ == stamp_pixels_revision_ && stamp_rendered_prepared_ == prepared &&
        stamp_rendered_origin_.x == origin.x && stamp_rendered_origin_.y == origin.y &&
        stamp_rendered_view_origin_.x == view_origin.x && stamp_rendered_view_origin_.y == view_origin.y &&
        stamp_rendered_angle_ == canvas().view_angle && stamp_rendered_zoom_ == canvas().zoom() &&
        stamp_rendered_viewport_.width == viewport.width &&
        stamp_rendered_viewport_.height == viewport.height) {
        return;
    }
    const gf::Rect destination = gf::Rect::intersection(
        canvas().client_rectangle(),
        canvas().bitmap_to_client({static_cast<int>(origin.x), static_cast<int>(origin.y),
                                   stamp_preview_.width, stamp_preview_.height}));
    if (destination.empty()) {
        stamp_view_destination_ = {};
        stamp_rendered_revision_ = 0;
        return;
    }
    ++canvas().work_statistics_.stamp_renders;
    const int width = static_cast<int>(std::ceil(destination.width)),
              height = static_cast<int>(std::ceil(destination.height));
    Image image;
    if (transform_preview_field_) {
        const double c = std::cos(canvas().view_angle) * canvas().zoom(),
                     s = std::sin(canvas().view_angle) * canvas().zoom();
        const gf::Point first = screen({origin.x + 0.5, origin.y + 0.5});
        const AffineMap map{c, -s, first.x - destination.x - 0.5, s, c, first.y - destination.y - 0.5};
        render_affine(*transform_preview_field_, map, width, height, image, WarpSampling::Point);
    } else {
        image.reset(width, height, {0, 0, 0, 0});
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const gui_drawing::PointF point =
                    canvas().client_to_bitmap({destination.x + x + 0.5, destination.y + y + 0.5});
                image.set(x, y,
                          stamp_preview_.get(static_cast<int>(std::floor(point.x - origin.x)),
                                             static_cast<int>(std::floor(point.y - origin.y))));
            }
        }
    }
    std::vector<std::byte> pixels(image.pixels.size() * 4);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const Color color = image.pixels[i];
        pixels[i * 4] = static_cast<std::byte>((color.b * color.a + 127) / 255);
        pixels[i * 4 + 1] = static_cast<std::byte>((color.g * color.a + 127) / 255);
        pixels[i * 4 + 2] = static_cast<std::byte>((color.r * color.a + 127) / 255);
        pixels[i * 4 + 3] = static_cast<std::byte>(color.a);
    }
    const gf::ImageLoadResult result =
        stamp_view_image_.value ? (*window()).replace_bgra32_premultiplied(stamp_view_image_, width, height,
                                                                           width * 4, pixels, canvas())
                                : (*window()).load_bgra32_premultiplied(width, height, width * 4, pixels);
    if (result) {
        stamp_view_image_ = result.image;
        stamp_rendered_revision_ = stamp_pixels_revision_;
        stamp_rendered_prepared_ = prepared;
        stamp_rendered_origin_ = origin;
        stamp_rendered_view_origin_ = view_origin;
        stamp_rendered_angle_ = canvas().view_angle;
        stamp_rendered_zoom_ = canvas().zoom();
        stamp_rendered_viewport_ = viewport;
        stamp_view_destination_ = {destination.x, destination.y, static_cast<double>(width),
                                   static_cast<double>(height)};
    }
}
void Editor::paint_warp_overlay(gf::Painter& painter) {
    const gf::Color blue = gf::Color::rgba(30, 100, 190), white = gf::Color::rgba(255, 255, 255);
    if (document.tool == Tool::Stamp && (stamp_placement_ || cursor_client_)) {
        double width = stamp_preview_.pixels.empty() ? stamp_width : stamp_preview_.width;
        double height = stamp_preview_.pixels.empty() ? (document.stamp_shape == StampShape::Circle ||
                                                                 document.stamp_shape == StampShape::Square
                                                             ? stamp_width
                                                             : stamp_height)
                                                      : stamp_preview_.height;
        width *= (*canvas_).zoom();
        height *= (*canvas_).zoom();
        const gf::Point client = stamp_placement_ ? screen(stamp_placement_center_) : *cursor_client_;
        gf::Rect bounds{client.x - width / 2, client.y - height / 2, width, height};
        const gui_drawing::PointF center = canvas().client_to_bitmap(client);
        const Point image_origin{
            stamp_placement_ ? std::trunc(stamp_placement_center_.x - stamp_preview_.width * 0.5)
                             : std::round(center.x - width / (2 * canvas().zoom())),
            stamp_placement_ ? std::trunc(stamp_placement_center_.y - stamp_preview_.height * 0.5)
                             : std::round(center.y - height / (2 * canvas().zoom()))};
        if (!stamp_placement_ && stamp_image_.value != 0) {
            if (std::abs(canvas().view_angle) > 1e-10) {
                if (stamp_view_image_.value && !stamp_view_destination_.empty()) {
                    painter.draw_image(stamp_view_image_, stamp_view_destination_,
                                       adding_stamp_material_ ? 0.5 : 0.65);
                }
            } else {
                painter.draw_image(stamp_image_, bounds, adding_stamp_material_ ? 0.5 : 0.65);
            }
        }
        double scale = (*canvas_).zoom();
        if (stamp_preview_.pixels.empty()) {
            std::vector<Point> outline = stamp_outline(document.stamp_shape, static_cast<int>(width / scale),
                                                       static_cast<int>(height / scale));
            Shape geometry = stamp_geometry_shape(document.stamp_shape);
            bool open = geometry == Shape::Line || geometry == Shape::Bezier || geometry == Shape::Arc;
            std::size_t edges = outline.size() - (open ? 1 : 0);
            for (std::size_t i = 0; i < edges; ++i) {
                Point first = outline[i], last = outline[(i + 1) % outline.size()];
                gf::Point a = screen({image_origin.x + first.x, image_origin.y + first.y});
                gf::Point b = screen({image_origin.x + last.x, image_origin.y + last.y});
                painter.draw_line(a, b, white, open ? 3 * scale + 2 : 3);
                painter.draw_line(a, b, blue, open ? 3 * scale : 1);
            }
        } else {
            for (std::size_t i = 0; i + 1 < stamp_boundary_.size(); i += 2) {
                Point first = stamp_boundary_[i], last = stamp_boundary_[i + 1];
                painter.draw_line(screen({image_origin.x + first.x, image_origin.y + first.y}),
                                  screen({image_origin.x + last.x, image_origin.y + last.y}), blue, 1);
            }
        }
    }

    if (warp_mode_ == WarpMode::mesh) {
        for (const MeshTriangle& triangle : reshape_mesh_.triangles) {
            for (int edge = 0; edge < 3; ++edge) {
                Point a = reshape_mesh_.nodes[triangle.nodes[edge]].target;
                Point b = reshape_mesh_.nodes[triangle.nodes[(edge + 1) % 3]].target;
                painter.draw_line(screen({a.x + warp_original_.x, a.y + warp_original_.y}),
                                  screen({b.x + warp_original_.x, b.y + warp_original_.y}),
                                  gf::Color::rgba(30, 100, 190, 120), 1);
            }
        }
        for (const MeshNode& node : reshape_mesh_.nodes) {
            gf::Point point = screen({node.target.x + warp_original_.x, node.target.y + warp_original_.y});
            painter.fill_rect({point.x - 4, point.y - 4, 8, 8}, white);
            painter.stroke_rect({point.x - 4, point.y - 4, 8, 8}, blue, 2);
        }
    } else if (document.selection.active && (!document.selection.on_canvas || document.tool == Tool::Select ||
                                             document.tool == Tool::Lasso)) {
        gf::Point handle = rotation_handle();
        painter.fill_rect({handle.x - 10, handle.y - 10, 20, 20}, white);
        painter.stroke_rect({handle.x - 10, handle.y - 10, 20, 20}, blue, 1);
        gf::Point previous{handle.x - 5, handle.y};
        for (int i = 1; i <= 18; ++i) {
            double angle = std::numbers::pi + i * std::numbers::pi * 1.6 / 18;
            gf::Point next{handle.x + 5 * std::cos(angle), handle.y + 5 * std::sin(angle)};
            painter.draw_line(previous, next, blue, 1.5);
            previous = next;
        }
        painter.draw_line(previous, {previous.x + 3, previous.y - 4}, blue, 1.5);
        painter.draw_line(previous, {previous.x - 4, previous.y - 2}, blue, 1.5);
        if (warp_mode_ == WarpMode::rotation) {
            painter.draw_text_utf8({handle.x + 14, handle.y + 4},
                                   std::to_string(static_cast<int>(std::lround(rotation_angle))) + "°",
                                   {gf::FontRole::control, 12, 400, false}, blue);
        }
    }
}
} // namespace paint::forms
