#!/usr/bin/env python3
"""Cuts the standard view's part images out of the concept image (task 6-8).

    python tools/ui_mockup_parts.py [UI-STANDARD-v1.png] [--out <folder>] [--debug <folder>]

The concept image (the work folder's imagegen/accepted/UI-STANDARD-v1.png,
outside the repository) is the finished look. It is read as drawn for a 150%
screen: one image pixel is 2/3 of a CSS pixel, so the parts are used at the
size they have in the image and come out sharp at 150%.

What is cut out (image pixels, 1672 x 941; the page starts at y = 34, under
the drawn title bar):
  frame.jpg       the wood around the window: the left and right edges and the
                  foot, packed for a CSS border-image (slices 0 12 31 11)
  bar.jpg         the top bar's metal plate (x 11..1660, y 34..139)
  panel-side.jpg  the panels' dark plates: side bar, content, queue
  panel-main.jpg  (y 142..910)
  panel-queue.jpg
  button.jpg      the round metal button (previous) and the amber-ringed one
  play.jpg        (play)
  vu-face.jpg     the VU meter's face and bezel (left meter)

Everything printed or drawn on top - text, icons, the scale and its numbers,
the needle, sliders, the cover, the dividers - is removed and filled in from
the surroundings (CLAUDE.md chapter 8: text and scales are drawn by the
program; only the needle, knobs and lamps move):
  - metal and panels are brushed sideways: each row is filled across the gap
    from its own two ends (keeping the streaks), with fine detail taken from
    the same row elsewhere; rows covered from end to end (dividers) are filled
    from the rows above and below;
  - buttons and the VU face are smooth: filled by push-pull (an image pyramid
    that spreads the known pixels into the holes).
Only numpy and Pillow are needed.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'ui', 'src', 'textures')
DEFAULT_MOCKUP = os.path.join(HERE, '..', '..', 'imagegen', 'accepted', 'UI-STANDARD-v1.png')

Box = tuple  # (x0, y0, x1, y1), x1 and y1 excluded


# --- filling -------------------------------------------------------------------

def push_pull(img: np.ndarray, known: np.ndarray) -> np.ndarray:
    """Fills the unknown pixels smoothly from the known ones."""
    levels = []
    c = img * known[..., None]
    w = known.astype(np.float64)
    while min(w.shape) > 1:
        levels.append((c, w))
        h, wd = w.shape
        c = np.pad(c, ((0, h % 2), (0, wd % 2), (0, 0)), mode='edge')
        w = np.pad(w, ((0, h % 2), (0, wd % 2)), mode='edge')
        h2, w2 = w.shape[0] // 2, w.shape[1] // 2
        c = c.reshape(h2, 2, w2, 2, 3).sum(axis=(1, 3))
        w = w.reshape(h2, 2, w2, 2).sum(axis=(1, 3))
        scale = np.minimum(w, 1) / np.maximum(w, 1e-9)
        c = c * scale[..., None]
        w = np.minimum(w, 1)
    color = c / np.maximum(w, 1e-9)[..., None]
    for c, w in reversed(levels):
        h, wd = w.shape
        up = np.stack([np.asarray(Image.fromarray(color[..., k].astype(np.float32), 'F').resize((wd, h), Image.BILINEAR))
                       for k in range(3)], axis=-1)
        own = c / np.maximum(w, 1e-9)[..., None]
        color = own * w[..., None] + up * (1 - w[..., None])
    return color


def smooth_1d(v: np.ndarray, known: np.ndarray, sigma: float) -> np.ndarray:
    """Fills a 1-D series across its gaps, then smooths it."""
    xs = np.arange(len(v))
    if known.sum() == 0:
        return np.full_like(v, v.mean())
    v = np.interp(xs, xs[known], v[known])
    r = max(1, int(round(sigma / 1.7)))
    for _ in range(3):
        v = box_blur(v, r, 0)
    return v


def feather(mask: np.ndarray, r: int) -> np.ndarray:
    """0..1: 1 inside the mask, easing to 0 over about r pixels outside it."""
    return np.maximum(mask * 1.0, np.clip(blur(dilate(mask, r // 2).astype(np.float64), max(1, r / 2)), 0, 1))


def streaks(d: np.ndarray, known: np.ndarray, seed: int, length: int = 24) -> np.ndarray:
    """Brushing streaks for every pixel: noise drawn out sideways, as strong in
    each row as the row's own detail where it is known."""
    h, w = known.shape
    rng = np.random.default_rng(seed)
    n = rng.standard_normal((h, w))
    for _ in range(3):
        n = box_blur(n, length, 1)
    n /= n.std(axis=1, keepdims=True) + 1e-9
    amp = np.zeros(h)
    has = known.sum(axis=1) >= 16
    for y in range(h):
        if has[y]:
            amp[y] = luminance(d[y, known[y]]).std()
    amp = smooth_1d(amp, has, 1.5)
    return (n * amp[:, None])[..., None] * np.ones(3)


def model_brushed(img: np.ndarray, known: np.ndarray, seed: int) -> np.ndarray:
    """A surface brushed sideways: each row's own colour, times a smooth light
    across the width, plus the rows' own streaks."""
    h, w = known.shape
    rows = np.zeros((h, 3))
    has = known.sum(axis=1) >= max(8, w // 20)
    for y in range(h):
        if has[y]:
            rows[y] = np.median(img[y, known[y]], axis=0)
    for c in range(3):
        rows[:, c] = smooth_1d(rows[:, c], has, 0.6)
    L = luminance(img)
    Lr = luminance(rows)
    ratio = np.where(known, L / np.maximum(Lr[:, None], 1e-4), 0)
    cnt = known.sum(axis=0)
    gain = smooth_1d(np.where(cnt > 0, ratio.sum(axis=0) / np.maximum(cnt, 1), 1.0), cnt > h // 4, 40)
    low = rows[:, None, :] * gain[None, :, None]
    return low + streaks(img - low, known, seed)


def model_smooth(img: np.ndarray, known: np.ndarray, patch: np.ndarray, sigma: float = 40) -> np.ndarray:
    """A plate with little texture: the light spread from the known pixels and
    smoothed, plus fine grain taken from a clean patch (mirrored to tile)."""
    low = push_pull(img, known)
    r = max(1, int(round(sigma / 1.7)))
    for _ in range(3):
        low = box_blur(box_blur(low, r, 0), r, 1)
    pr = patch.copy()
    for _ in range(3):
        pr = box_blur(box_blur(pr, 3, 0), 3, 1)
    grain = patch - pr
    grain = np.concatenate([grain, grain[:, ::-1]], axis=1)
    grain = np.concatenate([grain, grain[::-1]], axis=0)
    h, w = known.shape
    reps = (h // grain.shape[0] + 1, w // grain.shape[1] + 1, 1)
    return low + np.tile(grain, reps)[:h, :w]


def blend(original: np.ndarray, made: np.ndarray, mask: np.ndarray, r: int = 5) -> np.ndarray:
    a = feather(mask, r)[..., None]
    return original * (1 - a) + made * a


# --- masks -----------------------------------------------------------------------

def rect_mask(shape, boxes, origin=(0, 0)) -> np.ndarray:
    m = np.zeros(shape, bool)
    ox, oy = origin
    for x0, y0, x1, y1 in boxes:
        m[max(0, y0 - oy):max(0, y1 - oy), max(0, x0 - ox):max(0, x1 - ox)] = True
    return m


def dilate(mask: np.ndarray, r: int) -> np.ndarray:
    img = Image.fromarray((mask * 255).astype(np.uint8))
    return np.asarray(img.filter(ImageFilter.MaxFilter(2 * r + 1))) > 0


def luminance(img: np.ndarray) -> np.ndarray:
    return img @ np.array([0.2126, 0.7152, 0.0722])


def box_blur(a: np.ndarray, r: int, axis: int) -> np.ndarray:
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r + 1, r)
    c = np.cumsum(np.pad(a, pad, mode='edge'), axis=axis)
    n = a.shape[axis]
    hi = np.take(c, np.arange(2 * r + 1, 2 * r + 1 + n), axis=axis)
    lo = np.take(c, np.arange(0, n), axis=axis)
    return (hi - lo) / (2 * r + 1)


def blur(gray: np.ndarray, r: float) -> np.ndarray:
    """About a Gaussian of radius r: three box blurs each way."""
    k = max(1, int(round(r / 1.7)))
    for _ in range(3):
        gray = box_blur(box_blur(gray, k, 0), k, 1)
    return gray


def stands_out(img: np.ndarray, r: float = 6, threshold: float = 0.03) -> np.ndarray:
    """Pixels brighter or darker than the plate around them (text, icons,
    lines, shadows)."""
    L = luminance(img)
    return np.abs(L - blur(L, r)) > threshold


def breaks_streaks(img: np.ndarray, threshold: float = 0.06) -> np.ndarray:
    """On metal brushed sideways: what is not a sideways streak (anything with
    an edge across the brushing)."""
    L = luminance(img)
    k = 12
    side = L
    for _ in range(3):
        side = box_blur(side, k, 1)
    return np.abs(L - side) > threshold


# --- the parts -------------------------------------------------------------------

def crop(img: np.ndarray, box: Box) -> np.ndarray:
    x0, y0, x1, y1 = box
    return img[y0:y1, x0:x1].copy()


BAR: Box = (11, 34, 1660, 139)
# What sits on the bar, with room for glows and shadows.
BAR_ITEMS = [
    (28, 46, 114, 132),     # cover
    (116, 56, 290, 116),    # title and artist
    (297, 40, 312, 136),    # divider
    (316, 52, 498, 128),    # buttons
    (366, 36, 446, 137),    # the play button's glow, up to the bar's edges
    (500, 40, 514, 136),    # divider
    (514, 72, 850, 106),    # time, seek bar, duration
    (870, 66, 918, 110),    # speaker
    (956, 58, 1040, 82),    # VOLUME
    (918, 80, 1090, 106),   # volume slider
    (1093, 40, 1108, 136),  # divider
    (1108, 44, 1404, 136),  # VU meters and L, R
    (1403, 40, 1418, 136),  # divider
    (1418, 62, 1660, 112),  # output tags
]

PANELS = {
    'panel-side.jpg': ((12, 142, 218, 910), [
        (14, 176, 218, 240),   # the lit item
        (14, 840, 218, 892),   # TUBE VIEW
    ]),
    'panel-main.jpg': ((220, 142, 1239, 910), [
        (244, 156, 1222, 216),  # heading, search, sort, view buttons
        (250, 200, 510, 470),  # the chosen tile and its amber glow
        (272, 222, 488, 460), (520, 222, 728, 460), (764, 222, 972, 460), (1008, 222, 1216, 460),
        (272, 462, 488, 682), (520, 462, 728, 682), (764, 462, 972, 682), (1008, 462, 1216, 682),
        (272, 680, 488, 896), (520, 680, 728, 896), (764, 680, 972, 896), (1008, 680, 1216, 896),
    ]),
    'panel-queue.jpg': ((1242, 142, 1660, 910), [
        (1256, 300, 1640, 362),  # the playing row
        (1634, 250, 1652, 850),  # scroll bar
    ]),
}

# A clean patch of a panel (the queue's foot, under its last row), for the
# panels' fine grain.
PANEL_GRAIN: Box = (1262, 796, 1622, 902)

PREV_BUTTON = (345, 89.5, 22.5)  # centre x, y, radius
PLAY_BUTTON = (405.5, 89, 28.0)
VU_LEFT: Box = (1115, 51, 1252, 114)
VU_FACE: Box = (1121, 57, 1247, 109)  # the printed area inside the bezel


def frame(img: np.ndarray) -> np.ndarray:
    """Left and right edges squeezed to 300 rows above the foot (31 rows)."""
    side_h = 300
    left = img[34:910, 0:11]
    right = img[34:910, 1660:1672]
    foot = img[910:941, :]
    out = np.zeros((side_h + foot.shape[0], img.shape[1], 3))
    sq = lambda a: np.stack([np.asarray(Image.fromarray(a[..., c].astype(np.float32), 'F').resize((a.shape[1], side_h), Image.LANCZOS))
                             for c in range(3)], -1)
    out[:side_h, 0:11] = sq(left)
    out[:side_h, 1660:1672] = sq(right)
    out[side_h:] = foot
    return out


def button(img: np.ndarray, cx: float, cy: float, r: float, icon_r: float) -> np.ndarray:
    box = (int(round(cx - r)), int(round(cy - r)), int(round(cx + r)), int(round(cy + r)))
    part = crop(img, box)
    h, w = part.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    icon = np.hypot(xx - (cx - box[0]), yy - (cy - box[1])) < icon_r
    filled = push_pull(part, ~icon)
    for _ in range(2):  # soften what was filled in
        filled = box_blur(box_blur(filled, 1, 0), 1, 1)
    return blend(part, filled, icon, 3)


def vu_face(img: np.ndarray) -> tuple:
    """The face keeps its light, not its print: the inside of the bezel is
    replaced by the light spread from the unprinted paper, smoothed."""
    part = crop(img, VU_LEFT)
    fx0, fy0, fx1, fy1 = (VU_FACE[0] - VU_LEFT[0], VU_FACE[1] - VU_LEFT[1], VU_FACE[2] - VU_LEFT[0], VU_FACE[3] - VU_LEFT[1])
    face = np.zeros(part.shape[:2], bool)
    face[fy0:fy1, fx0:fx1] = True
    L = luminance(part)
    ink = (blur(L, 4) - L) > 0.03          # dark print: scale, numbers, VU, needle
    red = (part[..., 0] - part[..., 1]) > 0.22  # the red zone
    mask = dilate((ink | red) & face, 3) & face
    paper = push_pull(part, ~mask & face)
    for _ in range(3):
        paper = box_blur(box_blur(paper, 3, 0), 3, 1)
    inner = np.zeros_like(face)
    inner[fy0 + 1:fy1 - 1, fx0 + 1:fx1 - 1] = True
    return blend(part, paper, inner, 2), mask


def load(path: str) -> np.ndarray:
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.float64) / 255


def save(arr: np.ndarray, name: str, quality: int = 90, folder: str = ''):
    img = Image.fromarray((np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8))
    path = os.path.join(folder or OUT, name)
    img.save(path, 'JPEG', quality=quality, optimize=True, progressive=True, subsampling=0)
    print(f'{name}: {img.width} x {img.height}, {os.path.getsize(path) // 1024} KB')


def save_mask(mask: np.ndarray, name: str, folder: str):
    Image.fromarray((mask * 255).astype(np.uint8)).save(os.path.join(folder, name))


def main():
    global OUT
    args = sys.argv[1:]
    debug = None
    if '--out' in args:
        i = args.index('--out')
        OUT = args[i + 1]
        del args[i:i + 2]
    if '--debug' in args:
        i = args.index('--debug')
        debug = args[i + 1]
        del args[i:i + 2]
        os.makedirs(debug, exist_ok=True)
    src = load(args[0] if args else DEFAULT_MOCKUP)
    if src.shape[:2] != (941, 1672):
        raise SystemExit(f'expected the 1672 x 941 concept image, got {src.shape[1]} x {src.shape[0]}')
    os.makedirs(OUT, exist_ok=True)

    save(frame(src), 'frame.jpg', quality=88)

    bar = crop(src, BAR)
    mask = rect_mask(bar.shape[:2], BAR_ITEMS, origin=BAR[:2])
    mask |= dilate(breaks_streaks(bar), 2)
    mask[:2] = mask[-2:] = False  # the bar's own top and bottom edges
    save(blend(bar, model_brushed(bar, ~dilate(mask, 4), seed=3), mask, 14), 'bar.jpg', quality=88)
    if debug:
        save_mask(mask, 'bar-mask.png', debug)

    for name, (box, items) in PANELS.items():
        part = crop(src, box)
        items_mask = rect_mask(part.shape[:2], items, origin=box[:2])
        mask = dilate(stands_out(part), 4) | items_mask
        # Keep the panel's own rim (its rounded edge and highlight), except
        # where something sits on it.
        rim = np.zeros(mask.shape, bool)
        rim[:5, :] = rim[-5:, :] = True
        rim[:, :5] = rim[:, -5:] = True
        made = model_smooth(part, ~dilate(mask, 3) & ~rim, crop(src, PANEL_GRAIN))
        replace = ~rim | items_mask
        save(blend(part, made, replace, 2), name, quality=85)
        if debug:
            save_mask(mask, name.replace('.jpg', '-mask.png'), debug)

    save(button(src, *PREV_BUTTON, icon_r=13.5), 'button.jpg')
    save(button(src, *PLAY_BUTTON, icon_r=14), 'play.jpg')
    face, mask = vu_face(src)
    save(face, 'vu-face.jpg')
    if debug:
        save_mask(mask, 'vu-mask.png', debug)


if __name__ == '__main__':
    main()
