#pragma once
#include "atlas.hpp"
#include <atomic>
namespace paint {
// Independent recipes over unpremultiplied encoded RGB. Generation owns its output
// pixels and never edits the source container. RGBA output retains hidden RGB.
enum class IconStatePreset { Normal, Hot, Disabled };
// Indexed DIB retains 1/4/8-bit targets; a 2-bit PNG source uses a 4-bit DIB target.
enum class IconStatePalette { Rgba, IndexedDib };
struct IconStateRecipe {
    IconStatePreset preset = IconStatePreset::Disabled;
    double saturation = 0, brightness = 0, opacity = 55;
    IconStatePalette palette = IconStatePalette::Rgba;
};
IconStateRecipe icon_state_preset(IconStatePreset preset);
std::string icon_state_name(IconStatePreset preset);
enum class IconStateStatus { Ready, Failed, Cancelled };
struct IconStateEntry {
    int source_index = 0, output_index = -1;
    IconFormat source_format, output_format;
    IconStateStatus status = IconStateStatus::Cancelled;
    std::size_t partial_alpha_converted = 0;
    std::string diagnostic;
};
struct IconStateResult {
    IconStateRecipe recipe;
    ImageContainer output;
    std::vector<IconStateEntry> entries;
};
IconStateResult generate_icon_state(const ImageContainer& source, const std::vector<int>& selection,
                                    const IconStateRecipe& recipe, const std::atomic<bool>* cancel = nullptr);
bool icon_state_ready(const IconStateResult& result);
std::string icon_state_report(const IconStateResult& result);
} // namespace paint
