#pragma once
#include "image.hpp"
namespace paint {
enum class IconEffect {
    BrightnessContrast,
    HueSaturation,
    Grayscale,
    Opacity,
    Blur,
    Sharpen,
    Shadow,
    Fit,
    Gamma,
    Levels,
    Invert,
    EdgeFind,
    EdgeEnhance,
    Contour,
    Emboss
};
struct IconEffectSettings {
    IconEffect effect = IconEffect::BrightnessContrast;
    double brightness = 0, contrast = 0, hue = 0, saturation = 100, opacity = 100, strength = 100;
    int radius = 2, offset_x = 2, offset_y = 2, padding = 2;
    Color shadow = {0, 0, 0, 255};
    double gamma = 1;
    int input_black = 0, input_white = 255, output_black = 0, output_white = 255;
};
struct IconEffectResult {
    Image image;
    Image overflow; // Shadow proof before clipping, with image bounds in this coordinate system.
    Rect bounds;
    std::size_t clipped_pixels = 0;
};
// Bounded icon images (1..256 per axis), fixed output dimensions, no source mutation.
// Color adjustments use encoded RGB; filtering uses double premultiplied RGBA.
IconEffectResult render_icon_effect(const Image& source, const IconEffectSettings& settings);
} // namespace paint
