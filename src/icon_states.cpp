#include "icon_states.hpp"
#include "icon_palette.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace paint {
namespace {
void validate_recipe(const IconStateRecipe& recipe) {
    if (!std::isfinite(recipe.saturation) || recipe.saturation < 0 || recipe.saturation > 200 ||
        !std::isfinite(recipe.brightness) || recipe.brightness < -100 || recipe.brightness > 100 ||
        !std::isfinite(recipe.opacity) || recipe.opacity < 0 || recipe.opacity > 100 ||
        (recipe.palette != IconStatePalette::Rgba && recipe.palette != IconStatePalette::IndexedDib)) {
        throw std::runtime_error("Invalid icon state recipe.");
    }
    static_cast<void>(icon_state_name(recipe.preset));
}
std::uint8_t state_byte(double value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
}
std::string quote(const std::string& text) {
    std::string result = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char character : text) {
        if (character == '"' || character == '\\') {
            result += '\\';
            result += static_cast<char>(character);
        } else if (character < 32) {
            result += "\\u00";
            result += hex[character >> 4];
            result += hex[character & 15];
        } else {
            result += static_cast<char>(character);
        }
    }
    return result + '"';
}
} // namespace
IconStateRecipe icon_state_preset(IconStatePreset preset) {
    switch (preset) {
    case IconStatePreset::Normal:
        return {preset, 100, 0, 100, IconStatePalette::Rgba};
    case IconStatePreset::Hot:
        return {preset, 120, 10, 100, IconStatePalette::Rgba};
    case IconStatePreset::Disabled:
        return {preset, 0, 0, 55, IconStatePalette::Rgba};
    }
    throw std::runtime_error("Invalid icon state preset.");
}
std::string icon_state_name(IconStatePreset preset) {
    switch (preset) {
    case IconStatePreset::Normal:
        return "normal";
    case IconStatePreset::Hot:
        return "hot";
    case IconStatePreset::Disabled:
        return "disabled";
    }
    throw std::runtime_error("Invalid icon state preset.");
}
IconStateResult generate_icon_state(const ImageContainer& source, const std::vector<int>& selection,
                                    const IconStateRecipe& recipe, const std::atomic<bool>* cancel) {
    validate_recipe(recipe);
    if ((source.kind != ContainerKind::Icon && source.kind != ContainerKind::Cursor) || selection.empty() ||
        selection.size() > 256) {
        throw std::runtime_error("Choose 1 to 256 icon or cursor representations.");
    }
    IconStateResult result;
    result.recipe = recipe;
    result.output.kind = source.kind;
    const double saturation = recipe.saturation / 100;
    const double brightness = recipe.brightness * 255 / 100;
    const double opacity = recipe.opacity / 100;
    for (int index : selection) {
        if (index < 0 || static_cast<std::size_t>(index) >= source.frames.size()) {
            throw std::runtime_error("A selected representation is unavailable.");
        }
        IconStateEntry entry;
        entry.source_index = index;
        entry.source_format = icon_output_format(source.frames[index], source.kind);
        result.entries.push_back(entry);
    }
    for (IconStateEntry& entry : result.entries) {
        if (cancel && (*cancel).load()) {
            break;
        }
        const IconFrame& original = source.frames[entry.source_index];
        const Image& image = original.image;
        try {
            if (image.width < 1 || image.height < 1 || image.width > 256 || image.height > 256 ||
                image.pixels.size() != static_cast<std::size_t>(image.width) * image.height ||
                original.hotspot_x < 0 || original.hotspot_y < 0 || original.hotspot_x >= image.width ||
                original.hotspot_y >= image.height) {
                throw std::runtime_error("Invalid icon state image dimensions or hotspot.");
            }
            IconFrame transformed;
            transformed.hotspot_x = original.hotspot_x;
            transformed.hotspot_y = original.hotspot_y;
            transformed.image = image;
            for (std::size_t index = 0; index < image.pixels.size(); ++index) {
                if (legacy_xor_pixel(original, index)) {
                    throw std::runtime_error(
                        "State recipes cannot transform background-dependent XOR pixels.");
                }
                const Color color = image.pixels[index];
                if (!color.a) {
                    continue;
                }
                const double gray = 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
                transformed.image.pixels[index] = {
                    state_byte(gray + saturation * (color.r - gray) + brightness),
                    state_byte(gray + saturation * (color.g - gray) + brightness),
                    state_byte(gray + saturation * (color.b - gray) + brightness),
                    state_byte(color.a * opacity)};
            }
            if (recipe.palette == IconStatePalette::IndexedDib &&
                (entry.source_format.bits_per_pixel == 1 || entry.source_format.bits_per_pixel == 2 ||
                 entry.source_format.bits_per_pixel == 4 || entry.source_format.bits_per_pixel == 8)) {
                IconPaletteOptions options;
                options.bits_per_pixel =
                    entry.source_format.bits_per_pixel == 2 ? 4 : entry.source_format.bits_per_pixel;
                IconPaletteResult indexed = convert_icon_palette(transformed, options);
                entry.partial_alpha_converted = indexed.partial_pixels;
                transformed = std::move(indexed.frame);
            }
            if (cancel && (*cancel).load()) {
                break;
            }
            entry.output_format = icon_output_format(transformed, source.kind);
            entry.output_index = static_cast<int>(result.output.frames.size());
            result.output.frames.push_back(std::move(transformed));
            entry.status = IconStateStatus::Ready;
        } catch (const std::exception& error) {
            entry.status = IconStateStatus::Failed;
            entry.diagnostic = error.what();
        }
    }
    return result;
}
bool icon_state_ready(const IconStateResult& result) {
    if (result.entries.empty() || result.output.frames.size() != result.entries.size()) {
        return false;
    }
    for (const IconStateEntry& entry : result.entries) {
        if (entry.status != IconStateStatus::Ready) {
            return false;
        }
    }
    return true;
}
std::string icon_state_report(const IconStateResult& result) {
    const IconStateRecipe& recipe = result.recipe;
    validate_recipe(recipe);
    std::ostringstream report;
    report.imbue(std::locale::classic());
    report << std::setprecision(17)
           << "{\n  \"schemaVersion\": 1,\n  \"recipeVersion\": \"plan-paint-icon-state-v1\",\n"
           << "  \"preset\": " << quote(icon_state_name(recipe.preset))
           << ",\n  \"saturation\": " << recipe.saturation << ",\n  \"brightness\": " << recipe.brightness
           << ",\n  \"opacity\": " << recipe.opacity << ",\n  \"palettePolicy\": "
           << quote(recipe.palette == IconStatePalette::Rgba ? "rgba"
                                                             : "indexed-threshold128-white-no-dither")
           << ",\n  \"kind\": " << quote(result.output.kind == ContainerKind::Cursor ? "cur" : "ico")
           << ",\n  \"ready\": " << (icon_state_ready(result) ? "true" : "false")
           << ",\n  \"representations\": [";
    for (std::size_t i = 0; i < result.entries.size(); ++i) {
        const IconStateEntry& entry = result.entries[i];
        report << (i ? ",\n" : "\n") << "    {\"sourceIndex\":" << entry.source_index
               << ",\"outputIndex\":" << entry.output_index
               << ",\"sourceBpp\":" << entry.source_format.bits_per_pixel
               << ",\"outputBpp\":" << entry.output_format.bits_per_pixel
               << ",\"partialAlphaConverted\":" << entry.partial_alpha_converted << ",\"status\":"
               << quote(entry.status == IconStateStatus::Ready    ? "ready"
                        : entry.status == IconStateStatus::Failed ? "failed"
                                                                  : "cancelled")
               << ",\"diagnostic\":" << quote(entry.diagnostic) << '}';
    }
    report << "\n  ]\n}\n";
    return report.str();
}
} // namespace paint
