// Blue Reverie (0.83.0, TacticBlue's homebrew - module kModBlueReverie): read-only rules helpers for its factions.
//   Atokera Legacy - Synthesis (readied planets: Planetary Shield + Production 0, +1 to every PRODUCTION value), Kohi (the
//                    flagship: the other units in its system copy the unit abilities of other players' units of their
//                    type there - the practical version, owner 2026-11-23), Obelisk (mech: other players can't exhaust its
//                    planet), Modular Designs (1 unit gains 1 unit ability with a value of 3 for the action: TacticalEffects
//                    mod*), Mirrorshard I (PRODUCTION 0, i.e. 1 with Synthesis).
//   Belkosea Allied States (0.84.0) - superweapons: PlanetState::superweapon (0-4, kSuperweapons), the cards in Player::swCards;
//                    Availyn (a nebula, PRODUCTION 3), Glatison (other players' units there lose SUSTAIN DAMAGE), Mors (PLANETARY
//                    SHIELD), Caled's line; Arrogance (structures: superweaponsOf).
//   Pharad'n Order (0.85.0) - Mark of Pharad'n: their captured infantry (tokens, LRR 17.3) are what infantry cost - unitsAvailable
//                    gives them as the infantry they can produce / place, productionCost leaves infantry out; Sedtra's move (the
//                    ground forces it carries) in moveValue; Reality-Field Impactor (TacticalEffects::rfiSys) in anomaliesFor; the
//                    Black Pyramid fights as a ground force (pyramidOn, with the Sleeping World's code).
#include <algorithm>
#include <cstring>

#include "engine/query.h"

namespace ti {

bool brAtokera(const Game& g, int p) {
    return (g.modules & kModBlueReverie) && p >= 0 && p < g.numPlayers && isFaction(g, p, "atokera");
}

// Synthesis: "Readied planets you control have PLANETARY SHIELD and PRODUCTION 0 as if they were units." (space stations
// aren't planets for it)
bool synthesisPlanet(const Game& g, int p, int planet) {
    if (!brAtokera(g, p) || planet < 0 || planet >= int(g.planets.size())) return false;
    const PlanetState& ps = g.planets[size_t(planet)];
    return ps.owner == p && !ps.exhausted && !ps.destroyed && !isStation(g, planet);
}

// Kohi (Atokera flagship): "Your other units in this system also have the unit and text abilities of other players' units of the
// same type in this system." The practical version (owner 2026-11-23): the unit abilities a type lacks - SUSTAIN DAMAGE,
// ANTI-FIGHTER BARRAGE, BOMBARDMENT, SPACE CANNON, PLANETARY SHIELD, PRODUCTION - from other players' units of that type in the
// system (space or planets), the best value on offer; what the type already has stays its own.
bool kohiIn(const Game& g, int p, int sys) {
    if (!(g.modules & kModBlueReverie) || p < 0 || p >= g.numPlayers || sys < 0 || sys >= int(g.systems.size())) return false;
    return flagshipAbility(g, p, "atokera") && flagshipIn(g, p, sys);
}
UnitDef kohiDef(const Game& g, int p, UnitType t, int sys) {
    UnitDef x = unitDef(g, p, t);
    if (isFlagship(t) || !kohiIn(g, p, sys)) return x;
    const SystemState& s = g.systems[size_t(sys)];
    const UnitDef own = x;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p) continue;
        bool there = s.space[size_t(q)].count(t) > 0;
        for (int16_t pl : s.planets) there = there || g.planets[size_t(pl)].units[size_t(q)].count(t) > 0;
        if (!there) continue;
        const UnitDef& o = unitDef(g, q, t);
        if (!own.sustain && o.sustain && unitSustains(g, q, t)) x.sustain = true;
        if (own.afb <= 0 && o.afb > 0 && (x.afb <= 0 || o.afb < x.afb || (o.afb == x.afb && o.afbDice > x.afbDice))) x.afb = o.afb, x.afbDice = o.afbDice;
        if (own.bombard <= 0 && o.bombard > 0 && (x.bombard <= 0 || o.bombard < x.bombard || (o.bombard == x.bombard && o.bombardDice > x.bombardDice)))
            x.bombard = o.bombard, x.bombardDice = o.bombardDice;
        if (own.cannon <= 0 && o.cannon > 0 && (x.cannon <= 0 || o.cannon < x.cannon || (o.cannon == x.cannon && o.cannonDice > x.cannonDice)))
            x.cannon = o.cannon, x.cannonDice = o.cannonDice, x.deepCannon = o.deepCannon;
        if (!own.shield && o.shield) x.shield = true;
        if (own.production <= 0 && !own.productionPlusResources && o.production > 0 && !o.productionPlusResources)
            x.production = std::max(x.production, o.production);  // (a space dock's "resources + X" is its planet's - not copied)
    }
    return x;
}

// Modular Designs (Atokera technology) - the unit chosen this action (TacticalEffects::mod*): does p's unit of type t at
// (sys, planet; -1 = space) carry the ability (1 Sustain Damage, 2 Anti-Fighter Barrage 3, 3 Bombardment 3, 4 Space Cannon 3,
// 5 Production 3)?
bool modularAt(const Game& g, int p, UnitType t, int sys, int planet, int ability) {
    const TacticalEffects& f = g.fx;
    return f.modOwner == p && f.modAbility == ability && f.modType == int(t) && f.modSys == sys && f.modPlanet == planet;
}

// Sustain Damage for a whole type (unitSustainsAt): Kohi's copy; Panoply Animus (Atokera breakthrough): "Your structures gain SUSTAIN DAMAGE"
bool brSustains(const Game& g, int p, UnitType t, int sys) {
    if (!(g.modules & kModBlueReverie) || p < 0 || p >= g.numPlayers) return false;
    if (isStructure(t) && breakthroughIs(g, p, "atokera")) return true;
    return kohiIn(g, p, sys) && !isFlagship(t) && kohiDef(g, p, t, sys).sustain;
}

// Modular Designs' Sustain Damage: 1 unit of a type without it - 1 while that unit is undamaged
int modularSustainLeft(const Game& g, int p, UnitType t, int sys, int planet) {
    if (!modularAt(g, p, t, sys, -2, 1) || sys < 0 || unitSustainsAt(g, p, t, sys, planet)) return 0;
    if (sustainBlockedAt(g, p, t, sys, planet)) return 0;
    const SystemState& s = g.systems[size_t(sys)];
    int dmg = s.space[size_t(p)].dmg[int(t)];  // used already: more of the type damaged than when it was chosen
    for (int16_t x : s.planets) dmg += g.planets[size_t(x)].units[size_t(p)].dmg[int(t)];
    if (dmg > g.fx.modDmg0) return 0;
    const Force& f = planet < 0 ? s.space[size_t(p)] : g.planets[size_t(planet)].units[size_t(p)];
    return f.n[int(t)] > f.dmg[int(t)] ? 1 : 0;
}

// Planetary Shield on a planet for its units' owner q, from the Blue Reverie: Synthesis (q's readied planet), Kohi (a copied
// shield on q's units there)
bool brPlanetShield(const Game& g, int q, int planet) {
    if (!(g.modules & kModBlueReverie) || q < 0 || q >= g.numPlayers || planet < 0 || planet >= int(g.planets.size())) return false;
    if (synthesisPlanet(g, q, planet)) return true;
    if (g.fx.triOwner == q && g.fx.triPlanet == planet) return true;  // Tri-Weave Shielding (Atokera note)
    if (morsShield(g, q, planet)) return true;                        // (0.84.0) Mors (Belkosea superweapon): PLANETARY SHIELD
    const PlanetState& ps = g.planets[size_t(planet)];
    if (kohiIn(g, q, ps.system))
        for (int t = 0; t < kUnitTypes; ++t)
            if (ps.units[size_t(q)].n[t] && kohiDef(g, q, UnitType(t), ps.system).shield) return true;
    return false;
}

// Obelisk (Atokera mech): "Other players cannot exhaust this planet." True when `by` can't exhaust it.
bool obeliskGuards(const Game& g, int planet, int by) {
    if (!(g.modules & kModBlueReverie) || planet < 0 || planet >= int(g.planets.size())) return false;
    const int o = g.planets[size_t(planet)].owner;
    if (o < 0 || o >= g.numPlayers || o == by || !mechAbilities(g)) return false;
    if (!g.planets[size_t(planet)].units[size_t(o)].count(UnitType::Mech)) return false;
    return !std::strcmp(unitDef(g, o, UnitType::Mech).key, "atokera_mech");
}

// The PRODUCTION value of p's units of type t at (sys, planet) - 0 = none. Synthesis's +1 for each unit with PRODUCTION
// (Mirrorshard I has PRODUCTION 0: 1 with it); Kohi's copied PRODUCTION. Without the Blue Reverie: the card's value.
int brUnitProduction(const Game& g, int p, UnitType t, int sys, const UnitDef& d) {
    if (!(g.modules & kModBlueReverie)) return d.production;
    int v = d.production;
    bool has = v > 0 || d.productionPlusResources || !std::strcmp(d.key, "atokera_cruiser");
    if (!has && sys >= 0 && kohiIn(g, p, sys)) {
        const UnitDef k = kohiDef(g, p, t, sys);
        if (k.production > 0) v = k.production, has = true;
    }
    if (has && brAtokera(g, p)) ++v;  // Synthesis
    return has ? v : 0;
}

// ================================================================== the Belkosea Allied States (0.84.0)
// The superweapon cards ("fake relics" in the AsyncTI4 data, relics/blue_reverie.json) - the cards' wording
const SuperweaponDef kSuperweapons[kSuperweaponCount] = {
    {"availyn", "Availyn", "This system is a nebula. At any time, you may exhaust this card to produce up to 3 fighters in a system that "
                           "contains your ships. PRODUCTION 3"},
    {"caled", "Caled", "At any time, you may exhaust this card to destroy 1 of your opponent's ships in the active system, if a line from "
                       "this system to that system would cross only parallel non-anomaly borders."},
    {"glatison", "Glatison", "At any time, you may exhaust this card to repair all of your damaged units. Other players' units in this system "
                             "lose SUSTAIN DAMAGE. ANTI-FIGHTER BARRAGE 4 (x2)."},
    {"grom", "Grom", "At any time, you may exhaust this card to resolve ANTI-FIGHTER BARRAGE 4 against each fighter in 1 adjacent system "
                     "(including your own)."},
    {"mors", "Mors", "At any time, you may exhaust this card to force each unit in 1 non-adjacent system to become damaged (including your "
                     "own). PLANETARY SHIELD"},
};

bool brBelkosea(const Game& g, int p) {
    return (g.modules & kModBlueReverie) && p >= 0 && p < g.numPlayers && isFaction(g, p, "belkosea");
}

int superweaponPlanet(const Game& g, int k) {
    if (!(g.modules & kModBlueReverie) || k < 0 || k >= kSuperweaponCount) return -1;
    for (size_t x = 0; x < g.planets.size(); ++x)
        if (g.planets[x].superweapon == k && !g.planets[x].destroyed) return int(x);
    return -1;
}
int superweaponSystem(const Game& g, int k) {
    const int x = superweaponPlanet(g, k);
    return x < 0 ? -1 : g.planets[size_t(x)].system;
}
int superweaponOwner(const Game& g, int k) {
    const int x = superweaponPlanet(g, k);
    return x < 0 ? -1 : g.planets[size_t(x)].owner;
}
int superweaponsOf(const Game& g, int p) {
    if (!(g.modules & kModBlueReverie) || p < 0) return 0;
    int n = 0;
    for (const PlanetState& ps : g.planets)
        if (ps.superweapon >= 0 && ps.owner == p && !ps.destroyed) ++n;
    return n;
}

// Armageddon Project (Belkosea monument, Monuments+): "This card has the text abilities of each superweapon card in a play area that has
// 1 of your control tokens" - its "this system" is the monument's: the holder with a token on card k whose monument is in sys
bool armageddonAt(const Game& g, int k, int sys, int* holder, int* planet) {
    if (!(g.modules & kModBlueReverie) || !(g.modules & kModMonuments) || sys < 0) return false;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (!((g.players[size_t(q)].swArm >> k) & 1) || g.players[size_t(q)].eliminated) continue;
        int s = -1, pl = -1;
        if (!monAbilityAt(g, q, "belkosea", &s, &pl) || s != sys || superweaponPlanet(g, k) < 0) continue;
        if (holder) *holder = q;
        if (planet) *planet = pl;
        return true;
    }
    return false;
}

namespace {
bool swHere(const Game& g, int sys, int k, int* owner = nullptr) {
    if (!(g.modules & kModBlueReverie) || sys < 0 || sys >= int(g.systems.size())) return false;
    for (int16_t x : g.systems[size_t(sys)].planets) {
        const PlanetState& ps = g.planets[size_t(x)];
        if (ps.superweapon == k && !ps.destroyed) {
            if (owner) *owner = ps.owner;
            return true;
        }
    }
    return armageddonAt(g, k, sys, owner, nullptr);
}
}  // namespace

bool availynNebula(const Game& g, int sys) { return swHere(g, sys, kSwAvailyn); }

int availynProduction(const Game& g, int p, int planet) {
    if (!(g.modules & kModBlueReverie) || planet < 0 || planet >= int(g.planets.size())) return 0;
    const PlanetState& ps = g.planets[size_t(planet)];
    if (ps.superweapon == kSwAvailyn && ps.owner == p && p >= 0 && !ps.destroyed) return 3;
    int h = -1, pl = -1;
    return armageddonAt(g, kSwAvailyn, ps.system, &h, &pl) && h == p && pl == planet ? 3 : 0;  // (the monument's PRODUCTION 3)
}

bool glatisonDenies(const Game& g, int p, int sys) {
    int o = -1;
    return swHere(g, sys, kSwGlatison, &o) && o >= 0 && o != p;
}

bool morsShield(const Game& g, int q, int planet) {
    if (!(g.modules & kModBlueReverie) || planet < 0 || planet >= int(g.planets.size())) return false;
    const PlanetState& ps = g.planets[size_t(planet)];
    if (ps.superweapon == kSwMors && ps.owner == q && q >= 0 && !ps.destroyed) return true;
    int h = -1, pl = -1;
    return armageddonAt(g, kSwMors, ps.system, &h, &pl) && h == q && pl == planet;  // (the monument's planet)
}

// "if a line from this system to that system would cross only parallel non-anomaly borders": the active system lies straight along
// one of the six hex directions (only then are all the borders crossed parallel), and every system on the way - both ends too - is
// on the board and no anomaly (hyperlanes count as empty systems). Not its own system.
bool caledLine(const Game& g, int from, int to) {
    if (from < 0 || to < 0 || from == to || from >= int(g.systems.size()) || to >= int(g.systems.size())) return false;
    const SystemState& a = g.systems[size_t(from)];
    const SystemState& b = g.systems[size_t(to)];
    if (a.offBoard || b.offBoard) return false;
    const int dist = hexDistance(a.pos, b.pos);
    for (int d = 0; d < 6; ++d) {
        const Hex step = hexDir(d);
        if (Hex{a.pos.q + step.q * dist, a.pos.r + step.r * dist} != b.pos) continue;
        for (int i = 0; i <= dist; ++i) {
            const int s = g.findSystemAt(Hex{a.pos.q + step.q * i, a.pos.r + step.r * i});
            if (s < 0 || isAnomaly(g, s)) return false;
        }
        return true;
    }
    return false;
}

bool swCardReady(const Game& g, int p, int k) {
    if (!(g.modules & kModBlueReverie) || p < 0 || p >= g.numPlayers || k < 0 || k >= kSuperweaponCount) return false;
    const Player& pl = g.players[size_t(p)];
    return ((pl.swCards >> k) & 1) && !((pl.swExhausted >> k) & 1);
}

// ---------------------------------------------------------------- the Pharad'n Order (0.85.0)
bool brPharadn(const Game& g, int p) {
    return (g.modules & kModBlueReverie) && p >= 0 && p < g.numPlayers && isFaction(g, p, "pharadn");
}
bool markOfPharadn(const Game& g, int p) { return g.phase != Phase::Setup && brPharadn(g, p); }
int pharCaptured(const Game& g, int p) { return capturedCount(g, p, UnitType::Infantry); }

// Black Pyramid (Monuments+): "This unit is treated as a structure and a ground force."
bool pyramidOn(const Game& g, int p, int planet) {
    if (!(g.modules & kModMonuments) || planet < 0 || !brPharadn(g, p)) return false;
    return g.planets[size_t(planet)].units[size_t(p)].count(UnitType::Monument) > 0;
}

}  // namespace ti
