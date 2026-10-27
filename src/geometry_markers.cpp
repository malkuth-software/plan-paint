#include "geometry_style.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace paint {
namespace {
class MarkerBuilder {
    std::vector<StrokeSegment>& segments_;
    Point origin_, direction_;
    double size_, tolerance_;
    Point point(double x, double y) const {
        return {origin_.x + size_ * (direction_.x * x - direction_.y * y),
                origin_.y + size_ * (direction_.y * x + direction_.x * y)};
    }
    void line(double x1, double y1, double x2, double y2) {
        segments_.push_back({point(x1, y1), point(x2, y2)});
    }
    void ring(double center, double radius) {
        const double step = 2 * std::acos(std::clamp(1 - tolerance_ / (radius * size_), -1.0, 1.0));
        const int samples =
            std::clamp(static_cast<int>(std::ceil(2 * std::numbers::pi / std::max(step, 0.001))), 16, 512);
        for (int sample = 0; sample < samples; ++sample) {
            const double first = 2 * std::numbers::pi * sample / samples;
            const double last = 2 * std::numbers::pi * (sample + 1) / samples;
            line(center + radius * std::cos(first), radius * std::sin(first),
                 center + radius * std::cos(last), radius * std::sin(last));
        }
    }
    void many() {
        line(1, 0, 0, -0.55);
        line(1, 0, 0, 0);
        line(1, 0, 0, 0.55);
    }
    void bar(double x) {
        line(x, -0.5, x, 0.5);
    }

  public:
    MarkerBuilder(std::vector<StrokeSegment>& segments, Point origin, Point direction, double size,
                  double tolerance)
        : segments_(segments), origin_(origin), direction_(direction), size_(size), tolerance_(tolerance) {}
    double append(EndpointMarker marker) {
        double trim = 0;
        switch (marker) {
        case EndpointMarker::None:
            break;
        case EndpointMarker::Arrow:
            line(1, -0.55, 0, 0);
            line(0, 0, 1, 0.55);
            line(0, 0, 1, 0);
            trim = 1;
            break;
        case EndpointMarker::Triangle:
            line(1, -0.55, 0, 0);
            line(0, 0, 1, 0.55);
            line(1, 0.55, 1, -0.55);
            trim = 1;
            break;
        case EndpointMarker::Diamond:
            line(0, 0, 0.5, -0.5);
            line(0.5, -0.5, 1, 0);
            line(1, 0, 0.5, 0.5);
            line(0.5, 0.5, 0, 0);
            trim = 1;
            break;
        case EndpointMarker::Circle:
            ring(0.5, 0.5);
            trim = 1;
            break;
        case EndpointMarker::Bar:
            bar(0);
            line(0, 0, 0.5, 0);
            trim = 0.5;
            break;
        case EndpointMarker::One:
            bar(0);
            bar(0.5);
            line(0, 0, 1, 0);
            trim = 1;
            break;
        case EndpointMarker::Many:
            many();
            trim = 1;
            break;
        case EndpointMarker::ZeroOne:
            bar(0);
            line(0, 0, 0.65, 0);
            ring(1, 0.35);
            trim = 1.35;
            break;
        case EndpointMarker::OneMany:
            many();
            bar(1.5);
            line(1, 0, 1.85, 0);
            trim = 1.85;
            break;
        case EndpointMarker::ZeroMany:
            many();
            line(1, 0, 1.15, 0);
            ring(1.5, 0.35);
            trim = 1.85;
            break;
        }
        return trim * size_;
    }
};
std::vector<Point> trimmed(const std::vector<Point>& points, double begin, double end) {
    std::vector<Point> result;
    double along = 0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        const Point first = points[index - 1], last = points[index];
        const double length = std::hypot(last.x - first.x, last.y - first.y);
        if (length <= 1e-9) {
            continue;
        }
        if (along + length >= begin && along <= end) {
            const double a = std::clamp((begin - along) / length, 0.0, 1.0);
            const double b = std::clamp((end - along) / length, 0.0, 1.0);
            const Point start{first.x + (last.x - first.x) * a, first.y + (last.y - first.y) * a};
            const Point stop{first.x + (last.x - first.x) * b, first.y + (last.y - first.y) * b};
            if (result.empty()) {
                result.push_back(start);
            }
            result.push_back(stop);
        }
        along += length;
        if (along >= end) {
            break;
        }
    }
    return result;
}
} // namespace
std::vector<StrokeSegment> geometry_outline(const std::vector<Point>& points, const GeometryStyle& style,
                                            double width, bool closed, double tolerance) {
    if (closed || (style.start == EndpointMarker::None && style.end == EndpointMarker::None)) {
        return styled_outline(points, style.stroke, width, closed);
    }
    if (points.size() < 2 || !std::isfinite(width) || width <= 0) {
        return {};
    }
    double total = 0;
    Point first_direction{}, last_direction{};
    for (std::size_t index = 1; index < points.size(); ++index) {
        const double dx = points[index].x - points[index - 1].x, dy = points[index].y - points[index - 1].y;
        const double length = std::hypot(dx, dy);
        if (!std::isfinite(length)) {
            return {};
        }
        if (length > 1e-9) {
            if (total == 0) {
                first_direction = {dx / length, dy / length};
            }
            last_direction = {-dx / length, -dy / length};
            total += length;
        }
    }
    if (!std::isfinite(total) || total <= 1e-9) {
        return {};
    }
    const double size = std::min(std::max(6.0, width * 3), total * 0.2);
    tolerance = std::isfinite(tolerance) ? std::clamp(tolerance, 0.001, 1.0) : 0.125;
    std::vector<StrokeSegment> markers;
    MarkerBuilder first(markers, points.front(), first_direction, size, tolerance);
    MarkerBuilder last(markers, points.back(), last_direction, size, tolerance);
    const double begin = first.append(style.start), end = total - last.append(style.end);
    const std::vector<Point> shaft = trimmed(points, begin, end);
    std::vector<StrokeSegment> result = styled_outline(shaft, style.stroke, width, false);
    result.insert(result.end(), markers.begin(), markers.end());
    return result;
}
} // namespace paint
