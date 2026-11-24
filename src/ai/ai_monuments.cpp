// Monuments+ (0.82.0): the computer players' monument choices.
//
// Where to raise the monument: every card cares where it stands ("Monuments should always care where they are located" -
// the Monuments+ design notes), so a spot is valued by what the monument's numbers do there (PRODUCTION, SPACE CANNON,
// PLANETARY SHIELD), by the planet, and by how safe it is (a structure is lost with the planet). Each monument's own
// ability adds its own reasons (aiMonumentAbilityValue - the abilities come in later steps of 0.82.0).

#include <algorithm>
#include <cstring>

#include "ai/ai.h"
#include "ai/ai_internal.h"
#include "engine/query.h"

namespace ti {

namespace {
constexpr int kMonPlanetId = 100000, kMonSpaceId = 200000;  // (rules_monuments.cpp)
Answer one(int id) {
    Answer a;
    a.picks.push_back(id);
    return a;
}
}  // namespace

// What the monument's own card (and, for the Nekro, the cards it copies) is worth at this spot beyond its numbers: each ability
// looks at "this system" (and the ones next to it), so the spot is chosen for what the ability can reach from there
float aiMonumentAbilityValue(const Game& g, int p, int sys, int planet) {
    (void)planet;
    int c[3];
    const int n = monumentCards(g, p, c);
    float v = 0.f;
    std::vector<int> near{sys};
    for (int16_t a : g.systems[size_t(sys)].adj) near.push_back(a);
    auto enemyAround = [&](int steps) { return aiEnemyNear(g, p, sys, steps); };
    for (int i = 0; i < n; ++i) {
        const char* k = kMonuments[c[i]].key;
        auto is = [&](const char* x) { return !std::strcmp(k, x); };
        if (is("creuss")) {  // wormhole systems next to it
            for (int16_t a : g.systems[size_t(sys)].adj) v += wormholesOf(g, a) ? 0.9f : 0.f;
        } else if (is("letnev")) {  // a space dock there (production doubled), ships for the votes
            if (productionCapacity(g, p, sys) > 0) v += 1.5f;
        } else if (is("muaat")) {
            for (int16_t a : g.systems[size_t(sys)].adj) v += (kSystems[g.systems[size_t(a)].def].anomalies & kSupernova) ? 1.f : 0.f;
        } else if (is("yssaril") || is("naaz") || is("hacan")) {  // other players around
            for (int q = 0; q < g.numPlayers; ++q) {
                if (q == p) continue;
                bool there = false;
                for (int s : near) there = there || hasUnits(g, q, s);
                if (there) v += is("yssaril") ? 0.5f : 0.3f;
            }
        } else if (is("mentak") || is("argent") || is("creuss") || is("sol") || is("nomad")) {  // the front
            if (enemyAround(1)) v += 0.6f;
            else if (enemyAround(2)) v += 0.3f;
        } else if (is("arborec")) {  // planets nobody holds, next to it
            for (int s : near)
                for (int16_t x : g.systems[size_t(s)].planets)
                    if (g.planets[size_t(x)].owner < 0) v += 0.35f;
        } else if (is("titans")) {
            for (int s : near)
                for (int16_t x : g.systems[size_t(s)].planets)
                    if (!g.planets[size_t(x)].sleeper) v += 0.2f;
        } else if (is("l1z1x") || is("keleres")) {
            v += 0.15f * float(g.systems[size_t(sys)].adj.size());
        } else if (is("mahact")) {  // others' tokens land in it when they come
            if (enemyAround(2)) v += 0.5f;
        } else if (is("saar")) {  // far from their docks: the beacon brings them closer
            v += 0.4f;
        } else if (is("jolnar") || is("xxcha") || is("yin")) {
            v += 0.4f;
        // (0.82.1) Thunder's Edge's monuments
        } else if (is("bastion")) {  // its station card: +1 commodity value and a 1 / 1 card - kept while nobody else holds the space
            v += 1.6f;
            if (g.systems[size_t(sys)].space[size_t(p)].nonFighterShips() >= 2) v += 0.4f;
        } else if (is("crimson")) {  // breaches next to it (the Crimson open more of them)
            for (int s : near) v += g.systems[size_t(s)].breach ? 0.35f : 0.f;
            v += 0.2f;
        } else if (is("deepwrought")) {  // no fight there unless we start it: a safe harbour at the front
            if (enemyAround(1)) v += 0.9f;
            else if (enemyAround(2)) v += 0.5f;
        } else if (is("firmament")) {  // tokens come from whoever activates the system: a busy place
            for (int q = 0; q < g.numPlayers; ++q) {
                if (q == p) continue;
                bool there = false;
                for (int s : near) there = there || hasShips(g, q, s);
                if (there) v += 0.3f;
            }
        } else if (is("ralnel")) {  // space structures around to move
            v += 0.3f;
        // (0.82.2) Discordant Stars, batch 1
        } else if (is("axis")) {  // a trade good whenever ships are built here: where our docks build
            v += productionCapacity(g, p, sys) > 0 ? 0.9f : 0.4f;
        } else if (is("khrask")) {  // a 2-dice ground fighter that defends the planet
            v += 0.6f + (enemyAround(1) ? 0.6f : enemyAround(2) ? 0.3f : 0.f);
        } else if (is("cymiae")) {  // a toll on whoever comes: a front-line system
            v += 0.3f + (enemyAround(1) ? 0.6f : enemyAround(2) ? 0.3f : 0.f);
        } else if (is("lizho")) {  // a command token whenever its system is activated
            v += 0.6f + (enemyAround(1) ? 0.3f : 0.f);
        } else if (is("olradin")) {  // the planet readied after Diplomacy: its value
            if (planet >= 0) v += 0.12f * float(planetResources(g, planet) + planetInfluence(g, planet));
        } else if (is("mirveda")) {  // a free first round of ground combat in its system
            v += 0.4f + (enemyAround(1) ? 0.6f : enemyAround(2) ? 0.3f : 0.f);
        } else if (is("veldyr")) {  // space cannon that can't be dodged: where our PDS stand
            int pds = 0;
            for (int16_t x : g.systems[size_t(sys)].planets) pds += g.planets[size_t(x)].units[size_t(p)].count(UnitType::PDS);
            v += 0.2f + 0.3f * float(std::min(pds, 3));
        } else if (is("nokar")) {  // a dock-like system ships can leave after building
            v += 0.7f;
        } else if (is("edyn")) {  // a free mech every round
            v += 1.5f;
        } else if (is("rohdhna")) {  // 2 cheaper production here (and SPACE CANNON 7 x2)
            v += productionCapacity(g, p, sys) > 0 ? 1.2f : 0.5f;
        } else if (is("kortali")) {  // fallen units hit back: a front line
            v += 0.3f + (enemyAround(1) ? 0.5f : enemyAround(2) ? 0.25f : 0.f);
        } else if (is("ghemina")) {  // a shortcut to systems with 2 of our PDS / docks
            v += 0.5f;
        // (0.82.3) Discordant Stars, batch 2
        } else if (is("myko")) {  // fighters without capacity in and around it (and free fighters on omens): where the fleet stands
            v += 0.5f + (productionCapacity(g, p, sys) > 0 ? 0.4f : 0.f) + (enemyAround(1) ? 0.4f : enemyAround(2) ? 0.2f : 0.f);
        } else if (is("dihmohn")) {  // a launch pad: ships there reach 1 further - in the middle of things
            v += 0.4f + 0.08f * float(g.systems[size_t(sys)].adj.size()) + (productionCapacity(g, p, sys) > 0 ? 0.4f : 0.f);
        } else if (is("vaden")) {  // debtors' ships around it
            for (int q = 0; q < g.numPlayers; ++q) {
                if (q == p || g.dsDebt[size_t(q)] <= 0) continue;
                bool there = false;
                for (int s : near) there = there || hasShips(g, q, s);
                if (there) v += 0.4f;
            }
            v += 0.2f;
        } else if (is("florzen")) {  // the stored fighters come out here: where a fleet gathers, near the enemy
            v += 0.4f + (enemyAround(1) ? 0.5f : enemyAround(2) ? 0.3f : 0.f);
        } else if (is("mortheus")) {  // other fleets next to it to pull in: a front where our fleet waits
            v += 0.3f + (enemyAround(1) ? 0.6f : enemyAround(2) ? 0.3f : 0.f);
        } else if (is("nivyn")) {  // a trap for others' ships and a road to the Wound
            v += 0.4f + (g.dsWound >= 0 ? 0.3f : 0.f) + (enemyAround(1) ? 0.4f : 0.f);
        } else if (is("augurs")) {  // explorations of a planet of ours with a trait
            if (planet >= 0) {
                const PlanetDef& d = kPlanets[g.planets[size_t(planet)].def];
                v += d.trait != Trait::None ? 0.8f : 0.f;
            }
        } else if (is("tnelis")) {  // a scorched earth stronghold: worth more where it will be attacked
            v += 0.3f + (enemyAround(1) ? 0.5f : enemyAround(2) ? 0.25f : 0.f);
        } else if (is("vaylerian")) {  // +1 commodity value while others' ships are around: a crossroads
            int others = 0;
            for (int q = 0; q < g.numPlayers; ++q) {
                if (q == p) continue;
                bool there = false;
                for (int s : near) there = there || hasShips(g, q, s);
                others += there ? 1 : 0;
            }
            v += 0.3f + 0.35f * float(std::min(others, 3));
        } else if (is("fsc")) {  // others' units around it (and our notes in their hands)
            for (int q = 0; q < g.numPlayers; ++q) {
                if (q == p) continue;
                bool there = false;
                for (int s : near) there = there || hasUnits(g, q, s);
                if (there) v += 0.25f;
            }
        } else if (is("celdauri")) {  // ground forces for our dock planets: away from them, safe
            v += 0.4f + (enemyAround(1) ? -0.2f : 0.f);
        } else if (is("zelian")) {  // an asteroid field we move into freely: others need Antimass Deflectors
            v += 0.6f + (enemyAround(1) ? 0.4f : 0.f);
        // (0.82.4) Discordant Stars, batch 3
        } else if (is("kollecc")) {  // a den our cloaked ships come out of: where we fight (a legendary planet)
            v += 0.5f + (enemyAround(1) ? 0.4f : enemyAround(2) ? 0.2f : 0.f);
        } else if (is("bentor")) {  // placed by DEPLOY on others' planets; on our own planet it is a PRODUCTION 1 structure
            v += 0.3f;
        } else if (is("kjalengard")) {  // command tokens from the combats around it: a front line
            v += 0.4f + (enemyAround(1) ? 0.6f : enemyAround(2) ? 0.3f : 0.f);
        } else if (is("lanefir")) {  // specialties in and around it (anyone's)
            int sk = 0;
            for (int s : near)
                for (int16_t x : g.systems[size_t(s)].planets) sk += kPlanets[g.planets[size_t(x)].def].skip != TechColor::None ? 1 : 0;
            v += 0.3f + 0.35f * float(std::min(sk, 4));
        } else if (is("cheiran")) {  // PRODUCTION = our structures around it
            int n = 0;
            for (int s : near) {
                n += g.systems[size_t(s)].space[size_t(p)].structures();
                for (int16_t x : g.systems[size_t(s)].planets) n += g.planets[size_t(x)].units[size_t(p)].structures();
            }
            v += 0.4f * float(std::min(n + 1, 6));
        } else if (is("kyro")) {  // a legendary planet with an attachment and a specialty; a relic when it falls
            v += 1.0f;
        } else if (is("ghoti")) {  // a frontier token for us and a planetless system while we move
            v += 0.6f;
        } else if (is("kolume")) {  // SPACE CANNON 6 x2 and shots at others' ships when they pass: near enemies, near other guns
            v += 0.4f + (enemyAround(1) ? 0.5f : enemyAround(2) ? 0.25f : 0.f);
        } else if (is("gledge")) {  // laws blank for a turn (our Core planets) and PRODUCTION 1
            v += 0.4f + (productionCapacity(g, p, sys) > 0 ? 0.f : 0.3f);
        } else if (is("rhodun_front") || is("rhodun_back")) {  // +1 / +2 to our combat rolls in and around it: the front
            v += 0.5f + (enemyAround(1) ? 0.6f : enemyAround(2) ? 0.3f : 0.f);
        }
    }
    return v;
}

float aiMonumentSpotValue(const Game& g, int p, int id) {
    if (!isMonumentOption(id)) return -1e9f;
    const int planet = id >= kMonSpaceId ? -1 : id - kMonPlanetId;
    const int sys = id >= kMonSpaceId ? id - kMonSpaceId : g.planets[size_t(planet)].system;
    const UnitDef& u = unitDef(g, p, UnitType::Monument);
    float v = 3.f;  // raising it at all (its card works only on the board; a structure for the structure objectives)
    v += 0.8f * float(u.production);
    if (u.cannon > 0) v += 0.35f * float(u.cannonDice) * float(11 - u.cannon);
    if (u.shield) v += 0.8f;
    if (planet >= 0) {
        v += 0.15f * aiPlanetValue(g, p, planet);
        const bool home = g.systems[size_t(sys)].home == p;
        if (home) v += 0.5f;  // safest, but monuments are meant to be out on the board (most cards look at "this system")
    }
    // a structure is lost with the planet (or, in space, when blockaded): keep it away from enemy fleets
    if (aiEnemyNear(g, p, sys, 1)) v -= 2.f;
    else if (aiEnemyNear(g, p, sys, 2)) v -= 0.7f;
    if (planet < 0 && g.systems[size_t(sys)].space[size_t(p)].nonFighterShips() == 0) v -= 0.5f;
    v += aiMonumentAbilityValue(g, p, sys, planet);
    return v;
}

// What Monuments+'s Construction card is worth more for its third line ("If your monument is not on the board, you may
// place it"): the best spot the monument could go to now (0 when it is on the board or fits nowhere)
float aiMonumentCardBonus(const Game& g, int p) {
    if (playerMonument(g, p) < 0 || unitsAvailable(g, p, UnitType::Monument) <= 0) return 0.f;
    const int m = playerMonument(g, p);
    float best = 0.f;
    if (kMonuments[m].places & (kMonEmpty | kMonStation)) {
        for (size_t s = 0; s < g.systems.size(); ++s)
            if (monumentFitsSpace(g, p, int(s))) best = std::max(best, aiMonumentSpotValue(g, p, kMonSpaceId + int(s)));
    } else {
        for (int x : controlledPlanets(g, p))
            if (!isDMZ(g, x) && monumentFitsPlanet(g, p, x)) best = std::max(best, aiMonumentSpotValue(g, p, kMonPlanetId + x));
    }
    return best <= 0.5f ? 0.f : std::min(3.f, 1.f + 0.4f * best);
}

namespace {
// how much p wants to hurt q's units (0 = a friend: a deal partner they keep promises with, or not at all)
float harmWish(const Game& g, int p, int q) {
    if (q == p) return -1.5f;
    if (q >= g.numPlayers) return 0.6f;  // neutral units
    if (g.players[size_t(q)].eliminated) return 0.f;
    float v = q < 8 ? aiStyle(g, p).vs[q] : 1.f;
    for (const Promise& pr : g.promises)
        if (pr.state == kPromiseActive && pr.by == p && pr.to == q &&
            (pr.kind == PromiseKind::NoAttack || pr.kind == PromiseKind::LetPass))
            v *= 0.1f;
    // the leader is everybody's target
    int best = 0;
    for (int r = 0; r < g.numPlayers; ++r) best = std::max(best, int(g.players[size_t(r)].vp));
    if (g.players[size_t(q)].vp == best && best >= g.vpGoal - 3) v *= 1.4f;
    return v;
}
float shipCost(const Game& g, int q, int t) { return float(unitCost2(g, q, UnitType(t))) * 0.5f; }
std::vector<int> nearSystems(const Game& g, int sys) {
    std::vector<int> v{sys};
    for (int16_t a : g.systems[size_t(sys)].adj) v.push_back(a);
    return v;
}
// Argent's ACTION in a system: each damaged ship with SUSTAIN DAMAGE destroyed, each undamaged one damaged
float argentValue(const Game& g, int p, int s) {
    float v = 0.f;
    for (int q = 0; q < kSides; ++q) {
        if (q == p || (q >= g.numPlayers && q != kNeutral)) continue;
        const Force& f = g.systems[size_t(s)].space[size_t(q)];
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!f.n[t] || !isShip(UnitType(t)) || !unitDef(g, q, UnitType(t)).sustain) continue;
            const bool can = unitSustainsAt(g, q, UnitType(t), s, -1);
            const float dead = float(can ? f.dmg[t] : f.n[t]), hurt = can ? float(f.n[t] - f.dmg[t]) : 0.f;
            v += harmWish(g, p, q) * shipCost(g, q, t) * (dead + 0.3f * hurt);
        }
    }
    return v;
}
int bestArgentSystem(const Game& g, int p, int sys, float* value) {
    int best = -1;
    float bv = 0.f;
    for (int s : nearSystems(g, sys)) {
        if (hasShips(g, p, s)) continue;
        const float v = argentValue(g, p, s);
        if (v > bv) bv = v, best = s;
    }
    if (value) *value = bv;
    return best;
}
// (0.82.3) Mirrorforge: the ships q would send from a (the cheapest first, until 4 resources' worth) and how good the fight is for us
Force mirrorforgePull(const Game& g, int q, int a) {
    Force out, f = g.systems[size_t(a)].space[size_t(q)];
    int worth = 0;
    for (int guard = 0; guard < 40 && worth < 8; ++guard) {
        int best = -1;
        for (int t = 0; t < kUnitTypes; ++t)
            if (f.n[t] && isShip(UnitType(t)) && UnitType(t) != UnitType::Fighter && unitDef(g, q, UnitType(t)).move > 0 &&
                (best < 0 || unitCost2(g, q, UnitType(t)) < unitCost2(g, q, UnitType(best))))
                best = t;
        if (best < 0) break;
        f.remove(UnitType(best), 1, false);
        out.add(UnitType(best), 1);
        worth += unitCost2(g, q, UnitType(best));
    }
    return out;
}
float mirrorforgeValue(const Game& g, int p, int sys, int a, int q) {
    if (q == p || mawBlocks(g, q, a)) return 0.f;
    const Force pulled = mirrorforgePull(g, q, a);
    if (pulled.nonFighterShips() <= 0) return 0.f;
    const Force& mine = g.systems[size_t(sys)].space[size_t(p)];
    if (mine.ships() <= 0) return 0.f;  // (no fight without our ships there)
    Force theirs = g.systems[size_t(sys)].space[size_t(q)];
    for (int t = 0; t < kUnitTypes; ++t) theirs.n[t] = uint8_t(theirs.n[t] + pulled.n[t]);
    const float odds = fightOdds(g, p, mine, q, theirs, true);
    if (odds < 0.65f) return 0.f;
    float worth = 0.f;
    for (int t = 0; t < kUnitTypes; ++t) worth += float(pulled.n[t]) * shipCost(g, q, t);
    return harmWish(g, p, q) * worth * odds * 0.6f;
}
int bestMirrorforge(const Game& g, int p, int sys, float* value) {
    int best = -1;
    float bv = 0.f;
    for (int16_t a : g.systems[size_t(sys)].adj)
        for (int q = 0; q < kSides; ++q) {
            if (q == p || (q >= g.numPlayers && q != kNeutral)) continue;
            const float v = mirrorforgeValue(g, p, sys, a, q);
            if (v > bv) bv = v, best = a * 16 + q;
        }
    if (value) *value = bv;
    return best;
}
// a planet's need for ground forces: enemies near, planet worth
float planetNeed(const Game& g, int p, int x) {
    const PlanetState& ps = g.planets[size_t(x)];
    float v = 0.2f + 0.1f * aiPlanetValue(g, p, x);
    if (aiEnemyNear(g, p, ps.system, 1)) v += 2.f;
    else if (aiEnemyNear(g, p, ps.system, 2)) v += 0.8f;
    v -= 0.35f * float(ps.units[size_t(p)].groundForces());
    if (g.systems[size_t(ps.system)].home == p) v -= 0.8f;
    return v;
}
}  // namespace

// The value of a monument card's ACTION now (component actions 3100 + card)
float aiMonumentActionValue(const Game& g, int p, int card) {
    if (card < 0 || card >= kMonumentCount) return 0.f;
    const char* k = kMonuments[card].key;
    int sys = -1, planet = -1;
    if (!monAbilityAt(g, p, k, &sys, &planet)) return 0.f;
    if (!std::strcmp(k, "argent")) {
        float v = 0.f;
        bestArgentSystem(g, p, sys, &v);
        return v >= 2.f ? std::min(4.f, 0.5f * v) : 0.f;
    }
    if (!std::strcmp(k, "titans")) return g.round >= 2 ? 1.2f : 0.6f;  // a sleeper (a PDS later with Awaken)
    if (!std::strcmp(k, "mortheus")) {  // (0.82.3) Mirrorforge: a fight we're likely to win against ships we'd like gone
        float v = 0.f;
        bestMirrorforge(g, p, sys, &v);
        return v >= 1.5f ? std::min(5.f, v) : 0.f;
    }
    if (!std::strcmp(k, "yin")) {
        if (planet < 0) return 0.f;
        const int n = g.planets[size_t(planet)].units[size_t(p)].count(UnitType::Infantry) / 2;
        float best = 0.f;
        for (int x : controlledPlanets(g, p))
            if (x != planet) best = std::max(best, planetNeed(g, p, x));
        return n > 0 && best > 1.f ? std::min(3.f, 0.6f * float(n) * best) : 0.f;
    }
    return 0.f;
}

namespace {
Answer pickOne(int id) {
    Answer a;
    a.picks.push_back(id);
    return a;
}
bool hasOpt(const Decision& d, int id) {
    for (const Option& o : d.options)
        if (o.id == id && o.enabled) return true;
    return false;
}
// Tag::MonumentAbility
Answer abilityAnswer(const Game& g, const Decision& d) {
    const int p = d.player;
    const int card = d.ability;
    int sys = -1, planet = -1;
    if (card >= 0 && card < kMonumentCount) monAbilityAt(g, p, kMonuments[card].key, &sys, &planet);
    auto best = [&](auto score, float bar) {
        int b = hasOpt(d, -1) ? -1 : (d.options.empty() ? -1 : d.options[0].id);
        float bv = bar;
        for (const Option& o : d.options) {
            if (!o.enabled || o.id == -1) continue;
            const float v = score(o.id);
            if (v > bv) bv = v, b = o.id;
        }
        return pickOne(b);
    };
    switch (d.value) {
        case kMonAskArborec:  // an uncontrolled planet: taken; another's with their units: coexisting (counts for objectives)
            return best(
                [&](int x) {
                    const PlanetState& ps = g.planets[size_t(x)];
                    bool others = false;
                    int theirGf = 0;
                    for (int q = 0; q < kSides; ++q)
                        if (q != p && !ps.units[size_t(q)].empty()) others = true, theirGf += ps.units[size_t(q)].groundForces();
                    if (ps.owner == p) return planetNeed(g, p, x);
                    if (!others) return 2.f + 0.3f * aiPlanetValue(g, p, x);
                    return theirGf >= 3 ? 0.3f : 1.f + 0.1f * aiPlanetValue(g, p, x);
                },
                -1e9f);
        case kMonAskArgent: return pickOne(bestArgentSystem(g, p, sys, nullptr));
        case kMonAskCreuss: {
            Answer a;
            for (const Option& o : d.options) {
                if (!o.enabled) continue;
                float v = 0.f;
                for (int q = 0; q < kSides; ++q)
                    if (q != p && (q < g.numPlayers || q == kNeutral) && hasShips(g, q, o.id)) v += harmWish(g, p, q) - 0.5f;
                // (owner M5: our own ships there take a hit too) cheap when one of them can sustain it, else a ship is lost
                if (hasShips(g, p, o.id)) {
                    const Force& f = g.systems[size_t(o.id)].space[size_t(p)];
                    float cheapest = 1e9f;
                    bool absorb = false;
                    for (int t = 0; t < kUnitTypes; ++t) {
                        if (!f.n[t] || !isShip(UnitType(t))) continue;
                        if (unitDef(g, p, UnitType(t)).sustain && f.n[t] > f.dmg[t] && unitSustainsAt(g, p, UnitType(t), int(o.id), -1)) absorb = true;
                        cheapest = std::min(cheapest, shipCost(g, p, t));
                    }
                    v -= absorb ? 0.4f : 0.6f + 1.2f * cheapest;
                }
                if (v > 0.2f) a.picks.push_back(o.id);
            }
            return a;
        }
        case kMonAskJolnar:  // the most infantry, where they're needed most
            return best([&](int id) { return float(id % 4) + 0.2f * planetNeed(g, p, id / 4); }, -1e9f);
        case kMonAskL1z1x:  // our own token off a system next to it with our ships or production there (it can be activated again)
            return best(
                [&](int id) {
                    const int from = id / 16, q = id % 16;
                    if (q != p) return -1.f;
                    float v = 0.f;
                    if (hasShips(g, p, from)) v += 1.2f;
                    if (productionCapacity(g, p, from) > 0) v += 1.f;
                    if (hasShips(g, p, sys) || productionCapacity(g, p, sys) > 0) v -= 1.f;  // (the monument's system gets locked)
                    return v;
                },
                0.9f);
        case kMonAskMahact:  // another player's token (Edict) first, then their own
            return best([&](int q) { return q == p ? 1.f : 2.f; }, 0.f);
        case kMonAskMentak:
            return best(
                [&](int s) {
                    float v = 0.f;
                    bool victimActive = d.targetPlayer == p;
                    for (int q = 0; q < kSides; ++q) {
                        if (q >= g.numPlayers && q != kNeutral) continue;
                        const Force& f = g.systems[size_t(s)].space[size_t(q)];
                        for (int t = 0; t < kUnitTypes; ++t)
                            if (f.n[t] > f.dmg[t] && isShip(UnitType(t)) && unitDef(g, q, UnitType(t)).sustain) {
                                v += (q == p ? -1.5f : harmWish(g, p, q)) * shipCost(g, q, t) * float(f.n[t] - f.dmg[t]);
                                if (q == d.targetPlayer && q != p) victimActive = true;
                            }
                    }
                    return victimActive ? v : 0.f;
                },
                5.f);
        case kMonAskMuaat: return pickOne(0);  // the votes
        case kMonAskNorr:
            return best(
                [&](int id) {
                    if (id >= 50000) return 0.6f + (aiEnemyNear(g, p, id - 50000, 1) ? 0.8f : 0.f);
                    return 0.4f + planetNeed(g, p, id);
                },
                -1e9f);
        case kMonAskNorrReplace:  // a space dock where there's none, else a PDS: the monument comes back to be placed again
            return pickOne(hasOpt(d, int(UnitType::SpaceDock)) ? int(UnitType::SpaceDock) : hasOpt(d, int(UnitType::PDS)) ? int(UnitType::PDS) : -1);
        case kMonAskNomad: {
            const bool gone = unitsOnBoard(g, p, UnitType::Flagship) == 0;
            const bool front = sys >= 0 && aiEnemyNear(g, p, sys, 2);
            return pickOne(gone && g.round >= 2 && spendable(g, p, false) >= 12 && (front || g.round >= 4) ? 1 : 0);
        }
        case kMonAskSaar:
            return best([&](int s) { return float(g.systems[size_t(s)].space[size_t(p)].nonFighterShips()) + 0.01f * float(s); }, -1e9f);
        case kMonAskSol: {  // infantry made at home go to the front (the Cenotaph's planet), others stay where they were placed
            int most = 0;
            for (const Option& o : d.options) most = std::max(most, o.id);
            const bool frontline = sys >= 0 && aiEnemyNear(g, p, sys, 2);
            const bool atHome = d.planet >= 0 && g.systems[size_t(g.planets[size_t(d.planet)].system)].home == p;
            return pickOne(frontline && atHome ? std::max(0, most - (most > 2 ? 1 : 0)) : 0);
        }
        case kMonAskSolCommit: return pickOne(0);
        case kMonAskTitans:
            return best(
                [&](int x) {
                    const PlanetState& ps = g.planets[size_t(x)];
                    if (ps.owner == p) return 1.f + (ps.units[size_t(p)].count(UnitType::PDS) ? 0.f : 0.6f) + 0.05f * aiPlanetValue(g, p, x);
                    if (ps.owner < 0) return 0.8f;
                    return 0.5f;
                },
                -1e9f);
        case kMonAskCabal: return pickOne(1);
        case kMonAskWinnuTake: {
            int most = 0;
            for (const Option& o : d.options) most = std::max(most, o.id);
            return pickOne(most);
        }
        case kMonAskWinnuStore: {  // only with Mecatol Rex in hand (where the vault opens) and plenty to spare
            bool mec = false, owes = false;
            for (int x : controlledPlanets(g, p))
                if (g.planets[size_t(x)].system == g.mecatol()) mec = true;
            for (const Promise& pr : g.promises)  // (a loan to pay back: keep everything for it)
                if (pr.state == kPromiseActive && pr.by == p && pr.kind == PromiseKind::Repay) owes = true;
            return pickOne(mec && !owes && g.round >= 3 && spendable(g, p, false, false) >= 9 && hasOpt(d, 1) ? 1 : 0);
        }
        case kMonAskXxcha: return pickOne(1);
        case kMonAskAtokera: return pickOne(spendable(g, p, false) >= 2 ? 1 : 0);  // (0.83.0) Deepmantle Interface: a unit when it can pay
        case kMonAskBelkosea: {  // (0.84.0) Armageddon Project: Mors (a shield there, damage far away), Glatison, Availyn, Grom, Caled
            for (int k : {kSwMors, kSwGlatison, kSwAvailyn, kSwGrom, kSwCaled})
                if (hasOpt(d, k)) return pickOne(k);
            return pickOne(d.options.empty() ? -1 : d.options[0].id);
        }
        case kMonAskBelkoseaUse:
            return best([&](int k) { return k >= 0 ? aiBrSwValue(g, p, k, d.targetPlayer, d.targetPlayer >= 0 ? g.activeSystem : -1) : 0.f; }, 0.f);
        case kMonAskYin: return best([&](int x) { return planetNeed(g, p, x); }, -1e9f);
        case kMonAskKeleresLaw: return pickOne(g.players[size_t(p)].tactic + g.players[size_t(p)].fleet + g.players[size_t(p)].strategy < 14 ? 1 : 0);
        case kMonAskKeleresToken: return pickOne(1);
        case kMonAskNekro: {  // a free token; otherwise keep the copies
            const Player& pl = g.players[size_t(p)];
            // (0.82.6) only a token on offer - since the rules pass (fixes 43) "Z" needs the Valefar Assimilator Z breakthrough
            // (the coverage sims: 2 answers asked for a "Z" that wasn't there)
            const int want = pl.nekroMon[0] < 0 ? 0 : pl.nekroMon[1] < 0 ? 1 : -1;
            return pickOne(want >= 0 && hasOpt(d, want) ? want : -1);
        }
        case kMonAskNaalu: {  // coexist when the fight is unlikely to be won
            const int q = d.targetPlayer;
            int mine = 0, theirs = 0;
            if (d.planet >= 0) {
                mine = g.planets[size_t(d.planet)].units[size_t(p)].groundForces();
                theirs = q >= 0 ? g.planets[size_t(d.planet)].units[size_t(q)].groundForces() : 0;
            }
            return pickOne(theirs >= mine ? 1 : 0);
        }
        case kMonAskHacan: return pickOne(d.options.empty() ? -1 : d.options[0].id);
        // (0.82.1) Thunder's Edge's monuments
        case kMonAskBastion:   // the 2 hits: whoever we'd most like to hurt
        case kMonAskCrimson:   // 1 hit through the breach (or none: friends are spared)
            return best([&](int q) { return harmWish(g, p, q); }, d.value == kMonAskCrimson ? 0.3f : -1e9f);
        case kMonAskDeepwrought: {  // who fights: ourselves when we'd win, else the player with the biggest fleet there
            const int s = d.options.empty() ? -1 : d.options[0].at;
            if (s < 0) break;
            float mine = 0.f;
            for (int q = 0; q < kSides; ++q) {
                if (q == p || (q >= g.numPlayers && q != kNeutral) || !hasShips(g, q, s) || !hasOpt(d, p)) continue;
                mine = std::max(mine, fightOdds(g, p, g.systems[size_t(s)].space[size_t(p)], q, g.systems[size_t(s)].space[size_t(q)], true));
            }
            if (mine >= 0.55f) return pickOne(p);
            return best([&](int q) { return q == p ? -1.f : float(g.systems[size_t(s)].space[size_t(q)].nonFighterShips()); }, -1e9f);
        }
        case kMonAskDeepwroughtFoe: {  // the fight we're likeliest to win, against someone we'd hurt
            const int s = d.options.empty() ? -1 : d.options[0].at;
            if (s < 0) break;
            return best(
                [&](int q) {
                    const float odds = fightOdds(g, p, g.systems[size_t(s)].space[size_t(p)], q, g.systems[size_t(s)].space[size_t(q)], true);
                    return odds * (0.6f + std::max(0.f, harmWish(g, p, q)));
                },
                -1e9f);
        }
        case kMonAskEpiphanySkip:  // our own token first - the others may still go onto plot cards
            return best([&](int q) { return q == p ? 2.f : 1.f + 0.1f * float(g.players[size_t(q)].vp); }, -1e9f);
        case kMonAskEpiphanyPlot:  // another puppet for our plots: the leaders first
            return best([&](int id) { return 1.f + 0.2f * float(g.players[size_t(id % 16)].vp); }, 0.f);
        case kMonAskObsidian:  // our infantry onto their best planet (they take it once theirs are gone)
            return best([&](int id) { return 0.5f + 0.6f * float(id % 4) + 0.15f * aiPlanetValue(g, p, id / 4); }, 0.f);
        case kMonAskRalnel:  // another player's space structure into our fleet's system, where it falls; else leave things be
            return best(
                [&](int id) {
                    const int t = id % 16, q = (id / 16) % 16, s = id / 256;
                    if (q == p || q >= g.numPlayers) return -1.f;
                    bool trap = false;
                    for (int16_t a : g.systems[size_t(s)].adj)
                        if (int(a) != s && hasShips(g, p, a) && !hasShips(g, q, a) && !(g.systems[size_t(a)].d().anomalies & kSupernova)) trap = true;
                    if (!trap) return -1.f;
                    return harmWish(g, p, q) * (t == int(UnitType::Monument) ? 2.f : 1.2f);
                },
                0.4f);
        // (0.82.2) Discordant Stars, batch 1
        case kMonAskLizho:  // a command token for us; else our own token off (the system is ours to activate again)
            if (hasOpt(d, -1)) return pickOne(-1);
            if (hasOpt(d, p)) return pickOne(p);
            return best([&](int q) { return -harmWish(g, p, q); }, -1e9f);
        case kMonAskEdyn:  // a free mech for the fight
            return pickOne(1);
        case kMonAskKortali:  // the hit: whoever we'd most like to hurt
            return best([&](int q) { return harmWish(g, p, q); }, -1e9f);
        // (0.82.3) Discordant Stars, batch 2
        case kMonAskMyko: return pickOne(1);  // a free fighter
        case kMonAskDihmohn: {  // only when a ship there gets somewhere it couldn't
            const int act = g.activeSystem;
            if (act < 0 || sys < 0) return pickOne(0);
            const int b = moveBonusMax(g, p);
            for (int t = 0; t < kUnitTypes; ++t) {
                const Force& f = g.systems[size_t(sys)].space[size_t(p)];
                if (!f.n[t] || !isShip(UnitType(t)) || (UnitType(t) == UnitType::Fighter && !fightersFree(g, p))) continue;
                if (!shipReach(g, p, sys, act, UnitType(t), b).ok && shipReach(g, p, sys, act, UnitType(t), b + 1).ok) return pickOne(1);
            }
            return pickOne(0);
        }
        case kMonAskVaden: return pickOne(harmWish(g, p, d.targetPlayer) > 0.2f ? 1 : 0);  // (not from a friend we keep promises with)
        case kMonAskFlorzen: {  // only those that must go (beyond the capacity): the rest fight where they were built
            int least = 1 << 20;
            for (const Option& o : d.options) least = std::min(least, o.id);
            return pickOne(least);
        }
        case kMonAskFlorzenOut: {  // all that fit
            int most = -1;
            for (const Option& o : d.options) most = std::max(most, o.id);
            return pickOne(most);
        }
        case kMonAskMortheus: {
            const int c = bestMirrorforge(g, p, sys, nullptr);
            return pickOne(c >= 0 && hasOpt(d, c) ? c : -1);
        }
        case kMonAskMortheusShip:  // (asked of the owner of the ships) the cheapest first, the weakest of equals
            return best([&](int t) { return -100.f * float(unitCost2(g, p, UnitType(t))) + float(unitDef(g, p, UnitType(t)).combat); }, -1e9f);
        case kMonAskAugurs: {  // trade goods for our scored secrets - worth more than the other planet's exploration
            int secrets = 0;
            for (int16_t o : g.players[size_t(p)].scored)
                if (scoredAsSecret(g, p, o)) ++secrets;
            return pickOne(secrets >= 1 ? 1 : 0);
        }
        case kMonAskTnelisLoss: return pickOne(1);  // nobody left to take the planet
        case kMonAskTnelis:  // the most valuable unit of whoever we'd most like to hurt
            return best([&](int c) {
                        const int t = c % 16, q = (c / 16) % 16;
                        return harmWish(g, p, q) * (1.f + 0.5f * float(unitCost2(g, q, UnitType(t))) + (isGroundForce(UnitType(t)) ? 1.f : 0.f));
                    },
                        0.1f);
        case kMonAskVaylerian: return pickOne(1);  // an action card
        case kMonAskFsc:  // a command token for showing a card
            for (const Option& o : d.options)
                if (o.id >= 0 && o.enabled) return pickOne(o.id);
            return pickOne(-1);
        // (0.82.4) Discordant Stars, batch 3
        case kMonAskKollecc: return pickOne(hasOpt(d, 100) ? 100 : -1);  // the whole cloaked fleet
        case kMonAskBentor: return pickOne(1);  // a free monument and a free exploration
        case kMonAskKjalengard: {  // our token is worth it unless the victor is someone we fight
            const int w = d.targetPlayer;
            return pickOne(w < 0 || w == p || harmWish(g, p, w) < 1.2f ? 1 : 0);
        }
        case kMonAskKjalengardWin: return pickOne(1);
        case kMonAskLanefir: return pickOne(1);  // (our own planet stays ready)
        case kMonAskKyro: return best([&](int x) { return aiPlanetValue(g, p, x) - (aiEnemyNear(g, p, g.planets[size_t(x)].system, 1) ? 2.f : 0.f); }, -1e9f);
        case kMonAskKolume: {  // a shot at the ships we'd most like gone, through the chain if needed
            auto fireValue = [&](int x, int q) {
                if (q == p) return 0.f;
                float w = 0.f;
                const Force& f = g.systems[size_t(x)].space[size_t(q)];
                for (int t = 0; t < kUnitTypes; ++t) w += float(f.n[t]) * shipCost(g, q, t);
                return harmWish(g, p, q) * std::min(w, 6.f);
            };
            auto bestFrom = [&](int start) {  // the best target reachable from a system (hops through systems with SPACE CANNON)
                std::vector<int> seen{start}, todo{start};
                float bv = 0.f;
                for (size_t i = 0; i < todo.size() && i < 40; ++i) {
                    const int s0 = todo[i];
                    std::vector<int> nb{s0};
                    for (int16_t a : g.systems[size_t(s0)].adj) nb.push_back(a);
                    for (int x : nb) {
                        if (g.systems[size_t(x)].isHyperlane()) continue;
                        bool cannon = false;
                        for (int q = 0; q < kSides && !cannon; ++q) {
                            if (q >= g.numPlayers && q != kNeutral) continue;
                            auto any = [&](const Force& f) {
                                for (int t = 0; t < kUnitTypes; ++t)
                                    if (f.n[t] && unitDef(g, q, UnitType(t)).cannon > 0) return true;
                                return false;
                            };
                            cannon = any(g.systems[size_t(x)].space[size_t(q)]);
                            for (int16_t y : g.systems[size_t(x)].planets) cannon = cannon || any(g.planets[size_t(y)].units[size_t(q)]);
                        }
                        if (!cannon) continue;
                        for (int q = 0; q < kSides; ++q)
                            if ((q < g.numPlayers || q == kNeutral) && hasShips(g, q, x)) bv = std::max(bv, fireValue(x, q) * (i == 0 ? 1.f : 0.9f));
                        if (std::find(seen.begin(), seen.end(), x) == seen.end()) seen.push_back(x), todo.push_back(x);
                    }
                }
                return bv;
            };
            // (mandatory: with no "don't" the least bad shot - the bar only matters when declining is offered)
            return best([&](int id) { return id >= 100000 ? 0.95f * bestFrom(id - 100000) : fireValue(id / 16, id % 16); },
                        hasOpt(d, -1) ? 0.3f : -1e9f);
        }
        case kMonAskGledge: {  // blank the laws for ourselves when one binds us, or for the leader who owns one
            int lead = -1;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && (lead < 0 || g.players[size_t(q)].vp > g.players[size_t(lead)].vp)) lead = q;
            for (const Law& l : g.laws)
                if (l.owner >= 0 && l.owner == lead && g.players[size_t(lead)].vp >= g.vpGoal - 4 && hasOpt(d, lead)) return pickOne(lead);
            static const char* kBinding[] = {"regulations", "conscription", "absol_travelban", "absol_articleswar", "warcrimes", "conventions"};
            for (const Law& l : g.laws)
                for (const char* k : kBinding)
                    if (!std::strcmp(kAgendas[l.agenda].key, k) && hasOpt(d, p)) return pickOne(p);
            return pickOne(-1);
        }
        case kMonAskGledgeCores: {  // the 2 Core planets worth least (resources + influence)
            std::vector<std::pair<float, int>> v;
            for (const Option& o : d.options)
                if (o.enabled && o.id >= 0) v.push_back({float(planetResources(g, o.id) + planetInfluence(g, o.id)), o.id});
            std::sort(v.begin(), v.end());
            Answer a;
            for (size_t i = 0; i < v.size() && i < 2; ++i) a.picks.push_back(v[i].second);
            return a;
        }
        case kMonAskRhodun: {  // the back when the Fracture's players are near (it bars them and gives +2 against them)
            int frac = 0;
            for (int q = 0; q < g.numPlayers; ++q) {
                if (q == p) continue;
                bool in = false;
                for (size_t s = 0; s < g.systems.size() && !in; ++s) in = g.systems[s].fracture && hasUnits(g, q, int(s));
                if (in && aiEnemyNear(g, p, sys, 2)) ++frac;
            }
            return pickOne(frac > 0 ? 1 : 0);
        }
        case kMonAskRhodunBack: return pickOne(1);  // (asked only when the front helps in this combat and the back doesn't)
        case kMonAskCeldauri:  // a ground force to a dock planet that has none, while 2 stay on the Relay's planet
            return best([&](int c) {
                        const int y = c / 16;
                        int x = -1;
                        monAbilityAt(g, p, "celdauri", nullptr, &x);
                        if (x < 0 || g.planets[size_t(x)].units[size_t(p)].groundForces() <= 2) return -1.f;
                        return g.planets[size_t(y)].units[size_t(p)].groundForces() == 0 ? planetNeed(g, p, y) : -1.f;
                    },
                        0.5f);
        case kMonAskRalnelTo: {
            const int q = d.targetPlayer;
            return best(
                [&](int a) {
                    if (q == p) return aiEnemyNear(g, p, a, 1) ? -1.f : 0.5f;
                    return (hasShips(g, p, a) ? 2.f : 0.f) - (q >= 0 && q < kSides && hasShips(g, q, a) ? 3.f : 0.f);
                },
                -1e9f);
        }
        default: break;
    }
    // a yes / no: yes; otherwise the first option
    if (hasOpt(d, 1) && hasOpt(d, 0) && d.options.size() == 2) return pickOne(1);
    return pickOne(d.options.empty() ? -1 : d.options[0].id);
}
}  // namespace

bool aiMonumentAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out) {
    (void)rng;
    if (d.tag == Tag::MonumentAbility) {
        if (d.kind == DecisionKind::Pick) {
            out = abilityAnswer(g, d);
            // (pick many: an empty answer is "none")
            if (d.maxPick > 1 || d.minPick == 0) return true;
            if (out.picks.size() != 1) out = pickOne(d.options.empty() ? -1 : d.options[0].id);
            return true;
        }
        return false;
    }
    if (d.tag != Tag::MonumentPlace) return false;
    const int p = d.player;
    int best = -1;
    float bv = 0.5f;  // (worth less than this: keep it for a better moment - "Not now" is -1)
    for (const Option& o : d.options) {
        if (!o.enabled || !isMonumentOption(o.id)) continue;
        const float v = aiMonumentSpotValue(g, p, o.id);
        if (v > bv) bv = v, best = o.id;
    }
    out = one(best);
    return true;
}


// ================================================================== Monuments+'s cards (0.82.0, rules_monuments3.cpp)
namespace {
const UnitType kStructs[] = {UnitType::PDS, UnitType::SpaceDock, UnitType::Monument};
bool holdsSecret(const Game& g, int p, const char* k) {
    for (int16_t o : g.players[size_t(p)].secrets)
        if (!std::strcmp(kObjectives[o].key, k)) return true;
    return false;
}
bool cathedralPoint(const Game& g, int q, int planet) {  // q holds the Cathedral of Ixth's point for its monument on this planet
    for (const Law& l : g.laws)
        if (l.agenda >= 0 && !std::strcmp(kAgendas[l.agenda].key, "cathedralofixth") && l.target == planet)
            return q >= 0 && q < g.numPlayers && g.players[size_t(q)].cathedralVP == 1;
    return false;
}
// Festival on planet x: readied (its value once more this round), and with our monument there 4 infantry or a mech
float festivalValue(const Game& g, int p, int x) {
    const PlanetState& ps = g.planets[size_t(x)];
    float v = ps.exhausted ? 0.25f * float(planetResources(g, x) + planetInfluence(g, x)) : 0.f;
    if (ps.units[size_t(p)].count(UnitType::Monument)) {
        v += 1.1f;
        if (holdsSecret(g, p, "dagw") && monumentGuards(g, p) < 3) v += 2.f;  // Defend a Great Work
        if (aiEnemyNear(g, p, ps.system, 1)) v += 0.5f;                       // a guard where it's needed
    }
    return v;
}
// Rebel Bombing: what destroying q's structure t at planet / sys does to them (in our wish to hurt them)
float bombValue(const Game& g, int p, int q, UnitType t, int planet, int sys) {
    float v = t == UnitType::PDS ? 0.5f : t == UnitType::SpaceDock ? 1.4f : 1.6f;
    if (t == UnitType::SpaceDock && planet < 0) v += 0.3f;                               // a floating factory and its cargo
    if (t == UnitType::Monument && planet >= 0 && cathedralPoint(g, q, planet)) v += 3.f;  // the Cathedral of Ixth's point goes too
    if (t == UnitType::SpaceDock && aiEnemyNear(g, q, sys, 1)) v += 0.4f;                // a forward base
    (void)sys;
    return v * std::max(0.f, harmWish(g, p, q));
}
float bestBomb(const Game& g, int p, int* id) {
    float best = 0.f;
    if (id) *id = -1;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p || g.players[size_t(q)].eliminated || !areNeighbours(g, p, q)) continue;
        for (size_t s = 0; s < g.systems.size(); ++s) {
            const SystemState& sy = g.systems[s];
            if (sy.home >= 0 || sy.offBoard) continue;
            for (UnitType t : kStructs) {
                if (sy.space[size_t(q)].count(t)) {
                    const float v = bombValue(g, p, q, t, -1, int(s));
                    if (v > best) {
                        best = v;
                        if (id) *id = q * 10000000 + int(t) * 100000 + 50000 + int(s);
                    }
                }
                for (int16_t x : sy.planets)
                    if (g.planets[size_t(x)].units[size_t(q)].count(t)) {
                        const float v = bombValue(g, p, q, t, x, int(s));
                        if (v > best) {
                            best = v;
                            if (id) *id = q * 10000000 + int(t) * 100000 + x;
                        }
                    }
            }
        }
    }
    return best;
}
// Renovation: what one of our structures is worth where it is, and what a new one there would be
float keepWorth(const Game& g, int p, UnitType t, int planet, int sys) {
    if (t == UnitType::Monument) return 6.f;  // (its card works only where it stands - moved only by Construction)
    if (t == UnitType::SpaceDock) return g.systems[size_t(sys)].home == p ? 6.f : planet < 0 ? 2.f : 1.8f;
    return 0.5f;
}
float newWorth(const Game& g, int p, UnitType t, int planet, int sys) {
    const Goals& G = aiGoals(g, p);
    if (t == UnitType::Monument) return 0.6f * aiMonumentSpotValue(g, p, monumentOptionId(planet, sys));
    if (t == UnitType::SpaceDock) {
        float v = planet >= 0 ? 1.f + 0.2f * float(planetResources(g, planet)) : 1.2f;
        for (int16_t x : g.systems[size_t(sys)].planets)  // (another dock in the same system adds little)
            if (x != planet && g.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock)) v -= 0.8f;
        return v + G.dock + G.structure + (g.systems[size_t(sys)].home < 0 ? G.structOutside : 0.f);
    }
    return 0.5f + G.pds + G.structure + (g.systems[size_t(sys)].home < 0 ? G.structOutside : 0.f);
}
// the best renovation: (place id as rules_monuments3.cpp encodes it, the type) and its gain
float bestRenovation(const Game& g, int p, int* place, int* type) {
    float best = 0.f;
    if (place) *place = -1;
    if (type) *type = -1;
    auto consider = [&](UnitType out, int planet, int sys) {
        for (UnitType in : kStructs) {
            if (in == out || unitsAvailable(g, p, in) <= 0) continue;
            // (the same checks as Rules::renovationTypes)
            if (planet >= 0) {
                const PlanetState& ps = g.planets[size_t(planet)];
                if (ps.owner != p || isDMZ(g, planet)) continue;
                const Force& f = ps.units[size_t(p)];
                const int less = f.count(in) - (in == out ? 1 : 0);
                if (in == UnitType::PDS && (unitDef(g, p, in).spaceOnly || (less >= 2 && !lawInPlay(g, "defense_act")))) continue;
                if (in == UnitType::SpaceDock && (unitDef(g, p, in).spaceOnly || less >= 1)) continue;
                if (in == UnitType::Monument && (playerMonument(g, p) < 0 || !monumentFitsPlanet(g, p, planet))) continue;
            } else {
                if (in == UnitType::PDS) continue;
                if (in == UnitType::SpaceDock) {
                    bool mine = false;
                    for (int16_t x : g.systems[size_t(sys)].planets) mine = mine || g.planets[size_t(x)].owner == p;
                    if (!unitDef(g, p, in).spaceOnly || !mine) continue;
                }
                if (in == UnitType::Monument && (playerMonument(g, p) < 0 || !monumentFitsSpace(g, p, sys))) continue;
            }
            const float v = newWorth(g, p, in, planet, sys) - keepWorth(g, p, out, planet, sys);
            if (v > best) {
                best = v;
                if (place) *place = int(out) * 100000 + (planet >= 0 ? planet : 50000 + sys);
                if (type) *type = int(in);
            }
        }
    };
    for (size_t s = 0; s < g.systems.size(); ++s) {
        for (UnitType t : kStructs) {
            if (g.systems[s].space[size_t(p)].count(t)) consider(t, -1, int(s));
            for (int16_t x : g.systems[s].planets)
                if (g.planets[size_t(x)].units[size_t(p)].count(t)) consider(t, x, int(s));
        }
    }
    return best;
}
}  // namespace

// the value of playing one of Monuments+'s action cards now (a component action - aiComponentValue's scale)
float aiMonCardValue(const Game& g, int p, int card) {
    const char* k = card >= 0 && card < kActionCardCount ? kActionCards[card].key : "";
    if (!std::strcmp(k, "festival")) {
        float best = 0.f;
        for (size_t x = 0; x < g.planets.size(); ++x) {
            const PlanetState& ps = g.planets[x];
            if (ps.destroyed || isHomePlanet(g, int(x)) || isStation(g, int(x)) || !ps.units[size_t(p)].structures()) continue;
            best = std::max(best, festivalValue(g, p, int(x)));
        }
        return best >= 0.7f ? std::min(2.2f, best) : 0.f;
    }
    if (!std::strcmp(k, "rebelbombing")) {
        if (g.players[size_t(p)].tradeGoods < 2) return 0.f;
        const float b = bestBomb(g, p, nullptr) - 0.6f;  // (2 trade goods)
        return b >= 0.6f ? std::min(2.2f, b) : 0.f;
    }
    if (!std::strcmp(k, "renovation")) {
        const float b = bestRenovation(g, p, nullptr, nullptr);
        return b >= 1.f ? std::min(2.f, 0.5f * b) : 0.f;
    }
    return 0.f;
}

// the choices while resolving them (Tag::CardTarget, d.card)
bool aiMonCardTarget(const Game& g, const Decision& d, Answer& out) {
    const int p = d.player, c = d.card;
    const char* k = c >= 0 && c < kActionCardCount ? kActionCards[c].key : "";
    auto bestBy = [&](auto score) {
        int best = -1;
        float bv = -1e9f;
        for (const Option& o : d.options) {
            if (!o.enabled) continue;
            const float v = score(o.id);
            if (v > bv) bv = v, best = o.id;
        }
        return best;
    };
    if (!std::strcmp(k, "festival")) {
        if (d.prompt.find("ready which planet") != std::string::npos) {
            out = one(bestBy([&](int x) { return festivalValue(g, p, x); }));
            return true;
        }
        // 4 infantry (Defend a Great Work counts them; more bodies) - a mech when we chase Mechanize the Military
        const bool mech = aiGoals(g, p).mech > 0.f;
        out = one(bestBy([&](int id) { return id == 1 ? (mech ? 2.f : 0.5f) : 1.f; }));
        return true;
    }
    if (!std::strcmp(k, "rebelbombing")) {
        out = one(bestBy([&](int id) {
            const int q = id / 10000000, rest = id % 10000000, loc = rest % 100000;
            const int planet = loc < 50000 ? loc : -1, sys = planet >= 0 ? g.planets[size_t(planet)].system : loc - 50000;
            return bombValue(g, p, q, UnitType(rest / 100000), planet, sys);
        }));
        return true;
    }
    if (!std::strcmp(k, "renovation")) {
        int place = -1, type = -1;
        bestRenovation(g, p, &place, &type);
        if (d.prompt.find("replace which") != std::string::npos) {
            out = one(place >= 0 && hasOpt(d, place) ? place : d.options.front().id);
            return true;
        }
        // the replacement: what bestRenovation chose for this place, else the most useful one offered
        if (type >= 0 && hasOpt(d, type)) {
            out = one(type);
            return true;
        }
        out = one(bestBy([&](int t) { return t == int(UnitType::Monument) ? 3.f : t == int(UnitType::SpaceDock) ? 2.f : 1.f; }));
        return true;
    }
    return false;
}

// the value of a Monuments+ law's outcome for p (outcomeValue's "victory point" scale)
float aiMonOutcomeValue(const Game& g, int p, int a, int o) {
    const char* k = kAgendas[a].key;
    if (!std::strcmp(k, "cathedralofixth")) {  // a point now for the planet's owner if their monument stands there; later for a placement
        if (o < 0 || o >= int(g.planets.size())) return 0.f;
        const PlanetState& ps = g.planets[size_t(o)];
        const int q = ps.owner;
        float v = 0.f;
        if (q == p) {
            if (ps.units[size_t(p)].count(UnitType::Monument)) v += 3.f;
            else if (playerMonument(g, p) >= 0 && unitsAvailable(g, p, UnitType::Monument) > 0 && monumentFitsPlanet(g, p, o)) v += 1.2f;
            v += 0.2f;
        } else if (q >= 0) {
            if (ps.units[size_t(q)].count(UnitType::Monument)) v -= 1.5f * std::max(0.3f, harmWish(g, p, q));
            else v -= 0.2f;
        }
        return v;
    }
    if (!std::strcmp(k, "ministerofculture")) {  // +2 commodities while our monument is on the board, and a refill now
        if (o == p) return unitsOnBoard(g, p, UnitType::Monument) > 0 ? 1.4f : 0.5f;
        return -0.2f * std::max(0.3f, harmWish(g, p, o));
    }
    return 0.f;
}

}  // namespace ti
