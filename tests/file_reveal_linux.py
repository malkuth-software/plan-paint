#!/usr/bin/env python3
"""Exercise the real Linux launcher with isolated desktop command fixtures."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

executable = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="paint-reveal-linux-") as temporary:
    root = Path(temporary)
    asset = root / "café, % # '$ (art).png"
    asset.write_bytes(b"not an image: reveal must not decode")
    log = root / "requests.jsonl"
    script = f"#!{sys.executable}\n" + '''import json, os, sys, time
from pathlib import Path
name = Path(sys.argv[0]).name
with open(os.environ["REVEAL_TEST_LOG"], "a") as log:
    log.write(json.dumps([name, *sys.argv[1:]]) + "\\n")
mode = os.environ.get("REVEAL_TEST_MODE", "selection")
if name == "dbus-send" and mode == "timeout": time.sleep(10)
sys.exit(0 if mode == "selection" or (name == "xdg-open" and mode != "failure") else 1)
'''
    for command in ("dbus-send", "xdg-open"):
        file = root / command
        file.write_text(script)
        file.chmod(0o700)
    env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ["PATH"], REVEAL_TEST_LOG=str(log))
    for mode in ("selection", "fallback", "failure", "timeout"):
        log.write_text("")
        result = subprocess.run([executable, "--native", str(asset)], env=dict(env, REVEAL_TEST_MODE=mode),
                                capture_output=True, text=True, timeout=12)
        calls = [json.loads(line) for line in log.read_text().splitlines()]
        assert calls[0][-2:] == ["array:string:" + asset.as_uri(), "string:"], calls
        if mode == "selection":
            assert result.returncode == 0 and result.stdout.strip() == "selection" and len(calls) == 1, result
        else:
            assert calls[1] == ["xdg-open", root.as_uri()] and len(calls) == 2, calls
            assert (result.returncode != 0) == (mode == "failure"), result
            if mode != "failure": assert result.stdout.strip() == "folder", result
        assert asset.read_bytes() == b"not an image: reveal must not decode"
print("Linux file-manager selection, fallback, failure and timeout contracts passed")
