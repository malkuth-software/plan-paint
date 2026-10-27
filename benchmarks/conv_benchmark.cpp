#include "conv.hpp"
#include "warp.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
paint::Image fixture(int side) {
    paint::Image image;
    image.reset(side, side);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            image.set(x, y,
                      {static_cast<std::uint8_t>((x * 13 + y * 7) % 256),
                       static_cast<std::uint8_t>((x * 3 + y * 11) % 256),
                       static_cast<std::uint8_t>((x / 11 + y / 17) % 2 * 255),
                       static_cast<std::uint8_t>(128 + (x + 3 * y) % 128)});
        }
    }
    return image;
}
std::uint64_t checksum(const paint::Image& image) {
    std::uint64_t hash = 14695981039346656037ull;
    for (paint::Color pixel : image.pixels) {
        for (std::uint8_t component : {pixel.r, pixel.g, pixel.b, pixel.a}) {
            hash = (hash ^ component) * 1099511628211ull;
        }
    }
    return hash;
}
void result(const char* name, std::vector<double>& timings, const paint::Image& output) {
    std::sort(timings.begin(), timings.end());
    std::cout << name << " median_ms=" << timings[timings.size() / 2] << " checksum=" << checksum(output)
              << '\n';
}
void resize_case(const char* name, const paint::Image& source, int width, int height, int repeats) {
    paint::Image output;
    paint::conv_resize(source, width, height, output);
    std::vector<double> timings;
    for (int i = 0; i < repeats; ++i) {
        Clock::time_point start = Clock::now();
        paint::conv_resize(source, width, height, output);
        timings.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    result(name, timings, output);
}
void warp_case(const char* name, const paint::ConvWarpField& field, const paint::AffineMap& map, int side,
               paint::WarpSampling sampling, int repeats) {
    paint::Image output;
    paint::render_affine(field, map, side, side, output, sampling);
    std::vector<double> timings;
    for (int i = 0; i < repeats; ++i) {
        Clock::time_point start = Clock::now();
        paint::render_affine(field, map, side, side, output, sampling);
        timings.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    result(name, timings, output);
}
} // namespace
int main(int argc, char** argv) {
    int repeats = argc > 1 ? std::clamp(std::stoi(argv[1]), 1, 99) : 7;
    std::cout << std::fixed << std::setprecision(3);
    paint::Image medium = fixture(512);
    paint::Image small = fixture(192);
    resize_case("resize_512_to_256", medium, 256, 256, repeats);
    resize_case("resize_width_only", medium, 256, 512, repeats);
    resize_case("resize_height_only", medium, 512, 256, repeats);
    resize_case("resize_192_to_384", small, 384, 384, repeats);
    resize_case("resize_small_width_only", small, 96, 192, repeats);
    paint::ConvWarpField field;
    Clock::time_point start = Clock::now();
    field.compile(small);
    std::cout << "compile_192 elapsed_ms="
              << std::chrono::duration<double, std::milli>(Clock::now() - start).count() << '\n';
    warp_case("warp_point_192", field, {0.98, -0.12, 12, 0.12, 0.98, -8}, 192, paint::WarpSampling::Point,
              repeats);
    warp_case("warp_minify_192_to_96", field, {0.5, 0, 0, 0, 0.5, 0}, 96, paint::WarpSampling::Minification,
              repeats);
    warp_case("warp_area_192", field, {0.98, -0.12, 12, 0.12, 0.98, -8}, 192, paint::WarpSampling::Area,
              repeats);
    return 0;
}
