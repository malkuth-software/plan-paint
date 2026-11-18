"""Package the same toolkit fonts and notices from source or an installed SDK."""
from pathlib import Path
import shutil


def copy_toolkit_notices(toolkit, destination):
    installed = toolkit / "share/licenses/GUIForms"
    if installed.is_dir():
        shutil.copytree(installed, destination, dirs_exist_ok=True)
        return
    # Since GUI.Forms took its text stack and codecs from STXlib and stx
    # (2026-11-16: no HarfBuzz, FreeType or SheenBidi), the notices exist
    # only as the installed SDK collects them from the stx package; a source
    # checkout does not hold them at fixed paths.
    raise SystemExit(f"GUI.Forms notices need an installed SDK: {installed} is missing")
