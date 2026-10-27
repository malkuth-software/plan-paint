#include "numeric_transform.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace paint {
namespace {
bool same_image(const Image& left, const Image& right) {
    return left.width == right.width && left.height == right.height &&
           std::equal(left.pixels.begin(), left.pixels.end(), right.pixels.begin(), right.pixels.end(),
                      equal);
}
bool same_selection(const FloatingSelection& left, const FloatingSelection& right) {
    if (left.active != right.active || left.x != right.x || left.y != right.y ||
        left.on_canvas != right.on_canvas || left.canvas_selection != right.canvas_selection ||
        left.coverage != right.coverage || left.source != right.source ||
        left.outline.size() != right.outline.size() || !same_image(left.image, right.image)) {
        return false;
    }
    for (std::size_t index = 0; index < left.outline.size(); ++index) {
        if (left.outline[index].x != right.outline[index].x ||
            left.outline[index].y != right.outline[index].y) {
            return false;
        }
    }
    return true;
}
void check_cancel(const std::atomic<bool>* cancel) {
    if (cancel && (*cancel).load()) {
        throw std::runtime_error("Transform preview cancelled.");
    }
}
double snapped(double value) {
    const double integer = std::round(value);
    return std::abs(value - integer) < 1e-12 ? integer : value;
}
bool integer_translation(const AffineMap& map) {
    return map.xx == 1 && map.yy == 1 && map.xy == 0 && map.yx == 0 && map.tx == std::round(map.tx) &&
           map.ty == std::round(map.ty);
}
} // namespace
AffineMap numeric_transform_map(int width, int height, const NumericTransformSettings& settings) {
    if (width < 1 || height < 1 || !std::isfinite(settings.offset_x) || !std::isfinite(settings.offset_y) ||
        std::abs(settings.offset_x) > 16384 || std::abs(settings.offset_y) > 16384 ||
        !std::isfinite(settings.scale_x) || !std::isfinite(settings.scale_y) || settings.scale_x < 1 ||
        settings.scale_x > 1600 || settings.scale_y < 1 || settings.scale_y > 1600 ||
        !std::isfinite(settings.angle) || std::abs(settings.angle) > 360 ||
        (settings.sampling != WarpSampling::Point && settings.sampling != WarpSampling::Minification &&
         settings.sampling != WarpSampling::Area)) {
        throw std::runtime_error("Invalid numeric transform settings.");
    }
    const double radians = std::remainder(settings.angle, 360.0) * std::numbers::pi / 180;
    const double cosine = snapped(std::cos(radians)), sine = snapped(std::sin(radians));
    const double sx = settings.scale_x / 100, sy = settings.scale_y / 100;
    const double cx = (width - 1) * 0.5, cy = (height - 1) * 0.5;
    AffineMap result{cosine * sx, -sine * sy, 0, sine * sx, cosine * sy, 0};
    result.tx = cx - result.xx * cx - result.xy * cy + settings.offset_x;
    result.ty = cy - result.yx * cx - result.yy * cy + settings.offset_y;
    return result;
}
NumericTransformSource capture_numeric_transform(const Document& document) {
    document.require_rgba_transform();
    NumericTransformSource result;
    result.canvas = document.image;
    result.selection = document.selection;
    result.artwork = document.selection.active ? document.selected_image() : document.image;
    result.secondary = document.ink.secondary;
    result.revision = document.revision;
    result.epoch = document.atlas_epoch;
    result.active = document.atlas.active;
    return result;
}
NumericTransformResult render_numeric_transform(const NumericTransformSource& source,
                                                const NumericTransformSettings& settings,
                                                const std::atomic<bool>* cancel) {
    check_cancel(cancel);
    const AffineMap map = numeric_transform_map(source.artwork.width, source.artwork.height, settings);
    NumericTransformResult result;
    result.canvas = source.canvas;
    result.selection = source.selection;
    const bool exact_translation = integer_translation(map) && settings.sampling != WarpSampling::Area;
    if (exact_translation && map.tx == 0 && map.ty == 0) {
        return result;
    }
    Rect bounds{0, 0, source.artwork.width, source.artwork.height};
    Image transformed;
    if (exact_translation) {
        if (source.selection.active) {
            bounds.x = static_cast<int>(map.tx);
            bounds.y = static_cast<int>(map.ty);
            transformed = source.artwork;
        } else {
            transformed.reset(source.canvas.width, source.canvas.height, {0, 0, 0, 0});
            for (int y = 0; y < source.artwork.height; ++y) {
                check_cancel(cancel);
                for (int x = 0; x < source.artwork.width; ++x) {
                    transformed.set(x + static_cast<int>(map.tx), y + static_cast<int>(map.ty),
                                    source.artwork.get(x, y));
                }
            }
        }
    } else {
        ConvWarpField field;
        field.compile(source.artwork, cancel);
        if (source.selection.active) {
            bounds = affine_bounds(field, map);
        }
        AffineMap local = map;
        local.tx -= bounds.x;
        local.ty -= bounds.y;
        check_cancel(cancel);
        render_affine(field, local, bounds.w, bounds.h, transformed, settings.sampling, cancel);
        check_cancel(cancel);
    }
    if (source.selection.active) {
        if (source.selection.on_canvas) {
            for (int y = 0; y < source.artwork.height; ++y) {
                for (int x = 0; x < source.artwork.width; ++x) {
                    if (source.selection.contains(source.selection.x + x, source.selection.y + y)) {
                        result.canvas.set(source.selection.x + x, source.selection.y + y, source.secondary);
                    }
                }
            }
        }
        result.selection.image = std::move(transformed);
        result.selection.x += bounds.x;
        result.selection.y += bounds.y;
        result.selection.coverage = source.selection.transformed_mask(map, bounds);
        result.selection.outline.clear();
        result.selection.source.reset();
        result.selection.on_canvas = false;
    } else {
        result.canvas = std::move(transformed);
    }
    result.changed =
        !same_image(result.canvas, source.canvas) || !same_selection(result.selection, source.selection);
    return result;
}
void apply_numeric_transform(Document& document, const NumericTransformSource& source,
                             NumericTransformResult result) {
    if (document.revision != source.revision || document.atlas_epoch != source.epoch ||
        document.atlas.active != source.active || !same_image(document.image, source.canvas) ||
        !same_selection(document.selection, source.selection)) {
        throw std::runtime_error("Image or selection changed. Reopen Transform.");
    }
    if (!result.changed) {
        return;
    }
    if (result.canvas.width != document.image.width || result.canvas.height != document.image.height) {
        throw std::runtime_error("Transform must preserve canvas dimensions.");
    }
    document.require_rgba_transform();
    document.checkpoint();
    document.image = std::move(result.canvas);
    document.selection = std::move(result.selection);
}
} // namespace paint
