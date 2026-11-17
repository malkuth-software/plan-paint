#!/usr/bin/env python3
"""Package the native frontend: the program alone, on the desktop's X11 and glibc."""
from pathlib import Path
from release_version import project_version
from font_pack import copy_fonts
from toolkit_resources import copy_toolkit_notices
from check_system_libraries import require_system_libraries
import argparse
import hashlib
import json
import platform
import re
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build-forms")
    parser.add_argument("--output", default="dist")
    parser.add_argument("--gui-forms-sdk", type=Path, required=True)
    parser.add_argument("--version", default=project_version())
    parser.add_argument("--strip", default="strip", help="Target GNU strip; removes unneeded symbols from packaged copies")
    args = parser.parse_args()
    build = (ROOT / args.build).resolve()
    sdk = args.gui_forms_sdk.resolve()
    source = build / "plan-paint"
    # One module (owner, 2026-11-15): the package carries the program only.
    # NEEDED names only the C library and GUI.Forms' X11, ATK and GLib host
    # libraries, and ldd resolves them in system directories; at run time the
    # desktop's own X11 (or XWayland) and glibc are used. No library, loader
    # or X11 locale data is copied: the package is built against no
    # distribution's copies (STXlib is to carry the C library later).
    require_system_libraries([source])
    dependencies = subprocess.check_output(["ldd", str(source)], text=True)
    if re.search(r"/ld-musl-", dependencies):
        raise RuntimeError("A musl program cannot use the desktop's glibc X11; package a glibc build.")
    architecture = {"aarch64": "arm64", "x86_64": "x64"}.get(platform.machine())
    if architecture is None:
        raise RuntimeError("Unverified package architecture: " + platform.machine())
    distribution = (ROOT / args.output).resolve()
    bundle = distribution / "PlanPaint"
    if bundle.exists():
        shutil.rmtree(bundle)
    (bundle / "bin").mkdir(parents=True)
    executable = bundle / "bin/plan-paint"
    shutil.copy2(source, executable)
    # A build-tree RUNPATH would point at the builder's directories.
    subprocess.run(["patchelf", "--remove-rpath", str(executable)], check=True)
    subprocess.run([args.strip, "--strip-unneeded", str(executable)], check=True)
    copy_fonts(sdk, bundle / "bin/fonts", bundle / "licenses/fonts")
    shutil.copytree(ROOT / "languages", bundle / "bin/languages", ignore=shutil.ignore_patterns("*.md"))
    launcher = bundle / "Plan Paint"
    launcher.write_text(
        '#!/bin/sh\nset -eu\n'
        'app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
        'exec "$app_dir/bin/plan-paint" "$@"\n')
    launcher.chmod(0o755)
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(ROOT / name, bundle)
    shutil.copy2(ROOT / "packaging/README.txt", bundle / "README.txt")
    shutil.copytree(ROOT / "packaging/licenses", bundle / "licenses", dirs_exist_ok=True)
    copy_toolkit_notices(sdk, bundle / "licenses/GUIForms")
    # The packaged program, as shipped, still resolves only system libraries.
    require_system_libraries([executable])
    manifest = {
        "version": args.version, "architecture": architecture,
        "frontend": "GUI.Forms", "window_system": "X11; XWayland on Wayland desktops",
        "loader": "the desktop's glibc",
        "system_libraries": "the desktop's X11 (or XWayland), ATK, GLib and glibc; none are carried",
        "files": {str(path.relative_to(bundle)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in sorted(bundle.rglob("*")) if path.is_file()},
    }
    (bundle / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    archive = distribution / f"plan-paint-{args.version}-linux-{architecture}.tar.gz"
    with tarfile.open(archive, "w:gz") as output:
        output.add(bundle, arcname="PlanPaint")
    print(archive)

if __name__ == "__main__":
    main()
