// PolyForm Noncommercial License 1.0.0 (LICENSE). Copyright (c) 2026 joshuah.rainstar@gmail.com.
// Native adaptation by Astra, sponsored by Joshuah. Thanks to Hashem.
#include "warp.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// The transform tools read the material through stxlib::conv (STXlib), the
// one canonical CONV (owner, 2026-11-22): point samples are the field's
// value (stxlib::conv::Field::evaluate, stx_conv's Surface bit for bit), and
// renders are its transfers, preview (the field at each destination pixel's
// pulled-back centre) or canon (its average over the pulled-back footprint),
// through an affine map or a mesh map. The material is transferred from a
// copy on a transparent margin, since a transfer clamps positions to its
// field: outside the material the ground is transparent, and the material's
// edge falls to it across one pixel.
namespace paint {
namespace {
// Image pixels are four bytes, r g b a, rows contiguous: the field's RGBA8
// layout, read and written through unsigned char.
static_assert(sizeof(Color) == stxlib::conv::channels && alignof(Color) == 1);
static_assert(std::is_standard_layout_v<Color> && std::is_trivially_copyable_v<Color>);
// The transparent margin around the material in the transfers' field: two
// pixels, so the ground's outer nodes, where a clamped position lands, are
// transparent and every cell past the first margin pixel reads it.
constexpr int ground_margin = 2;

const stxlib::u8* pixel_bytes(const Image& image) {
    const stxlib::u8* bytes = reinterpret_cast<const stxlib::u8*>(image.pixels.data());
    return bytes;
}
stxlib::u8* pixel_bytes(Image& image) {
    stxlib::u8* bytes = reinterpret_cast<stxlib::u8*>(image.pixels.data());
    return bytes;
}
// A field compiled from `image` (at least 2 x 2); throws on refusal.
stxlib::conv::Field compiled_field(const Image& image) {
    const std::size_t width = static_cast<std::size_t>(image.width);
    const std::size_t height = static_cast<std::size_t>(image.height);
    stxlib::OperationResult<stxlib::conv::Field> made =
        stxlib::conv::Field::make(width, height, stxlib::conv::FieldOptions{});
    if (made.failed()) {
        throw std::runtime_error("CONV could not allocate the material's field.");
    }
    stxlib::conv::Field field = std::move(made).take_value();
    const stxlib::conv::Rgba8Image source{stxlib::View<stxlib::u8>(pixel_bytes(image), image.pixels.size() * 4),
                                          width, height, width * 4};
    if (field.compile(source).failed()) {
        throw std::runtime_error("CONV rejected the material.");
    }
    return field;
}
// `source` copied into `ground` with its top left at (left, top).
void copy_into(Image& ground, const Image& source, int left, int top) {
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            ground.set(left + x, top + y, source.get(x, y));
        }
    }
}
void check_cancelled(const std::atomic<bool>* cancelled, const char* message) {
    if (cancelled && (*cancelled).load(std::memory_order_relaxed)) {
        throw std::runtime_error(message);
    }
}
} // namespace
void ConvWarpField::compile(const Image& source, const std::atomic<bool>* cancelled) {
    if (source.width < 1 || source.height < 1 || source.width > 32768 - 2 * ground_margin ||
        source.height > 32768 - 2 * ground_margin ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height) {
        throw std::invalid_argument("Invalid CONV source image.");
    }
    check_cancelled(cancelled, "CONV preparation superseded.");
    const Color first = source.pixels.front();
    bool constant = true;
    for (const Color color : source.pixels) {
        if (!equal(color, first)) {
            constant = false;
            break;
        }
    }
    // The ground: the material on a transparent margin.
    Image ground;
    ground.reset(source.width + 2 * ground_margin, source.height + 2 * ground_margin, {0, 0, 0, 0});
    copy_into(ground, source, ground_margin, ground_margin);
    stxlib::conv::Field ground_field = compiled_field(ground);
    check_cancelled(cancelled, "CONV preparation superseded.");
    // The material's own field, for point samples. A one-pixel axis is
    // repeated: the field across it is then constant and along the other
    // axis it is that line's CONV curve. A constant material needs none.
    stxlib::conv::Field material_field;
    if (!constant) {
        Image repeated;
        repeated.reset(std::max(2, source.width), std::max(2, source.height), {0, 0, 0, 0});
        for (int y = 0; y < repeated.height; ++y) {
            for (int x = 0; x < repeated.width; ++x) {
                repeated.set(x, y, source.get(std::min(x, source.width - 1), std::min(y, source.height - 1)));
            }
        }
        material_field = compiled_field(repeated);
    }
    check_cancelled(cancelled, "CONV preparation superseded.");
    const double alpha = first.a / 255.0;
    constant_value_ = {first.r / 255.0 * alpha, first.g / 255.0 * alpha, first.b / 255.0 * alpha, alpha};
    constant_ = constant;
    material_ = std::move(material_field);
    ground_ = std::move(ground_field);
    width_ = source.width;
    height_ = source.height;
}
std::size_t ConvWarpField::storage_bytes() const {
    const std::size_t bytes = material_.storage_bytes() + ground_.storage_bytes();
    return bytes;
}
WarpSample ConvWarpField::sample_components(Point position) const {
    WarpSample sample;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || position.x < -0.5 || position.y < -0.5 ||
        position.x >= width_ - 0.5 || position.y >= height_ - 0.5 || !ground_.ready()) {
        return sample;
    }
    if (constant_) {
        return constant_value_;
    }
    // On a one-pixel axis every position is that pixel's centre.
    const Point at{width_ == 1 ? 0.0 : position.x, height_ == 1 ? 0.0 : position.y};
    double values[4] = {};
    if (material_.evaluate(at.x, at.y, stxlib::MutableView<double>(values)).failed()) {
        return sample;
    }
    sample = {values[0], values[1], values[2], values[3]};
    return sample;
}
WarpSample ConvWarpField::sample_premultiplied(Point position) const {
    WarpSample value = sample_components(position);
    value.a = std::clamp(value.a, 0.0, 1.0);
    value.r = std::clamp(value.r, 0.0, value.a);
    value.g = std::clamp(value.g, 0.0, value.a);
    value.b = std::clamp(value.b, 0.0, value.a);
    return value;
}
namespace {
static Color encoded_sample(WarpSample value) {
    double alpha = std::clamp(value.a, 0.0, 1.0);
    if (alpha < 0.5 / 255.0) {
        return {0, 0, 0, 0};
    }
    Color result{static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.r / alpha, 0.0, 1.0))),
                 static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.g / alpha, 0.0, 1.0))),
                 static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.b / alpha, 0.0, 1.0))),
                 static_cast<std::uint8_t>(std::round(255.0 * alpha))};
    return result;
}
} // namespace
Color ConvWarpField::sample(Point position) const {
    WarpSample value = sample_premultiplied(position);
    Color color = encoded_sample(value);
    return color;
}

namespace {
static Point transformed(Point point, const AffineMap& map) {
    Point result{map.xx * point.x + map.xy * point.y + map.tx, map.yx * point.x + map.yy * point.y + map.ty};
    return result;
}
static AffineMap inverse_map(const AffineMap& map) {
    double determinant = map.xx * map.yy - map.xy * map.yx;
    double scale = std::max({std::abs(map.xx), std::abs(map.xy), std::abs(map.yx), std::abs(map.yy)});
    if (!std::isfinite(determinant) || !std::isfinite(map.tx) || !std::isfinite(map.ty) || scale == 0.0 ||
        std::abs(determinant) <= 1.0e-12 * scale * scale) {
        throw std::invalid_argument("Warp map is singular or not finite.");
    }
    AffineMap inverse{map.yy / determinant,  -map.xy / determinant, 0.0,
                      -map.yx / determinant, map.xx / determinant,  0.0};
    inverse.tx = -inverse.xx * map.tx - inverse.xy * map.ty;
    inverse.ty = -inverse.yx * map.tx - inverse.yy * map.ty;
    return inverse;
}
static double map_footprint(const AffineMap& inverse) {
    // Largest singular value, including compression along a sheared direction.
    // Normalize before squaring to avoid overflow for otherwise valid maps.
    double scale =
        std::max({std::abs(inverse.xx), std::abs(inverse.xy), std::abs(inverse.yx), std::abs(inverse.yy)});
    double xx = inverse.xx / scale, xy = inverse.xy / scale;
    double yx = inverse.yx / scale, yy = inverse.yy / scale;
    double a = xx * xx + yx * yx;
    double b = xx * xy + yx * yy;
    double d = xy * xy + yy * yy;
    double eigenvalue = 0.5 * (a + d + std::hypot(a - d, 2.0 * b));
    double footprint = scale * std::sqrt(eigenvalue);
    return footprint;
}
// The transfer mode of a sampling: Point is the preview, Area the canon, and
// Minification the canon where the map minifies (a source footprint wider
// than a pixel) and the preview elsewhere, so identity, translations and
// quarter turns keep their samples.
static stxlib::conv::Mode transfer_mode(WarpSampling sampling, double footprint) {
    if (sampling == WarpSampling::Area) {
        return stxlib::conv::Mode::canon;
    }
    if (sampling == WarpSampling::Minification && footprint > 1.0) {
        return stxlib::conv::Mode::canon;
    }
    return stxlib::conv::Mode::preview;
}
// Destination rows go out in chunks of this many, claimed by up to eight
// workers, each with its own workspace; cancellation is checked between
// chunks.
constexpr std::size_t rows_per_chunk = 32;
template <typename Map>
struct TransferJob {
    const stxlib::conv::Field& field;
    const Map& map;
    stxlib::conv::Mode mode = stxlib::conv::Mode::preview;
    stxlib::conv::MutableRgba8Image destination;
    const std::atomic<bool>* cancelled = nullptr;
    std::atomic<std::size_t> next_chunk{0};
    std::atomic<bool> failed{false};
};
template <typename Map>
struct TransferWorker {
    TransferJob<Map>* job = nullptr;
    stxlib::conv::Workspace workspace;
    static void run(TransferWorker& worker) {
        TransferJob<Map>& job = *worker.job;
        const std::size_t height = job.destination.height;
        for (;;) {
            if (job.failed.load(std::memory_order_relaxed)) {
                return;
            }
            if (job.cancelled && (*job.cancelled).load(std::memory_order_relaxed)) {
                return;
            }
            const std::size_t chunk = job.next_chunk.fetch_add(1, std::memory_order_relaxed);
            const std::size_t first = chunk * rows_per_chunk;
            if (first >= height) {
                return;
            }
            const stxlib::conv::Region region{0, first, job.destination.width,
                                              std::min(height, first + rows_per_chunk)};
            const stxlib::OperationStatus status =
                stxlib::conv::transfer(job.field, job.map, job.mode, job.destination, region, worker.workspace);
            if (status.failed()) {
                job.failed.store(true, std::memory_order_relaxed);
                return;
            }
        }
    }
};
// `field` through `map` by `mode` into `output` (its size already set).
// Throws on a refusal or on cancellation; `output` is then unspecified.
template <typename Map>
void transfer_into(const stxlib::conv::Field& field, const Map& map, stxlib::conv::Mode mode, Image& output,
                   const std::atomic<bool>* cancelled) {
    const std::size_t width = static_cast<std::size_t>(output.width);
    const std::size_t height = static_cast<std::size_t>(output.height);
    TransferJob<Map> job{field, map, mode,
                         stxlib::conv::MutableRgba8Image{
                             stxlib::MutableView<stxlib::u8>(pixel_bytes(output), output.pixels.size() * 4), width,
                             height, width * 4}};
    job.cancelled = cancelled;
    const std::size_t chunks = (height + rows_per_chunk - 1) / rows_per_chunk;
    std::size_t count = std::min<std::size_t>(8, std::max(1u, std::thread::hardware_concurrency()));
    if (width * height < 65536) {
        count = 1;
    }
    count = std::max<std::size_t>(1, std::min(count, chunks));
    std::vector<TransferWorker<Map>> workers(count);
    for (TransferWorker<Map>& worker : workers) {
        stxlib::OperationResult<stxlib::conv::Workspace> workspace = stxlib::conv::Workspace::make();
        if (workspace.failed()) {
            throw std::runtime_error("CONV could not allocate a workspace.");
        }
        worker.job = &job;
        worker.workspace = std::move(workspace).take_value();
    }
    if (count == 1) {
        TransferWorker<Map>::run(workers[0]);
    } else {
        std::vector<std::jthread> threads;
        threads.reserve(count);
        for (TransferWorker<Map>& worker : workers) {
            threads.emplace_back(TransferWorker<Map>::run, std::ref(worker));
        }
        for (std::jthread& thread : threads) {
            thread.join();
        }
    }
    check_cancelled(cancelled, "CONV rendering cancelled.");
    if (job.failed.load(std::memory_order_relaxed)) {
        throw std::runtime_error("CONV rejected the transfer.");
    }
}
static double orient(Point a, Point b, Point c) {
    double result = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    return result;
}
static bool within_triangle(Point point, Point a, Point b, Point c, double tolerance) {
    return orient(a, b, point) >= -tolerance && orient(b, c, point) >= -tolerance &&
           orient(c, a, point) >= -tolerance;
}
static bool edges_cross(Point a, Point b, Point c, Point d) {
    double ab_c = orient(a, b, c), ab_d = orient(a, b, d), cd_a = orient(c, d, a), cd_b = orient(c, d, b);
    // Closed segments: non-neighbouring touching edges are invalid too.
    if (((ab_c > 0.0 && ab_d < 0.0) || (ab_c < 0.0 && ab_d > 0.0)) &&
        ((cd_a > 0.0 && cd_b < 0.0) || (cd_a < 0.0 && cd_b > 0.0))) {
        return true;
    }
    if (std::abs(ab_c) < 1.0e-9 && c.x >= std::min(a.x, b.x) && c.x <= std::max(a.x, b.x) &&
        c.y >= std::min(a.y, b.y) && c.y <= std::max(a.y, b.y)) {
        return true;
    }
    if (std::abs(ab_d) < 1.0e-9 && d.x >= std::min(a.x, b.x) && d.x <= std::max(a.x, b.x) &&
        d.y >= std::min(a.y, b.y) && d.y <= std::max(a.y, b.y)) {
        return true;
    }
    if (std::abs(cd_a) < 1.0e-9 && a.x >= std::min(c.x, d.x) && a.x <= std::max(c.x, d.x) &&
        a.y >= std::min(c.y, d.y) && a.y <= std::max(c.y, d.y)) {
        return true;
    }
    if (std::abs(cd_b) < 1.0e-9 && b.x >= std::min(c.x, d.x) && b.x <= std::max(c.x, d.x) &&
        b.y >= std::min(c.y, d.y) && b.y <= std::max(c.y, d.y)) {
        return true;
    }
    return false;
}
static bool finite_point(Point point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::abs(point.x) <= 1.0e7 &&
           std::abs(point.y) <= 1.0e7;
}
} // namespace
Rect affine_bounds(const ConvWarpField& field, const AffineMap& map) {
    AffineMap inverse = inverse_map(map);
    (void)inverse;
    if (field.width() < 1 || field.height() < 1) {
        throw std::invalid_argument("Compile the CONV material first.");
    }
    Point corners[4] = {{-0.5, -0.5},
                        {field.width() - 0.5, -0.5},
                        {field.width() - 0.5, field.height() - 0.5},
                        {-0.5, field.height() - 0.5}};
    double left = std::numeric_limits<double>::infinity(), top = left, right = -left, bottom = -left;
    for (Point corner : corners) {
        Point point = transformed(corner, map);
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    if (left < -1.0e8 || top < -1.0e8 || right > 1.0e8 || bottom > 1.0e8) {
        throw std::length_error("Transformed bounds exceed the coordinate limit.");
    }
    int x = static_cast<int>(std::floor(left + 0.5 + 1.0e-10)),
        y = static_cast<int>(std::floor(top + 0.5 + 1.0e-10));
    Rect result{x, y, static_cast<int>(std::ceil(right + 0.5 - 1.0e-10)) - x,
                static_cast<int>(std::ceil(bottom + 0.5 - 1.0e-10)) - y};
    return result;
}
void render_affine(const ConvWarpField& field, const AffineMap& map, int width, int height,
                   Image& destination, WarpSampling sampling, const std::atomic<bool>* cancelled) {
    check_cancelled(cancelled, "CONV rendering cancelled.");
    if (field.width() < 1) {
        throw std::invalid_argument("Compile the CONV material first.");
    }
    AffineMap inverse = inverse_map(map);
    Image output;
    output.reset(width, height, {0, 0, 0, 0});
    // Destination pixel centre (x, y) reads the ground's node coordinates:
    // the source position plus the margin.
    stxlib::conv::AffineMap pull{inverse.xx, inverse.xy, inverse.yx, inverse.yy, inverse.tx + ground_margin,
                                 inverse.ty + ground_margin};
    transfer_into(field.ground(), pull, transfer_mode(sampling, map_footprint(inverse)), output, cancelled);
    destination = std::move(output);
}
bool reshape_mesh_valid(const ReshapeMesh& mesh) {
    if (mesh.nodes.size() < 3 || mesh.triangles.empty()) {
        return false;
    }
    std::size_t boundary_count = 0;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (!finite_point(mesh.nodes[i].source) || !finite_point(mesh.nodes[i].target)) {
            return false;
        }
        if (mesh.nodes[i].boundary) {
            ++boundary_count;
        }
    }
    for (const MeshTriangle& triangle : mesh.triangles) {
        for (std::size_t index : triangle.nodes) {
            if (index >= mesh.nodes.size()) {
                return false;
            }
        }
        const MeshNode& a = mesh.nodes[triangle.nodes[0]];
        const MeshNode& b = mesh.nodes[triangle.nodes[1]];
        const MeshNode& c = mesh.nodes[triangle.nodes[2]];
        double source_area = orient(a.source, b.source, c.source);
        double target_area = orient(a.target, b.target, c.target);
        if (source_area <= 1.0e-10 || target_area <= std::max(1.0e-9, source_area * 1.0e-5)) {
            return false;
        }
    }
    if (boundary_count < 3) {
        return false;
    }
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (!mesh.nodes[i].boundary) {
            continue;
        }
        std::size_t next = (i + 1) % mesh.nodes.size();
        while (!mesh.nodes[next].boundary) {
            next = (next + 1) % mesh.nodes.size();
        }
        for (std::size_t j = i + 1; j < mesh.nodes.size(); ++j) {
            if (!mesh.nodes[j].boundary) {
                continue;
            }
            std::size_t after = (j + 1) % mesh.nodes.size();
            while (!mesh.nodes[after].boundary) {
                after = (after + 1) % mesh.nodes.size();
            }
            if (next == j || after == i) {
                continue;
            }
            if (edges_cross(mesh.nodes[i].target, mesh.nodes[next].target, mesh.nodes[j].target,
                            mesh.nodes[after].target)) {
                return false;
            }
        }
    }
    return true;
}
bool move_reshape_node(ReshapeMesh& mesh, std::size_t node, Point target) {
    if (node >= mesh.nodes.size() || !finite_point(target)) {
        return false;
    }
    Point previous = mesh.nodes[node].target;
    mesh.nodes[node].target = target;
    if (reshape_mesh_valid(mesh)) {
        return true;
    }
    mesh.nodes[node].target = previous;
    return false;
}
namespace {
static double incircle(Point a, Point b, Point c, Point d) {
    double ax = a.x - d.x, ay = a.y - d.y, bx = b.x - d.x, by = b.y - d.y, cx = c.x - d.x, cy = c.y - d.y;
    double result = (ax * ax + ay * ay) * (bx * cy - by * cx) - (bx * bx + by * by) * (ax * cy - ay * cx) +
                    (cx * cx + cy * cy) * (ax * by - ay * bx);
    return result;
}
static void improve_mesh_angles(ReshapeMesh& mesh) {
    // Lawson flips improve the ear triangulation without moving the outline.
    // A bounded sweep count affects only triangle quality, never validity.
    for (int pass = 0; pass < 32; ++pass) {
        bool changed = false;
        for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
            for (std::size_t j = i + 1; j < mesh.triangles.size(); ++j) {
                MeshTriangle first = mesh.triangles[i], second = mesh.triangles[j];
                std::size_t shared[2] = {}, count = 0, c = 0, d = 0;
                for (std::size_t vertex : first.nodes) {
                    bool found = false;
                    for (std::size_t other : second.nodes) {
                        if (vertex == other) {
                            found = true;
                        }
                    }
                    if (found && count < 2) {
                        shared[count++] = vertex;
                    } else {
                        c = vertex;
                    }
                }
                if (count != 2) {
                    continue;
                }
                for (std::size_t vertex : second.nodes) {
                    if (vertex != shared[0] && vertex != shared[1]) {
                        d = vertex;
                    }
                }
                std::size_t a = shared[0], b = shared[1];
                Point pa = mesh.nodes[a].source, pb = mesh.nodes[b].source, pc = mesh.nodes[c].source,
                      pd = mesh.nodes[d].source;
                if (orient(pa, pb, pc) < 0.0) {
                    std::swap(a, b);
                    std::swap(pa, pb);
                }
                if (orient(pc, pd, pa) * orient(pc, pd, pb) >= -1.0e-10) {
                    continue;
                }
                double scale =
                    std::max({std::hypot(pa.x - pd.x, pa.y - pd.y), std::hypot(pb.x - pd.x, pb.y - pd.y),
                              std::hypot(pc.x - pd.x, pc.y - pd.y), 1.0});
                if (incircle(pa, pb, pc, pd) <= 1.0e-12 * scale * scale * scale * scale) {
                    continue;
                }
                MeshTriangle left{{c, d, a}}, right{{d, c, b}};
                if (orient(pc, pd, pa) < 0.0) {
                    left = {{d, c, a}};
                    right = {{c, d, b}};
                }
                mesh.triangles[i] = left;
                mesh.triangles[j] = right;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
}
} // namespace

ReshapeMesh make_reshape_mesh(const std::vector<Point>& outline, double spacing) {
    if (outline.size() < 3 || outline.size() > 4096 || !std::isfinite(spacing) || spacing < 1.0) {
        throw std::invalid_argument("A reshape lasso needs 3 to 4096 points and spacing at least 1 pixel.");
    }
    std::vector<Point> points;
    for (Point point : outline) {
        if (!finite_point(point) || std::abs(point.x) > 1.0e7 || std::abs(point.y) > 1.0e7) {
            throw std::invalid_argument("Invalid reshape point.");
        }
        if (!points.empty() && std::hypot(point.x - points.back().x, point.y - points.back().y) < 1.0e-7) {
            continue;
        }
        points.push_back(point);
    }
    if (points.size() > 1 &&
        std::hypot(points.front().x - points.back().x, points.front().y - points.back().y) < 1.0e-7) {
        points.pop_back();
    }
    // Remove collinear lasso samples without changing the polygon boundary.
    bool changed = true;
    while (changed && points.size() > 3) {
        changed = false;
        for (std::size_t i = 0; i < points.size(); ++i) {
            Point a = points[(i + points.size() - 1) % points.size()], b = points[i],
                  c = points[(i + 1) % points.size()];
            if (std::abs(orient(a, b, c)) < 1.0e-8 &&
                (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y) >= 0.0) {
                points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    double area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        Point a = points[i], b = points[(i + 1) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    if (std::abs(area) < 1.0e-8 || points.size() < 3) {
        throw std::invalid_argument("The reshape lasso has no area.");
    }
    if (area < 0.0) {
        std::reverse(points.begin(), points.end());
    }
    ReshapeMesh mesh;
    // Start with original boundary vertices: their cyclic node order remains
    // the boundary invariant used by the global injectivity check.
    for (std::size_t i = 0; i < points.size(); ++i) {
        Point a = points[i], b = points[(i + 1) % points.size()];
        double segments = std::ceil(std::hypot(b.x - a.x, b.y - a.y) / spacing);
        if (segments > 2048.0 || mesh.nodes.size() + static_cast<std::size_t>(segments) > 2048) {
            throw std::length_error("Use a coarser reshape spacing; mesh limit is 2048 nodes.");
        }
        int count = std::max(1, static_cast<int>(segments));
        for (int j = 0; j < count; ++j) {
            double t = static_cast<double>(j) / count;
            Point point{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
            mesh.nodes.push_back({point, point, true});
        }
    }
    // Validate polygon simplicity before ear clipping.
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        for (std::size_t j = i + 1; j < mesh.nodes.size(); ++j) {
            std::size_t ni = (i + 1) % mesh.nodes.size(), nj = (j + 1) % mesh.nodes.size();
            if (ni == j || nj == i) {
                continue;
            }
            if (edges_cross(mesh.nodes[i].source, mesh.nodes[ni].source, mesh.nodes[j].source,
                            mesh.nodes[nj].source)) {
                throw std::invalid_argument("The reshape outline crosses itself.");
            }
        }
    }
    std::vector<std::size_t> ring;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        ring.push_back(i);
    }
    while (ring.size() > 3) {
        bool found = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            std::size_t a = ring[(i + ring.size() - 1) % ring.size()], b = ring[i],
                        c = ring[(i + 1) % ring.size()];
            Point pa = mesh.nodes[a].source, pb = mesh.nodes[b].source, pc = mesh.nodes[c].source;
            if (orient(pa, pb, pc) <= 1.0e-10) {
                continue;
            }
            bool occupied = false;
            for (std::size_t node : ring) {
                if (node == a || node == b || node == c) {
                    continue;
                }
                if (within_triangle(mesh.nodes[node].source, pa, pb, pc, 1.0e-10)) {
                    occupied = true;
                    break;
                }
            }
            if (occupied) {
                continue;
            }
            mesh.triangles.push_back({{a, b, c}});
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
            found = true;
            break;
        }
        if (!found) {
            throw std::invalid_argument("Could not triangulate the reshape outline.");
        }
    }
    mesh.triangles.push_back({{ring[0], ring[1], ring[2]}});
    // Interior lattice insertion. Strict interior tests avoid splitting an
    // existing edge on only one side (which would create a T junction).
    double left = points[0].x, right = left, top = points[0].y, bottom = top;
    for (Point point : points) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    double columns = std::ceil((right - left) / spacing), rows = std::ceil((bottom - top) / spacing);
    if (columns * rows > 16384.0) {
        throw std::length_error("Use coarser reshape spacing for this region.");
    }
    for (double y = top + spacing * 0.5; y < bottom; y += spacing) {
        for (double x = left + spacing * 0.47; x < right; x += spacing) {
            Point point{x, y};
            for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
                MeshTriangle triangle = mesh.triangles[i];
                Point a = mesh.nodes[triangle.nodes[0]].source, b = mesh.nodes[triangle.nodes[1]].source,
                      c = mesh.nodes[triangle.nodes[2]].source;
                double guard = spacing * spacing * 1.0e-4;
                if (orient(a, b, point) <= guard || orient(b, c, point) <= guard ||
                    orient(c, a, point) <= guard) {
                    continue;
                }
                if (mesh.nodes.size() >= 2048) {
                    throw std::length_error("Use a coarser reshape spacing; mesh limit is 2048 nodes.");
                }
                std::size_t node = mesh.nodes.size();
                mesh.nodes.push_back({point, point, false});
                mesh.triangles[i] = {{triangle.nodes[0], triangle.nodes[1], node}};
                mesh.triangles.push_back({{triangle.nodes[1], triangle.nodes[2], node}});
                mesh.triangles.push_back({{triangle.nodes[2], triangle.nodes[0], node}});
                break;
            }
        }
    }
    improve_mesh_angles(mesh);
    if (!reshape_mesh_valid(mesh)) {
        throw std::invalid_argument("Reshape triangulation is degenerate.");
    }
    return mesh;
}
ReshapeMesh make_reshape_mesh(const Image& source, double spacing) {
    if (source.width < 1 || source.height < 1) {
        throw std::invalid_argument("An empty image cannot be reshaped.");
    }
    // The full pixel footprint includes the boundary pixels under identity.
    std::vector<Point> outline{{-0.5, -0.5},
                               {source.width - 0.5, -0.5},
                               {source.width - 0.5, source.height - 0.5},
                               {-0.5, source.height - 0.5}};
    ReshapeMesh mesh = make_reshape_mesh(outline, spacing);
    return mesh;
}
namespace {
static AffineMap triangle_inverse(const MeshNode& a, const MeshNode& b, const MeshNode& c) {
    AffineMap target{b.target.x - a.target.x, c.target.x - a.target.x, a.target.x,
                     b.target.y - a.target.y, c.target.y - a.target.y, a.target.y};
    AffineMap inverse = inverse_map(target);
    double bx = b.source.x - a.source.x, cx = c.source.x - a.source.x;
    double by = b.source.y - a.source.y, cy = c.source.y - a.source.y;
    AffineMap map{bx * inverse.xx + cx * inverse.yx,
                  bx * inverse.xy + cx * inverse.yy,
                  a.source.x + bx * inverse.tx + cx * inverse.ty,
                  by * inverse.xx + cy * inverse.yx,
                  by * inverse.xy + cy * inverse.yy,
                  a.source.y + by * inverse.tx + cy * inverse.ty};
    return map;
}
// The largest number of grid nodes a mesh map is sampled on; a larger
// destination doubles its spacing until it fits.
constexpr std::size_t maximum_grid_nodes = std::size_t{1} << 22;
// The destination grid a reshape mesh is carried on: nodes `spacing` pixels
// apart from (0, 0) to past the last pixel centre, each with the triangle
// whose map gives its source position (none yet: `unassigned`).
struct MeshGrid {
    std::size_t nodes_x = 0, nodes_y = 0;
    double spacing = 1.0;
    std::vector<std::size_t> triangle;
    std::vector<double> source_uv;
};
constexpr std::size_t unassigned = std::numeric_limits<std::size_t>::max();
static MeshGrid make_mesh_grid(int width, int height) {
    MeshGrid grid;
    std::size_t step = 1;
    for (;;) {
        grid.nodes_x = static_cast<std::size_t>(width - 1) / step + 2;
        grid.nodes_y = static_cast<std::size_t>(height - 1) / step + 2;
        if (grid.nodes_x * grid.nodes_y <= maximum_grid_nodes) {
            break;
        }
        step = step * 2;
    }
    grid.spacing = static_cast<double>(step);
    grid.triangle.assign(grid.nodes_x * grid.nodes_y, unassigned);
    grid.source_uv.assign(grid.nodes_x * grid.nodes_y * 2, 0.0);
    return grid;
}
// Each grid node inside a target triangle takes that triangle (the first
// that holds it, shared edges once).
static void assign_inside(MeshGrid& grid, const ReshapeMesh& mesh) {
    for (std::size_t index = 0; index < mesh.triangles.size(); ++index) {
        const MeshTriangle& triangle = mesh.triangles[index];
        const Point a = mesh.nodes[triangle.nodes[0]].target;
        const Point b = mesh.nodes[triangle.nodes[1]].target;
        const Point c = mesh.nodes[triangle.nodes[2]].target;
        const double low_x = std::max(0.0, std::ceil(std::min({a.x, b.x, c.x}) / grid.spacing));
        const double high_x =
            std::min(static_cast<double>(grid.nodes_x - 1), std::floor(std::max({a.x, b.x, c.x}) / grid.spacing));
        const double low_y = std::max(0.0, std::ceil(std::min({a.y, b.y, c.y}) / grid.spacing));
        const double high_y =
            std::min(static_cast<double>(grid.nodes_y - 1), std::floor(std::max({a.y, b.y, c.y}) / grid.spacing));
        if (high_x < low_x || high_y < low_y) {
            continue;
        }
        const double tolerance = 1.0e-10 * std::max(1.0, orient(a, b, c));
        for (std::size_t row = static_cast<std::size_t>(low_y); row <= static_cast<std::size_t>(high_y); ++row) {
            for (std::size_t column = static_cast<std::size_t>(low_x); column <= static_cast<std::size_t>(high_x);
                 ++column) {
                const std::size_t node = row * grid.nodes_x + column;
                const Point point{static_cast<double>(column) * grid.spacing,
                                  static_cast<double>(row) * grid.spacing};
                if (grid.triangle[node] == unassigned && within_triangle(point, a, b, c, tolerance)) {
                    grid.triangle[node] = index;
                }
            }
        }
    }
}
// A grid node outside every triangle takes the triangle of its nearest
// assigned node (a breadth-first sweep from every assigned node over the
// grid's four neighbours), whose map then continues past the mesh's edge.
static void assign_outside(MeshGrid& grid) {
    std::vector<std::size_t> frontier;
    for (std::size_t node = 0; node < grid.triangle.size(); ++node) {
        if (grid.triangle[node] != unassigned) {
            frontier.push_back(node);
        }
    }
    if (frontier.empty()) {
        throw std::invalid_argument("The reshape mesh lies outside the destination.");
    }
    std::vector<std::size_t> next;
    while (!frontier.empty()) {
        next.clear();
        for (std::size_t node : frontier) {
            const std::size_t row = node / grid.nodes_x;
            const std::size_t column = node % grid.nodes_x;
            const std::size_t neighbours[4] = {column > 0 ? node - 1 : unassigned,
                                               column + 1 < grid.nodes_x ? node + 1 : unassigned,
                                               row > 0 ? node - grid.nodes_x : unassigned,
                                               row + 1 < grid.nodes_y ? node + grid.nodes_x : unassigned};
            for (std::size_t neighbour : neighbours) {
                if (neighbour != unassigned && grid.triangle[neighbour] == unassigned) {
                    grid.triangle[neighbour] = grid.triangle[node];
                    next.push_back(neighbour);
                }
            }
        }
        frontier.swap(next);
    }
}
// Every grid node's source position: its triangle's map at the node, plus
// the ground's margin.
static void place_sources(MeshGrid& grid, const std::vector<AffineMap>& inverses) {
    for (std::size_t node = 0; node < grid.triangle.size(); ++node) {
        const AffineMap& inverse = inverses[grid.triangle[node]];
        const Point point{static_cast<double>(node % grid.nodes_x) * grid.spacing,
                          static_cast<double>(node / grid.nodes_x) * grid.spacing};
        const Point source = transformed(point, inverse);
        grid.source_uv[node * 2] = source.x + ground_margin;
        grid.source_uv[node * 2 + 1] = source.y + ground_margin;
    }
}
} // namespace
void render_mesh(const ConvWarpField& field, const ReshapeMesh& mesh, int width, int height,
                 Image& destination, WarpSampling sampling) {
    if (field.width() < 1) {
        throw std::invalid_argument("Compile the CONV material first.");
    }
    if (!reshape_mesh_valid(mesh)) {
        throw std::invalid_argument("The reshape mesh folds or overlaps.");
    }
    if (width < 1 || height < 1) {
        throw std::invalid_argument("Cannot render a mesh to an empty image.");
    }
    std::vector<AffineMap> inverses;
    inverses.reserve(mesh.triangles.size());
    double footprint = 1.0;
    for (const MeshTriangle& triangle : mesh.triangles) {
        AffineMap inverse = triangle_inverse(mesh.nodes[triangle.nodes[0]], mesh.nodes[triangle.nodes[1]],
                                             mesh.nodes[triangle.nodes[2]]);
        footprint = std::max(footprint, map_footprint(inverse));
        inverses.push_back(inverse);
    }
    // The mesh as stxlib::conv's MeshMap: a regular destination grid whose
    // nodes carry the piecewise-affine source position (continued past the
    // mesh by the nearest triangle's map, onto the transparent ground).
    MeshGrid grid = make_mesh_grid(width, height);
    assign_inside(grid, mesh);
    assign_outside(grid);
    place_sources(grid, inverses);
    const stxlib::conv::MeshMap map{grid.nodes_x, grid.nodes_y, grid.spacing,
                                    stxlib::View<double>(grid.source_uv.data(), grid.source_uv.size())};
    Image output;
    output.reset(width, height, {0, 0, 0, 0});
    transfer_into(field.ground(), map, transfer_mode(sampling, footprint), output, nullptr);
    destination = std::move(output);
}
} // namespace paint
