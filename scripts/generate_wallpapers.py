#!/usr/bin/env python3
"""Draws the shipped Material 3 Expressive wallpapers.

Flat tonal palettes and the M3 Expressive shape set (cookies, flowers, wavy
circles, pills, arches, squircles), at 4K. Deterministic: the same seed
draws the same picture.

    scripts/generate_wallpapers.py [OUTDIR]   (default shell/data/wallpapers)

Needs pycairo; ImageMagick turns the PNGs into JPEGs.
"""
import math
import os
import random
import subprocess
import sys
import tempfile

import cairo

W, H = 3840, 2160


def rgb(hexstr):
    h = hexstr.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))


# ── Shapes, as closed paths around (0, 0) with radius about r ────────────────

def polar(ctx, r_of, steps=720):
    for i in range(steps + 1):
        t = 2 * math.pi * i / steps
        r = r_of(t)
        (ctx.move_to if i == 0 else ctx.line_to)(r * math.cos(t), r * math.sin(t))
    ctx.close_path()


def cookie(ctx, r, lobes=9, depth=0.08):
    polar(ctx, lambda t: r * (1 - depth + depth * math.cos(lobes * t)))


def flower(ctx, r, petals=6):
    # Rounder petals than a cosine: the power flattens the valleys
    polar(ctx, lambda t: r * (0.72 + 0.28 * abs(math.cos(petals * t / 2)) ** 0.6))


def wavy(ctx, r, waves=14):
    cookie(ctx, r, waves, 0.035)


def squircle(ctx, r, p=4.0):
    polar(ctx, lambda t: r / (abs(math.cos(t)) ** p + abs(math.sin(t)) ** p) ** (1 / p))


def pill(ctx, r, ratio=2.6):
    w, h = r * ratio, r
    ctx.new_sub_path()
    ctx.arc(-w / 2 + h / 2, 0, h / 2, math.pi / 2, 3 * math.pi / 2)
    ctx.arc(w / 2 - h / 2, 0, h / 2, -math.pi / 2, math.pi / 2)
    ctx.close_path()


def arch(ctx, r):
    ctx.new_sub_path()
    ctx.arc(0, 0, r / 2, math.pi, 0)
    ctx.line_to(r / 2, r * 1.1)
    ctx.line_to(-r / 2, r * 1.1)
    ctx.close_path()


def clover(ctx, r):
    cookie(ctx, r, 4, 0.16)


SHAPES = [cookie, flower, wavy, squircle, pill, arch, clover]


def draw(ctx, shape, x, y, r, angle, color, **kw):
    ctx.save()
    ctx.translate(x, y)
    ctx.rotate(angle)
    shape(ctx, r, **kw)
    ctx.set_source_rgb(*color)
    ctx.fill()
    ctx.restore()


def background(ctx, top, bottom):
    g = cairo.LinearGradient(0, 0, W * 0.4, H)
    g.add_color_stop_rgb(0, *top)
    g.add_color_stop_rgb(1, *bottom)
    ctx.set_source(g)
    ctx.paint()


def shadow(ctx, shape, x, y, r, angle, dark, **kw):
    # A soft drop: a few widening, fading copies below the shape
    for i in range(6, 0, -1):
        ctx.save()
        ctx.translate(x, y + r * 0.035 * i)
        ctx.rotate(angle)
        shape(ctx, r * (1 + 0.006 * i), **kw)
        ctx.set_source_rgba(0, 0, 0, (0.05 if dark else 0.025))
        ctx.fill()
        ctx.restore()


# ── Compositions ─────────────────────────────────────────────────────────────

def scatter(ctx, pal, rnd):
    """Large shapes spilling off the edges, a quiet middle for the clock."""
    background(ctx, pal["bg"], pal["bg2"])
    spots = [(0.08, 0.12, 0.42), (0.92, 0.18, 0.32), (0.18, 0.92, 0.36), (0.86, 0.86, 0.46),
             (0.52, 1.02, 0.22), (0.5, -0.04, 0.18)]
    for i, (fx, fy, fr) in enumerate(spots):
        shape = SHAPES[(i + rnd.randrange(len(SHAPES))) % len(SHAPES)]
        r = fr * H
        a = rnd.uniform(0, math.pi)
        c = pal["tones"][i % len(pal["tones"])]
        shadow(ctx, shape, fx * W, fy * H, r, a, pal["dark"])
        draw(ctx, shape, fx * W, fy * H, r, a, c)


def hero(ctx, pal, rnd):
    """One big shape off to the side, with a few small companions."""
    background(ctx, pal["bg"], pal["bg2"])
    big = rnd.choice([cookie, flower, clover])
    x, y, r = W * 0.74, H * 0.56, H * 0.62
    a = rnd.uniform(0, math.pi)
    shadow(ctx, big, x, y, r, a, pal["dark"])
    draw(ctx, big, x, y, r, a, pal["tones"][1])
    draw(ctx, big, x, y, r * 0.62, a + 0.3, pal["tones"][2])
    for fx, fy, fr, shape in [(0.14, 0.22, 0.12, wavy), (0.26, 0.8, 0.09, pill), (0.08, 0.66, 0.06, squircle)]:
        a = rnd.uniform(0, math.pi)
        shadow(ctx, shape, fx * W, fy * H, fr * H, a, pal["dark"])
        draw(ctx, shape, fx * W, fy * H, fr * H, a, pal["tones"][0])


def pills(ctx, pal, rnd):
    """Diagonal bands of pills, as stacked tonal stripes."""
    background(ctx, pal["bg"], pal["bg2"])
    ctx.save()
    ctx.translate(W / 2, H / 2)
    ctx.rotate(-math.pi / 7)
    h = H * 0.17
    for row in range(-5, 6):
        y = row * h * 1.18
        x = -W * 0.9 + rnd.uniform(0, h * 2)
        while x < W * 0.9:
            length = rnd.uniform(1.6, 4.2)
            c = pal["tones"][(row + int(x / h)) % len(pal["tones"])]
            draw(ctx, pill, x + length * h / 2, y, h, 0, c, ratio=length)
            x += length * h + h * 0.22
    ctx.restore()


def rings(ctx, pal, rnd):
    """Concentric wavy rings rippling out from a corner."""
    background(ctx, pal["bg"], pal["bg2"])
    cx, cy = W * rnd.choice([0.12, 0.88]), H * 0.9
    tones = pal["tones"]
    r = math.hypot(W, H) * 1.05
    i = 0
    # Few, broad rings: the waves stay gentle all the way in
    while r > H * 0.22:
        draw(ctx, wavy, cx, cy, r, i * 0.17, tones[i % len(tones)], waves=9)
        r *= 0.76
        i += 1


PALETTES = {
    "coral": {"dark": False, "bg": rgb("#FFE9E4"), "bg2": rgb("#FFD8CF"),
              "tones": [rgb("#FFB4A8"), rgb("#F0806C"), rgb("#C2412E"), rgb("#FFF4F1")]},
    "sage": {"dark": False, "bg": rgb("#EEF4E2"), "bg2": rgb("#D9E7C6"),
             "tones": [rgb("#B9D69C"), rgb("#7FA45C"), rgb("#4C662B"), rgb("#F7FBEF")]},
    "lavender": {"dark": False, "bg": rgb("#F3EDFF"), "bg2": rgb("#E4D8FF"),
                 "tones": [rgb("#D0BCFF"), rgb("#9A82DB"), rgb("#6750A4"), rgb("#FBF8FF")]},
    "sunset": {"dark": False, "bg": rgb("#FFF0DC"), "bg2": rgb("#FFDDB3"),
               "tones": [rgb("#FFC27A"), rgb("#F2994A"), rgb("#B85E14"), rgb("#FFF8EE")]},
    "ocean": {"dark": True, "bg": rgb("#0B1D2E"), "bg2": rgb("#06121E"),
              "tones": [rgb("#14324C"), rgb("#1E4D6B"), rgb("#4C8DB5"), rgb("#8ECFF2")]},
    "mint": {"dark": True, "bg": rgb("#0F1F1A"), "bg2": rgb("#08130F"),
             "tones": [rgb("#1A3A30"), rgb("#2A5E4D"), rgb("#4E9C80"), rgb("#9CF2D1")]},
    "rose": {"dark": True, "bg": rgb("#2B0B18"), "bg2": rgb("#1C0610"),
             "tones": [rgb("#4D1730"), rgb("#7D2A4D"), rgb("#B84C76"), rgb("#FFB0C9")]},
    "graphite": {"dark": True, "bg": rgb("#1C1B1F"), "bg2": rgb("#121115"),
                 "tones": [rgb("#2B2930"), rgb("#3E3B45"), rgb("#5E5A68"), rgb("#CAC4D0")]},
}

WALLPAPERS = [
    ("shapes-coral", "coral", scatter, 3),
    ("bloom-lavender", "lavender", hero, 11),
    ("pills-sunset", "sunset", pills, 5),
    ("ripples-sage", "sage", rings, 2),
    ("shapes-ocean", "ocean", scatter, 8),
    ("bloom-rose", "rose", hero, 4),
    ("pills-mint", "mint", pills, 9),
    ("ripples-graphite", "graphite", rings, 6),
]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "shell/data/wallpapers")
    os.makedirs(out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name, palette, compose, seed in WALLPAPERS:
            surface = cairo.ImageSurface(cairo.FORMAT_RGB24, W, H)
            ctx = cairo.Context(surface)
            ctx.set_antialias(cairo.ANTIALIAS_BEST)
            compose(ctx, PALETTES[palette], random.Random(seed))
            png = os.path.join(tmp, name + ".png")
            surface.write_to_png(png)
            dst = os.path.join(out, name + ".jpg")
            subprocess.run(["magick", png, "-quality", "90", "-sampling-factor", "4:2:0", "-strip", dst], check=True)
            print(dst)


if __name__ == "__main__":
    main()
