#include "carpet.hpp"
#include "codecs.hpp"
#include "paint_tools.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
std::uint64_t checksum(const paint::Image& image) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (paint::Color c : image.pixels) {
        for (std::uint8_t v : {c.r, c.g, c.b, c.a}) {
            hash = (hash ^ v) * 1099511628211ULL;
        }
    }
    return hash;
}
void controls_and_deposition() {
    paint::CarpetParameters base = paint::carpet_preset(3);
    const paint::Image reference = paint::render_carpet(base, 64, 64);
    const std::uint64_t original = checksum(reference);
    require(checksum(paint::render_carpet(base, 64, 64)) == original, "seed is not deterministic");
    for (int i = 0; i < 12; ++i) {
        paint::CarpetParameters p = base;
        switch (i) {
        case 0:
            p.web = .2;
            break;
        case 1:
            p.radius = .015;
            break;
        case 2:
            p.height = 1.1;
            break;
        case 3:
            p.disorder = 1.1;
            break;
        case 4:
            p.crimp = 1.2;
            break;
        case 5:
            p.roughness = .7;
            break;
        case 6:
            p.dye = 1.9;
            break;
        case 7:
            p.light = 20;
            break;
        case 8:
            p.view = 40;
            break;
        case 9:
            p.magnification = 1.5;
            break;
        case 10:
            p.hillshade = true;
            break;
        case 11:
            ++p.seed;
            break;
        }
        require(checksum(paint::render_carpet(p, 64, 64)) != original,
                "carpet control has no rendered effect");
    }
    paint::Image whole, split;
    whole.reset(96, 48, {20, 30, 40, 255});
    split = whole;
    paint::CarpetStroke a, b;
    a.segment(whole, {10, 24}, {85, 24}, 20, reference, .6, false);
    for (int x = 11; x <= 85; ++x) {
        b.segment(split, {static_cast<double>(x - 1), 24}, {static_cast<double>(x), 24}, 20, reference, .6,
                  false);
    }
    require(checksum(whole) == checksum(split), "carpet deposition depends on mouse event frequency");
    a.segment(whole, {85, 24}, {10, 24}, 20, reference, .6, false);
    require(checksum(whole) == checksum(split), "carpet revisits multiply one coat");
    paint::CarpetParameters invalid = base;
    invalid.radius = std::numeric_limits<double>::quiet_NaN();
    bool rejected = false;
    try {
        paint::render_carpet(invalid, 32, 32);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "nonfinite carpet geometry accepted");
}
} // namespace
int main(int argc, char** argv) {
    try {
        const std::array<double, 9> flat{}, east{0, 1, 2, 0, 1, 2, 0, 1, 2}, south{0, 0, 0, 1, 1, 1, 2, 2, 2};
        require(std::abs(paint::horn_hillshade(flat, 1, 1, 45, 315) - std::sqrt(.5)) < 1e-12,
                "flat hillshade");
        require(paint::horn_hillshade(east, 1, 1, 45, 90) < 1e-12, "east slope opposite lighting");
        require(std::abs(paint::horn_hillshade(east, 1, 1, 45, 270) - 1) < 1e-12, "west illumination");
        require(std::abs(paint::horn_hillshade(south, 1, 1, 45, 0) - 1) < 1e-12, "north illumination");
        std::atomic<bool> cancel{true};
        bool cancelled = false;
        try {
            paint::render_carpet(paint::carpet_preset(0), 32, 32, &cancel);
        } catch (const std::runtime_error&) {
            cancelled = true;
        }
        require(cancelled, "cancelled generator continued");
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        const paint::Image image = paint::render_carpet(paint::carpet_preset(0), 256, 256);
        std::cout << "Royal velvet 256x256 native: "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                  << " seconds\n";
        require(image.width == 256 && image.height == 256, "render dimensions");
        controls_and_deposition();
        if (argc > 1) {
            const std::filesystem::path root = argv[1];
            std::filesystem::create_directories(root);
            paint::save_image(paint::render_carpet(paint::carpet_preset(0), 660, 440),
                              (root / "royal.png").string());
            for (int i = 1; i < paint::carpet_preset_count; ++i) {
                const paint::Image tile = paint::render_carpet_tile(paint::carpet_preset(i));
                paint::save_image(tile, (root / (std::to_string(i) + ".png")).string());
            }
            paint::CarpetParameters shaded = paint::carpet_preset(3);
            shaded.hillshade = true;
            paint::save_image(paint::render_carpet_tile(shaded), (root / "hillshade.png").string());
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
