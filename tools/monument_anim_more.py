"""Monuments+ animated pictures, part 2: Thunder's Edge (6), Twilight's Fall (9), Blue Reverie (10) and Discordant Stars (35).

Art sources (owner + Cacotopos, 2026-11-21 23:43): Caco drew the cards' art; the Twilight's Fall pictures he made with Midjourney
(fine to use); the Drahn monument (wallpaperbetter) and the Nekro "data center" reminder card (Reminders/Nekro Station Front,
Shutterstock) are LEFT OUT until confirmed. The Seraph Data Center (Bastion) is Caco's.

Loaded by gen_monument_anim.py, which owns the pipeline (cut, grade, palette, dither, render). Here each picture is a short
list of effects from a small kit (fx_*), so 51 pictures stay readable. Coordinates come from planning sheets of the card's
picture area at half size (350 x 300, see S()); P() maps them to art pixels for the usual cut CB.
"""
import math

import numpy as np
from scipy import ndimage

from gen_monument_anim import (ART_H, ART_W, N_FRAMES, TAU, XX, YY, Blinkers, Flow, Particles, S, add, angular, bolt, box,
                               flash, flicker, hue_mask, lum, monument, radius, ring, soft, stars, twinkle, wave)

CB = (0, 360, 640, 840)  # the cut for these: clear of the card's bevelled top-right corner (4:3)


def P(sx, sy, crop=CB):
    return S(sx, sy, crop)


def PX(sx):
    return P(sx, 0)[0]


def PY(sy):
    return P(0, sy)[1]


def compose(base, *fx):
    def frame(t):
        img = base.copy()
        for f in fx:
            f(img, t)
        return img
    return frame


def sat(base):
    return base.max(-1) - base.min(-1)


# ------------------------------------------------------------------ the effects kit (each returns f(img, t))
def fx_drift(rng, mask, amp=0.12, sigma=6.0, vx=1, vy=0, aniso=(2.0, 1.0)):
    """brighter and darker patches drifting across mask (clouds, nebulae, water)"""
    f = Flow(rng, sigma, vx=vx, vy=vy, aniso=aniso)
    return lambda img, t: flicker(img, mask, f(t), amp=amp)


def fx_haze(rng, mask, colour, amt=0.25, sigma=5.0, vx=1, vy=0, aniso=(3.0, 1.0)):
    """a coloured mist drifting over mask"""
    f = Flow(rng, sigma, vx=vx, vy=vy, aniso=aniso)
    return lambda img, t: add(img, mask * np.clip(0.4 + 0.4 * f(t), 0, 1), colour, amt)


def fx_fire(rng, mask, amp=0.25, hot=(255, 210, 120), hot_amt=0.2, sigma=2.4, vy=-2):
    """flames / energy streaming (vy < 0 up, > 0 down)"""
    f1 = Flow(rng, sigma, vy=vy, aniso=(1.0, 1.8))
    f2 = Flow(rng, sigma * 0.65, vy=vy - 1 if vy < 0 else vy + 1, aniso=(1.0, 1.5))
    return lambda img, t: flicker(img, mask, 0.65 * f1(t) + 0.35 * f2(t), amp=amp, hot=hot, hot_amt=hot_amt)


def fx_pour(rng, mask, amp=0.15, sigma=1.4, vy=2, hot=(240, 250, 255), hot_amt=0.08):
    """thin streaks running along mask (light shafts, falling water, curtains)"""
    f = Flow(rng, sigma, vy=vy, aniso=(0.6, 4.0))
    return lambda img, t: flicker(img, mask, f(t), amp=amp, hot=hot, hot_amt=hot_amt)


def fx_blink(rng, mask, glow=None, glow_amt=0.15, max_px=30, **kw):
    """lights that keep their own rhythms; a patch bigger than max_px pixels is not a light (a lit wall, a glow band) and is
    left alone"""
    lab, n = ndimage.label(mask > kw.get("thr", 0.35))
    if n:
        sizes = np.asarray(ndimage.sum(np.ones_like(mask), lab, range(1, n + 1)))
        big = np.concatenate([[False], sizes > max_px])
        mask = mask * ~ndimage.binary_dilation(big[lab], iterations=1)
    b = Blinkers(rng, mask, **kw)
    return lambda img, t: b.apply(img, t, glow=glow, glow_amt=glow_amt)


def fx_twinkle(rng, base, region, thr=24.0, min_l=80.0, depth=0.85):
    st = stars(base, region, thr=thr, min_l=min_l, rng=rng)
    return lambda img, t: twinkle(img, st, t, depth=depth)


def fx_pulse(cx, cy, rx, ry, colour, lo=0.1, hi=0.4, f=2, ph=0.0):
    g = np.exp(-(((XX - cx) / rx) ** 2 + ((YY - cy) / ry) ** 2))
    return lambda img, t: add(img, g, colour, lo + (hi - lo) * wave(t, f, ph))


def fx_orbit(cx, cy, mask, colour, amt=0.55, n=2, width=0.3, squash=1.0, loops=1, sign=1):
    def f(img, t):
        for j in range(n):
            add(img, mask * angular(cx, cy, sign * TAU * (loops * t / N_FRAMES + j / n), width, squash), colour, amt)
    return f


def fx_sweep(mask, colour, amt=0.5, angle=-0.35, width=0.04, speed=1.5):
    """a band of light crossing mask once a loop; angle = direction it travels (radians, 0 = rightward, -pi/2 = up)"""
    c, s_ = math.cos(angle), math.sin(angle)
    u = XX / ART_W * c + YY / ART_H * s_
    u = (u - u.min()) / (u.max() - u.min())

    def f(img, t):
        s = (t / N_FRAMES) * speed - (speed - 1) / 2
        add(img, mask * np.exp(-((u - s) ** 2) / (2 * width ** 2)), colour, amt)
    return f


def fx_climb(rng, base, region, colour=(120, 240, 255), amt=0.8, maxn=7, thr=0.42, down=False):
    """small lights running along the long vertical edges inside region, each in its own rhythm"""
    l = lum(base)
    edge = np.clip(np.abs(ndimage.sobel(l, axis=1)) / 120.0, 0, 1) * region
    long_ = (ndimage.uniform_filter(edge, size=(13, 1)) > thr) & (edge > 0.3)
    lab, n = ndimage.label(long_, structure=np.ones((3, 3)))
    sizes = ndimage.sum(long_, lab, range(1, n + 1)) if n else []
    keep = [i + 1 for i in np.argsort(sizes)[::-1][:maxn] if sizes[i] >= 10] if n else []
    edges = []
    for i in keep:
        ys, _ = np.nonzero(lab == i)
        edges.append((soft(lab == i, 0.5), float(ys.min()), float(ys.max()), rng.uniform(0, 1), rng.randint(1, 3)))

    def f(img, t):
        for m_, y0, y1, p0, loops in edges:
            s = (p0 + loops * t / N_FRAMES) % 1.0
            y = (y0 - 4 + s * (y1 - y0 + 8)) if down else (y1 + 4 - s * (y1 - y0 + 8))
            add(img, m_ * np.exp(-((YY - y) ** 2) / 8.0), colour, amt * math.sin(math.pi * s) ** 0.5)
    return f


def fx_ripple(cx, cy, rx, ry, colour, amt=0.45, n=3, region=None, w=1.0):
    reg = 1.0 if region is None else region

    def f(img, t):
        rp = np.zeros((ART_H, ART_W), np.float32)
        for j in range(n):
            s = (t / N_FRAMES + j / n) % 1.0
            rp += ring(cx, cy, 2 + s * rx, 1 + s * ry, w * (0.8 + s)) * (1 - s) ** 1.2
        add(img, rp * reg, colour, amt)
    return f


def fx_packets(x0, y0, x1, y1, colour, amt=0.8, n=2, width=0.9, size=3.0, along=None):
    """lights running along the line (x0, y0) -> (x1, y1), n per loop; along = also brighten the whole line a little"""
    dx, dy = x1 - x0, y1 - y0
    L = max(math.hypot(dx, dy), 1e-3)
    u = ((XX - x0) * dx + (YY - y0) * dy) / (L * L)
    dist = np.abs((XX - x0) * dy - (YY - y0) * dx) / L
    line = np.exp(-(dist ** 2) / (2 * width ** 2)) * (u > -0.02) * (u < 1.02)

    def f(img, t):
        for j in range(n):
            s = (t / N_FRAMES * n + j / n) % 1.0
            add(img, line * np.exp(-(((u - s) * L) ** 2) / (2 * size ** 2)), colour, amt * math.sin(math.pi * s) ** 0.5)
        if along:
            add(img, line, colour, along)
    return f


def fx_particles(p, mask=None):
    return lambda img, t: p.draw(img, t, mask)


def fx_lightning(rng, sky, strikes, times=(14, 41), colour=(235, 245, 255), sky_colour=(180, 200, 255), sky_amt=0.15):
    bolts = [bolt(rng, *s, jag=4, segs=10) for s in strikes]

    def f(img, t):
        for k, at in enumerate(times):
            fl = flash(t, at)
            if fl:
                add(img, sky, sky_colour, sky_amt * fl)
                add(img, bolts[k % len(bolts)], colour, fl)
    return f


def fx_rays(cx, cy, angles, colour, lo=0.05, hi=0.15, width=0.05, reach=70.0, f=1):
    r = np.zeros((ART_H, ART_W), np.float32)
    for a in angles:
        r += angular(cx, cy, a, width)
    r *= np.exp(-radius(cx, cy) / reach)
    return lambda img, t: add(img, r, colour, lo + (hi - lo) * wave(t, f))


def fx_spiral(rng, cx, cy, r0, colour, n=10, squash=1.3, turns=1.5, sign=1):
    """motes spiralling into (cx, cy)"""
    ps = [(rng.uniform(0, 1), rng.uniform(0, TAU), rng.uniform(0.6, 1.0)) for _ in range(n)]

    def f(img, t):
        for p0, a0, b in ps:
            s = (p0 + t / N_FRAMES) % 1.0
            a = a0 + sign * s * TAU * turns
            rr = r0 * (1 - s) ** 0.8
            x, y = cx + math.cos(a) * rr, cy + math.sin(a) * rr / squash
            add(img, np.exp(-((XX - x) ** 2 + (YY - y) ** 2) / 0.8), colour, b * math.sin(math.pi * s) ** 0.6)
    return f


def lanes(rng, rows, n=3, colour=(220, 235, 255), bright=(0.7, 1.0), size=1):
    """small craft crossing in lanes: rows = [(y, direction), ...]"""
    out = []
    for y, d in rows:
        x0 = -12 if d > 0 else ART_W + 12
        out.append(fx_particles(Particles(rng, n, x0, x0, y, y + rng.uniform(-3, 3), colour, loops=(1, 1), dx=(d * (ART_W + 24),) * 2,
                                          sway=0.3, bright=bright, size=size)))
    return out


def birds(rng, n, y0, y1, d=1, shade=(-70, -60, -50)):
    x0 = (-12, 10) if d > 0 else (ART_W + 2, ART_W + 24)
    return fx_particles(Particles(rng, n, x0[0], x0[1], y0, y1, shade, loops=(1, 1), dx=(d * (ART_W + 10), d * (ART_W + 30)), sway=2.0,
                                  size=2, bright=(0.7, 1.0)))


def snow(rng, n=34, colour=(230, 240, 255), bright=(0.3, 0.7), dx=(6, 14)):
    return fx_particles(Particles(rng, n, -10, ART_W + 10, -2, ART_H + 2, colour, loops=(1, 2), dx=dx, sway=1.2, bright=bright))


def rising(rng, n, x0, x1, y0, y1, colour, loops=(1, 2), sway=1.5, bright=(0.4, 0.9), dx=(0, 0)):
    return fx_particles(Particles(rng, n, x0, x1, y0, y1, colour, loops=loops, sway=sway, bright=bright, dx=dx))


def bright_small(base, thr=200, region=1.0):
    """small bright points (windows, lamps)"""
    l = lum(base)
    local = ndimage.gaussian_filter(l, 1.5)
    return np.clip((l - local - 8) / 30, 0, 1) * (l > thr) * region


# ================================================================== Thunder's Edge
@monument("bastion", "TE/Bastion.jpg", CB, palette=48, dither=12.0, sat=1.1, shadow=(16, 18, 40), shadow_amt=0.35)
def bastion(base, rng):
    """Seraph Data Center - a great station over a cloudy world, the sun at the rim. Moves: the clouds drift below, the
    station's lights blink, a glint runs up its spine, the sun breathes."""
    l = lum(base)
    station = box(PX(85), 0, PX(270), ART_H, 4) * (l < 160)
    clouds = soft(l > 110, 1.5) * (1 - soft(station, 2) * 1.5).clip(0, 1)
    spine = np.clip((l - 110) / 90, 0, 1) * box(PX(85), 0, PX(270), ART_H, 2)
    return compose(base,
                   fx_drift(rng, clouds, amp=0.1, sigma=6, vx=1),
                   fx_blink(rng, bright_small(base, 150, box(PX(85), 0, PX(270), ART_H, 0)), glow=(255, 225, 190), thr=0.25),
                   fx_sweep(spine, (255, 245, 225), amt=0.45, angle=-1.15, width=0.035),
                   fx_pulse(*P(80, 14), 10, 8, (255, 240, 210), 0.08, 0.3, f=1))



@monument("crimson", "TE/Crimson Rebellion.jpg", CB, sat=1.05, shadow=(24, 8, 26))
def crimson(base, rng):
    """The Crimson Rebellion's monument - red curtains of light falling over a violet world. Moves: light streams down the
    curtains and they sway, a glow climbs one curtain, the world's surface shimmers, sparks glint in the dark."""
    red = soft(hue_mask(base, (255, 40, 70), 40, 60), 0.8) * box(0, 0, ART_W, PY(235), 4)
    world = box(0, PY(225), ART_W, ART_H, 4)
    return compose(base,
                   fx_pour(rng, red, amp=0.18, sigma=1.3, vy=1, hot=(255, 170, 190), hot_amt=0.1),
                   fx_drift(rng, red, amp=0.1, sigma=2.0, vx=1, aniso=(0.5, 4.0)),
                   fx_sweep(red * box(PX(160), 0, PX(230), ART_H, 3), (255, 200, 220), amt=0.5, angle=-math.pi / 2, width=0.05),
                   fx_haze(rng, world, (210, 130, 255), amt=0.22, sigma=4, vx=1),
                   fx_twinkle(rng, base, (lum(base) < 90).astype(np.float32), thr=20, min_l=50))


@monument("deepwrought", "TE/Deepwrought.jpg", CB, sat=1.1, shadow=(10, 20, 36), shadow_amt=0.35)
def deepwrought(base, rng):
    """The Deepwrought Scholarate's monument - a tower city on the sea floor. Moves: light ripples through the water,
    shafts of light shimmer down from the surface, a school of fish swims past, bubbles rise, amber lamps glow and fade."""
    l = lum(base)
    fish = Particles(rng, 14, -14, 6, PY(150), PY(195), (225, 240, 255), loops=(1, 1), dx=(ART_W + 20, ART_W + 30), sway=2.5,
                     bright=(0.4, 0.8))
    return compose(base,
                   fx_drift(rng, box(0, 0, ART_W, ART_H, 0), amp=0.07, sigma=3.0, vx=1, vy=1, aniso=(1.0, 1.0)),
                   fx_pour(rng, soft(l > 100, 2) * box(0, 0, ART_W, PY(110), 6), amp=0.12, sigma=1.6, vy=1),
                   fx_blink(rng, hue_mask(base, (255, 140, 40), 40, 90), glow=(255, 170, 80), thr=0.3, steady=0.2, blink=0.1),
                   fx_particles(fish),
                   rising(rng, 18, 0, ART_W, ART_H + 2, -2, (190, 230, 255), loops=(1, 2), sway=1.0, bright=(0.25, 0.55)))


@monument("firmament", "TE/Firmament.jpg", CB, palette=52, dither=12.0, sat=1.2, shadow=(18, 22, 50), shadow_amt=0.3)
def firmament(base, rng):
    """The Firmament's monument - a great white ring on a garden terrace under a blue sky. Moves: clouds drift, a gleam runs
    round the ring, light shimmers in the sky beyond it, birds glide past."""
    l = lum(base)
    cx, cy = P(245, 130)
    rr = radius(cx, cy)
    sky = soft(l > 90, 1.5) * box(0, 0, ART_W, PY(170), 5) * np.clip((rr - 33) / 4, 0, 1)
    ringm = np.clip((l - 140) / 80, 0, 1) * (rr > 20) * (rr < 42)
    return compose(base,
                   fx_drift(rng, sky, amp=0.12, sigma=6, vx=1),
                   fx_orbit(cx, cy, ringm, (255, 250, 235), amt=0.45, n=1, width=0.35),
                   fx_pulse(cx, cy, 12, 14, (200, 230, 255), 0.05, 0.2, f=2),
                   birds(rng, 4, PY(40), PY(80), d=-1))


@monument("obsidian", "TE/Obsidian.jpg", CB, sat=1.1, shadow=(20, 10, 24))
def obsidian(base, rng):
    """The Obsidian's monument - a black arch on a desert plain under a red giant. Moves: violet ripples turn inside the
    arch, the red giant's storms drift, dust blows across the plain."""
    cx, cy = P(212, 132)
    inside = np.clip((17 - radius(cx, cy, 1.0, 1.15)) / 3, 0, 1)
    redsky = soft(hue_mask(base, (220, 70, 60), 40, 50), 1.5) * box(0, 0, ART_W, PY(190), 4)
    dust = Particles(rng, 22, -10, ART_W * 0.6, PY(205), PY(285), (230, 170, 130), loops=(1, 1), dx=(30, 60), sway=1.0, bright=(0.2, 0.45))
    return compose(base,
                   fx_drift(rng, redsky, amp=0.12, sigma=5, vx=1),
                   fx_ripple(cx, cy, 17, 20, (150, 100, 230), amt=0.22, n=3, region=inside),
                   fx_orbit(cx, cy, inside, (110, 80, 200), amt=0.15, n=2, width=0.5, sign=-1),
                   fx_particles(dust))


@monument("ralnel", "TE/Ral-Nel.jpg", CB, sat=1.1, shadow=(12, 14, 30))
def ralnel(base, rng):
    """The Ral Nel Consortium's monument - a slow procession of great hulls through the stars. Moves: windows blink along
    the hulls, a glint slides over them, stars twinkle, dust drifts past."""
    l = lum(base)
    hulls = np.clip((l - 90) / 90, 0, 1)
    return compose(base,
                   fx_blink(rng, bright_small(base, 175), glow=(255, 240, 210), thr=0.25, steady=0.4, blink=0.35),
                   fx_sweep(hulls, (255, 245, 225), amt=0.35, angle=0.5, width=0.05),
                   fx_twinkle(rng, base, (soft(l < 70, 1) > 0.6).astype(np.float32), thr=22, min_l=60),
                   fx_particles(Particles(rng, 10, -10, ART_W, 0, ART_H, (190, 200, 220), loops=(1, 1), dx=(30, 50), sway=0.5,
                                          bright=(0.2, 0.4))))


# ================================================================== Blue Reverie
@monument("atokera", "BR/Atokera.jpg", CB, palette=48, sat=1.3, shadow=(14, 14, 34))
def atokera(base, rng):
    """Atokera's monument - a glittering crystal city rising through a field of drifting rock. Moves: light streams up the
    city, rocks drift past, stars twinkle, the ships at its foot blink."""
    l = lum(base)
    column = soft(np.clip((l - 120) / 90, 0, 1) * box(PX(115), 0, PX(225), ART_H, 4), 0.6)
    return compose(base,
                   fx_fire(rng, column, amp=0.2, hot=(255, 235, 170), hot_amt=0.15, sigma=2.0, vy=-2),
                   fx_twinkle(rng, base, 1 - box(PX(110), 0, PX(230), ART_H, 0), thr=24, min_l=80),
                   fx_blink(rng, bright_small(base, 150, box(0, PY(170), PX(110), ART_H, 0)), glow=(170, 230, 255), thr=0.25),
                   fx_particles(Particles(rng, 12, -10, ART_W, 0, ART_H, (-40, -40, -30), loops=(1, 1), dx=(20, 40), sway=0.4, size=2,
                                          bright=(0.6, 1.0))))


@monument("belkosea", "BR/Belkosea.jpg", CB, sat=1.1, shadow=(16, 18, 30), shadow_amt=0.35)
def belkosea(base, rng):
    """Belkosea's monument - black spires on a frozen plain before a pale moon, a forge glowing at their feet. Moves: the
    forge fire flickers and throws sparks, light climbs the spires, mist creeps over the plain, snow falls."""
    fire = soft(hue_mask(base, (255, 130, 50), 40, 70), 0.8) * box(PX(170), PY(180), PX(310), ART_H, 3)
    fx_, fy_ = P(238, 232)
    return compose(base,
                   fx_fire(rng, fire, amp=0.3, hot=(255, 210, 130), hot_amt=0.25),
                   rising(rng, 18, fx_ - 14, fx_ + 14, fy_, fy_ - 40, (255, 170, 70), loops=(1, 3)),
                   fx_climb(rng, base, box(PX(100), PY(60), PX(320), PY(240), 0), colour=(255, 190, 120), amt=0.6, maxn=6),
                   fx_haze(rng, box(0, PY(240), ART_W, ART_H, 4), (215, 220, 235), amt=0.2),
                   snow(rng, 26, bright=(0.25, 0.55)))


@monument("kaltrim", "BR/Kaltrim.jpg", CB, sat=1.1, shadow=(12, 18, 24))
def kaltrim(base, rng):
    """Kaltrim's monument - a vast dark station with a golden heart. Moves: the heart glows and fades, light runs down the
    golden line beneath it, the hull's lamps blink, a pale fog drifts behind."""
    l = lum(base)
    teal = soft(hue_mask(base, (120, 190, 180), 32, 70), 1.5) * box(0, 0, ART_W, PY(120), 4)
    return compose(base,
                   fx_drift(rng, teal, amp=0.15, sigma=6, vx=1),
                   fx_pulse(*P(188, 145), 9, 5, (255, 210, 120), 0.1, 0.4, f=2),
                   fx_packets(*P(188, 175), *P(188, 300), (255, 220, 130), amt=0.9, n=2, width=0.8),
                   fx_blink(rng, hue_mask(base, (255, 200, 90), 38, 110) * (radius(*P(188, 145)) > 8), glow=(255, 210, 120), thr=0.3))


@monument("pharadn", "BR/Pharad_n.jpg", CB, sat=1.1, shadow=(14, 16, 26), shadow_amt=0.35)
def pharadn(base, rng):
    """Pharad'n's monument - a dark shard rising from the clouds, two moons hanging beside it, a lit world above. Moves:
    the clouds roll, the lights of the world above glitter, a gleam climbs the shard's edges."""
    l = lum(base)
    clouds = soft((l > 140) & (sat(base) < 60), 1.5) * box(0, PY(90), ART_W, ART_H, 4)
    return compose(base,
                   fx_drift(rng, clouds, amp=0.12, sigma=6, vx=1),
                   fx_twinkle(rng, base, box(0, 0, ART_W, PY(110), 0), thr=16, min_l=70),
                   fx_climb(rng, base, box(PX(150), 0, PX(330), PY(250), 0), colour=(220, 235, 255), amt=0.6, maxn=5))


@monument("qhet", "BR/Qhet.jpg", CB, sat=1.1, shadow=(16, 20, 30), shadow_amt=0.35)
def qhet(base, rng):
    """Qhet's monument - turquoise pools in a great stone hall. Moves: the water shimmers and rings spread across the pools,
    shafts of light shimmer from above, motes drift up through the light."""
    l = lum(base)
    pools = soft(hue_mask(base, (80, 230, 220), 30, 90), 1.0)
    return compose(base,
                   fx_drift(rng, pools, amp=0.15, sigma=2.5, vx=1, aniso=(2.0, 1.0)),
                   fx_ripple(*P(125, 215), 30, 8, (190, 255, 245), amt=0.35, n=2, region=pools),
                   fx_ripple(*P(235, 145), 22, 6, (190, 255, 245), amt=0.3, n=2, region=pools),
                   fx_pour(rng, soft(l > 110, 2) * box(0, 0, ART_W, PY(130), 6), amp=0.1, vy=1),
                   rising(rng, 20, 0, ART_W, ART_H, PY(40), (255, 235, 190), loops=(1, 1), bright=(0.25, 0.55)))


@monument("sarcosa", "BR/Sarcosa.jpg", CB, sat=1.1, shadow=(12, 16, 26))
def sarcosa(base, rng):
    """Sarcosa's monument - a dark frozen city around a glowing cyan arch. Moves: the arch's light flows upward, small craft
    slip between the towers, snow drifts down, a magenta lamp glows below."""
    cyan = soft(hue_mask(base, (60, 230, 230), 30, 80), 0.8) * box(PX(110), PY(110), PX(290), ART_H, 4)
    return compose(base,
                   fx_fire(rng, cyan, amp=0.18, hot=(190, 255, 255), hot_amt=0.12, sigma=2.0, vy=-1),
                   *lanes(rng, [(PY(70), 1), (PY(150), -1)], n=2, colour=(210, 240, 255)),
                   snow(rng, 28, bright=(0.25, 0.5)),
                   fx_pulse(*P(12, 290), 6, 6, (255, 90, 200), 0.05, 0.3, f=1))


@monument("toldar_dishonor", "BR/Toldar - Dishonor.jpg", CB, sat=1.05, shadow=(18, 10, 16))
def toldar_dishonor(base, rng):
    """Toldar's monument, the side of Dishonor - an arena ringed with fire. Moves: the fires flicker upward, embers rise
    over the arena, the crowd glitters."""
    fire = soft(hue_mask(base, (255, 120, 40), 42, 80), 0.8)
    return compose(base,
                   fx_fire(rng, fire, amp=0.3, hot=(255, 215, 140), hot_amt=0.22),
                   rising(rng, 30, 0, ART_W, ART_H, PY(20), (255, 160, 70), loops=(1, 2), bright=(0.4, 0.9)),
                   fx_twinkle(rng, base, box(PX(60), PY(90), PX(290), PY(230), 0), thr=16, min_l=60))


@monument("toldar_honor", "BR/Toldar - Honor.jpg", CB, sat=1.1, shadow=(12, 18, 28))
def toldar_honor(base, rng):
    """Toldar's monument, the side of Honor - a spire at the heart of a packed arena. Moves: the crowd glitters, the golden
    lamps blink, light climbs the spire to a pulsing tip."""
    return compose(base,
                   fx_twinkle(rng, base, box(PX(40), PY(60), PX(330), PY(280), 0), thr=16, min_l=70),
                   fx_blink(rng, hue_mask(base, (255, 200, 60), 38, 120), glow=(255, 220, 120), thr=0.3, steady=0.4),
                   fx_packets(*P(187, 190), *P(187, 70), (200, 250, 240), amt=0.8, n=2, width=0.9),
                   fx_pulse(*P(187, 70), 3, 4, (210, 255, 245), 0.1, 0.5, f=2))


@monument("uydai", "BR/Uydai.jpg", CB, sat=1.05, shadow=(20, 24, 50), shadow_amt=0.3)
def uydai(base, rng):
    """Uydai's monument - white towers over a turquoise lagoon. Moves: the lagoon shimmers and glints, clouds drift, a
    gleam climbs the towers, birds glide past."""
    l = lum(base)
    water = soft(hue_mask(base, (60, 200, 220), 35, 100), 1.0) * box(0, PY(180), ART_W, ART_H, 3)
    clouds = soft((l > 200) & (sat(base) < 40), 1.5) * box(0, 0, ART_W, PY(150), 4)
    towers = np.clip((l - 190) / 50, 0, 1) * box(PX(90), 0, PX(300), PY(200), 2)
    return compose(base,
                   fx_drift(rng, water, amp=0.15, sigma=2.5, vx=1, aniso=(3.0, 1.0)),
                   fx_twinkle(rng, base, water, thr=14, min_l=150),
                   fx_drift(rng, clouds, amp=0.08, sigma=6, vx=1),
                   fx_sweep(towers, (255, 255, 255), amt=0.35, angle=-math.pi / 2, width=0.05),
                   birds(rng, 4, PY(30), PY(70), d=1))


@monument("xin", "BR/Xin.jpg", CB, sat=1.1, shadow=(14, 12, 30))
def xin(base, rng):
    """Xin's monument - a dark arch over a city of spires under a violet nebula. Moves: the nebula drifts, stars twinkle,
    the city's lights blink, rings spread across the pool at its foot, small craft pass."""
    l = lum(base)
    neb = soft(l > 90, 2) * box(0, 0, ART_W, PY(170), 5)
    return compose(base,
                   fx_drift(rng, neb, amp=0.15, sigma=5, vx=1),
                   fx_twinkle(rng, base, box(0, 0, ART_W, PY(180), 0), thr=22, min_l=80),
                   fx_blink(rng, bright_small(base, 140, box(0, PY(120), ART_W, PY(270), 0)), glow=(200, 220, 255), thr=0.25),
                   fx_ripple(*P(232, 282), 30, 6, (150, 220, 255), amt=0.4, n=2, region=box(0, PY(255), ART_W, ART_H, 2)),
                   *lanes(rng, [(PY(110), -1)], n=2, colour=(220, 200, 255)))


# ================================================================== Discordant Stars
@monument("augurs", "DS/Augurs.jpg", CB, sat=1.1, shadow=(12, 18, 24))
def augurs(base, rng):
    """The Augurs of Ilyxum's monument - a dark pyramid traced with burning lines. Moves: light runs up the burning lines,
    the molten ground flickers, the clouds behind drift."""
    l = lum(base)
    lines = soft(hue_mask(base, (255, 100, 40), 40, 70), 0.6)
    clouds = soft((l > 150) & (sat(base) < 70), 1.5) * box(0, 0, ART_W, PY(230), 4)
    return compose(base,
                   fx_fire(rng, lines * box(0, 0, ART_W, PY(250), 2), amp=0.35, hot=(255, 190, 110), hot_amt=0.25, sigma=1.6, vy=-1),
                   fx_fire(rng, lines * box(0, PY(240), ART_W, ART_H, 2), amp=0.3, hot=(255, 200, 120), hot_amt=0.2),
                   fx_drift(rng, clouds, amp=0.1, sigma=6, vx=1))


@monument("axis", "DS/Axis.jpg", CB, palette=52, dither=12.0, sat=1.1, shadow=(18, 14, 30))
def axis(base, rng):
    """The Axis Order's monument - a tower on a crag, a column of fire burning through it. Moves: fire rises through the
    column, the blaze at its foot flickers and throws embers, clouds drift behind."""
    l = lum(base)
    column = soft(hue_mask(base, (255, 110, 60), 40, 80), 0.8) * box(PX(185), 0, PX(245), PY(270), 3)
    foot = soft(hue_mask(base, (255, 140, 60), 45, 90), 0.8) * box(PX(120), PY(250), PX(300), ART_H, 3)
    sky = soft((l > 150) & (sat(base) < 80), 1.5) * (1 - box(PX(150), PY(20), PX(270), ART_H, 4))
    return compose(base,
                   fx_fire(rng, column, amp=0.25, hot=(255, 220, 150), hot_amt=0.2, sigma=1.8, vy=-3),
                   fx_fire(rng, foot, amp=0.3, hot=(255, 220, 140), hot_amt=0.25),
                   rising(rng, 20, *P(170, 0)[:1], PX(250), ART_H, PY(170), (255, 170, 80), loops=(1, 3)),
                   fx_drift(rng, sky, amp=0.05, sigma=7, vx=1))


@monument("bentor", "DS/Bentor.jpg", CB, palette=52, dither=12.0, sat=1.1, shadow=(20, 14, 34))
def bentor(base, rng):
    """The Bentor Conglomerate's monument - a domed vault under a violet dusk. Moves: the dusk clouds drift, a gleam slides
    over the dome, the windows' lights blink, dust drifts in the air."""
    l = lum(base)
    sky = soft(l > 120, 1.5) * (1 - box(PX(120), PY(35), PX(330), ART_H, 4))
    dome = np.clip((l - 90) / 90, 0, 1) * box(PX(200), PY(35), PX(330), PY(150), 2)
    return compose(base,
                   fx_drift(rng, sky, amp=0.05, sigma=7, vx=1),
                   fx_sweep(dome, (255, 235, 255), amt=0.4, angle=0.3, width=0.05),
                   fx_blink(rng, bright_small(base, 150, box(PX(120), PY(100), ART_W, ART_H, 0)), glow=(255, 220, 200), thr=0.25),
                   fx_particles(Particles(rng, 30, -5, ART_W, 0, ART_H, (230, 210, 240), loops=(1, 2), dx=(10, 30), sway=1.5,
                                          bright=(0.25, 0.5))),
                   *lanes(rng, [(PY(40), 1), (PY(80), -1)], n=2, colour=(255, 230, 240)))


@monument("celdauri", "DS/Celdauri.jpg", CB, sat=1.1, shadow=(12, 16, 36), shadow_amt=0.35)
def celdauri(base, rng):
    """The Celdauri Trade Confederation's monument - a ring gate holding a blue star, rain sweeping the plain. Moves:
    lights chase round the gate, the star at its heart flares and dims, rain falls, the wet ground shimmers."""
    l = lum(base)
    cx, cy = P(213, 150)
    rr = radius(cx, cy)
    inner = np.clip((l - 120) / 90, 0, 1) * (rr > 16) * (rr < 34) * hue_mask(base, (90, 170, 255), 45)
    rain = Particles(rng, 46, -20, ART_W, -2, ART_H + 2, (170, 200, 240), loops=(3, 4), dx=(-12, -8), sway=0.0, bright=(0.15, 0.35))
    return compose(base,
                   fx_orbit(cx, cy, inner, (200, 235, 255), amt=0.55, n=3, width=0.28),
                   fx_pulse(cx, cy, 4, 4, (255, 240, 255), 0.2, 0.7, f=3),
                   fx_rays(cx, cy, [0, math.pi / 2, math.pi, -math.pi / 2], (230, 220, 255), 0.05, 0.25, width=0.04, reach=12, f=3),
                   fx_drift(rng, box(0, PY(240), ART_W, ART_H, 3), amp=0.1, sigma=2.0, vx=1, aniso=(3.0, 1.0)),
                   fx_particles(rain))


@monument("cheiran", "DS/Cheiran.jpg", CB, sat=1.15, shadow=(10, 18, 24))
def cheiran(base, rng):
    """The Cheiran Hordes' monument - a stone tower crusted with shells, flakes of rock drifting round it. Moves: rock
    flakes drift upward, a pale mist moves behind, motes of light float, a gleam climbs the tower."""
    l = lum(base)
    mist = soft(hue_mask(base, (110, 200, 200), 35, 60), 1.5)
    tower = np.clip((l - 70) / 90, 0, 1) * box(PX(150), 0, PX(300), PY(150), 3)
    return compose(base,
                   fx_haze(rng, mist, (170, 230, 230), amt=0.2, sigma=5, vx=1),
                   fx_particles(Particles(rng, 16, 0, ART_W, ART_H + 2, -2, (-50, -50, -40), loops=(1, 1), dx=(-10, 10), sway=1.0, size=2,
                                          bright=(0.6, 1.0))),
                   rising(rng, 16, 0, ART_W, ART_H, 0, (190, 240, 230), loops=(1, 1), bright=(0.25, 0.5)),
                   fx_sweep(tower, (220, 245, 240), amt=0.35, angle=-math.pi / 2, width=0.05))


@monument("cymiae", "DS/Cymiae.jpg", CB, sat=1.1, shadow=(14, 22, 34), shadow_amt=0.35)
def cymiae(base, rng):
    """The Cymiae's monument - a white tower rising from green mountains into the clouds. Moves: the clouds and mist drift,
    lights climb the tower's edges, birds glide past."""
    l = lum(base)
    mist = soft((l > 160) & (sat(base) < 50), 1.5)
    return compose(base,
                   fx_drift(rng, mist, amp=0.12, sigma=6, vx=1),
                   fx_climb(rng, base, box(PX(90), PY(40), PX(300), ART_H, 0), colour=(170, 235, 255), amt=0.45, maxn=7),
                   birds(rng, 3, PY(60), PY(110), d=-1))


@monument("dihmohn", "DS/Dih-Mohn.jpg", CB, sat=1.1, shadow=(10, 16, 36), shadow_amt=0.35)
def dihmohn(base, rng):
    """The Dih-Mohn Flotilla's monument - a ring station over the clouds around a blue core. Moves: the core pulses, lights
    chase round the ring, the station's lamps blink, the clouds below drift."""
    l = lum(base)
    cx, cy = P(230, 118)
    rr = radius(cx, cy, 1.0, 0.65)
    ringm = np.clip((l - 110) / 90, 0, 1) * (rr > 16) * (rr < 52)
    clouds = soft((l > 140) & (sat(base) < 70), 1.5) * (rr > 55)
    return compose(base,
                   fx_pulse(cx, cy, 10, 7, (120, 220, 255), 0.12, 0.45, f=2),
                   fx_orbit(cx, cy, ringm, (190, 240, 255), amt=0.45, n=2, width=0.3, squash=1.55),
                   fx_blink(rng, bright_small(base, 170, (rr < 55).astype(np.float32)), glow=(220, 240, 255), thr=0.25),
                   fx_drift(rng, clouds, amp=0.1, sigma=6, vx=1))


@monument("edyn", "DS/Edyn.jpg", CB, sat=1.1, shadow=(18, 16, 28), shadow_amt=0.35)
def edyn(base, rng):
    """The Edyn Mandate's monument - a crystal throne before a ring of light. Moves: the crystal glows and glitters, a gleam
    runs round the ring, motes drift in the light, the candles flicker."""
    l = lum(base)
    cx, cy = P(200, 112)
    rr = radius(cx, cy)
    halo = np.clip((l - 160) / 70, 0, 1) * (rr > 32) * (rr < 50)
    candles = hue_mask(base, (255, 160, 70), 45, 110) * np.clip(bright_small(base, 110) * 3, 0, 1) * box(0, PY(190), ART_W, ART_H, 0)
    return compose(base,
                   fx_pulse(cx, cy, 6, 8, (255, 240, 210), 0.1, 0.4, f=2),
                   fx_twinkle(rng, base, box(cx - 9, cy - 14, cx + 9, cy + 14, 0), thr=12, min_l=120),
                   fx_orbit(cx, cy, halo, (255, 250, 230), amt=0.55, n=2, width=0.35),
                   fx_blink(rng, candles, glow=(255, 190, 100), thr=0.25, steady=0.0, blink=0.0, fmax=6),
                   fx_particles(Particles(rng, 34, cx - 44, cx + 44, ART_H, 0, (255, 240, 210), loops=(1, 2), dx=(-6, 6), sway=2.0,
                                          bright=(0.25, 0.55))))


@monument("fsc", "DS/FSC.jpg", CB, palette=52, dither=11.0, sat=1.1, shadow=(18, 22, 40), shadow_amt=0.3)
def fsc(base, rng):
    """The Free Systems Compact's monument - a white tower over a bright city. Moves: clouds drift, the tower's green panel
    pulses, light climbs the tower, traffic flows along the roads."""
    l = lum(base)
    sky = soft((l > 170) & (sat(base) < 70), 1.5) * box(0, 0, ART_W, PY(170), 4) * (1 - box(PX(205), 0, PX(250), ART_H, 2))
    return compose(base,
                   fx_drift(rng, sky, amp=0.04, sigma=8, vx=1),
                   fx_pulse(*P(225, 108), 3, 7, (120, 255, 160), 0.05, 0.45, f=2),
                   fx_packets(*P(225, 240), *P(225, 0), (230, 255, 240), amt=0.7, n=3, width=1.4),
                   *lanes(rng, [(PY(250), 1), (PY(268), -1), (PY(258), 1)], n=6, colour=(255, 240, 200), bright=(0.6, 1.0), size=2),
                   *lanes(rng, [(PY(120), -1)], n=2, colour=(230, 240, 255)),
                   birds(rng, 4, PY(40), PY(90), d=1))


@monument("florzen", "DS/Florzen.jpg", CB, sat=1.1, shadow=(10, 16, 34), shadow_amt=0.35)
def florzen(base, rng):
    """The Florzen Profiteers' monument - a harbour city ringed with lights at the edge of a dark sea. Moves: the harbour
    lights blink, the sea shimmers, water pours over the edge, small craft pass above."""
    l = lum(base)
    lights = hue_mask(base, (255, 150, 60), 40, 100)
    falls = np.clip((l - 100) / 90, 0, 1) * box(PX(235), PY(235), PX(290), ART_H, 2)
    return compose(base,
                   fx_blink(rng, lights, glow=(255, 190, 110), thr=0.3, steady=0.3, blink=0.4),
                   fx_drift(rng, box(0, 0, ART_W, PY(200), 4) * (lights < 0.2), amp=0.08, sigma=3, vx=1, aniso=(2.0, 1.0)),
                   fx_pour(rng, falls, amp=0.25, sigma=1.0, vy=3),
                   *lanes(rng, [(PY(42), 1)], n=2, colour=(220, 235, 255)))


@monument("gledge", "DS/GLEdge.jpg", CB, sat=1.1, shadow=(14, 14, 20))
def gledge(base, rng):
    """The GLEdge Union's monument - a crater city lit in tiers of amber. Moves: the tiers' lights glitter, smoke drifts
    across the crater, small craft cross it."""
    l = lum(base)
    lights = hue_mask(base, (255, 150, 60), 40, 120)
    crater = box(PX(20), PY(30), PX(340), PY(260), 6)
    return compose(base,
                   fx_twinkle(rng, base, crater, thr=14, min_l=90, depth=0.9),
                   fx_blink(rng, lights * crater, glow=(255, 200, 120), thr=0.35, steady=0.6, blink=0.25),
                   fx_haze(rng, crater, (200, 170, 150), amt=0.12, sigma=5, vx=1),
                   *lanes(rng, [(PY(120), 1), (PY(170), -1)], n=2, colour=(255, 220, 170)))


@monument("ghemina", "DS/Ghemina.jpg", CB, palette=48, dither=12.0, sat=1.1, shadow=(14, 20, 44), shadow_amt=0.35)
def ghemina(base, rng):
    """The Ghemina Raiders' monument - twin crystal towers among snowy peaks. Moves: blue light streams up the towers, a
    glint climbs them, mist drifts at their feet, snow falls."""
    l = lum(base)
    towers_ = np.maximum(box(PX(130), PY(10), PX(190), PY(285), 1), box(PX(212), PY(10), PX(272), PY(285), 1))
    blue = soft(hue_mask(base, (40, 140, 255), 28, 90) * (sat(base) > 120), 0.6) * towers_
    towers = np.clip((l - 150) / 80, 0, 1) * box(PX(120), 0, PX(260), PY(280), 2)
    return compose(base,
                   fx_fire(rng, blue, amp=0.25, hot=(190, 225, 255), hot_amt=0.2, sigma=1.8, vy=-2),
                   fx_sweep(towers, (235, 245, 255), amt=0.35, angle=-math.pi / 2, width=0.05),
                   fx_haze(rng, box(0, PY(240), ART_W, ART_H, 4), (220, 230, 245), amt=0.2),
                   snow(rng, 30))


@monument("ghoti", "DS/Ghoti.jpg", CB, sat=1.05, shadow=(8, 20, 24))
def ghoti(base, rng):
    """The Ghoti Wayfarers' monument - a living sphere in a sea of glowing tendrils. Moves: patterns ripple over the
    sphere, the tendrils' glow flows, the bright blooms pulse, bubbles rise."""
    l = lum(base)
    cx, cy = P(215, 160)
    sphere = np.clip((36 - radius(cx, cy)) / 3, 0, 1)
    tendrils = soft(hue_mask(base, (60, 230, 160), 40, 70), 1.0) * (1 - sphere)
    return compose(base,
                   fx_drift(rng, sphere, amp=0.14, sigma=2.5, vx=1, vy=-1, aniso=(1.0, 1.0)),
                   fx_drift(rng, tendrils, amp=0.15, sigma=3.0, vx=1, vy=-1, aniso=(1.5, 1.0)),
                   fx_pulse(*P(62, 62), 6, 6, (255, 170, 80), 0.1, 0.45, f=2),
                   fx_pulse(*P(312, 222), 6, 6, (255, 150, 90), 0.1, 0.45, f=2, ph=2.0),
                   rising(rng, 18, 0, ART_W, ART_H, 0, (190, 255, 230), loops=(1, 1), sway=2.0, bright=(0.25, 0.5)))


@monument("khrask", "DS/Khrask.jpg", CB, sat=1.1, shadow=(14, 14, 22))
def khrask(base, rng):
    """The Khrask's monument - a hulk of living rock with a green fire at its crown. Moves: the green core pulses and
    throws rays, the ember in the dark flickers, dust and grit drift past."""
    ember = soft(hue_mask(base, (255, 120, 40), 40, 70), 0.8) * box(0, PY(120), PX(110), PY(240), 3)
    return compose(base,
                   fx_pulse(*P(200, 52), 8, 8, (150, 255, 170), 0.12, 0.5, f=2),
                   fx_rays(*P(200, 52), [k * TAU / 7 for k in range(7)], (180, 255, 190), 0.03, 0.15, width=0.06, reach=30, f=2),
                   fx_fire(rng, ember, amp=0.3, hot=(255, 200, 120), hot_amt=0.25),
                   fx_particles(Particles(rng, 34, -10, ART_W, 0, ART_H, (220, 210, 200), loops=(1, 2), dx=(20, 40), sway=1.0,
                                          bright=(0.25, 0.5))),
                   fx_drift(rng, soft(lum(base) > 150, 2), amp=0.1, sigma=5, vx=1))


@monument("kjalengard", "DS/Kjalengard.jpg", CB, sat=1.1, shadow=(16, 16, 28), shadow_amt=0.35)
def kjalengard(base, rng):
    """The Kjalengard's monument - a winged statue in a candlelit hall before a great round window. Moves: the candles
    flicker, the window's light breathes and falls in rays, dust motes drift through it."""
    cx, cy = P(210, 85)
    candles = hue_mask(base, (255, 170, 80), 45, 120) * np.clip(bright_small(base, 120) * 3, 0, 1) * box(0, PY(150), ART_W, ART_H, 0)
    return compose(base,
                   fx_blink(rng, candles, glow=(255, 200, 110), thr=0.25, steady=0.0, blink=0.0, fmax=7),
                   fx_pulse(cx, cy, 30, 26, (255, 250, 235), 0.04, 0.14, f=1),
                   fx_rays(cx, cy, [1.2, 1.45, 1.7, 1.95], (255, 245, 225), 0.03, 0.2, width=0.05, reach=80),
                   fx_particles(Particles(rng, 36, cx - 40, cx + 40, ART_H, 0, (255, 245, 220), loops=(1, 1), dx=(-8, 8), sway=2.0,
                                          bright=(0.15, 0.4))))


@monument("kollecc", "DS/Kollecc.jpg", CB, sat=1.15, shadow=(8, 10, 18))
def kollecc(base, rng):
    """The Kollecc Society's monument - a dark city canyon strung with neon. Moves: the neon signs flicker in their own
    rhythms, rain streaks down, the sky beyond shimmers."""
    l = lum(base)
    neon = np.clip((l - 90) / 60, 0, 1) * (sat(base) > 70)
    sky = soft((l > 120) & (sat(base) < 90), 1.5) * box(PX(150), 0, PX(330), PY(180), 3)
    rain = Particles(rng, 80, -20, ART_W, -2, ART_H + 2, (160, 190, 230), loops=(3, 4), dx=(-10, -6), sway=0.0, bright=(0.2, 0.45))
    return compose(base,
                   fx_blink(rng, neon, glow=None, thr=0.3, steady=0.35, blink=0.4, low=0.3, fmax=6),
                   fx_drift(rng, sky, amp=0.16, sigma=4, vx=1),
                   *lanes(rng, [(PY(90), 1), (PY(130), -1)], n=3, colour=(255, 120, 160)),
                   fx_particles(rain))


@monument("kolume", "DS/Kolume.jpg", CB, sat=1.05, shadow=(22, 22, 30), shadow_amt=0.3)
def kolume(base, rng):
    """The Kolume's monument - a white sculpture of radiating blades in a garden court. Moves: light sweeps across the
    blades, the sky's haze drifts, rings spread over the fountain pool, motes float."""
    l = lum(base)
    blades = np.clip((l - 160) / 70, 0, 1) * box(PX(80), PY(20), PX(330), PY(240), 2)
    sky = soft(l > 200, 2) * box(0, 0, ART_W, PY(90), 4)
    return compose(base,
                   fx_sweep(blades, (255, 250, 240), amt=0.35, angle=0.2, width=0.04),
                   fx_drift(rng, sky, amp=0.06, sigma=6, vx=1),
                   fx_ripple(*P(232, 266), 30, 5, (235, 240, 245), amt=0.3, n=2, region=box(PX(150), PY(250), PX(320), ART_H, 2)),
                   fx_particles(Particles(rng, 14, 0, ART_W, ART_H, 0, (255, 250, 235), loops=(1, 1), dx=(-6, 6), sway=2.0, bright=(0.15, 0.35))))


@monument("kortali", "DS/Kortali.jpg", CB, sat=1.1, shadow=(12, 16, 34))
def kortali(base, rng):
    """The Kortali Tribunal's monument - a tall alien spire of stacked discs strung with cyan lamps. Moves: the lamps pulse
    in their own rhythms, light climbs the central column, mist drifts beside it."""
    l = lum(base)
    cyan = hue_mask(base, (60, 220, 255), 35, 100)
    mist = soft((l > 150) & (sat(base) < 80), 1.5) * (1 - box(PX(130), 0, PX(280), ART_H, 4))
    return compose(base,
                   fx_blink(rng, cyan, glow=(140, 235, 255), thr=0.3, steady=0.2, blink=0.2),
                   fx_packets(*P(205, 300), *P(205, 15), (150, 240, 255), amt=0.7, n=2, width=1.2),
                   fx_drift(rng, mist, amp=0.1, sigma=6, vx=1))


@monument("kyro", "DS/Kyro.jpg", CB, sat=1.1, shadow=(10, 16, 18))
def kyro(base, rng):
    """The Kyro Sodality's monument - a glass reliquary of green fire standing in a pool of molten light. Moves: the green
    fire rises inside the glass, bubbles drift up through it, the molten pool flickers, a glint runs down the glass."""
    l = lum(base)
    green = soft(hue_mask(base, (80, 255, 150), 40, 80), 0.8)
    glass = np.clip((l - 80) / 90, 0, 1) * box(PX(130), PY(40), PX(250), PY(250), 2)
    pool = soft(hue_mask(base, (255, 140, 40), 40, 70), 0.8) * box(0, PY(240), ART_W, ART_H, 3)
    return compose(base,
                   fx_fire(rng, green, amp=0.25, hot=(200, 255, 210), hot_amt=0.2, sigma=2.0, vy=-1),
                   rising(rng, 14, PX(150), PX(235), PY(245), PY(70), (200, 255, 220), loops=(1, 2), sway=1.0, bright=(0.3, 0.6)),
                   fx_fire(rng, pool, amp=0.3, hot=(255, 210, 120), hot_amt=0.22),
                   fx_sweep(glass, (220, 255, 240), amt=0.3, angle=math.pi / 2, width=0.04))


@monument("lanefir", "DS/Lanefir.jpg", CB, sat=1.1, shadow=(8, 18, 22))
def lanefir(base, rng):
    """The Lanefir Remnants' monument - a vast library under a window onto a world, a teal beam falling to the floor.
    Moves: the beam shimmers down, motes rise through it, the world's clouds drift, stars twinkle."""
    l = lum(base)
    beam = soft(hue_mask(base, (80, 220, 200), 35, 70), 1.0) * box(PX(205), PY(130), PX(300), ART_H, 3)
    wx, wy = P(268, 72)
    world = np.clip((31 - radius(wx, wy)) / 3, 0, 1)
    return compose(base,
                   fx_pour(rng, beam, amp=0.18, sigma=1.4, vy=1, hot=(190, 255, 240), hot_amt=0.1),
                   rising(rng, 16, PX(215), PX(290), ART_H, PY(140), (190, 255, 235), loops=(1, 1), bright=(0.3, 0.6)),
                   fx_drift(rng, world, amp=0.08, sigma=3, vx=1),
                   fx_twinkle(rng, base, box(0, 0, PX(200), PY(60), 0), thr=20, min_l=60))


@monument("lizho", "DS/Li-Zho.jpg", CB, sat=1.1, shadow=(16, 16, 34), shadow_amt=0.35)
def lizho(base, rng):
    """The Li-Zho Dynasty's monument - a fortress palace on a crag under a pink sky and a teal moon. Moves: birds wheel
    past, the sky's haze drifts, the palace windows glimmer, the moon glows."""
    l = lum(base)
    sky = soft(l > 110, 2) * box(0, 0, ART_W, PY(250), 4) * (1 - box(PX(110), PY(20), PX(300), PY(230), 6))
    return compose(base,
                   birds(rng, 9, PY(30), PY(110), d=1),
                   fx_drift(rng, sky, amp=0.14, sigma=6, vx=1),
                   fx_haze(rng, box(0, PY(230), ART_W, ART_H, 5), (255, 190, 200), amt=0.2),
                   fx_twinkle(rng, base, box(PX(110), PY(40), PX(300), PY(230), 0), thr=14, min_l=70),
                   fx_pulse(*P(325, 32), 9, 9, (170, 255, 240), 0.03, 0.15, f=1))


@monument("mirveda", "DS/Mirveda.jpg", CB, sat=1.1, shadow=(20, 12, 14))
def mirveda(base, rng):
    """The Mirveda Protectorate's monument - a grey bastion in a red canyon. Moves: dust blows through the canyon, the
    bastion's lamps blink, a gleam slides over its dome."""
    l = lum(base)
    canyon = soft(hue_mask(base, (220, 110, 70), 40, 50), 1.5)
    dome = np.clip((l - 140) / 80, 0, 1) * box(PX(80), PY(10), PX(330), PY(150), 2)
    return compose(base,
                   fx_haze(rng, canyon, (235, 150, 110), amt=0.2, sigma=5, vx=1),
                   fx_blink(rng, bright_small(base, 160), glow=(255, 230, 200), thr=0.25),
                   fx_sweep(dome, (255, 250, 245), amt=0.35, angle=0.25, width=0.04))


@monument("mortheus", "DS/Mortheus.jpg", CB, sat=1.1, shadow=(14, 12, 40))
def mortheus(base, rng):
    """The Mortheus' monument - a chain of blue spheres hanging in a nebula, a star flaring behind. Moves: the nebula
    drifts, the star flares, lights run down the chain of spheres, stars twinkle, a glint crosses the great spheres."""
    l = lum(base)
    spheres = np.clip((l - 80) / 100, 0, 1) * hue_mask(base, (60, 90, 255), 45)
    return compose(base,
                   fx_drift(rng, soft(l > 60, 2), amp=0.12, sigma=6, vx=1),
                   fx_pulse(*P(290, 30), 7, 7, (255, 255, 255), 0.15, 0.6, f=2),
                   fx_rays(*P(290, 30), [k * TAU / 6 for k in range(6)], (230, 235, 255), 0.05, 0.25, width=0.04, reach=25, f=2),
                   fx_packets(*P(240, 120), *P(135, 280), (220, 235, 255), amt=0.8, n=2, width=1.4),
                   fx_twinkle(rng, base, box(0, 0, ART_W, ART_H, 0), thr=24, min_l=90),
                   fx_sweep(spheres, (220, 230, 255), amt=0.3, angle=0.6, width=0.05))


@monument("myko", "DS/Myko.jpg", CB, palette=52, dither=13.0, sat=1.1, shadow=(10, 18, 28))
def myko(base, rng):
    """The Myko-Mentori's monument - a mushroom tower with lit windows above a misty valley. Moves: the windows glow and
    fade, spores drift up into the dusk, mist creeps along the valley."""
    l = lum(base)
    windows = hue_mask(base, (255, 160, 80), 40, 110) * np.clip(bright_small(base, 100) * 3, 0, 1)
    return compose(base,
                   fx_blink(rng, windows, glow=(255, 200, 130), thr=0.3, steady=0.3, blink=0.2),
                   rising(rng, 44, 0, ART_W, ART_H, PY(30), (190, 240, 230), loops=(1, 2), sway=2.5, bright=(0.3, 0.7)),
                   fx_haze(rng, box(0, PY(200), ART_W, ART_H, 5), (150, 200, 210), amt=0.3),
                   fx_drift(rng, box(0, PY(150), ART_W, ART_H, 6), amp=0.12, sigma=4, vx=1, aniso=(2.5, 1.0)),
                   fx_pulse(*P(200, 30), 30, 8, (255, 200, 150), 0.02, 0.12, f=1))


@monument("nivyn", "DS/Nivyn.jpg", CB, sat=1.1, shadow=(12, 10, 20))
def nivyn(base, rng):
    """The Nivyn Star Kings' monument - a black hole wrapped in a blazing disk. Moves: bright knots race round the disk, its
    fire churns, motes spiral in, the heart flickers."""
    l = lum(base)
    cx, cy = P(213, 140)
    rr = radius(cx, cy, 1.0, 0.8)
    disk = np.clip((l - 100) / 100, 0, 1) * (rr > 12) * (rr < 75)
    return compose(base,
                   fx_drift(rng, disk, amp=0.15, sigma=2.5, vx=1, vy=1, aniso=(1.0, 1.0)),
                   fx_orbit(cx, cy, disk, (255, 235, 190), amt=0.45, n=3, width=0.22, squash=1.25),
                   fx_spiral(rng, cx, cy, 55, (255, 220, 170), n=10, squash=1.25),
                   fx_pulse(cx, cy, 3, 3, (255, 160, 190), 0.1, 0.5, f=4))


@monument("nokar", "DS/Nokar.jpg", CB, sat=1.1, shadow=(10, 16, 32))
def nokar(base, rng):
    """The Nokar Sellships' monument - a fortress port between cliffs on an icy sea. Moves: the port's lamps blink, the ice
    and water shimmer, craft cross the harbour, snow drifts."""
    l = lum(base)
    lamps = hue_mask(base, (255, 150, 60), 40, 100)
    water = soft(l > 110, 1.5) * box(0, PY(220), ART_W, ART_H, 3)
    return compose(base,
                   fx_blink(rng, lamps, glow=(255, 190, 110), thr=0.3, steady=0.35, blink=0.35),
                   fx_drift(rng, water, amp=0.2, sigma=2.5, vx=1, aniso=(3.0, 1.0)),
                   *lanes(rng, [(PY(205), 1), (PY(150), -1), (PY(100), 1)], n=3, colour=(255, 220, 170)),
                   fx_haze(rng, box(0, PY(180), ART_W, ART_H, 5), (170, 200, 230), amt=0.18),
                   snow(rng, 40, bright=(0.25, 0.55)))


@monument("olradin", "DS/Olradin.jpg", CB, sat=1.2, shadow=(14, 16, 18), shadow_amt=0.35)
def olradin(base, rng):
    """The Olradin League's monument - a sprawl of blocky grey towers. Moves: scattered lamps blink in the towers, a band of
    light sweeps across them, a thin fog drifts between them."""
    l = lum(base)
    blocks = np.clip((l - 120) / 90, 0, 1)
    return compose(base,
                   fx_blink(rng, bright_small(base, 150), glow=(230, 240, 200), thr=0.2, steady=0.3, blink=0.45),
                   fx_sweep(blocks, (240, 250, 235), amt=0.3, angle=0.35, width=0.05),
                   fx_haze(rng, box(0, 0, ART_W, ART_H, 0), (180, 190, 185), amt=0.1, sigma=6, vx=1))


@monument("rhodun_back", "DS/Rhodun Back.jpg", CB, palette=48, dither=12.0, sat=1.05, shadow=(14, 18, 34), shadow_amt=0.3)
def rhodun_back(base, rng):
    """The Rhodun's monument, its other face - a blazing sphere of energy above a ringed bridge. Moves: energy crackles over
    the sphere, arcs of lightning leap across it, the clouds drift around it."""
    l = lum(base)
    cx, cy = P(178, 95)
    sphere = np.clip((44 - radius(cx, cy)) / 4, 0, 1)
    clouds = soft(l > 120, 2) * (1 - sphere)
    strikes = [(cx - 30, cy - 20, cx + 5, cy + 30), (cx + 25, cy - 30, cx - 10, cy + 25), (cx - 10, cy - 38, cx + 30, cy + 10)]
    crackle = Flow(rng, 1.4, vx=1, vy=1, aniso=(1.0, 1.0))
    crackle2 = Flow(rng, 2.2, vx=-1, vy=1, aniso=(1.0, 1.0))

    def energy(img, t):
        n = np.abs(crackle(t)) + 0.6 * np.abs(crackle2(t))
        veins = np.clip(0.55 - n, 0, None) / 0.55  # thin bright lines where the fields cross zero
        add(img, sphere * veins, (215, 240, 255), 0.55)
    return compose(base,
                   energy,
                   fx_lightning(rng, sphere, strikes, times=(9, 30, 51), sky_colour=(220, 240, 255), sky_amt=0.08),
                   fx_drift(rng, clouds, amp=0.05, sigma=6, vx=1))


@monument("rhodun_front", "DS/Rhodun Front.jpg", CB, palette=48, dither=12.0, sat=1.05, shadow=(16, 20, 36), shadow_amt=0.3)
def rhodun_front(base, rng):
    """The Rhodun's monument - a floating city of bridges under a pale sphere. Moves: the city's lights
    glimmer, a glint slides over the sphere, birds glide past."""
    l = lum(base)
    cx, cy = P(230, 95)
    disc = np.clip((47 - radius(cx, cy)) / 5, 0, 1)
    sphere = disc * np.clip((l - 110) / 80, 0, 1)
    return compose(base,
                   fx_twinkle(rng, base, box(0, PY(150), ART_W, ART_H, 0), thr=14, min_l=90),
                   fx_sweep(sphere, (255, 255, 255), amt=0.22, angle=0.3, width=0.06),
                   birds(rng, 4, PY(150), PY(200), d=-1))


@monument("rohdhna", "DS/Roh_Dhna.jpg", CB, sat=1.1, shadow=(12, 14, 34))
def rohdhna(base, rng):
    """The Roh'Dhna Mechatronics' monument - a great glass sphere roaring fire from beneath, under a stormy sky. Moves: the
    exhaust pours and flickers, sparks fall, lightning cracks the clouds, a glint slides over the glass."""
    l = lum(base)
    exhaust = soft(hue_mask(base, (255, 140, 60), 42, 90), 0.8) * box(PX(130), PY(150), PX(270), ART_H, 3)
    glass = np.clip((l - 120) / 90, 0, 1) * (radius(*P(200, 120)) < 50) * (1 - exhaust)
    sky = box(0, 0, ART_W, PY(200), 6) * (radius(*P(200, 120)) > 52)
    return compose(base,
                   fx_fire(rng, exhaust, amp=0.3, hot=(255, 225, 150), hot_amt=0.25, sigma=1.6, vy=2),
                   rising(rng, 16, PX(150), PX(250), PY(230), ART_H + 2, (255, 180, 90), loops=(1, 2)),
                   fx_lightning(rng, sky, [(*P(40, 0), *P(70, 150)), (*P(320, 10), *P(300, 140))], times=(16, 45), sky_amt=0.07),
                   fx_sweep(glass, (230, 240, 255), amt=0.3, angle=0.35, width=0.05))


@monument("tnelis", "DS/Tnelis.jpg", CB, sat=1.1, shadow=(14, 18, 40), shadow_amt=0.3)
def tnelis(base, rng):
    """The Tnelis Syndicate's monument - a canyon of towers under a bright sky. Moves: clouds drift, air traffic streams
    between the towers, windows glimmer."""
    l = lum(base)
    clouds = soft((l > 180) & (sat(base) < 60), 1.5)
    return compose(base,
                   fx_drift(rng, clouds, amp=0.14, sigma=6, vx=1),
                   *lanes(rng, [(PY(100), 1), (PY(140), -1), (PY(190), 1), (PY(230), -1)], n=4, colour=(255, 240, 210), bright=(0.6, 1.0)),
                   birds(rng, 4, PY(40), PY(80), d=-1),
                   fx_twinkle(rng, base, box(0, PY(60), ART_W, ART_H, 0), thr=16, min_l=110))


@monument("vaden", "DS/Vaden.jpg", CB, sat=1.05, shadow=(10, 18, 16))
def vaden(base, rng):
    """The Vaden Banking Clans' monument - a city glowing green and gold. Moves: the city's lights flicker, a green haze
    drifts, light streams along the rails."""
    l = lum(base)
    return compose(base,
                   fx_blink(rng, bright_small(base, 140), glow=(240, 255, 190), thr=0.2, steady=0.4, blink=0.35),
                   fx_drift(rng, box(0, 0, ART_W, PY(230), 4), amp=0.08, sigma=6, vx=1),
                   *lanes(rng, [(PY(222), 1), (PY(248), -1)], n=4, colour=(150, 230, 255), bright=(0.6, 1.0)))


@monument("vaylerian", "DS/Vaylerian.jpg", CB, sat=1.1, shadow=(10, 14, 30))
def vaylerian(base, rng):
    """The Vaylerian Scourge's monument - a vast warship diving into a world's atmosphere, fire at its stern. Moves: the
    fire flickers and sheds sparks, the hull's lights blink, the world's clouds drift, fighters stream past, stars twinkle."""
    l = lum(base)
    fire = soft(hue_mask(base, (255, 140, 50), 40, 90), 0.8)
    clouds = soft(l > 130, 1.5) * box(0, PY(120), PX(200), ART_H, 4)
    return compose(base,
                   fx_fire(rng, fire, amp=0.3, hot=(255, 215, 140), hot_amt=0.25),
                   fx_blink(rng, bright_small(base, 160, box(PX(150), 0, PX(320), ART_H, 0)), glow=(255, 220, 190), thr=0.25),
                   fx_drift(rng, clouds, amp=0.1, sigma=6, vx=1),
                   fx_twinkle(rng, base, box(0, 0, ART_W, PY(110), 0), thr=22, min_l=70),
                   *lanes(rng, [(PY(60), -1)], n=3, colour=(200, 230, 255)))


@monument("veldyr", "DS/Veldyr.jpg", CB, sat=1.1, shadow=(18, 16, 24), shadow_amt=0.35)
def veldyr(base, rng):
    """The Veldyr Sovereignty's monument - a giant gun over a misty city, firing a beam of light. Moves: pulses run up the
    beam, the gun's blue lamps blink, the city's lights glimmer, mist drifts."""
    l = lum(base)
    blue = hue_mask(base, (60, 140, 255), 38, 90) * np.clip(bright_small(base, 90) * 3, 0, 1)
    return compose(base,
                   fx_packets(*P(262, 40), *P(350, -5), (240, 250, 255), amt=0.8, n=4, width=1.4),
                   rising(rng, 12, *P(250, 40)[:1], PX(270), PY(50), PY(10), (220, 240, 255), loops=(2, 3)),
                   fx_blink(rng, blue, glow=(150, 200, 255), thr=0.3, steady=0.3, blink=0.4),
                   fx_twinkle(rng, base, box(0, PY(200), ART_W, ART_H, 0), thr=14, min_l=100),
                   fx_haze(rng, box(0, PY(150), ART_W, ART_H, 5), (210, 200, 215), amt=0.25),
                   fx_drift(rng, box(0, 0, ART_W, PY(160), 5), amp=0.06, sigma=6, vx=1))


@monument("zelian", "DS/Zelian.jpg", CB, sat=1.05, shadow=(20, 8, 8))
def zelian(base, rng):
    """The Zelian Purifier's monument - ships threading an asteroid field lit orange by a nearby star. Moves: the star's
    light drifts over the field, rocks tumble past, the ships' engines glow."""
    l = lum(base)
    return compose(base,
                   fx_drift(rng, soft(l > 60, 2), amp=0.18, sigma=5, vx=-1),
                   fx_particles(Particles(rng, 34, -10, ART_W + 30, 0, ART_H, (-70, -50, -40), loops=(1, 2), dx=(-40, -20), sway=0.5, size=2,
                                          bright=(0.6, 1.0))),
                   fx_pulse(ART_W + 4, ART_H * 0.55, 40, 50, (255, 170, 80), 0.02, 0.2, f=2),
                   fx_sweep(np.clip((l - 70) / 90, 0, 1), (255, 210, 150), amt=0.3, angle=math.pi, width=0.06),
                   fx_blink(rng, bright_small(base, 170), glow=(255, 220, 170), thr=0.2, steady=0.0, blink=0.0))


# ================================================================== Twilight's Fall (Caco's Midjourney pictures)
@monument("blacktf", "TF/King Black.jpg", CB, palette=48, dither=13.0, sat=1.1, shadow=(20, 10, 26))
def blacktf(base, rng):
    """The Flesh Cathedral - a black cathedral wrapped in great tentacles over a burning city. Moves: the city's fires flicker
    and throw embers, the city lights glitter, lights glow and fade in the cathedral's windows, the dusk clouds roll."""
    l = lum(base)
    fire = soft(hue_mask(base, (255, 130, 50), 42, 90), 0.8) * box(0, PY(225), ART_W, ART_H, 3)
    sky = box(0, 0, ART_W, PY(240), 6) * (1 - box(PX(80), PY(20), PX(285), ART_H, 6))
    return compose(base,
                   fx_drift(rng, sky, amp=0.14, sigma=6, vx=1),
                   fx_fire(rng, fire, amp=0.35, hot=(255, 210, 130), hot_amt=0.25),
                   rising(rng, 26, 0, ART_W, ART_H, PY(170), (255, 150, 70), loops=(1, 2), bright=(0.35, 0.8)),
                   fx_twinkle(rng, base, box(0, PY(230), ART_W, ART_H, 0), thr=14, min_l=90),
                   fx_blink(rng, bright_small(base, 110, box(PX(110), PY(20), PX(240), PY(230), 0)), glow=(255, 190, 120), thr=0.2,
                            steady=0.3, blink=0.3, max_px=8))


@monument("bluetf", "TF/King Blue.jpg", CB, palette=48, dither=13.0, sat=1.1, shadow=(16, 16, 36))
def bluetf(base, rng):
    """Caracas - white blade-spires under a sky streaked with fire. Moves: the burning streaks drift across the sky, a gleam
    climbs the white spires, their red lamps blink, the pale sun breathes, small craft cross by the platforms."""
    l = lum(base)
    streaks = soft(hue_mask(base, (255, 110, 40), 42, 60), 1.0) * box(0, 0, ART_W, PY(210), 4)
    white = np.clip((l - 150) / 70, 0, 1) * (sat(base) < 60) * box(0, PY(20), ART_W, PY(270), 1)
    lamps = hue_mask(base, (255, 80, 40), 40, 90) * np.clip(bright_small(base, 90) * 3, 0, 1) * box(0, PY(80), ART_W, ART_H, 0)
    return compose(base,
                   fx_drift(rng, streaks, amp=0.25, sigma=2.5, vx=1, aniso=(4.0, 1.0)),
                   fx_sweep(white, (255, 255, 255), amt=0.45, angle=-math.pi / 2, width=0.04),
                   fx_blink(rng, lamps, glow=(255, 120, 80), thr=0.2, steady=0.2, blink=0.5, max_px=6),
                   fx_pulse(*P(30, 60), 5, 5, (255, 240, 210), 0.05, 0.35, f=1),
                   *lanes(rng, [(PY(252), 1), (PY(238), -1)], n=3, colour=(255, 220, 190)))


@monument("greentf", "TF/King Green.jpg", CB, palette=48, dither=13.0, sat=1.15, shadow=(14, 22, 30), shadow_amt=0.35)
def greentf(base, rng):
    """The Crown of Thorns - a spiked city rising from a green plain under a pale sun, debris hanging in the sky. Moves: the
    sun flares, a shaft of light pulses down onto the crown, the debris drifts, the city glitters, light ripples over the plain."""
    l = lum(base)
    sx, sy = P(195, 22)
    plain = box(0, PY(120), ART_W, ART_H, 4) * (1 - box(PX(130), PY(90), PX(260), PY(235), 6))
    return compose(base,
                   fx_pulse(sx, sy, 9, 7, (240, 250, 255), 0.05, 0.35, f=2),
                   fx_rays(sx, sy, [k * TAU / 8 + 0.2 for k in range(8)], (220, 240, 255), 0.02, 0.12, width=0.05, reach=25, f=2),
                   fx_packets(sx, sy + 4, *P(195, 92), (230, 255, 240), amt=0.6, n=2, width=1.2, size=2.0),
                   fx_particles(Particles(rng, 24, -5, ART_W + 5, 0, PY(130), (-60, -60, -50), loops=(1, 1), dx=(10, 24), sway=1.0, size=1,
                                          bright=(0.5, 1.0))),
                   fx_twinkle(rng, base, box(PX(120), PY(90), PX(270), PY(240), 0), thr=14, min_l=70),
                   fx_drift(rng, plain, amp=0.12, sigma=2.5, vx=1, aniso=(3.0, 1.0)))


@monument("orangetf", "TF/King Orange.jpg", CB, palette=50, dither=12.0, sat=1.1, shadow=(14, 18, 44), shadow_amt=0.3)
def orangetf(base, rng):
    """The Starlit Redoubt - a vast black monolith full of stars standing over a desert city. Moves: the stars inside the
    monolith twinkle and drift, a gleam runs down its edge, the clouds drift, heat shimmers over the desert."""
    l = lum(base)
    mono = soft((l < 70).astype(np.float32), 0.8) * box(PX(140), PY(10), PX(300), PY(270), 2)
    clouds = soft((l > 170) & (sat(base) < 70), 1.5) * (1 - mono)
    edge = np.clip(np.abs(ndimage.sobel(l, axis=1)) / 150, 0, 1) * box(PX(140), PY(10), PX(300), PY(260), 2)
    return compose(base,
                   fx_twinkle(rng, base, mono > 0.5, thr=10, min_l=40, depth=0.95),
                   fx_drift(rng, mono, amp=0.25, sigma=3, vx=0, vy=1, aniso=(1.0, 1.0)),
                   fx_sweep(edge, (255, 250, 240), amt=0.5, angle=math.pi / 2, width=0.05),
                   fx_drift(rng, clouds, amp=0.1, sigma=6, vx=1),
                   fx_haze(rng, box(0, PY(255), ART_W, ART_H, 3), (255, 225, 180), amt=0.18, sigma=3, vx=1, aniso=(4.0, 1.0)))


@monument("pinktf", "TF/King Pink.jpg", CB, palette=50, dither=12.0, sat=1.1, shadow=(14, 18, 40), shadow_amt=0.3)
def pinktf(base, rng):
    """Avatar of Mirrors - a vast egg-shaped citadel hanging over a rocky shore in a storm. Moves: its windows blink, a gleam
    slides over its hull, the storm clouds roll, lightning flickers in the distance."""
    l = lum(base)
    egg = np.clip((48 - radius(*P(200, 150), 1.0, 1.9)) / 3, 0, 1)
    hull = np.clip((l - 120) / 90, 0, 1) * egg
    sky = box(0, 0, ART_W, PY(235), 5) * (1 - soft(egg, 2))
    return compose(base,
                   fx_drift(rng, sky, amp=0.14, sigma=6, vx=1),
                   fx_blink(rng, bright_small(base, 140) * egg, glow=(230, 240, 255), thr=0.2, steady=0.3, blink=0.4, max_px=6),
                   fx_sweep(hull, (255, 255, 255), amt=0.4, angle=0.4, width=0.04),
                   fx_lightning(rng, sky * box(0, 0, PX(120), PY(200), 3), [(*P(30, 30), *P(60, 190)), (*P(320, 40), *P(300, 170))],
                                times=(18, 47), sky_amt=0.08))


@monument("purpletf", "TF/King Purple.jpg", CB, palette=48, dither=13.0, sat=1.1, shadow=(12, 10, 34))
def purpletf(base, rng):
    """Halo Cortex - a spiked ring around a blazing violet core, above the limb of a world. Moves: the core pulses and throws
    rays, lights race round the ring, the lamps on its spires twinkle, the world's clouds drift, stars twinkle."""
    l = lum(base)
    cx, cy = P(195, 158)
    rr = radius(cx, cy, 1.0, 0.45)
    ringm = np.clip((l - 70) / 90, 0, 1) * (rr > 36) * (rr < 58)
    world = box(PX(290), PY(150), ART_W, ART_H, 4) * (l > 70)
    return compose(base,
                   fx_pulse(cx, cy, 6, 5, (220, 170, 255), 0.1, 0.5, f=2),
                   fx_rays(cx, cy, [k * TAU / 6 for k in range(6)], (220, 190, 255), 0.03, 0.18, width=0.05, reach=18, f=2),
                   fx_orbit(cx, cy, ringm, (255, 230, 200), amt=0.6, n=3, width=0.25, squash=2.2),
                   fx_twinkle(rng, base, box(PX(90), PY(60), PX(310), PY(220), 0), thr=16, min_l=80),
                   fx_drift(rng, world, amp=0.12, sigma=3, vx=1, aniso=(3.0, 1.0)),
                   fx_twinkle(rng, base, (rr > 62) * (1 - box(PX(290), PY(150), ART_W, ART_H, 0)), thr=22, min_l=60))


@monument("redtf", "TF/King Red.jpg", CB, palette=44, dither=14.0, sat=1.05, shadow=(26, 6, 12), shadow_amt=0.35)
def redtf(base, rng):
    """The Serrated Throne - a black cathedral of spires against a blood-red sky. Moves: the red sky churns, light climbs the
    spires' edges, a red glow runs up the central spire, embers drift up from below."""
    l = lum(base)
    sky = soft(hue_mask(base, (220, 40, 30), 35, 60), 1.5)
    return compose(base,
                   fx_drift(rng, sky, amp=0.14, sigma=5, vx=1),
                   fx_climb(rng, base, box(PX(110), PY(20), PX(270), PY(260), 0), colour=(255, 120, 90), amt=0.7, maxn=7),
                   fx_packets(*P(200, 250), *P(200, 25), (255, 110, 80), amt=0.8, n=2, width=1.0, size=3.0),
                   rising(rng, 22, 0, ART_W, ART_H, PY(120), (255, 120, 70), loops=(1, 2), bright=(0.3, 0.7)))


@monument("whitetf", "TF/King White.jpg", CB, palette=56, dither=13.0, sat=1.1, shadow=(26, 24, 34), shadow_amt=0.3)
def whitetf(base, rng):
    """The Alabaster Edifice - a white wall set with golden orbs and gold lines. Moves: light gleams across the orbs, the big
    orbs glow and fade in turn, small gold studs glint, soft light moves over the wall."""
    l = lum(base)
    gold = np.clip(hue_mask(base, (230, 160, 60), 35, 90) * 1.5, 0, 1) * np.clip((sat(base) - 60) / 40, 0, 1)
    return compose(base,
                   fx_drift(rng, box(0, 0, ART_W, ART_H, 0) * (1 - gold) * (lum(base) < 200), amp=0.04, sigma=8, vx=1, aniso=(1.5, 1.0)),
                   fx_sweep(gold, (255, 250, 220), amt=0.5, angle=0.6, width=0.05),
                   fx_pulse(*P(222, 128), 12, 12, (255, 236, 180), 0.0, 0.14, f=1),
                   fx_pulse(*P(125, 205), 8, 8, (255, 236, 180), 0.0, 0.14, f=1, ph=2.1),
                   fx_pulse(*P(322, 250), 7, 7, (255, 236, 180), 0.0, 0.14, f=1, ph=4.2),
                   fx_twinkle(rng, base, gold > 0.3, thr=10, min_l=140, depth=0.6))


KY = (0, 410, 640, 890)  # lower than usual, so the water under the spire is in the picture


@monument("yellowtf", "TF/King Yellow.jpg", KY, palette=48, dither=13.0, sat=1.1, shadow=(16, 14, 24))
def yellowtf(base, rng):
    """Aura Vault - a golden spire in a dark canyon above still water. Moves: light runs up the spire's golden seam, its lamps
    glimmer, the clouds drift between the cliffs, the water shimmers with its reflection, mist creeps at its foot."""
    l = lum(base)

    def Q(sx, sy):
        return S(sx, sy, KY)
    spire = box(Q(150, 0)[0], 0, Q(250, 0)[0], Q(0, 262)[1], 3)
    clouds = soft((l > 90) & (sat(base) < 80), 2) * box(Q(90, 0)[0], 0, Q(320, 0)[0], Q(0, 200)[1], 6) * (1 - spire)
    water = box(0, Q(0, 262)[1], ART_W, ART_H, 2)
    return compose(base,
                   fx_packets(*Q(200, 262), *Q(200, 45), (255, 240, 170), amt=0.8, n=2, width=0.9, size=3.0),
                   fx_twinkle(rng, base, spire, thr=12, min_l=120),
                   fx_drift(rng, clouds, amp=0.12, sigma=6, vx=1),
                   fx_drift(rng, water, amp=0.22, sigma=1.5, vx=1, aniso=(4.0, 0.6)),
                   fx_packets(*Q(200, 266), *Q(200, 300), (255, 230, 150), amt=0.5, n=2, width=1.2, size=2.0),
                   fx_haze(rng, box(0, Q(0, 240)[1], ART_W, Q(0, 268)[1], 3), (200, 180, 150), amt=0.18))
