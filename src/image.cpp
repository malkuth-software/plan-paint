#include "image.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace paint {
bool equal(Color left, Color right) {
    return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}
static double linear_channel(std::uint8_t channel) {
    double value = static_cast<double>(channel) / 255.0;
    if (value <= 0.04045) {
        return value / 12.92;
    }
    double linear = std::pow((value + 0.055) / 1.055, 2.4);
    return linear;
}
static std::uint8_t encoded_channel(double linear) {
    linear = std::clamp(linear, 0.0, 1.0);
    double value = 12.92 * linear;
    if (linear > 0.0031308) {
        value = 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    }
    std::uint8_t result = static_cast<std::uint8_t>(std::round(255.0 * value));
    return result;
}
Lab to_oklab(Color color) {
    double r = linear_channel(color.r), g = linear_channel(color.g), b = linear_channel(color.b);
    double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    Lab result{0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
               1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
               0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
    return result;
}
Color from_oklab(Lab lab) {
    double l = lab.l + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
    double m = lab.l - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
    double s = lab.l - 0.0894841775 * lab.a - 1.2914855480 * lab.b;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    Color result{encoded_channel(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
                 encoded_channel(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
                 encoded_channel(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s), 255};
    return result;
}
std::string to_hex(Color color) {
    char buffer[10] = {};
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color.r, color.g, color.b);
    std::string result(buffer);
    return result;
}
bool from_hex(const std::string& text, Color& color) {
    std::string digits = text;
    if (digits.starts_with('#')) {
        digits.erase(0, 1);
    }
    if (digits.size() != 6) {
        return false;
    }
    for (char digit : digits) {
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') ||
              (digit >= 'A' && digit <= 'F'))) {
            return false;
        }
    }
    unsigned long value = std::strtoul(digits.c_str(), nullptr, 16);
    color = {static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 8),
             static_cast<std::uint8_t>(value), 255};
    return true;
}
void Image::reset(int new_width, int new_height, Color fill) {
    if (new_width < 1 || new_height < 1 || new_width > 32768 || new_height > 32768) {
        throw std::invalid_argument("Image dimensions must be 1 to 32768 pixels.");
    }
    std::size_t count = static_cast<std::size_t>(new_width) * static_cast<std::size_t>(new_height);
    if (count > 100000000) {
        throw std::invalid_argument("This image exceeds the 100 megapixel working limit.");
    }
    std::vector<Color> replacement(count, fill);
    pixels.swap(replacement);
    width = new_width;
    height = new_height;
}
bool Image::contains(int x, int y) const {
    return x >= 0 && y >= 0 && x < width && y < height;
}
Color Image::get(int x, int y) const {
    if (!contains(x, y)) {
        return {0, 0, 0, 0};
    }
    return pixels[static_cast<std::size_t>(y) * width + x];
}
void Image::set(int x, int y, Color color) {
    if (!contains(x, y)) {
        return;
    }
    pixels[static_cast<std::size_t>(y) * width + x] = color;
}
void Image::blend(int x, int y, Color color) {
    if (!contains(x, y) || color.a == 0) {
        return;
    }
    if (color.a == 255) {
        set(x, y, color);
        return;
    }
    Color back = get(x, y);
    double alpha = color.a / 255.0;
    double remaining = back.a / 255.0 * (1.0 - alpha);
    double total = alpha + remaining;
    Color result{static_cast<std::uint8_t>(std::round((color.r * alpha + back.r * remaining) / total)),
                 static_cast<std::uint8_t>(std::round((color.g * alpha + back.g * remaining) / total)),
                 static_cast<std::uint8_t>(std::round((color.b * alpha + back.b * remaining) / total)),
                 static_cast<std::uint8_t>(std::round(total * 255.0))};
    set(x, y, result);
}
Image cropped(const Image& source, Rect bounds) {
    Image result;
    result.reset(bounds.w, bounds.h, {0, 0, 0, 0});
    for (int y = 0; y < bounds.h; ++y) {
        for (int x = 0; x < bounds.w; ++x) {
            result.set(x, y, source.get(x + bounds.x, y + bounds.y));
        }
    }
    return result;
}
void composite(Image& destination, const Image& source, int x, int y) {
    for (int row = 0; row < source.height; ++row) {
        for (int col = 0; col < source.width; ++col) {
            destination.blend(x + col, y + row, source.get(col, row));
        }
    }
}
} // namespace paint
