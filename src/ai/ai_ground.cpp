// The groundwork (0.25.0) - what every good Twilight Imperium player does,
// whatever their style (the owner's tester: "a common strategic foundation"
// under the archetypes). It sits under the existing layers and changes none of
// them: the Strategy Core still suggests the faction's plan, the archetype /
// quirks / faction traits / evolution still shape how the bot plays it, the
// B-Factor still decides betrayals, and the difficulty still scales it all.
// The groundwork only adds values (and one planning step) to the plans those
// layers already compare:
//   * the slice: the planets whose nearest home is ours, claimed by the end of
//     round 2 (and not wandering into a neighbour's slice while ours is open);
//   * the equidistant systems: raced for, or split with the neighbour by a
//     deal (each promises to stay out of the other's pick);
//   * the custodians: the bot that can land on Mecatol Rex this turn keeps 6
//     influence for it, everyone else spends theirs (0.24 bots all held it back);
//     a loaded carrier is staged next to Mecatol Rex in rounds 2-3;
//   * plastic: carriers pick up ground forces on their way (LRR 95.1), the
//     home system is activated after the ships that must leave it, staging
//     moves keep tactic tokens from going to waste;
//   * tempo: one public objective a round - the nearest one gets the effort
//     (as Competitive did since 0.12.1).
// Each part has a weight (aiTune "g_*", tuned by self-play); the difficulty
// sets how much of it a bot follows: Easy loose, Competitive in full.
// Self-play (0.25.0, paired seats: the same games with the weight changed on
// half the seats, TI4_TUNESEATS + TI4_AITUNE2): the round order (g_lock) and
// the en-route pickups (g_route) win games; the slice / equidistant / foreign
// / custodians / split / tempo / staging weights came out even at 0.5x-2x
// and stay at 1; a rule against buying Leadership tokens with planets the
// home dock needs (g_plastic) lost clearly - a command token is worth more
// than the units - and is off. Groundwork seats against plain ones in the
// same games: ~55% of the wins at Normal, Hard and Competitive, 51% at Easy.
//
// Betrayal timing (owner, 0.25.0): below Competitive a bot acts on a betrayal
// objective at once, as willing as its personality is; Competitive has no
// personality weight but waits for a moment that doesn't wreck its economy.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <queue>

#include "ai/ai_internal.h"
#include "engine/data.h"
#include "engine/personality.h"
#include "engine/query.h"

namespace ti {

namespace {

// ------------------------------------------------------------------ the slices
struct Slices {
    uint64_t key = 0;
    std::vector<int8_t> owner;         // per system: slice owner, -2 equidistant, -1 none
    std::vector<uint8_t> tied;         // per system: bit per player tied for an equidistant
    std::vector<std::vector<int8_t>> dist;  // [player][system]: moves from that player's home (99 = no route)
};

const Slices& slices(const Game& g) {
    static Slices s;
    uint64_t key = (uint64_t(g.seed) << 20) ^ (uint64_t(g.systems.size()) << 4) ^ uint64_t(g.numPlayers);
    for (const Player& pl : g.players) key = key * 131 + uint64_t(pl.homeSystem + 1);
    // (systems that changed places - Riftwalker Meian - or tiles added in play: worked out again)
    for (const SystemState& sy : g.systems) key = key * 31 + uint64_t(uint32_t(sy.pos.q * 977 + sy.pos.r));
    if (s.key == key && s.owner.size() == g.systems.size()) return s;
    s.key = key;
    const int n = int(g.systems.size()), np = g.numPlayers;
    s.owner.assign(size_t(n), -1);
    s.tied.assign(size_t(n), 0);
    s.dist.assign(size_t(np), std::vector<int8_t>(size_t(n), 99));
    for (int p = 0; p < np; ++p) {
        const int h = g.players[size_t(p)].homeSystem;
        if (h < 0 || h >= n) continue;
        std::queue<int> q;
        s.dist[size_t(p)][size_t(h)] = 0;
        q.push(h);
        // a home beside the board (the Creuss, the Crimson Rebellion) counts from the systems it opens onto (0.55.0):
        // their slice is round the gate / the Sorrow, as it is for players at the table
        if (g.systems[size_t(h)].offBoard)
            for (int16_t y : g.systems[size_t(h)].adj) s.dist[size_t(p)][size_t(y)] = 0, q.push(y);
        while (!q.empty()) {
            const int x = q.front();
            q.pop();
            for (int16_t y : g.systems[size_t(x)].adj)
                if (s.dist[size_t(p)][size_t(y)] > s.dist[size_t(p)][size_t(x)] + 1) {
                    s.dist[size_t(p)][size_t(y)] = int8_t(s.dist[size_t(p)][size_t(x)] + 1);
                    q.push(y);
                }
        }
    }
    for (int x = 0; x < n; ++x) {
        const SystemState& sys = g.systems[size_t(x)];
        if (sys.home >= 0 || x == g.mecatol() || sys.planets.empty()) continue;
        int best = 99, who = -1, ties = 0;
        for (int p = 0; p < np; ++p) {
            const int d = s.dist[size_t(p)][size_t(x)];
            if (d < best) {
                best = d;
                who = p;
                ties = 1;
            } else if (d == best) {
                ++ties;
            }
        }
        if (best >= 99) continue;
        if (ties > 1) {
            s.owner[size_t(x)] = -2;
            for (int p = 0; p < np; ++p)
                if (s.dist[size_t(p)][size_t(x)] == best) s.tied[size_t(x)] |= uint8_t(1u << p);
        } else {
            s.owner[size_t(x)] = int8_t(who);
        }
    }
    return s;
}

// test only: TI4_SIMGROUNDSEATS=024 - only these seats follow the groundwork (head-to-head), "-" = nobody
int groundMask() {
    static int mask = [] {
        const char* s = std::getenv("TI4_SIMGROUNDSEATS");
        if (!s) return -1;
        int m = 0;
        for (const char* c = s; *c; ++c)
            if (*c >= '0' && *c <= '7') m |= 1 << (*c - '0');
        return m;
    }();
    return mask;
}

bool canLandOnMecatol(const Game& g, int p);

}  // namespace

int aiSliceOf(const Game& g, int sys) {
    const Slices& s = slices(g);
    return sys >= 0 && sys < int(s.owner.size()) ? s.owner[size_t(sys)] : -1;
}

bool aiSliceTied(const Game& g, int sys, int p) {
    const Slices& s = slices(g);
    return sys >= 0 && sys < int(s.tied.size()) && p >= 0 && p < 8 && ((s.tied[size_t(sys)] >> p) & 1);
}

int aiHomeMoves(const Game& g, int p, int sys) {
    const Slices& s = slices(g);
    if (p < 0 || p >= int(s.dist.size()) || sys < 0 || sys >= int(g.systems.size())) return 99;
    return s.dist[size_t(p)][size_t(sys)];
}

// How much of the groundwork this computer player follows (0 = none).
float aiGroundScale(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers || g.phase == Phase::Setup) return 0.f;
    const int m = groundMask();
    if (m >= 0 && !((m >> p) & 1)) return 0.f;
    AiSeat seat(p);  // its own difficulty (and tuning, in self-play tests)
    const int l = aiLevelOf(g);
    static const float kScale[4] = {0.35f, 0.7f, 0.85f, 1.f};
    return kScale[std::max(0, std::min(3, l))] * aiTune("ground", 1.f);
}

// The opening: rounds 1-2 in full, fading out by round 5.
float aiGroundOpening(const Game& g) {
    static const float k[6] = {1.f, 1.f, 1.f, 0.6f, 0.3f, 0.f};
    return k[std::max(0, std::min(5, g.round))];
}

const Ground& aiGround(const Game& g, int p) {
    static Ground cache[8];
    static size_t key[8] = {};
    static const Game* who[8] = {};
    static Ground none;
    if (p < 0 || p >= 8 || p >= g.numPlayers) return none;
    const size_t k = g.log.size() * 31 + size_t(g.phase) * 7 + size_t(g.round) * 1009 + size_t(g.activePlayer + 1);
    if (who[p] == &g && key[p] == k) return cache[p];
    who[p] = &g;
    key[p] = k;
    Ground& G = cache[p];
    G = Ground();
    G.scale = aiGroundScale(g, p);
    if (G.scale <= 0.f) return G;
    G.open = aiGroundOpening(g);
    const Slices& s = slices(g);
    for (size_t x = 0; x < g.systems.size() && x < s.owner.size(); ++x) {
        const bool mine = s.owner[x] == p, equi = s.owner[x] == -2 && ((s.tied[x] >> p) & 1);
        if (!mine && !equi) continue;
        for (int16_t pl : g.systems[x].planets) {
            if (g.planets[size_t(pl)].owner >= 0) continue;
            if (mine) ++G.sliceOpen;
            else ++G.equiOpen;
        }
    }
    G.custodian = !g.custodiansRemoved && g.mecatol() >= 0 && canLandOnMecatol(g, p);
    return G;
}

namespace {

// Ground forces + a ship that can carry them into Mecatol Rex this turn (or already there).
bool canLandOnMecatol(const Game& g, int p) {
    const int mec = g.mecatol();
    if (mec < 0 || !canActivate(g, p, mec)) return false;
    if (g.systems[size_t(mec)].space[p].groundForces() > 0) return true;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        if (int(s) == mec || g.systems[s].hasToken(p)) continue;
        const Force& f = g.systems[s].space[p];
        int gf = f.groundForces();
        for (int16_t pl : g.systems[s].planets) gf += g.planets[size_t(pl)].units[p].groundForces();
        if (gf <= 0) continue;
        for (int t : kShipIdx)
            if (f.n[t] && unitDef(g, p, UnitType(t)).capacity > 0 && shipReach(g, p, int(s), mec, UnitType(t), 0).ok) return true;
    }
    return false;
}

}  // namespace

bool aiCanLandOnMecatol(const Game& g, int p) { return canLandOnMecatol(g, p); }

// Extra value of an unclaimed planet: our slice first, the equidistants raced for,
// a neighbour's slice left alone while ours is open.
float aiGroundPlanet(const Game& g, int p, int pl) {
    const PlanetState& ps = g.planets[size_t(pl)];
    if (ps.owner >= 0) return 0.f;
    AiSeat seat(p);
    const Ground& G = aiGround(g, p);
    if (G.scale <= 0.f || G.open <= 0.f) return 0.f;
    const int sys = ps.system;
    if (sys == g.mecatol()) return 0.f;
    const float worth = 1.5f + 0.5f * float(planetResources(g, pl)) + 0.3f * float(planetInfluence(g, pl));
    const int owner = aiSliceOf(g, sys);
    const float k = G.scale * G.open;
    if (owner == p) return worth * k * aiTune("g_slice", 1.f);
    if (owner == -2 && aiSliceTied(g, sys, p)) {
        // a race: worth more while a tied neighbour could get there first
        float race = 1.f;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p && aiSliceTied(g, sys, q))
                for (int s2 : systemsWithin(g, sys, 1))
                    if (hasShips(g, q, s2)) race = 1.4f;
        return worth * k * race * aiTune("g_equi", 1.f);
    }
    if (owner >= 0 && owner != p && G.sliceOpen > 0) return -worth * k * 0.5f * aiTune("g_foreign", 1.f);
    return 0.f;
}

// ------------------------------------------------------------------ betrayal timing
namespace {
// how willing each archetype is to carry a betrayal out at once (below Competitive)
float willingness(int arch) {
    static const float k[int(Arch::Count)] = {
        0.8f,   // Politician
        0.6f,   // Peacekeeper
        1.3f,   // Warlord
        1.5f,   // Betrayer
        1.2f,   // Swindler
        0.8f,   // Merchant
        1.2f,   // Opportunist
        1.0f,   // Rusher
        0.7f,   // Turtler
        1.1f,   // Chaotic
    };
    return arch >= 0 && arch < int(Arch::Count) ? k[arch] : 1.f;
}
}  // namespace

// Competitive: is now a moment when a betrayal doesn't wreck the economy?
bool aiBetrayalMomentOk(const Game& g, int p) {
    const Player& me = g.players[size_t(p)];
    const Ground& G = aiGround(g, p);
    if (g.round <= 2 && G.sliceOpen > 0) return false;  // the slice comes first
    // keep a tactical action for production - unless the home dock has already produced this round
    const bool produced = me.homeSystem >= 0 && g.systems[size_t(me.homeSystem)].hasToken(p);
    if (me.tactic < (produced ? 1 : 2)) return false;
    const int home = me.homeSystem;
    if (home >= 0)  // not while our own home is threatened
        for (int s : systemsWithin(g, home, 1))
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && g.systems[size_t(s)].space[q].nonFighterShips() >= 2) return false;
    return spendable(g, p, false) >= 3;  // we can rebuild what the fight costs
}

float aiBetrayUrge(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers || !isBot(g, p)) return 1.f;
    const Personality& pe = g.players[size_t(p)].pers;
    if (pe.target < 0) return 1.f;
    AiSeat seat(p);
    if (aiCompetitiveSeat(g, p)) return aiBetrayalMomentOk(g, p) ? aiTune("g_betraynow", 1.5f) : aiTune("g_betraywait", 0.3f);
    const int a = pe.now >= 0 ? pe.now : pe.arch;
    return aiTune("g_betrayrush", 1.5f) * willingness(a);
}

}  // namespace ti
