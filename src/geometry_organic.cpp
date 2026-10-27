#include "geometry_style.hpp"
#include <algorithm>
#include <cmath>
namespace paint {
namespace {
double knot_value(std::uint32_t index, std::uint32_t seed) {
    std::uint32_t value = index * 0x9e3779b9U + seed;
    value = (value ^ (value >> 16)) * 0x7feb352dU;
    value = (value ^ (value >> 15)) * 0x846ca68bU;
    value ^= value >> 16;
    return static_cast<double>(value) / 2147483647.5 - 1;
}
double variation(double position, int knots, bool closed, std::uint32_t seed) {
    const int index = static_cast<int>(std::floor(position));
    const double t = position - index;
    const double smooth = t * t * (3 - 2 * t);
    const int first = closed ? index % knots : index;
    const int next = closed ? (index + 1) % knots : index + 1;
    return knot_value(static_cast<std::uint32_t>(first), seed) * (1 - smooth) +
           knot_value(static_cast<std::uint32_t>(next), seed) * smooth;
}
Point varied(Point point, double along, double total, int knots, double amount, std::uint32_t seed,
             bool closed) {
    double envelope = 1;
    if (!closed) {
        const double t = std::clamp(std::min(along, total - along) / std::min(16.0, total * 0.25), 0.0, 1.0);
        envelope = t * t * (3 - 2 * t);
    }
    const double phase = along / total * knots;
    return {point.x + amount * envelope * variation(phase, knots, closed, seed),
            point.y + amount * envelope * variation(phase, knots, closed, seed ^ 0xa511e9b3U)};
}
} // namespace
std::vector<Point> organic_contour(const std::vector<Point>& points, double amount, std::uint32_t seed,
                                   bool closed) {
    if (!std::isfinite(amount) || amount <= 0 || points.size() < 2) {
        return points;
    }
    const std::size_t edges = points.size() - 1 + (closed && points.size() > 2 ? 1 : 0);
    double total = 0;
    for (std::size_t edge = 0; edge < edges; ++edge) {
        const Point first = points[edge], last = points[(edge + 1) % points.size()];
        total += std::hypot(last.x - first.x, last.y - first.y);
    }
    if (!std::isfinite(total)) {
        return {};
    }
    if (total <= 1e-9) {
        return points;
    }
    const int knots = static_cast<int>(std::clamp(std::ceil(total / 48), 2.0, 65536.0));
    const double spacing = std::max(4.0, total / 65536.0);
    const double amplitude = std::clamp(amount, 0.0, 3.0) * std::min(2.0, total / 12);
    std::vector<Point> result;
    double along = 0;
    for (std::size_t edge = 0; edge < edges; ++edge) {
        const Point first = points[edge], last = points[(edge + 1) % points.size()];
        const double length = std::hypot(last.x - first.x, last.y - first.y);
        if (length <= 1e-9) {
            continue;
        }
        const int samples = static_cast<int>(std::ceil(length / spacing));
        for (int sample = 0; sample < samples; ++sample) {
            const double t = static_cast<double>(sample) / samples;
            const Point point{first.x + (last.x - first.x) * t, first.y + (last.y - first.y) * t};
            result.push_back(varied(point, along + length * t, total, knots, amplitude, seed, closed));
        }
        along += length;
    }
    if (!closed) {
        result.push_back(points.back());
    }
    return result;
}
} // namespace paint
