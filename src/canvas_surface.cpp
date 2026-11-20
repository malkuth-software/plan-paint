#include "canvas_surface.hpp"
#if defined(__clang__) && defined(__aarch64__) && defined(__APPLE__)
#include "conv_point_lanes.hpp"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <mutex>
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
// Six-point Gauss-Legendre on [0, 1].
const double gauss_nodes[6] = {0.033765242898423975, 0.16939530676686774, 0.38069040695840156,
                               0.61930959304159844,  0.83060469323313226, 0.96623475710157603};
const double gauss_weights[6] = {0.08566224618958517, 0.1803807865240693, 0.23395696728634552,
                                 0.23395696728634552, 0.1803807865240693, 0.08566224618958517};
// The integrals over [0, t] of the five current functions tau_j (tau_j is the
// sum of the quintic Bernstein polynomials B_r for r > j). The integral of
// B_r over [0, t] is a sixth of the sum of the degree-six Bernstein
// polynomials B6_k(t) for k > r.
void tau_integrals(double t, double integrals[5]) {
    const double s = 1.0 - t;
    double t_power[7] = {1.0}, s_power[7] = {1.0};
    for (int k = 1; k < 7; ++k) {
        t_power[k] = t_power[k - 1] * t;
        s_power[k] = s_power[k - 1] * s;
    }
    const double binomial[7] = {1.0, 6.0, 15.0, 20.0, 15.0, 6.0, 1.0};
    // basis_integral[r] = integral of B_r over [0, t].
    double basis_integral[6] = {};
    double tail = 0.0;
    for (int k = 6; k >= 1; --k) {
        tail += binomial[k] * t_power[k] * s_power[6 - k];
        basis_integral[k - 1] = tail / 6.0;
    }
    double sum = 0.0;
    for (int j = 4; j >= 0; --j) {
        sum += basis_integral[j + 1];
        integrals[j] = sum;
    }
}
// A cell's value is the convex combination, by the quintic Bernstein
// polynomials, of its partial sums anchor + c_0 + ... + c_{r-1}. When those
// keep 0 <= alpha <= 1 and 0 <= colour <= alpha, so does every value in the
// cell, and the physical projection changes nothing there.
bool projection_free(const stx_conv::surface::LineCell* cells) {
    double alpha[6] = {cells[3].anchor};
    for (int r = 1; r < 6; ++r) {
        alpha[r] = alpha[r - 1] + cells[3].currents[r - 1];
    }
    for (int r = 0; r < 6; ++r) {
        if (!(alpha[r] >= 0.0 && alpha[r] <= 1.0)) {
            return false;
        }
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
        double colour = cells[channel].anchor;
        for (int r = 0; r < 6; ++r) {
            if (r > 0) {
                colour += cells[channel].currents[r - 1];
            }
            if (!(colour >= 0.0 && colour <= alpha[r])) {
                return false;
            }
        }
    }
    return true;
}
// The cell functions at t: tau_j (the sum of the quintic Bernstein
// polynomials B_r for r > j) and the bubble psi = [t(1 - t)]^3 (2t - 1).
struct CellBasis {
    double tau[5] = {};
    double psi = 0.0;
};
CellBasis cell_basis(double t) {
    CellBasis basis;
    const double s = 1.0 - t;
    const double t2 = t * t, t3 = t2 * t, t4 = t2 * t2;
    const double s2 = s * s, s3 = s2 * s, s4 = s2 * s2;
    basis.tau[4] = t4 * t;
    basis.tau[3] = basis.tau[4] + 5.0 * t4 * s;
    basis.tau[2] = basis.tau[3] + 10.0 * t3 * s2;
    basis.tau[1] = basis.tau[2] + 10.0 * t2 * s3;
    basis.tau[0] = basis.tau[1] + 5.0 * t * s4;
    basis.psi = t3 * s3 * (2.0 * t - 1.0);
    return basis;
}
#if defined(__clang__) && defined(__aarch64__) && defined(__APPLE__)
CellBasis point_basis(double t) {
    CellBasis basis;
    const double s = 1.0 - t;
    const double t2 = t * t, t3 = t2 * t, t4 = t2 * t2;
    const double s2 = s * s, s3 = s2 * s, s4 = s2 * s2;
    basis.tau[4] = t4 * t;
    basis.tau[3] = basis.tau[4] + 5.0 * (t4 * s);
    basis.tau[2] = basis.tau[3] + 10.0 * (t3 * s2);
    basis.tau[1] = basis.tau[2] + 10.0 * (t2 * s3);
    basis.tau[0] = basis.tau[1] + 5.0 * (t * s4);
    basis.psi = t3 * s3 * (2.0 * t - 1.0);
    return basis;
}
const lane_kernel::RowCell& cached_row_cell(int row, std::size_t x, int width_,
                                            const std::vector<Color>& pixels_,
                                            const std::vector<stx_conv::surface::CellShape>& shapes_u_,
                                            std::uint64_t version_) {
    constexpr std::size_t block = 8, slots = 512;
    struct Entry {
        std::uint64_t version;
        std::size_t key;
        lane_kernel::RowCell cells[block];
    };
    static thread_local std::vector<Entry> cache(slots);
    const std::size_t start = x / block * block;
    const std::size_t key = std::size_t(row) * ((width_ + block - 1) / block) + x / block;
    // Stagger rows independently of the image stride, including power-of-two widths.
    Entry& entry = cache[(x / block + std::size_t(row) * 53) % slots];
    if (entry.version != version_ || entry.key != key) {
        std::array<lane_kernel::T, block + 7> samples{};
        for (int k = 0; k < int(block + 7); ++k) {
            const int column = int(start) + k - 3;
            if (column < 0 || column >= width_) {
                continue;
            }
            const Color pixel = pixels_[std::size_t(row) * width_ + column];
            const double a = pixel.a / 255.0;
            samples[k][0] = pixel.r / 255.0 * a;
            samples[k][1] = pixel.g / 255.0 * a;
            samples[k][2] = pixel.b / 255.0 * a;
            samples[k][3] = a;
        }
        const std::size_t count = std::min(block, std::size_t(width_ - 1) - start);
        for (std::size_t k = 0; k < count; ++k) {
            entry.cells[k] = lane_kernel::compile(std::span<const lane_kernel::T, 8>(samples.data() + k, 8),
                                                  shapes_u_[start + k]);
        }
        entry.version = version_;
        entry.key = key;
    }
    return entry.cells[x - start];
}
static lane_kernel::RowCell compact_column(int width, int height, const std::vector<Color>& pixels,
                                           const std::vector<stx_conv::surface::CellShape>& shapes_u,
                                           const std::vector<stx_conv::surface::CellShape>& shapes_v,
                                           std::uint64_t version, std::size_t cell_u, std::size_t cell_v,
                                           const CellBasis& horizontal) {
    std::array<lane_kernel::T, 8> windows{};
    const stx_conv::surface::CellShape& shape = shapes_v[cell_v];
    const int halo =
        shape.left == stx_conv::surface::Jet::interior && shape.right == stx_conv::surface::Jet::interior ? 1
                                                                                                          : 0;
    for (int k = halo; k < 8 - halo; ++k) {
        const int row = int(cell_v) - 3 + k;
        if (row < 0 || row >= height) {
            continue;
        }
        const lane_kernel::RowCell& cell = cached_row_cell(row, cell_u, width, pixels, shapes_u, version);
        lane_kernel::T value = cell.anchor;
        for (int h = 0; h < 5; ++h) {
            value += cell.currents[h] * horizontal.tau[h];
        }
        windows[k] = value;
    }
    return lane_kernel::compile(windows, shapes_v[cell_v]);
}
#endif
double cell_value(const stx_conv::surface::LineCell& cell, const CellBasis& basis) {
    double value = cell.anchor;
    for (int j = 0; j < 5; ++j) {
        value += cell.currents[j] * basis.tau[j];
    }
    value += cell.bubble * basis.psi;
    return value;
}
WarpSample projected(WarpSample value) {
    value.a = std::clamp(value.a, 0.0, 1.0);
    value.r = std::clamp(value.r, 0.0, value.a);
    value.g = std::clamp(value.g, 0.0, value.a);
    value.b = std::clamp(value.b, 0.0, value.a);
    return value;
}
WarpSample cells_at(const stx_conv::surface::LineCell* cells, double t) {
    const CellBasis basis = cell_basis(t);
    const WarpSample value{cell_value(cells[0], basis), cell_value(cells[1], basis), cell_value(cells[2], basis),
                           cell_value(cells[3], basis)};
    return value;
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
    std::mutex mutex;
    std::exception_ptr failure;
};
void render_worker(RenderJob& job) {
    try {
        const int tiles_x = (job.columns + 63) / 64, tiles_y = (job.rows + 31) / 32;
        for (int tile = job.next.fetch_add(1); tile < tiles_x * tiles_y; tile = job.next.fetch_add(1)) {
            const int x0 = (tile % tiles_x) * 64, y0 = (tile / tiles_x) * 32;
            for (int y = y0; y < std::min(y0 + 32, job.rows); ++y) {
                Color* const row = job.target + static_cast<std::size_t>(y) * job.stride;
                const double cy = job.top + y + 0.5;
                for (int x = x0; x < std::min(x0 + 64, job.columns); ++x) {
                    const double cx = job.left + x + 0.5;
                    row[x] = job.surface.sample({job.map.ux * cx + job.map.uy * cy + job.map.u0,
                                                 job.map.vx * cx + job.map.vy * cy + job.map.v0});
                }
            }
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(job.mutex);
        job.failure = std::current_exception();
    }
}
} // namespace

static std::uint64_t next_surface_version() {
    static std::atomic<std::uint64_t> version{1};
    return version.fetch_add(1, std::memory_order_relaxed);
}
std::size_t CanvasSurface::storage_bytes() const noexcept {
    return packed_.capacity() * sizeof(Color) + nodes_.capacity() * sizeof(double) +
           cells_.capacity() * sizeof(stx_conv::surface::LineCell) +
           (shapes_u_.capacity() + shapes_v_.capacity()) * sizeof(stx_conv::surface::CellShape);
}
void CanvasSurface::clear() noexcept {
    version_ = 0;
    std::vector<Color>().swap(packed_);
    ready_ = false;
    width_ = 0;
    height_ = 0;
    std::vector<double>().swap(nodes_);
    std::vector<stx_conv::surface::LineCell>().swap(cells_);
    shapes_u_ = {};
    shapes_v_ = {};
}
void CanvasSurface::build(const Image& source, Storage storage) {
    clear();
    if (source.width < 2 || source.height < 2 ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height) {
        throw std::invalid_argument("The CONV surface needs at least 2 x 2 pixels.");
    }
    if (source.pixels.size() > maximum_pixels) {
        throw std::length_error("A CONV surface holds 4 megapixels at most; use a smaller picture or selection.");
    }
    try {
        width_ = source.width;
        height_ = source.height;
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
        if (storage == Storage::point_tiles && fast_point_tiles) {
            packed_ = source.pixels;
            version_ = next_surface_version();
            ready_ = true;
            return;
        }
        nodes_.resize(source.pixels.size() * channels);
        for (std::size_t i = 0; i < source.pixels.size(); ++i) {
            const Color color = source.pixels[i];
            const double alpha = color.a / 255.0;
            nodes_[i * 4] = color.r / 255.0 * alpha;
            nodes_[i * 4 + 1] = color.g / 255.0 * alpha;
            nodes_[i * 4 + 2] = color.b / 255.0 * alpha;
            nodes_[i * 4 + 3] = alpha;
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
    if (point_tiles()) {
        for (int y = top; y < bottom; ++y) {
            const std::size_t offset = std::size_t(y) * width_ + left;
            std::copy_n(source.pixels.begin() + offset, right - left, packed_.begin() + offset);
        }
        version_ = next_surface_version();
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
#if defined(__clang__) && defined(__aarch64__) && defined(__APPLE__)
    if (point_tiles()) {
        const lane_kernel::RowCell across = compact_column(width_, height_, packed_, shapes_u_, shapes_v_,
                                                           version_, cell_u, cell_v, point_basis(t_u));
        const CellBasis vertical = point_basis(t_v);
        lane_kernel::T value = across.anchor;
        for (int h = 0; h < 5; ++h) {
            value += across.currents[h] * vertical.tau[h];
        }
        return {value[0], value[1], value[2], value[3]};
    }
#endif
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
void CanvasSurface::column_cells(double u, std::size_t cell_v, stx_conv::surface::LineCell* cells) const {
    const std::size_t cells_u = static_cast<std::size_t>(width_) - 1;
    std::size_t cell_u = 0;
    double t_u = 0.0;
    locate(u, cells_u, cell_u, t_u);
    // One basis for the column's 32 row-cell values.
    const CellBasis basis = cell_basis(t_u);
#if defined(__clang__) && defined(__aarch64__) && defined(__APPLE__)
    if (point_tiles()) {
        const lane_kernel::RowCell across =
            compact_column(width_, height_, packed_, shapes_u_, shapes_v_, version_, cell_u, cell_v, basis);
        for (int c = 0; c < 4; ++c) {
            cells[c].anchor = across.anchor[c];
            for (int h = 0; h < 5; ++h) {
                cells[c].currents[h] = across.currents[h][c];
            }
            cells[c].bubble = 0.0;
        }
        return;
    }
#endif
    std::array<std::array<double, 8>, channels> windows{};
    for (int k = 0; k < 8; ++k) {
        const int row = static_cast<int>(cell_v) - 3 + k;
        if (row < 0 || row >= height_) {
            continue;
        }
        const std::size_t first = (static_cast<std::size_t>(row) * cells_u + cell_u) * channels;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            windows[channel][k] = cell_value(cells_[first + channel], basis);
        }
    }
    for (std::size_t channel = 0; channel < channels; ++channel) {
        cells[channel] = stx_conv::surface::compile_line_cell(std::span<const double, 8>(windows[channel]),
                                                              shapes_v_[cell_v], bubble);
    }
}
WarpSample CanvasSurface::integrate_cell_polygon(std::span<const Point> polygon) const {
    WarpSample total;
    const int count = static_cast<int>(polygon.size());
    if (!ready_ || count < 3 || count > 12) {
        return total;
    }
    // The slab boundaries: the vertices' distinct u, in order.
    double cuts[12] = {};
    int cut_count = 0;
    double v_low = polygon[0].y, v_high = polygon[0].y;
    for (int i = 0; i < count; ++i) {
        v_low = std::min(v_low, polygon[i].y);
        v_high = std::max(v_high, polygon[i].y);
        int place = cut_count;
        while (place > 0 && cuts[place - 1] > polygon[i].x) {
            --place;
        }
        if (place > 0 && cuts[place - 1] == polygon[i].x) {
            continue;
        }
        for (int k = cut_count; k > place; --k) {
            cuts[k] = cuts[k - 1];
        }
        cuts[place] = polygon[i].x;
        ++cut_count;
    }
    // The v-cell the polygon lies in; beyond the first or last centre the
    // surface is constant along v (its value at that centre).
    const std::size_t cells_v = static_cast<std::size_t>(height_) - 1;
    const double middle = 0.5 * (v_low + v_high);
    const bool before = middle < 0.0, after = middle > static_cast<double>(cells_v);
    const std::size_t cell_v =
        before ? 0 : after ? cells_v - 1 : std::min(cells_v - 1, static_cast<std::size_t>(std::floor(middle)));
    stx_conv::surface::LineCell cells[channels];
    for (int slab = 0; slab + 1 < cut_count; ++slab) {
        const double left = cuts[slab], span_u = cuts[slab + 1] - cuts[slab];
        for (int column = 0; column < 6; ++column) {
            const double u = left + span_u * gauss_nodes[column];
            // The chord of the convex polygon at u.
            double low = 1e300, high = -1e300;
            for (int i = 0; i < count; ++i) {
                const Point a = polygon[i], b = polygon[(i + 1) % count];
                if (a.x == b.x || u < std::min(a.x, b.x) || u > std::max(a.x, b.x)) {
                    continue;
                }
                const double v = a.y + (u - a.x) / (b.x - a.x) * (b.y - a.y);
                low = std::min(low, v);
                high = std::max(high, v);
            }
            if (!(high > low)) {
                continue;
            }
            column_cells(u, cell_v, cells);
            WarpSample chord;
            if (before || after) {
                const WarpSample value = projected(cells_at(cells, before ? 0.0 : 1.0));
                chord = {value.r * (high - low), value.g * (high - low), value.b * (high - low),
                         value.a * (high - low)};
            } else {
                const double base = static_cast<double>(cell_v);
                const double t0 = std::clamp(low - base, 0.0, 1.0), t1 = std::clamp(high - base, 0.0, 1.0);
                double sums[channels] = {};
                if (projection_free(cells)) {
                    double start[5] = {}, end[5] = {};
                    tau_integrals(t0, start);
                    tau_integrals(t1, end);
                    for (std::size_t channel = 0; channel < channels; ++channel) {
                        double sum = cells[channel].anchor * (t1 - t0);
                        for (int j = 0; j < 5; ++j) {
                            sum += cells[channel].currents[j] * (end[j] - start[j]);
                        }
                        sums[channel] = sum;
                    }
                } else {
                    for (int point = 0; point < 6; ++point) {
                        const WarpSample value = projected(cells_at(cells, t0 + (t1 - t0) * gauss_nodes[point]));
                        const double weight = (t1 - t0) * gauss_weights[point];
                        sums[0] += value.r * weight;
                        sums[1] += value.g * weight;
                        sums[2] += value.b * weight;
                        sums[3] += value.a * weight;
                    }
                }
                chord = {sums[0], sums[1], sums[2], sums[3]};
            }
            const double weight = span_u * gauss_weights[column];
            total.r += chord.r * weight;
            total.g += chord.g * weight;
            total.b += chord.b * weight;
            total.a += chord.a * weight;
        }
    }
    return total;
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
    RenderJob job{*this, map, left, top, columns, rows, target, stride, 0, {}, {}};
    const unsigned count = worker_count(static_cast<std::size_t>(columns) * rows);
    if (count == 1) {
        render_worker(job);
        if (job.failure) {
            std::rethrow_exception(job.failure);
        }
        return;
    }
    std::vector<std::jthread> workers;
    workers.reserve(count);
    for (unsigned index = 0; index < count; ++index) {
        workers.emplace_back(render_worker, std::ref(job));
    }
    for (std::jthread& worker : workers) {
        worker.join();
    }
    if (job.failure) {
        std::rethrow_exception(job.failure);
    }
}
} // namespace paint
