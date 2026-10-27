# Gradient fills

Open the arrow on the Fill bucket and choose **Use gradient**. A **Gradients** ribbon tab appears. Choose a preset or edit the color strip, then click the canvas to fill a connected area. **Solid / material fill** in the same menu restores the ordinary bucket and hides the Gradients tab.

The gradient editor retains its current settings while switching tools or documents within the same application session. It does not yet save named custom presets or persist gradient settings across application restarts.

## Color stops

- Click an empty position on the strip to add a color stop, or use **Add**. New stops start with the color already present at that position.
- Drag a handle to move it. Handles may pass one another; the selected handle keeps its color.
- Use **Stop** to choose a handle and **Position (%)** for precise placement. Between 2 and 32 stops are supported.
- Click the color swatch or **Color…** to edit the selected stop. The color dialog supports RGB, OKHSL, hex and alpha. Cancel leaves the stop unchanged. Primary and Alt materials are independent of gradient colors.
- **Remove** deletes the selected stop, keeping at least two. **Reverse** reverses the color order and stop positions.
- Stops at the same position create a sharp transition. The last stop at that position supplies the color on its right side.

The strip shows the complete color progression regardless of direction. A checkerboard behind it reveals transparency. Colors blend in OKLab, weighted by their alpha, so an invisible stop's hidden RGB does not tint its visible neighbor. Alpha then composites onto the existing canvas.

## Direction and region

**Linear** supports an angle in degrees: 0° runs left to right, 90° top to bottom, and angles increase clockwise in document coordinates. The first and last colors reach the ends of the connected region's bounding box projected onto that direction.

**Circular** radiates from the center of the connected region's bounds. Equal distances from the center have equal colors, even in a wide or narrow region; its radius reaches the farthest bounding corner. Angle is disabled in this mode.

Like the ordinary bucket, the gradient bucket visits connected pixels matching the clicked source color. Selection boundaries and holes block it, as do guides. Pasted floating content does not become a painting mask. Atlas wrapping follows the existing bucket behavior; wrapped components use their bounds in the original image. Rotating the working view changes neither the stored gradient direction nor document coordinates.

Each fill is one Undo step and keeps the active selection. Editing stops prepares subsequent fills; it does not alter previously painted gradients. To replace a just-filled gradient across the same region, Undo and fill again.

## Presets

The gallery contains Rainbow spectrum, Sunset fire, Electric blue, Metallic chrome, Gunmetal, Purple galaxy, Gold, Rose quartz, Ocean, Emerald, Copper, and Black and white. Chrome, gunmetal, gold and copper use alternating highlights and shadows for metallic bands. These are Paint's own color arrangements, inspired by classic presentation gradients; they are not imported Microsoft preset definitions.

## Implementation and checks

`GradientSampler` prepares at most 32 OKLab stops. The bucket discovers its connected region before writing colors, reusing the existing visited mask and flood queue, then shades only visited pixels within the region bounds. It does not retain a full-canvas gradient texture.

Core checks cover linear and radial geometry, nonuniform and coincident stops, transparency, moved handles, connected boundaries, selection holes, floating selections, guides and atlas wrapping. GUI.Forms checks exercise the actual Fill menu, preset gallery, stop editing and cancel, mode switching, selection preservation and exact Undo.
