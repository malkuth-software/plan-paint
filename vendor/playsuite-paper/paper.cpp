#include "paper.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace paper {
namespace {

constexpr double pi = 3.14159265358979323846;

// Integer hashing: the same bits on every platform.
std::uint32_t mix32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

std::uint32_t lattice_hash(int ix, int iy, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(ix) * 0x9E3779B1U;
    h ^= static_cast<std::uint32_t>(iy) * 0x85EBCA77U;
    h ^= seed * 0xC2B2AE3DU;
    const std::uint32_t mixed = mix32(h);
    return mixed;
}

// 0..1 from 32 bits.
double unit(std::uint32_t h) {
    const double value = static_cast<double>(h) * (1.0 / 4294967296.0);
    return value;
}

struct Random {
    std::uint32_t state = 0;
    double uniform() {
        state += 0x9E3779B9U;
        const std::uint32_t h = mix32(state);
        const double value = unit(h);
        return value;
    }
};

double clamp_finite(double value, double low, double high, double fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    const double clamped = std::clamp(value, low, high);
    return clamped;
}

Parameters sanitized(const Parameters& p) {
    const Parameters d{};
    Parameters s = p;
    s.pixels_per_point = clamp_finite(p.pixels_per_point, 0.25, 8.0, d.pixels_per_point);
    s.colour.red = clamp_finite(p.colour.red, 0.0, 1.0, d.colour.red);
    s.colour.green = clamp_finite(p.colour.green, 0.0, 1.0, d.colour.green);
    s.colour.blue = clamp_finite(p.colour.blue, 0.0, 1.0, d.colour.blue);
    s.mottle = clamp_finite(p.mottle, 0.0, 0.5, d.mottle);
    s.mottle_size = clamp_finite(p.mottle_size, 2.0, 4096.0, d.mottle_size);
    s.tooth = clamp_finite(p.tooth, 0.0, 0.5, d.tooth);
    s.tooth_size = clamp_finite(p.tooth_size, 1.0, 256.0, d.tooth_size);
    s.machine_grain = clamp_finite(p.machine_grain, 1.0, 8.0, d.machine_grain);
    s.web = clamp_finite(p.web, 0.0, 20000.0, d.web);
    s.web_length = clamp_finite(p.web_length, 0.5, 100.0, d.web_length);
    s.web_contrast = clamp_finite(p.web_contrast, 0.0, 0.5, d.web_contrast);
    s.flecks = clamp_finite(p.flecks, 0.0, 500.0, d.flecks);
    s.fleck_contrast = clamp_finite(p.fleck_contrast, 0.0, 0.5, d.fleck_contrast);
    s.grain = clamp_finite(p.grain, 0.0, 0.3, d.grain);
    s.fibres = clamp_finite(p.fibres, 0.0, 500.0, d.fibres);
    s.fibre_length = clamp_finite(p.fibre_length, 1.0, 400.0, d.fibre_length);
    s.fibre_contrast = clamp_finite(p.fibre_contrast, 0.0, 0.5, d.fibre_contrast);
    s.laid_spacing = clamp_finite(p.laid_spacing, 0.0, 4096.0, d.laid_spacing);
    s.chain_spacing = clamp_finite(p.chain_spacing, 0.0, 4096.0, d.chain_spacing);
    s.laid_depth = clamp_finite(p.laid_depth, 0.0, 0.5, d.laid_depth);
    s.blotch = clamp_finite(p.blotch, 0.0, 0.5, d.blotch);
    s.blotch_size = clamp_finite(p.blotch_size, 2.0, 4096.0, d.blotch_size);
    s.warmth = clamp_finite(p.warmth, 0.0, 1.0, d.warmth);
    return s;
}

Shading sanitized(const Shading& shading) {
    const Shading d{};
    Shading s = shading;
    s.gutter_width = clamp_finite(shading.gutter_width, 0.0, 4096.0, d.gutter_width);
    s.gutter_depth = clamp_finite(shading.gutter_depth, 0.0, 0.8, d.gutter_depth);
    s.edge_width = clamp_finite(shading.edge_width, 0.0, 4096.0, d.edge_width);
    s.edge_depth = clamp_finite(shading.edge_depth, 0.0, 0.8, d.edge_depth);
    return s;
}

// One octave of value noise over the whole picture, accumulated into `field` as
// amplitude * (value - 0.5). The lattice is a grid of `cell_px`-pixel cells; across a
// repeating tile the grid is fitted to whole cells so the noise wraps. Corner values
// are hashed once per lattice corner per row, not per pixel.
struct NoiseScratch {
    std::vector<int> ix_of_x;
    std::vector<float> sx_of_x;
    std::vector<float> top;
    std::vector<float> bottom;
};

void add_value_noise(std::vector<float>& field, int w, int h, double cell_px_x, double cell_px_y, bool periodic,
                     std::uint32_t seed, float amplitude, NoiseScratch& scratch) {
    if (amplitude == 0.0f) {
        return;
    }
    int cells_x = 0;
    int cells_y = 0;
    double cell_w = cell_px_x;
    double cell_h = cell_px_y;
    if (periodic) {
        cells_x = std::max(2, static_cast<int>(std::lround(w / cell_px_x)));
        cells_y = std::max(2, static_cast<int>(std::lround(h / cell_px_y)));
        cell_w = static_cast<double>(w) / cells_x;
        cell_h = static_cast<double>(h) / cells_y;
    } else {
        cells_x = static_cast<int>(std::ceil(w / cell_px_x)) + 1;
        cells_y = static_cast<int>(std::ceil(h / cell_px_y)) + 1;
    }
    scratch.ix_of_x.resize(static_cast<std::size_t>(w));
    scratch.sx_of_x.resize(static_cast<std::size_t>(w));
    for (int x = 0; x < w; ++x) {
        const double fx = x / cell_w;
        int ix = static_cast<int>(std::floor(fx));
        ix = std::min(ix, cells_x - 1);
        const double t = fx - ix;
        scratch.ix_of_x[static_cast<std::size_t>(x)] = ix;
        scratch.sx_of_x[static_cast<std::size_t>(x)] = static_cast<float>(t * t * (3.0 - 2.0 * t));
    }
    scratch.top.resize(static_cast<std::size_t>(cells_x) + 1);
    scratch.bottom.resize(static_cast<std::size_t>(cells_x) + 1);
    int row_iy = -1;
    for (int y = 0; y < h; ++y) {
        const double fy = y / cell_h;
        int iy = static_cast<int>(std::floor(fy));
        iy = std::min(iy, cells_y - 1);
        const double ty = fy - iy;
        const float sy = static_cast<float>(ty * ty * (3.0 - 2.0 * ty));
        if (iy != row_iy) {
            row_iy = iy;
            const int iy_next = periodic ? (iy + 1) % cells_y : iy + 1;
            for (int ix = 0; ix <= cells_x; ++ix) {
                const int wrapped = periodic ? ix % cells_x : ix;
                scratch.top[static_cast<std::size_t>(ix)] = static_cast<float>(unit(lattice_hash(wrapped, iy, seed)));
                scratch.bottom[static_cast<std::size_t>(ix)] =
                    static_cast<float>(unit(lattice_hash(wrapped, iy_next, seed)));
            }
        }
        float* row = field.data() + static_cast<std::ptrdiff_t>(y) * w;
        const int* ix_of_x = scratch.ix_of_x.data();
        const float* sx_of_x = scratch.sx_of_x.data();
        const float* top = scratch.top.data();
        const float* bottom = scratch.bottom.data();
        for (int x = 0; x < w; ++x) {
            const int ix = ix_of_x[x];
            const float sx = sx_of_x[x];
            const float a = top[ix];
            const float b = top[ix + 1];
            const float c = bottom[ix];
            const float d = bottom[ix + 1];
            const float upper = a + (b - a) * sx;
            const float lower = c + (d - c) * sx;
            const float value = upper + (lower - upper) * sy;
            row[x] += amplitude * (value - 0.5f);
        }
    }
}

// Three octaves, the finer ones at half and a quarter of the cell, so a repeating
// tile keeps repeating. `stretch` elongates every cell along x (a running grain).
void add_fractal_noise(std::vector<float>& field, int w, int h, double cell_px, double stretch, bool periodic,
                       std::uint32_t seed, float amplitude, NoiseScratch& scratch) {
    add_value_noise(field, w, h, cell_px * stretch, cell_px, periodic, seed, amplitude * 0.55f, scratch);
    add_value_noise(field, w, h, cell_px * 0.5 * stretch, cell_px * 0.5, periodic, seed + 7U, amplitude * 0.30f,
                    scratch);
    add_value_noise(field, w, h, cell_px * 0.25 * stretch, cell_px * 0.25, periodic, seed + 13U,
                    amplitude * 0.15f, scratch);
}

// Fibres: short quadratic curves, each splatted as a soft stroke into a signed field
// (light fibres positive, dark negative). Where strokes overlap the stronger wins, so
// walking along a curve does not pile up where samples overlap.
struct FibreField {
    int w = 0;
    int h = 0;
    bool periodic = false;
    std::vector<float> value;

    void splat(double cx, double cy, double radius, float strength) {
        const int x0 = static_cast<int>(std::floor(cx - radius));
        const int x1 = static_cast<int>(std::ceil(cx + radius));
        const int y0 = static_cast<int>(std::floor(cy - radius));
        const int y1 = static_cast<int>(std::ceil(cy + radius));
        for (int y = y0; y <= y1; ++y) {
            int py = y;
            if (periodic) {
                py = ((y % h) + h) % h;
            } else if (y < 0 || y >= h) {
                continue;
            }
            for (int x = x0; x <= x1; ++x) {
                int px = x;
                if (periodic) {
                    px = ((x % w) + w) % w;
                } else if (x < 0 || x >= w) {
                    continue;
                }
                const double dx = x + 0.5 - cx;
                const double dy = y + 0.5 - cy;
                const double distance = std::sqrt(dx * dx + dy * dy);
                const double coverage = std::clamp(radius - distance + 0.5, 0.0, 1.0);
                if (coverage <= 0.0) {
                    continue;
                }
                const float sample = static_cast<float>(coverage) * strength;
                float& cell = value[static_cast<std::size_t>(py) * w + px];
                if (std::fabs(sample) > std::fabs(cell)) {
                    cell = sample;
                }
            }
        }
    }
};

void draw_fibres(FibreField& field, const Parameters& p, std::uint32_t seed) {
    const double ppp = p.pixels_per_point;
    const double area_points = (field.w / ppp) * (field.h / ppp);
    const int count = static_cast<int>(std::lround(p.fibres * area_points / 10000.0));
    if (count <= 0 || p.fibre_contrast <= 0.0) {
        return;
    }
    Random random{seed * 0x3C6EF372U + 0x1B873593U};
    for (int i = 0; i < count; ++i) {
        const double x = random.uniform() * field.w;
        const double y = random.uniform() * field.h;
        const double length = p.fibre_length * (0.5 + 1.5 * random.uniform()) * ppp;
        const double angle = random.uniform() * pi;
        const double bend = (random.uniform() - 0.5) * 0.3 * length;
        const bool light = random.uniform() > 0.5;
        const double contrast = p.fibre_contrast * (0.5 + random.uniform());
        const double width = (0.45 + 0.55 * random.uniform()) * ppp;
        const double ca = std::cos(angle);
        const double sa = std::sin(angle);
        const double end_x = x + ca * length;
        const double end_y = y + sa * length;
        const double control_x = (x + end_x) * 0.5 - sa * bend;
        const double control_y = (y + end_y) * 0.5 + ca * bend;
        const double radius = width * 0.5 + 0.25;
        const float strength = static_cast<float>(light ? contrast : -contrast);
        const int steps = std::max(2, static_cast<int>(std::ceil(length / 0.6)));
        for (int step = 0; step <= steps; ++step) {
            const double t = static_cast<double>(step) / steps;
            const double u = 1.0 - t;
            const double px = u * u * x + 2.0 * u * t * control_x + t * t * end_x;
            const double py = u * u * y + 2.0 * u * t * control_y + t * t * end_y;
            field.splat(px, py, radius, strength);
        }
    }
}

// The fibre web: tiny fibres accumulated as density. Each is a gently curved segment
// a fraction of a point wide; its coverage is added, so overlaps count.
struct WebField {
    int w = 0;
    int h = 0;
    bool periodic = false;
    std::vector<float> density;

    void add(double cx, double cy, double radius, float weight) {
        const int x0 = static_cast<int>(std::floor(cx - radius - 0.5));
        const int x1 = static_cast<int>(std::ceil(cx + radius + 0.5));
        const int y0 = static_cast<int>(std::floor(cy - radius - 0.5));
        const int y1 = static_cast<int>(std::ceil(cy + radius + 0.5));
        for (int y = y0; y <= y1; ++y) {
            int py = y;
            if (periodic) {
                py = ((y % h) + h) % h;
            } else if (y < 0 || y >= h) {
                continue;
            }
            for (int x = x0; x <= x1; ++x) {
                int px = x;
                if (periodic) {
                    px = ((x % w) + w) % w;
                } else if (x < 0 || x >= w) {
                    continue;
                }
                const double dx = x + 0.5 - cx;
                const double dy = y + 0.5 - cy;
                const double distance = std::sqrt(dx * dx + dy * dy);
                const double coverage = std::clamp(radius - distance + 0.5, 0.0, 1.0);
                if (coverage > 0.0) {
                    density[static_cast<std::size_t>(py) * w + px] += static_cast<float>(coverage) * weight;
                }
            }
        }
    }
};

void draw_web(WebField& field, const Parameters& p, std::uint32_t seed) {
    const double ppp = p.pixels_per_point;
    const double area_points = (field.w / ppp) * (field.h / ppp);
    const int count = static_cast<int>(std::lround(p.web * area_points / 10000.0));
    if (count <= 0) {
        return;
    }
    Random random{seed * 0x7FEB352DU + 0x846CA68BU};
    const double spread = pi / p.machine_grain;
    for (int i = 0; i < count; ++i) {
        const double x = random.uniform() * field.w;
        const double y = random.uniform() * field.h;
        const double length = p.web_length * (0.5 + random.uniform()) * ppp;
        const double angle = (random.uniform() - 0.5) * spread;
        const double bend = (random.uniform() - 0.5) * 0.25 * length;
        const double radius = (0.06 + 0.10 * random.uniform()) * ppp;
        const float weight = static_cast<float>(0.6 + 0.8 * random.uniform());
        const double ca = std::cos(angle);
        const double sa = std::sin(angle);
        const double end_x = x + ca * length;
        const double end_y = y + sa * length;
        const double control_x = (x + end_x) * 0.5 - sa * bend;
        const double control_y = (y + end_y) * 0.5 + ca * bend;
        const int steps = std::max(1, static_cast<int>(std::ceil(length / 0.7)));
        for (int step = 0; step <= steps; ++step) {
            const double t = static_cast<double>(step) / steps;
            const double u = 1.0 - t;
            const double px = u * u * x + 2.0 * u * t * control_x + t * t * end_x;
            const double py = u * u * y + 2.0 * u * t * control_y + t * t * end_y;
            field.add(px, py, radius, weight);
        }
    }
    // Centre the density on its mean so the sheet keeps its colour.
    double sum = 0.0;
    for (float value : field.density) {
        sum += value;
    }
    const double mean = sum / static_cast<double>(field.density.size());
    if (mean <= 0.0) {
        field.density.clear();
        return;
    }
    const float scale = static_cast<float>(1.0 / mean);
    for (float& value : field.density) {
        value = std::clamp(value * scale - 1.0f, -1.0f, 1.0f);
    }
}

// Flecks: short dark specks lying along the machine direction, splatted like fibres.
void draw_flecks(FibreField& field, const Parameters& p, std::uint32_t seed) {
    const double ppp = p.pixels_per_point;
    const double area_points = (field.w / ppp) * (field.h / ppp);
    const int count = static_cast<int>(std::lround(p.flecks * area_points / 10000.0));
    if (count <= 0 || p.fleck_contrast <= 0.0) {
        return;
    }
    Random random{seed * 0x27D4EB2FU + 0x165667B1U};
    for (int i = 0; i < count; ++i) {
        const double x = random.uniform() * field.w;
        const double y = random.uniform() * field.h;
        const double length = (0.3 + 1.2 * random.uniform()) * ppp;
        const double angle = (random.uniform() - 0.5) * 0.9;
        const double radius = (0.2 + 0.35 * random.uniform()) * ppp + 0.1;
        const float strength = static_cast<float>(-p.fleck_contrast * (0.35 + 0.65 * random.uniform()));
        const double ca = std::cos(angle);
        const double sa = std::sin(angle);
        const int steps = std::max(1, static_cast<int>(std::ceil(length / 0.5)));
        for (int step = 0; step <= steps; ++step) {
            const double t = static_cast<double>(step) / steps - 0.5;
            field.splat(x + ca * length * t, y + sa * length * t, radius, strength);
        }
    }
}

// A soft line every `period` pixels: 1 on the line, falling to 0 `half_width` pixels
// away. Fitted to whole periods across a repeating tile.
struct Lines {
    double period = 0.0;
    double half_width = 1.0;
    [[nodiscard]] double at(double position) const {
        const double phase = position - std::floor(position / period) * period;
        const double distance = std::min(phase, period - phase);
        const double pulse = std::clamp(1.0 - distance / half_width, 0.0, 1.0);
        return pulse;
    }
};

Lines fit_lines(double spacing_points, double ppp, int extent, bool periodic, double half_width_points) {
    Lines lines;
    const double spacing_px = spacing_points * ppp;
    if (spacing_px <= 0.0) {
        return lines;
    }
    if (periodic) {
        const int count = std::max(1, static_cast<int>(std::lround(extent / spacing_px)));
        lines.period = static_cast<double>(extent) / count;
    } else {
        lines.period = spacing_px;
    }
    lines.half_width = std::max(0.6, half_width_points * ppp);
    return lines;
}

bool valid_size(int width, int height) {
    const bool ok = width >= 4 && height >= 4 && width <= max_side && height <= max_side;
    return ok;
}

std::uint8_t to_byte(double value) {
    const double rounded = std::lround(std::clamp(value, 0.0, 255.0));
    return static_cast<std::uint8_t>(rounded);
}

void render(const Parameters& given, int w, int h, bool periodic, const Shading& shading, Image& out) {
    const Parameters p = sanitized(given);
    const double ppp = p.pixels_per_point;
    const std::size_t count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    NoiseScratch scratch;

    // Slow tone: the mottle and, for parchment, the blotches.
    std::vector<float> tone(count, 0.0f);
    add_fractal_noise(tone, w, h, p.mottle_size * ppp, 1.0, periodic, p.seed * 3U + 101U,
                      static_cast<float>(2.0 * p.mottle), scratch);
    std::vector<float> blotch;
    if (p.blotch > 0.0) {
        blotch.assign(count, 0.0f);
        add_fractal_noise(blotch, w, h, p.blotch_size * ppp, 1.0, periodic, p.seed * 5U + 211U, 1.0f, scratch);
    }

    // The tooth: fine relief, lit from the upper left by its slope.
    std::vector<float> relief;
    if (p.tooth > 0.0) {
        relief.assign(count, 0.0f);
        add_fractal_noise(relief, w, h, p.tooth_size * ppp, p.machine_grain, periodic, p.seed * 7U + 307U, 1.0f,
                          scratch);
    }

    FibreField fibres;
    fibres.w = w;
    fibres.h = h;
    fibres.periodic = periodic;
    const bool any_fibres = p.fibres > 0.0 && p.fibre_contrast > 0.0;
    const bool any_flecks = p.flecks > 0.0 && p.fleck_contrast > 0.0;
    if (any_fibres || any_flecks) {
        fibres.value.assign(count, 0.0f);
        if (any_fibres) {
            draw_fibres(fibres, p, p.seed);
        }
        if (any_flecks) {
            draw_flecks(fibres, p, p.seed);
        }
    }

    WebField web;
    web.w = w;
    web.h = h;
    web.periodic = periodic;
    if (p.web > 0.0 && p.web_contrast > 0.0) {
        web.density.assign(count, 0.0f);
        draw_web(web, p, p.seed);
    }

    const Lines laid = fit_lines(p.laid_spacing, ppp, h, periodic, 0.55);
    const Lines chain = fit_lines(p.chain_spacing, ppp, w, periodic, 1.1);
    const double chain_depth = p.laid_depth * 1.8;

    const double gutter_px = shading.gutter_width * ppp;
    const double edge_px = shading.edge_width * ppp;

    // Relief slopes between pixels two apart are about 2 / cell of the noise's range;
    // this brings a typical tooth to about `tooth` of full scale.
    const double tooth_gain = p.tooth * 1.6 * (p.tooth_size * ppp);
    const double base_r = p.colour.red * 255.0;
    const double base_g = p.colour.green * 255.0;
    const double base_b = p.colour.blue * 255.0;
    const double green_share = 1.0 - 0.03 * p.warmth;
    const double blue_share = 1.0 - 0.14 * p.warmth;
    const std::uint32_t grain_seed = p.seed * 11U + 401U;

    out.width = w;
    out.height = h;
    out.rgba.resize(count * 4);
    for (int y = 0; y < h; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        const int y_up = periodic ? (y + h - 1) % h : std::max(0, y - 1);
        const int y_down = periodic ? (y + 1) % h : std::min(h - 1, y + 1);
        const double laid_pulse = laid.period > 0.0 ? laid.at(y + 0.5) : 0.0;
        for (int x = 0; x < w; ++x) {
            const std::size_t at = row + static_cast<std::size_t>(x);
            double shade = tone[at];
            if (!relief.empty()) {
                const int x_left = periodic ? (x + w - 1) % w : std::max(0, x - 1);
                const int x_right = periodic ? (x + 1) % w : std::min(w - 1, x + 1);
                const double across = relief[row + static_cast<std::size_t>(x_left)] -
                                      relief[row + static_cast<std::size_t>(x_right)];
                const double down = relief[static_cast<std::size_t>(y_up) * w + x] -
                                    relief[static_cast<std::size_t>(y_down) * w + x];
                shade += tooth_gain * (across + 0.6 * down);
            }
            if (p.grain > 0.0) {
                const double speckle = unit(lattice_hash(x, y, grain_seed)) - 0.5;
                shade += 2.0 * p.grain * speckle;
            }
            if (!fibres.value.empty()) {
                shade += fibres.value[at];
            }
            if (!web.density.empty()) {
                shade += p.web_contrast * web.density[at];
            }
            if (!blotch.empty()) {
                const double n = std::clamp(blotch[at] + 0.5, 0.0, 1.0);
                shade -= p.blotch * (n * n - 0.3);
            }
            if (laid_pulse > 0.0) {
                shade -= p.laid_depth * laid_pulse;
            }
            if (chain.period > 0.0) {
                shade -= chain_depth * chain.at(x + 0.5);
            }
            if (shading.gutter != Gutter::none && gutter_px > 0.0) {
                const double from_binding = shading.gutter == Gutter::left ? x + 0.5 : w - x - 0.5;
                const double t = from_binding / gutter_px;
                if (t < 1.0) {
                    shade -= shading.gutter_depth * (1.0 - t) * (1.0 - t);
                }
            }
            if (edge_px > 0.0) {
                const double from_edge = std::min(std::min(x + 0.5, w - x - 0.5), std::min(y + 0.5, h - y - 0.5));
                const double t = from_edge / edge_px;
                if (t < 1.0) {
                    shade -= shading.edge_depth * (1.0 - t) * (1.0 - t);
                }
            }
            const double delta = shade * 255.0;
            std::uint8_t* pixel = out.rgba.data() + at * 4;
            pixel[0] = to_byte(base_r + delta);
            pixel[1] = to_byte(base_g + delta * green_share);
            pixel[2] = to_byte(base_b + delta * blue_share);
            pixel[3] = 255;
        }
    }
}

} // namespace

const char* preset_name(Preset preset) {
    switch (preset) {
    case Preset::book:
        return "book";
    case Preset::copy:
        return "copy";
    case Preset::recycled:
        return "recycled";
    case Preset::wove:
        return "wove";
    case Preset::laid:
        return "laid";
    case Preset::india:
        return "india";
    case Preset::parchment:
        return "parchment";
    case Preset::rag:
        return "rag";
    case Preset::kraft:
        return "kraft";
    case Preset::night:
        return "night";
    }
    return "wove";
}

Parameters preset(Preset preset) {
    Parameters p;
    switch (preset) {
    case Preset::book:
        // Uncoated book paper: a dense fibre mat seen flat, faint formation clouds a few
        // millimetres across, no lit relief and no visible fibre inclusions.
        p.colour = {0.969, 0.949, 0.894};
        p.mottle = 0.008;
        p.mottle_size = 9.0;
        p.tooth = 0.0;
        p.machine_grain = 2.0;
        p.grain = 0.006;
        p.fibres = 0.0;
        p.web = 6000.0;
        p.web_length = 5.0;
        p.web_contrast = 0.02;
        break;
    case Preset::copy:
        // Bright, slightly cool office paper: a denser, shorter-fibred mat, smoother still.
        p.colour = {0.980, 0.980, 0.969};
        p.mottle = 0.006;
        p.mottle_size = 8.0;
        p.tooth = 0.0;
        p.machine_grain = 1.6;
        p.grain = 0.005;
        p.fibres = 0.0;
        p.web = 8000.0;
        p.web_length = 4.0;
        p.web_contrast = 0.016;
        p.warmth = 0.4;
        break;
    case Preset::recycled:
        // Greyer stock: the same mat with flecks of unbleached fibre and old ink.
        p.colour = {0.925, 0.906, 0.859};
        p.mottle = 0.012;
        p.mottle_size = 9.0;
        p.tooth = 0.0;
        p.machine_grain = 2.0;
        p.grain = 0.007;
        p.web = 6000.0;
        p.web_length = 5.0;
        p.web_contrast = 0.025;
        p.fibres = 2.0;
        p.fibre_length = 4.0;
        p.fibre_contrast = 0.05;
        p.flecks = 40.0;
        p.fleck_contrast = 0.14;
        break;
    case Preset::wove:
        break;
    case Preset::laid:
        p.colour = {0.945, 0.914, 0.831};
        p.mottle = 0.022;
        p.tooth = 0.025;
        p.grain = 0.010;
        p.fibres = 18.0;
        p.laid_spacing = 2.8;
        p.chain_spacing = 28.0;
        p.laid_depth = 0.02;
        break;
    case Preset::india:
        p.colour = {0.941, 0.925, 0.878};
        p.mottle = 0.012;
        p.mottle_size = 40.0;
        p.tooth = 0.010;
        p.tooth_size = 1.8;
        p.grain = 0.008;
        p.fibres = 6.0;
        p.fibre_length = 8.0;
        p.fibre_contrast = 0.04;
        break;
    case Preset::parchment:
        p.colour = {0.910, 0.839, 0.682};
        p.mottle = 0.06;
        p.mottle_size = 70.0;
        p.tooth = 0.02;
        p.grain = 0.012;
        p.fibres = 8.0;
        p.fibre_contrast = 0.08;
        p.blotch = 0.05;
        p.blotch_size = 140.0;
        break;
    case Preset::rag:
        p.colour = {0.980, 0.969, 0.933};
        p.mottle = 0.015;
        p.tooth = 0.035;
        p.tooth_size = 2.4;
        p.grain = 0.014;
        p.fibres = 45.0;
        p.fibre_length = 14.0;
        p.fibre_contrast = 0.085;
        break;
    case Preset::kraft:
        p.colour = {0.769, 0.627, 0.439};
        p.mottle = 0.04;
        p.tooth = 0.035;
        p.grain = 0.02;
        p.fibres = 35.0;
        p.fibre_length = 22.0;
        p.fibre_contrast = 0.09;
        break;
    case Preset::night:
        p.colour = {0.141, 0.125, 0.106};
        p.mottle = 0.012;
        p.tooth = 0.012;
        p.grain = 0.008;
        p.fibres = 12.0;
        p.fibre_contrast = 0.035;
        p.warmth = 0.6;
        break;
    }
    return p;
}

bool generate_tile(const Parameters& parameters, int width, int height, Image& out) {
    if (!valid_size(width, height)) {
        return false;
    }
    const Shading none{};
    render(parameters, width, height, true, none, out);
    return true;
}

bool generate_sheet(const Parameters& parameters, int width, int height, const Shading& shading, Image& out) {
    if (!valid_size(width, height)) {
        return false;
    }
    const Shading safe = sanitized(shading);
    render(parameters, width, height, false, safe, out);
    return true;
}

} // namespace paper
