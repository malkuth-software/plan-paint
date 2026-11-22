#pragma once
#include "image.hpp"
namespace paint {
// CONV through stxlib::conv: the canonical CONV field of the straight-alpha
// image (its separated form, paper closure, no bubble), read through an axis
// map; destination rows are split into bands on up to eight worker threads,
// one workspace each. Sides up to stxlib::conv::max_side (32768). A one-pixel
// source side is repeated to two (the field is constant across it). Input and
// output may alias; destination is replaced only after success, and an
// unchanged size is a byte-exact copy.
// Preview: nodes aligned at both ends (target x reads source x (w - 1) / (W - 1)).
void conv_resize(const Image& source, int width, int height, Image& destination);
// Canon: each target pixel is the field's average over its footprint,
// (x + [0, 1)) w / W - 1/2 on each axis, exact for the field's cells.
void conv_resize_area(const Image& source, int width, int height, Image& destination);
} // namespace paint
