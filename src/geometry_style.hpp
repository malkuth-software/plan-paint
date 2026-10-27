#pragma once
#include "image.hpp"
namespace paint {
enum class StrokeStyle { Solid, Dashed, Dotted };
enum class EndpointMarker {
    None,
    Arrow,
    Triangle,
    Diamond,
    Circle,
    Bar,
    One,
    Many,
    ZeroOne,
    OneMany,
    ZeroMany
};
inline constexpr int endpoint_marker_count = 11;
struct GeometryStyle {
    double corner_radius = 0;
    StrokeStyle stroke = StrokeStyle::Solid;
    double organic = 0;
    std::uint32_t seed = 1;
    EndpointMarker start = EndpointMarker::None, end = EndpointMarker::None;
};
struct StrokeSegment {
    Point first, last;
};
std::vector<StrokeSegment> geometry_outline(const std::vector<Point>& points, const GeometryStyle& style,
                                            double width, bool closed, double tolerance = 0.125);
// Spacing follows arc length across vertices, with round-cap room in each gap.
// Dots are zero-length segments using the same brush footprint as the outline.
std::vector<StrokeSegment> styled_outline(const std::vector<Point>& points, StrokeStyle style, double width,
                                          bool closed);
// Deterministic smooth variation shared by the edge and body of the shape.
std::vector<Point> organic_contour(const std::vector<Point>& points, double amount, std::uint32_t seed,
                                   bool closed);
// Circular fillets are tangent to both adjacent edges. Each consumes at most
// half of either edge, so large radii cannot reverse short edges or overlap.
std::vector<Point> soften_corners(const std::vector<Point>& points, double radius, bool closed,
                                  double tolerance = 0.125);
} // namespace paint
