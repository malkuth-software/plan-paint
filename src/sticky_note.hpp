#pragma once
#include "text.hpp"
#include <atomic>
namespace paint {
struct StickyNoteSettings {
    std::string text;
    int width = 256, height = 192, padding = 16, paper = 0;
    std::uint32_t seed = 1;
    Color paper_color{255, 242, 166, 255}, ink_color{48, 41, 28, 255};
    TextStyle font;
};
struct StickyNoteResult {
    Image image;
    bool text_clipped = false;
};
StickyNoteResult render_sticky_note(const StickyNoteSettings& settings,
                                    const std::atomic<bool>* cancel = nullptr);
Image render_sticky_paper(const StickyNoteSettings& settings, const std::atomic<bool>* cancel = nullptr);
} // namespace paint
