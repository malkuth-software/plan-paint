#pragma once
#include "image.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

namespace paint {
// Source-to-target map of pixel-centre coordinates. Identity preserves samples.
struct AffineMap {
    double xx = 1.0, xy = 0.0, tx = 0.0;
    double yx = 0.0, yy = 1.0, ty = 0.0;
};
struct WarpSample {
    double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
};

// Owns an immutable double-precision, shared tensor-quintic CONV* atlas.
// Compilation allocates ~800 bytes/source pixel plus temporary work storage.
// Inputs are capped at 1,048,576 padded source pixels before allocation.
// Compile once per selected material; reuse for every rotation/mesh preview.
// compile() prepares a replacement before publishing; old state survives failure.
class ConvWarpField {
  public:
    void compile(const Image& source, const std::atomic<bool>* cancelled = nullptr);
    // Independently admitted CONV channels, before physical color projection.
    // Raw RGB can exceed alpha; use only for numerical inspection/reference work.
    [[nodiscard]] WarpSample sample_components(Point source_position) const;
    // Physical premultiplied RGBA: 0 <= RGB <= alpha <= 1, projected before filtering.
    [[nodiscard]] WarpSample sample_premultiplied(Point source_position) const;
    [[nodiscard]] Color sample(Point source_position) const;
    [[nodiscard]] int width() const {
        return width_;
    }
    [[nodiscard]] int height() const {
        return height_;
    }
    [[nodiscard]] std::size_t storage_bytes() const;

  private:
    int width_ = 0, height_ = 0, lattice_width_ = 0;
    std::vector<double> controls_;
    bool constant_ = false;
    WarpSample constant_value_;
};

enum class WarpSampling { Point, Minification, Area };

// Point preserves nodal samples. Minification uses a continuous residual filter:
// target-square side sqrt(max(0, 1 - 1 / sigma_max(inverse)^2)), so identity,
// translations and quarter rotations remain exact. Area always uses the full
// unit target square, including at identity. Affine Area clips footprints at
// source-cell boundaries and uses positive triangle quadrature; physical color
// projection is piecewise polynomial, so its integral remains a numerical approximation.
// Mesh Area additionally clips at triangle boundaries. Minification uses an 8x8 stencil.
// Transparent outside the source footprint; edge centres extend half a pixel.
// All output pixels are replaced. Invalid maps leave destination unchanged.
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
// Mesh geometry is piecewise affine. Pixel values always use the CONV* atlas.
// Minification uses the maximum triangle stretch for one common filter, keeping
// shared coverage samples consistent. A local deformation can widen that filter
// smoothly across the mesh. An equivalent whole-affine mesh uses the same filter.
void render_mesh(const ConvWarpField& field, const ReshapeMesh& mesh, int width, int height,
                 Image& destination, WarpSampling sampling = WarpSampling::Minification);
} // namespace paint
