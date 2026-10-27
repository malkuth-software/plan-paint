#pragma once
#include "atlas.hpp"
namespace paint {
enum class IconPaletteDither { None, FloydSteinberg };
struct IconPaletteOptions {
    unsigned bits_per_pixel = 8;
    unsigned alpha_threshold = 128;
    Color matte{255, 255, 255, 255};
    IconPaletteDither dither = IconPaletteDither::None;
    // Empty builds a palette; otherwise use these exact ordered opaque RGB entries.
    std::vector<Color> fixed_palette;
};
struct IconPaletteResult {
    IconFrame frame;
    std::vector<Color> palette;
    std::size_t transparent_pixels = 0, partial_pixels = 0;
};
// Independent weighted median-cut in encoded RGB; see docs/ICON_EDITING.md.
// Builds an indexed DIB snapshot retained until the result's pixels are edited.
IconPaletteResult convert_icon_palette(const IconFrame& source, const IconPaletteOptions& options);
// Standard GIMP RGB palette interchange. Color names are not pixel identities.
std::vector<Color> decode_icon_palette(const std::string& text);
std::string encode_icon_palette(const std::vector<Color>& colors);
std::vector<Color> load_icon_palette(const std::string& path);
// Editing uses retained indices, never an RGB-to-index guess. Ordinary RGBA
// changes invalidate this identity until an explicit conversion is performed.
bool has_icon_palette(const IconFrame& frame);
IconFrame edit_icon_palette_entry(const IconFrame& source, unsigned entry, Color color);
IconFrame paint_icon_indices(const IconFrame& source, const std::vector<std::size_t>& pixels, unsigned entry,
                             bool transparent = false);
} // namespace paint
