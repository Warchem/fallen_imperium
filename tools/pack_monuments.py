#!/usr/bin/env python3
"""Monuments+ (0.82.0): copy the animated monument pictures the game uses into assets/monuments/<key>.png.

usage: pack_monuments.py STRIPDIR [OUTDIR]

STRIPDIR is the output folder of tools/gen_monument_anim.py (<key>_strip.png: 64 frames of 160 x 120 side by side).
Only the monuments in src/engine/gen/monuments.inc are copied (the game's own keys). Each picture is saved paletted
when it has 256 colours or fewer (checked lossless), else as RGB.
"""
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def game_keys():
    src = open(os.path.join(ROOT, "src/engine/gen/monuments.inc"), encoding="utf-8").read()
    return re.findall(r'^\s*\{"([a-z0-9_]+)",', src, re.M)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    strips = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "assets/monuments")
    os.makedirs(out, exist_ok=True)
    keys = game_keys()
    total = 0
    for k in keys:
        src = os.path.join(strips, k + "_strip.png")
        if not os.path.exists(src):
            print("missing", src)
            continue
        im = Image.open(src).convert("RGB")
        if im.size != (160 * 64, 120):
            print("wrong size", src, im.size)
            continue
        pic = im
        colours = im.getcolors(256)
        if colours is not None:
            p = im.convert("P", palette=Image.Palette.ADAPTIVE, colors=256, dither=Image.Dither.NONE)
            if p.convert("RGB").tobytes() == im.tobytes():
                pic = p
        path = os.path.join(out, k + ".png")
        pic.save(path, optimize=True)
        total += os.path.getsize(path)
    print(len(keys), "monuments,", total // 1024, "KB")


if __name__ == "__main__":
    main()
