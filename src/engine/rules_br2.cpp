// Blue Reverie (0.84.0, TacticBlue's homebrew - module kModBlueReverie): the Belkosea Allied States. The cards' wording (owner
// 2026-11-21) in faction_text.cpp and query_br.cpp (kSuperweapons). Readings (sent to the owner 2026-11-24, RULES-CHECK 0.84.0):
//   Classified Developments - the component action (6000 + Ab::BrClassified): 5 resources / influence in any mix (each planet one or
//                    the other, trade goods too), a superweapon not built yet (one of each; a purged one never comes back) on a planet
//                    they control without one (not a space station).
//   Arrogance      - superweapons are structures (query structuresOf / planetsWithStructuresOutsideHome); they aren't units, so no
//                    ability can destroy them; lost only when the planet changes hands (setControl) or is destroyed - then purged
//                    with the card (brSuperweaponLost).
//   "At any time"  - on their own turn (action list options 660-668, they don't use the action) and at the start of every round of a
//                    combat they fight in (brAnyTime; "Not during this combat" stops the questions).
//   Availyn        - a nebula (query anomaliesFor / isAnomaly / the combat's +1), PRODUCTION 3 on its planet; the card: up to 3 fighters
//                    in a system with their ships (productionCore, fighters only).
//   Caled          - in a combat in the active system: 1 of the opponent's ships there, the opponent choosing which (destroyed, not a
//                    hit), when the active system lies straight along a hex direction from Caled's with no anomaly on the way
//                    (query caledLine; hyperlanes count as empty systems).
//   Glatison       - the card repairs all their damaged units everywhere; other players' units in its system lose SUSTAIN DAMAGE
//                    (query sustainBlockedAt); ANTI-FIGHTER BARRAGE 4 (x2) in its system's space combats they fight (brExtraDice).
//   Grom           - a die at 4 for each other player's fighter in its system or an adjacent one.
//   Mors           - every undamaged unit with SUSTAIN DAMAGE in a system neither its own nor adjacent becomes damaged (theirs too);
//                    PLANETARY SHIELD on its planet (brPlanetShield).
//   Conviction     - (legendary, S.O.L.G. Industries Group) 2 commodities; readied whenever they exhaust a superweapon card.
//   Synchrony Matrix (technology) - readies a superweapon card, a planet, a legendary planet card, a relic, an agent / Spike, the
//                    breakthrough or the monument card.
//   Trinity Stockpile (breakthrough) - a strategy token purged (Player::brTokensPurged - out of the game), every unit on a planet with a
//                    superweapon or in / next to their home system destroyed, then the planet exhausted.
//   Wyvern I / II  - commit to planets as ground forces in their invasions (fightersLand, like the Matriarch), back to space after it.
//   Uzean Wardog   - per hit its dice produce: 1 commodity, or 1 commodity turned into a trade good (brWardog).
//   Ascendancy     - at the start of a space combat round: damage it for 1 hit on the opponent's non-fighter ships.
//   Razalka Gris   - after a system is activated: the active player moves through 1 system's other ships (TacticalEffects::razalkaSys).
//   Ciphus Gault   - from the second round of space combat: their units with ANTI-FIGHTER BARRAGE roll it instead of their combat
//                    rolls (RollMods::useAfb; the hits are barrage hits - fighters only, unless Spike).
//   Spike          - at the start of a space combat: their fighters' barrage hits any ship this combat; readied when they lose a combat
//                    (no units left there, or they retreated) - never purged.
//   Mercenary Sortie (note) - at the start of a combat its holder fights: 3 dice, each 8+ destroys 1 of the opponent's fighters / infantry
//                    in that combat; back to the Belkosea after the first round.
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
constexpr int kShipMask = kShipMaskAll;
constexpr int kNonFighter = kShipMask & ~(1 << int(UnitType::Fighter));

void bkEvent(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    if (text.find(name) == std::string::npos) {  // the ability's name in the line (feed, game log, simulator counts)
        e.text = text.size() && text.back() == '.' ? text.substr(0, text.size() - 1) : text;
        e.text += " (" + name + ").";
    }
    e.system = int16_t(sys);
    E.emit(e);
}
std::string swName(int k) { return k >= 0 && k < kSuperweaponCount ? kSuperweapons[k].name : "?"; }
bool isKey(const Game& g, int planet, const char* key) {
    return planet >= 0 && planet < int(g.planets.size()) && !std::strcmp(kPlanets[g.planets[size_t(planet)].def].key, key);
}
// Conviction (Belkosea home space station): the planet, if p controls it
int convictionOf(const Game& g, int p) {
    for (int x : controlledPlanets(g, p))
        if (isKey(g, x, "conviction")) return x;
    return -1;
}
int sustainUndamaged(const Game& g, int q, int sys) {  // q's undamaged units in sys that can sustain damage
    int n = 0;
    const SystemState& s = g.systems[size_t(sys)];
    for (int t = 0; t < kUnitTypes; ++t) {
        if (s.space[size_t(q)].n[t] > s.space[size_t(q)].dmg[t] && unitSustainsAt(g, q, UnitType(t), sys, -1))
            n += s.space[size_t(q)].n[t] - s.space[size_t(q)].dmg[t];
        for (int16_t x : s.planets) {
            const Force& f = g.planets[size_t(x)].units[size_t(q)];
            if (f.n[t] > f.dmg[t] && unitSustainsAt(g, q, UnitType(t), sys, x)) n += f.n[t] - f.dmg[t];
        }
    }
    return n;
}
int damagedUnits(const Game& g, int p) {
    int n = 0;
    for (const SystemState& s : g.systems)
        for (int t = 0; t < kUnitTypes; ++t) n += s.space[size_t(p)].dmg[t];
    for (const PlanetState& ps : g.planets)
        for (int t = 0; t < kUnitTypes; ++t) n += ps.units[size_t(p)].dmg[t];
    return n;
}
int othersFighters(const Game& g, int p, int sys) {
    int n = 0;
    for (int q = 0; q < kSides; ++q)
        if (q != p && (q < g.numPlayers || q == kNeutral)) n += g.systems[size_t(sys)].space[size_t(q)].count(UnitType::Fighter);
    return n;
}
}  // namespace

// ================================================================== Arrogance
// "When a superweapon is destroyed, instead purge it and its superweapon card." - the planet changed hands or is gone
void Rules::brSuperweaponLost(int planet, const char* why) {
    if (planet < 0 || planet >= int(G.planets.size())) return;
    PlanetState& ps = G.planets[size_t(planet)];
    const int k = ps.superweapon;
    if (k < 0) return;
    const int o = ps.owner;
    ps.superweapon = -1;
    if (o >= 0 && o < G.numPlayers) {
        Player& pl = G.players[size_t(o)];
        pl.swCards = uint8_t(pl.swCards & ~(1u << k));
        pl.swExhausted = uint8_t(pl.swExhausted & ~(1u << k));
        pl.swPurged = uint8_t(pl.swPurged | (1u << k));
    }
    brArmageddonLost(k);
    bkEvent(E, o, "Arrogance", swName(k) + " on " + planetName(planet) + " is " + why + ": it and its card are purged (Arrogance).", ps.system);
}

// Exhaust superweapon card k - S.O.L.G. Industries Group (Conviction): "When you exhaust a superweapon card, ready this card."
void Rules::brExhaustSw(int p, int k) {
    Player& pl = G.players[size_t(p)];
    pl.swExhausted = uint8_t(pl.swExhausted | (1u << k));
    const int c = convictionOf(G, p);
    if (c >= 0 && G.planets[size_t(c)].abilityExhausted) {
        G.planets[size_t(c)].abilityExhausted = false;
        say(p, pn(p) + " readies S.O.L.G. Industries Group (Conviction).", G.planets[size_t(c)].system);
    }
}

// ================================================================== Classified Developments
// "ACTION: Spend a combined total of 5 resources or influence to place 1 superweapon on a planet you control, and gain its superweapon card."
std::vector<int> Rules::brClassifiedPlanets(int p) const {
    std::vector<int> v;
    for (int x : controlledPlanets(G, p)) {
        const PlanetState& ps = G.planets[size_t(x)];
        if (ps.superweapon < 0 && !ps.destroyed && !isStation(G, x) && !isDMZ(G, x)) v.push_back(x);
    }
    return v;
}
void Rules::brActionOptions(int p, std::vector<Option>& o) {
    if (!brOn() || !fac(p, "belkosea")) return;
    const Player& pl = G.players[size_t(p)];
    if ((pl.swCards | pl.swPurged) == (1u << kSuperweaponCount) - 1) return;  // all five built (or purged)
    if (brClassifiedPlanets(p).empty() || spendableMixed(G, p) < 5) return;
    o.push_back({6000 + int(Ab::BrClassified), "Classified Developments",
                 "Spend 5 resources or influence in any mix: place a superweapon on a planet you control and gain its card."});
}
bool Rules::brAction(int p, Ab ab) {
    Player& pl = G.players[size_t(p)];
    if (ab != Ab::BrClassified || !fac(p, "belkosea")) return false;
    std::vector<Option> opts;
    for (int k = 0; k < kSuperweaponCount; ++k)
        if (!(((pl.swCards | pl.swPurged) >> k) & 1)) opts.push_back({k, kSuperweapons[k].name, kSuperweapons[k].text});
    const std::vector<int> where = brClassifiedPlanets(p);
    if (opts.empty() || where.empty()) return false;
    opts.push_back({-1, "Cancel", "Choose a different action."});
    const int k = fpick(p, Ab::BrClassified, "Classified Developments: build which superweapon?", opts,
                        "It costs 5 resources or influence in any mix. Each planet can hold 1 superweapon.");
    if (k < 0) return false;
    std::vector<Option> pls;
    for (int x : where)
        pls.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + " resources, " + std::to_string(planetInfluence(G, x)) + " influence"});
    pls.push_back({-1, "Cancel", ""});
    curValue_ = k;
    const int x = fpick(p, Ab::BrClassifiedWhere, std::string("Classified Developments: place ") + kSuperweapons[k].name + " on which planet?", pls,
                        kSuperweapons[k].text, false, true);
    curValue_ = 0;
    if (x < 0) return false;
    if (!pay(p, 5, false, "Classified Developments", true, 0, 0, true)) return false;
    G.planets[size_t(x)].superweapon = int8_t(k);
    pl.swCards = uint8_t(pl.swCards | (1u << k));
    pl.swExhausted = uint8_t(pl.swExhausted & ~(1u << k));
    bkEvent(E, p, "Classified Developments", pn(p) + " builds the superweapon " + swName(k) + " on " + planetName(x) + " (Classified Developments).",
            G.planets[size_t(x)].system);
    return true;
}

// ================================================================== the superweapon cards ("At any time")
// Could superweapon card k do something now? (att / def / sys: the combat it is asked in, -1 none)
// (mon: through the Armageddon Project card - "This card has the text abilities of each superweapon card in a play area that has 1 of
// your control tokens": exhaust the monument card, "this system" = the monument's)
bool Rules::brSwUsable(int p, int k, int att, int def, int sys, bool mon) const {
    int home = -1;
    if (mon) {
        if (!brArmageddonHas(p, k, &home)) return false;
    } else {
        if (!swCardReady(G, p, k)) return false;
        home = superweaponSystem(G, k);
    }
    if (home < 0) return false;
    switch (k) {
        case kSwAvailyn: {
            if (unitsAvailable(G, p, UnitType::Fighter) <= 0 || spendable(G, p, false) < 1) return false;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (G.systems[s].space[size_t(p)].ships() > 0) return true;
            return false;
        }
        case kSwCaled: {
            if (att < 0 || sys != G.activeSystem || (p != att && p != def)) return false;
            const int opp = p == att ? def : att;
            return opp >= 0 && G.systems[size_t(sys)].space[size_t(opp)].ships() > 0 && caledLine(G, home, sys);
        }
        case kSwGlatison: return damagedUnits(G, p) > 0;
        case kSwGrom: {
            if (othersFighters(G, p, home) > 0) return true;
            for (int16_t a : G.systems[size_t(home)].adj)
                if (othersFighters(G, p, a) > 0) return true;
            return false;
        }
        case kSwMors:
            for (size_t s = 0; s < G.systems.size(); ++s) {
                if (int(s) == home || std::find(G.systems[size_t(home)].adj.begin(), G.systems[size_t(home)].adj.end(), int16_t(s)) !=
                                          G.systems[size_t(home)].adj.end())
                    continue;
                for (int q = 0; q < kSides; ++q)
                    if (q != p && (q < G.numPlayers || q == kNeutral) && sustainUndamaged(G, q, int(s)) > 0) return true;
            }
            return false;
        default: return false;
    }
}

// Use superweapon card k: false = backed out (nothing spent)
bool Rules::brUseSw(int p, int k, int att, int def, int sys, bool mon) {
    if (!brSwUsable(p, k, att, def, sys, mon)) return false;
    int home = superweaponSystem(G, k);
    if (mon) brArmageddonHas(p, k, &home);
    auto spend = [&]() {  // exhaust the superweapon card - or the monument card
        if (!mon) {
            brExhaustSw(p, k);
            return;
        }
        G.players[size_t(p)].monExhausted = true;
        const int card = monumentIndex("belkosea");
        monEvent(p, card, pn(p) + " exhausts " + monName(card) + " for " + swName(k) + "'s ability.", home);
    };
    switch (k) {
        case kSwAvailyn: {
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (G.systems[s].space[size_t(p)].ships() > 0) {
                    Option o{int(s), sysName(int(s)), std::to_string(G.systems[s].space[size_t(p)].count(UnitType::Fighter)) + " fighters there"};
                    o.at = int(s);
                    opts.push_back(o);
                }
            opts.push_back({-1, "Cancel", ""});
            const int where = fpick(p, Ab::BrAvailyn, "Availyn: produce up to 3 fighters in which system?", opts, kSuperweapons[k].text, true);
            if (where < 0) return false;
            spend();
            bkEvent(E, p, "Availyn", pn(p) + "'s Availyn launches fighters into " + sysName(where) + " (Availyn).", where);
            const int was = dsTypeMask_;
            const bool na = notProductionAbility_;
            dsTypeMask_ = 1 << int(UnitType::Fighter);
            notProductionAbility_ = true;
            productionCore(p, where, 3, false, false);
            notProductionAbility_ = na;
            dsTypeMask_ = was;
            return true;
        }
        case kSwCaled: {
            const int opp = p == att ? def : att;
            const SystemState& s = G.systems[size_t(sys)];
            spend();
            std::vector<Option> opts;
            for (int t : kShipIdx)
                if (s.space[size_t(opp)].n[t]) opts.push_back({t, unitTypeName(UnitType(t)), std::to_string(s.space[size_t(opp)].n[t]) + " here"});
            if (opts.empty()) return true;
            int t = opts[0].id;
            if (opp == kNeutral) {  // neutral units: the lowest on their reference card
                for (UnitType u : {UnitType::Fighter, UnitType::Destroyer, UnitType::Cruiser, UnitType::Carrier, UnitType::Dreadnought,
                                   UnitType::WarSun, UnitType::Flagship})
                    if (s.space[size_t(opp)].count(u)) {
                        t = int(u);
                        break;
                    }
            } else if (opts.size() > 1) {
                curFor_ = p;
                t = pick(opp, Tag::AssignHits, pn(p) + "'s Caled fires: destroy one of your ships here", opts,
                         "Caled destroys 1 of your ships in the active system - you choose which.");
                curFor_ = -1;
            }
            bkEvent(E, p, "Caled", pn(p) + "'s Caled fires from " + sysName(home) + " (Caled).", sys);
            destroyUnits(opp, UnitType(t), 1, sys, -1, p, "Caled");
            return true;
        }
        case kSwGlatison: {
            spend();
            int n = 0;
            for (SystemState& s : G.systems)
                for (int t = 0; t < kUnitTypes; ++t) n += s.space[size_t(p)].dmg[t], s.space[size_t(p)].dmg[t] = 0;
            for (PlanetState& ps : G.planets)
                for (int t = 0; t < kUnitTypes; ++t) n += ps.units[size_t(p)].dmg[t], ps.units[size_t(p)].dmg[t] = 0;
            bkEvent(E, p, "Glatison", pn(p) + "'s Glatison repairs " + std::to_string(n) + (n == 1 ? " unit" : " units") + " (Glatison).", home);
            return true;
        }
        case kSwGrom: {
            std::vector<Option> opts;
            std::vector<int> cand{home};
            for (int16_t a : G.systems[size_t(home)].adj) cand.push_back(a);
            for (int s : cand)
                if (othersFighters(G, p, s) > 0) {
                    Option o{s, sysName(s), std::to_string(othersFighters(G, p, s)) + " other players' fighters"};
                    o.at = s;
                    opts.push_back(o);
                }
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            const int where = fpick(p, Ab::BrGrom, "Grom: anti-fighter barrage 4 against each fighter in which system?", opts, kSuperweapons[k].text, true);
            if (where < 0) return false;
            spend();
            bkEvent(E, p, "Grom", pn(p) + "'s Grom fires into " + sysName(where) + " (Grom).", where);
            for (int q = 0; q < kSides; ++q) {
                if (q == p || (q >= G.numPlayers && q != kNeutral)) continue;
                const int f = G.systems[size_t(where)].space[size_t(q)].count(UnitType::Fighter);
                if (!f) continue;
                RollResult r;
                addDice(r, f, 4);
                reportRoll(p, where, "Grom (anti-fighter barrage)", r);
                if (r.hits) destroyUnits(q, UnitType::Fighter, std::min(r.hits, f), where, -1, p, "Grom");
            }
            return true;
        }
        case kSwMors: {
            std::vector<Option> opts;
            const std::vector<int16_t>& adj = G.systems[size_t(home)].adj;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                if (int(s) == home || std::find(adj.begin(), adj.end(), int16_t(s)) != adj.end()) continue;
                int others = 0;
                for (int q = 0; q < kSides; ++q)
                    if (q != p && (q < G.numPlayers || q == kNeutral)) others += sustainUndamaged(G, q, int(s));
                if (others <= 0) continue;
                Option o{int(s), sysName(int(s)),
                         std::to_string(others) + " of other players' units, " + std::to_string(sustainUndamaged(G, p, int(s))) + " of yours"};
                o.at = int(s);
                opts.push_back(o);
            }
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            const int where = fpick(p, Ab::BrMors, "Mors: force every unit in which system to become damaged?", opts, kSuperweapons[k].text, true);
            if (where < 0) return false;
            spend();
            int n = 0;
            SystemState& s = G.systems[size_t(where)];
            for (int q = 0; q < kSides; ++q) {
                if (q >= G.numPlayers && q != kNeutral) continue;
                for (int t = 0; t < kUnitTypes; ++t) {
                    Force& f = s.space[size_t(q)];
                    if (f.n[t] > f.dmg[t] && unitSustainsAt(G, q, UnitType(t), where, -1)) n += f.n[t] - f.dmg[t], f.dmg[t] = f.n[t];
                    for (int16_t x : s.planets) {
                        Force& g2 = G.planets[size_t(x)].units[size_t(q)];
                        if (g2.n[t] > g2.dmg[t] && unitSustainsAt(G, q, UnitType(t), where, x)) n += g2.n[t] - g2.dmg[t], g2.dmg[t] = g2.n[t];
                    }
                }
            }
            bkEvent(E, p, "Mors", pn(p) + "'s Mors strikes " + sysName(where) + ": " + std::to_string(n) + (n == 1 ? " unit is" : " units are") + " damaged (Mors).",
                    where);
            return true;
        }
        default: return false;
    }
}

// ================================================================== Synchrony Matrix, Trinity Stockpile, Conviction
// Synchrony Matrix: "ready 1 non-technology component other than a strategy card in your play area" - id: 100 + superweapon,
// 1000 + planet, 20000 + legendary planet card, 30000 + relic, 40000 + leader slot, 50000 breakthrough, 50001 monument card
std::vector<Option> Rules::brSynchronyTargets(int p) const {
    std::vector<Option> o;
    const Player& pl = G.players[size_t(p)];
    for (int k = 0; k < kSuperweaponCount; ++k)
        if (((pl.swCards & pl.swExhausted) >> k) & 1) o.push_back({100 + k, std::string("Superweapon: ") + kSuperweapons[k].name, kSuperweapons[k].text});
    for (int x : controlledPlanets(G, p)) {
        const PlanetState& ps = G.planets[size_t(x)];
        if (ps.exhausted && !ps.destroyed)
            o.push_back({1000 + x, planetName(x), std::to_string(planetResources(G, x)) + " resources, " + std::to_string(planetInfluence(G, x)) + " influence"});
        if (ps.abilityExhausted && *legendaryName(ps.def)) o.push_back({20000 + x, legendaryName(ps.def), legendaryText(ps.def)});
    }
    for (int16_t r : pl.exhaustedRelics) o.push_back({30000 + r, kRelics[r].name, "Relic"});
    for (size_t i = 0; i < pl.leaders.size(); ++i)
        if (pl.leaders[i].status == kExhausted) o.push_back({40000 + int(i), kLeaders[pl.leaders[i].leader].name, "Leader"});
    if (hasBreakthrough(G, p) && pl.btExhausted) o.push_back({50000, "Breakthrough", ""});
    if (pl.monExhausted) o.push_back({50001, "Monument card", ""});
    return o;
}
bool Rules::brSynchrony(int p) {
    if (!techReady(p, "dsbelky")) return false;
    std::vector<Option> o = brSynchronyTargets(p);
    if (o.empty()) return false;
    o.push_back({-1, "Cancel", ""});
    const int c = fpick(p, Ab::BrSynchrony, "Synchrony Matrix: ready which component?", o, "Exhaust Synchrony Matrix to ready it.");
    if (c < 0) return false;
    exhaustTech(p, "dsbelky");
    Player& pl = G.players[size_t(p)];
    std::string what;
    if (c >= 100 && c < 100 + kSuperweaponCount) {
        pl.swExhausted = uint8_t(pl.swExhausted & ~(1u << (c - 100)));
        what = swName(c - 100);
    } else if (c >= 1000 && c < 20000) {
        G.planets[size_t(c - 1000)].exhausted = false;
        what = planetName(c - 1000);
    } else if (c >= 20000 && c < 30000) {
        G.planets[size_t(c - 20000)].abilityExhausted = false;
        what = legendaryName(G.planets[size_t(c - 20000)].def);
    } else if (c >= 30000 && c < 40000) {
        pl.exhaustedRelics.erase(std::remove(pl.exhaustedRelics.begin(), pl.exhaustedRelics.end(), int16_t(c - 30000)), pl.exhaustedRelics.end());
        what = kRelics[c - 30000].name;
    } else if (c >= 40000 && c < 50000) {
        const int i = c - 40000;
        what = kLeaders[pl.leaders[size_t(i)].leader].name;
        if (kLeaders[pl.leaders[size_t(i)].leader].type == kAgent) readyAgent(p, i);
        else pl.leaders[size_t(i)].status = kReady;
    } else if (c == 50000) {
        pl.btExhausted = false;
        what = "their breakthrough";
    } else if (c == 50001) {
        pl.monExhausted = false;
        what = "their monument card";
    }
    bkEvent(E, p, "Synchrony Matrix", pn(p) + " readies " + what + " (Synchrony Matrix).");
    return true;
}

// Trinity Stockpile: planets with a superweapon, or adjacent to their home system (LRR 6.3: its own planets too), with units on them
std::vector<int> Rules::brTrinityPlanets(int p) const {
    std::vector<int> v;
    const int home = G.players[size_t(p)].homeSystem;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.destroyed) continue;
        bool ok = ps.superweapon >= 0;
        if (!ok && home >= 0) {
            const std::vector<int16_t>& adj = G.systems[size_t(home)].adj;
            ok = ps.system == home || std::find(adj.begin(), adj.end(), ps.system) != adj.end();
        }
        if (!ok) continue;
        bool units = false;
        for (int q = 0; q < kSides && !units; ++q) units = !ps.units[size_t(q)].empty();
        if (units) v.push_back(int(x));
    }
    return v;
}
bool Rules::brTrinityReady(int p) const {
    const Player& pl = G.players[size_t(p)];
    return brOn() && breakthroughsOn(G) && breakthroughIs(G, p, "belkosea") && !pl.btExhausted && pl.strategy > 0;
}
bool Rules::brTrinity(int p, int onlyPlanet) {
    if (!brTrinityReady(p)) return false;
    std::vector<Option> o;
    for (int x : brTrinityPlanets(p)) {
        if (onlyPlanet >= 0 && x != onlyPlanet) continue;
        int mine = 0, theirs = 0;
        for (int q = 0; q < kSides; ++q) (q == p ? mine : theirs) += G.planets[size_t(x)].units[size_t(q)].total();
        o.push_back({x, planetName(x), std::to_string(theirs) + " of other players' units, " + std::to_string(mine) + " of yours"});
    }
    if (o.empty()) return false;
    o.push_back({-1, "Cancel", ""});
    const int x = fpick(p, Ab::BrTrinity, "Trinity Stockpile: destroy every unit on which planet?", o,
                        "Exhaust the breakthrough and purge 1 command token from your strategy pool. Then the planet is exhausted.", false, true);
    if (x < 0) return false;
    Player& pl = G.players[size_t(p)];
    pl.btExhausted = true;
    pl.strategy = int8_t(pl.strategy - 1);
    pl.brTokensPurged = int8_t(pl.brTokensPurged + 1);
    PlanetState& ps = G.planets[size_t(x)];
    const int sys = ps.system;
    bkEvent(E, p, "Trinity Stockpile", pn(p) + " purges a command token: Trinity Stockpile strikes " + planetName(x) + " (Trinity Stockpile).", sys);
    for (int q = 0; q < kSides; ++q) {
        if (q >= G.numPlayers && q != kNeutral) continue;
        for (int t = 0; t < kUnitTypes; ++t)
            if (ps.units[size_t(q)].n[t]) destroyUnits(q, UnitType(t), ps.units[size_t(q)].n[t], sys, x, p, "Trinity Stockpile");
    }
    if (ps.owner >= 0 && !ps.exhausted) {
        ps.exhausted = true;
        say(p, planetName(x) + " is exhausted (Trinity Stockpile).", sys);
    }
    return true;
}

// ================================================================== their own turn: options 660-668 (they don't use the action)
void Rules::brFreeOptions(int p, std::vector<Option>& o) {
    if (!brOn() || p < 0 || p >= G.numPlayers) return;
    const Player& pl = G.players[size_t(p)];
    for (int k = 0; k < kSuperweaponCount; ++k)
        if (brSwUsable(p, k, -1, -1, -1))
            o.push_back({660 + k, std::string("Superweapon: ") + kSuperweapons[k].name, std::string(kSuperweapons[k].text) + " Doesn't use your action."});
    const int c = convictionOf(G, p);
    if (c >= 0 && !G.planets[size_t(c)].abilityExhausted && pl.commodities < commodityCap(G, pl))
        o.push_back({665, "S.O.L.G. Industries Group (Conviction)", "Exhaust it to gain 2 commodities. Doesn't use your action."});
    if (techReady(p, "dsbelky") && !brSynchronyTargets(p).empty())
        o.push_back({666, "Synchrony Matrix", "Exhaust it to ready 1 component in your play area (not a technology or strategy card). Doesn't use your action."});
    if (brTrinityReady(p) && !brTrinityPlanets(p).empty())
        o.push_back({667, "Breakthrough: Trinity Stockpile",
                     "Exhaust it and purge 1 strategy token: destroy every unit on a planet with a superweapon or in or next to your home system. "
                     "Doesn't use your action."});
    brArmageddonOptions(p, o);  // 668 (Monuments+)
}
bool Rules::brFreeAction(int p, int id) {
    if (id >= 660 && id < 660 + kSuperweaponCount) return brUseSw(p, id - 660, -1, -1, -1);
    if (id == 665) {
        const int c = convictionOf(G, p);
        if (c < 0 || G.planets[size_t(c)].abilityExhausted) return false;
        Player& pl = G.players[size_t(p)];
        G.planets[size_t(c)].abilityExhausted = true;
        const int before = pl.commodities;
        pl.commodities = int8_t(std::min(commodityCap(G, pl), pl.commodities + 2));
        bkEvent(E, p, "S.O.L.G. Industries Group", pn(p) + " gains " + std::to_string(pl.commodities - before) + " commodities (S.O.L.G. Industries Group).",
                G.planets[size_t(c)].system);
        siphon(p, pl.commodities - before);
        return true;
    }
    if (id == 666) return brSynchrony(p);
    if (id == 667) return brTrinity(p, -1);
    if (id == 668) return brArmageddonUse(p, -1, -1, -1);
    return false;
}

// "At any time" in a combat side fights: superweapon cards, Synchrony Matrix, Trinity Stockpile (the planet fought over)
void Rules::brAnyTime(int side, int att, int def, int sys, int planet) {
    if (side < 0 || side >= G.numPlayers || ((brNoAsk_ >> side) & 1)) return;
    const int opp = side == att ? def : att;
    for (int guard = 0; guard < 12; ++guard) {
        std::vector<Option> o;
        for (int k = 0; k < kSuperweaponCount; ++k)
            if (brSwUsable(side, k, att, def, sys)) o.push_back({660 + k, std::string("Superweapon: ") + kSuperweapons[k].name, kSuperweapons[k].text});
        brArmageddonCombatOptions(side, att, def, sys, o);  // 668
        if (techReady(side, "dsbelky")) {
            const Player& pl = G.players[size_t(side)];
            if (pl.swCards & pl.swExhausted) o.push_back({666, "Synchrony Matrix", "Ready an exhausted superweapon card (or another component)."});
        }
        if (planet >= 0 && brTrinityReady(side)) {
            const std::vector<int> tp = brTrinityPlanets(side);
            if (std::find(tp.begin(), tp.end(), planet) != tp.end())
                o.push_back({667, "Breakthrough: Trinity Stockpile", "Destroy every unit on " + planetName(planet) + " - yours too."});
        }
        if (o.empty()) return;
        o.push_back({-1, "Fight on", "Nothing more now."});
        if (G.players[size_t(side)].human) o.push_back({-2, "Not during this combat", "Don't ask again until this combat is over."});
        curFor_ = opp;
        curValue_ = sys;
        const int c = fpick(side, Ab::BrSuperweapon, "Use a superweapon or card before this combat round?", o, "At any time, as the cards say.");
        curFor_ = -1;
        curValue_ = 0;
        if (c == -2) brNoAsk_ = uint16_t(brNoAsk_ | (1u << side));
        if (c < 0) return;
        if (c >= 660 && c < 660 + kSuperweaponCount) brUseSw(side, c - 660, att, def, sys);
        else if (c == 666) brSynchrony(side);
        else if (c == 667) brTrinity(side, planet);
        else if (c == 668) brArmageddonUse(side, att, def, sys);
        const bool fighting = planet < 0 ? hasShips(G, att, sys) && hasShips(G, def, sys)
                                         : !G.planets[size_t(planet)].units[size_t(att)].empty() && !G.planets[size_t(planet)].units[size_t(def)].empty();
        if (!fighting) return;
    }
}

// ================================================================== combat
// Spike (Belkosea hero), Mercenary Sortie (note) - "At the start of combat"
void Rules::brCombatStart(int att, int def, int sys, int planet) {
    if (planet >= 0) pharGroundStart(att, def, planet);  // (0.85.0) Avhkan (Pharad'n commander)
    brSpike_ = 0;
    brNoAsk_ = 0;
    brSortie_ = -1;
    const bool space = planet < 0;
    for (int side : {att, def}) {
        if (side < 0 || side >= G.numPlayers) continue;
        const int opp = side == att ? def : att;
        // Spike: "At the start of combat: You may exhaust this card; if you do, during this combat, hits produced by your fighters'
        // ANTI-FIGHTER BARRAGE are instead produced against all ships (not just fighters)."
        if (space && fac(side, "belkosea") && heroUnlocked(G, side)) {
            const Force& f = G.systems[size_t(sys)].space[size_t(side)];
            if (f.count(UnitType::Fighter) > 0 && unitDef(G, side, UnitType::Fighter).afb > 0 && opp >= 0 &&
                G.systems[size_t(sys)].space[size_t(opp)].ships() > 0) {
                curFor_ = opp;
                curValue_ = sys;
                const bool use = fyes(side, Ab::BrSpike, "Exhaust Spike: your fighters' anti-fighter barrage hits any ship this combat?",
                                      "Spike is readied after you lose a combat.");
                curFor_ = -1;
                curValue_ = 0;
                if (use) {
                    for (LeaderState& ls : G.players[size_t(side)].leaders)
                        if (kLeaders[ls.leader].type == kHero && ls.status == kReady) ls.status = kExhausted;
                    brSpike_ = uint16_t(brSpike_ | (1u << side));
                    bkEvent(E, side, "Spike", pn(side) + "'s ace leads the fighters: their barrage hits any ship this combat (Spike).", sys);
                }
            }
        }
    }
    // Mercenary Sortie: its holder (not the Belkosea) in this combat
    const int bk = playerOfFaction(G, "belkosea");
    if (bk < 0 || !brOn()) return;
    for (int side : {att, def}) {
        if (side < 0 || side >= G.numPlayers || side == bk || !holdsNote(G, side, bk, kNoteFaction) || brSortie_ >= 0) continue;
        const int opp = side == att ? def : att;
        auto targets = [&]() {
            if (opp < 0) return 0;
            if (space) return G.systems[size_t(sys)].space[size_t(opp)].count(UnitType::Fighter);
            return G.planets[size_t(planet)].units[size_t(opp)].count(UnitType::Infantry) + G.planets[size_t(planet)].units[size_t(opp)].count(UnitType::Fighter);
        };
        if (targets() <= 0) continue;
        curFor_ = opp;
        curValue_ = sys;
        const bool use = fyes(side, Ab::BrSortie, "Play " + pn(bk) + "'s Mercenary Sortie?",
                              "Roll 3 dice: each 8 or more destroys 1 of your opponent's fighters or infantry in this combat. The note goes back to " +
                                  pn(bk) + " after the first round.");
        curFor_ = -1;
        curValue_ = 0;
        if (!use) continue;
        brSortie_ = side;
        bkEvent(E, side, "Mercenary Sortie", pn(side) + " calls in " + pn(bk) + "'s mercenaries (Mercenary Sortie).", sys);
        RollResult r;
        addDice(r, 3, 8);
        reportRoll(side, sys, "Mercenary Sortie", r);
        for (int h = 0; h < r.hits && targets() > 0; ++h) {
            UnitType t = UnitType::Fighter;
            if (!space) {
                const Force& f = G.planets[size_t(planet)].units[size_t(opp)];
                const int inf = f.count(UnitType::Infantry), ftr = f.count(UnitType::Fighter);
                t = inf > 0 ? UnitType::Infantry : UnitType::Fighter;
                if (inf > 0 && ftr > 0) {
                    curFor_ = opp;
                    t = UnitType(fpick(side, Ab::BrSortieTarget, "Mercenary Sortie: destroy which unit?",
                                       {{int(UnitType::Infantry), "Infantry", std::to_string(inf) + " here"},
                                        {int(UnitType::Fighter), "Fighter", std::to_string(ftr) + " here"}}));
                    curFor_ = -1;
                }
            }
            destroyUnits(opp, t, 1, sys, planet, side, "Mercenary Sortie");
        }
    }
}

// Mercenary Sortie: "After the first round of this combat, return this card to the Belkosea player."
void Rules::brSortieBack() {
    if (brSortie_ < 0) return;
    const int bk = playerOfFaction(G, "belkosea");
    if (bk >= 0 && holdsNote(G, brSortie_, bk, kNoteFaction)) returnNote(brSortie_, makeNote(bk, kNoteFaction));
    brSortie_ = -1;
}

// The start of each combat round: "at any time" cards, Ascendancy (flagship), Ciphus Gault (commander)
void Rules::brCombatRoundStart(int att, int def, int sys, int planet, int round, RollMods* mods) {
    const bool space = planet < 0;
    if (round == 2) brSortieBack();
    for (int side : {att, def}) {
        if (side < 0 || side >= G.numPlayers || G.players[size_t(side)].eliminated) continue;
        const int opp = side == att ? def : att;
        brAnyTime(side, att, def, sys, planet);
        if (opp < 0) continue;
        SystemState& s = G.systems[size_t(sys)];
        // Ascendancy (Belkosea flagship): "During combat, you may have this ship become damaged to produce 1 hit against another player's
        // non-fighter ships in this system."
        if (space && flagshipAbility(G, side, "belkosea") && flagshipIn(G, side, sys) && s.space[size_t(opp)].nonFighterShips() > 0) {
            Force& f = s.space[size_t(side)];
            const int t = int(UnitType::Flagship);
            if (f.n[t] > f.dmg[t] && unitSustainsAt(G, side, UnitType::Flagship, sys, -1)) {
                curFor_ = opp;
                curValue_ = sys;
                const bool use = fyes(side, Ab::BrAscendancy, "Ascendancy: let your flagship become damaged to make 1 hit on " + pn(opp) + "'s non-fighter ships?",
                                      "The hit goes to a non-fighter ship; your flagship can't sustain damage again until it is repaired.");
                curFor_ = -1;
                curValue_ = 0;
                if (use) {
                    f.dmg[t] = uint8_t(f.dmg[t] + 1);
                    bkEvent(E, side, "Ascendancy", pn(side) + "'s flagship takes damage to strike (Ascendancy).", sys);
                    assignHits(opp, sys, -1, 1, kNonFighter, "Ascendancy", side);
                }
            }
        }
        // Ciphus Gault (Belkosea commander): "When one of your units would make a combat roll: You may use that unit's ANTI-FIGHTER
        // BARRAGE instead. You cannot use this ability during the first round of space combat."
        if (mods && space && round >= 2 && commanderUsable(G, side, "belkosea")) {
            int mask = 0;
            for (int t = 0; t < kUnitTypes; ++t)
                if (isShip(UnitType(t)) && s.space[size_t(side)].n[t] && unitDef(G, side, UnitType(t)).afb > 0) mask |= 1 << t;
            const bool spike = (brSpike_ >> side) & 1;
            if (mask && (s.space[size_t(opp)].count(UnitType::Fighter) > 0 || (spike && (mask & (1 << int(UnitType::Fighter)))))) {
                curFor_ = opp;
                curValue_ = sys;
                const bool use = fyes(side, Ab::BrCiphus, "Ciphus Gault: your units with anti-fighter barrage roll it instead of their combat rolls this round?",
                                      spike ? "Your fighters' barrage hits any ship (Spike); the others' hit fighters only."
                                            : "Barrage hits can only destroy fighters.");
                curFor_ = -1;
                curValue_ = 0;
                if (use) {
                    mods[side == att ? 0 : 1].useAfb = mask;
                    bkEvent(E, side, "Ciphus Gault", pn(side) + "'s units fire anti-fighter barrage this round (Ciphus Gault).", sys);
                }
            }
        }
    }
}

// Ciphus Gault: the hits of the dice that rolled ANTI-FIGHTER BARRAGE instead of a combat roll - fighters only (Spike: a fighter's hit
// any ship)
int Rules::brAfbOnlyHits(int shooter, const RollResult& r, const RollMods& m) const {
    if (!m.useAfb) return 0;
    const bool spike = shooter >= 0 && shooter < kSides && ((brSpike_ >> shooter) & 1);
    int n = 0;
    for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i) {
        const int t = r.unit[i];
        if (t < 0 || !((m.useAfb >> t) & 1) || r.dice[i] < r.need[i]) continue;
        if (spike && t == int(UnitType::Fighter)) continue;
        ++n;
    }
    return n;
}
// Spike: the hits of the fighters' dice in an anti-fighter barrage (they go to any ship)
int Rules::brSpikeHits(int side, const RollResult& r) const {
    if (side < 0 || side >= kSides || !((brSpike_ >> side) & 1)) return 0;
    int n = 0;
    for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i)
        if (r.unit[i] == int(UnitType::Fighter) && r.dice[i] >= r.need[i]) ++n;
    return n;
}
bool Rules::brSpikeOn(int side) const { return side >= 0 && side < kSides && ((brSpike_ >> side) & 1); }

// The end of a combat: Spike readies after a loss ("After you lose a combat, ready this card."); Mercenary Sortie goes back
void Rules::brCombatEnd(int att, int def, int sys, int planet) {
    brSortieBack();
    for (int side : {att, def}) {
        if (side < 0 || side >= G.numPlayers || !fac(side, "belkosea")) continue;
        const int opp = side == att ? def : att;
        auto left = [&](int q) {
            if (q < 0) return false;
            if (planet < 0) return G.systems[size_t(sys)].space[size_t(q)].ships() > 0;
            return groundForcesOn(G, q, planet) > 0;
        };
        if (left(side) || !left(opp)) continue;  // (a draw is no loss)
        for (LeaderState& ls : G.players[size_t(side)].leaders)
            if (kLeaders[ls.leader].type == kHero && ls.status == kExhausted) {
                ls.status = kReady;
                bkEvent(E, side, "Spike", pn(side) + "'s ace is back in the cockpit (Spike).", sys);
            }
    }
    brSpike_ = 0;
    brNoAsk_ = 0;
}

// Uzean Wardog (Belkosea mech): "When this unit produces a hit, gain 1 commodity or convert 1 of your commodities to a trade good."
void Rules::brWardog(int p, const RollResult& r, int sys) {
    if (!brOn() || p < 0 || p >= G.numPlayers || !mechAbilities(G) || std::strcmp(unitDef(G, p, UnitType::Mech).key, "belkosea_mech")) return;
    int hits = 0;
    for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i)
        if (r.unit[i] == int(UnitType::Mech) && r.dice[i] >= r.need[i]) ++hits;
    if (!hits) return;
    Player& pl = G.players[size_t(p)];
    const int cap = commodityCap(G, pl);
    int convert = 0;
    const int most = std::min(hits, int(pl.commodities));
    if (most > 0) {
        std::vector<Option> o;
        for (int c = 0; c <= most; ++c) {
            const int gain = std::min(hits - c, std::max(0, cap - (pl.commodities - c)));
            o.push_back({c, c == 0 ? "Gain commodities" : "Convert " + std::to_string(c), std::to_string(gain) + " commodities gained, " + std::to_string(c) + " turned into trade goods"});
        }
        convert = fpick(p, Ab::BrWardog, "Uzean Wardog: " + std::to_string(hits) + (hits == 1 ? " hit" : " hits") + " - gain commodities or convert them?", o,
                        "For each hit: gain 1 commodity, or convert 1 of your commodities to a trade good.");
        convert = std::max(0, std::min(convert, most));
    }
    pl.commodities = int8_t(pl.commodities - convert);
    if (convert) gainTGNotSupply(p, convert);
    const int before = pl.commodities;
    pl.commodities = int8_t(std::min(cap, pl.commodities + (hits - convert)));
    if (pl.commodities > before) siphon(p, pl.commodities - before);
    bkEvent(E, p, "Uzean Wardog",
            pn(p) + "'s Wardogs: " + std::to_string(pl.commodities - before) + " commodities gained, " + std::to_string(convert) + " turned into trade goods (Uzean Wardog).",
            sys);
}

// Wyvern I / II: "You may commit this unit to a planet as if it were a ground force. When combat ends, return it to the space area."
bool Rules::wyvernsLand(int p, int sys) const {
    return brOn() && p >= 0 && p < G.numPlayers && fac(p, "belkosea") && sys == G.activeSystem && p == G.activePlayer;
}

// ================================================================== Razalka Gris (agent)
// "After a system is activated: You may exhaust this card to choose 1 system; until the end of movement, other players' ships in that
// system do not affect ships' movement."
void Rules::brActivated(int p, int sys) {
    if (!brOn() || G.fx.razalkaSys >= 0) return;
    const int user = agentUser(p, "belkosea");
    if (user < 0) return;
    std::vector<Option> o;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (int(s) == sys || G.systems[s].offBoard) continue;
        int others = 0;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p) others += G.systems[s].space[size_t(q)].ships();
        if (others <= 0 || hexDistance(G.systems[s].pos, G.systems[size_t(sys)].pos) > 3) continue;
        Option x{int(s), sysName(int(s)), std::to_string(others) + " of other players' ships"};
        x.at = int(s);
        o.push_back(x);
    }
    if (o.empty()) return;
    o.push_back({-1, "Don't", ""});
    curFor_ = p;
    curValue_ = sys;
    const int c = fpick(user, Ab::BrRazalka,
                        user == p ? std::string("Razalka Gris: move through other players' ships in which system?")
                                  : "Razalka Gris: let " + pn(p) + "'s ships move through other players' ships in which system?",
                        o, "Exhaust Razalka Gris: until the end of movement, other players' ships there don't block movement.", true);
    curFor_ = -1;
    curValue_ = 0;
    if (c < 0) return;
    spendAgent(user, "belkosea");
    agentDone(user, p);
    G.fx.razalkaSys = int16_t(c);
    bkEvent(E, user, "Razalka Gris", pn(p) + "'s ships may pass the ships in " + sysName(c) + " this movement (Razalka Gris).", c);
}

}  // namespace ti

namespace ti {

// ================================================================== Armageddon Project (Belkosea monument, Monuments+)
// "At the end of the strategy phase, choose and place 1 control token on a superweapon card in a play area. This card has the text
// abilities of each superweapon card in a play area that has 1 of your control tokens." - Player::swArm (bit = superweapon card);
// the static abilities in query_br.cpp (armageddonAt: "this system" = the monument's), the "exhaust this card" ones through the
// monument card (brUseSw mon).
bool Rules::brArmageddonHas(int p, int k, int* home) const {
    if (!brOn() || !monumentsOn() || p < 0 || p >= G.numPlayers || k < 0 || k >= kSuperweaponCount) return false;
    if (!((G.players[size_t(p)].swArm >> k) & 1) || !monCardReady(G, p)) return false;
    int sys = -1;
    if (!monAbilityAt(G, p, "belkosea", &sys) || superweaponPlanet(G, k) < 0) return false;  // (the card is still in a play area)
    if (home) *home = sys;
    return true;
}
void Rules::brArmageddonLost(int k) {  // the superweapon card was purged: the control tokens on it go back
    for (Player& pl : G.players) pl.swArm = uint8_t(pl.swArm & ~(1u << k));
}
void Rules::brArmageddonOptions(int p, std::vector<Option>& o) {
    for (int k = 0; k < kSuperweaponCount; ++k)
        if (brSwUsable(p, k, -1, -1, -1, true)) {
            o.push_back({668, "Monument: " + monName(monumentIndex("belkosea")),
                         "Exhaust it to use the ability of a superweapon card with your control token. Doesn't use your action."});
            return;
        }
}
void Rules::brArmageddonCombatOptions(int p, int att, int def, int sys, std::vector<Option>& o) {
    for (int k = 0; k < kSuperweaponCount; ++k)
        if (brSwUsable(p, k, att, def, sys, true)) {
            o.push_back({668, "Monument: " + monName(monumentIndex("belkosea")), "Exhaust it to use the ability of a superweapon card with your control token."});
            return;
        }
}
bool Rules::brArmageddonUse(int p, int att, int def, int sys) {
    std::vector<Option> o;
    for (int k = 0; k < kSuperweaponCount; ++k)
        if (brSwUsable(p, k, att, def, sys, true)) o.push_back({k, kSuperweapons[k].name, kSuperweapons[k].text});
    if (o.empty()) return false;
    o.push_back({-1, "Cancel", ""});
    const int card = monumentIndex("belkosea");
    curFor_ = att >= 0 ? (p == att ? def : att) : -1;
    curValue_ = sys;
    const int k = mpick(p, card, kMonAskBelkoseaUse, monName(card) + ": use which superweapon card's ability?", o, kMonuments[card].text);
    curFor_ = -1;
    curValue_ = 0;
    if (k < 0) return false;
    return brUseSw(p, k, att, def, sys, true);
}
void Rules::brArmageddonStrategyEnd() {
    if (!brOn() || !monumentsOn()) return;
    const int card = monumentIndex("belkosea");
    if (card < 0) return;
    for (int q : initiativeOrder(G)) {
        int sys = -1;
        if (G.players[size_t(q)].eliminated || !monAbilityAt(G, q, "belkosea", &sys)) continue;
        std::vector<Option> o;
        for (int k = 0; k < kSuperweaponCount; ++k) {
            const int x = superweaponPlanet(G, k);
            if (x < 0 || ((G.players[size_t(q)].swArm >> k) & 1)) continue;
            o.push_back({k, kSuperweapons[k].name, std::string("In ") + pn(G.planets[size_t(x)].owner) + "'s play area. " + kSuperweapons[k].text});
        }
        if (o.empty()) continue;
        const int k = o.size() == 1 ? o[0].id
                                    : mpick(q, card, kMonAskBelkosea, monName(card) + ": put a control token on which superweapon card?", o, kMonuments[card].text);
        if (k < 0) continue;
        G.players[size_t(q)].swArm = uint8_t(G.players[size_t(q)].swArm | (1u << k));
        monEvent(q, card, pn(q) + " puts a control token on " + swName(k) + " (" + monName(card) + ").", sys);
    }
}

}  // namespace ti
