#pragma once
#include "image.hpp"
#include <atomic>
namespace paint {
enum class IconStampScale { Natural, Fit };
struct IconStampSettings {
    IconStampScale scale = IconStampScale::Natural;
    int anchor = 4; // Row-major 3 by 3 grid: top-left through bottom-right.
    int offset_x = 0, offset_y = 0;
    double percent = 100, opacity = 100;
    bool behind = false, flatten = false;
    Color matte{255, 255, 255, 255};
};
struct IconStampResult {
    Image image;
    Rect placement;
    std::size_t clipped_pixels = 0;
    bool upscaled = false;
};
// Paint's straight-alpha encoded-RGB source-over contract, preserving dimensions.
// Center anchoring floors half-pixel offsets; resizing uses the existing CONV kernel.
IconStampResult compose_icon_stamp(const Image& source, const Image& stamp, const IconStampSettings& settings,
                                   const std::atomic<bool>* cancel = nullptr);
} // namespace paint
