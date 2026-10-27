# Color mosaic

Mosaic keeps the rectangular saturation/level plane and offers 96 colors for each of 24 hue positions. RGB uses the existing HSV coordinates over sRGB; OKHSL uses Björn Ottosson's [OKHSL conversion](https://bottosson.github.io/posts/colorpicker/). Every offered value is an actual in-gamut 8-bit RGB color from the selected hue slice. Seven shared neutrals, including black and white, anchor its left boundary. Colors remain independent of alpha.

## Objective

A constant hexagonal grid gives every part of the rectangle equal space, although some regions contain much smaller perceptual differences. A direct OKLab nearest-color partition has the opposite usability problem: the lightness axis can dominate chroma and create long, thin cells on screen.

The picker therefore uses a weighted centroidal Voronoi lattice in screen coordinates. Cell boundaries use Euclidean distance in the rectangle, scaled to its 290:224 aspect ratio, so cells remain compact targets. Perceptual variation controls their density rather than stretching their geometry.

The implementation samples a 65 by 65 grid. At each sample, centered finite differences of OKLab coordinates estimate the local perceptual surface area A = |dLab/ds × dLab/dl|. Let W = A(1 + 2s²) and M = max W over the current hue slice. The density is:

    rho(s,l) = (0.08 + W / max(M, 1e-12)) (0.25 + 3s²)

The floor preserves usable cells in compressed near-black and near-white regions. The saturation factors explicitly prioritize saturated choices. These are design weights, not a universal perceptual law. The optimized discrete objective is:

    E = sum_i rho_i min_j |q_i - q_j|²
    q = (290/224 * saturation, level)

Deterministic farthest-point seeds undergo 32 weighted centroid updates. Each update projects to a sample in that cell, rejects duplicate RGB values, and keeps the neutral anchors fixed. This produces a mostly hexagonal nucleated lattice, with smaller cells toward saturation and larger neutral cells. Boundary cells and changes in density naturally produce other polygon shapes. The procedure finds a deterministic local solution; it does not claim a globally optimal palette or perfect hexagons.

## Picking and rendering

Rendering and pointer input use the same cell lookup. Faint outlines distinguish neighboring cells; a stronger outline identifies the selected cell. Each hue slice is cached on first use. Switching hue changes the available 96-color slice, rather than reserving 96 colors for the whole spectrum. Numeric channels and alpha remain editable outside this discrete picking mode.

The [iWantHue theory discussion](https://medialab.github.io/iwanthue/theory/) provides useful background on color-space distance and palette clustering. Rainstar's lattice implementation is independent. Tests check all 48 hue/space slices for deterministic output, 96 distinct RGB values, selectable sites, exact slice membership, black/white preservation, and a larger allocation to saturation than to near-neutrals. Native inspection checks the actual cell geometry and hit behavior.
