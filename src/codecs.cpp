#include "codecs.hpp"
#include "import_codecs.hpp"
#include "safe_file.hpp"
#define STBI_WINDOWS_UTF8
#define STBIW_WINDOWS_UTF8
#define STBI_MAX_DIMENSIONS 16384
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include "stb_image.h"
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tiffio.h>
#include <webp/decode.h>

namespace paint {
namespace {
const std::int64_t maximum_decoded_pixels = 64000000;
void validate_decoded_size(int width, int height) {
    if (width < 1 || height < 1 || width > 16384 || height > 16384 ||
        static_cast<std::int64_t>(width) * height > maximum_decoded_pixels) {
        throw std::runtime_error("Image dimensions are invalid or exceed the 64-megapixel import limit.");
    }
}
bool gif_signature(const unsigned char* bytes, std::size_t size) {
    return size >= 6 && (std::memcmp(bytes, "GIF87a", 6) == 0 || std::memcmp(bytes, "GIF89a", 6) == 0);
}
std::uint16_t little16(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(bytes[0] | (static_cast<unsigned>(bytes[1]) << 8));
}
std::uint32_t little32(const unsigned char* bytes) {
    return bytes[0] | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}
void validate_bmp_profile(const unsigned char* bytes, std::size_t size) {
    if (size < 30 || bytes[0] != 'B' || bytes[1] != 'M') {
        return;
    }
    const std::uint32_t header_size = little32(bytes + 14);
    const std::size_t bits_offset = header_size == 12 ? 24 : 28;
    if ((header_size != 12 && header_size < 40) || bits_offset + 2 > size) {
        throw std::runtime_error("BMP has an invalid header.");
    }
    const std::uint16_t bits = little16(bytes + bits_offset);
    if (bits != 24 && bits != 32) {
        throw std::runtime_error(
            "For safe import, BMP must use direct 24-bit or 32-bit pixels. Convert paletted BMP files to "
            "PNG first.");
    }
}
struct TiffMemory {
    std::vector<std::uint8_t>* bytes;
    std::size_t offset;
    bool writable;
};
const std::size_t maximum_encoded_bytes = 512000000;
tmsize_t tiff_read(thandle_t handle, void* destination, tmsize_t requested) {
    TiffMemory& memory = *static_cast<TiffMemory*>(handle);
    if (requested < 0 || memory.offset > (*memory.bytes).size()) {
        return 0;
    }
    const std::size_t available = (*memory.bytes).size() - memory.offset;
    const std::size_t count = std::min(static_cast<std::size_t>(requested), available);
    if (count > 0) {
        std::memcpy(destination, (*memory.bytes).data() + memory.offset, count);
        memory.offset += count;
    }
    return static_cast<tmsize_t>(count);
}
tmsize_t tiff_write(thandle_t handle, void* source, tmsize_t requested) {
    TiffMemory& memory = *static_cast<TiffMemory*>(handle);
    if (!memory.writable || requested < 0) {
        return 0;
    }
    const std::size_t count = static_cast<std::size_t>(requested);
    if (memory.offset > maximum_encoded_bytes || count > maximum_encoded_bytes - memory.offset) {
        return 0;
    }
    if (memory.offset + count > (*memory.bytes).size()) {
        (*memory.bytes).resize(memory.offset + count);
    }
    if (count > 0) {
        std::memcpy((*memory.bytes).data() + memory.offset, source, count);
        memory.offset += count;
    }
    return requested;
}
toff_t tiff_seek(thandle_t handle, toff_t offset, int origin) {
    TiffMemory& memory = *static_cast<TiffMemory*>(handle);
    std::uint64_t base = 0;
    if (origin == SEEK_CUR) {
        base = memory.offset;
    } else if (origin == SEEK_END) {
        base = (*memory.bytes).size();
    } else if (origin != SEEK_SET) {
        return static_cast<toff_t>(-1);
    }
    if (offset > maximum_encoded_bytes || base > maximum_encoded_bytes - offset) {
        return static_cast<toff_t>(-1);
    }
    const std::size_t position = static_cast<std::size_t>(base + offset);
    if (position > (*memory.bytes).size()) {
        if (!memory.writable) {
            return static_cast<toff_t>(-1);
        }
        (*memory.bytes).resize(position);
    }
    memory.offset = position;
    return static_cast<toff_t>(position);
}
int tiff_close(thandle_t) {
    return 0;
}
toff_t tiff_size(thandle_t handle) {
    TiffMemory& memory = *static_cast<TiffMemory*>(handle);
    return static_cast<toff_t>((*memory.bytes).size());
}
int tiff_map(thandle_t, void**, toff_t*) {
    return 0;
}
void tiff_unmap(thandle_t, void*, toff_t) {}
TIFF* open_tiff_memory(TiffMemory& memory, const char* mode) {
    return TIFFClientOpen("Plan Paint memory image", mode, &memory, tiff_read, tiff_write, tiff_seek,
                          tiff_close, tiff_size, tiff_map, tiff_unmap);
}
} // namespace
std::string image_extension(const std::string& path) {
    std::string result = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
    for (char& value : result) {
        value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    }
    return result;
}
Image decode_image(const void* data, std::size_t size) {
    if (size > 256000000) {
        throw std::runtime_error("Encoded file exceeds 256 MB.");
    }
    int width = 0, height = 0, channels = 0;
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    if (gif_signature(bytes, size)) {
        throw std::runtime_error(
            "GIF import is disabled because the available decoder is not safe for untrusted files. "
            "Convert this picture to PNG first.");
    }
    reject_animation(bytes, size);
    validate_bmp_profile(bytes, size);
    if (is_avif(data, size)) {
        return decode_avif(data, size);
    }
    if (size >= 6 && bytes[0] == 0 && bytes[1] == 0 && (bytes[2] == 1 || bytes[2] == 2) && bytes[3] == 0) {
        ImageContainer icons = decode_icon_container(data, size);
        int best = 0;
        for (std::size_t i = 1; i < icons.frames.size(); ++i) {
            if (icons.frames[i].image.width * icons.frames[i].image.height >
                icons.frames[best].image.width * icons.frames[best].image.height) {
                best = static_cast<int>(i);
            }
        }
        return icons.frames[best].image;
    }
    if (!stbi_info_from_memory(bytes, static_cast<int>(size), &width, &height, &channels)) {
        if (!WebPGetInfo(bytes, size, &width, &height)) {
            throw std::runtime_error("This image format could not be decoded.");
        }
        validate_decoded_size(width, height);
        WebPBitstreamFeatures features{};
        if (WebPGetFeatures(bytes, size, &features) != VP8_STATUS_OK || features.has_animation) {
            throw std::runtime_error(features.has_animation ? "Animated WebP files are not supported."
                                                            : "WebP decoding failed.");
        }
        Image result;
        result.reset(width, height);
        if (!WebPDecodeRGBAInto(bytes, size, reinterpret_cast<std::uint8_t*>(result.pixels.data()),
                                result.pixels.size() * 4, width * 4)) {
            throw std::runtime_error("WebP decoding failed.");
        }
        return result;
    }
    validate_decoded_size(width, height);
    const int expected_width = width, expected_height = height;
    Image result;
    result.reset(width, height);
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4),
        &stbi_image_free);
    if (!pixels) {
        throw std::runtime_error(stbi_failure_reason());
    }
    if (width != expected_width || height != expected_height) {
        throw std::runtime_error("Image dimensions changed during decoding.");
    }
    std::memcpy(result.pixels.data(), pixels.get(), result.pixels.size() * sizeof(Color));
    return result;
}
Image load_image(const std::string& path) {
    std::string ext = image_extension(path);
    if (ext == ".tif" || ext == ".tiff") {
        std::vector<std::uint8_t> bytes = read_image_bytes(path);
        TiffMemory memory{&bytes, 0, false};
        TIFF* file = open_tiff_memory(memory, "rm");
        if (!file) {
            throw std::runtime_error("Could not open TIFF.");
        }
        std::unique_ptr<TIFF, decltype(&TIFFClose)> close_file(file, &TIFFClose);
        std::uint32_t width = 0, height = 0;
        std::uint16_t bits = 0, samples = 0, photometric = 0, planar = 0, compression = 0, orientation = 0;
        if (!TIFFGetField(file, TIFFTAG_IMAGEWIDTH, &width) ||
            !TIFFGetField(file, TIFFTAG_IMAGELENGTH, &height)) {
            throw std::runtime_error("TIFF is missing its image dimensions.");
        }
        validate_decoded_size(static_cast<int>(width), static_cast<int>(height));
        TIFFGetFieldDefaulted(file, TIFFTAG_BITSPERSAMPLE, &bits);
        TIFFGetFieldDefaulted(file, TIFFTAG_SAMPLESPERPIXEL, &samples);
        TIFFGetFieldDefaulted(file, TIFFTAG_PHOTOMETRIC, &photometric);
        TIFFGetFieldDefaulted(file, TIFFTAG_PLANARCONFIG, &planar);
        TIFFGetFieldDefaulted(file, TIFFTAG_COMPRESSION, &compression);
        TIFFGetFieldDefaulted(file, TIFFTAG_ORIENTATION, &orientation);
        if (bits != 8 || (samples != 3 && samples != 4) || photometric != PHOTOMETRIC_RGB ||
            planar != PLANARCONFIG_CONTIG || compression != COMPRESSION_NONE || TIFFIsTiled(file) ||
            (orientation != ORIENTATION_TOPLEFT && orientation != ORIENTATION_BOTLEFT) ||
            !TIFFLastDirectory(file)) {
            throw std::runtime_error(
                "For safe import, TIFF must be one uncompressed 8-bit RGB or RGBA image. Convert other "
                "TIFF variants to PNG first.");
        }
        std::uint16_t extra_count = 0;
        std::uint16_t* extra_types = nullptr;
        if (samples == 4 &&
            (!TIFFGetField(file, TIFFTAG_EXTRASAMPLES, &extra_count, &extra_types) || extra_count != 1 ||
             (extra_types[0] != EXTRASAMPLE_UNASSALPHA && extra_types[0] != EXTRASAMPLE_ASSOCALPHA))) {
            throw std::runtime_error("TIFF RGBA input must identify its alpha channel.");
        }
        const tmsize_t scanline_size = TIFFScanlineSize(file);
        const std::size_t expected_scanline = static_cast<std::size_t>(width) * samples;
        if (scanline_size < 0 || static_cast<std::size_t>(scanline_size) != expected_scanline) {
            throw std::runtime_error("TIFF scanline geometry is inconsistent.");
        }
        Image result;
        result.reset(static_cast<int>(width), static_cast<int>(height));
        std::vector<std::uint8_t> scanline(expected_scanline);
        for (std::uint32_t source_y = 0; source_y < height; ++source_y) {
            if (TIFFReadScanline(file, scanline.data(), source_y, 0) < 0) {
                throw std::runtime_error("TIFF decoding failed.");
            }
            const std::uint32_t target_y =
                orientation == ORIENTATION_TOPLEFT ? source_y : height - source_y - 1;
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::size_t source = static_cast<std::size_t>(x) * samples;
                unsigned red = scanline[source], green = scanline[source + 1], blue = scanline[source + 2];
                const unsigned alpha = samples == 4 ? scanline[source + 3] : 255;
                if (samples == 4 && extra_types[0] == EXTRASAMPLE_ASSOCALPHA && alpha > 0 && alpha < 255) {
                    red = std::min(255U, (red * 255 + alpha / 2) / alpha);
                    green = std::min(255U, (green * 255 + alpha / 2) / alpha);
                    blue = std::min(255U, (blue * 255 + alpha / 2) / alpha);
                } else if (alpha == 0) {
                    red = green = blue = 0;
                }
                result.set(static_cast<int>(x), static_cast<int>(target_y),
                           {static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                            static_cast<std::uint8_t>(blue), static_cast<std::uint8_t>(alpha)});
            }
        }
        return result;
    }
    if (ext == ".svg" || ext == ".svgz") {
        return rasterize_svg(path);
    }
    std::vector<std::uint8_t> bytes = read_image_bytes(path);
    if (ext == ".heic" || ext == ".heif" || ext == ".hif") {
        return decode_native_heif(bytes.data(), bytes.size());
    }
    return decode_image(bytes.data(), bytes.size());
}
std::vector<std::uint8_t> read_image_bytes(const std::string& path, std::size_t maximum) {
    return read_regular_file_bounded(
        path, maximum,
        "Could not read this image as a bounded regular file, or it exceeds its import size limit.");
}
bool writable_image_path(const std::string& path) {
    std::string ext = image_extension(path);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tif" ||
           ext == ".tiff" || ext == ".tga" || ext == ".ico" || ext == ".cur";
}
ImageContainer load_container(const std::string& path) {
    std::string ext = image_extension(path);
    if (ext == ".ico" || ext == ".cur") {
        std::vector<std::uint8_t> bytes = read_image_bytes(path);
        return decode_icon_container(bytes.data(), bytes.size());
    }
    ImageContainer result;
    if (ext == ".svg" || ext == ".svgz") {
        const std::shared_ptr<const SvgSource> source = load_svg_source(path);
        IconFrame frame;
        frame.image = (*source).original;
        frame.svg_source = source;
        result.frames.push_back(std::move(frame));
    } else {
        result.frames.push_back({load_image(path), 0, 0, {}});
    }
    return result;
}
void save_encoded_bytes(const std::vector<std::uint8_t>& bytes, const std::string& path) {
    if (bytes.empty() || bytes.size() > maximum_encoded_bytes) {
        throw std::runtime_error("Encoded image is empty or exceeds the 512 MB save limit.");
    }
    write_file_atomic(bytes, path, "Could not save the image without replacing the existing file.");
}
void save_container(const ImageContainer& container, const std::string& path) {
    std::string ext = image_extension(path);
    if (ext != ".ico" && ext != ".cur") {
        throw std::runtime_error("An icon collection must be saved as ICO or CUR.");
    }
    ImageContainer output = container;
    output.kind = ext == ".cur" ? ContainerKind::Cursor : ContainerKind::Icon;
    save_encoded_bytes(encode_icon_container(output), path);
}

static void append_encoded(void* context, void* data, int size) {
    std::vector<std::uint8_t>& bytes = *static_cast<std::vector<std::uint8_t>*>(context);
    if (size < 0 || static_cast<std::size_t>(size) > maximum_encoded_bytes - bytes.size()) {
        throw std::runtime_error("Encoded image exceeds the 512 MB save limit.");
    }
    std::uint8_t* first = static_cast<std::uint8_t*>(data);
    bytes.insert(bytes.end(), first, first + size);
}
std::vector<std::uint8_t> encode_bmp(const Image& source) {
    Image image;
    image.reset(source.width, source.height);
    composite(image, source, 0, 0);
    std::vector<std::uint8_t> bytes;
    int ok =
        stbi_write_bmp_to_func(append_encoded, &bytes, image.width, image.height, 4, image.pixels.data());
    if (!ok) {
        throw std::runtime_error("Bitmap encoding failed.");
    }
    return bytes;
}
std::vector<std::uint8_t> encode_png(const Image& image) {
    std::vector<std::uint8_t> bytes;
    int ok = stbi_write_png_to_func(append_encoded, &bytes, image.width, image.height, 4, image.pixels.data(),
                                    image.width * 4);
    if (!ok) {
        throw std::runtime_error("PNG encoding failed.");
    }
    return bytes;
}
namespace {
std::vector<std::uint8_t> encode_tiff(const Image& image) {
    std::vector<std::uint8_t> bytes;
    TiffMemory memory{&bytes, 0, true};
    TIFF* file = open_tiff_memory(memory, "wm");
    if (!file) {
        throw std::runtime_error("Could not create TIFF.");
    }
    std::unique_ptr<TIFF, decltype(&TIFFClose)> close_file(file, &TIFFClose);
    bool ok = TIFFSetField(file, TIFFTAG_IMAGEWIDTH, image.width) &&
              TIFFSetField(file, TIFFTAG_IMAGELENGTH, image.height) &&
              TIFFSetField(file, TIFFTAG_SAMPLESPERPIXEL, 4) &&
              TIFFSetField(file, TIFFTAG_BITSPERSAMPLE, 8) &&
              TIFFSetField(file, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) &&
              TIFFSetField(file, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) &&
              TIFFSetField(file, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB) &&
              TIFFSetField(file, TIFFTAG_COMPRESSION, COMPRESSION_NONE) &&
              TIFFSetField(file, TIFFTAG_ROWSPERSTRIP, 1);
    std::uint16_t extra = EXTRASAMPLE_UNASSALPHA;
    ok = ok && TIFFSetField(file, TIFFTAG_EXTRASAMPLES, 1, &extra);
    for (int row = 0; ok && row < image.height; ++row) {
        void* scanline =
            const_cast<Color*>(image.pixels.data() + static_cast<std::size_t>(row) * image.width);
        ok = TIFFWriteScanline(file, scanline, static_cast<std::uint32_t>(row), 0) >= 0;
    }
    ok = ok && TIFFWriteDirectory(file) != 0;
    if (!ok || bytes.empty()) {
        throw std::runtime_error("TIFF encoding failed.");
    }
    return bytes;
}
std::vector<std::uint8_t> encode_image(const Image& source, const std::string& ext, int quality) {
    Image flattened;
    const Image* input = &source;
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
        flattened.reset(source.width, source.height);
        composite(flattened, source, 0, 0);
        input = &flattened;
    }
    const Image& image = *input;
    const int width = image.width, height = image.height;
    const void* pixels = image.pixels.data();
    std::vector<std::uint8_t> bytes;
    int ok = 0;
    if (ext == ".png") {
        return encode_png(image);
    } else if (ext == ".jpg" || ext == ".jpeg") {
        ok = stbi_write_jpg_to_func(append_encoded, &bytes, width, height, 4, pixels, quality);
    } else if (ext == ".bmp") {
        return encode_bmp(image);
    } else if (ext == ".tga") {
        ok = stbi_write_tga_to_func(append_encoded, &bytes, width, height, 4, pixels);
    } else if (ext == ".tif" || ext == ".tiff") {
        return encode_tiff(image);
    } else {
        throw std::runtime_error("Choose PNG, JPEG, BMP, TIFF, TGA, ICO or CUR.");
    }
    if (!ok || bytes.empty()) {
        throw std::runtime_error("Image encoding failed.");
    }
    return bytes;
}
} // namespace
void save_image(const Image& image, const std::string& path, int quality) {
    if (image_extension(path) == ".ico" || image_extension(path) == ".cur") {
        ImageContainer container;
        container.kind = image_extension(path) == ".cur" ? ContainerKind::Cursor : ContainerKind::Icon;
        container.frames.push_back({image, 0, 0, {}});
        save_container(container, path);
        return;
    }
    const std::vector<std::uint8_t> bytes =
        encode_image(image, image_extension(path), std::clamp(quality, 1, 100));
    save_encoded_bytes(bytes, path);
}
} // namespace paint
