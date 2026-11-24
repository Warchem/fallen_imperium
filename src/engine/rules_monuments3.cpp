// Monuments+ (0.82.0), the cards beside the monuments - module kModMonuments. The cards' own wording (the owner's v3.1 card
// files; card_text.cpp / data.cpp), appended to the tables (Source::Monuments) and only in the decks with the module:
//   Action cards (1 of each): Festival, Rebel Bombing, Renovation - all "ACTION:" (component actions).
//   Laws: Cathedral of Ixth (Elect Non-Home Planet - Mecatol Rex too), Minister of Culture (Elect Player); in whichever agenda
//         deck is used (Absol's Agendas too).
//   Secret objectives: Defend a Great Work (status phase; query meetsObjective "dagw"), Topple a Monument (action phase:
//         destroyUnits -> actionSecret "tam").
//   Monuments to the Ages Omega: with Monuments+ the Thunder's Edge event's monuments are "monoliths" (its wording - data.cpp
//         setMonumentsTexts, rules_events.cpp); the rules were already the Omega card's (points to the planet's controller).
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
bool key(int card, const char* k) { return card >= 0 && card < kActionCardCount && !std::strcmp(kActionCards[card].key, k); }
const UnitType kStructures[] = {UnitType::PDS, UnitType::SpaceDock, UnitType::Monument};
// Renovation / Rebel Bombing option ids: a structure type at a place (a planet, or a system's space area)
int placeId(int t, int planet, int sys) { return t * 100000 + (planet >= 0 ? planet : 50000 + sys); }
}  // namespace

bool monCard(int card) { return card >= 0 && card < kActionCardCount && kActionCards[card].source == Source::Monuments; }

// ------------------------------------------------------------------ Festival
// "ACTION: Ready a non-home planet that contains 1 of your structures. If that planet contains your monument, place 4 infantry or
// 1 mech on it."
std::vector<Option> Rules::festivalPlanets(int p) const {
    std::vector<Option> opts;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.destroyed || isHomePlanet(G, int(x)) || isStation(G, int(x)) || !ps.units[size_t(p)].structures()) continue;
        const bool mon = ps.units[size_t(p)].count(UnitType::Monument) > 0;
        opts.push_back({int(x), planetName(int(x)),
                        std::string(ps.exhausted ? "Exhausted - it is readied." : "Already ready.") +
                            (mon ? " Your monument: 4 infantry or 1 mech there." : ""),
                        true, ps.system});
    }
    return opts;
}

// ------------------------------------------------------------------ Rebel Bombing
// "ACTION: Spend 2 trade goods to destroy 1 of your neighbor's structures in a non-home system."
std::vector<Option> Rules::rebelTargets(int p) const {
    std::vector<Option> opts;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p || G.players[size_t(q)].eliminated || !areNeighbours(G, p, q)) continue;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            const SystemState& sy = G.systems[s];
            if (sy.home >= 0 || sy.offBoard) continue;
            for (UnitType t : kStructures) {
                if (sy.space[size_t(q)].count(t))  // (a floating factory, a Gauss Cannon, The Panopticon)
                    opts.push_back({q * 10000000 + placeId(int(t), -1, int(s)), pn(q) + "'s " + unitTypeName(t) + " in " + sysName(int(s)), "", true, int(s)});
                for (int16_t x : sy.planets)
                    if (G.planets[size_t(x)].units[size_t(q)].count(t))
                        opts.push_back({q * 10000000 + placeId(int(t), x, int(s)), pn(q) + "'s " + unitTypeName(t) + " on " + planetName(x), "", true, int(s)});
            }
        }
    }
    return opts;
}

// ------------------------------------------------------------------ Renovation
// "ACTION: Replace 1 of your structures with 1 structure from your reinforcements." A different structure, where it may stand:
// a PDS (2 a planet, Homeland Defense Act: any), a space dock (1 a planet; the Saar's float in space), the monument where its
// card allows (Cultural Monuments: "can only be placed on planets or in systems matching their traits or special placement
// text"). Only from the reinforcements (the card says so - no piece taken off the board, LRR 23.4a doesn't apply).
std::vector<int> Rules::renovationTypes(int p, int planet, int sys, UnitType out) const {
    std::vector<int> v;
    for (UnitType t : kStructures) {
        if (t == out || unitsAvailable(G, p, t) <= 0) continue;
        if (planet >= 0) {
            const PlanetState& ps = G.planets[size_t(planet)];
            if (ps.owner != p || isDMZ(G, planet)) continue;
            const Force& f = ps.units[size_t(p)];
            const int less = f.count(t) - (t == out ? 1 : 0);  // (what stays there of that type)
            if (t == UnitType::PDS && (unitDef(G, p, t).spaceOnly || (less >= 2 && !lawInPlay(G, "defense_act")))) continue;
            if (t == UnitType::SpaceDock && (unitDef(G, p, t).spaceOnly || less >= 1)) continue;
            if (t == UnitType::Monument && (playerMonument(G, p) < 0 || !monumentFitsPlanet(G, p, planet))) continue;
        } else {
            if (t == UnitType::PDS) continue;  // (Gauss Cannons go through Construction)
            if (t == UnitType::SpaceDock) {
                bool mine = false;  // (the Saar's floating factories: a system with a planet of theirs)
                for (int16_t x : G.systems[size_t(sys)].planets) mine = mine || G.planets[size_t(x)].owner == p;
                if (!unitDef(G, p, t).spaceOnly || !mine) continue;
            }
            if (t == UnitType::Monument && (playerMonument(G, p) < 0 || !monumentFitsSpace(G, p, sys))) continue;
        }
        v.push_back(int(t));
    }
    return v;
}

std::vector<Option> Rules::renovationChoices(int p) const {
    std::vector<Option> opts;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const SystemState& sy = G.systems[s];
        for (UnitType t : kStructures) {
            if (sy.space[size_t(p)].count(t) && !renovationTypes(p, -1, int(s), t).empty())
                opts.push_back({placeId(int(t), -1, int(s)), std::string(unitTypeName(t)) + " in " + sysName(int(s)), "", true, int(s)});
            for (int16_t x : sy.planets)
                if (G.planets[size_t(x)].units[size_t(p)].count(t) && !renovationTypes(p, x, int(s), t).empty())
                    opts.push_back({placeId(int(t), x, int(s)), std::string(unitTypeName(t)) + " on " + planetName(x), "", true, int(s)});
        }
    }
    return opts;
}

// ------------------------------------------------------------------ the cards
bool Rules::monComponentUsable(int p, int card) const {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers) return false;
    const Player& pl = G.players[size_t(p)];
    if (key(card, "festival")) return !festivalPlanets(p).empty();
    if (key(card, "rebelbombing")) return pl.tradeGoods >= 2 && !rebelTargets(p).empty();
    if (key(card, "renovation")) return !renovationChoices(p).empty();
    return false;
}

bool Rules::monResolve(int p, int card, const WinCtx* c) {
    (void)c;
    if (!monCard(card)) return false;
    Player& pl = G.players[size_t(p)];
    if (key(card, "festival")) {
        const std::vector<Option> opts = festivalPlanets(p);
        if (opts.empty()) return true;
        const int x = pick(p, Tag::CardTarget, "Festival: ready which planet?", opts, actionCardText(card), false, true);
        PlanetState& ps = G.planets[size_t(x)];
        ps.exhausted = false;
        say(p, pn(p) + " readies " + planetName(x) + " (Festival).", ps.system);
        if (!ps.units[size_t(p)].count(UnitType::Monument)) return true;
        std::vector<Option> units;
        const int inf = std::min(4, unitsForPlacement(G, p, UnitType::Infantry));
        if (inf > 0) units.push_back({0, std::to_string(inf) + " infantry", "Placed on " + planetName(x) + "."});
        if (unitsForPlacement(G, p, UnitType::Mech) > 0) units.push_back({1, "1 mech", "Placed on " + planetName(x) + "."});
        if (units.empty()) return true;
        curPlanet_ = x;
        const int u = units.size() == 1 ? units[0].id : pick(p, Tag::CardTarget, "Festival: your monument is there - place 4 infantry or 1 mech?", units);
        curPlanet_ = -1;
        const UnitType t = u == 1 ? UnitType::Mech : UnitType::Infantry;
        const int k = u == 1 ? 1 : inf;
        say(p, pn(p) + " places " + std::to_string(k) + " " + unitTypeName(t, k > 1) + " on " + planetName(x) + " (Festival).", ps.system);
        addUnits(p, t, k, ps.system, x);
        return true;
    }
    if (key(card, "rebelbombing")) {
        const std::vector<Option> opts = rebelTargets(p);
        if (opts.empty() || pl.tradeGoods < 2) return true;
        const int id = pick(p, Tag::CardTarget, "Rebel Bombing: destroy which structure?", opts, actionCardText(card), true);
        const int owner = id / 10000000, rest = id % 10000000;  // (owner, structure type, place)
        const UnitType t = UnitType(rest / 100000);
        const int loc = rest % 100000;
        const int planet = loc < 50000 ? loc : -1, sys = planet >= 0 ? G.planets[size_t(planet)].system : loc - 50000;
        pl.tradeGoods = int8_t(pl.tradeGoods - 2);
        say(p, pn(p) + " spends 2 trade goods: rebels bomb " + pn(owner) + "'s " + unitTypeName(t) + " " +
                   (planet >= 0 ? "on " + planetName(planet) : "in " + sysName(sys)) + " (Rebel Bombing).",
            sys);
        destroyUnits(owner, t, 1, sys, planet, p, "Rebel Bombing");
        if (planet < 0) removeExcessCapacity(owner, sys);  // (a floating factory's capacity is gone)
        return true;
    }
    if (key(card, "renovation")) {
        const std::vector<Option> opts = renovationChoices(p);
        if (opts.empty()) return true;
        const int id = pick(p, Tag::CardTarget, "Renovation: replace which of your structures?", opts, actionCardText(card), true);
        const UnitType out = UnitType(id / 100000);
        const int loc = id % 100000;
        const int planet = loc < 50000 ? loc : -1, sys = planet >= 0 ? G.planets[size_t(planet)].system : loc - 50000;
        std::vector<Option> with;
        for (int t : renovationTypes(p, planet, sys, out)) {
            std::string d = t == int(UnitType::Monument) ? std::string(kMonuments[playerMonument(G, p)].where) : "";
            with.push_back({t, t == int(UnitType::Monument) ? std::string(kUnits[pl.unit[int(UnitType::Monument)]].name) + " (your monument)"
                                                             : std::string(unitTypeName(UnitType(t))),
                            d});
        }
        if (with.empty()) return true;
        curPlanet_ = planet;
        const int in = with.size() == 1 ? with[0].id
                                        : pick(p, Tag::CardTarget, "Renovation: replace the " + std::string(unitTypeName(out)) + " with which structure?", with);
        curPlanet_ = -1;
        const UnitType t = UnitType(in);
        Force& f = planet >= 0 ? G.planets[size_t(planet)].units[size_t(p)] : G.systems[size_t(sys)].space[size_t(p)];
        f.remove(out, 1, true);  // (back to the reinforcements - replaced, not destroyed)
        if (isShip(out) || out == UnitType::SpaceDock) fleetDirty_ = true;
        say(p, pn(p) + " renovates: the " + unitTypeName(out) + " " + (planet >= 0 ? "on " + planetName(planet) : "in " + sysName(sys)) +
                   " makes way for a" + (t == UnitType::Monument ? " monument" : std::string(" ") + unitTypeName(t)) + " (Renovation).",
            sys);
        if (planet < 0 && out == UnitType::SpaceDock) removeExcessCapacity(p, sys);
        if (t == UnitType::Monument) {
            placeMonument(p, monumentOptionId(planet, sys));
            return true;
        }
        if (planet >= 0 && monumentInstead(p, planet)) return true;  // Monuments to the Ages: a monolith instead
        if (t == UnitType::PDS && planet >= 0 && hecatoncheires(p, planet)) return true;  // the Titans' mech: a mech and an infantry
        addUnits(p, t, 1, sys, planet);
        if (t == UnitType::PDS && planet >= 0) absolPdsPlaced(p, planet);  // (Absol's Homeland Defense Act)
        if (t == UnitType::SpaceDock && planet >= 0 && (G.modules & kModDiscordant)) dsDockPlaced(p, planet);  // Industrious
        if (t == UnitType::SpaceDock && lawOwner(G, "minister_industry") == p) production(p, sys, productionCapacity(G, p, sys), false);
        return true;
    }
    return true;
}

// ------------------------------------------------------------------ the laws
// Cathedral of Ixth: "Attach this card to the elected planet. The owner of this planet gains 1 VP if their monument is on this
// planet. When a player places their monument on this planet, they gain 1 VP. When a player's monument on this planet is removed,
// they lose 1 VP."
// Minister of Culture: "As long as the owner of this card controls their monument their commodity value is increased by 2 (query
// commodityValue). The elected player gains this card and replenishes their commodities."
void Rules::monLawResolve(int a, int o) {
    const char* k = kAgendas[a].key;
    if (!std::strcmp(k, "cathedralofixth")) {
        addLaw(a, -1, o);
        const PlanetState& ps = G.planets[size_t(o)];
        say(-1, "Cathedral of Ixth is attached to " + planetName(o) + ".", ps.system);
        for (int q = 0; q < G.numPlayers; ++q) {
            Player& pl = G.players[size_t(q)];
            pl.cathedralVP = 0;
            if (!ps.units[size_t(q)].count(UnitType::Monument)) continue;
            if (q == ps.owner) {
                pl.cathedralVP = 1;
                gainVP(q, 1, "Cathedral of Ixth: their monument is on " + planetName(o));
            } else {
                pl.cathedralVP = 2;  // (another player's monument there now: no point now, none lost when it goes)
            }
        }
        return;
    }
    if (!std::strcmp(k, "ministerofculture")) {
        addLaw(a, o, -1);
        Player& pl = G.players[size_t(o)];
        const int before = pl.commodities;
        pl.commodities = int8_t(std::max<int>(pl.commodities, refreshTo(G, pl)));
        say(o, pn(o) + " replenishes commodities: " + std::to_string(pl.commodities) + " (Minister of Culture).");
        replenished(o, pl.commodities - before);
        return;
    }
}

// The Cathedral of Ixth's points follow the monuments on its planet (checked before every question): a monument placed there - 1
// VP; that monument removed (destroyed, captured, replaced, returned) - the point goes. When the card leaves play the points
// gained stay. Not while Law's Order blanks the laws.
void Rules::cathedralCheck() {
    if (!monumentsOn() || G.lawsBlank) return;
    const Law* law = nullptr;
    for (const Law& l : G.laws)
        if (l.agenda >= 0 && !std::strcmp(kAgendas[l.agenda].key, "cathedralofixth")) law = &l;
    const int x = law ? law->target : -1;
    for (int q = 0; q < G.numPlayers; ++q) {
        Player& pl = G.players[size_t(q)];
        if (!law || x < 0) {
            pl.cathedralVP = 0;
            continue;
        }
        const bool there = !G.planets[size_t(x)].destroyed && G.planets[size_t(x)].units[size_t(q)].count(UnitType::Monument) > 0;
        if (there && pl.cathedralVP == 0) {
            pl.cathedralVP = 1;
            gainVP(q, 1, "Cathedral of Ixth: their monument is placed on " + planetName(x));
        } else if (!there && pl.cathedralVP == 1) {
            pl.cathedralVP = 0;
            loseVP(q, 1, "Cathedral of Ixth: their monument on " + planetName(x) + " is removed");
        } else if (!there && pl.cathedralVP == 2) {
            pl.cathedralVP = 0;
        }
    }
}

}  // namespace ti
