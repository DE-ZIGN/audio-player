"""A made-up library for the Store screenshots (task 8-5d).

The screenshots must not show real albums, names or covers, so this writes
a library of its own: test signals and simple synthesized tones, under
made-up artist, album and track names, with covers drawn here. Nothing in
it is a recording of anyone. Run it once, then point the app at the
folder (tools/store_screenshots.py does that with a library of its own).

    pip install numpy soundfile mutagen pillow
    python tools/make_store_demo.py OUT_DIR

Each album is a folder with FLAC files (tags: title, artist, album, album
artist, genre, date, track number) and a cover.jpg. Most tracks carry 20
seconds of sound and then digital silence, which FLAC stores in almost no
space, so the durations in the list look like albums while the folder stays
small; the tracks the screenshots play have sound all the way through.
"""

from __future__ import annotations

import math
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import soundfile as sf
from mutagen.flac import FLAC
from PIL import Image, ImageDraw, ImageFilter


@dataclass
class Album:
    artist: str
    title: str
    genre: str
    year: int
    rate: int
    bits: int
    tracks: list[tuple[str, int]]  # (title, seconds)
    palette: tuple[str, str, str]
    pattern: str
    full: set[int] = field(default_factory=set)  # track numbers with sound all the way through
    signals: bool = False  # the reference-signal album


def mmss(s: str) -> int:
    m, sec = s.split(":")
    return int(m) * 60 + int(sec)


ALBUMS: list[Album] = [
    Album("Hollow Pine Ensemble", "Lanterns Over Kesseldorf", "Jazz", 2024, 96000, 24,
          [("Lamplighter's Walk", mmss("5:12")), ("Kesseldorf at Dusk", mmss("6:40")), ("Slow Tram Home", mmss("4:58")),
           ("Brass in the Fog", mmss("7:21")), ("Midnight Ledger", mmss("5:47")), ("The Long Platform", mmss("8:03")),
           ("Lanterns", mmss("4:36"))],
          ("#2b1d12", "#c8863a", "#f1d9a6"), "rings", full={1, 2, 3}),
    Album("霧野ことり", "夜明けの鉱石ラジオ", "J-Pop", 2023, 44100, 16,
          [("鉱石ラジオ", mmss("4:12")), ("水曜日の窓辺", mmss("3:55")), ("ガラスの街灯", mmss("4:31")),
           ("ひとりぶんの朝", mmss("3:48")), ("遠回りの帰り道", mmss("4:20")), ("周波数", mmss("5:02")),
           ("おやすみ、灯台", mmss("4:44")), ("夜明け", mmss("3:36"))],
          ("#1c2340", "#e8a0a8", "#f6e7c8"), "sun"),
    Album("Orchestra Vintermark", "Suite for Glass Harmonics", "Classical", 2022, 192000, 24,
          [("I. Prelude", mmss("6:18")), ("II. Allemande", mmss("7:42")), ("III. Courante", mmss("4:09")),
           ("IV. Sarabande", mmss("8:55")), ("V. Gigue", mmss("5:27"))],
          ("#0f1a1c", "#7fb3b0", "#e4efe9"), "lines"),
    Album("Neon Fathom", "Signal Bloom", "Electronic", 2025, 44100, 16,
          [("Carrier Wave", mmss("5:31")), ("Bloom", mmss("6:02")), ("Phase Garden", mmss("4:47")),
           ("Undertow", mmss("7:15")), ("Low Orbit", mmss("5:58")), ("Afterglow Protocol", mmss("6:26"))],
          ("#120a24", "#8f5cf2", "#4fe0d4"), "grid"),
    Album("Ines Calloway Trio", "Night Ferry to Valdera", "Jazz", 2021, 96000, 24,
          [("Night Ferry", mmss("6:33")), ("Valdera Blue", mmss("5:19")), ("Harbour Lights", mmss("7:04")),
           ("Two Coffees Later", mmss("4:51")), ("Fog Signal", mmss("6:12")), ("Arrival", mmss("5:40"))],
          ("#101820", "#3f6e9a", "#e2c08d"), "waves"),
    Album("灯台守楽団", "潮汐表", "Folk", 2020, 48000, 24,
          [("満ち潮", mmss("4:26")), ("岬の家", mmss("5:13")), ("潮汐表", mmss("6:01")),
           ("網をつくろう", mmss("3:59")), ("引き潮", mmss("5:35")), ("灯台守のうた", mmss("4:48"))],
          ("#1a1f1a", "#9cb36b", "#efe6c9"), "sun"),
    Album("Static Orchard", "Paper Satellites", "Rock", 2024, 44100, 16,
          [("Paper Satellites", mmss("3:41")), ("Wires", mmss("4:02")), ("Orchard Road", mmss("3:57")),
           ("Feedback Season", mmss("4:33")), ("Small Hours", mmss("5:10")), ("Ground Station", mmss("4:15")),
           ("Last Transmission", mmss("6:08"))],
          ("#1e1414", "#d0503c", "#efe2d0"), "stripes"),
    Album("白樺トーンズ", "冬の真空管", "Pop", 2025, 44100, 16,
          [("ウォームアップ", mmss("3:33")), ("冬の真空管", mmss("4:18")), ("アンバー", mmss("3:52")),
           ("雪の日のレコード", mmss("4:41")), ("ヒーターの灯", mmss("4:05")), ("春を待つ", mmss("5:20"))],
          ("#1b1612", "#f0a040", "#f4ecdf"), "rings"),
    Album("Quiet Meridian", "Tidal Clocks", "Ambient", 2023, 88200, 24,
          [("Tidal Clock I", mmss("9:12")), ("Salt Hours", mmss("8:24")), ("Tidal Clock II", mmss("10:05")),
           ("Shoreline Drift", mmss("7:48"))],
          ("#0b1416", "#4f8f9a", "#cfe3e0"), "waves"),
    Album("Marlow & The Tidewater Six", "Copper Coast Sessions", "Blues", 2019, 44100, 16,
          [("Copper Coast", mmss("4:44")), ("Tidewater Shuffle", mmss("3:58")), ("Rain on the Pier", mmss("5:26")),
           ("Six O'Clock Boat", mmss("4:12")), ("Salt & Smoke", mmss("5:03")), ("Goodnight, Harbour", mmss("4:37"))],
          ("#1d150f", "#b8733a", "#e9d6b8"), "stripes"),
    Album("DE:ZIGN Test Signals", "Reference Signals", "Test", 2026, 44100, 16,
          [("1 kHz, -20 dBFS", 60), ("1 kHz, -90 dBFS", 60), ("Sweep 20 Hz - 20 kHz", 30),
           ("Pink noise, -20 dBFS RMS", 60), ("Inter-sample peak: fs/4, 45°, 0 dBFS", 30)],
          ("#141414", "#e0a040", "#d8d8d8"), "grid", signals=True),
]

CONTENT_SECONDS = 20.0


# ---------------------------------------------------------------------------
# Sound
# ---------------------------------------------------------------------------

def tones(rate: int, seconds: float, seed: int, level_db: float) -> np.ndarray:
    """A slow progression of soft chords: sums of sine partials with gentle envelopes."""
    rng = np.random.default_rng(seed)
    n = int(rate * seconds)
    t = np.arange(n) / rate
    out = np.zeros((n, 2))
    roots = [220.0, 246.94, 196.0, 174.61, 261.63, 233.08]
    chord_len = 4.0
    k = rng.integers(0, len(roots))
    start = 0.0
    while start < seconds:
        root = roots[k % len(roots)] * (2.0 ** int(rng.integers(-1, 1)))
        minor = rng.random() < 0.5
        notes = [root, root * (2 ** ((3 if minor else 4) / 12)), root * 1.5, root * 2]
        a = int(start * rate)
        b = min(n, int((start + chord_len + 1.0) * rate))
        tt = t[a:b] - start
        env = np.minimum(1.0, tt / 0.6) * np.exp(-tt / 3.5)
        for i, f in enumerate(notes):
            pan = 0.3 + 0.4 * ((i * 0.37 + k * 0.11) % 1.0)
            partial = np.sin(2 * np.pi * f * tt + rng.random() * 6.28) * (0.6 / (i + 1))
            partial += 0.15 * np.sin(2 * np.pi * 2 * f * tt) / (i + 1)
            out[a:b, 0] += partial * env * (1 - pan)
            out[a:b, 1] += partial * env * pan
        # A soft "bass" note and a light tick on each beat.
        bass = np.sin(2 * np.pi * root / 2 * tt) * np.exp(-tt / 1.2) * 0.5
        out[a:b, 0] += bass
        out[a:b, 1] += bass
        start += chord_len
        k += rng.integers(1, 3)
    peak = np.max(np.abs(out)) or 1.0
    return out / peak * 10 ** (level_db / 20)


def signal(name: str, rate: int, seconds: float) -> np.ndarray:
    n = int(rate * seconds)
    t = np.arange(n) / rate
    if name.startswith("1 kHz, -20"):
        x = np.sin(2 * np.pi * 1000 * t) * 10 ** (-20 / 20)
    elif name.startswith("1 kHz, -90"):
        x = np.sin(2 * np.pi * 1000 * t) * 10 ** (-90 / 20)
    elif name.startswith("Sweep"):
        f0, f1 = 20.0, 20000.0
        x = np.sin(2 * np.pi * f0 * seconds / math.log(f1 / f0) * (np.exp(t / seconds * math.log(f1 / f0)) - 1)) * 0.5
    elif name.startswith("Pink"):
        rng = np.random.default_rng(1)
        white = rng.standard_normal(n)
        spec = np.fft.rfft(white)
        f = np.fft.rfftfreq(n, 1 / rate)
        f[0] = f[1]
        x = np.fft.irfft(spec / np.sqrt(f), n)
        x *= 10 ** (-20 / 20) / np.sqrt(np.mean(x**2))
    elif name.startswith("Inter-sample"):
        # The acceptance test's signal (CLAUDE.md chapter 5): fs/4, phase 45°, samples at 0 dBFS: +3.01 dBTP.
        x = np.sin(2 * np.pi * (rate / 4) * t + np.pi / 4) * math.sqrt(2)
        x = np.clip(x, -1.0, 1.0 - 2**-15)
    else:
        raise ValueError(name)
    return np.stack([x, x], axis=1)


def write_track(path: Path, album: Album, number: int, title: str, seconds: int):
    rate = album.rate
    if album.signals:
        sound = signal(title, rate, seconds)
    else:
        length = seconds if number in album.full else min(seconds, CONTENT_SECONDS)
        seed = zlib.crc32(f"{album.title}/{number}".encode())  # the same sound on every run
        sound = tones(rate, length, seed, level_db=-6.0 - (number % 3) * 2.0)
        total = int(rate * seconds)
        if len(sound) < total:
            fade = min(len(sound), int(rate * 2))
            sound[-fade:] *= np.linspace(1, 0, fade)[:, None]
            sound = np.concatenate([sound, np.zeros((total - len(sound), 2))])
    subtype = "PCM_24" if album.bits == 24 else "PCM_16"
    sf.write(path, sound, rate, subtype=subtype, format="FLAC")
    tags = FLAC(path)
    tags["TITLE"] = title
    tags["ARTIST"] = album.artist
    tags["ALBUMARTIST"] = album.artist
    tags["ALBUM"] = album.title
    tags["GENRE"] = album.genre
    tags["DATE"] = str(album.year)
    tags["TRACKNUMBER"] = str(number)
    tags["TRACKTOTAL"] = str(len(album.tracks))
    tags.save()


# ---------------------------------------------------------------------------
# Covers
# ---------------------------------------------------------------------------

def hex_rgb(h: str) -> tuple[int, int, int]:
    return tuple(int(h[i : i + 2], 16) for i in (1, 3, 5))  # type: ignore[return-value]


def cover(album: Album, path: Path, size: int = 800):
    bg, accent, light = (hex_rgb(c) for c in album.palette)
    img = Image.new("RGB", (size, size), bg)
    # A soft vertical gradient toward the accent.
    grad = Image.linear_gradient("L").resize((size, size))
    img = Image.composite(Image.new("RGB", (size, size), tuple(int(c * 0.55) for c in accent)), img, grad.point(lambda v: v // 3))
    d = ImageDraw.Draw(img)
    c = size / 2
    if album.pattern == "rings":
        for i, r in enumerate(range(int(size * 0.42), 10, -int(size * 0.035))):
            col = accent if i % 2 == 0 else bg
            d.ellipse([c - r, c - r, c + r, c + r], outline=col, width=max(2, size // 160))
        d.ellipse([c - size * 0.06, c - size * 0.06, c + size * 0.06, c + size * 0.06], fill=light)
    elif album.pattern == "sun":
        d.ellipse([c - size * 0.22, size * 0.28, c + size * 0.22, size * 0.72], fill=accent)
        for i in range(9):
            y = size * 0.55 + i * size * 0.045
            d.rectangle([0, y, size, y + size * 0.018], fill=bg)
        d.line([size * 0.1, size * 0.8, size * 0.9, size * 0.8], fill=light, width=max(2, size // 200))
    elif album.pattern == "lines":
        for i in range(24):
            x = size * 0.12 + i * size * 0.032
            h = size * (0.15 + 0.55 * abs(math.sin(i * 0.45)))
            d.line([x, c - h / 2, x, c + h / 2], fill=accent if i % 3 else light, width=max(2, size // 220))
    elif album.pattern == "grid":
        step = size // 16
        for gx in range(1, 16):
            for gy in range(1, 16):
                r = step * 0.18 * (1 + math.sin(gx * 0.7) * math.cos(gy * 0.5))
                col = accent if (gx + gy) % 5 else light
                d.ellipse([gx * step - r, gy * step - r, gx * step + r, gy * step + r], fill=col)
    elif album.pattern == "waves":
        for j in range(14):
            pts = [(x, size * (0.2 + j * 0.045) + math.sin(x / size * 6.28 * 1.5 + j * 0.4) * size * 0.05)
                   for x in range(0, size + 1, 8)]
            d.line(pts, fill=accent if j % 4 else light, width=max(2, size // 260))
    elif album.pattern == "stripes":
        for i in range(-size, size, size // 12):
            d.polygon([(i, size), (i + size // 24, size), (i + size + size // 24, 0), (i + size, 0)], fill=accent)
        d.rectangle([size * 0.18, size * 0.18, size * 0.82, size * 0.82], fill=bg)
        d.rectangle([size * 0.24, size * 0.24, size * 0.76, size * 0.76], outline=light, width=max(2, size // 200))
    img = img.filter(ImageFilter.GaussianBlur(0.6))
    img.save(path, quality=90)


def safe(name: str) -> str:
    return "".join("_" if ch in '<>:"/\\|?*' else ch for ch in name)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for album in ALBUMS:
        folder = out / safe(album.artist) / safe(album.title)
        folder.mkdir(parents=True, exist_ok=True)
        cover(album, folder / "cover.jpg")
        for number, (title, seconds) in enumerate(album.tracks, start=1):
            path = folder / f"{number:02d} {safe(title)}.flac"
            if not path.exists():
                write_track(path, album, number, title, seconds)
        print(f"{album.artist} / {album.title}: {len(album.tracks)} tracks")


if __name__ == "__main__":
    main()
