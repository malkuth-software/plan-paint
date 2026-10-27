#include "codecs.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>
namespace paint {
namespace {
class Bytes {
  public:
    const std::uint8_t* data;
    std::size_t size;
    void check(std::size_t offset, std::size_t count) const {
        if (offset > size || count > size - offset) {
            throw std::runtime_error("Truncated ICO/CUR image.");
        }
    }
    unsigned u16(std::size_t offset) const {
        check(offset, 2);
        return data[offset] | (unsigned(data[offset + 1]) << 8);
    }
    std::uint32_t u32(std::size_t offset) const {
        check(offset, 4);
        return data[offset] | (std::uint32_t(data[offset + 1]) << 8) |
               (std::uint32_t(data[offset + 2]) << 16) | (std::uint32_t(data[offset + 3]) << 24);
    }
    std::uint32_t big32(std::size_t offset) const {
        check(offset, 4);
        return (std::uint32_t(data[offset]) << 24) | (std::uint32_t(data[offset + 1]) << 16) |
               (std::uint32_t(data[offset + 2]) << 8) | data[offset + 3];
    }
};
IconFormat png_format(Bytes bytes) {
    bytes.check(0, 33);
    if (bytes.big32(8) != 13 || std::memcmp(bytes.data + 12, "IHDR", 4) != 0) {
        throw std::runtime_error("ICO/CUR PNG header is invalid.");
    }
    const unsigned depth = bytes.data[24], type = bytes.data[25];
    const unsigned channels = type == 0 || type == 3 ? 1 : type == 2 ? 3 : type == 4 ? 2 : type == 6 ? 4 : 0;
    if (!channels) {
        throw std::runtime_error("ICO/CUR PNG color type is invalid.");
    }
    IconFormat format{IconStorage::Png, depth * channels, 0};
    for (std::size_t offset = 8; offset < bytes.size;) {
        bytes.check(offset, 12);
        const std::size_t length = bytes.big32(offset);
        bytes.check(offset + 8, length + 4);
        if (type == 3 && std::memcmp(bytes.data + offset + 4, "PLTE", 4) == 0) {
            if (length == 0 || length > 768 || length % 3 != 0) {
                throw std::runtime_error("ICO/CUR PNG palette is invalid.");
            }
            format.palette_colors = static_cast<unsigned>(length / 3);
        }
        if (std::memcmp(bytes.data + offset + 4, "IEND", 4) == 0) {
            break;
        }
        offset += length + 12;
    }
    return format;
}
void put16(std::vector<std::uint8_t>& bytes, unsigned value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    put16(bytes, value & 65535);
    put16(bytes, value >> 16);
}
std::uint8_t component(std::uint32_t value, std::uint32_t mask, std::uint8_t fallback) {
    if (!mask) {
        return fallback;
    }
    int shift = std::countr_zero(mask);
    std::uint32_t maximum = mask >> shift;
    if ((maximum & (maximum + 1)) != 0) {
        throw std::runtime_error("ICO/CUR contains a noncontiguous color mask.");
    }
    return static_cast<std::uint8_t>((std::uint64_t((value & mask) >> shift) * 255 + maximum / 2) / maximum);
}
IconFrame decode_dib(Bytes bytes, int width, int height, IconSource& retained) {
    unsigned header = bytes.u32(0);
    if (header != 12 && header != 40 && header != 52 && header != 56 && header != 108 && header != 124) {
        throw std::runtime_error("Unsupported ICO/CUR bitmap header.");
    }
    bytes.check(0, header);
    int dib_width = header == 12 ? int(bytes.u16(4)) : static_cast<std::int32_t>(bytes.u32(4));
    int dib_height = header == 12 ? int(bytes.u16(6)) : static_cast<std::int32_t>(bytes.u32(8));
    unsigned planes = bytes.u16(header == 12 ? 8 : 12), bpp = bytes.u16(header == 12 ? 10 : 14);
    unsigned compression = header == 12 ? 0 : bytes.u32(16);
    if (dib_width != width || (dib_height != height * 2 && dib_height != -height * 2) || planes != 1 ||
        (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) ||
        (compression != 0 && compression != 3 && compression != 6) ||
        (compression != 0 && bpp != 16 && bpp != 32)) {
        throw std::runtime_error("Invalid or unsupported ICO/CUR bitmap layout.");
    }
    std::size_t offset = header;
    std::uint32_t masks[4] = {bpp == 16 ? 0x7c00u : 0xff0000u, bpp == 16 ? 0x3e0u : 0xff00u,
                              bpp == 16 ? 0x1fu : 0xffu, bpp == 32 ? 0xff000000u : 0u};
    if (compression == 3 || compression == 6) {
        std::size_t mask_start = header == 40 ? offset : 40;
        int count = compression == 6 || header >= 56 ? 4 : 3;
        for (int i = 0; i < count; ++i) {
            masks[i] = bytes.u32(mask_start + i * 4);
        }
        if (count == 3) {
            masks[3] = 0;
        }
        if (header == 40) {
            offset += count * 4;
        }
        if (!masks[0] || !masks[1] || !masks[2] || (masks[0] & masks[1]) || (masks[0] & masks[2]) ||
            (masks[1] & masks[2]) || (masks[3] & (masks[0] | masks[1] | masks[2]))) {
            throw std::runtime_error("ICO/CUR color masks overlap or are missing.");
        }
    }
    unsigned palette_count = header == 12 ? 0 : bytes.u32(32);
    if (!palette_count && bpp <= 8) {
        palette_count = 1u << bpp;
    }
    if (palette_count > 256 || (bpp <= 8 && palette_count > (1u << bpp))) {
        throw std::runtime_error("Invalid ICO/CUR palette.");
    }
    std::vector<Color> palette;
    for (unsigned i = 0; i < palette_count; ++i) {
        bytes.check(offset, header == 12 ? 3 : 4);
        palette.push_back({bytes.data[offset + 2], bytes.data[offset + 1], bytes.data[offset], 255});
        offset += header == 12 ? 3 : 4;
    }
    std::size_t stride = ((width * bpp + 31) / 32) * 4, mask_stride = ((width + 31) / 32) * 4;
    bytes.check(offset, stride * height);
    std::size_t mask_offset = offset + stride * height;
    bool has_mask = mask_offset <= bytes.size && mask_stride * height <= bytes.size - mask_offset;
    if (!has_mask && bpp != 32) {
        throw std::runtime_error("ICO/CUR transparency mask is truncated.");
    }
    IconFrame result;
    result.image.reset(width, height, {0, 0, 0, 0});
    if (bpp <= 8) {
        retained.palette = palette;
        retained.indices.resize(result.image.pixels.size());
    }
    bool has_alpha = false;
    for (int y = 0; y < height; ++y) {
        int row = dib_height < 0 ? y : height - 1 - y;
        const std::uint8_t* source = bytes.data + offset + row * stride;
        for (int x = 0; x < width; ++x) {
            Color color{};
            if (bpp <= 8) {
                unsigned index = bpp == 8   ? source[x]
                                 : bpp == 4 ? (source[x / 2] >> ((1 - x % 2) * 4)) & 15
                                            : (source[x / 8] >> (7 - x % 8)) & 1;
                if (index >= palette.size()) {
                    throw std::runtime_error("ICO/CUR palette index is invalid.");
                }
                color = palette[index];
                retained.indices[static_cast<std::size_t>(y) * width + x] = index;
            } else if (bpp == 24) {
                color = {source[x * 3 + 2], source[x * 3 + 1], source[x * 3], 255};
            } else {
                std::uint32_t value = source[x * (bpp / 8)] | (unsigned(source[x * (bpp / 8) + 1]) << 8);
                if (bpp == 32) {
                    value |=
                        (std::uint32_t(source[x * 4 + 2]) << 16) | (std::uint32_t(source[x * 4 + 3]) << 24);
                }
                color = {component(value, masks[0], 0), component(value, masks[1], 0),
                         component(value, masks[2], 0), component(value, masks[3], bpp == 32 ? 0 : 255)};
                if (bpp == 32 && color.a != 0) {
                    has_alpha = true;
                }
            }
            result.image.set(x, y, color);
        }
    }
    for (int y = 0; y < height; ++y) {
        int row = dib_height < 0 ? y : height - 1 - y;
        for (int x = 0; x < width; ++x) {
            Color& color = result.image.pixels[y * width + x];
            if (bpp == 32 && has_alpha) {
                continue;
            }
            bool masked =
                has_mask && ((bytes.data[mask_offset + row * mask_stride + x / 8] >> (7 - x % 8)) & 1);
            color.a = masked ? 0 : 255;
            if (masked && (color.r || color.g || color.b)) {
                if (result.xor_pixels.empty()) {
                    result.xor_pixels.resize(result.image.pixels.size(), {0, 0, 0, 0});
                }
                result.xor_pixels[y * width + x] = {color.r, color.g, color.b, 255};
                color = {255, 255, 255, 0};
            } else if (masked) {
                color = {0, 0, 0, 0};
            }
        }
    }
    retained.format = {IconStorage::Dib, bpp, bpp <= 8 ? palette_count : 0};
    return result;
}
std::vector<std::uint8_t> encode_dib(const IconFrame& frame) {
    const Image& image = frame.image;
    bool has_xor = false;
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        if (legacy_xor_pixel(frame, i)) {
            has_xor = true;
            break;
        }
    }
    if (has_xor) {
        for (std::size_t i = 0; i < image.pixels.size(); ++i) {
            if (image.pixels[i].a != 0 && image.pixels[i].a != 255) {
                throw std::runtime_error("Legacy XOR cursor pixels cannot share a bitmap with partial alpha. "
                                         "Erase the XOR pixels or use opaque colors before saving.");
            }
        }
    }
    std::size_t mask_stride = ((image.width + 31) / 32) * 4;
    std::vector<std::uint8_t> bytes;
    put32(bytes, 40);
    put32(bytes, image.width);
    put32(bytes, image.height * 2);
    put16(bytes, 1);
    put16(bytes, 32);
    put32(bytes, 0);
    put32(bytes, static_cast<std::uint32_t>(image.pixels.size() * 4 + mask_stride * image.height));
    put32(bytes, 0);
    put32(bytes, 0);
    put32(bytes, 0);
    put32(bytes, 0);
    for (int y = image.height - 1; y >= 0; --y) {
        for (int x = 0; x < image.width; ++x) {
            std::size_t index = y * image.width + x;
            Color c = image.pixels[index];
            if (legacy_xor_pixel(frame, index)) {
                c = frame.xor_pixels[index];
            } else if (!c.a) {
                c = {0, 0, 0, 0};
            }
            bytes.push_back(c.b);
            bytes.push_back(c.g);
            bytes.push_back(c.r);
            bytes.push_back(has_xor ? 0 : c.a);
        }
    }
    for (int y = image.height - 1; y >= 0; --y) {
        std::size_t row = bytes.size();
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
ImageContainer decode_icon_container(const void* data, std::size_t size) {
    Bytes bytes{static_cast<const std::uint8_t*>(data), size};
    if (size > 256000000 || bytes.u16(0) != 0 || (bytes.u16(2) != 1 && bytes.u16(2) != 2)) {
        throw std::runtime_error("Invalid ICO/CUR header.");
    }
    int count = static_cast<int>(bytes.u16(4));
    if (count < 1 || count > 256) {
        throw std::runtime_error("ICO/CUR must contain between 1 and 256 images.");
    }
    bytes.check(6, count * 16);
    ImageContainer result;
    result.kind = bytes.u16(2) == 2 ? ContainerKind::Cursor : ContainerKind::Icon;
    std::size_t retained_payload_bytes = 0;
    for (int i = 0; i < count; ++i) {
        std::size_t entry = 6 + i * 16, offset = bytes.u32(entry + 12), length = bytes.u32(entry + 8);
        int width = bytes.data[entry] ? bytes.data[entry] : 256,
            height = bytes.data[entry + 1] ? bytes.data[entry + 1] : 256;
        if (offset < 6 + static_cast<std::size_t>(count) * 16 || length < 8) {
            throw std::runtime_error("ICO/CUR image directory is invalid.");
        }
        bytes.check(offset, length);
        if (length > 256000000 - retained_payload_bytes) {
            throw std::runtime_error("ICO/CUR retained image data exceeds the memory limit.");
        }
        retained_payload_bytes += length;
        IconFrame frame;
        std::shared_ptr<IconSource> source = std::make_shared<IconSource>();
        const unsigned char png[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        if (std::memcmp(bytes.data + offset, png, 8) == 0) {
            frame.image = decode_image(bytes.data + offset, length);
            if (frame.image.width != width || frame.image.height != height) {
                throw std::runtime_error("ICO/CUR PNG dimensions disagree with its directory.");
            }
            (*source).format = png_format({bytes.data + offset, length});
        } else {
            frame = decode_dib({bytes.data + offset, length}, width, height, *source);
        }
        if (result.kind == ContainerKind::Cursor) {
            frame.hotspot_x = bytes.u16(entry + 4);
            frame.hotspot_y = bytes.u16(entry + 6);
            if (frame.hotspot_x >= width || frame.hotspot_y >= height) {
                throw std::runtime_error("CUR hotspot is outside its image.");
            }
        }
        (*source).image = frame.image;
        (*source).xor_pixels = frame.xor_pixels;
        (*source).payload.assign(bytes.data + offset, bytes.data + offset + length);
        frame.source = std::move(source);
        result.frames.push_back(std::move(frame));
    }
    return result;
}
std::vector<std::uint8_t> encode_icon_container(const ImageContainer& container) {
    if (container.frames.empty() || container.frames.size() > 256 || container.kind == ContainerKind::Image) {
        throw std::runtime_error("Choose 1 to 256 icon or cursor sizes.");
    }
    std::vector<std::vector<std::uint8_t>> payloads;
    for (std::size_t i = 0; i < container.frames.size(); ++i) {
        const IconFrame& frame = container.frames[i];
        if (frame.image.width < 1 || frame.image.height < 1 || frame.image.width > 256 ||
            frame.image.height > 256 || frame.hotspot_x < 0 || frame.hotspot_y < 0 ||
            frame.hotspot_x >= frame.image.width || frame.hotspot_y >= frame.image.height ||
            frame.image.pixels.size() != static_cast<std::size_t>(frame.image.width) * frame.image.height) {
            throw std::runtime_error(
                "ICO/CUR dimensions or hotspot are invalid (maximum 256 by 256 pixels).");
        }
        if (icon_source_matches(frame)) {
            payloads.push_back((*frame.source).payload);
        } else {
            const IconFormat format = icon_output_format(frame, container.kind);
            payloads.push_back(format.storage == IconStorage::Png ? encode_png(frame.image)
                                                                  : encode_dib(frame));
        }
    }
    std::vector<std::uint8_t> bytes;
    put16(bytes, 0);
    put16(bytes, container.kind == ContainerKind::Cursor ? 2 : 1);
    put16(bytes, static_cast<unsigned>(payloads.size()));
    std::uint32_t offset = 6 + static_cast<std::uint32_t>(payloads.size()) * 16;
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        const IconFrame& frame = container.frames[i];
        bytes.push_back(static_cast<std::uint8_t>(frame.image.width));
        bytes.push_back(static_cast<std::uint8_t>(frame.image.height));
        const IconFormat format = icon_output_format(frame, container.kind);
        bytes.push_back(static_cast<std::uint8_t>(format.palette_colors < 256 ? format.palette_colors : 0));
        bytes.push_back(0);
        put16(bytes, container.kind == ContainerKind::Cursor ? frame.hotspot_x : 1);
        put16(bytes, container.kind == ContainerKind::Cursor ? frame.hotspot_y : format.bits_per_pixel);
        put32(bytes, static_cast<std::uint32_t>(payloads[i].size()));
        put32(bytes, offset);
        offset += static_cast<std::uint32_t>(payloads[i].size());
    }
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        bytes.insert(bytes.end(), payloads[i].begin(), payloads[i].end());
    }
    return bytes;
}
} // namespace paint
