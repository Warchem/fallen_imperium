// Computer players and faction abilities, leaders and faction notes (Stage 5).
// Simple rules of thumb, like the rest of the AI; every answer still goes
// through the engine's validators.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "ai/ai.h"
#include "ai/ai_internal.h"
#include "engine/deal.h"
#include "engine/map.h"
#include "engine/personality.h"
#include "engine/query.h"

namespace ti {

namespace {
Answer one(int id) {
    Answer a;
    a.picks.push_back(id);
    return a;
}
bool has(const Decision& d, int id) {
    for (const Option& o : d.options)
        if (o.id == id && o.enabled) return true;
    return false;
}
int firstReal(const Decision& d) {
    for (const Option& o : d.options)
        if (o.enabled && o.id >= 0) return o.id;
    for (const Option& o : d.options)
        if (o.enabled) return o.id;
    return d.options.empty() ? 0 : d.options[0].id;
}
bool yesNo(const Decision& d) { return d.options.size() == 2 && has(d, 1) && has(d, 0) && d.options[0].label == "Yes"; }
Answer yes(bool b) { return one(b ? 1 : 0); }

// Best option by a score; options with id < 0 ("no", "cancel") are skipped.
template <class F>
int bestBy(const Decision& d, F score, float minScore = -1e9f) {
    int best = -1;
    float bv = minScore;
    for (const Option& o : d.options) {
        if (!o.enabled || o.id < 0) continue;
        float v = score(o.id);
        if (v > bv) {
            bv = v;
            best = o.id;
        }
    }
    return best;
}

float planetWorth(const Game& g, int x) { return float(planetResources(g, x) + planetInfluence(g, x)); }

// Planets near enemy ships are where extra infantry matter most.
float defendScore(const Game& g, int p, int x) {
    const PlanetState& ps = g.planets[x];
    float v = planetWorth(g, x) * 0.3f - float(ps.units[p].groundForces()) * 0.5f;
    for (int16_t a : g.systems[ps.system].adj)
        if (otherShipsIn(g, p, a)) v += 2.f;
    if (g.systems[ps.system].home == p) v += 0.5f;
    return v;
}

int ownOption(const Game& g, const Decision& d, int p) {
    // options labelled "<player>'s <unit>": pick the strongest of our own
    std::string mine = playerName(g, p) + "'s ";
    int best = -1, bestCombat = 99;
    for (const Option& o : d.options) {
        if (o.id < 0 || o.label.compare(0, mine.size(), mine) != 0) continue;
        int t = o.id % 16;
        int c = unitDef(g, p, UnitType(t)).combat;
        if (c > 0 && c < bestCombat) {
            bestCombat = c;
            best = o.id;
        }
    }
    return best;
}

int numberBefore(const std::string& s, const std::string& marker) {
    size_t at = s.find(marker);
    if (at == std::string::npos || at == 0) return 0;
    size_t b = at;
    while (b > 0 && s[b - 1] >= '0' && s[b - 1] <= '9') --b;
    return std::atoi(s.substr(b, at - b).c_str());
}

// Value of keeping an owned technology (for Rin, the Jol-Nar hero) - 0.44.6: on the same scale as the replacements
// (its own research want; it used to be a flat 0.5 / 1.5, so the bot swapped X-89 and Hyper Metabolism back and
// forth), a little more for what we know works
float keepValue(const Game& g, int p, int t) { return aiTechWant(g, p, t) + 0.5f; }

// Combat odds in the active system: is the opponent clearly stronger in space?
bool outgunned(const Game& g, int p) {
    int sys = g.activeSystem;
    if (sys < 0) return false;
    int mine = combatStrength(g, p, sys, true), theirs = 0;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p) theirs = std::max(theirs, combatStrength(g, q, sys, true));
    return theirs * 10 >= mine * 6;
}

int enemyNonFighters(const Game& g, int p, int sys) {
    if (sys < 0) return 0;
    int n = 0;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p) n += g.systems[sys].space[q].nonFighterShips();
    return n;
}

float targetValue(const Game& g, int p, int sys);
// Wormhole Generator / Creuss Iff (0.44.7): how good a system is for Creuss wormhole token k - a new link near targets
// and near our fleet; never a second token in one system, a wormhole of that kind already there, or a supernova
// (half the placements used to be stacked or in supernovas)
float creussSpot(const Game& g, int p, int sys, int k) {
    if (sys < 0 || sys >= int(g.systems.size()) || k < 0 || k > 2) return -9.f;
    const uint8_t bit = k == 0 ? kAlpha : k == 1 ? kBeta : kGamma;
    for (int i = 0; i < 3; ++i)
        if (i != k && g.creussTokens[i] == sys) return -6.f;
    if (g.systems[size_t(sys)].d().wormholes & bit) return -5.f;
    if (g.systems[size_t(sys)].d().anomalies & kSupernova) return -5.f;
    float v = targetValue(g, p, sys);
    if (sys == g.nexus && !g.systems[size_t(sys)].nexusActive) v -= 1.f;  // (0.44.9: it comes back when the nexus opens)
    bool fleet = hasShips(g, p, sys);
    for (int16_t a : g.systems[size_t(sys)].adj) fleet = fleet || hasShips(g, p, a);
    if (fleet) v += 1.f;
    if (g.systems[size_t(sys)].home == p) v -= 1.f;
    // Wormhole Weavers (Creuss faction trait, 0.59.0): a door behind another player's lines - their planets in or next
    // to it with none of their ships around (their home half again)
    if (fquirkIs(g, p, "ghost", 0)) {
        float back = 0.f;
        std::vector<int> near = {sys};
        for (int16_t a : g.systems[size_t(sys)].adj) near.push_back(a);
        for (int x : near)
            for (int16_t pl : g.systems[size_t(x)].planets) {
                const int o = g.planets[size_t(pl)].owner;
                if (o < 0 || o == p || o >= g.numPlayers) continue;
                bool guarded = hasShips(g, o, x);
                for (int16_t a : g.systems[size_t(x)].adj) guarded = guarded || hasShips(g, o, a);
                if (!guarded) back += aiTune("weavespot", 0.6f) * (g.systems[size_t(x)].home == o ? 1.5f : 1.f);
            }
        v += std::min(4.f, back);
    }
    return v;
}

// Icarus Drive (fixes 24): what Creuss token k in system s adds - a new link near targets (creussSpot, minus what it
// gives where it is now), and in our own activation, when s is the active system, the ships that can come in through it
float icarusGain(const Game& g, int p, int s, int k) {
    const float now = g.creussTokens[k] >= 0 ? creussSpot(g, p, g.creussTokens[k], k) : -1.f;
    float v = creussSpot(g, p, s, k) - now;
    if (p != g.activePlayer || s != g.activeSystem || g.creussTokens[k] == s) return v;
    Game h = g;
    h.creussTokens[k] = int16_t(s);
    computeAdjacency(h);
    float extra = 0.f, carriers = 0.f;
    for (size_t x = 0; x < g.systems.size(); ++x) {
        if (int(x) == s || g.systems[x].hasToken(p)) continue;
        for (int t : kShipIdx) {
            const int n = g.systems[x].space[size_t(p)].n[size_t(t)];
            if (!n || UnitType(t) == UnitType::Fighter) continue;
            if (shipReach(g, p, int(x), s, UnitType(t), moveBonusMax(g, p)).ok) continue;
            if (!shipReach(h, p, int(x), s, UnitType(t), moveBonusMax(h, p)).ok) continue;
            extra += n * std::max(1.f, unitDef(g, p, UnitType(t)).cost2 * 0.5f);
            if (unitDef(g, p, UnitType(t)).capacity > 0) carriers += n;
        }
    }
    if (extra <= 0.f) return v;
    bool enemies = otherShipsIn(g, p, s), land = false;
    for (int16_t x : g.systems[size_t(s)].planets) land = land || g.planets[size_t(x)].owner != p;
    return v + extra * (enemies ? 0.9f : 0.3f) + (land ? 2.5f * std::min(2.f, carriers) : 0.f);
}
// ... and what the mech costs us (2 resources, a ground force with Sustain Damage; more when it guards a planet alone)
float icarusMechCost(const Game& g, int p, int s, int slot) {
    float c = 3.5f;
    if (slot > 0) {
        const int x = g.systems[size_t(s)].planets[size_t(slot - 1)];
        const PlanetState& ps = g.planets[size_t(x)];
        bool near = otherShipsIn(g, p, s);
        for (int16_t a : g.systems[size_t(s)].adj) near = near || otherShipsIn(g, p, a);
        if (ps.owner == p && ps.units[size_t(p)].groundForces() <= 1 && near) c += 4.f;
    } else if (s == g.activeSystem && p == g.activePlayer) {
        c += 2.f;  // (aboard our ships where we are about to land)
    }
    return c;
}

// Riftwalker Meian (fixes 24 games): the best swap of two systems the hero may take, by what moves next to what - our
// fleets beside planets to take and Mecatol Rex (strength x how good the new surroundings are, against the old), and
// enemy fleets brought beside our planets or taken away from them. Returns the gain (0: nothing worth it).
float meianBest(const Game& g, int p, const std::vector<int>& cands, int* outA, int* outB) {
    const int mec = g.mecatol();
    auto around = [&](Hex pos, int skipA, int skipB, bool mine) {  // targets (or our own planets) beside a spot
        float v = 0.f;
        for (int d = 0; d < 6; ++d) {
            const int x = g.findSystemAt(hexNeighbor(pos, d));
            if (x < 0 || x == skipA || x == skipB) continue;
            for (int16_t pl : g.systems[size_t(x)].planets) {
                const bool ours = g.planets[size_t(pl)].owner == p;
                if (ours == mine) v += planetWorth(g, pl) * (mine ? 1.f : 0.3f);
            }
            if (!mine && x == mec) v += 2.f;
        }
        return v;
    };
    auto enemyStrength = [&](int s) {
        int v = 0;
        for (int q = 0; q < kSides; ++q)
            if (q != p && hasShips(g, q, s)) v += q < g.numPlayers ? combatStrength(g, q, s, true) : 8;
        return v;
    };
    float best = 0.f;
    for (size_t i = 0; i < cands.size(); ++i)
        for (size_t j = i + 1; j < cands.size(); ++j) {
            const int a = cands[i], b = cands[j];
            const Hex pa = g.systems[size_t(a)].pos, pb = g.systems[size_t(b)].pos;
            float gain = 0.f;
            for (int k = 0; k < 2; ++k) {
                const int x = k ? b : a;
                const Hex from = k ? pb : pa, to = k ? pa : pb;
                if (const int s = combatStrength(g, p, x, true)) gain += 0.02f * float(s) * (around(to, a, b, false) - around(from, a, b, false));
                if (const int e = enemyStrength(x)) gain -= 0.02f * float(e) * (around(to, a, b, true) - around(from, a, b, true));
            }
            if (gain > best) best = gain, *outA = a, *outB = b;
        }
    return best;
}
std::vector<int> meianCandidates(const Game& g, int p) {
    std::vector<int> c;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        const SystemState& sy = g.systems[s];
        if (sy.offBoard || sy.isHyperlane() || int(s) == g.nexus || (g.fixes >= 24 && sy.fracture)) continue;
        if (wormholesOf(g, int(s)) || hasUnits(g, p, int(s))) c.push_back(int(s));
    }
    return c;
}

// How attractive a system's surroundings are as a destination (planets of others nearby, Mecatol Rex).
float targetValue(const Game& g, int p, int sys) {
    float v = 0;
    std::vector<int> near = {sys};
    for (int16_t a : g.systems[sys].adj) near.push_back(a);
    for (int s : near)
        for (int16_t x : g.systems[s].planets)
            if (g.planets[x].owner != p) v += planetWorth(g, x) * 0.3f;
    int mec = g.mecatol();
    if (mec >= 0 && (sys == mec || std::find(g.systems[sys].adj.begin(), g.systems[sys].adj.end(), mec) != g.systems[sys].adj.end()))
        v += 2.f;
    return v;
}

int enemyLight(const Game& g, int p, int sys) {  // other players' infantry and fighters in a system
    int n = 0;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p) continue;
        n += g.systems[sys].space[q].count(UnitType::Fighter) + g.systems[sys].space[q].count(UnitType::Infantry);
        for (int16_t x : g.systems[sys].planets) n += g.planets[x].units[q].count(UnitType::Infantry);
    }
    return n;
}

// batch 4 helpers
// Airo Shir Aur: the best (own fleet -> adjacent enemy fleet) throw, as option id system * 1000 + target, or -1.
int bestHurl(const Game& g, int p, float* value) {
    int best = -1;
    float bv = 0.f;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        if (!hasShips(g, p, int(s))) continue;
        for (int16_t a : g.systems[s].adj)
            for (int v = 0; v < g.numPlayers; ++v) {
                if (v == p || !hasShips(g, v, a)) continue;
                float odds = fightOdds(g, p, g.systems[s].space[p], v, g.systems[a].space[v], true);
                float w = odds * float(g.systems[a].space[v].nonFighterShips() + 1);
                if (odds >= 0.75f && w > bv) {
                    bv = w;
                    best = int(s) * 1000 + a;
                }
            }
    }
    if (value) *value = bv;
    return best;
}
// Vortex: what one capture is worth - a fighter / infantry is only a token (LRR 17.3), worth something to the
// Vuil'raith alone; nothing from a player blockading one of our docks (LRR 17.6); units on planets count (0.44.8)
float vortexWorth(const Game& g, int p, int q, int t) {
    if (t == int(UnitType::Fighter) || t == int(UnitType::Infantry)) return isFaction(g, p, "cabal") ? float(unitCost2(g, q, UnitType(t))) : 0.2f;
    return float(unitCost2(g, q, UnitType(t)));
}
bool vortexBlocked(const Game& g, int p, int q) {
    for (size_t s = 0; s < g.systems.size(); ++s) {
        bool dock = false;
        for (int16_t x : g.systems[s].planets)
            if (g.planets[x].units[p].count(UnitType::SpaceDock)) dock = true;
        if (dock && hasShips(g, q, int(s)) && !hasShips(g, p, int(s))) return true;
    }
    return false;
}
int capturableCost(const Game& g, int p) {  // Vortex: the most valuable capture in reach
    float best = 0;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        bool dock = false;
        for (int16_t x : g.systems[s].planets)
            if (g.planets[x].units[p].count(UnitType::SpaceDock)) dock = true;
        if (!dock) continue;
        for (int16_t a : g.systems[s].adj)
            for (int q = 0; q < g.numPlayers; ++q) {
                if (q == p || vortexBlocked(g, p, q)) continue;
                for (int t = 0; t <= int(UnitType::Mech); ++t) {
                    if (isStructure(UnitType(t)) || unitsAvailable(g, q, UnitType(t)) <= 0) continue;
                    int n = g.systems[a].space[q].n[t];
                    for (int16_t x : g.systems[a].planets) n += g.planets[x].units[q].n[t];
                    if (n) best = std::max(best, vortexWorth(g, p, q, t));
                }
            }
    }
    return int(best + 0.5f);
}
}  // namespace

float aiFreeTacticalValue(const Game& g, int p);  // ai.cpp
float aiFactionComponentValue(const Game& g, int p, int option) {
    const Player& me = g.players[p];
    const Ab ab = Ab(option >= 6000 ? option - 6000 : option - 700);
    if (float tv = 0.f; option >= 6000 && aiTeComponentValue(g, p, int(ab), tv)) return tv;  // breakthroughs (ai_te.cpp)
    if (float dv = 0.f; option >= 6000 && aiDsComponentValue(g, p, int(ab), dv)) return dv;  // Discordant Stars (ai_ds.cpp)
    switch (ab) {
        case Ab::HacanAgent: return me.commodities + 2 <= commodityValue(g, me) ? 0.6f : 0.f;
        case Ab::OrbitalDrop:
            // (the Sol review, checkpoint 2 - test knob odrop, OFF) only with enemy ships in or next to a system with a planet
            // of ours, or in the last round. Tested (90 games): 111 Orbital Drops instead of 132, 22 wins / 7.67 VP against 27
            // / 7.84
            if (aiTune("odrop", 0.f) > 0.f && !(aiLevelScale(g) > 0.f && aiEndgame(g, p).lastRound)) {
                bool threat = false;
                for (int x : controlledPlanets(g, p)) {
                    const int sy = g.planets[size_t(x)].system;
                    if (otherShipsIn(g, p, sy)) threat = true;
                    for (int16_t a : g.systems[size_t(sy)].adj)
                        if (otherShipsIn(g, p, a)) threat = true;
                    if (threat) break;
                }
                if (!threat) return 0.f;
            }
            return me.strategy >= 2 ? 1.1f : 0.4f;
        case Ab::ProductionBiomes: return me.strategy >= 2 ? 2.4f : me.strategy >= 1 ? 2.2f : 0.f;  // (0.44.6: 4 trade goods - worth a turn)
        case Ab::NaaluAgent:  // Z'eu Omega: a free tactical action (0.43.0, C3: a promised one is worth keeping the word)
            return g.fixes >= 2 && g.agentFor[p] >= 0 ? 2.5f : aiFreeTacticalValue(g, p);
        case Ab::XxchaAgent: {
            float best = 0;
            for (int x : controlledPlanets(g, p))
                if (g.planets[x].exhausted) best = std::max(best, planetWorth(g, x));
            return best >= 4 ? 0.6f : 0.f;
        }
        case Ab::SolHero: {
            // (the Sol review, checkpoint 2 - test knob jaceuse) only when the planner finds a use (ai.cpp ActionChoice), or as
            // a filler in the last round
            if (aiTune("jaceuse", 0.f) > 0.f && !(aiLevelScale(g) > 0.f && aiEndgame(g, p).lastRound)) return 0.f;
            int n = 0;
            for (const SystemState& s : g.systems)
                if (s.hasToken(p)) ++n;
            return n >= 4 && me.tactic >= 1 ? 1.2f : 0.f;
        }
        case Ab::LetnevHero: {
            // (the Letnev review, checkpoint 2 - test knob darkuse) only when the planner finds a use (ai.cpp ActionChoice),
            // or as a filler in the last round
            if (aiTune("darkuse", 2.f) > 0.f && !(aiLevelScale(g) > 0.f && aiEndgame(g, p).lastRound)) return 0.f;
            for (const SystemState& s : g.systems)
                if (s.space[p].nonFighterShips() >= me.fleet && me.tactic >= 1) return 1.1f;
            return 0.f;
        }
        case Ab::JolnarHero: return g.round >= 5 ? 0.9f : 0.f;
        // 0.43.0 (debug passes: offered ~500 times each, never used - no value here)
        case Ab::ArgentHero: {  // Mirik Aun Sissiri: Raise a Fleet / Command an Armada in one move, or a late regroup
            int movable = 0, best = 0, most = 0;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                if (g.systems[s].home != p) movable += g.systems[s].space[p].nonFighterShips();
                most = std::max(most, g.systems[s].space[p].nonFighterShips());
            }
            for (size_t s = 0; s < g.systems.size(); ++s)
                if (g.systems[s].hasToken(p) && !otherShipsIn(g, p, int(s)))
                    best = std::max(best, movable + (g.systems[s].home == p ? g.systems[s].space[p].nonFighterShips() : 0));
            if (aiLevelScale(g) > 0.f) {
                const Goals& G = aiGoals(g, p);
                if (G.fleetTarget > 0 && best >= G.fleetTarget && most < G.fleetTarget) return 1.5f;
            }
            return g.round >= 5 && best >= 4 ? 0.35f : 0.f;  // (before passing: the fleet gathered for the next round)
        }
        case Ab::NomadHero: {  // Ahk-Syl Siven: the flagship moves again this round
            if (g.round < 3 || me.tactic < 1) return 0.f;
            for (const SystemState& s : g.systems)
                if (s.hasToken(p) && s.space[p].count(UnitType::Flagship)) return 0.9f;
            return 0.f;
        }
        case Ab::XxchaHero: {
            if (!teXxchaHero(g)) return g.round >= 5 ? 1.0f : 0.f;
            // (0.57.1) Thunder's Edge's Xxekir Grom: 4 free PDS / mechs and the planets under them readied - best with
            // spent planets to ready (then spend them again) or enemies close to home
            int spent = 0, value = 0;
            for (int x : controlledPlanets(g, p))
                if (g.planets[size_t(x)].exhausted && !isStation(g, x)) ++spent, value += std::max(planetResources(g, x), planetInfluence(g, x));
            const bool late = g.round >= 5 || me.vp >= g.vpGoal - 3;
            if (spent >= 3 && value >= 6) return 1.2f;
            return late ? 0.8f : 0.f;
        }
        // batch 2
        case Ab::ArborecAgent: return 0.8f;  // a free upgrade
        case Ab::ArborecHero:
            // (the Arborec review, checkpoint 2 - test knob arbhero) it was used with 8 resources that the objective
            // reserve then kept back: in sims it often built nothing at all, or 2 infantry. With the knob: only with 8
            // that can really be spent, and worth more the more there is to spend. Tested OFF: the base built nothing in
            // 25 of 55 uses (13 of 36 on fresh seeds); variant 1 never wasted it but used it in 23 games (13 wins / 6.80
            // VP against 13 / 6.97); variant 2 in 39 games, 8.9 resources of units a use against 4.4, 15 wins / 6.89 VP,
            // fresh seeds 23 / 7.20 against 25 / 7.23 - no gain in the numbers, only no wasted hero. ON (variant 2)
            // since the owner's go (2026-11-12: even in the numbers, and a bot that throws its hero away looks wrong);
            // arbhero=0 is the old behaviour
            if (aiTune("arbhero", 2.f) > 0.f) {
                // (variant 2: from 5 free, or 3 from round 6 on - variant 1 waited for 8 and used it in 23 games of 90)
                const int free = aiFreeSpend(g, p, false);
                const int need = aiTune("arbhero", 2.f) >= 2.f ? (g.round >= 6 ? 3 : 5) : 8;
                return g.round >= 3 && free >= need ? 1.0f + 0.1f * float(std::min(20, free)) : 0.f;
            }
            return g.round >= 3 && spendable(g, p, false) >= 8 ? 1.0f : 0.f;
        case Ab::L1Z1XHero: return g.round >= 5 && unitsOnBoard(g, p, UnitType::Dreadnought) >= 2 ? 0.6f : 0.f;
        case Ab::PromiseOfProtection: return me.tradeGoods >= 2 ? 0.6f : 0.f;
        case Ab::TradeConvoys: return aiTradeConvoysValue(g, p);
        case Ab::DarkPact: return aiDarkPactValue(g, p);
        case Ab::StallTactics: return me.actionCards.size() >= 7 ? 0.35f : 0.f;  // stall instead of passing
        case Ab::MageonImplants: return 0.9f;
        case Ab::YinHero: return g.round >= 3 ? 1.0f : 0.f;
        case Ab::YssarilHero: {
            int cards = 0;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p) cards += int(g.players[q].actionCards.size());
            return g.round >= 3 && cards >= 6 ? 0.9f : 0.f;
        }
        // batch 3
        case Ab::WormholeGenerator: {  // (0.44.7: by the best spot - a badly placed token is worth moving)
            float gain = 0.f;
            for (int k = 0; k < 3; ++k) {
                float best = -9.f;
                for (size_t s = 0; s < g.systems.size(); ++s) best = std::max(best, creussSpot(g, p, int(s), k));
                const float now = g.creussTokens[k] >= 0 ? creussSpot(g, p, g.creussTokens[k], k) : -1.f;
                gain = std::max(gain, best - now);
            }
            return gain >= 4.f ? 1.f : gain >= 2.f ? 0.5f : 0.1f;
        }
        case Ab::StarForge: return me.strategy >= 2 ? 0.8f : 0.3f;
        case Ab::Inferno: {  // (0.43.0 debug passes: a cruiser at the front for a spare token - more so with foes about)
            // (the Muaat review, checkpoint 2 - test knob inferno) never used in 90 games: once the flagship is out the
            // bots rarely hold 2 strategy tokens - the last one goes for a cruiser before passing (value 0.35).
            // OFF - owner 2026-11-12 "leave it as is": 7 uses, no gain (17 -> 16 wins, fresh seeds 12 -> 12).
            if (me.strategy < 2 && !(me.strategy == 1 && aiTune("inferno", 0.f) > 0.f)) return 0.f;
            if (me.strategy == 1) {
                float near1 = 0.f;
                for (size_t s = 0; s < g.systems.size(); ++s)
                    if (g.systems[s].space[size_t(p)].count(UnitType::Flagship))
                        for (int16_t a : g.systems[s].adj)
                            for (int q = 0; q < g.numPlayers; ++q)
                                if (q != p && g.systems[size_t(a)].space[size_t(q)].nonFighterShips() > 0) near1 = 0.3f;
                return 0.35f + near1;
            }
            float near = 0.f;
            for (size_t s = 0; s < g.systems.size(); ++s)
                if (g.systems[s].space[size_t(p)].count(UnitType::Flagship)) {
                    std::vector<int> zone{int(s)};
                    for (int16_t a : g.systems[s].adj) zone.push_back(a);
                    for (int z : zone)
                        for (int q = 0; q < g.numPlayers; ++q)
                            if (q != p && g.systems[size_t(z)].space[size_t(q)].nonFighterShips() > 0) near = 0.3f;
                }
            return (me.strategy >= 3 ? 1.1f : 0.6f) + near;
        }
        case Ab::MuaatAgent:
            // (the Muaat review, checkpoint 2 - test knob umbatplan) only when its production would buy something: 78
            // of 290 uses built nothing (the reserves / the fleet pool kept the production empty).
            // OFF - owner 2026-11-12 "leave it as is": variant 2 cut the wasted uses 78 -> 2 but the Muaat won no more
            // (90 games 17 -> 14 wins, VP 6.86 -> 6.82; fresh seeds 12 -> 11); variant 1 (asks the production code -
            // aiUmbatSystem) still missed, as the real decision keeps the objective reserve (aiFreeSpend 0). Revisit
            // with a reserve-aware prediction or Umbat lending in deals (the owner's guide: offer it to allies).
            if (aiTune("umbatplan", 0.f) >= 2.f) return aiFreeSpend(g, p, false) >= 2 && spendable(g, p, false) >= 4 ? 0.7f : 0.f;  // (variant 2)
            if (aiTune("umbatplan", 0.f) > 0.f) return aiUmbatWorth(g, p) ? 0.8f : 0.f;
            return spendable(g, p, false) >= 4 ? 0.7f : 0.f;
        case Ab::FiresOfGashlai: return 0.6f;
        case Ab::BloodPact: return 0.35f;
        case Ab::LazaxGate: return 0.4f;
        case Ab::CreussHero: {
            if (g.fixes < 24) return g.round >= 4 ? 0.7f : 0.f;
            // (fixes 24) only a swap worth a once-a-game hero - a lower bar late, when it would go unused
            int a = -1, b = -1;
            const float gain = meianBest(g, p, meianCandidates(g, p), &a, &b);
            const float bar = g.round >= 6 || aiEndgame(g, p).lastRound ? aiTune("meianlate", 1.f) : aiTune("meian", 2.5f);
            return gain >= bar ? std::min(1.5f, 0.5f + 0.2f * gain) : 0.f;
        }
        case Ab::SaarHero: {
            int best = 0;
            for (size_t s = 0; s < g.systems.size(); ++s) best = std::max(best, enemyLight(g, p, int(s)));
            return best >= 3 ? 0.9f : 0.f;
        }
        case Ab::WinnuHero: return g.round >= 4 ? 1.0f : 0.f;
        case Ab::NekroHero: return g.round >= 3 ? 1.0f : 0.f;
        case Ab::EmpyreanHero: return g.round >= 4 ? 0.9f : 0.f;
        case Ab::NekroAgent: {
            bool spare = me.tactic >= 3;
            if (me.actionCards.size() >= 3)
                for (int16_t c : me.actionCards)
                    if (aiCardKeepValue(c) < 3.f) spare = true;
            return spare ? 0.5f : 0.f;
        }
        // batch 4
        case Ab::Fabrication:
        case Ab::BlackMarketForgery:
            for (int t = 0; t < 3; ++t)
                if (relicFragments(g, p, t) >= 2) return 1.2f;
            // (the Naaz-Rokha review, checkpoint 2 - test knob fabkeep, default off) the bots purged 1 fragment for a command
            // token ~3 times a game (0.7 relics a game from Fabrication): keep a lone fragment for a pair until the tactic
            // pool is empty or round 6. Tested (fabkeep=1): relics 1.81 -> 1.99 a game; TE 14 wins / 7.04 VP against 9 / 7.19,
            // fresh seeds 17 / 6.91 against 21 / 7.04; plain (fresh) 19 / 7.27 against 18 / 7.24 - no gain. OFF
            if (aiTune("fabkeep", 0.f) > 0.f && me.tactic > 0 && g.round < 6) return 0.f;
            return me.tactic <= 1 ? 0.6f : 0.2f;
        case Ab::Terraform: return 0.8f;
        case Ab::Vortex: {
            const int c = capturableCost(g, p);
            return c >= 6 ? 0.8f : c >= 2 ? 0.4f : 0.05f;  // (0.44.8: not an action for a token)
        }
        case Ab::MahactHero: {
            float v = 0;
            return bestHurl(g, p, &v) >= 0 ? 1.2f : 0.f;
        }
        case Ab::NaazHero: return g.round >= 3 ? 1.2f : 0.f;
        case Ab::TitansHero: {
            int d = findPlanet("elysium");
            for (int x : controlledPlanets(g, p))
                if (g.planets[x].def == d) return g.round >= 3 ? 1.0f : 0.f;
            return 0.f;
        }
        // batch 5: the Council Keleres
        case Ab::ExecutiveOrder: {
            if (g.round < 2) return 0.f;
            int mine = me.tradeGoods, best = 0;
            for (int x : readyPlanets(g, p)) mine += std::max(planetInfluence(g, x), planetResources(g, x));
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && !g.players[q].eliminated) best = std::max(best, availableVotes(g, q));
            // (the Keleres review, checkpoint 2 - knob keleo, test: a multiplier, default 1 = as before; 2.6 uses a game, each
            // exhausting planets for the votes)
            return aiTune("keleo", 1.f) * (mine >= best ? (fquirkIs(g, p, "keleres", 1) ? 0.9f : 0.6f) : 0.25f);
        }
        case Ab::KeleresHarka: return g.round >= 2 ? 0.9f : 0.f;
        case Ab::CabalHero: {
            int ships = 0;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                if (!dimensionalTear(g, int(s))) continue;
                std::vector<int> zone = {int(s)};
                for (int16_t a : g.systems[s].adj) zone.push_back(a);
                for (int z : zone)
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p) ships += g.systems[z].space[q].nonFighterShips();
            }
            // (the Vuil'raith review, checkpoint 2 - test knob carrion) wait for a big catch (expected captured cost =
            // 0.3 x their ships' cost) and then use it ahead of a tactical action; a filler in the last round. OFF (90 games,
            // carrion=3: hero captures 94 -> 104, 12 wins / 6.38 VP against 11 / 6.41 - no gain)
            if (aiTune("carrion", 0.f) > 0.f) {
                float cost = 0.f;
                std::vector<int> seen;
                for (size_t s = 0; s < g.systems.size(); ++s) {
                    if (!dimensionalTear(g, int(s))) continue;
                    std::vector<int> zone = {int(s)};
                    for (int16_t a : g.systems[s].adj) zone.push_back(a);
                    for (int z : zone) {
                        if (std::find(seen.begin(), seen.end(), z) != seen.end()) continue;
                        seen.push_back(z);
                        for (int q = 0; q < g.numPlayers; ++q)
                            if (q != p)
                                for (int t : kShipIdx)
                                    if (t != int(UnitType::Fighter)) cost += 0.5f * float(g.systems[size_t(z)].space[size_t(q)].n[t] * unitCost2(g, q, UnitType(t)));
                    }
                }
                const float catchV = 0.3f * cost;
                if (aiLevelScale(g) > 0.f && aiEndgame(g, p).lastRound) return catchV >= 1.f ? 2.5f : 0.f;
                return catchV >= aiTune("carrion", 0.f) ? 1.0f + catchV : 0.f;
            }
            return ships >= 3 ? 1.0f : 0.f;
        }
        default: return 0.f;
    }
}

bool aiFactionAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out) {
    (void)rng;
    if (d.tag != Tag::Faction) return false;
    const int p = d.player;
    const Player& me = g.players[p];
    const Ab ab = Ab(d.ability);
    const bool yn = yesNo(d);
    if (aiTeAnswer(g, d, out)) return true;  // Thunder's Edge (ai_te.cpp)
    if (aiDsAnswer(g, d, rng, out)) return true;  // Discordant Stars (ai_ds.cpp)
    switch (ab) {
        case Ab::OrbitalDrop:
        case Ab::MilitarySupport:
        case Ab::SardakkAgent:
        case Ab::InfantryReturn:
            // (Sardakk review, owner 2026-11-11) T'ro: not on a home planet while 2+ tactic tokens are left (a front-line
            // planet later in the round is worth more) - unless enemy ships are within 2 systems of home
            if (yn && ab == Ab::SardakkAgent && d.prompt.find("to let") == std::string::npos && aiLevelScale(g) > 0.f &&
                aiTune("trohold", 1.f) > 0.f && g.activeSystem >= 0 && me.tactic >= 2 && g.systems[size_t(g.activeSystem)].home >= 0) {
                bool away = false;  // a planet of ours here outside a home system
                for (int16_t x : g.systems[size_t(g.activeSystem)].planets)
                    if (g.planets[size_t(x)].owner == p && !isHomePlanet(g, x)) away = true;
                bool threat = false;
                if (me.homeSystem >= 0)
                    for (int s2 : systemsWithin(g, me.homeSystem, 2))
                        if (otherShipsIn(g, p, s2)) threat = true;
                if (!away && !threat) {
                    out = yes(false);
                    return true;
                }
            }
            if (yn) {
                // T'ro used for someone else: no
                out = yes(ab != Ab::SardakkAgent || d.prompt.find("to let") == std::string::npos);
                return true;
            }
            {
                int x = bestBy(d, [&](int id) { return defendScore(g, p, id); });
                out = one(x >= 0 ? x : firstReal(d));
            }
            return true;
        case Ab::SolMechDeploy: out = yes(spendable(g, p, false) >= 5); return true;
        case Ab::PrideOfKenara: {  // (the Hacan review) the planet that most needs the units
            int x = bestBy(d, [&](int id) { return defendScore(g, p, id); });
            out = one(x >= 0 ? x : firstReal(d));
            return true;
        }
        case Ab::NaaluAgent:  // Z'eu: the (old) peek is for people; Z'eu Omega's free tactical action goes to itself
            // (fixes 27) Thunder's Edge's Z'eu: our own command token back (a free activation) - yes; another player's: only
            // a promised one (answered in ai_deals)
            if (d.prompt.find("Exhaust Z'eu to return your command token") == 0) {
                out = one(1);
                return true;
            }
            if (d.prompt.find("Exhaust Z'eu: return ") == 0) {
                out = one(0);
                return true;
            }
            if (yn) out = yes(false);
            else out = one(g.fixes >= 2 && g.agentFor[p] >= 0 && has(d, g.agentFor[p]) ? g.agentFor[p] : p);  // promised: theirs
            return true;
        case Ab::NaaluMechTE: {  // (fixes 27) Iconoclast (Thunder's Edge): the planet that most needs a defender, else the best
            const int x = bestBy(d, [&](int id) { return id < 0 ? -1.f : planetWorth(g, id) + 2.f * defendScore(g, p, id); });
            out = one(x >= 0 ? x : firstReal(d));
            return true;
        }
        case Ab::AiDevAlgorithm: {  // 0.30.0: keep the planet when money is tight, else keep the card for production
            if (d.prompt.find("instead of your other waiver") != std::string::npos) {  // (0.44.9: AIDA before a one-shot waiver)
                out = yes(true);
                return true;
            }
            int cost = 6;
            out = yes(spendable(g, p, false) < cost + 4);
            return true;
        }
        case Ab::LetnevMechDeploy: out = yes(spendable(g, p, false) >= 4); return true;
        case Ab::SolCommander:  // (the Sol review, owner's go 2026-11-12) Claire Gibson costs nothing and works every combat
            if (yn && aiTune("claireold", 0.f) <= 0.f) {
                out = yes(true);
                return true;
            }
            [[fallthrough]];
        case Ab::Tekklar:
            // (Sardakk review, owner 2026-11-11) a close fight (fewer than twice their ground forces), or any fight against
            // the Sardakk (their -1 too); otherwise keep it for a fight that matters
            if (yn && aiLevelScale(g) > 0.f && aiTune("tekklarold", 0.f) <= 0.f && g.groundCombatPlanet >= 0) {
                const PlanetState& ps = g.planets[size_t(g.groundCombatPlanet)];
                const int mine = ps.units[size_t(p)].groundForces();
                int theirs = 0;
                bool vsSardakk = false;
                for (int q = 0; q < kSides; ++q) {
                    if (q == p || ps.units[size_t(q)].groundForces() == 0) continue;
                    theirs += ps.units[size_t(q)].groundForces();
                    if (q < g.numPlayers && isFaction(g, q, "sardakk")) vsSardakk = true;
                }
                out = yes(mine > 0 && (vsSardakk || theirs == 0 || mine < 2 * theirs));  // (none left to fight with: keep it)
                return true;
            }
            [[fallthrough]];
        case Ab::Brilliant:
        case Ab::ResearchAgreement:
        case Ab::JolnarCommander:
        case Ab::XxchaAgent:
            if (yn) {
                out = yes(true);
                return true;
            }
            if (ab == Ab::XxchaAgent) {  // ready our best exhausted planet
                int x = bestBy(d, [&](int id) { return g.planets[id].owner == p ? planetWorth(g, id) : -1.f; }, 0.f);
                out = one(x >= 0 ? x : (has(d, -1) ? -1 : firstReal(d)));
                return true;
            }
            out = one(firstReal(d));
            return true;
        case Ab::SolAgent:
        case Ab::LetnevAgent: {
            int c = ownOption(g, d, p);
            out = one(c >= 0 ? c : -1);
            return true;
        }
        case Ab::Munitions: {
            int sys = g.activeSystem;
            bool hard = true;
            if (sys >= 0) {
                int mine = combatStrength(g, p, sys, true), theirs = 0;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p) theirs = std::max(theirs, combatStrength(g, q, sys, true));
                hard = theirs * 10 >= mine * 6;
            }
            out = yes(hard || me.tradeGoods >= 6);
            return true;
        }
        case Ab::WarFunding:
            // (the Letnev review) War Funding Omega's second choice: reroll the opponent's dice when they hit more than their
            // dice do on average (Decision::value = hits x 1000 + the average x 10)
            if (d.prompt.rfind("War Funding: reroll all of", 0) == 0) {
                out = yes((d.value / 1000) * 10 > d.value % 1000);
                return true;
            }
            out = yes(outgunned(g, p));
            return true;
        case Ab::WrathOfKenara: {
            int n = std::min<int>(int(d.options.size()), std::max(0, int(me.tradeGoods)));
            n = std::min(n, d.maxPick);
            out = Answer();
            for (const Option& o : d.options)
                if (int(out.picks.size()) < n) out.picks.push_back(o.id);
            return true;
        }
        case Ab::QuantumDatahub: {
            // 0.44.5: the cards as this player values them now (not a fixed table), against the price - 3 trade
            // goods, a strategy token - and a little for what the other player gains or loses by the swap
            int best = -1;
            float bv = 4.5f;
            if (me.tradeGoods >= 3 + aiGoals(g, p).keepTg)
                for (const Option& o : d.options) {
                    if (o.id < 0) continue;
                    const int mine = o.id / 100, theirs = o.id % 100;
                    const int q = theirs >= 0 && theirs < kStrategyCards ? g.cardOwner[theirs] : -1;
                    float gain = aiStrategyCardValue(g, p, theirs) - aiStrategyCardValue(g, p, mine);
                    if (q >= 0 && q < g.numPlayers) gain -= 0.3f * (aiStrategyCardValue(g, q, mine) - aiStrategyCardValue(g, q, theirs));
                    if (gain > bv) {
                        bv = gain;
                        best = o.id;
                    }
                }
            out = one(best);
            return true;
        }
        case Ab::ProductionBiomes: {  // give the 2 trade goods to the player furthest behind
            int q = bestBy(d, [&](int id) { return -float(g.players[id].vp); });
            out = one(q >= 0 ? q : firstReal(d));
            return true;
        }
        case Ab::HacanAgent: out = one(has(d, p) ? p : -1); return true;
        case Ab::HacanCommander: {
            int k = me.tradeGoods >= 6 ? 2 : 0;
            // (the Hacan review, checkpoint 2 - test knob gila, OFF) half the trade goods over 4, up to 6 (12 votes). Tested (90
            // games): votes 10.8 a game vs 8.0, 19 wins / 7.39 VP against 17 / 7.30, fresh 17 / 7.39 against 16 / 7.37, plain
            // 25 / 7.83 against 25 / 7.74; with harrugh=5 too 16 / 7.34 and fresh 15 / 7.38 - inside the noise
            if (aiTune("gila", 0.f) > 0.f) k = std::max(0, std::min(6, (me.tradeGoods - 4) / 2));
            out = one(has(d, k) ? k : 0);
            return true;
        }
        case Ab::HacanHero:
            // (the Hacan review, checkpoint 2 - test knob harrugh) only at a dock that produces harrugh units or more, or
            // in the last round (85 uses in 90 games: 51 at the 4-unit home dock, 23 saved under 5 resources). Tested (90
            // games): harrugh=5 18 wins / 7.33 VP against 17 / 7.30, fresh 18 / 7.40 against 16 / 7.37, plain 27 / 7.84
            // against 25 / 7.74 (68 uses instead of 85); harrugh=6 17 / 7.31; with gila too 16 / 7.34, fresh 15 / 7.38
            // Owner 2026-11-12 20:41 ("if there is consistency in improvement, let's keep that one on"): ON, default 5
            if (aiTune("harrugh", 5.f) > 0.f) {
                const int sys = g.activeSystem >= 0 ? g.activeSystem : me.homeSystem;
                const bool last = aiLevelScale(g) > 0.f && aiEndgame(g, p).lastRound;
                out = yes(g.round >= 4 && (last || (sys >= 0 && productionCapacity(g, p, sys) >= int(aiTune("harrugh", 5.f)))));
                return true;
            }
            out = yes(g.round >= 4);
            return true;
        case Ab::SpatialConduit: {
            // worth it when ships elsewhere can't reach the active system normally
            int sys = g.activeSystem, newly = 0;
            if (sys >= 0)
                for (size_t s = 0; s < g.systems.size(); ++s) {
                    if (int(s) == sys || g.systems[s].hasToken(p)) continue;
                    const Force& f = g.systems[s].space[p];
                    for (int t : kShipIdx)
                        if (f.n[t] && unitDef(g, p, UnitType(t)).move > 0 && !shipReach(g, p, int(s), sys, UnitType(t)).ok)
                            newly += f.n[t];
                }
            bool enemy = false;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && sys >= 0 && hasUnits(g, q, sys)) enemy = true;
            // (0.44.7: also to land on an empty planet - unowned or someone else's - with 2 or more ships coming in)
            bool planet = false;
            if (sys >= 0)
                for (int16_t x : g.systems[size_t(sys)].planets) planet = planet || g.planets[size_t(x)].owner != p;
            // (the Jol-Nar review, checkpoint 2 - switch sccuse, test: 1 ship that gets there is enough)
            out = yes(newly >= (aiTune("sccuse", 0.f) > 0.f ? 1 : 2) && (enemy || planet));
            return true;
        }
        case Ab::JolnarAgent: {
            // (fixes 27, the Jol-Nar review) for another player's research: only a promised use (answered in ai_deals) - a
            // person's seat in tests: no
            if (d.prompt.find("Exhaust Doctor Sucaban: ") == 0) {
                out = one(0);
                return true;
            }
            if (yn) {
                out = yes(d.prompt.find("to let") == std::string::npos);
                return true;
            }
            if (d.prompt.find("from where") != std::string::npos) {  // (a person's seat in tests: the biggest group)
                int best = -1, most = -1;
                for (const Option& o : d.options) {
                    const int n = o.id >= 10000 ? g.systems[size_t(o.id - 10000)].space[size_t(p)].count(UnitType::Infantry)
                                                : g.planets[size_t(o.id)].units[size_t(p)].count(UnitType::Infantry);
                    if (n > most) most = n, best = o.id;
                }
                out = one(best);
                return true;
            }
            int planets = 0;
            for (int x : controlledPlanets(g, p))
                if (g.planets[x].units[p].count(UnitType::Infantry)) ++planets;
            int spare = std::max(0, unitsOnBoard(g, p, UnitType::Infantry) - planets);
            int k = 0;
            for (const Option& o : d.options)
                if (o.id <= spare) k = std::max(k, o.id);
            out = one(k);
            return true;
        }
        case Ab::JolnarHero: {
            if (d.prompt.find(" with?") != std::string::npos) {  // choosing the replacement
                int t = bestBy(d, [&](int id) { return aiTechWant(g, p, id); });
                out = one(t >= 0 ? t : -1);
                return true;
            }
            // which technology to replace: a weak one whose colour has a better choice
            int best = -1;
            float bv = 0.9f;
            for (const Option& o : d.options) {
                if (o.id < 0) continue;
                float want = 0;
                for (int u = 0; u < kTechCount; ++u)
                    if (kTechs[u].color == kTechs[o.id].color && !kTechs[u].unitUpgrade && techAvailableToResearch(g, p, u))
                        want = std::max(want, aiTechWant(g, p, u));
                float gain = want - keepValue(g, p, o.id);
                if (gain > bv) {
                    bv = gain;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case Ab::Exotrireme: {
            static const UnitType kOrder[] = {UnitType::WarSun, UnitType::Flagship, UnitType::Flagship2, UnitType::Dreadnought,
                                              UnitType::Carrier, UnitType::Cruiser, UnitType::Destroyer, UnitType::Fighter};
            if (yn) {
                int sys = g.activeSystem >= 0 ? g.activeSystem : 0;
                if (aiTune("exoold", 0.f) > 0.f) {  // (test only: the rough strength count before the Sardakk review)
                    int strong = 0;
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p) {
                            const Force& f = g.systems[sys].space[q];
                            strong += f.count(UnitType::WarSun) * 3 + f.flagships() * 3 + f.count(UnitType::Dreadnought) * 2 +
                                      f.count(UnitType::Carrier) + f.count(UnitType::Cruiser);
                        }
                    out = yes(strong >= 3);
                    return true;
                }
                // (Sardakk review, owner 2026-11-11) a trade: the 2 best ships it destroys against the Exotrireme it costs
                // (a damaged one goes first), or a fight it is losing that the trade turns
                int o = -1;
                for (int q = 0; q < kSides && o < 0; ++q)
                    if (q != p && !g.systems[size_t(sys)].space[size_t(q)].empty() && hasShips(g, q, sys)) o = q;
                if (o < 0) {
                    out = yes(false);
                    return true;
                }
                const Force& mine = g.systems[size_t(sys)].space[size_t(p)];
                const Force& theirs = g.systems[size_t(sys)].space[size_t(o)];
                auto worth = [&](UnitType t, bool damaged) {
                    const float c = float(unitDef(g, o, t).cost2) / 2.f;
                    return damaged ? c * 0.6f : c;
                };
                Force after = theirs;
                float gain = 0.f;
                for (int n = 0; n < 2; ++n) {  // the two best: undamaged first, then damaged
                    float bv = 0.f;
                    int bt = -1;
                    bool bd = false;
                    for (UnitType t : kOrder) {
                        const int i = int(t);
                        if (after.n[i] - after.dmg[i] > 0 && worth(t, false) > bv) bv = worth(t, false), bt = i, bd = false;
                        if (after.dmg[i] > 0 && worth(t, true) > bv) bv = worth(t, true), bt = i, bd = true;
                    }
                    if (bt < 0) break;
                    gain += bv;
                    after.remove(UnitType(bt), 1, bd);
                }
                const bool dmgDread = mine.dmg[int(UnitType::Dreadnought)] > 0;
                const float cost = 4.f * (dmgDread ? 0.6f : 1.f);
                Force myAfter = mine;
                myAfter.remove(UnitType::Dreadnought, 1, true);
                const float now = fightOdds(g, p, mine, o, theirs, true);
                const float then = after.nonFighterShips() + after.count(UnitType::Fighter) == 0 ? 1.f : fightOdds(g, p, myAfter, o, after, true);
                out = yes(gain > cost + 0.75f || (now < 0.45f && then > now + 0.1f));
                return true;
            }
            out = Answer();
            for (UnitType t : kOrder)
                for (int damaged = 0; damaged < 2; ++damaged)  // (fixes 25 lists them apart: undamaged first)
                    for (const Option& o : d.options)
                        if (o.id / 16 == int(t) && ((o.id % 16) >= 8) == (damaged == 1) && int(out.picks.size()) < d.maxPick) out.picks.push_back(o.id);
            if (int(out.picks.size()) < d.minPick) out.picks.push_back(firstReal(d));
            return true;
        }
        case Ab::SardakkCommander: {
            // bring ground forces from planets that keep at least one
            out = Answer();
            int sys = g.activeSystem;
            bool target = false;
            if (sys >= 0)
                for (int16_t x : g.systems[sys].planets)
                    if (g.planets[x].owner != p && !isDMZ(g, x)) target = true;
            // (Sardakk review, owner 2026-11-11: also from the active system's own planets, to land on another one there)
            const bool here = aiTune("ghomhere", 1.f) > 0.f;
            if (target)
                for (const Option& o : d.options)
                    if (o.id >= 0 && g.planets[o.id].units[p].groundForces() >= 2 && (here || g.planets[o.id].system != sys))
                        out.picks.push_back(o.id);
            return true;
        }
        case Ab::CrimsonCommander: {  // (0.58.2) Ahk Siever: a commodity while there's room, else turn one into a trade good
            out = one(has(d, 1) ? 1 : has(d, 2) ? 2 : 0);
            return true;
        }
        case Ab::SardakkCommanderUnit: {
            // the mech (it can sustain damage) when the planet keeps an infantry behind; else the infantry
            const int x = d.value;
            const bool mech = x >= 0 && x < int(g.planets.size()) && g.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry) >= 1;
            out = Answer();
            out.picks.push_back(mech ? 1 : 0);
            return true;
        }
        case Ab::SardakkHero: {
            int sys = g.activeSystem;
            int mine = sys >= 0 ? combatStrength(g, p, sys, true) : 0, theirs = 0;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && sys >= 0) theirs = std::max(theirs, combatStrength(g, q, sys, true));
            if (aiTune("shvalold", 0.f) > 0.f || sys < 0) {  // (test only: the rule before the Sardakk review)
                out = yes(theirs > mine && sys >= 0 && g.systems[sys].space[p].groundForces() >= 3);
                return true;
            }
            // (Sardakk review, owner 2026-11-11) Sh'val is worth nothing once the game is over: worth it when it skips a
            // fight or space cannon that costs something - a stronger fleet, guns that would hit our ships, or from round 5
            // (the last round: always) a fight for planets worth more than the ships it sends home
            const SystemState& s = g.systems[size_t(sys)];
            const int gf = s.space[size_t(p)].groundForces();
            bool enemyShips = otherShipsIn(g, p, sys);
            float cannon = 0.f;  // expected space cannon offense hits against us
            for (int q = 0; q < kSides; ++q) {
                if (q == p) continue;
                auto guns = [&](const Force& f, bool deepOnly) {
                    for (int t = 0; t < kUnitTypes; ++t) {
                        if (!f.n[t]) continue;
                        const UnitDef& u = unitDef(g, q, UnitType(t));
                        if (u.cannon <= 0 || (deepOnly && !u.deepCannon)) continue;
                        cannon += float(f.n[t] * std::max<int>(1, u.cannonDice)) * std::max(0.f, (11.f - float(u.cannon)) / 10.f);
                    }
                };
                for (int16_t x : s.planets) guns(g.planets[size_t(x)].units[size_t(q)], false);
                guns(s.space[size_t(q)], false);
                for (int16_t a : s.adj) {
                    for (int16_t x : g.systems[size_t(a)].planets) guns(g.planets[size_t(x)].units[size_t(q)], true);
                    guns(g.systems[size_t(a)].space[size_t(q)], true);
                }
            }
            float shipCost = 0.f, gain = 0.f;
            for (int t : kShipIdx) shipCost += float(s.space[size_t(p)].n[t]) * float(unitDef(g, p, UnitType(t)).cost2) / 2.f;
            for (int16_t x : s.planets) {
                const PlanetState& ps = g.planets[size_t(x)];
                if (ps.owner == p || isDMZ(g, x)) continue;
                int def = 0;  // (only planets the landing can take: 2 per defender + 1, as the planner counts)
                for (int q = 0; q < kSides; ++q)
                    if (q != p) def += g.planets[size_t(x)].units[size_t(q)].groundForces();
                if (gf < (def > 0 ? def * 2 + 1 : 1)) continue;
                gain += float(planetResources(g, x) + planetInfluence(g, x)) + (ps.owner >= 0 ? 2.f : 0.f) +
                        (isLegendaryPlanet(g, x) ? 3.f : 0.f);
                if (sys == g.mecatol()) gain += 3.f;
            }
            const bool late = g.round >= 5 || aiEndgame(g, p).lastRound;
            bool use = false;
            if (gf >= 2 && theirs > mine) use = true;                          // a fight we'd lose
            if (gf >= 2 && cannon >= 1.f && shipCost <= 8.f) use = true;       // guns that would cost us ships
            if (late && gf >= 1 && (enemyShips || cannon >= 1.f) && (aiEndgame(g, p).lastRound || shipCost <= gain * 1.5f)) use = true;
            if (gain <= 0.f) use = false;
            out = yes(use);
            return true;
        }
        case Ab::PeaceAccords: {
            int x = bestBy(d, [&](int id) { return planetWorth(g, id) + (g.planets[id].owner < 0 ? 1.f : 0.f); });
            out = one(x >= 0 ? x : -1);
            return true;
        }
        case Ab::Quash:
        case Ab::PoliticalFavor: {
            int a = d.agenda >= 0 ? d.agenda : g.currentAgenda;
            bool use = false;
            // (the Xxcha review, checkpoint 2 - test knob quashuse, default off) Quash was never used in 90 games: by the agenda
            // phase the bots rarely keep 2 strategy tokens - with the knob, the last token is enough when the agenda likely
            // to pass costs us more than the knob's value. Tested (quashuse=1.5): Quash 10 times in 90 games (none before),
            // 9 wins / 6.96 VP against 11 / 6.98 - OFF
            const float qbar = aiTune("quashuse", 0.f);
            if (a >= 0 && (ab == Ab::PoliticalFavor || me.strategy >= 2 || (qbar > 0.f && me.strategy >= 1))) {
                float conf = 0;
                int o = aiPredictOutcome(g, a, &conf);
                const float v = aiOutcomeValue(g, p, a, o);
                use = ab == Ab::Quash && qbar > 0.f && me.strategy < 2 ? v < -qbar : v < -1.0f;
            }
            out = yes(use);
            return true;
        }
        case Ab::InstinctTraining: {
            // 0.44.6: a Sabotage against our own good card - cancel the Sabotage; a strong card against us - the last
            // strategy token is worth it too (it used to need 2, which the secondaries had usually taken)
            if (d.value > 0) {
                const int owner = (d.value - 1) / 1000, card = (d.value - 1) % 1000;
                out = yes(owner == p && me.strategy >= 1 && aiCardKeepValue(card) >= 1.5f);
                return true;
            }
            const bool strong = d.card >= 0 && aiCardKeepValue(d.card) >= 2.f;
            out = yes((me.strategy >= 2 || (me.strategy >= 1 && strong)) && d.card >= 0 && aiWantsCancel(g, p, d.card, d.targetPlayer));
            return true;
        }
        case Ab::NullificationField: {
            // (the Xxcha review, checkpoint 2 - test knob nfuse, default off) used 4 times in 90 games (it needs 2 strategy
            // tokens): with the knob, 1 is enough when the active player's ships nearby outnumber ours in the system by the
            // knob's value (an attack is coming). Tested (nfuse=1): 20 uses in 90 games against 4, 11 wins / 6.98 VP either
            // way (both knobs: 9 / 6.96) - OFF
            if (aiTune("nfuse", 0.f) > 0.f && me.strategy >= 1 && g.activeSystem >= 0 && g.activePlayer >= 0 && g.activePlayer != p) {
                const int sys = g.activeSystem, q = g.activePlayer;
                int theirs = g.systems[size_t(sys)].space[size_t(q)].nonFighterShips();
                for (int16_t a : g.systems[size_t(sys)].adj) theirs += g.systems[size_t(a)].space[size_t(q)].nonFighterShips();
                const int mine = g.systems[size_t(sys)].space[size_t(p)].nonFighterShips();
                out = yes(me.strategy >= 2 || float(theirs) >= float(mine) + aiTune("nfuse", 0.f));
                return true;
            }
            out = yes(me.strategy >= 2);
            return true;
        }
        case Ab::XxchaHero: {
            if (teXxchaHero(g)) {  // (0.57.1) where the PDS / mechs go: spent planets first (they are readied), then the
                // planets worth most, mechs where enemies are near, PDS where there are none yet
                int best = -1;
                float bv = -1e9f;
                for (const Option& o : d.options) {
                    if (!o.enabled || o.id < 0) continue;
                    const int x = o.id / 2;
                    const bool mech = o.id % 2;
                    const PlanetState& ps = g.planets[size_t(x)];
                    float v = float(planetResources(g, x) + planetInfluence(g, x)) * 0.3f;
                    if (ps.exhausted) v += 3.f + float(std::max(planetResources(g, x), planetInfluence(g, x)));
                    bool enemies = false;
                    for (int16_t a : g.systems[size_t(ps.system)].adj)
                        for (int q = 0; q < g.numPlayers; ++q)
                            if (q != p && g.systems[size_t(a)].space[size_t(q)].ships() > 0) enemies = true;
                    const int pdsHere = ps.units[size_t(p)].count(UnitType::PDS);
                    if (mech) v += enemies ? 1.5f : 0.6f;
                    else v += pdsHere == 0 ? 1.2f : 0.2f;
                    if (ps.units[size_t(p)].total() == 0) v += 0.5f;
                    if (v > bv) bv = v, best = o.id;
                }
                out = one(best >= 0 ? best : firstReal(d));
                return true;
            }
            if (d.maxPick > 1 || d.minPick != 1) {  // which agendas
                std::vector<std::pair<float, int>> v;
                for (const Option& o : d.options) {
                    if (!o.enabled || o.id < 0) continue;
                    v.push_back({-aiOutcomeValue(g, p, o.id, 0), o.id});
                }
                std::sort(v.begin(), v.end());
                out = Answer();
                for (auto& x : v)
                    if (int(out.picks.size()) < d.maxPick) out.picks.push_back(x.second);
                return true;
            }
            int a = d.agenda;
            int o = bestBy(d, [&](int id) { return a >= 0 ? aiOutcomeValue(g, p, a, id) : 0.f; });
            out = one(o >= 0 ? o : firstReal(d));
            return true;
        }
        // ---------------- batch 2
        case Ab::Mitosis:
        case Ab::YinSpinner:
        case Ab::YssarilMechDeploy: {
            int x = bestBy(d, [&](int id) {
                if (id >= 20000) {  // Yin Spinner Omega: in space with ships (cargo for an invasion) - 0.44.6: only with room
                    const int s = id - 20000;
                    if (s < 0 || s >= int(g.systems.size())) return -1.f;
                    const int room = shipCapacity(g, p, s) + dockFighterSlots(g, p, s) - transportedInSpace(g, p, s);
                    return room > 0 ? 0.3f : -1.f;
                }
                int pl = id % 10000;
                return defendScore(g, p, pl) + (id >= 10000 ? 0.5f : 0.f);
            });
            out = one(x >= 0 ? x : firstReal(d));
            return true;
        }
        case Ab::Bioplasmosis: {
            if (d.prompt.find("from which") != std::string::npos) {
                int x = bestBy(d, [&](int id) {
                    int n = g.planets[id].units[p].count(UnitType::Infantry);
                    return n >= 4 ? float(n) - defendScore(g, p, id) : -1.f;
                }, 0.f);
                out = one(x >= 0 ? x : -1);
                return true;
            }
            if (d.prompt.find("how many") != std::string::npos) {
                int k = 1;
                for (const Option& o : d.options) k = std::max(k, o.id);
                out = one(std::max(1, k / 2));
                return true;
            }
            // (0.44.6) only where they are clearly needed more than where they stand - moving half of them makes the
            // source weaker and the target stronger, so the gap must be bigger than that (it used to move them back and
            // forth between two planets)
            const int src = d.value - 1;
            if (src >= 0 && src < int(g.planets.size())) {
                const float k = float(std::max(1, g.planets[size_t(src)].units[size_t(p)].count(UnitType::Infantry) / 2));
                const float base = defendScore(g, p, src);
                int y = bestBy(d, [&](int id) { return defendScore(g, p, id) - base - k - 1.f; }, 0.f);
                out = one(y >= 0 ? y : -1);
                return true;
            }
            int y = bestBy(d, [&](int id) { return defendScore(g, p, id); });
            out = one(y >= 0 ? y : -1);
            return true;
        }
        case Ab::ArborecFlagship: out = yes(spendable(g, p, false) >= 3); return true;
        case Ab::ArborecCommander: out = yes(spendable(g, p, false) >= 2); return true;
        case Ab::Stymie: {
            // freeze the most ships of the player whose token it is (Stymie Omega: the mover - the active player)
            int a = codexOn(g) && g.activePlayer >= 0 ? int(g.activePlayer) : playerOfFaction(g, "arborec");
            int s = bestBy(d, [&](int id) { return a >= 0 ? float(g.systems[id].space[a].ships()) : 0.f; }, 0.5f);
            out = one(s >= 0 ? s : -1);
            return true;
        }
        case Ab::ArborecAgent: {
            auto gain = [&](int t, int u) {
                return float(unitCost2(g, p, UnitType(u)) - unitCost2(g, p, UnitType(t))) + (u == int(UnitType::Dreadnought) ? 2.f : 0.f);
            };
            if (d.prompt.find("which player's ship") != std::string::npos) {
                // (fixes 26) the Arborec bot picks its own ship with the best upgrade, as before (another player's: later)
                float best = 0.f;
                int pick = -1;
                for (const Option& o : d.options) {
                    if (o.id < 0 || o.id / 4096 != p) continue;
                    const int t = o.id % 16;
                    for (int u : kShipIdx)
                        if (u != t && u != int(UnitType::Fighter) && unitsAvailable(g, p, UnitType(u)) > 0 &&
                            unitCost2(g, p, UnitType(u)) <= unitCost2(g, p, UnitType(t)) + 4 && gain(t, u) > best)
                            best = gain(t, u), pick = o.id;
                }
                out = one(pick);
                return true;
            }
            if (d.prompt.find("Letani Ospha: replace your") != std::string::npos) {  // (fixes 26) the ship's owner picks
                const std::string ty = d.prompt.substr(std::string("Letani Ospha: replace your ").size());
                int t = -1;
                for (int x : kShipIdx)
                    if (ty.compare(0, std::strlen(unitTypeName(UnitType(x))), unitTypeName(UnitType(x))) == 0 &&
                        (t < 0 || std::strlen(unitTypeName(UnitType(x))) > std::strlen(unitTypeName(UnitType(t)))))
                        t = x;
                int pick = -1;
                float best = 0.f;
                for (const Option& o : d.options)
                    if (o.id >= 0 && t >= 0 && gain(t, o.id) > best) best = gain(t, o.id), pick = o.id;
                out = one(pick);
                return true;
            }
            int c = bestBy(d, [&](int id) {
                int t = (id / 16) % 16, u = id % 16;
                return gain(t, u);
            });
            out = one(c >= 0 ? c : -1);
            return true;
        }
        case Ab::ArborecHero: {
            if (aiTune("arbhero", 2.f) > 0.f) {  // (arbhero, on) the front first: enemy ships near, our fleet there
                if (aiFreeSpend(g, p, false) < 2) {
                    out = one(-1);
                    return true;
                }
                int s = bestBy(d, [&](int id) {
                    if (id < 0 || id >= int(g.systems.size())) return -1.f;
                    float v = 0.1f * float(g.systems[size_t(id)].space[size_t(p)].nonFighterShips());
                    for (int x : systemsWithin(g, id, 1))
                        for (int q = 0; q < g.numPlayers; ++q)
                            if (q != p) v += float(g.systems[size_t(x)].space[size_t(q)].nonFighterShips());
                    if (id == g.mecatol()) v += 1.f;
                    if (id == g.players[size_t(p)].homeSystem) v += 0.5f;
                    return v;
                });
                out = one(has(d, s) ? s : -1);
                return true;
            }
            int s = spendable(g, p, false) >= 3 ? firstReal(d) : -1;
            out = one(has(d, s) ? s : -1);
            return true;
        }
        case Ab::L1Z1XHero: {
            if (d.maxPick > 1 || d.minPick != 1 || d.prompt.find("which ships") != std::string::npos) {  // which ships: all of them
                out = Answer();
                // (the L1Z1X review, checkpoint 2 - test knob helmshome, OFF) the home fleet stays with enemy ships within 2.
                // Tested (90 games): 18 wins / 7.01 VP against 17 / 7.04 - the home was attacked 4 times in 90 games without
                // it (once within a round of The Helmsman): nothing to fix
                const int home = me.homeSystem;
                bool guard = false;
                if (aiTune("helmshome", 0.f) > 0.f && home >= 0)
                    for (int x : systemsWithin(g, home, 2))
                        if (otherShipsIn(g, p, x)) guard = true;
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0 && !(guard && o.id / 256 == home)) out.picks.push_back(o.id);
                if (out.picks.empty())
                    for (const Option& o : d.options)
                        if (o.enabled && o.id >= 0) out.picks.push_back(o.id);
                return true;
            }
            // (the L1Z1X review, checkpoint 2 - test knob helmsodds, OFF) the system next to the planets the gathered fleet can
            // take: each neighbour's enemy planets x the odds against the ships there. Tested (90 games): 15 wins / 6.92 VP
            // against 17 / 7.04 (both knobs 16 / 6.84); today's choice already wins 40 of the 52 attacks that follow The
            // Helmsman (9 more end in a retreat, 3 lost)
            if (aiTune("helmsodds", 0.f) > 0.f) {
                Force fleet;
                for (const SystemState& sy : g.systems) {
                    fleet.add(UnitType::Dreadnought, sy.space[size_t(p)].count(UnitType::Dreadnought));
                    fleet.add(UnitType::Flagship, sy.space[size_t(p)].count(UnitType::Flagship));
                }
                int s = bestBy(d, [&](int id) {
                    float v = 0;
                    for (int16_t a : g.systems[id].adj) {
                        float worth = 0;
                        int owner = -1;
                        for (int16_t x : g.systems[a].planets)
                            if (g.planets[x].owner >= 0 && g.planets[x].owner != p) worth += planetWorth(g, x), owner = g.planets[x].owner;
                        float odds = 1.f;
                        for (int q = 0; q < g.numPlayers; ++q)
                            if (q != p && g.systems[a].space[size_t(q)].nonFighterShips() > 0)
                                odds = std::min(odds, fightOdds(g, p, fleet, q, g.systems[a].space[size_t(q)], true));
                        if (owner >= 0 || odds < 1.f) v += worth * odds - (odds < 0.4f ? 2.f : 0.f);
                    }
                    return v;
                });
                out = one(s >= 0 ? s : -1);
                return true;
            }
            int s = bestBy(d, [&](int id) {
                float v = 0;
                for (int16_t a : g.systems[id].adj)
                    for (int16_t x : g.systems[a].planets)
                        if (g.planets[x].owner >= 0 && g.planets[x].owner != p) v += planetWorth(g, x);
                return v;
            });
            out = one(s >= 0 ? s : -1);
            return true;
        }
        case Ab::Ambush: {
            out = Answer();
            for (const Option& o : d.options)
                if (int(out.picks.size()) < d.maxPick) out.picks.push_back(o.id);
            return true;
        }
        case Ab::Pillage: out = one(has(d, 0) ? 0 : has(d, 1) ? 1 : -1); return true;
        case Ab::SalvageOps: {
            int t = bestBy(d, [&](int id) { return float(unitCost2(g, p, UnitType(id))); });
            out = one(t >= 0 && spendable(g, p, false) >= unitCost2(g, p, UnitType(t)) / 2 + 2 ? t : -1);
            return true;
        }
        case Ab::MentakHero:
            // (the Mentak review, checkpoint 2 - test knob ipsodds, default off) only in a fight we expect to win against 2+
            // non-fighter ships (the ships come from the opponent's losses), or late in the game against anything. Tested
            // (ipsodds=0.55): used in 61 games of 90 against 75, 1.05 ships a use against 0.85, but 15 wins / 7.03 VP against
            // 16 / 7.04 - OFF
            if (aiTune("ipsodds", 0.f) > 0.f && g.activeSystem >= 0) {
                int q = -1;
                for (int k = 0; k < kSides; ++k)
                    if (k != p && hasShips(g, k, g.activeSystem)) q = k;
                const float odds = q >= 0 ? fightOdds(g, p, g.systems[size_t(g.activeSystem)].space[size_t(p)], q,
                                                      g.systems[size_t(g.activeSystem)].space[size_t(q)], true)
                                          : 0.f;
                out = yes((enemyNonFighters(g, p, g.activeSystem) >= 2 && odds >= aiTune("ipsodds", 0.f)) || (g.round >= 7 && q >= 0));
                return true;
            }
            out = yes(enemyNonFighters(g, p, g.activeSystem) >= 3 || g.round >= 6);
            return true;
        case Ab::Foresight: {
            if (d.prompt.find("take which ground forces") != std::string::npos) {  // (fixes 27) the defenders stay
                out = Answer();
                return true;
            }
            if (!outgunned(g, p) || g.activeSystem < 0) {
                out = one(-1);
                return true;
            }
            int to = bestBy(d, [&](int id) { return hasUnits(g, p, id) ? 1.f : 0.f; });
            out = one(to >= 0 ? to : firstReal(d));
            return true;
        }
        case Ab::Indoctrination: {
            int c = has(d, 1) && spendable(g, p, true) >= 6 ? 1 : spendable(g, p, true) >= 4 ? 0 : -1;
            if (c == 0 && !has(d, 0)) c = has(d, 1) && spendable(g, p, true) >= 5 ? 1 : -1;  // (fixes 26: no infantry left - the mech)
            out = one(c);
            return true;
        }
        case Ab::YinCommander: {  // (fixes 26: asked of a person - as the bots' own pick, the planet with the most infantry)
            int x = bestBy(d, [&](int id) {
                if (id >= 20000) return id - 20000 < int(g.systems.size()) ? 0.5f * float(g.systems[size_t(id - 20000)].space[size_t(p)].count(UnitType::Infantry)) : -1.f;
                return id < int(g.planets.size()) ? float(g.planets[size_t(id)].units[size_t(p)].count(UnitType::Infantry)) : -1.f;
            });
            out = one(x >= 0 ? x : firstReal(d));
            return true;
        }
        case Ab::Devotion:
            if (d.prompt.find("takes the hit") != std::string::npos) {
                // (fixes 26) the Yin choose the ship: the costliest one the hit destroys (a damaged ship, or one that can't
                // sustain damage), else one that has to use up its sustain
                const int sys = g.activeSystem;
                int victim = -1;
                for (int k = 0; k < kSides && sys >= 0; ++k)
                    if (k != p && (k < g.numPlayers || k == kNeutral) && g.systems[size_t(sys)].space[size_t(k)].ships() > 0) victim = k;
                int x = bestBy(d, [&](int id) {
                    const int u = id >= 64 ? id - 64 : id;
                    if (u < 0 || u >= kUnitTypes || victim < 0) return 0.f;
                    const bool damaged = id >= 64 || g.systems[size_t(sys)].space[size_t(victim)].dmg[u] >= g.systems[size_t(sys)].space[size_t(victim)].n[u];
                    const bool sustains = !damaged && victim < g.numPlayers && unitSustainsAt(g, victim, UnitType(u), sys, -1);
                    const float cost = float(victim < g.numPlayers ? unitCost2(g, victim, UnitType(u)) : neutralUnitDef(UnitType(u)).cost2) * 0.5f;
                    return sustains ? 0.4f + cost * 0.1f : cost + (u == int(UnitType::Carrier) ? 1.f : 0.f);
                });
                out = one(x >= 0 ? x : firstReal(d));
                return true;
            }
            [[fallthrough]];
        case Ab::ImpulseCore: {
            int sys = g.activeSystem;
            bool worth = enemyNonFighters(g, p, sys) >= 1 && (outgunned(g, p) || ab == Ab::ImpulseCore);
            int t = has(d, int(UnitType::Destroyer)) ? int(UnitType::Destroyer) : has(d, int(UnitType::Cruiser)) ? int(UnitType::Cruiser) : -1;
            // (the Yin review, checkpoint 2 - test knob devfree) since fixes 26 the Yin choose the ship Devotion hits: trade a
            // destroyer (or a cruiser) for a costlier ship the hit destroys outright (a damaged one, or one that can't
            // sustain) - not only when outgunned. Tested OFF: Devotion 29 uses in 90 games against 22, but 17 wins / 7.07 VP
            // against 19 / 7.06
            if (ab == Ab::Devotion && !worth && t >= 0 && g.fixes >= 26 && aiTune("devfree", 0.f) > 0.f && sys >= 0) {
                float best = 0.f;
                for (int q = 0; q < kSides; ++q) {
                    if (q == p || (q >= g.numPlayers && q != kNeutral)) continue;
                    const Force& of = g.systems[size_t(sys)].space[size_t(q)];
                    for (int u : kShipIdx) {
                        if (!of.n[u] || u == int(UnitType::Fighter)) continue;
                        const bool dies = of.dmg[u] > 0 || q == kNeutral || !unitSustainsAt(g, q, UnitType(u), sys, -1);
                        const float c = float(q < g.numPlayers ? unitCost2(g, q, UnitType(u)) : neutralUnitDef(UnitType(u)).cost2);
                        if (dies) best = std::max(best, c);
                    }
                }
                worth = best > float(unitCost2(g, p, UnitType(t)));
            }
            out = one(worth ? t : -1);
            return true;
        }
        case Ab::YinHero: {
            int x = bestBy(d, [&](int id) {
                if (g.planets[size_t(id)].owner == p) return -1.f;  // (fixes 26 offers the Yin's own planets: bots invade as before)
                int def = 0;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p) def += g.planets[id].units[q].groundForces();
                return def <= 1 ? planetWorth(g, id) - float(def) * 2.f : -1.f;
            }, 0.5f);
            out = one(x >= 0 ? x : -1);
            return true;
        }
        case Ab::StallTactics: {
            int c = bestBy(d, [&](int id) { return 5.f - aiCardKeepValue(id); });
            out = one(c >= 0 ? c : -1);
            return true;
        }
        case Ab::MageonImplants:
        case Ab::SpyNet: {
            if (yn) {
                out = yes(true);
                return true;
            }
            if (d.prompt.find("whose") != std::string::npos) {
                int q = bestBy(d, [&](int id) { return float(g.players[id].actionCards.size()); });
                out = one(q >= 0 ? q : -1);
                return true;
            }
            int c = bestBy(d, [&](int id) { return aiCardKeepValue(id); });
            out = one(c >= 0 ? c : firstReal(d));
            return true;
        }
        case Ab::YssarilHero: {
            if (d.prompt.find("Kyver: show") != std::string::npos) {
                int c = bestBy(d, [&](int id) { return 5.f - aiCardKeepValue(id); });
                out = one(c >= 0 ? c : firstReal(d));
                return true;
            }
            out = one(numberBefore(d.options.size() > 1 ? d.options[1].detail : std::string(), " action cards") >= 5 ? 0 : 1);
            return true;
        }
        // ---------------- batch 3
        case Ab::WormholeGenerator:
        case Ab::CreussIff:
        case Ab::IcarusDrive: {
            if (ab == Ab::IcarusDrive && g.fixes >= 24 && d.prompt.find("Icarus Drive: remove a mech") != std::string::npos) {
                // (fixes 24) which mech, if any: the best token for its system against what the mech is worth here
                int best = -1;
                float bv = aiTune("icarus", 0.f);
                for (const Option& o : d.options) {
                    if (o.id < 0) continue;
                    const int s = o.id / 16, slot = o.id % 16;
                    float gain = -9.f;
                    for (int k = 0; k < 3; ++k) gain = std::max(gain, icarusGain(g, p, s, k));
                    const float v = gain - icarusMechCost(g, p, s, slot);
                    if (v > bv) bv = v, best = o.id;
                }
                out = one(best >= 0 ? best : -1);
                return true;
            }
            if (ab == Ab::IcarusDrive && g.fixes >= 24 && d.prompt.find("Which Creuss") != std::string::npos) {
                int k = -1;
                float kv = -1e9f;
                for (int i = 0; i < 3; ++i)
                    if (has(d, i) && icarusGain(g, p, d.value, i) > kv) kv = icarusGain(g, p, d.value, i), k = i;
                out = one(k >= 0 ? k : firstReal(d));
                return true;
            }
            if (yn && ab == Ab::CreussIff && g.fixes >= 24) {
                // (fixes 24) play the note when a token placement is worth something to us - or anyway from round 6
                float gain = -9.f;
                for (int k = 0; k < 3; ++k) {
                    float best = -9.f;
                    for (size_t s = 0; s < g.systems.size(); ++s) best = std::max(best, creussSpot(g, p, int(s), k));
                    const float now = g.creussTokens[k] >= 0 ? creussSpot(g, p, g.creussTokens[k], k) : -1.f;
                    gain = std::max(gain, best - now);
                }
                out = yes(gain >= aiTune("iff", 2.f) || g.round >= 6);
                return true;
            }
            if (yn) {
                if (ab == Ab::IcarusDrive) {
                    bool offBoard = false;
                    for (int16_t t : g.creussTokens)
                        if (t < 0) offBoard = true;
                    out = yes(offBoard && g.activeSystem >= 0 && !wormholesOf(g, g.activeSystem));
                } else {
                    out = yes(true);
                }
                return true;
            }
            if (d.prompt.find("Which Creuss") != std::string::npos) {
                // the token that gains most by a new place (one not on the board yet, or one badly placed)
                int k = -1;
                float kv = -1e9f;
                for (int i = 0; i < 3; ++i) {
                    if (!has(d, i)) continue;
                    float best = -9.f;
                    for (size_t s = 0; s < g.systems.size(); ++s) best = std::max(best, creussSpot(g, p, int(s), i));
                    const float now = g.creussTokens[i] >= 0 ? creussSpot(g, p, g.creussTokens[i], i) : -1.f;
                    if (best - now > kv) kv = best - now, k = i;
                }
                // (0.44.9: the original Wormhole Generator in the status phase - only when a token gains by it)
                if (g.phase == Phase::Status && kv < 0.5f && has(d, -1)) k = -1;
                out = one(k >= 0 ? k : (g.phase == Phase::Status && has(d, -1) ? -1 : firstReal(d)));
                return true;
            }
            const int k = d.prompt.find("alpha") != std::string::npos ? 0 : d.prompt.find("beta") != std::string::npos ? 1 : 2;
            int s = bestBy(d, [&](int id) { return creussSpot(g, p, id, k); });
            out = one(s >= 0 ? s : firstReal(d));
            return true;
        }
        case Ab::CreussHero: {
            if (g.fixes >= 24) {  // (the same swap its value was worked out for)
                std::vector<int> cands;
                for (const Option& o : d.options)
                    if (o.id >= 0) cands.push_back(o.id);
                int a = -1, b = -1;
                meianBest(g, p, cands, &a, &b);
                if (a >= 0 && b >= 0) {
                    out = Answer();
                    out.picks = {a, b};
                    return true;
                }
            }
            // bring the strongest fleet next to the most valuable targets
            int a = -1, best = 0;
            for (const Option& o : d.options) {
                int v = combatStrength(g, p, o.id, true);
                if (v > best) best = v, a = o.id;
            }
            int b = -1;
            float bv = 0;
            for (const Option& o : d.options) {
                if (o.id == a) continue;
                float v = targetValue(g, p, o.id) - (a >= 0 ? targetValue(g, p, a) : 0.f);
                if (v > bv) bv = v, b = o.id;
            }
            out = Answer();
            if (a >= 0 && b >= 0) {
                out.picks = {a, b};
            } else {
                for (const Option& o : d.options)
                    if (out.picks.size() < 2) out.picks.push_back(o.id);
            }
            return true;
        }
        case Ab::CreussCommander: {  // (fixes 24: how many fighters - all of them; old games: yes)
            if (yn) break;
            int best = -1;
            for (const Option& o : d.options)
                if (o.enabled && o.id > best) best = o.id;
            out = one(best >= 0 ? best : firstReal(d));
            return true;
        }
        case Ab::CreussAgent: {
            // Emissary Taivra on our own activation (fixes 24): only when it lets more of our ships in - and not when
            // the new adjacency mostly helps others (their deep space cannon in wormhole systems now in range)
            if (g.fixes >= 24 && g.activePlayer != p) {  // (another player's activation, nothing promised - a seat a person
                out = one(0);                            // plays, answered by a bot in tests: no)
                return true;
            }
            if (!yn || g.fixes < 24 || g.activeSystem < 0) break;
            const int sys = g.activeSystem;
            Game h = g;
            h.fx.taivra = true;
            float extra = 0.f;
            for (size_t x = 0; x < g.systems.size(); ++x) {
                if (int(x) == sys || g.systems[x].hasToken(p)) continue;
                for (int t : kShipIdx) {
                    const int k = g.systems[x].space[size_t(p)].n[size_t(t)];
                    if (!k || UnitType(t) == UnitType::Fighter) continue;
                    if (shipReach(g, p, int(x), sys, UnitType(t), moveBonusMax(g, p)).ok) continue;
                    if (shipReach(h, p, int(x), sys, UnitType(t), moveBonusMax(h, p)).ok)
                        extra += k * std::max(1.f, unitDef(g, p, UnitType(t)).cost2 * 0.5f);
                }
            }
            float risk = 0.f;
            for (size_t w = 0; w < g.systems.size(); ++w) {
                if (int(w) == sys || !wormholesOf(g, int(w)) || adjacentSystems(g, sys, int(w))) continue;
                for (int q = 0; q < g.numPlayers; ++q) {
                    if (q == p || !unitDef(g, q, UnitType::PDS).deepCannon) continue;
                    int n = g.systems[w].space[size_t(q)].count(UnitType::PDS);
                    for (int16_t x : g.systems[w].planets) n += g.planets[size_t(x)].units[size_t(q)].count(UnitType::PDS);
                    risk += float(n);
                }
            }
            const bool fight = otherShipsIn(g, p, sys);
            out = yes(extra >= 1.5f && extra * (fight ? 0.8f : 0.5f) > risk * 1.2f + aiTune("taivra", 0.5f));
            return true;
        }
        case Ab::DimensionalSplicer: {
            if (yn) {
                // "before Assault Cannon?" (fixes 24 games: the combat comes with the question) - the opponent gives up
                // its cheapest non-fighter ship to the cannon; the Splicer first can take that one and leave the cannon a
                // dearer ship. Both orders are compared (before: always the cannon first)
                const int sys = d.value, o = d.targetPlayer;
                if (g.fixes < 24 || sys < 0 || sys >= int(g.systems.size()) || o < 0 || o >= kSides) {
                    out = yes(false);
                    return true;
                }
                auto splice = [&](const Force& f, int t, bool* kills) {
                    const bool sus = unitSustains(g, o, UnitType(t)) && f.dmg[t] < f.n[t];
                    const float c = float(unitDef(g, o, UnitType(t)).cost2);
                    if (kills) *kills = !sus;
                    return (sus ? 0.3f * c : c) + 1.f;
                };
                auto cheapest = [&](const Force& f) {
                    int best = -1;
                    float bc = 1e9f;
                    for (int t : kShipIdx)
                        if (t != int(UnitType::Fighter) && f.n[t] && float(unitDef(g, o, UnitType(t)).cost2) < bc)
                            bc = float(unitDef(g, o, UnitType(t)).cost2), best = t;
                    return best;
                };
                const Force& f = g.systems[size_t(sys)].space[size_t(o)];
                float cannonFirst = 0.f, splicerFirst = 0.f;
                if (const int x = cheapest(f); x >= 0) {
                    Force h = f;
                    h.remove(UnitType(x), 1, false);
                    float s = 0.f;
                    for (int t : kShipIdx)
                        if (h.n[t]) s = std::max(s, splice(h, t, nullptr));
                    cannonFirst = float(unitDef(g, o, UnitType(x)).cost2) + s;
                }
                for (int t : kShipIdx) {
                    if (!f.n[t]) continue;
                    bool kills = false;
                    const float s = splice(f, t, &kills);
                    Force h = f;
                    if (kills) h.remove(UnitType(t), 1, false);
                    const int y = cheapest(h);
                    splicerFirst = std::max(splicerFirst, s + (y >= 0 ? float(unitDef(g, o, UnitType(y)).cost2) : 0.f));
                }
                out = yes(splicerFirst > cannonFirst + 0.25f);
                return true;
            }
            // (0.44.8) the combat's system and opponent come with the question (neutral units too); a hit that a
            // Sustain Damage would just absorb is worth little
            int t = bestBy(d, [&](int id) {
                if (id < 0) return -1.f;
                int sys = d.value >= 0 && d.value < int(g.systems.size()) ? d.value : (g.activeSystem >= 0 ? g.activeSystem : 0);
                int o = d.targetPlayer >= 0 && d.targetPlayer < kSides ? d.targetPlayer : -1;
                for (int q = 0; q < g.numPlayers && o < 0; ++q)
                    if (q != p && g.systems[sys].space[q].n[id]) o = q;
                if (o < 0) return 0.f;
                const Force& f = g.systems[sys].space[o];
                bool canSustain = unitSustains(g, o, UnitType(id)) && f.dmg[id] < f.n[id];
                const float c = float(unitDef(g, o, UnitType(id)).cost2);
                float v = (canSustain ? 0.3f * c : c) + 1.f;
                // (fixes 24) our Assault Cannon fires next: the opponent then gives up its cheapest non-fighter ship
                if (d.context.find("Assault Cannon fires next") != std::string::npos) {
                    Force h = f;
                    if (!canSustain) h.remove(UnitType(id), 1, false);
                    float bc = 0.f;
                    bool any = false;
                    for (int u : kShipIdx)
                        if (u != int(UnitType::Fighter) && h.n[u]) {
                            const float cu = float(unitDef(g, o, UnitType(u)).cost2);
                            if (!any || cu < bc) bc = cu, any = true;
                        }
                    v += bc;
                }
                return v;
            });
            out = one(t >= 0 ? t : firstReal(d));
            return true;
        }
        case Ab::StarForge: {
            if (d.prompt.find("place what") != std::string::npos) {
                int sys = -1;
                for (size_t s = 0; s < g.systems.size(); ++s)
                    if (g.systems[s].space[p].count(UnitType::WarSun)) sys = int(s);
                int room = sys >= 0 ? shipCapacity(g, p, sys) - g.systems[sys].space[p].count(UnitType::Fighter) -
                                          g.systems[sys].space[p].groundForces()
                                    : 0;
                int t = room >= 2 && has(d, int(UnitType::Fighter)) ? int(UnitType::Fighter)
                        : has(d, int(UnitType::Destroyer))          ? int(UnitType::Destroyer)
                                                                    : firstReal(d);
                out = one(t);
                return true;
            }
            out = one(bestBy(d, [&](int id) { return targetValue(g, p, id); }));
            return true;
        }
        case Ab::MuaatHero: {
            int sys = g.activeSystem;
            bool mine = false, theirs = false;
            if (sys >= 0) {
                for (int16_t x : g.systems[sys].planets) {
                    if (g.planets[x].owner == p) mine = true;
                    else if (g.planets[x].owner >= 0) theirs = true;
                }
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p && hasUnits(g, q, sys)) theirs = true;
            }
            out = yes(theirs && !mine);
            return true;
        }
        case Ab::SaarCommander: {  // send the new units to the dock with enemy ships next to it
            auto threat = [&](int s) {
                int n = 0;
                for (int16_t a : g.systems[s].adj)
                    if (otherShipsIn(g, p, a)) ++n;
                return n;
            };
            int here = g.activeSystem >= 0 ? threat(g.activeSystem) : 0;
            int c = bestBy(d, [&](int id) { return float(threat(id / 1000)); }, float(here));
            out = one(c >= 0 && here == 0 ? c : -1);
            return true;
        }
        case Ab::SaarCommanderCount: {  // (fixes 27) Rowl Sarrig, dock by dock: all of them to the dock chosen (as before)
            int most = 0;
            for (const Option& o : d.options)
                if (o.enabled && o.id > most) most = o.id;
            out = one(most);
            return true;
        }
        case Ab::SaarHero: {
            int s = bestBy(d, [&](int id) { return float(enemyLight(g, p, id)); });
            out = one(s >= 0 ? s : -1);
            return true;
        }
        case Ab::ChaosMapping: {
            // (0.44.7) home, or - once the floating factories have left it - the factory facing the most enemy ships
            if (spendable(g, p, false) < 3) {
                out = one(-1);
                return true;
            }
            const int home = me.homeSystem;
            int s = bestBy(d, [&](int id) {
                if (id < 0 || id >= int(g.systems.size())) return -1.f;
                float v = id == home ? 1.f : 0.5f;
                std::vector<int> zone{id};
                for (int16_t a : g.systems[size_t(id)].adj) zone.push_back(a);
                for (int z : zone)
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p) v += 0.5f * float(g.systems[size_t(z)].space[size_t(q)].nonFighterShips());
                return v;
            }, 0.f);
            out = one(s);
            return true;
        }
        case Ab::WinnuAgent: out = yes(spendable(g, p, false) >= 2); return true;
        case Ab::Acquiescence:
            if (!yn) {  // (fixes 26) the original card at the end of the strategy phase: a card the Winnu chose, if better
                int best = -1;
                float bv = 1.0f;
                for (const Option& o : d.options) {
                    if (o.id < 0) continue;
                    const float gain = aiStrategyCardValue(g, p, o.id % 100) - aiStrategyCardValue(g, p, o.id / 100);
                    if (gain > bv) bv = gain, best = o.id;
                }
                out = one(best);
                return true;
            }
            out = yes(true);
            return true;
        case Ab::WinnuHero: {
            if (d.maxPick > 1 || d.minPick == 0) {  // (fixes 26) who may use the secondary: the players we're friendly with
                out = Answer();
                for (const Option& o : d.options)
                    if (o.id >= 0 && o.id < g.numPlayers && attitudeOf(g, p, o.id) == Attitude::Friendly &&
                        int(out.picks.size()) < d.maxPick)
                        out.picks.push_back(o.id);
                return true;
            }
            static const float kPrimary[8] = {1.0f, 0.6f, 1.0f, 0.9f, 0.7f, 0.9f, 1.4f, 1.3f};
            // (the Winnu review, checkpoint 2 - test knob mathispick) the card worth most to us now (aiStrategyCardValue)
            // instead of the fixed table - it was Technology in 76 of 76 uses. Tested (90 games): Imperial 52 times, then
            // Leadership 8, Technology 7; 12 wins / 6.58 VP against 4 / 6.10, fresh seeds 14 / 7.11 against 11 / 6.92, without
            // Thunder's Edge 13 / 6.81 against 15 / 6.93. Owner 2026-11-12 21:46 ("Lets go ahead and turn on those changes"): ON
            const bool byValue = aiTune("mathispick", 1.f) > 0.f && aiLevelScale(g) > 0.f;
            int c = bestBy(d, [&](int id) { return id >= 0 && id < 8 ? (byValue ? aiStrategyCardValue(g, p, id) : kPrimary[id]) : 0.f; });
            out = one(c >= 0 ? c : firstReal(d));
            return true;
        }
        case Ab::GalacticThreat: {
            float conf = 0;
            int a = d.agenda >= 0 ? d.agenda : g.currentAgenda;
            int o = a >= 0 ? aiPredictOutcome(g, a, &conf) : -1;
            out = one(has(d, o) ? o : firstReal(d));
            return true;
        }
        case Ab::TechSingularity: {
            int t = bestBy(d, [&](int id) {
                if (id >= 30000) {  // Valefar Assimilator Z: that faction's flagship abilities on ours
                    const char* k = kFactions[id - 30000].key;
                    float v = 2.5f;
                    for (const char* strong : {"sardakk", "winnu", "jolnar", "letnev", "mahact", "l1z1x", "naaz", "argent"})
                        if (!std::strcmp(k, strong)) v = 3.8f;
                    return v * (unitsOnBoard(g, p, UnitType::Flagship) > 0 ? 1.f : 0.7f);
                }
                float v = aiTechWant(g, p, id % 10000) + (id >= 10000 ? 1.f : 0.f);
                // 0.43.0 (owner: "verify the Nekro steal with Valefar"): moving a placed token gives that technology
                // up - X was moved again and again while Y stayed unused
                if (id >= 10000) {
                    const int cur = id >= 20000 ? me.assimY : me.assimX;
                    if (cur >= 0) v -= aiTechWant(g, p, cur) + 0.5f;
                }
                return v;
            });
            out = one(t >= 0 ? t : -1);
            return true;
        }
        case Ab::NekroAgent: {
            auto worth = [&](int id) {
                if (id == 1000) return me.tactic >= 3 ? 1.f : -1.f;
                if (id >= 1000) return -1.f;
                return 3.f - aiCardKeepValue(id);
            };
            // (fixes 27) "choose a player": the bot picks itself, when it has something to give up as before (a deal to lend it
            // is for later); nobody otherwise
            if (d.prompt.find("choose a player") != std::string::npos) {
                bool self = false;
                for (const Option& o : d.options) self = self || o.id == p;
                float best = 0.f;
                if (self) {
                    if (me.tactic > 0) best = std::max(best, worth(1000));
                    for (int16_t c : me.actionCards) best = std::max(best, worth(c));
                }
                out = one(self && best > 0.f ? p : -1);
                return true;
            }
            int c = bestBy(d, worth, 0.f);
            out = one(c);
            return true;
        }
        case Ab::Alastor: {
            if (d.prompt.find("how many") == std::string::npos) break;  // (the old yes / no: yes)
            // (fixes 27) survivors back to their planet: as many as can; the space area's ground forces: all of them (as the
            // old yes); a planet's: none (as before - checkpoint 2 looks at it)
            int best = -1, most = -1, least = 1 << 20;
            for (const Option& o : d.options)
                if (o.enabled && o.id >= 0) most = std::max(most, o.id), least = std::min(least, o.id);
            best = d.prompt.find("go back") != std::string::npos || d.value < 0 ? most : least;
            // (checkpoint 2 - switch alastorplanet, ON - owner 2026-11-14: keep it on, it may help define a better strategy
            // later) a planet's ground forces join too, keeping as many on it as the other players have ground forces in the
            // space area (who could invade it after the combat). Test, 90 games: The Alastor fights 0.09 -> 0.13 a game, 13
            // wins / 6.54 VP either way. alastorplanet=0 turns it off
            if (aiTune("alastorplanet", 1.f) > 0.f && d.value >= 0 && d.value < int(g.planets.size()) && d.prompt.find("go back") == std::string::npos) {
                const int sys = g.planets[size_t(d.value)].system;
                int invaders = 0;
                for (int q = 0; q < kSides && sys >= 0; ++q)
                    if (q != p) invaders += g.systems[size_t(sys)].space[size_t(q)].groundForces();
                best = std::max(least, most - invaders);
            }
            out = one(best >= 0 ? best : firstReal(d));
            return true;
        }
        case Ab::NekroHero: {
            if (d.prompt.find("gain which") != std::string::npos) {
                int t = bestBy(d, [&](int id) { return aiTechWant(g, p, id); });
                out = one(t >= 0 ? t : firstReal(d));
                return true;
            }
            int x = bestBy(d, [&](int id) { return planetWorth(g, id); });
            out = one(x >= 0 ? x : -1);
            return true;
        }
        case Ab::Aetherstream: {  // (0.44.7) ours: always; a neighbour's: as with Aetherpassage - behind us, not coming at us
            const int a = d.targetPlayer;
            if (a < 0 || a == p) {
                out = yes(true);
                return true;
            }
            out = yes(g.players[size_t(a)].vp < me.vp && !hostileFlag(g, p, a) && (g.activeSystem < 0 || !hasUnits(g, p, g.activeSystem)));
            return true;
        }
        case Ab::Aetherpassage: {
            int a = g.activePlayer;
            bool ok = a >= 0 && g.players[a].vp < me.vp && (g.activeSystem < 0 || !hasUnits(g, p, g.activeSystem));
            out = yes(ok);
            return true;
        }
        // (fixes 26, the Empyrean review: any player's unit) bots repair only their own
        case Ab::Dynamo: out = yes(spendable(g, p, true) >= 4 && d.prompt.find("repair your ") != std::string::npos); return true;
        case Ab::EmpyreanAgent:  // (fixes 26) Acamar for another player: only a promised one (ai_deals) - a person's seat in tests: no
            if (d.targetPlayer >= 0 && d.targetPlayer != p) {
                out = one(0);
                return true;
            }
            break;
        case Ab::Watcher: out = yes(d.card >= 0 && aiWantsCancel(g, p, d.card, d.targetPlayer)); return true;
        // ---------------- batch 4
        case Ab::RaidFormation:
        case Ab::Vortex: {
            int t = -1;
            if (ab == Ab::RaidFormation) {
                int o = -1;  // the opponent in the active combat
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p && g.activeSystem >= 0 && hasShips(g, q, g.activeSystem)) o = q;
                t = bestBy(d, [&](int id) { return o >= 0 ? float(unitCost2(g, o, UnitType(id))) : 0.f; });
            } else {
                t = bestBy(d, [&](int id) { return id < 0 ? -1.f : vortexWorth(g, p, id / 16, id % 16); });
            }
            out = one(t >= 0 ? t : firstReal(d));
            return true;
        }
        case Ab::ArgentAgent: {
            // (fixes 26, the Argent review) Trilossa Aun Mirik for another player: only a promised use (answered in ai_deals)
            // - a person's seat in tests: no
            if (d.targetPlayer >= 0 && d.targetPlayer != p) {
                out = one(0);
                return true;
            }
            if (yn) {
                out = yes(true);
                return true;
            }
            if (d.prompt.find("how many") != std::string::npos) {
                int best = -1, bk = 0;
                for (const Option& o : d.options)
                    if (o.id % 16 > bk) bk = o.id % 16, best = o.id;
                out = one(best >= 0 ? best : firstReal(d));
                return true;
            }
            int x = bestBy(d, [&](int id) { return defendScore(g, p, id); });
            out = one(x >= 0 ? x : -1);
            return true;
        }
        case Ab::ArgentHero: {
            if (d.maxPick > 1 || d.minPick != 1) {  // which ships: everything not guarding home
                out = Answer();
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0 && g.systems[o.id / 16].home != p) out.picks.push_back(o.id);
                return true;
            }
            for (const Option& o : d.options)
                if (o.id < 0 && o.label == "Done") {
                    out = one(-1);
                    return true;
                }
            // (0.43.0: for Raise a Fleet / Command an Armada - where most of the fleet already is)
            const bool fleetGoal = aiLevelScale(g) > 0.f && aiGoals(g, p).fleetTarget > 0;
            int s = bestBy(d, [&](int id) { return fleetGoal ? float(g.systems[id].space[p].nonFighterShips()) + targetValue(g, p, id) * 0.01f
                                                             : targetValue(g, p, id); });
            out = one(s >= 0 ? s : -1);
            return true;
        }
        case Ab::MahactAgent: {
            int s = me.strategy <= 2 ? firstReal(d) : -1;
            out = one(has(d, s) ? s : firstReal(d));
            return true;
        }
        case Ab::MahactHero: {
            float v = 0;
            int c = bestHurl(g, p, &v);
            out = one(has(d, c) ? c : -1);
            return true;
        }
        case Ab::CrimsonLegionnaire: out = one(has(d, 1) ? 1 : 0); return true;
        case Ab::GeneticRecombination: {
            const int a = d.agenda >= 0 ? d.agenda : g.currentAgenda;
            if (d.prompt.find(": make ") != std::string::npos) {  // Mahact: which outcome to force
                const int voter = d.value;
                int o = -1;
                float bv = 1.0f;
                if (voter >= 0 && voter < g.numPlayers && availableVotes(g, voter) >= 3 && a >= 0)
                    for (const Option& op : d.options) {
                        if (op.id == kNoVote) continue;
                        float v = aiOutcomeValue(g, p, a, op.id);
                        if (v > bv) bv = v, o = op.id;
                    }
                out = one(o >= 0 ? o : kNoVote);
                return true;
            }
            // the voter: vote for it unless it hurts more than a fleet token
            bool vote = a < 0 || aiOutcomeValue(g, p, a, d.value) >= -1.5f || !has(d, 0);
            out = one(vote ? 1 : 0);
            return true;
        }
        case Ab::Scepter: {
            int s = bestBy(d, [&](int id) {
                float v = 0;
                for (int16_t x : g.systems[id].planets)
                    if (g.planets[x].owner == p) v += planetWorth(g, x);
                return v;
            });
            out = one(s >= 0 ? s : -1);
            return true;
        }
        case Ab::DistantSuns: {
            int c = bestBy(d, [&](int id) {
                switch (kExploreCards[id].kind) {
                    case ExploreKind::Attach: return 3.f;
                    case ExploreKind::Fragment: return 2.5f;
                    case ExploreKind::Instant: return 2.f;
                    default: return 1.f;
                }
            });
            out = one(c >= 0 ? c : firstReal(d));
            return true;
        }
        case Ab::Fabrication:
        case Ab::BlackMarketForgery: {
            int c = -1;
            for (const Option& o : d.options)
                if (o.id >= 0 && o.id < 10 && c < 0) c = o.id;
            if (c < 0 && has(d, 10)) c = 10;
            out = one(c);
            return true;
        }
        case Ab::SaarAgent:
            // (fixes 27, the Saar review) Captain Mendosa for another player: only a promised use (answered in ai_deals) - a
            // person's seat in tests: no; their own activation: yes (asked only when a ship reaches the system with it)
            out = yes(d.prompt.find("Exhaust Captain Mendosa: ") != 0);
            return true;
        case Ab::NaazAgent: {
            // (fixes 27, the Naaz-Rokha review) for another player: only a promised use (answered in ai_deals) - a person's
            // seat in tests: no
            if (d.prompt.find("Exhaust Garv and Gunn: ") == 0) {
                out = one(0);
                return true;
            }
            int x = bestBy(d, [&](int id) { return planetWorth(g, id) + (g.planets[id].units[p].count(UnitType::Mech) ? 3.f : 0.f); });
            out = one(x >= 0 ? x : -1);
            return true;
        }
        case Ab::NaazHero: {
            static const float kSec[8] = {0.6f, 0.3f, 0.6f, 0.8f, 0.5f, 0.5f, 1.0f, 0.7f};
            std::vector<std::pair<float, int>> v;
            for (const Option& o : d.options)
                if (o.enabled && o.id >= 0 && o.id < 8) v.push_back({-(kSec[o.id] + (o.id == 6 && spendable(g, p, false) >= 4 ? 0.5f : 0.f)), o.id});
            std::sort(v.begin(), v.end());
            out = Answer();
            for (auto& x : v)
                if (int(out.picks.size()) < d.maxPick) out.picks.push_back(x.second);
            return true;
        }
        case Ab::Supercharge: {
            // (0.44.8) a close fight - neutral fleets count; on the ground only against 2+ defenders or a close fight
            int sys = g.activeSystem;
            bool close = true;
            if (sys >= 0 && g.groundCombatPlanet < 0) {
                int mine = combatStrength(g, p, sys, true), theirs = 0;
                for (int q = 0; q < kSides; ++q)
                    if (q != p && (q < g.numPlayers || q == kNeutral)) theirs = std::max(theirs, combatStrength(g, q, sys, true));
                close = theirs * 10 >= mine * 4;
            } else if (g.groundCombatPlanet >= 0) {
                const int x = g.groundCombatPlanet;
                int mine = groundForcesOn(g, p, x), theirs = 0;
                for (int q = 0; q < kSides; ++q)
                    if (q != p) theirs += groundForcesOn(g, q, x);
                close = theirs >= 2 || theirs * 10 >= mine * 5;
            }
            out = yes(close);
            return true;
        }
        case Ab::NomadMercer: {
            // (fixes 26, the Nomad review) for another player: only a promised use (answered in ai_deals) - a person's seat
            // in tests: no
            if (d.prompt.find("Exhaust Field Marshal Mercer: ") == 0) {
                out = one(0);
                return true;
            }
            const int np = int(g.planets.size());
            auto stackAt = [&](int loc) {
                if (loc < 0) return 0;
                return loc < np ? g.planets[size_t(loc)].units[size_t(p)].groundForces()
                                : loc - np < int(g.systems.size()) ? g.systems[size_t(loc - np)].space[size_t(p)].groundForces() : 0;
            };
            if (yn) {
                bool want = false;
                const int act = g.activeSystem >= 0 ? g.activeSystem : d.value > 0 && d.value < int(g.systems.size()) ? d.value : -1;
                if (act >= 0)
                    for (int16_t x : g.systems[act].planets)
                        if (g.planets[x].owner == p && defendScore(g, p, x) > 1.0f) want = true;
                // (0.55.0: asked 67 times in 80 games, never used) also to stage spare troops at the front for free: from
                // a stack of 3+ to our planets here when enemies or Mecatol Rex are within 2 (it readies every round)
                if (!want && act >= 0 && g.round >= 2 && g.systems[act].home != p) {
                    bool front = false;
                    for (int x : systemsWithin(g, act, 2)) {
                        if (x == g.mecatol()) front = true;
                        for (int q = 0; q < g.numPlayers && !front; ++q)
                            if (q != p && (g.systems[size_t(x)].space[size_t(q)].nonFighterShips() > 0 || g.systems[size_t(x)].home == q))
                                front = true;
                    }
                    int spare = 0;
                    for (size_t x = 0; x < g.planets.size(); ++x)
                        if (g.planets[x].system != act) spare = std::max(spare, g.planets[x].units[size_t(p)].groundForces() - 2);
                    want = front && spare >= 1 && aiTune("mercer", 1.f) > 0.f;
                }
                out = yes(want);
                return true;
            }
            // which ground forces: from the biggest stacks (0.58.2: also with only one to choose - then the question asks
            // for exactly 1 like the planet question after it, and its unit ids were scored as planets: a crash)
            if (d.maxPick > 1 || d.minPick != 1 || !d.onPlanets) {
                std::vector<int> ids;
                for (const Option& o : d.options)
                    if (o.id >= 0) ids.push_back(o.id);
                std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) { return stackAt(a / 64) > stackAt(b / 64); });
                out = Answer();
                for (int id : ids)
                    if (int(out.picks.size()) < d.maxPick) out.picks.push_back(id);
                if (out.picks.empty()) out.picks.push_back(firstReal(d));
                return true;
            }
            int x = bestBy(d, [&](int id) { return defendScore(g, p, id); });
            out = one(x >= 0 ? x : firstReal(d));
            return true;
        }
        case Ab::NomadThundarian: {
            // (fixes 26, the Nomad review) in another player's combat: only when we promised them our agent - judged from their
            // side (Decision::value = their hits * 100 + their opponent's)
            if (d.targetPlayer >= 0 && d.targetPlayer != p && g.agentFor[size_t(p)] != d.targetPlayer) {
                out = one(0);
                return true;
            }
            const int mine = d.value / 100, theirs = d.value % 100;
            out = yes(theirs - mine >= 2 || (mine == 0 && theirs >= 2));
            return true;
        }
        case Ab::NomadArtuno: out = yes(d.value >= 2); return true;
        case Ab::MentakAgent:
            // (fixes 27, the Mentak review) the Yssaril with Ssruu as Suffi An: a card for them and for the pillaged player - only
            // with a short hand, and not for a player close to winning (the Mentak's own Suffi An: always)
            if (!isFaction(g, p, "mentak")) {
                int victim = -1;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p && d.prompt.find(" and " + playerName(g, q) + " each draw") != std::string::npos) victim = q;
                out = yes(int(g.players[p].actionCards.size()) < 3 && (victim < 0 || g.players[victim].vp < g.vpGoal - 3));
                return true;
            }
            break;
        case Ab::QuantumManipulator:
        case Ab::Amalgamation: {
            int k = -1;
            for (const Option& o : d.options) k = std::max(k, o.id);
            out = one(k);
            return true;
        }
        case Ab::Cavalry: {
            if (yn) {
                out = yes(true);
                return true;
            }
            int t = bestBy(d, [&](int id) { return float(unitDef(g, p, UnitType(id)).combat); });
            out = one(t >= 0 ? t : -1);
            return true;
        }
        case Ab::Terragenesis: out = one(has(d, 10000) ? 10000 : -1); return true;
        case Ab::Hecatoncheires: {
            int x = g.groundCombatPlanet;
            bool threat = false;
            for (int y : controlledPlanets(g, p))
                if (defendScore(g, p, y) > 1.5f && (d.prompt.find(std::string(kPlanets[g.planets[y].def].name)) != std::string::npos)) threat = true;
            (void)x;
            // (the Titans review, checkpoint 2 - knob hecakeep, default 2) the bots took a mech and an infantry for 1.9
            // of the 2.3 sleepers awakened a game, and unlocked Tungstantus (5 structures) in 26% of games: keep the PDS until
            // the commander is unlocked, unless the planet is threatened (1) - or at all (2). Tested: 1 changed nothing (the
            // threat test passes for most awakened planets); 2: Tungstantus unlocked in 60% of games (26% before), PDS 1.3 a
            // game, 14 wins / 7.24 VP against 11 / 6.94, fresh seeds 19 / 7.33 against 15 / 6.88. ON (2) with awakenval=3 since
            // the owner's go (2026-11-13 15:31); hecakeep=0 = the old way
            bool tung = false;  // (Tungstantus unlocked)
            for (const LeaderState& ls : g.players[size_t(p)].leaders)
                if (kLeaders[ls.leader].type == kCommander && ls.status != kLocked) tung = true;
            if (aiTune("hecakeep", 2.f) > 0.f && !tung) {
                out = yes(aiTune("hecakeep", 2.f) < 2.f && threat);  // (2: never before the unlock)
                return true;
            }
            out = yes(threat || unitsOnBoard(g, p, UnitType::PDS) >= 4);
            return true;
        }
        case Ab::TitansAgent: {
            // (fixes 27, the Titans review) for another player's units: only a promised use (answered in ai_deals) - a
            // person's seat in tests: no
            if (d.prompt.find("Exhaust Tellurian for ") == 0) {
                out = one(0);
                return true;
            }
            const int hits = d.value;
            bool use = false;
            if (g.groundCombatPlanet >= 0) {
                use = hits >= groundForcesOn(g, p, g.groundCombatPlanet);
            } else if (g.activeSystem >= 0) {
                const Force& f = g.systems[g.activeSystem].space[p];
                int cheap = f.count(UnitType::Fighter);
                for (int t : kShipIdx)
                    if (unitSustains(g, p, UnitType(t))) cheap += f.n[t] - f.dmg[t];
                use = hits > cheap;
            }
            out = yes(use || hits >= 3);
            return true;
        }
        case Ab::Terraform: {
            int x = bestBy(d, [&](int id) { return planetWorth(g, id); });
            out = one(x >= 0 ? x : -1);
            return true;
        }
        case Ab::CabalAgent: {
            int t = bestBy(d, [&](int id) { return float(unitCost2(g, p, UnitType(id))); });
            out = one(t >= 0 && unitCost2(g, p, UnitType(t)) >= 6 ? t : -1);
            return true;
        }
        // ---- batch 5: the Council Keleres
        case Ab::LawsOrder: {
            const int active = d.value;
            const bool gavel = fquirkIs(g, p, "keleres", 1);
            // (the Keleres review, checkpoint 2 - switch kellaw, test: 6.7 payments a game, 69% at the start of its own turn
            // for any law - mostly the Gavel's Conventions of War / Travel Ban / others' elected laws, before the turn's plan)
            const bool lean = aiTune("kellaw", 0.f) > 0.f;
            bool want = false;
            for (const Law& l : g.laws) {
                const char* k = kAgendas[l.agenda].key;
                auto is = [&](const char* key) { return !std::strcmp(k, key); };
                if (active == p) {  // laws in the way of our own turn
                    if (is("minister_peace") && l.owner >= 0 && l.owner != p) want = true;
                    if (is("censure") && l.owner == p && !me.actionCards.empty()) want = true;
                    if (is("revolution")) {
                        for (int8_t c : me.cards)
                            if (c == 6 && !((me.cardsUsed >> c) & 1)) want = true;  // Technology still to play
                    }
                    if ((is("travel_ban") || is("conventions") || is("articles_war")) && gavel && !lean) want = true;
                    if (gavel && !lean && l.owner >= 0 && l.owner != p) want = true;  // someone else's elected law
                } else if (l.owner == active && active >= 0) {  // a law that helps the player whose turn it is
                    if (is("minister_war") || (gavel && g.players[active].vp >= me.vp + 2)) want = true;
                }
            }
            if (!want) {
                out = one(0);
                return true;
            }
            out = one(has(d, 1) ? 1 : has(d, 2) ? 2 : 0);  // a commodity first
            return true;
        }
        case Ab::KeleresAgent:
            if (!yn && has(d, 3)) {  // (fixes 27: a person's offer when the commodities aren't needed - a bot answering for one keeps him)
                out = one(0);
                return true;
            }
            out = yn ? yes(true) : one(firstReal(d));
            return true;
        case Ab::KeleresCommander:
            out = yn ? yes(true) : one(firstReal(d));
            return true;
        case Ab::ExecutiveOrder: out = one(has(d, 0) ? 0 : firstReal(d)); return true;  // the top of the deck
        case Ab::HegemonicTrade: {  // (0.44.5: the biggest gain, more for a space dock producing here)
            int s = bestBy(d, [&](int id) {
                if (id < 0 || id >= int(g.planets.size())) return 0.f;
                float v = float(planetInfluence(g, id) - planetResources(g, id));
                if (g.planets[size_t(id)].system == g.activeSystem && g.planets[size_t(id)].units[size_t(p)].count(UnitType::SpaceDock)) v *= 1.5f;
                return v;
            }, 1.f);
            out = one(s >= 0 ? s : -1);
            return true;
        }
        case Ab::AgencySupply: {
            if (spendable(g, p, false) < 3) {
                out = one(-1);
                return true;
            }
            int s = bestBy(d, [&](int id) {
                float v = float(productionCapacity(g, p, id));
                for (int16_t a : g.systems[id].adj) v += 0.6f * float(enemyNonFighters(g, p, a));
                if (blockaded(g, p, id)) v -= 3.f;
                return v;
            }, 1.f);
            out = one(s >= 0 ? s : -1);
            return true;
        }
        case Ab::KeleresOdlynn:
        case Ab::KeleresRider: {
            if (yn) {
                bool use = true;
                if (ab == Ab::KeleresRider) {  // give up our vote for a likely 1 card + 2 trade goods
                    float conf = 0;
                    const int o = aiPredictOutcome(g, d.agenda, &conf);
                    const float stake = std::fabs(aiOutcomeValue(g, p, d.agenda, o));
                    use = conf >= 0.55f && (availableVotes(g, p) <= 3 || stake < 0.5f);
                } else {
                    use = g.round >= 3 || g.players[p].vp >= g.vpGoal - 4;
                }
                out = yes(use);
                return true;
            }
            if (ab == Ab::KeleresRider) {
                float conf = 0;
                const int o = aiPredictOutcome(g, d.agenda, &conf);
                out = one(has(d, o) ? o : firstReal(d));
            } else {  // Odlynn Myrr: our 6 votes go behind the outcome we want
                const int o = bestBy(d, [&](int id) { return aiOutcomeValue(g, p, d.agenda, id); });
                out = one(o >= 0 ? o : firstReal(d));
            }
            return true;
        }
        case Ab::KeleresKuuasi: {
            bool use = false;
            const int sys = g.activeSystem;
            if (sys >= 0)
                for (int q = 0; q < g.numPlayers; ++q) {
                    if (q == p || !hasShips(g, q, sys)) continue;
                    const float odds = fightOdds(g, p, g.systems[sys].space[p], q, g.systems[sys].space[q], true);
                    if (odds < 0.85f || g.systems[sys].space[q].nonFighterShips() >= 3) use = true;
                }
            out = one(use ? firstReal(d) : -1);
            return true;
        }
        case Ab::MuaatAgent: {
            // (fixes 26, the Muaat review) Umbat: "choose a player" - the bot picks itself (a deal to lend it is for later)
            if (d.prompt.find("which player") != std::string::npos) {
                bool self = false;
                for (const Option& o : d.options) self = self || o.id == p;
                out = one(self ? p : -1);
                return true;
            }
            // (checkpoint 2 - test knob umbatplan, OFF - see the Umbat value above) the system where the production buys
            // the most (it took the first)
            if (aiTune("umbatplan", 0.f) > 0.f) {
                const int s = aiUmbatSystem(g, p);
                for (const Option& o : d.options)
                    if (s >= 0 && o.id == s) {
                        out = one(s);
                        return true;
                    }
            }
            break;
        }
        default: break;
    }
    if (yn) {
        out = yes(true);
        return true;
    }
    out = one(firstReal(d));
    return true;
}

}  // namespace ti
