#!/usr/bin/env python3
"""0.58.0, the Miyoo package: the portrait atlas (64 px frames, one row per faction) cut into one strip per faction,
art/portraits/<key>-miyoo.png - the game loads only the strips it shows (the whole atlas would take ~25 MB of the
Miyoo's ~100 MB). Usage: split_strips.py ATLAS.png portrait_keys.inc OUTDIR"""
import os
import re
import sys

from PIL import Image


def main(atlas, keys_inc, out):
    keys = re.findall(r'"([^"]+)"', open(keys_inc).read().split('kPortraitKeys')[1].split('};')[0])
    size = int(re.search(r'kPortraitSize\s*=\s*(\d+)', open(keys_inc).read()).group(1))
    im = Image.open(atlas).convert('RGB')
    os.makedirs(out, exist_ok=True)
    for r, k in enumerate(keys):
        im.crop((0, r * size, im.width, (r + 1) * size)).save(os.path.join(out, k + '-miyoo.png'), optimize=True)
    print(f'{len(keys)} strips in {out}')


if __name__ == '__main__':
    main(*sys.argv[1:4])
