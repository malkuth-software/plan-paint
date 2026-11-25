#pragma once
// Plan Paint's stx_conv implementation at 45d8442 (src/conv.hpp), kept only for
// benchmarks/conv_compare (PLAN_PAINT_CONV_COMPARE): namespace paint::legacy.
#include "image.hpp"
namespace paint::legacy {
// CONV* through stx_conv: double precision, premultiplied sRGB, separable
// nodal/basin plans; independent lines run on up to eight worker threads.
// Input and output may alias; destination is replaced only after success.
void conv_resize(const Image& source, int width, int height, Image& destination);
// Full target-pixel basins on each resized axis, including half-pixel source edges.
// Unchanged dimensions preserve their samples; no-op resize is byte-exact.
void conv_resize_area(const Image& source, int width, int height, Image& destination);
} // namespace paint::legacy
