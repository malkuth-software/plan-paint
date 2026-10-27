#include "icon_palette.hpp"
#include "safe_file.hpp"
#include <locale>
#include <sstream>
#include <stdexcept>
namespace paint {
std::vector<Color> decode_icon_palette(const std::string& text) {
    if (text.size() > 65536 || text.find('\0') != std::string::npos) {
        throw std::runtime_error("Palette file exceeds 64 KiB or contains invalid text.");
    }
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    std::string line;
    std::getline(input, line);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line != "GIMP Palette") {
        throw std::runtime_error("Choose a GIMP RGB palette (.gpl).");
    }
    std::vector<Color> colors;
    bool header = true, named = false, columns = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (header && !named && line.starts_with("Name: ")) {
            named = true;
            continue;
        }
        if (header && named && !columns && line.starts_with("Columns: ")) {
            std::istringstream value(line.substr(9));
            value.imbue(std::locale::classic());
            int count = -1;
            if (!(value >> count) || count < 0 || count > 255 || !(value >> std::ws).eof()) {
                throw std::runtime_error("Invalid palette column count.");
            }
            columns = true;
            continue;
        }
        header = false;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream color(line);
        color.imbue(std::locale::classic());
        int r = -1, g = -1, b = -1;
        if (!(color >> r >> g >> b) || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 ||
            (color.peek() != std::char_traits<char>::eof() && color.peek() != ' ' && color.peek() != '\t')) {
            throw std::runtime_error("Invalid palette RGB entry; expected three integers from 0 to 255.");
        }
        colors.push_back(
            {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), 255});
        if (colors.size() > 256) {
            throw std::runtime_error("Icon palettes support at most 256 entries.");
        }
    }
    if (colors.empty()) {
        throw std::runtime_error("The palette has no colors.");
    }
    return colors;
}
std::string encode_icon_palette(const std::vector<Color>& colors) {
    if (colors.empty() || colors.size() > 256) {
        throw std::runtime_error("Choose a palette with 1 to 256 entries.");
    }
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << "GIMP Palette\nName: Plan Paint icon palette\nColumns: 16\n# Ordered RGB entries\n";
    for (std::size_t index = 0; index < colors.size(); ++index) {
        const Color color = colors[index];
        if (color.a != 255) {
            throw std::runtime_error("Palette entries require opaque RGB colors.");
        }
        text << unsigned(color.r) << ' ' << unsigned(color.g) << ' ' << unsigned(color.b) << "\tIndex "
             << index << '\n';
    }
    return text.str();
}
std::vector<Color> load_icon_palette(const std::string& path) {
    const std::vector<std::uint8_t> bytes =
        read_regular_file_bounded(path, 65536, "Could not read the palette file.");
    return decode_icon_palette(std::string(bytes.begin(), bytes.end()));
}
} // namespace paint
