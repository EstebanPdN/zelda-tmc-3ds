#!/usr/bin/env python3
"""Convert the Ukrainian boot logo to the port's raw splash format.

platform/3ds/assets/splash-ua.png (any size, transparent background) ->
platform/3ds/romfs/splash-ua.rgb565 (400x240, little-endian RGB565, black
background), the same format as the upstream romfs/splash.rgb565.
"""
import struct
import sys
from pathlib import Path

from PIL import Image

W, H, MARGIN = 400, 240, 4

root = Path(__file__).resolve().parent.parent
src = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "platform/3ds/assets/splash-ua.png"
dst = Path(sys.argv[2]) if len(sys.argv) > 2 else root / "platform/3ds/romfs/splash-ua.rgb565"

logo = Image.open(src).convert("RGBA")
logo = logo.crop(logo.getchannel("A").getbbox())
scale = min((W - 2 * MARGIN) / logo.width, (H - 2 * MARGIN) / logo.height)
logo = logo.resize((round(logo.width * scale), round(logo.height * scale)), Image.LANCZOS)

canvas = Image.new("RGBA", (W, H), (0, 0, 0, 255))
canvas.alpha_composite(logo, ((W - logo.width) // 2, (H - logo.height) // 2))

pixels = [((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) for r, g, b, _ in canvas.getdata()]
dst.write_bytes(struct.pack(f"<{W * H}H", *pixels))
print(f"{dst}: {W}x{H}, logo {logo.width}x{logo.height}")
