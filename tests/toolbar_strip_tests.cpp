#include "codecs.hpp"
#include "toolbar_strip.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
void contracts() {
    paint::ImageContainer source;
    paint::IconFrame red, blue;
    red.image.reset(2, 2, {230, 20, 40, 128});
    red.image.set(0, 0, {31, 42, 53, 0});
    blue.image.reset(1, 1, {20, 40, 230, 255});
    source.frames = {red, blue};
    const std::vector<int> order = paint::parse_strip_order(" 2, 1,2 ", 2);
    require(order == std::vector<int>({1, 0, 1}), "explicit order permits repeated images");
    paint::ToolbarStripOptions options;
    options.width = 4;
    options.height = 4;
    options.columns = 2;
    options.gap_x = 3;
    options.gap_y = 2;
    const paint::ToolbarStripPlan plan = paint::plan_toolbar_strip(source, order, options);
    require(plan.width == 11 && plan.height == 10 && plan.cells[1].image.x == 8 && plan.cells[2].image.y == 7,
            "row-major cells include only inter-cell gaps and center artwork");
    paint::Image image = paint::render_toolbar_strip(source, order, options);
    require(paint::equal(image.get(8, 1), red.image.get(0, 0)) &&
                paint::equal(image.get(9, 2), red.image.get(1, 1)),
            "transparent export preserves straight RGBA including hidden RGB");
    require(image.get(5, 5).a == 0 && image.get(10, 9).a == 0,
            "gaps and unused last-row cell are transparent");
    std::vector<std::uint8_t> bytes = paint::encode_png(image);
    paint::Image decoded = paint::decode_image(bytes.data(), bytes.size());
    require(paint::equal(decoded.get(9, 2), red.image.get(1, 1)), "PNG retains partial alpha");
    options.opaque = true;
    options.matte = {10, 50, 90, 12};
    image = paint::render_toolbar_strip(source, order, options);
    require(paint::equal(image.get(8, 1), {10, 50, 90, 255}) && image.get(9, 2).a == 255 &&
                image.get(5, 5).a == 255,
            "matte ignores color alpha and makes transparent artwork and gaps opaque");
    bytes = paint::encode_bmp(image);
    decoded = paint::decode_image(bytes.data(), bytes.size());
    require(paint::equal(decoded.get(9, 2), image.get(9, 2)) &&
                paint::equal(decoded.get(5, 5), image.get(5, 5)),
            "BMP exactly encodes the reviewed matte pixels");
    options.fit = true;
    const paint::ToolbarStripPlan fitted = paint::plan_toolbar_strip(source, order, options);
    require(fitted.cells[0].upscaled && fitted.cells[0].image.w == 4, "fit reports upscaling");
    options.width = 1;
    options.height = 1;
    options.fit = false;
    bool rejected = false;
    try {
        static_cast<void>(paint::plan_toolbar_strip(source, {0}, options));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "actual-pixel mode rejects clipping");
    options.fit = true;
    require(paint::render_toolbar_strip(source, {0}, options).width == 1,
            "explicit fitting allows smaller cells");
    for (const std::string& invalid : {"", "1,", "1,,2", "0", "3", "1.5", "-1", "1 2"}) {
        rejected = false;
        try {
            static_cast<void>(paint::parse_strip_order(invalid, 2));
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "malformed or unavailable ordinals rejected");
    }
    options.width = 4096;
    options.height = 4096;
    options.columns = 256;
    rejected = false;
    try {
        static_cast<void>(paint::plan_toolbar_strip(source, std::vector<int>(256, 0), options));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "oversized output rejected before pixel allocation");
    std::atomic<bool> cancel{true};
    rejected = false;
    try {
        static_cast<void>(paint::render_toolbar_strip(source, order, {}, &cancel));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "cancelled work produces no image");
    source.frames[0].image.set(1, 0, {255, 255, 255, 0});
    source.frames[0].xor_pixels.resize(4);
    source.frames[0].xor_pixels[1] = {255, 255, 255, 255};
    rejected = false;
    try {
        static_cast<void>(paint::plan_toolbar_strip(source, {0}, {}));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "background-dependent legacy XOR cannot silently become RGBA");
}
} // namespace
int main() {
    try {
        contracts();
        std::cout << "Toolbar strip contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
