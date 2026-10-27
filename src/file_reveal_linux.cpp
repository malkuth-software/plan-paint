#include "file_reveal.hpp"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char** environ;
namespace paint {
namespace {
struct ReapChild {
    pid_t child;
    void operator()() const {
        while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
        }
    }
};
bool request_program(const std::vector<std::string>& arguments, bool folder,
                     const std::atomic<bool>* cancel) {
    if (cancel && (*cancel).load()) {
        return false;
    }
    std::vector<char*> argv;
    for (const std::string& value : arguments) {
        argv.push_back(const_cast<char*>(value.c_str()));
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return false;
    }
    int setup = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    setup |= posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    setup |= posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t child = 0;
    const int launched =
        setup ? setup : posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (launched != 0) {
        return false;
    }
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(4);
    for (;;) {
        int status = 0;
        const pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        if (waited < 0 && errno != EINTR) {
            return false;
        }
        const bool cancelled = cancel && (*cancel).load();
        if (cancelled || std::chrono::steady_clock::now() >= deadline) {
            // xdg-open can remain attached to an opened desktop window. Do not close it.
            if (folder) {
                std::thread(ReapChild{child}).detach();
                return !cancelled;
            }
            kill(child, SIGKILL);
            ReapChild{child}();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
} // namespace
RevealOutcome reveal_in_file_manager(const std::string& path, const std::atomic<bool>* cancel) {
    const RevealTarget target = prepare_file_reveal(path);
    if (request_program(reveal_dbus_arguments(target), false, cancel)) {
        return RevealOutcome::SelectionRequested;
    }
    if (cancel && (*cancel).load()) {
        return RevealOutcome::Cancelled;
    }
    if (request_program({"xdg-open", target.parent_uri}, true, cancel)) {
        return RevealOutcome::FolderRequested;
    }
    if (cancel && (*cancel).load()) {
        return RevealOutcome::Cancelled;
    }
    throw std::runtime_error(
        "No file manager accepted the request. Install a FileManager1 service or xdg-open.");
}
} // namespace paint
