#pragma once
#include "atlas.hpp"
#include <filesystem>
#include <memory>
namespace paint {
struct RecoveryRecord {
    std::string id, source_path, opened_path;
    std::uint64_t created_ms = 0, captured_ms = 0, revision = 0, saved_ms = 0, generation = 0;
    ImageContainer image;
};
// An OS lease lives for the session, not for a guessed process-ID timeout.
// Destruction releases the lease but deliberately retains unfinished artwork.
class RecoverySession {
  public:
    explicit RecoverySession(const std::filesystem::path& root, const std::string& id = "");
    ~RecoverySession();
    RecoverySession(const RecoverySession&) = delete;
    RecoverySession& operator=(const RecoverySession&) = delete;
    const std::string& id() const {
        return id_;
    }
    void write(RecoveryRecord record);
    RecoveryRecord read() const;
    void discard();
    static std::vector<std::string> candidates(const std::filesystem::path& root);

  private:
    struct Lease;
    std::unique_ptr<Lease> lease_;
    std::filesystem::path directory_;
    std::string id_;
    mutable int next_slot_ = 0;
    mutable std::uint64_t generation_ = 0;
};
std::uint64_t recovery_time_ms();
} // namespace paint
