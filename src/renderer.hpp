#pragma once
#include <SDL3/SDL.h>
#include <imgui.h>

namespace paint {
void configure_interface_style();
// Draw ImGui's logical window coordinates into the current physical framebuffer.
void render_interface(SDL_Renderer* renderer, ImDrawData* draw_data);
} // namespace paint
