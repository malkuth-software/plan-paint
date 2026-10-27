# Interface colors and crash recovery

Settings provides an interface hue control covering 0–359 degrees. Royal blue is
220, pink is 330, purple is 280, and green is 140. Changing the control previews
the color immediately; Cancel restores the previous color and OK persists it.

The application recolors authored blue chrome in OKHSL, retaining perceptual
lightness, saturation, gradient geometry and alpha. Royal blue uses the exact
original bytes. Icons, document pixels, material samples, neutral surfaces and
golden selected-tool states are not recolored. High-contrast control recipes
retain their authored colors. Theme recipes are shared among controls and rebuilt
only when the hue changes; they are not copied for every repaint.

The Home ribbon removes the Clipboard caption, brings the tools' top row level
with the tall controls, clears the tools divider, tightens the gaps around Guide,
Shapes, Size and Edit colors, and aligns the Undo/Redo pair with Save.

## Recovery behavior

Recovery is enabled by default, with a 60-second interval. Settings accepts
15–600 seconds. The first unsaved change can be captured on the next timer tick;
subsequent captures respect the interval. Unchanged editor presentations are
skipped. Active pointer gestures and background transformations finish before a
snapshot is taken. Encoding and disk I/O run on one background worker; the UI
copies a consistent document image at an event boundary.

Each document receives a random UUID v4. Its private recovery directory contains
two alternating `.rpr` records and an OS-held session lease. The records carry:

- UUID and format version;
- the path initially opened and the most recent explicit save path;
- creation, snapshot and explicit-save times in UTC epoch milliseconds;
- document revision and an increasing snapshot generation;
- lossless image content and an integrity checksum.

A record is written to a unique temporary file, flushed, then atomically replaces
one slot. The other completed slot remains available. Generation order is
independent of wall-clock changes. A malformed, truncated or checksum-damaged
newest record falls back to the previous readable one. Both damaged files remain
in place for investigation. This protects completed snapshots against process
crashes; it does not promise survival of a failing disk or every power-loss case.

On macOS and Linux a held `flock` lease identifies live sessions; Windows uses an
exclusive file handle. A crash releases the lease automatically. Other running
Paint sessions are not offered for recovery, and claiming a candidate acquires
its lease again to prevent two windows restoring it simultaneously. No PID reuse
or stale-timeout heuristic is used.

At a normal empty launch, Paint offers abandoned documents after the native
window has entered its event loop. File → Recover
unfinished artwork also opens this flow. Yes opens an unsaved recovered copy;
No retains that document for later; Cancel stops browsing. Restored documents
retain their recovery identity and provenance but require an explicit destination
on Save. Opening a file through the command line takes precedence over startup
recovery, which remains available from File.

A successful explicit save retires that document's snapshots. Normal close or
replacement retires them only after the usual Save / Discard / Cancel decision
allows it. Cancelling preserves them. Disabling automatic snapshots stops new
writes and retains existing copies. There is no age-based deletion of abandoned
artwork. Cleanup removes only known recovery records, and removes empty session
directories without deleting unrecognized files.

## What is recovered

Snapshots preserve the visible artwork, including transparent RGBA pixels,
floating pasted content and visible uncommitted text. Sprite editing is captured
back into its sheet. ICO/CUR artwork retains frames and cursor hotspots, including
the container codec's supported legacy pixel semantics. Painting is captured in
original image coordinates, independently of zoom or working-view rotation.

Recovery restores raster artwork rather than the complete editing session:
undo history, live path/text handles, selection masks, guides and view navigation
are not reconstructed. Edits since the last completed snapshot may be lost.

## Storage and verification

Recovery is local to the existing Paint preference directory:

- macOS: `~/Library/Application Support/rainstar/RainstarPaint/Recovery`
- Windows: `%APPDATA%/rainstar/RainstarPaint/Recovery`
- Linux: `$XDG_DATA_HOME/rainstar/RainstarPaint/Recovery`, or
  `~/.local/share/rainstar/RainstarPaint/Recovery`

The settings format is RSPS7 and reads older RSPS1–RSPS6 preferences. Files use
UTF-8 paths with length-prefixed metadata; record reads have a 512 MB bound and
image decoding retains the existing codec validation limits.

`paint-recovery` exercises identity, live leases, Unicode metadata, transparent
pixels, clock changes, damaged generations, cleanup and multi-frame cursors.
`paint-recovery-process` exits a real process without running destructors and
checks restoration from another process. `paint-forms` additionally checks hue
lightness and gold invariance around the wheel, native control geometry, settings
preview/cancellation/persistence, timer capture, floating artwork, disabled
recovery, restore-as-copy and explicit-save retirement.

Native color review uses the real interface fixture:
`paint-forms-native-tests --interface 0 330` renders pink; replace the final
argument with 220, 280 or 140 for royal blue, purple or green.
