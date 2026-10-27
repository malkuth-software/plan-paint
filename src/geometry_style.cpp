#include "geometry_style.hpp"
#include <algorithm>
#include <cmath>
namespace paint {
namespace {
double distance(Point a, Point b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}
} // namespace
std::vector<StrokeSegment> styled_outline(const std::vector<Point>& points, StrokeStyle style, double width,
                                          bool closed) {
    std::vector<StrokeSegment> result;
    if (points.size() < 2 || !std::isfinite(width) || width <= 0) {
        return result;
    }
    width = std::max(1.0, width);
    for (Point point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return {};
        }
    }
    const double period = width * (style == StrokeStyle::Dotted ? 3 : 7);
    const double dash = width * 4;
    double distance_along = 0, next_dot = 0;
    const std::size_t edges = points.size() - 1 + (closed && points.size() > 2 ? 1 : 0);
    for (std::size_t edge = 0; edge < edges; ++edge) {
        const Point first = points[edge], last = points[(edge + 1) % points.size()];
        const double length = distance(first, last);
        if (!std::isfinite(length)) {
            return {};
        }
        if (style == StrokeStyle::Solid) {
            result.push_back({first, last});
        } else if (length > 1e-9) {
            const Point direction{(last.x - first.x) / length, (last.y - first.y) / length};
            if (style == StrokeStyle::Dotted) {
                while (next_dot <= distance_along + length + 1e-9) {
                    const double step = std::clamp(next_dot - distance_along, 0.0, length);
                    const Point dot{first.x + direction.x * step, first.y + direction.y * step};
                    result.push_back({dot, dot});
                    next_dot += period;
                }
            } else {
                double step = 0;
                while (step < length - 1e-9) {
                    const double phase = std::fmod(distance_along + step, period);
                    const bool painted = phase < dash;
                    const double remaining = (painted ? dash : period) - phase;
                    const double end = std::min(length, step + std::max(remaining, 1e-9));
                    if (painted) {
                        result.push_back({{first.x + direction.x * step, first.y + direction.y * step},
                                          {first.x + direction.x * end, first.y + direction.y * end}});
                    }
                    step = end;
                }
            }
        }
        distance_along += length;
    }
    return result;
}
std::vector<Point> soften_corners(const std::vector<Point>& points, double radius, bool closed,
                                  double tolerance) {
    if (!std::isfinite(radius) || radius <= 0 || points.size() < 3) {
        return points;
    }
    radius = std::min(radius, 4096.0);
    tolerance = std::isfinite(tolerance) ? std::clamp(tolerance, 0.001, 1.0) : 0.125;
    std::vector<Point> clean;
    clean.reserve(points.size());
    for (Point point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return {};
        }
        if (clean.empty() || distance(clean.back(), point) > 1e-9) {
            clean.push_back(point);
        }
    }
    if (closed && clean.size() > 1 && distance(clean.front(), clean.back()) < 1e-9) {
        clean.pop_back();
    }
    if (clean.size() < 3) {
        return clean;
    }
    std::vector<Point> result;
    for (std::size_t index = 0; index < clean.size(); ++index) {
        const Point corner = clean[index];
        if (!closed && (index == 0 || index + 1 == clean.size())) {
            result.push_back(corner);
            continue;
        }
        const Point before = clean[(index + clean.size() - 1) % clean.size()],
                    after = clean[(index + 1) % clean.size()];
        const double first = distance(corner, before), second = distance(corner, after);
        if (!std::isfinite(first) || !std::isfinite(second)) {
            return {};
        }
        const Point u{(before.x - corner.x) / first, (before.y - corner.y) / first};
        const Point v{(after.x - corner.x) / second, (after.y - corner.y) / second};
        const double cosine = std::clamp(u.x * v.x + u.y * v.y, -1.0, 1.0);
        // Preserve straight samples, cusps and smooth curves' tiny turns.
        if (cosine < -0.9995 || cosine > 0.9995) {
            result.push_back(corner);
            continue;
        }
        const double half = std::acos(cosine) * 0.5;
        const double cut = std::min(radius / std::tan(half), std::min(first, second) * 0.5);
        const double effective = cut * std::tan(half);
        const double bisector = std::hypot(u.x + v.x, u.y + v.y);
        const double offset = effective / (std::sin(half) * bisector);
        const Point center{corner.x + (u.x + v.x) * offset, corner.y + (u.y + v.y) * offset};
        const Point a{corner.x + u.x * cut, corner.y + u.y * cut},
            b{corner.x + v.x * cut, corner.y + v.y * cut};
        const double ax = a.x - center.x, ay = a.y - center.y, bx = b.x - center.x, by = b.y - center.y;
        const double sweep = std::atan2(ax * by - ay * bx, ax * bx + ay * by);
        const double step = 2 * std::acos(std::clamp(1 - tolerance / effective, -1.0, 1.0));
        const int steps =
            std::clamp(static_cast<int>(std::ceil(std::abs(sweep) / std::max(step, 0.001))), 1, 4096);
        const double start = std::atan2(ay, ax);
        result.push_back(a);
        for (int sample = 1; sample < steps; ++sample) {
            const double angle = start + sweep * sample / steps;
            result.push_back(
                {center.x + effective * std::cos(angle), center.y + effective * std::sin(angle)});
        }
        result.push_back(b);
    }
    return result;
}
} // namespace paint
