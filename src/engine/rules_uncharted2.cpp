// Uncharted Space exploration cards and relics (0.33.0, owner: "add any specific Uncharted Space action
// cards and tokens associated with the add-on"). The 16 exploration cards of its deck and its 7 relics are
// appended to the PoK tables (Source::Uncharted) and only shuffled in with the module. Star Charts stay in
// front of their owner (in Player::fragments, so they trade like fragments) and two of them put a new
// system at the edge of the board; so does Azdel's Key. Our own short wording is in card_text.cpp.
#include <algorithm>
#include <cstring>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
bool exIs(int c, const char* k) { return c >= 0 && c < kExploreCardCount && !std::strcmp(kExploreCards[c].key, k); }
int deckOf(const Game& g, int planet) {  // exploration deck of a planet's trait, -1 if none
    if (g.planets[size_t(planet)].terraform) return int(ExploreType::Cultural);
    switch (kPlanets[g.planets[size_t(planet)].def].trait) {
        case Trait::Cultural: return int(ExploreType::Cultural);
        case Trait::Hazardous: return int(ExploreType::Hazardous);
        case Trait::Industrial: return int(ExploreType::Industrial);
        default: return -1;
    }
}
const char* tokenPool(int i) { return i == 0 ? "tactic" : i == 1 ? "fleet" : "strategy"; }
std::string unitsText(const Force& f) {
    std::string s;
    for (int t = 0; t < kUnitTypes; ++t)
        if (f.n[t]) s += (s.empty() ? "" : ", ") + std::to_string(f.n[t]) + " " + unitTypeName(UnitType(t), f.n[t] > 1);
    return s;
}
}  // namespace

// ------------------------------------------------------------------ command tokens
// "Redistribute 1 command token": move one token from one pool to another (the fleet pool must still
// cover the largest fleet). false if nothing was moved.
bool Rules::redistributeOne(int p, Tag tag, const std::string& why) {
    Player& pl = G.players[p];
    int maxShips = 0;
    for (size_t s = 0; s < G.systems.size(); ++s) maxShips = std::max(maxShips, fleetUseIn(G, p, int(s)));
    maxShips -= fleetLimit(G, p) - pl.fleet;
    int8_t* pool[3] = {&pl.tactic, &pl.fleet, &pl.strategy};
    std::vector<Option> opts;
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) {
            if (a == b || *pool[a] <= 0) continue;
            if (a == 1 && pl.fleet - 1 < maxShips) continue;  // the fleet pool must keep covering the ships
            if (b == 1 && pl.fleet >= fleetPoolCap(G, p)) continue;  // (Fleet Regulations; 0.81.0: Absol's too)
            opts.push_back({a * 3 + b, std::string("Move 1 token: ") + tokenPool(a) + " to " + tokenPool(b),
                            std::to_string(pl.tactic) + " tactic / " + std::to_string(pl.fleet) + " fleet / " + std::to_string(pl.strategy) +
                                " strategy now"});
        }
    if (opts.empty()) return false;
    opts.push_back({-1, "Leave them as they are", ""});
    const int c = pick(p, tag, why + ": move which command token?", opts);
    if (c < 0) return false;
    *pool[c / 3] = int8_t(*pool[c / 3] - 1);
    *pool[c % 3] = int8_t(*pool[c % 3] + 1);
    say(p, pn(p) + " moves a command token from the " + tokenPool(c / 3) + " pool to the " + tokenPool(c % 3) + " pool (" + why + ").");
    return true;
}

// ------------------------------------------------------------------ the exploration cards
bool Rules::usExplore(int p, int c, int sys, int planet) {
    if (c < 0 || c >= kExploreCardCount || kExploreCards[c].source != Source::Uncharted) return false;
    Player& pl = G.players[p];
    const std::string name = kExploreCards[c].name;
    auto mechOrInfantry = [&](const std::string& reward) -> bool {  // as in the PoK cards
        if (planet < 0) return false;
        Force& f = G.planets[planet].units[p];
        if (f.count(UnitType::Mech) > 0) return true;
        if (f.count(UnitType::Infantry) == 0) return false;
        if (!yesNo(p, Tag::ExploreChoice, name + ": remove 1 infantry from " + planetName(planet) + "?", reward)) return false;
        f.remove(UnitType::Infantry, 1, false);
        say(p, pn(p) + " removes 1 infantry from " + planetName(planet) + ".", sys);
        if (brOn()) pharRemoved(p, 1);  // (0.85.0) Immortal (Pharad'n)
        return true;
    };
    if (exIs(c, "warforgeruins")) {  // War Forge Ruins (0.43.0, U1): a mech there, or an infantry given up - 1 mech or 2 infantry
        if (!mechOrInfantry("Place 1 mech or 2 infantry on this planet.")) return true;
        std::vector<Option> o;
        if (unitsForPlacement(G, p, UnitType::Mech) > 0) o.push_back({0, "1 mech", ""});
        if (unitsAvailable(G, p, UnitType::Infantry) > 0) o.push_back({1, "2 infantry", ""});
        if (o.empty()) return true;
        const int k = o.size() == 1 ? o[0].id : pick(p, Tag::ExploreChoice, name + ": place what on " + planetName(planet) + "?", o);
        if (k == 0) addUnits(p, UnitType::Mech, 1, sys, planet);
        else addUnits(p, UnitType::Infantry, std::min(2, unitsAvailable(G, p, UnitType::Infantry)), sys, planet);
        say(p, pn(p) + " finds a war forge on " + planetName(planet) + ": " + (k == 0 ? "1 mech." : "2 infantry."), sys);
        return true;
    }
    if (exIs(c, "forgottentradestation")) {  // Deserted Trade Station: 1 trade good per planet in the system
        int n = sys >= 0 ? int(G.systems[sys].planets.size()) : 0;
        if (G.fixes >= 31 && sys >= 0)  // (fixes 31, the exploration pass) a space station isn't a planet (Thunder's Edge)
            for (int16_t x : G.systems[size_t(sys)].planets) n -= isStation(G, x) ? 1 : 0;
        if (n > 0) {
            gainTG(p, n);
            say(p, pn(p) + " gains " + std::to_string(n) + " trade good" + (n > 1 ? "s" : "") + " (" + name + ").");
        }
        return true;
    }
    if (exIs(c, "distinguishedadmiral")) {
        std::vector<Option> o = {{0, "Redistribute 1 command token", "Move a token from one of your pools to another."}};
        if (spendable(G, p, true, false) >= 2 && reinforcementTokens(p) > 0)
            o.push_back({1, "Spend 2 influence: gain 1 command token", ""});
        o.push_back({-1, "Nothing", ""});
        const int k = pick(p, Tag::ExploreChoice, name, o);
        if (k == 0) redistributeOne(p, Tag::ExploreChoice, name);
        else if (k == 1 && pay(p, 2, true, name, true)) gainTokens(p, 1, false, name + ": gain 1 command token.");
        return true;
    }
    if (exIs(c, "scorcheddepot")) {
        if (!mechOrInfantry("Remove 1 command token from this system, or redistribute 1 of yours.")) return true;
        std::vector<Option> o;
        for (int q = 0; q < G.numPlayers; ++q)
            if (sys >= 0 && G.systems[sys].hasToken(q))
                o.push_back({100 + q, q == p ? "Remove your command token from " + sysName(sys) : "Remove " + pn(q) + "'s command token from " + sysName(sys),
                             "It goes back to its owner's reinforcements."});
        o.push_back({0, "Redistribute 1 of your command tokens", ""});
        o.push_back({-1, "Nothing", ""});
        const int k = pick(p, Tag::ExploreChoice, name, o);
        if (k >= 100) {
            G.systems[sys].tokens &= uint16_t(~(1u << (k - 100)));
            say(p, pn(p) + " removes " + (k - 100 == p ? std::string("their") : pn(k - 100) + "'s") + " command token from " + sysName(sys) + " (" +
                       name + ").",
                sys);
        } else if (k == 0) {
            redistributeOne(p, Tag::ExploreChoice, name);
        }
        return true;
    }
    if (exIs(c, "seedyspaceport")) {
        if (!mechOrInfantry("Ready your agent or draw 1 action card.")) return true;
        std::vector<Option> o;
        for (size_t i = 0; i < pl.leaders.size(); ++i)
            if (kLeaders[pl.leaders[i].leader].type == kAgent && pl.leaders[i].status == kExhausted)
                o.push_back({int(i), std::string("Ready ") + kLeaders[pl.leaders[i].leader].name, "your agent"});
        o.push_back({-1, "Draw 1 action card", ""});
        const int k = o.size() == 1 ? -1 : pick(p, Tag::ExploreChoice, name, o);
        if (k >= 0) {
            readyAgent(p, k);
            say(p, pn(p) + " readies " + kLeaders[pl.leaders[size_t(k)].leader].name + " (" + name + ").");
        } else {
            drawActionCards(p, 1, name);
        }
        return true;
    }
    if (exIs(c, "hiddenlaboratory")) {  // explore the frontier deck in this system
        const int f = drawExplore(int(ExploreType::Frontier));
        if (f < 0) {
            say(p, "The frontier exploration deck is empty.");
            return true;
        }
        Event e;
        e.kind = EventKind::Explore;
        e.player = int8_t(p);
        e.card = int16_t(f);
        e.system = int16_t(sys);
        e.text = pn(p) + " explores the frontier deck in " + sysName(sys) + ": " + kExploreCards[f].name + " (" + name + ").";
        E.emit(e);
        resolveExplore(p, f, sys, -1);
        return true;
    }
    if (exIs(c, "ancientshipyard")) {
        if (sys >= 0 && !otherShipsIn(G, p, sys) && unitsForPlacement(G, p, UnitType::Cruiser) > 0 &&
            yesNo(p, Tag::ExploreChoice, name + ": place 1 cruiser in " + sysName(sys) + "?", "")) {
            addUnits(p, UnitType::Cruiser, 1, sys, -1);
            say(p, pn(p) + " places 1 cruiser in " + sysName(sys) + " (" + name + ").", sys);
            enforceFleetLimit(p);
        }
        return true;
    }
    if (exIs(c, "folderspace")) {  // Folded Space
        if (reinforcementTokens(p) > 0) {
            pl.fleet = int8_t(pl.fleet + 1);
            say(p, pn(p) + " places 1 command token in their fleet pool (" + name + ").");
        }
        if (sys < 0) return true;
        Force& here = G.systems[sys].space[p];
        if (here.ships() + here.groundForces() == 0) return true;
        std::vector<Option> dest;
        for (size_t d = 0; d < G.systems.size(); ++d)
            if (int(d) != sys && !G.systems[d].offBoard && shipsMayEnter(G, p, int(d)) && !otherShipsIn(G, p, int(d)))
                dest.push_back({int(d), sysName(int(d)), ""});
        if (dest.empty()) return true;
        dest.push_back({-1, "Don't move", ""});
        const int to = pick(p, Tag::ExploreChoice, name + ": move your units from " + sysName(sys) + " to which system?", dest,
                            "Any system without other players' ships.", true);
        if (to < 0) return true;
        std::vector<Option> types;
        for (int t = 0; t < kUnitTypes; ++t)
            if (here.n[t] && (isShip(UnitType(t)) || isGroundForce(UnitType(t))))
                types.push_back({t, std::to_string(here.n[t]) + " " + unitTypeName(UnitType(t), here.n[t] > 1), ""});
        std::vector<int> picks = types.size() == 1 ? std::vector<int>{types[0].id}
                                                   : pickMany(p, Tag::ExploreChoice, name + ": move which units?", types, 1, int(types.size()));
        Force moved;
        Force& dst = G.systems[size_t(to)].space[p];
        for (int t : picks) {
            const int k = here.n[t], dmg = here.dmg[t];
            if (k <= 0) continue;
            here.remove(UnitType(t), k, false);
            dst.add(UnitType(t), k);
            dst.dmg[t] = uint8_t(dst.dmg[t] + dmg);
            moved.add(UnitType(t), k);
        }
        if (moved.ships() + moved.groundForces() == 0) return true;
        Event e;
        e.kind = EventKind::Move;
        e.player = int8_t(p);
        e.system = int16_t(to);
        e.from = {int16_t(sys)};
        e.text = pn(p) + " moves " + unitsText(moved) + " to " + sysName(to) + " (" + name + ").";
        E.emit(e);
        removeExcessCapacity(p, to);
        removeExcessCapacity(p, sys);
        checkNexus(to);
        enforceFleetLimit(p);
        return true;
    }
    if (exIs(c, "suspiciouswreckage")) {  // 2 other players each give you a promissory note
        std::vector<Option> who;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].eliminated && !G.players[q].notes.empty())
                who.push_back({q, pn(q), std::to_string(G.players[q].notes.size()) + " promissory notes in hand"});
        if (who.empty()) return true;
        const int n = std::min<int>(2, int(who.size()));
        std::vector<int> picks = int(who.size()) == n ? std::vector<int>() : pickMany(p, Tag::ExploreChoice, name + ": choose 2 players", who, n, n);
        if (picks.empty())
            for (const Option& o : who) picks.push_back(o.id);
        for (int q : picks) {
            std::vector<Option> notes;
            for (int16_t x : G.players[q].notes) notes.push_back({x, noteNameOf(G, x), noteTextOf(G, x)});
            if (notes.empty()) continue;
            const int prevV = curValue_;
            curValue_ = p;  // (for the computer player: who receives it)
            const int note = notes.size() == 1 ? notes[0].id
                                               : pick(q, Tag::ExploreChoice, name + ": give " + pn(p) + " which promissory note?", notes);
            curValue_ = prevV;
            giveNote(q, p, note);
        }
        return true;
    }
    if (exIs(c, "darkvisions")) {
        // Look at the top card of each exploration deck, discard any of them, explore up to 1 of your planets,
        // then shuffle each deck.
        std::vector<Option> tops;
        for (int t = 0; t < 4; ++t) {
            auto& deck = G.exploreDeck[t];
            if (deck.empty() && !G.exploreDiscard[t].empty()) {
                deck = G.exploreDiscard[t];
                G.exploreDiscard[t].clear();
                E.rng().shuffle(deck);
            }
            if (deck.empty()) continue;
            const int top = deck.back();
            tops.push_back({t, std::string(exploreTypeName(ExploreType(t))) + ": " + kExploreCards[top].name, exploreText(top)});
        }
        E.revealed();
        if (!tops.empty()) {
            say(p, pn(p) + " looks at the top card of each exploration deck (" + name + ").");
            const std::vector<int> drop = pickMany(p, Tag::ExploreChoice, name + ": discard which of these top cards?", tops, 0, int(tops.size()),
                                                   "Then you may explore one of your planets.");
            for (int t : drop) {
                const int top = G.exploreDeck[t].back();
                G.exploreDeck[t].pop_back();
                G.exploreDiscard[t].push_back(int16_t(top));
                say(p, pn(p) + " discards " + kExploreCards[top].name + " from the " + exploreTypeName(ExploreType(t)) + " deck.");
            }
        }
        std::vector<Option> mine;
        for (int x : controlledPlanets(G, p)) {
            const int t = deckOf(G, x);
            if (t < 0) continue;
            const std::string top = G.exploreDeck[t].empty() ? std::string("the deck is empty") : std::string("top card: ") + kExploreCards[G.exploreDeck[t].back()].name;
            mine.push_back({x, planetName(x), std::string(exploreTypeName(ExploreType(t))) + " - " + top});
        }
        if (!mine.empty()) {
            mine.push_back({-1, "Don't explore", ""});
            const int x = pick(p, Tag::ExploreChoice, name + ": explore which of your planets?", mine, "", false, true);
            if (x >= 0) explorePlanet(p, x);
        }
        for (auto& d : G.exploreDeck) E.rng().shuffle(d);
        return true;
    }
    return false;
}

// Arcane Citadel: when its controller exhausts the planet, 1 infantry from reinforcements is placed on it.
void Rules::planetsExhausted(int p, const std::vector<int>& planets) {
    if ((G.modules & kModDiscordant) && p >= 0) ds4PlanetsExhausted(p, planets);  // Olradin policies, Baggil Wildpaw
    // Pilgrimage Beacons (Rhodun, 0.44.7 fixes 11): any exhaust of our specialty planets on our turn - paying,
    // Psychoarchaeology, research... - offers its ship once the action is over (no production in the middle of a payment)
    if ((G.modules & kModDiscordant) && G.fixes >= 11 && p >= 0 && p < G.numPlayers && p == G.activePlayer && G.phase == Phase::Action &&
        owns(p, "dsrhodb") && !G.players[size_t(p)].dsPilgrim)
        for (int x : planets)
            if (x >= 0 && G.planets[size_t(x)].owner == p && planetHasSkip(G, x)) {
                const int s = G.planets[size_t(x)].system;
                if (s >= 0 && std::find(pilgrimSystems_.begin(), pilgrimSystems_.end(), s) == pilgrimSystems_.end()) pilgrimSystems_.push_back(s);
            }
    if (!(G.modules & kModUncharted) || p < 0) return;
    for (int x : planets) {
        if (x < 0 || G.planets[size_t(x)].owner != p || !hasAttachment(G, x, "arcanecitadel")) continue;
        if (unitsAvailable(G, p, UnitType::Infantry) <= 0) continue;
        addUnits(p, UnitType::Infantry, 1, G.planets[size_t(x)].system, x);
        say(p, pn(p) + " places 1 infantry on " + planetName(x) + " (Arcane Citadel).", G.planets[size_t(x)].system);
    }
}

// ------------------------------------------------------------------ a new system at the edge of the board
int Rules::placeNewSystem(int p, int def, Tag tag, const std::string& why) {
    const std::vector<Hex> spots = boardEdgeSpots(G);
    if (spots.empty()) {
        say(p, "There is no room at the edge of the board.");
        return -1;
    }
    std::vector<Option> opts;
    for (Hex h : spots) {
        std::string next;
        int n = 0;
        for (int e = 0; e < 6; ++e) {
            const int j = G.findSystemAt(hexNeighbor(h, e));
            if (j >= 0 && !G.systems[size_t(j)].isHyperlane()) next += (n++ ? ", " : "") + sysName(j);
        }
        opts.push_back({spotId(h), "Beside " + next, std::to_string(n) + " systems next to it"});
    }
    spotPick_ = true;  // (0.61.2: the spaces are picked on the map)
    const int id = opts.size() == 1 ? opts[0].id
                                    : pick(p, tag, why + ": place " + kSystems[def].name + " (" + kSystems[def].key + ") where?", opts,
                                           "At the edge of the board, next to at least 2 systems.");
    spotPick_ = false;
    const int s = addBoardSystem(G, def, spotHex(id));
    // RULES-CHECK: as at setup (LRR 38.1); (fixes 31) a space station isn't a planet (Thunder's Edge), as at setup
    if (G.fixes >= 31 ? planetless(G, s) : G.systems[size_t(s)].planets.empty()) G.systems[size_t(s)].frontier = true;
    computeAdjacency(G);
    {
        Event e;
        e.kind = EventKind::Log;
        e.player = int8_t(p);
        e.system = int16_t(s);
        e.text = pn(p) + " adds " + sysName(s) + " to the edge of the galaxy (" + why + ").";
        E.emit(e);
    }
    return s;
}

bool Rules::starChartAction(int p) {
    Player& pl = G.players[p];
    if (starChartCount(G, p) < 2 || blueTilesLeft(G).empty() || boardEdgeSpots(G).empty()) return false;
    const int prev = curExplore_;
    for (int16_t f : pl.fragments)
        if (kExploreCards[f].kind == ExploreKind::Chart) curExplore_ = f;
    int left = 2;
    for (size_t i = 0; i < pl.fragments.size() && left > 0;) {
        if (kExploreCards[pl.fragments[i]].kind == ExploreKind::Chart) {
            pl.fragments.erase(pl.fragments.begin() + long(i));
            --left;
        } else {
            ++i;
        }
    }
    say(p, pn(p) + " purges 2 Star Charts.");
    purged(p);
    const std::vector<int> tiles = blueTilesLeft(G);
    const int def = tiles[size_t(E.rng().below(int(tiles.size())))];
    E.revealed();
    placeNewSystem(p, def, Tag::ExploreChoice, "Star Charts");
    curExplore_ = prev;
    return true;
}

// Azdel's Key: draw 3 blue tiles, place 1 of them, purge the rest.
bool Rules::azdelsKey(int p) {
    std::vector<int> tiles = blueTilesLeft(G);
    if (tiles.empty() || boardEdgeSpots(G).empty()) return false;
    const int r = findRelic("decrypted_cartoglyph");
    const int prev = curRelic_;
    curRelic_ = r;
    loseRelic(p, r, true);
    E.rng().shuffle(tiles);
    tiles.resize(std::min<size_t>(3, tiles.size()));
    E.revealed();
    std::vector<Option> opts;
    for (int t : tiles) {
        std::string what;
        for (int k = 0; k < kSystems[t].planetCount; ++k) {
            const PlanetDef& pd = kPlanets[kSystems[t].planets[k]];
            what += (k ? ", " : "") + std::string(pd.name) + " " + std::to_string(pd.res) + "/" + std::to_string(pd.inf);
        }
        if (what.empty()) what = "no planets";
        opts.push_back({t, std::string(kSystems[t].name) + " (" + kSystems[t].key + ")", what});
    }
    const int def = opts.size() == 1 ? opts[0].id : pick(p, Tag::RelicChoice, "Azdel's Key: place which system?", opts, "The others are purged.");
    for (int t : tiles)
        if (t != def) G.purgedTiles.push_back(int16_t(t));
    placeNewSystem(p, def, Tag::RelicChoice, "Azdel's Key");
    curRelic_ = prev;
    return true;
}

// ------------------------------------------------------------------ the other relics
// Forgotten Throne: secrets that could be scored with it - other players' scored ones and your own unscored
// ones, whose requirements you meet now.
std::vector<int> Rules::throneSecrets(int p) const {
    std::vector<int> v;
    for (int q = 0; q < G.numPlayers; ++q)
        for (int16_t o : (q == p ? G.players[q].secrets : G.players[q].scored))
            if (kObjectives[o].kind == 3 && !hasScored(G, p, o) && meetsObjective(G, p, o) &&
                std::find(v.begin(), v.end(), int(o)) == v.end())
                v.push_back(o);
    return v;
}

bool Rules::forgottenThrone(int p) {
    const int r = findRelic("throne_of_the_false_emperor");
    std::vector<Option> opts;
    if (!G.relicDeck.empty()) opts.push_back({-2, "Gain 1 relic", ""});
    for (int o : throneSecrets(p))
        opts.push_back({o, std::string("Score ") + kObjectives[o].name + " - 1 VP",
                        std::string(hasScored(G, p, o) ? "" : std::find(G.players[p].secrets.begin(), G.players[p].secrets.end(), int16_t(o)) != G.players[p].secrets.end()
                                                                   ? "your secret objective: "
                                                                   : "scored by another player: ") +
                            objectiveSummary(o)});
    if (opts.empty()) return false;
    opts.push_back({-1, "Cancel", ""});
    const int prev = curRelic_;
    curRelic_ = r;
    const int c = pick(p, Tag::RelicChoice, "Forgotten Throne: purge it to do what?", opts,
                       // 0.43.0 (U7): what "score 1 scored secret objective" means here
                       "A secret objective another player has already scored, whose condition you meet too - or one of your own "
                       "unscored secrets you meet.");
    curRelic_ = prev;
    if (c == -1) return false;
    loseRelic(p, r, true);
    if (c == -2) gainRelic(p, "Forgotten Throne");
    else if (!scoreObjective(p, c)) say(p, pn(p) + " can't score " + kObjectives[c].name + " after all.");
    return true;
}

void Rules::e6g0(int p) {
    if (!relicReady(p, "e6-g0_network")) return;
    exhaustRelic(p, "e6-g0_network");
    drawActionCards(p, 1, "E6-G0 Network");
}

// Eye of Vogul: after activating a system, purge it - 2 of your ships count as adjacent to the active system.
void Rules::eyeOfVogul(int p, int sys) {
    if (!(G.modules & kModUncharted) || !holdsRelic(p, "eye_of_vogul")) return;
    std::vector<Option> ships;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (int(s) == sys || (G.systems[s].hasToken(p) && !G.fx.dominusOrb && !freeholdAt(G, p, int(s)))) continue;  // (0.82.2 Freehold)
        const Force& f = G.systems[s].space[p];
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!f.n[t] || !isShip(UnitType(t)) || moveValue(G, p, int(s), UnitType(t)) <= 0) continue;
            if (shipReach(G, p, int(s), sys, UnitType(t), 0).ok) continue;  // gets there anyway
            for (int k = 0; k < std::min<int>(2, f.n[t]); ++k)
                ships.push_back({int(s) * 16 + t + k * 4096, std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)), ""});
        }
    }
    if (ships.empty()) return;
    const int r = findRelic("eye_of_vogul");
    const int prev = curRelic_;
    curRelic_ = r;
    std::vector<int> picks;
    if (yesNo(p, Tag::RelicChoice, "Purge the Eye of Vogul?", "2 of your ships count as adjacent to " + sysName(sys) + " this tactical action."))
        picks = pickMany(p, Tag::RelicChoice, "Eye of Vogul: which 2 ships?", ships, 1, std::min<int>(2, int(ships.size())));
    curRelic_ = prev;
    if (picks.empty()) return;
    loseRelic(p, r, true);
    for (int id : picks) G.fx.vogul.push_back(int16_t(id % 4096));
    say(p, pn(p) + "'s chosen ships count as adjacent to " + sysName(sys) + " (Eye of Vogul).", sys);
}

// Twilight Mirror: at the start of the agenda phase, purge it to take 1 action that isn't a strategic action.
void Rules::twilightMirror() {
    if (!(G.modules & kModUncharted)) return;
    for (int p : clockwiseFrom(G, G.speaker)) {
        Player& pl = G.players[p];
        if (pl.eliminated || !holdsRelic(p, "twilight_mirror")) continue;
        std::vector<Option> opts;
        if (pl.tactic > 0) opts.push_back({0, "Tactical action", "Activate a system: move, fight, invade and produce."});
        for (const Option& o : componentActionOptions(p)) opts.push_back(o);
        if (opts.empty()) continue;
        opts.push_back({98, "Keep the Twilight Mirror", "Don't take an action now."});  // (98: "end turn", no pass prompt)
        const int r = findRelic("twilight_mirror");
        const int c = pick(p, Tag::ActionChoice, "Twilight Mirror: purge it to take an action now?", opts,
                           "At the start of the agenda phase: one tactical or component action (not a strategic action).");
        if (c == 98) continue;
        say(p, pn(p) + " looks into the Twilight Mirror: one more action before the agenda.");
        const Phase was = G.phase;
        const int8_t active = G.activePlayer;
        G.phase = Phase::Action;  // the action is resolved as in the action phase
        G.activePlayer = int8_t(p);
        lastActiveSystem_ = -1;
        turnEnded_ = false;
        bool done = false;
        if (c == 0) {
            const int before = pl.tactic;
            tacticalAction(p);
            done = G.fixes >= 4 ? activated_ : pl.tactic != before;  // (0.43.3: see takeTurn)
        } else {
            done = componentAction(p, c);
        }
        G.phase = was;
        G.activePlayer = active;
        if (done) loseRelic(p, r, true);
    }
}

}  // namespace ti
