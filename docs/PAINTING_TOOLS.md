# Painting and lettering in 0.3

## Brushes

Open the Brushes arrow to choose **Additive**, **Mix**, or **Heal**. The tool tab exposes settings for the selected family. Primary and Alt retain their separate colors and materials.

Additive brushes deposit paint. Calligraphy changes the tip, oil and bristle produce directional strands, and crayon, pencil, watercolor, pastel and charcoal use different grain and deposition profiles. Spray can uses a dense center and a soft falloff, with small lightness variations and smaller color variations in OKLab. Choose **Brushes → Fine glitter** to select Spray can and enable its sparkle in one step. The **Fine glitter** checkbox on the Tool tab switches the fine facets and sparse bright glints on or off. Natural pencil has fine, firm tooth; crayon has a dense waxy core with coarse breaks near its rim; soft pastel has a chalky body and feathered rim; charcoal has a compact center and a wider, finely granular powder edge. Their coverage is retained per stroke so overlapping dabs do not flatten all four into the same opaque mark. The small Home **Pencil** always uses a solid one-pixel tip, independent of the last brush and pattern.

Each Primary or Alt slot holds **one** material: Solid, No color, a brush, or a pattern. Selecting a brush clears its pattern; selecting a pattern clears its brush. Selecting a color preserves its material. **Solid** deposits a plain color. Both slots stay available; right-click drawing uses Alt regardless of Primary’s material. The material choices apply to strokes, paths and shapes.

With **Alt carries body** off, Alt supplies the gaps in Primary; choose Alt **No color** to leave those gaps transparent. With the switch on, Primary supplies the edge and Alt supplies the body of a shape or path. The switch sits beside **Smooth lines** in Materials and remains available while Primary is Solid. The Edge and Fill switches independently control which parts are drawn.

Mix brushes transform existing pixels. Choose Ripples, Glass tile, Wigner counterflow, Support braid, Support lens, Moving rooms, or Contour relief. Strength controls the blend with the original image; Scale controls the spatial period and Phase shifts the pattern. Contour relief uses the image’s own lightness contours to add raised-paint highlights and shadows without moving pixels or changing opacity. For this effect, Scale controls the contour width and Phase rotates the light; flat regions remain unchanged. Each gesture samples an immutable starting image, so repeated pointer events over the same position do not repeatedly degrade the result.

Heal first needs a source: its first click captures one, or use **Set source** to replace it. **Aligned source** keeps the source offset between strokes; disable it to return to the captured origin with each stroke. The source marker shows where pixels are sampled. A new source is a snapshot; subsequent painting does not alter it.

**Hardness** controls only the brush edge. **Color match** adapts donor detail using paired source and destination samples outside the painted repair area, so the marked defect does not tint its own replacement. **Clone exactly** disables this correction at any hardness. **Preserve transparency**, enabled by default, retains destination alpha and leaves fully transparent pixels and their hidden RGB unchanged. Disable it to transfer source alpha; combine this with Clone exactly and full hardness for exact RGBA copying in the brush interior. When no healthy surrounding samples are available, healing retains the donor color.

Pencil and all brush families offer **Stabilizer** and a lag distance. The pointer trajectory is filtered in distance along the stroke, with the same integration for a straight movement whether it arrives in one event or many.

Eraser offers Hard, Soft, Blur, Sharpen and Smudge. Blur and Sharpen use a local perceptual average; Smudge displaces samples along the drag. Strength and Scale appear for these three modes.

## Stamp

Capture a region with Stamp, then click or drag to place it. **Hardness** 1 retains the capture mask; lower settings feather inward from the chosen shape's boundary. The width, height, scale and angle controls remain available.

**Add material** retains the current stamp and returns to capture mode. The retained stamp appears at half opacity while a new region is chosen. The new content is aligned to the retained stamp's size and contributes detail after local OKLab color correction. New opaque material can fill transparent parts of the captured stamp, extending its silhouette within the capture bounds. Moving this preview does not paint onto the source picture. **Clear stamp** discards both the current stamp and a retained basis.

## Guides and selection

Guide is the drafting triangle beside Path. Click to add vertices. Clicking an earlier vertex connects back to it; clicking the first vertex closes the guide. The next edge previews beneath the pointer. Right-click cancels that pending edge and removes a lone initial anchor; **Set guide** closes the retained outline. Drag a vertex to reshape it or drag inside a closed guide to move it. **Protect body** or the filled-square ribbon switch blocks the inside; when off, the boundary acts as a line stencil. Click the main **Guide** button again to unset it. **Clear guide** in the tool tab and **Unset guide** in its arrow menu do the same.

The Guide arrow menu also offers **Edit guide** and **Swap segment > Bézier / Arc**. Choose a curve type, click a guide segment and drag its handles; right-click returns to ordinary guide editing. Moving a node carries its attached curve controls, and moving a closed guide carries the complete curved outline. These changes affect only the stencil.

A guide constrains Pencil, Brush, Fill and Stamp. It is display and editing state and never becomes exported artwork. Selection, Path, shapes, text, crop, cut, paste, resize, reshape and document resizing clear it. Changing atlas frames also clears it. Cut with a guide and no pixel selection only unsets the guide: the image, clipboard and document history are untouched. A real pixel selection retains ordinary Cut behavior.

Choose **Tightening lasso** or **Inner-void lasso** from the selection menu. Tightening compares the lasso boundary's background in OKLab, ignores small perceptual variations such as JPEG flecks, and retains the connected object nearest its center with a feathered edge. Inner-void starts inside the drawn loop and expands through similar connected pixels, beyond the loop, until it reaches the enclosing boundary. Draw around the intended subject for Tightening, or inside the intended void for Inner-void. These are perceptual color selections, not semantic object recognition.

Selections retain animated dashed contours around every selected island and interior hole. Hold **Ctrl** while drawing another lasso to add that freeform region; hold **Alt** to subtract it. Both modifier operations use the exact drawn region regardless of the active lasso mode.

With a selection active, choose Guide to protect its silhouette immediately. With Fill enabled, enclosed holes belong to the protected body too: a tightening lasso around an outlined oval protects its interior as well as its ink. The exterior keeps its feathered coverage until the guide's vertices are edited. Turn Fill off for a boundary-only stencil. Switching to Brush keeps the guide active; its cross-hatching marks the protected body after the editing handles disappear.

## Paths

An extending Path can snap back to any retained node. The preview shows the connecting segment; clicking commits it and ends that run. Click again to begin another run at an existing junction or a new point. Right-click cancels the pending segment; if its initial anchor is the only node in the new run, that anchor is removed too. Committed runs remain editable. Right-click also cancels a Line while dragging it.

Enable **Edit nodes** in the Path controls to move retained junctions with the primary mouse button. Clicking empty paper in this mode does not add geometry. Hovering an editable node or a visible curve handle shows a pale-gold highlight and a hand cursor. The moving node has a stronger gold highlight, and each drag remains one Undo step. Disable Edit nodes or choose **Continue path** to resume adding segments. The existing right-drag shortcut remains available. Curve controls use square handles; the handle being moved is highlighted, and overlapping hit regions select the nearest handle.

In Edit nodes, click an existing curved segment to reveal its handles. Selecting it preserves its Bézier or arc type and does not draw or add an Undo step. Drag a square handle to reshape the curve; clicking a handle without moving it leaves the curve unchanged. Selecting a junction returns to node editing. **Swap segment** remains the command for converting a straight segment or changing its curve type.

In the Path arrow menu, choose **Swap segment**, then Bézier or Arc. Click an existing segment between active blue endpoints. Drag the controls to reshape it, then right-click to return to ordinary Path editing. The endpoints remain part of the path; Undo and Redo retain the active geometry.

## Atlas painting

These controls apply while an atlas frame is active:

- **Warp edges** wraps strokes horizontally and vertically. Neighbor copies around the current frame show seams as it changes.
- **Preserve transparency** retains every original alpha byte, including partially transparent edges. Fully transparent pixels retain their hidden RGB values. Hard and soft erasing make no changes while this is enabled.
- Right-click another frame thumbnail to show it over the current frame at 50% opacity. The current frame remains editable. **Dismiss reference** removes the overlay. It does not enter the saved image.

## Text

The font list includes DynaPuff, Bubble Sans, Anton and Titan One, with their open-font licenses bundled alongside them. **Outline letters** uses Primary for contours, including the edges of counters such as the holes in B and O, and Alt for the body of the letters. The holes themselves remain open. Outline width is adjustable; a thin font can have little interior left at a large width.

The WordArt selector offers 3D extrusion, embossed edges and an OKLab gradient between Primary and Alt. **Skew**, **Perspective** and **Bend** affect the live text box. Text, selection and caret positioning remain editable under the same mapping. **Reset** restores plain geometry and treatment. **Place text** or Ctrl/Command+Enter commits exactly the current raster preview; Escape cancels it.

## Color and view

The ribbon palette button cycles Basic, Custom and Themed. Custom contains 29 saved colors plus a permanent **Transparency** swatch, shown as a red slash on white. Existing 16-color and 30-color preferences migrate automatically; the first 29 saved colors are retained. Basic and Themed keep their 30 colors.

Choose the side-by-side **RGB** or **OKHSL** button above the color plane. The RGB view uses hue, saturation and value to choose sRGB colors; OKHSL uses the perceptual picker conversion by Björn Ottosson. Numeric RGB, alpha, hexadecimal and OKLab controls remain available. **Mosaic** divides the existing rectangle into 96 deterministic, outlined cells for each of 24 hue positions in each color space. The compact lattice devotes more cells to saturation and larger cells to near-neutrals; click a cell to choose its color. Seven neutral anchors include black and white. The [mosaic design](COLOR_MOSAIC.md) explains the objective and its limits.

Themed colors form ten columns of Primary, Alt and accent: Glue, Phosphor, Camo, Royal, Afterglow, Arcade, Lagoon, Porcelain, Bordeaux and Ochre. Hover a themed swatch for its theme and position.

Eyedropper defaults to Exact pixel. Small average samples 5×5 pixels; Representative rejects isolated light and dark flecks in that neighborhood. Its optional local lens and the Magnifier use a round, cursor-centered 8× view.

**File > Settings > Canvas surround** offers the original grey-blue felt, moss green felt, warm brown felt, tan felt, and matte-painted slate, clay or ivory. The original surround remains the default. Moss, brown and tan use a dense, quiet cloth surface inspired by pool-table felt, with very fine fibers and restrained tonal variation; the matte surfaces use a fine, shallow stipple. These textures stay at a fixed screen size as the artwork zooms. Old green-felt preferences select moss green automatically.

**Transparency display** chooses Checkerboard or a configurable solid color, with pink as the initial solid choice. Surround and transparency settings change only the view; exported RGBA pixels remain unchanged.

The Royale Cobalt ribbon uses darker rear tabs with raised right edges. The selected tab comes forward and joins the command surface; clicking it again still collapses the ribbon. Buttons have blue faces at rest, a darker selected face and raised bevels. A press reverses the bevel and moves the content inward; selection keeps the raised edge. Focus and disabled states remain distinct. The fine divider above the arrow on Paste, Brushes, Stamp, Path and Guide identifies their dropdown region. Help stays at the right edge. The ribbon and status bar resize with the window, down to an 800-pixel client width.

## Sources and implementation scope

- [OKHSL and the reference picker](https://bottosson.github.io/posts/colorpicker/) supply the actual OKHSL conversion. The vendored MIT implementation is promoted to double precision.
- [iWantHue's theory](https://medialab.github.io/iwanthue/theory/) informed the perceptual clustering approach. Rainstar's deterministic palette code is independently implemented.
- [Kid Pix 4's user guide](https://www.lisd.org/technology/itswebs/elem/tech/handouts/Kid%20Pix%204%20User%20Guide.pdf) describes local Mixer effects. Rainstar's Mix tools use its own raster implementation.
- The Mix family adapts geometric and perceptual color laws from CONV Support Toys 0.4.1 in the [OBS plugin source distribution](https://paymenottowork.com/obs-plugins/). The local brush implementation does not reproduce the full OBS support solver or claim pixel equivalence with that plugin. Its source names the counterflow effect **Wigner counterflow**.
- The [Fine Glitter Texture demonstration by PrettyWebz Media](https://www.youtube.com/watch?v=wdm7KHKfxxw) informed the combination of fine grain and sparse high-intensity highlights. Rainstar generates its own facets during painting.

Collapsing or reopening the ribbon keeps the artwork at the same screen position and zoom. The expanded workspace reveals the area previously covered by the ribbon. File > About Plan Paint displays the version, dedication, credits and license inside a keyboard-accessible application dialog.

## Persistent selections and skew handles

A selection made on the canvas remains active when switching tools. Pencil, all brush families, fill, stamp, eraser, shapes, editable paths and text affect only selected pixels. Lasso holes remain protected; flood fill cannot cross unselected space to reach another selected island. Escape dismisses the selection. Choosing Select or Lasso exposes its movement and transform handles.

Pasted images remain floating overlays. They do not constrain drawing on the underlying canvas. Copying or moving a canvas selection uses the current artwork, including paint added after selecting. Undo and Redo retain the selection alongside the canvas edit.

Four gold handles sit just outside the selection's edge midpoints. The top and bottom handles slide horizontally; the side handles slide vertically. Each shears its edge while holding the opposite edge fixed. The drag preview is bounded by the viewport, and release renders with the existing CONV transform. The selection mask transforms separately from image alpha, so transparent selected pixels remain paintable. Square handles continue to resize.

## Placing lines and shapes

Lines and preset shapes use click–move–click by default. The first click anchors a live preview; move without holding a mouse button and click again to place it. Escape or right-click cancels an unfinished shape. Bézier and Arc use the same gesture to establish their starting line; their control handles remain draggable afterward. Polygon and Path retain their existing corner-by-corner placement.

File > Settings > Lines and shapes offers **Click, move, click** and **Click, hold, release**. The choice persists between launches. Freehand Pencil, Brush and Eraser strokes retain their held gesture.

Hold Ctrl to draw a preset closed shape around its initial center. Circles, stars, regular polygons, gears and bursts use the pointer distance as their radius. Other closed shapes grow symmetrically from the center toward the pointer; Ctrl+Shift constrains their proportions. Lines, curves and vertex-by-vertex polygons retain their endpoint controls.
