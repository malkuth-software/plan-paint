#include "alpha_tools.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void alpha_contracts() {
    paint::Image source;
    source.reset(256, 1);
    for (int x = 0; x < 256; ++x) {
        source.set(x, 0, {97, 31, 213, static_cast<std::uint8_t>(x)});
    }
    const paint::AlphaStatistics stats = paint::alpha_statistics(source);
    require(stats.minimum == 0 && stats.maximum == 255, "alpha extrema");
    const paint::Image mask = paint::alpha_image(source);
    const paint::Image roundtrip = paint::with_alpha_mask(source, mask, false);
    for (int x = 0; x < 256; ++x) {
        require(stats.histogram[x] == 1, "every alpha bin counted");
        require(paint::equal(mask.get(x, 0), {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(x),
                                              static_cast<std::uint8_t>(x), 255}),
                "opaque grayscale copy");
        require(paint::equal(roundtrip.get(x, 0), source.get(x, 0)),
                "exact alpha round trip including hidden RGB");
    }
    paint::Image colored;
    colored.reset(3, 1, {0, 0, 0, 0});
    colored.set(0, 0, {255, 0, 0, 0});
    colored.set(1, 0, {0, 0, 1, 255});
    colored.set(2, 0, {0, 0, 2, 0});
    const paint::Image gray = paint::with_alpha_mask(colored, colored, false);
    require(gray.get(0, 0).a == 85 && gray.get(1, 0).a == 0 && gray.get(2, 0).a == 1,
            "mean uses RGB rounding, ignores mask alpha");
    paint::Image target;
    target.reset(2, 1, {11, 22, 33, 77});
    bool rejected = false;
    try {
        static_cast<void>(paint::with_alpha_mask(target, colored, false));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected && target.get(0, 0).a == 77, "mismatched mask is not implicitly resized");
    const paint::Image resized = paint::with_alpha_mask(target, colored, true);
    require(paint::equal(resized.get(0, 0), {11, 22, 33, 85}) &&
                paint::equal(resized.get(1, 0), {11, 22, 33, 1}),
            "nearest pixel-center mapping");
    paint::Image invalid = target;
    invalid.pixels.pop_back();
    rejected = false;
    try {
        static_cast<void>(paint::alpha_image(invalid));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "invalid geometry rejected before indexing");
}
} // namespace
int main() {
    try {
        alpha_contracts();
        std::cout << "Alpha tools contracts passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
