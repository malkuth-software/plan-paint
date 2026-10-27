#include "codecs.hpp"
#include "document.hpp"
#include "fixtures/icon_reference.hpp"
#include "icon_sheet.hpp"
#include "import_codecs.hpp"
#include "conv.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
void same(const paint::Image& a, const paint::Image& b) {
    require(a.width == b.width && a.height == b.height, "image dimensions differ");
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        require(a.pixels[i].a == b.pixels[i].a && (!a.pixels[i].a || paint::equal(a.pixels[i], b.pixels[i])),
                "pixel roundtrip differs");
    }
}
void svg_icon_targets() {
    const std::string path = std::string(FORMAT_FIXTURES) + "/import.svg";
    const paint::ImageContainer source = paint::load_container(path);
    require(paint::svg_source_matches(source.frames[0]), "SVG container retains its validated source");
    paint::Document document;
    document.replace_container(source, path);
    const std::vector<paint::IconGenerationTarget> targets = {
        {0, -1, 16, 16, 32}, {0, -1, 32, 32, 32}, {0, -1, 96, 64, 32}};
    document.generate_icon_targets(targets, {}, false);
    bool differs_from_bitmap = false;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const paint::IconGenerationTarget& target = targets[index];
        const paint::Image direct = paint::rasterize_svg(path, target.width, target.height);
        same(document.atlas.icons[index].image, direct);
        paint::Image scaled;
        paint::conv_resize(source.frames[0].image, target.width, target.height, scaled);
        differs_from_bitmap = differs_from_bitmap || !std::equal(scaled.pixels.begin(), scaled.pixels.end(), direct.pixels.begin(), direct.pixels.end(), paint::equal);
        require(!document.atlas.icons[index].svg_source, "generated targets are independent ordinary pixel images");
    }
    require(differs_from_bitmap, "fixture must distinguish direct vector rasterization from bitmap resampling");
    const std::vector<std::uint8_t> encoded = paint::encode_icon_container(document.output_container(false));
    const paint::ImageContainer decoded = paint::decode_icon_container(encoded.data(), encoded.size());
    for (std::size_t index = 0; index < decoded.frames.size(); ++index) {
        same(decoded.frames[index].image, document.atlas.icons[index].image);
        require(!decoded.frames[index].svg_source, "standard ICO output contains no retained vector source");
    }
    document.undo();
    require(document.atlas.kind == paint::AtlasKind::None && paint::svg_source_matches(document.output_container(false).frames[0]),
            "Undo generation restores the original raster and its vector eligibility");
    document.checkpoint();
    document.image.reset(160, 100, {30, 220, 70, 255});
    require(!paint::svg_source_matches(document.output_container(false).frames[0]),
            "pixel edits disable automatic use of original SVG geometry");
    document.generate_icon_targets({{0, -1, 24, 24, 32}}, {}, false);
    require(paint::equal(document.image.get(12, 12), {30, 220, 70, 255}),
            "generation must preserve edited bitmap artwork instead of silently reverting to SVG");
    document.undo();
    document.undo();
    require(paint::svg_source_matches(document.output_container(false).frames[0]),
            "Undoing the pixel edit restores vector eligibility");
    document.make_icon_sizes({16, 32}, false);
    same(document.atlas.icons[0].image, paint::rasterize_svg(path, 16, 16));
    document.new_image(12, 12);
    require(!document.svg_source, "new ordinary documents release prior SVG provenance");
}
void imports() {
    std::string root = FORMAT_FIXTURES;
    paint::Image avif = paint::load_image(root + "/quadrants.avif");
    require(avif.width == 64 && avif.height == 48, "AVIF dimensions differ");
    require(avif.get(12, 12).r > 200 && avif.get(12, 36).b > 220, "AVIF colors or orientation differ");
#ifdef __APPLE__
    paint::Image heif = paint::load_image(root + "/quadrants.heic");
    require(heif.width == 64 && heif.height == 48, "HEIF dimensions differ");
    require(heif.get(12, 12).r > 200 && heif.get(12, 36).b > 220, "HEIF colors or orientation differ");
#endif
    paint::Image svg = paint::load_image(root + "/import.svg");
    require(svg.width == 160 && svg.height == 100, "SVG intrinsic size differs");
    require(svg.get(0, 0).a == 0 && svg.get(15, 32).r > svg.get(15, 32).b &&
                svg.get(50, 32).b > svg.get(50, 32).r,
            "SVG clip or gradient differs");
    require(svg.get(80, 20).g > 250 && svg.get(80, 20).a >= 126 && svg.get(80, 20).a <= 129,
            "SVG transform or unpremultiplication differs");
    bool filters_rejected = false;
    try {
        paint::load_image(root + "/filters.svg");
    } catch (const std::exception& exception) {
        filters_rejected = std::string(exception.what()).find("filters") != std::string::npos;
    }
    require(filters_rejected, "unsupported SVG filters disappeared silently");
    int text = 0;
    for (int y = 65; y < 95; ++y) {
        for (int x = 5; x < 65; ++x) {
            if (svg.get(x, y).a) {
                ++text;
            }
        }
    }
    require(text > 80, "SVG text not rasterized");
    const char* animations[] = {"/animated.gif", "/animated.png"};
    for (int i = 0; i < 2; ++i) {
        bool rejected = false;
        try {
            paint::load_image(root + animations[i]);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "animation container was silently flattened");
    }
    require(!paint::writable_image_path("drawing.svg") && !paint::writable_image_path("drawing.gif") &&
                !paint::writable_image_path("drawing.webp") && !paint::writable_image_path("drawing.avif") &&
                !paint::writable_image_path("drawing.heic"),
            "import-only format marked writable");
}
void icons() {
    paint::ImageContainer indexed =
        paint::decode_icon_container(paint::test::indexed_icon, sizeof(paint::test::indexed_icon));
    const paint::IconFormat indexed_format = paint::icon_output_format(indexed.frames[0], indexed.kind);
    require(indexed_format.storage == paint::IconStorage::Png && indexed_format.bits_per_pixel == 2 &&
                indexed_format.palette_colors == 3 && indexed.frames[0].image.get(1, 0).a == 128,
            "indexed PNG source palette, depth or partial alpha is wrong");
    require(paint::encode_icon_container(indexed) ==
                std::vector<std::uint8_t>(paint::test::indexed_icon,
                                          paint::test::indexed_icon + sizeof(paint::test::indexed_icon)),
            "indexed PNG payload was converted or directory depth changed");
    indexed.frames[0].image.set(0, 0, {30, 40, 50, 255});
    require(paint::icon_output_format(indexed.frames[0], indexed.kind).bits_per_pixel == 32 &&
                paint::icon_output_format(indexed.frames[0], indexed.kind).storage == paint::IconStorage::Dib,
            "edited indexed PNG did not report its planned RGBA save format");
    paint::ImageContainer container;
    container.kind = paint::ContainerKind::Icon;
    const int sizes[] = {16, 32, 48, 256};
    for (int i = 0; i < 4; ++i) {
        paint::IconFrame frame;
        frame.image.reset(sizes[i], sizes[i], {21, 105, 230, 128});
        frame.image.set(0, 0, {0, 0, 0, 0});
        frame.hotspot_x = i + 1;
        frame.hotspot_y = i + 2;
        container.frames.push_back(frame);
    }
    for (int kind = 0; kind < 2; ++kind) {
        container.kind = kind ? paint::ContainerKind::Cursor : paint::ContainerKind::Icon;
        std::vector<std::uint8_t> bytes = paint::encode_icon_container(container);
        paint::ImageContainer loaded = paint::decode_icon_container(bytes.data(), bytes.size());
        require(loaded.frames.size() == 4 && loaded.kind == container.kind,
                "multi-size container lost entries");
        for (int i = 0; i < 4; ++i) {
            same(container.frames[i].image, loaded.frames[i].image);
            const paint::IconFormat format = paint::icon_output_format(loaded.frames[i], loaded.kind);
            require(paint::icon_source_matches(loaded.frames[i]) && format.bits_per_pixel == 32 &&
                        format.storage ==
                            (!kind && i == 3 ? paint::IconStorage::Png : paint::IconStorage::Dib),
                    "imported representation encoding is not identified");
            if (kind) {
                require(loaded.frames[i].hotspot_x == i + 1 && loaded.frames[i].hotspot_y == i + 2,
                        "CUR hotspot lost");
            }
        }
        require(paint::encode_icon_container(loaded) == bytes,
                "untouched generated payloads changed on save");
        for (std::size_t truncated = 0; truncated < bytes.size();
             truncated += std::max(std::size_t(1), bytes.size() / 71)) {
            bool rejected = false;
            try {
                paint::decode_icon_container(bytes.data(), truncated);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "truncated icon accepted");
        }
    }
    const std::uint8_t (&legacy)[sizeof(paint::test::legacy_cursor)] = paint::test::legacy_cursor;
    paint::ImageContainer cur = paint::decode_icon_container(legacy, sizeof(legacy));
    require(cur.frames[0].source && (*cur.frames[0].source).format.bits_per_pixel == 1 &&
                (*cur.frames[0].source).format.palette_colors == 2 &&
                paint::icon_source_matches(cur.frames[0]),
            "indexed source depth or palette count missing");
    require(cur.frames[0].image.get(0, 0).a == 255 && cur.frames[0].image.get(1, 0).r == 255,
            "legacy CUR colors wrong");
    require(cur.frames[0].image.get(0, 1).a == 0 && paint::legacy_xor_pixel(cur.frames[0], 3),
            "legacy CUR AND/XOR lost");
    std::vector<std::uint8_t> rewritten = paint::encode_icon_container(cur);
    require(rewritten == std::vector<std::uint8_t>(legacy, legacy + sizeof(legacy)),
            "untouched indexed cursor was converted instead of preserving its original payload");
    paint::ImageContainer hotspot = cur;
    hotspot.frames[0].hotspot_x = 0;
    hotspot.frames[0].hotspot_y = 1;
    const std::vector<std::uint8_t> moved = paint::encode_icon_container(hotspot);
    require(moved[10] == 0 && moved[12] == 1 &&
                std::equal(moved.begin() + 22, moved.end(), rewritten.begin() + 22, rewritten.end()),
            "hotspot-only edit changed indexed pixels or lost new directory coordinates");
    paint::ImageContainer variants = cur;
    variants.frames.push_back(cur.frames[0]);
    variants.frames[0].image.set(0, 0, {20, 40, 60, 255});
    const std::vector<std::uint8_t> variant_bytes = paint::encode_icon_container(variants);
    const paint::ImageContainer variant_copy =
        paint::decode_icon_container(variant_bytes.data(), variant_bytes.size());
    require(variant_copy.frames.size() == 2 && (*variant_copy.frames[0].source).format.bits_per_pixel == 32 &&
                (*variant_copy.frames[1].source).format.bits_per_pixel == 1 &&
                (*variant_copy.frames[1].source).payload == (*cur.frames[0].source).payload,
            "editing one representation converted an untouched same-size indexed variant");
    paint::IconFrame hidden = cur.frames[0];
    hidden.image.set(0, 1, {42, 17, 9, 0});
    require(!paint::icon_source_matches(hidden), "hidden RGB edit incorrectly reused imported bytes");
    hidden = cur.frames[0];
    hidden.xor_pixels[3].r = 10;
    require(!paint::icon_source_matches(hidden), "XOR edit incorrectly reused imported bytes");
    paint::ImageContainer restored = paint::decode_icon_container(rewritten.data(), rewritten.size());
    require(paint::legacy_xor_pixel(restored.frames[0], 3) && restored.frames[0].hotspot_x == 1,
            "legacy CUR save lost XOR or hotspot");
    paint::Document legacy_document;
    legacy_document.replace_container(cur, "legacy.cur");
    legacy_document.checkpoint();
    legacy_document.image.set(0, 0, {40, 60, 80, 255});
    require(!paint::icon_source_matches(legacy_document.output_container(true).frames[0]),
            "pending document edits bypass source comparison");
    legacy_document.undo();
    require(paint::encode_icon_container(legacy_document.output_container(true)) == rewritten,
            "Undo did not restore original indexed payload eligibility");
    legacy_document.flip(true);
    paint::ImageContainer mirrored = legacy_document.output_container(true);
    require(paint::legacy_xor_pixel(mirrored.frames[0], 2) && mirrored.frames[0].hotspot_x == 0,
            "legacy flip lost XOR or hotspot");
    legacy_document.rotate(1);
    paint::ImageContainer turned = legacy_document.output_container(true);
    require(paint::legacy_xor_pixel(turned.frames[0], 0) && turned.frames[0].hotspot_x == 1 &&
                turned.frames[0].hotspot_y == 0,
            "legacy rotation lost XOR or hotspot");
    paint::Document document;
    document.replace_container(container, "test.cur");
    require(document.atlas.count() == 4 && document.image.width == 256, "container did not open all sizes");
    document.atlas_select(0, false);
    document.set_hotspot(12, 14);
    document.checkpoint();
    document.image.set(2, 3, {88, 99, 111, 255});
    document.atlas_select(1, false);
    paint::ImageContainer edited = document.output_container(true);
    require(edited.frames[0].hotspot_x == 12 && edited.frames[0].image.get(2, 3).r == 88,
            "frame edits lost after navigation");
    document.undo();
    require(document.atlas.active == 0 && document.image.get(2, 3).r == 21,
            "undo did not restore the edited icon");
}
void icon_sheets() {
    paint::ImageContainer source;
    source.kind = paint::ContainerKind::Icon;
    paint::IconFrame first, second;
    first.image.reset(5, 3, {90, 120, 150, 128});
    second.image.reset(3, 5, {200, 40, 60, 255});
    source.frames = {first, second};
    paint::IconSheetOptions options;
    options.columns = 2;
    options.padding = 2;
    options.gap = 3;
    options.labels = false;
    options.background = paint::IconSheetBackground::Transparent;
    const paint::IconSheetLayout layout = paint::plan_icon_sheet(source, {0, 1}, options);
    require(layout.width == 17 && layout.height == 9 && layout.cells[0].image.x == 2 &&
                layout.cells[0].image.y == 3 && layout.cells[1].image.x == 11 && layout.cells[1].image.y == 2,
            "odd-size contact sheet centering changed dimensions or resampled artwork");
    const paint::Image transparent = paint::render_icon_sheet(source, {0, 1}, options);
    require(paint::equal(transparent.get(2, 3), first.image.get(0, 0)) && transparent.get(0, 0).a == 0 &&
                paint::equal(transparent.get(11, 2), second.image.get(0, 0)),
            "transparent sheet failed to preserve source alpha and exact pixels");
    options.background = paint::IconSheetBackground::Light;
    const paint::Image light = paint::render_icon_sheet(source, {0, 1}, options);
    paint::Image expected;
    expected.reset(1, 1);
    expected.blend(0, 0, first.image.get(0, 0));
    require(paint::equal(light.get(2, 3), expected.get(0, 0)) && light.get(0, 0).a == 255,
            "opaque proof did not composite straight-alpha pixels over its background");
    options.labels = true;
    const paint::IconSheetLayout reordered = paint::plan_icon_sheet(source, {1, 0, 1}, options);
    require(reordered.cells[0].text.starts_with("#2") && reordered.cells[1].text.starts_with("#1") &&
                reordered.cells[2].source_index == 1,
            "sheet labels or ordering lost original representation identity");
    bool rejected = false;
    try {
        paint::plan_icon_sheet(source, {}, options);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "empty contact sheet accepted");
    paint::ImageContainer cursor =
        paint::decode_icon_container(paint::test::legacy_cursor, sizeof(paint::test::legacy_cursor));
    options.labels = false;
    options.padding = 0;
    const paint::Image xor_proof = paint::render_icon_sheet(cursor, {0}, options);
    require(xor_proof.get(1, 1).r == 0 && xor_proof.get(1, 1).a == 255,
            "XOR proof was not evaluated against the selected opaque background");
    options.background = paint::IconSheetBackground::Transparent;
    rejected = false;
    try {
        paint::render_icon_sheet(cursor, {0}, options);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "transparent export silently flattened background-dependent XOR");
    std::atomic<bool> cancel{true};
    require(paint::render_icon_sheet(source, {0}, options, &cancel).pixels.empty(),
            "cancelled sheet published partial output");
    source.frames[0].image.reset(256, 256);
    options.columns = 16;
    options.padding = 64;
    options.gap = 64;
    rejected = false;
    try {
        paint::plan_icon_sheet(source, std::vector<int>(256, 0), options);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "oversized contact sheet was not rejected at layout preflight");
    require(paint::equal(first.image.get(0, 0), {90, 120, 150, 128}), "proofing mutated source pixels");
}
void atlas() {
    paint::Document document;
    document.new_image(20, 16);
    document.image.set(19, 15, {77, 88, 99, 255});
    paint::AtlasGrid grid;
    grid.rows = 3;
    grid.columns = 3;
    grid.margin_x = 1;
    grid.margin_y = 1;
    grid.spacing_x = 1;
    grid.spacing_y = 1;
    document.configure_atlas(grid);
    require(!document.dirty(), "grid setup dirties pixels");
    require(document.image.width == 5 && document.image.height == 4, "row/column grid size wrong");
    document.checkpoint();
    document.image.set(0, 0, {200, 40, 80, 255});
    document.atlas_select(3, true);
    document.atlas_select(6, true);
    require(document.atlas.sequence == std::vector<int>({0, 3, 6}), "vertical Ctrl sequence wrong");
    document.atlas_step(1);
    require(document.atlas.active == 0 && document.image.get(0, 0).r == 200,
            "selected frame stepping lost edit");
    document.atlas_step(-1);
    require(document.atlas.active == 6, "reverse sequence did not wrap");
    paint::Image sheet = document.output_image();
    require(sheet.width == 20 && sheet.height == 16 && sheet.get(1, 1).r == 200 && sheet.get(19, 15).r == 77,
            "sheet output lost frame or unused edge");
    document.undo();
    require(document.atlas.active == 0 && document.image.get(0, 0).r == 255, "frame undo failed");
    document.redo();
    require(document.output_image().get(1, 1).r == 200, "frame redo failed");
    document.atlas_select(-1, false);
    document.checkpoint();
    document.image.set(7, 6, {11, 22, 33, 255});
    document.atlas_select(4, false);
    require(document.image.get(0, 0).r == 11, "whole-sheet edits not reflected in frame");
    document.leave_atlas();
    require(document.image.width == 20 && document.image.get(7, 6).r == 11, "leaving grid lost edits");
    require(paint::sprite_sheet_filename("hero_walk.png") &&
                paint::sprite_sheet_filename("SPRITE-SHEET.PNG") &&
                !paint::sprite_sheet_filename("family.png"),
            "filename-only heuristic wrong");
}
} // namespace
int main() {
    try {
        imports();
        svg_icon_targets();
        icons();
        icon_sheets();
        atlas();
        std::cout << "Import formats, icon/cursor containers, Atlas edits and history passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
