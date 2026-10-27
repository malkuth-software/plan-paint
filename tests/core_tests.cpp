#include "codecs.hpp"
#include "color_tools.hpp"
#include "conv.hpp"
#include "desktop.hpp"
#include "document.hpp"
#include "fixtures/conv_reference.hpp"
#include "guide.hpp"
#include "material.hpp"
#include "paint_tools.hpp"
#include "safe_file.hpp"
#include "text_session.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <set>
#include <stdexcept>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#else
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
void test_icon_size_production() {
    paint::ImageContainer original;
    original.kind = paint::ContainerKind::Cursor;
    for (int size : {16, 16, 32, 48}) {
        paint::IconFrame frame;
        frame.image.reset(size, size, {static_cast<std::uint8_t>(size), 80, 120, 173});
        frame.hotspot_x = size / 2;
        frame.hotspot_y = size / 4;
        original.frames.push_back(frame);
    }
    original.frames[1].image.set(0, 0, {255, 255, 255, 0});
    original.frames[1].xor_pixels.resize(16 * 16);
    original.frames[1].xor_pixels[0] = {255, 255, 255, 255};
    paint::Document document;
    document.replace_container(original, "production.cur");
    document.atlas_select(2);
    document.image.set(4, 4, {210, 30, 70, 99});
    const paint::Image edited_source = document.image;
    const paint::IconSizePlan additions =
        paint::plan_icon_sizes(original.frames, {16, 24, 24, 32}, paint::IconSizePolicy::AddMissing);
    require(additions.kept == 4 && additions.added == 1 && additions.replaced == 0 && additions.removed == 0,
            "add-missing plan preserves duplicate representations and deduplicates requested sizes");
    document.make_icon_sizes({16, 24, 24, 32}, true);
    require(document.atlas.count() == 5 && document.atlas.active == 2 && document.undo_history.size() == 1,
            "generation preserves the current image and creates one transaction");
    require(paint::equal(document.atlas.icons[2].image.get(4, 4), edited_source.get(4, 4)) &&
                paint::legacy_xor_pixel(document.atlas.icons[1], 0),
            "add-missing preserves pending pixels and untouched legacy representations");
    require(document.atlas.icons[4].image.width == 24 && document.atlas.icons[4].hotspot_x == 12 &&
                document.atlas.icons[4].hotspot_y == 6,
            "generated cursor hotspot follows source pixel centers");
    const std::uint64_t revision = document.revision;
    document.make_icon_sizes({16, 24, 32}, true);
    require(document.revision == revision && document.undo_history.size() == 1,
            "already-complete add-missing request is a true no-op");
    document.undo();
    require(document.atlas.count() == 4 && document.atlas.active == 2 &&
                paint::equal(document.image.get(4, 4), edited_source.get(4, 4)),
            "one Undo restores all original images and the unsynchronized current source");
    document.redo();
    require(document.atlas.count() == 5 && document.atlas.icons[4].image.width == 24,
            "Redo restores generated images");
    const paint::IconSizePlan exact =
        paint::plan_icon_sizes(document.atlas.icons, {16, 32}, paint::IconSizePolicy::ExactProfile);
    require(exact.kept == 3 && exact.removed == 2 && exact.added == 0,
            "exact profile removes only off-profile dimensions, preserving same-size variants");
    document.make_icon_sizes({16, 32}, true, paint::IconSizePolicy::ExactProfile);
    require(document.atlas.count() == 3 && paint::legacy_xor_pixel(document.atlas.icons[1], 0),
            "exact profile retains legacy data for matching representations");
    document.make_icon_sizes({16}, true, paint::IconSizePolicy::ReplaceSelected);
    require(document.atlas.count() == 3 && document.atlas.icons[0].xor_pixels.empty() &&
                document.atlas.icons[1].xor_pixels.empty() && document.atlas.active == 2 &&
                paint::equal(document.atlas.icons[2].image.get(4, 4), edited_source.get(4, 4)),
            "replacement affects all chosen-size entries while preserving other artwork");
    const std::size_t history = document.undo_history.size();
    bool rejected = false;
    try {
        document.make_icon_sizes({24, 257}, true);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && document.atlas.count() == 3 && document.undo_history.size() == history,
            "invalid later target cannot partially mutate a document");
    rejected = false;
    try {
        document.make_icon_sizes({}, true, paint::IconSizePolicy::ExactProfile);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && document.atlas.count() == 3, "empty exact profile cannot delete every image");
    document.replace_container(original, "legacy.cur");
    document.atlas_select(1);
    rejected = false;
    try {
        document.make_icon_sizes({24}, true);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && document.atlas.count() == 4 && document.undo_history.empty(),
            "background-dependent XOR source is rejected before history or data mutation");
    document.make_icon_sizes({16}, true, paint::IconSizePolicy::ExactProfile);
    require(document.atlas.count() == 2 && paint::legacy_xor_pixel(document.atlas.icons[1], 0),
            "exact pruning without resampling works for a legacy source");
    const paint::IconFrame repeated = original.frames[0];
    original.frames.assign(256, repeated);
    document.replace_container(original, "full.cur");
    rejected = false;
    try {
        document.make_icon_sizes({24}, true);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && document.atlas.count() == 256 && document.undo_history.empty(),
            "generation beyond the container entry limit must fail before changing the document");
}
void test_explicit_icon_targets() {
    paint::ImageContainer source;
    source.kind = paint::ContainerKind::Cursor;
    for (paint::Color color : {paint::Color{220, 20, 10, 255}, paint::Color{10, 30, 220, 255}}) {
        paint::IconFrame frame;
        frame.image.reset(16, 16, color);
        frame.hotspot_x = 8;
        frame.hotspot_y = 4;
        source.frames.push_back(frame);
    }
    paint::Document document;
    document.replace_container(source, "targets.cur");
    document.atlas_select(0);
    const std::vector<paint::IconGenerationTarget> targets = {
        {1, 0, 16, 16, 32}, {0, 1, 16, 16, 32}, {0, -1, 20, 12, 4}, {1, -1, 20, 12, 8}};
    document.generate_icon_targets(targets, {}, true);
    require(document.atlas.count() == 4 && document.undo_history.size() == 1 && document.atlas.active == 0,
            "explicit generation is one transaction and retains the current representation");
    require(paint::equal(document.atlas.icons[0].image.get(3, 3), source.frames[1].image.get(3, 3)) &&
                paint::equal(document.atlas.icons[1].image.get(3, 3), source.frames[0].image.get(3, 3)),
            "cross-replacements must read original sources, never an earlier generated image");
    const std::vector<std::uint8_t> bytes = paint::encode_icon_container(document.output_container(true));
    const paint::ImageContainer decoded = paint::decode_icon_container(bytes.data(), bytes.size());
    require(decoded.frames[2].image.width == 20 && decoded.frames[2].image.height == 12 &&
                (*decoded.frames[2].source).format.bits_per_pixel == 4 &&
                (*decoded.frames[3].source).format.bits_per_pixel == 8 &&
                decoded.frames[2].hotspot_x == 10 && decoded.frames[2].hotspot_y == 3,
            "custom rectangular legacy variants and scaled cursor hotspots survive file round trip");
    document.undo();
    require(document.atlas.count() == 2 && paint::equal(document.image.get(0, 0), source.frames[0].image.get(0, 0)),
            "Undo restores all independent source images");
    document.redo();
    require(document.atlas.count() == 4, "Redo restores every target");
    const std::size_t history = document.undo_history.size();
    document.generate_icon_targets({}, {}, true);
    require(document.undo_history.size() == history, "keeping every image is a no-op");
    for (const std::vector<paint::IconGenerationTarget>& invalid : {
             std::vector<paint::IconGenerationTarget>{{0, -1, 24, 24, 32}, {0, -1, 257, 16, 32}},
             std::vector<paint::IconGenerationTarget>{{0, 0, 16, 16, 32}, {1, 0, 16, 16, 32}},
             std::vector<paint::IconGenerationTarget>{{99, -1, 16, 16, 32}},
             std::vector<paint::IconGenerationTarget>{{0, -1, 16, 16, 2}}}) {
        bool rejected = false;
        try { document.generate_icon_targets(invalid, {}, true); }
        catch (const std::exception&) { rejected = true; }
        require(rejected && document.atlas.count() == 4 && document.undo_history.size() == history,
                "invalid target plans cannot partially publish or append history");
    }
    document.generate_icon_targets({{1, -1, 32, 16, 32}}, {1}, true);
    require(document.atlas.count() == 4 &&
                paint::equal(document.atlas.icons.back().image.get(3, 3), source.frames[0].image.get(3, 3)),
            "a removed source remains available to all targets in the same transaction");
    document.new_image(32, 24);
    document.generate_icon_targets({{0, -1, 18, 12, 32}}, {}, false);
    require(document.atlas.count() == 1 && document.image.width == 18 && document.image.height == 12,
            "ordinary artwork can generate a custom-only profile");
}
void test_color() {
    for (int r = 0; r <= 255; r += 17) {
        for (int g = 0; g <= 255; g += 17) {
            for (int b = 0; b <= 255; b += 17) {
                paint::Color color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                   static_cast<std::uint8_t>(b), 255};
                paint::Color roundtrip = paint::from_oklab(paint::to_oklab(color));
                require(paint::equal(color, roundtrip), "OKLab roundtrip differs");
                paint::Color parsed;
                require(paint::from_hex(paint::to_hex(color), parsed) && paint::equal(parsed, color),
                        "hex roundtrip differs");
            }
        }
    }
    paint::Color invalid;
    require(!paint::from_hex("#12zz34", invalid), "invalid hex accepted");
}
void test_perceptual_tools() {
    for (int r = 0; r <= 255; r += 17) {
        for (int g = 0; g <= 255; g += 17) {
            for (int b = 0; b <= 255; b += 17) {
                const paint::Color color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                         static_cast<std::uint8_t>(b), 255};
                for (int space = 0; space < 2; ++space) {
                    const paint::PickerSpace mode = static_cast<paint::PickerSpace>(space);
                    const paint::ColorCoordinates coordinates = paint::picker_coordinates(mode, color);
                    require(std::isfinite(coordinates.hue) && std::isfinite(coordinates.saturation) &&
                                std::isfinite(coordinates.level),
                            "picker produced nonfinite coordinate");
                    const paint::Color restored = paint::picker_color(mode, coordinates);
                    if (std::abs(restored.r - r) > 1 || std::abs(restored.g - g) > 1 ||
                        std::abs(restored.b - b) > 1) {
                        std::cerr << "space=" << space << " input=" << r << "," << g << "," << b
                                  << " output=" << paint::to_hex(restored) << " hsl=" << coordinates.hue
                                  << "," << coordinates.saturation << "," << coordinates.level << "\n";
                    }
                    require(std::abs(restored.r - r) <= 1 && std::abs(restored.g - g) <= 1 &&
                                std::abs(restored.b - b) <= 1,
                            "RGB / OKHSL roundtrip exceeds one byte");
                }
            }
        }
    }
    for (int space = 0; space < 2; ++space) {
        const paint::PickerSpace mode = static_cast<paint::PickerSpace>(space);
        for (int band = 0; band < 24; ++band) {
            const double hue = band / 24.0;
            const paint::ColorMosaic first(mode, hue), second(mode, hue);
            std::set<std::string> unique;
            int saturated = 0, neutral_side = 0;
            for (std::size_t cell = 0; cell < first.colors().size(); ++cell) {
                const paint::Color color = first.colors()[cell];
                const paint::ColorCoordinates coordinates = first.coordinates(cell);
                unique.insert(paint::to_hex(color));
                require(paint::equal(color, second.colors()[cell]), "mosaic nucleation is deterministic");
                require(paint::equal(color, paint::picker_color(mode, coordinates)),
                        "mosaic cell must stay in its own hue slice");
                require(first.cell_at(coordinates.saturation, coordinates.level) == cell,
                        "every mosaic cell is independently pickable");
                saturated += coordinates.saturation >= 0.6;
                neutral_side += coordinates.saturation <= 0.2;
            }
            require(unique.size() == 96, "each hue must provide 96 distinct colors");
            require(saturated > neutral_side, "mosaic offers more saturated cells than near-neutral cells");
            require(paint::equal(first.nearest({0, 0, 0, 37}), {0, 0, 0, 37}) &&
                        paint::equal(first.nearest({255, 255, 255, 193}), {255, 255, 255, 193}),
                    "mosaic preserves black, white and alpha");
        }
    }
    paint::Image image;
    image.reset(5, 5, {160, 100, 90, 255});
    image.set(2, 2, {255, 255, 255, 255});
    require(paint::equal(paint::sample_color(image, {2, 2}, paint::SampleMode::Exact), image.get(2, 2)),
            "exact eyedropper changed default");
    const paint::Color representative = paint::sample_color(image, {2, 2}, paint::SampleMode::Representative);
    require(paint::equal(representative, image.get(0, 0)),
            "representative eyedropper retained isolated glitter fleck");
    require(!paint::equal(paint::sample_color(image, {2, 2}, paint::SampleMode::Average), representative),
            "average unexpectedly rejects flecks");
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rainstar-palette-migration-test.bin";
    {
        std::ofstream output(path, std::ios::binary);
        const unsigned char header[] = {'R', 'S', 'P', 'C', '1', 1, 128};
        output.write(reinterpret_cast<const char*>(header), sizeof(header));
        for (int slot = 0; slot < 16; ++slot) {
            const unsigned char color[] = {static_cast<unsigned char>(slot * 10), 70, 80, 130};
            output.write(reinterpret_cast<const char*>(color), 4);
        }
    }
    paint::CustomColors palette;
    palette.storage_path = path.string();
    palette.load();
    require(palette.occupied == 0x8001 && palette.colors[15].r == 150 && palette.colors[16].r == 255,
            "legacy custom palette migration failed");
    palette.store(28, {7, 8, 9, 10});
    paint::CustomColors reloaded;
    reloaded.storage_path = path.string();
    reloaded.load();
    require((reloaded.occupied & (1U << 28)) && paint::equal(reloaded.colors[28], {7, 8, 9, 10}) &&
                reloaded.colors[15].r == 150,
            "29-slot custom palette persistence lost colors or occupancy");
    const std::uint32_t occupied = reloaded.occupied;
    bool rejected = false;
    try {
        reloaded.store(29, {1, 2, 3, 4});
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    require(rejected && reloaded.occupied == occupied, "transparency slot cannot consume a custom color");
    std::filesystem::remove(path);
}
paint::Image relief_image(const paint::Image& source, double scale, double phase, double strength = 1,
                          paint::Point origin = {0, 0}, bool wrap = false) {
    paint::Image result = source;
    paint::TransformStroke stroke;
    stroke.begin(source, origin);
    stroke.segment(result, {source.width * 0.5, source.height * 0.5},
                   {source.width * 0.5, source.height * 0.5},
                   std::hypot(source.width, source.height) * 3, paint::MixEffect::ContourRelief,
                   strength, scale, phase, wrap);
    return result;
}
void test_contour_relief() {
    paint::Image flat;
    flat.reset(24, 24, {110, 135, 160, 123});
    paint::Image result = relief_image(flat, 24, 0);
    require(std::equal(result.pixels.begin(), result.pixels.end(), flat.pixels.begin(), paint::equal), "relief must preserve flat colors exactly");
    flat.reset(24, 24, {230, 70, 125, 0});
    result = relief_image(flat, 24, 0, 0.7);
    require(std::equal(result.pixels.begin(), result.pixels.end(), flat.pixels.begin(), paint::equal), "relief must preserve fully transparent hidden RGB");
    paint::Image source;
    source.reset(64, 48, {100, 100, 100, 177});
    for (int y = 0; y < source.height; ++y) {
        for (int x = 32; x < source.width; ++x) { source.set(x, y, {170, 170, 170, 177}); }
    }
    const paint::Image first = relief_image(source, 24, 0);
    const paint::Image opposite = relief_image(source, 24, std::numbers::pi);
    require(first.get(31, 24).r < source.get(31, 24).r &&
            opposite.get(31, 24).r > source.get(31, 24).r,
            "half-turn light rotation must exchange relief highlights and shadows");
    require(std::equal(first.pixels.begin(), first.pixels.end(), relief_image(source, 24, 0, 1, {19, 7}).pixels.begin(), paint::equal),
            "relief must follow content, not the gesture origin");
    require(std::equal(source.pixels.begin(), source.pixels.end(), relief_image(source, 24, 0, 0).pixels.begin(), paint::equal), "zero strength changes pixels");
    require(paint::equal(first.get(12, 24), source.get(12, 24)), "relief changes remote flat areas");
    const paint::Image broad = relief_image(source, 64, 0);
    require(paint::equal(first.get(26, 24), source.get(26, 24)) &&
            !paint::equal(broad.get(26, 24), source.get(26, 24)), "scale must control contour width");
    for (std::size_t i = 0; i < source.pixels.size(); ++i) {
        require(first.pixels[i].a == source.pixels[i].a, "relief changed opacity");
    }
    paint::Image hidden = source;
    for (int y = 0; y < source.height; ++y) {
        for (int x = 32; x < source.width; ++x) { source.set(x, y, {0, 0, 0, 0}); hidden.set(x, y, {255, 0, 255, 0}); }
    }
    require(paint::equal(relief_image(source, 24, 0).get(31, 24),
                         relief_image(hidden, 24, 0).get(31, 24)), "hidden RGB casts relief shadows");
    const paint::Image wrapped = relief_image(first, 24, 0, 1, {0, 0}, true);
    require(!paint::equal(wrapped.get(0, 24), first.get(0, 24)), "wrapped relief ignores the opposite edge");

    const char* capture = std::getenv("PAINT_RELIEF_CAPTURE");
    if (capture && *capture) {
        paint::Image scene;
        scene.reset(240, 180, {130, 155, 163, 255});
        paint::Ink ink;
        ink.size = 3;
        ink.primary = {193, 108, 67, 255};
        paint::draw_shape(scene, paint::Shape::RoundedRectangle, {22, 24}, {130, 140}, ink, true, false);
        ink.primary = {58, 105, 124, 255};
        paint::draw_shape(scene, paint::Shape::Star5, {104, 27}, {218, 144}, ink, true, false);
        for (int x = 18; x < 225; ++x) {
            const int y = 155 + static_cast<int>(7 * std::sin(x * 0.055));
            scene.set(x, y, {214, 190, 122, 255});
        }
        const paint::Image raised = relief_image(scene, 32, 0);
        const paint::Image reversed = relief_image(scene, 32, std::numbers::pi);
        paint::Image sheet;
        sheet.reset(720, 180, {});
        for (int y = 0; y < 180; ++y) {
            for (int x = 0; x < 240; ++x) {
                sheet.set(x, y, scene.get(x, y));
                sheet.set(x + 240, y, raised.get(x, y));
                sheet.set(x + 480, y, reversed.get(x, y));
            }
        }
        paint::save_image(sheet, capture);
    }
}
void test_transformative_brushes() {
    paint::StrokeStabilizer whole, split;
    whole.reset({0, 0});
    split.reset({0, 0});
    paint::Point whole_point = whole.advance({100, 0}, 15), split_point;
    for (int step = 1; step <= 100; ++step) {
        split_point = split.advance({static_cast<double>(step), 0}, 15);
    }
    require(std::hypot(whole_point.x - split_point.x, whole_point.y - split_point.y) < 1e-9 &&
                whole_point.x < 100,
            "stabilizer integrates the same straight trajectory independently of event frequency");
    paint::Image source;
    source.reset(80, 48, {130, 70, 50, 255});
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            source.set(x, y,
                       {static_cast<std::uint8_t>(x * 3), static_cast<std::uint8_t>(y * 5),
                        static_cast<std::uint8_t>((x + y) * 2), static_cast<std::uint8_t>(80 + x * 2)});
        }
    }
    paint::HealingBrush clone;
    clone.capture(source, {8, 12});
    paint::Image target = source;
    clone.begin(target, {40, 24}, {true, false, true});
    clone.segment(target, {40, 24}, {60, 24}, 5, 1, 1);
    for (int x = 40; x <= 60; ++x) {
        require(paint::equal(target.get(x, 24), source.get(x - 32, 12)),
                "hard continuous clone lost source offset or exact RGBA");
    }
    require(paint::equal(target.get(70, 40), source.get(70, 40)),
            "clone changed pixels outside its footprint");
    paint::Image heal_source, healed;
    heal_source.reset(40, 40, {190, 110, 90, 255});
    healed.reset(40, 40, {90, 150, 170, 255});
    clone.capture(heal_source, {20, 20});
    clone.begin(healed, {20, 20});
    clone.segment(healed, {20, 20}, {20, 20}, 16, 0.2, 1);
    const paint::Color healed_center = healed.get(20, 20);
    require(std::abs(healed_center.r - 90) <= 1 && std::abs(healed_center.g - 150) <= 1 &&
                std::abs(healed_center.b - 170) <= 1,
            "OKLab healing did not correct a constant donor to recipient hue and lightness");
    healed.reset(40, 40, {90, 150, 170, 128});
    healed.set(20, 20, {0, 0, 0, 128});
    healed.set(19, 20, {37, 41, 53, 0});
    clone.begin(healed, {20, 20});
    clone.segment(healed, {20, 20}, {20, 20}, 10, 1, 1);
    require(std::abs(healed.get(20, 20).r - 90) <= 1 && std::abs(healed.get(20, 20).g - 150) <= 1 &&
                healed.get(20, 20).a == 128,
            "hard healing excludes the defect from color matching and preserves destination alpha");
    require(paint::equal(healed.get(19, 20), {37, 41, 53, 0}),
            "healing preserves hidden RGB beneath destination transparency");
    clone.begin(healed, {10, 10}, {false, true, false});
    require(clone.source_for({10, 10}).x == 20 && clone.source_for({10, 10}).y == 20,
            "unaligned healing returns to the captured source each stroke");
    paint::HealingBrush whole_heal, split_heal;
    paint::Image whole_image = source, split_image = source;
    whole_heal.capture(source, {10, 12});
    split_heal.capture(source, {10, 12});
    whole_heal.begin(whole_image, {40, 24});
    split_heal.begin(split_image, {40, 24});
    whole_heal.segment(whole_image, {40, 24}, {60, 24}, 8, 0.35, 1);
    split_heal.segment(split_image, {40, 24}, {50, 24}, 8, 0.35, 1);
    split_heal.segment(split_image, {50, 24}, {60, 24}, 8, 0.35, 1);
    require(std::memcmp(whole_image.pixels.data(), split_image.pixels.data(), whole_image.pixels.size() * 4) == 0,
            "healing repairs the whole_image stroke independently of pointer segmentation");
    std::set<std::string> effects;
    for (int index = 0; index < paint::mix_effect_count; ++index) {
        paint::TransformStroke stroke;
        target = source;
        stroke.begin(target, {20, 20});
        stroke.segment(target, {20, 20}, {60, 28}, 18, static_cast<paint::MixEffect>(index), 1, 16, 0.7);
        require(paint::equal(target.get(78, 46), source.get(78, 46)), "mix effect escaped its footprint");
        const paint::Image once = target;
        stroke.segment(target, {20, 20}, {60, 28}, 18, static_cast<paint::MixEffect>(index), 1, 16, 0.7);
        require(std::memcmp(target.pixels.data(), once.pixels.data(), target.pixels.size() * 4) == 0,
                "mix effect accumulated with repeated identical pointer events");
        std::uint64_t digest = 1469598103934665603ULL;
        for (std::size_t pixel = 0; pixel < target.pixels.size(); ++pixel) {
            digest = (digest ^ target.pixels[pixel].r) * 1099511628211ULL;
            digest = (digest ^ target.pixels[pixel].g) * 1099511628211ULL;
        }
        effects.insert(std::to_string(digest));
    }
    require(effects.size() == paint::mix_effect_count, "transformative brush effects are indistinguishable");
    paint::StrokeStabilizer stabilizer;
    stabilizer.reset({0, 0});
    const paint::Point delayed = stabilizer.advance({5, 0}, 8);
    require(delayed.x > 0 && delayed.x < 5, "stabilizer failed to slow trajectory change");
    const paint::Point direct = stabilizer.advance({7, 3}, 0);
    require(direct.x == 7 && direct.y == 3, "disabled stabilizer changes trajectory");
    paint::Image a, b;
    a.reset(100, 60, {0, 0, 0, 0});
    b = a;
    paint::DynamicBrushStroke first, second;
    paint::Ink ink;
    ink.brush = paint::Brush::Airbrush;
    ink.primary = {170, 110, 60, 255};
    ink.size = 24;
    first.segment(a, {20, 30}, {80, 30}, ink, true);
    second.segment(b, {20, 30}, {40, 30}, ink, true);
    second.segment(b, {40, 30}, {60, 30}, ink, true);
    second.segment(b, {60, 30}, {80, 30}, ink, true);
    require(std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size() * 4) == 0,
            "spray depends on pointer event segmentation");
    const paint::Brush dry_media[] = {paint::Brush::Pencil, paint::Brush::Crayon, paint::Brush::Pastel,
                                      paint::Brush::Charcoal};
    for (std::size_t index = 0; index < std::size(dry_media); ++index) {
        paint::Image single, divided;
        single.reset(100, 60, {0, 0, 0, 0});
        divided = single;
        ink.brush = dry_media[index];
        paint::DynamicBrushStroke full, split;
        full.segment(single, {20, 30}, {80, 30}, ink, false);
        split.segment(divided, {20, 30}, {40, 30}, ink, false);
        split.segment(divided, {40, 30}, {60, 30}, ink, false);
        split.segment(divided, {60, 30}, {80, 30}, ink, false);
        require(std::equal(single.pixels.begin(), single.pixels.end(), divided.pixels.begin(), paint::equal),
                "dry brush coverage depends on pointer event segmentation");
        split.segment(divided, {80, 30}, {80, 30}, ink, false);
        require(std::equal(single.pixels.begin(), single.pixels.end(), divided.pixels.begin(), paint::equal),
                "stationary dry brush events accumulate extra pigment");
    }
    std::set<std::string> colors;
    for (std::size_t pixel = 0; pixel < a.pixels.size(); ++pixel) {
        if (a.pixels[pixel].a) {
            colors.insert(paint::to_hex(a.pixels[pixel]));
        }
    }
    require(colors.size() > 20, "glitter spray lacks varying facets");
}
void test_material_collections_and_dry_contact() {
    paint::Document document;
    document.ink.primary = {180, 30, 20, 255};
    document.ink.secondary = {20, 60, 190, 255};
    paint::select_pattern(document.ink, paint::Pattern::Checker);
    paint::select_brush(document.alt_ink, paint::Brush::Crayon);
    document.ink.size = 32;
    const paint::Ink layered = document.primary_ink();
    const paint::Ink alternate = document.alternate_ink();
    const paint::MaterialSurface surface(layered, layered.brush), wax(alternate, alternate.brush);
    require(paint::equal(surface.sample(20, 20, 3), document.ink.primary),
            "Primary pattern foreground is a flat color");
    require(paint::equal(surface.sample(24, 20, 3), wax.sample(24, 20, 3)),
            "Primary pattern gaps sample the independent Alt brush");
    paint::select_pattern(document.alt_ink, paint::Pattern::None);
    const paint::Ink open = document.primary_ink();
    require(paint::MaterialSurface(open, open.brush).sample(24, 20, 3).a == 0,
            "Alt None leaves pattern gaps transparent");
    for (paint::Brush medium : {paint::Brush::Crayon, paint::Brush::Charcoal}) {
        paint::Ink ink;
        ink.size = 40;
        ink.brush = medium;
        const paint::MaterialSurface dry(ink, medium);
        double core = 0, rim = 0, core_square = 0, rim_square = 0;
        for (int x = 0; x < 256; ++x) {
            const double a = dry.sample(x, 17, 20).a, b = dry.sample(x, 17, 1).a;
            core += a;
            rim += b;
            core_square += a * a;
            rim_square += b * b;
        }
        core /= 256;
        rim /= 256;
        std::cout << paint::brush_names[static_cast<int>(medium)] << " core alpha " << core << ", rim alpha "
                  << rim << '\n';
        require(core > 180 && core > rim * 1.8, "dry medium must deposit a dense core with a lighter rim");
        require(rim_square / 256 - rim * rim > (core_square / 256 - core * core) * 3,
                "granulation must be concentrated at the edge rather than perforating the core");
        paint::Image stroke;
        stroke.reset(280, 70, {0, 0, 0, 0});
        paint::DynamicBrushStroke brush;
        brush.segment(stroke, {25, 35}, {255, 35}, ink, false);
        double middle = 0, outer = 0;
        for (int x = 40; x < 240; ++x) {
            middle += stroke.get(x, 35).a;
            outer += stroke.get(x, 17).a;
        }
        require(middle > outer * 1.8 && middle / 200 > 180,
                "freehand deposition retains the dense core and granular edge");
    }
    document.new_image(100, 90);
    document.shape_fill = true;
    document.continuous_path = false;
    document.alt_carries_body = true;
    paint::select_pattern(document.alt_ink, paint::Pattern::Solid);
    document.add_path_node({10, 10});
    document.add_path_node({85, 10});
    document.add_path_node({85, 80});
    document.end_path_geometry();
    const paint::Image original = document.image;
    document.alt_carries_body = false;
    paint::select_brush(document.alt_ink, paint::Brush::Charcoal);
    document.add_path_node({15, 85});
    document.end_path_geometry();
    require(std::equal(original.pixels.begin(), original.pixels.end(), document.image.pixels.begin(),
                       paint::equal),
            "canceling a later run preserves a saved run's body assignment and material");
}
void test_conv() {
    paint::Image input, output;
    input.reset(13, 9, {53, 177, 229, 149});
    const int sizes[6][2] = {{29, 17}, {7, 5}, {1, 1}, {1, 19}, {31, 1}, {13, 9}};
    for (const int (&size)[2] : sizes) {
        paint::conv_resize(input, size[0], size[1], output);
        for (paint::Color pixel : output.pixels) {
            require(paint::equal(pixel, {53, 177, 229, 149}), "CONV constant preservation failed");
        }
    }
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            input.set(x, y, {static_cast<std::uint8_t>(x * 17), static_cast<std::uint8_t>(y * 23), 0, 255});
        }
    }
    paint::conv_resize(input, 25, 17, output);
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            require(paint::equal(input.get(x, y), output.get(x * 2, y * 2)),
                    "CONV nodal interpolation failed");
        }
    }
    // An unchanged dimension must preserve its rows/columns, including when
    // source and destination alias. These fixtures exercise both one-axis paths.
    paint::conv_resize(input, 25, 9, output);
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 13; ++x) {
            require(paint::equal(input.get(x, y), output.get(x * 2, y)), "width-only resize moved a row");
        }
    }
    paint::Image original = input;
    paint::conv_resize(input, 13, 17, input);
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 13; ++x) {
            require(paint::equal(original.get(x, y), input.get(x, y * 2)),
                    "height-only alias resize moved a column");
        }
    }
    input.reset(1, 3, {20, 40, 60, 255});
    paint::conv_resize(input, 7, 1, output);
    for (paint::Color pixel : output.pixels) {
        require(paint::equal(pixel, {20, 40, 60, 255}), "short line failed");
    }
}
void test_conv_area() {
    paint::Image source, output;
    source.reset(13, 9, {37, 141, 215, 137});
    const int sizes[][2] = {{3, 2}, {26, 18}, {1, 1}, {13, 9}};
    for (const int (&size)[2] : sizes) {
        paint::conv_resize_area(source, size[0], size[1], output);
        for (paint::Color color : output.pixels) {
            require(paint::equal(color, source.get(0, 0)), "area resize preserves constant RGBA");
        }
    }
    source.reset(12, 1);
    for (int x = 0; x < source.width; ++x) {
        source.set(x, 0, {static_cast<std::uint8_t>(20 + x * x), 0, 0, 255});
    }
    paint::conv_resize_area(source, 24, 1, output);
    for (int x = 3; x < 21; ++x) {
        const double left = x * 0.5 - 0.5, right = left + 0.5;
        const int expected =
            static_cast<int>(std::lround(20 + (left * left + left * right + right * right) / 3));
        require(std::abs(static_cast<int>(output.get(x, 0).r) - expected) <= 1,
                "area enlargement matches the analytic quadratic pixel integral");
    }
    const paint::Image original = source;
    paint::conv_resize_area(source, source.width, source.height, source);
    require(std::memcmp(source.pixels.data(), original.pixels.data(),
                        source.pixels.size() * sizeof(paint::Color)) == 0,
            "no-op area resize is byte exact and supports aliasing");
    source.reset(2, 1, {0, 0, 0, 255});
    source.set(1, 0, {255, 255, 255, 255});
    paint::conv_resize_area(source, 4, 1, output);
    const int expected[4] = {0, 64, 191, 255};
    for (int x = 0; x < 4; ++x) {
        require(output.get(x, 0).r == expected[x],
                "short-line areas include constant half-pixel edge strips");
    }
    source.set(0, 0, {255, 0, 0, 255});
    source.set(1, 0, {0, 0, 255, 0});
    paint::conv_resize_area(source, 1, 1, output);
    require(paint::equal(output.get(0, 0), {255, 0, 0, 128}),
            "area resize averages premultiplied opacity without hidden-color bleed");
}
void test_conv_reference() {
    for (const fixtures::Case& fixture : fixtures::cases) {
        paint::Image input, output;
        input.reset(fixture.width, fixture.height);
        std::memcpy(input.pixels.data(), fixture.input, input.pixels.size() * 4);
        paint::conv_resize(input, fixture.out_width, fixture.out_height, output);
        const std::uint8_t* actual = reinterpret_cast<const std::uint8_t*>(output.pixels.data());
        for (std::size_t i = 0; i < output.pixels.size() * 4; ++i) {
            if (actual[i] != fixture.expected[i]) {
                std::cerr << "reference difference at byte " << i << ": " << static_cast<int>(actual[i])
                          << " vs " << static_cast<int>(fixture.expected[i]) << '\n';
                throw std::runtime_error("Native CONV differs from website JavaScript reference");
            }
        }
    }
}
void test_editing() {
    paint::Document doc;
    doc.new_image(20, 20);
    doc.checkpoint();
    doc.ink.primary = {0, 0, 0, 255};
    doc.ink.pattern = paint::Pattern::Dither50;
    paint::flood(doc.image, 0, 0, doc.ink);
    int dark = 0;
    for (paint::Color pixel : doc.image.pixels) {
        if (pixel.r == 0) {
            ++dark;
        }
    }
    require(dark == 200, "pattern bucket region/density failed");
    doc.undo();
    require(doc.image.get(0, 0).r == 255, "undo failed");
    doc.redo();
    require(doc.image.get(0, 0).r == 0, "redo failed");
    doc.select({0, 0, 10, 10});
    doc.selection.x = 10;
    doc.selection.y = 10;
    doc.commit_selection();
    doc.undo();
    require(doc.image.get(0, 0).r == 0, "selection transaction undo failed");
    paint::Image stamp = paint::make_stamp(doc.image, {0, 0, 10, 10}, paint::StampShape::Circle, false, {});
    require(stamp.get(0, 0).a == 0 && stamp.get(5, 5).a == 255, "stamp mask failed");
    paint::Image textured;
    textured.reset(20, 20);
    paint::Ink fill_ink;
    fill_ink.secondary = {0, 0, 0, 255};
    paint::draw_shape(textured, paint::Shape::Rectangle, {1, 1}, {18, 18}, fill_ink, false, true,
                      paint::Brush::Watercolor);
    require(textured.get(10, 10).r > 0 && textured.get(10, 10).r < 250,
            "watercolor shape fill ignored its medium");
}
void test_materials_and_shapes() {
    paint::Ink ink;
    ink.primary = {42, 88, 139, 210};
    ink.secondary = ink.primary;
    ink.size = 28;
    for (int i = 4; i < paint::brush_count; ++i) {
        ink.brush = static_cast<paint::Brush>(i);
        paint::Image whole, split;
        whole.reset(224, 110, {230, 220, 200, 255});
        split = whole;
        paint::MaterialStroke a, b;
        a.segment(whole, {10, 25}, {210, 75}, ink);
        b.segment(split, {10, 25}, {60, 37.5}, ink);
        b.segment(split, {60, 37.5}, {130, 55}, ink);
        b.segment(split, {130, 55}, {210, 75}, ink);
        require(std::memcmp(whole.pixels.data(), split.pixels.data(), whole.pixels.size() * 4) == 0,
                "material coat depends on collinear mouse event subdivision");
        b.segment(split, {10, 25}, {210, 75}, ink);
        require(std::memcmp(whole.pixels.data(), split.pixels.data(), whole.pixels.size() * 4) == 0,
                "revisiting a material coat multiplied its opacity");
        paint::MaterialSurface surface(ink, ink.brush);
        int minimum = 255, maximum = 0;
        for (int y = 0; y < 40; ++y) {
            for (int x = 0; x < 40; ++x) {
                int alpha = surface.sample(x, y, 12).a;
                minimum = std::min(minimum, alpha);
                maximum = std::max(maximum, alpha);
            }
        }
        if (ink.brush == paint::Brush::Gel) {
            require(minimum == ink.primary.a && maximum == ink.primary.a, "gel body varies opacity");
            int min_red = 255, max_red = 0;
            for (int x = 0; x < 80; ++x) {
                const paint::Color c = surface.sample(x, 17, 2);
                min_red = std::min(min_red, static_cast<int>(c.r));
                max_red = std::max(max_red, static_cast<int>(c.r));
            }
            require(max_red - min_red > 3, "gel has no brightness variation");
        } else {
            require(maximum - minimum > 3, "natural medium has no spatial material variation");
        }
        paint::Ink dry = ink;
        dry.pigment_load = 0;
        paint::MaterialSurface empty(dry, dry.brush);
        require(empty.sample(10, 20, 3).a == 0, "zero paint load deposited pigment");
        paint::Image fill;
        fill.reset(140, 120, {255, 255, 255, 255});
        paint::draw_shape(fill, paint::Shape::Rectangle, {5, 5}, {130, 110}, ink, false, true, ink.brush);
        require(fill.get(60, 60).r < 255 && fill.get(0, 0).r == 255,
                "material fill leaked outside its region");
    }
    ink.brush = paint::Brush::Watercolor;
    paint::MaterialSurface water(ink, ink.brush);
    require(water.sample(20, 20, 0.5).a > water.sample(20, 20, 20).a,
            "watercolor rim did not accumulate pigment");
    paint::MaterialSurface original(ink, ink.brush);
    ++ink.noise;
    paint::MaterialSurface reseeded(ink, ink.brush);
    unsigned changes = 0;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 24; ++x) {
            changes += !paint::equal(original.sample(x, y, 10), reseeded.sample(x, y, 10));
        }
    }
    require(changes > 500, "new grain did not regenerate the procedural surface");
    const paint::Point ends[4] = {{230, 100}, {-70, 100}, {230, -100}, {-70, -100}};
    for (int i = 0; i < 4; ++i) {
        paint::Point start{40, 30};
        std::vector<paint::Point> circle = paint::shape_points(paint::Shape::Circle, start, ends[i]);
        double min_x = circle[0].x, max_x = min_x, min_y = circle[0].y, max_y = min_y;
        for (std::size_t j = 0; j < circle.size(); ++j) {
            min_x = std::min(min_x, circle[j].x);
            max_x = std::max(max_x, circle[j].x);
            min_y = std::min(min_y, circle[j].y);
            max_y = std::max(max_y, circle[j].y);
        }
        require(std::abs((max_x - min_x) - (max_y - min_y)) < 1e-9, "Circle permitted unequal diameters");
        double cx = (min_x + max_x) / 2, cy = (min_y + max_y) / 2, r = (max_x - min_x) / 2;
        for (std::size_t j = 0; j < circle.size(); ++j) {
            require(std::abs(std::hypot(circle[j].x - cx, circle[j].y - cy) - r) < 1e-9,
                    "Circle is not circular");
        }
    }
    for (int i = static_cast<int>(paint::Shape::Circle); i < paint::shape_count; ++i) {
        std::vector<paint::Point> points =
            paint::shape_points(static_cast<paint::Shape>(i), {0, 0}, {100, 100});
        require(points.size() >= (static_cast<paint::Shape>(i) == paint::Shape::Arc ? 2 : 3),
                "shape has no valid outline");
        for (std::size_t j = 0; j < points.size(); ++j) {
            require(std::isfinite(points[j].x) && std::isfinite(points[j].y),
                    "new shape has invalid geometry");
        }
    }
}
void test_eraser_and_pixel_target() {
    paint::Image hard, soft, split;
    hard.reset(64, 64, {40, 100, 170, 200});
    soft = hard;
    split = hard;
    paint::EraserStroke eraser;
    eraser.segment(hard, {20.5, 20.5}, {40.5, 20.5}, 12, false);
    require(hard.get(30, 20).a == 0 && hard.get(20, 20).a == 0, "hard eraser left opacity");
    require(hard.get(15, 15).a == 200, "round eraser used a square footprint");
    eraser.clear();
    eraser.segment(soft, {20.5, 20.5}, {40.5, 20.5}, 12, true);
    require(soft.get(30, 20).a == 0 && soft.get(30, 22).a > 0 && soft.get(30, 25).a > soft.get(30, 22).a,
            "soft eraser has no center-to-edge opacity taper");
    eraser.clear();
    for (int x = 20; x < 40; ++x) {
        eraser.segment(split, {x + .5, 20.5}, {x + 1.5, 20.5}, 12, true);
    }
    require(std::equal(split.pixels.begin(), split.pixels.end(), soft.pixels.begin(), paint::equal),
            "soft eraser strength depends on pointer event frequency");
    eraser.segment(split, {20.5, 20.5}, {40.5, 20.5}, 12, true);
    require(std::equal(split.pixels.begin(), split.pixels.end(), soft.pixels.begin(), paint::equal),
            "same eraser gesture compounded opacity");
    paint::Image pencil;
    pencil.reset(4, 4, {255, 255, 255, 255});
    paint::Ink ink;
    ink.primary = {0, 0, 0, 255};
    paint::pixel_line(pencil, {1.99, 1.99}, {1.99, 1.99}, ink);
    require(pencil.get(1, 1).r == 0 && pencil.get(2, 2).r == 255, "pencil rounded into neighboring pixel");
}
double reference_segment_distance(paint::Point p, paint::Point a, paint::Point b) {
    double dx = b.x - a.x, dy = b.y - a.y, length2 = dx * dx + dy * dy;
    double t = length2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length2, 0.0, 1.0) : 0;
    return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}
void test_continuous_geometry_coverage() {
    const int widths[3] = {1, 3, 7};
    for (int w = 0; w < 3; ++w) {
        double radius = widths[w] * .5, expected = 80 * widths[w] + std::numbers::pi * radius * radius;
        for (int degrees = 0; degrees < 180; degrees += 15) {
            double angle = degrees * std::numbers::pi / 180;
            paint::Point a{64 - 40 * std::cos(angle), 64 - 40 * std::sin(angle)};
            paint::Point b{64 + 40 * std::cos(angle), 64 + 40 * std::sin(angle)};
            paint::Image image;
            image.reset(128, 128, {0, 0, 0, 0});
            paint::Ink ink;
            ink.size = widths[w];
            paint::MaterialStroke coat;
            coat.segment(image, a, b, ink);
            double area = 0;
            for (std::size_t i = 0; i < image.pixels.size(); ++i) {
                area += image.pixels[i].a / 255.0;
            }
            if (std::abs(area - expected) >= std::max(.8, expected * .009)) {
                std::cerr << "width=" << widths[w] << " angle=" << degrees << " area=" << area
                          << " expected=" << expected << "\n";
            }
            require(std::abs(area - expected) < std::max(.8, expected * .009),
                    "stroke thickness changes with angle");
        }
    }
    // Independent 64x64 area integration checks shared fill/outline edge compositing.
    std::vector<paint::Point> triangle = {{12.2, 8.7}, {57.6, 26.3}, {19.4, 57.1}};
    paint::Image image;
    image.reset(70, 70, {255, 255, 255, 255});
    paint::Ink ink;
    ink.primary = {0, 0, 0, 151};
    ink.secondary = {80, 120, 180, 97};
    ink.size = 1;
    paint::polygon(image, triangle, ink, true, true, true, paint::Brush::Round);
    for (int y = 6; y < 60; ++y) {
        for (int x = 10; x < 60; ++x) {
            double distance = 100;
            for (int edge = 0; edge < 3; ++edge) {
                distance =
                    std::min(distance, reference_segment_distance({double(x), double(y)}, triangle[edge],
                                                                  triangle[(edge + 1) % 3]));
            }
            if (distance > 1.3) {
                continue;
            }
            double expected[3] = {0, 0, 0};
            for (int sy = 0; sy < 64; ++sy) {
                for (int sx = 0; sx < 64; ++sx) {
                    paint::Point sample{x + (sx + .5) / 64 - .5, y + (sy + .5) / 64 - .5};
                    double color[3] = {255, 255, 255};
                    if (paint::inside_polygon(triangle, sample.x, sample.y)) {
                        const int fill[3] = {80, 120, 180};
                        for (int c = 0; c < 3; ++c) {
                            color[c] = fill[c] * (97.0 / 255) + 255 * (1 - 97.0 / 255);
                        }
                    }
                    bool stroke = false;
                    for (int edge = 0; edge < 3; ++edge) {
                        if (reference_segment_distance(sample, triangle[edge], triangle[(edge + 1) % 3]) <=
                            .5) {
                            stroke = true;
                        }
                    }
                    for (int c = 0; c < 3; ++c) {
                        expected[c] += color[c] * (stroke ? 1 - 151.0 / 255 : 1) / 4096;
                    }
                }
            }
            paint::Color actual = image.get(x, y);
            require(std::abs(actual.r - expected[0]) < 3 && std::abs(actual.g - expected[1]) < 3 &&
                        std::abs(actual.b - expected[2]) < 3,
                    "fill/outline edge differs from independent area integration");
        }
    }
}
void test_stamp_masks_and_oblique_edges() {
    paint::Image source;
    source.reset(80, 64, {75, 130, 190, 255});
    for (int i = 0; i < paint::stamp_shape_count; ++i) {
        paint::StampShape shape = static_cast<paint::StampShape>(i);
        std::vector<paint::Point> points = paint::stamp_outline(shape, 58, 42);
        require(points.size() >= 2, "stamp has no usable outline");
        for (const paint::Point& point : points) {
            require(point.x >= 0 && point.y >= 0 && point.x <= 58 && point.y <= 42,
                    "stamp outline escapes its capture bounds");
        }
        for (const paint::Point& point : paint::stamp_outline(shape, 1, 1)) {
            require(point.x >= 0 && point.y >= 0 && point.x <= 1 && point.y <= 1,
                    "minimum stamp size has out-of-bounds geometry");
        }
        paint::Image mask = paint::make_stamp(source, {0, 0, 58, 42}, shape, false, {255, 255, 255, 255});
        int visible = 0;
        for (const paint::Color& pixel : mask.pixels) {
            visible += pixel.a > 0;
        }
        require(visible > 20, "stamp shape produced an empty mask");
        if (shape == paint::StampShape::Bezier || shape == paint::StampShape::Arc) {
            require(points.size() > 10, "curved stamp degenerated to a straight segment");
        }
    }
    paint::Ink ink;
    ink.size = 1;
    // Check geometric support and connectivity at awkward, fractional angles.
    // Antialiasing may soften a boundary but must never deposit isolated islands.
    for (int degrees = 1; degrees < 180; degrees += 7) {
        double angle = (degrees + 0.37) * std::numbers::pi / 180;
        paint::Point a{48.23 - 32 * std::cos(angle), 48.41 - 32 * std::sin(angle)};
        paint::Point b{48.23 + 32 * std::cos(angle), 48.41 + 32 * std::sin(angle)};
        std::vector<paint::Point> nodes{a, b, {44.71, 77.23}};
        for (int closed = 0; closed < 2; ++closed) {
            paint::Image image;
            image.reset(96, 96, {0, 0, 0, 0});
            if (closed) {
                paint::polygon(image, nodes, ink, true, false);
            } else {
                paint::stroke(image, a, b, ink);
            }
            int first = -1, total = 0;
            for (int y = 0; y < 96; ++y) {
                for (int x = 0; x < 96; ++x) {
                    if (image.get(x, y).a == 0) {
                        continue;
                    }
                    if (first < 0) {
                        first = y * 96 + x;
                    }
                    ++total;
                    double distance = reference_segment_distance({double(x), double(y)}, a, b);
                    if (closed) {
                        distance = std::min(distance,
                                            reference_segment_distance({double(x), double(y)}, b, nodes[2]));
                        distance = std::min(distance,
                                            reference_segment_distance({double(x), double(y)}, nodes[2], a));
                    }
                    require(distance <= 0.5 + std::sqrt(0.5),
                            "solid edge grew pixels outside its geometric support");
                }
            }
            require(first >= 0, "smooth line disappeared");
            std::vector<bool> visited(96 * 96);
            std::vector<int> queue{first};
            visited[first] = true;
            for (std::size_t i = 0; i < queue.size(); ++i) {
                int x = queue[i] % 96, y = queue[i] / 96;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx, ny = y + dy, index = ny * 96 + nx;
                        if (nx < 0 || ny < 0 || nx >= 96 || ny >= 96 || visited[index]) {
                            continue;
                        }
                        if (image.get(nx, ny).a) {
                            visited[index] = true;
                            queue.push_back(index);
                        }
                    }
                }
            }
            require(static_cast<int>(queue.size()) == total, "solid edge contains detached pixels");
        }
    }
}
void test_curves() {
    paint::CurveGeometry curve;
    curve.set_line({10, 10}, {110, 10});
    curve.move_handle(0, {10, 110});
    curve.move_handle(1, {110, 110});
    paint::Point quarter = curve.at(0.25), middle = curve.at(0.5);
    require(std::abs(quarter.x - 25.625) < 1e-10 && std::abs(quarter.y - 66.25) < 1e-10 && middle.x == 60 &&
                middle.y == 85 && curve.handle_count() == 2,
            "cubic Bézier does not follow its independent endpoint controls");
    curve.set_line({0, 0}, {100, 0});
    curve.move_handle(0, {-200, 0});
    curve.move_handle(1, {300, 0});
    std::vector<paint::Point> samples = curve.samples();
    double left = 0, right = 100;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        paint::Point point = samples[i];
        left = std::min(left, point.x);
        right = std::max(right, point.x);
    }
    require(left < -40 && right > 140, "Bézier flattening discarded collinear control reversals");

    curve.kind = paint::CurveKind::Arc;
    curve.set_line({-50, 0}, {50, 0});
    curve.move_handle(0, {20, 50});
    require(curve.handle_count() == 1 && curve.handle(0).x == 0 && curve.handle(0).y == 50,
            "arc midpoint left the perpendicular bisector");
    quarter = curve.at(0.25);
    require(std::abs(quarter.x + 50 / std::sqrt(2.0)) < 1e-10 &&
                std::abs(quarter.y - 50 / std::sqrt(2.0)) < 1e-10,
            "arc is not a circular semicircle through its midpoint handle");
    samples = curve.samples();
    for (std::size_t i = 1; i < samples.size(); ++i) {
        paint::Point point = samples[i], previous = samples[i - 1];
        require(std::abs(std::hypot(point.x, point.y) - 50) < 1e-9, "arc samples left the circle");
        double chord_radius = std::hypot((point.x + previous.x) / 2, (point.y + previous.y) / 2);
        require(50 - chord_radius <= 0.125, "arc sampling exceeded its pixel error tolerance");
    }
    curve.move_handle(0, {0, -50});
    require(curve.at(0.5).y == -50 && curve.at(0.25).y < 0, "arc cannot bend across the baseline");
    curve.move_handle(0, {0, 100});
    samples = curve.samples();
    for (std::size_t i = 0; i < samples.size(); ++i) {
        paint::Point point = samples[i];
        require(std::abs(std::hypot(point.x, point.y - 37.5) - 62.5) < 1e-9,
                "major arc radius or center is incorrect");
    }
    curve.move_handle(0, {0, 1e-8});
    require(std::isfinite(curve.at(0.25).y) && std::abs(curve.at(0.25).y) < 1e-7,
            "nearly straight arc is numerically unstable");
    curve.set_line({4, 10}, {4, 110});
    curve.move_handle(0, {-46, 60});
    require(curve.at(0.5).x == -46 && curve.at(0.5).y == 60, "vertical arc handle is misplaced");

    paint::Document doc;
    doc.new_image(128, 128);
    doc.ink.primary = {20, 70, 140, 130};
    doc.ink.size = 4;
    doc.begin_curve(paint::CurveKind::Bezier, {10, 20});
    require(!doc.dirty() && doc.undo_history.empty(), "first curve click changed the picture or history");
    require(!doc.establish_curve({10, 20}) && doc.undo_history.empty(), "zero-length curve was accepted");
    doc.commit_curve();
    require(!doc.curve.base && !doc.dirty(), "releasing a lone anchor preserved a mark");
    doc.begin_curve(paint::CurveKind::Bezier, {10, 20});
    require(doc.establish_curve({110, 20}), "curve starting line was not accepted");
    paint::Image baseline = doc.image;
    doc.checkpoint();
    doc.curve.geometry.move_handle(0, {10, 110});
    doc.sync_curve();
    paint::Image bent = doc.image;
    doc.undo();
    require(doc.curve.line_set && doc.curve.geometry.first_control.y == 20 &&
                std::equal(baseline.pixels.begin(), baseline.pixels.end(), doc.image.pixels.begin(),
                           paint::equal),
            "curve undo failed to restore both its editable handle and its raster");
    doc.redo();
    require(doc.curve.line_set && doc.curve.geometry.first_control.y == 110 &&
                std::equal(bent.pixels.begin(), bent.pixels.end(), doc.image.pixels.begin(), paint::equal),
            "curve redo lost its editable geometry or raster");
    doc.saved_revision = doc.revision;
    doc.sync_curve();
    require(!doc.dirty() && doc.curve.line_set, "saved curve was flattened or left dirty");
    doc.commit_curve();
    require(!doc.curve.base &&
                std::equal(bent.pixels.begin(), bent.pixels.end(), doc.image.pixels.begin(), paint::equal),
            "releasing a curve repainted translucent coverage");
    doc.undo();
    require(!doc.curve.base && std::equal(baseline.pixels.begin(), baseline.pixels.end(),
                                          doc.image.pixels.begin(), paint::equal),
            "undo after curve release resurrected handles or lost the prior raster");
    doc.undo();
    require(!doc.curve.base && doc.image.get(50, 20).r == 255, "curve baseline undo failed");
}
void test_line_smoothing() {
    paint::Image smooth, hard;
    smooth.reset(64, 64, {255, 255, 255, 255});
    hard = smooth;
    paint::Ink ink;
    ink.size = 3;
    paint::stroke(smooth, {7, 9}, {53, 37}, ink);
    ink.smooth = false;
    paint::stroke(hard, {7, 9}, {53, 37}, ink);
    int gray = 0, black = 0;
    for (std::size_t i = 0; i < hard.pixels.size(); ++i) {
        require(hard.pixels[i].r == 0 || hard.pixels[i].r == 255,
                "hard lines contain only whole-pixel edges");
        if (hard.pixels[i].r == 0) {
            ++black;
        }
        if (smooth.pixels[i].r > 0 && smooth.pixels[i].r < 255) {
            ++gray;
        }
    }
    require(gray > 20 && black > 80,
            "smoothing toggle preserves a solid stroke while changing edge coverage");
}
void test_path_history() {
    paint::Document doc;
    doc.new_image(64, 64);
    doc.continuous_path = true;
    doc.ink.primary = {0, 0, 0, 170};
    doc.ink.size = 3;
    doc.add_path_node({8, 8});
    doc.add_path_node({48, 8});
    paint::Image line = doc.image;
    doc.add_path_node({48, 48});
    paint::Image corner = doc.image;
    require(doc.undo_history.size() == 3, "path nodes were grouped into one undo step");
    doc.end_path_geometry();
    require(doc.path.nodes.size() == 3 && !doc.path.extending && doc.undo_history.size() == 3,
            "ending geometry discarded junctions or inserted an empty undo step");
    doc.add_path_node({8, 8});
    require(std::equal(corner.pixels.begin(), corner.pixels.end(), doc.image.pixels.begin(), paint::equal),
            "a new run connected itself to the previous endpoint");
    doc.add_path_node({8, 48});
    require(doc.image.get(8, 30).r < 255, "branch from a retained junction is missing");
    doc.undo();
    require(doc.path.nodes.size() == 4 && doc.path.extending && doc.image.get(8, 30).r == 255 &&
                doc.image.get(48, 30).r < 255,
            "branch undo removed old geometry or kept the new segment");
    doc.undo();
    require(doc.path.nodes.size() == 3 && !doc.path.extending,
            "undo did not remove the new run's starting node");
    doc.undo();
    require(doc.path.nodes.size() == 2 && doc.path.extending &&
                std::equal(line.pixels.begin(), line.pixels.end(), doc.image.pixels.begin(), paint::equal),
            "undo across a stopped run did not restore the prior editable segment");
    doc.redo();
    require(
        doc.path.nodes.size() == 3 && !doc.path.extending &&
            std::equal(corner.pixels.begin(), corner.pixels.end(), doc.image.pixels.begin(), paint::equal),
        "redo lost nodes, the stopped state, or translucent coverage");
    doc.commit_path();
    doc.undo();
    require(doc.path.nodes.empty() && doc.path.session == 0 && doc.image.get(48, 30).r == 255 &&
                doc.image.get(30, 8).r < 255,
            "released path undo resurrected controls or erased multiple segments");
    doc.redo();
    require(doc.path.nodes.empty() && doc.image.get(48, 30).r < 255,
            "released path redo resurrected controls or lost pixels");

    doc.new_image(64, 64);
    doc.add_path_node({8, 8});
    doc.add_path_node({48, 8});
    doc.undo();
    doc.undo();
    require(doc.path.nodes.empty() && !doc.dirty(), "undoing every node did not restore a clean canvas");
    doc.redo();
    require(doc.path.nodes.size() == 1, "redo could not restore the first live node");
    doc.add_path_node({8, 48});
    require(doc.redo_history.empty() && doc.image.get(30, 8).r == 255 && doc.image.get(8, 30).r < 255,
            "a replacement segment retained the abandoned redo branch");
    doc.saved_revision = doc.revision;
    doc.sync_path();
    require(!doc.dirty(), "saved live path remained dirty solely because of its nodes");
    doc.ink.primary = {200, 10, 30, 255};
    doc.sync_path();
    require(doc.dirty(), "changing a saved live path's appearance did not mark the document dirty");

    doc.new_image(64, 64);
    doc.checkpoint();
    doc.image.set(2, 2, {10, 20, 30, 255});
    doc.add_path_node({8, 8});
    doc.add_path_node({48, 8});
    doc.undo();
    doc.undo();
    doc.undo();
    require(doc.path.nodes.empty() && doc.image.get(2, 2).r == 255,
            "undo through the path's first node did not reach earlier image edits");
    doc.redo();
    doc.redo();
    doc.redo();
    require(doc.path.nodes.size() == 2 && doc.path.extending && doc.image.get(2, 2).r == 10,
            "redo through earlier image edits lost the still-active path session");
    paint::Image transformed;
    transformed.reset(32, 32, {40, 80, 120, 255});
    doc.checkpoint();
    doc.assign_canvas(transformed);
    doc.sync_path();
    require(doc.path.session == 0 && doc.image.width == 32 && doc.image.get(8, 8).b == 120,
            "live path controls overwrote a whole-image transform result");
    doc.undo();
    require(doc.path.session == 0 && doc.image.width == 64 && doc.image.get(2, 2).r == 10,
            "transform undo lost the source drawing or restored obsolete path controls");
}
void test_guides_masks_and_path_swap() {
    // A narrow comb has more perimeter than the main square but less area.
    std::vector<std::uint8_t> islands(80 * 40, 0);
    for (int y = 5; y < 25; ++y) {
        for (int x = 5; x < 25; ++x) {
            islands[y * 80 + x] = 255;
        }
    }
    for (int x = 35; x < 75; ++x) {
        islands[5 * 80 + x] = 255;
        for (int y = 6; y < (x % 2 == 0 ? 15 : 6); ++y) {
            islands[y * 80 + x] = 255;
        }
    }
    const std::vector<paint::Point> outer = paint::mask_outline(islands, 80, 40);
    require(outer.size() == 4 && paint::inside_polygon(outer, 15, 15),
            "guide outline chooses the largest area rather than a smaller jagged island");
    paint::Image source;
    source.reset(40, 40, {248, 248, 248, 255});
    for (int y = 10; y < 30; ++y) {
        for (int x = 10; x < 30; ++x) {
            source.set(x, y, {70, 100, 130, 255});
        }
    }
    for (int y = 16; y < 24; ++y) {
        for (int x = 16; x < 24; ++x) {
            source.set(x, y, {248, 248, 248, 255});
        }
    }
    source.set(6, 7, {246, 248, 249, 255});
    const std::vector<paint::Point> loop{{4, 4}, {36, 4}, {36, 36}, {4, 36}};
    paint::SelectionMask mask = paint::tighten_lasso(source, loop, false);
    require(mask.bounds.x == 10 && mask.bounds.y == 10 && mask.bounds.w == 20 && mask.bounds.h == 20 &&
                mask.coverage[2 * 20 + 2] == 255 && mask.coverage[10 * 20 + 10] == 0,
            "tight lasso retains the ring, excludes its hole, and rejects near-white JPEG flecks");
    paint::SelectionMask hole = paint::tighten_lasso(source, loop, true);
    require(hole.bounds.x == 16 && hole.bounds.y == 16 && hole.bounds.w == 8 && hole.bounds.h == 8 &&
                hole.coverage[4 * 8 + 4] == 255,
            "inner-void lasso selects only the enclosed center background");
    paint::Document lifted;
    lifted.new_image(4, 1);
    lifted.image.pixels = {{19, 35, 97, 121}, {70, 90, 110, 255}, {13, 22, 33, 0}, {7, 8, 9, 255}};
    const paint::Image original = lifted.image;
    paint::SelectionMask feather;
    feather.bounds = {0, 0, 4, 1};
    feather.coverage = {255, 87, 255, 0};
    lifted.select_mask(feather);
    paint::Image visible = lifted.visible_image();
    require(std::equal(visible.pixels.begin(), visible.pixels.end(), original.pixels.begin(), paint::equal),
            "untouched feathered selection previews original RGBA bytes");
    lifted.commit_selection();
    require(std::equal(lifted.image.pixels.begin(), lifted.image.pixels.end(), original.pixels.begin(),
                       paint::equal),
            "placing a feathered selection preserves partial alpha and hidden RGB");
    lifted.select_mask(feather);
    lifted.selection.image.set(1, 0, {255, 0, 0, 255});
    lifted.commit_selection();
    require(lifted.image.get(1, 0).r == 255, "editing selected pixels prevents original-pixel restoration");
    lifted.replace(original, "");
    lifted.select_mask(feather);
    lifted.selection.x = 1;
    lifted.commit_selection();
    require(paint::equal(lifted.image.get(0, 0), lifted.ink.secondary),
            "moving a feathered selection leaves its cut source behind");
    paint::Guide guide;
    guide.selection.bounds = {0, 0, 7, 7};
    guide.selection.coverage.assign(49, 0);
    for (int y = 1; y <= 5; ++y) {
        for (int x = 1; x <= 5; ++x) {
            if (x == 1 || x == 5 || y == 1 || y == 5) {
                guide.selection.coverage[y * 7 + x] = 128;
            }
        }
    }
    const paint::SelectionMask ring = guide.selection;
    guide.seal_selection_body();
    require(guide.blocked(3, 3) == 1 && guide.blocked(0, 3) == 0 &&
                guide.selection.coverage[3 * 7 + 1] == 128,
            "selection guide seals enclosed body without changing feathered edges or exterior");
    const paint::SelectionMask sealed = guide.selection;
    guide.seal_selection_body();
    require(guide.selection.coverage == sealed.coverage, "sealing the guide body is idempotent");
    guide.selection = ring;
    guide.selection.coverage[1 * 7 + 3] = 0;
    guide.seal_selection_body();
    require(guide.blocked(3, 3) == 0, "open silhouettes retain exterior-connected gaps");
    guide.clear();
    guide.nodes = {{9, 9}, {31, 9}, {31, 31}, {9, 31}};
    guide.closed = true;
    paint::Image target = source;
    for (paint::Color& color : target.pixels) {
        color = {255, 0, 0, 255};
    }
    paint::constrain_paint(target, source, guide, false);
    require(paint::equal(target.get(20, 20), source.get(20, 20)) && target.get(3, 3).r == 255,
            "guide protects its body while allowing edits outside");
    guide.fill = false;
    paint::Ink ink;
    ink.primary = {255, 0, 0, 255};
    target.reset(40, 40);
    paint::stencil_flood(target, {2, 2}, ink, guide, false);
    require(target.get(20, 20).g == 255 && target.get(2, 2).g == 0, "line-only guide blocks flood crossing");
    guide.translate({3, -2});
    require(guide.nodes[0].x == 12 && guide.nodes[0].y == 7, "guide translation follows the nodes");
    paint::Document document;
    document.new_image(80, 80);
    document.tool = paint::Tool::Path;
    document.add_path_node({10, 30});
    document.add_path_node({60, 30});
    require(document.swap_path_segment({0, 1}, paint::CurveKind::Bezier) == 0,
            "existing path edge swaps to Bezier");
    document.path.segments[0].geometry.move_handle(0, {20, 5});
    document.sync_path();
    require(document.path_contour(0, 2, false).size() > 2, "swapped path raster uses curved samples");
    document.undo();
    require(document.path.nodes.size() == 2 && document.path.segments.empty(),
            "undo restores original line and active path nodes");
    document.redo();
    require(document.path.segments.size() == 1, "redo restores swapped segment geometry");
}
void test_text_treatments() {
    paint::TextSession text;
    text.begin({0, 0});
    text.resize({0, 0, 260, 140});
    text.style.size = 90;
    text.style.face_path = (std::filesystem::path(__FILE__).parent_path().parent_path() /
                            "assets/fonts/poster/Anton-Regular.ttf")
                               .string();
    text.replace("BO");
    text.style.contour = true;
    text.style.outline_width = 2;
    text.refresh({20, 70, 160, 255}, {255, 210, 40, 255});
    int edge = 0, body = 0, clear = 0;
    for (paint::Color pixel : text.preview.pixels) {
        edge += paint::equal(pixel, {20, 70, 160, 255});
        body += paint::equal(pixel, {255, 210, 40, 255});
        clear += pixel.a == 0;
    }
    require(edge > 100 && body > 100 && clear > 100,
            "outline text keeps primary contours, Alt interiors, and open counters");
    paint::Image flat = text.preview;
    text.style.skew = 0.35;
    text.style.perspective = -0.6;
    text.style.warp = 0.3;
    text.refresh({20, 70, 160, 255}, {255, 210, 40, 255});
    require(std::memcmp(text.preview.pixels.data(), flat.pixels.data(),
                        flat.pixels.size() * sizeof(paint::Color)) != 0,
            "text transform changes the cached raster preview");
    for (int y = 0; y <= 140; y += 7) {
        for (int x = 0; x <= 260; x += 13) {
            paint::Point mapped =
                text.source_point(text.display_point({static_cast<double>(x), static_cast<double>(y)}));
            require(std::hypot(mapped.x - x, mapped.y - y) < 1e-9,
                    "transformed text hit testing inverts the visible mapping");
        }
    }
    text.style.contour = false;
    text.style.skew = text.style.perspective = text.style.warp = 0;
    for (int style = 1; style <= 3; ++style) {
        text.style.word_art = static_cast<paint::WordArt>(style);
        text.refresh({20, 70, 160, 255}, {255, 210, 40, 255});
        require(!text.preview.pixels.empty() && text.edit.content == "BO",
                "WordArt preserves the editable text");
    }
}
void test_codecs() {
    paint::Image image;
    image.reset(17, 13, {62, 147, 219, 255});
    image.set(4, 7, {197, 27, 55, 255});
    const char* formats[] = {"png", "bmp", "tga", "tiff", "jpg"};
    for (const char* format : formats) {
        std::filesystem::path file =
            std::filesystem::temp_directory_path() / (std::string("rainstar-codec-test.") + format);
        paint::save_image(image, file.string());
        paint::save_image(image, file.string()); // Replacing an existing picture must work too.
        paint::Image loaded = paint::load_image(file.string());
        require(loaded.width == 17 && loaded.height == 13, "codec dimensions failed");
        if (std::string(format) != "jpg") {
            require(paint::equal(loaded.get(4, 7), image.get(4, 7)), "lossless codec failed");
        }
        std::filesystem::remove(file);
        std::filesystem::path unicode = std::filesystem::temp_directory_path() /
                                        std::filesystem::path(std::u8string(u8"Rainstar-é-绘画."));
        unicode += format;
        std::u8string encoded_path = unicode.u8string();
        std::string path(encoded_path.begin(), encoded_path.end());
        paint::save_image(image, path);
        paint::save_image(image, path);
        loaded = paint::load_image(path);
        require(loaded.width == image.width && loaded.height == image.height,
                "Unicode filename roundtrip failed");
        std::filesystem::remove(unicode);
    }
    for (const char* format : {"gif", "webp", "avif"}) {
        const std::filesystem::path rejected =
            std::filesystem::temp_directory_path() / (std::string("rainstar-codec-test.") + format);
        bool refused = false;
        try {
            paint::save_image(image, rejected.string());
        } catch (const std::exception&) {
            refused = true;
        }
        require(refused && !paint::writable_image_path(rejected.string()),
                "import-only format remained writable");
    }
}
void test_canvas_backing_settings() {
    const paint::Image original = paint::canvas_backing_texture(paint::CanvasBacking::PaleFelt);
    std::uint64_t checksum = 14695981039346656037ULL;
    for (const paint::Color color : original.pixels) {
        for (const std::uint8_t value : {color.r, color.g, color.b, color.a}) {
            checksum = (checksum ^ value) * 1099511628211ULL;
        }
    }
    require(original.width == 128 && original.height == 128 && checksum == 0xfa489442779bc35eULL,
            "the original grey-blue felt remains byte-identical");
    for (int index = 1; index < paint::canvas_backing_count; ++index) {
        const paint::Image texture = paint::canvas_backing_texture(static_cast<paint::CanvasBacking>(index));
        require(texture.width == 512 && texture.height == 512, "new backing has two samples per UI pixel");
        for (const paint::Color color : texture.pixels) {
            require(color.a == 255, "a surround cannot introduce transparent holes");
        }
    }
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "rainstar-backing-test.txt";
    for (int index = 0; index < paint::canvas_backing_count; ++index) {
        paint::EditorSettings settings;
        settings.storage_path = path.string();
        settings.scroll_distance = 2.5;
        settings.drag_shapes = index % 2 != 0;
        settings.rotate_view = index % 2 == 0;
        settings.canvas_backing = static_cast<paint::CanvasBacking>(index);
        settings.solid_transparency = true;
        settings.transparency_color = {235, 131, 190, 255};
        settings.save();
        paint::EditorSettings loaded;
        loaded.storage_path = settings.storage_path;
        loaded.load();
        require(loaded.canvas_backing == settings.canvas_backing && loaded.scroll_distance == 2.5 &&
                    loaded.drag_shapes == settings.drag_shapes &&
                    loaded.rotate_view == settings.rotate_view && loaded.solid_transparency &&
                    paint::equal(loaded.transparency_color, settings.transparency_color),
                "every backing survives a preference round trip with scroll and transparency settings");
    }
    const std::string legacy[] = {"RSPS1\n3.5\n", "RSPS2\n3.5\n1\n", "RSPS3\n3.5\n1\n1\n#ED82C1\n",
                                  "RSPS4\n3.5\n999\n0\n#FFFFFF\n"};
    for (int index = 0; index < 4; ++index) {
        const std::vector<std::uint8_t> bytes(legacy[index].begin(), legacy[index].end());
        paint::write_file_atomic(bytes, path.string(), "settings migration fixture");
        paint::EditorSettings loaded;
        loaded.storage_path = path.string();
        loaded.load();
        require(loaded.canvas_backing == (index == 1 || index == 2 ? paint::CanvasBacking::MossFelt
                                                                   : paint::CanvasBacking::PaleFelt) &&
                    loaded.scroll_distance == 3.5 && !loaded.drag_shapes && !loaded.rotate_view,
                "old felt preferences migrate and unknown backing values fall back safely");
        if (index == 2) {
            require(loaded.solid_transparency &&
                        paint::equal(loaded.transparency_color, {237, 130, 193, 255}),
                    "legacy settings retain their transparency display");
        }
    }
    std::filesystem::remove(path);
}
void test_file_security() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "rainstar-file-security-tests";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const std::filesystem::path sentinel = directory / "sentinel.txt";
    const std::filesystem::path destination = directory / "picture.bin";
    {
        std::ofstream output(sentinel);
        output << "sentinel";
    }
#ifndef _WIN32
    std::filesystem::create_symlink(sentinel, destination);
    const std::vector<std::uint8_t> linked =
        paint::read_regular_file_bounded(destination.string(), 64, "bounded symlink read failed");
    require(std::string(linked.begin(), linked.end()) == "sentinel",
            "bounded read did not follow a regular-file symlink");
    const std::vector<std::uint8_t> replacement = {'p', 'a', 'i', 'n', 't'};
    paint::write_file_atomic(replacement, destination.string(), "atomic write test failed");
    require(!std::filesystem::is_symlink(destination), "atomic save retained a destination symlink");
    std::ifstream sentinel_input(sentinel);
    std::string sentinel_text;
    sentinel_input >> sentinel_text;
    require(sentinel_text == "sentinel", "atomic save followed a destination symlink");
    const std::filesystem::path pipe = directory / "untrusted-pipe";
    require(mkfifo(pipe.c_str(), 0600) == 0, "could not create file-type test pipe");
    bool special_rejected = false;
    try {
        static_cast<void>(paint::read_regular_file_bounded(pipe.string(), 64, "special file rejected"));
    } catch (const std::exception&) {
        special_rejected = true;
    }
    require(special_rejected, "bounded read accepted a named pipe");
    paint::EditorSettings settings;
    settings.storage_path = pipe.string();
    settings.load();
    paint::RecentFiles recent;
    recent.storage_path = pipe.string();
    recent.load();
    paint::CustomColors colors;
    colors.storage_path = pipe.string();
    colors.load();
    paint::TextSession text;
    text.begin({0, 0});
    text.style.face_path = pipe.string();
    text.edit.content = "safe fallback";
    text.refresh({0, 0, 0, 255}, {255, 255, 255, 255});
    require(!text.preview.pixels.empty(), "unsafe font file did not fall back to the embedded face");
#else
    {
        std::ofstream output(destination, std::ios::binary);
        output << "old";
    }
    // Plan Paint's file-change probe can hold the destination open, sharing
    // read, write and delete, while a save replaces it.
    HANDLE probe = CreateFileW(destination.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    require(probe != INVALID_HANDLE_VALUE, "could not open the probe handle");
    const std::vector<std::uint8_t> replacement = {'p', 'a', 'i', 'n', 't'};
    bool replaced = true;
    try {
        paint::write_file_atomic(replacement, destination.string(), "atomic write test failed");
    } catch (const std::exception&) {
        replaced = false;
    }
    CloseHandle(probe);
    require(replaced, "atomic save failed while a shared handle had the destination open");
    const std::vector<std::uint8_t> saved =
        paint::read_regular_file_bounded(destination.string(), 64, "replaced file read failed");
    require(saved == replacement, "atomic save under a shared handle wrote the wrong bytes");
#endif
    const std::uint8_t gif[] = {'G', 'I', 'F', '8', '9', 'a'};
    bool rejected = false;
    try {
        paint::decode_image(gif, sizeof(gif));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "GIF signature reached stb_image");

    std::vector<std::uint8_t> bmp(30, 0);
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[14] = 40;
    bmp[28] = 8;
    rejected = false;
    try {
        paint::decode_image(bmp.data(), bmp.size());
    } catch (const std::exception& exception) {
        rejected = std::string(exception.what()).find("24-bit or 32-bit") != std::string::npos;
    }
    require(rejected, "paletted BMP reached its unsafe decoder path");

    const std::string image_svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><image href='/etc/passwd' "
        "width='10' height='10'/></svg>";
    const std::vector<std::uint8_t> svg_bytes(image_svg.begin(), image_svg.end());
    const std::filesystem::path svg_path = directory / "external.svg";
    paint::write_file_atomic(svg_bytes, svg_path.string(), "SVG test write failed");
    rejected = false;
    try {
        paint::load_image(svg_path.string());
    } catch (const std::exception& exception) {
        rejected = std::string(exception.what()).find("raster image elements") != std::string::npos;
        if (!rejected) {
            throw std::runtime_error(std::string("SVG rejection used an unexpected path: ") +
                                     exception.what());
        }
    }
    require(rejected, "SVG external image resource remained reachable");

    const std::filesystem::path ordinary_png = directory / "ordinary.png";
    const std::filesystem::path disguised_heif = directory / "disguised.heic";
    paint::Image ordinary;
    ordinary.reset(4, 4, {31, 71, 191, 255});
    paint::save_image(ordinary, ordinary_png.string());
    const std::vector<std::uint8_t> png = paint::read_image_bytes(ordinary_png.string());
    paint::write_file_atomic(png, disguised_heif.string(), "HEIF signature test write failed");
    rejected = false;
    try {
        static_cast<void>(paint::load_image(disguised_heif.string()));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "a non-HEIF file reached the native HEIF decoder");
    std::filesystem::remove_all(directory);
}
} // namespace
int main() {
    try {
        test_icon_size_production();
        test_explicit_icon_targets();
        test_color();
        test_perceptual_tools();
        test_contour_relief();
        test_transformative_brushes();
        test_material_collections_and_dry_contact();
        test_conv();
        test_conv_reference();
        test_conv_area();
        test_editing();
        test_materials_and_shapes();
        test_eraser_and_pixel_target();
        test_continuous_geometry_coverage();
        test_line_smoothing();
        test_path_history();
        test_stamp_masks_and_oblique_edges();
        test_curves();
        test_guides_masks_and_path_swap();
        test_text_treatments();
        test_codecs();
        test_canvas_backing_settings();
        test_file_security();
        std::cout << "Color, CONV, editing, codecs and file-security tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
