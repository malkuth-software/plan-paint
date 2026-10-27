"""Package the same toolkit fonts and notices from source or an installed SDK."""
from pathlib import Path
import shutil


def copy_toolkit_notices(toolkit, destination):
    installed = toolkit / "share/licenses/GUIForms"
    if installed.is_dir():
        shutil.copytree(installed, destination, dirs_exist_ok=True)
        return
    files = {
        "LICENSE": "LICENSE",
        "src/core/threading/atomic_pool/LICENSE": "threadpool_atomic_fast-LICENSE.txt",
        "third_party/unicode/LICENSE.txt": "Unicode-LICENSE.txt",
        "third_party/sheenbidi/LICENSE": "SheenBidi-LICENSE.txt",
        "stx_zlib/LICENSE": "stx_zlib-LICENSE.txt",
        "stx_zlib/LICENSE-puff.txt": "stx_zlib-puff-LICENSE.txt",
        "stx_zlib/LICENSE-zlib.txt": "stx_zlib-zlib-LICENSE.txt",
        "stx_png/LICENSE": "stx_png-LICENSE.txt",
        "stx_conv/LICENSE": "stx_conv-LICENSE.txt",
        "third_party/harfbuzz/COPYING": "HarfBuzz-COPYING.txt",
        "third_party/freetype/docs/FTL.TXT": "FreeType-FTL.txt",
    }
    if (toolkit / "third_party/skia/LICENSE").exists():
        # Skia is built without libpng and zlib; GUI.Forms decodes PNG with
        # its own stx_png and stx_zlib, whose notices are listed above.
        files["third_party/skia/LICENSE"] = "Skia-LICENSE.txt"
    destination.mkdir(parents=True, exist_ok=True)
    for source, name in files.items():
        shutil.copy2(toolkit / source, destination / name)
