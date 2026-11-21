// AI choices for exploration, relics, legendary planets and Psychoarchaeology.
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "ai/ai_internal.h"
#include "engine/deal.h"
#include "engine/map.h"
#include "engine/query.h"

namespace ti {

namespace {
Answer pick1(int id) {
    Answer a;
    a.picks.push_back(id);
    return a;
}
bool hasOpt(const Decision& d, int id) {
    for (const Option& o : d.options)
        if (o.id == id && o.enabled) return true;
    return false;
}
int firstReal(const Decision& d) {  // first option that isn't "no / cancel"
    for (const Option& o : d.options)
        if (o.enabled && o.id >= 0) return o.id;
    return d.options.empty() ? 0 : d.options[0].id;
}
bool exKey(int c, const char* k) { return c >= 0 && c < kExploreCardCount && !std::strcmp(kExploreCards[c].key, k); }
bool relicKey(int r, const char* k) { return r >= 0 && r < kRelicCount && !std::strcmp(kRelics[r].key, k); }
bool yesNoDecision(const Decision& d) { return d.options.size() == 2 && hasOpt(d, 1) && hasOpt(d, 0); }

// ---- Uncharted Space legendary planets (0.33.0)
int prereqs(int t) {
    int n = 0;
    for (int c = 0; c < 4; ++c) n += kTechs[t].req[c];
    return n;
}
// Mystic Library: the swap worth most (what we gain minus what we purge)
struct Swap {
    int from = -1, to = -1;
    float gain = 0;
};
Swap prismBest(const Game& g, int p) {
    Swap best;
    const Player& me = g.players[p];
    for (int16_t f : me.techs) {
        if (kTechs[f].faction >= 0 || kTechs[f].unitUpgrade || kTechs[f].color == TechColor::None) continue;
        const float loss = aiTechWant(g, p, f);
        for (int t = 0; t < kTechCount; ++t) {
            if (t == f || me.hasTech(t) || kTechs[t].faction >= 0 || kTechs[t].color == TechColor::None) continue;
            if (!techImplemented(t) || prereqs(t) != prereqs(f)) continue;
            const float v = aiTechWant(g, p, t) - loss;
            if (v > best.gain) best = {f, t, v};
        }
    }
    return best;
}
// Fractal Gate: a cheap ship (no cargo to strand) into an adjacent system worth being in, without leaving
// home or a threatened system bare
struct Hop {
    int ship = -1, to = -1;
    float value = 0;
};
Hop domnaBest(const Game& g, int p) {
    Hop best;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        const Force& f = g.systems[s].space[size_t(p)];
        if (f.nonFighterShips() <= 0) continue;
        const bool home = int(s) == g.players[size_t(p)].homeSystem;
        if (home && f.nonFighterShips() <= 1) continue;
        for (UnitType t : {UnitType::Destroyer, UnitType::Cruiser, UnitType::Dreadnought, UnitType::Carrier}) {
            if (!f.n[int(t)]) continue;
            if (t == UnitType::Carrier && (f.count(UnitType::Fighter) + f.groundForces() > 0)) continue;  // its cargo would be lost
            for (size_t d = 0; d < g.systems.size(); ++d) {
                if (d == s || g.systems[d].isHyperlane() || !adjacentSystems(g, int(s), int(d)) || otherShipsIn(g, p, int(d))) continue;
                if (g.systems[d].d().anomalies & kSupernova) continue;
                float v = aiPresenceValue(g, p, int(d), true) - aiPresenceValue(g, p, int(s), true) * 0.5f;
                for (int16_t pl : g.systems[d].planets)
                    if (g.planets[pl].owner != p) v += aiPlanetValue(g, p, pl) * 0.15f;
                if (int(d) == g.mecatol()) v += 0.6f;
                if (v > best.value) best = {int(s) * 16 + int(t), int(d), v};
            }
            break;  // one ship type per system is enough to look at
        }
    }
    return best;
}

// ---- Uncharted Space exploration cards and relics (0.33.0)
// How much an exploration card is worth to whoever explores with it (Dark Visions).
float exploreCardValue(int c) {
    if (c < 0 || c >= kExploreCardCount) return 0.f;
    if (exKey(c, "dmz")) return -1.f;  // our structures leave the planet
    if (exKey(c, "lc")) return 2.f;
    if (exKey(c, "dv") || exKey(c, "darkvisions")) return 1.8f;
    if (exKey(c, "ed")) return 1.5f;
    switch (kExploreCards[c].kind) {
        case ExploreKind::Fragment: return 2.f;
        case ExploreKind::Attach: return 1.6f;
        case ExploreKind::Chart: return 1.3f;
        case ExploreKind::Token: return 0.6f;
        default: return 1.f;
    }
}
int deckOfPlanet(const Game& g, int x) {
    if (g.planets[size_t(x)].terraform) return int(ExploreType::Cultural);
    switch (kPlanets[g.planets[size_t(x)].def].trait) {
        case Trait::Cultural: return int(ExploreType::Cultural);
        case Trait::Hazardous: return int(ExploreType::Hazardous);
        case Trait::Industrial: return int(ExploreType::Industrial);
        default: return -1;
    }
}
// A new tile at the edge of the board: near our home and our units, away from enemy fleets.
float spotValue(const Game& g, int p, Hex h) {
    const int home = g.players[size_t(p)].homeSystem;
    float v = home >= 0 ? -1.0f * float(hexDistance(h, g.systems[size_t(home)].pos)) : 0.f;
    for (int e = 0; e < 6; ++e) {
        const int j = g.findSystemAt(hexNeighbor(h, e));
        if (j < 0) continue;
        if (hasUnits(g, p, j)) v += 0.6f;
        if (otherShipsIn(g, p, j)) v -= 0.8f;
    }
    return v;
}
// Command tokens: one move from pool a to pool b (a * 3 + b) - tactic tokens matter most.
int tokenMove(const Game& g, int p, const Decision& d) {
    const Player& me = g.players[size_t(p)];
    int maxUse = 0;
    for (size_t s = 0; s < g.systems.size(); ++s) maxUse = std::max(maxUse, fleetUseIn(g, p, int(s)));
    const int slack = fleetLimit(g, p) - maxUse;
    if (slack >= 2 && hasOpt(d, 1 * 3 + 0)) return 1 * 3 + 0;          // fleet -> tactic
    if (me.strategy >= 3 && hasOpt(d, 2 * 3 + 0)) return 2 * 3 + 0;     // strategy -> tactic
    if (slack < 0 && me.tactic >= 2 && hasOpt(d, 0 * 3 + 1)) return 0 * 3 + 1;  // short of fleet supply
    return -1;
}
// Forgotten Throne: secret objectives it could score now
bool throneSecret(const Game& g, int p) {
    for (int q = 0; q < g.numPlayers; ++q)
        for (int16_t o : (q == p ? g.players[size_t(q)].secrets : g.players[size_t(q)].scored))
            if (kObjectives[o].kind == 3 && !hasScored(g, p, o) && meetsObjective(g, p, o)) return true;
    return false;
}
// Eye of Vogul: ships worth bringing into a fight that can't reach it (strongest first)
float shipWorth(const Game& g, int p, UnitType t) {
    const UnitDef& u = unitDef(g, p, t);
    if (u.combat <= 0) return 0.f;
    return float(std::max(0, 11 - u.combat)) * float(std::max<int>(1, u.combatDice)) + (u.sustain ? 2.f : 0.f);
}
struct VogulShip {
    int id = -1;
    float worth = 0;
};
std::vector<VogulShip> vogulShipsFor(const Game& g, int p, int sys) {
    std::vector<VogulShip> v;
    const int home = g.players[size_t(p)].homeSystem;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        if (int(s) == sys || g.systems[s].hasToken(p)) continue;
        const Force& f = g.systems[s].space[size_t(p)];
        if (int(s) == home && f.nonFighterShips() <= 1) continue;  // don't leave home bare
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!f.n[t] || !isShip(UnitType(t)) || UnitType(t) == UnitType::Fighter || moveValue(g, p, int(s), UnitType(t)) <= 0) continue;
            if (shipReach(g, p, int(s), sys, UnitType(t), 0).ok) continue;
            for (int k = 0; k < std::min<int>(2, f.n[t]); ++k) v.push_back({int(s) * 16 + t + k * 4096, shipWorth(g, p, UnitType(t))});
        }
    }
    std::sort(v.begin(), v.end(), [](const VogulShip& a, const VogulShip& b) { return a.worth > b.worth; });
    return v;
}
bool vogulWanted(const Game& g, int p, int sys) {
    if (sys < 0) return false;
    bool fight = otherShipsIn(g, p, sys);
    for (int16_t x : g.systems[size_t(sys)].planets)
        if (g.planets[size_t(x)].owner >= 0 && g.planets[size_t(x)].owner != p) fight = true;
    if (!fight) return false;
    const std::vector<VogulShip> v = vogulShipsFor(g, p, sys);
    float w = 0;
    for (size_t i = 0; i < v.size() && i < 2; ++i) w += v[i].worth;
    return w >= 9.f;  // e.g. two dreadnoughts, a war sun
}

// Best enemy planet for the Stellar Converter (-1 if none).
int converterTarget(const Game& g, const Decision& d) {
    int best = -1;
    float bv = 0;
    for (const Option& o : d.options) {
        if (o.id < 0) continue;
        int owner = g.planets[o.id].owner;
        if (owner == d.player) continue;
        float v = owner >= 0 ? 2.f + planetResources(g, o.id) + planetInfluence(g, o.id) : 0.5f;
        if (owner >= 0 && g.players[owner].vp >= g.players[d.player].vp) v += 2.f;
        if (v > bv) {
            bv = v;
            best = o.id;
        }
    }
    return bv >= 3.f ? best : -1;
}
}  // namespace

float aiExploreCardValue(int c) { return exploreCardValue(c); }  // (0.44.4: Cryptic Insights)

float aiExploreComponentValue(const Game& g, int p, int option) {
    if (option >= 5000) return 1.3f;  // Enigmatic Device
    if (option >= 4000) {
        int r = option - 4000;
        if (relicKey(r, "codex")) return g.actionDiscard.size() >= 3 ? 1.2f : 0.5f;
        // Codex II (0.60.0): Dynamis Core keeps paying +2 commodities a refill - cashed in late (or for the last push);
        // JR-XS455-O every round (a structure for us, or a trade good); Nano-Forge at once on our best safe planet
        if (relicKey(r, "dynamiscore")) {
            const Endgame& E = aiEndgame(g, p);
            return g.round >= 6 || E.lastRound ? 0.8f + 0.25f * float(commodityValue(g, p)) : 0.f;
        }
        if (relicKey(r, "titanprototype")) return spendable(g, p, false) >= 5 ? 1.1f : 0.7f;
        if (relicKey(r, "nanoforge")) return 2.6f;
        // Thunder's Edge (0.34.0)
        if (relicKey(r, "bookoflatvinia")) {  // a point if we hold all four specialties, else the speaker token late on
            int skips = 0;
            for (int x : controlledPlanets(g, p)) skips |= planetSkips(g, x);
            if ((skips & 15) == 15) return 4.f;
            return g.round >= 4 && g.speaker != p ? 0.6f : 0.f;
        }
        if (relicKey(r, "circletofthevoid")) return 0.9f;
        if (relicKey(r, "thesilverflame")) {  // a last throw: one point from winning while someone else is too
            const Player& me = g.players[size_t(p)];
            bool race = false;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && !g.players[size_t(q)].eliminated && g.players[size_t(q)].vp >= g.vpGoal - 1) race = true;
            if (me.vp == g.vpGoal - 1 && race) return 0.6f;
            // (0.55.0) the last round, somebody else about to win and our points short of it: the one throw left
            const Endgame& E = aiEndgame(g, p);
            return E.lastRound && E.threat >= 0 && me.vp >= g.vpGoal - 1 && E.myReach < float(g.vpGoal) ? 0.6f : 0.f;
        }
        // Uncharted Space (0.33.0)
        if (relicKey(r, "decrypted_cartoglyph")) return 1.3f;  // the best of 3 new systems, next to us
        if (relicKey(r, "throne_of_the_false_emperor")) return throneSecret(g, p) ? 4.f : g.round >= 3 ? 1.4f : 0.4f;
        if (relicKey(r, "stellarconverter")) {
            // only worth it with a good enemy target
            for (size_t x = 0; x < g.planets.size(); ++x) {
                const PlanetState& ps = g.planets[x];
                if (ps.destroyed || ps.owner < 0 || ps.owner == p || kPlanets[ps.def].legendary) continue;
                if (g.systems[ps.system].home >= 0 || int(ps.system) == g.mecatol()) continue;
                for (int16_t a : g.systems[ps.system].adj)
                    for (int t = 0; t < kUnitTypes; ++t)
                        if (g.systems[a].space[p].n[t] && unitDef(g, p, UnitType(t)).bombard > 0 &&
                            planetResources(g, int(x)) + planetInfluence(g, int(x)) >= 3)
                            return 1.5f;
            }
            return 0.f;
        }
        return 0.5f;
    }
    if (option == 3010) return 1.1f;  // Star Charts: a new system next to us
    if (option >= 3000) return 2.5f;  // relic fragments -> relic
    return 0.f;
}

bool aiExploreAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out) {
    (void)rng;
    const int p = d.player;
    const Player& me = g.players[p];
    switch (d.tag) {
        case Tag::TokenRedistribute:  // Disciple Fran (Kolume agent, 0.43.0: answered like the other single moves)
            if (d.kind == DecisionKind::Pick && d.prompt.find("move which command token") != std::string::npos) {
                out = pick1(tokenMove(g, d.player, d));
                if (!hasOpt(d, out.picks[0])) out = pick1(-1);
                return true;
            }
            return false;
        case Tag::ExploreChoice: {
            const int c = d.explore;
            auto has = [&](const char* s) { return d.prompt.find(s) != std::string::npos; };
            if ((exKey(c, "fb") || exKey(c, "lf")) && has(": spend which?")) {  // (0.61.0) a commodity is worth less than a trade good
                out = pick1(0);
                return true;
            }
            // ---- Uncharted Space (0.33.0)
            if (has("move which command token")) {  // Distinguished Admiral, Scorched Depot
                out = pick1(tokenMove(g, p, d));
                if (!hasOpt(d, out.picks[0])) out = pick1(-1);
                return true;
            }
            if (has("place ") && has(" where?")) {  // Star Charts: a new system at the edge of the board
                int b = firstReal(d);
                float bv = -1e9f;
                for (const Option& o : d.options)
                    if (spotValue(g, p, spotHex(o.id)) > bv) {
                        bv = spotValue(g, p, spotHex(o.id));
                        b = o.id;
                    }
                out = pick1(b);
                return true;
            }
            if (exKey(c, "distinguishedadmiral")) {
                out = pick1(hasOpt(d, 1) && spendable(g, p, true, false) >= 4 ? 1 : hasOpt(d, 0) ? 0 : -1);
                return true;
            }
            if (exKey(c, "scorcheddepot") && !yesNoDecision(d)) {  // our own token back from this system
                out = pick1(hasOpt(d, 100 + p) ? 100 + p : 0);
                return true;
            }
            if (exKey(c, "seedyspaceport") && !yesNoDecision(d)) {  // an exhausted agent first
                out = pick1(firstReal(d) >= 0 && hasOpt(d, firstReal(d)) ? firstReal(d) : -1);
                return true;
            }
            if (exKey(c, "folderspace")) {
                if (has("move which units")) {  // which units: all of them
                    out = Answer();
                    for (const Option& o : d.options) out.picks.push_back(o.id);
                    return true;
                }
                // to a system worth being in (or stay)
                int from = -1;
                for (size_t s = 0; s < g.systems.size(); ++s)
                    if (d.prompt.find(" from " + std::string(kSystems[g.systems[s].def].name)) != std::string::npos) from = int(s);
                int b = -1;
                float bv = 1.0f;
                for (const Option& o : d.options) {
                    if (o.id < 0) continue;
                    float v = aiPresenceValue(g, p, o.id, true) - (from >= 0 ? aiPresenceValue(g, p, from, true) * 0.5f : 0.f);
                    for (int16_t x : g.systems[size_t(o.id)].planets)
                        if (g.planets[size_t(x)].owner != p) v += aiPlanetValue(g, p, x) * 0.15f;
                    if (o.id == g.mecatol()) v += 0.6f;
                    if (v > bv) {
                        bv = v;
                        b = o.id;
                    }
                }
                out = pick1(b);
                return true;
            }
            if (exKey(c, "suspiciouswreckage")) {
                if (has("choose 2 players")) {  // those with the most notes
                    std::vector<std::pair<int, int>> v;
                    for (const Option& o : d.options) v.push_back({int(g.players[size_t(o.id)].notes.size()), o.id});
                    std::sort(v.rbegin(), v.rend());
                    out = Answer();
                    for (size_t i = 0; i < v.size() && int(i) < d.maxPick; ++i) out.picks.push_back(v[i].second);
                    return true;
                }
                int b = firstReal(d);  // giving one away: the least valuable
                float bv = 1e9f;
                for (const Option& o : d.options)
                    if (noteDefaultValue(g, o.id) < bv) {
                        bv = noteDefaultValue(g, o.id);
                        b = o.id;
                    }
                out = pick1(b);
                return true;
            }
            if (exKey(c, "darkvisions")) {
                if (has("discard which")) {  // drop the cards that would hurt the explorer
                    out = Answer();
                    for (const Option& o : d.options) {
                        const auto& deck = g.exploreDeck[size_t(o.id)];
                        if (!deck.empty() && exploreCardValue(deck.back()) < 0.5f) out.picks.push_back(o.id);
                    }
                    return true;
                }
                int b = -1;  // the planet whose deck shows the best card
                float bv = 0.7f;
                for (const Option& o : d.options) {
                    if (o.id < 0) continue;
                    const int t = deckOfPlanet(g, o.id);
                    if (t < 0 || g.exploreDeck[size_t(t)].empty()) continue;
                    const float v = exploreCardValue(g.exploreDeck[size_t(t)].back());
                    if (v > bv) {
                        bv = v;
                        b = o.id;
                    }
                }
                out = pick1(b);
                return true;
            }
            if (exKey(c, "frln")) {
                // 0.42.2 (#187): no ship where the fleet pool is full - it would be removed at once
                const int sys = d.system >= 0 ? d.system : g.activeSystem;
                const bool room = sys < 0 || fleetLimit(g, p) - fleetUseIn(g, p, sys) > 0;
                for (UnitType t : {UnitType::Cruiser, UnitType::Destroyer, UnitType::Infantry, UnitType::Fighter})
                    if ((room || t == UnitType::Infantry) && hasOpt(d, int(t))) {
                        out = pick1(int(t));
                        return true;
                    }
                out = pick1(-1);
                return true;
            }
            if (yesNoDecision(d)) {
                // removing an infantry for a reward: only if another one stays
                if (d.prompt.find("remove 1 infantry") != std::string::npos) {
                    int inf = 0;
                    for (const PlanetState& ps : g.planets)
                        if (ps.owner == p && d.prompt.find(kPlanets[ps.def].name) != std::string::npos) inf = ps.units[p].count(UnitType::Infantry);
                    out = pick1(inf >= 2 ? 1 : 0);
                    return true;
                }
                out = pick1(1);
                return true;
            }
            if (exKey(c, "aw")) {
                out = pick1(me.commodities >= 2 && hasOpt(d, 1) ? 1 : 0);
                return true;
            }
            if (exKey(c, "fb") || exKey(c, "lf")) {
                out = pick1(hasOpt(d, 1) && (me.tradeGoods >= 1 || me.commodities >= 1) ? 1 : 0);
                return true;
            }
            if (exKey(c, "ms")) {
                int maxComm = commodityValue(g, me);
                out = pick1(hasOpt(d, 1) && me.commodities * 2 >= maxComm ? 1 : 0);
                return true;
            }
            out = pick1(firstReal(d));
            return true;
        }
        case Tag::RelicChoice: {
            const int r = d.relic;
            // ---- Thunder's Edge (0.34.0)
            if (relicKey(r, "thetriad")) {  // when most of it goes to use, or the payment needs it
                const int tv = triadValue(g, p);
                int need = 0;
                const size_t at = d.context.find(" needed)");
                if (at != std::string::npos) {
                    size_t b = d.context.rfind('(', at);
                    if (b != std::string::npos) need = std::atoi(d.context.c_str() + b + 1);
                }
                const bool infl = d.prompt.find("influence") != std::string::npos;
                const bool must = spendable(g, p, infl) - tv < need;
                out = pick1(must || need >= tv - 1 ? 1 : 0);
                return true;
            }
            if (relicKey(r, "bookoflatvinia")) {  // the technology we want most
                int b = firstReal(d);
                float bv = 0.f;
                for (const Option& o : d.options)
                    if (o.id >= 0 && aiTechWant(g, p, o.id) > bv) {
                        bv = aiTechWant(g, p, o.id);
                        b = o.id;
                    }
                out = pick1(bv > 0.f ? b : firstReal(d));
                return true;
            }
            if (relicKey(r, "circletofthevoid")) {  // nearest to our home
                const int home = g.players[size_t(p)].homeSystem;
                int b = firstReal(d), bd = 1 << 20;
                for (const Option& o : d.options)
                    if (o.id >= 0 && home >= 0) {
                        const int dist = hexDistance(g.systems[size_t(o.id)].pos, g.systems[size_t(home)].pos);
                        if (dist < bd) {
                            bd = dist;
                            b = o.id;
                        }
                    }
                out = pick1(b);
                return true;
            }
            if (relicKey(r, "heartofixth")) {  // our miss into a hit, or a hit against us into a miss
                const bool mine = g.sageRoller == p, atUs = g.sageTarget == p;
                int b = -1;
                for (const Option& o : d.options) {
                    if (o.id < 0) continue;
                    const bool plus = o.id % 2 == 1;
                    if ((mine && plus) || (atUs && !plus)) {
                        b = o.id;
                        break;
                    }
                }
                out = pick1(b);
                return true;
            }
            if (relicKey(r, "neuraloop")) {
                if (d.prompt.find("which deck") != std::string::npos) {  // the deck whose cards we're likeliest to meet
                    out = pick1(hasOpt(d, 1) ? 1 : firstReal(d));
                    return true;
                }
                // replace an objective the others meet and we don't - with the Neuraloop itself
                const int obj = d.value;
                int meetOthers = 0;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != p && obj >= 0 && obj < kObjectiveCount && meetsObjective(g, q, obj)) ++meetOthers;
                const bool mineToo = obj >= 0 && obj < kObjectiveCount && meetsObjective(g, p, obj);
                int self = -1;
                for (const Option& o : d.options)
                    if (relicKey(o.id, "neuraloop")) self = o.id;
                out = pick1(!mineToo && meetOthers >= 2 && self >= 0 ? self : -1);
                return true;
            }
            // ---- Uncharted Space (0.33.0)
            if (relicKey(r, "decrypted_cartoglyph")) {
                int b = firstReal(d);
                float bv = -1e9f;
                for (const Option& o : d.options) {
                    const float v = d.prompt.find("where?") != std::string::npos ? spotValue(g, p, spotHex(o.id)) : tileValue(o.id);
                    if (v > bv) {
                        bv = v;
                        b = o.id;
                    }
                }
                out = pick1(b);
                return true;
            }
            // ---- Codex II (0.60.0)
            if (relicKey(r, "titanprototype")) {
                if (yesNoDecision(d)) {  // a structure for 3 resources when there's a PDS / dock worth placing, else the trade good
                    const bool rich = spendable(g, p, false) >= 5;
                    out = pick1(rich ? 1 : 0);
                    return true;
                }
                out = pick1(hasOpt(d, p) ? p : firstReal(d));  // (ourselves)
                return true;
            }
            if (relicKey(r, "nanoforge")) {  // the planet least exposed, best values
                int b = firstReal(d);
                float bv = -1e9f;
                for (const Option& o : d.options) {
                    if (o.id < 0) continue;
                    const int s = g.planets[size_t(o.id)].system;
                    float v = float(planetResources(g, o.id) + planetInfluence(g, o.id));
                    for (int16_t a : g.systems[size_t(s)].adj)
                        for (int q = 0; q < g.numPlayers; ++q)
                            if (q != p && g.systems[size_t(a)].space[size_t(q)].ships() > 0) v -= 2.f;
                    if (v > bv) bv = v, b = o.id;
                }
                out = pick1(b);
                return true;
            }
            if (relicKey(r, "throne_of_the_false_emperor")) {  // a secret objective if one is there, else a relic
                int b = hasOpt(d, -2) ? -2 : -1;
                for (const Option& o : d.options)
                    if (o.id >= 0) b = o.id;
                out = pick1(b);
                return true;
            }
            if (relicKey(r, "eye_of_vogul")) {
                if (yesNoDecision(d)) {
                    out = pick1(vogulWanted(g, p, g.activeSystem) ? 1 : 0);
                    return true;
                }
                out = Answer();
                for (const VogulShip& v : vogulShipsFor(g, p, g.activeSystem))
                    if (hasOpt(d, v.id) && int(out.picks.size()) < d.maxPick) out.picks.push_back(v.id);
                if (out.picks.empty()) out.picks.push_back(firstReal(d));
                return true;
            }
            if (relicKey(r, "dominusorb")) {  // (0.55.0) when the movement it opens is worth a relic
                out = pick1(aiDominusGain(g, p, g.activeSystem) >= aiTune("orbgain", 6.f) ? 1 : 0);
                return true;
            }
            if (relicKey(r, "thalnos")) {  // reroll only where a hit is very likely
                out = Answer();
                for (const Option& o : d.options)
                    if (o.detail.find("needs 5") != std::string::npos || o.detail.find("needs 4") != std::string::npos ||
                        o.detail.find("needs 3") != std::string::npos)
                        if (o.label.find("Infantry") != std::string::npos || o.label.find("Fighter") != std::string::npos)
                            out.picks.push_back(o.id);
                return true;
            }
            if (relicKey(r, "mawofworlds")) {
                int best = -1;
                float bv = 0;
                if (g.round >= 5)
                    for (const Option& o : d.options) {
                        if (o.id < 0) continue;
                        float v = aiTechWant(g, p, o.id);
                        if (v > bv) {
                            bv = v;
                            best = o.id;
                        }
                    }
                out = pick1(best);
                return true;
            }
            if (relicKey(r, "codex")) {
                out = Answer();
                for (const Option& o : d.options)
                    if (int(out.picks.size()) < d.maxPick) out.picks.push_back(o.id);
                return true;
            }
            if (relicKey(r, "stellarconverter")) {
                out = pick1(converterTarget(g, d));
                return true;
            }
            if (yesNoDecision(d)) {
                out = pick1(1);
                return true;
            }
            out = pick1(firstReal(d));  // Crown of Emphidia: explore the first planet offered
            return true;
        }
        case Tag::LegendaryAbility: {
            auto has = [&](const char* s) { return d.prompt.find(s) != std::string::npos; };
            // ---- Thunder's Edge (0.34.0)
            if (has("The Acropolis")) {  // an agent first, then the richest planet, then a technology
                int b = -1;
                float bv = 0.f;
                for (const Option& o : d.options) {
                    if (o.id < 0) continue;
                    float v = o.id >= 40000 ? 2.5f : o.id >= 30000 ? 3.f : o.id >= 20000 ? 1.5f : o.id >= 10000 ? 2.f
                                                                                                 : float(planetResources(g, o.id) + planetInfluence(g, o.id)) * 0.6f;
                    if (v > bv) {
                        bv = v;
                        b = o.id;
                    }
                }
                out = pick1(bv >= 1.2f ? b : -1);
                return true;
            }
            if (has("The Galactic Council")) {  // trade away a secret we can't get to (early on)
                const int s = aiSecretToReturn(g, d);
                out = pick1(g.round <= 5 && s >= 0 && hasOpt(d, s) && !meetsObjective(g, p, s) ? s : -1);
                return true;
            }
            if (has("Maxis Central Control")) {
                int b = -1;
                float bv = 1.0f;
                for (const Option& o : d.options) {
                    if (o.id < 0) continue;
                    float v = aiPlanetValue(g, p, o.id);
                    if (g.planets[size_t(o.id)].owner >= 0) v += 0.5f;  // taken from a rival counts twice
                    if (v > bv) {
                        bv = v;
                        b = o.id;
                    }
                }
                out = pick1(b);
                return true;
            }
            if (has("Dok 'N Pic's Salvage Yard")) {
                out = pick1(firstReal(d));
                return true;
            }
            if (has("Aeurex Mechanica: place which")) {  // the most valuable ship we have the upgrade for
                int b = -1, bc = -1;
                for (const Option& o : d.options)
                    if (o.id >= 0 && unitDef(g, p, UnitType(o.id)).cost2 > bc) {
                        bc = unitDef(g, p, UnitType(o.id)).cost2;
                        b = o.id;
                    }
                out = pick1(b);
                return true;
            }
            if (has("Aeurex Mechanica: place the")) {  // with our biggest fleet
                int b = firstReal(d), bn = -1;
                for (const Option& o : d.options)
                    if (o.id >= 0 && g.systems[size_t(o.id)].space[size_t(p)].nonFighterShips() > bn) {
                        bn = g.systems[size_t(o.id)].space[size_t(p)].nonFighterShips();
                        b = o.id;
                    }
                out = pick1(b);
                return true;
            }
            // Uncharted Space (0.33.0)
            if (has("Mystic Library")) {
                const Swap sw = prismBest(g, p);
                if (has("purge which")) out = pick1(hasOpt(d, sw.from) ? sw.from : -1);
                else if (has("gain which")) {
                    int b = firstReal(d);
                    float bv = -1e9f;
                    for (const Option& o : d.options)
                        if (o.id >= 0 && aiTechWant(g, p, o.id) > bv) {
                            bv = aiTechWant(g, p, o.id);
                            b = o.id;
                        }
                    out = pick1(b);
                } else out = pick1(sw.gain >= 4.f && hasOpt(d, 1) ? 1 : 0);  // a clear gain (no swapping back and forth)
                if (!hasOpt(d, out.picks[0])) out = pick1(d.options.back().id);
                return true;
            }
            if (has("Fractal Gate")) {
                const Hop h = domnaBest(g, p);
                if (has("move which")) out = pick1(hasOpt(d, h.ship) ? h.ship : -1);
                else if (has("to which")) out = pick1(hasOpt(d, h.to) ? h.to : firstReal(d));
                else out = pick1(h.value >= 1.0f && hasOpt(d, 1) ? 1 : 0);
                if (!hasOpt(d, out.picks[0])) out = pick1(d.options.back().id);
                return true;
            }
            if (has("Council Black Site: which outcome")) {
                float conf = 0;
                const int o = aiPredictOutcome(g, g.currentAgenda, &conf);
                out = pick1(hasOpt(d, o) ? o : firstReal(d));
                return true;
            }
            if (has("Imperial Salvage Yard: place")) {  // where enemies are near, else with the biggest fleet
                int b = firstReal(d);
                float bv = -1e9f;
                for (const Option& o : d.options) {
                    if (o.id < 0) continue;
                    float v = float(g.systems[size_t(o.id)].space[size_t(p)].nonFighterShips()) * 0.3f;
                    for (size_t s = 0; s < g.systems.size(); ++s)
                        if ((int(s) == o.id || adjacentSystems(g, o.id, int(s))) && otherShipsIn(g, p, int(s))) v += 1.f;
                    if (v > bv) {
                        bv = v;
                        b = o.id;
                    }
                }
                out = pick1(b);
                return true;
            }
            if (has("Bi-Phasic Generators: put")) {  // next to our ships: we can explore it soon
                int b = firstReal(d);
                // (0.61.0: supernovas are offered now - only where our ships can go)
                auto reachable = [&](int s) { return s >= 0 && (!(g.systems[size_t(s)].d().anomalies & kSupernova) || canEndMoveIn(g, p, s)); };
                for (const Option& o : d.options)
                    if (reachable(o.id)) {
                        b = o.id;
                        break;
                    }
                for (const Option& o : d.options) {
                    if (!reachable(o.id)) continue;
                    bool near = false;
                    for (size_t s = 0; s < g.systems.size() && !near; ++s)
                        near = hasShips(g, p, int(s)) && adjacentSystems(g, o.id, int(s));
                    if (o.id >= 0 && near) {
                        b = o.id;
                        break;
                    }
                }
                out = pick1(b);
                return true;
            }
            if (d.prompt.find("Exterrix") != std::string::npos || d.prompt.find("Mallice") != std::string::npos) {
                out = pick1(hasOpt(d, 2) && me.commodities >= 3 ? 2 : 1);
                return true;
            }
            out = pick1(firstReal(d) == 0 ? 1 : firstReal(d));
            if (!hasOpt(d, out.picks[0])) out = pick1(firstReal(d));
            return true;
        }
        case Tag::ExploreOrder: {
            // Scanlink Drone Network (0.44.5): a planet of ours first - what's found there (attachments) is ours
            if (d.prompt.find("Scanlink") != std::string::npos) {
                int best = firstReal(d);
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0 && o.id < int(g.planets.size()) && g.planets[size_t(o.id)].owner == d.player) {
                        best = o.id;
                        break;
                    }
                out = pick1(best);
                return true;
            }
            out = pick1(firstReal(d));
            return true;
        }
        case Tag::ComponentChoice:
            if (d.prompt.find("Psychoarchaeology") != std::string::npos) {
                out = Answer();
                for (const Option& o : d.options) out.picks.push_back(o.id);
                return true;
            }
            return false;
        default: return false;
    }
}

}  // namespace ti
