// Headless AI-vs-AI games for testing the rules (ti4 --simulate N [players] [seed]).
// Every answer goes through the rules' validators; any rejected AI answer,
// broken invariant, crash or never-ending game is reported.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>

#include "ai/ai.h"
#include "engine/deal.h"
#include "engine/personality.h"
#include "engine/engine.h"
#include "engine/query.h"
#include "engine/rules.h"
#include "platform/platform.h"
#include "sim/tempo.h"

namespace ti {

namespace {
// Waypoints test driver (0.56.0, TI4_SIMWAYPOINTS=1): every computer move gets an explicit route for each system its
// ships leave - a shortest one, every other time through a gravity rift when one is on a shortest-enough way - so the
// validator, the rolls and save / replay see thousands of routes. Plans the routes would make illegal stay as they were.
long g_wpRoutes = 0, g_wpRift = 0, g_wpDropped = 0;
bool findRoute(const ti::Game& g, int p, int from, int to, bool rifts, std::vector<int>& out) {
    using namespace ti;
    struct Node {
        std::vector<int> via;
    };
    std::vector<Node> q{Node{}};
    std::set<int> seen{from};
    for (size_t h = 0; h < q.size() && q.size() < 400; ++h) {
        const std::vector<int> via = q[h].via;
        if (via.size() > 5) continue;
        for (int v : routeNextSteps(g, p, from, via, to)) {
            if (v == to) {
                if (via.empty()) continue;  // (a straight step: no route needed)
                out = via;
                return true;
            }
            if (seen.count(v)) continue;
            if (!rifts && (anomaliesFor(g, p, v) & kRift)) continue;
            seen.insert(v);
            Node n{via};
            n.via.push_back(v);
            q.push_back(n);
        }
    }
    return false;
}
void simRoutes(const ti::Game& g, const ti::Decision& d, ti::Answer& a) {
    using namespace ti;
    const int p = d.player, act = d.system;
    std::map<int, std::vector<int>> types;  // from -> ship types moving
    for (size_t i = 0; i + 3 < a.data.size(); i += 4)
        if (a.data[i + 1] < 0 && movesAsShip(g, p, UnitType(a.data[i + 2]))) types[a.data[i]].push_back(a.data[i + 2]);
    std::vector<int> plan = a.data;
    static long turn = 0;
    for (const auto& ft : types) {
        std::vector<int> via;
        const bool tryRift = (++turn % 2) == 0;
        bool found = (tryRift && findRoute(g, p, ft.first, act, true, via)) || findRoute(g, p, ft.first, act, false, via) ||
                     findRoute(g, p, ft.first, act, true, via);
        if (!found) continue;
        bool all = true;
        for (int t : ft.second)
            if (UnitType(t) != UnitType::Fighter && !shipReachVia(g, p, ft.first, via, act, UnitType(t), moveBonusMax(g, p)).ok) all = false;
        if (!all) continue;
        std::vector<int> with = plan;
        for (int st : via) with.insert(with.end(), {kWaypointTuple, ft.first, st, 0});
        if (const std::string why = checkMovePlan(g, p, act, with); !why.empty()) {
            if (std::getenv("TI4_WPDEBUG")) std::fprintf(stderr, "WPDROP %s\n", why.c_str());
            ++g_wpDropped;
            continue;
        }
        plan = with;
        ++g_wpRoutes;
        for (int st : via)
            if (anomaliesFor(g, p, st) & kRift) {
                ++g_wpRift;
                break;
            }
    }
    a.data = plan;
}

std::string checkInvariants(const Game& g) {
    for (int p = 0; p < g.numPlayers; ++p) {
        const Player& pl = g.players[p];
        if (pl.tactic < 0 || pl.fleet < 0 || pl.strategy < 0) return "negative command pool";
        int onBoard = 0;
        for (const SystemState& s : g.systems)
            if (s.hasToken(p)) ++onBoard;
        onBoard += edictTokensOf(g, p);  // tokens in a Mahact fleet pool (Edict)
        if (pl.tactic + pl.fleet + pl.strategy + onBoard > kCommandTokens)
            return "more than 16 command tokens: " + playerName(g, p) + " " + std::to_string(pl.tactic) + "/" + std::to_string(pl.fleet) +
                   "/" + std::to_string(pl.strategy) + " + " + std::to_string(onBoard) + " on the board";
        if (pl.tradeGoods < 0) return "negative trade goods";
        for (int t = 0; t < kUnitTypes; ++t) {
            if (unitsOnBoard(g, p, UnitType(t)) + capturedFrom(g, p, UnitType(t)) + (g.fixes >= 19 ? pl.phantom.count(UnitType(t)) : 0) >
                unitLimitOf(g, p, UnitType(t)))
                return "unit limit exceeded: " + playerName(g, p) + " has " + std::to_string(unitsOnBoard(g, p, UnitType(t))) + " " +
                       unitTypeName(UnitType(t), true);
        }
        for (const SystemState& s : g.systems) {
            for (int t = 0; t < kUnitTypes; ++t)
                if (s.space[p].dmg[t] > s.space[p].n[t]) return "damaged count above unit count";
        }
        int vpFromObjectives = 0;
        for (int16_t o : pl.scored) vpFromObjectives += kObjectives[o].points;
        // (the Mentak review: a Mutiny Against made Jol-Nar lose a point it had from an objective - points lost count)
        if (vpFromObjectives > pl.vp + pl.vpLost && pl.vp < g.vpGoal) {  // the VP track stops at the goal
            std::string list;
            for (int16_t o : pl.scored) list += std::string(" ") + kObjectives[o].key;
            return "VP lower than scored objectives: " + playerName(g, p) + " " + std::to_string(pl.vp) + " < " + std::to_string(vpFromObjectives) + " (" + list + ")";
        }
    }
    for (size_t x = 0; x < g.planets.size(); ++x) {
        const PlanetState& pl = g.planets[x];
        for (int p = 0; p < g.numPlayers; ++p) {
            if (p == pl.owner || ((pl.coexist >> p) & 1)) continue;  // (Thunder's Edge: coexisting units - Slumberstate's awakened PDS)
            if (pl.units[p].structures() > 0)
                return "structure on a planet the player doesn't control: " + playerName(g, p) + " on " + kPlanets[pl.def].name +
                       " (owner " + (pl.owner >= 0 ? playerName(g, pl.owner) : std::string("nobody")) + ")";
        }
        // (Thunder's Edge: neutral units count; coexisting ones don't - fixes 25: the neutral guards a Viper coexisted with
        // stay, coexisting, when Viper Hollow takes the planet)
        int armies = pl.units[kNeutral].groundForces() > 0 && !((pl.coexist >> kNeutral) & 1) ? 1 : 0;
        for (int p = 0; p < g.numPlayers; ++p)
            if (pl.units[p].groundForces() > 0 && !((pl.coexist >> p) & 1)) ++armies;
        // (0.43.0: not once the game is won - a win in the middle of a tactical action stops it before the invasion)
        if (armies > 1 && int(x) != g.groundCombatPlanet && g.phase != Phase::GameOver && g.winner < 0)
        {
            std::string who;
            for (int p = 0; p < kSides; ++p)
                if (pl.units[size_t(p)].groundForces() > 0)
                    who += " " + (p == kNeutral ? std::string("neutral") : playerName(g, p)) + " " + std::to_string(pl.units[size_t(p)].groundForces()) +
                           (((pl.coexist >> p) & 1) ? " (coexisting)" : "");
            return std::string("two players' ground forces share a planet after combat: ") + kPlanets[pl.def].name + " -" + who +
                   "; owner " + (pl.owner >= 0 ? playerName(g, pl.owner) : std::string("nobody"));
        }
    }
    return "";
}

// If the AI's answer is rejected, try something simple so the game continues.
Answer fallback(const Decision& d) {
    Answer a;
    switch (d.kind) {
        case DecisionKind::Pick:
            for (const Option& o : d.options)
                if (o.enabled && int(a.picks.size()) < std::max(1, d.minPick)) a.picks.push_back(o.id);
            break;
        case DecisionKind::Produce: a.data.assign(kUnitTypes + 1, 0); a.data[kUnitTypes] = -1; break;
        default: break;
    }
    return a;
}

}  // namespace

int runSimulation(int games, int players, uint32_t seed, bool verbose) {
    int failures = 0, finishedGames = 0, totalRounds = 0, totalAnswers = 0, rejected = 0;
    int vpWins = 0, undos = 0;
    std::map<int, int> winRounds;                    // VP wins by the round they happened in
    std::map<std::string, int> vpAll, vpWinner;      // victory points by source (all players / winners)
    std::map<std::string, int> dealStats;            // negotiations: deals and promises
    struct ArchStat {
        int seats = 0, wins = 0, vp = 0, betrayals = 0, attacks = 0, offers = 0, received = 0, accepted = 0, broken = 0;
        double planets3 = 0, value3 = 0, near2 = 0;  // economy: non-home planets / res+inf after round 3, % next to home after round 2
        int seen3 = 0, seen2 = 0;
    };
    std::map<int, ArchStat> archStats;
    int dealRounds = 0;
    // (test only, 0.55.0) TI4_PERSONASTATS=N: how the table treats seat N (offers made / accepted early and late, its
    // broken promises, Mecatol turns, raids, attacks on it) - with TI4_PERSONA to check the bots answer a habit
    const int pSeat = std::getenv("TI4_PERSONASTATS") ? std::atoi(std::getenv("TI4_PERSONASTATS")) : -1;
    double psOff[2] = {}, psAcc[2] = {}, psToOff[2] = {}, psToAcc[2] = {}, psBroken = 0, psMec = 0, psRaids = 0, psGames = 0;
    int vpGames = 0;
    double leaderAt[12] = {}, avgAt[12] = {}, planetsAt[12] = {};
    double valueAt[12] = {}, nearOwnAt[12] = {}, nearFreeAt[12] = {}, nearSeen[12] = {};  // early economy
    double commRefills = 0, commSwaps = 0, commTgPerGame = 0;                             // commodity trading
    double galHomes = 0, galNearValue = 0, galNearAnom = 0, galSpread = 0, galLeg = 0, galLegNear = 0, galGames = 0;  // galaxy
    double carriersAt[12] = {}, gfAt[12] = {}, actsIn[12] = {};
    int winsBySeat[8] = {};
    int roundPlayers[12] = {}, roundPub[12] = {}, roundSec[12] = {}, roundWinSeen[12] = {}, roundWinPub[12] = {}, roundWinSec[12] = {};  // VP of the leader / average player at the start of each round
    int roundsSeen[12] = {};
    std::map<std::string, std::pair<int, int>> objStats;  // TI4_OBJSTATS=1: objective -> (games revealed / held, times scored)
    const bool wantObj = std::getenv("TI4_OBJSTATS") != nullptr;
    std::map<std::string, int> stats;  // TI4_CARDSTATS=1: how often each card / agenda came up
    const bool wantStats = std::getenv("TI4_CARDSTATS") != nullptr;
    TempoScore tempo;  // TI4_TEMPO=1: the groundwork scorecard (slice, equidistants, custodians, spending, scoring tempo)
    const bool wantTempo = std::getenv("TI4_TEMPO") != nullptr;
    uint32_t t0 = monotonicMs();
    // TI4_SIMFIRST=N (test only, 0.43.0): start at game N - replays one failed game of a long run on its own
    const int firstGame = std::getenv("TI4_SIMFIRST") ? std::max(0, std::atoi(std::getenv("TI4_SIMFIRST"))) : 0;
    for (int gi = firstGame; gi < games; ++gi) {
        int n = players > 0 ? players : 3 + int((seed + gi) % 6);
        Setup s;
        s.players = n;
        s.human = -1;
        // TI4_SIMHUMAN=N (test only): seat N is marked as the human (answered by the AI) - exercises human-only paths
        if (const char* h = std::getenv("TI4_SIMHUMAN")) s.human = std::atoi(h) % n;
        if (const char* lv = std::getenv("TI4_SIMLEVEL")) s.aiLevel = std::atoi(lv);  // 0 Easy, 1 Normal, 2 Hard
        if (const char* lv = std::getenv("TI4_SIMLEVELS"))  // per seat, e.g. "3030" (0.19.0 faction menu)
            for (int i = 0; i < n && lv[i]; ++i) s.levels.push_back(lv[i] >= '0' && lv[i] <= '3' ? lv[i] - '0' : -1);
        s.seed = seed + uint32_t(gi) * 7919u;
        if (const char* hb = std::getenv("TI4_SIMHABITS")) {  // test only (0.54.0): "games attacks raids mecatol broken kept deals"
            HabitProfile& h = s.habits;
            if (std::sscanf(hb, "%d %f %f %f %f %f %f", &h.games, &h.attacks, &h.raids, &h.mecatol, &h.broken, &h.kept, &h.deals) != 7)
                h = HabitProfile();
        }
        if (std::getenv("TI4_SIMRANDOMMAP")) s.draft = 0;  // test only: the random galaxy instead of the draft
        if (std::getenv("TI4_SIMLAYOUT")) s.layout = std::atoi(std::getenv("TI4_SIMLAYOUT"));  // 0.58.0: 1 = no hyperlanes
        if (const char* m = std::getenv("TI4_SIMMODULES")) s.modules = std::atoi(m);  // 0.33.0: e.g. 3 = Codex + Uncharted
        if (const char* x = std::getenv("TI4_SIMEXTRAS")) s.extras = uint8_t(std::atoi(x));  // 0.55.0: e.g. 127 = Mecatol Rex Calm
        // 0.36.0: galactic events - a bit mask, or "rot" for 2 different playable events each game
        if (const char* ev = std::getenv("TI4_SIMEVENTS")) {
            if (!std::strcmp(ev, "rot")) {
                std::vector<int> ok;
                for (int k = 0; k < kEventCount; ++k)
                    if (galacticEvent(k).done) ok.push_back(k);
                if (!ok.empty()) s.events = (1 << ok[size_t(gi) % ok.size()]) | (1 << ok[size_t(gi * 7 + 3) % ok.size()]);
            } else {
                s.events = std::atoi(ev);
            }
        }
        if (const char* fl = std::getenv("TI4_SIMFACTIONS")) {  // e.g. "sol,letnev,hacan": these factions first
            std::string list = fl;
            std::vector<int> all;
            for (size_t at = 0; at <= list.size();) {
                size_t comma = list.find(',', at);
                std::string key = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                if (!key.empty()) all.push_back(findFaction(key.c_str()));
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
            if (int(all.size()) > n) {  // 0.43.0: a longer list - each game seats the next n of it (every faction gets played)
                for (int k = 0; k < n; ++k) s.factions.push_back(all[size_t((gi * n + k) % int(all.size()))]);
            } else {
                s.factions = all;
                // rotate so every faction gets different seats over the games
                if (!s.factions.empty()) std::rotate(s.factions.begin(), s.factions.begin() + gi % s.factions.size(), s.factions.end());
            }
        }
        // TI4_SIMARCH (test only): "none" = no personalities (the 0.11 bots), or archetype keys per seat
        // ("warlord,turtler,..."), rotated over the games like TI4_SIMFACTIONS
        if (const char* al = std::getenv("TI4_SIMARCH")) {
            std::string list = al;
            if (list == "none") {
                s.archetypes.assign(n, -2);
            } else {
                size_t at = 0;
                while (at <= list.size() && int(s.archetypes.size()) < n) {
                    size_t comma = list.find(',', at);
                    std::string key = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                    int a = -1;
                    for (int k = 0; k < int(Arch::Count); ++k)
                        if (key == kArchetypes[k].key) a = k;
                    s.archetypes.push_back(a);
                    if (comma == std::string::npos) break;
                    at = comma + 1;
                }
                if (!s.archetypes.empty()) std::rotate(s.archetypes.begin(), s.archetypes.begin() + gi % s.archetypes.size(), s.archetypes.end());
            }
        }
        Engine e;
        e.start(s);
        // TI4_SIMNOTES=1 (test only): everyone hands their faction note and Alliance to the next player,
        // as if traded, so the notes' effects come up. Skipped for the games that check undo / reload.
        // (0.43.0: TI4_SIMTECHS / TI4_SIMUNLOCK alone used to do nothing - only with one of the other three)
        bool testSetupDone = !((std::getenv("TI4_SIMNOTES") || std::getenv("TI4_SIMNEUTRAL") || std::getenv("TI4_SIMBT") ||
                                std::getenv("TI4_SIMTECHS") || std::getenv("TI4_SIMUNLOCK")) && gi >= 8);
        bool galaxyCounted = false;
        auto testSetup = [&]() {
            testSetupDone = true;
            Game& gg = e.g();
            if (std::getenv("TI4_SIMBT"))  // test only (0.43.0): every player has their breakthrough from the start (no "gained" effects)
                for (Player& pl : gg.players) pl.breakthrough = true;
            if (std::getenv("TI4_SIMUNLOCK"))  // test only: every commander and hero unlocked from the start
                for (Player& pl : gg.players)
                    for (LeaderState& ls : pl.leaders)
                        if (ls.status == kLocked) ls.status = kReady;
            if (const char* tl = std::getenv("TI4_SIMTECHS"))  // test only: give these (non-upgrade) technologies to whoever may own them
                for (int p = 0; p < n; ++p) {
                    std::string list = std::string(tl) + ",";
                    for (size_t at = 0, c; (c = list.find(',', at)) != std::string::npos; at = c + 1) {
                        int t = findTech(list.substr(at, c - at).c_str());
                        // unit upgrades only with TI4_SIMUPGRADES=1
                        if (t < 0 || (kTechs[t].unitUpgrade && !std::getenv("TI4_SIMUPGRADES")) || !techAvailableToResearch(gg, p, t))
                            continue;
                        Player& pl = gg.players[p];
                        pl.techs.push_back(int16_t(t));
                        if (kTechs[t].unitUpgrade)
                            for (int u = 0; u < kUnitCount; ++u)
                                if (kUnits[u].tech == t && (kUnits[u].faction < 0 || kUnits[u].faction == pl.faction))
                                    pl.unit[int(kUnits[u].type)] = int16_t(u);
                    }
                }
            if (std::getenv("TI4_SIMSETUPDUMP"))  // test only: what each player starts with
                for (int p = 0; p < n; ++p) {
                    const Player& pl = gg.players[p];
                    std::printf("seat %d %s home=%s comm=%d techs:", p, kFactions[pl.faction].name,
                                pl.homeSystem >= 0 ? kSystems[gg.systems[pl.homeSystem].def].name : "?", int(pl.commodities));
                    for (int16_t t : pl.techs) std::printf(" %s", kTechs[t].key);
                    std::printf(" leaders:");
                    for (const LeaderState& ls : pl.leaders) std::printf(" %s", kLeaders[ls.leader].key);
                    std::printf(" notes:");
                    for (int16_t nt : pl.notes) std::printf(" %d/%d", noteOwner(nt), noteType(nt));
                    std::printf(" home production %d units:", pl.homeSystem >= 0 ? productionCapacity(gg, p, pl.homeSystem) : 0);
                    if (pl.homeSystem >= 0) {
                        const SystemState& hs = gg.systems[pl.homeSystem];
                        for (int t = 0; t < kUnitTypes; ++t)
                            if (hs.space[p].n[t]) std::printf(" %d%s", hs.space[p].n[t], unitTypeShort(UnitType(t)));
                        for (int16_t x : hs.planets) {
                            std::printf(" |%s:", kPlanets[gg.planets[x].def].name);
                            for (int t = 0; t < kUnitTypes; ++t)
                                if (gg.planets[x].units[p].n[t]) std::printf(" %d%s", gg.planets[x].units[p].n[t], unitTypeShort(UnitType(t)));
                        }
                    }
                    std::printf("\n");
                }
            // TI4_SIMNEUTRAL=1 (test only, Thunder's Edge 0.34.0): neutral units scattered over the board - a few
            // ships in empty-ish systems, ground forces (and now and then a mech or a PDS) on unclaimed planets
            if (std::getenv("TI4_SIMNEUTRAL")) {
                uint32_t h = uint32_t(s.seed) * 2654435761u + 77u;
                auto roll = [&](int k) {
                    h = h * 1664525u + 1013904223u;
                    return int((h >> 8) % uint32_t(k));
                };
                for (size_t sy = 0; sy < gg.systems.size(); ++sy) {
                    SystemState& ss = gg.systems[sy];
                    if (ss.home >= 0 || ss.offBoard || ss.isHyperlane() || int(sy) == gg.mecatol()) continue;
                    bool taken = false;
                    for (int q = 0; q < n; ++q)
                        if (hasUnits(gg, q, int(sy))) taken = true;
                    if (taken) continue;
                    const int r = roll(10);
                    if (r < 3) {
                        ss.space[kNeutral].add(UnitType::Destroyer, 1 + roll(2));
                        if (r == 0) ss.space[kNeutral].add(UnitType::Cruiser, 1);
                        if (r == 1) ss.space[kNeutral].add(UnitType::Fighter, 2);
                        if (r == 2 && roll(3) == 0) ss.space[kNeutral].add(UnitType::Dreadnought, 1);
                    }
                    for (int16_t x : ss.planets) {
                        if (gg.planets[x].owner >= 0 || roll(10) >= 4) continue;
                        Force& f = gg.planets[x].units[kNeutral];
                        f.add(UnitType::Infantry, 1 + roll(3));
                        if (roll(4) == 0) f.add(UnitType::Mech, 1);
                        if (roll(5) == 0) f.add(UnitType::PDS, 1);
                    }
                }
            }
            for (int p = 0; p < n && std::getenv("TI4_SIMNOTES"); ++p) {
                int q = (p + 1) % n;
                auto& h = gg.players[p].notes;
                for (int type : {kNoteFaction, kNoteAlliance}) {
                    auto it = std::find(h.begin(), h.end(), makeNote(p, type));
                    if (it == h.end()) continue;
                    if (type == kNoteAlliance && isFaction(gg, q, "mahact")) continue;  // Hubris
                    h.erase(it);
                    (type == kNoteAlliance ? gg.players[q].notesInPlay : gg.players[q].notes).push_back(makeNote(p, type));
                }
            }
        };
        Rng aiRng(s.seed ^ 0xA11CEu);
        tempo.reset();
        int steps = 0;
        std::string problem;
        Answer last;
        bool haveLast = false;
        int lastRound = 0;
        while (e.waiting() && steps < 60000) {
            if (e.game().round != lastRound) {  // a new round: note the VP standings
                lastRound = e.game().round;
                if (lastRound >= 2 && lastRound - 1 < 12) {
                    int hi = 0, sum = 0;
                    for (const Player& pl : e.game().players) {
                        hi = std::max<int>(hi, pl.vp);
                        sum += pl.vp;
                    }
                    const int dbgFrom = std::getenv("TI4_OBJDEBUG") ? std::atoi(std::getenv("TI4_OBJDEBUG")) : -1;
                    if (dbgFrom >= 0 && lastRound - 1 >= dbgFrom && lastRound - 1 <= dbgFrom + 3) {  // what players still miss
                        const Game& gg = e.game();
                        std::printf("game %d after round %d:\n", gi, lastRound - 1);
                        for (int q = 0; q < gg.numPlayers; ++q) {
                            if (std::getenv("TI4_OBJLEADER") && gg.players[q].vp < hi) continue;
                            int ss = 0;
                            for (int16_t o : gg.players[q].scored) ss += kObjectives[o].kind == 3;
                            std::printf("  %-14s vp %d (secrets scored %d, tac %d str %d tg %d):", playerName(gg, q).c_str(), gg.players[q].vp, ss,
                                        gg.players[q].tactic, gg.players[q].strategy, gg.players[q].tradeGoods);
                            for (int16_t o : gg.publicObjectives) {
                                if (hasScored(gg, q, o)) {
                                    std::printf(" [%s ok]", kObjectives[o].key);
                                    continue;
                                }
                                ObjProgress pr = objectiveProgress(gg, q, o);
                                if (pr.need) std::printf(" %s %d/%d", kObjectives[o].key, pr.have, pr.need);
                                else std::printf(" %s %s", kObjectives[o].key, meetsObjective(gg, q, o) ? "can" : "no");
                            }
                            for (int16_t o : gg.players[q].secrets) {
                                ObjProgress pr = objectiveProgress(gg, q, o);
                                std::printf(" | S:%s %d/%d", kObjectives[o].key, pr.have, pr.need);
                            }
                            std::printf("\n");
                        }
                    }
                    {
                        int np = 0;
                        for (size_t x = 0; x < e.game().planets.size(); ++x)
                            if (e.game().planets[x].owner >= 0 && !isHomePlanet(e.game(), int(x))) ++np;
                        planetsAt[lastRound - 1] += double(np) / e.game().numPlayers;
                        {  // economy: res+inf of non-home planets; planets next to home held by the owner
                            const Game& gg = e.game();
                            double val = 0;
                            for (size_t x = 0; x < gg.planets.size(); ++x)
                                if (gg.planets[x].owner >= 0 && !isHomePlanet(gg, int(x)))
                                    val += kPlanets[gg.planets[x].def].res + kPlanets[gg.planets[x].def].inf;
                            valueAt[lastRound - 1] += val / gg.numPlayers;
                            for (int q = 0; q < gg.numPlayers; ++q) {
                                const SystemState& hs = gg.systems[size_t(gg.players[q].homeSystem)];
                                if (hs.offBoard) continue;
                                int own = 0, total = 0, unclaimed = 0;
                                for (const SystemState& ss : gg.systems) {
                                    if (ss.offBoard || ss.home >= 0 || hexDistance(ss.pos, hs.pos) != 1) continue;
                                    for (int16_t pl : ss.planets) {
                                        ++total;
                                        if (gg.planets[size_t(pl)].owner == q) ++own;
                                        else if (gg.planets[size_t(pl)].owner < 0) ++unclaimed;
                                    }
                                }
                                if (lastRound - 1 == 2 && gg.players[q].pers.arch >= 0) {
                                    ArchStat& as = archStats[gg.players[q].pers.arch];
                                    if (total) {
                                        as.near2 += double(own) / total;
                                        ++as.seen2;
                                    }
                                }
                                if (lastRound - 1 == 3 && gg.players[q].pers.arch >= 0) {
                                    ArchStat& as = archStats[gg.players[q].pers.arch];
                                    for (size_t x = 0; x < gg.planets.size(); ++x)
                                        if (gg.planets[x].owner == q && !isHomePlanet(gg, int(x))) {
                                            as.planets3 += 1;
                                            as.value3 += kPlanets[gg.planets[x].def].res + kPlanets[gg.planets[x].def].inf;
                                        }
                                    ++as.seen3;
                                }
                                if (!total) continue;
                                nearOwnAt[lastRound - 1] += double(own) / total;
                                nearFreeAt[lastRound - 1] += double(unclaimed) / total;
                                nearSeen[lastRound - 1] += 1;
                            }
                        }
                        int carriers = 0, gf = 0;
                        for (int q = 0; q < e.game().numPlayers; ++q) {
                            carriers += unitsOnBoard(e.game(), q, UnitType::Carrier);
                            gf += unitsOnBoard(e.game(), q, UnitType::Infantry) + unitsOnBoard(e.game(), q, UnitType::Mech);
                        }
                        carriersAt[lastRound - 1] += double(carriers) / e.game().numPlayers;
                        gfAt[lastRound - 1] += double(gf) / e.game().numPlayers;
                    }
                    leaderAt[lastRound - 1] += hi;
                    avgAt[lastRound - 1] += double(sum) / e.game().numPlayers;
                    ++roundsSeen[lastRound - 1];
                }
            }
            // test-only notes / technologies: once setup (faction notes, starting techs) is complete
            if (!testSetupDone && e.game().phase != Phase::Setup) testSetup();
            if (!galaxyCounted && e.game().phase != Phase::Setup) {  // the galaxy as built: what is next to each home
                galaxyCounted = true;
                const Game& gg = e.game();
                double lo = 1e9, hi = -1e9;
                for (int p = 0; p < gg.numPlayers; ++p) {
                    const SystemState& hs = gg.systems[gg.players[p].homeSystem];
                    Hex home = hs.pos;
                    if (hs.offBoard)  // the Creuss gate
                        for (const SystemState& x : gg.systems)
                            if (!x.offBoard && hexDistance(x.pos, hs.pos) == 1 && !std::strcmp(x.d().key, "17")) home = x.pos;
                    double v = 0;
                    for (const SystemState& x : gg.systems) {
                        if (x.offBoard || x.home >= 0 || x.isHyperlane() || hexDistance(x.pos, home) != 1) continue;
                        for (int16_t pl : x.planets) v += kPlanets[gg.planets[pl].def].res + kPlanets[gg.planets[pl].def].inf;
                        if (x.d().anomalies) galNearAnom += 1;
                    }
                    galNearValue += v;
                    galHomes += 1;
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
                for (const SystemState& x : gg.systems)
                    for (int16_t pl : x.planets)
                        if (kPlanets[gg.planets[pl].def].legendary) {
                            galLeg += 1;
                            for (const SystemState& h : gg.systems)
                                if (h.home >= 0 && !h.offBoard && hexDistance(h.pos, x.pos) == 1) {
                                    galLegNear += 1;
                                    break;
                                }
                        }
                galSpread += hi - lo;
                galGames += 1;
            }
            // Undo check: going back one step and giving the same answer again
            // must lead to exactly the same position.
            if (gi < 4 && haveLast && steps % 17 == 0 && e.canUndo()) {
                std::vector<std::string> logBefore = e.game().log;
                std::string promptBefore = e.decision().prompt;
                int countBefore = e.answerCount();
                std::string err;
                if (!e.undo() || e.answerCount() != countBefore - 1) {
                    problem = "undo failed";
                    break;
                }
                if (!e.answer(last, &err)) {
                    problem = "answer after undo rejected: " + err;
                    break;
                }
                if (e.game().log != logBefore || !e.waiting() || e.decision().prompt != promptBefore) {
                    problem = "undo + same answer gave a different position";
                    break;
                }
                e.takeEvents();
                ++undos;
            }
            const Decision& d = e.decision();
            static const bool timeIt = std::getenv("TI4_AITIME") != nullptr;
            uint32_t tA = timeIt ? monotonicMs() : 0;
            Answer a = aiAnswer(e.game(), d, aiRng);
            if (timeIt) {
                static uint32_t worst = 0;
                uint32_t dt = monotonicMs() - tA;
                if (dt > worst) {
                    worst = dt;
                    std::printf("AITIME %u ms tag %d kind %d \"%s\"\n", dt, int(d.tag), int(d.kind), d.prompt.c_str());
                }
            }
            static const bool simWp = std::getenv("TI4_SIMWAYPOINTS") != nullptr;  // (0.56.0, test only: routes on every move)
            if (simWp && d.kind == DecisionKind::Move && d.tag == Tag::Movement && !a.data.empty()) simRoutes(e.game(), d, a);
            static const int traceFrom = std::getenv("TI4_TRACEFROM") ? std::atoi(std::getenv("TI4_TRACEFROM")) : -1;
            if (traceFrom >= 0 && steps >= traceFrom)
                std::printf("  trace %d: p%d tag %d ab %d \"%s\" -> %d\n", steps, d.player, int(d.tag), d.ability, d.prompt.c_str(),
                            a.picks.empty() ? -999 : a.picks[0]);
            if (wantTempo) tempo.answered(e.game(), d, a);
            static const bool abStats = std::getenv("TI4_ABSTATS") != nullptr;  // 0.43.0 (debug passes): which faction
            if (abStats) {                                                     // questions / actions ever come up
                static std::map<int, int>* asked = new std::map<int, int>();
                static std::map<int, int>* used = new std::map<int, int>();
                static std::map<int, int>* offered = new std::map<int, int>();
                static std::map<int, int>* yes = new std::map<int, int>();   // 0.44.4: yes / no questions answered yes
                static std::map<int, int>* yn = new std::map<int, int>();
                if (d.tag == Tag::Faction) ++(*asked)[d.ability];
                if (d.tag == Tag::Faction && d.options.size() == 2 && d.options[0].id == 1 && d.options[1].id == 0) {
                    ++(*yn)[d.ability];
                    if (!a.picks.empty() && a.picks[0] == 1) ++(*yes)[d.ability];
                }
                if (d.tag == Tag::ActionChoice)
                    for (const Option& o : d.options)
                        if (o.enabled && o.id >= 6000 && o.id < 7000) ++(*offered)[o.id - 6000];
                if (d.tag == Tag::ActionChoice && !a.picks.empty() && a.picks[0] >= 6000 && a.picks[0] < 7000) ++(*used)[a.picks[0] - 6000];
                if (d.tag == Tag::ActionChoice && !a.picks.empty() && a.picks[0] >= 700 && a.picks[0] < 1000) ++(*used)[a.picks[0] - 700];
                // (after 0.44.4) technology component actions (2000 + tech) and the "use this technology?" questions
                static std::map<int, int>* techOffered = new std::map<int, int>();
                static std::map<int, int>* techUsed = new std::map<int, int>();
                static std::map<std::string, std::pair<int, int>>* useTech = new std::map<std::string, std::pair<int, int>>();
                if (d.tag == Tag::ActionChoice)
                    for (const Option& o : d.options)
                        if (o.enabled && o.id >= 2000 && o.id < 3000) ++(*techOffered)[o.id - 2000];
                if (d.tag == Tag::ActionChoice && !a.picks.empty() && a.picks[0] >= 2000 && a.picks[0] < 3000) ++(*techUsed)[a.picks[0] - 2000];
                if (d.tag == Tag::UseTech) {
                    auto& u = (*useTech)[d.prompt.substr(0, 60)];
                    ++u.first;
                    // (yes / no: 1; a list such as Bio-Stims: any real choice - 0.44.6)
                    const bool yesNoQ = d.options.size() == 2 && d.options[0].id == 1 && d.options[1].id == 0;
                    if (!a.picks.empty() && (yesNoQ ? a.picks[0] == 1 : a.picks[0] >= 0)) ++u.second;
                }
                // (0.55.0) every component action (action cards 1000+, techs 2000+, fragments 3000+, relics 4000+, ...)
                static std::map<int, int>* compOffered = new std::map<int, int>();
                static std::map<int, int>* compUsed = new std::map<int, int>();
                if (d.tag == Tag::ActionChoice) {
                    for (const Option& o : d.options)
                        if (o.enabled && o.id >= 1000 && o.id < 6000) ++(*compOffered)[o.id];
                    if (!a.picks.empty() && a.picks[0] >= 1000 && a.picks[0] < 6000) ++(*compUsed)[a.picks[0]];
                }
                static bool hooked3 = false;
                if (!hooked3) {
                    hooked3 = true;
                    std::atexit([]() {
                        for (const auto& kv : *compOffered)
                            std::fprintf(stderr, "COMPSTATS %d offered %d used %d\n", kv.first, kv.second, (*compUsed)[kv.first]);
                    });
                }
                static bool hooked2 = false;
                if (!hooked2) {
                    hooked2 = true;
                    std::atexit([]() {
                        for (const auto& kv : *techOffered) std::fprintf(stderr, "TECHSTATS offered %s %d\n", kTechs[kv.first].key, kv.second);
                        for (const auto& kv : *techUsed) std::fprintf(stderr, "TECHSTATS used %s %d\n", kTechs[kv.first].key, kv.second);
                        for (const auto& kv : *useTech)
                            std::fprintf(stderr, "TECHSTATS usetech %d %d %s\n", kv.second.first, kv.second.second, kv.first.c_str());
                    });
                }
                static bool hooked = false;
                if (!hooked) {
                    hooked = true;
                    std::atexit([]() {
                        for (const auto& kv : *asked) std::fprintf(stderr, "ABSTATS asked %d %d\n", kv.first, kv.second);
                        for (const auto& kv : *used) std::fprintf(stderr, "ABSTATS used %d %d\n", kv.first, kv.second);
                        for (const auto& kv : *offered) std::fprintf(stderr, "ABSTATS offered %d %d\n", kv.first, kv.second);
                        for (const auto& kv : *yn) std::fprintf(stderr, "ABSTATS yesno %d %d\n", kv.first, kv.second);
                        for (const auto& kv : *yes) std::fprintf(stderr, "ABSTATS yes %d %d\n", kv.first, kv.second);
                    });
                }
            }
            std::string err;
            if (!e.answer(a, &err)) {
                ++rejected;
                if (verbose || rejected <= 20)
                    std::printf("  game %d: AI answer rejected (tag %d kind %d, \"%s\"): %s\n", gi, int(d.tag), int(d.kind),
                                d.prompt.c_str(), err.c_str());
                Answer f = fallback(d);
                if (!e.answer(f, &err)) {
                    problem = "fallback also rejected: " + err;
                    break;
                }
                a = f;
            }
            last = a;
            haveLast = true;
            if (wantTempo) tempo.step(e.game());
            static const bool ingressCheck = std::getenv("TI4_INGRESSCHECK") != nullptr;  // (after 0.44.4, test only)
            if (ingressCheck) {
                // when the Fracture comes into play: the ingress tokens against the synergy of the breakthrough that rolled it
                // in (checked once the placing questions are over)
                static bool was = false, pending = false;
                static int who = -1;
                static size_t logSeen = 0;
                const Game& gg = e.game();
                if (gg.log.size() < logSeen) logSeen = 0, was = pending = false;  // (a new game)
                if (gg.fractureIn && !was) {
                    pending = true;
                    who = -1;
                    for (size_t i = 0; i < gg.log.size(); ++i)  // (the last breakthrough gained before it)
                        if (gg.log[i].find("gains their breakthrough") != std::string::npos)
                            for (int q = 0; q < gg.numPlayers; ++q)
                                if (gg.log[i].compare(0, playerName(gg, q).size(), playerName(gg, q)) == 0) who = q;
                }
                const bool placing = e.waiting() && e.decision().ability == int(Ab::IngressPlace);
                if (pending && !placing) {
                    pending = false;
                    TechColor a = TechColor::None, b = TechColor::None;
                    const bool syn = who >= 0 && synergyOf(gg, who, a, b);
                    const int last = gg.teSystem >= 0 ? gg.teSystem : gg.mecatol();
                    int on = 0, ok = 0, colA = 0, colB = 0;
                    std::string where;
                    for (size_t s = 0; s < gg.systems.size(); ++s) {
                        if (!gg.systems[s].ingress || int(s) == last) continue;
                        ++on;
                        int sk = 0;
                        for (int16_t x : gg.systems[s].planets) sk |= planetSkips(gg, x);
                        const bool ma = syn && ((sk >> int(a)) & 1), mb = syn && ((sk >> int(b)) & 1);
                        colA += ma ? 1 : 0;
                        colB += mb ? 1 : 0;
                        ok += (!syn && sk) || ma || mb ? 1 : 0;
                        where += std::string(" ") + kSystems[gg.systems[s].def].name + "[";
                        for (int c = 0; c < 4; ++c)
                            if ((sk >> c) & 1) where += "BGRY"[c];
                        where += "]";
                    }
                    int avail[4] = {};  // systems on the board with each specialty (Fracture excluded)
                    for (size_t s = 0; s < gg.systems.size(); ++s) {
                        if (gg.systems[s].fracture) continue;
                        int sk = 0;
                        for (int16_t x : gg.systems[s].planets)
                            if (!gg.planets[size_t(x)].destroyed) sk |= planetSkips(gg, x);
                        for (int c = 0; c < 4; ++c) avail[c] += (sk >> c) & 1;
                    }
                    where += " | on the board B" + std::to_string(avail[0]) + " G" + std::to_string(avail[1]) + " R" + std::to_string(avail[2]) +
                             " Y" + std::to_string(avail[3]);
                    std::printf("INGRESS %s by %s (%s%s%s): %d tokens, %d matching (%s %d, %s %d):%s\n",
                                ok == on && (!syn || (colA >= 1 && colB >= 1)) ? "ok" : "MISMATCH",
                                who >= 0 ? kFactions[size_t(gg.players[size_t(who)].faction)].key : "?", syn ? colorName(a) : "no synergy",
                                syn ? "/" : "", syn ? colorName(b) : "", on, ok, syn ? colorName(a) : "-", colA, syn ? colorName(b) : "-", colB,
                                where.c_str());
                }
                was = gg.fractureIn;
                logSeen = gg.log.size();
            }
            if (const char* w = std::getenv("TI4_WATCHUNIT")) {  // debug: "player,unitType" - print when the count changes
                static int prev = -1;
                int pw = std::atoi(w), ut = std::atoi(std::strchr(w, ',') ? std::strchr(w, ',') + 1 : "0");
                int c = unitsOnBoard(e.game(), pw, UnitType(ut));
                if (c != prev) {
                    std::printf("WATCH step %d count %d after: %s\n", steps, c, e.game().log.empty() ? "" : e.game().log.back().c_str());
                    prev = c;
                }
            }
            if (std::getenv("TI4_TOKWATCH")) {  // debug: pool totals that grow without a "command tokens" log line
                static int prevTok[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
                static size_t prevLog = 0;
                const Game& gw = e.game();
                if (gw.log.size() < prevLog) {
                    prevLog = 0;
                    for (int& x : prevTok) x = -1;
                }
                for (int pw = 0; pw < gw.numPlayers; ++pw) {
                    const Player& pl = gw.players[pw];
                    int t = pl.tactic + pl.fleet + pl.strategy;
                    if (prevTok[pw] >= 0 && t > prevTok[pw]) {
                        bool logged = false;
                        for (size_t i = prevLog; i < gw.log.size(); ++i)
                            if (gw.log[i].find("command tokens:") != std::string::npos) logged = true;
                        if (!logged) {
                            std::printf("TOKWATCH %s %d -> %d:\n", playerName(gw, pw).c_str(), prevTok[pw], t);
                            for (size_t i = prevLog > 3 ? prevLog - 3 : 0; i < gw.log.size(); ++i) std::printf("    %s\n", gw.log[i].c_str());
                        }
                    }
                    prevTok[pw] = t;
                }
                prevLog = gw.log.size();
            }
            ++steps;
            if ((steps % 50 == 0 || std::getenv("TI4_INVEVERY")) && e.game().activeSystem < 0) {
                std::string inv = checkInvariants(e.game());
                if (!inv.empty()) {
                    problem = "invariant: " + inv;
                    break;
                }
            }
            if (wantTempo) tempo.events(e.game(), e.takeEvents());  // (0.53.0: combats for the operations scorecard)
            else e.takeEvents();
        }
        const Game& g = e.game();
        if (problem.empty() && !e.crashMessage().empty()) problem = "crash: " + e.crashMessage();
        if (problem.empty() && !e.finished()) problem = "game did not finish after " + std::to_string(steps) + " answers";
        totalAnswers += steps;
        if (!problem.empty()) {
            ++failures;
            std::printf("FAIL game %d (%d players, seed %u, round %d): %s\n", gi, n, s.seed, g.round, problem.c_str());
            int k = std::getenv("TI4_SIMLOG") ? 0 : std::max(0, int(g.log.size()) - 8);
            for (size_t i = size_t(k); i < g.log.size(); ++i) std::printf("     | %s\n", g.log[i].c_str());
            if (const char* sd = std::getenv("TI4_SIMSAVEDIR")) {  // (0.44.5: the failed game's save too, for --dumplog)
                if (std::FILE* f = std::fopen((std::string(sd) + "/game" + std::to_string(gi) + "-fail.ti4").c_str(), "w")) {
                    const std::string t = e.saveText();
                    std::fwrite(t.data(), 1, t.size(), f);
                    std::fclose(f);
                }
            }
            continue;
        }
        if (const char* sd = std::getenv("TI4_SIMSAVEDIR")) {  // 0.44.4 (test only): every game's save, for replay checks
            if (std::FILE* f = std::fopen((std::string(sd) + "/game" + std::to_string(gi) + ".ti4").c_str(), "w")) {
                const std::string t = e.saveText();
                std::fwrite(t.data(), 1, t.size(), f);
                std::fclose(f);
            }
        }
        // Save/load check: replaying the answers must reproduce the same game.
        if (gi < 8) {
            Engine copy;
            std::string err;
            if (!copy.loadText(e.saveText(), &err)) {
                ++failures;
                std::printf("FAIL game %d: reload failed: %s\n", gi, err.c_str());
                continue;
            }
            if (copy.game().log != g.log || copy.game().winner != g.winner) {
                ++failures;
                std::printf("FAIL game %d: reloaded game differs from the original\n", gi);
                continue;
            }
        }
        ++finishedGames;
        totalRounds += g.round;
        if (wantTempo) tempo.endGame(g);
        if (std::getenv("TI4_HABITSTATS")) {  // test only (0.54.0): every seat's habits as game/habits.cpp would count them
            static double sum[6] = {}, seats = 0;
            for (int q = 0; q < g.numPlayers; ++q) {
                float attacks = 0, kept = 0, deals = 0;
                for (int b = 0; b < g.numPlayers; ++b) {
                    if (b == q) continue;
                    attacks += float(g.players[size_t(b)].pers.attacks[size_t(q)]);
                    kept += float(g.players[size_t(b)].pers.kept[size_t(q)]);
                    deals += float(g.players[size_t(b)].pers.deals[size_t(q)]);
                }
                sum[0] += attacks / std::max(1, g.round);
                sum[1] += g.players[size_t(q)].raids;
                sum[2] += g.players[size_t(q)].mecHeld;
                sum[3] += g.players[size_t(q)].promisesBroken;
                sum[4] += kept;
                sum[5] += deals;
                seats += 1;
            }
            if (gi == games - 1)
                std::printf("habits (a bot seat's average): attacks/round %.2f raids %.2f mecatol turns %.2f broken %.2f kept %.2f deals %.2f\n",
                            sum[0] / seats, sum[1] / seats, sum[2] / seats, sum[3] / seats, sum[4] / seats, sum[5] / seats);
        }
        if (wantStats) {
            for (const std::string& line : g.log) {
                size_t k = line.find(" plays ");
                if (k != std::string::npos && line.back() == '.') {
                    std::string card = line.substr(k + 7, line.size() - k - 8);
                    bool strategy = false;
                    for (int c = 0; c < kStrategyCards; ++c)
                        if (card == strategyName(c)) strategy = true;
                    if (!strategy) ++stats["card  " + card];
                }
                if (line.rfind("Agenda: ", 0) == 0) ++stats["agenda " + line.substr(8, line.find(" (") - 8)];
                if (line.find(" is now a law") != std::string::npos) ++stats["law    " + line.substr(0, line.find(" is now a law"))];
                if (line.find("is cancelled") != std::string::npos) ++stats["~ sabotaged"];
                {
                    size_t k = line.find(" explores ");
                    size_t c = line.rfind(": ");
                    if (k != std::string::npos && c != std::string::npos && c > k)
                        ++stats["explore " + line.substr(c + 2, line.size() - c - 3)];
                    k = line.find(" gains the relic ");
                    if (k != std::string::npos) ++stats["relic   " + line.substr(k + 17, line.find(" (", k) - k - 17)];
                    if (line.find(" purges ") != std::string::npos && line.find("fragments") == std::string::npos)
                        ++stats["relic used: " + line.substr(line.find(" purges ") + 8)];
                    if (line.find("Crown of Thalnos") != std::string::npos) ++stats["~ thalnos rerolls"];
                    if (line.find("wormhole nexus becomes active") != std::string::npos) ++stats["~ nexus activated"];
                    if (line.find("is destroyed by the Stellar Converter") != std::string::npos) ++stats["~ planets destroyed"];
                    if (line.find("(Psychoarchaeology)") != std::string::npos) ++stats["~ psychoarchaeology"];
                    for (const char* lg : {"Imperial Arms Vault", "The Atrament", "Mirage Flight Academy", "Exterrix Headquarters"})
                        if (line.find(lg) != std::string::npos) ++stats[std::string("legend  ") + lg];
                }
                // Discordant Stars (0.37.0): the ability's name is in its line
                for (const char* k : {"Zealous", "Ruthless", "Magistrate", "Tempest Drive", "Deliverance Engine", "Blessing of the Queens",
                                      "Queen Lucreia", "Queen Lorena", "Cargo Raiders", "(Scour)", "(Raze)", "Lost Cause", "Scavenger Exos",
                                      "Clan's Favor", "Spear of Vaylar", "Eclipse", "A New Edifice", "Iconoclasm", "War Stories",
                                      "Memory of Dusk", "Spark Thrusters", "ATS Armaments reroll", "Spoils of War", "Vassa Hagi",
                                      "Master Halbert", "Resonance Cascade", "Troubadour", "Contagion", "Auriga", "Indoctrination Teams",
                                      "Vector Programs", "Kyro Rider", "(Tox)", "Silas Deriga", "Brikk Strain", "Pustule", "Moult",
                                      "Chitin Hulk", "Brood Pod", "Carcinisation", "Operator Kkavras", "Phquaiset", "Biomass Event",
                                      "Nauplius", "Projection of Power", "Emergency Mobilization", "George Nobin", "Henry Storcher",
                                      "trade port on", "Minuteman", "Trade Alliance", "Trade Port anti-fighter", "Strike Team",
                                      "Subversive",
                                      // batch 2 (0.38.0)
                                      "Shard Volley", "Hyperkinetic Ordnance", "Meteor Slings", "Stone Speakers", "Combinatorial Bypass",
                                      "Hkot Tokal", "Zelian A", "Collider", "Davish S'Norri", "Megalith", "Daedalon Flight System",
                                      "Obsessive Designs", "Priestess Tuh", "Ancient Knowledge", "Pilgrimage Beacons", "Favor of Rhodun",
                                      "Bishop Ulin", "Plausible Deniability", "Plots Within Plots", "Contract Network", "Deliberate Action",
                                      "Applied Biothermics", "Stone's Embrace", "Industrious", "Cataclysm", "(Daedalon)",
                                      "seizes every factory", "Rond Bri'ay", "Automatons", "Contractual Obligations", "Crystal Chorus",
                                      "Blessing of the Ancients", "Meditation", "Disciple Fran", "overcharges", "Coil Overcharge",
                                      "Sanctification Field", "Omniscience Field", "Recycled Materials", "Autofabricator", "Ky'vir",
                                      "Conspirators", "B-Unit", "Fillipo Rois", "Udosh B'rtul", "Splintering Gale", "Lithoids",
                                      // batch 3 (0.39.0)
                                      "Secret Maps", "Fortune Seekers", "Ancient Blueprints", "Wayfinder", "Merged Replicators",
                                      "Encryption Key", "C.O.O. Mgur", "Primordial Data Core", "(Auctioneer)", "Migrant Fleet",
                                      "Maximus", "(Aegis)", "Impressment Programs", "Combat Drills", "(Jgin Faru)", "Clona Bathru",
                                      "defense matrix", "Repairitor", "(Mercenaries)", "Data Leak", "Shadow Markets", "Blackmail Programs",
                                      "Underground Market", "Sal Gavda", "Quaxdol Junitas", "Harmonic Convergence", "Privateer",
                                      "Mantle Cracking", "Deep Mining", "Beg Bersha", "Orion Platform", "Gledge Base", "(Durran)",
                                      "Gorthrim", "Oracle AI", "Psychographics", "Sentient Datapool", "Read the Fates", "(Clodho)",
                                      "(Lachis)", "weaves fate", "(Iledrith)", "(Glory)", "(Heroism)", "Hulgade's Hammer",
                                      "Zhrgar Stimulants", "(Vassalage)", "Merkismathr Asvand", "Ygegnad", "(Skald)", "Lightning Drives",
                                      "Sdallari Tvungovot", "(Voldun)",
                                      // batch 4 (0.40.0)
                                      "Cloaked Fleets", "Shroud of Lith", "Nightingale V", "Seeker Drones", "Shrouded Skirmishers",
                                      "Captain Dust", "Shrouded Expedition", "Nightshade Vanguard", "(Facsimile)", "Secrets of the Weave",
                                      "Illusory Presence", "Particle Sieve", "Fractal Plating", "(Walik)", "(Komat)", "Two by Two",
                                      "(Duuban)", "Fine Print", "Collateralized Loans", "Binding Debts", "Aurum Vadra", "Midas Turbine",
                                      "Vaden Handshake", "Krovoz Strike Teams", "Yudri Sukhov", "Komdar Borodin", "calls in every debt",
                                      "(Collector)", "Autonetic Memory", "Cybernetic Madness", "Reprocessor Alpha", "Recursive Worm",
                                      "Algorithmic Replication", "Skhot Unit X-12", "Koryl Ferax", "the Scream", "(Revenant)",
                                      "Voidsailors", "Voidwake Missiles", "Voidflare Warden", "Nivyn Guidance", "Suldhan Wraeg",
                                      "Eye of the Abyss", "(Policies)", "(Connect)", "(Preserve)", "(Empower)", "(Exploit)", "(Plunder)",
                                      "(Control)", "False Flag Operations", "Geosympathic Impeller", "Incite Revolution", "Baggil Wildpaw",
                                      "Knak Half-Ear", "Shadow Council",
                                      // batch 5 (0.41.0)
                                      "Rally to the Cause", "(Diplomats)", "Covert Strike Teams", "Broadcast Teams", "Cordo Haved",
                                      "President Cyhn", "Heart of Rebellion", "Envoy Network", "(Grace)", "(Radiance)",
                                      "prediction was right", "Unity Algorithm", "Encrypted Trade Hub", "Edyn Rider",
                                      "(Kadryn)", "Allant grants", "Living Will", "Hired Guns", "Nokar Navigator", "Sabre", "Local Contracts",
                                      "Sal Sparrow", "Jack Hallard", "Irrefutable Offer", "Military Industrial Complex", "Axis Order",
                                      "Bearer of Heavens", "Rift Engines", "Emergency Deployment", "Industry Secrets",
                                      "Shipmonger Zsknck", "Designer TckVsk", "Aggressive Marketing", "Prescient Memories", "Divination",
                                      "Necrophage", "Psyclobea Qarnyx", "Mycelium Ring", "Psychoactive Armaments", "Gift of Insight",
                                      "Lactarius Indigo", "Amanita Muscaria", "Soul Cage", "Amandia Pholdis", "Raid Leaders",
                                      "War Song Implants", "Skarvald & Torvar", "Jarl Vel", "Raze Order", "Alleged Birthright",
                                      "(Jotun)", "(The Lady)", "finds a recruit", "Liberator", "Combat Transport II", "Rule of Two",
                                      // batch 6 (0.42.0)
                                      "Corporate Entity", "Holding Company", "Targeted Acquisition", "SEIDR Project", "Solis Morden",
                                      "A New Dawn", "Aurora Stormcaller", "Cunning", "Interference Grid", "Minefields", "Account Siphon",
                                      "Gravitic Inhibitor", "Wraith Engine", "Trusted Counselor", "Vasra Ivo", "Star's Razor",
                                      "Oro-Zhin Elite", "Combat Drones", "Orbital Defense Grid", "Rapid Excavation", "Logic Machina",
                                      "Dhor's Hammer", "(Nexus)", "blockaded and destroyed", "Spawning Grounds", "Parallel Production",
                                      "Ghoti Relay", "(Becece)", "Transcendence", "(Tioleombp)", "Networked Command"})
                    if (line.find(k) != std::string::npos) ++stats[std::string("ds      ") + k];
                // Stage 5: faction abilities, leaders, notes
                for (const char* k : {"unlocks their commander", "unlocks their hero", "exhausts their agent"}) {
                    size_t at = line.find(k);
                    size_t skip = std::string(k).find("their ") + 6;
                    if (at != std::string::npos) ++stats[std::string("leader  ") + line.substr(at + skip, line.size() - at - skip - 1)];
                }
                for (const char* k : {"uses Jace X", "uses Darktalon Treilla", "uses Rin, the Master", "uses Xxekir Grom",
                                      "uses Harrugh Gefhara", "uses Sh'val", "Orbital Drop", "(Production Biomes)", "Nullification Field:",
                                      "Spatial Conduit", "plays Military Support", "by Peace Accords", "quashes", "plays Political Favor",
                                      "(Quantum Datahub Node)", "with Instinct Training", "(Research Agreement)", "plays Tekklar Legion",
                                      "(War Funding)", "Genesis puts", "(Rear Admiral Farran)", "Valkyrie Exoskeleton strikes",
                                      "(Valkyrie Particle Weave)", "(Claire Gibson)", "(G'hom Sek'kus)", "(T'ro)", "(Gila the Silvertongue)",
                                      "Munitions Reserves reroll", "Wrath of Kenara", "(Exotrireme II)", "Dunlain Reaper on",
                                      "deploys a mech on", "Rerolled (Agnlan Oln)", "(Doctor Sucaban)", "Arc Secundus is repaired",
                                      "comes back next turn", "infantry return to", "(E-Res Siphons)", "Carth of Golden Sands",
                                      "refills commodities (Carth", "Ggrocuto Rinn", "Alliance", "plays War Funding",
                                      // batch 2
                                      "(Mitosis)", "(Bioplasmosis)", "plays Stymie", "(Letani Ospha)", "uses Letani Miasmiala",
                                      "(Dirzuga Rophal)", "(Duha Menaimon)", "assimilates", "Harrow (", "(I48S)",
                                      "plays Cybernetic Enhancements", "uses The Helmsman", "pillages", "(Suffi An)", "(Salvage Operations)",
                                      "(S'ula Mentarion)", "Ipswitch", "plays Promise of Protection", "(Foresight)", "plays Gift of Prescience",
                                      "(Telepathic)", "uses The Oracle", "(Matriarch)", "(Indoctrination)", "(Devotion)", "(Yin Spinner)",
                                      "plays Greyfire Mutagen", "(Brother Milor)", "uses Dannel", "Van Hauge explodes", "(Stall Tactics)",
                                      "(Mageon Implants)", "(Spy Net)", "uses Kyver", "(Neuroglaive)", "(Scheming)", "Inheritance Systems",
                                      "(Ssruu)", "uses Ssruu", "loses a fleet token", "(Annihilator)",
                                      // batch 3
                                      "puts the Creuss", "(Icarus Drive)", "(Emissary Taivra)", "(Sai Seravus)", "uses Riftwalker Meian",
                                      "uses the Dimensional Splicer", "(Star Forge)", "(Ember Colossus)", "Inferno forges", "calls on Umbat",
                                      "plays Fires of the Gashlai", "uses Adjudicator Ba'al", "(Magmus)", "(Scavenge)", "Captain Mendosa:",
                                      "(Rowl Sarrig)", "unleashes Gurno Aggero", "plays Ragh's Call", "with Chaos Mapping", "reclaims Mecatol Rex",
                                      "(Reclaimer)", "(Berekar Berekon)", "(Hegemonic Trade Policy)", "uses Mathis Mathinus", "plays Acquiescence",
                                      "(Lazax Gate Folding)", "(Blood Ties)", "(Galactic Threat)", "prediction was wrong",
                                      "(Technological Singularity)", "places Valefar Assimilator", "(Propagation)", "(Nekro Malleon)",
                                      "(Nekro Acidos)", "uses UNIT.DSGN.FLAYESH", "plays Antivirus", "(The Alastor)", "opens an Aetherpassage",
                                      "(Acamar)", "(Xuange)", "uses Conservator Procyon", "plays Blood Pact", "(Blood Pact)",
                                      "rides the Aetherstream", "(Dynamo)", "Watcher cancels", "(Voidwatch)", "(Galactic Threat).",
                                      // batch 4
                                      "(Zeal)", "Raid Formation damages", "Strike Wing Alpha II destroys", "(Trilossa Aun Mirik)",
                                      "uses Mirik Aun Sissiri", "plays Strike Wing Ambuscade", "fleet pool (Edict)", "(Jae Mir Kan)",
                                      "(Il Na Viroset)", "uses Airo Shir Aur", "Starlancer ends", "Crimson Legionnaires",
                                      "uses Genetic Recombination", "instead (Genetic Recombination)", "plays Scepter of Dominion",
                                      "Distant Suns", "(Fabrication)", "explore one of your planets", "Dart and Tai", "uses Hesh and Prit",
                                      "plays Black Market Forgery", "(Pre-Fab Arcologies)", "supercharges", "(Field Marshal Mercer)",
                                      "The Thundarian", "from Artuno", "uses Ahk-Syl Siven", "Quantum Manipulators cancel", "plays The Cavalry",
                                      "Cavalry ship shrugs", "(Temporal Command Suite)", "(Future Sight)", "(Terragenesis)", "awakens",
                                      "(Coalescence)", "deploys Ouranos", "deploys Hecatoncheires", "(Tellurian)", "(Tungstantus)",
                                      "uses Ul the Progenitor", "terraforms", "(Devour)", "(The Terror Between)", "(Reanimator)",
                                      "(Amalgamation)", "(Riftmeld)", "(The Stillness of Stars)", "uses It Feeds on Carrion", "plays Crucible",
                                      "(Vortex)", "captured units return", "Eidolons flip", "(Navarch Feng)",
                                      // Thunder's Edge (0.34.0)
                                      "gains their breakthrough", "joins the expedition", "Thunder's Edge is revealed",
                                      "The Fracture tears open", "infantry on Thunder's Edge", "An ingress token moves", "(Psychospore)",
                                      "(Vaults of the Heir)", "Deorbit Barrage on", "(N'orr Supremacy)", "N'orr Supremacy: gain",
                                      "(Thunder's Paradox)", "(Specialist Compounds)", "uses Mindsieve", "(Auto-Factories)",
                                      "(Fealty Uplink)", "coexisting units are alone", "Fracture stirs", "Avernus forms",
                                      "Avernus follows", "(The Nucleus)", "(Yin Ascendant)", "(Slumberstate Computing)",
                                      "coexist on", "(Wing Transfer)", "(Void Tether)", "Custodian's Favour",
                                      "(Valefar Assimilator Z)", "Eidolon Maximum rises", "Eidolon Maximum falls",
                                      "exhausts Custodia Vigilia", "Exhaust The Triad", "The Silver Flame", "(Specialist",
                                      // Thunder's Edge factions (0.35.0)
                                      "(Phoenix Standard)", "(Raise the Standard)", "(Dame Briar)", "(Liberate)",
                                      "Lyra Keen destroys", "Proxima Targeting VI", "(The Icon)", "(Hyperion VI)", "A3 Valiance",
                                      "(Survival Instinct)", "(Miniaturization)", "(Kan Kip Rel)", "(Watchful Ojz)", "(Director Nel)",
                                      "(Nanomachines)", "The Last Dispatch", "Alarum calls", "(Data Skimmer)",
                                      "(Oceanbound)", "units coexist on", "(Doctor Carrina", "(Aello)", "(Hydrothermal Mining)",
                                      "(Radical Advancement)", "(Ta Zern)", "(Share Knowledge)", "Visionaria Select", "Eanautic",
                                      "breach opens", "breach in", "seals the breach", "(Sundered)", "(Ahk Siever)", "(Subatomic Splicer)",
                                      "Homesick Phantom", "phantom fleet", "Revenant steps", "(Ahk Ravin)", "(Sever)"})
                    if (line.find(k) != std::string::npos) ++stats[std::string("faction ") + k];
                if (line.find(" casts ") != std::string::npos) ++stats["~ votes cast"];
                if (line.find("draws ") != std::string::npos && line.find("action card") != std::string::npos) ++stats["~ action card draws"];
                {  // 0.43.0 (debug passes): which secret objectives get scored
                    const size_t so = line.find("(secret objective ");
                    if (so != std::string::npos) {
                        const size_t e2 = line.find(')', so);
                        if (e2 != std::string::npos) ++stats["secret  " + line.substr(so + 18, e2 - so - 18)];
                    }
                }
            }
            if (g.custodiansRemoved) ++stats["~ games with agenda phase"];
            for (int q = 0; q < g.numPlayers; ++q)  // ... and which stay in hand unscored
                for (int16_t o : g.players[size_t(q)].secrets) {
                    ++stats[std::string("secheld ") + kObjectives[o].name];
                }
        }
        if (const char* lp = std::getenv("TI4_SIMLOG")) {  // 0.31.0: every game's log, for checking new rules
            // (0.42.1: TI4_SIMLOG=1 only prints - it used to write a file named "1" into the project)
            if (std::FILE* f = std::strcmp(lp, "1") ? std::fopen(lp, "a") : nullptr) {
                std::fprintf(f, "=== game %d\n", gi);
                for (const std::string& line : g.log) std::fprintf(f, "%s\n", line.c_str());
                std::fclose(f);
            }
        }
        if (g.winner >= 0 && g.players[g.winner].vp >= g.vpGoal) {
            ++vpWins;
            ++winRounds[g.round];
        }
        if (wantObj) {
            for (int16_t o : g.publicObjectives) {
                auto& st = objStats[std::string(kObjectives[o].kind == 2 ? "II  " : "I   ") + kObjectives[o].name];
                ++st.first;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (hasScored(g, q, o)) ++st.second;
            }
            for (int q = 0; q < g.numPlayers; ++q) {
                for (int16_t o : g.players[q].secrets) ++objStats[std::string("S   ") + kObjectives[o].name].first;
                for (int16_t o : g.players[q].scored)
                    if (kObjectives[o].kind == 3) {
                        auto& st = objStats[std::string("S   ") + kObjectives[o].name];
                        ++st.first;
                        ++st.second;
                    }
            }
        }
        // commodity trading: refills (log) and deals that handed commodities over
        for (const std::string& line : g.log)
            if (line.find("refills commodities") != std::string::npos) commRefills += 1;
        for (const DealRecord& d : g.deals)
            if (d.result == 1 && d.text.find("commodit") != std::string::npos) commSwaps += 1;
        for (int q = 0; q < g.numPlayers; ++q) commTgPerGame += double(g.players[q].commTg) / g.numPlayers;
        // negotiations
        for (const DealRecord& d : g.deals) {
            ++dealStats[d.result == 1 ? "made" : d.result == 2 ? "declined" : "fell through"];
            if (d.result == 1) ++dealStats[d.contract ? "  with components" : "  promises only"];
        }
        for (const Promise& p : g.promises) {
            static const char* kState[4] = {"promise in force at the end", "promise kept", "promise broken", "promise expired"};
            ++dealStats[kState[std::min<int>(3, p.state)]];
            ++dealStats[std::string("  kind: ") + promiseKindName(p.kind)];
        }
        dealRounds += g.round;
        if (pSeat >= 0 && pSeat < g.numPlayers) {
            psGames += 1;
            for (const DealRecord& d : g.deals) {
                const int late = d.round >= 4 ? 1 : 0;
                if (d.from == pSeat) psOff[late] += 1, psAcc[late] += d.result == 1 ? 1 : 0;
                if (d.to == pSeat) psToOff[late] += 1, psToAcc[late] += d.result == 1 ? 1 : 0;
            }
            psBroken += g.players[size_t(pSeat)].promisesBroken;
            psMec += g.players[size_t(pSeat)].mecHeld;
            psRaids += g.players[size_t(pSeat)].raids;
        }
        // personalities: wins, points and betrayals by archetype
        for (int q = 0; q < g.numPlayers; ++q) {
            const Personality& pe = g.players[q].pers;
            if (pe.arch < 0) continue;
            ArchStat& st = archStats[pe.arch];
            ++st.seats;
            st.vp += g.players[q].vp;
            st.betrayals += pe.betrayals;
            if (g.winner == q) ++st.wins;
            for (int b = 0; b < g.numPlayers; ++b) st.attacks += g.players[b].pers.attacks[q];
            for (const DealRecord& r : g.deals) {
                if (r.from == q) ++st.offers;
                if (r.to == q) {
                    ++st.received;
                    if (r.result == 1) ++st.accepted;
                }
            }
            st.broken += g.players[q].promisesBroken;
        }
        // where the victory points came from
        ++vpGames;
        if (g.winner >= 0 && g.winner < 8) ++winsBySeat[g.winner];
        {  // per round: how many players (and the winner) scored a public / a secret
            int round = 0;
            std::set<std::string> pubDone, secDone;
            auto flush = [&]() {
                if (round < 1 || round > 11) return;
                roundPlayers[round] += n;
                roundPub[round] += int(pubDone.size());
                roundSec[round] += int(secDone.size());
                if (g.winner >= 0) {
                    ++roundWinSeen[round];
                    roundWinPub[round] += pubDone.count(playerName(g, g.winner)) ? 1 : 0;
                    roundWinSec[round] += secDone.count(playerName(g, g.winner)) ? 1 : 0;
                }
                pubDone.clear();
                secDone.clear();
            };
            for (const std::string& line : g.log) {
                if (line.rfind("--- Round ", 0) == 0) {
                    flush();
                    round = std::atoi(line.c_str() + 10);
                    continue;
                }
                if (round >= 1 && round <= 11 && line.find(" activates ") != std::string::npos && line.find("Minister") == std::string::npos)
                    actsIn[round] += 1.0 / n;
                size_t k = line.find(" gains ");
                if (k == std::string::npos || line.find(" victory point") == std::string::npos) continue;
                std::string who = line.substr(0, k);
                if (line.find("(secret objective") != std::string::npos) secDone.insert(who);
                else
                    for (int ob = 0; ob < kObjectiveCount; ++ob)
                        if (kObjectives[ob].kind != 3 && line.find(std::string("(") + kObjectives[ob].name + ")") != std::string::npos) pubDone.insert(who);
            }
            flush();
        }
        for (const std::string& line : g.log) {
            size_t k = line.find(" gains ");
            if (k == std::string::npos || line.find(" victory point") == std::string::npos) continue;
            size_t o = line.find(" (", k), c = line.rfind(") - now");
            if (o == std::string::npos || c == std::string::npos || c < o) continue;
            int pts = std::atoi(line.c_str() + k + 7);
            std::string why = line.substr(o + 2, c - o - 2), src = "other";
            if (why.rfind("secret objective", 0) == 0) src = "secret";
            else if (why == "custodians token") src = "custodians";
            else if (why.rfind("Imperial:", 0) == 0) src = "imperial (Mecatol)";
            else if (why.rfind("Support for the Throne", 0) == 0) src = "support for the throne";
            else if (why == "Shard of the Throne" || why == "Crown of Emphidia") src = "relic";
            else if (why == "Imperial Rider" || why == "Political Censure" || why == "Mutiny" || why == "Seed of an Empire") src = "agenda";
            else
                for (int ob = 0; ob < kObjectiveCount; ++ob)
                    if (why == kObjectives[ob].name) src = kObjectives[ob].kind == 2 ? "public II" : "public I";
            vpAll[src] += pts;
            if (g.winner >= 0 && line.rfind(playerName(g, g.winner) + " gains ", 0) == 0) vpWinner[src] += pts;
        }
        if (std::getenv("TI4_SIMLOG")) {
            for (const std::string& line : g.log) std::printf("  | %s\n", line.c_str());
        }
        if (verbose) {
            std::printf("game %d: %d players, %d rounds, winner %s with %d VP, %d answers\n", gi, n, g.round,
                        g.winner >= 0 ? playerName(g, g.winner).c_str() : "-", g.winner >= 0 ? g.players[g.winner].vp : 0,
                        steps);
        }
    }
    for (const auto& kv : stats) std::printf("  %-48s %d\n", kv.first.c_str(), kv.second);
    for (const auto& kv : objStats)
        std::printf("  obj %-40s held/revealed %3d  scored %3d\n", kv.first.c_str(), kv.second.first, kv.second.second);
    if (vpGames) {
        int early = 0;
        std::printf("VP wins by round:");
        for (const auto& kv : winRounds) {
            std::printf(" r%d:%d", kv.first, kv.second);
            if (kv.first >= 6 && kv.first <= 8) early += kv.second;
        }
        std::printf("  (rounds 6-8: %d of %d games)\n", early, vpGames);
        std::printf("VP after round (leader / average):");
        for (int r = 1; r < 10; ++r)
            if (roundsSeen[r]) std::printf("  r%d %.1f/%.1f", r, leaderAt[r] / roundsSeen[r], avgAt[r] / roundsSeen[r]);
        std::printf("\nnon-home planets per player after round:");
        for (int r = 1; r < 10; ++r)
            if (roundsSeen[r]) std::printf("  r%d %.1f", r, planetsAt[r] / roundsSeen[r]);
        std::printf("\nres+inf of non-home planets per player after round:");
        for (int r = 1; r < 10; ++r)
            if (roundsSeen[r]) std::printf("  r%d %.1f", r, valueAt[r] / roundsSeen[r]);
        std::printf("\nplanets next to home: held by the home's owner %% / unclaimed %% after round:");
        for (int r = 1; r < 6; ++r)
            if (nearSeen[r]) std::printf("  r%d %.0f/%.0f", r, 100.0 * nearOwnAt[r] / nearSeen[r], 100.0 * nearFreeAt[r] / nearSeen[r]);
        if (galGames > 0)
            std::printf("\ngalaxy (%s): next to each home res+inf %.1f (spread %.1f), anomalies %.2f; legendary planets next to a home %.0f%%",
                        std::getenv("TI4_SIMRANDOMMAP") ? "random" : "draft", galNearValue / galHomes, galSpread / galGames, galNearAnom / galHomes,
                        100.0 * galLegNear / std::max(1.0, galLeg));
        std::printf("\ncommodities per game: refills %.1f, deals with commodities %.1f, trade goods from commodities per player %.1f",
                    commRefills / std::max(1, vpGames), commSwaps / std::max(1, vpGames), commTgPerGame / std::max(1, vpGames));
        std::printf("\nper player after round (carriers / ground forces / tactical actions in it):");
        for (int r = 1; r < 10; ++r)
            if (roundsSeen[r]) std::printf("  r%d %.1f/%.1f/%.1f", r, carriersAt[r] / roundsSeen[r], gfAt[r] / roundsSeen[r], actsIn[r] / std::max(1, vpGames));
        std::printf("\n");
        std::printf("wins by seat:");
        for (int q = 0; q < 8; ++q) std::printf(" s%d:%d", q, winsBySeat[q]);
        std::printf("\n");
        std::printf("scored in round (players public%%/secret%% | winner public%%/secret%%):");
        for (int r = 1; r < 10; ++r)
            if (roundPlayers[r])
                std::printf("  r%d %.0f/%.0f|%.0f/%.0f", r, 100.0 * roundPub[r] / roundPlayers[r], 100.0 * roundSec[r] / roundPlayers[r],
                            100.0 * roundWinPub[r] / std::max(1, roundWinSeen[r]), 100.0 * roundWinSec[r] / std::max(1, roundWinSeen[r]));
        std::printf("\n");
        std::printf("VP per game by source (all players | winner):");
        for (const auto& kv : vpAll)
            std::printf("  %s %.1f|%.1f", kv.first.c_str(), double(kv.second) / vpGames, double(vpWinner[kv.first]) / vpGames);
        std::printf("\n");
        std::printf("deals per game (%.1f rounds):", double(dealRounds) / vpGames);
        for (const auto& kv : dealStats) std::printf("  %s %.1f", kv.first.c_str(), double(kv.second) / vpGames);
        std::printf("\n");
        if (psGames > 0)
            std::printf("seat %d per game: its offers rounds 1-3 %.1f (%.0f%% taken), 4+ %.1f (%.0f%% taken); offers to it 1-3 %.1f (%.0f%% "
                        "taken), 4+ %.1f (%.0f%%); promises it broke %.2f; Mecatol turns held %.1f; home raids %.2f\n",
                        pSeat, psOff[0] / psGames, 100 * psAcc[0] / std::max(1.0, psOff[0]), psOff[1] / psGames,
                        100 * psAcc[1] / std::max(1.0, psOff[1]), psToOff[0] / psGames, 100 * psToAcc[0] / std::max(1.0, psToOff[0]),
                        psToOff[1] / psGames, 100 * psToAcc[1] / std::max(1.0, psToOff[1]), psBroken / psGames, psMec / psGames,
                        psRaids / psGames);
        if (!archStats.empty()) {
            std::printf("archetypes (seats, win %%, avg VP, betrayals per seat):");
            for (const auto& kv : archStats)
                std::printf("  %s %d/%.0f%%/%.1f/%.1f", kArchetypes[kv.first].name, kv.second.seats,
                            100.0 * kv.second.wins / std::max(1, kv.second.seats), double(kv.second.vp) / std::max(1, kv.second.seats),
                            double(kv.second.betrayals) / std::max(1, kv.second.seats));
            std::printf("\narchetypes (economy: next to home held after r2 %%, non-home planets / res+inf after r3):");
            for (const auto& kv : archStats)
                std::printf("  %s %.0f%%/%.1f/%.1f", kArchetypes[kv.first].name, 100.0 * kv.second.near2 / std::max(1, kv.second.seen2),
                            kv.second.planets3 / std::max(1, kv.second.seen3), kv.second.value3 / std::max(1, kv.second.seen3));
            std::printf("\narchetypes (attacks, offers made, %% of offers accepted, promises broken - per seat):");
            for (const auto& kv : archStats) {
                const ArchStat& a = kv.second;
                const double n = std::max(1, a.seats);
                std::printf("  %s %.1f/%.1f/%.0f%%/%.1f", kArchetypes[kv.first].name, a.attacks / n, a.offers / n,
                            100.0 * a.accepted / std::max(1, a.received), a.broken / n);
            }
            std::printf("\n");
        }
    }
    if (wantTempo) tempo.report(stdout);
    double secs = double(monotonicMs() - t0) / 1000.0;
    if (std::getenv("TI4_SIMWAYPOINTS")) std::printf("waypoints: %ld routes set (%ld through a gravity rift), %ld left out (the plan would be illegal)\n", g_wpRoutes, g_wpRift, g_wpDropped);
    std::printf("simulated %d games: %d finished, %d failed, %d AI answers rejected; avg %.1f rounds; %d won on VP; %d answers; "
                "%d undo checks; %.1f s\n",
                games, finishedGames, failures, rejected, finishedGames ? double(totalRounds) / finishedGames : 0.0, vpWins,
                totalAnswers, undos, secs);
    return failures == 0 ? 0 : 1;
}

}  // namespace ti
