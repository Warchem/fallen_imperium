// Computer players and Thunder's Edge's own questions (0.34.0): the expedition to Thunder's Edge, placing it and
// the ingress tokens, and the breakthroughs' choices (Ab::Expedition .. Ab::Bt*). Called from aiFactionAnswer and
// aiFactionComponentValue. Rules of thumb like the rest of the AI; the engine checks every answer.
#include <algorithm>
#include <cstring>

#include "ai/ai.h"
#include "ai/ai_internal.h"
#include "engine/deal.h"
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
bool promisedNoAttack(const Game& g, int p, int q) {  // a no-attack pact either way
    for (const Promise* pr : activePromises(g, p, q))
        if (pr->kind == PromiseKind::NoAttack) return true;
    return false;
}
// the Firmament's traits stay with the Obsidian
bool firmQuirk(const Game& g, int p, int which) { return fquirkIs(g, p, "firmament", which) || fquirkIs(g, p, "obsidian", which); }
int firstReal(const Decision& d) {
    for (const Option& o : d.options)
        if (o.enabled && o.id >= 0) return o.id;
    return d.options.empty() ? -1 : d.options[0].id;
}

// moves from p's home to each system (plain adjacency; 99 = out of reach)
std::vector<int> fromHome(const Game& g, int p) {
    std::vector<int> d(g.systems.size(), 99);
    const int h = g.players[size_t(p)].homeSystem;
    if (h < 0) return d;
    std::vector<int> q{h};
    d[size_t(h)] = 0;
    for (size_t i = 0; i < q.size(); ++i)
        for (int16_t n : g.systems[size_t(q[i])].adj)
            if (d[size_t(n)] > d[size_t(q[i])] + 1) {
                d[size_t(n)] = d[size_t(q[i])] + 1;
                q.push_back(n);
            }
    return d;
}

int slicesOf(const Game& g, int p) {
    int n = 0;
    for (int8_t o : g.expedition) n += o == p ? 1 : 0;
    return n;
}
int slicesClaimed(const Game& g) {
    int n = 0;
    for (int8_t o : g.expedition) n += o >= 0 ? 1 : 0;
    return n;
}

float cheapestCards(const Game& g, int p, int k) {  // what giving up the k least-liked action cards costs
    std::vector<float> v;
    for (int16_t c : g.players[size_t(p)].actionCards) v.push_back(aiCardKeepValue(c));
    std::sort(v.begin(), v.end());
    float s = 0;
    for (int i = 0; i < k && i < int(v.size()); ++i) s += v[size_t(i)];
    return s;
}

// What a slice of the expedition costs this player, in the planner's units (about 1 per resource).
float sliceCost(const Game& g, int p, int s) {
    const Player& me = g.players[size_t(p)];
    switch (s) {
        case 0: return aiFreeSpend(g, p, false) >= 5 ? 4.5f : 99.f;
        case 1: return aiFreeSpend(g, p, true) >= 5 ? (g.round <= 2 ? 3.5f : 4.5f) : 99.f;
        case 2: return me.actionCards.size() >= 2 ? 1.2f + cheapestCards(g, p, 2) : 99.f;
        case 3: {
            if (me.secrets.empty()) return 99.f;
            int worst = 0;  // the secret furthest from done
            for (int16_t o : me.secrets) worst = std::max(worst, objectiveProgress(g, p, o).missing());
            return worst >= 3 ? 3.f : worst == 2 ? 5.f : 8.f;
        }
        case 4: {
            float best = 99.f;
            for (int x : readyPlanets(g, p))
                if (planetHasSkip(g, x)) best = std::min(best, 0.8f + 0.5f * float(planetResources(g, x) + planetInfluence(g, x)));
            return best;
        }
        case 5: return me.tradeGoods >= 3 + aiGoals(g, p).keepTg ? 3.2f : 99.f;
    }
    return 99.f;
}

// What another slice is worth: the first one gains the breakthrough; later ones count for who places infantry on
// Thunder's Edge (a legendary planet) when the sixth is claimed.
float sliceWorth(const Game& g, int p) {
    const Player& me = g.players[size_t(p)];
    // (0.59.0, owner: "the Crimson don't expand into their slice") the Resonance Generator is their Slipstream - the +1
    // move out of a home beside the board: the first slice before anything else
    if (!me.breakthrough && isFaction(g, p, "crimson") && aiTune("crimsonexp", 1.f) > 0.f) return 15.f;
    if (!me.breakthrough) return aiCoreExpeditionLater(g, p) ? 3.5f : 9.f;  // 0.43.0: the guide's round
    const int mine = slicesOf(g, p), all = slicesClaimed(g);
    int rival = 0;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p) rival = std::max(rival, slicesOf(g, q));
    float v = 1.2f;
    if (mine + 1 >= rival) v += 1.5f;      // we'd lead (or tie) the expedition
    if (all == 5) v += 1.5f;               // the last slice: we choose where Thunder's Edge goes
    if (g.round >= 5) v *= 0.6f;           // late: the planet comes too late to matter much
    return v;
}
// Ahk Ravin (Crimson agent, 0.43.0 debug passes - owner T33): our weak ship at the front trades places with a strong
// one in a quiet system (ids system * 16 + type); the gain in fighting worth, 0 if there's none worth an agent
int threatAround(const Game& g, int p, int s) {
    int n = 0;
    std::vector<int> zone{s};
    for (int16_t a : g.systems[size_t(s)].adj) zone.push_back(a);
    for (int z : zone)
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p) n += g.systems[size_t(z)].space[size_t(q)].nonFighterShips();
    return n;
}
float fightWorth(const Game& g, int p, int t) {
    const UnitDef& u = unitDef(g, p, UnitType(t));
    if (u.combat <= 0) return 0.f;
    return float(std::max(0, 11 - u.combat)) * float(std::max<int>(1, u.combatDice)) + (u.sustain ? 3.f : 0.f);
}
// (0.59.0, owner: "the Crimson don't expand into their slice") Ahk Ravin as a ferry: a loaded ship at the home beside
// the board trades places with a cheap ship out at free planets - the whole wave lands next turn instead of two trips
// later. 'front' = the ship out there, 'back' = the carrier at home; the worth of the planets it brings in reach.
float ravinExpand(const Game& g, int p, int* front, int* back) {
    if (aiTune("ravinexp", 1.f) <= 0.f || !isFaction(g, p, "crimson")) return 0.f;
    const int home = g.players[size_t(p)].homeSystem;
    if (home < 0 || !g.systems[size_t(home)].offBoard) return 0.f;
    const SystemState& hs = g.systems[size_t(home)];
    if (hs.hasToken(p)) return 0.f;  // (LRR 95.3: nothing would go along)
    int carrier = -1, cap = 0;
    for (int t : kShipIdx)
        if (t != int(UnitType::Fighter) && hs.space[size_t(p)].n[t] && unitDef(g, p, UnitType(t)).capacity > cap)
            cap = unitDef(g, p, UnitType(t)).capacity, carrier = t;
    int gf = hs.space[size_t(p)].groundForces();
    for (int16_t x : hs.planets) gf += g.planets[size_t(x)].units[size_t(p)].groundForces();
    if (carrier < 0 || cap < 2 || gf < 2) return 0.f;
    auto freeWorth = [&](int x) {
        float v = 0.f;
        for (int16_t pl : g.systems[size_t(x)].planets) {
            const PlanetState& ps = g.planets[size_t(pl)];
            if (ps.owner >= 0 || ps.destroyed || (g.systems[size_t(x)].custodians && !g.custodiansRemoved) || neutralGroundOn(g, pl) > 0) continue;
            v += aiPlanetValue(g, p, pl);
        }
        return v;
    };
    float best = 0.f;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        const SystemState& ss = g.systems[s];
        if (int(s) == home || ss.offBoard || ss.isHyperlane() || !ss.space[size_t(p)].ships() || otherShipsIn(g, p, int(s))) continue;
        int ship = -1, cost = 1 << 20;  // the cheapest ship there goes home
        for (int t : kShipIdx)
            if (ss.space[size_t(p)].n[t] && !isFlagship(UnitType(t)) && unitCost2(g, p, UnitType(t)) < cost)
                cost = unitCost2(g, p, UnitType(t)), ship = t;
        if (ship < 0) continue;
        float v = ss.hasToken(p) ? 0.5f * freeWorth(int(s)) : freeWorth(int(s));  // (with our token: landed next round)
        for (int16_t a : ss.adj)
            if (!g.systems[size_t(a)].hasToken(p) && !otherShipsIn(g, p, a) && !g.systems[size_t(a)].offBoard) v += 0.6f * freeWorth(a);
        v *= 0.12f;
        if (v > best) {
            best = v;
            if (front) *front = int(s) * 16 + ship;
            if (back) *back = home * 16 + carrier;
        }
    }
    return std::min(best, 3.f);
}
float ravinPair(const Game& g, int p, int* front, int* back) {
    float best = 0.f;
    for (size_t f = 0; f < g.systems.size(); ++f) {
        const Force& ff = g.systems[f].space[size_t(p)];
        if (!ff.nonFighterShips() || threatAround(g, p, int(f)) == 0) continue;
        for (size_t b = 0; b < g.systems.size(); ++b) {
            if (b == f) continue;
            const Force& fb = g.systems[b].space[size_t(p)];
            if (!fb.nonFighterShips() || threatAround(g, p, int(b)) > 0) continue;
            for (int tw : kShipIdx) {
                if (!ff.n[tw] || tw == int(UnitType::Fighter) || tw == int(UnitType::Carrier)) continue;
                for (int ts : kShipIdx) {
                    if (!fb.n[ts] || ts == int(UnitType::Fighter) || ts == int(UnitType::Carrier)) continue;
                    const float gain = fightWorth(g, p, ts) - fightWorth(g, p, tw);
                    if (gain > best) {
                        best = gain;
                        if (front) *front = int(f) * 16 + tw;
                        if (back) *back = int(b) * 16 + ts;
                    }
                }
            }
        }
    }
    return best;
}
}  // namespace

// Vaults of the Heir (0.44.4): how much Mahact would miss an owned technology - its own worth, unit upgrades (the unit
// goes back), its colour while objectives want that colour or few of it are owned (prerequisites)
static float mahactKeep(const Game& g, int p, int t, const Goals& go) {
    const TechDef& d = kTechs[size_t(t)];
    float k = aiTechWant(g, p, t);
    if (d.unitUpgrade) k += 2.f + go.upgrade;
    if (!d.unitUpgrade && d.color != TechColor::None) {
        const int c = int(d.color);
        k += go.color[c] * 0.5f;
        if (techCount(g, p, d.color) <= 2) k += 1.f;
    }
    if (d.faction >= 0) k += 1.f;
    return k;
}

bool aiTeAnswer(const Game& g, const Decision& d, Answer& out) {
    const int p = d.player;
    const Player& me = g.players[size_t(p)];
    switch (Ab(d.ability)) {
        case Ab::Expedition: {
            int best = -1;
            float margin = 0.f;
            const float worth = sliceWorth(g, p);
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                const float m = worth - sliceCost(g, p, o.id) + aiCoreSliceBias(g, p, o.id);
                if (m > margin) margin = m, best = o.id;
            }
            out = one(has(d, best) ? best : -1);
            return true;
        }
        case Ab::ExpeditionPay: {
            if (d.prompt.find("action cards") != std::string::npos) {  // the 2 least-liked cards
                std::vector<std::pair<float, int>> v;
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0 && o.id < int(me.actionCards.size()))
                        v.push_back({aiCardKeepValue(me.actionCards[size_t(o.id)]), o.id});
                std::sort(v.begin(), v.end());
                Answer a;
                for (size_t i = 0; i < v.size() && int(a.picks.size()) < std::max(1, d.maxPick); ++i) a.picks.push_back(v[i].second);
                out = a;
                return true;
            }
            if (d.prompt.find("secret") != std::string::npos) {  // the one furthest from done
                out = one(bestBy(d, [&](int o) { return float(objectiveProgress(g, p, o).missing()); }));
                return true;
            }
            out = one(bestBy(d, [&](int x) { return -float(planetResources(g, x) + planetInfluence(g, x)); }));  // the poorest planet
            return true;
        }
        case Ab::TePlace: {  // near our home and our fleets, far from the leader's
            const std::vector<int> mine = fromHome(g, p);
            int leader = -1;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && (leader < 0 || g.players[size_t(q)].vp > g.players[size_t(leader)].vp)) leader = q;
            const std::vector<int> theirs = leader >= 0 ? fromHome(g, leader) : std::vector<int>(g.systems.size(), 99);
            out = one(bestBy(d, [&](int s) {
                float v = -float(mine[size_t(s)]) * 2.f + float(std::min(theirs[size_t(s)], 6)) * 0.6f;
                if (hasShips(g, p, s)) v += 2.f;
                for (int16_t a : g.systems[size_t(s)].adj)
                    if (hasShips(g, p, a)) v += 0.8f;
                return v;
            }));
            return true;
        }
        case Ab::TeInfantry:  // ourselves when tied, else the weakest player
            if (has(d, p)) {
                out = one(p);
            } else {
                out = one(bestBy(d, [&](int q) { return -float(g.players[size_t(q)].vp); }));
            }
            return true;
        case Ab::IngressPlace:
        case Ab::BtCabal: {  // ingress tokens open the Fracture: near us
            const std::vector<int> mine = fromHome(g, p);
            if (Ab(d.ability) == Ab::BtCabal && d.prompt.find("move an ingress") != std::string::npos) {
                out = one(bestBy(d, [&](int s) { return float(mine[size_t(s)]); }));  // the one furthest from us moves
                if (!has(d, out.picks[0])) out = one(-1);
                return true;
            }
            out = one(bestBy(d, [&](int s) { return -float(mine[size_t(s)]); }));
            return true;
        }
        case Ab::BtArborec: {  // Psychospore: the token where our ships could still want to move from
            out = one(bestBy(d, [&](int s) {
                float v = float(g.systems[size_t(s)].space[size_t(p)].nonFighterShips()) * 1.5f;
                for (int16_t a : g.systems[size_t(s)].adj)
                    if (otherShipsIn(g, p, a)) v += 1.f;
                return v;
            }));
            return true;
        }
        case Ab::BtMahact: {  // Vaults of the Heir: the technology we'd miss least
            const Goals& go = aiGoals(g, p);
            out = one(bestBy(d, [&](int t) { return t < 0 ? -50.f : -mahactKeep(g, p, t, go); }));
            return true;
        }
        case Ab::BtSaar: {
            if (d.prompt.find("how many") != std::string::npos) {  // ~1.5 dice per defending ground force, within spare resources
                int target = 0;
                for (const Option& o : d.options) target = std::max(target, o.id);
                int gf = 1;  // (0.44.4: Decision::value = the planet)
                if (d.value > 0 && d.value < int(g.planets.size())) {
                    gf = 0;
                    for (int q = 0; q < kSides; ++q)
                        if (q != p) gf += g.planets[size_t(d.value)].units[size_t(q)].groundForces();
                }
                const int free = std::max(1, aiFreeSpend(g, p, false));
                const int want = std::max(1, std::min({std::max(2, (gf * 3 + 1) / 2), free, target}));
                out = one(has(d, want) ? want : firstReal(d));
                return true;
            }
            out = one(bestBy(d, [&](int x) {  // valuable planets with few defenders
                int gf = 0;
                for (int q = 0; q < kSides; ++q)
                    if (q != p) gf += g.planets[size_t(x)].units[size_t(q)].groundForces();
                float v = float(planetResources(g, x) + planetInfluence(g, x)) - float(gf) * 0.7f;
                for (int16_t a : g.systems[size_t(g.planets[size_t(x)].system)].adj)
                    if (hasShips(g, p, a)) v += 1.f;  // we could follow it up
                return v;
            }));
            return true;
        }
        case Ab::BtKeleres: {  // 0.44.4: Custodia Vigilia toward a payment worth it (value = amount needed), not any small one
            const int cv = std::max(custodiaValue(g, p, d.prompt.find("influence") != std::string::npos), 1);
            const bool influence = d.prompt.find("influence") != std::string::npos;
            const int need = d.value;
            out = one(need >= cv - 1 || aiFreeSpend(g, p, influence) < need ? 1 : 0);
            return true;
        }
        case Ab::BtSardakk: {  // N'orr Supremacy: a good unit upgrade beats a token
            if (has(d, 1)) {  // (0.43.4: "research a unit upgrade" - the research step picks which)
                float best = 0.f;
                for (int t = 0; t < kTechCount; ++t)
                    if (kTechs[t].unitUpgrade && techAvailableToResearch(g, p, t) && canResearchNow(g, p, t))
                        best = std::max(best, aiTechWant(g, p, t));
                out = one(best > 2.5f ? 1 : 0);
                return true;
            }
            const int t = bestBy(d, [&](int id) { return id >= 1000 ? aiTechWant(g, p, id - 1000) : 2.5f; });
            out = one(has(d, t) ? t : 0);
            return true;
        }
        case Ab::BtNomad: {  // Thunder's Paradox: only to cash in Artuno's stored trade goods
            int pick = -1;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0 || o.id >= 10000) continue;  // (0.44.4: not other players' agents)
                const int b = o.id % 16;
                if (b < int(me.leaders.size()) && std::strstr(kLeaders[me.leaders[size_t(b)].leader].key, "nomadagentartuno") &&
                    me.agentTG >= 2)
                    pick = o.id;
            }
            out = one(pick);
            return true;
        }
        case Ab::BtJolnar: {  // Specialist Compounds: a cheap planet instead of 4 / 6 resources
            const int cost = d.value > 0 ? d.value : 4;  // (0.44.4: the cost comes in Decision::value)
            int best = -1;
            float bv = 0.f;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                const int x = o.id / 4, c = o.id % 4;
                float want = 0.f;  // the best technology of that colour we could get
                for (int t = 0; t < kTechCount; ++t)
                    if (kTechs[t].color == TechColor(c) && !kTechs[t].unitUpgrade && canResearchNow(g, p, t))
                        want = std::max(want, aiTechWant(g, p, t));
                const float v = want + float(cost) - float(planetResources(g, x)) * 1.2f;
                if (v > bv) bv = v, best = o.id;
            }
            out = one(has(d, best) ? best : -1);
            return true;
        }
        case Ab::BtNaalu: {  // Mindsieve: give a cheap note, keep the token when tokens are plentiful
            // (never Support for the Throne or an Alliance: that's a point / our commander for them)
            const int n = bestBy(d, [&](int id) {
                const int t = noteType(id);
                return t == kNoteSupportThrone || t == kNoteAlliance ? -50.f : -noteDefaultValue(g, id);
            });
            const float nv = has(d, n) ? noteDefaultValue(g, n) : 99.f;
            bool bad = !has(d, n) || noteType(n) == kNoteSupportThrone || noteType(n) == kNoteAlliance;
            // (the Naalu review, checkpoint 2 - test knob sievekeep, default off) the bots gave a note ~12 times a game, mostly
            // their own Trade Agreement (it costs their commodities at the next refresh) or Political Secret: keep their own
            // notes unless the strategy pool is empty. Tested (sievekeep=1): uses 12.1 -> 10.2 a game; 25 wins / 7.81 VP against
            // 22 / 7.46, fresh seeds 20 / 7.21 against 21 / 7.51 - no gain. OFF
            if (aiTune("sievekeep", 0.f) > 0.f && has(d, n) && noteOwner(n) == p && me.strategy > 0) bad = true;
            const bool give = !bad && (nv <= 1.f || (me.strategy <= 0 && nv <= 2.f));
            out = one(give ? n : -1);
            return true;
        }
        case Ab::BtMuaat: {
            if (d.options.size() == 2 && has(d, 1) && has(d, 0)) {  // carry Avernus along: only into a quiet system
                const int s = g.activeSystem;
                out = one(s >= 0 && !otherShipsIn(g, p, s) ? 1 : 0);
                return true;
            }
            if (d.prompt.find("place what") != std::string::npos) {  // a destroyer where enemies are near, else fighters
                bool threat = false;
                for (size_t s = 0; s < g.systems.size(); ++s)
                    if (g.systems[s].space[size_t(p)].count(UnitType::WarSun))
                        for (int16_t a : g.systems[s].adj) threat = threat || otherShipsIn(g, p, a);
                const int t = threat && has(d, int(UnitType::Destroyer)) ? int(UnitType::Destroyer) : firstReal(d);
                out = one(t);
                return true;
            }
            if (d.prompt.find("Avernus planet") != std::string::npos) {  // near home, away from enemy fleets
                const std::vector<int> mine = fromHome(g, p);
                out = one(bestBy(d, [&](int s) {
                    float v = -float(mine[size_t(s)]);
                    for (int16_t a : g.systems[size_t(s)].adj)
                        if (otherShipsIn(g, p, a)) v -= 1.5f;
                    return v;
                }));
                return true;
            }
            break;
        }
        case Ab::BtTitans: {
            // (0.44.9) asked to let the Titans place a sleeper on our planet: only for a friend (Awaken can put their PDS there)
            if (d.value < 0 && d.targetPlayer >= 0 && d.targetPlayer < g.numPlayers) {
                const int t = d.targetPlayer;
                out = one(attitudeScore(g, p, t) < 9.f && !hostileFlag(g, p, t) && g.players[size_t(t)].vp + 2 < g.vpGoal ? 1 : 0);
                return true;
            }
            if (d.prompt.find("ask to place a sleeper") != std::string::npos) {  // (we never ask - the bots don't use it)
                out = one(-1);
                return true;
            }
            // coexist: two more action cards a round beats a fight on our own sleeper planet - unless we'd clearly
            int mine = 0, theirs = 0;  // win a planet worth having (0.44.4)
            if (d.value > 0 && d.value < int(g.planets.size())) {
                const PlanetState& ps = g.planets[size_t(d.value)];
                mine = ps.units[size_t(p)].groundForces();
                for (int q = 0; q < kSides; ++q)
                    if (q != p) theirs += ps.units[size_t(q)].groundForces();
            }
            const bool sure = mine >= 2 * theirs + 1 && mine >= 2;
            out = one(sure && d.value > 0 && planetResources(g, d.value) + planetInfluence(g, d.value) >= 3 ? 0 : 1);
            return true;
        }
        case Ab::BtNaaz:
            out = one(firstReal(d));
            return true;
        case Ab::BtEmpyrean: {  // tether the border toward the strongest enemy fleet
            auto sideV = [&](int s) {
                float v = 0.f;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p) {
                        v += float(g.systems[size_t(s)].space[size_t(q)].nonFighterShips());
                        for (int16_t a : g.systems[size_t(s)].adj) v += 0.5f * float(g.systems[size_t(a)].space[size_t(q)].nonFighterShips());
                    }
                return v;
            };
            auto tetherV = [&](size_t i) { return std::max(sideV(g.tethers[i].first), sideV(g.tethers[i].second)); };
            // (fixes 26, the Empyrean review) "let them cross / fire across your tether?" - bots never allow it (as before)
            if (d.prompt.find("Void Tether: let ") == 0) {
                out = one(0);
                return true;
            }
            // (fixes 26) which token: a new one while one is left, else the one on the poorer border
            if (d.prompt.find("place a new tether or move one") != std::string::npos) {
                int pickId = -1;
                float lo = 1e9f;
                for (const Option& o : d.options) {
                    if (o.id == 999) {
                        pickId = 999;
                        break;
                    }
                    const size_t i = size_t(o.id - 1000);
                    if (o.id >= 1000 && i < g.tethers.size() && tetherV(i) < lo) lo = tetherV(i), pickId = o.id;
                }
                out = one(pickId >= 0 ? pickId : firstReal(d));
                return true;
            }
            int best = -1;
            float bv = 0.5f;
            if (g.tethers.size() >= 2) {  // 0.44.4: moving the older tether - only for a better border
                float lo = tetherV(0);    // (fixes 26: the Empyrean choose which - the poorer one)
                if (g.fixes >= 26) lo = std::min(lo, tetherV(1));
                bv = std::max(bv, lo + 0.5f);
            }
            // (the Empyrean review, checkpoint 2 - knob tetherhome, ON - owner 2026-11-12 23:50) the guide: "Tether borders that lead into
            // your home or your scoring systems" - a border with our home (or our planets) on one side counts more.
            // Tested (90 TE games each, wins / VP): 18 / 7.07 -> 22 / 7.30, fresh seeds 11 / 7.11 -> 11 / 7.18 (with
            // bloodpact: 13 / 7.36); tethers placed 5.4 -> 7.8 a game
            const float home = aiTune("tetherhome", 1.f);
            auto ours = [&](int s) {
                if (s == g.players[size_t(p)].homeSystem) return 2.f;
                for (int16_t x : g.systems[size_t(s)].planets)
                    if (g.planets[size_t(x)].owner == p) return 0.8f;
                return 0.f;
            };
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                float v = sideV(o.id);
                if (home > 0.f && g.activeSystem >= 0 && v > 0.f) v += home * std::max(ours(g.activeSystem), ours(o.id));
                if (v > bv) bv = v, best = o.id;
            }
            out = one(best);
            return true;
        }
        case Ab::CrimsonAgent: {  // Ahk Ravin: we swap our own ships (see ravinPair)
            // (0.59.0) cargo: take along only what would be stranded (removed) once the ship has left - except a ship
            // leaving the home beside the board (ravinExpand): it takes every ground force it can
            if (d.prompt.find("take along") != std::string::npos || d.prompt.find("go along") != std::string::npos) {
                const int sys = d.value / 16, type = d.value % 16;
                if (isFaction(g, p, "crimson") && sys == g.players[size_t(p)].homeSystem && aiTune("ravinexp", 1.f) > 0.f) {
                    if (d.prompt.find("take along") != std::string::npos) {
                        int pickId = -1;
                        for (const Option& o : d.options)  // ground forces: from space first, then the planets
                            if (o.enabled && o.id >= 0 && (o.id % 16 == int(UnitType::Infantry) || o.id % 16 == int(UnitType::Mech)) &&
                                (pickId < 0 || (o.id < 16 && pickId >= 16)))
                                pickId = o.id;
                        out = one(pickId);
                    } else {
                        int most = 0;
                        for (const Option& o : d.options) most = std::max(most, o.id);
                        out = one(most);
                    }
                    return true;
                }
                int over = 0;
                if (sys >= 0 && sys < int(g.systems.size())) {
                    Force f = g.systems[size_t(sys)].space[size_t(p)];
                    f.remove(UnitType(type), 1, false);
                    over = overflowOf(g, p, sys, f);
                }
                if (d.prompt.find("take along") != std::string::npos) {
                    int pickId = -1;
                    for (int t : {int(UnitType::Infantry), int(UnitType::Mech), int(UnitType::Fighter)})
                        if (pickId < 0 && over > 0 && has(d, t)) pickId = t;  // (from the space area: ids below 16)
                    out = one(pickId);
                } else {
                    int k = -1, most = -1;
                    for (const Option& o : d.options) {
                        most = std::max(most, o.id);
                        if (o.id >= over && (k < 0 || o.id < k)) k = o.id;
                    }
                    out = one(k >= 0 ? k : most);
                }
                return true;
            }
            int front = -1, back = -1;
            const float war = ravinPair(g, p, &front, &back);
            int ef = -1, eb = -1;  // (0.59.0) or the ferry, when it is worth more
            if (ravinExpand(g, p, &ef, &eb) > (war >= 5.f ? 0.7f : 0.f)) front = ef, back = eb;
            if (d.prompt.find("which player") != std::string::npos) out = one(has(d, p) ? p : firstReal(d));
            else if (d.prompt.find("first ship") != std::string::npos) out = one(has(d, front) ? front : firstReal(d));
            else out = one(has(d, back) ? back : firstReal(d));
            return true;
        }
        case Ab::Sever: {  // the system an enemy fleet could reach through its wormholes
            int best = -1, bv = 0;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                const uint8_t w = wormholesOf(g, o.id);
                int n = 0;
                for (size_t e = 0; e < g.systems.size(); ++e)
                    if (int(e) != o.id && (wormholesOf(g, int(e)) & w))
                        for (int q = 0; q < g.numPlayers; ++q)
                            if (q != p) n += g.systems[e].space[size_t(q)].nonFighterShips();
                if (n > bv) bv = n, best = o.id;
            }
            out = one(best);
            return true;
        }
        case Ab::BtArgent: {  // Wing Transfer (0.43.0 debug passes, owner T33: "have them use it")
            // enemy warships in or next to a system
            auto threat = [&](int s) {
                int n = 0;
                std::vector<int> zone{s};
                for (int16_t a : g.systems[size_t(s)].adj) zone.push_back(a);
                for (int z : zone)
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p) n += g.systems[size_t(z)].space[size_t(q)].nonFighterShips();
                return n;
            };
            const int act = g.activeSystem;
            out = Answer();
            if (d.prompt.find("move a ship") != std::string::npos) {
                // spare warships from the active system out to a threatened neighbour that holds fewer than the threat
                int best = -1;
                float bv = 0.f;
                for (const Option& o : d.options) {
                    if (!o.enabled || o.id < 0) continue;
                    const int a = o.id / 100000, b = (o.id % 100000) / 16, t = o.id % 16;
                    if (a != act || t == int(UnitType::Fighter) || t == int(UnitType::Carrier) || t == int(UnitType::WarSun)) continue;
                    const int here = g.systems[size_t(a)].space[size_t(p)].nonFighterShips();
                    const int there = g.systems[size_t(b)].space[size_t(p)].nonFighterShips();
                    const float v = float(threat(b) - there) - float(std::max(0, threat(a) + 1 - (here - 1)));
                    if (v > bv) bv = v, best = o.id;
                }
                out.picks.push_back(best);
                return true;
            }
            // the tokens: neighbours more exposed than the active system (they're ours alone; we won't need to
            // activate them to defend them)
            // (0.44.4: never where our own ships wait - a token there would hold them back from this move)
            if (act >= 0 && g.systems[size_t(act)].space[size_t(p)].nonFighterShips() >= 2)
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0 && threat(o.id) > threat(act) && g.systems[size_t(o.id)].space[size_t(p)].nonFighterShips() == 0 &&
                        int(out.picks.size()) < std::min(2, d.maxPick))
                        out.picks.push_back(o.id);
            if (out.picks.empty() && d.minPick > 0) out.picks.push_back(firstReal(d));
            return true;
        }
        // ---- Thunder's Edge factions (0.35.0)
        case Ab::Galvanize: {  // the unit that rolls best: big ships first, then mechs, then the rest
            static const float kRank[kUnitTypes] = {3, 4, 2, 5, 1, 6, 1.5f, 2.5f, 0, 0, 0, 6};  // carrier .. space dock order below
            (void)kRank;
            out = one(bestBy(d, [&](int id) {
                const int t = id % 16;
                const UnitDef& u = unitDef(g, d.targetPlayer >= 0 ? d.targetPlayer : p, UnitType(t));
                // (fixes 27, the Last Bastion review: Dame Briar may galvanize a PDS - its space cannon die, a little less)
                if (isStructure(UnitType(t))) return u.cannon > 0 ? float(11 - u.cannon) - 2.f : -20.f;
                return float(11 - std::max(1, int(u.combat))) + (UnitType(t) == UnitType::Fighter ? -3.f : 0.f);
            }));
            return true;
        }
        case Ab::PhoenixStandard:
        case Ab::RaiseStandard:
        case Ab::Hyperion:
            out = one(1);
            return true;
        case Ab::BastionAgent: {  // our own units, or a friend's against our enemies
            const int q = d.targetPlayer;
            out = one(q < 0 || q == p ? 1 : 0);
            return true;
        }
        case Ab::BastionHero: {  // worth it against a crowd
            int foes = 0;
            const int sys = g.activeSystem;
            if (sys >= 0)
                for (int q = 0; q < kSides; ++q)
                    if (q != p) {
                        foes += g.systems[size_t(sys)].space[size_t(q)].total();
                        for (int16_t x : g.systems[size_t(sys)].planets) foes += g.planets[size_t(x)].units[size_t(q)].total();
                    }
            out = one(foes >= 3 ? 1 : 0);
            return true;
        }
        case Ab::Proxima: {  // (0.44.8) with hit points to spare, or when our galvanized units soak up the backlash
            const int x = g.groundCombatPlanet;
            int mine = 0, theirs = 0, galv = 0;
            if (x >= 0)
                for (int q = 0; q < kSides; ++q) {
                    const Force& f = g.planets[size_t(x)].units[size_t(q)];
                    // hit points: mechs (Sustain Damage) count twice
                    const int hp = groundForcesOn(g, q, x) + f.count(UnitType::Mech) - f.dmg[int(UnitType::Mech)];
                    (q == p ? mine : theirs) += hp;
                    if (q == p)
                        for (int t = 0; t < kUnitTypes; ++t) galv += f.galv[t];
                }
            // expected backlash hits after the cancel: 3 dice at 8 = 0.9; 1 cancelled -> 0.24, 2+ -> 0.03
            const float self = g.fixes < 12 || galv == 0 ? 0.9f : galv == 1 ? 0.24f : 0.03f;
            out = one(theirs > 0 && (mine >= theirs + 2 || (self < 0.5f && mine >= 2)) ? 1 : 0);
            return true;
        }
        case Ab::Nano1: {  // (0.44.8) the PDS where it shields or fires best - not on a home that already has some
            int best = -1;
            float bv = -1e9f;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0 || o.id >= int(g.planets.size())) continue;
                const PlanetState& ps = g.planets[size_t(o.id)];
                const int sys = ps.system;
                float v = 0.3f * float(planetResources(g, o.id) + planetInfluence(g, o.id));
                const int have = ps.units[size_t(p)].count(UnitType::PDS);
                if (have == 0) v += 2.f;  // Planetary Shield
                if (isHomePlanet(g, o.id)) v -= have > 0 ? 3.f : 1.f;
                int foes = 0;
                for (int16_t a : g.systems[size_t(sys)].adj)
                    if (otherShipsIn(g, p, a)) ++foes;
                if (otherShipsIn(g, p, sys)) ++foes;
                v += 1.2f * float(std::min(foes, 3));
                if (std::strcmp(kPlanets[ps.def].key, "mr") == 0) v += 1.5f;
                if (v > bv) bv = v, best = o.id;
            }
            out = one(best >= 0 ? best : firstReal(d));
            return true;
        }
        case Ab::BtBastion: {  // The Icon: to the front
            int best = -1;
            float bv = 0.5f;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                float v = 0.f;
                for (int16_t a : g.systems[size_t(o.id)].adj)
                    if (otherShipsIn(g, p, a)) v += 1.f;
                if (v > bv) bv = v, best = o.id;
            }
            out = one(best);
            return true;
        }
        case Ab::SurvivalInstinct: {  // the hardest-hitting ship, unless its own system is under threat
            int best = -1;
            float bv = 0.f;
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                const int s = o.id / 16, t = o.id % 16;
                if (UnitType(t) == UnitType::Fighter) continue;
                bool threat = false;
                for (int16_t a : g.systems[size_t(s)].adj)
                    if (a != g.activeSystem && otherShipsIn(g, p, a)) threat = true;
                if (threat && g.systems[size_t(s)].space[size_t(p)].nonFighterShips() <= 1) continue;
                const float v = float(11 - std::max(1, int(unitDef(g, p, UnitType(t)).combat))) * float(std::max(1, int(unitDef(g, p, UnitType(t)).combatDice)));
                if (v > bv) bv = v, best = o.id;
            }
            out = one(best);
            return true;
        }
        case Ab::LastDispatch:
            out = one(bestBy(d, [&](int t) { return float(unitDef(g, p, UnitType(t)).cost2); }));
            return true;
        case Ab::RalNelCommander:
            // (the Ral Nel review, checkpoint 2 - switch rnojz, test: never used, 1.5 retreats announced a game): the 2 dearest
            // ships leave at once (fixes 27: before the round's rolls), to the quietest neighbour. ON - owner 2026-11-14 ("Yea
            // let's turn watchful ojz"; 180 games 35 vs 32 wins); rnojz=0 turns it off
            if (aiTune("rnojz", 1.f) > 0.f) {
                if (d.prompt.find("which ship") != std::string::npos) {
                    out = one(bestBy(d, [&](int t) { return t < 0 ? -1.f : float(unitDef(g, p, UnitType(t)).cost2); }));
                    return true;
                }
                out = one(bestBy(d, [&](int s) {
                    if (s < 0 || s >= int(g.systems.size())) return -1e9f;
                    float v = hasUnits(g, p, s) ? 1.f : 0.f;
                    for (int16_t a : g.systems[size_t(s)].adj)
                        for (int q = 0; q < g.numPlayers; ++q)
                            if (q != p) v -= 0.5f * float(g.systems[size_t(a)].space[size_t(q)].nonFighterShips());
                    return v;
                }));
                return true;
            }
            out = one(-1);
            return true;
        case Ab::RalNelHero:
            out = one(1);
            return true;
        case Ab::BtRalNel:
            out = one(bestBy(d, [&](int c) { return aiCardKeepValue(c); }));
            return true;
        case Ab::RalNelAgent: {
            if (d.prompt.find("to whom") != std::string::npos) {  // the player furthest behind
                out = one(bestBy(d, [&](int q) { return -float(g.players[size_t(q)].vp) + (q == p ? -99.f : 0.f); }));
                return true;
            }
            out = one(bestBy(d, [&](int c) { return -aiCardKeepValue(c); }));  // give the weaker card
            return true;
        }
        case Ab::Nano3:
            out = one(bestBy(d, [&](int c) { return -aiCardKeepValue(c); }));
            return true;
        case Ab::ResearchTeam:
        case Ab::FirmamentMech: {  // coexist unless we clearly win the fight on that planet
            int x = g.groundCombatPlanet;
            const int other = d.targetPlayer;
            if (x < 0 && g.activeSystem >= 0)
                for (int16_t y : g.systems[size_t(g.activeSystem)].planets)
                    if (other >= 0 && groundForcesOn(g, other, y) > 0 && groundForcesOn(g, p, y) > 0) x = y;
            int mine = 0, theirs = 0;
            if (x >= 0 && other >= 0) {
                mine = groundForcesOn(g, p, x);
                theirs = groundForcesOn(g, other, x);
            }
            if (fquirkIs(g, p, "deepwrought", 0)) out = one(mine >= theirs * 3 + 3 ? 0 : 1);  // Visiting Scholar
            else if (Ab(d.ability) == Ab::FirmamentMech && p == g.activePlayer && aiTune("viperinv", 1.f) > 0.f) {
                // (0.59.1, owner's faction debug pass) the Firmament invading came to take the planet: fight with the
                // odds on our side - coexist when we'd likely lose, or when the coup is near (Viper Hollow takes it then)
                const Player& me2 = g.players[size_t(p)];
                const bool coupSoon = isFaction(g, p, "firmament") && (plotCount(me2) >= 2 || (plotCount(me2) >= 1 && g.round >= 3));
                out = one(coupSoon ? (mine >= theirs * 2 + 2 ? 0 : 1) : (mine >= theirs + 1 ? 0 : 1));
            } else out = one(mine >= theirs * 2 + 2 ? 0 : 1);
            return true;
        }
        case Ab::DeepwroughtAgent: {
            if (d.prompt.find("which of") != std::string::npos) {  // the richest planet to sit beside
                out = one(bestBy(d, [&](int x) { return float(planetResources(g, x) + planetInfluence(g, x)); }));
                return true;
            }
            if (d.prompt.find("Doctor Carrina: ignore") == 0) {  // (fixes 27) we research: keep the planet unless they lead by 2
                const int dw = d.targetPlayer;
                out = one(dw >= 0 && g.players[size_t(dw)].vp >= me.vp + 2 ? 0 : 1);
                return true;
            }
            const int q = d.targetPlayer;  // help anyone but the leader
            int lead = 0;
            for (int r = 0; r < g.numPlayers; ++r) lead = std::max<int>(lead, g.players[size_t(r)].vp);
            // (the Deepwrought review, checkpoint 2 - switch dwcarrina, test: with tied scores nobody is "below the leader", so
            // she was never lent in the first rounds - helping gives us an infantry, coexisting, and an ocean card): help
            // anyone who isn't ahead of us
            if (aiTune("dwcarrina", 0.f) > 0.f) {
                out = one(q >= 0 && g.players[size_t(q)].vp <= me.vp ? 1 : 0);
                return true;
            }
            out = one(q >= 0 && g.players[size_t(q)].vp < lead ? 1 : 0);
            return true;
        }
        case Ab::DeepwroughtCommander:
        case Ab::Eanautic:
            out = one(1);
            return true;
        case Ab::Radical: {
            int best = -1;
            float bv = fquirkIs(g, p, "deepwrought", 1) ? -0.5f : 0.5f;  // Library Card: climb every round
            for (const Option& o : d.options) {
                if (!o.enabled || o.id < 0) continue;
                float gain = aiTechWant(g, p, o.id % 1000) - aiTechWant(g, p, o.id / 1000);
                if (!std::strcmp(kTechs[o.id / 1000].key, "radical")) gain -= 3.f;  // (0.44.9: it climbs again next round)
                if (gain > bv) bv = gain, best = o.id;
            }
            out = one(best);
            return true;
        }
        case Ab::DeepwroughtHero: {  // a technology the others lean on and we don't
            out = one(bestBy(d, [&](int t) {
                float v = 0.f;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (g.players[size_t(q)].hasTech(t)) v += q == p ? -3.f : 1.f;
                return v;
            }));
            return true;
        }
        case Ab::ShareKnowledge:
            out = one(bestBy(d, [&](int t) { return aiTechWant(g, p, t); }));
            return true;
        case Ab::BtDeepwrought: {
            if (d.options.size() == 2 && has(d, 1) && has(d, 0)) {  // buy a technology for 3 trade goods and a note
                float want = 0.f;
                for (int t = 0; t < kTechCount; ++t)
                    if (kTechs[t].faction < 0 && !(g.fixes >= 8 && kTechs[t].unitUpgrade) && canResearchNow(g, p, t))
                        want = std::max(want, aiTechWant(g, p, t));
                out = one(me.tradeGoods >= 3 + aiGoals(g, p).keepTg + 1 && want >= 5.f ? 1 : 0);
                return true;
            }
            out = one(bestBy(d, [&](int n) { return -noteDefaultValue(g, n) - (noteType(n) == kNoteSupportThrone ? 50.f : 0.f); }));
            return true;
        }
        case Ab::Incursion:  // open breaches, never close our own
            out = one(d.prompt.find(": open") != std::string::npos ? 1 : 0);
            return true;
        case Ab::Exile:
            if (fquirkIs(g, p, "crimson", 1)) out = one(has(d, 1) ? 1 : 0);  // Quiet Breaches: inactive
            else out = one(has(d, 2) ? 2 : has(d, 1) ? 1 : 0);
            return true;
        case Ab::MoveBreach: {  // (0.59.0) the inactive breach we need least: not the Sorrow's, away from our ships
            out = one(bestBy(d, [&](int s) {
                if (s < 0 || s >= int(g.systems.size())) return -1e9f;
                const SystemState& ss = g.systems[size_t(s)];
                float v = 0.f;
                if (!std::strcmp(ss.d().key, "94")) v -= 100.f;  // the Sorrow - the way home
                if (hasUnits(g, p, s)) v -= 20.f;
                int near = 99;
                for (size_t t = 0; t < g.systems.size(); ++t)
                    if (g.systems[t].space[size_t(p)].ships() > 0 && !g.systems[t].offBoard && !ss.offBoard)
                        near = std::min(near, hexDistance(ss.pos, g.systems[t].pos));
                return v + float(near);
            }));
            return true;
        }
        case Ab::RemoveBreach: {  // the Crimson keep theirs (Quiet Breaches: closed); everyone else closes them
            if (!isFaction(g, p, "crimson")) {
                out = one(1);
                return true;
            }
            bool keep = !fquirkIs(g, p, "crimson", 1);
            // (0.59.0, owner: tune Quiet Breaches) it still closes its breaches - but keeps the Sorrow's (the way home),
            // the one its Quietus sits in, and one where a Revenant can take a planet on its next turn
            const int s = d.value;
            if (!keep && s >= 0 && s < int(g.systems.size())) {
                const SystemState& sy = g.systems[size_t(s)];
                if (!std::strcmp(sy.d().key, "94")) keep = true;
                if (sy.space[size_t(p)].count(UnitType::Flagship)) keep = true;
                if (mechAbilities(g) && unitsAvailable(g, p, UnitType::Mech) > 0)
                    for (int16_t x : sy.planets)
                        if (g.planets[size_t(x)].owner != p) keep = true;
            }
            out = one(keep ? 0 : 1);
            return true;
        }
        case Ab::Subatomic: {  // (0.59.0: fighters ask how many - as before, all of them or none)
            int most = 0;
            for (const Option& o : d.options) most = std::max(most, o.id);
            out = one(aiFreeSpend(g, p, false) >= 4 ? most : 0);
            return true;
        }
        case Ab::CrimsonHero: {  // spring the phantom fleet in a fight; (0.55.0: never used before) hide new ships on the
            // card from round 3 while home is calm - a reserve that joins any space combat of ours at once
            if (d.prompt.find("purge") != std::string::npos) {
                out = one(1);
                return true;
            }
            if (d.prompt.find("how many") != std::string::npos) {  // (0.59.0: "Some of them" - bots answer all or none)
                int most = 0;
                for (const Option& o : d.options) most = std::max(most, o.id);
                out = one(most);
                return true;
            }
            const int home = g.players[size_t(p)].homeSystem;
            bool calm = home >= 0;
            if (home >= 0)
                for (int s2 : systemsWithin(g, home, 1))
                    if (otherShipsIn(g, p, s2)) calm = false;
            out = one(g.round >= 3 && calm && aiTune("phantom", 1.f) > 0.f && g.players[size_t(p)].phantom.nonFighterShips() < 6 ? 1 : 0);
            return true;
        }
        case Ab::Revenant: {  // (fixes 24: offered for our own planets too - bots still only come to take one)
            bool take = g.fixes < 24;
            if (!take && g.activeSystem >= 0)
                for (int16_t x : g.systems[size_t(g.activeSystem)].planets)
                    if (g.planets[size_t(x)].owner != p) take = true;
            out = one(take ? 1 : 0);
            return true;
        }
        case Ab::BtCrimson: {  // open an inactive breach, else a new active one next to enemies
            out = one(bestBy(d, [&](int c) {
                if (c < 1000) return g.systems[size_t(c)].breach == 1 ? 2.f : -5.f;
                float v = 0.f;
                for (int16_t a : g.systems[size_t(c - 1000)].adj)
                    for (int16_t x : g.systems[size_t(a)].planets)
                        if (g.planets[size_t(x)].owner >= 0 && g.planets[size_t(x)].owner != p) v += 0.5f;
                return v;
            }));
            return true;
        }
        // ---- the Firmament / the Obsidian
        case Ab::PlotsWithin:
        case Ab::BlackOps:
        case Ab::Enervate:
            out = one(1);
            return true;
        case Ab::PlotCard: {  // the plot that fits the victim (see the strategy guide)
            const int q = d.targetPlayer;
            out = one(bestBy(d, [&](int k) {
                if (q < 0 || q >= g.numPlayers) return float(k == kPlotAssail);
                const Player& v = g.players[size_t(q)];
                const bool nb = areNeighbours(g, p, q);
                switch (k) {
                    case kPlotSiphon: return float(kFactions[v.faction].commodities) * 0.7f;
                    case kPlotExtract: {
                        int lack = 0;
                        for (int16_t t : v.techs)
                            if (kTechs[t].faction < 0 && !me.hasTech(t)) ++lack;
                        return 0.5f + float(lack) * 0.5f;
                    }
                    case kPlotEnervate: return 2.0f;
                    case kPlotSeethe: {
                        float best = 0.f;
                        for (int x : controlledPlanets(g, q))
                            if (!isHomePlanet(g, x)) best = std::max(best, aiPlanetValue(g, p, x));
                        return (nb ? 0.6f : 0.3f) * best;
                    }
                    case kPlotAssail: return nb ? 2.4f : 0.8f;
                    default: return 0.f;
                }
            }));
            return true;
        }
        case Ab::FirmamentAgent:  // Myru Vos: our own fleet under space cannon, or as promised (0.43.0: not just for the way through)
            if (g.fixes >= 25 && g.activePlayer != p) {  // (fixes 25: another player's move - only as promised; a seat a person
                out = one(g.agentFor[size_t(p)] == g.activePlayer ? 1 : 0);  // plays, answered by a bot in tests: no)
                return true;
            }
            out = one(d.value == 1 || g.fixes < 2 || g.activePlayer != p ? 1 : 0);
            return true;
        case Ab::FirmamentHero: {  // the leader (the neighbour leader first)
            out = one(bestBy(d, [&](int q) { return float(g.players[size_t(q)].vp) + (areNeighbours(g, p, q) ? 1.5f : 0.f); }));
            return true;
        }
        case Ab::PlotToken: {  // the leader's token, on Assail or Siphon best
            out = one(bestBy(d, [&](int c) {
                const int k = c / 8, q = c % 8;
                return float(g.players[size_t(q)].vp) + (k == kPlotAssail && areNeighbours(g, p, q) ? 2.f : 0.f) + (k == kPlotSiphon ? 1.f : 0.f);
            }));
            return true;
        }
        case Ab::NeuralParasite:
        case Ab::Parasite2:
            out = one(bestBy(d, [&](int c) {
                if (Ab(d.ability) == Ab::NeuralParasite) return 1.f;
                const int pm = g.fixes >= 25 ? 16 : 8, sm = g.fixes >= 25 ? 4096 : 1024;  // (fixes 25: room for the neutral side)
                const int q = c % pm, planet = c / sm - 1;
                if (planet < 0) return 0.5f;
                // (a neutral guard: a planet opened up for us - worth it when it is the last one there)
                const float who = q == kNeutral ? 0.8f : 1.f;
                return ((groundForcesOn(g, q, planet) == 1 ? 3.f : 1.f) + aiPlanetValue(g, p, planet) * 0.1f) * who;
            }));
            return true;
        case Ab::Sowing: {  // bank when a coup is coming (2 for 1 on The Reaping); keep 2 to spend
            int want = 0;
            if (plotCount(me) >= 1 || g.round <= 2) want = std::max(0, std::min(3, int(me.tradeGoods) - 2));
            if (firmQuirk(g, p, 1)) want = std::min(3, int(me.tradeGoods));  // Coup Clock
            out = one(bestBy(d, [&](int k) { return -std::abs(float(k - want)); }));
            return true;
        }
        case Ab::Seethe: {
            const int q = d.targetPlayer;
            out = one(bestBy(d, [&](int c) {
                if (d.onPlanets || c >= 1000) {  // the reveal: every unit on the planet
                    float v = aiPlanetValue(g, p, c);
                    for (int s = 0; s < kSides; ++s)
                        if (s != p) v += float(g.planets[size_t(c)].units[size_t(s)].total()) * 0.6f;
                    return v - float(g.planets[size_t(c)].units[size_t(p)].total()) * 1.5f;
                }
                if (c <= 0) return 0.2f;  // an infantry in space
                const int x = c - 1;
                return q >= 0 && groundForcesOn(g, q, x) == 1 ? 2.f + aiPlanetValue(g, p, x) * 0.1f : 1.f;
            }, true));
            return true;
        }
        case Ab::Extract:
            if (d.options.size() == 2 && has(d, 1) && has(d, 0)) {  // pay 4 resources for the puppet's new technology
                out = one(aiFreeSpend(g, p, false) >= 6 ? 1 : 0);
                return true;
            }
            out = one(bestBy(d, [&](int t) { return aiTechWant(g, p, t); }));
            return true;
        case Ab::ObsidianAgent: {  // Vos Hollow: never against ourselves or a friend
            const int q = d.targetPlayer;
            out = one(q >= 0 && q != p && !promisedNoAttack(g, p, q) ? 1 : 0);
            return true;
        }
        case Ab::Malevolency: {  // pass it on - the Obsidian to whoever leads, the others to the Obsidian's best foe
            if (aiTune("malevrival", 0.f) > 0.f && !isFaction(g, p, "obsidian")) {  // (test knob: a rival, not back to the Obsidian)
                out = one(bestBy(d, [&](int q) { return float(g.players[size_t(q)].vp) - (isFaction(g, q, "obsidian") ? 3.f : 0.f); }));
                return true;
            }
            out = one(bestBy(d, [&](int q) { return float(g.players[size_t(q)].vp) + (isFaction(g, q, "obsidian") ? 0.5f : 0.f); }));
            return true;
        }
        case Ab::Planesplitter: {
            if (has(d, -1)) {  // which token: none if we can already reach one
                bool reach = false;
                for (size_t s = 0; s < g.systems.size(); ++s)
                    if (g.systems[s].ingress) {
                        if (hasShips(g, p, int(s))) reach = true;
                        for (int16_t a : g.systems[s].adj) reach = reach || hasShips(g, p, a);
                    }
                if (reach) {
                    out = one(-1);
                    return true;
                }
                const std::vector<int> dist = fromHome(g, p);
                out = one(bestBy(d, [&](int s) { return float(dist[size_t(s)]); }));
                return true;
            }
            out = one(bestBy(d, [&](int s) { return float(g.systems[size_t(s)].space[size_t(p)].nonFighterShips()); }));
            return true;
        }
        // ---- galactic events (0.36.0)
        case Ab::EvMobilize:
            if (d.onPlanets) {  // the space dock: the richest planet away from home
                out = one(bestBy(d, [&](int x) { return float(planetResources(g, x)) + (isHomePlanet(g, x) ? -5.f : 0.f); }));
                return true;
            }
            out = one(bestBy(d, [&](int t) { return aiTechWant(g, p, t); }));
            return true;
        case Ab::EvWildsTech:
            out = one(bestBy(d, [&](int t) { return aiTechWant(g, p, t); }));
            return true;
        case Ab::ScarTech: {  // entropic scar (0.43.4): a faction technology for a strategy token, if it's worth one
            int best = -1;
            float bv = 2.f;
            for (const Option& o : d.options)
                if (o.id >= 0 && aiTechWant(g, p, o.id) > bv) {
                    bv = aiTechWant(g, p, o.id);
                    best = o.id;
                }
            out = one(best);
            return true;
        }
        case Ab::EvSpoils:
            out = one(1);
            return true;
        case Ab::EvMonument:
            if (d.prompt.find("destroy") != std::string::npos) out = one(0);  // its points are ours now
            else out = one(g.round <= 6 && me.tradeGoods >= 5 ? 1 : 0);
            return true;
        case Ab::EvAtomics:
        case Ab::EvX89: {  // the leader's best planet with the most on it - never our own
            int lead = -1;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && (lead < 0 || g.players[size_t(q)].vp > g.players[size_t(lead)].vp)) lead = q;
            out = one(bestBy(d, [&](int x) {
                const PlanetState& ps = g.planets[size_t(x)];
                if (ps.owner == p) return -50.f;
                float v = aiPlanetValue(g, p, x) * 0.3f;
                for (int s = 0; s < kSides; ++s)
                    if (s != p) v += float(ps.units[size_t(s)].groundForces() + ps.units[size_t(s)].structures() * 2);
                v -= float(ps.units[size_t(p)].total()) * 3.f;
                if (ps.owner == lead) v += 2.f;
                return v;
            }));
            return true;
        }
        case Ab::EvExplore:
            out = one(firstReal(d));
            return true;
        case Ab::BtYssaril: {
            if (d.prompt.compare(0, 14, "Ask the Yssari") == 0) {  // (0.44.9: borrowing Scheming - always worth asking)
                out = one(1);
                return true;
            }
            if (has(d, 1) && has(d, 0) && (d.options.size() == 2 || has(d, 3))) {  // the Yssaril: share with anyone not ahead of us
                const int q = d.targetPlayer;
                // (the Yssaril review, checkpoint 2 - test knob gloomshare, OFF) 1: only players 2+ VP behind or Friendly; 2:
                // nobody (the shared Scheming came up 1662 times in 90 games - the other players' card filter). Tested (90
                // games): gloomshare=1 11 wins / 7.01 VP against 18 / 7.07 (shared 1351 times); gloomshare=2 15 / 7.31 - no gain
                const float gs = aiTune("gloomshare", 0.f);
                if (gs > 0.f && q >= 0 && q < g.numPlayers) {
                    const bool share = gs < 1.5f && (g.players[size_t(q)].vp + 2 <= me.vp || attitudeOf(g, p, q) == Attitude::Friendly);
                    out = one(share ? 1 : 0);
                    return true;
                }
                out = one(q >= 0 && q < g.numPlayers && g.players[size_t(q)].vp <= me.vp ? 1 : 0);
                return true;
            }
            out = one(bestBy(d, [&](int c) { return -aiCardKeepValue(c); }));  // the card we like least
            return true;
        }
        default: return false;
    }
    return false;
}

// Breakthrough component actions (6000 + Ab): about what the plan would gain.
// resources + influence of p's EXHAUSTED home planets - what The Blade's Orchestra would ready (the Firmament's coup timing)
int aiHomeSpentValue(const Game& g, int p) {
    const int home = g.players[size_t(p)].homeSystem;
    if (home < 0) return 0;
    int v = 0;
    for (int16_t x : g.systems[size_t(home)].planets) {
        const PlanetState& ps = g.planets[size_t(x)];
        if (ps.owner == p && ps.exhausted) v += planetResources(g, int(x)) + planetInfluence(g, int(x));
    }
    return v;
}

// (the Firmament bot review, owner 2026-11-11) the coup now: it's due, a home planet has been spent this round and a
// tactic token is left for the Hollows that The Blade's Orchestra readies
bool aiCoupNow(const Game& g, int p) {
    if (aiTune("couptiming", 1.f) <= 0.f || !isFaction(g, p, "firmament")) return false;
    const Player& me = g.players[size_t(p)];
    if (me.tactic < 1 || aiHomeSpentValue(g, p) < 2) return false;
    float v = 0.f;
    return aiTeComponentValue(g, p, int(Ab::Puppets), v) && v >= 1.0f;
}

bool aiTeComponentValue(const Game& g, int p, int ab, float& v) {
    const Player& me = g.players[size_t(p)];
    switch (Ab(ab)) {
        case Ab::BtArborec: {  // 0.44.4: an infantry, and a system of ours open again (ships there may move / it can be activated)
            v = 0.5f;
            for (const SystemState& s : g.systems)
                if (s.hasToken(p) && s.space[size_t(p)].nonFighterShips() > 0) v = 0.8f;
            return true;
        }
        case Ab::BtMahact: {  // 0.44.4: a relic for the technology we'd miss least - not one objectives or prerequisites need
            const Goals& go = aiGoals(g, p);
            float low = 99.f;
            for (int16_t t : me.techs) low = std::min(low, mahactKeep(g, p, t, go));
            const float relics = float(me.relics.size());
            // (0.55.0: offered 433 times in 140 games, never used - the bar was too high)
            v = g.round >= 3 && low < aiTune("mahactlow", 4.f) ? std::max(0.f, 0.9f - 0.25f * relics) : 0.f;
            return true;
        }
        case Ab::BtSaar: v = aiFreeSpend(g, p, false) >= 3 ? 0.7f : 0.f; return true;
        case Ab::BtMuaat: v = 0.8f; return true;   // The Nucleus: Star Forge for free
        case Ab::BtYssaril: v = me.actionCards.size() >= 7 ? 0.3f : 0.f; return true;  // a stall instead of passing
        case Ab::RalNelAgent: v = 0.7f; return true;   // a card for us, one to make a friend
        case Ab::Nano1: v = g.round >= 2 ? 0.6f : 0.3f; return true;
        case Ab::Nano2: {
            int dmg = 0;
            for (const SystemState& s : g.systems)
                for (int t = 0; t < kUnitTypes; ++t) dmg += s.space[size_t(p)].dmg[t];
            v = dmg >= 2 ? 0.8f : 0.3f;
            return true;
        }
        case Ab::Nano3: v = 0.3f; return true;  // (0.43.0: just enough to be used before passing - exhausting is free then)
        case Ab::DeepwroughtHero: v = g.round >= 4 ? 0.7f : 0.f; return true;
        case Ab::ShareKnowledge: {
            const int dw = playerOfFaction(g, "deepwrought");
            float best = 0.f;
            if (dw >= 0)
                for (int16_t t : g.players[size_t(dw)].techs)
                    if (kTechs[t].faction < 0 && !kTechs[t].unitUpgrade && !me.hasTech(t)) best = std::max(best, aiTechWant(g, p, t));
            v = best >= 4.f ? 0.8f : 0.f;
            return true;
        }
        case Ab::BtDeepwrought: {
            int rich = 0;  // Library Card: as soon as two rivals hold 3 trade goods (0.44.4: and a note to give us)
            for (int q = 0; q < g.numPlayers; ++q) {
                if (q == p || g.players[size_t(q)].tradeGoods < 3) continue;
                bool note = false;
                for (int16_t n : g.players[size_t(q)].notes) note = note || noteOwner(n) != p;
                if (note) ++rich;
            }
            v = rich == 0 ? 0.f : fquirkIs(g, p, "deepwrought", 1) ? (rich >= 2 ? 1.6f : 0.4f) : 0.5f + 0.25f * float(std::min(rich, 3));
            return true;
        }
        case Ab::BtCrimson: {
            bool closed = false;
            for (const SystemState& s : g.systems) closed = closed || s.breach == 1;
            v = closed ? 0.5f : 0.3f;
            return true;
        }
        case Ab::CrimsonAgent: v = std::max(ravinPair(g, p, nullptr, nullptr) >= 5.f ? 0.7f : 0.f, ravinExpand(g, p, nullptr, nullptr)); return true;
        case Ab::Sever: {  // a wormhole we stand in that an enemy fleet could come through
            v = 0.f;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                const uint8_t w = wormholesOf(g, int(s));
                if (!w || !hasUnits(g, p, int(s))) continue;
                for (size_t e = 0; e < g.systems.size(); ++e)
                    if (e != s && (wormholesOf(g, int(e)) & w))
                        for (int q = 0; q < g.numPlayers; ++q)
                            if (q != p && g.systems[e].space[size_t(q)].nonFighterShips() >= 2) v = 0.45f;
            }
            return true;
        }
        case Ab::BtNaaz: v = g.round >= 2 ? 1.2f : 0.5f; return true;  // Eidolon Maximum
        case Ab::Puppets: {  // the coup: with 2 plots (1 from round 3), never with the game nearly over
            const int n = plotCount(me);
            int lead = 0;
            for (const Player& q : g.players) lead = std::max<int>(lead, q.vp);
            const float coup = aiTune("firmcoup", 0.f);  // (test only: 1 = later, 2 = never)
            if (n == 0 || lead >= g.vpGoal - 1 || coup >= 2.f) {
                v = 0.f;
            } else if (coup >= 1.f) {
                v = (n >= 2 && g.round >= 4) || (n >= 1 && g.round >= 6) ? 1.6f : 0.f;
            } else if (firmQuirk(g, p, 1) && aiTune("coupclock", 1.f) <= 0.f) {  // (test only: the old Coup Clock, 2 plots)
                v = n >= 2 ? 1.8f : 0.f;
            } else if (firmQuirk(g, p, 1)) {  // Coup Clock (owner 2026-11-11): the moment it holds 2 plots, round 3 at the latest
                v = n >= 2 ? 1.8f : g.round >= 3 ? 1.6f : 0.f;
            } else {
                v = n >= 2 ? 1.6f : g.round >= 5 ? 1.5f : g.round >= 3 ? 1.1f : 0.f;
            }
            // (the Firmament bot review, owner 2026-11-11) The Blade's Orchestra readies Cronos Hollow and Tallin Hollow
            // at the coup: while neither home planet has been spent and a tactic token is left, spend them first (the
            // coup then only comes before passing - aiCoupNow takes it once one is spent)
            if (v > 0.f && aiTune("couptiming", 1.f) > 0.f && me.tactic >= 1 && aiHomeSpentValue(g, p) < 2) v = 0.35f;
            return true;
        }
        case Ab::FirmamentHero: v = 1.9f; return true;  // a plot (before the coup)
        // galactic events (0.36.0)
        case Ab::EvTotalWar: v = 3.0f; return true;  // a victory point
        case Ab::EvMercenary: v = me.tradeGoods >= 5 ? 1.0f : 0.4f; return true;
        case Ab::EvExplore: v = 0.5f; return true;
        case Ab::EvAtomics: {  // worth it on a rich enemy stronghold (2+ ground forces / structures)
            float best = 0.f;
            for (size_t x = 0; x < g.planets.size(); ++x) {
                const PlanetState& ps = g.planets[x];
                if (ps.destroyed || ps.owner == p || ps.owner < 0 || ps.system < 0 || isHomePlanet(g, int(x))) continue;
                const float n = float(ps.units[size_t(ps.owner)].groundForces() + ps.units[size_t(ps.owner)].structures() * 2);
                best = std::max(best, n);
            }
            v = best >= 5.f ? 1.4f : best >= 3.f && g.round >= 4 ? 0.9f : 0.f;
            return true;
        }
        case Ab::EvX89: {
            float best = 0.f;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                bool bomb = false;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (g.systems[s].space[size_t(p)].n[t] && unitDef(g, p, UnitType(t)).bombard > 0) bomb = true;
                if (!bomb) continue;
                for (int16_t x : g.systems[s].planets)
                    if (g.planets[size_t(x)].owner >= 0 && g.planets[size_t(x)].owner != p) best = std::max(best, aiPlanetValue(g, p, x));
            }
            v = best >= 3.f ? 1.0f : 0.f;
            return true;
        }
        case Ab::ObsidianHero: {
            int tired = 0;
            for (int x : controlledPlanets(g, p))
                if (g.planets[size_t(x)].exhausted) tired += std::max(planetResources(g, x), planetInfluence(g, x));
            v = tired >= 7 ? 1.2f : 0.f;
            // (the Firmament bot review, owner 2026-11-11) only when there's still a tactical action to spend them on
            if (aiTune("obshero", 1.f) > 0.f && me.tactic < 1) v = 0.f;
            return true;
        }
        default: return false;
    }
}

}  // namespace ti
