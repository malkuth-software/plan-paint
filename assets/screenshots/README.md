# Screenshots

The `macos-home-0.3.2.jpg` and `macos-guide-0.3.2.jpg` images are untouched native window captures from the packaged macOS 0.3.2 application. They show its Home ribbon, moss cloth surround, and an editable curved Guide around a flattened brush and lettering specimen. The original JPEG bytes from CUA are retained. `capture-0.3.2.json` records the executable and image hashes.

The 0.2.0 native application captures are:

- `linux-home.png`: GUI.Forms application client pixels read from its X11 window
  on Alpine ARM64 with dwm. The native interaction fixture drew the displayed
  shapes, brush stroke and editable Bézier through the application's input path.
- `windows-home.png`: the same native interaction fixture running in Wine. The
  image is the actual Windows host's CPU presentation surface, captured through
  its test automation interface. It is not a screenshot from a native Windows
  installation.

The older `macos-*.png` files are historical captures of the earlier comparison
frontend. They do not represent the 0.2.0 native GUI.Forms interface.
