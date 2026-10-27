# Accessible drawing and language support

Settings > Canvas controls adds a drawing position and discrete actions below the image. Enter X/Y in original image pixels, then use Move to position, Primary click or Alt click. Start stroke latches the gesture, directional buttons advance it by Step (pixels), and Finish stroke releases it. Ordinary pointer hover does not move a latched stroke. Undo removes the complete mark. The same original-coordinate mapping works while the view is rotated.

The panel's checkboxes replace held Alt/color, constrain and center modifiers. With the canvas focused, arrow keys move the virtual drawing position and Space clicks. Without this mode, arrow keys retain normal panning behavior. Click-to-place shapes finish through the panel's Finish stroke action. The panel releases pointer capture after every action so its controls remain reachable.

A keyboard or accessibility activation of a spirograph peg size seats it in the selected socket or first empty socket. The existing pointer drag remains available. On the focused custom pattern canvas, arrow keys choose a pixel, Space paints black and Enter paints white. Changes use the tile's undo history.

Controls expose semantic names, roles, values and actions through GUI.Forms. Numeric dialog fields have human-readable names. A containing focus scope limits the accessible tree and actions to the active dialog or popup; stale background controls cannot invoke commands behind it.

Platform adapters are independent of Paint:

- macOS uses the existing AppKit accessibility adapter. Native development review exercised coordinate editing and a completed stroke through accessibility actions.
- Windows publishes an MSAA `IAccessible` tree through `WM_GETOBJECT`, with names, states, geometry, default actions and editable values. Its protocol fixture has been cross-built and exercised under Wine. A native Windows/NVDA or Narrator session remains necessary for a platform usability claim; this is not a full UI Automation provider.
- Linux exposes the same semantics through ATK/AT-SPI on the X11 host. A separate AT-SPI client in a Linux VM exercised discovery, activation, numeric editing and Unicode text entry. This validates the service bridge fixture, not an end-to-end Orca session in packaged Paint. ATK is used as an accessibility service adapter, not as a widget or rendering toolkit.

These controls provide alternatives to continuous physical dragging. Voice Control, switch scanning, eye tracking and other assistive input still need end-to-end sessions with their intended users. Visual artwork interpretation and comprehensive nonvisual drawing guidance are separate from control access.

Language selection is under Settings. The list shows all available choices when the window has room, with a visible scrollbar when it does not. English is compiled in; optional UTF-8 JSON catalogs use per-message English fallback. See [language pack format](../languages/README.md). macOS reads preferred languages through Core Foundation, Windows uses preferred UI languages, and Linux honors message-locale environment preferences. Operating-system file dialogs and standard alert buttons follow the operating system language, independently of the application override. The command-line `--language=ar` and `--canvas-controls` options apply to one launch and do not overwrite stored settings.

When the interface is not English, **Help! English!** appears beside Help on the ribbon. Its confirmation stays in English. Choose OK to save English as the application language, save any unfinished picture, then restart Plan Paint. Cancel leaves the language unchanged. This action does not close the picture or restart the application automatically.

The CPU text stack resolves bidirectional text before shaping, preserves UTF-8 source offsets, and wraps unspaced CJK text at grapheme boundaries when it exceeds a line. Bundled fallback fonts cover the supplied scripts. Right-to-left help and dialog labels align right; the entire ribbon arrangement is not mirrored.

## Windows focus traversal

The pinned GUI.Forms patches include an MSAA self-reference correction. A focused
control returns `CHILDID_SELF` when queried about its own focus or hit target.
Returning a newly allocated wrapper for the same control could cause Windows
UI Automation to recurse indefinitely and leave the application unresponsive.
The toolkit regression checks focus descent and self hit-testing. The patch is
verified by checksum when fetching the toolkit source.
