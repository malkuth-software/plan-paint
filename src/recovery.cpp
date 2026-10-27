#include "recovery.hpp"
#include "codecs.hpp"
#include "paths.hpp"
#include "safe_file.hpp"
#include <algorithm>
#include <chrono>
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
#include <sys/file.h>
#include <unistd.h>
#endif
namespace paint {
namespace {
constexpr std::size_t maximum_record = 512000000;
bool valid_id(const std::string& id) {
    if (id.size() != 36) {
        return false;
    }
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (id[i] != '-') {
                return false;
            }
        } else if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}
std::string uuid() {
    std::random_device random;
    std::string id;
    const char* digits = "0123456789abcdef";
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            id += '-';
        } else {
            id += digits[random() & 15U];
        }
    }
    id[14] = '4';
    id[19] = digits[8U | (random() & 3U)];
    return id;
}
void append(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}
std::uint64_t take(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
    if (offset > bytes.size() || bytes.size() - offset < 8) {
        throw std::runtime_error("Incomplete recovery record.");
    }
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    }
    return value;
}
void append_text(std::vector<std::uint8_t>& bytes, const std::string& value) {
    if (value.size() > 65536) {
        throw std::runtime_error("Recovery filename is too long.");
    }
    append(bytes, value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
}
std::string take_text(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
    const std::uint64_t size = take(bytes, offset);
    if (size > 65536 || size > bytes.size() - offset) {
        throw std::runtime_error("Invalid recovery metadata.");
    }
    std::string value(bytes.begin() + offset, bytes.begin() + offset + size);
    offset += size;
    return value;
}
std::uint64_t checksum(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t value = 14695981039346656037ULL;
    for (std::size_t i = 0; i < size; ++i) {
        value = (value ^ bytes[i]) * 1099511628211ULL;
    }
    return value;
}
RecoveryRecord decode(const std::filesystem::path& file, const std::string& id) {
    const std::vector<std::uint8_t> bytes =
        read_regular_file_bounded(path_to_utf8(file), maximum_record, "Cannot read recovery snapshot.");
    std::size_t offset = 0;
    if (take(bytes, offset) != 0x3156434552505352ULL) {
        throw std::runtime_error("Unknown recovery format.");
    }
    const std::uint64_t expected = take(bytes, offset);
    if (checksum(bytes.data() + offset, bytes.size() - offset) != expected) {
        throw std::runtime_error("Damaged recovery snapshot.");
    }
    RecoveryRecord record;
    record.id = take_text(bytes, offset);
    if (record.id != id) {
        throw std::runtime_error("Recovery document ID mismatch.");
    }
    record.source_path = take_text(bytes, offset);
    record.opened_path = take_text(bytes, offset);
    record.created_ms = take(bytes, offset);
    record.captured_ms = take(bytes, offset);
    record.revision = take(bytes, offset);
    record.saved_ms = take(bytes, offset);
    record.generation = take(bytes, offset);
    const std::uint64_t kind = take(bytes, offset);
    const std::uint64_t length = take(bytes, offset);
    if (length != bytes.size() - offset || length == 0 || kind > 2) {
        throw std::runtime_error("Invalid recovery image.");
    }
    if (kind == 0) {
        record.image.frames.push_back({decode_image(bytes.data() + offset, length), 0, 0, {}});
    } else {
        record.image = decode_icon_container(bytes.data() + offset, length);
        if (static_cast<std::uint64_t>(record.image.kind) != kind) {
            throw std::runtime_error("Recovery container mismatch.");
        }
    }
    return record;
}
} // namespace
struct RecoverySession::Lease {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit Lease(const std::filesystem::path& path) {
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Recovery document is in use or unavailable.");
        }
    }
    ~Lease() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
#else
    int handle = -1;
    explicit Lease(const std::filesystem::path& path) {
        handle = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (handle < 0) {
            throw std::runtime_error("Cannot open recovery document lease.");
        }
        if (flock(handle, LOCK_EX | LOCK_NB) != 0) {
            close(handle);
            handle = -1;
            throw std::runtime_error("Recovery document is in use.");
        }
    }
    ~Lease() {
        if (handle >= 0) {
            close(handle);
        }
    }
#endif
};
std::uint64_t recovery_time_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}
RecoverySession::RecoverySession(const std::filesystem::path& root, const std::string& id)
    : id_(id.empty() ? uuid() : id) {
    if (!valid_id(id_)) {
        throw std::runtime_error("Invalid recovery document ID.");
    }
    directory_ = root / id_;
    if (id.empty()) {
        std::filesystem::create_directories(root);
        if (!std::filesystem::create_directory(directory_)) {
            throw std::runtime_error("Recovery ID already exists.");
        }
#ifndef _WIN32
        std::filesystem::permissions(directory_, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace);
#endif
    } else if (std::filesystem::symlink_status(directory_).type() != std::filesystem::file_type::directory) {
        throw std::runtime_error("Recovery directory is unavailable.");
    }
    lease_ = std::make_unique<Lease>(directory_ / "session.lock");
    next_slot_ = id.empty() ? 0 : -1;
}
RecoverySession::~RecoverySession() {
    lease_.reset();
    std::error_code error;
    if (!std::filesystem::exists(directory_ / "0.rpr", error) && !error &&
        !std::filesystem::exists(directory_ / "1.rpr", error) && !error) {
        std::filesystem::remove(directory_ / "session.lock", error);
        if (!error) {
            std::filesystem::remove(directory_, error);
        }
    }
}
void RecoverySession::write(RecoveryRecord record) {
    if (next_slot_ < 0) {
        try {
            static_cast<void>(read());
        } catch (const std::exception&) {
            next_slot_ = 0;
        }
    }
    record.id = id_;
    if (record.image.frames.empty()) {
        throw std::runtime_error("Cannot recover an empty image.");
    }
    std::vector<std::uint8_t> payload = record.image.kind == ContainerKind::Image
                                            ? encode_png(record.image.frames.front().image)
                                            : encode_icon_container(record.image);
    if (payload.size() > maximum_record - 200000) {
        throw std::runtime_error("Recovery snapshot exceeds the supported size.");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(payload.size() + 256 + record.source_path.size() + record.opened_path.size());
    append(bytes, 0x3156434552505352ULL);
    append(bytes, 0);
    append_text(bytes, id_);
    append_text(bytes, record.source_path);
    append_text(bytes, record.opened_path);
    append(bytes, record.created_ms);
    append(bytes, record.captured_ms);
    append(bytes, record.revision);
    append(bytes, record.saved_ms);
    append(bytes, generation_ + 1);
    append(bytes, static_cast<std::uint64_t>(record.image.kind));
    append(bytes, payload.size());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const std::uint64_t hash = checksum(bytes.data() + 16, bytes.size() - 16);
    for (unsigned i = 0; i < 8; ++i) {
        bytes[8 + i] = static_cast<std::uint8_t>(hash >> (8 * i));
    }
    write_file_atomic(bytes, path_to_utf8(directory_ / (std::to_string(next_slot_) + ".rpr")),
                      "Could not write the recovery snapshot. Previous snapshots remain available.");
    ++generation_;
    next_slot_ = 1 - next_slot_;
}
RecoveryRecord RecoverySession::read() const {
    RecoveryRecord best;
    bool found = false;
    for (int slot = 0; slot < 2; ++slot) {
        try {
            RecoveryRecord record = decode(directory_ / (std::to_string(slot) + ".rpr"), id_);
            if (!found || record.generation > best.generation) {
                best = std::move(record);
                found = true;
                next_slot_ = 1 - slot;
            }
        } catch (const std::exception&) {
        }
    }
    if (!found) {
        throw std::runtime_error("No intact recovery snapshot was found. The files have been retained.");
    }
    generation_ = best.generation;
    return best;
}
void RecoverySession::discard() {
    // Only our two records; never remove a directory containing unrecognized files.
    for (int slot = 0; slot < 2; ++slot) {
        std::filesystem::remove(directory_ / (std::to_string(slot) + ".rpr"));
    }
}
std::vector<std::string> RecoverySession::candidates(const std::filesystem::path& root) {
    std::vector<std::string> ids;
    if (!std::filesystem::exists(root)) {
        return ids;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root)) {
        const std::string id = path_to_utf8(entry.path().filename());
        if (!valid_id(id) || entry.symlink_status().type() != std::filesystem::file_type::directory) {
            continue;
        }
        if (!std::filesystem::exists(entry.path() / "0.rpr") &&
            !std::filesystem::exists(entry.path() / "1.rpr")) {
            continue;
        }
        try {
            Lease probe(entry.path() / "session.lock");
            ids.push_back(id);
        } catch (const std::exception&) {
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}
} // namespace paint
