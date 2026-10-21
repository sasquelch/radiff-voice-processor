#!/usr/bin/env python3
"""Generates the original RaDiff app icon (macOS .icns + Windows .ico) without any image library.

Design: dark rounded square, amber "voice level" bars (seven vertical bars forming a speech
envelope) and a thin baseline. Run from the repo root:

    python3 src/standalone/resources/icon/make_icons.py

Writes RaDiffVoice.icns and RaDiffVoice.ico next to the other resources. The outputs are
committed, so this only needs to run when the design changes.
"""
import math
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.dirname(HERE)

BG = (24, 27, 31)
EDGE = (62, 68, 76)
AMBER = (240, 160, 48)
AMBER_DIM = (120, 84, 30)
BARS = [0.30, 0.55, 0.85, 1.00, 0.70, 0.45, 0.25]  # relative heights


def png_bytes(size, pixels):
    """pixels: list of rows, each a bytes object of RGBA."""
    raw = b"".join(b"\x00" + row for row in pixels)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def render(size):
    s = float(size)
    radius = 0.22 * s
    pad = 0.04 * s
    inner_l, inner_r = pad, s - pad
    bar_zone_l, bar_zone_r = 0.20 * s, 0.80 * s
    base_y = 0.76 * s
    top_y = 0.22 * s
    n = len(BARS)
    slot = (bar_zone_r - bar_zone_l) / n
    bar_w = slot * 0.58

    def rounded_sdf(x, y):
        # signed distance to the rounded square (negative inside)
        cx, cy = (inner_l + inner_r) / 2, (inner_l + inner_r) / 2
        hx, hy = (inner_r - inner_l) / 2 - radius, (inner_r - inner_l) / 2 - radius
        dx, dy = abs(x - cx) - hx, abs(y - cy) - hy
        ox, oy = max(dx, 0.0), max(dy, 0.0)
        return math.hypot(ox, oy) + min(max(dx, dy), 0.0) - radius

    def coverage(d, aa=1.0):
        return min(1.0, max(0.0, 0.5 - d / aa))

    rows = []
    for py in range(size):
        y = py + 0.5
        row = bytearray()
        for px in range(size):
            x = px + 0.5
            a_sq = coverage(rounded_sdf(x, y))
            if a_sq <= 0.0:
                row += b"\x00\x00\x00\x00"
                continue
            r, g, b = BG
            # subtle edge highlight
            e = coverage(abs(rounded_sdf(x, y) + 0.012 * s) - 0.006 * s)
            r, g, b = (r + (EDGE[0] - r) * e, g + (EDGE[1] - g) * e, b + (EDGE[2] - b) * e)
            # baseline
            bl = coverage(abs(y - base_y) - 0.008 * s)
            if bar_zone_l - 0.03 * s <= x <= bar_zone_r + 0.03 * s and bl > 0:
                r, g, b = (r + (AMBER_DIM[0] - r) * bl, g + (AMBER_DIM[1] - g) * bl, b + (AMBER_DIM[2] - b) * bl)
            # bars with rounded tops
            for i, h in enumerate(BARS):
                bx = bar_zone_l + slot * (i + 0.5)
                bh = (base_y - top_y) * h
                by0 = base_y - bh
                dx = abs(x - bx) - bar_w / 2
                if dx > 1.0:
                    continue
                dy = (by0 + bar_w / 2) - y
                if y >= by0 + bar_w / 2:
                    d = dx if y <= base_y else max(dx, y - base_y)
                else:
                    d = math.hypot(max(dx + bar_w / 2, 0.0), max(dy, 0.0)) - bar_w / 2
                    d = max(d, dx) if dx > 0 else d
                a = coverage(d)
                if a > 0:
                    r, g, b = (r + (AMBER[0] - r) * a, g + (AMBER[1] - g) * a, b + (AMBER[2] - b) * a)
            row += bytes((int(r + 0.5), int(g + 0.5), int(b + 0.5), int(a_sq * 255 + 0.5)))
        rows.append(bytes(row))
    return rows


def main():
    pngs = {}
    for size in (16, 32, 64, 128, 256, 512, 1024):
        pngs[size] = png_bytes(size, render(size))
        print(f"rendered {size}px")

    # Windows .ico: PNG-compressed entries (supported since Windows Vista)
    entries = [(16, pngs[16]), (32, pngs[32]), (48, png_bytes(48, render(48))), (256, pngs[256])]
    ico = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    body = b""
    for size, data in entries:
        ico += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        body += data
        offset += len(data)
    with open(os.path.join(RES, "RaDiffVoice.ico"), "wb") as f:
        f.write(ico + body)
    print("wrote RaDiffVoice.ico")

    if sys.platform == "darwin":
        with tempfile.TemporaryDirectory() as tmp:
            iconset = os.path.join(tmp, "RaDiffVoice.iconset")
            os.mkdir(iconset)
            names = {16: ["icon_16x16.png"], 32: ["icon_16x16@2x.png", "icon_32x32.png"],
                     64: ["icon_32x32@2x.png"], 128: ["icon_128x128.png"],
                     256: ["icon_128x128@2x.png", "icon_256x256.png"],
                     512: ["icon_256x256@2x.png", "icon_512x512.png"], 1024: ["icon_512x512@2x.png"]}
            for size, files in names.items():
                for name in files:
                    with open(os.path.join(iconset, name), "wb") as f:
                        f.write(pngs[size])
            subprocess.check_call(["iconutil", "-c", "icns", iconset, "-o", os.path.join(RES, "RaDiffVoice.icns")])
            print("wrote RaDiffVoice.icns")
    else:
        print("skipping .icns (needs macOS iconutil)")


if __name__ == "__main__":
    main()
