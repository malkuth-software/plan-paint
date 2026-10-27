#pragma once
#include "image.hpp"
namespace paint {
enum class CurveKind { Bezier, Arc };
struct CurveGeometry {
    CurveKind kind = CurveKind::Bezier;
    Point start, end, first_control, second_control;
    double bulge = 0;
    void set_line(Point first, Point last);
    int handle_count() const;
    Point handle(int index) const;
    void move_handle(int index, Point point);
    Point at(double t) const;
    std::vector<Point> samples(double tolerance = 0.125) const;
};
} // namespace paint
