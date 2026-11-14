// Shared helpers for the rules: asking questions, paying costs, gaining
// victory points and tokens, adding/destroying units, elimination, plus the
// validators that check movement, production, landing and hit-assignment plans.
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>

#include "engine/rules.h"

namespace ti {

namespace {
std::string sysLabel(const Game& g, int s) {
    const SystemState& sy = g.systems[s];
    if (sy.home >= 0 && kSystems[sy.def].back != Back::Home)  // Ghoti Mobile Command: home is where the flagship is
        return std::string(kSystems[sy.def].name) + " (" + playerName(g, sy.home) + " home)";
    if (sy.home >= 0) return playerName(g, sy.home) + " home";
    return kSystems[sy.def].name;
}
}  // namespace

// ============================================================ validators
std::string checkPayment(const Game& g, int p, const Decision& d, const Answer& a) {
    if (a.number == -1 && a.picks.empty()) return "";  // declined (only offered when optional)
    const Player& pl = g.players[p];
    if (a.number < 0 || a.number > pl.tradeGoods) return "You don't have that many trade goods.";
    if (!d.allowTradeGoods && a.number > 0) return "Trade goods can't be used for this.";
    std::set<int> seen;
    const int worth = tgWorth(g, p);  // Mirror Computing
    int total = a.number * worth;
    for (int x : a.picks) {
        if (!seen.insert(x).second) return "A planet was chosen twice.";
        if (std::find(d.planets.begin(), d.planets.end(), x) == d.planets.end()) return "That planet can't be used.";
        total += planetPayValue(g, x, d.influence, d.mixed);
    }
    if (total < d.amount) return "Not enough: " + std::to_string(total) + " of " + std::to_string(d.amount) + ".";
    if (d.amount2 > 0) {
        // what's left must still cover the influence part
        int left = (pl.tradeGoods - a.number) * worth;
        for (int x : d.planets)
            if (!seen.count(x)) left += planetInfluence(g, x);
        if (left < d.amount2) return "Keep enough planets for the influence part as well.";
    }
    return "";
}

int maxAbsorbableHits(const Game& g, int p, int sys, int planet, int mask, bool noSustain) {
    const Force& f = planet < 0 ? g.systems[sys].space[p] : g.planets[planet].units[p];
    int n = 0;
    int metali = 0;  // ships without Sustain Damage that may use it here (Metali Void Shielding, Splintering Gale)
    for (int t = 0; t < kUnitTypes; ++t) {
        if (mask && !((mask >> t) & 1)) continue;
        n += f.n[t];
        if (!noSustain && unitSustainsAt(g, p, UnitType(t), sys, planet)) n += (f.n[t] - f.dmg[t]) * sustainWorth(g, p);
        else if (!noSustain && f.n[t] > f.dmg[t] && metaliSustain(g, p, UnitType(t), sys, planet)) metali += f.n[t] - f.dmg[t];
    }
    return n + std::min(metali, extraSustains(g, p, sys, planet)) * sustainWorth(g, p);
}

std::string checkHits(const Game& g, int p, int sys, int planet, int hits, int mask, const std::vector<int>& data,
                      bool noSustain) {
    const Force& f = planet < 0 ? g.systems[sys].space[p] : g.planets[planet].units[p];
    if (data.size() % 2) return "Bad answer.";
    int destroy[kUnitTypes] = {}, sustain[kUnitTypes] = {};
    for (size_t i = 0; i < data.size(); i += 2) {
        int t = data[i], mode = data[i + 1];
        if (t < 0 || t >= kUnitTypes) return "Bad unit.";
        if (mask && !((mask >> t) & 1)) return std::string(unitTypeName(UnitType(t), true)) + " can't be chosen here.";
        if (mode == 1) {
            if (noSustain) return "Sustain Damage can't be used here.";
            ++sustain[t];
        } else {
            ++destroy[t];
        }
    }
    int metali = 0;  // Metali Void Shielding: one ship without Sustain Damage may use it
    for (int t = 0; t < kUnitTypes; ++t) {
        if (destroy[t] > f.n[t]) return "Not that many " + std::string(unitTypeName(UnitType(t), true)) + ".";
        if (sustain[t]) {
            if (!unitSustainsAt(g, p, UnitType(t), sys, planet)) {
                if (!metaliSustain(g, p, UnitType(t), sys, planet))
                    return std::string(unitTypeName(UnitType(t), true)) + " can't sustain damage here.";
                metali += sustain[t];
            }
            if (sustain[t] > f.n[t] - f.dmg[t]) return "Those units are already damaged.";
        }
    }
    if (metali > extraSustains(g, p, sys, planet))
        return extraSustains(g, p, sys, planet) == 1 ? "Only one ship without Sustain Damage may sustain damage here."
                                                     : "Only " + std::to_string(extraSustains(g, p, sys, planet)) + " ships without Sustain Damage may sustain damage here.";
    int need = std::min(hits, maxAbsorbableHits(g, p, sys, planet, mask, noSustain));
    // Each destroyed unit takes 1 hit, each Sustain Damage cancels 1 (2 with Non-Euclidean Shielding).
    const int worth = sustainWorth(g, p);
    int absorbed = 0, sustains = 0;
    for (size_t i = 0; i < data.size(); i += 2) {
        absorbed += data[i + 1] == 1 ? worth : 1;
        sustains += data[i + 1] == 1 ? 1 : 0;
    }
    int smallest = (int(data.size()) / 2 - sustains) > 0 ? 1 : worth;
    if (absorbed < need || (!data.empty() && absorbed - smallest >= need))
        return std::string(noSustain ? "Choose exactly " : "Assign exactly ") + std::to_string(need) +
               (noSustain ? " unit(s)." : " hit(s)") + (worth > 1 && !noSustain ? " (Sustain Damage cancels 2)." : ".");
    return "";
}

std::string checkLandPlan(const Game& g, int p, int sys, const std::vector<int>& data, const LandSpec& spec) {
    if (data.size() % 3) return "Bad answer.";
    const SystemState& s = g.systems[sys];
    const int mask = spec.typeMask ? spec.typeMask : ((1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech)));
    int used[kUnitTypes] = {};
    int onMust = 0;
    std::map<int, std::pair<int, int>> perPlanet;  // planet -> (ground forces, fighters) landing (Matriarch)
    for (size_t i = 0; i < data.size(); i += 3) {
        int pl = data[i], t = data[i + 1], k = data[i + 2];
        if (std::find(s.planets.begin(), s.planets.end(), pl) == s.planets.end()) return "That planet isn't here.";
        if (!spec.targets.empty() && std::find(spec.targets.begin(), spec.targets.end(), pl) == spec.targets.end())
            return std::string(kPlanets[g.planets[pl].def].name) + " can't be chosen.";
        if (!spec.bombard && isDMZ(g, pl))
            return std::string(kPlanets[g.planets[pl].def].name) + " is a Demilitarized Zone: no units can land there.";
        if (!spec.bombard && t == int(UnitType::Infantry) && arcaneShield(g, pl))
            return std::string(kPlanets[g.planets[pl].def].name) + " has the Arcane Shield Emitter: no infantry can land there.";
        if (t < 0 || t >= kUnitTypes || !((mask >> t) & 1))
            return spec.bombard ? "Only units with Bombardment can fire." : "Only ground forces can land.";
        if (k <= 0) return "Bad count.";
        if (!spec.bombard && s.custodians && !g.custodiansRemoved && !std::strcmp(kPlanets[g.planets[pl].def].key, "mr"))
            return "Remove the custodians token before landing on Mecatol Rex.";
        used[t] += k;
        if (pl == spec.mustLand) onMust += k;
        if (t == int(UnitType::Fighter)) perPlanet[pl].second += k;
        else perPlanet[pl].first += k;
    }
    for (int t = 0; t < kUnitTypes; ++t) {
        const int have = spec.bombard ? bombardAvailable(g, p, sys, UnitType(t)) : s.space[p].n[t];
        if (used[t] > have)
            return spec.bombard ? "You don't have that many of those units here."
                                : "You don't have that many ground forces in space here.";
    }
    // (fixes 27, the Naalu review - LRR FAQ "The Naalu Collective") fighters may be committed alone: the combat is a draw
    // when only they are left, and they return to space
    if (!spec.bombard && g.fixes < 27)
        for (auto& e : perPlanet)
            if (e.second.second > 0 && e.second.first == 0)
                return "Fighters can only land together with ground forces (Matriarch).";
    if (spec.mustLand >= 0 && onMust == 0)
        return "You removed the custodians token, so at least 1 ground force must land on " +
               std::string(kPlanets[g.planets[spec.mustLand].def].name) + ".";
    return "";
}

std::map<int, std::vector<int>> moveRoutes(const std::vector<int>& plan) {
    std::map<int, std::vector<int>> out;
    for (size_t i = 0; i + 3 < plan.size(); i += 4)
        if (plan[i] == kWaypointTuple) out[plan[i + 1]].push_back(plan[i + 2]);
    return out;
}

std::vector<int> moveUnitsOnly(const std::vector<int>& plan) {
    std::vector<int> out;
    for (size_t i = 0; i + 3 < plan.size(); i += 4)
        if (plan[i] != kWaypointTuple) out.insert(out.end(), plan.begin() + long(i), plan.begin() + long(i) + 4);
    return out;
}

std::string checkMovePlan(const Game& g, int p, int active, const std::vector<int>& plan, bool allowStrand, bool fleetStrict) {
    if (plan.size() % 4) return "Bad answer.";
    const int nsys = int(g.systems.size());
    // Waypoints (0.56.0): routes the player chose, checked here; the units part is checked as before
    const std::map<int, std::vector<int>> routes = moveRoutes(plan);
    const std::vector<int> data = routes.empty() ? plan : moveUnitsOnly(plan);
    for (const auto& rt : routes) {
        const int from = rt.first;
        if (from < 0 || from >= nsys || from == active) return "Bad route.";
        if (rt.second.size() > 12) return "That route is too long.";
        for (size_t k = 0; k < rt.second.size(); ++k) {
            const int st = rt.second[k];
            if (st < 0 || st >= nsys || st == from || st == active) return "Bad route.";
            if (std::find(rt.second.begin(), rt.second.begin() + long(k), st) != rt.second.begin() + long(k))
                return "A route goes through " + sysLabel(g, st) + " twice.";
        }
        bool ship = false;
        for (size_t i = 0; i + 3 < data.size(); i += 4)
            if (data[i] == from && data[i + 1] < 0 && data[i + 2] >= 0 && data[i + 2] < kUnitTypes && movesAsShip(g, p, UnitType(data[i + 2])))
                ship = true;
        if (!ship) return "A route is set for " + sysLabel(g, from) + ", but no ship moves from there.";
    }
    auto routeOf = [&](int from) -> const std::vector<int>* {
        auto it = routes.find(from);
        return it == routes.end() ? nullptr : &it->second;
    };
    // a route's systems, the start included (for the pick-ups on the way)
    auto onRoute = [&](int from, int sys) {
        const std::vector<int>* r = routeOf(from);
        return sys == from || (r && std::find(r->begin(), r->end(), sys) != r->end());
    };
    std::map<int, Force> takeSpace;    // per system: units taken from the space area
    std::map<int, int> takeFromPlanets;  // per system: ground forces taken from planets
    std::map<int, std::array<int, kUnitTypes>> perPlanet;
    std::map<int, Force> miniFrom;  // Miniaturization (Ral Nel): structures carried from these systems / planets
    std::map<int, Force> miniPlanet;
    const bool ralnel = isFaction(g, p, "ralnel");
    for (size_t i = 0; i < data.size(); i += 4) {
        int from = data[i], loc = data[i + 1], t = data[i + 2], k = data[i + 3];
        if (from < 0 || from >= nsys) return "Bad system.";
        // LRR 95.1: moving ships may pick up ground forces from the active system's planets too (to land on
        // another planet there - 0.30.0, owner); its space area and ships don't move
        if (from == active && loc < 0) return "Units already in the active system don't move.";
        // (owner 2026-11-11: not the pick-up from the active system's planets - its token is this activation's; that
        // pick-up was always refused since 0.30.0. Refused answers are never saved, so old games replay unchanged)
        if (g.systems[from].hasToken(p) && !(from == active && loc >= 0) && !g.fx.dominusOrb && !(heroActive(g, p, "nomad") && flagshipIn(g, p, from)))
            return "Units can't leave a system that has your command token.";
        if (t < 0 || t >= kUnitTypes || k <= 0) return "Bad units.";
        UnitType ut = UnitType(t);
        // Floating factories (Saar) move as if they were ships; other structures never move - except the Ral Nel's
        // (Miniaturization: any ship carries them, no capacity needed)
        if (isStructure(ut) && ralnel && !movesAsShip(g, p, ut)) {
            Force& f = loc < 0 ? miniFrom[from] : miniPlanet[loc];
            f.add(ut, k);
            const Force& have = loc < 0 ? g.systems[from].space[p] : g.planets[loc].units[p];
            if (f.n[t] > have.n[t]) return "Not that many structures there.";
            if (loc >= 0) miniFrom[from].add(ut, 0);  // (marks the system)
            continue;
        }
        if (isStructure(ut) && !(loc < 0 && movesAsShip(g, p, ut))) return "Structures can't move.";
        if (loc < 0) {
            Force& f = takeSpace[from];
            f.add(ut, k);
            if (f.n[t] > g.systems[from].space[p].n[t]) return "Not that many units there.";
        } else {
            const auto& pls = g.systems[from].planets;
            if (std::find(pls.begin(), pls.end(), loc) == pls.end()) return "Bad planet.";
            if (!isGroundForce(ut)) return "Only ground forces can be picked up from planets.";
            auto& arr = perPlanet[loc];
            arr[t] += k;
            if (arr[t] > g.planets[loc].units[p].n[t]) return "Not that many units on that planet.";
            takeFromPlanets[from] += k;
        }
    }
    const bool gd = moveBoost(g, p);  // Gravity Drive / Ionian Fuel Refinery
    const bool v13 = g.fixes >= 13;    // (0.44.9: boosts stack on one ship - owner's answer P6; detours use them - P8)
    const int bmax = moveBonusMax(g, p);
    struct MovingScope {  // Gravleash Maneuvers (0.44.4): the ships moving out of each system, for moveValue
        explicit MovingScope(const std::map<int, Force>* m) { g_movingShips = m; }
        ~MovingScope() { g_movingShips = nullptr; }
    } movingScope(&takeSpace);
    struct Mover {
        int sys;
        UnitType t;
        int k;
        int need = 0;  // (0.44.9) +move each of these ships already uses
    };
    std::vector<Mover> carriers;  // moving ships with capacity
    std::set<int> sources;        // systems that provide transported units
    Force arriving;
    int capacity = 0, carried = 0, bonusUsed = 0, mendosaUsed = 0;
    int fightersCarried = 0;  // (0.44.9) Eye of the Curator fighters carried (half a slot each)
    const bool lightning = lightningDrives(g, p);  // Lightning Drives (Gledge): +1 for ships that carry no fighters / infantry
    int mechOnly = 0;                              // capacity of the ships that use it (mechs only)
    bool raidUsed = false;                         // Raid Leaders (Discordant Stars): one ship's capacity 2
    std::map<int, int> vogulUsed;  // Eye of Vogul: system * 16 + type -> ships moved that way
    const int topMove = g.fx.mendosa && p == g.activePlayer ? maxShipMove(g) : 0;  // Captain Mendosa
    // 0.44.2 (fixes >= 7, tester: "with Fighter II the game always assumes they fly on their own - I cannot carry them"):
    // fighters that reach the system only with Gravity Drive's +1 are carried when a moving ship can take them, instead
    // of each using up the +1 (which the carrier needed)
    struct BoostFighters {
        int sys, k;
        int need = 1;  // (0.44.9) +move each needs
    };
    std::vector<BoostFighters> boostFighters;
    // Hil Colish (fixes 24): ships that reach the active system only through its delta wormhole there - it must move too
    const int colishO = colishOrigin(g, p);
    bool needsColish = false;
    for (auto& it : takeSpace) {
        const int sys = it.first;
        const Force& take = it.second;
        for (int t = 0; t < kUnitTypes; ++t) {
            int k = take.n[t];
            if (!k) continue;
            UnitType ut = UnitType(t);
            arriving.add(ut, k);
            if (isGroundForce(ut) && !(ut == UnitType::Mech && eidolonShip(g, p))) {  // (Eidolon Maximum moves on its own, 0.44.4)
                carried += k;
                sources.insert(sys);
                continue;
            }
            const std::vector<int>* route = routeOf(sys);
            std::string routeWhy;
            Reach r = route ? shipReachVia(g, p, sys, *route, active, ut, bmax, &routeWhy) : shipReach(g, p, sys, active, ut, bmax);
            if (!r.ok && topMove > 0 && ut != UnitType::Fighter) {
                int mv = moveValue(g, p, sys, ut);
                if (mv > 0 && mv < topMove) {
                    r = route ? shipReachVia(g, p, sys, *route, active, ut, topMove - mv + bmax) : shipReach(g, p, sys, active, ut, topMove - mv + bmax);
                    if (r.ok) {
                        mendosaUsed += k;
                        r.needsBonus = false;
                    }
                }
            }
            if (r.ok && r.colish && colishO >= 0 && colishO != active && !(isFlagship(ut) && sys == colishO)) needsColish = true;
            if (!r.ok) {
                if (ut == UnitType::Fighter) {  // fighters without enough move are carried
                    if (curatorHalf(g, p)) fightersCarried += k;  // (0.44.9: two to a slot - Stabilization Arrays)
                    else carried += k;
                    sources.insert(sys);
                    continue;
                }
                if (route) return std::string(unitTypeName(ut, true)) + " in " + sysLabel(g, sys) + " can't follow that route: " + routeWhy;
                return std::string(unitTypeName(ut, true)) + " in " + sysLabel(g, sys) + " can't reach the active system.";
            }
            const int need = r.needsBonus ? (v13 ? std::max(1, int(r.bonus)) : 1) : 0;  // +move per ship
            if (r.needsBonus && ut == UnitType::Fighter && g.fixes >= 7) {
                boostFighters.push_back({sys, k, need});
                continue;
            }
            int boosted = 0;  // ships of this group moving 1 further with Lightning Drives
            if (r.needsBonus) {
                if (lightning && ut != UnitType::Fighter && g.fixes >= 11 && unitDef(g, p, ut).capacity <= 0) {
                    boosted = k;  // (0.44.7: ships that carry nothing take Lightning Drives - Gravity Drive stays for a carrier)
                    bonusUsed += k * (need - 1);  // (0.44.9: anything beyond Lightning Drives' +1 from the other boosts)
                } else if (lightning && ut != UnitType::Fighter) {
                    if (v13) {  // each ship that takes Lightning Drives (and carries nothing) needs 1 boost less
                        const int left = std::max(0, boostShips(g, p) - bonusUsed), total = k * need;
                        boosted = std::max(0, std::min(k, total - left));
                        bonusUsed += total - boosted;
                    } else {
                        const int viaGd = std::min(k, std::max(0, boostShips(g, p) - bonusUsed));
                        bonusUsed += viaGd;
                        boosted = k - viaGd;
                    }
                } else {
                    bonusUsed += k * need;
                }
            }
            // Rift Engines (Axis, 0.43.0 D113): the +2 belongs to the one ship that went through the rift
            if (k > 1 && g.fixes >= 2 && g.fx.dsRiftOwner == p && t == g.fx.dsRiftType && sys == g.fx.dsRiftSys && !route &&
                !shipReachNoRift(g, p, sys, active, ut, bmax).ok)
                return std::string("Rift Engines gives +2 to one ship only: move just 1 ") + unitTypeName(ut) + " from " + sysLabel(g, sys) +
                       " (the others can't reach the active system).";
            // Wraith Engine (Li-Zho, 0.43.0 D127): likewise only the one chosen ship counts as next to the active system
            if (k > 1 && g.fixes >= 2 && g.fx.dsWraithOwner == p && t == g.fx.dsWraithType && sys == g.fx.dsWraithSys && !route &&
                !shipReachNoRift(g, p, sys, active, ut, bmax).ok)
                return std::string("The Wraith Engine moves one ship only: move just 1 ") + unitTypeName(ut) + " from " + sysLabel(g, sys) +
                       " (the others can't reach the active system).";
            if (r.vogul) {
                vogulUsed[sys * 16 + t] += k;
                if (vogulUsed[sys * 16 + t] > vogulShips(g, p, sys, ut))
                    return std::string("The Eye of Vogul only brings the ships picked for it from ") + sysLabel(g, sys) + ".";
            }
            int cap = unitDef(g, p, ut).capacity;
            int extra = 0;
            if (g.modules & kModDiscordant) {
                // Raid Leaders: the chosen ship carries 2
                if (g.fx.dsRaidOwner == p && int(ut) == g.fx.dsRaidType && !raidUsed) {
                    extra += std::max(0, 2 - cap);
                    raidUsed = true;
                }
                // The Lady (Ghemina): every flagship leaving her system - she is among them, with 3 instead of 7
                if (ut == UnitType::Flagship && sys == g.dsLadySys && g.fixes < 16 && k >= g.systems[sys].space[p].count(UnitType::Flagship) &&
                    isFaction(g, p, "ghemina"))
                    extra -= 4;
            }
            // Myru Vos (0.43.0 T30): a ship that only gets here through other players' ships carries nothing
            if ((cap > 0 || extra > 0) && g.fx.myruThrough == p && !shipReachNoMyru(g, p, sys, active, ut, bmax).ok) cap = extra = 0;
            if (cap > 0 || extra > 0) {
                capacity += (k - boosted) * cap + extra;
                mechOnly += boosted * cap;
                carriers.push_back({sys, ut, k, need});
            }
        }
    }
    // Ahk-Syl Siven (Nomad hero): only the flagship and what it carries leave systems with your command tokens
    if (heroActive(g, p, "nomad") && !g.fx.dominusOrb) {
        std::map<int, int> cargoFrom;
        for (size_t i = 0; i < data.size(); i += 4) {
            const int from = data[i], t = data[i + 2], k = data[i + 3];
            if (!g.systems[from].hasToken(p)) continue;
            if (UnitType(t) == UnitType::Flagship) continue;
            if (movesAsShip(g, p, UnitType(t)) && UnitType(t) != UnitType::Fighter) return "Only your flagship can leave a system with your command token (Ahk-Syl Siven).";
            cargoFrom[from] += k;
        }
        for (auto& c : cargoFrom) {
            auto it = takeSpace.find(c.first);
            if (it == takeSpace.end() || !it->second.count(UnitType::Flagship))
                return "Units leave a system with your command token only with your flagship (Ahk-Syl Siven).";
            if (c.second > unitDef(g, p, UnitType::Flagship).capacity) return "Your flagship can't carry that many (Ahk-Syl Siven).";
        }
    }
    // Aerie Sentinel (Argent mech): needs no capacity while transported
    const bool aerie = isFaction(g, p, "argent") && mechAbilities(g);
    if (aerie) carried -= arriving.count(UnitType::Mech);
    for (auto& it : takeFromPlanets) {
        carried += it.second;
        sources.insert(it.first);
        for (auto& pp : perPlanet)
            if (g.planets[pp.first].system == it.first) {
                for (int t = 0; t < kUnitTypes; ++t) arriving.add(UnitType(t), pp.second[t]);
                if (aerie) carried -= pp.second[int(UnitType::Mech)];  // Aerie Sentinel
            }
    }
    // a ship's way from where it starts through 'via' to the active system (0.54.0, fixes 18: also through gravity rifts
    // it doesn't roll for - the tester's carrier passing through the Vuil'raith home)
    auto legs = [&](int from, int via) {
        if (g.fixes >= 18) return travelCostNoRoll(g, p, from, via) + travelCostNoRoll(g, p, via, active);
        return travelCost(g, p, from, via) + travelCost(g, p, via, active);
    };
    for (const BoostFighters& b : boostFighters) {  // carried as far as there's room and a ship passing them, the rest fly
        bool served = false;
        for (const Mover& m : carriers) {
            if (m.sys == b.sys || (routeOf(m.sys) && onRoute(m.sys, b.sys))) {  // (a route: where it stops)
                served = true;
                break;
            }
            if (routeOf(m.sys) || !passableFor(g, p, b.sys)) continue;
            const int mv = moveValue(g, p, m.sys, m.t) + (v13 ? m.need : gd ? 1 : 0);
            if (legs(m.sys, b.sys) <= mv) {
                served = true;
                break;
            }
        }
        const bool half = curatorHalf(g, p);
        const int room = half ? 2 * (capacity - carried) - fightersCarried : capacity - carried;
        const int take = served ? std::min(b.k, std::max(0, room)) : 0;
        if (take > 0) {
            if (half) fightersCarried += take;
            else carried += take;
            sources.insert(b.sys);
        }
        bonusUsed += (b.k - take) * b.need;
    }
    if (bonusUsed > boostShips(g, p)) {
        const int b = boostShips(g, p);  // (0.44.7: the real number - and not "Gravity Drive" when it isn't Gravity Drive)
        if (b == 1 && ownsTechKey(g, p, "gd")) return "Gravity Drive can only boost one ship.";
        return b <= 0 ? std::string("None of these ships can move 1 further.")
                      : "Only " + std::to_string(b) + (b == 1 ? " ship" : " ships") + " can move 1 further.";
    }
    if (mendosaUsed > 1) return "Captain Mendosa can only speed up one ship.";
    carried += fighterSlots(g, p, fightersCarried);
    if (carried > capacity + mechOnly) return "Not enough capacity to carry those fighters and ground forces.";
    if (mechOnly > 0 && carried - (aerie ? 0 : arriving.count(UnitType::Mech)) > capacity)
        return "Ships moving 1 further with Lightning Drives can't carry fighters or infantry.";
    // Ships can pick up units where they start and in each system they pass through (LRR 95.1).
    // (0.44.9, fixes 13 - the owner's answer P8: a detour that needs +move uses up a boost, like any other move)
    for (int src : sources) {
        bool served = false;
        int viaBoost = -1, extraNeed = 0;
        for (size_t mi = 0; mi < carriers.size(); ++mi) {
            const Mover& m = carriers[mi];
            if (m.sys == src || (routeOf(m.sys) && onRoute(m.sys, src))) {  // (Waypoints: a stop on its route)
                served = true;
                break;
            }
            if (routeOf(m.sys) || !passableFor(g, p, src)) continue;  // (a ship on a route makes no other detour)
            if (v13) {
                const int mv = moveValue(g, p, m.sys, m.t);
                const int dist = legs(m.sys, src);
                if (dist <= mv + m.need) {
                    served = true;
                    break;
                }
                if (dist <= mv + bmax && (viaBoost < 0 || dist - mv - m.need < extraNeed)) viaBoost = int(mi), extraNeed = dist - mv - m.need;
                continue;
            }
            int mv = moveValue(g, p, m.sys, m.t) + (gd ? 1 : 0);
            if (legs(m.sys, src) <= mv) {
                served = true;
                break;
            }
        }
        if (!served && viaBoost >= 0) {  // one ship of that group makes the detour with the boosts
            served = true;
            bonusUsed += extraNeed;
            if (carriers[size_t(viaBoost)].k == 1) carriers[size_t(viaBoost)].need += extraNeed;
        }
        if (!served) return "No moving ship with capacity starts in or passes through " + sysLabel(g, src) + ".";
    }
    if (v13 && bonusUsed > boostShips(g, p)) {
        const int b = boostShips(g, p);
        return b <= 0 ? std::string("None of these ships can move further (a pick-up on the way needs it).")
                      : "Only " + std::to_string(b) + " move boost" + (b == 1 ? "" : "s") + " to share - the pick-ups on the way need more.";
    }
    for (auto& mf : miniFrom) {  // Miniaturization: a moving ship of any kind must start there (or it's the active system)
        if (mf.first == active) continue;
        bool ship = false;
        auto it = takeSpace.find(mf.first);
        if (it != takeSpace.end())
            for (int t : kShipIdx)
                if (it->second.n[t]) ship = true;
        if (!ship) return "Your structures in " + sysLabel(g, mf.first) + " need a ship moving from there (Miniaturization).";
    }
    // What stays behind in each system must still fit in the ships that stay.
    std::set<int> touched;
    for (auto& it : takeSpace) touched.insert(it.first);
    for (int s : touched) {
        if (capacityOverflow(g, p, s) > 0) continue;  // already over (e.g. during setup): leave it
        Force left = g.systems[s].space[p];
        const Force& take = takeSpace[s];
        for (int t = 0; t < kUnitTypes; ++t) left.n[t] = uint8_t(left.n[t] - take.n[t]);
        if (overflowOf(g, p, s, left) > 0 && !allowStrand)  // (0.43.0: a person may leave them - they are removed)
            return "Moving those ships would strand fighters or ground forces in " + sysLabel(g, s) + ".";
        // Fighter II: fighters left without capacity count against the fleet pool there
        if (fleetUse(g, p, s, left) > fleetLimit(g, p) && fleetUse(g, p, s, left) > fleetUseIn(g, p, s))
            return "The fighters left in " + sysLabel(g, s) + " would be over your fleet pool (Fighter II).";
    }
    // Gravitic Inhibitor (Li-Zho trap, Discordant Stars): at most fleet pool - 1 ships move in
    if (g.fx.dsInhibit == p && arriving.nonFighterShips() > std::max(0, fleetLimit(g, p) - 1))
        return "Gravitic Inhibitor: at most " + std::to_string(std::max(0, fleetLimit(g, p) - 1)) + " ships can move in.";
    // Fleet pool and capacity in the active system afterwards. (0.58.5, fixes 22 - owner: "I should be able to exceed
    // the fleet pool for a tile": the move is allowed; the extra ships are removed after the combat and invasion if
    // one happens there, else right after the move - Rules::fleetDeferSys_)
    Force after = g.systems[active].space[p];
    for (int t = 0; t < kUnitTypes; ++t) after.n[t] = uint8_t(after.n[t] + arriving.n[t]);
    if ((g.fixes < 22 || fleetStrict) && fleetUse(g, p, active, after) > fleetLimit(g, p) && fleetUse(g, p, active, after) > fleetUseIn(g, p, active))
        return "Your fleet pool allows only " + std::to_string(fleetLimit(g, p)) + " non-fighter ships in a system" +
               (fightersFree(g, p) ? " (fighters beyond capacity count too)." : ".");
    if (overflowOf(g, p, active, after) > capacityOverflow(g, p, active))
        return "Not enough capacity in the active system for all fighters and ground forces.";
    if (needsColish) {
        bool flagship = false;
        for (size_t i = 0; i + 3 < data.size(); i += 4)
            if (data[i] == colishO && data[i + 1] < 0 && data[i + 2] == int(UnitType::Flagship) && data[i + 3] > 0) flagship = true;
        if (!flagship)
            return "Some of these ships only get there through Hil Colish's delta wormhole in the active system - move Hil Colish "
                   "too (it goes first).";
    }
    return "";
}

// 0.43.0 (owner: "let me leave a fighter behind on purpose, with a warning"): what a legal move would leave
// without capacity (removed after the movement), or "" when nothing is
std::string strandWarning(const Game& g, int p, int active, const std::vector<int>& data) {
    const std::string strict = checkMovePlan(g, p, active, data, false);
    if (strict.rfind("Moving those ships would strand", 0) != 0 || !checkMovePlan(g, p, active, data, true).empty()) return "";
    std::string where = strict.substr(strict.rfind(" in ") + 4);
    if (!where.empty() && where.back() == '.') where.pop_back();
    return "Fighters or ground forces left behind in " + where + " have no capacity there: they will be removed.";
}

bool combatAhead(const Game& g, int p, int sys) {
    if (sys < 0 || sys >= int(g.systems.size())) return false;
    const SystemState& s = g.systems[size_t(sys)];
    for (int q = 0; q < kSides; ++q) {
        if (q == p || (q >= g.numPlayers && q != kNeutral)) continue;
        if (s.space[size_t(q)].ships() > 0) return true;  // a space combat
        for (int16_t x : s.planets)
            if (g.planets[size_t(x)].units[size_t(q)].groundForces() > 0) return true;  // ground forces to invade
    }
    return false;
}

std::string fleetOverWarning(const Game& g, int p, int active, const std::vector<int>& data) {
    if (g.fixes < 22 || !checkMovePlan(g, p, active, data).empty() || checkMovePlan(g, p, active, data, false, true).empty()) return "";
    const std::string n = std::to_string(fleetLimit(g, p));
    return g.fixes == 22 && combatAhead(g, p, active)
               ? "More ships than your fleet pool (" + n + ") here: they all fight, then you remove the extra ships after the combat and invasion."
               : "More ships than your fleet pool (" + n + ") here: you choose and remove the extra ships right after the move (LRR 37.3).";
}

std::string checkProducePlan(const Game& g, int p, int sys, const std::vector<int>& data, int capacityLimit, int typeMask, bool fleetStrict) {
    if (data.size() != size_t(kUnitTypes) + 1) return "Bad answer.";
    int total = 0;
    // LRR 14.1: a blockaded unit can't produce ships (ground forces are fine).
    bool enemyShips = blockaded(g, p, sys) && !g.fx.noBlockade;  // (Primespace Actualization, Discordant Stars)
    int gf = 0;
    for (int t = 0; t < kUnitTypes; ++t) {
        int k = data[t];
        if (k < 0) return "Bad count.";
        if (!k) continue;
        UnitType ut = UnitType(t);
        if (isStructure(ut)) return "Structures can't be produced.";
        if (typeMask && !((typeMask >> t) & 1)) return std::string(unitTypeName(ut, true)) + " can't be produced this time.";
        if (unitDef(g, p, ut).cost2 <= 0) return std::string(unitTypeName(ut, true)) + " can't be produced.";
        if (k > unitsAvailable(g, p, ut)) return "Not enough " + std::string(unitTypeName(ut, true)) + " left in your supply.";
        if (isShip(ut) && enemyShips) return "Blockaded: other players' ships are here, so only ground forces can be produced.";
        total += k;
        if (isGroundForce(ut)) gf += k;
    }
    int freeFF = 0;  // That Which Molds Flesh (Vuil'raith commander): 2 fighters / infantry don't count
    if (commanderUsable(g, p, "cabal")) freeFF = std::min(2, data[int(UnitType::Fighter)] + data[int(UnitType::Infantry)]);
    // Combat Drones (Mirveda, Discordant Stars): their space docks don't produce infantry
    if ((g.modules & kModDiscordant) && data[int(UnitType::Infantry)] > 0 && isFaction(g, p, "mirveda") && !anyPlanetGFFor(g, p))
        return "Combat Drones: your space docks can't produce infantry.";
    // Desperados (Nokar, Discordant Stars): 1 destroyer doesn't count
    if ((g.modules & kModDiscordant) && data[int(UnitType::Destroyer)] > 0 && isFaction(g, p, "nokar")) ++freeFF;
    if (breakthroughIs(g, p, "sol")) {  // Bellum Gloriosum: ground forces / fighters up to the new ships' capacity
        int cap = 0;
        for (int t : kShipIdx)
            if (data[t] && UnitType(t) != UnitType::Fighter) cap += data[t] * std::max(0, int(unitDef(g, p, UnitType(t)).capacity));
        const int ff = data[int(UnitType::Fighter)] + data[int(UnitType::Infantry)] + data[int(UnitType::Mech)];
        freeFF += std::min(cap, ff - freeFF);
    }
    if (total - freeFF > capacityLimit) return "Production limit here is " + std::to_string(capacityLimit) + " units.";
    int gfPlanet = data[kUnitTypes];
    const SystemState& s = g.systems[sys];
    if (gf) {
        std::vector<int> dest = groundForceDestinations(g, p, sys);
        if (std::find(dest.begin(), dest.end(), gfPlanet) == dest.end())
            return "Ground forces must be placed on a planet with your space dock.";
    }
    // Mitosis (Arborec): space docks can't produce infantry - only the units with Production of their own
    // (Letani Warriors, Letani Behemoth) can; abilities that produce without docks are exempt.
    if (data[int(UnitType::Infantry)] > 0 && isFaction(g, p, "arborec") && !anyPlanetGFFor(g, p)) {
        int cap = 0;
        auto add = [&](const Force& f) {
            for (int t = 0; t < kUnitTypes; ++t)
                if (f.n[t] && UnitType(t) != UnitType::SpaceDock) cap += f.n[t] * std::max(0, int(unitDef(g, p, UnitType(t)).production));
        };
        add(s.space[p]);
        for (int16_t x : s.planets) add(g.planets[x].units[p]);
        if (data[int(UnitType::Infantry)] > cap)
            return "Arborec space docks can't produce infantry (Mitosis): only " + std::to_string(cap) + " infantry here.";
    }
    Force after = s.space[p];
    for (int t = 0; t < kUnitTypes; ++t) {
        UnitType ut = UnitType(t);
        if (isGroundForce(ut) && gfPlanet >= 0) continue;
        after.n[t] = uint8_t(after.n[t] + data[t]);
    }
    int limit = fleetLimit(g, p);
    auto reinforcements = [&]() {
        const Player& pl = g.players[size_t(p)];
        int n = kCommandTokens - pl.tactic - pl.fleet - pl.strategy - edictTokensOf(g, p);
        for (const SystemState& s2 : g.systems) n -= s2.hasToken(p) ? 1 : 0;
        return n;
    };
    if (g.fixes >= 8 && breakthroughIs(g, p, "hacan") && reinforcements() > 0) {  // Auto-Factories (0.44.4): the token
        int ships = 0;                                                                   // comes before the fleet check
        for (int t : kShipIdx)
            if (t != int(UnitType::Fighter)) ships += data[size_t(t)];
        if (ships >= 3) ++limit;
    }
    // (0.45.0, fixes 14 - owner: LRR 37.3 doesn't forbid it - ships beyond the fleet pool are removed afterwards, the
    // player choosing which; producing a better ship to scrap a weaker one is allowed)
    if ((g.fixes < 14 || fleetStrict) && fleetUse(g, p, sys, after) > limit && fleetUse(g, p, sys, after) > fleetUseIn(g, p, sys))
        return "Your fleet pool allows only " + std::to_string(fleetLimit(g, p)) + " non-fighter ships here" +
               (fightersFree(g, p) ? " (fighters beyond capacity count too)." : ".");
    if (overflowOf(g, p, sys, after) > capacityOverflow(g, p, sys))
        return "Not enough capacity for those fighters / ground forces in space.";
    return "";
}

// ============================================================ Rules helpers
std::string Rules::sysName(int s) const {
    if (s < 0) return "?";
    const SystemState& sy = G.systems[s];
    const SystemDef& d = sy.d();
    if (sy.home >= 0 && d.back != Back::Home) return std::string(d.name) + " (" + pn(sy.home) + " home)";  // Ghoti Mobile Command
    if (sy.home >= 0) return std::string(pn(sy.home)) + " home";
    if (d.planetCount == 0) return std::string(d.name) + " (" + d.key + ")";
    return d.name;
}

std::string Rules::planetName(int pl) const { return kPlanets[G.planets[pl].def].name; }

void Rules::say(int p, const std::string& text, int sys) { E.log(p, text, EventKind::Log, sys); }

void Rules::event(EventKind kind, int p, const std::string& text, int sys, int planet) {
    Event e;
    e.kind = kind;
    e.player = int8_t(p);
    e.system = int16_t(sys);
    e.planet = int16_t(planet);
    e.text = text;
    E.emit(std::move(e));
}

int Rules::pick(int p, Tag tag, const std::string& prompt, std::vector<Option> opts, const std::string& context,
                bool onMap, bool onPlanets) {
    if (p == kNeutral) {  // Thunder's Edge: neutral units make no decisions (the rare one is the first that fits)
        for (const Option& o : opts)
            if (o.enabled) return o.id;
        return opts.empty() ? -1 : opts[0].id;
    }
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Pick;
    d.tag = tag;
    d.prompt = prompt;
    d.context = context;
    d.options = std::move(opts);
    d.onMap = onMap;
    d.onPlanets = onPlanets;
    d.card = curCard_;
    d.agenda = curAgenda_;
    d.explore = curExplore_;
    d.relic = curRelic_;
    d.ability = curAbility_;
    d.value = curValue_;
    if (curFor_ >= 0) d.targetPlayer = curFor_;
    std::vector<Option> copy = d.options;
    d.validate = [copy](const Answer& a) -> std::string {
        if (a.picks.size() != 1) return "Choose one option.";
        for (const Option& o : copy)
            if (o.id == a.picks[0]) return o.enabled ? "" : "That option isn't available.";
        return "Not one of the options.";
    };
    Answer a = E.ask(std::move(d));
    return a.picks[0];
}

std::vector<int> Rules::pickMany(int p, Tag tag, const std::string& prompt, std::vector<Option> opts, int minPick,
                                 int maxPick, const std::string& context, bool onPlanets) {
    if (p == kNeutral) {  // (see pick)
        std::vector<int> v;
        for (const Option& o : opts)
            if (o.enabled && int(v.size()) < minPick) v.push_back(o.id);
        return v;
    }
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Pick;
    d.tag = tag;
    d.prompt = prompt;
    d.context = context;
    d.options = std::move(opts);
    d.minPick = minPick;
    d.maxPick = maxPick;
    d.onPlanets = onPlanets;
    d.card = curCard_;
    d.agenda = curAgenda_;
    d.explore = curExplore_;
    d.relic = curRelic_;
    d.ability = curAbility_;
    d.value = curValue_;
    std::vector<Option> copy = d.options;
    d.validate = [copy, minPick, maxPick](const Answer& a) -> std::string {
        if (int(a.picks.size()) < minPick || int(a.picks.size()) > maxPick)
            return "Choose between " + std::to_string(minPick) + " and " + std::to_string(maxPick) + ".";
        std::set<int> seen;
        for (int x : a.picks) {
            if (!seen.insert(x).second) return "Chosen twice.";
            bool ok = false;
            for (const Option& o : copy)
                if (o.id == x && o.enabled) ok = true;
            if (!ok) return "Not one of the options.";
        }
        return "";
    };
    Answer a = E.ask(std::move(d));
    return a.picks;
}

bool Rules::yesNo(int p, Tag tag, const std::string& prompt, const std::string& context) {
    return pick(p, tag, prompt, {{1, "Yes", ""}, {0, "No", ""}}, context) == 1;
}

bool Rules::pay(int p, int amount, bool influence, const std::string& why, bool optional, int extraInfluence,
                int tradeGoodsToo, bool mixed) {
    // Ocean cards (Deepwrought, Thunder's Edge): planet cards worth 1 resource or 1 influence each
    if (const int oc = amount > 0 && !inOcean_ ? oceansReady(G, p) : 0) {
        std::vector<Option> opts;
        for (int k = std::min(oc, amount); k >= 0; --k) opts.push_back({k, std::to_string(k) + " ocean card" + (k == 1 ? "" : "s"), ""});
        const bool banners = G.players[size_t(p)].oceans == 0;  // (Bannerhalls: Kjalengard)
        if (banners)
            for (Option& o : opts) o.label = std::to_string(o.id) + (o.id == 1 ? " Bannerhall" : " Bannerhalls");
        const int use = pick(p, Tag::OceanPay, std::string(banners ? "Exhaust Bannerhalls" : "Exhaust ocean cards") + " toward " + why + "?", opts,
                             std::to_string(amount) + (influence ? " influence" : " resources") + " needed; each " +
                                 (banners ? "Bannerhall" : "ocean card") + " is worth 1.");
        Player& dp = G.players[size_t(p)];
        const int before = dp.oceansExhausted;
        dp.oceansExhausted = int8_t(oceanCards(G, p));  // (the rest of the payment doesn't count them)
        inOcean_ = true;
        const bool ok = pay(p, amount - use, influence, why, optional, extraInfluence, tradeGoodsToo, mixed);
        inOcean_ = false;
        dp.oceansExhausted = int8_t(before + (ok ? use : 0));
        return ok;
    }
    // Custodia Vigilia (Keleres breakthrough): a planet card without a place on the board
    if (const int cv = amount > 0 && !inCustodia_ ? custodiaValue(G, p, influence) : 0) {
        curValue_ = amount;  // (0.44.4: what is needed, for the AI)
        const bool use = fyes(p, Ab::BtKeleres, "Exhaust Custodia Vigilia for " + std::to_string(cv) + (influence ? " influence?" : " resources?"),
                              "Toward " + why + " (" + std::to_string(amount) + " needed).");
        curValue_ = 0;
        Player& kp = G.players[size_t(p)];
        kp.custodiaExhausted = true;  // (used, or kept out of the rest of this payment)
        inCustodia_ = true;
        const bool ok = pay(p, use ? std::max(0, amount - cv) : amount, influence, why, optional, extraInfluence, tradeGoodsToo, mixed);
        inCustodia_ = false;
        if (!use || !ok) kp.custodiaExhausted = false;
        else say(p, pn(p) + " exhausts Custodia Vigilia.");
        return ok;
    }
    // The Triad (Thunder's Edge relic): exhausted like a planet card toward this payment
    if (const int tv = amount > 0 && !inTriad_ ? triadValue(G, p) : 0) {
        const int r = findRelic("thetriad");
        curRelic_ = r;
        const bool use = yesNo(p, Tag::RelicChoice, "Exhaust The Triad for " + std::to_string(tv) + (influence ? " influence?" : " resources?"),
                               "Toward " + why + " (" + std::to_string(amount) + " needed).");
        curRelic_ = -1;
        if (use) {
            G.players[size_t(p)].exhaustedRelics.push_back(int16_t(r));
            relicUsedEvent(p, r, false);
            inTriad_ = true;
            const bool ok = pay(p, std::max(0, amount - tv), influence, why, optional, extraInfluence, tradeGoodsToo, mixed);
            inTriad_ = false;
            if (!ok) {  // (nothing paid after all: it stays ready)
                auto& ex = G.players[size_t(p)].exhaustedRelics;
                auto it = std::find(ex.begin(), ex.end(), int16_t(r));
                if (it != ex.end()) ex.erase(it);
            }
            return ok;
        }
        // kept: the rest of the payment mustn't count it (marked exhausted meanwhile)
        G.players[size_t(p)].exhaustedRelics.push_back(int16_t(r));
        inTriad_ = true;
        const bool ok = pay(p, amount, influence, why, optional, extraInfluence, tradeGoodsToo, mixed);
        inTriad_ = false;
        auto& ex = G.players[size_t(p)].exhaustedRelics;
        ex.erase(std::find(ex.begin(), ex.end(), int16_t(r)));
        return ok;
    }
    // The Ghoti planet card (Spawning Grounds, Discordant Stars): 3 resources or 3 influence, not on the board
    if ((G.modules & kModDiscordant) && amount > 0 && !inGhoti_ && !mixed) {
        const int v = dsGhotiPlanet(p, amount, influence, why);
        if (v > 0) {
            inGhoti_ = true;
            const bool ok = pay(p, std::max(0, amount - v), influence, why, optional, extraInfluence, tradeGoodsToo, mixed);
            inGhoti_ = false;
            if (!ok) G.players[size_t(p)].dsGhotiExhausted = false;
            return ok;
        }
    }
    // Solis Morden (Veldyr agent, Discordant Stars): research paid with a planet's influence
    if ((G.modules & kModDiscordant) && amount > 0 && !influence && !mixed && !inSolis_ && why.rfind("researching", 0) == 0) {
        int planet = -1;
        const int v = dsSolis(p, amount, planet);
        if (v > 0) {
            inSolis_ = true;
            const bool ok = pay(p, std::max(0, amount - v), influence, why, optional, extraInfluence, tradeGoodsToo, mixed);
            inSolis_ = false;
            if (!ok && planet >= 0) G.planets[size_t(planet)].exhausted = false;
            return ok;
        }
    }
    // Diplomats (Free Systems, Discordant Stars): an uncontrolled planet's card toward the payment, once per action
    if ((G.modules & kModDiscordant) && amount > 0 && !inDiplomats_ && !mixed && extraInfluence <= 0) {
        int planet = -1;
        const int v = dsDiplomats(p, amount, influence, why, planet);
        if (v > 0) {
            inDiplomats_ = true;
            const bool ok = pay(p, std::max(0, amount - v), influence, why, optional, extraInfluence, tradeGoodsToo, mixed);
            inDiplomats_ = false;
            if (!ok && planet >= 0) {
                G.planets[size_t(planet)].exhausted = false;
                dsOncePerAction_ = uint8_t(dsOncePerAction_ & ~2);
            }
            return ok;
        }
    }
    // Udosh B'rtul (Khrask agent, Discordant Stars): one planet's resources count as influence too (exhausted now)
    if ((G.modules & kModDiscordant) && influence && !mixed && extraInfluence <= 0 && !inXander_ && amount > 0) {
        const int got = dsUdosh(p, amount + tradeGoodsToo);
        if (got > 0) {
            amount = std::max(0, amount - got);
            if (amount == 0) return payCore(p, 0, influence, why, optional, 0, tradeGoodsToo, mixed);
        }
    }
    // Xander Alexin Victori III (Keleres agent): commodities count as trade goods for this payment
    const int x = inXander_ || extraInfluence > 0 ? -1 : xanderOffer(p, amount + tradeGoodsToo, influence, mixed);
    if (x < 0) return payCore(p, amount, influence, why, optional, extraInfluence, tradeGoodsToo, mixed);
    Player& pl = G.players[p];
    const int c0 = pl.commodities, tg0 = pl.tradeGoods;
    pl.tradeGoods = int8_t(tg0 + c0);
    pl.commodities = 0;
    inXander_ = true;
    const bool ok = payCore(p, amount, influence, why, optional, extraInfluence, tradeGoodsToo, mixed);
    inXander_ = false;
    const int spent = std::max(0, tg0 + c0 - pl.tradeGoods), fromComm = std::min(c0, spent);  // commodities go first
    pl.commodities = int8_t(c0 - fromComm);
    pl.tradeGoods = int8_t(tg0 - (spent - fromComm));
    if (fromComm) say(p, pn(p) + " pays with " + std::to_string(fromComm) + " commodit" + (fromComm == 1 ? "y" : "ies") + " (Xander Alexin Victori III).");
    return ok;
}

bool Rules::payCore(int p, int amount, bool influence, const std::string& why, bool optional, int extraInfluence,
                    int tradeGoodsToo, bool mixed) {
    Player& pl = G.players[p];
    if (tradeGoodsToo > pl.tradeGoods) return false;
    if (amount <= 0 && extraInfluence <= 0) {
        pl.tradeGoods = int8_t(pl.tradeGoods - tradeGoodsToo);
        return true;
    }
    const int worth = tgWorth(G, p);  // Mirror Computing
    // (commodities only count when Xander was used: then they are already among the trade goods)
    bool afford = (mixed ? spendableMixed(G, p) : spendable(G, p, influence)) - xanderCommodities(G, p) * worth - tradeGoodsToo * worth >= amount;
    if (afford && extraInfluence > 0) {  // resources and influence: the same planet can't pay both
        pl.tradeGoods = int8_t(pl.tradeGoods - tradeGoodsToo);
        afford = canPayBoth(G, p, influence ? 0 : amount, extraInfluence + (influence ? amount : 0));
        pl.tradeGoods = int8_t(pl.tradeGoods + tradeGoodsToo);
    }
    if (!afford) {
        if (!optional) say(p, pn(p) + " can't afford " + why + ".");
        return false;
    }
    pl.tradeGoods = int8_t(pl.tradeGoods - tradeGoodsToo);
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Pay;
    d.tag = Tag::Payment;
    d.prompt = std::string("Pay ") + std::to_string(amount) + (mixed ? " resources or influence" : influence ? " influence" : " resources") +
               ": " + why;
    if (extraInfluence > 0 && !influence)  // 0.43.0 (D72): a payment in two parts says so (Privateer, Udosh B'rtul...)
        d.context = "Resources first - then " + std::to_string(extraInfluence) + " influence from other planets.";
    d.amount = amount;
    d.influence = influence;
    d.mixed = mixed;
    d.amount2 = extraInfluence;
    d.planets = ti::readyPlanets(G, p);
    d.maxPick = optional ? 1 : 0;  // UI hint: declining allowed
    Decision copy = d;
    const Game* gp = &G;
    d.validate = [gp, p, copy, optional](const Answer& a) -> std::string {
        if (a.number == -1 && a.picks.empty()) return optional ? "" : "This cost must be paid.";
        return checkPayment(*gp, p, copy, a);
    };
    Answer a = E.ask(std::move(d));
    if (a.number == -1 && a.picks.empty()) {
        pl.tradeGoods = int8_t(pl.tradeGoods + tradeGoodsToo);
        return false;
    }
    for (int x : a.picks) G.planets[x].exhausted = true;
    pl.tradeGoods = int8_t(pl.tradeGoods - a.number);
    planetsExhausted(p, a.picks);  // Arcane Citadel (Uncharted Space)
    if (extraInfluence > 0) return pay(p, extraInfluence, true, why + " - the influence part", false);
    return true;
}

void Rules::checkWin() {
    // LRR 98.7: if several players reach the goal together, earliest in initiative order wins.
    for (int p : initiativeOrder(G))
        if (G.players[p].vp >= G.vpGoal) {
            G.winner = int8_t(p);
            throw GameOverSignal();
        }
}

void Rules::loseVP(int p, int n, const std::string& why) {
    G.players[p].vpLost = int16_t(G.players[p].vpLost + std::min<int>(n, G.players[p].vp));
    G.players[p].vp = int8_t(std::max(0, G.players[p].vp - n));
    E.log(p, pn(p) + " loses " + std::to_string(n) + " victory point" + (n > 1 ? "s" : "") + " (" + why + ") - now " +
                 std::to_string(G.players[p].vp) + ".",
          EventKind::Score);
}

void Rules::gainVP(int p, int n, const std::string& why) {
    const bool civil = eventOn(G, kEvCivilized);  // Civilized Society: the game ends after the status phase
    G.players[p].vp = int8_t(std::min(civil ? 99 : G.vpGoal, G.players[p].vp + n));
    E.log(p, pn(p) + " gains " + std::to_string(n) + " victory point" + (n > 1 ? "s" : "") + " (" + why + ") - now " +
                 std::to_string(G.players[p].vp) + ".",
          EventKind::Score);
    if (!civil) checkWin();
}

int Rules::tokensInPools(int p) const {
    const Player& pl = G.players[p];
    return pl.tactic + pl.fleet + pl.strategy;
}

int Rules::tokensOnBoard(int p) const {
    int n = 0;
    for (const SystemState& s : G.systems)
        if (s.hasToken(p)) ++n;
    return n;
}

int Rules::reinforcementTokens(int p) const {
    return kCommandTokens - tokensInPools(p) - tokensOnBoard(p) - edictTokensOf(G, p);  // Edict: tokens in Mahact's fleet pool
}

void Rules::placeTokenFromReinforcements(int p, int sys, const std::string& why) {
    if (G.systems[sys].hasToken(p)) return;  // LRR 20.6: nothing more to place
    if (reinforcementTokens(p) <= 0) {
        // LRR 20.3b / 32.2a / 78.7d: take one from a pool on the command sheet instead.
        Player& pl = G.players[p];
        std::vector<Option> opts;
        if (pl.tactic > 0) opts.push_back({0, "Tactic pool", std::to_string(pl.tactic) + " tokens"});
        if (pl.fleet > 0) opts.push_back({1, "Fleet pool", std::to_string(pl.fleet) + " tokens"});
        if (pl.strategy > 0) opts.push_back({2, "Strategy pool", std::to_string(pl.strategy) + " tokens"});
        if (opts.empty()) return;
        int c = opts.size() == 1 ? opts[0].id
                                 : pick(p, Tag::TokenFromPool, "No command tokens left in reinforcements: take one from which pool?",
                                        opts, why);
        if (c == 0) --pl.tactic;
        else if (c == 1) --pl.fleet;
        else --pl.strategy;
        if (c == 1) enforceFleetLimit(p);
    }
    G.systems[sys].tokens |= uint16_t(1u << p);
    zeuTE(p, sys);  // (fixes 27: Thunder's Edge's Z'eu)
}

void Rules::gainTokens(int p, int n, bool redistribute, const std::string& why) {
    Player& pl = G.players[p];
    n = std::max(0, std::min(n, reinforcementTokens(p)));
    if (n == 0 && !redistribute) return;
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Tokens;
    d.tag = redistribute ? Tag::TokenRedistribute : Tag::TokenGain;
    d.prompt = redistribute ? "Arrange your command tokens" : "Place " + std::to_string(n) + " new command token(s)";
    d.context = why;
    d.newTokens = n;
    d.redistribute = redistribute;
    const int t0 = pl.tactic, f0 = pl.fleet, s0 = pl.strategy;
    int maxShips = 0;
    for (size_t s = 0; s < G.systems.size(); ++s) maxShips = std::max(maxShips, fleetUseIn(G, p, int(s)));
    maxShips -= fleetLimit(G, p) - pl.fleet;  // Armada: 2 ships more than the fleet pool tokens
    const int fleetCap = lawInPlay(G, "regulations") ? 4 : 99;  // Fleet Regulations
    d.value = fleetCap;
    d.validate = [=](const Answer& a) -> std::string {
        if (a.data.size() != 3) return "Bad answer.";
        int t = a.data[0], f = a.data[1], s = a.data[2];
        if (t < 0 || f < 0 || s < 0) return "Pools can't be negative.";
        if (t + f + s != t0 + f0 + s0 + n) return "Place exactly " + std::to_string(t0 + f0 + s0 + n) + " tokens in total.";
        if (!redistribute && (t < t0 || f < f0 || s < s0)) return "Only add tokens here.";
        if (f > fleetCap && f > f0) return "Fleet Regulations: at most 4 tokens in your fleet pool.";
        if (f < maxShips && f < f0)
            return "Your fleet pool must cover the " + std::to_string(maxShips) + " ships in your largest fleet.";
        return "";
    };
    Answer a = E.ask(std::move(d));
    pl.tactic = int8_t(a.data[0]);
    pl.fleet = int8_t(a.data[1]);
    pl.strategy = int8_t(a.data[2]);
    enforceFleetLimit(p);
    say(p, pn(p) + " command tokens: " + std::to_string(pl.tactic) + " tactic / " + std::to_string(pl.fleet) +
               " fleet / " + std::to_string(pl.strategy) + " strategy.");
}

void Rules::addUnits(int p, UnitType t, int k, int sys, int planet) {
    // Gauss Cannon (Mirveda PDS, Discordant Stars): placed in the space area instead of on a planet
    if (planet >= 0 && t == UnitType::PDS && (G.modules & kModDiscordant) && p >= 0 && p < G.numPlayers && unitDef(G, p, t).spaceOnly) planet = -1;
    k = std::min(k, (p < G.numPlayers ? unitLimitOf(G, p, t) : unitLimit(t)) - unitsOnBoard(G, p, t));  // the pieces in the box are the limit
    if (k <= 0) return;
    if (planet >= 0 && (isDMZ(G, planet) || G.planets[planet].destroyed)) {
        say(p, std::string("Units can't be placed on ") + planetName(planet) + " (Demilitarized Zone).");
        return;
    }
    if (planet >= 0 && t == UnitType::Infantry && arcaneShield(G, planet)) {  // Arcane Shield Emitter (0.43.0)
        say(p, std::string("Infantry can't be placed on ") + planetName(planet) + " (Arcane Shield Emitter).");
        return;
    }
    if (planet >= 0) G.planets[planet].units[p].add(t, k);
    else G.systems[sys].space[p].add(t, k);
    if (planet < 0 && isShip(t)) fleetDirty_ = true;  // (0.46.1: the fleet pool is checked before the next question)
    if (sys == G.nexus) checkNexus(sys);
}

void Rules::destroyUnits(int p, UnitType t, int k, int sys, int planet, int byPlayer, const char* how) {
    Force& f = planet >= 0 ? G.planets[planet].units[p] : G.systems[sys].space[p];
    k = std::min<int>(k, f.n[int(t)]);
    if (k <= 0) return;
    if ((G.modules & kModDiscordant) && !removing_ && p < G.numPlayers) k -= dsSaveUnits(p, t, k, sys, planet, byPlayer);  // Pustule (Kyro mech)
    if (k <= 0) return;
    // Silence of Stars (Li-Zho flagship, 0.43.0 D126): only an uncanceled hit can destroy it (not Direct Hit, a gravity
    // rift or an ability that destroys outright); being removed still works
    if (t == UnitType::Flagship && !byHits_ && !removing_ && (G.modules & kModDiscordant) && G.fixes >= 2 && fac(p, "lizho")) {
        say(p, pn(p) + "'s Silence of Stars can only be destroyed by hits - it survives (" + (how && *how ? how : "") + ").");
        return;
    }
    // Eradica (Nivyn flagship, Discordant Stars): other players' units in its system are removed, not destroyed
    struct EradicaGuard {
        bool& f;
        bool was;
        ~EradicaGuard() { f = was; }
    } eradica{removing_, removing_};
    if ((G.modules & kModDiscordant) && !removing_ && dsEradica(p, sys)) removing_ = true;
    const int galvBefore = f.galv[int(t)];
    f.remove(t, k, true);
    const int galvLost = galvBefore - f.galv[int(t)];  // (Last Bastion: galvanized units are the last to go)
    Event e;
    e.kind = EventKind::Destroyed;
    e.player = int8_t(p);
    e.other = int8_t(byPlayer);
    e.system = int16_t(sys);
    e.planet = int16_t(planet);
    e.unit = int8_t(t);
    e.count = int8_t(k);
    e.text = pn(p) + " loses " + (isFlagship(t) && p < G.numPlayers && G.players[size_t(p)].unit[int(UnitType::Flagship2)] >= 0
                                      ? unitLabel(G, p, t)  // (0.52.0: The Lady / The Lord)
                                      : std::to_string(k) + " " + unitTypeName(t, k > 1)) +
             (how && *how ? std::string(" (") + how + ")" : "");
    E.emit(e);
    if (byPlayer >= 0 && byPlayer != p && (t == UnitType::WarSun || isFlagship(t))) actionSecret(byPlayer, "dtgs");
    if (G.events) eventDestroyed(p, t, k, byPlayer);  // Total War (galactic event)
    if (p < G.numPlayers && isShip(t) && t != UnitType::Fighter && !removing_ && eventOn(G, kEvFighters))  // Age of Fighters: purged
        G.players[size_t(p)].purgedUnits[int(t)] = uint8_t(G.players[size_t(p)].purgedUnits[int(t)] + k);
    if (G.modules & kModDiscordant) dsUnitsDestroyed(p, t, k, sys, planet, byPlayer);  // Raze, Chitin Hulk, Nauplius
    if (p >= G.numPlayers) return;  // (Thunder's Edge: neutral units have no abilities that answer to their losses)
    if (t == UnitType::Infantry && !removing_) infantryDestroyed(p, k);  // Infantry II / Spec Ops II
    if (t == UnitType::Infantry && !removing_ && (G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtInfantryDied(p, planet, k);  // Dreamwalkers
    if (t == UnitType::Mech && G.players[size_t(p)].eidolonMax) eidolonLost(p);  // Absolute Synergy (Thunder's Edge)
    if (G.modules & kModThundersEdge) {  // Thunder's Edge factions (0.35.0)
        // (fixes 24: A3 Valiance and Lyra Keen say "destroyed" - not for units removed, e.g. in a gravity rift)
        if (galvLost > 0 && !(G.fixes >= 24 && removing_)) galvanizedLost(p, t, galvLost, sys, planet);
        if (!removing_) dameBriar(p, sys);
        if (G.fixes < 19) subatomicSplicer(p, t, k);  // Crimson technology
    }
    unitDestroyedFactions(p, t, k, sys, planet);  // Van Hauge, Brother Milor, Ipswitch, Salvage Operations
    // (0.55.0, fixes 19: after Devour / The Terror Between - a ship the Vuil'raith capture isn't in the reinforcements
    // to be rebuilt; before, the same carrier came back at home AND sat on the Vuil'raith's sheet: 5 of 4 carriers)
    if ((G.modules & kModThundersEdge) && G.fixes >= 19) subatomicSplicer(p, t, k);
    // Self-Assembly Routines (0.44.8, fixes 12: destroyed only - not mechs removed, LRR 31.2)
    if (t == UnitType::Mech && owns(p, "sar") && (G.fixes < 12 || !removing_)) {
        gainTG(p, k);
        say(p, pn(p) + " gains " + std::to_string(k) + " trade good(s) (Self-Assembly Routines).");
    }
    // Crash Landing (Thunder's Edge): the last ship in the active system is gone, ground forces still in its space area
    if (planet < 0 && sys >= 0 && sys == G.activeSystem && isShip(t) && p < G.numPlayers && (G.modules & kModThundersEdge) &&
        !hasShips(G, p, sys) && G.systems[size_t(sys)].space[size_t(p)].groundForces() > 0 &&
        !(G.fixes >= 24 && removing_))  // (fixes 24: the card says "destroyed" - not a ship removed, e.g. in a gravity rift)
        lastShipLost(p, sys);
}

void Rules::setControl(int planet, int p) {
    PlanetState& pl = G.planets[planet];
    int old = pl.owner;
    if (old == p) return;
    // (fixes 25, owner's faction debug pass) control passing because of coexistence (Viper EX-23 / Research Team, Puppets
    // of the Blade, Viper Hollow - the old owner coexists by now): their structures stay, coexisting - "Coexisting
    // structures are always blockaded" (TE rulebook); the same for neutral units that coexist there. Assimilate and
    // Infiltrate still replace them.
    const bool oldCoexists = G.fixes >= 25 && old >= 0 && ((pl.coexist >> old) & 1);
    const bool neutralCoexists = G.fixes >= 25 && ((pl.coexist >> kNeutral) & 1);
    if (old >= 0 && !assimilate(planet, p, old)) {  // Assimilate (L1Z1X): the structures become theirs
        // Infiltrate: the structures become the new owner's instead.
        bool infiltrate = false;
        if (pl.units[old].structures()) {
            WinCtx w;
            w.w = Win::GainControl;
            w.a = p;
            w.b = old;
            w.sys = pl.system;
            w.planet = planet;
            infiltrate = cardHas(window(w), "infiltrate");
        }
        // the old owner's structures there are lost (LRR 49.5a)
        Force& f = pl.units[old];
        for (UnitType t : {UnitType::PDS, UnitType::SpaceDock}) {
            int k = f.count(t);
            if (!k || (oldCoexists && !infiltrate)) continue;
            destroyUnits(old, t, k, pl.system, planet, p, infiltrate ? "Infiltrate" : "captured");
            if (infiltrate) addUnits(p, t, std::min(k, unitsAvailable(G, p, t)), pl.system, planet);
        }
    }
    if (old >= 0 && G.systems[pl.system].home >= 0) actionSecret(old, "bam");
    if (p >= 0 && !neutralCoexists)  // Thunder's Edge: neutral structures left on the planet are lost too (as another player's, LRR 49.5a)
        for (int t = 0; t < kUnitTypes; ++t)
            if (pl.units[kNeutral].n[t]) destroyUnits(kNeutral, UnitType(t), pl.units[kNeutral].n[t], pl.system, planet, p, "captured");
    pl.owner = int8_t(p);
    pl.exhausted = true;  // newly gained planets arrive exhausted
    persControl(planet, old, p);
    event(EventKind::Control, p, pn(p) + " takes control of " + planetName(planet) + ".", pl.system, planet);
    if (lawOwner(G, "minister_exploration") == p) {
        gainTG(p, 1);
        say(p, pn(p) + " gains 1 trade good (Minister of Exploration).");
    }
    if (old >= 0) {
        WinCtx w;
        w.w = Win::LoseControl;
        w.a = p;
        w.b = old;
        w.sys = pl.system;
        w.planet = planet;
        window(w);
    }
    if (G.phase == Phase::Action && G.fixes < 9) integratedEconomy(p, planet);
    afterGainControl(planet, p, old);
    gainedControl3(planet, p, old);  // Scavenge, Scavenger Zeta, Reclamation
    gainedControl4(planet, p, old);  // Dart and Tai
    if (G.events) eventControl(planet, p, old);  // Total War, Dangerous Wilds (galactic events)
    if (G.modules & kModDiscordant) dsControl(planet, p, old);  // Scour, Yvin Korduul (Discordant Stars)
    // Integrated Economy (0.44.5, fixes 9): after the exploration (an attachment can raise the budget) - if still ours
    // (fixes 27, the shared technologies - owner 2026-11-14 "Do A"): in every phase but the game's setup (the card has
    // no phase; e.g. Colonial Redistribution in the agenda phase)
    const bool ieWhen = G.phase == Phase::Action || (G.fixes >= 27 && G.phase != Phase::Setup && G.phase != Phase::GameOver);
    if (ieWhen && G.fixes >= 9 && G.planets[size_t(planet)].owner == p) integratedEconomy(p, planet);
}

bool Rules::owns(int p, const char* key) const { return ownsTechKey(G, p, key); }

bool Rules::techReady(int p, const char* key) const {
    int t = findTech(key);
    return ownsTechKey(G, p, key) && !G.players[p].techExhausted(t);
}

void Rules::exhaustTech(int p, const char* key) {
    int t = findTech(key);
    if (t >= 0 && !G.players[p].techExhausted(t)) {
        G.players[p].exhaustedTechs.push_back(int16_t(t));
        if ((G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtTechExhausted(p);  // Synchronicity VI (0.43.0)
    }
}

void Rules::researchTech(int p, int tech, bool researched) {
    Player& pl = G.players[p];
    if (tech < 0 || pl.hasTech(tech)) return;
    pl.techs.push_back(int16_t(tech));
    // Anti-Intellectual Revolution: after researching, destroy one of your non-fighter ships.
    if (researched && G.phase != Phase::Setup && lawInPlay(G, "revolution")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            for (int t : kShipIdx)
                if (t != int(UnitType::Fighter) && G.systems[s].space[p].n[t])
                    opts.push_back({int(s) * 16 + t, std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)), ""});
        if (!opts.empty()) {
            int id = opts.size() == 1 ? opts[0].id
                                      : pick(p, Tag::AgendaChoice, "Anti-Intellectual Revolution: destroy which ship?", opts);
            destroyUnits(p, UnitType(id % 16), 1, id / 16, -1, -1, "Anti-Intellectual Revolution");
            removeExcessCapacity(p, id / 16);
        }
    }
    if (kTechs[tech].unitUpgrade) {
        for (int u = 0; u < kUnitCount; ++u) {
            if (kUnits[u].tech != tech) continue;
            if (kUnits[u].faction >= 0 && kUnits[u].faction != pl.faction) continue;
            pl.unit[int(kUnits[u].type)] = int16_t(u);
        }
        // 0.44.4 (fixes 8): a flip breakthrough already gained goes on top of the new upgrade (Tribune, Eye of the Curator, Omni-Forgeworld)
        if (G.fixes >= 8 && (G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtUnits(p);
    }
    if (researched) researchAgreement(p, tech);  // Jol-Nar's note
    // Nekro Acidos (Nekro commander): an action card after gaining a technology
    if (G.phase != Phase::Setup && commanderUsable(G, p, "nekro")) drawActionCards(p, 1, "Nekro Acidos");
    if (G.modules & kModThundersEdge) {
        firmamentGained(p, tech);  // Planesplitter: the Fracture
        extractCopy(p, tech);      // Extract (the Obsidian's plot)
    }
}

// Asks the player to choose 'count' units to remove (not destroy) from a
// space area, then removes them.
void Rules::chooseRemovals(int p, int sys, int count, int mask, const std::string& why) {
    Force& f = G.systems[sys].space[p];
    int present = 0, typesPresent = 0, onlyType = -1;
    for (int t = 0; t < kUnitTypes; ++t)
        if (((mask >> t) & 1) && f.n[t]) {
            present += f.n[t];
            ++typesPresent;
            onlyType = t;
        }
    count = std::min(count, present);
    if (count <= 0) return;
    struct Removing {
        bool& flag;
        bool old;
        ~Removing() { flag = old; }
    } guard{removing_, removing_};
    removing_ = true;
    if (p == kNeutral) {  // Thunder's Edge: neutral units lose those lowest on their reference card first
        static const UnitType order[] = {UnitType::Fighter, UnitType::Destroyer, UnitType::Cruiser, UnitType::Carrier, UnitType::Dreadnought,
                                         UnitType::WarSun, UnitType::Flagship, UnitType::Infantry, UnitType::Mech};
        for (UnitType t : order) {
            if (count <= 0) break;
            if (!((mask >> int(t)) & 1) || !f.n[int(t)]) continue;
            const int k = std::min<int>(count, f.n[int(t)]);
            destroyUnits(p, t, k, sys, -1, -1, why.c_str());
            count -= k;
        }
        return;
    }
    if (typesPresent == 1 || count == present) {
        for (int t = 0; t < kUnitTypes; ++t)
            if (((mask >> t) & 1) && f.n[t]) {
                int k = typesPresent == 1 ? count : f.n[t];
                destroyUnits(p, UnitType(t), k, sys, -1, -1, why.c_str());
            }
        (void)onlyType;
        return;
    }
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Hits;
    d.tag = Tag::RemoveExcess;
    d.prompt = "Remove " + std::to_string(count) + " unit" + (count > 1 ? "s" : "") + " (" + why + ")";
    d.context = "Your units in " + sysName(sys) + ".";
    d.system = sys;
    d.hits = count;
    d.hitMask = mask;
    d.noSustain = true;
    const Game* gp = &G;
    d.validate = [gp, p, sys, count, mask](const Answer& a) { return checkHits(*gp, p, sys, -1, count, mask, a.data, true); };
    Answer a = E.ask(std::move(d));
    int remove[kUnitTypes] = {};
    for (size_t i = 0; i < a.data.size(); i += 2) ++remove[a.data[i]];
    for (int t = 0; t < kUnitTypes; ++t)
        if (remove[t]) destroyUnits(p, UnitType(t), remove[t], sys, -1, -1, why.c_str());
}

// LRR 37.3 / 16.3 (0.46.1, fixes 15 - owner: "the game should check the fleet pool at all times"): before every
// question, a player with more ships in a system than their fleet pool allows, or more fighters / ground forces in a
// space area than their ships can carry, removes the excess there - whatever caused it (Industrious turning a dock
// into a war sun, a card or relic placing a ship, a token leaving the fleet pool, a space dock destroyed by a card).
// Not while ships are moving (the move checks when it ends), while a removal is being chosen, or in the system of a
// space combat in progress (it checks when the combat ends, LRR 16.3c).
void Rules::fleetStateCheck() {
    if (G.fixes < 15 || G.round < 1 || fleetHold_ > 0 || inFleetCheck_ || removing_ || G.phase == Phase::GameOver) return;
    if (!fleetDirty_ && G.log.size() == fleetLogSize_) return;
    struct Guard {
        bool& f;
        ~Guard() { f = false; }
    } guard{inFleetCheck_};
    inFleetCheck_ = true;
    for (int p = 0; p < G.numPlayers; ++p) {
        if (G.players[size_t(p)].eliminated) continue;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (inSpaceCombat_ && int(s) == combatSys_) continue;
            if (int(s) == fleetDeferSys_ && p == G.activePlayer) continue;  // (0.58.5: after its combat and invasion)
            const Force& f = G.systems[s].space[size_t(p)];
            const int limit = fleetLimit(G, p);
            const bool fleet = f.nonFighterShips() + f.count(UnitType::Fighter) + f.count(UnitType::Mech) > limit && fleetUseIn(G, p, int(s)) > limit;
            const bool cap = !fleet && f.count(UnitType::Fighter) + f.groundForces() > 0 && capacityOverflow(G, p, int(s)) > 0;
            if (!fleet && !cap) continue;
            if (fleet)
                say(p, pn(p) + " has more ships in " + sysName(int(s)) + " than their fleet pool allows (" + std::to_string(limit) +
                           ") - the extra ships are removed (LRR 37.3).", int(s));
            else
                say(p, pn(p) + " has more fighters or ground forces in " + sysName(int(s)) +
                           " than their ships can carry - the extra are removed (LRR 16.3).", int(s));
            removeExcessCapacity(p, int(s));
        }
    }
    fleetDirty_ = false;
    fleetLogSize_ = G.log.size();
}

void Rules::enforceFleetLimit(int p) {
    // LRR 37.3: the player chooses which excess ships to remove.
    for (size_t s = 0; s < G.systems.size(); ++s) enforceFleetIn(p, int(s));
}

void Rules::enforceFleetIn(int p, int s) {
    // Fighter II: fighters beyond capacity count too (Naalu: half each), so they may go instead.
    for (int guard = 0; guard < 8; ++guard) {
        int over = fleetUseIn(G, p, s) - fleetLimit(G, p);
        if (over <= 0) return;
        int mask = 0;
        for (int t : kShipIdx)
            if (t != int(UnitType::Fighter) || excessFighters(G, p, s, G.systems[s].space[p]) > 0) mask |= 1 << t;
        chooseRemovals(p, s, over, mask, "over fleet pool limit");
        int over2 = capacityOverflow(G, p, s);
        if (over2 > 0) {
            int cmask = (1 << int(UnitType::Fighter)) | (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech));
            chooseRemovals(p, s, over2, cmask, "no capacity");
        }
    }
}

void Rules::removeExcessCapacity(int p, int sys) {
    if (p < 0 || p >= G.numPlayers) return;  // (neutral units have no capacity rules)
    // Flotilla (Dih-Mohn, Discordant Stars): infantry beyond their non-fighter ships here go first
    if ((G.modules & kModDiscordant) && fac(p, "dihmohn")) {
        const Force& f = G.systems[sys].space[p];
        const int extra = f.count(UnitType::Infantry) - f.nonFighterShips();
        if (extra > 0) {
            const bool was = removing_;
            removing_ = true;
            destroyUnits(p, UnitType::Infantry, extra, sys, -1, -1, "Flotilla");
            removing_ = was;
            say(p, pn(p) + " returns " + std::to_string(extra) + " infantry: no more infantry than non-fighter ships in a space area (Flotilla).", sys);
        }
    }
    // LRR 16.3a: the player chooses which excess fighters / ground forces to remove.
    // (0.45.0, fixes 14 - LRR 16.3c: not during a combat in that system - the excess goes at the end of the combat,
    // so Brother Milor's fighters fight on)
    const bool fighting = G.fixes >= 14 && inSpaceCombat_ && sys == combatSys_;
    int over = fighting ? 0 : capacityOverflow(G, p, sys);
    if (over > 0) {
        int mask = (1 << int(UnitType::Fighter)) | (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech));
        if (fightersFree(G, p)) mask &= ~(1 << int(UnitType::Fighter));  // Fighter II: only ground forces need room
        chooseRemovals(p, sys, over, mask, "no capacity");
    }
    if (fleetUseIn(G, p, sys) > fleetLimit(G, p) && !(sys == fleetDeferSys_ && p == G.activePlayer)) enforceFleetIn(p, sys);  // (0.58.5)
}

void Rules::checkEliminations() {
    for (int p = 0; p < G.numPlayers; ++p)
        if (shouldBeEliminated(G, p)) eliminate(p);
}

void Rules::eliminate(int p) {
    Player& pl = G.players[p];
    // LRR 33: units, tokens and cards leave the game.
    for (SystemState& s : G.systems) {
        s.space[p] = Force();
        s.tokens &= uint16_t(~(1u << p));
    }
    for (PlanetState& x : G.planets) x.units[p] = Force();
    for (int8_t c : pl.cards) G.cardOwner[c] = -1;
    pl.cards.clear();
    for (int16_t o : pl.secrets) G.secretDeck.push_back(o);
    pl.secrets.clear();
    for (int16_t c : pl.actionCards) G.actionDiscard.push_back(c);
    pl.actionCards.clear();
    // LRR 69.7: their promissory notes leave the game; others' notes go back to their owners.
    for (int16_t n : pl.notes)
        if (noteOwner(n) != p) G.players[noteOwner(n)].notes.push_back(n);
    for (int16_t n : pl.notesInPlay)
        if (noteOwner(n) != p) G.players[noteOwner(n)].notes.push_back(n);
    pl.notes.clear();
    pl.notesInPlay.clear();
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p) continue;
        Player& o = G.players[q];
        for (int16_t n : std::vector<int16_t>(o.notesInPlay))
            if (noteOwner(n) == p) {
                o.notesInPlay.erase(std::find(o.notesInPlay.begin(), o.notesInPlay.end(), n));
                if (noteType(n) == kNoteSupportThrone) o.vp = int8_t(std::max(0, o.vp - 1));
            }
        o.notes.erase(std::remove_if(o.notes.begin(), o.notes.end(), [p](int16_t n) { return noteOwner(n) == p; }), o.notes.end());
    }
    pl.edict = 0;
    for (Player& o : G.players) o.edict = uint8_t(o.edict & ~(1u << p));  // Edict tokens leave the game too
    pl.captured.clear();
    for (size_t i = 0; i < G.laws.size();)
        if (G.laws[i].owner == p) discardLaw(int(i), "its owner was eliminated");
        else ++i;
    E.rng().shuffle(G.secretDeck);
    pl.eliminated = true;
    pl.passed = true;
    Event e;
    e.kind = EventKind::Eliminated;
    e.player = int8_t(p);
    e.text = pn(p) + " has been eliminated from the game.";
    E.emit(e);
    if (G.speaker == p) {
        std::vector<int> left = clockwiseFrom(G, (p + 1) % G.numPlayers);
        if (!left.empty()) G.speaker = int8_t(left[0]);
    }
    std::vector<int> alive = initiativeOrder(G);
    if (alive.size() == 1) {
        G.winner = int8_t(alive[0]);
        throw GameOverSignal();
    }
}

void Rules::feedNote(int player, const std::string& text) {
    Event e;
    e.kind = EventKind::Note;
    e.player = int8_t(player);
    e.text = text;
    E.emit(std::move(e));
}

}  // namespace ti
