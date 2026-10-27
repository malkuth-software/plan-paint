# Ribbon artwork

These SVGs derive from Rainstar Paint's existing `classic_icon` vector artwork,
with optical-size shape contours for the GUI.Forms ribbon. The application uses
embedded PNG rasters at 2x density for 16, 24 and 32 logical pixel ImageLists.
The shape drawer uses the 24-pixel set; small ribbon slots use the 16-pixel set.

Regenerate from the repository root after configuring the GUI.Forms build:

```sh
python3 scripts/export-ribbon-icons.py
cmake -S . -B build-forms
cmake --build build-forms --target paint-export-ribbon-icons
build-forms/paint-export-ribbon-icons "$PWD"
cmake --build build-forms
```

LunaSVG rasterizes the SVG contours through the existing Paint format engine.
Normal application builds consume `src/forms/ribbon_icons.hpp` and do not need
the source SVG files at runtime.
