#!/usr/bin/env python3
"""Select one numeric version before any native build.

Main publishes major.minor.(baseline_patch + run_number); reruns keep the same
version. This favors unique, reproducible CI identities over consecutive patch
numbers. Explicit tags must use the baseline series and cannot predate it.
Manual dispatch is a dry build unless publish=true; PRs use the baseline.
All fields fit Windows installer's 16-bit version components.
"""
import argparse
import json
import os
from pathlib import Path
import re


def validate(value):
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", value):
        raise ValueError("Release version must be a canonical numeric major.minor.patch")
    parts = tuple(map(int, value.split(".")))
    if any(part > 65535 for part in parts):
        raise ValueError("Release version exceeds portable installer component range")
    return parts


def baseline(root):
    source = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(PlanPaint VERSION ([0-9.]+)", source)
    if match is None:
        raise ValueError("Missing literal Plan Paint baseline version")
    validate(match.group(1))
    return match.group(1)


def select(base, event, ref, run_number, requested=False):
    major, minor, patch = validate(base)
    publish = event == "push" and (ref == "refs/heads/main" or ref.startswith("refs/tags/v"))
    publish = publish or (event == "workflow_dispatch" and requested and ref == "refs/heads/main")
    if event == "workflow_dispatch" and requested and ref != "refs/heads/main":
        raise ValueError("Manual publication is restricted to main")
    if ref.startswith("refs/tags/"):
        if not ref.startswith("refs/tags/v"):
            raise ValueError("Expected a v-prefixed release tag")
        version = ref[len("refs/tags/v"):]
        parts = validate(version)
        if parts[:2] != (major, minor) or parts[2] < patch:
            raise ValueError("Release tag must match the source baseline series and not predate it")
    elif publish:
        if type(run_number) is not int or run_number <= 0:
            raise ValueError("Publication requires a positive workflow run number")
        version = f"{major}.{minor}.{patch + run_number}"
    else:
        version = base
    validate(version)
    return {"version": version, "tag": "v" + version, "publish": str(publish).lower()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    data = select(baseline(args.root), os.environ.get("GITHUB_EVENT_NAME", "local"),
                  os.environ.get("GITHUB_REF", ""), int(os.environ.get("GITHUB_RUN_NUMBER", "0")),
                  os.environ.get("PUBLISH_REQUESTED") == "true")
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            for key, value in data.items():
                output.write(f"{key}={value}\n")
    print(json.dumps(data))


if __name__ == "__main__":
    main()
