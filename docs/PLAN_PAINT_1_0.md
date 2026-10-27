# Plan Paint 1.0

Plan Paint is the new name of Rainstar Paint. GUI.Forms is the primary native interface and the only frontend in release packages. The older SDL/ImGui frontend is deprecated, disabled by default, and retained for historical comparison as `plan-paint-legacy`.

> "For I know the plans I have for you, declares the LORD, plans to prosper you and not to harm you, plans to give you hope and a future."

— Jeremiah 29:11

## Upgrading

Applications, executables, portable folders and release files now use Plan Paint or `plan-paint`. The macOS bundle and installer identifiers retain `org.rainstar.paint`; settings, palettes, custom patterns and recovery records retain the pre-1.0 storage directory. Existing user data stays available without a destructive migration. Internal CMake options retain their `RAINSTAR_` names so build scripts continue to configure the same features.

GitHub's repository rename preserves redirects from the previous repository. The website's old `/rainstar-paint/` route redirects to `/plan-paint/`. Earlier release tags and files remain historical Rainstar Paint artifacts.

## Included in 1.0

- Native Forms ribbon, hue settings, refreshed help and the supplied Plan Paint icon.
- Atomic saves and bounded crash recovery with explicit recovered-copy saving.
- Linear and circular gradient bucket fills with 2–32 stops and twelve presets.
- Refined OKLab dithering and posterization, plus the independent dithering brush.
- Spirograph guides, inserts and media pegs; gel pens, custom pattern canvas and tool cursors.
- Carpet textures, persistent selections, view rotation, text and CONV transforms from preceding releases.

The icon source is `assets/app-icon-source.png`, supplied by the project owner. `scripts/make-icon.py` creates PNG, ICO and ICNS encodings while retaining the complete design. Historical screenshots are identified as such.
