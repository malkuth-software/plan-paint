#include "localization.hpp"
#include "paths.hpp"
#include <cstdlib>
#include <cstdint>
#include <vector>
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace paint {
namespace {
void append_language_list(std::vector<std::string>& output, const char* list) {
    if (!list) { return; }
    const std::string text(list);
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find(':', start);
        const std::string normalized = normalize_language_tag(text.substr(start, end == std::string::npos ? end : end - start));
        if (!normalized.empty()) { output.push_back(normalized); }
        if (end == std::string::npos) { break; }
        start = end + 1;
    }
}
} // namespace
std::vector<std::string> system_languages() {
    std::vector<std::string> result;
#if defined(__APPLE__)
    CFArrayRef preferred = CFLocaleCopyPreferredLanguages();
    if (preferred) {
        const CFIndex count = CFArrayGetCount(preferred);
        for (CFIndex i = 0; i < count && i < 128; ++i) {
            CFStringRef language = static_cast<CFStringRef>(CFArrayGetValueAtIndex(preferred, i));
            char name[256] = {};
            if (language && CFGetTypeID(language) == CFStringGetTypeID() &&
                CFStringGetCString(language, name, sizeof(name), kCFStringEncodingUTF8)) {
                append_language_list(result, name);
            }
        }
        CFRelease(preferred);
    }
#elif defined(_WIN32)
    ULONG count = 0, size = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 0 && size < 65536) {
        std::vector<wchar_t> names(size);
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, names.data(), &size)) {
            std::size_t index = 0;
            while (index < names.size() && names[index]) {
                std::string name;
                while (index < names.size() && names[index]) { name.push_back(static_cast<char>(names[index++])); }
                append_language_list(result, name.c_str());
                ++index;
            }
        }
    }
    if (result.empty()) {
        wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) {
            const std::wstring wide(name);
            const std::string narrow(wide.begin(), wide.end());
            append_language_list(result, narrow.c_str());
        }
    }
#else
    const char* locale = std::getenv("LC_ALL");
    if (!locale || !*locale) { locale = std::getenv("LC_MESSAGES"); }
    if (!locale || !*locale) { locale = std::getenv("LANG"); }
    const std::string normalized = locale ? normalize_language_tag(locale) : "";
    // GNU LANGUAGE is an ordered message-language preference unless the process
    // explicitly requests the C/POSIX locale.
    if (normalized != "en-us" || (locale && std::string_view(locale).starts_with("en"))) {
        append_language_list(result, std::getenv("LANGUAGE"));
    }
    append_language_list(result, locale);
#endif
    if (result.empty()) { result.push_back("en-us"); }
    return result;
}
std::filesystem::path application_language_directory() {
    std::filesystem::path executable;
#if defined(__APPLE__)
    std::uint32_t size = 0;
    static_cast<void>(_NSGetExecutablePath(nullptr, &size));
    if (size > 0 && size < 65536) {
        std::vector<char> path(size);
        if (_NSGetExecutablePath(path.data(), &size) == 0) { executable = path_from_utf8(path.data()); }
    }
#elif defined(_WIN32)
    std::vector<wchar_t> path(32768);
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (size > 0 && size < path.size()) { executable = std::filesystem::path(std::wstring(path.data(), size)); }
#else
    std::vector<char> path(65536);
    const ssize_t size = readlink("/proc/self/exe", path.data(), path.size());
    if (size > 0 && static_cast<std::size_t>(size) < path.size()) {
        executable = path_from_utf8(std::string_view(path.data(), static_cast<std::size_t>(size)));
    }
#endif
    if (executable.empty()) { return {}; }
#if defined(__APPLE__)
    if (executable.parent_path().filename() == "MacOS") {
        return executable.parent_path().parent_path() / "Resources" / "languages";
    }
#endif
    return executable.parent_path() / "languages";
}
} // namespace paint
