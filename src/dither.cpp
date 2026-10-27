#include "dither.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>
namespace paint {
namespace {
struct V {
    double x = 0, y = 0, z = 0;
};
V add(V a, V b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
V sub(V a, V b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
V mul(V a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
double dot(V a, V b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double decode(std::uint8_t c) {
    double s = c / 255.0;
    return s <= .04045 ? s / 12.92 : std::pow((s + .055) / 1.055, 2.4);
}
std::uint8_t encode(double c) {
    c = std::clamp(c, 0.0, 1.0);
    return static_cast<std::uint8_t>(
        std::lround(255 * (c <= .0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - .055)));
}
V linear(Color c) {
    return {decode(c.r), decode(c.g), decode(c.b)};
}
Color encoded(V v, std::uint8_t alpha = 255) {
    return {encode(v.x), encode(v.y), encode(v.z), alpha};
}
V lab(Color c) {
    Lab l = to_oklab(c);
    return {l.l, l.a, l.b};
}
std::uint32_t hash(std::uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    return v ^ (v >> 16);
}
double random(std::uint32_t v) {
    return (hash(v) + .5) / 4294967296.0;
}
bool allowed(const Image& image, std::span<const std::uint8_t> mask, int x, int y) {
    return image.contains(x, y) && (mask.empty() || mask[static_cast<std::size_t>(y) * image.width + x]);
}
struct Sample {
    Color color;
    V perceptual;
    double weight = 1;
};
struct SampleSet {
    std::vector<Sample> values;
    std::vector<Color> exact;
};
std::uint32_t rgb_key(Color c) {
    return static_cast<std::uint32_t>(c.r) | (static_cast<std::uint32_t>(c.g) << 8) |
           (static_cast<std::uint32_t>(c.b) << 16);
}
// One independently jittered observation per stratum avoids fixed-stride aliasing.
// The exact-color inventory also protects small pixel-art palettes, however rare.
SampleSet samples(const Image& image, std::span<const std::uint8_t> mask, const std::atomic<bool>* cancel) {
    SampleSet out;
    std::size_t active = 0;
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        if ((i & 4095) == 0 && cancel && (*cancel).load()) {
            return {};
        }
        Color c = image.pixels[i];
        if ((mask.empty() || mask[i]) && c.a) {
            ++active;
            if (out.exact.size() <= 32) {
                bool found = false;
                for (Color existing : out.exact) {
                    found = found || rgb_key(existing) == rgb_key(c);
                }
                if (!found) {
                    c.a = 255;
                    out.exact.push_back(c);
                }
            }
        }
    }
    const std::size_t count = std::min<std::size_t>(active, 16384);
    if (!count) {
        return out;
    }
    out.values.reserve(count);
    std::size_t ordinal = 0, stratum = 0, target = 0;
    for (std::size_t i = 0; i < image.pixels.size() && stratum < count; ++i) {
        if ((i & 4095) == 0 && cancel && (*cancel).load()) {
            return {};
        }
        Color c = image.pixels[i];
        if ((!mask.empty() && !mask[i]) || !c.a) {
            continue;
        }
        if (ordinal == 0 || ordinal > target) {
            const std::size_t begin = stratum * active / count;
            const std::size_t end = (stratum + 1) * active / count;
            target = begin + hash(static_cast<std::uint32_t>(stratum) ^ 0x921a7835U) % (end - begin);
        }
        if (ordinal == target) {
            out.values.push_back({c, lab(c), c.a / 255.0});
            ++stratum;
        }
        ++ordinal;
    }
    return out;
}
double component(V value, int axis) {
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}
struct SampleLess {
    int axis;
    bool operator()(const Sample& a, const Sample& b) const {
        double x = component(a.perceptual, axis), y = component(b.perceptual, axis);
        return x == y ? rgb_key(a.color) < rgb_key(b.color) : x < y;
    }
};
struct Cluster {
    std::vector<Sample> values;
    V mean;
    double gain = 0;
    std::size_t cut = 0;
};
Cluster cluster(std::vector<Sample> values) {
    Cluster result;
    V total{};
    double mass = 0;
    for (const Sample& sample : values) {
        total = add(total, mul(sample.perceptual, sample.weight));
        mass += sample.weight;
    }
    result.mean = mul(total, 1 / mass);
    int best_axis = 0;
    for (int axis = 0; axis < 3; ++axis) {
        std::sort(values.begin(), values.end(), SampleLess{axis});
        V left{};
        double left_mass = 0;
        for (std::size_t i = 0; i + 1 < values.size(); ++i) {
            left = add(left, mul(values[i].perceptual, values[i].weight));
            left_mass += values[i].weight;
            const double right_mass = mass - left_mass;
            if (component(values[i].perceptual, axis) == component(values[i + 1].perceptual, axis) ||
                right_mass < 1e-12) {
                continue;
            }
            V difference = sub(mul(left, 1 / left_mass), mul(sub(total, left), 1 / right_mass));
            double gain = left_mass * right_mass / mass * dot(difference, difference);
            if (gain > result.gain) {
                result.gain = gain;
                result.cut = i + 1;
                best_axis = axis;
            }
        }
    }
    std::sort(values.begin(), values.end(), SampleLess{best_axis});
    result.values = std::move(values);
    return result;
}
V tail_mean(std::vector<Sample> values, bool high) {
    std::sort(values.begin(), values.end(), SampleLess{0});
    double mass = 0;
    for (const Sample& sample : values) {
        mass += sample.weight;
    }
    // Average a half-percent tail rather than amplifying a single outlier.
    const double limit = mass * .005;
    mass = 0;
    V total{};
    for (std::size_t i = 0; i < values.size() && mass < limit; ++i) {
        const Sample& sample = values[high ? values.size() - 1 - i : i];
        double weight = std::min(sample.weight, limit - mass);
        total = add(total, mul(sample.perceptual, weight));
        mass += weight;
    }
    return mul(total, 1 / mass);
}
bool palette_less(Color a, Color b);
std::vector<Color> make_palette(const SampleSet& source, int count, bool dither,
                                const std::atomic<bool>* cancel) {
    if (source.values.empty()) {
        return {};
    }
    if (source.exact.size() <= static_cast<std::size_t>(count)) {
        std::vector<Color> exact = source.exact;
        std::sort(exact.begin(), exact.end(), palette_less);
        return exact;
    }
    // Split where the actual squared OKLab error falls most, then refine the
    // representatives. Unlike a saturating similarity, large tonal errors keep
    // their importance. Alpha supplies the only sample weight.
    std::vector<Cluster> clusters;
    clusters.push_back(cluster(source.values));
    while (clusters.size() < static_cast<std::size_t>(count)) {
        if (cancel && (*cancel).load()) {
            return {};
        }
        std::size_t best = 0;
        for (std::size_t i = 1; i < clusters.size(); ++i) {
            if (clusters[i].gain > clusters[best].gain) {
                best = i;
            }
        }
        if (clusters[best].gain < 1e-15 || clusters[best].cut == 0) {
            break;
        }
        Cluster& parent = clusters[best];
        std::vector<Sample> right(parent.values.begin() + parent.cut, parent.values.end());
        parent.values.resize(parent.cut);
        parent = cluster(std::move(parent.values));
        clusters.push_back(cluster(std::move(right)));
    }
    std::vector<V> centers;
    for (const Cluster& group : clusters) {
        centers.push_back(group.mean);
    }
    for (int iteration = 0; iteration < 32; ++iteration) {
        if (cancel && (*cancel).load()) {
            return {};
        }
        std::array<V, 32> sums{};
        std::array<double, 32> masses{};
        for (const Sample& sample : source.values) {
            int nearest = 0;
            double distance = std::numeric_limits<double>::infinity();
            for (std::size_t j = 0; j < centers.size(); ++j) {
                V delta = sub(sample.perceptual, centers[j]);
                if (dot(delta, delta) < distance) {
                    distance = dot(delta, delta);
                    nearest = static_cast<int>(j);
                }
            }
            sums[nearest] = add(sums[nearest], mul(sample.perceptual, sample.weight));
            masses[nearest] += sample.weight;
        }
        double change = 0;
        for (std::size_t j = 0; j < centers.size(); ++j) {
            if (masses[j] > 0) {
                V mean = mul(sums[j], 1 / masses[j]), delta = sub(mean, centers[j]);
                change = std::max(change, dot(delta, delta));
                centers[j] = mean;
            }
        }
        if (change < 1e-10) {
            break;
        }
    }
    std::vector<Color> palette;
    for (V center : centers) {
        palette.push_back(from_oklab({center.x, center.y, center.z}));
    }
    std::sort(palette.begin(), palette.end(), palette_less);
    if (dither && palette.size() > 1) {
        // Dithering needs coverage between colors, whereas posterizing needs
        // representative centers. Modest endpoint expansion retains more range
        // without dedicating scarce entries to isolated extrema.
        V low = mul(add(lab(palette.front()), tail_mean(source.values, false)), .5);
        V high = mul(add(lab(palette.back()), tail_mean(source.values, true)), .5);
        palette.front() = from_oklab({low.x, low.y, low.z});
        palette.back() = from_oklab({high.x, high.y, high.z});
        std::sort(palette.begin(), palette.end(), palette_less);
    }
    return palette;
}
constexpr double mixture_penalty = .08;
struct Mix {
    std::array<int, 4> ids{};
    std::array<double, 4> weights{};
    int size = 0;
    V point;
};
bool solve(std::array<std::array<double, 4>, 3>& m, int n, double* answer) {
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int row = col + 1; row < n; ++row) {
            if (std::abs(m[row][col]) > std::abs(m[pivot][col])) {
                pivot = row;
            }
        }
        if (std::abs(m[pivot][col]) < 1e-14) {
            return false;
        }
        for (int j = col; j <= n; ++j) {
            std::swap(m[col][j], m[pivot][j]);
        }
        double scale = m[col][col];
        for (int j = col; j <= n; ++j) {
            m[col][j] /= scale;
        }
        for (int row = 0; row < n; ++row) {
            if (row != col) {
                scale = m[row][col];
                for (int j = col; j <= n; ++j) {
                    m[row][j] -= scale * m[col][j];
                }
            }
        }
    }
    for (int i = 0; i < n; ++i) {
        answer[i] = m[i][n];
    }
    return true;
}
Mix project(V source, const std::vector<V>& palette) {
    Mix mix;
    mix.size = 1;
    mix.weights[0] = 1;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < palette.size(); ++i) {
        V d = sub(palette[i], source);
        if (dot(d, d) < best) {
            best = dot(d, d);
            mix.ids[0] = static_cast<int>(i);
        }
    }
    mix.point = palette[mix.ids[0]];
    // Fit the mean while penalizing visible spread between the contributing
    // colors. This is a convex objective with nonnegative weights, not pixel
    // swapping or post-processing. All terms use squared OKLab distance.
    std::array<double, 32> costs{};
    for (std::size_t i = 0; i < palette.size(); ++i) {
        V delta = sub(palette[i], source);
        costs[i] = dot(delta, delta);
    }
    for (int iteration = 0; iteration < 32; ++iteration) {
        double average_cost = 0;
        for (int j = 0; j < mix.size; ++j) {
            average_cost += mix.weights[j] * costs[mix.ids[j]];
        }
        V residual = sub(mix.point, source);
        int entering = -1;
        double gain = -1e-12;
        for (std::size_t i = 0; i < palette.size(); ++i) {
            double value =
                dot(residual, sub(palette[i], mix.point)) + .5 * mixture_penalty * (costs[i] - average_cost);
            if (value < gain) {
                gain = value;
                entering = static_cast<int>(i);
            }
        }
        if (entering < 0) {
            break;
        }
        std::array<int, 5> ids{};
        for (int i = 0; i < mix.size; ++i) {
            ids[i] = mix.ids[i];
        }
        ids[mix.size] = entering;
        const int count = mix.size + 1;
        Mix next = mix;
        double error = dot(residual, residual) + mixture_penalty * average_cost;
        const double before = error;
        // Minimize the convex fit on each small active face with nonnegative
        // affine weights, without an external numerical library.
        for (int bits = 1; bits < (1 << count); ++bits) {
            int n = std::popcount(static_cast<unsigned int>(bits));
            if (n > 4) {
                continue;
            }
            Mix trial;
            trial.size = n;
            int index = 0;
            for (int j = 0; j < count; ++j) {
                if (bits & (1 << j)) {
                    trial.ids[index++] = ids[j];
                }
            }
            V origin = palette[trial.ids[0]];
            std::array<V, 3> edges{};
            std::array<std::array<double, 4>, 3> matrix{};
            std::array<double, 3> solution{};
            for (int j = 0; j < n - 1; ++j) {
                edges[j] = sub(palette[trial.ids[j + 1]], origin);
            }
            for (int j = 0; j < n - 1; ++j) {
                for (int k = 0; k < n - 1; ++k) {
                    matrix[j][k] = dot(edges[j], edges[k]);
                }
                matrix[j][n - 1] = dot(edges[j], sub(source, origin)) -
                                   .5 * mixture_penalty * (costs[trial.ids[j + 1]] - costs[trial.ids[0]]);
            }
            if (n > 1 && !solve(matrix, n - 1, solution.data())) {
                continue;
            }
            trial.weights[0] = 1;
            bool valid = true;
            for (int j = 1; j < n; ++j) {
                trial.weights[j] = solution[j - 1];
                trial.weights[0] -= solution[j - 1];
            }
            for (int j = 0; j < n; ++j) {
                if (trial.weights[j] < -1e-10) {
                    valid = false;
                }
            }
            if (!valid) {
                continue;
            }
            trial.point = {};
            double sum = 0;
            for (int j = 0; j < n; ++j) {
                trial.weights[j] = std::max(0.0, trial.weights[j]);
                sum += trial.weights[j];
            }
            for (int j = 0; j < n; ++j) {
                trial.weights[j] /= sum;
                trial.point = add(trial.point, mul(palette[trial.ids[j]], trial.weights[j]));
            }
            V d = sub(source, trial.point);
            double candidate = dot(d, d);
            for (int j = 0; j < n; ++j) {
                candidate += mixture_penalty * trial.weights[j] * costs[trial.ids[j]];
            }
            if (candidate < error - 1e-15) {
                error = candidate;
                next = trial;
            }
        }
        if (before - error < 1e-15) {
            break;
        }
        mix = next;
    }
    for (int i = 0; i < mix.size; ++i) {
        for (int j = i + 1; j < mix.size; ++j) {
            if (mix.ids[j] < mix.ids[i]) {
                std::swap(mix.ids[i], mix.ids[j]);
                std::swap(mix.weights[i], mix.weights[j]);
            }
        }
    }
    return mix;
}
bool palette_less(Color a, Color b) {
    V x = lab(a), y = lab(b);
    return std::tie(x.x, x.y, x.z) < std::tie(y.x, y.y, y.z);
}
struct PaletteFit {
    std::vector<V> colors;
    double error = std::numeric_limits<double>::infinity();
};
PaletteFit fit_mixtures(const SampleSet& source, std::vector<V> centers, const std::atomic<bool>* cancel) {
    const int count = static_cast<int>(centers.size());
    const std::size_t stride = std::max<std::size_t>(1, (source.values.size() + 4095) / 4096);
    PaletteFit best;
    double previous = std::numeric_limits<double>::infinity();
    for (int iteration = 0; iteration <= 12; ++iteration) {
        std::array<std::array<double, 35>, 32> equations{};
        double objective = 0;
        for (std::size_t i = 0; i < source.values.size(); i += stride) {
            if (i % (stride * 256) == 0 && cancel && (*cancel).load()) {
                return {};
            }
            const Sample& sample = source.values[i];
            Mix mix = project(sample.perceptual, centers);
            V delta = sub(mix.point, sample.perceptual);
            objective += sample.weight * dot(delta, delta);
            for (int j = 0; j < mix.size; ++j) {
                const int a = mix.ids[j];
                double weight = sample.weight * mix.weights[j];
                delta = sub(centers[a], sample.perceptual);
                objective += mixture_penalty * weight * dot(delta, delta);
                for (int k = 0; k < mix.size; ++k) {
                    equations[a][mix.ids[k]] += weight * mix.weights[k];
                }
                equations[a][a] += mixture_penalty * weight;
                equations[a][count] += (1 + mixture_penalty) * weight * sample.perceptual.x;
                equations[a][count + 1] += (1 + mixture_penalty) * weight * sample.perceptual.y;
                equations[a][count + 2] += (1 + mixture_penalty) * weight * sample.perceptual.z;
            }
        }
        if (objective < best.error) {
            best = {centers, objective};
        }
        if (iteration == 12 ||
            (iteration > 0 && std::abs(previous - objective) < 1e-6 * std::max(1.0, previous))) {
            break;
        }
        previous = objective;
        // Holding mixture weights fixed gives a positive normal matrix:
        // W'AW + penalty*diag(W'A1), with RHS (1+penalty)*W'AX.
        // Solve its three OKLab coordinates together; no image-wide matrix exists.
        for (int i = 0; i < count; ++i) {
            if (equations[i][i] < 1e-12) {
                equations[i][i] = 1;
                equations[i][count] = centers[i].x;
                equations[i][count + 1] = centers[i].y;
                equations[i][count + 2] = centers[i].z;
            }
        }
        bool valid = true;
        for (int i = 0; i < count; ++i) {
            int pivot = i;
            for (int j = i + 1; j < count; ++j) {
                if (std::abs(equations[j][i]) > std::abs(equations[pivot][i])) {
                    pivot = j;
                }
            }
            if (std::abs(equations[pivot][i]) < 1e-12) {
                valid = false;
                break;
            }
            std::swap(equations[i], equations[pivot]);
            double divisor = equations[i][i];
            for (int k = i; k < count + 3; ++k) {
                equations[i][k] /= divisor;
            }
            for (int j = 0; j < count; ++j) {
                if (j != i) {
                    double factor = equations[j][i];
                    for (int k = i; k < count + 3; ++k) {
                        equations[j][k] -= factor * equations[i][k];
                    }
                }
            }
        }
        if (!valid) {
            break;
        }
        for (int i = 0; i < count; ++i) {
            // Evaluate the actual 8-bit, in-gamut palette on the next iteration;
            // retain the best iterate if clipping or rounding worsens the fit.
            centers[i] =
                lab(from_oklab({equations[i][count], equations[i][count + 1], equations[i][count + 2]}));
        }
    }
    return best;
}
std::vector<Color> mixture_palette(const SampleSet& source, const std::vector<Color>& initial,
                                   const std::atomic<bool>* cancel) {
    if (source.exact.size() <= initial.size() || initial.empty()) {
        return initial;
    }
    std::vector<V> centers;
    for (Color c : initial) {
        centers.push_back(lab(c));
    }
    PaletteFit best = fit_mixtures(source, centers, cancel);
    // A coverage seed can escape the local solution that spends several colors
    // on a large dark region but merges a small, bright chromatic accent.
    centers.clear();
    V low = tail_mean(source.values, false);
    centers.push_back(lab(from_oklab({low.x, low.y, low.z})));
    std::vector<double> distances(source.values.size(), std::numeric_limits<double>::infinity());
    while (centers.size() < initial.size()) {
        if (cancel && (*cancel).load()) {
            return {};
        }
        std::size_t chosen = 0;
        for (std::size_t i = 0; i < source.values.size(); ++i) {
            V delta = sub(source.values[i].perceptual, centers.back());
            distances[i] = std::min(distances[i], dot(delta, delta));
            if (distances[i] * source.values[i].weight > distances[chosen] * source.values[chosen].weight) {
                chosen = i;
            }
        }
        centers.push_back(source.values[chosen].perceptual);
    }
    PaletteFit coverage = fit_mixtures(source, centers, cancel);
    if (coverage.error < best.error) {
        best = std::move(coverage);
    }
    std::vector<Color> palette;
    for (V color : best.colors) {
        palette.push_back(from_oklab({color.x, color.y, color.z}));
    }
    std::sort(palette.begin(), palette.end(), palette_less);
    return palette;
}
} // namespace
std::uint16_t dither_rank(int x, int y, DitherPattern pattern) {
    unsigned int xx = static_cast<unsigned int>(x), yy = static_cast<unsigned int>(y), rank = 0;
    for (unsigned int j = 0; j < 6; ++j) {
        unsigned int xb = (xx >> j) & 1U, yb = (yy >> j) & 1U;
        if (pattern == DitherPattern::Scrambled) {
            unsigned int cell = hash((xx >> (j + 1)) ^ std::rotl(yy >> (j + 1), 16) ^ (j * 0x9e3779b9U));
            rank = (rank << 1) | (xb ^ yb ^ (cell & 1U));
            rank = (rank << 1) | (yb ^ ((cell >> 1) & 1U));
        } else {
            unsigned int lower = (1U << j) - 1U;
            unsigned int twist = std::popcount((xx & lower) ^ (yy & lower)) & 1U;
            rank = (rank << 1) | (xb ^ yb);
            rank = (rank << 1) | (yb ^ twist);
        }
    }
    if (pattern == DitherPattern::Drift) {
        unsigned int word = (xx >> 6) ^ std::rotl(yy >> 6, 16);
        for (unsigned int j = 0; j < 8; ++j) {
            rank ^= (std::popcount(word & std::rotl(0x9e3779b9U, static_cast<int>(j * 3))) & 1U) << j;
        }
    }
    return static_cast<std::uint16_t>(rank);
}
Image dithered(const Image& source, std::span<const std::uint8_t> mask, DitherOptions options,
               std::vector<Color>* offered, const std::atomic<bool>* cancel) {
    if (!mask.empty() && mask.size() != source.pixels.size()) {
        throw std::invalid_argument("Dither mask dimensions do not match the image.");
    }
    if (options.colors < 2 || options.colors > 32) {
        throw std::invalid_argument("Choose between 2 and 32 colors.");
    }
    SampleSet observations = samples(source, mask, cancel);
    const bool dither = options.pattern != DitherPattern::Posterize;
    std::vector<Color> palette = make_palette(observations, options.colors, dither, cancel);
    if (dither && !(cancel && (*cancel).load())) {
        palette = mixture_palette(observations, palette, cancel);
    }
    if (cancel && (*cancel).load()) {
        return {};
    }
    if (offered) {
        *offered = palette;
    }
    Image output = source;
    if (palette.empty()) {
        return output;
    }
    std::vector<V> colors;
    for (Color c : palette) {
        colors.push_back(lab(c));
    }
    struct CachedMix {
        std::uint32_t key = 0xffffffffU;
        Mix mix;
    };
    // Direct-mapped replacement keeps the cache useful after a photo has filled
    // it. Exact RGB keys prevent quantized lookup artifacts; memory stays bounded.
    std::vector<CachedMix> cache;
    if (options.pattern != DitherPattern::Posterize) {
        cache.resize(65536);
    }
    std::vector<V> current(static_cast<std::size_t>(source.width) + 2), next(current.size());
    for (int y = 0; y < source.height; ++y) {
        if (cancel && (*cancel).load()) {
            return {};
        }
        const bool reverse = (y & 1) != 0;
        const int direction = reverse ? -1 : 1;
        for (int column = 0; column < source.width; ++column) {
            int x = reverse ? source.width - 1 - column : column;
            std::size_t i = static_cast<std::size_t>(y) * source.width + x;
            Color original = source.pixels[i];
            if ((!mask.empty() && !mask[i]) || !original.a) {
                continue;
            }
            int chosen = 0;
            V source_color = lab(original);
            Mix mixture;
            if (options.pattern != DitherPattern::Posterize) {
                const std::uint32_t key = rgb_key(original);
                CachedMix& entry = cache[hash(key) & 65535U];
                if (entry.key != key) {
                    entry.key = key;
                    entry.mix = project(source_color, colors);
                }
                mixture = entry.mix;
            }
            if (options.pattern == DitherPattern::Crosswind || options.pattern == DitherPattern::Posterize) {
                // Transport only representable error. Accumulating an
                // out-of-hull component creates waves and colored edge fringes.
                V target = options.pattern == DitherPattern::Crosswind ? add(mixture.point, current[x + 1])
                                                                       : lab(original);
                double distance = std::numeric_limits<double>::infinity();
                for (std::size_t j = 0; j < palette.size(); ++j) {
                    V delta = sub(target, colors[j]);
                    if (dot(delta, delta) < distance) {
                        distance = dot(delta, delta);
                        chosen = static_cast<int>(j);
                    }
                }
                if (options.pattern == DitherPattern::Crosswind) {
                    const std::array<int, 4> dx{direction, -direction, 0, direction}, dy{0, 1, 1, 1};
                    const std::array<double, 4> base{7, 3, 5, 1};
                    std::array<double, 4> weights{};
                    double sum = 0;
                    for (int j = 0; j < 4; ++j) {
                        int xx = x + dx[j], yy = y + dy[j];
                        if (!allowed(source, mask, xx, yy) || !source.get(xx, yy).a) {
                            continue;
                        }
                        V delta = sub(source_color, lab(source.get(xx, yy)));
                        weights[j] = base[j] *
                                     (.2 + 1.6 * random(static_cast<std::uint32_t>(i) * 31 + j * 911)) *
                                     (.1 + .9 / (1 + dot(delta, delta) / .012));
                        sum += weights[j];
                    }
                    V error = sub(target, colors[chosen]);
                    if (sum > 0) {
                        for (int j = 0; j < 4; ++j) {
                            if (weights[j] > 0) {
                                std::vector<V>& row = dy[j] ? next : current;
                                row[x + dx[j] + 1] = add(row[x + dx[j] + 1], mul(error, weights[j] / sum));
                            }
                        }
                    }
                }
            } else {
                const Mix& mix = mixture;
                double threshold = (dither_rank(x, y, options.pattern) + .5) / 4096.0, cumulative = 0;
                chosen = mix.ids[mix.size - 1];
                for (int j = 0; j < mix.size; ++j) {
                    cumulative += mix.weights[j];
                    if (threshold < cumulative) {
                        chosen = mix.ids[j];
                        break;
                    }
                }
            }
            Color c = palette[chosen];
            c.a = original.a;
            output.pixels[i] = c;
        }
        current.swap(next);
        std::fill(next.begin(), next.end(), V{});
    }
    return output;
}
void DitherBrushStroke::clear() {
    pending_ = 0;
    serial_ = 0;
    started_ = false;
}
void DitherBrushStroke::dab(Image& image, Point center, double diameter, DitherBrushMode mode,
                            std::uint32_t seed, bool wrap, std::span<const std::uint8_t> mask) {
    const double radius = std::max(.5, diameter * .5);
    const int left = static_cast<int>(std::floor(center.x - radius)),
              right = static_cast<int>(std::ceil(center.x + radius));
    const int top = static_cast<int>(std::floor(center.y - radius)),
              bottom = static_cast<int>(std::ceil(center.y + radius));
    std::vector<std::pair<std::size_t, Color>> changes;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            double dx = x - center.x, dy = y - center.y, r2 = (dx * dx + dy * dy) / (radius * radius);
            if (r2 >= 1) {
                continue;
            }
            int xx = wrap ? (x % image.width + image.width) % image.width : x,
                yy = wrap ? (y % image.height + image.height) % image.height : y;
            if (!allowed(image, mask, xx, yy)) {
                continue;
            }
            std::uint32_t noise = hash(static_cast<std::uint32_t>(x) * 374761393U ^
                                       static_cast<std::uint32_t>(y) * 668265263U ^ seed ^ serial_ * 104729U);
            if (random(noise) > .28 * (1 - r2) * (1 - r2)) {
                continue;
            }
            Color original = image.get(xx, yy);
            if (!original.a) {
                continue;
            }
            Color color;
            if (mode == DitherBrushMode::Neighborhood) {
                V mean{};
                double mass = 0;
                for (int j = -2; j <= 2; ++j) {
                    for (int k = -2; k <= 2; ++k) {
                        int sx = xx + k, sy = yy + j;
                        if (wrap) {
                            sx = (sx % image.width + image.width) % image.width;
                            sy = (sy % image.height + image.height) % image.height;
                        }
                        if (!allowed(image, mask, sx, sy)) {
                            continue;
                        }
                        Color neighbor = image.get(sx, sy);
                        double a = neighbor.a / 255.0;
                        mean = add(mean, mul(linear(neighbor), a));
                        mass += a;
                    }
                }
                if (mass <= 0) {
                    continue;
                }
                color = encoded(mul(add(linear(original), mul(mean, 1 / mass)), .5), original.a);
            } else {
                Lab value = to_oklab(original);
                value.l += .025 * (2 * random(noise ^ 0xa511e9b3U) - 1);
                double saturation = 1 + .1 * (2 * random(noise ^ 0x63d83595U) - 1);
                value.a *= saturation;
                value.b *= saturation;
                color = from_oklab(value);
                color.a = original.a;
            }
            changes.emplace_back(static_cast<std::size_t>(yy) * image.width + xx, color);
        }
    }
    // All neighborhood reads precede writes, so one dab has no scan-direction feedback.
    for (const std::pair<std::size_t, Color>& change : changes) {
        image.pixels[change.first] = change.second;
    }
    ++serial_;
}
void DitherBrushStroke::segment(Image& image, Point start, Point end, double diameter, DitherBrushMode mode,
                                std::uint32_t seed, bool wrap, std::span<const std::uint8_t> mask) {
    if (image.width <= 0 || image.height <= 0) {
        return;
    }
    if (!mask.empty() && mask.size() != image.pixels.size()) {
        throw std::invalid_argument("Brush mask dimensions do not match the image.");
    }
    diameter = std::clamp(diameter, 1.0, 1024.0);
    double spacing = std::max(1.0, diameter * .12), distance = std::hypot(end.x - start.x, end.y - start.y);
    if (!started_) {
        dab(image, start, diameter, mode, seed, wrap, mask);
        started_ = true;
    }
    if (distance <= 0) {
        return;
    }
    double position = spacing - pending_;
    for (; position <= distance; position += spacing) {
        double t = position / distance;
        dab(image, {start.x + t * (end.x - start.x), start.y + t * (end.y - start.y)}, diameter, mode, seed,
            wrap, mask);
    }
    pending_ = std::fmod(pending_ + distance, spacing);
}
} // namespace paint
