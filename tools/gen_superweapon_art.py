#!/usr/bin/env python3
"""The Belkosea superweapons window (0.84.0, owner 2026-11-23 22:02): "the expanded superweapons pieces window shows each superweapon
card around a massive hi-def Retrowave superweapon animation in the center". Draft 1 (a white lens ship) was too rough; 22:30 the owner
sent references (a painting of a landed no-ship - a grey ovoid with an eye-like aperture - and two Midjourney pictures by Caco of
brutalist megastructures): "The eerie ominous and dark dreary rough nature, the megastructure with a Brutalist-style cannon or armament,
that should be the animation. For the individual cards, an ominous single shape similar to the no-ship ... spherical rather than
elongated. Hi-def Retrowave."

The art rule (owner 2026-11-21): references only - nothing traced or copied. Everything here is drawn by this script:
  * THE MEGASTRUCTURE (centre, ART_W x ART_H): a small ray marcher builds a brutalist fortress of slabs - a top-heavy tower over a narrow
    shaft, cantilevered decks with girders, a pipe, antennas, a pod turret, distant towers in the fog - and a huge cannon jutting from a
    breech block; (owner 2026-11-24: it looked disembodied) the gun sits in a mount ring on a brutalist breech house to its right -
    ribbed concrete, an arm above and a strut below, power bus lines and a coupling ring that glow as it charges. The geometry is rendered once (the camera doesn't move: a G-buffer of depth, normal, material, position); every frame
    then lights it: rough concrete (panel grooves, rain streaks, cell-by-cell grime), hundreds of small orange windows (some flicker), deck
    edge lamps, drifting mist layers between the depths, a molten drip falling from a deck, and the cannon's cycle - it charges (a crimson
    glow growing in the muzzle, spilling on the hull), fires (a beam, a flash), and cools - once per loop.
  * THE SPHERE (cards, CARD_PX): a weathered grey sphere floating in the dark, turned three-quarters, its surface cut with seams and
    glyph-like engravings, an oval recess with concentric rings and a slit iris that glows - each superweapon its own colour.
  * The Retrowave treatment of the unlock pictures (tools/gen_portrait_art.py): its own palette (k-means over all frames) with ordered
    (Bayer) dithering, N_FRAMES x FRAME_MS, a seamless loop.

Usage:  python3 tools/gen_superweapon_art.py OUTDIR
Out:    OUTDIR/fortress.gif (3x), OUTDIR/fortress_strip.png, OUTDIR/spheres.gif (the five sphere icons, 3x), OUTDIR/window.png (the window
        mock-up at 1440 x 1080, frame 0), OUTDIR/window.gif (the mock-up animated, 960 x 720)
Deterministic.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage
from scipy.cluster.vq import kmeans2

ART_W, ART_H = 288, 176
CARD_PX = 64
N_FRAMES = 64
FRAME_MS = 100
TAU = 2.0 * math.pi
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float32) / 16.0 - 0.47
SS = 3  # supersampling of the fortress


def wave(t, f=1, ph=0.0):
    return 0.5 + 0.5 * math.sin(TAU * f * t / N_FRAMES + ph)


def norm(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-9)


def hash2(a, b):
    """0..1 per integer cell (a, b) - arrays"""
    h = np.sin(a * 127.1 + b * 311.7) * 43758.5453
    return h - np.floor(h)


def hash3(a, b, c):
    h = np.sin(a * 127.1 + b * 311.7 + c * 74.7) * 43758.5453
    return h - np.floor(h)


# ------------------------------------------------------------------ the fortress (world: y up, the camera at the origin looking +z)
def box(p, c, h):
    q = np.abs(p - np.array(c, np.float32)) - np.array(h, np.float32)
    return np.linalg.norm(np.maximum(q, 0), axis=-1) + np.minimum(np.max(q, axis=-1), 0)


def cyl_seg(p, a, b, r):
    """a capped cylinder from a to b (iq)"""
    a, b = np.array(a, np.float32), np.array(b, np.float32)
    ba = b - a
    pa = p - a
    baba = float(ba @ ba)
    paba = pa @ ba
    x = np.linalg.norm(pa * baba - ba * paba[..., None], axis=-1) - r * baba
    y = np.abs(paba - baba * 0.5) - baba * 0.5
    x2, y2 = x * x, y * y * baba
    d = np.where(np.maximum(x, y) < 0, -np.minimum(x2, y2), np.where(x > 0, x2, 0) + np.where(y > 0, y2, 0))
    return np.sign(d) * np.sqrt(np.abs(d)) / baba


def sphere(p, c, r):
    return np.linalg.norm(p - np.array(c, np.float32), axis=-1) - r


def oct_xy(ax, ay, h, c):
    """a chamfered square (octagon) in x / y: half size h, chamfer line c (|x| + |y| <= c * sqrt 2)"""
    return np.maximum(np.maximum(ax - h, ay - h), (ax + ay) * 0.70710678 - c * 0.70710678)


CANNON_C = np.array([0.42, -0.08, 3.72], np.float32)  # the cannon block's centre
CANNON_HD = 0.5                                         # its half depth
# (owner 2026-11-23 23:02: a destructive beam of hot plasma - the mouth turned toward the lower left, so the beam crosses the frame)
CAN_Z = (lambda v: v / np.linalg.norm(v))(np.array([0.55, 0.16, 1.0], np.float32))  # the bore's axis, into the tower
CAN_X = (lambda v: v / np.linalg.norm(v))(np.cross(np.array([0.0, 1.0, 0.0], np.float32), CAN_Z))
CAN_Y = np.cross(CAN_Z, CAN_X)
BORE_DEPTH = 1.43                                       # the tunnel runs deep into the tower (local z of its end)
MUZZLE = CANNON_C + CAN_Z * (BORE_DEPTH - 0.05)         # the glow deep in the bore
MOUTH = CANNON_C - CAN_Z * CANNON_HD                    # the mouth's centre


def cannon_local(p):
    q = p - CANNON_C
    return np.stack([q @ CAN_X, q @ CAN_Y, q @ CAN_Z], -1)


def cannon_world(lx, ly, lz):
    return CANNON_C + CAN_X * lx + CAN_Y * ly + CAN_Z * lz


def obox(p, a, b, hw, hh, up=(0.0, 0.0, 1.0)):
    """a box along the segment a -> b, half width hw / half height hh across it"""
    a, b = np.asarray(a, np.float32), np.asarray(b, np.float32)
    d = b - a
    L = float(np.linalg.norm(d))
    z = d / L
    x = np.cross(np.asarray(up, np.float32), z)
    x = x / np.linalg.norm(x)
    y = np.cross(z, x)
    q = p - (a + b) * 0.5
    return box(np.stack([q @ x, q @ y, q @ z], -1), (0, 0, 0), (hw, hh, L * 0.5))

M_CONCRETE, M_DECK, M_PIPE, M_GUN, M_POD, M_FAR, M_CONDUIT = 1, 2, 3, 4, 5, 6, 7


def scene(p):
    """distance + material"""
    parts = []
    # the tower: a top-heavy block over a narrower shaft
    tower = np.minimum(box(p, (-0.62, 1.45, 5.0), (0.95, 2.05, 0.9)), box(p, (-0.38, -2.1, 4.8), (0.5, 1.5, 0.55)))
    # greebles on its front: protruding blocks, one per cell, random depth (domain repetition)
    cy = np.floor(p[..., 1] / 0.2)
    cw = 0.18 + 0.22 * hash2(cy, 5.0)  # (each row its own block width)
    cx = np.floor(p[..., 0] / cw)
    hh = hash2(cx, cy)
    local = np.stack([(p[..., 0] - (cx + 0.5) * cw), (p[..., 1] - (cy + 0.5) * 0.2), p[..., 2] - (4.1 - 0.02 * hh)], -1)
    hs = np.stack([cw * (0.3 + 0.15 * hh), np.full_like(hh, 0.06 + 0.03 * hash2(cy, 9.0)), 0.02 + 0.05 * hh], -1)
    q = np.abs(local) - hs
    gre = np.linalg.norm(np.maximum(q, 0), axis=-1) + np.minimum(np.max(q, axis=-1), 0)
    gre = np.maximum(gre, box(p, (-0.62, 1.2, 4.1), (0.93, 1.8, 0.1)))  # (on the upper block's face only)
    gre = np.where(hh > 0.6, gre, 9.0)
    qc = cannon_local(p)
    tunnel = np.maximum(oct_xy(np.abs(qc[..., 0]), np.abs(qc[..., 1]), 0.62, 0.62 * 1.414 * 0.94), np.maximum(qc[..., 2] - BORE_DEPTH - 0.2, -(qc[..., 2] + CANNON_HD)))
    parts.append((np.maximum(np.minimum(tower, gre), -tunnel), M_CONCRETE))
    # a buttress under the overhang, curving down into the shaft (a box minus a cylinder along z)
    but = box(p, (-1.25, -0.9, 5.0), (0.35, 0.32, 0.8))
    hole = np.linalg.norm(np.stack([p[..., 0] + 1.6, p[..., 1] + 1.22], -1), axis=-1) - 0.67
    parts.append((np.maximum(but, -hole), M_CONCRETE))
    # the upper cantilever and its lip
    upper = np.minimum(box(p, (0.95, 0.72, 4.62), (1.05, 0.22, 0.6)), box(p, (1.05, 0.98, 4.45), (0.95, 0.04, 0.5)))
    parts.append((upper, M_CONCRETE))
    # the decks below it, girders underneath
    deck = np.minimum(box(p, (1.6, -0.2, 4.35), (0.55, 0.07, 0.45)), box(p, (1.55, -0.44, 4.4), (0.48, 0.045, 0.4)))
    gx = np.floor(p[..., 0] / 0.22)
    gird = box(np.stack([p[..., 0] - (gx + 0.5) * 0.22, p[..., 1], p[..., 2]], -1), (0, -0.3, 4.4), (0.02, 0.05, 0.4))
    gird = np.maximum(gird, box(p, (1.6, -0.3, 4.4), (0.5, 0.1, 0.45)))
    parts.append((np.minimum(deck, gird), M_DECK))
    # the pipe: up the tower's face, then along under the upper cantilever
    pipe = np.minimum(cyl_seg(p, (-0.52, -3.0, 4.02), (-0.52, 0.86, 4.02), 0.07), cyl_seg(p, (-0.52, 0.86, 4.02), (-0.52, 0.86, 4.3), 0.07))
    pipe = np.minimum(pipe, sphere(p, (-0.52, 0.86, 4.02), 0.075))
    parts.append((pipe, M_PIPE))
    # antennas on the upper lip
    ant = np.full(p.shape[:-1], 9.0, np.float32)
    for x, hgt in ((0.35, 0.3), (0.62, 0.18), (1.32, 0.38), (1.71, 0.22)):
        ant = np.minimum(ant, box(p, (x, 1.02 + hgt / 2, 4.2), (0.008, hgt / 2, 0.008)))
    parts.append((ant, M_DECK))
    # the cannon (owner 2026-11-23 22:39: brutalist): a monolithic block with a chamfered-square (octagonal) bore facing us - stepped
    # frames recede into it, its walls lit from inside; greebles round the mouth
    q = cannon_local(p)
    lz = q[..., 2]
    ax, ay = np.abs(q[..., 0]), np.abs(q[..., 1])
    zf = -CANNON_HD  # the front face (local z)
    housing = np.maximum(oct_xy(ax, ay, 0.66, 0.66 * 1.414 * 0.94), np.maximum(zf - lz, lz - BORE_DEPTH - 0.15))
    step = np.clip(np.floor((lz - zf) / 0.09), 0, 4)
    rb = np.where(lz < zf + 0.45, 0.52 - 0.04 * step, 0.36)  # stepped frames, then the bore
    bore = np.maximum(oct_xy(ax, ay, rb, rb * 1.414 * 0.92), lz - BORE_DEPTH)
    housing = np.maximum(housing, -bore)
    # blocks on the face round the mouth (one per cell, random depth)
    gx2, gy2 = np.floor(q[..., 0] / 0.11), np.floor(q[..., 1] / 0.09)
    gh = hash2(gx2 + 40.0, gy2)
    gl = np.stack([q[..., 0] - (gx2 + 0.5) * 0.11, q[..., 1] - (gy2 + 0.5) * 0.09, lz - (zf - 0.015 * gh)], -1)
    gq = np.abs(gl) - np.stack([np.full_like(gh, 0.04), np.full_like(gh, 0.03), 0.012 + 0.03 * gh], -1)
    gb = np.linalg.norm(np.maximum(gq, 0), axis=-1) + np.minimum(np.max(gq, axis=-1), 0)
    ring_zone = (oct_xy(ax, ay, 0.66, 0.66 * 1.414 * 0.94) < 0) & (oct_xy(ax, ay, 0.55, 0.55 * 1.414 * 0.92) > 0)
    gb = np.where((gh > 0.5) & ring_zone, gb, 9.0)
    # the collar where it meets the tower, and two buttress blocks above and below
    collar = box(q, (-0.1, 0.0, CANNON_HD + 0.1), (0.78, 0.78, 0.14))
    braces = np.minimum(box(q, (0.45, 0.62, 0.1), (0.12, 0.2, 0.45)), box(q, (-0.45, -0.62, 0.1), (0.14, 0.22, 0.45)))
    parts.append((np.minimum(np.minimum(housing, gb), np.minimum(collar, braces)), M_GUN))
    # (owner 2026-11-24 07:32: "there's no backing or structure to the right behind the cannon, so it looks disembodied") the gun's
    # carriage: the barrel runs on, banded, into a brutalist breech house to the right - a stepped block of ribbed concrete with a
    # deep collar where the barrel enters, a raking strut under the gun, hydraulic lines along its back
    mount = np.maximum(oct_xy(ax, ay, 0.98, 0.98 * 1.414 * 0.94), np.maximum(1.12 - lz, lz - 2.4))  # the mount ring round its back
    band = np.maximum(oct_xy(ax, ay, 0.74, 0.74 * 1.414 * 0.94), np.abs(lz - 0.95) - 0.06)
    parts.append((np.minimum(mount, band), M_GUN))
    hyd = np.minimum(cyl_seg(p, cannon_world(0.3, 0.67, 0.2), cannon_world(0.3, 0.67, 1.3), 0.045),
                     cyl_seg(p, cannon_world(-0.2, 0.69, 0.1), cannon_world(-0.2, 0.69, 1.3), 0.045))
    parts.append((hyd, M_PIPE))
    # the power feed: bus lines along the breech house's face into the mount, and a coupling ring where the gun sits in it - they
    # glow as the gun charges, the light running toward the muzzle
    feed = np.maximum(np.maximum(oct_xy(ax, ay, 0.86, 0.86 * 1.414 * 0.94), -oct_xy(ax, ay, 0.76, 0.76 * 1.414 * 0.94)),
                      np.abs(lz - 1.09) - 0.045)
    for wy in (0.6, 0.1, -0.4):
        feed = np.minimum(feed, box(p, (2.8, wy, 4.98), (1.45, 0.045, 0.045)))
    parts.append((feed, M_CONDUIT))
    base = box(p, (2.75, -1.0, 6.1), (1.5, 2.0, 1.0))
    upper2 = box(p, (3.05, 1.25, 6.65), (1.15, 0.4, 0.95))
    lip = box(p, (2.65, 1.04, 5.95), (1.62, 0.06, 1.02))
    house = np.minimum(np.minimum(base, upper2), lip)
    rx = np.floor((p[..., 0] - 1.25) / 0.38)  # vertical ribs down its face (one per cell)
    rib = box(np.stack([p[..., 0] - 1.25 - (rx + 0.5) * 0.38, p[..., 1], p[..., 2]], -1), (0, -1.0, 5.05), (0.07, 2.0, 0.07))
    rib = np.maximum(rib, box(p, (2.75, -1.0, 5.05), (1.45, 1.9, 0.12)))
    strut = obox(p, (1.6, -3.3, 4.9), cannon_world(0.05, -0.55, 1.0), 0.22, 0.22)
    strut = np.minimum(strut, obox(p, (2.45, 1.0, 5.2), cannon_world(0.5, 0.66, 0.55), 0.17, 0.17))  # the arm holding it from above
    parts.append((np.minimum(np.minimum(house, rib), strut), M_CONCRETE))
    # the pod turret under the lower deck
    pod = np.minimum(sphere(p, (1.42, -0.68, 4.3), 0.17), cyl_seg(p, (1.42, -0.7, 4.3), (1.15, -0.86, 4.0), 0.03))
    pod = np.minimum(pod, cyl_seg(p, (1.42, -0.5, 4.3), (1.42, -0.62, 4.3), 0.05))
    parts.append((pod, M_POD))
    # distant towers in the fog
    far = np.minimum(np.minimum(box(p, (3.2, 0.2, 10.5), (0.7, 4.5, 0.8)), box(p, (-3.6, 0.5, 12.0), (1.1, 5.0, 1.0))),
                     np.minimum(box(p, (1.7, 2.2, 14.0), (1.6, 0.9, 1.2)), box(p, (4.4, -0.6, 13.0), (1.8, 0.5, 1.0))))
    # (the second fortress behind, its own decks)
    far = np.minimum(far, np.minimum(box(p, (2.6, 1.3, 8.2), (0.9, 0.12, 0.7)), box(p, (2.9, 0.6, 8.0), (1.2, 0.08, 0.6))))
    far = np.minimum(far, np.minimum(box(p, (2.2, -0.9, 8.6), (0.5, 2.5, 0.5)), box(p, (-2.6, -1.5, 8.0), (0.7, 2.5, 0.6))))
    far = np.minimum(far, box(p, (5.5, 1.0, 16.0), (1.2, 3.0, 1.0)))
    parts.append((far, M_FAR))
    d = parts[0][0]
    m = np.full(d.shape, parts[0][1], np.int8)
    for dd, mm in parts[1:]:
        closer = dd < d
        d = np.where(closer, dd, d)
        m = np.where(closer, mm, m)
    return d, m


TILT = math.radians(6)
FOVK = 0.74


def camera(W, H):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    u = (xx + 0.5 - W * 0.5) / H
    v = -(yy + 0.5 - H * 0.5) / H
    rd = np.stack([u * FOVK, v * FOVK, np.ones_like(u)], -1)
    rd = np.stack([rd[..., 0], rd[..., 1] * math.cos(TILT) + rd[..., 2] * math.sin(TILT), -rd[..., 1] * math.sin(TILT) + rd[..., 2] * math.cos(TILT)], -1)
    return norm(rd)


def project(pw):
    """world point -> art pixel (x, y) (the inverse of camera)"""
    y = pw[1] * math.cos(TILT) - pw[2] * math.sin(TILT)
    z = pw[1] * math.sin(TILT) + pw[2] * math.cos(TILT)
    u, v = pw[0] / z / FOVK, y / z / FOVK
    return u * ART_H + ART_W * 0.5, -v * ART_H + ART_H * 0.5


def render_gbuffer():
    W, H = ART_W * SS, ART_H * SS
    rd = camera(W, H).reshape(-1, 3)
    n = rd.shape[0]
    t = np.full(n, 0.5, np.float32)
    hit = np.zeros(n, bool)
    steps = np.zeros(n, np.float32)
    idx = np.arange(n)
    for i in range(220):
        if idx.size == 0:
            break
        p = rd[idx] * t[idx, None]
        d, _ = scene(p)
        h = d < 0.0007 * t[idx]
        hit[idx[h]] = True
        t[idx] += np.where(h, 0, d * 0.75)
        steps[idx] += 1
        keep = (~h) & (t[idx] < 30.0)
        idx = idx[keep]
    p = rd * t[:, None]
    d, mat = scene(p)
    e = 0.0015
    ex, ey, ez = np.array([e, 0, 0], np.float32), np.array([0, e, 0], np.float32), np.array([0, 0, e], np.float32)
    nrm = np.stack([scene(p + ex)[0] - scene(p - ex)[0], scene(p + ey)[0] - scene(p - ey)[0], scene(p + ez)[0] - scene(p - ez)[0]], -1)
    nrm = norm(nrm)
    probe = np.clip(scene(p + nrm * 0.06)[0] / 0.06, 0, 1)
    ao = np.clip(1.0 - steps / 220.0 * 0.6, 0.35, 1.0) * (0.4 + 0.6 * probe)
    mat = np.where(hit, mat, 0)
    sh = (H, W)
    return dict(hit=hit.reshape(sh), t=t.reshape(sh), p=p.reshape(sh + (3,)), n=nrm.reshape(sh + (3,)), mat=mat.reshape(sh), ao=ao.reshape(sh))


def periodic_noise(w, h, scale, seed):
    """tileable smooth noise 0..1 (filtered white noise, wrapped)"""
    rng = np.random.RandomState(seed)
    a = rng.rand(h, w).astype(np.float32)
    a = ndimage.gaussian_filter(a, scale, mode="wrap")
    return (a - a.min()) / (a.max() - a.min() + 1e-6)


class Fortress:
    def __init__(self):
        g = render_gbuffer()
        H, W = g["hit"].shape
        p, n, mat, hit = g["p"], g["n"], g["mat"], g["hit"]
        x, y, z = p[..., 0], p[..., 1], p[..., 2]
        # rough concrete: grime per panel cell, grooves, rain streaks
        cell = hash2(np.floor(x / 0.16 + 0.5 * np.floor(y / 0.12)), np.floor(y / 0.12)) * 0.35 + hash2(np.floor(x / 0.05), np.floor(y / 0.04)) * 0.15
        groove = (np.abs(((y / 0.12) % 1.0) - 0.5) > 0.45) | (np.abs(((x / 0.16 + 0.5 * np.floor(y / 0.12)) % 1.0) - 0.5) > 0.47)
        streak = hash2(np.floor(x / 0.012), 3.0) * (0.5 + 0.5 * np.sin(y * 9.0 + hash2(np.floor(x / 0.012), 7.0) * 20.0))
        base = np.array([0.46, 0.5, 0.48], np.float32)
        stain = hash2(np.floor(x / 0.5), np.floor(y / 0.35)) * 0.5 + hash2(np.floor(x / 0.23) + 3, np.floor(y / 0.6)) * 0.5
        alb = base * (0.55 + cell[..., None] - 0.2 * streak[..., None] - 0.25 * stain[..., None])
        alb = np.where(groove[..., None] & (mat[..., None] == M_CONCRETE), alb * 0.55, alb)
        alb = np.where((mat == M_DECK)[..., None], np.array([0.38, 0.4, 0.4], np.float32) * (0.8 + 0.3 * cell[..., None]), alb)
        alb = np.where((mat == M_PIPE)[..., None], np.array([0.5, 0.48, 0.44], np.float32), alb)
        alb = np.where((mat == M_GUN)[..., None], np.array([0.33, 0.34, 0.36], np.float32) * (0.85 + 0.3 * cell[..., None]), alb)
        alb = np.where((mat == M_POD)[..., None], np.array([0.55, 0.57, 0.6], np.float32), alb)
        alb = np.where((mat == M_CONDUIT)[..., None], np.array([0.22, 0.2, 0.2], np.float32), alb)
        key = norm(np.array([0.55, 0.62, -0.55], np.float32))
        ndl = np.clip((n * key).sum(-1), 0, 1)
        skyl = np.clip(n[..., 1] * 0.5 + 0.5, 0, 1)
        light = (np.array([150, 175, 180], np.float32) * ndl[..., None] * 0.75 + np.array([60, 82, 84], np.float32) * skyl[..., None] * 0.6) * g["ao"][..., None]
        self.lit = alb * light
        # the windows: small lit cells on faces toward the camera (and the sides); some flicker
        front = (n[..., 2] < -0.6) & ((mat == M_CONCRETE) | (mat == M_DECK))
        side = (np.abs(n[..., 0]) > 0.6) & (mat == M_CONCRETE)
        wx, wy, wz = np.floor(x / 0.045), np.floor(y / 0.06), np.floor(z / 0.045)
        fx, fy, fz = (x / 0.045) % 1.0, (y / 0.06) % 1.0, (z / 0.045) % 1.0
        hw = np.where(side, hash2(wz + 91.0, wy), hash2(wx, wy))
        inner = np.where(side, np.abs(fz - 0.5) < 0.3, np.abs(fx - 0.5) < 0.3) & (np.abs(fy - 0.5) < 0.25)
        band = hash2(np.floor(y / 0.25), 17.0)  # (lit floors and dark ones)
        self.win = (front | side | (mat == M_FAR)) & inner & (hw < 0.02 + 0.09 * (band > 0.62)) & hit & (z < 17.0)
        self.win_ph = hash2(wx * 3.0 + wz, wy * 5.0) * TAU
        self.win_fl = hash2(wy * 7.0, wx * 2.0 + wz) < 0.18
        self.win_hue = hash2(wx + 5.0, wy + wz)
        # inside the cannon's bore: its walls lit warm from within, rows of small lamps
        cl = cannon_local(p)
        qx, qy, lz = np.abs(cl[..., 0]), np.abs(cl[..., 1]), cl[..., 2]
        zf = -CANNON_HD
        self.bore = hit & (mat == M_GUN) & (oct_xy(qx, qy, 0.54, 0.54 * 1.414 * 0.92) < 0.005) & (lz > zf + 0.02)
        deep = np.clip((lz - zf) / (BORE_DEPTH - zf), 0, 1)
        self.rim = hit & (mat == M_GUN) & (lz < zf + 0.47) & (oct_xy(qx, qy, 0.6, 0.6 * 1.414 * 0.92) < 0.01)  # (the mouth's frames: red-hot after a shot)
        self.bore_glow = self.bore * (0.35 + 0.65 * deep)
        bl = (np.abs(((lz / 0.16) % 1.0) - 0.5) < 0.12) & (hash2(np.floor(lz / 0.16), np.floor((cl[..., 0] + 2 * cl[..., 1]) / 0.06)) < 0.22)
        self.bore_lamp = self.bore & bl & (lz > zf + 0.12)
        # deck edge lamps
        self.lamp = (mat == M_DECK) & (n[..., 2] < -0.6) & (np.abs(((x / 0.11) % 1.0) - 0.5) < 0.1) & (hash2(np.floor(x / 0.11), 3.0) < 0.6) & hit
        self.lamp_ph = np.floor(x / 0.11)
        # the power feed (0.84.1): its light runs toward the muzzle - along the gun (its local z), along the house's face (x)
        self.feed = hit & (mat == M_CONDUIT)
        self.feed_s = np.where(np.abs(n[..., 2]) > 0.9, (x - 1.2) * 0.8 + 1.6, cl[..., 2])
        t = g["t"]
        self.fogk = np.where(hit, 1.0 - np.exp(-np.maximum(t - 3.2, 0) * 0.16), 1.0)
        self.depth = np.where(hit, t, 99.0)
        self.y = np.where(hit, y, 0)
        self.hit = hit
        # the muzzle's light reaches what faces it
        dv = (MOUTH - CAN_Z * 0.35)[None, None, :] - p
        dist = np.linalg.norm(dv, axis=-1)
        self.spill = np.where(hit, np.clip((n * dv).sum(-1) / np.maximum(dist, 1e-3), 0, 1) / (0.3 + dist * dist * 3.0), 0) * g["ao"]
        self.mist = [periodic_noise(W * 2, H, 18.0, 11), periodic_noise(W * 2, H, 10.0, 12), periodic_noise(W * 2, H, 26.0, 13)]
        self.mx, self.my = project(MUZZLE)
        self.ox, self.oy = project(MOUTH)
        # the beam's axis on the screen: points out of the mouth toward the viewer, with their radius in pixels
        self.beam = []
        sdist = 0.0
        while True:
            pw = MOUTH - CAN_Z * sdist
            zc = pw[1] * math.sin(TILT) + pw[2] * math.cos(TILT)
            if zc < 0.5:
                break
            bx, by = project(pw)
            self.beam.append((bx, by, ART_H / (zc * FOVK), sdist))
            sdist += 0.05
        rs = np.random.RandomState(21)
        self.sparks = [(rs.uniform(0.58, 0.86), rs.uniform(0, TAU), rs.uniform(30, 120), rs.uniform(0.04, 0.12)) for _ in range(70)]
        rng = np.random.RandomState(9)
        self.craft = [(rng.uniform(0.15, 0.55), rng.randint(1, 3), rng.uniform(0, 1)) for _ in range(4)]

    def frame(self, t):
        H, W = self.hit.shape
        ph = t / N_FRAMES
        yy = np.linspace(0, 1, H, dtype=np.float32)[:, None]
        bg = np.array([10, 20, 22], np.float32) * (1 - yy[..., None]) + np.array([46, 66, 66], np.float32) * yy[..., None]
        bg = np.broadcast_to(bg, (H, W, 3)).copy()
        img = np.where(self.hit[..., None], self.lit, bg)
        # the cannon's cycle (owner 2026-11-23 23:02: "a destructive beam of hot plasma"): it charges (0 .. 0.56), ignites, pours a
        # beam of plasma out of the frame (0.58 .. 0.86), and the beam breaks up; the mouth stays red-hot, smoke rises
        if ph < 0.56:
            charge = (ph / 0.56) ** 2.2
        elif ph < 0.88:
            charge = 1.0
        else:
            charge = max(0.0, 1.0 - (ph - 0.88) / 0.1)
        flash = math.exp(-((ph - 0.585) / 0.018) ** 2)
        if 0.57 <= ph < 0.84:
            power = min(1.0, (ph - 0.57) / 0.02)
        elif 0.84 <= ph < 0.92:
            power = max(0.0, 1.0 - (ph - 0.84) / 0.08)
        else:
            power = 0.0
        heat = max(0.0, 1.0 - (ph - 0.6) / 0.4) if ph >= 0.6 else 0.0
        img += self.spill[..., None] * np.array([255, 80, 30], np.float32) * (0.35 * charge + 1.6 * power + 1.5 * flash)
        img += (self.rim * heat ** 1.5)[..., None] * np.array([255, 70, 20], np.float32) * 0.9  # (the mouth glows red-hot)
        # windows and lamps
        k = 0.75 + 0.25 * np.sin(TAU * 2 * ph + self.win_ph)
        fl = np.where(self.win_fl, (np.sin(TAU * 7 * ph + self.win_ph * 3) > -0.2).astype(np.float32), 1.0)
        wc = np.where((self.win_hue < 0.85)[..., None], np.array([255, 150, 70], np.float32), np.array([255, 220, 170], np.float32))
        img += (self.win * k * fl)[..., None] * wc
        img = np.where(self.bore[..., None], img * 0.45, img)  # (the tunnel is dark - its light comes from the lamps and the core)
        img += (self.bore_glow ** 2)[..., None] * np.array([120, 45, 20], np.float32) * (0.25 + 1.1 * charge)
        img += (self.bore_lamp * (0.7 + 0.3 * np.sin(TAU * ph * 3)))[..., None] * np.array([255, 170, 90], np.float32)
        flow = 0.5 + 0.5 * np.sin(self.feed_s * 9.0 + TAU * ph * 8)
        fk = (0.18 + 1.3 * charge + 0.8 * power) * (0.35 + 0.65 * flow)
        img += (self.feed * fk)[..., None] * np.array([255, 80, 36], np.float32)
        lk = 0.6 + 0.4 * np.sin(TAU * ph * 2 + self.lamp_ph * 0.9)
        img += (self.lamp * lk)[..., None] * np.array([200, 90, 45], np.float32)
        # fog by depth, heavier low down
        low = np.clip((-self.y - 0.6) / 2.5, 0, 1)
        f = np.clip(self.fogk + low * 0.35 * self.hit, 0, 1)
        img = img * (1 - f[..., None] * 0.85) + np.array([40, 60, 60], np.float32) * f[..., None] * 0.85
        # drifting mist: three layers at their depths, each sliding one tile width per loop (seamless)
        for m, dep, amt in zip(self.mist, (4.3, 6.0, 9.0), (0.32, 0.28, 0.4)):
            off = int(round(ph * m.shape[1])) % m.shape[1]
            sh = np.roll(m, -off, axis=1)[:, :W]
            a = np.clip((sh - 0.45) * 2.0, 0, 1) * amt * (self.depth > dep) * (0.6 + 0.6 * yy)
            img = img * (1 - a[..., None]) + np.array([62, 88, 86], np.float32) * a[..., None]
        # the molten drip from the lower deck: a thin glowing line, drops running down it
        dx, dy = project(np.array([1.8, -0.49, 4.05], np.float32))
        dxi, top = int(round(dx * SS)), max(0, int(round(dy * SS)))
        if 0 <= dxi < W and top < H:
            col = np.arange(top, H)
            s = (col - top) / max(H - top, 1)
            glow = (0.5 + 0.5 * np.exp(-s * 2.0)) * (0.6 + 0.4 * np.sin(TAU * (s * 3 - ph * 4)))
            for w, a in ((0, 1.0), (1, 0.35), (-1, 0.35)):
                if 0 <= dxi + w < W:
                    img[col, dxi + w] += (glow * a)[:, None] * np.array([255, 120, 50], np.float32)
        # small craft crossing far away
        for (yf, sp, ph0) in self.craft:
            s = (ph0 + sp * ph) % 1.0
            cx, cy = int(s * W), int(yf * H)
            if self.depth[cy, min(cx, W - 1)] > 8.0:
                img[max(0, cy - 1):cy + 1, max(0, cx - 2 * SS):cx + 1] += np.array([200, 120, 80], np.float32) * 0.7
        img = np.clip(img, 0, 255)
        img = img.reshape(ART_H, SS, ART_W, SS, 3).mean((1, 3))
        # the muzzle: its glow and, when it fires, the beam out of the frame and the flash
        mx, my = self.mx, self.my
        yyA, xxA = np.mgrid[0:ART_H, 0:ART_W].astype(np.float32)
        r = np.sqrt((xxA - mx) ** 2 + (yyA - my) ** 2)
        img += (np.exp(-(r / (2.0 + 3.0 * charge)) ** 2) * (0.4 + 1.6 * charge))[..., None] * np.array([255, 50, 30], np.float32)
        img += (np.exp(-(r / 1.2) ** 2) * charge)[..., None] * np.array([255, 230, 200], np.float32)
        if power > 0.0:  # the beam: a white-hot core, yellow and orange plasma, a crimson glow, its edges boiling
            u = np.full((ART_H, ART_W), 99.0, np.float32)
            along = np.zeros((ART_H, ART_W), np.float32)
            for i, (bx, by, k, sd) in enumerate(self.beam):
                wob = 1.0 + 0.18 * math.sin(sd * 9.0 - ph * TAU * 6) + 0.1 * math.sin(sd * 23.0 + ph * TAU * 11)
                rad = max(0.6, (0.44 + 0.1 * sd) * k * wob * (0.6 + 0.4 * power))  # (the plasma spreads as it goes)
                d = np.sqrt((xxA - bx) ** 2 + (yyA - by) ** 2) / rad
                closer = d < u
                along = np.where(closer, sd, along)
                u = np.where(closer, d, u)
            # (boiling: pulses running out along the beam, ripples round it)
            pulse = 0.5 + 0.5 * np.sin(along * 14.0 - ph * TAU * 10)
            u = u * (1.0 - 0.12 * pulse)
            core = np.clip(1.0 - u / 0.5, 0, 1)
            body = np.clip(1.0 - u, 0, 1)
            glow = np.exp(-np.maximum(u - 1.0, 0) * 4.5) * np.clip((4.0 - u) / 2.0, 0, 1) * np.clip(1.5 - along * 0.6, 0.0, 1.0)
            img += (glow * power)[..., None] * np.array([255, 70, 40], np.float32) * 0.8
            img = img * (1 - (body * power)[..., None]) + (body * power)[..., None] * (
                np.array([255, 120, 35], np.float32) + (body ** 1.5)[..., None] * np.array([0, 110, 80], np.float32))
            img += (core * power)[..., None] * np.array([255, 255, 235], np.float32)
            # sparks thrown off the beam and out of the mouth
            for (t0, ang, sp, life) in self.sparks:
                a = ph - t0
                if a < 0 or a > life:
                    continue
                f = a / life
                L = sp * a
                x0, y0 = self.ox + math.cos(ang) * L, self.oy + math.sin(ang) * L + 40 * a * a
                for k2 in range(4):
                    xi, yi = int(x0 - math.cos(ang) * k2), int(y0 - math.sin(ang) * k2)
                    if 0 <= xi < ART_W and 0 <= yi < ART_H:
                        img[yi, xi] += np.array([255, 190, 90], np.float32) * (1 - f) * (1 - k2 * 0.22)
        if ph >= 0.84:  # smoke rolls out of the mouth and rises
            age = (ph - 0.84) / 0.16
            for k3 in range(3):
                cx = self.ox - 6 * k3 - 10 * age
                cy = self.oy - 6 - 30 * age - 8 * k3
                rr = 8 + 26 * age + 5 * k3
                sm = np.exp(-(((xxA - cx) ** 2 + (yyA - cy) ** 2) / (rr * rr)))
                sa = (sm * 0.45 * math.sin(math.pi * min(1.0, age * 1.1)))[..., None]
                img = img * (1 - sa) + np.array([52, 54, 54], np.float32) * sa
        img += (flash * 1.0 + power * 0.25) * np.array([60, 26, 14], np.float32)
        img = np.clip(img, 0, 255)
        if 0.575 <= ph < 0.68:  # the ground shakes
            k = int(t * 7919) % 5 - 2
            img = np.roll(np.roll(img, k, axis=1), (int(t * 104729) % 3) - 1, axis=0)
        return img


# ------------------------------------------------------------------ the sphere (card icons)
SPHERE_COLOURS = {  # the iris's light, per superweapon
    "Availyn": (190, 90, 255), "Caled": (255, 50, 40), "Glatison": (70, 220, 255), "Grom": (255, 170, 50), "Mors": (220, 20, 60),
}


def sphere_frames(glow_rgb, seed):
    """a weathered grey sphere, three-quarters turned, an oval recess with rings and a slit iris; its panels creep round one step per loop"""
    S = CARD_PX * 3
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
    u = (xx + 0.5 - S / 2) / (S * 0.42)
    v = -(yy + 0.5 - S / 2) / (S * 0.42)
    rr = u * u + v * v
    inside = rr < 1.0
    zz = np.sqrt(np.clip(1.0 - rr, 0, 1))
    n = np.stack([u, v, -zz], -1)  # (the camera looks +z: the visible side faces -z)
    key = norm(np.array([-0.55, 0.6, -0.6], np.float32))
    rimd = norm(np.array([0.7, -0.2, 0.7], np.float32))
    g = np.array(glow_rgb, np.float32)
    yaw = math.radians(-16)
    cyw, syw = math.cos(yaw), math.sin(yaw)
    X = n[..., 0] * cyw - n[..., 2] * syw
    Z = n[..., 0] * syw + n[..., 2] * cyw
    Y = n[..., 1]
    ax = np.arctan2(X, -Z)
    lat = np.arcsin(np.clip(Y, -1, 1))
    ov = (ax / 0.72) ** 2 + (lat / 0.55) ** 2
    recess = (ov < 1.0) & inside
    ndl = np.clip((n * key).sum(-1), 0, 1)
    fres = np.clip(1 - np.abs(n[..., 2]), 0, 1) ** 3
    rim = np.clip((n * rimd).sum(-1), 0, 1) * fres
    frames = []
    for t in range(N_FRAMES):
        spin = TAU / 24.0 * (t / N_FRAMES)
        lon = ax + spin
        band = np.floor(lat / 0.22)
        seg = np.floor(lon / (TAU / 24.0))
        cell = hash2(seg, band)
        seam = (np.abs(((lat / 0.22) % 1.0) - 0.5) > 0.45) | (np.abs(((lon / (TAU / 24.0)) % 1.0) - 0.5) > 0.46)
        glyph = (hash3(np.floor(lon / 0.05), np.floor(lat / 0.05), seg) < 0.18) & (cell > 0.5)
        alb = 0.52 + 0.22 * cell - 0.12 * glyph - 0.2 * seam
        col = np.array([200, 205, 210], np.float32) * (alb * (0.15 + 0.9 * ndl))[..., None]
        col += np.array([170, 40, 60], np.float32) * rim[..., None] * 0.9
        rings = np.abs(((np.sqrt(ov) * 5.0 - spin * 2.0) % 1.0) - 0.5) < 0.12
        rc = np.array([60, 62, 66], np.float32) * (0.35 + 0.5 * ndl)[..., None] * np.where(rings[..., None], 1.4, 0.8)
        rc = np.where(((ov > 0.86) & recess)[..., None], rc * 0.35, rc)
        col = np.where(recess[..., None], rc, col)
        breathe = 0.6 + 0.4 * math.sin(TAU * t / N_FRAMES)
        iris = recess & (np.abs(ax) < 0.09 * (0.6 + 0.4 * breathe)) & (np.abs(lat) < 0.42)
        pupil = np.exp(-((ax / 0.05) ** 2 + (lat / 0.08) ** 2))
        col = col + (iris * (0.8 + 0.6 * breathe))[..., None] * g + (pupil * recess * breathe)[..., None] * np.array([255, 255, 255], np.float32) * 0.6
        img = np.zeros((S, S, 3), np.float32)
        back = np.exp(-(rr / 1.5)) * 0.35
        img += back[..., None] * (g * 0.25 + np.array([60, 10, 20], np.float32))
        img = np.where(inside[..., None], col, img)
        bloom = ndimage.gaussian_filter(iris.astype(np.float32), 4.0)
        img += bloom[..., None] * g * (0.9 * breathe)
        img = img.reshape(CARD_PX, 3, CARD_PX, 3, 3).mean((1, 3))
        frames.append(np.clip(img, 0, 255))
    return frames


# ------------------------------------------------------------------ palette + dithering (as gen_portrait_art)
def make_palette(frames, k, seed):
    data = np.concatenate([f.reshape(-1, 3) for f in frames]).astype(np.float64)
    rng = np.random.RandomState(seed)
    idx = rng.choice(len(data), size=min(len(data), 150000), replace=False)
    cent, _ = kmeans2(data[idx], k, minit="++", seed=seed, iter=30)
    cent = np.clip(cent, 0, 255)
    pal = np.vstack([cent, data[np.argmin(data.sum(1))], data[np.argmax(data.sum(1))]])
    return np.unique(np.round(pal), axis=0).astype(np.float32)


def quantize(img, pal, dither=10.0):
    h, w = img.shape[:2]
    bay = np.tile(BAYER, (h // 4 + 1, w // 4 + 1))[:h, :w]
    d = np.clip(img + bay[..., None] * dither, 0, 255)
    out = np.empty((h, w), np.uint8)
    for y0 in range(0, h, 40):
        dd = d[y0:y0 + 40]
        dist = ((dd[:, :, None, :] - pal[None, None, :, :]) ** 2).sum(-1)
        out[y0:y0 + 40] = np.argmin(dist, axis=2)
    return out


# ------------------------------------------------------------------ the window mock-up
CARDS = [
    ("Availyn", "This system is a nebula. At any time, you may exhaust this card to produce up to 3 fighters in a system that contains your ships. PRODUCTION 3", "ready", "on Iterum"),
    ("Caled", "At any time, you may exhaust this card to destroy 1 of your opponent's ships in the active system, if a line from this system to that system would cross only parallel non-anomaly borders.", "exhausted", "on Tarana"),
    ("Glatison", "At any time, you may exhaust this card to repair all of your damaged units. Other players' units in this system lose SUSTAIN DAMAGE. ANTI-FIGHTER BARRAGE 4 (x2).", "ready", "on Quann"),
    ("Grom", "At any time, you may exhaust this card to resolve ANTI-FIGHTER BARRAGE 4 against each fighter in 1 adjacent system (including your own).", "unbuilt", "not built yet"),
    ("Mors", "At any time, you may exhaust this card to force each unit in 1 non-adjacent system to become damaged (including your own). PLANETARY SHIELD", "ready", "on Lodor"),
]


def font(sz, bold=False):
    for p in (["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"] if bold else []) + ["/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
        if os.path.exists(p):
            return ImageFont.truetype(p, sz)
    return ImageFont.load_default()


def wrap(d, text, f, width):
    words, lines, cur = text.split(), [], ""
    for w in words:
        nxt = (cur + " " + w).strip()
        if d.textlength(nxt, font=f) > width and cur:
            lines.append(cur)
            cur = w
        else:
            cur = nxt
    if cur:
        lines.append(cur)
    return lines


def card(name, text, state, w, h, where, icon):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    dim = state != "ready"
    edge = (190, 70, 60) if not dim else (90, 70, 70)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 12, fill=(14, 16, 18, 245), outline=edge, width=4)
    d.rounded_rectangle((10, 10, w - 11, 56), 8, fill=(44, 30, 28) if not dim else (30, 28, 30))
    d.text((w // 2, 33), name.upper(), font=font(26, True), fill=(245, 232, 222) if not dim else (140, 132, 130), anchor="mm")
    ic = Image.fromarray(icon.astype(np.uint8)).resize((icon.shape[1] * 2, icon.shape[0] * 2), Image.NEAREST)
    if dim:
        ic = Image.fromarray((np.asarray(ic, np.float32) * 0.45).astype(np.uint8))
    im.paste(ic, (w // 2 - ic.width // 2, 62))
    y = 62 + ic.height + 4
    d.text((w // 2, y + 8), where, font=font(16), fill=(200, 175, 160) if not dim else (120, 110, 110), anchor="mm")
    f = font(16)
    y += 26
    for line in wrap(d, text, f, w - 30):
        d.text((15, y), line, font=f, fill=(230, 226, 222) if not dim else (125, 120, 118))
        y += 20
    lab = {"exhausted": ("EXHAUSTED", (70, 20, 20), (255, 140, 130)), "unbuilt": ("NOT BUILT", (30, 30, 34), (170, 170, 180)),
           "ready": ("READY", (30, 60, 40), (150, 255, 170))}[state]
    d.rounded_rectangle((w // 2 - 80, h - 48, w // 2 + 80, h - 16), 8, fill=lab[1])
    d.text((w // 2, h - 32), lab[0], font=font(18, True), fill=lab[2], anchor="mm")
    return im


def window(frame_rgb, icons):
    W, H = 1440, 1080
    im = Image.new("RGB", (W, H), (7, 9, 10))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((12, 12, W - 13, H - 13), 18, fill=(10, 12, 13), outline=(150, 60, 50), width=3)
    d.text((W // 2, 50), "SUPERWEAPONS", font=font(40, True), fill=(240, 226, 214), anchor="mm")
    d.text((W // 2, 90), "The Belkosea Allied States - Classified Developments", font=font(20), fill=(190, 160, 150), anchor="mm")
    art = Image.fromarray(frame_rgb.astype(np.uint8)).resize((ART_W * 3, ART_H * 3), Image.NEAREST)
    ax, ay = (W - art.width) // 2, 116
    im.paste(art, (ax, ay))
    d.rectangle((ax - 3, ay - 3, ax + art.width + 2, ay + art.height + 2), outline=(190, 70, 60), width=3)
    cw, ch = 250, 452
    places = [(28, 116, cw, ch), (28, 116 + ch + 20, cw, ch), (W - 28 - cw, 116, cw, ch), (W - 28 - cw, 116 + ch + 20, cw, ch),
              ((W - 520) // 2, ay + art.height + 20, 520, H - 32 - (ay + art.height + 20))]
    for (n, t, s, wh), (x, y, w, h) in zip(CARDS, places):
        c = card(n, t, s, w, h, wh, icons[n])
        im.paste(c, (x, y), c)
    d.text((W - 300, H - 60), "A: Use the card", font=font(20), fill=(200, 180, 170), anchor="rm")
    d.text((W - 300, H - 34), "B: Back", font=font(20), fill=(200, 180, 170), anchor="rm")
    return im


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/superweapon"
    os.makedirs(out, exist_ok=True)
    fo = Fortress()
    fr = []
    for t in range(N_FRAMES):
        fr.append(fo.frame(t))
    pal = make_palette(fr, 64, 3)
    q = [pal[quantize(f, pal)] for f in fr]
    gif = [Image.fromarray(f.astype(np.uint8)).resize((ART_W * 3, ART_H * 3), Image.NEAREST) for f in q]
    gif[0].save(os.path.join(out, "fortress.gif"), save_all=True, append_images=gif[1:], duration=FRAME_MS, loop=0)
    Image.fromarray(np.concatenate([q[i] for i in range(0, N_FRAMES, 8)], axis=1).astype(np.uint8)).save(os.path.join(out, "fortress_strip.png"))
    icons = {}
    for i, (name, rgb) in enumerate(SPHERE_COLOURS.items()):
        sf = sphere_frames(rgb, i)
        sp = make_palette(sf, 32, 5 + i)
        icons[name] = [sp[quantize(f, sp, 8.0)] for f in sf]
    row = [np.concatenate([icons[n][t] for n in SPHERE_COLOURS], axis=1) for t in range(N_FRAMES)]
    sg = [Image.fromarray(r.astype(np.uint8)).resize((r.shape[1] * 3, r.shape[0] * 3), Image.NEAREST) for r in row]
    sg[0].save(os.path.join(out, "spheres.gif"), save_all=True, append_images=sg[1:], duration=FRAME_MS, loop=0)
    # the game's strips (0.84.0: ui/superweapon_ui.cpp): the fortress 64 frames side by side, the spheres one row each (Availyn ..
    # Mors), paletted
    strip = Image.fromarray(np.concatenate(q, axis=1).astype(np.uint8))
    strip.convert("P", palette=Image.Palette.ADAPTIVE, colors=256, dither=Image.Dither.NONE).save(os.path.join(out, "superweapon_fortress.png"), optimize=True)
    rows = [np.concatenate(icons[n], axis=1) for n in SPHERE_COLOURS]
    sph = Image.fromarray(np.concatenate(rows, axis=0).astype(np.uint8))
    sph.convert("P", palette=Image.Palette.ADAPTIVE, colors=256, dither=Image.Dither.NONE).save(os.path.join(out, "superweapon_spheres.png"), optimize=True)
    window(q[0], {n: icons[n][0] for n in icons}).save(os.path.join(out, "window.png"))
    wg = [window(q[t], {n: icons[n][t] for n in icons}).resize((960, 720), Image.LANCZOS) for t in range(N_FRAMES)]
    wg[0].save(os.path.join(out, "window.gif"), save_all=True, append_images=wg[1:], duration=FRAME_MS, loop=0)
    print("wrote", out, "palette", len(pal))


if __name__ == "__main__":
    main()
