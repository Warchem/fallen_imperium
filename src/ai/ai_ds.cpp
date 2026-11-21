// Computer players and the Discordant Stars factions' questions (0.37.0): Ab::Ds* (rules_ds.cpp). Called from
// aiFactionAnswer and aiFactionComponentValue. Rules of thumb like the rest of the AI; the engine checks every answer.
// Anything not handled here falls back to aiFactionAnswer's default (yes / the first option).
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "ai/ai.h"
#include "ai/ai_internal.h"
#include "engine/personality.h"
#include "engine/deal.h"
#include "engine/query.h"

namespace ti {

namespace {
Answer one(int id) {
    Answer a;
    a.picks.push_back(id);
    return a;
}
Answer yes(bool b) { return one(b ? 1 : 0); }
bool isYesNo(const Decision& d) { return d.options.size() == 2 && d.options[0].id == 1 && d.options[1].id == 0 && d.options[0].label == "Yes"; }
bool has(const Decision& d, int id) {
    for (const Option& o : d.options)
        if (o.enabled && o.id == id) return true;
    return false;
}
template <class F>
int bestBy(const Decision& d, F score, bool allowNegative = false) {
    int best = -1000000;
    float bv = -1e9f;
    for (const Option& o : d.options) {
        if (!o.enabled || (o.id < 0 && !allowNegative)) continue;
        const float v = score(o.id);
        if (v > bv) {
            bv = v;
            best = o.id;
        }
    }
    return best;
}
int maxOption(const Decision& d) {
    int m = -1;
    for (const Option& o : d.options)
        if (o.enabled) m = std::max(m, o.id);
    return m;
}
// how much an exploration card is worth on average (keep it, or let Vassa Hagi draw another)
float exploreWorth(int c) {
    if (c < 0 || c >= kExploreCardCount) return 1.f;
    if (!std::strcmp(kExploreCards[c].key, "dmz")) return -1.f;
    switch (kExploreCards[c].kind) {
        case ExploreKind::Fragment: return 2.f;
        case ExploreKind::Attach: return 1.6f;
        case ExploreKind::Chart: return 1.3f;
        case ExploreKind::Token: return 0.4f;
        default: return 1.f;
    }
}
// the action card we'd miss least
int cheapestCard(const Decision& d) {
    return bestBy(d, [](int c) { return -aiCardKeepValue(c); });
}
// enemy strength next to / in a system (non-fighter ships)
int threatNear(const Game& g, int p, int sys) {
    int n = 0;
    std::vector<int> zone{sys};
    for (int16_t a : g.systems[size_t(sys)].adj) zone.push_back(a);
    for (int z : zone)
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p) n += g.systems[size_t(z)].space[size_t(q)].nonFighterShips();
    return n;
}

// Sanctification Field (Rhodun, 0.44.5): what silencing one technology does in this tactical action - unit upgrades
// of the units here, the combat technologies, Gravity Drive of the active player (before they move) and the
// invasion ones; the research appeal no longer decides (it always picked Fleet Logistics or Sarween Tools)
float sanctifyValue(const Game& g, int z, int c) {
    if (c < 0) return 0.f;
    const int q = c / 1000, t = c % 1000;
    const int sys = g.activeSystem;
    if (q < 0 || q >= g.numPlayers || t < 0 || t >= kTechCount || sys < 0) return 0.f;
    const bool active = q == g.activePlayer;
    const TechDef& d = kTechs[t];
    if (d.unitUpgrade) {
        int type = -1;
        for (int u = 0; u < kUnitCount && type < 0; ++u)
            if (kUnits[u].tech == t) type = int(kUnits[u].type);
        if (type < 0) return 0.f;
        int n = g.systems[size_t(sys)].space[size_t(q)].count(UnitType(type));
        for (int16_t x : g.systems[size_t(sys)].planets) n += g.planets[size_t(x)].units[size_t(q)].count(UnitType(type));
        if (active)  // (ships that may still move in)
            for (int16_t a : g.systems[size_t(sys)].adj) n += g.systems[size_t(a)].space[size_t(q)].count(UnitType(type));
        return float(n) * (0.5f + 0.5f * float(unitCost2(g, q, UnitType(type))) / 2.f);
    }
    static const struct {
        const char* key;
        float active, other;  // the active player's / a defender's
    } kRelevant[] = {{"gd", 3.f, 0.f},   {"asc", 3.f, 3.f}, {"da", 2.5f, 2.5f}, {"ps", 2.f, 2.f},   {"amd", 1.5f, 0.5f},
                     {"gls", 0.5f, 1.5f}, {"x89_base", 2.f, 1.f}, {"l4", 2.f, 0.f}, {"md_base", 0.f, 2.f}, {"dxa", 1.5f, 1.f},
                     {"nes", 2.f, 2.f},  {"vw", 2.f, 2.f},  {"st", 1.f, 0.f},   {"aida", 0.5f, 0.f}, {"ie", 1.f, 0.f},
                     // warfare debug pass (0.44.8)
                     {"ds", 2.f, 2.f},   {"sc", 1.5f, 1.5f}, {"vpw", 1.f, 1.5f}, {"proxima", 1.f, 1.f}, {"mr", 1.f, 0.f}};
    // a Nekro's Valefar Assimilator: worth what the copied technology is worth (0.44.8, fixes 12 - before that a
    // blanked Valefar card kept its copy)
    if (g.fixes >= 12 && (!std::strcmp(d.key, "vax") || !std::strcmp(d.key, "vay"))) {
        const int copy = !std::strcmp(d.key, "vax") ? g.players[size_t(q)].assimX : g.players[size_t(q)].assimY;
        return copy >= 0 && copy != t ? sanctifyValue(g, z, q * 1000 + copy) : 0.f;
    }
    for (const auto& r : kRelevant)
        if (!std::strcmp(d.key, r.key)) return active ? r.active : r.other;
    (void)z;
    return 0.1f;
}

// Emergency Deployment (Axis, 0.44.5): how good a planet is for a new space dock - its resources, away from our
// other docks and from enemy fleets; never a DMZ (the engine leaves those out)
float dockSpot(const Game& g, int p, int x) {
    if (x < 0 || x >= int(g.planets.size())) return -1.f;
    const PlanetState& ps = g.planets[size_t(x)];
    const int sys = ps.system;
    float v = float(planetResources(g, x)) + 0.3f * float(planetInfluence(g, x));
    if (g.systems[size_t(sys)].home == p) v -= 3.f;  // (home has one)
    for (int16_t y : g.systems[size_t(sys)].planets)
        if (g.planets[size_t(y)].units[size_t(p)].count(UnitType::SpaceDock)) v -= 2.f;  // (one in this system already)
    v -= 1.2f * float(threatNear(g, p, sys));
    return v;
}
// the best spot, or -1.f
float bestDockSpot(const Game& g, int p) {
    float best = -1.f;
    for (int x : controlledPlanets(g, p))
        if (!g.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock) && !isDMZ(g, x)) best = std::max(best, dockSpot(g, p, x));
    return best;
}

// Mercenary Captains (Nokar, 0.44.4): how good a system is for the neutral fleet - next to the points leader's home or
// planets, near enemy fleets; never our own systems or next to our home
float nokarSpot(const Game& g, int p, int s) {
    const SystemState& sy = g.systems[size_t(s)];
    const int home = g.players[size_t(p)].homeSystem;
    if (s == home) return -10.f;
    for (int16_t x : sy.planets)
        if (g.planets[size_t(x)].owner == p) return -10.f;
    if (home >= 0)
        for (int16_t a : sy.adj)
            if (a == home) return -10.f;
    int leader = -1;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && !g.players[size_t(q)].eliminated && (leader < 0 || g.players[size_t(q)].vp > g.players[size_t(leader)].vp)) leader = q;
    float v = 0.f;
    if (leader >= 0) {
        const int lh = g.players[size_t(leader)].homeSystem;
        for (int16_t a : sy.adj)
            if (a == lh) v += 3.f;
        for (int16_t x : sy.planets)
            if (g.planets[size_t(x)].owner == leader) v += 2.f;
    }
    return v + 0.3f * float(threatNear(g, p, s));
}
// Spark Thrusters (Lanefir, 0.43.0 debug passes): a lone ship with nothing aboard slips over to join a fleet of ours
// next door (option id = (from * 100 + to) * 16 + type); -1 = not worth it
float sparkScore(const Game& g, int p, int id) {
    if (id < 0) return -1.f;
    const int from = (id / 16) / 100, to = (id / 16) % 100;
    const UnitType t = UnitType(id % 16);
    if (from < 0 || to < 0 || size_t(from) >= g.systems.size() || size_t(to) >= g.systems.size()) return -1.f;
    if (t == UnitType::Fighter || from == g.players[size_t(p)].homeSystem) return -1.f;
    const Force& src = g.systems[size_t(from)].space[size_t(p)];
    if (src.nonFighterShips() != 1 || transportedInSpace(g, p, from) > 0) return -1.f;  // (nothing left behind)
    const int join = g.systems[size_t(to)].space[size_t(p)].nonFighterShips();
    if (join == 0 || fleetUseIn(g, p, to) + 1 > g.players[size_t(p)].fleet) return -1.f;
    return 0.5f + 0.1f * float(std::min(join, 5)) + 0.2f * float(std::min(threatNear(g, p, from), 3));
}
float sparkBest(const Game& g, int p) {
    float best = -1.f;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        const Force& f = g.systems[s].space[size_t(p)];
        if (f.nonFighterShips() != 1) continue;
        for (int a : routeNeighbors(g, p, int(s))) {
            if (g.systems[size_t(a)].isHyperlane() || otherShipsIn(g, p, a)) continue;
            for (int t : kShipIdx)
                if (f.n[t]) best = std::max(best, sparkScore(g, p, (int(s) * 100 + a) * 16 + t));
        }
    }
    return best;
}
// the relic we'd rather keep (Data Leak)
float relicPref(int r) {
    if (r < 0 || r >= kRelicCount) return 0.f;
    static const struct {
        const char* key;
        float v;
    } kPref[] = {{"shard", 9}, {"emphidia", 6}, {"obsidian", 5}, {"emelpar", 5}, {"thalnos", 5}, {"dominusorb", 4}, {"mawofworlds", 4},
                 {"prophetstears", 4}, {"stellarconverter", 4}, {"codex", 4}, {"quantumcore", 5}, {"heartofixth", 4}, {"thetriad", 4}};
    for (const auto& k : kPref)
        if (!std::strcmp(kRelics[r].key, k.key)) return k.v;
    return 3.f;
}
float planetWorth(const Game& g, int x) { return x < 0 ? -1.f : float(planetResources(g, x) + planetInfluence(g, x)); }
// how good an objective is for us now: scored at once beats almost there
float objectiveFit(const Game& g, int p, int o) {
    if (o < 0) return -1.f;
    if (meetsObjective(g, p, o)) return 10.f + float(kObjectives[o].points);
    const ObjProgress pr = objectiveProgress(g, p, o);
    return pr.need > 0 ? 5.f * float(pr.have) / float(pr.need) : 0.f;
}
// the side we fight in the active combat (space)
float spaceOdds(const Game& g, int p) {
    const int sys = g.activeSystem;
    if (sys < 0) return 0.5f;
    for (int q = 0; q < kSides; ++q) {
        if (q == p || (q >= g.numPlayers && q != kNeutral)) continue;
        if (!g.systems[size_t(sys)].space[size_t(q)].ships()) continue;
        const Force& mine = g.systems[size_t(sys)].space[size_t(p)];
        const Force& theirs = g.systems[size_t(sys)].space[size_t(q)];
        return p == g.activePlayer ? fightOdds(g, p, mine, q, theirs, true) : 1.f - fightOdds(g, q, theirs, p, mine, true);
    }
    return 0.5f;
}
}  // namespace

bool aiRohdhnaFactory(const Game& g, int p, bool money) {
    if (p < 0 || p >= g.numPlayers || !isFaction(g, p, "rohdhna")) return false;
    if (g.round < 2 && aiTune("rhind", 0.f) <= 0.f) return false;
    if (unitsForPlacement(g, p, UnitType::WarSun) <= 0 || unitsAvailable(g, p, UnitType::SpaceDock) <= 0) return false;
    if (money && spendable(g, p, false) < 6) return false;
    const Player& me = g.players[size_t(p)];
    for (int x : controlledPlanets(g, p)) {
        const int s = g.planets[size_t(x)].system;
        if (s == me.homeSystem || otherShipsIn(g, p, s) || g.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock)) continue;
        return true;
    }
    return false;
}

bool aiDsAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out) {
    const int p = d.player;
    if (p < 0 || p >= g.numPlayers) return false;
    const Player& me = g.players[size_t(p)];
    const Ab ab = Ab(d.ability);
    if (int(ab) < int(Ab::DsZealous)) return false;
    const bool yn = isYesNo(d);
    switch (ab) {
        case Ab::DsPyndil: {  // (0.43.4) the type with the most capacity that can reach the active system
            const int act = g.activeSystem;
            out = one(bestBy(d, [&](int t) {
                float v = float(unitDef(g, p, UnitType(t)).capacity);
                for (size_t s = 0; s < g.systems.size(); ++s)
                    if (g.systems[s].space[size_t(p)].n[size_t(t)] > 0 && act >= 0)
                        v += hexDistance(g.systems[s].pos, g.systems[size_t(act)].pos) <= 2 ? 3.f : 0.f;
                return v;
            }));
            return true;
        }
        case Ab::DsClansFavor:  // only to save a fleet that is losing
            if (yn) {
                out = yes(spaceOdds(g, p) < 0.3f && g.systems[size_t(std::max(0, int(g.activeSystem)))].space[size_t(p)].nonFighterShips() >= 2);
                return true;
            }
            if (g.fixes >= 29 && d.prompt.find("which action card") != std::string::npos) {  // (the Vaylerian review) the card we need least
                out = one(bestBy(d, [&](int c) { return -aiCardKeepValue(c); }, true));
                return true;
            }
            return false;
        case Ab::DsYvin:
            if (g.fixes >= 29 && d.value >= 1000) {  // (the Vaylerian review) another player's planet: only for the player it was promised to
                const int b = d.value - 1000;
                out = yes(b == p || (d.targetPlayer >= 0 && b == d.targetPlayer));
                return true;
            }
            return false;
        case Ab::DsAts: {  // rerolls: a couple per round, all of them in a close fight
            const int m = maxOption(d);
            out = one(std::min(m, d.value ? (spaceOdds(g, p) < 0.7f ? m : 2) : 1));
            return true;
        }
        case Ab::DsPhquaiset: out = one(maxOption(d)); return true;
        case Ab::DsZealous:  // (the Kortali review, checkpoint 2 - test knob kozeal, OFF) Thunder's Edge: the primary only with a plan
            // worth the token (aiFreeTacticalValue), else the secondary's Production at home - unless there is nothing to spend
            if (yn && aiTune("kozeal", 0.f) > 0.f && (g.modules & kModThundersEdge)) {
                out = yes(aiFreeTacticalValue(g, p) >= aiTune("kozeal", 0.f) || spendable(g, p, false) < 3);
                return true;
            }
            return false;
        case Ab::DsRuthless:  // (fixes 28, the Kortali review) the kind that dies: a damaged mech, else infantry, else the mech
            if (yn) return false;
            out = one(bestBy(d, [&](int t) {
                const int pl = g.groundCombatPlanet;
                int dmg = 0;
                for (int q = 0; q < g.numPlayers && pl >= 0; ++q)
                    if (q != p) dmg += g.planets[size_t(pl)].units[size_t(q)].damaged(UnitType(t));
                return t == int(UnitType::Mech) ? (dmg > 0 ? 3.f : 1.f) : 2.f;
            }));
            return true;
        case Ab::DsTempest:  // (fixes 28) with another player's token there: only our own token (removing theirs frees their system)
            if (yn) return false;
            out = one(has(d, p) ? p : -1);
            return true;
        case Ab::DsLorena:  // (fixes 28) a barrage hit costs a fighter: kept for the combat roll unless only fighters fight here
            if (yn && d.value == 1) {
                const int s = g.activeSystem;
                out = yes(s < 0 || g.systems[size_t(s)].space[size_t(p)].nonFighterShips() == 0);
                return true;
            }
            return false;
        case Ab::DsLucreia:  // (fixes 28: Yes / No / don't ask again) - a bot is asked only for itself or the player it promised it to
            if (yn) return false;
            out = one(1);
            return true;
        case Ab::DsNadalia:
            if (yn) {
                out = yes(d.value >= 3 || g.round >= 6 || me.vp >= g.vpGoal - 3);
                return true;
            }
            return false;
        case Ab::DsPustule:
        case Ab::DsScour: {
            // (the Vaylerian review, checkpoint 2 - test switch vyscour) Scour never came up in 270 games: a planet worth 2 for a card worth
            // less than 3.5. REVISIT (checkpoint 2, 90 forced games each): planets readied +15 / +43 in 90 games - 19 wins / 7.19 VP against
            // 18 / 7.04, second sample 14 / 6.81 against 15 / 7.11: not clear in both - off
            if (ab == Ab::DsScour && aiTune("vyscour", 0.f) > 0.f) {
                const int c = d.value >= 2 ? cheapestCard(d) : -1;
                out = one(c >= 0 && aiCardKeepValue(c) < 3.5f ? c : -1);
                return true;
            }
            if (ab == Ab::DsScour && d.value < (fquirkIs(g, p, "vaylerian", 1) ? 2 : 3)) {  // a planet worth a card (Card Sharp: cheaper)
                out = one(-1);
                return true;
            }
            const int c = cheapestCard(d);
            out = one(c >= 0 && aiCardKeepValue(c) < 2.5f ? c : -1);
            return true;
        }
        case Ab::DsChitin: out = one(has(d, int(UnitType::Destroyer)) ? int(UnitType::Destroyer) : has(d, int(UnitType::Fighter)) ? int(UnitType::Fighter) : -1); return true;
        case Ab::DsDyln:
            if (yn) {
                out = yes(d.value == 1 && (g.round >= 3 || me.vp >= g.vpGoal - 4));
                return true;
            }
            return false;
        case Ab::DsBroodPod:
            if (yn) {
                out = yes(spendable(g, p, false) >= 3 && threatNear(g, p, std::max(0, int(g.activeSystem))) > 0);
                return true;
            }
            out = Answer();  // keep the structures
            return true;
        case Ab::DsTitus: {
            // (the Celdauri review, checkpoint 2 - test knob titusdef) the hero as a defence: another player activates a
            // system with our planet and has warships next to it or in it - a dock and Production before they move in
            bool defend = false;
            if (aiTune("titusdef", 0.f) > 0.f && d.value != p && d.value >= 0 && d.value < g.numPlayers && g.round >= 2 && g.activeSystem >= 0 &&
                spendable(g, p, false) >= 3) {
                int foes = g.systems[size_t(g.activeSystem)].space[size_t(d.value)].nonFighterShips();
                for (int16_t a : g.systems[size_t(g.activeSystem)].adj) foes += g.systems[size_t(a)].space[size_t(d.value)].nonFighterShips();
                defend = foes >= 2;
            }
            if (!defend && (d.value != p || g.round < 3 || spendable(g, p, false) < 4)) {
                out = one(-1);
                return true;
            }
            {
                const int x = bestBy(d, [&](int x) { return float(planetResources(g, x) + planetInfluence(g, x)); });
                bool bare = false;  // (fixes 28) "Use him (no space dock to place)"
                for (const Option& o : d.options) bare = bare || (o.id == -2 && o.enabled);
                out = one(x >= 0 ? x : bare ? -2 : -1);
            }
            return true;
        }
        case Ab::DsMinuteman:
            if (d.value == p && threatNear(g, p, std::max(0, int(g.activeSystem))) == 0) {  // our own move with nobody about
                out = one(-1);
                return true;
            }
            out = one(bestBy(d, [&](int x) { return float(planetResources(g, x) + planetInfluence(g, x)); }));
            return true;
        case Ab::DsNobin: {
            // (fixes 28) "exhaust George Nobin so that player may place a dock?" - a bot only when it promised (kept in
            // ai_deals; here a lapsed promise or the bare promise list)
            if (g.fixes >= 28 && d.targetPlayer >= 0 && d.targetPlayer != p) {
                out = one(g.agentFor[size_t(p)] == d.targetPlayer ? 1 : 0);
                return true;
            }
            // (the Celdauri review, checkpoint 2 - test knob celdock) the strategy guide: the agent's dock goes on a good
            // non-home planet, ideally one away from home that unlocks Henry Storcher - not a second dock at home while
            // the commander is still locked
            if (aiTune("celdock", 0.f) > 0.f && isFaction(g, p, "celdauri") && g.activeSystem >= 0 && g.systems[size_t(g.activeSystem)].home == p &&
                leaderStatus(g, p, kCommander) == kLocked) {
                out = one(-1);
                return true;
            }
            const int x = bestBy(d, [&](int x) { return float(planetResources(g, x) + planetInfluence(g, x)); });
            out = one(x >= 0 ? x : -1);
            return true;
        }
        case Ab::DsEmergencyMob: {
            // (0.44.8) a dock system to activate again - home or one with enemies near - while we still have turns, a
            // tactic token and money to build (it used to wait for home with 2 strategy and at most 1 tactic: never)
            const int home = me.homeSystem;
            if (me.passed || me.tactic < 1 || aiFreeSpend(g, p, false) < 3 || me.strategy < 1) {
                out = one(-1);
                return true;
            }
            const int s = bestBy(d, [&](int id) {
                if (id < 0 || id >= int(g.systems.size())) return -1.f;
                return float(threatNear(g, p, id)) + (id == home ? 1.f : 0.f);
            }, 0.5f);
            out = one(s >= 0 && (me.strategy >= 2 || threatNear(g, p, s) > 0) ? s : -1);
            return true;
        }
        case Ab::DsUdosh:  // a resource planet pays for influence: worth it when it carries 2+ resources
            if (isYesNo(d) && g.fixes >= 28 && d.targetPlayer >= 0 && d.targetPlayer != p) {  // (the Khrask review) asked for another
                out = yes(g.agentFor[p] == d.targetPlayer);                                   // player only when promised: keep it
                return true;
            }
            if (isYesNo(d)) {
                int best = 0;
                for (int x : readyPlanets(g, p))
                    if (g.planets[size_t(x)].system != g.players[size_t(p)].homeSystem) best = std::max(best, planetResources(g, x));
                out = yes(best >= 2);
            } else {
                out = one(bestBy(d, [&](int x) { return x < 0 ? 0.f : float(planetResources(g, x) * 2 + planetInfluence(g, x)) - 3.f; }, true));
            }
            return true;
        case Ab::DsThakt: {
            // (fixes 28) Thakt Clqua places each unit on its own: everything where enemies are nearest our structures, home
            // first - the ships together (the scores don't change between the questions), ground forces on planets
            if (d.prompt.rfind("Thakt Clqua: place 1 ", 0) != 0) return false;
            const bool structure = d.prompt.find("Space Dock") != std::string::npos || d.prompt.find("PDS") != std::string::npos;
            const bool ground = d.prompt.find("Infantry") != std::string::npos || d.prompt.find("Mech") != std::string::npos;
            auto sysScore = [&](int s) {
                if (s < 0 || s >= int(g.systems.size())) return -100.f;
                return 2.f * float(threatNear(g, p, s)) + (g.systems[size_t(s)].home == p ? 1.5f : 0.f);
            };
            out = one(bestBy(d, [&](int id) {
                if (id >= 10000) {
                    const int x = id - 10000;
                    const float base = float(planetResources(g, x) + planetInfluence(g, x)) * 0.2f;
                    if (structure) return float(planetResources(g, x)) + (g.systems[size_t(g.planets[size_t(x)].system)].home == p ? 0.f : 1.f) +
                                          0.5f * sysScore(g.planets[size_t(x)].system);
                    return sysScore(g.planets[size_t(x)].system) + base;
                }
                return ground ? sysScore(id) - 5.f : sysScore(id);
            }));
            return true;
        }
        case Ab::DsKkavras: {  // (0.43.0 debug passes: asked ~600 times, always no) our own token off a system whose
            // fleet or dock we'd use again this round, onto one that doesn't matter
            // (fixes 28) "exhaust Operator Kkavras so that player may move a token?" - a bot only when it promised
            if (g.fixes >= 28 && d.targetPlayer >= 0 && d.targetPlayer != p) {
                out = one(g.agentFor[size_t(p)] == d.targetPlayer ? 1 : 0);
                return true;
            }
            if (g.activePlayer != p || g.players[size_t(p)].tactic < 1) {  // (only for our own token, at our turn's end)
                out = one(-1);
                return true;
            }
            auto worth = [&](int s) {
                bool dock = false;
                for (int16_t x : g.systems[size_t(s)].planets) dock = dock || g.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock) > 0;
                return float(g.systems[size_t(s)].space[size_t(p)].nonFighterShips()) + (dock ? 2.f : 0.f);
            };
            auto score = [&](int id) { return id < 0 ? -1.f : worth(id / 1000) - worth(id % 1000) - 2.f; };
            const int b = bestBy(d, score);
            out = one(b >= 0 && score(b) > 0.f ? b : -1);
            return true;
        }
        case Ab::DsTroubadour:
            if (d.prompt == "Troubadour: end your turn now?") {  // 0.43.0: the bots act (waiting isn't in their plans)
                out = yes(false);
                return true;
            }
            if (fragmentCount(g, p) < (fquirkIs(g, p, "lanefir", 1) ? 1 : 2) && unitsOnBoard(g, p, UnitType::Mech) >= 2) {  // (Iconoclast Scholar)
                out = one(-1);
                return true;
            }
            out = one(bestBy(d, [&](int x) {
                const int s = g.planets[size_t(x)].system;
                return float(threatNear(g, p, s)) + 0.1f * float(planetResources(g, x) + planetInfluence(g, x));
            }));
            return true;
        case Ab::DsVassa:
            if (yn) {
                out = yes(exploreWorth(d.value) <= 0.5f);
                return true;
            }
            return false;
        case Ab::DsEdifice:  // explore all of them
            out = Answer();
            for (const Option& o : d.options)
                if (o.enabled && int(out.picks.size()) < d.maxPick) out.picks.push_back(o.id);
            return true;
        case Ab::DsContagion: {
            // 0.43.0 (D14): the personality decides how bold the plague is - the aggressive ones go for a planet held by a
            // single defender (Subversive: a draw is theirs), the rest only for empty ones
            const int a = archNow(g, p);
            const bool bold = a == int(Arch::Warlord) || a == int(Arch::Betrayer) || a == int(Arch::Rusher) || a == int(Arch::Chaotic) ||
                              a == int(Arch::Opportunist);
            // (the Kyro review, checkpoint 2 - test knob kycontag, OFF) every personality goes for a single defender (Subversive: 1 infantry
            // against 1 wins ~59% of the time, a draw being theirs)
            const int allowed = (bold || aiTune("kycontag", 0.f) > 0.f) && g.fixes >= 2 ? 1 : 0;
            out = one(bestBy(d, [&](int x) {
                const PlanetState& ps = g.planets[size_t(x)];
                int foes = neutralGroundOn(g, x);
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p) foes += groundForcesOn(g, q, x);
                if (foes > allowed) return -10.f;
                // (fixes 28, the Kyro review) their own planets are targets now: an infantry more there only when nothing else is
                if (ps.owner == p) return 0.2f;
                float v = float(planetResources(g, x) + planetInfluence(g, x)) + 1.f;
                // (fixes 28) the planet's space cannon defense fires at the infantry: worth less
                if (g.fixes >= 28) {
                    bool guns = false;
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p && g.planets[size_t(x)].units[size_t(q)].count(UnitType::PDS)) guns = true;
                    if (guns) v *= 0.4f;
                }
                if (ps.owner >= 0 && ps.owner != p) {
                    const float rel = float(me.pers.bf[size_t(ps.owner)]);
                    v += 1.f - 0.05f * rel;  // a rival's empty planet is a fine prize
                }
                if (!std::strcmp(kPlanets[ps.def].key, "mr")) v += 4.f;
                return foes ? v * 0.6f : v;
            }));
            // (fixes 28: no "Hold back" any more - then the best of what is offered, their own planet first)
            if (out.picks[0] >= 0 && has(d, -1)) {
                int foes = neutralGroundOn(g, out.picks[0]);
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p) foes += groundForcesOn(g, q, out.picks[0]);
                if (foes > allowed) out = one(-1);
            } else if (out.picks[0] < 0 && has(d, -1)) {
                out = one(-1);
            }
            return true;
        }
        case Ab::DsKyroRider: {  // the same judgement as the Keleres Rider
            Decision d2 = d;
            d2.ability = int(Ab::KeleresRider);
            return aiFactionAnswer(g, d2, rng, out);
        }
        case Ab::DsTox:  // (fixes 28: Yes / No / don't ask again) a bot is asked for itself or the player it promised it to
            if (yn || d.prompt.find("which planet") != std::string::npos) return false;
            out = one(1);
            return true;
        case Ab::DsSilas: {
            const int m = maxOption(d);
            out = one(g.round >= 5 || me.vp >= g.vpGoal - 3 || fquirkIs(g, p, "kyro", 1) ? m : 0);  // (Pale Rider: always)
            return true;
        }
        case Ab::DsVector:  // the card we would like most ourselves (our own trade goods stay with us - 0.43.0)
            if (d.prompt.find("your own") != std::string::npos) {
                out = one(-1);
                return true;
            }
            out = one(bestBy(d, [&](int c) { return aiStrategyCardValue(g, p, c); }));
            return true;
        case Ab::DsVenerable:
            if (d.prompt.find("for which relic") != std::string::npos) {  // (fixes 28: one question per relic in play) the leader's for a revealed one
                int leader = -1;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p && (leader < 0 || g.players[size_t(q)].vp > g.players[size_t(leader)].vp)) leader = q;
                int pickId = -1;
                if (d.value == leader && leader >= 0 && g.players[size_t(leader)].vp >= g.vpGoal - 4)
                    for (const Option& o : d.options)
                        if (o.enabled && o.id >= 0 && o.id < 100000) {
                            pickId = o.id;
                            break;
                        }
                out = one(pickId);
                return true;
            }
            if (d.prompt.find("swap") != std::string::npos) {  // a swap only to hurt the leader
                int leader = -1;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p && (leader < 0 || g.players[size_t(q)].vp > g.players[size_t(leader)].vp)) leader = q;
                int pickId = -1;
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0 && o.id / 1000 == leader && g.players[size_t(leader)].vp >= g.vpGoal - 4) pickId = o.id;
                out = one(pickId);
                return true;
            }
            return false;
        case Ab::DsSparkMove: out = one(bestBy(d, [&](int id) { return sparkScore(g, p, id); })); return true;
        case Ab::DsSpark: out = one(has(d, 1) && (g.players[size_t(p)].strategy >= 3 || !has(d, 0)) ? 1 : 0); return true;  // pay with?
        // ---- batch 2 (0.38.0)
        case Ab::DsMeteorSlings:
        case Ab::DsStoneSpeakers: {  // hits beyond the defenders become infantry
            const int pl = d.value;
            int defenders = 0;
            for (int q = 0; q < kSides; ++q)
                if (q != p && pl >= 0) defenders += g.planets[size_t(pl)].units[size_t(q)].groundForces();
            const int m = maxOption(d);
            int hits = m;
            for (const Option& o : d.options) hits = std::max(hits, o.id);
            // (the Khrask review, checkpoint 2 - test switch khgrove, OFF) with Mended Grove, against more defenders than hits: every hit
            // an infantry, coexisting (the Grove's influence grows by 1 for each planet with coexisting units). 270 games: 63 wins / 7.42
            // against 61 / 7.33, coexistence 0.5 a game (1 in 270 without) - within the noise
            if (d.ability == int(Ab::DsMeteorSlings) && aiTune("khgrove", 0.f) > 0.f && breakthroughIs(g, p, "khrask") && breakthroughsOn(g) &&
                defenders > 0 && defenders >= hits && m == hits) {
                out = one(m);
                return true;
            }
            out = one(std::max(0, std::min(m, hits - defenders)));
            return true;
        }
        case Ab::DsBypass:
            if (yn) {
                bool guns = false;
                const int sys = g.activeSystem;
                if (sys >= 0)
                    for (int16_t x : g.systems[size_t(sys)].planets)
                        for (int q = 0; q < g.numPlayers; ++q)
                            if (q != p && (g.planets[size_t(x)].units[size_t(q)].count(UnitType::PDS) || g.planets[size_t(x)].units[size_t(q)].count(UnitType::Mech)))
                                guns = true;
                out = yes(guns);
                return true;
            }
            return false;
        case Ab::DsZelianAgent:
            if (g.fixes >= 29 && d.value >= 1000) {  // (the Zelian review) another player's invasion: only for the player it was promised to
                const int b = d.value - 1000;
                out = yes(b == p || (d.targetPlayer >= 0 && b == d.targetPlayer));
                return true;
            }
            return false;
        case Ab::DsHyperkinetic:
            if (yn && g.fixes >= 29) {  // (the Zelian review) played before the dice: with 2+ dice, or late in the game
                out = yes(d.value >= 2 || g.round >= 5);
                return true;
            }
            return false;
        case Ab::DsCollider:
            out = one(bestBy(d, [&](int c) {
                const int t = (c / 8) % 16;
                return t == int(UnitType::Mech) ? 3.f : t == int(UnitType::PDS) ? 2.5f : t == int(UnitType::SpaceDock) ? 2.f : 1.f;
            }));
            return true;
        case Ab::DsObsessive:
            if (yn) {
                out = yes(spendable(g, p, false) >= 4);
                return true;
            }
            return false;
        case Ab::DsTurra:
            if (yn) {
                out = yes(d.value >= 1);
                return true;
            }
            return false;
        case Ab::DsDeliberate: out = one(has(d, 0) ? 0 : has(d, 2) ? 2 : has(d, 1) ? 1 : -1); return true;
        case Ab::DsStoneEmbrace:
            out = one(bestBy(d, [&](int x) { return -float(planetResources(g, x) + planetInfluence(g, x)); }));
            if (me.strategy >= 3) out = one(-1);  // (plenty of tokens: keep the planets)
            return true;
        case Ab::DsIndustrious:
            if (yn) {
                // (balance pass 0.66.0 - knob rhnoind, ON: the bots don't use Industrious) a war sun from a dock is Terrafactory I -
                // it can't move: 6 resources tied to one system. Every way of using it more cost the bots wins (see ai.cpp
                // ConstructionChoice: 31-35 wins against 39 in 360 games); never using it, with rhrecycle (630 games in four
                // samples): 76 wins / 6.81 VP against 70 / 6.66 - only on the front (rhfront) 73 / 6.76. rhnoind=0 = the old
                // way (yes from round 2; test knob rhind: round 1 too)
                out = yes((g.round >= 2 || aiTune("rhind", 0.f) > 0.f) && aiTune("rhnoind", 1.f) <= 0.f);
                // (balance pass 0.66.0 - test knob rhfront, OFF) only on the front: other players' ships within 2 systems of the
                // dock, where a war sun that can't move still fights
                if (aiTune("rhfront", 0.f) > 0.f) {
                    int sys = -1;
                    for (int x : controlledPlanets(g, p))
                        if (g.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock) &&
                            d.prompt.find(std::string(" on ") + kPlanets[g.planets[size_t(x)].def].name + " into") != std::string::npos)
                            sys = g.planets[size_t(x)].system;
                    bool front = false;
                    if (sys >= 0)
                        for (int s : systemsWithin(g, sys, 2))
                            for (int q = 0; q < g.numPlayers; ++q)
                                if (q != p && g.systems[size_t(s)].space[size_t(q)].nonFighterShips() > 0) front = true;
                    out = yes(g.round >= 2 && front);
                }
                return true;
            }
            return false;
        case Ab::DsCataclysm:
            if (yn) {
                // (the Zelian review, checkpoint 2 - test switch zlcata) the guide: "Use the hero on a system a rival needs" - a lower bar
                // (3 trade goods, or from round 5) instead of 4 / round 6; used in 58-70% of their games at the table's bar. REVISIT
                // (checkpoint 2, 90 forced games each): used in 0.79-0.82 of their games (0.66-0.70) - 15 wins / 7.49 VP against 19 /
                // 7.51, second sample 12 / 6.90 against 14 / 6.93: worse in both - off
                if (aiTune("zlcata", 0.f) > 0.f) {
                    out = yes(d.value >= 3 || g.round >= 5);
                    return true;
                }
                out = yes(d.value >= 4 || g.round >= 6);
                return true;
            }
            return false;
        case Ab::DsPrimespace:
            if (yn) {
                out = yes(d.value >= 6 && spendable(g, p, false) >= 6);
                return true;
            }
            return false;
        case Ab::DsAutomatons: out = one(bestBy(d, [&](int x) { return float(planetResources(g, x)); })); return true;
        case Ab::DsContractual:
            // (0.44.9) ourselves only with money to spare (a ship we pay for); the others: whoever's votes and lead hurt most
            out = one(bestBy(d, [&](int q) {
                if (q == p) return spendable(g, p, false) >= 8 && g.players[size_t(p)].vp < g.vpGoal - 3 ? 1.f : -1.f;
                return float(availableVotes(g, q)) + (g.players[size_t(q)].vp >= me.vp ? 3.f : 0.f);
            }));
            return true;
        case Ab::DsSplinteringGale: {  // (the Khrask review) the dearest ships first
            std::vector<std::pair<int, int>> c;
            for (const Option& o : d.options)
                if (o.enabled) c.push_back({unitCost2(g, p, UnitType(o.id / 16)) * 4 - o.id % 16, o.id});
            std::sort(c.rbegin(), c.rend());
            out = Answer();
            for (size_t i = 0; i < c.size() && int(i) < d.maxPick; ++i) out.picks.push_back(c[i].second);
            return true;
        }
        case Ab::DsVehlTikar: {  // wake our own spent planets, best first
            std::vector<std::pair<float, int>> c;
            for (const Option& o : d.options)
                if (o.enabled && g.planets[size_t(o.id)].owner == p && g.planets[size_t(o.id)].exhausted)
                    c.push_back({float(planetResources(g, o.id) + planetInfluence(g, o.id)), o.id});
            std::sort(c.rbegin(), c.rend());
            out = Answer();
            for (size_t i = 0; i < c.size() && int(i) < d.maxPick; ++i) out.picks.push_back(c[i].second);
            return true;
        }
        case Ab::DsSaintBinal: {
            std::vector<std::pair<float, int>> c;
            for (const Option& o : d.options)
                if (o.enabled) c.push_back({aiTechWant(g, p, o.id), o.id});
            std::sort(c.rbegin(), c.rend());
            out = Answer();
            for (size_t i = 0; i < c.size() && int(i) < d.maxPick; ++i) out.picks.push_back(c[i].second);
            return true;
        }
        case Ab::DsSaintKeep:
        case Ab::DsMeditation: out = one(bestBy(d, [&](int t) { return aiTechWant(g, p, t); })); return true;
        case Ab::DsFran: {
            const int prom = aiAgentPromisedTo(g, p);  // (rules 28) the promised player
            out = one(prom >= 0 && has(d, prom) ? prom : has(d, p) ? p : maxOption(d));
            return true;
        }
        case Ab::DsSanctification: out = one(bestBy(d, [&](int c) { return sanctifyValue(g, p, c); }, 0.5f)); return true;
        case Ab::DsPriestess:
            if (g.fixes >= 29 && d.value >= 1000) {  // (the Zealots review) another player's research: only for the player it was promised to
                const int b = d.value - 1000;
                out = yes(b == p || (d.targetPlayer >= 0 && b == d.targetPlayer));
                return true;
            }
            if (g.fixes >= 29) {  // the system: home, or where a threat is near
                out = one(bestBy(d, [&](int s) { return s < 0 ? -1.f : 1.f + float(threatNear(g, p, s)) + (g.systems[size_t(s)].home == p ? 0.5f : 0.f); }));
                return true;
            }
            return false;
        case Ab::DsOmniscience: {
            const int act = d.value;
            // (0.44.8) only when there will be rolls this action: other units in the active system, or ours bombarding /
            // firing space cannon there (it used to go on empty and home activations, and the card is exhausted)
            bool rolls = false;
            if (g.activeSystem >= 0) {
                const int s = g.activeSystem;
                for (int q = 0; q < kSides && !rolls; ++q)
                    if (q != p && q != act && hasUnits(g, q, s)) rolls = true;
                if (act != p && act >= 0) rolls = rolls || hasUnits(g, p, s);
                if (act == p)
                    for (int16_t x : g.systems[size_t(s)].planets) rolls = rolls || (g.planets[size_t(x)].owner >= 0 && g.planets[size_t(x)].owner != p);
            }
            if (!rolls) out = one(-1);
            else if (act == p) out = one(p * 2 + 1);
            else if (act >= 0 && g.activeSystem >= 0 && hasUnits(g, p, g.activeSystem)) out = one(act * 2);
            else out = one(-1);
            return true;
        }
        case Ab::DsRecycled: {
            const int sys = std::max(0, int(g.activeSystem));
            const Force& f = g.systems[size_t(sys)].space[size_t(p)];
            int pick = has(d, int(UnitType::Dreadnought)) && f.damaged(UnitType::Dreadnought) && me.tradeGoods < 3 ? int(UnitType::Dreadnought) : -1;
            // Autofabricator (their mech, 0.43.0 debug passes - never came up): a spare cruiser well away from any
            // fight comes back as a trade good and a mech
            if (pick < 0 && mechAbilities(g) && unitsAvailable(g, p, UnitType::Mech) > 0 && has(d, int(UnitType::Cruiser)) &&
                threatNear(g, p, sys) == 0 && f.nonFighterShips() >= 2 && g.round >= 2)
                pick = int(UnitType::Cruiser);
            // (balance pass 0.66.0 - knob rhrecycle, ON; used 7 times in 45 games before) the guide: "recycle the carriers you no
            // longer need" - from round 3 an empty carrier with no free planet within 2 systems comes back as 2 trade goods (and
            // the Autofabricator's mech); a damaged dreadnought whatever the trade goods. Tested (540 games in three samples):
            // 68 wins / 6.71 VP against 65 / 6.65 - small; with rhnoind see Industrious above. 0 = the old way
            if (pick < 0 && aiTune("rhrecycle", 1.f) > 0.f) {
                if (has(d, int(UnitType::Dreadnought)) && f.damaged(UnitType::Dreadnought)) pick = int(UnitType::Dreadnought);
                else if (has(d, int(UnitType::Carrier)) && g.round >= 3 && threatNear(g, p, sys) == 0 &&
                         f.count(UnitType::Infantry) + f.count(UnitType::Mech) == 0 && unitsOnBoard(g, p, UnitType::Carrier) >= 2) {
                    bool freePlanet = false;
                    for (int s2 : systemsWithin(g, sys, 2))
                        for (int16_t x : g.systems[size_t(s2)].planets)
                            if (g.planets[size_t(x)].owner < 0 && !g.planets[size_t(x)].destroyed) freePlanet = true;
                    if (!freePlanet) pick = int(UnitType::Carrier);
                }
            }
            out = one(pick);
            return true;
        }
        case Ab::DsAutofabricator: {  // on one of our planets (it holds it), else the space area
            int x = -2;
            for (const Option& o : d.options)
                if (o.enabled && o.id >= 0) x = o.id;
            out = one(x);
            return true;
        }
        case Ab::DsKyvir:
            // (the Roh'Dhna review, checkpoint 2 - test switch rhkyvir) the flagship for 4 resources (it costs 8 to build): from round 2 with
            // 4 to spend - used in 48-56% of their games. REVISIT (90 forced games each): used 0.61 a game instead of 0.43-0.48 - 7 / 6.62 against
            // 8 / 6.68, second sample 12 / 6.93 against 13 / 6.98 - no gain, off
            if (aiTune("rhkyvir", 0.f) > 0.f) {
                out = one(g.round >= 2 && spendable(g, p, false) >= 4 ? (has(d, int(UnitType::Destroyer)) ? int(UnitType::Destroyer) : has(d, int(UnitType::Cruiser)) ? int(UnitType::Cruiser) : -1) : -1);
                return true;
            }
            out = one(g.round >= 3 && spendable(g, p, false) >= 6 ? (has(d, int(UnitType::Destroyer)) ? int(UnitType::Destroyer) : has(d, int(UnitType::Cruiser)) ? int(UnitType::Cruiser) : -1) : -1);
            return true;
        case Ab::DsFillipo: {  // 0.43.0 (D51): sparingly - only when one more move brings a ship into a fight or a landing
            const int act = g.activeSystem;
            bool target = false;
            if (act >= 0) {
                target = otherShipsIn(g, p, act);
                for (int16_t x : g.systems[size_t(act)].planets) target = target || g.planets[size_t(x)].owner != p;
            }
            out = one(bestBy(d, [&](int c) {
                if (c < 0 || !target) return c < 0 ? 0.f : -1.f;
                const int s = c / 16;
                if (s == act || otherShipsIn(g, p, s)) return -1.f;  // (not a ship about to fight where it is)
                for (int t : kShipIdx)
                    if (t != int(UnitType::Fighter) && g.systems[size_t(s)].space[size_t(p)].n[t] &&
                        !shipReach(g, p, s, act, UnitType(t)).ok && shipReach(g, p, s, act, UnitType(t), 1).ok)
                        return 1.f + 0.1f * float(unitCost2(g, p, UnitType(t)));
                return -1.f;
            }, true));
            return true;
        }
        // ---- batch 3 (0.39.0)
        case Ab::DsSecretMaps:
        case Ab::DsMigrant:
        case Ab::DsAuctioneer:
        case Ab::DsGledgeBase: out = one(bestBy(d, [&](int x) { return planetWorth(g, x); })); return true;
        case Ab::DsIledrith: out = one(bestBy(d, [&](int x) { return float(threatNear(g, p, g.planets[size_t(x)].system)) * 2.f + planetWorth(g, x); })); return true;
        case Ab::DsPilgrimage:  // (0.44.7) where the ship helps most - enemies near; not without money for one
            if (spendable(g, p, false) < 2) {
                out = one(-1);
                return true;
            }
            out = one(bestBy(d, [&](int s) { return s < 0 ? -1.f : 1.f + float(threatNear(g, p, s)) + (g.systems[size_t(s)].home == p ? 0.5f : 0.f); }, 0.f));
            return true;
        case Ab::DsIndoctrination:  // (0.44.6: the threatened, valuable planet - not always home)
            out = one(bestBy(d, [&](int x) {
                const PlanetState& ps = g.planets[size_t(x)];
                return float(threatNear(g, p, ps.system)) * 2.f + planetWorth(g, x) * 0.5f - 0.6f * float(ps.units[size_t(p)].groundForces()) -
                       (g.systems[size_t(ps.system)].home == p ? 1.f : 0.f);
            }));
            return true;
        case Ab::DsEncryptionKey:
            if (d.prompt.find("which colour") != std::string::npos) {  // the colour our wanted technologies miss most
                out = one(bestBy(d, [&](int c) {
                    float v = 0;
                    for (int t = 0; t < kTechCount; ++t) {
                        if (me.hasTech(t) || !techAvailableToResearch(g, p, t)) continue;
                        int miss[4];
                        missingPrereqs(g, p, t, miss);
                        if (miss[c] > 0) v += aiTechWant(g, p, t);
                    }
                    return v;
                }));
                return true;
            }
            out = one(bestBy(d, [&](int x) { return x < 0 ? -1.f : (planetHasSkip(g, x) ? 0.f : 5.f) + planetWorth(g, x) * 0.1f; }));
            return true;
        case Ab::DsMgur: {
            const int prom = aiAgentPromisedTo(g, p);  // (rules 28) the promised player
            out = one(prom >= 0 && has(d, prom) ? prom : has(d, p) ? p : maxOption(d));
            return true;
        }
        case Ab::DsReplicators:
            if (d.prompt == "Exhaust Merged Replicators?") {  // 0.43.0: the biggest gain we can pay for
                out = one(d.value >= 2 ? bestBy(d, [&](int c) { return c < 0 ? 0.f : float(c >= 100 ? std::min(c - 100, d.value) : 2); }) : -1);
                return true;
            }
            out = yes(d.value >= 2);
            return true;
        case Ab::DsDurran: out = yes(d.value >= 2); return true;
        case Ab::DsMaximus: out = yes(spendable(g, p, false) >= 2); return true;
        case Ab::DsAegis: out = yes(d.value != int(UnitType::Fighter)); return true;
        case Ab::DsCombatDrills: out = yes(d.value >= 0 && d.value < kUnitTypes && unitCost2(g, p, UnitType(d.value)) >= 6); return true;
        case Ab::DsImpressment: out = yes(spendable(g, p, false) >= 3); return true;
        case Ab::DsImpressAction: out = one(bestBy(d, [&](int x) { return -planetWorth(g, x); })); return true;
        case Ab::DsJginFaru: out = yes(d.value > 0); return true;
        case Ab::DsYpru: {
            const int sys = g.activeSystem;
            const int mine = sys >= 0 ? g.systems[size_t(sys)].space[size_t(p)].nonFighterShips() : 0;
            bool use = d.value >= 3 || (d.value >= 2 && mine <= d.value + 1);
            // test switch dmnypru (the Dih-Mohn review, checkpoint 2: off; the owner made him "every round"): also to take
            // the enemy's Sustain Damage away - their undamaged sustaining ships outnumber ours by the knob (2: used 0.52 -> 0.56
            // a game, 12 / 6.50 vs 12 / 6.51, fresh 8 / 6.59 vs 9 / 6.58 - no effect)
            if (!use && sys >= 0 && aiTune("dmnypru", 0.f) > 0.f) {
                int ours = 0, theirs = 0;
                for (int q = 0; q < g.numPlayers; ++q) {
                    const Force& fo = g.systems[size_t(sys)].space[size_t(q)];
                    for (int t : kShipIdx)
                        if (fo.n[t] > fo.dmg[t] && unitSustains(g, q, UnitType(t))) (q == p ? ours : theirs) += fo.n[t] - fo.dmg[t];
                }
                use = float(theirs) >= float(ours) + aiTune("dmnypru", 0.f);
            }
            out = yes(use);
            return true;
        }
        case Ab::DsMercenaries: {  // only into our own fights
            if (d.prompt.find("how many") != std::string::npos) {  // (fixes 28: "up to 2") - all of them
                out = one(maxOption(d));
                return true;
            }
            if (d.value == 0) {
                // (test switch flzmerc, the Florzen review, checkpoint 2: off) someone else's fight against a leader this many
                // points ahead of us and of their opponent: fighters for the underdog (DsMercTarget gives them to the lower score)
                const float lead = aiTune("flzmerc", 0.f);
                bool sell = false;
                const int sys = g.activeSystem, a = g.activePlayer;
                if (lead > 0.f && sys >= 0 && a >= 0 && a < g.numPlayers) {
                    int b = -1;
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != a && g.systems[size_t(sys)].space[size_t(q)].ships()) b = q;
                    if (b >= 0 && b != p && a != p) {
                        const int va = g.players[size_t(a)].vp, vb = g.players[size_t(b)].vp;
                        const int top = std::max(va, vb), low = std::min(va, vb);
                        sell = float(top) >= float(std::max(int(me.vp), low)) + lead;
                    }
                }
                if (!sell) {
                    out = one(-1);
                    return true;
                }
            }
            out = one(bestBy(d, [&](int s) { return s < 0 ? -1.f : float(g.systems[size_t(s)].space[size_t(p)].count(UnitType::Fighter)) - 2.f * float(threatNear(g, p, s)); }));
            return true;
        }
        case Ab::DsMercTarget: out = one(has(d, p) ? p : bestBy(d, [&](int q) { return -float(g.players[size_t(q)].vp); })); return true;
        case Ab::DsDataLeak: out = one(bestBy(d, [&](int r) { return relicPref(r); })); return true;
        case Ab::DsBlackMarkets: {  // the deck whose fragments we collect
            out = one(bestBy(d, [&](int deck) {
                float n = 0;
                for (int16_t f : me.fragments)
                    if (isRelicFragment(f) && int(kExploreCards[f].type) == deck) n += 1;
                return n + (d.options.size() && d.options[0].id == deck ? 0.5f : 0.f);
            }));
            return true;
        }
        case Ab::DsBlackmail: {
            const int card = d.value / 16, actor = d.value % 16;
            const int av = actor >= 0 && actor < g.numPlayers ? g.players[size_t(actor)].vp : 0;
            const bool imperial = card == 7;
            out = yes((imperial && av >= me.vp && (me.strategy >= 2 || av >= g.vpGoal - 3)) || (av >= std::max(6, me.vp + 2) && me.strategy >= 2));
            return true;
        }
        case Ab::DsUnderground: out = yes(true); return true;
        case Ab::DsCordo: {  // (fixes 28: at the end of a turn)
            if (d.prompt.find("Exhaust Cordo Haved for") != std::string::npos) {  // (asked only when promised)
                out = yes(true);
                return true;
            }
            out = one(bestBy(d, [&](int c) {
                if (c < 0) return 1.0f;  // (the bar: keep it for a better turn)
                const int x = c / 10, e = c % 10;
                if (x < 0 || x >= int(g.planets.size())) return 0.f;
                const char* k = kPlanets[size_t(g.planets[size_t(x)].def)].key;
                auto is = [&](const char* s) { return !std::strcmp(k, s); };
                if (is("mallice")) return e == 1 ? 2.f : 0.9f * float(me.commodities);
                if (is("hopesend")) return e == 1 ? 2.f : 1.5f;
                if (is("silence")) return 2.5f;
                if (is("primor")) return 1.5f;
                if (is("mirage")) return 1.1f;
                if (is("thundersedge")) return me.tactic >= 1 && !me.passed ? 3.f : 0.f;
                if (is("emelpar")) {
                    for (const LeaderState& ls : me.leaders)
                        if (kLeaders[ls.leader].type == kAgent && ls.status == kExhausted) return 2.5f;
                    return 1.2f;
                }
                return 0.8f;  // (Bi-Phasic Generators, Mystic Library, Fractal Gate: rarely worth an agent)
            }));
            return true;
        }
        case Ab::DsSalGavda:
            if (d.prompt.find("Exhaust Sal Gavda for") != std::string::npos) {  // (fixes 28: asked only when promised)
                out = yes(true);
                return true;
            }
            if (d.onPlanets) {
                out = one(bestBy(d, [&](int x) { return -float(threatNear(g, p, g.planets[size_t(x)].system)); }));
                return true;
            }
            out = one(bestBy(d, [&](int c) { return c < 0 ? 0.f : (threatNear(g, p, g.planets[size_t(c / 16)].system) >= 3 ? 1.f : -1.f); }, true));
            return true;
        case Ab::DsQuaxdol:
            out = one(bestBy(d, [&](int x) { return x < 0 ? 0.f : (g.planets[size_t(x)].exhausted ? 3.f : 1.f) + planetWorth(g, x) * 0.2f; }, true));
            return true;
        case Ab::DsBanua:
            if (d.onPlanets) {
                out = one(maxOption(d));
                return true;
            }
            out = one(bestBy(d, [&](int c) { return c < 0 ? 0.5f : (g.planets[size_t(c / 16)].owner != p ? 2.f : 0.f); }, true));
            return true;
        case Ab::DsPrivateer: {  // influence for mechs while enough is left for tokens and votes
            const int inf = spendable(g, p, true, false);
            out = one(bestBy(d, [&](int k) { return k == 0 ? 0.5f : (inf - k >= 4 ? float(k) : -1.f); }));
            return true;
        }
        case Ab::DsMantle: {
            // (the Gledge review, checkpoint 2 - switch glfront, test) the Reference card's Dhonraz Installations bombards planets next
            // to a Core token's system: a Core on the border (other players' ground forces next door) is worth more. OFF: glfront=0.5 won
            // 34 of 270 (three seed sets) against 32 off, VP 6.59 against 6.61 - no effect
            const float front = aiTune("glfront", 0.f);
            out = one(bestBy(d, [&](int x) {
                if (x < 0) return -9.f;
                float v = 2.f - float(planetResources(g, x)) - 0.6f * float(planetInfluence(g, x));
                if (front > 0.f && g.planets[size_t(x)].system >= 0) {
                    int foes = 0;
                    const int cs = g.planets[size_t(x)].system;
                    for (int16_t a : g.systems[size_t(cs)].adj) {
                        if (a == cs) continue;
                        for (int16_t y : g.systems[size_t(a)].planets)
                            for (int q = 0; q < g.numPlayers; ++q) foes += q != p ? groundForcesOn(g, q, y) : 0;
                    }
                    v += front * float(std::min(foes, 4));
                }
                return v;
            }));
            return true;
        }
        case Ab::DsDeepMining: out = yes(d.value == 0); return true;
        case Ab::DsGorthrim:
            if (d.onPlanets) {  // a harmful attachment goes on the least valuable planet
                const bool bad = d.value >= 0 && d.value < kExploreCardCount && !std::strcmp(kExploreCards[d.value].key, "dmz");
                out = one(bestBy(d, [&](int x) { return bad ? -planetWorth(g, x) : planetWorth(g, x); }));
                return true;
            }
            out = one(maxOption(d));
            return true;
        case Ab::DsPsychographics: out = one(bestBy(d, [&](int o) { return float(kObjectives[o].points); })); return true;
        case Ab::DsUnity:
            if (d.prompt.find("stake this prediction") != std::string::npos) {  // (fixes 28) once a phase: when a public is ready now
                bool ready = false;
                for (int16_t o : g.publicObjectives) ready = ready || (!hasScored(g, p, o) && meetsObjective(g, p, o));
                // (TEST switch "edstake", the Edyn checkpoint 2 - off: 0) on the first agenda of the phase, stake only a
                // prediction we are this sure of; else wait for the second
                const float sure = aiTune("edstake", 0.f);
                if (ready && sure > 0.f && g.agendaNumber == 0 && g.currentAgenda >= 0) {
                    float conf = 0.f;
                    aiPredictOutcome(g, g.currentAgenda, &conf);
                    if (conf < sure) ready = false;
                }
                out = yes(ready);
                return true;
            }
            out = one(bestBy(d, [&](int o) { return o < 0 ? 0.f : float(kObjectives[o].points); }));  // (0.44.6: the most points)
            return true;
        case Ab::DsDatapool: out = yes(me.tradeGoods - 3 >= aiGoals(g, p).keepTg); return true;  // (0.44.5: spend objectives first)
        case Ab::DsClodho: out = yes(true); return true;
        case Ab::DsAtropha: {
            const float keep = objectiveFit(g, p, me.dsSheet);
            const int best = bestBy(d, [&](int o) { return objectiveFit(g, p, o); });
            out = one(best >= 0 && objectiveFit(g, p, best) > keep + 0.5f ? best : -1);
            return true;
        }
        case Ab::DsGlory:
            if (d.prompt.find("Move which") != std::string::npos) {  // a token where we have nothing left
                out = one(bestBy(d, [&](int s) { return hasUnits(g, p, s) ? 0.f : 1.f; }));
                return true;
            }
            {
                // (the Kjalengard review, checkpoint 2 - test knob kjglory, OFF) the research with only 1 strategy token left when a
                // unit upgrade it can research now is worth the knob's value or more (a full technology for 1 token)
                const float kg = aiTune("kjglory", 0.f);
                if (kg > 0.f && has(d, 2) && me.strategy >= 1) {
                    float best = 0.f;
                    for (int t = 0; t < kTechCount; ++t)
                        if (upgradeUnitType(t) >= 0 && !me.hasTech(t) && canResearchNow(g, p, t)) best = std::max(best, aiTechWant(g, p, t));
                    if (best >= kg) {
                        out = one(2);
                        return true;
                    }
                }
            }
            out = one(has(d, 2) && me.strategy >= 2 ? 2 : has(d, 1) ? 1 : -1);
            return true;
        case Ab::DsHeroism: out = yes(true); return true;
        case Ab::DsMerkismathr:
            // (fixes 28, the Kjalengard review) asked at another player's combat only when promised (aiDealAnswer keeps it); with no
            // Glory token to move, only for our own commodities
            if (g.fixes >= 28 && d.prompt.find("no Glory token") != std::string::npos) {
                out = yes(g.activePlayer == p);
                return true;
            }
            out = yes(true);
            return true;
        case Ab::DsZhrgar: out = yes(d.value >= 2 && (d.targetPlayer < 0 || d.targetPlayer == p)); return true;  // (0.44.6: not for others)
        case Ab::DsVassalage: out = yes(d.value >= 3); return true;
        case Ab::DsYgegnad:
            if (g.fixes >= 28) {  // (the Kjalengard review: system*16 + player) our own tokens, where our ships are; never another player's
                out = one(bestBy(d, [&](int c) { return c < 0 || c % 16 != p ? -1.f : 1.f + float(g.systems[size_t(c / 16)].space[size_t(p)].ships()); }));
                return true;
            }
            out = one(bestBy(d, [&](int s) { return s < 0 ? -1.f : 1.f + float(g.systems[size_t(s)].space[size_t(p)].ships()); }));
            return true;
        // ---- batch 4 (0.40.0)
        case Ab::DsCloaked: {  // keep a reserve of 2 cloaked ships for ambushes; home ships stay when threatened
            int held = 0;
            for (int16_t c : me.captured)
                if (c / 16 == p && !isGroundForce(UnitType(c % 16))) ++held;
            // (the Kollecc review, checkpoint 2 - knob kocloak, ON at 4 - owner 2026-11-16 11:29 "Let's keep the switches on"; it was 2)
            // the reserve of cloaked ships kept for Shroud of Lith (tested 20 / 6.93 vs 18 / 6.98)
            const int want = std::max(0, int(aiTune("kocloak", 4.f)) - held);
            // (fixes 28, the Kollecc review) bit 1 = produced at home, bit 2 = only fighters (they may be cloaked now: the bot keeps them)
            if (d.value & 2) {
                out = one(0);
                return true;
            }
            out = one((d.value & 1) && threatNear(g, p, me.homeSystem) > 0 ? 0 : std::min(want, maxOption(d)));
            return true;
        }
        case Ab::DsShroud: out = yes(true); return true;  // units on the sheet do nothing; bring them out whenever the fleet moves
        case Ab::DsTreasure:
        case Ab::DsAiSurvey: out = one(-1); return true;
        case Ab::DsNightingale:
        case Ab::DsSkirmishers:
        case Ab::DsSieve:
        case Ab::DsDuuban:
        case Ab::DsFinePrint:
        case Ab::DsKrovoz:
        case Ab::DsCollector:
        case Ab::DsSkhot:
        case Ab::DsPreserve: out = yes(true); return true;
        case Ab::DsSeeker:
        case Ab::DsCaptainDust: {  // the fragment kind we collect most of
            auto have = [&](int kind) {
                float n = 0;
                for (int16_t f : me.fragments)
                    if (isRelicFragment(f) && int(kExploreCards[f].type) == kind) n += 1;
                return n;
            };
            if (ab == Ab::DsSeeker) out = one(bestBy(d, [&](int c) { return have(int(kExploreCards[c % 1000].type)); }));
            else out = one(bestBy(d, [&](int deck) { return have(deck) + 0.5f; }));
            return true;
        }
        case Ab::DsGriphyn: out = one(bestBy(d, [&](int r) { return relicPref(r); })); return true;
        case Ab::DsNightshade: out = one(d.value == 0 ? maxOption(d) : 0); return true;
        case Ab::DsFacsimile:
            if (spaceOdds(g, p) > 0.85f) {
                out = one(-1);
                return true;
            }
            out = one(bestBy(d, [&](int t) { return t < 0 ? -1.f : float(unitCost2(g, p, UnitType(t))); }));
            return true;
        case Ab::DsIllusory: {  // help an outcome we voted for: the 2 planets with the most influence
            Answer a;
            if (d.value == 1) {
                std::vector<std::pair<int, int>> v;
                for (const Option& o : d.options) v.push_back({planetInfluence(g, o.id), o.id});
                std::sort(v.rbegin(), v.rend());
                for (size_t i = 0; i < v.size() && i < 2; ++i) a.picks.push_back(v[i].second);
            }
            out = a;
            return true;
        }
        case Ab::DsFractal:
            if (d.value == int(UnitType::Fighter) || d.value == int(UnitType::Infantry)) {
                out = one(-1);
                return true;
            }
            out = one(bestBy(d, [&](int s) { return s < 0 ? -1.f : float(g.systems[size_t(s)].space[size_t(p)].ships()); }));
            return true;
        case Ab::DsWalik: {
            // (fixes 28) for the player it was promised to: keep the promise; "which unit": the biggest ship
            const bool other = g.fixes >= 28 && d.targetPlayer >= 0 && d.targetPlayer != p;
            // (the Mortheus review, checkpoint 2 - test knob mowalik, OFF) their home is a nebula (fixes 28): a ship made there leaves it
            // at move 1 - Walik already for 1 new non-fighter ship, not 2
            const int need = aiTune("mowalik", 0.f) > 0.f && g.fixes >= 28 ? 1 : 2;
            if (d.prompt.find("which") != std::string::npos) {
                if (d.value < need && !(other && g.agentFor[size_t(p)] == d.targetPlayer)) {
                    out = one(-1);
                    return true;
                }
                out = one(bestBy(d, [&](int t) { return t < 0 || t == 99 ? -1.f : float(unitCost2(g, d.targetPlayer >= 0 ? d.targetPlayer : p, UnitType(t))); }));
                return true;
            }
            if (d.value < need && !(other && g.agentFor[size_t(p)] == d.targetPlayer)) {
                out = one(-1);
                return true;
            }
            out = one(bestBy(d, [&](int s) {
                if (s < 0) return 0.f;
                if (s >= int(g.systems.size())) return -1.f;  // (fixes 28: 99999 = "don't ask again this round", for a person)
                float v = 0;
                for (int16_t x : g.systems[size_t(s)].planets) v += g.planets[size_t(x)].owner < 0 ? planetWorth(g, x) : 0.f;
                return v - 2.f * float(threatNear(g, p, s));
            }, true));
            return true;
        }
        case Ab::DsBayan: out = one(bestBy(d, [&](int t) { return t < 0 ? -1.f : float(unitCost2(g, p, UnitType(t))); })); return true;
        case Ab::DsPutriv: {  // (the Vaden review, rules 29) as many tokens as they can pay for; the rest stay for Collateralized Loans / Collector
            int k = 0;
            for (const Option& o : d.options)
                if (o.enabled && o.id <= d.value) k = std::max(k, o.id);
            out = one(k);
            return true;
        }
        case Ab::DsCollateral: out = yes(d.value != int(UnitType::Fighter)); return true;
        case Ab::DsBindingDebts: out = yes(me.tradeGoods >= 2); return true;
        case Ab::DsMidas: {  // (0.44.7) the trade goods that make the move worth the most more than they cost
            const int act = g.activeSystem;
            if (act < 0) {
                out = one(0);
                return true;
            }
            const float base = aiMovePlanValue(g, p, act);
            int best = 0;
            float bv = 0.5f;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id <= 0) continue;
                Game h = g;
                h.fx.dsBoost = int8_t(o.id);
                h.fx.dsBoostOwner = int8_t(p);
                const float gain = aiMovePlanValue(h, p, act) - base - 0.9f * float(o.id);
                if (gain > bv) bv = gain, best = o.id;
            }
            out = one(best);
            return true;
        }
        case Ab::DsKrovozHit: out = yes(d.value >= 2); return true;
        case Ab::DsYudri:
            if (g.fixes >= 29 && d.value >= 1000) {  // (the Vaden review) another player's turn: only for the player it was promised to
                const int b = d.value - 1000;
                out = yes(b == p || (d.targetPlayer >= 0 && b == d.targetPlayer));
                return true;
            }
            out = one(bestBy(d, [&](int x) { return x < 0 ? -1.f : float(planetInfluence(g, x)); }));
            return true;
        case Ab::DsPutrivPay:
            if (g.fixes >= 29) {  // (the Vaden review) the cheapest: a Vaden note back, 2 commodities, 1 trade good, then the cheapest other note
                const int vd = playerOfFaction(g, "vaden");
                out = one(bestBy(d, [&](int c) {
                    if (c == 1) return 8.f;
                    if (c == 0) return 7.f;
                    const int n = c - 100;
                    if (noteOwner(n) == vd) return 9.f;
                    if (noteType(n) == kNoteSupportThrone || noteType(n) == kNoteAlliance) return -5.f;
                    return 6.f - noteDefaultValue(g, n);
                }, true));
                return true;
            }
            out = one(has(d, 1) ? 1 : has(d, 0) ? 0 : maxOption(d));
            return true;
        case Ab::DsAutonetic:
            if (d.prompt.find("which card") != std::string::npos) {
                out = one(bestBy(d, [&](int c) { return aiCardKeepValue(c); }));
                return true;
            }
            if (d.prompt.find("which planet") != std::string::npos) {
                out = one(bestBy(d, [&](int x) { return float(threatNear(g, p, g.planets[size_t(x)].system)); }));
                return true;
            }
            {
                float best = 0;
                for (int16_t c : g.actionDiscard) best = std::max(best, aiCardKeepValue(c));
                const bool full = int(me.actionCards.size()) >= actionCardLimit(g, p) - 1;
                // (balance pass 0.66.0 - knob cyauto, ON at 3) a card from the discard pile is followed by Cybernetic Madness's
                // discard: it swaps a card, it adds none (214 of 315 choices before, always Sabotage / Direct Hit / Shields
                // Holding). 1 = draw them all (the infantry when the hand is full); 2 = the infantry, the pile only for a junk card
                // in hand; 3 = the infantry in rounds 1-3, then as 1. Tested (Competitive 6p, TE + DS, Cymiae in seat 0, three
                // samples of 180 games): the old way 9 / 9 / 13 wins (5.7%, VP 6.15); 1: 16 / 17; 2: 22 / 13 / 18 (9.8%, 6.39);
                // 3: 28 / 16 / 16 (11.1%, 6.48) - 0 = the old way
                const int mode = int(aiTune("cyauto", 3.f));
                if (mode > 0) {
                    float worst = 99.f;
                    for (int16_t c : me.actionCards) worst = std::min(worst, aiCardKeepValue(c));
                    if (mode == 2 && has(d, 1) && best >= 3.f && worst <= 1.f) out = one(1);
                    else if (mode == 2 || (mode == 3 && g.round <= 3)) out = one(has(d, 2) ? 2 : 0);
                    else out = one(full && has(d, 2) ? 2 : 0);
                    return true;
                }
                out = one(has(d, 1) && best >= 3.f ? 1 : has(d, 2) && full ? 2 : 0);
            }
            return true;
        case Ab::DsRecursiveWorm: {  // a rival's token where their fleet next door could hit us
            int best = -1;
            float bv = 1.5f;
            for (const Option& o : d.options) {
                if (o.id < 0) continue;
                const int s = o.id / 16, q = o.id % 16;
                float v = 0;
                for (int16_t a : g.systems[size_t(s)].adj) v += float(g.systems[size_t(a)].space[size_t(q)].nonFighterShips());
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case Ab::DsKoryl: {
            if (d.prompt.find("comes from where") != std::string::npos) {  // (fixes 28, the Cymiae review) reinforcements first,
                // else the mech least needed where it stands: far from enemies, not the only mech on its planet
                if (has(d, -2)) {
                    out = one(-2);
                    return true;
                }
                out = one(bestBy(d, [&](int id) {
                    const int s = id >= 100000 ? id - 100000 : g.planets[size_t(id)].system;
                    float v = -float(threatNear(g, p, s));
                    if (id < 100000 && g.planets[size_t(id)].units[size_t(p)].count(UnitType::Mech) > 1) v += 1.f;
                    if (id < 100000 && g.systems[size_t(s)].home == p) v -= 0.5f;
                    return v;
                }));
                return true;
            }
            const int c = cheapestCard(d);
            out = one(c >= 0 && aiCardKeepValue(c) < 2.f ? c : -1);
            return true;
        }
        case Ab::DsVoiceUnited: out = one(bestBy(d, [&](int c) { return -aiCardKeepValue(c); })); return true;
        case Ab::DsAlgorithmic: out = one(bestBy(d, [&](int c) { return aiCardKeepValue(c); })); return true;
        case Ab::DsVoidsailors: out = one(bestBy(d, [&](int c) { return exploreWorth(c); })); return true;
        case Ab::DsVoidwake:
            if (g.fixes >= 29) {  // (the Nivyn review) value = the best unit's expected gain x10 (it is first): 1 hit, or half with the commander
                out = one(d.value >= 10 || (d.value >= 5 && commanderUsable(g, p, "nivyn")) ? d.options[0].id : -1);
                return true;
            }
            out = yes(d.value >= 2 || (d.value >= 1 && commanderUsable(g, p, "nivyn")));  // (0.44.5: the commander rewards damage)
            return true;
        case Ab::DsWarden:
            if (g.fixes >= 29) {  // (the Nivyn review) the mech to damage is picked: the first is the safest
                // (checkpoint 2 - test knob nvwound) another player's activation next to our units: the Wound there makes their ships
                // roll for a gravity rift when they leave it (we never do) and their attack out of it starts from a nebula.
                // REVISIT (the Nivyn review, checkpoint 2, 90 forced games each): the Wound moved 4.4 a game instead of 2.1 - 14 wins /
                // 7.36 VP against 14 / 7.47, second sample 19 / 7.44 against 19 / 7.46: no gain - off
                bool harass = false;
                if (d.value == 0 && aiTune("nvwound", 0.f) > 0.f && g.activeSystem >= 0 && g.activePlayer >= 0 && g.activePlayer != p &&
                    !d.options.empty() && d.options[0].id >= 0 && d.options[0].id < 99999)
                    for (int16_t a : g.systems[size_t(g.activeSystem)].adj) harass = harass || hasUnits(g, p, a);
                out = one(d.value == 2 || harass ? d.options[0].id : -1);
                return true;
            }
            out = yes(d.value == 2);
            return true;
        case Ab::DsGuidance: out = yes(false); return true;
        case Ab::DsSuldhan:
            if (g.fixes >= 29) {  // (the Nivyn review) before the ability is used: value = (who it helps + 1) * 8 + ability, options best first
                const int ben = d.value / 8 - 1, kind = d.value % 8;
                const bool mine = ben >= 0 && (ben == p || ben == d.targetPlayer);
                const int t = d.options.empty() ? -1 : d.options[0].id;
                // a roll or a shield: always worth the agent (it comes back each round); Sustain Damage: a dreadnought or bigger
                const bool big = t == int(UnitType::Dreadnought) || t == int(UnitType::WarSun) || t == int(UnitType::Flagship) || t == int(UnitType::Flagship2);
                const bool worth = t >= 0 && t < kUnitTypes && (kind != 3 || big);
                out = one(mine && worth ? t : -1);
                return true;
            }
            out = yes(d.value >= 1);
            return true;
        case Ab::DsPolicy: {  // Control (votes), Preserve (fragments), Empower (commodities) - by temperament
            // (0.43.0 debug passes: every bot set the same three, so Connect / Plunder / Exploit never came up)
            const Style& S = aiStyle(g, p);
            if (d.prompt.find("The People") != std::string::npos) out = one(S.on && S.pol < 1.f && S.aggression >= 1.1f ? 0 : 1);
            else if (d.prompt.find("The Environment") != std::string::npos) out = one(S.on && S.explore >= 1.5f ? 1 : 0);  // (Plunder costs 2 production)
            else out = one(S.on && S.aggression >= 1.2f ? 1 : 0);
            return true;
        }
        case Ab::DsConnect:  // an infantry to one of our planets that needs it (an enemy fleet next to it, a thin guard)
            out = one(bestBy(d, [&](int y) {
                if (y < 0) return 0.5f;
                const int sys = g.planets[size_t(y)].system;
                return float(threatNear(g, p, sys)) - float(g.planets[size_t(y)].units[size_t(p)].groundForces());
            }, true));
            return true;
        case Ab::DsExploit: out = one(bestBy(d, [&](int s) { return s < 0 ? -1.f : float(g.systems[size_t(s)].space[size_t(p)].ships()); })); return true;
        case Ab::DsPlunder:
            if (g.fixes >= 29) {  // (the Olradin review) which unit: an infantry when 2+ ground forces stand there
                out = one(d.value >= 2 && has(d, int(UnitType::Infantry)) ? int(UnitType::Infantry) : -1);
                return true;
            }
            out = yes(d.value >= 2);
            return true;
        case Ab::DsFalseFlag:
            if (d.prompt.find("ready which") != std::string::npos) {
                // (0.44.8) a real gain: our planet worth 2 or more than the cheapest ready one we'd exhaust for it (it
                // used to trade a 3/1 for a 2/0)
                float cheapest = 1e9f;
                for (int x : readyPlanets(g, p)) cheapest = std::min(cheapest, planetWorth(g, x));
                const int y = bestBy(d, [&](int x) { return x < 0 ? -1.f : g.planets[size_t(x)].owner == p ? planetWorth(g, x) - cheapest : -9.f; }, 1.9f);
                out = one(y >= 0 ? y : -1);
                return true;
            }
            out = one(bestBy(d, [&](int x) { return -planetWorth(g, x); }));
            return true;
        case Ab::DsGeosympathic: {  // (0.44.7) when +1 move for everyone is worth a planet exhausted - the cheapest one
            const int act = g.activeSystem;
            if (act < 0) {
                out = one(-1);
                return true;
            }
            Game h = g;
            h.fx.dsMove = 1;
            const float gain = aiMovePlanValue(h, p, act) - aiMovePlanValue(g, p, act);
            int cheap = -1;
            for (const Option& o : d.options)
                if (o.enabled && o.id >= 0 && (cheap < 0 || planetWorth(g, o.id) < planetWorth(g, cheap))) cheap = o.id;
            out = one(cheap >= 0 && gain > 0.4f * planetWorth(g, cheap) + 0.5f ? cheap : -1);
            return true;
        }
        case Ab::DsIncite:
            out = one(d.prompt.find("ready which") != std::string::npos ? bestBy(d, [&](int x) { return planetWorth(g, x); })
                                                                         : bestBy(d, [&](int x) { return -planetWorth(g, x); }));
            return true;
        case Ab::DsBaggil:
            if (g.fixes >= 29 && d.value != p && d.value != d.targetPlayer) {  // (the Olradin review) another player's planet: only when promised
                out = one(-1);
                return true;
            }
            out = one(bestBy(d, [&](int x) { return x < 0 || x >= 99999 ? -1.f : float(std::max(planetResources(g, x), planetInfluence(g, x))); }));
            return true;
        case Ab::DsKnak: out = one(bestBy(d, [&](int x) { return x < 0 ? -1.f : float(std::max(planetResources(g, x), planetInfluence(g, x))); })); return true;
        case Ab::DsPahn: out = one(-1); return true;
        // ---- batch 5 (0.41.0)
        case Ab::DsRally:  // the free system with the richest planets
            out = one(bestBy(d, [&](int s) {
                if (s < 0) return -1.f;
                float v = 0;
                for (int16_t x : g.systems[size_t(s)].planets) v += planetWorth(g, x);
                return v - 0.5f * float(threatNear(g, p, s));
            }));
            return true;
        case Ab::DsDiplomats:  // the planet that covers most of the bill without waste
            out = one(bestBy(d, [&](int x) {
                if (x < 0) return -1.f;
                const int v = d.prompt.find("influence") != std::string::npos ? planetInfluence(g, x) : planetResources(g, x);
                return v <= d.value ? float(v) : float(d.value) - 0.5f * float(v - d.value);
            }));
            return true;
        case Ab::DsDaedalon:  // 0.43.0: an empty planet first; a defended one only against a single defender
            out = one(bestBy(d, [&](int x) {
                if (x < 0) return 0.2f;
                int foes = neutralGroundOn(g, x);
                for (int q = 0; q < g.numPlayers; ++q) foes += q != p ? groundForcesOn(g, q, x) : 0;
                if (foes > 1) return -1.f;
                return planetWorth(g, x) * (foes ? 0.5f : 1.f) + (foes ? 0.f : 1.f);
            }, true));
            return true;
        // ---- Discordant Stars breakthroughs (0.43.0)
        case Ab::DsCelagrom: {  // (0.52.1) the Celagrom's hit: the costliest enemy ship that can't simply shrug it off
            const int opp = d.value;
            out = one(bestBy(d, [&](int t) {
                if (t < 0 || opp < 0) return 0.f;
                const UnitDef& u = unitDef(g, opp, UnitType(t));
                return float(std::max<int>(1, u.cost2)) * (u.sustain ? 0.35f : 1.f) + 0.1f;
            }));
            return true;
        }
        case Ab::DsbtCeldauri:  // the Celagrom joins the biggest fleet near a front
            out = one(bestBy(d, [&](int s) { return float(g.systems[size_t(s)].space[size_t(p)].nonFighterShips()) + float(threatNear(g, p, s)); }));
            return true;
        case Ab::DsbtAxis: out = one(-1); return true;
        case Ab::DsbtFlorzen:  // the richest other player (more notes to take and more to lose)
            out = one(g.round >= 2 ? bestBy(d, [&](int q) { return q < 0 ? 0.5f : float(g.players[size_t(q)].notes.size()) + 0.2f * float(g.players[size_t(q)].tradeGoods); }, true) : -1);
            return true;
        case Ab::DsbtCymiae:
            if (d.prompt.find("purge a promissory") != std::string::npos) {  // a leader's token out, else the trade goods - if a spare note
                int spare = 0;  // (only notes we can do without: Ceasefire, Political Secret, Trade Agreement - and not in round 1-2)
                for (int16_t n : me.notes)
                    if (g.fixes < 8 || noteOwner(n) == p)  // (0.44.4: only our own notes can be purged)
                        spare += noteType(n) == kNoteCeasefire || noteType(n) == kNotePoliticalSecret || noteType(n) == kNoteTradeAgreement ? 1 : 0;
                if (spare == 0 || g.round <= 2) {
                    out = one(-1);
                    return true;
                }
                out = one(bestBy(d, [&](int c) {
                    if (c < 0) return 0.f;
                    if (c == 100) return 1.5f;
                    return 1.f + float(g.players[size_t(c)].vp) * 0.2f;
                }));
            } else if (d.prompt.find("Purge which") != std::string::npos) {  // a generic note we don't need
                out = one(bestBy(d, [&](int n) {
                    const int t = noteType(n);
                    return t == kNotePoliticalSecret ? 1.f : t == kNoteTradeAgreement ? 0.8f : t == kNoteCeasefire ? 0.6f : -2.f;
                }));
            } else {  // 0.44.4: into a system with other players' ships - the token locks them there
                out = one(bestBy(d, [&](int s) {
                    int n = 0;
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p) n += g.systems[size_t(s)].space[size_t(q)].nonFighterShips();
                    return float(n) - 0.1f * float(threatNear(g, p, s));
                }));
            }
            return true;
        case Ab::DsbtGhoti: {  // 0.44.4: on passing, pull a lone ship under threat into a neighbouring stack of ours
            auto score = [&](int s, int a, int t) {
                if (t == int(UnitType::Fighter) || t == int(UnitType::Flagship) || t == int(UnitType::WarSun)) return 0.f;
                const Force& here = g.systems[size_t(s)].space[size_t(p)];
                if (here.nonFighterShips() != 1 || here.groundForces() + here.count(UnitType::Fighter) > 0) return 0.f;
                if (g.systems[size_t(s)].home >= 0) return 0.f;
                const int threat = threatNear(g, p, s);
                return threat >= 2 ? float(threat) + 0.5f * float(g.systems[size_t(a)].space[size_t(p)].nonFighterShips()) : 0.f;
            };
            if (d.prompt.find("Move which ship") != std::string::npos) {
                out = one(bestBy(d, [&](int m) { return score(m / 4096, (m / 16) % 256, m % 16); }));
                return true;
            }
            float best = 0.f;
            if (has(d, 1))
                for (size_t s = 0; s < g.systems.size(); ++s)
                    for (int t : kShipIdx)
                        if (g.systems[s].space[size_t(p)].n[t])
                            for (int16_t a : g.systems[s].adj)
                                if (hasShips(g, p, a) && !otherShipsIn(g, p, a)) best = std::max(best, score(int(s), a, t));
            out = one(best >= 2.f ? 1 : -1);
            return true;
        }
        // 0.44.4: an action card instead of a weak exploration card only (Decision::value = the card)
        case Ab::DsbtAugers:
            out = yes(int(me.actionCards.size()) < actionCardLimit(g, p) - 1 && (d.value <= 0 || aiExploreCardValue(d.value) <= 1.05f));
            return true;
        // 0.44.4: a free technology beats a command token unless tokens are short
        case Ab::DsbtKolume: out = one(has(d, 1) && me.tactic + me.fleet + me.strategy >= 6 ? 1 : has(d, 0) ? 0 : has(d, 1) ? 1 : -1); return true;
        case Ab::DsbtLanefir:
            if (isYesNoOptions(d)) out = yes(true);
            else out = one(bestBy(d, [&](int x) { return x < 0 ? 0.f : 1.f + planetWorth(g, x) * 0.1f; }));
            return true;
        case Ab::DsbtNokar:
            if (d.prompt.find("goes where") != std::string::npos)  // 0.44.4: in the leader's way - never in ours
                out = one(bestBy(d, [&](int s) { return s < 0 ? 0.f : nokarSpot(g, p, s); }, true));
            else out = one(me.commodities + 2 <= commodityCap(g, me) ? 0 : 1);
            return true;
        case Ab::DsbtZealots: out = one(has(d, 0) ? 0 : has(d, 1) ? 1 : -1); return true;  // (0.44.4: the free technology first)
        case Ab::DsbtGledge:  // (the Gledge review) Dhonraz Installations: the planet with the most of other players' ground forces (the leaders' first)
            out = one(bestBy(d, [&](int x) {
                if (x < 0) return 0.f;
                float n = 0.f;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p) n += float(groundForcesOn(g, q, x)) * (1.f + 0.05f * float(g.players[size_t(q)].vp));
                return n + planetWorth(g, x) * 0.1f;
            }));
            return true;
        case Ab::DsbtKyro: out = one(bestBy(d, [&](int c) { return -aiCardKeepValue(c); })); return true;
        case Ab::DsbtBentor:
            // 0.44.4: the frontier deck while Ancient Blueprints still wants the unknown fragment (only that deck has them)
            if (isYesNoOptions(d))
                out = yes(isFaction(g, p, "bentor") && !(me.dsFrags & (1 << int(ExploreType::Frontier))) &&
                          !g.exploreDeck[size_t(ExploreType::Frontier)].empty());
            else out = one(bestBy(d, [&](int x) { return x < 0 ? 0.f : 1.f + planetWorth(g, x) * 0.1f; }));
            return true;
        case Ab::DsbtKollecc:  // a relic fragment or an attachment from the museum, else draw
            out = one(bestBy(d, [&](int c) {
                if (c < 0) return 0.5f;
                return isRelicFragment(c) ? 2.f : kExploreCards[c].kind == ExploreKind::Attach ? 1.5f : 0.2f;
            }));
            return true;
        case Ab::DsbtFreesys:
        case Ab::DsbtLizho:
            if (d.prompt.find("comes from") != std::string::npos) {  // 0.44.4: spare infantry far from danger, never the home's last ones
                out = one(bestBy(d, [&](int y) {
                    if (y >= 100000) return 2.f;  // (riding in space: spare)
                    const PlanetState& ps = g.planets[size_t(y)];
                    const int inf = ps.units[size_t(p)].count(UnitType::Infantry);
                    float v = float(inf) - float(threatNear(g, p, ps.system)) * 0.5f - planetWorth(g, y) * 0.1f;
                    if (isHomePlanet(g, y) && inf <= 2) v -= 5.f;
                    return v;
                }));
                return true;
            }
            out = one(bestBy(d, [&](int x) { return x < 0 ? 0.f : 0.5f + planetWorth(g, x) * 0.1f; }));
            return true;
        case Ab::DsbtTnelis:  // losing the fight: a secret for safety (the one furthest from done)
            out = one(d.value < 0 ? bestBy(d, [&](int o) { return o < 0 ? -50.f : float(objectiveProgress(g, p, o).missing()); }) : -1);
            return true;
        case Ab::DsbtOlradin:
            if (isYesNoOptions(d)) {  // 0.44.4: rise up only where our infantry outnumber the defenders (the card goes for good)
                int good = 0, bad = 0;
                for (size_t x = 0; x < g.planets.size(); ++x) {
                    if (!((g.planets[x].coexist >> p) & 1)) continue;
                    const int mine = g.planets[x].units[size_t(p)].count(UnitType::Infantry);
                    if (!mine) continue;
                    int theirs = 0;
                    for (int q = 0; q < kSides; ++q)
                        if (q != p) theirs += g.planets[x].units[size_t(q)].groundForces();
                    (mine > theirs ? good : bad) += 1;
                }
                // (the Olradin review, checkpoint 2 - test switch olrise) from round 3: rose in 0.03-0.11 of their games. REVISIT (90 forced
                // games each): 19 / 7.31 and 20 / 7.49 against 19 / 7.32 and 20 / 7.48 - no effect, off
                if (aiTune("olrise", 0.f) > 0.f) out = yes(good >= 1 && good >= bad && g.round >= 3);
                else out = yes(good >= 1 && good >= bad && (good >= 2 || g.round >= 5));
            }
            else out = one(me.tradeGoods >= 3 ? bestBy(d, [&](int c) { return c < 0 ? 0.f : 0.5f + planetWorth(g, c % 1000) * 0.1f; }) : -1);
            return true;
        case Ab::DsbtKhrask:
            // (fixes 28, the Khrask review) Mended Grove's coexistence: when the infantry would lose the fight - not on a planet they can
            // take (Meteor Slings now reach undefended planets); the Grove's payments: yes
            if (g.fixes >= 28 && d.prompt.rfind("Mended Grove:", 0) == 0 && d.value >= 0 && d.value < int(g.planets.size())) {
                int them = 0, mine = int(g.planets[size_t(d.value)].units[size_t(p)].groundForces());
                for (int q = 0; q < kSides; ++q)
                    if (q != p) them += g.planets[size_t(d.value)].units[size_t(q)].groundForces();
                out = yes(them > 0 && them >= mine);
                return true;
            }
            out = yes(true);
            return true;  // (coexisting keeps the infantry alive)
        case Ab::DsbtDihmohn:  // (ships in the middle of the fight if we can pay for 2; else the frontier token)
            out = one(has(d, 1) && spendable(g, p, false) >= 3 && aiFreeSpend(g, p, false) >= 2 ? 1 : has(d, 0) ? 0 : -1);
            return true;
        case Ab::DsbtGhemina: out = yes(true); return true;  // (a retreat announced: leave before their dice)
        case Ab::DsbtVaden: out = yes(false); return true;
        case Ab::DsbtCheiran:  // (a damaged dreadnought / war sun comes back as fresh units; never the flagship)
            out = one(bestBy(d, [&](int c) {
                if (c < 0) return 0.5f;
                const int s = c / 16, t = c % 16;
                if (t == int(UnitType::Flagship) || !g.systems[size_t(s)].space[size_t(p)].dmg[t]) return 0.f;
                return float(unitCost2(g, p, UnitType(t)));
            }, true));
            return true;
        case Ab::DsbtVaylerian: {  // 0.44.4: no fight here - send a spare warship to guard a threatened planet of ours next door
            if (d.prompt.find("discard which") != std::string::npos) {
                out = one(bestBy(d, [&](int c) { return -aiCardKeepValue(c); }));
                return true;
            }
            const int act = g.activeSystem;
            bool fight = false;
            for (int q = 0; q < kSides && act >= 0; ++q) fight = fight || (q != p && g.systems[size_t(act)].space[size_t(q)].ships() > 0);
            float cheapest = 99.f;
            for (int16_t c : me.actionCards) cheapest = std::min(cheapest, aiCardKeepValue(c));
            int best = -1;
            float bv = 0.f;
            if (!fight && act >= 0 && g.systems[size_t(act)].space[size_t(p)].nonFighterShips() >= 2 && cheapest <= 1.2f)
                for (const Option& o : d.options) {
                    if (!o.enabled || o.id < 0) continue;
                    const int a = o.id / 16, t = o.id % 16;
                    if (t != int(UnitType::Destroyer) && t != int(UnitType::Cruiser) && t != int(UnitType::Dreadnought)) continue;
                    bool ours = false;
                    for (int16_t x : g.systems[size_t(a)].planets) ours = ours || g.planets[size_t(x)].owner == p;
                    if (!ours || g.systems[size_t(a)].space[size_t(p)].nonFighterShips() > 0) continue;
                    const float v = float(threatNear(g, p, a)) - (t == int(UnitType::Dreadnought) ? 1.f : 0.f);
                    if (v >= 2.f && v > bv) bv = v, best = o.id;
                }
            out = one(best);
            return true;
        }
        case Ab::DsbtMortheus: {
            // 0.44.4: a stronger ship in for the weakest one here (one without cargo to lose), never one guarding home
            const int act = g.activeSystem;
            auto worth = [&](int t) { return float(unitCost2(g, p, UnitType(t))) + float(unitDef(g, p, UnitType(t)).combatDice); };
            auto weakestHere = [&]() {
                float w = 99.f;
                if (act < 0) return w;
                const Force& f = g.systems[size_t(act)].space[size_t(p)];
                for (int t : kShipIdx)
                    if (f.n[t] && unitDef(g, p, UnitType(t)).capacity == 0) w = std::min(w, worth(t));
                return w;
            };
            if (d.prompt.find("swap in") != std::string::npos) {
                const float floor = weakestHere();
                out = one(bestBy(d, [&](int c) {
                    if (c < 0) return 0.f;
                    const int s = c / 16, t = c % 16;
                    if (s == g.players[size_t(p)].homeSystem || unitDef(g, p, UnitType(t)).capacity > 0) return -1.f;
                    return worth(t) - floor - 2.f;
                }, true));
            } else {
                out = one(bestBy(d, [&](int t) { return -worth(t) - (unitDef(g, p, UnitType(t)).capacity > 0 ? 10.f : 0.f); }));
            }
            return true;
        }
        case Ab::DsbtEdyn:
            if (d.prompt.find("In return") != std::string::npos) {  // 0.44.4 (the other player): a secret rather than a guest on our planet
                out = one(has(d, 0) ? 0 : 1);
                return true;
            }
            if (d.prompt.find("to which player") != std::string::npos)
                out = one(bestBy(d, [&](int q) { return q < 0 ? -1.f : float(g.players[size_t(q)].vp); }));
            else if (d.prompt.find("which of your secrets") != std::string::npos)
                out = one(bestBy(d, [&](int o) { return float(objectiveProgress(g, p, o).missing()); }));
            else out = one(bestBy(d, [&](int x) { return planetWorth(g, x); }));
            return true;
        case Ab::DsPlague:  // a free infantry (fixes 28: offered without Dacxive Animators too - then nothing to gain)
            if (!ownsTechKey(g, p, "dxa") || unitsAvailable(g, p, UnitType::Infantry) <= 0) {
                out = one(-1);
                return true;
            }
            out = one(bestBy(d, [&](int x) { return x < 0 ? 0.f : 1.f + planetWorth(g, x) * 0.1f; }));
            return true;
        case Ab::DsEnvoy:  // 0.43.0: the biggest vote another player has on that trait (the points leader's first)
            out = one(bestBy(d, [&](int x) {
                if (x < 0) return 0.5f;
                const int q = g.planets[size_t(x)].owner;
                if (q == p) return -5.f;
                int lead = 0;
                for (int r = 0; r < g.numPlayers; ++r) lead = std::max(lead, int(g.players[size_t(r)].vp));
                return float(planetInfluence(g, x)) + (g.players[size_t(q)].vp == lead ? 0.6f : 0.f);
            }));
            return true;
        case Ab::DsOtto:
        case Ab::DsWarSong:
        case Ab::DsKantrus: out = one(bestBy(d, [&](int x) { return planetWorth(g, x); })); return true;
        case Ab::DsKorela:
            out = one(bestBy(d, [&](int x) {
                float n = 0;
                for (int q = 0; q < kSides; ++q)
                    if (q != p) n += float(g.planets[size_t(x)].units[size_t(q)].total());
                return n - 3.f * float(g.planets[size_t(x)].units[size_t(p)].total());
            }));
            return true;
        case Ab::DsGrace: {  // the secondary worth most now
            static const float kPref[8] = {0.2f, 0.6f, 0.5f, 1.2f, 0.9f, 1.f, 1.5f, 0.7f};
            // ("edgrace", the Edyn checkpoint 2 - ON, owner 2026-11-15 14:33; 0 = off) with 2 or fewer strategy tokens, a
            // token only for a secondary worth at least this much (Construction, Warfare, Technology; Leadership's needs none)
            const float bar = aiTune("edgrace", 1.f);
            out = one(bestBy(d, [&](int c) {
                if (c < 0) return -1.f;
                if (bar > 0.f && c != 0 && me.strategy <= 2 && kPref[c & 7] < bar) return -2.f;
                return kPref[c & 7];
            }));
            return true;
        }
        case Ab::DsRadiance: {
            float conf = 0;
            const int o = aiPredictOutcome(g, d.agenda, &conf);
            out = one(has(d, o) ? o : maxOption(d));
            return true;
        }
        case Ab::DsEdynRider: {  // the same judgement as the Keleres Rider
            Decision d2 = d;
            d2.ability = int(Ab::KeleresRider);
            return aiFactionAnswer(g, d2, rng, out);
        }
        case Ab::DsAllant: {  // (an extra action of our own; rules 28: the promised player's)
            const int prom = aiAgentPromisedTo(g, p);
            out = one(prom >= 0 && has(d, prom) ? prom : has(d, p) && me.tactic >= 1 ? p : -1);
            return true;
        }
        case Ab::DsAllantPeek: out = yes(false); return true;
        case Ab::DsKadryn: out = yes(d.value == 0); return true;
        case Ab::DsMidir:
            if (d.prompt.find("which agenda") != std::string::npos) {  // (fixes 28: the order - as drawn)
                out = one(d.options.empty() ? -1 : d.options[0].id);
                return true;
            }
            out = one(bestBy(d, [&](int o) { return aiOutcomeValue(g, p, d.agenda, o); }, true));
            return true;
        case Ab::DsSigilToken: {  // lock the leader out
            out = one(bestBy(d, [&](int c) { return c < 0 ? -1.f : float(g.players[size_t(c % 16)].vp) + 0.1f * float(threatNear(g, c % 16, c / 16)); }));
            return true;
        }
        case Ab::DsHired: {  // sell the small ships where they fight someone else - and leave none of ours to be fought
            if (d.value != 3) {
                out = one(-1);
                return true;
            }
            out = one(bestBy(d, [&](int t) { return t < 0 ? -100.f : -float(unitCost2(g, p, UnitType(t))); }));
            return true;
        }
        case Ab::DsHiredBuy: out = yes((d.value & 1) || me.tradeGoods >= 4); return true;
        case Ab::DsHireOffer: {  // 0.43.0: sell ships to a player about to fight someone else - not to a near winner
            const int b = g.activePlayer;
            const bool nearWin = b >= 0 && g.players[size_t(b)].vp >= g.vpGoal - 3;
            const bool rich = b >= 0 && g.players[size_t(b)].tradeGoods + g.players[size_t(b)].commodities >= 2;
            out = yes((d.value & 1) && !nearWin && rich && !(b >= 0 && hostileFlag(g, p, b)));
            return true;
        }
        case Ab::DsHirePick:  // the cheapest ships, the ones already there first
            out = one(bestBy(d, [&](int c) {
                if (c < 0) return -50.f;
                return -float(unitCost2(g, p, UnitType(c % 16))) + (c / 16 == g.activeSystem ? 0.5f : 0.f);
            }));
            return true;
        case Ab::DsNavigator: out = yes(spaceOdds(g, p) < 0.45f); return true;
        case Ab::DsDavish:
            if (g.fixes >= 29) {  // (the Tnelis review) another player's invasion: only for the player it was promised to
                const bool mine = d.value == p || (d.targetPlayer >= 0 && d.value == d.targetPlayer);
                out = one(mine && !d.options.empty() ? d.options[0].id : -1);  // (the first ship offered)
                return true;
            }
            return false;
        case Ab::DsRondBriay:
            if (g.fixes >= 29) {  // (the Roh'Dhna review) another player's production: only for the player it was promised to
                out = yes(d.value == p || (d.targetPlayer >= 0 && d.value == d.targetPlayer));
                return true;
            }
            return false;
        case Ab::DsSalSparrow:
            if (g.fixes >= 29) {  // (the Nokar review) the promised player, else ourselves with room in the fleet pool
                const int act = g.activeSystem;
                const bool room = act >= 0 && fleetLimit(g, p) - fleetUseIn(g, p, act) > 0;
                out = one(d.targetPlayer >= 0 && has(d, d.targetPlayer) ? d.targetPlayer : has(d, p) && room ? p : -1);
                return true;
            }
            out = yes(p == g.activePlayer && d.value > 0);  // (room in the fleet pool)
            return true;
        case Ab::DsSkarvald:  // (the Ghemina review) their own hits; another player's only when promised - then keep the promise
            out = yes(p == g.activePlayer || (g.activePlayer >= 0 && g.agentFor[p] == g.activePlayer));
            return true;
        case Ab::DsOrderBuy: {  // the dearest order first: they sell for more
            int best = -1;
            for (const Option& o : d.options)
                if (o.enabled && o.id >= 0 && (best < 0 || o.id < best)) best = o.id;
            out = one(best);
            return true;
        }
        case Ab::DsOrderUse: {  // where the new ship is needed most - and fits the fleet pool (0.42.2), else keep the order
            const int n = d.prompt.find("2 destroyers") != std::string::npos ? 2 : 1;
            out = one(bestBy(d, [&](int s) {
                if (s < 0) return 0.5f;
                if (fleetLimit(g, p) - fleetUseIn(g, p, s) < n) return -1.f;
                return 1.f + float(threatNear(g, p, s));
            }, true));
            return true;
        }
        case Ab::DsRiftEngines: {  // 0.43.0 (D113): a ship that reaches a fight only with the +2 (not a big one - 30% lost)
            const int act = g.activeSystem;
            bool target = false;
            if (act >= 0) {
                target = otherShipsIn(g, p, act);
                for (int16_t x : g.systems[size_t(act)].planets) target = target || (g.planets[size_t(x)].owner != p);
            }
            out = one(bestBy(d, [&](int c) {
                if (c < 0 || !target) return c < 0 ? 0.f : -1.f;
                const int s = c / 16;
                const UnitType t = UnitType(c % 16);
                if (t == UnitType::Flagship || t == UnitType::WarSun) return -1.f;
                if (shipReach(g, p, s, act, t).ok || !shipReach(g, p, s, act, t, 2).ok) return -1.f;
                // (0.44.7) only when the plan really uses it - the gain against a 30% chance of losing the ship
                Game h = g;
                h.fx.dsRiftType = int8_t(t);
                h.fx.dsRiftOwner = int8_t(p);
                h.fx.dsRiftSys = int16_t(s);
                const float gain = aiMovePlanValue(h, p, act) - aiMovePlanValue(g, p, act) - 0.3f * float(unitCost2(g, p, t)) / 2.f;
                return gain > 0.3f ? gain : -1.f;
            }, true));
            return true;
        }
        case Ab::DsTckVsk: out = yes(spendable(g, p, false) >= 8); return true;
        case Ab::DsOmen: {  // a sure hit on a die that would usually miss; the smallest Omen that does it
            // (fixes 28: one Omen die per die - a second one in the same roll only for a die that needs 8 or more; a gravity rift
            // roll (value 4): an Omen of 4+ keeps a dreadnought, a flagship or a war sun)
            const bool again = d.prompt.find("another Omen") != std::string::npos;
            const bool rift = d.value == 4;
            int best = -1;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0 || o.id >= 99999) continue;
                const bool big = o.label.find("Dreadnought") != std::string::npos || o.label.find("Flagship") != std::string::npos ||
                                 o.label.find("War Sun") != std::string::npos;
                const bool worth = rift ? big : o.id % 100 >= (again ? 8 : 6);
                if (worth && (best < 0 || o.id / 100 < best / 100)) best = o.id;
            }
            out = one(fquirkIs(g, p, "mykomentori", 1) && best >= 0 && best % 100 < 8 && !rift ? -1 : best);
            return true;
        }
        case Ab::DsLactarius:
            if (d.prompt.find("Exhaust Lactarius Indigo before") != std::string::npos) {  // (fixes 28) before the roll, no dice in view
                const int q = d.value, b = d.targetPlayer, act = g.activeSystem;
                const bool rift = d.prompt.find("gravity rift") != std::string::npos;
                auto fights = [&](int x) { return act >= 0 && x != q && hasUnits(g, x, act) && hasUnits(g, q, act); };
                int lead = 0;
                for (int r = 0; r < g.numPlayers; ++r) lead = std::max(lead, int(g.players[size_t(r)].vp));
                const bool leader = q != p && q < g.numPlayers && g.players[size_t(q)].vp == lead && lead >= g.vpGoal - 3;
                // a roll to spoil: someone fighting us (or the one we promised it to), the leader; a roll to make: ours, or the promised one's
                const bool spoil = q != p && q != b && (fights(p) || (b >= 0 && fights(b)) || leader);
                const bool help = q == p || q == b;
                out = one(bestBy(d, [&](int c) {
                    if (c < 0 || c >= 99999) return c < 0 ? 0.f : -1.f;
                    for (const Option& o : d.options) {
                        if (o.id != c) continue;
                        const size_t k = o.detail.find("needs ");
                        const int need = k == std::string::npos ? 10 : std::atoi(o.detail.c_str() + k + 6);
                        const bool sure = o.detail.find("A sure hit") != std::string::npos || o.detail.find("A sure pass") != std::string::npos;
                        const bool big = o.label.find("Dreadnought") != std::string::npos || o.label.find("Flagship") != std::string::npos ||
                                         o.label.find("War Sun") != std::string::npos;
                        if (rift) return (spoil && !sure && big) || (help && sure && big) ? 1.f : -1.f;  // a ship lost / kept
                        // (the agent comes back each round: worth it on a die likely to hit for them, or to miss for us)
                        if (spoil && !sure) return need <= 6 ? float(11 - need) / 10.f : -1.f;
                        if (help && sure) return need >= 7 ? float(need - 1) / 10.f : -1.f;
                        return -1.f;
                    }
                    return -1.f;
                }, true));
            } else if (d.prompt.find("change one of") != std::string::npos) {  // 0.43.0: any roll, dice in view
                const int q = d.value, act = g.activeSystem;
                const bool facing = act >= 0 && q != p && hasUnits(g, p, act) && hasUnits(g, q, act);  // they fight us
                int lead = 0;
                for (int r = 0; r < g.numPlayers; ++r) lead = std::max(lead, int(g.players[size_t(r)].vp));
                const bool leader = q != p && g.players[size_t(q)].vp == lead && lead >= g.vpGoal - 3;
                out = one(bestBy(d, [&](int c) {
                    if (c < 0) return 0.f;
                    bool spoils = false;  // "A hit becomes a miss"
                    for (const Option& o : d.options)
                        if (o.id == c) spoils = o.detail.find("hit becomes a miss") != std::string::npos;
                    // an enemy's hit spoiled when they fight us (or lead the race), our own miss saved
                    if (q == p) return spoils ? -1.f : 1.f;
                    return spoils && (facing || leader) ? 1.f : -1.f;
                }, true));
            } else {
                out = yes(d.value <= 5);
            }
            return true;
        case Ab::DsInsightReroll:
            if (d.prompt.find("one of your dice") != std::string::npos) {  // 0.43.0: the die likeliest to hit
                out = one(bestBy(d, [&](int i) {
                    if (i < 0) return 0.f;
                    for (const Option& o : d.options)
                        if (o.id == i) {
                            const size_t k = o.detail.find("needs ");
                            const int need = k == std::string::npos ? 10 : std::atoi(o.detail.c_str() + k + 6);
                            return need <= 8 ? float(11 - need) : -1.f;
                        }
                    return -1.f;
                }, true));
            } else {
                out = yes(true);
            }
            return true;
        case Ab::DsPsychoactive: out = yes(d.value >= 2); return true;  // (0.44.6: once a round - 2 misses are worth it)
        case Ab::DsWhichFlagship:
            if (d.prompt.find("build") != std::string::npos) out = one(1);
            else if (d.prompt.find("moved") != std::string::npos) out = one(d.value > 0 ? 1 : 0);
            else out = one(0);
            return true;
        case Ab::DsRaidLeaders:
            out = one(has(d, int(UnitType::Dreadnought)) ? int(UnitType::Dreadnought) : has(d, int(UnitType::Cruiser)) ? int(UnitType::Cruiser)
                      : has(d, int(UnitType::Destroyer)) ? int(UnitType::Destroyer) : -1);
            return true;
        case Ab::DsShipmonger: {
            // 0.42.2 (#187: 23 ships in 60 games went straight back over the fleet pool): the ship goes where the
            // fleet pool has room - a front first; a player gets it only if they have room somewhere
            auto room = [&](int q, int s) { return fleetLimit(g, q) - fleetUseIn(g, q, s); };
            auto anyRoom = [&](int q) {
                for (size_t s = 0; s < g.systems.size(); ++s)
                    if (hasShips(g, q, int(s)) && room(q, int(s)) > 0) return true;
                return false;
            };
            if (d.prompt.find("which player") != std::string::npos) {  // ourselves if it fits, else the commodities
                if (const int prom = aiAgentPromisedTo(g, p); prom >= 0 && has(d, prom)) {  // (rules 28) the promised player
                    out = one(prom);
                    return true;
                }
                if (has(d, p) && anyRoom(p)) {
                    out = one(p);
                    return true;
                }
                out = one(bestBy(d, [&](int q) {
                    if (q == p) return -5.f;
                    return (anyRoom(q) ? 2.f : 0.f) - float(g.players[size_t(q)].vp) * 0.3f;
                }));
                return true;
            }
            if (d.prompt.find("which ship") != std::string::npos) {
                // (fixes 28: the chosen player may decline) no room anywhere in the fleet pool - the ship would go straight back
                if (has(d, -1) && !anyRoom(p)) {
                    out = one(-1);
                    return true;
                }
                out = one(has(d, int(UnitType::Cruiser)) ? int(UnitType::Cruiser) : int(UnitType::Destroyer));
                return true;
            }
            if (d.prompt.find("where") != std::string::npos) {
                out = one(bestBy(d, [&](int s) {
                    if (s < 0) return -9.f;
                    float v = room(p, s) > 0 ? 10.f : 0.f;
                    for (int16_t a : g.systems[size_t(s)].adj)
                        if (otherShipsIn(g, p, a)) v += 1.f;  // a front
                    return v + float(g.systems[size_t(s)].space[size_t(p)].nonFighterShips()) * 0.2f;
                }));
                return true;
            }
            return false;
        }
        case Ab::DsDemiQueen:
            if (d.prompt.find("give the") != std::string::npos) {  // to whoever holds the most promissory notes
                out = one(bestBy(d, [&](int q) { return q < 0 ? -1.f : float(g.players[size_t(q)].notes.size()); }));
                return true;
            }
            return false;
        // ---- batch 6 (0.42.0)
        case Ab::DsBranchPlanet: out = one(bestBy(d, [&](int x) { return planetWorth(g, x); })); return true;  // (our own planet: the best)
        case Ab::DsHolding: out = one(has(d, 0) ? 0 : 1); return true;
        case Ab::DsTargeted:
            if (d.prompt.find("which neighbour") != std::string::npos) {  // the neighbour with the most planets to host it
                out = one(bestBy(d, [&](int q) { return q < 0 ? -1.f : float(controlledPlanets(g, q).size()); }));
                return true;
            }
            out = one(maxOption(d) >= 0 ? bestBy(d, [&](int n) { return n < 0 ? -1.f : 1.f; }) : -1);
            return true;
        case Ab::DsSolis:
            if (g.fixes >= 29 && d.value >= 1000) {  // (the Veldyr review) another player's research: only for the player it was promised to
                const int b = d.value - 1000;
                out = yes(b == p || (d.targetPlayer >= 0 && b == d.targetPlayer));
                return true;
            }
            out = one(bestBy(d, [&](int x) { return x < 0 ? -1.f : float(planetInfluence(g, x)) - 0.6f * float(planetResources(g, x)); }));
            return true;
        case Ab::DsAurora: out = yes(d.value == 1); return true;  // (when someone else comes)
        case Ab::DsAuberon: out = one(bestBy(d, [&](int t) { return t < 0 ? -1.f : aiTechWant(g, p, t); })); return true;
        case Ab::DsTrapPlace: {  // the nastiest trap first
            static const int kOrder[5] = {1, 2, 3, 0, 4};
            for (int k : kOrder)
                if (has(d, k)) {
                    out = one(k);
                    return true;
                }
            out = one(-1);
            return true;
        }
        case Ab::DsTrapPlanet: out = one(bestBy(d, [&](int x) { return planetWorth(g, x) + 2.f * float(threatNear(g, p, g.planets[size_t(x)].system)); })); return true;
        case Ab::DsTrapReveal: out = yes(true); return true;
        case Ab::DsGridTarget: out = one(bestBy(d, [&](int q) { return q < 0 ? -1.f : 1.f; })); return true;
        case Ab::DsSiphonTake: {
            int best = -1;
            for (const Option& o : d.options)
                if (o.enabled && o.id >= 0 && (best < 0 || o.id == 0)) best = o.id;
            out = one(best);
            return true;
        }
        case Ab::DsWraith: {  // 0.43.0 (D127): the biggest ship that can't get into the fight otherwise
            const int act = g.activeSystem;
            bool target = false;
            if (act >= 0) {
                target = otherShipsIn(g, p, act);
                for (int16_t x : g.systems[size_t(act)].planets) target = target || g.planets[size_t(x)].owner != p;
            }
            out = one(bestBy(d, [&](int c) {
                if (c < 0 || !target) return c < 0 ? 0.f : -1.f;
                const int s = c / 16;
                const UnitType t = UnitType(c % 16);
                if (t == UnitType::Fighter || s == act || shipReach(g, p, s, act, t).ok || otherShipsIn(g, p, s)) return -1.f;
                return 0.5f + 0.1f * float(unitCost2(g, p, t));
            }, true));
            return true;
        }
        case Ab::DsCounselor: out = yes(true); return true;
        case Ab::DsVasra: {
            // (fixes 28, the Li-Zho review) for the player it promised the agent to as well (d.targetPlayer): their ships' systems
            const int b = g.fixes >= 28 && d.targetPlayer >= 0 && d.targetPlayer < g.numPlayers ? int(d.targetPlayer) : p;
            // (checkpoint 2 - lzvasra ON, owner 2026-11-16 18:12 "let's get those switches on") about half the fighters the Li-Zho lose
            // over the fleet pool came straight from Vasra Ivo (Heavy Bomber II: fighters beyond capacity count against the fleet pool):
            // only where the 2 fit, else keep her (lost fighters 3.2 -> 1.5-1.7 a game)
            const bool fitOnly = aiTune("lzvasra", 1.f) > 0.f;
            out = one(b == g.activePlayer ? bestBy(d, [&](int s) {
                if (s < 0 || s == 99999) return -1.f;
                if (fitOnly) {
                    Force f = g.systems[size_t(s)].space[size_t(b)];
                    f.add(UnitType::Fighter, 2);
                    if (overflowOf(g, b, s, f) > 0 || fleetUse(g, b, s, f) > fleetLimit(g, b)) return -2.f;
                }
                return 1.f + float(threatNear(g, b, s));
            }) : -1);
            return true;
        }
        case Ab::DsKhazRin:
            if (d.prompt.find("where does") != std::string::npos) {  // keep a Trap where it is unless it sits in reserve
                if (has(d, -1) && d.options[0].label.rfind("Leave it on", 0) == 0) {
                    out = one(-1);
                    return true;
                }
                out = one(bestBy(d, [&](int x) { return x < 0 ? -1.f : (g.planets[size_t(x)].owner == p ? planetWorth(g, x) + 3.f : 0.f); }));
                return true;
            }
            if (d.prompt.find("how many fighters") != std::string::npos) {
                out = one(d.value > 0 ? std::min(maxOption(d), d.value - 1) : maxOption(d));  // (0.46.1: only what fits)
                return true;
            }
            return false;
        case Ab::DsOroZhin: out = one(-1); return true;
        case Ab::DsCombatDrones: out = isYesNo(d) ? yes(true) : one(maxOption(d)); return true;  // (fixes 28: how many - all)
        case Ab::DsAssaultMachina: out = one(0); return true;  // (fixes 28: transported infantry stay infantry)
        case Ab::DsOrbitalGrid:
        case Ab::DsOrbitalShield: out = d.options.size() == 2 && isYesNo(d) ? yes(true) : one(bestBy(d, [&](int x) { return planetWorth(g, x); })); return true;
        case Ab::DsExcavation:  // (fixes 28: which PDS - only asked once the note is played)
            out = one(d.prompt.find("from which planet") != std::string::npos ? d.options[0].id : -1);
            return true;
        case Ab::DsLogicMachina:
            // (fixes 28, the Mirveda review) for another player only when promised (d.targetPlayer): keep it; their token
            if (g.fixes >= 28 && d.targetPlayer >= 0 && d.targetPlayer != p) {
                out = one(g.agentFor[size_t(p)] == d.targetPlayer ? 1 : 0);
                return true;
            }
            // (the Mirveda review, checkpoint 2 - test knob mvlogic, OFF: on 2026-11-16 19:38, off again 20:49 - owner "keep the Gauss Cannon
            // switch on instead, you can turn off the other"; with both on 34 wins in 180 games against 39) used once in 90 games with "2+
            // strategy tokens"; with 1+ 0.8-0.9 a game
            out = yes(me.strategy >= (aiTune("mvlogic", 0.f) > 0.f ? 1 : 2));
            return true;
        case Ab::DsWrathMachina:
            if (d.prompt.find("which system") != std::string::npos) {
                out = one(bestBy(d, [&](int s) {
                    float v = 0;
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p) v += float(g.systems[size_t(s)].space[size_t(q)].nonFighterShips()) * (1.f + 0.2f * float(g.players[size_t(q)].vp));
                    return v;
                }));
                return true;
            }
            return false;
        case Ab::DsEmergency: {  // (0.44.5) a new dock from reinforcements; moving one only away from danger
            if (d.prompt.find("which space dock") != std::string::npos) {
                if (has(d, -1)) {
                    out = one(-1);
                    return true;
                }
                const int home = me.homeSystem;
                out = one(bestBy(d, [&](int x) {
                    if (x < 0 || x >= int(g.planets.size()) || g.planets[size_t(x)].system == home) return -99.f;
                    return float(threatNear(g, p, g.planets[size_t(x)].system)) - float(planetResources(g, x));
                }, -98.f));
                if (out.picks[0] < 0) out = one(bestBy(d, [](int) { return 0.f; }));
                return true;
            }
            out = one(bestBy(d, [&](int x) { return dockSpot(g, p, x); }, -1e9f));
            return true;
        }
        case Ab::DsLocalContracts: {  // (0.44.5) where 1 unit's Production does most; a token already there costs nothing
            const int home = me.homeSystem;
            out = one(bestBy(d, [&](int s) {
                if (s < 0 || s >= int(g.systems.size())) return -99.f;
                const bool token = g.systems[size_t(s)].hasToken(p);
                // (a token at home locks it for the rest of the round)
                return float(singleUnitProduction(g, p, s)) + 0.5f * float(threatNear(g, p, s)) + (token ? 2.f : 0.f) - (s == home && !token ? 3.f : 0.f);
            }, -98.f));
            if (out.picks[0] < 0) out = one(bestBy(d, [](int) { return 0.f; }));
            return true;
        }
        case Ab::DsDeliverance:  // (0.44.8) the kind of ship the hit destroys and costs them most
            if (yn) {
                out = yes(true);
                return true;
            }
            out = one(bestBy(d, [&](int t) {
                const int s = g.activeSystem;
                float v = 0.f;
                for (int q = 0; q < g.numPlayers && s >= 0; ++q) {
                    if (q == p) continue;
                    const Force& f = g.systems[size_t(s)].space[size_t(q)];
                    if (!f.n[size_t(t)]) continue;
                    const bool soaks = unitSustains(g, q, UnitType(t)) && f.damaged(UnitType(t)) < f.n[size_t(t)];
                    v = std::max(v, float(unitCost2(g, q, UnitType(t))) * (soaks ? 0.3f : 1.f));
                }
                return v;
            }, -1.f));
            return true;
        case Ab::DsDaedalonTech: {  // (0.44.5) the hit where it destroys the most: a damaged dreadnought, a cruiser...
            if (yn) {
                out = yes(true);
                return true;
            }
            const int s = g.activeSystem;
            out = one(bestBy(d, [&](int t) {
                if (t == int(UnitType::Dreadnought)) {
                    int dmg = 0;
                    for (int q = 0; q < g.numPlayers && s >= 0; ++q)
                        if (q != p) dmg += g.systems[size_t(s)].space[size_t(q)].damaged(UnitType::Dreadnought);
                    return dmg > 0 ? 4.f : 0.5f;
                }
                return t == int(UnitType::Cruiser) ? 2.f : 1.f;
            }));
            return true;
        }
        case Ab::DsParallel: out = one(spendable(g, p, false) >= 3 ? bestBy(d, [&](int s) { return s < 0 ? -1.f : 1.f + float(threatNear(g, p, s)); }) : -1); return true;
        case Ab::DsGhotiRelay: out = one(bestBy(d, [&](int s) { return s < 0 ? -1.f : 1.f + float(threatNear(g, p, s)); })); return true;
        case Ab::DsBecece: out = one(d.player == g.activePlayer || has(d, 1) ? (spendable(g, p, false) >= 4 ? 1 : 2) : -1); return true;  // (a bot is asked for others only when promised)
        case Ab::DsNmenmede: out = one(bestBy(d, [&](int c) { return c < 0 ? -1.f : float(unitCost2(g, p, UnitType(c % 16)) - unitCost2(g, p, UnitType(c / 16))); })); return true;
        case Ab::DsTioleombp: out = one(-1); return true;
        case Ab::DsNetworked:
        case Ab::DsGhotiPlanet: {
            const bool votes = d.prompt.find("votes") != std::string::npos;
            if (votes && g.currentAgenda >= 0) {
                // (0.44.6) the extra votes on a contested agenda, or on the phase's last one - not spent on the first
                // agenda whatever it is (neither is ready again before the status phase)
                float conf = 1.f;
                aiPredictOutcome(g, g.currentAgenda, &conf);
                out = yes(g.agendaNumber >= 1 || conf < 0.65f);
                return true;
            }
            out = yes(votes || d.prompt.find("influence") != std::string::npos || d.prompt.find("resources") != std::string::npos);
            return true;
        }
        default: return false;
    }
}

// Component actions (6000 + Ab): Blessing of the Queens, Spark Thrusters, Thakt Clqua, The Venerable.
bool aiDsComponentValue(const Game& g, int p, int ab, float& v) {
    if (ab < int(Ab::DsZealous)) return false;
    const Player& me = g.players[size_t(p)];
    switch (Ab(ab)) {
        case Ab::DsBlessing: {  // a command token back where our ships are needed again
            v = 0.f;
            for (size_t s = 0; s < g.systems.size(); ++s)
                if (g.systems[s].hasToken(p) && g.systems[s].space[size_t(p)].nonFighterShips() >= 2) v = std::max(v, 0.8f + 0.2f * float(threatNear(g, p, int(s))));
            return true;
        }
        case Ab::DsSpark: {  // a strategy token we can spare (3+) or a 5th relic fragment
            const int t = findTech("dslaneb");
            const bool token = t >= 0 && ownsTechKey(g, p, "dslaneb") && !me.techExhausted(t) && me.strategy >= 3;
            v = (token || fragmentCount(g, p) >= 5) && sparkBest(g, p) > 0.f ? 0.45f : 0.f;
            return true;
        }
        case Ab::DsbtCymiae:  // cymmill (the Cymiae review, checkpoint 2; owner 2026-11-15: "let them use their breakthrough
            // action when it suits them" - ON): 3 more cards in the discard pile for Autonetic Memory to pick from
            v = aiTune("cymmill", 0.6f) > 0.f && g.actionDeck.size() >= 3 ? aiTune("cymmill", 0.6f) : 0.f;
            return true;
        case Ab::DsbtBentor: v = g.round >= 3 ? 0.2f : 0.f; return true;  // (the others explore too: a foothold late, before passing)
        case Ab::DsbtGledge: {  // (the Gledge review) Dhonraz Installations: worth it when the dice can kill 1+ ground forces somewhere
            float best = 0.f;
            const float hits = 0.6f * float(dhonrazDice(g, p));
            for (int x : dhonrazTargets(g, p)) {
                int n = 0;
                for (int q = 0; q < g.numPlayers; ++q) n += q != p ? groundForcesOn(g, q, x) : 0;
                best = std::max(best, std::min(hits, float(n)));
            }
            // (checkpoint 2 - test knob dhonval, default 0.4) the weight of an expected kill. 1.0 tested: 3.2 uses a game against 3.1,
            // 11 wins / 6.80 VP of 90 against 15 / 6.76 - kept at 0.4
            v = best >= 1.f ? aiTune("dhonval", 0.4f) * best : 0.f;
            return true;
        }
        case Ab::DsbtEdyn: v = g.round >= 3 ? 0.6f : 0.f; return true;
        case Ab::DsbtKolume: v = me.tactic + me.fleet + me.strategy < 8 ? 1.0f : 0.6f; return true;
        case Ab::DsbtNokar: {  // 0.44.4: only with a good spot for the neutral fleet
            float best = 0.f;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                if (g.systems[s].isHyperlane() || g.systems[s].fracture || g.systems[s].offBoard) continue;
                bool ships = false;
                for (int q = 0; q < g.numPlayers && !ships; ++q) ships = hasShips(g, q, int(s));
                if (!ships) best = std::max(best, nokarSpot(g, p, int(s)));
            }
            v = g.round >= 2 && best >= 2.f ? 0.6f : 0.f;
            return true;
        }
        case Ab::DsEncryptionKey:  // 0.43.0: attaching a note is an action now - worth it before the late game
        case Ab::DsGledgeBase: v = g.round <= 6 ? 1.1f : 0.6f; return true;
        case Ab::DsVehlTikar: {
            float tired = 0;
            for (int x : controlledPlanets(g, p))
                if (g.planets[size_t(x)].exhausted) tired += float(planetResources(g, x) + planetInfluence(g, x));
            v = tired >= 8 ? 3.f : 0.f;
            return true;
        }
        case Ab::DsSaintBinal: v = g.round >= 3 ? 4.f : 0.5f; return true;
        case Ab::DsMeditation:  // (0.43.0: it also unlocks Issac of Sinci - 0 unlocks in 18 games before)
            // (the Kolume review, checkpoint 2 - knob kumed, ON at 2.5 - owner 2026-11-16 12:57 "turn the meditation switch on") never used in
            // 270 games (Issac unlocked in 1-4%): worth the knob while the commander is locked, with 1 strategy token left too (tested:
            // Meditation in 21 of 90 games, Issac unlocked 23%, 21 / 7.39 vs 19 / 7.30)
            if (aiTune("kumed", 2.5f) > 0.f && leaderStatus(g, p, kCommander) == kLocked) {
                v = me.strategy >= 1 ? aiTune("kumed", 2.5f) : 0.f;
                return true;
            }
            v = me.strategy >= 2 ? (leaderStatus(g, p, kCommander) == kLocked && g.round >= 2 ? 1.1f : 0.6f) : 0.f;
            return true;
        case Ab::DsBiothermics: {  // (0.43.0) a stall instead of passing: Applied Biothermics pays for each player who
            int still = 0;         // passed before us
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && !g.players[size_t(q)].passed && !g.players[size_t(q)].eliminated) ++still;
            v = still > 0 && me.commodities < commodityCap(g, me) ? 0.35f : 0.f;
            return true;
        }
        case Ab::DsFran: {  // (0.43.0) out of tactic tokens with a spare fleet token: one more tactical action
            int fleetNeed = 0;
            for (const SystemState& s : g.systems) fleetNeed = std::max(fleetNeed, s.space[size_t(p)].nonFighterShips());
            v = me.tactic == 0 && (me.fleet > fleetNeed + 1 || me.strategy >= 2) ? 0.9f : 0.f;
            if (aiAgentPromisedTo(g, p) >= 0) v = 2.5f;  // (rules 28) a promised Disciple Fran
            return true;
        }
        case Ab::DsWonell: v = g.round >= 4 ? 1.0f : 0.f; return true;
        case Ab::DsThakt: v = g.round >= 3 ? 6.f : 1.f; return true;
        // ---- batch 4 (0.40.0)
        case Ab::DsSeeker: v = 0.8f; return true;
        case Ab::DsGriphyn: v = g.round >= 3 ? 3.5f : 1.f; return true;
        case Ab::DsBayan: {
            int n = 0;
            for (const SystemState& s : g.systems) n += s.space[size_t(p)].nonFighterShips() ? 1 : 0;
            v = g.round >= 3 && n >= 3 ? 0.8f * float(n) : 0.f;
            return true;
        }
        case Ab::DsPutriv: {
            int n = 0;
            for (int q = 0; q < g.numPlayers; ++q) n += q != p ? g.dsDebt[q] : 0;
            v = n >= 3 ? 0.9f * float(n) : 0.f;
            return true;
        }
        case Ab::DsVoiceUnited: v = g.round >= 3 ? 2.5f : 0.5f; return true;
        case Ab::DsAlgorithmic: {
            float best = 0;
            for (int16_t c : g.actionDiscard) best = std::max(best, aiCardKeepValue(c));
            v = std::max(0.f, best - 1.f);
            return true;
        }
        case Ab::DsKwill: {
            int foe = 0, mine = 0;
            for (const SystemState& s : g.systems)
                for (int q = 0; q < g.numPlayers; ++q)
                    for (int t : kShipIdx)
                        if (s.space[size_t(q)].n[t] > s.space[size_t(q)].dmg[t] && unitSustains(g, q, UnitType(t)))
                            (q == p ? mine : foe) += s.space[size_t(q)].n[t] - s.space[size_t(q)].dmg[t];
            v = g.round >= 3 ? std::max(0.f, 0.4f * float(foe - 2 * mine)) : 0.f;
            // (the Nivyn review, checkpoint 2 - test knob nvkrill) before an attack: an enemy fleet with ships that can sustain damage next
            // to 2+ of our non-fighter ships - cracked hulls there for the rest of the round (the hero stays out until it ends, rules 29).
            // REVISIT (checkpoint 2, 90 forced games each): Krill Drakkon used 0.90 a game either way (the board value already decides) -
            // 15 wins / 7.48 against 14 / 7.47, second sample 18 / 7.44 against 19 / 7.46: no gain - off
            if (aiTune("nvkrill", 0.f) > 0.f && g.round >= 2) {
                int best = 0;
                for (size_t s = 0; s < g.systems.size(); ++s) {
                    if (g.systems[s].space[size_t(p)].nonFighterShips() < 2) continue;
                    for (int16_t a : g.systems[s].adj)
                        for (int q = 0; q < g.numPlayers; ++q) {
                            if (q == p) continue;
                            int n = 0;
                            for (int t : kShipIdx)
                                if (unitSustains(g, q, UnitType(t))) n += g.systems[size_t(a)].space[size_t(q)].n[t] - g.systems[size_t(a)].space[size_t(q)].dmg[t];
                            best = std::max(best, n);
                        }
                }
                if (best >= 2) v = std::max(v, 1.5f + 0.5f * float(best));
            }
            return true;
        }
        case Ab::DsPahn: v = g.round >= 3 ? 0.25f * float(controlledPlanets(g, p).size()) : 0.f; return true;
        case Ab::DsIncite: v = 0.4f; return true;
        // ---- batch 5 (0.41.0)
        case Ab::DsCordo: v = 1.2f; return true;
        case Ab::DsOtto: v = g.round >= 4 ? 2.f : 0.f; return true;
        case Ab::DsMidir: {
            int n = 0;
            for (size_t s = 0; s < g.systems.size(); ++s)
                for (int16_t x : g.systems[s].planets) n += g.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech) ? 1 : 0;
            v = g.round >= 3 ? 1.5f * float(std::min(n, 4)) : 0.f;
            return true;
        }
        case Ab::DsLocalContracts: v = spendable(g, p, false) >= 4 ? 1.f : 0.f; return true;
        case Ab::DsStarsails:
            // (the Nokar review, checkpoint 2 - test knob nksails) earlier: from round 2 with 4 planets and 4 to spend - a fleet from every
            // planet while it can still take ground. REVISIT (checkpoint 2, 90 forced games each): used 0.89 a game instead of 0.83 - 14
            // wins / 7.11 against 15 / 7.17, second sample 11 / 6.70 against 11 / 6.70: no gain - off
            if (aiTune("nksails", 0.f) > 0.f)
                v = g.round >= 2 && controlledPlanets(g, p).size() >= 4 && spendable(g, p, false) >= 4 ? 3.f : 0.f;
            else
                v = g.round >= 3 && controlledPlanets(g, p).size() >= 5 && spendable(g, p, false) >= 6 ? 2.5f : 0.f;
            return true;
        case Ab::DsEmergency: {  // (0.44.5: worth it for a new dock on a good planet; not for moving one around)
            const float spot = unitsAvailable(g, p, UnitType::SpaceDock) > 0 ? bestDockSpot(g, p) : -1.f;
            v = spot >= 2.f ? 1.5f + 0.25f * std::min(spot, 4.f) : 0.f;
            return true;
        }
        case Ab::DsShipmonger: v = aiAgentPromisedTo(g, p) >= 0 ? 2.5f : 0.9f; return true;  // (rules 28: a promised one)
        case Ab::DsDemiQueen: v = g.round >= 3 ? 2.f : 0.f; return true;
        case Ab::DsInsight: v = 0.8f; return true;
        case Ab::DsKorela: v = g.round >= 3 ? 2.5f : 0.5f; return true;
        case Ab::DsKantrus: v = g.round >= 3 ? 3.f : 0.5f; return true;
        // ---- batch 6 (0.42.0)
        case Ab::DsAuberon: v = g.round >= 3 ? 3.f : 0.5f; return true;
        case Ab::DsKhazRin: v = g.round >= 4 ? 2.5f : 0.f; return true;
        case Ab::DsWrathMachina: v = g.round >= 3 ? 3.f : 0.f; return true;
        case Ab::DsNmenmede: v = g.round >= 3 ? 3.f : 0.f; return true;
        // ---- batch 3 (0.39.0)
        case Ab::DsMantle: {
            float best = -9.f;
            for (int x : controlledPlanets(g, p))
                if (x >= 0 && g.planets[size_t(x)].system != me.homeSystem && std::strcmp(kPlanets[g.planets[size_t(x)].def].key, "mr") &&
                    std::find(g.dsCores.begin(), g.dsCores.end(), int16_t(x)) == g.dsCores.end())
                    best = std::max(best, 2.f - float(planetResources(g, x)) - 0.6f * float(planetInfluence(g, x)));
            v = best > -9.f ? 2.5f + 0.4f * best : 0.f;
            return true;
        }
        case Ab::DsMgur: {
            int n = 0;
            for (unsigned b = me.dsFrags; b; b &= b - 1) ++n;
            v = n >= 2 ? 0.5f * float(n) : 0.f;
            if (n >= 1 && aiAgentPromisedTo(g, p) >= 0) v = 2.5f;  // (rules 28) a promised C.O.O. Mgur
            return true;
        }
        case Ab::DsKenTucc: {
            int n = 0;
            for (int x : controlledPlanets(g, p))
                n += hasTraitOf(g, x, Trait::Cultural) || hasTraitOf(g, x, Trait::Hazardous) || hasTraitOf(g, x, Trait::Industrial) ? 1 : 0;
            // (the Bentor review, checkpoint 2 - knob kentucc: the fewest planets worth it, default 5) used in 53 of 83 games
            // where he unlocked; kentucc=3: used in 75 games, 11 wins / 6.66 VP against 12 / 6.70 - no gain, left at 5
            v = n >= int(aiTune("kentucc", 5.f)) ? 0.8f * float(n) : 0.f;
            return true;
        }
        case Ab::DsImpressAction: v = 0.9f; return true;
        case Ab::DsGorthrim: v = g.round >= 3 ? 4.f : 1.f; return true;
        case Ab::DsAtropha:
            // (the Augurs review, checkpoint 2 - knob atrophawait, default 0 = off) a third of the uses kept the card already on
            // the sheet (the hero spent on a look): only when that card fits us poorly (less than half done, not met). Wasted
            // uses 29 -> 14 (fresh seeds 25 -> 15) but wins 14 / 6.96 against 13 / 6.99 (fresh 11 / 6.73 against 10 / 6.76) -
            // no real gain: OFF (owner 2026-11-14 kept only the objective planning). REVISIT with a better test of the
            // drawn cards' worth (e.g. use her the turn before the status phase, or to deny the leader the next objective)
            if (aiTune("atrophawait", 0.f) > 0.f) {
                v = g.round >= 2 && objectiveFit(g, p, me.dsSheet) < 2.5f ? 2.5f : 0.f;
                return true;
            }
            v = g.round >= 2 ? 2.5f : 0.5f;
            return true;
        // (fixes 28, the Augurs review) Read the Fates is played face up as an action now: the bots never use the
        // objective it shows, and face up it goes back when they activate a system with Augurs units - kept in hand
        case Ab::DsReadFates: v = 0.f; return true;
        case Ab::DsYgegnad: {
            int n = 0;
            for (int16_t gl : g.dsGlory) {
                bool tok = g.systems[size_t(gl)].hasToken(p);
                for (int16_t a : g.systems[size_t(gl)].adj) tok = tok || g.systems[size_t(a)].hasToken(p);
                n += tok || g.fixes >= 28 ? 1 : 0;  // (rules 28: 1 command token for each Glory token anyway)
            }
            v = n >= 2 ? 1.2f * float(n) : 0.f;
            return true;
        }
        case Ab::DsBanua: {
            int n = 0;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                bool planet = false;
                for (int16_t x : g.systems[s].planets) planet = planet || g.planets[size_t(x)].owner == p;
                n += planet && !otherShipsIn(g, p, int(s)) ? 1 : 0;
            }
            v = g.round >= 3 && n >= 4 ? 0.4f * float(n) : 0.f;
            return true;
        }
        case Ab::DsVenerable: {
            int inPlay = 0;
            for (const Player& q : g.players) inPlay += int(q.relics.size());
            v = inPlay >= 2 || g.round >= 6 ? 1.f + 0.8f * float(inPlay) : 0.f;
            (void)me;
            return true;
        }
        default: v = 0.f; return true;
    }
}

}  // namespace ti
