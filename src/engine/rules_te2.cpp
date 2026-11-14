// Thunder's Edge, part 2 (0.34.0): neutral units on the board (the pirates and mercenaries of its action cards;
// the Fracture's guards come in part 3), coexistence, and the 14 new action cards (20 in the deck). They are
// appended to the action card table (Source::ThundersEdge) and only shuffled in with the module. Our own short
// wording is in card_text.cpp; the AI for them is in ai/ai_cards.cpp.
//
// Neutral units (rulebook): non-player forces in their own slot of every place's unit arrays (kNeutral). They
// fight whoever moves into their space area or lands on their planet, always use their unit abilities (sustain
// damage, space cannon, anti-fighter barrage), take hits lowest on the reference card first (rules_combat.cpp
// neutralHits) and never take turns. They count as another player's units for abilities and blockades.
#include <algorithm>
#include <cstring>

#include "engine/deal.h"
#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
bool key(int card, const char* k) { return card >= 0 && card < kActionCardCount && !std::strcmp(kActionCards[card].key, k); }
int notPassed(const Game& g) {
    int n = 0;
    for (const Player& p : g.players)
        if (!p.passed && !p.eliminated) ++n;
    return n;
}
bool isMecatolPlanet(int16_t x, const Game& g) { return !std::strcmp(kPlanets[g.planets[size_t(x)].def].key, "mr"); }
bool readiedCard(const Game& g, int p) {
    const Player& pl = g.players[size_t(p)];
    for (int8_t c : pl.cards)
        if (!((pl.cardsUsed >> c) & 1)) return true;
    return false;
}
}  // namespace

bool teCard(int card) { return card >= 0 && card < kActionCardCount && kActionCards[card].source == Source::ThundersEdge; }

// ------------------------------------------------------------------ neutral units
void Rules::placeNeutral(UnitType t, int k, int sys, int planet, const std::string& why) {
    if (k <= 0) return;
    Force& f = planet >= 0 ? G.planets[size_t(planet)].units[kNeutral] : G.systems[size_t(sys)].space[kNeutral];
    f.add(t, k);
    say(-1, std::to_string(k) + " neutral " + unitTypeName(t, k > 1) + " placed " +
                (planet >= 0 ? "on " + planetName(planet) : "in " + sysName(sys)) + (why.empty() ? "" : " (" + why + ")") + ".",
        sys);
}

std::vector<int> Rules::pirateSystems() const {
    std::vector<int> out;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const SystemState& sy = G.systems[s];
        if (sy.home >= 0 || sy.isHyperlane() || sy.offBoard) continue;
        bool ships = false;
        for (int q = 0; q < G.numPlayers; ++q)
            if (hasShips(G, q, int(s))) ships = true;
        if (!ships) out.push_back(int(s));
    }
    return out;
}

std::vector<int> Rules::mercenaryPlanets() const {
    std::vector<int> out;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.destroyed || isHomePlanet(G, int(x)) || isStation(G, int(x))) continue;
        if (G.systems[size_t(ps.system)].custodians && !G.custodiansRemoved) continue;  // (Mecatol Rex under the custodians)
        bool units = false;
        for (int q = 0; q < kSides; ++q)
            if (!ps.units[size_t(q)].empty()) units = true;
        if (!units) out.push_back(int(x));
    }
    return out;
}

// ------------------------------------------------------------------ coexistence
// A player whose units coexist on a planet neither fights there nor controls it; when their units are the only ones
// left on it, they gain control of it (exhausted).
// (fixes 25, owner's faction debug pass 2026-11-11) LRR 63.4 / 79.4: a player's PDS or space dock on a planet with none of
// their own ground forces and another player's units there is destroyed - so a coexisting player whose ground forces on
// someone else's planet are gone (a fight, bombardment, Seethe, picked up) loses the structures they left there
void Rules::coexistStructures() {
    if (G.fixes < 25 || !(G.modules & kModThundersEdge)) return;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        PlanetState& ps = G.planets[x];
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == ps.owner || !ps.units[size_t(q)].structures() || groundForcesOn(G, q, int(x)) > 0) continue;
            bool others = false;
            for (int o = 0; o < G.numPlayers; ++o)
                if (o != q && !ps.units[size_t(o)].empty()) others = true;
            if (!others) continue;
            for (UnitType t : {UnitType::PDS, UnitType::SpaceDock})
                if (const int k = ps.units[size_t(q)].count(t))
                    destroyUnits(q, t, k, ps.system, int(x), ps.owner, "none of their ground forces left there");
            if (ps.units[size_t(q)].empty()) ps.coexist = uint16_t(ps.coexist & ~(1u << q));
        }
    }
}

void Rules::coexistCleanup() {
    coexistStructures();  // (fixes 25)
    for (size_t x = 0; x < G.planets.size(); ++x) {
        PlanetState& ps = G.planets[x];
        if (!ps.coexist) continue;
        if (G.fixes >= 25 && ((ps.coexist >> kNeutral) & 1) && ps.units[kNeutral].empty())  // (no neutral units left)
            ps.coexist = uint16_t(ps.coexist & ~(1u << kNeutral));
        for (int q = 0; q < G.numPlayers; ++q) {
            if (!((ps.coexist >> q) & 1)) continue;
            if (ps.units[size_t(q)].empty()) {
                ps.coexist = uint16_t(ps.coexist & ~(1u << q));
                continue;
            }
            bool others = !ps.units[kNeutral].empty();
            for (int o = 0; o < G.numPlayers; ++o)
                if (o != q && !ps.units[size_t(o)].empty()) others = true;
            if (others) continue;
            ps.coexist = uint16_t(ps.coexist & ~(1u << q));
            if (ps.owner != q) {
                say(q, pn(q) + "'s coexisting units are alone on " + planetName(int(x)) + ".", ps.system);
                setControl(int(x), q);
            }
        }
    }
    for (int q = 0; q < G.numPlayers; ++q) oceanLimit(q);  // Oceanbound (Deepwrought)
}

// ------------------------------------------------------------------ when they can be played
std::vector<int> Rules::strategizeCards(int p, bool primary) const {
    std::vector<int> out;
    const Player& pl = G.players[size_t(p)];
    for (int s = 0; s < kStrategyCards; ++s) {
        const int owner = G.cardOwner[s];
        const bool unchosen = owner < 0;
        const bool readied = owner >= 0 && !((G.players[size_t(owner)].cardsUsed >> s) & 1);
        if (!unchosen && !readied) continue;
        if (!primary) {  // the secondary needs a strategy token (Leadership's costs none)
            if (s != 0 && pl.strategy <= 0 && !(s == 4 && fac(p, "hacan"))) continue;
            if (s == 4 && pl.commodities >= commodityCap(G, pl)) continue;
            if (s == 7 && G.secretDeck.empty()) continue;
            if (s == 5 && productionCapacity(G, p, pl.homeSystem) <= 0) continue;
            if (s == 6 && spendable(G, p, false) < 4) continue;
        }
        out.push_back(s);
    }
    return out;
}

std::vector<std::pair<int, int>> Rules::rescueShips(int p, int sys) const {
    std::vector<std::pair<int, int>> out;
    const bool gd = moveBoost(G, p);
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (int(s) == sys || G.systems[s].hasToken(p)) continue;
        const Force& f = G.systems[s].space[size_t(p)];
        for (int t : kShipIdx)
            if (f.n[t] && t != int(UnitType::Fighter) && shipReach(G, p, int(s), sys, UnitType(t), gd ? 1 : 0).ok)
                out.push_back({int(s), t});
    }
    return out;
}

bool Rules::teCardPlayable(int p, int card, const WinCtx& c) const {
    if (!(G.modules & kModThundersEdge)) return false;
    const Player& pl = G.players[size_t(p)];
    auto is = [&](const char* k) { return key(card, k); };
    switch (c.w) {
        case Win::AfterTransaction:  // Lie in Wait: two of your neighbours trade
            if (!is("lieinwait") || p == c.a || p == c.b || c.a < 0 || c.b < 0) return false;
            if (!areNeighbours(G, p, c.a) || !areNeighbours(G, p, c.b)) return false;
            return !G.players[size_t(c.a)].actionCards.empty() || !G.players[size_t(c.b)].actionCards.empty();
        case Win::TurnStart:  // Extreme Duress: on another player who still has a readied strategy card
            return is("extremeduress") && p != c.a && c.a >= 0 && readiedCard(G, c.a) && G.players[size_t(c.a)].duress < 0;
        case Win::TurnEnd:
            if (is("puppetsonastring")) return pl.passed && pl.tactic > 0;
            if (is("crisis")) return notPassed(G) >= 2 && !skipNext_;
            return false;
        case Win::AfterMove:  // Rescue: another player moved into a system with our ships
            return is("rescue") && p != c.active && hasShips(G, p, c.sys) && !rescueShips(p, c.sys).empty();
        case Win::DealNegotiation:  // Black Market Dealings: one of the two negotiating
            return is("blackmarketdealing") && (p == c.a || p == c.b) && !blackMarketPair(G, c.a, c.b);
        case Win::LastShipLost: {  // Crash Landing
            if (!is("crashlanding") || p != c.a || G.systems[size_t(c.sys)].space[size_t(p)].groundForces() <= 0) return false;
            for (int16_t x : G.systems[size_t(c.sys)].planets)
                if (!isMecatolPlanet(x, G) && !G.planets[size_t(x)].destroyed && !isStation(G, x)) return true;
            return false;
        }
        default: return false;
    }
}

bool Rules::teComponentUsable(int p, int card) const {
    if (!(G.modules & kModThundersEdge)) return false;
    const Player& pl = G.players[size_t(p)];
    auto is = [&](const char* k) { return key(card, k); };
    if (is("strategize")) return !strategizeCards(p, false).empty();
    if (is("overrule")) return !strategizeCards(p, true).empty();
    if (is("piratecontract")) return !pirateSystems().empty();
    if (is("piratefleet")) return !pirateSystems().empty() && spendable(G, p, false) >= 3;
    if (is("mercenarycontract")) return pl.tradeGoods >= 2 && !mercenaryPlanets().empty();
    if (is("brilliance")) {
        for (int x : controlledPlanets(G, p))
            if (G.planets[size_t(x)].exhausted && planetHasSkip(G, x)) return true;
        return false;  // (the breakthrough choice comes with the breakthroughs)
    }
    if (is("exchangeprogram")) {
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[size_t(q)].eliminated) return true;
        return false;
    }
    return false;
}

// ------------------------------------------------------------------ what they do
bool Rules::teResolve(int p, int card, const WinCtx* c) {
    if (!teCard(card)) return false;
    Player& pl = G.players[size_t(p)];
    auto is = [&](const char* k) { return key(card, k); };
    const int sys = c ? c->sys : -1;

    if (is("blackmarketdealing") && c) {  // relics, action cards and secrets may be part of this transaction
        G.blackMarket[0] = int8_t(c->a);
        G.blackMarket[1] = int8_t(c->b);
        say(p, pn(c->a) + " and " + pn(c->b) + " may trade relics, action cards and secret objectives (Black Market Dealings).");
        return true;
    }
    if (is("lieinwait") && c) {  // look at both hands, take 1 card from each
        for (int q : {c->a, c->b}) {
            if (q < 0 || G.players[size_t(q)].actionCards.empty()) continue;
            std::vector<Option> opts;
            for (int16_t x : G.players[size_t(q)].actionCards) {
                bool dup = false;
                for (const Option& o : opts)
                    if (o.id == x) dup = true;
                if (!dup) opts.push_back({x, kActionCards[x].name, actionCardText(x)});
            }
            const int take = pick(p, Tag::CardTarget, "Lie in Wait: take which of " + pn(q) + "'s action cards?", opts,
                                  pn(q) + " holds " + std::to_string(G.players[size_t(q)].actionCards.size()) + " action cards.");
            auto& h = G.players[size_t(q)].actionCards;
            auto it = std::find(h.begin(), h.end(), int16_t(take));
            if (it == h.end()) continue;
            h.erase(it);
            pl.actionCards.push_back(int16_t(take));
            say(p, pn(p) + " looks at " + pn(q) + "'s action cards and takes one (Lie in Wait).");
        }
        return true;
    }
    if (is("extremeduress") && c) {
        G.players[size_t(c->a)].duress = int8_t(p);
        say(p, pn(c->a) + " is under Extreme Duress: unless their next action is a strategic action, they discard their action "
                          "cards, give " + pn(p) + " their trade goods and show their secret objectives.");
        return true;
    }
    if (is("puppetsonastring")) {  // one action, now
        const int prevActive = G.activePlayer;
        G.activePlayer = int8_t(p);
        say(p, pn(p) + " takes 1 action (Puppets on a String).");
        takeTurn(p, true);
        G.activePlayer = int8_t(prevActive);
        return true;
    }
    if (is("crisis")) {
        skipNext_ = true;
        say(p, "Crisis: the next player's turn is skipped.");
        return true;
    }
    if (is("rescue") && c) {
        std::vector<Option> opts;
        for (const auto& r : rescueShips(p, sys))
            opts.push_back({r.first * 16 + r.second, std::string(unitTypeName(UnitType(r.second))) + " from " + sysName(r.first), ""});
        if (opts.empty()) return true;
        const int id = pick(p, Tag::CardTarget, "Rescue: move which ship into " + sysName(sys) + "?", opts, "", true);
        const int from = id / 16, t = id % 16;
        Force& src = G.systems[size_t(from)].space[size_t(p)];
        const bool dmg = src.dmg[t] >= src.n[t];  // (an undamaged one if there is one)
        src.remove(UnitType(t), 1, false);
        G.systems[size_t(sys)].space[size_t(p)].add(UnitType(t), 1);
        if (dmg) G.systems[size_t(sys)].space[size_t(p)].dmg[t]++;
        Event e;
        e.kind = EventKind::Move;
        e.player = int8_t(p);
        e.system = int16_t(sys);
        e.from = {int16_t(from)};
        e.text = pn(p) + " moves 1 " + unitTypeName(UnitType(t)) + " into " + sysName(sys) + " (Rescue).";
        E.emit(e);
        return true;
    }
    if (is("crashlanding") && c) {
        Force& sp = G.systems[size_t(sys)].space[size_t(p)];
        std::vector<Option> opts;
        for (int16_t x : G.systems[size_t(sys)].planets) {
            if (isMecatolPlanet(x, G) || G.planets[size_t(x)].destroyed || isStation(G, x)) continue;
            for (UnitType t : {UnitType::Infantry, UnitType::Mech})
                if (sp.count(t)) opts.push_back({x * 16 + int(t), std::string(unitTypeName(t)) + " onto " + planetName(x), ""});
        }
        if (opts.empty()) return true;
        const int id = pick(p, Tag::CardTarget, "Crash Landing: place which ground force on which planet?", opts, "", false, true);
        const int x = id / 16, t = id % 16;
        sp.remove(UnitType(t), 1, false);
        PlanetState& ps = G.planets[size_t(x)];
        ps.units[size_t(p)].add(UnitType(t), 1);
        bool others = !ps.units[kNeutral].empty();
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !ps.units[size_t(q)].empty()) others = true;
        if (others) {
            ps.coexist = uint16_t(ps.coexist | (1u << p));
            say(p, pn(p) + "'s " + unitTypeName(UnitType(t)) + " crash-lands on " + planetName(x) + " and coexists there.", sys);
        } else {
            say(p, pn(p) + "'s " + unitTypeName(UnitType(t)) + " crash-lands on " + planetName(x) + ".", sys);
            if (ps.owner != p) setControl(x, p);
        }
        return true;
    }

    // ---- component actions
    if (is("strategize") || is("overrule")) {
        const bool primary = is("overrule");
        std::vector<Option> opts;
        for (int s : strategizeCards(p, primary))
            opts.push_back({s, std::to_string(s + 1) + " " + strategyName(s), primary ? strategyPrimary(s) : strategySecondary(s)});
        if (opts.empty()) return true;
        const int s = pick(p, Tag::CardTarget, std::string(primary ? "Overrule" : "Strategize") + ": which strategy card's " +
                                                   (primary ? "primary" : "secondary") + " ability?",
                           opts);
        const int prev = strategicPlayer_;
        strategicPlayer_ = -1;
        if (primary) {
            say(p, pn(p) + " resolves the primary ability of " + strategyName(s) + " (Overrule).");
            primaryAbility(p, s);
        } else {
            say(p, pn(p) + " resolves the secondary ability of " + strategyName(s) + " (Strategize).");
            const bool was = forcedSecondary_;
            forcedSecondary_ = true;
            secondaryAbility(p, s);
            forcedSecondary_ = was;
        }
        strategicPlayer_ = prev;
        return true;
    }
    if (is("piratecontract") || is("piratefleet")) {
        const bool fleet = is("piratefleet");
        if (fleet && !pay(p, 3, false, "Pirate Fleet", false)) return true;
        std::vector<Option> opts;
        for (int s : pirateSystems()) opts.push_back({s, sysName(s), ""});
        if (opts.empty()) return true;
        const int s = pick(p, Tag::CardTarget, std::string(fleet ? "Pirate Fleet" : "Pirate Contract") + ": place the neutral " +
                                                   (fleet ? "fleet" : "destroyer") + " in which system?",
                           opts, "A non-home system without any player's ships.", true);
        if (fleet) {
            placeNeutral(UnitType::Carrier, 1, s, -1, "Pirate Fleet");
            placeNeutral(UnitType::Cruiser, 1, s, -1, "");
            placeNeutral(UnitType::Destroyer, 1, s, -1, "");
            placeNeutral(UnitType::Fighter, 2, s, -1, "");
        } else {
            placeNeutral(UnitType::Destroyer, 1, s, -1, "Pirate Contract");
        }
        return true;
    }
    if (is("mercenarycontract")) {
        std::vector<Option> opts;
        for (int x : mercenaryPlanets())
            opts.push_back({x, planetName(x), G.planets[size_t(x)].owner >= 0 ? pn(G.planets[size_t(x)].owner) + "'s" : "unclaimed"});
        if (opts.empty() || pl.tradeGoods < 2) return true;
        const int x = pick(p, Tag::CardTarget, "Mercenary Contract: place 2 neutral infantry on which planet?", opts,
                           "A non-home planet without units. Its owner loses it.", false, true);
        pl.tradeGoods = int8_t(pl.tradeGoods - 2);
        PlanetState& ps = G.planets[size_t(x)];
        placeNeutral(UnitType::Infantry, 2, ps.system, x, "Mercenary Contract");
        if (ps.owner >= 0) {
            const int old = ps.owner;
            ps.owner = -1;
            ps.exhausted = false;
            say(old, pn(old) + " loses " + planetName(x) + " (its card goes back to the deck).", ps.system);
            persControl(x, old, -1);
        }
        return true;
    }
    if (is("brilliance")) {
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p))
            if (G.planets[size_t(x)].exhausted && planetHasSkip(G, x)) opts.push_back({x, "Ready " + planetName(x), ""});
        if (opts.empty()) return true;
        const int x = pick(p, Tag::CardTarget, "Brilliance: ready which planet?", opts, "", false, true);
        G.planets[size_t(x)].exhausted = false;
        say(p, pn(p) + " readies " + planetName(x) + " (Brilliance).");
        return true;
    }
    if (is("exchangeprogram")) {
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[size_t(q)].eliminated) opts.push_back({q, pn(q), ""});
        if (opts.empty()) return true;
        const int q = pick(p, Tag::CardTarget, "Exchange Program: with which player?", opts);
        auto spots = [&](int who, int host) {  // planets 'host' controls with host's ground forces
            std::vector<int> v;
            for (int x : controlledPlanets(G, host))
                if (G.planets[size_t(x)].units[size_t(host)].groundForces() > 0 && !isDMZ(G, x)) v.push_back(x);
            (void)who;
            return v;
        };
        const std::vector<int> mine = spots(p, q), theirs = spots(q, p);
        const bool possible = !mine.empty() && !theirs.empty() && unitsAvailable(G, p, UnitType::Infantry) > 0 &&
                              unitsAvailable(G, q, UnitType::Infantry) > 0;
        bool agreed = false;
        if (possible) {
            const int prevFor = curFor_;
            curFor_ = p;
            agreed = yesNo(q, Tag::CardTarget, pn(p) + " proposes an Exchange Program: agree?",
                           "Each of you places 1 infantry into coexistence on a planet the other controls. "
                           "If you refuse, you each discard 1 token from your fleet pool.");
            curFor_ = prevFor;
        }
        if (agreed) {
            for (int side = 0; side < 2; ++side) {
                const int who = side == 0 ? p : q;
                const std::vector<int>& at = side == 0 ? mine : theirs;
                std::vector<Option> o2;
                for (int x : at) o2.push_back({x, planetName(x), ""});
                const int x = pick(who, Tag::CardTarget, "Exchange Program: place your infantry on which planet?", o2, "", false, true);
                G.planets[size_t(x)].units[size_t(who)].add(UnitType::Infantry, 1);
                G.planets[size_t(x)].coexist = uint16_t(G.planets[size_t(x)].coexist | (1u << who));
                say(who, pn(who) + " places 1 infantry on " + planetName(x) + " (coexisting, Exchange Program).", G.planets[size_t(x)].system);
            }
        } else {
            for (int who : {p, q})
                if (G.players[size_t(who)].fleet > 0) {
                    G.players[size_t(who)].fleet = int8_t(G.players[size_t(who)].fleet - 1);
                    say(who, pn(who) + " discards 1 token from the fleet pool (Exchange Program refused).");
                }
            enforceFleetLimit(p);
            enforceFleetLimit(q);
        }
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ the hooks
void Rules::teTurnStart(int p) {
    G.players[size_t(p)].duress = -1;
    if (!(G.modules & kModThundersEdge)) return;
    WinCtx w;
    w.w = Win::TurnStart;
    w.a = p;
    window(w);
    obsidianTurnStart(p);  // Neural Parasite (Obsidian)
}

bool Rules::teTurnEnd(int p) {
    if (!(G.modules & kModThundersEdge)) return false;
    G.players[size_t(p)].duress = -1;
    WinCtx w;
    w.w = Win::TurnEnd;
    w.a = p;
    window(w);
    return skipNext_;
}

void Rules::duressCheck(int p, bool strategic) {
    Player& pl = G.players[size_t(p)];
    const int by = pl.duress;
    if (by < 0) return;
    pl.duress = -1;
    if (strategic) {
        say(p, pn(p) + " makes a strategic action: Extreme Duress has no effect.");
        return;
    }
    int tg = pl.tradeGoods;
    const std::vector<int16_t> hand = pl.actionCards;
    pl.actionCards.clear();
    for (int16_t x : hand) discardActionCard(p, x, false);
    if (tg && vaultShut(p, by, "their trade goods")) tg = 0;  // (High Security Vault: the cards still go)
    pl.tradeGoods = int8_t(pl.tradeGoods - tg);
    if (tg) gainTG(by, tg);
    std::string secrets;
    for (int16_t o : pl.secrets) secrets += std::string(secrets.empty() ? "" : ", ") + kObjectives[o].name;
    say(p, pn(p) + " doesn't make a strategic action (Extreme Duress): discards their action cards and gives " + pn(by) + " " +
               std::to_string(tg) + " trade goods.");
    if (!secrets.empty()) feedNote(by, pn(p) + "'s secret objectives: " + secrets + ".");
}

void Rules::teTransaction(int a, int b) {
    if (!(G.modules & kModThundersEdge) || a < 0 || b < 0) return;
    WinCtx w;
    w.w = Win::AfterTransaction;
    w.a = a;
    w.b = b;
    window(w);
}

void Rules::lastShipLost(int p, int sys) {
    if (!(G.modules & kModThundersEdge) || p < 0 || p >= G.numPlayers) return;
    WinCtx w;
    w.w = Win::LastShipLost;
    w.a = p;
    w.sys = sys;
    window(w);
}

}  // namespace ti
