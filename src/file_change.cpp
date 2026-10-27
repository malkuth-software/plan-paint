#include "file_change.hpp"
#include "paths.hpp"
#include <system_error>

namespace paint {
FileStamp file_stamp(const std::string& path) noexcept {
    try {
        const std::filesystem::path file = path_from_utf8(path);
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::status(file, error);
        if (status.type() == std::filesystem::file_type::not_found) {
            return {FileAvailability::Missing};
        }
        if (error || !std::filesystem::is_regular_file(status)) {
            return {};
        }
        const std::uintmax_t bytes = std::filesystem::file_size(file, error);
        if (error) {
            return {};
        }
        const std::filesystem::file_time_type modified = std::filesystem::last_write_time(file, error);
        if (error) {
            return {};
        }
        return {FileAvailability::Available, bytes, modified};
    } catch (...) {
        return {};
    }
}
} // namespace paint
