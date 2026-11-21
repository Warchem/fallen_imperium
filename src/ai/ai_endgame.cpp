// The endgame (0.55.0, the owner's final bot pass): stop the winner and the last round all-in.
//
// Stop the winner: a player who can reach the victory-point goal at the coming scoring (what they score with is
// worked out from what anyone at the table can see - their points, the public objectives they meet now, Imperial in
// hand with Mecatol Rex, and the secrets they hold: half a point, since nobody knows them) is "the threat". For each
// of their planets and each system with their ships the bot tries taking it away (on the board as it is, put back at
// once) and sees whether they would still get there: what breaks their win is worth a lot to take.
//
// The last round: when somebody will very likely reach the goal at this round's scoring, nothing is worth keeping for
// later - the bots fight at shorter odds, play their cards, use every tactical action, and stop gathering for later.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ai/ai_internal.h"
#include "engine/data.h"
#include "engine/query.h"

namespace ti {

namespace {

bool holdsMecatol(const Game& g, int q) {
    const int mec = g.mecatol();
    if (mec < 0) return false;
    for (int16_t pl : g.systems[size_t(mec)].planets)
        if (g.planets[size_t(pl)].owner == q) return true;
    return false;
}

bool imperialReady(const Game& g, int q) {
    const Player& pl = g.players[size_t(q)];
    return (g.phase == Phase::Action || g.phase == Phase::Strategy) && pl.holdsCard(7) && !((pl.cardsUsed >> 7) & 1);
}

// (0.74.0, the A.I. difficulty's memory of the table) The chance that player q holds a secret objective it can score
// now, as `viewer` can judge it: q's unscored secrets are some of the secrets viewer hasn't seen (the secret deck and
// every other player's unscored secrets - their union is public knowledge: everything not yet revealed), so each one
// is met with the share of that pool q meets on the board as it stands.
float secretChance(const Game& g, int q, int viewer) {
    int held = 0;
    for (int16_t o : g.players[size_t(q)].secrets)
        if (std::find(g.players[size_t(q)].scored.begin(), g.players[size_t(q)].scored.end(), o) == g.players[size_t(q)].scored.end()) ++held;
    if (held == 0) return 0.f;
    std::vector<int16_t> pool = g.secretDeck;
    for (int r = 0; r < g.numPlayers; ++r)
        if (r != viewer) pool.insert(pool.end(), g.players[size_t(r)].secrets.begin(), g.players[size_t(r)].secrets.end());
    if (pool.empty()) return 0.f;
    std::sort(pool.begin(), pool.end());
    int met = 0;
    for (int16_t o : pool)
        if (meetsObjective(g, q, o)) ++met;
    const float f = float(met) / float(pool.size());
    float none = 1.f;
    for (int i = 0; i < held; ++i) none *= 1.f - f;
    return 1.f - none;
}

}  // namespace

// The points q can expect at the coming scoring, from what everyone can see (p's own secrets count exactly for p).
float aiReach(const Game& g, int q, int viewer) {
    const Player& pl = g.players[size_t(q)];
    float reach = float(pl.vp);
    if (pl.eliminated) return 0.f;
    // public objectives they meet now and haven't scored: one at the status phase (the best), a second with Imperial
    int best = 0, second = 0;
    for (int16_t o : g.publicObjectives) {
        if (std::find(pl.scored.begin(), pl.scored.end(), o) != pl.scored.end()) continue;
        if (objectiveProgress(g, q, o).missing() > 0) continue;
        const int pts = kObjectives[size_t(o)].points;
        if (pts > best) second = best, best = pts;
        else if (pts > second) second = pts;
    }
    if (pl.noPublics) best = second = 0;  // (The Silver Flame)
    reach += float(best);
    const bool imperial = imperialReady(g, q);
    if (imperial) reach += float(second) + (holdsMecatol(g, q) ? 1.f : 0.f);
    // (the strategy phase: Imperial still on the table - whoever holds Mecatol Rex may take it)
    else if (g.phase == Phase::Strategy && g.cardOwner[7] < 0 && holdsMecatol(g, q)) reach += float(second) + 1.f;
    // secrets: known to their holder only
    int unscored = 0;
    for (int16_t o : pl.secrets)
        if (std::find(pl.scored.begin(), pl.scored.end(), o) == pl.scored.end()) {
            ++unscored;
            if (q == viewer && objectiveProgress(g, q, o).missing() == 0) reach += float(kObjectives[size_t(o)].points) * 0.9f, unscored = -99;
        }
    if (q != viewer && unscored > 0) {
        // the A.I. difficulty (0.74.0) weighs what q could score with what is on the board; the others count half a point
        if (viewer >= 0 && aiIsAISeat(g, viewer) && aiTune("aisecret", 1.f) > 0.f) reach += secretChance(g, q, viewer);
        else reach += aiTune("reachsecret", 0.5f);
    }
    return reach;
}

const Endgame& aiEndgame(const Game& g, int p) {
    static Endgame cache[8];
    static uint64_t key[8] = {};
    static const Game* owner[8] = {};
    static Endgame none;
    if (p < 0 || p >= 8 || p >= g.numPlayers) return none;
    uint64_t k = 1469598103934665603ull;
    auto mixIn = [&](uint64_t v) { k = (k ^ v) * 1099511628211ull; };
    mixIn(uint64_t(g.round));
    mixIn(uint64_t(g.phase));
    mixIn(g.log.size());
    mixIn(uint64_t(g.activePlayer + 1));
    for (const Player& pl : g.players) mixIn(uint64_t(pl.vp) * 977u + pl.scored.size() + 7u * pl.cardsUsed);
    if (owner[p] == &g && key[p] == k) return cache[p];
    owner[p] = &g;
    key[p] = k;
    AiSeat seat(p);
    Endgame& E = cache[p];
    E = Endgame();
    if (aiLevelScale(g) <= 0.f || aiTune("endgame", 1.f) <= 0.f || g.phase == Phase::Setup) return E;
    const float goal = float(g.vpGoal);
    E.myReach = aiReach(g, p, p);
    float top = E.myReach;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p || g.players[size_t(q)].eliminated) continue;
        const float r = aiReach(g, q, p);
        top = std::max(top, r);
        if (r >= goal - aiTune("reachslack", 0.4f) && r > E.threatReach) {
            E.threat = q;
            E.threatReach = r;
        }
    }
    E.lastRound = aiTune("lastround", 1.f) > 0.f && top >= goal - 0.4f;
    if (E.threat < 0 || aiTune("stopwinner", 1.f) <= 0.f) return E;
    // what takes their win away: each of their planets, each system with their ships - tried on the board as it is
    const int q = E.threat;
    E.planet.assign(g.planets.size(), 0.f);
    E.system.assign(g.systems.size(), 0.f);
    Game& mg = const_cast<Game&>(g);  // (changed and put back at once: the game waits for this answer)
    const float base = E.threatReach;
    const float big = kVP * aiTune("denyv", 2.f) * aiLevelScale(g);
    auto worth = [&](float after) {
        if (after < goal - aiTune("reachslack", 0.4f)) return big;  // their win is gone
        return after < base ? big * 0.3f * (base - after) : 0.f;   // ... or smaller
    };
    for (size_t pl = 0; pl < g.planets.size(); ++pl) {
        if (g.planets[pl].owner != q || g.planets[pl].system < 0) continue;
        mg.planets[pl].owner = -1;
        const float after = aiReach(g, q, p);
        mg.planets[pl].owner = int8_t(q);
        E.planet[pl] = worth(after);
    }
    for (size_t s = 0; s < g.systems.size(); ++s) {
        if (g.systems[s].space[size_t(q)].nonFighterShips() == 0) continue;
        const Force keep = g.systems[s].space[size_t(q)];
        mg.systems[s].space[size_t(q)] = Force();
        const float after = aiReach(g, q, p);
        mg.systems[s].space[size_t(q)] = keep;
        E.system[s] = worth(after);
    }
    static const bool debug = std::getenv("TI4_ENDDEBUG") != nullptr;  // (test only)
    if (debug) {
        static int lastRound[8] = {}, lastThreat[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
        if (lastRound[p] != g.round || lastThreat[p] != q) {
            lastRound[p] = g.round;
            lastThreat[p] = q;
            int crit = 0;
            for (float v : E.planet) crit += v >= big ? 1 : 0;
            int critS = 0;
            for (float v : E.system) critS += v >= big ? 1 : 0;
            std::fprintf(stderr, "endgame p%d r%d: threat p%d reach %.1f (vp %d), %d planets / %d systems break it; mine %.1f\n", p, g.round, q,
                         double(base), int(g.players[size_t(q)].vp), crit, critS, double(E.myReach));
        }
    }
    return E;
}

}  // namespace ti
