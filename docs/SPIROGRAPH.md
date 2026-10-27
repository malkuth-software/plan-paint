# Spirograph

Open **Path / graph dropdown → Spirograph**. The green support and blue insert
are temporary translucent apparatus above the canvas. Their parts never enter
an image export.

## Pegs and media

1. Choose a guide and an insert. The menus beneath their trays open the full
   illustrated catalogs; their captions identify the current parts.
2. Drag a Fine (1 px), Medium (2 px), or Bold (4 px) peg into an empty socket.
3. Choose **Fill pegs**. Click the peg for Primary ink or right-click for Alt.
   No Color empties it. Empty sockets cannot hold ink.
4. Choose **Operate**, then click a seated peg. Its gold outline identifies it
   as the target for **Peg medium** and the ordinary **Materials** gallery.
5. Choose a medium. Each peg keeps its own ink, width, material settings, and
   miniature stroke preview in its cap. Brush assignment does not draw.
6. Drag the insert's center grip. Every loaded peg draws with its own medium.

For precise placement without dragging, open **Choose hole**. Each numbered
entry says whether it will **Place fine peg** in an empty hole or **Select peg**
already there. Selecting an occupied hole keeps its ink, width and medium.
Use **Load Primary** or **Load Alt** to fill the selected peg directly; No Color
empties its ink while leaving the peg seated. Activate Fine, Medium or Bold
with the keyboard or an accessibility action to change its width. These setup
controls do not mark the canvas. Hole numbers run from the outside inward on
the circular kit wheels. Their positions are approximate, so instruction-book
patterns will be visually similar rather than exact reproductions.

All thirteen additive media are available, including Gel pen, Watercolor,
Marker, Natural pencil, Crayon, Oil, and Airbrush. These use the same stroke
renderers as freehand brushes. Pixel-mixing and cloning tools are not ink media.
Gel pen has a solid body, gently varying brightness, and sparse glints. Its
material texture remains fixed during a stroke; extra mouse events do not add
extra coats. Grain scale changes the variation scale and Paper tooth changes
the amount of sheen. Paint load zero deposits nothing.

A click selects a peg; moving more than four screen pixels starts a peg drag.
Drop it in another socket to move it, or outside the insert to remove it. An
invalid drop within the insert restores the original socket. Escape cancels a
move. **Deselect peg** returns material choices to the ordinary drawing tool;
clicking away from the apparatus also clears peg selection.

Drag the green support to reposition the assembly without drawing. **Center
guide** returns it to the image center. Removing or replacing an insert clears
its pegs, materials, and selection. The raised red close button removes the
apparatus while keeping the drawing. Each continuous operation is one Undo
step, including all loaded pegs. Drawing respects the active canvas selection,
regular guide constraints, and rotated working view.

Use the ribbon's **Scale** field or drag the gold square on the support's lower
right to enlarge or shrink the apparatus relative to the paper. The scale
field accepts values from 0.05 to 64 and remains available to keyboard and
accessibility tools. Resizing keeps the center, rolling phase and loaded pegs;
pen widths remain in image pixels. It does not resize existing marks or add an
Undo step. Canvas zoom remains independent of apparatus scale.
Escape during a support move or resize restores the position or size from
before that gesture, including the Scale field.

Choose **Lift wheel** to pick up the insert without drawing. Drag its center
grip or an empty part of its body anywhere on the paper. The wheel keeps its
current rotation and loaded pens; the guide stays in place. An amber outline
means the wheel is off the track, and **Operate** is disabled. Bring it within
12 screen pixels of a valid contact position to seat it on the current ring
or rack. **Escape** during a drag restores the previous placement, including
an off-track position. Choose **Operate** after seating to roll and draw again.
The next stroke starts at the new location without a connecting line from
the old one, and remains a separate Undo step. Moving or resizing the support
carries an attached wheel without drawing; only deliberate rolling deposits ink.

## Component catalog

The catalog includes 18 guides and 32 inserts. The ribbon starts with the two
paired rings and circular wheels 40, 63 and 48; wheel 63 is initially inserted.

- Paired rings: 144/96 and 150/105, named with outer/inner tooth counts. These
  reproduce the Deluxe kit's two circular pitch-track ratios. Both toothed
  edges are visible; the active edge is darker. Switching **Roll outside**
  changes the contact track without resizing the physical ring.
- Additional rings: 120, 144, 180, 210, and 240 teeth.
- Shaped guides: oval, long oval, rounded triangle, square, pentagon, hexagon,
  egg, and shield.
- Rack 150: a closed capsule with straight sides and rounded ends. Wheels roll
  around its full 150-tooth perimeter. Its proportions are approximate: the
  rounded-end pitch radius uses 25 tooth-radius units. The original standard
  and long straight supports remain available with travel limits.
- Deluxe circular inserts: 24, 30, 32, 40, 42, 45, 48, 52, 56, 60, 63, 72,
  75, 80 and 84 teeth. They have respectively 5, 8, 9, 13, 14, 16, 17, 19,
  21, 23, 25, 29, 31, 33 and 35 holes in approximate inward spirals. Every
  hole can hold an independent pen; the numbered menu uses columns so even
  the largest wheel remains within the window at the standard ribbon size.
- Additional circular inserts: 12, 18, 31, 36, 37 and 64 teeth; the prime
  counts support longer repeating patterns.
- Bar 40: an approximate rounded rectangular wheel with nine holes and a
  40-tooth perimeter. It produces 12-fold repetition in the 96-tooth ring and
  21-fold repetition in the 105-tooth ring. The outline and hole positions are
  approximate; they are not measured molded-part dimensions.
- Eye 60: a lens-shaped wheel with rounded tips and 13 approximate holes. Its
  60-tooth perimeter gives eight repeats in the 96-tooth ring and seven in the
  105-tooth ring, matching the kit pattern chart. The outline uses circular
  side arcs joined to smaller tip arcs; dimensions are approximate.
- Triangle 54: an approximate rounded, unequal triangle with 12 holes, following
  the manufacturer’s illustrated layout. Its 54-tooth perimeter produces
  16 repeats in ring 96 and 35 in ring 105. The older experimental Triangle 42
  remains available separately.
- Shaped inserts: oval, long oval, rounded triangle, square, pentagon, hexagon,
  egg, and shield. Each has its own socket arrangement.
- Quad 60: a four-armed wheel with concave cutouts and ten approximate holes.
  It rolls along each rounded tip, then pivots between adjacent tip endpoints.
  It currently supports the inner 96- and 105-tooth kit rings, producing eight
  and seven repeats. Its fitted size changes by approximately 0.27% between
  those rings to match the repeating patterns; this is an approximation rather
  than a measured molded part. Other guides and outside rolling are disabled
  for this wheel.

**Roll outside** places the insert against the outside of a closed guide,
producing a different family of curves. Rack 150 always rolls outside; its
outside control is selected and disabled. The older straight racks roll along
a bounded segment.
Inside combinations are conservatively checked for curvature clearance;
incompatible parts are disabled. Returning from outside to inside requires a
compatible pair. Changing the guide preserves its approximate displayed size
by rescaling the entire apparatus, including the insert.

## Contact model

Most shaped rings and inserts use a support function
`h(n) = 1 + sum(a[k] cos(k*n))`, with positive curvature radius `h + h''`.
Its boundary is `h*n + h'*t`; its arc length has an analytic integral. An
invertible arc map matches the distances traveled on the guide and insert,
then aligns their contact normals. This determines both translation and
rotation, including asymmetric shapes. Circles use the exact familiar radius
ratio as a fast path. The straight racks match arc length against a line.
Rack 150 uses distance around a capsule as its phase: its contact normal stays
constant along each straight side and turns continuously around the rounded
ends. Matching arc length gives continuous rolling and supports lifting with
the wheel's rotation held fixed.

The Bar, Eye and Triangle 54 use joined circular arcs for their sides and rounded corners. Their tangents
stay continuous at each join, and their positive curvature permits exact arc
inversion. The same contact model therefore supports both rolling and lifting.
Compatibility and pen subdivision use bounds from the selected profile,
including their tighter corners.

Quad uses a separate continuous pose parameter through rolling and pivoting.
At the middle of a pivot interval both adjacent endpoints touch the ring;
the supporting endpoint changes without a jump in wheel position or rotation.
Lifting solves for the same rotation at the new placement. Concave cutouts
remain empty in both the rendered outline and pointer hit testing.

Integer tooth counts fix the perimeter ratio, so closed-guide patterns return
to their initial pose after `insert_teeth / gcd(guide_teeth, insert_teeth)`
guide circuits. The tooth marks visualize the pitch geometry; there is no
rigid-body collision simulation or accumulating integration slip. These are
original smooth pitch profiles, not reproductions of commercial molded parts.
The paired circular ring counts follow the
[Deluxe guide](https://www.worldbrands.es/wp-content/uploads/80977_SpirographDeluxeGuide_V2_esp_ONLINE_O_LOW_compressed.pdf).
Circular tooth and hole counts follow the illustrated kit and this
[firsthand inventory of a Deluxe set](https://spirographicart.com/2014/05/02/chart-spirograph-wheels-rings/).
The spiral socket coordinates are an approximation, not measurements of the
molded pieces. The Bar, Eye and Triangle 54 follow visually close approximations with verified
contact and repetition. Quad uses the guide-dependent approximation described above;
other experimental profiles remain distinct from the kit. The capsule
rack's width has not been measured from a physical part; the
catalog is not yet a complete Deluxe replica.

Pen subdivision is bounded to half an image pixel by a curvature-dependent
speed bound. Per-peg stroke state preserves continuous deposition across mouse
events. Sparse transparent coats are composited in socket order, with higher
numbered sockets above lower ones at crossings. One scratch bitmap is shared
among all pegs; the temporary coats and bitmap are released after each drag. The nearest center-path position is projected near the previous phase;
this avoids substituting a circular orbit for a noncircular piece.

## Geometry references

The physical kits informed the range of mechanisms, not the code or drawings:

- [Wild Gears Compact set](https://www.wildgears.com/compact-gear-set.html):
  circular and noncircular gears, varied socket sizes, and component reuse.
- [Wild Gears Oval set](https://www.wildgears.com/oval-gear-set.html):
  oval inserts and frames with different symmetries.
- [Wild Gears Modular Oblong set](https://shop.wildgears.com/products/modular-oblong-gear-set):
  racks and oblong supports.
- [PlayMonster Art Studio instructions](https://playmonster.com/wp-content/uploads/2021/10/artstudio-instructions.pdf):
  rack use and outside rolling.
- [Firsthand Bar wheel demonstration](https://spirographicart.com/2016/06/05/intro-bar-wheel/):
  12-point and 21-point repetition in the two kit rings, matching wheel 40.

- [Manufacturer pattern guide](https://www.worldbrands.es/wp-content/uploads/80979_Spirograph-Guide_v2_EN_COMPLETE_compressed.pdf):
  page 7 shows eight and seven repeats for the Eye in the 96- and 105-tooth
  rings. A 60-tooth perimeter is inferred from these two repetition counts;
  the guide does not print a tooth count on the Eye. Its Triangle examples
  show 16 and 35 repeats, consistent with the implemented 54-tooth perimeter.
  The illustrated Triangle has 12 holes and the instructions use hole 11;
  this differs from the 10 holes listed in the older firsthand inventory.

Nested independently moving inserts, concave tracks, and modular track assembly
remain possible extensions requiring additional motion constraints.
