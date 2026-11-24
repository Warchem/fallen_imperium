"""Blue Reverie codec portraits (0.83.0 drafts, owner 2026-11-23 18:33: "Let's make the negotiation portrait drafts for each"),
drawn after Cacotopos's portraits (the owner's folder of Blue Reverie portrait references) -
the same pictures the unlock screen's art comes from (tools/gen_portrait_art.py). Drawn in code with the kit (pix.py), in the
round-2 style (faces_v2.py), never traced. Neutral expressions.

Usage: python3 faces_br.py OUTDIR  ->  sheet_faces_br.png (4x, frames t=0 and t=0.5) + anim_<key>.gif (4x)"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

from pix import (Portrait, ramp, hexrgb, breath, sway, pulse, blink, mix, wave, bezier, ribbon, FRAMES,  # noqa: E402
                 save_gif)
from faces_v2 import painted_bg, bg_px  # noqa: E402


def stars(p, seed, n, box=(0, 0, 64, 64), t=0.0, col=(200, 210, 240)):
    """twinkling background stars (drawn into p.bg, so the figure covers them)"""
    rng = np.random.RandomState(seed)
    for i in range(n):
        x, y = rng.randint(box[0], box[2]), rng.randint(box[1], box[3])
        k = pulse(t, rng.uniform(0, 1), rng.randint(1, 3))
        if k > 0.25:
            bg_px(p, x, y, mix(tuple(int(v) for v in p.bg[y, x]), col, 0.35 + 0.65 * k))


def sweep_over(p, mask, t, col, width=2.0, angle=0.8, span=0.5, start=0.0, amount=0.6):
    """a band of light crossing `mask` diagonally once per loop (during `span` of it)"""
    s = (t + start) % 1.0
    if s > span:
        return
    s /= span
    proj = p.xx * math.cos(angle) + p.yy * math.sin(angle)
    lo, hi = proj.min() - 3 * width, proj.max() + 3 * width
    c = lo + (hi - lo) * s
    k = np.exp(-((proj - c) ** 2) / (2 * width * width)) * mask
    hit = k > 0.15
    cc = np.array(hexrgb(col) if isinstance(col, str) else col, float)
    p.rgb[hit] = (p.rgb[hit] * (1 - k[hit, None] * amount) + cc * k[hit, None] * amount).astype(np.uint8)


def drift(p, t, pts, col_a, col_b, rise=50, cycles=1):
    """motes drifting upward (after the outline): pts = (x, y_start, phase)"""
    for x0, y0, ph in pts:
        u = (t * cycles + ph) % 1.0
        if u < 0.9:
            p.px(x0 + sway(t, ph, 1, 2), int(round(y0 - u * rise)), mix(col_a, col_b, u))


# ---------------------------------------------------------------------------------------------------------------
def energy(p, mask, t, ph=0.0, cycles=2, core=(235, 255, 255), mid=(60, 200, 255), deep=(20, 60, 170)):
    """blue energy showing through openings in the living metal: brightest along each opening's middle, pulsing"""
    from scipy import ndimage
    if not mask.any():
        return
    d = ndimage.distance_transform_edt(np.pad(mask, 1))[1:-1, 1:-1]
    k = np.clip(d / max(float(d.max()), 1.0), 0, 1)
    pl = pulse(t, ph, cycles)
    lev = np.clip(k * (0.7 + 0.5 * pl) + 0.15 * pl, 0, 1)
    c1, c2, c3 = (np.array(c, float) for c in (deep, mid, core))
    col = np.where(lev[..., None] < 0.5, c1 + (c2 - c1) * (lev[..., None] / 0.5), c2 + (c3 - c2) * ((lev[..., None] - 0.5) / 0.5))
    p.rgb[mask] = col[mask].astype(np.uint8)
    p.a |= mask
    # the glow spilling onto the metal round the opening
    ring = ndimage.binary_dilation(mask, iterations=1) & ~mask & p.a
    p.rgb[ring] = (p.rgb[ring] * (0.55 - 0.15 * pl) + np.array(mid, float) * (0.45 + 0.15 * pl)).astype(np.uint8)


def liquid(p, mask, t, amp=0.28, col=(255, 236, 170)):
    """living metal: bands of light flowing slowly over the shell (a slanted ripple drifting once per loop)"""
    w = np.sin(p.xx * 0.55 + p.yy * 0.35 - 2 * math.pi * t) * np.sin(p.xx * 0.21 - p.yy * 0.47 + 2 * math.pi * t + 1.3)
    k = np.clip(w, 0, 1) * mask
    hit = k > 0.35
    cc = np.array(col, float)
    p.rgb[hit] = (p.rgb[hit] * (1 - k[hit, None] * amp) + cc * k[hit, None] * amp).astype(np.uint8)
    dk = np.clip(-w, 0, 1) * mask
    hd = dk > 0.6
    p.rgb[hd] = (p.rgb[hd] * (1 - dk[hd, None] * 0.18)).astype(np.uint8)


def atokera(t=0.0):
    """after the owner's picture: the Atokera - a being of shifting living metal: an angular gold shell with plates that drift and
    flow, and through openings in it the blue energy inside, pulsing (owner 2026-11-23: "The blue isn't a visor but pulsing energy
    being exposed from inside the living metal shell"); a swept-back crest of metal blades; a teal haze and stars behind.
    Moves: breathing, light flows over the metal, plates and crest blades drift a pixel, gold strands close and open over the
    openings, the energy pulses, motes of it leak out."""
    p = Portrait()
    painted_bg(p, (10, 18, 34), (40, 104, 126), 10, 24, 46, steps=6)
    stars(p, 7, 26, t=t, col=(190, 240, 255))
    gold = ramp('#c8962e', spread=0.72, hue_shift=0.05)
    core = ramp('#1a1830', spread=0.6)
    body, head = breath(t), breath(t, -0.1)
    golds = np.zeros((64, 64), bool)
    with p.at(0, body):
        # inside the shell: dark, lit from within
        p.fill(p.poly([(4, 66), (6, 48), (16, 40), (28, 37), (42, 37), (52, 41), (60, 50), (62, 66)]), core, radius=6, depth=5,
               texture=0.06)
        # the shell: plates that drift a pixel as the metal shifts
        plates = [([(0, 52), (1, 40), (6, 30), (12, 33), (22, 34), (27, 41), (18, 47), (8, 52)], 0.12, 0.0, 4),
                  ([(2, 57), (8, 50), (18, 48), (16, 56), (6, 61)], 0.0, 0.3, 2.5),
                  ([(45, 40), (53, 35), (61, 38), (64, 50), (56, 52), (49, 46)], -0.1, 0.55, 3),
                  ([(21, 42), (32, 39), (33, 51), (23, 53)], 0.08, 0.15, 3),
                  ([(36, 40), (47, 43), (47, 52), (36, 51)], -0.12, 0.7, 3),
                  ([(24, 55), (44, 55), (43, 59), (25, 59)], 0.0, 0.4, 1.5),
                  ([(26, 61), (42, 61), (41, 66), (27, 66)], -0.08, 0.85, 1.5),
                  ([(48, 55), (58, 53), (60, 66), (50, 66)], -0.15, 0.25, 2.5)]
        for poly, b, ph, r in plates:
            with p.at(sway(t, ph, 1), 0):
                golds |= p.fill(p.poly(poly), gold, radius=r, depth=3.5, bias=b, texture=0.05)
        p.stroke([(3, 42), (13, 36), (22, 37)], gold[4])
        for pts, ph in (([(6, 44), (11, 41), (15, 43)], 0.3), ([(52, 40), (56, 44), (55, 48)], 0.7)):  # veins in the pauldrons
            energy(p, p.line_mask(pts, 1) & golds, t, ph, cycles=2)
        # the energy between the breastplates and under the pauldron
        gaps = p.poly([(33, 41), (36, 41), (36, 52), (33, 52)]) | p.poly([(20, 47), (27, 43), (28, 45), (22, 49)])
        gaps &= ~golds
        energy(p, gaps, t, 0.1)
    with p.at(0, head):
        # the neck: dark, a seam of energy
        p.fill(p.poly([(30, 27), (41, 27), (42, 38), (29, 38)]), core, radius=2, depth=2)
        energy(p, p.poly([(34, 28), (37, 28), (37, 37), (34, 37)]), t, 0.2)
        # the crest: blades sweeping back toward the viewer's left, each drifting on its own
        for poly, b, ph in (([(26, 8), (13, 1), (16, 6), (24, 12)], 0.15, 0.0), ([(24, 13), (9, 10), (12, 14), (23, 17)], 0.05, 0.33),
                            ([(24, 18), (11, 19), (14, 22), (25, 22)], -0.05, 0.66)):
            with p.at(sway(t, ph, 1), 0):
                golds |= p.fill(p.poly(poly), gold, radius=1.5, depth=2, bias=b)
        # the head-shell, turned right: a rounded gold dome
        hm = [(22, 14), (25, 6), (32, 2), (41, 2), (48, 6), (52, 13), (52, 21), (48, 28), (41, 31), (34, 30), (28, 26), (24, 20)]
        shell = p.fill(p.poly(hm), gold, radius=6, depth=6, texture=0.04)
        golds |= shell
        golds |= p.fill(p.poly([(28, 24), (22, 32), (33, 29)]), gold, radius=1.5, depth=2, bias=0.05)  # the jaw plate
        p.stroke([(27, 7), (38, 3)], gold[4])
        # the opening where the face would be: a ragged tear in the shell, the energy inside
        tear = p.poly([(37, 11), (40, 8), (43, 10), (46, 7), (49, 11), (51, 16), (48, 18), (50, 22), (45, 21), (42, 23), (40, 19),
                       (37, 18), (39, 15)])
        energy(p, tear, t, 0.0, cycles=2)
        # living metal strands across the tear, closing and opening
        for (x0, y0, x1, y1, ph) in ((38, 13, 50, 15, 0.0), (39, 18, 49, 17, 0.5)):
            dy = sway(t, ph, 1)
            p.stroke([(x0, y0 + dy), ((x0 + x1) // 2, (y0 + y1) // 2 - dy), (x1, y1 + dy)], gold[2])
        # veins of energy through the shell - the light is inside it
        for pts, ph in (([(29, 6), (32, 10), (31, 14), (34, 17)], 0.4), ([(46, 24), (42, 26), (39, 29)], 0.6),
                        ([(26, 16), (30, 19)], 0.8)):
            energy(p, p.line_mask(pts, 1) & shell, t, ph, cycles=2)
    liquid(p, golds, t)
    p.rim('#5ae6ff', side=1, amount=0.35)
    p.rim('#ff60c8', side=-1, amount=0.18)
    p.outline('#0a0810')
    # motes of energy leaking out of the tear and the chest
    drift(p, t, ((48, 18, 0.0), (51, 14, 0.4), (35, 48, 0.2), (37, 44, 0.7)), (200, 250, 255), (40, 120, 220), rise=16, cycles=2)
    return p


# ---------------------------------------------------------------------------------------------------------------
def belkosea(t=0.0):
    """after the owner's picture, as the owner asked (2026-11-23: "regal, menacing, and dominant. Think of a star wars menacing
    officer"): a Belkosea grand officer - a gaunt, hard face, chin raised, looking down his nose at the viewer with narrowed cold
    eyes and a set mouth; grey hair swept back; a crisp white uniform with a tall collar, gold shoulder boards, a rank plaque and
    a dark belt; a cold command deck behind, a red alert light behind him and snow beyond the window.
    Moves: breathing, a slow blink, the red light pulses, the snow falls past the window, the deck lights blink."""
    p = Portrait()
    painted_bg(p, (8, 10, 18), (60, 24, 36), 32, 20, 40, steps=6)
    # the window: a tall dark pane with falling snow, the frame's struts
    for y in range(0, 50):
        for x in range(8, 56):
            if (x - 8) % 16 in (0, 1):
                bg_px(p, x, y, (30, 34, 44))
    for y in range(48, 64):  # the deck's console line
        for x in range(64):
            bg_px(p, x, y, (20, 22, 30) if y > 50 else (40, 44, 56))
    for x in range(2, 64, 6):  # console lights
        k = pulse(t, x * 0.13, 2)
        if k > 0.45:
            bg_px(p, x, 52, mix((60, 70, 90), (255, 80, 70) if x % 4 else (120, 200, 255), k))
    for x0, ph in ((12, 0.0), (20, 0.3), (28, 0.6), (40, 0.15), (48, 0.45), (54, 0.75), (34, 0.9)):
        u = (t * 2 + ph) % 1.0
        y = int(u * 48)
        bg_px(p, (x0 - int(u * 4)) % 64, y, (190, 200, 222) if int(ph * 10) % 2 else (140, 150, 176))
    white = ramp('#d8dce0', spread=0.52, hue_shift=0.04)
    skin = [(96, 86, 100), (150, 126, 126), (196, 168, 156), (224, 198, 184), (242, 224, 212)]  # pale, cold shadows
    hair = ramp('#8c9096', spread=0.62)
    gold = ramp('#c8a040', spread=0.6, hue_shift=0.04)
    dark = ramp('#24262e', spread=0.55)
    red = pulse(t, 0.0, 1)
    body, head = breath(t), breath(t, -0.1)
    with p.at(0, body):
        # the white uniform: broad, square shoulders
        p.fill(p.poly([(0, 66), (1, 50), (10, 43), (24, 40), (40, 40), (54, 43), (63, 50), (64, 66)]), white, radius=8, depth=6,
               texture=0.03)
        # gold shoulder boards with braid
        for side in (-1, 1):
            p.fill(p.poly([(32 + side * 13, 41), (32 + side * 27, 44), (32 + side * 27, 48), (32 + side * 14, 46)]), gold,
                   radius=1.5, depth=2, bias=0.1 if side < 0 else -0.05)
            for k in range(3):
                p.px(32 + side * (17 + k * 3), 45, gold[4])
            p.stroke([(32 + side * 25, 48), (32 + side * 24, 56)], gold[2])  # the braid hanging
        # the double-breasted front: a dark line, the rank plaque, the belt
        p.stroke([(32, 46), (32, 66)], white[1])
        p.stroke([(40, 46), (40, 60)], white[1])
        p.fill(p.poly([(14, 50), (24, 50), (24, 54), (14, 54)]), dark, radius=1, depth=1.5, bias=0.1)
        for i, c in enumerate(((220, 60, 60), (220, 60, 60), (70, 120, 220), (230, 200, 90), (70, 120, 220))):
            p.px(15 + i * 2, 52, c)
        p.fill(p.poly([(4, 60), (60, 60), (60, 64), (4, 64)]), dark, radius=1.5, depth=2)
        p.fill(p.poly([(29, 59), (35, 59), (35, 65), (29, 65)]), gold, radius=1, depth=1.5, bias=0.1)
    with p.at(0, head):
        # the neck and the tall collar
        p.fill(p.poly([(27, 28), (37, 28), (38, 38), (26, 38)]), skin, radius=2, depth=2, bias=-0.15)
        p.fill(p.poly([(21, 36), (27, 32), (37, 32), (43, 36), (42, 44), (32, 47), (22, 44)]), white, radius=4, depth=4, bias=0.05)
        p.stroke([(27, 33), (32, 38), (37, 33)], white[1])
        p.px(24, 39, gold[3]), p.px(40, 39, gold[3])  # collar pins
        # the head: front-on, chin raised - a long gaunt face, hollow cheeks, a hard jaw
        face = [(23, 11), (26, 5), (32, 3), (38, 5), (41, 11), (41, 19), (39, 26), (35, 31), (29, 31), (25, 26), (23, 19)]
        p.fill(p.poly(face), skin, radius=6, depth=4, texture=0.02, bias=0.08)
        for side in (-1, 1):  # hollow cheeks under the cheekbones
            p.stroke([(32 + side * 7, 21), (32 + side * 6, 24)], skin[2])
            p.px(32 + side * 6, 19, skin[4])
        # grey hair swept hard back, a high hairline
        p.fill(p.poly([(22, 12), (23, 5), (28, 1), (36, 1), (41, 5), (42, 12), (39, 7), (32, 6), (25, 7)]), hair, radius=3,
               depth=3, texture=0.08)
        for x in (27, 31, 35, 39):
            p.stroke([(x, 2), (x - 1, 6)], hair[3])
        p.fill(p.ellipse(22.5, 17, 1.6, 2.8), skin, radius=1, depth=1.5), p.fill(p.ellipse(41.5, 17, 1.6, 2.8), skin, radius=1,
                                                                                  depth=1.5, bias=-0.1)
        # brows drawn down hard toward the nose; narrowed eyes looking down at the viewer
        p.stroke([(25, 13), (30, 15)], (60, 56, 64)), p.stroke([(34, 15), (39, 13)], (60, 56, 64))
        p.pixels([(30, 15), (34, 15)], (60, 56, 64))
        p.stroke([(26, 17), (30, 17)], skin[1]), p.stroke([(34, 17), (38, 17)], skin[1])  # the heavy lids
        if blink(t, 0.6, 2):
            p.stroke([(26, 18), (30, 18)], skin[0]), p.stroke([(34, 18), (38, 18)], skin[0])
        else:
            p.pixels([(26, 18), (30, 18), (34, 18), (38, 18)], (210, 208, 210))
            p.pixels([(27, 18), (29, 18), (35, 18), (37, 18)], (226, 224, 226))
            p.pixels([(28, 18), (36, 18)], (30, 40, 56))
        p.stroke([(32, 17), (32, 23)], skin[3]), p.stroke([(33, 19), (33, 23)], skin[2])  # the long nose
        p.pixels([(31, 24), (33, 24)], skin[1])
        p.stroke([(29, 27), (35, 27)], (120, 84, 90))  # a thin, set mouth, the corners turned down
        p.pixels([(28, 28), (36, 28)], (130, 96, 100))
        p.stroke([(30, 29), (34, 29)], skin[3])
    p.rim(mix('#ff3040', '#ff7060', red), side=-1, amount=0.25 + 0.2 * red)
    p.rim('#9ad8ff', side=1, amount=0.25)
    p.outline('#08080c')
    return p


# ---------------------------------------------------------------------------------------------------------------
def pharadn(t=0.0):
    """after the owner's picture: a Pharadn priest - a bird's skull for a head (ivory, a long hooked beak pointing down, dark
    eye sockets), a golden halo behind it, a bronze collar of stacked rings, a tattered white cloak over the shoulders and
    a dark robe with gold fittings beneath. Neutral.
    Moves: light travels round the halo, breathing, the cloak's hem stirs, a faint light in the sockets, ash drifts."""
    p = Portrait()
    painted_bg(p, (26, 22, 22), (78, 64, 46), 32, 18, 40, steps=6)
    stars(p, 11, 18, t=t, col=(230, 210, 170))
    ivory = ramp('#e2d6bc', spread=0.55, hue_shift=0.05)
    gold = ramp('#b8862e', spread=0.72, hue_shift=0.05)
    cloak = ramp('#d8ccb4', spread=0.6, hue_shift=0.04)
    robe = ramp('#2a2420', spread=0.6)
    body, head = breath(t), breath(t, -0.1)
    # the halo: a gold ring behind the head (not breathing - it hangs in the air), light travelling round it
    cx, cy = 32, 17
    ring_m = np.zeros((64, 64), bool)
    d = np.sqrt(((p.xx + 0.5 - cx) / 17.0) ** 2 + ((p.yy + 0.5 - cy) / 16.0) ** 2)
    ring_m = (d > 0.84) & (d < 1.0)
    p.fill(ring_m, gold, radius=1.5, depth=2, bias=0.05)
    th = 2 * math.pi * t
    ang = np.arctan2(p.yy + 0.5 - cy, p.xx + 0.5 - cx)
    dd = (ang - th + math.pi) % (2 * math.pi) - math.pi
    k = np.exp(-(dd * dd) / (2 * 0.35 ** 2)) * ring_m
    hit = k > 0.1
    p.rgb[hit] = (p.rgb[hit] * (1 - k[hit, None] * 0.7) + np.array((255, 240, 180), float) * k[hit, None] * 0.7).astype(np.uint8)
    inner = (d > 0.78) & (d <= 0.84)
    p.rgb[inner & ~p.a] = np.array(gold[0], np.uint8)
    p.a |= inner
    with p.at(0, body):
        # the white cloak over both shoulders, ragged at the hem
        hem = [(0, 66), (2, 50), (8, 40), (18, 35), (26, 36), (24, 50), (22, 66)]
        p.fill(p.poly([(x + (sway(t, 0.1, 1) if y > 56 else 0), y) for x, y in hem]), cloak, radius=6, depth=5, texture=0.08)
        hem2 = [(64, 66), (62, 50), (56, 40), (46, 35), (38, 36), (40, 50), (42, 66)]
        p.fill(p.poly([(x + (sway(t, 0.4, 1) if y > 56 else 0), y) for x, y in hem2]), cloak, radius=6, depth=5, bias=-0.1,
               texture=0.08)
        for pts in ([(6, 46), (10, 64)], [(14, 42), (16, 60)], [(52, 44), (56, 62)], [(46, 40), (48, 58)]):
            p.stroke(pts, cloak[1])
        for x, y in ((8, 52), (12, 48), (50, 50), (56, 54), (18, 58)):  # worn gold-brown streaks
            p.stroke([(x, y), (x + 3, y + 1)], (150, 120, 70))
        # the dark robe between, gold fittings down the front
        p.fill(p.poly([(24, 38), (40, 38), (42, 66), (22, 66)]), robe, radius=4, depth=4, texture=0.05)
        p.stroke([(32, 40), (32, 66)], gold[2])
        for y in (44, 50, 56, 62):
            p.fill(p.poly([(29, y), (35, y), (34, y + 3), (30, y + 3)]), gold, radius=1, depth=1.5, bias=0.1)
    with p.at(0, head):
        # the collar: bronze rings stacked round the neck
        for (ry, rx, yy, b) in ((4, 12, 36, -0.05), (3.5, 10, 33, 0.05), (3, 8, 30, 0.1)):
            p.fill(p.ellipse(32, yy, rx, ry), gold, radius=2, depth=2.5, bias=b, texture=0.04)
            p.stroke([(32 - rx + 2, yy), (32 + rx - 2, yy)], gold[1])
        p.fill(p.poly([(28, 20), (36, 20), (37, 29), (27, 29)]), robe, radius=2, depth=2)  # the neck in shadow
        # the skull: a rounded cranium, the long beak tapering down to a hooked point at the collar
        p.fill(p.ellipse(32, 9, 7.5, 7), ivory, radius=5, depth=5, texture=0.03)
        beak = [(25, 11), (39, 11), (38, 15), (36, 20), (34, 25), (33, 29), (31, 30), (30, 27), (28, 20), (26, 15)]
        p.fill(p.poly(beak), ivory, radius=3, depth=3.5, texture=0.03)
        p.stroke([(32, 14), (32, 28)], ivory[1])
        p.stroke([(34, 18), (33, 27)], ivory[3])
        p.pixels([(31, 30), (30, 29)], ivory[0])
        # the eye sockets: deep, a faint light far inside
        for ex in (28.5, 35.5):
            p.fill(p.ellipse(ex, 11.5, 2.6, 2.2), robe, radius=1, depth=1, flat=0, edge=False)
        g = pulse(t, 0.25, 1) ** 2
        if g > 0.3:
            p.px(28, 12, mix(robe[1], (120, 220, 255), g)), p.px(35, 12, mix(robe[1], (120, 220, 255), g))
        p.pixels([(28, 4), (29, 3), (30, 3)], ivory[4])
        p.stroke([(37, 4), (38, 7)], ivory[1])  # a crack
    p.rim('#ff8ad8', side=1, amount=0.25)
    p.rim('#7ad0ff', side=-1, amount=0.15)
    p.outline('#0e0a08')
    drift(p, t, ((10, 62, 0.0), (54, 60, 0.4), (20, 50, 0.7), (46, 56, 0.2)), (255, 190, 110), (90, 60, 40), rise=40)
    return p


# ---------------------------------------------------------------------------------------------------------------
def qhet(t=0.0):
    """after the owner's picture: a Qhet envoy - a dark armoured figure: a smooth dark helmet with big round ear-discs, a
    visor of glowing eyes, a tall upturned collar trimmed in gold, a dark plated coat with gold trim lines and small
    orange lights; stars behind. Neutral.
    Moves: breathing, the eyes shift cyan -> magenta and back, light runs along the gold trim, the chest lights glow."""
    p = Portrait()
    painted_bg(p, (6, 10, 22), (34, 40, 76), 32, 14, 40, steps=6)
    stars(p, 5, 30, t=t)
    armour = ramp('#3c3e48', spread=0.7, hue_shift=0.05)
    helm = ramp('#2c2e38', spread=0.7, hue_shift=0.04)
    silver = ramp('#9aa0aa', spread=0.6)
    gold = ramp('#d0a23a', spread=0.6, hue_shift=0.04)
    body, head = breath(t), breath(t, -0.1)
    trim = np.zeros((64, 64), bool)
    with p.at(0, body):
        # the coat: wide plated shoulders with short spikes
        p.fill(p.poly([(0, 66), (1, 46), (8, 38), (20, 34), (44, 34), (56, 38), (63, 46), (64, 66)]), armour, radius=7, depth=6,
               texture=0.05)
        for sx in (6, 10, 54, 58):
            p.fill(p.poly([(sx - 2, 41), (sx, 35), (sx + 2, 41)]), armour, radius=1, depth=1.5, bias=0.1)
        for pts in ([(10, 46), (20, 44)], [(44, 44), (54, 46)], [(12, 52), (22, 50)], [(42, 50), (52, 52)]):
            p.stroke(pts, armour[4])
            p.stroke([(a, b + 1) for a, b in pts], armour[0])
        # the front: a dark seam, gold trim lines curling down
        p.stroke([(32, 38), (32, 66)], armour[0])
        for pts in ([(25, 40), (24, 50), (27, 56), (26, 64)], [(39, 40), (40, 50), (37, 56), (38, 64)], [(20, 58), (24, 60)],
                    [(44, 58), (40, 60)]):
            trim |= p.line_mask(pts, 1)
            p.stroke(pts, gold[2])
        # the small orange lights low on the coat
        k = pulse(t, 0.1, 3)
        p.glow(8, 58, '#fff0d0', mix((200, 80, 30), (255, 150, 70), k), r=1, strength=0.5 + 0.4 * k)
        p.glow(56, 58, '#fff0d0', mix((200, 80, 30), (255, 150, 70), k), r=1, strength=0.5 + 0.4 * k)
    with p.at(0, head):
        # the tall upturned collar standing behind the head, gold edged
        for side in (-1, 1):
            col = [(32 + side * 8, 38), (32 + side * 17, 20), (32 + side * 20, 18), (32 + side * 19, 26), (32 + side * 13, 42)]
            p.fill(p.poly(col), armour, radius=2.5, depth=3, bias=0.1 if side < 0 else -0.12)
            pts = [(32 + side * 17, 20), (32 + side * 20, 18), (32 + side * 19, 26), (32 + side * 13, 42)]
            trim |= p.line_mask(pts, 1)
            p.stroke(pts, gold[3])
        p.fill(p.poly([(26, 26), (38, 26), (39, 36), (25, 36)]), helm, radius=2, depth=2)  # the neck seal
        # the helmet: smooth dome, a ridge down the middle, the face plate
        hm = [(22, 14), (25, 5), (32, 1), (39, 5), (42, 14), (41, 24), (37, 31), (27, 31), (23, 24)]
        p.fill(p.poly(hm), helm, radius=6, depth=6, texture=0.03)
        p.stroke([(32, 2), (32, 11)], helm[4])
        p.pixels([(27, 6), (37, 6), (28, 9), (36, 9)], helm[1])
        p.fill(p.poly([(25, 20), (39, 20), (37, 29), (27, 29)]), helm, radius=2, depth=2, bias=-0.1)  # the face plate
        for y in (23, 26):
            p.stroke([(28, y), (36, y)], helm[0])
        # the ear-discs at eye height
        for ex in (20, 44):
            p.fill(p.ellipse(ex, 16, 4.4, 5.0), silver, radius=2.2, depth=3, bias=0.1 if ex < 32 else -0.1)
            p.fill(p.ellipse(ex, 16, 2.2, 2.8), helm, radius=1, depth=1)
            p.px(ex, 16, mix((60, 160, 255), (200, 240, 255), pulse(t, 0.5, 2)))
        # the eyes: two bright slits glowing cyan -> magenta -> cyan (cyan at the loop's start)
        k = 0.5 - 0.5 * math.cos(2 * math.pi * t)
        core = mix((200, 250, 255), (255, 200, 250), k)
        halo = mix((40, 190, 255), (240, 70, 230), k)
        for x0 in (26, 34):
            p.glow(x0 + 1, 16, core, halo, r=1, strength=1.0)
            p.glow(x0 + 3, 16, core, halo, r=1, strength=1.0)
            p.stroke([(x0, 16), (x0 + 4, 16)], core)
            p.pixels([(x0 + 1, 15), (x0 + 3, 15)], mix(halo, core, 0.5))
    sweep_over(p, trim, t, (255, 248, 190), width=2.0, angle=1.4, span=0.6, amount=0.8)
    p.rim('#ff60d8', side=1, amount=0.3)
    p.rim('#60c8ff', side=-1, amount=0.2)
    p.outline('#06060c')
    return p


# ---------------------------------------------------------------------------------------------------------------
def uydai(t=0.0):
    """after the owner's picture: an Uydai - a fox-like face (pale fur, a long snout, a black nose) painted with blue markings,
    glowing blue eyes, a big round goggle lens on the forehead; an ochre hood with tall furry ears through it; stars behind.
    Neutral. Moves: breathing, a glint crosses the lens, the eyes glow, the ear fur stirs."""
    p = Portrait()
    painted_bg(p, (10, 14, 28), (34, 54, 74), 32, 10, 42, steps=6)
    stars(p, 3, 24, t=t)
    hood = ramp('#c8984c', spread=0.66, hue_shift=0.05)
    fur = ramp('#e6dccc', spread=0.55, hue_shift=0.04)
    ear = ramp('#b8aca0', spread=0.6)
    blue = ramp('#2a8ac8', spread=0.6, hue_shift=0.03)
    dark = ramp('#14161e', spread=0.5)
    metal = ramp('#5a5e68', spread=0.65)
    body, head = breath(t), breath(t, -0.1)
    with p.at(0, body):
        # the hood and its folds round the shoulders
        hd = [(2, 66), (4, 44), (9, 28), (17, 16), (32, 10), (47, 16), (55, 28), (60, 44), (62, 66)]
        p.fill(p.poly(hd), hood, radius=9, depth=7, texture=0.06)
        for pts in ([(8, 44), (12, 66)], [(13, 34), (17, 58)], [(56, 44), (52, 66)], [(51, 34), (47, 58)]):
            p.stroke(pts, hood[1])
        p.stroke([(10, 46), (14, 64)], hood[3])
        # the dark inside of the hood
        p.fill(p.poly([(18, 22), (25, 14), (39, 14), (46, 22), (48, 40), (42, 60), (22, 60), (16, 40)]), dark, radius=3, depth=2,
               edge=False)
    with p.at(0, head):
        # the ears through the hood: tall and wide, grey-brown, pale fur at the edges stirring
        for side in (-1, 1):
            tip = (32 + side * 28 + sway(t, 0.2 if side < 0 else 0.6, 1), 0)
            e = [(32 + side * 9, 17), tip, (32 + side * 29, 9), (32 + side * 24, 24)]
            p.fill(p.poly(e), ear, radius=3, depth=3, bias=0.1 if side < 0 else -0.1, texture=0.1)
            # the pale fur inside, tufts at the edge
            p.fill(p.poly([(32 + side * 13, 17), (tip[0] - side * 2, tip[1] + 4), (32 + side * 23, 19)]), fur, radius=2, depth=2,
                   texture=0.15, edge=False)
            for k in range(5):
                p.px(32 + side * (14 + k * 3) + sway(t, 0.3 + k * 0.2, 1), 16 - k * 3, fur[4])
                p.px(32 + side * (15 + k * 3), 17 - k * 3, fur[2])
    with p.at(0, head):
        # the face: pale fur, a long snout down to the black nose
        face = [(19, 26), (24, 18), (32, 15), (40, 18), (45, 26), (45, 34), (40, 42), (36, 53), (28, 53), (24, 42), (19, 34)]
        p.fill(p.poly(face), fur, radius=6, depth=5, texture=0.04)
        # the blue markings: a band across the eyes, a stripe down the snout, cheek bands, the brow under the lens
        for poly in ([(21, 28), (29, 27), (30, 32), (23, 34)], [(35, 27), (43, 28), (41, 34), (34, 32)],
                     [(31, 33), (33, 33), (33, 46), (31, 46)], [(24, 38), (27, 39), (27, 43), (25, 42)],
                     [(40, 38), (37, 39), (37, 43), (39, 42)], [(28, 22), (36, 22), (35, 24), (29, 24)]):
            p.fill(p.poly(poly), blue, radius=1.5, depth=2, bias=0.1, edge=False)
        p.stroke([(29, 54), (35, 54)], dark[0])  # the mouth under the nose
        for side in (-1, 1):  # whiskers
            p.stroke([(32 + side * 4, 50), (32 + side * 9, 49)], fur[2])
        p.stroke([(30, 48), (34, 48)], fur[4])
        p.fill(p.ellipse(32, 51.5, 2.6, 2.0), dark, radius=1, depth=1.5, bias=0.2)  # the nose
        p.px(31, 51, dark[4])
        # the eyes, glowing blue
        k = pulse(t, 0.0, 2)
        for ex in (26, 38):  # (owner 2026-11-23 18:55: the first draft's eyes - black sclera tried and reverted)
            p.fill(p.ellipse(ex, 30, 2.4, 1.8), dark, radius=1, depth=1, flat=0, edge=False)
            p.glow(ex, 30, mix((170, 240, 255), (240, 255, 255), k), mix((40, 160, 255), (110, 230, 255), k), r=1,
                   strength=0.6 + 0.3 * k)
        # the goggle lens on the forehead, a strap across the hood
        p.stroke([(16, 17), (48, 17)], metal[1])
        p.stroke([(16, 18), (48, 18)], metal[0])
        p.fill(p.ellipse(32, 16, 7.4, 6.8), metal, radius=2, depth=2.5, bias=0.1)
        lens = p.fill(p.ellipse(32, 16, 5.6, 5.0), blue, radius=3, depth=3, bias=0.15, edge=False)
        p.pixels([(29, 13), (30, 13), (28, 14)], (210, 240, 255))
        p.stroke([(28, 18), (36, 18)], blue[1])  # the water line in the lens
        sweep_over(p, lens, t, (240, 252, 255), width=1.0, angle=-0.7, span=0.3, start=0.1, amount=0.85)
    p.rim('#7ae0ff', side=1, amount=0.3)
    p.rim('#ffb070', side=-1, amount=0.18)
    p.outline('#08080e')
    return p


ALL = [('atokera', 'Atokera Legacy', atokera), ('belkosea', 'Belkosea Allied States', belkosea),
       ('pharadn', 'Pharadn Order', pharadn), ('qhet', 'Qhet Republic', qhet), ('uydai', 'Uydai Conclave', uydai)]


def main(out):
    os.makedirs(out, exist_ok=True)
    scale = 4
    cell = 64 * scale
    sheet = Image.new('RGB', (len(ALL) * (cell * 2 + 24) + 16, cell + 40), (14, 12, 20))
    d = ImageDraw.Draw(sheet)
    for i, (k, name, fn) in enumerate(ALL):
        x = 16 + i * (cell * 2 + 24)
        for j, tt in enumerate((0.0, 0.5)):
            p = fn(tt)
            bg = tuple(int(v) for v in p.backdrop) + (255,)
            sheet.paste(p.image(scale, bg=bg).convert('RGB'), (x + j * (cell + 4), 8))
        d.text((x, cell + 16), k + ' - ' + name, fill=(230, 220, 255))
        save_gif(fn, os.path.join(out, 'anim_%s.gif' % k), scale=scale)
    sheet.save(os.path.join(out, 'sheet_faces_br.png'))
    print('wrote', out)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
