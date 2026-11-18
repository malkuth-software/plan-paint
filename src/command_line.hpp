#pragma once
#include <span>
#include <string>
#include <vector>

namespace paint {
// What Plan Paint's command line asks for. Arguments are UTF-8.
struct CommandLine {
    // The first file operand: a path, or a file:// URI converted to a path.
    std::string initial_path;
    // --language=<tag>; empty when not given.
    std::string language;
    bool canvas_controls = false;
    // Options Plan Paint does not know. They are reported, never opened as files.
    std::vector<std::string> ignored_options;
};

// The process arguments after the program name, as UTF-8. On Windows the
// narrow argv is in the ANSI code page and cannot carry every filename, so
// the arguments are read again from GetCommandLineW; elsewhere argv is used.
std::vector<std::string> utf8_arguments(int argc, char** argv);

// Options start with "--"; "--" alone ends them, so a file named "--x" can be
// opened as "-- --x". Only the first operand is kept.
CommandLine parse_command_line(std::span<const std::string> arguments);

// A local file:// URI (empty or "localhost" host) as a UTF-8 path, with
// percent escapes decoded. Anything else is returned unchanged.
std::string path_from_file_uri(const std::string& argument);
} // namespace paint
