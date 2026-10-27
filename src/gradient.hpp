#pragma once
#include "image.hpp"
#include <span>
#include <string>
#include <vector>
namespace paint {
enum class GradientKind { Linear, Radial };
struct GradientStop {
    double position = 0;
    Color color;
};
struct Gradient {
    std::vector<GradientStop> stops{{0, {0, 0, 0, 255}}, {1, {255, 255, 255, 255}}};
    GradientKind kind = GradientKind::Linear;
    double angle = 0;
};
struct GradientPreset {
    std::string name;
    Gradient gradient;
};
std::span<const GradientPreset> gradient_presets();
// Stops are sorted, clamped and bounded to 32 entries. At least two survive.
void normalize_gradient(Gradient& gradient);
std::size_t move_gradient_stop(Gradient& gradient, std::size_t index, double position);
class GradientSampler {
  public:
    GradientSampler(const Gradient& gradient, Rect bounds);
    Color at(double position) const;
    Color sample(double x, double y) const;

  private:
    struct Stop {
        double position;
        Lab color;
        double alpha;
    };
    std::vector<Stop> stops_;
    GradientKind kind_;
    double center_x_, center_y_, dx_, dy_, span_, radius_;
};
} // namespace paint
