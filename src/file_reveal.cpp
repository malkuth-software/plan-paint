#include "file_reveal.hpp"
#include "paths.hpp"
#include <stdexcept>
namespace paint {
namespace {
std::string file_uri(const std::filesystem::path& path) {
    const std::u8string bytes = path.generic_u8string();
    std::string result = bytes.starts_with(u8"/") ? "file://" : "file:///";
    constexpr char hex[] = "0123456789ABCDEF";
    for (const char8_t value : bytes) {
        const unsigned byte = static_cast<unsigned char>(value);
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
            byte == '-' || byte == '_' || byte == '.' || byte == '~' || byte == '/') {
            result.push_back(static_cast<char>(byte));
        } else {
            result.push_back('%');
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 15]);
        }
    }
    return result;
}
} // namespace
RevealTarget prepare_file_reveal(const std::string& path) {
    if (path.empty() || path.find('\0') != std::string::npos) {
        throw std::runtime_error("Choose an existing file or folder to reveal.");
    }
    // Do not canonicalize: reveal the selected symlink itself rather than its target.
    const std::filesystem::path absolute = std::filesystem::absolute(path_from_utf8(path));
    if (!std::filesystem::exists(std::filesystem::symlink_status(absolute))) {
        throw std::runtime_error("The selected file or folder no longer exists.");
    }
    return {path_to_utf8(absolute), file_uri(absolute), file_uri(absolute.parent_path())};
}
std::vector<std::string> reveal_dbus_arguments(const RevealTarget& target) {
    return {"dbus-send",
            "--session",
            "--print-reply",
            "--reply-timeout=3000",
            "--dest=org.freedesktop.FileManager1",
            "/org/freedesktop/FileManager1",
            "org.freedesktop.FileManager1.ShowItems",
            "array:string:" + target.uri,
            "string:"};
}
} // namespace paint
