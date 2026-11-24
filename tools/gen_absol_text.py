#!/usr/bin/env python3
"""Absol's Agendas (0.81.0): the cards' own wording (owner 2026-11-21: new content is not paraphrased) from the AsyncTI4 data
set (data/agendas/absol.json, public domain) into src/engine/gen/absol_text.inc - entries of card_text.cpp's kAgText:
{key, For / elect text, Against text, done}. Only obvious misspellings are fixed; "For: " / "Against: " are dropped (the
screens name the outcome); curly quotes become straight ones (the fonts have no curly quotes).

Usage:  python3 tools/gen_absol_text.py [path to .../src/main/resources]
"""
import json
import os
import re
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/ext/async/src/main/resources")
OUT = "src/engine/gen/absol_text.inc"
FIXES = [("they they", "they"), ("\u201c", '"'), ("\u201d", '"'), ("\u2019", "'"), ("\u2018", "'"), ("\u2014", " - ")]


# Version 4 of the printed cards (Absol's own package, sent by the owner 2026-11-21: "Version 4 Cards Without Bleed") differs
# from the AsyncTI4 data in places - the printed card wins. (key, side 1 = For / elect text, 2 = Against, old, new)
V4 = [
    ("absol_affirmation", 2, "with the current laws, and place", "with the current laws and place"),
    ("absol_standardization", 1, "until they have 3 tokens in their tactic pool, 3 tokens in their fleet pool, and 2 tokens in their strategy pool. They",
     "until they have:\n3 tokens in their tactic pool\n3 tokens in their fleet pool\n2 tokens in their strategy pool\nThey"),
    ("absol_checks", 1, "in a three- or four-player game", "in a 3- or 4-player game"),
    ("absol_redistribution", 1, "then chooses one other player", "then chooses 1 other player"),
    ("absol_redistribution", 1, "that player may place 3 infantry", "that player places 3 infantry"),
    ("absol_covert", 1, "it is played as normal and the speaker draws the next card.", "it is revealed and the speaker draws the next card instead."),
    ("absol_covert", 1, "elect player, etc);", "elect player, etc.);"),
    ("absol_spending", 1, "\n- use the PRODUCTION", "\n- Use the PRODUCTION"),
    ("absol_spending", 1, "\n- research up to", "\n- Research up to"),
    ("absol_spending", 1, "\n- place 1 structure", "\n- Place 1 structure"),
    ("absol_spending", 1, "\n- draw 1 action card", "\n- Draw 1 action card"),
    ("absol_spending", 1, "\n- explore a planet", "\n- Explore a planet"),
    ("absol_arbiter", 1, "(from most votes to least)", "(in order of most votes to least)"),
    ("absol_mutiny", 1, 'If all players voted "For", the speaker', 'If all players voted "For," the speaker'),
    ("absol_constitution", 2, 'Every player who voted "Against" or abstained must give a player who voted "For" a promissory note from their hand, if possible.',
     'Each player who voted "Against" or abstained must give a player who voted "For" 1 promissory note from their hand, if able.'),
    ("absol_pax", 1, "Players may not receive Ceasefire", "Players cannot receive Ceasefire"),
    ("absol_execution", 1, "The players that voted for the elected player", "The other players that voted for the elected player"),
    ("absol_schematics", 1, "That unit upgrade loses the SUSTAIN DAMAGE", "That unit type loses the SUSTAIN DAMAGE"),
    ("absol_seeds", 2, "to each of their tactic, fleet, and strategy pools", "to each of their tactics, fleet, and strategy pools"),
    ("absol_shared", 1, "explore any frontier tokens in systems that contain anomalies and their ships", "explore any frontier tokens in anomalies that contain their ships"),
    ("absol_swords", 1, "may explore every planet from which", "may explore each planet from which"),
    ("absol_limits", 1, "may not take or receive", "cannot take or receive"),
    ("absol_limits", 1, "If a control token from every player", "If a control token from each player"),
    ("absol_measures", 2, 'The player that cast the most votes "For" chooses one strategy card', 'The player who cast the most votes "For" chooses 1 strategy card'),
    ("absol_recon", 2, "takes a command token from the reinforcements", "takes 1 command token from the reinforcements"),
    ("absol_recon", 2, "systems that contain alpha or beta wormholes of their choice.", "systems that contain alpha or beta wormholes."),
    ("absol_whresearch", 1, "may research a technology", "may research 1 technology"),
] + [(f"absol_emergency{i}", 1, "place it in the common play area. This card", "place it in the common play area and reveal another card from the agenda deck. This card")
     for i in range(1, 5)]


def clean(t, prefix):
    if t is None or t.strip() in ("", "None"):
        return ""
    t = t.strip()
    if prefix and t.lower().startswith(prefix.lower()):
        t = t[len(prefix):].strip()
    for a, b in FIXES:
        t = t.replace(a, b)
    t = re.sub(r"[ \t]+", " ", t)
    t = re.sub(r" *\n *", "\n", t)
    return t


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


cards = {a["alias"]: a for a in json.load(open(os.path.join(ROOT, "data/agendas/absol.json")))}
deck = [d for d in json.load(open(os.path.join(ROOT, "data/decks/absol.json"))) if d["alias"] == "agendas_absol"][0]["cardIDs"]
lines = ["// Generated by tools/gen_absol_text.py from the AsyncTI4 data set (data/agendas/absol.json) - Absol's Agendas, the",
         "// cards' own wording. Do not edit by hand."]
for aid in deck:
    a = cards[aid]
    fa = (a.get("target") or "").lower().startswith("for/against")
    t1 = clean(a.get("text1"), "For:" if fa else "")
    t2 = clean(a.get("text2"), "Against:" if fa else "")
    if aid == "absol_covert":  # its "target" holds the first paragraph of the card
        t1 = clean(a.get("target"), "") + "\n" + t1
    for k, side, o, n in V4:
        if k != aid:
            continue
        o, n = o.replace("\\n", "\n"), n.replace("\\n", "\n")
        if side == 1 and o in t1:
            t1 = t1.replace(o, n)
        elif side == 2 and o in t2:
            t2 = t2.replace(o, n)
        elif n not in (t1 if side == 1 else t2):
            print("V4 fix not applied:", k, side, o[:50])
    lines.append(f"    {{{cstr(aid)}, {cstr(t1)}, {cstr(t2)}, true}},")
open(OUT, "w").write("\n".join(lines) + "\n")
print(f"wrote {OUT}: {len(deck)} agendas")
