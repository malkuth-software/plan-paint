#include "application.hpp"
#include "conv.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace paint {
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
void Application::start_reshape() {
    if (!document.selection.active) {
        status = "Use Free-form selection to lasso an object first.";
        return;
    }
    if (warp_worker.busy()) {
        status = "Please wait for the current image transform.";
        return;
    }
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
        reshape_mesh = make_reshape_mesh(outline, mesh_spacing);
    } else {
        reshape_mesh = make_reshape_mesh(document.selection.image, mesh_spacing);
    }
    reshape_origin = {static_cast<double>(document.selection.x), static_cast<double>(document.selection.y)};
    reshape_field.reset();
    reshape_active = true;
    reshape_render_pending = false;
    reshape_commit_pending = false;
    active_mesh_node = -1;
    mesh_generation = 0;
    warp_worker.compile(WarpTask::CompileSelection, document.selection.image);
    status = "Preparing the CONV material. Drag the blue knobs; Escape commits the shape.";
}
void Application::finish_reshape() {
    if (!reshape_active) {
        return;
    }
    if (mesh_generation == 0 && !warp_worker.busy()) {
        document.commit_selection();
        reshape_active = false;
        reshape_field.reset();
        reshape_mesh = {};
        document.tool = Tool::Select;
        texture_dirty = true;
        return;
    }
    reshape_commit_pending = true;
    reshape_render_pending = true;
    active_mesh_node = -1;
    status = "Finishing the reshaped picture with CONV area sampling...";
}
void Application::regenerate_stamp() {
    if (document.stamp.pixels.empty()) {
        return;
    }
    ++stamp_generation;
    stamp_render_pending = true;
    if (!stamp_field && !warp_worker.busy()) {
        warp_worker.compile(WarpTask::CompileStamp, document.stamp, stamp_source_generation);
        status = "Preparing the CONV stamp...";
    }
}
void Application::poll_warp() {
    WarpResult result;
    if (warp_worker.take(result)) {
        bool stale_stamp =
            (result.task == WarpTask::CompileStamp && result.generation != stamp_source_generation) ||
            (result.task == WarpTask::Stamp && result.generation != stamp_generation);
        if (stale_stamp) {
            // Reset is immediate, even while an obsolete sample is compiling.
        } else if (result.task == WarpTask::CompileRotation || result.task == WarpTask::PreviewRotation ||
                   result.task == WarpTask::CommitRotation) {
            rotation_result(result);
        } else if (!result.error.empty()) {
            error = result.error;
            reshape_commit_pending = false;
            reshape_render_pending = false;
            stamp_render_pending = false;
            transform_active = false;
            if (result.task == WarpTask::CompileSelection) {
                reshape_active = false;
                document.tool = Tool::Select;
            }
        } else if (result.task == WarpTask::CompileSelection) {
            if (reshape_active) {
                reshape_field = std::move(result.field);
                status = "Drag the blue knobs. A red warning means the mesh would fold. Escape commits.";
            }
        } else if (result.task == WarpTask::CompileStamp) {
            stamp_field = std::move(result.field);
        } else if (result.task == WarpTask::PreviewMesh || result.task == WarpTask::CommitMesh) {
            if (result.generation == mesh_generation && reshape_active) {
                document.selection.image = std::move(result.image);
                document.selection.x = static_cast<int>(reshape_origin.x) + result.bounds.x;
                document.selection.y = static_cast<int>(reshape_origin.y) + result.bounds.y;
                document.selection.coverage.clear();
                document.selection.outline.clear();
                texture_dirty = true;
                if (result.task == WarpTask::CommitMesh) {
                    document.commit_selection();
                    reshape_active = false;
                    reshape_field.reset();
                    reshape_mesh = {};
                    reshape_commit_pending = false;
                    reshape_render_pending = false;
                    document.tool = Tool::Select;
                    status = "Reshape placed. Undo returns the original object.";
                }
            }
        } else if (result.task == WarpTask::Stamp) {
            if (result.generation == stamp_generation) {
                stamp_preview = std::move(result.image);
                SDL_DestroyTexture(stamp_texture);
                stamp_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                                  stamp_preview.width, stamp_preview.height);
                if (!stamp_texture) {
                    throw std::runtime_error(SDL_GetError());
                }
                SDL_UpdateTexture(stamp_texture, nullptr, stamp_preview.pixels.data(),
                                  stamp_preview.width * 4);
                SDL_SetTextureBlendMode(stamp_texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(stamp_texture, SDL_SCALEMODE_NEAREST);
                ++texture_generation;
                status =
                    "Click to stamp. R rotates; + and - resize. Right-click or Escape lifts a new stamp.";
            }
        } else if (result.task == WarpTask::Transform) {
            if (transform_selection) {
                document.selection.image = std::move(result.image);
                document.selection.coverage.clear();
                document.selection.outline.clear();
                document.selection.x += result.bounds.x;
                document.selection.y += result.bounds.y;
            } else {
                document.checkpoint();
                document.assign_canvas(std::move(result.image));
            }
            transform_active = false;
            texture_dirty = true;
            status = "Resize and skew complete.";
        }
    }
    if (warp_worker.busy()) {
        return;
    }
    if (rotation_active && rotation_field && rotation_render_pending) {
        const AffineMap map = rotation_map();
        const Rect bounds = affine_bounds(*rotation_field, map);
        warp_worker.affine(rotation_commit_pending ? WarpTask::CommitRotation : WarpTask::PreviewRotation,
                           rotation_field, map, bounds, rotation_generation);
        rotation_render_pending = false;
    } else if (reshape_active && reshape_field && reshape_render_pending) {
        Rect bounds = mesh_bounds(reshape_mesh);
        warp_worker.mesh(reshape_commit_pending ? WarpTask::CommitMesh : WarpTask::PreviewMesh, reshape_field,
                         reshape_mesh, bounds, mesh_generation);
        reshape_render_pending = false;
    } else if (stamp_render_pending && !stamp_field && !document.stamp.pixels.empty()) {
        warp_worker.compile(WarpTask::CompileStamp, document.stamp, stamp_source_generation);
    } else if (stamp_render_pending && stamp_field) {
        double angle = stamp_angle * std::numbers::pi / 180.0;
        double cosine = std::cos(angle) * stamp_scale, sine = std::sin(angle) * stamp_scale;
        AffineMap map{cosine, -sine, 0, sine, cosine, 0};
        Rect bounds = affine_bounds(*stamp_field, map);
        warp_worker.affine(WarpTask::Stamp, stamp_field, map, bounds, stamp_generation);
        stamp_render_pending = false;
    }
}
void Application::request_skew(int width, int height) {
    document.require_rgba_transform();
    if (warp_worker.busy()) {
        throw std::runtime_error("Finish the current transform first.");
    }
    if (!std::isfinite(skew_horizontal) || !std::isfinite(skew_vertical) || std::abs(skew_horizontal) > 80 ||
        std::abs(skew_vertical) > 80) {
        throw std::runtime_error("Skew angles must be between -80 and 80 degrees.");
    }
    const Image& input = document.selection.active ? document.selection.image : document.image;
    Image source;
    if (resize_scale) {
        conv_resize(input, width, height, source);
    } else {
        source.reset(width, height, document.ink.secondary);
        composite(source, input, 0, 0);
    }
    double horizontal = std::tan(skew_horizontal * std::numbers::pi / 180.0);
    double vertical = std::tan(skew_vertical * std::numbers::pi / 180.0);
    if (std::abs(1 - horizontal * vertical) < 0.05) {
        throw std::runtime_error("This skew collapses the picture. Choose smaller angles.");
    }
    AffineMap map{1, horizontal, 0, vertical, 1, 0};
    Point corners[4] = {{-0.5, -0.5}, {width - 0.5, -0.5}, {width - 0.5, height - 0.5}, {-0.5, height - 0.5}};
    double left = 0, right = 0, top = 0, bottom = 0;
    for (int i = 0; i < 4; ++i) {
        double x = corners[i].x + horizontal * corners[i].y, y = vertical * corners[i].x + corners[i].y;
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
    transform_selection = document.selection.active;
    transform_active = true;
    warp_worker.transform(source, map, bounds);
    status = "Resizing and skewing the material with CONV...";
}
} // namespace paint
