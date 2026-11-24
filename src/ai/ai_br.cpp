// Computer players and the Blue Reverie factions' questions (0.83.0): Ab::Br* (rules_br.cpp). Called from aiFactionAnswer.
// Rules of thumb like the rest of the AI; the engine checks every answer. Anything not handled here falls back to
// aiFactionAnswer's default (yes / the first option).
//   Atokera Legacy - Modular Designs (the ability that matters most in the active system), Ordagka (the likely outcome),
//                    Mirrorshard II (the front: our biggest fleet near enemies), Magruda (the richest planet), Kapoko Vui (a
//                    fight the ground forces can't carry alone), Tri-Weave Shielding / Obelisk (yes)
//   Belkosea Allied States (0.84.0) - Classified Developments (aiBrComponentValue: the first superweapons early; Availyn,
//                    Mors, Glatison, Grom, Caled; each on the planet that suits it), the "at any time" cards in combat and on
//                    their turn (aiBrSwValue / aiBrFreeValue), Razalka Gris (only when it lets more of our ships reach the active
//                    system), Spike / Ascendancy (yes), Ciphus Gault (when the barrage is worth more), Uzean Wardog (trade goods),
//                    Trinity Stockpile (a planet whose units are worth far more to others)
#include <algorithm>
#include <cstring>

#include "ai/ai.h"
#include "ai/ai_internal.h"
#include "engine/query.h"

namespace ti {

namespace {
Answer brOne(int id) {
    Answer a;
    a.picks.push_back(id);
    return a;
}
bool brHas(const Decision& d, int id) {
    for (const Option& o : d.options)
        if (o.enabled && o.id == id) return true;
    return false;
}
template <class F>
int brBest(const Decision& d, F score) {  // the best option by score; -1 ("don't") when nothing scores above 0
    int best = -1;
    float bv = 0.f;
    for (const Option& o : d.options) {
        if (!o.enabled || o.id < 0) continue;
        const float v = score(o.id);
        if (v > bv) {
            bv = v;
            best = o.id;
        }
    }
    return best;
}
int enemyGround(const Game& g, int p, int planet) {
    int n = 0;
    for (int q = 0; q < kSides; ++q)
        if (q != p && (q < g.numPlayers || q == kNeutral)) n += groundForcesOn(g, q, planet);
    return n;
}

// ---- the Belkosea
bool isAdj(const Game& g, int a, int b) {
    if (a < 0 || b < 0) return false;
    for (int16_t x : g.systems[size_t(a)].adj)
        if (x == b) return true;
    return false;
}
int othersFightersIn(const Game& g, int p, int s) {
    int n = 0;
    for (int q = 0; q < kSides; ++q)
        if (q != p && (q < g.numPlayers || q == kNeutral)) n += g.systems[size_t(s)].space[size_t(q)].count(UnitType::Fighter);
    return n;
}
// undamaged units that can sustain, others' (weighted by cost) minus p's own, in s
float morsGain(const Game& g, int p, int s) {
    float v = 0.f;
    const SystemState& sy = g.systems[size_t(s)];
    for (int q = 0; q < kSides; ++q) {
        if (q >= g.numPlayers && q != kNeutral) continue;
        for (int t = 0; t < kUnitTypes; ++t) {
            int k = 0;
            if (sy.space[size_t(q)].n[t] > sy.space[size_t(q)].dmg[t] && unitSustainsAt(g, q, UnitType(t), s, -1)) k += sy.space[size_t(q)].n[t] - sy.space[size_t(q)].dmg[t];
            for (int16_t x : sy.planets) {
                const Force& f = g.planets[size_t(x)].units[size_t(q)];
                if (f.n[t] > f.dmg[t] && unitSustainsAt(g, q, UnitType(t), s, x)) k += f.n[t] - f.dmg[t];
            }
            const float w = float(k) * (0.5f + 0.25f * float(unitCost2(g, q < g.numPlayers ? q : p, UnitType(t))));
            v += q == p ? -1.3f * w : w;
        }
    }
    return v;
}
// how close s is to other players' ships (a front)
float frontOf(const Game& g, int p, int s) {
    float v = 0.f;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p) continue;
        v += 0.6f * float(g.systems[size_t(s)].space[size_t(q)].nonFighterShips());
        for (int16_t a : g.systems[size_t(s)].adj) v += 0.35f * float(g.systems[size_t(a)].space[size_t(q)].nonFighterShips());
    }
    return v;
}
int capRoom(const Game& g, int p, int s) {
    const Force& f = g.systems[size_t(s)].space[size_t(p)];
    return forceCapacity(g, p, f) - f.count(UnitType::Fighter) - f.groundForces();
}
}  // namespace

// The value of using superweapon card k now (combat: opp / sys, -1 none) - 0 = not worth it
float aiBrSwValue(const Game& g, int p, int k, int opp, int sys) {
    const int home = superweaponSystem(g, k);
    switch (k) {
        case kSwAvailyn: {  // fighters into a fight we are in, or where there is room for them
            if (spendable(g, p, false) < 2 || unitsAvailable(g, p, UnitType::Fighter) < 2) return 0.f;
            if (sys >= 0 && opp >= 0 && g.groundCombatPlanet < 0 && g.systems[size_t(sys)].space[size_t(p)].ships() > 0)
                return (capRoom(g, p, sys) >= 2 || fightersFree(g, p)) ? 1.4f : 0.f;
            if (spendable(g, p, false) < 5) return 0.f;
            for (size_t s = 0; s < g.systems.size(); ++s)
                if (g.systems[s].space[size_t(p)].ships() > 0 && capRoom(g, p, int(s)) >= 3 && frontOf(g, p, int(s)) > 0.5f) return 0.7f;
            return 0.f;
        }
        case kSwCaled: return sys >= 0 && opp >= 0 ? 2.2f : 0.f;
        case kSwGlatison: {  // our damaged units in the fight (they sustain again), or a few anywhere before a fight
            int here = 0, all = 0;
            for (size_t s = 0; s < g.systems.size(); ++s)
                for (int t = 0; t < kUnitTypes; ++t) {
                    all += g.systems[s].space[size_t(p)].dmg[t];
                    if (int(s) == sys) here += g.systems[s].space[size_t(p)].dmg[t];
                }
            for (const PlanetState& ps : g.planets)
                for (int t = 0; t < kUnitTypes; ++t) {
                    all += ps.units[size_t(p)].dmg[t];
                    if (ps.system == sys) here += ps.units[size_t(p)].dmg[t];
                }
            if (sys >= 0) return here > 0 ? 1.2f + 0.4f * float(here) : 0.f;
            return all >= 3 ? 0.6f : 0.f;
        }
        case kSwGrom: {
            if (home < 0) return 0.f;
            int best = 0;
            if (sys >= 0 && (sys == home || isAdj(g, home, sys))) best = othersFightersIn(g, p, sys) * 2;
            best = std::max(best, othersFightersIn(g, p, home));
            for (int16_t a : g.systems[size_t(home)].adj) best = std::max(best, othersFightersIn(g, p, a));
            return best >= 2 ? 0.4f + 0.3f * float(std::min(best, 8)) : 0.f;
        }
        case kSwMors: {
            if (home < 0) return 0.f;
            float best = 0.f;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                if (int(s) == home || isAdj(g, home, int(s))) continue;
                float v = morsGain(g, p, int(s));
                if (int(s) == sys) v *= 2.5f;                        // the fight we are in
                else if (frontOf(g, p, int(s)) <= 0.f) v *= 0.5f;    // (a fleet far from us matters less)
                best = std::max(best, v);
            }
            return best >= 3.f ? 0.4f + 0.15f * std::min(best, 12.f) : 0.f;
        }
        default: return 0.f;
    }
}

// Their own turn: options 660-668 (no action used)
float aiBrFreeValue(const Game& g, int p, int id) {
    const Player& pl = g.players[size_t(p)];
    if (id >= 660 && id < 660 + kSuperweaponCount) return aiBrSwValue(g, p, id - 660, -1, -1);
    if (id == 665) return pl.commodities + 2 <= commodityCap(g, pl) ? 0.8f : 0.f;  // S.O.L.G. Industries Group
    if (id == 666) {  // Synchrony Matrix: a planet worth readying, or an exhausted superweapon worth using
        for (int k = 0; k < kSuperweaponCount; ++k)
            if (((pl.swCards & pl.swExhausted) >> k) & 1) return 0.6f;
        for (int x : controlledPlanets(g, p))
            if (g.planets[size_t(x)].exhausted && planetResources(g, x) + planetInfluence(g, x) >= 4) return 0.55f;
        return 0.f;
    }
    if (id == 667) {  // Trinity Stockpile: costs a strategy token - only for a big blow
        const int home = pl.homeSystem;
        float best = 0.f;
        for (size_t x = 0; x < g.planets.size(); ++x) {
            const PlanetState& ps = g.planets[x];
            if (ps.destroyed) continue;
            bool ok = ps.superweapon >= 0 || ps.system == home || (home >= 0 && isAdj(g, home, ps.system));
            if (!ok) continue;
            float v = 0.f;
            for (int q = 0; q < kSides; ++q) {
                if (q >= g.numPlayers && q != kNeutral) continue;
                for (int t = 0; t < kUnitTypes; ++t) {
                    const float w = float(ps.units[size_t(q)].n[t]) * float(unitCost2(g, q < g.numPlayers ? q : p, UnitType(t))) * 0.5f;
                    v += q == p ? -1.5f * w : w;
                }
            }
            best = std::max(best, v);
        }
        return best >= 6.f ? 1.0f + 0.1f * best : 0.f;
    }
    if (id == 668) {  // Armageddon Project
        float best = 0.f;
        for (int k = 0; k < kSuperweaponCount; ++k)
            if ((pl.swArm >> k) & 1) best = std::max(best, aiBrSwValue(g, p, k, -1, -1));
        return best;
    }
    return 0.f;
}

// Classified Developments (component action)
bool aiBrComponentValue(const Game& g, int p, int ab, float& v) {
    if (Ab(ab) != Ab::BrClassified) return false;
    const int have = __builtin_popcount(g.players[size_t(p)].swCards);
    v = spendableMixed(g, p) < 5 ? 0.f : have == 0 ? 3.0f : have == 1 ? 2.3f : 1.4f;
    return true;
}

bool aiBrAnswer(const Game& g, const Decision& d, Answer& out) {
    const int p = d.player;
    if (p < 0 || p >= g.numPlayers || int(d.ability) < int(Ab::BrModularDesigns) || int(d.ability) > int(Ab::BrSortieTarget)) return false;
    const Ab ab = Ab(d.ability);
    const int sys = g.activeSystem;
    switch (ab) {
        case Ab::BrModularDesigns: {
            if (sys < 0) {
                out = brOne(-1);
                return true;
            }
            const SystemState& s = g.systems[size_t(sys)];
            int enemyShips = 0, enemyFighters = 0, enemyGf = 0;
            for (int q = 0; q < kSides; ++q) {
                if (q == p || (q >= g.numPlayers && q != kNeutral)) continue;
                enemyShips += s.space[size_t(q)].ships();
                enemyFighters += s.space[size_t(q)].count(UnitType::Fighter);
            }
            for (int16_t x : s.planets) enemyGf += enemyGround(g, p, x);
            const bool landing = s.space[size_t(p)].groundForces() > 0;
            const int res = spendable(g, p, false);
            out = brOne(brBest(d, [&](int id) {
                const int ability = id / 10000, t = (id / 100) % 100;
                const UnitType ut = UnitType(t);
                switch (ability) {
                    case 1:  // Sustain Damage: the dearest ship in a fight, or a ground force in an invasion
                        if (isShip(ut)) return enemyShips > 0 ? 2.f + 0.5f * float(unitCost2(g, p, ut)) : 0.f;
                        return enemyGf > 0 && landing ? 2.f : 0.f;
                    case 2: return enemyFighters >= 2 ? 0.8f * float(std::min(enemyFighters, 4)) : 0.f;  // Anti-Fighter Barrage 3
                    case 3: return enemyGf > 0 && landing ? 1.5f + 0.3f * float(std::min(enemyGf, 4)) : 0.f;  // Bombardment 3
                    case 4: return enemyShips > 0 ? 1.6f : 0.f;  // Space Cannon 3 (offense, right after this)
                    case 5:  // Production 3: little production here and the money to use it
                        return productionCapacity(g, p, sys) < 3 && res >= 3 && enemyShips == 0 ? 1.2f + 0.2f * float(std::min(res, 6)) : 0.f;
                    default: return 0.f;
                }
            }));
            return true;
        }
        case Ab::BrOrdagka: {  // the outcome most likely to win (free: it only counts if it comes true)
            float conf = 0.f;
            const int o = g.currentAgenda >= 0 ? aiPredictOutcome(g, g.currentAgenda, &conf) : -1;
            if (brHas(d, o)) {
                out = brOne(o);
                return true;
            }
            for (const Option& op : d.options)
                if (op.enabled && op.id != kNoVote) {
                    out = brOne(op.id);
                    return true;
                }
            out = brOne(kNoVote);
            return true;
        }
        case Ab::BrMirrorshard: {  // the front: our ships next to other players' ships
            out = brOne(brBest(d, [&](int s) {
                if (s < 0 || s >= int(g.systems.size())) return 0.f;
                float v = 1.f + 0.2f * float(g.systems[size_t(s)].space[size_t(p)].nonFighterShips());
                for (int16_t a : g.systems[size_t(s)].adj)
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p && g.systems[size_t(a)].space[size_t(q)].nonFighterShips() > 0) v += 1.f;
                if (g.systems[size_t(s)].home == p) v += 0.5f;
                return v;
            }));
            return true;
        }
        case Ab::BrMagruda:  // the planet with the most resources (the options come sorted that way)
            out = brOne(brBest(d, [&](int x) { return x >= 0 ? 1.f + float(planetResources(g, x)) : 0.f; }));
            return true;
        case Ab::BrKapoko: {  // only when the ships tip a fight the ground forces can't carry alone (once a game)
            if (sys < 0) {
                out = brOne(0);
                return true;
            }
            const SystemState& s = g.systems[size_t(sys)];
            int mostDef = 0, open = 0;
            for (int16_t x : s.planets) {
                if (g.planets[size_t(x)].owner == p || isDMZ(g, x)) continue;
                mostDef = std::max(mostDef, enemyGround(g, p, x));
                ++open;
            }
            const int gf = s.space[size_t(p)].groundForces(), ships = s.space[size_t(p)].nonFighterShips();
            const bool late = g.round >= 5;
            out = brOne((mostDef > 0 && gf < 2 * mostDef + 1 && ships >= 2) || (gf == 0 && open > 0 && ships >= 1 && late) ? 1 : 0);
            return true;
        }
        // ---- the Belkosea
        case Ab::BrClassified: {  // the superweapon: Availyn (a second production) first, then Mors, Glatison, Grom, Caled
            static const int order[] = {kSwAvailyn, kSwMors, kSwGlatison, kSwGrom, kSwCaled};
            for (int k : order)
                if (brHas(d, k)) {
                    out = brOne(k);
                    return true;
                }
            out = brOne(-1);
            return true;
        }
        case Ab::BrClassifiedWhere: {
            const int k = d.value;
            out = brOne(brBest(d, [&](int x) {
                if (x < 0) return 0.f;
                const PlanetState& ps = g.planets[size_t(x)];
                const int s = ps.system;
                const bool home = g.systems[size_t(s)].home == p;
                const float worth = float(planetResources(g, x) + planetInfluence(g, x));
                const float front = frontOf(g, p, s);
                bool dock = false;
                for (int16_t y : g.systems[size_t(s)].planets) dock = dock || g.planets[size_t(y)].units[size_t(p)].count(UnitType::SpaceDock) > 0;
                switch (k) {
                    case kSwAvailyn: return 1.f + (home ? 0.f : 3.f) + (dock ? 0.f : 2.f) + 0.3f * worth + 0.5f * std::min(front, 3.f);
                    case kSwMors: return 1.f + 0.6f * worth + 1.0f * std::min(front, 4.f) + (home ? 1.5f : 0.f);
                    case kSwGlatison:
                    case kSwGrom: return 1.f + 1.5f * std::min(front, 5.f) + 0.2f * worth;
                    case kSwCaled: {
                        int lines = 0;
                        for (size_t t = 0; t < g.systems.size(); ++t)
                            if (caledLine(g, s, int(t)) && hexDistance(g.systems[size_t(s)].pos, g.systems[t].pos) <= 4) ++lines;
                        return 1.f + 0.4f * float(lines) + 0.5f * std::min(front, 3.f);
                    }
                    default: return 1.f;
                }
            }));
            return true;
        }
        case Ab::BrSuperweapon: {  // the combat question: the best card now (or fight on)
            const int opp = d.targetPlayer, csys = d.value;
            out = brOne(brBest(d, [&](int id) {
                if (id >= 660 && id < 660 + kSuperweaponCount) return aiBrSwValue(g, p, id - 660, opp, csys);
                const Player& pl = g.players[size_t(p)];
                if (id == 666) {  // Synchrony Matrix: an exhausted superweapon that would be worth using here
                    float v = 0.f;
                    for (int k = 0; k < kSuperweaponCount; ++k)
                        if (((pl.swCards & pl.swExhausted) >> k) & 1) v = std::max(v, aiBrSwValue(g, p, k, opp, csys) - 0.3f);
                    return v;
                }
                if (id == 667) {  // Trinity Stockpile on the planet fought over: when they have far more there than we do
                    const int x = g.groundCombatPlanet;
                    if (x < 0) return 0.f;
                    const int mine = g.planets[size_t(x)].units[size_t(p)].total(), theirs = enemyGround(g, p, x);
                    return theirs >= mine + 3 ? 1.0f : 0.f;
                }
                if (id == 668) {
                    float v = 0.f;
                    for (int k = 0; k < kSuperweaponCount; ++k)
                        if ((pl.swArm >> k) & 1) v = std::max(v, aiBrSwValue(g, p, k, opp, csys));
                    return v;
                }
                return 0.f;
            }));
            return true;
        }
        case Ab::BrAvailyn: {  // the fight we are in, else the most room near a front
            out = brOne(brBest(d, [&](int s) {
                if (s < 0) return 0.f;
                const bool fight = otherShipsIn(g, p, s);
                const int room = capRoom(g, p, s);
                if (room < 1 && !fightersFree(g, p)) return 0.05f;
                return 1.f + (fight ? 4.f : 0.f) + 0.4f * float(std::min(room, 3)) + 0.3f * frontOf(g, p, s);
            }));
            return true;
        }
        case Ab::BrGrom:
            out = brOne(brBest(d, [&](int s) { return s >= 0 ? 1.f + float(othersFightersIn(g, p, s)) : 0.f; }));
            return true;
        case Ab::BrMors:
            out = brOne(brBest(d, [&](int s) {
                if (s < 0) return 0.f;
                const float v = morsGain(g, p, s) * (s == g.activeSystem ? 2.f : 1.f) + 0.5f * frontOf(g, p, s);
                return v > 0.f ? v : 0.f;
            }));
            return true;
        case Ab::BrSynchrony:  // an exhausted superweapon card first, then the richest planet, then the rest
            out = brOne(brBest(d, [&](int c) {
                if (c < 0) return 0.f;
                if (c >= 100 && c < 100 + kSuperweaponCount) return 10.f + aiBrSwValue(g, p, c - 100, -1, g.activeSystem);
                if (c >= 1000 && c < 20000) return 2.f + float(planetResources(g, c - 1000) + planetInfluence(g, c - 1000)) * 0.5f;
                return 1.f;
            }));
            return true;
        case Ab::BrTrinity:
            out = brOne(brBest(d, [&](int x) {
                if (x < 0) return 0.f;
                const PlanetState& ps = g.planets[size_t(x)];
                float v = 0.f;
                for (int q = 0; q < kSides; ++q) {
                    if (q >= g.numPlayers && q != kNeutral) continue;
                    for (int t = 0; t < kUnitTypes; ++t) {
                        const float w = float(ps.units[size_t(q)].n[t]) * float(unitCost2(g, q < g.numPlayers ? q : p, UnitType(t))) * 0.5f;
                        v += q == p ? -1.5f * w : w;
                    }
                }
                return v >= 3.f ? v : 0.f;
            }));
            return true;
        case Ab::BrRazalka: {  // only when it lets more of our ships reach the active system
            const int active = d.targetPlayer, asys = d.value;
            if (active != p || asys < 0) {
                out = brOne(-1);
                return true;
            }
            auto reachers = [&](const Game& h) {
                float n = 0.f;
                for (size_t s = 0; s < h.systems.size(); ++s) {
                    if (int(s) == asys || h.systems[s].hasToken(p)) continue;
                    for (int t : kShipIdx) {
                        const int k = h.systems[s].space[size_t(p)].n[t];
                        if (!k || t == int(UnitType::Fighter)) continue;
                        if (shipReach(h, p, int(s), asys, UnitType(t)).ok) n += float(k) * (0.5f + 0.25f * float(unitCost2(h, p, UnitType(t))));
                    }
                }
                return n;
            };
            const float base = reachers(g);
            Game h = g;
            out = brOne(brBest(d, [&](int s) {
                if (s < 0) return 0.f;
                h.fx.razalkaSys = int16_t(s);
                const float v = reachers(h) - base;
                return v >= 1.f ? v : 0.f;
            }));
            return true;
        }
        case Ab::BrSpike: {  // our fighters with barrage against ships: worth it with a few fighters
            const int sys2 = d.value;
            out = brOne(sys2 >= 0 && g.systems[size_t(sys2)].space[size_t(p)].count(UnitType::Fighter) >= 3 ? 1 : 0);
            return true;
        }
        case Ab::BrAscendancy:
            out = brOne(1);
            return true;
        case Ab::BrCiphus: {  // the barrage's expected kills (on fighters only, unless Spike) against the combat rolls' hits
            const int opp = d.targetPlayer, sys2 = d.value;
            if (opp < 0 || sys2 < 0) {
                out = brOne(0);
                return true;
            }
            const Force& f = g.systems[size_t(sys2)].space[size_t(p)];
            const int enemyF = g.systems[size_t(sys2)].space[size_t(opp)].count(UnitType::Fighter);
            const bool spike = d.context.find("Spike") != std::string::npos;
            float combat = 0.f, afbAny = 0.f, afbF = 0.f;
            for (int t = 0; t < kUnitTypes; ++t) {
                if (!f.n[t] || !isShip(UnitType(t))) continue;
                const UnitDef& u = unitDef(g, p, UnitType(t));
                if (u.afb <= 0) continue;
                combat += float(f.n[t] * std::max(1, int(u.combatDice))) * float(11 - u.combat) / 10.f;
                const float a = float(f.n[t] * std::max(1, int(u.afbDice))) * float(11 - u.afb) / 10.f;
                if (spike && t == int(UnitType::Fighter)) afbAny += a;
                else afbF += a;
            }
            const float afb = afbAny + std::min(afbF, float(enemyF));
            out = brOne(afb > combat + 0.1f ? 1 : 0);
            return true;
        }
        case Ab::BrWardog: {  // trade goods are ours to spend: convert as many as we can
            int most = 0;
            for (const Option& o : d.options) most = std::max(most, o.id);
            out = brOne(most);
            return true;
        }
        case Ab::BrSortieTarget:
            out = brOne(brHas(d, int(UnitType::Infantry)) ? int(UnitType::Infantry) : int(UnitType::Fighter));
            return true;
        default: return false;  // Tri-Weave Shielding, Obelisk, Mercenary Sortie: yes / the first planet (aiFactionAnswer's default)
    }
}

}  // namespace ti
