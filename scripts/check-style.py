#!/usr/bin/env python3
"""Check the mechanical exclusions in the supplied house style, not vendor code."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
failures = []
for folder in (ROOT / "src", ROOT / "tests", ROOT / "benchmarks"):
    for path in folder.rglob("*"):
        if path.suffix not in {".cpp", ".hpp", ".mm"}:
            continue
        source = path.read_text(encoding="utf-8")
        # Remove comments and literals so documentation does not trigger syntax checks.
        source = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', " ", source, flags=re.S)
        for name, pattern in (("auto", r"\bauto\b"), ("arrow member access", r"->"),
                              ("lambda", r"\[[^\]]*\]\s*(?:\([^;{}]*\))?\s*(?:mutable\s*)?\{"),
                              ("structured binding", r"\b(?:const\s+)?auto\s*[&]*\s*\[")):
            if re.search(pattern, source):
                failures.append(f"{path.relative_to(ROOT)}: prohibited {name}")
if failures:
    print("\n".join(failures))
    sys.exit(1)
print("First-party C++ house-style exclusions pass.")
