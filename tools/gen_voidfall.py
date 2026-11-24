#!/usr/bin/env python3
"""The Voidfall tileset's art (0.32.0, owner: "use the Voidfall art for the aesthetic and palette ... not
pixelated, high definition ... animated").

The look, after the owner's Voidfall pictures: deep violet and indigo space, painted nebulae in magenta,
hot pink and orange with teal in the cold corners, the faint diagonal print lines of the box art, planets
lit warm from one side with a magenta rim on the other. The game builds every tile at the screen's own
resolution (src/ui/voidfall.cpp) from:

  * backgrounds: 8 nebula skies for ordinary systems and one for each special tile (anomalies, Mecatol
    Rex, Mallice, the Creuss gate, The Dark, the Vuil'raith home, hyperlanes) - the hex's bounding box,
    2 : sqrt(3);
  * moving layers drawn over them: tileable veils that drift (every sky), and pictures that turn round
    the middle (the rift's spiral, a supernova's corona, Mallice's warp, the Creuss gate, Mecatol's halo)
    or slide past (asteroids);
  * a map of every planet's surface (equirectangular), with night lights / glowing cracks and clouds
    where it has them, turned on its sphere by the game.

Two packs: voidfall-pc.dat (skies 512 wide, planets 256 x 128) and voidfall-miyoo.dat (half size).
A pack is "VFD1", a table of contents and the files (JPEG / PNG) one after another; index.txt in it holds
each picture's settings for the game.

usage: gen_voidfall.py DATA.json OUT_DIR [PREVIEW_DIR]     (DATA.json = `ti4 --dumpdata`; `make voidfall`)
"""
import io
import json
import math
import os
import struct
import sys
import zlib

import numpy as np
from PIL import Image
from scipy import ndimage as ndi

TAU = 2 * math.pi
BW, BH = 512, 444          # a sky: the hex's box (corner radius 256)
PW, PH = 256, 128          # a planet's map
VS = 256                   # a veil (tileable)


def hexc(s):
    s = s.lstrip("#")
    return np.array([int(s[i:i + 2], 16) for i in (0, 2, 4)], float) / 255.0


def ramp(stops):
    """stops: [(t, '#rrggbb'), ...] -> f(v) for arrays of v in 0..1"""
    ts = np.array([t for t, _ in stops])
    cs = np.stack([hexc(c) for _, c in stops])

    def f(v):
        v = np.clip(v, 0, 1)
        return np.stack([np.interp(v, ts, cs[:, k]) for k in range(3)], -1)
    return f


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


# ---------------------------------------------------------------- noise
def tnoise(rng, size, base):
    g = rng.random((base, base))
    z = ndi.zoom(g, size / base, order=3, mode="grid-wrap", grid_mode=True)
    return z[:size, :size]


def fbm(seed, size=512, base=4, octaves=7, gain=0.55):
    """tileable fractal noise, 0..1"""
    rng = np.random.default_rng(seed)
    out = np.zeros((size, size))
    amp, tot, b = 1.0, 0.0, base
    for _ in range(octaves):
        if b > size // 2:
            break
        out += amp * tnoise(rng, size, b)
        tot += amp
        amp *= gain
        b *= 2
    out /= tot
    lo, hi = np.percentile(out, 1), np.percentile(out, 99)
    return np.clip((out - lo) / (hi - lo + 1e-9), 0, 1)


def warp(field, dx, dy, amt):
    n = field.shape[0]
    yy, xx = np.mgrid[0:n, 0:n].astype(float)
    return ndi.map_coordinates(field, [yy + (dy - 0.5) * amt, xx + (dx - 0.5) * amt], order=1, mode="grid-wrap")


def nebula_field(seed, size=512, warp_amt=90.0, base=3):
    a = fbm(seed, size, base=base)
    dx = fbm(seed + 1, size, base=2, octaves=5)
    dy = fbm(seed + 2, size, base=2, octaves=5)
    w = warp(a, dx, dy, warp_amt)
    lo, hi = np.percentile(w, 1), np.percentile(w, 99)
    return np.clip((w - lo) / (hi - lo + 1e-9), 0, 1)


def crop(a, w=BW, h=BH):
    return a[:h, :w]


def grid(w=BW, h=BH):
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    return xx - w / 2 + 0.5, yy - h / 2 + 0.5   # from the centre, in sky pixels (corner radius 256)


def stars(img, seed, density=0.0016, warm=0.3, big=0.04):
    h, w, _ = img.shape
    rng = np.random.default_rng(seed)
    n = int(w * h * density)
    xs, ys = rng.uniform(0, w, n), rng.uniform(0, h, n)
    br = rng.random(n) ** 3
    cols = np.where(rng.random(n)[:, None] < warm, hexc("ffd9b8"), hexc("dcd6ff"))
    for x, y, b, c in zip(xs, ys, br, cols):
        r = 0.6 + 1.6 * b
        x0, x1 = int(max(0, x - 4 * r)), int(min(w, x + 4 * r + 1))
        y0, y1 = int(max(0, y - 4 * r)), int(min(h, y + 4 * r + 1))
        if x0 >= x1 or y0 >= y1:
            continue
        yy, xx = np.mgrid[y0:y1, x0:x1].astype(float)
        d = np.hypot(xx + 0.5 - x, yy + 0.5 - y)
        g = np.exp(-(d / (0.55 * r)) ** 2) * (0.35 + 0.65 * b)
        if b > 1 - big:  # a few with a cross flare
            g = g + 0.5 * b * (np.exp(-np.abs(xx + 0.5 - x) / 0.6) * np.exp(-np.abs(yy + 0.5 - y) / (3.5 * r)) +
                               np.exp(-np.abs(yy + 0.5 - y) / 0.6) * np.exp(-np.abs(xx + 0.5 - x) / (3.5 * r)))
        img[y0:y1, x0:x1] = 1 - (1 - img[y0:y1, x0:x1]) * (1 - np.clip(g, 0, 1)[..., None] * c)
    return img


def hatch(img, amt=0.045, period=7.0):
    """the box art's diagonal print lines"""
    h, w, _ = img.shape
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    s = 0.5 + 0.5 * np.cos((xx + yy) * TAU / period)
    return img * (1 - amt * smooth(0.55, 0.95, s))[..., None]


def screen(a, b):
    return 1 - (1 - a) * (1 - b)


SPACE = ramp([(0.0, "07040f"), (0.35, "0e0822"), (0.7, "1a0f3c"), (1.0, "26145a")])


def base_space(seed, tint="1a0f3c"):
    v = crop(fbm(seed, base=2, octaves=5))
    img = SPACE(v * 0.85)
    img = img * 0.8 + hexc(tint) * 0.2 * v[..., None]
    return img


def paint_cloud(img, seed, rmp, cut=0.45, soft=0.35, amt=1.0, warp_amt=90.0, mask=None, base=3):
    d = crop(nebula_field(seed, warp_amt=warp_amt, base=base))
    a = smooth(cut, cut + soft, d) * amt
    if mask is not None:
        a = a * mask
    col = rmp(np.clip((d - cut) / (1 - cut), 0, 1))
    return img * (1 - a[..., None]) + col * a[..., None], d


# palettes after the box art
MAGENTA = ramp([(0, "2a0f4f"), (0.35, "6d1f86"), (0.6, "c0308f"), (0.82, "ff5aa0"), (1, "ffc4c0")])
VIOLET = ramp([(0, "1d1050"), (0.4, "3b1f88"), (0.7, "7046c8"), (1, "c9b6ff")])
TEAL = ramp([(0, "0b1f3f"), (0.4, "135a7a"), (0.75, "2ab3c4"), (1, "b8f4f0")])
EMBER = ramp([(0, "3a0f2a"), (0.35, "8a2240"), (0.65, "e0553a"), (0.85, "ff9a4a"), (1, "ffe0a8")])
CRIMSON = ramp([(0, "1a0408"), (0.35, "5a0c14"), (0.65, "b3141e"), (0.85, "ff3b2a"), (1, "ffb0a0")])
GOLD = ramp([(0, "2a1408"), (0.4, "7a4416"), (0.7, "d99a3a"), (1, "fff0c0")])
ICE = ramp([(0, "0b1430"), (0.4, "2a4f9a"), (0.75, "79c3f0"), (1, "f0fbff")])

SKY_STYLES = [  # (main cloud ramp, accent ramp, accent amount, veil tint)
    (MAGENTA, TEAL, 0.55, "ff4fa8"),
    (VIOLET, EMBER, 0.5, "b07cff"),
    (MAGENTA, EMBER, 0.6, "ff7a5a"),
    (TEAL, MAGENTA, 0.5, "4fd8e0"),
    (VIOLET, MAGENTA, 0.6, "d06cff"),
    (EMBER, VIOLET, 0.45, "ff9a4a"),
    (MAGENTA, VIOLET, 0.5, "ff6ad0"),
    (TEAL, EMBER, 0.45, "6ae0d0"),
]


def sky(k):
    main, acc, aamt, _ = SKY_STYLES[k]
    s = 7000 + 37 * k
    img = base_space(s)
    img, d = paint_cloud(img, s + 1, main, cut=0.42, soft=0.4, amt=0.95)
    m = crop(fbm(s + 3, base=2, octaves=4))
    img, _ = paint_cloud(img, s + 2, acc, cut=0.5, soft=0.35, amt=aamt, mask=smooth(0.45, 0.75, m), base=4)
    # dark dust lanes through the bright parts
    lane = crop(nebula_field(s + 4, warp_amt=60, base=4))
    img = img * (1 - 0.55 * smooth(0.62, 0.85, lane))[..., None]
    img = stars(img, s + 5, density=0.0014)
    return hatch(img)


def polar(cx=0.0, cy=0.0):
    x, y = grid()
    return np.hypot(x - cx, y - cy), np.arctan2(y - cy, x - cx)


def sky_rift():
    img = base_space(4101) * 0.7
    r, a = polar()
    n = crop(fbm(4102, base=4))
    # the accretion: a ring of hot gas, pinker outside, twisted by the spiral
    ring = np.exp(-((r - 120 - 30 * (n - 0.5)) / 55) ** 2)
    spiral = 0.5 + 0.5 * np.cos(a * 3 + np.log(r + 8) * 6)
    heat = np.clip(ring * (0.55 + 0.45 * spiral), 0, 1)
    img = screen(img, EMBER(heat) * heat[..., None] * 0.95)
    img = screen(img, MAGENTA(np.clip(r / 260, 0, 1)) * (np.exp(-((r - 200) / 70) ** 2) * 0.5)[..., None])
    img = stars(img, 4103, density=0.001)
    # the hole: black, with a thin bright edge (photon ring)
    hole = smooth(62, 50, r)
    img = img * (1 - hole[..., None])
    edge = np.exp(-((r - 60) / 4.5) ** 2)
    img = screen(img, hexc("ffd8b0") * edge[..., None] * 0.9)
    return hatch(img)


def flow_rift():
    """spiral arms of glowing gas round the middle (turned by the game); additive"""
    S = 512
    yy, xx = np.mgrid[0:S, 0:S].astype(float)
    x, y = xx - S / 2 + 0.5, yy - S / 2 + 0.5
    r, a = np.hypot(x, y), np.arctan2(y, x)
    n = fbm(4111, S, base=6)
    arms = np.clip(np.cos(a * 2 - np.log(r + 10) * 7 + n * 2.5) * 0.5 + 0.5, 0, 1) ** 3
    fade = smooth(60, 90, r) * smooth(255, 150, r)
    v = arms * fade * (0.5 + 0.5 * n)
    col = EMBER(0.35 + 0.6 * v) * v[..., None] * 1.1
    col = screen(col, MAGENTA(0.8 * v) * (v * 0.4)[..., None])
    return np.clip(col, 0, 1)


def sky_supernova(hot="ffb060"):
    img = base_space(4301) * 0.75
    r, a = polar()
    n = crop(fbm(4302, base=5))
    core = np.exp(-(r / 70) ** 2)
    halo = np.exp(-(r / 170) ** 1.5)
    img = screen(img, EMBER(np.clip(halo * 0.9 + 0.1 * n, 0, 1)) * (halo * 0.95)[..., None])
    img = screen(img, MAGENTA(np.clip(r / 300, 0, 1)) * (np.exp(-((r - 210) / 60) ** 2) * 0.45 * n)[..., None])
    img = screen(img, hexc(hot) * core[..., None])
    img = screen(img, hexc("fff6e0") * np.exp(-(r / 30) ** 2)[..., None])
    img = stars(img, 4303, density=0.0008)
    return hatch(img, 0.03)


def flow_corona():
    """flames round a star: rays that turn and flicker (additive, centred)"""
    S = 512
    yy, xx = np.mgrid[0:S, 0:S].astype(float)
    x, y = xx - S / 2 + 0.5, yy - S / 2 + 0.5
    r, a = np.hypot(x, y), np.arctan2(y, x)
    n = fbm(4311, S, base=8)
    rays = (0.5 + 0.5 * np.cos(a * 9 + n * 4)) ** 4 + 0.6 * (0.5 + 0.5 * np.cos(a * 17 - n * 3)) ** 6
    v = rays * smooth(40, 80, r) * np.exp(-((r - 80) / 95) ** 2)
    v = np.clip(v, 0, 1)
    return np.clip(EMBER(0.45 + 0.55 * v) * v[..., None], 0, 1)


def sky_nebula():
    img = base_space(4201)
    img, _ = paint_cloud(img, 4202, MAGENTA, cut=0.25, soft=0.45, amt=1.0)
    img, _ = paint_cloud(img, 4203, VIOLET, cut=0.45, soft=0.4, amt=0.6)
    img, _ = paint_cloud(img, 4204, TEAL, cut=0.6, soft=0.3, amt=0.45)
    img = stars(img, 4205, density=0.0006)
    return hatch(img)


def rocks_layer(seed, n=34, smin=6, smax=26, size=512, warm=True):
    """tileable asteroids with a warm key light and a magenta rim; RGBA"""
    rng = np.random.default_rng(seed)
    col = np.zeros((size, size, 3))
    alpha = np.zeros((size, size))
    for _ in range(n):
        cx, cy = rng.uniform(0, size, 2)
        s = smin + (smax - smin) * rng.random() ** 2.2
        k = rng.integers(5, 9)
        ph = rng.uniform(0, TAU, 4)
        for ox in (-size, 0, size):
            for oy in (-size, 0, size):
                x0, x1 = int(max(0, cx + ox - 1.6 * s)), int(min(size, cx + ox + 1.6 * s + 1))
                y0, y1 = int(max(0, cy + oy - 1.6 * s)), int(min(size, cy + oy + 1.6 * s + 1))
                if x0 >= x1 or y0 >= y1:
                    continue
                yy, xx = np.mgrid[y0:y1, x0:x1].astype(float)
                dx, dy = xx + 0.5 - cx - ox, yy + 0.5 - cy - oy
                rr, aa = np.hypot(dx, dy), np.arctan2(dy, dx)
                edge = s * (1 + 0.18 * np.cos(aa * 2 + ph[0]) + 0.1 * np.cos(aa * k + ph[1]) + 0.06 * np.cos(aa * 11 + ph[2]))
                inside = smooth(edge + 1.0, edge - 1.0, rr)
                if inside.max() <= 0:
                    continue
                # a lumpy sphere: light from the upper left, a magenta rim lower right
                nz = np.sqrt(np.clip(1 - (rr / (edge + 1e-6)) ** 2, 0, 1))
                lx, ly = -dx / (edge + 1e-6), -dy / (edge + 1e-6)
                lam = np.clip(0.55 * lx + 0.45 * ly + 0.5 * nz, 0, 1)
                rim = np.clip(-0.6 * lx - 0.5 * ly, 0, 1) * (1 - nz) ** 1.5
                tex = 0.75 + 0.25 * np.cos(dx * 0.9 + ph[3]) * np.cos(dy * 0.8 - ph[3])
                base = hexc("3a2a4a") * 0.6 + hexc("6a5070") * 0.4
                c = base * (0.25 + 0.95 * lam * tex)[..., None]
                if warm:
                    c = c + hexc("ff9a4a") * (lam ** 3 * 0.45)[..., None]
                c = c + hexc("ff4fa8") * (rim * 0.9)[..., None]
                a = inside
                col[y0:y1, x0:x1] = col[y0:y1, x0:x1] * (1 - a[..., None]) + np.clip(c, 0, 1) * a[..., None]
                alpha[y0:y1, x0:x1] = np.maximum(alpha[y0:y1, x0:x1], a)
    return np.concatenate([col, alpha[..., None]], -1)


def sky_asteroids(seed, alpha_ring=False):
    img = base_space(seed)
    img, _ = paint_cloud(img, seed + 1, VIOLET, cut=0.5, soft=0.4, amt=0.6)
    dust = crop(nebula_field(seed + 2, warp_amt=50, base=5))
    img = screen(img, EMBER(dust * 0.6) * (smooth(0.55, 0.9, dust) * 0.35)[..., None])
    img = stars(img, seed + 3, density=0.001)
    # a few big rocks that stay put (the moving ones are the layer over them)
    r = rocks_layer(seed + 4, n=10, smin=10, smax=30)
    r = crop(r)
    img = img * (1 - r[..., 3:]) + r[..., :3] * r[..., 3:]
    return hatch(img)


def sky_nova_seed():
    """0.43.0 (owner D44): the Muaat's Nova Seed - a hotter star with a crown of flares round it"""
    img = sky_supernova("ffc070")
    r, a = polar()
    n = crop(fbm(8101, base=8))
    crown = np.exp(-((r - 150) / 26) ** 2) * (0.55 + 0.45 * np.cos(a * 12 + n * 2.5)) ** 3
    img = screen(img, EMBER(np.clip(crown, 0, 1)) * (crown * 0.9)[..., None])
    return hatch(img, 0.02)


def sky_zelian():
    """0.43.0 (owner D44): the Zelian asteroid field - a cracked world's ember heart, its rubble in a ring round it"""
    img = base_space(3601, tint="140c26")
    img, _ = paint_cloud(img, 3602, VIOLET, cut=0.55, soft=0.4, amt=0.45)
    r, a = polar()
    n = crop(fbm(3603, base=6))
    img = screen(img, EMBER(np.clip(np.exp(-(r / 60) ** 1.6) + 0.15 * n, 0, 1)) * (np.exp(-(r / 75) ** 1.4) * 0.9)[..., None])
    img = screen(img, hexc("ffe0a0") * np.exp(-(r / 16) ** 2)[..., None])
    img = stars(img, 3604, density=0.0008)
    ring = crop(rocks_layer(3605, n=90, smin=8, smax=20))
    keep = (smooth(45, 75, r) * (1 - smooth(215, 250, r)))[..., None]
    al = ring[..., 3:] * keep
    img = img * (1 - al) + ring[..., :3] * al
    return hatch(img)


def with_rocks(img, seed, n=9, smin=8, smax=24, keep_r=None):
    """big rocks that stay put, painted over a sky (keep_r: none closer than this to the middle)"""
    r = crop(rocks_layer(seed, n=n, smin=smin, smax=smax))
    a = r[..., 3:]
    if keep_r is not None:
        rr, _ = polar()
        a = a * smooth(keep_r - 10, keep_r + 25, rr)[..., None]
    return img * (1 - a) + r[..., :3] * a


def sky_asteroid_rift():
    """Uncharted Space's Asteroid Rift (d121, 0.33.1): the rift, with rocks round it being pulled in"""
    return with_rocks(sky_rift(), 12101, n=12, smin=7, smax=22, keep_r=95)


def sky_asteroid_nebula():
    """Uncharted Space's Asteroid Nebula (d120, 0.33.1): the nebula's clouds with rocks in them"""
    return with_rocks(sky_nebula(), 12001, n=10, smin=9, smax=28)


def seg_dist(x, y, pts):
    """distance from every pixel to a polyline"""
    d = np.full(x.shape, 1e9)
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        vx, vy = x1 - x0, y1 - y0
        t = np.clip(((x - x0) * vx + (y - y0) * vy) / (vx * vx + vy * vy), 0, 1)
        d = np.minimum(d, np.hypot(x - x0 - t * vx, y - y0 - t * vy))
    return d


def sky_scar():
    """Thunder's Edge's entropic scar (114 / 116, 0.34.0): space torn open - a jagged crack across the
    tile, white-hot inside, bleeding crimson light, with fissures branching off and shards round it"""
    img = base_space(11401, tint="3c0f1a") * 0.75
    img, _ = paint_cloud(img, 11402, CRIMSON, cut=0.5, soft=0.4, amt=0.55)
    img = stars(img, 11403, density=0.0008)
    x, y = grid()
    rng = np.random.default_rng(11404)
    n = 15
    xs = np.linspace(-300, 300, n)
    ys = xs * 0.32 + rng.uniform(-26, 26, n) * np.where(np.arange(n) % 2, 1, -1)
    main = list(zip(xs, ys))
    d = seg_dist(x, y, main)
    branches = []
    for i in range(2, n - 2, 2):  # fissures
        a = rng.uniform(0, TAU)
        ln = rng.uniform(40, 110)
        bx, by = xs[i], ys[i]
        pts = [(bx, by)]
        for k in range(3):
            a += rng.uniform(-0.6, 0.6)
            bx, by = bx + np.cos(a) * ln / 3, by + np.sin(a) * ln / 3
            pts.append((bx, by))
        branches.append(seg_dist(x, y, pts))
    db = np.min(branches, axis=0)
    wob = crop(fbm(11405, base=6))
    glow = np.exp(-(d / (60 + 30 * wob)) ** 1.3)
    img = screen(img, CRIMSON(np.clip(glow * 0.9, 0, 1)) * (glow * 0.9)[..., None])
    rim = np.exp(-(d / 9.0) ** 2)
    img = screen(img, hexc("ff4a4a") * rim[..., None])
    core = np.exp(-(d / 3.2) ** 2)
    img = screen(img, hexc("fff0e8") * core[..., None])
    fiss = np.exp(-(db / 3.5) ** 2) * 0.8 + np.exp(-(db / 14) ** 2) * 0.35
    img = screen(img, hexc("ff6a5a") * fiss[..., None])
    # the darkness torn round the crack (space pulled apart on either side)
    img = img * (1 - 0.35 * np.exp(-((d - 30) / 18) ** 2))[..., None]
    img = with_rocks(img, 11406, n=10, smin=5, smax=14)
    return hatch(img, 0.035)


def sky_dark():
    img = SPACE(crop(fbm(5601, base=2)) * 0.35)
    x, y = grid()
    cur = crop(nebula_field(5602, warp_amt=140, base=2))
    band = np.exp(-((y + 40 * np.sin(x / 90.0) + 60 * (cur - 0.5)) / 110) ** 2)
    img = screen(img, VIOLET(cur * 0.7) * (band * smooth(0.4, 0.8, cur) * 0.55)[..., None])
    img = stars(img, 5603, density=0.0005)
    r, _ = polar()
    img = img * (0.55 + 0.45 * smooth(60, 220, r))[..., None]  # the void in the middle
    return hatch(img, 0.03)


def sky_creuss():
    img = base_space(1701)
    img, _ = paint_cloud(img, 1702, ICE, cut=0.5, soft=0.4, amt=0.55)
    img = stars(img, 1703, density=0.0012)
    r, _ = polar()
    img = screen(img, ICE(np.clip(1 - r / 260, 0, 1)) * (np.exp(-(r / 150) ** 2) * 0.5)[..., None])
    return hatch(img)


def flow_spiral(seed, rmp, arms=4, inner=40, outer=250, tight=5.0, amt=1.0):
    S = 512
    yy, xx = np.mgrid[0:S, 0:S].astype(float)
    x, y = xx - S / 2 + 0.5, yy - S / 2 + 0.5
    r, a = np.hypot(x, y), np.arctan2(y, x)
    n = fbm(seed, S, base=6)
    v = np.clip(np.cos(a * arms + np.log(r + 10) * tight + n * 2) * 0.5 + 0.5, 0, 1) ** 2.5
    v = v * smooth(inner * 0.6, inner * 1.4, r) * smooth(outer, outer * 0.55, r) * (0.55 + 0.45 * n) * amt
    return np.clip(rmp(0.3 + 0.7 * v) * v[..., None], 0, 1)


def sky_mallice(active):
    img = base_space(8201 if active else 8202) * (1.0 if active else 0.75)
    r, a = polar()
    if active:
        tunnel = np.exp(-(r / 180) ** 1.6)
        img = screen(img, MAGENTA(tunnel) * (tunnel * 0.8)[..., None])
        img = screen(img, TEAL(np.clip(1 - r / 200, 0, 1)) * (np.exp(-((r - 120) / 50) ** 2) * 0.45)[..., None])
        img = screen(img, hexc("fff0ff") * np.exp(-(r / 28) ** 2)[..., None])
    else:
        img = screen(img, VIOLET(np.clip(1 - r / 260, 0, 1)) * (np.exp(-(r / 160) ** 2) * 0.35)[..., None])
    img = stars(img, 8203, density=0.0012)
    return hatch(img)


def sky_mecatol():
    img = base_space(1801, tint="2a1a40")
    img, _ = paint_cloud(img, 1802, VIOLET, cut=0.5, soft=0.4, amt=0.55)
    r, a = polar()
    halo = np.exp(-(r / 210) ** 2)
    img = screen(img, GOLD(halo * 0.85) * (halo * 0.55)[..., None])
    img = stars(img, 1803, density=0.0012, warm=0.6)
    return hatch(img)


def flow_halo():
    S = 512
    yy, xx = np.mgrid[0:S, 0:S].astype(float)
    x, y = xx - S / 2 + 0.5, yy - S / 2 + 0.5
    r, a = np.hypot(x, y), np.arctan2(y, x)
    n = fbm(1811, S, base=8)
    # fine rays of light round the throne world, some longer than others, fading out
    rays = (0.5 + 0.5 * np.cos(a * 36 + n * 1.2)) ** 30 * 0.8 + (0.5 + 0.5 * np.cos(a * 15 - n * 2.0)) ** 40 * 0.6
    reach = 150 + 90 * fbm(1812, S, base=6)
    v = rays * smooth(84, 104, r) * smooth(reach, reach * 0.55, r) * 0.55
    glow = np.exp(-((r - 96) / 18) ** 2) * 0.22
    v = np.clip(v + glow, 0, 1)
    return np.clip(GOLD(0.55 + 0.45 * v) * v[..., None], 0, 1)


def sky_cabal():
    """The Vuil'raith home after the owner's picture: a crimson storm of tendrils and toothed maws, a white
    burst low on the left, cold blue space in the upper right. The pale tentacles round Acheron and the
    lightning are drawn by the game, moving."""
    x, y = grid()
    img = SPACE(crop(fbm(5401, base=2)) * 0.5)
    blue = smooth(-40, 80, x * 0.8 - y - 60)
    img, _ = paint_cloud(img, 5402, ICE, cut=0.45, soft=0.4, amt=0.55, mask=blue)
    red = 1 - smooth(-20, 60, x * 0.8 - y - 90)
    storm = ramp([(0, "1a0408"), (0.4, "5a0c14"), (0.7, "a8121c"), (0.9, "e8302a"), (1, "ff7a5a")])
    img, d = paint_cloud(img, 5403, storm, cut=0.3, soft=0.55, amt=0.92, mask=red)
    img = img * (0.45 + 0.55 * crop(fbm(5406, base=3)))[..., None]  # black pockets in the storm
    # tendrils: dark red veins winding through the cloud
    w = crop(nebula_field(5405, warp_amt=120, base=3))
    vein = np.exp(-((w - 0.5) / 0.018) ** 2) * red
    img = img * (1 - 0.6 * vein[..., None]) + hexc("ff5a3a") * (vein * 0.18)[..., None]
    # maws: dark mouths ringed with pale teeth, half lost in the cloud
    for k, (mx, my, mr) in enumerate(((-150.0, -95.0, 26.0), (130.0, 150.0, 20.0), (-200.0, 40.0, 14.0))):
        rr, aa = np.hypot(x - mx, y - my), np.arctan2(y - my, x - mx)
        # a dark gullet sinking into the cloud, ringed by thin pale fangs pointing in, a raw red lip
        mouth = smooth(mr * 0.75, mr * 0.2, rr)
        img = img * (1 - 0.7 * mouth[..., None])
        fang = smooth(mr * 0.45, mr * 0.95, rr) * smooth(mr * 1.05, mr * 0.95, rr) * \
            np.clip((np.cos(aa * 9 + k) - 1 + 0.35 * (rr / mr)) / 0.35, 0, 1)
        img = screen(img, hexc("e8c8c0") * (fang * 0.22 * red)[..., None])
        lip = np.exp(-((rr - mr * 1.02) / (mr * 0.14)) ** 2)
        img = screen(img, hexc("a01420") * (lip * 0.5)[..., None])
    # the burst: a white-hot core with rays and a red fringe
    bx, by = -150.0, 120.0
    rr, aa = np.hypot(x - bx, y - by), np.arctan2(y - by, x - bx)
    rays = np.clip(np.cos(aa * 7) * 0.5 + np.cos(aa * 13 + 1.0) * 0.5, 0, 1) ** 4
    glow = np.exp(-(rr / 26) ** 2) + rays * np.exp(-(rr / 120) ** 2) * 0.8
    img = screen(img, CRIMSON(np.clip(glow * 1.2, 0, 1)) * np.clip(glow * 1.6, 0, 1)[..., None] * 0.7)
    img = screen(img, hexc("ffffff") * np.clip(np.exp(-(rr / 14) ** 2) * 1.2, 0, 1)[..., None])
    img = stars(img, 5404, density=0.0009, warm=0.6)
    return hatch(img, 0.035)


def sky_hyperlane():
    img = SPACE(crop(fbm(8301, base=2)) * 0.55)
    img, _ = paint_cloud(img, 8302, VIOLET, cut=0.6, soft=0.35, amt=0.35)
    img = stars(img, 8303, density=0.0008)
    return hatch(img, 0.03)


def veil(seed, size=VS):
    """tileable wisps, grey (the game tints and adds them)"""
    a = nebula_field(seed, size, warp_amt=size * 0.3, base=3)
    v = smooth(0.5, 0.95, a) ** 1.3
    ridge = 1 - np.abs(nebula_field(seed + 5, size, warp_amt=size * 0.25, base=4) - 0.5) * 2
    v = np.clip(v * (0.55 + 0.6 * ridge ** 3), 0, 1)
    return v


# ---------------------------------------------------------------- planets
def sphere_points(w=PW, h=PH):
    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    lon = (u - 0.5) * TAU
    s = (v - 0.5) * math.pi
    lon, s = np.meshgrid(lon, s)
    c = np.cos(s)
    return np.stack([c * np.sin(lon), np.sin(s), c * np.cos(lon)], -1)


P3 = sphere_points()
_LAT = None


def snoise(seed, freq=2.0, octaves=6, gain=0.52, N=64):
    """seamless noise on the sphere, 0..1"""
    rng = np.random.default_rng(seed)
    out = np.zeros(P3.shape[:2])
    amp, tot, f = 1.0, 0.0, freq
    for _ in range(octaves):
        if f > N * 0.45:
            break
        g = rng.random((N, N, N))
        c = (P3 * f + N / 2).transpose(2, 0, 1)
        out += amp * ndi.map_coordinates(g, c, order=3, mode="grid-wrap")
        tot += amp
        amp *= gain
        f *= 2.0
    out /= tot
    lo, hi = np.percentile(out, 1), np.percentile(out, 99)
    return np.clip((out - lo) / (hi - lo + 1e-9), 0, 1)


def swarp(seed, freq=1.5, amt=0.35, **kw):
    """domain-warped sphere noise (swirling)"""
    global P3
    keep = P3
    d = np.stack([snoise(seed + 11 + k, freq=1.2, octaves=3) - 0.5 for k in range(3)], -1)
    P3 = keep + d * amt
    n = np.linalg.norm(P3, axis=-1)[..., None]
    P3 = P3 / n
    try:
        return snoise(seed, freq=freq, **kw)
    finally:
        P3 = keep


def lat():
    return P3[..., 1]


class Surf:
    def __init__(self):
        self.alb = np.zeros(P3.shape[:2] + (3,))
        self.emis = None     # 0..1, glows on the night side (pulses)
        self.clouds = None   # 0..1
        self.atmo = "b07cff"
        self.rim = "ff4fa8"
        self.ambient = 0.16
        self.emis_col = "ff7a3a"


def k_ocean(S, s):
    land_n = swarp(s, freq=1.6)
    land = smooth(0.54, 0.58, land_n)
    det = snoise(s + 1, freq=6)
    sea = TEAL(0.25 + 0.6 * np.clip(land_n / 0.56, 0, 1) ** 2.5)
    ground = EMBER(0.55 + 0.35 * det) * 0.55 + GOLD(0.45 + 0.4 * det) * 0.45
    S.alb = sea * (1 - land[..., None]) + ground * land[..., None]
    ice = smooth(0.82, 0.9, np.abs(lat()))
    S.alb = S.alb * (1 - ice[..., None]) + hexc("e8f4ff") * ice[..., None]
    S.clouds = smooth(0.55, 0.85, swarp(s + 2, freq=2.5, amt=0.5))
    S.atmo, S.rim = "6ad8ff", "ff4fa8"


def k_lush(S, s):
    land_n = swarp(s, freq=1.7)
    land = smooth(0.46, 0.5, land_n)
    det = snoise(s + 1, freq=5)
    sea = TEAL(0.3 + 0.5 * np.clip(land_n / 0.47, 0, 1) ** 2)
    green = ramp([(0, "0e3b33"), (0.5, "1f8a6a"), (0.8, "6ad08a"), (1, "e6d27a")])(0.25 + 0.7 * det)
    S.alb = sea * (1 - land[..., None]) + green * land[..., None]
    S.clouds = smooth(0.55, 0.85, swarp(s + 2, freq=2.8, amt=0.5))
    S.atmo, S.rim = "7ae8d0", "ff5aa0"


def k_jungle(S, s):
    n = swarp(s, freq=2.2)
    riv = snoise(s + 3, freq=3)
    S.alb = ramp([(0, "06241f"), (0.4, "0f5a45"), (0.75, "2fae7a"), (1, "b6f08a")])(0.15 + 0.8 * n)
    r = np.exp(-((riv - 0.5) / 0.018) ** 2)
    S.alb = S.alb * (1 - r[..., None]) + hexc("1c7aa0") * r[..., None]
    S.clouds = smooth(0.6, 0.88, swarp(s + 2, freq=3, amt=0.5)) * 0.8
    S.atmo, S.rim = "5af0b0", "ff6aa0"


def k_lavender(S, s):
    band = swarp(s, freq=1.2, amt=0.25)
    b = 0.5 + 0.5 * np.sin(lat() * 9 + band * 6)
    S.alb = VIOLET(0.45 + 0.4 * b) * 0.6 + hexc("f0e2ff") * (0.4 * band)[..., None]
    S.clouds = smooth(0.62, 0.9, swarp(s + 2, freq=2.4)) * 0.6
    S.atmo, S.rim = "d6b8ff", "ff6ad0"


def k_desert(S, s):
    n = swarp(s, freq=1.8)
    dune = 0.5 + 0.5 * np.sin(lat() * 20 + n * 9)
    S.alb = ramp([(0, "5a2412"), (0.4, "b55a22"), (0.75, "f2a04a"), (1, "ffe2a0")])(0.3 + 0.45 * n + 0.2 * dune)
    can = np.exp(-((snoise(s + 4, freq=3) - 0.5) / 0.02) ** 2)
    S.alb = S.alb * (1 - 0.6 * can[..., None])
    S.atmo, S.rim = "ffb070", "ff4f8a"


def k_rust(S, s):
    n = swarp(s, freq=2.0)
    cr = snoise(s + 5, freq=7)
    v = 0.25 + 0.6 * n
    S.alb = ramp([(0, "2a0a0a"), (0.4, "7a2016"), (0.75, "d24a2a"), (1, "ffa070")])(np.where(cr > 0.75, v * 0.6, v))
    S.atmo, S.rim = "ff7a50", "ff4fa8"


def k_lava(S, s):
    n = swarp(s, freq=2.2)
    c = swarp(s + 7, freq=3.0, amt=0.4)
    crack = np.exp(-((c - 0.5) / 0.035) ** 2)
    S.alb = ramp([(0, "120810"), (0.5, "35182c"), (1, "6a3a48")])(0.2 + 0.8 * n)
    S.alb = S.alb * (1 - crack[..., None]) + EMBER(0.7 + 0.3 * crack) * crack[..., None]
    S.emis = np.clip(crack * 1.1, 0, 1)
    S.emis_col = "ff6a2a"
    S.atmo, S.rim = "ff6a3a", "ff3a80"
    S.ambient = 0.2


def k_ice(S, s):
    n = swarp(s, freq=1.8)
    S.alb = ICE(0.45 + 0.5 * n)
    v = np.exp(-((swarp(s + 3, freq=2.5) - 0.5) / 0.02) ** 2)
    S.alb = S.alb * (1 - v[..., None]) + hexc("4a8ae0") * v[..., None]
    S.emis = v * 0.5
    S.emis_col = "6ae0ff"
    S.atmo, S.rim = "bfe8ff", "b07cff"


def k_toxic(S, s):
    band = swarp(s, freq=1.4, amt=0.4)
    b = 0.5 + 0.5 * np.sin(lat() * 7 + band * 7)
    S.alb = ramp([(0, "1a2a10"), (0.4, "4a7a1a"), (0.75, "a8d83a"), (1, "f0ffa0")])(0.2 + 0.6 * b)
    S.alb = S.alb * 0.8 + TEAL(band) * 0.2
    S.clouds = smooth(0.6, 0.9, swarp(s + 2, freq=2.6)) * 0.5
    S.atmo, S.rim = "c8ff6a", "ff4fa8"


def k_gas(S, s):
    sw = swarp(s, freq=1.5, amt=0.5)
    b = lat() * 11 + sw * 4
    S.alb = ramp([(0, "3a1450"), (0.3, "8a2a7a"), (0.55, "e05a6a"), (0.8, "ffa860"), (1, "ffe6c0")])(0.5 + 0.45 * np.sin(b))
    spot = np.exp(-(((P3[..., 0] - 0.5) / 0.12) ** 2 + ((P3[..., 1] - 0.25) / 0.07) ** 2))
    S.alb = S.alb * (1 - spot[..., None] * 0.7) + hexc("ff4f6a") * (spot * 0.7)[..., None]
    S.atmo, S.rim = "ff9ae0", "ff4fa8"


def k_barren(S, s):
    n = swarp(s, freq=2.4)
    cr = snoise(s + 5, freq=10)
    v = 0.3 + 0.55 * n
    v = np.where(cr > 0.78, v * 0.55, np.where(cr > 0.72, np.minimum(1, v * 1.2), v))
    S.alb = ramp([(0, "1a1426"), (0.5, "5a5070"), (1, "c4b8d8")])(v)
    S.atmo, S.rim = "a898c8", "ff6ad0"
    S.atmo = None


def k_steel(S, s):
    n = snoise(s, freq=2.2)
    det = snoise(s + 1, freq=12)
    u = np.linspace(0, 1, P3.shape[1])[None, :] * np.ones(P3.shape[:2])
    trench = (np.abs(np.sin(u * TAU * 24)) < 0.06) | (np.abs(np.sin(lat() * 30)) < 0.06)
    S.alb = ramp([(0, "141220"), (0.5, "3e3a54"), (1, "8a86a8")])(0.3 + 0.5 * n + 0.15 * (det - 0.5))
    S.alb = np.where(trench[..., None], S.alb * 0.35, S.alb)
    lights = trench & (det > 0.55)
    S.emis = lights.astype(float) * 0.9
    S.emis_col = "ffb05a"
    S.atmo, S.rim = None, "ff5aa0"


def k_city(S, s):
    n = snoise(s, freq=2.0)
    det = snoise(s + 1, freq=14)
    u = np.linspace(0, 1, P3.shape[1])[None, :] * np.ones(P3.shape[:2])
    street = (np.abs(np.sin(u * TAU * 40 + n * 3)) < 0.07) | (np.abs(np.sin(lat() * 44 + n * 2)) < 0.07)
    sea = n < 0.25
    S.alb = GOLD(0.35 + 0.5 * det) * 0.75 + hexc("b8a890") * (0.25 * det)[..., None]
    S.alb = np.where(street[..., None], S.alb * 0.4, S.alb)
    S.alb = np.where(sea[..., None], TEAL(0.3 + n), S.alb)
    S.emis = (street & ~sea & (det > 0.4)).astype(float)
    S.emis_col = "ffd27a"
    S.atmo, S.rim = "ffd890", "ff7ab0"


def k_mallice(S, s):
    n = swarp(s, freq=2.0)
    S.alb = TEAL(0.25 + 0.6 * n) * 0.7 + VIOLET(n) * 0.3
    v = np.exp(-((swarp(s + 3, freq=3) - 0.5) / 0.025) ** 2)
    S.alb = S.alb * (1 - v[..., None]) + hexc("ff6ad0") * v[..., None]
    S.emis = v
    S.emis_col = "ff6ad0"
    S.atmo, S.rim = "6ae8e0", "ff4fa8"


def k_dark(S, s):
    n = snoise(s, freq=2.0)
    S.alb = VIOLET(0.05 + 0.2 * n) * 0.6
    g = np.exp(-((swarp(s + 3, freq=2.4) - 0.5) / 0.03) ** 2)
    S.emis = g * 0.8
    S.emis_col = "b07cff"
    S.atmo, S.rim = "6a3ab0", "b07cff"
    S.ambient = 0.08


def k_cabal(S, s):
    """Acheron: grey-violet rock riddled with holes that glow red inside (owner's picture)"""
    n = snoise(s, freq=2.5)
    rng = np.random.default_rng(s)
    pts = rng.normal(size=(260, 3))
    pts /= np.linalg.norm(pts, axis=1)[:, None]
    flat = P3.reshape(-1, 3)
    d = np.sort(np.linalg.norm(flat[:, None, :] - pts[None, :, :], axis=2), axis=1)
    d1 = d[:, 0].reshape(P3.shape[:2])
    edge = (d[:, 1] - d[:, 0]).reshape(P3.shape[:2])
    rad = 0.1 + 0.05 * snoise(s + 2, freq=3)
    hole = smooth(rad, rad * 0.7, d1) * smooth(0.0, 0.25, lat() * 0.0 + (P3[..., 0] * 0.6 - P3[..., 1] * 0.2 + 0.5))
    S.alb = ramp([(0, "1a1420"), (0.5, "4a4458"), (1, "9a90a8")])(0.3 + 0.6 * n)
    S.alb = S.alb * (1 - 0.4 * smooth(0.04, 0.0, edge)[..., None])
    deep = smooth(rad * 0.7, rad * 0.2, d1)
    inner = CRIMSON(0.55 + 0.4 * deep)
    S.alb = S.alb * (1 - hole[..., None]) + inner * hole[..., None]
    S.alb = S.alb * (1 - (deep * hole * 0.6)[..., None])
    S.emis = np.clip(hole * (0.5 + 0.5 * (1 - deep)), 0, 1)
    S.emis_col = "ff3a1a"
    S.atmo, S.rim = "c01a24", "6a8aff"


def k_mirage(S, s):
    n = swarp(s, freq=1.6)
    S.alb = MAGENTA(0.35 + 0.5 * n) * 0.6 + TEAL(1 - n) * 0.4
    S.clouds = smooth(0.55, 0.85, swarp(s + 2, freq=2.2)) * 0.7
    S.atmo, S.rim = "ffd890", "6ae8ff"


# 0.43.1: the Fracture's worlds and Thunder's Edge get looks of their own
def k_styx(S, s):
    """Styx: a black ocean wound with glowing blue currents, a few ash-pale islands"""
    n = swarp(s, freq=1.6)
    cur = np.exp(-((swarp(s + 3, freq=2.2, amt=0.45) - 0.5) / 0.03) ** 2)
    S.alb = ramp([(0, "05060f"), (0.5, "0e1430"), (1, "24305a")])(0.2 + 0.7 * n)
    isl = smooth(0.7, 0.74, n)
    S.alb = S.alb * (1 - isl[..., None]) + ramp([(0, "6a6470"), (1, "c8c0c8")])(snoise(s + 1, freq=6))[..., :] * isl[..., None]
    S.alb = S.alb * (1 - cur[..., None]) + hexc("3a8aff") * cur[..., None]
    S.emis = np.clip(cur * 1.1, 0, 1)
    S.emis_col = "5ab0ff"
    S.clouds = smooth(0.62, 0.9, swarp(s + 2, freq=2.4, amt=0.5)) * 0.55
    S.atmo, S.rim = "6a9aff", "ff4f6a"


def k_lethe(S, s):
    """Lethe: a pale world under drifting fog - forgetting itself"""
    band = swarp(s, freq=1.1, amt=0.3)
    b = 0.5 + 0.5 * np.sin(lat() * 6 + band * 5)
    S.alb = ramp([(0, "3a3450"), (0.5, "8a84a8"), (1, "e8e4f4")])(0.35 + 0.45 * b)
    S.clouds = smooth(0.42, 0.8, swarp(s + 2, freq=1.8, amt=0.6)) * 0.85
    S.atmo, S.rim = "e0dcff", "ff6ad0"


def k_storm(S, s):
    """Thunder's Edge: a steel-grey giant banded with storms, lightning in its clouds"""
    sw = swarp(s, freq=1.4, amt=0.6)
    b = lat() * 9 + sw * 5
    S.alb = ramp([(0, "141a30"), (0.35, "3a4668"), (0.6, "7a88ac"), (0.82, "c0c8de"), (1, "f2f5ff")])(0.5 + 0.42 * np.sin(b))
    eye = np.exp(-(((P3[..., 0] - 0.32) / 0.07) ** 2 + ((P3[..., 1] + 0.2) / 0.05) ** 2))
    S.alb = S.alb * (1 - eye[..., None] * 0.6) + hexc("e8eeff") * (eye * 0.6)[..., None]
    S.clouds = smooth(0.52, 0.86, swarp(s + 2, freq=2.6, amt=0.6)) * 0.8
    flash = smooth(0.8, 0.9, snoise(s + 5, freq=8))
    S.emis = np.clip(flash, 0, 1)
    S.emis_col = "9ad8ff"
    S.atmo, S.rim = "9ab8ff", "7a8cff"


KINDS = {"ocean": k_ocean, "lush": k_lush, "jungle": k_jungle, "lavender": k_lavender, "desert": k_desert,
         "rust": k_rust, "lava": k_lava, "ice": k_ice, "toxic": k_toxic, "gas": k_gas, "barren": k_barren,
         "steel": k_steel, "city": k_city, "mallice": k_mallice, "dark": k_dark, "mirage": k_mirage, "cabal": k_cabal,
         "styx": k_styx, "lethe": k_lethe, "storm": k_storm}

# the same look per planet as the Retrowave tileset (tools/gen_retro.py)
NAMED = {
    "mecatolrex": "city", "mallice": "mallice", "mirage": "mirage", "quann": "ocean", "lodor": "desert", "atlas": "lava",
    "cormund": "rust", "everra": "lavender", "creuss": "ice", "thedark": "dark", "primor": "lush", "hope'send": "lava",
    "jord": "lush", "mollprimus": "desert", "darien": "desert", "muaat": "lava", "nestphar": "jungle", "[0.0.0]": "steel",
    "winnu": "lavender", "mordaiii": "barren", "maaluuk": "ocean", "druaa": "jungle", "arcprime": "steel",
    "wrenterra": "barren", "ragh": "desert", "lisisii": "rust", "nar": "ocean", "jol": "ice", "quinarra": "lava",
    "tren'lak": "rust", "trenlak": "rust", "archonren": "jungle", "archontau": "toxic", "retillion": "jungle",
    "shalloq": "toxic", "arretze": "desert", "hercant": "gas", "kamdorn": "desert", "ixth": "steel", "arcturus": "ice",
    "acheron": "cabal", "elysium": "lush", "naazir": "jungle", "rokha": "toxic", "ylir": "ice", "avar": "ocean",
    "valk": "rust",
    # 0.43.1: the Fracture and Thunder's Edge
    "cocytus": "ice", "styx": "styx", "lethe": "lethe", "phlegethon": "lava", "thunder'sedge": "storm", "thundersedge": "storm",
    "magna": "steel",  # (0.83.0 Blue Reverie: the Atokera's living-metal homeworld)
    "iterum": "ice",  # (0.84.0 Blue Reverie: the Belkosea's frozen fortress world; Conviction is their space station)
    "phar": "desert",  # (0.85.0 Blue Reverie: the Pharad'n Order's tomb world)
}
BY_TRAIT = {1: ["ocean", "lush", "jungle", "lavender", "lush", "ocean"],
            2: ["lava", "ice", "toxic", "gas", "barren", "lava"],
            3: ["desert", "rust", "steel", "barren", "desert", "rust"],
            0: ["ocean", "lush", "desert", "ice", "steel", "jungle"]}


def planet_kind(p):
    k = p["name"].lower().replace(" ", "")
    if k in NAMED:
        return NAMED[k]
    h = zlib.crc32(p["key"].encode())
    return BY_TRAIT.get(p["trait"], BY_TRAIT[0])[h % 6]


def paint_planet(p):
    kind = planet_kind(p)
    seed = zlib.crc32(p["key"].encode()) % 100000
    S = Surf()
    KINDS[kind](S, seed)
    if p["legendary"]:
        S.atmo = "ffd27a"
    period = 18 + (zlib.crc32(b"spin" + p["key"].encode()) % 18)   # 18-35 s a turn
    if kind == "city":
        period = 48
    return kind, S, period




# ---------------------------------------------------------------- the Fracture and Thunder's Edge (0.43.1, owner:
# "a Voidfall version ... not anomaly or black hole backgrounds for the Fracture ... something entirely unique")
# The Fracture: space cracked like a pane of dark glass - every shard shows another patch of red sky, the cracks
# glow molten (the game adds a pulse running along them: "pulse" in index.txt, the crack mask fmaskN). Thunder's
# Edge: the cyclone it hides in, turning round the planet in its eye.
def cracked(seed, kind, accent, impact=(0.0, 0.0)):
    """Space cracked like a pane of dark glass struck once: radial cracks from the point of impact, rings of
    straight chords between them; every shard shows a different patch of red sky (a broken mirror) and is lit
    across like a facet; the cracks glow molten. kind: planet / void / egress. Returns (rgb, crack mask)."""
    rng = np.random.default_rng(seed)
    X, Y = grid()
    px, py = impact
    dx, dy = X - px, Y - py
    r = np.hypot(dx, dy)
    th = np.arctan2(dy, dx) % TAU
    nrad = {"planet": 9, "void": 15, "egress": 13}[kind]
    a = np.sort((np.arange(nrad) + rng.uniform(-0.32, 0.32, nrad)) * TAU / nrad + rng.uniform(0, TAU)) % TAU
    a = np.sort(a)
    wob = rng.uniform(0, TAU, nrad)
    # each radial crack wanders a little with distance (and can stop short)
    reach = rng.uniform(0.55, 1.6, nrad) * 330
    if kind != "planet":
        reach = np.maximum(reach, 260)
    radial = []
    for k in range(nrad):
        # glass cracks run straight, with a little jag (a few pixels, not a wave)
        ak = a[k] + (3.5 * np.sin(r / 23.0 + wob[k]) + 1.8 * np.sin(r / 6.5 + 2 * wob[k]) + 6.0 * np.sin(r / 97.0 + 3 * wob[k])) / np.maximum(r, 24.0)
        dth = np.angle(np.exp(1j * (th - ak)))
        radial.append((dth, ak))
    # sector index: how many cracks lie before this angle (with the wobble)
    sector = np.zeros(r.shape, int)
    for k in range(nrad):
        sector += (radial[k][0] > 0).astype(int)
    sector %= nrad
    dist_rad = np.full(r.shape, 1e9)
    inten = np.zeros(r.shape)
    rad_int = rng.uniform(0.35, 1.0, nrad)
    for k in range(nrad):
        dth, ak = radial[k]
        d = np.abs(dth) * np.maximum(r, 1.0)
        d = np.where(r > reach[k], 1e9, d)
        fade = np.clip(1.15 - r / (reach[k] + 1), 0.25, 1.0)
        better = d < dist_rad
        dist_rad = np.where(better, d, dist_rad)
        inten = np.where(better, rad_int[k] * fade, inten)
    # rings: straight chords between neighbouring radial cracks
    nring = {"planet": 3, "void": 5, "egress": 3}[kind]
    radii = np.sort(rng.uniform(45, 330, nring)) if kind != "egress" else np.array([70.0, 150.0, 240.0])
    ring_idx = np.zeros(r.shape, int)
    dist_ring = np.full(r.shape, 1e9)
    ring_int = np.zeros(r.shape)
    for j, rj in enumerate(radii):
        # per sector the chord's two ends sit at jittered radii on the two cracks
        ends = rj * rng.uniform(0.82, 1.18, nrad)
        k0 = sector
        k1 = (sector + 1) % nrad
        a0, a1 = a[(k0 - 1) % nrad], a[k0 % nrad]
        r0, r1 = ends[(k0 - 1) % nrad], ends[k0 % nrad]
        # chord from (r0, a0) to (r1, a1)
        x0, y0 = r0 * np.cos(a0), r0 * np.sin(a0)
        x1, y1 = r1 * np.cos(a1), r1 * np.sin(a1)
        nx, ny = y1 - y0, -(x1 - x0)
        nl = np.hypot(nx, ny) + 1e-6
        sd = ((dx - x0) * nx + (dy - y0) * ny) / nl
        outside = sd * np.sign((0 - x0) * nx + (0 - y0) * ny) < 0
        ring_idx += outside.astype(int)
        d = np.abs(sd)
        present = rng.random(nrad)[k0] < (0.8 if kind != "planet" else 0.6)
        d = np.where(present, d, 1e9)
        better = d < dist_ring
        dist_ring = np.where(better, d, dist_ring)
        ring_int = np.where(better, 0.85 - 0.12 * j, ring_int)
    edge = np.minimum(dist_rad, dist_ring)
    cint = np.where(dist_rad <= dist_ring, inten, ring_int)
    shard = sector * 8 + ring_idx
    # the sky in each shard
    neb = nebula_field(seed + 3, warp_amt=110, base=3)
    neb2 = nebula_field(seed + 4, warp_amt=70, base=4)
    acc = {"ice": ICE, "violet": VIOLET, "ember": EMBER, "teal": TEAL, "gold": GOLD}[accent]
    img = np.zeros((BH, BW, 3))
    ids = np.unique(shard)
    for sid in ids:
        m = shard == sid
        srng = np.random.default_rng(seed * 1000 + int(sid))
        ox, oy, ang = srng.uniform(0, 512), srng.uniform(0, 512), srng.uniform(0, TAU)
        c, s = math.cos(ang), math.sin(ang)
        u = (X[m] * c - Y[m] * s + ox) % 512
        v = (X[m] * s + Y[m] * c + oy) % 512
        d = ndi.map_coordinates(neb, [v, u], order=1, mode="grid-wrap")
        d2 = ndi.map_coordinates(neb2, [v, u], order=1, mode="grid-wrap")
        col = SPACE(0.2 + 0.55 * d2)
        cl = CRIMSON(np.clip((d - 0.3) / 0.7, 0, 1))
        al = smooth(0.3, 0.85, d)[..., None]
        col = col * (1 - al) + cl * al
        roll = srng.random()
        if roll < 0.14:
            ac = acc(np.clip(d * 1.05, 0, 1))
            col = col * 0.45 + ac * (0.35 + 0.55 * smooth(0.2, 0.75, d))[..., None] * 0.6
        elif roll > 0.84:
            col = col * 0.3  # obsidian
        f = srng.uniform(0, TAU)
        proj = (X[m] * math.cos(f) + Y[m] * math.sin(f))
        lo, hi = proj.min(), proj.max()
        g = 0.68 + 0.5 * (proj - lo) / max(1.0, hi - lo)
        img[m] = col * g[..., None]
    st = stars(np.zeros((BH, BW, 3)), seed + 9, density=0.0012, warm=0.55)
    st2 = stars(np.zeros((BH, BW, 3)), seed + 10, density=0.0012, warm=0.55)
    img = screen(img, np.where((shard % 2 == 0)[..., None], st, st2) * 0.85)
    # the cracks: a dark split, a molten line, a crimson bloom - brighter near the impact
    near = np.exp(-r / (260.0 if kind != "planet" else 200.0))
    w = cint * (0.55 + 0.45 * near)
    gap = 0.9 + 1.6 * w * (1.4 if kind == "void" else 1.0)
    dark = smooth(gap + 0.5, gap - 0.5, edge)
    hot = np.exp(-((edge - gap - 0.9) / 1.0) ** 2) * w
    core = np.exp(-((edge - gap - 0.7) / 0.5) ** 2) * w ** 1.5
    bloom = np.exp(-np.maximum(edge - gap, 0) / (7.0 + 9.0 * w)) * w
    img = img * (1 - 0.92 * dark)[..., None]
    img = screen(img, hexc("7a0614")[None, None, :] * (0.6 * bloom)[..., None])
    img = screen(img, hexc("ff4424")[None, None, :] * (0.9 * hot)[..., None])
    img = screen(img, hexc("fff2d0")[None, None, :] * core[..., None])
    mask = np.clip(0.5 * bloom + 0.9 * hot + core, 0, 1)
    # the point of impact: splinters of light (the void), the gate (an egress)
    if kind == "egress":
        # the way out: a tall slit where the pane has split open - white light through crimson lips
        slit = np.exp(-((dx / 9.0) ** 2 + (dy / 120.0) ** 2) ** 1.2)
        lips = np.exp(-((np.abs(dx) - 14.0 * np.sqrt(np.clip(1 - (dy / 125.0) ** 2, 0, 1))) / 5.0) ** 2) * (np.abs(dy) < 125)
        glow = np.exp(-((dx / 46.0) ** 2 + (dy / 150.0) ** 2))
        img = img * (1 - 0.35 * glow)[..., None]
        img = screen(img, hexc("ff2a4a")[None, None, :] * (0.75 * lips + 0.3 * glow)[..., None])
        img = screen(img, hexc("fff4f8")[None, None, :] * slit[..., None])
        mask = np.clip(mask + 0.8 * lips + 0.6 * slit + 0.25 * glow, 0, 1)
    else:
        # the point of impact: crushed glass - a little ring of splinters, no glow to speak of
        crush = np.exp(-r / (16.0 if kind == "void" else 10.0))
        sp = (0.5 + 0.5 * np.cos(th * 23.0 + np.sin(r / 5.0) * 2.0)) ** 8 * np.exp(-((r - 24.0) / 14.0) ** 2)
        img = img * (1 - 0.5 * crush)[..., None]
        img = screen(img, hexc("ffd8b8")[None, None, :] * (0.55 * sp + 0.35 * crush)[..., None])
        mask = np.clip(mask + 0.6 * sp + 0.4 * crush, 0, 1)
    # embers drifting off the cracks
    emb = np.zeros((BH, BW))
    erng = np.random.default_rng(seed + 11)
    for _ in range({"planet": 150, "void": 320, "egress": 260}[kind]):
        x, y = erng.uniform(0, BW), erng.uniform(0, BH)
        xi, yi = int(x), int(y)
        if edge[yi, xi] > 30 * erng.random() + 3:
            continue
        rr = erng.uniform(0.5, 1.6)
        y0, y1, x0, x1 = max(0, yi - 5), min(BH, yi + 6), max(0, xi - 5), min(BW, xi + 6)
        yy, xx = np.mgrid[y0:y1, x0:x1]
        emb[y0:y1, x0:x1] += np.exp(-(np.hypot(xx - x, yy - y) / rr) ** 2) * erng.uniform(0.35, 1.0)
    img = screen(img, hexc("ffa060")[None, None, :] * np.clip(emb, 0, 1)[..., None])
    return np.clip(hatch(img), 0, 1), mask


FRACTURE = [  # fracture1..7: Cocytus, egress, void, Styx, void, egress, Lethe / Phlegethon
    (1, "planet", "ice", (150.0, -120.0)),
    (2, "egress", "violet", (0.0, 0.0)),
    (3, "void", "ember", (-30.0, 20.0)),
    (4, "planet", "violet", (-160.0, 110.0)),
    (5, "void", "gold", (40.0, -25.0)),
    (6, "egress", "teal", (0.0, 0.0)),
    (7, "planet", "ember", (-150.0, -130.0)),
]


STORM = ramp([(0, "0a0c1e"), (0.3, "1c2448"), (0.55, "3e4f86"), (0.75, "7f95c8"), (0.9, "c8d6f0"), (1, "f4f8ff")])


def cyclone(seed, X, Y, arms=3, tight=1.5):
    """cloud density of a cyclone seen from above: puffy cloud (sheared along the spiral) gathered into arms"""
    r = np.hypot(X, Y) + 1e-3
    th = np.arctan2(Y, X)
    shear = tight * np.log(r / 60.0)
    c, s = np.cos(shear), np.sin(shear)
    # puffy cloud, its coordinates turned more the further out (the arms drag it round)
    puff = fbm(seed, 512, base=5, octaves=7, gain=0.6)
    u = ((X * c - Y * s) * 0.9 + 256) % 512
    v = ((X * s + Y * c) * 1.6 + 256) % 512
    p = ndi.map_coordinates(puff, [v, u], order=1, mode="grid-wrap")
    band = 0.5 + 0.5 * np.cos(arms * (th + shear))
    dens = np.clip((0.35 + 0.65 * band) * (0.25 + 0.95 * p), 0, 1)
    return dens, r, th, p


def sky_te():
    """Thunder's Edge (0.43.1, Voidfall): the storm it hides in - a cyclone of cloud wound round a calm eye (the
    planet sits there), lit from the upper left, lightning glowing inside it, deep indigo beyond"""
    X, Y = grid()
    img = base_space(9601, tint="141a40")
    dens, r, th, p = cyclone(9602, X, Y)
    eye = smooth(62.0, 104.0, r)
    outer = smooth(350.0, 200.0, r)
    a = smooth(0.22, 0.62, dens) * eye * (0.25 + 0.75 * outer)
    light = np.clip(0.6 - 0.5 * (X + Y) / 420.0 + 0.35 * (p - 0.5), 0, 1)
    col = STORM(np.clip(0.18 + 0.85 * dens * (0.45 + 0.55 * light), 0, 1))
    col = col * (0.82 + 0.18 * light[..., None]) + VIOLET(dens) * 0.12
    img = img * (1 - a[..., None]) + col * a[..., None]
    # the eye wall: towering cloud, the brightest of all
    wall = np.exp(-((r - 84.0) / 15.0) ** 2) * (0.55 + 0.45 * p)
    img = screen(img, hexc("dde6ff")[None, None, :] * (0.7 * wall)[..., None])
    # lightning inside the clouds: cyan-white glows
    rng = np.random.default_rng(9604)
    for _ in range(10):
        ang, rr = rng.uniform(0, TAU), rng.uniform(110, 250)
        cx, cy = math.cos(ang) * rr, math.sin(ang) * rr * 0.85
        g = np.exp(-((X - cx) ** 2 + (Y - cy) ** 2) / (2 * rng.uniform(14, 28) ** 2)) * rng.uniform(0.4, 0.9)
        img = screen(img, hexc("9ad8ff")[None, None, :] * (g * a)[..., None])
    img = stars(img, 9605, density=0.0006)
    return np.clip(hatch(img), 0, 1)


def flow_te():
    """the moving layer: wisps of pale cloud (added to the sky), turning round the eye"""
    S = 512
    yy, xx = np.mgrid[0:S, 0:S].astype(float)
    X, Y = xx - S / 2 + 0.5, yy - S / 2 + 0.5
    dens, r, th, p = cyclone(9611, X, Y, arms=4, tight=1.8)
    a = smooth(0.5, 0.85, dens) * smooth(70.0, 115.0, r) * smooth(256.0, 160.0, r)
    return np.clip(np.stack([a * 0.38, a * 0.42, a * 0.55], -1), 0, 1)


# ---------------------------------------------------------------- the packs
def to_u8(a):
    return (np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8)


# ---------------------------------------------------------------- the New Game backdrop (0.33.1)
def tendril_strokes(w, h, seed, n, scale=1.0):
    """the void's tendrils (after the box art): long tapering bundles of strands sweeping in from beyond the
    upper right edge; returns (coverage 0..1, position along 0..1, tone 0..1) at w x h"""
    from PIL import ImageDraw, ImageFilter
    rng = np.random.default_rng(seed)
    S = 2  # drawn twice the size, then reduced (smooth edges)
    cov = Image.new("L", (w * S, h * S), 0)
    along = Image.new("L", (w * S, h * S), 0)
    tone = Image.new("L", (w * S, h * S), 0)
    dc, da, dt = ImageDraw.Draw(cov), ImageDraw.Draw(along), ImageDraw.Draw(tone)
    for _ in range(n):
        if rng.random() < 0.6:  # from above the top edge (right half) or beyond the right edge
            sx, sy = w * rng.uniform(0.4, 1.05), -h * rng.uniform(0.04, 0.15)
        else:
            sx, sy = w * rng.uniform(1.03, 1.12), h * rng.uniform(-0.05, 0.45)
        ang = math.radians(rng.uniform(205, 250))   # down and to the left
        length = h * rng.uniform(0.5, 1.15) * scale
        curl = rng.uniform(-1.0, 1.0)
        w0 = h * rng.uniform(0.022, 0.055) * scale
        bundle_tone = rng.uniform(0.0, 1.0)
        strands = int(rng.integers(3, 7))
        for st in range(strands):
            off = (st - (strands - 1) / 2) / max(1, strands - 1)   # -0.5 .. 0.5 across the bundle
            sw = w0 * rng.uniform(0.35, 0.7)
            ph = rng.uniform(0, TAU)
            ln = length * rng.uniform(0.75, 1.05)
            stone = np.clip(bundle_tone + rng.uniform(-0.25, 0.25), 0, 1)
            steps = 160
            px, py = sx, sy
            prev = None
            for k in range(steps + 1):
                u = k / steps
                a = ang + curl * u * u * 1.5 + 0.22 * math.sin(u * 6 + ph * 0.3)
                # strands spread a little apart along the bundle, then gather at the tip
                spread = w0 * off * 1.6 * math.sin(math.pi * min(1.0, u * 1.3)) + 0.4 * w0 * math.sin(u * 4 + ph)
                pt = ((px - math.sin(a) * spread) * S, (py - math.cos(a) * spread) * S)
                if prev is not None:
                    width = sw * (1 - u) ** 1.25 + 0.7
                    lw = max(1, int(round(width * S)))
                    val = int(255 * (0.55 + 0.45 * (1 - u)) * (0.85 + 0.15 * math.sin(u * 5 + ph)))
                    for d, v in ((dc, val), (da, int(255 * u)), (dt, int(255 * stone))):
                        d.line([prev, pt], fill=v, width=lw)
                        r = lw / 2
                        d.ellipse([pt[0] - r, pt[1] - r, pt[0] + r, pt[1] + r], fill=v)   # round joints
                prev = pt
                seg = ln / steps
                px, py = px + math.cos(a) * seg, py - math.sin(a) * seg
    cov = cov.filter(ImageFilter.GaussianBlur(0.8 * S)).resize((w, h), Image.LANCZOS)
    along = along.resize((w, h), Image.LANCZOS)
    tone = tone.resize((w, h), Image.LANCZOS)
    return np.asarray(cov, float) / 255.0, np.asarray(along, float) / 255.0, np.asarray(tone, float) / 255.0


def newgame_backdrop(w=1280, h=960):
    """the Voidfall New Game screen: void tendrils sweeping in from the upper right, blue and pink clouds
    swirling low on the left, a teal sea of light along the bottom, stars, the print lines"""
    from PIL import ImageFilter
    size = max(w, h)
    v = fbm(9301, size, base=2, octaves=6)[:h, :w]
    img = SPACE(v * 0.8)
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    fx, fy = xx / w, yy / h
    # clouds, lower left: violet and blue billows with pink rims
    cl = nebula_field(9302, size, warp_amt=140, base=3)[:h, :w]
    mask = np.clip(1.35 - (fx * 0.95 + (1 - fy) * 1.05), 0, 1) ** 1.1
    a = smooth(0.36, 0.72, cl) * mask
    img = img * (1 - a[..., None]) + VIOLET(cl * 0.9) * a[..., None]
    rim = smooth(0.55, 0.66, cl) * (1 - smooth(0.66, 0.8, cl)) * mask
    img = screen(img, MAGENTA(0.75 + 0.25 * cl) * (rim * 0.7)[..., None])
    cl2 = nebula_field(9303, size, warp_amt=120, base=4)[:h, :w]
    a2 = smooth(0.52, 0.82, cl2) * mask * 0.55
    img = img * (1 - a2[..., None]) + ICE(cl2 * 0.75) * a2[..., None]
    # a warm glow behind the tendrils, upper right
    glow = np.exp(-(((fx - 0.85) / 0.45) ** 2 + ((fy - 0.08) / 0.38) ** 2))
    img = screen(img, EMBER(glow * 0.75) * (glow * 0.5)[..., None])
    # the tendrils: crimson to magenta by bundle, darker at the edges of each strand, ember towards the tips
    cov, along, tone = tendril_strokes(w, h, 9304, 16)
    base_col = CRIMSON(0.5 + 0.35 * (1 - tone))[...] * (1 - tone[..., None]) + MAGENTA(0.55 + 0.35 * tone) * tone[..., None]
    lit = np.clip(cov * 1.4 - 0.2, 0, 1)
    col = base_col * (0.45 + 0.75 * lit)[..., None]
    col = screen(col, EMBER(0.6 + 0.4 * along) * ((along ** 2.5) * 0.7 * lit)[..., None])
    col = screen(col, hexc("ffd0e0") * (np.clip(cov - 0.85, 0, 1) * 2.5)[..., None])   # bright cores
    k = np.clip(cov * 1.6, 0, 1)
    img = img * (1 - k[..., None]) + col * k[..., None]
    halo = np.asarray(Image.fromarray((cov * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(h * 0.015)), float) / 255.0
    img = screen(img, MAGENTA(0.8) * (halo * 0.3)[..., None])
    # the sea of light along the bottom (the box art's waves): slow teal swells, broken pale crests
    n1 = nebula_field(9305, size, warp_amt=80, base=3)[:h, :w]
    wave_y = 0.84 + 0.035 * np.sin(fx * 7 + 1.3) + 0.012 * np.sin(fx * 19 + 0.4)
    band = smooth(wave_y - 0.015, wave_y + 0.05, fy)
    depth = np.clip((fy - wave_y) / 0.16, 0, 1)
    sea = TEAL(0.35 + 0.35 * n1 - 0.2 * depth)
    img = img * (1 - band[..., None]) + sea * band[..., None]
    foam = np.zeros_like(fx)
    for j, (dy, amp, fr, ph) in enumerate(((0.0, 0.006, 11, 0.0), (0.035, 0.008, 15, 1.7), (0.08, 0.01, 9, 3.1))):
        line = wave_y + dy + amp * np.sin(fx * fr + ph)
        brk = smooth(0.45, 0.7, nebula_field(9310 + j, size, warp_amt=40, base=8)[:h, :w])
        foam += np.exp(-((fy - line) / (0.0045 + 0.002 * j)) ** 2) * brk * (1.0 - 0.25 * j)
    img = screen(img, hexc("e6fbff") * (np.clip(foam, 0, 1) * 0.75)[..., None])
    img = stars(img, 9306, density=0.0005)
    # vignette, then the print lines
    vig = 1 - 0.35 * np.clip(np.hypot(fx - 0.5, fy - 0.5) * 1.6 - 0.35, 0, 1)
    img = img * vig[..., None]
    return hatch(np.clip(img, 0, 1), amt=0.05, period=7.0)


def jpg(a, q=88, w=None, h=None):
    im = Image.fromarray(to_u8(a), "RGB" if a.ndim == 3 else "L")
    if w:
        im = im.resize((w, h), Image.LANCZOS)
    b = io.BytesIO()
    im.save(b, "JPEG", quality=q, optimize=True)
    return b.getvalue()


def png_rgba(a, w=None, h=None):
    im = Image.fromarray(to_u8(a), "RGBA")
    if w:
        im = im.resize((w, h), Image.LANCZOS)
    b = io.BytesIO()
    im.save(b, "PNG", optimize=True)
    return b.getvalue()


def write_pack(path, files):
    toc = bytearray(b"VFD1") + struct.pack("<I", len(files))
    head = 8 + sum(2 + len(n.encode()) + 8 for n, _ in files)
    off = head
    for n, data in files:
        nb = n.encode()
        toc += struct.pack("<H", len(nb)) + nb + struct.pack("<II", off, len(data))
        off += len(data)
    with open(path, "wb") as f:
        f.write(toc)
        for _, data in files:
            f.write(data)
    print("wrote %s: %d files, %.1f MB" % (path, len(files), off / 1e6))


def main():
    data = json.load(open(sys.argv[1]))
    out = sys.argv[2]
    prev = sys.argv[3] if len(sys.argv) > 3 else None
    os.makedirs(out, exist_ok=True)
    if prev:
        os.makedirs(prev, exist_ok=True)
    index = ["# Voidfall tileset (tools/gen_voidfall.py)",
             "# sky NAME FLOW MOTION SPEED VEIL_RGB TWINKLES   MOTION: drift (tileable, veil) | turn (centred) | slide (tileable picture)",
             "# planet KEY KIND PERIOD_S AMBIENT ATMO_RGB RIM_RGB EMIS_RGB HAS_EMIS HAS_CLOUDS"]
    pc, mi = [], []

    def add(name, a, kind="jpg", q=88):
        h, w = a.shape[:2]
        if kind == "jpg":
            pc.append((name, jpg(a, q)))
            mi.append((name, jpg(a, q - 4, w // 2, h // 2)))
        else:
            pc.append((name, png_rgba(a)))
            mi.append((name, png_rgba(a, w // 2, h // 2)))
        if prev:
            im = Image.fromarray(to_u8(a[..., :3] if a.ndim == 3 else a))
            im.thumbnail((256, 256))
            im.save(os.path.join(prev, name + ".png"))

    # the New Game backdrop (0.33.1): 1280 x 960 (the Miyoo's is half that)
    add("newgame", newgame_backdrop(), q=86)
    print("newgame backdrop", flush=True)
    # veils (shared): tileable greys
    for k in range(4):
        add("veil%d" % k, veil(9100 + k), q=86)
        print("veil", k, flush=True)
    skies = []
    for k in range(8):
        add("star%d" % k, sky(k))
        skies.append("sky star%d veil%d drift 1.0 %s 10" % (k, k % 4, SKY_STYLES[k][3]))
        print("sky", k, flush=True)
    specials = [
        ("41", sky_rift(), ("flow_rift", flow_rift(), "turn", "0.6"), "ff7a5a", 4),
        ("42", sky_nebula(), (None, None, "drift", "1.6"), "ff5aa0", 6),
        ("43", sky_supernova(), ("flow_corona", flow_corona(), "turn", "0.25"), "ffa050", 3),
        ("80", sky_supernova("ff9a70"), ("flow_corona", None, "turn", "-0.3"), "ff8a5a", 3),
        ("81", sky_nova_seed(), ("flow_corona", None, "turn", "0.35"), "ffb060", 3),
        ("44", sky_asteroids(4401), ("rocks", rocks_layer(4411), "slide", "1.0"), "b07cff", 6),
        ("45", sky_asteroids(4501), ("rocks", None, "slide", "-0.8"), "ff9a6a", 6),
        ("79", sky_asteroids(7901), ("rocks", None, "slide", "0.7"), "b07cff", 6),
        ("67", sky_rift(), ("flow_rift", None, "turn", "0.45"), "ff7a5a", 4),
        ("68", sky_nebula(), (None, None, "drift", "1.4"), "d06cff", 6),
        ("56", sky_dark(), (None, None, "drift", "0.7"), "8a5aff", 4),
        ("17", sky_creuss(), ("flow_creuss", flow_spiral(1711, ICE, arms=4, inner=50, tight=4.0, amt=0.9), "turn", "0.5"), "8ad0ff", 8),
        ("54", sky_cabal(), (None, None, "drift", "1.2"), "ff3a3a", 6),
        ("82a", sky_mallice(False), ("flow_mallice", flow_spiral(8211, VIOLET, arms=3, inner=30, tight=6.0, amt=0.6), "turn", "0.3"), "8a6aff", 8),
        ("82b", sky_mallice(True), ("flow_mallice_on", flow_spiral(8212, MAGENTA, arms=5, inner=24, tight=6.5, amt=1.0), "turn", "1.1"), "ff6ad0", 6),
        ("18", sky_mecatol(), ("flow_halo", flow_halo(), "turn", "0.08"), "ffc070", 8),
        ("hyper", sky_hyperlane(), (None, None, "drift", "0.6"), "6a8aff", 6),
        # Uncharted Space's paired anomalies (0.33.1): a second moving layer of rocks
        ("d121", sky_asteroid_rift(), ("flow_rift", None, "turn", "0.5"), "ff7a5a", 4, ("rocks", "slide", "0.45")),
        ("d120", sky_asteroid_nebula(), ("rocks", None, "slide", "-0.6"), "ff5aa0", 6),
        ("d36", sky_zelian(), (None, None, "drift", "0.8"), "ffb070", 5),  # 0.43.0: the Zelian rubble
        # Thunder's Edge (0.34.0): the entropic scar
        ("scar", sky_scar(), (None, None, "drift", "0.9"), "ff3a4a", 5),
        # 0.43.1: Thunder's Edge's storm (the expedition side)
        ("te", sky_te(), ("flow_te", flow_te(), "turn", "0.35"), "8ab0ff", 5),
    ]
    # 0.43.1: the Fracture's seven systems - cracked space; the crack masks for the pulse
    pulses = []
    for k, kind, acc, imp in FRACTURE:
        img, mask = cracked(9300 + 17 * k, kind, acc, imp)
        specials.append(("frac%d" % k, img, (None, None, "drift", "0.5"), "ff3a2a", 3))
        add("fmask%d" % k, mask, q=90)
        pulses.append("pulse frac%d fmask%d ff5a30 %s %d" % (k, k, "2.6" if kind == "void" else "3.4", -1 if kind == "egress" else 1))
        print("fracture", k, kind, flush=True)
    made = set()
    for name, img, (fname, fimg, motion, speed), vtint, tw, *second in specials:
        add(name, img)
        if fname and fname not in made and fimg is not None:
            if fname == "rocks":
                add(fname, fimg, kind="png")
            else:
                add(fname, fimg, q=86)
            made.add(fname)
        line = "sky %s %s %s %s %s %d" % (name, fname or "veil%d" % (zlib.crc32(name.encode()) % 4), motion, speed, vtint, tw)
        if second:  # (flow, motion, speed) of a second moving layer
            line += " %s %s %s" % second[0]
        skies.append(line)
        print("sky", name, flush=True)
    index += skies
    index += ["# pulse SKY MASK RGB PERIOD_S DIR   (0.43.1: light running along a mask - out from the middle, or in with -1)"] + pulses
    kinds = {}
    for p in data["planets"]:
        kind, S, period = paint_planet(p)
        key = p["key"]
        add("p_" + key, S.alb, q=90)
        he = S.emis is not None
        hc = S.clouds is not None
        if he:
            add("e_" + key, S.emis, q=86)
        if hc:
            add("c_" + key, S.clouds, q=86)
        index.append("planet %s %s %d %.2f %s %s %s %d %d" % (key, kind, period, S.ambient, S.atmo or "-", S.rim, S.emis_col,
                                                            int(he), int(hc)))
        kinds.setdefault(kind, []).append(p["name"])
    for k, v in sorted(kinds.items()):
        print("planets %-9s %2d: %s" % (k, len(v), ", ".join(v)))
    idx = ("\n".join(index) + "\n").encode()
    pc.append(("index.txt", idx))
    mi.append(("index.txt", idx))
    write_pack(os.path.join(out, "voidfall-pc.dat"), pc)
    write_pack(os.path.join(out, "voidfall-miyoo.dat"), mi)


if __name__ == "__main__":
    main()
