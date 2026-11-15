#include "superimpose.hpp"
#include <algorithm>
#include <cmath>
namespace paint {
bool SuperimposedImage::contains(Point point) const {
    return active && point.x >= x && point.y >= y && point.x < x + image.width && point.y < y + image.height;
}
void superimpose_onto(Image& target, const Image& overlay, int x, int y, const SuperimposeLook& look, Rect area) {
    if (overlay.width <= 0 || overlay.height <= 0) {
        return;
    }
    const int left = std::max({area.x, x, 0});
    const int top = std::max({area.y, y, 0});
    const int right = std::min({area.x + area.w, x + overlay.width, target.width});
    const int bottom = std::min({area.y + area.h, y + overlay.height, target.height});
    if (left >= right || top >= bottom) {
        return;
    }
    const double opacity =
        1.0 - std::clamp(look.transparency, 0.0, superimpose_maximum_transparency) / 100.0;
    const double brightness = std::clamp(look.brightness, -100.0, 100.0) / 100.0;
    for (int row = top; row < bottom; ++row) {
        for (int column = left; column < right; ++column) {
            const Color source = overlay.get(column - x, row - y);
            const double alpha = opacity * source.a / 255.0;
            if (alpha <= 0.0) {
                continue;
            }
            const Color below = target.get(column, row);
            const double below_alpha = below.a / 255.0 * (1.0 - alpha);
            const double out_alpha = alpha + below_alpha;
            const double channels[3][2] = {{static_cast<double>(source.r), static_cast<double>(below.r)},
                                           {static_cast<double>(source.g), static_cast<double>(below.g)},
                                           {static_cast<double>(source.b), static_cast<double>(below.b)}};
            std::uint8_t mixed[3] = {0, 0, 0};
            for (int channel = 0; channel < 3; ++channel) {
                const double raw = channels[channel][0];
                const double lit = brightness >= 0.0 ? raw + (255.0 - raw) * brightness : raw * (1.0 + brightness);
                const double value = (lit * alpha + channels[channel][1] * below_alpha) / out_alpha;
                mixed[channel] = static_cast<std::uint8_t>(std::clamp(std::round(value), 0.0, 255.0));
            }
            const Color result{mixed[0], mixed[1], mixed[2],
                               static_cast<std::uint8_t>(std::clamp(std::round(out_alpha * 255.0), 0.0, 255.0))};
            target.set(column, row, result);
        }
    }
}
} // namespace paint
