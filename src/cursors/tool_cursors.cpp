#include "cursors/tool_cursors.hpp"
#include "codecs.hpp"
#include "cursors/tool_sheet.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace paint::forms {
namespace {
// Sheet cells are authored in Plan Paint. Explicit tool keys prevent a new
// enum member from silently selecting another tool's artwork. The nib or
// contact location is stored once and scaled by the toolkit on every platform.
const std::array<Point, 16> hotspots = {{{3, 3},
                                         {3, 3},
                                         {10, 24},
                                         {3, 3},
                                         {16, 16},
                                         {11, 20},
                                         {8, 26},
                                         {16, 17},
                                         {10, 25},
                                         {3, 3},
                                         {3, 3},
                                         {3, 3},
                                         {3, 3},
                                         {3, 3},
                                         {3, 3},
                                         {3, 3}}};
const std::array<Tool, 16> cursor_tools = {Tool::Select,  Tool::Lasso,  Tool::Pencil,   Tool::Fill,
                                           Tool::Text,    Tool::Eraser, Tool::Picker,   Tool::Magnifier,
                                           Tool::Brush,   Tool::Shape,  Tool::Path,     Tool::Stamp,
                                           Tool::Reshape, Tool::Guide,  Tool::Freehand, Tool::Spirograph};
using CursorSet = std::array<gui_forms::CursorImagesPtr, 16>;
CursorSet make_cursors() noexcept {
    CursorSet result;
    try {
        const Image sheet = decode_image(cursor_sheet_png, sizeof(cursor_sheet_png));
        if (sheet.width != 128 || sheet.height != 128) {
            return result;
        }
        for (std::size_t cell = 0; cell < result.size(); ++cell) {
            if (cursor_tools[cell] == Tool::Magnifier || cursor_tools[cell] == Tool::Stamp) {
                // These tools draw their own canvas preview. A transparent native
                // cursor leaves the preview's central pixels unobstructed.
                result[cell] = gui_forms::CursorImages::create(
                    {{1, 1, 1.0, {{0, 0, 0, 0}}}}, 0, 0);
                continue;
            }
            const Image artwork =
                cropped(sheet, {static_cast<int>(cell % 4) * 32, static_cast<int>(cell / 4) * 32, 32, 32});
            Image outlined = artwork;
            // A one-pixel white keyline keeps every black contact edge readable
            // on dark artwork. The editable source stays straight-alpha RGBA.
            for (int y = 0; y < 32; ++y) {
                for (int x = 0; x < 32; ++x) {
                    if (artwork.get(x, y).a != 0) {
                        continue;
                    }
                    bool adjacent = false;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (artwork.contains(x + dx, y + dy) && artwork.get(x + dx, y + dy).a > 0) {
                                adjacent = true;
                            }
                        }
                    }
                    if (adjacent) {
                        outlined.set(x, y, {255, 255, 255, 255});
                    }
                }
            }
            std::vector<gui_forms::CursorImage> variants;
            for (int size : {16, 24, 32, 48, 64}) {
                gui_forms::CursorImage image;
                image.width = size;
                image.height = size;
                image.scale = size / 32.0;
                image.rgba.reserve(static_cast<std::size_t>(size * size));
                for (int y = 0; y < size; ++y) {
                    for (int x = 0; x < size; ++x) {
                        const int hx = static_cast<int>(std::lround(hotspots[cell].x * size / 32.0));
                        const int hy = static_cast<int>(std::lround(hotspots[cell].y * size / 32.0));
                        const int sx = std::clamp(static_cast<int>(hotspots[cell].x) +
                                                      static_cast<int>(std::lround((x - hx) * 32.0 / size)),
                                                  0, 31);
                        const int sy = std::clamp(static_cast<int>(hotspots[cell].y) +
                                                      static_cast<int>(std::lround((y - hy) * 32.0 / size)),
                                                  0, 31);
                        const Color c = outlined.get(sx, sy);
                        image.rgba.push_back({c.r, c.g, c.b, c.a});
                    }
                }
                variants.push_back(std::move(image));
            }
            result[cell] = gui_forms::CursorImages::create(std::move(variants), hotspots[cell].x / 32.0,
                                                           hotspots[cell].y / 32.0);
        }
    } catch (const std::exception&) {
        // Decoding, allocation, or toolkit validation failure retains a usable
        // stock cursor. No per-pointer-event retries or external asset reads.
        result = {};
    }
    return result;
}
} // namespace
gui_forms::CursorImagesPtr tool_cursor_images(Tool tool) noexcept {
    static const CursorSet cursors = make_cursors();
    for (std::size_t index = 0; index < cursor_tools.size(); ++index) {
        if (cursor_tools[index] == tool) {
            return cursors[index];
        }
    }
    return {};
}
gui_forms::CursorKind tool_cursor_fallback(Tool tool) noexcept {
    return tool == Tool::Text ? gui_forms::CursorKind::text : gui_forms::CursorKind::crosshair;
}
void apply_tool_cursor(gui_forms::Control& control, Tool tool) {
    control.set_custom_cursor(tool_cursor_images(tool), tool_cursor_fallback(tool));
}
} // namespace paint::forms
