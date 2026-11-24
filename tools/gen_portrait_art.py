#!/usr/bin/env python3
"""Faction pictures for the unlock screen (0.83.0 drafts, owner 2026-11-23 18:12: Blue Reverie - "hi-def retrowave pixelated
versions" of the faction portraits Cacotopos made, for the unlock screen; then codec talking heads from them).

The owner's rule (2026-11-21 12:44): Blue Reverie unlocks get pixelated hi-def Retrowave versions of their art - not the art
itself. Each portrait is turned into OUR pixel art the way tools/gen_monument_anim.py turns the monument cards: cut to 4:5,
shrunk to ART_W x ART_H, graded toward the Retrowave look (violet / navy shadows, richer colour, a neon rim light along the
figure's edge), reduced to its own palette (k-means over all frames) with ordered (Bayer) dithering - stable from frame to frame,
so only what moves changes - and animated: N_FRAMES x FRAME_MS, a seamless loop (every effect periodic, integer frequencies).

Usage:  python3 tools/gen_portrait_art.py SRC_DIR OUTDIR [key,key,...]
        SRC_DIR holds the owner's pictures (Atokera.PNG, ...: his folder of Blue Reverie portrait references)
Out:    OUTDIR/<key>.gif (3x, for review), OUTDIR/<key>_strip.png (1x frames side by side), OUTDIR/<key>_compare.png
        (the picture | our frame 0 at 3x), OUTDIR/sheet.png (every portrait, frames 0 and N/2, 2x)
Deterministic.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage
from scipy.cluster.vq import kmeans2

ART_W, ART_H = 160, 200
N_FRAMES = 64
FRAME_MS = 100
TAU = 2.0 * math.pi
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float32) / 16.0 - 0.47
YY, XX = np.mgrid[0:ART_H, 0:ART_W].astype(np.float32)


# ------------------------------------------------------------------ the picture
def load(path):
    """the owner's picture cut to 4:5 (centred), area-averaged down to ART_W x ART_H"""
    im = Image.open(path).convert("RGB")
    w, h = im.size
    if w / h > ART_W / ART_H:
        nw = int(h * ART_W / ART_H)
        im = im.crop(((w - nw) // 2, 0, (w - nw) // 2 + nw, h))
    else:
        nh = int(w * ART_H / ART_W)
        im = im.crop((0, (h - nh) // 2, w, (h - nh) // 2 + nh))
    return np.asarray(im.resize((ART_W, ART_H), Image.BOX), np.float32), im


def lum(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def grade(a, sat=1.25, contrast=1.08, shadow=(22, 12, 52), shadow_amt=0.45, lift=0.0):
    """the Retrowave grade: richer colour, shadows pulled toward deep violet / navy"""
    l = lum(a)[..., None]
    a = l + (a - l) * sat
    a = (a - 128.0) * contrast + 128.0 + lift
    w = np.clip(1.0 - lum(np.clip(a, 0, 255)) / 120.0, 0, 1)[..., None] * shadow_amt
    a = a * (1 - w) + np.array(shadow, np.float32) * w
    return np.clip(a, 0, 255)


def hue_mask(a, want, tol=60.0, min_l=0.0):
    """how close each pixel's colour direction is to `want` (RGB), 0..1, for pixels brighter than min_l"""
    v = a / np.maximum(np.linalg.norm(a, axis=-1, keepdims=True), 1)
    w = np.array(want, np.float32)
    w = w / np.linalg.norm(w)
    ang = np.degrees(np.arccos(np.clip((v * w).sum(-1), -1, 1)))
    return np.clip(1 - ang / tol, 0, 1) * (lum(a) >= min_l)


def soft(m, s):
    return ndimage.gaussian_filter(m.astype(np.float32), s)


def add(img, mask, colour, amt=1.0):
    img += mask[..., None] * np.array(colour, np.float32) * amt


def radius(cx, cy, sx=1.0, sy=1.0):
    return np.sqrt(((XX - cx) / sx) ** 2 + ((YY - cy) / sy) ** 2)


def blob(cx, cy, r, sy=1.0):
    """a soft round light"""
    d = radius(cx, cy, 1.0, sy)
    return np.exp(-(d * d) / (2 * r * r))


def ring(cx, cy, rx, ry, width):
    d = np.abs(np.sqrt(((XX - cx) / max(rx, 0.01)) ** 2 + ((YY - cy) / max(ry, 0.01)) ** 2) - 1.0) * max(rx, ry)
    return np.exp(-(d * d) / (2 * width * width))


def angular(cx, cy, theta, width, squash=1.0):
    """light at angle theta (radians) round (cx, cy)"""
    a = np.arctan2((YY - cy) * squash, XX - cx)
    d = (a - theta + math.pi) % TAU - math.pi
    return np.exp(-(d * d) / (2 * width * width))


def wave(t, f=1, ph=0.0):
    """0..1, f times per loop"""
    return 0.5 + 0.5 * math.sin(TAU * f * t / N_FRAMES + ph)


def sweep(t, angle=0.6, width=7.0, f=1, start=0.0, span=1.0):
    """a band of light crossing the picture diagonally once per loop (f times); `span` = the part of the loop it takes
    (the rest of the loop it waits off the picture)"""
    s = ((t / N_FRAMES) * f + start) % 1.0
    if s > span:
        return np.zeros((ART_H, ART_W), np.float32)
    s /= span
    proj = XX * math.cos(angle) + YY * math.sin(angle)
    lo, hi = float(proj.min()) - 3 * width, float(proj.max()) + 3 * width
    c = lo + (hi - lo) * s
    d = proj - c
    return np.exp(-(d * d) / (2 * width * width)).astype(np.float32)


# ------------------------------------------------------------------ periodic building blocks
class Particles:
    """n particles travelling from (x, y_from) to (x + dx, y_to), each `loops` times per animation loop (integer, so the loop
    is seamless), fading in and out; sway = sideways wobble"""

    def __init__(self, rng, n, x0, x1, y_from, y_to, colour, loops=(1, 2), dx=(0, 0), sway=1.0, size=1, bright=(0.6, 1.0)):
        self.p = []
        for _ in range(n):
            self.p.append((rng.uniform(x0, x1), rng.uniform(0, 1), rng.randint(loops[0], loops[1] + 1), rng.uniform(dx[0], dx[1]),
                           rng.uniform(0, TAU), rng.uniform(bright[0], bright[1]), rng.randint(1, 3)))
        self.y_from, self.y_to, self.colour, self.sway, self.size = y_from, y_to, np.array(colour, np.float32), sway, size

    def draw(self, img, t, mask=None):
        for x, ph, loops, dx, wob, b, wf in self.p:
            s = (ph + loops * t / N_FRAMES) % 1.0
            y = self.y_from + (self.y_to - self.y_from) * s
            xx = x + dx * s + self.sway * math.sin(TAU * (wf * s) + wob)
            a = b * math.sin(math.pi * s) ** 0.6
            xi, yi = int(round(xx)), int(round(y))
            for oy in range(self.size):
                for ox in range(self.size):
                    X, Y = xi + ox, yi + oy
                    if 0 <= X < ART_W and 0 <= Y < ART_H:
                        m = 1.0 if mask is None else mask[Y, X]
                        img[Y, X] += self.colour * a * m


def stars(base, region, thr=26.0, min_l=60.0, rng=None):
    """small points brighter than their surroundings inside region: (ys, xs, excess, freq, phase)"""
    l = lum(base)
    local = ndimage.gaussian_filter(l, 1.6)
    pick = (l - local > thr) & (l > min_l) & (region > 0.5)
    ys, xs = np.nonzero(pick)
    ex = (l - local)[ys, xs]
    f = rng.randint(1, 4, size=len(ys))
    ph = rng.uniform(0, TAU, size=len(ys))
    return ys, xs, ex, f, ph


def twinkle(img, st, t, depth=0.8, tint=(1.0, 1.0, 1.0)):
    ys, xs, ex, f, ph = st
    k = 0.5 + 0.5 * np.sin(TAU * f * t / N_FRAMES + ph)  # 0..1
    img[ys, xs] -= ((1 - k) * depth * ex)[:, None] * np.array(tint, np.float32)


# ------------------------------------------------------------------ the figure, its rim light and the Retrowave sky
def figure_mask(cut):
    """the figure against the sky: OpenCV's GrabCut on the cut picture at 400 x 500 (the frame's edge seeds the background -
    every figure runs off the bottom), smoothed, then area-averaged down to the art: a soft
    0..1 mask with a clean outline"""
    import cv2
    im = np.asarray(cut.resize((400, 500), Image.LANCZOS))
    a = cv2.cvtColor(im, cv2.COLOR_RGB2BGR)
    m = np.zeros(a.shape[:2], np.uint8)
    bg, fg = np.zeros((1, 65), np.float64), np.zeros((1, 65), np.float64)
    cv2.setRNGSeed(1)
    # (a 2-px margin on the sides: Atokera's arm and Uydai's hood run off the frame's edges, and anything outside the rectangle
    # is taken as sky)
    cv2.grabCut(a, m, (2, 5, 396, 495), bg, fg, 6, cv2.GC_INIT_WITH_RECT)
    f = ((m == 1) | (m == 3)).astype(np.float32)
    lab, n = ndimage.label(f > 0.5)
    if n > 1:  # the figure only (specks of sky taken for it dropped)
        sizes = ndimage.sum(f > 0.5, lab, range(1, n + 1))
        f = (lab == (1 + int(np.argmax(sizes)))).astype(np.float32)
    f = ndimage.binary_fill_holes(f > 0.5).astype(np.float32)
    f = ndimage.gaussian_filter(f, 1.5)
    small = np.asarray(Image.fromarray((f * 255).astype(np.uint8)).resize((ART_W, ART_H), Image.BOX), np.float32) / 255.0
    return np.clip((small - 0.5) * 2.5 + 0.5, 0, 1)


def rim(mask, side, width=1.5):
    """the figure's edge facing `side` (+1 right, -1 left): how much brighter the mask is here than `width` px that way"""
    k = int(math.ceil(width))
    shifted = np.roll(mask, -side * k, axis=1)
    if side > 0:
        shifted[:, -k:] = 0
    else:
        shifted[:, :k] = 0
    e = np.clip(mask - shifted, 0, 1) * mask
    return np.clip(e * 1.4, 0, 1)


def sky_grade(img, fig, top=(30, 14, 70), bottom=(70, 18, 80), amt=0.35):
    """the sky behind the figure pulled toward a violet -> magenta Retrowave gradient"""
    k = (YY / (ART_H - 1))[..., None]
    col = np.array(top, np.float32) * (1 - k) + np.array(bottom, np.float32) * k
    w = ((1 - fig) * amt)[..., None]
    return img * (1 - w * 0.5) + col * w


# ------------------------------------------------------------------ palette + dithering
def make_palette(frames, k, seed):
    data = np.concatenate([f.reshape(-1, 3) for f in frames]).astype(np.float64)
    rng = np.random.RandomState(seed)
    idx = rng.choice(len(data), size=min(len(data), 80000), replace=False)
    cent, _ = kmeans2(data[idx], k, minit="++", seed=seed, iter=30)
    cent = np.clip(cent, 0, 255)
    dark = data[np.argmin(data.sum(1))]
    lite = data[np.argmax(data.sum(1))]
    pal = np.vstack([cent, dark, lite])
    return np.unique(np.round(pal), axis=0).astype(np.float32)


def quantize(img, pal, dither=12.0):
    bay = np.tile(BAYER, (ART_H // 4 + 1, ART_W // 4 + 1))[:ART_H, :ART_W]
    d = np.clip(img + bay[..., None] * dither, 0, 255)
    out = np.empty((ART_H, ART_W), np.uint8)
    for y0 in range(0, ART_H, 50):  # (in bands: the distance table is big)
        dd = d[y0:y0 + 50]
        dist = ((dd[:, :, None, :] - pal[None, None, :, :]) ** 2).sum(-1)
        out[y0:y0 + 50] = np.argmin(dist, axis=2)
    return out


# ------------------------------------------------------------------ the portraits
PORTRAITS = {}


def portrait(key, src, palette=56, dither=11.0, **grade_args):
    def deco(fn):
        PORTRAITS[key] = dict(src=src, fn=fn, palette=palette, dither=dither, grade=grade_args)
        return fn
    return deco


class Common:
    """what every portrait shares: the figure mask, the rim lights, the sky grade and the twinkling stars"""

    def __init__(self, base, fig, rng, rim_r, rim_l, rim_r_amt=0.85, rim_l_amt=0.4, glow=None, glow_amt=0.22, sky=None,
                 star_thr=24.0):
        self.fig = fig
        self.figs = fig
        self.rim_r, self.rim_l = rim(fig, 1), rim(fig, -1)
        self.rim_r_col, self.rim_l_col = rim_r, rim_l
        self.rim_r_amt, self.rim_l_amt = rim_r_amt, rim_l_amt
        # the backlight: a soft neon glow round the silhouette, in the sky only
        self.glow = soft(fig, 5.0) * (1 - fig)
        self.glow = self.glow / max(float(self.glow.max()), 1e-6)
        self.glow_col, self.glow_amt = glow or rim_r, glow_amt
        self.sky = sky or {}
        self.stars = stars(base, (1 - soft(fig, 1.5)) > 0.9, thr=star_thr, rng=rng)

    def apply(self, img, t, rim_pulse=0.15):
        img = sky_grade(img, self.figs, **self.sky)
        add(img, self.glow, self.glow_col, self.glow_amt * (0.85 + 0.15 * wave(t, 1)))
        k = 1 - rim_pulse + rim_pulse * wave(t, 1)
        add(img, self.rim_r, self.rim_r_col, self.rim_r_amt * k)
        add(img, self.rim_l, self.rim_l_col, self.rim_l_amt * (1 - rim_pulse + rim_pulse * wave(t, 1, math.pi)))
        twinkle(img, self.stars, t)
        return img


@portrait("atokera", "Atokera.PNG", palette=60, dither=10.0, sat=1.2, shadow=(18, 16, 48), shadow_amt=0.4)
def atokera(base, fig, rng):
    """the gold-armoured Atokera: the teal visor breathes, light runs over the gold, gold flecks drift off the armour"""
    c = Common(base, fig, rng, (60, 230, 255), (255, 70, 200), glow=(40, 150, 255))
    gold = hue_mask(base, (230, 170, 40), tol=28, min_l=70) * c.figs
    visor = soft(hue_mask(base, (40, 170, 170), tol=35, min_l=40) * ((XX > 70) & (XX < 120) & (YY > 10) & (YY < 45)), 0.6)
    flecks = Particles(rng, 22, 20, 140, 150, 20, (255, 200, 90), loops=(1, 2), dx=(-8, 12), sway=1.5, bright=(0.4, 0.9))
    motes = Particles(rng, 10, 110, 158, 190, 30, (140, 230, 255), loops=(1, 1), dx=(-6, 6), sway=1.0, bright=(0.3, 0.6))

    def frame(t):
        img = c.apply(base.copy(), t)
        add(img, visor, (60, 255, 230), 0.55 + 0.45 * wave(t, 2))
        add(img, soft(visor, 2.5), (40, 200, 255), 0.25 * wave(t, 2))
        add(img, gold * sweep(t, angle=0.9, width=6.0, span=0.55), (255, 240, 190), 0.75)
        flecks.draw(img, t)
        motes.draw(img, t, mask=1 - c.figs)
        return img
    return frame


@portrait("belkosea", "Belkosea.PNG", palette=60, dither=10.0, sat=1.15, shadow=(16, 18, 46), shadow_amt=0.45)
def belkosea(base, fig, rng):
    """the Belkosea officer: snow falls past the city, its windows blink, a cold light crosses the white uniform"""
    c = Common(base, fig, rng, (90, 220, 255), (255, 90, 190), rim_r_amt=0.7, rim_l_amt=0.4, glow=(60, 170, 255),
               sky=dict(top=(14, 20, 60), bottom=(60, 20, 80), amt=0.4))
    white = (lum(base) > 150) * c.figs
    city = (1 - soft(c.fig, 1.5)) * ((XX > 120) | (XX < 45))
    windows = np.zeros((ART_H, ART_W), np.float32)
    ys, xs = np.nonzero((lum(base) - ndimage.gaussian_filter(lum(base), 1.5) > 14) & (city > 0.6))
    for y, x in zip(ys, xs):
        windows[y, x] = 1
    lab, n = ndimage.label(windows > 0)
    rate = rng.randint(1, 4, size=n + 1)
    ph = rng.uniform(0, 1, size=n + 1)
    snow = Particles(rng, 40, 0, 160, -5, 205, (220, 235, 255), loops=(1, 2), dx=(-10, -4), sway=1.2, bright=(0.35, 0.8))
    eyes = blob(88, 43, 1.3) + blob(106, 43, 1.3)

    def frame(t):
        img = c.apply(base.copy(), t)
        k = ((rate * t / N_FRAMES + ph) % 1.0 < 0.55).astype(np.float32)
        on = k[lab] * (lab > 0)
        add(img, soft(on, 0.5) * city, (255, 190, 120), 0.6)
        add(img, white * sweep(t, angle=1.1, width=8.0, span=0.5, start=0.35), (170, 230, 255), 0.35)
        add(img, eyes, (150, 230, 255), 0.2 + 0.2 * wave(t, 3))
        snow.draw(img, t, mask=1 - c.figs * 0.85)
        return img
    return frame


@portrait("pharadn", "Pharadn.PNG", palette=56, dither=10.0, sat=1.25, shadow=(24, 12, 44), shadow_amt=0.45)
def pharadn(base, fig, rng):
    """the Pharadn priest: light travels round the golden halo, the bronze collar glints, ash drifts down"""
    c = Common(base, fig, rng, (255, 120, 220), (90, 200, 255), rim_r_amt=0.7, rim_l_amt=0.4, glow=(255, 140, 60))
    halo = ring(97, 47, 51, 45, 2.4) * hue_mask(base, (200, 140, 60), tol=40, min_l=50)
    collar = hue_mask(base, (200, 150, 70), tol=35, min_l=60) * ((YY > 90) & (YY < 135))
    socket = blob(93, 57, 2.2, 1.4)
    ash = Particles(rng, 26, 0, 160, -5, 205, (230, 215, 190), loops=(1, 1), dx=(-6, 6), sway=2.0, bright=(0.25, 0.6))
    embers = Particles(rng, 10, 50, 145, 95, 0, (255, 170, 80), loops=(1, 2), dx=(-5, 5), sway=1.5, bright=(0.4, 0.8))

    def frame(t):
        img = c.apply(base.copy(), t)
        th = TAU * t / N_FRAMES
        add(img, halo * (angular(97, 47, th, 0.55, 51 / 45) + angular(97, 47, th + math.pi, 0.35, 51 / 45) * 0.5),
            (255, 235, 160), 1.1)
        add(img, soft(ring(97, 47, 51, 45, 2.4), 2.0), (255, 150, 60), 0.12 + 0.08 * wave(t, 2))
        add(img, collar * sweep(t, angle=0.2, width=5.0, span=0.4, start=0.5), (255, 240, 200), 0.8)
        add(img, socket, (120, 220, 255), 0.35 * wave(t, 1, 1.0) ** 2)
        ash.draw(img, t, mask=1 - c.figs * 0.6)
        embers.draw(img, t)
        return img
    return frame


@portrait("qhet", "Qhet.PNG", palette=60, dither=10.0, sat=1.3, shadow=(14, 12, 40), shadow_amt=0.4)
def qhet(base, fig, rng):
    """the Qhet envoy: the visor eyes cycle cyan -> magenta, light runs along the gold trim, the chest lights glow"""
    c = Common(base, fig, rng, (255, 80, 210), (70, 210, 255), glow=(200, 60, 255))
    eyes = soft((blob(65, 33, 3.0, 0.7) + blob(83, 33, 3.0, 0.7)), 0.5)
    trim = hue_mask(base, (220, 170, 50), tol=30, min_l=60) * c.figs
    lights = blob(15, 182, 4.0) + blob(135, 178, 4.0)
    phones = blob(52, 28, 4.0) + blob(100, 28, 4.0)
    motes = Particles(rng, 14, 0, 160, 200, 0, (150, 210, 255), loops=(1, 1), dx=(-4, 4), sway=0.8, bright=(0.3, 0.6))

    def frame(t):
        img = c.apply(base.copy(), t)
        k = wave(t, 1)
        col = (70 + 185 * (1 - k), 200 * k + 60 * (1 - k), 255)
        add(img, eyes, col, 0.8)
        add(img, soft(eyes, 3.0), col, 0.3)
        add(img, trim * sweep(t, angle=1.35, width=9.0, span=0.6), (255, 245, 170), 0.9)
        add(img, lights, (255, 120, 60), 0.25 + 0.25 * wave(t, 3))
        add(img, phones, (120, 220, 255), 0.12 * wave(t, 2, 0.5))
        motes.draw(img, t, mask=1 - c.figs)
        return img
    return frame


@portrait("uydai", "Uydai.PNG", palette=56, dither=10.0, sat=1.2, shadow=(18, 14, 46), shadow_amt=0.42)
def uydai(base, fig, rng):
    """the hooded Uydai: a glint crosses the goggle lens, the eyes and the blue markings glow, dust drifts in the dark"""
    c = Common(base, fig, rng, (90, 220, 255), (255, 110, 200), glow=(255, 170, 70))
    lens = (radius(80, 60, 1.0, 0.95) < 15.5).astype(np.float32)
    eyes = blob(62, 88, 2.0) + blob(103, 88, 2.0)
    marks = hue_mask(base, (40, 170, 230), tol=30, min_l=60) * c.figs * (YY > 70)
    dust = Particles(rng, 24, 0, 160, 200, -5, (200, 220, 255), loops=(1, 1), dx=(-8, 8), sway=1.5, bright=(0.25, 0.55))

    def frame(t):
        img = c.apply(base.copy(), t)
        add(img, lens * sweep(t, angle=-0.7, width=2.5, span=0.3, start=0.1), (230, 250, 255), 1.0)
        add(img, soft(lens, 1.0) * (lens < 0.5) * ring(80, 60, 15.5, 14.7, 1.0), (120, 230, 255), 0.3 * wave(t, 1))
        add(img, eyes, (120, 240, 255), 0.5 + 0.4 * wave(t, 2))
        add(img, soft(eyes, 2.0), (60, 200, 255), 0.25 * wave(t, 2))
        add(img, marks, (60, 200, 255), 0.18 * wave(t, 1, 2.0))
        dust.draw(img, t, mask=1 - c.figs * 0.7)
        return img
    return frame


# ------------------------------------------------------------------ build
def build(key, src_dir, out):
    spec = PORTRAITS[key]
    rng = np.random.RandomState(abs(hash(key)) % (2 ** 31) if False else sum(map(ord, key)) * 7919)
    base, cut = load(os.path.join(src_dir, spec["src"]))
    fig = figure_mask(cut)
    base = grade(base, **spec["grade"])
    fn = spec["fn"](base, fig, rng)
    frames = [np.clip(fn(t), 0, 255) for t in range(N_FRAMES)]
    pal = make_palette(frames, spec["palette"], seed=sum(map(ord, key)))
    idx = [quantize(f, pal, spec["dither"]) for f in frames]
    pal8 = pal.astype(np.uint8)
    rgb = [pal8[i] for i in idx]
    os.makedirs(out, exist_ok=True)
    gif = [Image.fromarray(r).resize((ART_W * 3, ART_H * 3), Image.NEAREST) for r in rgb]
    gif[0].save(os.path.join(out, key + ".gif"), save_all=True, append_images=gif[1:], duration=FRAME_MS, loop=0, disposal=1)
    strip = Image.new("RGB", (ART_W * N_FRAMES, ART_H))
    for i, r in enumerate(rgb):
        strip.paste(Image.fromarray(r), (i * ART_W, 0))
    strip.save(os.path.join(out, key + "_strip.png"))
    src = cut.resize((ART_W * 3, ART_H * 3), Image.LANCZOS)
    cmp_ = Image.new("RGB", (ART_W * 6 + 10, ART_H * 3), (20, 20, 26))
    cmp_.paste(src, (0, 0))
    cmp_.paste(gif[0], (ART_W * 3 + 10, 0))
    cmp_.save(os.path.join(out, key + "_compare.png"))
    print(f"{key}: {len(pal)} colours, {N_FRAMES} frames")
    return rgb


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src_dir, out = sys.argv[1], sys.argv[2]
    keys = sys.argv[3].split(",") if len(sys.argv) > 3 else list(PORTRAITS)
    done = {k: build(k, src_dir, out) for k in keys}
    # the review sheet: frames 0 and N/2 of each, 2x, with the key under them
    cols = len(done)
    sheet = Image.new("RGB", (cols * (ART_W * 2 * 2 + 30) + 10, ART_H * 2 + 40), (16, 14, 24))
    d = ImageDraw.Draw(sheet)
    for i, (k, rgb) in enumerate(done.items()):
        x = 10 + i * (ART_W * 4 + 30)
        for j, fr in enumerate((0, N_FRAMES // 2)):
            sheet.paste(Image.fromarray(rgb[fr]).resize((ART_W * 2, ART_H * 2), Image.NEAREST), (x + j * (ART_W * 2 + 6), 10))
        d.text((x, ART_H * 2 + 18), k, fill=(230, 220, 255))
    sheet.save(os.path.join(out, "sheet.png"))


if __name__ == "__main__":
    main()
