# Dithering and the dithering brush

Select artwork, open the Selection arrow, and choose **Dither / posterize…**.
Without an active canvas selection the command processes the canvas. A pasted
floating object remains above the canvas and does not restrict this operation.

Choose 2–32 colors and Crosswind, Weave, Scrambled, Drift, or Posterize. The
background job prepares the exact result shown by the preview. OK applies that
prepared image as one undoable edit; Cancel preserves the document. Changing an
option cancels the preceding job. The active selection, its holes, and alpha are
preserved; unselected pixels and fully transparent pixels are left byte-identical.

- Crosswind distributes quantization error to future selected neighbors with
  deterministic varied weights and less transport across source-color changes.
- Weave uses a regular Walsh threshold lattice.
- Scrambled varies local Walsh phases at multiple scales.
- Drift changes the finer weave allocation over larger regions.
- Posterize chooses the nearest palette color without a spatial pattern.

## Color reduction in 1.0

Both palette construction and color assignment use OKLab. Posterize first splits
sampled colors where doing so removes the most squared perceptual error, then
refines the representatives by assigning samples to their nearest center and
recomputing each center. It uses the resulting palette directly, without a pattern.

Dithering fits a different problem: the available colors must also mix well.
For each source color **s**, palette colors **pᵢ** receive nonnegative weights
**wᵢ** summing to one. The mixture minimizes

`|Σ wᵢ pᵢ − s|² + 0.08 Σ wᵢ |pᵢ − s|²`.

The first term preserves the local average in OKLab; the second discourages
visible spread between contributors. Without that second term, a good average
can conceal harsh bright or differently colored speckles. At most four colors
contribute to one pixel's mixture. These are perceptual mixtures, not a claim
of exact linear-light energy reproduction when pixels are optically averaged.

The dither palette alternates this mixture fit with a small least-squares solve
for its colors. It compares two starting palettes: refined representatives with
modestly expanded endpoints, and colors spread across the source's range. The
better sampled objective wins. Each iteration is evaluated after conversion to
actual 8-bit RGB; a worse clipped or rounded iterate is discarded. This improves
coverage of shadows, highlights and small color accents without an external
numerical library or a whole-image decomposition.

Weave, Scrambled and Drift place those mixtures with their existing Walsh ranks.
Each complete constant-mixture 64×64 tile has balanced ranks, with palette counts
within one pixel of the requested proportions. Crosswind diffuses the remaining
error from the representable mixture; an impossible out-of-palette component
cannot accumulate into colored fringes. No mode swaps pixels after placement.

Small existing palettes are preserved exactly when their RGB color count is at
or below the requested count. Otherwise, deterministic jittered strata provide
at most 16384 selected visible observations, weighted by alpha. They avoid the
aliasing of regularly spaced samples. Representative refinement takes at most
32 iterations. Mixture refinement uses at most 4096 of those observations and
12 updates per start; its normal matrix is at most 32×32. The exact-RGB mixture
cache replaces entries within 65536 fixed slots. Crosswind uses two error rows.
Cancellation is checked during sampling, refinement and rendering.

Two or four colors still impose a severe limit on a photograph. Posterization
intentionally produces flat regions; dithering exchanges some fine texture for
more intermediate tone. The allocator is a bounded approximation, not a globally
optimal palette or a guarantee that every image improves at every color count.

## Dithering brush

Open the Brushes arrow and choose **Dithering brush**. Its size uses the normal
size controls. The Tool ribbon's **Brightness / saturation noise** switch selects
between two operations:

- Switch off: Neighborhood. A sampled pixel moves halfway toward the alpha-weighted
  linear-RGB mean of its selected 5×5 neighborhood.
- Switch on: Noise. A sampled pixel receives up to ±0.025 OKLab lightness and
  ±10% chroma scaling. Gamut clipping can limit the adjustment.

Both operations act on existing pixels and preserve each pixel's alpha. Primary
and Alt ink do not supply replacement colors. This is not an additive peg medium.

The spray probability is 0.28 × (1−r²)² inside the normalized brush disk and zero
outside it. It is inspired by the requested center-heavy disk behavior; it is not
a uniform hyperbolic-area distribution. Dabs follow traveled distance, with one
initial dab per gesture, so extra stationary mouse events do not build up noise.
Each dab reads all its neighborhoods before applying changes. Selection holes
cannot supply neighborhood colors. Normal guide constraints, rotated-view inverse
coordinates, and atlas wrapping continue to apply. A stroke is one Undo step.
