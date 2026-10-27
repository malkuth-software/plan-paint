#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>
namespace paint {
enum class RevealOutcome { SelectionRequested, FolderRequested, Cancelled };
struct RevealTarget {
    std::string path, uri, parent_uri;
};
// Existing filesystem entries only; paths are absolute and never interpreted as commands.
RevealTarget prepare_file_reveal(const std::string& path);
std::vector<std::string> reveal_dbus_arguments(const RevealTarget& target);
RevealOutcome reveal_in_file_manager(const std::string& path, const std::atomic<bool>* cancel = nullptr);
using RevealHandler = std::function<RevealOutcome(const std::string&, const std::atomic<bool>*)>;
} // namespace paint
