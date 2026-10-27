#pragma once
#include <cstdint>
namespace paint::test {
// Independently assembled 1-bit 2x2 CUR with black/white AND/XOR pixels.
inline constexpr std::uint8_t legacy_cursor[] = {
    0, 0, 2, 0, 1, 0, 2, 2, 2,   0,   1,   0, 0,    0,  64, 0, 0,    0, 22, 0, 0,    0, 40, 0, 0, 0, 2, 0, 0,
    0, 4, 0, 0, 0, 1, 0, 1, 0,   0,   0,   0, 0,    16, 0,  0, 0,    0, 0,  0, 0,    0, 0,  0, 0, 2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 0, 0x40, 0,  0,  0, 0x40, 0, 0,  0, 0xc0, 0, 0,  0, 0, 0, 0, 0};
// Independently assembled indexed PNG in ICO: 2 bpp, three palette colors, partial alpha.
inline constexpr std::uint8_t indexed_icon[] = {
    0,   0,  1,  0,   1,   0,   2,   2,  3,   0,   1,   0,   2,   0,   105, 0,   0,   0,  22,
    0,   0,  0,  137, 80,  78,  71,  13, 10,  26,  10,  0,   0,   0,   13,  73,  72,  68, 82,
    0,   0,  0,  2,   0,   0,   0,   2,  2,   3,   0,   0,   0,   15,  216, 229, 183, 0,  0,
    0,   9,  80, 76,  84,  69,  240, 30, 50,  20,  200, 60,  50,  80,  230, 108, 186, 50, 81,
    0,   0,  0,  3,   116, 82,  78,  83, 255, 128, 0,   127, 109, 104, 120, 0,   0,   0,  12,
    73,  68, 65, 84,  120, 156, 99,  16, 96,  104, 0,   0,   0,   180, 0,   145, 0,   10, 216,
    179, 0,  0,  0,   0,   73,  69,  78, 68,  174, 66,  96,  130,
};
} // namespace paint::test
