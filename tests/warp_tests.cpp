#include "warp.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
void near(double a, double b, double tolerance, const char* message) {
    require(std::abs(a - b) <= tolerance, message);
}
// Straight 8-bit from premultiplied channel values, clamped once as a
// transfer clamps them (alpha to [0, 1], colour to [0, alpha]).
paint::Color straight(const double values[4]) {
    const double alpha = std::clamp(values[3], 0.0, 1.0);
    paint::Color result{0, 0, 0, static_cast<std::uint8_t>(std::round(255.0 * alpha))};
    if (alpha > 0.0) {
        result.r = static_cast<std::uint8_t>(std::round(255.0 * std::clamp(values[0], 0.0, alpha) / alpha));
        result.g = static_cast<std::uint8_t>(std::round(255.0 * std::clamp(values[1], 0.0, alpha) / alpha));
        result.b = static_cast<std::uint8_t>(std::round(255.0 * std::clamp(values[2], 0.0, alpha) / alpha));
    }
    return result;
}
// The ground's raw field at ground node coordinates (u, v), clamped to it.
void ground_at(const paint::ConvWarpField& field, double u, double v, double values[4]) {
    const stxlib::OperationStatus status = field.ground().evaluate(u, v, stxlib::MutableView<double>(values, 4));
    require(status.succeeded(), "the ground evaluates");
}
// render_affine's pull-back: destination pixel (x, y) reads the ground's
// node coordinates (ou + a11 x + a12 y, ov + a21 x + a22 y), the source
// position plus the two-pixel margin.
stxlib::conv::AffineMap ground_pull(const paint::AffineMap& map) {
    const double determinant = map.xx * map.yy - map.xy * map.yx;
    const double a11 = map.yy / determinant, a12 = -map.xy / determinant;
    const double a21 = -map.yx / determinant, a22 = map.xx / determinant;
    const stxlib::conv::AffineMap pull{a11, a12, a21, a22, -a11 * map.tx - a12 * map.ty + 2.0,
                                       -a21 * map.tx - a22 * map.ty + 2.0};
    return pull;
}
constexpr int divisions = 256;
// Destination pixel (x, y) as stxlib::conv's affine transfer forms it, on a
// map whose lines are destination columns (|a22| > |a21|): the composited
// transfer (include/stxlib/conv.hpp; transfer.cpp, affine_composited_state),
// not the 2D field. Pass 1 reads each ground row r at the point of column x
// where v = r, u = alpha x + beta r + gamma: its value (preview) or its 1D
// field's average over u +- |alpha| / 2 (canon). Pass 2 compiles that column
// as a 1D field, whose admission acts on pass 1's values, and reads it at
// the pixel's v (preview) or averages it over v +- |a22| / 2 (canon). The
// averages are dense midpoint sums; only their bounded error separates this
// from the transfer.
paint::Color composited_reference(const paint::ConvWarpField& field, const stxlib::conv::AffineMap& pull, int x,
                                  int y, bool canon) {
    require(std::abs(pull.a22) > std::abs(pull.a21), "the reference takes maps whose lines are columns");
    constexpr std::size_t lanes = 4;
    const double alpha = pull.a11 - pull.a12 * pull.a21 / pull.a22;
    const double beta = pull.a12 / pull.a22;
    const double gamma = pull.ou - pull.a12 * pull.ov / pull.a22;
    const double half_u = 0.5 * std::abs(alpha), half_v = 0.5 * std::abs(pull.a22);
    const std::size_t rows = static_cast<std::size_t>(field.height() + 4);
    std::vector<double> line(rows * lanes, 0.0);
    for (std::size_t row = 0; row < rows; ++row) {
        const double u = alpha * x + beta * static_cast<double>(row) + gamma;
        double values[4] = {};
        if (!canon) {
            ground_at(field, u, static_cast<double>(row), values);
            for (std::size_t channel = 0; channel < lanes; ++channel) {
                line[row * lanes + channel] = values[channel];
            }
            continue;
        }
        for (int i = 0; i < divisions; ++i) {
            ground_at(field, u - half_u + 2.0 * half_u * (i + 0.5) / divisions, static_cast<double>(row), values);
            for (std::size_t channel = 0; channel < lanes; ++channel) {
                line[row * lanes + channel] += values[channel] / divisions;
            }
        }
    }
    stxlib::OperationResult<stxlib::conv::LineField> made =
        stxlib::conv::LineField::make(rows, lanes, stxlib::conv::FieldOptions{});
    require(made.succeeded(), "the column field is made");
    stxlib::conv::LineField column = std::move(made).take_value();
    const stxlib::conv::LineSamples samples{stxlib::View<double>(line.data(), line.size()), rows, lanes, lanes};
    require(column.compile(samples).succeeded(), "the column field compiles");
    const double v = pull.ov + pull.a21 * x + pull.a22 * y;
    double sums[4] = {};
    const int count = canon ? divisions : 1;
    for (int i = 0; i < count; ++i) {
        double values[4] = {};
        const double at = canon ? v - half_v + 2.0 * half_v * (i + 0.5) / divisions : v;
        require(column.evaluate(at, stxlib::MutableView<double>(values, 4)).succeeded(),
                "the column field evaluates");
        for (std::size_t channel = 0; channel < lanes; ++channel) {
            sums[channel] += values[channel] / count;
        }
    }
    return straight(sums);
}
// The canon's pixel over the source square [left, left + 1) x [top, top + 1)
// under an axis-aligned map, composited as above.
paint::Color canon_reference(const paint::ConvWarpField& field, double left, double top) {
    const stxlib::conv::AffineMap pull{1.0, 0.0, 0.0, 1.0, left + 0.5 + 2.0, top + 0.5 + 2.0};
    return composited_reference(field, pull, 0, 0, true);
}
// Destination pixel (x, y) through the 2D field, as a mesh map forms it:
// the ground's value at the pulled-back centre (preview), or its average
// over the pulled-back pixel square (canon; dense midpoint sum).
paint::Color field_reference(const paint::ConvWarpField& field, const stxlib::conv::AffineMap& pull, int x, int y,
                             bool canon) {
    constexpr int samples = 96;
    const int count = canon ? samples : 1;
    double sums[4] = {};
    for (int j = 0; j < count; ++j) {
        for (int i = 0; i < count; ++i) {
            const double px = canon ? x - 0.5 + (i + 0.5) / samples : x;
            const double py = canon ? y - 0.5 + (j + 0.5) / samples : y;
            double values[4] = {};
            ground_at(field, pull.ou + pull.a11 * px + pull.a12 * py, pull.ov + pull.a21 * px + pull.a22 * py, values);
            for (int channel = 0; channel < 4; ++channel) {
                sums[channel] += values[channel] / (static_cast<double>(count) * count);
            }
        }
    }
    return straight(sums);
}
void near_color(paint::Color actual, paint::Color expected, const char* message) {
    near(actual.r, expected.r, 1.0, message);
    near(actual.g, expected.g, 1.0, message);
    near(actual.b, expected.b, 1.0, message);
    near(actual.a, expected.a, 1.0, message);
}
void test_constant_and_affine() {
    paint::Image image;
    image.reset(13, 9, {53, 177, 229, 149});
    paint::ConvWarpField field;
    field.compile(image);
    for (double y = 0.0; y <= 8.0; y += 0.13) {
        for (double x = 0.0; x <= 12.0; x += 0.17) {
            require(paint::equal(field.sample({x, y}), {53, 177, 229, 149}), "constant field changed");
        }
    }
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 13; ++x) {
            image.set(x, y,
                      {static_cast<std::uint8_t>(10 + x * 9 + y * 4),
                       static_cast<std::uint8_t>(30 + x * 2 + y * 13), 100, 255});
        }
    }
    field.compile(image);
    for (double y = 0.0; y <= 8.0; y += 0.13) {
        for (double x = 0.0; x <= 12.0; x += 0.17) {
            paint::WarpSample sample = field.sample_premultiplied({x, y});
            near(sample.r, (10 + x * 9 + y * 4) / 255.0, 2.0e-10, "affine red reproduction failed");
            near(sample.g, (30 + x * 2 + y * 13) / 255.0, 2.0e-10, "affine green reproduction failed");
        }
    }
}
void test_identity_and_alpha() {
    paint::Image image;
    image.reset(17, 13);
    std::uint32_t state = 17;
    for (paint::Color& pixel : image.pixels) {
        state = 1664525u * state + 1013904223u;
        pixel = {static_cast<std::uint8_t>(state), static_cast<std::uint8_t>(state >> 8),
                 static_cast<std::uint8_t>(state >> 16), 255};
    }
    paint::ConvWarpField field;
    field.compile(image);
    paint::Image output;
    paint::render_affine(field, {}, 17, 13, output);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        require(paint::equal(image.pixels[i], output.pixels[i]), "identity lost source samples");
    }
    paint::AffineMap quarter{0, -1, 12, 1, 0, 0};
    paint::render_affine(field, quarter, 13, 17, output);
    for (int y = 0; y < 13; ++y) {
        for (int x = 0; x < 17; ++x) {
            require(paint::equal(image.get(x, y), output.get(12 - y, x)),
                    "quarter rotation lost source samples");
        }
    }
    paint::Rect bounds = paint::affine_bounds(field, quarter);
    require(bounds.w == 13 && bounds.h == 17 && bounds.x == 0 && bounds.y == 0, "rotation bounds failed");
    image.reset(13, 9, {255, 0, 0, 0});
    for (int y = 0; y < 9; ++y) {
        for (int x = 6; x < 13; ++x) {
            image.set(x, y, {0, 0, 255, 255});
        }
    }
    field.compile(image);
    for (double x = 4.0; x < 8.0; x += 0.05) {
        paint::Color sample = field.sample({x, 4.2});
        require(sample.r == 0 && sample.g == 0, "transparent hidden red leaked into edge");
        if (sample.a > 0) {
            require(sample.b == 255, "premultiplied edge color lost");
        }
    }
    image.reset(1, 2, {7, 80, 230, 128});
    field.compile(image);
    require(paint::equal(field.sample({0, 1}), image.get(0, 1)), "tiny image extension failed");
    // A one-pixel-wide material is its column's CONV line, constant across.
    paint::Image line;
    line.reset(1, 6);
    for (int y = 0; y < 6; ++y) {
        line.set(0, y, {static_cast<std::uint8_t>(y * 40), 20, static_cast<std::uint8_t>(250 - y * y * 7), 255});
    }
    field.compile(line);
    for (int y = 0; y < 6; ++y) {
        require(paint::equal(field.sample({0.3, static_cast<double>(y)}), line.get(0, y)),
                "a one-pixel-wide material lost its pixels");
    }
    require(paint::equal(field.sample({-0.4, 2.5}), field.sample({0.4, 2.5})),
            "a one-pixel-wide material varies across");
}
void test_mesh() {
    paint::Image image;
    image.reset(80, 60, {3, 90, 230, 255});
    paint::ReshapeMesh mesh = paint::make_reshape_mesh(image, 20);
    require(paint::reshape_mesh_valid(mesh), "initial mesh invalid");
    std::size_t interior = mesh.nodes.size();
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (!mesh.nodes[i].boundary) {
            interior = i;
            break;
        }
    }
    require(interior < mesh.nodes.size(), "mesh lacks interior knobs");
    paint::Point old = mesh.nodes[interior].target;
    require(!paint::move_reshape_node(mesh, interior, {-1000, -1000}), "fold accepted");
    near(mesh.nodes[interior].target.x, old.x, 0.0, "failed move changed node");
    require(paint::move_reshape_node(mesh, interior, {old.x + 0.25, old.y + 0.25}),
            "small interior move rejected");
    require(paint::move_reshape_node(mesh, interior, old), "reset move failed");
    paint::ConvWarpField field;
    field.compile(image);
    // The unmoved mesh's triangle maps are the identity only to rounding:
    // Minification must still take the preview (the canon would average the
    // edge pixels with the transparent ground), so it covers every pixel and
    // gives the Point render's bytes.
    paint::Image output, point;
    paint::render_mesh(field, mesh, 80, 60, output);
    for (paint::Color pixel : output.pixels) {
        require(paint::equal(pixel, {3, 90, 230, 255}), "mesh identity has cracks");
    }
    paint::render_mesh(field, mesh, 80, 60, point, paint::WarpSampling::Point);
    require(std::equal(output.pixels.begin(), output.pixels.end(), point.pixels.begin(), paint::equal),
            "an identity mesh minifies");
    std::vector<paint::Point> outline{{0, 0}, {60, 0}, {60, 20}, {30, 20}, {30, 50}, {0, 50}};
    mesh = paint::make_reshape_mesh(outline, 10);
    require(paint::reshape_mesh_valid(mesh), "concave lasso triangulation failed");
    bool rejected = false;
    try {
        mesh = paint::make_reshape_mesh({{0, 0}, {30, 30}, {0, 30}, {30, 0}}, 10);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "crossing lasso accepted");
}
// The field is stxlib::conv's (the paper's Surface, paper closure, no
// bubble) through the premultiplied pixels, on this 7x8 mixed
// affine/wrapped/curved-edge input: the surface is continuous across cells
// and projected samples stay in the colour domain. (Its equality with
// stx_conv's surface is reported by the one-off benchmarks/conv_compare.)
void test_surface_continuity() {
    paint::Image image;
    image.reset(8, 7);
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 8; ++x) {
            image.set(x, y,
                      {static_cast<std::uint8_t>((x * 31 + y * 17) % 256),
                       static_cast<std::uint8_t>((x * 11 + y * 53) % 256),
                       static_cast<std::uint8_t>((x - 3) * (x - 3) + (y - 3) * (y - 3) < 7 ? 255 : 0), 255});
        }
    }
    paint::ConvWarpField field;
    field.compile(image);
    for (int y = 0; y < 6; ++y) {
        for (int x = 1; x < 7; ++x) {
            paint::WarpSample left = field.sample_components({x - 1.0e-9, y + 0.37});
            paint::WarpSample right = field.sample_components({x + 1.0e-9, y + 0.37});
            near(left.r, right.r, 1.0e-7, "surface discontinuous across a cell edge");
        }
    }
    for (double y = 0; y <= 6; y += .19) {
        for (double x = 0; x <= 7; x += .17) {
            paint::WarpSample sample = field.sample_premultiplied({x, y});
            require(sample.a >= 0 && sample.a <= 1 && sample.r >= 0 && sample.r <= sample.a && sample.g >= 0 &&
                        sample.g <= sample.a && sample.b >= 0 && sample.b <= sample.a,
                    "projected sample left the colour domain");
        }
    }
}
void test_admitted_edge_and_mesh_area() {
    paint::Image image;
    image.reset(13, 11, {0, 0, 0, 255});
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 13; ++x) {
            if (x + 2 * y >= 15) {
                image.set(x, y, {255, 255, 255, 255});
            }
        }
    }
    paint::ConvWarpField field;
    field.compile(image);
    for (double y = .1; y < 9.9; y += .21) {
        for (double x = .1; x < 11.9; x += .23) {
            paint::WarpSample before = field.sample_premultiplied({x - 1.0e-5, y - 2.0e-5});
            paint::WarpSample after = field.sample_premultiplied({x + 1.0e-5, y + 2.0e-5});
            require(after.r - before.r >= -1.0e-9, "admitted edge has wrong-way current");
        }
    }
    image.reset(80, 60, {31, 96, 220, 255});
    field.compile(image);
    paint::ReshapeMesh mesh = paint::make_reshape_mesh(image, 15);
    for (paint::MeshNode& node : mesh.nodes) {
        node.target = {node.source.x + .15 * node.source.y, node.source.y};
    }
    require(paint::reshape_mesh_valid(mesh), "affine mesh unexpectedly invalid");
    paint::Image output;
    paint::render_mesh(field, mesh, 90, 60, output);
    for (int y = 3; y < 57; ++y) {
        for (int x = 13; x < 76; ++x) {
            require(paint::equal(output.get(x, y), {31, 96, 220, 255}),
                    "mesh quadrature has seam or double coverage");
        }
    }
    std::size_t interior = 0;
    while (interior < mesh.nodes.size() && mesh.nodes[interior].boundary) {
        ++interior;
    }
    require(interior < mesh.nodes.size(), "test mesh lacks interior node");
    for (paint::MeshNode& node : mesh.nodes) {
        node.target = node.source;
    }
    paint::Point original = mesh.nodes[interior].source;
    paint::Point integer{std::round(original.x), std::round(original.y)};
    require(paint::move_reshape_node(mesh, interior, integer), "small knob move rejected");
    paint::render_mesh(field, mesh, 80, 60, output, paint::WarpSampling::Point);
    require(paint::equal(output.get(static_cast<int>(integer.x), static_cast<int>(integer.y)),
                         field.sample(original)),
            "mesh does not interpolate knob");
    paint::AffineMap singular{1, 1, 0, 1, 1, 0};
    paint::Image previous = output;
    bool rejected = false;
    try {
        paint::render_affine(field, singular, 80, 60, output);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "singular affine map accepted");
    for (std::size_t i = 0; i < output.pixels.size(); ++i) {
        require(paint::equal(output.pixels[i], previous.pixels[i]), "invalid affine map changed destination");
    }
}
void test_physical_alpha_before_filtering() {
    paint::Image image;
    image.reset(5, 5);
    constexpr paint::Color columns[5] = {
        {0, 0, 0, 0}, {255, 0, 0, 51}, {255, 0, 0, 204}, {204, 0, 0, 255}, {204, 0, 0, 255}};
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            image.set(x, y, columns[x]);
        }
    }
    paint::ConvWarpField field;
    field.compile(image);
    paint::WarpSample raw = field.sample_components({1.685, 2.0});
    near(raw.r, 0.64167095527343732, 1.0e-12, "independent raw color field changed");
    near(raw.a, 0.62268291957710920, 1.0e-12, "independent raw opacity field changed");
    require(raw.r > raw.a + 0.01, "alpha fixture no longer exercises component coupling");
    paint::WarpSample physical = field.sample_premultiplied({1.685, 2.0});
    near(physical.a, raw.a, 0.0, "physical projection changed admitted alpha");
    near(physical.r, physical.a, 0.0, "physical sample contains excess premultiplied red");
    // Area is the canon: the raw field's average over the pixel, clamped once
    // after it is formed (the former quadrature projected each sample first,
    // red 248 here, where late clipping gave 252).
    paint::Image edge;
    paint::render_affine(field, {1, 0, 0.2, 0, 1, 0}, 5, 5, edge, paint::WarpSampling::Area);
    near_color(edge.get(2, 2), canon_reference(field, 1.3, 1.5), "area render is not the ground's composited canon");
    // All channels, including mixed colors and hidden RGB in transparent source pixels.
    std::uint32_t state = 19;
    for (paint::Color& pixel : image.pixels) {
        state = state * 1664525u + 1013904223u;
        pixel = {static_cast<std::uint8_t>(state), static_cast<std::uint8_t>(state >> 8),
                 static_cast<std::uint8_t>(state >> 16), static_cast<std::uint8_t>(state >> 24)};
    }
    image.set(2, 2, {255, 127, 63, 0});
    field.compile(image);
    for (double y = -0.6; y < 4.6; y += 0.11) {
        for (double x = -0.6; x < 4.6; x += 0.13) {
            physical = field.sample_premultiplied({x, y});
            require(physical.a >= 0.0 && physical.a <= 1.0 && physical.r >= 0.0 && physical.r <= physical.a &&
                        physical.g >= 0.0 && physical.g <= physical.a && physical.b >= 0.0 &&
                        physical.b <= physical.a,
                    "physical RGBA left the premultiplied color domain");
        }
    }
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            paint::Color original = image.get(x, y);
            paint::Color expected = original.a == 0 ? paint::Color{0, 0, 0, 0} : original;
            require(paint::equal(field.sample({static_cast<double>(x), static_cast<double>(y)}), expected),
                    "physical projection changed a visible source node");
        }
    }
    // The canon against an independent dense integration of the composited
    // canon's two passes over the pixel's footprint, clamped once.
    paint::Image output;
    paint::render_affine(field, {1, 0, 0.3, 0, 1, 0.4}, 5, 5, output, paint::WarpSampling::Area);
    near_color(output.get(2, 2), canon_reference(field, 1.2, 1.1), "filtered pixel differs from the canon integral");
}
void test_sampling_continuity_and_equivalent_maps() {
    paint::Image image;
    image.reset(5, 5);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            image.set(x, y, {static_cast<std::uint8_t>(10 * x * x), 0, 0, 255});
        }
    }
    paint::ConvWarpField field;
    field.compile(image);
    paint::Image identity, moved, area;
    paint::render_affine(field, {}, 5, 5, identity);
    paint::render_affine(field, {1, 0, 0.000001, 0, 1, 0}, 5, 5, moved);
    require(identity.get(2, 2).r == 40 && moved.get(2, 2).r == 40,
            "infinitesimal translation changed the default sampling functional");
    paint::render_affine(field, {}, 5, 5, area, paint::WarpSampling::Area);
    near_color(area.get(2, 2), canon_reference(field, 1.5, 1.5),
               "explicit area mode is not the canon at identity");
    paint::render_affine(field, {1, 0, 0.000001, 0, 1, 0}, 5, 5, moved, paint::WarpSampling::Area);
    require(moved.get(2, 2).r == area.get(2, 2).r, "explicit area functional jumps near identity");
    // Equivalent affine and mesh maps. render_affine is stxlib::conv's
    // composited transfer, render_mesh the 2D field; the two agree where pass
    // 1 reads every row at the same u (beta = a12 / a22 = 0: translations and
    // quarter turns), since this fixture's material rows are identical, so
    // each column pass 2 compiles is one profile scaled. The mesh's grid
    // carries source positions rounded once more than the affine map's, so a
    // byte may round the other way.
    constexpr paint::AffineMap maps[3] = {{1, 0, 1, 0, 1, 0}, {0, -1, 4, 1, 0, 0}, {1, 0, 0.000001, 0, 1, 0}};
    for (const paint::AffineMap& map : maps) {
        paint::ReshapeMesh mesh = paint::make_reshape_mesh(image, 2.0);
        for (paint::MeshNode& node : mesh.nodes) {
            node.target = {map.xx * node.source.x + map.xy * node.source.y + map.tx,
                           map.yx * node.source.x + map.yy * node.source.y + map.ty};
        }
        for (paint::WarpSampling sampling :
             {paint::WarpSampling::Point, paint::WarpSampling::Minification, paint::WarpSampling::Area}) {
            paint::Image affine_output, mesh_output;
            paint::render_affine(field, map, 7, 7, affine_output, sampling);
            paint::render_mesh(field, mesh, 7, 7, mesh_output, sampling);
            for (std::size_t i = 0; i < affine_output.pixels.size(); ++i) {
                near_color(affine_output.pixels[i], mesh_output.pixels[i],
                           "equivalent affine and mesh maps produced different pixels");
            }
        }
    }
    // A shear with scale reads each row at its own u, so the two transfers
    // are different readings of the field near admission (material edges):
    // each is checked against its own reference, every pixel, including the
    // first row and column, whose squares the mesh grid must cover whole.
    {
        const paint::AffineMap map{0.8, 0.12, 0.4, 0, 0.9, 0.3};
        const stxlib::conv::AffineMap pull = ground_pull(map);
        paint::ReshapeMesh mesh = paint::make_reshape_mesh(image, 2.0);
        for (paint::MeshNode& node : mesh.nodes) {
            node.target = {map.xx * node.source.x + map.xy * node.source.y + map.tx,
                           map.yx * node.source.x + map.yy * node.source.y + map.ty};
        }
        // The map minifies, so Minification is the canon.
        for (paint::WarpSampling sampling :
             {paint::WarpSampling::Point, paint::WarpSampling::Minification, paint::WarpSampling::Area}) {
            const bool canon = sampling != paint::WarpSampling::Point;
            paint::Image affine_output, mesh_output;
            paint::render_affine(field, map, 7, 7, affine_output, sampling);
            paint::render_mesh(field, mesh, 7, 7, mesh_output, sampling);
            for (int y = 0; y < 7; ++y) {
                for (int x = 0; x < 7; ++x) {
                    near_color(affine_output.get(x, y), composited_reference(field, pull, x, y, canon),
                               "a sheared affine render is not the composited transfer");
                    near_color(mesh_output.get(x, y), field_reference(field, pull, x, y, canon),
                               "a sheared mesh render is not the 2D field");
                }
            }
        }
    }
    // Minification switches from the preview to the canon where the map
    // starts to minify (a footprint of one pixel); past it the canon is
    // continuous in the scale.
    for (double stretch : {1.25, 3.0}) {
        double before = 1.0 / (stretch - 0.0000001);
        double after = 1.0 / (stretch + 0.0000001);
        paint::render_affine(field, {before, 0, 2 - 2 * before, 0, before, 2 - 2 * before}, 5, 5, identity);
        paint::render_affine(field, {after, 0, 2 - 2 * after, 0, after, 2 - 2 * after}, 5, 5, moved);
        require(paint::equal(identity.get(2, 2), moved.get(2, 2)), "filter switched at a scale threshold");
    }
}
struct ConcurrentAffineProbe {
    const paint::ConvWarpField& field;
    paint::Image result;
    paint::WarpSampling sampling;
    static void run(ConcurrentAffineProbe& probe) {
        for (int i = 0; i < 4; ++i) {
            paint::render_affine(probe.field, {}, 256, 256, probe.result, probe.sampling);
        }
    }
};
void test_reused_affine_workers() {
    paint::Image first, second;
    // Nine pixels a side: the canon's footprint at the centre reads only
    // the material, not the transparent ground around it.
    first.reset(9, 9, {200, 30, 40, 255});
    second.reset(9, 9, {20, 90, 220, 255});
    paint::ConvWarpField a, b;
    a.compile(first);
    b.compile(second);
    std::array<ConcurrentAffineProbe, 3> probes{{{a, {}, paint::WarpSampling::Point},
                                                 {b, {}, paint::WarpSampling::Point},
                                                 {a, {}, paint::WarpSampling::Area}}};
    std::vector<std::jthread> callers;
    for (ConcurrentAffineProbe& probe : probes) {
        callers.emplace_back(ConcurrentAffineProbe::run, std::ref(probe));
    }
    for (std::jthread& caller : callers) {
        caller.join();
    }
    require(paint::equal(probes[0].result.get(4, 4), first.get(4, 4)) &&
                paint::equal(probes[1].result.get(4, 4), second.get(4, 4)) &&
                paint::equal(probes[2].result.get(4, 4), first.get(4, 4)),
            "reused point and area workers retain distinct concurrent jobs");
    require(probes[0].result.get(20, 20).a == 0 && probes[1].result.get(20, 20).a == 0,
            "reused workers clear pixels outside each source");
}
void benchmark(int side) {
    paint::Image image;
    image.reset(side, side);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            image.set(x, y,
                      {static_cast<std::uint8_t>((x * 13 + y * 7) % 256),
                       static_cast<std::uint8_t>((x + y) % 256),
                       static_cast<std::uint8_t>((x / 11 + y / 17) % 2 * 255), 255});
        }
    }
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    paint::ConvWarpField field;
    field.compile(image);
    std::chrono::steady_clock::time_point compiled = std::chrono::steady_clock::now();
    paint::Image output;
    paint::AffineMap map{0.98, -0.12, side * 0.06, 0.12, 0.98, -side * 0.05};
    paint::render_affine(field, map, side, side, output, paint::WarpSampling::Point);
    std::chrono::steady_clock::time_point rendered = std::chrono::steady_clock::now();
    std::cout << side << "x" << side << " compile " << std::chrono::duration<double>(compiled - start).count()
              << " s; point render " << std::chrono::duration<double>(rendered - compiled).count()
              << " s; retained " << field.storage_bytes() << " bytes\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        test_constant_and_affine();
        test_identity_and_alpha();
        test_mesh();
        test_surface_continuity();
        test_admitted_edge_and_mesh_area();
        test_physical_alpha_before_filtering();
        test_sampling_continuity_and_equivalent_maps();
        test_reused_affine_workers();
        std::cout << "CONV warp constants, surface continuity, stxlib::conv canon, affine fields, alpha, tiny "
                     "images, mesh identity, folds and lasso tests passed.\n";
        if (argc > 1) {
            benchmark(std::stoi(argv[1]));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
