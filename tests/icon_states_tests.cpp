#include "codecs.hpp"
#include "fixtures/icon_reference.hpp"
#include "icon_palette.hpp"
#include "icon_states.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
paint::IconFrame source_frame() {
    paint::IconFrame frame;
    frame.image.reset(2, 2, {255, 0, 0, 255});
    frame.image.set(0, 0, {255, 0, 0, 128});
    frame.image.set(1, 0, {17, 39, 61, 0});
    frame.hotspot_x = 1;
    frame.hotspot_y = 1;
    return frame;
}
void state_pixels_and_ownership() {
    const paint::IconFrame frame = source_frame();
    const paint::ImageContainer source{paint::ContainerKind::Cursor, {frame}};
    paint::IconStateRecipe recipe = paint::icon_state_preset(paint::IconStatePreset::Normal);
    const paint::IconStateResult normal = paint::generate_icon_state(source, {0}, recipe);
    require(paint::icon_state_ready(normal) && normal.output.kind == paint::ContainerKind::Cursor,
            "normal cursor result is ready");
    require(std::equal(frame.image.pixels.begin(), frame.image.pixels.end(),
                       normal.output.frames[0].image.pixels.begin(), paint::equal),
            "normal state preserves pixels including hidden RGB");
    recipe = paint::icon_state_preset(paint::IconStatePreset::Disabled);
    const paint::IconStateResult disabled = paint::generate_icon_state(source, {0}, recipe);
    const paint::IconFrame& output = disabled.output.frames[0];
    require(paint::equal(output.image.get(0, 0), {54, 54, 54, 70}) &&
                paint::equal(output.image.get(1, 0), {17, 39, 61, 0}),
            "disabled transforms color and opacity while retaining fully hidden RGB");
    require(output.hotspot_x == 1 && output.hotspot_y == 1 &&
                paint::equal(source.frames[0].image.get(0, 0), {255, 0, 0, 128}),
            "state output keeps cursor hotspot and does not modify source");
    const std::vector<std::uint8_t> encoded = paint::encode_icon_container(disabled.output);
    const paint::ImageContainer decoded = paint::decode_icon_container(encoded.data(), encoded.size());
    require(decoded.kind == paint::ContainerKind::Cursor && decoded.frames[0].hotspot_x == 1 &&
                paint::equal(decoded.frames[0].image.get(0, 0), {54, 54, 54, 70}),
            "state cursor output retains pixels and hotspot through standard CUR encoding");
    const paint::IconStateResult repeated = paint::generate_icon_state(source, {0}, recipe);
    require(paint::encode_icon_container(repeated.output) == encoded, "state output is deterministic");
    recipe = paint::icon_state_preset(paint::IconStatePreset::Hot);
    const paint::IconStateResult hot = paint::generate_icon_state(source, {0}, recipe);
    require(paint::equal(hot.output.frames[0].image.get(0, 1), {255, 15, 15, 255}) &&
                hot.output.frames[0].image.get(0, 0).a == 128,
            "hot clips saturated colors and preserves source alpha");
    recipe.brightness = -100;
    recipe.saturation = 0;
    recipe.opacity = 0;
    const paint::IconStateResult zero = paint::generate_icon_state(source, {0}, recipe);
    require(paint::equal(zero.output.frames[0].image.get(0, 0), {0, 0, 0, 0}),
            "explicit zero opacity and minimum brightness produce transparent black");
}
void mixed_depth_decisions() {
    paint::ImageContainer source =
        paint::decode_icon_container(paint::test::indexed_icon, sizeof(paint::test::indexed_icon));
    require(paint::icon_output_format(source.frames[0], source.kind).bits_per_pixel == 2,
            "reference fixture exercises actual 2-bit PNG input");
    for (unsigned depth : {1u, 4u, 8u}) {
        paint::IconPaletteOptions options;
        options.bits_per_pixel = depth;
        source.frames.push_back(paint::convert_icon_palette(source_frame(), options).frame);
    }
    source.frames.push_back(source_frame());
    for (int index = 0; index < 5; ++index) {
        source.frames.push_back(source.frames[index]);
    }
    const std::vector<int> selection{9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
    paint::IconStateRecipe recipe = paint::icon_state_preset(paint::IconStatePreset::Disabled);
    recipe.palette = paint::IconStatePalette::IndexedDib;
    const paint::IconStateResult result = paint::generate_icon_state(source, selection, recipe);
    require(paint::icon_state_ready(result) && result.entries.size() == 10 &&
                result.output.frames.size() == 10,
            "ten mixed-depth inputs yield ten output decisions");
    for (std::size_t index = 0; index < selection.size(); ++index) {
        const paint::IconStateEntry& entry = result.entries[index];
        const unsigned source_depth =
            paint::icon_output_format(source.frames[selection[index]], source.kind).bits_per_pixel;
        const unsigned expected_depth = source_depth == 2 ? 4 : source_depth;
        require(entry.source_index == selection[index] && entry.output_index == static_cast<int>(index) &&
                    entry.source_format.bits_per_pixel == source_depth &&
                    entry.output_format.bits_per_pixel == expected_depth,
                "selection order and source-to-output depth mapping are explicit");
        if (source_depth <= 8) {
            require(entry.partial_alpha_converted > 0, "indexed recipe reports partial alpha conversion");
        }
    }
    const paint::IconStateResult duplicate = paint::generate_icon_state(source, {0, 0}, recipe);
    require(duplicate.output.frames.size() == 2 && duplicate.entries[1].source_index == 0,
            "duplicate selected representations are not silently collapsed");
    recipe.palette = paint::IconStatePalette::Rgba;
    const paint::IconStateResult rgba = paint::generate_icon_state(source, selection, recipe);
    for (const paint::IconStateEntry& entry : rgba.entries) {
        require(entry.output_format.bits_per_pixel == 32 && entry.partial_alpha_converted == 0,
                "RGBA policy preserves computed opacity for every source depth");
    }
    const std::string report = paint::icon_state_report(result);
    require(report.find("plan-paint-icon-state-v1") != std::string::npos &&
                report.find("\"sourceBpp\":2,\"outputBpp\":4") != std::string::npos &&
                report.find("indexed-threshold128-white-no-dither") != std::string::npos,
            "report identifies recipe version, actual policy and 2-to-4-bit conversion");
}
void failed_and_cancelled_decisions() {
    paint::IconFrame xor_frame = source_frame();
    xor_frame.image.set(0, 0, {255, 255, 255, 0});
    xor_frame.xor_pixels.resize(4, {255, 255, 255, 255});
    const paint::ImageContainer source{paint::ContainerKind::Icon, {source_frame(), xor_frame}};
    paint::IconStateRecipe recipe = paint::icon_state_preset(paint::IconStatePreset::Disabled);
    const paint::IconStateResult failed = paint::generate_icon_state(source, {0, 1, 0}, recipe);
    require(!paint::icon_state_ready(failed) && failed.entries.size() == 3 &&
                failed.entries[0].status == paint::IconStateStatus::Ready &&
                failed.entries[1].status == paint::IconStateStatus::Failed &&
                failed.entries[1].output_index == -1 && failed.entries[2].output_index == 1,
            "one XOR failure blocks export readiness without hiding other decisions");
    require(paint::icon_state_report(failed).find("\"ready\": false") != std::string::npos,
            "failed family cannot be reported ready");
    const std::atomic<bool> cancel{true};
    const paint::IconStateResult cancelled = paint::generate_icon_state(source, {0, 1, 0}, recipe, &cancel);
    require(!paint::icon_state_ready(cancelled) && cancelled.output.frames.empty(),
            "cancelled generation publishes no image");
    for (const paint::IconStateEntry& entry : cancelled.entries) {
        require(entry.status == paint::IconStateStatus::Cancelled && entry.output_index == -1,
                "cancelled generation retains a decision for every requested image");
    }
    for (double saturation :
         {-1.0, 201.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        recipe.saturation = saturation;
        bool rejected = false;
        try {
            static_cast<void>(paint::generate_icon_state(source, {0}, recipe));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "invalid numerical recipes are rejected");
    }
}
} // namespace
int main() {
    try {
        state_pixels_and_ownership();
        mixed_depth_decisions();
        failed_and_cancelled_decisions();
        std::cout << "Icon state contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
