#include "command_line.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace paint {
namespace {
int hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}
bool starts_with_folded(const std::string& text, const char* prefix) {
    std::size_t index = 0;
    for (; prefix[index] != '\0'; ++index) {
        if (index >= text.size()) {
            return false;
        }
        char character = text[index];
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if (character != prefix[index]) {
            return false;
        }
    }
    return true;
}
#ifdef _WIN32
std::string utf8_from_wide(const wchar_t* text) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), bytes, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(bytes) - 1);
    return result;
}
#endif
} // namespace

std::vector<std::string> utf8_arguments(int argc, char** argv) {
    std::vector<std::string> result;
#ifdef _WIN32
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide != nullptr) {
        // Owned by the shell allocator until LocalFree below.
        for (int index = 1; index < count; ++index) {
            result.push_back(utf8_from_wide(wide[index]));
        }
        LocalFree(wide);
        return result;
    }
#endif
    for (int index = 1; index < argc; ++index) {
        result.push_back(argv[index]);
    }
    return result;
}
std::string path_from_file_uri(const std::string& argument) {
    if (!starts_with_folded(argument, "file://")) {
        return argument;
    }
    std::string rest = argument.substr(7);
    if (starts_with_folded(rest, "localhost/")) {
        rest = rest.substr(9);
    } else if (rest.empty() || rest.front() != '/') {
        // A remote host: not a local file, so leave it for the open to reject.
        return argument;
    }
    std::string decoded;
    decoded.reserve(rest.size());
    for (std::size_t index = 0; index < rest.size(); ++index) {
        const char character = rest[index];
        if (character == '%' && index + 2 < rest.size()) {
            const int high = hex_value(rest[index + 1]);
            const int low = hex_value(rest[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>(high * 16 + low));
                index += 2;
                continue;
            }
        }
        decoded.push_back(character);
    }
#ifdef _WIN32
    // file:///C:/dir/x.png names C:/dir/x.png.
    if (decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':') {
        decoded.erase(decoded.begin());
    }
#endif
    return decoded;
}
CommandLine parse_command_line(std::span<const std::string> arguments) {
    CommandLine result;
    bool options_ended = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (!options_ended && argument == "--") {
            options_ended = true;
        } else if (!options_ended && argument.starts_with("--language=")) {
            result.language = argument.substr(11);
        } else if (!options_ended && argument == "--canvas-controls") {
            result.canvas_controls = true;
        } else if (!options_ended && argument.starts_with("--")) {
            result.ignored_options.push_back(argument);
        } else if (result.initial_path.empty() && !argument.empty()) {
            result.initial_path = path_from_file_uri(argument);
        }
    }
    return result;
}
} // namespace paint
