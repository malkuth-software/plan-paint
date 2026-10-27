#pragma once
#include "document.hpp"
#include <atomic>
namespace paint {
struct NumericTransformSettings {
    double offset_x = 0, offset_y = 0, scale_x = 100, scale_y = 100, angle = 0;
    WarpSampling sampling = WarpSampling::Minification;
};
struct NumericTransformSource {
    Image canvas, artwork;
    FloatingSelection selection;
    Color secondary;
    std::uint64_t revision = 0, epoch = 0;
    int active = -1;
};
struct NumericTransformResult {
    Image canvas;
    FloatingSelection selection;
    bool changed = false;
};
// Scale about the artwork's pixel-centre midpoint, then rotate clockwise, then offset.
AffineMap numeric_transform_map(int width, int height, const NumericTransformSettings& settings);
NumericTransformSource capture_numeric_transform(const Document& document);
NumericTransformResult render_numeric_transform(const NumericTransformSource& source,
                                                const NumericTransformSettings& settings,
                                                const std::atomic<bool>* cancel = nullptr);
// Stale results are rejected before any edit. A neutral result creates no history.
void apply_numeric_transform(Document& document, const NumericTransformSource& source,
                             NumericTransformResult result);
} // namespace paint
