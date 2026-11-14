#!/usr/bin/env python3
"""Standard (printed) galaxy layouts without hyperlanes - 0.58.0, owner: "if players want to create a standard map
regardless of player count, it should be allowed".

The home positions and board shapes are the Living Rules Reference's "Game Board Setups" diagrams (page 6, read off
the diagram, not copied): three-player (28 systems), four-player, five-player (no hyperlanes, with the trade goods
for the three seats placed worse), eight-player (61 systems). Seven players have no printed board without
hyperlanes: the seven-player board with its hyperlane gap filled by ordinary systems. Six players use the normal
layout (it has none).

Each player's tiles (blue / red) follow LRR setup step 6; the extra tiles the speaker places next to Mecatol Rex are
"extra" cells in ring 1. For the random galaxy each other cell is given to one player (a "slice": the cells nearest
their home, every player the same number), found with an assignment that keeps the cells close to their owner.

Output: src/engine/gen/std_layouts.inc
  LayoutCell kind 0 fixed (Mecatol Rex), 1 home (a = seat, b = trade goods at the start), 2 slice cell (a = seat,
  b = slot), 3 extra tile (a = 0 blue, 1 red, 2 either); StdLayoutDef {players, blue, red per player, first, count}.
"""
import os
import numpy as np
from scipy.optimize import linear_sum_assignment

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'engine', 'gen', 'std_layouts.inc')
MECATOL_DEF = 17  # kSystems index of "18" (gamedata.inc)

DIRS = [(0, -1), (1, -1), (1, 0), (0, 1), (-1, 1), (-1, 0)]  # N, NE, SE, S, SW, NW (src/galaxy/hex.h)
WALK = [2, 3, 4, 5, 0, 1]


def ring(radius):
    if radius == 0:
        return [(0, 0)]
    h, out = (0, -radius), []
    for side in range(6):
        for _ in range(radius):
            out.append(h)
            d = DIRS[WALK[side]]
            h = (h[0] + d[0], h[1] + d[1])
    return out


def hex_of(pos):
    return ring(pos // 100)[pos % 100 - 1] if pos else (0, 0)


def dist(a, b):
    dq, dr = a[0] - b[0], a[1] - b[1]
    return (abs(dq) + abs(dr) + abs(dq + dr)) // 2


def pix(h):
    return 1.5 * h[0], 1.7320508 * (h[1] + h[0] * 0.5)


def all_positions(rings):
    return [r * 100 + i + 1 for r in range(1, rings + 1) for i in range(len(ring(r)))]


# players: (rings, cells used (None = all), homes clockwise from the top, {home: trade goods}, blue, red, extras)
SPECS = {
    3: dict(rings=3, cells=all_positions(2) + [303, 305, 309, 311, 315, 317], homes=[304, 310, 316], bonus={},
            blue=6, red=2, extras=[]),
    4: dict(rings=3, cells=None, homes=[305, 309, 314, 318], bonus={}, blue=5, red=3, extras=[]),
    5: dict(rings=3, cells=None, homes=[304, 307, 310, 314, 318], bonus={304: 2, 307: 4, 310: 2}, blue=4, red=2,
            extras=[1]),
    # (blue 5 + red 2 each and 4 extras of either colour: the base game + Prophecy of Kings hold 36 blue, 18 red)
    7: dict(rings=4, cells=None, homes=[401, 404, 407, 410, 416, 419, 422], bonus={}, blue=5, red=2, extras=[2, 2, 2, 2]),
    8: dict(rings=4, cells=None, homes=[401, 404, 407, 410, 413, 416, 419, 422], bonus={}, blue=4, red=2,
            extras=[0, 0, 1, 1]),
}


def build(n, sp):
    cells = sp['cells'] if sp['cells'] is not None else all_positions(sp['rings'])
    homes = sp['homes']
    free = [p for p in cells if p not in homes]
    k = sp['blue'] + sp['red']
    ex = len(sp['extras'])
    assert len(free) == n * k + ex, (n, len(free), n * k + ex)
    # columns: each seat k times, then the extras (ring 1 only)
    cols = [s for s in range(n) for _ in range(k)] + [-1] * ex
    cost = np.zeros((len(free), len(cols)))
    for i, p in enumerate(free):
        h = hex_of(p)
        hx, hy = pix(h)
        for j, s in enumerate(cols):
            if s < 0:
                cost[i, j] = 0.0 if p // 100 == 1 else 1e6
            else:
                home = hex_of(homes[s])
                d = dist(h, home)
                ox, oy = pix(home)
                # nearest first; then the closer in a straight line (ties between equally far seats)
                cost[i, j] = d * d * 10 + ((hx - ox) ** 2 + (hy - oy) ** 2) * 0.01
    rows, colsel = linear_sum_assignment(cost)
    out = [(0, 0, MECATOL_DEF, 0)]
    for s, p in enumerate(homes):
        out.append((p, 1, s, sp['bonus'].get(p, 0)))
    slot = [0] * n
    exi = 0
    for i, j in sorted(zip(rows, colsel), key=lambda x: free[x[0]]):
        s = cols[j]
        if s < 0:
            out.append((free[i], 3, sp['extras'][exi], 0))
            exi += 1
        else:
            out.append((free[i], 2, s, slot[s]))
            slot[s] += 1
    # each slice: report how far its cells are from the owner's home (for the notes)
    far = []
    for s in range(n):
        ds = [dist(hex_of(c[0]), hex_of(homes[s])) for c in out if c[1] == 2 and c[2] == s]
        far.append(sum(ds) / len(ds))
    print(f'{n} players: {len(cells) + 1} systems, slices of {k}, extras {ex}, mean distance to home per slice ' +
          ' '.join(f'{f:.2f}' for f in far))
    return out


def main():
    lines = ['// Generated by tools/gen_std_layouts.py - the printed galaxy layouts without hyperlanes (0.58.0).',
             '// pos = ring*100 + index (index 1 = north, clockwise); kind 0 fixed (Mecatol Rex), 1 home (seat, trade',
             '// goods at the start), 2 slice cell (seat, slot), 3 extra tile next to Mecatol Rex (0 blue, 1 red, 2 either).']
    cells, defs = [], []
    for n in sorted(SPECS):
        sp = SPECS[n]
        c = build(n, sp)
        defs.append((n, sp['blue'], sp['red'], len(cells), len(c)))
        cells += c
    lines.append(f'const LayoutCell kStdLayoutCells[{len(cells)}] = {{')
    for p, kind, a, b in cells:
        lines.append(f'  {{{p}, {kind}, {a}, {b}}},')
    lines.append('};')
    lines.append(f'const StdLayoutDef kStdLayouts[{len(defs)}] = {{')
    for d in defs:
        lines.append('  {%d, %d, %d, %d, %d},' % d)
    lines.append('};')
    with open(OUT, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('wrote', OUT)


if __name__ == '__main__':
    main()
