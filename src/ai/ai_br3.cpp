// Computer players and the Pharad'n Order (0.85.0): Ab::Phar* (rules_br3.cpp). Called from aiFactionAnswer before aiBrAnswer.
// Rules of thumb; the engine checks every answer.
//   Ancient Empire     tombs on the 14 planets nearest their home (the ones they will take), worth first
//   Nokash             on themselves: a planet whose infantry aren't needed (no enemy near) when it pays 2+ trade goods
//   Hidden Vaults      produce on every explore; destroy 2 infantry on a safe planet when a Sacrotitan can rise there
//   Avhkan             the unlocking action from round 3 with 5 planets of spare infantry; at a ground combat always
//   Pharad'n the Immortal  when the defence would fail without it: the better of "destroy all infantry" / "captured infantry join"
//   Sacrotitan / Immortal II / Black Pyramid  yes - the threatened planet first
//   Reality-Field Impactor  the anomaly that lets most of our ships reach the active system (a game copy per anomaly), or the
//                      active system's nebula when other players' ships defend it
//   Death Binding (holder)  with 6+ infantry on the board, the technology it wants most of a short list
//   Sedtra (move 0, +1 per ground force carried): aiPharFixMove adds ground forces from its system to a move plan that needs them
#include <algorithm>
#include <cstring>
#include <map>

#include "ai/ai.h"
#include "ai/ai_internal.h"
#include "engine/query.h"
#include "engine/rules.h"

namespace ti {

namespace {
Answer one(int id) {
    Answer a;
    a.picks.push_back(id);
    return a;
}
bool has(const Decision& d, int id) {
    for (const Option& o : d.options)
        if (o.enabled && o.id == id) return true;
    return false;
}
// other players' ships in sys or next to it
int threatAt(const Game& g, int p, int sys) {
    if (sys < 0) return 0;
    int n = 0;
    auto add = [&](int s) {
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p) n += g.systems[size_t(s)].space[size_t(q)].nonFighterShips();
    };
    add(sys);
    for (int16_t a : g.systems[size_t(sys)].adj) add(a);
    return n;
}
float worth(const Game& g, int x) {
    return float(planetResources(g, x) + planetInfluence(g, x)) + (isLegendaryPlanet(g, x) ? 2.f : 0.f) + (g.planets[size_t(x)].system == g.mecatol() ? 2.f : 0.f);
}
int enemyGroundAt(const Game& g, int p, int x) {
    int n = 0;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p) n += groundForcesOn(g, q, x);
    return n;
}
int homeDist(const Game& g, int p, int sys) {
    const int h = g.players[size_t(p)].homeSystem;
    if (h < 0 || sys < 0) return 9;
    return hexDistance(g.systems[size_t(h)].pos, g.systems[size_t(sys)].pos);
}
// Nokash on p's own planet x: the trade goods it brings, less what the infantry are worth there
float nokashValue(const Game& g, int p, int x) {
    const Player& pl = g.players[size_t(p)];
    const int k = g.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry);
    if (k <= 0) return -9.f;
    const int gain = std::max(0, std::min(k, commodityValue(g, p) - int(pl.commodities)));
    const int tg = pl.commodities + gain;
    const int sys = g.planets[size_t(x)].system;
    float v = float(tg) - 0.35f * float(k);
    if (threatAt(g, p, sys) > 0) v -= 2.5f + 0.5f * float(k);
    if (isHomePlanet(g, x)) v -= 0.5f;
    if (g.planets[size_t(x)].units[size_t(p)].structures() > 0 && g.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech) == 0) v -= 0.8f;
    return v;
}
}  // namespace

float aiPharFreeValue(const Game& g, int p, int id) {
    if (!brPharadn(g, p)) return 0.f;
    if (id == 670) {  // Nokash on ourselves - or on the player we sold it to (a deal's "use your agent for me")
        if (aiAgentPromisedTo(g, p) >= 0) return 1.f;
        float best = 0.f;
        for (int x : controlledPlanets(g, p)) best = std::max(best, nokashValue(g, p, x));
        return best >= 1.2f ? 0.5f + 0.2f * best : 0.f;
    }
    if (id == 671) {  // Hidden Vaults: 2 infantry for a Sacrotitan
        const Player& pl = g.players[size_t(p)];
        if (!mechAbilities(g) || g.phase != Phase::Action || pl.pharMech == int32_t(g.turnCount) + 1 || unitsForPlacement(g, p, UnitType::Mech) <= 0)
            return 0.f;
        for (int x : controlledPlanets(g, p))
            if (g.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry) >= 3 && threatAt(g, p, g.planets[size_t(x)].system) == 0) return 0.7f;
        return 0.f;
    }
    return 0.f;
}

bool aiPharComponentValue(const Game& g, int p, int ab, float& v) {
    if (Ab(ab) != Ab::PharAvhkanUnlock) return false;
    // the commander's unlock: 10 infantry (captured: they come back) for "destroy 1 infantry" at every defence
    int spare = 0;
    for (int x : controlledPlanets(g, p))
        if (g.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry) >= 2 && threatAt(g, p, g.planets[size_t(x)].system) == 0) ++spare;
    v = g.round >= 3 && spare >= 5 ? 1.1f : 0.f;
    return true;
}

// Sedtra moves only with ground forces aboard: a plan that moves it without them gets some from its system (or leaves it at home)
void aiPharFixMove(const Game& g, int p, int active, std::vector<int>& data) {
    if (!brPharadn(g, p) || unitDef(g, p, UnitType::Flagship).move != 0) return;
    if (checkMovePlan(g, p, active, data, false, true).empty()) return;
    int from = -1;
    for (size_t i = 0; i + 3 < data.size(); i += 4)
        if (data[i + 1] < 0 && data[i + 2] == int(UnitType::Flagship) && data[i + 3] > 0) from = data[i];
    if (from < 0) return;
    std::vector<int> d2 = data;
    int moving = 0;
    for (size_t i = 0; i + 3 < d2.size(); i += 4)
        if (d2[i] == from && isGroundForce(UnitType(d2[i + 2]))) moving += d2[i + 3];
    const SystemState& s = g.systems[size_t(from)];
    // the ground forces in its space area that the plan leaves, then those on its planets
    int taken[kUnitTypes] = {};
    for (size_t i = 0; i + 3 < d2.size(); i += 4)
        if (d2[i] == from && d2[i + 1] < 0) taken[d2[i + 2]] += d2[i + 3];
    for (UnitType t : {UnitType::Infantry, UnitType::Mech}) {
        const int left = s.space[size_t(p)].count(t) - taken[int(t)];
        const int k = std::min(left, 3 - moving);
        if (k > 0) {
            d2.insert(d2.end(), {from, -1, int(t), k});
            moving += k;
        }
    }
    for (int16_t x : s.planets)
        for (UnitType t : {UnitType::Infantry, UnitType::Mech}) {
            if (moving >= 3 || g.planets[size_t(x)].owner != p) continue;
            const int have = g.planets[size_t(x)].units[size_t(p)].count(t);
            const int keep = t == UnitType::Infantry && threatAt(g, p, from) > 0 ? 1 : 0;
            const int k = std::min(have - keep, 3 - moving);
            if (k > 0) {
                d2.insert(d2.end(), {from, int(x), int(t), k});
                moving += k;
            }
        }
    if (checkMovePlan(g, p, active, d2, false, true).empty()) {
        data = d2;
        return;
    }
    // or without it
    std::vector<int> d3;
    for (size_t i = 0; i + 3 < data.size(); i += 4)
        if (!(data[i] == from && data[i + 1] < 0 && data[i + 2] == int(UnitType::Flagship)))
            d3.insert(d3.end(), {data[i], data[i + 1], data[i + 2], data[i + 3]});
    if (checkMovePlan(g, p, active, d3, false, true).empty()) data = d3;
}

bool aiPharAnswer(const Game& g, const Decision& d, Answer& out) {
    const int p = d.player;
    if (p < 0 || p >= g.numPlayers || int(d.ability) < int(Ab::PharTombs) || int(d.ability) > int(Ab::PharPyramid)) return false;
    const Ab ab = Ab(d.ability);
    switch (ab) {
        case Ab::PharTombs: {  // the planets nearest home first, the better ones among them; other homes last
            std::vector<std::pair<float, int>> s;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                const int x = o.id;
                const int sys = g.planets[size_t(x)].system;
                float v = 10.f - 2.2f * float(homeDist(g, p, sys)) + 0.4f * worth(g, x);
                if (isHomePlanet(g, x)) v -= 12.f;
                s.push_back({v, x});
            }
            std::sort(s.begin(), s.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
            Answer a;
            for (size_t i = 0; i < s.size() && int(i) < d.maxPick; ++i) a.picks.push_back(s[i].second);
            out = a;
            return true;
        }
        case Ab::PharNokash: {  // ourselves - or the player we promised it to
            const int q = aiAgentPromisedTo(g, p);
            out = one(q >= 0 && has(d, q) ? q : has(d, p) ? p : -1);
            return true;
        }
        case Ab::PharNokashPlanet: {
            int best = -1;
            // (another player's offer: only when it pays well - unless we bought it)
            const bool bought = d.targetPlayer >= 0 && d.targetPlayer < g.numPlayers && g.agentFor[size_t(d.targetPlayer)] == p;
            float bv = d.targetPlayer == p || d.targetPlayer < 0 ? 1.5f : bought ? 0.3f : 2.5f;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                const float v = nokashValue(g, p, o.id);
                if (v > bv) bv = v, best = o.id;
            }
            out = one(best);
            return true;
        }
        case Ab::PharVaultsDestroy: {  // 2 infantry on a safe planet with 3 or more, for a Sacrotitan
            std::map<int, std::vector<int>> byPlanet;
            for (const Option& o : d.options)
                if (o.enabled && o.id >= 0 && o.id < 10000000 && (o.id % 128) / 8 == int(UnitType::Infantry)) byPlanet[o.id / 128].push_back(o.id);
            Answer a;
            int best = -1, most = 2;
            for (const auto& b : byPlanet) {
                const int x = b.first;
                if (g.planets[size_t(x)].owner != p || threatAt(g, p, g.planets[size_t(x)].system) > 0) continue;
                const int k = g.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry);
                if (k > most) most = k, best = x;
            }
            if (best >= 0 && unitsForPlacement(g, p, UnitType::Mech) > 0) {
                const auto& ids = byPlanet[best];
                for (size_t i = 0; i < ids.size() && i < 2; ++i) a.picks.push_back(ids[i]);
            }
            out = a;
            return true;
        }
        case Ab::PharAvhkanUnlock: {  // the 5 safest planets with the most infantry
            std::vector<std::pair<float, int>> s;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                const int x = o.id;
                s.push_back({float(g.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry)) - 3.f * float(threatAt(g, p, g.planets[size_t(x)].system)), x});
            }
            std::sort(s.begin(), s.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
            Answer a;
            for (size_t i = 0; i < s.size() && i < 5; ++i) a.picks.push_back(s[i].second);
            out = a;
            return true;
        }
        case Ab::PharHero: {
            const int pl = d.value, att = d.targetPlayer;
            if (pl < 0 || att < 0 || att >= g.numPlayers) {
                out = one(0);
                return true;
            }
            const PlanetState& ps = g.planets[size_t(pl)];
            auto strength = [&](int q, bool noInf) {
                const Force& f = ps.units[size_t(q)];
                float s = (noInf ? 0.f : float(f.count(UnitType::Infantry))) + 1.7f * float(f.count(UnitType::Mech));
                if (pyramidOn(g, q, pl)) s += 2.6f;
                if (sleepingWorldOn(g, q, pl) && !pyramidOn(g, q, pl)) s += 2.f;
                return s;
            };
            const float mine = strength(p, false), theirs = strength(att, false);
            if (att == p || mine >= 1.3f * theirs + 0.5f) {  // (holding anyway)
                out = one(0);
                return true;
            }
            const float a1 = strength(p, true) - strength(att, true);                                  // all infantry gone
            const float a2 = mine + float(pharCaptured(g, p)) - theirs;                               // the captured join
            const float stake = worth(g, pl) + (isHomePlanet(g, pl) ? 6.f : 0.f) + 1.5f * float(ps.units[size_t(p)].structures());
            if (stake < 3.f && g.round < 6) {
                out = one(0);
                return true;
            }
            if (has(d, 2) && a2 >= a1 && a2 > 0.f) out = one(2);
            else if (has(d, 1) && a1 > -0.5f && ps.units[size_t(att)].count(UnitType::Infantry) >= 2) out = one(1);
            else if (has(d, 2) && a2 > -1.f) out = one(2);
            else out = one(0);
            return true;
        }
        case Ab::PharHeroCount: {
            int best = 1;
            for (const Option& o : d.options)
                if (o.enabled) best = std::max(best, o.id);
            out = one(best);
            return true;
        }
        case Ab::PharImmortal: {  // the planet most threatened, else the one nearest other players
            int best = -1;
            float bv = -99.f;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                const int x = o.id, sys = g.planets[size_t(x)].system;
                const float v = 3.f * float(threatAt(g, p, sys)) + 0.4f * worth(g, x) - 0.6f * float(g.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry)) +
                                (isHomePlanet(g, x) ? -1.f : 0.5f);
                if (v > bv) bv = v, best = x;
            }
            out = one(best);
            return true;
        }
        case Ab::PharImpactor: {
            const int act = d.value >= 0 ? d.value : g.activeSystem;
            if (act < 0) {
                out = one(-1);
                return true;
            }
            // the active system's nebula with other players' ships in it: their +1 is gone
            int enemies = 0;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p) enemies += g.systems[size_t(act)].space[size_t(q)].ships();
            if (enemies > 0 && has(d, act) && (anomaliesFor(g, -2, act) & kNebula)) {
                out = one(act);
                return true;
            }
            // else: the anomaly that lets the most of our ships reach it
            auto reachWorth = [&](const Game& h) {
                float v = 0.f;
                for (size_t s = 0; s < h.systems.size(); ++s) {
                    if (int(s) == act || h.systems[s].hasToken(p)) continue;
                    const Force& f = h.systems[s].space[size_t(p)];
                    for (int t : kShipIdx) {
                        if (!f.n[t] || t == int(UnitType::Fighter)) continue;
                        if (shipReach(h, p, int(s), act, UnitType(t)).ok) v += float(f.n[t]) * float(unitCost2(h, p, UnitType(t)));
                    }
                }
                return v;
            };
            const float base = reachWorth(g);
            int best = -1;
            float bv = 1.5f;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                Game h = g;
                h.fx.rfiSys = int16_t(o.id);
                const float v = reachWorth(h) - base;
                if (v > bv) bv = v, best = o.id;
            }
            out = one(best);
            return true;
        }
        case Ab::PharDeathTech: {
            static const char* kLike[] = {"nm", "st", "ps", "amd", "aida", "sdn", "da", "pa", "gd", "dxa"};
            for (const char* k : kLike)
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0 && !std::strcmp(kTechs[size_t(o.id)].key, k)) {
                        out = one(o.id);
                        return true;
                    }
            for (const Option& o : d.options)
                if (o.enabled) {
                    out = one(o.id);
                    return true;
                }
            return false;
        }
        case Ab::PharDeathRemove: {  // from the planets with the most infantry and nobody near; space last
            std::vector<std::pair<float, int>> s;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                float v;
                if (o.id >= 10000000) v = -5.f;
                else {
                    const int x = o.id / 8;
                    v = float(g.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry)) - 3.f * float(threatAt(g, p, g.planets[size_t(x)].system)) -
                        0.3f * float(o.id % 8);
                }
                s.push_back({v, o.id});
            }
            std::sort(s.begin(), s.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
            Answer a;
            for (size_t i = 0; i < s.size() && int(i) < 4; ++i) a.picks.push_back(s[i].second);
            out = a;
            return true;
        }
        case Ab::PharDeathBinding: {
            out = one(unitsOnBoard(g, p, UnitType::Infantry) >= 6 && g.round <= 7 ? 1 : 0);
            return true;
        }
        case Ab::PharPyramid: {  // into the fight when there is one, else our best planet here
            int best = -1;
            float bv = -1.f;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                const int x = o.id;
                float v = worth(g, x);
                if (g.planets[size_t(x)].owner != p) v += 6.f + 2.f * float(enemyGroundAt(g, p, x));
                if (v > bv) bv = v, best = x;
            }
            out = one(best);
            return true;
        }
        case Ab::PharAvhkan: {  // (owner 11:14: any infantry there) the invader's; never our own (one less defender now)
            int pickd = 0;
            for (const Option& o : d.options)
                if (o.enabled && o.id > 0 && o.id - 1 != p) pickd = o.id;
            out = one(pickd);
            return true;
        }
        default: break;
    }
    // yes / no questions: Sacrotitan, Hidden Vaults' produce - yes
    out = one(1);
    return true;
}

}  // namespace ti
