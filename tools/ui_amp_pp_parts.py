#!/usr/bin/env python3
"""Cuts the amp view's push-pull pictures out of its concept images (task 9-9).

    python tools/ui_amp_pp_parts.py [--src <folder>] [--out <folder>] [--debug <folder>]

The push-pull amp (PP EL34, PP CLASS A) has a panel of its own: MODE, TUBE
TYPE and SPACE are push buttons, CROSSFEED a toggle, and the wood carries
shuffle and repeat (the user, 2026-11-14: the amp changes look and layout
with the tube type). As tools/ui_amp_parts.py does for the single-ended amp,
this takes the upper part of the concept images and removes what the page
draws instead.

The concept images (the work folder's imagegen/accepted/, outside the
repository), read as drawn for a 150% screen:

  UI-AMP-PP-NOTEXT-v1.png       the push-pull amp, tubes lit, its words already
                                taken off (used when present)
  UI-AMP-PP-PURE-NOTEXT-v1.png  the same, tubes dark (原音モード), lined up with
                                the lit one pixel for pixel (used when present)
  UI-AMP-PP-NOTEXT-XFEEDOFF-v1.png  the NOTEXT picture with the CROSSFEED lever
                                down, nothing else changed (the toggle's off
                                state is cut from it; without it, the on state
                                is turned over)
  UI-AMP-PP-v1.png              the lit one with its words: used, with the words
                                filled in here, while the NOTEXT pictures are
                                missing; while the dark one is missing, the lit
                                picture stands in for it (the tubes stay lit)

What comes out (ui/src/textures/amp/):
  pp-lit.jpg, pp-pure.jpg   the amp (x 0..1672, y 42..624), as se-lit / se-pure
  pp-button.jpg, pp-play.jpg  the round buttons on the wood, symbols removed
  pp-xfeed-on.png, pp-xfeed-off.png  the CROSSFEED toggle, lever up and down, cut
                                out (the amp pictures have no toggle)

Removed and drawn by the page: the words (when the source has them), the
buttons' lit state (every push button is made dark; the page lights the
chosen ones and writes their labels), the knobs' pointers, the VU meters'
print and needles, the badge's words, the round buttons on the wood and the
CROSSFEED lever. Kept: the buttons' dark bodies, the knobs' bodies and their
printed scales, the dividers, the screws, the vents, the tubes.
Only numpy and Pillow are needed.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ui_mockup_parts import blend, box_blur, crop, dilate, feather, load, luminance, push_pull, rect_mask, blur  # noqa: E402
from ui_mockup_parts import save as save_to  # noqa: E402
from ui_amp_parts import TOP, BOTTOM, disc, fill_rows, put, button, without_box, cut_out, save_png  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'ui', 'src', 'textures', 'amp')
SRC = os.path.join(HERE, '..', '..', 'imagegen', 'accepted')

PANEL = (24, 285, 1650, 530)  # the metal plate, with the raised arch that carries VOLUME
WOOD = (14, 528, 1658, 622)

# The push buttons (x0, y0, x1, y1), by group: one of each group is lit on a
# picture; it is made dark from a dark one of the same group.
KEYS = {
    'mode': {'pure': (104, 391, 212, 425), 'tube': (104, 443, 212, 477)},
    'type': {'se300b': (301, 377, 441, 413), 'ppel34': (301, 422, 441, 456), 'ppclassa': (301, 465, 441, 499)},
    'space': {'off': (1203, 370, 1263, 402), 'club': (1269, 370, 1329, 402),
              'hall': (1336, 370, 1396, 402), 'arena': (1401, 370, 1461, 402)},
}

# The panel's words (image px, with room around them): only when the source has them.
WORDS = [
    (122, 349, 194, 374),    # MODE
    (318, 343, 430, 369),    # TUBE TYPE
    (560, 349, 632, 374),    # DRIVE
    (790, 290, 884, 315),    # VOLUME (on the arch)
    (986, 355, 1040, 380),   # BASS
    (1080, 355, 1156, 380),  # TREBLE
    (1292, 343, 1358, 369),  # SPACE
    (1306, 406, 1356, 430),  # MIX
    (1500, 354, 1604, 379),  # CROSSFEED
]
VU_LETTER = {'L': (199, 250, 222, 276), 'R': (1455, 250, 1479, 276)}
BADGE = (699, 505, 974, 534)        # the badge under VOLUME
BADGE_WORDS = (722, 508, 952, 532)  # its words

# The knobs: centre, the face's radius, and the drawn pointer(s) (degrees from up, clockwise).
KNOBS = {
    'drive': ((594.0, 434.5), 35.5, [0.0]),
    'volume': ((835.5, 416.0), 76.0, [0.0, 42.0]),
    'bass': ((1012.0, 431.0), 30.5, [0.0]),
    'treble': ((1117.0, 431.0), 30.5, [0.0]),
    'mix': ((1331.0, 465.5), 29.0, [0.0]),
}

VU_PAPER = {'L': (86, 123, 327, 245), 'R': (1346, 123, 1587, 245)}
VU_HUMP = {'L': (207, 245), 'R': (1467, 245)}

# The round buttons on the wood (centre, radius) and as drawn with their glow.
ROUND = {'shuffle': (600.5, 576.5, 28.5), 'prev': (710.5, 576.5, 28.5), 'play': (836.5, 576.5, 39.0),
         'next': (963.5, 576.5, 28.5), 'repeat': (1075.5, 576.5, 28.5)}
ROUND_GLOW = {'play': 52, 'other': 38}

# The CROSSFEED toggle: a box centred on the nut (y 416..470: centre 443), tall
# enough for the lever up (tip at y 398) and turned over (tip at y 488).
XFEED = (1519, 393, 1583, 493)
XFEED_NUT_Y = 443

VENTS = [(150, 562, 462, 602), (1220, 562, 1522, 602)]  # the slots in the wood (kept)
DIVIDERS = [(245, 345, 256, 505), (479, 345, 490, 505), (1178, 345, 1189, 505), (1477, 345, 1488, 505)]
SCREWS = [(40, 352), (40, 497), (1630, 352), (1630, 497)]


def save(arr, name, quality=90):
    save_to(arr, name, quality=quality, folder=OUT)


def lit_amount(img: np.ndarray, box) -> float:
    """How amber the button's rim is: lit buttons have a bright amber border."""
    x0, y0, x1, y1 = box
    part = img[y0 - 3:y1 + 3, x0 - 3:x1 + 3]
    ring = np.ones(part.shape[:2], bool)
    ring[6:-6, 6:-6] = False
    r, g, b = part[..., 0], part[..., 1], part[..., 2]
    amber = (r > 0.7) & (g > 0.45) & (b < 0.35)
    return float(amber[ring].mean())


def key_without_label(img: np.ndarray, box) -> np.ndarray:
    """The button's face, its label filled from the dark face around it."""
    x0, y0, x1, y1 = box
    m = 6
    part = img[y0 + m:y1 - m, x0 + m:x1 - m].copy()
    L = luminance(part)
    ink = dilate((L - blur(L, 6)) > 0.06, 2) | dilate(L > 0.45, 2)
    filled = push_pull(part, ~ink)
    for _ in range(2):
        filled = box_blur(box_blur(filled, 2, 0), 2, 1)
    res = img.copy()
    res[y0 + m:y1 - m, x0 + m:x1 - m] = blend(part, filled, ink, 2)
    return res


def keys_dark(img: np.ndarray, words_drawn: bool):
    """Every push button dark and without its label. Returns the picture and
    which button of each group was lit."""
    out = img.copy()
    if words_drawn:
        for group in KEYS.values():
            for box in group.values():
                out = key_without_label(out, box)
    lit = {}
    src = out.copy()
    for gname, group in KEYS.items():
        amounts = {k: lit_amount(img, b) for k, b in group.items()}
        dark = min(amounts, key=amounts.get)
        lit[gname] = [k for k, a in amounts.items() if a > 0.08]
        sx0, sy0, sx1, sy1 = group[dark]
        for k in lit[gname]:
            x0, y0, x1, y1 = group[k]
            # The glow on the metal round it: filled row by row from the metal
            # either side (the buttons themselves are left out).
            out = put(out, halo_filled(out, (x0, y0, x1, y1)), PANEL)
            # Then the dark button's body, centred on the lit one.
            dx = (x0 + x1 - sx0 - sx1) // 2
            dy = (y0 + y1 - sy0 - sy1) // 2
            m = 3  # the lit rim reaches a little outside the button
            patch = crop(src, (sx0 - m, sy0 - m, sx1 + m, sy1 + m))
            tx0, ty0 = sx0 - m + dx, sy0 - m + dy
            h, w = patch.shape[:2]
            target = out[ty0:ty0 + h, tx0:tx0 + w]
            mask = np.zeros((h, w), bool)
            mask[1:-1, 1:-1] = True
            out[ty0:ty0 + h, tx0:tx0 + w] = blend(target, patch, mask, 1)
    return out, lit


def not_metal(shape) -> np.ndarray:
    """On the panel, what is not plain brushed metal: no streaks are copied from there."""
    m = rect_mask(shape, DIVIDERS + WORDS + [XFEED], origin=PANEL[:2])
    for (cx, cy), R, _ in KNOBS.values():
        m |= disc(shape, cx, cy, R + 26, PANEL[:2])  # with the printed scale (its ticks reach about R + 22)
    for cx, cy in SCREWS:
        m |= disc(shape, cx, cy, 13, PANEL[:2])
    return m


def not_metal_pp(shape) -> np.ndarray:
    m = not_metal(shape) & ~rect_mask(shape, [XFEED], origin=PANEL[:2])
    m |= rect_mask(shape, [(PANEL[0], PANEL[1], 44, PANEL[3]), (1606, PANEL[1], PANEL[2], PANEL[3])], origin=PANEL[:2])  # the bevels
    # The push buttons with their rims and shadows.
    keys = [(b[0] - 5, b[1] - 5, b[2] + 5, b[3] + 5) for g in KEYS.values() for b in g.values()]
    return m | rect_mask(shape, keys, origin=PANEL[:2])


def halo_filled(img: np.ndarray, box) -> np.ndarray:
    part = crop(img, PANEL)
    shape = part.shape[:2]
    g = 16
    halo = rect_mask(shape, [(box[0] - g, box[1] - g, box[2] + g, box[3] + g)], origin=PANEL[:2])
    bodies = rect_mask(shape, [(b[0] - 1, b[1] - 1, b[2] + 1, b[3] + 1) for gr in KEYS.values() for b in gr.values()],
                       origin=PANEL[:2])
    mask = halo & ~bodies
    unsafe = bodies | not_metal(shape)
    made = fill_rows(part, mask, unsafe)
    return blend(part, made, mask, 2)


def knob_without_pointers(img: np.ndarray, centre, R: float, pointers) -> np.ndarray:
    """A knob's face is brushed in circles: each drawn pointer is replaced, at
    every radius, by what lies on either side of it (as tools/ui_amp_parts.py)."""
    cx, cy = centre
    x0, y0 = int(cx - R), int(cy - R)
    size = int(2 * R) + 2
    part = img[y0:y0 + size, x0:x0 + size].copy()
    h, w = part.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    dx, dy = xx + x0 - cx, yy + y0 - cy
    r = np.hypot(dx, dy)
    ang = (np.degrees(np.arctan2(dx, -dy)) + 360) % 360
    for deg in pointers:
        width = 9.0 if R > 50 else 14.0
        d = (ang - deg + 540) % 360 - 180
        hole = (np.abs(d) < width) & (r < R - 1) & (r > 2)

        def sample(a):
            sx = np.clip(np.round(cx + r * np.sin(a) - x0).astype(int), 0, w - 1)
            sy = np.clip(np.round(cy - r * np.cos(a) - y0).astype(int), 0, h - 1)
            return part[sy, sx]

        left, right = sample(np.radians(deg - width - 3)), sample(np.radians(deg + width + 3))
        t = np.clip((d + width + 3) / (2 * (width + 3)), 0, 1)[..., None]
        fill = left * (1 - t) + right * t
        a = feather(hole, 2)[..., None] * (r < R - 1)[..., None]
        part = part * (1 - a) + fill * a
    res = img.copy()
    res[y0:y0 + h, x0:x0 + w] = part
    return res


def vu_without_print(img: np.ndarray, ch: str) -> np.ndarray:
    """The paper keeps its light, not its print (as tools/ui_amp_parts.py)."""
    x0, y0, x1, y1 = VU_PAPER[ch]
    m = 6
    outer = (x0 - m, y0 - m, x1 + m, y1 + m)
    part = crop(img, outer)
    shape = part.shape[:2]
    paper = rect_mask(shape, [VU_PAPER[ch]], origin=outer[:2])
    inner = rect_mask(shape, [(x0 + 4, y0 + 4, x1 - 4, y1 - 3)], origin=outer[:2])
    hx, hy = VU_HUMP[ch]
    hump = disc(shape, hx, hy + 6, 26, outer[:2])
    L = luminance(part)
    ink = (blur(L, 4) - L) > 0.03
    red = (part[..., 0] - part[..., 1]) > 0.3
    mask = dilate((ink | red) & inner & ~hump, 3) & inner & ~hump
    known = paper & ~mask & ~hump
    smooth = push_pull(part, known)
    for _ in range(3):
        smooth = box_blur(box_blur(smooth, 3, 0), 3, 1)
    out = blend(part, smooth, inner & ~hump, 2)
    res = img.copy()
    res[outer[1]:outer[3], outer[0]:outer[2]] = out
    return res


def smooth_fill(img: np.ndarray, box, m: int = 8, passes: int = 2) -> np.ndarray:
    outer = (box[0] - m, box[1] - m, box[2] + m, box[3] + m)
    part = crop(img, outer)
    mask = rect_mask(part.shape[:2], [box], origin=outer[:2])
    filled = push_pull(part, ~mask)
    for _ in range(passes):
        filled = box_blur(box_blur(filled, 2, 0), 2, 1)
    res = img.copy()
    res[outer[1]:outer[3], outer[0]:outer[2]] = blend(part, filled, mask, 3)
    return res


def panel_without_words(img: np.ndarray) -> np.ndarray:
    part = crop(img, PANEL)
    shape = part.shape[:2]
    words = rect_mask(shape, WORDS, origin=PANEL[:2])
    unsafe = not_metal(shape) | rect_mask(shape, [b for g in KEYS.values() for b in g.values()], origin=PANEL[:2])
    made = fill_rows(part, words, unsafe)
    return put(img, blend(part, made, words, 2), PANEL)


def wood_without_buttons(img: np.ndarray, words_drawn: bool) -> np.ndarray:
    out = smooth_fill(img, BADGE_WORDS, m=4) if words_drawn else img
    part = crop(out, WOOD)
    shape = part.shape[:2]
    buttons = np.zeros(shape, bool)
    for name, (x, y, _) in ROUND.items():
        buttons |= disc(shape, x, y, ROUND_GLOW['play' if name == 'play' else 'other'], WOOD[:2])
    badge = rect_mask(shape, [(BADGE[0] - 4, BADGE[1] - 4, BADGE[2] + 4, BADGE[3] + 6)], origin=WOOD[:2])
    panel = np.zeros(shape, bool)
    panel[:max(0, 538 - WOOD[1])] = True  # the plate's lower edge stays
    mask = buttons & ~badge & ~panel
    vents = rect_mask(shape, VENTS, origin=WOOD[:2])
    made = fill_rows(part, mask, badge | panel | vents, edge=4, grain=6)
    return put(out, blend(part, made, mask, 2), WOOD)


def amp(img: np.ndarray, words_drawn: bool):
    out = without_box(img, XFEED, PANEL, not_metal_pp)  # the page puts the toggle back
    out, lit = keys_dark(out, words_drawn)
    for (c, R, pointers) in KNOBS.values():
        out = knob_without_pointers(out, c, R, pointers)
    if words_drawn:
        out = panel_without_words(out)
        for ch in 'LR':
            out = smooth_fill(out, VU_LETTER[ch])
    for ch in 'LR':
        out = vu_without_print(out, ch)
    out = wood_without_buttons(out, words_drawn)
    return out[TOP:BOTTOM], lit


def xfeed(img: np.ndarray, down: bool) -> np.ndarray:
    cut = cut_out(img, without_box(img, XFEED, PANEL, not_metal_pp), XFEED)
    if down:
        # No picture with the lever down: the toggle turned over about the nut
        # (the box is centred on it; the light then comes from below).
        assert XFEED[1] + XFEED[3] == 2 * XFEED_NUT_Y
        return cut[::-1].copy()
    return cut


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
    lit_path = os.path.join(SRC, 'UI-AMP-PP-NOTEXT-v1.png')
    pure_path = os.path.join(SRC, 'UI-AMP-PP-PURE-NOTEXT-v1.png')
    words_drawn = not os.path.exists(lit_path)
    if words_drawn:
        lit_path = os.path.join(SRC, 'UI-AMP-PP-v1.png')
    lit = load(lit_path)
    pure = load(pure_path) if os.path.exists(pure_path) and not words_drawn else lit
    print('from', os.path.basename(lit_path), 'and', os.path.basename(pure_path) if pure is not lit else '(the lit one for the dark one too)')
    for img in (lit, pure):
        if img.shape[:2] != (941, 1672):
            raise SystemExit(f'expected 1672 x 941 concept images, got {img.shape[1]} x {img.shape[0]}')
    os.makedirs(OUT, exist_ok=True)

    a, lit_keys = amp(lit, words_drawn)
    print('lit buttons on the lit picture:', lit_keys)
    save(a, 'pp-lit.jpg', quality=86)
    b, pure_keys = amp(pure, words_drawn)
    save(b, 'pp-pure.jpg', quality=86)
    src = lit
    save(button(src, *ROUND['prev'][:2], r=ROUND['prev'][2] + 2, icon_r=15), 'pp-button.jpg')
    save(button(src, *ROUND['play'][:2], r=ROUND['play'][2] + 4, icon_r=19), 'pp-play.jpg')
    save_png(xfeed(lit, False), 'pp-xfeed-on.png', folder=OUT)
    # The lever down, from the picture drawn so (only the toggle differs), when there is one.
    off_path = os.path.join(SRC, 'UI-AMP-PP-NOTEXT-XFEEDOFF-v1.png')
    if os.path.exists(off_path) and not words_drawn:
        off = load(off_path)
        save_png(cut_out(off, without_box(off, XFEED, PANEL, not_metal_pp), XFEED), 'pp-xfeed-off.png', folder=OUT)
        print('the lever down from', os.path.basename(off_path))
    else:
        save_png(xfeed(lit, True), 'pp-xfeed-off.png', folder=OUT)
    if debug:
        save_to(lit[TOP:BOTTOM], 'pp-source.jpg', quality=86, folder=debug)


if __name__ == '__main__':
    main()
