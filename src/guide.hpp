#pragma once
#include "curve.hpp"
#include "gradient.hpp"
#include "raster.hpp"
namespace paint {
enum class LassoMode { Free, Tighten, InnerVoid, Wand };
struct SelectionMask {
    Rect bounds;
    std::vector<std::uint8_t> coverage;
    std::vector<Point> outline;
};
SelectionMask tighten_lasso(const Image& image, const std::vector<Point>& polygon, bool inner_void,
                            double tolerance = 0.025);
SelectionMask similar_colors(const Image& image, Point seed, double tolerance = 0.025);
void trim_selection_mask(SelectionMask& mask);
std::vector<std::vector<Point>> mask_contours(const std::vector<std::uint8_t>& mask, int width, int height);
std::vector<Point> mask_outline(const std::vector<std::uint8_t>& mask, int width, int height);
struct GuideSegment {
    std::size_t edge = 0;
    CurveGeometry geometry;
};
struct Guide {
    std::vector<Point> nodes;
    bool closed = false, fill = true;
    double width = 2;
    // A selection-derived body fills enclosed holes while retaining the
    // feathered exterior until a node edit replaces it with the editable polygon.
    SelectionMask selection;
    std::vector<GuideSegment> segments;
    std::vector<Point> curved_boundary;
    const std::vector<Point>& boundary() const;
    void rebuild_boundary();
    void seal_selection_body();
    int swap_segment(std::size_t edge, CurveKind kind);
    void move_node(std::size_t index, Point point);
    void move_handle(int segment, int handle, Point point);
    void clear();
    bool active() const;
    double blocked(int x, int y) const;
    void translate(Point delta);
};
void constrain_paint(Image& image, const Image& base, const Guide& guide, bool preserve_alpha);
struct FloatingSelection;
void stencil_flood(Image& image, Point point, const Ink& ink, const Guide& guide, bool wrap,
                   const FloatingSelection* selection = nullptr, const Gradient* gradient = nullptr);
} // namespace paint
