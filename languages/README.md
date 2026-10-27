# Language packs

Plan Paint selects the operating system's preferred supported language on first launch. Settings > Language overrides this choice; restart the application after changing it. English is compiled into the application. Removing this directory cannot remove English.

Each other language is a flat UTF-8 JSON object named after its normalized language tag, for example `fr-fr.json`:

```json
{
  "@language": "fr-fr",
  "@name": "Français",
  "@direction": "ltr",
  "Open…": "Ouvrir…"
}
```

Keys are the exact English messages from source, including punctuation, whitespace and line breaks. Values are translations. `@name` is the language's own name; `@direction` is `ltr` or `rtl`. Missing messages use their compiled English text. Invalid packs are omitted from the selector. The loader rejects duplicate keys, invalid UTF-8, mismatched file identity, oversized files and malformed JSON. It never executes content.

Preserve product names, numeric limits, shortcut key names, named `{placeholders}`, leading/trailing spaces and paragraph breaks. Do not translate filenames, font-family identifiers or internal command IDs. User artwork text is never passed through the translation catalog. Run `python3 scripts/check-languages.py` after editing a pack.

The initial catalogs include menus, settings, tool descriptions, presets and the full in-app help. They were prepared with translation assistance and structural checks; independent native-speaker proofreading remains welcome, especially for specialized geometry and textile terms.

The catalog includes Simplified and Traditional Chinese, Japanese, German, Italian, French, Spanish, Hebrew, Persian, Arabic, Russian, Ukrainian, Nigerian Pidgin, Hindi, Bengali, Urdu, Punjabi in Gurmukhi and Shahmukhi scripts, Brazilian Portuguese, Indonesian, Swahili and Vietnamese. These are language choices, not nationality labels.
