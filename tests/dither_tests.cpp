#include "codecs.hpp"
#include "dither.hpp"
#include "document.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
bool identical(const paint::Image& a, const paint::Image& b) {
    if (a.width != b.width || a.height != b.height) {
        return false;
    }
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        if (!paint::equal(a.pixels[i], b.pixels[i])) {
            return false;
        }
    }
    return true;
}
double color_error(paint::Color a, paint::Color b) {
    paint::Lab x = paint::to_oklab(a), y = paint::to_oklab(b);
    return (x.l - y.l) * (x.l - y.l) + (x.a - y.a) * (x.a - y.a) + (x.b - y.b) * (x.b - y.b);
}
void palette_regressions() {
    using namespace paint;
    // A fixed-stride sampler used to miss narrow repeating colors completely.
    Image stripes;
    stripes.reset(512, 256, {20, 24, 30, 255});
    for (int y = 0; y < stripes.height; ++y) {
        stripes.set(1, y, {242, 87, 34, 255});
        stripes.set(3, y, {18, 210, 170, 128});
    }
    stripes.set(511, 255, {240, 248, 253, 255});
    for (DitherPattern mode : {DitherPattern::Crosswind, DitherPattern::Weave, DitherPattern::Scrambled,
                               DitherPattern::Drift, DitherPattern::Posterize}) {
        require(identical(stripes, dithered(stripes, {}, {4, mode})),
                "An existing four-color image retains even rare and periodic colors exactly");
    }
    Image ramp;
    ramp.reset(256, 64);
    for (int y = 0; y < ramp.height; ++y) {
        for (int x = 0; x < ramp.width; ++x) {
            std::uint8_t value = static_cast<std::uint8_t>(x);
            ramp.set(x, y, {value, value, value, 255});
        }
    }
    double previous = 1;
    for (int count : {2, 4, 8, 16, 32}) {
        Image poster = dithered(ramp, {}, {count, DitherPattern::Posterize});
        double error = 0;
        for (std::size_t i = 0; i < ramp.pixels.size(); ++i) {
            error += color_error(ramp.pixels[i], poster.pixels[i]) / ramp.pixels.size();
        }
        require(error < previous * .4, "Additional poster colors resolve a grayscale ramp");
        previous = error;
    }
    require(previous < .0001, "Posterization retains fine tonal resolution at 32 colors");
    for (DitherPattern mode :
         {DitherPattern::Crosswind, DitherPattern::Weave, DitherPattern::Scrambled, DitherPattern::Drift}) {
        Image output = dithered(ramp, {}, {8, mode});
        double squared = 0;
        for (int x = 0; x < 256; x += 16) {
            double difference = 0;
            for (int y = 0; y < 64; ++y) {
                for (int xx = x; xx < x + 16; ++xx) {
                    Lab before = to_oklab(ramp.get(xx, y)), after = to_oklab(output.get(xx, y));
                    difference += (before.l - after.l) / 1024;
                    require(std::abs(after.a) < .002 && std::abs(after.b) < .002,
                            "Neutral artwork does not acquire colored dither noise");
                }
            }
            squared += difference * difference / 16;
        }
        require(std::sqrt(squared) < .018, "Dither preserves local perceived tone across a ramp");
    }
}
void checks() {
    using namespace paint;
    for (DitherPattern pattern : {DitherPattern::Weave, DitherPattern::Scrambled, DitherPattern::Drift}) {
        std::array<int, 4096> counts{};
        for (int y = -64; y < 0; ++y) {
            for (int x = 128; x < 192; ++x) {
                ++counts[dither_rank(x, y, pattern)];
            }
        }
        for (int n : counts) {
            require(n == 1, "Every Walsh tile has balanced threshold ranks");
        }
    }
    Image source;
    source.reset(31, 27);
    std::vector<std::uint8_t> mask(source.pixels.size());
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            source.set(x, y,
                       {static_cast<std::uint8_t>(x * 8), static_cast<std::uint8_t>(y * 9),
                        static_cast<std::uint8_t>((x * 13 + y * 11) % 256),
                        static_cast<std::uint8_t>(x % 7 ? 128 : 0)});
            mask[static_cast<std::size_t>(y) * source.width + x] = (x < 10 || x > 20) && y > 3;
        }
    }
    for (DitherPattern pattern : {DitherPattern::Crosswind, DitherPattern::Weave, DitherPattern::Scrambled,
                                  DitherPattern::Drift, DitherPattern::Posterize}) {
        std::vector<Color> palette;
        Image output = dithered(source, mask, {8, pattern}, &palette);
        require(!palette.empty() && palette.size() <= 8, "Requested palette size is respected");
        for (std::size_t i = 0; i < source.pixels.size(); ++i) {
            require(output.pixels[i].a == source.pixels[i].a, "Alpha is unchanged");
            if (!mask[i] || !source.pixels[i].a) {
                require(equal(output.pixels[i], source.pixels[i]),
                        "Selection holes and hidden RGB are unchanged");
            } else {
                bool found = false;
                for (Color c : palette) {
                    if (c.r == output.pixels[i].r && c.g == output.pixels[i].g && c.b == output.pixels[i].b) {
                        found = true;
                    }
                }
                require(found, "Every processed RGB belongs to the palette");
            }
        }
        require(identical(output, dithered(source, mask, {8, pattern})), "Deterministic dithering");
        Image flat;
        flat.reset(9, 9, {57, 83, 129, 211});
        require(identical(flat, dithered(flat, {}, {8, pattern})), "An exact constant color is preserved");
    }
    std::atomic<bool> cancel{true};
    require(dithered(source, mask, {8, DitherPattern::Scrambled}, nullptr, &cancel).pixels.empty(),
            "Canceled work is not published");
    bool rejected = false;
    try {
        static_cast<void>(dithered(source, std::span(mask).first(3), {}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "Invalid mask rejected");
    for (DitherBrushMode mode : {DitherBrushMode::Neighborhood, DitherBrushMode::Noise}) {
        Image painted = source;
        DitherBrushStroke brush;
        brush.segment(painted, {0, 12}, {30, 12}, 24, mode, 782, false, mask);
        for (std::size_t i = 0; i < source.pixels.size(); ++i) {
            require(painted.pixels[i].a == source.pixels[i].a, "Brush preserves alpha");
            if (!mask[i] || !source.pixels[i].a) {
                require(equal(painted.pixels[i], source.pixels[i]), "Brush respects holes and transparency");
            }
        }
        Image repeated = source;
        brush.clear();
        brush.segment(repeated, {0, 12}, {30, 12}, 24, mode, 782, false, mask);
        require(identical(repeated, painted), "Brush seeded determinism");
        require(!identical(source, painted), "Brush actually modifies selected artwork");
    }
    Image flat;
    flat.reset(65, 65, {120, 120, 120, 177});
    Image unchanged = flat;
    DitherBrushStroke brush;
    brush.segment(flat, {0, 32}, {64, 32}, 32, DitherBrushMode::Neighborhood, 11);
    require(identical(flat, unchanged), "Neighborhood midpoint leaves constant material unchanged");
    // Measure center falloff over many independent noise dabs, normalized by area.
    double inner = 0, outer = 0, inner_sites = 0, outer_sites = 0;
    for (int seed = 0; seed < 32; ++seed) {
        Image image = unchanged;
        brush.clear();
        brush.segment(image, {32, 32}, {32, 32}, 64, DitherBrushMode::Noise,
                      static_cast<std::uint32_t>(seed));
        for (int y = 0; y < 65; ++y) {
            for (int x = 0; x < 65; ++x) {
                double r = std::hypot(x - 32, y - 32) / 32;
                bool changed = !equal(image.get(x, y), unchanged.get(x, y));
                if (r < .4) {
                    inner += changed;
                    inner_sites += 1;
                }
                if (r > .75 && r < 1) {
                    outer += changed;
                    outer_sites += 1;
                }
            }
        }
    }
    require(inner / inner_sites > 4 * outer / outer_sites, "Brush is substantially denser near its center");
    Document document;
    document.image = source;
    document.checkpoint();
    document.image = dithered(source, mask, {});
    Image edited = document.image;
    document.undo();
    require(identical(source, document.image), "Dither undo restores exact original");
    document.redo();
    require(identical(edited, document.image), "Dither redo restores exact result");
}
} // namespace
int main(int argc, char** argv) {
    try {
        checks();
        palette_regressions();
        if (argc == 3) {
            paint::Image source = paint::load_image(argv[1]);
            for (int mode = 0; mode < 5; ++mode) {
                std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
                paint::Image result =
                    paint::dithered(source, {}, {16, static_cast<paint::DitherPattern>(mode)});
                double ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count();
                paint::save_image(result, std::string(argv[2]) + "-" + std::to_string(mode) + ".png");
                std::cout << mode << "," << ms << " ms\n";
            }
            for (int mode = 0; mode < 2; ++mode) {
                paint::Image result = source;
                paint::DitherBrushStroke brush;
                for (int y = 12; y < source.height; y += 16) {
                    brush.segment(result, {0, static_cast<double>(y)},
                                  {static_cast<double>(source.width - 1), static_cast<double>(y)}, 40,
                                  static_cast<paint::DitherBrushMode>(mode), 54);
                }
                paint::save_image(result, std::string(argv[2]) + "-brush-" + std::to_string(mode) + ".png");
            }
        }
        std::cout << "Dither tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
