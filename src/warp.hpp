#pragma once
#include "canvas_surface.hpp"
#include "image.hpp"
#include <stxlib/conv.hpp>
#include <array>
#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

namespace paint {
// Source-to-target map of pixel-centre coordinates. Identity preserves samples.
struct AffineMap {
    double xx = 1.0, xy = 0.0, tx = 0.0;
    double yx = 0.0, yy = 1.0, ty = 0.0;
};

// The CONV field of a transform's material: stxlib::conv's field (the
// paper's Surface in its separated form, paper closure, no bubble) through
// its pixels, twice: the material itself, for point samples, and the
// material on a transparent margin (the ground), which the renders transfer
// from. A few bytes a pixel each. A constant material is sampled as its one
// colour; a material one pixel wide or high is its line, constant across.
// Up to 32764 pixels a side. Compile once per selected material; reuse for
// every rotation/mesh preview. compile() prepares a replacement before
// publishing; old state survives failure. Move-only (the fields are).
class ConvWarpField {
  public:
    void compile(const Image& source, const std::atomic<bool>* cancelled = nullptr);
    // Independently admitted CONV channels, before physical color projection
    // (stxlib::conv::Field::evaluate). Raw RGB can exceed alpha; use only for
    // numerical inspection/reference work. Transparent outside the
    // half-pixel footprint.
    [[nodiscard]] WarpSample sample_components(Point source_position) const;
    // Physical premultiplied RGBA: 0 <= RGB <= alpha <= 1.
    [[nodiscard]] WarpSample sample_premultiplied(Point source_position) const;
    [[nodiscard]] Color sample(Point source_position) const;
    [[nodiscard]] int width() const {
        return width_;
    }
    [[nodiscard]] int height() const {
        return height_;
    }
    [[nodiscard]] std::size_t storage_bytes() const;
    // The ground's field: the material at node (x + 2, y + 2) of a copy with
    // a transparent margin of two pixels. Empty before compile().
    [[nodiscard]] const stxlib::conv::Field& ground() const {
        return ground_;
    }

  private:
    int width_ = 0, height_ = 0;
    // The material's field (a one-pixel axis repeated to two); empty for a
    // constant material.
    stxlib::conv::Field material_;
    stxlib::conv::Field ground_;
    bool constant_ = false;
    WarpSample constant_value_;
};

enum class WarpSampling { Point, Minification, Area };

// Renders are stxlib::conv transfers from the ground. Point is the preview
// (the field at each destination pixel's pulled-back centre), so identity,
// translations and quarter rotations keep their samples. Area is the canon
// (the field's average over each destination pixel's pulled-back unit
// square, including at identity). Minification is the canon where the map
// minifies (largest source footprint above one pixel, beyond rounding, so an
// unmoved mesh is not minifying) and the preview elsewhere. A transfer clamps each value to [0, alpha] after it is formed.
// Outside the material the ground is transparent; the material's edge falls
// to it across one pixel. All output pixels are replaced. Invalid maps leave
// destination unchanged.
// affine_bounds includes every target pixel basin intersecting the transformed
// source footprint. Subtract returned x/y from map.tx/map.ty before rendering.
[[nodiscard]] Rect affine_bounds(const ConvWarpField& field, const AffineMap& source_to_target);
void render_affine(const ConvWarpField& field, const AffineMap& source_to_target, int width, int height,
                   Image& destination, WarpSampling sampling = WarpSampling::Minification,
                   const std::atomic<bool>* cancelled = nullptr);

struct MeshNode {
    Point source;
    Point target;
    bool boundary = false;
};
struct MeshTriangle {
    std::array<std::size_t, 3> nodes = {};
};
struct ReshapeMesh {
    std::vector<MeshNode> nodes;
    std::vector<MeshTriangle> triangles;
};

// A simple lasso outline in local image coordinates. Counterclockwise and
// clockwise input are accepted; self-intersections and zero area are rejected.
// Boundary edges are subdivided and interior points inserted at spacing.
[[nodiscard]] ReshapeMesh make_reshape_mesh(const std::vector<Point>& outline, double spacing);
// Rectangle helper; use the polygon overload for the actual lasso outline.
[[nodiscard]] ReshapeMesh make_reshape_mesh(const Image& source, double spacing);
// Rejects inverted/near-degenerate triangles and intersecting boundary edges.
// Mesh topology must come from make_reshape_mesh; only target nodes may change.
// Failure preserves the previous target point.
[[nodiscard]] bool move_reshape_node(ReshapeMesh& mesh, std::size_t node, Point target);
[[nodiscard]] bool reshape_mesh_valid(const ReshapeMesh& mesh);
// Mesh geometry is piecewise affine, carried as stxlib::conv's MeshMap: a
// regular destination grid (one pixel apart, wider past 4M nodes) from
// (-1, -1), so it covers every pixel's whole square (the canon averages a
// pixel over the part the grid covers), whose nodes take the source position
// of the target triangle that holds them, or, past the mesh, of the nearest
// one's map continued onto the ground. Minification takes the canon when any
// triangle minifies.
void render_mesh(const ConvWarpField& field, const ReshapeMesh& mesh, int width, int height,
                 Image& destination, WarpSampling sampling = WarpSampling::Minification);
} // namespace paint
