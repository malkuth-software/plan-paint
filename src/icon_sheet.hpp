#pragma once
#include "atlas.hpp"
#include <atomic>
namespace paint {
enum class IconSheetBackground { Light, Dark, Checker, Transparent, Custom };
struct IconSheetOptions {
    int columns = 4, padding = 12, gap = 16, label_size = 12;
    bool labels = true;
    IconSheetBackground background = IconSheetBackground::Light;
    Color custom = {80, 100, 140, 255};
};
struct IconSheetCell {
    int source_index = 0;
    Rect image;
    Point label;
    std::string text;
};
struct IconSheetLayout {
    int width = 0, height = 0;
    std::vector<IconSheetCell> cells;
};
// Selection order is output order. Original ordinals distinguish same-size representations.
// No resampling. The complete layout is checked before allocating output pixels.
IconSheetLayout plan_icon_sheet(const ImageContainer& source, const std::vector<int>& selection,
                                const IconSheetOptions& options);
Image render_icon_sheet(const ImageContainer& source, const std::vector<int>& selection,
                        const IconSheetOptions& options, const std::atomic<bool>* cancel = nullptr);
} // namespace paint
