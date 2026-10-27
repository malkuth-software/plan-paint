#pragma once
#include "raster.hpp"
#include <array>
#include <unordered_map>

namespace paint {
// Procedural deposition over a stable paper surface. No image assets or fluid solver.
class MaterialSurface {
    Ink ink_;
    Brush brush_;
    double cosine_ = 1, sine_ = 0;
    std::unique_ptr<MaterialSurface> alternate_;
    RasterSpace space_;
    Color sample_primary(int x, int y, double edge_distance) const;

  public:
    MaterialSurface(const Ink& ink, Brush brush, RasterSpace space = {});
    Color sample(int x, int y, double edge_distance) const;
};
bool textured_brush(Brush brush);
struct PixelCoverage {
    std::array<std::uint64_t, 16> bits{};
    int count() const;
    bool full() const;
};
struct FillBoundaryPixel {
    PixelCoverage coverage;
    Color original, paint;
};
using FillBoundary = std::unordered_map<int, FillBoundaryPixel>;
class MaterialStroke {
    struct Pixel {
        Color original;
        double depth = -1e20;
        PixelCoverage coverage;
    };
    std::unordered_map<int, Pixel> pixels_;
    const FillBoundary* fill_boundary_ = nullptr;

  public:
    explicit MaterialStroke(const FillBoundary* boundary = nullptr) : fill_boundary_(boundary) {}
    void clear();
    // Each gesture deposits one coat over the union of its swept brush footprint.
    // Revisiting a pixel within that coat does not multiply its opacity by event count.
    void segment(Image& image, Point start, Point end, const Ink& ink, RasterSpace space = {});
};
void material_fill(Image& image, const std::vector<Point>& polygon, const Ink& ink, Brush brush,
                   FillBoundary* boundary = nullptr, RasterSpace space = {});
} // namespace paint
