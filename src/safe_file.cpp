#include "safe_file.hpp"
#include "paths.hpp"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace paint {
namespace {
std::string random_token() {
    std::random_device source;
    const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve(32);
    for (int index = 0; index < 4; ++index) {
        const unsigned value = source();
        for (int shift = 28; shift >= 0; shift -= 4) {
            result.push_back(digits[(value >> shift) & 15U]);
        }
    }
    return result;
}
std::filesystem::path temporary_path(const std::filesystem::path& target) {
    return target.parent_path() /
           (target.filename().native() + path_from_utf8(".rainstar-" + random_token() + ".tmp").native());
}
#ifdef _WIN32
bool write_all(HANDLE file, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD requested = static_cast<DWORD>(
            std::min(remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, requested, &written, nullptr) || written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}
// Outcome of renaming the open temporary file over the target in place.
enum class HandleRename { renamed, failed, unsupported };
// Renames the file open as `file` (opened with DELETE access) to `target`
// with POSIX semantics: the target is replaced even while another handle
// that shares delete access, such as Plan Paint's own file-change probe or
// a scanner, has it open. MoveFileExW refuses that replacement with
// ERROR_ACCESS_DENIED. File systems without POSIX renames report
// `unsupported`, and the caller falls back to MoveFileExW.
HandleRename rename_open_file(HANDLE file, const std::filesystem::path& target, bool replace_existing) {
    const std::wstring& name = target.native();
    const std::size_t name_bytes = name.size() * sizeof(wchar_t);
    if (name_bytes > std::numeric_limits<DWORD>::max() - sizeof(FILE_RENAME_INFO)) {
        return HandleRename::failed;
    }
    // FILE_RENAME_INFO ends in a one-character array; the name runs past it.
    const std::size_t storage_bytes = sizeof(FILE_RENAME_INFO) + name_bytes;
    std::vector<std::uint8_t> storage(storage_bytes, 0);
    FILE_RENAME_INFO* information = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    (*information).Flags = FILE_RENAME_FLAG_POSIX_SEMANTICS;
    if (replace_existing) {
        (*information).Flags |= FILE_RENAME_FLAG_REPLACE_IF_EXISTS;
    }
    (*information).RootDirectory = nullptr;
    (*information).FileNameLength = static_cast<DWORD>(name_bytes);
    std::memcpy(storage.data() + offsetof(FILE_RENAME_INFO, FileName), name.data(), name_bytes);
    if (SetFileInformationByHandle(file, FileRenameInfoEx, information, static_cast<DWORD>(storage_bytes))) {
        return HandleRename::renamed;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED || error == ERROR_INVALID_FUNCTION) {
        return HandleRename::unsupported;
    }
    return HandleRename::failed;
}
#else
bool write_all(int file, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written = write(file, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}
#endif
} // namespace

std::vector<std::uint8_t> read_regular_file_bounded(const std::string& path, std::size_t maximum,
                                                    const char* failure_message) {
    const std::filesystem::path source = path_from_utf8(path);
#ifdef _WIN32
    HANDLE file = CreateFileW(source.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(failure_message);
    }
    try {
        BY_HANDLE_FILE_INFORMATION information{};
        LARGE_INTEGER length{};
        if (!GetFileInformationByHandle(file, &information) || GetFileType(file) != FILE_TYPE_DISK ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            !GetFileSizeEx(file, &length) || length.QuadPart <= 0 ||
            static_cast<unsigned long long>(length.QuadPart) > maximum ||
            static_cast<unsigned long long>(length.QuadPart) >
                static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error(failure_message);
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length.QuadPart));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const DWORD requested = static_cast<DWORD>(std::min(
                bytes.size() - offset, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD received = 0;
            if (!ReadFile(file, bytes.data() + offset, requested, &received, nullptr) || received == 0) {
                throw std::runtime_error(failure_message);
            }
            offset += received;
        }
        const bool closed = CloseHandle(file) != 0;
        file = INVALID_HANDLE_VALUE;
        if (!closed) {
            throw std::runtime_error(failure_message);
        }
        return bytes;
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        throw;
    }
#else
    int file = open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (file < 0) {
        throw std::runtime_error(failure_message);
    }
    try {
        struct stat information {};
        if (fstat(file, &information) != 0 || !S_ISREG(information.st_mode) || information.st_size <= 0 ||
            static_cast<std::uint64_t>(information.st_size) > maximum ||
            static_cast<std::uint64_t>(information.st_size) >
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error(failure_message);
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(information.st_size));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const ssize_t received = read(file, bytes.data() + offset, bytes.size() - offset);
            if (received < 0 && errno == EINTR) {
                continue;
            }
            if (received <= 0) {
                throw std::runtime_error(failure_message);
            }
            offset += static_cast<std::size_t>(received);
        }
        const bool closed = close(file) == 0;
        file = -1;
        if (!closed) {
            throw std::runtime_error(failure_message);
        }
        return bytes;
    } catch (...) {
        if (file >= 0) {
            close(file);
        }
        throw;
    }
#endif
}

void write_file_atomic(const std::vector<std::uint8_t>& bytes, const std::string& path,
                       const char* failure_message, bool replace_existing) {
    const std::filesystem::path target = path_from_utf8(path);
    std::filesystem::path temporary;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 64 && file == INVALID_HANDLE_VALUE; ++attempt) {
        temporary = temporary_path(target);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) {
            throw std::runtime_error(failure_message);
        }
    }
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(failure_message);
    }
    const bool written = write_all(file, bytes) && FlushFileBuffers(file);
    HandleRename rename = HandleRename::failed;
    if (written) {
        rename = rename_open_file(file, target, replace_existing);
    }
    const bool closed = CloseHandle(file) != 0;
    if (rename == HandleRename::renamed) {
        // The data was flushed before the rename; a failed close cannot lose it.
        return;
    }
    if (!written || !closed || rename == HandleRename::failed ||
        !MoveFileExW(temporary.c_str(), target.c_str(),
                     (replace_existing ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error(failure_message);
    }
#else
    int file = -1;
    for (int attempt = 0; attempt < 64 && file < 0; ++attempt) {
        temporary = temporary_path(target);
        file = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0666);
        if (file < 0 && errno != EEXIST) {
            throw std::runtime_error(failure_message);
        }
    }
    if (file < 0) {
        throw std::runtime_error(failure_message);
    }
    const bool written = write_all(file, bytes) && fsync(file) == 0;
    const bool closed = close(file) == 0;
    if (!written || !closed) {
        unlink(temporary.c_str());
        throw std::runtime_error(failure_message);
    }
    // A hard-link publication atomically refuses an existing destination. The
    // temporary is a sibling, so both names are on the same filesystem.
    const int published = replace_existing ? rename(temporary.c_str(), target.c_str())
                                           : link(temporary.c_str(), target.c_str());
    if (!replace_existing || published != 0) {
        unlink(temporary.c_str());
    }
    if (published != 0) {
        throw std::runtime_error(failure_message);
    }
#endif
}
} // namespace paint
