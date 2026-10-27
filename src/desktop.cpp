#include "desktop.hpp"
#include "localization.hpp"
#include "codecs.hpp"
#include "conv.hpp"
#include "paths.hpp"
#include "safe_file.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
namespace paint {
// Retain pre-1.0 storage so Plan Paint finds existing settings and recovery data.
std::string preference_directory() {
    std::filesystem::path directory;
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) {
        directory = std::filesystem::path(home) / "Library/Application Support/rainstar/RainstarPaint";
    }
#elif defined(_WIN32)
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    if (appdata) {
        directory = std::filesystem::path(appdata) / L"rainstar" / L"RainstarPaint";
    }
#else
    const char* data = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    if (data && *data) {
        directory = std::filesystem::path(data) / "rainstar/RainstarPaint";
    } else if (home) {
        directory = std::filesystem::path(home) / ".local/share/rainstar/RainstarPaint";
    }
#endif
    if (directory.empty()) {
        throw std::runtime_error("Paint could not find its preferences folder.");
    }
    std::filesystem::create_directories(directory);
    return path_to_utf8(directory) + std::string(1, std::filesystem::path::preferred_separator);
}
void EditorSettings::load() {
    if (storage_path.empty()) {
        return;
    }
    std::vector<std::uint8_t> bytes;
    try {
        bytes = read_regular_file_bounded(storage_path, 4096, "Paint could not read its settings file.");
    } catch (const std::exception&) {
        return;
    }
    const std::string encoded(bytes.begin(), bytes.end());
    std::istringstream input(encoded);
    std::string magic;
    double distance = 0;
    if (!(input >> magic >> distance)) {
        return;
    }
    const int version = magic.size() == 5 && magic.starts_with("RSPS") ? magic[4] - '0' : 0;
    if (version < 1 || version > 8 || !std::isfinite(distance) || distance < 0.1 || distance > 100) {
        return;
    }
    scroll_distance = distance;
    int background = 0;
    if (version >= 2 && input >> background) {
        canvas_backing = background >= 0 && background < (version >= 4 ? canvas_backing_count : 2)
                             ? static_cast<CanvasBacking>(background) : CanvasBacking::PaleFelt;
    }
    int solid = 0;
    std::string color;
    Color parsed;
    if (version >= 3 && input >> solid >> color && from_hex(color, parsed)) {
        solid_transparency = solid == 1;
        parsed.a = 255;
        transparency_color = parsed;
    }
    int drag = 0, rotate = 0;
    drag_shapes = version >= 5 && (input >> drag) && drag == 1;
    rotate_view = version >= 6 && (input >> rotate) && rotate == 1;
    if (version >= 7) {
        int hue = 220, enabled = 1, seconds = 60;
        if (input >> hue >> enabled >> seconds) {
            interface_hue = hue >= 0 && hue <= 359 ? hue : 220;
            recovery_enabled = enabled != 0;
            recovery_seconds = std::clamp(seconds, 15, 600);
        }
    }
    if (version >= 8) {
        std::string preference;
        int controls = 0;
        if (input >> preference >> controls) {
            language = preference == "system" ? preference : normalize_language_tag(preference);
            if (language.empty()) { language = "system"; }
            canvas_controls = controls == 1;
        }
    }
}
void EditorSettings::save() const {
    if (storage_path.empty()) {
        return;
    }
    std::ostringstream output;
    output << "RSPS8\n"
           << scroll_distance << '\n'
           << static_cast<int>(canvas_backing) << '\n'
           << (solid_transparency ? 1 : 0) << '\n'
           << to_hex(transparency_color) << '\n'
           << (drag_shapes ? 1 : 0) << '\n'
           << (rotate_view ? 1 : 0) << '\n'
           << interface_hue << '\n' << (recovery_enabled ? 1 : 0) << '\n'
           << recovery_seconds << '\n' << language << '\n' << (canvas_controls ? 1 : 0) << '\n';
    const std::string encoded = output.str();
    const std::vector<std::uint8_t> bytes(encoded.begin(), encoded.end());
    write_file_atomic(bytes, storage_path, "Paint could not save its settings file.");
}
void RecentFiles::load() {
    paths.clear();
    if (storage_path.empty()) {
        return;
    }
    std::vector<std::uint8_t> bytes;
    try {
        bytes =
            read_regular_file_bounded(storage_path, 1000000, "Paint could not read its recent-files list.");
    } catch (const std::exception&) {
        return;
    }
    const std::string encoded(bytes.begin(), bytes.end());
    std::istringstream input(encoded, std::ios::in | std::ios::binary);
    std::vector<std::string> loaded;
    // Length-prefixed records preserve spaces, backslashes and embedded newlines.
    for (int index = 0; index < 12 && input; ++index) {
        std::uint32_t length = 0;
        input.read(reinterpret_cast<char*>(&length), sizeof(length));
        if (!input || length == 0 || length > 65536) {
            break;
        }
        std::string path(length, '\0');
        input.read(path.data(), length);
        if (!input || path.find('\0') != std::string::npos) {
            break;
        }
        loaded.push_back(path);
    }
    paths = std::move(loaded);
}
void RecentFiles::remember(const std::string& path) {
    if (path.empty() || path.size() > 65536) {
        return;
    }
    paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
    paths.insert(paths.begin(), path);
    if (paths.size() > 12) {
        paths.resize(12);
    }
    if (storage_path.empty()) {
        return;
    }
    std::vector<std::uint8_t> bytes;
    for (const std::string& item : paths) {
        const std::uint32_t length = static_cast<std::uint32_t>(item.size());
        const std::uint8_t* encoded_length = reinterpret_cast<const std::uint8_t*>(&length);
        bytes.insert(bytes.end(), encoded_length, encoded_length + sizeof(length));
        bytes.insert(bytes.end(), item.begin(), item.end());
    }
    write_file_atomic(bytes, storage_path, "Paint could not save its recent-files list.");
}
std::string desktop_export(const Image& image, const char* purpose) {
    const std::int64_t tick = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
    std::string path = preference_directory() + purpose + "-" + std::to_string(tick) + ".png";
    save_image(image, path);
    return path;
}
Image wallpaper_image(const Image& image, int width, int height, WallpaperLayout layout) {
    if (image.width <= 0 || image.height <= 0) {
        throw std::runtime_error("There is no picture to use as wallpaper.");
    }
    Image result;
    result.reset(width, height);
    if (layout == WallpaperLayout::Fill) {
        int crop_width = image.width;
        int crop_height = image.height;
        if (static_cast<std::int64_t>(image.width) * height >
            static_cast<std::int64_t>(image.height) * width) {
            crop_width = static_cast<int>(std::ceil(static_cast<double>(image.height) * width / height));
        } else {
            crop_height = static_cast<int>(std::ceil(static_cast<double>(image.width) * height / width));
        }
        Image cropped;
        cropped.reset(crop_width, crop_height, {0, 0, 0, 0});
        composite(cropped, image, -(image.width - crop_width) / 2, -(image.height - crop_height) / 2);
        Image scaled;
        conv_resize(cropped, width, height, scaled);
        composite(result, scaled, 0, 0);
    } else if (layout == WallpaperLayout::Center) {
        composite(result, image, (width - image.width) / 2, (height - image.height) / 2);
    } else {
        for (int y = 0; y < height; y += image.height) {
            for (int x = 0; x < width; x += image.width) {
                composite(result, image, x, y);
            }
        }
    }
    return result;
}
} // namespace paint
