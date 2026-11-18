#!/usr/bin/env python3
"""Cuts the amp view's pictures out of its concept images (task 9-9).

    python tools/ui_amp_parts.py [--src <folder>] [--out <folder>] [--debug <folder>] [--with-words]

--with-words uses the concept images with their words even when the NOTEXT
ones are there.

The concept images (the work folder's imagegen/accepted/, outside the
repository) are the finished look, read as drawn for a 150% screen like the
standard view's (tools/ui_mockup_parts.py): one image pixel is 2/3 of a CSS
pixel.

  UI-AMP-SE-v1.png       the single-ended amp, tubes lit (音作りモード)
  UI-AMP-SE-PURE-v2.png  the same amp, tubes dark (原音モード); it lines up
                         with the lit one pixel for pixel
  UI-AMP-SE-NOTEXT-v1.png, UI-AMP-SE-PURE-NOTEXT-v1.png
                         the same two with the printed words already taken
                         off (by the image tool that drew them; everything
                         else within a pixel of the originals). Used when
                         present: the words then need no filling in here.

What comes out (ui/src/textures/amp/):
  se-lit.jpg, se-pure.jpg  the amp itself (x 0..1672, y 42..624: under the
                           drawn title bar, down to the wood under the panel).
                           The page shows one over the other and fades
                           between them, so the tubes light up and go dark.
  button.jpg, play.jpg     the round buttons on the wood (plain and the
                           amber-ringed one), their symbols removed
  toggle-up.png,           the MODE toggle, lever up (PURE) and down (TUBE),
  toggle-down.png          cut out (transparent round it): the amp pictures
                           have no toggle, so neither lever shows through

Removed from the amp and drawn by the program instead (CLAUDE.md chapter 8:
text and scales are drawn by the program; only the needles, knobs and lamps
move):
  - every word printed on the panel and under the meters (unless the
    NOTEXT pictures are used);
  - the lamps (lit or not) and their glow: a dark lamp is put back in every
    socket, the page lights the chosen ones;
  - the VU meters' print and needles (their paper keeps its light);
  - the VOLUME knob's pointer (the page draws one at the volume);
  - the words in the badge on the wood, and the round buttons with their
    glow (the two pictures draw them differently; the page puts its own
    back: prev, play / pause, stop, next).
The toggle (lever and nut) is taken off the amp pictures too and cut out on
its own, lever up from the dark picture and lever down from the lit one.
The knobs' bodies, the dividers, the screws, the tubes and the transformer
stay as drawn: the page draws a pointer on each knob.
Only numpy and Pillow are needed.
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ui_mockup_parts import (blend, blur, box_blur, crop, dilate, feather, load, luminance,  # noqa: E402
                             model_brushed, push_pull, rect_mask, save_mask)
from ui_mockup_parts import save as save_to  # noqa: E402
from ui_mockup_parts import without_symbol  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'ui', 'src', 'textures', 'amp')
SRC = os.path.join(HERE, '..', '..', 'imagegen', 'accepted')

TOP = 42      # under the drawn title bar
BOTTOM = 624  # the wood under the panel ends here; the page's own panels follow

PANEL = (56, 352, 1616, 530)  # the metal plate, from the left screws to the right ones
WOOD = (14, 528, 1658, 622)

# The panel's words (image px, with room around them).
WORDS = [
    (138, 376, 210, 407),    # MODE
    (216, 417, 270, 443),    # PURE
    (216, 461, 270, 487),    # TUBE
    (346, 391, 415, 414),    # DRIVE
    (492, 401, 548, 425),    # BASS
    (594, 401, 672, 425),    # TREBLE
    (796, 353, 882, 375),    # VOLUME
    (1048, 401, 1120, 425),  # SPACE
    (984, 468, 1022, 490),   # OFF
    (1032, 468, 1080, 490),  # CLUB
    (1086, 468, 1133, 490),  # HALL
    (1138, 468, 1195, 490),  # ARENA
    (1239, 394, 1285, 417),  # MIX
    (1402, 394, 1522, 419),  # TUBE TYPE
    (1372, 459, 1422, 504),  # SE 300B
    (1438, 459, 1488, 504),  # PP EL34
    (1498, 459, 1571, 504),  # PP CLASS A
]

# The lamps: centre and the radius of the socket (its glow reaches further).
LAMPS = {
    'pure': (200.6, 429.4), 'tube': (200.6, 473.0),
    'off': (1004, 450), 'club': (1056, 450), 'hall': (1108, 450), 'arena': (1164, 450),
    'se300b': (1397, 448), 'ppel34': (1463, 446), 'ppclassa': (1531, 446),
}
LAMP_R = 9.5
LAMP_GLOW_R = 21
DARK_LAMP = 'club'  # dark in both images

VOLUME = (837.5, 456.5)  # the knob's centre
VOLUME_FACE_R = 74       # its face and the knurled ring
VOLUME_POINTER = {'lit': 35.0, 'pure': 140.0}  # degrees from up, clockwise: the drawn pointer

# The MODE toggle: a box centred on the nut (about x 151, y 449), tall enough
# for the lever up (ball top at y 401) and down (ball bottom at y 497) with
# their shadows; clear
# of the PURE and TUBE lamps (their glow reaches x 180). The lit picture draws
# its toggle (lever down) 2 px right and 5 px higher than the dark one: it is
# cut from a box moved by as much, so the nut stays put when the lever moves.
TOGGLE_DOWN_SHIFT = (2, -5)
TOGGLE = (117, 391, 183, 526)  # down to the panel's lower edge: the lever's shadow reaches it

# The toggle's solid parts as each picture draws them (image px): the nut (an
# ellipse: centre and radii), the lever's ball (centre and radius) and its
# stem (a centre and half-width where it leaves the hole and where it meets
# the ball). Everything else the toggle changes on the metal is its shadow.
TOGGLE_PARTS = {
    'up': {'nut': (150.7, 450.5, 25.0, 24.5), 'ball': (148.5, 423.3, 13.8),
           'stem': ((150.0, 453.0, 8.5), (148.8, 426.0, 9.5))},
    'down': {'nut': (151.0, 445.5, 26.5, 25.5), 'ball': (150.0, 478.5, 15.0),
             'stem': ((151.0, 446.0, 6.5), (150.0, 474.0, 9.5))},
}
# Put back on the bare metal, a cut toggle must give the picture again: the
# mean difference over its box stays under this (measured: about 0.01).
TOGGLE_MATCH_LIMIT = 0.02

# The VU meters: the paper inside the bezel, and the channel letter under it.
VU_PAPER = {'L': (140, 185, 357, 301), 'R': (1318, 185, 1536, 301)}
VU_HUMP = {'L': (248.5, 301), 'R': (1427, 301)}  # the black hump that hides the pivot (kept)
VU_LETTER = {'L': (236, 309, 262, 335), 'R': (1414, 309, 1442, 335)}

BADGE_WORDS = (196, 561, 384, 586)  # inside the badge on the wood
BUTTONS = [(670, 575), (779, 575), (890, 575), (1000, 575)]  # where the page puts prev, play, stop, next
BUTTON_R = 33.5
# The buttons as each picture draws them (centre, and a radius with their glow).
DRAWN_BUTTONS = {
    'lit': [(670, 575, 40), (779, 575, 40), (890, 575, 48), (1000, 575, 40)],
    'pure': [(708, 576, 40), (835, 576, 45), (962, 576, 40)],
}


def save(arr, name, quality=90):
    save_to(arr, name, quality=quality, folder=OUT)


def at(box, origin):
    x0, y0, x1, y1 = box
    return (x0 - origin[0], y0 - origin[1], x1 - origin[0], y1 - origin[1])


def disc(shape, cx, cy, r, origin=(0, 0)):
    h, w = shape
    yy, xx = np.mgrid[0:h, 0:w]
    return np.hypot(xx + origin[0] - cx, yy + origin[1] - cy) < r


DIVIDERS = [(296, 390, 309, 506), (703, 390, 716, 506), (955, 390, 968, 506), (1334, 390, 1347, 506)]
KNOBS = [((379, 459), 46), ((518, 463), 36), ((632, 463), 36), (VOLUME, 80), ((1264, 455), 44)]
SCREWS = [(80, 387), (80, 505), (1591, 387), (1591, 505)]


def fill_rows(part: np.ndarray, mask: np.ndarray, unsafe: np.ndarray, edge: int = 5, grain: int = 8) -> np.ndarray:
    """Fills what is masked on a surface brushed (or grained) sideways, row by
    row: each gap gets a ramp between the row's own colour on either side of
    it, plus the row's fine streaks copied from beside the gap (where nothing
    else is drawn), so the hairlines run on through it."""
    out = part.copy()
    h, w = mask.shape
    bad = mask | unsafe
    for y in range(h):
        row = mask[y]
        if not row.any():
            continue
        line = part[y]
        low_line = line.copy()
        for _ in range(3):
            low_line = box_blur(low_line, grain, 0)
        detail = line - low_line
        x = 0
        while x < w:
            if not row[x]:
                x += 1
                continue
            a = x
            while x < w and row[x]:
                x += 1
            b = x  # the gap is [a, b)
            n = b - a
            left = [i for i in range(max(0, a - 40), a) if not bad[y, i]][-edge:]
            right = [i for i in range(b, min(w, b + 40)) if not bad[y, i]][:edge]
            lv = line[left].mean(axis=0) if left else None
            rv = line[right].mean(axis=0) if right else None
            if lv is None and rv is None:
                continue
            lv = rv if lv is None else lv
            rv = lv if rv is None else rv
            t = (np.arange(n) + 0.5)[:, None] / n
            low = lv * (1 - t) + rv * t
            # Streaks from beside the gap, from the nearest clean stretch.
            tex = np.zeros((n, 3))
            for off in (-(n + 4), n + 4, -2 * (n + 4), 2 * (n + 4), -3 * (n + 4), 3 * (n + 4)):
                s0, s1 = a + off, b + off
                if s0 >= 0 and s1 <= w and not bad[y, s0:s1].any():
                    tex = detail[s0:s1]
                    break
            out[y, a:b] = low + tex
    return out


def panel_without_words_and_lamps(img: np.ndarray, seed: int, words_drawn: bool = True) -> np.ndarray:
    part = crop(img, PANEL)
    shape = part.shape[:2]
    words = rect_mask(shape, WORDS, origin=PANEL[:2]) if words_drawn else np.zeros(shape, bool)
    lamps = np.zeros(shape, bool)
    for cx, cy in LAMPS.values():
        lamps |= disc(shape, cx, cy, LAMP_GLOW_R, PANEL[:2])
    mask = words | lamps
    # What is not plain metal: no streaks are copied from there.
    unsafe = rect_mask(shape, DIVIDERS + [TOGGLE], origin=PANEL[:2])
    for (cx, cy), r in KNOBS:
        unsafe |= disc(shape, cx, cy, r, PANEL[:2])
    for cx, cy in SCREWS:
        unsafe |= disc(shape, cx, cy, 13, PANEL[:2])
    made = fill_rows(part, mask, unsafe)
    # Only the words and the lamps are replaced; the knobs stay as drawn.
    return blend(part, made, mask, 2)


def not_metal_se(shape) -> np.ndarray:
    """On the panel, what is not plain brushed metal (no streaks are copied from there)."""
    m = rect_mask(shape, DIVIDERS, origin=PANEL[:2])
    for (cx, cy), r in KNOBS:
        m |= disc(shape, cx, cy, r, PANEL[:2])
    for cx, cy in SCREWS:
        m |= disc(shape, cx, cy, 13, PANEL[:2])
    for cx, cy in LAMPS.values():
        m |= disc(shape, cx, cy, LAMP_GLOW_R, PANEL[:2])
    return m


def without_box(img: np.ndarray, box, panel, not_metal) -> np.ndarray:
    """The panel with `box` filled with brushed metal, row by row."""
    part = crop(img, panel)
    shape = part.shape[:2]
    mask = rect_mask(shape, [box], origin=panel[:2])
    made = fill_rows(part, mask, not_metal(shape))
    return put(img, blend(part, made, mask, 2), panel)


def toggle_solid(shape, parts, origin, grow: float = 1.0, fine: int = 4) -> np.ndarray:
    """How much of each pixel the nut and the lever cover (0..1), from their
    shapes: metal on metal cannot be told from the plate by colour."""
    h, w = shape
    yy, xx = np.mgrid[0:h * fine, 0:w * fine]
    x = (xx + 0.5) / fine + origin[0]
    y = (yy + 0.5) / fine + origin[1]
    cx, cy, rx, ry = parts['nut']
    m = ((x - cx) / (rx + grow)) ** 2 + ((y - cy) / (ry + grow)) ** 2 <= 1
    bx, by, br = parts['ball']
    m |= np.hypot(x - bx, y - by) <= br + grow
    # The stem: within its half-width of the line from the hole to the ball (the lever may lean).
    (x0, y0, w0), (x1, y1, w1) = parts['stem']
    sx, sy = x1 - x0, y1 - y0
    t = np.clip(((x - x0) * sx + (y - y0) * sy) / (sx * sx + sy * sy), 0, 1)
    m |= np.hypot(x - (x0 + sx * t), y - (y0 + sy * t)) <= w0 + (w1 - w0) * t + grow
    return m.reshape(h, fine, w, fine).mean(axis=(1, 3))


def cut_toggle(img: np.ndarray, empty: np.ndarray, box, parts) -> np.ndarray:
    """The toggle in `box` as RGBA, to be put on the picture without it
    (`empty`, the box filled with metal): the nut and the lever as drawn,
    opaque, and their shadow as black, as strong as the metal is darker in
    the picture than bare. A shadow kept as the picture's own darkened pixels
    showed as a patch with a hard edge (2026-11-18)."""
    a = crop(img, box)
    b = crop(empty, box)
    h, w = a.shape[:2]
    solid = toggle_solid((h, w), parts, box[:2])
    shade = np.clip(1 - luminance(a) / np.maximum(luminance(b), 1e-3), 0, 1)
    # Smoothed (the filled-in metal's streaks are not the picture's), from the metal only.
    metal = (solid < 0.01) * 1.0
    shade = blur(shade * metal, 2.5) / np.maximum(blur(metal, 2.5), 1e-3)
    shade = np.clip((shade - 0.04) / 0.96, 0, 0.85)
    # Nothing at the box's edge, so no line shows where the cut ends.
    yy, xx = np.mgrid[0:h, 0:w]
    shade *= np.clip(np.minimum.reduce([xx, yy, w - 1 - xx, h - 1 - yy]) / 6.0, 0, 1)
    alpha = solid + (1 - solid) * shade
    rgb = a * (solid / np.maximum(alpha, 1e-4))[..., None]  # the shadow's share is black
    out = np.concatenate([rgb, alpha[..., None]], axis=2)
    back = b * (1 - alpha[..., None]) + rgb * alpha[..., None]
    miss = float(np.abs(back - a).mean())
    if miss > TOGGLE_MATCH_LIMIT:
        raise SystemExit(f'the cut toggle put back on the metal differs from the picture by {miss:.3f} '
                         f'(limit {TOGGLE_MATCH_LIMIT}): check TOGGLE and TOGGLE_PARTS')
    return out


def save_png(rgba: np.ndarray, name: str, folder: str = ''):
    path = os.path.join(folder or OUT, name)
    Image.fromarray((np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA').save(path, optimize=True)
    print(f'{name}: {rgba.shape[1]} x {rgba.shape[0]}, {os.path.getsize(path) // 1024} KB')


def put_dark_lamps(panel: np.ndarray, lamp: np.ndarray) -> np.ndarray:
    """A dark lamp (cut from a socket that is dark) back in every socket."""
    out = panel.copy()
    r = lamp.shape[0] // 2
    ky, kx = np.mgrid[-r:r + 1, -r:r + 1]
    a = np.clip(LAMP_R + 3 - np.hypot(kx, ky), 0, 1.5) / 1.5  # soft edge
    for cx, cy in LAMPS.values():
        x, y = int(round(cx - PANEL[0])), int(round(cy - PANEL[1]))
        region = out[y - r:y + r + 1, x - r:x + r + 1]
        region[:] = region * (1 - a[..., None]) + lamp * a[..., None]
    return out


def cut_lamp(img: np.ndarray) -> np.ndarray:
    cx, cy = LAMPS[DARK_LAMP]
    r = int(LAMP_R + 3)
    x, y = int(round(cx)), int(round(cy))
    return img[y - r:y + r + 1, x - r:x + r + 1].copy()


def without_pointer(img: np.ndarray, pointer_deg: float) -> np.ndarray:
    """The VOLUME knob's face is brushed in circles: the drawn pointer is
    replaced, at every radius, by what lies on either side of it."""
    cx, cy = VOLUME
    R = VOLUME_FACE_R
    x0, y0 = int(cx - R), int(cy - R)
    part = img[y0:y0 + 2 * R + 1, x0:x0 + 2 * R + 1].copy()
    h, w = part.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    dx, dy = xx + x0 - cx, yy + y0 - cy
    r = np.hypot(dx, dy)
    ang = (np.degrees(np.arctan2(dx, -dy)) + 360) % 360  # from up, clockwise
    width = 11.0  # degrees either side of the pointer
    d = (ang - pointer_deg + 540) % 360 - 180
    hole = (np.abs(d) < width) & (r < R - 1) & (r > 2)
    # Sample the two sides at the same radius and mix them across the hole.
    a0 =np.radians(pointer_deg - width - 3)
    a1 = np.radians(pointer_deg + width + 3)

    def sample(a):
        sx = cx + r * np.sin(a) - x0
        sy = cy - r * np.cos(a) - y0
        sx = np.clip(np.round(sx).astype(int), 0, w - 1)
        sy = np.clip(np.round(sy).astype(int), 0, h - 1)
        return part[sy, sx]

    left, right = sample(a0), sample(a1)
    t = np.clip((d + width + 3) / (2 * (width + 3)), 0, 1)[..., None]
    fill = left * (1 - t) + right * t
    a = feather(hole, 3)[..., None] * (r < R - 1)[..., None]
    out = part * (1 - a) + fill * a
    res = img.copy()
    res[y0:y0 + h, x0:x0 + w] = out
    return res


def vu_without_print(img: np.ndarray, ch: str) -> np.ndarray:
    """The paper keeps its light, not its print (as tools/ui_mockup_parts.py)."""
    box = VU_PAPER[ch]
    x0, y0, x1, y1 = box
    m = 6
    outer = (x0 - m, y0 - m, x1 + m, y1 + m)
    part = crop(img, outer)
    shape = part.shape[:2]
    paper = rect_mask(shape, [box], origin=outer[:2])
    # Keep the rounded corners of the bezel: only well inside the paper.
    inner = rect_mask(shape, [(x0 + 3, y0 + 3, x1 - 3, y1 - 2)], origin=outer[:2])
    hx, hy = VU_HUMP[ch]
    hump = disc(shape, hx, hy + 4, 25, outer[:2])
    L = luminance(part)
    ink = (blur(L, 4) - L) > 0.03
    red = (part[..., 0] - part[..., 1]) > 0.22
    mask = dilate((ink | red) & inner & ~hump, 3) & inner & ~hump
    known = paper & ~mask & ~hump
    smooth = push_pull(part, known)
    for _ in range(3):
        smooth = box_blur(box_blur(smooth, 3, 0), 3, 1)
    a = (inner & ~hump)
    out = blend(part, smooth, a, 2)
    res = img.copy()
    res[outer[1]:outer[3], outer[0]:outer[2]] = out
    return res


def bezel_without_letter(img: np.ndarray, ch: str) -> np.ndarray:
    box = VU_LETTER[ch]
    m = 8
    outer = (box[0] - m, box[1] - m, box[2] + m, box[3] + m)
    part = crop(img, outer)
    mask = rect_mask(part.shape[:2], [box], origin=outer[:2])
    filled = push_pull(part, ~mask)
    for _ in range(2):
        filled = box_blur(box_blur(filled, 2, 0), 2, 1)
    res = img.copy()
    res[outer[1]:outer[3], outer[0]:outer[2]] = blend(part, filled, mask, 3)
    return res


def wood_without_badge_words_and_buttons(img: np.ndarray, drawn, words_drawn: bool = True) -> np.ndarray:
    part = crop(img, WOOD)
    shape = part.shape[:2]
    if words_drawn:
        words = rect_mask(shape, [BADGE_WORDS], origin=WOOD[:2])
        # The badge's inside is dark and smooth.
        filled = push_pull(part, ~words)
        for _ in range(2):
            filled = box_blur(box_blur(filled, 2, 0), 2, 1)
        part = blend(part, filled, words, 2)
    # The buttons as drawn (the two pictures differ: four lit, three dark) and
    # their glow go; the grain runs on through. The page puts its own back.
    buttons = np.zeros(shape, bool)
    for bx, by, r in drawn:
        buttons |= disc(shape, bx, by, r, WOOD[:2])
    badge = rect_mask(shape, [(170, 550, 410, 600)], origin=WOOD[:2])
    made = fill_rows(part, buttons, badge, edge=4, grain=6)
    return blend(part, made, buttons, 2)


def put(img: np.ndarray, part: np.ndarray, box) -> np.ndarray:
    res = img.copy()
    res[box[1]:box[3], box[0]:box[2]] = part
    return res


def amp(img: np.ndarray, lamp: np.ndarray, pointer: float, drawn, seed: int, words_drawn: bool = True) -> np.ndarray:
    # The page puts the toggle back (either picture's: both boxes are cleared).
    dx, dy = TOGGLE_DOWN_SHIFT
    out = without_box(img, (TOGGLE[0] + min(0, dx), TOGGLE[1] + min(0, dy), TOGGLE[2] + max(0, dx), TOGGLE[3] + max(0, dy)),
                      PANEL, not_metal_se)
    out = without_pointer(out, pointer)
    panel = put_dark_lamps(panel_without_words_and_lamps(out, seed, words_drawn), lamp)
    out = put(out, panel, PANEL)
    for ch in 'LR':
        out = vu_without_print(out, ch)
        if words_drawn:
            out = bezel_without_letter(out, ch)
    out = put(out, wood_without_badge_words_and_buttons(out, drawn, words_drawn), WOOD)
    return out[TOP:BOTTOM]


def button(img: np.ndarray, cx: float, cy: float, r: float, icon_r: float) -> np.ndarray:
    box = (int(round(cx - r)), int(round(cy - r)), int(round(cx + r)), int(round(cy + r)))
    return without_symbol(crop(img, box), cx - box[0], cy - box[1], icon_r)


def main():
    global OUT, SRC
    args = sys.argv[1:]
    debug = None
    for flag in ('--out', '--src', '--debug'):
        if flag in args:
            i = args.index(flag)
            value = args[i + 1]
            del args[i:i + 2]
            if flag == '--out':
                OUT = value
            elif flag == '--src':
                SRC = value
            else:
                debug = value
                os.makedirs(debug, exist_ok=True)
    # The pictures with the words already off, when there are both.
    clean = [os.path.join(SRC, n) for n in ('UI-AMP-SE-NOTEXT-v1.png', 'UI-AMP-SE-PURE-NOTEXT-v1.png')]
    words_drawn = not all(os.path.exists(n) for n in clean)
    if '--with-words' in args:
        words_drawn = True
    if words_drawn:
        lit = load(os.path.join(SRC, 'UI-AMP-SE-v1.png'))
        pure = load(os.path.join(SRC, 'UI-AMP-SE-PURE-v2.png'))
    else:
        lit, pure = load(clean[0]), load(clean[1])
    print('from', 'the concept images (words filled in here)' if words_drawn else 'the NOTEXT pictures')
    for img in (lit, pure):
        if img.shape[:2] != (941, 1672):
            raise SystemExit(f'expected 1672 x 941 concept images, got {img.shape[1]} x {img.shape[0]}')
    os.makedirs(OUT, exist_ok=True)

    lamp = cut_lamp(pure)
    save(amp(lit, lamp, VOLUME_POINTER['lit'], DRAWN_BUTTONS['lit'], 11, words_drawn), 'se-lit.jpg', quality=86)
    save(amp(pure, lamp, VOLUME_POINTER['pure'], DRAWN_BUTTONS['pure'], 11, words_drawn), 'se-pure.jpg', quality=86)
    save(button(lit, *BUTTONS[1], r=BUTTON_R + 2, icon_r=17), 'button.jpg')
    save(button(lit, *BUTTONS[2], r=BUTTON_R + 4, icon_r=19), 'play.jpg')  # 17 left a corner of the pause bar as a speck
    save_png(cut_toggle(pure, without_box(pure, TOGGLE, PANEL, not_metal_se), TOGGLE, TOGGLE_PARTS['up']), 'toggle-up.png')
    dx, dy = TOGGLE_DOWN_SHIFT
    down = (TOGGLE[0] + dx, TOGGLE[1] + dy, TOGGLE[2] + dx, TOGGLE[3] + dy)
    save_png(cut_toggle(lit, without_box(lit, down, PANEL, not_metal_se), down, TOGGLE_PARTS['down']), 'toggle-down.png')
    if debug:
        save_to(lamp, 'lamp.jpg', quality=92, folder=debug)
        mask = rect_mask((BOTTOM - TOP, 1672), WORDS, origin=(0, TOP))
        save_mask(mask, 'words-mask.png', debug)


if __name__ == '__main__':
    main()
