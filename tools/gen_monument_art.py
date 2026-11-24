#!/usr/bin/env python3
"""Monuments+ drafts (0.82.0 plan, owner 2026-11-21): art drawn by us, not taken from the cards.

  1. assets/icons/units/monument.png - the monument structure as a grey shaded picture in the style of the other
     unit pictures (light from the upper left), painted in the player's colour by the game (gen_icons.py packs it as
     "unit/monument"). The shape: a cathedral of spires on a stepped round base - the silhouette of Monuments+'s own
     wireframe token, so a monument never reads as a PDS or a space dock.
  2. OUTDIR/revenance_circuit.png - our own pixel art of Revenance Circuit's scene ("a kaleidoscope of dimensional fire":
     rings of dimensional gates stacked into the distance, a beam through them that turns to fire below), in the
     Retrowave look: a fixed palette, ordered (Bayer) dithering, drawn at ART_W x ART_H and shown 3x without smoothing.

Usage:  python3 tools/gen_monument_art.py [OUTDIR]      (deterministic)
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1] if len(sys.argv) > 1 else "build/monument_art"
SPRITE = "assets/icons/units/monument.png"


# ------------------------------------------------------------------ 1. the monument sprite
def sprite():
    S = 8                      # supersampling
    W, H = 64 * S, 88 * S      # final 64 x 88
    shape = Image.new("L", (W, H), 0)     # coverage
    shade = Image.new("L", (W, H), 0)     # grey (255 = lit)
    ds, dh = ImageDraw.Draw(shape), ImageDraw.Draw(shade)

    def poly(pts, grey):
        p = [(x * S, y * S) for x, y in pts]
        ds.polygon(p, fill=255)
        dh.polygon(p, fill=grey)

    def spire(cx, base, top, half, lit, dark):
        # a needle spire: lit left face, dark right face, a ridge down the middle
        poly([(cx - half, base), (cx, top), (cx, base)], lit)
        poly([(cx, top), (cx + half, base), (cx, base)], dark)

    # the stepped round base (two tiers), seen a little from above
    def ellipse(cx, cy, rx, ry, grey):
        box = [(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S]
        ds.ellipse(box, fill=255)
        dh.ellipse(box, fill=grey)

    def band(cx, top, bot, rx, ry, lit, dark):
        # a drum: two ellipses joined, lit on the left, dark on the right
        ellipse(cx, bot, rx, ry, dark)
        poly([(cx - rx, top), (cx - rx, bot), (cx, bot + ry), (cx, top + ry)], lit)
        poly([(cx, top + ry), (cx, bot + ry), (cx + rx, bot), (cx + rx, top)], dark)
        ellipse(cx, top, rx, ry, 235)

    band(32, 74, 80, 30, 6, 205, 120)   # the lower tier
    band(32, 68, 73, 23, 5, 220, 135)   # the upper tier
    # the outer spires (short), then the inner ones, the central needle last (in front)
    for cx, top, half in ((12, 40, 4.0), (52, 40, 4.0)):
        poly([(cx - half - 1, 70), (cx + half + 1, 70), (cx + half, 60), (cx - half, 60)], 170)
        spire(cx, 61, top, half, 225, 125)
    for cx, top, half in ((21, 22, 5.0), (43, 22, 5.0)):
        poly([(cx - half - 1.5, 70), (cx + half + 1.5, 70), (cx + half + 0.5, 50), (cx - half - 0.5, 50)], 185)
        spire(cx, 51, top, half, 235, 130)
    # the nave between them and the central needle
    poly([(24, 70), (40, 70), (39, 44), (25, 44)], 200)
    poly([(32, 70), (40, 70), (39, 44), (32, 44)], 140)
    spire(32, 45, 2, 7.5, 250, 135)
    # a ring round the needle's waist (the "circuit")
    box = [(32 - 11) * S, (40 - 3) * S, (32 + 11) * S, (40 + 3) * S]
    dh.ellipse(box, outline=255, width=int(1.6 * S))
    ds.ellipse(box, outline=255, width=int(1.6 * S))

    # details in a darker grey: arched doors on the nave and the tiers, ribs on the spires
    def line(pts, grey, w=1.0):
        dh.line([(x * S, y * S) for x, y in pts], fill=grey, width=max(1, int(w * S)))

    for cx in (28.5, 35.5):
        dh.rectangle([(cx - 1.6) * S, 58 * S, (cx + 1.6) * S, 69 * S], fill=60)
        dh.ellipse([(cx - 1.6) * S, 56.6 * S, (cx + 1.6) * S, 59.8 * S], fill=60)
    for x in range(14, 51, 6):
        line([(x, 76.5), (x, 79.5)], 90, 0.9)
    for cx, top in ((21, 22), (43, 22), (32, 2)):
        line([(cx, top + 6), (cx, top + 40)], 110, 0.6)

    # antialias down to the final size, alpha from the coverage
    a = shape.resize((64, 88), Image.LANCZOS)
    g = shade.resize((64, 88), Image.LANCZOS)
    ga = np.asarray(g, dtype=np.float32)
    aa = np.asarray(a, dtype=np.float32)
    # unpremultiply the grey (edges were averaged with black)
    grey = np.where(aa > 0, np.clip(ga * 255.0 / np.maximum(aa, 1), 0, 255), 0)
    rgba = np.dstack([grey, grey, grey, aa]).astype(np.uint8)
    os.makedirs(os.path.dirname(SPRITE), exist_ok=True)
    Image.fromarray(rgba, "RGBA").save(SPRITE)
    print("wrote", SPRITE)


# ------------------------------------------------------------------ 2. Revenance Circuit, our own pixel art
ART_W, ART_H = 268, 113
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float32) / 16.0 - 0.47

PALETTE = [  # the Retrowave ramps: night blues, violet, cyan, white, ember orange / red
    (4, 6, 16), (10, 14, 34), (18, 24, 60), (28, 36, 92), (42, 56, 130), (60, 84, 170), (86, 120, 210),
    (120, 170, 240), (170, 220, 255), (230, 248, 255), (255, 255, 255),
    (30, 14, 48), (58, 26, 86), (96, 44, 130), (140, 70, 170),
    (16, 60, 80), (24, 110, 140), (40, 170, 200), (90, 220, 240),
    (60, 18, 12), (120, 36, 16), (190, 66, 20), (240, 120, 30), (255, 180, 70), (255, 226, 150),
]
PAL = np.array(PALETTE, dtype=np.float32)


def revenance():
    W, H = ART_W, ART_H
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    rng = np.random.RandomState(1902)
    img = np.zeros((H, W, 3), np.float32)
    # the sky: deep blue above, violet in the middle, ember glow along the bottom
    t = yy / H
    img += np.dstack([4 + 16 * t, 6 + 10 * t, 22 + 30 * (1 - abs(t - 0.4))])
    neb = np.zeros((H, W), np.float32)
    for _ in range(14):  # soft nebula blobs
        cx, cy, r = rng.uniform(0, W), rng.uniform(0, H * 0.7), rng.uniform(18, 60)
        neb += np.exp(-((xx - cx) ** 2 + (yy - cy) ** 2) / (2 * r * r)) * rng.uniform(0.3, 0.9)
    img += np.dstack([neb * 22, neb * 12, neb * 40])
    ember = np.exp(-((yy - H) ** 2) / (2 * (H * 0.22) ** 2)) * np.exp(-((xx - W * 0.5) ** 2) / (2 * (W * 0.3) ** 2))
    img += np.dstack([ember * 190, ember * 60, ember * 14])
    # the far rain of light: thin vertical streaks from the top
    for _ in range(16):
        x0 = rng.randint(4, W - 4)
        ln = rng.uniform(0.15, 0.55) * H
        a = rng.uniform(0.15, 0.45)
        col = np.array([120, 170, 240]) * a
        fall = np.clip(1 - yy[:, x0] / ln, 0, 1)
        img[:, x0] += np.outer(fall, col)
    # the rings: dimensional gates stacked into the distance (ellipses, bright near the beam)
    cx = W * 0.5
    for k in range(9):
        cy = H * (0.30 + 0.065 * k)
        rx = 26 + k * 15.0
        ry = 3.0 + k * 1.6
        d = np.abs(np.sqrt(((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2) - 1.0) * min(rx, ry * 3)
        ring = np.exp(-d * d / 0.45) * (0.35 + 0.45 * np.exp(-((xx - cx) / (rx * 0.6)) ** 2))
        warm = k / 8.0  # the lower rings take the fire's colour
        col = np.array([90 + 160 * warm, 180 - 60 * warm, 255 - 200 * warm])
        img += ring[..., None] * col[None, None, :] * 0.75
    # the beam: thin and cyan above, a white core at the heart, fire below
    core_y = H * 0.52
    width = np.where(yy < core_y, 0.7 + (yy / core_y) * 1.3, 2.0 + (yy - core_y) / (H - core_y) * 7.0)
    beam = np.exp(-((xx - cx) ** 2) / (2 * width ** 2))
    above = yy < core_y
    img += np.where(above[..., None], beam[..., None] * np.array([130, 210, 255]) * 0.85, 0)
    fire = beam * np.clip((yy - core_y) / (H - core_y) * 1.4, 0, 1.2)
    img += np.where(~above[..., None], fire[..., None] * np.array([255, 120, 30]) + beam[..., None] * np.array([255, 220, 140]) * 0.6, 0)
    heart = np.exp(-((xx - cx) ** 2 + ((yy - core_y) * 1.8) ** 2) / (2 * 4.0 ** 2))
    img += heart[..., None] * np.array([255, 255, 255]) * 1.1
    halo = np.exp(-((xx - cx) ** 2 + ((yy - core_y) * 2.4) ** 2) / (2 * 16.0 ** 2))
    img += halo[..., None] * np.array([60, 140, 200]) * 0.45
    # sparks rising from the fire
    for _ in range(60):
        sx = cx + rng.normal(0, 18)
        sy = rng.uniform(core_y, H)
        if 0 <= sx < W and 0 <= sy < H:
            img[int(sy), int(sx)] += np.array([255, 170, 60]) * rng.uniform(0.5, 1.0)
    # stars
    for _ in range(120):
        sx, sy = rng.randint(0, W), rng.randint(0, int(H * 0.6))
        img[sy, sx] += rng.uniform(60, 200)
    img = np.clip(img, 0, 255)
    # to the palette with ordered dithering
    bay = np.tile(BAYER, (H // 4 + 1, W // 4 + 1))[:H, :W]
    dith = np.clip(img + bay[..., None] * 26, 0, 255)
    d2 = ((dith[:, :, None, :] - PAL[None, None, :, :]) ** 2).sum(-1)
    out = PAL[np.argmin(d2, axis=2)].astype(np.uint8)
    os.makedirs(OUT, exist_ok=True)
    Image.fromarray(out, "RGB").save(os.path.join(OUT, "revenance_circuit.png"))
    Image.fromarray(out, "RGB").resize((W * 3, H * 3), Image.NEAREST).save(os.path.join(OUT, "revenance_circuit_x3.png"))
    print("wrote", os.path.join(OUT, "revenance_circuit.png"))


if __name__ == "__main__":
    sprite()
    revenance()
