#include "ai/ai.h"
#include "ai/ai_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/deal.h"
#include "engine/personality.h"
#include "engine/query.h"
#include "engine/rules.h"

namespace ti {

namespace {

// ============================================================ small helpers
bool keyIs(const char* a, const char* b) { return !std::strcmp(a, b); }

int firstEnabled(const Decision& d) {
    for (const Option& o : d.options)
        if (o.enabled) return o.id;
    return d.options.empty() ? 0 : d.options[0].id;
}
bool hasOption(const Decision& d, int id) {
    for (const Option& o : d.options)
        if (o.id == id && o.enabled) return true;
    return false;
}
bool isYesNo(const Decision& d) { return d.options.size() == 2 && hasOption(d, 1) && hasOption(d, 0); }

Answer pickOne(int id) {
    Answer a;
    a.picks.push_back(id);
    return a;
}

float expectedHits(const Game& g, int p, const Force& f, bool space) {
    float e = 0, miss = 1.f;
    // (Sardakk review, owner 2026-11-11) Unrelenting: +1 to each of their units' combat rolls
    const bool seat = p >= 0 && p < g.numPlayers;
    const int mod = seat && aiTune("unrelodds", 1.f) > 0.f && isFaction(g, p, "sardakk") ? 1 : 0;
    // C'morran N'orr (the same review): +1 to the other ships' combat rolls while it is in the fleet
    const bool cmorran = space && seat && f.n[int(UnitType::Flagship)] > 0 && aiTune("cmorranodds", 1.f) > 0.f && flagshipAbility(g, p, "sardakk");
    // (the Jol-Nar review, checkpoint 2 - switch fragodds, test) Fragile: -1 to their combat rolls (infantry beside a Shield
    // Paling mech excepted); J.N.S. Hylarim: each natural 9 or 10 makes 2 more hits (0.4 hits a die)
    const bool frag = seat && aiTune("fragodds", 0.f) > 0.f && isFaction(g, p, "jolnar");
    const bool paling = frag && !space && f.n[int(UnitType::Mech)] > 0 && mechAbilities(g);
    if (frag && space && f.n[int(UnitType::Flagship)] > 0 && flagshipAbility(g, p, "jolnar"))
        e += 0.4f * float(f.n[int(UnitType::Flagship)] * std::max<int>(1, unitDef(g, p, UnitType::Flagship).combatDice));
    for (int t = 0; t < kUnitTypes; ++t) {
        if (!f.n[t]) continue;
        UnitType ut = UnitType(t);
        if (space ? !isShip(ut) : !isGroundForce(ut)) continue;
        const UnitDef& u = unitDef(g, p, ut);
        if (u.combat <= 0) continue;
        const int m = mod + (cmorran && ut != UnitType::Flagship ? 1 : 0) - (frag && !(paling && ut == UnitType::Infantry) ? 1 : 0);
        float chance = std::max(0.f, std::min(1.f, (11.f - u.combat + m) / 10.f));
        e += f.n[t] * chance * std::max<int>(1, u.combatDice);
        for (int k = 0; k < f.n[t] * std::max<int>(1, u.combatDice); ++k) miss *= 1.f - chance;
    }
    if (!space && x89Omega(g, p)) e *= 2.f;  // X-89 Bacterial Weapon (Thunder's Edge's Codex): twice the ground hits
    // Valkyrie Particle Weave (0.44.8): 1 more ground combat hit in a round where the opponent scored any
    (void)miss;
    if (!space && e > 0.f && p >= 0 && p < g.numPlayers && ownsTechKey(g, p, "vpw")) e += 0.6f;
    return e;
}

float hitPoints(const Game& g, int p, const Force& f, bool space) {
    float h = 0;
    for (int t = 0; t < kUnitTypes; ++t) {
        if (!f.n[t]) continue;
        UnitType ut = UnitType(t);
        if (space ? !isShip(ut) : !isGroundForce(ut)) continue;
        h += f.n[t];
        // (0.44.8: Non-Euclidean Shielding - one Sustain Damage cancels 2 hits)
        if (unitSustains(g, p, ut)) h += float((f.n[t] - f.dmg[t]) * (p >= 0 && p < g.numPlayers ? sustainWorth(g, p) : 1));
    }
    return h;
}

// 0.42.0 (#187: bots sent lone carriers past enemy PDS and lost them every round): the hits other
// players' space cannon would score against our ships moving into sys - units with space cannon in
// the system, and those next door that reach (PDS II and the like)
float cannonThreat(const Game& g, int p, int sys) {
    float e = 0;
    std::vector<int> around{sys};
    for (int16_t a : g.systems[size_t(sys)].adj)
        if (a != sys) around.push_back(a);
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p || g.players[q].eliminated) continue;
        for (int s2 : around) {
            const SystemState& S = g.systems[size_t(s2)];
            auto add = [&](const Force& f) {
                for (int t = 0; t < kUnitTypes; ++t) {
                    if (!f.n[t]) continue;
                    const UnitDef& u = unitDef(g, q, UnitType(t));
                    if (u.cannon <= 0 || (s2 != sys && !u.deepCannon)) continue;
                    e += f.n[t] * std::max<int>(1, u.cannonDice) * std::max(0.f, std::min(1.f, (11.f - u.cannon) / 10.f));
                }
            };
            add(S.space[size_t(q)]);
            for (int16_t x : S.planets) add(g.planets[size_t(x)].units[size_t(q)]);
        }
    }
    return e;
}

// Early economy (owner, 0.14.0: bots chased objectives but left the planets
// around their home empty): in the first rounds a planet is worth its
// resources and influence again, most of all next to our home and next to
// what we already hold - a growing economy and a compact border around home.
// Fades out by round 5, when objectives take over.
// (0.59.0) the Crimson have no space dock out on the board yet (their home is beside it); (0.59.1, owner: the Creuss
// expand slowly) the Creuss too - their home is beside the board as well
bool crimsonNeedsDock(const Game& g, int p) {
    if (!isFaction(g, p, "crimson") && !(isFaction(g, p, "ghost") && aiTune("creussdock", 1.f) > 0.f)) return false;
    for (const PlanetState& ps : g.planets)
        if (ps.owner == p && ps.units[size_t(p)].count(UnitType::SpaceDock) && g.systems[size_t(ps.system)].home != p) return false;
    return true;
}

float economyValue(const Game& g, int p, int pl) {
    static const float kRound[6] = {1.f, 1.f, 0.85f, 0.6f, 0.3f, 0.1f};
    const float k = kRound[std::min(5, std::max(0, g.round))] * aiTune("econ", 1.f);
    const PlanetState& ps = g.planets[pl];
    const int home = g.players[p].homeSystem;
    if (k <= 0.f || ps.owner == p || home < 0) return 0.f;
    const int sys = ps.system;
    int dist = 3;
    if (!g.systems[home].offBoard) dist = hexDistance(g.systems[sys].pos, g.systems[home].pos);
    else if (std::find(g.systems[home].adj.begin(), g.systems[home].adj.end(), int16_t(sys)) != g.systems[home].adj.end()) dist = 1;
    else  // (0.55.0: a home beside the board - the Creuss, the Crimson - is measured from its gate / the Sorrow, where
          // its slice lies; it was "far" before, so those bots valued the planets round their gate at a quarter)
        for (int16_t a : g.systems[home].adj)
            if (!g.systems[a].offBoard) dist = std::min(dist, hexDistance(g.systems[sys].pos, g.systems[a].pos));
    float near = dist <= 1 ? 1.7f : dist == 2 ? 1.1f : 0.45f;
    if (dist > 1)  // next to a system where we hold a planet: the border grows outward, in one piece
        for (int16_t a : g.systems[sys].adj) {
            bool ours = false;
            for (int16_t x : g.systems[a].planets)
                if (g.planets[x].owner == p) ours = true;
            if (ours) {
                near += 0.35f;
                break;
            }
        }
    // (0.59.1) the Creuss home has 2 influence: the custodians (6) and influence objectives need it from the planets
    const float infW = isFaction(g, p, "ghost") ? aiTune("creussinf", 1.25f) : 0.8f;
    const float worth = float(planetResources(g, pl)) + infW * float(planetInfluence(g, pl)) + 0.6f;
    // taking a planet from someone is a fight, valued elsewhere: only a little here
    return worth * near * k * (ps.owner >= 0 ? 0.3f : 1.2f);
}

float planetValue(const Game& g, int p, int pl) {
    const PlanetState& ps = g.planets[pl];
    const PlanetDef& d = kPlanets[ps.def];
    const int res = planetResources(g, pl), inf = planetInfluence(g, pl);
    float v = res + inf * 0.8f + 0.8f;
    if (d.skip != TechColor::None) v += 1.0f;
    if (d.legendary) v += 2.0f;
    const bool mecatol = !std::strcmp(d.key, "mr");
    if (mecatol) v += 3.0f;
    // Styx (Thunder's Edge): a victory point while held - a seat (0.44.2, tester: "solidify Mecatol Rex and Styx")
    if (!std::strcmp(d.key, "styx") && aiTune("seatstyx", 1.f) > 0.f)
        v += aiLevelScale(g) > 0.f ? kVP * 0.8f * aiLevelScale(g) * aiTune("seatstyx", 1.f) : 4.f;
    const int sys = ps.system;
    const int h = g.systems[sys].home;
    if (h >= 0 && h != p) v += 2.0f;  // conquering home planets scores objectives
    v += aiGroundPlanet(g, p, pl);     // the groundwork: our slice first, the equidistants raced for
    if (aiLevelScale(g) <= 0.f) {
        // Easy: the old fixed preferences only
        for (int16_t o : g.publicObjectives) {
            if (hasScored(g, p, o)) continue;
            const char* k = kObjectives[o].key;
            if ((keyIs(k, "corner") || keyIs(k, "unify_colonies")) && d.trait != Trait::None) v += 1.0f;
            if ((keyIs(k, "research_outposts") || keyIs(k, "brain_trust")) && d.skip != TechColor::None) v += 2.0f;
            if ((keyIs(k, "expand_borders") || keyIs(k, "subdue")) && h < 0) v += 1.5f;
        }
        return v;
    }
    // what this planet does for our objectives (public and secret)
    const Goals& G = aiGoals(g, p);
    for (int t = 1; t <= 3; ++t)
        if (hasTraitOf(g, pl, Trait(t))) v += G.trait[t];
    if (planetHasSkip(g, pl)) v += G.skip;
    if (!ps.attachments.empty()) v += G.attach;
    else if (ps.owner < 0 && d.trait != Trait::None) v += G.attach * 0.3f;  // exploring it may attach something
    if (d.legendary) v += G.legendary;
    if (!isHomePlanet(g, pl)) v += G.nonHome;
    if (h >= 0 && h != p) v += G.otherHome;
    bool nearOther = h >= 0 && h != p;
    for (int16_t a : g.systems[sys].adj)
        if (g.systems[a].home >= 0 && g.systems[a].home != p) nearOther = true;
    if (nearOther) v += G.nearOtherHome;
    v += G.anyPlanet + G.resPts * res + G.infPts * inf;
    if (mecatol) v += G.mecatol;
    for (int16_t x : g.systems[sys].planets)
        if (x != pl && g.planets[x].owner >= 0 && g.planets[x].owner != p) {
            v += G.sharedSystem;
            break;
        }
    if (G.leader >= 0 && ps.owner == G.leader) v += G.leaderThreat;
    {  // (0.55.0) stop the winner: a planet their win rests on
        const Endgame& E = aiEndgame(g, p);
        if (E.threat >= 0 && ps.owner == E.threat && size_t(pl) < E.planet.size()) v += E.planet[size_t(pl)];
    }
    v += economyValue(g, p, pl);
    v += aiCorePlanetBonus(g, p, pl);  // 0.43.0: the strategy guides
    return v;
}

int maxNonFighterShipsOf(const Game& g, int p) {
    int m = 0;
    for (const SystemState& s : g.systems) m = std::max(m, s.space[p].nonFighterShips());
    return m;
}

int largestFleet(const Game& g, int p) {
    int m = 0;
    for (size_t s = 0; s < g.systems.size(); ++s) m = std::max(m, fleetUseIn(g, p, int(s)));
    return m;
}

bool enemyNear(const Game& g, int p, int sys, int steps) {
    for (int s : systemsWithin(g, sys, steps))
        if (otherShipsIn(g, p, s)) return true;
    return false;
}

// Resources / influence / trade goods the AI wants to keep untouched so it can
// pay for a "spend" objective in the coming status phase.
struct Reserve {
    int resources = 0, influence = 0, tradeGoods = 0;
};
Reserve objectiveReserve(const Game& g, int p) {
    Reserve best;
    if (aiLevelScale(g) > 0.f) {
        const Goals& G = aiGoals(g, p);
        best.resources = G.keepRes;
        best.influence = G.keepInf;
        best.tradeGoods = std::min<int>(g.players[p].tradeGoods, G.keepTg + aiStyle(g, p).tgKeep);
        return best;
    }
    int bestPoints = 0;
    int totalRes = g.players[p].tradeGoods, totalInf = g.players[p].tradeGoods;
    for (int x : controlledPlanets(g, p)) {
        totalRes += planetResources(g, x);
        totalInf += planetInfluence(g, x);
    }
    for (int16_t o : g.publicObjectives) {
        if (hasScored(g, p, o)) continue;
        SpendNeed n = objectiveSpend(o);
        if (!(n.resources || n.influence || n.tradeGoods)) continue;
        if (n.tradeGoods > g.players[p].tradeGoods) continue;
        if (n.resources + n.tradeGoods > totalRes || n.influence + n.tradeGoods > totalInf) continue;
        if (n.resources + n.influence + n.tradeGoods > totalRes + totalInf - g.players[p].tradeGoods) continue;
        if (kObjectives[o].points > bestPoints) {
            bestPoints = kObjectives[o].points;
            best.resources = n.resources;
            best.influence = n.influence;
            best.tradeGoods = n.tradeGoods;
        }
    }
    return best;
}

// Extra value for getting units into a system because of an objective we
// don't have yet (presence objectives: "have ships in ...").
float presenceValue(const Game& g, int p, int sys) {
    if (aiLevelScale(g) > 0.f) return aiPresenceValue(g, p, sys, true) * aiTune("pres", 1.f);
    if (hasShips(g, p, sys)) return 0;
    const SystemState& s = g.systems[sys];
    int mec = g.mecatol();
    bool nextToMecatol = mec >= 0 && adjacentSystems(g, sys, mec);
    bool special = s.d().anomalies != 0 || sys == mec;
    for (int16_t pl : s.planets)
        if (kPlanets[g.planets[pl].def].legendary) special = true;
    bool noPlanets = s.planets.empty();
    bool edge = s.edge && s.home != p;
    bool nextToEnemyHome = false, nextToAnomaly = false;
    for (int16_t a : s.adj) {
        if (g.systems[a].home >= 0 && g.systems[a].home != p) nextToEnemyHome = true;
        if (g.systems[a].d().anomalies) nextToAnomaly = true;
    }
    float v = 0;
    auto want = [&](int o, bool fits, float w) {
        if (fits && !hasScored(g, p, o) && !meetsObjective(g, p, o)) v += w * kObjectives[o].points;
    };
    for (int16_t o : g.publicObjectives) {
        const char* k = kObjectives[o].key;
        if (keyIs(k, "intimidate")) want(o, nextToMecatol, 2.5f);
        else if (keyIs(k, "make_history") || keyIs(k, "become_legend")) want(o, special, 2.0f);
        else if (keyIs(k, "deep_space") || keyIs(k, "vast_territories")) want(o, noPlanets, 1.5f);
        else if (keyIs(k, "outer_rim") || keyIs(k, "control_borderlands")) want(o, edge, 1.5f);
    }
    for (int16_t o : g.players[p].secrets) {
        const char* k = kObjectives[o].key;
        if (keyIs(k, "ctr")) want(o, true, 1.5f);
        else if (keyIs(k, "te")) want(o, nextToEnemyHome, 3.0f);
        else if (keyIs(k, "lsc")) want(o, nextToAnomaly, 2.0f);
        else if (keyIs(k, "btgk")) want(o, (s.d().wormholes & (kAlpha | kBeta)) != 0, 2.0f);
    }
    return v;
}

}  // namespace

float fightOdds(const Game& g, int att, const Force& a, int def, const Force& d, bool space) {
    float ea = expectedHits(g, att, a, space), ed = expectedHits(g, def, d, space);
    float ha = hitPoints(g, att, a, space), hd = hitPoints(g, def, d, space);
    // (Sardakk review, owner 2026-11-11) Valkyrie Exoskeleton: each Sardakk mech that can still sustain strikes back
    // once when it does (about 0.7 of a hit - it has to be hit first; never a sure win on its own)
    if (!space && hd > 0 && ha > 0 && mechAbilities(g) && aiTune("valkodds", 1.f) > 0.f) {
        auto strike = [&](int p, const Force& f) {
            const int m = int(UnitType::Mech);
            return p >= 0 && p < g.numPlayers && isFaction(g, p, "sardakk") ? 0.7f * float(f.n[m] - f.dmg[m]) : 0.f;
        };
        if (const float s = strike(att, a)) hd = std::max(0.5f, hd - s);
        if (const float s = strike(def, d)) ha = std::max(0.5f, ha - s);
    }
    // (the Mahact review, checkpoint 2 - test knob rexodds) Arvicon Rex: +2 to the flagship's rolls against an opponent
    // whose command token isn't in the Mahact's fleet pool (0.2 more hits per die). OFF: it changes almost nothing (90
    // games 14 wins / 6.87 VP against 14 / 6.84; fresh seeds 7 / 6.43 both)
    if (space && aiTune("rexodds", 0.f) > 0.f) {
        auto rex = [&](int p, const Force& f, int opp) {
            if (p < 0 || p >= g.numPlayers || !f.n[int(UnitType::Flagship)] || !flagshipAbility(g, p, "mahact")) return 0.f;
            if (opp >= 0 && opp < g.numPlayers && ((g.players[size_t(p)].edict >> opp) & 1)) return 0.f;
            return 0.2f * float(f.n[int(UnitType::Flagship)] * std::max<int>(1, unitDef(g, p, UnitType::Flagship).combatDice));
        };
        ea += rex(att, a, def);
        ed += rex(def, d, att);
    }
    if (hd <= 0) return 1.f;
    if (ha <= 0) return 0.f;
    if (ed <= 0.01f) return ea > 0 ? 0.98f : 0.5f;
    if (ea <= 0.01f) return 0.02f;
    float tA = hd / ea, tD = ha / ed;  // rounds each side needs
    float r = tD / tA;
    return (r * r) / (1.f + r * r);
}

namespace {

bool controlsMecatol(const Game& g, int p);
Answer aiProduce(const Game& g, const Decision& d);

// Would we buy anything if we produced in sys now? (asks the production code itself)
bool wouldProduce(const Game& g, int p, int sys, int extra = 0) {
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Produce;
    d.tag = Tag::Production;
    d.system = sys;
    d.amount = productionCapacity(g, p, sys) + extra;
    if (d.amount <= 0) return false;
    Answer a = aiProduce(g, d);
    for (int t = 0; t < kUnitTypes && t < int(a.data.size()); ++t)
        if (a.data[t] > 0) return true;
    return false;
}

}  // namespace

// (the Muaat review, checkpoint 2 - test knob umbatplan) Would Umbat's production buy anything now? Asks the production
// code with Umbat's limits (2 units, each costing 4 or less) in each system with our war sun or flagship. (The real one
// may also put ground forces on any of our planets there: this is the cautious side.)
int aiUmbatSystem(const Game& g, int p) {
    int best = -1, bestCost = 0;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        const Force& f = g.systems[s].space[size_t(p)];
        if (!f.count(UnitType::WarSun) && !f.count(UnitType::Flagship)) continue;
        Decision d;
        d.player = p;
        d.kind = DecisionKind::Produce;
        d.tag = Tag::Production;
        d.system = int(s);
        d.amount = 2;
        d.context = "Up to 2 units. Only units that cost 4 or less.";
        int mask = 0;
        for (int t = 0; t < kUnitTypes; ++t)
            if (unitCost2(g, p, UnitType(t)) <= 8) mask |= 1 << t;
        d.hitMask = mask;
        Answer a = aiProduce(g, d);
        int cost = 0;  // (what it buys there, in production cost)
        for (int t = 0; t < kUnitTypes && t < int(a.data.size()); ++t) cost += a.data[size_t(t)] * unitCost2(g, p, UnitType(t));
        if (cost > bestCost) bestCost = cost, best = int(s);
    }
    return best;
}
bool aiUmbatWorth(const Game& g, int p) { return aiUmbatSystem(g, p) >= 0; }

namespace {

// Competitive: how much nearer to our objectives the board would be after a move.
// Plays the move out on a copy (ships in, unopposed planets taken) and scores every
// open public objective and held secret: met = its points, otherwise part credit.
float objectiveScore(const Game& g, int p) {
    float v = 0;
    auto add = [&](int o) {
        const float pts = float(kObjectives[o].points) * kVP;
        if (meetsObjective(g, p, o)) {
            v += pts;
            return;
        }
        ObjProgress pr = objectiveProgress(g, p, o);
        if (pr.need > 0) v += pts * 0.55f * float(std::min(pr.have, pr.need)) / float(pr.need);
    };
    for (int16_t o : g.publicObjectives)
        if (!hasScored(g, p, o)) add(o);
    for (int16_t o : g.players[p].secrets) add(o);
    return v;
}

float objectiveDelta(const Game& g, int p, int sys, const std::vector<int>& move) {
    if (move.empty()) return 0.f;
    Game h = g;
    Force& dst = h.systems[sys].space[p];
    for (size_t i = 0; i + 3 < move.size(); i += 4) {
        const int from = move[i], loc = move[i + 1], t = move[i + 2], k = move[i + 3];
        if (from < 0 || from >= int(h.systems.size()) || t < 0 || t >= kUnitTypes || k <= 0) continue;
        Force& src = loc >= 0 ? h.planets[loc].units[p] : h.systems[from].space[p];
        const int n = std::min<int>(k, src.n[t]);
        src.remove(UnitType(t), n, false);
        dst.add(UnitType(t), n);
    }
    // enemy ships: assume we win only a clearly favourable fight
    for (int q = 0; q < kSides; ++q)  // (kNeutral: Thunder's Edge's neutral units)
        if (q != p && (q < h.numPlayers || q == kNeutral) && hasShips(h, q, sys)) {
            if (fightOdds(g, p, dst, q, h.systems[sys].space[q], true) < 0.65f) return 0.f;
            h.systems[sys].space[q] = Force();
        }
    // land on planets nobody defends, one ground force each
    for (int16_t pl : h.systems[sys].planets) {
        if (h.planets[pl].owner == p || dst.groundForces() == 0) continue;
        bool defended = h.planets[pl].units[kNeutral].groundForces() > 0;
        for (int q = 0; q < h.numPlayers; ++q)
            if (q != p && h.planets[pl].units[q].groundForces()) defended = true;
        if (defended) continue;
        const UnitType gt = dst.count(UnitType::Infantry) ? UnitType::Infantry : UnitType::Mech;
        dst.remove(gt, 1, false);
        h.planets[pl].units[p].add(gt, 1);
        h.planets[pl].owner = int8_t(p);
    }
    return objectiveScore(h, p) - objectiveScore(g, p);
}

// ============================================================ tactical planning
struct Plan {
    int system = -1;
    float value = 0;
    std::vector<int> move;
};

// (Sardakk review, owner 2026-11-11 - test A) the ground forces a Sardakk landing needs: the fewest infantry with ground
// odds of sardodds (0.7) - Unrelenting, Valkyrie Particle Weave and the mech counted - instead of 2 per defender + 1
int sardakkNeed(const Game& g, int p, int pl, int fixedNeed) {
    if (fixedNeed <= 1 || aiTune("sardgodds", 0.f) <= 0.f || !isFaction(g, p, "sardakk")) return fixedNeed;
    Force def;
    int dq = g.planets[size_t(pl)].owner >= 0 && g.planets[size_t(pl)].owner != p ? g.planets[size_t(pl)].owner : -1, most = 0;
    for (int q = 0; q < kSides; ++q) {
        if (q == p) continue;
        const Force& f = g.planets[size_t(pl)].units[size_t(q)];
        for (UnitType t : {UnitType::Infantry, UnitType::Mech}) {
            def.add(t, f.count(t));
            def.dmg[int(t)] = uint8_t(def.dmg[int(t)] + f.dmg[int(t)]);
        }
        if (dq < 0 && f.groundForces() > most) most = f.groundForces(), dq = q;
    }
    if (def.groundForces() == 0) return fixedNeed;
    if (dq < 0) dq = kNeutral;
    const float bar = aiTune("sardodds", 0.7f);
    for (int n = 1; n < fixedNeed; ++n) {
        Force a;
        a.add(UnitType::Infantry, n);
        if (fightOdds(g, p, a, dq, def, false) >= bar) return n + (fixedNeed - (def.groundForces() * 2 + 1) > 0 ? fixedNeed - (def.groundForces() * 2 + 1) : 0);
    }
    return fixedNeed;
}

// does p have a mech anywhere on the board (the Firmament's Viper plans)?
static bool aiHasMechOut(const Game& g, int p) {
    for (const SystemState& s : g.systems)
        if (s.space[size_t(p)].count(UnitType::Mech)) return true;
    for (const PlanetState& ps : g.planets)
        if (ps.units[size_t(p)].count(UnitType::Mech)) return true;
    return false;
}

// (2026-11-12, found in the Sardakk review) A plan that takes away the ships whose capacity holds fighters or ground
// forces in another system's space strands them there: the move was refused and the plan dropped - most often the
// strike of an operation ("everything in reach goes in") whose carrier also held the fighters next to it. Take the
// stranded units along when the move stays legal, else leave enough capacity behind (the weakest ships with capacity).
static std::vector<int> aiUnstrand(const Game& g, int p, int sys, std::vector<int> mv) {
    auto leftAt = [&](const std::vector<int>& m, int s) {
        Force left = g.systems[size_t(s)].space[size_t(p)];
        for (size_t i = 0; i + 3 < m.size(); i += 4)
            if (m[i] == s && m[i + 1] < 0) left.n[size_t(m[i + 2])] = uint8_t(std::max(0, int(left.n[size_t(m[i + 2])]) - m[i + 3]));
        return left;
    };
    auto stranded = [&](const std::vector<int>& m, int s) { return overflowOf(g, p, s, leftAt(m, s)); };
    auto addOne = [](std::vector<int>& m, int s, int t) {
        for (size_t i = 0; i + 3 < m.size(); i += 4)
            if (m[i] == s && m[i + 1] < 0 && m[i + 2] == t) {
                ++m[i + 3];
                return;
            }
        m.insert(m.end(), {s, -1, t, 1});
    };
    std::vector<int> srcs;
    for (size_t i = 0; i + 3 < mv.size(); i += 4)
        if (mv[i + 1] < 0 && mv[i] != sys && mv[i] >= 0 && mv[i] < int(g.systems.size()) &&
            std::find(srcs.begin(), srcs.end(), mv[i]) == srcs.end())
            srcs.push_back(mv[i]);
    for (int s : srcs) {
        if (capacityOverflow(g, p, s) > 0 || stranded(mv, s) <= 0) continue;
        // 1. along: fighters first, then ground forces
        std::vector<int> along = mv;
        Force left = leftAt(mv, s);
        for (UnitType t : {UnitType::Fighter, UnitType::Infantry, UnitType::Mech})
            while (left.n[int(t)] > 0 && stranded(along, s) > 0) {
                addOne(along, s, int(t));
                --left.n[int(t)];
            }
        if (stranded(along, s) <= 0 && checkMovePlan(g, p, sys, along, true, true).empty()) {
            mv = along;
            continue;
        }
        // 2. leave capacity behind (the weakest ships with capacity), with the load it held there
        std::vector<int> stay = mv;
        while (stranded(stay, s) > 0) {
            int bi = -1, bc = -1, bcap = 0;
            for (size_t i = 0; i + 3 < stay.size(); i += 4) {
                if (stay[i] != s || stay[i + 1] >= 0 || !isShip(UnitType(stay[i + 2]))) continue;
                const UnitDef& u = unitDef(g, p, UnitType(stay[i + 2]));
                if (u.capacity <= 0) continue;
                if (u.combat > bc || (u.combat == bc && u.capacity > bcap)) {
                    bi = int(i);
                    bc = u.combat;
                    bcap = u.capacity;
                }
            }
            if (bi < 0) break;
            if (--stay[size_t(bi) + 3] == 0) stay.erase(stay.begin() + bi, stay.begin() + bi + 4);
        }
        if (stranded(stay, s) > 0) continue;
        // ... its load from there stays too (as much as fits with what stays)
        auto dropOne = [&](std::vector<int>& m, size_t i) {
            if (--m[i + 3] == 0) m.erase(m.begin() + long(i), m.begin() + long(i) + 4);
        };
        for (UnitType t : {UnitType::Mech, UnitType::Infantry, UnitType::Fighter}) {
            for (bool more = true; more;) {
                more = false;
                for (size_t i = 0; i + 3 < stay.size(); i += 4)
                    if (stay[i] == s && stay[i + 1] < 0 && stay[i + 2] == int(t)) {
                        std::vector<int> back = stay;
                        dropOne(back, i);
                        if (stranded(back, s) <= 0 && checkMovePlan(g, p, sys, stay, true, true).rfind("Not enough capacity", 0) == 0) {
                            stay = back;
                            more = true;
                        }
                        break;
                    }
            }
        }
        // ... and pick-ups from planets come off while the ships that still go can't carry them (last ones first)
        auto cargoShort = [&](const std::vector<int>& m) {
            const std::string e = checkMovePlan(g, p, sys, m, true, true);
            return e.rfind("Not enough capacity", 0) == 0 || e.rfind("No moving ship with capacity", 0) == 0;
        };
        for (int guard = 0; guard < 40 && cargoShort(stay); ++guard) {
            size_t last = stay.size();
            for (size_t i = 0; i + 3 < stay.size(); i += 4)
                if (stay[i + 1] >= 0) last = i;
            if (last >= stay.size()) break;
            dropOne(stay, last);
        }
        if (stranded(stay, s) <= 0) mv = stay;
    }
    return mv;
}

// Builds a movement plan into 'sys' and estimates its value.
Plan buildPlan(const Game& g, int p, int sys, bool forActivation) {
    Plan plan;
    plan.system = sys;
    const SystemState& target = g.systems[sys];
    if (target.isHyperlane()) return plan;
    if (forActivation && !canActivate(g, p, sys)) return plan;
    // (fixes 26, the Mahact review) Il Na Viroset now ends the turn at the activation: nothing to plan in such a system
    if (forActivation && g.fixes >= 26 && g.systems[size_t(sys)].hasToken(p)) return plan;
    if (aiPromiseForbids(g, p, sys)) return plan;  // we promised to stay out
    if (target.home >= 0 && target.home != p && isFaction(g, target.home, "crimson")) return plan;  // Sundered: units die there
    const bool gd = moveBoost(g, p);  // Gravity Drive / Ionian Fuel Refinery

    // What's there?
    Force enemyShips;
    int enemy = -1;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && hasShips(g, q, sys)) {
            enemy = q;
            enemyShips = target.space[q];
        }
    if (enemy < 0 && neutralShipsIn(g, sys)) {  // Thunder's Edge: neutral ships fight like another player's
        enemy = kNeutral;
        enemyShips = target.space[kNeutral];
    }
    const bool neutralFoe = enemy == kNeutral;
    std::vector<int> targets;
    int gfNeeded = 0, freeTargets = 0;
    float prize = 0;
    const Style& S = aiStyle(g, p);
    // betrayal timing (0.25.0): at once below Competitive (by personality), Competitive when the economy allows
    const float urge = S.betrayTarget >= 0 ? aiBetrayUrge(g, p) : 1.f;
    // (the Firmament bot review, owner 2026-11-11 "let's make those changes"): Viper EX-23 coexists instead of fighting,
    // so one mech is all it takes to sit on another player's (non-home) planet - Viper Hollow takes it at the coup, and
    // coexisting counts as control for objectives meanwhile. viperDelta = what the plan loses if no mech comes along
    const bool viperOn = (g.modules & kModThundersEdge) && mechAbilities(g) && isFaction(g, p, "firmament") &&
                         aiTune("viperplan", 1.f) > 0.f && aiLevelScale(g) > 0.f && aiHasMechOut(g, p);
    int viperCount = 0, viperExtra = 0;
    float viperDelta = 0.f;
    for (int16_t pl : target.planets) {
        if (g.planets[pl].owner == p) continue;
        if (target.custodians && !g.custodiansRemoved && spendable(g, p, true) < aiCustodiansInf(g, p)) continue;
        int defenders = neutralGroundOn(g, pl);  // (neutral ground forces defend too)
        int magenHit = 0;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p) {
                defenders += g.planets[pl].units[q].groundForces();
                // (0.44.8) the Codex Magen Defense Grid: 1 more infantry per structure when we activate, and a free hit
                const int st = g.planets[pl].units[q].structures();
                if (st > 0 && codexOn(g) && ownsTechKey(g, q, "md_base")) {
                    defenders += std::min(st, unitsAvailable(g, q, UnitType::Infantry));
                    magenHit = 1;
                }
            }
        targets.push_back(pl);
        if (defenders == 0 && g.planets[pl].owner < 0) ++freeTargets;
        const int fightNeed = sardakkNeed(g, p, pl, (defenders > 0 ? defenders * 2 + 1 : 1) + magenHit);
        const int po = g.planets[pl].owner;
        const bool viper = viperOn && defenders > 0 && po >= 0 && po != p && !isHomePlanet(g, pl) && !((g.planets[pl].coexist >> p) & 1);
        gfNeeded += viper ? 1 : fightNeed;
        float pv = planetValue(g, p, pl) * (defenders > 0 ? 0.7f : 1.f);
        if (S.on) {  // personality: grabbing free planets vs taking them from someone
            const int o = g.planets[pl].owner;
            if (o < 0) {
                pv = pv * S.expansion + (kPlanets[g.planets[pl].def].trait != Trait::None ? S.explore : 0.f);
            } else {
                pv *= std::max(0.5f, std::min(2.5f, S.aggression * S.vs[o]));
                if (S.betrayTarget == o && S.betrayKind == int(Betrayal::TakePlanet)) pv += kVP * 0.6f * urge;
                // 0.44.2 (owner: "an attack of opportunity" - Mecatol Rex left open, everyone tries for it): a seat whose
                // holder has no ships there and at most 1 ground force
                if (aiSeatOpen(g, pl) && aiLevelScale(g) > 0.f)
                    pv += kVP * aiTune("seatopp", 0.7f) * aiLevelScale(g) * std::max(0.5f, std::min(1.5f, S.vs[o]));
                if (fquirkIs(g, p, "l1z1x", 1) && g.planets[pl].units[o].structures()) pv += 2.f;  // Assimilation Protocol
            }
        }
        if (viper) {
            const float vv = planetValue(g, p, pl) * aiTune("viperv", 0.6f);
            ++viperCount;
            viperExtra += fightNeed - 1;
            viperDelta += pv - vv;
            pv = vv;
        }
        prize += pv;
    }
    const float level = aiLevelScale(g);
    const Goals& G = aiGoals(g, p);
    if (target.custodians && !g.custodiansRemoved && spendable(g, p, true) >= aiCustodiansInf(g, p))
        prize += level > 0.f ? kVP * 1.3f : 5.f;  // a victory point (and Mecatol Rex for Imperial)
    // 0.44.2 (tester: nobody touched Mecatol Rex before mid round 3 - in the sims only 9% of round-2 turns had ground
    // forces that could reach it): influence for the custodians but none ready this turn - park a loaded ship in
    // Mecatol Rex's space (taken next round, and nobody else lands without a fight)
    int infAll = g.players[size_t(p)].tradeGoods;  // (exhausted planets too: ready again next round)
    for (int x : controlledPlanets(g, p)) infAll += planetInfluence(g, x);
    const bool custodiansLater = level > 0.f && target.custodians && !g.custodiansRemoved && spendable(g, p, true) < aiCustodiansInf(g, p) &&
                                 infAll >= aiCustodiansInf(g, p) &&
                                 g.round >= (level >= 0.99f ? 1 : 2) && aiTune("seatpark", 1.f) > 0.f;

    // Plan Ahead (0.53.0): the operation this bot works toward over several turns (ai_ops.cpp)
    const AiOp& op = aiOp(g, p);
    const bool opOn = op.kind != kOpNone && op.dist.size() == g.systems.size();
    const bool opHere = opOn && sys == op.target;
    // ... its ships in striking distance stay there - unless this plan is the operation's (the strike, or gathering
    // no further away) or a fight at home or next door
    const int myHome = g.players[size_t(p)].homeSystem;
    const bool atHome = enemy >= 0 && (target.home == p || (myHome >= 0 && adjacentSystems(g, sys, myHome)));
    const bool opHold = opOn && !opHere && !atHome && aiTune("ophold", 0.f) > 0.f;  // (a hard hold - off: the loss below weighs it instead)

    // Already here
    Force mine = target.space[p];
    int gfInSpace = mine.groundForces();
    // Combat Drones (Mirveda, Discordant Stars): their fighters become infantry when they invade
    const bool drones = (g.modules & kModDiscordant) && isFaction(g, p, "mirveda") && aiTune("drones", 1.f) > 0.f;
    if (drones) gfInSpace += mine.count(UnitType::Fighter);

    // Collect movable groups by distance.
    struct Group {
        int from, loc, type, count, dist;
    };
    std::vector<Group> ships, gfs;
    std::vector<Group> guards;  // (0.58.2) the calm home's one guard, when it can complete a two-planet claim
    // Ahk-Syl Siven (Nomad hero, 0.43.0 debug passes): this round the flagship may leave systems with our tokens - the
    // planner used to skip those systems altogether, so the hero did nothing for a bot
    const bool siven = heroActive(g, p, "nomad");
    // 0.44.7: Gravity Drive (and the like) boost only so many ships - more groups needing the +1 made the whole plan
    // fail the move check and be dropped
    int boostLeft = lightningDrives(g, p) ? 99 : boostShips(g, p);
    for (size_t s = 0; s < g.systems.size(); ++s) {
        const bool flagOnly = g.systems[s].hasToken(p) && siven && g.systems[s].space[p].count(UnitType::Flagship) > 0;
        if (int(s) == sys || (g.systems[s].hasToken(p) && !flagOnly && !g.fx.dominusOrb) || g.systems[s].isHyperlane()) continue;
        const Force& f = g.systems[s].space[p];
        // keep one ship at home when enemies are close, and where a presence objective needs it
        // (the groundwork, 0.25.0: the hold value is already scaled by the difficulty, so is the bar -
        // Normal bots used to drift away from the first of two Intimidate the Council systems)
        bool homeGuard = (g.systems[s].home == p && enemyNear(g, p, int(s), 2)) ||
                         (level > 0.f && f.nonFighterShips() > 0 &&
                          aiPresenceHold(g, p, int(s)) >= kVP * 0.4f * (aiGroundScale(g, p) > 0.f ? level : 1.f));
        // early on, a home fleet with no warship (Muaat's lone war sun) only stays for an enemy
        // right next door: it is how the ground forces get out to the planets around home
        if (homeGuard && level > 0.f && g.round <= 2 && g.systems[s].home == p && !enemyNear(g, p, int(s), 1)) {
            bool warship = false;
            for (int t : kShipIdx)
                if (t != int(UnitType::Fighter) && f.n[t] && unitDef(g, p, UnitType(t)).capacity <= 0) warship = true;
            if (!warship) homeGuard = false;
        }
        // personalities that guard home (Turtler, Rusher, Homebody...) keep more there
        int keep = homeGuard ? 1 : 0;
        if (g.systems[s].home == p && level > 0.f && !enemyNear(g, p, int(s), 1)) {
            // 0.17.0 (owner: Rusher / Swindler "don't expand into their slice"): with no enemy
            // right next door, carriers never stand guard - they take the planets around home.
            // A Swindler's carrier + cruiser start used to stay home whenever any ship was 2 away.
            int warships = 0;
            for (int t : kShipIdx)
                if (t != int(UnitType::Fighter) && f.n[t] && unitDef(g, p, UnitType(t)).capacity <= 0) warships += f.n[t];
            keep = std::min(keep, warships);
            if (S.on) {
                int guard = std::min(S.homeGuard + (homeGuard ? 1 : 0), warships);
                if (g.round <= 2) guard = std::min(guard, 1);  // the slice comes first
                keep = std::max(keep, guard);
            }
        } else if (g.systems[s].home == p && S.on) {
            int guard = S.homeGuard + (homeGuard ? 1 : 0);
            if (!homeGuard && level > 0.f) {
                int warships = 0;
                for (int t : kShipIdx)
                    if (t != int(UnitType::Fighter) && f.n[t] && unitDef(g, p, UnitType(t)).capacity <= 0) warships += f.n[t];
                guard = std::min(guard, warships);
            }
            keep = std::max(keep, guard);
        }
        // a Saar floating factory out in the galaxy keeps an escort while enemies are about
        // (it is destroyed if it is ever blockaded, LRR 79.4a)
        if (g.systems[s].home != p && f.count(UnitType::SpaceDock) && unitDef(g, p, UnitType::SpaceDock).spaceOnly &&
            enemyNear(g, p, int(s), 2))
            keep = std::max(keep, 1);
        // 0.44.2 (tester: the Saar don't fortify their factories): 2 when an enemy is right next door (Hard and up) - OFF since
        // the Saar review (owner 2026-11-13, checkpoint 2: 53 wins vs 39 in 360 games without it; saarfort=1 brings it back)
        if (g.systems[s].home != p && f.count(UnitType::SpaceDock) && unitDef(g, p, UnitType::SpaceDock).spaceOnly && level >= 0.99f &&
            enemyNear(g, p, int(s), 1) && aiTune("saarfort", 0.f) > 0.f)
            keep = std::max(keep, 2);
        // Mobile Command (Ghoti, Discordant Stars): their home goes wherever the flagship goes - nothing stands guard
        if ((g.modules & kModDiscordant) && g.systems[s].home == p && isFaction(g, p, "ghoti")) keep = 0;
        if (int(s) == g.mecatol() && S.on && S.mecatol >= 1.4f) keep = std::max(keep, 1);  // Rusher, Heirs of Lazax: hold Mecatol Rex
        if (int(s) == g.mecatol() && aiPersona(p) == 2 && controlsMecatol(g, p)) keep = f.nonFighterShips();  // (test: a camper)
        if (int(s) == g.mecatol() && aiCompetitive(g) && controlsMecatol(g, p))
            keep = std::max(keep, enemyNear(g, p, int(s), 2) ? 2 : 1);  // Competitive: hold the seat of the empire
        // 0.44.2 (tester: "solidify Mecatol Rex and Styx"): every level above Easy keeps a ship on a seat it holds
        if (level > 0.f && aiTune("seatguard", 1.f) > 0.f && aiSeatHeldIn(g, p, int(s))) keep = std::max(keep, enemyNear(g, p, int(s), 2) ? 2 : 1);
        bool opKept = false;  // Plan Ahead: posted for the operation
        if (opHold && op.tierAt[s] >= 0 && op.tierAt[s] <= 1 && g.systems[s].home != p &&
            !(op.dist[size_t(sys)] >= 1 && op.dist[size_t(sys)] <= op.dist[s])) {
            keep = std::max(keep, f.nonFighterShips());
            opKept = true;
        }
        int left = f.nonFighterShips();
        for (int t : kShipIdx) {
            if (!f.n[t] || t == int(UnitType::Fighter)) continue;
            if (flagOnly && t != int(UnitType::Flagship)) continue;
            Reach r = shipReach(g, p, int(s), sys, UnitType(t), gd ? 1 : 0);
            if (!r.ok) continue;
            if (r.viaRift && aiCompetitive(g) && (unitDef(g, p, UnitType(t)).capacity > 0 || enemy < 0)) continue;
            // Lightning Drives (Gledge): ships moving 1 further can't carry - transports stay out of that
            if (r.needsBonus && lightningDrives(g, p) && unitDef(g, p, UnitType(t)).capacity > 0) continue;
            int k = std::min<int>(f.n[t], std::max(0, left - keep));
            if (r.vogul) k = std::min(k, vogulShips(g, p, int(s), UnitType(t)));  // Eye of Vogul: only the ships picked
            if (r.needsBonus) k = std::min(k, boostLeft);
            // 0.44.7: Rift Engines / Wraith Engine move ONE chosen ship - the rest of its kind there only if they reach
            // without it (the whole group used to go, fail the move check and cancel the plan)
            if ((g.modules & kModDiscordant) && k > 1 &&
                ((g.fx.dsRiftOwner == p && int(s) == g.fx.dsRiftSys && t == g.fx.dsRiftType) ||
                 (g.fx.dsWraithOwner == p && int(s) == g.fx.dsWraithSys && t == g.fx.dsWraithType)) &&
                !shipReachNoRift(g, p, int(s), sys, UnitType(t), gd ? 1 : 0).ok)
                k = 1;
            left -= k;
            if (r.needsBonus) boostLeft -= std::max(0, k);
            if (k > 0) ships.push_back({int(s), -1, t, k, r.viaRift ? 3 : 1});
        }
        if (flagOnly || opKept) continue;  // (what it carries: left to the people / kept for the operation)
        int gfSpace = f.groundForces();
        if (gfSpace) {
            if (f.count(UnitType::Infantry)) gfs.push_back({int(s), -1, int(UnitType::Infantry), f.count(UnitType::Infantry), 0});
            if (f.count(UnitType::Mech)) gfs.push_back({int(s), -1, int(UnitType::Mech), f.count(UnitType::Mech), 0});
        }
        if (drones && f.count(UnitType::Fighter)) gfs.push_back({int(s), -1, int(UnitType::Fighter), f.count(UnitType::Fighter), 0});
        // 0.42.0 (#187: Li-Zho, Hacan and many Discordant Stars factions start with infantry on 2-3 home
        // planets, and one stayed on EACH - a carrier left with one infantry): in the opening a calm home
        // keeps one ground force in all, on the dock planet (else the one with the most)
        int guardPlanet = -2;  // -2: one on each planet as before
        if (g.systems[s].home == p && level > 0.f && aiTune("homeguard1", 1.f) > 0.f && !enemyNear(g, p, int(s), 1) &&
            g.round <= int(aiTune("homeguardr", 2.f))) {
            int most = -1;
            guardPlanet = -1;
            for (int16_t pl : g.systems[s].planets) {
                const Force& pf = g.planets[pl].units[p];
                const int v = pf.groundForces() + (pf.count(UnitType::SpaceDock) ? 100 : 0);
                if (pf.groundForces() > 0 && v > most) most = v, guardPlanet = pl;
            }
        }
        for (int16_t pl : g.systems[s].planets) {
            const Force& pf = g.planets[pl].units[p];
            // 0.43.0 (debug passes): secrets that want units to stay put - Mechanize the Military (a lone mech stays
            // on its planet), Occupy the Fringe (a stack of 4+ away from our docks keeps growing)
            if (level > 0.f && sys != int(s)) {
                if (G.groundStack > 0.f && pf.groundForces() >= 4 && !pf.count(UnitType::SpaceDock)) continue;
                if (G.mech > 0.f && pf.count(UnitType::Mech) == 1 && pf.count(UnitType::Infantry) == 0) continue;
            }
            bool keepOne = guardPlanet >= -1 ? pl == guardPlanet : (g.systems[s].home == p || enemyNear(g, p, int(s), 1));
            int inf = pf.count(UnitType::Infantry) - (keepOne ? 1 : 0);
            if (keepOne && guardPlanet >= 0 && pl == guardPlanet && pf.count(UnitType::Infantry) > 0 && sys != int(s))
                guards.push_back({int(s), pl, int(UnitType::Infantry), 1, 0});
            if (pf.count(UnitType::Mech) && keepOne && inf < 0) inf = pf.count(UnitType::Infantry);
            if (inf > 0) gfs.push_back({int(s), pl, int(UnitType::Infantry), inf, 0});
            int mech = pf.count(UnitType::Mech) - ((keepOne && pf.count(UnitType::Infantry) == 0) ? 1 : 0);
            if (mech > 0) gfs.push_back({int(s), pl, int(UnitType::Mech), mech, 0});
        }
    }

    // Choose ships: warships to beat any enemy fleet, carriers for ground forces.
    std::vector<int> move;
    Force arriving;
    int capacityFrom[256] = {};  // per origin system (index < 256)
    int riftShips = 0;  // ships that come through a gravity rift (0.42.0)
    auto addShips = [&](const Group& gr, int k) {
        if (gr.dist >= 3 && gr.type != int(UnitType::Fighter)) riftShips += k;
        move.insert(move.end(), {gr.from, -1, gr.type, k});
        arriving.add(UnitType(gr.type), k);
        if (gr.from < 256) capacityFrom[gr.from] += k * unitDef(g, p, UnitType(gr.type)).capacity;
    };
    int fleet = fleetLimit(g, p);
    auto fleetRoom = [&]() { return fleet - (mine.nonFighterShips() + arriving.nonFighterShips()); };
    bool fight = enemy >= 0;
    if (fight) {
        Force total = mine;
        std::vector<Group> sorted = ships;
        std::sort(sorted.begin(), sorted.end(), [&](const Group& a, const Group& b) {
            return unitDef(g, p, UnitType(a.type)).combat < unitDef(g, p, UnitType(b.type)).combat;
        });
        // secrets scored by winning this fight (Normal / Hard)
        auto holds = [&](const char* k) {
            if (level < 0.99f) return false;  // Hard only
            for (int16_t o : g.players[p].secrets)
                if (keyIs(kObjectives[o].key, k)) return true;
            return false;
        };
        const bool wantDyp = holds("dyp");  // 3+ non-fighter ships at the end of a space combat
        const bool wantUf = holds("uf");    // win with the flagship
        bool flagshipJoined = mine.flagships() > 0;
        const bool opStrike = opHere && op.foe == enemy && aiTune("opstrike", 1.f) > 0.f;
        const bool hill = opStrike && op.kind == kOpMecatol;  // (Mecatol Rex: spare ships only - enough for a sure win, not all)
        for (const Group& gr : sorted) {
            bool more = fightOdds(g, p, total, enemy, enemyShips, true) <= (hill ? aiTune("ophillsure", 0.9f) : 0.8f);
            if (opStrike && !hill) more = true;  // Plan Ahead: the operation's strike - everything in reach goes in
            if (wantDyp && total.nonFighterShips() < 4) more = true;  // one spare in case of a loss
            if (wantUf && !flagshipJoined && isFlagship(UnitType(gr.type))) more = true;
            if (!more) continue;
            int k = std::min(gr.count, fleetRoom());
            if (k <= 0) break;
            addShips(gr, k);
            total.add(UnitType(gr.type), k);
            if (isFlagship(UnitType(gr.type))) flagshipJoined = true;
        }
        float odds = fightOdds(g, p, total, enemy, enemyShips, true);
        const bool vsLeader = enemy == G.leader;
        float secret = 0;
        if (level > 0.f) {
            int most = 0;
            for (const Player& q : g.players) most = std::max<int>(most, q.vp);
            if (wantDyp && total.nonFighterShips() >= 4) secret += kVP;
            if (wantUf && flagshipJoined) secret += kVP;
            if (holds("btv") && target.d().anomalies) secret += kVP;
            if (holds("dts") && target.home >= 0 && target.home != p) secret += kVP;
            if (holds("sar") && !neutralFoe && g.players[enemy].vp == most && most > 0) secret += kVP;
            if (holds("dtgs") && (enemyShips.flagships() || enemyShips.count(UnitType::WarSun))) secret += kVP;
        }
        float need = S.on ? S.minOdds : 0.6f;
        if (vsLeader || secret > 0) need -= 0.1f;
        if (S.betrayTarget == enemy) need -= 0.05f * std::min(2.f, urge);
        // Kingslay (0.43.0, owner): Mecatol Rex or the holder's home - a fair fight is enough
        const bool kingslay = S.betrayTarget == enemy && S.betrayKind == int(Betrayal::Kingslay) &&
                              (sys == g.mecatol() || (target.home >= 0 && target.home == enemy));
        if (kingslay) need = std::min(need, 0.45f);
        if (opStrike) need = std::min(need, aiTune("opneed", 0.65f));
        {  // (0.55.0) the endgame: their ships here are part of the threat's win; the last round fights at shorter odds
            const Endgame& E = aiEndgame(g, p);
            if (E.lastRound && aiTune("lastneed", 0.1f) > 0.f) need = std::max(0.4f, need - aiTune("lastneed", 0.1f));
            if (E.threat >= 0 && enemy == E.threat && size_t(sys) < E.system.size() && E.system[size_t(sys)] > 0.f)
                need = std::max(0.45f, need - 0.1f);
        }
        // Mecatol Rex (0.54.0): only clear wins (0.53.0: fair fights there cost both fleets and a third player took the
        // prize); warlike natures go in a little sooner, cautious ones later; more when a third player is watching
        if (opStrike && op.kind == kOpFleet) need = std::max(need, aiTune("opfleetneed", 0.7f));  // (a big fight: only a clear win)
        if (hill) {
            float hn = aiTune("opmecneed", 0.75f);
            if (S.on) hn -= 0.1f * std::max(-1.f, std::min(1.f, S.aggression - 1.f));
            if (op.watch > 0.5f) hn += aiTune("opwatchneed", 0.1f);
            need = std::max(need, std::min(0.92f, hn));
        }
        static const bool seatDebug = std::getenv("TI4_SEATDEBUG") != nullptr;  // (test only)
        if (seatDebug && aiStyxPlanet(g) >= 0 && sys == int(g.planets[size_t(aiStyxPlanet(g))].system))
            std::fprintf(stderr, "seat attack: p%d round %d Styx: ships %d odds %.2f need %.2f (groups %zu, fleet %d)\n", p, int(g.round), total.nonFighterShips(),
                         odds, need, ships.size(), fleetLimit(g, p));
        if (odds < need) return plan;  // not worth it
        float aggr = 1.f;
        if (S.on) {
            const int theirShips = enemyShips.nonFighterShips();
            aggr = (neutralFoe ? 1.f : S.aggression * S.vs[enemy]) * (theirShips <= 2 ? S.smallFights : theirShips >= 4 ? S.bigFights : 1.f);
            aggr = std::max(0.4f, std::min(3.f, aggr));
        }
        plan.value += enemyShips.nonFighterShips() * 1.5f * odds * aggr;
        plan.value += (G.combat + (vsLeader ? G.leaderThreat : 0.f) + secret) * odds * aggr;
        // (the Mahact review, checkpoint 2 - test knob edictval) Edict: a won fight puts one of the loser's tokens in our
        // fleet pool if none is there yet - a fleet token, their commander through Imperia once it is unlocked, and the
        // step toward Il Na Viroset (2 other factions' tokens). OFF (90 games: edictval=4 Edict tokens 219 -> 245, 14 wins
        // / 6.87 VP against 14 / 6.84; edictval=8 259 tokens but 8 wins - more fights, no more wins; with rexodds on fresh
        // seeds 6300/6400 10 / 6.66 against 7 / 6.43, seeds 6500/6600 9 / 6.67 against 14 / 6.83: 33 wins in 270 games
        // against 35)
        if (aiTune("edictval", 0.f) > 0.f && !neutralFoe && enemy < g.numPlayers && isFaction(g, p, "mahact") &&
            !((g.players[size_t(p)].edict >> enemy) & 1)) {
            float e = 1.f;
            if (leaderStatus(g, enemy, kCommander) != kLocked) e += 0.5f;
            if (leaderStatus(g, p, kCommander) == kLocked && __builtin_popcount(g.players[size_t(p)].edict) == 1) e += 0.5f;
            plan.value += aiTune("edictval", 0.f) * e * odds;
        }
        if (aggr > 1.f) plan.value += (aggr - 1.f) * 4.f * odds;  // a grudge, or a warlike nature
        // (the gathering's end: worth the prize and what was put into getting there)
        {
            const Endgame& E = aiEndgame(g, p);
            if (E.threat >= 0 && enemy == E.threat && size_t(sys) < E.system.size()) plan.value += E.system[size_t(sys)] * odds;
        }
        if (opStrike) {
            float w = 1.f;
            if (hill && op.watch > 0.f) w = 1.f - aiTune("opwatchv", 0.6f) * op.watch;  // (a third player would take it from us)
            plan.value += op.value * odds * aiTune("opstrike", 1.f) * (1.f + std::min(1.f, op.readiness)) * w;
        }
        if (S.betrayTarget == enemy &&
            (S.betrayKind == int(Betrayal::AttackWeakest) || S.betrayKind == int(Betrayal::BreakPromise) || kingslay) &&
            (!aiCompetitive(g) || odds >= aiTune("g_betrayodds", 0.7f)))  // Competitive: no costly gamble for it either
            plan.value += kVP * 0.8f * odds * urge;
    }
    // Achieve Supremacy: the flagship or a war sun into Mecatol Rex or another player's home system
    if (level >= 0.99f && G.supremacy > 0 && (sys == g.mecatol() || (target.home >= 0 && target.home != p)) &&
        !mine.flagships() && !mine.count(UnitType::WarSun)) {
        bool added = false;
        for (const Group& gr : ships) {
            if (added || fleetRoom() <= 0) break;
            if (!isFlagship(UnitType(gr.type)) && gr.type != int(UnitType::WarSun)) continue;
            int already = 0;
            for (size_t i = 0; i < move.size(); i += 4)
                if (move[i] == gr.from && move[i + 2] == gr.type) already += move[i + 3];
            if (already == 0) addShips(gr, 1);
            added = true;
        }
        if (added) {
            float odds = 1.f;
            if (enemy >= 0) {
                Force total = mine;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (arriving.n[t]) total.add(UnitType(t), arriving.n[t]);
                odds = fightOdds(g, p, total, enemy, enemyShips, true);
            }
            if (odds >= 0.55f) plan.value += G.supremacy * odds;
        }
    }
    // Ground forces for planets.
    if (viperCount > 0 && mine.count(UnitType::Mech) == 0)  // (a Viper target: the mechs are picked up first)
        std::stable_partition(gfs.begin(), gfs.end(), [](const Group& gr) { return gr.type == int(UnitType::Mech); });
    int gfWanted = std::max(0, gfNeeded - gfInSpace);
    std::vector<int> gfLeft(gfs.size());  // per group: not taken yet
    for (size_t i = 0; i < gfs.size(); ++i) gfLeft[i] = gfs[i].count;
    if (!targets.empty() && gfWanted > 0) {
        // pick up ground forces from origins whose ships can carry them
        std::vector<Group> carriers;
        for (const Group& gr : ships)
            if (unitDef(g, p, UnitType(gr.type)).capacity > 0) carriers.push_back(gr);
        std::sort(carriers.begin(), carriers.end(), [&](const Group& a, const Group& b) { return a.dist < b.dist; });
        auto alreadyMoving = [&](const Group& c) {
            int already = 0;
            for (size_t i = 0; i < move.size(); i += 4)
                if (move[i] == c.from && move[i + 2] == c.type) already += move[i + 3];
            return already;
        };
        for (const Group& c : carriers) {
            if (gfWanted <= 0) break;
            // ground forces available at that origin
            int avail = 0;
            for (size_t i = 0; i < gfs.size(); ++i)
                if (gfs[i].from == c.from) avail += gfLeft[i];
            if (!avail) continue;
            // how many of these carriers are already moving?
            int already = alreadyMoving(c);
            int capPer = unitDef(g, p, UnitType(c.type)).capacity;
            int freeCap = c.from < 256 ? capacityFrom[c.from] : 0;
            // existing fighters from that origin would also need room; keep it simple: count only gf
            while (freeCap < std::min(avail, gfWanted) && already < c.count && fleetRoom() > 0) {
                addShips(c, 1);
                ++already;
                freeCap += capPer;
            }
            int take = std::min({avail, gfWanted, freeCap});
            for (size_t i = 0; i < gfs.size(); ++i) {
                const Group& gf = gfs[i];
                if (take <= 0) break;
                if (gf.from != c.from || gfLeft[i] <= 0) continue;
                int k = std::min(take, gfLeft[i]);
                move.insert(move.end(), {gf.from, gf.loc, gf.type, k});
                arriving.add(UnitType(gf.type), k);
                take -= k;
                gfWanted -= k;
                gfLeft[i] -= k;
                if (c.from < 256) capacityFrom[c.from] -= k;
            }
        }
        // The groundwork (0.25.0): carriers also pick up ground forces in the systems they
        // pass through (LRR 95.1) - a carrier out in the slice calls at home on its way to
        // the next planets.
        if (gfWanted > 0 && level > 0.f && aiGroundScale(g, p) > 0.f && aiTune("g_route", 1.f) > 0.f) {
            for (const Group& c : carriers) {
                if (gfWanted <= 0) break;
                const int mv = moveValue(g, p, c.from, UnitType(c.type)) + (gd ? 1 : 0);
                const int capPer = unitDef(g, p, UnitType(c.type)).capacity;
                for (size_t i = 0; i < gfs.size() && gfWanted > 0; ++i) {
                    const Group& gf = gfs[i];
                    if (gfLeft[i] <= 0 || gf.from == c.from || !passableFor(g, p, gf.from)) continue;
                    const int dist = travelCost(g, p, c.from, gf.from) + travelCost(g, p, gf.from, sys);
                    if (dist > mv) continue;
                    // (0.44.9, fixes 13: a detour that needs the +1 uses a boost - unless the carrier needs it anyway)
                    if (g.fixes >= 13 && gd && dist > mv - 1 && shipReach(g, p, c.from, sys, UnitType(c.type), 0).ok) {
                        if (boostLeft <= 0 || lightningDrives(g, p)) continue;
                        --boostLeft;
                    }
                    int already = alreadyMoving(c);
                    int freeCap = c.from < 256 ? capacityFrom[c.from] : 0;
                    while (freeCap < std::min(gfLeft[i], gfWanted) && already < c.count && fleetRoom() > 0) {
                        addShips(c, 1);
                        ++already;
                        freeCap += capPer;
                    }
                    const int k = std::min({gfLeft[i], gfWanted, freeCap});
                    if (k <= 0) continue;
                    move.insert(move.end(), {gf.from, gf.loc, gf.type, k});
                    arriving.add(UnitType(gf.type), k);
                    gfWanted -= k;
                    gfLeft[i] -= k;
                    if (c.from < 256) capacityFrom[c.from] -= k;
                }
            }
        }
    }
    // (0.59.1: carrying every ground force out of the Creuss home was tried and made them slower - 2.1 planets in round 1
    // instead of 2.5, slice 57% instead of 73%: waiting at home they reach any slice system with the next carrier)
    // 0.58.2 (tester: "not bringing 2 infantry to double systems upon initial expansion"): in rounds 1-2 a carrier one
    // ground force short of taking every free planet of a system takes the calm home's guard along (it used to land 1
    // of 2 - 16% of the first expansions into two-planet systems - and the other planet waited rounds for a new token)
    if (level > 0.f && g.round <= 2 && enemy < 0 && freeTargets >= 2 && gfWanted == 1 && !guards.empty() &&
        aiTune("guardgo", 1.f) > 0.f) {
        for (const Group& gu : guards) {
            if (gfWanted <= 0 || gu.from >= 256) break;
            bool carrying = false;  // a carrier from the guard's system is in the plan
            for (size_t i = 0; i < move.size(); i += 4)
                if (move[i] == gu.from && move[i + 1] < 0 && unitDef(g, p, UnitType(move[i + 2])).capacity > 0) carrying = true;
            if (!carrying || capacityFrom[gu.from] <= 0) continue;
            move.insert(move.end(), {gu.from, gu.loc, gu.type, 1});
            arriving.add(UnitType(gu.type), 1);
            --gfWanted;
            --capacityFrom[gu.from];
        }
    }
    // Mechanize the Military (0.43.0 debug passes): carry spare mechs to our planets here that have none - the bots
    // built their mechs and left them together on the dock planet (aiLand puts them down one each)
    if (level > 0.f && G.mech > 0.f && enemy < 0 && aiTune("mtm", 1.f) > 0.f) {
        int withMech = 0;
        for (const PlanetState& x : g.planets)
            if (x.units[p].count(UnitType::Mech)) ++withMech;
        int spots = 0;
        for (int16_t x : target.planets)
            if (g.planets[x].owner == p && !isDMZ(g, x) && !g.planets[x].units[p].count(UnitType::Mech)) ++spots;
        spots = std::min(spots, std::max(0, 4 - withMech)) - arriving.count(UnitType::Mech);
        if (spots > 0) {
            for (const Group& c : ships) {
                if (spots <= 0) break;
                if (unitDef(g, p, UnitType(c.type)).capacity <= 0) continue;
                int avail = 0;
                for (size_t i = 0; i < gfs.size(); ++i)
                    if (gfs[i].from == c.from && gfs[i].type == int(UnitType::Mech)) avail += gfLeft[i];
                if (!avail) continue;
                int already = 0;
                for (size_t i = 0; i < move.size(); i += 4)
                    if (move[i] == c.from && move[i + 2] == c.type) already += move[i + 3];
                int freeCap = c.from < 256 ? capacityFrom[c.from] : 0;
                const int capPer = unitDef(g, p, UnitType(c.type)).capacity;
                while (freeCap < std::min(avail, spots) && already < c.count && fleetRoom() > 0) {
                    addShips(c, 1);
                    ++already;
                    freeCap += capPer;
                }
                for (size_t i = 0; i < gfs.size() && spots > 0 && freeCap > 0; ++i) {
                    if (gfs[i].from != c.from || gfs[i].type != int(UnitType::Mech) || gfLeft[i] <= 0) continue;
                    const int k = std::min({gfLeft[i], spots, freeCap});
                    move.insert(move.end(), {gfs[i].from, gfs[i].loc, gfs[i].type, k});
                    arriving.add(UnitType::Mech, k);
                    gfLeft[i] -= k;
                    spots -= k;
                    freeCap -= k;
                    if (c.from < 256) capacityFrom[c.from] -= k;
                    plan.value += G.mech * 0.8f * float(k);
                }
            }
        }
    }
    // Presence objectives: send one ship if nothing else is going there.
    float presence = presenceValue(g, p, sys) * S.pres;
    if (S.betrayKind == int(Betrayal::Embargo))  // park ships in one of their systems
        for (int16_t x : target.planets)
            if (g.planets[x].owner == S.betrayTarget) {
                presence += kVP * 0.6f * urge;
                break;
            }
    if (S.on && S.pres > 1.f && target.planets.empty() && !target.isHyperlane() && !hasShips(g, p, sys)) presence += 1.f;
    // (the Vuil'raith review, checkpoint 2 - test knob riftseek) That Which Molds Flesh unlocks with units in 3 gravity
    // rifts: a ship into a rift without our units, the more when it is the 2nd or the 3rd. riftseek=3, 90 games each: 15 wins
    // / 6.61 VP against 11 / 6.41 (commander in 25 games vs 17), fresh seeds 14 / 7.02 against 9 / 6.87, plain 19 / 7.07
    // against 17 / 6.93 (41 vs 29, round 3.2 vs 4.1). Owner 2026-11-12: ON (default 3).
    if (aiTune("riftseek", 3.f) > 0.f && isFaction(g, p, "cabal") && leaderStatus(g, p, kCommander) == kLocked &&
        (anomaliesFor(g, p, sys) & kRift) && !hasUnits(g, p, sys)) {
        int n = 0;
        for (size_t s = 0; s < g.systems.size(); ++s)
            if (int(s) != sys && (anomaliesFor(g, p, int(s)) & kRift) && hasUnits(g, p, int(s))) ++n;
        presence += aiTune("riftseek", 3.f) * (n >= 2 ? 1.f : n == 1 ? 0.5f : 0.25f);
    }
    if (presence > 0 && arriving.ships() == 0 && mine.ships() == 0 && enemy < 0) {
        const Group* pickShip = nullptr;
        for (const Group& gr : ships) {
            if (fleetRoom() <= 0) break;
            const Force& at = g.systems[gr.from].space[p];
            // don't strip a system of its last ship if it has fighters/ground forces to carry
            if (at.count(UnitType::Fighter) + at.groundForces() > 0 && unitDef(g, p, UnitType(gr.type)).capacity > 0) continue;
            if (!pickShip || unitDef(g, p, UnitType(gr.type)).cost2 < unitDef(g, p, UnitType(pickShip->type)).cost2) pickShip = &gr;
        }
        if (pickShip) {
            addShips(*pickShip, 1);
            plan.value += presence;
        }
    } else if (presence > 0 && arriving.ships() > 0) {
        plan.value += presence;
    }
    // Raise a Fleet / Command an Armada: gather non-fighter ships in one of our systems
    if (G.fleetTarget > 0 && enemy < 0 && mine.nonFighterShips() + arriving.nonFighterShips() > 0 && aiTune("fleet", 1.f) > 0.f) {
        int have = mine.nonFighterShips() + arriving.nonFighterShips();
        int best = maxNonFighterShipsOf(g, p);
        if (have < G.fleetTarget) {
            for (const Group& gr : ships) {
                if (have >= G.fleetTarget || fleetRoom() <= 0) break;
                int already = 0;
                for (size_t i = 0; i < move.size(); i += 4)
                    if (move[i] == gr.from && move[i + 2] == gr.type) already += move[i + 3];
                int k = std::min({gr.count - already, G.fleetTarget - have, fleetRoom()});
                if (k <= 0) continue;
                addShips(gr, k);
                have += k;
            }
        }
        if (have > best) plan.value += float(std::min(have, G.fleetTarget) - std::min(best, G.fleetTarget)) * G.fleetShip;
    }
    // Kingslay (0.43.0, owner: bots afraid of a fortified Mecatol Rex): most Kingslay bots had no ship that could
    // reach Mecatol Rex or the holder's home at all - gather the fleet next to them first, as many warships as the
    // holder has there (+2), for the next round's attack
    if (S.on && S.betrayKind == int(Betrayal::Kingslay) && S.betrayTarget >= 0 && S.betrayTarget < g.numPlayers && enemy < 0 && level > 0.f &&
        aiTune("kingstage", 1.f) > 0.f) {
        const int mec = g.mecatol(), hh = g.players[size_t(S.betrayTarget)].homeSystem;
        auto nextTo = [&](int a, int b) {
            return a >= 0 && b >= 0 && std::find(g.systems[size_t(a)].adj.begin(), g.systems[size_t(a)].adj.end(), int16_t(b)) != g.systems[size_t(a)].adj.end();
        };
        const int goal = nextTo(sys, mec) ? mec : nextTo(sys, hh) ? hh : -1;
        if (goal >= 0 && sys != mec && sys != hh) {
            const int want = g.systems[size_t(goal)].space[size_t(S.betrayTarget)].nonFighterShips() + 2;
            int have = mine.nonFighterShips() + arriving.nonFighterShips();
            const int before = have;
            for (const Group& gr : ships) {
                if (have >= want || fleetRoom() <= 0) break;
                if (gr.type == int(UnitType::Fighter) || nextTo(gr.from, goal) || gr.from == goal) continue;  // (those can go straight in)
                int already = 0;
                for (size_t i = 0; i < move.size(); i += 4)
                    if (move[i] == gr.from && move[i + 2] == gr.type) already += move[i + 3];
                const int k = std::min({gr.count - already, want - have, fleetRoom()});
                if (k <= 0) continue;
                addShips(gr, k);
                have += k;
            }
            if (have > before) plan.value += kVP * aiTune("kingstagev", 0.8f) * std::min(1.5f, urge) * std::min(1.f, float(have) / float(want));
        }
    }
    // 0.44.2 (tester: "solidify strategic objectives like Mecatol Rex and Styx"): nobody ever attacked Styx (its 2 neutral
    // dreadnoughts, destroyer and 3 infantry) in 30 Thunder's Edge games. From round 3 a bot gathers warships and a
    // loaded carrier on the way to it (1 or 2 systems away, closer than they were) - as many warships as defend it, +2 -
    // for the attack.
    // Plan Ahead (0.53.0): gather for the operation - ships coming closer to its target (with the ground forces their
    // carriers hold), worth how much sooner and surer the strike becomes (the readiness: ships that can strike this
    // turn count fully, one move short half, two moves short a quarter)
    // (never into a gravity rift: every way out of it rolls for the ships)
    if (opOn && !opHere && enemy < 0 && op.dist[size_t(sys)] >= 1 && op.dist[size_t(sys)] <= int(aiTune("opreach", 4.f)) &&
        canEndMoveIn(g, p, sys) && !(anomaliesFor(g, p, sys) & kRift) && aiTune("opgather", 1.f) > 0.f) {
        const int ds = op.dist[size_t(sys)];
        int added = 0;
        for (const Group& gr : ships) {
            if (fleetRoom() <= 0) break;
            if (gr.type == int(UnitType::Fighter)) continue;
            const int df = op.dist[size_t(gr.from)];
            if (df >= 0 && df <= ds) continue;  // (only ships coming closer)
            int already = 0;
            for (size_t i = 0; i < move.size(); i += 4)
                if (move[i] == gr.from && move[i + 2] == gr.type) already += move[i + 3];
            const int k = std::min(gr.count - already, fleetRoom());
            if (k <= 0) continue;
            addShips(gr, k);
            added += k;
            // a carrier brings its ground forces (for the landing)
            if (unitDef(g, p, UnitType(gr.type)).capacity > 0 && gr.from < 256)
                for (size_t i = 0; i < gfs.size(); ++i) {  // (in space first, then from our planets there)
                    const Group& gf = gfs[i];
                    if (gf.from != gr.from || capacityFrom[gr.from] <= 0 || gfLeft[i] <= 0) continue;
                    const int c = std::min(gfLeft[i], capacityFrom[gr.from]);
                    move.insert(move.end(), {gf.from, gf.loc, gf.type, c});
                    arriving.add(UnitType(gf.type), c);
                    capacityFrom[gr.from] -= c;
                    gfLeft[i] -= c;
                }
        }
        (void)added;  // (its worth: the readiness the move adds - below, for every plan)
    }
    if (level > 0.f && enemy < 0 && g.round >= 3 && aiTune("seatsiege", 1.f) > 0.f && !(opOn && op.kind == kOpStyx)) {
        const int sx = aiStyxPlanet(g);
        const int ss = sx >= 0 ? int(g.planets[size_t(sx)].system) : -1;
        if (ss >= 0 && ss != sys && g.planets[size_t(sx)].owner != p && !aiPromiseForbids(g, p, ss)) {
            std::vector<int> dist(g.systems.size(), -1);  // moves from Styx (anomalies ignored)
            std::vector<int> q{ss};
            dist[size_t(ss)] = 0;
            for (size_t qi = 0; qi < q.size(); ++qi)
                for (int16_t v : g.systems[size_t(q[qi])].adj)
                    if (dist[size_t(v)] < 0 && dist[size_t(q[qi])] < 3) {
                        dist[size_t(v)] = dist[size_t(q[qi])] + 1;
                        q.push_back(v);
                    }
            const int dsys = dist[size_t(sys)];
            if (dsys == 1 || dsys == 2) {
                const int holder = g.planets[size_t(sx)].owner >= 0 ? int(g.planets[size_t(sx)].owner) : int(kNeutral);
                // enough to win it: 75% odds against what holds it now
                const Force& theirs = g.systems[size_t(ss)].space[size_t(holder)];
                Force total = mine;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (arriving.n[t]) total.add(UnitType(t), arriving.n[t]);
                auto odds = [&]() { return theirs.nonFighterShips() ? fightOdds(g, p, total, holder, theirs, true) : 1.f; };
                const int want = theirs.nonFighterShips() + 2;
                int have = mine.nonFighterShips() + arriving.nonFighterShips();
                const int before = have;
                const float oddsBefore = odds();
                for (const Group& gr : ships) {
                    if ((have >= want && odds() >= 0.75f) || fleetRoom() <= 0) break;
                    const int dfrom = dist[size_t(gr.from)];
                    if (gr.type == int(UnitType::Fighter) || (dfrom >= 0 && dfrom <= dsys)) continue;  // (only ships coming closer)
                    int already = 0;
                    for (size_t i = 0; i < move.size(); i += 4)
                        if (move[i] == gr.from && move[i + 2] == gr.type) already += move[i + 3];
                    const int k = std::min({gr.count - already, want - have, fleetRoom()});
                    if (k <= 0) continue;
                    addShips(gr, k);
                    total.add(UnitType(gr.type), k);
                    have += k;
                    // a carrier brings its ground forces (the landing needs twice the defenders, +1)
                    if (unitDef(g, p, UnitType(gr.type)).capacity > 0 && gr.from < 256)
                        for (const Group& gf : gfs) {
                            if (gf.from != gr.from || gf.loc >= 0 || capacityFrom[gr.from] <= 0) continue;
                            const int c = std::min(gf.count, capacityFrom[gr.from]);
                            move.insert(move.end(), {gf.from, gf.loc, gf.type, c});
                            arriving.add(UnitType(gf.type), c);
                            capacityFrom[gr.from] -= c;
                        }
                }
                const float now = odds();
                if (have > before && now > oddsBefore)
                    plan.value += kVP * aiTune("seatsiegev", 0.6f) * level * (dsys == 1 ? 1.f : 0.5f) * std::min(1.f, now / 0.75f);
                static const bool seatDebug = std::getenv("TI4_SEATDEBUG") != nullptr;  // (test only)
                if (seatDebug)
                    std::fprintf(stderr, "seat siege: p%d round %d into %s (%d away): have %d (was %d) want %d value %.1f\n", p, int(g.round),
                                 kSystems[target.def].name, dsys, have, before, want, plan.value);
            }
        }
    }
    // A promise to leave a system (Leave Mecatol Rex / the Nexus): take every ship from there along
    for (size_t pi = 0; pi < g.promises.size(); ++pi) {
        const Promise& pr = g.promises[pi];
        if (pr.state != kPromiseActive || pr.by != p || pr.kind != PromiseKind::LeaveSystem || pr.target == sys) continue;
        if (!aiMeansToKeep(g, p, pr, int(pi))) continue;
        bool any = false;
        for (const Group& gr : ships) {
            if (gr.from != pr.target) continue;
            int already = 0;
            for (size_t i = 0; i < move.size(); i += 4)
                if (move[i] == gr.from && move[i + 2] == gr.type) already += move[i + 3];
            int k = std::min(gr.count - already, fleetRoom());
            if (k > 0) {
                addShips(gr, k);
                any = true;
            }
        }
        if (any) plan.value += kVP * 0.8f;
    }
    // Staging: nothing to take here, but planets / objective systems next door.
    // Bring a carrier with ground forces (or a ship) closer for the next turn.
    // (the groundwork, 0.25.0: below Hard too, toward our slice, the equidistants and the custodians,
    // while the opening lasts - a tactic token that would otherwise go unused)
    const float gwStage = level > 0.f && level < 0.99f ? aiGroundScale(g, p) * aiGroundOpening(g) * aiTune("g_stage", 1.f) : 0.f;
    if ((level >= 0.99f || gwStage > 0.f) && enemy < 0 && targets.empty() && move.empty() && target.home != p &&
        !target.isHyperlane()) {
        // only targets our loaded carriers can't reach directly this turn (otherwise just go there)
        auto directlyReachable = [&](int a) {
            for (const Group& gr : ships) {
                if (unitDef(g, p, UnitType(gr.type)).capacity <= 0) continue;
                bool loaded = false;
                for (const Group& gf : gfs)
                    if (gf.from == gr.from) loaded = true;
                if (loaded && shipReach(g, p, gr.from, a, UnitType(gr.type), gd ? 1 : 0).ok) return true;
            }
            return false;
        };
        float frontier = 0;
        for (int16_t a : target.adj) {
            if (a == sys || g.systems[a].isHyperlane()) continue;
            // (0.43.0 debug passes: not once the system is activated - Call of the Void's +1 move made the Fracture's
            // planets "directly reachable" after activating an egress, so the staging fell away and the bots moved
            // nothing: ~370 empty activations in 136 games)
            if (aiTune("stagedirect", 1.f) > 0.f && (forActivation || g.activeSystem != sys) && directlyReachable(a)) continue;
            float v = 0;
            const bool gwOnly = level < 0.99f;  // groundwork staging: only toward the opening's targets
            if (gwOnly && !(aiSliceOf(g, a) == p || aiSliceTied(g, a, p) || (a == g.mecatol() && g.round >= 2))) continue;
            for (int16_t pl : g.systems[a].planets) {
                if (g.planets[pl].owner == p) continue;
                if (gwOnly && g.planets[pl].owner >= 0) continue;
                if (g.systems[a].custodians && !g.custodiansRemoved && spendable(g, p, true) < aiCustodiansInf(g, p) &&
                    !(infAll >= aiCustodiansInf(g, p) && aiTune("seatstage", 1.f) > 0.f))  // (0.44.2: enough once the planets are ready again)
                    continue;
                int def = 0;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p) def += g.planets[pl].units[q].groundForces();
                v += planetValue(g, p, pl) * (def ? 0.4f : 1.f);
                // 0.44.2: a seat its holder left open is worth getting next to (the attack of opportunity next turn)
                if (g.planets[pl].owner >= 0 && aiSeatOpen(g, pl)) v += kVP * aiTune("seatopp", 0.7f) * level;
            }
            if (!gwOnly) v += aiPresenceValue(g, p, a, true) * 0.5f;
            // 0.44.2: the custodians are a victory point - worth staging for (as in the plan that takes them)
            if (g.systems[a].custodians && !g.custodiansRemoved && infAll >= aiCustodiansInf(g, p) && aiTune("seatstage", 1.f) > 0.f)
                v += kVP * 1.3f * aiTune("seatstage", 1.f);
            if (a == g.mecatol()) v *= S.mecatol;  // Rusher, Heirs of Lazax: work toward Mecatol Rex
            frontier = std::max(frontier, v);
        }
        if (frontier > 0) {
            int gfMoved = 0;
            std::vector<Group> carriers;
            for (const Group& gr : ships)
                if (unitDef(g, p, UnitType(gr.type)).capacity > 0) carriers.push_back(gr);
            for (const Group& c : carriers) {
                int avail = 0;
                for (const Group& gf : gfs)
                    if (gf.from == c.from) avail += gf.count;
                if (!avail || fleetRoom() <= 0) continue;
                addShips(c, 1);
                int take = std::min<int>(avail, unitDef(g, p, UnitType(c.type)).capacity);
                for (const Group& gf : gfs) {
                    if (take <= 0) break;
                    if (gf.from != c.from) continue;
                    int k = std::min(take, gf.count);
                    move.insert(move.end(), {gf.from, gf.loc, gf.type, k});
                    arriving.add(UnitType(gf.type), k);
                    take -= k;
                    gfMoved += k;
                    if (c.from < 256) capacityFrom[c.from] -= k;
                }
                break;  // one carrier is enough
            }
            if (gfMoved > 0) {
                float v = frontier * aiTune("stage", 0.8f) * S.stage * (level >= 0.99f ? 1.f : gwStage);
                if (enemyNear(g, p, sys, 1)) v *= 0.5f;
                plan.value += v;
            }
        }
    }
    // Saar floating factories (owner, 0.27.0: "it doesn't move its space dock, preventing the
    // popular Saarball"): a factory joins a fleet going out into the galaxy - escorted, never
    // into a fight it might lose (blockaded = destroyed) - and builds there this same turn
    // (production follows movement in the active system).
    bool ffArrives = false;
    float ffValue = 0;
    int ffCapAt = -1;  // the system whose factory's capacity is in capacityFrom
    if (level > 0.f && unitDef(g, p, UnitType::SpaceDock).spaceOnly && target.home != p && !hasProductionIn(g, p, sys) &&
        aiTune("saarball", 1.f) > 0.f) {
        bool safe = enemy < 0;
        if (!safe) {
            Force total = mine;
            for (int t = 0; t < kUnitTypes; ++t)
                if (arriving.n[t]) total.add(UnitType(t), arriving.n[t]);
            safe = fightOdds(g, p, total, enemy, enemyShips, true) >= 0.85f;
        }
        // how much a factory here would open up: free / enemy planets in and around it, Mecatol
        float front = 0;
        auto around = [&](int a) {
            for (int16_t pl : g.systems[a].planets)
                if (g.planets[pl].owner != p) front += (planetResources(g, pl) + planetInfluence(g, pl)) * 0.12f + 0.3f;
            if (a == g.mecatol()) front += 1.5f;
        };
        around(sys);
        for (int16_t a : target.adj)
            if (a != sys && !g.systems[a].isHyperlane()) around(a);
        const int home = g.players[p].homeSystem;
        const bool threatHome = home >= 0 && enemyNear(g, p, home, 1);
        int ffFrom = -1, ffEscort = 0;
        if (safe && front > 0.5f && !(enemyNear(g, p, sys, 1) && mine.nonFighterShips() + arriving.nonFighterShips() < 2)) {
            for (size_t s = 0; s < g.systems.size(); ++s) {
                if (int(s) == sys || g.systems[s].hasToken(p) || !g.systems[s].space[p].count(UnitType::SpaceDock)) continue;
                const int ffHere = g.systems[s].space[p].count(UnitType::SpaceDock);
                // the last factory at home stays while home is threatened, and in round 1 (it builds the opening)
                if (int(s) == home && ffHere == 1 && (threatHome || g.round < 2)) continue;
                Reach r = shipReach(g, p, int(s), sys, UnitType::SpaceDock, gd ? 1 : 0);
                if (!r.ok || r.viaRift) continue;  // never roll a factory through a gravity rift
                // it travels with ships from the same system (or ours already waiting there)
                bool escorted = mine.nonFighterShips() > 0;
                for (size_t i = 0; i < move.size(); i += 4)
                    if (move[i] == int(s) && move[i + 1] < 0 && isShip(UnitType(move[i + 2])) && move[i + 2] != int(UnitType::Fighter))
                        escorted = true;
                // 0.44.2 (tester: the Saar don't move their factories): the factory's own warships come along to escort it
                // - the ball moves together (1 in 4 times a factory could go, nothing of its own was moving with it)
                if (!escorted && ffFrom < 0 && aiTune("saarescort", 1.f) > 0.f && !enemyNear(g, p, int(s), 1)) {
                    int already = 0;
                    for (size_t i = 0; i < move.size(); i += 4)
                        if (move[i] == int(s) && move[i + 1] < 0) already += move[i + 3];
                    for (const Group& gr : ships) {
                        if (gr.from != int(s) || gr.type == int(UnitType::Fighter) || gr.loc >= 0 || fleetRoom() <= 0 || already) continue;
                        const int k = std::min(gr.count, std::max(1, std::min(fleetRoom(), 2)));
                        addShips(gr, k);
                        escorted = true;
                        break;
                    }
                }
                if (!escorted) continue;
                // 0.44.2 (tester: "Saar is not using space docks properly ... Saar balls"): the factory whose own ships
                // go along the most (the ball), not just the first one found
                int escort = 0;
                for (size_t i = 0; i < move.size(); i += 4)
                    if (move[i] == int(s) && move[i + 1] < 0 && isShip(UnitType(move[i + 2])) && move[i + 2] != int(UnitType::Fighter))
                        escort += move[i + 3];
                if (ffFrom >= 0 && (escort <= ffEscort || aiTune("saarball2", 1.f) <= 0.f)) continue;
                ffFrom = int(s);
                ffEscort = escort;
                if (aiTune("saarball2", 1.f) <= 0.f) break;
            }
        }
        static const bool saarDebug = std::getenv("TI4_SAARDEBUG") != nullptr;  // (test only)
        if (saarDebug && forActivation) {
            int why[5] = {};  // per factory system: token, home kept, no reach, not escorted
            for (size_t s = 0; s < g.systems.size(); ++s) {
                if (int(s) == sys || !g.systems[s].space[p].count(UnitType::SpaceDock)) continue;
                if (g.systems[s].hasToken(p)) { ++why[0]; continue; }
                if (int(s) == home && g.systems[s].space[p].count(UnitType::SpaceDock) == 1 && (threatHome || g.round < 2)) { ++why[1]; continue; }
                if (!shipReach(g, p, int(s), sys, UnitType::SpaceDock, gd ? 1 : 0).ok) { ++why[2]; continue; }
                ++why[3];
            }
            std::fprintf(stderr, "saar ff: safe %d front %.1f chosen %d | token %d home %d noreach %d reachable %d\n", int(safe), front, ffFrom, why[0], why[1],
                         why[2], why[3]);
        }
        if (ffFrom >= 0) {
            move.insert(move.end(), {ffFrom, -1, int(UnitType::SpaceDock), 1});
            // (the Saar review, checkpoint 2 - owner 2026-11-13 19:40: ON; ffcargo=0 the old way) the factory's capacity
            // counts for what rides along - without it the fighters it held were left stranded and the move refused, the
            // factory dropped (59 wins vs 53 in 360 games, factory moves 2.7-3.2 a game vs 1.5-1.7)
            if (ffFrom < 256 && (aiTune("ffcargo", 1.f) > 0.f || aiTune("saarball3", 0.f) > 0.f || aiTune("sbpull", 0.f) > 0.f)) {
                capacityFrom[ffFrom] += unitDef(g, p, UnitType::SpaceDock).capacity;
                ffCapAt = ffFrom;
            }
            ffArrives = true;
            ffValue = std::min(front, 4.f) * aiTune("saarball", 1.f);
            if (ffFrom == home) ffValue -= 1.f;  // home loses its yard
            // 0.44.2: the Saarball - the factory moves on with its fleet (2+ warships) and builds at the front
            if (aiTune("saarball2", 1.f) > 0.f && ffEscort >= 2) ffValue += std::min(front, 6.f) * 0.5f + 1.5f;
            // (the Saar review, checkpoint 2 - switch sbpull, test) the ball's factory going toward its front is worth more,
            // away from it less (the planner's own moves - no steps of their own)
            if (aiTune("sbpull", 0.f) > 0.f) {
                const SaarBall& sb = aiSaarBall(g, p);
                if (sb.ball == ffFrom && sb.toFront.size() == g.systems.size() && sb.toFront[size_t(ffFrom)] >= 0) {
                    const int now = sb.toFront[size_t(ffFrom)], then = sb.toFront[size_t(sys)] >= 0 ? sb.toFront[size_t(sys)] : now + 1;
                    const float k = aiTune("sbpull", 0.f);
                    ffValue += then < now ? k * float(now - then) : -0.5f * k * float(then - now);
                }
            }
        }
    }
    plan.value += ffValue;
    // The Saarball (the Saar review, owner 2026-11-13 - switch saarball3, test): this turn's step toward the front - the
    // ball's factory and its whole escort go together and build where they arrive
    float sbValue = 0.f;
    if (forActivation && level > 0.f && aiTune("saarball3", 0.f) > 0.f) {
        const SaarBall& sb = aiSaarBall(g, p);
        if (sb.ball >= 0 && sb.next == sys) {
            const std::vector<int> before = move;
            const Force arrivedBefore = arriving;
            int capBefore = sb.ball < 256 ? capacityFrom[sb.ball] : 0;
            bool hasFF = false;
            for (size_t i = 0; i + 3 < move.size(); i += 4)
                if (move[i] == sb.ball && move[i + 1] < 0 && move[i + 2] == int(UnitType::SpaceDock)) hasFF = true;
            if (!hasFF) move.insert(move.end(), {sb.ball, -1, int(UnitType::SpaceDock), 1});
            if (sb.ball < 256 && ffCapAt != sb.ball) {
                capacityFrom[sb.ball] += unitDef(g, p, UnitType::SpaceDock).capacity;
                ffCapAt = sb.ball;
            }
            // its whole escort (the guard the planner keeps at a factory goes with the factory; at home 1 warship stays
            // while enemies are within 2 - Nomadic: the Saar score without their home)
            const Force& bf = g.systems[size_t(sb.ball)].space[size_t(p)];
            int stay = sb.ball == myHome && enemyNear(g, p, sb.ball, 2) ? 1 : 0;
            for (int pass = 0; pass < 2; ++pass)  // (warships without capacity stay first)
                for (int t : kShipIdx) {
                    if (t == int(UnitType::Fighter) || !bf.n[t] || (unitDef(g, p, UnitType(t)).capacity <= 0) != (pass == 0)) continue;
                    if (!shipReach(g, p, sb.ball, sys, UnitType(t), 0).ok) continue;
                    int already = 0;
                    for (size_t i = 0; i + 3 < move.size(); i += 4)
                        if (move[i] == sb.ball && move[i + 1] < 0 && move[i + 2] == t) already += move[i + 3];
                    int k = bf.n[t] - already;
                    const int st = std::min(stay, k);
                    k -= st;
                    stay -= st;
                    k = std::min(k, fleetRoom());
                    if (k > 0) addShips(Group{sb.ball, -1, t, k, 1}, k);
                }
            int escort = mine.nonFighterShips();  // (a factory never rolls alone: blockaded, it is destroyed - ours already there count)
            for (size_t i = 0; i + 3 < move.size(); i += 4)
                if (move[i] == sb.ball && move[i + 1] < 0 && isShip(UnitType(move[i + 2])) && move[i + 2] != int(UnitType::Fighter)) escort += move[i + 3];
            if (escort == 0) {
                static const bool sbDebug = std::getenv("TI4_SAARDEBUG") != nullptr;  // (test only)
                if (sbDebug) {
                    std::fprintf(stderr, "sbnoescort into %s from %s:", kSystems[target.def].name, kSystems[g.systems[size_t(sb.ball)].def].name);
                    for (int t : kShipIdx)
                        if (bf.n[t]) std::fprintf(stderr, " %dx%s reach %d", int(bf.n[t]), unitTypeName(UnitType(t)), int(shipReach(g, p, sb.ball, sys, UnitType(t), 0).ok));
                    std::fprintf(stderr, " token %d fleetroom %d\n", int(g.systems[size_t(sb.ball)].hasToken(p)), fleetRoom());
                }
                move = before;
                arriving = arrivedBefore;
                if (sb.ball < 256) capacityFrom[sb.ball] = capBefore;
                if (!hasFF && ffCapAt == sb.ball) ffCapAt = -1;
            } else {
            ffArrives = true;
            sbValue = aiTune("saarball3", 0.f) * (1.5f + 0.5f * std::min(6.f, std::max(0.f, sb.score)));
            plan.value += sbValue;
            }
        }
    }
    // Gauss Cannons (Mirveda PDS in the space area, 0.43.0 - owner D128: "move them with fleets to help take over
    // planets"): guns in the ships' own systems come along when the fleet is enough to win; they fire their space
    // cannon at the defenders and bombard the planets before the invasion
    if (level > 0.f && !move.empty() && unitDef(g, p, UnitType::PDS).spaceOnly && aiTune("gauss", 1.f) > 0.f) {
        bool safe = enemy < 0;
        if (!safe) {
            Force total = mine;
            for (int t = 0; t < kUnitTypes; ++t)
                if (arriving.n[t]) total.add(UnitType(t), arriving.n[t]);
            safe = fightOdds(g, p, total, enemy, enemyShips, true) >= 0.7f;
        }
        bool worth = enemy >= 0;
        for (int16_t pl : target.planets) worth = worth || (g.planets[size_t(pl)].owner != p && !g.planets[size_t(pl)].destroyed);
        const int home = g.players[p].homeSystem;
        if (safe && worth) {
            std::vector<int> origins;
            for (size_t i = 0; i < move.size(); i += 4)
                if (move[i + 1] < 0 && isShip(UnitType(move[i + 2])) && move[i + 2] != int(UnitType::Fighter)) origins.push_back(move[i]);
            int added = 0;
            for (int from : origins) {
                if (added >= 2) break;
                int guns = g.systems[size_t(from)].space[size_t(p)].count(UnitType::PDS);
                for (size_t i = 0; i < move.size(); i += 4)
                    if (move[i] == from && move[i + 2] == int(UnitType::PDS)) guns -= move[i + 3];
                if (from == home && enemyNear(g, p, home, 1)) guns = std::min(guns, std::max(0, guns - 1));  // (one stays to guard)
                if (guns <= 0) continue;
                Reach r = shipReach(g, p, from, sys, UnitType::PDS, gd ? 1 : 0);
                if (!r.ok || r.viaRift) continue;
                const int k = std::min(guns, 2 - added);
                move.insert(move.end(), {from, -1, int(UnitType::PDS), k});
                added += k;
                plan.value += 0.9f * float(k) * aiTune("gauss", 1.f);
            }
        }
    }
    // 0.42.1 (#187: a lone carrier went past the same PDS two rounds running - the leader's planets
    // were worth it): space cannon waiting there - spare warships from the same origins come along
    // to take its hits (the mover picks which ships are lost), cheapest first
    if (level > 0.f && !move.empty() && aiTune("escort", 1.f) > 0.f) {
        const float threat = cannonThreat(g, p, sys);
        if (threat >= 0.3f) {
            Force total = mine;
            for (size_t i = 0; i < move.size(); i += 4)
                if (move[i + 1] < 0 && isShip(UnitType(move[i + 2]))) total.add(UnitType(move[i + 2]), move[i + 3]);
            int want = int(threat + 0.99f) + 1 - int(hitPoints(g, p, total, true));
            std::vector<int> origins;
            for (size_t i = 0; i < move.size(); i += 4)
                if (move[i + 1] < 0 && isShip(UnitType(move[i + 2]))) origins.push_back(move[i]);
            for (UnitType t : {UnitType::Destroyer, UnitType::Cruiser, UnitType::Dreadnought}) {
                for (const Group& gr : ships) {
                    if (want <= 0 || fleetRoom() <= 0) break;
                    if (gr.type != int(t) || std::find(origins.begin(), origins.end(), gr.from) == origins.end()) continue;
                    int already = 0;
                    for (size_t i = 0; i < move.size(); i += 4)
                        if (move[i] == gr.from && move[i + 2] == gr.type) already += move[i + 3];
                    const int k = std::min({gr.count - already, want, fleetRoom()});
                    if (k <= 0) continue;
                    addShips(gr, k);
                    want -= k * (unitSustainsAt(g, p, t, sys, -1) ? 2 : 1);
                }
            }
        }
    }
    // 0.42.0 (Ghoti: the flagship took the 1 infantry a planet needed and the move was refused -
    // the rest would have been stranded in a home with no planets): fighters and ground forces the
    // ships staying behind can't hold come along, as far as the moving ships have room
    for (size_t oi = 0; oi < move.size(); oi += 4) {
        const int o = move[oi];
        if (move[oi + 1] >= 0 || !isShip(UnitType(move[oi + 2])) || o < 0 || o >= 256) continue;
        bool first = true;
        for (size_t j = 0; j < oi; j += 4)
            if (move[j] == o && move[j + 1] < 0 && isShip(UnitType(move[j + 2]))) first = false;
        if (!first) continue;
        Force left = g.systems[size_t(o)].space[p];
        for (size_t j = 0; j < move.size(); j += 4)
            if (move[j] == o && move[j + 1] < 0) left.n[move[j + 2]] = uint8_t(left.n[move[j + 2]] - move[j + 3]);
        for (UnitType t : {UnitType::Infantry, UnitType::Mech, UnitType::Fighter}) {
            int k = 0;
            while (left.count(t) > 0 && capacityFrom[o] > 0 && overflowOf(g, p, o, left) > 0) {
                left.n[int(t)] = uint8_t(left.n[int(t)] - 1);
                --capacityFrom[o];
                ++k;
            }
            if (k) {
                move.insert(move.end(), {o, -1, int(t), k});
                arriving.add(t, k);
            }
        }
    }
    // (0.59.0, owner: "the Crimson don't expand into their slice") the Revenant: 1 mech from the reinforcements lands in
    // a system with a breach (Incursion opens an inactive one when it is activated) - no carrier needed, as long as
    // nobody else's ships are left there (the invasion needs the space clear)
    const bool revenantDrop = forActivation && (g.modules & kModThundersEdge) && isFaction(g, p, "crimson") && mechAbilities(g) &&
                              target.breach != 0 && unitsAvailable(g, p, UnitType::Mech) > 0 && !targets.empty() &&
                              (enemy < 0 || !move.empty()) && aiTune("revenantplan", 1.f) > 0.f;
    int gfArriving = arriving.groundForces() + gfInSpace + (drones ? arriving.count(UnitType::Fighter) : 0) + (revenantDrop ? 1 : 0);
    if (std::getenv("TI4_AIDEBUG3"))
        std::fprintf(stderr, "      %s: ships %zu gfs %zu targets %zu need %d arriving %d move %zu prize %.1f\n",
                     kSystems[target.def].name, ships.size(), gfs.size(), targets.size(), gfNeeded, gfArriving,
                     move.size() / 4, prize);
    if (viperCount > 0 && std::getenv("TI4_VIPERDEBUG"))
        std::fprintf(stderr, "VIPER r%d %s targets %d mech %d+%d need %d arriving %d enemy %d prize %.1f delta %.1f\n", g.round,
                     kSystems[target.def].name, viperCount, arriving.count(UnitType::Mech), mine.count(UnitType::Mech), gfNeeded,
                     gfArriving, enemy, prize, viperDelta);
    if (viperCount > 0 && arriving.count(UnitType::Mech) + mine.count(UnitType::Mech) == 0) {  // no Viper: a fight after all
        prize += viperDelta;
        gfNeeded += viperExtra;
    }
    if (!targets.empty()) {
        if (gfArriving == 0) prize = 0;
        else if (gfArriving < gfNeeded) prize *= float(gfArriving) / float(gfNeeded);
        // planets defended by enemy space cannon are riskier
        plan.value += prize;
    }
    // Plan Ahead: the operation's landing (Styx once its guard ships are gone)
    if (opHere && enemy < 0 && !targets.empty() && gfArriving > 0 && aiTune("opland", 1.f) > 0.f)
        plan.value += op.value * aiTune("oplandv", 0.5f) * std::min(1.f, float(gfArriving) / float(std::max(1, op.gfNeed)));
    if (custodiansLater && enemy < 0 && arriving.groundForces() > 0 && !move.empty())
        plan.value += kVP * 1.3f * level * aiTune("seatpark", 0.45f);
    // ... and a plan next to Mecatol Rex that brings ground forces beyond what its planets need, in a ship with
    // capacity: they stay aboard for the custodians (aiLand keeps them)
    if (level > 0.f && enemy < 0 && !g.custodiansRemoved && g.mecatol() >= 0 && sys != g.mecatol() && infAll >= aiCustodiansInf(g, p) &&
        adjacentSystems(g, sys, g.mecatol()) && aiTune("seatkeep", 1.f) > 0.f && gfArriving > gfNeeded && !move.empty()) {
        bool carrier = false;
        for (size_t i = 0; i + 3 < move.size(); i += 4)
            if (move[i + 1] < 0 && isShip(UnitType(move[i + 2])) && unitDef(g, p, UnitType(move[i + 2])).capacity > 0) carrier = true;
        if (carrier) plan.value += kVP * 1.3f * level * aiTune("seatkeep", 0.3f);
    }
    // Validate; if the full plan fails, fall back to warships only, then nothing.
    // Miniaturization (Ral Nel, 0.43.0 debug passes - owner T33: "have them use it"): a ship leaving a quiet system
    // takes a PDS along to one of our planets at the front (it lands at the end of the tactical action)
    if (!move.empty() && level > 0.f && isFaction(g, p, "ralnel") && aiTune("mini", 1.f) > 0.f) {
        bool landing = false;  // a planet there that will be ours: one we hold with room for a PDS, or an undefended target
        for (int16_t x : target.planets) {
            const PlanetState& ps = g.planets[x];
            if (ps.destroyed) continue;
            if (ps.owner == p && ps.units[p].count(UnitType::PDS) < 2) landing = true;
            if (ps.owner != p && std::find(targets.begin(), targets.end(), int(x)) != targets.end()) {
                int def = neutralGroundOn(g, x);
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p) def += ps.units[q].groundForces();
                if (def == 0) landing = true;
            }
        }
        if (landing && (enemyNear(g, p, sys, 1) || target.home < 0)) {
            for (size_t i = 0; i + 3 < move.size() && landing; i += 4) {
                const int from = move[i];
                if (move[i + 1] >= 0 || !isShip(UnitType(move[i + 2])) || g.systems[from].home == p || enemyNear(g, p, from, 1)) continue;
                for (int16_t x : g.systems[from].planets)
                    if (g.planets[x].owner == p && g.planets[x].units[p].count(UnitType::PDS) > 0) {
                        std::vector<int> with = move;
                        with.insert(with.end(), {from, int(x), int(UnitType::PDS), 1});
                        if (checkMovePlan(g, p, sys, with, false, true).empty()) {
                            move = with;
                            plan.value += 0.8f;
                            landing = false;
                        }
                        break;
                    }
            }
        }
    }
    // Plan Ahead: what the move does for the operation - ships coming closer make the strike sooner and surer (the
    // gathering), ships taken away from it set it back (what they do elsewhere has to be worth more)
    if (opOn && !opHere && !move.empty() && aiTune("opgather", 1.f) > 0.f && !aiEndgame(g, p).lastRound) {  // (nothing to gather for in the last round)
        const float after = aiOpReadinessAfter(g, p, op, sys, move);
        const float delta = after - op.readiness;
        plan.value += op.value * aiTune(delta >= 0.f ? "opgatherv" : "oplossv", delta >= 0.f ? 1.6f : 1.f) * delta;
        static const bool opDebug = std::getenv("TI4_OPDEBUG") != nullptr;  // (test only)
        if (opDebug && std::fabs(delta) > 0.01f)
            std::fprintf(stderr, "op move: p%d r%d into %s (%d away): readiness %.2f -> %.2f value %.1f\n", p, int(g.round), kSystems[target.def].name,
                         op.dist[size_t(sys)], op.readiness, after, plan.value);
    }
    // Hil Colish (fixes 24): ships that get here only through its delta wormhole need it to move first - take it along
    if (!move.empty() && !checkMovePlan(g, p, sys, move, false, true).empty()) {
        const int co = colishOrigin(g, p);
        bool has = false;
        for (size_t i = 0; i + 3 < move.size(); i += 4)
            if (move[i] == co && move[i + 1] < 0 && move[i + 2] == int(UnitType::Flagship)) has = true;
        if (co >= 0 && co != sys && !has) {
            std::vector<int> with = move;
            with.insert(with.begin(), {co, -1, int(UnitType::Flagship), 1});
            if (checkMovePlan(g, p, sys, with, false, true).empty()) move = with;
        }
    }
    if (!move.empty() && aiTune("unstrand", 0.f) > 0.f && !checkMovePlan(g, p, sys, move, false, true).empty()) {
        std::vector<int> fixedMove = aiUnstrand(g, p, sys, move);
        if (fixedMove != move && checkMovePlan(g, p, sys, fixedMove, false, true).empty()) {
            if (enemy >= 0) {  // ships left behind: the fight's odds as they now are
                Force before = mine, after = mine;
                for (size_t i = 0; i + 3 < move.size(); i += 4)
                    if (move[i + 1] < 0 && isShip(UnitType(move[i + 2]))) before.add(UnitType(move[i + 2]), move[i + 3]);
                for (size_t i = 0; i + 3 < fixedMove.size(); i += 4)
                    if (fixedMove[i + 1] < 0 && isShip(UnitType(fixedMove[i + 2]))) after.add(UnitType(fixedMove[i + 2]), fixedMove[i + 3]);
                const float o0 = fightOdds(g, p, before, enemy, enemyShips, true), o1 = fightOdds(g, p, after, enemy, enemyShips, true);
                if (o0 > 0.01f && o1 < o0) plan.value *= o1 / o0;
            }
            int gf0 = 0, gf1 = 0;  // ground forces that no longer come: the landing is worth less
            for (size_t i = 0; i + 3 < move.size(); i += 4)
                if (isGroundForce(UnitType(move[i + 2]))) gf0 += move[i + 3];
            for (size_t i = 0; i + 3 < fixedMove.size(); i += 4)
                if (isGroundForce(UnitType(fixedMove[i + 2]))) gf1 += fixedMove[i + 3];
            if (gf0 > 0 && gf1 < gf0) plan.value *= 0.5f + 0.5f * float(gf1) / float(gf0);
            move = fixedMove;
        }
    }
    if (!move.empty() && !checkMovePlan(g, p, sys, move, false, true).empty()) {
        static const bool sbDebug = std::getenv("TI4_SAARDEBUG") != nullptr;  // (test only)
        if (sbDebug && sbValue > 0.f) {
            std::fprintf(stderr, "sbrefused into %s: %s | move", kSystems[target.def].name, checkMovePlan(g, p, sys, move, false, true).c_str());
            for (size_t i = 0; i + 3 < move.size(); i += 4) std::fprintf(stderr, " %d:%d:%d:%d", move[i], move[i + 1], move[i + 2], move[i + 3]);
            std::fprintf(stderr, "\n");
        }
        if (std::getenv("TI4_AIDEBUG2"))
            std::fprintf(stderr, "      plan into %s rejected: %s\n", kSystems[target.def].name,
                         checkMovePlan(g, p, sys, move, false, true).c_str());
        std::vector<int> shipsOnly;
        for (size_t i = 0; i < move.size(); i += 4)
            if (move[i + 1] < 0 && isShip(UnitType(move[i + 2]))) shipsOnly.insert(shipsOnly.end(), move.begin() + i, move.begin() + i + 4);
        if (checkMovePlan(g, p, sys, shipsOnly, false, true).empty()) {
            move = shipsOnly;
            plan.value *= 0.3f;
        } else {
            move.clear();
            plan.value = 0;
        }
    }
    plan.move = move;
    // Wormhole Weavers (Creuss faction trait, 0.59.0 - owner: make the trait do what it says): a strike that reaches
    // another player's ships or planets through a wormhole is worth a quarter more - half more behind their lines (a
    // planet of theirs with none of their ships in or next to its system)
    if (!move.empty() && plan.value > 0.f && fquirkIs(g, p, "ghost", 0) && aiTune("weave", 1.25f) > 1.f) {
        bool viaWh = false;
        for (size_t i = 0; i + 3 < move.size() && !viaWh; i += 4)
            if (move[i + 1] < 0 && move[i] != sys && isShip(UnitType(move[i + 2])) && UnitType(move[i + 2]) != UnitType::Fighter)
                viaWh = shipReach(g, p, move[i], sys, UnitType(move[i + 2]), moveBonusMax(g, p)).viaWormhole;
        bool enemy = false, behind = false;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p && hasShips(g, q, sys)) enemy = true;
        for (int16_t x : g.systems[size_t(sys)].planets) {
            const int o = g.planets[size_t(x)].owner;
            if (o < 0 || o == p || o >= g.numPlayers) continue;
            enemy = true;
            bool guarded = hasShips(g, o, sys);
            for (int16_t a : g.systems[size_t(sys)].adj) guarded = guarded || hasShips(g, o, a);
            if (!guarded) behind = true;
        }
        if (viaWh && enemy) plan.value *= behind ? aiTune("weaveback", 1.5f) : aiTune("weave", 1.25f);
    }
    // 0.42.0 (#187): the space cannon waiting there and gravity rifts on the way can sink what we
    // send - a lone carrier past an enemy PDS used to go (and die) round after round
    if (!move.empty() && plan.value > 0.f && level > 0.f && aiTune("cannonrisk", 1.f) > 0.f) {
        Force total = mine;
        int shipsIn = 0;
        for (size_t i = 0; i < move.size(); i += 4)
            if (move[i + 1] < 0 && isShip(UnitType(move[i + 2]))) {
                total.add(UnitType(move[i + 2]), move[i + 3]);
                if (move[i + 2] != int(UnitType::Fighter)) shipsIn += move[i + 3];
            }
        const float hp = hitPoints(g, p, total, true);
        const float threat = cannonThreat(g, p, sys);
        if (threat > 0.f && hp > 0.f) {
            const float loss = std::min(1.f, threat / hp);
            if (std::getenv("TI4_AIDEBUG2"))
                std::fprintf(stderr, "      cannon risk at %s: threat %.2f hp %.1f value %.2f -> %.2f\n", kSystems[target.def].name, threat, hp,
                             plan.value, plan.value - plan.value * loss * 0.8f - threat * 0.8f);
            plan.value -= plan.value * loss * 0.8f + threat * 0.8f;
        }
        if (riftShips > 0 && shipsIn > 0) plan.value -= plan.value * 0.3f * std::min(1.f, float(riftShips) / float(shipsIn));
        if (plan.value <= 0.f) {
            plan.value = 0.f;
            plan.move.clear();
            move.clear();
        }
    }
    if (ffArrives) {  // the fallback above drops the factory with everything that isn't a ship
        bool still = false;
        for (size_t i = 0; i < move.size(); i += 4)
            if (move[i + 2] == int(UnitType::SpaceDock)) still = true;
        if (!still) {
            ffArrives = false;
            plan.value -= ffValue * 0.3f + sbValue;  // (the Saarball's step is the factory's)
        }
    }
    if (aiCompetitive(g) && !move.empty()) {  // what the move does for our objectives, played out
        float delta = objectiveDelta(g, p, sys, move);
        if (delta > 0.f) plan.value += delta * aiTune("objdelta", 0.8f) * aiUrgency(g, p);
    }
    // deals: notes we'd lose, promises we keep, demands that were refused
    {
        std::vector<int> opponents;
        if (enemy >= 0 && enemy < g.numPlayers) opponents.push_back(enemy);  // (no deals with neutral units)
        for (int pl : targets)
            if (g.planets[pl].owner >= 0 && g.planets[pl].owner != p &&
                std::find(opponents.begin(), opponents.end(), g.planets[pl].owner) == opponents.end())
                opponents.push_back(g.planets[pl].owner);
        if (opponents.empty()) plan.value += aiPromisePlanValue(g, p, sys, -1, false);
        for (size_t i = 0; i < opponents.size(); ++i)
            plan.value += aiPromisePlanValue(g, p, sys, opponents[i], true) - (i ? aiPromisePlanValue(g, p, sys, -1, false) : 0.f);
    }
    // personality: Mecatol Rex, anomalies, distance from home, an enemy home system it wants
    if (S.on && plan.value > 0 && !move.empty()) {
        if (sys == g.mecatol()) plan.value += (S.mecatol - 1.f) * 5.f;
        if (target.d().anomalies) plan.value += S.anomaly;
        if (S.homeHitTarget >= 0 && target.home == S.homeHitTarget) plan.value += S.homeHit;
        const int home = g.players[p].homeSystem;
        if (S.farPenalty > 0 && home >= 0) {
            int dist = hexDistance(target.pos, g.systems[home].pos);
            if (g.systems[home].offBoard) dist = std::max(0, dist - 1);  // (beside the board: from its gate)
            if (dist > 2) plan.value = std::max(0.1f, plan.value - S.farPenalty * float(dist - 2) * 2.f);
        }
    }
    // Production at this system - only if we would really build something there
    // (activating a system and then buying nothing wastes a command token)
    // (the Arborec review, checkpoint 2 - test knob duhaplan) Duha Menaimon: activating the system the flagship already
    // sits in lets it produce up to 5 units there - the planner counted only the infantry's PRODUCTION. Tested OFF:
    // no effect (Duha used 37 times in 90 games against 36; 13 wins / 6.94 VP against 13 / 6.97) - the flagship
    // mostly sits where the bot already activates or builds anyway
    const int duha = aiTune("duhaplan", 0.f) > 0.f && isFaction(g, p, "arborec") && flagshipAbility(g, p, "arborec") &&
                             flagshipIn(g, p, sys) && !noAbilities(g, p, sys)
                         ? 5
                         : 0;
    if (ffArrives && !move.empty()) {  // the factory builds where it arrives (budget as below)
        Reserve r = objectiveReserve(g, p);
        int budget = std::min(spendable(g, p, false) - r.resources - r.tradeGoods, aiFreeSpend(g, p, false));
        if (budget >= 3) plan.value += std::min<int>(unitDef(g, p, UnitType::SpaceDock).production, budget) * 0.6f + 1.0f;
    } else if ((hasProductionIn(g, p, sys) || duha > 0) && !otherShipsIn(g, p, sys) && (level <= 0.f || wouldProduce(g, p, sys, duha))) {
        Reserve r = objectiveReserve(g, p);
        int budget = spendable(g, p, false) - r.resources - r.tradeGoods;
        if (level > 0.f) budget = std::min(budget, aiFreeSpend(g, p, false));
        // (0.59.0, owner: the Crimson don't expand) with a dock out on the board, the home beside it builds 2 moves from
        // everything: the dock out there is the better place
        const float far = (isFaction(g, p, "crimson") || (isFaction(g, p, "ghost") && aiTune("creussdock", 1.f) > 0.f)) &&
                                  g.systems[size_t(sys)].offBoard && sys == myHome && !crimsonNeedsDock(g, p) && aiTune("crimsonhome", 1.f) > 0.f
                              ? 0.55f
                              : 1.f;
        if (budget >= 3) plan.value += (std::min(productionCapacity(g, p, sys) + duha, budget) * 0.6f + 1.0f) * far;
        else if (budget >= 1 && level > 0.f) plan.value += (budget * 0.6f + 0.6f) * far;  // a couple of infantry still help
        // units an objective wants (flagship / war sun, dreadnoughts, mechs, a big fleet here)
        if (budget >= 3 && level > 0.f) {
            float want = 0;
            if (G.flagshipOrWarSun > 0 && budget >= 8 &&
                (unitsAvailable(g, p, UnitType::Flagship) > 0 || unitsAvailable(g, p, UnitType::Flagship2) > 0 ||
                 unitsAvailable(g, p, UnitType::WarSun) > 0))  // (Muaat: no tech needed)
                want += G.flagshipOrWarSun;
            if (aiCoreWarSuns(g, p) && budget >= unitDef(g, p, UnitType::WarSun).cost2 / 2 - 1) want += 2.5f;  // Strategy Core: a war sun
            if (G.dread > 0 && budget >= 4) want += G.dread;
            if (G.mech > 0 && budget >= 2) want += G.mech;
            if (G.fleetShip > 0 && g.systems[sys].space[p].nonFighterShips() + 3 >= G.fleetTarget) want += G.fleetShip * 2.f;
            plan.value += want * 0.8f;
        }
    }
    // (0.59.0) nothing moves, but something lands: a Revenant, or ground forces already waiting in this space area (a
    // carrier that came with Ahk Ravin's swap) - an activation is all it takes
    const bool landsAnyway = plan.value > 0 && enemy < 0 && !targets.empty() && (revenantDrop || gfInSpace > 0) && aiTune("landhere", 1.f) > 0.f;
    // (the Titans review, checkpoint 2 - knob awakenval = the value of each sleeper, default 3) Awaken: activating the
    // system turns the Titans' sleeper tokens there into PDS - the bots awakened 2.3 of the 5.3 they placed a game, only
    // when they happened to go there. Tested (awakenval=3): awakened 4.8 a game; TE 21 wins / 7.39 VP against 11 / 6.94,
    // fresh seeds 21 / 7.37 against 15 / 6.88, plain (fresh) 24 / 7.88 against 18 / 7.38. With hecakeep=2: 29 / 7.94,
    // fresh 26 / 7.86, plain fresh 25 / 7.71. ON (3) with hecakeep=2 since the owner's go (2026-11-13 15:31 "Let's turn on
    // both"); awakenval=0 = the old way
    float awaken = 0.f;
    if (aiTune("awakenval", 3.f) > 0.f && forActivation && enemy < 0 && isFaction(g, p, "titans")) {
        int n = 0;
        for (int16_t x : target.planets)
            if (g.planets[size_t(x)].sleeper && !g.planets[size_t(x)].destroyed) ++n;
        n = std::min(n, unitsAvailable(g, p, UnitType::PDS) + unitsAvailable(g, p, UnitType::Mech));
        awaken = aiTune("awakenval", 3.f) * float(n);
        plan.value += awaken;
    }
    if (move.empty() && !((hasProductionIn(g, p, sys) || duha > 0) && plan.value > 0) && !landsAnyway && awaken <= 0.f) plan.value = 0;
    // (0.59.1, owner: the Creuss park ships in their empty Gate - 7 of 30 games in round 1) a system with no planets
    // right beside the home: going there buys one step and costs a command token, unless there is a fight or a build
    if (plan.value > 0 && isFaction(g, p, "ghost") && aiTune("creussgate", 1.f) > 0.f && enemy < 0 && g.systems[size_t(sys)].planets.empty() &&
        !g.systems[size_t(sys)].offBoard && (g.systems[size_t(sys)].d().wormholes & kDelta) && !hasProductionIn(g, p, sys) &&
        !breakthroughIs(g, p, "ghost"))
        plan.value *= 0.3f;
    return plan;
}

static Plan bestPlanCore(const Game& g, int p);

// (the Mahact review, checkpoint 2 - test knob ilna) Il Na Viroset ends the turn at the activation (fixes 26), but both
// tokens go back: ships locked in by our token can move again on a later turn, a dock can produce again. Worth the
// turn and a tactic token when the best plan with that token gone uses the system (its ships or its production) -
// at a discount for the turn it waits. OFF (90 games, ilna=0.7: used 108 times, the freed ships moved on the next turn
// in 93; 13 wins / 6.66 VP against 14 / 6.84 - no gain).
Plan bestPlan(const Game& g, int p) {
    Plan best = bestPlanCore(g, p);
    static const bool sbDebug = std::getenv("TI4_SAARDEBUG") != nullptr;  // (test only) the Saarball's step against the pick
    if (sbDebug && aiTune("saarball3", 0.f) > 0.f) {
        const SaarBall& sb = aiSaarBall(g, p);
        if (sb.ball >= 0) {
            const Plan st = sb.next >= 0 ? buildPlan(g, p, sb.next, true) : Plan();
            bool ffIn = false;
            for (size_t i = 0; i + 3 < st.move.size(); i += 4)
                if (st.move[i + 2] == int(UnitType::SpaceDock)) ffIn = true;
            if (sb.next >= 0 && !ffIn) {
                // (why the step lost its factory: the move as the hook built it, checked)
                std::fprintf(stderr, "sbdrop p%d into %s: %s\n", p, kSystems[g.systems[size_t(sb.next)].def].name,
                             st.move.empty() ? "no move" : checkMovePlan(g, p, sb.next, st.move, false, true).c_str());
            }
            std::fprintf(stderr, "sbturn p%d r%d tac %d ball %s front %s next %s step %.1f ff %d | pick %s %.1f\n", p, int(g.round),
                         int(g.players[size_t(p)].tactic), kSystems[g.systems[size_t(sb.ball)].def].name,
                         sb.front >= 0 ? kSystems[g.systems[size_t(sb.front)].def].name : "-",
                         sb.next >= 0 ? kSystems[g.systems[size_t(sb.next)].def].name : "-", st.value, int(ffIn),
                         best.system >= 0 ? kSystems[g.systems[size_t(best.system)].def].name : "-", best.value);
        }
    }
    static thread_local bool inIlNa = false;
    const float k = aiTune("ilna", 0.f);
    if (k <= 0.f || inIlNa || g.fixes < 26 || g.players[size_t(p)].tactic < 2 || !commanderUsable(g, p, "mahact")) return best;
    inIlNa = true;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        const SystemState& ss = g.systems[s];
        if (!ss.hasToken(p) || !canActivate(g, p, int(s))) continue;
        bool dock = false;
        for (int16_t x : ss.planets)
            if (g.planets[x].units[size_t(p)].count(UnitType::SpaceDock)) dock = true;
        if (ss.space[size_t(p)].nonFighterShips() == 0 && !dock && !ss.space[size_t(p)].count(UnitType::WarSun)) continue;
        Game h = g;
        h.systems[s].tokens = uint16_t(h.systems[s].tokens & ~(1u << p));
        Plan next = bestPlanCore(h, p);
        bool uses = next.system == int(s);
        for (size_t i = 0; i + 3 < next.move.size() && !uses; i += 4)
            if (next.move[i] == int(s)) uses = true;
        if (!uses) continue;
        const float v = k * next.value - 1.f;
        if (v > best.value) {
            best = Plan();
            best.system = int(s);
            best.value = v;
        }
    }
    inIlNa = false;
    return best;
}

static Plan bestPlanCore(const Game& g, int p) {
    Plan best;
    // what a system's activation locks in place: Competitive since 0.12.1, the groundwork's
    // order of the round (ships out first, then the home dock) below it since 0.25.0
    float lockW = 0.f;
    if (aiCompetitive(g)) lockW = 0.7f;
    else if (aiLevelScale(g) > 0.f) lockW = 0.7f * aiGroundScale(g, p) * aiTune("g_lock", 1.f);
    if (lockW <= 0.f) {
        for (size_t s = 0; s < g.systems.size(); ++s) {
            if (!canActivate(g, p, int(s))) continue;
            Plan pl = buildPlan(g, p, int(s), true);
            if (pl.value > best.value) best = pl;
        }
        return best;
    }
    // Competitive: activating a system locks our ships there for the round. Count
    // what they could still do elsewhere, so the bot expands before it builds at
    // home rather than trapping its carriers.
    std::vector<Plan> plans;
    for (size_t s = 0; s < g.systems.size(); ++s)
        if (canActivate(g, p, int(s))) plans.push_back(buildPlan(g, p, int(s), true));
    const int tokensLeft = g.players[p].tactic;
    float bestScore = 0.f;
    for (const Plan& pl : plans) {
        if (pl.value <= 0.f) continue;
        float lock = 0.f;
        if (tokensLeft >= 2)
            for (const Plan& other : plans) {
                if (other.system == pl.system || other.value <= lock) continue;
                for (size_t i = 0; i + 3 < other.move.size(); i += 4)
                    if (other.move[i] == pl.system) {
                        lock = other.value;
                        break;
                    }
            }
        float score = pl.value - lock * lockW;
        if (score > bestScore) {
            bestScore = score;
            best = pl;
        }
    }
    if (best.system < 0)  // nothing worth more than what it locks: take the plain best
        for (const Plan& pl : plans)
            if (pl.value > best.value) best = pl;
    static const bool opDebug = std::getenv("TI4_OPDEBUG") != nullptr;  // (test only) the operation's plans against the pick
    if (opDebug) {
        const AiOp& op = aiOp(g, p);
        if (op.kind != kOpNone && op.dist.size() == g.systems.size()) {
            const Plan* bo = nullptr;
            for (const Plan& pl : plans)
                if (pl.system >= 0 && op.dist[size_t(pl.system)] >= 0 && op.dist[size_t(pl.system)] <= 2 && (!bo || pl.value > bo->value)) bo = &pl;
            std::fprintf(stderr, "op turn: p%d r%d op %d %s (posted %d, odds %.2f/%.2f) picks %s %.1f; best op plan %s %.1f\n", p, int(g.round), op.kind,
                         kSystems[g.systems[size_t(op.target)].def].name, op.tier[0].nonFighterShips(), op.oddsReady, op.oddsLater,
                         best.system >= 0 ? kSystems[g.systems[size_t(best.system)].def].name : "-", best.value,
                         bo ? kSystems[g.systems[size_t(bo->system)].def].name : "-", bo ? bo->value : 0.f);
            if (best.system >= 0) {
                std::fprintf(stderr, "    pick moves (dist %d):", op.dist[size_t(best.system)]);
                for (size_t i = 0; i + 3 < best.move.size(); i += 4)
                    std::fprintf(stderr, " %dx%s from %s (dist %d)", best.move[i + 3], unitTypeName(UnitType(best.move[i + 2])),
                                 kSystems[g.systems[size_t(best.move[i])].def].name, op.dist[size_t(best.move[i])]);
                std::fprintf(stderr, "\n");
            }
        }
    }
    return best;
}

// ============================================================ strategy cards
bool hardAI(const Game& g) { return aiLevelScale(g) >= 0.99f; }

bool controlsMecatol(const Game& g, int p) {
    int mec = g.mecatol();
    if (mec < 0) return false;
    for (int16_t x : g.systems[mec].planets)
        if (g.planets[x].owner == p) return true;
    return false;
}

// Warfare (0.17.0, owner: "very rarely does Warfare get taken"): the primary
// is a second activation of a system (move a fleet again, produce twice at a
// big dock) plus a free rearrangement of the command sheet; it matters once
// the fights start, near Mecatol Rex held by someone else, with a fleet next
// to enemies, or with enemies at the door.
float warfareValue(const Game& g, int p, const Goals& G) {
    float v = 3.5f + G.combat * 0.5f + (G.leader >= 0 && G.leader != p ? 0.5f : 0.f);
    v += std::min(1.2f, 0.3f * float(std::max(0, g.round - 1)));  // the middle game is when fleets meet
    const Player& pl = g.players[p];
    int fleetSys = -1, fleet = 0;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        int n = g.systems[s].space[p].nonFighterShips();
        if (n > fleet) {
            fleet = n;
            fleetSys = int(s);
        }
    }
    if (fleetSys >= 0 && fleet >= 3) {
        bool target = false;
        for (int s : systemsWithin(g, fleetSys, 2)) {
            if (otherShipsIn(g, p, s)) target = true;
            for (int16_t x : g.systems[s].planets)
                if (g.planets[x].owner >= 0 && g.planets[x].owner != p) target = true;
        }
        if (target) v += 0.8f + std::min(0.8f, 0.2f * float(fleet - 3));
    }
    const int mec = g.mecatol();
    if (mec >= 0 && fleetSys >= 0 && fleet >= 2) {
        int holder = -1;
        for (int16_t x : g.systems[mec].planets) holder = g.planets[x].owner;
        if (holder >= 0 && holder != p && hexDistance(g.systems[fleetSys].pos, g.systems[mec].pos) <= 2) v += 1.2f;
    }
    const int home = pl.homeSystem;
    if (home >= 0) {
        if (enemyNear(g, p, home, 1)) v += 0.8f;  // produce at home twice / move out and back
        if (productionCapacity(g, p, home) >= 5 && spendable(g, p, false) >= 8) v += 0.5f;
    }
    if (pl.fleet >= 5 && pl.tactic <= 2) v += 0.4f;  // the free rearrangement moves fleet tokens into tactics
    if (g.modules & kModThundersEdge) v += 0.6f + aiFreeTacticalValue(g, p);  // revised card: a tactical action, no token
    return v;
}

// 0.42.0 (Ghoti, Discordant Stars: took Construction in round 1 with no planet to build on):
// nothing to build on - next to worthless; the Ghoti with planets but no dock - a second
// factory besides the flagship
float constructionAdjust(const Game& g, int p) {
    if (controlledPlanets(g, p).empty()) return -4.5f;
    if ((g.modules & kModDiscordant) && isFaction(g, p, "ghoti") && unitsOnBoard(g, p, UnitType::SpaceDock) == 0) return 2.f;
    return 0.f;
}

// Strategy card value with the objectives in mind (Normal / Hard).
float tradeCardIncome(const Game& g, int p);
float cardValueGoals(const Game& g, int p, int card) {
    const Player& pl = g.players[p];
    const Goals& G = aiGoals(g, p);
    const float level = aiLevelScale(g);
    float v = 0;
    switch (card) {
        case 0: {  // Leadership: tokens (and token objectives)
            v = 4.5f;
            if (pl.tactic + pl.strategy < G.keepTokens + 2) v += 2.f;
            if (pl.tactic <= 1) v += 1.f;
            if (aiCompetitive(g)) v += aiTune("lead", 1.5f);  // tokens fuel secondaries (secrets) and turns
            if (pl.tradeGoods - G.keepTg >= 8 && pl.tactic <= 3) v += 1.5f;  // 0.30.0: trade goods to turn into tokens
            break;
        }
        case 1: {  // Diplomacy: ready planets again (spend objectives), protect a system
            v = 3.f + (largestFleet(g, p) > 3 ? 0.5f : 0.f);
            if (G.keepRes + G.keepInf > 0) v += 1.5f;
            break;
        }
        case 2: v = 3.5f + std::min(2.f, G.actionCard * 0.5f + G.law * 0.5f); break;  // Politics
        case 3: {  // Construction
            v = unitsOnBoard(g, p, UnitType::SpaceDock) < 2 ? 5.5f : 3.5f;
            v += std::min(kVP, (G.structure + G.structOutside + G.pds + G.dock) * 0.6f);
            v += constructionAdjust(g, p);
            break;
        }
        case 4: v = 4.5f + (pl.tradeGoods < 3 ? 1.f : 0.f) + std::min(kVP, G.tg * 1.5f) + tradeCardIncome(g, p); break;  // Trade
        case 5: v = warfareValue(g, p, G); break;  // Warfare
        case 6: {  // Technology: techs our objectives want
            v = 5.5f + (spendable(g, p, false) >= 6 ? 1.5f : 0.f);
            float want = G.upgrade + G.factionTech;
            for (float c : G.color) want = std::max(want, c);
            v += std::min(kVP, want * 0.7f);
            break;
        }
        case 7: {  // Imperial: score a public objective now (+1 VP with Mecatol Rex, else a secret)
            v = 3.f + g.round * 0.3f;
            if (controlsMecatol(g, p)) v += kVP;
            else v += 1.f;
            if (G.metPublics >= 2) v += kVP;
            else if (G.metPublics == 1) v += kVP * 0.4f;
            if (aiCompetitive(g)) {  // a sure point (and an extra objective) is worth fighting the table for
                if (controlsMecatol(g, p)) v += kVP * aiTune("impmec", 0.8f);
                if (G.metPublics >= 1) v += kVP * aiTune("imppub", 0.6f);
            }
            break;
        }
    }
    v += g.cardTradeGoods[card] * 1.4f;
    // the speaker order matters less than the card, but an early card acts first
    v += (7 - card) * 0.05f;
    (void)level;
    return v;
}

// What the Trade card's X-1 trades should bring in (0.14.0 rules): neighbours'
// commodities but one each, if they go along (most do). Round 1 with the free
// refills house rule: nothing extra.
float tradeCardIncome(const Game& g, int p) {
    if (!(g.tradeRules & 9) || ((g.tradeRules & 2) && g.round <= 1)) return 0.f;
    if (g.tradeRules & 8) {  // community X-1 (0.17.0): about 1 trade good from each player who goes along
        float tg = 0;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p && !g.players[q].eliminated && !hostileFlag(g, p, q)) tg += areNeighbours(g, p, q) ? 0.9f : 0.6f;
        return std::min(tg, kVP * 0.5f);
    }
    float tg = 0;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && !g.players[q].eliminated && areNeighbours(g, p, q) && !hostileFlag(g, p, q))
            tg += float(std::max(0, int(kFactions[g.players[q].faction].commodities) - 1)) * 0.6f;
    return std::min(tg, kVP * 0.6f);
}

float cardValuePlain(const Game& g, int p, int card);
float cardValue(const Game& g, int p, int card) {
    float deny = 0.f;
    // (0.55.0) stop the winner: Imperial out of the hands of a threat who holds Mecatol Rex (its point, and a public)
    if (card == 7 && g.phase == Phase::Strategy && aiLevelScale(g) > 0.f && aiTune("stopwinner", 1.f) > 0.f) {
        const Endgame& E = aiEndgame(g, p);
        if (E.threat >= 0 && controlsMecatol(g, E.threat))
            deny = kVP * aiTune("denyimp", 0.8f) * aiLevelScale(g);
    }
    return cardValuePlain(g, p, card) + (card >= 0 && card < 8 ? aiStyle(g, p).card[card] : 0.f) + deny;
}
float cardValuePlain(const Game& g, int p, int card) {
    if (card < 0 || card >= kStrategyCards) return 0.f;
    if (aiLevelScale(g) >= 0.99f && aiTune("cards", 1.f) > 0.f) return cardValueGoals(g, p, card);
    const Player& pl = g.players[p];
    float v = 0;
    switch (card) {
        case 0: v = 5.5f; break;                                      // Leadership
        case 1: v = 3.0f + (largestFleet(g, p) > 3 ? 1.f : 0.f); break;  // Diplomacy
        case 2: v = 3.5f; break;                                      // Politics
        case 3: v = (unitsOnBoard(g, p, UnitType::SpaceDock) < 2 ? 6.5f : 4.0f) + constructionAdjust(g, p); break;
        case 4: v = 5.0f + (pl.tradeGoods < 3 ? 1.f : 0.f) + tradeCardIncome(g, p); break;    // Trade
        case 5: v = 4.0f; break;                                      // Warfare
        case 6: v = 6.5f + (spendable(g, p, false) >= 6 ? 1.5f : 0.f); break;  // Technology
        case 7: {                                                     // Imperial
            v = 5.0f + g.round * 0.5f;
            int mec = g.mecatol();
            if (mec >= 0)
                for (int16_t x : g.systems[mec].planets)
                    if (g.planets[x].owner == p) v += 4;
            for (int16_t o : g.publicObjectives)
                if (!hasScored(g, p, o) && meetsObjective(g, p, o)) v += 2;
            break;
        }
    }
    v += g.cardTradeGoods[card] * 1.4f;
    return v;
}

bool cardGoodNow(const Game& g, int p, int card, float bestTactical) {
    const Player& pl = g.players[p];
    switch (card) {
        case 0: return true;
        case 1: {
            int exhausted = 0;
            for (int x : controlledPlanets(g, p))
                if (g.planets[x].exhausted) ++exhausted;
            return exhausted >= 2 || bestTactical < 2;
        }
        case 2: return true;
        case 3: {
            // Construction (owner, 0.17.0): expand and score first, build after - a
            // structure on a new planet outside home is worth more (and counts for
            // objectives) than one at home on the first turn.
            if (pl.tactic == 0 || bestTactical < 2.f) return true;
            const int home = pl.homeSystem;
            int spots = 0;
            for (int x : controlledPlanets(g, p))
                if (g.planets[x].system != home && g.planets[x].units[p].structures() == 0 && !g.planets[x].destroyed) ++spots;
            if (spots == 0) return false;
            if (aiLevelScale(g) > 0.f) {
                const Goals& G = aiGoals(g, p);
                if (G.structure + G.structOutside + G.pds + G.dock >= 2.f) return true;  // an objective waits for it
            }
            return bestTactical < 4.f;
        }
        case 4: return true;
        case 5: {
            // Thunder's Edge's Warfare is a free tactical action (0.43.0 debug passes: played when there was nothing
            // worth doing - over a third of them moved nothing): now when there is a plan worth a tactic token
            if (g.modules & kModThundersEdge) return bestTactical >= 2.f || (pl.tactic == 0 && bestTactical > 0.5f);
            int onBoard = 0;
            for (const SystemState& s : g.systems)
                if (s.hasToken(p)) ++onBoard;
            return onBoard >= 2 || pl.tactic == 0 || bestTactical < 2;
        }
        case 6: return true;
        case 7: {
            if (aiLevelScale(g) > 0.f) {
                const Goals& G = aiGoals(g, p);
                if (G.metPublics >= 2 || (G.metPublics >= 1 && controlsMecatol(g, p))) return true;
                return pl.tactic == 0 || bestTactical < 2;
            }
            for (int16_t o : g.publicObjectives)
                if (!hasScored(g, p, o) && meetsObjective(g, p, o)) return true;
            return pl.tactic == 0 || bestTactical < 2;
        }
    }
    return true;
}

// ============================================================ technology choice
float techWant(const Game& g, int p, int t) {
    const TechDef& d = kTechs[t];
    static const struct {
        const char* key;
        float v;
    } table[] = {{"fl", 9}, {"st", 8}, {"gd", 7}, {"asc", 7}, {"hm", 7},  {"ps", 6},   {"da", 6},  {"amd", 5},
                 {"gls", 4}, {"dxa", 4}, {"x89_base", 4}, {"md_base", 4}, {"lwd", 4}, {"det", 3},
                 {"cr2", 7}, {"cv2", 7}, {"dn2", 6}, {"ws", 8},  {"sd2", 6}, {"dd2", 5}, {"ff2", 5}, {"inf2", 5}, {"pds2", 4},
                 // faction technologies (Stage 5 batch 1)
                 {"l4", 5}, {"nes", 6}, {"qdn", 4}, {"pm", 6}, {"ers", 6}, {"scc", 5}, {"vpw", 6}, {"it", 5}, {"nf", 6},
                 // batch 2
                 {"bio", 4}, {"is", 5}, {"mc", 7}, {"so", 5}, {"ng", 4}, {"yso", 5}, {"ic", 5}, {"tp", 3}, {"mi", 5},
                 // batch 3
                 {"wg", 4}, {"ds", 5}, {"mr", 5}, {"cm", 4}, {"lgf", 3}, {"htp", 4}, {"as", 4}, {"vw", 5},
                 // batch 4
                 {"ah", 5}, {"gr", 4}, {"pfa", 4}, {"sc", 6}, {"tcs", 4}, {"vtx", 5},
                 // Discordant Stars batch 1 (0.37.0)
                 {"dskortg", 4}, {"dskortr", 5}, {"dsvaylr", 5}, {"dslaneb", 2}, {"dslaner", 5}, {"dskyrog", 5}, {"dskyroy", 5},
                 {"dscheir", 5}, {"dsceldr", 3},
                 // batch 2 (0.38.0)
                 {"dskhrag", 4}, {"dsrhodb", 4}, {"dsrhody", 5}, {"dszelir", 5}, {"dsrohdy", 4}, {"dstnely", 3}, {"dskolug", 4}, {"dskolur", 5},
                 {"dsbentg", 4}, {"dsbenty", 4}, {"dsdihmy", 5}, {"dsflorg", 4}, {"dsgledb", 4}, {"dsaugug", 6}, {"dsauguy", 5}, {"dskjalg", 4},
                 {"dskollb", 4}, {"dskolly", 4}, {"dsmortr", 4}, {"dsmorty", 4}, {"dsvadeb", 2}, {"dsvadey", 5}, {"dscymiy", 4}, {"dsnivyy", 4},
                 {"dsolrab", 2}, {"dsolrar", 4},
                 // Discordant Stars batch 5 (0.41.0)
                 {"dsfreeg", 4}, {"dsfreey", 4}, {"dsedyng", 5}, {"dsedyny", 4}, {"dsnokay", 4}, {"dsaxisb", 1}, {"dsaxisy", 3},
                 {"dsmykog", 5}, {"dsghemg", 4},
                 // Discordant Stars batch 6 (0.42.0)
                 {"dsveldr", 5}, {"dslizhb", 2}, {"dsmirvr", 4}, {"dsghotg", 5}, {"dsghoty", 5},
                 // Thunder's Edge faction technologies (0.43.0 debug passes: they all sat at the default 2)
                 {"proxima", 4}, {"hydrothermal", 4}, {"radical", 4}, {"subatomic", 5}, {"nanomachines", 5},
                 {"planesplitter-firm", 3}, {"parasite-firm", 4},
                 // cybernetic debug pass (0.44.5: these sat at the default 2)
                 {"sdn", 3.5f}, {"td", 2.5f}, {"ie", 4}, {"pi", 3}, {"asn", 5}, {"executiveorder", 5},
                 // biotic debug pass (0.44.6)
                 {"nm", 4.5f}, {"bs", 3}};
    float v = 2;
    for (const auto& e : table)
        if (keyIs(d.key, e.key)) v = e.v;
    // warfare debug pass (0.44.8): AI Development Algorithm and Self-Assembly Routines sat at the default 2; the
    // Codex Magen Defense Grid (infantry on every planet with a structure, a free hit) is one of the best red cards
    if (keyIs(d.key, "aida")) v = 3.f;
    if (keyIs(d.key, "sar")) v = 3.f + 0.3f * float(std::min(5, unitsOnBoard(g, p, UnitType::Mech)));
    if (keyIs(d.key, "md_base") && codexOn(g)) v = 5.5f;
    if (keyIs(d.key, "mr")) {  // Magmus Reactor: only worth it with a supernova within reach
        int near = 99;
        for (size_t s = 0; s < g.systems.size(); ++s) {
            if (!(g.systems[s].d().anomalies & kSupernova) || g.systems[s].offBoard) continue;
            for (size_t m = 0; m < g.systems.size(); ++m)
                if (!g.systems[m].offBoard && (int(m) == g.players[size_t(p)].homeSystem || hasUnits(g, p, int(m))))
                    near = std::min(near, hexDistance(g.systems[s].pos, g.systems[m].pos));
        }
        v = near <= 2 ? 5.f : near <= 3 ? 3.5f : 1.5f;
    }
    if (keyIs(d.key, "pa")) {  // Psychoarchaeology (0.44.6): worth as much as the specialty planets we hold
        int skips = 0;
        for (int x : controlledPlanets(g, p)) skips += planetHasSkip(g, x) ? 1 : 0;
        v = std::min(7.f, 2.f + float(skips));
    }
    if (d.unitUpgrade && d.faction >= 0) v = 7;  // faction unit upgrades
    if (!techImplemented(t)) v = std::min(v, 2.5f);
    {
        const Style& S = aiStyle(g, p);  // personality: research appetite, favourite upgrades
        v *= S.research;
        // (the L1Z1X review, checkpoint 2 - test knob prefresolve) a favourite upgrade named by its generic key matched a
        // faction's own only when the first two letters agreed (ff2 / ffac2): Maximum Impact's "dn2" never matched
        // Super-Dreadnought II (sdn2), nor the Argent's "dd2" Strike Wing Alpha II (swa2). Tested on Maximum Impact (90
        // games): 17 wins / 7.10 VP against 17 / 7.09, Super-Dreadnought II in 19 games either way (its prerequisites
        // decide, and a researchable faction upgrade is worth 7 already). Owner 2026-11-12 18:11 "keep it on for now for
        // the L1Z1X": on for them (l1pref, default 1); for everyone else (the Argent) prefresolve stays off until their review
        // (the Argent review, checkpoint 2 - test knob argpref, default off) Strike Doctrine's "dd2" -> Strike Wing Alpha II:
        // 22 wins / 7.72 VP against 21 / 7.71 (forced Strike Doctrine 19 / 7.64 against 18 / 7.62), Strike Wing Alpha II
        // researched in 62 games either way - it heads the Argent's core path already. OFF
        const bool resolve = (aiTune("prefresolve", 0.f) > 0.f || (aiTune("l1pref", 1.f) > 0.f && isFaction(g, p, "l1z1x")) ||
                              (aiTune("argpref", 0.f) > 0.f && isFaction(g, p, "argent"))) &&
                             d.unitUpgrade && d.faction >= 0;
        for (const char* k : S.techPref)
            if (k && (keyIs(d.key, k) || (d.unitUpgrade && d.faction >= 0 && !std::strncmp(k, d.key, 2)) ||
                      (resolve && aiResolveTech(g, p, k) == t))) v += 4.f;
    }
    v += aiCoreTechBonus(g, p, t);  // the Strategy Core: the faction's technology path, answers to the neighbours
    // (the Naalu review, checkpoint 2 - test knob ngwant, default off) Neuroglaive was researched in 7% of games. Tested
    // (ngwant=3): researched in 24-32% of games; TE 25 wins / 7.51 VP against 22 / 7.46, fresh 20 / 7.53 against 21 / 7.51,
    // plain fresh 18 / 7.38 against 20 / 7.44 - no gain. OFF
    if (aiTune("ngwant", 0.f) > 0.f && keyIs(d.key, "ng")) v += aiTune("ngwant", 0.f);
    // (the Yin review, checkpoint 2 - test knob yinspin) Yin Spinner Omega (2 infantry after every production) was
    // researched in 10 games of 90. Tested (yinspin=4): researched in 46 games, 332 uses, 21 wins / 7.24 VP against
    // 19 / 7.06; fresh seeds 14 / 7.04 against 11 / 6.83 - ON since the owner's go (2026-11-12); yinspin=0 = the old way
    if (aiTune("yinspin", 4.f) > 0.f && keyIs(d.key, "yso") && codexOn(g)) v += aiTune("yinspin", 4.f);
    // objectives that reward colours / upgrades / faction technology
    if (aiLevelScale(g) > 0.f) {
        const Goals& G = aiGoals(g, p);
        if (d.unitUpgrade) v += G.upgrade;
        else if (d.color != TechColor::None) v += G.color[int(d.color)];
        if (d.faction >= 0) v += G.factionTech;
        int req = d.req[0] + d.req[1] + d.req[2] + d.req[3];
        return v + req * 0.3f;
    }
    for (int16_t o : g.publicObjectives) {
        const char* k = kObjectives[o].key;
        if (hasScored(g, p, o)) continue;
        if (d.unitUpgrade && (keyIs(k, "develop") || keyIs(k, "revolutionize"))) v += 3;
        if (!d.unitUpgrade && d.color != TechColor::None && (keyIs(k, "diversify") || keyIs(k, "master_science")) &&
            techCount(g, p, d.color) == 1)
            v += 3;
    }
    // cheap prerequisites are better (keeps colours concentrated)
    int req = d.req[0] + d.req[1] + d.req[2] + d.req[3];
    v += req * 0.3f;
    return v;
}

// ============================================================ hits / payment / tokens
const UnitType kSacrificeOrder[] = {UnitType::Fighter, UnitType::Infantry, UnitType::Destroyer, UnitType::Cruiser,
                                    UnitType::Carrier, UnitType::Mech, UnitType::Dreadnought, UnitType::Flagship,
                                    UnitType::Flagship2, UnitType::WarSun, UnitType::PDS, UnitType::SpaceDock};

}  // namespace

// (0.44.7) what a tactical action into sys is worth to p as the planner sees it now - for "would this bonus help?"
// questions (Midas Turbine, Geosympathic Impeller, Rift Engines): compare a copy of the game with the bonus
float aiMovePlanValue(const Game& g, int p, int sys) {
    if (sys < 0 || sys >= int(g.systems.size())) return 0.f;
    return buildPlan(g, p, sys, false).value;
}

Answer aiHits(const Game& g, const Decision& d) {
    Answer a;
    const int p = d.player;
    const Force& f = d.planet >= 0 ? g.planets[d.planet].units[p] : g.systems[d.system].space[p];
    int need = std::min(d.hits, maxAbsorbableHits(g, p, d.system, d.planet, d.hitMask, d.noSustain));
    auto allowed = [&](UnitType t) { return !d.hitMask || ((d.hitMask >> int(t)) & 1); };
    // sustain damage first (most valuable units first)
    // (0.42.1: every type that can sustain here - Hel-Titan PDS, and Discordant Stars units with Sustain Damage
    // were missing, so a bombardment the rules said could be absorbed had no answer)
    for (UnitType t : {UnitType::WarSun, UnitType::Flagship, UnitType::Flagship2, UnitType::Dreadnought, UnitType::Mech, UnitType::Carrier,
                       UnitType::Cruiser, UnitType::Destroyer, UnitType::PDS, UnitType::SpaceDock, UnitType::Infantry, UnitType::Fighter}) {
        if (d.noSustain) break;
        if (!allowed(t) || !unitSustainsAt(g, p, t, d.system, d.planet)) continue;
        int k = f.n[int(t)] - f.dmg[int(t)];
        const int worth = sustainWorth(g, p);  // Non-Euclidean Shielding: 2
        for (int i = 0; i < k && need > 0; ++i, need -= worth) a.data.insert(a.data.end(), {int(t), 1});
    }
    // Metali Void Shielding (Thunder's Edge relic) / Splintering Gale (Khrask): ships without Sustain Damage that may
    // use it - the most valuable first
    int extra = d.noSustain ? 0 : extraSustains(g, p, d.system, d.planet);
    for (UnitType t : {UnitType::Carrier, UnitType::Cruiser, UnitType::Destroyer, UnitType::Dreadnought, UnitType::Flagship,
                       UnitType::Flagship2, UnitType::WarSun}) {
        if (extra <= 0 || need <= 0) break;
        if (!allowed(t) || unitSustainsAt(g, p, t, d.system, d.planet) || !metaliSustain(g, p, t, d.system, d.planet)) continue;
        for (int k = f.n[int(t)] - f.dmg[int(t)]; k > 0 && extra > 0 && need > 0; --k, --extra) {
            a.data.insert(a.data.end(), {int(t), 1});
            need -= sustainWorth(g, p);
        }
    }
    int used[kUnitTypes] = {};
    for (UnitType t : kSacrificeOrder) {
        if (!allowed(t)) continue;
        int k = f.n[int(t)];
        for (int i = 0; i < k && need > 0; ++i, --need) {
            a.data.insert(a.data.end(), {int(t), 0});
            ++used[int(t)];
        }
    }
    return a;
}

Answer aiPayment(const Game& g, const Decision& d) {
    Answer a;
    const int p = d.player;
    const Player& pl = g.players[p];
    // value lost by exhausting a planet = its "other" number
    struct P {
        int id, give, lose;
    };
    std::vector<P> ps;
    for (int x : d.planets)
        ps.push_back({x, planetPayValue(g, x, d.influence, d.mixed),
                      d.mixed ? std::min(planetResources(g, x), planetInfluence(g, x))
                              : d.influence ? planetResources(g, x) : planetInfluence(g, x)});
    std::sort(ps.begin(), ps.end(), [](const P& a, const P& b) {
        return a.give * 10 - a.lose * 4 > b.give * 10 - b.lose * 4;
    });
    if (ps.size() > 24) ps.resize(24);
    float bestCost = 1e9f;
    std::vector<int> bestPicks;
    int bestTg = 0;
    const int maxSum = d.amount + 8;
    // dp over sums: cost and chosen set
    std::vector<float> cost(maxSum + 1, 1e9f);
    std::vector<uint32_t> set(maxSum + 1, 0);
    cost[0] = 0;
    for (size_t i = 0; i < ps.size(); ++i) {
        for (int s = maxSum; s >= 0; --s) {
            if (cost[s] >= 1e8f) continue;
            int ns = std::min(maxSum, s + ps[i].give);
            float nc = cost[s] + ps[i].lose * 0.6f + 0.05f;
            if (nc < cost[ns]) {
                cost[ns] = nc;
                set[ns] = set[s] | (1u << i);
            }
        }
    }
    const int worth = d.allowTradeGoods ? tgWorth(g, p) : 1;  // Mirror Computing
    const bool protect = aiLevelScale(g) > 0.f && aiReserveOk(g, p, {}, 0);
    for (int tg = 0; tg <= pl.tradeGoods && (tg - 1) * worth < d.amount; ++tg) {
        if (tg > 0 && !d.allowTradeGoods) break;
        int needP = std::max(0, d.amount - tg * worth);
        for (int s = std::max(0, needP); s <= maxSum; ++s) {
            if (cost[s] >= 1e8f) continue;
            float c = cost[s] + (s - needP) * 0.5f + tg * 1.1f;
            if (c < bestCost) {
                // respect a reserved influence amount (amount2)
                std::vector<int> picks;
                for (size_t i = 0; i < ps.size(); ++i)
                    if ((set[s] >> i) & 1) picks.push_back(ps[i].id);
                Answer test;
                test.picks = picks;
                test.number = tg;
                if (!checkPayment(g, p, d, test).empty()) continue;
                // don't spend what we keep for the status phase, if another way exists
                if (protect && !aiReserveOk(g, p, picks, tg)) {
                    c += 30.f;
                    if (c >= bestCost) continue;
                }
                bestCost = c;
                bestPicks = picks;
                bestTg = tg;
            }
        }
    }
    if (bestCost >= 1e8f) {
        // fall back: everything
        for (int x : d.planets) a.picks.push_back(x);
        a.number = d.allowTradeGoods ? std::min<int>(pl.tradeGoods, std::max(0, (d.amount + worth - 1) / worth)) : 0;
        return a;
    }
    a.picks = bestPicks;
    a.number = bestTg;
    return a;
}

Answer aiTokens(const Game& g, const Decision& d) {
    const Player& pl = g.players[d.player];
    int t = pl.tactic, f = pl.fleet, s = pl.strategy;
    int n = d.newTokens;
    // ships the fleet pool must cover (Armada: 2 fewer)
    int maxShips = std::max(0, largestFleet(g, d.player) - (fleetLimit(g, d.player) - pl.fleet));
    // (the Letnev review, checkpoint 2 - test knob darktok) Darktalon Treilla lifts the limit only until the round ends: keep
    // the fleet tokens the ships will need then (the status phase's redistribution used to empty the fleet pool). Alone, 90
    // games: ships lost over the limit 60 -> 25 but 11 wins / 6.78 VP against 13 / 6.83 (fresh 10 / 6.72 against 12 / 6.74).
    const bool darkTok = aiTune("darktok", 1.f) > 0.f && heroActive(g, d.player, "letnev");
    if (darkTok) maxShips = std::max(0, largestFleet(g, d.player) - 2);
    Answer a;
    int wantFleet = aiStyle(g, d.player).fleetTokens;  // Warlord, Turtler: big fleets
    // Hard and up: Raise a Fleet / Command an Armada need that many ships in one
    // system, and the fleet pool caps them - grow it toward the objective
    if (hardAI(g)) {
        const Goals& G = aiGoals(g, d.player);
        if (G.fleetTarget > 0 && G.fleetTarget <= (aiCompetitive(g) ? 6 : 5) && G.fleetShip > 0) wantFleet = std::max(wantFleet, G.fleetTarget);
    }
    if (!d.redistribute) {
        const bool keepStrategy = aiLevelScale(g) > 0.f && aiTune("tokstrat", 1.f) > 0.f;  // secondaries: Construction, research, secret objectives
        for (int i = 0; i < n; ++i) {
            if (darkTok && f < maxShips && (d.value <= 0 || f < d.value)) ++f;  // (the ships kept after the round)
            else if (wantFleet > 0 && t >= 2 && f < wantFleet && f < std::min(5, d.value > 0 ? d.value : 5)) ++f;
            else if (keepStrategy && t >= 2 && s < 1) ++s;
            else if (t < 3) ++t;
            else if (s < 2) ++s;
            else if (f < std::max(3, maxShips + 1) && f < std::min(5, d.value > 0 ? d.value : 5)) ++f;
            else ++t;
        }
    } else {
        int total = t + f + s + n;
        // fleet: enough for our biggest fleet (+1 to grow), but not more than 5
        int fleetMin = std::min<int>(pl.fleet, maxShips);  // the rules forbid shrinking below this
        int fleetT = std::max(fleetMin, std::min(4, std::max(2, maxShips + 1)));
        if (wantFleet > 0) fleetT = std::max(fleetT, std::min(wantFleet, total - 3));
        if (darkTok) fleetT = std::max(fleetT, std::min(maxShips, total - 2));  // (the ships kept after the round)
        if (d.value > 0) fleetT = std::min(fleetT, std::max<int>(pl.fleet, d.value));  // Fleet Regulations (0.25.0 fix)
        fleetT = std::min(fleetT, total);
        int rest = total - fleetT;
        int stratT = rest >= 6 ? 2 : (rest >= 3 ? 1 : 0);
        int tacT = rest - stratT;
        t = tacT;
        f = fleetT;
        s = stratT;
    }
    a.data = {t, f, s};
    return a;
}

namespace {

Answer aiMove(const Game& g, const Decision& d) {
    Plan p = buildPlan(g, d.player, d.system, false);
    Answer a;
    a.data = p.move;
    if (std::getenv("TI4_AIDEBUG") && !checkMovePlan(g, d.player, d.system, a.data, false, true).empty())
        std::fprintf(stderr, "AIMOVE %s into %s: %s (%zu moves)\n", playerName(g, d.player).c_str(), kSystems[g.systems[d.system].def].name,
                     checkMovePlan(g, d.player, d.system, a.data, false, true).c_str(), a.data.size() / 4);
    if (!checkMovePlan(g, d.player, d.system, a.data, false, true).empty()) a.data.clear();
    return a;
}

Answer aiBombard(const Game& g, const Decision& d) {
    // Every bombarding unit fires at the defended planet we most want.
    Answer a;
    const int p = d.player;
    int best = -1;
    float bv = -1;
    for (int pl : d.planets) {
        int def = 0;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p) def += g.planets[pl].units[q].groundForces();
        float v = planetValue(g, p, pl) + def * 0.5f;
        if (v > bv) {
            bv = v;
            best = pl;
        }
    }
    if (best < 0) return a;
    for (int t = 0; t < kUnitTypes; ++t) {
        int n = bombardAvailable(g, p, d.system, UnitType(t));  // Annihilator: mechs on planets too
        if (((d.hitMask >> t) & 1) && n) a.data.insert(a.data.end(), {best, t, n});
    }
    LandSpec spec;
    spec.typeMask = d.hitMask;
    spec.targets = d.planets;
    spec.bombard = true;
    if (!checkLandPlan(g, p, d.system, a.data, spec).empty()) a.data.clear();
    return a;
}

Answer aiLand(const Game& g, const Decision& d) {
    if (d.tag == Tag::BombardPlan) return aiBombard(g, d);
    Answer a;
    const int p = d.player;
    const SystemState& s = g.systems[d.system];
    int inf = s.space[p].count(UnitType::Infantry), mech = s.space[p].count(UnitType::Mech);
    struct T {
        int pl;
        int need;
        float value;
    };
    std::vector<T> targets;
    // (the Firmament bot review, owner 2026-11-11) Viper EX-23: one mech coexists on another player's non-home planet
    const bool viperOn = (g.modules & kModThundersEdge) && mechAbilities(g) && isFaction(g, p, "firmament") && mech > 0 &&
                         aiTune("viperplan", 1.f) > 0.f && aiLevelScale(g) > 0.f;
    std::vector<char> viperT;
    for (int16_t pl : s.planets) {
        if (g.planets[pl].owner == p || isDMZ(g, pl)) continue;
        if (s.custodians && !g.custodiansRemoved) continue;
        int def = 0;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p) def += g.planets[pl].units[q].groundForces();
        const int po = g.planets[pl].owner;
        const bool vip = viperOn && def > 0 && po >= 0 && !isHomePlanet(g, pl) && !((g.planets[pl].coexist >> p) & 1);
        targets.push_back({pl, vip ? 1 : def > 0 ? sardakkNeed(g, p, pl, def * 2 + 1) : 1, planetValue(g, p, pl) * (vip ? aiTune("viperv", 0.6f) : 1.f)});
        viperT.push_back(vip ? 1 : 0);
    }
    if (d.planet >= 0)  // custodians removed: Mecatol Rex must get at least one
        for (T& t : targets)
            if (t.pl == d.planet) t.value += 100;
    if (!viperOn) std::sort(targets.begin(), targets.end(), [](const T& a, const T& b) { return a.value > b.value; });
    else {  // (sorted together with their Viper marks)
        std::vector<size_t> ord(targets.size());
        for (size_t i = 0; i < ord.size(); ++i) ord[i] = i;
        std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return targets[a].value > targets[b].value; });
        std::vector<T> t2;
        std::vector<char> v2;
        for (size_t i : ord) {
            t2.push_back(targets[i]);
            v2.push_back(viperT[i]);
        }
        targets.swap(t2);
        viperT.swap(v2);
    }
    std::vector<int> give(targets.size(), 0);
    // Mechanize the Military (0.43.0 debug passes - 3 scored of 65 held): mechs the invasion doesn't need go one each
    // onto our own planets here that have none (they used to sit together on the dock planet)
    std::vector<int> ownSpots;
    if (aiLevelScale(g) > 0.f && aiGoals(g, p).mech > 0.f && mech > 0) {
        int sumNeed = 0, vc = 0;
        for (size_t i = 0; i < targets.size(); ++i) {
            sumNeed += targets[i].need;
            vc += viperT[i];
        }
        vc = std::min(vc, mech);  // (Viper targets keep their mechs)
        int spare = std::max(0, mech - vc - std::max(0, sumNeed - vc - inf));
        for (int16_t pl : s.planets)
            if (spare > 0 && g.planets[pl].owner == p && !isDMZ(g, pl) && !g.planets[pl].units[p].count(UnitType::Mech) &&
                !g.planets[pl].destroyed) {
                ownSpots.push_back(pl);
                --spare;
                --mech;
            }
    }
    int total = inf + mech;
    // 0.44.2 (the custodians, tester: nobody went for Mecatol Rex): next to Mecatol Rex (or in its space) with the
    // custodians still there and the influence for them, up to 2 ground forces the landing doesn't need stay aboard
    int keepAboard = 0;
    if (aiLevelScale(g) > 0.f && !g.custodiansRemoved && g.mecatol() >= 0 && aiTune("seatkeep", 1.f) > 0.f &&
        (d.system == g.mecatol() || adjacentSystems(g, d.system, g.mecatol()))) {
        int infAll = g.players[size_t(p)].tradeGoods;
        for (int x : controlledPlanets(g, p)) infAll += planetInfluence(g, x);
        bool carrier = false;
        for (int t : kShipIdx)
            if (s.space[p].n[t] && unitDef(g, p, UnitType(t)).capacity > 0) carrier = true;
        int need = 0;
        for (const T& t : targets) need += t.need;
        if (infAll >= aiCustodiansInf(g, p) && carrier) keepAboard = std::min(2, std::max(0, total - need));
        // (infantry stay: the spare mechs keep their place in the spreading below)
        const int stayInf = std::min(keepAboard, inf);
        inf -= stayInf;
        total -= stayInf;
    }
    // ... and one mech to each target planet first, the infantry make up the rest
    std::vector<int> mechTo(targets.size(), 0);
    for (size_t i = 0; i < targets.size() && mech > 0; ++i)  // (a Viper target gets its mech first)
        if (viperT[i] && total > 0) {
            mechTo[i] = 1;
            give[i] = 1;
            --mech;
            --total;
        }
    if (aiLevelScale(g) > 0.f && aiGoals(g, p).mech > 0.f && mech > 1)
        for (size_t i = 0; i < targets.size() && mech > 0; ++i)
            if (!mechTo[i] && targets[i].need <= 1 + inf) {  // (not a fight the infantry can't carry alone)
                mechTo[i] = 1;
                --mech;
            }
    for (size_t i = 0; i < targets.size() && total > 0; ++i) {
        int k = std::min(std::max(targets[i].need, mechTo[i]) - give[i], total);
        if (k <= 0) continue;
        give[i] += k;
        total -= k;
    }
    // spread leftovers onto the contested / most valuable targets
    // (Occupy the Fringe, 0.43.0: onto the planet that gets the most - a stack of 9 is the point)
    if (total > 0 && !targets.empty() && aiLevelScale(g) > 0.f && aiGoals(g, p).groundStack > 0.f) {
        size_t bi = 0;
        for (size_t i = 1; i < targets.size(); ++i)
            if (give[i] > give[bi]) bi = i;
        give[bi] += total;
        total = 0;
    }
    for (size_t i = 0; i < targets.size() && total > 0; ++i) {
        give[i] += total;
        total = 0;
    }
    for (size_t i = 0; i < targets.size(); ++i) {
        int k = give[i];
        int m = std::min(k, mech + mechTo[i]);
        if (mechTo[i]) {  // (the spread mech, then infantry before any more mechs)
            const int ii = std::min(k - 1, inf);
            m = 1 + std::min(std::max(0, k - 1 - ii), mech);
        }
        if (m) {
            a.data.insert(a.data.end(), {targets[i].pl, int(UnitType::Mech), m});
            mech -= m - mechTo[i];
            k -= m;
        }
        if (k > 0) {
            int ii = std::min(k, inf);
            if (ii) {
                a.data.insert(a.data.end(), {targets[i].pl, int(UnitType::Infantry), ii});
                inf -= ii;
            }
        }
    }
    for (int pl : ownSpots) a.data.insert(a.data.end(), {pl, int(UnitType::Mech), 1});
    // 0.44.2 (tester: "solidify" Mecatol Rex / Styx): ground forces nobody needed go onto a seat we hold, up to 3 there
    if (aiLevelScale(g) > 0.f && aiTune("seatguard", 1.f) > 0.f && targets.empty() && d.planet < 0)
        for (int16_t pl : s.planets) {
            if (g.planets[pl].owner != p || !aiSeatPlanet(g, pl)) continue;
            const int room = std::max(0, 3 - g.planets[pl].units[p].groundForces());
            const int ii = std::min(room, inf), mm = std::min(room - ii, mech);
            if (ii) a.data.insert(a.data.end(), {int(pl), int(UnitType::Infantry), ii});
            if (mm) a.data.insert(a.data.end(), {int(pl), int(UnitType::Mech), mm});
            break;
        }
    LandSpec spec;
    spec.mustLand = d.planet;
    // Matriarch (Naalu flagship): the fighters join the landing on the first contested planet
    if ((d.hitMask >> int(UnitType::Fighter)) & 1) {
        spec.typeMask = d.hitMask;
        int fighters = s.space[p].count(UnitType::Fighter), target = -1;
        for (size_t i = 0; i < targets.size() && target < 0; ++i)
            if (give[i] > 0 && targets[i].need > 1) target = targets[i].pl;
        if (fighters > 0 && target >= 0) {
            std::vector<int> with = a.data;
            with.insert(with.end(), {target, int(UnitType::Fighter), fighters});
            if (checkLandPlan(g, p, d.system, with, spec).empty()) a.data = with;
        }
    }
    if (!checkLandPlan(g, p, d.system, a.data, spec).empty()) {
        a.data.clear();
        if (d.planet >= 0) {  // fall back: one ground force onto the required planet
            int t = s.space[p].count(UnitType::Infantry) ? int(UnitType::Infantry) : int(UnitType::Mech);
            a.data = {d.planet, t, 1};
        }
    }
    return a;
}

Answer aiProduceCore(const Game& g, const Decision& d);
// (0.44.5) Contractual Obligations: when 1 unit must be made, the cheapest that the rules accept
Answer aiProduce(const Game& g, const Decision& d) {
    Answer a = aiProduceCore(g, d);
    if (!d.mustProduce || !d.validate) return a;
    int total = 0;
    for (int t = 0; t < kUnitTypes && t < int(a.data.size()); ++t) total += a.data[size_t(t)];
    if (total > 0 && d.validate(a).empty()) return a;
    int best = -1, bestCost = 1 << 20;
    for (int t : kShipIdx) {
        Answer b;
        b.data.assign(kUnitTypes + 1, 0);
        b.data[size_t(t)] = 1;
        b.data[kUnitTypes] = a.data.size() > size_t(kUnitTypes) ? a.data[kUnitTypes] : -1;
        if (!d.validate(b).empty()) continue;
        const int c = unitCost2(g, d.player, UnitType(t));
        if (c < bestCost) {
            bestCost = c;
            best = t;
        }
    }
    if (best >= 0) {
        a.data.assign(kUnitTypes + 1, 0);
        a.data[size_t(best)] = 1;
        a.data[kUnitTypes] = -1;
    }
    return a;
}

Answer aiProduceCore(const Game& g, const Decision& d) {
    const int p = d.player;
    const int sys = d.system;
    std::vector<int> plan(kUnitTypes + 1, 0);
    // ground forces go to a planet with our space dock (or space, for floating factories)
    std::vector<int> dest = groundForceDestinations(g, p, sys);
    int gfPlanet = dest.empty() ? -1 : dest[0];
    plan[kUnitTypes] = gfPlanet;
    int budget = spendable(g, p, false);
    const bool free = d.context.find("Everything is free this time") != std::string::npos;  // (Harrugh Gefhara, Matriphagy)
    if (free) budget = std::max(budget, d.costCap > 0 ? d.costCap : 99);
    if (budget <= 1 && !(free && budget >= 1)) {
        Answer a;
        a.data = plan;
        return a;
    }
    // keep enough for a "spend" objective we can score this status phase
    Reserve r = objectiveReserve(g, p);
    if (!free) budget = std::max(0, budget - r.resources - r.tradeGoods);
    if (aiLevelScale(g) > 0.f && !free) budget = std::min(budget, aiFreeSpend(g, p, false));
    if (d.costCap > 0) budget = std::min(budget, d.costCap);  // Maximus (Dih-Mohn flagship)
    if (d.planet >= 0) {  // Integrated Economy: cost capped by the planet's resources, ground forces go there
        budget = std::min(budget, planetResources(g, d.planet));
        plan[kUnitTypes] = d.planet;
    }
    auto costOf = [&](const std::vector<int>& pl) {
        int c[kUnitTypes];
        for (int t = 0; t < kUnitTypes; ++t) c[t] = pl[t];
        if (free) return unitsCostValue(g, p, c);  // (only the combined-cost limit counts)
        const int paid = std::max(0, productionCost(g, p, c) - amalgamationSavings(g, p, c)) + terrafactoryExtra(g, p, d.system, c);  // Vuil'raith: captured units pay
        return d.costCap > 0 ? std::max(paid, unitsCostValue(g, p, c)) : paid;
    };
    // That Which Molds Flesh: the decision shows 2 more, the limit itself is 2 lower (fighters / infantry only)
    const int limit = d.amount - (d.context.find("Molds Flesh") != std::string::npos ? 2 : 0);
    // (0.59.0, owner: "the Crimson don't expand") their home is beside the board: ground forces built there wait for a
    // ship to carry them out - only as many as the ships there (and those built now) can take; and 2 mechs stay in
    // the reinforcements for the Revenant
    const bool besideHome = isFaction(g, p, "crimson") && sys >= 0 && sys == g.players[size_t(p)].homeSystem && g.systems[size_t(sys)].offBoard &&
                            aiTune("crimsonhome", 1.f) > 0.f;
    int homeRoom = 0;
    if (besideHome) {
        const Force& hf = g.systems[size_t(sys)].space[size_t(p)];
        homeRoom = forceCapacity(g, p, hf) - std::max(0, hf.count(UnitType::Fighter) - dockFighterSlots(g, p, sys)) - hf.groundForces();
        for (int16_t x : g.systems[size_t(sys)].planets) homeRoom -= g.planets[size_t(x)].units[size_t(p)].groundForces();
    }
    auto tryAdd = [&](UnitType t, int k) {
        std::vector<int> next = plan;
        next[int(t)] += k;
        if (besideHome && (isGroundForce(t) || t == UnitType::Fighter)) {
            if (t == UnitType::Mech && mechAbilities(g) && (g.modules & kModThundersEdge) &&
                unitsAvailable(g, p, UnitType::Mech) - next[int(UnitType::Mech)] < 2)
                return false;
            int room = homeRoom;
            for (int u : kShipIdx)
                if (u != int(UnitType::Fighter)) room += next[size_t(u)] * unitDef(g, p, UnitType(u)).capacity;
            // (fighters beyond the docks' room take carrier space too: a carrier boxed in by fighters can't take
            // the ground forces out - its moves were refused as "stranding" them)
            const Force& hf = g.systems[size_t(sys)].space[size_t(p)];
            const int slots = dockFighterSlots(g, p, sys);
            const int extraF = std::max(0, hf.count(UnitType::Fighter) + next[int(UnitType::Fighter)] - slots) -
                               std::max(0, hf.count(UnitType::Fighter) - slots);
            if (next[int(UnitType::Infantry)] + next[int(UnitType::Mech)] + extraF > room) return false;
        }
        if (costOf(next) > budget) return false;
        if (g.fixes >= 14 && sys >= 0) {  // (0.45.0: the rules let a production go over the fleet pool - the bots don't)
            Force after = g.systems[size_t(sys)].space[size_t(p)];
            for (int u = 0; u < kUnitTypes; ++u)
                if (!isGroundForce(UnitType(u))) after.n[u] = uint8_t(after.n[u] + next[size_t(u)]);
            const int use = fleetUse(g, p, sys, after);
            if (use > fleetLimit(g, p) && use > fleetUseIn(g, p, sys)) return false;
        }
        if (d.planet >= 0 && d.validate) {  // Integrated Economy (after 0.44.4): its own rules - ground forces on that planet, no dock
            Answer t;
            t.data = next;
            if (!d.validate(t).empty()) return false;
        } else if (!checkProducePlan(g, p, sys, next, limit, d.hitMask, true).empty()) {  // (0.45.0: bots stay within the fleet pool)
            return false;
        }
        plan = next;
        return true;
    };
    // Combat Drones (Mirveda): no infantry from docks - fighters are the invaders
    const bool drones = (g.modules & kModDiscordant) && isFaction(g, p, "mirveda") && aiTune("drones", 1.f) > 0.f;
    int infHere = 0;
    for (int16_t pl : g.systems[sys].planets) infHere += g.planets[pl].units[p].count(UnitType::Infantry);
    if (unitsOnBoard(g, p, UnitType::Infantry) >= 24) infHere = 99;  // plenty already (Arborec, Yin Spinner...)
    int capacityTotal = 0;
    for (size_t s = 0; s < g.systems.size(); ++s) capacityTotal += shipCapacity(g, p, int(s));
    // priorities: first what an objective asks for
    if (aiLevelScale(g) > 0.f && aiTune("objprod", 1.f) > 0.f) {
        const Goals& G = aiGoals(g, p);
        // (0.52.0: the Ghemina's second flagship, the Lady, after their first)
        if (G.flagshipOrWarSun > 0 && !tryAdd(UnitType::Flagship, 1) && !tryAdd(UnitType::Flagship2, 1)) tryAdd(UnitType::WarSun, 1);
        if (G.supremacy > 0 && unitsOnBoard(g, p, UnitType::Flagship) + unitsOnBoard(g, p, UnitType::Flagship2) == 0 &&
            !tryAdd(UnitType::Flagship, 1))
            tryAdd(UnitType::Flagship2, 1);
        if (G.dread > 0) {
            ObjProgress best;
            for (int16_t o : g.players[p].secrets)
                if (!std::strcmp(kObjectives[o].key, "gamf")) best = objectiveProgress(g, p, o);
            for (int k = 0; k < std::max(1, best.missing()) && k < 2; ++k) tryAdd(UnitType::Dreadnought, 1);
        }
        if (G.mech > 0) tryAdd(UnitType::Mech, 2);
        // (Sardakk review, owner 2026-11-11 - test D) a mech or two for the invasions once Valkyrie Particle Weave or G'hom
        // Sek'kus is in (they sustain and strike back)
        if (isFaction(g, p, "sardakk") && mechAbilities(g) && aiTune("sardmech", 0.f) > 0.f &&
            (ownsTechKey(g, p, "vpw") || commanderUsable(g, p, "sardakk")) && unitsOnBoard(g, p, UnitType::Mech) < int(aiTune("sardmechs", 2.f)))
            tryAdd(UnitType::Mech, 1);
        // (the Firmament bot review, owner 2026-11-11) Viper EX-23: a mech or two for the Viper plans (buildPlan) while
        // other players' non-home planets with ground forces are within 2 systems - OFF (test knob vipermechs=2): in 90
        // games the extra mechs cost the Firmament ~0.4 VP and the Viper plans came up 0.19 times a game
        if ((g.modules & kModThundersEdge) && mechAbilities(g) && isFaction(g, p, "firmament") && aiTune("viperplan", 1.f) > 0.f &&
            unitsOnBoard(g, p, UnitType::Mech) < int(aiTune("vipermechs", 0.f))) {
            bool near = false;
            for (int s2 : systemsWithin(g, sys, 2)) {
                for (int16_t x : g.systems[size_t(s2)].planets) {
                    const PlanetState& ps = g.planets[size_t(x)];
                    if (ps.owner >= 0 && ps.owner != p && !isHomePlanet(g, x) && ps.units[size_t(ps.owner)].groundForces() > 0 &&
                        !((ps.coexist >> p) & 1))
                        near = true;
                }
                if (near) break;
            }
            if (near) tryAdd(UnitType::Mech, 1);
        }
        // Fillipo Rois (Tnelis commander, 0.43.0 debug passes: unlocked in 3 games of 18): 6 destroyers on the board
        if (isFaction(g, p, "tnelis") && leaderStatus(g, p, kCommander) == kLocked && unitsOnBoard(g, p, UnitType::Destroyer) < 6)
            tryAdd(UnitType::Destroyer, 2);
        if (aiCoreWarSuns(g, p)) tryAdd(UnitType::WarSun, 1);  // Strategy Core (Muaat, the aggressive path)
        if (G.fleetShip > 0 && G.fleetTarget > 0) {
            int here = g.systems[sys].space[p].nonFighterShips();
            if (here + 4 >= G.fleetTarget)
                for (int k = here; k < G.fleetTarget; ++k)
                    if (!tryAdd(UnitType::Destroyer, 1) && !tryAdd(UnitType::Cruiser, 1)) break;
        }
    }
    if (aiLevelScale(g) > 0.f && aiTune("expand", 1.f) > 0.f) {
        // Expansion: while there are unclaimed planets around, make sure ground forces and
        // the carriers to move them are ready here.
        int open = 0;
        for (int s2 : systemsWithin(g, sys, 3))
            for (int16_t pl : g.systems[s2].planets)
                if (g.planets[pl].owner < 0 && !(g.systems[s2].custodians && !g.custodiansRemoved)) ++open;
        if (open > 0) {
            int gfHere = g.systems[sys].space[p].groundForces();
            for (int16_t pl : g.systems[sys].planets) gfHere += g.planets[pl].units[p].groundForces();
            if (drones) gfHere += g.systems[sys].space[p].count(UnitType::Fighter);
            int capHere = shipCapacity(g, p, sys) - g.systems[sys].space[p].count(UnitType::Fighter);
            // 0.42.0 (Ghoti): the flagship is the factory too and moves once a round - a carrier is a second arm
            if ((g.modules & kModDiscordant) && isFaction(g, p, "ghoti") && g.systems[sys].space[p].count(UnitType::Flagship))
                capHere -= unitDef(g, p, UnitType::Flagship).capacity;
            int want = std::min(open, int(aiTune("expandgf", 6.f)));
            if (capHere < want && unitsAvailable(g, p, UnitType::Carrier) > 0) tryAdd(UnitType::Carrier, 1);
            for (int k = 0; k < 3 && gfHere + plan[int(UnitType::Infantry)] + (drones ? plan[int(UnitType::Fighter)] : 0) < want + 1; ++k)
                if (!tryAdd(UnitType::Infantry, 2) && !tryAdd(UnitType::Infantry, 1) &&
                    !(drones && (tryAdd(UnitType::Fighter, 2) || tryAdd(UnitType::Fighter, 1))))
                    break;
        }
    }
    {
        // personality: favourite units first (Crystal Swarm, New Emperor, Maximum Impact, Overgrowth...)
        const Style& S = aiStyle(g, p);
        static const UnitType kOrder[] = {UnitType::WarSun, UnitType::Flagship, UnitType::Flagship2, UnitType::Dreadnought, UnitType::Cruiser,
                                          UnitType::Destroyer, UnitType::Mech, UnitType::Fighter, UnitType::Infantry};
        if (S.on)
            for (UnitType t : kOrder) {
                if (S.unit[int(t)] < 1.4f) continue;
                int n = (t == UnitType::Fighter || t == UnitType::Infantry) ? 2 : 1;
                if ((t == UnitType::WarSun || isFlagship(t)) && (budget < 8 || g.round < 2)) continue;
                tryAdd(t, n);
            }
    }
    // Plan Ahead (0.54.0, owner: "build ships for the goal"): at a dock within 3 moves of the operation's target, what
    // its strike still lacks - warships while the gathered fleet is short of a sure win, ground forces (and a carrier
    // for them) while the landing is short. After the expansion's needs (sims: before them it cost the bots tempo).
    if (aiLevelScale(g) > 0.f && aiTune("opbuild", 1.f) > 0.f && d.planet < 0) {
        const AiOp& op = aiOp(g, p);
        if (op.kind != kOpNone && op.dist.size() == g.systems.size() && op.dist[size_t(sys)] >= 0 && op.dist[size_t(sys)] <= 3 &&
            op.readiness < 0.95f) {
            Force all = op.tier[0];
            for (int tr = 1; tr < 3; ++tr)
                for (int t = 0; t < kUnitTypes; ++t)
                    if (op.tier[tr].n[t]) all.add(UnitType(t), op.tier[tr].n[t]);
            if (op.theirs.ships() > 0 && aiOpOdds(g, p, op, all) < aiTune("opbuildodds", 0.8f))
                for (int k = 0; k < 2; ++k)
                    if (!tryAdd(UnitType::Dreadnought, 1) && !tryAdd(UnitType::Cruiser, 1)) break;
            const int lack = op.gfNeed - all.groundForces();
            if (lack > 0 && (op.kind == kOpStyx || op.kind == kOpCustodians)) {
                int cap = shipCapacity(g, p, sys) - g.systems[size_t(sys)].space[size_t(p)].count(UnitType::Fighter) -
                          g.systems[size_t(sys)].space[size_t(p)].groundForces();
                if (cap < std::min(lack, 2) && unitsAvailable(g, p, UnitType::Carrier) > 0 && tryAdd(UnitType::Carrier, 1))
                    cap += unitDef(g, p, UnitType::Carrier).capacity;
                const int n = std::min(std::min(lack, 4), std::max(1, cap));
                if (!tryAdd(UnitType::Infantry, n)) tryAdd(UnitType::Infantry, 1);
            }
        }
    }
    if (infHere < 3 && !tryAdd(UnitType::Infantry, 2) && drones) tryAdd(UnitType::Fighter, 2);
    if (capacityTotal < 6) tryAdd(UnitType::Carrier, 1);
    if (budget >= 12 && unitsAvailable(g, p, UnitType::WarSun) > 0) tryAdd(UnitType::WarSun, 1);
    if (budget >= 10 && g.round >= 3 && unitsAvailable(g, p, UnitType::Flagship) > 0) tryAdd(UnitType::Flagship, 1);
    if (budget >= 10 && g.round >= 3 && unitsAvailable(g, p, UnitType::Flagship2) > 0) tryAdd(UnitType::Flagship2, 1);  // (0.52.0: the Lady)
    // 0.42.1 (#187: Arborec built 18 infantry at home in one go - its fleet pool was full - and lost
    // 19 to Plague): the filler stops adding infantry once 8 ground forces stand in this system
    int gfThere = g.systems[sys].space[p].groundForces();
    for (int16_t pl : g.systems[sys].planets) gfThere += g.planets[pl].units[p].groundForces();
    const int infCap = int(aiTune("infcap", 8.f));
    for (int guard = 0; guard < 12; ++guard) {
        bool added = false;
        const bool infRoom = infCap <= 0 || gfThere + plan[int(UnitType::Infantry)] < infCap;
        if (budget - costOf(plan) >= 4 && tryAdd(UnitType::Dreadnought, 1)) added = true;
        else if (tryAdd(UnitType::Cruiser, 1)) added = true;
        else if (tryAdd(UnitType::Destroyer, 1)) added = true;
        else if (tryAdd(UnitType::Fighter, 2)) added = true;
        else if (infHere < 99 && infRoom && tryAdd(UnitType::Infantry, 2)) added = true;
        else if (tryAdd(UnitType::Fighter, 1)) added = true;
        if (!added) break;
    }
    Answer a;
    a.data = plan;
    return a;
}

int aiPickSecretToKeep(const Game& g, const Decision& d) {
    static const struct {
        const char* key;
        int ease;
    } ease[] = {{"ctr", 8}, {"te", 8}, {"lsc", 7}, {"syc", 7}, {"hrm", 7}, {"csl", 6}, {"eh", 6}, {"sai", 6},
                {"faa", 5}, {"mrm", 5}, {"mp", 5}, {"btgk", 5}, {"fwm", 5}, {"eap", 5}, {"btv", 5}, {"dyp", 6},
                {"pe", 5}, {"fc", 4}, {"ose", 4}, {"mlp", 4}, {"otf", 3}, {"pem", 4}, {"sar", 4}, {"fwp", 4},
                {"mew", 3}, {"gamf", 3}, {"ans", 3}, {"dts", 3}, {"dtgs", 2}, {"ttfd", 2}, {"uf", 2}, {"bam", 1},
                {"mtm", 2}};
    int best = firstEnabled(d), bestScore = -1000;
    for (const Option& o : d.options) {
        int score = 3;
        for (const auto& e : ease)
            if (o.id >= 0 && o.id < kObjectiveCount && keyIs(kObjectives[o.id].key, e.key)) score = e.ease;
        if (o.id >= 0 && o.id < kObjectiveCount && aiLevelScale(g) > 0.f) {
            ObjProgress pr = objectiveProgress(g, d.player, o.id);
            if (pr.need > 0) score = 9 - 2 * pr.missing() + (pr.need <= 2 ? 1 : 0);  // how close we are
            if (objectiveSpend(o.id).any()) score = 2;
            const char* k = kObjectives[o.id].key;
            if (keyIs(k, "sb") || keyIs(k, "baf") || keyIs(k, "bam")) score = 0;  // need trades or losses
            // rarely reachable for the AI (measured in simulations)
            if (keyIs(k, "fwp")) score = std::min(score, 1);
            if (keyIs(k, "dfat") || keyIs(k, "mew") || keyIs(k, "ose")) score = std::min(score, 2);
            if (keyIs(k, "dts") || keyIs(k, "ttfd") || keyIs(k, "dtgs")) score = std::min(score, 2);  // (0.43.0: ~1 in 10 scored)
            if (keyIs(k, "fc") && pr.missing() > 1) score = 1;
        }
        if (o.id >= 0 && o.id < kObjectiveCount && meetsObjective(g, d.player, o.id)) score += 4;
        if (d.tag == Tag::DiscardSecret) score = -score;
        if (score > bestScore) {
            bestScore = score;
            best = o.id;
        }
    }
    return best;
}

Answer aiPick(const Game& g, const Decision& d, Rng& rng) {
    const int p = d.player;
    switch (d.tag) {
        case Tag::SetupTech: {
            std::vector<std::pair<float, int>> scored;
            for (const Option& o : d.options) scored.push_back({techWant(g, p, o.id) + rng.below(10) * 0.01f, o.id});
            // (0.59.0) the Crimson: a blue start (Dark Energy Tap) so Gravity Drive can follow at once
            if (isFaction(g, p, "crimson") && aiTune("crimsongd", 1.f) > 0.f)
                for (auto& sc : scored)
                    if (!std::strcmp(kTechs[sc.second].key, "det")) sc.first += 20.f;
            // Berserkers of Kjalengard (Discordant Stars): their free unit upgrade - fighters that die for Heroism
            // and fly with the Star Dragons, or dreadnoughts; not an early war sun they can't pay for
            if (isFaction(g, p, "kjalengard"))
                for (auto& sc : scored) {
                    const char* k = kTechs[sc.second].key;
                    sc.first = !std::strcmp(k, "ff2") ? 20.f : !std::strcmp(k, "dn2") ? 18.f : !std::strcmp(k, "dd2") ? 16.f : !std::strcmp(k, "inf2") ? 15.f
                               : !std::strcmp(k, "ws") ? 0.f : sc.first;
                }
            std::sort(scored.rbegin(), scored.rend());
            Answer a;
            for (int i = 0; i < d.minPick && i < int(scored.size()); ++i) a.picks.push_back(scored[i].second);
            if (a.picks.empty()) a.picks.push_back(scored[0].second);
            return a;
        }
        case Tag::SetupSecret:
        case Tag::DiscardSecret: return pickOne(aiPickSecretToKeep(g, d));
        case Tag::StrategyPick: {
            int best = firstEnabled(d);
            float bv = -1e9f;
            for (const Option& o : d.options) {
                float v = cardValue(g, p, o.id) + rng.below(100) * 0.02f;
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            return pickOne(best);
        }
        case Tag::ActionChoice: {
            Plan best = bestPlan(g, p);
            if (std::getenv("TI4_AIDEBUG")) {
                std::fprintf(stderr, "AI %s round %d: best plan system %d value %.2f moves %zu (tactic %d)\n",
                             playerName(g, p).c_str(), g.round, best.system, best.value, best.move.size() / 4,
                             g.players[p].tactic);
                for (size_t s = 0; s < g.systems.size(); ++s) {
                    if (!canActivate(g, p, int(s))) continue;
                    Plan pl = buildPlan(g, p, int(s), true);
                    if (pl.value > 0 || hexDistance(g.systems[s].pos, g.systems[g.players[p].homeSystem].pos) <= 1) {
                        std::fprintf(stderr, "    %-24s value %.2f moves %zu\n", kSystems[g.systems[s].def].name, pl.value,
                                     pl.move.size() / 4);
                    }
                }
            }
            // deals first: they don't use the action
            if (hasOption(d, 650) && aiWantsToNegotiate(g, p)) return pickOne(650);
            // E6-G0 Network (Uncharted Space relic): a free action card while there is room for it
            if (hasOption(d, 640) && int(g.players[p].actionCards.size()) < actionCardLimit(g, p)) return pickOne(640);
            // Local Contracts (Nokar, 0.43.0): free production when there is money for it
            if (hasOption(d, 642) && spendable(g, p, false) >= 4) return pickOne(642);
            if (hasOption(d, 643)) return pickOne(643);  // Erasure Corps (Lanefir breakthrough): a free exploration
            // Absolute Synergy (fixes 27: no action - option 645): from round 2, as the component action was valued (ai_te)
            if (hasOption(d, 645) && g.round >= 2) return pickOne(645);
            // Thunder's Edge space station: cash in commodities nobody will wash for us (a full pool, or after round 2
            // with no deal partner about)
            // (0.44.9: an Omni-Forgeworld takes damage for it - only when no real station is ready, the war sun is safe
            // from enemies and there are 3+ commodities)
            bool realStation = false;
            for (int x : readyPlanets(g, p))
                if (stationFor(g, p, x)) realStation = true;
            bool forgeOk = realStation;
            if (!realStation && forgeworldsOf(g, p, true) > 0 && g.players[p].commodities >= 3)
                for (size_t s = 0; s < g.systems.size() && !forgeOk; ++s) {
                    const Force& f = g.systems[s].space[size_t(p)];
                    if (f.count(UnitType::WarSun) > f.dmg[int(UnitType::WarSun)] && !enemyNear(g, p, int(s), 1)) forgeOk = true;
                }
            if (hasOption(d, 641) && forgeOk && (g.players[p].commodities >= commodityValue(g, p) || (g.round >= 3 && !aiWantsToNegotiate(g, p))))
                return pickOne(641);
            // free faction options first (Hacan agent): they don't use the turn
            for (const Option& o : d.options)
                if (o.id >= 700 && o.id < 1000 && aiFactionComponentValue(g, p, o.id) >= 0.5f) return pickOne(o.id);
            // Soul Cage (0.43.0): a bound hero costs the Myko-Mentori nothing but the cage - from round 5 the most useful
            // one goes as soon as its ability does something
            if (g.round >= 5) {
                int cage = -1;
                float cv = 0.45f;
                for (const Option& o : d.options)
                    if (o.id >= 6000 && o.label.compare(0, 10, "Soul Cage:") == 0) {
                        const float v = aiComponentValue(g, p, o.id);
                        if (v > cv) cv = v, cage = o.id;
                    }
                if (cage >= 0) return pickOne(cage);
            }
            // Extreme Duress (Thunder's Edge): anything but a strategic action costs our hand and our trade goods
            if (g.players[p].duress >= 0 && (g.players[p].tradeGoods > 0 || !g.players[p].actionCards.empty())) {
                int bestCard = -1;
                float bv = -1e9f;
                for (const Option& o : d.options)
                    if (o.id >= 10 && o.id < 20 && cardValue(g, p, o.id - 10) > bv) {
                        bv = cardValue(g, p, o.id - 10);
                        bestCard = o.id;
                    }
                if (bestCard >= 0) return pickOne(bestCard);
            }
            // (the Firmament bot review, owner 2026-11-11) the coup right after the home planets are spent, while a
            // tactic token is left for the Hollows The Blade's Orchestra readies
            if (hasOption(d, 6000 + int(Ab::Puppets)) && std::getenv("TI4_COUPDEBUG")) {
                float pv = 0.f;
                aiTeComponentValue(g, p, int(Ab::Puppets), pv);
                std::fprintf(stderr, "COUP r%d tactic %d strategy %d homeSpent %d value %.2f now %d best %.2f\n", g.round,
                             g.players[p].tactic, g.players[p].strategy, aiHomeSpentValue(g, p), pv, int(aiCoupNow(g, p)), best.value);
            }
            if (hasOption(d, 6000 + int(Ab::Puppets)) && aiCoupNow(g, p)) return pickOne(6000 + int(Ab::Puppets));
            // component actions (action cards, Sling Relay, faction abilities)
            int comp = -1;
            float compValue = 0;
            for (const Option& o : d.options)
                if (o.id >= 1000) {
                    float v = aiComponentValue(g, p, o.id);
                    if (v > compValue) {
                        compValue = v;
                        comp = o.id;
                    }
                }
            if (comp >= 0 && compValue >= 1.0f && (best.value < 2.0f || g.players[p].tactic == 0)) return pickOne(comp);
            // (the Letnev review, checkpoint 2 - test knob darkuse) Darktalon Treilla when the best plan without the fleet
            // limit puts more ships in a system than the limit allows and is worth darkuse more than the best plan now.
            // 90 games with darktok: 15 wins / 6.98 VP against 13 / 6.83, fresh seeds 12 / 6.72 against 12 / 6.74; used 34
            // times instead of 47, ships lost over the limit 60 -> 29 (fresh 51 -> 31). Owner 2026-11-12: ON (default 2).
            if (aiTune("darkuse", 2.f) > 0.f && hasOption(d, 6000 + int(Ab::LetnevHero)) && g.players[size_t(p)].tactic >= 1) {
                Game h = g;
                h.players[size_t(p)].heroRound = int16_t(g.round);
                const Plan free = bestPlan(h, p);
                int arriving = 0;
                for (size_t i = 0; i + 3 < free.move.size(); i += 4)
                    if (isShip(UnitType(free.move[i + 2])) && free.move[i + 2] != int(UnitType::Fighter)) arriving += free.move[i + 3];
                const int lim = g.players[size_t(p)].fleet + 2;  // (Armada)
                if (free.system >= 0 && g.systems[size_t(free.system)].space[size_t(p)].nonFighterShips() + arriving > lim &&
                    free.value - best.value >= aiTune("darkuse", 2.f))
                    return pickOne(6000 + int(Ab::LetnevHero));
            }
            // (the Sol review, checkpoint 2 - test knob jaceuse) Jace X when the best plan with every Sol command token off the
            // board is a system we can't activate now (our token is there) and is worth jaceuse more than the best plan now.
            // Tested (90 games each, OFF): jaceuse=2 31 wins / 8.00 VP against 27 / 7.84, but fresh seeds 27 / 7.88 against
            // 28 / 7.98 and without Thunder's Edge 24 / 7.90 against 25 / 8.01 - the first gain was luck
            if (aiTune("jaceuse", 0.f) > 0.f && hasOption(d, 6000 + int(Ab::SolHero)) && g.players[size_t(p)].tactic >= 1) {
                Game h = g;
                for (SystemState& sy : h.systems) sy.tokens &= uint16_t(~(1u << p));
                const Plan free = bestPlan(h, p);
                if (free.system >= 0 && g.systems[size_t(free.system)].hasToken(p) && free.value - best.value >= aiTune("jaceuse", 0.f))
                    return pickOne(6000 + int(Ab::SolHero));
            }
            // Discordant Stars (0.37.0): a hero worth more than most tactical actions goes first
            if (comp >= 6000 + int(Ab::DsZealous) && compValue >= 5.0f && compValue >= best.value * 0.6f) return pickOne(comp);
            // (the Vuil'raith review, checkpoint 2 - test knob carrion) It Feeds on Carrion on a big catch goes first too
            if (comp == 6000 + int(Ab::CabalHero) && aiTune("carrion", 0.f) > 0.f && compValue >= 2.f && compValue >= best.value * 0.6f)
                return pickOne(comp);
            // strategy card that's worth playing now?
            for (const Option& o : d.options)
                if (o.id >= 10 && o.id < 20 && cardGoodNow(g, p, o.id - 10, best.value)) return pickOne(o.id);
            // Lead From the Front / Galvanize: keep enough tokens for the status phase
            float tacticalBar = 1.5f, tacticalLow = 0.8f;
            if (aiCompetitive(g)) {
                tacticalBar = aiTune("cbar", 1.0f);
                tacticalLow = aiTune("clow", 0.35f);
            }
            if (aiLevelScale(g) > 0.f && aiEndgame(g, p).lastRound && aiTune("lastbar", 1.f) > 0.f) {
                // (0.55.0) the last round: every tactical action that does anything at all
                tacticalBar = std::min(tacticalBar, 0.3f);
                tacticalLow = std::min(tacticalLow, 0.05f);
            }
            if (aiLevelScale(g) > 0.f) {
                const Goals& G = aiGoals(g, p);
                const Player& me = g.players[p];
                if (G.keepTokens > 0 && me.tactic + me.strategy <= G.keepTokens) tacticalBar = tacticalLow = kVP * 1.2f;
            }
            if (hasOption(d, 0) && best.value >= tacticalBar) return pickOne(0);
            for (const Option& o : d.options)
                if (o.id >= 10 && o.id < 20) return pickOne(o.id);
            if (hasOption(d, 0) && best.value >= tacticalLow) return pickOne(0);
            if (comp >= 0 && compValue >= (aiLevelScale(g) > 0.f && aiEndgame(g, p).lastRound ? 0.05f : 0.3f)) return pickOne(comp);  // before passing
            if (hasOption(d, 600)) return pickOne(600);  // Psychoarchaeology: cash in specialty planets
            // Prove Endurance: be the last to pass - keep acting while others still play
            if (hasOption(d, 0) && hardAI(g) && best.value > 0.f) {
                bool pe = false, othersActive = false;
                for (int16_t o : g.players[p].secrets)
                    if (keyIs(kObjectives[o].key, "pe")) pe = true;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p && !g.players[q].passed && !g.players[q].eliminated) othersActive = true;
                if (pe && othersActive) return pickOne(0);
            }
            // Last Gecko Out (Ral Nel trait): one more small tactical action while others still play
            if (hasOption(d, 0) && best.value > 0.f && g.players[p].tactic >= 1 && fquirkIs(g, p, "ralnel", 0)) {
                bool othersActive = false;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p && !g.players[q].passed && !g.players[q].eliminated) othersActive = true;
                if (othersActive) return pickOne(0);
            }
            if (std::getenv("TI4_PASSDEBUG") && hasOption(d, 99))
                std::fprintf(stderr, "PASS round %d %s tactic %d best %.2f\n", g.round, playerName(g, p).c_str(), g.players[p].tactic,
                             best.value);
            if (hasOption(d, 98)) return pickOne(98);
            if (hasOption(d, 99)) return pickOne(99);
            return pickOne(firstEnabled(d));
        }
        case Tag::Activate: {
            Plan best = bestPlan(g, p);
            if (std::getenv("TI4_GROUNDDEBUG") && best.system >= 0) {  // test only: en-route pickups in the plans taken
                std::vector<int> shipFrom;
                for (size_t i = 0; i + 3 < best.move.size(); i += 4)
                    if (isShip(UnitType(best.move[i + 2])) && best.move[i + 1] < 0) shipFrom.push_back(best.move[i]);
                int enRoute = 0;
                for (size_t i = 0; i + 3 < best.move.size(); i += 4)
                    if (isGroundForce(UnitType(best.move[i + 2])) &&
                        std::find(shipFrom.begin(), shipFrom.end(), best.move[i]) == shipFrom.end())
                        enRoute += best.move[i + 3];
                if (enRoute) std::fprintf(stderr, "GROUND r%d %s en route %d into %s\n", g.round, playerName(g, p).c_str(), enRoute,
                                          kSystems[g.systems[best.system].def].name);
            }
            if (best.system >= 0 && hasOption(d, best.system)) return pickOne(best.system);
            if (hasOption(d, -2)) return pickOne(-2);
            // 0.43.0 (debug passes): a tactical action that must happen (Thunder's Edge Warfare, Z'eu) used to go to the
            // first system on the list - Mecatol Rex, where nothing could move (~1 in 4 of the Mecatol activations).
            // Now the best plan among the systems offered (Warfare also allows those with our token: home again for
            // its production), else one where we produce, else one our ships can reach.
            {
                int pick = -1;
                float bv = 0.f;
                for (const Option& o : d.options) {
                    if (!o.enabled || o.id < 0) continue;
                    const float v = buildPlan(g, p, o.id, false).value;
                    if (v > bv) bv = v, pick = o.id;
                }
                if (pick < 0)
                    for (const Option& o : d.options)
                        if (o.enabled && o.id >= 0 && hasProductionIn(g, p, o.id)) {
                            pick = o.id;
                            break;
                        }
                auto reachable = [&](int sys) {
                    for (size_t s2 = 0; s2 < g.systems.size(); ++s2) {
                        if (int(s2) == sys || g.systems[s2].hasToken(p)) continue;
                        const Force& f = g.systems[s2].space[p];
                        for (int t : kShipIdx)
                            if (f.n[t] && t != int(UnitType::Fighter) && shipReach(g, p, int(s2), sys, UnitType(t), moveBoost(g, p) ? 1 : 0).ok) return true;
                    }
                    return false;
                };
                if (pick < 0)
                    for (const Option& o : d.options)
                        if (o.enabled && o.id >= 0 && reachable(o.id)) {
                            pick = o.id;
                            break;
                        }
                if (pick >= 0) return pickOne(pick);
            }
            return pickOne(firstEnabled(d));
        }
        case Tag::Retreat: {
            int sys = g.activeSystem;
            int enemy = -1;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && hasShips(g, q, sys)) enemy = q;
            if (enemy < 0 && neutralShipsIn(g, sys)) enemy = kNeutral;  // Thunder's Edge
            if (enemy < 0) return pickOne(0);
            float odds = fightOdds(g, p, g.systems[sys].space[p], enemy, g.systems[sys].space[enemy], true);
            const Style& S = aiStyle(g, p);
            if (S.neverRetreat) return pickOne(0);  // Blood Rage
            return pickOne(odds < (S.on ? S.retreatOdds : 0.25f) ? 1 : 0);
        }
        case Tag::RetreatCargo: {  // (0.54.0) ground forces the winner would overrun go along; otherwise they hold the planet
            const int pl = d.value / 16, t = d.value % 16;
            if (pl < 0 || pl >= int(g.planets.size())) return pickOne(0);
            const int sys = g.planets[size_t(pl)].system;
            int theirs = 0;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p) theirs += g.systems[size_t(sys)].space[size_t(q)].groundForces();
            int ours = 0;
            for (UnitType u : {UnitType::Infantry, UnitType::Mech}) ours += g.planets[size_t(pl)].units[size_t(p)].count(u);
            const bool structures = g.planets[size_t(pl)].units[size_t(p)].structures() > 0;
            int most = 0;
            for (const Option& o : d.options) most = std::max(most, o.id);
            // (a planet with a PDS or dock keeps them: its cannon may still hold the invaders off)
            (void)t;
            if (theirs > 0 && theirs >= ours + (structures ? 1 : 0)) return pickOne(most);
            return pickOne(0);
        }
        case Tag::RetreatTo: {
            int best = firstEnabled(d);
            for (const Option& o : d.options)
                if (o.id >= 0 && g.systems[o.id].home == p) best = o.id;
            return pickOne(best);
        }
        case Tag::Bombard: {
            for (const Option& o : d.options)
                if (o.id >= 0) return pickOne(o.id);
            return pickOne(-1);
        }
        case Tag::SecondaryUse: {
            const Player& pl = g.players[p];
            int card = -1;
            for (int c = 0; c < kStrategyCards; ++c)
                if (d.prompt.find(strategyName(c)) != std::string::npos) card = c;
            bool yes = false;
            const bool free = d.prompt.find("Crisis") != std::string::npos;
            // Construction's token lands where we build and locks our ships there: only
            // outside home, or where we already have a token
            bool constructionSpot = false;
            for (int x : controlledPlanets(g, p)) {
                int sx = g.planets[x].system;
                if (g.systems[sx].hasToken(p) || (g.systems[sx].home != p && !hasShips(g, p, sx))) constructionSpot = true;
            }
            switch (card) {
                case 1: yes = pl.strategy >= 2 || free; break;
                case 2: yes = pl.strategy >= 2 || free; break;
                case 3: {
                    // the strategy token lands in that system and locks our ships there: never at
                    // home, unless we already have a token there (then nothing gets locked)
                    bool spot = false;
                    for (int x : controlledPlanets(g, p)) {
                        int sx = g.planets[x].system;
                        if (g.systems[sx].hasToken(p) || (g.systems[sx].home != p && !hasShips(g, p, sx))) spot = true;
                    }
                    yes = spot && pl.strategy >= 2;
                    break;
                }
                case 4: {  // replenish commodities: worth it when a neighbour can trade for them
                    yes = false;
                    const int missing = commodityValue(g, pl) - pl.commodities;
                    bool partner = false;
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p && !g.players[q].eliminated && areNeighbours(g, p, q) && !hostileFlag(g, p, q)) partner = true;
                    if (aiLevelScale(g) > 0.f && partner && missing >= 2) yes = pl.strategy >= 2 || (pl.strategy >= 1 && missing >= 3);
                    break;
                }
                case 5:
                    yes = pl.strategy >= 2 && spendable(g, p, false) >= 4;
                    if (isFaction(g, p, "kortali") && pl.strategy >= 1) yes = true;  // Zealous (Discordant Stars): it is the primary for them
                    break;
                case 6: yes = spendable(g, p, false) - objectiveReserve(g, p).resources >= 5; break;
                case 7: yes = (pl.strategy >= 2 || free) && pl.secrets.size() < 3; break;
            }
            if (hardAI(g) && !free && aiTune("secondary", 1.f) > 0.f) {
                const Goals& G = aiGoals(g, p);
                const bool spare = pl.strategy >= 1 && pl.tactic + pl.strategy - 1 >= G.keepTokens;
                switch (card) {
                    case 3: yes = constructionSpot && (yes || (spare && G.structure + G.structOutside + G.pds + G.dock >= kVP * 0.5f)); break;
                    case 6: {  // research when a technology our objectives want is in reach
                        float want = G.upgrade + G.factionTech;
                        for (float c : G.color) want = std::max(want, c);
                        yes = spare && aiFreeSpend(g, p, false) >= 4 && (yes || want >= kVP * 0.5f);
                        break;
                    }
                    case 7: {  // another secret objective (or a better one in place of the hardest)
                        int scoredSecrets = 0;
                        for (int16_t x : pl.scored)
                            if (kObjectives[x].kind == 3) ++scoredSecrets;
                        yes = spare && scoredSecrets < 3 && (int(pl.secrets.size()) + scoredSecrets < 3 || !pl.secrets.empty());
                        break;
                    }
                    default: yes = yes && spare; break;
                }
            }
            if (aiCompetitive(g) && card == 7 && pl.strategy >= 1 && pl.secrets.size() < 3) yes = true;  // secrets are victory points
            // (0.59.0, owner: the Crimson don't expand) their home is beside the board: Gravity Drive and a space dock
            // out in their slice come first in rounds 1-3
            if (!free && !yes && isFaction(g, p, "crimson") && g.round <= 3 && pl.strategy >= 1 && aiLevelScale(g) > 0.f &&
                aiTune("crimsonearly", 1.f) > 0.f) {
                if (card == 6 && !ownsTechKey(g, p, "gd") && canResearchNow(g, p, findTech("gd")) && spendable(g, p, false) >= 4) yes = true;
                if (card == 3 && constructionSpot && crimsonNeedsDock(g, p)) yes = true;
            }
            // (0.59.1) the Creuss: a space dock out on the board in rounds 1-3 (Construction's secondary)
            if (!free && !yes && isFaction(g, p, "ghost") && g.round <= 3 && pl.strategy >= 1 && aiLevelScale(g) > 0.f && card == 3 &&
                constructionSpot && crimsonNeedsDock(g, p))
                yes = true;
            if (free && card != 3 && card != 6) yes = true;
            // 0.44.4: breakthroughs that make a secondary cheap - Mindsieve (Naalu: a spare promissory note instead of the
            // token), Specialist Compounds (Jol-Nar: a technology-specialty planet instead of the 4 resources)
            if (!yes && !free && breakthroughIs(g, p, "naalu")) {
                bool cheap = false;
                for (int16_t n : pl.notes) {
                    const int t = noteType(n);
                    if (t != kNoteSupportThrone && t != kNoteAlliance && noteDefaultValue(g, n) <= 1.5f) cheap = true;
                }
                if (cheap) {
                    const int missing = commodityValue(g, pl) - pl.commodities;
                    switch (card) {
                        case 1: case 2: yes = true; break;
                        case 3: yes = constructionSpot; break;
                        case 4: yes = missing >= 2; break;
                        case 5: yes = spendable(g, p, false) >= 4; break;
                        case 6: yes = aiFreeSpend(g, p, false) >= 4; break;
                        case 7: yes = pl.secrets.size() < 3; break;
                        default: break;
                    }
                }
            }
            if (!yes && !free && card == 6 && pl.strategy >= 1 && breakthroughIs(g, p, "jolnar")) {
                for (int x : readyPlanets(g, p))
                    if (planetSkips(g, x)) yes = true;
            }
            // Production Biomes (0.44.6): its last strategy token is worth 4 trade goods - not for a lesser secondary
            if (yes && !free && pl.strategy <= 1 && card != 6 && card != 7 && ownsTechKey(g, p, "pm") &&
                !pl.techExhausted(findTech("pm")))
                yes = false;
            return pickOne(yes ? 1 : 0);
        }
        case Tag::Leadership: {
            int inf = aiLevelScale(g) > 0.f ? aiFreeSpend(g, p, true) : spendable(g, p, true) - objectiveReserve(g, p).influence;
            // The groundwork (0.25.0, plastic): in the opening, while the home dock still has to
            // produce this round, buy tokens only with influence that costs no production -
            // planets with little in resources, spare trade goods. Self-play says no: a command
            // token is worth more than the units those resources would buy (off by default).
            {
                const Player& me = g.players[p];
                const int home = me.homeSystem;
                if (aiGroundScale(g, p) >= 0.5f && aiGroundOpening(g) > 0.f && aiTune("g_plastic", 0.f) > 0.f && home >= 0 &&
                    canActivate(g, p, home) && productionCapacity(g, p, home) > 0) {
                    int cheap = std::max(0, me.tradeGoods - aiGoals(g, p).keepTg) * std::max(1, tgWorth(g, p));
                    for (int x : readyPlanets(g, p))
                        if (planetResources(g, x) <= 1) cheap += planetInfluence(g, x);
                    inf = std::min(inf, cheap);
                }
            }
            int k = inf >= 9 ? 2 : inf >= 5 ? 1 : 0;
            if (aiGroundScale(g, p) > 0.f && aiTune("g_leadkeep", -1.f) >= 0.f)  // test only: influence kept back, the rest buys tokens
                k = std::max(0, (inf - int(aiTune("g_leadkeep", -1.f))) / 3);
            if (aiLevelScale(g) > 0.f) {
                const Goals& G = aiGoals(g, p);
                const Player& me = g.players[p];
                if (G.keepTokens > me.tactic + me.strategy && inf >= 3) k = std::max(k, std::min(3, inf / 3));
            }
            {   // 0.30.0 (owner: Yssaril sat on 18 trade goods with 2 tactic tokens): a pile of trade goods
                // the bot isn't saving for anything is turned into command tokens - turns it can actually take
                const Player& me = g.players[p];
                const int spare = me.tradeGoods - aiGoals(g, p).keepTg - 3;
                if (spare >= 3 && me.tactic + me.fleet + me.strategy < 16)
                    k = std::max(k, std::min({inf / 3, spare / 3 + (me.tactic <= 2 ? 1 : 0), 4}));
            }
            while (k > 0 && !hasOption(d, k)) --k;
            return pickOne(k);
        }
        case Tag::DiplomacySystem: {
            // 0.43.0 (owner: "they only use Diplomacy on their home"): the system whose planets are worth the most
            // AND are most at risk - enemy warships (and carriers with ground forces) that can reach it this round,
            // less what already guards it. Nothing threatened anywhere: home, as before.
            int best = firstEnabled(d);
            for (const Option& o : d.options)
                if (g.systems[o.id].home == p) best = o.id;
            float bestV = 0.f;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0 || o.id >= int(g.systems.size())) continue;
                const SystemState& sy = g.systems[size_t(o.id)];
                float worth = 0.f;
                for (int16_t x : sy.planets)
                    if (g.planets[size_t(x)].owner == p)
                        worth += float(planetResources(g, x) + planetInfluence(g, x)) + (kPlanets[g.planets[size_t(x)].def].legendary ? 3.f : 0.f) +
                                 (sy.home == p ? 4.f : 0.f) + float(g.planets[size_t(x)].units[size_t(p)].structures()) * 1.5f;
                float danger = 0.f;
                const std::vector<int> near1 = systemsWithin(g, o.id, 1), near2 = systemsWithin(g, o.id, 2);
                for (int z : near2) {
                    const bool close = std::find(near1.begin(), near1.end(), z) != near1.end();
                    for (int q = 0; q < g.numPlayers; ++q) {
                        if (q == p) continue;
                        const Force& f = g.systems[size_t(z)].space[size_t(q)];
                        const float hostile = hostileFlag(g, p, q) ? 1.5f : 1.f;
                        const float ships = float(f.nonFighterShips()) + 0.5f * float(f.groundForces());
                        danger += ships * hostile * (close ? 1.f : 0.45f);
                    }
                }
                float guard = float(sy.space[size_t(p)].nonFighterShips());
                for (int16_t x : sy.planets) guard += 0.5f * float(g.planets[size_t(x)].units[size_t(p)].groundForces());
                const float v = worth * danger / (1.f + 0.6f * guard);
                if (v > bestV + 0.5f) {
                    bestV = v;
                    best = o.id;
                }
            }
            return pickOne(best);
        }
        case Tag::ReadyPlanets: {
            std::vector<std::pair<int, int>> v;
            for (const Option& o : d.options) v.push_back({planetResources(g, o.id) + planetInfluence(g, o.id), o.id});
            std::sort(v.rbegin(), v.rend());
            Answer a;
            for (int i = 0; i < d.maxPick && i < int(v.size()); ++i) a.picks.push_back(v[i].second);
            return a;
        }
        case Tag::PoliticsSpeaker: return pickOne(hasOption(d, p) ? p : firstEnabled(d));
        case Tag::OceanPay: {  // ocean cards first: 1 each, never wasted
            int best = -1;
            for (const Option& o : d.options) best = std::max(best, o.id);
            return pickOne(best);
        }
        case Tag::EngageCoexisting: {  // fight coexisting units only when clearly stronger
            int x = -1;
            const int q = d.targetPlayer;
            if (g.activeSystem >= 0)
                for (int16_t y : g.systems[size_t(g.activeSystem)].planets)
                    if (q >= 0 && groundForcesOn(g, q, y) > 0 && groundForcesOn(g, p, y) > 0) x = y;
            const bool strong = x >= 0 && groundForcesOn(g, p, x) >= 2 * groundForcesOn(g, q, x) + 1;
            return pickOne(strong ? 1 : 0);
        }
        case Tag::ConstructionChoice: {  // Thunder's Edge Construction: produce when there's money to build with
            const bool structures = unitsAvailable(g, p, UnitType::PDS) + unitsAvailable(g, p, UnitType::SpaceDock) > 1;
            const bool money = (aiLevelScale(g) > 0.f ? aiFreeSpend(g, p, false) : spendable(g, p, false)) >= 5;
            return pickOne(hasOption(d, 2) && (money || !structures) && g.round >= 2 ? 2 : 1);
        }
        case Tag::ConstructionDock: {  // the dock that builds most, nearer the enemy on ties
            int best = firstEnabled(d);
            float bv = -1e9f;
            for (const Option& o : d.options) {
                const int s = o.id >= 100000 ? o.id - 100000 : g.planets[size_t(o.id)].system;
                float v = float(productionCapacity(g, p, s));
                if (enemyNear(g, p, s, 2)) v += 1.5f;
                if (v > bv) bv = v, best = o.id;
            }
            return pickOne(best);
        }
        case Tag::ConstructionWhere: {
            // prefer a space dock on a good non-home planet, else a PDS on the most valuable planet
            int best = -1;
            float bv = -1;
            for (const Option& o : d.options) {
                if (o.id == -1) continue;
                float v;
                if (o.id <= -10) {
                    // Saar floating factory (owner, 0.27.0: Saar never placed a second one - a PDS
                    // always outscored the flat 4). A factory moves with the fleet and builds where
                    // the planets and the fighting are ("Saarball"): worth more than a PDS, best out
                    // of home, never a second one where one already is.
                    const int s = -o.id - 10;
                    const bool home = g.systems[s].home == p;
                    const int here = g.systems[s].space[p].count(UnitType::SpaceDock);
                    v = 6.f;
                    if (home) v += here ? -4.5f : 0.5f;
                    else v += 1.5f - 3.5f * float(here);
                    for (int16_t a : g.systems[s].adj)
                        if (a == g.mecatol()) v += 1.f;
                    if (enemyNear(g, p, s, 1) && g.systems[s].space[p].nonFighterShips() == 0) v -= 2.5f;  // blockaded = destroyed
                    if (aiLevelScale(g) > 0.f) {
                        const Goals& G = aiGoals(g, p);
                        v += G.structure + G.dock;
                    }
                } else {
                    int planet = o.id / 2;
                    bool dock = o.id % 2;
                    bool home = g.systems[g.planets[planet].system].home == p;
                    v = dock ? (home ? 1.f : 3.f + planetResources(g, planet)) : 1.f + planetValue(g, p, planet) * 0.2f;
                    // (0.59.0) the Crimson's first dock out on the board: their production 2 moves nearer
                    if (dock && !home && crimsonNeedsDock(g, p) && aiTune("crimsondock", 1.f) > 0.f) v += 5.f;
                    if (!dock) v += aiStyle(g, p).pdsPref * 2.f;  // Turtler, Shell Defense, Iron Shepherd
                    // (the Arborec review, checkpoint 2 - test knob bestfriend) the Arborec's Best Friend trait says it
                    // fortifies its legendary planets and Mecatol Rex, but its PDS liking is general (and toned down on
                    // Competitive): sims put fewer PDS on Mecatol Rex with it (15) than without (21). Tested OFF: PDS on
                    // legendary planets 17 -> 34 in 90 games with Best Friend, Mecatol Rex 15 -> 16, but 7 wins / 6.58 VP
                    // against 8 / 6.64
                    if (!dock && aiTune("bestfriend", 0.f) > 0.f && fquirkIs(g, p, "arborec", 1) &&
                        (isLegendaryPlanet(g, planet) || g.planets[size_t(planet)].system == g.mecatol()))
                        v += 3.f;
                    if (enemyNear(g, p, g.planets[planet].system, 1)) v += dock ? -2.f : 1.5f;
                    // 0.43.0 (owner D128): the Mirveda's PDS are Gauss Cannons that travel with the fleets - put them
                    // where a fleet is, out on the front; never alone in space next to the enemy (blockaded = lost)
                    if (!dock && unitDef(g, p, UnitType::PDS).spaceOnly) {
                        const int sy = g.planets[planet].system;
                        const int fleet = g.systems[size_t(sy)].space[size_t(p)].nonFighterShips();
                        v += 1.5f + 0.6f * float(std::min(fleet, 4)) + (home ? -1.f : 1.f);
                        if (fleet == 0 && enemyNear(g, p, sy, 1)) v -= 3.f;
                    }
                    if (aiLevelScale(g) > 0.f) {
                        const Goals& G = aiGoals(g, p);
                        v += G.structure + (dock ? G.dock : G.pds);
                        // a planet outside home without structures yet counts for Improve Infrastructure
                        if (!home && g.planets[planet].units[p].structures() == 0) v += G.structOutside;
                    }
                }
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            if (best == -1) best = hasOption(d, -1) ? -1 : firstEnabled(d);
            return pickOne(best);
        }
        case Tag::ConstructionSystem: {
            int best = firstEnabled(d);
            float bv = -1e9f;
            for (const Option& o : d.options) {
                float v = 0;
                for (int16_t pl : g.systems[o.id].planets)
                    if (g.planets[pl].owner == p) v += planetResources(g, pl);
                if (!g.systems[o.id].hasToken(p)) {
                    if (g.systems[o.id].home == p) v -= 20;
                    if (hasShips(g, p, o.id)) v -= 10;
                } else {
                    v += 2;
                }
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            return pickOne(best);
        }
        case Tag::TradeReplenish:  // free refills: friends (personality and relationship)
        case Tag::TradeX1Offer: {  // X-1 trades: most bots, with players they don't hold a grudge against
            if (d.tag == Tag::TradeX1Offer && (g.tradeRules & 8)) {
                Answer a;
                a.picks = aiTradeX1Picks(g, p, d);
                return a;
            }
            Answer a;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0 || o.id >= g.numPlayers) continue;
                if (d.tag == Tag::TradeX1Offer ? aiTradeX1Offer(g, p, o.id) : aiTradeGift(g, p, o.id)) a.picks.push_back(o.id);
            }
            return a;
        }
        case Tag::TradeX1Accept:
            if (g.tradeRules & 8) return pickOne(aiTradeX1Response(g, p, d.targetPlayer, d.value));
            return pickOne(aiTradeX1Accept(g, p, d.targetPlayer) ? 1 : 0);
        case Tag::TradeX1Settle: return pickOne(aiTradeX1Settle(g, p, d.targetPlayer) ? 1 : 0);
        case Tag::WarfareToken: {
            // the system to activate again: the biggest fleet, or a big dock to produce twice
            int best = firstEnabled(d), bv = -1;
            for (const Option& o : d.options) {
                int v = g.systems[o.id].space[p].nonFighterShips() * 2 + (productionCapacity(g, p, o.id) >= 4 ? 3 : 0);
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            return pickOne(best);
        }
        case Tag::ScorePublic:
        case Tag::ImperialScore: {
            int best = -1, bv = 0;
            for (const Option& o : d.options) {
                if (o.id < 0) continue;
                int v = kObjectives[o.id].points * 10 - (objectiveSpend(o.id).any() ? 3 : 0);
                if (aiLevelScale(g) > 0.f) {
                    // score first what could slip away (ships, planets); technologies stay
                    const char* k = kObjectives[o.id].key;
                    for (const char* x : {"intimidate", "make_history", "become_legend", "deep_space", "vast_territories", "outer_rim",
                                          "control_borderlands", "raise_fleet", "command_armada", "supremacy", "push_boundaries"})
                        if (keyIs(k, x)) v += 4;
                    for (const char* x : {"corner", "unify_colonies", "expand_borders", "subdue", "research_outposts", "brain_trust",
                                          "lost_outposts", "ancient_monuments", "conquer", "distant_lands"})
                        if (keyIs(k, x)) v += 2;
                    for (const char* x : {"develop", "revolutionize", "diversify", "master_science"})
                        if (keyIs(k, x)) v -= 2;
                }
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            return pickOne(best);
        }
        case Tag::ScoreSecret: {
            for (const Option& o : d.options)
                if (o.id >= 0) return pickOne(o.id);
            return pickOne(-1);
        }
        case Tag::Research: {
            int best = -1;
            float bv = -1e9f;
            for (const Option& o : d.options) {
                if (o.id < 0) continue;
                float v = techWant(g, p, o.id) + rng.below(10) * 0.05f;
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            if (best < 0) best = hasOption(d, -1) ? -1 : firstEnabled(d);
            return pickOne(best);
        }
        case Tag::SkipPrereq: {
            Answer a;
            for (const Option& o : d.options)
                if (int(a.picks.size()) < d.minPick) a.picks.push_back(o.id);
            return a;
        }
        case Tag::ExtraResearch:
            if (aiLevelScale(g) > 0.f) return pickOne(aiFreeSpend(g, p, false) >= 6 ? 1 : 0);
            return pickOne(spendable(g, p, false) - objectiveReserve(g, p).resources >= 7 ? 1 : 0);
        case Tag::Custodians: return pickOne(1);
        case Tag::ActivateConfirm: return pickOne(1);
        case Tag::TokenSpend: return pickOne(firstEnabled(d));
        case Tag::CannonTarget: {
            int best = firstEnabled(d), bv = -1;
            for (const Option& o : d.options) {
                int v = g.systems[g.activeSystem >= 0 ? g.activeSystem : 0].space[o.id].nonFighterShips();
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            return pickOne(best);
        }
        case Tag::TokenFromPool: {
            const Player& pl = g.players[p];
            if (hasOption(d, 2) && pl.strategy > 1) return pickOne(2);
            if (hasOption(d, 0)) return pickOne(0);
            if (hasOption(d, 2)) return pickOne(2);
            return pickOne(firstEnabled(d));
        }
        case Tag::SpaceCannonFire: return pickOne(1);
        case Tag::UseTech: return pickOne(1);
        case Tag::WarfareDock: return pickOne(firstEnabled(d));
        case Tag::AssignHits: {
            // Assault Cannon (0.44.8: bots threw away the first type listed - carriers, cargo and all): the cheapest
            // loss; a carrier counts the cargo its capacity was holding
            if (d.prompt.find("Assault Cannon") == std::string::npos) break;
            const int sys = g.activeSystem;
            int best = -1;
            float bv = 1e9f;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0 || o.id >= kUnitTypes) continue;
                const UnitType t = UnitType(o.id);
                float v = float(unitCost2(g, p, t));
                if (sys >= 0) {
                    const Force& f = g.systems[size_t(sys)].space[size_t(p)];
                    const int capOne = unitDef(g, p, t).capacity;
                    if (capOne > 0) {
                        const int spare = shipCapacity(g, p, sys) - transportedInSpace(g, p, sys);
                        v += 1.5f * float(std::max(0, capOne - spare));  // cargo lost with it
                    }
                    if (unitSustains(g, p, t) && f.dmg[o.id] < f.n[o.id]) v += 0.5f;  // an undamaged sustainer is worth more
                }
                if (v < bv) bv = v, best = o.id;
            }
            return pickOne(best >= 0 ? best : firstEnabled(d));
        }
        default: break;
    }
    if (d.prompt.compare(0, 19, "Reverie Implants: s") == 0) {  // a blind bid (0.44.4: the sure answers first)
        const int other = d.targetPlayer;
        const int otherTg = other >= 0 && other < g.numPlayers ? g.players[size_t(other)].tradeGoods : -1;
        auto hasOpt = [&](int id) {
            for (const Option& o : d.options)
                if (o.id == id) return true;
            return false;
        };
        if (d.value == 1 && otherTg == 0 && hasOpt(1)) return pickOne(1);  // the Florzen: they can only spend 0
        if (d.value == 0 && otherTg == 0) return pickOne(0);                // their side: the Florzen can only spend 0
        return pickOne(d.options[size_t(rng.below(int(d.options.size())))].id);
    }
    if (d.prompt.compare(0, 13, "Brikk Strain:") == 0) {  // 0.43.0: the clause worth most (Imperial: a point if we can score)
        if (d.prompt.find("Imperial") != std::string::npos) {
            bool can = false;
            for (int16_t o : g.publicObjectives) can = can || (!hasScored(g, p, o) && meetsObjective(g, p, o));
            return pickOne(can ? 0 : 1);
        }
        return pickOne(0);
    }
    if (isYesNo(d)) return pickOne(1);
    if (d.maxPick > 1 || d.minPick != 1) {
        Answer a;
        for (const Option& o : d.options)
            if (o.enabled && int(a.picks.size()) < d.minPick) a.picks.push_back(o.id);
        return a;
    }
    return pickOne(firstEnabled(d));
}

}  // namespace

float aiTechWant(const Game& g, int p, int tech) { return techWant(g, p, tech); }
float aiPlanetValue(const Game& g, int p, int pl) { return planetValue(g, p, pl); }
float aiStrategyCardValue(const Game& g, int p, int card) { return cardValue(g, p, card); }
bool isYesNoOptions(const Decision& d) { return isYesNo(d); }

Answer aiAnswer(const Game& g, const Decision& d, Rng& rng) {
    g_aiSeat = d.player;
    Answer cards;
    if (aiGalaxyAnswer(g, d, rng, cards)) return cards;  // galaxy setup
    if (aiDealAnswer(g, d, rng, cards)) return cards;  // negotiations, promised votes
    if (aiFactionAnswer(g, d, rng, cards)) return cards;
    if (aiExploreAnswer(g, d, rng, cards)) return cards;
    if (aiCardsAnswer(g, d, rng, cards)) return cards;
    switch (d.kind) {
        case DecisionKind::Pick: return aiPick(g, d, rng);
        case DecisionKind::Pay: return aiPayment(g, d);
        case DecisionKind::Move: return aiMove(g, d);
        case DecisionKind::Produce: return aiProduce(g, d);
        case DecisionKind::Hits: return aiHits(g, d);
        case DecisionKind::Land: return aiLand(g, d);
        case DecisionKind::Tokens: return aiTokens(g, d);
        case DecisionKind::Deal: return Answer();  // handled by aiDealAnswer
    }
    return Answer();
}

// 0.31.0: Z'eu Omega (Codex) - how much a free tactical action is worth now (0 = no good target outside the homes)
float aiFreeTacticalValue(const Game& g, int p) {
    const Plan pl = bestPlan(g, p);
    if (pl.system < 0 || g.systems[pl.system].home >= 0 || pl.move.empty()) return 0.f;
    return pl.value > 1.5f ? 1.3f : pl.value > 0.6f ? 0.7f : 0.f;
}


int aiSecretToReturn(const Game& g, const Decision& d) {  // (0.33.0: AI Augury) the secret worth least to us
    Decision dd = d;
    dd.tag = Tag::DiscardSecret;
    return aiPickSecretToKeep(g, dd);
}

// (0.55.0, never used before) the Dominus Orb: what this movement gains when units may leave systems with our
// command tokens - the plan for the active system with and without it (the flag set for a moment)
float aiDominusGain(const Game& g, int p, int sys) {
    if (sys < 0 || sys >= int(g.systems.size())) return 0.f;
    const float without = buildPlan(g, p, sys, false).value;
    Game& mg = const_cast<Game&>(g);
    const bool was = mg.fx.dominusOrb;
    mg.fx.dominusOrb = true;
    const float with = buildPlan(g, p, sys, false).value;
    mg.fx.dominusOrb = was;
    return with - without;
}

}  // namespace ti
