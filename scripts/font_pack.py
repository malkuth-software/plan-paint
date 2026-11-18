"""Copy Plan Paint's canvas faces and the GUI.Forms interface faces' notices.

The interface faces are compiled into GUI.Forms (font pack stage 4); its SDK
installs only their licences and attribution, which the package carries in
licenses/fonts. The canvas text tool's faces are Plan Paint's own content
faces (the interface/content split) and are copied with their notices."""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
NOTICE_PATTERNS = ("OFL-*.txt", "LICENSE-*.txt", "ATTRIBUTION.md")

def copy_fonts(sdk, destination, notices):
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    for directory in ("cairo-unicode", "poster"):
        for source in sorted((ROOT / "assets/fonts" / directory).iterdir()):
            if source.is_file():
                shutil.copy2(source, destination / source.name)
    fonts = sdk / "share/GUIForms/fonts"
    if not fonts.is_dir():
        fonts = sdk / "assets/fonts"
    notices.mkdir(parents=True, exist_ok=True)
    for pattern in NOTICE_PATTERNS:
        for source in sorted(fonts.glob(pattern)):
            shutil.copy2(source, notices / source.name)
