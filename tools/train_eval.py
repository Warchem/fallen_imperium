#!/usr/bin/env python3
"""Self-play training of the A.I. difficulty's evaluation (0.86.0; owner 2026-11-24: "that sounds great").

The A.I. level plays its best few options forward on copies of the game to the end of the round's status phase and then
has to judge each copy: who is better placed to win? Until 0.86.0 that judgement was a hand-made formula (ai_think.cpp
worth(): 10 x VP + 0.5 x objective progress + 0.6 x planet resources+influence + 0.4 x fleet). This script learns it
instead, from games the bots played against each other.

  1. ti4 --simulate with TI4_EVALDUMP=FILE writes, at the end of every round's status phase, each player's position as
     numbers (aiEvalFeatures, ai_think.cpp) - "F seed round player vpGoal f1 .. fN" - and who won each game ("W seed
     winner round vp...").
  2. This script fits, per stage of the game (rounds 1-2, 3-4, 5, 6+), a "conditional logit": every player gets a
     score w . f, and the chance that player wins the game is exp(score) / sum over the table of exp(score). The
     weights w are chosen so the players who actually went on to win get the highest chances (maximum likelihood, with
     a little L2 shrinkage). More of a good thing never counts against a player: the weights of the plain features are
     kept at zero or above (a negative one would only be the games' coincidence - e.g. relic holders who spent their
     turns exploring - and would teach the A.I. to avoid relics); the two VP shapes (squared, lead) are free.
     Games split 80 / 20 by seed: the 20 % only measure how well it predicts.
  3. It compares that with the hand-made formula on the same held-out positions (the formula's best temperature per
     stage is fitted too, so it gets a fair chance) and writes src/ai/gen/eval_weights.inc.

Usage: tools/train_eval.py DUMP [DUMP...] [--out src/ai/gen/eval_weights.inc] [--l2 0.01]
"""
import argparse
import collections
import sys

import numpy as np
from scipy.optimize import minimize

RAW = ["vp", "objective", "resinf", "fleet", "ground", "structures", "techs", "tokens", "secrets_open", "action_cards",
       "tg_comm", "mecatol", "relics"]
# derived features (the C++ side computes the same, in the same order - ai_think.cpp learnedScore)
NAMES = RAW + ["vp_sq", "vp_lead"]
BUCKETS = [(1, 2), (3, 4), (5, 5), (6, 99)]


def bucket_of(r):
    for i, (a, b) in enumerate(BUCKETS):
        if a <= r <= b:
            return i
    return len(BUCKETS) - 1


def derive(X, goal):
    """X: players x raw features of one table at one moment."""
    vp = X[:, 0]
    best_other = np.array([np.max(np.delete(vp, i)) if len(vp) > 1 else 0.0 for i in range(len(vp))])
    vp_sq = vp * vp / max(goal, 1)
    lead = vp - best_other
    return np.column_stack([X, vp_sq, lead])


def load(paths):
    feats = collections.defaultdict(dict)  # (seed) -> round -> list of (player, goal, raw)
    wins = {}
    for path in paths:
        with open(path) as f:
            for line in f:
                t = line.split()
                if not t:
                    continue
                if t[0] == "F":
                    seed, rnd, p, goal = int(t[1]), int(t[2]), int(t[3]), int(t[4])
                    raw = [float(x) for x in t[5:]]
                    feats[seed].setdefault(rnd, []).append((p, goal, raw))
                elif t[0] == "W":
                    seed, winner = int(t[1]), int(t[2])
                    vps = [int(x) for x in t[4:]]
                    wins[seed] = (winner, vps)
    tables = []  # (seed, round, X derived, winner index)
    for seed, rounds in feats.items():
        if seed not in wins:
            continue
        winner, vps = wins[seed]
        if winner < 0:
            continue
        for rnd, rows in rounds.items():
            rows.sort()
            if len(rows) < 3 or rows[-1][0] != len(rows) - 1:
                continue
            goal = rows[0][1]
            if vps[winner] < goal:  # (won some other way)
                continue
            X = derive(np.array([r[2] for r in rows]), goal)
            tables.append((seed, rnd, X, winner))
    return tables


def nll(w, tabs, l2, scale):
    total, grad = 0.0, np.zeros_like(w)
    for X, y in tabs:
        Z = (X / scale) @ w
        Z = Z - Z.max()
        e = np.exp(Z)
        P = e / e.sum()
        total -= np.log(P[y] + 1e-12)
        grad -= (X[y] / scale) - P @ (X / scale)
    n = max(len(tabs), 1)
    return total / n + l2 * (w @ w), grad / n + 2 * l2 * w


def score(tabs, util):
    """mean log-loss and how often the player with the best score won"""
    ll, hit = 0.0, 0
    for X, y in tabs:
        Z = util(X)
        Z = Z - Z.max()
        P = np.exp(Z) / np.exp(Z).sum()
        ll -= np.log(P[y] + 1e-12)
        hit += int(np.argmax(Z) == y)
    n = max(len(tabs), 1)
    return ll / n, hit / n


def lit(v):
    """a C++ float literal ("0.f", "0.0812f", "1e-05f")"""
    t = f"{v:.6g}"
    if "." not in t and "e" not in t:
        t += "."
    return t + "f"


def hand(X):  # ai_think.cpp worth() before 0.86.0
    return 10 * X[:, 0] + 0.5 * X[:, 1] + 0.6 * X[:, 2] + 0.4 * X[:, 3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumps", nargs="+")
    ap.add_argument("--out", default="src/ai/gen/eval_weights.inc")
    ap.add_argument("--l2", type=float, default=0.01)
    args = ap.parse_args()
    tables = load(args.dumps)
    games = sorted({t[0] for t in tables})
    test_seeds = set(games[i] for i in range(len(games)) if i % 5 == 4)
    print(f"{len(games)} games, {len(tables)} positions ({len(test_seeds)} games held out)")
    allW = []
    report = []
    for b, (lo, hi) in enumerate(BUCKETS):
        tr = [(X, y) for s, r, X, y in tables if bucket_of(r) == b and s not in test_seeds]
        te = [(X, y) for s, r, X, y in tables if bucket_of(r) == b and s in test_seeds]
        if not tr:
            allW.append(np.zeros(len(NAMES)))
            continue
        allX = np.vstack([X for X, _ in tr])
        scale = allX.std(axis=0) + 1e-6
        w0 = np.zeros(len(NAMES))
        bounds = [(0.0, None)] * len(RAW) + [(None, None)] * (len(NAMES) - len(RAW))
        res = minimize(nll, w0, args=(tr, args.l2, scale), jac=True, method="L-BFGS-B", bounds=bounds)
        w = res.x / scale
        allW.append(w)
        # the hand-made formula with its best temperature on the same training positions
        temps = np.linspace(0.005, 0.5, 100)
        best_t = min(temps, key=lambda t: score(tr, lambda X: t * hand(X))[0])
        n_players = np.mean([len(X) for X, _ in te]) if te else 0
        base = np.log(n_players) if te else 0
        lw, aw = score(te, lambda X: X @ w)
        lh, ah = score(te, lambda X: best_t * hand(X))
        report.append((lo, hi, len(tr), len(te), base, lh, ah, lw, aw))
        print(f"rounds {lo}-{hi if hi < 99 else '+'}: train {len(tr)}, test {len(te)} | log-loss: even odds {base:.3f}, "
              f"hand-made {lh:.3f} (best guess wins {ah:.0%}), learned {lw:.3f} ({aw:.0%})")
        for name, v in zip(NAMES, w):
            print(f"    {name:14s} {v:+.4f}")
    with open(args.out, "w") as f:
        f.write("// Generated by tools/train_eval.py from self-play games (Competitive bots, TI4_EVALDUMP) - do not edit.\n")
        f.write("// A player's score = sum of weight x feature; the chance to win = exp(score) / sum over the table.\n")
        f.write("// Features: " + ", ".join(NAMES) + "\n")
        f.write("// Buckets: rounds " + ", ".join(f"{a}-{b if b < 99 else '+'}" for a, b in BUCKETS) + "\n")
        for (lo, hi, ntr, nte, base, lh, ah, lw, aw) in report:
            f.write(f"//   rounds {lo}-{hi if hi < 99 else '+'}: held-out log-loss even {base:.3f}, hand-made {lh:.3f} "
                    f"({ah:.0%} top pick wins), learned {lw:.3f} ({aw:.0%}); {ntr} / {nte} positions\n")
        f.write(f"constexpr int kEvalBuckets = {len(BUCKETS)};\n")
        f.write(f"constexpr int kEvalNames = {len(NAMES)};\n")
        f.write("constexpr int kEvalBucketFrom[kEvalBuckets] = {" + ", ".join(str(a) for a, _ in BUCKETS) + "};\n")
        f.write("constexpr float kEvalWeights[kEvalBuckets][kEvalNames] = {\n")
        for w in allW:
            f.write("    {" + ", ".join(lit(v) for v in w) + "},\n")
        f.write("};\n")
    print("wrote", args.out)


if __name__ == "__main__":
    sys.exit(main())
