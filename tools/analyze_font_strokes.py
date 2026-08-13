#!/usr/bin/env python3
"""
Stroke-consistency analysis for Pebble bitmap fonts.

Pebble rasterizes fonts to 1-bit bitmaps at build time. At small sizes a
stroke whose ideal width falls between 1 and 2 pixels rounds one way on
some glyphs and the other way on others, which reads as uneven weight -
some bars look twice as heavy as their neighbours.

This renders each glyph exactly as the SDK does (FreeType monochrome,
no antialiasing) and measures every stroke, so a size can be chosen on
evidence rather than by eye.

A "run" is a maximal span of set pixels:
  - runs along a ROW   = vertical stem widths
  - runs along a COLUMN = horizontal bar thicknesses

The goal is every stroke landing on exactly 1px. A size where strokes
are split between 1 and 2 is what produces the uneven look.

Usage:
    python analyze_font_strokes.py <font.ttf> [more.ttf ...] \\
        --sizes 10 11 12 13 14 --chars 0123456789
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Error: Pillow not installed. Run: pip install Pillow")
    sys.exit(1)

# Strokes are thin; anything wider is a bowl or counter, not a stroke.
MAX_STROKE_PX = 3


def render_mono(font, char):
    """Render one glyph the way the Pebble font generator does: 1-bit,
    no antialiasing. Returns a list of rows of 0/1."""
    # Generous canvas; the glyph is cropped to its ink bounds after.
    size = font.size
    img = Image.new("1", (size * 3, size * 3), 0)
    draw = ImageDraw.Draw(img)
    draw.fontmode = "1"  # "1" = no antialiasing (FreeType MONO target)
    draw.text((size, size), char, font=font, fill=1)

    bbox = img.getbbox()
    if not bbox:
        return []
    img = img.crop(bbox)
    px = img.load()
    w, h = img.size
    return [[1 if px[x, y] else 0 for x in range(w)] for y in range(h)]


def runs(seq):
    """Lengths of maximal spans of 1s."""
    out, n = [], 0
    for v in seq:
        if v:
            n += 1
        elif n:
            out.append(n)
            n = 0
    if n:
        out.append(n)
    return out


def analyze(font_path, size, chars):
    try:
        font = ImageFont.truetype(str(font_path), size)
    except OSError as e:
        return {"error": str(e)}

    stems, bars = [], []  # vertical stem widths, horizontal bar thicknesses
    heights = set()

    for ch in chars:
        grid = render_mono(font, ch)
        if not grid:
            continue
        heights.add(len(grid))
        w = len(grid[0])

        for row in grid:
            stems += [r for r in runs(row) if r <= MAX_STROKE_PX]
        for x in range(w):
            col = [grid[y][x] for y in range(len(grid))]
            bars += [r for r in runs(col) if r <= MAX_STROKE_PX]

    def dist(vals):
        if not vals:
            return {}
        return {n: vals.count(n) for n in sorted(set(vals))}

    def purity(vals):
        """Fraction of strokes at the most common width. 1.0 = uniform."""
        if not vals:
            return 0.0
        d = dist(vals)
        return max(d.values()) / len(vals)

    return {
        "stems": dist(stems),
        "bars": dist(bars),
        "stem_purity": purity(stems),
        "bar_purity": purity(bars),
        "glyph_heights": sorted(heights),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fonts", nargs="+")
    ap.add_argument("--sizes", nargs="+", type=int,
                    default=[10, 11, 12, 13, 14, 15, 16])
    ap.add_argument("--chars", default="0123456789")
    args = ap.parse_args()

    for font_path in args.fonts:
        p = Path(font_path)
        print(f"\n{'=' * 72}\n{p.name}\n{'=' * 72}")
        print(f"{'size':>5} {'H':>6} {'stems (w:count)':<22} "
              f"{'bars (h:count)':<22} {'uniform?':<10}")
        print("-" * 72)

        for size in args.sizes:
            r = analyze(p, size, args.chars)
            if "error" in r:
                print(f"{size:>5} ERROR: {r['error']}")
                continue

            h = ",".join(str(x) for x in r["glyph_heights"])
            stems = " ".join(f"{k}:{v}" for k, v in r["stems"].items())
            bars = " ".join(f"{k}:{v}" for k, v in r["bars"].items())

            # Clean means: every stroke is 1px, in both directions.
            all_one = (list(r["stems"]) == [1] and list(r["bars"]) == [1])
            mixed_h = len(r["glyph_heights"]) > 1
            if all_one and not mixed_h:
                verdict = "CLEAN"
            elif all_one:
                verdict = "clean/ragged-h"
            else:
                verdict = (f"mixed "
                           f"{r['stem_purity']:.0%}/{r['bar_purity']:.0%}")

            print(f"{size:>5} {h:>6} {stems:<22} {bars:<22} {verdict:<10}")

    print("\nCLEAN = every stroke exactly 1px and all digits the same height.")
    print("mixed X%/Y% = share of stems/bars at the most common width;")
    print("              anything below 100% renders with uneven weight.")


if __name__ == "__main__":
    main()
