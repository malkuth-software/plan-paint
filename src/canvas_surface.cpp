#include "canvas_surface.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <span>
#include <stdexcept>
#include <thread>

namespace paint {
namespace {
const std::size_t channels = 4;
// Areas below this many cells or pixels run on the calling thread.
const std::size_t parallel_threshold = 65536;

unsigned worker_count(std::size_t work) {
    if (work < parallel_threshold) {
        return 1;
    }
    const unsigned count = std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
    return count;
}
// The cell holding a clamped coordinate and the local parameter in it, as
// stx_conv::surface::Surface locates an open axis.
void locate(double position, std::size_t cells, std::size_t& cell, double& t) {
    const double extent = static_cast<double>(cells);
    const double clamped = position < 0.0 ? 0.0 : position > extent ? extent : position;
    double whole = std::floor(clamped);
    if (whole >= extent) {
        whole = extent - 1.0;
    }
    cell = static_cast<std::size_t>(whole);
    t = clamped - whole;
}
Color encoded(WarpSample value) {
    const double alpha = std::clamp(value.a, 0.0, 1.0);
    if (alpha < 0.5 / 255.0) {
        return {0, 0, 0, 0};
    }
    const Color result{static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.r / alpha, 0.0, 1.0))),
                       static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.g / alpha, 0.0, 1.0))),
                       static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.b / alpha, 0.0, 1.0))),
                       static_cast<std::uint8_t>(std::round(255.0 * alpha))};
    return result;
}
} // namespace

// Rows claimed one at a time from `next`; each row's cells are written by
// one worker only.
struct CanvasSurfaceBuildJob {
    CanvasSurface& surface;
    std::atomic<int> next{0};
    static void run(CanvasSurfaceBuildJob& job) {
        for (;;) {
            const int row = job.next.fetch_add(1, std::memory_order_relaxed);
            if (row >= job.surface.height()) {
                return;
            }
            job.surface.compile_row_cells(row, 0, job.surface.width() - 2);
        }
    }
};
namespace {
struct RenderJob {
    const CanvasSurface& surface;
    const CanvasSurface::TargetMap& map;
    int left = 0, top = 0, columns = 0, rows = 0;
    Color* target = nullptr;
    std::size_t stride = 0;
    std::atomic<int> next{0};
};
void render_worker(RenderJob& job) {
    for (;;) {
        const int y = job.next.fetch_add(1, std::memory_order_relaxed);
        if (y >= job.rows) {
            return;
        }
        Color* const row = job.target + static_cast<std::size_t>(y) * job.stride;
        const double cy = job.top + y + 0.5;
        for (int x = 0; x < job.columns; ++x) {
            const double cx = job.left + x + 0.5;
            const Point position{job.map.ux * cx + job.map.uy * cy + job.map.u0,
                                 job.map.vx * cx + job.map.vy * cy + job.map.v0};
            row[x] = job.surface.sample(position);
        }
    }
}
} // namespace

void CanvasSurface::clear() noexcept {
    ready_ = false;
    width_ = 0;
    height_ = 0;
    nodes_ = {};
    cells_ = {};
    shapes_u_ = {};
    shapes_v_ = {};
}
void CanvasSurface::build(const Image& source) {
    clear();
    if (source.width < 2 || source.height < 2 ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height) {
        throw std::invalid_argument("The CONV surface needs at least 2 x 2 pixels.");
    }
    if (source.pixels.size() > maximum_pixels) {
        throw std::length_error("The image is too large for the turned CONV view (4 megapixels at most).");
    }
    try {
        width_ = source.width;
        height_ = source.height;
        nodes_.resize(source.pixels.size() * channels);
        for (std::size_t i = 0; i < source.pixels.size(); ++i) {
            const Color color = source.pixels[i];
            const double alpha = color.a / 255.0;
            nodes_[i * 4] = color.r / 255.0 * alpha;
            nodes_[i * 4 + 1] = color.g / 255.0 * alpha;
            nodes_[i * 4 + 2] = color.b / 255.0 * alpha;
            nodes_[i * 4 + 3] = alpha;
        }
        const stx_conv::surface::LineLayout across{static_cast<std::size_t>(width_), false, closure, {}};
        const stx_conv::surface::LineLayout down{static_cast<std::size_t>(height_), false, closure, {}};
        shapes_u_.resize(static_cast<std::size_t>(width_) - 1);
        shapes_v_.resize(static_cast<std::size_t>(height_) - 1);
        for (std::size_t cell = 0; cell < shapes_u_.size(); ++cell) {
            shapes_u_[cell] = stx_conv::surface::cell_shape(across, cell);
        }
        for (std::size_t cell = 0; cell < shapes_v_.size(); ++cell) {
            shapes_v_[cell] = stx_conv::surface::cell_shape(down, cell);
        }
        cells_.resize(static_cast<std::size_t>(height_) * shapes_u_.size() * channels);
        CanvasSurfaceBuildJob job{*this};
        const unsigned count = worker_count(cells_.size());
        if (count == 1) {
            CanvasSurfaceBuildJob::run(job);
        } else {
            std::vector<std::jthread> workers;
            workers.reserve(count);
            for (unsigned index = 0; index < count; ++index) {
                workers.emplace_back(CanvasSurfaceBuildJob::run, std::ref(job));
            }
        }
    } catch (...) {
        clear();
        throw;
    }
    ready_ = true;
}
void CanvasSurface::compile_row_cells(int row, int first_cell, int last_cell) {
    const std::size_t row_nodes = static_cast<std::size_t>(row) * width_;
    const std::size_t row_cells = static_cast<std::size_t>(row) * (width_ - 1);
    std::array<double, 8> window{};
    for (int cell = first_cell; cell <= last_cell; ++cell) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            for (int k = 0; k < 8; ++k) {
                const int index = cell - 3 + k;
                // Outside the line the entries are zero and never read.
                window[k] = index >= 0 && index < width_
                                ? nodes_[(row_nodes + static_cast<std::size_t>(index)) * channels + channel]
                                : 0.0;
            }
            cells_[(row_cells + static_cast<std::size_t>(cell)) * channels + channel] =
                stx_conv::surface::compile_line_cell(std::span<const double, 8>(window), shapes_u_[cell], bubble);
        }
    }
}
void CanvasSurface::update(const Image& source, Rect damage) {
    if (!ready_ || source.width != width_ || source.height != height_) {
        throw std::invalid_argument("The CONV surface was built for another image size.");
    }
    const int left = std::max(0, damage.x), top = std::max(0, damage.y);
    const int right = std::min(width_, damage.x + damage.w), bottom = std::min(height_, damage.y + damage.h);
    if (right <= left || bottom <= top) {
        return;
    }
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * width_ + x;
            const Color color = source.pixels[i];
            const double alpha = color.a / 255.0;
            nodes_[i * 4] = color.r / 255.0 * alpha;
            nodes_[i * 4 + 1] = color.g / 255.0 * alpha;
            nodes_[i * 4 + 2] = color.b / 255.0 * alpha;
            nodes_[i * 4 + 3] = alpha;
        }
    }
    // Cell i reads pixels i-3..i+4: the cells that read a damaged pixel.
    const int first_cell = std::max(0, left - 4);
    const int last_cell = std::min(width_ - 2, right - 1 + 3);
    for (int y = top; y < bottom; ++y) {
        compile_row_cells(y, first_cell, last_cell);
    }
}
WarpSample CanvasSurface::sample_components(Point position) const {
    WarpSample result;
    if (!ready_ || !std::isfinite(position.x) || !std::isfinite(position.y) || position.x < -0.5 ||
        position.y < -0.5 || position.x >= width_ - 0.5 || position.y >= height_ - 0.5) {
        return result;
    }
    const std::size_t cells_u = static_cast<std::size_t>(width_) - 1;
    std::size_t cell_u = 0, cell_v = 0;
    double t_u = 0.0, t_v = 0.0;
    locate(position.x, cells_u, cell_u, t_u);
    locate(position.y, static_cast<std::size_t>(height_) - 1, cell_v, t_v);
    // The u-line values at u of the eight rows around the v-cell.
    std::array<std::array<double, 8>, channels> windows{};
    for (int k = 0; k < 8; ++k) {
        const int row = static_cast<int>(cell_v) - 3 + k;
        if (row < 0 || row >= height_) {
            continue;
        }
        const std::size_t first = (static_cast<std::size_t>(row) * cells_u + cell_u) * channels;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            windows[channel][k] = stx_conv::surface::evaluate_line_cell(cells_[first + channel], t_u);
        }
    }
    double values[channels] = {};
    for (std::size_t channel = 0; channel < channels; ++channel) {
        const stx_conv::surface::LineCell across = stx_conv::surface::compile_line_cell(
            std::span<const double, 8>(windows[channel]), shapes_v_[cell_v], bubble);
        values[channel] = stx_conv::surface::evaluate_line_cell(across, t_v);
    }
    result = {values[0], values[1], values[2], values[3]};
    return result;
}
WarpSample CanvasSurface::sample_premultiplied(Point position) const {
    WarpSample value = sample_components(position);
    value.a = std::clamp(value.a, 0.0, 1.0);
    value.r = std::clamp(value.r, 0.0, value.a);
    value.g = std::clamp(value.g, 0.0, value.a);
    value.b = std::clamp(value.b, 0.0, value.a);
    return value;
}
Color CanvasSurface::sample(Point position) const {
    const Color color = encoded(sample_premultiplied(position));
    return color;
}
void CanvasSurface::render(const TargetMap& map, int left, int top, int columns, int rows, Color* target,
                           std::size_t stride) const {
    if (columns <= 0 || rows <= 0) {
        return;
    }
    RenderJob job{*this, map, left, top, columns, rows, target, stride};
    const unsigned count = worker_count(static_cast<std::size_t>(columns) * rows);
    if (count == 1) {
        render_worker(job);
        return;
    }
    std::vector<std::jthread> workers;
    workers.reserve(count);
    for (unsigned index = 0; index < count; ++index) {
        workers.emplace_back(render_worker, std::ref(job));
    }
}
} // namespace paint
