#pragma once
#include "image.hpp"
#include <stxlib/conv.hpp>
#include <cstddef>

namespace paint {
// Premultiplied RGBA in [0, 1] (before projection a channel may leave it).
struct WarpSample {
    double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
};
// The rotated canvas's CONV surface: stxlib::conv's field (STXlib, the one
// canonical CONV) through the image's pixels, owned here so that an edit
// recompiles only what it touches.
//
// The field's value is stx_conv's tensor surface without the bubble, bit for
// bit (stxlib::conv::Field::evaluate). Each source row holds its own
// admission, read from that row's pixels only, so a changed pixel changes
// samples only within four pixels of it; update() recompiles the changed
// rows (Field::recompile), and the field afterwards is, byte for byte, what
// build() writes for the same pixels.
//
// Coordinates are pixel centres: pixel (x, y) is the node (x, y). Inside the
// half-pixel footprint [-0.5, width - 0.5) x [-0.5, height - 0.5) a sample
// is the field's value (clamped to the nodes); outside it is transparent.
// The field clamps every position to its nodes, so render() masks the frame
// at the footprint. The rotated canvas view samples it.
class CanvasSurface {
  public:
    // Kept from the stx_conv surface (256 bytes a pixel), so the pictures
    // that get a rotated view do not change; the field holds far less.
    static constexpr std::size_t maximum_pixels = 4194304;

    // Compiles the field of `source`. Throws invalid_argument below 2 x 2
    // pixels and length_error above maximum_pixels or a side over
    // stxlib::conv::max_side; the surface is then cleared.
    void build(const Image& source);
    // Recompiles the rows holding a pixel of `damage` (clipped to the image)
    // from `source`, which has the built size, the built pixels outside
    // `damage` and its new pixels inside. Throws invalid_argument for another
    // size or an unbuilt surface. Should the local recompile be refused, the
    // whole field is compiled again; should that fail too, the surface is
    // cleared and runtime_error thrown.
    void update(const Image& source, Rect damage);
    void clear() noexcept;
    [[nodiscard]] bool ready() const noexcept {
        return field_.ready();
    }
    [[nodiscard]] int width() const noexcept {
        return width_;
    }
    [[nodiscard]] int height() const noexcept {
        return height_;
    }
    // Every channel's field value at a pixel-centre position, before any
    // projection (premultiplied; colour may exceed alpha near overshoot).
    [[nodiscard]] WarpSample sample_components(Point position) const;
    // Physical premultiplied RGBA: 0 <= colour <= alpha <= 1.
    [[nodiscard]] WarpSample sample_premultiplied(Point position) const;
    // Straight-alpha 8-bit: transparent below half a level of opacity, else
    // colour divided by opacity, each rounded to 8 bits.
    [[nodiscard]] Color sample(Point position) const;

    // Frame pixel (x, y) samples the source at
    // u = ux * (x + 0.5) + uy * (y + 0.5) + u0, v likewise.
    struct TargetMap {
        double ux = 1.0, uy = 0.0, u0 = -0.5;
        double vx = 0.0, vy = 1.0, v0 = -0.5;
    };
    // The whole frame: `height` rows of `width` pixels, rows `stride` pixels
    // apart.
    struct Frame {
        Color* pixels = nullptr;
        int width = 0, height = 0;
        std::size_t stride = 0;
    };
    // Fills the frame area {left, top, columns, rows} (inside the frame) with
    // the field's preview transfer (stxlib::conv::transfer, Mode::preview: the
    // field at each frame pixel's centre, clamped and divided, straight RGBA8)
    // and makes every pixel whose centre maps outside the footprint
    // transparent. The map is anchored at the frame's origin, so any area
    // gives the bytes a whole frame gives there. Independent row chunks run
    // on up to eight workers when the area is large. An unbuilt surface or a
    // map that is not finite leaves the area transparent. Throws
    // runtime_error when the field refuses the transfer.
    void render(const TargetMap& map, const Frame& frame, int left, int top, int columns, int rows) const;

  private:
    stxlib::conv::Field field_;
    int width_ = 0, height_ = 0;
};
} // namespace paint
