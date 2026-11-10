// Opening files whatever their extension's case, by content when the name
// does not say, and with a clear message when Plan Paint cannot open them.
#include "codecs.hpp"
#include "command_line.hpp"
#include "desktop.hpp"
#include "file_type.hpp"
#include "safe_file.hpp"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
std::string path_text(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}
void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    paint::write_file_atomic(bytes, path_text(path), "file-open test write failed");
}
// The message of the exception load_image throws, or empty when it opens.
std::string load_failure(const std::filesystem::path& path) {
    try {
        static_cast<void>(paint::load_image(path_text(path)));
    } catch (const std::exception& exception) {
        return exception.what();
    }
    return {};
}
bool contains(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

void extension_case() {
    const char* png_names[] = {"a.png", "a.PNG", "a.Png", "dir.v2/b.pNg", "C:\\Pictures\\Shot.PNG"};
    for (const char* name : png_names) {
        require(paint::image_type_from_extension(name) == paint::ImageFileType::png,
                "a PNG extension in any case names PNG");
    }
    require(paint::image_type_from_extension("x.JPEG") == paint::ImageFileType::jpeg, "JPEG upper case");
    require(paint::image_type_from_extension("x.Tif") == paint::ImageFileType::tiff, "TIFF mixed case");
    require(paint::image_type_from_extension("x.ICO") == paint::ImageFileType::ico, "ICO upper case");
    require(paint::image_type_from_extension("x.png.bak") == paint::ImageFileType::unknown,
            "only the last extension counts");
    require(paint::image_type_from_extension("noextension") == paint::ImageFileType::unknown,
            "no extension names nothing");
    require(paint::writable_image_path("X.PNG") && paint::writable_image_path("y.Jpeg") &&
                paint::writable_image_path("z.CUR"),
            "save formats are chosen without regard to case");
    require(!paint::writable_image_path("w.WEBP") && !paint::writable_image_path("v.svg"),
            "import-only formats stay unwritable in any case");
}

void signatures() {
    const std::uint8_t png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13};
    const std::uint8_t jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0};
    const std::uint8_t gif[] = {'G', 'I', 'F', '8', '9', 'a'};
    const std::uint8_t webp[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'};
    const std::uint8_t tiff_le[] = {'I', 'I', '*', 0};
    const std::uint8_t tiff_be[] = {'M', 'M', 0, '*'};
    const std::uint8_t ico[] = {0, 0, 1, 0, 1, 0};
    const std::uint8_t cur[] = {0, 0, 2, 0, 1, 0};
    const std::uint8_t avif[] = {0, 0, 0, 24, 'f', 't', 'y', 'p', 'a', 'v', 'i', 'f', 0, 0, 0, 0,
                                 'm', 'i', 'f', '1', 'm', 'i', 'a', 'f'};
    const std::uint8_t heif[] = {0, 0, 0, 24, 'f', 't', 'y', 'p', 'm', 'i', 'f', '1', 0, 0, 0, 0,
                                 'm', 'i', 'f', '1', 'h', 'e', 'i', 'c'};
    const std::uint8_t gzip[] = {0x1F, 0x8B, 8, 0};
    const std::string svg = "\xEF\xBB\xBF  <?xml version='1.0'?>\n<!-- art -->\n<svg width='4' height='4'/>";
    const std::string text = "Plain notes, not a picture.";
    require(paint::sniff_image_type(png, sizeof(png)) == paint::ImageFileType::png, "PNG signature");
    require(paint::sniff_image_type(jpeg, sizeof(jpeg)) == paint::ImageFileType::jpeg, "JPEG signature");
    require(paint::sniff_image_type(gif, sizeof(gif)) == paint::ImageFileType::gif, "GIF signature");
    require(paint::sniff_image_type(webp, sizeof(webp)) == paint::ImageFileType::webp, "WebP signature");
    require(paint::sniff_image_type(tiff_le, sizeof(tiff_le)) == paint::ImageFileType::tiff, "TIFF II");
    require(paint::sniff_image_type(tiff_be, sizeof(tiff_be)) == paint::ImageFileType::tiff, "TIFF MM");
    require(paint::sniff_image_type(ico, sizeof(ico)) == paint::ImageFileType::ico, "ICO directory");
    require(paint::sniff_image_type(cur, sizeof(cur)) == paint::ImageFileType::cur, "CUR directory");
    require(paint::sniff_image_type(avif, sizeof(avif)) == paint::ImageFileType::avif,
            "AVIF wins over its mif1 compatibility brand");
    require(paint::sniff_image_type(heif, sizeof(heif)) == paint::ImageFileType::heif, "HEIC brand");
    require(paint::sniff_image_type(gzip, sizeof(gzip)) == paint::ImageFileType::svgz, "gzip signature");
    require(paint::sniff_image_type(reinterpret_cast<const std::uint8_t*>(svg.data()), svg.size()) ==
                paint::ImageFileType::svg,
            "SVG after a byte-order mark, declaration and comment");
    require(paint::sniff_image_type(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()) ==
                paint::ImageFileType::unknown,
            "plain text is not an image");
    require(paint::sniff_image_type(nullptr, 0) == paint::ImageFileType::unknown, "empty input");

    require(paint::resolve_image_type("photo.dat", png, sizeof(png)) == paint::ImageFileType::png,
            "an unknown extension with PNG bytes resolves to PNG");
    require(paint::resolve_image_type("photo.JPG", png, sizeof(png)) == paint::ImageFileType::png,
            "content wins over a wrong extension");
    require(paint::resolve_image_type("sprite.TGA", reinterpret_cast<const std::uint8_t*>(text.data()),
                                      text.size()) == paint::ImageFileType::tga,
            "TGA, which has no signature, is taken from its name");
    require(paint::resolve_image_type("fake.png", reinterpret_cast<const std::uint8_t*>(text.data()),
                                      text.size()) == paint::ImageFileType::unknown,
            "a PNG name over other content does not resolve");
    require(paint::resolve_image_type("art.SVGZ", gzip, sizeof(gzip)) == paint::ImageFileType::svgz,
            "gzip named as SVG is SVGZ");
    require(paint::resolve_image_type("archive.gz", gzip, sizeof(gzip)) == paint::ImageFileType::unknown,
            "gzip with another name is not an image");
}

void open_from_disk(const std::filesystem::path& directory) {
    paint::Image image;
    image.reset(5, 3, {12, 200, 90, 255});
    image.set(2, 1, {250, 10, 20, 128});
    const std::filesystem::path original = directory / "original.png";
    paint::save_image(image, path_text(original));
    const std::vector<std::uint8_t> png = paint::read_image_bytes(path_text(original));

    // Every spelling of the extension, no extension, and an unrelated one.
    const char* names[] = {"UPPER.PNG", "Mixed.Png", "lower.png", "content.bin", "noextension", "named.jpg"};
    for (const char* name : names) {
        const std::filesystem::path path = directory / name;
        write_bytes(path, png);
        const paint::Image loaded = paint::load_image(path_text(path));
        require(loaded.width == 5 && loaded.height == 3 && paint::equal(loaded.get(2, 1), image.get(2, 1)),
                "a PNG opens whatever its name");
        const paint::ImageContainer container = paint::load_container(path_text(path));
        require(container.frames.size() == 1 && container.frames[0].image.width == 5,
                "a PNG opens as a document whatever its name");
    }

    // Trailing bytes after IEND used to be walked as chunks and rejected as
    // "PNG chunk is truncated".
    std::vector<std::uint8_t> trailing = png;
    trailing.insert(trailing.end(), 64, 0xFF);
    const std::filesystem::path trailing_path = directory / "Trailing.PNG";
    write_bytes(trailing_path, trailing);
    require(load_failure(trailing_path).empty(), "a PNG with data after IEND opens");

    // An acTL chunk before the image data is still an animation and refused.
    std::vector<std::uint8_t> animated = png;
    const std::uint8_t actl[] = {0, 0, 0, 8, 'a', 'c', 'T', 'L', 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0};
    // Signature (8) plus the IHDR chunk (4 length + 4 type + 13 data + 4 CRC).
    animated.insert(animated.begin() + 33, std::begin(actl), std::end(actl));
    const std::filesystem::path animated_path = directory / "animated.png";
    write_bytes(animated_path, animated);
    require(contains(load_failure(animated_path), "Animation"), "an APNG is still refused");

    // Unsupported files say which file, what its name claimed, and what opens.
    const std::string notes = "Shopping list: paint, brushes.";
    const std::vector<std::uint8_t> text(notes.begin(), notes.end());
    write_bytes(directory / "notes.txt", text);
    const std::string unknown = load_failure(directory / "notes.txt");
    require(contains(unknown, "cannot open") && contains(unknown, "notes.txt") && contains(unknown, "PNG"),
            "an unknown file gets a message naming it and the supported formats");
    write_bytes(directory / "fake.PNG", text);
    require(contains(load_failure(directory / "fake.PNG"), "its name says PNG"),
            "a damaged PNG says its name claimed PNG");
    const std::vector<std::uint8_t> gzip = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 3};
    write_bytes(directory / "drawing.svgz", gzip);
    require(contains(load_failure(directory / "drawing.svgz"), "SVGZ"), "SVGZ gets its own message");
}

void command_lines() {
    const std::vector<std::string> first = {"--language=fr", "--bogus", "file:///tmp/My%20Art/Shot.PNG",
                                            "second.png"};
    const paint::CommandLine parsed = paint::parse_command_line(first);
    require(parsed.language == "fr", "language option");
    require(parsed.ignored_options.size() == 1 && parsed.ignored_options[0] == "--bogus",
            "unknown options are reported, not opened");
    require(parsed.initial_path == "/tmp/My Art/Shot.PNG", "a file URI becomes a decoded path");

    const std::vector<std::string> second = {"--canvas-controls", "--", "--odd name.png"};
    const paint::CommandLine ended = paint::parse_command_line(second);
    require(ended.canvas_controls && ended.initial_path == "--odd name.png", "-- ends the options");

    require(paint::path_from_file_uri("file://localhost/x.png") == "/x.png", "localhost file URI");
    require(paint::path_from_file_uri("file://server/x.png") == "file://server/x.png",
            "a remote file URI is left for the open to reject");
    require(paint::path_from_file_uri("plain/path.PNG") == "plain/path.PNG", "plain paths pass through");
    require(paint::path_from_file_uri("FILE:///a%2") == "/a%2", "a short escape is kept literally");
#ifdef _WIN32
    require(paint::path_from_file_uri("file:///C:/Art/x.png") == "C:/Art/x.png", "drive-letter file URI");
#endif
}

void recent_files() {
    paint::RecentFiles recent;
    recent.remember("C:/Art/Shot.PNG");
    recent.remember("c:\\art\\shot.png");
#ifdef _WIN32
    require(recent.paths.size() == 1 && recent.paths[0] == "c:\\art\\shot.png",
            "Windows recent files are one entry whatever the case and separators");
#else
    require(recent.paths.size() == 2, "case-sensitive systems keep distinct names");
#endif
}
} // namespace

int main() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "plan-paint-file-open";
    try {
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        extension_case();
        signatures();
        open_from_disk(directory);
        command_lines();
        recent_files();
        std::filesystem::remove_all(directory);
        std::cout << "File types, content sniffing, open messages, command lines and recent files passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        return 1;
    }
}
