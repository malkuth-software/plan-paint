#include "gradient.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace paint {
namespace {
bool stop_less(const GradientStop& a, const GradientStop& b) {
    return a.position < b.position;
}
Gradient preset(std::initializer_list<Color> colors, double angle = 0) {
    Gradient result;
    result.stops.clear();
    result.angle = angle;
    std::size_t i = 0;
    for (Color color : colors) {
        result.stops.push_back({static_cast<double>(i++) / (colors.size() - 1), color});
    }
    return result;
}
} // namespace
std::span<const GradientPreset> gradient_presets() {
    static const std::vector<GradientPreset> presets{
        {"Rainbow spectrum", preset({{239, 24, 38, 255},
                                     {255, 132, 16, 255},
                                     {255, 232, 35, 255},
                                     {38, 193, 70, 255},
                                     {25, 136, 238, 255},
                                     {61, 44, 181, 255},
                                     {154, 40, 207, 255}})},
        {"Sunset fire", preset({{73, 4, 33, 255},
                                {176, 17, 29, 255},
                                {242, 67, 18, 255},
                                {255, 164, 26, 255},
                                {255, 244, 145, 255}},
                               90)},
        {"Electric blue",
         preset({{5, 12, 59, 255}, {15, 51, 163, 255}, {8, 152, 242, 255}, {110, 255, 255, 255}})},
        {"Metallic chrome",
         {{{0, {24, 31, 41, 255}},
           {.25, {154, 164, 176, 255}},
           {.47, {248, 252, 255, 255}},
           {.5, {63, 71, 85, 255}},
           {.7, {171, 183, 192, 255}},
           {1, {239, 245, 250, 255}}},
          GradientKind::Linear,
          90}},
        {"Gunmetal",
         {{{0, {12, 18, 26, 255}},
           {.38, {76, 88, 104, 255}},
           {.49, {187, 201, 217, 255}},
           {.52, {28, 38, 53, 255}},
           {1, {91, 110, 128, 255}}},
          GradientKind::Linear,
          90}},
        {"Purple galaxy", preset({{12, 11, 57, 255},
                                  {56, 25, 129, 255},
                                  {150, 40, 189, 255},
                                  {234, 77, 165, 255},
                                  {52, 51, 157, 255}},
                                 35)},
        {"Gold", preset({{77, 37, 4, 255},
                         {192, 119, 20, 255},
                         {255, 238, 157, 255},
                         {164, 91, 10, 255},
                         {255, 221, 115, 255}},
                        90)},
        {"Rose quartz",
         preset({{94, 24, 74, 255}, {214, 91, 151, 255}, {255, 193, 211, 255}, {255, 246, 229, 255}}, 45)},
        {"Ocean",
         preset({{5, 30, 75, 255}, {8, 101, 156, 255}, {15, 191, 186, 255}, {191, 252, 230, 255}}, 90)},
        {"Emerald",
         preset({{9, 39, 28, 255}, {18, 110, 62, 255}, {109, 198, 75, 255}, {236, 255, 164, 255}}, 45)},
        {"Copper", preset({{57, 24, 20, 255},
                           {171, 78, 41, 255},
                           {255, 203, 149, 255},
                           {107, 47, 35, 255},
                           {236, 165, 105, 255}},
                          90)},
        {"Black and white", preset({{0, 0, 0, 255}, {255, 255, 255, 255}})}};
    return presets;
}
void normalize_gradient(Gradient& gradient) {
    if (!std::isfinite(gradient.angle)) {
        gradient.angle = 0;
    }
    gradient.angle = std::fmod(gradient.angle, 360.0);
    if (gradient.angle < 0) {
        gradient.angle += 360;
    }
    if (gradient.stops.size() > 32) {
        gradient.stops.resize(32);
    }
    for (GradientStop& stop : gradient.stops) {
        stop.position = std::isfinite(stop.position) ? std::clamp(stop.position, 0.0, 1.0) : 0;
    }
    std::stable_sort(gradient.stops.begin(), gradient.stops.end(), stop_less);
    if (gradient.stops.empty()) {
        gradient.stops.push_back({0, {0, 0, 0, 255}});
    }
    if (gradient.stops.size() == 1) {
        gradient.stops.push_back({1, gradient.stops.front().color});
    }
}
std::size_t move_gradient_stop(Gradient& gradient, std::size_t index, double position) {
    if (index >= gradient.stops.size()) {
        return 0;
    }
    GradientStop stop = gradient.stops[index];
    stop.position = std::isfinite(position) ? std::clamp(position, 0.0, 1.0) : 0;
    gradient.stops.erase(gradient.stops.begin() + index);
    std::size_t destination = 0;
    while (destination < gradient.stops.size() && gradient.stops[destination].position <= stop.position) {
        ++destination;
    }
    gradient.stops.insert(gradient.stops.begin() + destination, stop);
    return destination;
}
GradientSampler::GradientSampler(const Gradient& gradient, Rect bounds) {
    Gradient normalized = gradient;
    normalize_gradient(normalized);
    kind_ = normalized.kind;
    for (const GradientStop& stop : normalized.stops) {
        stops_.push_back({stop.position, to_oklab(stop.color), stop.color.a / 255.0});
    }
    center_x_ = bounds.x + (bounds.w - 1) * .5;
    center_y_ = bounds.y + (bounds.h - 1) * .5;
    double radians = normalized.angle * std::numbers::pi / 180;
    dx_ = std::cos(radians);
    dy_ = std::sin(radians);
    span_ = std::max(1e-12,
                     std::abs(dx_) * std::max(0, bounds.w - 1) + std::abs(dy_) * std::max(0, bounds.h - 1));
    radius_ = std::max(.5, std::hypot(std::max(0, bounds.w - 1) * .5, std::max(0, bounds.h - 1) * .5));
}
Color GradientSampler::at(double position) const {
    position = std::isfinite(position) ? std::clamp(position, 0.0, 1.0) : 0;
    std::size_t upper = 0;
    while (upper < stops_.size() && stops_[upper].position <= position) {
        ++upper;
    }
    const Stop& a = stops_[upper ? upper - 1 : 0];
    const Stop& b = stops_[std::min(upper, stops_.size() - 1)];
    const double t = b.position > a.position
                         ? std::clamp((position - a.position) / (b.position - a.position), 0.0, 1.0)
                         : 0;
    double alpha = (1 - t) * a.alpha + t * b.alpha;
    double u = alpha > 1e-12 ? t * b.alpha / alpha : t;
    Color result =
        from_oklab({a.color.l + (b.color.l - a.color.l) * u, a.color.a + (b.color.a - a.color.a) * u,
                    a.color.b + (b.color.b - a.color.b) * u});
    result.a = static_cast<std::uint8_t>(std::lround(alpha * 255));
    return result;
}
Color GradientSampler::sample(double x, double y) const {
    return at(kind_ == GradientKind::Radial ? std::hypot(x - center_x_, y - center_y_) / radius_
                                            : .5 + ((x - center_x_) * dx_ + (y - center_y_) * dy_) / span_);
}
} // namespace paint
