# Custom patterns

Materials includes Bricks, Buttons, Cargo Net, Circuits, Cobblestones, Straw Mat,
and Custom pattern. Bricks retains its existing staggered masonry tile.

Choose Edit custom pattern below the pattern swatches to switch to the dedicated
pattern canvas. Return to main canvas switches back. The main image, selection,
view, active tool, and undo history are retained while the tile is edited.

The tile starts at 8 × 8 pixels. Width and height can be set independently from
1 to 128 pixels. Resize tile preserves the upper-left overlapping pixels and
fills added space with white. The magnified grid fits the editing area; the
preview shows repetition at actual pixel scale.

Only the pixel pencil is available: left-drag draws black and right-drag draws
white. Undo and Redo belong to this canvas, including size changes. The green
surround is #008000. Main-document commands and file drops are unavailable in
this view.

The black and white tile supplies the foreground/background mask for the
selected material. On the main canvas it uses the normal material colors and
alternate/transparent handling. Editing the tile changes subsequent painting;
already-painted image pixels are unchanged.

Completed edits are saved atomically as custom-pattern.bin in Paint's local
preferences folder. Tile dimensions and pixels survive application restarts;
the tile's undo history lasts for the current session. The stored tile has a
bounded binary format, independently validated before loading.
