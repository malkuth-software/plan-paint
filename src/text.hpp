#pragma once
#include "image.hpp"
namespace paint {
enum class WordArt { Plain, Extruded, Embossed, Sunset };
struct TextStyle {
    int size = 24;
    bool contour = false;
    double outline_width = 2, skew = 0, perspective = 0, warp = 0;
    WordArt word_art = WordArt::Plain;
    std::string face_path;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeout = false;
    bool mono = false;
    bool opaque = false;
    bool word_wrap = true;
};
struct TextGlyph {
    std::uint32_t codepoint = 0;
    std::size_t begin = 0, end = 0;
    int x = 0, y = 0, advance = 0;
};
struct TextLayout {
    std::vector<TextGlyph> glyphs;
    std::vector<Point> carets;
    int width = 0, height = 0, line_height = 0, ascent = 0;
};
TextLayout layout_text(const std::string& text, const TextStyle& style, int wrap_width = 0);
void render_text(Image& image, Point origin, const TextLayout& layout, const TextStyle& style, Color color,
                 Color background);
std::size_t previous_text_boundary(const std::string& text, std::size_t cursor);
std::size_t next_text_boundary(const std::string& text, std::size_t cursor);
void draw_text(Image& image, Point origin, const std::string& text, const TextStyle& style, Color color,
               Color background, const std::string& font_path);
extern const unsigned char embedded_font[];
extern const unsigned int embedded_font_size;
extern const unsigned char embedded_font_bold[];
extern const unsigned int embedded_font_bold_size;
extern const unsigned char embedded_font_italic[];
extern const unsigned int embedded_font_italic_size;
extern const unsigned char embedded_font_bolditalic[];
extern const unsigned int embedded_font_bolditalic_size;
extern const unsigned char embedded_font_mono[];
extern const unsigned int embedded_font_mono_size;
struct EmbeddedFont {
    const unsigned char* data = nullptr;
    unsigned int size = 0;
};
EmbeddedFont portsmouth_face(const TextStyle& style);
} // namespace paint
