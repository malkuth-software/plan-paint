#!/usr/bin/env python3
"""The harness bootstrap's smoke build: the codec-free warp kernel and its test.

    python3 scripts/smoke-build.py [--jobs N]

Run from the workspace root (Plan Paint's checkout). Configures
.build/native-<platform>/plan-paint-smoke with PLAN_PAINT_SMOKE=ON (no image
codecs, no GUI.Forms), builds it with clang and ccache when present, and runs
paint-warp. It needs only CMake, Ninja and the LLVM 22.1.8 toolchain the
bootstrap restores. The harness toolchain file is used when the harness is
checked out at template_harness/; on macOS only when the LLVM runtime prefix
it needs is named (HARNESS_LLVM_RUNTIME or GUI_FORMS_LLVM_RUNTIME).

Exits nonzero when the configure, the build or the test fails; the build tree
is left in place for inspection.
"""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_FILE = ROOT / "template_harness" / "harness" / "cmake" / "llvm22.cmake"


def host_platform():
    systems = {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}
    system = systems.get(platform.system(), platform.system().lower())
    if system.startswith(("mingw", "msys", "cygwin")):
        system = "windows"
    architecture = "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x64"
    return system + "-" + architecture


def toolchain_arguments(host):
    if not TOOLCHAIN_FILE.is_file():
        return []
    if host.startswith("macos"):
        runtime = os.environ.get("HARNESS_LLVM_RUNTIME") or os.environ.get("GUI_FORMS_LLVM_RUNTIME")
        if not runtime:
            return []
        os.environ.setdefault("HARNESS_LLVM_RUNTIME", runtime)
    return ["-DCMAKE_TOOLCHAIN_FILE=" + TOOLCHAIN_FILE.as_posix()]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    args = parser.parse_args()
    host = host_platform()
    build = ROOT / ".build" / ("native-" + host) / "plan-paint-smoke"
    os.environ.setdefault("CC", "clang")
    os.environ.setdefault("CXX", "clang++")
    configure = ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 "-DPLAN_PAINT_SMOKE=ON"]
    configure.extend(toolchain_arguments(host))
    if shutil.which("ccache"):
        os.environ.setdefault("CCACHE_DIR", str(ROOT / ".ccache"))
        os.environ.setdefault("CCACHE_BASEDIR", str(ROOT))
        os.environ.setdefault("CCACHE_COMPILERCHECK", "content")
        configure.extend(["-DCMAKE_C_COMPILER_LAUNCHER=ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"])
    subprocess.run(configure, check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", str(args.jobs)], check=True)
    subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure", "--timeout", "180"], check=True)
    print("Smoke build passed: " + build.relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
