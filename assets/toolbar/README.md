# Hand and dropper

The owner supplied the hand-and-dropper pixel artwork for Rainstar Paint.
`dropper-hand-source.png` is the imagegen-edited transparent version with the
wrist shortened. `dropper-hand.png` is its 64-pixel toolbar derivative; the
application embeds only that small PNG through `src/forms/dropper_icon.hpp`.

Regenerate the toolbar derivative with:

```
sips -Z 64 assets/toolbar/dropper-hand-source.png --out assets/toolbar/dropper-hand.png
```

The icon replaces the previous code-drawn hand while retaining click-to-pick
and drag-to-pan behavior.
