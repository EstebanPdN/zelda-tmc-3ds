#!/usr/bin/env python3
"""
PNG -> TMC 4bpp tile blob converter.

Reads a PNG via ImageMagick `convert` (no Python deps), quantises its
colours to 15 + transparent, tiles the result into 8x8 blocks (GBA OBJ
1D mapping = row-major within each tile, row-major over tiles) and
writes raw 4bpp pixel data. Output matches the format of TMC's runtime
asset files:
  - assets/gfx/gfx_*_*x*_4bpp_uncompressed.bin
  - assets/sprites/<symbol>/*.4bpp

Usage:
  png_to_tmc4bpp.py <input.png> <output.bin>
                    [--width=N] [--height=N]
                    [--palette=<pal.bin>]

If --width / --height are omitted the PNG dimensions are used; they must
be multiples of 8. If --palette is given the script reuses that 16-colour
GBA-format palette (32 bytes RGB555 LE) and quantises against it;
otherwise a new palette is written next to the output as <output>.pal.bin.

Index 0 is reserved as transparent (alpha < 128 in the source).
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def rgb555(r, g, b):
    return ((r >> 3) & 0x1F) | (((g >> 3) & 0x1F) << 5) | (((b >> 3) & 0x1F) << 10)


def load_palette(path):
    data = Path(path).read_bytes()
    if len(data) < 32:
        sys.exit(f"palette {path} is {len(data)} bytes, need at least 32")
    pal = []
    for i in range(16):
        c = struct.unpack_from("<H", data, i * 2)[0]
        r = ((c >> 0)  & 0x1F) << 3
        g = ((c >> 5)  & 0x1F) << 3
        b = ((c >> 10) & 0x1F) << 3
        pal.append((r, g, b))
    return pal


def build_palette(rgba_pixels):
    counts = {}
    for r, g, b, a in rgba_pixels:
        if a < 128:
            continue
        key = (r & 0xF8, g & 0xF8, b & 0xF8)  # round to RGB555 buckets
        counts[key] = counts.get(key, 0) + 1
    top = sorted(counts.items(), key=lambda kv: -kv[1])[:15]
    pal = [(0, 0, 0)]  # idx 0 = transparent
    pal.extend(k for k, _ in top)
    while len(pal) < 16:
        pal.append((0, 0, 0))
    return pal


def closest_index(r, g, b, a, palette):
    if a < 128:
        return 0
    best_i, best_d = 0, 1 << 30
    for i in range(1, 16):
        pr, pg, pb = palette[i]
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if d < best_d:
            best_d, best_i = d, i
    return best_i


def pack_4bpp_tiles(indices, width, height):
    if width % 8 or height % 8:
        sys.exit(f"image {width}x{height} is not a multiple of 8")
    tw, th = width // 8, height // 8
    out = bytearray(tw * th * 32)
    o = 0
    for ty in range(th):
        for tx in range(tw):
            for py in range(8):
                for px in range(0, 8, 2):
                    src_y = ty * 8 + py
                    src_x = tx * 8 + px
                    lo = indices[src_y * width + src_x]
                    hi = indices[src_y * width + src_x + 1]
                    out[o] = (lo & 0xF) | ((hi & 0xF) << 4)
                    o += 1
    return bytes(out)


def png_to_rgba(png_path, target_w, target_h):
    """Use ImageMagick `convert` to decode PNG → raw RGBA bytes. Returns
    (w, h, [(r,g,b,a)...] of length w*h)."""
    convert = "convert"
    with tempfile.NamedTemporaryFile(suffix=".rgba", delete=False) as tmp:
        tmp_path = tmp.name
    try:
        cmd = [convert, str(png_path)]
        if target_w and target_h:
            cmd += ["-resize", f"{target_w}x{target_h}!"]
        cmd += ["-depth", "8", "RGBA:" + tmp_path]
        subprocess.run(cmd, check=True)
        data = Path(tmp_path).read_bytes()
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass

    if target_w and target_h:
        w, h = target_w, target_h
    else:
        identify = subprocess.run(
            ["identify", "-format", "%w %h", str(png_path)],
            check=True, capture_output=True, text=True,
        )
        w, h = map(int, identify.stdout.split())

    expected = w * h * 4
    if len(data) != expected:
        sys.exit(f"convert produced {len(data)} bytes, expected {expected} for {w}x{h}")

    pixels = []
    for i in range(0, len(data), 4):
        pixels.append((data[i], data[i + 1], data[i + 2], data[i + 3]))
    return w, h, pixels


def main():
    ap = argparse.ArgumentParser(description="PNG → TMC 4bpp tile blob")
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--width", type=int, default=0)
    ap.add_argument("--height", type=int, default=0)
    ap.add_argument("--palette")
    args = ap.parse_args()

    w, h, rgba = png_to_rgba(args.input, args.width, args.height)

    palette = load_palette(args.palette) if args.palette else build_palette(rgba)
    indices = [closest_index(r, g, b, a, palette) for (r, g, b, a) in rgba]
    blob = pack_4bpp_tiles(indices, w, h)

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(blob)
    print(f"wrote {out_path} ({len(blob)} bytes, {w}x{h} = {w * h // 64} tiles)")

    if not args.palette:
        pal_path = out_path.with_suffix(".pal.bin")
        pal_bytes = bytearray()
        for r, g, b in palette:
            pal_bytes += struct.pack("<H", rgb555(r, g, b))
        pal_path.write_bytes(pal_bytes)
        print(f"wrote {pal_path} ({len(pal_bytes)} bytes, 16 colours)")


if __name__ == "__main__":
    main()
