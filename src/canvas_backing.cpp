#include "canvas_backing.hpp"
#include "carpet.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <stdexcept>
namespace paint {
namespace {
std::uint32_t next_noise(std::uint32_t& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}
std::uint8_t channel(double value) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

} // namespace
const char* canvas_backing_name(CanvasBacking backing) {
    const char* names[] = {"Original grey-blue", "Billiard green",  "Fine wool felt", "Ivory felt",
                           "Slate felt",         "Terracotta felt", "Royal velvet",   "Brushed sapphire",
                           "Oxblood velvet",     "Emerald velvet",  "Plum velvet",    "Charcoal velvet",
                           "Moss felt"};
    const int index = static_cast<int>(backing);
    return index >= 0 && index < canvas_backing_count ? names[index] : names[0];
}
double canvas_backing_tile_size(CanvasBacking backing) {
    return backing == CanvasBacking::PaleFelt ? 128 : 256;
}
namespace {
// The carpet preset of each material after the pale felt (index - 1 is
// unused for the pale felt, which is drawn by hand).
constexpr int backing_presets[] = {0, 3, 4, 11, 5, 10, 0, 1, 2, 6, 7, 8, 9};
static_assert(std::size(backing_presets) == static_cast<std::size_t>(canvas_backing_count));
} // namespace
Color canvas_backing_color(CanvasBacking backing) {
    const int index = static_cast<int>(backing);
    if (index < 0 || index >= canvas_backing_count) {
        throw std::invalid_argument("Unknown canvas backing.");
    }
    if (backing == CanvasBacking::PaleFelt) {
        const Color felt{208, 218, 226, 255};
        return felt;
    }
    const Color dye = carpet_preset(backing_presets[index]).color;
    return dye;
}
Image canvas_backing_texture(CanvasBacking backing, const std::atomic<bool>* cancel) {
    const int index = static_cast<int>(backing);
    if (index < 0 || index >= canvas_backing_count) {
        throw std::invalid_argument("Unknown canvas backing.");
    }
    Image image;
    std::uint32_t random = 0x5241494eU;
    if (backing == CanvasBacking::PaleFelt) {
        // Preserve the original screen-space tile byte for byte.
        image.reset(128, 128, {});
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                const int fiber =
                    static_cast<int>((next_noise(random) >> 24) % 7) - 3 + ((x + 3 * y) % 17 == 0 ? 1 : 0);
                image.set(x, y, {channel(208 + fiber), channel(218 + fiber), channel(226 + fiber), 255});
            }
        }
        return image;
    }
    // Cache one generated tile only. Background changes never retain a gallery
    // of fiber buffers; all rendering scratch storage is freed on completion.
    static std::mutex mutex;
    static CanvasBacking cached = CanvasBacking::Count;
    static Image tile;
    const std::lock_guard<std::mutex> guard(mutex);
    if (cached != backing) {
        tile = render_carpet_tile(carpet_preset(backing_presets[index]), 512, cancel);
        cached = backing;
    }
    return tile;
}
} // namespace paint
