OKHSL reference implementation by Björn Ottosson, 2021.
Source: https://bottosson.github.io/misc/ok_color.h
Retrieved 2026-09-19. MIT license is retained in the header.
Local adaptation: scalar float types and float constants promoted to double,
FLT_MAX changed to DBL_MAX, source encoding normalized to UTF-8.
The Paint adapter handles achromatic input before the reference inverse.
Double-precision math functions replace the single-precision variants.
The published OKHSL equations and gamut approximation are preserved.
External sRGB colors may map just beyond saturation 1 near saturated blue;
the adapter retains that coordinate for exact round trips instead of silently
clipping an existing color. User plane input remains in the unit square.
