#include "cursors/tool_cursors.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace
int main() {
    try {
        for (int i = 0; i <= static_cast<int>(paint::Tool::Spirograph); ++i) {
            const gui_forms::CursorImagesPtr images =
                paint::forms::tool_cursor_images(static_cast<paint::Tool>(i));
            const bool preview_tool = i == static_cast<int>(paint::Tool::Magnifier) ||
                                      i == static_cast<int>(paint::Tool::Stamp);
            require(images && (*images).images().size() == (preview_tool ? 1 : 5),
                    "tool has no appropriate cursor representation");
            for (double scale : {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0}) {
                const gui_forms::CursorImage raster = (*images).rasterize(scale);
                if (preview_tool) {
                    for (const gui_forms::Color pixel : raster.rgba) {
                        require(pixel.alpha == 0, "native cursor obscures the tool preview");
                    }
                    continue;
                }
                const int x = (*images).hotspot_x(raster), y = (*images).hotspot_y(raster);
                const gui_forms::Color contact = raster.rgba[static_cast<std::size_t>(y) * raster.width + x];
                if (!(contact.alpha == 255 && contact.red < 80 && contact.green < 80 && contact.blue < 80)) {
                    std::cerr << "Tool " << i << " at " << scale << " contact " << x << "," << y << '\n';
                    throw std::runtime_error("hotspot must land on the visible dark contact point");
                }
                require(raster.width == static_cast<int>(32 * scale), "logical cursor size changed");
            }
        }
        require(!paint::forms::tool_cursor_images(static_cast<paint::Tool>(999)),
                "unknown tool must use stock fallback");
        require(paint::forms::tool_cursor_fallback(paint::Tool::Text) == gui_forms::CursorKind::text,
                "text fallback is not an I-beam");
        std::cout << "14 tool contacts stay aligned; magnifier and stamp previews stay unobstructed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
