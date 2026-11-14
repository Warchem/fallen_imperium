#!/usr/bin/env python3
"""Fallen Imperium showcase: tilesets, the height of a space battle, the height of a ground battle and a
Mahact / Yssaril negotiation, cut from the demo clips, with title cards, captions and crossfades.
Output: showcase.mp4 (1280x960, 20 fps). (0.57.2 demo; not part of the build.)

Needs, in D: demo_tilesets.mp4, demo_space_combat_retrowave.mp4, demo_ground_combat_nekro_vs_sol.mp4 (the 0.57.2
demo clips), and next to this script seg_offer.mp4 / seg_counter.mp4, made from the screen test's codec frames:
  TI4_SCALE=2 TI4_PREVIEW_MS=50 TI4_UITEST_ONLY=codecanim TI4_CODEC_ANIM="mahact,yssaril,offer,7600" ti4 --uitest DIR
  TI4_SCALE=2 TI4_PREVIEW_MS=50 TI4_UITEST_ONLY=codecanim TI4_CODEC_ANIM="yssaril,mahact,counter,6800" ti4 --uitest DIR
  ffmpeg -framerate 20 -i DIR/anim_%04d.ppm -c:v libx264 -crf 16 -pix_fmt yuv420p seg_offer.mp4   (each, 3.7 MB a frame)"""
import subprocess, sys, math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H, FPS = 1280, 960, 20
OUT = sys.argv[1] if len(sys.argv) > 1 else 'showcase.mp4'
D = '/mnt/user-data/outputs/'
HERE = os.path.dirname(os.path.abspath(__file__))
BOLD = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
REG = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
GOLD = (234, 182, 76)
PINK = (254, 88, 199)
CYAN = (110, 220, 235)
WHITE = (236, 238, 246)
DIM = (150, 160, 190)


def font(path, size):
    return ImageFont.truetype(path, size)


# ------------------------------------------------------------------ backgrounds
rng = np.random.default_rng(7)
STARS = [(rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0.3, 1.0), rng.uniform(0, 6.28), rng.uniform(0.6, 2.2))
         for _ in range(260)]


def gradient(top=(9, 15, 32), bottom=(6, 10, 22)):
    t = np.linspace(0, 1, H)[:, None]
    g = np.zeros((H, W, 3), np.float32)
    for k in range(3):
        g[:, :, k] = top[k] + (bottom[k] - top[k]) * t
    return g


_BASE = {}


def _base(arc):
    """The navy gradient, and (arc) a planet's night side low on the right with a lit rim, like the title screen."""
    if arc not in _BASE:
        img = Image.fromarray(gradient().astype(np.uint8)).convert('RGBA')
        if arc:
            cx, cy, r = W * 1.30, H * 1.62, 1010
            glow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
            ImageDraw.Draw(glow).ellipse([cx - r - 10, cy - r - 10, cx + r + 10, cy + r + 10], fill=(70, 120, 230, 150))
            glow = glow.filter(ImageFilter.GaussianBlur(26))
            img.alpha_composite(glow)
            disc = Image.new('RGBA', (W, H), (0, 0, 0, 0))
            d = ImageDraw.Draw(disc)
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(7, 11, 26, 255))
            d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(96, 150, 240, 255), width=3)
            img.alpha_composite(disc)
        _BASE[arc] = (img.convert('RGB'), arc and (lambda x, y: (x - W * 1.30) ** 2 + (y - H * 1.62) ** 2 < 1010 ** 2))
    return _BASE[arc]


def starfield(t, arc=True):
    base, inside = _base(arc)
    img = base.copy()
    d = ImageDraw.Draw(img)
    for x, y, b, ph, sp in STARS:
        if inside and inside(x, y):
            continue
        v = b * (0.65 + 0.35 * math.sin(ph + t * sp))
        c = tuple(int(min(255, 70 + 185 * v)) for _ in range(3))
        if b > 0.9:
            d.rectangle([x - 1, y - 1, x + 1, y + 1], fill=c)
        else:
            d.point((x, y), fill=c)
    a = np.asarray(img).astype(np.float32)
    return a + rng.normal(0, 1.1, a.shape[:2])[:, :, None].astype(np.float32)  # (a little grain: no banding in the video)


PAD_BG = None


def battle_pad():
    """The battle clips are 840x732: shown 1102x960 in the middle; the sides get the same navy and some stars."""
    global PAD_BG
    if PAD_BG is None:
        PAD_BG = starfield(0.0, arc=False)
    return PAD_BG


# ------------------------------------------------------------------ text helpers
def glow_text(layer, xy, text, f, col, glow=None, anchor='la', radius=6):
    """Text with a soft glow, drawn into an RGBA layer."""
    if glow:
        g = Image.new('RGBA', layer.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).text(xy, text, font=f, fill=glow + (200,), anchor=anchor)
        g = g.filter(ImageFilter.GaussianBlur(radius))
        layer.alpha_composite(g)
    ImageDraw.Draw(layer).text(xy, text, font=f, fill=col + (255,), anchor=anchor)


def caption_band(head, sub, where):
    """A section label: 'bottom' = a band over the hint bar, 'corner' = top-left box (battles)."""
    L = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(L)
    fh, fs = font(BOLD, 34), font(REG, 21)
    if where == 'bottom':
        fh = font(BOLD, 30)
        d.rectangle([0, H - 62, W, H], fill=(4, 7, 16, 238))
        d.line([0, H - 62, W, H - 62], fill=PINK + (200,), width=2)
        glow_text(L, (26, H - 31), head, fh, PINK, glow=PINK, radius=5, anchor='lm')
        tw = d.textlength(head, font=fh)
        d.text((26 + tw + 22, H - 30), sub, font=fs, fill=WHITE + (255,), anchor='lm')
    else:
        tw = max(d.textlength(head, font=fh), d.textlength(sub, font=fs))
        d.rounded_rectangle([18, 18, 18 + tw + 40, 18 + 98], radius=10, fill=(4, 7, 16, 215), outline=PINK + (180,), width=2)
        glow_text(L, (38, 28), head, fh, PINK, glow=PINK, radius=5)
        d.text((38, 76), sub, font=fs, fill=WHITE + (255,))
    return np.asarray(L).astype(np.float32)


def blend(frame, layer, a):
    if a <= 0:
        return frame
    al = layer[:, :, 3:4] / 255.0 * a
    return frame * (1 - al) + layer[:, :, :3] * al


# ------------------------------------------------------------------ clip readers (streamed: one frame at a time)
def read_clip(path, start, dur, fit=None):
    """Frames (float32 HxWx3) of path from start for dur seconds at 20 fps; fit='battle' = 1102x960 on the pad."""
    vf = f'fps={FPS}'
    if fit == 'battle':
        vf += ',scale=1102:960:flags=lanczos'
        w, h = 1102, 960
    else:
        vf += f',scale={W}:{H}'
        w, h = W, H
    p = subprocess.Popen(['ffmpeg', '-v', 'error', '-ss', f'{start:.3f}', '-t', f'{dur:.3f}', '-i', path, '-vf', vf,
                          '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE)
    size = w * h * 3
    while True:
        buf = p.stdout.read(size)
        if len(buf) < size:
            break
        f = np.frombuffer(buf, np.uint8).reshape(h, w, 3)
        if fit == 'battle':
            base = battle_pad().copy()
            x0 = (W - w) // 2
            base[:, x0:x0 + w] = f
            yield base
        else:
            yield f.astype(np.float32)
    p.wait()


# ------------------------------------------------------------------ title cards
def title_card(dur, lines, t0=0.0, fin=0.0, fout=0.0):
    """lines: list of (text, font, colour, y, glow, appear_at)"""
    n = int(round(dur * FPS))
    layers = []
    for text, f, col, y, glow, at in lines:
        L = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        glow_text(L, (W // 2, y), text, f, col, glow=glow, anchor='mm', radius=9)
        layers.append((np.asarray(L).astype(np.float32), at))
    rule = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(rule).line([W // 2 - 330, 445, W // 2 + 330, 445], fill=GOLD + (220,), width=3)
    rule = np.asarray(rule).astype(np.float32)
    for i in range(n):
        fr = starfield(t0 + i / FPS)
        for L, at in layers:
            fr = blend(fr, L, min(1.0, max(0.0, (i / FPS - at) / 0.6)))
        fr = blend(fr, rule, min(1.0, max(0.0, (i / FPS - 0.5) / 0.6)))
        a = 1.0
        if fin > 0:
            a = min(a, (i + 1) / (fin * FPS))
        if fout > 0:
            a = min(a, (n - i) / (fout * FPS))
        yield fr * max(0.0, min(1.0, a))


def with_caption(frames, layer, show=3.6, fade=0.4, start=0.15):
    for i, f in enumerate(frames):
        t = i / FPS
        a = min(1.0, max(0.0, (t - start) / fade)) * min(1.0, max(0.0, (start + show - t) / fade))
        yield blend(f, layer, a)


def run(pieces, out):
    """pieces: list of (frame iterator, crossfade seconds into this piece). Streams to ffmpeg."""
    enc = subprocess.Popen(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', str(FPS),
                            '-i', '-', '-c:v', 'libx264', '-preset', 'slow', '-crf', '17', '-x264-params', 'aq-mode=3', '-pix_fmt', 'yuv420p',
                            '-movflags', '+faststart', out], stdin=subprocess.PIPE)
    count = 0

    def put(f):
        nonlocal count
        enc.stdin.write(np.clip(f, 0, 255).astype(np.uint8).tobytes())
        count += 1

    tail = []  # the previous piece's last frames, held back for the next crossfade
    for idx, (it, xf) in enumerate(pieces):
        nxt = pieces[idx + 1][1] if idx + 1 < len(pieces) else 0.0
        hold = int(round(nxt * FPS))
        k = len(tail)
        buf = []
        for j, f in enumerate(it):
            if j < k:  # blend with the previous piece's tail
                w = (j + 1) / (k + 1)
                f = tail[j] * (1 - w) + f * w
            buf.append(f)
            if len(buf) > hold:
                put(buf.pop(0))
        tail = buf
    for f in tail:
        put(f)
    enc.stdin.close()
    enc.wait()
    print(f'{count} frames = {count / FPS:.1f} s')


def main():
    hT, hS = font(BOLD, 92), font(REG, 30)
    pieces = []
    # 1. opening
    pieces.append((title_card(3.4, [
        ('FALLEN IMPERIUM', hT, GOLD, 380, GOLD, 0.15),
        ('A fan-made digital Twilight Imperium', hS, WHITE, 500, None, 0.9),
        ('for Windows and the Steam Deck', hS, DIM, 545, None, 1.2),
    ], fin=0.6), 0.0))
    # 2. tilesets: for each, the caption + map + a zoom (A), then the expanded tile (B)
    cap = caption_band('THREE TILESETS', 'Retrowave pixel art, Voidfall painted HD, Simplistic - switch any time', 'bottom')
    for n, (a0, b0) in enumerate(((0.0, 6.30), (12.45, 18.75), (24.90, 31.20))):
        A = read_clip(D + 'demo_tilesets.mp4', a0, 4.3)
        if n == 0:
            A = with_caption(A, cap, show=3.9)
        pieces.append((A, 0.5 if n == 0 else 0.3))
        pieces.append((read_clip(D + 'demo_tilesets.mp4', b0, 2.55), 0.25))
    # 3. the height of the space battle (Retrowave)
    pieces.append((with_caption(read_clip(D + 'demo_space_combat_retrowave.mp4', 2.0, 8.4, fit='battle'),
                                caption_band('SPACE COMBAT', 'Every ship fights - dice, hits, a war sun goes down', 'corner'), show=3.4), 0.45))
    # 4. the height of the ground battle (Nekro Virus vs the Federation of Sol)
    pieces.append((with_caption(read_clip(D + 'demo_ground_combat_nekro_vs_sol.mp4', 2.2, 7.2, fit='battle'),
                                caption_band('GROUND COMBAT', 'The Nekro Virus invades Sol', 'corner'), show=3.4), 0.45))
    # 5. negotiation: the Mahact offer, the Yssaril counter
    pieces.append((with_caption(read_clip(os.path.join(HERE, 'seg_offer.mp4'), 0, 7.6),
                                caption_band('NEGOTIATIONS', 'The Mahact make an offer over the comms codec...', 'bottom'), show=7.2, start=0.3), 0.45))
    pieces.append((with_caption(read_clip(os.path.join(HERE, 'seg_counter.mp4'), 0, 6.8),
                                caption_band('NEGOTIATIONS', '...and the Yssaril counter. Every faction has its own voice.', 'bottom'),
                                show=6.4, start=0.05), 0.35))
    # 6. closing
    pieces.append((title_card(4.6, [
        ('FALLEN IMPERIUM', hT, GOLD, 380, GOLD, 0.1),
        ('Version 0.57.2  -  Pre-Beta', hS, WHITE, 500, None, 0.5),
        ('github.com/Warchem/fallen_imperium', font(BOLD, 30), CYAN, 548, CYAN, 0.9),
        ('Unofficial fan project. Twilight Imperium is a trademark of Fantasy Flight Games.', font(REG, 19), DIM, 860, None, 1.3),
    ], t0=4.0, fout=0.8), 0.6))
    run(pieces, OUT)


if __name__ == '__main__':
    main()
