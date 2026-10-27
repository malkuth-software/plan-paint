#include "icon_stamp.hpp"
#include "conv.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace paint {
namespace {
void check_cancel(const std::atomic<bool>* cancel) {
    if (cancel && (*cancel).load()) {
        throw std::runtime_error("Cancelled during stamp composition.");
    }
}
void validate_image(const Image& image) {
    if (image.width < 1 || image.height < 1 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height) {
        throw std::runtime_error("Stamp composition requires complete source and stamp images.");
    }
}
int anchored(int extent, int item, int anchor) {
    return anchor == 0   ? 0
           : anchor == 1 ? static_cast<int>(std::floor((extent - item) / 2.0))
                         : extent - item;
}
} // namespace
IconStampResult compose_icon_stamp(const Image& source, const Image& stamp, const IconStampSettings& settings,
                                   const std::atomic<bool>* cancel) {
    validate_image(source);
    validate_image(stamp);
    if (settings.anchor < 0 || settings.anchor > 8 || settings.offset_x < -8192 || settings.offset_x > 8192 ||
        settings.offset_y < -8192 || settings.offset_y > 8192 || !std::isfinite(settings.percent) ||
        settings.percent < 1 || settings.percent > 800 || !std::isfinite(settings.opacity) ||
        settings.opacity < 0 || settings.opacity > 100) {
        throw std::runtime_error("Stamp settings exceed their supported ranges.");
    }
    check_cancel(cancel);
    double scale = settings.percent / 100.0;
    if (settings.scale == IconStampScale::Fit) {
        scale *= std::min(double(source.width) / stamp.width, double(source.height) / stamp.height);
    }
    const double scaled_width = std::max(1.0, std::round(stamp.width * scale));
    const double scaled_height = std::max(1.0, std::round(stamp.height * scale));
    if (scaled_width > 8192 || scaled_height > 8192 || scaled_width * scaled_height > 16 * 1024 * 1024) {
        throw std::runtime_error("Scaled stamp exceeds 8192 pixels per axis or 16 million pixels.");
    }
    const int width = static_cast<int>(scaled_width), height = static_cast<int>(scaled_height);
    Image resized;
    if (width == stamp.width && height == stamp.height) {
        resized = stamp;
    } else {
        conv_resize(stamp, width, height, resized);
    }
    IconStampResult result;
    result.image = source;
    result.placement = {anchored(source.width, width, settings.anchor % 3) + settings.offset_x,
                        anchored(source.height, height, settings.anchor / 3) + settings.offset_y, width,
                        height};
    result.upscaled = width > stamp.width || height > stamp.height;
    for (int y = 0; y < height; ++y) {
        check_cancel(cancel);
        for (int x = 0; x < width; ++x) {
            Color pixel = resized.get(x, y);
            pixel.a = static_cast<std::uint8_t>(std::lround(pixel.a * settings.opacity / 100.0));
            if (pixel.a == 0) {
                continue;
            }
            const int target_x = result.placement.x + x, target_y = result.placement.y + y;
            if (!source.contains(target_x, target_y)) {
                ++result.clipped_pixels;
                continue;
            }
            if (settings.behind) {
                const Color foreground = source.get(target_x, target_y);
                if (foreground.a == 255) {
                    continue;
                }
                result.image.set(target_x, target_y, pixel);
                result.image.blend(target_x, target_y, foreground);
            } else {
                result.image.blend(target_x, target_y, pixel);
            }
        }
    }
    if (settings.flatten) {
        Color matte = settings.matte;
        matte.a = 255;
        Image opaque;
        opaque.reset(source.width, source.height, matte);
        for (int y = 0; y < source.height; ++y) {
            check_cancel(cancel);
            for (int x = 0; x < source.width; ++x) {
                opaque.blend(x, y, result.image.get(x, y));
            }
        }
        result.image = std::move(opaque);
    }
    return result;
}
} // namespace paint
