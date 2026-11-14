// Prophecy of Kings exploration (LRR 35), frontier tokens (38), attachments
// (12), relic fragments and relics (73), the wormhole nexus (100), legendary
// planet abilities, and the technologies that explore (Dark Energy Tap,
// Scanlink Drone Network) or use specialties (Psychoarchaeology).
#include <algorithm>
#include <cstring>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
bool exIs(int card, const char* k) { return card >= 0 && card < kExploreCardCount && !std::strcmp(kExploreCards[card].key, k); }
bool relicIs(int r, const char* k) { return r >= 0 && r < kRelicCount && !std::strcmp(kRelics[r].key, k); }
int traitDeck(Trait t) {
    switch (t) {
        case Trait::Cultural: return int(ExploreType::Cultural);
        case Trait::Hazardous: return int(ExploreType::Hazardous);
        case Trait::Industrial: return int(ExploreType::Industrial);
        default: return -1;
    }
}
}  // namespace

// ------------------------------------------------------------------ setup
void Rules::setupExploration() {
    for (int t = 0; t < 4; ++t) {
        G.exploreDeck[t].clear();
        G.exploreDiscard[t].clear();
    }
    const bool us = (G.modules & kModUncharted) != 0;  // Uncharted Space cards and relics (0.33.0) only with the module
    for (int c = 0; c < kExploreCardCount; ++c)
        if (us || kExploreCards[c].source != Source::Uncharted) {
            // (0.43.0, U1: War Forge Ruins and Arcane Shield Emitter join only new games' decks)
            if (G.fixes < 2 && (exIs(c, "warforgeruins") || exIs(c, "arcaneshieldemitter"))) continue;
            for (int k = 0; k < kExploreCards[c].count; ++k) G.exploreDeck[int(kExploreCards[c].type)].push_back(int16_t(c));
        }
    for (auto& d : G.exploreDeck) E.rng().shuffle(d);
    G.relicDeck.clear();
    const bool te = (G.modules & kModThundersEdge) != 0;  // Thunder's Edge relics (0.34.0) only with that module
    for (int r = 0; r < kRelicCount; ++r) {
        if (kRelics[r].source == Source::ThundersEdge && !(te && teRelicReady(r))) continue;
        if (us || kRelics[r].source != Source::Uncharted) G.relicDeck.push_back(int16_t(r));
    }
    E.rng().shuffle(G.relicDeck);
    // LRR 38.1: a frontier token in every system without planets (not hyperlanes). (fixes 26, the Empyrean review - owner
    // 2026-11-12: the Thunder's Edge rulebook - "A system that contains a space station and no planets gets a frontier token
    // during setup")
    for (size_t x = 0; x < G.systems.size(); ++x) {
        SystemState& s = G.systems[x];
        if (!s.isHyperlane() && (G.fixes >= 26 ? planetless(G, int(x)) : s.planets.empty())) s.frontier = true;
    }
}

int Rules::drawExplore(int type) {
    auto& deck = G.exploreDeck[type];
    if (deck.empty()) {
        deck = G.exploreDiscard[type];
        G.exploreDiscard[type].clear();
        E.rng().shuffle(deck);
    }
    if (deck.empty()) return -1;
    int c = deck.back();
    deck.pop_back();
    E.revealed();
    return c;
}

// ------------------------------------------------------------------ exploring
void Rules::explorePlanet(int p, int planet) {
    const PlanetState& ps = G.planets[planet];
    if (ps.destroyed) return;
    int type = traitDeck(kPlanets[ps.def].trait);
    if (ps.terraform) {  // Terraform: all three traits - the explorer picks the deck
        std::vector<Option> decks;
        for (Trait t : {Trait::Cultural, Trait::Hazardous, Trait::Industrial})
            decks.push_back({traitDeck(t), exploreTypeName(ExploreType(traitDeck(t))), ""});
        type = pick(p, Tag::ExploreOrder, "Explore " + planetName(planet) + " with which deck? (Terraform)", decks);
    } else if (kPlanets[ps.def].trait2 != Trait::None && traitDeck(kPlanets[ps.def].trait2) >= 0) {
        // Thunder's Edge: a planet with two traits - its explorer picks one of the two decks
        std::vector<Option> decks;
        for (Trait t : {kPlanets[ps.def].trait, kPlanets[ps.def].trait2})
            decks.push_back({traitDeck(t), exploreTypeName(ExploreType(traitDeck(t))), ""});
        type = pick(p, Tag::ExploreOrder, "Explore " + planetName(planet) + " with which deck?", decks, "It has two traits.");
    }
    if (type < 0) return;  // LRR 35.2b: no trait, nothing to explore
    if (G.modules & kModDiscordant) {  // Deep Mining (Gledge): a trade good instead; Black Markets (Florzen): another trait
        type = dsExploreDeck(p, planet, type);
        if (type < 0) return;
    }
    const bool dsbt = (G.modules & kModDiscordant) && breakthroughsOn(G);
    if (dsbt && dsbtExploreInstead(p, planet)) return;  // Historian Conclave (Bentor)
    const int museum = dsbt ? dsbtMuseum(p, type) : -1;  // The Collector's Museum (Kollecc)
    int c = museum >= 0 ? museum : drawExplore(type);
    if (c < 0) {
        say(p, std::string("The ") + exploreTypeName(ExploreType(type)) + " exploration deck is empty.");
        return;
    }
    if (fac(p, "naaz") && ps.units[p].count(UnitType::Mech)) c = distantSuns(p, planet, type, c);  // Distant Suns
    if (G.modules & kModDiscordant) c = dsRedraw(p, type, c);  // Vassa Hagi (Lanefir agent)
    if ((G.modules & kModDiscordant) && breakthroughsOn(G) && dsbtExploreCard(p, planet, c)) {  // Cryptic Insights (0.43.0)
        if (G.fixes >= 8) {  // 0.44.4: the planet was still explored
            explored4(p, planet);
            dsExplored(p, planet);
        }
        return;
    }
    {
        Event e;
        e.kind = EventKind::Explore;
        e.player = int8_t(p);
        e.card = int16_t(c);
        e.system = ps.system;
        e.planet = int16_t(planet);
        e.text = pn(p) + " explores " + planetName(planet) + ": " + kExploreCards[c].name + ".";
        E.emit(e);
    }
    const size_t discardBefore = G.exploreDiscard[size_t(type)].size();
    resolveExplore(p, c, ps.system, planet);
    if (museum >= 0 && G.fixes >= 8) {  // 0.44.4: only if this card was discarded (decks hold copies; an attached one stays)
        auto& d = G.exploreDiscard[size_t(type)];
        if (d.size() > discardBefore && d.back() == int16_t(museum)) {
            d.pop_back();
            G.exploreDeck[size_t(type)].push_back(int16_t(museum));
            E.rng().shuffle(G.exploreDeck[size_t(type)]);
        }
    } else if (museum >= 0) {  // (the museum's card goes back into its deck, shuffled, rather than to the discard pile)
        auto& d = G.exploreDiscard[type];
        auto it = std::find(d.rbegin(), d.rend(), int16_t(museum));
        if (it != d.rend()) {
            d.erase(std::next(it).base());
            G.exploreDeck[type].push_back(int16_t(museum));
            E.rng().shuffle(G.exploreDeck[type]);
        }
    }
    explored4(p, planet);  // Terragenesis, Pre-Fab Arcologies
    if (G.modules & kModDiscordant) dsExplored(p, planet);  // ATS Armaments, Master Halbert
}

void Rules::exploreFrontier(int p, int sys) {
    SystemState& s = G.systems[sys];
    if (!s.frontier) return;
    s.frontier = false;  // LRR 35.6
    int c = drawExplore(int(ExploreType::Frontier));
    if (c < 0) {
        say(p, "The frontier exploration deck is empty.");
        return;
    }
    if (G.modules & kModDiscordant) c = dsFrontierRedraw(p, c);  // Voidsailors (Nivyn)
    {
        Event e;
        e.kind = EventKind::Explore;
        e.player = int8_t(p);
        e.card = int16_t(c);
        e.system = int16_t(sys);
        e.text = pn(p) + " explores the frontier token in " + sysName(sys) + ": " + kExploreCards[c].name + ".";
        E.emit(e);
    }
    resolveExplore(p, c, sys, -1);
    if (G.modules & kModDiscordant) dsExplored(p, -1);  // ATS Armaments
    if (G.modules & kModDiscordant) dsFrontierExplored(p, sys);  // Fortune Seekers, Migrant Fleet
}

void Rules::resolveExplore(int p, int c, int sys, int planet) {
    Player& pl = G.players[p];
    const int prev = curExplore_;
    curExplore_ = c;
    const int maxComm = commodityCap(G, pl);  // (Age of Commerce: no limit)
    bool discard = true;  // LRR 35.7: instant cards go to the discard pile
    auto mechOrInfantry = [&](const std::string& reward) -> bool {
        // "If you have a mech on this planet, or if you remove 1 infantry from it"
        if (planet < 0) return false;
        Force& f = G.planets[planet].units[p];
        if (f.count(UnitType::Mech) > 0) return true;
        if (f.count(UnitType::Infantry) == 0) return false;
        if (!yesNo(p, Tag::ExploreChoice, std::string(kExploreCards[c].name) + ": remove 1 infantry from " + planetName(planet) + "?",
                   reward))
            return false;
        f.remove(UnitType::Infantry, 1, false);
        say(p, pn(p) + " removes 1 infantry from " + planetName(planet) + ".", sys);
        return true;
    };

    switch (kExploreCards[c].kind) {
        case ExploreKind::Chart:  // Star Chart (Uncharted Space): kept, two of them make a new system
            pl.fragments.push_back(int16_t(c));
            say(p, pn(p) + " keeps a Star Chart (" + std::to_string(starChartCount(G, p)) + " now).");
            discard = false;
            break;
        case ExploreKind::Fragment:
            pl.fragments.push_back(int16_t(c));
            say(p, pn(p) + (std::strchr("AEIOU", kExploreCards[c].name[0]) ? " gains an " : " gains a ") + kExploreCards[c].name + ".");
            discard = false;
            break;
        case ExploreKind::Attach: {
            if (planet < 0) break;
            PlanetState& ps = G.planets[planet];
            if (exIs(c, "dmz")) {
                // structures go back to reinforcements, ground forces to the space area
                for (int q = 0; q < G.numPlayers; ++q) {
                    Force& f = ps.units[q];
                    for (UnitType t : {UnitType::PDS, UnitType::SpaceDock}) f.remove(t, f.count(t), false);
                    for (UnitType t : {UnitType::Infantry, UnitType::Mech}) {
                        int k = f.count(t);
                        if (!k) continue;
                        f.remove(t, k, false);
                        G.systems[sys].space[q].add(t, k);
                    }
                    removeExcessCapacity(q, sys);
                }
            }
            if (exIs(c, "arcaneshieldemitter"))  // Arcane Shield Emitter (0.43.0): the infantry go up to the space area
                for (int q = 0; q < G.numPlayers; ++q) {
                    Force& f = ps.units[q];
                    const int k = f.count(UnitType::Infantry);
                    if (!k) continue;
                    f.remove(UnitType::Infantry, k, false);
                    G.systems[sys].space[q].add(UnitType::Infantry, k);
                    removeExcessCapacity(q, sys);  // (without room in the ships they are destroyed)
                }
            ps.attachments.push_back(int16_t(c));
            say(p, std::string(kExploreCards[c].name) + " is attached to " + planetName(planet) + ".", sys);
            discard = false;
            break;
        }
        case ExploreKind::Token:
            discard = false;
            if (exIs(c, "gw") || exIs(c, "gamma")) {
                G.systems[sys].tokenWormholes |= kGamma;
                computeAdjacency(G);
                say(p, "A gamma wormhole appears in " + sysName(sys) + ".", sys);  // the card is purged
            } else if (exIs(c, "ion")) {
                int side = pick(p, Tag::ExploreChoice, "Ion Storm: which side faces up?",
                                {{0, "Alpha wormhole", ""}, {1, "Beta wormhole", ""}},
                                "It flips each time ships travel through it.");
                if (G.ionStorm >= 0) G.systems[G.ionStorm].tokenWormholes &= uint8_t(~(kAlpha | kBeta));
                G.ionStorm = int16_t(sys);
                G.ionAlpha = side == 0;
                G.systems[sys].tokenWormholes |= G.ionAlpha ? kAlpha : kBeta;
                computeAdjacency(G);
                say(p, std::string("An ion storm (") + (G.ionAlpha ? "alpha" : "beta") + " wormhole) appears in " + sysName(sys) + ".",
                    sys);
            } else if (exIs(c, "mirage")) {
                // The Mirage planet appears in this system; the explorer gains it, ready.
                int def = findPlanet("mirage");
                PlanetState ps;
                ps.def = int16_t(def);
                ps.system = int16_t(sys);
                int idx = int(G.planets.size());
                G.planets.push_back(ps);
                G.systems[sys].planets.push_back(int16_t(idx));
                G.planets[idx].owner = int8_t(p);
                G.planets[idx].exhausted = false;
                event(EventKind::Control, p, pn(p) + " gains the Mirage planet in " + sysName(sys) + ".", sys, idx);
                // RULES-CHECK: LRR 25.1c / 35.1 - nobody controlled Mirage, so the new owner explores it.
                explorePlanet(p, idx);
            }
            break;
        case ExploreKind::Instant: {
            if (exIs(c, "frln")) {
                // Freelancers: produce 1 unit in this system, influence may pay as resources.
                std::vector<Option> opts;
                int canPay = spendableMixed(G, p);
                for (int t = 0; t < kUnitTypes; ++t) {
                    UnitType ut = UnitType(t);
                    if (isStructure(ut) || unitsAvailable(G, p, ut) <= 0 || unitCost2(G, p, ut) <= 0) continue;
                    int cost = (unitCost2(G, p, ut) + 1) / 2;
                    if (cost > canPay) continue;
                    if (isGroundForce(ut) && (planet < 0 || isDMZ(G, planet))) continue;
                    opts.push_back({t, unitDef(G, p, ut).name, std::to_string(cost) + " resources or influence"});
                }
                opts.push_back({-1, "Nothing", ""});
                int t = pick(p, Tag::ExploreChoice, "Freelancers: produce 1 unit in " + sysName(sys) + "?", opts,
                             "Influence can be spent as if it were resources.");
                if (t >= 0) {
                    UnitType ut = UnitType(t);
                    int cost = (unitCost2(G, p, ut) + 1) / 2;
                    // influence may be spent as if it were resources: each planet pays its higher value
                    if (pay(p, cost, false, "Freelancers", false, 0, 0, true)) {
                        addUnits(p, ut, 1, sys, isGroundForce(ut) ? planet : -1);
                        say(p, pn(p) + " produces 1 " + unitTypeName(ut) + " (Freelancers).", sys);
                        if (G.modules & kModThundersEdge) phantomProducedOne(p, sys, ut, 1);  // Homesick Phantom (fixes 24)
                        removeExcessCapacity(p, sys);
                        enforceFleetLimit(p);
                    }
                }
            } else if (exIs(c, "mo")) {
                if (planet >= 0 && unitsAvailable(G, p, UnitType::Infantry) > 0 && !isDMZ(G, planet) &&
                    yesNo(p, Tag::ExploreChoice, "Mercenary Outfit: place 1 infantry on " + planetName(planet) + "?", ""))
                    addUnits(p, UnitType::Infantry, 1, sys, planet);
            } else if (exIs(c, "vfs")) {
                if (mechOrInfantry("Gain 1 command token.")) gainTokens(p, 1, false, "Volatile Fuel Source: gain 1 command token.");
            } else if (exIs(c, "exp")) {
                if (planet >= 0 && G.planets[planet].exhausted && mechOrInfantry("Ready " + planetName(planet) + ".")) {
                    G.planets[planet].exhausted = false;
                    say(p, planetName(planet) + " is readied.", sys);
                }
            } else if (exIs(c, "cm")) {
                if (mechOrInfantry("Gain 1 trade good.")) {
                    gainTG(p, 1);
                    say(p, pn(p) + " gains 1 trade good.");
                }
            } else if (exIs(c, "aw")) {
                std::vector<Option> o = {{0, "Gain 2 commodities", std::to_string(pl.commodities) + " of " + std::to_string(maxComm) + " now"}};
                if (pl.commodities > 0) o.push_back({1, "Convert up to 2 commodities to trade goods", ""});
                if (pick(p, Tag::ExploreChoice, "Abandoned Warehouses", o) == 0) {
                    const int before = pl.commodities;
                    pl.commodities = int8_t(std::min(maxComm, pl.commodities + 2));
                    siphon(p, pl.commodities - before);
                } else {
                    int n = std::min<int>(2, pl.commodities);
                    pl.commodities = int8_t(pl.commodities - n);
                    gainTG(p, n);
                }
            } else if (exIs(c, "fb") || exIs(c, "lf")) {
                bool fab = exIs(c, "lf");
                std::vector<Option> o = {{0, "Gain 1 commodity", ""}};
                bool canSpend = pl.tradeGoods > 0 || pl.commodities > 0;
                bool canDo = fab ? (planet >= 0 && unitsAvailable(G, p, UnitType::Mech) > 0 && !isDMZ(G, planet)) : true;
                if (canSpend && canDo)
                    o.push_back({1, fab ? "Spend 1 trade good or commodity: place 1 mech here" : "Spend 1 trade good or commodity: draw 1 action card", ""});
                if (pick(p, Tag::ExploreChoice, kExploreCards[c].name, o) == 0) {
                    const int before = pl.commodities;
                    pl.commodities = int8_t(std::min(maxComm, pl.commodities + 1));
                    siphon(p, pl.commodities - before);
                } else {
                    if (pl.commodities > 0) pl.commodities = int8_t(pl.commodities - 1);
                    else pl.tradeGoods = int8_t(pl.tradeGoods - 1);
                    if (fab) addUnits(p, UnitType::Mech, 1, sys, planet);
                    else drawActionCards(p, 1, "Functioning Base");
                }
            } else if (exIs(c, "dv")) {
                drawSecret(p);
            } else if (exIs(c, "ed")) {
                pl.enigmatic = int8_t(pl.enigmatic + 1);
                discard = false;  // stays in the play area until used
                say(p, pn(p) + " keeps an Enigmatic Device (research a technology for 6 resources).");
            } else if (exIs(c, "lc")) {
                drawActionCards(p, 2, "Lost Crew");
            } else if (usExplore(p, c, sys, planet)) {  // Uncharted Space (0.33.0)
            } else if (exIs(c, "ms")) {
                std::vector<Option> o = {{0, "Refill your commodities", std::to_string(pl.commodities) + " of " + std::to_string(maxComm) + " now"}};
                if (pl.commodities > 0) o.push_back({1, "Convert your commodities to trade goods", ""});
                if (pick(p, Tag::ExploreChoice, "Merchant Station", o) == 0) {
                    const int before = pl.commodities;
                    pl.commodities = int8_t(refreshTo(G, pl));
                    siphon(p, pl.commodities - before);
                } else {
                    const int conv_ = pl.commodities;
                    pl.commodities = 0;
                    gainTG(p, conv_);
                }
            }
            break;
        }
    }
    if (discard) G.exploreDiscard[int(kExploreCards[c].type)].push_back(int16_t(c));
    curExplore_ = prev;
}

// Called by setControl after the new owner is set.
void Rules::afterGainControl(int planet, int p, int old) {
    // (0.43.0 debug passes: no reference held - exploring can add planets to G.planets and move it; ASan)
    auto Ps = [&]() -> const PlanetState& { return G.planets[size_t(planet)]; };
    // Shard of the Throne: taking its owner's legendary or home planet takes the relic.
    if (old >= 0 && old != p) {
        int shard = findRelic("shard");
        bool home = G.systems[Ps().system].home == old;
        if (G.players[old].hasRelic(shard) && (kPlanets[Ps().def].legendary || home)) {
            loseRelic(old, shard, false);
            takeRelic(p, shard);
        }
    }
    // Mallice: taking it flips the wormhole nexus.
    if (Ps().system == G.nexus) checkNexus(G.nexus);
    if (!std::strcmp(kPlanets[Ps().def].key, "mallice") && G.nexus >= 0 && !G.systems[G.nexus].nexusActive) {
        G.systems[G.nexus].nexusActive = true;
        computeAdjacency(G);
        say(p, "The wormhole nexus becomes active.", G.nexus);
        nexusTokensBack();
    }
    // LRR 35.1: a planet nobody controlled is explored.
    if (old < 0 && G.phase != Phase::Setup) explorePlanet(p, planet);
    // Thunder's Edge (0.34.0): relic icons, Styx's victory point, the Thunder's Edge planet's breakthrough, Fealty Uplink
    if (G.modules & kModThundersEdge) {
        relicIcon(planet, p);
        if (!std::strcmp(kPlanets[Ps().def].key, "styx")) {
            if (old >= 0 && old != p) loseVP(old, 1, "lost Styx");
            if (p >= 0) gainVP(p, 1, "Styx");
        }
        if (!std::strcmp(kPlanets[Ps().def].key, "thundersedge")) gainBreakthrough(p, "Thunder's Edge");
        btControl(planet, p);
        liberate(planet, p);  // Last Bastion
    }
}

void Rules::checkNexus(int sys) {
    if (sys < 0 || sys != G.nexus || G.systems[sys].nexusActive) return;
    bool units = false;
    for (int q = 0; q < G.numPlayers; ++q)
        if (hasUnits(G, q, sys)) units = true;
    if (!units) return;
    G.systems[sys].nexusActive = true;
    computeAdjacency(G);
    say(-1, "The wormhole nexus becomes active: its alpha and beta wormholes open.", sys);
    nexusTokensBack();
}

void Rules::ionStormUsed(int sys) {
    if (sys < 0 || sys != G.ionStorm) return;
    G.systems[sys].tokenWormholes &= uint8_t(~(kAlpha | kBeta));
    G.ionAlpha = !G.ionAlpha;
    G.systems[sys].tokenWormholes |= G.ionAlpha ? kAlpha : kBeta;
    computeAdjacency(G);
    say(-1, std::string("The ion storm flips to its ") + (G.ionAlpha ? "alpha" : "beta") + " side.", sys);
}

// ------------------------------------------------------------------ relics
bool Rules::holdsRelic(int p, const char* key) const { return p >= 0 && p < G.numPlayers && G.players[p].hasRelic(findRelic(key)); }
bool Rules::relicReady(int p, const char* key) const {
    if (p < 0 || p >= G.numPlayers) return false;
    int r = findRelic(key);
    return G.players[p].hasRelic(r) && !G.players[p].relicExhausted(r);
}
void Rules::exhaustRelic(int p, const char* key) {
    int r = findRelic(key);
    if (r >= 0 && !G.players[p].relicExhausted(r)) {
        G.players[p].exhaustedRelics.push_back(int16_t(r));
        relicUsedEvent(p, r, false);
    }
}

// The relic card is shown large when it's used (owner, 0.16.0).
void Rules::relicUsedEvent(int p, int r, bool purge) {
    Event e;
    e.kind = EventKind::RelicUsed;
    e.player = int8_t(p);
    e.card = int16_t(r);
    e.label = purge ? "purges" : "uses";
    if (!purge) e.text = pn(p) + " uses " + kRelics[r].name + ".";  // (a purge is already said in the feed)
    E.emit(e);
}

void Rules::gainRelic(int p, const std::string& why) {
    if (G.relicDeck.empty()) {  // LRR 73.2a
        say(p, "The relic deck is empty.");
        return;
    }
    int r = G.relicDeck.back();
    G.relicDeck.pop_back();
    E.revealed();
    if (G.modules & kModDiscordant) r = dsRelicDraw(p, r);  // Data Leak (Florzen), Underground Market
    if ((G.modules & kModDiscordant) && dsRelicGained(p, r)) return;  // A New Edifice (Lanefir): purged instead
    {
        Event e;
        e.kind = EventKind::Relic;
        e.player = int8_t(p);
        e.card = int16_t(r);
        e.text = pn(p) + " gains the relic " + kRelics[r].name + (why.empty() ? "" : " (" + why + ")") + ".";
        E.emit(e);
    }
    takeRelic(p, r);
}

void Rules::takeRelic(int p, int r) {
    G.players[p].relics.push_back(int16_t(r));
    if (relicIs(r, "shard")) gainVP(p, 1, "Shard of the Throne");
    if (relicIs(r, "obsidian")) drawSecret(p);
    teTakeRelic(p, r);  // Thunder's Edge: Book of Latvinia
    iconoclastTE(p);    // (fixes 27) Thunder's Edge's Iconoclast: a Naalu mech when another player gains a relic
}

void Rules::loseRelic(int p, int r, bool purge) {
    Player& pl = G.players[p];
    auto it = std::find(pl.relics.begin(), pl.relics.end(), int16_t(r));
    if (it == pl.relics.end()) return;
    if (purge && G.fixes < 8 && (G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtPurged(p);  // Erasure Corps (Lanefir, 0.43.0; fixes 8: in purged())
    pl.relics.erase(it);
    auto ex = std::find(pl.exhaustedRelics.begin(), pl.exhaustedRelics.end(), int16_t(r));
    if (ex != pl.exhaustedRelics.end()) pl.exhaustedRelics.erase(ex);
    if (relicIs(r, "shard")) loseVP(p, 1, "lost the Shard of the Throne");
    if (purge) {
        say(p, pn(p) + " purges " + kRelics[r].name + ".");
        relicUsedEvent(p, r, true);
        purged(p);
    }
}

void Rules::purgeFragments(int p, int count, int type, const std::string& why) {
    Player& pl = G.players[p];
    if (G.fixes < 8 && (G.modules & kModDiscordant) && breakthroughsOn(G) && count > 0) dsbtPurged(p);  // Erasure Corps (0.43.0; fixes 8: purged())
    const int wanted = count;
    // exact type first, unknown fragments last
    for (int pass = 0; pass < 2 && count > 0; ++pass)
        for (size_t i = 0; i < pl.fragments.size() && count > 0;) {
            ExploreType t = kExploreCards[pl.fragments[i]].type;
            bool ok = pass == 0 ? (type < 0 ? t != ExploreType::Frontier : int(t) == type) : true;
            if (!isRelicFragment(pl.fragments[i])) ok = false;  // a Star Chart (Uncharted Space)
            if (ok) {
                pl.fragments.erase(pl.fragments.begin() + long(i));
                --count;
            } else {
                ++i;
            }
        }
    say(p, pn(p) + " purges relic fragments (" + why + ").");
    if (G.modules & kModDiscordant) dsFragmentsPurged(p, wanted - count);  // ATS Armaments, Master Halbert
    purged(p);
}

bool Rules::fragmentAction(int p, int type) {
    const int need = eventOn(G, kEvExploration) ? 2 : 3;  // Age of Exploration (galactic event)
    if (relicFragments(G, p, type) < need) return false;
    purgeFragments(p, need, type, std::to_string(need) + " " + exploreTypeName(ExploreType(type)) + " fragments");
    gainRelic(p, "relic fragments");
    return true;
}

bool Rules::relicAction(int p, int r) {
    Player& pl = G.players[p];
    const int prev = curRelic_;
    curRelic_ = r;
    bool done = false;
    if (relicIs(r, "codex")) {
        std::vector<Option> opts;
        for (int16_t c : G.actionDiscard) {
            bool dup = false;
            for (const Option& o : opts)
                if (o.id == c) dup = true;
            if (!dup) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
        }
        if (!opts.empty()) {
            std::vector<int> picks = pickMany(p, Tag::RelicChoice, "The Codex: take up to 3 action cards from the discard pile", opts, 1,
                                              std::min<int>(3, int(opts.size())));
            loseRelic(p, r, true);
            for (int c : picks) {
                auto it = std::find(G.actionDiscard.begin(), G.actionDiscard.end(), int16_t(c));
                if (it != G.actionDiscard.end()) G.actionDiscard.erase(it);
                pl.actionCards.push_back(int16_t(c));
            }
            say(p, pn(p) + " takes " + std::to_string(picks.size()) + " action cards from the discard pile.");
            enforceHandLimit(p);
            done = true;
        }
    } else if (teRelicAction(p, r)) {  // Thunder's Edge (0.34.0)
        done = true;
    } else if (relicIs(r, "decrypted_cartoglyph")) {  // Uncharted Space (0.33.0)
        done = azdelsKey(p);
    } else if (relicIs(r, "throne_of_the_false_emperor")) {
        done = forgottenThrone(p);
    } else if (relicIs(r, "stellarconverter")) {
        std::vector<Option> opts;
        int mec = G.mecatol();
        for (size_t s = 0; s < G.systems.size(); ++s) {
            const SystemState& sy = G.systems[s];
            if ((sy.home >= 0 || int(s) == mec) && !eventOn(G, kEvWild)) continue;  // (Wild, Wild Galaxy: anywhere)
            bool near = false;
            for (int16_t a : sy.adj) {
                const SystemState& as = G.systems[a];
                for (int t = 0; t < kUnitTypes; ++t)
                    if (as.space[p].n[t] && unitDef(G, p, UnitType(t)).bombard > 0) near = true;
                for (int16_t x : as.planets)
                    for (int t = 0; t < kUnitTypes; ++t)
                        if (G.planets[x].units[p].n[t] && unitDef(G, p, UnitType(t)).bombard > 0) near = true;
            }
            if (!near) continue;
            for (int16_t x : sy.planets)
                if (!kPlanets[G.planets[x].def].legendary || eventOn(G, kEvWild))
                    opts.push_back({x, planetName(x),
                                    G.planets[x].owner >= 0 ? pn(G.planets[x].owner) + "'s planet" : std::string("unclaimed")});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Cancel", ""});
            int x = pick(p, Tag::RelicChoice, "Stellar Converter: destroy which planet?", opts,
                         "Every unit on it is destroyed and the planet is gone for the rest of the game.", false, true);
            if (x >= 0) {
                loseRelic(p, r, true);
                destroyPlanet(x, p);
                done = true;
            }
        }
    }
    curRelic_ = prev;
    return done;
}

void Rules::destroyPlanet(int x, int by, const char* how) {
    PlanetState& ps = G.planets[x];
    const int sys = ps.system;
    for (int q = 0; q < G.numPlayers; ++q)
        for (int t = 0; t < kUnitTypes; ++t)
            if (ps.units[q].n[t]) destroyUnits(q, UnitType(t), ps.units[q].n[t], sys, x, by, how);
    int old = ps.owner;
    ps.owner = -1;
    ps.attachments.clear();
    ps.destroyed = true;
    auto& list = G.systems[sys].planets;
    list.erase(std::remove(list.begin(), list.end(), int16_t(x)), list.end());
    say(by, planetName(x) + " is destroyed by " + how + ".", sys);
    if (old >= 0) checkEliminations();
}

bool Rules::enigmaticAction(int p) {
    Player& pl = G.players[p];
    if (pl.enigmatic <= 0 || spendable(G, p, false) < 6) return false;
    if (!researchStep(p, 6, "Enigmatic Device")) return false;
    pl.enigmatic = int8_t(pl.enigmatic - 1);  // purged
    say(p, pn(p) + " purges an Enigmatic Device.");
    purged(p);
    return true;
}

// Crown of Thalnos: reroll missed combat dice with +1; a unit whose reroll misses is destroyed.
void Rules::crownOfThalnos(int p, int sys, int planet, RollResult& r) {
    if (!holdsRelic(p, "thalnos") || r.dice.empty()) return;
    std::vector<Option> opts;
    for (size_t i = 0; i < r.dice.size(); ++i) {
        if (r.dice[i] >= r.need[i] || i >= r.unit.size() || r.unit[i] < 0) continue;
        opts.push_back({int(i), std::string(unitTypeName(UnitType(r.unit[i]))) + ": rolled " + std::to_string(r.dice[i]),
                        "needs " + std::to_string(r.need[i]) + "; rerolled with +1 it needs " + std::to_string(r.need[i] - 1)});
    }
    if (opts.empty()) return;
    const int prev = curRelic_;
    curRelic_ = findRelic("thalnos");
    std::vector<int> picks = pickMany(p, Tag::RelicChoice, "Crown of Thalnos: reroll which dice? (+1 to each)", opts, 0, int(opts.size()),
                                      "A unit whose reroll misses is destroyed.");
    curRelic_ = prev;
    if (picks.empty()) return;
    int lost[kUnitTypes] = {};
    for (int i : picks) {
        int v = E.rng().d10();
        bool hit = v + 1 >= r.need[i];
        r.dice[size_t(i)] = int8_t(std::min(10, v + 1));
        if (hit) ++r.hits;
        else ++lost[r.unit[size_t(i)]];
    }
    say(p, pn(p) + " rerolls " + std::to_string(picks.size()) + " dice with the Crown of Thalnos.", sys);
    for (int t = 0; t < kUnitTypes; ++t)
        if (lost[t]) destroyUnits(p, UnitType(t), lost[t], sys, planet, -1, "Crown of Thalnos");
}

bool Rules::scepter(int p) {
    if (!relicReady(p, "emelpar") || reinforcementTokens(p) <= 0) return false;
    const int prev = curRelic_;
    curRelic_ = findRelic("emelpar");
    bool use = yesNo(p, Tag::RelicChoice, "Exhaust the Scepter of Emelpar?", "Spend a token from your reinforcements instead of your strategy pool.");
    curRelic_ = prev;
    if (!use) return false;
    exhaustRelic(p, "emelpar");
    return true;
}

bool Rules::prophetsTears(int p, bool needPrereq) {
    if (!relicReady(p, "prophetstears")) return false;
    const int prev = curRelic_;
    curRelic_ = findRelic("prophetstears");
    bool use = yesNo(p, Tag::RelicChoice, needPrereq ? "Exhaust the Prophet's Tears to ignore 1 prerequisite?"
                                                     : "Exhaust the Prophet's Tears to draw 1 action card?",
                     "");
    curRelic_ = prev;
    if (!use) return false;
    exhaustRelic(p, "prophetstears");
    if (!needPrereq) drawActionCards(p, 1, "The Prophet's Tears");
    return true;
}

void Rules::mawOfWorlds() {
    for (int p : clockwiseFrom(G, G.speaker)) {
        if (!holdsRelic(p, "mawofworlds")) continue;
        std::vector<Option> opts;
        for (int t = 0; t < kTechCount; ++t)
            if (techAvailableToResearch(G, p, t) && (kTechs[t].source == Source::Base || kTechs[t].source == Source::PoK))
                opts.push_back({t, kTechs[t].name, techSummary(t)});
        if (opts.empty()) continue;
        opts.push_back({-1, "Keep the Maw of Worlds", ""});
        const int prev = curRelic_;
        curRelic_ = findRelic("mawofworlds");
        int t = pick(p, Tag::RelicChoice, "Maw of Worlds: purge it and exhaust all your planets to gain any technology?", opts);
        curRelic_ = prev;
        if (t < 0) continue;
        loseRelic(p, findRelic("mawofworlds"), true);
        for (int x : controlledPlanets(G, p)) G.planets[x].exhausted = true;
        researchTech(p, t, G.fixes < 27);  // (fixes 27: "gain 1 technology" - not research, as Plagiarize)
        E.log(p, pn(p) + " gains " + kTechs[t].name + ".", EventKind::Research);
    }
}

// Status phase: relics ready; Crown of Emphidia + Tomb of Emphidia.
void Rules::statusRelics() {
    for (int p = 0; p < G.numPlayers; ++p) {
        Player& pl = G.players[p];
        if (pl.eliminated) continue;
        pl.exhaustedRelics.clear();
        for (PlanetState& ps : G.planets)
            if (ps.owner == p) ps.abilityExhausted = false;
        if (holdsRelic(p, "emphidia")) {
            bool tomb = false;
            for (size_t x = 0; x < G.planets.size(); ++x)
                if (G.planets[x].owner == p && hasAttachment(G, int(x), "toe")) tomb = true;
            if (tomb) {
                const int prev = curRelic_;
                curRelic_ = findRelic("emphidia");
                bool use = yesNo(p, Tag::RelicChoice, "Purge the Crown of Emphidia to gain 1 victory point?",
                                 "You control the Tomb of Emphidia.");
                curRelic_ = prev;
                if (use) {
                    loseRelic(p, findRelic("emphidia"), true);
                    gainVP(p, 1, "Crown of Emphidia");
                }
            }
        }
    }
}

// ------------------------------------------------------------------ tactical action hooks
void Rules::scanlink(int p, int sys) {
    if (!owns(p, "sdn")) return;
    std::vector<Option> opts;
    for (int16_t x : G.systems[sys].planets)
        if (!G.planets[x].units[p].empty() && traitDeck(kPlanets[G.planets[x].def].trait) >= 0)
            opts.push_back({x, planetName(x), traitName(kPlanets[G.planets[x].def].trait)});
    if (opts.empty()) return;
    opts.push_back({-1, "Don't explore", ""});
    int x = pick(p, Tag::ExploreOrder, "Scanlink Drone Network: explore a planet here?", opts, "", false, true);
    if (x >= 0) explorePlanet(p, x);
}

void Rules::afterTactical(int p, int sys) {
    if (G.players[p].eliminated) return;
    // Dark Energy Tap: explore the frontier token where your ships are.
    if (G.systems[sys].frontier && owns(p, "det") && hasShips(G, p, sys)) exploreFrontier(p, sys);
    // Crown of Emphidia: explore one of your planets.
    if (relicReady(p, "emphidia")) {
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p))
            if (traitDeck(kPlanets[G.planets[x].def].trait) >= 0) opts.push_back({x, planetName(x), traitName(kPlanets[G.planets[x].def].trait)});
        if (!opts.empty()) {
            opts.push_back({-1, "Don't use it", ""});
            const int prev = curRelic_;
            curRelic_ = findRelic("emphidia");
            int x = pick(p, Tag::RelicChoice, "Exhaust the Crown of Emphidia to explore one of your planets?", opts, "", false, true);
            curRelic_ = prev;
            if (x >= 0) {
                exhaustRelic(p, "emphidia");
                explorePlanet(p, x);
            }
        }
    }
}

// ------------------------------------------------------------------ legendary planets
void Rules::legendaryAbilities(int p) {
    for (int x : controlledPlanets(G, p)) {
        PlanetState& ps = G.planets[x];
        if (ps.abilityExhausted || ps.destroyed || !*legendaryName(ps.def)) continue;
        const char* k = kPlanets[ps.def].key;
        auto is = [&](const char* s) { return !std::strcmp(k, s); };
        std::vector<Option> opts;
        if (is("hopesend")) {
            if (unitsAvailable(G, p, UnitType::Mech) > 0) opts.push_back({1, "Place 1 mech on a planet you control", ""});
            opts.push_back({2, "Draw 1 action card", ""});
        } else if (is("primor")) {
            if (unitsAvailable(G, p, UnitType::Infantry) > 0) opts.push_back({1, "Place up to 2 infantry on planets you control", ""});
        } else if (is("mirage")) {
            bool ships = false;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (hasShips(G, p, int(s))) ships = true;
            if (ships) opts.push_back({1, "Place up to 2 fighters with your ships", ""});
        } else if (is("mallice")) {
            opts.push_back({1, "Gain 2 trade goods", ""});
            if (G.players[p].commodities > 0) opts.push_back({2, "Convert all your commodities to trade goods", ""});
        } else if (is("silence")) {  // Uncharted Space (0.33.0)
            bool ships = false;
            for (size_t s = 0; s < G.systems.size() && !ships; ++s) ships = hasShips(G, p, int(s));
            if (ships && unitsAvailable(G, p, UnitType::Cruiser) > 0) opts.push_back({1, "Place 1 cruiser with your ships", ""});
        } else if (is("echo")) {
            if (!echoSystems(p).empty()) opts.push_back({1, "Put a frontier token in a system without planets", ""});
        } else if (is("prism")) {
            if (!prismPurgeable(p).empty()) opts.push_back({1, "Swap a technology", ""});
        } else if (is("domna")) {
            if (!domnaShips(p).empty()) opts.push_back({1, "Move 1 of your ships to an adjacent system", ""});
        }
        if (opts.empty()) continue;
        opts.push_back({0, "Not now", ""});
        int c = pick(p, Tag::LegendaryAbility, std::string(legendaryName(ps.def)) + " (" + planetName(x) + ")", opts, legendaryText(ps.def));
        if (c == 0) continue;
        ps.abilityExhausted = true;
        std::vector<Option> mine;
        for (int y : controlledPlanets(G, p))
            if (!isDMZ(G, y)) mine.push_back({y, planetName(y), ""});
        if (is("hopesend")) {
            if (c == 2) {
                drawActionCards(p, 1, legendaryName(ps.def));
            } else if (!mine.empty()) {
                int y = pick(p, Tag::LegendaryAbility, "Place the mech on which planet?", mine, "", false, true);
                addUnits(p, UnitType::Mech, 1, G.planets[y].system, y);
                say(p, pn(p) + " places a mech on " + planetName(y) + " (" + legendaryName(ps.def) + ").", G.planets[y].system);
            }
        } else if (is("primor")) {
            for (int i = 0; i < 2 && !mine.empty(); ++i) {
                std::vector<Option> o = mine;
                if (i == 1) o.push_back({-1, "No more", ""});
                int y = pick(p, Tag::LegendaryAbility, "The Atrament: place an infantry on which planet?", o, "", false, true);
                if (y < 0) break;
                addUnits(p, UnitType::Infantry, 1, G.planets[y].system, y);
                say(p, pn(p) + " places an infantry on " + planetName(y) + " (" + legendaryName(ps.def) + ").", G.planets[y].system);
            }
        } else if (is("mirage")) {
            std::vector<Option> o;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (hasShips(G, p, int(s))) o.push_back({int(s), sysName(int(s)), ""});
            int s = pick(p, Tag::LegendaryAbility, "Mirage Flight Academy: place up to 2 fighters where?", o, "", true);
            addUnits(p, UnitType::Fighter, 2, s, -1);
            removeExcessCapacity(p, s);
            say(p, pn(p) + " places 2 fighters in " + sysName(s) + " (" + legendaryName(ps.def) + ").", s);
        } else if (is("silence")) {
            std::vector<Option> o;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (hasShips(G, p, int(s))) o.push_back({int(s), sysName(int(s)), ""});
            int s = pick(p, Tag::LegendaryAbility, "Imperial Salvage Yard: place the cruiser where?", o, "", true);
            addUnits(p, UnitType::Cruiser, 1, s, -1);
            say(p, pn(p) + " places a cruiser in " + sysName(s) + " (" + legendaryName(ps.def) + ").", s);
            enforceFleetLimit(p);
        } else if (is("echo")) {
            std::vector<Option> o;
            for (int s : echoSystems(p)) o.push_back({s, sysName(s), ""});
            int s = pick(p, Tag::LegendaryAbility, "Bi-Phasic Generators: put the frontier token where?", o, "", true);
            G.systems[size_t(s)].frontier = true;
            say(p, pn(p) + " puts a frontier token in " + sysName(s) + " (" + legendaryName(ps.def) + ").", s);
        } else if (is("prism")) {
            std::vector<Option> o;
            for (int t : prismPurgeable(p)) o.push_back({t, kTechs[t].name, ""});
            o.push_back({-1, "Cancel", ""});
            const int from = pick(p, Tag::LegendaryAbility, "Mystic Library: purge which technology?", o,
                                  std::string(legendaryText(ps.def)) + " (A technology that isn't a unit upgrade or a faction one becomes another "
                                                          "with as many prerequisites.)");
            std::vector<Option> to;
            if (from >= 0)
                for (int t : prismTargets(p, from)) to.push_back({t, kTechs[t].name, ""});
            if (from < 0 || to.empty()) {
                ps.abilityExhausted = false;  // backed out: nothing spent
                continue;
            }
            const int gain = pick(p, Tag::LegendaryAbility, "Mystic Library: gain which technology?", to, legendaryText(ps.def));
            Player& pl = G.players[p];
            pl.techs.erase(std::remove(pl.techs.begin(), pl.techs.end(), int16_t(from)), pl.techs.end());
            pl.exhaustedTechs.erase(std::remove(pl.exhaustedTechs.begin(), pl.exhaustedTechs.end(), int16_t(from)), pl.exhaustedTechs.end());
            researchTech(p, gain, false);
            say(p, pn(p) + " purges " + kTechs[from].name + " and gains " + kTechs[gain].name + " (" + legendaryName(ps.def) + ").");
            purged(p);
        } else if (is("domna")) {
            std::vector<Option> o = domnaShips(p);
            o.push_back({-1, "Cancel", ""});
            const int id = pick(p, Tag::LegendaryAbility, "Fractal Gate: move which ship?", o, legendaryText(ps.def));
            if (id < 0) {
                ps.abilityExhausted = false;
                continue;
            }
            const int from = id / 16;
            const UnitType t = UnitType(id % 16);
            std::vector<Option> to;
            for (int s : domnaDestinations(p, from)) to.push_back({s, sysName(s), ""});
            const int dest = pick(p, Tag::LegendaryAbility, "Fractal Gate: to which system?", to, "", true);
            Force& src = G.systems[size_t(from)].space[p];
            const bool damaged = src.dmg[int(t)] >= src.n[int(t)];
            src.remove(t, 1, false);
            G.systems[size_t(dest)].space[p].add(t, 1);
            if (damaged) G.systems[size_t(dest)].space[p].dmg[int(t)] = uint8_t(G.systems[size_t(dest)].space[p].dmg[int(t)] + 1);
            say(p, pn(p) + " moves a " + unitTypeName(t) + " from " + sysName(from) + " to " + sysName(dest) + " (" + legendaryName(ps.def) + ").", dest);
            removeExcessCapacity(p, from);
            removeExcessCapacity(p, dest);
            checkNexus(dest);
        } else if (is("mallice")) {
            Player& pl = G.players[p];
            if (c == 1) {
                gainTG(p, 2);
                say(p, pn(p) + " gains 2 trade goods (" + legendaryName(ps.def) + ").");
            } else {
                const int conv = pl.commodities;
                pl.commodities = 0;
                say(p, pn(p) + " converts " + std::to_string(conv) + " commodities to trade goods (" + legendaryName(ps.def) + ").");
                gainTG(p, conv);
            }
        }
    }
}

// ---- Uncharted Space legendary planets (0.33.0)
// Bi-Phasic Generators: systems without planets (not hyperlanes) that have no frontier token yet.
std::vector<int> Rules::echoSystems(int p) const {
    (void)p;
    std::vector<int> out;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const SystemState& ss = G.systems[s];
        if (ss.isHyperlane() || !ss.planets.empty() || ss.frontier || ss.home >= 0) continue;
        if (ss.d().anomalies & kSupernova) continue;  // nobody could ever explore it
        if (int(s) == G.nexus) continue;
        out.push_back(int(s));
    }
    return out;
}
static int prereqCount(int t) {
    int n = 0;
    for (int c = 0; c < 4; ++c) n += kTechs[t].req[c];
    return n;
}
// Mystic Library: our technologies that can go (not faction, not unit upgrades) and have something to become.
std::vector<int> Rules::prismPurgeable(int p) const {
    std::vector<int> out;
    for (int16_t t : G.players[p].techs)
        if (kTechs[t].faction < 0 && !kTechs[t].unitUpgrade && kTechs[t].color != TechColor::None && !prismTargets(p, t).empty())
            out.push_back(t);
    return out;
}
// ... and what each can become: generic technologies we don't own with as many prerequisites.
std::vector<int> Rules::prismTargets(int p, int from) const {
    std::vector<int> out;
    const int need = prereqCount(from);
    for (int t = 0; t < kTechCount; ++t) {
        if (t == from || G.players[p].hasTech(t) || kTechs[t].faction >= 0 || kTechs[t].color == TechColor::None) continue;
        if (!techImplemented(t) || prereqCount(t) != need) continue;
        out.push_back(t);
    }
    return out;
}
// Fractal Gate: our ships (system * 16 + type) that have somewhere to go.
std::vector<Option> Rules::domnaShips(int p) const {
    std::vector<Option> out;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const Force& f = G.systems[s].space[p];
        if (f.ships() <= 0 || domnaDestinations(p, int(s)).empty()) continue;
        for (int t : kShipIdx)
            if (f.n[t] > 0) out.push_back({int(s) * 16 + t, std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)), ""});
    }
    return out;
}
std::vector<int> Rules::domnaDestinations(int p, int from) const {
    std::vector<int> out;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (int(s) == from || G.systems[s].isHyperlane() ||
            !(adjacentSystems(G, from, int(s)) || (G.fixes >= 24 && qeLinked(G, p, from, int(s)))))  // (Quantum Entanglement)
            continue;
        if (otherShipsIn(G, p, int(s))) continue;
        if (G.fixes >= 12 ? !canEndMoveIn(G, p, int(s)) : (G.systems[s].d().anomalies & kSupernova) != 0) continue;  // (0.44.8: Magmus)
        out.push_back(int(s));
    }
    return out;
}
// Council Black Site (Tarrock): after an agenda is revealed, predict its outcome; right = a secret objective.
void Rules::tarrockPredict(int agenda) {
    tarrockPicks_.clear();
    for (int p : windowOrder()) {
        for (int x : controlledPlanets(G, p)) {
            PlanetState& ps = G.planets[x];
            if (ps.abilityExhausted || ps.destroyed || std::strcmp(kPlanets[ps.def].key, "tarrock") != 0) continue;
            if (!yesNo(p, Tag::LegendaryAbility, "Council Black Site (Tarrock): predict the outcome of this agenda?", legendaryText(ps.def)))
                continue;
            std::vector<Option> outs = agendaOutcomes(agenda);
            if (outs.empty()) continue;
            const int o = pick(p, Tag::LegendaryAbility, "Council Black Site: which outcome do you predict?", outs, legendaryText(ps.def));
            ps.abilityExhausted = true;
            std::string name;
            for (const Option& op : outs)
                if (op.id == o) name = op.label;
            say(p, pn(p) + " predicts \"" + name + "\" (Council Black Site).");
            tarrockPicks_.push_back({p, o});
        }
    }
}
void Rules::tarrockCheck(int outcome) {
    for (const auto& pr : tarrockPicks_) {
        if (pr.second != outcome) continue;
        say(pr.first, pn(pr.first) + " predicted it right (Council Black Site).");
        drawSecret(pr.first);
    }
    tarrockPicks_.clear();
}

// Psychoarchaeology: during the action phase, exhaust specialty planets for 1 trade good each.
void Rules::psychoarchaeology(int p) {
    std::vector<Option> opts;
    for (int x : ti::readyPlanets(G, p))
        if (planetHasSkip(G, x)) opts.push_back({x, planetName(x), "1 trade good"});
    if (opts.empty()) return;
    std::vector<int> picks = pickMany(p, Tag::ComponentChoice, "Psychoarchaeology: exhaust which planets for 1 trade good each?", opts, 0,
                                      int(opts.size()), "", true);
    for (int x : picks) G.planets[x].exhausted = true;
    planetsExhausted(p, picks);  // Arcane Citadel
    gainTG(p, int(picks.size()));
    if (!picks.empty()) say(p, pn(p) + " gains " + std::to_string(picks.size()) + " trade goods (Psychoarchaeology).");
}

}  // namespace ti
