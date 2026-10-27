#include "color_tools.hpp"
#include "../vendor/ok_color/ok_color.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace paint {
namespace {
std::uint8_t channel(double value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
}
double distance_squared(Lab a, Lab b) {
    return (a.l - b.l) * (a.l - b.l) + (a.a - b.a) * (a.a - b.a) + (a.b - b.b) * (a.b - b.b);
}
std::size_t nearest_center(Lab lab, const std::array<Lab, 96>& centers) {
    std::size_t best = 0;
    double distance = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index < centers.size(); ++index) {
        const double candidate = distance_squared(lab, centers[index]);
        if (candidate < distance) {
            best = index;
            distance = candidate;
        }
    }
    return best;
}
} // namespace
Color picker_color(PickerSpace space, ColorCoordinates coordinates) {
    const double hue = coordinates.hue - std::floor(coordinates.hue);
    const double saturation = std::clamp(coordinates.saturation, 0.0, 1.5);
    const double level = std::clamp(coordinates.level, 0.0, 1.0);
    if (space == PickerSpace::OKHSL) {
        const ok_color::RGB rgb = ok_color::okhsl_to_srgb({hue, saturation, level});
        return {channel(rgb.r), channel(rgb.g), channel(rgb.b), 255};
    }
    const double h = hue * 6, c = level * saturation;
    const double x = c * (1 - std::abs(std::fmod(h, 2) - 1)), m = level - c;
    const int sector = static_cast<int>(h);
    const std::array<double, 6> red{c, x, 0, 0, x, c}, green{x, c, c, x, 0, 0}, blue{0, 0, x, c, c, x};
    return {channel(red[sector] + m), channel(green[sector] + m), channel(blue[sector] + m), 255};
}
ColorCoordinates picker_coordinates(PickerSpace space, Color color) {
    const double r = color.r / 255.0, g = color.g / 255.0, b = color.b / 255.0;
    if (space == PickerSpace::OKHSL) {
        if (color.r == color.g && color.g == color.b) {
            return {0, 0, ok_color::toe(to_oklab(color).l)};
        }
        const ok_color::HSL hsl = ok_color::srgb_to_okhsl({r, g, b});
        return {hsl.h, hsl.s, std::clamp(hsl.l, 0.0, 1.0)};
    }
    const double maximum = std::max({r, g, b}), minimum = std::min({r, g, b}), delta = maximum - minimum;
    double hue = 0;
    if (delta > 0) {
        hue = maximum == r   ? std::fmod((g - b) / delta + 6, 6) / 6
              : maximum == g ? ((b - r) / delta + 2) / 6
                             : ((r - g) / delta + 4) / 6;
    }
    return {hue, maximum == 0 ? 0 : delta / maximum, maximum};
}
Color sample_color(const Image& image, Point point, SampleMode mode, int radius) {
    const int x = static_cast<int>(std::floor(point.x)), y = static_cast<int>(std::floor(point.y));
    if (mode == SampleMode::Exact) {
        return image.get(x, y);
    }
    radius = std::clamp(radius, 1, 16);
    std::vector<Lab> samples;
    std::vector<double> weights;
    double alpha = 0;
    int count = 0;
    for (int row = std::max(0, y - radius); row <= std::min(image.height - 1, y + radius); ++row) {
        for (int column = std::max(0, x - radius); column <= std::min(image.width - 1, x + radius);
             ++column) {
            const Color color = image.get(column, row);
            alpha += color.a;
            ++count;
            if (color.a) {
                samples.push_back(to_oklab(color));
                weights.push_back(color.a / 255.0);
            }
        }
    }
    if (samples.empty()) {
        return {0, 0, 0, 0};
    }
    std::vector<double> light, a, b;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        light.push_back(samples[index].l);
        a.push_back(samples[index].a);
        b.push_back(samples[index].b);
    }
    std::sort(light.begin(), light.end());
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    const std::size_t middle = samples.size() / 2;
    const Lab median{light[middle], a[middle], b[middle]};
    std::vector<double> deviations;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        deviations.push_back(std::sqrt(distance_squared(samples[index], median)));
    }
    std::sort(deviations.begin(), deviations.end());
    const double cutoff = std::max(0.015, deviations[middle] * 3);
    Lab sum;
    double total = 0;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (mode == SampleMode::Representative &&
            distance_squared(samples[index], median) > cutoff * cutoff) {
            continue;
        }
        const double weight = weights[index];
        sum.l += samples[index].l * weight;
        sum.a += samples[index].a * weight;
        sum.b += samples[index].b * weight;
        total += weight;
    }
    Color result = from_oklab(total > 0 ? Lab{sum.l / total, sum.a / total, sum.b / total} : median);
    result.a = static_cast<std::uint8_t>(std::lround(alpha / count));
    return result;
}
ColorMosaic::ColorMosaic(PickerSpace space, double hue) {
    hue -= std::floor(hue);
    struct Sample {
        Color color;
        Lab lab;
        ColorCoordinates coordinates;
        double weight = 0;
    };
    constexpr int columns = 64, rows = 64;
    std::vector<Sample> samples;
    samples.reserve((columns + 1) * (rows + 1));
    for (int row = 0; row <= rows; ++row) {
        for (int column = 0; column <= columns; ++column) {
            const ColorCoordinates coordinates{hue, column / static_cast<double>(columns),
                                               row / static_cast<double>(rows)};
            const Color color = picker_color(space, coordinates);
            samples.push_back({color, to_oklab(color), coordinates, 0});
        }
    }
    for (int row = 0; row <= rows; ++row) {
        for (int column = 0; column <= columns; ++column) {
            Sample& sample = samples[row * (columns + 1) + column];
            const Lab left = samples[row * (columns + 1) + std::max(0, column - 1)].lab,
                      right = samples[row * (columns + 1) + std::min(columns, column + 1)].lab,
                      down = samples[std::max(0, row - 1) * (columns + 1) + column].lab,
                      up = samples[std::min(rows, row + 1) * (columns + 1) + column].lab;
            const Lab ds{right.l - left.l, right.a - left.a, right.b - left.b},
                dl{up.l - down.l, up.a - down.a, up.b - down.b};
            const Lab cross{ds.a * dl.b - ds.b * dl.a, ds.b * dl.l - ds.l * dl.b, ds.l * dl.a - ds.a * dl.l};
            const double area = std::sqrt(cross.l * cross.l + cross.a * cross.a + cross.b * cross.b);
            // Perceptual area supplies the local color variation. The smooth
            // saturation preference is explicit; the screen-space objective
            // below keeps the resulting cells practical to click.
            const double saturation = sample.coordinates.saturation;
            sample.weight = area * (1 + 2 * saturation * saturation);
        }
    }
    constexpr std::size_t neutrals = 7;
    for (std::size_t index = 0; index < neutrals; ++index) {
        colors_[index] = from_oklab({index / static_cast<double>(neutrals - 1), 0, 0});
        centers_[index] = to_oklab(colors_[index]);
        coordinates_[index] = picker_coordinates(space, colors_[index]);
        coordinates_[index].hue = hue;
    }
    // Use Euclidean distance in the displayed rectangle so the cells remain
    // compact click targets. Perceptual surface area supplies the density,
    // instead of stretching cells along the much smaller chroma axis.
    double maximum_weight = 0;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        maximum_weight = std::max(maximum_weight, samples[index].weight);
    }
    for (std::size_t index = 0; index < samples.size(); ++index) {
        Sample& sample = samples[index];
        const double saturation = sample.coordinates.saturation;
        sample.weight =
            (0.08 + sample.weight / std::max(maximum_weight, 1e-12)) * (0.25 + 3 * saturation * saturation);
    }
    std::vector<double> distances(samples.size(), std::numeric_limits<double>::max());
    for (std::size_t site = 0; site < centers_.size(); ++site) {
        if (site >= neutrals) {
            std::size_t chosen = 0;
            double farthest = -1;
            for (std::size_t index = 0; index < samples.size(); ++index) {
                const double score = distances[index] * std::sqrt(samples[index].weight);
                if (score > farthest && samples[index].coordinates.saturation > 0 &&
                    samples[index].coordinates.level > 0 && samples[index].coordinates.level < 1) {
                    chosen = index;
                    farthest = score;
                }
            }
            colors_[site] = samples[chosen].color;
            centers_[site] = samples[chosen].lab;
            coordinates_[site] = samples[chosen].coordinates;
        }
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const double ds =
                             (samples[index].coordinates.saturation - coordinates_[site].saturation) * aspect,
                         dl = samples[index].coordinates.level - coordinates_[site].level;
            distances[index] = std::min(distances[index], ds * ds + dl * dl);
        }
    }
    std::vector<std::size_t> assignments(samples.size());
    for (int iteration = 0; iteration < 32; ++iteration) {
        std::array<Point, 96> sums{};
        std::array<double, 96> weights{};
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const Sample& sample = samples[index];
            const std::size_t cell = cell_at(sample.coordinates.saturation, sample.coordinates.level);
            assignments[index] = cell;
            sums[cell].x += sample.coordinates.saturation * sample.weight;
            sums[cell].y += sample.coordinates.level * sample.weight;
            weights[cell] += sample.weight;
        }
        for (std::size_t site = neutrals; site < centers_.size(); ++site) {
            if (weights[site] > 0) {
                sums[site] = {sums[site].x / weights[site], sums[site].y / weights[site]};
            }
        }
        std::array<double, 96> closest;
        closest.fill(std::numeric_limits<double>::max());
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const std::size_t site = assignments[index];
            if (site < neutrals || weights[site] == 0) {
                continue;
            }
            const double ds = (samples[index].coordinates.saturation - sums[site].x) * aspect,
                         dl = samples[index].coordinates.level - sums[site].y;
            const double distance = ds * ds + dl * dl;
            if (distance < closest[site]) {
                bool duplicate = false;
                for (std::size_t other = 0; other < colors_.size(); ++other) {
                    if (other != site && equal(colors_[other], samples[index].color)) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) {
                    continue;
                }
                closest[site] = distance;
                colors_[site] = samples[index].color;
                centers_[site] = samples[index].lab;
                coordinates_[site] = samples[index].coordinates;
            }
        }
    }
}
std::size_t ColorMosaic::cell_at(double saturation, double level) const {
    std::size_t best = 0;
    double distance = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index < coordinates_.size(); ++index) {
        const double ds = (saturation - coordinates_[index].saturation) * aspect,
                     dl = level - coordinates_[index].level;
        if (ds * ds + dl * dl < distance) {
            best = index;
            distance = ds * ds + dl * dl;
        }
    }
    return best;
}

std::size_t ColorMosaic::nearest_index(Color color) const {
    return nearest_center(to_oklab(color), centers_);
}
Color ColorMosaic::nearest(Color color) const {
    Color result = colors_[nearest_index(color)];
    result.a = color.a;
    return result;
}
const std::array<Color, 96>& ColorMosaic::colors() const {
    return colors_;
}
ColorCoordinates ColorMosaic::coordinates(std::size_t index) const {
    return coordinates_.at(index);
}
} // namespace paint
