#pragma once
// Superimpose (the paste option): an image laid over the work as a tracing
// aid. It is drawn over the canvas in the view only: never merged into the
// document's pixels, saved, exported or recorded for undo. The work below
// shows through it, and drawing goes to the work underneath.
#include "image.hpp"
namespace paint {
struct SuperimposedImage {
    Image image;
    // Its top-left corner, in document pixels.
    int x = 0, y = 0;
    bool active = false;
    // Whether `point` (document pixels) lies on the image's rectangle.
    bool contains(Point point) const;
};
// How it is drawn. Transparency runs from 0 to superimpose_maximum_transparency
// percent, so the image never vanishes entirely; brightness from -100 (black)
// through 0 (unchanged) to +100 (white) percent.
constexpr double superimpose_maximum_transparency = 95;
struct SuperimposeLook {
    double transparency = 50;
    double brightness = 0;
};
// Blends `overlay`, with its top-left corner at (x, y), over `target` within
// `area` (document pixels, clipped to both images), source over destination:
// the overlay's colour, adjusted by the brightness, at its own alpha times
// the opacity (1 - transparency), over the work. Pixels of `target` outside
// `area` are left as they are. `overlay` and `target` must not be the same
// image.
void superimpose_onto(Image& target, const Image& overlay, int x, int y, const SuperimposeLook& look, Rect area);
} // namespace paint
