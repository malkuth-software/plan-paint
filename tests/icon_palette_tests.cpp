#include "codecs.hpp"
#include "icon_palette.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void check_roundtrip(const paint::IconFrame& frame, unsigned depth, paint::ContainerKind kind) {
    const paint::ImageContainer container{kind, {frame}};
    const std::vector<std::uint8_t> bytes = paint::encode_icon_container(container);
    const paint::ImageContainer decoded = paint::decode_icon_container(bytes.data(), bytes.size());
    const paint::IconFrame& copy = decoded.frames[0];
    require(paint::icon_output_format(copy, kind).bits_per_pixel == depth, "indexed depth survives save");
    require(copy.image.width == frame.image.width && copy.image.height == frame.image.height,
            "indexed dimensions survive save");
    require(std::equal(copy.image.pixels.begin(), copy.image.pixels.end(), frame.image.pixels.begin(),
                       frame.image.pixels.end(), paint::equal),
            "indexed pixels and mask roundtrip");
    require(copy.xor_pixels.empty(), "transparent pixels never become legacy XOR");
    require(paint::encode_icon_container(decoded) == bytes, "encoded indexed snapshot preserved");
    if (kind == paint::ContainerKind::Cursor) {
        require(copy.hotspot_x == frame.hotspot_x && copy.hotspot_y == frame.hotspot_y,
                "cursor hotspot survives indexed conversion");
    }
}
void shared_palette_contracts() {
    const std::vector<paint::Color> colors =
        paint::decode_icon_palette("GIMP Palette\r\nName: Shared\r\nColumns: 4\r\n# RGB\r\n255 0 0 Red\r\n0 "
                                   "0 0 Black\r\n255 0 0 Duplicate\r\n0 0 255 Blue\r\n");
    require(colors.size() == 4 && paint::equal(colors[0], colors[2]), "GPL retains order and duplicates");
    const std::vector<paint::Color> roundtrip =
        paint::decode_icon_palette(paint::encode_icon_palette(colors));
    require(std::equal(colors.begin(), colors.end(), roundtrip.begin(), roundtrip.end(), paint::equal),
            "GPL roundtrip preserves ordered RGB entries");
    require(paint::decode_icon_palette("GIMP Palette\n1 2 3\n").size() == 1, "old GPL headers supported");
    for (const std::string& invalid :
         {"Wrong\n0 0 0\n", "GIMP Palette\n", "GIMP Palette\n256 0 0\n", "GIMP Palette\n-1 0 0\n",
          "GIMP Palette\n1 2 3.5\n", "GIMP Palette\nName: x\nColumns: 256\n1 2 3\n"}) {
        bool rejected = false;
        try {
            static_cast<void>(paint::decode_icon_palette(invalid));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "invalid GPL rejected before conversion");
    }
    paint::IconFrame source;
    source.image.reset(3, 1, {250, 0, 0, 255});
    source.image.set(1, 0, {20, 30, 40, 0});
    source.image.set(2, 0, {0, 0, 250, 255});
    source.hotspot_x = 2;
    paint::IconPaletteOptions options;
    options.bits_per_pixel = 4;
    options.fixed_palette = colors;
    const paint::IconFrame converted = paint::convert_icon_palette(source, options).frame;
    require((*converted.source).indices == std::vector<unsigned>({0, 1, 3}),
            "fixed mapping uses first equal RGB and explicit black mask index");
    require(std::equal(colors.begin(), colors.end(), (*converted.source).palette.begin(), paint::equal),
            "conversion does not reorder shared palette entries");
    check_roundtrip(converted, 4, paint::ContainerKind::Cursor);
    options.dither = paint::IconPaletteDither::FloydSteinberg;
    check_roundtrip(paint::convert_icon_palette(source, options).frame, 4, paint::ContainerKind::Cursor);
    options.fixed_palette = {{255, 0, 0, 255}, {255, 255, 255, 255}};
    bool rejected = false;
    try {
        static_cast<void>(paint::convert_icon_palette(source, options));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "transparent shared conversion requires black instead of inventing a color or XOR");
    options.fixed_palette = colors;
    options.bits_per_pixel = 1;
    rejected = false;
    try {
        static_cast<void>(paint::convert_icon_palette(source, options));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "over-capacity shared palette is rejected without reducing colors");
}
void indexed_edit_contracts() {
    paint::IconFrame source;
    source.image.reset(2, 2, {20, 40, 60, 255});
    source.image.set(1, 0, {220, 230, 240, 255});
    source.hotspot_x = 1;
    paint::IconPaletteOptions options;
    options.bits_per_pixel = 1;
    paint::IconFrame indexed = paint::convert_icon_palette(source, options).frame;
    std::vector<std::uint8_t> bytes = paint::encode_icon_container({paint::ContainerKind::Cursor, {indexed}});
    // Both entries have identical RGB, but the top-right pixel still uses entry 1.
    for (std::size_t channel = 0; channel < 3; ++channel) {
        bytes[22 + 44 + channel] = bytes[22 + 40 + channel];
    }
    indexed = paint::decode_icon_container(bytes.data(), bytes.size()).frames[0];
    require(paint::has_icon_palette(indexed), "import retains editable indexed identities");
    require((*indexed.source).indices == std::vector<unsigned>({0, 1, 0, 0}),
            "import retains duplicate-color pixel indices");
    const paint::IconFrame recolored = paint::edit_icon_palette_entry(indexed, 1, {170, 30, 80, 255});
    require(paint::equal(recolored.image.get(1, 0), {170, 30, 80, 255}) &&
                paint::equal(recolored.image.get(0, 0), indexed.image.get(0, 0)),
            "entry editing changes only pixels using that index, not equal RGB entries");
    require((*recolored.source).indices == (*indexed.source).indices && recolored.hotspot_x == 1,
            "recolor retains indices and hotspot");
    check_roundtrip(recolored, 1, paint::ContainerKind::Cursor);
    const paint::IconFrame repainted = paint::paint_icon_indices(indexed, {0}, 1);
    require(paint::equal(repainted.image.get(0, 0), indexed.image.get(0, 0)) &&
                (*repainted.source).indices[0] == 1 && (*indexed.source).indices[0] == 0,
            "index painting changes identity even when RGB is identical, preserving the original");
    const paint::IconFrame painted_color = paint::edit_icon_palette_entry(repainted, 1, {1, 2, 3, 255});
    require(paint::equal(painted_color.image.get(0, 0), {1, 2, 3, 255}) &&
                paint::equal(painted_color.image.get(1, 0), {1, 2, 3, 255}),
            "subsequent palette edit follows the newly painted indices");
    require(paint::paint_icon_indices(indexed, {}, 1).source == indexed.source &&
                paint::edit_icon_palette_entry(indexed, 1, (*indexed.source).palette[1]).source ==
                    indexed.source,
            "no-op edits preserve exact source payload identity");
    bool rejected = false;
    try {
        static_cast<void>(paint::paint_icon_indices(indexed, {0, 4}, 1));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected && (*indexed.source).indices[0] == 0, "invalid index edit is atomic");
    paint::IconFrame modified = indexed;
    modified.image.set(0, 0, {1, 2, 3, 255});
    require(!paint::has_icon_palette(modified), "RGBA edits cannot use stale indices");
    source.image.reset(2, 2, {0, 0, 0, 255});
    source.image.set(1, 0, {250, 250, 250, 255});
    source.image.set(0, 1, {0, 0, 0, 0});
    const paint::IconFrame transparent = paint::convert_icon_palette(source, options).frame;
    rejected = false;
    try {
        static_cast<void>(paint::edit_icon_palette_entry(transparent, 0, {255, 0, 0, 255}));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "mask black cannot become a background-dependent XOR color");
    const paint::IconFrame erased = paint::paint_icon_indices(transparent, {1}, 0, true);
    require(erased.image.get(1, 0).a == 0, "indexed erase preserves a separate binary mask");
    check_roundtrip(erased, 1, paint::ContainerKind::Cursor);
    const paint::IconFrame opaque = paint::paint_icon_indices(erased, {1}, 0, false);
    require(paint::equal(opaque.image.get(1, 0), {0, 0, 0, 255}), "black index can be opaque or transparent");
    check_roundtrip(opaque, 1, paint::ContainerKind::Cursor);
}
void conversion_contracts() {
    paint::IconFrame source;
    source.image.reset(2, 2, {0, 0, 0, 255});
    source.image.set(1, 0, {255, 255, 255, 255});
    source.image.set(0, 1, {255, 255, 255, 255});
    source.hotspot_x = 1;
    paint::IconPaletteOptions options;
    options.bits_per_pixel = 1;
    const paint::IconPaletteResult monochrome = paint::convert_icon_palette(source, options);
    const std::vector<std::uint8_t>& dib = (*monochrome.frame.source).payload;
    require(dib.size() == 64 && dib[14] == 1 && dib[32] == 2, "1-bit DIB header and size");
    require(dib[48] == 0x80 && dib[52] == 0x40 && dib[56] == 0 && dib[60] == 0,
            "bottom-up MSB-first packed pixels and DWORD-aligned masks");
    check_roundtrip(monochrome.frame, 1, paint::ContainerKind::Cursor);
    source.image.set(0, 0, {0, 0, 0, 0});
    const paint::IconPaletteResult masked_mono = paint::convert_icon_palette(source, options);
    require(paint::equal(masked_mono.frame.image.get(0, 0), {0, 0, 0, 0}) &&
                paint::equal(masked_mono.frame.image.get(1, 0), {255, 255, 255, 255}) &&
                paint::equal(masked_mono.frame.image.get(1, 1), {0, 0, 0, 255}),
            "one-bit black, white and transparency share the black entry with separate AND bits");
    check_roundtrip(masked_mono.frame, 1, paint::ContainerKind::Cursor);
    source.image.reset(5, 3, {20, 80, 120, 255});
    source.image.set(0, 0, {255, 17, 90, 0});
    source.image.set(1, 0, {0, 0, 0, 127});
    source.image.set(2, 0, {0, 0, 0, 128});
    source.image.set(4, 2, {250, 60, 90, 255});
    source.hotspot_x = 4;
    source.hotspot_y = 2;
    options.bits_per_pixel = 4;
    const paint::IconPaletteResult indexed = paint::convert_icon_palette(source, options);
    require(indexed.transparent_pixels == 2 && indexed.partial_pixels == 2, "transparency loss counted");
    require(paint::equal(indexed.frame.image.get(0, 0), {0, 0, 0, 0}) &&
                indexed.frame.image.get(1, 0).a == 0 &&
                paint::equal(indexed.frame.image.get(2, 0), {127, 127, 127, 255}),
            "127 transparent, 128 matte-composited and opaque");
    for (unsigned depth : {1u, 8u}) {
        paint::IconPaletteOptions alternative = options;
        alternative.bits_per_pixel = depth;
        check_roundtrip(paint::convert_icon_palette(source, alternative).frame, depth,
                        paint::ContainerKind::Cursor);
    }
    require(source.image.get(0, 0).r == 255 && source.image.get(2, 0).a == 128, "source untouched");
    check_roundtrip(indexed.frame, 4, paint::ContainerKind::Cursor);
    require((*indexed.frame.source).payload ==
                (*paint::convert_icon_palette(source, options).frame.source).payload,
            "palette construction deterministic");
    paint::IconFrame edited = indexed.frame;
    edited.image.set(0, 0, {17, 18, 19, 255});
    require(paint::icon_output_format(edited, paint::ContainerKind::Icon).bits_per_pixel == 32,
            "further editing invalidates indexed snapshot");
    source.image.reset(256, 256);
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            source.image.set(x, y, {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), 110, 255});
        }
    }
    options.bits_per_pixel = 8;
    const paint::IconPaletteResult plain = paint::convert_icon_palette(source, options);
    options.dither = paint::IconPaletteDither::FloydSteinberg;
    const paint::IconPaletteResult dithered = paint::convert_icon_palette(source, options);
    require(dithered.palette.size() == 256, "8-bit palette uses full capacity");
    require(!std::equal(plain.frame.image.pixels.begin(), plain.frame.image.pixels.end(),
                        dithered.frame.image.pixels.begin(), paint::equal),
            "dither changes gradient output");
    require((*dithered.frame.source).payload ==
                (*paint::convert_icon_palette(source, options).frame.source).payload,
            "error diffusion deterministic");
    check_roundtrip(dithered.frame, 8, paint::ContainerKind::Icon);
    source.image.reset(7, 2, {200, 60, 30, 0});
    source.hotspot_x = 0;
    source.hotspot_y = 0;
    for (unsigned depth : {1u, 4u, 8u}) {
        options.bits_per_pixel = depth;
        const paint::IconPaletteResult empty = paint::convert_icon_palette(source, options);
        require(empty.transparent_pixels == 14 && empty.palette.size() == 1, "empty icon palette");
        check_roundtrip(empty.frame, depth, paint::ContainerKind::Icon);
    }
    source.xor_pixels.resize(14, {255, 255, 255, 255});
    source.image.reset(7, 2, {255, 255, 255, 0});
    bool rejected = false;
    try {
        static_cast<void>(paint::convert_icon_palette(source, options));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "legacy XOR is rejected before conversion");
    source.xor_pixels.clear();
    source.image.pixels.pop_back();
    rejected = false;
    try {
        static_cast<void>(paint::convert_icon_palette(source, options));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "malformed image geometry is rejected");
}
} // namespace
int main() {
    try {
        conversion_contracts();
        indexed_edit_contracts();
        shared_palette_contracts();
        std::cout << "Icon palette contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
