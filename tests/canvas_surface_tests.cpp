#include "canvas_surface.hpp"
#include <stx_conv/surface.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
// Flat runs, hard edges, translucent and transparent pixels, and a ramp:
// the shapes the canvas holds.
paint::Image document(int width, int height) {
    paint::Image image;
    image.reset(width, height, {255, 255, 255, 255});
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x > width / 2) {
                image.set(x, y, {static_cast<std::uint8_t>(x * 9), static_cast<std::uint8_t>(y * 5), 90, 255});
            }
            if (x > 4 && x < 15 && y > 3 && y < 12) {
                image.set(x, y, {200, 30, 40, 255});
            }
            if (y == height - 3) {
                image.set(x, y, {20, 120, 220, static_cast<std::uint8_t>(x * 6)});
            }
            if (x == 2 && y > 6) {
                image.set(x, y, {0, 0, 0, 0});
            }
        }
    }
    return image;
}
bool same(const paint::WarpSample& a, const paint::WarpSample& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
// The canvas surface (stxlib::conv's field) evaluates stx_conv's tensor
// surface: its samples are stx_conv::surface::Surface's, bit for bit, at
// nodes, inside cells, on the clamped half-pixel border and at the far edges.
void surface_equals_stx_conv() {
    const paint::Image image = document(41, 29);
    paint::CanvasSurface canvas;
    canvas.build(image);
    std::vector<double> nodes(image.pixels.size() * 4);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const double alpha = image.pixels[i].a / 255.0;
        nodes[i * 4] = image.pixels[i].r / 255.0 * alpha;
        nodes[i * 4 + 1] = image.pixels[i].g / 255.0 * alpha;
        nodes[i * 4 + 2] = image.pixels[i].b / 255.0 * alpha;
        nodes[i * 4 + 3] = alpha;
    }
    stx_conv::surface::SurfaceShape shape;
    shape.nodes_u = static_cast<std::size_t>(image.width);
    shape.nodes_v = static_cast<std::size_t>(image.height);
    shape.channels = 4;
    shape.closure = stx_conv::surface::Closure::paper;
    shape.bubble = stx_conv::surface::Bubble::none;
    stx_conv::surface::Surface reference(shape);
    require(reference.compile(nodes, {}, {}) == stx_conv::Status::ok, "the stx_conv surface compiles");
    std::size_t checked = 0;
    for (int j = -3; j < image.height * 7 - 3; ++j) {
        for (int i = -3; i < image.width * 7 - 3; ++i) {
            const paint::Point position{i / 7.0 - 0.0625, j / 7.0 - 0.0625};
            if (position.x < -0.5 || position.y < -0.5 || position.x >= image.width - 0.5 ||
                position.y >= image.height - 0.5) {
                require(same(canvas.sample_components(position), {}),
                        "outside the footprint is transparent");
                continue;
            }
            double values[4] = {};
            require(reference.evaluate(position.x, position.y, values) == stx_conv::Status::ok,
                    "the stx_conv surface evaluates");
            const paint::WarpSample sample = canvas.sample_components(position);
            require(sample.r == values[0] && sample.g == values[1] && sample.b == values[2] && sample.a == values[3],
                    "the canvas surface is stx_conv's surface, bit for bit");
            ++checked;
        }
    }
    require(checked > 50000, "the comparison covers the image");
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            // Premultiplied pixels keep no colour where they keep no opacity.
            const paint::Color want = image.get(x, y).a == 0 ? paint::Color{0, 0, 0, 0} : image.get(x, y);
            require(paint::equal(canvas.sample({static_cast<double>(x), static_cast<double>(y)}), want),
                    "the surface passes through every pixel");
        }
    }
}
// An edit recompiles only the rows holding a changed pixel; the result is a
// fresh build of the edited picture, bit for bit, and samples more than four
// pixels from the edit are unchanged.
void local_update_is_exact() {
    const paint::Image before = document(53, 37);
    paint::Image after = before;
    for (int i = 0; i < 14; ++i) {
        after.set(20 + i, 15 + i / 3, {240, 160, 20, 255});
        after.set(20 + i, 16 + i / 3, {240, 160, 20, 200});
    }
    after.set(0, 0, {1, 2, 3, 4});
    after.set(52, 36, {9, 9, 9, 255});
    paint::CanvasSurface edited;
    edited.build(before);
    paint::CanvasSurface original;
    original.build(before);
    edited.update(after, {20, 15, 14, 7});
    edited.update(after, {0, 0, 1, 1});
    edited.update(after, {52, 36, 5, 5});
    paint::CanvasSurface fresh;
    fresh.build(after);
    for (int j = -3; j < after.height * 5 - 3; ++j) {
        for (int i = -3; i < after.width * 5 - 3; ++i) {
            const paint::Point position{i / 5.0 + 0.03, j / 5.0 + 0.03};
            require(same(edited.sample_components(position), fresh.sample_components(position)),
                    "a local update equals a fresh build, bit for bit");
            const bool near_stroke = position.x > 20 - 4 && position.x < 34 + 3 && position.y > 15 - 4 &&
                                     position.y < 21 + 3;
            const bool near_corner = (position.x < 4 && position.y < 4) || (position.x > 48 && position.y > 32);
            if (!near_stroke && !near_corner) {
                require(same(edited.sample_components(position), original.sample_components(position)),
                        "samples beyond the surface's reach of an edit are unchanged");
            }
        }
    }
}
// Any area of a frame renders the bytes the whole frame has there.
void areas_render_like_the_whole_frame() {
    const paint::Image image = document(64, 48);
    paint::CanvasSurface surface;
    surface.build(image);
    const double c = std::cos(0.35) / 1.7, s = std::sin(0.35) / 1.7;
    const paint::CanvasSurface::TargetMap map{c, s, -6.25, -s, c, 9.5};
    const int columns = 300, rows = 240;
    std::vector<paint::Color> whole(static_cast<std::size_t>(columns) * rows, paint::Color{1, 2, 3, 4});
    std::vector<paint::Color> parts(whole.size(), paint::Color{5, 6, 7, 8});
    const paint::CanvasSurface::Frame whole_frame{whole.data(), columns, rows, static_cast<std::size_t>(columns)};
    const paint::CanvasSurface::Frame parts_frame{parts.data(), columns, rows, static_cast<std::size_t>(columns)};
    surface.render(map, whole_frame, 0, 0, columns, rows);
    surface.render(map, parts_frame, 0, 0, columns, 100);
    surface.render(map, parts_frame, 0, 100, 37, rows - 100);
    surface.render(map, parts_frame, 37, 100, columns - 37, rows - 100);
    for (std::size_t i = 0; i < whole.size(); ++i) {
        require(paint::equal(whole[i], parts[i]), "an area renders as the whole frame does");
    }
}
// The render is the field's preview transfer masked at the footprint: a
// frame pixel whose centre maps outside [-0.5, width - 0.5) x [-0.5,
// height - 0.5) is transparent, and inside it agrees with sample() at that
// position to one 8-bit level (the transfer rounds its own encoding). An
// unbuilt surface renders a transparent area.
void render_masks_the_footprint() {
    const paint::Image image = document(37, 26);
    paint::CanvasSurface surface;
    surface.build(image);
    const double c = std::cos(-0.6) / 2.3, s = std::sin(-0.6) / 2.3;
    const paint::CanvasSurface::TargetMap map{c, s, -9.0, -s, c, 4.75};
    const int columns = 160, rows = 150;
    std::vector<paint::Color> frame(static_cast<std::size_t>(columns) * rows, paint::Color{9, 9, 9, 9});
    surface.render(map, {frame.data(), columns, rows, static_cast<std::size_t>(columns)}, 0, 0, columns, rows);
    std::size_t inside = 0, outside = 0;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const double cx = x + 0.5, cy = y + 0.5;
            const paint::Point position{map.ux * cx + map.uy * cy + map.u0, map.vx * cx + map.vy * cy + map.v0};
            const paint::Color pixel = frame[static_cast<std::size_t>(y) * columns + x];
            if (position.x < -0.5 || position.y < -0.5 || position.x >= image.width - 0.5 ||
                position.y >= image.height - 0.5) {
                require(paint::equal(pixel, {0, 0, 0, 0}), "beyond the footprint the frame is transparent");
                ++outside;
                continue;
            }
            const paint::Color want = surface.sample(position);
            require(std::abs(pixel.a - want.a) <= 1, "the frame's opacity is the surface's");
            if (want.a >= 16) {
                require(std::abs(pixel.r - want.r) <= 1 && std::abs(pixel.g - want.g) <= 1 &&
                            std::abs(pixel.b - want.b) <= 1,
                        "the frame's colour is the surface's");
            }
            if (pixel.a == 0) {
                require(paint::equal(pixel, {0, 0, 0, 0}), "a frame pixel without opacity keeps no colour");
            }
            ++inside;
        }
    }
    require(inside > 2000 && outside > 2000, "the frame covers the footprint and beyond");
    paint::CanvasSurface unbuilt;
    std::vector<paint::Color> blank(64, paint::Color{9, 9, 9, 9});
    unbuilt.render(map, {blank.data(), 8, 8, 8}, 2, 2, 4, 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const bool in_area = x >= 2 && x < 6 && y >= 2 && y < 6;
            const paint::Color want = in_area ? paint::Color{0, 0, 0, 0} : paint::Color{9, 9, 9, 9};
            require(paint::equal(blank[static_cast<std::size_t>(y) * 8 + x], want),
                    "an unbuilt surface clears only the area");
        }
    }
}
void sizes_are_checked() {
    paint::CanvasSurface surface;
    paint::Image narrow;
    narrow.reset(1, 9);
    bool refused = false;
    try {
        surface.build(narrow);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && !surface.ready(), "a picture under 2 x 2 pixels has no surface");
    paint::Image large;
    large.reset(2049, 2049);
    refused = false;
    try {
        surface.build(large);
    } catch (const std::length_error&) {
        refused = true;
    }
    require(refused && !surface.ready(), "a picture over the limit has no surface");
    paint::Image small;
    small.reset(2, 2, {10, 20, 30, 255});
    surface.build(small);
    require(surface.ready() && paint::equal(surface.sample({1, 1}), {10, 20, 30, 255}), "2 x 2 pixels build");
}
} // namespace
int main() {
    try {
        surface_equals_stx_conv();
        local_update_is_exact();
        areas_render_like_the_whole_frame();
        render_masks_the_footprint();
        sizes_are_checked();
        std::cout << "Canvas surface: stx_conv's surface bit for bit, exact local updates, area renders, the "
                     "footprint mask and size limits passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
