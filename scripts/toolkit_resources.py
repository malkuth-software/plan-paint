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
        "third_party/harfbuzz/COPYING": "HarfBuzz-COPYING.txt",
        "third_party/freetype/docs/FTL.TXT": "FreeType-FTL.txt",
    }
    if (toolkit / "third_party/skia/LICENSE").exists():
        files.update({"third_party/skia/LICENSE": "Skia-LICENSE.txt",
            "third_party/skia/third_party/externals/libpng/LICENSE": "libpng-LICENSE.txt",
            "third_party/skia/third_party/externals/zlib/LICENSE": "zlib-LICENSE.txt"})
    destination.mkdir(parents=True, exist_ok=True)
    for source, name in files.items():
        shutil.copy2(toolkit / source, destination / name)
