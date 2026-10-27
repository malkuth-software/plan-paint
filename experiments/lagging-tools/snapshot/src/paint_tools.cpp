#include "paint_tools.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
namespace paint {
namespace {
constexpr double tau = 2 * std::numbers::pi;
std::uint8_t byte(double value) {
    return static_cast<std::uint8_t>(std::clamp(std::round(value), 0.0, 255.0));
}
double random_unit(int x, int y, std::uint32_t seed) {
    std::uint32_t n = static_cast<std::uint32_t>(x) * 374761393U +
                      static_cast<std::uint32_t>(y) * 668265263U + seed * 1274126177U;
    n = (n ^ (n >> 13)) * 1274126177U;
    return ((n ^ (n >> 16)) >> 8) / 16777216.0;
}
int wrap_index(int value, int extent) {
    return (value % extent + extent) % extent;
}
Color bounded_pixel(const Image& image, int x, int y, bool wrap) {
    if (image.width < 1 || image.height < 1) {
        return {0, 0, 0, 0};
    }
    return image.get(wrap ? wrap_index(x, image.width) : std::clamp(x, 0, image.width - 1),
                     wrap ? wrap_index(y, image.height) : std::clamp(y, 0, image.height - 1));
}
Lab local_mean(const Image& image, int x, int y, int radius, bool wrap) {
    Lab sum;
    double mass = 0;
    // A fixed Gaussian quadrature footprint avoids cost growing quadratically
    // with large brush diameters while retaining a broad low-frequency field.
    for (int row = -3; row <= 3; ++row) {
        for (int column = -3; column <= 3; ++column) {
            const Color color = bounded_pixel(image, x + column * radius / 3, y + row * radius / 3, wrap);
            const Lab lab = to_oklab(color);
            const double weight = std::exp(-(row * row + column * column) / 5.0) * color.a / 255.0;
            sum.l += lab.l * weight;
            sum.a += lab.a * weight;
            sum.b += lab.b * weight;
            mass += weight;
        }
    }
    return mass > 0 ? Lab{sum.l / mass, sum.a / mass, sum.b / mass} : Lab{};
}
double segment_distance(Point point, Point start, Point end) {
    const double dx = end.x - start.x, dy = end.y - start.y, length = dx * dx + dy * dy;
    const double t =
        length > 1e-12 ? std::clamp(((point.x - start.x) * dx + (point.y - start.y) * dy) / length, 0.0, 1.0)
                       : 0;
    return std::hypot(point.x - start.x - t * dx, point.y - start.y - t * dy);
}
double soft_mask(double distance, double radius, double hardness) {
    if (hardness >= 0.999) {
        return std::clamp(radius + 0.5 - distance, 0.0, 1.0);
    }
    const double t = std::clamp((radius - distance) / std::max(0.5, radius * (1 - hardness)), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
struct Support {
    double mean = 0, signed_mean = 0, spread = 0, cancellation = 0;
};
Support support_at(const Image& image, int x, int y, int dx, int dy, int reach, bool wrap) {
    std::array<double, 5> current{};
    double mass = 0, delta = 0;
    for (int k = 0; k < 5; ++k) {
        const int offset = (k - 2) * reach;
        const double a = to_oklab(bounded_pixel(image, x + offset * dx, y + offset * dy, wrap)).l;
        const double b =
            to_oklab(bounded_pixel(image, x + (offset + reach) * dx, y + (offset + reach) * dy, wrap)).l;
        current[k] = b - a;
        mass += std::abs(current[k]);
        delta += current[k];
    }
    Support result;
    if (mass < 1e-9) {
        return result;
    }
    for (int k = 0; k < 5; ++k) {
        const double position = -1 + k * 0.5;
        result.mean += position * std::abs(current[k]) / mass;
        result.signed_mean += position * current[k] / mass;
    }
    for (int k = 0; k < 5; ++k) {
        const double centered = -1 + k * 0.5 - result.mean;
        result.spread += centered * centered * std::abs(current[k]) / mass;
    }
    result.spread = std::sqrt(result.spread);
    result.cancellation = std::clamp(1 - std::abs(delta) / mass, 0.0, 1.0);
    return result;
}
Color mix_pixel(const Image& image, int x, int y, Point origin, Point motion, MixEffect effect, double scale,
                double phase, bool wrap) {
    const double u = (x - origin.x) / scale, v = (y - origin.y) / scale;
    double dx = 0, dy = 0, hue = 0, chroma = 0, lightness = 0, fold = 0;
    if (effect == MixEffect::Blur || effect == MixEffect::Sharpen) {
        const Color original = image.get(x, y);
        const Lab blurred = local_mean(image, x, y, std::max(1, static_cast<int>(scale / 10)), wrap);
        Lab lab = blurred;
        if (effect == MixEffect::Sharpen) {
            const Lab source = to_oklab(original);
            lab = {source.l + (source.l - blurred.l) * 1.6, source.a + (source.a - blurred.a) * 0.4,
                   source.b + (source.b - blurred.b) * 0.4};
        }
        Color result = from_oklab(lab);
        result.a = original.a;
        return result;
    }
    if (effect == MixEffect::Smudge) {
        return sample_bilinear(image, x - motion.x * 0.6, y - motion.y * 0.6, wrap);
    }
    if (effect == MixEffect::Ripple) {
        const double radius = std::hypot(u, v);
        const double wave = std::sin(radius * tau + phase) * scale * 0.12;
        dx = radius > 1e-9 ? u / radius * wave : 0;
        dy = radius > 1e-9 ? v / radius * wave : 0;
    } else if (effect == MixEffect::Glass) {
        dx = (0.5 - (u - std::floor(u))) * scale * 0.75;
        dy = (0.5 - (v - std::floor(v))) * scale * 0.75;
    } else {
        // Adapted transport laws from CONV* Support Toys 0.4.1. Here the
        // signed support is measured locally along the brush's image axes.
        const int reach = std::max(1, static_cast<int>(scale / 8));
        const Support h = support_at(image, x, y, 1, 0, reach, wrap);
        const Support vertical = support_at(image, x, y, 0, 1, reach, wrap);
        const double spread = (h.spread + vertical.spread) * 0.5;
        const double cancel = (h.cancellation + vertical.cancellation) * 0.5;
        const double norm = std::max(0.18, std::hypot(u, v)), rx = u / norm, ry = v / norm;
        const double bx = h.mean + 0.34 * (h.spread - 0.42) * std::sin(tau * v + 0.7 * phase);
        const double by = vertical.mean + 0.34 * (vertical.spread - 0.42) * std::sin(tau * u - 0.9 * phase);
        const double tx = -by, ty = bx;
        if (effect == MixEffect::Counterflow) {
            const double interference =
                std::sin(tau * (4 * u - 3 * v) + phase + 5 * (h.signed_mean - vertical.signed_mean));
            dx = (1 + 3 * cancel) * h.signed_mean - 2.2 * cancel * tx + 0.45 * interference * rx;
            dy = (1 + 3 * cancel) * vertical.signed_mean - 2.2 * cancel * ty + 0.45 * interference * ry;
            hue = 0.23 * cancel * interference;
            chroma = 0.18 * cancel * std::cos(phase + tau * u);
            lightness = -0.20 * cancel * interference;
            fold = 0.42 * cancel * std::sin(phase + tau * (u + v));
        } else if (effect == MixEffect::Braid) {
            const double braid = std::sin(tau * (3 * u - 2 * v) + 1.4 * phase);
            dx = braid * bx + (1 - std::abs(braid)) * tx + 0.42 * rx * cancel;
            dy = braid * by + (1 - std::abs(braid)) * ty + 0.42 * ry * cancel;
        } else if (effect == MixEffect::SupportLens) {
            const double lens = std::sin(phase + tau * norm * 1.8);
            dx = 0.55 * bx + rx * lens * (0.55 + spread + cancel);
            dy = 0.55 * by + ry * lens * (0.55 + spread + cancel);
        } else if (effect == MixEffect::Rooms) {
            const double a = std::sin(phase + 1.7 * std::floor(u * 2) + 2.3 * std::floor(v * 2));
            const double b = std::cos(0.71 * phase - 2.1 * std::floor(u * 2) + 1.3 * std::floor(v * 2));
            dx = 0.35 * bx + a * (0.45 + h.spread) + b * tx;
            dy = 0.35 * by + b * (0.45 + vertical.spread) + a * ty;
        } else if (effect == MixEffect::Holonomy) {
            const double solid_angle =
                tau * (u * v + 0.35 * spread) + 3 * h.signed_mean * vertical.signed_mean;
            const double angle = phase + solid_angle + std::atan2(by, bx);
            dx = std::cos(angle) * bx - std::sin(angle) * by + (0.45 + cancel) * std::sin(angle) * rx;
            dy = std::sin(angle) * bx + std::cos(angle) * by - (0.45 + cancel) * std::sin(angle) * ry;
            hue = 0.20 * std::sin(solid_angle);
            chroma = 0.13 * std::cos(angle);
            lightness = 0.19 * std::sin(angle);
            fold = 0.38 * std::cos(solid_angle);
        }
        dx *= scale * 0.18;
        dy *= scale * 0.18;
    }
    Color result = sample_bilinear(image, x + dx, y + dy, wrap);
    if (hue != 0 || lightness != 0) {
        Lab lab = to_oklab(result);
        const double c = std::cos(hue * tau), s = std::sin(hue * tau), gain = std::exp(chroma * 2);
        const double a = gain * (c * lab.a - s * lab.b), b = gain * (s * lab.a + c * lab.b);
        if (effect == MixEffect::Counterflow) {
            lab.l += lightness + 0.30 * fold * std::sin(tau * (lab.l + hue));
        } else if (effect == MixEffect::Holonomy) {
            lab.l = 0.5 + (lab.l - 0.5) * c + lightness + 0.20 * fold;
        } else {
            lab.l += lightness + 0.36 * fold * std::sin(2 * tau * lab.l + hue * tau);
            lab.l -= std::floor(lab.l);
        }
        Color changed = from_oklab({std::clamp(lab.l, 0.0, 1.0), a, b});
        changed.a = result.a;
        result = changed;
    }
    return result;
}
} // namespace
const char* mix_effect_name(MixEffect effect) {
    static const std::array<const char*, 10> names{"Ripples",       "Glass tile",   "Wigner counterflow",
                                                   "Support braid", "Support lens", "Moving rooms",
                                                   "Holonomy",      "Blur",         "Sharpen",
                                                   "Smudge"};
    return names.at(static_cast<std::size_t>(effect));
}
void StrokeStateTracker::reset(Point point, double noise_pixels, double momentum) {
    anchor_ = filtered_ = point;
    last_time_ = cell_time_ = 0;
    noise_pixels = std::isfinite(noise_pixels) ? std::clamp(noise_pixels, 0.1, 20.0) : 1.5;
    variance_ = noise_pixels * noise_pixels;
    momentum_ = std::isfinite(momentum) ? std::clamp(momentum, 0.0, 100.0) : 0;
    // Preserve the broad constant-velocity prior, but reduce the local current's
    // variance and process noise together. Established motion keeps its momentum;
    // isolated deviations have less authority to bend it. Zero preserves the
    // original tracker exactly. This changes the prior, not just the output gain.
    const double local_variance = 40000 * std::exp(-0.10 * momentum_);
    // Paper's midpoint-cell Matérn-3/2 realization, retuned from metres/seconds
    // to pointer pixels/seconds. Fixed 240 Hz cells extend for the whole stroke.
    // No history, dense oracle, or finite-horizon transition table is retained.
    constexpr double h = 1.0 / 240;
    constexpr double lengths[5] = {0.03, 0.07, 0.15, 0.3, 0.6};
    constexpr double velocity_variance = 40000;
    for (int k = 0; k < 5; ++k) {
        Branch& branch = branches_[k];
        branch = {};
        const double a = std::sqrt(3.0) * h / lengths[k];
        const double e = std::exp(-a);
        branch.transition[0][0] = branch.transition[1][1] = 1;
        branch.transition[0][1] = branch.transition[0][2] = h;
        branch.transition[2][2] = e * (1 + a);
        branch.transition[2][3] = e * a;
        branch.transition[3][2] = -e * a;
        branch.transition[3][3] = e * (1 - a);
        branch.covariance[0][0] = variance_;
        for (int i = 1; i < 4; ++i) {
            branch.covariance[i][i] = i == 1 ? velocity_variance : local_variance;
        }
        for (int i = 2; i < 4; ++i) {
            for (int j = 2; j < 4; ++j) {
                double product = 0;
                for (int d = 2; d < 4; ++d) {
                    product += branch.transition[i][d] * branch.transition[j][d];
                }
                branch.noise[i][j] = local_variance * ((i == j ? 1.0 : 0.0) - product);
            }
        }
    }
}
Point StrokeStateTracker::advance(Point point, double seconds) {
    if (!std::isfinite(seconds) || !std::isfinite(point.x) || !std::isfinite(point.y) ||
        seconds <= last_time_) {
        return filtered_;
    }
    // A long delivery gap provides no evidence of continued hand motion.
    // Re-anchor instead of extrapolating a stale velocity across a pause.
    if (seconds - last_time_ > 0.5) {
        reset(point, std::sqrt(variance_), momentum_);
        last_time_ = cell_time_ = seconds;
        return filtered_;
    }
    constexpr double h = 1.0 / 240;
    while (cell_time_ + h <= seconds) {
        for (Branch& branch : branches_) {
            double mean[4][2] = {}, temp[4][4] = {}, covariance[4][4] = {};
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    for (int d = 0; d < 2; ++d) {
                        mean[i][d] += branch.transition[i][j] * branch.mean[j][d];
                    }
                    for (int d = 0; d < 4; ++d) {
                        temp[i][d] += branch.transition[i][j] * branch.covariance[j][d];
                    }
                }
            }
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    covariance[i][j] = branch.noise[i][j];
                    for (int d = 0; d < 4; ++d) {
                        covariance[i][j] += temp[i][d] * branch.transition[j][d];
                    }
                    branch.covariance[i][j] = covariance[i][j];
                }
                for (int d = 0; d < 2; ++d) {
                    branch.mean[i][d] = mean[i][d];
                }
            }
        }
        cell_time_ += h;
    }
    const double offset = seconds - cell_time_;
    const double observation[4] = {1, offset, offset, 0};
    double max_evidence = -std::numeric_limits<double>::infinity();
    for (Branch& branch : branches_) {
        double projected[4] = {}, residual[2] = {point.x - anchor_.x, point.y - anchor_.y};
        double innovation = variance_;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                projected[i] += branch.covariance[i][j] * observation[j];
            }
            innovation += observation[i] * projected[i];
            for (int d = 0; d < 2; ++d) {
                residual[d] -= observation[i] * branch.mean[i][d];
            }
        }
        branch.evidence -= 0.5 * (2 * std::log(2 * std::numbers::pi * innovation) +
                                  (residual[0] * residual[0] + residual[1] * residual[1]) / innovation);
        max_evidence = std::max(max_evidence, branch.evidence);
        for (int i = 0; i < 4; ++i) {
            for (int d = 0; d < 2; ++d) {
                branch.mean[i][d] += projected[i] / innovation * residual[d];
            }
            for (int j = i; j < 4; ++j) {
                const double value = 0.5 * (branch.covariance[i][j] + branch.covariance[j][i]) -
                                     projected[i] * projected[j] / innovation;
                branch.covariance[i][j] = branch.covariance[j][i] = value;
            }
        }
    }
    Point total{};
    double weight_sum = 0;
    for (Branch& branch : branches_) {
        branch.evidence -= max_evidence;
        const double weight = std::exp(branch.evidence);
        weight_sum += weight;
        for (int i = 0; i < 4; ++i) {
            total.x += weight * observation[i] * branch.mean[i][0];
            total.y += weight * observation[i] * branch.mean[i][1];
        }
    }
    filtered_ = {anchor_.x + total.x / weight_sum, anchor_.y + total.y / weight_sum};
    last_time_ = seconds;
    return filtered_;
}
void SparseStrokeTracker::reset(Point point, double uncertainty, double momentum, double dither) {
    tracker_.reset(point, uncertainty, momentum);
    raw_ = retained_ = emitted_ = point;
    for (Point& knot : knots_) {
        knot = point;
    }
    raw_time_ = 0;
    remaining_ = 6;
    seed_ = 1;
    finished_ = false;
    dither_ = std::isfinite(dither) ? std::clamp(dither, 0.0, 1.0) : 0;
}
double SparseStrokeTracker::noise() {
    seed_ = 1664525U * seed_ + 1013904223U;
    return dither_ * (2 * static_cast<double>(seed_ >> 8) / 16777215 - 1);
}
void SparseStrokeTracker::append(Point point, std::vector<Point>& output) {
    // A uniform cubic B-spline stays inside its control hull and shares first
    // and second derivatives at joins. No posterior correction is drawn as a
    // discontinuous jump. Tessellate the actual curve, not just its endpoints.
    const Point p[4] = {knots_[0], knots_[1], knots_[2], point};
    double length = 0;
    for (int i = 1; i < 4; ++i) {
        length += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    }
    const int steps = std::clamp(static_cast<int>(std::ceil(length / 0.75)), 4, 4096);
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps, t2 = t * t, t3 = t2 * t;
        const double b[4] = {(1 - 3 * t + 3 * t2 - t3) / 6, (4 - 6 * t2 + 3 * t3) / 6,
                             (1 + 3 * t + 3 * t2 - 3 * t3) / 6, t3 / 6};
        Point value{};
        for (int j = 0; j < 4; ++j) {
            value.x += b[j] * p[j].x;
            value.y += b[j] * p[j].y;
        }
        if (std::hypot(value.x - emitted_.x, value.y - emitted_.y) > 1e-9) {
            output.push_back(value);
            emitted_ = value;
        }
    }
    knots_[0] = knots_[1];
    knots_[1] = knots_[2];
    knots_[2] = point;
}
std::vector<Point> SparseStrokeTracker::advance(Point point, double seconds) {
    std::vector<Point> output;
    if (finished_ || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(seconds) ||
        seconds <= raw_time_) {
        return output;
    }
    const Point start = raw_;
    const double distance = std::hypot(point.x - start.x, point.y - start.y);
    double consumed = 0;
    while (distance - consumed >= remaining_) {
        consumed += remaining_;
        const double fraction = consumed / distance;
        const Point observation{start.x + fraction * (point.x - start.x),
                                start.y + fraction * (point.y - start.y)};
        const double time = raw_time_ + fraction * (seconds - raw_time_);
        retained_ = tracker_.advance({observation.x + noise(), observation.y + noise()}, time);
        append(retained_, output);
        remaining_ = 6;
    }
    remaining_ = std::max(1e-9, remaining_ - (distance - consumed));
    raw_ = point;
    raw_time_ = seconds;
    return output;
}
std::vector<Point> SparseStrokeTracker::finish(Point point, double seconds) {
    if (finished_ || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(seconds)) {
        return {};
    }
    std::vector<Point> output = advance(point, seconds);
    // Flush a short gesture and the final partial interval without adding dither
    // to the endpoint. Repeated knots finish the tail with zero tangent; they
    // are reconstruction constraints, never additional estimator observations.
    retained_ = tracker_.advance(point, std::max(seconds, raw_time_) + 1e-6);
    append(retained_, output);
    for (int i = 0; i < 3; ++i) {
        append(retained_, output);
    }
    finished_ = true;
    return output;
}
void StrokeStabilizer::reset(Point point) {
    filtered_ = pointer_ = point;
}
Point StrokeStabilizer::advance(Point point, double lag) {
    const Point previous = pointer_;
    const double distance = std::hypot(point.x - previous.x, point.y - previous.y);
    pointer_ = point;
    if (lag <= 0) {
        filtered_ = point;
        return point;
    }
    if (distance <= 1e-12) {
        return filtered_;
    }
    lag = std::max(0.1, lag);
    const double decay = std::exp(-distance / lag);
    const double dx = (point.x - previous.x) / distance * lag;
    const double dy = (point.y - previous.y) / distance * lag;
    // Exact integration of df/ds = (pointer(s) - f) / lag along a straight
    // pointer segment: splitting that segment into more events changes nothing.
    filtered_.x = point.x - dx + (filtered_.x - previous.x + dx) * decay;
    filtered_.y = point.y - dy + (filtered_.y - previous.y + dy) * decay;
    return filtered_;
}
Color interpolate_pixel(Color base, Color replacement, double amount) {
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount == 1) {
        return replacement;
    }
    if (amount == 0) {
        return base;
    }
    const double a = base.a * (1 - amount), b = replacement.a * amount, sum = a + b;
    return sum > 0 ? Color{byte((base.r * a + replacement.r * b) / sum),
                           byte((base.g * a + replacement.g * b) / sum),
                           byte((base.b * a + replacement.b * b) / sum), byte(sum)}
                   : Color{0, 0, 0, 0};
}
Color sample_bilinear(const Image& image, double x, double y, bool wrap) {
    const int left = static_cast<int>(std::floor(x)), top = static_cast<int>(std::floor(y));
    const double fx = x - left, fy = y - top;
    const std::array<Color, 4> pixels{
        bounded_pixel(image, left, top, wrap), bounded_pixel(image, left + 1, top, wrap),
        bounded_pixel(image, left, top + 1, wrap), bounded_pixel(image, left + 1, top + 1, wrap)};
    const std::array<double, 4> weights{(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
    double red = 0, green = 0, blue = 0, alpha = 0;
    for (int index = 0; index < 4; ++index) {
        const double weight = weights[index] * pixels[index].a;
        red += weight * pixels[index].r;
        green += weight * pixels[index].g;
        blue += weight * pixels[index].b;
        alpha += weight;
    }
    return alpha > 0 ? Color{byte(red / alpha), byte(green / alpha), byte(blue / alpha), byte(alpha)}
                     : Color{0, 0, 0, 0};
}
void DynamicBrushStroke::clear() {
    dry_pixels_.clear();
    pending_ = 0;
    dab_ = 0;
    started_ = false;
}
void DynamicBrushStroke::segment(Image& image, Point start, Point end, const Ink& ink, bool glitter,
                                 bool wrap) {
    const double dx = end.x - start.x, dy = end.y - start.y, length = std::hypot(dx, dy);
    const Point direction = length > 1e-9 ? Point{dx / length, dy / length} : Point{1, 0};
    const double spacing = std::max(0.5, ink.size * 0.075);
    if (!started_) {
        dab(image, start, direction, ink, glitter, wrap);
        started_ = true;
        pending_ = spacing;
    }
    if (length < 1e-9) {
        return;
    }
    double along = pending_;
    for (; along <= length; along += spacing) {
        dab(image, {start.x + dx * along / length, start.y + dy * along / length}, direction, ink, glitter,
            wrap);
    }
    pending_ = along - length;
}
void DynamicBrushStroke::dab(Image& image, Point center, Point direction, const Ink& ink, bool glitter,
                             bool wrap) {
    center.x = std::round(center.x * 4096) / 4096;
    center.y = std::round(center.y * 4096) / 4096;
    const double radius = std::max(0.5, ink.size * 0.5);
    Ink moving = ink;
    const bool dry = ink.brush == Brush::Pencil || ink.brush == Brush::Crayon || ink.brush == Brush::Pastel ||
                     ink.brush == Brush::Charcoal;
    if (ink.brush == Brush::Pencil) {
        moving.grain_scale *= 0.45;
        moving.paper_roughness *= 0.35;
        moving.pigment_load = std::sqrt(std::clamp(ink.pigment_load, 0.0, 1.0));
    } else if (ink.brush == Brush::Crayon) {
        moving.grain_scale *= 1.7;
        moving.paper_roughness = std::min(1.0, ink.paper_roughness * 1.5);
    } else if (ink.brush == Brush::Pastel) {
        moving.grain_scale *= 0.6;
        moving.paper_roughness *= 0.6;
    } else if (ink.brush == Brush::Charcoal) {
        moving.grain_scale *= 0.85;
        moving.paper_roughness = std::min(1.0, ink.paper_roughness * 1.35);
    }
    moving.material_angle += std::atan2(direction.y, direction.x) * 180 / std::numbers::pi;
    const MaterialSurface material(moving, ink.brush);
    const std::uint32_t seed = ink.noise + ++dab_ * 7919;
    const int left = static_cast<int>(std::floor(center.x - radius - 1));
    const int right = static_cast<int>(std::ceil(center.x + radius + 1));
    const int top = static_cast<int>(std::floor(center.y - radius - 1));
    const int bottom = static_cast<int>(std::ceil(center.y + radius + 1));
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (!wrap && !image.contains(x, y)) {
                continue;
            }
            const double dx = x - center.x, dy = y - center.y;
            double distance = std::hypot(dx, dy);
            if (ink.brush == Brush::Calligraphy || ink.brush == Brush::CalligraphyLeft) {
                const double sign = ink.brush == Brush::Calligraphy ? 1 : -1;
                distance = std::hypot((dx + sign * dy) * 0.7071, (dx - sign * dy) * 2.4);
            }
            const double edge = std::clamp(radius + 0.5 - distance, 0.0, 1.0);
            if (edge == 0) {
                continue;
            }
            Color color = material.sample(x, y, radius - distance);
            double opacity = edge;
            if (ink.brush == Brush::Airbrush) {
                const double density =
                    std::exp(-3.6 * distance * distance / (radius * radius)) * ink.pigment_load * 0.24;
                if (random_unit(x, y, seed) >= density) {
                    continue;
                }
                Lab lab = to_oklab(color);
                lab.l += (random_unit(x, y, seed + 1) - 0.5) * 0.065;
                lab.a += (random_unit(x, y, seed + 2) - 0.5) * 0.006;
                lab.b += (random_unit(x, y, seed + 3) - 0.5) * 0.006;
                if (glitter) {
                    const double facet = random_unit(x, y, ink.noise + 47);
                    // Fine facets have a dark body, a bright shoulder and rare
                    // near-white specular peaks. No repeating bitmap or stars.
                    lab.l = std::clamp(lab.l * (0.63 + 0.68 * facet) + (facet > 0.91 ? 0.38 : 0), 0.0, 1.0);
                    const double tint = facet > 0.91 ? 0.18 : 1.0;
                    lab.a *= tint;
                    lab.b *= tint;
                }
                const std::uint8_t alpha = color.a;
                color = from_oklab(lab);
                color.a = alpha;
            } else if (ink.brush == Brush::Watercolor) {
                opacity *= 0.12 + 0.16 * std::exp(-std::max(0.0, radius - distance) / 1.5);
            } else if (ink.brush == Brush::Oil || ink.brush == Brush::Bristle) {
                const double across = -dx * direction.y + dy * direction.x;
                const double hair = random_unit(static_cast<int>(std::floor(across * 1.8)), 0, ink.noise);
                opacity *= ink.brush == Brush::Bristle ? (hair > 0.46 ? 0.48 : 0.025) : 0.42 + 0.32 * hair;
            } else if (ink.brush == Brush::Marker) {
                opacity *= 0.20;
            } else if (ink.brush == Brush::Pencil) {
                // A firm graphite core with fine, stable tooth. It does not
                // acquire a new random opacity with each overlapping dab.
                opacity *= 0.85 + 0.15 * random_unit(x, y, ink.noise + 101);
            } else if (ink.brush == Brush::Crayon) {
                // Core pressure and wax-bitten edges are sampled from the
                // stable paper surface, including across overlapping dabs.
            } else if (ink.brush == Brush::Pastel) {
                const double depth = std::clamp((radius - distance) / std::max(1.0, radius * 0.35), 0.0, 1.0);
                opacity *=
                    (0.4 + 0.6 * std::sqrt(depth)) * (0.82 + 0.18 * random_unit(x, y, ink.noise + 107));
                const double chalk = 0.14 * ink.paper_roughness;
                color.r = byte(color.r + (255 - color.r) * chalk);
                color.g = byte(color.g + (255 - color.g) * chalk);
                color.b = byte(color.b + (255 - color.b) * chalk);
            }
            color.a = byte(color.a * opacity);
            const int px = wrap ? wrap_index(x, image.width) : x, py = wrap ? wrap_index(y, image.height) : y;
            if (dry) {
                if (!color.a) {
                    continue;
                }
                const double coverage = ink.alternate ? radius - distance + 2 : color.a / 255.0;
                const int index = py * image.width + px;
                std::unordered_map<int, DryDeposit>::iterator found = dry_pixels_.find(index);
                if (found == dry_pixels_.end()) {
                    found = dry_pixels_.emplace(index, DryDeposit{image.get(px, py), 0}).first;
                }
                DryDeposit& deposited = (*found).second;
                if (coverage <= deposited.coverage) {
                    continue;
                }
                deposited.coverage = coverage;
                image.set(px, py, deposited.original);
            }
            image.blend(px, py, color);
        }
    }
}
void TransformStroke::begin(const Image& image, Point origin) {
    base_ = image;
    coverage_.assign(image.pixels.size(), 0);
    origin_ = origin;
}
void TransformStroke::segment(Image& image, Point start, Point end, double diameter, MixEffect effect,
                              double strength, double scale, double phase, bool wrap) {
    if (base_.width != image.width || base_.height != image.height) {
        begin(image, start);
    }
    const double radius = std::max(0.5, diameter * 0.5);
    const int left = static_cast<int>(std::floor(std::min(start.x, end.x) - radius - 1));
    const int right = static_cast<int>(std::ceil(std::max(start.x, end.x) + radius + 1));
    const int top = static_cast<int>(std::floor(std::min(start.y, end.y) - radius - 1));
    const int bottom = static_cast<int>(std::ceil(std::max(start.y, end.y) + radius + 1));
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (!wrap && !image.contains(x, y)) {
                continue;
            }
            const double amount =
                soft_mask(segment_distance({static_cast<double>(x), static_cast<double>(y)}, start, end),
                          radius, 0.55) *
                std::clamp(strength, 0.0, 1.0);
            const int px = wrap ? wrap_index(x, image.width) : x, py = wrap ? wrap_index(y, image.height) : y;
            const std::size_t index = static_cast<std::size_t>(py) * image.width + px;
            if (amount <= coverage_[index]) {
                continue;
            }
            coverage_[index] = amount;
            const Color replacement =
                mix_pixel(base_, px, py, origin_, {end.x - origin_.x, end.y - origin_.y}, effect,
                          std::max(2.0, scale), phase, wrap);
            image.pixels[index] = interpolate_pixel(base_.pixels[index], replacement, amount);
        }
    }
}
bool HealingBrush::has_source() const {
    return !source_.pixels.empty();
}
void HealingBrush::clear() {
    source_ = {};
    target_ = {};
    aligned_ = false;
    coverage_.clear();
}
void HealingBrush::capture(const Image& image, Point point) {
    source_ = image;
    source_point_ = {std::floor(point.x), std::floor(point.y)};
    aligned_ = false;
}
void HealingBrush::begin(const Image& image, Point point) {
    target_ = image;
    coverage_.assign(image.pixels.size(), 0);
    if (!aligned_) {
        offset_ = {source_point_.x - std::floor(point.x), source_point_.y - std::floor(point.y)};
        aligned_ = true;
    }
}
Point HealingBrush::source_for(Point destination) const {
    return aligned_ ? Point{destination.x + offset_.x, destination.y + offset_.y} : source_point_;
}
void HealingBrush::segment(Image& image, Point start, Point end, double diameter, double hardness,
                           double correction, bool wrap) {
    if (!has_source() || target_.width != image.width || target_.height != image.height) {
        return;
    }
    const double radius = std::max(0.5, diameter * 0.5);
    const int left = static_cast<int>(std::floor(std::min(start.x, end.x) - radius - 1));
    const int right = static_cast<int>(std::ceil(std::max(start.x, end.x) + radius + 1));
    const int top = static_cast<int>(std::floor(std::min(start.y, end.y) - radius - 1));
    const int bottom = static_cast<int>(std::ceil(std::max(start.y, end.y) + radius + 1));
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const Point source = source_for({static_cast<double>(x), static_cast<double>(y)});
            const int sx = static_cast<int>(source.x), sy = static_cast<int>(source.y);
            if (!wrap && !source_.contains(sx, sy)) {
                continue;
            }
            if (!wrap && !image.contains(x, y)) {
                continue;
            }
            const double amount =
                soft_mask(segment_distance({static_cast<double>(x), static_cast<double>(y)}, start, end),
                          radius, hardness);
            const int px = wrap ? wrap_index(x, image.width) : x, py = wrap ? wrap_index(y, image.height) : y;
            const std::size_t index = static_cast<std::size_t>(py) * image.width + px;
            if (amount <= coverage_[index]) {
                continue;
            }
            coverage_[index] = amount;
            Color color = bounded_pixel(source_, sx, sy, wrap);
            if (hardness < 0.999 && color.a) {
                const int mean_radius = std::max(2, static_cast<int>(radius * 1.25));
                const Lab source_mean = local_mean(source_, sx, sy, mean_radius, wrap);
                const Lab target_mean = local_mean(target_, px, py, mean_radius, wrap);
                Lab lab = to_oklab(color);
                const double gain = std::clamp(correction, 0.0, 1.0);
                lab.l += (target_mean.l - source_mean.l) * gain;
                lab.a += (target_mean.a - source_mean.a) * gain;
                lab.b += (target_mean.b - source_mean.b) * gain;
                const std::uint8_t alpha = color.a;
                color = from_oklab(lab);
                color.a = alpha;
            }
            image.pixels[index] = interpolate_pixel(target_.pixels[index], color, amount);
        }
    }
}
Image heal_stamp_material(const Image& basis, const Image& material) {
    Image result = basis;
    const int radius = std::max(2, std::min(basis.width, basis.height) / 8);
    for (int y = 0; y < basis.height; ++y) {
        for (int x = 0; x < basis.width; ++x) {
            const double sx = (x + 0.5) * material.width / basis.width - 0.5;
            const double sy = (y + 0.5) * material.height / basis.height - 0.5;
            Color color = sample_bilinear(material, sx, sy);
            if (!color.a) {
                continue;
            }
            const Color original = basis.get(x, y);
            if (!original.a) {
                result.set(x, y, color);
                continue;
            }
            const std::uint8_t incoming_alpha = color.a;
            const Lab old_mean = local_mean(basis, x, y, radius, false);
            const Lab new_mean =
                local_mean(material, static_cast<int>(sx), static_cast<int>(sy), radius, false);
            Lab lab = to_oklab(color);
            lab.l += old_mean.l - new_mean.l;
            lab.a += old_mean.a - new_mean.a;
            lab.b += old_mean.b - new_mean.b;
            const double contribution = color.a / 255.0 * 0.65;
            color = from_oklab(lab);
            color.a = original.a;
            Color combined = interpolate_pixel(original, color, contribution);
            combined.a =
                static_cast<std::uint8_t>(incoming_alpha + (original.a * (255 - incoming_alpha) + 127) / 255);
            result.set(x, y, combined);
        }
    }
    return result;
}
} // namespace paint
