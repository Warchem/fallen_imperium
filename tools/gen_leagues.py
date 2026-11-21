#!/usr/bin/env python3
"""League mode data (0.80.0): the League Designer page's leagues, rivals, points and bonuses -> src/game/gen/leagues.inc.

Source: docs/design/league_data/ (a snapshot of the owner's League Designer page, claude.ai artifact CtzTvRVoiHfEXD56wrzYpH:
tiers/*.json, rivals/*.json, settings.json). Re-read the page into that folder and run this again after he edits it.
Usage: tools/gen_leagues.py [DATA_DIR] [OUT]
"""
import json, os, sys, glob

DATA = sys.argv[1] if len(sys.argv) > 1 else "docs/design/league_data"
OUT = sys.argv[2] if len(sys.argv) > 2 else "src/game/gen/leagues.inc"

# the page's own ids for the four leagues the owner made himself -> readable ones (they go into the profile file)
RENAME = {"myehsm738uupg98wgdvk": "grand", "3gxwsa0clelhz5xwhlnf": "spacequest", "36qqvmenqz9jjoeanh4z": "scpt",
          "1ktiruyb4bmuhtguz5wq": "caco"}
# what each league gives: its emblem and the player colour it unlocks (pcolours index; -1 none) - from the reward texts and
# the Unlockables page (artifact DyXJxSqZyQuPx7EDGM1mXB)
REWARD = {"bronze": ("bronze", 10), "silver": ("silver", 9), "gold": ("gold", 8), "ultimate": ("ultimate", 23),
          "builder": ("", -1), "grand": ("crown", 20), "spacequest": ("starfarer", 15), "scpt": ("laurel", 16),
          "caco": ("crest", 12), "lantern": ("lantern", 11), "ionstorm": ("bolt", 14), "sunset": ("sun", 17),
          "pearl": ("pearl", 13), "grid": ("circuit", 21), "blueprint": ("compass", 22), "crystal": ("shard", 18),
          "ashfall": ("ember", 19)}
DIFF = {"easy": 0, "normal": 1, "hard": 2, "competitive": 3, "ai": 4, "rising": 5, "mixed": 6}
GALAXY = {"random": 0, "draft": 1, "seeded": 2, "choice": 3}
POOL = {"any": 0, "favoured": 1, "weighted": 2, "player": 3}
MODS = {"codex": 1, "uncharted": 2, "te": 4, "ds": 8}  # engine/state.h kModCodex .. kModDiscordant
ARCH = ["politician", "peacekeeper", "warlord", "betrayer", "swindler", "merchant", "opportunist", "rusher", "turtler", "chaotic"]


def c(s):
    s = s if s is not None else ""
    for a, b in (("\u2018", "'"), ("\u2019", "'"), ("\u201c", '"'), ("\u201d", '"'), ("\u2013", "-"), ("\u2014", "-")):
        s = s.replace(a, b)  # (the game's fonts have no curly quotes)
    return json.dumps(s, ensure_ascii=False)


def main():
    tiers = {}
    for f in glob.glob(os.path.join(DATA, "tiers", "*.json")):
        k = os.path.basename(f)[:-5]
        tiers[RENAME.get(k, k)] = json.load(open(f))
    names = {t["name"].strip().lower(): k for k, t in tiers.items()}
    rows = []
    for k, t in sorted(tiers.items(), key=lambda kv: kv[1].get("order", 0)):
        st = t.get("status", "after")
        after = ""
        if st == "after" or st == "other":
            u = (t.get("unlock") or "").lower()
            for n, kk in sorted(names.items(), key=lambda x: -len(x[0])):
                if n in u and kk != k:
                    after = kk
                    break
        fav = [f for f, v in sorted((t.get("factions") or {}).items()) if v == "fav"]
        ban = [f for f, v in sorted((t.get("factions") or {}).items()) if v == "ban"]
        pf = [a for a, v in sorted((t.get("personalities") or {}).items()) if v == "fav"]
        pb = [a for a, v in sorted((t.get("personalities") or {}).items()) if v == "ban"]
        mods = sum(b for m, b in MODS.items() if (t.get("modules") or {}).get(m))
        r = t.get("rules") or {}
        emb, col = REWARD.get(k, ("", -1))
        rows.append("    {%s, %s, 0x%06X, %d, %s, %s, %d, %d, %d, %d, %d, %d, %d, %s, %s, %s, %s, %d, %s, %s, %s, %s, %d, %s, %s, %s, %d, %s}," % (
            c(k), c(t["name"].strip()), int(t.get("color", "#7c6cff")[1:], 16), int(t.get("order", 0)), c(after), c(t.get("unlock", "")),
            int(t.get("threshold", 15)), int(t.get("maxGames", 6)), int(t.get("players", 6)), int(t.get("vp", 10)),
            DIFF.get(t.get("difficulty", "hard"), 2), GALAXY.get(t.get("galaxy", "draft"), 1), mods,
            "true" if r.get("factionDraft") else "false", "true" if r.get("kingslay") else "false",
            "true" if r.get("freeRefills") else "false", "true" if r.get("x1") else "false",
            POOL.get(t.get("pool", "any"), 0), c(",".join(fav)), c(",".join(ban)), c(",".join(pf)), c(",".join(pb)),
            int(t.get("rivals", 0)), c((t.get("reward") or "").strip()), c((t.get("notes") or "").strip()), c(emb), col,
            "true" if t.get("builder") else "false"))
    rivals = []
    for f in glob.glob(os.path.join(DATA, "rivals", "*.json")):
        d = json.load(open(f))
        k = os.path.basename(f)[:-5]
        lg = RENAME.get(d.get("league", ""), d.get("league", ""))
        rivals.append((lg, int(d.get("order", 0)), k, d))
    order = {k: t.get("order", 0) for k, t in tiers.items()}
    rivals.sort(key=lambda x: (order.get(x[0], 99), x[1], x[2]))
    rrows = []
    for lg, _, k, d in rivals:
        a = d.get("arch", "")
        rrows.append("    {%s, %s, %s, %d, %s, %s, %s}," % (c(k), c(d.get("name", "").strip()), c(lg), ARCH.index(a) if a in ARCH else -1,
                                                       c(d.get("faction", "")), c((d.get("grudge") or "").strip()), c((d.get("taunt") or "").strip())))
    s = json.load(open(os.path.join(DATA, "settings.json")))
    place = s["settings"]["place"]
    out = ["// Generated by tools/gen_leagues.py from docs/design/league_data (the owner's League Designer page). Do not edit.",
           "const LeagueDef kLeagues[] = {"] + rows + ["};", "const RivalDef kRivals[] = {"] + rrows + ["};",
           "const int kPlacePoints[8] = {%s};" % ", ".join(str(int(p)) for p in place),
           "const BonusDef kBonuses[] = {"] + ["    {%s, %s, %d}," % (c(b[0]), c(b[1]), b[2]) for b in s["bonuses"]] + ["};",
           "const char* const kLeaguePitch = %s;" % c(s["settings"]["pitch"]),
           "const char* const kLeagueTieRule = %s;" % c(s["settings"]["tieRule"])]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w").write("\n".join(out) + "\n")
    print("%s: %d leagues, %d rivals" % (OUT, len(rows), len(rrows)))


if __name__ == "__main__":
    main()
