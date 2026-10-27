#include "document.hpp"
#include "material.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
void reference_and_tiles(paint::Brush brush) {
    paint::Image actual, reference;
    actual.reset(220, 70, {170, 80, 30, 51});
    reference.reset(880, 280, {170, 80, 30, 51});
    const std::vector<paint::Point> points{{-3, 9}, {205, 54}, {104, 5}};
    paint::Ink ink;
    ink.size = 3;
    ink.brush = brush;
    ink.primary = {20, 80, 210, 133};
    ink.secondary = {210, 50, 30, 95};
    ink.supersample = true;
    paint::polygon(actual, points, ink, true, true, true, brush);
    ink.supersample = false;
    ink.size *= 4;
    std::vector<paint::Point> enlarged;
    for (const paint::Point point : points) {
        enlarged.push_back({point.x * 4 + 1.5, point.y * 4 + 1.5});
    }
    paint::Ink body = ink;
    std::swap(body.primary, body.secondary);
    paint::FillBoundary boundary;
    const paint::RasterSpace space{4, 0, 0};
    paint::material_fill(reference, enlarged, body, brush, &boundary, space);
    paint::MaterialStroke coat(&boundary);
    for (std::size_t i = 0; i < enlarged.size(); ++i) {
        coat.segment(reference, enlarged[i], enlarged[(i + 1) % enlarged.size()], ink, space);
    }
    for (int y = 0; y < actual.height; ++y) {
        for (int x = 0; x < actual.width; ++x) {
            int a = 0, r = 0, g = 0, b = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    const paint::Color color = reference.get(x * 4 + sx, y * 4 + sy);
                    a += color.a;
                    r += color.r * color.a;
                    g += color.g * color.a;
                    b += color.b * color.a;
                }
            }
            const paint::Color expected{static_cast<std::uint8_t>((r + a / 2) / a),
                                        static_cast<std::uint8_t>((g + a / 2) / a),
                                        static_cast<std::uint8_t>((b + a / 2) / a),
                                        static_cast<std::uint8_t>((a + 8) / 16)};
            require(paint::equal(actual.get(x, y), expected),
                    "tiled rendering differs from full 4x render, including alpha and joins");
        }
    }
}
void paper_coordinates() {
    paint::Ink ink;
    ink.size = 12;
    ink.brush = paint::Brush::Crayon;
    ink.pattern = paint::Pattern::Checker;
    ink.primary = {200, 20, 80, 190};
    ink.secondary = {10, 140, 70, 130};
    const paint::MaterialSurface normal(ink, ink.brush);
    ink.size *= 4;
    const paint::MaterialSurface enlarged(ink, ink.brush, {4, -32, 16});
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 32; ++x) {
            require(paint::equal(normal.sample(x, y + 16, 1.25),
                                 enlarged.sample((x + 32) * 4 + 3, y * 4 + 2, 5)),
                    "4x rendering changes paper grain, pattern or pigment depth");
        }
    }
    paint::Image image;
    const paint::Color hidden{13, 29, 71, 0};
    image.reset(210, 60, hidden);
    ink = {};
    ink.supersample = true;
    ink.pattern = paint::Pattern::Checker;
    ink.primary = {230, 30, 60, 255};
    ink.secondary = {10, 120, 190, 255};
    const std::vector<paint::Point> rectangle{{5, 5}, {205, 5}, {205, 40}, {5, 40}};
    paint::polygon(image, rectangle, ink, false, true, true, paint::Brush::Round, &ink);
    for (int x = 8; x < 202; ++x) {
        require(paint::equal(image.get(x, 20), paint::patterned(ink, x, 20)),
                "4x fill changes the pattern scale or introduces a tile seam");
    }
    require(paint::equal(image.get(0, 0), hidden) && paint::equal(image.get(100, 50), hidden),
            "4x rendering changes untouched transparent RGB");
}
void path_history() {
    paint::Document document;
    document.new_image(64, 64);
    document.ink.supersample = true;
    document.continuous_path = true;
    document.add_path_node({8, 10});
    document.add_path_node({51, 29});
    const paint::Image line = document.image;
    document.add_path_node({20, 51});
    const paint::Image corner = document.image;
    document.undo();
    require(document.ink.supersample &&
                std::equal(line.pixels.begin(), line.pixels.end(), document.image.pixels.begin(), paint::equal),
            "4x path undo loses quality or pixels");
    document.redo();
    require(std::equal(corner.pixels.begin(), corner.pixels.end(), document.image.pixels.begin(), paint::equal),
            "4x path redo changes the rendered result");
}
}
int main() {
    try {
        reference_and_tiles(paint::Brush::Round);
        reference_and_tiles(paint::Brush::Watercolor);
        reference_and_tiles(paint::Brush::Crayon);
        paper_coordinates();
        path_history();
        std::cout << "4x rendering, alpha, tile seams, materials and history pass.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
