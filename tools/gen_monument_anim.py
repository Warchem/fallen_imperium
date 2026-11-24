#!/usr/bin/env python3
"""Monuments+ animated card art (0.82.0, owner 2026-11-21 18:03): "Make the art for each monument animated" -
"convert the ones in the rulebook into animated pictures (we can go over them together)".

Each monument's picture (Cacotopos's art on the Monuments+ cards) is turned into OUR pixel art: cut from the card,
shrunk to ART_W x ART_H, graded toward the Retrowave look (deeper violet / navy shadows, richer colour), reduced
to its own small palette (k-means over all the animation frames) with ordered (Bayer) dithering - stable from frame
to frame, so only what moves changes - and animated: N_FRAMES frames x FRAME_MS, a seamless loop. Every effect
is periodic in the loop (integer frequencies), so frame N would equal frame 0.

The effects are built per monument from simple parts: glows that pulse, light that flows along a beam, packets
running down a beam, ripples, flickering fire (periodic noise drifting upward), embers / snow / sparks
(particles that wrap), twinkling stars, windows that blink, a glint sweeping across, heat haze.

Usage:  python3 tools/gen_monument_anim.py CARDS_DIR OUTDIR [key,key,...]
        CARDS_DIR = docs/owner/monuments/art_src (the cards' picture areas, kept in the repository) or the
        Monuments+ package's "Monuments" folder (Base+PoK/Arborec.jpg, ..., TE/Keleres.jpg)
Out:    OUTDIR/<key>.gif (3x, for review), OUTDIR/<key>_strip.png (1x frames side by side),
        OUTDIR/<key>_compare.png (the card's picture | our frame 0 at 3x)
Deterministic.
"""
import math
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage
from scipy.cluster.vq import kmeans2

ART_W, ART_H = 160, 120
N_FRAMES = 64
FRAME_MS = 100
TAU = 2.0 * math.pi
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float32) / 16.0 - 0.47
YY, XX = np.mgrid[0:ART_H, 0:ART_W].astype(np.float32)


# ------------------------------------------------------------------ the picture
def card_crop(path, box):
    """box = (x0, y0, x1, y1) on the 1764 x 1200 card. The repository keeps only each card's picture area,
    0..700 x 320..920 (docs/owner/monuments/art_src, 700 x 600); either kind of file works."""
    im = Image.open(path).convert("RGB")
    if im.size == (700, 600):
        box = (box[0], box[1] - 320, box[2], box[3] - 320)
    return im.crop(box)


def load_art(path, box):
    """the cut, area-averaged down to ART_W x ART_H"""
    im = card_crop(path, box)
    im = im.resize((ART_W * 4, ART_H * 4), Image.LANCZOS).resize((ART_W, ART_H), Image.BOX)
    return np.asarray(im, dtype=np.float32)


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


def box(x0, y0, x1, y1, feather=3.0):
    m = ((XX >= x0) & (XX < x1) & (YY >= y0) & (YY < y1)).astype(np.float32)
    return soft(m, feather) if feather > 0 else m


# ------------------------------------------------------------------ periodic building blocks
class Flow:
    """smooth random field (blurred noise, wrapping at the picture's edges) drifting at a steady speed: vx / vy =
    whole picture widths / heights travelled per loop (integers, so the loop is seamless; vy < 0 = upward).
    sigma = feature size in pixels; aniso stretches it (x, y). Values: mean 0, sd 1."""

    def __init__(self, rng, sigma, vx=0, vy=0, aniso=(1.0, 1.0)):
        f = rng.normal(size=(ART_H, ART_W))
        f = ndimage.gaussian_filter(f, (sigma * aniso[1], sigma * aniso[0]), mode="wrap")
        self.f = (f / f.std()).astype(np.float32)
        self.vx, self.vy = vx, vy

    def __call__(self, t):
        ox = self.vx * ART_W * t / N_FRAMES
        oy = self.vy * ART_H * t / N_FRAMES
        ix, fx = int(math.floor(ox)), ox - math.floor(ox)
        iy, fy = int(math.floor(oy)), oy - math.floor(oy)
        a = np.roll(np.roll(self.f, iy, 0), ix, 1)  # f(x - ox, y - oy)
        b = np.roll(a, 1, 1)
        c = np.roll(a, 1, 0)
        d = np.roll(b, 1, 0)
        return a * (1 - fx) * (1 - fy) + b * fx * (1 - fy) + c * (1 - fx) * fy + d * fx * fy


class Particles:
    """n particles travelling from (x, y_from) to (x + dx, y_to), each `loops` times per animation loop (integer, so
    the loop is seamless), fading in and out; sway = sideways wobble"""

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


def stars(base, region, thr=30.0, min_l=70.0, rng=None):
    """small points brighter than their surroundings inside region: (ys, xs, excess, freq, phase)"""
    l = lum(base)
    local = ndimage.gaussian_filter(l, 1.6)
    pick = (l - local > thr) & (l > min_l) & (region > 0.5)
    ys, xs = np.nonzero(pick)
    ex = (l - local)[ys, xs]
    f = rng.randint(1, 4, size=len(ys))
    ph = rng.uniform(0, TAU, size=len(ys))
    return ys, xs, ex, f, ph


def twinkle(img, st, t, depth=0.75):
    ys, xs, ex, f, ph = st
    k = 0.5 + 0.5 * np.sin(TAU * f * t / N_FRAMES + ph)  # 0..1
    img[ys, xs] -= ((1 - k) * depth * ex)[:, None] * np.array([1, 1, 1], np.float32)


def ring(cx, cy, rx, ry, width):
    d = np.abs(np.sqrt(((XX - cx) / max(rx, 0.01)) ** 2 + ((YY - cy) / max(ry, 0.01)) ** 2) - 1.0) * max(rx, ry)
    return np.exp(-(d * d) / (2 * width * width))


def shift_rows(img, mask, dx):
    """heat haze: each row moved by dx[y] (fractional, linear), blended in by mask"""
    out = img.copy()
    x = XX - dx[:, None]
    x0 = np.clip(np.floor(x).astype(int), 0, ART_W - 1)
    x1 = np.clip(x0 + 1, 0, ART_W - 1)
    fr = (x - np.floor(x))[..., None]
    rows = np.arange(ART_H)[:, None]
    moved = img[rows, x0] * (1 - fr) + img[rows, x1] * fr
    m = mask[..., None]
    return out * (1 - m) + moved * m


def add(img, mask, colour, amt=1.0):
    img += mask[..., None] * np.array(colour, np.float32) * amt


# ------------------------------------------------------------------ more building blocks
CROP = (0, 345, 680, 855)  # the usual cut: the card's picture minus a strip at the top and bottom (4:3)


def S(sx, sy, crop=CROP):
    """a spot on the planning sheets (the card's picture 0..700 x 320..920 at half size) -> art pixels for crop"""
    k = (crop[2] - crop[0]) / ART_W
    return (sx * 2 - crop[0]) / k, (320 + sy * 2 - crop[1]) / k


def wave(t, f=1, ph=0.0):
    """0..1, f times per loop"""
    return 0.5 + 0.5 * math.sin(TAU * f * t / N_FRAMES + ph)


def angular(cx, cy, theta, width, squash=1.0):
    """light at angle theta (radians) round (cx, cy); squash > 1 for an ellipse flattened by that much"""
    a = np.arctan2((YY - cy) * squash, XX - cx)
    d = (a - theta + math.pi) % TAU - math.pi
    return np.exp(-(d * d) / (2 * width * width))


def radius(cx, cy, sx=1.0, sy=1.0):
    return np.sqrt(((XX - cx) / sx) ** 2 + ((YY - cy) / sy) ** 2)


def flicker(img, mask, n, amp=0.25, hot=None, hot_amt=0.25, knee=0.3):
    """brightness varied by a field n (sd 1) inside mask; the brightest tongues tinted `hot`"""
    img *= (1 + mask * amp * n)[..., None]
    if hot is not None:
        add(img, mask * np.clip(n - knee, 0, None), hot, hot_amt)


def line_mask(pts, width=0.7):
    """a soft polyline (art pixels)"""
    m = np.zeros((ART_H, ART_W), np.float32)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(steps + 1):
            x = x0 + (x1 - x0) * i / steps
            y = y0 + (y1 - y0) * i / steps
            xi, yi = int(round(x)), int(round(y))
            if 0 <= xi < ART_W and 0 <= yi < ART_H:
                m[yi, xi] = 1
    return np.clip(soft(m, width) * 2.2, 0, 1)


def bolt(rng, x0, y0, x1, y1, jag=3.0, segs=9, branch=True):
    """a lightning bolt from (x0, y0) to (x1, y1) as a mask"""
    pts = [(x0, y0)]
    for i in range(1, segs):
        f = i / segs
        pts.append((x0 + (x1 - x0) * f + rng.uniform(-jag, jag), y0 + (y1 - y0) * f + rng.uniform(-jag * 0.4, jag * 0.4)))
    pts.append((x1, y1))
    m = line_mask(pts, 0.55)
    m = np.maximum(m, soft(m, 1.6) * 0.5)  # the glow round it
    if branch:
        k = rng.randint(2, segs - 2)
        bx, by = pts[k]
        m = np.maximum(m, line_mask([(bx, by), (bx + rng.uniform(-10, 10), by + rng.uniform(5, 12))], 0.5) * 0.7)
    return m


class Blinkers:
    """lights (components of mask > thr) that each keep their own rhythm: steady, blinking or pulsing"""

    def __init__(self, rng, mask, thr=0.35, steady=0.45, blink=0.3, low=0.25, fmax=4):
        self.lab, n = ndimage.label(mask > thr)
        self.soft = soft(mask, 0.6)
        self.modes = []
        for _ in range(n):
            r = rng.rand()
            self.modes.append((0 if r < steady else 1 if r < steady + blink else 2, rng.randint(1, fmax + 1),
                               rng.uniform(0, 1), rng.uniform(0.3, 0.7)))
        self.low = low

    def apply(self, img, t, glow=None, glow_amt=0.12):
        k = np.ones(len(self.modes) + 1, np.float32)
        for i, (mode, f, p, duty) in enumerate(self.modes, start=1):
            s = (f * t / N_FRAMES + p) % 1.0
            if mode == 1:
                k[i] = 1.0 if s < duty else self.low
            elif mode == 2:
                k[i] = 0.55 + 0.45 * math.sin(TAU * s)
        w = k[self.lab]
        img *= (1 - self.soft * (1 - w) * 0.85)[..., None]
        if glow is not None:
            add(img, self.soft * (w > 0.99), glow, glow_amt)


def flash(t, at, length=3):
    """a lightning flash: 1, .45, .2 over `length` frames from frame `at`"""
    d = (t - at) % N_FRAMES
    return (1.0, 0.45, 0.2, 0.08)[d] if d < min(length, 4) else 0.0


# ------------------------------------------------------------------ palette + dithering
def make_palette(frames, k, seed):
    data = np.concatenate([f.reshape(-1, 3) for f in frames]).astype(np.float64)
    rng = np.random.RandomState(seed)
    idx = rng.choice(len(data), size=min(len(data), 60000), replace=False)
    cent, _ = kmeans2(data[idx], k, minit="++", seed=seed, iter=30)
    cent = np.clip(cent, 0, 255)
    # keep the extremes: the darkest and brightest pixels always find a match
    dark = data[np.argmin(data.sum(1))]
    lite = data[np.argmax(data.sum(1))]
    pal = np.vstack([cent, dark, lite])
    return np.unique(np.round(pal), axis=0).astype(np.float32)


def quantize(img, pal, dither=16.0):
    bay = np.tile(BAYER, (ART_H // 4 + 1, ART_W // 4 + 1))[:ART_H, :ART_W]
    d = np.clip(img + bay[..., None] * dither, 0, 255)
    dist = ((d[:, :, None, :] - pal[None, None, :, :]) ** 2).sum(-1)
    return np.argmin(dist, axis=2).astype(np.uint8)


# ------------------------------------------------------------------ the monuments
MONUMENTS = {}


def monument(key, card, crop, palette=40, dither=16.0, **grade_args):
    def deco(fn):
        MONUMENTS[key] = dict(card=card, crop=crop, fn=fn, palette=palette, dither=dither, grade=grade_args)
        return fn
    return deco


@monument("creuss", "Base+PoK/Creuss.jpg", (12, 392, 692, 902), sat=1.2, shadow=(14, 14, 50))
def creuss(base, rng):
    """Revenance Circuit - "a kaleidoscope of dimensional fire": rings of gates on a dark plane, a beam of light
    through them that burns orange below. Moves: packets of light running down the beam, the gate under the beam
    breathing, ripples spreading over the plane, the fire flickering upward with embers, drops of light sliding
    down the far streaks, stars twinkling."""
    l = lum(base)
    # the beam: the brightest thin column of the upper half
    col = l[: ART_H // 2].mean(0)
    bx = float(np.argmax(ndimage.uniform_filter1d(col, 3)))
    beam = np.exp(-((XX - bx) ** 2) / (2 * 0.9 ** 2)) * (YY < ART_H * 0.97)
    # the gate under the beam: brightest blob near the beam in the middle band
    band = l * box(bx - 30, ART_H * 0.15, bx + 30, ART_H * 0.6, 0)
    cy = float(np.unravel_index(np.argmax(ndimage.gaussian_filter(band, 3)), band.shape)[0])
    core = np.exp(-(((XX - bx) / 14) ** 2 + ((YY - cy) / 4.5) ** 2))
    fire = hue_mask(base, (255, 110, 30), 40, 60) * box(0, ART_H * 0.55, ART_W, ART_H, 4)
    fire = soft(fire, 1.2)
    plane = box(0, cy - 6, ART_W, ART_H * 0.8, 5) * (1 - soft(fire, 3) * 1.5).clip(0, 1)
    f1 = Flow(rng, 3.0, vy=-2, aniso=(1.0, 1.8))
    f2 = Flow(rng, 1.6, vy=-3, aniso=(1.0, 1.5))
    embers = Particles(rng, 26, bx - 26, bx + 26, ART_H * 0.98, ART_H * 0.55, (255, 160, 60), loops=(1, 3), sway=1.5)
    # far streaks: thin bright vertical lines in the top third
    top = l[: int(ART_H * 0.3)]
    tl = top.mean(0) - ndimage.uniform_filter1d(top.mean(0), 9)
    streak_x = [x for x in range(2, ART_W - 2) if tl[x] > 6 and abs(x - bx) > 4 and tl[x] == tl[max(0, x - 2): x + 3].max()]
    drops = [(x, rng.uniform(0, 1), rng.randint(1, 3)) for x in streak_x]
    st = stars(base, box(0, 0, ART_W, ART_H * 0.45, 0) * (np.abs(XX - bx) > 3), rng=rng)

    def frame(t):
        img = base.copy()
        ph = TAU * t / N_FRAMES
        # packets down the beam (2 per loop, then 3 smaller)
        pk = np.zeros_like(l)
        for n, w, a in ((2, 5.0, 1.0), (3, 2.5, 0.6)):
            for j in range(n):
                y = ((t / N_FRAMES) * n + j / n) % 1.0 * ART_H * 1.1 - 6
                pk += a * np.exp(-((YY - y) ** 2) / (2 * w * w))
        add(img, beam * pk, (150, 230, 255), 0.9)
        add(img, beam, (120, 200, 255), 0.25 + 0.15 * math.sin(ph * 4))
        # the gate breathing
        add(img, core, (90, 180, 255), 0.35 + 0.3 * math.sin(ph * 2))
        # ripples over the plane: 3 rings growing from the gate
        rp = np.zeros_like(l)
        for j in range(3):
            s = (t / N_FRAMES + j / 3) % 1.0
            rp += ring(bx, cy, 8 + s * ART_W * 0.75, 2.2 + s * ART_H * 0.42, 0.9 + s * 1.2) * (1 - s) ** 1.3
        add(img, rp * plane, (70, 170, 255), 0.55)
        # fire: rising tongues of brighter / darker flame
        n = 0.65 * f1(t) + 0.35 * f2(t)
        img *= (1 + fire * 0.22 * n)[..., None]
        add(img, fire * np.clip(n - 0.3, 0, None), (255, 200, 110), 0.22)
        embers.draw(img, t)
        # drops sliding down the streaks
        for x, p0, loops in drops:
            y = ((p0 + loops * t / N_FRAMES) % 1.0) * ART_H * 0.5
            add(img, np.exp(-((XX - x) ** 2) / 0.5 - ((YY - y) ** 2) / 6.0), (160, 210, 255), 0.9)
        twinkle(img, st, t)
        return img

    return frame


@monument("muaat", "Base+PoK/Muaat.jpg", (0, 330, 680, 840), sat=1.0, contrast=1.12, shadow=(26, 8, 34), shadow_amt=0.55)
def muaat(base, rng):
    """Glory Furnace - a ring-shaped forge firing a beam of fire through its heart. Moves: fire flowing along the
    beam, the ring's rim of fire flickering, the heart pulsing, heat haze above the beam, sparks thrown off."""
    l = lum(base)
    hot = np.clip((l - 120) / 90, 0, 1) * hue_mask(base, (255, 130, 50), 45)
    # hand-placed: the beam runs from (0, 52) to (160, 82) through the heart at (91, 69); the ring round it
    yline = 52 + XX * (30.0 / 160.0)
    beam = np.exp(-((YY - yline) ** 2) / (2 * 4.0 ** 2)) * np.maximum(hot, 0.35)
    cx, cy = 92.0, 68.0
    ringm = ring(cx, cy, 24, 31, 3.0) * np.maximum(hot, 0.25)
    core = np.exp(-(((XX - cx) / 9) ** 2 + ((YY - cy) / 11) ** 2))
    flow1 = Flow(rng, 3.0, vx=3, aniso=(2.5, 1.0))
    flow2 = Flow(rng, 1.8, vx=4, aniso=(2.0, 1.0))
    rim1 = Flow(rng, 1.8, vy=-2)
    haze_m = soft(np.exp(-((YY - (yline - 10)) ** 2) / (2 * 6.0 ** 2)), 1) * 0.9
    sparks = Particles(rng, 20, cx - 6, cx + 6, cy - 22, cy - 50, (255, 180, 70), loops=(1, 3), dx=(-34, 34), sway=1.5)
    sparks2 = Particles(rng, 14, cx - 6, cx + 6, cy + 22, cy + 48, (255, 130, 40), loops=(1, 2), dx=(-28, 28), sway=1.5)

    def frame(t):
        ph = TAU * t / N_FRAMES
        rows = np.arange(ART_H, dtype=np.float32)
        img = shift_rows(base, haze_m, 0.8 * np.sin(TAU * (rows / 7.0) + ph * 6))
        n = 0.6 * flow1(t) + 0.4 * flow2(t)
        img *= (1 + beam * 0.25 * n)[..., None]
        add(img, beam * np.clip(n - 0.2, 0, None), (255, 230, 160), 0.35)
        r = rim1(t)
        img *= (1 + ringm * 0.3 * r)[..., None]
        add(img, ringm * np.clip(r - 0.4, 0, None), (255, 200, 90), 0.3)
        add(img, core, (255, 170, 80), 0.18 + 0.16 * math.sin(ph * 3))
        sparks.draw(img, t)
        sparks2.draw(img, t)
        return img

    return frame


@monument("l1z1x", "Base+PoK/L1Z1X.jpg", (0, 330, 680, 840), sat=1.2, shadow=(12, 18, 40))
def l1z1x(base, rng):
    """[0.0.2] - a node city of black towers lit in orange. Moves: windows blinking on and off in their own rhythms,
    data pulses climbing the tower edges, mist drifting through the gaps between the towers."""
    l = lum(base)
    lit = hue_mask(base, (255, 130, 50), 42, 70)
    lab, n = ndimage.label(lit > 0.35)
    # per window: a mode (steady / blink / pulse), frequency, phase, share of the time on
    modes = []
    for _ in range(n):
        r = rng.rand()
        modes.append((0 if r < 0.45 else 1 if r < 0.75 else 2, rng.randint(1, 5), rng.uniform(0, TAU), rng.uniform(0.3, 0.7)))
    lit_s = soft(lit, 0.6)
    # tower edges: long vertical runs where a dark tower meets something lighter; each gets its own pulse
    edge = np.clip(np.abs(ndimage.sobel(l, axis=1)) / 120.0, 0, 1)
    long_ = (ndimage.uniform_filter(edge, size=(15, 1)) > 0.42) & (edge > 0.3)
    elab, en = ndimage.label(long_, structure=np.ones((3, 3)))
    sizes = ndimage.sum(long_, elab, range(1, en + 1))
    keep = [i + 1 for i in np.argsort(sizes)[::-1][:7] if sizes[i] >= 14]
    edges = []
    for i in keep:
        ys, _ = np.nonzero(elab == i)
        edges.append((soft(elab == i, 0.5), float(ys.min()), float(ys.max()), rng.uniform(0, 1), rng.randint(1, 3)))
    # the gaps between the towers: the teal sky
    sky = soft(hue_mask(base, (90, 150, 160), 30, 70) > 0.3, 1.5)
    m1 = Flow(rng, 6.0, vx=1, aniso=(2.0, 1.0))
    m2 = Flow(rng, 3.5, vx=2, vy=-1, aniso=(1.6, 1.0))

    def frame(t):
        img = base.copy()
        k = np.ones(n + 1, np.float32)
        for i, (mode, f, p, duty) in enumerate(modes, start=1):
            s = (f * t / N_FRAMES + p / TAU) % 1.0
            if mode == 1:
                k[i] = 1.0 if s < duty else 0.25
            elif mode == 2:
                k[i] = 0.55 + 0.45 * math.sin(TAU * s)
        win = k[lab]
        img = img * (1 - (lit_s * (1 - win) * 0.85)[..., None])
        # data pulses: a short light climbing each long edge, each in its own rhythm
        for m_, y0, y1, p0, loops in edges:
            s = (p0 + loops * t / N_FRAMES) % 1.0
            y = y1 + 4 - s * (y1 - y0 + 8)
            add(img, m_ * np.exp(-((YY - y) ** 2) / 8.0), (120, 240, 255), 0.85 * math.sin(math.pi * s) ** 0.5)
        # mist through the gaps
        m = 0.6 * m1(t) + 0.4 * m2(t)
        add(img, sky * np.clip(0.35 + 0.35 * m, 0, 1) * box(0, ART_H * 0.35, ART_W, ART_H, 10), (150, 200, 200), 0.35)
        return img

    return frame


@monument("naalu", "Base+PoK/Naalu.jpg", (20, 392, 700, 902), sat=1.15, shadow=(16, 18, 60), shadow_amt=0.35)
def naalu(base, rng):
    """Nefishh Projector - a crystal spire city on a snowy peak. Moves: a glint climbing the crystal, its heart
    glowing, an aurora rippling across the sky, clouds drifting at the foot of the peak, snow falling, stars."""
    l = lum(base)
    crystal = soft(hue_mask(base, (200, 190, 255), 32, 140) * box(ART_W * 0.3, 0, ART_W * 0.85, ART_H * 0.9, 0), 0.6)
    heart_xy = ndimage.center_of_mass(crystal * (l > 200)) if (crystal * (l > 200)).sum() > 0 else (ART_H * 0.4, ART_W * 0.6)
    hy, hx = float(heart_xy[0]), float(heart_xy[1])
    heart = np.exp(-(((XX - hx) / 5) ** 2 + ((YY - hy) / 9) ** 2))
    sky = box(0, 0, ART_W, ART_H * 0.6, 8) * (1 - soft(crystal > 0.15, 3) * 2).clip(0, 1)
    au1 = Flow(rng, 7.0, vx=1, aniso=(3.0, 1.0))
    au2 = Flow(rng, 4.0, vx=-1, aniso=(3.0, 1.0))
    cl = Flow(rng, 5.0, vx=1, aniso=(3.0, 1.0))
    clouds = box(0, ART_H * 0.62, ART_W, ART_H, 6)
    snow = Particles(rng, 36, -10, ART_W + 10, -2, ART_H + 2, (230, 240, 255), loops=(1, 2), dx=(6, 14), sway=1.2, bright=(0.3, 0.7))
    st = stars(base, box(0, 0, ART_W, ART_H * 0.5, 0), thr=22, min_l=60, rng=rng)

    def frame(t):
        img = base.copy()
        ph = TAU * t / N_FRAMES
        # the glint: a thin diagonal line of light climbing the crystal once per loop
        s = (t / N_FRAMES) * 1.5 - 0.25
        u = (ART_H - YY) / ART_H + (XX - hx) / ART_W * 0.5
        band = np.exp(-((u - s * 1.3) ** 2) / (2 * 0.018 ** 2))
        add(img, crystal * band, (235, 245, 255), 0.45)
        add(img, heart, (200, 225, 255), 0.22 + 0.2 * math.sin(ph * 3))
        # the aurora: a soft ribbon whose height follows a drifting field
        a = 0.6 * au1(t) + 0.4 * au2(t)
        yc = ART_H * 0.18 + 6.0 * a
        ribbon = np.exp(-((YY - yc) ** 2) / (2 * 4.5 ** 2)) * np.clip(0.5 + 0.5 * a, 0, 1)
        add(img, sky * ribbon, (60, 220, 170), 0.28)
        add(img, sky * ribbon * np.clip(-a, 0, None), (160, 90, 255), 0.22)
        # clouds at the foot of the peak
        add(img, clouds * np.clip(cl(t) * 0.5, 0, 1), (180, 210, 255), 0.2)
        twinkle(img, st, t)
        snow.draw(img, t)
        return img

    return frame


@monument("arborec", "Base+PoK/Arborec.jpg", CROP, sat=1.15, shadow=(20, 16, 40))
def arborec(base, rng):
    """Flaah Orbitals - an orbital band over a hazy world, light raining down through the haze. Moves: the shafts of
    light shimmer downward, a glint runs along the band, spores drift down, the red growths glow and fade."""
    l = lum(base)
    x0, y0 = S(0, 180)
    x1, y1 = S(350, 266)
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy)
    u = ((XX - x0) * dx + (YY - y0) * dy) / (L * L)
    dist = np.abs((XX - x0) * dy - (YY - y0) * dx) / L
    band = np.exp(-(dist ** 2) / (2 * 3.0 ** 2)) * np.clip((l - 110) / 90, 0.2, 1)
    sky = box(0, 0, ART_W, S(0, 175)[1], 6)
    shafts = Flow(rng, 1.0, vy=2, aniso=(0.6, 6.0))
    red = soft(hue_mask(base, (200, 60, 50), 35, 35), 0.8) * box(0, S(0, 185)[1], ART_W, ART_H, 3)
    glow = Flow(rng, 3.0, vx=1)
    spores = Particles(rng, 30, -5, ART_W + 5, -2, ART_H * 0.78, (230, 220, 160), loops=(1, 1), dx=(-6, 6), sway=2.0, bright=(0.3, 0.7))

    def frame(t):
        img = base.copy()
        flicker(img, sky, shafts(t), amp=0.14)
        s = (t / N_FRAMES) * 1.4 - 0.2
        add(img, band * np.exp(-((u - s) ** 2) / (2 * 0.04 ** 2)), (255, 240, 200), 0.6)
        flicker(img, red, glow(t), amp=0.3, hot=(255, 120, 90), hot_amt=0.15)
        spores.draw(img, t)
        return img

    return frame


@monument("argent", "Base+PoK/Argent.jpg", CROP, sat=1.1, shadow=(16, 20, 30))
def argent(base, rng):
    """Phoenix Cauldron - a ring station with a burning heart. Moves: two lights chase round the orange inner ring,
    the heart pulses, the green nebula behind drifts, stars twinkle."""
    cx, cy = S(217, 140)
    r = radius(cx, cy)
    orange = soft(hue_mask(base, (255, 150, 40), 40, 80), 0.6) * (r > 9) * (r < 44)
    core = np.exp(-((r / 8.0) ** 2))
    green = soft(hue_mask(base, (140, 180, 60), 35, 40), 2) * np.clip((r - 40) / 8, 0, 1)
    neb = Flow(rng, 5.0, vx=1, aniso=(1.5, 1.0))
    st = stars(base, (r > 50).astype(np.float32), thr=24, min_l=60, rng=rng)

    def frame(t):
        img = base.copy()
        flicker(img, green, neb(t), amp=0.2)
        for j in range(2):
            th = TAU * (t / N_FRAMES + j / 2)
            add(img, orange * angular(cx, cy, th, 0.32), (255, 215, 130), 0.65)
        add(img, core, (255, 170, 70), 0.12 + 0.25 * wave(t, 2))
        twinkle(img, st, t)
        return img

    return frame


@monument("empyrean", "Base+PoK/Empyrean.jpg", CROP, sat=1.1, shadow=(26, 14, 50))
def empyrean(base, rng):
    """The Panopticon - a ringed city floating among golden clouds under a violet storm. Moves: the city's lights
    twinkle, the clouds drift, a searchlight from the central tower sweeps to and fro, lightning flickers in the
    storm."""
    tx, ty = S(205, 74)
    a0, b0 = S(25, 95)
    a1, b1 = S(335, 200)
    st = stars(base, box(a0, b0, a1, b1, 0), thr=16, min_l=90, rng=rng)
    clouds = soft(hue_mask(base, (230, 170, 90), 40, 60), 2) * (1 - box(a0, b0, a1, b1, 4))
    cl = Flow(rng, 6.0, vx=1, aniso=(2.0, 1.0))
    sky = box(*S(235, 0), *S(350, 95), 6)
    bolts = [bolt(rng, *S(rng.uniform(250, 340), 0), *S(rng.uniform(250, 340), 80), jag=3) * sky for _ in range(2)]

    def frame(t):
        img = base.copy()
        flicker(img, clouds, cl(t), amp=0.15)
        twinkle(img, st, t, depth=0.9)
        th = math.pi / 2 + 1.0 * math.sin(TAU * t / N_FRAMES)
        rr = radius(tx, ty)
        add(img, angular(tx, ty, th, 0.07) * (YY > ty) * np.exp(-rr / 55.0), (255, 235, 190), 0.35)
        add(img, np.exp(-(rr / 2.5) ** 2), (255, 240, 200), 0.3 + 0.4 * wave(t, 3))
        for k, at in enumerate((18, 45)):
            f = flash(t, at)
            if f:
                add(img, sky, (200, 170, 255), 0.25 * f)
                add(img, bolts[k], (240, 230, 255), f)
        return img

    return frame


@monument("hacan", "Base+PoK/Hacan.jpg", CROP, sat=1.15, shadow=(18, 16, 40))
def hacan(base, rng):
    """Mowshir Freeport - a busy free port, ships moored over a city. Moves: the ships' running lights blink, small
    craft cross the sky in lanes, the city glitters, haze drifts."""
    lights = hue_mask(base, (255, 140, 50), 38, 110) * box(0, 0, ART_W, S(0, 175)[1], 0)
    bl = Blinkers(rng, lights, thr=0.3, steady=0.35, blink=0.4)
    st = stars(base, box(*S(150, 150), ART_W, ART_H, 0), thr=18, min_l=100, rng=rng)
    lanes = []
    for k, (sy, col) in enumerate(((60, (255, 200, 140)), (88, (170, 220, 255)), (128, (255, 170, 110)), (40, (200, 230, 255)))):
        y = S(0, sy)[1]
        d = 1 if k % 2 == 0 else -1
        x0 = -12 if d > 0 else ART_W + 12
        lanes.append(Particles(rng, 4, x0, x0, y, y + rng.uniform(-3, 3), col, loops=(1, 1), dx=(d * (ART_W + 24),) * 2, sway=0.3,
                               bright=(0.7, 1.0)))
    haze = Flow(rng, 7.0, vx=1, aniso=(2.5, 1.0))
    sky = box(0, 0, ART_W, S(0, 150)[1], 8)

    def frame(t):
        img = base.copy()
        add(img, sky * np.clip(0.5 + 0.35 * haze(t), 0, 1), (150, 170, 210), 0.18)
        bl.apply(img, t, glow=(255, 170, 90))
        for ln in lanes:
            ln.draw(img, t)
        twinkle(img, st, t, depth=0.9)
        return img

    return frame


@monument("jolnar", "Base+PoK/Jol-Nar.jpg", CROP, palette=48, dither=10.0, sat=1.1, shadow=(20, 24, 44), shadow_amt=0.35)
def jolnar(base, rng):
    """Calm Seas Sanatorium - a sanatorium built over great falls. Moves: the falls pour, spray rises at their feet,
    the clouds drift, the sanatorium's lens glows softly."""
    l = lum(base)
    sat_ = base.max(-1) - base.min(-1)
    falls = soft((l > 150) & (sat_ < 40), 0.8) * box(0, S(0, 214)[1], ART_W, ART_H, 3)
    pour = Flow(rng, 2.0, vy=3, aniso=(0.6, 3.0))
    spray = Flow(rng, 3.0, vy=-1, aniso=(1.5, 1.0))
    foot = box(0, S(0, 250)[1], ART_W, ART_H, 4)
    sky = soft((l > 150) & (sat_ < 40), 1.5) * box(0, 0, ART_W, S(0, 195)[1], 4)
    cl = Flow(rng, 6.0, vx=1, aniso=(2.0, 1.0))
    lx, ly = S(228, 182)
    lens = ring(lx, ly, 6.5, 7.5, 1.2)
    drops = Particles(rng, 22, 0, S(140, 0)[0], ART_H, S(0, 230)[1], (235, 245, 250), loops=(1, 2), dx=(-4, 4), sway=1.0, bright=(0.3, 0.6))

    def frame(t):
        img = base.copy()
        flicker(img, falls, pour(t), amp=0.1, hot=(240, 250, 255), hot_amt=0.08)
        add(img, foot * np.clip(0.45 + 0.35 * spray(t), 0, 1), (225, 235, 240), 0.25)
        flicker(img, sky, cl(t), amp=0.06)
        add(img, lens, (110, 230, 220), 0.25 + 0.3 * wave(t, 2))
        drops.draw(img, t)
        return img

    return frame


@monument("letnev", "Base+PoK/Letnev.jpg", CROP, sat=1.15, shadow=(14, 18, 40))
def letnev(base, rng):
    """Palatine Obelisk - a black spire under a storm. Moves: the storm clouds roll, lightning strikes the spire twice
    a loop, light climbs the spire's edges, mist drifts over the violet plain."""
    l = lum(base)
    sx_, tipy = S(195, 18)
    spire = box(sx_ - 9, tipy, sx_ + 9, ART_H, 1) * (l < 120)
    sky = box(0, 0, ART_W, S(0, 230)[1], 6) * (1 - soft(box(sx_ - 8, tipy, sx_ + 8, ART_H, 0), 2))
    storm = Flow(rng, 6.0, vx=1, aniso=(2.0, 1.0))
    edge = np.clip(np.abs(ndimage.sobel(l, axis=1)) / 120.0, 0, 1) * box(sx_ - 10, tipy, sx_ + 10, ART_H, 0)
    ground = box(0, S(0, 235)[1], ART_W, ART_H, 4)
    mist = Flow(rng, 5.0, vx=1, aniso=(3.0, 1.0))
    # far strikes on either side of the spire, cloud to plain
    bolts = [bolt(rng, *S(40, 20), *S(70, 232), jag=4, segs=11), bolt(rng, *S(320, 10), *S(285, 228), jag=4, segs=11)]

    def frame(t):
        img = base.copy()
        flicker(img, sky, storm(t), amp=0.16)
        for j in range(2):
            y = ART_H * (1.05 - ((t / N_FRAMES) * 2 + j / 2) % 1.0 * 1.1)
            add(img, edge * np.exp(-((YY - y) ** 2) / 12.0), (120, 240, 230), 0.5)
        add(img, ground * np.clip(0.4 + 0.35 * mist(t), 0, 1), (150, 110, 200), 0.25)
        for k, at in enumerate((12, 40)):
            f = flash(t, at)
            if f:
                add(img, sky, (170, 230, 255), 0.16 * f)
                add(img, bolts[k], (235, 250, 255), f)
                add(img, spire, (150, 230, 255), 0.3 * f)
        return img

    return frame


@monument("mahact", "Base+PoK/Mahact.jpg", CROP, sat=1.1, shadow=(18, 20, 44), shadow_amt=0.35)
def mahact(base, rng):
    """Spire of Ixth - a needle spire over a blue city, bright clouds behind. Moves: the clouds drift, golden light
    climbs the spire, the city below glitters, a slow shimmer of sunlight."""
    l = lum(base)
    sx_, _ = S(210, 0)
    spire = box(sx_ - 7, 0, sx_ + 7, ART_H, 1)
    clouds = soft(l > 165, 2) * (1 - soft(spire, 2)) * box(0, 0, ART_W, S(0, 225)[1], 4)
    cl = Flow(rng, 7.0, vx=1, aniso=(2.5, 1.0))
    edge = np.clip(np.abs(ndimage.sobel(l, axis=1)) / 100.0, 0, 1) * spire
    st = stars(base, box(0, S(0, 215)[1], ART_W, ART_H, 0), thr=16, min_l=80, rng=rng)

    def frame(t):
        img = base.copy()
        flicker(img, clouds, cl(t), amp=0.08)
        for j in range(3):
            y = ART_H * (1.05 - ((t / N_FRAMES) * 1 + j / 3) % 1.0 * 1.15)
            add(img, edge * np.exp(-((YY - y) ** 2) / 20.0), (255, 215, 120), 0.6)
        twinkle(img, st, t, depth=0.9)
        return img

    return frame


@monument("mentak", "Base+PoK/Mentak.jpg", CROP, sat=1.1, shadow=(14, 14, 30))
def mentak(base, rng):
    """Erwan's Fist - a great ring of blades hanging in the dark. Moves: a glint travels along the blades, the red
    lights blink in their own rhythms, stars twinkle, debris drifts."""
    l = lum(base)
    cx, cy = S(200, 262)
    r = radius(cx, cy)
    metal = np.clip((l - 120) / 90, 0, 1) * (r > 45) * (r < 95)
    red = hue_mask(base, (255, 60, 60), 35, 60)
    bl = Blinkers(rng, red, thr=0.3, steady=0.3, blink=0.45, low=0.15)
    st = stars(base, (metal < 0.1).astype(np.float32), thr=26, min_l=70, rng=rng)
    debris = Particles(rng, 10, -5, ART_W + 5, ART_H * 0.1, ART_H * 0.9, (170, 180, 200), loops=(1, 1), dx=(20, 40), sway=0.5, bright=(0.3, 0.5))
    vapour = soft(l > 185, 1.5) * box(0, 0, S(150, 0)[0], S(0, 110)[1], 4)
    vf = Flow(rng, 3.0, vx=1, vy=-1)

    def frame(t):
        img = base.copy()
        th = -math.pi * 1.05 + (t / N_FRAMES) * math.pi * 1.4
        flicker(img, vapour, vf(t), amp=0.18)
        add(img, metal * angular(cx, cy, th, 0.15), (230, 240, 255), 0.75 * math.sin(math.pi * t / N_FRAMES) ** 0.5)
        bl.apply(img, t, glow=(255, 90, 80), glow_amt=0.2)
        twinkle(img, st, t, depth=0.9)
        debris.draw(img, t)
        return img

    return frame


@monument("naaz", "Base+PoK/NRA.jpg", CROP, sat=1.1, shadow=(16, 18, 36))
def naaz(base, rng):
    """Quantum Reliquary - a golden reliquary sphere in a blue-lit hall. Moves: the light inside the sphere breathes,
    a glint slides across its glass, motes orbit it, the hall's blue lamps pulse."""
    cx, cy = S(175, 122)
    rr = radius(cx, cy)
    R = 35.0
    sphere = np.clip((R - rr) / 3, 0, 1)
    core = np.exp(-((rr / 18) ** 2))
    blue = soft(hue_mask(base, (60, 160, 255), 30, 80), 1.0) * (rr > R + 2)
    lamps = Flow(rng, 3.0, vy=-1)
    motes = [(rng.uniform(0, 1), rng.uniform(0.8, 1.15), rng.randint(1, 3)) for _ in range(7)]

    def frame(t):
        img = base.copy()
        add(img, core * sphere, (255, 210, 120), 0.12 + 0.2 * wave(t, 2))
        s = (t / N_FRAMES) * 1.6 - 0.3
        u = ((XX - cx) + (YY - cy)) / (2 * R) * 0.7 + 0.5
        add(img, sphere * np.exp(-((u - s) ** 2) / (2 * 0.035 ** 2)), (255, 250, 230), 0.45)
        flicker(img, blue, lamps(t), amp=0.3, hot=(150, 220, 255), hot_amt=0.2)
        for p0, k, loops in motes:
            a = TAU * (p0 + loops * t / N_FRAMES)
            x = cx + math.cos(a) * (R + 8) * k
            y = cy + math.sin(a) * 10 * k - math.cos(a) * 4
            if math.sin(a) < 0 and math.hypot(x - cx, y - cy) < R:
                continue  # behind the sphere
            add(img, np.exp(-((XX - x) ** 2 + (YY - y) ** 2) / 0.8), (255, 235, 170), 0.9)
        return img

    return frame


@monument("sardakk", "Base+PoK/N_Orr.jpg", CROP, sat=1.15, shadow=(16, 22, 24))
def sardakk(base, rng):
    """G'hom Firefly Beacon - a glowing hive tower among drifting egg-sacs and fireflies. Moves: the fireflies glow
    and fade, more of them wander through the dark, the hive's fire flickers upward."""
    ff = hue_mask(base, (200, 235, 60), 30, 90)
    bl = Blinkers(rng, ff, thr=0.3, steady=0.2, blink=0.2, low=0.35, fmax=3)
    hive = soft(hue_mask(base, (255, 140, 30), 40, 80), 1.0)
    fire = Flow(rng, 2.5, vy=-2, aniso=(1.0, 1.8))
    flies = Particles(rng, 22, -5, ART_W + 5, ART_H + 2, -2, (215, 255, 90), loops=(1, 1), dx=(-12, 12), sway=4.0, size=1, bright=(0.5, 1.0))

    def frame(t):
        img = base.copy()
        flicker(img, hive, fire(t), amp=0.25, hot=(255, 210, 110), hot_amt=0.2)
        bl.apply(img, t, glow=(220, 255, 120), glow_amt=0.25)
        flies.draw(img, t)
        return img

    return frame


@monument("nekro", "Base+PoK/Nekro.jpg", CROP, sat=1.1, shadow=(22, 12, 24))
def nekro(base, rng):
    """Valefar Assimilator "M" - a dark city floating over the clouds, its red heart glowing. Moves: the heart
    pulses, a red scan sweeps the clouds below, the city's red lights blink, the clouds drift."""
    l = lum(base)
    hx, hy = S(210, 72)
    heart = np.exp(-(radius(hx, hy, 1.0, 0.7) / 4.0) ** 2)
    red = hue_mask(base, (255, 70, 50), 35, 70)
    bl = Blinkers(rng, red * (radius(hx, hy) > 6), thr=0.3, steady=0.4, blink=0.35, low=0.2)
    sat_ = base.max(-1) - base.min(-1)
    clouds = soft((l > 120) & (sat_ < 60), 1.5) * box(0, S(0, 215)[1], ART_W, ART_H, 4)
    cl = Flow(rng, 6.0, vx=1, aniso=(2.5, 1.0))

    def frame(t):
        img = base.copy()
        flicker(img, clouds, cl(t), amp=0.14)
        add(img, heart, (255, 80, 50), 0.25 + 0.4 * wave(t, 2))
        th = math.pi / 2 + 0.75 * math.sin(TAU * t / N_FRAMES)
        add(img, angular(hx, hy, th, 0.05) * (YY > hy + 10) * np.exp(-radius(hx, hy) / 70.0), (255, 60, 50), 0.4)
        bl.apply(img, t, glow=(255, 90, 70), glow_amt=0.15)
        return img

    return frame


@monument("nomad", "Base+PoK/Nomad.jpg", CROP, sat=1.15, shadow=(12, 16, 36))
def nomad(base, rng):
    """Lodestar - a shrine where a column of light rises. Moves: light streams up the column, rings ripple out across
    the floor, sparks rise, the shrine breathes."""
    l = lum(base)
    cx, _ = S(200, 0)
    bx, by = S(200, 256)
    column = soft(np.clip((l - 110) / 100, 0, 1) * box(cx - 14, 0, cx + 14, by, 2), 0.6)
    up = Flow(rng, 1.4, vy=-2, aniso=(0.6, 4.0))
    floor = box(0, by - 12, ART_W, ART_H, 4)
    base_glow = np.exp(-(radius(bx, by, 3.0, 1.0) / 8.0) ** 2)
    sparks = Particles(rng, 22, cx - 10, cx + 10, by, 0, (190, 230, 255), loops=(1, 2), dx=(-4, 4), sway=1.5, bright=(0.4, 0.9))

    def frame(t):
        img = base.copy()
        flicker(img, column, up(t), amp=0.22, hot=(200, 240, 255), hot_amt=0.25)
        rp = np.zeros((ART_H, ART_W), np.float32)
        for j in range(3):
            s = (t / N_FRAMES + j / 3) % 1.0
            rp += ring(bx, by, 6 + s * 75, 1.5 + s * 14, 0.8 + s) * (1 - s) ** 1.2
        add(img, rp * floor, (110, 200, 255), 0.5)
        add(img, base_glow, (150, 220, 255), 0.15 + 0.25 * wave(t, 2))
        sparks.draw(img, t)
        return img

    return frame


@monument("saar", "Base+PoK/Saar.jpg", CROP, sat=1.1, shadow=(20, 12, 20))
def saar(base, rng):
    """Beacon of Ragh - a dark diamond over a fire, under a shaft of light. Moves: the light shaft shimmers down, the
    fire below flickers upward and throws embers, a gleam runs round the diamond's edge."""
    l = lum(base)
    cx, cy = S(205, 140)
    shaft = soft(np.clip((l - 130) / 90, 0, 1) * box(*S(140, 0), *S(270, 105), 3), 0.8)
    down = Flow(rng, 1.4, vy=2, aniso=(0.6, 4.0))
    fire = soft(hue_mask(base, (255, 150, 50), 40, 90), 0.8) * box(0, S(0, 150)[1], ART_W, ART_H, 3)
    up = Flow(rng, 2.4, vy=-2, aniso=(1.0, 1.8))
    rim = np.clip(np.abs(ndimage.sobel(l, axis=0)) / 150 + np.abs(ndimage.sobel(l, axis=1)) / 150, 0, 1) \
        * (np.abs(XX - cx) / 40 + np.abs(YY - cy) / 46 < 1.15) * (np.abs(XX - cx) / 40 + np.abs(YY - cy) / 46 > 0.7)
    embers = Particles(rng, 24, cx - 30, cx + 30, ART_H, S(0, 120)[1], (255, 170, 70), loops=(1, 3), sway=1.5)

    def frame(t):
        img = base.copy()
        flicker(img, shaft, down(t), amp=0.18)
        flicker(img, fire, up(t), amp=0.25, hot=(255, 220, 140), hot_amt=0.22)
        add(img, rim * angular(cx, cy, TAU * t / N_FRAMES, 0.35), (255, 200, 120), 0.5)
        embers.draw(img, t)
        return img

    return frame


@monument("sol", "Base+PoK/Sol.jpg", CROP, sat=1.1, shadow=(20, 22, 40), shadow_amt=0.35)
def sol(base, rng):
    """The Cenotaph - a white memorial with a red lamp line and a green eye, among autumn trees. Moves: a red light
    climbs the lamp line, the green eye pulses, clouds drift, leaves fall."""
    l = lum(base)
    lx, _ = S(205, 0)
    y0, y1 = S(0, 60)[1], S(0, 180)[1]
    lamp = np.exp(-((XX - lx) ** 2) / (2 * 0.9 ** 2)) * (YY > y0) * (YY < y1)
    ex, ey = S(207, 170)
    eye = np.exp(-(radius(ex, ey) / 2.6) ** 2)
    sat_ = base.max(-1) - base.min(-1)
    clouds = soft((l > 170) & (sat_ < 50), 1.5) * (np.abs(XX - lx) > 14) * box(0, 0, ART_W, S(0, 230)[1], 3)
    cl = Flow(rng, 6.0, vx=1, aniso=(2.0, 1.0))
    leaves = [Particles(rng, 14, *S(0, 120)[:1], S(110, 0)[0], S(0, 150)[1], ART_H + 2, (240, 120, 50), loops=(1, 1), dx=(8, 20),
                        sway=2.5, bright=(0.5, 0.9)),
              Particles(rng, 9, S(300, 0)[0], ART_W, S(0, 170)[1], ART_H + 2, (240, 140, 60), loops=(1, 1), dx=(-14, -4),
                        sway=2.5, bright=(0.5, 0.9))]

    def frame(t):
        img = base.copy()
        flicker(img, clouds, cl(t), amp=0.1)
        s = (t / N_FRAMES) * 2 % 1.0
        y = y1 - s * (y1 - y0)
        add(img, lamp * np.exp(-((YY - y) ** 2) / 8.0), (255, 120, 100), 0.9 * math.sin(math.pi * s) ** 0.5)
        add(img, eye, (120, 255, 150), 0.2 + 0.35 * wave(t, 2))
        for p in leaves:
            p.draw(img, t)
        return img

    return frame


@monument("titans", "Base+PoK/Titans.jpg", CROP, contrast=1.0, sat=1.1, shadow=(22, 12, 40))
def titans(base, rng):
    """Scepter of Ul - a towering scepter pouring pink light. Moves: light streams down its columns, its crown of
    light breathes, the floor's glow ripples outward, sparks drift up and stars twinkle."""
    l = lum(base)
    cx, _ = S(188, 0)
    fx, fy = S(190, 282)
    tower = soft(np.clip((l - 140) / 90, 0, 1) * box(*S(95, 75), *S(290, 270), 5), 0.6)
    down = Flow(rng, 1.3, vy=2, aniso=(0.6, 4.0))
    crown = np.exp(-(radius(*S(190, 20), 2.2, 1.0) / 14.0) ** 2)
    floor = box(0, fy - 16, ART_W, ART_H, 3)
    st = stars(base, box(0, 0, ART_W, S(0, 120)[1], 0), thr=20, min_l=80, rng=rng)
    sparks = Particles(rng, 20, fx - 40, fx + 40, ART_H, S(0, 60)[1], (255, 200, 250), loops=(1, 2), dx=(-6, 6), sway=1.5, bright=(0.4, 0.9))

    def frame(t):
        img = base.copy()
        flicker(img, tower, down(t), amp=0.12, hot=(255, 220, 255), hot_amt=0.08)
        add(img, crown, (255, 160, 240), 0.06 + 0.12 * wave(t, 2))
        rp = np.zeros((ART_H, ART_W), np.float32)
        for j in range(2):
            s = (t / N_FRAMES + j / 2) % 1.0
            rp += ring(fx, fy, 8 + s * 70, 2 + s * 12, 0.9 + s) * (1 - s) ** 1.2
        add(img, rp * floor, (255, 150, 240), 0.5)
        twinkle(img, st, t, depth=0.9)
        sparks.draw(img, t)
        return img

    return frame


@monument("cabal", "Base+PoK/Vuil_Raith.jpg", CROP, sat=1.1, shadow=(14, 10, 22))
def cabal(base, rng):
    """She Consumes Worlds - a vast beast of tendrils and fire with burning eyes. Moves: the eyes flare and dim like
    breathing, molten veins pulse through its body, embers rise from it."""
    l = lum(base)
    ex0, ey0 = S(185, 100)
    ex1, ey1 = S(245, 145)
    eyes = soft(hue_mask(base, (255, 110, 40), 40, 90) * box(ex0, ey0, ex1, ey1, 0), 0.7)
    halo = soft(eyes, 3.0)
    lava = soft(hue_mask(base, (255, 80, 40), 40, 50), 0.8) * box(0, S(0, 160)[1], ART_W, ART_H, 3)
    veins = Flow(rng, 2.4, vy=-1, aniso=(1.0, 1.5))
    embers = Particles(rng, 36, 10, ART_W - 10, ART_H, S(0, 120)[1], (255, 140, 60), loops=(1, 2), sway=2.0, bright=(0.4, 0.9))
    teal = soft(hue_mask(base, (90, 190, 190), 30, 60), 1.5)
    tf = Flow(rng, 4.0, vx=-1, aniso=(1.5, 1.0))

    def frame(t):
        img = base.copy()
        # a heartbeat: two quick swells, then rest
        ph = (t / N_FRAMES * 2) % 1.0
        beat = math.exp(-((ph - 0.1) ** 2) / 0.004) + 0.6 * math.exp(-((ph - 0.24) ** 2) / 0.004)
        flicker(img, teal, tf(t), amp=0.15)
        flicker(img, lava, veins(t) + beat * 1.2, amp=0.25, hot=(255, 170, 80), hot_amt=0.22)
        b = wave(t, 1)
        img *= (1 + eyes * (0.35 * b - 0.1))[..., None]
        add(img, halo, (255, 90, 30), 0.18 + 0.3 * b)
        embers.draw(img, t)
        return img

    return frame


@monument("winnu", "Base+PoK/Winnu.jpg", CROP, sat=1.1, shadow=(24, 14, 20))
def winnu(base, rng):
    """The Imperial Vault - a great gimbal in a vault, lit by a round golden window. Moves: shafts of light fall from
    the window and breathe, dust drifts through them, a gleam travels round the gimbal's rings."""
    l = lum(base)
    wx, wy = S(215, 40)
    gx, gy = S(210, 150)
    rays = np.zeros((ART_H, ART_W), np.float32)
    for th, w in ((1.25, 0.06), (1.57, 0.08), (1.9, 0.06), (1.45, 0.04), (1.72, 0.04)):
        rays += angular(wx, wy, th, w)
    rays *= (YY > wy) * np.exp(-radius(wx, wy) / 70.0)
    window = soft(np.clip((l - 170) / 60, 0, 1) * box(0, 0, ART_W, S(0, 120)[1], 0), 1.0)
    glow = Flow(rng, 4.0, vy=1)
    metal = np.clip((l - 90) / 80, 0, 1) * (radius(gx, gy) > 10) * (radius(gx, gy) < 42) * (1 - window)
    dust = Particles(rng, 38, wx - 34, wx + 34, wy, ART_H, (255, 225, 160), loops=(1, 1), dx=(-10, 10), sway=2.5, bright=(0.3, 0.7))

    def frame(t):
        img = base.copy()
        flicker(img, window, glow(t), amp=0.12)
        add(img, rays, (255, 210, 140), 0.08 + 0.1 * wave(t, 1))
        add(img, metal * angular(gx, gy, TAU * t / N_FRAMES, 0.3), (255, 220, 160), 0.35)
        dust.draw(img, t)
        return img

    return frame


@monument("xxcha", "Base+PoK/Xxcha.jpg", CROP, sat=1.1, shadow=(14, 20, 34))
def xxcha(base, rng):
    """Qanoj Shield Array - hexagonal shield frames over a ring of golden lights. Moves: a shimmer crosses the hex
    frames, lights chase round the golden ring, the shield over the centre ripples."""
    l = lum(base)
    cx, cy = S(205, 195)
    hexes = np.clip((l - 110) / 90, 0, 1) * box(0, 0, ART_W, S(0, 120)[1], 2)
    gold = soft(hue_mask(base, (255, 160, 60), 38, 100), 0.6)
    st = stars(base, box(0, S(0, 110)[1], ART_W, ART_H, 0), thr=24, min_l=110, rng=rng)

    def frame(t):
        img = base.copy()
        s = (t / N_FRAMES) * 1.5 - 0.25
        u = XX / ART_W * 0.8 + YY / ART_H * 0.2
        add(img, hexes * np.exp(-((u - s) ** 2) / (2 * 0.05 ** 2)), (170, 240, 255), 0.55)
        for j in range(3):
            add(img, gold * angular(cx, cy, TAU * (t / N_FRAMES + j / 3), 0.25, squash=2.6), (255, 230, 150), 0.55)
        sh = (t / N_FRAMES * 2) % 1.0
        add(img, ring(cx, cy - 6, 6 + sh * 50, 2 + sh * 18, 1.0) * (1 - sh) * box(0, S(0, 110)[1], ART_W, S(0, 255)[1], 4),
            (140, 230, 255), 0.45)
        twinkle(img, st, t, depth=0.9)
        return img

    return frame


@monument("yin", "Base+PoK/Yin.jpg", CROP, sat=1.1, shadow=(14, 14, 36))
def yin(base, rng):
    """The Blessed Sanctum - a dark teardrop with a burning core, split by a line of light, wrapped in cloud. Moves:
    the core pulses, light runs along the line out of the core both ways, rings spread from the core, the clouds
    swirl."""
    l = lum(base)
    cx, cy = S(200, 118)
    beam = np.exp(-((XX - cx) ** 2) / (2 * 0.8 ** 2))
    core = np.exp(-(radius(cx, cy, 1.0, 1.6) / 3.0) ** 2)
    sat_ = base.max(-1) - base.min(-1)
    clouds = soft((l > 150) & (sat_ < 70), 1.5)
    cl = Flow(rng, 5.0, vx=1, aniso=(2.0, 1.0))

    def frame(t):
        img = base.copy()
        flicker(img, clouds, cl(t), amp=0.1)
        add(img, core, (255, 110, 60), 0.3 + 0.5 * wave(t, 2))
        s = (t / N_FRAMES * 2) % 1.0
        for y in (cy + s * (ART_H - cy + 6), cy - s * (cy + 6)):
            add(img, beam * np.exp(-((YY - y) ** 2) / 10.0), (255, 200, 160), 0.8 * (1 - s * 0.5))
        rp = ring(cx, cy, 4 + s * 60, 6 + s * 70, 1.0 + s) * (1 - s) ** 1.3
        add(img, rp, (190, 210, 255), 0.3)
        return img

    return frame


@monument("yssaril", "Base+PoK/Yssaril.jpg", CROP, sat=1.15, shadow=(14, 24, 28), shadow_amt=0.35)
def yssaril(base, rng):
    """Sector KVD-14 - towers of stacked discs over a jungle, a pale world in the sky. Moves: the towers' lights
    twinkle, small craft slip between the towers, mist creeps over the jungle, birds glide past."""
    st = stars(base, box(*S(80, 0), *S(350, 260), 0), thr=18, min_l=110, rng=rng)
    jungle = soft(hue_mask(base, (90, 160, 60), 40, 40), 1.5) * box(0, S(0, 200)[1], ART_W, ART_H, 3)
    mist = Flow(rng, 5.0, vx=1, aniso=(3.0, 1.0))
    craft = [Particles(rng, 3, -10, -10, S(0, 110)[1], S(0, 104)[1], (220, 240, 255), loops=(1, 1), dx=(ART_W + 20,) * 2, sway=0.3),
             Particles(rng, 3, ART_W + 10, ART_W + 10, S(0, 175)[1], S(0, 178)[1], (255, 230, 180), loops=(1, 1), dx=(-ART_W - 20,) * 2,
                       sway=0.3)]
    birds = Particles(rng, 4, -10, 20, S(0, 60)[1], S(0, 90)[1], (-70, -60, -50), loops=(1, 1), dx=(ART_W, ART_W + 20), sway=2.0,
                      size=2, bright=(0.7, 1.0))
    l = lum(base)
    world = soft((l > 200) & (hue_mask(base, (240, 240, 190), 20) > 0.3), 1.5)
    wf = Flow(rng, 4.0, vx=1, aniso=(3.0, 1.0))

    def frame(t):
        img = base.copy()
        twinkle(img, st, t, depth=0.9)
        add(img, jungle * np.clip(0.4 + 0.4 * mist(t), 0, 1), (200, 230, 220), 0.32)
        flicker(img, world, wf(t), amp=0.07)
        for c in craft:
            c.draw(img, t)
        birds.draw(img, t)
        return img

    return frame


KEL_CROP = (0, 350, 664, 848)


@monument("keleres", "TE/Keleres.jpg", KEL_CROP, dither=12.0, sat=1.1, shadow=(16, 22, 50), shadow_amt=0.35)
def keleres(base, rng):
    """Pharus Iustitiae - a statue of Justice with a spear before a ring of light, a city below. Moves: light runs
    round the ring, the spear's tip glints, clouds drift, birds glide, the city glitters."""
    l = lum(base)
    rx, ry = S(215, 52, KEL_CROP)
    rr = radius(rx, ry)
    halo = np.clip((l - 170) / 60, 0, 1) * (rr > 26) * (rr < 48)
    tx, ty = S(220, 14, KEL_CROP)
    sat_ = base.max(-1) - base.min(-1)
    clouds = soft((l > 150) & (sat_ < 90), 1.5) * box(0, 0, ART_W, S(0, 220, KEL_CROP)[1], 3) * (1 - halo)
    cl = Flow(rng, 6.0, vx=1, aniso=(2.5, 1.0))
    st = stars(base, box(0, S(0, 215, KEL_CROP)[1], ART_W, ART_H, 0), thr=16, min_l=120, rng=rng)
    birds = Particles(rng, 3, ART_W + 4, ART_W + 30, S(0, 120, KEL_CROP)[1], S(0, 80, KEL_CROP)[1], (-70, -60, -40), loops=(1, 1),
                      dx=(-ART_W - 40, -ART_W - 20), sway=2.0, size=2)

    def frame(t):
        img = base.copy()
        flicker(img, clouds, cl(t), amp=0.08)
        for j in range(2):
            add(img, halo * angular(rx, ry, TAU * (t / N_FRAMES + j / 2), 0.3), (235, 250, 255), 0.4)
        g = max(0.0, math.sin(TAU * t / N_FRAMES * 2)) ** 6
        add(img, np.exp(-(((XX - tx) / 0.7) ** 2 + ((YY - ty) / 4.0) ** 2)) + np.exp(-(((XX - tx) / 4.0) ** 2 + ((YY - ty) / 0.7) ** 2)),
            (255, 255, 255), 0.9 * g)
        twinkle(img, st, t, depth=0.9)
        birds.draw(img, t)
        return img

    return frame


# ------------------------------------------------------------------ render
def render(key, cards_dir, out):
    m = MONUMENTS[key]
    rng = np.random.RandomState(sum(map(ord, key)) * 97)
    if m.get("paint"):  # a picture drawn from scratch (none at the moment; the module that did it is in git history)
        raw, masks = m["paint"](np.random.RandomState(sum(map(ord, key)) * 31))
        base = grade(raw, **m["grade"])
        fn = m["fn"](base, rng, masks)
    else:
        raw = load_art(os.path.join(cards_dir, m["card"]), m["crop"])
        base = grade(raw, **m["grade"])
        fn = m["fn"](base, rng)
    frames = [np.clip(fn(t), 0, 255) for t in range(N_FRAMES)]
    pal = make_palette(frames, m["palette"], seed=sum(map(ord, key)))
    idx = [quantize(f, pal, m["dither"]) for f in frames]
    os.makedirs(out, exist_ok=True)
    palb = pal.astype(np.uint8)
    rgb = [palb[i] for i in idx]
    Image.fromarray(np.concatenate(rgb, axis=1), "RGB").save(os.path.join(out, key + "_strip.png"))
    flat = list(palb.reshape(-1)) + [0] * (768 - palb.size)
    gif = []
    for i in idx:
        p = Image.fromarray(i, "P").resize((ART_W * 3, ART_H * 3), Image.NEAREST)
        p.putpalette(flat)
        gif.append(p)
    gif[0].save(os.path.join(out, key + ".gif"), save_all=True, append_images=gif[1:], duration=FRAME_MS, loop=0, optimize=False,
                disposal=1)
    # the card's picture beside our first frame (a drawn one: the reference card, if it is there, for comparison only)
    ref = m.get("ref") if m.get("paint") else m["card"]
    if ref and os.path.exists(os.path.join(cards_dir, ref)):
        src = card_crop(os.path.join(cards_dir, ref), m["crop"]).resize((ART_W * 3, ART_H * 3), Image.LANCZOS)
    else:
        src = Image.new("RGB", (ART_W * 3, ART_H * 3), (20, 20, 28))
    cmp_ = Image.new("RGB", (ART_W * 6 + 12, ART_H * 3), (20, 20, 28))
    cmp_.paste(src, (0, 0))
    cmp_.paste(Image.fromarray(rgb[0], "RGB").resize((ART_W * 3, ART_H * 3), Image.NEAREST), (ART_W * 3 + 12, 0))
    cmp_.save(os.path.join(out, key + "_compare.png"))
    print(f"{key}: {len(pal)} colours, wrote {out}/{key}.gif")


if __name__ == "__main__":
    # part 2 (Thunder's Edge, Blue Reverie, Discordant Stars) registers into this module's MONUMENTS
    sys.modules["gen_monument_anim"] = sys.modules[__name__]
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import monument_anim_more  # noqa: F401
    cards, out = sys.argv[1], sys.argv[2]
    keys = sys.argv[3].split(",") if len(sys.argv) > 3 else list(MONUMENTS)
    for k in keys:
        render(k, cards, out)
