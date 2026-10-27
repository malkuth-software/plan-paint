#include "icon_validation.hpp"
#include "codecs.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>
namespace paint {
namespace {
void check_cancel(const std::atomic<bool>* cancel) {
    if (cancel && (*cancel).load()) {
        throw std::runtime_error("Icon validation cancelled.");
    }
}
std::array<std::uint64_t, 3> alpha_counts(const Image& image) {
    std::array<std::uint64_t, 3> result{};
    for (Color pixel : image.pixels) {
        ++result[pixel.a == 0 ? 0 : pixel.a == 255 ? 2 : 1];
    }
    return result;
}
std::string quoted(const std::string& text) {
    std::string result = "\"";
    const char hex[] = "0123456789abcdef";
    for (unsigned char value : text) {
        if (value == '"' || value == '\\') {
            result += '\\';
            result += char(value);
        } else if (value < 32) {
            result += "\\u00";
            result += hex[value / 16];
            result += hex[value % 16];
        } else {
            result += char(value);
        }
    }
    return result + '"';
}
std::string alpha_json(const std::array<std::uint64_t, 3>& counts) {
    return "[" + std::to_string(counts[0]) + "," + std::to_string(counts[1]) + "," +
           std::to_string(counts[2]) + "]";
}
std::string format_json(IconFormat format) {
    return "{\"storage\":" +
           quoted(format.storage == IconStorage::Png   ? "png"
                  : format.storage == IconStorage::Dib ? "dib"
                                                       : "unknown") +
           ",\"bitsPerPixel\":" + std::to_string(format.bits_per_pixel) + "}";
}
} // namespace
std::vector<int> parse_validation_sizes(const std::string& text) {
    std::vector<int> result;
    std::size_t start = 0;
    for (;;) {
        const std::size_t comma = text.find(',', start);
        const std::string part = text.substr(start, comma == std::string::npos ? comma : comma - start);
        const std::size_t first = part.find_first_not_of(" \t\r\n"), last = part.find_last_not_of(" \t\r\n");
        if (first == std::string::npos) {
            throw std::runtime_error("Enter comma-separated sizes from 1 to 256.");
        }
        const std::string token = part.substr(first, last - first + 1);
        int size = 0;
        const std::from_chars_result parsed =
            std::from_chars(token.data(), token.data() + token.size(), size);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || size < 1 || size > 256 ||
            result.size() >= 32 || std::find(result.begin(), result.end(), size) != result.end()) {
            throw std::runtime_error("Choose 1 to 32 distinct sizes from 1 to 256.");
        }
        result.push_back(size);
        if (comma == std::string::npos) {
            return result;
        }
        start = comma + 1;
    }
}
IconValidation validate_icon_output(const ImageContainer& source, const IconValidationProfile& profile,
                                    const std::atomic<bool>* cancel) {
    check_cancel(cancel);
    if (source.kind == ContainerKind::Image || source.frames.empty() || source.frames.size() > 256 ||
        profile.sizes.empty() || profile.sizes.size() > 32 ||
        (profile.depth != 0 && profile.depth != 4 && profile.depth != 8 && profile.depth != 32)) {
        throw std::runtime_error("Invalid icon validation profile or container.");
    }
    std::vector<int> sizes;
    for (int size : profile.sizes) {
        if (size < 1 || size > 256 || std::find(sizes.begin(), sizes.end(), size) != sizes.end()) {
            throw std::runtime_error("Invalid validation sizes.");
        }
        sizes.push_back(size);
    }
    IconValidation result;
    result.kind = source.kind;
    result.profile = profile;
    std::map<std::tuple<int, int, unsigned>, std::vector<int>> groups;
    for (std::size_t index = 0; index < source.frames.size(); ++index) {
        check_cancel(cancel);
        const IconFrame& frame = source.frames[index];
        IconValidationFrame item;
        item.index = int(index);
        item.width = frame.image.width;
        item.height = frame.image.height;
        try {
            if (item.width < 1 || item.height < 1 || item.width > 256 || item.height > 256 ||
                frame.image.pixels.size() != std::size_t(item.width) * item.height) {
                throw std::runtime_error("Invalid icon pixel dimensions or data.");
            }
            item.current_alpha = alpha_counts(frame.image);
            item.saved = icon_output_format(frame, source.kind);
            item.has_imported = bool(frame.source);
            if (frame.source) {
                item.imported = (*frame.source).format;
                item.source_alpha = alpha_counts((*frame.source).image);
            }
            for (std::size_t pixel = 0; pixel < frame.image.pixels.size(); ++pixel) {
                if (legacy_xor_pixel(frame, pixel)) {
                    ++item.xor_pixels;
                }
            }
            const std::vector<std::uint8_t> bytes = encode_icon_container({source.kind, {frame}});
            check_cancel(cancel);
            const ImageContainer decoded = decode_icon_container(bytes.data(), bytes.size());
            const Image& output = decoded.frames[0].image;
            if (output.width != item.width || output.height != item.height) {
                throw std::runtime_error("Encoded image dimensions differ from the current image.");
            }
            item.saved = (*decoded.frames[0].source).format;
            item.saved_alpha = alpha_counts(output);
            for (std::size_t pixel = 0; pixel < output.pixels.size(); ++pixel) {
                if (output.pixels[pixel].a != frame.image.pixels[pixel].a) {
                    ++item.changed_alpha;
                }
            }
            item.valid = true;
            groups[{item.width, item.height, item.saved.bits_per_pixel}].push_back(int(index));
        } catch (const std::exception& error) {
            check_cancel(cancel);
            item.error = error.what();
        }
        result.frames.push_back(std::move(item));
    }
    for (int size : sizes) {
        bool present = false;
        for (const IconValidationFrame& item : result.frames) {
            if (item.valid && item.width == size && item.height == size &&
                (profile.depth == 0 || item.saved.bits_per_pixel == profile.depth)) {
                present = true;
                break;
            }
        }
        if (!present) {
            result.missing.push_back(size);
        }
    }
    for (const std::pair<const std::tuple<int, int, unsigned>, std::vector<int>>& group : groups) {
        if (group.second.size() > 1) {
            result.duplicates.push_back(group.second);
        }
    }
    check_cancel(cancel);
    return result;
}
std::string icon_validation_json(const IconValidation& validation) {
    std::ostringstream output;
    output << "{\n  \"schemaVersion\":1,\n  \"kind\":"
           << quoted(validation.kind == ContainerKind::Cursor ? "cur" : "ico")
           << ",\n  \"alphaBuckets\":[\"zero\",\"partial\",\"opaque\"],\n  \"requiredDepth\":"
           << validation.profile.depth << ",\n  \"requiredSizes\":[";
    for (std::size_t i = 0; i < validation.profile.sizes.size(); ++i) {
        output << (i ? "," : "") << validation.profile.sizes[i];
    }
    output << "],\n  \"missingSizes\":[";
    for (std::size_t i = 0; i < validation.missing.size(); ++i) {
        output << (i ? "," : "") << validation.missing[i];
    }
    output << "],\n  \"duplicateOrdinals\":[";
    for (std::size_t i = 0; i < validation.duplicates.size(); ++i) {
        output << (i ? ",[" : "[");
        for (std::size_t j = 0; j < validation.duplicates[i].size(); ++j) {
            output << (j ? "," : "") << validation.duplicates[i][j] + 1;
        }
        output << ']';
    }
    output << "],\n  \"frames\":[";
    for (std::size_t i = 0; i < validation.frames.size(); ++i) {
        const IconValidationFrame& frame = validation.frames[i];
        output << (i ? ",\n" : "\n") << "    {\"ordinal\":" << frame.index + 1 << ",\"width\":" << frame.width
               << ",\"height\":" << frame.height << ",\"valid\":" << (frame.valid ? "true" : "false")
               << ",\"sourceFormat\":" << (frame.has_imported ? format_json(frame.imported) : "null")
               << ",\"outputFormat\":" << format_json(frame.saved)
               << ",\"sourceAlpha\":" << (frame.has_imported ? alpha_json(frame.source_alpha) : "null")
               << ",\"currentAlpha\":" << alpha_json(frame.current_alpha)
               << ",\"savedAlpha\":" << alpha_json(frame.saved_alpha)
               << ",\"alphaChangesOnSave\":" << frame.changed_alpha
               << ",\"legacyXorPixels\":" << frame.xor_pixels << ",\"error\":" << quoted(frame.error) << '}';
    }
    output << "\n  ]\n}\n";
    return output.str();
}
} // namespace paint
