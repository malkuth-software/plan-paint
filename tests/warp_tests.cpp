#include "warp.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
void near(double a, double b, double tolerance, const char* message) {
    require(std::abs(a - b) <= tolerance, message);
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
    paint::Image output;
    paint::render_mesh(field, mesh, 80, 60, output);
    for (paint::Color pixel : output.pixels) {
        require(paint::equal(pixel, {3, 90, 230, 255}), "mesh identity has cracks");
    }
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
void test_canonical_reference() {
    // Independent Python finite_joint_control_nets/evaluate_joint_atlas receipt,
    // 7x8 mixed affine/wrapped/curved-edge input. Reference proposal stores FP32;
    // native keeps doubles, so this comparison admits that storage discrepancy.
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
    double positions[6][2] = {{.13, .31}, {1.7, 2.1}, {2.3, 3.7}, {4.14, 1.16}, {5.9, 4.1}, {6.7, 5.6}};
    double expected[6][3] = {{.036470588235294116, .07003921568627453, 0},
                             {.3466666666666666, .5096967046420239, 1},
                             {.5262138414719475, .9393932445801931, 1},
                             {.580632620635306, .4187344480119232, .9196900484410252},
                             {.9499470468430964, .08457244260906291, .07392009691925135},
                             {.10271603622105065, .44901960784313716, 0}};
    for (int i = 0; i < 6; ++i) {
        paint::WarpSample sample = field.sample_components({positions[i][0], positions[i][1]});
        near(sample.r, expected[i][0], 2.0e-5, "canonical red reference mismatch");
        near(sample.g, expected[i][1], 2.0e-5, "canonical green reference mismatch");
        near(sample.b, expected[i][2], 2.0e-5, "canonical blue reference mismatch");
    }
    for (int y = 0; y < 6; ++y) {
        for (int x = 1; x < 7; ++x) {
            paint::WarpSample left = field.sample_components({x - 1.0e-9, y + 0.37});
            paint::WarpSample right = field.sample_components({x + 1.0e-9, y + 0.37});
            near(left.r, right.r, 1.0e-7, "shared atlas edge discontinuity");
        }
    }
    for (double y = 0; y <= 6; y += .19) {
        for (double x = 0; x <= 7; x += .17) {
            paint::WarpSample sample = field.sample_components({x, y});
            require(sample.r >= -1e-12 && sample.r <= 1 + 1e-12 && sample.g >= -1e-12 &&
                        sample.g <= 1 + 1e-12 && sample.b >= -1e-12 && sample.b <= 1 + 1e-12,
                    "atlas range violation");
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
    // On this mixed-opacity edge, late clipping would return red 252. Projecting
    // before integration agrees with the independent dense integral, red 248.
    paint::Image edge;
    paint::render_affine(field, {1, 0, 0.2, 0, 1, 0}, 5, 5, edge, paint::WarpSampling::Area);
    require(edge.get(2, 2).r == 248, "excess color leaked between quadrature samples before clipping");
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
    // Independent dense midpoint integration of the physical field. Unlike a
    // mirrored Gauss-rule test, this catches projection after, rather than before,
    // averaging and allows only the bounded filter's numerical integration error.
    constexpr int divisions = 256;
    double sums[4] = {};
    for (int y = 0; y < divisions; ++y) {
        for (int x = 0; x < divisions; ++x) {
            paint::Point position{1.2 + (x + 0.5) / divisions, 1.1 + (y + 0.5) / divisions};
            physical = field.sample_premultiplied(position);
            sums[0] += physical.r;
            sums[1] += physical.g;
            sums[2] += physical.b;
            sums[3] += physical.a;
        }
    }
    paint::Image output;
    paint::render_affine(field, {1, 0, 0.3, 0, 1, 0.4}, 5, 5, output, paint::WarpSampling::Area);
    paint::Color actual = output.get(2, 2);
    near(actual.r, std::round(255.0 * sums[0] / sums[3]), 1.0, "filtered physical red differs from integral");
    near(actual.g, std::round(255.0 * sums[1] / sums[3]), 1.0,
         "filtered physical green differs from integral");
    near(actual.b, std::round(255.0 * sums[2] / sums[3]), 1.0,
         "filtered physical blue differs from integral");
    near(actual.a, std::round(255.0 * sums[3] / (divisions * divisions)), 1.0,
         "filtered alpha differs from integral");
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
    require(area.get(2, 2).r == 41, "explicit area mode silently became a point sample at identity");
    paint::render_affine(field, {1, 0, 0.000001, 0, 1, 0}, 5, 5, moved, paint::WarpSampling::Area);
    require(moved.get(2, 2).r == area.get(2, 2).r, "explicit area functional jumps near identity");
    constexpr paint::AffineMap maps[4] = {
        {1, 0, 1, 0, 1, 0}, {0.8, 0.12, 0.4, 0, 0.9, 0.3}, {0, -1, 4, 1, 0, 0}, {1, 0, 0.000001, 0, 1, 0}};
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
                require(paint::equal(affine_output.pixels[i], mesh_output.pixels[i]),
                        "equivalent affine and mesh maps produced different pixels");
            }
        }
    }
    // Former 2/4/8-rule thresholds no longer introduce finite switches.
    for (double stretch : {1.0, 1.25, 3.0}) {
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
    first.reset(5, 5, {200, 30, 40, 255});
    second.reset(5, 5, {20, 90, 220, 255});
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
    require(paint::equal(probes[0].result.get(2, 2), first.get(2, 2)) &&
                paint::equal(probes[1].result.get(2, 2), second.get(2, 2)) &&
                paint::equal(probes[2].result.get(2, 2), first.get(2, 2)),
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
        test_canonical_reference();
        test_admitted_edge_and_mesh_area();
        test_physical_alpha_before_filtering();
        test_sampling_continuity_and_equivalent_maps();
        test_reused_affine_workers();
        std::cout << "CONV warp constants, affine fields, cardinality, alpha, tiny images, mesh identity, "
                     "folds and lasso tests passed.\n";
        if (argc > 1) {
            benchmark(std::stoi(argv[1]));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
