#!/usr/bin/env python3
"""Bundle the native frontend for the standardized Ubuntu 24.04 ABI."""
from pathlib import Path
from release_version import project_version
from font_pack import copy_fonts
from toolkit_resources import copy_toolkit_notices
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
    dependencies = subprocess.check_output(["ldd", str(source)], text=True)
    if "not found" in dependencies or "Error" in dependencies:
        raise RuntimeError(dependencies)
    loader_match = re.search(r"(/\S*/ld-musl-[^\s()]+)", dependencies)
    loader = Path(loader_match[1]).resolve() if loader_match else None
    architecture = {"aarch64": "arm64", "x86_64": "x64"}.get(platform.machine())
    if architecture is None:
        raise RuntimeError("Unverified package architecture: " + platform.machine())
    distribution = (ROOT / args.output).resolve()
    bundle = distribution / "PlanPaint"
    if bundle.exists():
        shutil.rmtree(bundle)
    (bundle / "bin").mkdir(parents=True)
    (bundle / "lib").mkdir()
    executable = bundle / "bin/plan-paint"
    shutil.copy2(source, executable)
    closure = {}
    for soname, filename in re.findall(r"(\S+) => (/\S+)", dependencies):
        closure[soname] = Path(filename).resolve()
    if loader:
        closure[loader.name] = loader
    else:
        # Keep the glibc loader, libc and tightly coupled runtime modules on the host.
        for soname in list(closure):
            if soname in {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1", "libresolv.so.2"} or soname.startswith("libnss_"):
                del closure[soname]
    for soname, filename in closure.items():
        shutil.copy2(filename, bundle / "lib" / soname)
    subprocess.run(["patchelf", "--set-rpath", "$ORIGIN/../lib", str(executable)], check=True)
    for library in (bundle / "lib").iterdir():
        if library.name.startswith(("libc.musl", "ld-musl")):
            continue
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(library)], check=True)
    for binary in [executable] + sorted((bundle / "lib").iterdir()):
        subprocess.run([args.strip, "--strip-unneeded", str(binary)], check=True)
    copy_fonts(sdk, bundle / "bin/fonts")
    shutil.copytree(ROOT / "languages", bundle / "bin/languages", ignore=shutil.ignore_patterns("*.md"))
    locale = Path("/usr/share/X11/locale")
    if not locale.is_dir():
        raise RuntimeError("X11 locale data is required for keyboard input.")
    shutil.copytree(locale, bundle / "share/X11/locale")
    launcher = bundle / "Plan Paint"
    launcher.write_text(
        '#!/bin/sh\nset -eu\n'
        'app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
        'export XLOCALEDIR="$app_dir/share/X11/locale"\n'
        'export GUI_FORMS_FONT_DIR="$app_dir/bin/fonts"\n'
        + ('exec "$app_dir/lib/' + loader.name + '" --library-path "$app_dir/lib" "$app_dir/bin/plan-paint" "$@"\n'
           if loader else 'exec "$app_dir/bin/plan-paint" "$@"\n'))
    launcher.chmod(0o755)
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(ROOT / name, bundle)
    shutil.copy2(ROOT / "packaging/README.txt", bundle / "README.txt")
    shutil.copytree(ROOT / "packaging/licenses", bundle / "licenses")
    copy_toolkit_notices(sdk, bundle / "licenses/GUIForms")
    # Every loader-resolved dependency must come from this folder, including libc.
    resolved = subprocess.check_output([str(bundle / "lib" / loader.name), "--library-path", str(bundle / "lib"), "--list", str(executable)]
        if loader else ["ldd", str(executable)], text=True)
    if "not found" in resolved:
        raise RuntimeError(resolved)
    for soname, filename in re.findall(r"(\S+) => (/\S+)", resolved):
        # musl reports the executable's PT_INTERP spelling for its already-loaded
        # libc, even when this command explicitly runs our packaged loader.
        if loader and soname.startswith("libc.musl-") and Path(filename).name == loader.name:
            if (bundle / "lib" / soname).read_bytes() != (bundle / "lib" / loader.name).read_bytes():
                raise RuntimeError("Packaged libc differs from the selected loader")
            continue
        if not loader and (soname in {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1", "libresolv.so.2"} or soname.startswith("libnss_")):
            continue
        if not Path(filename).resolve().is_relative_to(bundle):
            raise RuntimeError("Package still depends on an external library: " + filename)
    manifest = {
        "version": args.version, "architecture": architecture,
        "frontend": "GUI.Forms", "window_system": "X11; XWayland on Wayland desktops",
        "loader": loader.name if loader else "system glibc (Ubuntu 24.04 or newer)",
        "runtime_packages": subprocess.check_output(["apk", "info", "-v"], text=True).splitlines() if shutil.which("apk") else [],
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
