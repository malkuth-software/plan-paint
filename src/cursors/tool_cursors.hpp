#pragma once
#include "document.hpp"
#include <gui_forms/control.hpp>
namespace paint::forms {
gui_forms::CursorImagesPtr tool_cursor_images(Tool tool) noexcept;
gui_forms::CursorKind tool_cursor_fallback(Tool tool) noexcept;
void apply_tool_cursor(gui_forms::Control& control, Tool tool);
} // namespace paint::forms
