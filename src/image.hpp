#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace paint {
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};
bool equal(Color left, Color right);
struct Point {
    double x = 0.0;
    double y = 0.0;
};
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};
struct Lab {
    double l = 0.0;
    double a = 0.0;
    double b = 0.0;
};
Lab to_oklab(Color color);
Color from_oklab(Lab lab);
std::string to_hex(Color color);
bool from_hex(const std::string& text, Color& color);

// Straight-alpha sRGB RGBA, rows contiguous. Bounds validated on reset.
// Replacement is prepared before publication; allocation failure preserves the image.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;
    void reset(int new_width, int new_height, Color fill = {255, 255, 255, 255});
    bool contains(int x, int y) const;
    Color get(int x, int y) const;
    void set(int x, int y, Color color);
    void blend(int x, int y, Color color);
};
static_assert(sizeof(Color) == 4);
Image cropped(const Image& source, Rect bounds);
void composite(Image& destination, const Image& source, int x, int y);
} // namespace paint
