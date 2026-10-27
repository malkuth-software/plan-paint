#include "document.hpp"
#include "gradient.hpp"
#include "guide.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void sampling() {
    using namespace paint;
    const Color red{231, 32, 51, 255}, blue{20, 80, 220, 255}, green{35, 210, 80, 255};
    Gradient gradient{{{0, red}, {.3, green}, {1, blue}}};
    GradientSampler sampler(gradient, {10, 20, 101, 51});
    require(equal(sampler.sample(10, 20), red) && equal(sampler.sample(110, 70), blue),
            "Linear spans region bounds");
    require(equal(sampler.sample(40, 45), green), "Nonuniform interior stop has its exact color");
    require(equal(sampler.sample(60, 20), sampler.sample(60, 70)), "Zero degree gradient is horizontal");
    gradient.angle = 90;
    sampler = GradientSampler(gradient, {10, 20, 101, 51});
    require(equal(sampler.sample(60, 20), red) && equal(sampler.sample(60, 70), blue),
            "Ninety degrees runs top to bottom");
    gradient.angle = 45;
    sampler = GradientSampler(gradient, {10, 20, 101, 51});
    require(equal(sampler.sample(10, 20), red) && equal(sampler.sample(110, 70), blue),
            "Diagonal reaches both stops");
    gradient.kind = GradientKind::Radial;
    sampler = GradientSampler(gradient, {10, 20, 101, 51});
    require(equal(sampler.sample(60, 45), red) && equal(sampler.sample(110, 70), blue),
            "Circular gradient spans center to corner");
    require(equal(sampler.sample(80, 45), sampler.sample(60, 65)),
            "Radial distance is circular even in a wide region");
    gradient.stops = {{0, red}, {.5, red}, {.5, blue}, {1, blue}};
    sampler = GradientSampler(gradient, {0, 0, 100, 100});
    require(equal(sampler.at(.4999), red) && equal(sampler.at(.5), blue),
            "Coincident stops form a sharp transition");
    gradient.stops = {{0, {255, 0, 0, 0}}, {1, blue}};
    sampler = GradientSampler(gradient, {0, 0, 100, 100});
    Color middle = sampler.at(.5);
    require(middle.r == blue.r && middle.g == blue.g && middle.b == blue.b && middle.a == 128,
            "Transparent hidden color does not bleed into visible interpolation");
    gradient.stops.clear();
    gradient.angle = std::numeric_limits<double>::quiet_NaN();
    normalize_gradient(gradient);
    require(gradient.stops.size() == 2 && gradient.angle == 0,
            "Empty and nonfinite gradients normalize safely");
    gradient.stops = {{0, red}, {.5, green}, {1, blue}};
    const std::size_t index = move_gradient_stop(gradient, 0, .75);
    require(index == 1 && equal(gradient.stops[index].color, red),
            "Dragging across another stop preserves selected color");
    require(gradient_presets().size() >= 12, "Preset gallery includes twelve distinct families");
    for (const GradientPreset& preset : gradient_presets()) {
        require(preset.gradient.stops.size() >= 2 && preset.gradient.stops.size() <= 32,
                "All presets have bounded multi-stop support");
    }
}
void connected_regions() {
    using namespace paint;
    const Color white{255, 255, 255, 255}, black{0, 0, 0, 255};
    Gradient gradient{{{0, {230, 20, 40, 255}}, {1, {20, 70, 220, 255}}}};
    Ink ink;
    Guide guide;
    Image image;
    image.reset(20, 16);
    for (int y = 0; y < 16; ++y) {
        image.set(9, y, black);
    }
    stencil_flood(image, {2, 2}, ink, guide, false, nullptr, &gradient);
    require(equal(image.get(0, 0), gradient.stops.front().color) &&
                equal(image.get(8, 15), gradient.stops.back().color),
            "Gradient fits the connected region rather than whole canvas");
    require(equal(image.get(9, 5), black) && equal(image.get(10, 5), white),
            "Gradient flood respects a one-pixel barrier");
    Document document;
    document.new_image(20, 16);
    std::vector<std::uint8_t> mask(320, 0);
    for (int y = 3; y < 13; ++y) {
        for (int x = 2; x < 18; ++x) {
            if (!(x >= 7 && x < 12 && y >= 6 && y < 10)) {
                mask[y * 20 + x] = 255;
            }
        }
    }
    document.select_mask({{0, 0, 20, 16}, mask, {}});
    document.settle_selection();
    stencil_flood(document.image, {3, 4}, ink, guide, false, &document.selection, &gradient);
    require(equal(document.image.get(2, 3), gradient.stops.front().color) &&
                equal(document.image.get(17, 12), gradient.stops.back().color),
            "Selection bounds determine gradient extent");
    require(equal(document.image.get(9, 8), white) && equal(document.image.get(0, 0), white),
            "Selection holes and exterior survive");
    document.image.reset(20, 16);
    document.selection.canvas_selection = false;
    stencil_flood(document.image, {0, 0}, ink, guide, false, &document.selection, &gradient);
    require(!equal(document.image.get(9, 8), white) && !equal(document.image.get(19, 15), white),
            "Floating paste does not constrain the bucket");
    guide.nodes = {{5, 4}, {14, 4}, {14, 12}, {5, 12}};
    guide.closed = true;
    guide.fill = false;
    image.reset(20, 16);
    stencil_flood(image, {0, 0}, ink, guide, false, nullptr, &gradient);
    require(equal(image.get(9, 8), white) && !equal(image.get(0, 0), white),
            "Guide outline blocks gradient flood");
    guide.clear();
    image.reset(20, 16, black);
    for (int y = 0; y < 16; ++y) {
        image.set(0, y, white);
        image.set(19, y, white);
    }
    stencil_flood(image, {0, 0}, ink, guide, true, nullptr, &gradient);
    require(equal(image.get(19, 8), gradient.stops.back().color) && equal(image.get(1, 8), black),
            "Wrapped bucket reaches matching opposite-edge pixels");
}
} // namespace
int main() {
    try {
        sampling();
        connected_regions();
        std::cout << "Gradient sampling, alpha, stops, selection, guides and wrapped regions passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
