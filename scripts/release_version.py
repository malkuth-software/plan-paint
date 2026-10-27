#!/usr/bin/env python3
"""Read the package version from the CMake project declaration."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def project_version():
    source = (ROOT / "CMakeLists.txt").read_text()
    match = re.search(r"project\(PlanPaint VERSION ([0-9]+\.[0-9]+\.[0-9]+)", source)
    if match is None:
        raise RuntimeError("CMake project version is missing")
    return match.group(1)

if __name__ == "__main__":
    print(project_version())
