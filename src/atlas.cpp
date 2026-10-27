#include "atlas.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
namespace paint {
bool icon_source_matches(const IconFrame& frame) {
    if (!frame.source || (*frame.source).payload.empty()) {
        return false;
    }
    const IconSource& source = *frame.source;
    return frame.image.width == source.image.width && frame.image.height == source.image.height &&
           std::equal(frame.image.pixels.begin(), frame.image.pixels.end(), source.image.pixels.begin(),
                      source.image.pixels.end(), equal) &&
           std::equal(frame.xor_pixels.begin(), frame.xor_pixels.end(), source.xor_pixels.begin(),
                      source.xor_pixels.end(), equal);
}
IconFormat icon_output_format(const IconFrame& frame, ContainerKind kind) {
    if (icon_source_matches(frame)) {
        return (*frame.source).format;
    }
    bool has_xor = false;
    for (std::size_t index = 0; index < frame.image.pixels.size(); ++index) {
        if (legacy_xor_pixel(frame, index)) {
            has_xor = true;
            break;
        }
    }
    const bool png =
        kind == ContainerKind::Icon && frame.image.width == 256 && frame.image.height == 256 && !has_xor;
    return {png ? IconStorage::Png : IconStorage::Dib, 32, 0};
}
IconSizePlan plan_icon_sizes(const std::vector<IconFrame>& existing, const std::vector<int>& sizes,
                             IconSizePolicy policy) {
    if (sizes.empty()) {
        throw std::runtime_error("Choose at least one icon size.");
    }
    std::vector<int> targets;
    for (int size : sizes) {
        if (size < 1 || size > 256) {
            throw std::runtime_error("Icon sizes must be between 1 and 256 pixels.");
        }
        if (std::find(targets.begin(), targets.end(), size) == targets.end()) {
            targets.push_back(size);
        }
    }
    IconSizePlan plan;
    std::vector<int> present;
    for (std::size_t index = 0; index < existing.size(); ++index) {
        const Image& image = existing[index].image;
        const bool selected = image.width == image.height &&
                              std::find(targets.begin(), targets.end(), image.width) != targets.end();
        if (selected) {
            present.push_back(image.width);
        }
        if (!selected && policy == IconSizePolicy::ExactProfile) {
            ++plan.removed;
            continue;
        }
        const bool generate = selected && policy == IconSizePolicy::ReplaceSelected;
        plan.steps.push_back({static_cast<int>(index), image.width, generate});
        if (generate) {
            ++plan.replaced;
        } else {
            ++plan.kept;
        }
    }
    for (int size : targets) {
        if (std::find(present.begin(), present.end(), size) == present.end()) {
            plan.steps.push_back({-1, size, true});
            ++plan.added;
        }
    }
    if (plan.steps.size() > 256) {
        throw std::runtime_error("ICO/CUR must contain between 1 and 256 images.");
    }
    return plan;
}
void AtlasGrid::validate(const Image& sheet) const {
    if (columns < 1 || rows < 1 || columns > 4096 || rows > 4096 || columns * rows > 4096 || margin_x < 0 ||
        margin_y < 0 || spacing_x < 0 || spacing_y < 0 || margin_x > sheet.width / 2 ||
        margin_y > sheet.height / 2 || spacing_x > sheet.width || spacing_y > sheet.height) {
        throw std::runtime_error("Choose a grid with 1 to 4096 sprites and nonnegative margins and spacing.");
    }
    if (sheet.width - 2 * margin_x - (columns - 1) * spacing_x < columns ||
        sheet.height - 2 * margin_y - (rows - 1) * spacing_y < rows) {
        throw std::runtime_error("The rows, columns, margins and spacing do not fit this image.");
    }
}
Rect AtlasGrid::frame(const Image& sheet, int index) const {
    validate(sheet);
    if (index < 0 || index >= columns * rows) {
        throw std::runtime_error("Sprite index is outside the grid.");
    }
    int width = (sheet.width - 2 * margin_x - (columns - 1) * spacing_x) / columns;
    int height = (sheet.height - 2 * margin_y - (rows - 1) * spacing_y) / rows;
    return {margin_x + (index % columns) * (width + spacing_x),
            margin_y + (index / columns) * (height + spacing_y), width, height};
}
int AtlasState::count() const {
    return kind == AtlasKind::Sheet ? grid.columns * grid.rows : static_cast<int>(icons.size());
}
Image AtlasState::frame_image(int index) const {
    if (index < 0 || index >= count()) {
        throw std::runtime_error("Atlas frame is unavailable.");
    }
    if (kind == AtlasKind::Sheet) {
        return cropped(sheet, grid.frame(sheet, index));
    }
    return icons[index].image;
}
std::size_t AtlasState::bytes() const {
    std::size_t result = sheet.pixels.size() * sizeof(Color);
    for (std::size_t i = 0; i < icons.size(); ++i) {
        result += (icons[i].image.pixels.size() + icons[i].xor_pixels.size()) * sizeof(Color);
        if (icons[i].source) {
            const IconSource& source = *icons[i].source;
            result += source.payload.size() +
                      (source.image.pixels.size() + source.xor_pixels.size() + source.palette.size()) *
                          sizeof(Color) +
                      source.indices.size() * sizeof(unsigned);
        }
    }
    return result;
}
bool sprite_sheet_filename(const std::string& filename) {
    std::u8string stem =
        std::filesystem::path(std::u8string(filename.begin(), filename.end())).stem().u8string();
    std::string name;
    for (std::size_t i = 0; i < stem.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(stem[i]);
        name += std::isalnum(c) ? static_cast<char>(std::tolower(c)) : ' ';
    }
    name = " " + name + " ";
    const char* tokens[] = {"sprite", "sprites", "spritesheet", "spritesheets", "spritemap", "sheet",
                            "atlas",  "tileset", "tilemap",     "strip",        "animation", "anim",
                            "idle",   "walk",    "run",         "attack"};
    for (std::size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); ++i) {
        if (name.find(std::string(" ") + tokens[i] + " ") != std::string::npos) {
            return true;
        }
    }
    return false;
}
bool legacy_xor_pixel(const IconFrame& frame, std::size_t index) {
    return index < frame.xor_pixels.size() && frame.xor_pixels[index].a &&
           equal(frame.image.pixels[index], {255, 255, 255, 0});
}
} // namespace paint
