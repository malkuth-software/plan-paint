#include "platform.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
namespace paint {
std::vector<std::string> installed_fonts() {
    std::vector<std::string> result;
    std::filesystem::path bundled;
    const char* override_directory = std::getenv("GUI_FORMS_FONT_DIR");
    if (override_directory) {
        bundled = override_directory;
    } else {
#ifdef __APPLE__
        std::uint32_t length = 0;
        _NSGetExecutablePath(nullptr, &length);
        std::vector<char> executable(length);
        if (_NSGetExecutablePath(executable.data(), &length) == 0) {
            bundled = std::filesystem::path(executable.data()).parent_path() / "../Resources/fonts";
        }
#elif defined(_WIN32)
        std::vector<wchar_t> executable(32768);
        DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length > 0 && length < executable.size()) {
            bundled = std::filesystem::path(executable.data()).parent_path() / "fonts";
        }
#else
        std::error_code error;
        bundled = std::filesystem::read_symlink("/proc/self/exe", error).parent_path() / "fonts";
#endif
    }
    std::error_code bundled_error;
    if (!bundled.empty() && std::filesystem::is_directory(bundled, bundled_error)) {
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(bundled, bundled_error)) {
            std::filesystem::path path = entry.path();
            if (path.extension() == ".ttf" || path.extension() == ".otf") {
                std::u8string encoded = path.u8string();
                result.emplace_back(encoded.begin(), encoded.end());
            }
        }
    }
    std::sort(result.begin(), result.end());
    const std::size_t bundled_count = result.size();
#ifdef __APPLE__
    const char* directories[] = {"/System/Library/Fonts/Supplemental", "/Library/Fonts"};
#elif defined(_WIN32)
    const char* directories[] = {"C:/Windows/Fonts"};
#else
    const char* directories[] = {"/usr/share/fonts", "/usr/local/share/fonts"};
#endif
    for (const char* directory : directories) {
        std::error_code error;
        std::filesystem::recursive_directory_iterator cursor(
            directory, std::filesystem::directory_options::skip_permission_denied, error);
        std::filesystem::recursive_directory_iterator end;
        while (!error && cursor != end && result.size() < 1024) {
            std::filesystem::path path = (*cursor).path();
            std::string extension = path.extension().string();
            if ((*cursor).is_regular_file(error) &&
                (extension == ".ttf" || extension == ".TTF" || extension == ".otf")) {
                std::u8string encoded = path.u8string();
                result.emplace_back(encoded.begin(), encoded.end());
            }
            cursor.increment(error);
        }
    }
    std::sort(result.begin() + static_cast<std::ptrdiff_t>(bundled_count), result.end());
    return result;
}
} // namespace paint
