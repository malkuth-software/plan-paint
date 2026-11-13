#pragma once
#include "image.hpp"
#include <atomic>
namespace paint {
// Stored preference values: the original two positions remain compatible.
enum class CanvasBacking {
    PaleFelt,
    MossFelt,
    BrownFelt,
    TanFelt,
    MatteSlate,
    MatteClay,
    MatteIvory,
    Sapphire,
    Oxblood,
    Emerald,
    Plum,
    Charcoal,
    Moss,
    Count
};
inline constexpr int canvas_backing_count = static_cast<int>(CanvasBacking::Count);
const char* canvas_backing_name(CanvasBacking backing);
// The material's plain colour, shown while its tile renders.
Color canvas_backing_color(CanvasBacking backing);
// The material's tile: the pale felt's at once, every other material's by a
// fibre render (slow: call it off the UI thread), of which the last is kept.
// A set `cancel` stops that render with std::runtime_error and keeps
// nothing. Thread-safe.
Image canvas_backing_texture(CanvasBacking backing, const std::atomic<bool>* cancel = nullptr);
double canvas_backing_tile_size(CanvasBacking backing);
} // namespace paint
