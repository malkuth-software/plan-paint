# Carpet textures

Carpet generates fabric from explicit fibers in native C++ and paints the finished
texture with a circular brush. Choose **Brushes → Carpet generator…**. After
accepting a texture, use **Carpet settings…** in the tool panel to change it.
The generator renders on a cancellable worker; a stroke samples the cached tile.

The ten sliders control velvet-to-felt structure, fiber diameter, pile/sheet
depth, nap disorder, fiber crimp, optical roughness, dye concentration, light
direction, view tilt and magnification. Dye color accepts hex RGB. **New fibers**
changes the deterministic seed. **Hillshade relief** adds surface relief to the
colored optical result. Accept commits the finished texture; Cancel or Escape
leaves the previous texture and brush family intact.

Diameter is shown in micrometers, depth in millimeters, angles in degrees and
magnification relative to the original renderer's default framing. The geometry
uses millimeters internally. These are model parameters, not calibrated textile
measurements. Roughness also adjusts the azimuthal lobe width; presets retain
independently tuned longitudinal and azimuthal roughness until that slider moves.

Twelve presets are available: Royal velvet, Brushed sapphire, Oxblood velvet,
Billiard green, Fine wool felt, Slate felt, Emerald velvet, Plum velvet, Charcoal
velvet, Moss felt, Terracotta felt and Ivory felt. The first six preserve the
Fabric Lab HTML renderer's preset parameters. The remaining six are additional
color/seed variations. Manual edits are shown as Current settings.

The same twelve presets replace the older generated canvas backings in Settings.
The original grey-blue backing remains byte-for-byte unchanged. Backgrounds are
screen-space decoration; they do not alter or export with the document. Carpet
brush strokes are document pixels and export normally. Generator dye and lighting
determine their color, while brush size and active ink alpha control deposition.
Revisiting a pixel within one gesture preserves a single coat; another gesture
can add another coat. Texture coordinates remain anchored to the document. Per-gesture
coverage bookkeeping, including hash-table capacity, is released at gesture end.

## Native renderer

`src/carpet.cpp` ports the final Fabric Lab HTML fiber renderer. It includes
curved pile and web fibers, analytic ray/capsule intersections, directional
shadowing, Fresnel reflection, dye absorption, and normalized R, TT and TRT
scattering lobes. The external scattering approximation and fiber-count limits
are retained; this is not a converged path tracer or a calibrated material model.

Segments stream directly into the camera and light rasters instead of retaining
millions of geometry objects. Shading accumulates one output row at a time,
reuses common lobe calculations and uses double precision without fast-math.
Geometry and shading scratch buffers are released when a render finishes.
Only the current brush tile and one background tile are cached. A 512-square
RGBA tile occupies 1 MiB before GUI upload copies; temporary ray-hit rasters
are larger and exist during generation only.

The native brush and backing use 512-square tiles with 2× supersampling. A
32-pixel overlap blends opposite edges to avoid a hard repeat boundary. This
small tiling adaptation is separate from the original optical renderer.
The core renderer accepts output dimensions from 16 through 1024; the tile
wrapper reserves its overlap within that limit. Invalid parameters are rejected.

## Hillshade

For a north-up 3×3 height neighborhood `a b c / d e f / g h i`, Horn derivatives are

```
p = ((c + 2*f + i) - (a + 2*d + g)) / (8 * cell_width)
q = ((g + 2*h + i) - (a + 2*b + c)) / (8 * cell_height)
```

Rows run southward. The unit normal is proportional to `(-p, +q, 1)` when the
world y axis points north. The clamped dot product with the light vector is
algebraically equivalent to the slope/aspect hillshade expression. Azimuth is
clockwise from north; altitude is measured above the horizontal plane.
See [Esri's description of Horn hillshade](https://pro.arcgis.com/en/pro-app/3.5/tool-reference/3d-analyst/how-hillshade-works.htm).

The optional fabric relief uses this neighborhood on the visible surface raster,
transforms the light into the same camera frame and retains the optical color.
Its multiplier is `0.30 + 0.70 * shade`; fixed light altitude is 50 degrees.
It needs only the adjacent three rows already present in the camera raster,
with no stored slope or aspect image and no per-pixel trigonometric conversion.
Borders clamp to valid cells; uncovered neighbors use the current surface height.
This is a local relief effect on the visible fabric, not a terrain-shadow solver.

## Verification

`paint-carpet-tests` checks analytic flat and sloped Horn surfaces, deterministic
seeds, every exposed render control, cancellation, invalid inputs and brush
coverage under different mouse-event subdivisions. `paint-forms-tests` exercises
the actual native controls, render supersession, invalid dye text, acceptance,
cancellation and stroke undo. Existing core tests cover all backing selections,
opacity and preference round trips, including the original grey-blue checksum.

Archived state-tracker, momentum and sparse-reconstruction experiments live in
`experiments/lagging-tools/`; they are excluded from production targets. The
conventional distance-lag stabilizer remains available.

## Optimization in 0.3.4

The rasterizer rejects off-screen segments before computing their intersections.
For each covered sample, the top of the capsule's endpoint-sphere hull bounds
its possible depth. A nearer existing hit rejects the sample before quadratic
and sphere intersection work. Separate camera and shadow raster specializations
omit normals and material writes from the depth-only shadow pass. Fiber seeds,
counts, geometry, optical equations and supersampling remain unchanged.

On an Apple Silicon Mac, three alternating before/after runs of the initial
native candidate and optimized renderer gave these median seconds for a
512-square seamless tile (including its overlap and 2× supersampling):

| Fabric | Before | Optimized | Time reduction |
| --- | ---: | ---: | ---: |
| Royal velvet | 0.737 | 0.632 | 14.2% |
| Royal velvet + hillshade | 0.740 | 0.628 | 15.2% |
| Billiard green | 1.340 | 1.017 | 24.1% |
| Billiard green + hillshade | 1.341 | 1.021 | 23.9% |
| Fine wool felt | 1.149 | 0.908 | 21.0% |
| Fine wool felt + hillshade | 1.149 | 0.912 | 20.6% |

These are local CPU render timings, not cross-platform guarantees or a comparison
with the browser renderer. Six full-size comparisons and 36 additional cases
covering all twelve presets, alternate seeds, maximum tilt, crimp and
magnification were byte-identical before and after the change. The additional
cases use 128-square output. The production tests separately exercise controls,
invalid inputs, cancellation and painting behavior.

Enable `RAINSTAR_BENCHMARKS` in CMake and run `paint-carpet-benchmark` to repeat
the six current-renderer timing/checksum cases. An optional directory argument
also writes raw RGBA tiles for exact comparison; it does not change rendering.
