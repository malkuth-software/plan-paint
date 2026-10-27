# Line, polygon and path rendering

The Shape and Path tool tabs offer three rendering choices. The same choice is
available in Materials while a shape, path or freehand shape is active.

- **Crisp** keeps whole-pixel edges.
- **Smooth**, the default, retains the existing coverage-based renderer.
- **4× Smooth** draws geometry at four times the width and height, then averages
  each 4 × 4 block back into one canvas pixel. Alpha-weighted reduction avoids
  color fringes on transparent artwork. Curved paths use a finer approximation
  before rendering.

Choose the mode before drawing, or change it while a curve or path is still
editable. Released marks remain ordinary canvas pixels. The choice does not
resize the paper, add a layer, or resample existing artwork. Undo and redo retain
the rendered marks. The pencil remains a one-pixel tool; freehand brushes retain
their Smooth lines switch.

Patterns and paper grain stay anchored to the original canvas coordinates in
4× mode. Pen width and pigment depth are measured in original image pixels.
Temporary tiles limit working memory; their borders include enough surrounding
geometry to keep material depth continuous. Rendering can take longer, especially
for large filled paths. Smooth already integrates solid stroke edges accurately,
so the difference can be subtle; 4× is an additional choice, not a replacement.

The rendering regression compares tiled output against a full-size 4× reference
for solid, watercolor and crayon geometry, including translucent joins and fills.
It also checks paper coordinates, pattern scale, transparent pixels and path
history. Native interface tests verify the three visible choices.
