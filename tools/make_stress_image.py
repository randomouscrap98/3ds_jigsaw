# /// script
# dependencies = ["numpy", "pillow"]
# ///
# Worst-case test pictures for the 3DS: 2048 x 2048, hard to compress (big files, slow SD reads)
# and full of detail ETC1 (Max) and RGBA4444 (High) handle badly. Same pixels every run (fixed seed).
# Usage: uv run tools/make_stress_image.py OUTDIR
#   stress_rgb8.png    8-bit RGB PNG: the realistic worst (a PNG export of a busy picture)
#   stress_q100.jpg    JPEG quality 100, no chroma subsampling
#   stress_rgba16.png  16-bit RGBA PNG of the same pixels: abusive, stb_image decodes it at
#                      8 bytes per pixel (32 MB) before converting, a decode-memory test
import os
import struct
import sys
import zlib

import numpy as np
from PIL import Image

SIZE = 2048
TILE = 512  # 4 x 4 tiles, 8 patterns twice (the second time turned and recolored)


def hsv(h, s, v):
    # h, s, v arrays in 0..1 -> uint8 RGB
    i = np.floor(h * 6).astype(int) % 6
    f = h * 6 - np.floor(h * 6)
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    r = np.choose(i, [v, q, p, p, t, v])
    g = np.choose(i, [t, v, v, q, p, p])
    b = np.choose(i, [p, p, t, v, v, q])
    return (np.stack([r, g, b], -1) * 255).astype(np.uint8)


def pattern(k, rng, hue):
    y, x = np.mgrid[0:TILE, 0:TILE]
    one = np.ones((TILE, TILE))
    if k == 0:  # pixel checkerboard of complementary colors (one color per ETC1 half block)
        return hsv((hue + 0.5 * ((x + y) % 2)) % 1, one, one)
    if k == 1:  # 1 px stripes cycling red, green, blue
        return hsv((hue + (x % 3) / 3) % 1, one, one)
    if k == 2:  # thin diagonal lines over a contrasting color
        line = ((x + 2 * y) % 7) == 0
        return hsv((hue + 0.5 * line) % 1, one, np.where(line, 1.0, 0.6))
    if k == 3:  # hue ramp in 3 px steps (block edges never line up)
        return hsv((hue + (x // 3) * 0.037 + (y // 5) * 0.011) % 1, one, one)
    if k == 4:  # confetti: random saturated 2x2 dots, offset so they straddle 4x4 blocks
        cells = rng.random((TILE // 2 + 1, TILE // 2 + 1))
        h = cells[(y + 1) // 2, (x + 1) // 2]
        return hsv(h, one, 0.5 + 0.5 * cells[(x + 1) // 2, (y + 1) // 2])
    if k == 5:  # full random noise: incompressible
        return rng.integers(0, 256, (TILE, TILE, 3), dtype=np.uint8)
    if k == 6:  # zone plate: rings getting finer to the corners, colored (aliasing)
        r2 = ((x - TILE / 2) ** 2 + (y - TILE / 2) ** 2) / TILE
        return hsv((hue + r2 * 0.01) % 1, 0.8 * one, 0.5 + 0.5 * np.cos(r2 * np.pi * 1.7))
    # smooth dark gradients, no noise: banding in RGBA4444, blockiness in ETC1
    return hsv((hue + x / TILE * 0.3) % 1, 0.4 + 0.6 * y / TILE, 0.05 + 0.25 * x / TILE)


def build():
    rng = np.random.default_rng(1234)
    img = np.zeros((SIZE, SIZE, 3), np.uint8)
    n = SIZE // TILE
    for t in range(n * n):
        k, second = t % 8, t >= 8
        tile = pattern(k, rng, (t * 0.13) % 1)
        if second:
            tile = np.rot90(tile).copy()
        if k != 7:  # light noise everywhere else: hard to compress, patterns still show
            noise = rng.integers(-12, 13, tile.shape)
            tile = np.clip(tile.astype(int) + noise, 0, 255).astype(np.uint8)
        ty, tx = divmod(t, n)
        img[ty * TILE:(ty + 1) * TILE, tx * TILE:(tx + 1) * TILE] = tile
    return img


def write_png16(path, rgb):
    # 16-bit RGBA, opaque; filter 0, level 9
    h, w, _ = rgb.shape
    px = np.empty((h, w, 4), ">u2")
    px[..., :3] = rgb.astype(np.uint16) * 257
    px[..., 3] = 0xFFFF
    raw = b"".join(b"\0" + px[r].tobytes() for r in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 16, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_stress_image.py OUTDIR")
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    img = build()
    Image.fromarray(img).save(os.path.join(out, "stress_rgb8.png"), compress_level=9)
    Image.fromarray(img).save(os.path.join(out, "stress_q100.jpg"), quality=100, subsampling=0)
    write_png16(os.path.join(out, "stress_rgba16.png"), img)
    for name in ("stress_rgb8.png", "stress_q100.jpg", "stress_rgba16.png"):
        print(f"{name}: {os.path.getsize(os.path.join(out, name)) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
