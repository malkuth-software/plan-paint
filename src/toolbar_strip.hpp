#pragma once
#include "atlas.hpp"
#include <atomic>
namespace paint {
struct ToolbarStripOptions {
    int width = 32, height = 32, columns = 1, gap_x = 0, gap_y = 0;
    bool fit = false, opaque = false;
    Color matte = {255, 255, 255, 255};
};
struct ToolbarStripCell {
    int index = 0;
    Rect image;
    bool upscaled = false;
};
struct ToolbarStripPlan {
    int width = 0, height = 0;
    std::vector<ToolbarStripCell> cells;
};
// One-based comma-separated ordinals; repeats deliberately duplicate cells.
std::vector<int> parse_strip_order(const std::string& text, int count);
ToolbarStripPlan plan_toolbar_strip(const ImageContainer& source, const std::vector<int>& order,
                                    const ToolbarStripOptions& options);
Image render_toolbar_strip(const ImageContainer& source, const std::vector<int>& order,
                           const ToolbarStripOptions& options, const std::atomic<bool>* cancel = nullptr);
} // namespace paint
