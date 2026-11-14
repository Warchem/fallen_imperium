// Computer players and Stage 3 content: when to play action cards, how to
// vote on agendas, predictions for riders, promissory notes and law
// abilities. Simple hand-written priorities, like the rest of the AI.
#include <algorithm>
#include <map>
#include <cmath>
#include <cstring>

#include "ai/ai.h"
#include "ai/ai_internal.h"
#include "engine/deal.h"
#include "engine/query.h"
#include "engine/rules.h"

namespace ti {

namespace {

bool key(int card, const char* k) { return card >= 0 && card < kActionCardCount && !std::strcmp(kActionCards[card].key, k); }
bool agKey(int a, const char* k) { return a >= 0 && a < kAgendaCount && !std::strcmp(kAgendas[a].key, k); }

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
int first(const Decision& d) {
    for (const Option& o : d.options)
        if (o.enabled) return o.id;
    return d.options.empty() ? 0 : d.options[0].id;
}
int firstNonNegative(const Decision& d) {
    for (const Option& o : d.options)
        if (o.enabled && o.id >= 0) return o.id;
    return first(d);
}
Answer firstN(const Decision& d, int n) {
    Answer a;
    for (const Option& o : d.options)
        if (o.enabled && int(a.picks.size()) < n) a.picks.push_back(o.id);
    return a;
}

int vpLeader(const Game& g, int except) {
    int best = -1;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != except && !g.players[q].eliminated && (best < 0 || g.players[q].vp > g.players[best].vp)) best = q;
    return best;
}

float strength(const Game& g, int p, int sys, bool space) { return float(combatStrength(g, p, sys, space)) + 0.01f; }

// ------------------------------------------------------------------ Transit Diodes (0.44.5)
// Enemy ships (and carried ground forces) that can reach a system soon (as the Diplomacy choice weighs it).
float sysDanger(const Game& g, int p, int sys) {
    float danger = 0.f;
    const std::vector<int> near1 = systemsWithin(g, sys, 1), near2 = systemsWithin(g, sys, 2);
    for (int z : near2) {
        const bool close = std::find(near1.begin(), near1.end(), z) != near1.end();
        for (int q = 0; q < g.numPlayers; ++q) {
            if (q == p || g.players[size_t(q)].eliminated) continue;
            const Force& f = g.systems[size_t(z)].space[size_t(q)];
            const float hostile = hostileFlag(g, p, q) ? 1.5f : 1.f;
            danger += (float(f.nonFighterShips()) + 0.5f * float(f.groundForces())) * hostile * (close ? 1.f : 0.45f);
        }
    }
    return danger;
}

// The move: our most valuable threatened planet gets ground forces from quiet planets (each keeps 1; none taken
// where our carriers wait, nor from a threatened home).
struct TdPlan {
    int to = -1;
    std::map<int, int> take;  // planet * 16 + unit type -> how many
    int total = 0;
};
TdPlan transitPlan(const Game& g, int p) {
    TdPlan plan;
    const std::vector<int> mine = controlledPlanets(g, p);
    float best = 3.f, bestDanger = 0.f;
    for (int x : mine) {
        const PlanetState& ps = g.planets[size_t(x)];
        const SystemState& sy = g.systems[size_t(ps.system)];
        const float danger = sysDanger(g, p, ps.system);
        if (danger < 1.f) continue;
        const float worth = float(planetResources(g, x) + planetInfluence(g, x)) + (kPlanets[ps.def].legendary ? 3.f : 0.f) +
                            (sy.home == p ? 4.f : 0.f) + 1.5f * float(ps.units[size_t(p)].structures());
        const float guard = float(ps.units[size_t(p)].groundForces()) + 0.5f * float(sy.space[size_t(p)].nonFighterShips());
        const float need = worth * danger / (1.f + guard);
        if (need > best) {
            best = need;
            bestDanger = danger;
            plan.to = x;
        }
    }
    // (the shared technologies review, checkpoint 2 - switch tdfwd, default off) nothing to defend: spare infantry from home
    // go forward, to the planet of ours beside the most we could take, with a carrier of ours within 2 to pick them up.
    // Tested 2026-11-14 (everyone given Transit Diodes, 98 Competitive games x 2, seats 024 / 135 switched): 930 uses vs
    // 762, but the switched seats won 98 of 196 against 98 of 196 off - no gain, left off.
    if (plan.to < 0 && aiTune("tdfwd", 0.f) > 0.f) {
        const int H = g.players[size_t(p)].homeSystem;
        float bestF = 2.f;
        for (int x : mine) {
            const PlanetState& ps = g.planets[size_t(x)];
            if (ps.system == H || g.systems[size_t(ps.system)].home >= 0) continue;
            float worth = 0.f;
            for (int z : systemsWithin(g, ps.system, 1)) {
                if (g.systems[size_t(z)].home >= 0 || z == g.mecatol()) continue;
                for (int16_t y : g.systems[size_t(z)].planets) {
                    const PlanetState& py = g.planets[size_t(y)];
                    if (py.owner == p) continue;
                    int theirs = 0;
                    for (int q = 0; q < kSides; ++q)
                        if (q != p) theirs += py.units[size_t(q)].groundForces();
                    if (theirs <= 2) worth += float(planetResources(g, y) + planetInfluence(g, y));
                }
            }
            bool carrier = false;
            for (int z : systemsWithin(g, ps.system, 2))
                if (g.systems[size_t(z)].space[size_t(p)].count(UnitType::Carrier) > 0) carrier = true;
            if (!carrier) continue;
            worth -= float(ps.units[size_t(p)].groundForces());  // (already stocked)
            if (worth > bestF) {
                bestF = worth;
                plan.to = x;
            }
        }
        if (plan.to < 0) return plan;
        for (int16_t x : g.systems[size_t(H)].planets) {
            const PlanetState& ps = g.planets[size_t(x)];
            if (ps.owner != p || plan.total >= 4) continue;
            const int k = std::min({ps.units[size_t(p)].count(UnitType::Infantry), ps.units[size_t(p)].groundForces() - 2, 4 - plan.total});
            if (k <= 0) continue;
            plan.take[x * 16 + int(UnitType::Infantry)] = k;
            plan.total += k;
        }
        if (plan.total < 2) {
            plan = TdPlan();
            plan.to = -1;
        }
        return plan;
    }
    if (plan.to < 0) return plan;
    for (int x : mine) {
        if (x == plan.to || plan.total >= 4) continue;
        const PlanetState& ps = g.planets[size_t(x)];
        const SystemState& sy = g.systems[size_t(ps.system)];
        if (sy.space[size_t(p)].count(UnitType::Carrier) > 0) continue;  // (an invasion force, maybe)
        const float danger = sysDanger(g, p, ps.system);
        if (danger > 0.5f * bestDanger || (sy.home == p && danger > 0.f)) continue;
        int spare = ps.units[size_t(p)].groundForces() - 1;
        for (UnitType t : {UnitType::Infantry, UnitType::Mech}) {
            const int k = std::min({spare, ps.units[size_t(p)].count(t), 4 - plan.total});
            if (k <= 0) continue;
            plan.take[x * 16 + int(t)] = k;
            plan.total += k;
            spare -= k;
        }
    }
    if (plan.total == 0) plan.to = -1;
    return plan;
}

// ------------------------------------------------------------------ Thunder's Edge (0.34.0)
// How much a neutral blocker in system s hurts the others (their planets near it, the leader most) and not us.
float pirateSpotValue(const Game& g, int p, int s) {
    const SystemState& sy = g.systems[size_t(s)];
    float v = 0.f;
    auto near = [&](int q, int sys) {
        if (hasUnits(g, q, sys)) return true;
        for (int16_t a : g.systems[size_t(sys)].adj)
            if (hasShips(g, q, a)) return true;
        return false;
    };
    float res = 0.f;
    for (int16_t x : sy.planets) res += float(planetResources(g, x) + planetInfluence(g, x)) + (g.planets[size_t(x)].owner >= 0 ? 1.f : 0.f);
    if (s == g.mecatol()) res += 4.f;
    int most = 0;
    for (const Player& pl : g.players) most = std::max<int>(most, pl.vp);
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p || g.players[size_t(q)].eliminated || !near(q, s)) continue;
        float w = 1.f + (g.players[size_t(q)].vp >= most && most > 0 ? 1.f : 0.f);
        for (int16_t x : sy.planets)
            if (g.planets[size_t(x)].owner == q) w += 0.8f;
        v += w * (1.f + 0.25f * res);
    }
    if (near(p, s)) v -= 3.f + 0.3f * res;  // we'd have to fight it ourselves
    return v;
}
// Mercenary Contract: an unguarded planet of an opponent (the leader most) - they lose it
float mercenarySpotValue(const Game& g, int p, int x) {
    const PlanetState& ps = g.planets[size_t(x)];
    if (ps.owner < 0 || ps.owner == p) return -1.f;
    int most = 0;
    for (const Player& pl : g.players) most = std::max<int>(most, pl.vp);
    float v = float(planetResources(g, x) + planetInfluence(g, x)) * 0.5f + (kPlanets[ps.def].legendary ? 2.f : 0.f) +
              float(ps.attachments.size()) * 0.7f;
    if (g.players[size_t(ps.owner)].vp >= most && most > 0) v *= 1.5f;
    return v;
}
// The best strategy card to Strategize (secondary) or Overrule (primary)
int strategizePick(const Game& g, int p, const Decision& d, bool primary) {
    int best = first(d);
    float bv = -1e9f;
    for (const Option& o : d.options) {
        if (!o.enabled || o.id < 0 || o.id >= kStrategyCards) continue;
        float v = aiStrategyCardValue(g, p, o.id);
        if (!primary) {  // a secondary is worth less, some of them nothing much
            static const float k[kStrategyCards] = {0.2f, 0.3f, 0.5f, 0.6f, 0.5f, 0.6f, 0.8f, 0.6f};
            v *= k[o.id];
        }
        if (v > bv) {
            bv = v;
            best = o.id;
        }
    }
    return best;
}

// ------------------------------------------------------------------ agenda values
float lawLikeValue(const Game& g, int p, int a);

// How good outcome o of agenda a would be for player p (rough "victory point" scale).
float outcomeValue(const Game& g, int p, int a, int o) {
    if (agKey(a, "covert") && g.covertAgenda >= 0) a = g.covertAgenda;
    const Player& me = g.players[p];
    const char* k = kAgendas[a].key;
    auto is = [&](const char* x) { return !std::strcmp(k, x); };
    const int n = g.numPlayers;
    switch (kAgendas[a].elect) {
        case Elect::Player: {
            bool bad = is("execution");
            bool self = o == p;
            if (bad) return self ? -3.f : (o == vpLeader(g, -1) ? 1.f : 0.3f);
            if (is("censure")) {
                // 0.30.0 (owner): Political Censure is a muzzle more than a prize - bots hand it to the player
                // they dislike most (B-Factor, with some chance in it), never to someone one point from winning
                const int goal = g.vpGoal > 0 ? g.vpGoal : 10;
                if (self) return me.vp >= goal - 1 ? 5.f : 0.4f;
                if (g.players[o].vp >= goal - 1) return -5.f;
                const float bf = me.pers.arch >= 0 ? me.pers.bf[size_t(o)] : 50.f;
                const uint32_t h = (uint32_t(g.round) * 2654435761u) ^ (uint32_t(p) * 40503u) ^ (uint32_t(o) * 9973u) ^ uint32_t(a);
                const float jitter = float((h >> 7) % 1000) / 1000.f - 0.5f;  // -0.5 .. 0.5
                return (bf - 50.f) / 25.f + jitter;
            }
            float good = is("warrant") ? 1.5f : is("standardization") ? 0.5f : 1.5f;
            if (self) return good;
            return o == vpLeader(g, p) ? -good * 0.6f : -good * 0.3f;
        }
        case Elect::Planet:
        case Elect::NonHomePlanet: {
            const PlanetState& ps = g.planets[o];
            float units = 0;
            for (int q = 0; q < n; ++q) units += float(ps.units[q].groundForces() + ps.units[q].structures());
            float worth = units * 0.4f + float(planetResources(g, o) + planetInfluence(g, o)) * (is("redistribution") ? 0.2f : 0.f);
            if (ps.owner == p) return -worth - 0.5f;
            return ps.owner == vpLeader(g, p) ? worth : worth * 0.5f;
        }
        case Elect::Law: return -lawLikeValue(g, p, o);
        case Elect::ScoredSecret: return meetsObjective(g, p, o) && !hasScored(g, p, o) ? 2.f : -0.4f;
        case Elect::StrategyCard: {
            static const float v[8] = {1.f, 0.5f, 1.5f, 0.8f, 1.f, 0.8f, 1.2f, 1.4f};
            return o >= 0 && o < 8 ? v[o] : 0.f;
        }
        case Elect::ForAgainst:
        case Elect::Covert: break;
    }
    const bool f = o == 0;
    if (is("mutiny")) return f ? 3.f : -3.f;  // the AI votes For, so For gains it a point
    if (is("unconventional")) return f ? 2.f : -0.4f * float(me.actionCards.size());
    if (is("seed_empire")) {
        int hi = 0, lo = 99;
        for (const Player& q : g.players)
            if (!q.eliminated) {
                hi = std::max<int>(hi, q.vp);
                lo = std::min<int>(lo, q.vp);
            }
        if (f) return me.vp == hi ? 3.f : -1.f;
        return me.vp == lo ? 3.f : -0.5f;
    }
    if (is("economic_equality")) return f ? (5.f - me.tradeGoods) * 0.3f : -me.tradeGoods * 0.3f;
    if (is("incentive")) return 0.3f;
    if (is("regulations")) return f ? -std::max(0, me.fleet - 4) * 1.f - 0.2f : 0.4f;
    if (is("rearmament")) {
        if (f) return 0.8f;
        int mechs = unitsOnBoard(g, p, UnitType::Mech);
        return -mechs * 0.5f;
    }
    if (is("plowshares")) {
        int inf = unitsOnBoard(g, p, UnitType::Infantry);
        return f ? -inf * 0.1f : float(controlledPlanets(g, p).size()) * 0.25f;
    }
    if (is("defense_act")) {
        int pds = unitsOnBoard(g, p, UnitType::PDS);
        return f ? pds * 0.1f + 0.1f : (pds ? -1.f : 0.3f);
    }
    if (is("sanctions")) return f ? -std::max(0, int(me.actionCards.size()) - 3) * 0.4f - 0.1f : -0.3f;
    if (is("schematics")) {
        bool ws = ownsWarSunTech(g, p);
        if (f) return ws ? -0.5f : 0.3f;
        return ws ? -0.4f * float(me.actionCards.size()) : 0.2f;
    }
    if (is("conventions")) return f ? 0.1f : -0.2f;
    if (is("conscription")) return f ? -0.4f : 0.f;
    if (is("rep_govt")) {
        int mine = availableVotes(g, p), total = 0, cnt = 0;
        for (int q = 0; q < n; ++q)
            if (!g.players[q].eliminated) {
                total += availableVotes(g, q);
                ++cnt;
            }
        return f ? (mine * cnt < total ? 1.f : -1.f) : -0.2f;
    }
    if (is("revolution")) return f ? -0.6f : -0.1f * float(me.techs.size());
    if (is("arms_reduction")) {
        auto excess = [&](int q) {
            return std::max(0, unitsOnBoard(g, q, UnitType::Dreadnought) - 2) + std::max(0, unitsOnBoard(g, q, UnitType::Cruiser) - 4);
        };
        if (f) {
            float others = 0;
            for (int q = 0; q < n; ++q)
                if (q != p) others += float(excess(q));
            return others / std::max(1, n - 1) * 0.5f - excess(p) * 0.7f;
        }
        int skips = 0;
        for (int x : controlledPlanets(g, p))
            if (planetHasSkip(g, x)) ++skips;
        return -skips * 0.2f;
    }
    if (is("artifact")) {
        int mec = g.mecatol(), units = 0;
        if (f && mec >= 0) {
            units += g.systems[mec].space[p].ships();
            for (int16_t a2 : g.systems[mec].adj) units += std::min(3, g.systems[a2].space[p].ships());
        }
        return f ? 1.f - units * 0.25f : 0.f;
    }
    if (is("shared_research")) return f ? 0.2f : -0.4f;
    if (is("travel_ban")) return f ? 0.f : -0.3f;
    if (is("wormhole_recon")) return f ? 0.f : -0.3f;
    if (is("wormhole_research")) {
        if (!f) return -0.2f;
        float v = 0;
        for (const SystemState& s : g.systems) {
            if (!s.d().wormholes) continue;
            if (s.space[p].ships()) v += 1.5f;
            if (s.d().wormholes & (kAlpha | kBeta)) v -= float(s.space[p].nonFighterShips()) * 0.6f;
        }
        return std::min(1.5f, v);
    }
    if (is("constitution")) {
        float laws = 0;
        for (const Law& l : g.laws) laws += lawLikeValue(g, p, l.agenda);
        return f ? -laws - 0.3f : 0.f;
    }
    if (is("checks")) return f ? 0.f : -0.2f;
    if (is("cladenstine")) return 0.f;
    if (is("articles_war")) return f ? 0.f : 0.5f;
    return 0.f;
}

// Value for p of a law that is in play (for repealing / abolishing).
float lawLikeValue(const Game& g, int p, int a) {
    if (a < 0 || a >= kAgendaCount || !kAgendas[a].law || kAgendas[a].elect == Elect::Law) return 0.f;
    int owner = -1;
    for (const Law& l : g.laws)
        if (l.agenda == a) owner = l.owner;
    if (kAgendas[a].elect == Elect::Player) return owner == p ? 1.5f : -0.4f;
    if (kAgendas[a].elect == Elect::ScoredSecret) return 0.f;
    return outcomeValue(g, p, a, 0);
}

// Votes each player is likely to cast for which outcome.
int predictOutcome(const Game& g, int a, float* confidence) {
    std::vector<Option> outs;
    // rebuild the outcome list the way the rules do (ids only matter)
    int real = agKey(a, "covert") && g.covertAgenda >= 0 ? g.covertAgenda : a;
    switch (kAgendas[real].elect) {
        case Elect::ForAgainst: outs = {{0, "", ""}, {1, "", ""}}; break;
        case Elect::Player:
            for (int q = 0; q < g.numPlayers; ++q)
                if (!g.players[q].eliminated) outs.push_back({q, "", ""});
            break;
        case Elect::StrategyCard:
            for (int c = 0; c < 8; ++c) outs.push_back({c, "", ""});
            break;
        case Elect::Law:
            for (const Law& l : g.laws) outs.push_back({l.agenda, "", ""});
            break;
        default:
            for (size_t x = 0; x < g.planets.size(); ++x)
                if (g.planets[x].owner >= 0) outs.push_back({int(x), "", ""});
            break;
    }
    if (outs.empty()) {
        if (confidence) *confidence = 0;
        return 0;
    }
    std::vector<float> votes(outs.size(), 0.f);
    for (int q = 0; q < g.numPlayers; ++q) {
        if (g.players[q].eliminated) continue;
        // (Hidden Agenda: others' votes are secret - only the Argent's are seen)
        if (int(g.votedFor.size()) > q && g.votedFor[q] != kNoVote && (!eventOn(g, kEvHidden) || isFaction(g, q, "argent"))) {
            for (size_t i = 0; i < outs.size(); ++i)
                if (outs[i].id == g.votedFor[q]) votes[i] += float(g.voteCount[q]);
            continue;
        }
        int best = 0;
        float bv = -1e9f;
        for (size_t i = 0; i < outs.size(); ++i) {
            float v = outcomeValue(g, q, a, outs[i].id);
            if (v > bv) {
                bv = v;
                best = int(i);
            }
        }
        if (bv > 0.2f) votes[best] += float(availableVotes(g, q));
    }
    int win = 0;
    float total = 0;
    for (size_t i = 0; i < votes.size(); ++i) {
        total += votes[i];
        if (votes[i] > votes[win]) win = int(i);
    }
    float second = 0;
    for (size_t i = 0; i < votes.size(); ++i)
        if (int(i) != win) second = std::max(second, votes[i]);
    if (confidence) *confidence = total <= 0 ? 0.3f : std::min(0.95f, 0.5f + (votes[win] - second) / (total + 1.f));
    return outs[win].id;
}

// ------------------------------------------------------------------ action cards
// Abyssal Starpaths (Uncharted Space, 0.33.0): two systems without planets joined so that a fleet of ours
// that can't reach the active system now can. Returns the ships it brings (0 = not worth it).
struct Starpath {
    int a = -1, b = -1, ships = 0;
};
Starpath starpathFor(const Game& g, int p, int target) {
    Starpath best;
    if (target < 0) return best;
    const int n = int(g.systems.size());
    auto empty = [&](int s) { return !g.systems[size_t(s)].isHyperlane() && g.systems[size_t(s)].planets.empty() && !g.systems[size_t(s)].offBoard; };
    auto bfs = [&](int from) {
        std::vector<int> dist(size_t(n), 99);
        std::vector<int> q{from};
        dist[size_t(from)] = 0;
        for (size_t i = 0; i < q.size(); ++i)
            for (int16_t a : g.systems[size_t(q[i])].adj) {
                if (dist[size_t(a)] <= dist[size_t(q[i])] + 1 || (g.systems[size_t(a)].d().anomalies & kSupernova)) continue;
                dist[size_t(a)] = dist[size_t(q[i])] + 1;
                q.push_back(a);
            }
        return dist;
    };
    for (int s = 0; s < n; ++s) {
        const Force& f = g.systems[size_t(s)].space[size_t(p)];
        if (s == target || f.nonFighterShips() <= 0 || g.systems[size_t(s)].hasToken(p)) continue;
        int m = 0;
        for (int t : kShipIdx)
            if (t != int(UnitType::Fighter) && f.n[t]) m = std::max(m, int(unitDef(g, p, UnitType(t)).move));
        const std::vector<int> d = bfs(s);
        if (d[size_t(target)] <= m) continue;  // gets there anyway
        for (int y = 0; y < n; ++y) {
            if (!empty(y) || d[size_t(y)] > m - 1) continue;
            for (int x = 0; x < n; ++x) {
                if (x == y || !empty(x)) continue;
                const bool next = x == target || std::find(g.systems[size_t(x)].adj.begin(), g.systems[size_t(x)].adj.end(), int16_t(target)) !=
                                                     g.systems[size_t(x)].adj.end();
                if (!next || d[size_t(y)] + 1 + (x == target ? 0 : 1) > m) continue;
                if (f.nonFighterShips() > best.ships) best = {y, x, f.nonFighterShips()};
            }
        }
    }
    return best;
}

float playValue(const Game& g, const Decision& d, int card) {
    const int p = d.player;
    const Player& me = g.players[p];
    const int sys = d.system;
    const int opp = d.targetPlayer;
    auto is = [&](const char* k) { return key(card, k); };
    const Win w = Win(d.window);
    // ---- Uncharted Space (0.33.0)
    if (usCard(card)) {
        if (is("arbitrage")) return 2.5f;
        if (is("ai_augury")) {  // only to trade away a secret we're far from
            for (int16_t o : me.secrets) {
                ObjProgress pr = objectiveProgress(g, p, o);
                if (!meetsObjective(g, p, o) && (pr.need == 0 || pr.missing() >= 2)) return 1.5f;
            }
            return 0.f;
        }
        if (is("fulfillment_protocols_ds")) {
            if (sys < 0) return 0.f;
            if (g.activePlayer != p) return 2.5f;  // someone is coming for our ships
            return otherShipsIn(g, p, sys) ? 2.f : 0.4f;
        }
        if (is("hostile_world_ds")) return d.planet >= 0 ? 1.4f + 0.25f * float(planetResources(g, d.planet)) : 0.f;
        if (is("echo_shielding")) return 2.f;
        if (is("illusory_duplication_ds")) {
            if (sys < 0) return 0.f;
            int n = 0;
            const Force& f = g.systems[size_t(sys)].space[size_t(p)];
            for (int t : kShipIdx)
                if (t != int(UnitType::Fighter) && !unitSustains(g, p, UnitType(t))) n += f.n[t];
            return n >= 2 ? 1.6f + 0.2f * float(n) : n == 1 ? 0.8f : 0.f;
        }
        if (is("freedom_fighters_ds")) return d.planet >= 0 ? 1.5f + 0.3f * float(planetInfluence(g, d.planet)) : 0.f;
        if (is("singularity_charge_ds")) {
            if (sys < 0 || opp < 0) return 0.f;
            auto fresh = [&](int q) {  // units that could still soak a hit
                int n = 0;
                const Force& f = d.planet >= 0 ? g.planets[size_t(d.planet)].units[size_t(q)] : g.systems[size_t(sys)].space[size_t(q)];
                for (int t = 0; t < kUnitTypes; ++t)
                    if (f.n[t] > f.dmg[t] && unitSustainsAt(g, q, UnitType(t), sys, d.planet)) n += f.n[t] - f.dmg[t];
                return n;
            };
            const int diff = fresh(opp) - fresh(p);
            return diff >= 2 ? 1.5f + 0.4f * float(diff) : 0.f;
        }
        if (is("emergency_meeting")) return 0.8f;
        if (is("special_session")) return 0.2f;
        if (is("carapace_plating")) return d.value > 0 ? 1.2f + 0.5f * float(std::min(d.value, 2)) : 0.f;
        if (is("contagion")) {
            if (d.planet < 0 || opp < 0) return 0.f;
            const int n = g.planets[size_t(d.planet)].units[size_t(opp)].count(UnitType::Infantry);
            return n >= 1 ? 1.0f + 0.4f * float((n + 1) / 2) : 0.f;
        }
        if (is("safety_overrides_ds")) return 2.f;
        if (is("confused_sage")) {  // turn one of our misses into a hit, or one of their hits on us into a miss
            const bool mine = g.sageRoller == p, aimedAtUs = g.sageTarget == p;
            for (size_t i = 0; i < g.sageDice.size(); ++i) {
                const int v = g.sageDice[i], need = g.sageNeed[i];
                if (mine && v < need && v + 3 >= need) return 1.8f;
                if (aimedAtUs && v >= need && v - 3 < need) return 1.8f;
            }
            return 0.f;
        }
        if (is("renegotiation")) return 1.5f;
        if (is("bounty_contracts")) {
            int best = 0;
            for (int16_t o : g.publicObjectives) {
                int n = 0;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (!g.players[size_t(q)].eliminated && !hasScored(g, q, o) && meetsObjective(g, q, o)) ++n;
                best = std::max(best, n);
            }
            return best ? 0.8f + 0.6f * float(best) : 0.f;
        }
        if (is("escape_clause")) return d.value >= 2 ? 1.5f + 0.2f * float(d.value) : 0.f;
        if (is("classified_weapons")) return d.value >= 2 ? 1.2f + 0.3f * float(d.value) : d.value == 1 ? 0.6f : 0.f;
        if (is("aggressive_broker")) return 2.f;
        if (is("abyssal_starpaths")) {
            const Starpath sp = starpathFor(g, p, sys);
            return sp.ships >= 2 ? 1.0f + 0.5f * float(sp.ships) : 0.f;
        }
        return 0.f;
    }
    // ---- Thunder's Edge (0.34.0)
    if (teCard(card)) {
        // 0.43.0 (debug pass 1, T11): Black Market Dealings - to sell spare action cards to a partner with trade goods
        if (is("blackmarketdealing")) {
            if (opp < 0 || opp >= g.numPlayers || w != Win::DealNegotiation) return 0.f;
            const int spare = int(me.actionCards.size()) - std::max(2, actionCardLimit(g, p) - 3);
            return spare >= 1 && g.players[size_t(opp)].tradeGoods >= 2 ? 0.9f : 0.f;
        }
        if (is("lieinwait")) return 1.6f;
        if (is("crashlanding")) return 1.3f;
        if (is("extremeduress")) {  // costly for them unless they play their strategy card anyway
            if (opp < 0 || opp >= g.numPlayers) return 0.f;
            const Player& o = g.players[size_t(opp)];
            return 0.4f + 0.15f * float(o.tradeGoods) + 0.25f * float(o.actionCards.size()) + (o.vp >= me.vp + 2 ? 0.5f : 0.f);
        }
        if (is("puppetsonastring")) return me.tactic >= 2 ? 1.6f : me.tactic == 1 ? 1.1f : 0.f;
        if (is("crisis")) {  // skip the next player: worth it against the leader or an enemy coming up
            int next = -1;
            std::vector<int> order = initiativeOrder(g);
            auto it = std::find(order.begin(), order.end(), d.targetPlayer >= 0 && d.targetPlayer < g.numPlayers ? d.targetPlayer : -1);
            if (it != order.end())
                for (size_t k = 1; k <= order.size(); ++k) {
                    const int q = order[size_t((size_t(it - order.begin()) + k) % order.size())];
                    if (!g.players[size_t(q)].passed && !g.players[size_t(q)].eliminated) {
                        next = q;
                        break;
                    }
                }
            if (next < 0 || next == p) return 0.f;
            int most = 0;
            for (const Player& pl : g.players) most = std::max<int>(most, pl.vp);
            return g.players[size_t(next)].vp >= most && most > 0 ? 1.4f : 0.5f;
        }
        if (is("rescue")) {
            if (sys < 0 || g.activePlayer < 0) return 0.f;
            const float before = strength(g, p, sys, true), them = strength(g, g.activePlayer, sys, true);
            return before < them * 1.4f ? 1.3f : 0.4f;
        }
        return 0.f;
    }
    if (is("summit")) return 3.f;
    if (is("investments")) return 1.f;
    if (is("disgrace")) {
        int picker = -1;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p) picker = picker < 0 || g.players[q].vp > g.players[picker].vp ? q : picker;
        return picker >= 0 && g.players[picker].vp >= me.vp + 2 && d.value == 7 ? 2.f : 0.f;
    }
    if (is("fs")) return sys >= 0 && otherShipsIn(g, p, sys) ? 1.5f : 0.f;
    if (is("solar_flare")) {
        if (sys < 0) return 0.f;
        for (int q = 0; q < g.numPlayers; ++q) {
            if (q == p) continue;
            for (int16_t x : g.systems[sys].planets)
                if (g.planets[x].units[q].count(UnitType::PDS)) return 2.f;
        }
        return 0.f;
    }
    if (is("harness")) return commodityValue(g, me) - me.commodities >= 2 ? 2.f : 0.f;
    // 0.43.0 (debug pass 1: never played): cards that open routes for the ships still to move in this action
    if (is("lost_star") || is("nav_suite")) {
        if (sys < 0 || g.activePlayer != p) return 0.f;
        float v = 0.f;
        for (size_t s = 0; s < g.systems.size(); ++s) {
            if (int(s) == sys || g.systems[s].hasToken(p) || g.systems[s].space[size_t(p)].nonFighterShips() == 0) continue;
            bool reachNow = false;
            for (int t : kShipIdx)
                if (!reachNow && g.systems[s].space[size_t(p)].n[t] && t != int(UnitType::Fighter)) reachNow = shipReach(g, p, int(s), sys, UnitType(t)).ok;
            if (reachNow) continue;
            if (is("lost_star")) {  // a wormhole of ours and one at the active system, alpha / beta
                const uint8_t a = wormholesOf(g, int(s)) & (kAlpha | kBeta), b = wormholesOf(g, sys) & (kAlpha | kBeta);
                if (a && b && hexDistance(g.systems[s].pos, g.systems[size_t(sys)].pos) > 1) v += float(g.systems[s].space[size_t(p)].nonFighterShips());
            } else {  // anomalies in the way: a nebula start (move 1), rifts / asteroids / supernovas between
                const bool near = hexDistance(g.systems[s].pos, g.systems[size_t(sys)].pos) <= 2;
                if (near && (anomaliesFor(g, p, int(s)) || anomaliesFor(g, p, sys))) v += float(g.systems[s].space[size_t(p)].nonFighterShips());
            }
        }
        return v >= 2.f ? 1.4f + 0.2f * std::min(v, 5.f) : 0.f;
    }
    if (is("rally") || is("upgrade") || is("fsb") || is("counterstroke")) return 2.5f;
    if (is("decoy")) return 1.2f;
    if (is("coup")) {
        if (opp < 0) return 0.f;
        if (d.value == 7) return 3.f;
        return g.players[opp].vp >= me.vp + 2 ? 2.f : 0.f;
    }
    if (is("master_plan")) return 2.f;
    if (is("experimental")) return 3.f;
    if (is("mjets")) return 2.f;
    if (is("scramble")) return d.value == p ? 1.5f : 0.f;  // (fixes 27: offered to third parties too - only when it's at us)
    if (is("reveal_prototype")) return spendable(g, p, false) >= 6 ? 2.f : 0.5f;
    if (is("mb") || is("f_prototype")) {
        if (sys < 0 || opp < 0) return 0.f;
        bool space = d.planet < 0;
        float r = strength(g, p, sys, space) / strength(g, opp, sys, space);
        return r > 0.4f && r < 2.5f ? 2.f : 0.f;
    }
    if (is("s_retreat")) {
        if (sys < 0 || opp < 0) return 0.f;
        float odds = fightOdds(g, opp, g.systems[sys].space[opp], p, g.systems[sys].space[p], true);
        return odds > 0.8f ? 3.f : 0.f;
    }
    if (is("emergency")) {
        int dmg = 0;
        if (sys >= 0) {
            for (int t = 0; t < kUnitTypes; ++t) dmg += g.systems[sys].space[p].dmg[t];
            for (int16_t x : g.systems[sys].planets)
                for (int t = 0; t < kUnitTypes; ++t) dmg += g.planets[x].units[p].dmg[t];
        }
        return dmg >= 2 ? 2.f : 0.f;
    }
    if (is("waylay")) return opp >= 0 && sys >= 0 && g.systems[sys].space[opp].count(UnitType::Fighter) == 0 ? 2.f : 0.5f;
    if (is("rout")) {
        if (sys < 0 || opp < 0) return 0.f;
        float myOdds = fightOdds(g, p, g.systems[sys].space[p], opp, g.systems[sys].space[opp], true);
        return myOdds < 0.45f ? 3.f : 0.f;
    }
    if (is("intercept")) {
        if (sys < 0 || opp < 0) return 0.f;
        float myOdds = fightOdds(g, p, g.systems[sys].space[p], opp, g.systems[sys].space[opp], true);
        return myOdds > 0.5f ? 2.f : 0.f;
    }
    if (is("sh")) return d.value >= 1 ? 1.5f + d.value * 0.3f : 0.f;
    if (is("dh") || is("reflective") || is("courageous")) return 3.f;
    if (is("salvage"))  // (0.44.4: not against a Vaden's High Security Vault)
        return opp >= 0 && g.players[opp].commodities >= 2 && !((g.modules & kModDiscordant) && breakthroughIs(g, opp, "vaden")) ? 2.f : 0.f;
    if (is("blitz") || is("bunker") || is("disable") || is("parley") || is("infiltrate") || is("reparations")) return 2.f;
    if (is("ghost_squad")) return 1.f;
    if (is("fire_team")) return d.value >= 2 ? 2.f : 0.f;
    if (is("war_machine")) return spendable(g, p, false) >= 3 ? 1.5f : 0.f;
    if (is("stability")) return me.holdsCard(7) || me.holdsCard(0) ? 1.5f : 0.f;
    if (is("abs")) return 1.f;
    if (is("dp")) return 1.f;
    if (w == Win::AgendaRevealed && is("veto")) {
        float best = -1e9f;
        float conf = 0;
        int o = predictOutcome(g, d.agenda >= 0 ? d.agenda : g.currentAgenda, &conf);
        best = outcomeValue(g, p, g.currentAgenda, o);
        return best < -1.f ? 2.f : 0.f;
    }
    if (w == Win::AfterAgendaRevealed) {
        int a = g.currentAgenda;
        if (is("hack")) {  // voting last matters when the outcomes are far apart for us
            float lo = 1e9f, hi = -1e9f;
            if (a >= 0 && kAgendas[a].elect == Elect::ForAgainst) {
                for (int o = 0; o < 2; ++o) {
                    float v = outcomeValue(g, p, a, o);
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            } else if (a >= 0 && kAgendas[a].elect == Elect::Player) {
                for (int q = 0; q < g.numPlayers; ++q)
                    if (!g.players[q].eliminated) {
                        float v = outcomeValue(g, p, a, q);
                        lo = std::min(lo, v);
                        hi = std::max(hi, v);
                    }
            }
            return hi - lo >= 3.f ? 1.2f : 0.f;
        }
        if (is("assassin") || is("insider")) return is("assassin") ? 0.6f : 0.f;
        float conf = 0;
        predictOutcome(g, a, &conf);
        float reward = is("imp_rider") ? 4.f : is("tech_rider") ? 2.5f : is("trade_rider") ? 2.f : is("lead_rider") ? 2.f
                     : is("politic_rider")                      ? 2.f
                     : is("war_rider")                          ? 2.f
                     : is("const_rider")                        ? 1.5f
                     : is("diplo_rider")                        ? 1.8f  // (0.42.2: 1 / 0.8 never cleared the bar)
                     : is("sanction")                           ? 1.5f
                                                                : 0.f;
        // a rider costs our vote: only worth it when we're fairly sure
        return conf >= 0.7f ? reward * conf - 0.8f : 0.f;
    }
    if (is("bribery")) return me.tradeGoods >= 3 ? 0.6f : 0.f;
    if (is("distinguished")) return 1.5f;
    if (is("confusing")) return outcomeValue(g, p, g.currentAgenda, p) < -0.5f ? 3.f : 0.f;
    if (is("confounding")) return outcomeValue(g, p, g.currentAgenda, p) > 1.f ? 3.f : 0.f;
    if (is("deadly_plot")) return outcomeValue(g, p, g.currentAgenda, d.value) < -1.5f ? 2.f : 0.f;
    if (is("sabo")) {
        // Cancel the card being played if it hurts us or helps the leader (0.21.1: not every
        // harmful card any more - only when we are the one likely to be hurt).
        const int c = d.card, by = d.targetPlayer;  // targetPlayer: who played it
        if (by == p) return 0.f;
        const bool combat = key(c, "dh") || key(c, "mb") || key(c, "sh") || key(c, "s_retreat") || key(c, "parley") ||
                            key(c, "experimental");
        if (combat) return g.activeSystem >= 0 && hasUnits(g, p, g.activeSystem) ? 2.f : 0.f;  // we are in that fight
        if (key(c, "coup")) return g.activePlayer == p ? 2.f : 0.f;                          // our strategy action
        const bool boost = key(c, "imp_rider") || key(c, "confounding");
        const bool aimed = key(c, "plague") || key(c, "lucky") || key(c, "meltdown") || key(c, "spy") || key(c, "insub") ||
                           key(c, "uprising") || key(c, "cripple") || key(c, "disgrace");
        if (!boost && !aimed) return 0.f;
        if (by < 0) return 2.f;
        int top = 0;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != by && !g.players[q].eliminated) top = std::max<int>(top, g.players[q].vp);
        const bool leader = g.players[by].vp >= top && g.players[by].vp > me.vp;
        if (boost) return leader || g.players[by].vp >= me.vp ? 2.f : 0.f;
        // the target is chosen later: we are the likely victim when they are our neighbour
        return leader || areNeighbours(g, p, by) ? 2.f : 0.f;
    }
    if (is("reverse_engineer")) return 1.f;
    return 0.f;
}

// Keep value of a card in hand (lowest gets discarded).
float keepValue(int card) {
    if (key(card, "sabo") || key(card, "dh") || key(card, "sh")) return 3.f;
    // 0.42.2: cards the bots (almost) never play go first when the hand is full
    for (const char* k : {"lost_star", "nav_suite", "silence_space", "insider", "scuttle", "special_session", "psionic_hammer",
                          "exchangeprogram", "blackmarketdealing"})
        if (key(card, k)) return 1.f;
    if (actionCardIsComponent(card)) return 1.5f;
    return 2.f;
}

Answer playCard(const Game& g, const Decision& d) {
    int best = -1;
    // (0.55.0) the last round: nothing to keep the cards for
    float bv = aiLevelScale(g) > 0.f && aiEndgame(g, d.player).lastRound && aiTune("lastcards", 1.f) > 0.f ? 0.15f : 0.4f;
    for (const Option& o : d.options) {
        if (o.id < 0) continue;
        Decision dd = d;
        float v = playValue(g, dd, o.id);
        if (v > bv) {
            bv = v;
            best = o.id;
        }
    }
    return one(best >= 0 ? best : (has(d, -1) ? -1 : first(d)));
}

// Choices made while resolving a card (Tag::CardTarget).
Answer cardTarget(const Game& g, const Decision& d) {
    const int p = d.player;
    const int c = d.card;
    auto is = [&](const char* k) { return key(c, k); };
    auto bestBy = [&](auto score) {
        int best = first(d);
        float bv = -1e9f;
        for (const Option& o : d.options) {
            if (!o.enabled || o.id < 0) continue;
            float v = score(o.id);
            if (v > bv) {
                bv = v;
                best = o.id;
            }
        }
        return best;
    };
    // (fixes 27, the action cards review) Manipulate Investments one trade good at a time: the first 3 cards 2 / 2 / 1 as
    // before; Ghost Squad a move at a time: everything from the other planets onto the invaded one (d.value), as before
    if (is("investments") && d.prompt.find("Manipulate Investments: trade good") == 0) {
        std::vector<int> en;
        for (const Option& o : d.options)
            if (o.enabled) en.push_back(o.id);
        if (en.empty()) return one(first(d));
        return one(en[size_t(d.value) % std::min<size_t>(3, en.size())]);
    }
    if (is("ghost_squad") && d.prompt.find("Ghost Squad: move from") == 0) {
        int best = -1, most = 0;
        for (const Option& o : d.options)
            if (o.enabled && o.id >= 0 && o.id != d.value && g.planets[size_t(o.id)].units[size_t(p)].groundForces() > most) {
                most = g.planets[size_t(o.id)].units[size_t(p)].groundForces();
                best = o.id;
            }
        return one(best);
    }
    if (is("ghost_squad") && d.prompt.find("Ghost Squad: move to") == 0) return one(has(d, d.value) ? d.value : first(d));
    if (is("ghost_squad") && d.prompt.find("Ghost Squad: how many") == 0) return one(d.options.back().id);
    // ---- Thunder's Edge (0.34.0)
    if (teCard(c)) {
        if (isYesNoOptions(d)) {  // Exchange Program, asked of us: agree unless they're an enemy (or we've fleet to spare)
            const int by = d.targetPlayer;
            const bool enemy = by >= 0 && by < g.numPlayers && hostileFlag(g, p, by);
            return one(enemy && g.players[size_t(p)].fleet > 3 ? 0 : 1);
        }
        if (is("lieinwait")) return one(bestBy([&](int x) { return aiCardKeepValue(x); }));
        if (is("rescue"))
            return one(bestBy([&](int id) { return 10.f - float(unitDef(g, p, UnitType(id % 16)).combat) + 0.1f * float(unitDef(g, p, UnitType(id % 16)).cost2); }));
        if (is("crashlanding"))
            return one(bestBy([&](int id) {
                const int x = id / 16;
                return aiPlanetValue(g, p, x) + (id % 16 == int(UnitType::Mech) ? 0.5f : 0.f) + (g.planets[size_t(x)].owner == p ? 2.f : 0.f);
            }));
        if (is("strategize")) return one(strategizePick(g, p, d, false));
        if (is("overrule")) return one(strategizePick(g, p, d, true));
        if (is("piratecontract") || is("piratefleet")) return one(bestBy([&](int s) { return pirateSpotValue(g, p, s); }));
        if (is("mercenarycontract")) return one(bestBy([&](int x) { return mercenarySpotValue(g, p, x); }));
        if (is("brilliance")) return one(bestBy([&](int x) { return float(planetResources(g, x)) + 0.5f * float(planetInfluence(g, x)); }));
        if (is("exchangeprogram")) {
            if (d.prompt.find("which planet") != std::string::npos)  // ours on their best planet (if theirs ever leave, it's ours)
                return one(bestBy([&](int x) { return aiPlanetValue(g, p, x); }));
            return one(bestBy([&](int q) { return hostileFlag(g, p, q) ? -1.f : 1.f + 0.1f * float(g.players[size_t(q)].fleet); }));
        }
        return one(first(d));
    }
    // ---- Uncharted Space (0.33.0)
    if (usCard(c)) {
        const Player& me = g.players[size_t(p)];
        auto fragCount = [&](int i) {  // how many fragments of this one's kind we hold
            int n = 0;
            for (int16_t f : me.fragments)
                if (isRelicFragment(f) && kExploreCards[f].type == kExploreCards[me.fragments[size_t(i)]].type) ++n;
            return n;
        };
        if (is("secured_trove") || is("remnant_collection")) {  // spend the loose ones, keep those near a set of 3
            std::vector<std::pair<int, int>> order;
            for (const Option& o : d.options) order.push_back({fragCount(o.id), o.id});
            std::sort(order.begin(), order.end());
            int want = is("secured_trove") ? 2 : 0;
            if (!want)
                for (const auto& e : order) want += e.first == 1 ? 1 : 0;
            want = std::max(d.minPick, std::min(d.maxPick, want));
            Answer a;
            for (int i = 0; i < want && i < int(order.size()); ++i) a.picks.push_back(order[size_t(i)].second);
            return a;
        }
        if (is("free_trade_network")) return one(has(d, 2) && me.commodities >= 2 ? 2 : first(d));
        if (is("psionic_hammer")) return one(bestBy([&](int q) { return float(g.players[size_t(q)].secrets.size()); }));
        if (is("planetary_rigs")) {
            if (d.prompt.find("deck") != std::string::npos) return one(first(d));
            return one(bestBy([&](int x) { return aiPlanetValue(g, p, x) - 2.f * float(g.planets[size_t(x)].attachments.size()); }));
        }
        if (is("micrometeoroid_storm_ds"))
            return one(bestBy([&](int s) {
                if (g.systems[size_t(s)].space[size_t(p)].count(UnitType::Fighter)) return -1.f;
                int n = 0;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p) n += g.systems[size_t(s)].space[size_t(q)].count(UnitType::Fighter);
                return float(n);
            }));
        if (is("personnel_writ")) return one(bestBy([&](int id) { return id >= 100 ? 2.f : 1.f; }));
        if (is("ai_augury")) return one(aiSecretToReturn(g, d));
        if (is("fulfillment_protocols_ds")) return one(bestBy([&](int t) { return float(unitDef(g, p, UnitType(t)).cost2); }));
        if (is("confused_sage")) {
            const bool mine = g.sageRoller == p;
            if (d.prompt.find("add 3") != std::string::npos) return one(mine ? 1 : 0);
            return one(bestBy([&](int i) {
                const int v = g.sageDice[size_t(i)], need = g.sageNeed[size_t(i)];
                if (mine) return v < need && v + 3 >= need ? 10.f - float(need - v) : 0.f;
                return v >= need && v - 3 < need ? 10.f - float(v - need) : 0.f;
            }));
        }
        if (is("safety_overrides_ds")) {  // give up the cheapest
            std::vector<std::pair<int, int>> order;
            for (const Option& o : d.options) order.push_back({unitDef(g, p, UnitType(o.id / 16)).cost2, o.id});
            std::sort(order.begin(), order.end());
            Answer a;
            for (int i = 0; i < d.minPick && i < int(order.size()); ++i) a.picks.push_back(order[size_t(i)].second);
            return a;
        }
        if (is("aggressive_broker")) {  // let them have whichever is worth less
            int room = 2;
            for (const Option& o : d.options)
                if (o.id == 2) room = std::atoi(o.detail.c_str());
            return one(room < 2 ? 2 : 1);
        }
        if (is("renegotiation")) return one(bestBy([&](int n) { return -noteDefaultValue(g, n); }));
        if (is("abyssal_starpaths")) {
            const Starpath sp = starpathFor(g, p, g.activeSystem);
            Answer a;
            if (has(d, sp.a) && has(d, sp.b)) a.picks = {sp.a, sp.b};
            else for (int i = 0; i < 2 && i < int(d.options.size()); ++i) a.picks.push_back(d.options[size_t(i)].id);
            return a;
        }
        if (is("bounty_contracts"))
            return one(bestBy([&](int o) {
                int n = 0;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (!g.players[size_t(q)].eliminated && !hasScored(g, q, o) && meetsObjective(g, q, o)) ++n;
                return float(n) + (meetsObjective(g, p, o) && !hasScored(g, p, o) ? 0.5f : 0.f);
            }));
        if (is("emergency_meeting")) {  // the agenda in the prompt: on top when its likely outcome suits us
            int a = -1;
            for (int i = 0; i < kAgendaCount; ++i)
                if (d.prompt.find(kAgendas[i].name) != std::string::npos) a = i;
            if (a < 0) return one(first(d));
            float conf = 0;
            const int o = aiPredictOutcome(g, a, &conf);
            return one(outcomeValue(g, p, a, o) >= 0.f ? 1 : 0);
        }
        return one(firstNonNegative(d));
    }
    if (d.maxPick > 1 || d.minPick != 1) {
        int want = d.maxPick;
        if (is("investments")) want = 3;
        if (is("scuttle") || is("decoy")) want = std::max(1, d.minPick);  // (Refit Troops: as many as it may)
        if (is("ghost_squad")) want = d.maxPick;
        want = std::max(d.minPick, std::min(d.maxPick, want));
        return firstN(d, want);
    }
    if (is("fsb") || is("confusing")) {
        // give the trade good to / elect the weakest... confusing: the leader
        if (is("confusing")) return one(bestBy([&](int q) { return float(g.players[q].vp); }));
        return one(bestBy([&](int q) { return -float(g.players[q].vp); }));
    }
    if (is("abs") || is("insub") || is("assassin") || is("dp") || is("spy"))
        return one(bestBy([&](int q) {
            if (q == p) return -100.f;
            float v = float(g.players[q].vp) * 2.f;
            if (is("spy")) v += float(g.players[q].actionCards.size());
            if (is("dp")) v += float(g.players[q].notes.size()) * 0.1f;
            return v;
        }));
    if (is("lucky")) return one(bestBy([&](int id) {  // id = system * 1000 + player * 16 + unit type
        const int t = (id % 1000) % 16;
        return float(t == int(UnitType::Dreadnought) ? 3 : t == int(UnitType::Cruiser) ? 2 : 1);
    }));
    if (is("plague") || is("unstable"))
        return one(bestBy([&](int x) {
            float v = 0;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p) v += float(g.planets[x].units[q].count(UnitType::Infantry));
            return v;
        }));
    if (is("uprising"))  // 0.43.0: its trade goods are the planet's resources (an influence planet gave 0)
        return one(bestBy([&](int x) { return float(planetResources(g, x)) + 0.2f * float(planetInfluence(g, x)); }));
    if (is("reparations"))
        return one(bestBy([&](int x) { return float(planetResources(g, x) + planetInfluence(g, x)); }));
    if (is("cripple"))
        return one(bestBy([&](int x) {
            float v = 0;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p) v += float(g.planets[x].units[q].count(UnitType::PDS));
            return v;
        }));
    if (is("plagiarize") || is("reveal_prototype")) return one(bestBy([&](int t) { return aiTechWant(g, p, t); }));
    if (is("divert_funding"))  // an entry technology we use least (0.42.2)
        return one(bestBy([&](int t) {
            const int lv = kTechs[t].req[0] + kTechs[t].req[1] + kTechs[t].req[2] + kTechs[t].req[3];
            return -aiTechWant(g, p, t) - 20.f * float(lv);
        }));
    if (is("war_effort") || is("war_rider"))  // 0.42.2 (#187): where the fleet pool has room - the ship was lost over it
        return one(bestBy([&](int s) {
            float v = fleetLimit(g, p) - fleetUseIn(g, p, s) > 0 ? 10.f : 0.f;
            for (int16_t a : g.systems[size_t(s)].adj)
                if (otherShipsIn(g, p, a)) v += 1.f;  // a front
            return v + float(g.systems[s].space[p].nonFighterShips()) * 0.2f;
        }));
    if (is("unexpected"))
        return one(bestBy([&](int s) { return float(g.systems[s].space[p].nonFighterShips()); }));
    if (is("repeal")) return one(bestBy([&](int i) { return -lawLikeValue(g, p, g.laws[i].agenda); }));
    if (is("s_retreat"))
        return one(bestBy([&](int s) { return g.systems[s].home == p ? 10.f : float(g.systems[s].space[p].ships()); }));
    if (is("bribery")) {
        int n = std::min<int>(g.players[p].tradeGoods, 3);
        return one(has(d, n) ? n : first(d));
    }
    if (is("f_deployment"))
        return one(bestBy([&](int x) { return g.systems[g.planets[x].system].home == p ? 0.f : 1.f + float(planetResources(g, x)); }));
    if (is("jamming")) {
        // lock the leader's ships with their own token
        int leader = vpLeader(g, p);
        if (d.prompt.find("whose") != std::string::npos) return one(has(d, leader) ? leader : first(d));
        return one(bestBy([&](int s) { return leader >= 0 ? float(g.systems[s].space[leader].nonFighterShips()) : 0.f; }));
    }
    if (is("meltdown")) return one(bestBy([&](int id) { return float(g.players[id % 16].vp); }));
    return one(firstNonNegative(d));
}

}  // namespace

// ------------------------------------------------------------------ public helpers
float aiOutcomeValue(const Game& g, int p, int a, int o) { return outcomeValue(g, p, a, o); }
float aiCardKeepValue(int card) { return keepValue(card); }
int aiPredictOutcome(const Game& g, int a, float* confidence) { return predictOutcome(g, a, confidence); }
bool aiWantsCancel(const Game& g, int p, int card, int by) {
    Decision d;
    d.player = p;
    d.card = card;
    d.targetPlayer = by;
    int sabo = findActionCard("sabo");
    return playValue(g, d, sabo) >= 1.f;
}

float aiComponentValue(const Game& g, int p, int option) {
    const Player& me = g.players[p];
    if (option >= 6000) return aiFactionComponentValue(g, p, option);
    if (option >= 3000) return aiExploreComponentValue(g, p, option);
    if (option >= 2000) {  // Sling Relay (0.44.7: only with money we'd really spend and room in a fleet for the ship -
        // 28% of them used to build nothing, 20% a single fighter)
        const int free = aiLevelScale(g) > 0.f ? aiFreeSpend(g, p, false) : spendable(g, p, false);
        if (free < 2) return 0.f;
        bool room = false;
        for (size_t s = 0; s < g.systems.size() && !room; ++s) {
            bool dock = g.systems[s].space[size_t(p)].count(UnitType::SpaceDock) > 0;
            for (int16_t x : g.systems[s].planets) dock = dock || g.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock) > 0;
            if (dock && !blockaded(g, p, int(s)) && fleetUseIn(g, p, int(s)) < fleetLimit(g, p)) room = true;
        }
        return room ? (free >= 3 ? 1.0f : 0.6f) : 0.f;
    }
    int c = option - 1000;
    auto is = [&](const char* k) { return key(c, k); };
    if (teCard(c)) {  // Thunder's Edge (0.34.0)
        if (is("overrule")) return 1.8f;
        if (is("strategize")) {
            float best = 0.f;
            for (int s = 0; s < kStrategyCards; ++s) {
                const int owner = g.cardOwner[s];
                if (owner >= 0 && ((g.players[size_t(owner)].cardsUsed >> s) & 1)) continue;
                best = std::max(best, aiStrategyCardValue(g, p, s));
            }
            return me.strategy > 0 ? std::min(2.f, 0.6f + 0.1f * best) : 0.2f;
        }
        if (is("piratecontract") || is("piratefleet")) {
            float best = 0.f;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                const SystemState& sy = g.systems[s];
                if (sy.home >= 0 || sy.isHyperlane() || sy.offBoard) continue;
                bool ships = false;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (hasShips(g, q, int(s))) ships = true;
                if (!ships) best = std::max(best, pirateSpotValue(g, p, int(s)));
            }
            return is("piratefleet") ? std::min(2.f, best * 0.15f) - (spendable(g, p, false) < 6 ? 0.5f : 0.f) : std::min(1.6f, best * 0.12f);
        }
        if (is("mercenarycontract")) {
            float best = 0.f;
            for (size_t x = 0; x < g.planets.size(); ++x) {
                const PlanetState& ps = g.planets[x];
                if (ps.destroyed || isHomePlanet(g, int(x)) || isStation(g, int(x))) continue;
                bool units = false;
                for (int q = 0; q < kSides; ++q)
                    if (!ps.units[size_t(q)].empty()) units = true;
                if (!units) best = std::max(best, mercenarySpotValue(g, p, int(x)));
            }
            return me.tradeGoods >= 4 ? best * 0.4f : best * 0.25f;
        }
        if (is("brilliance")) {
            float best = 0.f;
            for (int x : controlledPlanets(g, p))
                if (g.planets[size_t(x)].exhausted && planetHasSkip(g, x)) best = std::max(best, float(planetResources(g, x)));
            return 0.3f * best;
        }
        if (is("exchangeprogram")) return 0.2f;
        return 0.f;
    }
    if (usCard(c)) {  // Uncharted Space (0.33.0)
        if (is("preparation")) return 1.0f;
        if (is("secured_trove")) return 1.6f;
        if (is("professional_archeologists")) return 1.0f;
        if (is("planetary_rigs")) return 1.1f;
        if (is("psionic_hammer")) return 0.2f;
        if (is("remnant_collection")) {
            int loose = 0;
            for (int t = 0; t < 4; ++t) {
                int n = 0;
                for (int16_t f : me.fragments)
                    if (isRelicFragment(f) && int(kExploreCards[f].type) == t) ++n;
                if (n == 1 || (t == 3 && n > 0)) loose += n;
            }
            return loose >= 2 ? 0.6f + 0.4f * float(loose) : loose == 1 ? 0.5f : 0.f;
        }
        if (is("free_trade_network")) {
            int traits = 0;
            for (Trait t : {Trait::Cultural, Trait::Hazardous, Trait::Industrial}) {
                bool any = false;
                for (int x : controlledPlanets(g, p))
                    if (hasTraitOf(g, x, t)) any = true;
                traits += any ? 1 : 0;
            }
            return 0.4f * float(traits) + (me.commodities >= 2 ? 0.3f : 0.f);
        }
        if (is("micrometeoroid_storm_ds")) {
            int best = 0;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                if (g.systems[s].space[size_t(p)].count(UnitType::Fighter)) continue;
                int n = 0;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p) n += g.systems[s].space[size_t(q)].count(UnitType::Fighter);
                best = std::max(best, n);
            }
            return best >= 3 ? 1.0f + 0.15f * float(best) : 0.3f;
        }
        if (is("personnel_writ")) {
            float v = 0.f;
            for (const LeaderState& ls : me.leaders) {
                const int type = kLeaders[ls.leader].type;
                if (type == kCommander && ls.status == kLocked && me.strategy > 0) v = std::max(v, 1.4f);
                if (type == kAgent && ls.status == kExhausted) v = std::max(v, 1.0f);
            }
            return v;
        }
        return 0.f;
    }
    // 0.42.2 (#187: never played in 180 games): Tactical Bombardment when it ties up a good share of the
    // others' influence before an agenda phase; Divert Funding to swap a technology we hardly use for one we
    // want a lot (the research question then picks it)
    if (is("tactical")) {
        if (!g.custodiansRemoved || g.round < 3) return 0.1f;
        int inf = 0;
        for (size_t s = 0; s < g.systems.size(); ++s) {
            bool bomb = false;
            for (UnitType t : {UnitType::Dreadnought, UnitType::WarSun, UnitType::Mech, UnitType::Flagship, UnitType::Flagship2})
                if (bombardAvailable(g, p, int(s), t) > 0) bomb = true;
            if (!bomb) continue;
            for (int16_t x : g.systems[s].planets)
                if (g.planets[x].owner >= 0 && g.planets[x].owner != p && !g.planets[x].exhausted) inf += planetInfluence(g, x);
        }
        return inf >= 4 ? 1.0f + 0.1f * float(std::min(inf, 10)) : 0.1f;
    }
    if (is("divert_funding")) {  // only an entry technology goes back, for a higher one we want a lot more
        auto level = [](int t) { return kTechs[t].req[0] + kTechs[t].req[1] + kTechs[t].req[2] + kTechs[t].req[3]; };
        float worst = 1e9f, want = 0.f;
        for (int t : me.techs)
            if (kTechs[t].faction < 0 && !kTechs[t].unitUpgrade && level(t) == 0) worst = std::min(worst, aiTechWant(g, p, t));
        for (int t = 0; t < kTechCount; ++t)
            if (!me.hasTech(t) && techImplemented(t) && level(t) >= 1 && canResearchNow(g, p, t)) want = std::max(want, aiTechWant(g, p, t));
        return worst < 1e8f && want - worst >= 5.f ? 1.1f : 0.1f;
    }
    if (is("scuttle")) {  // 0.43.0: idle cheap ships into trade goods when an objective wants spending power
        const Goals& G = aiGoals(g, p);
        int idle = 0;
        for (size_t s = 0; s < g.systems.size(); ++s) {
            const Force& f = g.systems[s].space[size_t(p)];
            if (!f.count(UnitType::Destroyer) && !f.count(UnitType::Cruiser)) continue;
            bool foes = false;
            for (int z : systemsWithin(g, int(s), 2))
                for (int q = 0; q < g.numPlayers; ++q) foes = foes || (q != p && g.systems[size_t(z)].space[size_t(q)].nonFighterShips() > 0);
            if (!foes) idle += f.count(UnitType::Destroyer) + f.count(UnitType::Cruiser);
        }
        const bool short_ = G.keepTg + G.keepRes > me.tradeGoods + spendable(g, p, false);
        return idle >= 2 && (short_ || (g.round >= 5 && idle >= 3)) ? 1.2f : 0.1f;
    }
    if (is("f_deployment") || is("war_effort") || is("plagiarize") || is("impersonation")) return 1.3f;
    if (is("f_researched")) return 1.4f;
    if (is("meltdown")) return 1.5f;
    if (is("refit")) return 1.0f;
    if (is("lucky")) return 1.2f;
    if (is("spy") || is("jamming") || is("insub") || is("ghost_ship")) return 0.7f;
    if (is("economic_initiative")) {
        int n = 0;
        for (int x : controlledPlanets(g, p))
            if (g.planets[x].exhausted && hasTraitOf(g, x, Trait::Cultural)) n += planetInfluence(g, x);
        return n * 0.3f;
    }
    if (is("industrial_initiative")) {
        int n = 0;
        for (int x : controlledPlanets(g, p))
            if (hasTraitOf(g, x, Trait::Industrial)) ++n;
        return n * 0.4f;
    }
    if (is("mining_initiative")) {
        int best = 0;
        for (int x : controlledPlanets(g, p)) best = std::max(best, planetResources(g, x));
        return best * 0.4f;
    }
    if (is("messiah")) return float(controlledPlanets(g, p).size()) * 0.25f;
    if (is("f_conscription")) return 0.8f;
    if (is("uprising")) {  // worth it for 2+ trade goods from someone's ready non-home planet
        int best = 0;
        for (size_t x = 0; x < g.planets.size(); ++x) {
            const int q = g.planets[x].owner;
            if (q < 0 || q == p || g.planets[x].exhausted || g.systems[size_t(g.planets[x].system)].home >= 0) continue;
            best = std::max(best, planetResources(g, int(x)));
        }
        return best >= 2 ? 0.6f + 0.2f * float(best) : 0.f;
    }
    if (is("plague") || is("unstable") || is("cripple")) return 0.8f;
    if (is("repeal")) {
        float worst = 0;
        for (const Law& l : g.laws) worst = std::min(worst, lawLikeValue(g, p, l.agenda));
        return -worst;
    }
    (void)me;
    return 0.3f;
}

bool aiCardsAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out) {
    (void)rng;
    const int p = d.player;
    switch (d.tag) {
        case Tag::PlayActionCard: out = playCard(g, d); return true;
        case Tag::CardTarget: out = cardTarget(g, d); return true;
        case Tag::DiscardActionCard: {
            if (d.maxPick > 1 || d.minPick != 1) {
                out = firstN(d, d.minPick);
                return true;
            }
            int worst = first(d);
            float wv = 1e9f;
            for (const Option& o : d.options) {
                float v = keepValue(o.id);
                if (v < wv) {
                    wv = v;
                    worst = o.id;
                }
            }
            out = one(worst);
            return true;
        }
        case Tag::VoteOutcome: {
            int best = -100;
            float bv = 0.2f;
            // personalities: vote against the points leader (Spoiler, Kingslayer) or a betrayal target
            const Style& S = aiStyle(g, p);
            int leader = -1;
            if (S.spoiler > 0)
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p && !g.players[q].eliminated && (leader < 0 || g.players[q].vp > g.players[leader].vp)) leader = q;
            if (S.voteAgainst >= 0) bv = -1e9f;  // must vote (a betrayal objective or Kingslayer)
            // (the Keleres review, checkpoint 2 - switch kelodlynn, test: Odlynn Myrr used, then the bot abstained in 20 of 50
            // agendas - its 6 free votes unused): with Odlynn's votes it always votes for the outcome it values most
            if (aiTune("kelodlynn", 0.f) > 0.f && d.context.find("Odlynn Myrr: 6 more.") != std::string::npos) bv = -1e9f;
            // 0.44.6 (Radiance / Unity Algorithm): our prediction comes true more often with our votes - a point with
            // Unity still unused this round and an objective we could score, a token in a Sigil otherwise
            float predictBonus = 0.f;
            if (d.predicted != -1) {
                predictBonus = 0.8f;
                if (ownsTechKey(g, p, "dsedyng") && g.players[size_t(p)].dsUnityRound != g.round) {
                    bool scorable = false;
                    for (int16_t o : g.publicObjectives)
                        if (!hasScored(g, p, o) && meetsObjective(g, p, o)) scorable = true;
                    predictBonus = scorable ? 3.5f : 1.f;
                }
            }
            int bestAny = -100;  // (0.55.0: abstaining not offered - Special Session, a must-vote effect: the best there is)
            float anyV = -1e30f;
            for (const Option& o : d.options) {
                if (o.id == -100) continue;
                float v = outcomeValue(g, p, d.agenda, o.id);
                if (o.id == d.predicted) v += predictBonus;
                if (leader >= 0) v -= S.spoiler * 0.6f * std::max(0.f, outcomeValue(g, leader, d.agenda, o.id));
                if (S.voteAgainst >= 0) {
                    v -= 0.8f * outcomeValue(g, S.voteAgainst, d.agenda, o.id);
                    if (kAgendas[d.agenda].elect == Elect::Player && o.id == S.voteAgainst) v -= 5.f;
                }
                if (o.enabled && v > anyV) {
                    anyV = v;
                    bestAny = o.id;
                }
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(has(d, best) ? best : has(d, -100) ? -100 : bestAny);
            return true;
        }
        case Tag::VotePlanets: {
            // Keep some planets for the second agenda unless this one matters a lot.
            int a = g.currentAgenda >= 0 ? g.currentAgenda : d.agenda;
            float stake = std::fabs(outcomeValue(g, p, a, d.value));
            std::vector<int> planets = d.planets;
            std::sort(planets.begin(), planets.end(),
                      [&](int x, int y) { return planetInfluence(g, x) > planetInfluence(g, y); });
            int total = 0;
            for (int x : planets) total += planetInfluence(g, x);
            int budget = (g.agendaNumber == 0 && stake < 2.f) ? (total + 1) / 2 : total;
            // Genetic Recombination forced this vote: as few votes as possible for an outcome we don't like
            if (d.amount > 0 && outcomeValue(g, p, a, d.value) < 0.f) {
                out = Answer();
                for (auto it = planets.rbegin(); it != planets.rend(); ++it)
                    if (voteValue(g, p, *it) >= 1) {
                        out.picks.push_back(*it);
                        break;
                    }
                return true;
            }
            out = Answer();
            int used = 0;
            for (int x : planets) {
                if (used >= budget) break;
                out.picks.push_back(x);
                used += planetInfluence(g, x);
            }
            // Envoy Network (Free Systems, 0.44.6): a cultural, a hazardous and an industrial planet among them make 4
            // more votes - add the cheapest planet of each trait still missing (even one with no influence)
            if (ownsTechKey(g, p, "dsfreeg") && !out.picks.empty()) {
                for (Trait tr : {Trait::Cultural, Trait::Hazardous, Trait::Industrial}) {
                    bool have = false;
                    for (int x : out.picks) have = have || hasTraitOf(g, x, tr);
                    if (have) continue;
                    int cheap = -1;
                    for (int x : planets)
                        if (hasTraitOf(g, x, tr) && std::find(out.picks.begin(), out.picks.end(), x) == out.picks.end() &&
                            (cheap < 0 || planetInfluence(g, x) < planetInfluence(g, cheap)))
                            cheap = x;
                    if (cheap >= 0) out.picks.push_back(cheap);
                }
            }
            return true;
        }
        case Tag::Predict: {
            float conf = 0;
            int o = predictOutcome(g, d.agenda, &conf);
            out = one(has(d, o) ? o : first(d));
            return true;
        }
        case Tag::TieBreak: {
            int best = first(d);
            float bv = -1e9f;
            for (const Option& o : d.options) {
                float v = outcomeValue(g, p, g.currentAgenda >= 0 ? g.currentAgenda : d.agenda, o.id);
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case Tag::AgendaPeek: {
            float conf = 0;
            int o = predictOutcome(g, d.agenda, &conf);
            out = one(outcomeValue(g, p, d.agenda, o) >= 0 ? 1 : 0);
            return true;
        }
        case Tag::AgendaChoice: {
            if (d.maxPick > 1 || d.minPick != 1) {
                out = firstN(d, d.minPick);
                return true;
            }
            if (isYesNoOptions(d)) {
                out = one(1);
                return true;
            }
            if (d.prompt.find("Committee") != std::string::npos || d.prompt.find("Grant") != std::string::npos) {
                int best = first(d);
                float bv = -1e9f;
                for (const Option& o : d.options) {
                    float v = d.prompt.find("Grant") != std::string::npos ? aiTechWant(g, p, o.id)
                                                                          : outcomeValue(g, p, d.agenda, o.id);
                    if (v > bv) {
                        bv = v;
                        best = o.id;
                    }
                }
                out = one(best);
                return true;
            }
            if (has(d, p)) {
                out = one(p);
                return true;
            }
            out = one(firstNonNegative(d));
            return true;
        }
        case Tag::GiveNote: {
            // 0.44.6 (Voidwatch, S'ula Mentarion...): the note we'd miss least - its value as deals price it (the old
            // type table ranked an Alliance like a Ceasefire and a faction's second note like a Political Secret)
            int best = first(d);
            float bv = 1e9f;
            for (const Option& o : d.options) {
                if (!o.enabled) continue;
                const float v = noteDefaultValue(g, o.id);
                if (v < bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case Tag::PlayNote: out = one(firstNonNegative(d)); return true;
        case Tag::LawAbility: {
            if (isYesNoOptions(d)) {
                bool yes = true;
                if (d.prompt.find("Committee") != std::string::npos) {
                    float best = -1e9f;
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (!g.players[q].eliminated) best = std::max(best, outcomeValue(g, p, d.agenda, q));
                    yes = best >= 1.f;
                }
                if (d.prompt.find("Minister of Peace") != std::string::npos) {
                    // 0.21.1: it used to be spent on the first activation of any system with someone
                    // else's units. Keep it for when we are the one being attacked (our units are
                    // in the system and there is something to lose), or to stop a player about to win.
                    const int a = g.activePlayer, s = g.activeSystem;
                    yes = false;
                    if (a >= 0 && s >= 0 && a != p) {
                        if (hasUnits(g, p, s)) {
                            float stake = float(g.systems[s].space[p].nonFighterShips()) * 1.5f;
                            for (int16_t x : g.systems[s].planets)
                                if (g.planets[x].owner == p) stake += 1.f + aiPlanetValue(g, p, x) * 0.5f;
                            if (s == g.players[p].homeSystem) stake += 5.f;
                            float att = 0;  // ships the active player has within 2 systems
                            for (int x : systemsWithin(g, s, 2)) att += float(combatStrength(g, a, x, true));
                            const float def = float(combatStrength(g, p, s, true)) + 1.f;
                            yes = stake >= 2.f && att >= def * 0.5f;
                        }
                        if (g.players[a].vp + 2 >= g.vpGoal) yes = true;  // they could win with this turn
                    }
                }
                out = one(yes ? 1 : 0);
                return true;
            }
            if (d.prompt.find("Arbiter") != std::string::npos) {
                for (const Option& o : d.options)
                    if (o.id >= 0 && o.id % 100 == 7 && o.id / 100 != 7) {
                        out = one(o.id);
                        return true;
                    }
                out = one(-1);
                return true;
            }
            if (d.prompt.find("Minister of War") != std::string::npos) {
                int best = -1, bv = 0;
                for (const Option& o : d.options)
                    if (o.id >= 0 && g.systems[o.id].space[p].nonFighterShips() > bv) {
                        bv = g.systems[o.id].space[p].nonFighterShips();
                        best = o.id;
                    }
                out = one(g.players[p].tactic > 0 && best >= 0 ? best : -1);
                return true;
            }
            out = one(firstNonNegative(d));
            return true;
        }
        case Tag::StrategyGive: {
            int best = first(d);
            int lo = 99;
            for (const Option& o : d.options)
                if (g.players[o.id].vp < lo) {
                    lo = g.players[o.id].vp;
                    best = o.id;
                }
            out = one(best);
            return true;
        }
        case Tag::ComponentChoice: {
            if (d.prompt.find("Transit Diodes") != std::string::npos) {  // (0.44.5: the transitPlan move)
                const TdPlan plan = transitPlan(g, p);
                if (d.prompt.find("choose up to") != std::string::npos) {
                    out = Answer();
                    for (const Option& o : d.options) {
                        if (!o.enabled || o.id >= kTdSpace || int(out.picks.size()) >= d.maxPick) continue;
                        const int x = o.id / 64, t = (o.id % 64) / 4, i = o.id % 4;
                        auto it = plan.take.find(x * 16 + t);
                        if (it != plan.take.end() && i < it->second) out.picks.push_back(o.id);
                    }
                    if (int(out.picks.size()) < d.minPick) out = firstN(d, d.minPick);
                    return true;
                }
                if (d.prompt.find("which of them") != std::string::npos) {  // they all go to the one planet
                    out = firstN(d, d.maxPick);
                    return true;
                }
                out = one(has(d, plan.to) ? plan.to : firstNonNegative(d));
                return true;
            }
            if (d.maxPick > 1 || d.minPick != 1) {
                out = firstN(d, d.minPick);
                return true;
            }
            if (d.prompt.find("Contractual Obligations") != std::string::npos) {  // (0.44.5) home, where it's safest
                const int home = g.players[size_t(p)].homeSystem;
                out = one(has(d, home) ? home : firstNonNegative(d));
                return true;
            }
            if (d.prompt.find("Sling Relay") != std::string::npos) {
                // (0.44.7) a dock with room in the fleet pool for the new ship - the one with enemies nearest, else home
                const int home = g.players[p].homeSystem;
                int best = -1;
                float bv = -1e9f;
                for (const Option& o : d.options) {
                    if (!o.enabled || o.id < 0 || o.id >= int(g.systems.size())) continue;
                    float v = fleetUseIn(g, p, o.id) < fleetLimit(g, p) ? 1.f : -5.f;
                    if (o.id == home) v += 0.5f;
                    std::vector<int> zone{o.id};
                    for (int16_t a : g.systems[size_t(o.id)].adj) zone.push_back(a);
                    for (int z : zone)
                        for (int q = 0; q < g.numPlayers; ++q)
                            if (q != p) v += 0.4f * float(g.systems[size_t(z)].space[size_t(q)].nonFighterShips());
                    if (v > bv) bv = v, best = o.id;
                }
                out = one(best >= 0 ? best : (has(d, home) ? home : firstNonNegative(d)));
                return true;
            }
            out = one(firstNonNegative(d));
            return true;
        }
        case Tag::UseTech: {
            // Self-Assembly Routines (0.44.8): the mech where it guards the most - planets near enemy ships, no mech
            // there yet, the better planet
            if (d.prompt.find("Self-Assembly") != std::string::npos && !isYesNoOptions(d)) {
                int best = -1;
                float bv = -1e9f;
                for (const Option& o : d.options) {
                    if (!o.enabled || o.id < 0 || o.id >= int(g.planets.size())) continue;
                    const PlanetState& ps = g.planets[size_t(o.id)];
                    float v = 0.3f * float(planetResources(g, o.id) + planetInfluence(g, o.id));
                    if (ps.units[size_t(p)].count(UnitType::Mech) == 0) v += 1.f;
                    if (ps.units[size_t(p)].groundForces() == 0) v += 1.5f;
                    for (int16_t a : g.systems[size_t(ps.system)].adj)
                        if (otherShipsIn(g, p, a)) v += 0.8f;
                    if (v > bv) bv = v, best = o.id;
                }
                out = one(best >= 0 ? best : firstNonNegative(d));
                return true;
            }
            // Inheritance Systems when the prerequisites could be covered anyway (0.44.9): only when the specialty planets
            // it saves are worth more than the 2 resources
            if (isYesNoOptions(d) && d.prompt.find("Exhaust Inheritance Systems and spend 2 more") != std::string::npos) {
                const size_t at = d.prompt.find("prerequisite of ");
                int t = -1;
                if (at != std::string::npos) {
                    std::string name = d.prompt.substr(at + 16);
                    if (!name.empty() && name.back() == '?') name.pop_back();
                    for (int i = 0; i < kTechCount && t < 0; ++i)
                        if (name == kTechs[i].name) t = i;
                }
                int lost = 0;
                if (t >= 0) {
                    int m[4];
                    missingPrereqs(g, p, t, m);
                    for (int c = 0; c < 4; ++c) {
                        const std::vector<int> sp = skipPlanets(g, p, TechColor(c));
                        for (int i = 0; i < m[c] && i < int(sp.size()); ++i) lost += planetResources(g, sp[size_t(i)]);
                    }
                }
                out = one(lost >= 3 ? 1 : 0);
                return true;
            }
            // Duranium Armor (0.44.9): repair the most valuable unit
            if (d.prompt.find("Duranium Armor: repair which") != std::string::npos ||
                d.prompt.find("extra bombardment") != std::string::npos) {
                const bool dur = d.prompt[0] == 'D';
                int best = firstNonNegative(d);
                float bv = -1e9f;
                for (const Option& o : d.options) {
                    if (!o.enabled || o.id < 0) continue;
                    float v = 0.f;
                    if (dur) {
                        if (o.id < kUnitTypes) v = float(unitCost2(g, p, UnitType(o.id)));
                    } else if (o.id < int(g.planets.size())) {  // the extra bombardment dice (0.44.9): the most defenders
                        for (int q = 0; q < kSides; ++q)
                            if (q != p) v += float(g.planets[size_t(o.id)].units[size_t(q)].groundForces());
                    }
                    if (v > bv) bv = v, best = o.id;
                }
                out = one(best);
                return true;
            }
            // AI Development Algorithm (0.44.8: it was spent on the first production of the round, however small): on
            // a production big enough to use the discount, else kept for a unit upgrade's prerequisite
            if (isYesNoOptions(d) && d.prompt.find("AI Development Algorithm") != std::string::npos) {
                const int sys = g.activeSystem;
                const int cap = sys >= 0 ? productionCapacity(g, p, sys) : 0;
                out = one(cap >= 3 || g.players[size_t(p)].passed || g.phase != Phase::Action ? 1 : 0);
                return true;
            }
            if (isYesNoOptions(d) && d.prompt.find("Transit Diodes") != std::string::npos) {  // (0.44.5)
                out = one(transitPlan(g, p).to >= 0 ? 1 : 0);
                return true;
            }
            // Predictive Intelligence (0.44.5): rearrange when our tactic tokens ran dry and the other pools can spare
            // some (it is readied again in the status phase, before the agenda phase votes)
            if (isYesNoOptions(d) && d.prompt.find("Predictive Intelligence") != std::string::npos &&
                d.prompt.find("rearrange") != std::string::npos) {
                Decision t;
                t.player = p;
                t.redistribute = true;
                t.newTokens = 0;
                t.value = lawInPlay(g, "regulations") ? 4 : 99;
                const Answer a = aiTokens(g, t);
                const int now = g.players[size_t(p)].tactic;
                out = one(a.data.size() == 3 && (a.data[0] >= now + 2 || (now == 0 && a.data[0] >= 1)) ? 1 : 0);
                return true;
            }
            // Graviton Laser System (0.44.5): only worth the exhaust when the target has fighters to shield real ships
            if (isYesNoOptions(d) && d.prompt.find("Graviton") != std::string::npos && d.targetPlayer >= 0 &&
                d.targetPlayer < kSides && d.value >= 0 && d.value < int(g.systems.size())) {
                const Force& f = g.systems[size_t(d.value)].space[size_t(d.targetPlayer)];
                out = one(f.count(UnitType::Fighter) > 0 && f.nonFighterShips() > 0 ? 1 : 0);
                return true;
            }
            // Bio-Stims (0.44.6): what is worth having ready again this action phase (the status phase readies all
            // anyway) - a technology with another use this round, else the best planet; nothing after passing
            if (d.prompt.find("Bio-Stims") != std::string::npos) {
                static const struct {
                    const char* key;
                    float v;
                } kAgain[] = {{"sr", 3.f},  {"aida", 2.5f}, {"pm", 2.5f}, {"executiveorder", 2.f}, {"gls", 1.5f}, {"pi", 1.5f},
                              {"td", 1.f},  {"htp", 1.5f},  {"qdn", 1.f}, {"nf", 2.f},              {"scc", 1.5f}, {"dsaxisy", 1.5f},
                              {"dsnokay", 2.f}, {"x89_base", 1.5f}, {"md_base", 1.f}, {"wg", 1.5f}, {"is", 1.f}};
                const bool passed = g.players[size_t(p)].passed;
                int best = -1;
                float bv = 1.2f;
                for (const Option& o : d.options) {
                    if (!o.enabled || o.id < 0 || passed) continue;
                    float v = 0.f;
                    if (o.id >= 10000) {
                        const int t = o.id - 10000;
                        if (t < 0 || t >= kTechCount) continue;
                        v = 0.5f;
                        for (const auto& k : kAgain)
                            if (!std::strcmp(kTechs[t].key, k.key)) v = k.v;
                    } else if (o.id < int(g.planets.size())) {
                        v = float(planetResources(g, o.id)) + 0.5f * float(planetInfluence(g, o.id));
                    }
                    if (v > bv) {
                        bv = v;
                        best = o.id;
                    }
                }
                out = one(best);
                return true;
            }
            if (isYesNoOptions(d)) {
                bool no = d.prompt.find("rearrange") != std::string::npos;
                out = one(no ? 0 : 1);
                return true;
            }
            out = one(firstNonNegative(d));
            return true;
        }
        default: return false;
    }
}

}  // namespace ti
