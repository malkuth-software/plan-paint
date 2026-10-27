#!/usr/bin/env python3
"""Fetch the standalone toolkit at the reviewed source revision."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=ROOT / "gui_forms")
    args = parser.parse_args()
    lock = json.loads((ROOT / "third_party/gui-forms.lock.json").read_text())
    destination = args.destination.resolve()
    if destination.exists():
        revision = subprocess.check_output(["git", "-C", str(destination), "rev-parse", "HEAD"], text=True).strip()
        changes = subprocess.check_output(["git", "-C", str(destination), "status", "--porcelain", "--untracked-files=no"], text=True)
        if revision != lock["commit"] or changes:
            raise SystemExit("Existing toolkit differs from the pin; supply a fresh --destination.")
    else:
        subprocess.run(["git", "init", str(destination)], check=True)
        # The checkout may be created by Windows Git and then read by MSYS2 Git.
        subprocess.run(["git", "-C", str(destination), "config", "core.autocrlf", "false"], check=True)
        subprocess.run(["git", "-C", str(destination), "remote", "add", "origin",
                        "https://github.com/" + lock["repository"] + ".git"], check=True)
        subprocess.run(["git", "-C", str(destination), "fetch", "--depth=1", "origin", lock["commit"]], check=True)
        subprocess.run(["git", "-C", str(destination), "checkout", "--detach", lock["commit"]], check=True)
    print(destination)

if __name__ == "__main__":
    main()
