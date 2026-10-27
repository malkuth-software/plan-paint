#include "asset_browser.hpp"
#include "codecs.hpp"
#include "paths.hpp"
#include "safe_file.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>
namespace paint {
namespace {
std::string folded(std::string text) {
    for (char& character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return text;
}
bool classify(const std::filesystem::directory_entry& source, AssetEntry& entry) {
    std::error_code error;
    if (source.is_directory(error)) {
        entry.kind = AssetKind::Folder;
        return true;
    }
    if (!source.is_regular_file(error) || error) {
        return false;
    }
    const std::string extension = folded(path_to_utf8(source.path().extension()));
    if (extension == ".ico") {
        entry.kind = AssetKind::Icon;
    } else if (extension == ".cur") {
        entry.kind = AssetKind::Cursor;
    } else if (extension == ".svg" || extension == ".svgz") {
        entry.kind = AssetKind::Vector;
    } else {
        const std::string supported = ".png.jpg.jpeg.bmp.webp.avif.tiff.tif.tga.heic.heif.hif.";
        if (extension.empty() || supported.find(extension + ".") == std::string::npos) {
            return false;
        }
        entry.kind = AssetKind::Bitmap;
    }
    return true;
}
bool matches(AssetKind kind, AssetFilter filter) {
    return kind == AssetKind::Folder || filter == AssetFilter::All ||
           (filter == AssetFilter::Icons && kind == AssetKind::Icon) ||
           (filter == AssetFilter::Cursors && kind == AssetKind::Cursor) ||
           (filter == AssetFilter::Bitmaps && kind == AssetKind::Bitmap) ||
           (filter == AssetFilter::Vectors && kind == AssetKind::Vector);
}
bool entry_before(const AssetEntry& left, const AssetEntry& right) {
    if ((left.kind == AssetKind::Folder) != (right.kind == AssetKind::Folder)) {
        return left.kind == AssetKind::Folder;
    }
    const std::string left_name = folded(left.name), right_name = folded(right.name);
    if (left_name != right_name) {
        return left_name < right_name;
    }
    return left.name < right.name;
}
bool same(const AssetFingerprint& left, const AssetFingerprint& right) {
    return left.bytes == right.bytes && left.modified == right.modified;
}
int hex_digit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}
constexpr std::string_view bookmark_header = "Plan Paint asset folders v1\n";
} // namespace
AssetListing scan_assets(const std::string& directory, const std::string& query, AssetFilter filter,
                         std::size_t limit, const std::atomic<bool>* cancel) {
    if (limit < 1 || limit > 8192 || query.size() > 4096) {
        throw std::runtime_error("Asset browser query exceeds its limits.");
    }
    AssetListing result;
    result.directory = path_to_utf8(std::filesystem::canonical(path_from_utf8(directory)));
    const std::string needle = folded(query);
    std::size_t scanned = 0;
    for (const std::filesystem::directory_entry& source :
         std::filesystem::directory_iterator(path_from_utf8(result.directory))) {
        if (cancel && (*cancel).load(std::memory_order_relaxed)) {
            result.entries.clear();
            result.cancelled = true;
            return result;
        }
        if (++scanned > 50000) {
            result.truncated = true;
            break;
        }
        AssetEntry entry;
        entry.name = path_to_utf8(source.path().filename());
        if (folded(entry.name).find(needle) == std::string::npos || !classify(source, entry) ||
            !matches(entry.kind, filter)) {
            continue;
        }
        if (result.entries.size() == limit) {
            result.truncated = true;
            break;
        }
        entry.path = path_to_utf8(source.path());
        result.entries.push_back(std::move(entry));
    }
    if (cancel && (*cancel).load(std::memory_order_relaxed)) {
        result.entries.clear();
        result.cancelled = true;
        return result;
    }
    std::sort(result.entries.begin(), result.entries.end(), entry_before);
    return result;
}
AssetFingerprint asset_fingerprint(const std::string& path) {
    const std::filesystem::path file = path_from_utf8(path);
    if (!std::filesystem::is_regular_file(file)) {
        throw std::runtime_error("Asset is not a regular file.");
    }
    return {std::filesystem::file_size(file), std::filesystem::last_write_time(file)};
}
bool asset_unchanged(const AssetPreview& preview) {
    try {
        return same(preview.fingerprint, asset_fingerprint(preview.path));
    } catch (const std::exception&) {
        return false;
    }
}
AssetPreview load_asset_preview(const std::string& path) {
    AssetPreview result;
    result.path = path;
    result.fingerprint = asset_fingerprint(path);
    if (result.fingerprint.bytes > 32000000) {
        throw std::runtime_error("Asset preview is limited to files of 32 MB. Open larger images directly.");
    }
    result.images = load_container(path);
    if (!asset_unchanged(result)) {
        throw std::runtime_error("Asset changed while loading. Refresh its preview.");
    }
    if (result.images.frames.empty()) {
        throw std::runtime_error("Asset has no images.");
    }
    std::uint64_t largest = 0;
    for (std::size_t index = 0; index < result.images.frames.size(); ++index) {
        const Image& image = result.images.frames[index].image;
        const std::uint64_t area = std::uint64_t(image.width) * image.height;
        if (area > largest) {
            largest = area;
            result.preferred = index;
        }
    }
    return result;
}
Image render_asset_preview(const IconFrame& frame, int width, int height) {
    const Image& source = frame.image;
    if (width < 1 || height < 1 || width > 1024 || height > 1024 || source.width < 1 || source.height < 1 ||
        source.pixels.size() != std::size_t(source.width) * source.height) {
        throw std::runtime_error("Invalid asset preview dimensions.");
    }
    Image result;
    result.reset(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            result.set(x, y, ((x / 8 + y / 8) % 2) ? Color{205, 211, 219, 255} : Color{250, 250, 250, 255});
        }
    }
    const double scale = std::min({1.0, double(width) / source.width, double(height) / source.height});
    const int fitted_width = std::max(1, static_cast<int>(std::floor(source.width * scale)));
    const int fitted_height = std::max(1, static_cast<int>(std::floor(source.height * scale)));
    const int left = (width - fitted_width) / 2, top = (height - fitted_height) / 2;
    for (int y = 0; y < fitted_height; ++y) {
        for (int x = 0; x < fitted_width; ++x) {
            const int sx =
                std::min(source.width - 1, static_cast<int>((x + .5) * source.width / fitted_width));
            const int sy =
                std::min(source.height - 1, static_cast<int>((y + .5) * source.height / fitted_height));
            const std::size_t index = std::size_t(sy) * source.width + sx;
            if (legacy_xor_pixel(frame, index)) {
                const Color back = result.get(left + x, top + y), mask = frame.xor_pixels[index];
                result.set(left + x, top + y,
                           {static_cast<std::uint8_t>(back.r ^ mask.r),
                            static_cast<std::uint8_t>(back.g ^ mask.g),
                            static_cast<std::uint8_t>(back.b ^ mask.b), 255});
            } else {
                result.blend(left + x, top + y, source.pixels[index]);
            }
        }
    }
    return result;
}
Image asset_stamp(const AssetPreview& preview, std::size_t representation) {
    if (representation >= preview.images.frames.size()) {
        throw std::runtime_error("A selected representation is unavailable.");
    }
    const IconFrame& frame = preview.images.frames[representation];
    for (std::size_t index = 0; index < frame.image.pixels.size(); ++index) {
        if (legacy_xor_pixel(frame, index)) {
            throw std::runtime_error("Legacy XOR images cannot be used as background-independent stamps.");
        }
    }
    return frame.image;
}
std::vector<std::string> load_asset_bookmarks(const std::string& path) {
    if (!std::filesystem::exists(path_from_utf8(path))) {
        return {};
    }
    const std::vector<std::uint8_t> bytes =
        read_regular_file_bounded(path, 262144, "Cannot read asset bookmarks.");
    const std::string text(bytes.begin(), bytes.end());
    if (!text.starts_with(bookmark_header)) {
        throw std::runtime_error("Invalid asset bookmark file.");
    }
    std::vector<std::string> result;
    std::string folder;
    for (std::size_t offset = bookmark_header.size(); offset < text.size(); ++offset) {
        char character = text[offset];
        if (character == '\n') {
            if (folder.empty() || result.size() == 64 || !path_from_utf8(folder).is_absolute()) {
                throw std::runtime_error("Invalid asset bookmark file.");
            }
            if (std::find(result.begin(), result.end(), folder) == result.end()) {
                result.push_back(folder);
            }
            folder.clear();
        } else {
            if (character == '%') {
                if (offset + 2 >= text.size() || hex_digit(text[offset + 1]) < 0 ||
                    hex_digit(text[offset + 2]) < 0) {
                    throw std::runtime_error("Invalid asset bookmark file.");
                }
                character = static_cast<char>(hex_digit(text[offset + 1]) * 16 + hex_digit(text[offset + 2]));
                offset += 2;
            }
            if (character == '\0') {
                throw std::runtime_error("Invalid asset bookmark file.");
            }
            folder += character;
        }
    }
    if (!folder.empty()) {
        throw std::runtime_error("Invalid asset bookmark file.");
    }
    return result;
}
void save_asset_bookmarks(const std::vector<std::string>& folders, const std::string& path) {
    if (folders.size() > 64) {
        throw std::runtime_error("At most 64 asset folders can be bookmarked.");
    }
    std::string text(bookmark_header);
    constexpr std::string_view digits = "0123456789ABCDEF";
    for (const std::string& folder : folders) {
        if (folder.empty() || !path_from_utf8(folder).is_absolute()) {
            throw std::runtime_error("Asset bookmarks require absolute folder paths.");
        }
        for (unsigned char character : folder) {
            if (!character) {
                throw std::runtime_error("Invalid asset bookmark file.");
            }
            if (character == '%' || character < 32 || character == 127) {
                text += '%';
                text += digits[character >> 4];
                text += digits[character & 15];
            } else {
                text += static_cast<char>(character);
            }
        }
        text += '\n';
    }
    if (text.size() > 262144) {
        throw std::runtime_error("Asset bookmarks exceed their storage limit.");
    }
    write_file_atomic(std::vector<std::uint8_t>(text.begin(), text.end()), path,
                      "Cannot save asset bookmarks.");
}
} // namespace paint
