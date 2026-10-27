#pragma once
#include <filesystem>
#include <string_view>
namespace paint {
inline std::filesystem::path path_from_utf8(std::string_view path) {
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}
inline std::string path_to_utf8(const std::filesystem::path& path) {
    std::u8string bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}
} // namespace paint
