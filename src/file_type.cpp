#include "file_type.hpp"
#include "paths.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iterator>

namespace paint {
namespace {
// Only this many leading bytes are examined: enough for every signature and
// for an SVG root element after an XML declaration, comments or a doctype.
const std::size_t sniff_window = 4096;

struct ExtensionName {
    const char* extension;
    ImageFileType type;
};
const ExtensionName extension_names[] = {
    {".png", ImageFileType::png},   {".jpg", ImageFileType::jpeg},  {".jpeg", ImageFileType::jpeg},
    {".jpe", ImageFileType::jpeg},  {".jfif", ImageFileType::jpeg}, {".bmp", ImageFileType::bmp},
    {".dib", ImageFileType::bmp},   {".gif", ImageFileType::gif},   {".webp", ImageFileType::webp},
    {".avif", ImageFileType::avif}, {".heic", ImageFileType::heif}, {".heif", ImageFileType::heif},
    {".hif", ImageFileType::heif},  {".tif", ImageFileType::tiff},  {".tiff", ImageFileType::tiff},
    {".tga", ImageFileType::tga},   {".ico", ImageFileType::ico},   {".cur", ImageFileType::cur},
    {".svg", ImageFileType::svg},   {".svgz", ImageFileType::svgz}};

std::string folded_extension(const std::string& path) {
    const std::filesystem::path name = path_from_utf8(path);
    std::string result = path_to_utf8(name.extension());
    for (std::size_t index = 0; index < result.size(); ++index) {
        const char character = result[index];
        if (character >= 'A' && character <= 'Z') {
            result[index] = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}
std::uint32_t big32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}
bool starts_with(const std::uint8_t* bytes, std::size_t size, const char* signature, std::size_t length) {
    return size >= length && std::memcmp(bytes, signature, length) == 0;
}
bool brand_in(const std::uint8_t* brand, const char (*names)[5], std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (std::memcmp(brand, names[index], 4) == 0) {
            return true;
        }
    }
    return false;
}
// ISO base media (ftyp) files: AVIF when any brand is avif/avis, HEIF when a
// brand names HEVC. mif1/msf1 alone say only "some HEIF-family image" and are
// left unknown rather than guessed. AVIF is checked first because AVIF files
// also list mif1.
ImageFileType sniff_iso_media(const std::uint8_t* bytes, std::size_t size) {
    if (size < 16 || std::memcmp(bytes + 4, "ftyp", 4) != 0) {
        return ImageFileType::unknown;
    }
    const std::size_t box_size = std::min<std::size_t>(big32(bytes), std::min(size, sniff_window));
    if (box_size < 16) {
        return ImageFileType::unknown;
    }
    static const char avif_brands[][5] = {"avif", "avis"};
    static const char heif_brands[][5] = {"heic", "heix", "hevc", "hevx", "heim", "heis", "hevm", "hevs"};
    bool heif = brand_in(bytes + 8, heif_brands, std::size(heif_brands));
    if (brand_in(bytes + 8, avif_brands, std::size(avif_brands))) {
        return ImageFileType::avif;
    }
    // Compatible brands follow the major brand (8..12) and minor version (12..16).
    for (std::size_t offset = 16; offset + 4 <= box_size; offset += 4) {
        if (brand_in(bytes + offset, avif_brands, std::size(avif_brands))) {
            return ImageFileType::avif;
        }
        heif = heif || brand_in(bytes + offset, heif_brands, std::size(heif_brands));
    }
    return heif ? ImageFileType::heif : ImageFileType::unknown;
}
// Text whose first markup, after an optional UTF-8 byte-order mark and
// whitespace, leads to an <svg element inside the sniff window.
bool svg_text(const std::uint8_t* bytes, std::size_t size) {
    const std::size_t limit = std::min(size, sniff_window);
    std::size_t offset = 0;
    if (limit >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        offset = 3;
    }
    while (offset < limit && (bytes[offset] == ' ' || bytes[offset] == '\t' || bytes[offset] == '\r' ||
                              bytes[offset] == '\n')) {
        ++offset;
    }
    if (offset >= limit || bytes[offset] != '<') {
        return false;
    }
    const char root[] = "<svg";
    const std::uint8_t* first = bytes + offset;
    const std::uint8_t* last = bytes + limit;
    const std::uint8_t* found = std::search(first, last, root, root + 4);
    return found != last;
}
const char* type_name(ImageFileType type) {
    switch (type) {
    case ImageFileType::png: return "PNG";
    case ImageFileType::jpeg: return "JPEG";
    case ImageFileType::bmp: return "BMP";
    case ImageFileType::gif: return "GIF";
    case ImageFileType::webp: return "WebP";
    case ImageFileType::avif: return "AVIF";
    case ImageFileType::heif: return "HEIF";
    case ImageFileType::tiff: return "TIFF";
    case ImageFileType::tga: return "TGA";
    case ImageFileType::ico: return "ICO";
    case ImageFileType::cur: return "CUR";
    case ImageFileType::svg: return "SVG";
    case ImageFileType::svgz: return "SVGZ";
    case ImageFileType::unknown: return "";
    }
    return "";
}
} // namespace

ImageFileType image_type_from_extension(const std::string& path) {
    const std::string extension = folded_extension(path);
    for (std::size_t index = 0; index < std::size(extension_names); ++index) {
        if (extension == extension_names[index].extension) {
            return extension_names[index].type;
        }
    }
    return ImageFileType::unknown;
}
ImageFileType sniff_image_type(const std::uint8_t* bytes, std::size_t size) {
    if (bytes == nullptr || size == 0) {
        return ImageFileType::unknown;
    }
    const char png[] = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1A', '\n'};
    if (starts_with(bytes, size, png, 8)) {
        return ImageFileType::png;
    }
    if (size >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        return ImageFileType::jpeg;
    }
    if (starts_with(bytes, size, "GIF87a", 6) || starts_with(bytes, size, "GIF89a", 6)) {
        return ImageFileType::gif;
    }
    if (size >= 12 && std::memcmp(bytes, "RIFF", 4) == 0 && std::memcmp(bytes + 8, "WEBP", 4) == 0) {
        return ImageFileType::webp;
    }
    if (starts_with(bytes, size, "II*\0", 4) || starts_with(bytes, size, "MM\0*", 4)) {
        return ImageFileType::tiff;
    }
    // ICONDIR: reserved 0, type 1 (icon) or 2 (cursor), at least one image.
    if (size >= 6 && bytes[0] == 0 && bytes[1] == 0 && (bytes[2] == 1 || bytes[2] == 2) && bytes[3] == 0 &&
        (bytes[4] != 0 || bytes[5] != 0)) {
        return bytes[2] == 1 ? ImageFileType::ico : ImageFileType::cur;
    }
    // "BM" followed by a 14-byte file header and at least a core info header.
    if (size >= 26 && bytes[0] == 'B' && bytes[1] == 'M') {
        return ImageFileType::bmp;
    }
    const ImageFileType iso = sniff_iso_media(bytes, size);
    if (iso != ImageFileType::unknown) {
        return iso;
    }
    if (size >= 2 && bytes[0] == 0x1F && bytes[1] == 0x8B) {
        return ImageFileType::svgz;
    }
    if (svg_text(bytes, size)) {
        return ImageFileType::svg;
    }
    return ImageFileType::unknown;
}
ImageFileType resolve_image_type(const std::string& path, const std::uint8_t* bytes, std::size_t size) {
    const ImageFileType content = sniff_image_type(bytes, size);
    const ImageFileType named = image_type_from_extension(path);
    if (content == ImageFileType::svgz) {
        // Gzip is a general container: only a file named as SVG is taken for SVGZ.
        return named == ImageFileType::svg || named == ImageFileType::svgz ? ImageFileType::svgz
                                                                          : ImageFileType::unknown;
    }
    if (content != ImageFileType::unknown) {
        return content;
    }
    // TGA has no signature; an SVG may carry more than the sniff window of
    // comments before its root. Both decoders validate what they are given.
    if (named == ImageFileType::tga || named == ImageFileType::svg) {
        return named;
    }
    return ImageFileType::unknown;
}
bool writable_image_type(ImageFileType type) {
    return type == ImageFileType::png || type == ImageFileType::jpeg || type == ImageFileType::bmp ||
           type == ImageFileType::tiff || type == ImageFileType::tga || type == ImageFileType::ico ||
           type == ImageFileType::cur;
}
std::string unsupported_image_message(const std::string& path) {
    const std::string name = path_to_utf8(path_from_utf8(path).filename());
    const ImageFileType named = image_type_from_extension(path);
    std::string message = "Plan Paint cannot open \"" + name + "\": ";
    if (named != ImageFileType::unknown) {
        message += std::string("its name says ") + type_name(named) +
                   ", but its contents are not a recognised image. It may be damaged or another kind of file. ";
    } else {
        message += "its contents are not a recognised image. ";
    }
    message += "Plan Paint opens PNG, JPEG, BMP, TIFF, TGA, WebP, AVIF, HEIF (macOS), SVG, ICO and CUR images.";
    return message;
}
} // namespace paint
