#pragma once
#include "paint_tools.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace paint {
// Approximate Deluxe Quad: four circular tips joined by concave cutouts.
// The guide-specific radius matches the kit's 60-unit effective rolling length.
// Phase advances continuously through both rolling and endpoint-pivot intervals.
struct QuadPose {
    Point center, contact;
    double rotation;
};
class QuadTrack {
    static constexpr double quarter = std::numbers::pi / 2;
    static constexpr double beta = .2;
    double guide_, radius_, jump_, step_;
    static Point polar(double radius, double angle) {
        return {radius * std::cos(angle), radius * std::sin(angle)};
    }
    static Point turn(Point p, double angle) {
        return {p.x * std::cos(angle) - p.y * std::sin(angle),
                p.x * std::sin(angle) + p.y * std::cos(angle)};
    }

  public:
    explicit QuadTrack(double guide) : guide_(guide), radius_(0), jump_(0), step_(0) {
        if (!std::isfinite(guide) || guide < 96) {
            throw std::invalid_argument("Quad requires an inner circular ring of at least 96 units");
        }
        double low = 0, high = 70;
        for (int i = 0; i < 56; ++i) {
            const double r = (low + high) / 2;
            const double length = 2 * r * beta + 2 * guide * std::asin(r * std::sin(quarter / 2 - beta) / guide);
            if (length < 60 * quarter) {
                low = r;
            } else {
                high = r;
            }
        }
        radius_ = (low + high) / 2;
        jump_ = 2 * std::asin(radius_ * std::sin(quarter / 2 - beta) / guide);
        step_ = 60 * quarter / guide;
    }
    double radius() const { return radius_; }
    // Boundary parameter, not a support-normal parameter: includes the empty cutouts.
    static Point boundary(double phase) {
        const double index = std::floor(phase / quarter);
        const double local = phase - index * quarter;
        Point p{};
        if (local <= 2 * beta) {
            p = polar(1, local - beta);
        } else {
            const double x = std::cos(beta), y = std::sin(beta);
            const double a = -quarter - quarter * (local - 2 * beta) / (quarter - 2 * beta);
            p = {x + (x - y) * std::cos(a), x + (x - y) * std::sin(a)};
        }
        return turn(p, index * quarter);
    }
    QuadPose pose(double phase, double offset = 0) const {
        const double progress = phase * guide_ / 60 + offset;
        const double index = std::floor(progress / quarter);
        const double local = progress - index * quarter;
        const double base = index * step_, qbase = index * quarter;
        const double global = -offset * 60 / guide_;
        Point center{}, contact{};
        double rotation = 0;
        if (local <= 2 * beta) {
            const double theta = base + radius_ * local / guide_;
            rotation = theta - qbase + beta - local;
            center = polar(guide_ - radius_, theta);
            contact = polar(guide_, theta);
        } else {
            const double theta = base + 2 * radius_ * beta / guide_;
            const double fraction = (local - 2 * beta) / (quarter - 2 * beta);
            const double start = theta - qbase - beta;
            const double finish = theta + jump_ - qbase - quarter + beta;
            rotation = start + (finish - start) * fraction;
            const bool incoming = fraction >= .5;
            contact = polar(guide_, theta + (incoming ? jump_ : 0));
            const Point tip = polar(radius_, qbase + (incoming ? quarter - beta : beta) + rotation);
            center = {contact.x - tip.x, contact.y - tip.y};
        }
        return {turn(center, global), turn(contact, global), rotation + global};
    }
    // Rotation decreases strictly with offset, including endpoint pivots.
    double offset_for_rotation(double phase, double rotation) const {
        double low = -rotation - phase * guide_ / 60 + phase - 4 * quarter;
        double high = low + 8 * quarter;
        for (int i = 0; i < 60; ++i) {
            const double middle = (low + high) / 2;
            if (pose(phase, middle).rotation > rotation) {
                low = middle;
            } else {
                high = middle;
            }
        }
        return (low + high) / 2;
    }
};
} // namespace paint
