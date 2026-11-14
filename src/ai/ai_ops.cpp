// Plan Ahead (0.53.0, task #247 - owner: bots that keep an operation across turns and rounds: a goal, the fleet and
// ground forces gathered for it, the move in, the hold). Before this the move planner looked at one turn only: a
// target out of this turn's reach (Styx, three moves inside the Fracture behind two neutral dreadnoughts) was never
// worked toward - in 27 test games with Styx on the board nobody fought for it once.
//
// The bots keep no memory of their own (a game loaded from a save replays its answers, it never asks the bots
// again): the operation is worked out from the board each time, and what has gathered for it already makes the same
// choice win again - an operation sticks once it has begun.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ai/ai_internal.h"
#include "engine/data.h"
#include "engine/personality.h"
#include "engine/query.h"

namespace ti {

float fightOdds(const Game& g, int att, const Force& a, int def, const Force& d, bool space);  // (ai.cpp)

namespace {

bool opsOn(const Game& g) { return aiLevelScale(g) > 0.f && aiTune("ops", 1.f) > 0.f; }

// moves from each system to the target for p's ships (plain moves, anomalies as the route search treats them), up
// to kRange; -1 beyond
constexpr int kRange = 6;
std::vector<int8_t> movesTo(const Game& g, int p, int target) {
    std::vector<int8_t> d(g.systems.size(), -1);
    std::vector<int> q{target};
    d[size_t(target)] = 0;
    for (size_t qi = 0; qi < q.size(); ++qi) {
        const int u = q[qi];
        if (d[size_t(u)] >= kRange) continue;
        // (the way out of u toward the target is the way in from it: the board's steps go both ways)
        for (int v : routeNeighbors(g, p, u)) {
            if (v < 0 || v >= int(g.systems.size()) || d[size_t(v)] >= 0) continue;
            // a ship passes through v on its way: blocked by other players' ships (it can still start there)
            d[size_t(v)] = int8_t(d[size_t(u)] + 1);
            if (passableFor(g, p, v)) q.push_back(v);
        }
    }
    return d;
}

int moveOf(const Game& g, int p, int sys, UnitType t) { return std::max(1, moveValue(g, p, sys, t)) + (moveBoost(g, p) ? 1 : 0); }

// what defends the target: the ships of whoever holds it (or neutral guards)
int defenderOf(const Game& g, int p, int sys) {
    const SystemState& s = g.systems[size_t(sys)];
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && s.space[size_t(q)].nonFighterShips() + s.space[size_t(q)].count(UnitType::Fighter) > 0) return q;
    if (s.space[size_t(kNeutral)].ships() > 0) return kNeutral;
    for (int16_t pl : s.planets)
        if (g.planets[size_t(pl)].owner >= 0 && g.planets[size_t(pl)].owner != p) return g.planets[size_t(pl)].owner;
    return -1;
}

// How many more moves a ship of type t in s needs before the target is in its reach: 0 (in reach this turn), 1, 2;
// -1 farther (or no way)
// (boosted: it needs the +1 move of Gravity Drive and the like - only so many ships get it each move)
int tierOf(const Game& g, int p, const std::vector<int8_t>& dist, int target, int s, UnitType t, bool* boosted = nullptr) {
    if (boosted) *boosted = false;
    const int d = dist[size_t(s)];
    if (d < 1 || s == target) return -1;
    const int mv = moveOf(g, p, s, t);
    if (d <= mv) {
        const Reach r = shipReach(g, p, s, target, t, moveBoost(g, p) ? 1 : 0);
        if (boosted) *boosted = r.ok && r.needsBonus;
        // (Competitive: transports never go through a gravity rift - as the planner)
        if (r.ok && r.viaRift && aiCompetitive(g) && unitDef(g, p, t).capacity > 0) return -1;
        return r.ok ? 0 : 1;
    }
    const int extra = (d - mv + mv - 1) / mv;
    return extra <= 2 ? extra : -1;
}

bool foesNear(const Game& g, int p, int sys, int steps) {
    for (int s : systemsWithin(g, sys, steps))
        if (otherShipsIn(g, p, s)) return true;
    return false;
}

// (0.54.0, owner: "spare ships only") the non-fighter ships that stay where they are whatever the operation: a home
// guard (more with enemies about and for personalities that guard home), the guard of a seat we hold (Mecatol Rex,
// Styx), a floating factory's escort
int keepIn(const Game& g, int p, int s) {
    const Force& f = g.systems[size_t(s)].space[size_t(p)];
    const int n = f.nonFighterShips();
    if (n == 0) return 0;
    int keep = 0;
    if (g.systems[size_t(s)].home == p) {
        keep = n > 1 ? 1 : 0;
        if (aiTune("opspare", 1.f) > 0.f) {
            const Style& S = aiStyle(g, p);
            if (S.on) keep = std::max(keep, std::min(n - 1, S.homeGuard));
            if (foesNear(g, p, s, 2)) keep = std::max(keep, std::min(n, 2));
        }
    } else if (aiTune("opspare", 1.f) > 0.f) {
        if (aiSeatHeldIn(g, p, s)) keep = std::max(keep, std::min(n, foesNear(g, p, s, 2) ? 2 : 1));
        if (f.count(UnitType::SpaceDock) && unitDef(g, p, UnitType::SpaceDock).spaceOnly && foesNear(g, p, s, 2)) keep = std::max(keep, 1);
    }
    return keep;
}

// Our ships for the operation by how soon they can strike (tier 0: this turn; 1, 2: after that many more moves).
// Fighters and ground forces ride along as far as the ships have room (ground forces from our planets in the system
// they leave too - one stays on each).
struct Muster {
    Force tier[3];
    Force ready;                  // tier 0 without a command token on them: can go now
    std::vector<int8_t> tierAt;   // per system: the soonest tier of our ships there (-1 none)
    int ships[3] = {}, readyShips = 0;
};
Muster muster(const Game& g, int p, int target, const std::vector<int8_t>& dist) {
    Muster m;
    m.tierAt.assign(g.systems.size(), -1);
    int boostLeft = lightningDrives(g, p) ? 99 : boostShips(g, p);
    for (size_t s = 0; s < g.systems.size(); ++s) {
        const Force& f = g.systems[s].space[size_t(p)];
        if (f.nonFighterShips() == 0 || dist[s] < 1) continue;
        const bool token = g.systems[s].hasToken(p);
        int cap[3] = {};
        Force by[3];
        // (what stays: warships without capacity first - the carriers go and take what they hold)
        int kept = keepIn(g, p, int(s));
        int keptOf[kUnitTypes] = {};
        for (int pass = 0; pass < 2 && kept > 0; ++pass)
            for (int t : kShipIdx) {
                if (!f.n[t] || t == int(UnitType::Fighter) || kept <= 0) continue;
                if ((unitDef(g, p, UnitType(t)).capacity <= 0) != (pass == 0)) continue;
                const int k = std::min<int>(kept, f.n[t] - keptOf[t]);
                keptOf[t] += k;
                kept -= k;
            }
        for (int t : kShipIdx) {
            if (!f.n[t] || t == int(UnitType::Fighter)) continue;
            int k = f.n[t] - keptOf[t];
            bool boosted = false;
            const int tr = k > 0 ? tierOf(g, p, dist, target, int(s), UnitType(t), &boosted) : -1;
            if (tr < 0) continue;
            if (boosted && k > boostLeft) {  // (the rest a move short)
                const int rest = k - std::max(0, boostLeft);
                by[1].add(UnitType(t), rest);
                cap[1] += rest * unitDef(g, p, UnitType(t)).capacity;
                m.ships[1] += rest;
                k -= rest;
                if (m.tierAt[s] < 0 || 1 < m.tierAt[s]) m.tierAt[s] = 1;
                if (k <= 0) continue;
            }
            if (boosted) boostLeft -= k;
            by[tr].add(UnitType(t), k);
            cap[tr] += k * unitDef(g, p, UnitType(t)).capacity;
            m.ships[tr] += k;
            if (m.tierAt[s] < 0 || tr < m.tierAt[s]) m.tierAt[s] = int8_t(tr);
        }
        int fi = f.count(UnitType::Fighter), gf = f.groundForces();
        for (int16_t pl : g.systems[s].planets)
            if (g.planets[size_t(pl)].owner == p) gf += std::max(0, g.planets[size_t(pl)].units[size_t(p)].groundForces() - 1);
        for (int tr = 0; tr < 3; ++tr) {
            const int a = std::min(fi, cap[tr]);
            if (a > 0) by[tr].add(UnitType::Fighter, a);
            fi -= a;
            const int b = std::min(gf, cap[tr] - a);
            if (b > 0) by[tr].add(UnitType::Infantry, b);
            gf -= b;
            for (int t = 0; t < kUnitTypes; ++t)
                if (by[tr].n[t]) m.tier[tr].add(UnitType(t), by[tr].n[t]);
        }
        if (!token)
            for (int t = 0; t < kUnitTypes; ++t)
                if (by[0].n[t]) m.ready.add(UnitType(t), by[0].n[t]);
        if (!token) m.readyShips += by[0].nonFighterShips();
    }
    return m;
}

// the ground forces a landing on the target needs (twice the defenders, +1 - on its easiest planet)
int groundNeed(const Game& g, int p, int sys) {
    int best = 0;
    for (int16_t pl : g.systems[size_t(sys)].planets) {
        if (g.planets[size_t(pl)].owner == p || g.planets[size_t(pl)].destroyed) continue;
        int def = neutralGroundOn(g, pl);
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p) def += g.planets[size_t(pl)].units[size_t(q)].groundForces();
        const int need = 2 * def + 1;
        if (best == 0 || need < best) best = need;
    }
    return best;
}

struct Cand {
    int kind, target, foe;
    float value;
};

}  // namespace

const AiOp& aiOp(const Game& g, int p) {
    static AiOp cache[8];
    static uint64_t key[8] = {};
    static const Game* owner[8] = {};
    static AiOp none;
    if (p < 0 || p >= 8 || p >= g.numPlayers) return none;
    // (the same fingerprint as the goals: the board changes with every logged step)
    uint64_t k = 1469598103934665603ull;
    auto mixIn = [&](uint64_t v) { k = (k ^ v) * 1099511628211ull; };
    mixIn(uint64_t(g.round));
    mixIn(uint64_t(g.phase));
    mixIn(g.log.size());
    mixIn(uint64_t(g.activePlayer + 1));
    mixIn(uint64_t(g.activeSystem + 1));
    for (size_t s = 0; s < g.systems.size(); ++s) {  // (where the ships are, and our command tokens)
        uint64_t v = g.systems[s].hasToken(p) ? 1 : 0;
        for (int q = 0; q < g.numPlayers; ++q) v = v * 31 + uint64_t(g.systems[s].space[size_t(q)].ships() + 7 * g.systems[s].space[size_t(q)].groundForces());
        v = v * 31 + uint64_t(g.systems[s].space[size_t(kNeutral)].ships());
        if (v) mixIn(v * (s + 1));
    }
    if (owner[p] == &g && key[p] == k) return cache[p];
    owner[p] = &g;
    key[p] = k;
    AiSeat seat(p);
    AiOp& op = cache[p];
    op = AiOp();
    if (!opsOn(g) || g.phase != Phase::Action) return op;
    const bool early = g.round < int(aiTune("opround", 2.f));  // (round 1: only the custodians' staging)
    const Style& S = aiStyle(g, p);
    const Goals& G = aiGoals(g, p);
    const float level = aiLevelScale(g);
    std::vector<Cand> cands;
    // The custodians (0.54.0): a loaded ship in reach of Mecatol Rex for the round we can pay their 6 influence - the
    // landing itself is the planner's (it only lands with the influence ready)
    // (off - TI4_AITUNE opcust=1 for tests: in 0.54.0's sims it made the first landing later, 80% -> 72% by round 3 -
    // the soft hold kept carriers from the slice; the planner's own custodians staging does better)
    if (const int mec = g.mecatol(); mec >= 0 && !g.custodiansRemoved && aiTune("opcust", 0.f) > 0.f && !aiPromiseForbids(g, p, mec)) {
        int infAll = 0;
        for (const PlanetState& ps : g.planets)
            if (ps.owner == p) infAll += planetInfluence(g, int(&ps - &g.planets[0]));
        infAll += g.players[size_t(p)].tradeGoods;
        if (infAll >= aiCustodiansInf(g, p)) {
            const int foe = defenderOf(g, p, mec);
            cands.push_back({kOpCustodians, mec, foe, kVP * aiTune("opcust", 0.f) * (S.on ? std::max(0.7f, std::min(1.6f, S.mecatol)) : 1.f)});
        }
    }
    // Gathering before a big fight (0.54.0): another player's fleet of 4+ ships next to our home, or two moves from it
    if (!early && aiTune("opfleet", 0.5f) > 0.f) {
        const int home = g.players[size_t(p)].homeSystem;
        if (home >= 0)
            for (int s : systemsWithin(g, home, 2)) {
                if (s == home) continue;
                for (int q = 0; q < g.numPlayers; ++q) {
                    if (q == p) continue;
                    const Force& f = g.systems[size_t(s)].space[size_t(q)];
                    if (f.nonFighterShips() < 4 || aiPromiseForbids(g, p, s)) continue;
                    int cost2 = 0;
                    for (int t : kShipIdx)
                        if (f.n[t]) cost2 += f.n[t] * unitCost2(g, q, UnitType(t));
                    const bool next = adjacentSystems(g, s, home);
                    float v = kVP * aiTune("opfleet", 0.5f) * (next ? 1.f : 0.6f) * std::min(2.f, float(cost2) / 24.f);
                    if (S.on) v *= std::max(0.5f, std::min(1.5f, S.bigFights)) * std::max(0.5f, std::min(1.5f, S.vs[size_t(q)]));
                    cands.push_back({kOpFleet, s, q, v});
                }
            }
    }
    // Styx (Thunder's Edge): a victory point while held
    if (const int sx = aiStyxPlanet(g); sx >= 0 && g.planets[size_t(sx)].owner != p) {
        const int ss = g.planets[size_t(sx)].system;
        const int foe = defenderOf(g, p, ss);
        if (!aiPromiseForbids(g, p, ss)) cands.push_back({kOpStyx, ss, foe, kVP * aiTune("opstyx", 2.5f)});
    }
    // Mecatol Rex held by another player's fleet (the custodians gone) - the king of the hill (0.54.0, owner: "part of
    // the charm ... 2 factions fight over it, with a third threatening or taking it over"; the game's "Mecatol Rex:
    // Contested" setting, extras bit 128). Worth what it is worth through Imperial: a point each time its holder plays
    // Imperial - ours if we take it (we hold Imperial now, or may in the rounds left), the holder's that we stop (more
    // when the holder leads). 0.53.0 retook it for its own sake, and those bots fell behind.
    if (const int mec = g.mecatol(); mec >= 0 && g.custodiansRemoved && (g.extras & 128) && aiTune("opmecon", 1.f) > 0.f &&
                                      !aiPromiseForbids(g, p, mec)) {
        int holder = -1;
        for (int16_t pl : g.systems[size_t(mec)].planets)
            if (g.planets[size_t(pl)].owner >= 0) holder = g.planets[size_t(pl)].owner;
        const int foe = defenderOf(g, p, mec);
        if (holder >= 0 && holder != p && foe >= 0 && foe < g.numPlayers) {
            const float v = aiMecatolHill(g, p, holder);
            if (v > 0) cands.push_back({kOpMecatol, mec, foe, v});
        }
    }
    // a home system: Kingslay against its holder, a secret won in another player's home, Supremacy
    bool wantsHome = false;
    for (int16_t o : g.players[size_t(p)].secrets)
        if (!std::strcmp(kObjectives[o].key, "dts")) wantsHome = true;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p) continue;
        const int hs = g.players[size_t(q)].homeSystem;
        if (hs < 0 || g.systems[size_t(hs)].offBoard || aiPromiseForbids(g, p, hs)) continue;
        if (isFaction(g, q, "crimson")) continue;  // (Sundered: units die there)
        float v = 0;
        if (S.on && S.betrayTarget == q && S.betrayKind == int(Betrayal::Kingslay)) v += kVP * 0.8f * std::min(1.5f, aiBetrayUrge(g, p));
        if (wantsHome && level >= 0.99f) v += kVP * 0.9f;  // (won by the space combat itself)
        if (G.otherHome > 0) v += G.otherHome * 0.6f;      // (a planet there: the landing comes after)
        // (Sardakk review, owner 2026-11-11) Hive Command: "masses ground forces for a late push on a home system" - the
        // trait's target home (the leader, a neighbour, from round 5) is a campaign of its own
        if (S.on && S.homeHitTarget == q && fquirkIs(g, p, "sardakk", 1)) v += kVP * aiTune("hivepush", 0.f);
        if (v <= 0) continue;
        v = std::min(v, kVP * aiTune("ophomecap", 1.5f));  // (about one objective's worth)
        cands.push_back({kOpHome, hs, defenderOf(g, p, hs), v});
    }
    if (early) cands.erase(std::remove_if(cands.begin(), cands.end(), [](const Cand& c) { return c.kind != kOpCustodians; }), cands.end());
    static const bool debug = std::getenv("TI4_OPDEBUG") != nullptr;  // (test only)
    float bestScore = 0;
    for (Cand& c : cands) c.value *= level;  // (Normal: less of it)
    for (const Cand& c : cands) {
        AiOp o;
        o.kind = c.kind;
        o.target = c.target;
        o.foe = c.foe;
        o.value = c.value;
        o.dist = movesTo(g, p, c.target);
        if (c.foe >= 0) o.theirs = g.systems[size_t(c.target)].space[size_t(c.foe)];
        if (o.theirs.ships() == 0 && c.kind != kOpStyx && c.kind != kOpCustodians) continue;  // (no fleet to beat: the planner's own landing plans do it)
        o.gfNeed = groundNeed(g, p, c.target);
        const Muster m = muster(g, p, c.target, o.dist);
        for (int tr = 0; tr < 3; ++tr) o.tier[tr] = m.tier[tr];
        o.tierAt = m.tierAt;
        o.readiness = aiOpReadinessOf(g, p, o, o.tier);
        Force all = m.tier[0];
        for (int tr = 1; tr < 3; ++tr)
            for (int t = 0; t < kUnitTypes; ++t)
                if (m.tier[tr].n[t]) all.add(UnitType(t), m.tier[tr].n[t]);
        o.oddsReady = aiOpOdds(g, p, o, m.ready);
        o.oddsPosted = aiOpOdds(g, p, o, m.tier[0]);
        o.oddsLater = aiOpOdds(g, p, o, all);
        if (o.oddsLater < aiTune("opfeas", 0.6f)) {  // out of reach even gathered
            if (debug)
                std::fprintf(stderr, "op cand p%d r%d kind %d target %s: out of reach (%d/%d/%d ships, odds %.2f)\n", p, int(g.round), c.kind,
                             kSystems[g.systems[size_t(c.target)].def].name, m.ships[0], m.ships[1], m.ships[2], o.oddsLater);
            continue;
        }
        // worth: the prize, how sure the whole gathering would be, how far along it is (what is in place already
        // counts: the operation sticks once begun)
        const float score = c.value * std::min(1.f, o.oddsLater / 0.8f) * (aiTune("opbase", 0.5f) + o.readiness);
        if (debug)
            std::fprintf(stderr, "op cand p%d r%d kind %d target %s foe %d value %.1f ships %d/%d/%d (ready %d) odds %.2f/%.2f/%.2f readiness %.2f score %.1f\n",
                         p, int(g.round), c.kind, kSystems[g.systems[size_t(c.target)].def].name, c.foe, c.value, m.ships[0], m.ships[1], m.ships[2],
                         m.readyShips, o.oddsReady, o.oddsPosted, o.oddsLater, o.readiness, score);
        if (score <= bestScore) continue;
        bestScore = score;
        o.strike = o.oddsReady >= aiTune("opneed", 0.65f) && canActivate(g, p, c.target);
        if (c.kind == kOpMecatol) o.watch = aiOpWatchers(g, p, o, m.ready);
        op = o;
    }
    if (debug && g.phase == Phase::Action) {  // the pick, when it changes
        static int last[8] = {-9, -9, -9, -9, -9, -9, -9, -9};
        const int id = op.kind * 1000 + op.target;
        if (id != last[p])
            std::fprintf(stderr, "op pick p%d r%d kind %d target %s in reach %d odds %.2f/%.2f/%.2f readiness %.2f\n", p, int(g.round), op.kind,
                         op.target >= 0 ? kSystems[g.systems[size_t(op.target)].def].name : "-", op.tier[0].nonFighterShips(), op.oddsReady,
                         op.oddsPosted, op.oddsLater, op.readiness);
        last[p] = id;
    }
    return op;
}

// ---- The Saarball (the Saar review, owner 2026-11-13) --------------------------------------------------------------
// What a factory parked in sys brings within reach: other players' and free planets in it and next to it (theirs need
// a fight: less), Mecatol Rex there or next door; less where another of our docks already builds
float aiSaarFront(const Game& g, int p, int sys) {
    if (sys < 0 || sys >= int(g.systems.size())) return 0.f;
    float v = 0;
    auto around = [&](int a, float w) {
        for (int16_t pl : g.systems[size_t(a)].planets) {
            const PlanetState& ps = g.planets[size_t(pl)];
            if (ps.owner == p || ps.destroyed) continue;
            const float worth = (planetResources(g, pl) + planetInfluence(g, pl)) * 0.12f + 0.3f;
            v += worth * w * (ps.owner >= 0 ? 0.7f : 1.f);
        }
        if (a == g.mecatol() && !(g.planets.empty())) v += 1.5f * w;
    };
    around(sys, 1.2f);  // (in the system itself: the carriers there land without moving)
    bool dockNear = false;
    for (int16_t a : g.systems[size_t(sys)].adj) {
        if (a == sys || g.systems[size_t(a)].isHyperlane()) continue;
        around(a, 1.f);
        const Force& f = g.systems[size_t(a)].space[size_t(p)];
        if (f.count(UnitType::SpaceDock)) dockNear = true;
        for (int16_t pl : g.systems[size_t(a)].planets)
            if (g.planets[size_t(pl)].owner == p && g.planets[size_t(pl)].units[size_t(p)].count(UnitType::SpaceDock)) dockNear = true;
    }
    if (dockNear) v -= aiTune("sbdocknear", 0.8f);
    return v;
}

namespace {
// the best odds another player's ships that could reach sys next turn would have against what we keep there
float saarThreat(const Game& g, int p, int sys, const Force& ours) {
    if (ours.ships() == 0) return 1.f;
    float worst = 0.f;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p || g.players[size_t(q)].eliminated) continue;
        Force theirs;
        for (int s : systemsWithin(g, sys, 2)) {
            const Force& f = g.systems[size_t(s)].space[size_t(q)];
            if (f.nonFighterShips() == 0) continue;
            const bool beside = s == sys || adjacentSystems(g, s, sys);
            int cap = 0;
            for (int t : kShipIdx) {
                if (!f.n[t] || t == int(UnitType::Fighter)) continue;
                if (!beside && moveValue(g, q, s, UnitType(t)) < 2) continue;
                theirs.add(UnitType(t), f.n[t]);
                cap += f.n[t] * unitDef(g, q, UnitType(t)).capacity;
            }
            const int fi = std::min<int>(f.count(UnitType::Fighter), cap);
            if (fi > 0) theirs.add(UnitType::Fighter, fi);
        }
        if (theirs.nonFighterShips() == 0) continue;
        worst = std::max(worst, fightOdds(g, q, theirs, p, ours, true));
    }
    return worst;
}
}  // namespace

const SaarBall& aiSaarBall(const Game& g, int p) {
    static SaarBall cache[8];
    static uint64_t key[8] = {};
    static const Game* owner[8] = {};
    static SaarBall none;
    if (p < 0 || p >= 8 || p >= g.numPlayers || (aiTune("saarball3", 0.f) <= 0.f && aiTune("sbpull", 0.f) <= 0.f) || g.phase != Phase::Action || g.round < 2 ||
        aiLevelScale(g) <= 0.f || !unitDef(g, p, UnitType::SpaceDock).spaceOnly)
        return none;
    uint64_t k = 1469598103934665603ull;
    auto mixIn = [&](uint64_t v) { k = (k ^ v) * 1099511628211ull; };
    mixIn(uint64_t(g.round));
    mixIn(g.log.size());
    mixIn(uint64_t(g.activePlayer + 1));
    mixIn(uint64_t(g.activeSystem + 1));
    for (size_t s = 0; s < g.systems.size(); ++s) {
        uint64_t v = g.systems[s].hasToken(p) ? 1 : 0;
        for (int q = 0; q < g.numPlayers; ++q) v = v * 31 + uint64_t(g.systems[s].space[size_t(q)].ships());
        if (v) mixIn(v * (s + 1));
    }
    if (owner[p] == &g && key[p] == k) return cache[p];
    owner[p] = &g;
    key[p] = k;
    AiSeat seat(p);
    SaarBall& b = cache[p];
    b = SaarBall();
    // the ball: the factory with the strongest escort (cost of its non-fighter ships)
    const int home = g.players[size_t(p)].homeSystem;
    int bestEsc = 0;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        const Force& f = g.systems[s].space[size_t(p)];
        if (!f.count(UnitType::SpaceDock) || f.nonFighterShips() == 0) continue;
        if (int(s) == home && f.nonFighterShips() < 2 && foesNear(g, p, home, 2)) continue;  // (its one ship stays home)
        int esc = 0;
        for (int t : kShipIdx)
            if (t != int(UnitType::Fighter) && f.n[t]) esc += f.n[t] * unitCost2(g, p, UnitType(t));
        if (int(s) == home && foesNear(g, p, home, 1)) esc /= 2;  // (home under threat: the other one first)
        if (esc > bestEsc) bestEsc = esc, b.ball = int(s);
    }
    if (b.ball < 0) return b;
    const Force ballForce = g.systems[size_t(b.ball)].space[size_t(p)];
    // (the escort keeps up: the slowest of its non-fighter ships sets the pace)
    int ffMove = std::max(1, moveValue(g, p, b.ball, UnitType::SpaceDock));
    for (int t : kShipIdx)
        if (t != int(UnitType::Fighter) && ballForce.n[t]) ffMove = std::min(ffMove, std::max(1, moveValue(g, p, b.ball, UnitType(t))));
    // (no Gravity Drive +1: it boosts only so many ships, and the ball moves together)
    const std::vector<int8_t> fromBall = movesTo(g, p, b.ball);
    const bool chaos = ownsTechKey(g, p, "cm");
    const int maxSteps = int(aiTune("sbsteps", 4.f));
    float best = -1e9f;
    for (size_t t = 0; t < g.systems.size(); ++t) {
        const SystemState& ts = g.systems[t];
        const int d = fromBall[t];
        if (d < 0 || ts.isHyperlane() || ts.offBoard || int(t) == home || (ts.home >= 0 && ts.home != p)) continue;
        if (int(t) != b.ball && (otherShipsIn(g, p, int(t)) || !canEndMoveIn(g, p, int(t)))) continue;
        // (no parking in a gravity rift - leaving it rolls for the factory - or an entropic scar: no PRODUCTION there)
        if (anomaliesFor(g, p, int(t)) & (kRift | kScar)) continue;
        const int steps = (d + ffMove - 1) / ffMove;
        if (steps > maxSteps) continue;
        float v = aiSaarFront(g, p, int(t));
        if (chaos && (ts.d().anomalies & kAsteroid)) v += aiTune("sbrocks", 1.f);  // (Chaos Mapping: nobody can activate it)
        const float threat = saarThreat(g, p, int(t), ballForce);
        v *= 1.f - std::max(0.f, threat - aiTune("sbthreat", 0.35f));
        v -= aiTune("sbstep", 0.35f) * float(steps);
        if (int(t) == b.ball) v += aiTune("sbstay", 0.6f);  // (it sticks once there)
        if (v > best) best = v, b.front = int(t), b.score = v, b.steps = steps;
    }
    if (b.front < 0) return b;
    b.toFront = movesTo(g, p, b.front);
    if (b.front == b.ball || g.systems[size_t(b.ball)].hasToken(p)) return b;
    // this turn's step: the reachable system nearest the front (more within reach on a tie), safe to stop in
    const std::vector<int8_t>& toFront = b.toFront;
    const bool gd = moveBoost(g, p);
    int bestD = toFront[size_t(b.ball)] >= 0 ? toFront[size_t(b.ball)] : 99;
    float bestAround = -1e9f;
    for (size_t x = 0; x < g.systems.size(); ++x) {
        if (int(x) == b.ball || toFront[x] < 0 || fromBall[x] < 1 || fromBall[x] > ffMove) continue;
        if (!canActivate(g, p, int(x)) || g.systems[x].hasToken(p) || aiPromiseForbids(g, p, int(x))) continue;
        if (anomaliesFor(g, p, int(x)) & kRift) continue;  // (a rift on the way: the factory would roll to leave it)
        const Reach r = shipReach(g, p, b.ball, int(x), UnitType::SpaceDock, 0);
        if (!r.ok || r.viaRift || r.needsBonus) continue;
        (void)gd;
        if (otherShipsIn(g, p, int(x))) {  // (a fight on the way: only a sure one)
            int foe = -1;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && hasShips(g, q, int(x))) foe = q;
            if (foe < 0 && neutralShipsIn(g, int(x))) foe = kNeutral;
            if (foe < 0 || fightOdds(g, p, ballForce, foe, g.systems[x].space[size_t(foe)], true) < 0.85f) continue;
        }
        const float ar = aiSaarFront(g, p, int(x));
        if (toFront[x] < bestD || (toFront[x] == bestD && b.next >= 0 && ar > bestAround)) {
            bestD = toFront[x];
            bestAround = ar;
            b.next = int(x);
        }
    }
    static const bool debug = std::getenv("TI4_SAARDEBUG") != nullptr;  // (test only)
    if (debug)
        std::fprintf(stderr, "saarball p%d r%d ball %s front %s (%.1f, %d steps) next %s\n", p, int(g.round), kSystems[g.systems[size_t(b.ball)].def].name,
                     kSystems[g.systems[size_t(b.front)].def].name, b.score, b.steps, b.next >= 0 ? kSystems[g.systems[size_t(b.next)].def].name : "-");
    return b;
}

// What Mecatol Rex is worth to p taking it from its holder (0.54.0): the Imperial points it gives - p's own (Imperial in
// hand and not played yet: this round's point; later rounds: a share), the holder's that taking it stops (this round's if
// they hold Imperial unplayed - the timing; later ones), weighed by how much the holder's points matter (the leader's
// most), by how long p could hope to keep it, and by p's nature (Mecatol-minded, warlike - or cautious).
float aiMecatolHill(const Game& g, int p, int holder) {
    if (holder < 0 || holder >= g.numPlayers || holder == p) return 0.f;
    int lead = 0;
    for (int q = 0; q < g.numPlayers; ++q)
        if (!g.players[size_t(q)].eliminated) lead = std::max(lead, int(g.players[size_t(q)].vp));
    const float roundsLeft = std::max(0.5f, std::min(3.f, float(g.vpGoal - lead) / 2.f));
    auto impNow = [&](int q) {
        const Player& pl = g.players[size_t(q)];
        return g.phase == Phase::Action && pl.holdsCard(7) && !((pl.cardsUsed >> 7) & 1) ? 1.f : 0.f;
    };
    const float later = aiTune("ophillrate", 0.35f) * roundsLeft;  // (Imperial each later round: whoever holds Mecatol Rex wants it)
    const float ours = (impNow(p) + later) * aiTune("ophillkeep", 0.5f);  // (and we must hold it until then)
    const int hv = g.players[size_t(holder)].vp;
    float threat = hv >= lead - 1 ? aiTune("ophillleader", 0.6f) : aiTune("ophillother", 0.25f);
    // (0.54.0, habits: a person known to sit on Mecatol Rex game after game is contested harder)
    if (g.players[size_t(holder)].human && g.habits.games > 0 && g.habits.mecatol >= 6.f)
        threat *= 1.f + 0.3f * std::min(1.f, float(g.habits.games) / 3.f);
    const float theirs = (impNow(holder) * aiTune("ophillnow", 1.5f) + later) * threat;
    float v = kVP * (ours + theirs) * aiTune("ophill", 1.f);
    const Style& S = aiStyle(g, p);
    if (S.on) {
        v *= std::max(0.6f, std::min(1.6f, S.mecatol)) * std::max(0.7f, std::min(1.4f, S.aggression));
        v *= std::max(0.5f, std::min(1.5f, S.vs[size_t(holder)]));
        if (S.betrayTarget == holder && S.betrayKind == int(Betrayal::Kingslay)) v += kVP * 0.8f * std::min(1.5f, aiBetrayUrge(g, p));
    }
    return v;
}

namespace {
// the weakest ships go first (what a fight costs the winner, roughly)
Force afterLosses(const Game& g, int p, Force f, int losses) {
    while (losses > 0 && f.nonFighterShips() > 0) {
        if (f.count(UnitType::Fighter) > 0) {  // (fighters soak hits first)
            f.remove(UnitType::Fighter, 1);
            --losses;
            continue;
        }
        int worst = -1;
        for (int t : kShipIdx)
            if (t != int(UnitType::Fighter) && f.n[t] && (worst < 0 || unitCost2(g, p, UnitType(t)) < unitCost2(g, p, UnitType(worst)))) worst = t;
        if (worst < 0) break;
        f.remove(UnitType(worst), 1);
        --losses;
    }
    return f;
}
}  // namespace

// Check who's watching (0.54.0): the best odds a third player (not p, not the holder) would have next turn against what
// p's strike leaves at the target - its ships that reach the target now, against p's fleet after the fight's losses.
float aiOpWatchers(const Game& g, int p, const AiOp& op, const Force& strike) {
    if (op.kind == kOpNone || op.target < 0) return 0.f;
    // (each of their non-fighter ships costs us about one ship over the fight; sustain absorbs one more)
    int losses = op.theirs.nonFighterShips();
    for (int t : kShipIdx)
        if (op.foe >= 0 && op.foe < g.numPlayers && op.theirs.n[t] && unitDef(g, op.foe, UnitType(t)).sustain) losses += op.theirs.n[t] / 2;
    const Force left = afterLosses(g, p, strike, losses);
    if (left.nonFighterShips() == 0) return 0.f;
    float worst = 0.f;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p || q == op.foe || g.players[size_t(q)].eliminated) continue;
        Force theirs;
        for (size_t s = 0; s < g.systems.size(); ++s) {
            const Force& f = g.systems[s].space[size_t(q)];
            if (f.nonFighterShips() == 0 || int(s) == op.target || op.dist[s] < 1 || op.dist[s] > 3) continue;
            int cap = 0;
            for (int t : kShipIdx) {
                if (!f.n[t] || t == int(UnitType::Fighter)) continue;
                if (!shipReach(g, q, int(s), op.target, UnitType(t), moveBoost(g, q) ? 1 : 0).ok) continue;
                theirs.add(UnitType(t), f.n[t]);
                cap += f.n[t] * unitDef(g, q, UnitType(t)).capacity;
            }
            const int fi = std::min<int>(f.count(UnitType::Fighter), cap);
            if (fi > 0) theirs.add(UnitType::Fighter, fi);
        }
        if (theirs.nonFighterShips() == 0) continue;
        worst = std::max(worst, fightOdds(g, q, theirs, p, left, true));
    }
    return worst;
}

float aiOpOdds(const Game& g, int p, const AiOp& op, const Force& f) {
    if (op.kind == kOpNone) return 0.f;
    return op.theirs.ships() > 0 ? fightOdds(g, p, f, op.foe, op.theirs, true) : (f.nonFighterShips() ? 1.f : 0.f);
}

// How far f is toward a sure strike (0..1, about linear in the ships): the fight odds' strength ratio against the one a
// sure win needs (Lanchester: the ratio grows with the square of the fleet - hence the root)
float aiOpProgress(const Game& g, int p, const AiOp& op, const Force& f) {
    if (op.kind == kOpNone || f.nonFighterShips() == 0) return 0.f;
    const float ground = op.gfNeed > 0 ? std::min(1.f, float(f.groundForces()) / float(op.gfNeed)) : 1.f;
    if (op.theirs.ships() == 0) return ground;
    const float o = std::max(0.02f, std::min(0.98f, fightOdds(g, p, f, op.foe, op.theirs, true)));
    const float need = std::max(0.55f, std::min(0.95f, aiTune("opsure", 0.8f)));
    const float space = std::min(1.f, std::sqrt(std::sqrt((o / (1.f - o)) / (need / (1.f - need)))));
    const float gw = aiTune("opgroundw", 0.25f);
    return space * (1.f - gw) + ground * gw;
}

float aiOpReadinessOf(const Game& g, int p, const AiOp& op, const Force (&tier)[3]) {
    Force acc = tier[0];
    float prev = aiOpProgress(g, p, op, acc), r = prev;
    static const float w[3] = {1.f, 0.5f, 0.25f};
    for (int tr = 1; tr < 3; ++tr) {
        for (int t = 0; t < kUnitTypes; ++t)
            if (tier[tr].n[t]) acc.add(UnitType(t), tier[tr].n[t]);
        const float now = aiOpProgress(g, p, op, acc);
        r += w[tr] * std::max(0.f, now - prev);
        prev = std::max(prev, now);
    }
    return r;
}

float aiOpReadinessAfter(const Game& g, int p, const AiOp& op, int sys, const std::vector<int>& move) {
    if (op.kind == kOpNone || sys < 0 || sys >= int(op.dist.size())) return op.readiness;
    Force tier[3] = {op.tier[0], op.tier[1], op.tier[2]};
    int riderTo = -1;  // the soonest tier the moving ships with capacity reach
    for (size_t i = 0; i + 3 < move.size(); i += 4) {
        const UnitType t = UnitType(move[i + 2]);
        if (move[i + 1] >= 0 || !isShip(t) || t == UnitType::Fighter) continue;
        const int from = move[i], k = move[i + 3];
        const int was = tierOf(g, p, op.dist, op.target, from, t);
        if (was >= 0) tier[was].remove(t, k);
        const int to = sys == op.target ? 0 : tierOf(g, p, op.dist, op.target, sys, t);
        if (to >= 0) {
            tier[to].add(t, k);
            if (unitDef(g, p, t).capacity > 0 && (riderTo < 0 || to < riderTo)) riderTo = to;
        }
    }
    for (size_t i = 0; i + 3 < move.size(); i += 4) {
        const UnitType t = UnitType(move[i + 2]);
        if (!(t == UnitType::Fighter || isGroundForce(t))) continue;
        const int from = move[i], k = move[i + 3];
        const UnitType as = t == UnitType::Fighter ? UnitType::Fighter : UnitType::Infantry;  // (riders are counted as infantry)
        const int was = from >= 0 && from < int(op.tierAt.size()) ? op.tierAt[size_t(from)] : -1;
        if (was >= 0) tier[was].remove(as, k);
        if (riderTo >= 0) tier[riderTo].add(as, k);
    }
    return aiOpReadinessOf(g, p, op, tier);
}

}  // namespace ti
