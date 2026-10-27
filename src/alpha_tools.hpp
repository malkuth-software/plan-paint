#pragma once
#include "image.hpp"
#include <array>
#include <cstdint>
namespace paint {
struct AlphaStatistics {
    std::array<std::uint64_t, 256> histogram{};
    std::uint8_t minimum = 255, maximum = 0;
};
AlphaStatistics alpha_statistics(const Image& image);
Image alpha_image(const Image& source);
// Mask RGB uses rounded arithmetic mean in encoded byte space; mask alpha is ignored.
// Explicit resizing uses nearest pixel centers. Source RGB, including hidden RGB, is exact.
Image with_alpha_mask(const Image& source, const Image& mask, bool resize);
} // namespace paint
