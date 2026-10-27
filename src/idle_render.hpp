#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <imgui.h>
#include <vector>

namespace paint {
struct DrawListSnapshot {
    std::vector<ImDrawVert> vertices;
    std::vector<ImDrawIdx> indices;
    std::vector<ImDrawCmd> commands;
};
// Retain the last submitted frame. Texture contents have a separate generation:
// identical image vertices can refer to a texture whose pixels have changed.
class IdleRender {
  public:
    bool changed(const ImDrawData& data, std::uint64_t texture_generation);
    void submitted(const ImDrawData& data, std::uint64_t texture_generation);
    int wait_timeout(bool input_held, bool screenshot) const;
    void activity();
    void framed();

  private:
    std::vector<DrawListSnapshot> lists_;
    ImVec2 position_, size_, scale_;
    std::uint64_t texture_generation_ = 0;
    bool valid_ = false;
    int settling_frames_ = 3;
    Uint64 previous_frame_ = 0;
};
// Thread-safe SDL queue notification. The result mailbox remains its owner's data.
void wake_event_loop();
} // namespace paint
