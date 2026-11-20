#include "conv.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <stx_conv/stream.hpp>
#include <vector>

namespace {
std::uint8_t quantize(double value) {
    return static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value, 0.0, 1.0)));
}
void compare(int sw, int sh, int width, int height) {
    paint::Image input;
    input.reset(sw, sh);
    std::uint32_t random = 257;
    std::vector<double> nodes(input.pixels.size() * 4);
    for (std::size_t i = 0; i < input.pixels.size(); ++i) {
        random = random * 1664525u + 1013904223u;
        paint::Color color{static_cast<std::uint8_t>(random >> 24), static_cast<std::uint8_t>(random >> 16),
                           static_cast<std::uint8_t>(random >> 8), static_cast<std::uint8_t>(random)};
        input.pixels[i] = color;
        const double a = color.a / 255.0;
        nodes[4 * i] = color.r / 255.0 * a;
        nodes[4 * i + 1] = color.g / 255.0 * a;
        nodes[4 * i + 2] = color.b / 255.0 * a;
        nodes[4 * i + 3] = a;
    }
    paint::Image result = input;
    paint::conv_resize_rapid_area(result, width, height, result);
    if (result.width != width || result.height != height) {
        throw std::runtime_error("Wrong output size");
    }
    const paint::Rect region{width / 3, height / 4, width - width / 3,
                             std::max(1, (height - height / 4) / 2)};
    paint::Image partial;
    paint::conv_resize_rapid_region(input, width, height, region, partial);
    for (int y = 0; y < region.h; ++y) {
        for (int x = 0; x < region.w; ++x) {
            if (!paint::equal(partial.get(x, y), result.get(region.x + x, region.y + y))) {
                throw std::runtime_error("Viewport crop changed target phase");
            }
        }
    }
    if (width == sw && height == sh) {
        if (!std::equal(result.pixels.begin(), result.pixels.end(), input.pixels.begin(), paint::equal)) {
            throw std::runtime_error("No-op changed source bytes");
        }
        return;
    }
    std::vector<double> reference(result.pixels.size() * 4);
    stx_conv::stream::Workspace<double> workspace(sw, sh, width, height, 4, stx_conv::Sampling::pixel_area);
    if (stx_conv::stream::resize<double>(workspace, {nodes, std::size_t(sw) * 4},
                                         {reference, std::size_t(width) * 4}) != stx_conv::Status::ok) {
        throw std::runtime_error("Oracle failed");
    }
    for (std::size_t i = 0; i < result.pixels.size(); ++i) {
        const double a = std::clamp(reference[4 * i + 3], 0.0, 1.0);
        const double inv = a > 1.0 / 65535.0 ? 1.0 / a : 0.0;
        const paint::Color expected{quantize(reference[4 * i] * inv), quantize(reference[4 * i + 1] * inv),
                                    quantize(reference[4 * i + 2] * inv), quantize(a)};
        if (!paint::equal(expected, result.pixels[i])) {
            throw std::runtime_error("Band differs from full stream");
        }
    }
}
} // namespace
int main() {
    constexpr int extents[][4] = {{7, 9, 7, 9},         {1, 1, 137, 257},     {1, 261, 129, 7},
                                  {129, 1, 1, 261},     {4, 3, 137, 129},     {513, 267, 1, 1},
                                  {32, 257, 32, 67},    {257, 32, 67, 32},    {333, 267, 237, 261},
                                  {193, 127, 321, 255}, {513, 257, 257, 513}, {513, 257, 577, 65},
                                  {5, 9, 512, 7},       {5, 3, 197, 3}};
    for (const int (&size)[4] : extents) {
        compare(size[0], size[1], size[2], size[3]);
    }
    std::cout
        << "Rapid resize matches full-image stream for degenerate axes, band seams and aliased input.\n";
}
