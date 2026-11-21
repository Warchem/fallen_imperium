#!/usr/bin/env python3
"""0.69.0: builds src/audio/grim.inc from the sound drafts the owner kept (tools/sound_drafts/draft2-4.cpp):
the grim toolkit (helpers + mastering) and the recipes, copied function by function."""
import re, sys
root = sys.argv[1] if len(sys.argv) > 1 else "."
src = {k: open(f"{root}/tools/sound_drafts/{k}.cpp").read().split("\n") for k in ("draft2", "draft3", "draft4")}

def func(name, where):
    """the function `name` (with the comment lines just above it) from draft `where`"""
    lines = src[where]
    for i, l in enumerate(lines):
        if re.match(r"^(std::vector<[a-z0-9_]+>|void|float|struct) +" + re.escape(name) + r"\b", l):
            j = i
            while j > 0 and lines[j - 1].startswith("//") and not lines[j - 1].startswith("// ===") and not lines[j - 1].startswith("// ---"):
                j -= 1
            k = i
            if lines[i].rstrip().endswith("}") and lines[i].count("{") == lines[i].count("}"):
                return "\n".join(lines[j:i + 1])
            while not (lines[k].startswith("}")):
                k += 1
            return "\n".join(lines[j:k + 1])
    raise SystemExit(f"{name} not found in {where}")

def span(where, first, last):
    """the lines from the one starting with `first` up to the one starting with `last` (inclusive, to its closing brace)"""
    lines = src[where]
    a = next(i for i, l in enumerate(lines) if l.startswith(first))
    b = next(i for i, l in enumerate(lines) if i > a and l.startswith(last))
    while not lines[b].startswith("}"):
        b += 1
    return "\n".join(lines[a:b + 1])

out = []
out.append("""// The grim sounds (0.69.0, owner 2026-11-19: "Go ahead and add these") - included by synth.cpp inside its namespace.
// Built by tools/sound_drafts/make_grim_inc.py from the sound drafts he kept on the review page (tools/sound_drafts/draft2-4.cpp,
// rounds 2-4: "too cheery ... more gritty and grim"): the toolkit (resonant filters, grit, a dark hall, the mastering
// grim()) and the recipes - the short siren, the three landings and every faction's movement sound.
// Same maths rule as synth.cpp: only sin / cos / floor / frexp / ldexp (no expf / logf / powf).
""")
out.append("// ------------------------------------------------------------------ the toolkit (round 2)")
out.append("float fmodSafe(float x, float y) { return x - std::floor(x / y) * y; }  // (fmodf: kept off the maths library list)")
out.append(span("draft2", "struct Ph {", "std::vector<int16_t> grim("))
out.append("\n// ------------------------------------------------------------------ more of it (round 3)")
out.append(span("draft3", "// a deep, dark bell or toll", "std::vector<float> buzzer("))
out.append("\n// ================================================================== targeting a tile")
out.append(func("attackSirenShort", "draft3"))
for f in ("invadePods", "invadeBurn", "invadeBoots"):
    out.append(func(f, "draft2"))
out.append("\n// ================================================================== the factions on the move")
base = ["moveSol", "moveLetnev", "moveHacan", "moveJolNar", "moveSardakk", "moveXxcha", "moveArborec", "moveL1Z1X", "moveMentak",
        "moveNaalu", "moveNekro", "moveYssaril", "moveCreuss", "moveMuaat", "moveSaar", "moveWinnu", "moveYin", "moveEmpyrean",
        "moveMahact", "moveNaazRokha", "moveNomad", "moveTitans", "moveVuilraith", "moveKeleres"]
for f in base:
    out.append(func(f, "draft2"))
r3 = ["moveArgent3", "moveBastion", "moveRalNel", "moveDeepwrought", "moveCrimson", "moveFirmament", "moveObsidian", "moveAxis",
      "moveBentor", "moveKjalengard", "moveCeldauri", "moveCheiran", "moveCymiae", "moveDihMohn", "moveEdyn", "moveFlorzen",
      "moveFreeSystems", "moveGhemina", "moveGhoti", "moveGledge", "moveIlyxum", "moveKollecc", "moveKolume", "moveKortali",
      "moveKyro", "moveLanefir", "moveLizho", "moveLtokk", "moveMirveda", "moveMyko", "moveNivyn", "moveNokar", "moveOlradin",
      "moveRohdhna", "moveTnelis", "moveVaden", "moveVaylerian", "moveVeldyr", "moveRhodun", "moveZelian"]
for f in r3:
    out.append(func(f, "draft3"))
out.append(func("moveMortheus2", "draft4"))
text = "\n".join(out) + "\n"
text = text.replace("std::fmod(", "fmodSafe(")
open(f"{root}/src/audio/grim.inc", "w").write(text)
print(len(text.split("\n")), "lines")

# the table: kFactions order (src/engine/gen/gamedata.inc) -> recipe, loudness, hall (as on the review page)
table = [("arborec", "moveArborec", .11, .2), ("ghost", "moveCreuss", .1, .3), ("hacan", "moveHacan", .11, .2),
 ("jolnar", "moveJolNar", .1, .2), ("l1z1x", "moveL1Z1X", .11, .2), ("letnev", "moveLetnev", .11, .25), ("mentak", "moveMentak", .11, .2),
 ("muaat", "moveMuaat", .11, .2), ("naalu", "moveNaalu", .09, .3), ("nekro", "moveNekro", .1, .2), ("saar", "moveSaar", .11, .2),
 ("sardakk", "moveSardakk", .11, .2), ("sol", "moveSol", .11, .2), ("winnu", "moveWinnu", .1, .3), ("xxcha", "moveXxcha", .1, .3),
 ("yin", "moveYin", .1, .3), ("yssaril", "moveYssaril", .08, .3), ("argent", "moveArgent3", .11, .2), ("cabal", "moveVuilraith", .11, .25),
 ("empyrean", "moveEmpyrean", .09, .35), ("mahact", "moveMahact", .11, .25), ("nomad", "moveNomad", .1, .2), ("naaz", "moveNaazRokha", .11, .2),
 ("titans", "moveTitans", .12, .25), ("keleres", "moveKeleres", .11, .25), ("bastion", "moveBastion", .11, .22),
 ("deepwrought", "moveDeepwrought", .1, .35), ("firmament", "moveFirmament", .09, .3), ("obsidian", "moveObsidian", .11, .3),
 ("crimson", "moveCrimson", .11, .25), ("ralnel", "moveRalNel", .11, .2), ("augers", "moveIlyxum", .1, .35), ("axis", "moveAxis", .11, .22),
 ("bentor", "moveBentor", .11, .2), ("celdauri", "moveCeldauri", .11, .22), ("cheiran", "moveCheiran", .11, .2), ("cymiae", "moveCymiae", .1, .2),
 ("dihmohn", "moveDihMohn", .11, .22), ("edyn", "moveEdyn", .1, .35), ("florzen", "moveFlorzen", .11, .2), ("freesystems", "moveFreeSystems", .1, .22),
 ("ghemina", "moveGhemina", .11, .22), ("ghoti", "moveGhoti", .1, .35), ("gledge", "moveGledge", .11, .22), ("khrask", "moveLtokk", .11, .25),
 ("kjalengard", "moveKjalengard", .11, .22), ("kollecc", "moveKollecc", .09, .25), ("kolume", "moveKolume", .1, .3), ("kortali", "moveKortali", .11, .25),
 ("kyro", "moveKyro", .1, .22), ("lanefir", "moveLanefir", .1, .3), ("lizho", "moveLizho", .1, .25), ("mirveda", "moveMirveda", .1, .2),
 ("mortheus", "moveMortheus2", .1, .35), ("mykomentori", "moveMyko", .1, .25), ("nivyn", "moveNivyn", .1, .3), ("nokar", "moveNokar", .11, .2),
 ("olradin", "moveOlradin", .1, .25), ("rohdhna", "moveRohdhna", .11, .22), ("tnelis", "moveTnelis", .08, .2), ("vaden", "moveVaden", .1, .3),
 ("vaylerian", "moveVaylerian", .11, .22), ("veldyr", "moveVeldyr", .1, .25), ("zealots", "moveRhodun", .1, .25), ("zelian", "moveZelian", .11, .22)]
assert len(table) == 65
used = {f for (_, f, _, _) in table}
assert used == set(base + r3 + ["moveMortheus2"]), used ^ set(base + r3 + ["moveMortheus2"])
rows = ",\n".join(f'    {{"{k}", {f}, {l}f, {h}f}}' for (k, f, l, h) in table)
tail = f"""
// The movement sounds in kFactions order (the key is checked against the faction's in the tests: --audiotest)
struct MoveRecipe {{
    const char* key;
    std::vector<float> (*make)();
    float loud, hall;
}};
const MoveRecipe kMoveRecipes[65] = {{
{rows}}};
"""
open(f"{root}/src/audio/grim.inc", "a").write(tail)
print("table ok")
