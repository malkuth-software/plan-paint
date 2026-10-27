#include "carpet.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>
namespace paint {
namespace {
constexpr double pi = std::numbers::pi;
struct Vec {
    double x = 0, y = 0, z = 0;
    Vec operator+(Vec b) const {
        return {x + b.x, y + b.y, z + b.z};
    }
    Vec operator-(Vec b) const {
        return {x - b.x, y - b.y, z - b.z};
    }
    Vec operator*(double b) const {
        return {x * b, y * b, z * b};
    }
};
double dot(Vec a, Vec b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec cross(Vec a, Vec b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec unit(Vec a) {
    return a * (1 / std::max(1e-15, std::sqrt(dot(a, a))));
}
struct Basis {
    Vec r, u, v;
    explicit Basis(Vec direction) {
        v = unit(direction);
        r = unit(cross({0, 1, 0}, v));
        u = cross(v, r);
    }
    Vec project(Vec a) const {
        return {dot(a, r), dot(a, u), dot(a, v)};
    }
};
struct Random {
    std::uint32_t state;
    double uniform() {
        state += 0x6d2b79f5U;
        std::uint32_t t = (state ^ (state >> 15)) * (1U | state);
        t ^= t + (t ^ (t >> 7)) * (61U | t);
        return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
    }
    double normal() {
        const double u = uniform(), v = uniform();
        return std::sqrt(-2 * std::log(std::max(1e-9, u))) * std::cos(6.2831853 * v);
    }
};
double field(double x, double y, double seed) {
    return .52 * std::sin(x * .81 + y * .36 + seed) * std::cos(y * .91 - x * .17 + seed * .4) +
           .28 * std::sin(x * 1.77 - y * 1.21 + seed * .7) + .20 * std::sin(x * 3.03 + y * 2.01 + seed * 1.7);
}
struct Surface {
    Vec tangent, normal;
    double radius = 0, dye = 1;
};
template <bool full> struct Raster {
    int w, h;
    double hw, hh;
    Basis basis;
    std::vector<double> z;
    std::vector<Surface> surface;
    Raster(int width, int height, double half_width, double half_height, Basis axes)
        : w(width), h(height), hw(half_width), hh(half_height), basis(axes),
          z(static_cast<std::size_t>(w) * h, -1e6) {
        if constexpr (full) {
            surface.resize(z.size());
        }
    }
    void segment(Vec a, Vec b, double radius, double dye) {
        a = basis.project(a);
        b = basis.project(b);
        const double sx = w / (2 * hw), sy = h / (2 * hh);
        const int xmin =
            std::max(0, static_cast<int>(std::floor((std::min(a.x, b.x) - radius + hw) * sx - .5)));
        const int xmax =
            std::min(w - 1, static_cast<int>(std::ceil((std::max(a.x, b.x) + radius + hw) * sx - .5)));
        const int ymin =
            std::max(0, static_cast<int>(std::floor((std::min(a.y, b.y) - radius + hh) * sy - .5)));
        const int ymax =
            std::min(h - 1, static_cast<int>(std::ceil((std::max(a.y, b.y) + radius + hh) * sy - .5)));
        if (xmin > xmax || ymin > ymax) {
            return;
        }
        const Vec delta = b - a;
        const double length = std::sqrt(dot(delta, delta));
        if (length < 1e-9) {
            return;
        }
        const Vec t = delta * (1 / length);
        const double A = 1 - t.z * t.z, r2 = radius * radius;
        // A capsule is contained in the convex hull of its endpoint spheres.
        // Reject already occluded samples before solving either intersection.
        const double top = std::max(a.z, b.z) + radius;
        for (int y = ymin; y <= ymax; ++y) {
            const double Y = (y + .5) / sy - hh, ry = Y - a.y;
            for (int x = xmin; x <= xmax; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * w + x;
                if (top < z[index]) {
                    continue;
                }
                const double X = (x + .5) / sx - hw, rx = X - a.x, u = rx * t.x + ry * t.y;
                const double C = rx * rx + ry * ry - u * u - r2, disc = u * u * t.z * t.z - A * C;
                double zz = -1e6;
                Vec normal{0, 0, 1};
                if (disc >= 0 && A > 1e-9) {
                    const double q = (u * t.z + std::sqrt(disc)) / A, s = u + q * t.z;
                    if (s >= 0 && s <= length) {
                        zz = a.z + q;
                        if constexpr (full) {
                            normal = {(rx - t.x * s) / radius, (ry - t.y * s) / radius,
                                      (q - t.z * s) / radius};
                        }
                    }
                }
                const double d0 = rx * rx + ry * ry;
                if (d0 <= r2) {
                    const double q = std::sqrt(r2 - d0);
                    if (a.z + q > zz) {
                        zz = a.z + q;
                        if constexpr (full) {
                            normal = {rx / radius, ry / radius, q / radius};
                        }
                    }
                }
                const double ex = X - b.x, ey = Y - b.y, d1 = ex * ex + ey * ey;
                if (d1 <= r2) {
                    const double q = std::sqrt(r2 - d1);
                    if (b.z + q > zz) {
                        zz = b.z + q;
                        if constexpr (full) {
                            normal = {ex / radius, ey / radius, q / radius};
                        }
                    }
                }
                if (zz > z[index]) {
                    z[index] = zz;
                    if constexpr (full) {
                        surface[index] = {t, normal, radius, dye};
                    }
                }
            }
        }
    }
};
void check_cancel(const std::atomic<bool>* cancel) {
    if (cancel && (*cancel).load(std::memory_order_relaxed)) {
        throw std::runtime_error("Carpet rendering cancelled");
    }
}
void geometry(const CarpetParameters& p, double W, double H, Raster<true>& camera, Raster<false>& shadow,
              const std::atomic<bool>* cancel) {
    Random random{p.seed};
    const int pile = std::min(
        380000, static_cast<int>(std::round(W * H * p.coverage /
                                            (2 * p.radius * std::max(.13, p.height * .60)) * (1 - p.web))));
    const int web = std::min(
        160000, static_cast<int>(std::round(W * H * p.coverage / (2 * p.radius * p.length) * p.web)));
    for (int i = 0; i < pile; ++i) {
        if (i % 1024 == 0) {
            check_cancel(cancel);
        }
        const double x = (random.uniform() - .5) * W, y = (random.uniform() - .5) * H;
        const double nap = field(x * .67, y * .67, p.seed * .17);
        const double theta = -.65 + 1.75 * p.disorder * nap + random.normal() * (.18 + .35 * p.disorder);
        const double zcut = p.height * (1 + .12 * field(x * 2.2, y * 2.2, 11) + .055 * random.normal());
        const double tilt = .47 + .18 * random.uniform() + .16 * nap,
                     r = p.radius * std::exp(.13 * random.normal());
        const double dye = std::exp(.11 * random.normal()), bend = random.normal() * .12 * p.crimp,
                     turn = random.normal() * .5 * p.crimp;
        Vec a{x, y, 0};
        for (int j = 1; j <= 6; ++j) {
            const double t = j / 6.0, f = zcut * tilt * (.5 * t + .5 * t * t), side = bend * std::sin(pi * t),
                         angle = theta + turn * t * t;
            const Vec b{x + std::cos(angle) * f - std::sin(theta) * side,
                        y + std::sin(angle) * f + std::cos(theta) * side, zcut * t};
            camera.segment(a, b, r * (1 - .1 * t), dye);
            shadow.segment(a, b, r * (1 - .1 * t), dye);
            a = b;
        }
    }
    for (int i = 0; i < web; ++i) {
        if (i % 1024 == 0) {
            check_cancel(cancel);
        }
        double x = (random.uniform() - .5) * W, y = (random.uniform() - .5) * H;
        const double length = p.length * std::exp(.35 * random.normal() - .06), ds = length / 22;
        double theta = (random.uniform() - .5) * pi * 2;
        if (p.disorder < .99) {
            theta = theta * p.disorder - .6 * (1 - p.disorder);
        }
        double k = random.normal() * p.crimp * 1.8;
        const double z0 = p.height * (.06 + .90 * random.uniform()),
                     amp = p.height * (.22 + .38 * random.uniform());
        const double phase = random.uniform() * 6.2831853, phase2 = random.uniform() * 6.2831853;
        const double r = p.radius * std::exp(.17 * random.normal()), dye = std::exp(.18 * random.normal());
        const double raw0 = (z0 + amp * std::sin(phase) + amp * .24 * std::sin(phase2)) / p.height;
        Vec a{x, y, p.height * (.5 + .55 * std::tanh((raw0 - .5) * 1.9))};
        const double decay = std::exp(-ds / .40), diffusion = std::sqrt(1 - decay * decay) * p.crimp * 2.2;
        for (int j = 1; j <= 22; ++j) {
            const double t = j / 22.0;
            k = decay * k + diffusion * random.normal();
            theta += k * ds;
            x += std::cos(theta) * ds;
            y += std::sin(theta) * ds;
            const double raw =
                (z0 + amp * std::sin(phase + t * 14.7) + amp * .24 * std::sin(phase2 + t * 25)) / p.height;
            const Vec b{x, y, p.height * (.5 + .55 * std::tanh((raw - .5) * 1.9))};
            camera.segment(a, b, r, dye);
            shadow.segment(a, b, r, dye);
            a = b;
        }
    }
}
double fresnel(double c) {
    c = std::clamp(c, 0.0, 1.0);
    const double ct = std::sqrt(1 - (1 - c * c) / 2.25);
    const double rs = (c - 1.5 * ct) / (c + 1.5 * ct), rp = (1.5 * c - ct) / (1.5 * c + ct);
    return .5 * (rs * rs + rp * rp);
}
double log_i0(double x) {
    x = std::abs(x);
    if (x < 3.75) {
        const double y = x * x / (3.75 * 3.75);
        return std::log(
            1 + y * (3.5156229 +
                     y * (3.0899424 + y * (1.2067492 + y * (.2659732 + y * (.0360768 + y * .0045813))))));
    }
    const double y = 3.75 / x;
    return x - .5 * std::log(x) +
           std::log(
               .39894228 +
               y * (.01328592 +
                    y * (.00225319 +
                         y * (-.00157565 +
                              y * (.00916281 +
                                   y * (-.02057706 + y * (.02635537 + y * (-.01647633 + y * .00392377))))))));
}
double longitudinal(double si, double so, double ci, double co, double variance) {
    return std::exp(log_i0(ci * co / variance) - si * so / variance - 1 / variance) /
           (variance * (1 - std::exp(-2 / variance)));
}
double azimuthal(double phi, double center, double scale) {
    const double d = std::remainder(phi - center, 2 * pi), e = std::exp(-std::abs(d) / scale);
    return e / (scale * (1 + e) * (1 + e) * (1 - 2 / (1 + std::exp(pi / scale))));
}
double linear_channel(std::uint8_t value) {
    const double c = value / 255.0;
    return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
}
std::uint8_t encode(double c) {
    c = std::clamp(c, 0.0, 1.0);
    return static_cast<std::uint8_t>(
        std::lround(255 * (c <= .0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - .055)));
}
void validate(const CarpetParameters& p, int w, int h) {
    const double v[] = {
        p.web, p.radius,   p.height,        p.length, p.disorder, p.crimp,   p.roughness, p.azimuth_roughness,
        p.dye, p.coverage, p.magnification, p.light,  p.view,     p.exposure};
    for (double x : v) {
        if (!std::isfinite(x)) {
            throw std::invalid_argument("Carpet parameters must be finite");
        }
    }
    if (w < 16 || h < 16 || w > 1024 || h > 1024 || p.web < 0 || p.web > 1 || p.radius < .005 ||
        p.radius > .018 || p.height < .25 || p.height > 1.25 || p.length < .5 || p.length > 4 ||
        p.disorder < .05 || p.disorder > 1.15 || p.crimp < .1 || p.crimp > 1.3 || p.roughness < .25 ||
        p.roughness > .75 || p.azimuth_roughness < .1 || p.azimuth_roughness > 1 || p.dye < .5 || p.dye > 2 ||
        p.coverage < 1 || p.coverage > 12 || p.magnification < .45 || p.magnification > 1.8 || p.view < 0 ||
        p.view > 48 || p.exposure < .5 || p.exposure > 2) {
        throw std::invalid_argument("Carpet parameter outside supported range");
    }
}
} // namespace
const char* carpet_preset_name(int index) {
    const char* names[] = {"Royal velvet",    "Brushed sapphire", "Oxblood velvet",  "Billiard green",
                           "Fine wool felt",  "Slate felt",       "Emerald velvet",  "Plum velvet",
                           "Charcoal velvet", "Moss felt",        "Terracotta felt", "Ivory felt"};
    return names[std::clamp(index, 0, carpet_preset_count - 1)];
}
CarpetParameters carpet_preset(int index) {
    CarpetParameters p;
    switch (index) {
    case 1:
        p = {{20, 69, 147, 255}, .12, .012, .86, 2, .76, .50, .46, .49, 1.05, 5.5, .60, 140, 8, 1.2, 241};
        break;
    case 2:
        p = {{155, 24, 59, 255}, .03, .011, .67, 1.6, .43, .32, .40, .43, 1.12, 5.8, .60, 140, 8, 1.1, 895};
        break;
    case 3:
        p = {{24, 119, 79, 255}, .67, .008, .49, 1.85, .62, .58, .53, .62, 1, 10, .60, 140, 8, 1, 326};
        break;
    case 4:
        p = {{172, 152, 119, 255}, 1, .0075, .50, 2.5, 1, .80, .65, .72, .9, 10, .60, 140, 8, 1, 563};
        break;
    case 5:
        p = {{104, 121, 133, 255}, 1, .007, .46, 2.2, .87, .66, .58, .66, 1.02, 10, .60, 140, 8, 1, 103};
        break;
    case 6:
        p = carpet_preset(0);
        p.color = {16, 112, 75, 255};
        p.seed = 927;
        break;
    case 7:
        p = carpet_preset(2);
        p.color = {101, 39, 105, 255};
        p.seed = 337;
        break;
    case 8:
        p = carpet_preset(1);
        p.color = {53, 59, 70, 255};
        p.seed = 445;
        break;
    case 9:
        p = carpet_preset(4);
        p.color = {97, 120, 76, 255};
        p.seed = 642;
        break;
    case 10:
        p = carpet_preset(4);
        p.color = {169, 91, 66, 255};
        p.seed = 557;
        break;
    case 11:
        p = carpet_preset(5);
        p.color = {214, 205, 180, 255};
        p.dye = .65;
        p.seed = 833;
        break;
    default:
        break;
    }
    return p;
}
double horn_hillshade(const std::array<double, 9>& a, double cell_x, double cell_y, double altitude,
                      double azimuth) {
    if (!(cell_x > 0) || !(cell_y > 0) || !std::isfinite(cell_x) || !std::isfinite(cell_y) ||
        !std::isfinite(altitude) || !std::isfinite(azimuth)) {
        throw std::invalid_argument("Invalid hillshade geometry");
    }
    const double dx = ((a[2] + 2 * a[5] + a[8]) - (a[0] + 2 * a[3] + a[6])) / (8 * cell_x);
    const double dy = ((a[6] + 2 * a[7] + a[8]) - (a[0] + 2 * a[1] + a[2])) / (8 * cell_y);
    const double alt = altitude * pi / 180, az = azimuth * pi / 180;
    return std::clamp(
        (-dx * std::sin(az) * std::cos(alt) + dy * std::cos(az) * std::cos(alt) + std::sin(alt)) /
            std::sqrt(1 + dx * dx + dy * dy),
        0.0, 1.0);
}
Image render_carpet(const CarpetParameters& p, int width, int height, const std::atomic<bool>* cancel) {
    validate(p, width, height);
    check_cancel(cancel);
    const int ss = 2, w = width * ss, h = height * ss;
    const double ww = 10 / p.magnification, hh = ww * height / width, margin = 2.3 + p.height,
                 W = ww + 2 * margin, H = hh + 2 * margin;
    const double va = p.view * pi / 180, la = p.light * pi / 180, el = 50 * pi / 180;
    Raster<true> camera(w, h, ww / 2, hh / 2, Basis({0, std::sin(va), std::cos(va)}));
    Raster<false> shadow(900, 900, W * .56, H * .66 + p.height * 1.2,
                         Basis({std::cos(la) * std::cos(el), std::sin(la) * std::cos(el), std::sin(el)}));
    geometry(p, W, H, camera, shadow, cancel);
    const double col[3] = {linear_channel(p.color.r), linear_channel(p.color.g), linear_channel(p.color.b)};
    double sigma[3], bed[3];
    for (int k = 0; k < 3; ++k) {
        sigma[k] = -std::log(std::max(.0002, col[k])) * .28 * p.dye;
        const double t = std::exp(-sigma[k] * p.radius / .010), omega = .04 + .9216 * t / (1 - .04 * t),
                     q = std::sqrt(std::max(.0001, 1 - omega));
        bed[k] = (1 - q) / (1 + q);
    }
    const Vec L = camera.basis.project(shadow.basis.v), sr = camera.basis.project(shadow.basis.r),
              su = camera.basis.project(shadow.basis.u);
    const double px = 2 * camera.hw / w, py = 2 * camera.hh / h;
    const double base =
                     .726 * p.roughness + .812 * p.roughness * p.roughness + 3.7 * std::pow(p.roughness, 20),
                 variance = std::max(.026, base * base);
    const double az = std::max(.06, .626657 * (.265 * p.azimuth_roughness +
                                               1.194 * p.azimuth_roughness * p.azimuth_roughness +
                                               5.372 * std::pow(p.azimuth_roughness, 22)));
    const int offsets[8][2] = {{-3, 0}, {3, 0}, {0, -3}, {0, 3}, {-5, -5}, {5, 5}, {-5, 5}, {5, -5}};
    Image image;
    image.reset(width, height, {});
    // Only two output subpixel rows are accumulated; no full RGB float image.
    std::vector<std::array<double, 3>> row(width);
    for (int y = 0; y < h; ++y) {
        check_cancel(cancel);
        if (y % ss == 0) {
            std::fill(row.begin(), row.end(), std::array<double, 3>{});
        }
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            const Surface& s = camera.surface[i];
            double result[3] = {bed[0] * .32, bed[1] * .32, bed[2] * .32};
            if (s.radius > 0) {
                const Vec pos{(x + .5) * px - camera.hw, (y + .5) * py - camera.hh, camera.z[i]};
                const int qx = static_cast<int>(std::floor((dot(pos, sr) / shadow.hw * .5 + .5) * shadow.w)),
                          qy = static_cast<int>(std::floor((dot(pos, su) / shadow.hh * .5 + .5) * shadow.h));
                const double qz = dot(pos, L);
                double visibility = 0, ao = 0;
                for (int by = -1; by <= 1; ++by) {
                    for (int bx = -1; bx <= 1; ++bx) {
                        const int a = qx + bx, b = qy + by;
                        if (a < 0 || a >= shadow.w || b < 0 || b >= shadow.h) {
                            visibility += 1;
                        } else {
                            visibility +=
                                std::exp(-std::max(0.0, shadow.z[static_cast<std::size_t>(b) * shadow.w + a] -
                                                            qz - s.radius * .8) /
                                         (p.radius * 2));
                        }
                    }
                }
                visibility /= 9;
                for (int j = 0; j < 8; ++j) {
                    const int a = std::clamp(x + offsets[j][0], 0, w - 1),
                              b = std::clamp(y + offsets[j][1], 0, h - 1);
                    ao += std::max(0.0, camera.z[static_cast<std::size_t>(b) * w + a] - pos.z) /
                          (std::hypot(offsets[j][0] * px, offsets[j][1] * py) + .05);
                }
                ao = std::exp(-2.2 * ao / 8);
                const double so = std::clamp(s.tangent.z, -.998, .998), co = std::sqrt(1 - so * so),
                             si = std::clamp(s.tangent.x * L.x + s.tangent.y * L.y + so * L.z, -.998, .998),
                             ci = std::sqrt(1 - si * si);
                const double off = std::clamp((-s.normal.x * s.tangent.y + s.normal.y * s.tangent.x) / co,
                                              -.99, .99),
                             go = std::asin(off), gt = std::asin(off / (std::sqrt(2.25 - so * so) / co));
                const double F = fresnel(co * std::sqrt(1 - off * off)),
                             chord = s.radius / .010 * std::cos(gt) / std::sqrt(1 - so * so / 2.25) * s.dye;
                const double phi =
                    std::atan2((L.x * s.tangent.y - L.y * s.tangent.x) / co,
                               (-L.x * s.tangent.x * so - L.y * s.tangent.y * so) / co + L.z * co);
                const double R = longitudinal(si, so, ci, co, variance) * azimuthal(phi, -2 * go, az),
                             TT = longitudinal(si, so, ci, co, .25 * variance) *
                                  azimuthal(phi, pi + 2 * gt - 2 * go, az);
                const double wide = longitudinal(si, so, ci, co, 4 * variance),
                             TRT = wide * azimuthal(phi, 2 * pi + 4 * gt - 2 * go, az),
                             tail = wide / (2 * pi);
                double relief = 1;
                if (p.hillshade) {
                    std::array<double, 9> heights{};
                    for (int by = -1; by <= 1; ++by) {
                        for (int bx = -1; bx <= 1; ++bx) {
                            // Raster +y is north; Horn's rows run south. Missing hits
                            // extend the local height, avoiding artificial abyss edges.
                            const int a = std::clamp(x + bx, 0, w - 1), b = std::clamp(y - by, 0, h - 1);
                            const double z = camera.z[static_cast<std::size_t>(b) * w + a];
                            heights[(by + 1) * 3 + bx + 1] = z < -1e5 ? pos.z : z;
                        }
                    }
                    const double dx = ((heights[2] + 2 * heights[5] + heights[8]) -
                                       (heights[0] + 2 * heights[3] + heights[6])) /
                                      (8 * px);
                    const double dy = ((heights[6] + 2 * heights[7] + heights[8]) -
                                       (heights[0] + 2 * heights[1] + heights[2])) /
                                      (8 * py);
                    // L is already transformed into the camera frame. Rows in
                    // heights run southward, so the normal is (-dx,+dy,1).
                    relief = .30 +
                             .70 * std::clamp((-dx * L.x + dy * L.y + L.z) / std::sqrt(1 + dx * dx + dy * dy),
                                              0.0, 1.0);
                }
                for (int k = 0; k < 3; ++k) {
                    const double t = std::exp(-sigma[k] * chord), a1 = (1 - F) * (1 - F) * t, a2 = a1 * F * t,
                                 a3 = a2 * F * t / (1 - F * t);
                    const double incoming = .008 * ao + bed[k] * (.65 * ao + .22 * visibility + .12),
                                 key = F * R + a1 * TT + a2 * TRT + a3 * tail;
                    result[k] =
                        ((F + a1 + a2 + a3) * incoming + key * 2.8 * (.03 + .97 * visibility)) * relief;
                }
            }
            for (int k = 0; k < 3; ++k) {
                row[x / ss][k] += result[k];
            }
        }
        if (y % ss == ss - 1) {
            for (int x = 0; x < width; ++x) {
                std::uint8_t c[3];
                for (int k = 0; k < 3; ++k) {
                    double value = row[x][k] / (ss * ss) * p.exposure;
                    c[k] = encode(value / (1 + .65 * value));
                }
                image.set(x, height - 1 - y / ss, {c[0], c[1], c[2], 255});
            }
        }
    }
    return image;
}
Image render_carpet_tile(const CarpetParameters& p, int side, const std::atomic<bool>* cancel) {
    constexpr int overlap = 32;
    if (side < 16 || side > 1024 - overlap) {
        throw std::invalid_argument("Carpet tile size outside supported range");
    }
    const Image source = render_carpet(p, side + overlap, side + overlap, cancel);
    Image tile;
    tile.reset(side, side, {});
    for (int y = 0; y < side; ++y) {
        double wy = std::min(1.0, static_cast<double>(y) / overlap);
        wy = wy * wy * (3 - 2 * wy);
        for (int x = 0; x < side; ++x) {
            double wx = std::min(1.0, static_cast<double>(x) / overlap);
            wx = wx * wx * (3 - 2 * wx);
            const Color c[4] = {source.get(x, y), source.get(x < overlap ? x + side : x, y),
                                source.get(x, y < overlap ? y + side : y),
                                source.get(x < overlap ? x + side : x, y < overlap ? y + side : y)};
            const double weights[4] = {wx * wy, (1 - wx) * wy, wx * (1 - wy), (1 - wx) * (1 - wy)};
            double r = 0, g = 0, b = 0;
            for (int i = 0; i < 4; ++i) {
                r += c[i].r * weights[i];
                g += c[i].g * weights[i];
                b += c[i].b * weights[i];
            }
            tile.set(x, y,
                     {static_cast<std::uint8_t>(std::lround(r)), static_cast<std::uint8_t>(std::lround(g)),
                      static_cast<std::uint8_t>(std::lround(b)), 255});
        }
    }
    return tile;
}
} // namespace paint
