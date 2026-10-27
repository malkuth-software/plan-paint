#!/usr/bin/env python3
"""Encode the owner-supplied Plan Paint artwork as application icons."""
from pathlib import Path
from PIL import Image
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
assets = ROOT / 'assets'
source = Image.open(assets / 'app-icon-source.png').convert('RGBA')
# Keep the whole supplied design, including its transparent margin.
image = source.resize((1024, 1024), Image.Resampling.LANCZOS)
image.save(assets / 'app-icon.png')
image.save(assets / 'app-icon.ico', sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)])
if sys.platform == 'darwin':
    iconset = ROOT / 'dist/PlanPaint.iconset'
    iconset.mkdir(parents=True, exist_ok=True)
    for side in (16,32,128,256,512):
        image.resize((side,side), Image.Resampling.LANCZOS).save(iconset / f'icon_{side}x{side}.png')
        image.resize((side*2,side*2), Image.Resampling.LANCZOS).save(iconset / f'icon_{side}x{side}@2x.png')
    subprocess.run(['iconutil','-c','icns',str(iconset),'-o',str(assets / 'app-icon.icns')],check=True)
