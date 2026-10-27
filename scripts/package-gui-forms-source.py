#!/usr/bin/env python3
"""Publish a tracked, reproducible native GUI.Forms build snapshot."""
from pathlib import Path
import argparse
import hashlib
import gzip
import io
import json
import subprocess
import tarfile

parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--revision", default="HEAD", help="Committed toolkit revision to package")
args = parser.parse_args()
source = args.source.resolve()
commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", args.revision + "^{commit}"], text=True).strip()
prefix = subprocess.check_output(["git", "-C", str(source), "rev-parse", "--show-prefix"], text=True).strip()
entries = subprocess.check_output(["git", "-C", str(source), "ls-tree", "-r", "-z", commit, "--", "."]).decode().split("\0")
tracked = {entry.split("\t", 1)[1]: entry.split(" ", 1)[0] for entry in entries if entry}
files = list(tracked)
roots = {"assets", "cmake", "src", "include", "tests", "third_party", "tools", "examples", "demo", "benchmarks", "fuzz", "generated", "manifests", "file_manager_demoboard"}
standalone = {"CMakeLists.txt", "CMakePresets.json", "LICENSE", "README.md"}
guides = {"docs/APPLICATION.md", "docs/ARCHITECTURE.md", "docs/CANVAS.md", "docs/LINUX_HOST.md", "docs/RAINSTAR_PORT.md", "docs/VALIDATION.md"}
selected = [name for name in files if name and (name.split("/", 1)[0] in roots or name in standalone or name in guides or name.startswith("docs/reference/contracts/"))]
# Read committed bytes, so an unrelated working edit never enters the release.
args.output.mkdir(parents=True, exist_ok=True)
archive_path = args.output / ("gui-forms-native-source-" + commit[:12] + ".tar.gz")
with archive_path.open("wb") as raw, gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0) as compressed, tarfile.open(fileobj=compressed, mode="w") as archive:
    for name in sorted(selected):
        data = subprocess.check_output(["git", "-C", str(source), "show", commit + ":" + prefix + name])
        info = tarfile.TarInfo("gui-forms/" + name)
        info.size = len(data)
        info.mode = 0o755 if tracked[name] == "100755" else 0o644
        archive.addfile(info, io.BytesIO(data))
    metadata = (json.dumps({"commit": commit, "scope": "Native GUI.Forms library, tests, fonts and pinned dependency build recipes", "historical_planning_and_private_specimens": "excluded"}, indent=2) + "\n").encode()
    info = tarfile.TarInfo("gui-forms/SOURCE_SNAPSHOT.json")
    info.size = len(metadata)
    archive.addfile(info, io.BytesIO(metadata))
print(archive_path)
print(hashlib.sha256(archive_path.read_bytes()).hexdigest())
