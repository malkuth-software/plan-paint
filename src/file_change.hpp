#pragma once
#include <filesystem>
#include <string>

namespace paint {
enum class FileAvailability { Available, Missing, Unavailable };
struct FileStamp {
    FileAvailability availability = FileAvailability::Unavailable;
    std::uintmax_t bytes = 0;
    std::filesystem::file_time_type modified{};
    bool operator==(const FileStamp&) const = default;
};
// Metadata detects ordinary external edits, not content identity. No file contents are read.
FileStamp file_stamp(const std::string& path) noexcept;
} // namespace paint
