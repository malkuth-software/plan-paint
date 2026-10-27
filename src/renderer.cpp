#include "renderer.hpp"
#include "imgui_impl_sdlrenderer3.h"
#include <stdexcept>

namespace paint {
void configure_interface_style() {
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0;
    style.ChildRounding = 0;
    style.FrameRounding = 0;
    style.PopupRounding = 0;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 1;
    style.PopupBorderSize = 1;
    style.ScrollbarSize = 16;
    style.ScrollbarRounding = 0;
    style.GrabRounding = 0;
    style.Colors[ImGuiCol_Text] = ImVec4(0.10f, 0.13f, 0.17f, 1);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.96f, 0.96f, 1);
    style.Colors[ImGuiCol_Button] = ImVec4(0.96f, 0.97f, 0.98f, 1);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.87f, 0.93f, 0.99f, 1);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.75f, 0.86f, 0.96f, 1);
    style.Colors[ImGuiCol_Header] = ImVec4(0.84f, 0.91f, 0.97f, 1);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.87f, 0.93f, 0.99f, 1);
}

void render_interface(SDL_Renderer* renderer, ImDrawData* draw_data) {
    // SDL geometry stays in logical coordinates. Set the renderer scale each frame so
    // its vertices and the backend's clip rectangles agree, including after moving
    // the window between displays. The backend detects this scale and avoids doubling it.
    if (!SDL_SetRenderScale(renderer, (*draw_data).FramebufferScale.x, (*draw_data).FramebufferScale.y)) {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(draw_data, renderer);
}
} // namespace paint
