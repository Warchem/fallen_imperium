// Thunder's Edge galactic events (0.36.0): optional rules ticked on the Modules screen (Setup::events, Game::events,
// query eventOn). Our own wording in data.cpp (galacticEvent); the AI for their questions in ai/ai_te.cpp (Ab::Ev*).
//   setupEvents        - at the end of setup: Advent of the War Sun, Stellar Atomics, Mercenaries for Hire (the deck),
//                        Dangerous Wilds (neutral guards), Rapid Mobilization, Civilized Society (every public face up),
//                        Age of Fighters
//   eventOptions /     - component actions: Stellar Atomics, Total War (10 spoils for a point), Mercenaries for Hire,
//   eventAction          Age of Exploration (Dark Energy Tap adds a system), Conventions of War Abandoned (X-89 purges
//                        a planet)
//   hooks              - eventDestroyed (Total War), eventControl (Total War, Dangerous Wilds, Minor Factions),
//                        orthodoxyCheck (Zealous Orthodoxy), voidMoved (Call of the Void), eventsStatusEnd (Civilized
//                        Society, Monuments), eventsRoundEnd (Dangerous Wilds), weirdWormholes; others sit at their sites
//                        (grep eventOn)
#include <algorithm>
#include <cstring>

#include "engine/breakthroughs.h"
#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
void eventLog(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    e.system = int16_t(sys);
    E.emit(e);
}
// the system a player's "home" means on the board: the Creuss gate / the Sorrow for those off the board
int anchorOf(const Game& g, int p) {
    const int home = g.players[size_t(p)].homeSystem;
    if (home < 0) return -1;
    if (!g.systems[size_t(home)].offBoard) return home;
    const char* gate = isFaction(g, p, "ghost") ? "17" : isFaction(g, p, "crimson") ? "94" : nullptr;
    if (!gate) return home;
    const int def = findSystem(gate);
    for (size_t s = 0; s < g.systems.size(); ++s)
        if (g.systems[s].def == def) return int(s);
    return home;
}
}  // namespace

// ------------------------------------------------------------------ setup
void Rules::setupEvents() {
    if (!G.events || !(G.modules & kModThundersEdge)) return;
    for (int k = 0; k < kEventCount; ++k)
        if (eventOn(G, k)) E.log(-1, std::string("Galactic event: ") + galacticEvent(k).name + " - " + galacticEvent(k).text, EventKind::Phase);
    const int n = G.numPlayers;
    if (eventOn(G, kEvWarSun))  // Advent of the War Sun
        for (int p = 0; p < n; ++p) {
            if (fac(p, "muaat")) {
                Player& pl = G.players[size_t(p)];
                const int16_t note = makeNote(p, kNoteFaction);
                pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), note), pl.notes.end());
                for (LeaderState& ls : pl.leaders)
                    if (kLeaders[ls.leader].type == kCommander) ls.status = kReady;
                if (pl.homeSystem >= 0) addUnits(p, UnitType::WarSun, 1, pl.homeSystem, -1);
            } else {
                researchTech(p, warSunUpgradeFor(G, p));  // (fixes 26: their own war sun upgrade - the Roh'Dhna's)
            }
        }
    if (eventOn(G, kEvFighters))  // Age of Fighters: Fighter II (the Naalu: theirs)
        for (int p = 0; p < n; ++p) researchTech(p, findTech(fac(p, "naalu") ? "hcf2" : "ff2"));
    if (eventOn(G, kEvAtomics))
        for (Player& pl : G.players) pl.atomics = true;
    if (eventOn(G, kEvMercenaries)) {  // the Alliances of factions not in play
        G.mercDeck.clear();
        for (int f = 0; f < kFactionCount; ++f) {
            if (!factionImplemented(f) || !std::strcmp(kFactions[f].key, "obsidian")) continue;
            if (kFactions[f].source == Source::Discordant && (!(G.modules & kModDiscordant) || !factionInPool(f, G.dsLevel))) continue;
            bool inPlay = false;
            for (const Player& pl : G.players) inPlay = inPlay || pl.faction == f;
            if (!inPlay && findLeader((std::string(kFactions[f].key) + "commander").c_str()) >= 0) {
                // (fixes 25, owner's faction debug pass - Nocturne) the Firmament / the Obsidian are one faction: one
                // alliance card, its side picked at random (as the TE rulebook does for Minor Factions)
                if (G.fixes >= 25 && !std::strcmp(kFactions[f].key, "firmament") && E.rng().below(2)) G.mercDeck.push_back(int8_t(findFaction("obsidian")));
                else G.mercDeck.push_back(int8_t(f));
            }
        }
        E.rng().shuffle(G.mercDeck);
    }
    if (eventOn(G, kEvMinor)) minorFactions();
    if (eventOn(G, kEvWilds)) wildsGuard(true);
    if (eventOn(G, kEvMobilization)) {
        fractureEnter(G.speaker, false);
        // infantry on each planet next to home (one next to two homes stays empty), readied, not explored
        std::vector<int> claim(G.planets.size(), -1);
        for (int p = 0; p < n; ++p) {
            const int a = anchorOf(G, p);
            if (a < 0) continue;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                const SystemState& sy = G.systems[s];
                if (int(s) == a || sy.home >= 0 || sy.offBoard || sy.isHyperlane() || hexDistance(sy.pos, G.systems[size_t(a)].pos) != 1) continue;
                for (int16_t x : sy.planets) {
                    if (G.planets[size_t(x)].owner >= 0 || neutralUnitsIn(G, int(s))) continue;
                    claim[size_t(x)] = claim[size_t(x)] == -1 ? p : -2;
                }
            }
        }
        for (size_t x = 0; x < claim.size(); ++x) {
            const int p = claim[x];
            if (p < 0 || unitsAvailable(G, p, UnitType::Infantry) <= 0) continue;
            addUnits(p, UnitType::Infantry, 1, G.planets[x].system, int(x));
            setControl(int(x), p);
            G.planets[x].exhausted = false;
        }
        for (int p : fromSpeaker(G)) {
            // a space dock on a planet we control (one without a dock), with the flagship and 3 fighters
            std::vector<Option> opts;
            for (int x : controlledPlanets(G, p))
                if (!G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock)) opts.push_back({x, planetName(x), ""});
            if (!opts.empty() && unitsAvailable(G, p, UnitType::SpaceDock) > 0) {
                const int x = opts.size() == 1 ? opts[0].id
                                               : fpick(p, Ab::EvMobilize, "Rapid Mobilization: put a space dock on which planet?", opts,
                                                       "Your flagship and 3 fighters join it there.", false, true);
                const int sys = G.planets[size_t(x)].system;
                addUnits(p, UnitType::SpaceDock, 1, sys, x);
                if (unitsAvailable(G, p, UnitType::Flagship) > 0) addUnits(p, UnitType::Flagship, 1, sys, -1);
                else if (unitsAvailable(G, p, UnitType::Flagship2) > 0) addUnits(p, UnitType::Flagship2, 1, sys, -1);  // (0.52.0: the Lady)
                addUnits(p, UnitType::Fighter, std::min(3, unitsAvailable(G, p, UnitType::Fighter)), sys, -1);
                say(p, pn(p) + " builds a space dock on " + planetName(x) + " with their flagship and 3 fighters (Rapid Mobilization).", sys);
            }
            if (breakthroughsOn(G) && !G.players[size_t(p)].breakthrough) gainBreakthrough(p, "Rapid Mobilization");
            for (int k = 0; k < (fac(p, "nomad") ? 2 : 1); ++k) {
                std::vector<Option> techs;
                for (int t = 0; t < kTechCount; ++t)
                    if (canResearchNow(G, p, t)) techs.push_back({t, kTechs[t].name, techSummary(t)});
                if (techs.empty()) break;
                const int t = fpick(p, Ab::EvMobilize, "Rapid Mobilization: research a technology", techs);
                researchTech(p, t, true);
                E.log(p, pn(p) + " researches " + kTechs[t].name + " (Rapid Mobilization).", EventKind::Research);
            }
        }
    }
    if (eventOn(G, kEvCivilized))  // every public objective face up
        while (G.stage1Revealed < 5 || !G.stage2Deck.empty()) {
            const size_t before = G.publicObjectives.size();
            revealObjective();
            if (G.publicObjectives.size() == before) break;
        }
}

// Minor Factions: the home systems of factions not in play take the place of second-ring systems between the players'
// homes (one per player), with 3 neutral infantry; their planets have all three traits.
void Rules::minorFactions() {
    const int mec = G.mecatol();
    if (mec < 0) return;
    std::vector<int> unused;
    for (int f = 0; f < kFactionCount; ++f) {
        const char* k = kFactions[f].key;
        if (kFactions[f].source == Source::Discordant && (!(G.modules & kModDiscordant) || !factionInPool(f, G.dsLevel))) continue;
        if (!factionImplemented(f) || kFactions[f].homeSystem < 0 || !std::strcmp(k, "obsidian") || !std::strcmp(k, "keleres") ||
            !std::strcmp(k, "ghost") || !std::strcmp(k, "crimson"))
            continue;
        bool inPlay = false;
        for (const Player& pl : G.players) inPlay = inPlay || pl.faction == f || pl.tribunii == f;
        if (!inPlay) unused.push_back(f);
    }
    E.rng().shuffle(unused);
    struct Spot {
        int sys, gap;
    };
    std::vector<Spot> spots;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const SystemState& sy = G.systems[s];
        if (sy.home >= 0 || int(s) == mec || sy.offBoard || sy.isHyperlane() || sy.fracture || sy.d().anomalies ||
            hexDistance(sy.pos, G.systems[size_t(mec)].pos) != 2)
            continue;
        bool legendary = false;
        for (int16_t x : sy.planets) legendary = legendary || kPlanets[G.planets[size_t(x)].def].legendary;
        if (legendary) continue;
        std::vector<int> d;  // distances to the homes, nearest first
        for (const Player& pl : G.players)
            if (pl.homeSystem >= 0) d.push_back(hexDistance(sy.pos, G.systems[size_t(pl.homeSystem)].pos));
        std::sort(d.begin(), d.end());
        spots.push_back({int(s), d.size() >= 2 ? d[1] - d[0] : 9});
    }
    std::stable_sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.gap < b.gap; });
    int placed = 0;
    std::vector<int> taken;
    for (const Spot& sp : spots) {
        if (placed >= G.numPlayers || placed >= int(unused.size())) break;
        bool near = false;  // (not two side by side)
        for (int t : taken) near = near || hexDistance(G.systems[size_t(t)].pos, G.systems[size_t(sp.sys)].pos) <= 1;
        if (near) continue;
        const int f = unused[size_t(placed)];
        SystemState& sy = G.systems[size_t(sp.sys)];
        for (int16_t x : sy.planets) {  // the old system's planets leave the game
            G.planets[size_t(x)].destroyed = true;
            G.planets[size_t(x)].owner = -1;
        }
        sy.planets.clear();
        sy.def = kFactions[f].homeSystem;
        sy.frontier = false;
        const SystemDef& d = kSystems[sy.def];
        for (int k = 0; k < d.planetCount; ++k) {
            PlanetState ps;
            ps.def = d.planets[k];
            ps.system = int16_t(sp.sys);
            ps.minor = int8_t(f);
            sy.planets.push_back(int16_t(G.planets.size()));
            G.planets.push_back(ps);
        }
        const int np = int(sy.planets.size());
        for (int k = 0; k < np; ++k) {  // 3 neutral infantry, split as evenly as possible
            const int n = 3 / np + (k < 3 % np ? 1 : 0);
            if (n) placeNeutral(UnitType::Infantry, n, sp.sys, sy.planets[size_t(k)], "");
        }
        E.log(-1, std::string("Minor faction: the home system of ") + kFactions[f].name + " sits in the second ring (" + sysName(sp.sys) + ").",
              EventKind::Phase);
        taken.push_back(sp.sys);
        ++placed;
    }
    computeAdjacency(G);
}

// Dangerous Wilds: neutral infantry on each hazardous planet nobody holds, as many as its resources (setup: placed;
// end of a round: topped up again)
void Rules::wildsGuard(bool setup) {
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.destroyed || ps.owner >= 0 || ps.system < 0 || G.systems[size_t(ps.system)].home >= 0 || !hasTraitOf(G, int(x), Trait::Hazardous))
            continue;
        bool others = false;  // (players' ground forces there: no top-up)
        for (int q = 0; q < G.numPlayers; ++q) others = others || ps.units[size_t(q)].groundForces() > 0;
        if (others) continue;
        const int want = planetResources(G, int(x)), have = ps.units[kNeutral].count(UnitType::Infantry);
        if (want > have) placeNeutral(UnitType::Infantry, want - have, ps.system, int(x), setup ? "" : "Dangerous Wilds");
    }
}

// ------------------------------------------------------------------ component actions
void Rules::eventOptions(int p, std::vector<Option>& o) {
    if (!G.events) return;
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    const Player& pl = G.players[size_t(p)];
    if (eventOn(G, kEvAtomics) && pl.atomics)
        add(Ab::EvAtomics, "Stellar Atomics", "Spend your atomics token: destroy every ground force and structure on a non-home planet.");
    if (eventOn(G, kEvTotalWar)) {
        int spoils = 0;
        if (pl.homeSystem >= 0)
            for (int16_t x : G.systems[size_t(pl.homeSystem)].planets)
                if (G.planets[size_t(x)].owner == p) spoils += G.planets[size_t(x)].spoils;
        if (spoils >= 10) add(Ab::EvTotalWar, "Total War: 10 spoils", "Discard 10 commodities from your home planets to gain 1 victory point.");
    }
    if (eventOn(G, kEvMercenaries) && !G.mercDeck.empty() && pl.tradeGoods >= 3)
        add(Ab::EvMercenary, "Hire a mercenary", "Pay 3 trade goods: take the top Alliance of the mercenary deck and use that faction's commander ability.");
    if (eventOn(G, kEvExploration) && techReady(p, "det") && !boardEdgeSpots(G).empty())
        add(Ab::EvExplore, "Dark Energy Tap: explore beyond", "Exhaust it: a new system appears beside a non-home edge system with your ships.");
    if (eventOn(G, kEvConventions) && techReady(p, "x89_base"))
        add(Ab::EvX89, "X-89 Bacterial Weapon: purge a planet", "Exhaust it: a planet in a system with your bombarding units is purged.");
}

bool Rules::eventAction(int p, Ab ab) {
    Player& pl = G.players[size_t(p)];
    switch (ab) {
        case Ab::EvAtomics: {
            std::vector<Option> opts;
            for (size_t x = 0; x < G.planets.size(); ++x) {
                const PlanetState& ps = G.planets[x];
                if (ps.destroyed || ps.system < 0 || isHomePlanet(G, int(x))) continue;
                int n = 0;
                for (int s = 0; s < kSides; ++s) n += ps.units[size_t(s)].groundForces() + ps.units[size_t(s)].structures();
                if (n) opts.push_back({int(x), planetName(x), std::to_string(n) + " ground forces and structures"});
            }
            if (opts.empty() || !pl.atomics) return false;
            opts.push_back({-1, "Cancel", ""});
            const int x = fpick(p, Ab::EvAtomics, "Stellar Atomics: which planet?", opts, "Every ground force and structure on it is destroyed.", false, true);
            if (x < 0) return false;
            pl.atomics = false;
            const int sys = G.planets[size_t(x)].system;
            for (int s = 0; s < kSides; ++s)
                for (UnitType t : {UnitType::Infantry, UnitType::Mech, UnitType::PDS, UnitType::SpaceDock}) {
                    const int k = G.planets[size_t(x)].units[size_t(s)].count(t);
                    if (k) destroyUnits(s, t, k, sys, x, p, "Stellar Atomics");
                }
            eventLog(E, p, "Stellar Atomics", pn(p) + " sets off their atomics on " + planetName(x) + "!", sys);
            return true;
        }
        case Ab::EvTotalWar: {
            if (pl.homeSystem < 0) return false;
            int left = 10;
            for (int16_t x : G.systems[size_t(pl.homeSystem)].planets) {
                PlanetState& ps = G.planets[size_t(x)];
                if (ps.owner != p) continue;
                const int k = std::min<int>(left, ps.spoils);
                ps.spoils = int16_t(ps.spoils - k);
                left -= k;
            }
            if (left > 0) return false;
            gainVP(p, 1, "Total War");
            return true;
        }
        case Ab::EvMercenary: {
            if (G.mercDeck.empty() || pl.tradeGoods < 3) return false;
            pl.tradeGoods = int8_t(pl.tradeGoods - 3);
            const int f = G.mercDeck.back();
            G.mercDeck.pop_back();
            pl.alliances.push_back(int8_t(f));
            const int c = findLeader((std::string(kFactions[f].key) + "commander").c_str());
            eventLog(E, p, "Mercenaries for Hire",
                     pn(p) + " hires the " + kFactions[f].name + " (their commander's ability: " + (c >= 0 ? leaderText(c) : "") + ")");
            return true;
        }
        case Ab::EvExplore: return darkEnergyExplore(p);
        case Ab::EvX89: {
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                bool bomb = false;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (G.systems[s].space[size_t(p)].n[t] && unitDef(G, p, UnitType(t)).bombard > 0) bomb = true;
                // (0.44.6, fixes 10: any unit with BOMBARDMENT - Annihilator mechs on planets, the Discordant Stars
                // bombardment dice)
                if (!bomb && G.fixes >= 10) {
                    bomb = bombardAvailable(G, p, int(s), UnitType::Mech) > 0;
                    for (int t = 0; t < kUnitTypes && !bomb && (G.modules & kModDiscordant); ++t)
                        if (G.systems[s].space[size_t(p)].n[t] && dsBombardDiceOf(G, p, int(s), UnitType(t)) > 0) bomb = true;
                }
                if (!bomb) continue;
                for (int16_t x : G.systems[s].planets)
                    if (!G.planets[size_t(x)].destroyed && strcmp(kPlanets[G.planets[size_t(x)].def].key, "mr") != 0)
                        opts.push_back({x, planetName(x), G.planets[size_t(x)].owner >= 0 ? pn(G.planets[size_t(x)].owner) + "'s" : std::string("unclaimed")});
            }
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            const int x = fpick(p, Ab::EvX89, "X-89 Bacterial Weapon: purge which planet?", opts,
                                "Everything on it is destroyed and the planet is gone for the rest of the game.", false, true);
            if (x < 0) return false;
            exhaustTech(p, "x89_base");
            destroyPlanet(x, p, "X-89 Bacterial Weapon");
            return true;
        }
        default: return false;
    }
}

// Age of Exploration: Dark Energy Tap's action - a red (1-4) or blue (5-10) system beside a non-home edge system with
// our ships, touching at least 2 non-home systems; a frontier token if it has no planets.
bool Rules::darkEnergyExplore(int p) {
    std::vector<Hex> spots = boardEdgeSpots(G);
    std::vector<Option> from;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const SystemState& sy = G.systems[s];
        if (sy.home >= 0 || sy.offBoard || !hasShips(G, p, int(s))) continue;
        bool edge = false;
        for (const Hex& h : spots)
            if (hexDistance(h, sy.pos) == 1) edge = true;
        if (edge) from.push_back({int(s), sysName(int(s)), ""});
    }
    if (from.empty()) return false;
    from.push_back({-1, "Cancel", ""});
    const int s = fpick(p, Ab::EvExplore, "Dark Energy Tap: explore beyond which system?", from, "", true);
    if (s < 0) return false;
    exhaustTech(p, "det");
    const int roll = E.rng().d10();
    std::vector<int> tiles;
    for (int i = 0; i < kSystemCount; ++i) {
        if (kSystems[i].back != (roll <= 4 ? Back::Red : Back::Blue) || !systemInGame(G, i)) continue;
        bool used = std::find(G.purgedTiles.begin(), G.purgedTiles.end(), int16_t(i)) != G.purgedTiles.end();
        for (const SystemState& x : G.systems) used = used || x.def == i;
        if (!used) tiles.push_back(i);
    }
    say(p, pn(p) + " rolls " + std::to_string(roll) + " (Dark Energy Tap): a " + (roll <= 4 ? "red" : "blue") + " system.");
    if (tiles.empty()) return true;
    const int def = tiles[size_t(E.rng().below(int(tiles.size())))];
    E.revealed();
    std::vector<Option> opts;
    for (const Hex& h : spots) {
        if (hexDistance(h, G.systems[size_t(s)].pos) != 1) continue;
        int nonHome = 0;
        for (int e = 0; e < 6; ++e) {
            const int j = G.findSystemAt(hexNeighbor(h, e));
            if (j >= 0 && !G.systems[size_t(j)].isHyperlane() && G.systems[size_t(j)].home < 0) ++nonHome;
        }
        if (nonHome >= 2) opts.push_back({(h.q + 64) * 256 + (h.r + 64), "Beside " + sysName(s), std::to_string(nonHome) + " systems next to it"});
    }
    if (opts.empty()) {
        say(p, "There is no room beside " + sysName(s) + ".");
        return true;
    }
    const int id = opts.size() == 1 ? opts[0].id : fpick(p, Ab::EvExplore, std::string("Place ") + kSystems[def].name + " where?", opts);
    const Hex h{id / 256 - 64, id % 256 - 64};
    const int ns = addBoardSystem(G, def, h);
    if (G.systems[size_t(ns)].planets.empty()) G.systems[size_t(ns)].frontier = true;
    computeAdjacency(G);
    eventLog(E, p, "Age of Exploration", pn(p) + " discovers " + sysName(ns) + " beyond " + sysName(s) + ".", ns);
    return true;
}

// ------------------------------------------------------------------ hooks
// Total War: destroying another player's units earns their cost in commodities, on a planet in the home system
void Rules::eventDestroyed(int p, UnitType t, int k, int byPlayer) {
    if (!eventOn(G, kEvTotalWar) || byPlayer < 0 || byPlayer >= G.numPlayers || p == byPlayer || p < 0 || p >= G.numPlayers || k <= 0) return;
    const int each = t == UnitType::Infantry || t == UnitType::Fighter ? 1 : std::max(1, unitDef(G, p, t).cost2 / 2);
    const int home = G.players[size_t(byPlayer)].homeSystem;
    if (home < 0) return;
    for (int16_t x : G.systems[size_t(home)].planets)
        if (G.planets[size_t(x)].owner == byPlayer) {
            G.planets[size_t(x)].spoils = int16_t(G.planets[size_t(x)].spoils + each * k);
            say(byPlayer, pn(byPlayer) + " puts " + std::to_string(each * k) + " commodities on " + planetName(x) + " (Total War).", home);
            return;
        }
}

// Gaining control of a planet: Total War (its commodities may go home), Dangerous Wilds (research), Minor Factions
void Rules::eventControl(int planet, int p, int old) {
    if (!G.events || p < 0 || p >= G.numPlayers) return;
    PlanetState& ps = G.planets[size_t(planet)];
    if (eventOn(G, kEvTotalWar) && ps.spoils > 0 && G.systems[size_t(ps.system)].home != p) {
        const int home = G.players[size_t(p)].homeSystem;
        int dest = -1;
        if (home >= 0)
            for (int16_t x : G.systems[size_t(home)].planets)
                if (G.planets[size_t(x)].owner == p && dest < 0) dest = x;
        if (dest >= 0 && fyes(p, Ab::EvSpoils, "Total War: move the " + std::to_string(ps.spoils) + " commodities on " + planetName(planet) + " home?", "")) {
            G.planets[size_t(dest)].spoils = int16_t(G.planets[size_t(dest)].spoils + ps.spoils);
            say(p, pn(p) + " carries " + std::to_string(ps.spoils) + " commodities home (Total War).");
            ps.spoils = 0;
        }
    }
    if (eventOn(G, kEvWilds) && !ps.claimed && hasTraitOf(G, planet, Trait::Hazardous) && G.phase != Phase::Setup) {
        const int allow = planetResources(G, planet);
        std::vector<Option> opts;
        for (int t = 0; t < kTechCount; ++t) {
            if (G.players[size_t(p)].hasTech(t) || !techAvailableToResearch(G, p, t)) continue;
            int miss[4];
            missingPrereqs(G, p, t, miss);
            if (miss[0] + miss[1] + miss[2] + miss[3] <= allow || canResearchNow(G, p, t)) opts.push_back({t, kTechs[t].name, techSummary(t)});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "No technology", ""});
            const int t = fpick(p, Ab::EvWildsTech, "Dangerous Wilds: research a technology? (" + std::to_string(allow) + " prerequisites may be ignored)", opts);
            if (t >= 0) {
                researchTech(p, t, true);
                E.log(p, pn(p) + " researches " + kTechs[t].name + " (Dangerous Wilds).", EventKind::Research);
            }
        }
    }
    ps.claimed = true;
    if (eventOn(G, kEvMinor) && ps.minor >= 0) {  // Minor Factions: every planet of the system - their Alliance
        bool all = true;
        for (int16_t x : G.systems[size_t(ps.system)].planets) all = all && G.planets[size_t(x)].owner == p;
        const int8_t f = ps.minor;
        std::vector<int8_t>& mine = G.players[size_t(p)].alliances;
        if (all && std::find(mine.begin(), mine.end(), f) == mine.end()) {
            for (Player& q : G.players) q.alliances.erase(std::remove(q.alliances.begin(), q.alliances.end(), f), q.alliances.end());
            mine.push_back(f);
            const int c = findLeader((std::string(kFactions[f].key) + "commander").c_str());
            eventLog(E, p, "Minor Factions", pn(p) + " wins the Alliance of " + kFactions[f].name + " (" + (c >= 0 ? leaderText(c) : "") + ")",
                     ps.system);
        }
    }
    if (eventOn(G, kEvMonuments) && ps.monument >= 0 && old != p) {
        if (fyes(p, Ab::EvMonument, "Monuments to the Ages: destroy the monument on " + planetName(planet) + "?",
                 std::to_string(ps.monument) + " commodities under it (a victory point per 3, for whoever holds the planet).")) {
            ps.monument = -1;
            say(p, pn(p) + " tears down the monument on " + planetName(planet) + ".", ps.system);
        }
        monumentPoints();
    }
}

// Zealous Orthodoxy: the first player with 2 scored secret objectives
void Rules::orthodoxyCheck(int p) {
    if (!eventOn(G, kEvOrthodoxy) || G.orthodoxy >= 0 || p < 0 || p >= G.numPlayers) return;
    int n = 0;
    for (int16_t o : G.players[size_t(p)].scored) n += kObjectives[o].kind == 3;
    if (n < 2) return;
    G.orthodoxy = int8_t(G.players[size_t(p)].faction);
    const int16_t ally = makeNote(p, kNoteAlliance);  // their Alliance note is purged
    for (Player& q : G.players) {
        q.notes.erase(std::remove(q.notes.begin(), q.notes.end(), ally), q.notes.end());
        q.notesInPlay.erase(std::remove(q.notesInPlay.begin(), q.notesInPlay.end(), ally), q.notesInPlay.end());
    }
    eventLog(E, p, "Zealous Orthodoxy", pn(p) + " are the first with 2 secret objectives: everyone gains their Alliance ability.");
    gainVP(p, 1, "Zealous Orthodoxy");
}

// Call of the Void: units moved into a Fracture system - a command token
void Rules::voidMoved(int p, int sys, bool moved) {
    if (!moved || !eventOn(G, kEvVoid) || sys < 0 || !G.systems[size_t(sys)].fracture) return;
    gainTokens(p, 1, false, "Call of the Void: 1 command token.");
}

// End of the status phase: Civilized Society ends the game; Monuments to the Ages.
void Rules::eventsStatusEnd() {
    if (!G.events) return;
    if (eventOn(G, kEvCivilized)) {
        int best = -1;
        for (int p : initiativeOrder(G))
            if (!G.players[size_t(p)].eliminated && G.players[size_t(p)].vp >= G.vpGoal) best = p;
        if (best >= 0) {
            auto tie = [&](int p) {
                int v = G.players[size_t(p)].tradeGoods;
                for (int x : controlledPlanets(G, p)) v += planetInfluence(G, x);
                return v;
            };
            best = -1;
            for (int p : initiativeOrder(G)) {
                if (G.players[size_t(p)].eliminated) continue;
                if (best < 0 || G.players[size_t(p)].vp > G.players[size_t(best)].vp ||
                    (G.players[size_t(p)].vp == G.players[size_t(best)].vp && tie(p) > tie(best)))
                    best = p;
            }
            G.winner = int8_t(best);
            throw GameOverSignal();
        }
    }
}

// Weird Wormholes: each ship that moved through a wormhole rolls - 1-5 the next smaller ship, 6-10 the next bigger one
// (fighter, destroyer, cruiser, dreadnought, carrier, flagship, war sun), skipping types with none left in reinforcements
void Rules::weirdWormholes(int p, int sys, const std::vector<std::pair<int, int>>& ships) {
    static const UnitType kChain[] = {UnitType::Fighter, UnitType::Destroyer, UnitType::Cruiser, UnitType::Dreadnought,
                                      UnitType::Carrier, UnitType::Flagship, UnitType::WarSun};
    const int n = int(sizeof kChain / sizeof kChain[0]);
    RollResult res;
    std::string what;
    for (const auto& sh : ships) {
        int at = -1;
        for (int i = 0; i < n; ++i)
            if (int(kChain[i]) == sh.first || (sh.first == int(UnitType::Flagship2) && kChain[i] == UnitType::Flagship)) at = i;
        if (at < 0) continue;
        for (int k = 0; k < sh.second; ++k) {
            if (G.systems[size_t(sys)].space[size_t(p)].n[sh.first] <= 0) break;
            const int v = E.rng().d10();
            res.dice.push_back(int8_t(v));
            res.need.push_back(6);
            const int step = v <= 5 ? -1 : 1;
            int to = -1;
            for (int i = at + step; i >= 0 && i < n; i += step)
                if (unitsAvailable(G, p, kChain[i]) > 0) {
                    to = i;
                    break;
                }
            if (to < 0) continue;
            Force& f = G.systems[size_t(sys)].space[size_t(p)];
            f.remove(UnitType(sh.first), 1, true);
            f.add(kChain[to], 1);
            what += std::string(what.empty() ? "" : ", ") + unitTypeName(UnitType(sh.first)) + " -> " + unitTypeName(kChain[to]);
        }
    }
    recount(res);
    if (!res.dice.empty()) reportRoll(p, sys, "Weird Wormholes (1-5 smaller, 6-10 bigger)", res);
    if (!what.empty()) eventLog(E, p, "Weird Wormholes", pn(p) + "'s ships come out changed: " + what + ".", sys);
}

// Monuments to the Ages: placing a structure on a non-home planet, 5 trade goods buy a monument instead (3 at most).
bool Rules::monumentInstead(int p, int planet) {
    if (!eventOn(G, kEvMonuments) || planet < 0 || isHomePlanet(G, planet) || G.planets[size_t(planet)].monument >= 0 ||
        G.players[size_t(p)].tradeGoods < 5)
        return false;
    int built = 0;
    for (const PlanetState& ps : G.planets) built += ps.monument >= 0;
    if (built >= 3 || !fyes(p, Ab::EvMonument, "Monuments to the Ages: pay 5 trade goods for a monument on " + planetName(planet) + " instead?",
                            "Each status phase puts 1 commodity under it; every 3 is a victory point for whoever holds the planet."))
        return false;
    G.players[size_t(p)].tradeGoods = int8_t(G.players[size_t(p)].tradeGoods - 5);
    G.planets[size_t(planet)].monument = 0;
    eventLog(E, p, "Monuments to the Ages", pn(p) + " raises a monument on " + planetName(planet) + ".", G.planets[size_t(planet)].system);
    return true;
}

// The points each player holds from monuments (1 per 3 commodities under the ones on their planets) - gained and lost
// as they change hands.
void Rules::monumentPoints() {
    if (!eventOn(G, kEvMonuments)) return;
    for (int p = 0; p < G.numPlayers; ++p) {
        int want = 0;
        for (const PlanetState& ps : G.planets)
            if (ps.owner == p && ps.monument > 0 && !ps.destroyed) want += ps.monument / 3;
        Player& pl = G.players[size_t(p)];
        if (want > pl.monumentVP) {
            const int d = want - pl.monumentVP;
            pl.monumentVP = int8_t(want);
            gainVP(p, d, "Monuments to the Ages");
        } else if (want < pl.monumentVP) {
            const int d = pl.monumentVP - want;
            pl.monumentVP = int8_t(want);
            loseVP(p, d, "Monuments to the Ages");
        }
    }
}

// Start of the status phase: a commodity under each monument.
void Rules::eventsStatusStart() {
    if (!eventOn(G, kEvMonuments)) return;
    for (PlanetState& ps : G.planets)
        if (ps.monument >= 0 && !ps.destroyed) ps.monument = int8_t(std::min(99, ps.monument + 1));
    monumentPoints();
}

// End of a round: Dangerous Wilds' guards come back.
void Rules::eventsRoundEnd() {
    if (eventOn(G, kEvWilds)) wildsGuard(false);
}

}  // namespace ti
