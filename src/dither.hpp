#pragma once
#include "image.hpp"
#include <array>
#include <atomic>
#include <span>
namespace paint {
enum class DitherPattern { Crosswind, Weave, Scrambled, Drift, Posterize };
struct DitherOptions {
    int colors = 16;
    DitherPattern pattern = DitherPattern::Scrambled;
};
// Empty mask means the whole image. Otherwise mask has exactly width*height bytes.
// Alpha and unselected pixels remain unchanged. Output is prepared before publication.
Image dithered(const Image& source, std::span<const std::uint8_t> mask, DitherOptions options,
               std::vector<Color>* palette = nullptr, const std::atomic<bool>* cancel = nullptr);
std::uint16_t dither_rank(int x, int y, DitherPattern pattern);
enum class DitherBrushMode { Neighborhood, Noise };
class DitherBrushStroke {
    double pending_ = 0;
    std::uint32_t serial_ = 0;
    bool started_ = false;
    void dab(Image& image, Point center, double diameter, DitherBrushMode mode, std::uint32_t seed, bool wrap,
             std::span<const std::uint8_t> mask);

  public:
    void clear();
    void segment(Image& image, Point start, Point end, double diameter, DitherBrushMode mode,
                 std::uint32_t seed, bool wrap = false, std::span<const std::uint8_t> mask = {});
};
} // namespace paint
