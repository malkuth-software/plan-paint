#!/usr/bin/env python3
"""Check packaged startup with isolated preferences and shipped dependencies."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="paint-package-smoke-") as temporary:
        environment = os.environ.copy()
        environment.update(APPDATA=temporary, HOME=temporary, XDG_DATA_HOME=temporary)
        environment.pop("GUI_FORMS_FONT_DIR", None)
        environment.pop("DYLD_LIBRARY_PATH", None)
        environment.pop("LD_LIBRARY_PATH", None)
        if sys.platform == "win32":
            system = Path(environment.get("SystemRoot", "C:/Windows"))
            environment["PATH"] = os.pathsep.join((str(system / "System32"), str(system)))
        else:
            environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
        process = subprocess.Popen([str(binary), "--language=ru-ru"], cwd=binary.parent, env=environment)
        try:
            code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=10)
            print("Packaged startup survived five seconds; native rendering and input are checked by CTest.")
        else:
            raise SystemExit(f"Packaged application exited early: {code}")


if __name__ == "__main__":
    main()
