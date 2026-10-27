#include "material.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace paint {
namespace {
double hash(int x, int y, std::uint32_t seed) {
    std::uint32_t v = static_cast<std::uint32_t>(x) * 374761393u +
                      static_cast<std::uint32_t>(y) * 668265263u + seed * 1274126177u;
    v = (v ^ (v >> 13)) * 1274126177u;
    return static_cast<double>((v ^ (v >> 16)) >> 8) / 16777215.0;
}
double smooth(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}
double noise(double x, double y, std::uint32_t seed) {
    int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    double fx = smooth(x - ix), fy = smooth(y - iy);
    double a = hash(ix, iy, seed), b = hash(ix + 1, iy, seed), c = hash(ix, iy + 1, seed),
           d = hash(ix + 1, iy + 1, seed);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}
std::uint8_t channel(double value) {
    return static_cast<std::uint8_t>(std::clamp(std::round(value), 0.0, 255.0));
}
} // namespace
bool textured_brush(Brush brush) {
    return brush == Brush::Oil || brush == Brush::Crayon || brush == Brush::Pencil ||
           brush == Brush::Watercolor || brush == Brush::Bristle || brush == Brush::Pastel ||
           brush == Brush::Charcoal || brush == Brush::Marker || brush == Brush::Gel;
}
MaterialSurface::MaterialSurface(const Ink& ink, Brush brush, RasterSpace space)
    : ink_(ink), brush_(brush), space_(space) {
    ink_.size /= space.scale;
    if (ink_.alternate) {
        alternate_ = std::make_unique<MaterialSurface>(*ink_.alternate, (*ink_.alternate).brush, space);
        ink_.alternate.reset();
        ink_.transparent_pattern = true;
    }
    ink_.grain_scale = std::clamp(ink_.grain_scale, 0.3, 4.0);
    ink_.paper_roughness = std::clamp(ink_.paper_roughness, 0.0, 1.0);
    ink_.pigment_load = std::clamp(ink_.pigment_load, 0.0, 1.0);
    double angle = ink_.material_angle * std::numbers::pi / 180;
    cosine_ = std::cos(angle);
    sine_ = std::sin(angle);
}
Color MaterialSurface::sample(int x, int y, double edge_distance) const {
    const Color front = sample_primary(space_.paper_x(x), space_.paper_y(y), edge_distance / space_.scale);
    if (!alternate_ || front.a == 255) {
        return front;
    }
    const Color back = (*alternate_).sample(x, y, edge_distance);
    const double a = front.a / 255.0, b = back.a / 255.0 * (1 - a), sum = a + b;
    return sum > 0
               ? Color{channel((front.r * a + back.r * b) / sum), channel((front.g * a + back.g * b) / sum),
                       channel((front.b * a + back.b * b) / sum), channel(sum * 255)}
               : Color{0, 0, 0, 0};
}
Color MaterialSurface::sample_primary(int x, int y, double edge_distance) const {
    Color color = patterned(ink_, x, y);
    if (!textured_brush(brush_) || color.a == 0) {
        return color;
    }
    if (brush_ == Brush::Gel) {
        // Opaque body with fixed spatial sheen: no event-dependent dots or drying simulation.
        const double scale = ink_.grain_scale;
        const double field = noise(x / (8 * scale), y / (8 * scale), ink_.noise + 131);
        const double fleck = hash(x, y, ink_.noise + 193);
        const double shine = (.025 + .09 * field + (fleck > .975 ? .25 : 0)) * ink_.paper_roughness;
        const double tone = .965 + .035 * field;
        color.r = channel(color.r * tone + (255 - color.r) * shine);
        color.g = channel(color.g * tone + (255 - color.g) * shine);
        color.b = channel(color.b * tone + (255 - color.b) * shine);
        color.a = ink_.pigment_load > 0 ? color.a : 0;
        return color;
    }
    double scale = ink_.grain_scale, rough = ink_.paper_roughness, load = ink_.pigment_load;
    double u = (x * cosine_ + y * sine_) / scale, v = (-x * sine_ + y * cosine_) / scale;
    // Three spatial scales create clustered paper hills, not independent missing pixels.
    double paper = 0.48 * noise(x / (3.1 * scale), y / (3.1 * scale), ink_.noise) +
                   0.32 * noise(x / (11.3 * scale), y / (11.3 * scale), ink_.noise + 5) +
                   0.20 * noise(x / (0.85 * scale), y / (0.85 * scale), ink_.noise + 17);
    double fiber =
        noise(u / 43.0, v / 1.15 + 0.7 * noise(u / 70.0, v / 16.0, ink_.noise + 23), ink_.noise + 31);
    double opacity = 1, tone = 1, highlight = 0;
    if (brush_ == Brush::Watercolor) {
        double clouds = noise(x / (38 * scale), y / (38 * scale), ink_.noise + 41);
        double granulation = 1 + rough * (0.65 - paper) * 1.6;
        double rim = std::exp(-std::max(0.0, edge_distance) / (2.2 * scale));
        double tide = std::exp(-std::pow((clouds - 0.42) * 15, 2)) * rough;
        double density = load * (0.25 + 0.58 * clouds) * granulation * (1 + 1.25 * rim + 0.36 * tide);
        opacity = 1 - std::exp(-density);
        tone = 1 - 0.12 * rim - 0.055 * granulation;
    } else if (brush_ == Brush::Oil || brush_ == Brush::Bristle) {
        double groove = smooth((fiber - 0.22) * 2.1);
        double neighboring = noise(
            u / 43.0, (v + 0.6) / 1.15 + 0.7 * noise(u / 70.0, v / 16.0, ink_.noise + 23), ink_.noise + 31);
        double relief = (fiber - neighboring) * rough;
        opacity = (0.25 + 0.75 * load) * (1 - rough * (1 - groove) * (brush_ == Brush::Oil ? 0.48 : 0.88));
        tone = 1 + relief * (brush_ == Brush::Oil ? 0.48 : 0.16) - 0.07 * rough * (1 - groove);
        highlight = brush_ == Brush::Oil ? std::max(0.0, relief) * 0.16 : 0;
    } else if (brush_ == Brush::Crayon || brush_ == Brush::Pastel || brush_ == Brush::Charcoal ||
               brush_ == Brush::Pencil) {
        // Contact pressure is strongest in the core. Wax fills paper valleys;
        // charcoal compacts there, leaving exposed tooth and crumbs at the rim.
        // This is a bounded deposition approximation, not a particle simulation.
        const bool shaped_dry = brush_ == Brush::Crayon || brush_ == Brush::Charcoal;
        const double rim_width = std::max(1.0, ink_.size * (brush_ == Brush::Crayon ? 0.22 : 0.32));
        const double core = shaped_dry ? smooth(std::max(0.0, edge_distance) / rim_width) : 0;
        double pressure = 0.70 - load * 0.43 - core * load * 0.32 + (shaped_dry ? 0.13 * (1 - core) : 0);
        double contact = smooth((paper - pressure) * 7.0 + 0.35);
        contact = 1 - rough + rough * contact;
        double scratch = 0.65 + 0.35 * fiber;
        if (brush_ == Brush::Pencil) {
            opacity = load * contact * scratch * 0.82;
        } else if (brush_ == Brush::Charcoal) {
            const double crumbs = 0.25 + 0.75 * hash(x, y, ink_.noise + 109);
            const double rim = (0.25 + 0.75 * smooth(edge_distance / 2.0)) * crumbs;
            opacity = (0.35 + 0.65 * load) *
                      ((1 - core) * std::pow(contact, 1.3) * rim + core * (0.92 + 0.08 * paper));
        } else if (brush_ == Brush::Pastel) {
            opacity = (0.3 + 0.6 * load) * std::sqrt(contact) * (0.85 + 0.15 * fiber);
            highlight = 0.035 * rough;
        } else {
            const double wax = smooth((paper - 0.47) * 8 + edge_distance / (1.4 * scale));
            opacity =
                (0.45 + 0.55 * load) * ((1 - core) * contact * scratch * wax + core * (0.94 + 0.06 * fiber));
        }
        tone = 1 - 0.08 * rough * (1 - paper);
    } else if (brush_ == Brush::Marker) {
        opacity = (1 - std::exp(-load * 1.7)) * (1 - rough * 0.14 * (1 - fiber));
    }
    // Paint load zero always deposits nothing, including dense dry media.
    if (load <= 0) {
        opacity = 0;
    }
    color.r = channel(color.r * tone + (255 - color.r) * highlight);
    color.g = channel(color.g * tone + (255 - color.g) * highlight);
    color.b = channel(color.b * tone + (255 - color.b) * highlight);
    color.a = channel(color.a * std::clamp(opacity, 0.0, 1.0));
    return color;
}
int PixelCoverage::count() const {
    int result = 0;
    for (int i = 0; i < 16; ++i) {
        result += std::popcount(bits[i]);
    }
    return result;
}
bool PixelCoverage::full() const {
    for (int i = 0; i < 16; ++i) {
        if (bits[i] != UINT64_MAX) {
            return false;
        }
    }
    return true;
}
static Color covered_fill_and_stroke(const FillBoundaryPixel& fill, Color stroke, const PixelCoverage& mask) {
    int overlap = 0;
    for (int i = 0; i < 16; ++i) {
        overlap += std::popcount(fill.coverage.bits[i] & mask.bits[i]);
    }
    double stroke_area = mask.count() / 1024.0, fill_area = fill.coverage.count() / 1024.0,
           overlap_area = overlap / 1024.0;
    double stroke_alpha = stroke.a / 255.0, fill_alpha = fill.paint.a / 255.0,
           base_alpha = fill.original.a / 255.0;
    double sw = stroke_alpha * stroke_area;
    double fw = fill_alpha * (fill_area - stroke_alpha * overlap_area);
    double bw = base_alpha * (1 - sw - fw);
    double alpha = sw + fw + bw;
    if (alpha <= 0) {
        return {0, 0, 0, 0};
    }
    return {channel((stroke.r * sw + fill.paint.r * fw + fill.original.r * bw) / alpha),
            channel((stroke.g * sw + fill.paint.g * fw + fill.original.g * bw) / alpha),
            channel((stroke.b * sw + fill.paint.b * fw + fill.original.b * bw) / alpha),
            channel(alpha * 255)};
}
void MaterialStroke::clear() {
    pixels_.clear();
}
void MaterialStroke::segment(Image& image, Point start, Point end, const Ink& ink, RasterSpace space) {
    MaterialSurface material(ink, ink.brush, space);
    double radius = std::max(0.5, ink.size * 0.5), dx = end.x - start.x, dy = end.y - start.y,
           length2 = dx * dx + dy * dy;
    int top = std::max(0, static_cast<int>(std::floor(std::min(start.y, end.y) - radius - 1)));
    int bottom =
        std::min(image.height - 1, static_cast<int>(std::ceil(std::max(start.y, end.y) + radius + 1)));
    for (int y = top; y <= bottom; ++y) {
        double t0 = 0, t1 = 1;
        if (std::abs(dy) > 1e-10) {
            t0 = std::clamp((y - radius - 1 - start.y) / dy, 0.0, 1.0);
            t1 = std::clamp((y + radius + 1 - start.y) / dy, 0.0, 1.0);
        }
        double x0 = start.x + dx * t0, x1 = start.x + dx * t1;
        int left = std::max(0, static_cast<int>(std::floor(std::min(x0, x1) - radius - 1)));
        int right = std::min(image.width - 1, static_cast<int>(std::ceil(std::max(x0, x1) + radius + 1)));
        for (int x = left; x <= right; ++x) {
            double t = length2 > 1e-15
                           ? std::clamp(((x - start.x) * dx + (y - start.y) * dy) / length2, 0.0, 1.0)
                           : 0;
            double distance = std::hypot(x - start.x - t * dx, y - start.y - t * dy);
            double depth = radius - distance;
            if (depth <= (ink.smooth ? -0.707107 : 0.0)) {
                continue;
            }
            int index = y * image.width + x;
            Pixel& pixel = pixels_[index];
            if (pixel.depth < -1e10) {
                pixel.original = image.pixels[index];
            }
            if (depth <= pixel.depth && pixel.coverage.full()) {
                continue;
            }
            PixelCoverage covered;
            for (int word = 0; word < 16; ++word) {
                covered.bits[word] = UINT64_MAX;
            }
            if (ink.smooth && depth < 0.707107) {
                covered = {};
                // Integrate the continuous swept disk on a shared 32x32 pixel-area grid.
                // Samples are unioned across segments, so overlaps and joins never darken twice.
                for (int sy = 0; sy < 32; ++sy) {
                    for (int sx = 0; sx < 32; ++sx) {
                        double px = x + (sx + 0.5) / 32.0 - 0.5 - start.x;
                        double py = y + (sy + 0.5) / 32.0 - 0.5 - start.y;
                        double projection =
                            length2 > 1e-15 ? std::clamp((px * dx + py * dy) / length2, 0.0, 1.0) : 0;
                        double ex = px - projection * dx, ey = py - projection * dy;
                        if (ex * ex + ey * ey <= radius * radius) {
                            int bit = sy * 32 + sx;
                            covered.bits[bit / 64] |= std::uint64_t{1} << (bit % 64);
                        }
                    }
                }
            }
            PixelCoverage united = pixel.coverage;
            for (int word = 0; word < 16; ++word) {
                united.bits[word] |= covered.bits[word];
            }
            if (united.bits == pixel.coverage.bits && depth <= pixel.depth) {
                continue;
            }
            pixel.depth = std::max(pixel.depth, depth);
            pixel.coverage = united;
            Color color = material.sample(x, y, std::max(0.0, pixel.depth));
            if (fill_boundary_ && (*fill_boundary_).contains(index)) {
                image.pixels[index] = covered_fill_and_stroke((*fill_boundary_).at(index), color, united);
            } else {
                color.a = channel(color.a * united.count() / 1024.0);
                image.pixels[index] = pixel.original;
                image.blend(x, y, color);
            }
        }
    }
}
void material_fill(Image& image, const std::vector<Point>& points, const Ink& ink, Brush brush,
                   FillBoundary* boundary, RasterSpace space) {
    if (points.size() < 3) {
        return;
    }
    double min_x = points[0].x, max_x = min_x, min_y = points[0].y, max_y = min_y;
    for (std::size_t i = 1; i < points.size(); ++i) {
        min_x = std::min(min_x, points[i].x);
        max_x = std::max(max_x, points[i].x);
        min_y = std::min(min_y, points[i].y);
        max_y = std::max(max_y, points[i].y);
    }
    // A small off-canvas border keeps cropping from inventing a pigment rim.
    int left = std::max(-16 * space.scale, static_cast<int>(std::floor(min_x)) - 1);
    int top = std::max(-16 * space.scale, static_cast<int>(std::floor(min_y)) - 1);
    int right = std::min(image.width + 16 * space.scale, static_cast<int>(std::ceil(max_x)) + 1);
    int bottom = std::min(image.height + 16 * space.scale, static_cast<int>(std::ceil(max_y)) + 1);
    int width = right - left, height = bottom - top;
    if (width <= 0 || height <= 0) {
        return;
    }
    std::vector<std::uint16_t> coverage(static_cast<std::size_t>(width) * height, 0);
    std::vector<std::uint8_t> distance(coverage.size(), 0);
    std::vector<PixelCoverage> row_coverage(width);
    std::vector<double> crossings;
    crossings.reserve(points.size());
    for (int row = 0; row < height; ++row) {
        std::fill(row_coverage.begin(), row_coverage.end(), PixelCoverage{});
        for (int sy = 0; sy < 32; ++sy) {
            double y = top + row + (sy + 0.5) / 32.0 - 0.5;
            crossings.clear();
            for (std::size_t i = 0; i < points.size(); ++i) {
                Point a = points[i], b = points[(i + 1) % points.size()];
                if ((a.y > y) != (b.y > y)) {
                    crossings.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
                }
            }
            std::sort(crossings.begin(), crossings.end());
            for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
                int begin = std::max(left, static_cast<int>(std::floor(crossings[i] + 0.5)));
                int end = std::min(right, static_cast<int>(std::ceil(crossings[i + 1] + 0.5)));
                for (int x = begin; x < end; ++x) {
                    std::uint64_t row_mask = 4294967295ULL;
                    if (x - 0.5 < crossings[i] || x + 0.5 > crossings[i + 1]) {
                        row_mask = 0;
                        for (int sx = 0; sx < 32; ++sx) {
                            double sample = x + (sx + 0.5) / 32.0 - 0.5;
                            if (sample >= crossings[i] && sample < crossings[i + 1]) {
                                row_mask |= std::uint64_t{1} << sx;
                            }
                        }
                    }
                    row_coverage[x - left].bits[sy / 2] |= row_mask << ((sy % 2) * 32);
                }
            }
        }
        for (int x = left; x < right; ++x) {
            std::size_t index = static_cast<std::size_t>(row) * width + x - left;
            if (!ink.smooth) {
                bool inside = inside_polygon(points, x, top + row);
                for (int word = 0; word < 16; ++word) {
                    row_coverage[x - left].bits[word] = inside ? UINT64_MAX : 0;
                }
            }
            int count = row_coverage[x - left].count();
            coverage[index] = static_cast<std::uint16_t>(count);
            distance[index] = count ? static_cast<std::uint8_t>(32 * space.scale) : 0;
            if (boundary && count > 0 && count < 1024 && image.contains(x, top + row)) {
                FillBoundaryPixel& pixel = (*boundary)[(top + row) * image.width + x];
                pixel.coverage = row_coverage[x - left];
            }
        }
    }
    // Bounded chamfer distance approximates distance to the wet mask's edge in O(pixels).
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::size_t i = static_cast<std::size_t>(y) * width + x;
            int d = distance[i];
            if (x > 0) {
                d = std::min(d, distance[i - 1] + 1);
            }
            if (y > 0) {
                d = std::min(d, distance[i - width] + 1);
            }
            if (x > 0 && y > 0) {
                d = std::min(d, distance[i - width - 1] + 2);
            }
            distance[i] = static_cast<std::uint8_t>(d);
        }
    }
    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            std::size_t i = static_cast<std::size_t>(y) * width + x;
            int d = distance[i];
            if (x + 1 < width) {
                d = std::min(d, distance[i + 1] + 1);
            }
            if (y + 1 < height) {
                d = std::min(d, distance[i + width] + 1);
            }
            if (x + 1 < width && y + 1 < height) {
                d = std::min(d, distance[i + width + 1] + 2);
            }
            distance[i] = static_cast<std::uint8_t>(d);
        }
    }
    MaterialSurface material(ink, brush, space);
    for (int y = std::max(0, top); y < std::min(image.height, bottom); ++y) {
        for (int x = std::max(0, left); x < std::min(image.width, right); ++x) {
            std::size_t i = static_cast<std::size_t>(y - top) * width + x - left;
            if (!coverage[i]) {
                continue;
            }
            Color color = material.sample(x, y, std::max(0.0, distance[i] - 0.5));
            if (boundary && (*boundary).contains(y * image.width + x)) {
                FillBoundaryPixel& pixel = (*boundary).at(y * image.width + x);
                pixel.original = image.get(x, y);
                pixel.paint = color;
            }
            color.a = channel(color.a * coverage[i] / 1024.0);
            image.blend(x, y, color);
        }
    }
}
} // namespace paint
