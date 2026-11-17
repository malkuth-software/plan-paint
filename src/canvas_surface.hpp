#pragma once
#include "image.hpp"
#include <stx_conv/surface.hpp>
#include <cstddef>
#include <span>
#include <vector>

namespace paint {
// Premultiplied RGBA in [0, 1] (before projection a channel may leave it).
struct WarpSample {
    double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
};
// The rotated canvas's CONV surface: stx_conv's tensor surface through the
// image's premultiplied pixels (stx_conv::surface::Surface, evaluated the
// same way), owned here so that an edit recompiles only what it touches.
//
// Each source row holds its admitted u-cells: per cell and channel an
// anchor and five currents (the bubble slot stays zero), compiled from the
// eight pixels x-3..x+4 of that row and nothing else. A sample at (u, v) evaluates the
// eight rows around v at u, compiles one v-cell from those eight values and
// evaluates it at v. A changed pixel (x, y) therefore changes only row y's
// cells x-4..x+3, and samples only within four pixels of it; update()
// recompiles exactly those cells, and every cell equals what build() writes
// for the same pixels.
//
// Coordinates are pixel centres: pixel (x, y) is the node (x, y). Inside the
// half-pixel footprint [-0.5, width - 0.5) x [-0.5, height - 0.5) a sample
// is clamped to the nodes; outside it is transparent. Paper closure (the
// stream kernels' ends), without the bubble. The rotated canvas view
// and the transform tools (ConvWarpField) sample it.
class CanvasSurface {
  public:
    // 256 bytes a pixel are held (the cells and the premultiplied pixels).
    static constexpr std::size_t maximum_pixels = 4194304;
    static constexpr stx_conv::surface::Closure closure = stx_conv::surface::Closure::paper;
    // No bubble: it changes few pixels and costs about a tenth more per sample.
    static constexpr stx_conv::surface::Bubble bubble = stx_conv::surface::Bubble::none;

    // Compiles every cell of `source`. Throws invalid_argument below 2 x 2
    // pixels and length_error above maximum_pixels; the surface is then
    // cleared. Rows are compiled on up to eight workers.
    void build(const Image& source);
    // Recompiles the cells that read a pixel of `damage` (clipped to the
    // image) from `source`, which has the built size and its new pixels.
    void update(const Image& source, Rect damage);
    void clear() noexcept;
    [[nodiscard]] bool ready() const noexcept {
        return ready_;
    }
    [[nodiscard]] int width() const noexcept {
        return width_;
    }
    [[nodiscard]] int height() const noexcept {
        return height_;
    }
    // Every channel's surface value at a pixel-centre position, before any
    // projection (premultiplied; colour may exceed alpha near overshoot).
    [[nodiscard]] WarpSample sample_components(Point position) const;
    // Physical premultiplied RGBA: 0 <= colour <= alpha <= 1.
    [[nodiscard]] WarpSample sample_premultiplied(Point position) const;
    // The integral over a convex polygon (pixel-centre coordinates, at most
    // twelve vertices) of the physical premultiplied surface: each value is
    // projected before it is integrated. The polygon lies in one evaluation
    // cell: between neighbouring pixel centres, or in a half pixel beyond the
    // first or last centre, along each axis. It is cut into vertical slabs at
    // its vertices; six Gauss-Legendre columns per slab each compile one
    // v-cell, a quintic in v. Where that cell's partial sums keep it inside
    // the colour domain (then projection changes nothing in the cell) its
    // chord is integrated in closed form, and the whole is exact in real
    // arithmetic while the admission keeps its branch across the slab;
    // elsewhere six projected Gauss points integrate the chord.
    [[nodiscard]] WarpSample integrate_cell_polygon(std::span<const Point> polygon) const;
    // Straight-alpha 8-bit: transparent below half a level of opacity, else
    // colour divided by opacity, each rounded to 8 bits.
    [[nodiscard]] Color sample(Point position) const;

    // Frame pixel (x, y) samples the source at
    // u = ux * (x + 0.5) + uy * (y + 0.5) + u0, v likewise.
    struct TargetMap {
        double ux = 1.0, uy = 0.0, u0 = -0.5;
        double vx = 0.0, vy = 1.0, v0 = -0.5;
    };
    // Fills the frame area {left, top, columns, rows} with sample() at each
    // frame pixel's centre. `target` is the area's first pixel and rows are
    // `stride` pixels apart. A pixel's coordinates are computed from its
    // frame position, so any area gives the bytes a whole frame gives there.
    // Independent rows run on up to eight workers when the area is large.
    void render(const TargetMap& map, int left, int top, int columns, int rows, Color* target,
                std::size_t stride) const;

  private:
    void compile_row_cells(int row, int first_cell, int last_cell);
    // The v-cells across the eight rows around v-cell `cell_v`, at u
    // (clamped to the nodes), one per channel.
    void column_cells(double u, std::size_t cell_v, stx_conv::surface::LineCell* cells) const;
    int width_ = 0, height_ = 0;
    bool ready_ = false;
    // Premultiplied normalized pixels, [y][x][channel].
    std::vector<double> nodes_;
    // [(y * (width - 1) + cell) * 4 + channel].
    std::vector<stx_conv::surface::LineCell> cells_;
    std::vector<stx_conv::surface::CellShape> shapes_u_, shapes_v_;
    friend struct CanvasSurfaceBuildJob;
};
} // namespace paint
