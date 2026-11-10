#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace paint {
// The image kinds Plan Paint can name, sniff, open or save. Every decision
// that used to compare extension strings goes through this one enumeration,
// so a .PNG, .Png and .png name the same type and a file whose bytes are a
// known image opens by content whatever its name.
enum class ImageFileType : std::uint8_t {
    unknown,
    png,
    jpeg,
    bmp,
    gif,
    webp,
    avif,
    heif,
    tiff,
    tga,
    ico,
    cur,
    svg,
    // Gzip-compressed SVG: recognised so it gets its own message, not opened.
    svgz
};

// The type a filename's extension names, compared without regard to ASCII
// case. Unknown or missing extensions give ImageFileType::unknown.
ImageFileType image_type_from_extension(const std::string& path);

// The type the leading bytes identify (magic numbers; SVG by its root
// element within the first 4096 bytes). TGA has no signature and is never
// returned. Reads at most the first 4096 bytes of a buffer of `size` bytes.
ImageFileType sniff_image_type(const std::uint8_t* bytes, std::size_t size);

// The type to decode: the sniffed content wins over the name, so a PNG named
// .jpg or .dat opens as PNG. Only TGA (no signature) and SVG (whose root may
// lie past the sniff window) are taken from the name alone. Unknown means the
// file cannot be opened.
ImageFileType resolve_image_type(const std::string& path, const std::uint8_t* bytes, std::size_t size);

// Whether save_image or save_container can write the type the path names.
bool writable_image_type(ImageFileType type);

// The message for a file that resolve_image_type could not identify: names the
// file, says what the name claimed if anything, and lists what Plan Paint opens.
std::string unsupported_image_message(const std::string& path);
} // namespace paint
