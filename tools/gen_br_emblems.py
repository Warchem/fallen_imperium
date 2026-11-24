#!/usr/bin/env python3
"""Blue Reverie faction emblems (0.83.0), drawn by the game's own tools - shapes, not a copy of a picture.

The emblem's layout follows the faction's symbol (the Atokera: a ring around a figure - head, neck, a flared stem - cradled by
an open inner ring); the look follows our icons: each part filled with a metal gradient and edged in a lighter line.
Rendered 4x and shrunk (smooth edges), saved as assets/icons/factions_br/<key>.png (256 px, transparent); `make icons`
packs them as faction/<key>.

Usage: python3 tools/gen_br_emblems.py [OUTDIR]
"""
import math
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

S = 1024  # drawing size (4x)
OUT_PX = 256
YY, XX = np.mgrid[0:S, 0:S].astype(np.float32)


def disc(cx, cy, r):
    return (XX - cx) ** 2 + (YY - cy) ** 2 <= r * r


def ring(cx, cy, r0, r1):
    d = (XX - cx) ** 2 + (YY - cy) ** 2
    return (d <= r1 * r1) & (d >= r0 * r0)


def angle(cx, cy):  # 0 = straight up, clockwise, degrees
    return (np.degrees(np.arctan2(XX - cx, -(YY - cy))) + 360.0) % 360.0


def poly(pts):
    from matplotlib.path import Path  # (only for the polygon fill)
    p = Path(pts)
    inside = p.contains_points(np.stack([XX.ravel() + 0.5, YY.ravel() + 0.5], axis=1))
    return inside.reshape(S, S)


def atokera():
    c = S / 2
    k = S / 380.0  # the layout in a 380 box
    P = lambda v: v * k
    parts = []
    parts.append(ring(c, c, P(146), P(174)))  # the outer ring
    inner = ring(c, c, P(95), P(121))
    a = angle(c, c)
    inner &= ~((a > 140) & (a < 220))  # open at the bottom (the stem)
    inner &= ~((a > 336) | (a < 24))   # ... and at the top (the head's cradle)
    parts.append(inner)
    hx, hy = c, P(104)
    cradle = ring(hx, hy, P(41), P(58)) & (YY > hy - P(12))  # cradles the head, joined to the inner ring's ends
    parts[-1] = (parts[-1] & (YY > hy - P(12))) | cradle  # (one piece with the inner ring, its tips cut level with the cradle's)
    parts.append(disc(hx, hy, P(33)))      # the head
    parts.append(disc(c, P(184), P(27)))   # the neck
    stem = poly([(c - P(15), P(206)), (c + P(15), P(206)), (c + P(20), P(300)), (c + P(34), P(352)), (c - P(34), P(352)),
                 (c - P(20), P(300))])
    stem &= disc(c, c, P(160))
    parts.append(stem)
    return parts


def belkosea():
    """the Belkosea: a heavy ring, a blue disc of circuit traces inside it, a long needle crossing it corner to corner, a dark
    four-pointed star cut at the crossing - (mask, fill top, fill bottom, edge) per part, drawn in order"""
    c = S / 2
    k = S / 380.0
    P = lambda v: v * k
    a = angle(c, c)
    disc_in = disc(c, c, P(118))
    # circuit traces on the disc: a few broken arcs and spokes
    rng = np.random.RandomState(4)
    traces = np.zeros((S, S), bool)
    for r0 in (P(52), P(78), P(100)):
        a0 = rng.uniform(0, 360)
        span = rng.uniform(70, 140)
        traces |= ring(c, c, r0 - P(2.5), r0 + P(2.5)) & (((a - a0) % 360) < span)
    for ang in rng.uniform(0, 360, 7):
        t = math.radians(ang)
        r1, r2 = P(rng.uniform(40, 60)), P(rng.uniform(80, 112))
        x1, y1, x2, y2 = c + r1 * math.sin(t), c - r1 * math.cos(t), c + r2 * math.sin(t), c - r2 * math.cos(t)
        d = np.abs((XX - x1) * (y2 - y1) - (YY - y1) * (x2 - x1)) / max(math.hypot(x2 - x1, y2 - y1), 1)
        along = ((XX - x1) * (x2 - x1) + (YY - y1) * (y2 - y1)) / max((x2 - x1) ** 2 + (y2 - y1) ** 2, 1)
        traces |= (d < P(2.5)) & (along > 0) & (along < 1)
    traces &= disc_in
    ring_m = ring(c, c, P(116), P(152))
    # the needle: a long thin diamond from the lower left to the upper right, past the ring
    t = math.radians(45)
    ux, uy = math.cos(t), -math.sin(t)
    lx, ly = (XX - c) * ux + (YY - c) * uy, -(XX - c) * uy + (YY - c) * ux
    needle = (np.abs(ly) < P(16) * (1 - np.abs(lx) / P(205))) & (np.abs(lx) < P(205))
    # the star at the crossing: dark, four points
    star = (np.abs(lx) / P(58) + np.abs(ly) / P(22) < 1) | (np.abs(lx) / P(22) + np.abs(ly) / P(58) < 1)
    silver = ((236, 240, 246), (120, 128, 140), (60, 66, 78))
    return [(disc_in & ~star, (70, 130, 230), (18, 40, 120), (120, 170, 250)),
            (traces & ~star, (170, 215, 255), (90, 150, 240), (170, 215, 255)),
            (ring_m, *silver),
            (needle & ~star, *silver),
            (star & disc_in, (10, 16, 34), (4, 8, 20), (40, 70, 130))]


def pharadn():
    """the Pharad'n Order (0.85.0): a gold diamond with its side points cut flat, teal slats in the cuts, a dark band round a gold
    ring and an obsidian eye in the middle, a short spike below - (mask, fill top, fill bottom, edge) per part, drawn in order"""
    c = S / 2
    k = S / 380.0
    P = lambda v: v * k
    # the diamond: |x|/w + |y|/h <= 1, its left and right points cut by vertical flats
    w, h, cy = P(186), P(170), c - P(6)
    dia = (np.abs(XX - c) / w + np.abs(YY - cy) / h <= 1) & (np.abs(XX - c) < P(160))
    # the slats: three thin bars in each cut, a little in from the flat
    slats = np.zeros((S, S), bool)
    for side in (-1, 1):
        for i in range(3):
            x0 = c + side * (P(124) + P(10) * i)
            slats |= (np.abs(XX - x0) < P(3.2)) & (np.abs(YY - cy) < P(30) - P(7) * i)
    slats &= dia
    spike = poly([(c - P(26), cy + P(150)), (c + P(26), cy + P(150)), (c, cy + P(196))])
    band = disc(c, cy, P(104))
    gold_ring = ring(c, cy, P(62), P(84))
    eye = disc(c, cy, P(56))
    glint = disc(c - P(16), cy - P(18), P(12)) & eye
    gold = ((250, 214, 120), (170, 118, 30), (255, 236, 170))
    return [(dia & ~band, *gold),
            (spike, *gold),
            (slats, (90, 200, 200), (30, 110, 120), (150, 235, 230)),
            (band & ~gold_ring & ~eye, (34, 26, 10), (10, 8, 4), (90, 70, 24)),
            (gold_ring, *gold),
            (eye & ~glint, (36, 38, 46), (6, 6, 10), (70, 64, 40)),
            (glint, (120, 124, 140), (70, 74, 88), (120, 124, 140))]


def render_parts(parts, edge_px=9):
    """parts drawn in order, each with its own gradient fill and edge (the later on top)"""
    img = np.zeros((S, S, 4), np.float32)
    grad = (YY / S)[..., None]
    sheen = np.clip(1.15 - 0.35 * ((XX + YY) / S), 0.8, 1.15)[..., None]
    rng = np.random.RandomState(7)
    grain = ndimage.gaussian_filter(rng.rand(S, S).astype(np.float32), 3.0)
    grain = (grain - grain.mean()) / (grain.std() + 1e-6)
    for m, top, bottom, edge in parts:
        fill = np.array(top, np.float32) * (1 - grad) + np.array(bottom, np.float32) * grad
        fill = np.clip(fill * sheen * (1 + 0.05 * grain[..., None]), 0, 255)
        inner = ndimage.binary_erosion(m, iterations=edge_px)
        e = m & ~inner
        img[inner, :3] = fill[inner]
        img[inner, 3] = 255
        img[e, :3] = edge
        img[e, 3] = 255
    allm = img[..., 3] > 0
    rim = ndimage.binary_dilation(allm, iterations=6) & ~allm
    img[rim, :3] = (14, 18, 30)
    img[rim, 3] = 230
    return Image.fromarray(img.astype(np.uint8), "RGBA").resize((OUT_PX, OUT_PX), Image.LANCZOS)


def render(parts, fill_top, fill_bottom, edge, edge_px=9):
    img = np.zeros((S, S, 4), np.float32)
    grad = (YY / S)[..., None]
    fill = np.array(fill_top, np.float32) * (1 - grad) + np.array(fill_bottom, np.float32) * grad
    # a soft sheen: lighter toward the upper left
    sheen = np.clip(1.15 - 0.35 * ((XX + YY) / S), 0.8, 1.15)[..., None]
    fill = np.clip(fill * sheen, 0, 255)
    rng = np.random.RandomState(7)  # (a faint metal grain)
    grain = ndimage.gaussian_filter(rng.rand(S, S).astype(np.float32), 3.0)
    grain = (grain - grain.mean()) / (grain.std() + 1e-6)
    fill = np.clip(fill * (1 + 0.05 * grain[..., None]), 0, 255)
    for m in parts:
        inner = ndimage.binary_erosion(m, iterations=edge_px)
        e = m & ~inner
        img[inner, :3] = fill[inner]
        img[inner, 3] = 255
        img[e, :3] = edge
        img[e, 3] = 255
    # a dark rim round the whole emblem (reads on light and dark backgrounds)
    allm = img[..., 3] > 0
    rim = ndimage.binary_dilation(allm, iterations=6) & ~allm
    img[rim, :3] = (18, 26, 18)
    img[rim, 3] = 230
    out = Image.fromarray(img.astype(np.uint8), "RGBA").resize((OUT_PX, OUT_PX), Image.LANCZOS)
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                                             "assets/icons/factions_br")
    os.makedirs(out, exist_ok=True)
    render(atokera(), (120, 200, 110), (30, 105, 50), (205, 140, 80)).save(os.path.join(out, "atokera.png"))
    render_parts(belkosea(), edge_px=7).save(os.path.join(out, "belkosea.png"))  # (0.84.0)
    render_parts(pharadn(), edge_px=6).save(os.path.join(out, "pharadn.png"))  # (0.85.0)
    print("wrote", out)


if __name__ == "__main__":
    main()
