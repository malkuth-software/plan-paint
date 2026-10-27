# Experimental stroke state tracker

Pencil and Brushes offer **Use state-tracker** in their tool settings. Selecting
it enables Stabilizer and substitutes the state tracker for the distance-lag
model. Unchecking it restores the previous lag setting. Disabling Stabilizer
bypasses either model. The alternative is off by default.

**Noise (px)** is the assumed standard deviation of position variation, in
image pixels. It defaults to 1.5 and is captured at stroke start. Larger values
put less trust in individual samples; they may suppress detail and round turns.
**Momentum (%)** defaults to 60. It reduces the correlated local velocity prior
variance and its matching process noise by `exp(-0.10 * momentum)`, preserving
the constant-velocity prior. Small deviations consequently have less influence
on established motion. This is model-level inertia, not a positional dead zone:
sustained deviations can still change the stroke. Higher values soften corners
and delay settling after a stop. Zero reproduces the first tracker exactly.
Both parameters are captured at stroke start, including after a delivery gap.

The lag value has no effect while the state tracker is selected.

The implementation adapts *Exact Markov Elimination for Relational Current
Tracking*, https://github.com/falseywinchnet/papers_please/blob/main/relational_current_tracking.pdf.
Each of five branches tracks position, constant velocity, correlated local
velocity and its normalized derivative. The original three-dimensional
likelihood is changed to two dimensions. Matérn-3/2 correlation lengths are
0.03, 0.07, 0.15, 0.3 and 0.6 seconds; constant and local velocity prior variances
are each 40000 pixels squared per second squared before momentum scaling. These are experimental
pointer presets, not the paper's benchmark parameters.

The model uses fixed 1/240-second cells, extending as needed. Observations
between boundaries condition the current cell through its partial integral.
Normalized cumulative evidence weights the five branches. Storage is fixed;
there is no dense history or retrospective repaint. UI delivery times use a
monotonic clock. A gap longer than half a second re-anchors at the incoming
point instead of continuing stale motion. Each gesture resets the model, and
pointer release uses the same filtering path without snapping to the raw point.

The numerical test includes an independently calculated dense posterior fixture,
60/120/240 Hz synthetic strokes, stops and reversals, duplicate/invalid timestamps,
new gestures, and a long-stroke stability check. These checks do not establish
superiority on human handwriting or compensate for irregular OS event delivery.

## Sparse reconstruction trial

**Sparse smoothing** is selected by default within the opt-in state tracker.
Uncheck it to compare with the previous dense-observation tracker.

The sampler accumulates raw path length, retaining an observation every 6 image
pixels. Points and timestamps are interpolated along each delivered segment;
subdivision of the same timed straight segment preserves the observations.
Only these observations update the estimator. A repeatable pseudorandom dither
in [-0.2, 0.2] image pixels per axis perturbs retained observations. This injected
noise is separate from the Noise (px) likelihood uncertainty setting.

Posterior positions become control points of a uniform cubic B-spline. Adjacent
segments share first and second derivatives. The curve stays within the convex
hull of each four-control-point span, though estimator positions themselves may
overshoot the raw path. Curves are tessellated before painting, and image damage
is published once per input event. No already-painted brush deposit is replayed.

The displayed stroke trails the retained observations by roughly one observation
interval plus sampling wait and estimator response. Release submits an undithered
endpoint observation and flushes repeated endpoint knots, finishing at the
estimated endpoint with zero tangent. Short strokes and single-click dabs remain
supported. No periodic idle extrapolation is performed. This intentionally trades
immediate response and corner fidelity for geometric continuity.

Tests compare dense tracking, sparse reconstruction, increased observation
uncertainty, and injected noise on identical input paths. The initial fixture
shows most improvement from sparse reconstruction; added dither is not proven
beneficial. Uncertainty and dither are not equivalent operations.
