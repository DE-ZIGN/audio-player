#!/usr/bin/env python3
"""Makes the standard view's part images (task 6-8) from the images made by
image generation (the working folder's imagegen/inbox, outside the repository).

    python tools/ui_textures.py <imagegen inbox folder>

For each part it takes the newest image, and:
  - wood, panel: made seamless in both directions (blended with a copy
    shifted by half, one direction at a time), scaled down;
  - metal: made seamless sideways (it is laid side by side), scaled down;
  - VU face: scaled down (the scale, numbers and needle are drawn by the page);
  - buttons: cropped to the round button (found against the black around it),
    scaled down.
Writes JPEG files into ui/src/textures/. None of the images carries text.
"""
import os
import sys

from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'ui', 'src', 'textures')


def newest(inbox: str, part: str) -> str:
    folder = os.path.join(inbox, part)
    runs = sorted(d for d in os.listdir(folder) if os.path.isfile(os.path.join(folder, d, 'image.png')))
    if not runs:
        raise SystemExit(f'no image for {part}')
    return os.path.join(folder, runs[-1], 'image.png')


def ramp(n: int, axis_len: int, horizontal: bool, size) -> Image.Image:
    """A mask that is 0 along the middle line and 255 at both edges (smooth)."""
    w, h = size
    line = []
    for i in range(axis_len):
        t = abs(i - (axis_len - 1) / 2) / ((axis_len - 1) / 2)  # 0 middle, 1 edge
        t = t * t * (3 - 2 * t)  # smoothstep
        line.append(int(round(255 * t)))
    mask = Image.new('L', size)
    if horizontal:
        mask.putdata([line[x] for y in range(h) for x in range(w)])
    else:
        mask.putdata([line[y] for y in range(h) for x in range(w)])
    return mask


def seamless(img: Image.Image, both: bool) -> Image.Image:
    w, h = img.size
    # Sideways: near the left and right edges use the copy shifted by half,
    # whose edges continue into each other; its own seam is in the middle,
    # where the original is used.
    shifted = ImageChops.offset(img, w // 2, 0)
    img = Image.composite(shifted, img, ramp(w, w, True, img.size))
    if both:
        shifted = ImageChops.offset(img, 0, h // 2)
        img = Image.composite(shifted, img, ramp(h, h, False, img.size))
    return img


def crop_round(img: Image.Image, threshold: int = 40, margin: float = 0.0) -> Image.Image:
    """The bounding square of what is brighter than the black around it."""
    gray = img.convert('L').point(lambda v: 255 if v > threshold else 0)
    box = gray.getbbox()
    if not box:
        raise SystemExit('nothing to crop')
    l, t, r, b = box
    side = max(r - l, b - t)
    cx, cy = (l + r) / 2, (t + b) / 2
    half = side / 2 * (1 + margin)
    return img.crop((int(cx - half), int(cy - half), int(cx + half), int(cy + half)))


def save(img: Image.Image, name: str, size, quality: int = 86):
    img = img.convert('RGB').resize(size, Image.LANCZOS)
    path = os.path.join(OUT, name)
    img.save(path, 'JPEG', quality=quality, optimize=True, progressive=True)
    print(f'{name}: {size[0]} x {size[1]}, {os.path.getsize(path) // 1024} KB')


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    inbox = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    open_ = lambda part: Image.open(newest(inbox, part)).convert('RGB')

    save(seamless(open_('STD-WOOD'), both=True), 'wood.jpg', (640, 640))
    metal = seamless(open_('STD-METAL'), both=False)
    save(metal, 'metal.jpg', (960, 640))
    save(seamless(open_('STD-PANEL'), both=True), 'panel.jpg', (512, 512), quality=80)
    save(open_('STD-VU-FACE'), 'vu-face.jpg', (384, 256), quality=88)
    save(crop_round(open_('STD-BUTTON')), 'button.jpg', (160, 160), quality=88)
    # The play button's glow reaches past the ring: keep a little of it.
    save(crop_round(open_('STD-PLAY'), threshold=24, margin=0.0), 'play.jpg', (192, 192), quality=88)


if __name__ == '__main__':
    main()
