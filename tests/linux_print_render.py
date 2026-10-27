#!/usr/bin/env python3
"""Validate Paint's actual PostScript with Ghostscript, including paper orientation."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="rainstar-print-check-") as directory:
    ps = Path(directory) / "job.ps"
    ppm = Path(directory) / "page.ppm"
    subprocess.run([sys.argv[1], str(ps)], check=True)
    subprocess.run([sys.argv[2], "-dSAFER", "-dBATCH", "-dNOPAUSE", "-sDEVICE=ppmraw", "-r72",
                    "-sOutputFile=" + str(ppm), str(ps)], check=True)
    with ppm.open("rb") as source:
        assert source.readline().strip() == b"P6"
        numbers = []
        while len(numbers) < 3:
            line = source.readline()
            if not line.startswith(b"#"):
                numbers.extend(int(part) for part in line.split())
        width, height, maximum = numbers
        pixels = source.read()
    assert abs(width - 200 * 72 / 25.4) < 1 and abs(height - 100 * 72 / 25.4) < 1
    assert maximum == 255 and len(pixels) == width * height * 3
    def color(x, y):
        position = (y * width + x) * 3
        return tuple(pixels[position:position+3])
    assert color(width//2-45, height//2-45) == (255, 0, 0), "Top-left orientation"
    assert color(width//2+45, height//2-45) == (0, 255, 0), "Top-right orientation"
    assert color(width//2-45, height//2+45) == (0, 0, 255), "Bottom-left orientation"
    assert color(width//2+45, height//2+45) == (255, 255, 255), "Alpha must flatten onto paper"
    assert color(5, height//2) == color(width//2, 5) == (255, 255, 255), "Margins must be white"
    print("PostScript: paper size, aspect ratio, orientation, margins, alpha flattening and document preservation passed")
