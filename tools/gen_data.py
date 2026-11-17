#!/usr/bin/env python3
"""Turn the AsyncTI4 data set into compact C++ tables (src/engine/gen/gamedata.inc).

Source: https://github.com/AsyncTI4/TI4_map_generator_bot  (src/main/resources),
released into the public domain (Unlicense). Only base game + Prophecy of Kings
content is imported, and only numbers/names/structure: rules and card text are
written separately, in our own words, inside the engine.

Usage:  python3 tools/gen_data.py <path to .../src/main/resources>
"""
import json
import os
import re
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    "~/ext/async/src/main/resources")
OUT = "src/engine/gen/gamedata.inc"
SOURCES = ("base", "pok")


def load(rel):
    with open(os.path.join(ROOT, rel)) as f:
        return json.load(f)


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"' if s is not None else "nullptr"


TRAIT = {"CULTURAL": "Trait::Cultural", "HAZARDOUS": "Trait::Hazardous",
         "INDUSTRIAL": "Trait::Industrial"}
COLOR = {"PROPULSION": "TechColor::Blue", "BIOTIC": "TechColor::Green",
         "WARFARE": "TechColor::Red", "CYBERNETIC": "TechColor::Yellow"}
REQ_LETTER = {"B": 0, "G": 1, "R": 2, "Y": 3}
WH = {"ALPHA": 1, "BETA": 2, "GAMMA": 4, "DELTA": 8, "EPSILON": 16}
UNIT_TYPE = {"carrier": "Carrier", "cruiser": "Cruiser", "destroyer": "Destroyer",
             "dreadnought": "Dreadnought", "fighter": "Fighter", "flagship": "Flagship",
             "warsun": "WarSun", "infantry": "Infantry", "mech": "Mech", "pds": "PDS",
             "spacedock": "SpaceDock", "lady": "Flagship2", "celagrom": "Flagship2"}
FLEET_CODE = {"cv": "Carrier", "ca": "Cruiser", "cr": "Cruiser", "dd": "Destroyer",
              "dn": "Dreadnought", "ff": "Fighter", "fs": "Flagship", "ws": "WarSun",
              "inf": "Infantry", "gf": "Infantry", "mech": "Mech", "mf": "Mech",
              "pds": "PDS", "pd": "PDS", "sd": "SpaceDock"}
# The physical base game versions of cards that later got "Codex" reprints.
TECH_ALIAS = {"md": "md_base", "x89": "x89_base"}

out = []
emit = out.append

# ---------------------------------------------------------------- planets & systems
system_ids = [f"{i:02d}" for i in range(1, 81)] + ["82b"]
hyper_ids = [f"{n}{s}" for n in range(83, 92) for s in "ab"]

planets = []          # (key, name, res, inf, trait, skip, legendary, home, tile)
planet_index = {}
systems = []
for sid in system_ids:
    d = load(f"systems/{sid}.json")
    if d.get("source") not in SOURCES:
        continue
    pidx = []
    for pk in d.get("planets", []):
        p = load(f"planets/{pk}.json")
        skips = p.get("techSpecialties") or []
        planet_index[pk] = len(planets)
        pidx.append(len(planets))
        planets.append(dict(
            key=pk, name=p["name"], res=p["resources"], inf=p["influence"],
            trait=TRAIT.get(p.get("planetType") or "", "Trait::None"),
            skip=COLOR[skips[0]] if skips else "TechColor::None",
            legendary=bool(p.get("legendaryAbilityName")),
            home=(p.get("planetType") == "FACTION"), tile=sid))
    an = []
    if d.get("isAsteroidField"): an.append("kAsteroid")
    if d.get("isNebula"): an.append("kNebula")
    if d.get("isSupernova"): an.append("kSupernova")
    if d.get("isGravityRift"): an.append("kRift")
    wh = sum(WH[w] for w in d.get("wormholes", []))
    back = {"blue": "Back::Blue", "red": "Back::Red", "green": "Back::Home",
            "black": "Back::Special"}.get(d.get("tileBack"), "Back::Special")
    name = d["name"].split(" - ")[0] if back == "Back::Home" else d["name"]
    if sid == "82b":
        name = "Mallice"
    systems.append(dict(id=sid, name=name, back=back, wh=wh, an="|".join(an) or "0",
                        planets=pidx, src=d["source"], lanes=[0] * 6))

# The Mirage planet has no tile: the Mirage frontier card puts it into a system.
_m = load("planets/mirage.json")
planet_index["mirage"] = len(planets)
planets.append(dict(key="mirage", name=_m["name"], res=_m["resources"], inf=_m["influence"],
                    trait=TRAIT.get(_m.get("planetType") or "", "Trait::None"), skip="TechColor::None",
                    legendary=True, home=False, tile=None))

# hyperlanes: 6x6 edge connection matrix, rotation 0 (edges clockwise from north)
props = {}
for line in open(os.path.join(ROOT, "data/hyperlanes.properties")):
    m = re.match(r"^(\d+[ab])=(.*)$", line.strip())
    if m:
        props[m.group(1)] = m.group(2)
for hid in hyper_ids:
    rows = [list(map(int, r.split(","))) for r in props[hid].split(";")]
    lanes = [sum(1 << j for j in range(6) if rows[i][j]) for i in range(6)]
    systems.append(dict(id=hid, name="Hyperlane", back="Back::Hyperlane", wh=0, an="0",
                        planets=[], src="pok", lanes=lanes))
# Uncharted Space (0.31.0, a module): appended after everything above so that the indices of the base and
# Prophecy of Kings systems and planets - which saved games depend on - never move.
US_IDS = [f"d{n}" for n in range(100, 124)]
for sid in US_IDS:
    d = load(f"systems/{sid}.json")
    if d.get("source") != "uncharted_space":
        continue
    pidx = []
    for pk in d.get("planets", []) or []:
        p = load(f"planets/{pk}.json")
        skips = p.get("techSpecialties") or []
        planet_index[pk] = len(planets)
        pidx.append(len(planets))
        planets.append(dict(
            key=pk, name=p["name"], res=p["resources"], inf=p["influence"],
            trait=TRAIT.get(p.get("planetType") or "", "Trait::None"),
            skip=COLOR[skips[0]] if skips else "TechColor::None",
            legendary=bool(p.get("legendaryAbilityName")), home=False, tile=sid))
    an = []
    if d.get("isAsteroidField"): an.append("kAsteroid")
    if d.get("isNebula"): an.append("kNebula")
    if d.get("isSupernova"): an.append("kSupernova")
    if d.get("isGravityRift"): an.append("kRift")
    wh = sum(WH[w] for w in d.get("wormholes", []) or [])
    back = "Back::Red" if (an or (not pidx)) else "Back::Blue"
    name = d.get("name") or "/".join(planets[i]["name"] for i in pidx) or "Empty System"
    if name.startswith("DS Empty"):
        name = "Empty System"
    systems.append(dict(id=sid, name=name,
                        back=back, wh=wh, an="|".join(an) or "0", planets=pidx, src="uncharted_space", lanes=[0] * 6))
# Thunder's Edge (0.34.0, a module): every system and planet of the expansion, appended after Uncharted
# Space so no index moves - also the ones later parts use (faction homes, the Fracture, the planet Thunder's
# Edge). Only the blue and red ones go into the tile pools, and only with the module.
TE_IDS = ([str(n) for n in range(97, 112)] + [str(n) for n in range(113, 118)] + ["112"] +
          ["92", "93", "94", "95", "96a", "96b", "118"] + ["fracture%d" % k for k in range(1, 8)] + ["silver_flame"])
TE_TRAITS = {"CULTURAL": "Trait::Cultural", "HAZARDOUS": "Trait::Hazardous", "INDUSTRIAL": "Trait::Industrial"}


def te_planet(pk, tile):
    p = load(f"planets/{pk}.json")
    types = p.get("planetTypes") or ([p["planetType"]] if p.get("planetType") else [])
    traits = [TE_TRAITS[t] for t in types if t in TE_TRAITS]
    skips = [COLOR[s] for s in (p.get("techSpecialties") or []) if s in COLOR]
    flags = (1 if "SPACESTATION" in types else 0) | (2 if "LIGHTNING" in types else 0)
    planet_index[pk] = len(planets)
    planets.append(dict(
        key=pk, name=p["name"], res=p["resources"], inf=p["influence"],
        trait=traits[0] if traits else "Trait::None", trait2=traits[1] if len(traits) > 1 else "Trait::None",
        skip=skips[0] if skips else "TechColor::None", skip2=skips[1] if len(skips) > 1 else "TechColor::None",
        flags=flags, legendary=bool(p.get("legendaryAbilityName")), home="FACTION" in types, tile=tile))
    return planet_index[pk]


for sid in TE_IDS:
    d = load(f"systems/{sid}.json")
    pidx = [te_planet(pk, sid) for pk in (d.get("planets") or [])]
    an = []
    if d.get("isAsteroidField"): an.append("kAsteroid")
    if d.get("isNebula"): an.append("kNebula")
    if d.get("isSupernova"): an.append("kSupernova")
    if d.get("isGravityRift"): an.append("kRift")
    if d.get("isScar"): an.append("kScar")
    wh = sum(WH[w] for w in d.get("wormholes", []) or [])
    back = {"blue": "Back::Blue", "red": "Back::Red", "green": "Back::Home"}.get(d.get("tileBack"), "Back::Special")
    name = d.get("name") or "/".join(planets[i]["name"] for i in pidx)
    if back == "Back::Home":
        name = name.split(" - ")[0]
    systems.append(dict(id=sid, name=name, back=back, wh=wh, an="|".join(an) or "0", planets=pidx,
                        src="thunders_edge", lanes=[0] * 6))
# planets without a tile: Thunder's Edge itself (placed when the expedition is complete) and Avernus
for pk in ("thundersedge", "avernus"):
    te_planet(pk, None)
# Discordant Stars (0.37.0, a module): the 34 factions' home systems and planets, after Thunder's Edge's
DS_IDS = [f"d{n:02d}" for n in range(1, 35)]
for sid in DS_IDS:
    d = load(f"systems/{sid}.json")
    pidx = [te_planet(pk, sid) for pk in (d.get("planets") or [])]
    wh = sum(WH[w] for w in d.get("wormholes", []) or [])
    name = (d.get("name") or "/".join(planets[i]["name"] for i in pidx)).split(" - ")[0]
    systems.append(dict(id=sid, name=name, back="Back::Home", wh=wh, an="0", planets=pidx, src="ds", lanes=[0] * 6))
te_planet("ghoti", None)  # the Ghoti's home planet card: their home system has no planet
# 0.43.0 (owner, RULES-CHECK D44): tiles that replace a system in play - the Muaat supernova (Nova Seed: Adjudicator
# Ba'al) and the Zelian asteroid field (Zelian R: Cataclysm). Never dealt (Back::Special); appended last so every
# older index stays the same.
for sid, src, nm in (("81", "pok", None), ("d36", "ds", "Zelian Asteroids")):
    d = load(f"systems/{sid}.json")
    systems.append(dict(id=sid, name=nm or d.get("name"), back="Back::Special", wh=0,
                        an="kSupernova" if d.get("isSupernova") else "kAsteroid", planets=[], src=src, lanes=[0] * 6))
SRC_ENUM = {"base": "Source::Base", "pok": "Source::PoK", "uncharted_space": "Source::Uncharted",
            "thunders_edge": "Source::ThundersEdge", "ds": "Source::Discordant"}
system_index = {s["id"]: i for i, s in enumerate(systems)}

emit("// clang-format off")
emit("// GENERATED by tools/gen_data.py from the AsyncTI4 data set (public domain).")
emit("// Base game + Prophecy of Kings. Do not edit by hand.")
emit("")
emit(f"const PlanetDef kPlanets[{len(planets)}] = {{")
for p in planets:
    emit(f"  {{{cstr(p['key'])}, {cstr(p['name'])}, {p['res']}, {p['inf']}, {p['trait']}, {p['skip']}, "
         f"{'true' if p['legendary'] else 'false'}, {'true' if p['home'] else 'false'}, "
         f"{system_index[p['tile']] if p['tile'] else -1}, {p.get('trait2', 'Trait::None')}, "
         f"{p.get('skip2', 'TechColor::None')}, {p.get('flags', 0)}}},")
emit("};")
emit(f"const SystemDef kSystems[{len(systems)}] = {{")
for s in systems:
    pl = s["planets"] + [-1] * (3 - len(s["planets"]))
    emit(f"  {{{cstr(s['id'])}, {cstr(s['name'])}, {s['back']}, {s['wh']}, {s['an']}, "
         f"{SRC_ENUM.get(s['src'], 'Source::PoK')}, {len(s['planets'])}, "
         f"{{{pl[0]}, {pl[1]}, {pl[2]}}}, {{{', '.join(str(x) for x in s['lanes'])}}}}},")
emit("};")

# ---------------------------------------------------------------- technologies
techs = []
tech_index = {}
for t in load("data/technologies/pok.json"):
    if t.get("source") not in SOURCES:
        continue
    if t["alias"].startswith("nekroc4"):
        continue
    types = t.get("types") or []
    ttype = types[0] if types else "NONE"
    req = [0, 0, 0, 0]
    for ch in (t.get("requirements") or ""):
        req[REQ_LETTER[ch]] += 1
    tech_index[t["alias"]] = len(techs)
    techs.append(dict(key=t["alias"], name=t["name"].replace(" Ω", ""),
                      color=COLOR.get(ttype, "TechColor::None"),
                      upgrade=(ttype == "UNITUPGRADE"), req=req,
                      faction=t.get("faction"), src=t["source"],
                      base=t.get("baseUpgrade")))
# The Council Keleres (0.21.0, owner: the Thunder's Edge version): Executive
# Order is appended after the base + PoK technologies so older indices stay put.
for t in load("data/technologies/te_techs.json"):
    if t["alias"] == "executiveorder":
        req = [0, 0, 0, 0]
        for ch in (t.get("requirements") or ""):
            req[REQ_LETTER[ch]] += 1
        tech_index[t["alias"]] = len(techs)
        techs.append(dict(key=t["alias"], name=t["name"], color=COLOR[t["types"][0]], upgrade=False, req=req,
                          faction="keleres", src="pok", base=None))
# Thunder's Edge factions (0.35.0, part 4): their technologies after Executive Order (the Obsidian's two can't be
# researched - they come with the flip)
TE_FACTIONS = ("bastion", "ralnel", "crimson", "deepwrought", "firmament", "obsidian")
for t in load("data/technologies/te_techs.json"):
    if t.get("faction") not in TE_FACTIONS:
        continue
    req = [0, 0, 0, 0]
    for ch in ("" if t.get("requirements") in (None, "null") else t["requirements"]):
        req[REQ_LETTER[ch]] += 1
    ttype = (t.get("types") or ["NONE"])[0]
    tech_index[t["alias"]] = len(techs)
    techs.append(dict(key=t["alias"], name=t["name"].replace(" (Firmament)", "").replace(" (Obsidian)", ""),
                      color=COLOR.get(ttype, "TechColor::None"), upgrade=(ttype == "UNITUPGRADE"), req=req,
                      faction=t["faction"], src="thunders_edge", base=t.get("baseUpgrade")))
# Discordant Stars (0.37.0): the factions' technologies after Thunder's Edge's
for t in load("data/technologies/ds.json"):
    if t.get("source") != "ds":
        continue
    req = [0, 0, 0, 0]
    for ch in ("" if t.get("requirements") in (None, "null") else t["requirements"]):
        req[REQ_LETTER[ch]] += 1
    ttype = (t.get("types") or ["NONE"])[0]
    tech_index[t["alias"]] = len(techs)
    techs.append(dict(key=t["alias"], name=t["name"], color=COLOR.get(ttype, "TechColor::None"), upgrade=(ttype == "UNITUPGRADE"),
                      req=req, faction=t.get("faction"), src="ds", base=t.get("baseUpgrade")))
for alias, real in TECH_ALIAS.items():
    tech_index[alias] = tech_index[real]

# ---------------------------------------------------------------- factions
factions = []
for fn in ("data/factions/base.json", "data/factions/pok.json"):
    for f in load(fn):
        factions.append(f)
# The Council Keleres: one faction; its home system is chosen at setup (The
# Tribunii: the Mentak, Xxcha or Argent home), so homeSystem = -1 here.
KELERES = {"alias": "keleres", "factionName": "The Council Keleres", "homeSystem": None, "commodities": 2,
           "fleet": [("Carrier", 2, -1), ("Cruiser", 1, -1), ("Fighter", 2, -1), ("Infantry", 2, 0), ("SpaceDock", 1, 0)],
           "startingTech": [], "factionTech": ["executiveorder", "asn"],
           "abilities": ["the_tribuni", "council_patronage", "laws_order"],
           "leaders": ["keleresagent", "kelerescommander", "keleresheroharka", "keleresheroodlynn", "keleresherokuuasi"],
           "promissoryNotes": ["rider"], "units": ["keleres_flagship", "keleres_mech"], "source": "pok"}
factions.append(KELERES)
faction_index = {f["alias"]: i for i, f in enumerate(factions)}
for f in load("data/factions/te_factions.json"):  # Thunder's Edge (0.35.0): after the Keleres
    if f["alias"] in TE_FACTIONS:
        f = dict(f)
        if f["alias"] == "crimson":
            f["homeSystem"] = "118"  # Ahk Creuxx, beside the board; the Sorrow (94) takes the home's place
        factions.append(f)
faction_index = {f["alias"]: i for i, f in enumerate(factions)}
DS_FACTIONS = []
for f in load("data/factions/ds.json"):  # Discordant Stars (0.37.0): after Thunder's Edge's
    if f.get("source") == "ds":
        if f["alias"] == "augers":  # (0.39.0) the Discordant Stars reference spells it "Augurs"
            f = dict(f, factionName="Augurs of Ilyxum")
        factions.append(f)
        DS_FACTIONS.append(f["alias"])
faction_index = {f["alias"]: i for i, f in enumerate(factions)}
for alias in ("keleresm", "keleresx", "keleresa"):
    faction_index[alias] = faction_index["keleres"]

# ---------------------------------------------------------------- units
units = []
for fn in ("data/units/baseUnits.json", "data/units/pok.json"):
    for u in load(fn):
        if u.get("source") not in SOURCES or u.get("baseType") not in UNIT_TYPE:
            continue
        if u["id"] in ("nowarsun", "cavalry1", "cavalry2", "naaz_mech_space", "celagrom"):  # (celagrom: a placeholder type)
            continue
        units.append(u)
for u in load("data/units/keleres.json"):
    if u["id"] in ("keleres_flagship", "keleres_mech"):
        units.append(u)
for u in load("data/units/te_units.json"):  # Thunder's Edge factions (0.35.0)
    if u.get("faction") in TE_FACTIONS and u.get("baseType") in UNIT_TYPE:
        units.append(u)
LATE_UNITS = ("ghemina_flagship_lady", "celdauri_celagrom")  # (0.52.0 / 0.52.1: see below)
for u in load("data/units/ds.json"):  # Discordant Stars (0.37.0)
    # (0.41.0: the Ghemina's Combat Transport carriers are labelled "base" in the data)
    ds = u.get("source") == "ds" or u["id"] in ("ghemina_carrier", "ghemina_carrier2")
    if ds and u.get("faction") in DS_FACTIONS and u.get("baseType") in UNIT_TYPE:
        # (breakthroughs' units - the Kortali tribune, the Mirveda's Eye of the Curator, the Roh'Dhna Omni-Forgeworld:
        # breakthroughs are off with Discordant Stars)
        if u["id"].startswith("tribune") or u["id"] in ("mirveda_fighter3", "rohdhna_warsun3") or u["id"] in LATE_UNITS:
            continue
        units.append(u)
# 0.43.0 (owner D1: breakthroughs with Discordant Stars too): the flip-card units, appended last so no index moves, and
# kept out of the faction unit lists (a breakthrough puts them in play - refreshUnits)
BT_UNITS = ("tribune3", "mirveda_fighter3", "rohdhna_warsun3")
bt_units = {u["id"]: u for u in load("data/units/ds.json") if u["id"] in BT_UNITS}
for uid in BT_UNITS:
    units.append(dict(bt_units[uid], requiredTechId=None, upgradesFromUnitId=None))
# 0.52.0 (owner D117): the Ghemina's Lady as her own unit (UnitType::Flagship2), appended last so no index moves, and kept
# out of the faction's unit list (refreshUnits gives it to the Ghemina in games started with fixes >= 16). 0.52.1: the
# Celdauri's Celagrom (Trade Protectorate) the same way (fixes >= 17).
for u in load("data/units/ds.json"):
    if u["id"] in LATE_UNITS:
        units.append(u)
unit_index = {u["id"]: i for i, u in enumerate(units)}


def prod(u):
    v = u.get("productionValue")
    if v is None:
        return 0, False
    if isinstance(v, str) and v.startswith("+"):
        return int(v[1:]), True
    return int(v), False


emit(f"UnitDef kUnits[{len(units)}] = {{")
for u in units:
    p, plus = prod(u)
    fac = faction_index.get(u.get("faction"), -1) if u.get("faction") else -1
    tech = tech_index.get(u.get("requiredTechId"), -1) if u.get("requiredTechId") else -1
    upf = unit_index.get(u.get("upgradesFromUnitId"), -1) if u.get("upgradesFromUnitId") else -1
    cost2 = int(round(float(u.get("cost", 0)) * 2))
    emit("  {" + ", ".join([
        cstr(u["id"]), cstr(u["name"]), f"UnitType::{UNIT_TYPE[u['baseType']]}", str(fac), str(tech), str(upf),
        str(cost2), str(u.get("moveValue", 0)), str(u.get("capacityValue", 0)),
        str(u.get("combatHitsOn", 0)), str(u.get("combatDieCount", 1 if u.get("combatHitsOn") else 0)),
        str(u.get("afbHitsOn", 0)), str(u.get("afbDieCount", 0)),
        str(u.get("bombardHitsOn", 0)), str(u.get("bombardDieCount", 0)),
        str(u.get("spaceCannonHitsOn", 0)), str(u.get("spaceCannonDieCount", 0)),
        "true" if u.get("deepSpaceCannon") else "false",
        "true" if u.get("sustainDamage") else "false",
        "true" if u.get("planetaryShield") else "false",
        "true" if u.get("disablesPlanetaryShield") and u["id"] != "letnev_flagship" else "false",
        str(p), "true" if plus else "false",
        "true" if u.get("isSpaceOnly") else "false"]) + "},")
emit("};")

emit(f"TechDef kTechs[{len(techs)}] = {{")
for t in techs:
    fac = faction_index.get(t["faction"], -1) if t["faction"] and t["key"] != "iihq" else -1
    emit(f"  {{{cstr(t['key'])}, {cstr(t['name'])}, {t['color']}, {'true' if t['upgrade'] else 'false'}, "
         f"{{{', '.join(str(x) for x in t['req'])}}}, {fac}, "
         f"{tech_index.get(t['base'], -1) if t['base'] else -1}, "
         f"{SRC_ENUM.get(t['src'], 'Source::PoK')}}},")
emit("};")


def parse_fleet(f):
    """'2 cv, 4 inf n, pds n' -> [(type, count, planet slot or -1)]"""
    homes = [planets[planet_index[p]]["name"].lower() for p in f["homePlanets"]]
    homes_key = [re.sub(r"[^a-z0-9]", "", h) for h in homes]
    result = []
    for tok in f["startingFleet"].split(","):
        tok = tok.strip()
        if not tok:
            continue
        m = re.match(r"^(\d+)?\s*([a-z]+)\s*(\S+)?$", tok)
        if not m:
            raise SystemExit(f"cannot parse fleet token {tok!r} for {f['alias']}")
        n = int(m.group(1) or 1)
        utype = FLEET_CODE[m.group(2)]
        hint = (m.group(3) or "").lower()
        slot = -1
        if utype in ("Infantry", "Mech", "PDS", "SpaceDock"):
            slot = 0
            if hint:
                hk = re.sub(r"[^a-z0-9]", "", hint)
                for i, h in enumerate(homes_key):
                    if h.startswith(hk) or hk.startswith(h) or h.replace("archon", "a") .startswith(hk):
                        slot = i
                        break
                else:
                    # e.g. 'at' = Archon Tau, 'ar' = Archon Ren
                    for i, h in enumerate(homes):
                        initials = "".join(w[0] for w in h.split())
                        if initials == hk:
                            slot = i
                            break
        if slot >= 0:
            # convert to the planet's position in the system tile's own list
            home_sys = f["homeSystem"] if f["alias"] != "ghost" else "51"
            sys_planets = load(f"systems/{home_sys}.json").get("planets", []) or []
            slot = sys_planets.index(f["homePlanets"][slot]) if f["homePlanets"][slot] in sys_planets else -1  # (Ghoti: in space)
        result.append((utype, n, slot))
    return result


emit(f"const FactionDef kFactions[{len(factions)}] = {{")
for f in factions:
    home = f["homeSystem"]
    if f["alias"] == "ghost":
        home = "51"  # the Creuss home system itself (tile 17, the gate, sits on the board)
    fleet = f["fleet"] if "fleet" in f else parse_fleet(f)
    fleet_s = ", ".join(f"{{UnitType::{t}, {n}, {s}}}" for t, n, s in fleet)
    st = [tech_index[x] for x in (f.get("startingTech") or [])]
    ft = [tech_index[x] for x in (f.get("factionTech") or []) if x in tech_index]
    st += [-1] * (4 - len(st))
    ft += [-1] * (2 - len(ft))
    funits = [unit_index[u] for u in f.get("units", []) if u in unit_index and units[unit_index[u]].get("faction") and u not in BT_UNITS
              and u not in LATE_UNITS]
    funits += [-1] * (6 - len(funits))
    emit(f"  {{{cstr(f['alias'])}, {cstr(f['factionName'])}, {system_index[home] if home else -1}, {f.get('commodities', 0)}, "
         f"{len(fleet)}, {{{fleet_s}}}, {{{', '.join(map(str, st))}}}, {{{', '.join(map(str, ft))}}}, "
         f"{{{', '.join(map(str, funits[:6]))}}}, "
         f"{SRC_ENUM.get(f['source'], 'Source::PoK')}}},")
emit("};")

# Discordant Stars (0.37.0): factions that choose starting technologies (count, options; none listed = special)
emit("const StartChoiceDef kStartChoices[] = {")
nchoice = 0
for fi, f in enumerate(factions):
    if f.get("source") != "ds" or not f.get("startingTechAmount"):
        continue
    opts = [tech_index[t] for t in (f.get("startingTechOptions") or []) if t in tech_index][:8]
    opts += [-1] * (8 - len(opts))
    emit(f"  {{{fi}, {f['startingTechAmount']}, {{{', '.join(map(str, opts))}}}}},")
    nchoice += 1
emit("};")
emit(f"const int kStartChoiceCount = {nchoice};")

# ---------------------------------------------------------------- faction names
# Only names (abilities, leaders, promissory notes) for the faction sheet;
# what they do is written in our own words when each one is implemented.
abil = {}
for fn in ("data/abilities/base.json", "data/abilities/pok.json"):
    for a in load(fn):
        abil[a["id"]] = a["name"]
for a in load("data/abilities/other.json"):
    if a.get("faction") == "keleres":
        abil[a["id"]] = "The Tribunii" if a["id"] == "the_tribuni" else a["name"]
leaders = {l["id"]: l for l in load("data/leaders/pok.json")}
notes = {n["alias"]: n["name"] for n in load("data/promissory_notes/promissory_notes.json")}
# Thunder's Edge factions (0.35.0)
for a in load("data/abilities/te_abilities.json"):
    if a.get("faction") in TE_FACTIONS:
        abil[a["id"]] = a["name"]
for l in load("data/leaders/te_leaders.json"):
    if any(l["id"].startswith(k) for k in TE_FACTIONS):
        leaders[l["id"]] = l
for n in load("data/promissory_notes/thunders_edge.json"):
    if n.get("faction") in TE_FACTIONS or n["alias"] in ("raisethestandard", "shareknowledge", "blackops", "malevolency", "sever", "nanolink"):
        notes[n["alias"]] = n["name"]
# Discordant Stars (0.37.0)
for a in load("data/abilities/ds.json"):
    abil[a["id"]] = a["name"]
for l in load("data/leaders/ds.json"):
    leaders[l["id"]] = l
if "black_markets" in abil:  # owner (RULES-CHECK D67, 0.43.4): the newest printing calls it Shadow Markets
    abil["black_markets"] = "Shadow Markets"
if "nivynhero" in leaders:  # owner (RULES-CHECK D96): the Nivyn hero is Krill Drakkon (the data set spells it Kwill)
    leaders["nivynhero"] = dict(leaders["nivynhero"], name="Krill Drakkon")
if "lizhocommander" in leaders:  # the Li-Zho review (2026-11-16): the owner's DS Reference names him Dume Tathu (the data set: Dhume)
    leaders["lizhocommander"] = dict(leaders["lizhocommander"], name="Dume Tathu")
for n in load("data/promissory_notes/ds.json"):
    notes[n["alias"]] = n["name"]
LTYPE = {"agent": 0, "commander": 1, "hero": 2}
emit(f"const FactionInfo kFactionInfo[{len(factions)}] = {{")
for f in factions:
    ab = [abil[x] for x in f.get("abilities", []) if x in abil][:3]
    ab += [None] * (3 - len(ab))
    ls = []
    for x in f.get("leaders", []):
        l = leaders.get(x) or leaders.get(x.replace("-te", ""))
        if l:
            ls.append((l["name"], LTYPE.get(l["type"], 0)))
    ls.sort(key=lambda t: t[1])
    ls = ls[:5]
    names = [cstr(n) for n, _ in ls] + ["nullptr"] * (5 - len(ls))
    types = [str(t) for _, t in ls] + ["-1"] * (5 - len(ls))
    pn = [notes[x] for x in f.get("promissoryNotes", []) if x in notes][:2]
    pn += [None] * (2 - len(pn))
    emit(f"  {{{{{', '.join(cstr(a) for a in ab)}}}, {{{', '.join(names)}}}, {{{', '.join(types)}}}, "
         f"{{{', '.join(cstr(n) for n in pn)}}}}},")
emit("};")

# ---------------------------------------------------------------- leaders
# Every faction's leaders (Prophecy of Kings): agents, commander, hero. Only the
# names here; what they do is written in our own words in faction_text.cpp.
emit("LeaderDef kLeaders[] = {")
nleaders = 0
for fi, f in enumerate(factions):
    ls = []
    for x in f.get("leaders", []):
        key = x if x in leaders else x.replace("-te", "")
        l = leaders.get(key)
        if l:
            ls.append((key, l["name"], LTYPE.get(l["type"], 0)))
    ls.sort(key=lambda t: t[2])
    for key, name, t in ls:
        emit(f"  {{{cstr(key)}, {cstr(name)}, {fi}, {t}}},")
        nleaders += 1
emit("};")

# ---------------------------------------------------------------- objectives
objs = []
for o in load("data/public_objectives/public_objectives.json"):
    if o.get("source") in SOURCES:
        objs.append((o["alias"], o["name"], 1 if o.get("points") == 1 else 2, o.get("points", 1), o.get("phase"), o["source"]))
for o in load("data/secret_objectives/secret_objectives.json"):
    if o.get("source") in SOURCES:
        objs.append((o["alias"], o["name"], 3, 1, o.get("phase"), o["source"]))
PHASE = {"Status": "ObjPhase::Status", "Action": "ObjPhase::Action", "Agenda": "ObjPhase::Agenda"}
emit(f"ObjectiveDef kObjectives[{len(objs)}] = {{")
for key, name, kind, pts, ph, src in objs:
    emit(f"  {{{cstr(key)}, {cstr(name)}, {kind}, {pts}, {PHASE.get(ph, 'ObjPhase::Status')}, "
         f"{'Source::Base' if src == 'base' else 'Source::PoK'}}},")
emit("};")

# ---------------------------------------------------------------- action cards & agendas
# The decks used for a base + Prophecy of Kings game (120 action cards, 50
# agendas). Only names, counts and the kind of election are imported; what
# each card does is written in our own words in the engine.
decks = {d["alias"]: d for d in load("data/decks/pok.json")}
ac_all = {a["alias"]: a for a in load("data/action_cards/action_cards.json")}
ac_order = []
ac_count = {}
ac_first = {}
for cid in decks["action_cards_pok"]["cardIDs"]:
    a = ac_all[cid]
    if a["name"] not in ac_count:
        ac_order.append(a["name"])
        ac_count[a["name"]] = 0
        ac_first[a["name"]] = a
    ac_count[a["name"]] += 1
# Uncharted Space (0.33.0, a module): its own 30 cards, appended after the base + PoK ones (saved games
# refer to cards by index)
ds_decks = {d["alias"]: d for d in load("data/decks/ds.json")}
us_cards = {a["alias"]: a for a in load("data/action_cards/uncharted_space.json")}
for cid in ds_decks["action_cards_ds"]["cardIDs"]:
    a = us_cards.get(cid)
    if not a:
        continue
    a = dict(a, source="uncharted_space")
    if a["name"] not in ac_count:
        ac_order.append(a["name"])
        ac_count[a["name"]] = 0
        ac_first[a["name"]] = a
    ac_count[a["name"]] += 1
# Thunder's Edge (0.34.0, a module): its 20 cards (14 names), appended after Uncharted Space's
te_decks = {d["alias"]: d for d in load("data/decks/thundersedge.json")}
te_cards = {a["alias"]: a for a in load("data/action_cards/te_actioncards.json")}
for cid in te_decks["action_cards_te"]["cardIDs"]:
    a = te_cards.get(cid)
    if not a:
        continue
    a = dict(a, source="thunders_edge")
    if a["name"] not in ac_count:
        ac_order.append(a["name"])
        ac_count[a["name"]] = 0
        ac_first[a["name"]] = a
    ac_count[a["name"]] += 1
emit("// Action cards: key, name, copies in the deck, source")
emit(f"const ActionCardDef kActionCards[{len(ac_order)}] = {{")
for n in ac_order:
    a = ac_first[n]
    key = a["alias"]
    if ac_count[n] > 1 and key.endswith("1"):
        key = key[:-1]
    src = {"base": "Source::Base", "uncharted_space": "Source::Uncharted", "thunders_edge": "Source::ThundersEdge"}.get(a["source"], "Source::PoK")
    emit(f"  {{{cstr(key)}, {cstr(n)}, {ac_count[n]}, {src}}},")
emit("};")

ag_all = {}
for fn in ("data/agendas/base.json", "data/agendas/pok.json"):
    for a in load(fn):
        ag_all[a["alias"]] = a
def elect_kind(a):
    t = (a.get("target") or "").lower()
    if a["alias"] == "covert": return "Elect::Covert"
    if t.startswith("for/against") or "for:" in (a.get("text1") or "").lower(): return "Elect::ForAgainst"
    if "non-home planet" in t: return "Elect::NonHomePlanet"
    if "scored secret" in t: return "Elect::ScoredSecret"
    if "strategy card" in t: return "Elect::StrategyCard"
    if "elect law" in t: return "Elect::Law"
    if "elect planet" in t: return "Elect::Planet"
    if "elect player" in t: return "Elect::Player"
    raise SystemExit("unknown agenda target: " + a["alias"] + " " + t)
emit("// Agendas: key, name, law?, what is voted on, source")
agendas = decks["agendas_pok"]["cardIDs"]
emit(f"const AgendaDef kAgendas[{len(agendas)}] = {{")
for aid in agendas:
    a = ag_all[aid]
    name = a["name"].replace(" (Base Game)", "")
    src = "Source::Base" if a["source"] == "base" else "Source::PoK"
    emit(f"  {{{cstr(aid)}, {cstr(name)}, {'true' if a['type'] == 'Law' else 'false'}, {elect_kind(a)}, {src}}},")
emit("};")

# ---------------------------------------------------------------- exploration & relics
# Exploration decks (PoK): key, name, deck, copies, kind. Only names, counts and
# structure; what each card does is written in our own words in the engine.
EXPLORE_TYPE = {"Cultural": "ExploreType::Cultural", "Hazardous": "ExploreType::Hazardous",
                "Industrial": "ExploreType::Industrial", "Frontier": "ExploreType::Frontier"}
EXPLORE_KIND = {"Instant": "ExploreKind::Instant", "Attach": "ExploreKind::Attach",
                "Fragment": "ExploreKind::Fragment", "Token": "ExploreKind::Token", "Chart": "ExploreKind::Chart"}
ex_order = []
ex_count = {}
ex_first = {}
for e in load("data/explores/explores.json"):
    if e.get("source") != "pok":
        continue
    k = (e["type"], e["name"])
    if k not in ex_count:
        ex_order.append(k)
        ex_count[k] = 0
        ex_first[k] = e
    ex_count[k] += 1
# Uncharted Space (0.33.0, a module): its exploration cards as the Discordant Stars deck has them (16 - War
# Forge Ruins and Arcane Shield Emitter are in no deck), appended so the PoK indices never move. Star Charts
# stay in front of the player like fragments ("Chart").
us_explores = {e["id"]: e for e in load("data/explores/uncharted_space.json")}
for cid in ds_decks["explores_DS"]["cardIDs"]:
    e = us_explores.get(cid)
    if not e:
        continue
    e = dict(e, source="uncharted_space")
    if e["name"] == "Star Chart":
        e["resolution"] = "Chart"
    k = (e["type"], e["name"])
    if k not in ex_count:
        ex_order.append(k)
        ex_count[k] = 0
        ex_first[k] = e
    ex_count[k] += 1
# 0.43.0 (owner, RULES-CHECK U1: "Add them"): the two Uncharted Space cards in no deck, appended after the others
for cid in ("warforgeruins", "arcaneshieldemitter"):
    e = dict(us_explores[cid], source="uncharted_space")
    k = (e["type"], e["name"])
    ex_order.append(k)
    ex_count[k] = 1
    ex_first[k] = e
emit("// Exploration cards: key, name, deck, copies, kind, source")
emit(f"const ExploreCardDef kExploreCards[{len(ex_order)}] = {{")
for k in ex_order:
    e = ex_first[k]
    key = re.sub(r"\d+$", "", e["id"])
    src = "Source::Uncharted" if e.get("source") == "uncharted_space" else "Source::PoK"
    emit(f"  {{{cstr(key)}, {cstr(e['name'])}, {EXPLORE_TYPE[e['type']]}, {ex_count[k]}, "
         f"{EXPLORE_KIND.get(e.get('resolution'), 'ExploreKind::Instant')}, {src}}},")
emit("};")
relics = [dict(r, src="Source::PoK") for r in load("data/relics/pok.json") if r.get("source") == "pok"]
# Uncharted Space relics (0.33.0): the 7 of its relic deck, appended
us_relics = {r["alias"]: r for r in load("data/relics/ds.json")}
for rid in ds_decks["relics_ds"]["cardIDs"]:
    if rid in us_relics:
        relics.append(dict(us_relics[rid], src="Source::Uncharted"))
# Thunder's Edge relics (0.34.0): the 10 its relic deck adds to Prophecy of Kings' (with the Codex IV three)
te_relic_defs = {r["alias"]: r for f in ("data/relics/te_relics.json", "data/relics/codexiv.json") for r in load(f)}
pok_keys = {r["alias"] for r in relics}
for rid in te_decks["relics_pok_te"]["cardIDs"]:
    if rid in te_relic_defs and rid not in pok_keys:
        relics.append(dict(te_relic_defs[rid], src="Source::ThundersEdge"))
# Codex II relics (0.60.0, owner 2026-11-17: "add those in for codex enabled and thunders edge enabled games"): Dynamis Core,
# JR-XS455-O, Nano-Forge - appended (the deck takes them only in games with the Codex or Thunder's Edge: codex2Relic)
for r in load("data/relics/codexii.json"):
    relics.append(dict(r, src="Source::PoK"))
emit("// Relics: key, name, source")
emit(f"const RelicDef kRelics[{len(relics)}] = {{")
for r in relics:
    emit(f"  {{{cstr(r['alias'])}, {cstr(r['name'])}, {r['src']}}},")
emit("};")

# ---------------------------------------------------------------- map layouts
layouts = load("data/map_templates/standardMiltyLayouts.json")
emit("// Map layouts. pos = ring*100 + index (index 1 = north, clockwise).")
emit("// kind: 0 = fixed tile (tile, rotation), 1 = home of player, 2 = slice tile (player, slot)")
all_rows = []
starts = []
for lay in sorted(layouts, key=lambda l: l["playerCount"]):
    starts.append((lay["playerCount"], len(all_rows), len(lay["templateTiles"])))
    for t in lay["templateTiles"]:
        pos = int(t["pos"])
        if t.get("staticTileId"):
            tid = t["staticTileId"]
            m = re.match(r"^(\d+[ab]?)(\d)?$", tid)
            base, rot = m.group(1), int(m.group(2) or 0)
            if base not in system_index:
                base = base[:2] if base[:2] in system_index else base
            all_rows.append(f"{{{pos}, 0, {system_index[base]}, {rot}}}")
        elif t.get("home"):
            all_rows.append(f"{{{pos}, 1, {t['playerNumber'] - 1}, 0}}")
        else:
            all_rows.append(f"{{{pos}, 2, {t['playerNumber'] - 1}, {t['miltyTileIndex']}}}")
emit(f"const LayoutCell kLayoutCells[{len(all_rows)}] = {{")
for r in all_rows:
    emit(f"  {r},")
emit("};")
emit(f"const LayoutDef kLayouts[{len(starts)}] = {{")
for pc, s, n in starts:
    emit(f"  {{{pc}, {s}, {n}}},")
emit("};")

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w") as f:
    f.write("\n".join(out) + "\n")
print(f"wrote {OUT}: {len(planets)} planets, {len(systems)} systems, {len(units)} units, "
      f"{len(techs)} techs, {len(factions)} factions, {len(objs)} objectives, {len(starts)} layouts, "
      f"{len(ac_order)} action cards, {len(agendas)} agendas, {len(ex_order)} exploration cards, {len(relics)} relics")
