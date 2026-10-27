#include "idle_render.hpp"
#include "imgui_internal.h"
#include <algorithm>
#include <cstring>

namespace paint {
static bool equal_pair(ImVec2 a, ImVec2 b) {
    return a.x == b.x && a.y == b.y;
}
static bool equal_clip(ImVec4 a, ImVec4 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
bool IdleRender::changed(const ImDrawData& data, std::uint64_t texture_generation) {
    if (!valid_ || texture_generation_ != texture_generation || !equal_pair(position_, data.DisplayPos) ||
        !equal_pair(size_, data.DisplaySize) || !equal_pair(scale_, data.FramebufferScale) ||
        lists_.size() != static_cast<std::size_t>(data.CmdListsCount)) {
        return true;
    }
    for (int i = 0; i < data.CmdListsCount; ++i) {
        const ImDrawList& current = *data.CmdLists[i];
        const DrawListSnapshot& previous = lists_[i];
        if (previous.vertices.size() != static_cast<std::size_t>(current.VtxBuffer.Size) ||
            previous.indices.size() != static_cast<std::size_t>(current.IdxBuffer.Size) ||
            previous.commands.size() != static_cast<std::size_t>(current.CmdBuffer.Size)) {
            return true;
        }
        if ((!previous.vertices.empty() && std::memcmp(previous.vertices.data(), current.VtxBuffer.Data,
                                                       previous.vertices.size() * sizeof(ImDrawVert)) != 0) ||
            (!previous.indices.empty() && std::memcmp(previous.indices.data(), current.IdxBuffer.Data,
                                                      previous.indices.size() * sizeof(ImDrawIdx)) != 0)) {
            return true;
        }
        for (int j = 0; j < current.CmdBuffer.Size; ++j) {
            const ImDrawCmd& a = previous.commands[j];
            const ImDrawCmd& b = current.CmdBuffer[j];
            // Arbitrary callbacks may change output without changing vertices.
            if (b.UserCallback || a.UserCallback || a.TextureId != b.TextureId ||
                a.VtxOffset != b.VtxOffset || a.IdxOffset != b.IdxOffset || a.ElemCount != b.ElemCount ||
                !equal_clip(a.ClipRect, b.ClipRect)) {
                return true;
            }
        }
    }
    return false;
}
void IdleRender::submitted(const ImDrawData& data, std::uint64_t texture_generation) {
    lists_.resize(data.CmdListsCount);
    for (int i = 0; i < data.CmdListsCount; ++i) {
        const ImDrawList& current = *data.CmdLists[i];
        DrawListSnapshot& previous = lists_[i];
        previous.vertices.assign(current.VtxBuffer.begin(), current.VtxBuffer.end());
        previous.indices.assign(current.IdxBuffer.begin(), current.IdxBuffer.end());
        previous.commands.assign(current.CmdBuffer.begin(), current.CmdBuffer.end());
    }
    position_ = data.DisplayPos;
    size_ = data.DisplaySize;
    scale_ = data.FramebufferScale;
    texture_generation_ = texture_generation;
    valid_ = true;
}
int IdleRender::wait_timeout(bool input_held, bool screenshot) const {
    // An SDL event interrupts this wait immediately. Four timer ticks per second
    // preserve tooltip delays and caret blinking without continuous GPU submission.
    // ImGui may spread a burst of press/release events across several frames.
    bool queued_input = (*ImGui::GetCurrentContext()).InputEventsQueue.Size > 0;
    Uint64 interval = input_held || screenshot || queued_input || settling_frames_ > 0 ? 16 : 250;
    Uint64 elapsed = SDL_GetTicks() - previous_frame_;
    return elapsed >= interval ? 0 : static_cast<int>(interval - elapsed);
}
void IdleRender::activity() {
    settling_frames_ = 3;
}
void IdleRender::framed() {
    previous_frame_ = SDL_GetTicks();
    settling_frames_ = std::max(0, settling_frames_ - 1);
}
void wake_event_loop() {
    SDL_Event event{};
    event.type = SDL_EVENT_USER;
    SDL_PushEvent(&event);
}
} // namespace paint
