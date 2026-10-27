// PolyForm Noncommercial License 1.0.0 (LICENSE). Copyright (c) 2026 joshuah.rainstar@gmail.com.
// Native adaptation by Astra, sponsored by Joshuah. Thanks to Hashem.
#include "warp.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace paint {
namespace {
using Index = std::size_t;
using Byte = unsigned char;
constexpr double EPS = std::numeric_limits<double>::epsilon();
constexpr double STORAGE_EPS = 65536.0 * EPS;
static void bernstein(double t, double values[6]) {
    double u = 1.0 - t, t2 = t * t, u2 = u * u;
    values[0] = u2 * u2 * u;
    values[1] = 5.0 * u2 * u2 * t;
    values[2] = 10.0 * u2 * u * t2;
    values[3] = 10.0 * u2 * t2 * t;
    values[4] = 5.0 * u * t2 * t2;
    values[5] = t2 * t2 * t;
}
// Exact source-state construction from conv_warp/reference_source.c. All
// previous float storage is double here; no global mutable state remains.
struct AtlasBuilder {
    int sw = 0, sh = 0, lw = 0, lh = 0, build_phase = 0, build_lane = 0, build_channel = 0;
    std::size_t native_groups = 0;
    std::vector<double> native_control, native_source, stage, line, first, second;
    std::vector<double> raw, admitted, refined, beta, ring, capacity, cone_cache;
    std::vector<Byte> cone_counts;
    std::vector<int> coarse, locations, boundary_sign;
    int constant_channel[4] = {1, 1, 1, 1};
    double inv[6][6] = {}, native_diagnostic[5] = {};
    double channel_minimum = 0.0, completion_ray = 1.0;
    explicit AtlasBuilder(const Image& image) {
        sw = std::max(5, image.width);
        sh = std::max(5, image.height);
        lw = 5 * (sw - 1) + 1;
        lh = 5 * (sh - 1) + 1;
        std::size_t pixels = static_cast<std::size_t>(sw) * sh;
        if (pixels > 1048576) {
            throw std::length_error(
                "CONV warp material exceeds the 1 megapixel atlas limit; split the selection.");
        }
        native_groups = sh * (sw - 1) + sw * (sh - 1) + (sw - 1) * (sh - 1);
        native_source.resize(pixels * 4);
        native_control.resize(static_cast<std::size_t>(lw) * lh * 4);
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                Color color = image.get(std::min(x, image.width - 1), std::min(y, image.height - 1));
                double alpha = color.a / 255.0;
                std::size_t index = (static_cast<std::size_t>(y) * sw + x) * 4;
                native_source[index] = color.r / 255.0 * alpha;
                native_source[index + 1] = color.g / 255.0 * alpha;
                native_source[index + 2] = color.b / 255.0 * alpha;
                native_source[index + 3] = alpha;
            }
        }
        stage.resize(std::max(static_cast<std::size_t>(lw) * sh, static_cast<std::size_t>(sw) * lh) * 4);
        beta.resize(pixels);
        ring.resize(static_cast<std::size_t>(lw) * 24);
        std::size_t axis = static_cast<std::size_t>(std::max(sw, sh));
        line.resize(axis);
        first.resize(axis);
        second.resize(axis);
        raw.resize(axis * 5);
        admitted.resize(axis * 5);
        refined.resize(axis * 5);
        coarse.resize(axis);
        locations.resize(axis);
        boundary_sign.resize(axis);
        capacity.resize(native_groups);
        cone_cache.resize(static_cast<std::size_t>(sw - 1) * (sh - 1) * 4);
        cone_counts.resize(static_cast<std::size_t>(sw - 1) * (sh - 1));
        inverse_collocation();
    }
    double src(int x, int y, int ch) {
        return native_source[(static_cast<std::size_t>(y) * sw + x) * 4 + ch];
    }
    int signum(double x) {
        return (x > 0) - (x < 0);
    }
    double base_control(Index id, int ch) {
        int x = static_cast<int>(id % lw), y = static_cast<int>(id / lw), cx = std::min(sw - 2, x / 5),
            cy = std::min(sh - 2, y / 5);
        double u = (x - 5 * cx) / 5.0, v = (y - 5 * cy) / 5.0;
        return (1 - u) * (1 - v) * src(cx, cy, ch) + u * (1 - v) * src(cx + 1, cy, ch) +
               (1 - u) * v * src(cx, cy + 1, ch) + u * v * src(cx + 1, cy + 1, ch);
    }
    int owner(Index id) {
        int x = static_cast<int>(id % lw), y = static_cast<int>(id / lw), rx = x % 5, ry = y % 5;
        if (!rx && !ry) {
            return -1;
        }
        if (!ry) {
            return (y / 5) * (sw - 1) + x / 5;
        }
        int horizontal = sh * (sw - 1);
        if (!rx) {
            return horizontal + (x / 5) * (sh - 1) + y / 5;
        }
        return horizontal + sw * (sh - 1) + (y / 5) * (sw - 1) + x / 5;
    }
    void inverse_collocation(void) {
        double a[6][12] = {};
        for (int r = 0; r < 6; r++) {
            bernstein(r / 5.0, a[r]);
            for (int c = 0; c < 6; c++) {
                a[r][6 + c] = r == c;
            }
        }
        for (int c = 0; c < 6; c++) {
            int p = c;
            for (int r = c + 1; r < 6; r++) {
                if (std::abs(a[r][c]) > std::abs(a[p][c])) {
                    p = r;
                }
            }
            for (int k = 0; k < 12; k++) {
                double t = a[c][k];
                a[c][k] = a[p][k];
                a[p][k] = t;
            }
            double d = a[c][c];
            for (int k = 0; k < 12; k++) {
                a[c][k] /= d;
            }
            for (int r = 0; r < 6; r++) {
                if (r != c) {
                    double f = a[r][c];
                    for (int k = 0; k < 12; k++) {
                        a[r][k] -= f * a[c][k];
                    }
                }
            }
        }
        for (int r = 0; r < 6; r++) {
            for (int c = 0; c < 6; c++) {
                inv[r][c] = a[r][c + 6];
            }
        }
    }
    /* Input is the complete original line, including sign witnesses across plateaus. */
    int refine_line(int n) {
        int intervals = n - 1;
        first[0] = (-3 * line[0] + 4 * line[1] - line[2]) / 2;
        first[1] = (line[2] - line[0]) / 2;
        first[n - 2] = (line[n - 1] - line[n - 3]) / 2;
        first[n - 1] = (3 * line[n - 1] - 4 * line[n - 2] + line[n - 3]) / 2;
        second[0] = 2 * line[0] - 5 * line[1] + 4 * line[2] - line[3];
        second[1] = line[0] - 2 * line[1] + line[2];
        second[n - 2] = line[n - 3] - 2 * line[n - 2] + line[n - 1];
        second[n - 1] = 2 * line[n - 1] - 5 * line[n - 2] + 4 * line[n - 3] - line[n - 4];
        for (int i = 2; i < n - 2; i++) {
            first[i] = (line[i - 2] - 8 * line[i - 1] + 8 * line[i + 1] - line[i + 2]) / 12;
            second[i] =
                (-line[i + 2] + 16 * line[i + 1] - 30 * line[i] + 16 * line[i - 1] - line[i - 2]) / 12;
        }
        for (int i = 0; i < intervals; i++) {
            double b[6] = {line[i],
                           line[i] + first[i] / 5,
                           line[i] + 2 * first[i] / 5 + second[i] / 20,
                           line[i + 1] - 2 * first[i + 1] / 5 + second[i + 1] / 20,
                           line[i + 1] - first[i + 1] / 5,
                           line[i + 1]};
            for (int k = 0; k < 5; k++) {
                raw[5 * i + k] = b[k + 1] - b[k];
                admitted[5 * i + k] = 0;
            }
            coarse[i] = signum(line[i + 1] - line[i]);
        }
        int witness = 0;
        while (witness < intervals && !coarse[witness]) {
            witness++;
        }
        if (witness < intervals) {
            for (int i = 0; i < witness; i++) {
                coarse[i] = coarse[witness];
            }
            for (int i = witness + 1; i < intervals; i++) {
                if (!coarse[i]) {
                    coarse[i] = coarse[i - 1];
                }
            }
            int count = 0, previous = 0;
            for (int knot = 1; knot < intervals; knot++) {
                if (coarse[knot - 1] != coarse[knot]) {
                    int centre = 5 * knot, best = centre;
                    double best_cost = 1e300;
                    for (int candidate = std::max(previous + 1, centre - 4);
                         candidate < std::min(5 * intervals, centre + 5); candidate++) {
                        double cost = 0;
                        for (int k = std::max(0, centre - 5); k < std::min(5 * intervals, centre + 5); k++) {
                            if ((k < candidate ? coarse[knot - 1] : coarse[knot]) * raw[k] < 0) {
                                cost += raw[k] * raw[k];
                            }
                        }
                        if (cost < best_cost) {
                            best_cost = cost;
                            best = candidate;
                        }
                    }
                    locations[count] = best;
                    boundary_sign[count++] = coarse[knot];
                    previous = best;
                }
            }
            int boundary = 0, current_sign = coarse[witness];
            for (int cell = 0; cell < intervals; cell++) {
                int signs[5] = {}, order[5] = {}, masks[6] = {}, mask = 0, mask_count = 0;
                double target[5] = {}, best_values[5] = {}, best_error = 1e300;
                for (int k = 0; k < 5; k++) {
                    while (boundary < count && 5 * cell + k >= locations[boundary]) {
                        current_sign = boundary_sign[boundary++];
                    }
                    signs[k] = current_sign;
                    target[k] = signs[k] * raw[5 * cell + k];
                    order[k] = k;
                    if (signs[k] > 0) {
                        mask |= 1 << k;
                    }
                }
                for (int k = 1; k < 5; k++) {
                    int e = order[k], p = k;
                    while (p > 0 && raw[5 * cell + order[p - 1]] > raw[5 * cell + e]) {
                        order[p] = order[p - 1];
                        p--;
                    }
                    order[p] = e;
                }
                if (mask) {
                    masks[mask_count++] = mask;
                }
                for (int k = 0; k < 5; k++) {
                    mask ^= 1 << order[k];
                    if (mask) {
                        masks[mask_count++] = mask;
                    }
                }
                double total = line[cell + 1] - line[cell];
                int found = 0;
                for (int m = 0; m < mask_count; m++) {
                    mask = masks[m];
                    double numerator = -total;
                    int active = 0;
                    for (int k = 0; k < 5; k++) {
                        if (mask & (1 << k)) {
                            numerator += signs[k] * target[k];
                            active++;
                        }
                    }
                    double lambda = numerator / active, value[5] = {}, mass = 0, error = 0;
                    int valid = 1;
                    for (int k = 0; k < 5; k++) {
                        value[k] = (mask & (1 << k)) ? target[k] - lambda * signs[k] : 0;
                        if (value[k] < -64 * EPS) {
                            valid = 0;
                            break;
                        }
                        value[k] = std::max(0.0, value[k]);
                        mass += signs[k] * value[k];
                        double residual = value[k] - target[k];
                        error += residual * residual;
                    }
                    if (valid && std::abs(mass - total) <= 256 * EPS * std::max(1.0, std::abs(total)) &&
                        error < best_error) {
                        found = 1;
                        best_error = error;
                        for (int k = 0; k < 5; k++) {
                            best_values[k] = value[k];
                        }
                    }
                }
                if (!found) {
                    return 1;
                }
                for (int k = 0; k < 5; k++) {
                    admitted[5 * cell + k] = signs[k] * best_values[k];
                }
            }
        }
        for (int i = 0; i < intervals; i++) {
            for (int r = 0; r < 5; r++) {
                if (!r) {
                    refined[5 * i] = line[i];
                    continue;
                }
                double b[6] = {}, suffix = 0, value = line[i];
                bernstein(r / 5.0, b);
                for (int k = 4; k >= 0; k--) {
                    suffix += b[k + 1];
                    value += suffix * admitted[5 * i + k];
                }
                refined[5 * i + r] = value;
            }
        }
        refined[5 * intervals] = line[n - 1];
        return 0;
    }
    double source_axis_value(int x, int y, int ch, int vertical, int index) const {
        int sx = vertical ? x : index;
        int sy = vertical ? index : y;
        double value = native_source[(static_cast<std::size_t>(sy) * sw + sx) * 4 + ch];
        return value;
    }
    double source_jet(int x, int y, int ch, int vertical) {
        int i = vertical ? y : x, n = vertical ? sh : sw;
        if (i == 0) {
            return (-3.0 * source_axis_value(x, y, ch, vertical, 0) +
                    4.0 * source_axis_value(x, y, ch, vertical, 1) -
                    source_axis_value(x, y, ch, vertical, 2)) /
                   2.0;
        }
        if (i == n - 1) {
            return (3.0 * source_axis_value(x, y, ch, vertical, i) -
                    4.0 * source_axis_value(x, y, ch, vertical, i - 1) +
                    source_axis_value(x, y, ch, vertical, i - 2)) /
                   2.0;
        }
        if (i == 1 || i == n - 2) {
            return (source_axis_value(x, y, ch, vertical, i + 1) -
                    source_axis_value(x, y, ch, vertical, i - 1)) /
                   2.0;
        }
        double value = (source_axis_value(x, y, ch, vertical, i - 2) -
                        8.0 * source_axis_value(x, y, ch, vertical, i - 1) +
                        8.0 * source_axis_value(x, y, ch, vertical, i + 1) -
                        source_axis_value(x, y, ch, vertical, i + 2)) /
                       12.0;
        return value;
    }
    double blend(int x, int y) {
        int cx = std::min(sw - 2, x / 5), cy = std::min(sh - 2, y / 5);
        double u = (x - 5 * cx) / 5.0, v = (y - 5 * cy) / 5.0;
        return (1 - u) * (1 - v) * beta[cy * sw + cx] + u * (1 - v) * beta[cy * sw + cx + 1] +
               (1 - u) * v * beta[(cy + 1) * sw + cx] + u * v * beta[(cy + 1) * sw + cx + 1];
    }
    /* Geometry of the smallest containing positive cone, without atan2/trig. */
    typedef struct {
        double x, y;
    } direction;
    int half(direction a) {
        return a.y < 0 || (a.y == 0 && a.x < 0);
    }
    double cross(direction a, direction b) {
        return a.x * b.y - a.y * b.x;
    }
    int normals(int cx, int cy, int ch, direction* result) {
        if (constant_channel[ch]) {
            return 0;
        }
        direction dirs[100] = {}, anchor = {0, 0}, upper = {0, 0}, lower = {0, 0};
        int count = 0, has_upper = 0, has_lower = 0;
        const double witness_tolerance = 16384 * EPS;
        int x0 = std::max(0, cx - 2), x1 = std::min(sw - 1, cx + 3), y0 = std::max(0, cy - 2),
            y1 = std::min(sh - 1, cy + 3);
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                double a = src(x, y, ch), b = src(x + 1, y, ch), d = src(x, y + 1, ch),
                       e = src(x + 1, y + 1, ch);
                direction v[4] = {{b - a, d - a}, {b - a, e - b}, {e - d, d - a}, {e - d, e - b}};
                for (int k = 0; k < 4; k++) {
                    double length = std::sqrt(v[k].x * v[k].x + v[k].y * v[k].y);
                    if (length <= 2048 * EPS) {
                        continue;
                    }
                    v[k].x /= length;
                    v[k].y /= length;
                    // Three rays with strictly positive cyclic cross products already span
                    // the plane. More support vectors cannot restore a directional bound.
                    // Keep the original sorted construction for every restricted cone.
                    if (!count) {
                        anchor = v[k];
                    } else {
                        double side = cross(anchor, v[k]);
                        if (side > witness_tolerance && (!has_upper || cross(upper, v[k]) > 0)) {
                            upper = v[k];
                            has_upper = 1;
                        }
                        if (side < -witness_tolerance && (!has_lower || cross(v[k], lower) > 0)) {
                            lower = v[k];
                            has_lower = 1;
                        }
                        if (has_upper && has_lower && cross(upper, lower) > witness_tolerance) {
                            return 0;
                        }
                    }
                    int p = count;
                    while (p > 0 && (half(dirs[p - 1]) > half(v[k]) ||
                                     (half(dirs[p - 1]) == half(v[k]) && cross(dirs[p - 1], v[k]) < 0))) {
                        dirs[p] = dirs[p - 1];
                        p--;
                    }
                    dirs[p] = v[k];
                    count++;
                }
            }
        }
        if (!count) {
            return 0;
        }
        direction start = dirs[0], last = dirs[count - 1];
        int cut = -1, opposite = -1;
        double tol = 4096 * EPS;
        for (int i = 0; i < count; i++) {
            direction a = dirs[i], b = dirs[(i + 1) % count];
            double cr = cross(a, b), dot = a.x * b.x + a.y * b.y;
            if (cr < -tol) {
                cut = i;
                break;
            }
            if (std::abs(cr) <= tol && dot < 0) {
                opposite = i;
            }
        }
        if (cut >= 0) {
            start = dirs[(cut + 1) % count];
            last = dirs[cut];
        } else if (opposite >= 0) {
            start = dirs[(opposite + 1) % count];
            last = dirs[opposite];
            result[0] = direction{-start.y, start.x};
            for (int i = 0; i < count; i++) {
                if (cross(start, dirs[i]) > tol) {
                    return 1;
                }
            }
            result[1] = direction{start.y, -start.x};
            return 2;
        } else {
            for (int i = 1; i < count; i++) {
                if (std::abs(cross(start, dirs[i])) > tol || start.x * dirs[i].x + start.y * dirs[i].y < 0) {
                    return 0;
                }
            }
            result[0] = direction{-start.y, start.x};
            result[1] = direction{start.y, -start.x};
            result[2] = start;
            return 3;
        }
        result[0] = direction{-start.y, start.x};
        result[1] = direction{last.y, -last.x};
        return 2;
    }
    void clipped_cell(int cx, int cy) {
        double low[4] = {1e300, 1e300, 1e300, 1e300}, high[4] = {-1e300, -1e300, -1e300, -1e300};
        for (int y = std::max(0, cy - 2); y <= std::min(sh - 1, cy + 3); y++) {
            for (int x = std::max(0, cx - 2); x <= std::min(sw - 1, cx + 3); x++) {
                for (int ch = 0; ch < 4; ch++) {
                    double v = src(x, y, ch);
                    low[ch] = std::min(low[ch], v);
                    high[ch] = std::max(high[ch], v);
                }
            }
        }
        for (int j = 0; j < 6; j++) {
            for (int i = 0; i < 6; i++) {
                Index id = static_cast<std::size_t>(cy * 5 + j) * lw + cx * 5 + i;
                for (int ch = 0; ch < 4; ch++) {
                    if (constant_channel[ch]) {
                        native_control[id * 4 + ch] = src(0, 0, ch);
                        continue;
                    }
                    double v = native_control[id * 4 + ch], bounded = std::clamp(v, low[ch], high[ch]);
                    native_diagnostic[1] += v != bounded;
                    native_control[id * 4 + ch] = bounded;
                    if (!(i % 5) && !(j % 5)) {
                        native_control[id * 4 + ch] = src(cx + i / 5, cy + j / 5, ch);
                    }
                }
            }
        }
    }
    typedef struct {
        double candidate[36], base[36], limited[36];
        int group[36];
    } admission_patch;
    void add_constraint_term(Index ids[5], double weights[5], int& count, int x, int y, double value) {
        if (value == 0.0) {
            return;
        }
        Index id = static_cast<std::size_t>(y * 6 + x);
        int index = 0;
        while (index < count && ids[index] != id) {
            ++index;
        }
        if (index == count) {
            ids[count] = id;
            weights[count] = value;
            ++count;
        } else {
            weights[index] += value;
        }
    }
    void constraint_row(int i, int j, direction normal, int phase, admission_patch* patch) {
        Index ids[5] = {};
        double weights[5] = {};
        int count = 0;
        double dx = normal.x, dy = normal.y;
        if (!i) {
            add_constraint_term(ids, weights, count, 0, j, -5 * dx);
            add_constraint_term(ids, weights, count, 1, j, 5 * dx);
        } else if (i == 5) {
            add_constraint_term(ids, weights, count, 4, j, -5 * dx);
            add_constraint_term(ids, weights, count, 5, j, 5 * dx);
        } else {
            add_constraint_term(ids, weights, count, i - 1, j, -i * dx);
            add_constraint_term(ids, weights, count, i, j, (2 * i - 5) * dx);
            add_constraint_term(ids, weights, count, i + 1, j, (5 - i) * dx);
        }
        if (!j) {
            add_constraint_term(ids, weights, count, i, 0, -5 * dy);
            add_constraint_term(ids, weights, count, i, 1, 5 * dy);
        } else if (j == 5) {
            add_constraint_term(ids, weights, count, i, 4, -5 * dy);
            add_constraint_term(ids, weights, count, i, 5, 5 * dy);
        } else {
            add_constraint_term(ids, weights, count, i, j - 1, -j * dy);
            add_constraint_term(ids, weights, count, i, j, (2 * j - 5) * dy);
            add_constraint_term(ids, weights, count, i, j + 1, (5 - j) * dy);
        }

        double candidate_margin = 0, base_margin = 0, limited_margin = 0, motion = 0;
        int groups[5] = {}, ng = 0;
        double contributions[5] = {};
        for (int k = 0; k < count; k++) {
            Index id = ids[k];
            double a = weights[k], candidate = (*patch).candidate[id];
            candidate_margin += a * candidate;
            if (phase == 0 || phase == 3) {
                continue;
            }
            double base = (*patch).base[id];
            base_margin += a * base;
            int g = (*patch).group[id];
            if (phase == 1 && g >= 0) {
                int p = 0;
                while (p < ng && groups[p] != g) {
                    p++;
                }
                if (p == ng) {
                    groups[ng] = g;
                    contributions[ng++] = 0;
                }
                contributions[p] += a * (candidate - base);
            }
            if (phase == 2) {
                double limited = (*patch).limited[id];
                limited_margin += a * limited;
                motion += a * (candidate - limited);
            }
        }
        if (phase == 0) {
            native_diagnostic[0]++;
            channel_minimum = std::min(channel_minimum, candidate_margin);
        }
        if (phase == 1) {
            double harm = 0;
            for (int p = 0; p < ng; p++) {
                harm += std::max(0.0, -contributions[p]);
            }
            if (harm > STORAGE_EPS) {
                double ratio = std::min(1.0, std::max(0.0, base_margin) / harm);
                for (int p = 0; p < ng; p++) {
                    if (contributions[p] < 0) {
                        capacity[groups[p]] = std::min(capacity[groups[p]], ratio);
                    }
                }
            }
        }
        if (phase == 2 && motion < -STORAGE_EPS) {
            completion_ray = std::min(completion_ray, std::max(0.0, limited_margin) / -motion);
        }
        if (phase == 3) {
            native_diagnostic[2] = std::min(native_diagnostic[2], candidate_margin);
        }
    }
    void admit_cells(int y, int ch, int phase) {
        for (int x = 0; x < sw - 1; x++) {
            direction ns[3] = {};
            int n = 0;
            Index cell = static_cast<std::size_t>(y) * (sw - 1) + x;
            if (phase == 0) {
                n = normals(x, y, ch, ns);
                cone_counts[cell] = static_cast<Byte>(n);
                native_diagnostic[3] += n > 0;
                for (int k = 0; k < std::min(2, n); k++) {
                    cone_cache[cell * 4 + 2 * k] = ns[k].x;
                    cone_cache[cell * 4 + 2 * k + 1] = ns[k].y;
                }
            } else {
                n = cone_counts[cell];
                for (int k = 0; k < std::min(2, n); k++) {
                    ns[k] = direction{cone_cache[cell * 4 + 2 * k], cone_cache[cell * 4 + 2 * k + 1]};
                }
                if (n == 3) {
                    ns[2] = direction{ns[0].y, -ns[0].x};
                }
            }
            if (!n) {
                continue;
            }
            // Every directional row reuses these same 36 values and group owners.
            // In particular, do not reconstruct the bilinear base hundreds of times.
            admission_patch patch{};
            for (int j = 0; j < 6; j++) {
                for (int i = 0; i < 6; i++) {
                    int local = j * 6 + i;
                    Index id = static_cast<std::size_t>(y * 5 + j) * lw + x * 5 + i;
                    patch.candidate[local] = native_control[id * 4 + ch];
                    if (phase == 1 || phase == 2) {
                        double base = base_control(id, ch);
                        int g = owner(id);
                        patch.base[local] = base;
                        patch.group[local] = g;
                        if (phase == 2) {
                            patch.limited[local] =
                                base + (g < 0 ? 0 : capacity[g]) * (patch.candidate[local] - base);
                        }
                    }
                }
            }
            for (int k = 0; k < n; k++) {
                for (int j = 0; j < 6; j++) {
                    for (int i = 0; i < 6; i++) {
                        constraint_row(i, j, ns[k], phase, &patch);
                    }
                }
            }
        }
    }
    int build_native_step(int budget) {
        while (budget > 0 && build_phase < 12) {
            budget -= 1;
            int p = build_lane, ch = build_channel;
            if (build_phase == 0) {
                for (int x = 0; x < sw; x++) {
                    double xx = 0, yy = 0;
                    for (int c = 0; c < 4; c++) {
                        if (src(x, p, c) != src(0, 0, c)) {
                            constant_channel[c] = 0;
                        }
                        double dx = source_jet(x, p, c, 0), dy = source_jet(x, p, c, 1);
                        xx += dx * dx;
                        yy += dy * dy;
                    }
                    beta[p * sw + x] = xx + yy > 0 ? yy / (xx + yy) : .5;
                }
                if (++build_lane == sh) {
                    build_lane = 0;
                    build_phase++;
                }
            } else if (build_phase == 1) {
                for (int c = 0; c < 4; c++) {
                    if (constant_channel[c]) {
                        continue;
                    }
                    for (int x = 0; x < sw; x++) {
                        line[x] = src(x, p, c);
                    }
                    if (refine_line(sw)) {
                        return -1;
                    }
                    for (int x = 0; x < lw; x++) {
                        stage[(static_cast<std::size_t>(p) * lw + x) * 4 + c] = refined[x];
                    }
                }
                if (++build_lane == sh) {
                    build_lane = 0;
                    build_phase++;
                }
            } else if (build_phase == 2) {
                for (int c = 0; c < 4; c++) {
                    if (constant_channel[c]) {
                        continue;
                    }
                    for (int y = 0; y < sh; y++) {
                        line[y] = stage[(static_cast<std::size_t>(y) * lw + p) * 4 + c];
                    }
                    if (refine_line(sh)) {
                        return -1;
                    }
                    for (int y = 0; y < lh; y++) {
                        refined[y] *= 1 - blend(p, y);
                    }
                    for (int cy = 0; cy < sh - 1; cy++) {
                        for (int j = cy ? 1 : 0; j < 6; j++) {
                            double sum = 0;
                            for (int k = 0; k < 6; k++) {
                                sum += inv[j][k] * refined[5 * cy + k];
                            }
                            native_control[(static_cast<std::size_t>(5 * cy + j) * lw + p) * 4 + c] = sum;
                        }
                    }
                }
                if (++build_lane == lw) {
                    build_lane = 0;
                    build_phase++;
                }
            } else if (build_phase == 3) {
                for (int cx = 0; cx < sw - 1; cx++) {
                    for (int c = 0; c < 4; c++) {
                        if (constant_channel[c]) {
                            continue;
                        }
                        double values[6] = {};
                        for (int k = 0; k < 6; k++) {
                            values[k] =
                                native_control[(static_cast<std::size_t>(p) * lw + 5 * cx + k) * 4 + c];
                        }
                        for (int i = 0; i < 6; i++) {
                            double sum = 0;
                            for (int k = 0; k < 6; k++) {
                                sum += inv[i][k] * values[k];
                            }
                            native_control[(static_cast<std::size_t>(p) * lw + 5 * cx + i) * 4 + c] = sum;
                        }
                    }
                }
                if (++build_lane == lh) {
                    build_lane = 0;
                    build_phase++;
                }
            } else if (build_phase == 4) {
                for (int c = 0; c < 4; c++) {
                    if (constant_channel[c]) {
                        continue;
                    }
                    for (int y = 0; y < sh; y++) {
                        line[y] = src(p, y, c);
                    }
                    if (refine_line(sh)) {
                        return -1;
                    }
                    for (int y = 0; y < lh; y++) {
                        stage[(static_cast<std::size_t>(y) * sw + p) * 4 + c] = refined[y];
                    }
                }
                if (++build_lane == sw) {
                    build_lane = 0;
                    build_phase++;
                }
            } else if (build_phase == 5) {
                int row = p % 5;
                if (p && row == 0) {
                    row = 5;
                }
                for (int c = 0; c < 4; c++) {
                    if (constant_channel[c]) {
                        continue;
                    }
                    for (int x = 0; x < sw; x++) {
                        line[x] = stage[(static_cast<std::size_t>(p) * sw + x) * 4 + c];
                    }
                    if (refine_line(sw)) {
                        return -1;
                    }
                    for (int x = 0; x < lw; x++) {
                        refined[x] *= blend(x, p);
                    }
                    for (int cx = 0; cx < sw - 1; cx++) {
                        for (int i = cx ? 1 : 0; i < 6; i++) {
                            double sum = 0;
                            for (int k = 0; k < 6; k++) {
                                sum += inv[i][k] * refined[5 * cx + k];
                            }
                            ring[(static_cast<std::size_t>(row) * lw + 5 * cx + i) * 4 + c] = sum;
                        }
                    }
                }
                if (row == 5) {
                    int cy = p / 5 - 1;
                    for (int j = cy ? 1 : 0; j < 6; j++) {
                        for (int x = 0; x < lw; x++) {
                            for (int c = 0; c < 4; c++) {
                                if (constant_channel[c]) {
                                    continue;
                                }
                                double sum = 0;
                                for (int k = 0; k < 6; k++) {
                                    sum += inv[j][k] * ring[(static_cast<std::size_t>(k) * lw + x) * 4 + c];
                                }
                                native_control[(static_cast<std::size_t>(5 * cy + j) * lw + x) * 4 + c] +=
                                    sum;
                            }
                        }
                    }
                    for (Index k = 0; k < static_cast<std::size_t>(lw) * 4; k++) {
                        ring[k] = ring[static_cast<std::size_t>(5) * lw * 4 + k];
                    }
                }
                if (++build_lane == lh) {
                    build_lane = 0;
                    build_phase++;
                }
            } else if (build_phase == 6) {
                for (int x = 0; x < sw - 1; x++) {
                    clipped_cell(x, p);
                }
                if (++build_lane == sh - 1) {
                    build_lane = 0;
                    build_phase++;
                    channel_minimum = 1e300;
                }
            } else if (build_phase == 7) {
                admit_cells(p, ch, 0);
                if (++build_lane == sh - 1) {
                    build_lane = 0;
                    if (channel_minimum >= -STORAGE_EPS) {
                        native_diagnostic[2] = std::min(native_diagnostic[2], channel_minimum);
                        if (++build_channel == 4) {
                            build_phase = 12;
                        }
                        channel_minimum = 1e300;
                    } else {
                        for (Index k = 0; k < native_groups; k++) {
                            capacity[k] = 1;
                        }
                        build_phase++;
                    }
                }
            } else if (build_phase == 8) {
                admit_cells(p, ch, 1);
                if (++build_lane == sh - 1) {
                    build_lane = 0;
                    build_phase++;
                    completion_ray = 1;
                }
            } else if (build_phase == 9) {
                admit_cells(p, ch, 2);
                if (++build_lane == sh - 1) {
                    build_lane = 0;
                    build_phase++;
                }
            } else if (build_phase == 10) {
                for (int x = 0; x < lw; x++) {
                    Index id = static_cast<std::size_t>(p) * lw + x;
                    int g = owner(id);
                    double base = base_control(id, ch), candidate = native_control[id * 4 + ch],
                           limited = base + (g < 0 ? 0 : capacity[g]) * (candidate - base);
                    native_control[id * 4 + ch] = limited + completion_ray * (candidate - limited);
                }
                if (++build_lane == lh) {
                    build_lane = 0;
                    build_phase++;
                }
            } else if (build_phase == 11) {
                admit_cells(p, ch, 3);
                if (++build_lane == sh - 1) {
                    build_lane = 0;
                    channel_minimum = 1e300;
                    build_phase = ++build_channel == 4 ? 12 : 7;
                }
            }
        }
        if (build_phase == 12) {
            return 1;
        }
        return 0;
    }
};
} // namespace

void ConvWarpField::compile(const Image& source, const std::atomic<bool>* cancelled) {
    if (source.width < 1 || source.height < 1 || source.width > 32768 || source.height > 32768 ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height) {
        throw std::invalid_argument("Invalid CONV source image.");
    }
    if (cancelled && (*cancelled).load(std::memory_order_relaxed)) {
        throw std::runtime_error("CONV preparation superseded.");
    }
    const Color first = source.pixels.front();
    bool constant = true;
    for (const Color color : source.pixels) {
        if (!equal(color, first)) {
            constant = false;
            break;
        }
    }
    if (constant) {
        const double alpha = first.a / 255.0;
        constant_value_ = {first.r / 255.0 * alpha, first.g / 255.0 * alpha, first.b / 255.0 * alpha, alpha};
        constant_ = true;
        controls_ = {};
        controls_.shrink_to_fit();
        width_ = source.width;
        height_ = source.height;
        lattice_width_ = 0;
        return;
    }
    if (cancelled && (*cancelled).load(std::memory_order_relaxed)) {
        throw std::runtime_error("CONV preparation superseded.");
    }
    AtlasBuilder builder(source);
    while (builder.build_phase < 12) {
        if (cancelled && (*cancelled).load(std::memory_order_relaxed)) {
            throw std::runtime_error("CONV preparation superseded.");
        }
        int status = builder.build_native_step(64);
        if (status < 0) {
            throw std::runtime_error("CONV signed-current projection failed.");
        }
    }
    controls_.swap(builder.native_control);
    constant_ = false;
    width_ = source.width;
    height_ = source.height;
    lattice_width_ = builder.lw;
}
std::size_t ConvWarpField::storage_bytes() const {
    std::size_t bytes = controls_.size() * sizeof(double);
    return bytes;
}
WarpSample ConvWarpField::sample_components(Point position) const {
    WarpSample sample;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || position.x < -0.5 || position.y < -0.5 ||
        position.x >= width_ - 0.5 || position.y >= height_ - 0.5 || (!constant_ && controls_.empty())) {
        return sample;
    }
    if (constant_) {
        return constant_value_;
    }
    double x = std::clamp(position.x, 0.0, static_cast<double>(width_ - 1));
    double y = std::clamp(position.y, 0.0, static_cast<double>(height_ - 1));
    int cx = std::min(static_cast<int>(x), std::max(0, width_ - 2));
    int cy = std::min(static_cast<int>(y), std::max(0, height_ - 2));
    double bx[6] = {}, by[6] = {};
    bernstein(x - cx, bx);
    bernstein(y - cy, by);
    double values[4] = {};
    for (int j = 0; j < 6; ++j) {
        double row[4] = {};
        for (int i = 0; i < 6; ++i) {
            std::size_t offset = (static_cast<std::size_t>(cy * 5 + j) * lattice_width_ + cx * 5 + i) * 4;
            for (int c = 0; c < 4; ++c) {
                row[c] += bx[i] * controls_[offset + c];
            }
        }
        for (int c = 0; c < 4; ++c) {
            values[c] += by[j] * row[c];
        }
    }
    sample = {values[0], values[1], values[2], values[3]};
    return sample;
}
WarpSample ConvWarpField::sample_premultiplied(Point position) const {
    WarpSample value = sample_components(position);
    // Keep the admitted opacity. Project color at each query, before any positive
    // filter weights are accumulated; clipping only the final average is too late.
    value.a = std::clamp(value.a, 0.0, 1.0);
    value.r = std::clamp(value.r, 0.0, value.a);
    value.g = std::clamp(value.g, 0.0, value.a);
    value.b = std::clamp(value.b, 0.0, value.a);
    return value;
}
namespace {
static Color encoded_sample(WarpSample value) {
    double alpha = std::clamp(value.a, 0.0, 1.0);
    if (alpha < 0.5 / 255.0) {
        return {0, 0, 0, 0};
    }
    Color result{static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.r / alpha, 0.0, 1.0))),
                 static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.g / alpha, 0.0, 1.0))),
                 static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.b / alpha, 0.0, 1.0))),
                 static_cast<std::uint8_t>(std::round(255.0 * alpha))};
    return result;
}
} // namespace
Color ConvWarpField::sample(Point position) const {
    WarpSample value = sample_premultiplied(position);
    Color color = encoded_sample(value);
    return color;
}

namespace {
static Point transformed(Point point, const AffineMap& map) {
    Point result{map.xx * point.x + map.xy * point.y + map.tx, map.yx * point.x + map.yy * point.y + map.ty};
    return result;
}
static AffineMap inverse_map(const AffineMap& map) {
    double determinant = map.xx * map.yy - map.xy * map.yx;
    double scale = std::max({std::abs(map.xx), std::abs(map.xy), std::abs(map.yx), std::abs(map.yy)});
    if (!std::isfinite(determinant) || !std::isfinite(map.tx) || !std::isfinite(map.ty) || scale == 0.0 ||
        std::abs(determinant) <= 1.0e-12 * scale * scale) {
        throw std::invalid_argument("Warp map is singular or not finite.");
    }
    AffineMap inverse{map.yy / determinant,  -map.xy / determinant, 0.0,
                      -map.yx / determinant, map.xx / determinant,  0.0};
    inverse.tx = -inverse.xx * map.tx - inverse.xy * map.ty;
    inverse.ty = -inverse.yx * map.tx - inverse.yy * map.ty;
    return inverse;
}
struct SampleRule {
    int count = 1;
    double offsets[8] = {};
    double weights[8] = {1.0};
};
static SampleRule sample_rule(double footprint, WarpSampling sampling) {
    SampleRule rule;
    double side = 1.0;
    if (sampling == WarpSampling::Point) {
        return rule;
    }
    if (sampling == WarpSampling::Minification) {
        if (footprint <= 1.0) {
            return rule;
        }
        double reciprocal = 1.0 / footprint;
        side = std::sqrt((1.0 - reciprocal) * (1.0 + reciprocal));
    }
    // Fixed nodes avoid finite rule changes as a transform crosses a threshold.
    // The residual square shrinks continuously to the cardinal point sampler.
    rule.count = 8;
    constexpr double offsets[8] = {-0.48014492824876812, -0.39833323870681337, -0.26276620495816449,
                                   -0.09171732124782490, 0.09171732124782490,  0.26276620495816449,
                                   0.39833323870681337,  0.48014492824876812};
    constexpr double weights[8] = {0.05061426814518813, 0.11119051722668724, 0.15685332293894364,
                                   0.18134189168918099, 0.18134189168918099, 0.15685332293894364,
                                   0.11119051722668724, 0.05061426814518813};
    for (int i = 0; i < 8; ++i) {
        rule.offsets[i] = side * offsets[i];
        rule.weights[i] = weights[i];
    }
    return rule;
}
static double map_footprint(const AffineMap& inverse) {
    // Largest singular value, including compression along a sheared direction.
    // Normalize before squaring to avoid overflow for otherwise valid maps.
    double scale =
        std::max({std::abs(inverse.xx), std::abs(inverse.xy), std::abs(inverse.yx), std::abs(inverse.yy)});
    double xx = inverse.xx / scale, xy = inverse.xy / scale;
    double yx = inverse.yx / scale, yy = inverse.yy / scale;
    double a = xx * xx + yx * yx;
    double b = xx * xy + yx * yy;
    double d = xy * xy + yy * yy;
    double eigenvalue = 0.5 * (a + d + std::hypot(a - d, 2.0 * b));
    double footprint = scale * std::sqrt(eigenvalue);
    return footprint;
}
static void add_weighted(WarpSample& sum, WarpSample value, double weight) {
    sum.r += value.r * weight;
    sum.g += value.g * weight;
    sum.b += value.b * weight;
    sum.a += value.a * weight;
}
// A committed destination pixel is a parallelogram in source coordinates.
// Clip it at every source-cell boundary before quadrature: a single fixed
// target stencil can miss arbitrarily many thin cells under strong reduction.
struct FootprintPolygon {
    std::array<Point, 12> points = {};
    int count = 0;
};
static FootprintPolygon clip_footprint(const FootprintPolygon& input, int axis, double boundary,
                                       bool greater) {
    FootprintPolygon output;
    if (!input.count) {
        return output;
    }
    Point previous = input.points[input.count - 1];
    double pv = axis == 0 ? previous.x : previous.y;
    bool previous_inside = greater ? pv >= boundary : pv <= boundary;
    for (int i = 0; i < input.count; ++i) {
        const Point current = input.points[i];
        const double cv = axis == 0 ? current.x : current.y;
        const bool inside = greater ? cv >= boundary : cv <= boundary;
        if (inside != previous_inside) {
            const double t = (boundary - pv) / (cv - pv);
            output.points[output.count++] = {previous.x + t * (current.x - previous.x),
                                             previous.y + t * (current.y - previous.y)};
        }
        if (inside) {
            output.points[output.count++] = current;
        }
        previous = current;
        pv = cv;
        previous_inside = inside;
    }
    return output;
}
static WarpSample integrate_footprint(const ConvWarpField& field, const FootprintPolygon& footprint,
                                      double density) {
    double left = 1e30, top = 1e30, right = -1e30, bottom = -1e30;
    for (int i = 0; i < footprint.count; ++i) {
        const Point point = footprint.points[i];
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    WarpSample total;
    if (right <= -0.5 || bottom <= -0.5 || left >= field.width() - 0.5 || top >= field.height() - 0.5) {
        return total;
    }
    const int first_x = static_cast<int>(std::floor(std::max(-0.5, left)));
    const int first_y = static_cast<int>(std::floor(std::max(-0.5, top)));
    const int last_x = static_cast<int>(std::floor(std::min(field.width() - 0.5, right)));
    const int last_y = static_cast<int>(std::floor(std::min(field.height() - 0.5, bottom)));
    // Six-point Gauss-Legendre on each Duffy coordinate integrates a tensor
    // quintic on a triangle exactly in real arithmetic (including its Jacobian).
    // Physical RGBA projection within a patch is piecewise polynomial; that
    // projection retains positive quadrature and is not claimed algebraically exact.
    constexpr double nodes[6] = {0.033765242898423975, 0.16939530676686774, 0.38069040695840156,
                                 0.61930959304159844,  0.83060469323313226, 0.96623475710157603};
    constexpr double weights[6] = {0.08566224618958517, 0.1803807865240693, 0.23395696728634552,
                                   0.23395696728634552, 0.1803807865240693, 0.08566224618958517};
    for (int y = first_y; y <= last_y; ++y) {
        FootprintPolygon row = clip_footprint(footprint, 1, std::max(-0.5, static_cast<double>(y)), true);
        row = clip_footprint(row, 1, std::min(field.height() - 0.5, y + 1.0), false);
        if (row.count < 3) {
            continue;
        }
        for (int x = first_x; x <= last_x; ++x) {
            FootprintPolygon cell = clip_footprint(row, 0, std::max(-0.5, static_cast<double>(x)), true);
            cell = clip_footprint(cell, 0, std::min(field.width() - 0.5, x + 1.0), false);
            for (int triangle = 1; triangle + 1 < cell.count; ++triangle) {
                const Point a = cell.points[0], b = cell.points[triangle], c = cell.points[triangle + 1];
                const double determinant = std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
                if (determinant < 1e-24) {
                    continue;
                }
                for (int j = 0; j < 6; ++j) {
                    for (int i = 0; i < 6; ++i) {
                        const double u = nodes[i], v = (1 - u) * nodes[j];
                        const Point point{a.x + u * (b.x - a.x) + v * (c.x - a.x),
                                          a.y + u * (b.y - a.y) + v * (c.y - a.y)};
                        add_weighted(total, field.sample_premultiplied(point),
                                     density * determinant * (1 - u) * weights[i] * weights[j]);
                    }
                }
            }
        }
    }
    return total;
}
static WarpSample integrate_affine_pixel(const ConvWarpField& field, const AffineMap& inverse, Point center) {
    FootprintPolygon footprint;
    footprint.count = 4;
    constexpr double dx[4] = {-0.5, 0.5, 0.5, -0.5}, dy[4] = {-0.5, -0.5, 0.5, 0.5};
    for (int i = 0; i < 4; ++i) {
        footprint.points[i] = {center.x + inverse.xx * dx[i] + inverse.xy * dy[i],
                               center.y + inverse.yx * dx[i] + inverse.yy * dy[i]};
    }
    return integrate_footprint(field, footprint,
                               1.0 / std::abs(inverse.xx * inverse.yy - inverse.xy * inverse.yx));
}
struct AffineStencil {
    int count = 1;
    Point offsets[64] = {};
    double weights[64] = {1.0};
};
static AffineStencil prepare_affine_stencil(const AffineMap& inverse, const SampleRule& rule) {
    AffineStencil stencil;
    stencil.count = rule.count * rule.count;
    for (int j = 0; j < rule.count; ++j) {
        for (int i = 0; i < rule.count; ++i) {
            int index = j * rule.count + i;
            stencil.offsets[index] = {inverse.xx * rule.offsets[i] + inverse.xy * rule.offsets[j],
                                      inverse.yx * rule.offsets[i] + inverse.yy * rule.offsets[j]};
            stencil.weights[index] = rule.weights[i] * rule.weights[j];
        }
    }
    return stencil;
}
struct AffineJob {
    const ConvWarpField& field;
    const AffineMap& inverse;
    const AffineStencil& stencil;
    Image& output;
    bool area = false;
    const std::atomic<bool>* cancelled = nullptr;
    std::atomic<int> next_row{0};
};
static void affine_worker(AffineJob& job) {
    const int width = job.output.width, height = job.output.height;
    const AffineMap inverse = job.inverse;
    const AffineStencil stencil = job.stencil;
    const ConvWarpField& field = job.field;
    std::vector<Color>& pixels = job.output.pixels;
    for (;;) {
        if (job.cancelled && (*job.cancelled).load(std::memory_order_relaxed)) { return; }
        int y = job.next_row.fetch_add(1, std::memory_order_relaxed);
        if (y >= height) {
            return;
        }
        double row_x = inverse.xy * y + inverse.tx;
        double row_y = inverse.yy * y + inverse.ty;
        std::size_t offset = static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            if (x % 64 == 0 && job.cancelled && (*job.cancelled).load(std::memory_order_relaxed)) { return; }
            Point centre{row_x + inverse.xx * x, row_y + inverse.yx * x};
            WarpSample total;
            if (job.area) {
                total = integrate_affine_pixel(field, inverse, centre);
            }
            for (int i = 0; i < (job.area ? 0 : stencil.count); ++i) {
                Point position{centre.x + stencil.offsets[i].x, centre.y + stencil.offsets[i].y};
                WarpSample value = field.sample_premultiplied(position);
                add_weighted(total, value, stencil.weights[i]);
            }
            pixels[offset + x] = encoded_sample(total);
        }
    }
}
// Bounded executors serve concurrent viewport and commit callers. Threads
// sleep between jobs; each caller retains its stack-owned job until completion.
class AffineExecutor {
  public:
    AffineExecutor() {
        const unsigned count = std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
        try {
            for (unsigned i = 0; i < count; ++i) {
                workers_.emplace_back(run, std::ref(*this));
            }
        } catch (...) {
            stop();
            throw;
        }
    }
    ~AffineExecutor() {
        stop();
    }
    void execute(AffineJob& job) {
        std::lock_guard<std::mutex> caller(caller_mutex_);
        std::unique_lock<std::mutex> lock(mutex_);
        job_ = &job;
        remaining_ = workers_.size();
        error_ = {};
        ++generation_;
        ready_.notify_all();
        while (remaining_) {
            finished_.wait(lock);
        }
        job_ = nullptr;
        if (error_) {
            std::rethrow_exception(error_);
        }
    }

  private:
    std::mutex caller_mutex_, mutex_;
    std::condition_variable ready_, finished_;
    std::vector<std::thread> workers_;
    AffineJob* job_ = nullptr;
    std::uint64_t generation_ = 0;
    std::size_t remaining_ = 0;
    bool stopping_ = false;
    std::exception_ptr error_;
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (std::thread& worker : workers_) {
            worker.join();
        }
    }
    static void run(AffineExecutor& executor) {
        std::uint64_t seen = 0;
        std::unique_lock<std::mutex> lock(executor.mutex_);
        for (;;) {
            while (!executor.stopping_ && seen == executor.generation_) {
                executor.ready_.wait(lock);
            }
            if (executor.stopping_) {
                return;
            }
            seen = executor.generation_;
            AffineJob& job = *executor.job_;
            lock.unlock();
            std::exception_ptr error;
            try {
                affine_worker(job);
            } catch (...) {
                error = std::current_exception();
            }
            lock.lock();
            if (error && !executor.error_) {
                executor.error_ = error;
            }
            if (--executor.remaining_ == 0) {
                executor.finished_.notify_one();
            }
        }
    }
};
static AffineExecutor& affine_executor(WarpSampling sampling) {
    // A long final area integration must not hold up interactive point views.
    if (sampling == WarpSampling::Point) {
        static AffineExecutor interactive;
        return interactive;
    }
    static AffineExecutor final;
    return final;
}
static double orient(Point a, Point b, Point c) {
    double result = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    return result;
}
static bool within_triangle(Point point, Point a, Point b, Point c, double tolerance) {
    return orient(a, b, point) >= -tolerance && orient(b, c, point) >= -tolerance &&
           orient(c, a, point) >= -tolerance;
}
static bool edges_cross(Point a, Point b, Point c, Point d) {
    double ab_c = orient(a, b, c), ab_d = orient(a, b, d), cd_a = orient(c, d, a), cd_b = orient(c, d, b);
    // Closed segments: non-neighbouring touching edges are invalid too.
    if (((ab_c > 0.0 && ab_d < 0.0) || (ab_c < 0.0 && ab_d > 0.0)) &&
        ((cd_a > 0.0 && cd_b < 0.0) || (cd_a < 0.0 && cd_b > 0.0))) {
        return true;
    }
    if (std::abs(ab_c) < 1.0e-9 && c.x >= std::min(a.x, b.x) && c.x <= std::max(a.x, b.x) &&
        c.y >= std::min(a.y, b.y) && c.y <= std::max(a.y, b.y)) {
        return true;
    }
    if (std::abs(ab_d) < 1.0e-9 && d.x >= std::min(a.x, b.x) && d.x <= std::max(a.x, b.x) &&
        d.y >= std::min(a.y, b.y) && d.y <= std::max(a.y, b.y)) {
        return true;
    }
    if (std::abs(cd_a) < 1.0e-9 && a.x >= std::min(c.x, d.x) && a.x <= std::max(c.x, d.x) &&
        a.y >= std::min(c.y, d.y) && a.y <= std::max(c.y, d.y)) {
        return true;
    }
    if (std::abs(cd_b) < 1.0e-9 && b.x >= std::min(c.x, d.x) && b.x <= std::max(c.x, d.x) &&
        b.y >= std::min(c.y, d.y) && b.y <= std::max(c.y, d.y)) {
        return true;
    }
    return false;
}
static bool finite_point(Point point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::abs(point.x) <= 1.0e7 &&
           std::abs(point.y) <= 1.0e7;
}
} // namespace
Rect affine_bounds(const ConvWarpField& field, const AffineMap& map) {
    AffineMap inverse = inverse_map(map);
    (void)inverse;
    if (field.width() < 1 || field.height() < 1) {
        throw std::invalid_argument("Compile the CONV material first.");
    }
    Point corners[4] = {{-0.5, -0.5},
                        {field.width() - 0.5, -0.5},
                        {field.width() - 0.5, field.height() - 0.5},
                        {-0.5, field.height() - 0.5}};
    double left = std::numeric_limits<double>::infinity(), top = left, right = -left, bottom = -left;
    for (Point corner : corners) {
        Point point = transformed(corner, map);
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    if (left < -1.0e8 || top < -1.0e8 || right > 1.0e8 || bottom > 1.0e8) {
        throw std::length_error("Transformed bounds exceed the coordinate limit.");
    }
    int x = static_cast<int>(std::floor(left + 0.5 + 1.0e-10)),
        y = static_cast<int>(std::floor(top + 0.5 + 1.0e-10));
    Rect result{x, y, static_cast<int>(std::ceil(right + 0.5 - 1.0e-10)) - x,
                static_cast<int>(std::ceil(bottom + 0.5 - 1.0e-10)) - y};
    return result;
}
void render_affine(const ConvWarpField& field, const AffineMap& map, int width, int height,
                   Image& destination, WarpSampling sampling, const std::atomic<bool>* cancelled) {
    if (cancelled && (*cancelled).load(std::memory_order_relaxed)) { throw std::runtime_error("CONV rendering cancelled."); }
    if (field.width() < 1) {
        throw std::invalid_argument("Compile the CONV material first.");
    }
    AffineMap inverse = inverse_map(map);
    Image output;
    output.reset(width, height, {0, 0, 0, 0});
    SampleRule rule = sample_rule(map_footprint(inverse), sampling);
    AffineStencil stencil = prepare_affine_stencil(inverse, rule);
    AffineJob job{field, inverse, stencil, output, sampling == WarpSampling::Area};
    job.cancelled = cancelled;
    if (output.pixels.size() < 65536) {
        affine_worker(job);
    } else {
        affine_executor(sampling).execute(job);
    }
    if (cancelled && (*cancelled).load(std::memory_order_relaxed)) { throw std::runtime_error("CONV rendering cancelled."); }
    destination = std::move(output);
}

bool reshape_mesh_valid(const ReshapeMesh& mesh) {
    if (mesh.nodes.size() < 3 || mesh.triangles.empty()) {
        return false;
    }
    std::size_t boundary_count = 0;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (!finite_point(mesh.nodes[i].source) || !finite_point(mesh.nodes[i].target)) {
            return false;
        }
        if (mesh.nodes[i].boundary) {
            ++boundary_count;
        }
    }
    for (const MeshTriangle& triangle : mesh.triangles) {
        for (std::size_t index : triangle.nodes) {
            if (index >= mesh.nodes.size()) {
                return false;
            }
        }
        const MeshNode& a = mesh.nodes[triangle.nodes[0]];
        const MeshNode& b = mesh.nodes[triangle.nodes[1]];
        const MeshNode& c = mesh.nodes[triangle.nodes[2]];
        double source_area = orient(a.source, b.source, c.source);
        double target_area = orient(a.target, b.target, c.target);
        if (source_area <= 1.0e-10 || target_area <= std::max(1.0e-9, source_area * 1.0e-5)) {
            return false;
        }
    }
    if (boundary_count < 3) {
        return false;
    }
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (!mesh.nodes[i].boundary) {
            continue;
        }
        std::size_t next = (i + 1) % mesh.nodes.size();
        while (!mesh.nodes[next].boundary) {
            next = (next + 1) % mesh.nodes.size();
        }
        for (std::size_t j = i + 1; j < mesh.nodes.size(); ++j) {
            if (!mesh.nodes[j].boundary) {
                continue;
            }
            std::size_t after = (j + 1) % mesh.nodes.size();
            while (!mesh.nodes[after].boundary) {
                after = (after + 1) % mesh.nodes.size();
            }
            if (next == j || after == i) {
                continue;
            }
            if (edges_cross(mesh.nodes[i].target, mesh.nodes[next].target, mesh.nodes[j].target,
                            mesh.nodes[after].target)) {
                return false;
            }
        }
    }
    return true;
}
bool move_reshape_node(ReshapeMesh& mesh, std::size_t node, Point target) {
    if (node >= mesh.nodes.size() || !finite_point(target)) {
        return false;
    }
    Point previous = mesh.nodes[node].target;
    mesh.nodes[node].target = target;
    if (reshape_mesh_valid(mesh)) {
        return true;
    }
    mesh.nodes[node].target = previous;
    return false;
}
namespace {
static double incircle(Point a, Point b, Point c, Point d) {
    double ax = a.x - d.x, ay = a.y - d.y, bx = b.x - d.x, by = b.y - d.y, cx = c.x - d.x, cy = c.y - d.y;
    double result = (ax * ax + ay * ay) * (bx * cy - by * cx) - (bx * bx + by * by) * (ax * cy - ay * cx) +
                    (cx * cx + cy * cy) * (ax * by - ay * bx);
    return result;
}
static void improve_mesh_angles(ReshapeMesh& mesh) {
    // Lawson flips improve the ear triangulation without moving the outline.
    // A bounded sweep count affects only triangle quality, never validity.
    for (int pass = 0; pass < 32; ++pass) {
        bool changed = false;
        for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
            for (std::size_t j = i + 1; j < mesh.triangles.size(); ++j) {
                MeshTriangle first = mesh.triangles[i], second = mesh.triangles[j];
                std::size_t shared[2] = {}, count = 0, c = 0, d = 0;
                for (std::size_t vertex : first.nodes) {
                    bool found = false;
                    for (std::size_t other : second.nodes) {
                        if (vertex == other) {
                            found = true;
                        }
                    }
                    if (found && count < 2) {
                        shared[count++] = vertex;
                    } else {
                        c = vertex;
                    }
                }
                if (count != 2) {
                    continue;
                }
                for (std::size_t vertex : second.nodes) {
                    if (vertex != shared[0] && vertex != shared[1]) {
                        d = vertex;
                    }
                }
                std::size_t a = shared[0], b = shared[1];
                Point pa = mesh.nodes[a].source, pb = mesh.nodes[b].source, pc = mesh.nodes[c].source,
                      pd = mesh.nodes[d].source;
                if (orient(pa, pb, pc) < 0.0) {
                    std::swap(a, b);
                    std::swap(pa, pb);
                }
                if (orient(pc, pd, pa) * orient(pc, pd, pb) >= -1.0e-10) {
                    continue;
                }
                double scale =
                    std::max({std::hypot(pa.x - pd.x, pa.y - pd.y), std::hypot(pb.x - pd.x, pb.y - pd.y),
                              std::hypot(pc.x - pd.x, pc.y - pd.y), 1.0});
                if (incircle(pa, pb, pc, pd) <= 1.0e-12 * scale * scale * scale * scale) {
                    continue;
                }
                MeshTriangle left{{c, d, a}}, right{{d, c, b}};
                if (orient(pc, pd, pa) < 0.0) {
                    left = {{d, c, a}};
                    right = {{c, d, b}};
                }
                mesh.triangles[i] = left;
                mesh.triangles[j] = right;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
}
} // namespace

ReshapeMesh make_reshape_mesh(const std::vector<Point>& outline, double spacing) {
    if (outline.size() < 3 || outline.size() > 4096 || !std::isfinite(spacing) || spacing < 1.0) {
        throw std::invalid_argument("A reshape lasso needs 3 to 4096 points and spacing at least 1 pixel.");
    }
    std::vector<Point> points;
    for (Point point : outline) {
        if (!finite_point(point) || std::abs(point.x) > 1.0e7 || std::abs(point.y) > 1.0e7) {
            throw std::invalid_argument("Invalid reshape point.");
        }
        if (!points.empty() && std::hypot(point.x - points.back().x, point.y - points.back().y) < 1.0e-7) {
            continue;
        }
        points.push_back(point);
    }
    if (points.size() > 1 &&
        std::hypot(points.front().x - points.back().x, points.front().y - points.back().y) < 1.0e-7) {
        points.pop_back();
    }
    // Remove collinear lasso samples without changing the polygon boundary.
    bool changed = true;
    while (changed && points.size() > 3) {
        changed = false;
        for (std::size_t i = 0; i < points.size(); ++i) {
            Point a = points[(i + points.size() - 1) % points.size()], b = points[i],
                  c = points[(i + 1) % points.size()];
            if (std::abs(orient(a, b, c)) < 1.0e-8 &&
                (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y) >= 0.0) {
                points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    double area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        Point a = points[i], b = points[(i + 1) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    if (std::abs(area) < 1.0e-8 || points.size() < 3) {
        throw std::invalid_argument("The reshape lasso has no area.");
    }
    if (area < 0.0) {
        std::reverse(points.begin(), points.end());
    }
    ReshapeMesh mesh;
    // Start with original boundary vertices: their cyclic node order remains
    // the boundary invariant used by the global injectivity check.
    for (std::size_t i = 0; i < points.size(); ++i) {
        Point a = points[i], b = points[(i + 1) % points.size()];
        double segments = std::ceil(std::hypot(b.x - a.x, b.y - a.y) / spacing);
        if (segments > 2048.0 || mesh.nodes.size() + static_cast<std::size_t>(segments) > 2048) {
            throw std::length_error("Use a coarser reshape spacing; mesh limit is 2048 nodes.");
        }
        int count = std::max(1, static_cast<int>(segments));
        for (int j = 0; j < count; ++j) {
            double t = static_cast<double>(j) / count;
            Point point{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
            mesh.nodes.push_back({point, point, true});
        }
    }
    // Validate polygon simplicity before ear clipping.
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        for (std::size_t j = i + 1; j < mesh.nodes.size(); ++j) {
            std::size_t ni = (i + 1) % mesh.nodes.size(), nj = (j + 1) % mesh.nodes.size();
            if (ni == j || nj == i) {
                continue;
            }
            if (edges_cross(mesh.nodes[i].source, mesh.nodes[ni].source, mesh.nodes[j].source,
                            mesh.nodes[nj].source)) {
                throw std::invalid_argument("The reshape outline crosses itself.");
            }
        }
    }
    std::vector<std::size_t> ring;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        ring.push_back(i);
    }
    while (ring.size() > 3) {
        bool found = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            std::size_t a = ring[(i + ring.size() - 1) % ring.size()], b = ring[i],
                        c = ring[(i + 1) % ring.size()];
            Point pa = mesh.nodes[a].source, pb = mesh.nodes[b].source, pc = mesh.nodes[c].source;
            if (orient(pa, pb, pc) <= 1.0e-10) {
                continue;
            }
            bool occupied = false;
            for (std::size_t node : ring) {
                if (node == a || node == b || node == c) {
                    continue;
                }
                if (within_triangle(mesh.nodes[node].source, pa, pb, pc, 1.0e-10)) {
                    occupied = true;
                    break;
                }
            }
            if (occupied) {
                continue;
            }
            mesh.triangles.push_back({{a, b, c}});
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
            found = true;
            break;
        }
        if (!found) {
            throw std::invalid_argument("Could not triangulate the reshape outline.");
        }
    }
    mesh.triangles.push_back({{ring[0], ring[1], ring[2]}});
    // Interior lattice insertion. Strict interior tests avoid splitting an
    // existing edge on only one side (which would create a T junction).
    double left = points[0].x, right = left, top = points[0].y, bottom = top;
    for (Point point : points) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    double columns = std::ceil((right - left) / spacing), rows = std::ceil((bottom - top) / spacing);
    if (columns * rows > 16384.0) {
        throw std::length_error("Use coarser reshape spacing for this region.");
    }
    for (double y = top + spacing * 0.5; y < bottom; y += spacing) {
        for (double x = left + spacing * 0.47; x < right; x += spacing) {
            Point point{x, y};
            for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
                MeshTriangle triangle = mesh.triangles[i];
                Point a = mesh.nodes[triangle.nodes[0]].source, b = mesh.nodes[triangle.nodes[1]].source,
                      c = mesh.nodes[triangle.nodes[2]].source;
                double guard = spacing * spacing * 1.0e-4;
                if (orient(a, b, point) <= guard || orient(b, c, point) <= guard ||
                    orient(c, a, point) <= guard) {
                    continue;
                }
                if (mesh.nodes.size() >= 2048) {
                    throw std::length_error("Use a coarser reshape spacing; mesh limit is 2048 nodes.");
                }
                std::size_t node = mesh.nodes.size();
                mesh.nodes.push_back({point, point, false});
                mesh.triangles[i] = {{triangle.nodes[0], triangle.nodes[1], node}};
                mesh.triangles.push_back({{triangle.nodes[1], triangle.nodes[2], node}});
                mesh.triangles.push_back({{triangle.nodes[2], triangle.nodes[0], node}});
                break;
            }
        }
    }
    improve_mesh_angles(mesh);
    if (!reshape_mesh_valid(mesh)) {
        throw std::invalid_argument("Reshape triangulation is degenerate.");
    }
    return mesh;
}
ReshapeMesh make_reshape_mesh(const Image& source, double spacing) {
    if (source.width < 1 || source.height < 1) {
        throw std::invalid_argument("An empty image cannot be reshaped.");
    }
    // The full pixel footprint includes the boundary pixels under identity.
    std::vector<Point> outline{{-0.5, -0.5},
                               {source.width - 0.5, -0.5},
                               {source.width - 0.5, source.height - 0.5},
                               {-0.5, source.height - 0.5}};
    ReshapeMesh mesh = make_reshape_mesh(outline, spacing);
    return mesh;
}
namespace {
static AffineMap triangle_inverse(const MeshNode& a, const MeshNode& b, const MeshNode& c) {
    AffineMap target{b.target.x - a.target.x, c.target.x - a.target.x, a.target.x,
                     b.target.y - a.target.y, c.target.y - a.target.y, a.target.y};
    AffineMap inverse = inverse_map(target);
    double bx = b.source.x - a.source.x, cx = c.source.x - a.source.x;
    double by = b.source.y - a.source.y, cy = c.source.y - a.source.y;
    AffineMap map{bx * inverse.xx + cx * inverse.yx,
                  bx * inverse.xy + cx * inverse.yy,
                  a.source.x + bx * inverse.tx + cx * inverse.ty,
                  by * inverse.xx + cy * inverse.yx,
                  by * inverse.xy + cy * inverse.yy,
                  a.source.y + by * inverse.tx + cy * inverse.ty};
    return map;
}
} // namespace
void render_mesh(const ConvWarpField& field, const ReshapeMesh& mesh, int width, int height,
                 Image& destination, WarpSampling sampling) {
    if (field.width() < 1) {
        throw std::invalid_argument("Compile the CONV material first.");
    }
    if (!reshape_mesh_valid(mesh)) {
        throw std::invalid_argument("The reshape mesh folds or overlaps.");
    }
    Image output;
    output.reset(width, height, {0, 0, 0, 0});
    double footprint = 1.0;
    for (const MeshTriangle& triangle : mesh.triangles) {
        AffineMap inverse = triangle_inverse(mesh.nodes[triangle.nodes[0]], mesh.nodes[triangle.nodes[1]],
                                             mesh.nodes[triangle.nodes[2]]);
        footprint = std::max(footprint, map_footprint(inverse));
    }
    SampleRule rule = sample_rule(footprint, sampling);
    // One bit owns each quadrature node. Shared triangle edges can include
    // the same point, but exactly one triangle contributes its value.
    std::vector<std::uint64_t> ownership(output.pixels.size(), 0);
    std::vector<WarpSample> totals(output.pixels.size());
    for (const MeshTriangle& triangle : mesh.triangles) {
        const MeshNode& a = mesh.nodes[triangle.nodes[0]];
        const MeshNode& b = mesh.nodes[triangle.nodes[1]];
        const MeshNode& c = mesh.nodes[triangle.nodes[2]];
        AffineMap inverse = triangle_inverse(a, b, c);
        double area = orient(a.target, b.target, c.target);
        double low_x =
            std::clamp(std::min({a.target.x, b.target.x, c.target.x}) - 0.5, 0.0, static_cast<double>(width));
        double high_x = std::clamp(std::max({a.target.x, b.target.x, c.target.x}) + 0.5, -1.0,
                                   static_cast<double>(width - 1));
        double low_y = std::clamp(std::min({a.target.y, b.target.y, c.target.y}) - 0.5, 0.0,
                                  static_cast<double>(height));
        double high_y = std::clamp(std::max({a.target.y, b.target.y, c.target.y}) + 0.5, -1.0,
                                   static_cast<double>(height - 1));
        int left = static_cast<int>(std::floor(low_x)), right = static_cast<int>(std::ceil(high_x));
        int top = static_cast<int>(std::floor(low_y)), bottom = static_cast<int>(std::ceil(high_y));
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                std::size_t pixel = static_cast<std::size_t>(y) * width + x;
                if (sampling == WarpSampling::Area) {
                    FootprintPolygon polygon;
                    polygon.count = 3;
                    polygon.points[0] = a.target;
                    polygon.points[1] = b.target;
                    polygon.points[2] = c.target;
                    polygon = clip_footprint(polygon, 0, x - 0.5, true);
                    polygon = clip_footprint(polygon, 0, x + 0.5, false);
                    polygon = clip_footprint(polygon, 1, y - 0.5, true);
                    polygon = clip_footprint(polygon, 1, y + 0.5, false);
                    if (polygon.count >= 3) {
                        for (int i = 0; i < polygon.count; ++i) {
                            polygon.points[i] = transformed(polygon.points[i], inverse);
                        }
                        add_weighted(totals[pixel],
                                     integrate_footprint(
                                         field, polygon,
                                         1.0 / std::abs(inverse.xx * inverse.yy - inverse.xy * inverse.yx)),
                                     1.0);
                    }
                    continue;
                }
                for (int j = 0; j < rule.count; ++j) {
                    for (int i = 0; i < rule.count; ++i) {
                        std::uint64_t bit = std::uint64_t{1} << (j * rule.count + i);
                        if ((ownership[pixel] & bit) != 0) {
                            continue;
                        }
                        Point point{x + rule.offsets[i], y + rule.offsets[j]};
                        double wb = orient(a.target, point, c.target) / area;
                        double wc = orient(a.target, b.target, point) / area;
                        double wa = 1.0 - wb - wc;
                        if (wa < -1.0e-10 || wb < -1.0e-10 || wc < -1.0e-10) {
                            continue;
                        }
                        Point source = transformed(point, inverse);
                        WarpSample value = field.sample_premultiplied(source);
                        add_weighted(totals[pixel], value, rule.weights[i] * rule.weights[j]);
                        ownership[pixel] |= bit;
                    }
                }
            }
        }
    }
    for (std::size_t i = 0; i < output.pixels.size(); ++i) {
        output.pixels[i] = encoded_sample(totals[i]);
    }
    destination = std::move(output);
}
} // namespace paint
