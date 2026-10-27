#include "alpha_tools.hpp"
#include <algorithm>
#include <stdexcept>
namespace paint {
namespace {
void validate_alpha_image(const Image& image) {
    if (image.width < 1 || image.height < 1 ||
        image.pixels.size() != std::size_t(image.width) * image.height) {
        throw std::runtime_error("Invalid image geometry for alpha tools.");
    }
}
} // namespace
AlphaStatistics alpha_statistics(const Image& image) {
    validate_alpha_image(image);
    AlphaStatistics result;
    for (const Color& pixel : image.pixels) {
        ++result.histogram[pixel.a];
        result.minimum = std::min(result.minimum, pixel.a);
        result.maximum = std::max(result.maximum, pixel.a);
    }
    return result;
}
Image alpha_image(const Image& source) {
    validate_alpha_image(source);
    Image result;
    result.reset(source.width, source.height);
    for (std::size_t index = 0; index < source.pixels.size(); ++index) {
        const std::uint8_t alpha = source.pixels[index].a;
        result.pixels[index] = {alpha, alpha, alpha, 255};
    }
    return result;
}
Image with_alpha_mask(const Image& source, const Image& mask, bool resize) {
    validate_alpha_image(source);
    validate_alpha_image(mask);
    if (!resize && (source.width != mask.width || source.height != mask.height)) {
        throw std::runtime_error("Mask dimensions differ. Enable mask resizing to continue.");
    }
    Image result = source;
    for (int y = 0; y < source.height; ++y) {
        const int sy =
            static_cast<int>((std::uint64_t(y) * 2 + 1) * mask.height / (std::uint64_t(source.height) * 2));
        for (int x = 0; x < source.width; ++x) {
            const int sx =
                static_cast<int>((std::uint64_t(x) * 2 + 1) * mask.width / (std::uint64_t(source.width) * 2));
            const Color pixel = mask.get(sx, sy);
            result.pixels[std::size_t(y) * source.width + x].a =
                static_cast<std::uint8_t>((unsigned(pixel.r) + pixel.g + pixel.b + 1) / 3);
        }
    }
    return result;
}
} // namespace paint
