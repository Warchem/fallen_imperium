// Dice, hit assignment, space combat (with anti-fighter barrage, Assault
// Cannon and retreats) and ground combat.
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
constexpr int kShipMask = kShipMaskAll;  // Carrier .. WarSun, the second flagship
constexpr int kNonFighter = kShipMask & ~(1 << int(UnitType::Fighter));
constexpr int kGroundMask = (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech));
}  // namespace

Rules::RollResult Rules::rollUnits(int p, const Force& f, RollKind kind, int modifier, int extraDice, int fighterBonus,
                                   const RollMods* mods) {
    RollResult r;
    r.valor = mods && mods->valor && kind == RollKind::Combat;
    int bestValue = 99;
    for (int t = 0; t < kUnitTypes; ++t) {
        if (!f.n[t]) continue;
        const UnitDef& u = unitDef(G, p, UnitType(t));
        int value = 0, dice = 0;
        switch (kind) {
            case RollKind::Combat: value = u.combat; dice = u.combatDice; break;
            case RollKind::AFB: value = u.afb; dice = u.afbDice; break;
            case RollKind::Bombard: value = u.bombard; dice = u.bombardDice; break;
            case RollKind::Cannon: value = u.cannon; dice = u.cannonDice; break;
        }
        if (value <= 0) continue;
        dice = std::max(1, dice);
        bestValue = std::min(bestValue, value);
        int need = value - modifier - (UnitType(t) == UnitType::Fighter ? fighterBonus : 0) - (mods ? mods->perType[t] : 0);
        int count = f.n[t] * dice + (mods ? mods->extraDice[t] : 0);
        for (int i = 0; i < count; ++i) {
            int v = E.rng().d10();
            r.dice.push_back(int8_t(v));
            const bool one = mods && mods->oneType == t && i < dice && kind == RollKind::Combat;  // (one ship's dice)
            r.need.push_back(int8_t(need - (one ? mods->oneBonus : 0)));
            r.unit.push_back(int8_t(t));
            r.bonus9.push_back(int8_t(mods && mods->flagshipBonus && isFlagship(UnitType(t)) ? 2 : 0));
        }
    }
    for (int i = 0; i < extraDice && bestValue < 99; ++i) {  // Plasma Scoring, Trrakan Aun Zulok, Strike Wing Ambuscade
        int need = bestValue - modifier;
        int v = E.rng().d10();
        r.dice.push_back(int8_t(v));
        r.need.push_back(int8_t(need));
        r.unit.push_back(-1);
        r.bonus9.push_back(0);
    }
    if (G.modules & kModDiscordant) dsOmen(p, kind, r);  // Divination, Lactarius Indigo (Myko-Mentori)
    recount(r);
    if ((G.modules & kModDiscordant) && G.fixes >= 2) {  // 0.43.0 (D114 / D115): after seeing the dice
        dsAfterRoll(p, kind, r);
        recount(r);
        if (kind == RollKind::Combat && breakthroughsOn(G)) {  // the Tribune (The Queens' Wrath), Raiding Parties, Awakened Planetoids
            dsbtTribune(p, r);
            dsbtCombatDice(p, r);
            recount(r);
        }
    }
    return r;
}

void Rules::recount(RollResult& r) const {
    r.hits = 0;
    for (size_t i = 0; i < r.dice.size(); ++i) {
        if (r.valor && r.dice[i] >= 10) ++r.hits;  // Valor (Kjalengard): a natural 10 makes 1 more hit, modifiers or not
        if (r.dice[i] < r.need[i]) continue;
        ++r.hits;
        if (i < r.bonus9.size() && r.dice[i] >= 9) r.hits += r.bonus9[i];
    }
}

void Rules::rerollDice(int p, RollResult& r, bool allDice) {
    (void)p;
    for (size_t i = 0; i < r.dice.size(); ++i)
        if (allDice || r.dice[i] < r.need[i]) r.dice[i] = int8_t(E.rng().d10());
    recount(r);
}

void Rules::reportRoll(int p, int sys, const std::string& what, const RollResult& r) {
    Event e;
    e.kind = EventKind::Dice;
    e.player = int8_t(p);
    e.system = int16_t(sys);
    e.dice = r.dice;
    e.need = r.need;
    e.hits = r.hits;
    e.label = what;
    std::string dice;
    for (size_t i = 0; i < r.dice.size(); ++i) {
        if (i) dice += ' ';
        dice += std::to_string(r.dice[i]);
        if (r.dice[i] >= r.need[i]) dice += '*';
    }
    e.text = pn(p) + " - " + what + ": " + (dice.empty() ? "no dice" : dice) + " = " + std::to_string(r.hits) + " hit" +
             (r.hits == 1 ? "" : "s");
    G.log.push_back(e.text);
    E.emit(e);
}

// Thunder's Edge (0.34.0): hits on neutral units go to the units lowest on the neutral unit reference card
// first (fighters, destroyers, cruisers, carriers, dreadnoughts, war suns, the flagship; on the ground
// infantry, then mechs); a unit that can sustain damage always does.
void Rules::neutralHits(int sys, int planet, int hits, int mask, const std::string& why, int attacker) {
    Force& f = planet >= 0 ? G.planets[planet].units[kNeutral] : G.systems[sys].space[kNeutral];
    static const UnitType order[] = {UnitType::Fighter, UnitType::Destroyer, UnitType::Cruiser, UnitType::Carrier,
                                     UnitType::Dreadnought, UnitType::WarSun, UnitType::Flagship, UnitType::Infantry,
                                     UnitType::Mech, UnitType::PDS, UnitType::SpaceDock};
    int destroy[kUnitTypes] = {};
    for (int h = 0; h < hits; ++h) {
        bool done = false;
        for (UnitType t : order) {
            const int i = int(t);
            if (mask && !((mask >> i) & 1)) continue;
            if (f.n[i] - destroy[i] <= 0) continue;
            const int left = f.n[i] - destroy[i], dmgLeft = std::max(0, f.dmg[i] - destroy[i]);  // (destroyed: damaged first)
            if (unitSustainsAt(G, kNeutral, t, sys, planet) && left - dmgLeft > 0) {
                f.dmg[i] = uint8_t(f.dmg[i] + 1);
                Event e;
                e.kind = EventKind::Damaged;
                e.player = int8_t(kNeutral);
                e.other = int8_t(attacker);
                e.system = int16_t(sys);
                e.planet = int16_t(planet);
                e.unit = int8_t(i);
                e.count = 1;
                e.text = std::string("A neutral ") + unitTypeName(t) + " sustains damage.";
                E.emit(e);
                if (planet < 0 && isShip(t)) {  // Direct Hit
                    WinCtx w;
                    w.w = Win::AfterSustain;
                    w.a = kNeutral;
                    w.b = attacker;
                    w.sys = sys;
                    w.value = i;
                    w.space = true;
                    window(w);
                }
            } else {
                ++destroy[i];
            }
            done = true;
            break;
        }
        if (!done) break;
    }
    for (int t = 0; t < kUnitTypes; ++t)
        if (destroy[t]) destroyUnits(kNeutral, UnitType(t), destroy[t], sys, planet, attacker, why.c_str());
}

void Rules::assignHits(int victim, int sys, int planet, int hits, int mask, const std::string& why, int attacker) {
    if (victim == kNeutral) {
        if (hits > 0) neutralHits(sys, planet, hits, mask, why, attacker);
        return;
    }
    hits = tellurian(victim, sys, planet, hits, mask);  // Titans agent
    if (hits <= 0) return;
    // Eidolon Maximum (0.43.0 T8f): it can't be assigned hits from unit abilities (space cannon, barrage,
    // bombardment...) - only combat rolls ("space combat" / "ground combat") reach it
    if (G.fixes >= 2 && victim < G.numPlayers && G.players[size_t(victim)].eidolonMax && why.find("combat") == std::string::npos) {
        mask = (mask ? mask : 0xFFFF) & ~(1 << int(UnitType::Mech));
        if (maxAbsorbableHits(G, victim, sys, planet, mask) <= 0) return;
    }
    struct HitsGuard {  // 0.43.0 (D126): these are hits - the one way to destroy the Li-Zho flagship
        bool& f;
        bool was;
        ~HitsGuard() { f = was; }
    } hitsGuard{byHits_, byHits_};
    byHits_ = true;
    int maxAbs = maxAbsorbableHits(G, victim, sys, planet, mask);
    if (maxAbs <= 0) return;
    Force& f = planet >= 0 ? G.planets[planet].units[victim] : G.systems[sys].space[victim];
    if (hits >= maxAbs) {
        // (fixes 26, the Yin review) the units there when the hits came: one that arrives while they are destroyed (Brother
        // Milor's 2 fighters after the carrier) isn't destroyed by the same hits - the extra hits are lost
        const Force hitNow = f;
        // (fixes 26, the Letnev review - owner 2026-11-12) LRR 78.6a: "Before assigning hits, players may use their units'
        // Sustain Damage abilities to cancel hits" - also when the rest destroys them. With something to gain from it (Rear
        // Admiral Farran: a trade good; Valkyrie Exoskeleton: a hit back) every unit that can sustain does, then all go.
        std::vector<int> sus;
        if (G.fixes >= 26 && victim < G.numPlayers &&
            (commanderUsable(G, victim, "letnev") || (inGroundCombat_ && planet >= 0 && fac(victim, "sardakk") && mechAbilities(G))))
            for (int t = 0; t < kUnitTypes; ++t) {
                if ((mask && !((mask >> t) & 1)) || !unitSustainsAt(G, victim, UnitType(t), sys, planet)) continue;
                while (f.dmg[t] < f.n[t]) {
                    sus.push_back(t);
                    f.dmg[t] = uint8_t(f.dmg[t] + 1);
                    Event e;
                    e.kind = EventKind::Damaged;
                    e.player = int8_t(victim);
                    e.other = int8_t(attacker);
                    e.system = int16_t(sys);
                    e.planet = int16_t(planet);
                    e.unit = int8_t(t);
                    e.count = 1;
                    e.text = pn(victim) + "'s " + unitLabel(G, victim, UnitType(t)) + " sustains damage.";
                    E.emit(e);
                }
            }
        for (int t = 0; t < kUnitTypes; ++t) {
            const int k = G.fixes >= 26 ? std::min<int>(hitNow.n[t], f.n[t]) : f.n[t];
            if ((!mask || ((mask >> t) & 1)) && k > 0) destroyUnits(victim, UnitType(t), k, sys, planet, attacker, why.c_str());
        }
        for (int t : sus) sustained(victim, attacker, sys, planet, t);
        return;
    }
    Decision d;
    d.player = victim;
    d.kind = DecisionKind::Hits;
    d.tag = Tag::AssignHits;
    d.prompt = "Assign " + std::to_string(hits) + " hit" + (hits > 1 ? "s" : "") + " (" + why + ")";
    d.context = planet >= 0 ? "Units on " + planetName(planet) : "Your ships in " + sysName(sys);
    d.system = sys;
    d.planet = planet;
    d.hits = hits;
    d.hitMask = mask;
    d.targetPlayer = attacker;
    const Game* gp = &G;
    d.validate = [gp, victim, sys, planet, hits, mask](const Answer& a) {
        return checkHits(*gp, victim, sys, planet, hits, mask, a.data);
    };
    Answer a = E.ask(std::move(d));
    // sustain first (it only marks units damaged), then destroy (damaged first)
    std::vector<int> sustainedTypes;
    for (size_t i = 0; i < a.data.size(); i += 2)
        if (a.data[i + 1] == 1) {
            sustainedTypes.push_back(a.data[i]);
            int t = a.data[i];
            f.dmg[t] = uint8_t(f.dmg[t] + 1);
            Event e;
            e.kind = EventKind::Damaged;
            e.player = int8_t(victim);
            e.other = int8_t(attacker);
            e.system = int16_t(sys);
            e.planet = int16_t(planet);
            e.unit = int8_t(t);
            e.count = 1;
            e.text = pn(victim) + "'s " + unitLabel(G, victim, UnitType(t)) + " sustains damage.";
            E.emit(e);
        }
    int destroy[kUnitTypes] = {};
    for (size_t i = 0; i < a.data.size(); i += 2)
        if (a.data[i + 1] != 1) ++destroy[a.data[i]];
    for (int t = 0; t < kUnitTypes; ++t)
        if (destroy[t]) destroyUnits(victim, UnitType(t), destroy[t], sys, planet, attacker, why.c_str());
    // Rear Admiral Farran, Valkyrie Exoskeleton
    for (int t : sustainedTypes) sustained(victim, attacker, sys, planet, t);
    // Direct Hit / Reflective Shielding: after a ship uses Sustain Damage (Wild, Wild Galaxy: Direct Hit on mechs too)
    const bool wildDH = eventOn(G, kEvWild);
    if (planet < 0 || wildDH)
        for (int t : sustainedTypes) {
            if (f.dmg[t] == 0) continue;
            // (fixes 27: a mech fighting as a ship with The Alastor - Direct Hit on it, the owner 2026-11-14)
            if (planet < 0 ? !isShip(UnitType(t)) && !((alastorMask(victim) >> t) & 1) : UnitType(t) != UnitType::Mech) continue;
            WinCtx w;
            w.w = Win::AfterSustain;
            w.a = victim;
            w.b = attacker;
            w.sys = sys;
            w.planet = planet;
            w.value = t;
            w.space = planet < 0;
            window(w);
        }
}

std::vector<int> Rules::retreatOptions(int p, int sys) const {
    std::vector<int> out;
    if (noRetreat_ || p == kNeutral) return out;  // Airo Shir Aur; neutral units never retreat
    bool det = owns(p, "det");
    for (int a : routeNeighbors(G, p, sys)) {  // incl. Quantum Entanglement (Creuss)
        if (a == dsBlockRetreat_[p]) continue;  // Lost Cause (Vaylerian flagship, Discordant Stars)
        const SystemState& s = G.systems[a];
        if (s.isHyperlane()) continue;
        uint8_t an = s.d().anomalies;
        if ((an & kSupernova) && !owns(p, "mr")) continue;  // Magmus Reactor
        if ((an & kAsteroid) && !owns(p, "amd")) continue;
        if (otherShipsIn(G, p, a)) continue;
        bool mine = hasUnits(G, p, a);
        for (int16_t pl : s.planets)
            if (G.planets[pl].owner == p) mine = true;
        bool empty = true;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && hasUnits(G, q, a)) empty = false;
        if (G.fixes >= 11 && hasUnits(G, kNeutral, a)) empty = false;  // (0.44.7: neutral units are units too - Thunder's Edge)
        if (mine || (det && empty)) out.push_back(a);
    }
    // (fixes 26, the Nomad review - owner 2026-11-13) Memoria: "treat this unit as if it were adjacent to systems that contain
    // 1 or more of your mechs" - when the flagship is the only ship retreating (with what it carries), those systems too
    if (G.fixes >= 26 && p < G.numPlayers && flagshipText(G, p, "nomad") && flagshipIn(G, p, sys) && G.systems[size_t(sys)].space[p].nonFighterShips() == 1)
        for (size_t a = 0; a < G.systems.size(); ++a) {
            if (int(a) == sys || std::find(out.begin(), out.end(), int(a)) != out.end() || G.systems[a].isHyperlane() || G.systems[a].offBoard) continue;
            bool mech = G.systems[a].space[p].count(UnitType::Mech) > 0;
            for (int16_t x : G.systems[a].planets) mech = mech || G.planets[size_t(x)].units[p].count(UnitType::Mech) > 0;
            if (!mech || int(a) == dsBlockRetreat_[p] || otherShipsIn(G, p, int(a))) continue;
            const uint8_t an = G.systems[a].d().anomalies;
            if ((an & kSupernova) && !owns(p, "mr")) continue;
            if ((an & kAsteroid) && !owns(p, "amd")) continue;
            out.push_back(int(a));
        }
    if (G.modules & kModDiscordant) dsRetreatExtra(p, sys, out);  // Nokar Navigator, Jack Hallard, Freelance Outfit
    // Cult of the All-Mother (Ghoti breakthrough, 0.43.0): any empty system without other players' ships
    if ((G.modules & kModDiscordant) && breakthroughsOn(G) && p < G.numPlayers && breakthroughIs(G, p, "ghoti"))
        for (size_t a = 0; a < G.systems.size(); ++a) {
            if (int(a) == sys || G.systems[a].isHyperlane() || !G.systems[a].planets.empty() || otherShipsIn(G, p, int(a))) continue;
            if (G.fixes >= 8) {  // 0.44.4: the usual limits of a retreat still apply - anomalies ships can't enter, Lost Cause, off-board
                const uint8_t an = anomaliesFor(G, p, int(a));
                if ((an & kSupernova) && !owns(p, "mr")) continue;
                if ((an & kAsteroid) && !owns(p, "amd")) continue;
                if (int(a) == dsBlockRetreat_[p] || G.systems[a].offBoard) continue;
            }
            bool empty = true;
            for (int q = 0; q < kSides && empty; ++q) empty = q == p || !hasUnits(G, q, int(a));
            if (empty && std::find(out.begin(), out.end(), int(a)) == out.end()) out.push_back(int(a));
        }
    return out;
}

// (0.54.0, fixes 18 - the tester's report; Living Rules FAQ on 78.7: "While retreating, a player may transport zero or
// more ground forces from planets they control in the active system") the retreating ships take ground forces from
// the planets along, as many as they have room for (what then doesn't fit of the space area's cargo stays behind:
// it is removed - the player's choice)
void Rules::retreatCargo(int p, int from) {
    if (G.fixes < 18 || p < 0 || p >= G.numPlayers) return;
    int room = shipCapacity(G, p, from);
    for (int16_t pl : G.systems[size_t(from)].planets) {
        if (G.planets[size_t(pl)].owner != p) continue;
        for (UnitType t : {UnitType::Infantry, UnitType::Mech}) {
            Force& on = G.planets[size_t(pl)].units[size_t(p)];
            const int most = std::min<int>(on.count(t), room);
            if (most <= 0) continue;
            std::vector<Option> opts;
            for (int k = 0; k <= most; ++k) opts.push_back({k, k == 0 ? std::string("None") : std::to_string(k), ""});
            const int was = curValue_;
            curValue_ = pl * 16 + int(t);
            const int k = pick(p, Tag::RetreatCargo,
                               "Take " + std::string(t == UnitType::Mech ? "mechs" : "infantry") + " from " + planetName(pl) + " along?", opts,
                               "Your retreating ships have room for " + std::to_string(room) +
                                   ". What they carry in space that no longer fits stays behind (removed).");
            curValue_ = was;
            if (k <= 0) continue;
            Force& sp = G.systems[size_t(from)].space[size_t(p)];
            const int ti = int(t);
            const int undamaged = on.n[ti] - on.dmg[ti];
            const int movedDamaged = std::max(0, k - undamaged);
            const int movedGalv = std::min<int>(k, on.galv[ti]);
            on.galv[ti] = uint8_t(on.galv[ti] - movedGalv);
            on.remove(t, k, false);
            sp.add(t, k);
            sp.dmg[ti] = uint8_t(sp.dmg[ti] + movedDamaged);
            sp.galv[ti] = uint8_t(sp.galv[ti] + movedGalv);
            room -= k;
            say(p, pn(p) + " takes " + std::to_string(k) + " " + unitTypeName(t, k != 1) + " from " + planetName(pl) + " along.", from);
        }
    }
}

void Rules::doRetreat(int p, int from, int to) {
    // LRR 78.7: all ships (and what they carry) move to one adjacent system;
    // whatever doesn't fit is removed there. A command token is placed there.
    if (G.fixes >= 27 && from == alastorSys_) alastorReturn(p);  // (The Alastor: the planets' ground forces stay on them)
    retreatCargo(p, from);  // (0.54.0, fixes 18: ground forces from the planets too)
    Force& src = G.systems[from].space[p];
    Force& dst = G.systems[to].space[p];
    for (int t = 0; t < kUnitTypes; ++t) {
        dst.n[t] = uint8_t(dst.n[t] + src.n[t]);
        dst.dmg[t] = uint8_t(dst.dmg[t] + src.dmg[t]);
        dst.galv[t] = uint8_t(dst.galv[t] + src.galv[t]);
        src.n[t] = src.dmg[t] = src.galv[t] = 0;
    }
    Event e;
    e.kind = EventKind::Retreat;
    e.player = int8_t(p);
    e.system = int16_t(from);
    e.from = {int16_t(to)};
    e.text = pn(p) + " retreats to " + sysName(to) + ".";
    E.emit(e);
    // Kado S'mah-Qar (Kollecc commander, Discordant Stars): no token when the system they left had none of theirs
    const bool kado = (G.modules & kModDiscordant) && commanderUsable(G, p, "kollecc") && !G.systems[size_t(from)].hasToken(p);
    if (!noRetreatToken_ && !kado) placeTokenFromReinforcements(p, to, "Retreating places a command token in the system you retreat to.");
    removeExcessCapacity(p, to);
    enforceFleetLimit(p);
    // A retreat through the ion storm's wormhole flips it too (owner, RULES-CHECK 0.5.0 #2).
    if (ionStormStep(G, from, to)) ionStormUsed(G.ionStorm);
    checkNexus(to);  // LRR 100.2: units moving into the nexus activate it
    // (fixes 27, the Saar review - owner 2026-11-13) a lone Saar floating factory there is blockaded now: "If this unit is
    // blockaded, it is destroyed" (a retreat or Skilled Retreat)
    if (G.fixes >= 27) checkFloatingFactories(to);
}

void Rules::combatStart(int att, int def, int sys, int planet) {
    if (sys == G.mecatol())  // Rickar Rickani unlock
        for (int x : {att, def})
            if (x >= 0 && x < G.numPlayers) G.players[size_t(x)].mecatolCombat = true;
    promiseCombat(att, def, sys);  // red line; promises not to attack
    persCombatStart(att, def, sys, planet);
    if (att >= 0 && att < G.numPlayers && G.systems[size_t(sys)].home >= 0 && G.systems[size_t(sys)].home != att)
        ++G.players[size_t(att)].raids;  // (0.54.0, habits)
    Event e;
    e.kind = EventKind::CombatStart;
    e.player = int8_t(att);
    e.other = int8_t(def);
    e.system = int16_t(sys);
    e.planet = int16_t(planet);
    e.space = planet < 0;
    if (planet < 0) {
        e.a = G.systems[sys].space[att];
        e.b = G.systems[sys].space[def];
        e.text = "Space combat in " + sysName(sys) + ": " + pn(att) + " attacks " + pn(def) + ".";
    } else {
        e.a = G.planets[planet].units[att];
        e.b = G.planets[planet].units[def];
        e.text = "Ground combat on " + planetName(planet) + ": " + pn(att) + " against " + pn(def) + ".";
    }
    E.emit(e);
}

void Rules::combatWon(int winner, int loser, int sys, bool space) {
    Event e;
    e.kind = EventKind::CombatEnd;
    e.player = int8_t(winner);
    e.other = int8_t(loser);
    e.system = int16_t(sys);
    e.space = space;
    e.text = pn(winner) + " wins the " + (space ? "space" : "ground") + " combat against " + pn(loser) + ".";
    E.emit(e);
    edictWon(winner, loser);  // Mahact
    btWonCombat(winner, loser, sys);  // N'orr Supremacy (Thunder's Edge)
    if (G.modules & kModThundersEdge) reapingWin(winner, loser, sys);  // The Reaping (Obsidian breakthrough; fixes 25)
    if ((G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtWonCombat(winner, loser, sys, space);  // (0.43.0)
    if (G.modules & kModDiscordant) dsWonCombat(winner, loser, sys, space);  // Discordant Stars
    int maxVp = 0;
    for (const Player& pl : G.players) maxVp = std::max<int>(maxVp, pl.vp);
    if (loser < G.numPlayers && G.players[loser].vp == maxVp) actionSecret(winner, "sar");
    if (space && G.systems[sys].space[winner].flagships() > 0) actionSecret(winner, "uf");
    if (isAnomaly(G, sys)) actionSecret(winner, "btv");
    int h = G.systems[sys].home;
    if (h >= 0 && h != winner) actionSecret(winner, "dts");
    if (space && sys == G.activeSystem && G.systems[sys].space[winner].nonFighterShips() >= 3) actionSecret(winner, "dyp");
    if (winner == G.activePlayer && loser < G.numPlayers && ((notesHeld_ >> loser) & 1)) actionSecret(winner, "baf");
    if (space) {
        WinCtx w;
        w.w = Win::WonSpaceCombat;
        w.a = winner;
        w.b = loser;
        w.sys = sys;
        window(w);
    }
}

namespace {
// Duranium Armor: repair 1 unit that was damaged before this round's hits. Returns the type repaired (-1: none).
// (0.44.8, fixes 12 - `best`: the most valuable one, not the first in the unit list)
int repairOne(Force& f, const Force& before, bool best) {
    if (best) {
        for (UnitType u : {UnitType::WarSun, UnitType::Flagship, UnitType::Flagship2, UnitType::Dreadnought, UnitType::Cruiser,
                           UnitType::Carrier, UnitType::Destroyer, UnitType::Mech})
            if (before.dmg[int(u)] > 0 && f.dmg[int(u)] > 0) {
                f.dmg[int(u)] = uint8_t(f.dmg[int(u)] - 1);
                return int(u);
            }
    }
    for (int t = 0; t < kUnitTypes; ++t)
        if (before.dmg[t] > 0 && f.dmg[t] > 0) {
            f.dmg[t] = uint8_t(f.dmg[t] - 1);
            return t;
        }
    return -1;
}
}  // namespace

// Duranium Armor: the repair (0.44.9, fixes 13 - the owner's answer W8: the player chooses which unit when there is
// more than one kind to repair)
int Rules::duranium(int p, Force& f, const Force& before) {
    if (G.fixes >= 13) {
        std::vector<Option> kinds;
        for (UnitType u : {UnitType::WarSun, UnitType::Flagship, UnitType::Flagship2, UnitType::Dreadnought, UnitType::Cruiser,
                           UnitType::Carrier, UnitType::Destroyer, UnitType::Mech})
            if (before.dmg[int(u)] > 0 && f.dmg[int(u)] > 0)
                kinds.push_back({int(u), unitTypeName(u), std::to_string(f.dmg[int(u)]) + " damaged"});
        for (int t = 0; t < kUnitTypes; ++t)  // (anything else that can be damaged)
            if (before.dmg[t] > 0 && f.dmg[t] > 0 && std::none_of(kinds.begin(), kinds.end(), [&](const Option& o) { return o.id == t; }))
                kinds.push_back({t, unitTypeName(UnitType(t)), std::to_string(f.dmg[t]) + " damaged"});
        if (kinds.empty()) return -1;
        const int t = kinds.size() == 1 ? kinds[0].id : pick(p, Tag::UseTech, "Duranium Armor: repair which unit?", kinds, techSummary(findTech("da")));
        f.dmg[t] = uint8_t(f.dmg[t] - 1);
        return t;
    }
    return repairOne(f, before, G.fixes >= 12);
}

void Rules::spaceCombat(int att, int def, int sys) {
    combatStart(att, def, sys, -1);
    // (fixes 27, the Nekro review - owner 2026-11-14) Technological Singularity: "Once per combat, after 1 of your opponent's
    // units is destroyed" - from the very start (Assault Cannon, Ambush, ...), and Antivirus ("At the start of a combat") before
    // those
    if (G.fixes >= 27) nekroCombatStart(att, def, sys);
    if ((G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtSpaceCombatStart(att, def, sys);  // Mirage Craft (0.43.0)
    if (G.modules & kModThundersEdge) phantomCombat(att, def, sys);  // Homesick Phantom (Crimson hero)
    SystemState& s = G.systems[sys];
    inSpaceCombat_ = true;
    combatDraw_ = false;
    {
        WinCtx w;
        w.w = Win::CombatStart;
        w.a = att;
        w.b = def;
        w.sys = sys;
        window(w);
    }

    // 0.44.9 (fixes 13 - the owner's answers W2 / W3): The Alastor and the Z-Grav flip are chosen first, so those
    // ground forces / mechs count as ships for Assault Cannon (Living Rules FAQ) and may be its loss; then each player
    // resolves their start-of-combat effects, the active player first (LRR 1.19) - a Creuss with both Assault Cannon
    // and Dimensional Splicer picks the order
    const bool v13 = G.fixes >= 13;
    if (v13)
        for (int p : {att, def}) {
            if (p < G.numPlayers) alastorChoice(p, sys);
            else alastor_[p] = false;
            zgravFlip(p, sys);
        }
    // (fixes 27) the planets' ground forces fighting from the space area - the combat screens take the new forces
    if (G.fixes >= 27 && alastorSys_ == sys && (!alastorFrom_.empty() || !alastorAside_[att].empty() || !alastorAside_[def].empty())) {
        Event e;
        e.kind = EventKind::CombatStart;
        e.player = int8_t(att);
        e.other = int8_t(def);
        e.system = int16_t(sys);
        e.planet = -1;
        e.space = true;
        e.a = s.space[att];
        e.b = s.space[def];
        e.text = "The Alastor's ground forces take their places in " + sysName(sys) + ".";
        E.emit(e);
    }
    // (fixes 25, owner's faction debug pass) Vos Hollow: ships destroyed before the first round count too
    const bool vosEarly = G.fixes >= 25 && (G.modules & kModThundersEdge);
    const Force startA = s.space[att], startD = s.space[def];
    auto acShips = [&](int side) {  // non-fighter ships for Assault Cannon
        int n = s.space[side].nonFighterShips();
        if (v13) {
            if (alastor_[side]) n += s.space[side].groundForces();
            else if (zgrav_[side]) n += s.space[side].count(UnitType::Mech);
        }
        return n;
    };
    auto acReady = [&](int side, int other) { return owns(side, "asc") && acShips(side) >= 3 && acShips(other) > 0; };
    auto assaultCannon = [&](int side, int other) {
        std::vector<Option> opts;
        for (int t : kShipIdx)
            if (t != int(UnitType::Fighter) && s.space[other].n[t])
                opts.push_back({t, unitTypeName(UnitType(t)), ""});
        if (v13) {  // (The Alastor's ground forces, Z-Grav Eidolons)
            if (alastor_[other] && s.space[other].count(UnitType::Infantry)) opts.push_back({int(UnitType::Infantry), "Infantry", "fighting as a ship"});
            if ((alastor_[other] || zgrav_[other]) && s.space[other].count(UnitType::Mech)) opts.push_back({int(UnitType::Mech), "Mech", "fighting as a ship"});
        }
        // (0.44.8, fixes 12) Silence of Stars: the Li-Zho flagship can't be destroyed this way - another ship goes
        if (G.fixes >= 12 && opts.size() > 1 && (G.modules & kModDiscordant) && other < G.numPlayers && fac(other, "lizho"))
            opts.erase(std::remove_if(opts.begin(), opts.end(), [](const Option& o) { return o.id == int(UnitType::Flagship); }),
                       opts.end());
        if (opts.empty()) return;
        int t = opts.size() == 1 ? opts[0].id
                                 : pick(other, Tag::AssignHits, pn(side) + "'s Assault Cannon: destroy one of your ships", opts);
        if (other == kNeutral)  // neutral units: the lowest on their reference card
            for (UnitType u : {UnitType::Destroyer, UnitType::Cruiser, UnitType::Carrier, UnitType::Dreadnought, UnitType::WarSun,
                               UnitType::Flagship})
                if (s.space[other].count(u)) {
                    t = int(u);
                    break;
                }
        destroyUnits(other, UnitType(t), 1, sys, -1, side, "Assault Cannon");
    };
    // Assault Cannon (start of combat): the active player's first; the other side's needs its 3 ships when it
    // resolves (0.30.0: the feed says so when the first one took that away - owner's 0.29.1 report)
    const bool ascBoth = owns(att, "asc") && owns(def, "asc") && acShips(att) >= 3 && acShips(def) >= 3;
    for (int side : {att, def}) {
        int other = side == att ? def : att;
        if (side == def && ascBoth && acShips(def) < 3)
            feedNote(def, pn(def) + "'s Assault Cannon can't fire: " + pn(att) +
                              "'s went first (active player) and left fewer than 3 non-fighter ships.");
        const bool spl = v13 && side < G.numPlayers && splicerReady(side, other, sys);
        bool splFirst = false;
        if (spl && acReady(side, other)) {
            const int cf = curFor_, cv = curValue_;
            curFor_ = other;  // (for the AI: whose ships, in which system)
            curValue_ = sys;
            splFirst = fyes(side, Ab::DimensionalSplicer, "Resolve Dimensional Splicer before Assault Cannon?",
                            "Both happen at the start of this combat - you choose the order.");
            curFor_ = cf;
            curValue_ = cv;
        }
        if (splFirst) {
            dimensionalSplicer(side, other, sys, true);
            if (acReady(side, other)) assaultCannon(side, other);
            continue;
        }
        if (acReady(side, other)) assaultCannon(side, other);
        if (spl && splicerReady(side, other, sys)) dimensionalSplicer(side, other, sys);
    }
    spaceCombatStart2(att, def, sys);  // Ipswitch, Ambush, Impulse Core
    spaceCombatStart3(att, def, sys);  // The Alastor, Dimensional Splicer, Antivirus
    spaceCombatStart4(att, def, sys);  // Z-Grav Eidolon, The Cavalry
    if (G.modules & kModDiscordant) dsSpaceCombatStart(att, def, sys);  // Lost Cause, Projection of Power
    if (vosEarly) vosHollow(att, def, sys, startA, startD);  // (Assault Cannon, Dimensional Splicer)
    // Units that fight: ships - and, with The Alastor, the Nekro player's ground forces in space;
    // Z-Grav Eidolons (Naaz-Rokha mechs) in the active system.
    auto fmask = [&](int p) {
        return kShipMask | (alastor_[p] ? kGroundMask : 0) | (zgrav_[p] ? (1 << int(UnitType::Mech)) : 0);
    };
    auto fighting = [&](int p) {
        return hasShips(G, p, sys) || (alastor_[p] && s.space[p].groundForces() > 0) || (zgrav_[p] && s.space[p].count(UnitType::Mech) > 0);
    };
    // Cosmic Phenomenae (galactic event): Fighter I doesn't fight in an asteroid field
    const bool rocks = eventOn(G, kEvCosmic) && (s.d().anomalies & kAsteroid);
    auto fighters = [&](int p) {
        Force f = s.space[p];
        for (int t = 0; t < kUnitTypes; ++t)
            if (!((fmask(p) >> t) & 1)) f.n[t] = f.dmg[t] = 0;
        if (rocks && p < G.numPlayers && unitDef(G, p, UnitType::Fighter).upgradeOf < 0) f.n[int(UnitType::Fighter)] = f.dmg[int(UnitType::Fighter)] = 0;
        return f;
    };

    const bool nebula = (s.d().anomalies & kNebula) != 0 || ((G.modules & kModDiscordant) && sys == G.dsWound) ||  // (the Wound, Nivyn)
                        // Aurora Research Base (Veldyr breakthrough, 0.44.4 fixes 8): their home system is a nebula
                        (G.fixes >= 8 && (G.modules & kModDiscordant) && s.home >= 0 && s.home < G.numPlayers && breakthroughsOn(G) &&
                         breakthroughIs(G, s.home, "veldyr"));
    const int nebulaBonus = eventOn(G, kEvCosmic) ? 3 : 1;  // Cosmic Phenomenae: +3 for the defender
    int retreater = -1;
    int quietRounds = 0, lastTotal = -1;  // (the stalemate rule below)
    for (int round = 1; round <= 60; ++round) {
        if (!fighting(att) || !fighting(def)) break;
        combatRound_ = round;
        if (round >= 2) G.fx.illusory = 0;  // Illusory Duplication lasts one combat round
        kuuasiHero(att, def, sys);  // Keleres hero: the flagship and escorts join the fight
        // Start of the combat round: Morale Boost, Fighter Prototype, Skilled Retreat, Emergency Repairs
        int bonus[2] = {0, 0}, fighterBonus[2] = {0, 0};
        {
            WinCtx w;
            w.w = Win::CombatRoundStart;
            w.a = att;
            w.b = def;
            w.sys = sys;
            w.first = round == 1;
            for (const Played& x : window(w)) {
                int side = x.player == att ? 0 : 1;
                if (!std::strcmp(kActionCards[x.card].key, "mb")) bonus[side] += eventOn(G, kEvWild) ? 2 : 1;  // (Wild, Wild Galaxy: +2)
                if (!std::strcmp(kActionCards[x.card].key, "f_prototype")) fighterBonus[side] += 2;
            }
        }
        if (combatDraw_ || !fighting(att) || !fighting(def)) break;
        // Faction abilities, agents and faction techs at the start of the round
        RollMods mods[2] = {combatMods(att, def, sys, -1, true), combatMods(def, att, sys, -1, true)};
        bool munitions[2] = {false, false};
        combatRoundStartFactions(att, def, sys, -1, true, mods, munitions);
        // Prophecy of Ixth: +1 to its owner's fighters.
        int ixth = lawOwner(G, "prophecy");
        if (ixth == att) fighterBonus[0] += 1;
        if (ixth == def) fighterBonus[1] += 1;

        // Anti-fighter barrage (first round only)
        if (round == 1) {
            const Force afbA = s.space[att], afbD = s.space[def];  // (Vos Hollow, fixes 25)
            for (int side : {att, def}) {
                int other = side == att ? def : att;
                const int si = side == att ? 0 : 1;
                int fightersBefore = s.space[other].count(UnitType::Fighter);
                Force afb;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (s.space[side].n[t] && unitDef(G, side, UnitType(t)).afb > 0) afb.add(UnitType(t), s.space[side].n[t]);
                const bool cavalry = cavalry_[si] >= 0 && s.space[side].count(UnitType(cavalry_[si])) > 0;  // The Cavalry
                // Metali Void Armaments (Thunder's Edge relic): Anti-Fighter Barrage 6 (x3) of its own
                const bool metaliArm = side < G.numPlayers && (G.modules & kModThundersEdge) && holdsRelic(side, "metalivoidarmaments");
                const int dsAfb = (G.modules & kModDiscordant) ? dsAfbDice(side, sys) : 0;  // Projection of Power, Trade Port
                const int dsAfb5 = (G.modules & kModDiscordant) ? dsAfbDice5(side, sys) : 0;  // Zelian B
                if (afb.empty() && !cavalry && !metaliArm && !dsAfb && !dsAfb5) continue;
                if ((scarAt(G, sys) && !ignoresScars(G, side)) || quietusAt(G, side, sys)) continue;  // Thunder's Edge: no anti-fighter barrage inside an entropic scar (the Quietus; 0.44.4: the Nivyn ignore it)
                // Iconoclast Omega (Naalu mech, Codex 0.31.0): no anti-fighter barrage against the Naalu's units in its system
                if (codexOn(G) && !teNaalu(G) && fac(other, "naalu") && mechAbilities(G) &&
                    (s.space[other].count(UnitType::Mech) || [&] {
                        for (int16_t x : s.planets)
                            if (G.planets[x].units[other].count(UnitType::Mech)) return true;
                        return false;
                    }())) {
                    say(other, pn(side) + " can't use anti-fighter barrage here (Iconoclast).", sys);
                    continue;
                }
                // Raid Formation / Strike Wing Alpha II make a barrage worth rolling even without fighters to hit
                bool argentUse = false;
                if (fac(side, "argent")) {
                    for (int t : kShipIdx)
                        if (s.space[other].n[t] > s.space[other].dmg[t] && unitSustains(G, other, UnitType(t))) argentUse = true;
                    if (s.space[other].count(UnitType::Infantry) && !std::strcmp(unitDef(G, side, UnitType::Destroyer).key, "argent_destroyer2"))
                        argentUse = true;
                }
                bool waylay = false;
                {
                    WinCtx w;
                    w.w = Win::BeforeAFB;
                    w.a = side;
                    w.b = other;
                    w.sys = sys;
                    waylay = cardHas(window(w), "waylay");
                }
                if (fightersBefore == 0 && !waylay && !argentUse) continue;
                const int afbMod = classifiedWeapons(side, sys, afb.total()) + (other < G.numPlayers ? assail(side, other) : 0) +  // (one question after the other)
                                   ((G.modules & kModDiscordant) && side < G.numPlayers ? dsAbilityMod(side) : 0);       // (Issac of Sinci, Omniscience Field)
                const int afbDice = abilityDice(side, sys, false);
                RollMods galvDice;  // Galvanize (Thunder's Edge): 1 more die each
                for (int t = 0; t < kUnitTypes; ++t) galvDice.extraDice[t] = afb.n[t] ? std::min(s.space[side].galv[t], s.space[side].n[t]) : 0;
                RollResult r = rollUnits(side, afb, RollKind::AFB, afbMod, afb.empty() ? 0 : afbDice, 0, &galvDice);
                if (metaliArm) addDice(r, 3, 6 - afbMod);
                if (dsAfb) addDice(r, dsAfb, 6 - afbMod);
                if (dsAfb5) addDice(r, dsAfb5, 5 - afbMod);
                cavalryDice(si, side, r, RollKind::AFB);
                reportRoll(side, sys, "Anti-fighter barrage", r);
                afterUnitRoll(side, other, sys, r, "anti-fighter barrage");
                // (fixes 26, the Nomad review - owner 2026-11-13) Quantum Manipulator: "during combat" - the barrage too
                if (G.fixes >= 26 && r.hits > 0 && other < G.numPlayers) r.hits = quantumManipulator(other, sys, r.hits, true);
                if (waylay) {
                    if (r.hits) assignHits(other, sys, -1, r.hits, kShipMask | alastorMask(other), "anti-fighter barrage (Waylay)", side);
                } else if (r.hits) {
                    destroyUnits(other, UnitType::Fighter, std::min(r.hits, fightersBefore), sys, -1, side, "anti-fighter barrage");
                    raidFormation(side, other, sys, r.hits - fightersBefore);  // Argent
                }
                strikeWingAlpha(side, other, sys, r);
                if (fightersBefore > 0 && s.space[other].count(UnitType::Fighter) == 0) actionSecret(side, "fwp");
            }
            if (vosEarly) vosHollow(att, def, sys, afbA, afbD);  // (the barrage's losses)
            if (!fighting(att) || !fighting(def)) break;
        }

        // LRR 78.4: the defender announces first; if they do, the attacker can't this round.
        bool rd = false, ra = false;
        bool routed = false;
        {
            WinCtx w;
            w.w = Win::AnnounceRetreats;
            w.a = att;
            w.b = def;
            w.sys = sys;
            routed = cardHas(window(w), "rout");
        }
        if (!routed && !retreatOptions(def, sys).empty())
            rd = yesNo(def, Tag::Retreat, "Announce a retreat from " + sysName(sys) + "?",
                       "You fight this round, then leave if any of your ships survive. Your opponent wins the combat.");
        if (rd && (G.modules & kModDiscordant) && dsCargoRaiders(def, att, sys)) rd = false;  // Cargo Raiders (Vaylerian)
        if (routed) {
            ra = true;  // Rout: the attacker must announce a retreat
            say(att, pn(att) + " is forced to announce a retreat (Rout).", sys);
        } else if (!rd && !retreatOptions(att, sys).empty()) {
            ra = yesNo(att, Tag::Retreat, "Announce a retreat from " + sysName(sys) + "?",
                       "You fight this round, then leave if any of your ships survive. Your opponent wins the combat.");
            if (ra && (G.modules & kModDiscordant) && dsCargoRaiders(att, def, sys)) ra = false;
        }
        if (rd) say(def, pn(def) + " announces a retreat.", sys);
        if (ra && !routed) say(att, pn(att) + " announces a retreat.", sys);
        dsRetreating_ = rd ? def : ra ? att : -1;  // (Jack Hallard, Discordant Stars)
        if ((rd || ra) && (G.modules & kModDiscordant) && breakthroughsOn(G) && dsbtQuickRetreat(rd ? def : att, sys)) {  // Raiding Parties
            retreater = rd ? def : att;
            break;
        }
        if (rd || ra) {
            WinCtx w;
            w.w = Win::AfterRetreat;
            w.a = rd ? def : att;
            w.b = rd ? att : def;
            w.sys = sys;
            if (cardHas(window(w), "intercept")) {
                say(w.a, pn(w.a) + " can't retreat this round (Intercept).", sys);
                rd = ra = false;
            }
        }
        // Watchful Ojz (Ral Nel commander; fixes 27, the Ral Nel review): "When you declare a retreat: Immediately retreat up
        // to 2 of your ships" - before this round's rolls
        if ((rd || ra) && (G.modules & kModThundersEdge) && G.fixes >= 27) {
            const int who = rd ? def : att;
            ojz(who, who == def ? att : def, sys, -1);
            if (!hasShips(G, who, sys)) {  // (all of them gone: the combat is over)
                retreater = who;
                break;
            }
        }
        RollResult rA, rD;
        for (int again = 0; again < 4; ++again) {
            Force fa = fighters(att), fd = fighters(def);  // transported ground forces don't roll
            rA = rollUnits(att, fa, RollKind::Combat, bonus[0], 0, fighterBonus[0], &mods[0]);
            rD = rollUnits(def, fd, RollKind::Combat, (nebula ? nebulaBonus : 0) + bonus[1], 0, fighterBonus[1], &mods[1]);
            cavalryDice(0, att, rA, RollKind::Combat);  // The Cavalry
            cavalryDice(1, def, rD, RollKind::Combat);
            crownOfThalnos(att, sys, -1, rA);
            crownOfThalnos(def, sys, -1, rD);
            reportRoll(att, sys, "Combat round " + std::to_string(round), rA);
            reportRoll(def, sys, "Combat round " + std::to_string(round), rD);
            afterCombatRolls(att, def, sys, rA, rD, munitions);  // Munitions Reserves, War Funding, Wrath of Kenara
            if (!thundarian(att, def, sys, rA.hits, rD.hits)) break;  // The Thundarian: roll again
        }
        if (G.modules & kModDiscordant) dsAfterCombatRolls(att, def, sys, -1, rA, rD);  // Verrisus Ypru, Zhrgar Stimulants
        confusedSage(att, def, sys, rA);  // Uncharted Space (0.33.0)
        confusedSage(def, att, sys, rD);
        Force beforeA = s.space[att], beforeD = s.space[def];
        // Shields Holding: cancel up to 2 hits before assigning them.
        auto shields = [&](int victim, int& hits) {
            if (hits <= 0) return;
            WinCtx w;
            w.w = Win::BeforeSpaceHits;
            w.a = victim;
            w.b = victim == att ? def : att;
            w.sys = sys;
            w.value = hits;
            if (cardHas(window(w), "sh")) {
                int c = std::min(2, hits);
                hits -= c;
                say(victim, pn(victim) + " cancels " + std::to_string(c) + " hit" + (c > 1 ? "s" : "") + ".", sys);
            }
        };
        const bool dsOn = (G.modules & kModDiscordant) != 0;
        const int keepA = dsOn ? dsUncancellable(att, def, sys, rA) : 0, keepD = dsOn ? dsUncancellable(def, att, sys, rD) : 0;  // Raider II
        shields(def, rA.hits);
        shields(att, rD.hits);
        rA.hits = spaceHitsCancel(def, 1, sys, rA.hits, att);  // The Cavalry, Quantum Manipulator
        rD.hits = spaceHitsCancel(att, 0, sys, rD.hits, def);
        if (dsOn) {  // Queen Lorena (Kortali commander)
            rA.hits = std::max(keepA, dsCancelHits(def, att, sys, -1, rA.hits));
            rD.hits = std::max(keepD, dsCancelHits(att, def, sys, -1, rD.hits));
        }
        for (int side = 0; side < 2; ++side) {
            const int victim = side == 0 ? def : att, shooter = side == 0 ? att : def;
            const RollResult& r = side == 0 ? rA : rD;
            // [0.0.1] (L1Z1X flagship): hits by it and its dreadnoughts go to non-fighter ships if able
            int restricted = std::min(restrictedHits(shooter, sys, r) + ((G.modules & kModDiscordant) ? dsRestrictedHits(shooter, victim, sys, r) : 0),
                                      maxAbsorbableHits(G, victim, sys, -1, kNonFighter));  // (+ Raider cruisers, Discordant Stars)
            restricted = std::min(restricted, r.hits);
            if (restricted > 0) {
                assignHits(victim, sys, -1, restricted, kNonFighter, "space combat ([0.0.1])", shooter);
                assignHits(victim, sys, -1, r.hits - restricted, fmask(victim), "space combat", shooter);
            } else {
                assignHits(victim, sys, -1, r.hits, fmask(victim), "space combat", shooter);
            }
        }
        // Courageous to the End: after one of your ships is destroyed.
        for (int side : {att, def}) {
            const Force& before = side == att ? beforeA : beforeD;
            int best = -1;
            for (int t : kShipIdx)
                if (s.space[side].n[t] < before.n[t] &&
                    (best < 0 || unitDef(G, side, UnitType(t)).combat < unitDef(G, side, UnitType(best)).combat))
                    best = t;
            if (best < 0) continue;
            WinCtx w;
            w.w = Win::ShipDestroyed;
            w.a = side;
            w.b = side == att ? def : att;
            w.sys = sys;
            w.value = best;
            window(w);
        }
        if (G.modules & kModDiscordant) {  // Deliverance Engine, Carcinisation, Daedalon Flight System
            dsShipsLost(att, def, sys, beforeA);
            dsShipsLost(def, att, sys, beforeD);
            if (breakthroughsOn(G)) {  // Exodus Engineering (0.43.0)
                dsbtShipsLost(att, def, sys, beforeA);
                dsbtShipsLost(def, att, sys, beforeD);
            }
            dsSpaceRoundEnd(att, def, sys);
        }
        if (G.modules & kModThundersEdge) {
            vosHollow(att, def, sys, beforeA, beforeD);  // Vos Hollow (Obsidian agent)
            heavensEye(att, def, sys);                    // Heaven's Eye (Firmament flagship)
        }
        for (int q : {att, def})
            if (owns(q, "da")) {
                const int t = duranium(q, s.space[q], q == att ? beforeA : beforeD);
                if (t >= 0 && G.fixes >= 12) say(q, pn(q) + " repairs 1 " + unitTypeName(UnitType(t)) + " (Duranium Armor).", sys);
            }
        {
            WinCtx w;
            w.w = Win::CombatRoundEnd;
            w.a = att;
            w.b = def;
            w.sys = sys;
            window(w);
        }
        // Exotrireme II ("after a round of space combat"; fixes 25, owner 2026-11-11: after the Retreat step - LRR 78.7 /
        // 78.8 make it step 5 of the round, so ships that retreat get away; below)
        if (G.fixes < 25) afterSpaceRound(att, def, sys);
        // Devotion (Yin): "after each space combat round" - (fixes 26, the Yin review - owner 2026-11-12) after the Retreat
        // step, which ends the round (LRR 78.7 / 78.8; below), as Exotrireme II
        if (G.fixes < 26 && hasShips(G, att, sys) && hasShips(G, def, sys)) devotion(att, def, sys);
        // LRR 78.7: retreat only happens if the opponent still has ships.
        int who = rd ? def : ra ? att : -1;
        int opp = who == def ? att : def;
        if (who >= 0 && hasShips(G, who, sys) && fighting(opp)) {
            std::vector<int> dest = retreatOptions(who, sys);
            if (!dest.empty()) {
                std::vector<Option> opts;
                for (int x : dest) opts.push_back({x, sysName(x), ""});
                int to = opts.size() == 1 ? opts[0].id
                                          : pick(who, Tag::RetreatTo, "Retreat to which system?", opts, "", true);
                if (G.modules & kModThundersEdge) ralnelRetreat(who, opp, sys, to);  // The Last Dispatch, Watchful Ojz
                if ((G.modules & kModDiscordant) && dsRetreatCapture(who, sys)) {  // Nightingale V (Kollecc): onto the sheet instead
                    retreater = who;
                    break;
                }
                doRetreat(who, sys, to);
                retreater = who;
                break;
            }
        }
        if (G.fixes >= 25) afterSpaceRound(att, def, sys);  // Exotrireme II, after the Retreat step (no retreat happened)
        if (G.fixes >= 26 && hasShips(G, att, sys) && hasShips(G, def, sys)) devotion(att, def, sys);  // Devotion (Yin), the same
        // (0.44.9, fixes 13 - the owner's answer W14; Living Rules FAQ) a combat nobody can win - e.g. Duranium Armor and
        // Non-Euclidean Shielding cancelling every hit: 20 rounds in a row without a unit lost, then the attacker retreats,
        // or is destroyed if it can't
        if (G.fixes >= 13 && fighting(att) && fighting(def)) {
            const int now = int(s.space[att].total() + s.space[def].total());
            quietRounds = now == lastTotal ? quietRounds + 1 : 0;
            lastTotal = now;
            if (quietRounds >= 20) {
                say(att, "Neither side can destroy the other: " + pn(att) + " must retreat, or is destroyed.", sys);
                std::vector<int> dest = retreatOptions(att, sys);
                if (!dest.empty()) {
                    std::vector<Option> opts;
                    for (int x : dest) opts.push_back({x, sysName(x), ""});
                    const int to = opts.size() == 1 ? opts[0].id : pick(att, Tag::RetreatTo, "Retreat to which system?", opts, "", true);
                    doRetreat(att, sys, to);
                    retreater = att;
                } else {
                    for (int t = 0; t < kUnitTypes; ++t)
                        if (s.space[att].n[t] && ((fmask(att) >> t) & 1)) destroyUnits(att, UnitType(t), s.space[att].n[t], sys, -1, def, "no way to win");
                }
                break;
            }
        }
    }
    inSpaceCombat_ = false;
    G.dsGale = -1;  // (Splintering Gale: only during the combat)
    G.dsKomatSys = -1;  // (Komat: the same)
    // LRR 78.10: the player with ships left in the system wins (a retreat counts).
    bool a = fighting(att), d = fighting(def);
    if (G.fixes >= 27) {  // (The Alastor: the planets' ground forces go back, those aside return)
        alastorReturn(att);
        alastorReturn(def);
        alastorSys_ = -1;
    }
    alastor_[att] = alastor_[def] = false;
    int winner = combatDraw_ ? -1 : (a && !d) ? att : (d && !a) ? def : -1;
    // Subversive (Kyro, Discordant Stars): a combat that would end in a draw is theirs
    const int dsDraw = (G.modules & kModDiscordant) && winner < 0 && (combatDraw_ || (!a && !d)) ? dsDrawWinner(att, def) : -1;
    if (dsDraw >= 0) {
        winner = dsDraw;
        say(dsDraw, pn(dsDraw) + " turns the draw into a win (Subversive).", sys);
        combatWon(dsDraw, dsDraw == att ? def : att, sys, true);
    } else if (combatDraw_) {
        Event e;
        e.kind = EventKind::CombatEnd;
        e.system = int16_t(sys);
        e.text = "The combat ends in a draw (Skilled Retreat).";
        E.emit(e);
    } else if (a && !d) {
        combatWon(att, def, sys, true);
    } else if (d && !a) {
        combatWon(def, att, sys, true);
    } else if (!a && !d) {
        Event e;
        e.kind = EventKind::CombatEnd;
        e.system = int16_t(sys);
        e.text = retreater >= 0 ? "The combat ends." : "Both fleets are destroyed - the combat is a draw.";
        E.emit(e);
    }
    combatDraw_ = false;
    persSpaceCombatEnd(att, def, sys);
    spaceCombatEnded(att, def, sys, winner);  // Salvage Operations, S'ula Mentarion
    spaceCombatEnd4(att, def, sys);           // Z-Grav Eidolons flip back, The Cavalry returns
    combatEnded6(att, def, sys, -1);          // Thunder's Edge factions: Phoenix Standard, ...
    removeExcessCapacity(att, sys);
    removeExcessCapacity(def, sys);
}

void Rules::groundCombat(int att, int def, int planet) {
    PlanetState& pl = G.planets[planet];
    int sys = pl.system;
    combatStart(att, def, sys, planet);
    // LRR 65.3a: a war sun in the system switches off planetary shields (and so Magen Defense Grid).
    bool warSun = false;
    for (int t = 0; t < kUnitTypes; ++t)
        if (G.systems[sys].space[att].n[t] && unitDef(G, att, UnitType(t)).ignoresShield) warSun = true;
    // Matriarch (Naalu flagship): fighters that landed fight as ground forces
    // Hel-Titans (Titans PDS) are ground forces too
    auto mask = [&](int p) {
        return kGroundMask | (fightersLand(p, sys) ? (1 << int(UnitType::Fighter)) : 0) | (fac(p, "titans") ? (1 << int(UnitType::PDS)) : 0);
    };
    auto ground = [&](int p) {
        Force f;
        for (int t = 0; t < kUnitTypes; ++t)
            if ((mask(p) >> t) & 1) {
                f.n[t] = pl.units[p].n[t];
                f.dmg[t] = pl.units[p].dmg[t];
            }
        return f;
    };
    auto fighting = [&](int p) { return ground(p).groundForces() + ground(p).count(UnitType::Fighter) + ground(p).count(UnitType::PDS); };
    struct PlanetFlag {
        int16_t& v;
        ~PlanetFlag() { v = -1; }
    } planetFlag{G.groundCombatPlanet};
    G.groundCombatPlanet = int16_t(planet);
    {
        WinCtx w;
        w.w = Win::CombatStart;
        w.a = att;
        w.b = def;
        w.sys = sys;
        w.planet = planet;
        w.space = false;
        window(w);
    }
    groundCombatStartFactions(att, def, planet);  // Claire Gibson, Tekklar Legion
    if (G.modules & kModDiscordant) dsGroundCombatStart(att, def, planet);  // Ruthless (Kortali)
    // Magen Defense Grid Omega (Codex, 0.31.0): at the start of ground combat on a planet with your structures,
    // 1 hit on one of your opponent's ground forces
    // (0.44.8, fixes 12: the Cheiran's Byssus mechs count as structures; the hit can go to any unit fighting there -
    // Hel-Titans, the Matriarch's fighters)
    if (codexOn(G))
        for (int side : {def, att}) {
            const int other = side == def ? att : def;
            const bool v12 = G.fixes >= 12;
            const bool structs = pl.units[side].structures() > 0 ||
                                 (v12 && (G.modules & kModDiscordant) && side < G.numPlayers && pl.units[side].count(UnitType::Mech) &&
                                  pl.owner == side && fac(side, "cheiran"));
            if (!owns(side, "md_base") || !structs || (v12 ? fighting(other) == 0 : pl.units[other].groundForces() == 0)) continue;
            say(side, pn(side) + "'s Magen Defense Grid makes 1 hit.", sys);
            assignHits(other, sys, planet, 1, v12 ? mask(other) : kGroundMask, "Magen Defense Grid", side);
        }
    struct GroundFlag {
        bool& f;
        ~GroundFlag() { f = false; }
    } groundFlag{inGroundCombat_};
    inGroundCombat_ = true;
    if (G.modules & kModDiscordant) dsOncePerCombat_ = 0;
    for (int round = 1; round <= 60; ++round) {
        if (fighting(att) == 0 || fighting(def) == 0) break;
        combatRound_ = round;
        int bonus[2] = {0, 0};
        {
            WinCtx w;
            w.w = Win::CombatRoundStart;
            w.a = att;
            w.b = def;
            w.sys = sys;
            w.planet = planet;
            w.space = false;
            w.first = round == 1;
            for (const Played& x : window(w))
                if (!std::strcmp(kActionCards[x.card].key, "mb")) bonus[x.player == att ? 0 : 1] += eventOn(G, kEvWild) ? 2 : 1;
        }
        RollMods mods[2] = {combatMods(att, def, sys, planet, false), combatMods(def, att, sys, planet, false)};
        bool munitions[2] = {false, false};
        combatRoundStartFactions(att, def, sys, planet, false, mods, munitions);  // Evelyn DeLouis, Dunlain Reaper
        if (fighting(att) == 0 || fighting(def) == 0) break;
        bool magen = false;
        if (techReady(def, "md_base") && !warSun && !shieldsDown(G, def, sys) && !codexOn(G)) {
            bool shield = false;
            for (int t = 0; t < kUnitTypes; ++t)
                if (pl.units[def].n[t] && unitDef(G, def, UnitType(t)).shield &&
                    !(G.fixes >= 12 && disabled_ && UnitType(t) == UnitType::PDS))  // (0.44.8: Disable takes the PDS's shield)
                    shield = true;
            if (shield && yesNo(def, Tag::UseTech, "Exhaust Magen Defense Grid?",
                                "Your opponent rolls no dice in this round of ground combat.")) {
                exhaustTech(def, "md_base");
                magen = true;
            }
        }
        RollResult rA, rD;
        for (int again = 0; again < 4; ++again) {
            rA = RollResult();
            Force fa = ground(att), fd = ground(def);
            if (G.modules & kModDiscordant) dsGroundRollers(att, def, planet, fa, fd);  // Megalith (Khrask mech)
            if (!magen) rA = rollUnits(att, fa, RollKind::Combat, bonus[0], 0, 0, &mods[0]);
            rD = rollUnits(def, fd, RollKind::Combat, bonus[1], 0, 0, &mods[1]);
            // Fire Team: reroll the misses.
            for (int side : {att, def}) {
                RollResult& r = side == att ? rA : rD;
                if (r.dice.empty()) continue;
                WinCtx w;
                w.w = Win::AfterGroundRoll;
                w.a = side;
                w.b = side == att ? def : att;
                w.sys = sys;
                w.planet = planet;
                w.space = false;
                w.value = int(r.dice.size()) - r.hits;
                if (cardHas(window(w), "fire_team")) rerollDice(side, r, false);
            }
            // ATS Armaments (Lanefir, 0.44.8 fixes 12): the tokens taken this round reroll misses in ground combat too
            // (they used to be spent for nothing there)
            if (G.fixes >= 12 && (G.modules & kModDiscordant))
                for (int i = 0; i < 2; ++i) {
                    RollResult& r = i == 0 ? rA : rD;
                    if (dsRerolls_[i] <= 0 || (i == 0 && magen) || r.hits >= int(r.dice.size())) continue;
                    int left = dsRerolls_[i];
                    for (size_t d = 0; d < r.dice.size() && left > 0; ++d)
                        if (r.dice[d] < r.need[d]) {
                            r.dice[d] = int8_t(E.rng().d10());
                            --left;
                        }
                    recount(r);
                    dsRerolls_[i] = 0;
                    reportRoll(i == 0 ? att : def, sys, "ATS Armaments reroll", r);
                }
            if (!magen) crownOfThalnos(att, sys, planet, rA);
            crownOfThalnos(def, sys, planet, rD);
            if (!magen) reportRoll(att, sys, "Ground combat round " + std::to_string(round), rA);
            reportRoll(def, sys, "Ground combat round " + std::to_string(round), rD);
            if (!thundarian(att, def, sys, rA.hits, rD.hits)) break;  // The Thundarian: roll again
        }
        if (G.modules & kModDiscordant) dsAfterCombatRolls(att, def, sys, planet, rA, rD);  // Zhrgar Stimulants
        if (!magen) confusedSage(att, def, sys, rA);  // Uncharted Space (0.33.0)
        confusedSage(def, att, sys, rD);
        for (int side : {att, def}) {  // X-89 Bacterial Weapon (Codex, Thunder's Edge's version - 0.43.3): twice the hits
            RollResult& r = side == att ? rA : rD;
            if (side >= G.numPlayers || r.hits <= 0 || (side == att && magen) || !x89Omega(G, side)) continue;
            r.hits *= 2;
            say(side, pn(side) + "'s ground combat hits are doubled (X-89 Bacterial Weapon): " + std::to_string(r.hits) + ".", sys);
        }
        // Valkyrie Particle Weave: +1 hit if the opponent produced any (fixes 25, Sardakk review 2026-11-11: the hits their
        // dice produced - one side's extra hit doesn't trigger the other's (a Nekro with it against the Sardakk); the
        // card's wording in the log)
        {
            const int hitsA = rA.hits;
            const std::string vpwLine = G.fixes >= 25 ? " produces 1 additional hit (Valkyrie Particle Weave)." : " scores 1 extra hit (Valkyrie Particle Weave).";
            if (owns(att, "vpw") && rD.hits > 0 && !magen) {
                ++rA.hits;
                say(att, pn(att) + vpwLine, sys);
            }
            if (owns(def, "vpw") && (G.fixes >= 25 ? hitsA : rA.hits) > 0) {
                ++rD.hits;
                say(def, pn(def) + vpwLine, sys);
            }
        }
        // Carapace Plating (Uncharted Space): cancel up to 2 hits; Shields Holding too (Wild, Wild Galaxy)
        for (int side : {att, def}) {
            int& hits = side == att ? rD.hits : rA.hits;
            if (hits <= 0 || (!(G.modules & kModUncharted) && !eventOn(G, kEvWild))) continue;
            WinCtx w;
            w.w = Win::GroundHits;
            w.a = side;
            w.b = side == att ? def : att;
            w.sys = sys;
            w.planet = planet;
            w.space = false;
            w.value = hits;
            const std::vector<Played> played = window(w);
            if (cardHas(played, "carapace_plating")) {
                hits = std::max(0, hits - 2);
                say(side, pn(side) + " cancels up to 2 hits (Carapace Plating).", sys);
            }
            if (cardHas(played, "sh")) {
                hits = std::max(0, hits - 2);
                say(side, pn(side) + " cancels up to 2 hits (Shields Holding).", sys);
            }
        }
        if (G.modules & kModDiscordant) {  // Queen Lorena, Spc. Phquaiset
            rA.hits = dsCancelHits(def, att, sys, planet, rA.hits);
            rD.hits = dsCancelHits(att, def, sys, planet, rD.hits);
        }
        Force beforeA = pl.units[att], beforeD = pl.units[def];
        assignHits(def, sys, planet, rA.hits, mask(def), "ground combat", att);
        assignHits(att, sys, planet, rD.hits, mask(att), "ground combat", def);
        for (int q : {att, def})
            if (owns(q, "da")) {
                const int t = duranium(q, pl.units[q], q == att ? beforeA : beforeD);
                if (t >= 0 && G.fixes >= 12) say(q, pn(q) + " repairs 1 " + unitTypeName(UnitType(t)) + " (Duranium Armor).", sys);
            }
        if (G.modules & kModDiscordant) dsGroundRoundEnd(att, def, planet);  // Hulgade's Hammer (round 1), Collector
        // Contagion / Safety Overrides (Uncharted Space)
        if (G.modules & kModUncharted)
            for (int side : {att, def}) {
                const int other = side == att ? def : att;
                const Force& before = side == att ? beforeA : beforeD;
                if (before.count(UnitType::Infantry) > pl.units[side].count(UnitType::Infantry) && pl.units[side].count(UnitType::Infantry) > 0) {
                    WinCtx w;
                    w.w = Win::InfantryKilled;
                    w.a = other;
                    w.b = side;
                    w.sys = sys;
                    w.planet = planet;
                    w.space = false;
                    window(w);
                }
                if (before.count(UnitType::Mech) > pl.units[side].count(UnitType::Mech)) {
                    WinCtx w;
                    w.w = Win::MechKilled;
                    w.a = side;
                    w.b = other;
                    w.sys = sys;
                    w.planet = planet;
                    w.space = false;
                    window(w);
                }
            }
        {
            WinCtx w;
            w.w = Win::CombatRoundEnd;
            w.a = att;
            w.b = def;
            w.sys = sys;
            w.planet = planet;
            w.space = false;
            window(w);
        }
        harrow(att, def, planet);  // L1Z1X: bombardment after each round
        if (G.modules & kModThundersEdge) alarum(att, def, planet);  // Ral Nel mech
        // Heaven's Eye (fixes 25, owner's faction debug pass): "the end of every combat round" - ground combat rounds too
        // (LRR 42.4: they have "end of combat round" effects); a Firmament flagship in the space area above
        if ((G.modules & kModThundersEdge) && G.fixes >= 25)
            for (int q = 0; q < G.numPlayers; ++q) heavensEye(q, -1, sys);
        if ((G.modules & kModDiscordant) && breakthroughsOn(G) && dsbtGroundRoundEnd(att, def, planet)) {  // Hidden Hardpoints (0.43.0)
            if (G.fixes >= 8) {  // 0.44.4: the combat screen closes (nobody won - the units coexist)
                Event e;
                e.kind = EventKind::CombatEnd;
                e.system = int16_t(sys);
                e.planet = int16_t(planet);
                e.space = false;
                e.text = "The ground combat on " + planetName(planet) + " ends: the units coexist (Hidden Hardpoints).";
                E.emit(e);
            }
            combatEnded6(att, def, sys, planet);
            return;
        }
    }
    bool a = fighting(att) > 0, d = fighting(def) > 0;
    // (fixes 27, the Naalu review - LRR FAQ "The Naalu Collective": "If no Naalu ground forces are present, the combat is
    // considered to be a draw") the Matriarch's fighters alone don't win a ground combat
    bool onlyFighters = false;
    if (G.fixes >= 27)
        for (int side : {att, def}) {
            const Force fo = ground(side);
            if (fighting(side) > 0 && fo.groundForces() + fo.count(UnitType::PDS) == 0) {
                (side == att ? a : d) = false;
                onlyFighters = true;
            }
        }
    int winner = (a && !d) ? att : (d && !a) ? def : -1;
    if (winner < 0 && (G.modules & kModDiscordant) && dsDrawWinner(att, def) >= 0) {  // Subversive (Kyro): the draw is theirs
        winner = dsDrawWinner(att, def);
        say(winner, pn(winner) + " turns the draw into a win (Subversive).", sys);
        combatWon(winner, winner == att ? def : att, sys, false);
        combatEnded6(att, def, sys, planet);
        dsGroundCombatOver(att, def, planet, winner);
        // 0.44.6 (fixes 10): a win is a win - Dacxive Animators too
        if (G.fixes >= 10 && owns(winner, "dxa") && unitsAvailable(G, winner, UnitType::Infantry) > 0 &&
            yesNo(winner, Tag::UseTech, "Use Dacxive Animators?", "Place 1 infantry on " + planetName(planet) + "."))
            addUnits(winner, UnitType::Infantry, 1, sys, planet);
        return;
    }
    if (winner < 0) {
        if (G.modules & kModDiscordant) dsGroundCombatOver(att, def, planet, -1);  // Tox
        Event e;
        e.kind = EventKind::CombatEnd;
        e.system = int16_t(sys);
        e.planet = int16_t(planet);
        e.space = false;
        e.text = onlyFighters ? "The ground combat on " + planetName(planet) + " is a draw: only fighters are left (Matriarch)."
                              : "Both sides' ground forces are wiped out on " + planetName(planet) + " - a draw.";
        E.emit(e);
        combatEnded6(att, def, sys, planet);
        return;
    }
    combatWon(winner, winner == att ? def : att, sys, false);
    combatEnded6(att, def, sys, planet);  // Thunder's Edge factions
    if (G.modules & kModDiscordant) dsGroundCombatOver(att, def, planet, winner);  // Tox (Kyro agent)
    if (owns(winner, "dxa") && unitsAvailable(G, winner, UnitType::Infantry) > 0 &&
        yesNo(winner, Tag::UseTech, "Use Dacxive Animators?", "Place 1 infantry on " + planetName(planet) + "."))
        addUnits(winner, UnitType::Infantry, 1, sys, planet);
}

}  // namespace ti
