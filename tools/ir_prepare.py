"""Prepares candidate IRs for listening in the app (task 9-6, docs/ir-candidates.md).

The app's SPACE buttons (CLUB, HALL, ARENA) each take a mono or stereo IR file
(settings screen: 空間 → IR のファイル). Some candidates come in other shapes:

  pair     two mono files (MAIR: one file per microphone, FL and FR) -> one stereo file
  bformat  first-order B-format (Central Hall, York: W X Y Z) -> a stereo pair of
           virtual cardioid microphones, +-45 degrees by default (a coincident pair, like XY)
  trim     a mono or stereo file as it is, only cut at the first arrival (see --trim-db)
  info     prints a file's format, length and peak

--trim-db (pair, bformat, trim): cuts what comes before the first sample
above this level relative to the peak (e.g. -40), keeping 1 ms before it
(faded in). Measured IRs often start with the source's travel time and
noise (Central Hall: about -48 dB), and some synthesized ones with a slow
rise; the app removes only what is below -80 dB, so without this the wet
signal would start later for some IRs than for others.

The output is 32-bit float WAV at the input's rate, with the input's level
(nothing is normalised here: the app normalises each IR to unit energy).
Only numpy is needed. Reads PCM 16/24/32-bit and float 32/64-bit WAV (also
WAVE_FORMAT_EXTENSIBLE).

  python tools/ir_prepare.py pair FL.wav FR.wav out.wav [--trim-db -40]
  python tools/ir_prepare.py bformat in.wav out.wav [--order fuma|ambix] [--angle 90] [--trim-db -40]
  python tools/ir_prepare.py trim in.wav out.wav [--trim-db -40]
  python tools/ir_prepare.py info in.wav
"""

import argparse
import struct
import sys

import numpy as np


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] not in (b"RIFF", b"RF64") or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV file")
    pos, fmt, samples = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, channels, rate = struct.unpack("<HHI", body[:8])
            bits = struct.unpack("<H", body[14:16])[0]
            if tag == 0xFFFE:  # extensible: the subformat's first two bytes
                tag = struct.unpack("<H", body[24:26])[0]
            fmt = (tag, channels, rate, bits)
        elif cid == b"data":
            samples = body
        pos += 8 + size + (size & 1)
    if fmt is None or samples is None:
        raise ValueError(f"{path}: no fmt or data chunk")
    tag, channels, rate, bits = fmt
    if tag == 3 and bits in (32, 64):
        x = np.frombuffer(samples, dtype="<f4" if bits == 32 else "<f8").astype(np.float64)
    elif tag == 1 and bits == 16:
        x = np.frombuffer(samples, dtype="<i2").astype(np.float64) / 32768.0
    elif tag == 1 and bits == 24:
        b = np.frombuffer(samples[: len(samples) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v.astype(np.float64) / float(1 << 23)
    elif tag == 1 and bits == 32:
        x = np.frombuffer(samples, dtype="<i4").astype(np.float64) / float(1 << 31)
    else:
        raise ValueError(f"{path}: format tag {tag}, {bits} bit is not read here")
    frames = len(x) // channels
    return x[: frames * channels].reshape(frames, channels), rate


def write_wav(path, x, rate):
    x = np.asarray(x, dtype="<f4")
    frames, channels = x.shape
    data = x.tobytes()
    fmt = struct.pack("<HHIIHH", 3, channels, rate, rate * channels * 4, channels * 4, 32)
    fact = struct.pack("<I", frames)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"fact" + struct.pack("<I", 4) + fact
    body += b"data" + struct.pack("<I", len(data)) + data
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(body)) + body)


def trim_start(x, rate, trim_db):
    """Cuts before the first sample above trim_db (relative to the peak), keeping 1 ms, faded in."""
    if trim_db is None:
        return x
    m = np.max(np.abs(x), axis=1)
    peak = m.max()
    if peak <= 0:
        return x
    first = int(np.argmax(m > peak * 10 ** (trim_db / 20.0)))
    keep = int(round(0.001 * rate))
    start = max(0, first - keep)
    y = x[start:].copy()
    n = first - start
    if n > 0:
        y[:n] *= (0.5 - 0.5 * np.cos(np.pi * np.arange(n) / n))[:, None]
    return y


def trim(src, out, trim_db=-40.0):
    x, rate = read_wav(src)
    st = trim_start(x[:, :2], rate, trim_db)
    write_wav(out, st, rate)
    return st, rate


def pair(left, right, out, trim_db=None):
    l, rl = read_wav(left)
    r, rr = read_wav(right)
    if rl != rr:
        raise ValueError(f"the two files have different rates ({rl} and {rr} Hz)")
    n = max(len(l), len(r))
    st = np.zeros((n, 2))
    st[: len(l), 0] = l[:, 0]
    st[: len(r), 1] = r[:, 0]
    st = trim_start(st, rl, trim_db)
    write_wav(out, st, rl)
    return st, rl


def bformat(src, out, order="fuma", angle=90.0, trim_db=None):
    x, rate = read_wav(src)
    if x.shape[1] < 4:
        raise ValueError(f"{src}: B-format needs 4 channels, it has {x.shape[1]}")
    if order == "fuma":  # W X Y Z, W at -3 dB
        w, X, Y = x[:, 0] * np.sqrt(2.0), x[:, 1], x[:, 2]
    else:  # AmbiX: W Y Z X (ACN), SN3D
        w, Y, X = x[:, 0], x[:, 1], x[:, 3]
    half = np.radians(angle / 2.0)
    # A cardioid pointing at azimuth a (counter-clockwise from the front): 0.5 (W + X cos a + Y sin a).
    left = 0.5 * (w + X * np.cos(half) + Y * np.sin(half))
    right = 0.5 * (w + X * np.cos(-half) + Y * np.sin(-half))
    st = trim_start(np.stack([left, right], axis=1), rate, trim_db)
    write_wav(out, st, rate)
    return st, rate


def info(path):
    x, rate = read_wav(path)
    peak = float(np.max(np.abs(x))) if x.size else 0.0
    peak_db = 20 * np.log10(peak) if peak > 0 else float("-inf")
    print(f"{path}: {x.shape[1]} ch, {rate} Hz, {len(x) / rate:.3f} s, peak {peak_db:.1f} dBFS")


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("pair")
    a.add_argument("left")
    a.add_argument("right")
    a.add_argument("out")
    b = sub.add_parser("bformat")
    b.add_argument("src")
    b.add_argument("out")
    b.add_argument("--order", choices=["fuma", "ambix"], default="fuma")
    b.add_argument("--angle", type=float, default=90.0, help="degrees between the two cardioids")
    t = sub.add_parser("trim")
    t.add_argument("src")
    t.add_argument("out")
    for q in (a, b):
        q.add_argument("--trim-db", type=float, default=None, help="cut before the first arrival (dB from the peak)")
    t.add_argument("--trim-db", type=float, default=-40.0, help="cut before the first arrival (dB from the peak)")
    i = sub.add_parser("info")
    i.add_argument("path")
    args = p.parse_args(argv)
    if args.cmd == "pair":
        pair(args.left, args.right, args.out, args.trim_db)
        info(args.out)
    elif args.cmd == "bformat":
        bformat(args.src, args.out, args.order, args.angle, args.trim_db)
        info(args.out)
    elif args.cmd == "trim":
        trim(args.src, args.out, args.trim_db)
        info(args.out)
    else:
        info(args.path)


if __name__ == "__main__":
    main(sys.argv[1:])
