#!/usr/bin/env python3
"""Retrowave art for the game (0.26.0, owner: "pixel art tiles for all tiles").

Built on gen_tile_art.py - the style the owner reviewed and approved in
0.13 (fixed palette, ordered dithering, seamless 64 x 100 ms loops). The
game composes each tile at run time from these pieces:

  * animated backgrounds without planets: 8 starfields (any tile without an
    anomaly) and one per special tile (anomalies, Mallice, the Creuss gate,
    The Dark);
  * animated sprites: the four wormholes at two sizes, the ion storm token
    (both sides), the Dimensional Tear;
  * a texture for every planet (equirectangular, ramp + level per texel,
    cloud and glow layers), turned and lit by the game on its sphere - so a
    planet can sit anywhere at any size (map, expanded tile).

usage: gen_retro.py DATA.json OUT.dat [PREVIEW_DIR] [--scale K]
  DATA.json = `ti4 --dumpdata` (systems and planets); `make retro` runs both.
  --scale 2 (0.48.0): the sharp PC art (assets/retro-pc.dat) - the same designs on a pixel grid twice as fine
  (tiles 192 x 168, planet textures 128 x 64); the game reads the scale from the backgrounds' width.
"""
import json
import math
import os
import struct
import sys
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_tile_art as G  # noqa: E402

TAU = G.TAU
F = 64
FRAME_MS = 100
TW, TH = 64, 32  # planet textures
RAMP_NAMES = list(G.RAMPS.keys())
RID = {n: i for i, n in enumerate(RAMP_NAMES)}


# ---------------------------------------------------------------- backgrounds
def bg_starfield(seed, wisp=None):
    def fn(t):
        cv, hm = G.tile_canvas()
        G.space(cv, seed, hm, lo=0.05, hi=0.66)
        if wisp:  # a faint, still wisp of far nebula for variety (still = small files)
            G.nebula(cv, 0.0, seed + 5, hm, ramp_a=wisp[0], ramp_b=wisp[1], band=None, cut=0.6, amt=0.55, vmax=0.42)
        # stars kept dim enough that units and tokens read on top (owner, 0.13 review)
        G.stars(cv, seed + 1, t, hm, density=0.010, bright=0.78)
        G.plain_edge(cv)
        return cv
    return fn


def bg_rift(t):
    cv, hm = G.tile_canvas()
    G.space(cv, 4101, hm, lo=0.0, hi=0.6)
    G.stars(cv, 4102, t, hm, density=0.008)
    G.black_hole(cv, 0, 0, 7.5, t, 4103, hm)
    G.anomaly_border(cv)
    return cv


def bg_cormund(t):
    cv, hm = G.tile_canvas()
    G.space(cv, 6701, hm, lo=0.0, hi=0.65)
    G.stars(cv, 6702, t, hm, density=0.008)
    bx, by = -23.0, 19.0
    G.black_hole(cv, bx, by, 5.0, t, 6703, hm, inflow=30, reach=26.0)
    rng = np.random.default_rng(6704)
    n = 40
    p0 = rng.uniform(0, 1, n)
    off = rng.normal(0, 1.2, n)
    sp = rng.integers(1, 3, n)
    p = np.mod(p0 + sp * t, 1.0)
    sx, sy, cx_, cy_ = -6.0, 9.0, -20.0, 2.0
    x = (1 - p) ** 2 * sx + 2 * (1 - p) * p * cx_ + p * p * bx + off * (1 - p)
    y = (1 - p) ** 2 * sy + 2 * (1 - p) * p * cy_ + p * p * by
    lv = np.clip(0.9 - 0.5 * p, 0, 1)
    cv.dots(x, y, "rust", lv, hm)
    cv.dots(x[p > 0.6], y[p > 0.6], "hot", lv[p > 0.6], hm)
    G.anomaly_border(cv)
    return cv


def bg_everra(t):
    cv, hm = G.tile_canvas()
    G.space(cv, 6801, hm)
    G.stars(cv, 6802, t, hm)
    G.nebula(cv, t, 6803, hm, band=(0.8, -0.6, 34.0))
    G.anomaly_border(cv)
    return cv


def bg_asteroids_alpha(t):
    cv, hm = G.tile_canvas()
    G.space(cv, 7901, hm)
    G.stars(cv, 7902, t, hm)
    G.dust(cv, t, 7903, hm, 0.35)
    G.asteroid_field(cv, t, 7905, hm, n=26, smax=5.5, avoid=(0, 0, 21))
    G.glints(cv, t, 7906, hm, 6)
    G.anomaly_border(cv)
    return cv


def bg_asteroid_rift(t):
    """Uncharted Space's Asteroid Rift (d121, 0.33.1): the black hole with rocks round it"""
    cv, hm = G.tile_canvas()
    G.space(cv, 12101, hm, lo=0.0, hi=0.6)
    G.stars(cv, 12102, t, hm, density=0.008)
    G.black_hole(cv, 0, 0, 7.5, t, 12103, hm)
    G.asteroid_field(cv, t, 12105, hm, n=18, smax=4.5, avoid=(0, 0, 17))
    G.anomaly_border(cv)
    return cv


def bg_zelian_rubble(t):
    """0.43.0 (owner D44): the Zelian asteroid field - what Zelian R leaves of a cracked world: a dull ember core and
    its rubble in a ring round it"""
    cv, hm = G.tile_canvas()
    G.space(cv, 3601, hm, lo=0.05, hi=0.7)
    G.stars(cv, 3602, t, hm, density=0.008)
    G.embers(cv, 0, 0, 3.0, 12.0, t, 3603, 18, hm)
    G.star_body(cv, 0, 0, 3.2, t, 3604, ramp="fire", lo=0.2)
    G.asteroid_field(cv, t, 3605, hm, n=30, smin=1.6, smax=5.0, avoid=(0, 0, 9))
    G.dust(cv, t, 3606, hm, 0.45)
    G.glints(cv, t, 3607, hm, 5)
    G.anomaly_border(cv)
    return cv


def bg_asteroid_nebula(t):
    """Uncharted Space's Asteroid Nebula (d120, 0.33.1): rocks in a nebula's clouds"""
    cv, hm = G.tile_canvas()
    G.space(cv, 12001, hm)
    G.stars(cv, 12002, t, hm)
    G.nebula(cv, t, 12003, hm, band=(0.6, -0.8, 30.0))
    G.asteroid_field(cv, t, 12005, hm, n=22, smax=5.0)
    G.glints(cv, t, 12006, hm, 4)
    G.anomaly_border(cv)
    return cv


def seg_dist(x, y, pts):
    """distance from every pixel to a polyline"""
    d = np.full(x.shape, 1e9)
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        vx, vy = x1 - x0, y1 - y0
        u = np.clip(((x - x0) * vx + (y - y0) * vy) / (vx * vx + vy * vy), 0, 1)
        d = np.minimum(d, np.hypot(x - x0 - u * vx, y - y0 - u * vy))
    return d


_rs = np.random.default_rng(11400)
_SCAR_X = np.linspace(-52, 52, 13)
_SCAR_Y = _SCAR_X * 0.3 + _rs.uniform(1.5, 4.5, 13) * np.where(np.arange(13) % 2, 1, -1)
_SCAR_BR = []
for _i in range(2, 11, 3):  # fissures off the main crack
    _a, _bx, _by = _rs.uniform(0, TAU), _SCAR_X[_i], _SCAR_Y[_i]
    _pts = [(_bx, _by)]
    for _k in range(3):
        _a += _rs.uniform(-0.6, 0.6)
        _bx, _by = _bx + math.cos(_a) * 4.5, _by + math.sin(_a) * 4.5
        _pts.append((_bx, _by))
    _SCAR_BR.append(_pts)


def bg_scar(t):
    """Thunder's Edge's entropic scar (114 / 116, 0.34.0): space torn open by a jagged crack, white-hot
    inside, bleeding crimson light that breathes; motes drift into it"""
    cv, hm = G.tile_canvas()
    G.space(cv, 11401, hm, lo=0.0, hi=0.5)
    G.stars(cv, 11402, t, hm, density=0.006, bright=0.7)
    d = seg_dist(cv.X, cv.Y, list(zip(_SCAR_X, _SCAR_Y)))
    db = np.min([seg_dist(cv.X, cv.Y, b) for b in _SCAR_BR], axis=0)
    breathe = 0.82 + 0.18 * math.cos(TAU * t * 2)
    g = np.exp(-(d / 7.0) ** 1.2) * breathe
    cv.alpha("crimson", 0.12 + 0.55 * g, g * 0.95, hm & (d < 18))
    cv.shade("crimson", 0.8, hm & (db < 0.75))
    cv.shade("crimson", 0.86, hm & (d < 2.0))
    cv.shade("crimson", 1.0, hm & (d < 0.8))
    rng = np.random.default_rng(11403)
    n = 26
    p0, sp = rng.uniform(0, 1, n), rng.integers(1, 3, n)
    j = rng.integers(0, 13, n)
    sx, sy = rng.uniform(-40, 40, n), rng.uniform(-30, 30, n)
    pp = np.mod(p0 + sp * t, 1.0)
    x = sx + (_SCAR_X[j] - sx) * pp
    y = sy + (_SCAR_Y[j] - sy) * pp
    cv.dots(x, y, "crimson", 0.35 + 0.55 * pp, hm)
    G.anomaly_border(cv)
    return cv


def bg_cabal(t):
    """The Vuil'raith home (0.30.0, owner's picture): a crimson nebula full of tendrils and maws, a white
    burst tearing open low on the left, cold blue space in the upper right. The pale tentacles round
    Acheron are drawn by the game over the planet (retro.cpp)."""
    cv, hm = G.tile_canvas()
    G.space(cv, 5401, hm, lo=0.0, hi=0.45)
    blue = hm & ((cv.X * 0.8 - cv.Y) > 26)
    G.nebula(cv, t, 5402, blue, ramp_a="nebblue", ramp_b="storm", band=None, cut=0.48, amt=0.7, vmax=0.42)
    red = hm & ((cv.X * 0.8 - cv.Y) <= 30)
    G.nebula(cv, t, 5403, red, ramp_a="crimson", ramp_b="redb", band=(0.55, -0.45, 46.0), cut=0.38, amt=1.0, vmax=0.56)
    # tendrils: thin dark-red veins winding through the cloud
    w = G.N2(5405, n=40, kmax=7, beta=1.2, tk=2)(cv.X, cv.Y, t)
    vein = red & (np.abs(w - 0.5) < 0.022)
    cv.shade("crimson", 0.42 + 0.15 * np.cos(TAU * t + cv.X * 0.1), vein)
    # maws: dark mouths ringed with pale teeth, half lost in the cloud
    for k, (mx, my, mr) in enumerate(((-30.0, -18.0, 5.5), (24.0, 30.0, 4.5), (-8.0, 34.0, 3.6))):
        rr, aa = G.polar(cv, mx, my)
        breathe = 1.0 + 0.12 * math.sin(TAU * (t + 0.3 * k))
        cv.shade("black", 0.0, hm & (rr < mr * 0.62 * breathe))
        teeth = hm & (rr >= mr * 0.62 * breathe) & (rr < mr * breathe) & (np.cos(aa * 9 + k) > 0.55)
        cv.shade("cloud", 0.55, teeth)
        cv.shade("crimson", 0.3, hm & (rr >= mr * breathe) & (rr < mr * breathe + 1.3) & ~teeth)
    G.stars(cv, 5404, t, hm, density=0.007, bright=0.7)
    # the burst: a white-hot core with rays that turn and flicker, a red fringe round it
    bx, by = -24.0, 21.0
    rr, aa = G.polar(cv, bx, by)
    pulse = 0.5 + 0.5 * math.cos(TAU * t * 2)
    rays = np.clip(np.cos(aa * 6 + TAU * t) * 0.5 + np.cos(aa * 11 - TAU * 2 * t) * 0.5, 0, 1) ** 4
    glow = np.clip(1 - rr / (6.5 + 2.0 * pulse), 0, 1) ** 1.1 + rays * np.clip(1 - rr / 30.0, 0, 1) ** 2 * 0.95
    fringe = hm & (glow > 0.12) & (glow <= 0.35)
    cv.alpha("crimson", 0.9, glow * 2.0, fringe)
    cv.alpha("star", np.clip(glow, 0, 1), np.clip(glow * 1.6, 0, 1), hm & (glow > 0.35))
    G.plain_edge(cv)
    return cv


def bg_dark(t):
    cv, hm = G.tile_canvas()
    G.space(cv, 5601, hm, lo=0.0, hi=0.5)
    G.stars(cv, 5602, t, hm, density=0.006, bright=0.7)
    G.nebula(cv, t, 5603, hm, ramp_a="nebdark", ramp_b="nebdark", band=None, cut=0.36, gain=1.05, vmax=1.0, hole=(0, 0, G.HOME_R))
    G.anomaly_border(cv)
    return cv


def bg_creuss_gate(t):
    cv, hm = G.tile_canvas()
    G.space(cv, 1701, hm)
    G.stars(cv, 1702, t, hm)
    r, th = G.polar(cv, 0, 0)
    arm = np.cos(4 * th + 0.18 * r - TAU * t)
    a = np.clip(1 - r / 44, 0, 1) * np.clip((arm - 0.55) / 0.45, 0, 1) * 0.8
    cv.alpha("delta", 0.25 + 0.35 * a, a, hm & (r > 18))
    G.plain_edge(cv)
    return cv


def bg_mallice(active):
    def fn(t):
        cv, hm = G.tile_canvas()
        G.space(cv, 8201, hm, ramp="space", lo=0.0, hi=0.7)
        r, th = G.polar(cv, 0, 0)
        gl = np.clip(1 - r / 40, 0, 1) ** 1.5 * (0.9 if active else 0.4)
        cv.alpha("warp", 0.2 + 0.5 * gl, gl, hm)
        G.stars(cv, 8202, t, hm, density=0.006 if active else 0.01)
        if active:
            G.warp(cv, t, 8203, hm, intensity=1.0, nb=120, density=0.8, speeds=(1, 2, 3))
            G.warp(cv, t, 8204, hm, intensity=0.9, nb=60, density=0.4, speeds=(1, 2), ramp="gamma")
        else:
            G.warp(cv, t, 8203, hm, intensity=0.45, nb=80, density=0.35, speeds=(1,))
        G.plain_edge(cv)
        return cv
    return fn


def wrap(piece_fn):
    """A review piece (gen_tile_art PIECES) used as a background as it is."""
    return piece_fn


def piece(name):
    for p in G.PIECES:
        if p["name"] == name:
            return p["fn"]
    raise KeyError(name)


BACKGROUNDS = [
    ("star0", bg_starfield(301)), ("star1", bg_starfield(302)), ("star2", bg_starfield(303)),
    ("star3", bg_starfield(304)), ("star4", bg_starfield(305, ("nebblue", "nebblue"))),
    ("star5", bg_starfield(306, ("nebdark", "nebdark"))), ("star6", bg_starfield(307, ("nebpink", "nebblue"))),
    ("star7", bg_starfield(308, ("nebdark", "nebblue"))),
    ("41", bg_rift), ("42", piece("42_nebula")), ("43", piece("43_supernova")), ("44", piece("44_asteroid_field")),
    ("45", piece("45_asteroid_field")), ("67", bg_cormund), ("68", bg_everra), ("79", bg_asteroids_alpha),
    ("80", piece("80_supernova")), ("81", piece("81_muaat_supernova")), ("56", bg_dark), ("17", bg_creuss_gate),
    ("54", bg_cabal), ("d120", bg_asteroid_nebula), ("d36", bg_zelian_rubble), ("d121", bg_asteroid_rift), ("scar", bg_scar),
    ("82a", bg_mallice(False)), ("82b", bg_mallice(True)),
]


# ---------------------------------------------------------------- sprites
def vortex_sprite(kind, rv, seed, spin):
    half = int(math.ceil(rv * 1.55)) + 1

    def fn(t):
        cv = G.Cv(2 * half, 2 * half)
        m = G.disc_mask(cv, rv * 1.55)
        G.vortex(cv, 0, 0, rv, kind, t, seed, m, particles=8 if rv < 12 else 14, glow=0.5, spin=spin)
        return cv
    return fn


SPRITES = []
for _k, (_kind, _spin) in enumerate((("alpha", 1), ("beta", -1), ("gamma", 1), ("delta", 1), ("epsilon", -1))):
    SPRITES.append(("wh_%s_s" % _kind, vortex_sprite(_kind, 9.0, 9300 + _k, _spin)))
    SPRITES.append(("wh_%s_l" % _kind, vortex_sprite(_kind, 14.0, 9400 + _k, _spin)))


def station_sprite(dim):
    """Thunder's Edge (0.34.0): a space station - a tilted steel ring turning round a lit hub on four
    spokes, a mast with a blinking light (radius 14 art pixels; the game scales it to the planet's spot)"""
    rs, half = 14.0, 20
    lv = 0.6 if dim else 1.0

    def fn(t):
        cv = G.Cv(2 * half, 2 * half)
        X, Y = cv.X / rs, cv.Y / rs
        tilt = 0.42
        ru, rv = X, Y / tilt
        rr = np.hypot(ru, rv)
        th = np.arctan2(rv, ru)
        spin = TAU * t
        s_ = (rr - 0.82) / 0.15
        band = np.abs(s_) < 1.0
        seam = np.mod((th - spin) * 12 / TAU, 1.0)
        lit = np.clip(0.55 + 0.32 * np.cos(th + 2.36) - 0.22 * np.abs(s_) - 0.22 * (seam < 0.12), 0, 1)
        front = rv > 0
        lights = band & (np.abs(seam - 0.5) < 0.09) & (np.abs(s_) < 0.45)

        def spokes(want_front):
            m = np.zeros(X.shape, bool)
            for k in range(4):
                ph = spin * 0.25 + k * TAU / 4
                dx, dy = math.cos(ph), math.sin(ph)
                if (dy > 0) != want_front:
                    continue
                u = np.clip(ru * dx + rv * dy, 0, 0.82)
                m |= np.hypot(ru - u * dx, rv - u * dy) < 0.09
            return m

        cv.shade("cloud", lit * 0.6 * lv, band & ~front)                   # the far half of the ring
        cv.shade("cloud", 0.35 * lv, spokes(False) & ~band)
        mast = (np.abs(X) < 0.06) & (Y < -0.2) & (Y > -0.95)
        cv.shade("cloud", 0.55 * lv, mast)
        hu, hv = X / 0.34, (Y + 0.04) / 0.34
        hub = hu * hu + hv * hv < 1.0
        nz = np.sqrt(np.clip(1 - hu * hu - hv * hv, 0, 1))
        lam = np.clip((-0.6 * hu - 0.6 * hv + 0.55 * nz) / 0.98, 0, 1)
        cv.shade("cloud", (0.15 + 0.85 * lam) * lv, hub)
        win = hub & (np.abs(hv) < 0.16) & (np.mod((np.arctan2(hv, hu) + spin) * 8 / TAU, 1.0) < 0.5) & (nz > 0.3)
        cv.shade("hot", 0.75 * lv, win)
        cv.shade("cloud", 0.45 * lv, spokes(True) & ~band & ~hub)
        cv.shade("cloud", lit * lv, band & front)                          # the near half
        cv.shade("hot", (0.9 if not dim else 0.4), lights & (front | ~hub))
        if not dim and np.mod(t * 8, 1.0) < 0.3:  # the mast's light
            cv.dots([0.0, -1.0, 0.0, -1.0], [-0.95 * rs, -0.95 * rs, -0.95 * rs - 1, -0.95 * rs - 1], "crimson", 0.95)
        return cv
    return fn


SPRITES += [("station", station_sprite(False)), ("station_dim", station_sprite(True))]
SPRITES += [("ion_alpha", piece("token_ion_storm_alpha")), ("ion_beta", piece("token_ion_storm_beta")),
            ("tear", piece("token_dimensional_tear"))]


# ---------------------------------------------------------------- planets
def sphere_points():
    u = (np.arange(TW) + 0.5) / TW
    v = (np.arange(TH) + 0.5) / TH
    lon = (u - 0.5) * TAU
    s = (v - 0.5) * math.pi            # -pi/2 at the top row (screen y down)
    lon, s = np.meshgrid(lon, s)
    c = np.cos(s)
    return np.stack([c * np.sin(lon), np.sin(s), c * np.cos(lon)], -1)


P3 = sphere_points()


class Tex:
    def __init__(self):
        self.ramp = np.zeros((TH, TW), np.int16)
        self.level = np.zeros((TH, TW))
        self.flags = np.zeros((TH, TW), np.int16)   # 1 glow (pulses), 2 lights on the night side
        self.extra = np.zeros((TH, TW))             # clouds (0..1) or the glow's phase
        self.atmo = None
        self.cloud = None   # set by kinds whose extra layer is clouds (others use it for the glow's phase)
        self.rim = None
        self.ambient = 0.14
        self.gain = 1.0

    def put(self, ramp, level, mask=None):
        m = np.ones((TH, TW), bool) if mask is None else mask
        self.ramp[m] = RID[ramp]
        self.level[m] = np.broadcast_to(np.clip(level, 0, 1), (TH, TW))[m]


def N3(seed, **kw):
    return G.N3(seed, **kw)


def k_ocean(T, P, s):
    land_n = N3(s, fmin=1.2, fmax=7)(P)
    land = land_n > 0.56
    det = N3(s + 1, fmin=6, fmax=16)(P)
    T.put("ocean", 0.35 + 0.55 * np.clip(land_n / 0.56, 0, 1) ** 3)
    T.put("land", 0.35 + 0.55 * det, land)
    T.put("cloud", 0.9, np.abs(P[..., 1]) > 0.86)
    cl = N3(s + 2, fmin=2, fmax=10)(P)
    T.extra = np.clip((cl - 0.58) / 0.3, 0, 1) * (cl > 0.58)
    T.cloud = "cloud"
    T.atmo = "atmo"


def k_lush(T, P, s):
    land_n = N3(s, fmin=1.4, fmax=8)(P)
    land = land_n > 0.47
    det = N3(s + 1, fmin=5, fmax=16)(P)
    T.put("ocean", 0.4 + 0.5 * np.clip(land_n / 0.47, 0, 1) ** 2)
    T.put("jungle", 0.3 + 0.6 * det, land)
    T.put("land", 0.55 + 0.4 * det, land & (det > 0.7))
    T.put("cloud", 0.92, np.abs(P[..., 1]) > 0.9)
    cl = N3(s + 2, fmin=2.5, fmax=11)(P)
    T.extra = np.clip((cl - 0.55) / 0.3, 0, 1) * (cl > 0.55)
    T.cloud = "cloud"
    T.atmo = "atmo"


def k_jungle(T, P, s):
    n = N3(s, fmin=1.5, fmax=12)(P)
    riv = N3(s + 3, fmin=2, fmax=8)(P)
    T.put("jungle", 0.25 + 0.7 * n)
    T.put("ocean", 0.55, np.abs(riv - 0.5) < 0.03)
    cl = N3(s + 2, fmin=3, fmax=12)(P)
    T.extra = np.clip((cl - 0.6) / 0.3, 0, 1) * (cl > 0.6)
    T.cloud = "cloud"
    T.atmo = "teal"


def k_lavender(T, P, s):
    n = N3(s, fmin=1.2, fmax=8)(P)
    band = 0.5 + 0.3 * np.sin(P[..., 1] * 5.0 + 4.0 * (n - 0.5))
    T.put("lavender", 0.25 + 0.7 * band)
    cl = N3(s + 2, fmin=2, fmax=9)(P)
    T.extra = np.clip((cl - 0.64) / 0.25, 0, 1) * (cl > 0.64)
    T.cloud = "lavender"
    T.atmo = "lavender"


def k_desert(T, P, s):
    n = N3(s, fmin=1.5, fmax=10)(P)
    band = 0.5 + 0.32 * np.sin(P[..., 1] * 7.0 + 5.0 * (n - 0.5))
    T.put("sand", 0.3 + 0.7 * band)
    T.put("rust", 0.25 + 0.5 * n, n > 0.7)
    T.atmo = "sand"


def k_rust(T, P, s):
    n = N3(s, fmin=1.5, fmax=12)(P)
    cr = N3(s + 5, fmin=8, fmax=18)(P)
    v = 0.3 + 0.6 * n
    T.put("rust", np.where(cr > 0.72, v * 0.6, v))


def k_lava(T, P, s):
    n = N3(s, fmin=2, fmax=12)(P)
    T.put("lavarock", 0.25 + 0.75 * n)
    c = N3(s + 3, fmin=2.5, fmax=9)(P)
    crack = np.abs(c - 0.5) < 0.05
    T.put("lava", 0.55 + 0.25 * (1 - np.abs(c - 0.5) / 0.05), crack)
    T.flags[crack] = 1
    T.extra = np.where(crack, np.mod(9.0 * c / TAU, 1.0), 0.0)


def k_ice(T, P, s, vein_ramp="delta"):
    n = N3(s, fmin=1.5, fmax=12)(P)
    T.put("ice", 0.3 + 0.65 * n)
    c = N3(s + 7, fmin=3, fmax=10)(P)
    vein = np.abs(c - 0.5) < 0.04
    T.put(vein_ramp, 0.75, vein)
    T.flags[vein] = 1
    T.extra = np.where(vein, np.mod(c * 12 / TAU, 1.0), 0.0)
    T.atmo = "ice"


def k_toxic(T, P, s):
    n = N3(s, fmin=1.2, fmax=9)(P)
    band = 0.5 + 0.35 * np.sin(P[..., 1] * 6.0 + 6.0 * (n - 0.5))
    T.put("toxic", 0.2 + 0.7 * band)
    cl = N3(s + 2, fmin=2, fmax=10)(P)
    T.extra = np.clip((cl - 0.6) / 0.3, 0, 1) * (cl > 0.6)
    T.cloud = "toxic"
    T.atmo = "toxic"


def k_gas(T, P, s):
    n = N3(s, fmin=1.5, fmax=10)(P)
    y = P[..., 1]
    band = 0.5 + 0.28 * np.sin(y * 11.0 + 2.5 * (n - 0.5)) + 0.14 * np.sin(y * 23.0 + 1.3)
    T.put("gas", 0.18 + 0.75 * band)
    # a storm spot
    sp = (P[..., 0] - 0.55) ** 2 * 1.0 + (y - 0.32) ** 2 * 6.0 + (P[..., 2] - 0.75) ** 2 * 1.0
    T.put("crimson", 0.55, sp < 0.04)


def k_barren(T, P, s):
    n = N3(s, fmin=1.5, fmax=12)(P)
    cr = N3(s + 5, fmin=7, fmax=20)(P)
    v = 0.25 + 0.6 * n
    T.put("rock", np.where(cr > 0.7, v * 0.55, np.where(cr > 0.66, np.minimum(1, v * 1.25), v)))


def k_steel(T, P, s):  # industrial worlds: grey plating, dark trenches, lights in the cities
    n = N3(s, fmin=1.5, fmax=10)(P)
    det = N3(s + 4, fmin=8, fmax=20)(P)
    T.put("rock", 0.35 + 0.45 * n + 0.15 * (det - 0.5))
    T.put("steel", 0.35 + 0.4 * det, det > np.quantile(det, 0.7))
    c = N3(s + 3, fmin=2.5, fmax=9)(P)
    trench = np.abs(c - 0.5) < 0.035
    T.put("rock", 0.1, trench)
    T.put("rust", 0.3 + 0.4 * det, (n > 0.7) & ~trench)
    city = N3(s + 6, fmin=3, fmax=12)(P)
    lights = (city > 0.62) & ~trench & (G.hash01(np.floor(P[..., 0] * 40) + 97 * np.floor(P[..., 1] * 40) + 31 * np.floor(P[..., 2] * 40)) > 0.35)
    T.flags[lights] = 2
    T.extra = np.where(lights, np.mod(city * 40 / TAU, 1.0), 0.0)


def k_city(T, P, s):  # Mecatol Rex: the ruined imperial city world (no ring - owner)
    n = N3(s, fmin=1.2, fmax=6)(P)
    sea = n < np.quantile(n, 0.1)
    det = N3(s + 4, fmin=8, fmax=20)(P)
    lon = np.arctan2(P[..., 0], P[..., 2])
    lat = np.arcsin(np.clip(P[..., 1], -1, 1))
    gx = lon / TAU * 32
    gy = (lat / math.pi + 0.5) * 16
    street = (np.modf(gx + 100)[0] < 0.25) | (np.modf(gy + 100)[0] < 0.25)
    if TW > 64:  # the sharp art (0.48.0): a few broad avenues and broken side streets, not a full grid
        main = (np.modf(gx / 4 + 100)[0] < 0.06) | (np.modf(gy / 4 + 100)[0] < 0.06)
        cell = G.hash01(np.floor(gx) * 7.0 + np.floor(gy) * 131.0 + 5.0)
        side = ((np.modf(gx + 100)[0] < 0.2) & (cell > 0.62)) | ((np.modf(gy + 100)[0] < 0.2) & (cell < 0.3))
        street = main | side
    T.put("gold", 0.3 + 0.45 * det)                       # old gold and stone districts
    T.put("rock", 0.32 + 0.45 * det, (det < np.quantile(det, 0.3)) & ~sea)  # grey ruins
    T.put("rock", 0.14, street & ~sea)                      # the avenues
    T.put("ocean", 0.3 + 0.3 * n, sea)
    h = G.hash01(np.floor(gx) + 1000 * np.floor(gy))
    lights = ~sea & ~street & (h > 0.45)
    T.flags[lights] = 2
    T.extra = np.where(lights, np.mod(h * 40 / TAU, 1.0), 0.0)
    T.atmo = "gold"


def k_mallice(T, P, s):
    n = N3(s, fmin=1.5, fmax=10)(P)
    T.put("teal", 0.3 + 0.65 * n)
    c = N3(s + 8, fmin=2.5, fmax=8)(P)
    vein = np.abs(c - 0.5) < 0.05
    T.put("gamma", 0.65, vein)
    T.flags[vein] = 1
    T.extra = np.where(vein, np.mod(c * 10 / TAU, 1.0), 0.0)
    T.atmo = "teal"


def k_dark(T, P, s):
    n = N3(s, fmin=1.5, fmax=10)(P)
    T.put("lavender", 0.1 + 0.25 * n)
    g = N3(s + 9, fmin=6, fmax=16)(P)
    glow = g > 0.8
    T.put("gamma", 0.35, glow)
    T.flags[glow] = 1
    T.extra = np.where(glow, np.mod(g * 20 / TAU, 1.0), 0.0)
    T.rim = "rift"
    T.ambient = 0.05
    T.gain = 0.55
    T.atmo = "rift"


def k_cabal(T, P, s):
    """Acheron (0.30.0, owner's picture): dark rock eaten into a honeycomb, the holes glowing red-hot."""
    rng = np.random.default_rng(s)
    c = rng.normal(size=(46, 3))
    c /= np.linalg.norm(c, axis=1)[:, None]
    d = np.linalg.norm(P[..., None, :] - c[None, None, :, :], axis=-1)
    d.sort(axis=-1)
    d1, d2 = d[..., 0], d[..., 1]
    edge = d2 - d1
    n = N3(s + 1, fmin=3, fmax=14)(P)
    T.put("lavarock", 0.35 + 0.5 * n)
    T.put("rock", 0.25 + 0.35 * n, edge < 0.05)        # the ridges between the holes
    hole = (d1 < 0.20) & (edge > 0.06)
    deep = hole & (d1 < 0.12)
    T.put("crimson", 0.55 + 0.35 * (1 - d1 / 0.2), hole)
    T.put("lava", 0.6 + 0.3 * (1 - d1 / 0.12), deep)
    T.flags[hole] = 1                                   # they glow and pulse on the night side too
    T.extra = np.where(hole, np.mod(d1 * 30.0, 1.0), 0.0)
    T.atmo = "redb"


def k_mirage(T, P, s):
    n = N3(s, fmin=1.2, fmax=8)(P)
    T.put("nebpink", 0.35 + 0.5 * n)
    cl = N3(s + 2, fmin=2, fmax=9)(P)
    T.extra = np.clip((cl - 0.5) / 0.35, 0, 1) * (cl > 0.5)
    T.cloud = "gold"
    T.atmo = "gold"


def k_stormgiant(T, P, s):
    """The Thunder's Edge planet (0.45.0, owner): the grey storm giant of the expedition side, its clouds thinner so the
    planet shows - grey bands, blue-grey storm belts, two storm eyes and lightning that flickers on the night side."""
    n = N3(s, fmin=1.5, fmax=10)(P)
    y = P[..., 1]
    band = 0.5 + 0.3 * np.sin(y * 9.0 + 3.0 * (n - 0.5)) + 0.12 * np.sin(y * 21.0 + 0.7)
    T.put("rock", 0.3 + 0.55 * band)
    T.put("storm", 0.35 + 0.45 * band, band > 0.7)
    for (cx, cy, cz), rad in (((0.62, 0.28, 0.73), 0.045), ((-0.72, -0.33, 0.6), 0.03)):
        sp = (P[..., 0] - cx) ** 2 + (y - cy) ** 2 * 3.0 + (P[..., 2] - cz) ** 2
        T.put("cloud", 0.8, sp < rad)
        T.put("storm", 0.25, sp < rad * 0.22)
    l = N3(s + 9, fmin=10, fmax=26)(P)
    bolt = l > 0.84
    T.put("delta", 0.92, bolt)
    T.flags[bolt] = 1
    cl = N3(s + 2, fmin=2, fmax=9)(P)
    T.extra = np.where(bolt, np.mod(l * 40.0 / TAU, 1.0), 0.7 * np.clip((cl - 0.7) / 0.25, 0, 1) * (cl > 0.7))
    T.cloud = "cloud"
    T.atmo = "atmo"


KINDS = {"ocean": k_ocean, "lush": k_lush, "jungle": k_jungle, "lavender": k_lavender, "desert": k_desert,
         "rust": k_rust, "lava": k_lava, "ice": k_ice, "toxic": k_toxic, "gas": k_gas, "barren": k_barren,
         "steel": k_steel, "city": k_city, "mallice": k_mallice, "dark": k_dark, "mirage": k_mirage, "cabal": k_cabal,
         "stormgiant": k_stormgiant}

# planets with a look of their own (by name, lower case without spaces); the rest by trait
NAMED = {
    "mecatolrex": "city", "mallice": "mallice", "mirage": "mirage", "quann": "ocean", "lodor": "desert", "atlas": "lava",
    "cormund": "rust", "everra": "lavender", "creuss": "ice", "thedark": "dark", "primor": "lush", "hope'send": "lava",
    # homes
    "jord": "lush", "mollprimus": "desert", "darien": "desert", "muaat": "lava", "nestphar": "jungle", "[0.0.0]": "steel",
    "winnu": "lavender", "mordaiii": "barren", "maaluuk": "ocean", "druaa": "jungle", "arcprime": "steel",
    "wrenterra": "barren", "ragh": "desert", "lisisii": "rust", "nar": "ocean", "jol": "ice", "quinarra": "lava",
    "tren'lak": "rust", "trenlak": "rust", "archonren": "jungle", "archontau": "toxic", "retillion": "jungle",
    "shalloq": "toxic", "arretze": "desert", "hercant": "gas", "kamdorn": "desert", "ixth": "steel", "arcturus": "ice",
    "acheron": "cabal", "elysium": "lush", "naazir": "jungle", "rokha": "toxic", "ylir": "ice", "avar": "ocean",
    "valk": "rust",
    "thunder'sedge": "stormgiant",  # (0.45.0: the Thunder's Edge planet - owner)
    "magna": "steel",  # (0.83.0 Blue Reverie: the Atokera's living-metal homeworld)
    "iterum": "ice",  # (0.84.0 Blue Reverie: the Belkosea's frozen fortress world; Conviction is their space station)
    "phar": "desert",  # (0.85.0 Blue Reverie: the Pharad'n Order's tomb world)
}
BY_TRAIT = {1: ["ocean", "lush", "jungle", "lavender", "lush", "ocean"],      # cultural
            2: ["lava", "ice", "toxic", "gas", "barren", "lava"],             # hazardous
            3: ["desert", "rust", "steel", "barren", "desert", "rust"],       # industrial
            0: ["ocean", "lush", "desert", "ice", "steel", "jungle"]}


def planet_kind(p):
    k = p["name"].lower().replace(" ", "")
    if k in NAMED:
        return NAMED[k]
    h = zlib.crc32(p["key"].encode())
    return BY_TRAIT.get(p["trait"], BY_TRAIT[0])[h % 6]


def bake_planet(p):
    kind = planet_kind(p)
    seed = zlib.crc32(p["key"].encode()) % 100000
    T = Tex()
    KINDS[kind](T, P3, seed)
    if p["legendary"] and T.atmo is None:
        T.atmo = "gold"
    elif p["legendary"]:
        T.atmo = "gold"
    period = 140 + (zlib.crc32(b"spin" + p["key"].encode()) % 160)  # 14-30 s a turn
    if kind == "city":
        period = 360
    return kind, T, period


# ---------------------------------------------------------------- encoding
def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def packbits(data):
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        j = i
        while j + 1 < n and data[j + 1] == data[i] and j - i < 128:
            j += 1
        run = j - i + 1
        if run >= 2:
            out.append(126 + run)  # 128..254 -> repeat (c - 126) times (2..128)
            out.append(data[i])
            i = j + 1
            continue
        j = i
        while j < n and j - i < 127 and not (j + 1 < n and data[j + 1] == data[j]):
            j += 1
        lit = max(1, j - i)
        out.append(lit - 1)  # 0..126 -> lit + 1 literal bytes
        out += bytes(data[i:i + lit])
        i += lit
    return bytes(out)


def delta(prev, cur):
    """(skip, count, literal bytes)* - pixels that differ from the previous frame."""
    out = bytearray()
    n = len(cur)
    i = 0
    while i < n:
        s = i
        while i < n and cur[i] == prev[i]:
            i += 1
        if i >= n:
            break
        e = i
        # a run of changes; small gaps of unchanged pixels are cheaper as literals
        while e < n:
            if cur[e] != prev[e]:
                e += 1
                continue
            g = e
            while g < n and g - e < 3 and cur[g] == prev[g]:
                g += 1
            if g < n and g - e < 3 and cur[g] != prev[g]:
                e = g
                continue
            break
        out += varint(i - s) + varint(e - i) + bytes(cur[i:e])
        i = e
    return bytes(out)


def encode_anim(name, fn, frames=F):
    idx = [fn(f / frames).idx.copy() for f in range(frames)]
    st = np.stack(idx)
    used = np.unique(st)
    used = used[used != 0]
    if len(used) > 255:
        raise SystemExit("%s: %d colours" % (name, len(used)))
    lut = np.zeros(len(G.PAL), np.uint8)
    lut[used] = np.arange(1, len(used) + 1)
    loc = lut[st]
    h, w = loc.shape[1], loc.shape[2]
    chunks = [packbits(loc[0].tobytes())]
    for k in range(1, frames):
        chunks.append(delta(loc[k - 1].tobytes(), loc[k].tobytes()))
    offs, pos = [], 0
    for ch in chunks:
        offs.append(pos)
        pos += len(ch)
    body = b"".join(chunks)
    head = struct.pack("<B", len(name)) + name.encode() + struct.pack("<HHHH", w, h, frames, FRAME_MS)
    head += struct.pack("<B", len(used)) + b"".join(struct.pack("<H", int(c)) for c in used)
    head += b"".join(struct.pack("<I", o) for o in offs) + struct.pack("<I", len(body))
    return head + body, st


def encode_planet(p):
    kind, T, period = bake_planet(p)
    def rid(n):
        return 255 if n is None else RID[n]
    lvl = np.clip(np.rint(T.level * 255), 0, 255).astype(np.uint8)
    ext = np.clip(np.rint(T.extra * 255), 0, 255).astype(np.uint8)
    b0 = (T.ramp.astype(np.uint8) & 63) | (T.flags.astype(np.uint8) << 6)
    tex = np.stack([b0, lvl, ext], -1).astype(np.uint8)
    key = p["key"].encode()
    head = struct.pack("<B", len(key)) + key + struct.pack("<BBBBBHBB", rid(T.atmo), rid(T.cloud), rid(T.rim),
                                                         int(T.ambient * 100), int(T.gain * 100), period, TW, TH)
    return head + tex.tobytes(), kind


def main():
    global TW, TH, P3
    args = list(sys.argv[1:])
    if "--scale" in args:
        i = args.index("--scale")
        G.K = int(args[i + 1])
        del args[i:i + 2]
        TW, TH = 64 * G.K, 32 * G.K
        P3 = sphere_points()
    data = json.load(open(args[0]))
    out = args[1]
    prev_dir = args[2] if len(args) > 2 else None
    if len(RAMP_NAMES) > 63:
        raise SystemExit("too many ramps")
    blob = bytearray(b"RTW1")
    blob += struct.pack("<H", len(G.PAL))
    for r, g_, b in G.PAL:
        blob += struct.pack("<BBB", r, g_, b)
    blob += struct.pack("<B", len(RAMP_NAMES))
    for n in RAMP_NAMES:
        ids = G.RI[n]
        blob += struct.pack("<B", len(n)) + n.encode() + struct.pack("<B", len(ids)) + b"".join(struct.pack("<H", int(i)) for i in ids)
    anims = BACKGROUNDS + SPRITES
    blob += struct.pack("<H", len(anims))
    firsts = []
    for name, fn in anims:
        enc, st = encode_anim(name, fn)
        blob += enc
        firsts.append((name, st))
        print("%-10s %3dx%-3d %6d bytes" % (name, st.shape[2], st.shape[1], len(enc)), flush=True)
    planets = [p for p in data["planets"]]
    blob += struct.pack("<H", len(planets))
    kinds = {}
    for p in planets:
        enc, kind = encode_planet(p)
        blob += enc
        kinds.setdefault(kind, []).append(p["name"])
    for k, v in sorted(kinds.items()):
        print("planets %-9s %2d: %s" % (k, len(v), ", ".join(v)))
    with open(out, "wb") as f:
        f.write(blob)
    print("wrote", out, len(blob), "bytes")
    if prev_dir:  # first frames, 3x, for a look
        from PIL import Image
        os.makedirs(prev_dir, exist_ok=True)
        for name, st in firsts:
            rgb = G.PAL_NP[st[0]].repeat(3, 0).repeat(3, 1)
            Image.fromarray(rgb, "RGB").save(os.path.join(prev_dir, name + ".png"))


if __name__ == "__main__":
    main()
