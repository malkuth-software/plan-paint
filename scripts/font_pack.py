"""Copy Paint's explicit font selection, including all required font notices."""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]

def copy_fonts(sdk, destination):
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    fonts = sdk / "share/GUIForms/fonts"
    if not fonts.is_dir():
        fonts = sdk / "assets/fonts"
    for name in (ROOT / "packaging/fonts.txt").read_text().splitlines():
        shutil.copy2(fonts / name, destination / name)
    for directory in ("cairo-unicode", "poster"):
        for source in sorted((ROOT / "assets/fonts" / directory).iterdir()):
            if source.is_file():
                shutil.copy2(source, destination / source.name)
