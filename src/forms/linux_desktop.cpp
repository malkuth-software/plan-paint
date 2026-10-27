#include "localization.hpp"
#include "forms/linux_desktop.hpp"
#include "desktop.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <locale>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char** environ;
namespace paint {
namespace {
struct WaitChild {
    pid_t child;
    void operator()() const {
        int status;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
    }
};
std::string run_program(const std::vector<std::string>& arguments, bool wait = true) {
    std::vector<char*> argv;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        argv.push_back(const_cast<char*>(arguments[i].c_str()));
    }
    argv.push_back(nullptr);
    int output[2];
    if (pipe(output) != 0) {
        throw std::runtime_error(tr("Cannot create a desktop service channel."));
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output[1], STDERR_FILENO);
    if (!wait) {
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }
    posix_spawn_file_actions_addclose(&actions, output[0]);
    posix_spawn_file_actions_addclose(&actions, output[1]);
    pid_t child = 0;
    const int error = posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(output[1]);
    if (error != 0) {
        close(output[0]);
        throw std::runtime_error(arguments[0] + ": " + std::strerror(error));
    }
    // Acquisition applications can remain open independently of Paint.
    if (!wait) {
        close(output[0]);
        std::thread(WaitChild{child}).detach();
        return "";
    }
    std::string result;
    char buffer[4096];
    for (;;) {
        const ssize_t count = read(output[0], buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        if (result.size() < 65536) {
            result.append(buffer, static_cast<std::size_t>(count));
        }
    }
    close(output[0]);
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error(arguments[0] + tr(" failed. ") + result);
    }
    return result;
}
struct TemporaryFile {
    std::string path;
    TemporaryFile() {
        char name[] = "/tmp/rainstar-print-XXXXXX";
        const int descriptor = mkstemp(name);
        if (descriptor < 0) {
            throw std::runtime_error(tr("Cannot create a temporary print file."));
        }
        close(descriptor);
        path = name;
    }
    ~TemporaryFile() {
        unlink(path.c_str());
    }
};
} // namespace
bool acquire_picture(std::string&) {
    try {
        static_cast<void>(run_program({"simple-scan"}, false));
    } catch (const std::exception&) {
        try {
            static_cast<void>(run_program({"xsane"}, false));
        } catch (const std::exception&) {
            throw std::runtime_error(
                tr("Install Document Scanner (simple-scan) or XSane, then try this command again."));
        }
    }
    return false;
}
void set_wallpaper(const std::string& path) {
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    const std::string name = desktop == nullptr ? "" : desktop;
    if (name.find("KDE") != std::string::npos) {
        static_cast<void>(run_program({"plasma-apply-wallpaperimage", path}));
        return;
    }
    const bool cinnamon = name.find("Cinnamon") != std::string::npos;
    if (!cinnamon && name.find("GNOME") == std::string::npos && name.find("Unity") == std::string::npos &&
        name.find("Budgie") == std::string::npos) {
        throw std::runtime_error(tr("Wallpaper integration supports GNOME, Cinnamon, Budgie and KDE. Use this "
                                 "desktop's background settings with a saved picture."));
    }
    const std::string schema = cinnamon ? "org.cinnamon.desktop.background" : "org.gnome.desktop.background";
    std::string uri = "file://";
    const std::string absolute = std::filesystem::absolute(path).string();
    const char* hex = "0123456789ABCDEF";
    for (std::size_t i = 0; i < absolute.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(absolute[i]);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            uri += static_cast<char>(c);
        } else {
            uri += '%';
            uri += hex[c >> 4];
            uri += hex[c & 15];
        }
    }
    static_cast<void>(run_program({"gsettings", "set", schema, "picture-uri", uri}));
    const std::string keys = run_program({"gsettings", "list-keys", schema});
    if (keys.find("picture-uri-dark\n") != std::string::npos) {
        static_cast<void>(run_program({"gsettings", "set", schema, "picture-uri-dark", uri}));
    }
    if (keys.find("picture-options\n") != std::string::npos) {
        static_cast<void>(run_program({"gsettings", "set", schema, "picture-options", "stretched"}));
    }
}
namespace forms {
LinuxPrintSettings& linux_print_settings() {
    static LinuxPrintSettings settings;
    return settings;
}
std::vector<std::string> linux_printers() {
    std::vector<std::string> names;
    const std::string output = run_program({"lpstat", "-a"});
    std::size_t start = 0;
    while (start < output.size()) {
        const std::size_t end = output.find('\n', start), space = output.find(' ', start);
        if (space != std::string::npos && (end == std::string::npos || space < end)) {
            names.push_back(output.substr(start, space - start));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return names;
}
void write_print_postscript(const Image& image, const LinuxPrintSettings& settings, const std::string& path) {
    if (image.width <= 0 || image.height <= 0 || !std::isfinite(settings.width_mm) ||
        !std::isfinite(settings.height_mm) || !std::isfinite(settings.margin_mm) ||
        settings.width_mm <= 2 * settings.margin_mm || settings.height_mm <= 2 * settings.margin_mm ||
        settings.margin_mm < 0) {
        throw std::runtime_error(tr("Choose paper dimensions larger than twice the margin."));
    }
    std::ofstream file(path, std::ios::binary);
    file.imbue(std::locale::classic());
    const double width = settings.width_mm * 72 / 25.4, height = settings.height_mm * 72 / 25.4,
                 margin = settings.margin_mm * 72 / 25.4;
    const double scale = std::min((width - 2 * margin) / image.width, (height - 2 * margin) / image.height);
    file << "%!PS-Adobe-3.0\n%%Title: Plan Paint picture\n%%Pages: 1\n%%BoundingBox: 0 0 "
         << std::ceil(width) << ' ' << std::ceil(height) << "\n%%EndComments\n<< /PageSize [" << width << ' '
         << height << "] >> setpagedevice\n%%Page: 1 1\ngsave\n"
         << (width - image.width * scale) / 2 << ' ' << (height - image.height * scale) / 2 << " translate\n"
         << image.width * scale << ' ' << image.height * scale
         << " scale\n/paintdata currentfile /ASCIIHexDecode filter def\n"
         << image.width << ' ' << image.height << " 8 [" << image.width << " 0 0 -" << image.height << " 0 "
         << image.height << "]\npaintdata false 3 colorimage\n";
    const char* hex = "0123456789abcdef";
    // Flatten straight-alpha pixels onto paper without changing the document.
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const Color pixel = image.pixels[i];
        const unsigned channels[] = {pixel.r, pixel.g, pixel.b};
        for (unsigned c = 0; c < 3; ++c) {
            const unsigned value = (channels[c] * pixel.a + 255U * (255U - pixel.a) + 127U) / 255U;
            file.put(hex[value >> 4]);
            file.put(hex[value & 15]);
        }
        if (i % 12 == 11) {
            file.put('\n');
        }
    }
    file << ">\ngrestore\nshowpage\n%%EOF\n";
    file.close();
    if (!file) {
        throw std::runtime_error(tr("Cannot write the print job."));
    }
}
void linux_print_image(const Image& image, const LinuxPrintSettings& settings) {
    TemporaryFile file;
    write_print_postscript(image, settings, file.path);
    std::vector<std::string> arguments = {"lp", "-t", tr("Plan Paint picture"), "-n",
                                          std::to_string(settings.copies)};
    if (!settings.printer.empty()) {
        arguments.push_back("-d");
        arguments.push_back(settings.printer);
    }
    arguments.push_back("--");
    arguments.push_back(file.path);
    static_cast<void>(run_program(arguments));
}
} // namespace forms
} // namespace paint
