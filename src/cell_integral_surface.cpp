#include "cell_integral_surface.hpp"
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
} // namespace

// Rows claimed one at a time from `next`; each row's cells are written by
// one worker only.
struct CellIntegralBuildJob {
    CellIntegralSurface& surface;
    std::atomic<int> next{0};
    static void run(CellIntegralBuildJob& job) {
        for (;;) {
            const int row = job.next.fetch_add(1, std::memory_order_relaxed);
            if (row >= job.surface.height()) {
                return;
            }
            job.surface.compile_row_cells(row, 0, job.surface.width() - 2);
        }
    }
};
void CellIntegralSurface::clear() noexcept {
    ready_ = false;
    width_ = 0;
    height_ = 0;
    nodes_ = {};
    cells_ = {};
    shapes_u_ = {};
    shapes_v_ = {};
}
void CellIntegralSurface::build(const Image& source) {
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
        CellIntegralBuildJob job{*this};
        const unsigned count = worker_count(cells_.size());
        if (count == 1) {
            CellIntegralBuildJob::run(job);
        } else {
            std::vector<std::jthread> workers;
            workers.reserve(count);
            for (unsigned index = 0; index < count; ++index) {
                workers.emplace_back(CellIntegralBuildJob::run, std::ref(job));
            }
        }
    } catch (...) {
        clear();
        throw;
    }
    ready_ = true;
}
void CellIntegralSurface::compile_row_cells(int row, int first_cell, int last_cell) {
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
WarpSample CellIntegralSurface::sample_components(Point position) const {
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
void CellIntegralSurface::column_cells(double u, std::size_t cell_v, stx_conv::surface::LineCell* cells) const {
    const std::size_t cells_u = static_cast<std::size_t>(width_) - 1;
    std::size_t cell_u = 0;
    double t_u = 0.0;
    locate(u, cells_u, cell_u, t_u);
    // One basis for the column's 32 row-cell values.
    const CellBasis basis = cell_basis(t_u);
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
WarpSample CellIntegralSurface::integrate_cell_polygon(std::span<const Point> polygon) const {
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
WarpSample CellIntegralSurface::sample_premultiplied(Point position) const {
    WarpSample value = sample_components(position);
    value.a = std::clamp(value.a, 0.0, 1.0);
    value.r = std::clamp(value.r, 0.0, value.a);
    value.g = std::clamp(value.g, 0.0, value.a);
    value.b = std::clamp(value.b, 0.0, value.a);
    return value;
}
} // namespace paint
