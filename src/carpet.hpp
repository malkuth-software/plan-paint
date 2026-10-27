#pragma once
#include "image.hpp"
#include <array>
#include <atomic>
namespace paint {
struct CarpetParameters {
    Color color{22, 69, 197, 255};
    double web = 0, radius = .010, height = .62, length = 1.7, disorder = .34, crimp = .30;
    double roughness = .37, azimuth_roughness = .40, dye = 1, coverage = 5.8;
    double magnification = .60, light = 140, view = 8, exposure = 1.15;
    std::uint32_t seed = 718;
    bool hillshade = false;
};
inline constexpr int carpet_preset_count = 12;
const char* carpet_preset_name(int index);
CarpetParameters carpet_preset(int index);
// Native port of the accepted Fabric Lab fiber/capsule/optics renderer.
// Temporaries are released on return. Cancellation is checked between fibers/rows.
Image render_carpet(const CarpetParameters& parameters, int width = 256, int height = 256,
                    const std::atomic<bool>* cancel = nullptr);
Image render_carpet_tile(const CarpetParameters& parameters, int side = 512,
                         const std::atomic<bool>* cancel = nullptr);
// Horn 3x3 derivative. Rows run southward; azimuth is clockwise from north.
double horn_hillshade(const std::array<double, 9>& heights, double cell_x, double cell_y,
                      double altitude_degrees, double azimuth_degrees);
} // namespace paint
