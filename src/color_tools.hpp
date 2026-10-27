#pragma once
#include "image.hpp"
#include <array>
namespace paint {
enum class PickerSpace { RGB, OKHSL };
enum class SampleMode { Exact, Average, Representative };
struct ColorCoordinates {
    double hue = 0, saturation = 0, level = 0;
};
Color picker_color(PickerSpace space, ColorCoordinates coordinates);
ColorCoordinates picker_coordinates(PickerSpace space, Color color);
Color sample_color(const Image& image, Point point, SampleMode mode, int radius = 2);
// A deterministic weighted centroidal lattice on one hue slice. Compact cells
// use screen distance, with perceptual area and saturation setting the density.
// Seven shared neutrals anchor the boundary.
class ColorMosaic {
  public:
    explicit ColorMosaic(PickerSpace space, double hue = 0);
    static constexpr double aspect = 290.0 / 224.0;
    std::size_t cell_at(double saturation, double level) const;
    Color nearest(Color color) const;
    std::size_t nearest_index(Color color) const;
    const std::array<Color, 96>& colors() const;
    ColorCoordinates coordinates(std::size_t index) const;

  private:
    std::array<Color, 96> colors_{};
    std::array<Lab, 96> centers_{};
    std::array<ColorCoordinates, 96> coordinates_{};
};
} // namespace paint
