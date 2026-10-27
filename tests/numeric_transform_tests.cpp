#include "numeric_transform.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
bool same(const paint::Image& a, const paint::Image& b) {
    return a.width == b.width && a.height == b.height &&
           std::equal(a.pixels.begin(), a.pixels.end(), b.pixels.begin(), b.pixels.end(), paint::equal);
}
void contracts() {
    paint::Document document;
    paint::Image original;
    original.reset(8, 8, {23, 45, 67, 0});
    original.set(2, 3, {200, 90, 30, 180});
    document.replace(original, "");
    paint::NumericTransformSettings settings;
    paint::NumericTransformSource source = paint::capture_numeric_transform(document);
    paint::NumericTransformResult result = paint::render_numeric_transform(source, settings);
    require(!result.changed && same(result.canvas, original),
            "neutral transform preserves hidden RGB exactly");
    paint::apply_numeric_transform(document, source, std::move(result));
    require(document.undo_history.empty(), "neutral transform adds no history");
    settings.offset_x = 2;
    settings.offset_y = -1;
    result = paint::render_numeric_transform(source, settings);
    require(paint::equal(result.canvas.get(4, 2), original.get(2, 3)) && result.canvas.get(0, 0).a == 0 &&
                paint::equal(result.canvas.get(2, 0), original.get(0, 1)),
            "integer offsets clip to fixed canvas and preserve copied RGBA bytes");
    paint::apply_numeric_transform(document, source, std::move(result));
    require(document.undo_history.size() == 1 && document.image.width == 8,
            "whole transform is one fixed-canvas edit");
    document.undo();
    require(same(document.image, original), "Undo restores exact whole-image pixels");
    document.redo();
    require(paint::equal(document.image.get(4, 2), original.get(2, 3)), "Redo restores transform");
    document.replace(original, "");
    settings = {};
    settings.angle = 90;
    const paint::AffineMap quarter = paint::numeric_transform_map(8, 8, settings);
    require(quarter.xx == 0 && quarter.xy == -1 && quarter.tx == 7 && quarter.yx == 1 && quarter.yy == 0 &&
                quarter.ty == 0,
            "positive angle rotates clockwise around exact pixel-center midpoint");
    source = paint::capture_numeric_transform(document);
    result = paint::render_numeric_transform(source, settings);
    require(paint::equal(result.canvas.get(4, 2), original.get(2, 3)),
            "quarter rotation uses existing nodal CONV sampling");
    settings.angle = 0;
    settings.scale_x = 50;
    settings.scale_y = 200;
    settings.offset_x = 1.5;
    const paint::AffineMap scale = paint::numeric_transform_map(8, 8, settings);
    require(scale.xx == 0.5 && scale.yy == 2 && scale.tx == 3.25 && scale.ty == -3.5,
            "independent scales pivot before fractional translation");
    settings = {};
    settings.sampling = paint::WarpSampling::Area;
    result = paint::render_numeric_transform(source, settings);
    require(!same(result.canvas, original), "Area integrates the footprint even at identity");
    document.ink.secondary = {0, 0, 0, 0};
    document.select({1, 2, 3, 3});
    source = paint::capture_numeric_transform(document);
    const std::size_t history = document.undo_history.size();
    settings = {};
    settings.offset_x = 2;
    settings.offset_y = 1;
    result = paint::render_numeric_transform(source, settings);
    require(result.selection.active && result.selection.x == source.selection.x + 2 &&
                result.selection.y == source.selection.y + 1 && same(result.selection.image, source.artwork),
            "selected material moves without resampling at integer offsets");
    paint::apply_numeric_transform(document, source, std::move(result));
    require(document.undo_history.size() == history + 1 && document.selection.active &&
                document.image.width == 8,
            "selection transform retains floating selection and one history step");
    document.undo();
    require(document.selection.x == source.selection.x &&
                same(document.selection.image, source.selection.image),
            "Undo restores original floating selection");
    source = paint::capture_numeric_transform(document);
    result = paint::render_numeric_transform(source, settings);
    ++document.selection.x;
    bool rejected = false;
    try {
        paint::apply_numeric_transform(document, source, std::move(result));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "moving selection without a revision change rejects stale preview");
    document.replace(original, "");
    document.select({1, 2, 3, 3});
    document.settle_selection();
    require(document.selection.on_canvas, "fixture has an anchored painted selection");
    source = paint::capture_numeric_transform(document);
    result = paint::render_numeric_transform(source, settings);
    require(result.canvas.get(2, 3).a == 0 && result.selection.image.get(1, 1).a == 180 &&
                !result.selection.on_canvas,
            "anchored selection lifts only selected pixels with captured background color");
    require(document.image.get(2, 3).a == 180 && document.selection.on_canvas,
            "preview never lifts the live selection");
    std::atomic<bool> cancel{true};
    rejected = false;
    try {
        static_cast<void>(paint::render_numeric_transform(source, settings, &cancel));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "cancelled work publishes no result");
    paint::ConvWarpField field;
    field.compile(source.artwork);
    paint::Image destination = original;
    rejected = false;
    try {
        paint::render_affine(field, {}, 8, 8, destination, paint::WarpSampling::Area, &cancel);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && same(destination, original), "cancelled renderer preserves its destination");
    settings.angle = std::numeric_limits<double>::infinity();
    rejected = false;
    try {
        static_cast<void>(paint::numeric_transform_map(8, 8, settings));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "nonfinite inputs rejected before processing");
}
} // namespace
int main() {
    try {
        contracts();
        std::cout << "Numeric transform contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
