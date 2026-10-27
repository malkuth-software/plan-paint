# Plan Paint 1.0

Plan Paint is the new name of Rainstar Paint. GUI.Forms is the primary native interface and the only frontend in release packages. The older SDL/ImGui frontend is deprecated, disabled by default, and retained for historical comparison as `plan-paint-legacy`.

> "For I know the plans I have for you, declares the LORD, plans to prosper you and not to harm you, plans to give you hope and a future."

— Jeremiah 29:11

## Upgrading

Applications, executables, portable folders and release files now use Plan Paint or `plan-paint`. The vendor is Malkuth Software (owner, 2026-10-27): the macOS bundle and installer identifier is `com.malkuthsoft.planpaint` (it was `org.rainstar.paint`; the installer still installs to `/Applications/Plan Paint.app`, so an upgrade replaces the old bundle in place), and the Windows executable's version resource names Malkuth Software as the company. Linux releases are portable archives today; a Debian or other Linux package, when one is added, is named `malkuth-plan-paint` and its Maintainer is `Malkuth Software <maintainer@malkuthsoft.com>`. Install locations and the portable `PlanPaint` folders are unchanged. Settings, palettes, custom patterns and recovery records retain the pre-1.0 storage directory. Existing user data stays available without a destructive migration. Internal CMake options retain their `RAINSTAR_` names so build scripts continue to configure the same features.

GitHub's repository rename preserves redirects from the previous repository. The website's old `/rainstar-paint/` route redirects to `/plan-paint/`. Earlier release tags and files remain historical Rainstar Paint artifacts.

## Included in 1.0

- Native Forms ribbon, hue settings, refreshed help and the supplied Plan Paint icon.
- Atomic saves and bounded crash recovery with explicit recovered-copy saving.
- Linear and circular gradient bucket fills with 2–32 stops and twelve presets.
- Refined OKLab dithering and posterization, plus the independent dithering brush.
- Spirograph guides, inserts and media pegs; gel pens, custom pattern canvas and tool cursors.
- Carpet textures, persistent selections, view rotation, text and CONV transforms from preceding releases.

The icon source is `assets/app-icon-source.png`, supplied by the project owner. `scripts/make-icon.py` creates PNG, ICO and ICNS encodings while retaining the complete design. Historical screenshots are identified as such.
