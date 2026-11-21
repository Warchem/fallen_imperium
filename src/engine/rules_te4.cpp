// Thunder's Edge, part 3 (0.34.0): breakthroughs (gained with the expedition's first slice, Thunder's Edge,
// Brilliance...) and the roll that may bring The Fracture into play; the expedition and the Thunder's Edge
// planet; The Fracture itself (its seven systems at the board's edge with their neutral guards, reached only
// through ingress tokens) and the relic icons on its planets. Rules from the owner's Thunder's Edge rulebook
// (docs/owner/te), in our own words. The breakthroughs' faction abilities are in rules_breakthroughs.cpp.
#include <algorithm>
#include <cstring>

#include "engine/breakthroughs.h"
#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
void teAbilityEvent(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    e.system = int16_t(sys);
    E.emit(e);
}
}  // namespace

namespace {
int planetDef(const char* key) {
    for (int i = 0; i < kPlanetCount; ++i)
        if (!std::strcmp(kPlanets[i].key, key)) return i;
    return -1;
}
int systemDef(const char* key) {
    for (int i = 0; i < kSystemCount; ++i)
        if (!std::strcmp(kSystems[i].key, key)) return i;
    return -1;
}
const char* kSliceName[6] = {"Spend 5 resources", "Spend 5 influence", "Discard 2 action cards", "Discard 1 unscored secret objective",
                             "Exhaust 1 planet with a technology specialty", "Spend 3 trade goods"};
}  // namespace

// ------------------------------------------------------------------ where the Fracture and Thunder's Edge go
// Fixed when the galaxy is built: the Fracture's seven systems bend round a corner of the board two to five
// spaces out (the shape of the rulebook's diagram), Thunder's Edge one space off the board, both clear of the
// homes and the Wormhole Nexus. The screens draw them there too.
void tePlacement(Game& g) {
    int maxRing = 0;
    std::vector<Hex> board, homes;
    for (const SystemState& s : g.systems) {
        if (!s.offBoard) {
            board.push_back(s.pos);
            maxRing = std::max(maxRing, hexRingOf(s.pos));
        }
        if (s.home >= 0) homes.push_back(s.pos);
    }
    Hex nexus{9999, 9999};
    if (g.nexus >= 0) nexus = g.systems[size_t(g.nexus)].pos;
    auto minDist = [&](Hex h, const std::vector<Hex>& v) {
        int d = 99;
        for (const Hex& x : v) d = std::min(d, hexDistance(h, x));
        return d;
    };
    auto pattern = [](int corner, int ring) {
        const Hex apex{hexDir(corner).q * ring, hexDir(corner).r * ring};
        std::vector<Hex> v(7);
        v[3] = apex;
        Hex a = apex, b = apex;
        for (int k = 1; k <= 3; ++k) {
            a = hexNeighbor(a, corner + 4);
            b = hexNeighbor(b, corner + 2);
            v[size_t(3 - k)] = a;
            v[size_t(3 + k)] = b;
        }
        return v;
    };
    std::vector<Hex> frac;
    int bestScore = -(1 << 30);
    // (homes keep 4 spaces clear when it can: the screens put each faction's three info tiles beside its home)
    for (int homeGap : {4, 2})
    for (int ring = maxRing + 2; ring <= maxRing + 6 && frac.empty(); ++ring)
        for (int corner = 0; corner < 6; ++corner) {
            const std::vector<Hex> pt = pattern(corner, ring);
            bool ok = true;
            int homeD = 99, nexD = 99;
            for (const Hex& h : pt) {
                if (minDist(h, board) < 2) ok = false;
                for (const SystemState& s : g.systems)
                    if (s.pos == h) ok = false;
                homeD = std::min(homeD, minDist(h, homes));
                nexD = std::min(nexD, hexDistance(h, nexus));
            }
            if (!ok || homeD < homeGap || nexD < 2) continue;
            const int sc = -(ring - maxRing) * 100 + std::min(homeD, 6) * 10 + std::min(nexD, 6) * 3 + (corner == 0 ? 2 : 0);
            if (sc > bestScore) {
                bestScore = sc;
                frac = pt;
            }
        }
    // the chain's seven places from one end (fractureEnter puts fracture1..7 on them, the planets at the ends)
    for (size_t i = 0; i < 7 && i < frac.size(); ++i) g.fracturePos[i] = frac[i];
    Hex best{};
    int bestTe = -(1 << 30);
    for (int ring = maxRing + 1; ring <= maxRing + 3; ++ring)
        for (Hex h : hexRing(ring)) {
            if (minDist(h, board) < 2 || hexDistance(h, nexus) < 2) continue;
            bool clash = false;
            for (const Hex& f : frac)
                if (hexDistance(h, f) < 2) clash = true;
            if (clash) continue;
            const int sc = std::min(minDist(h, homes), 4) * 20 + std::min(hexDistance(h, nexus), 6) * 5 - ring * 30 -
                           std::abs(minDist(h, board) - 2) * 200 - (minDist(h, homes) < 4 ? 300 : 0);  // (clear of the info tiles)
            if (sc > bestTe) {
                bestTe = sc;
                best = h;
            }
        }
    g.tePos = best;
    // the Sorrow (the Crimson Rebellion's place on the board) starts with an inactive breach
    for (SystemState& s : g.systems)
        if (!std::strcmp(s.d().key, "94") && !s.breach) s.breach = 1;
}

// ------------------------------------------------------------------ breakthroughs
void Rules::gainBreakthrough(int p, const std::string& why) {
    if (!breakthroughsOn(G) || p < 0 || p >= G.numPlayers) return;
    Player& pl = G.players[size_t(p)];
    if (pl.breakthrough || (pl.btPurged && G.fixes >= 8)) return;
    pl.breakthrough = true;
    pl.btExhausted = false;
    const BreakthroughDef* bt = breakthroughFor(G, p);
    teAbilityEvent(E, p, bt ? bt->name : "Breakthrough",
                 pn(p) + " gains their breakthrough" + (bt ? std::string(", ") + bt->name : std::string()) + (why.empty() ? "" : " (" + why + ")") +
                     ".");
    btGained(p);  // "when you gain this card" (rules_breakthroughs.cpp)
    // the Fracture may stir (a 1 or a 10); the Vuil'raith's brings it at once (btGained)
    if (!G.fractureIn) {
        RollResult r;
        const int v = E.rng().d10();
        r.dice.push_back(int8_t(v));
        r.need.push_back(10);
        r.hits = (v == 1 || v == 10) ? 1 : 0;
        reportRoll(p, -1, "Breakthrough: the Fracture stirs on a 1 or a 10", r);
        if (v == 1 || v == 10) fractureEnter(p, true);
    }
}

// ------------------------------------------------------------------ the expedition
std::vector<int> Rules::expeditionSlices(int p) const {
    std::vector<int> v;
    if (!(G.modules & kModThundersEdge) || G.teSystem >= 0) return v;
    const Player& pl = G.players[size_t(p)];
    for (int s = 0; s < 6; ++s) {
        if (G.expedition[size_t(s)] >= 0) continue;
        bool can = false;
        switch (s) {
            case 0: can = spendable(G, p, false) >= 5; break;
            case 1: can = spendable(G, p, true) >= 5; break;
            case 2: can = pl.actionCards.size() >= 2; break;
            case 3: can = !pl.secrets.empty(); break;
            case 4:
                for (int x : ti::readyPlanets(G, p))
                    if (planetHasSkip(G, x)) can = true;
                break;
            case 5: can = pl.tradeGoods >= 3; break;
        }
        if (can) v.push_back(s);
    }
    return v;
}

void Rules::teExpedition(int p) {
    const std::vector<int> slices = expeditionSlices(p);
    if (slices.empty()) return;
    Player& pl = G.players[size_t(p)];
    std::vector<Option> opts;
    for (int s : slices) opts.push_back({s, kSliceName[s], ""});
    opts.push_back({-1, "Not now", ""});
    int claimed = 0;
    for (int8_t o : G.expedition) claimed += o >= 0 ? 1 : 0;
    const int s = fpick(p, Ab::Expedition, "Commit to the Thunder's Edge expedition?", opts,
                        std::to_string(claimed) + " of 6 slices claimed. " +
                            (pl.breakthrough ? "" : "Your first slice gains your breakthrough. ") +
                            "The last one places Thunder's Edge on the board.");
    if (s < 0) return;
    bool paid = true;
    switch (s) {
        case 0: paid = pay(p, 5, false, "the expedition", true); break;
        case 1: paid = pay(p, 5, true, "the expedition", true); break;
        case 2: {
            std::vector<Option> o2;
            for (size_t i = 0; i < pl.actionCards.size(); ++i)
                o2.push_back({int(i), kActionCards[pl.actionCards[i]].name, actionCardText(pl.actionCards[i])});
            std::vector<int> picks = fpickMany(p, Ab::ExpeditionPay, "Expedition: discard which 2 action cards?", o2, 2, 2);
            std::sort(picks.rbegin(), picks.rend());
            for (int i : picks) {
                const int16_t c = pl.actionCards[size_t(i)];
                pl.actionCards.erase(pl.actionCards.begin() + i);
                discardActionCard(p, c, false);
            }
            break;
        }
        case 3: {
            std::vector<Option> o2;
            for (int16_t o : pl.secrets) o2.push_back({o, kObjectives[o].name, objectiveSummary(o)});
            const int o = fpick(p, Ab::ExpeditionPay, "Expedition: discard which secret objective?", o2);
            pl.secrets.erase(std::find(pl.secrets.begin(), pl.secrets.end(), int16_t(o)));
            G.secretDeck.insert(G.secretDeck.begin(), int16_t(o));  // (back to the bottom of the deck)
            say(p, pn(p) + " discards a secret objective for the expedition.");
            break;
        }
        case 4: {
            std::vector<Option> o2;
            for (int x : ti::readyPlanets(G, p))
                if (planetHasSkip(G, x)) o2.push_back({x, planetName(x), ""});
            const int x = fpick(p, Ab::ExpeditionPay, "Expedition: exhaust which planet?", o2, "", false, true);
            G.planets[size_t(x)].exhausted = true;
            planetsExhausted(p, {x});
            break;
        }
        case 5:
            pl.tradeGoods = int8_t(pl.tradeGoods - 3);
            break;
    }
    if (!paid) return;
    G.expedition[size_t(s)] = int8_t(p);
    int mine = 0, all = 0;
    for (int8_t o : G.expedition) {
        mine += o == p ? 1 : 0;
        all += o >= 0 ? 1 : 0;
    }
    teAbilityEvent(E, p, "The expedition", pn(p) + " joins the expedition to Thunder's Edge: " + kSliceName[s] + " (" + std::to_string(all) + " of 6).");
    if (mine == 1) gainBreakthrough(p, "the expedition");
    if (all == 6) completeExpedition(p);
}

std::vector<int> Rules::teSpots() const {
    std::vector<int> v;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const SystemState& sy = G.systems[s];
        if (sy.isHyperlane() || sy.offBoard || sy.fracture || !sy.planets.empty()) continue;
        if ((sy.d().anomalies & kSupernova) || sy.d().wormholes) continue;
        v.push_back(int(s));
    }
    return v;
}

void Rules::completeExpedition(int last) {
    const std::vector<int> spots = teSpots();
    const int def = planetDef("thundersedge");
    if (spots.empty() || def < 0) return;
    std::vector<Option> opts;
    for (int s : spots) opts.push_back({s, sysName(s), ""});
    const int s = fpick(last, Ab::TePlace, "The expedition is complete: place Thunder's Edge in which system?", opts,
                        "A system without planets, supernovas or printed wormholes.", true);
    PlanetState ps;
    ps.def = int16_t(def);
    ps.system = int16_t(s);
    G.systems[size_t(s)].planets.push_back(int16_t(G.planets.size()));
    G.planets.push_back(ps);
    G.systems[size_t(s)].frontier = false;
    G.teSystem = int16_t(s);
    const int planet = int(G.planets.size()) - 1;
    teAbilityEvent(E, last, "Thunder's Edge", "Thunder's Edge is revealed in " + sysName(s) + ".");
    // the player with the most slices places that many infantry on it (ties: the last one to claim chooses)
    int counts[kMaxPlayers] = {}, most = 0;
    for (int8_t o : G.expedition)
        if (o >= 0) most = std::max(most, ++counts[size_t(o)]);
    std::vector<Option> tied;
    for (int q = 0; q < G.numPlayers; ++q)
        if (counts[q] == most && most > 0) tied.push_back({q, pn(q), std::to_string(most) + " slices"});
    int who = tied.empty() ? -1 : tied[0].id;
    if (tied.size() > 1) who = fpick(last, Ab::TeInfantry, "Which tied player places infantry on Thunder's Edge?", tied);
    if (who >= 0) {
        const int k = std::min(most, unitsAvailable(G, who, UnitType::Infantry));
        bool others = false;
        for (int q = 0; q < kSides; ++q)
            if (q != who && !G.planets[size_t(planet)].units[size_t(q)].empty()) others = true;
        if (k > 0 && !others) {
            addUnits(who, UnitType::Infantry, k, s, planet);
            say(who, pn(who) + " places " + std::to_string(k) + " infantry on Thunder's Edge.", s);
            setControl(planet, who);
        }
    }
    if (G.fractureIn) computeAdjacency(G);
}

// ------------------------------------------------------------------ the Fracture
std::vector<int> Rules::ingressCandidates(TechColor c) const {
    std::vector<int> v;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const SystemState& sy = G.systems[s];
        if (sy.fracture || sy.ingress) continue;
        bool match = false;
        for (int16_t x : sy.planets)
            if (!G.planets[size_t(x)].destroyed && (c == TechColor::None ? planetHasSkip(G, x) : ((planetSkips(G, x) >> int(c)) & 1)))
                match = true;
        if (match) v.push_back(int(s));
    }
    return v;
}

void Rules::fractureEnter(int p, bool byBreakthrough) {
    if (G.fractureIn || !(G.modules & kModThundersEdge)) return;
    // its place (fixed when the galaxy was built; again if new systems have taken it since)
    bool free = !(G.fracturePos[3] == Hex{});
    for (const Hex& h : G.fracturePos)
        for (const SystemState& s : G.systems)
            if (s.pos == h) free = false;
    if (!free) tePlacement(G);
    G.fractureIn = true;
    std::vector<int> sys(7, -1);
    // the chain from one end - fracture1..7 are Cocytus, egress, void, Styx, void, egress, Lethe / Phlegethon:
    // 0.43.2 (owner, fixes >= 3): in that order, the planets at the ends; before, the egress points were at the
    // ends (egress, Cocytus, void, Styx, void, Lethe / Phlegethon, egress - owner 0.28.0, taken back)
    static const int kAtOld[7] = {1, 0, 2, 3, 4, 6, 5}, kAtNew[7] = {0, 1, 2, 3, 4, 5, 6};
    const int* kAt = G.fixes >= 3 ? kAtNew : kAtOld;
    for (int i = 0; i < 7; ++i) {
        const int def = systemDef(("fracture" + std::to_string(i + 1)).c_str());
        if (def < 0) continue;
        sys[size_t(i)] = addBoardSystem(G, def, G.fracturePos[size_t(kAt[i])]);
        SystemState& s = G.systems[size_t(sys[size_t(i)])];
        s.fracture = true;
        s.egress = i == 1 || i == 5;
    }
    // (0.73.5: the event points at Styx - the ripple's epicentre on the game screen; display only, the log keeps the text)
    teAbilityEvent(E, p, "The Fracture", "The Fracture tears open at the edge of the galaxy.", sys[3]);
    // the neutral guards (the tiles' backs)
    auto onPlanet = [&](int s, const char* key) {
        for (int16_t x : G.systems[size_t(s)].planets)
            if (!std::strcmp(kPlanets[G.planets[size_t(x)].def].key, key)) return int(x);
        return -1;
    };
    if (sys[0] >= 0) {  // Cocytus
        placeNeutral(UnitType::Cruiser, 2, sys[0], -1, "");
        placeNeutral(UnitType::Infantry, 2, sys[0], onPlanet(sys[0], "cocytus"), "");
    }
    if (sys[3] >= 0) {  // Styx
        placeNeutral(UnitType::Dreadnought, 2, sys[3], -1, "");
        placeNeutral(UnitType::Destroyer, 1, sys[3], -1, "");
        placeNeutral(UnitType::Infantry, 3, sys[3], onPlanet(sys[3], "styx"), "");
    }
    if (sys[6] >= 0) {  // Lethe / Phlegethon
        placeNeutral(UnitType::Carrier, 1, sys[6], -1, "");
        placeNeutral(UnitType::Fighter, 4, sys[6], -1, "");
        placeNeutral(UnitType::Infantry, 1, sys[6], onPlanet(sys[6], "lethe"), "");
        placeNeutral(UnitType::Infantry, 1, sys[6], onPlanet(sys[6], "phlegethon"), "");
    }
    // ingress tokens: per synergy colour of the breakthrough, 3 systems with a matching specialty (6 at most);
    // otherwise 4 planets of different specialties in different systems
    auto placeIngress = [&](int s) {
        G.systems[size_t(s)].ingress = true;
        say(p, "An ingress token is placed in " + sysName(s) + ".", s);
    };
    TechColor a = TechColor::None, b = TechColor::None;
    const bool syn = byBreakthrough && synergyOf(G, p, a, b);
    if (syn) {
        for (TechColor c : {a, b})
            for (int k = 0; k < 3; ++k) {
                std::vector<Option> opts;
                for (int s : ingressCandidates(c)) opts.push_back({s, sysName(s), ""});
                if (opts.empty()) break;
                const int s = opts.size() == 1 ? opts[0].id
                                               : fpick(p, Ab::IngressPlace, std::string("Place an ingress token in a system with a ") +
                                                                                 colorName(c) + " specialty (" + std::to_string(k + 1) + " of 3)",
                                                       opts, "", true);
                placeIngress(s);
            }
    } else {
        int usedColours = 0;
        for (int k = 0; k < 4; ++k) {
            std::vector<Option> opts;
            for (int s : ingressCandidates(TechColor::None)) {
                int fresh = 0;  // specialties here not used yet
                for (int16_t x : G.systems[size_t(s)].planets) fresh |= planetSkips(G, x) & ~usedColours;
                if (fresh) opts.push_back({s, sysName(s), ""});
            }
            if (opts.empty()) break;
            const int s = fpick(p, Ab::IngressPlace, "Place an ingress token (planets of four different specialties)", opts, "", true);
            int fresh = 0;
            for (int16_t x : G.systems[size_t(s)].planets) fresh |= planetSkips(G, x) & ~usedColours;
            usedColours |= fresh & -fresh;  // (one colour per system)
            placeIngress(s);
        }
    }
    // and one in Thunder's Edge's system, or on Mecatol Rex while the expedition goes on
    const int last = G.teSystem >= 0 ? G.teSystem : G.mecatol();
    if (last >= 0 && !G.systems[size_t(last)].ingress) placeIngress(last);
    computeAdjacency(G);
}

// LRR-style: a planet with a relic icon gives its first controller a relic (Thunder's Edge)
void Rules::relicIcon(int planet, int p) {
    PlanetState& ps = G.planets[size_t(planet)];
    if (!(kPlanets[ps.def].flags & kPlanetRelic) || ps.relicTaken || p < 0) return;
    // (0.45.0, fixes 14 - owner's bug: the relic icons are the Fracture's planets; the Thunder's Edge planet has none)
    if (G.fixes >= 14 && !std::strcmp(kPlanets[ps.def].key, "thundersedge")) return;
    ps.relicTaken = true;
    gainRelic(p, planetName(planet) + "'s relic icon");
}

}  // namespace ti
