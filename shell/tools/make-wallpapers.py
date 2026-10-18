#!/usr/bin/env python3
"""Writes a few gradient PNG wallpapers (no dependencies) for headless tests."""
import os, struct, sys, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
W, H = 800, 450
palettes = [((63, 81, 181), (156, 39, 176)), ((0, 121, 107), (255, 193, 7)),
            ((183, 28, 28), (255, 112, 67)), ((33, 33, 33), (96, 125, 139)), ((2, 136, 209), (129, 212, 250))]
for n, (a, b) in enumerate(palettes):
    rows = bytearray()
    for y in range(H):
        rows.append(0)
        for x in range(W):
            t = (x / W + y / H) / 2
            rows += bytes(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b"")
    with open(os.path.join(out, f"gradient-{n + 1}.png"), "wb") as f:
        f.write(png)
