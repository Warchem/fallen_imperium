// Monuments+ (0.82.0; Cacotopos, after Blarknob's Monuments) - module kModMonuments.
//
// Each faction with a monument card gets its monument unit (UnitType::Monument, one piece) when the module is on. Monuments
// are structures (LRR 79 applies: they can't move, aren't produced, are lost with the planet's control). The Cultural
// Monuments event card: "Monuments are structures and can only be placed on planets or in systems matching their traits or
// special placement text."
//
// Construction (owner 2026-11-21: Monuments+'s card REPLACES every other Construction card, not a line added to it):
//   Primary - "Either place 1 structure on a planet you control or use the PRODUCTION ability of 1 of your space docks.
//              Place 1 structure on a planet you control. If your monument is not on the board, you may place it."
//   Secondary - "Spend 1 token from your strategy pool to place 1 non-monument structure on a planet you control."
// It is Thunder's Edge's revised card plus the monument line, so with Monuments+ the revised card is used even without
// Thunder's Edge. A monument counts as "1 structure" in the primary (Monuments+ FAQ: "Can I place a monument twice using the
// new Construction primary introduced in TE? Yep.") - and, as for any structure, with none in the reinforcements the one on
// the board may be taken back to place it again (LRR 23.4a). The third line only places it from the reinforcements.
//
// The abilities on the cards come with the rest of Monuments+ (later steps of 0.82.0).

#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

// option ids for a monument placement: on a planet, in a space area (The Panopticon)
namespace {
constexpr int kMonPlanetId = 100000, kMonSpaceId = 200000;
}

bool isMonumentOption(int id) { return id >= kMonPlanetId && id < kMonSpaceId + 100000; }
int monumentOptionId(int planet, int sys) { return planet >= 0 ? kMonPlanetId + planet : kMonSpaceId + sys; }

// The player's monument unit, when the module is on and their faction has one (setupFaction, refreshUnits)
void Rules::giveMonument(int p) {
    if (!(G.modules & kModMonuments) || p < 0 || p >= G.numPlayers) return;
    int m = monumentForFaction(G.players[size_t(p)].faction);
    if (m < 0) return;
    // (0.82.4) the Rhodun's card turned to its back
    if (G.players[size_t(p)].monFlipped && !std::strcmp(kMonuments[m].key, "rhodun_front")) m = monumentIndex("rhodun_back");
    // the sets added after 0.82.0: older games had none of these (0.82.1 Thunder's Edge, 0.82.2-0.82.4 Discordant Stars batches)
    static const struct {
        const char* set;
        int fixes;
    } kSince[] = {{"TE", 39}, {"DS1", 40}, {"DS2", 41}, {"DS3", 42}};
    for (const auto& k : kSince)
        if (!std::strcmp(kMonuments[m].set, k.set) && G.fixes < k.fixes) return;
    const int u = unitByKey(kMonuments[m].unit);
    if (u >= 0) G.players[size_t(p)].unit[int(UnitType::Monument)] = int16_t(u);
}

// Where p's monument may go now: planets they control that its card allows (DMZ excluded, as for any structure), or for The
// Panopticon any system its card allows. reinforcementsOnly: the third line of Construction, DEPLOY (FAQ: "deploy ... is only
// possible from your reinforcements"); otherwise the one on the board may be taken back for it (LRR 23.4a).
std::vector<Option> Rules::monumentSpots(int p, bool reinforcementsOnly, int onlySys) {
    std::vector<Option> opts;
    const int m = playerMonument(G, p);
    if (m < 0) return opts;
    const bool spare = unitsAvailable(G, p, UnitType::Monument) > 0;
    if (!spare && (reinforcementsOnly || unitsPlaceable(G, p, UnitType::Monument) <= 0)) return opts;
    int herePlanet = -1;
    const int hereSys = monumentSystem(G, p, &herePlanet);
    const std::string name = kUnits[G.players[size_t(p)].unit[int(UnitType::Monument)]].name;
    const std::string back = spare ? "" : " It comes off " + (herePlanet >= 0 ? planetName(herePlanet) : sysName(hereSys)) + " for it (LRR 23.4a).";
    if (kMonuments[m].places & (kMonEmpty | kMonStation)) {  // (The Panopticon, the Seraph Data Center)
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (onlySys >= 0 && int(s) != onlySys) continue;
            if (int(s) == hereSys || !monumentFitsSpace(G, p, int(s))) continue;
            opts.push_back({kMonSpaceId + int(s), name + " in " + sysName(int(s)), std::string(kMonuments[m].where) + back, true, int(s)});
        }
        return opts;
    }
    for (int x : controlledPlanets(G, p)) {
        const int s = G.planets[size_t(x)].system;
        if (onlySys >= 0 && s != onlySys) continue;
        if (x == herePlanet || isDMZ(G, x) || !monumentFitsPlanet(G, p, x)) continue;
        opts.push_back({kMonPlanetId + x, name + " on " + planetName(x), std::string(kMonuments[m].where) + back, true, s});
    }
    return opts;
}

// Place p's monument at a monumentSpots() option. The card is shown large to everyone (EventKind::MonumentPlaced, display only:
// held per the "Cards" pause setting - owner 2026-11-21 22:10).
void Rules::placeMonument(int p, int id) {
    if (!isMonumentOption(id)) return;
    const bool space = id >= kMonSpaceId;
    const int planet = space ? -1 : id - kMonPlanetId;
    const int sys = space ? id - kMonSpaceId : G.planets[size_t(planet)].system;
    if (unitsAvailable(G, p, UnitType::Monument) <= 0 && !reclaimUnit(p, UnitType::Monument, sys, false)) return;
    addUnits(p, UnitType::Monument, 1, sys, planet);
    if (space) monStationPlace(p, sys);  // (0.82.1) the Seraph Data Center: its space station card comes with it
    const int u = G.players[size_t(p)].unit[int(UnitType::Monument)];
    say(p, pn(p) + " raises their monument, " + std::string(kUnits[u].name) + ", " + (space ? "in " + sysName(sys) : "on " + planetName(planet)) + ".", sys);
    Event e;
    e.kind = EventKind::MonumentPlaced;
    e.player = int8_t(p);
    e.system = int16_t(sys);
    e.planet = int16_t(planet);
    e.count = int16_t(playerMonument(G, p));
    e.label = pn(p) + " placed their monument " + (space ? "in " + sysName(sys) : "on " + planetName(planet));  // (the card's line: the
    // feed already has the say() line above)
    E.emit(std::move(e));
    monPlaced(p);  // N'orr: "When you place your monument, produce up to a total of 3 infantry or fighters"
}

// Construction's third line: "If your monument is not on the board, you may place it."
void Rules::monumentLine(int p) {
    if (playerMonument(G, p) < 0 || unitsAvailable(G, p, UnitType::Monument) <= 0) return;
    std::vector<Option> opts = monumentSpots(p, true);
    if (opts.empty()) return;
    opts.push_back({-1, "Not now", "Keep it in your reinforcements."});
    const int m = playerMonument(G, p);
    const int c = pick(p, Tag::MonumentPlace, "Construction (3 of 3): place your monument?", opts,
                       std::string(kMonuments[m].where) + " If your monument is not on the board, you may place it.");
    if (c != -1) placeMonument(p, c);
}

}  // namespace ti
