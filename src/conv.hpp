#pragma once
#include "image.hpp"
namespace paint {
// Native CONV*: double precision, premultiplied sRGB, separable nodal/basin plans.
// Input and output may alias; destination is replaced only after success.
void conv_resize(const Image& source, int width, int height, Image& destination);
// Full target-pixel basins on each resized axis, including half-pixel source edges.
// Unchanged dimensions preserve their samples; no-op resize is byte-exact.
void conv_resize_area(const Image& source, int width, int height, Image& destination);
} // namespace paint
