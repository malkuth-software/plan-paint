#include "curve.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace paint {
namespace {
Point mix(Point first, Point last, double t) {
    return {first.x + (last.x - first.x) * t, first.y + (last.y - first.y) * t};
}
double segment_distance(Point point, Point first, Point last) {
    double dx = last.x - first.x, dy = last.y - first.y;
    double length_squared = dx * dx + dy * dy;
    double t =
        length_squared == 0
            ? 0
            : std::clamp(((point.x - first.x) * dx + (point.y - first.y) * dy) / length_squared, 0.0, 1.0);
    Point closest = mix(first, last, t);
    return std::hypot(point.x - closest.x, point.y - closest.y);
}
void flatten_bezier(Point a, Point b, Point c, Point d, double tolerance, int depth,
                    std::vector<Point>& output) {
    // A cubic lies in its control hull. Distance to the chord segment also
    // detects collinear reversals, which distance to the infinite line misses.
    if (depth == 16 || std::max(segment_distance(b, a, d), segment_distance(c, a, d)) <= tolerance) {
        output.push_back(d);
        return;
    }
    Point ab = mix(a, b, 0.5), bc = mix(b, c, 0.5), cd = mix(c, d, 0.5);
    Point abc = mix(ab, bc, 0.5), bcd = mix(bc, cd, 0.5), middle = mix(abc, bcd, 0.5);
    flatten_bezier(a, ab, abc, middle, tolerance, depth + 1, output);
    flatten_bezier(middle, bcd, cd, d, tolerance, depth + 1, output);
}
} // namespace
void CurveGeometry::set_line(Point first, Point last) {
    start = first;
    end = last;
    first_control = mix(first, last, 1.0 / 3.0);
    second_control = mix(first, last, 2.0 / 3.0);
    bulge = 0;
}
int CurveGeometry::handle_count() const {
    return kind == CurveKind::Arc ? 1 : 2;
}
Point CurveGeometry::handle(int index) const {
    return kind == CurveKind::Arc ? at(0.5) : index == 0 ? first_control : second_control;
}
void CurveGeometry::move_handle(int index, Point point) {
    if (kind == CurveKind::Bezier) {
        if (index == 0) {
            first_control = point;
        } else {
            second_control = point;
        }
        return;
    }
    double dx = end.x - start.x, dy = end.y - start.y, length = std::hypot(dx, dy);
    if (length > 0) {
        Point middle = mix(start, end, 0.5);
        // The midpoint of a circular arc stays on the chord's perpendicular bisector.
        bulge = ((point.x - middle.x) * -dy + (point.y - middle.y) * dx) / length;
    }
}
Point CurveGeometry::at(double t) const {
    if (t <= 0) {
        return start;
    }
    if (t >= 1) {
        return end;
    }
    if (kind == CurveKind::Bezier) {
        double u = 1 - t;
        return {u * u * u * start.x + 3 * u * u * t * first_control.x + 3 * u * t * t * second_control.x +
                    t * t * t * end.x,
                u * u * u * start.y + 3 * u * u * t * first_control.y + 3 * u * t * t * second_control.y +
                    t * t * t * end.y};
    }
    double dx = end.x - start.x, dy = end.y - start.y, length = std::hypot(dx, dy);
    if (length == 0 || std::abs(bulge) < 1e-9) {
        return mix(start, end, t);
    }
    // For half-chord a and signed sagitta h: R=(a²+h²)/(2|h|),
    // half-sweep=2 atan2(|h|,a). The handle is exactly the arc's t=1/2 point.
    double a = length / 2, h = std::abs(bulge);
    double radius = (a * a + h * h) / (2 * h);
    double angle = (2 * t - 1) * 2 * std::atan2(h, a);
    double x = radius * std::sin(angle);
    double half_sine = std::sin(angle / 2);
    double y = bulge - std::copysign(2 * radius * half_sine * half_sine, bulge);
    Point middle = mix(start, end, 0.5);
    return {middle.x + (x * dx - y * dy) / length, middle.y + (x * dy + y * dx) / length};
}
std::vector<Point> CurveGeometry::samples(double tolerance) const {
    if (!std::isfinite(tolerance) || tolerance <= 0) {
        throw std::invalid_argument("Curve tolerance must be positive and finite.");
    }
    std::vector<Point> result{start};
    if (kind == CurveKind::Bezier) {
        flatten_bezier(start, first_control, second_control, end, tolerance, 0, result);
    } else {
        double a = std::hypot(end.x - start.x, end.y - start.y) / 2;
        double h = std::abs(bulge);
        int count = 1;
        if (a > 0 && h >= 1e-9) {
            double radius = (a * a + h * h) / (2 * h);
            double sweep = 4 * std::atan2(h, a);
            // R*dtheta²/8 bounds the circular sagitta of each sampled chord.
            count = static_cast<int>(
                std::clamp(std::ceil(sweep * std::sqrt(radius / (8 * tolerance))), 2.0, 65536.0));
            if (count % 2) {
                ++count; // Keep the draggable midpoint among the exact samples.
            }
        }
        for (int i = 1; i <= count; ++i) {
            result.push_back(at(static_cast<double>(i) / count));
        }
    }
    return result;
}
} // namespace paint
