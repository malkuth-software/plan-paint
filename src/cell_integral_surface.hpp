#pragma once
#include "canvas_surface.hpp"
#include <stx_conv/surface.hpp>
#include <cstddef>
#include <span>
#include <vector>

namespace paint {
// The CONV surface's exact cell integral over a polygon, on stx_conv's tensor
// surface through the image's premultiplied pixels (stx_conv::surface::
// Surface, evaluated the same way). stxlib::conv, which the rotated view
// (CanvasSurface) and the transform tools (ConvWarpField) read, has no such
// integral yet; this is Plan Paint's remaining stx_conv use, kept with its
// test until stxlib::conv offers one.
//
// Each source row holds its admitted u-cells: per cell and channel an anchor
// and five currents (the bubble slot stays zero), compiled from the eight
// pixels x-3..x+4 of that row and nothing else. A sample at (u, v) evaluates
// the eight rows around v at u, compiles one v-cell from those eight values
// and evaluates it at v.
//
// Coordinates are pixel centres: pixel (x, y) is the node (x, y). Inside the
// half-pixel footprint [-0.5, width - 0.5) x [-0.5, height - 0.5) a sample is
// clamped to the nodes; outside it is transparent. Paper closure (the stream
// kernels' ends), without the bubble.
class CellIntegralSurface {
  public:
    // 256 bytes a pixel are held (the cells and the premultiplied pixels).
    static constexpr std::size_t maximum_pixels = 4194304;
    static constexpr stx_conv::surface::Closure closure = stx_conv::surface::Closure::paper;
    // No bubble, as stxlib::conv's field.
    static constexpr stx_conv::surface::Bubble bubble = stx_conv::surface::Bubble::none;

    // Compiles every cell of `source`. Throws invalid_argument below 2 x 2
    // pixels and length_error above maximum_pixels; the surface is then
    // cleared. Rows are compiled on up to eight workers.
    void build(const Image& source);
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
    friend struct CellIntegralBuildJob;
};
} // namespace paint
