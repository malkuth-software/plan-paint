#include "file_reveal.hpp"
#include "paths.hpp"
#include "safe_file.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
struct Directory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("paint-reveal-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() {
        std::filesystem::create_directory(path);
    }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void contracts() {
    Directory directory;
    const std::string path =
        paint::path_to_utf8(directory.path / paint::path_from_utf8("café, % # '$ (art).png"));
    paint::write_file_atomic({1, 2, 3}, path, "Cannot create reveal fixture");
    const paint::RevealTarget target = paint::prepare_file_reveal(path);
    require(std::filesystem::path(paint::path_from_utf8(target.path)).is_absolute(),
            "reveal path must be absolute");
    require(target.uri.find("caf%C3%A9%2C%20%25%20%23%20%27%24%20%28art%29.png") != std::string::npos,
            "URI encodes UTF-8, commas, shell metacharacters and URI punctuation");
    const std::vector<std::string> arguments = paint::reveal_dbus_arguments(target);
    require(arguments.size() == 9 && arguments[6] == "org.freedesktop.FileManager1.ShowItems" &&
                arguments[7] == "array:string:" + target.uri && arguments[8] == "string:",
            "DBus receives exactly one escaped URI and an empty startup ID");
    require(paint::prepare_file_reveal(paint::path_to_utf8(directory.path)).path ==
                paint::path_to_utf8(directory.path),
            "folders are selected in their parents rather than opened as files");
    require(paint::read_regular_file_bounded(path, 10, "read fixture") ==
                std::vector<std::uint8_t>({1, 2, 3}),
            "preparing a reveal never modifies the asset");
    for (const std::string& invalid : {std::string{}, path + ".missing", std::string("name\0suffix", 11)}) {
        bool rejected = false;
        try {
            static_cast<void>(paint::prepare_file_reveal(invalid));
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "missing, empty or NUL-containing paths rejected before launch");
    }
    std::atomic<bool> cancelled{true};
    require(paint::reveal_in_file_manager(path, &cancelled) == paint::RevealOutcome::Cancelled,
            "cancel before request does not invoke any desktop service");
#ifndef _WIN32
    const std::filesystem::path link = directory.path / "link.png";
    std::filesystem::create_symlink("missing-target", link);
    require(paint::prepare_file_reveal(paint::path_to_utf8(link)).path == paint::path_to_utf8(link),
            "a dangling symlink is revealed as the selected entry itself");
#endif
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--native") {
            const paint::RevealOutcome result = paint::reveal_in_file_manager(argv[2]);
            std::cout << (result == paint::RevealOutcome::SelectionRequested ? "selection" : "folder")
                      << '\n';
        } else {
            contracts();
            std::cout << "File reveal contracts passed\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
