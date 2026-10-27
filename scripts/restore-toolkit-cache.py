#!/usr/bin/env python3
"""Import a pinned GUI.Forms compiler cache; unavailable caches compile from source."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = Path(".build/toolchain/llvm-22.1.8-macos14")


def validated_members(bundle, revision):
    members = bundle.getmembers()
    manifest = next((entry for entry in members if entry.name == "cache-manifest.json"), None)
    if manifest is None or not manifest.isfile():
        raise ValueError("Cache manifest is missing")
    with bundle.extractfile(manifest) as stream:
        if json.load(stream).get("revision") != revision:
            raise ValueError("Cache manifest revision differs from the source pin")
    selected = []
    for entry in members:
        name = PurePosixPath(entry.name)
        if name.is_absolute() or ".." in name.parts or "\\" in entry.name or ":" in entry.name:
            raise ValueError("Unsafe cache archive path")
        if not (entry.name == "cache-manifest.json" or name.parts[:1] == (".ccache",)
                or name.parts[:2] == (".build", "toolchain")):
            continue
        if not (entry.isfile() or entry.isdir() or entry.issym()):
            raise ValueError("Unsupported cache entry")
        if entry.issym() and ("/" in entry.linkname or "\\" in entry.linkname
                            or ":" in entry.linkname or entry.linkname.startswith(".")):
            raise ValueError("Unsafe runtime symlink")
        selected.append(entry)
    links = {entry.name for entry in selected if entry.issym()}
    for entry in selected:
        if any(parent.as_posix() in links for parent in PurePosixPath(entry.name).parents):
            raise ValueError("Cache entry traverses a symlink")
    return selected


def restore(platform, archive=None):
    lock = json.loads((ROOT / "third_party/gui-forms.lock.json").read_text())
    cache = lock["caches"][platform]
    with tempfile.TemporaryDirectory(prefix="paint-cache-") as temporary:
        staging = Path(temporary)
        if archive is None:
            if not shutil.which("gh"):
                return "GitHub CLI unavailable; compiling from source"
            result = subprocess.run(["gh", "release", "download", lock["cache_release"],
                "--repo", lock["repository"], "--pattern", cache["asset"], "--dir", str(staging)], check=False)
            if result.returncode:
                return "cache unavailable; compiling from source"
            archive = staging / cache["asset"]
        with archive.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != cache["sha256"]:
            raise ValueError("Compiler cache SHA-256 differs from the reviewed lock")
        extracted = staging / "extracted"
        with tarfile.open(archive) as bundle:
            members = validated_members(bundle, lock["commit"])
            bundle.extractall(extracted, members=members, filter="data")
        for relative in (Path(".ccache"), RUNTIME):
            source = extracted / relative
            target = ROOT / relative
            if not source.exists():
                continue
            if relative == RUNTIME and target.exists():
                continue
            if target.exists() and any(path.is_symlink() for path in target.rglob("*")):
                raise ValueError("Existing cache contains symlinks")
            if target.is_symlink():
                raise ValueError("Cache destination is a symlink")
            shutil.copytree(source, target, dirs_exist_ok=True, symlinks=True)
        shutil.copy2(extracted / "cache-manifest.json", ROOT / "cache-manifest.json")
    return "restored " + platform + " for " + lock["commit"]


def prepare_runtime():
    tools = ROOT / "gui_forms/tools"
    runtime = ROOT / RUNTIME
    check = subprocess.run([sys.executable, str(tools / "macos_runtime_manifest.py"), str(runtime)], check=False)
    if check.returncode:
        # Preserve a mismatched runtime for diagnosis; build into a new prefix.
        if runtime.exists():
            runtime.rename(runtime.with_name(runtime.name + "-rejected-" + uuid.uuid4().hex[:8]))
        work = Path(os.environ.get("RUNNER_TEMP", str(ROOT / ".build"))) / "paint-llvm-runtimes"
        subprocess.run([sys.executable, str(tools / "build_macos_runtimes.py"), "--work", str(work),
                        "--prefix", str(runtime), "--jobs", os.environ.get("BUILD_JOBS", "2")], check=True)
        subprocess.run([sys.executable, str(tools / "macos_runtime_manifest.py"), str(runtime)], check=True)
    subprocess.run([sys.executable, str(tools / "audit_macos_minimum.py"), str(runtime / "lib")], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=("windows-x64", "macos-arm64", "linux-x64", "linux-arm64"))
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    print(restore(args.platform, args.archive), flush=True)
    if args.platform == "macos-arm64":
        prepare_runtime()


if __name__ == "__main__":
    main()
