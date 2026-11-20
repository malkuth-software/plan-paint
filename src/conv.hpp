#pragma once
#include "image.hpp"
namespace paint {
// CONV* through stx_conv: double precision, premultiplied sRGB, separable
// nodal/basin plans; independent lines run on up to eight worker threads.
// Input and output may alias; destination is replaced only after success.
void conv_resize(const Image& source, int width, int height, Image& destination);
// Full target-pixel basins on each resized axis, including half-pixel source edges.
// Unchanged dimensions preserve their samples; no-op resize is byte-exact.
void conv_resize_area(const Image& source, int width, int height, Image& destination);
// Ordered local box admission and bounded odd bubble, with full pixel basins.
void conv_resize_rapid_area(const Image& source, int width, int height, Image& destination);
// Calculates the requested rectangle using the original target grid and its boundary closures.
void conv_resize_rapid_region(const Image& source, int width, int height, Rect region, Image& destination);
} // namespace paint
