#pragma once
// Paper: a seeded, deterministic painter of paper for panels, pages and help sheets.
// Machine-made printing paper is a dense mat of tiny fibres seen flat: where more of
// them overlap the sheet is denser and whiter, where fewer do it shows through greyer,
// and the pulp's uneven settling clouds it faintly at a few millimetres. That mat, a
// per-pixel grain and the formation clouds are the whole of the book, copy and
// recycled presets. Handmade and decorative papers add what a mould and a surface
// leave: a tooth lit from the upper left, long light and dark fibre inclusions, the
// wire and chain lines of laid paper, parchment's blotches. Everything is quiet by
// design: a reader notices the paper only when looking for it, and text stays as
// legible as on a flat fill.
//
// Sizes are in points, so a sheet looks the same at every display scale: ask for
// `pixels_per_point` and the same parameters make the same paper, feature for feature,
// at 1x or 2x. A tile repeats seamlessly in both directions; a sheet is one picture
// of any size with an optional shadow into a gutter and a soft fall-off at its edges.
// A flat, open patch comes out at `colour`, so a caller keeps its palette and draws
// the picture unlit. Generation takes milliseconds: make a tile once per scale and
// keep it. No clock, threads or GUI.Forms here.
#include <cstdint>
#include <vector>

namespace paper {

// An sRGB colour, each channel 0..1.
struct Colour {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
};

struct Parameters {
    // Device pixels per point. Every size below is in points.
    double pixels_per_point = 1.0;

    // The sheet. `colour` is what a flat, open patch comes out as.
    Colour colour{0.957, 0.925, 0.847};

    // Slow variation in tone, as the pulp settled unevenly: a cloud this many points
    // across, darkening and lightening by up to `mottle` of full scale.
    double mottle = 0.025;
    double mottle_size = 56.0;

    // The surface's tooth: bumps this many points across, lit from the upper left,
    // shifting a pixel by up to about `tooth` of full scale. Machine-made paper is
    // calendered smooth and its fibres lie along the machine direction, so its grain
    // runs: `machine_grain` stretches the tooth that many times along x (1 for the
    // even tooth of a handmade sheet).
    double tooth = 0.03;
    double tooth_size = 2.6;
    double machine_grain = 1.0;

    // Per-pixel speckle, a fraction of full scale.
    double grain = 0.012;

    // The sheet's own fibre web, seen flat and unlit: this many tiny fibres per 10,000
    // square points, each about `web_length` points long and a fraction of a point
    // wide, laid over one another at random (or along the machine direction, the more
    // `machine_grain` says). Where more fibres overlap the sheet is denser and whiter
    // by up to `web_contrast` of full scale; where fewer do, it shows through greyer.
    // This is what machine-made printing paper is: a mat, not a relief.
    double web = 0.0;
    double web_length = 5.0;
    double web_contrast = 0.04;

    // Fibres on the surface: this many per 10,000 square points, each about
    // `fibre_length` points long (half to twice that), half of them lighter than the
    // sheet and half darker, each by up to `fibre_contrast` of full scale.
    double fibres = 25.0;
    double fibre_length = 12.0;
    double fibre_contrast = 0.07;

    // Flecks: the specks of unbleached fibre and ink in recycled stock, this many per
    // 10,000 square points, each under a point across and darker by up to
    // `fleck_contrast` of full scale.
    double flecks = 0.0;
    double fleck_contrast = 0.12;

    // Laid paper: fine wire lines this many points apart across the sheet, and wider
    // chain lines this many points apart down it. 0 for none (wove paper).
    double laid_spacing = 0.0;
    double chain_spacing = 0.0;
    double laid_depth = 0.02;

    // Broad uneven darkening in patches this many points across (parchment).
    double blotch = 0.0;
    double blotch_size = 120.0;

    // How much darkening warms the colour: 0 darkens in grey, 1 keeps the reds and
    // takes from the blues, as pigment in paper does.
    double warmth = 1.0;

    std::uint32_t seed = 1;
};

// Starting points. Adjust the fields from there.
enum class Preset {
    book,       // machine-made uncoated book paper: cream, calendered, a fine running grain
    copy,       // bright white office paper: smoother and cooler than book paper
    recycled,   // greyer stock with the flecks of unbleached fibre and old ink
    wove,       // handmade cream wove: even, faintly fibrous
    laid,       // wire and chain lines, as from a mould
    india,      // thin, smooth, slightly grey Bible paper
    parchment,  // warm, mottled and uneven
    rag,        // bright white cotton with visible tooth and fibres
    kraft,      // brown wrapping paper: coarse, long fibres
    night       // dark warm paper for reading at night, with light ink
};
inline constexpr int preset_count = 10;
const char* preset_name(Preset preset);
Parameters preset(Preset preset);

// A picture of paper: straight-alpha sRGB, row-major, width * height * 4 bytes in the
// order red, green, blue, alpha. Alpha is always 255.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

// The largest side generate_tile and generate_sheet accept.
inline constexpr int max_side = 4096;

// A tile that repeats seamlessly in both directions, `width` x `height` pixels. Mottle,
// tooth and laid lines are fitted to whole periods across it, so a requested size may
// be rounded slightly. Returns false, leaving `out` untouched, for a bad size.
[[nodiscard]] bool generate_tile(const Parameters& parameters, int width, int height, Image& out);

// What shades a whole sheet beyond its material: a soft shadow into a gutter at one
// side, as pages fall into a binding, and a faint darkening toward every edge.
enum class Gutter { none, left, right };
struct Shading {
    Gutter gutter = Gutter::none;
    double gutter_width = 48.0;  // points the shadow reaches into the page
    double gutter_depth = 0.10;  // fraction of full scale at the binding
    double edge_width = 0.0;     // points the fall-off reaches in from each edge; 0 for none
    double edge_depth = 0.04;    // fraction of full scale at the very edge
};

// One sheet of paper, `width` x `height` pixels, not repeating, shaded as `shading`
// says. Returns false, leaving `out` untouched, for a bad size.
[[nodiscard]] bool generate_sheet(const Parameters& parameters, int width, int height, const Shading& shading,
                                  Image& out);

} // namespace paper
