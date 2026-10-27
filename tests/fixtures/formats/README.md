# Format fixtures

Original test artwork by Astra, MIT licensed with Rainstar Paint. `quadrants.png` is a 64 × 48 red/blue orientation target. Its AVIF was encoded by FFmpeg/SVT-AV1 4.2.0 (one frame, CRF 1), its lossless WebP by libwebp `cwebp` 1.6.0, and its HEIC by macOS `sips`. `import.svg` exercises intrinsic dimensions, gradients, clipping, opacity, transforms, text and blur. The two eight-pixel animation fixtures were generated with Pillow and must be rejected by the importer.

The legacy 1-bit CUR fixture is independently assembled in `tests/format_tests.cpp`, with explicit XOR/AND rows, palette and hotspot fields.
