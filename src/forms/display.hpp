#pragma once
#include "image.hpp"
#include <gui_forms/canvas.hpp>
namespace paint::forms {
class PaintCanvas;
void publish_image(const Image& source, PaintCanvas& canvas, Rect damage = {});
// The document stays straight RGBA. Only this disposable presentation cache is premultiplied.
void publish_image(const Image& source, gui_forms::RasterCanvas& canvas, Rect damage = {});
} // namespace paint::forms
