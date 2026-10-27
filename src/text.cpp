#include "text.hpp"
#include "safe_file.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <algorithm>
#include <cmath>
namespace paint {
EmbeddedFont portsmouth_face(const TextStyle& style) {
    if (style.mono) {
        return {embedded_font_mono, embedded_font_mono_size};
    }
    if (style.bold && style.italic) {
        return {embedded_font_bolditalic, embedded_font_bolditalic_size};
    }
    if (style.bold) {
        return {embedded_font_bold, embedded_font_bold_size};
    }
    if (style.italic) {
        return {embedded_font_italic, embedded_font_italic_size};
    }
    return {embedded_font, embedded_font_size};
}
static std::uint32_t next_codepoint(const std::string& text, std::size_t& cursor) {
    std::uint32_t value = static_cast<unsigned char>(text[cursor++]);
    if (value < 128) {
        return value;
    }
    int following = value < 224 ? 1 : value < 240 ? 2 : 3;
    value &= following == 1 ? 31 : following == 2 ? 15 : 7;
    for (int i = 0; i < following && cursor < text.size(); ++i) {
        unsigned char byte = static_cast<unsigned char>(text[cursor++]);
        if ((byte & 192) != 128) {
            return 0xfffd;
        }
        value = (value << 6) | (byte & 63);
    }
    return value;
}
namespace {
class RasterFont {
  public:
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    float scale = 1;
    int ascent = 0, line_height = 0, bold_width = 0;
    bool synthetic_italic = false;
    explicit RasterFont(const TextStyle& style) {
        if (!style.face_path.empty()) {
            try {
                bytes = read_regular_file_bounded(style.face_path, 32000000,
                                                  "Paint could not read this font safely.");
            } catch (const std::exception&) {
                bytes.clear();
            }
        }
        EmbeddedFont face = portsmouth_face(style);
        const unsigned char* data = bytes.empty() ? face.data : bytes.data();
        if (!stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
            bytes.clear();
            stbtt_InitFont(&info, face.data, stbtt_GetFontOffsetForIndex(face.data, 0));
        }
        scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(style.size));
        int font_ascent = 0, descent = 0, gap = 0;
        stbtt_GetFontVMetrics(&info, &font_ascent, &descent, &gap);
        ascent = static_cast<int>(std::ceil(font_ascent * scale));
        line_height = std::max(1, static_cast<int>(std::ceil((font_ascent - descent + gap) * scale)));
        // External faces and the single mono face need deterministic synthetic styles.
        bool synthetic = !bytes.empty() || style.mono;
        bold_width = synthetic && style.bold ? std::max(1, style.size / 24) : 0;
        synthetic_italic = synthetic && style.italic;
    }
    int advance(std::uint32_t cp) const {
        int width = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&info, cp == '\t' ? ' ' : static_cast<int>(cp), &width, &bearing);
        int pixels = std::max(1, static_cast<int>(std::round(width * scale)) + bold_width);
        return cp == '\t' ? pixels * 4 : pixels;
    }
};
} // namespace
std::size_t previous_text_boundary(const std::string& text, std::size_t cursor) {
    cursor = std::min(cursor, text.size());
    if (cursor == 0) {
        return 0;
    }
    --cursor;
    while (cursor > 0 && (static_cast<unsigned char>(text[cursor]) & 192) == 128) {
        --cursor;
    }
    return cursor;
}
std::size_t next_text_boundary(const std::string& text, std::size_t cursor) {
    if (cursor >= text.size()) {
        return text.size();
    }
    ++cursor;
    while (cursor < text.size() && (static_cast<unsigned char>(text[cursor]) & 192) == 128) {
        ++cursor;
    }
    return cursor;
}
TextLayout layout_text(const std::string& text, const TextStyle& style, int wrap_width) {
    RasterFont font(style);
    TextLayout result;
    result.line_height = font.line_height;
    result.ascent = font.ascent;
    result.carets.resize(text.size() + 1);
    for (std::size_t cursor = 0; cursor < text.size();) {
        TextGlyph glyph;
        glyph.begin = cursor;
        glyph.codepoint = next_codepoint(text, cursor);
        glyph.end = cursor;
        glyph.advance =
            glyph.codepoint == '\n' || glyph.codepoint == '\r' ? 0 : font.advance(glyph.codepoint);
        result.glyphs.push_back(glyph);
    }
    int x = 0, y = 0;
    bool wrapping = style.word_wrap && wrap_width > 0;
    for (std::size_t i = 0; i < result.glyphs.size(); ++i) {
        TextGlyph& glyph = result.glyphs[i];
        bool space = glyph.codepoint == ' ' || glyph.codepoint == '\t';
        bool newline = glyph.codepoint == '\n';
        bool word_start = !space && !newline && (i == 0 || result.glyphs[i - 1].codepoint <= 32);
        if (wrapping && word_start && x > 0) {
            int word_width = 0;
            for (std::size_t j = i; j < result.glyphs.size() && result.glyphs[j].codepoint > 32; ++j) {
                word_width += result.glyphs[j].advance;
            }
            if (x + word_width > wrap_width) {
                x = 0;
                y += font.line_height;
            }
        }
        if (wrapping && !newline && x > 0 && x + glyph.advance > wrap_width) {
            x = 0;
            y += font.line_height;
        }
        glyph.x = x;
        glyph.y = y;
        for (std::size_t byte = glyph.begin; byte < glyph.end; ++byte) {
            result.carets[byte] = {static_cast<double>(x), static_cast<double>(y)};
        }
        if (newline) {
            x = 0;
            y += font.line_height;
        } else {
            x += glyph.advance;
        }
        result.width = std::max(result.width, x);
        result.carets[glyph.end] = {static_cast<double>(x), static_cast<double>(y)};
    }
    result.height = y + font.line_height;
    return result;
}
void render_text(Image& image, Point origin, const TextLayout& layout, const TextStyle& style, Color color,
                 Color background) {
    if (style.contour || style.word_art != WordArt::Plain) {
        TextStyle plain = style;
        plain.contour = false;
        plain.word_art = WordArt::Plain;
        plain.opaque = false;
        Image mask;
        mask.reset(image.width, image.height, {0, 0, 0, 0});
        render_text(mask, origin, layout, plain, {255, 255, 255, 255}, background);
        int radius = std::clamp(static_cast<int>(std::round(style.outline_width)), 1, 8);
        int depth = style.word_art == WordArt::Extruded ? std::max(2, style.size / 8) : 0;
        Lab dark = to_oklab(color);
        dark.l *= 0.48;
        Color shadow = from_oklab(dark);
        shadow.a = color.a;
        if (style.opaque) {
            for (int y = 0; y < layout.height; ++y) {
                for (int x = 0; x < layout.width; ++x) {
                    image.set(static_cast<int>(origin.x) + x, static_cast<int>(origin.y) + y, background);
                }
            }
        }
        for (int y = std::max(0, static_cast<int>(origin.y) - radius);
             y < std::min(image.height, static_cast<int>(origin.y) + layout.height + radius + depth); ++y) {
            for (int x = std::max(0, static_cast<int>(origin.x) - radius - style.size / 3);
                 x < std::min(image.width,
                              static_cast<int>(origin.x) + layout.width + style.size / 3 + radius + depth);
                 ++x) {
                unsigned body = mask.get(x, y).a, outer = body, inner = body, extrusion = 0;
                if (style.contour) {
                    for (int dy = -radius; dy <= radius; ++dy) {
                        for (int dx = -radius; dx <= radius; ++dx) {
                            if (dx * dx + dy * dy <= radius * radius) {
                                unsigned a = mask.contains(x + dx, y + dy) ? mask.get(x + dx, y + dy).a : 0;
                                outer = std::max(outer, a);
                                inner = std::min(inner, a);
                            }
                        }
                    }
                }
                for (int d = 1; d <= depth; ++d) {
                    if (mask.contains(x - d, y - d)) {
                        extrusion = std::max(extrusion, static_cast<unsigned>(mask.get(x - d, y - d).a));
                    }
                }
                if (extrusion) {
                    Color c = shadow;
                    c.a = static_cast<std::uint8_t>(extrusion * c.a / 255);
                    image.blend(x, y, c);
                }
                Color fill = style.contour ? background : color;
                if (!style.contour && style.word_art == WordArt::Sunset) {
                    double t = std::clamp((y - origin.y) / std::max(1, layout.height - 1), 0.0, 1.0);
                    Lab first = to_oklab(color), last = to_oklab(background);
                    fill = from_oklab({first.l + t * (last.l - first.l), first.a + t * (last.a - first.a),
                                       first.b + t * (last.b - first.b)});
                    fill.a = color.a;
                }
                if (!style.contour && style.word_art == WordArt::Embossed && body) {
                    int before = mask.contains(x - 1, y - 1) ? mask.get(x - 1, y - 1).a : 0;
                    int after = mask.contains(x + 1, y + 1) ? mask.get(x + 1, y + 1).a : 0;
                    Lab bevel = to_oklab(fill);
                    bevel.l = std::clamp(bevel.l + (after - before) / 255.0 * 0.24, 0.0, 1.0);
                    fill = from_oklab(bevel);
                    fill.a = color.a;
                }
                // The eroded interior uses Alt; the complete inner/outer contour uses Primary.
                unsigned interior = style.contour ? inner : body;
                Color c = fill;
                c.a = static_cast<std::uint8_t>(interior * c.a / 255);
                image.blend(x, y, c);
                if (style.contour) {
                    c = color;
                    c.a = static_cast<std::uint8_t>((outer - inner) * c.a / 255);
                    image.blend(x, y, c);
                }
            }
        }
        return;
    }
    RasterFont font(style);
    if (style.opaque) {
        for (int y = 0; y < layout.height; ++y) {
            for (int x = 0; x < layout.width; ++x) {
                image.set(static_cast<int>(origin.x) + x, static_cast<int>(origin.y) + y, background);
            }
        }
    }
    for (std::size_t i = 0; i < layout.glyphs.size(); ++i) {
        const TextGlyph& glyph = layout.glyphs[i];
        if (glyph.codepoint == '\n' || glyph.codepoint == '\r') {
            continue;
        }
        int pen = static_cast<int>(origin.x) + glyph.x;
        int baseline = static_cast<int>(origin.y) + glyph.y + font.ascent;
        int width = 0, height = 0, x_offset = 0, y_offset = 0;
        unsigned char* bitmap = glyph.codepoint == '\t'
                                    ? nullptr
                                    : stbtt_GetCodepointBitmap(&font.info, font.scale, font.scale,
                                                               static_cast<int>(glyph.codepoint), &width,
                                                               &height, &x_offset, &y_offset);
        if (bitmap) {
            for (int y = 0; y < height; ++y) {
                int slant = font.synthetic_italic ? static_cast<int>(std::round(-(y + y_offset) * 0.22)) : 0;
                // Union the bold coverage before compositing; repeated alpha blends darken antialiasing.
                for (int x = 0; x < width + font.bold_width; ++x) {
                    unsigned coverage = 0;
                    for (int b = 0; b <= font.bold_width; ++b) {
                        if (x >= b && x - b < width) {
                            coverage = std::max(coverage, static_cast<unsigned>(bitmap[y * width + x - b]));
                        }
                    }
                    Color pixel = color;
                    pixel.a = static_cast<std::uint8_t>(static_cast<unsigned>(color.a) * coverage / 255);
                    image.blend(pen + x + x_offset + slant, baseline + y + y_offset, pixel);
                }
            }
            stbtt_FreeBitmap(bitmap, nullptr);
        }
        int thickness = std::max(1, style.size / 18);
        for (int y = 0; y < thickness; ++y) {
            for (int x = pen; x < pen + glyph.advance; ++x) {
                if (style.underline) {
                    image.blend(x, baseline + 2 + y, color);
                }
                if (style.strikeout) {
                    image.blend(x, baseline - font.ascent / 3 + y, color);
                }
            }
        }
    }
}
void draw_text(Image& image, Point origin, const std::string& text, const TextStyle& style, Color color,
               Color background, const std::string& font_path) {
    TextStyle resolved = style;
    resolved.face_path = font_path;
    TextLayout layout = layout_text(text, resolved);
    render_text(image, origin, layout, resolved, color, background);
}
} // namespace paint
