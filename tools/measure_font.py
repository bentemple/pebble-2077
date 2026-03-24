#!/usr/bin/env python3
"""
Pixel-perfect font measurement tool for Pebble watchface development.

Renders characters at exact pixel sizes and measures their dimensions,
outputting values suitable for compile-time constants.

Usage:
    python measure_font.py <font_path> <font_size> [characters]

Examples:
    python measure_font.py resources/fonts/Rajdhani-Medium.ttf 86
    python measure_font.py resources/fonts/Orbitron-SemiBold.ttf 17 "0123456789:.FC-"
"""

import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Error: Pillow not installed. Run: pip install Pillow")
    sys.exit(1)

try:
    from fontTools.ttLib import TTFont
    HAS_FONTTOOLS = True
except ImportError:
    HAS_FONTTOOLS = False

# Default characters to measure
DEFAULT_CHARS = "0123456789:.FC"


def measure_glyph_bbox(font: ImageFont.FreeTypeFont, char: str) -> dict:
    """
    Measure the pixel-perfect bounding box of a rendered glyph.

    Returns dict with:
        - advance_width: horizontal distance to next character (what Pebble uses)
        - bbox_width: actual rendered pixel width (ink bounds)
        - bbox_height: actual rendered pixel height (ink bounds)
        - left_bearing: pixels from origin to left edge of glyph
        - top_bearing: pixels from baseline to top of glyph
    """
    # Get the advance width (distance to next character origin)
    # This is what Pebble uses for text layout
    bbox = font.getbbox(char)
    if bbox is None:
        return None

    left, top, right, bottom = bbox

    # getlength() returns the advance width (horizontal distance to next glyph)
    advance_width = font.getlength(char)

    return {
        "char": char,
        "advance_width": int(round(advance_width)),
        "bbox_width": right - left,
        "bbox_height": bottom - top,
        "left_bearing": left,
        "top_bearing": top,
        "right_edge": right,
        "bottom_edge": bottom,
    }


def read_font_kerning_table(font_path: str, characters: str) -> dict:
    """
    Read the actual kerning values from the font's KERN or GPOS tables.
    Returns dict mapping (char1, char2) -> kerning_value in font units.
    """
    if not HAS_FONTTOOLS:
        return {}

    tt = TTFont(font_path)
    kerning = {}

    # Try kern table first (older format)
    if "kern" in tt:
        kern_table = tt["kern"]
        for subtable in kern_table.kernTables:
            if hasattr(subtable, "kernTable"):
                for (left, right), value in subtable.kernTable.items():
                    if left in characters and right in characters:
                        kerning[(left, right)] = value

    # Try GPOS table (newer OpenType format)
    if "GPOS" in tt:
        gpos = tt["GPOS"]
        if hasattr(gpos.table, "LookupList") and gpos.table.LookupList:
            for lookup in gpos.table.LookupList.Lookup:
                if lookup.LookupType == 2:  # Pair adjustment
                    for subtable in lookup.SubTable:
                        if hasattr(subtable, "PairSet"):
                            # Format 1: specific pairs
                            for glyph, pairset in subtable.PairSet.items():
                                if glyph in characters:
                                    for record in pairset.PairValueRecord:
                                        second = record.SecondGlyph
                                        if second in characters:
                                            val1 = record.Value1
                                            if val1 and hasattr(val1, "XAdvance"):
                                                kerning[(glyph, second)] = val1.XAdvance

    tt.close()
    return kerning


def get_font_units_per_em(font_path: str) -> int:
    """Get the font's units per em for converting kerning to pixels."""
    if not HAS_FONTTOOLS:
        return 1000  # Common default

    tt = TTFont(font_path)
    upem = tt["head"].unitsPerEm
    tt.close()
    return upem


def measure_rendered_width(font: ImageFont.FreeTypeFont, text: str) -> int:
    """
    Measure actual pixel width by rendering to bitmap and counting.
    This is the ground truth - no floating point rounding issues.
    """
    # Render with plenty of padding
    bbox = font.getbbox(text)
    if bbox is None:
        return 0

    left, top, right, bottom = bbox
    width = right - left + 20
    height = bottom - top + 20

    img = Image.new("L", (width, height), color=0)
    draw = ImageDraw.Draw(img)
    draw.text((10 - left, 10 - top), text, font=font, fill=255)

    # Find rightmost non-zero pixel
    pixels = img.load()
    rightmost = 0
    for x in range(width):
        for y in range(height):
            if pixels[x, y] > 0:
                rightmost = max(rightmost, x)

    # Find leftmost non-zero pixel
    leftmost = width
    for x in range(width):
        for y in range(height):
            if pixels[x, y] > 0:
                leftmost = min(leftmost, x)
                break

    return rightmost - leftmost + 1 if rightmost >= leftmost else 0


def measure_pixel_kerning(font: ImageFont.FreeTypeFont, char1: str, char2: str) -> dict:
    """
    Measure true pixel-level kerning by rendering bitmaps.

    Returns:
        - pair_width: actual rendered width of the pair
        - sum_width: sum of individual character widths
        - kerning: difference (negative = characters overlap)
    """
    pair_width = measure_rendered_width(font, char1 + char2)
    char1_width = measure_rendered_width(font, char1)
    char2_width = measure_rendered_width(font, char2)

    return {
        "pair": char1 + char2,
        "pair_width": pair_width,
        "char1_width": char1_width,
        "char2_width": char2_width,
        "sum_width": char1_width + char2_width,
        "kerning": pair_width - (char1_width + char2_width),
    }


def render_glyph_image(font: ImageFont.FreeTypeFont, char: str, padding: int = 10) -> Image.Image:
    """Render a single glyph to an image for visual inspection."""
    # Get bounding box to size the image
    bbox = font.getbbox(char)
    if bbox is None:
        return None

    left, top, right, bottom = bbox
    width = right - left + padding * 2
    height = bottom - top + padding * 2

    # Create image with baseline marker
    img = Image.new("RGB", (max(width, 50), max(height, 50)), color=(40, 40, 40))
    draw = ImageDraw.Draw(img)

    # Draw the character
    # Offset to account for bearing and padding
    x = padding - left
    y = padding - top
    draw.text((x, y), char, font=font, fill=(255, 255, 255))

    # Draw advance width marker (red line)
    advance = int(font.getlength(char))
    draw.line([(padding + advance, 0), (padding + advance, img.height)], fill=(255, 0, 0), width=1)

    # Draw origin marker (green line)
    draw.line([(padding, 0), (padding, img.height)], fill=(0, 255, 0), width=1)

    return img


def measure_font(font_path: str, font_size: int, characters: str = DEFAULT_CHARS) -> list[dict]:
    """Load font and measure all specified characters."""
    path = Path(font_path)
    if not path.exists():
        raise FileNotFoundError(f"Font not found: {font_path}")

    font = ImageFont.truetype(str(path), font_size)

    results = []
    for char in characters:
        metrics = measure_glyph_bbox(font, char)
        if metrics:
            results.append(metrics)
        else:
            print(f"Warning: Could not measure '{char}'", file=sys.stderr)

    return results


def format_c_defines(results: list[dict], font_name: str, font_size: int) -> str:
    """Format results as C preprocessor defines."""
    lines = [f"// {font_name} {font_size}pt character widths (advance width)"]
    lines.append(f"// Generated by measure_font.py")
    lines.append("")

    char_names = {
        "0": "0", "1": "1", "2": "2", "3": "3", "4": "4",
        "5": "5", "6": "6", "7": "7", "8": "8", "9": "9",
        ":": "COLON", ".": "DOT", "F": "F", "C": "C",
        "-": "MINUS", "/": "SLASH", " ": "SPACE",
    }

    prefix = font_name.upper().replace("-", "_")

    for m in results:
        char = m["char"]
        name = char_names.get(char, f"CHAR_{ord(char):02X}")
        define_name = f"{prefix}_{font_size}_WIDTH_{name}"
        lines.append(f"#define {define_name} {m['advance_width']}")

    return "\n".join(lines)


def format_c_array(results: list[dict], font_name: str, font_size: int) -> str:
    """Format digit widths as a C array (for 0-9)."""
    digits = [m for m in results if m["char"].isdigit()]
    digits.sort(key=lambda m: m["char"])

    if len(digits) != 10:
        return "// Array format requires all digits 0-9"

    prefix = font_name.lower().replace("-", "_")
    lines = [
        f"// {font_name} {font_size}pt digit widths",
        f"static const int s_{prefix}_{font_size}_digit_widths[10] = {{",
    ]

    for i, m in enumerate(digits):
        comma = "," if i < 9 else ""
        lines.append(f"  {m['advance_width']}{comma}  // {m['char']}")

    lines.append("};")
    return "\n".join(lines)


def format_table(results: list[dict]) -> str:
    """Format results as a readable table."""
    lines = [
        "Character Measurements (pixels):",
        "-" * 70,
        f"{'Char':<6} {'Advance':<10} {'BBox W':<10} {'BBox H':<10} {'L-Bear':<10} {'T-Bear':<10}",
        "-" * 70,
    ]

    for m in results:
        char_display = repr(m["char"]) if m["char"] in " \t\n" else m["char"]
        lines.append(
            f"{char_display:<6} {m['advance_width']:<10} {m['bbox_width']:<10} "
            f"{m['bbox_height']:<10} {m['left_bearing']:<10} {m['top_bearing']:<10}"
        )

    return "\n".join(lines)


def save_glyph_images(font_path: str, font_size: int, characters: str, output_dir: Path):
    """Save rendered glyph images for visual inspection."""
    output_dir.mkdir(parents=True, exist_ok=True)

    font = ImageFont.truetype(font_path, font_size)

    for char in characters:
        img = render_glyph_image(font, char)
        if img:
            # Create safe filename
            if char.isalnum():
                filename = f"glyph_{char}.png"
            else:
                filename = f"glyph_0x{ord(char):02X}.png"
            img.save(output_dir / filename)
            print(f"Saved: {output_dir / filename}")


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Measure font character dimensions for Pebble development"
    )
    parser.add_argument("font_path", help="Path to TTF font file")
    parser.add_argument("font_size", type=int, help="Font size in points")
    parser.add_argument(
        "characters",
        nargs="?",
        default=DEFAULT_CHARS,
        help=f"Characters to measure (default: {DEFAULT_CHARS})"
    )
    parser.add_argument(
        "--format", "-f",
        choices=["table", "defines", "array", "all"],
        default="all",
        help="Output format"
    )
    parser.add_argument(
        "--images", "-i",
        metavar="DIR",
        help="Save glyph images to directory for visual inspection"
    )
    parser.add_argument(
        "--kerning", "-k",
        action="store_true",
        help="Show kerning pairs between digits"
    )

    args = parser.parse_args()

    # Resolve font path relative to script location or current dir
    font_path = Path(args.font_path)
    if not font_path.exists():
        # Try relative to project root
        script_dir = Path(__file__).parent
        project_root = script_dir.parent
        font_path = project_root / args.font_path

    if not font_path.exists():
        print(f"Error: Font not found: {args.font_path}", file=sys.stderr)
        sys.exit(1)

    # Measure characters
    results = measure_font(str(font_path), args.font_size, args.characters)

    # Get font name from path
    font_name = font_path.stem

    # Output based on format
    if args.format in ("table", "all"):
        print(f"\nFont: {font_name}")
        print(f"Size: {args.font_size}pt")
        print()
        print(format_table(results))

    if args.format in ("defines", "all"):
        print()
        print(format_c_defines(results, font_name, args.font_size))

    if args.format in ("array", "all"):
        print()
        print(format_c_array(results, font_name, args.font_size))

    # Show kerning info if requested
    if args.kerning:
        font = ImageFont.truetype(str(font_path), args.font_size)
        digits = "0123456789"

        # Method 1: Read actual kerning table from font
        print("\n=== Font Kerning Table Analysis ===")
        if HAS_FONTTOOLS:
            kern_table = read_font_kerning_table(str(font_path), digits)
            upem = get_font_units_per_em(str(font_path))

            if kern_table:
                print(f"Found {len(kern_table)} kerning pairs in font (units per em: {upem})")
                print("-" * 60)
                for (c1, c2), value in sorted(kern_table.items()):
                    # Convert font units to pixels at this size
                    px_value = (value * args.font_size) / upem
                    print(f"  '{c1}{c2}': {value} units = {px_value:+.1f}px at {args.font_size}pt")
            else:
                print("No kerning pairs found in font tables (kern/GPOS)")
                print("This font likely has NO built-in kerning for digits.")
        else:
            print("fonttools not installed - cannot read kerning tables")
            print("Install with: pip install fonttools")

        # Method 2: High-precision advance width comparison
        print("\n=== Advance Width Kerning Analysis ===")
        print("(comparing getlength() for pairs vs sum of individuals)")
        print("-" * 60)

        kerning_found = False
        for d1 in digits:
            for d2 in digits:
                pair = d1 + d2
                # Use raw float values for precision
                pair_len = font.getlength(pair)
                d1_len = font.getlength(d1)
                d2_len = font.getlength(d2)
                diff = pair_len - (d1_len + d2_len)

                # Only show if there's actual kerning (> 0.1px)
                if abs(diff) > 0.1:
                    kerning_found = True
                    print(f"  '{pair}': {diff:+.2f}px  "
                          f"(pair={pair_len:.1f}, sum={d1_len + d2_len:.1f})")

        if not kerning_found:
            print("No significant kerning detected in advance widths.")
            print("\nConclusion: KERNING_ADJUST in your code is a MANUAL")
            print("design choice, not derived from font metrics.")

    # Save images if requested
    if args.images:
        save_glyph_images(str(font_path), args.font_size, args.characters, Path(args.images))


if __name__ == "__main__":
    main()
