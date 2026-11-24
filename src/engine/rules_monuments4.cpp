// Monuments+ (0.82.1) - Thunder's Edge's six monuments (cards in tools/gen_monuments.py, the owner's v3.1 card files).
//
//   Seraph Data Center (Last Bastion)  "This unit is a space station and placed in the space area of a system that contains
//       your ships and exactly 1 non-legendary planet. Take its matching space station card while you control it. / When you
//       lose control of this station, destroy it and produce 2 hits against ships in this system."
//       Its station card ("Data Center", 1 / 1 - Monuments+'s reminder card) is a real space station in that system
//       (Player::monStation, a PlanetState made the first time): Thunder's Edge's station rules do the rest - control by the
//       only player with ships (stationControl), +1 commodity value, the exhaust to turn commodities into trade goods, spent
//       like a planet card, no votes. Control passing to another player destroys the monument (monStationLost).
//   Veils of Lost Creuss (Crimson Rebellion)  "After a breach becomes or is placed active side up in any system, if that
//       system is adjacent to this planet, you may produce 1 hit in that system against another player's ships."
//       (placeBreach / flipBreach -> monBreachActive; the planet's own system counts - LRR 6.3, as for Revenance Circuit)
//   The Pelagion (Deepwrought)  "Other players' ships in this system do not trigger space combat. After this unit is removed
//       from a planet, choose a player with ships in that system to resolve a space combat. Repeat this until it contains
//       no more than 1 player's ships."
//       Only the Deepwrought's own activation starts a space combat there (pelagionBlocks); with no combat the active player
//       may still invade (LRR 49.2 needs only ground forces in the space area). Removed (destroyed, captured, taken back,
//       renovated...) - noticed by monSync, resolved at the next safe point (monCheckpoint): the Deepwrought picks a player,
//       that player picks whom they fight (Monuments+ FAQ: "That player may choose any other player in the system").
//   Epiphany (Firmament)  "When a player activates this system, place their control token on this card if it does not have
//       one. When you research a technology, you may remove a control token from this card to skip 1 prerequisite, or move a
//       non-Firmament player's control token to a plot card." (Player::epiphanyTok; researchStep asks epiphanySkip /
//       epiphanyPlot - the card's own text works as a card, the tokens come only while it stands)
//   Epiphany Hollow (Obsidian)  "After a puppeted player activates this system, replace up to 2 of that player's infantry on
//       a planet they control with your infantry; those units coexist." (the Epiphany turns into it with Puppets of the
//       Blade - refreshUnits; it can't be placed again - Monuments+ FAQ)
//   Sor Tek Linkhub (Ral Nel)  "At the end of your tactical action, you may move 1 structure in the space area of this or an
//       adjacent system to the space area of a system adjacent to that structure." (monTacticalEnd)

#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
bool listHas(const std::vector<int16_t>& v, int x) { return std::find(v.begin(), v.end(), int16_t(x)) != v.end(); }
int shipMask() {
    int m = 0;
    for (int t = 0; t < kUnitTypes; ++t)
        if (isShip(UnitType(t))) m |= 1 << t;
    return m;
}
std::vector<int> holders(const Game& g, const char* key) {
    std::vector<int> v;
    if (!(g.modules & kModMonuments)) return v;
    for (int q = 0; q < g.numPlayers; ++q)
        if (!g.players[size_t(q)].eliminated && monCardHas(g, q, key)) v.push_back(q);
    return v;
}
std::string shipsText(const Game& g, int q, int sys) {
    const int n = g.systems[size_t(sys)].space[size_t(q)].ships();
    return std::to_string(n) + (n == 1 ? " ship there" : " ships there");
}
}  // namespace

int epiphanyTokens(const Game& g, int p) {
    if (!(g.modules & kModMonuments) || p < 0 || p >= g.numPlayers || !monCardHas(g, p, "firmament")) return 0;
    int n = 0;
    for (int q = 0; q < kMaxPlayers; ++q) n += (g.players[size_t(p)].epiphanyTok >> q) & 1;
    return n;
}

// ------------------------------------------------------------------ Seraph Data Center (Last Bastion)
// Its station card goes into the system the monument stands in (made the first time - a planet card with no tile, like
// Thunder's Edge's own planet; later placements move it) and p takes control of it.
void Rules::monStationPlace(int p, int sys) {
    if (!monCardHas(G, p, "bastion") || sys < 0 || sys >= int(G.systems.size())) return;
    Player& pl = G.players[size_t(p)];
    int x = pl.monStation;
    if (x < 0) {
        const int def = findPlanet("seraphdatacenter");
        if (def < 0) return;
        PlanetState ps;
        ps.def = int16_t(def);
        ps.system = int16_t(sys);
        x = int(G.planets.size());
        G.planets.push_back(ps);
        pl.monStation = int16_t(x);
    } else {
        auto& old = G.systems[size_t(G.planets[size_t(x)].system)].planets;
        old.erase(std::remove(old.begin(), old.end(), int16_t(x)), old.end());
        PlanetState& ps = G.planets[size_t(x)];
        ps.system = int16_t(sys);
        ps.destroyed = false;
        ps.owner = -1;
        ps.exhausted = false;
        ps.attachments.clear();
    }
    G.systems[size_t(sys)].planets.push_back(int16_t(x));
    setControl(x, p);
}

// its station card leaves play (the monument is gone)
void Rules::monStationRemove(int p) {
    const int x = G.players[size_t(p)].monStation;
    if (x < 0 || G.planets[size_t(x)].destroyed) return;
    PlanetState& ps = G.planets[size_t(x)];
    auto& v = G.systems[size_t(ps.system)].planets;
    v.erase(std::remove(v.begin(), v.end(), int16_t(x)), v.end());
    ps.owner = -1;
    ps.destroyed = true;
    ps.exhausted = false;
    ps.attachments.clear();
}

bool isMonumentStation(const Game& g, int planet, int* owner) {
    if (owner) *owner = -1;
    if (!(g.modules & kModMonuments) || planet < 0) return false;
    for (int q = 0; q < g.numPlayers; ++q)
        if (g.players[size_t(q)].monStation == planet) {
            if (owner) *owner = q;
            return true;
        }
    return false;
}

// "When you lose control of this station, destroy it and produce 2 hits against ships in this system." (setControl, after the
// control has passed; old = the monument's owner, by = who took it)
void Rules::monStationLost(int planet, int old, int by) {
    const int card = monumentIndex("bastion");
    const int sys = G.planets[size_t(planet)].system;
    int mpl = -1;
    const int msys = monumentSystem(G, old, &mpl);
    monEvent(old, card, pn(old) + " loses control of " + monName(card) + "'s station: it is destroyed, with 2 hits against the ships in " +
                            sysName(sys) + ".",
             sys);
    if (msys >= 0 && mpl < 0) destroyUnits(old, UnitType::Monument, 1, msys, -1, -1, "lost its station");
    monStationRemove(old);
    G.players[size_t(old)].monAt = 0;
    std::vector<Option> opts;
    if (by >= 0 && by < G.numPlayers && hasShips(G, by, sys)) {
        opts.push_back({by, pn(by), shipsText(G, by, sys)});
    } else {
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != old && !G.players[size_t(q)].eliminated && hasShips(G, q, sys)) opts.push_back({q, pn(q), shipsText(G, q, sys)});
        if (hasShips(G, kNeutral, sys)) opts.push_back({kNeutral, "Neutral ships", ""});
    }
    if (opts.empty()) return;
    curFor_ = by;
    const int t = opts.size() == 1 ? opts[0].id
                                   : mpick(old, card, kMonAskBastion, monName(card) + ": whose ships take the 2 hits?", opts, kMonuments[card].text);
    curFor_ = -1;
    assignHits(t, sys, -1, 2, shipMask(), monName(card), old);
}

// ------------------------------------------------------------------ keeping track
// Before every question and at the checkpoints: a Seraph Data Center's station card follows its monument (moved by the Sor Tek
// Linkhub) or leaves play with it; a Pelagion that has left its planet is noted for its space combats.
void Rules::monSync() {
    if (!monumentsOn()) return;
    for (int p = 0; p < G.numPlayers; ++p) {
        Player& pl = G.players[size_t(p)];
        int planet = -1;
        const int sys = monumentSystem(G, p, &planet);
        const int at = sys < 0 ? 0 : planet >= 0 ? planet + 1 : -(sys + 1);
        if (pl.monAt > 0 && at != pl.monAt && monCardHas(G, p, "deepwrought")) {
            const int was = G.planets[size_t(pl.monAt - 1)].system;
            pelagionPending_.push_back({was, p});
        }
        pl.monAt = at;
        const int x = pl.monStation;
        if (x < 0 || G.planets[size_t(x)].destroyed) continue;
        if (at < 0 && monCardHas(G, p, "bastion")) {
            if (G.planets[size_t(x)].system != sys) {  // (moved with it: still theirs)
                auto& old = G.systems[size_t(G.planets[size_t(x)].system)].planets;
                old.erase(std::remove(old.begin(), old.end(), int16_t(x)), old.end());
                G.planets[size_t(x)].system = int16_t(sys);
                G.systems[size_t(sys)].planets.push_back(int16_t(x));
            }
        } else {
            monStationRemove(p);
        }
    }
}

void Rules::monCheckpoint() {
    if (!monumentsOn()) return;
    if (!queenPending_.empty()) queenResolve();  // (0.82.2) Queen's Rest rolls left from units destroyed outside hit assignment
    if (keepPending_ > 0) keepResolve();         // (0.82.3) Gravelord's Keep: fighters owed for omen dice used
    if (!kyroPending_.empty()) kyroResolve();    // (0.82.4) Kyro Reliquary: destroyed - placed again
    monSync();
    while (!pelagionPending_.empty()) {
        const std::pair<int, int> e = pelagionPending_.front();
        pelagionPending_.erase(pelagionPending_.begin());
        pelagionCombats(e.first, e.second);
    }
}

// ------------------------------------------------------------------ The Pelagion (Deepwrought)
bool Rules::pelagionBlocks(int p, int sys) const {
    if (!monumentsOn()) return false;
    for (int q : holders(G, "deepwrought")) {
        int s = -1, planet = -1;
        if (q != p && monAbilityAt(G, q, "deepwrought", &s, &planet) && s == sys && planet >= 0) return true;
    }
    return false;
}

// "After this unit is removed from a planet, choose a player with ships in that system to resolve a space combat. Repeat this until
// it contains no more than 1 player's ships."
void Rules::pelagionCombats(int sys, int owner) {
    const int card = monumentIndex("deepwrought");
    for (int round = 0; round < 16; ++round) {
        std::vector<int> sides;
        for (int q = 0; q < G.numPlayers; ++q)
            if (!G.players[size_t(q)].eliminated && hasShips(G, q, sys)) sides.push_back(q);
        if (hasShips(G, kNeutral, sys)) sides.push_back(kNeutral);
        if (sides.size() <= 1) return;
        std::vector<Option> who;
        for (int q : sides)
            if (q < G.numPlayers) who.push_back({q, pn(q), shipsText(G, q, sys), true, sys});
        if (who.empty()) return;
        const bool canAsk = owner >= 0 && owner < G.numPlayers && !G.players[size_t(owner)].eliminated;
        const int x = who.size() == 1 || !canAsk
                          ? who[0].id
                          : mpick(owner, card, kMonAskDeepwrought,
                                  monName(card) + " is gone: who resolves a space combat in " + sysName(sys) + "?", who, kMonuments[card].text);
        std::vector<Option> foes;
        for (int q : sides)
            if (q != x) foes.push_back({q, q == kNeutral ? std::string("Neutral ships") : pn(q), shipsText(G, q, sys), true, sys});
        curFor_ = owner;
        const int y = foes.size() == 1 ? foes[0].id
                                       : mpick(x, card, kMonAskDeepwroughtFoe, "Space combat in " + sysName(sys) + ": against whom?", foes,
                                               "Monuments+ FAQ: \"That player may choose any other player in the system to resolve combat with.\"");
        curFor_ = -1;
        monEvent(owner, card,
                 "Without " + monName(card) + ": " + pn(x) + " and " + (y == kNeutral ? std::string("the neutral ships") : pn(y)) +
                     " resolve a space combat in " + sysName(sys) + ".",
                 sys);
        spaceCombat(x, y, sys);
        checkFloatingFactories(sys);
        for (int q = 0; q < G.numPlayers; ++q) removeExcessCapacity(q, sys);
        stationControl(sys);
        checkEliminations();
    }
}

// ------------------------------------------------------------------ Veils of Lost Creuss (Crimson Rebellion)
void Rules::monBreachActive(int by, int sys) {
    if (!monumentsOn() || sys < 0) return;
    const int card = monumentIndex("crimson");
    for (int c : holders(G, "crimson")) {
        int ms = -1, mpl = -1;
        if (!monAbilityAt(G, c, "crimson", &ms, &mpl) || mpl < 0) continue;  // ("this planet": it stands on one)
        if (ms != sys && !listHas(G.systems[size_t(ms)].adj, sys)) continue;
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != c && !G.players[size_t(q)].eliminated && hasShips(G, q, sys)) opts.push_back({q, pn(q), shipsText(G, q, sys), true, sys});
        if (hasShips(G, kNeutral, sys)) opts.push_back({kNeutral, "Neutral ships", "", true, sys});
        if (opts.empty()) continue;
        opts.push_back({-1, "No hit", ""});
        curFor_ = by;
        const int t = mpick(c, card, kMonAskCrimson, monName(card) + ": a breach opens in " + sysName(sys) + " - 1 hit against whose ships?", opts,
                            kMonuments[card].text);
        curFor_ = -1;
        if (t < 0) continue;
        monEvent(c, card, pn(c) + "'s " + monName(card) + " strikes through the breach in " + sysName(sys) + ".", sys);
        assignHits(t, sys, -1, 1, shipMask(), monName(card), c);
    }
}

// ------------------------------------------------------------------ Epiphany (Firmament) / Epiphany Hollow (Obsidian)
// (monActivated calls this) "When a player activates this system, place their control token on this card if it does not have one."
// / "After a puppeted player activates this system, replace up to 2 of that player's infantry on a planet they control with your
// infantry; those units coexist."
void Rules::monActivatedTE(int p, int sysAct) {
    if (p < 0 || p >= G.numPlayers) return;
    for (int f : holders(G, "firmament")) {
        int s = -1;
        if (!monAbilityAt(G, f, "firmament", &s) || s != sysAct) continue;
        Player& fp = G.players[size_t(f)];
        if ((fp.epiphanyTok >> p) & 1) continue;
        fp.epiphanyTok = uint16_t(fp.epiphanyTok | (1u << p));
        const int card = monumentIndex("firmament");
        monEvent(f, card, pn(p) + "'s control token goes onto " + monName(card) + ".", s);
    }
    for (int o : holders(G, "obsidian")) {
        int s = -1;
        if (o == p || !monAbilityAt(G, o, "obsidian", &s) || s != sysAct || !anyPuppet(G, o, p)) continue;
        const int spare = unitsForPlacement(G, o, UnitType::Infantry);
        if (spare <= 0) continue;
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p)) {
            const PlanetState& ps = G.planets[size_t(x)];
            const int n = ps.units[size_t(p)].count(UnitType::Infantry);
            if (n <= 0 || ps.destroyed || isDMZ(G, x)) continue;
            for (int k = 1; k <= std::min({2, n, spare}); ++k)
                opts.push_back({x * 4 + k, std::to_string(k) + " infantry on " + planetName(x),
                                std::to_string(n) + " of " + pn(p) + "'s infantry there; yours coexist with them", true, ps.system});
        }
        if (opts.empty()) continue;
        opts.push_back({-1, "Leave them", ""});
        const int card = monumentIndex("obsidian");
        curFor_ = p;
        const int c = mpick(o, card, kMonAskObsidian, monName(card) + ": " + pn(p) + ", your puppet, activated it - replace their infantry?", opts,
                            kMonuments[card].text, false, true);
        curFor_ = -1;
        if (c < 0) continue;
        const int x = c / 4, k = c % 4;
        PlanetState& ps = G.planets[size_t(x)];
        monEvent(o, card, pn(o) + " replaces " + std::to_string(k) + " of " + pn(p) + "'s infantry on " + planetName(x) + " with their own (" +
                              monName(card) + ").",
                 ps.system);
        ps.units[size_t(p)].remove(UnitType::Infantry, k);  // (replaced: back to their reinforcements)
        addUnits(o, UnitType::Infantry, k, ps.system, x);
        startCoexisting(o, x);
    }
}

// "you may remove a control token from this card to skip 1 prerequisite" - whose token (true = removed)
bool Rules::epiphanySkip(int p) {
    if (epiphanyTokens(G, p) <= 0) return false;
    Player& pl = G.players[size_t(p)];
    const int card = monumentIndex("firmament");
    std::vector<Option> opts;
    for (int q = 0; q < G.numPlayers; ++q)
        if ((pl.epiphanyTok >> q) & 1) opts.push_back({q, pn(q) + "'s control token", q == p ? "Your own." : ""});
    const int q = opts.size() == 1 ? opts[0].id
                                   : mpick(p, card, kMonAskEpiphanySkip, monName(card) + ": remove whose control token to skip the prerequisite?", opts,
                                           kMonuments[card].text);
    if (q < 0) return false;
    pl.epiphanyTok = uint16_t(pl.epiphanyTok & ~(1u << q));
    monEvent(p, card, pn(p) + " removes " + pn(q) + "'s control token from " + monName(card) + " to skip a prerequisite.");
    return true;
}

// "... or move a non-Firmament player's control token to a plot card" (a plot card in the Firmament's play area - RULES-CHECK M36)
void Rules::epiphanyPlot(int p) {
    if (epiphanyTokens(G, p) <= 0 || !fac(p, "firmament")) return;
    Player& pl = G.players[size_t(p)];
    std::vector<Option> opts;
    for (int k = 0; k < kPlotCount; ++k) {
        if (!((pl.plotsIn >> k) & 1)) continue;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && ((pl.epiphanyTok >> q) & 1) && !G.players[size_t(q)].eliminated && !((pl.plotTok[size_t(k)] >> q) & 1))
                opts.push_back({k * 16 + q, pn(q) + "'s token onto " + plotName(k), "They become a puppet of that plot when you are the Obsidian."});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "Leave the tokens", ""});
    const int card = monumentIndex("firmament");
    const int c = mpick(p, card, kMonAskEpiphanyPlot, monName(card) + ": move a control token onto one of your plot cards?", opts, kMonuments[card].text);
    if (c < 0) return;
    const int k = c / 16, q = c % 16;
    pl.epiphanyTok = uint16_t(pl.epiphanyTok & ~(1u << q));
    pl.plotTok[size_t(k)] = uint8_t(pl.plotTok[size_t(k)] | (1u << q));
    monEvent(p, card, pn(p) + " moves " + pn(q) + "'s control token from " + monName(card) + " onto a plot card.");
}

// ------------------------------------------------------------------ Sor Tek Linkhub (Ral Nel)
// the systems a structure in s's space area may go to: adjacent, on the board, not a hyperlane or a supernova
std::vector<int> Rules::linkhubDestinations(int s) const {
    std::vector<int> v;
    for (int16_t a : G.systems[size_t(s)].adj) {
        const SystemState& sy = G.systems[size_t(a)];
        if (int(a) == s || sy.offBoard || sy.def < 0 || sy.isHyperlane() || (sy.d().anomalies & kSupernova)) continue;
        if (std::find(v.begin(), v.end(), int(a)) == v.end()) v.push_back(a);
    }
    return v;
}

void Rules::monTacticalEnd(int p, int sysAct) {
    (void)sysAct;
    int ls = -1;
    if (!monumentsOn() || !monAbilityAt(G, p, "ralnel", &ls)) return;
    const int card = monumentIndex("ralnel");
    std::vector<int> from{ls};
    for (int16_t a : G.systems[size_t(ls)].adj)
        if (std::find(from.begin(), from.end(), int(a)) == from.end()) from.push_back(a);
    std::vector<Option> opts;
    for (int s : from) {
        if (linkhubDestinations(s).empty()) continue;
        for (int q = 0; q < G.numPlayers; ++q) {
            const Force& f = G.systems[size_t(s)].space[size_t(q)];
            for (UnitType t : {UnitType::SpaceDock, UnitType::PDS, UnitType::Monument}) {
                if (!f.count(t)) continue;
                const std::string what = t == UnitType::Monument ? std::string(kUnits[G.players[size_t(q)].unit[int(t)]].name)
                                                                 : std::string(unitTypeName(t));
                opts.push_back({(s * 16 + q) * 16 + int(t), (q == p ? std::string("Your ") : pn(q) + "'s ") + what + " in " + sysName(s),
                                "", true, s});
            }
        }
    }
    if (opts.empty()) return;
    opts.push_back({-1, "Don't move", ""});
    const int c = mpick(p, card, kMonAskRalnel, monName(card) + ": move a structure in a space area?", opts, kMonuments[card].text, true);
    if (c < 0) return;
    const int t = c % 16, q = (c / 16) % 16, s = c / 256;
    std::vector<Option> to;
    for (int a : linkhubDestinations(s)) to.push_back({a, sysName(a), "", true, a});
    if (to.empty()) return;
    curFor_ = q;
    const int d = to.size() == 1 ? to[0].id
                                 : mpick(p, card, kMonAskRalnelTo, monName(card) + ": move it to which system?", to, kMonuments[card].text, true);
    curFor_ = -1;
    Force& src = G.systems[size_t(s)].space[size_t(q)];
    if (!src.count(UnitType(t))) return;
    src.remove(UnitType(t), 1);
    G.systems[size_t(d)].space[size_t(q)].add(UnitType(t), 1);
    if (isShip(UnitType(t)) || UnitType(t) == UnitType::SpaceDock) fleetDirty_ = true;
    monEvent(p, card, pn(p) + "'s " + monName(card) + " moves " + (q == p ? std::string("their") : pn(q) + "'s") + " " +
                          (UnitType(t) == UnitType::Monument ? std::string("monument") : std::string(unitTypeName(UnitType(t)))) + " from " +
                          sysName(s) + " to " + sysName(d) + ".",
             d);
    removeExcessCapacity(q, s);
    removeExcessCapacity(q, d);
    checkFloatingFactories(d);
    monSync();          // (a Seraph Data Center takes its station card along)
    stationControl(d);  // (... and whoever alone has ships there controls it)
}

}  // namespace ti
