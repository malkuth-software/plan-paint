// PolyForm Noncommercial License 1.0.0 (LICENSE). Copyright (c) 2026 joshuah.rainstar@gmail.com.
// Native adaptation by Astra, sponsored by Joshuah. Thanks to Hashem.
#include "warp.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace paint {
void ConvWarpField::compile(const Image& source, const std::atomic<bool>* cancelled) {
    if (source.width < 1 || source.height < 1 || source.width > 32768 || source.height > 32768 ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height) {
        throw std::invalid_argument("Invalid CONV source image.");
    }
    if (cancelled && (*cancelled).load(std::memory_order_relaxed)) {
        throw std::runtime_error("CONV preparation superseded.");
    }
    const Color first = source.pixels.front();
    bool constant = true;
    for (const Color color : source.pixels) {
        if (!equal(color, first)) {
            constant = false;
            break;
        }
    }
    if (constant) {
        const double alpha = first.a / 255.0;
        constant_value_ = {first.r / 255.0 * alpha, first.g / 255.0 * alpha, first.b / 255.0 * alpha, alpha};
        constant_ = true;
        surface_.clear();
        width_ = source.width;
        height_ = source.height;
        return;
    }
    // A one-pixel axis is repeated: the surface across it is then constant
    // and along the other axis it is that line's CONV curve.
    CanvasSurface surface;
    if (source.width == 1 || source.height == 1) {
        Image padded;
        padded.reset(std::max(2, source.width), std::max(2, source.height));
        for (int y = 0; y < padded.height; ++y) {
            for (int x = 0; x < padded.width; ++x) {
                padded.set(x, y, source.get(std::min(x, source.width - 1), std::min(y, source.height - 1)));
            }
        }
        surface.build(padded);
    } else {
        surface.build(source);
    }
    if (cancelled && (*cancelled).load(std::memory_order_relaxed)) {
        throw std::runtime_error("CONV preparation superseded.");
    }
    surface_ = std::move(surface);
    constant_ = false;
    width_ = source.width;
    height_ = source.height;
}
std::size_t ConvWarpField::storage_bytes() const {
    if (constant_ || !surface_.ready()) {
        return 0;
    }
    // The surface's cells (seven doubles a channel) and premultiplied pixels.
    const std::size_t pixels = static_cast<std::size_t>(surface_.width()) * surface_.height();
    return pixels * (4 * 7 * sizeof(double) + 4 * sizeof(double));
}
WarpSample ConvWarpField::sample_components(Point position) const {
    WarpSample sample;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || position.x < -0.5 || position.y < -0.5 ||
        position.x >= width_ - 0.5 || position.y >= height_ - 0.5 || (!constant_ && !surface_.ready())) {
        return sample;
    }
    if (constant_) {
        return constant_value_;
    }
    // On a one-pixel axis every position is that pixel's centre.
    const Point at{width_ == 1 ? 0.0 : position.x, height_ == 1 ? 0.0 : position.y};
    sample = surface_.sample_components(at);
    return sample;
}
WarpSample ConvWarpField::integrate_cell_polygon(std::span<const Point> polygon) const {
    WarpSample total;
    if (polygon.size() < 3) {
        return total;
    }
    if (constant_) {
        double twice_area = 0.0;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const Point a = polygon[i], b = polygon[(i + 1) % polygon.size()];
            twice_area += a.x * b.y - a.y * b.x;
        }
        const double area = 0.5 * std::abs(twice_area);
        total = {constant_value_.r * area, constant_value_.g * area, constant_value_.b * area,
                 constant_value_.a * area};
        return total;
    }
    // A one-pixel axis was repeated: the surface is constant across it, so
    // the polygon's place across it does not matter.
    total = surface_.integrate_cell_polygon(polygon);
    return total;
}
WarpSample ConvWarpField::sample_premultiplied(Point position) const {
    WarpSample value = sample_components(position);
    // Keep the admitted opacity. Project color at each query, before any positive
    // filter weights are accumulated; clipping only the final average is too late.
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
struct SampleRule {
    int count = 1;
    double offsets[8] = {};
    double weights[8] = {1.0};
};
static SampleRule sample_rule(double footprint, WarpSampling sampling) {
    SampleRule rule;
    double side = 1.0;
    if (sampling == WarpSampling::Point) {
        return rule;
    }
    if (sampling == WarpSampling::Minification) {
        if (footprint <= 1.0) {
            return rule;
        }
        double reciprocal = 1.0 / footprint;
        side = std::sqrt((1.0 - reciprocal) * (1.0 + reciprocal));
    }
    // Fixed nodes avoid finite rule changes as a transform crosses a threshold.
    // The residual square shrinks continuously to the cardinal point sampler.
    rule.count = 8;
    constexpr double offsets[8] = {-0.48014492824876812, -0.39833323870681337, -0.26276620495816449,
                                   -0.09171732124782490, 0.09171732124782490,  0.26276620495816449,
                                   0.39833323870681337,  0.48014492824876812};
    constexpr double weights[8] = {0.05061426814518813, 0.11119051722668724, 0.15685332293894364,
                                   0.18134189168918099, 0.18134189168918099, 0.15685332293894364,
                                   0.11119051722668724, 0.05061426814518813};
    for (int i = 0; i < 8; ++i) {
        rule.offsets[i] = side * offsets[i];
        rule.weights[i] = weights[i];
    }
    return rule;
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
static void add_weighted(WarpSample& sum, WarpSample value, double weight) {
    sum.r += value.r * weight;
    sum.g += value.g * weight;
    sum.b += value.b * weight;
    sum.a += value.a * weight;
}
// A committed destination pixel is a parallelogram in source coordinates.
// Clip it at every source-cell boundary before quadrature: a single fixed
// target stencil can miss arbitrarily many thin cells under strong reduction.
struct FootprintPolygon {
    std::array<Point, 12> points = {};
    int count = 0;
};
static FootprintPolygon clip_footprint(const FootprintPolygon& input, int axis, double boundary,
                                       bool greater) {
    FootprintPolygon output;
    if (!input.count) {
        return output;
    }
    Point previous = input.points[input.count - 1];
    double pv = axis == 0 ? previous.x : previous.y;
    bool previous_inside = greater ? pv >= boundary : pv <= boundary;
    for (int i = 0; i < input.count; ++i) {
        const Point current = input.points[i];
        const double cv = axis == 0 ? current.x : current.y;
        const bool inside = greater ? cv >= boundary : cv <= boundary;
        if (inside != previous_inside) {
            const double t = (boundary - pv) / (cv - pv);
            output.points[output.count++] = {previous.x + t * (current.x - previous.x),
                                             previous.y + t * (current.y - previous.y)};
        }
        if (inside) {
            output.points[output.count++] = current;
        }
        previous = current;
        pv = cv;
        previous_inside = inside;
    }
    return output;
}
static WarpSample integrate_footprint(const ConvWarpField& field, const FootprintPolygon& footprint,
                                      double density) {
    double left = 1e30, top = 1e30, right = -1e30, bottom = -1e30;
    for (int i = 0; i < footprint.count; ++i) {
        const Point point = footprint.points[i];
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    WarpSample total;
    if (right <= -0.5 || bottom <= -0.5 || left >= field.width() - 0.5 || top >= field.height() - 0.5) {
        return total;
    }
    const int first_x = static_cast<int>(std::floor(std::max(-0.5, left)));
    const int first_y = static_cast<int>(std::floor(std::max(-0.5, top)));
    const int last_x = static_cast<int>(std::floor(std::min(field.width() - 0.5, right)));
    const int last_y = static_cast<int>(std::floor(std::min(field.height() - 0.5, bottom)));
    // Each piece of the footprint inside one evaluation cell is integrated by
    // the surface (CanvasSurface::integrate_cell_polygon): exact before
    // projection while the admission keeps its branch, with each value
    // projected to physical RGBA before it is integrated otherwise.
    for (int y = first_y; y <= last_y; ++y) {
        FootprintPolygon row = clip_footprint(footprint, 1, std::max(-0.5, static_cast<double>(y)), true);
        row = clip_footprint(row, 1, std::min(field.height() - 0.5, y + 1.0), false);
        if (row.count < 3) {
            continue;
        }
        for (int x = first_x; x <= last_x; ++x) {
            FootprintPolygon cell = clip_footprint(row, 0, std::max(-0.5, static_cast<double>(x)), true);
            cell = clip_footprint(cell, 0, std::min(field.width() - 0.5, x + 1.0), false);
            if (cell.count < 3) {
                continue;
            }
            add_weighted(total,
                         field.integrate_cell_polygon(
                             std::span<const Point>(cell.points.data(), static_cast<std::size_t>(cell.count))),
                         density);
        }
    }
    return total;
}
static WarpSample integrate_affine_pixel(const ConvWarpField& field, const AffineMap& inverse, Point center) {
    FootprintPolygon footprint;
    footprint.count = 4;
    constexpr double dx[4] = {-0.5, 0.5, 0.5, -0.5}, dy[4] = {-0.5, -0.5, 0.5, 0.5};
    for (int i = 0; i < 4; ++i) {
        footprint.points[i] = {center.x + inverse.xx * dx[i] + inverse.xy * dy[i],
                               center.y + inverse.yx * dx[i] + inverse.yy * dy[i]};
    }
    return integrate_footprint(field, footprint,
                               1.0 / std::abs(inverse.xx * inverse.yy - inverse.xy * inverse.yx));
}
struct AffineStencil {
    int count = 1;
    Point offsets[64] = {};
    double weights[64] = {1.0};
};
static AffineStencil prepare_affine_stencil(const AffineMap& inverse, const SampleRule& rule) {
    AffineStencil stencil;
    stencil.count = rule.count * rule.count;
    for (int j = 0; j < rule.count; ++j) {
        for (int i = 0; i < rule.count; ++i) {
            int index = j * rule.count + i;
            stencil.offsets[index] = {inverse.xx * rule.offsets[i] + inverse.xy * rule.offsets[j],
                                      inverse.yx * rule.offsets[i] + inverse.yy * rule.offsets[j]};
            stencil.weights[index] = rule.weights[i] * rule.weights[j];
        }
    }
    return stencil;
}
struct AffineJob {
    const ConvWarpField& field;
    const AffineMap& inverse;
    const AffineStencil& stencil;
    Image& output;
    bool area = false;
    const std::atomic<bool>* cancelled = nullptr;
    std::atomic<int> next_row{0};
};
static void affine_worker(AffineJob& job) {
    const int width = job.output.width, height = job.output.height;
    const AffineMap inverse = job.inverse;
    const AffineStencil stencil = job.stencil;
    const ConvWarpField& field = job.field;
    std::vector<Color>& pixels = job.output.pixels;
    for (;;) {
        if (job.cancelled && (*job.cancelled).load(std::memory_order_relaxed)) { return; }
        int y = job.next_row.fetch_add(1, std::memory_order_relaxed);
        if (y >= height) {
            return;
        }
        double row_x = inverse.xy * y + inverse.tx;
        double row_y = inverse.yy * y + inverse.ty;
        std::size_t offset = static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            if (x % 64 == 0 && job.cancelled && (*job.cancelled).load(std::memory_order_relaxed)) { return; }
            Point centre{row_x + inverse.xx * x, row_y + inverse.yx * x};
            WarpSample total;
            if (job.area) {
                total = integrate_affine_pixel(field, inverse, centre);
            }
            for (int i = 0; i < (job.area ? 0 : stencil.count); ++i) {
                Point position{centre.x + stencil.offsets[i].x, centre.y + stencil.offsets[i].y};
                WarpSample value = field.sample_premultiplied(position);
                add_weighted(total, value, stencil.weights[i]);
            }
            pixels[offset + x] = encoded_sample(total);
        }
    }
}
// Bounded executors serve concurrent viewport and commit callers. Threads
// sleep between jobs; each caller retains its stack-owned job until completion.
class AffineExecutor {
  public:
    AffineExecutor() {
        const unsigned count = std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
        try {
            for (unsigned i = 0; i < count; ++i) {
                workers_.emplace_back(run, std::ref(*this));
            }
        } catch (...) {
            stop();
            throw;
        }
    }
    ~AffineExecutor() {
        stop();
    }
    void execute(AffineJob& job) {
        std::lock_guard<std::mutex> caller(caller_mutex_);
        std::unique_lock<std::mutex> lock(mutex_);
        job_ = &job;
        remaining_ = workers_.size();
        error_ = {};
        ++generation_;
        ready_.notify_all();
        while (remaining_) {
            finished_.wait(lock);
        }
        job_ = nullptr;
        if (error_) {
            std::rethrow_exception(error_);
        }
    }

  private:
    std::mutex caller_mutex_, mutex_;
    std::condition_variable ready_, finished_;
    std::vector<std::thread> workers_;
    AffineJob* job_ = nullptr;
    std::uint64_t generation_ = 0;
    std::size_t remaining_ = 0;
    bool stopping_ = false;
    std::exception_ptr error_;
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (std::thread& worker : workers_) {
            worker.join();
        }
    }
    static void run(AffineExecutor& executor) {
        std::uint64_t seen = 0;
        std::unique_lock<std::mutex> lock(executor.mutex_);
        for (;;) {
            while (!executor.stopping_ && seen == executor.generation_) {
                executor.ready_.wait(lock);
            }
            if (executor.stopping_) {
                return;
            }
            seen = executor.generation_;
            AffineJob& job = *executor.job_;
            lock.unlock();
            std::exception_ptr error;
            try {
                affine_worker(job);
            } catch (...) {
                error = std::current_exception();
            }
            lock.lock();
            if (error && !executor.error_) {
                executor.error_ = error;
            }
            if (--executor.remaining_ == 0) {
                executor.finished_.notify_one();
            }
        }
    }
};
static AffineExecutor& affine_executor(WarpSampling sampling) {
    // A long final area integration must not hold up interactive point views.
    if (sampling == WarpSampling::Point) {
        static AffineExecutor interactive;
        return interactive;
    }
    static AffineExecutor final;
    return final;
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
    if (cancelled && (*cancelled).load(std::memory_order_relaxed)) { throw std::runtime_error("CONV rendering cancelled."); }
    if (field.width() < 1) {
        throw std::invalid_argument("Compile the CONV material first.");
    }
    AffineMap inverse = inverse_map(map);
    Image output;
    output.reset(width, height, {0, 0, 0, 0});
    SampleRule rule = sample_rule(map_footprint(inverse), sampling);
    AffineStencil stencil = prepare_affine_stencil(inverse, rule);
    AffineJob job{field, inverse, stencil, output, sampling == WarpSampling::Area};
    job.cancelled = cancelled;
    if (output.pixels.size() < 65536) {
        affine_worker(job);
    } else {
        affine_executor(sampling).execute(job);
    }
    if (cancelled && (*cancelled).load(std::memory_order_relaxed)) { throw std::runtime_error("CONV rendering cancelled."); }
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
} // namespace
void render_mesh(const ConvWarpField& field, const ReshapeMesh& mesh, int width, int height,
                 Image& destination, WarpSampling sampling) {
    if (field.width() < 1) {
        throw std::invalid_argument("Compile the CONV material first.");
    }
    if (!reshape_mesh_valid(mesh)) {
        throw std::invalid_argument("The reshape mesh folds or overlaps.");
    }
    Image output;
    output.reset(width, height, {0, 0, 0, 0});
    double footprint = 1.0;
    for (const MeshTriangle& triangle : mesh.triangles) {
        AffineMap inverse = triangle_inverse(mesh.nodes[triangle.nodes[0]], mesh.nodes[triangle.nodes[1]],
                                             mesh.nodes[triangle.nodes[2]]);
        footprint = std::max(footprint, map_footprint(inverse));
    }
    SampleRule rule = sample_rule(footprint, sampling);
    // One bit owns each quadrature node. Shared triangle edges can include
    // the same point, but exactly one triangle contributes its value.
    std::vector<std::uint64_t> ownership(output.pixels.size(), 0);
    std::vector<WarpSample> totals(output.pixels.size());
    for (const MeshTriangle& triangle : mesh.triangles) {
        const MeshNode& a = mesh.nodes[triangle.nodes[0]];
        const MeshNode& b = mesh.nodes[triangle.nodes[1]];
        const MeshNode& c = mesh.nodes[triangle.nodes[2]];
        AffineMap inverse = triangle_inverse(a, b, c);
        double area = orient(a.target, b.target, c.target);
        double low_x =
            std::clamp(std::min({a.target.x, b.target.x, c.target.x}) - 0.5, 0.0, static_cast<double>(width));
        double high_x = std::clamp(std::max({a.target.x, b.target.x, c.target.x}) + 0.5, -1.0,
                                   static_cast<double>(width - 1));
        double low_y = std::clamp(std::min({a.target.y, b.target.y, c.target.y}) - 0.5, 0.0,
                                  static_cast<double>(height));
        double high_y = std::clamp(std::max({a.target.y, b.target.y, c.target.y}) + 0.5, -1.0,
                                   static_cast<double>(height - 1));
        int left = static_cast<int>(std::floor(low_x)), right = static_cast<int>(std::ceil(high_x));
        int top = static_cast<int>(std::floor(low_y)), bottom = static_cast<int>(std::ceil(high_y));
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                std::size_t pixel = static_cast<std::size_t>(y) * width + x;
                if (sampling == WarpSampling::Area) {
                    FootprintPolygon polygon;
                    polygon.count = 3;
                    polygon.points[0] = a.target;
                    polygon.points[1] = b.target;
                    polygon.points[2] = c.target;
                    polygon = clip_footprint(polygon, 0, x - 0.5, true);
                    polygon = clip_footprint(polygon, 0, x + 0.5, false);
                    polygon = clip_footprint(polygon, 1, y - 0.5, true);
                    polygon = clip_footprint(polygon, 1, y + 0.5, false);
                    if (polygon.count >= 3) {
                        for (int i = 0; i < polygon.count; ++i) {
                            polygon.points[i] = transformed(polygon.points[i], inverse);
                        }
                        add_weighted(totals[pixel],
                                     integrate_footprint(
                                         field, polygon,
                                         1.0 / std::abs(inverse.xx * inverse.yy - inverse.xy * inverse.yx)),
                                     1.0);
                    }
                    continue;
                }
                for (int j = 0; j < rule.count; ++j) {
                    for (int i = 0; i < rule.count; ++i) {
                        std::uint64_t bit = std::uint64_t{1} << (j * rule.count + i);
                        if ((ownership[pixel] & bit) != 0) {
                            continue;
                        }
                        Point point{x + rule.offsets[i], y + rule.offsets[j]};
                        double wb = orient(a.target, point, c.target) / area;
                        double wc = orient(a.target, b.target, point) / area;
                        double wa = 1.0 - wb - wc;
                        if (wa < -1.0e-10 || wb < -1.0e-10 || wc < -1.0e-10) {
                            continue;
                        }
                        Point source = transformed(point, inverse);
                        WarpSample value = field.sample_premultiplied(source);
                        add_weighted(totals[pixel], value, rule.weights[i] * rule.weights[j]);
                        ownership[pixel] |= bit;
                    }
                }
            }
        }
    }
    for (std::size_t i = 0; i < output.pixels.size(); ++i) {
        output.pixels[i] = encoded_sample(totals[i]);
    }
    destination = std::move(output);
}
} // namespace paint
