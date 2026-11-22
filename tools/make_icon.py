#!/usr/bin/env python3
"""Draws the app's icon and writes every file made from it (task 8-3b).

    python tools/make_icon.py [--preview <file.png>]

The icon is the product site's mark (https://audio.dezign.jp/, its favicon):
a vacuum tube drawn in amber lines on a dark rounded plate. The site draws it
with thin lines (3.2 px on a 64 px plate); here the lines are thicker, so the
tube still reads on a taskbar (the user, 2026-11-21). Smaller sizes use
simpler drawings, as the filament (under 48 px) and three pins (at 24 px and
under) would run together into one blot: MEDIUM leaves out the filament,
SMALL also has two pins in place of three. Up to 48 px the lines are kept on
the pixel grid.

The drawing is set down once, below, in the site's own units (its SVG's
viewBox, 22 x 26). Both the SVG files and the pictures are made from it, so
they cannot drift apart. The pictures are drawn 16 times larger and averaged
down, square by square.

What comes out:
  app/resources/icon/icon.svg         the drawing, large sizes
  app/resources/icon/icon-medium.svg  the drawing, 25 to 40 px
  app/resources/icon/icon-small.svg   the drawing, 24 px and under
  app/resources/icon/icon.ico         the executable's icon: 16, 20, 24, 32,
                                      40, 48, 64, 256 (app/resources/version.rc.in);
                                      256 as PNG, the others as bitmaps
  packaging/msix/Assets/*.png         the MSIX package's pictures (task 8-3c):
                                      Square44x44Logo (each scale, each
                                      targetsize, plated and unplated),
                                      Square150x150Logo, StoreLogo (each scale)
  packaging/store/logos/app-tile-300.png  the Store listing's 1:1 app tile icon
--preview writes a sheet of every size, on a light and a dark background, for
looking at by eye.
"""

from __future__ import annotations

import argparse
import io
import math
import struct
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ICON_DIR = ROOT / "app" / "resources" / "icon"
MSIX_ASSETS = ROOT / "packaging" / "msix" / "Assets"
STORE_LOGOS = ROOT / "packaging" / "store" / "logos"

PLATE = (0x0C, 0x09, 0x07)  # the site's favicon
AMBER = (0xE8, 0xA2, 0x4A)
SUPERSAMPLE = 16


@dataclass(frozen=True)
class Drawing:
    """One drawing of the tube, in the site's units (viewBox 0 0 22 26)."""

    name: str
    stroke: float  # line width, in the site's units (the site's own: 1.52)
    pins: tuple[float, ...]  # x of each pin under the base
    filament: bool

    def strokes(self, at) -> list[list[tuple[float, float]]]:
        """Every line as a list of points in pixels; `at` places a point.
        The arc is made in pixels, from where its two ends were placed, so it
        still meets the sides when they are moved onto the pixel grid."""
        # The glass: up from (5,21) to (5,9), over the top to (17,9), down to
        # (17,21). (SVG: M5 21V9a6 6 0 0 1 12 0v12)
        (lx, ly), (rx, _) = at((5, 9)), at((17, 9))
        cx, r = (lx + rx) / 2, (rx - lx) / 2
        arc = [(cx - r * math.cos(a), ly - r * math.sin(a)) for a in (math.pi * i / 64 for i in range(65))]
        lines = [[at((5, 21))] + arc + [at((17, 21))]]
        lines.append([at((3, 21)), at((19, 21))])  # the base (M3 21h16)
        lines += [[at((x, 25)), at((x, 21))] for x in self.pins]  # the pins (M7 25v-4 ...)
        if self.filament:
            lines.append([at(p) for p in ((9, 17), (9, 12), (11, 10), (13, 12), (13, 17))])  # M9 17v-5l2-2 2 2v5
        return lines


LARGE = Drawing("icon", stroke=2.2, pins=(7, 11, 15), filament=True)
MEDIUM = Drawing("icon-medium", stroke=2.6, pins=(7, 11, 15), filament=False)
SMALL = Drawing("icon-small", stroke=2.8, pins=(8, 14), filament=False)
SMALL_UP_TO = 24  # px: this size and under use SMALL
MEDIUM_UP_TO = 40  # px: from here down to SMALL_UP_TO use MEDIUM
HINT_UP_TO = 48  # px: this size and under keep the lines on the pixel grid

ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 256)


def drawing_for(size: int) -> Drawing:
    if size <= SMALL_UP_TO:
        return SMALL
    return MEDIUM if size <= MEDIUM_UP_TO else LARGE


def plate_inset(size: int) -> int:
    """Transparent edge round the plate, whole pixels (1 px at 16, 16 at 256)."""
    return round(size / 16)


# The site's favicon puts the tube on its 64 px plate at translate(9 5)
# scale(2.1). Here the tube is centred on the plate instead (its lines run
# from y 3 to 25 and x 3 to 19), at the same size relative to the plate.
UNITS_PER_PLATE = 64 / 2.1
GLYPH_CENTRE = (11.0, 14.0)
PLATE_RADIUS = 12 / 64


def render(size: int, drawing: Drawing | None = None, plate: bool = True) -> Image.Image:
    drawing = drawing or drawing_for(size)
    big = size * SUPERSAMPLE
    inset = plate_inset(size) * SUPERSAMPLE
    side = big - 2 * inset
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    if plate:
        draw.rounded_rectangle([inset, inset, big - inset - 1, big - inset - 1], radius=side * PLATE_RADIUS, fill=PLATE + (255,))
    scale = side / UNITS_PER_PLATE
    centre = big / 2
    # Up to HINT_UP_TO px every line is a whole number of pixels wide and sits
    # on whole pixels (an odd width on a pixel's middle, an even one on the
    # line between two), so it is sharp, not smeared over two pixels.
    hint = size <= HINT_UP_TO
    width_px = drawing.stroke * scale / SUPERSAMPLE
    if hint:
        width_px = max(1, round(width_px))
    offset = 0.5 if width_px % 2 == 1 else 0.0

    def snap(v: float) -> float:
        if not hint:
            return v
        px = v / SUPERSAMPLE
        return (math.floor(px - offset + 0.5) + offset) * SUPERSAMPLE

    def at(p: tuple[float, float]) -> tuple[float, float]:
        return (snap(centre + (p[0] - GLYPH_CENTRE[0]) * scale), snap(centre + (p[1] - GLYPH_CENTRE[1]) * scale))

    width = width_px * SUPERSAMPLE
    r = width / 2
    for line in drawing.strokes(at):
        for a, b in zip(line, line[1:]):
            draw.line([a, b], fill=AMBER + (255,), width=max(1, round(width)))
        for x, y in line:  # round caps and joins
            draw.ellipse([x - r, y - r, x + r, y + r], fill=AMBER + (255,))
    return img.resize((size, size), Image.Resampling.BOX)


def svg(drawing: Drawing) -> str:
    """The drawing as SVG on a 64-unit plate, as the site's favicon is set out."""
    s = 64 / UNITS_PER_PLATE
    tx, ty = 32 - GLYPH_CENTRE[0] * s, 32 - GLYPH_CENTRE[1] * s
    paths = ["M5 21V9a6 6 0 0 1 12 0v12", "M3 21h16", " ".join(f"M{x:g} 25v-4" for x in drawing.pins)]
    if drawing.filament:
        paths.append("M9 17v-5l2-2 2 2v5")
    body = "\n".join(f'    <path d="{d}"/>' for d in paths)
    return (
        f"<!-- Made by tools/make_icon.py ({drawing.name}). Do not edit by hand. -->\n"
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">\n'
        f'  <rect width="64" height="64" rx="12" fill="#{"".join(f"{c:02x}" for c in PLATE)}"/>\n'
        f'  <g transform="translate({tx:.4g} {ty:.4g}) scale(2.1)" fill="none" stroke="#{"".join(f"{c:02x}" for c in AMBER)}"'
        f' stroke-width="{drawing.stroke:g}" stroke-linecap="round" stroke-linejoin="round">\n'
        f"{body}\n  </g>\n</svg>\n"
    )


# The MSIX package's pictures. Square44x44Logo is the app list's and the
# taskbar's; its targetsize ones are drawn for that pixel size exactly. The
# tile (Square150x150Logo) keeps the icon at two thirds of the tile, as the
# system's own tiles do.
SCALES = {100: 1.0, 125: 1.25, 150: 1.5, 200: 2.0, 400: 4.0}
TARGET_SIZES = (16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 256)


def tile(size: int, icon_fraction: float) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    inner = round(size * icon_fraction)
    icon = render(inner)
    img.alpha_composite(icon, ((size - inner) // 2, (size - inner) // 2))
    return img


def half_up(v: float) -> int:
    """62.5 -> 63, as Microsoft's table of sizes has it (round() would give 62)."""
    return math.floor(v + 0.5)


def msix_assets() -> dict[str, Image.Image]:
    out: dict[str, Image.Image] = {}
    for pct, k in SCALES.items():
        out[f"Square44x44Logo.scale-{pct}.png"] = render(half_up(44 * k))
        out[f"Square150x150Logo.scale-{pct}.png"] = tile(half_up(150 * k), 2 / 3)
        out[f"StoreLogo.scale-{pct}.png"] = render(half_up(50 * k))
    for t in TARGET_SIZES:
        out[f"Square44x44Logo.targetsize-{t}.png"] = render(t)
        out[f"Square44x44Logo.targetsize-{t}_altform-unplated.png"] = render(t)
        out[f"Square44x44Logo.targetsize-{t}_altform-lightunplated.png"] = render(t)
    return out


def preview(path: Path) -> None:
    sizes = (16, 20, 24, 32, 40, 48, 64, 256)
    pad = 16
    width = sum(sizes) + pad * (len(sizes) + 1)
    row = 256 + 2 * pad
    sheet = Image.new("RGBA", (width, row * 2), (0, 0, 0, 255))
    for i, bg in enumerate([(243, 243, 243, 255), (28, 28, 28, 255)]):
        band = Image.new("RGBA", (width, row), bg)
        x = pad
        for s in sizes:
            band.alpha_composite(render(s), (x, row - pad - s))
            x += s + pad
        sheet.alpha_composite(band, (0, i * row))
    # The small sizes again, each pixel as an 8 x 8 square, to see them exactly.
    zoom = [render(s).resize((s * 8, s * 8), Image.Resampling.NEAREST) for s in (16, 20, 24, 32)]
    zw = sum(z.width for z in zoom) + pad * (len(zoom) + 1)
    zh = max(z.height for z in zoom) + 2 * pad
    out = Image.new("RGBA", (max(width, zw), sheet.height + zh), (28, 28, 28, 255))
    out.alpha_composite(sheet, (0, 0))
    x = pad
    for z in zoom:
        out.alpha_composite(z, (x, sheet.height + pad))
        x += z.width + pad
    out.save(path)


def ico(images: list[Image.Image]) -> bytes:
    """An .ico as Windows' own are made: 256 px as PNG, the smaller sizes as
    32-bit bitmaps (older readers, .NET's and GDI+'s among them, take only
    bitmaps under 256 px, and pick another size in place of a PNG one)."""
    entries: list[tuple[int, bytes]] = []
    for img in images:
        w, h = img.size
        if w >= 256:
            buf = io.BytesIO()
            img.save(buf, format="PNG", optimize=True)
            entries.append((w, buf.getvalue()))
            continue
        header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
        r, g, b, a = img.split()
        bgra = Image.merge("RGBA", (b, g, r, a)).transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
        stride = (w + 31) // 32 * 4  # the AND mask: 1 bit a pixel, 1 where clear
        alpha = a.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
        mask = bytearray(stride * h)
        for y in range(h):
            for x in range(w):
                if alpha[y * w + x] == 0:
                    mask[y * stride + x // 8] |= 0x80 >> (x % 8)
        entries.append((w, header + bgra + bytes(mask)))
    out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for w, data in entries:
        out += struct.pack("<BBBBHHII", w % 256, w % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in entries:
        out += data
    return bytes(out)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--preview", type=Path, help="also write a sheet of every size here")
    args = ap.parse_args()

    ICON_DIR.mkdir(parents=True, exist_ok=True)
    for drawing in (LARGE, MEDIUM, SMALL):
        (ICON_DIR / f"{drawing.name}.svg").write_text(svg(drawing), encoding="utf-8", newline="\n")

    (ICON_DIR / "icon.ico").write_bytes(ico([render(s) for s in ICO_SIZES]))

    MSIX_ASSETS.mkdir(parents=True, exist_ok=True)
    for name, img in msix_assets().items():
        img.save(MSIX_ASSETS / name, optimize=True)

    STORE_LOGOS.mkdir(parents=True, exist_ok=True)
    render(300).save(STORE_LOGOS / "app-tile-300.png", optimize=True)

    if args.preview:
        preview(args.preview)


if __name__ == "__main__":
    main()
