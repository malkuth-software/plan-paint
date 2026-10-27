#pragma once
#include "image.hpp"
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
Image canvas_backing_texture(CanvasBacking backing);
double canvas_backing_tile_size(CanvasBacking backing);
} // namespace paint
