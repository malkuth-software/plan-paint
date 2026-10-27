#include "icon_palette.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
namespace paint {
namespace {
struct PaletteSample {
    Color color;
    std::uint32_t key = 0;
    std::size_t count = 0;
};
struct PaletteBox {
    std::vector<PaletteSample> samples;
    std::size_t population = 0;
    unsigned axis = 0, range = 0;
};
unsigned component(Color color, unsigned axis) {
    return axis == 0 ? color.r : axis == 1 ? color.g : color.b;
}
struct SampleOrder {
    unsigned axis;
    bool operator()(const PaletteSample& left, const PaletteSample& right) const {
        const unsigned a = component(left.color, axis), b = component(right.color, axis);
        return a == b ? left.key < right.key : a < b;
    }
};
void measure(PaletteBox& box) {
    box.population = 0;
    box.range = 0;
    for (const PaletteSample& sample : box.samples) {
        box.population += sample.count;
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        unsigned low = 255, high = 0;
        for (const PaletteSample& sample : box.samples) {
            low = std::min(low, component(sample.color, axis));
            high = std::max(high, component(sample.color, axis));
        }
        if (high - low > box.range) {
            box.range = high - low;
            box.axis = axis;
        }
    }
}
std::vector<Color> palette_for(const Image& image, unsigned capacity, bool reserve_black) {
    std::map<std::uint32_t, std::size_t> histogram;
    for (Color color : image.pixels) {
        if (color.a) {
            const std::uint32_t key = (unsigned(color.r) << 16) | (unsigned(color.g) << 8) | color.b;
            if (key != 0 || !reserve_black) {
                ++histogram[key];
            }
        }
    }
    PaletteBox initial;
    for (const std::pair<const std::uint32_t, std::size_t>& entry : histogram) {
        initial.samples.push_back(
            {{static_cast<std::uint8_t>(entry.first >> 16), static_cast<std::uint8_t>(entry.first >> 8),
              static_cast<std::uint8_t>(entry.first), 255},
             entry.first,
             entry.second});
    }
    if (initial.samples.empty()) {
        return {};
    }
    measure(initial);
    std::vector<PaletteBox> boxes;
    boxes.push_back(std::move(initial));
    while (boxes.size() < capacity) {
        std::size_t best = boxes.size(), score = 0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const std::size_t candidate = boxes[i].population * boxes[i].range;
            if (boxes[i].samples.size() > 1 && candidate > score) {
                best = i;
                score = candidate;
            }
        }
        if (best == boxes.size()) {
            break;
        }
        PaletteBox& box = boxes[best];
        std::sort(box.samples.begin(), box.samples.end(), SampleOrder{box.axis});
        std::size_t cut = 0, mass = 0;
        do {
            mass += box.samples[cut].count;
            ++cut;
        } while (cut < box.samples.size() - 1 && mass * 2 < box.population);
        PaletteBox right;
        right.samples.assign(box.samples.begin() + static_cast<std::ptrdiff_t>(cut), box.samples.end());
        box.samples.resize(cut);
        measure(box);
        measure(right);
        boxes.push_back(std::move(right));
    }
    std::vector<Color> result;
    for (const PaletteBox& box : boxes) {
        std::array<double, 3> sum{};
        for (const PaletteSample& sample : box.samples) {
            for (unsigned axis = 0; axis < 3; ++axis) {
                sum[axis] += component(sample.color, axis) * static_cast<double>(sample.count);
            }
        }
        result.push_back({static_cast<std::uint8_t>(std::lround(sum[0] / box.population)),
                          static_cast<std::uint8_t>(std::lround(sum[1] / box.population)),
                          static_cast<std::uint8_t>(std::lround(sum[2] / box.population)), 255});
    }
    return result;
}
void append_word(std::vector<std::uint8_t>& bytes, unsigned value, unsigned count) {
    for (unsigned byte = 0; byte < count; ++byte) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
    }
}
std::vector<std::uint8_t> indexed_dib(const Image& image, const std::vector<Color>& palette,
                                      const std::vector<unsigned>& indices, unsigned depth) {
    const unsigned color_count = 1u << depth;
    const unsigned stride = ((unsigned(image.width) * depth + 31) / 32) * 4;
    const unsigned mask_stride = ((unsigned(image.width) + 31) / 32) * 4;
    std::vector<std::uint8_t> bytes;
    append_word(bytes, 40, 4);
    append_word(bytes, image.width, 4);
    append_word(bytes, image.height * 2, 4);
    append_word(bytes, 1, 2);
    append_word(bytes, depth, 2);
    append_word(bytes, 0, 4);
    append_word(bytes, (stride + mask_stride) * image.height, 4);
    append_word(bytes, 0, 4);
    append_word(bytes, 0, 4);
    append_word(bytes, color_count, 4);
    append_word(bytes, 0, 4);
    for (unsigned i = 0; i < color_count; ++i) {
        const Color color = i < palette.size() ? palette[i] : Color{0, 0, 0, 255};
        bytes.insert(bytes.end(), {color.b, color.g, color.r, 0});
    }
    for (int y = image.height - 1; y >= 0; --y) {
        const std::size_t row = bytes.size();
        bytes.resize(row + stride, 0);
        for (int x = 0; x < image.width; ++x) {
            const unsigned bit = unsigned(x) * depth;
            bytes[row + bit / 8] |= static_cast<std::uint8_t>(
                indices[static_cast<std::size_t>(y) * image.width + x] << (8 - depth - bit % 8));
        }
    }
    for (int y = image.height - 1; y >= 0; --y) {
        const std::size_t row = bytes.size();
        bytes.resize(row + mask_stride, 0);
        for (int x = 0; x < image.width; ++x) {
            if (!image.get(x, y).a) {
                bytes[row + x / 8] |= static_cast<std::uint8_t>(128 >> (x % 8));
            }
        }
    }
    return bytes;
}
} // namespace
IconPaletteResult convert_icon_palette(const IconFrame& source, const IconPaletteOptions& options) {
    const Image& image = source.image;
    if (image.width < 1 || image.height < 1 || image.width > 256 || image.height > 256 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height ||
        (options.bits_per_pixel != 1 && options.bits_per_pixel != 4 && options.bits_per_pixel != 8) ||
        options.alpha_threshold < 1 || options.alpha_threshold > 255 || source.hotspot_x < 0 ||
        source.hotspot_y < 0 || source.hotspot_x >= image.width || source.hotspot_y >= image.height ||
        (options.dither != IconPaletteDither::None && options.dither != IconPaletteDither::FloydSteinberg)) {
        throw std::runtime_error("Invalid icon palette dimensions, depth or threshold.");
    }
    IconPaletteResult result;
    result.frame.hotspot_x = source.hotspot_x;
    result.frame.hotspot_y = source.hotspot_y;
    Image prepared = image;
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        if (legacy_xor_pixel(source, i)) {
            throw std::runtime_error("Palette conversion cannot preserve background-dependent XOR pixels.");
        }
        const Color color = image.pixels[i];
        result.partial_pixels += color.a > 0 && color.a < 255;
        if (color.a < options.alpha_threshold) {
            prepared.pixels[i] = {0, 0, 0, 0};
            ++result.transparent_pixels;
        } else {
            const double alpha = color.a / 255.0;
            prepared.pixels[i] = {
                static_cast<std::uint8_t>(std::lround(color.r * alpha + options.matte.r * (1 - alpha))),
                static_cast<std::uint8_t>(std::lround(color.g * alpha + options.matte.g * (1 - alpha))),
                static_cast<std::uint8_t>(std::lround(color.b * alpha + options.matte.b * (1 - alpha))), 255};
        }
    }
    const bool transparent = result.transparent_pixels != 0;
    const unsigned first_color = transparent ? 1 : 0;
    unsigned transparent_index = 0;
    if (options.fixed_palette.empty()) {
        result.palette = palette_for(prepared, (1u << options.bits_per_pixel) - first_color, transparent);
        if (transparent) {
            result.palette.insert(result.palette.begin(), {0, 0, 0, 255});
        }
    } else {
        if (options.fixed_palette.size() > (1u << options.bits_per_pixel)) {
            throw std::runtime_error("The shared palette exceeds the selected color depth.");
        }
        result.palette = options.fixed_palette;
        bool black = false;
        for (std::size_t index = 0; index < result.palette.size(); ++index) {
            const Color color = result.palette[index];
            if (color.a != 255) {
                throw std::runtime_error("Palette entries require opaque RGB colors.");
            }
            if (!black && !color.r && !color.g && !color.b) {
                transparent_index = static_cast<unsigned>(index);
                black = true;
            }
        }
        if (transparent && !black) {
            throw std::runtime_error("A shared palette needs a black entry for transparent icon pixels.");
        }
    }
    result.frame.image = prepared;
    std::vector<unsigned> indices(image.pixels.size(), transparent_index);
    std::vector<std::array<double, 3>> current(image.width + 2), next(image.width + 2);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * image.width + x;
            const Color color = prepared.pixels[index];
            if (!color.a) {
                continue;
            }
            std::array<double, 3> target{};
            for (unsigned axis = 0; axis < 3; ++axis) {
                target[axis] = std::clamp(component(color, axis) + current[x + 1][axis], 0.0, 255.0);
            }
            double distance = std::numeric_limits<double>::max();
            unsigned chosen = 0;
            for (unsigned i = 0; i < result.palette.size(); ++i) {
                double candidate = 0;
                for (unsigned axis = 0; axis < 3; ++axis) {
                    const double delta = target[axis] - component(result.palette[i], axis);
                    candidate += delta * delta;
                }
                if (candidate < distance) {
                    distance = candidate;
                    chosen = i;
                }
            }
            indices[index] = chosen;
            result.frame.image.pixels[index] = result.palette[chosen];
            if (options.dither == IconPaletteDither::FloydSteinberg) {
                for (unsigned axis = 0; axis < 3; ++axis) {
                    const double error = target[axis] - component(result.palette[chosen], axis);
                    current[x + 2][axis] += error * (7.0 / 16);
                    next[x][axis] += error * (3.0 / 16);
                    next[x + 1][axis] += error * (5.0 / 16);
                    next[x + 2][axis] += error * (1.0 / 16);
                }
            }
        }
        current.swap(next);
        std::fill(next.begin(), next.end(), std::array<double, 3>{});
    }
    std::shared_ptr<IconSource> encoded = std::make_shared<IconSource>();
    (*encoded).format = {IconStorage::Dib, options.bits_per_pixel, 1u << options.bits_per_pixel};
    (*encoded).palette = result.palette;
    (*encoded).palette.resize(1u << options.bits_per_pixel, {0, 0, 0, 255});
    (*encoded).indices = indices;
    (*encoded).image = result.frame.image;
    (*encoded).payload = indexed_dib(result.frame.image, result.palette, indices, options.bits_per_pixel);
    result.frame.source = std::move(encoded);
    return result;
}
bool has_icon_palette(const IconFrame& frame) {
    if (frame.image.width < 1 || frame.image.height < 1 || frame.image.width > 256 ||
        frame.image.height > 256 ||
        frame.image.pixels.size() != static_cast<std::size_t>(frame.image.width) * frame.image.height ||
        !icon_source_matches(frame)) {
        return false;
    }
    const IconSource& encoded = *frame.source;
    const unsigned depth = encoded.format.bits_per_pixel;
    if (encoded.format.storage != IconStorage::Dib || (depth != 1 && depth != 4 && depth != 8) ||
        encoded.palette.empty() || encoded.palette.size() > (1u << depth) ||
        encoded.indices.size() != frame.image.pixels.size()) {
        return false;
    }
    for (std::size_t i = 0; i < encoded.indices.size(); ++i) {
        if (encoded.indices[i] >= encoded.palette.size() || legacy_xor_pixel(frame, i)) {
            return false;
        }
    }
    return true;
}
namespace {
void require_palette(const IconFrame& frame, unsigned entry) {
    if (!has_icon_palette(frame)) {
        throw std::runtime_error("Convert this image to an indexed palette before editing entries.");
    }
    if (entry >= (*frame.source).palette.size()) {
        throw std::runtime_error("Palette entry is outside the current palette.");
    }
}
void publish_indexed_edit(IconFrame& frame, std::shared_ptr<IconSource> encoded) {
    (*encoded).image = frame.image;
    (*encoded).xor_pixels.clear();
    (*encoded).payload =
        indexed_dib(frame.image, (*encoded).palette, (*encoded).indices, (*encoded).format.bits_per_pixel);
    (*encoded).format.palette_colors = 1u << (*encoded).format.bits_per_pixel;
    (*encoded).palette.resize((*encoded).format.palette_colors, {0, 0, 0, 255});
    frame.source = std::move(encoded);
}
} // namespace
IconFrame edit_icon_palette_entry(const IconFrame& source, unsigned entry, Color color) {
    require_palette(source, entry);
    if (color.a != 255) {
        throw std::runtime_error("Indexed palette entries require opaque RGB colors.");
    }
    const IconSource& original = *source.source;
    for (std::size_t i = 0; i < original.indices.size(); ++i) {
        if (original.indices[i] == entry && !source.image.pixels[i].a && (color.r || color.g || color.b)) {
            throw std::runtime_error("This black entry is used by transparent pixels and must remain black.");
        }
    }
    if (equal(original.palette[entry], color)) {
        return source;
    }
    IconFrame result = source;
    std::shared_ptr<IconSource> encoded = std::make_shared<IconSource>(original);
    (*encoded).palette[entry] = color;
    for (std::size_t i = 0; i < original.indices.size(); ++i) {
        if (original.indices[i] == entry && result.image.pixels[i].a) {
            result.image.pixels[i] = color;
        }
    }
    publish_indexed_edit(result, std::move(encoded));
    return result;
}
IconFrame paint_icon_indices(const IconFrame& source, const std::vector<std::size_t>& pixels, unsigned entry,
                             bool transparent) {
    require_palette(source, entry);
    const IconSource& original = *source.source;
    Color color = original.palette[entry];
    if (transparent && (color.r || color.g || color.b)) {
        throw std::runtime_error("Transparent indexed pixels require a black palette entry.");
    }
    color.a = transparent ? 0 : 255;
    for (std::size_t pixel : pixels) {
        if (pixel >= original.indices.size()) {
            throw std::runtime_error("Indexed paint position is outside the current image.");
        }
    }
    bool changed = false;
    IconFrame result = source;
    std::shared_ptr<IconSource> encoded = std::make_shared<IconSource>(original);
    for (std::size_t pixel : pixels) {
        changed = changed || (*encoded).indices[pixel] != entry || !equal(result.image.pixels[pixel], color);
        (*encoded).indices[pixel] = entry;
        result.image.pixels[pixel] = color;
    }
    if (!changed) {
        return source;
    }
    publish_indexed_edit(result, std::move(encoded));
    return result;
}
} // namespace paint
