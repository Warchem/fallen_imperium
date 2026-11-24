// Monuments+ (0.82.0; Cacotopos, after Blarknob's Monuments) - the monuments' abilities.
//
// Each card's text is the rule (owner 2026-11-21: exact wording). "This system" / "this planet" is where the card's monument
// unit stands; such abilities do nothing while it is off the board. A few act as cards (the Monuments+ rulebook: "If an
// ability doesn't specify 'this system' or a location relating to the unit, the ability probably triggers even if you don't
// have the monument on the board"): the Winnu's vault, the Keleres' law tokens, the Nekro's assimilator tokens, the N'orr's
// "When you place your monument".
//
// The Nekro's Valefar Assimilator "M" gains the text of the cards with its "M" / "Z" tokens: every ability here asks
// monCardHas / monAbilityAt (query.cpp), which look at all of a player's cards - so "you" / "this system" are the Nekro and
// their monument.
//
// Questions: Tag::MonumentAbility (Decision::ability = the card whose text is used, Decision::value = MonAsk). The computer
// players answer in ai_monuments.cpp.

#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
// the systems "in or adjacent to" a system (the system first)
std::vector<int> inOrNext(const Game& g, int sys) {
    std::vector<int> v;
    if (sys < 0) return v;
    v.push_back(sys);
    for (int16_t a : g.systems[size_t(sys)].adj)
        if (std::find(v.begin(), v.end(), int(a)) == v.end()) v.push_back(a);
    return v;
}
bool inList(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }
bool dockIn(const Game& g, int p, int sys) {
    if (g.systems[size_t(sys)].space[size_t(p)].count(UnitType::SpaceDock)) return true;
    for (int16_t x : g.systems[size_t(sys)].planets)
        if (g.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock)) return true;
    return false;
}
bool hasTrait(const Game& g, int x) {
    return hasTraitOf(g, x, Trait::Cultural) || hasTraitOf(g, x, Trait::Hazardous) || hasTraitOf(g, x, Trait::Industrial);
}
// every player whose monument card has this card's text (the card's owner and a Nekro with a token on it)
std::vector<int> holdersOf(const Game& g, const char* key) {
    std::vector<int> v;
    if (!(g.modules & kModMonuments)) return v;
    for (int q = 0; q < g.numPlayers; ++q)
        if (!g.players[size_t(q)].eliminated && monCardHas(g, q, key)) v.push_back(q);
    return v;
}
}  // namespace

// ------------------------------------------------------------------ asking
int Rules::mpick(int p, int card, int ask, const std::string& prompt, std::vector<Option> opts, const std::string& context, bool onMap,
                 bool onPlanets) {
    if (mutedFor_ >= 0 && p != mutedFor_) {  // (as fpick: a player who can't resolve abilities now - the "don't" answer)
        for (const Option& o : opts)
            if (o.id == -1) return -1;
        if (opts.size() == 2 && opts[0].id == 1 && opts[1].id == 0) return 0;
    }
    const int prevA = curAbility_, prevV = curValue_;
    curAbility_ = card;
    curValue_ = ask;
    const int r = pick(p, Tag::MonumentAbility, prompt, std::move(opts), context, onMap, onPlanets);
    curAbility_ = prevA;
    curValue_ = prevV;
    return r;
}

std::vector<int> Rules::mpickMany(int p, int card, int ask, const std::string& prompt, std::vector<Option> opts, int minPick, int maxPick,
                                  const std::string& context, bool onMap) {
    if (mutedFor_ >= 0 && p != mutedFor_ && minPick == 0) return {};
    const int prevA = curAbility_, prevV = curValue_;
    curAbility_ = card;
    curValue_ = ask;
    (void)onMap;
    std::vector<int> r = pickMany(p, Tag::MonumentAbility, prompt, std::move(opts), minPick, maxPick, context);
    curAbility_ = prevA;
    curValue_ = prevV;
    return r;
}

bool Rules::myes(int p, int card, int ask, const std::string& prompt, const std::string& context) {
    return mpick(p, card, ask, prompt, {{1, "Yes", ""}, {0, "No", ""}}, context) == 1;
}

std::string Rules::monName(int card) const {
    if (card < 0 || card >= kMonumentCount) return "monument";
    const int u = unitByKey(kMonuments[card].unit);
    return u >= 0 ? kUnits[u].name : kMonuments[card].subtitle;
}

// unit = Monument and card = the monument card mark it as a monument's event (the screen can show the card); notice > 0 (owner
// 2026-11-22: "make sure the human player knows he's getting it") = a bonus taken without a question - 1 trade goods, 2 votes,
// amount in hits - shown large to its player until A (the "Cards" setting)
void Rules::monEvent(int p, int card, const std::string& text, int sys, int notice, int amount) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = monName(card);
    e.text = text;
    e.system = int16_t(sys);
    e.unit = int8_t(UnitType::Monument);
    e.card = int16_t(card);
    e.count = int16_t(notice);
    e.hits = amount;
    E.emit(e);
}

void Rules::monExhaust(int p, int card) {
    G.players[size_t(p)].monExhausted = true;
    (void)card;
}

// ------------------------------------------------------------------ component actions (ACTION: on the card)
// Argent (choose this or an adjacent system without your ships: a hit against each ship there with SUSTAIN DAMAGE), Titans of
// Ul (a sleeper token on a planet with a trait in or next to the system), Yin (for every 2 infantry on the planet, move 1 to
// another planet you control). LRR 22.3: offered only when they can do something.
namespace {
bool sustainShipsIn(const Game& g, int sys, int except) {
    for (int q = 0; q < kSides; ++q) {
        if (q == except || (q < g.numPlayers && g.players[size_t(q)].eliminated) || (q >= g.numPlayers && q != kNeutral)) continue;
        const Force& f = g.systems[size_t(sys)].space[size_t(q)];
        for (int t = 0; t < kUnitTypes; ++t)
            if (f.n[t] && isShip(UnitType(t)) && unitDef(g, q, UnitType(t)).sustain) return true;
    }
    return false;
}
int sleepersOnBoard(const Game& g) {
    int n = 0;
    for (const PlanetState& ps : g.planets) n += ps.sleeper ? 1 : 0;
    return n;
}
}  // namespace

std::vector<Option> Rules::monActionOptions(int p) {
    std::vector<Option> opts;
    if (!monumentsOn() || !monCardReady(G, p)) return opts;
    int c[3];
    const int n = monumentCards(G, p, c);
    int sys = -1, planet = -1;
    for (int i = 0; i < n; ++i) {
        const char* k = kMonuments[c[i]].key;
        if (!monAbilityAt(G, p, k, &sys, &planet)) continue;
        bool can = false;
        if (!std::strcmp(k, "argent")) {
            for (int s : inOrNext(G, sys))
                if (!hasShips(G, p, s) && sustainShipsIn(G, s, p)) can = true;
        } else if (!std::strcmp(k, "titans")) {
            // (the sleeper tokens are the Titans': a Nekro with this card's text has none to place - RULES-CHECK)
            if (sleepersOnBoard(G) < 5 && fac(p, "titans"))
                for (int s : inOrNext(G, sys))
                    for (int16_t x : G.systems[size_t(s)].planets)
                        if (!G.planets[size_t(x)].sleeper && !G.planets[size_t(x)].destroyed && hasTrait(G, x)) can = true;
        } else if (!std::strcmp(k, "yin")) {
            can = planet >= 0 && G.planets[size_t(planet)].units[size_t(p)].count(UnitType::Infantry) >= 2 && controlledPlanets(G, p).size() > 1;
        } else if (!std::strcmp(k, "mortheus")) {  // (0.82.3) Mirrorforge: another player's non-fighter ships next to it
            for (int16_t a : G.systems[size_t(sys)].adj)
                for (int q = 0; q < kSides && !can; ++q)
                    if (q != p && (q < G.numPlayers || q == kNeutral) && !G.systems[size_t(a)].isHyperlane() &&
                        G.systems[size_t(a)].space[size_t(q)].nonFighterShips() > 0)
                        can = true;
        }
        if (can) opts.push_back({3100 + c[i], "Monument: " + monName(c[i]), kMonuments[c[i]].text});
    }
    return opts;
}

bool Rules::monAction(int p, int card) {
    if (card < 0 || card >= kMonumentCount || !monCardReady(G, p)) return false;
    const char* k = kMonuments[card].key;
    int sys = -1, planet = -1;
    if (!monAbilityAt(G, p, k, &sys, &planet)) return false;
    if (!std::strcmp(k, "argent")) {
        std::vector<Option> opts;
        for (int s : inOrNext(G, sys))
            if (!hasShips(G, p, s) && sustainShipsIn(G, s, p)) opts.push_back({s, sysName(s), "A hit against each ship there with SUSTAIN DAMAGE."});
        if (opts.empty()) return false;
        opts.push_back({-1, "Back", ""});
        const int s = mpick(p, card, kMonAskArgent, monName(card) + ": which system?", opts, kMonuments[card].text, true);
        if (s < 0) return false;
        monExhaust(p, card);
        monEvent(p, card, pn(p) + " fires " + monName(card) + " into " + sysName(s) + ".", s);
        // "Produce a hit against each ship in that system that has SUSTAIN DAMAGE": each such ship takes its own hit - an
        // undamaged one uses Sustain Damage (it becomes damaged), a damaged one (or one that can't use it now) is destroyed
        for (int q = 0; q < kSides; ++q) {
            if (q == p || (q >= G.numPlayers && q != kNeutral)) continue;
            Force& f = G.systems[size_t(s)].space[size_t(q)];
            for (int t = 0; t < kUnitTypes; ++t) {
                if (!f.n[t] || !isShip(UnitType(t)) || !unitDef(G, q, UnitType(t)).sustain) continue;
                const int damaged = f.dmg[t], fresh = f.n[t] - f.dmg[t];
                const bool canSustain = unitSustainsAt(G, q, UnitType(t), s, -1);
                if (damaged > 0 || !canSustain) {
                    struct HitsGuard {
                        bool& b;
                        bool was;
                        ~HitsGuard() { b = was; }
                    } hg{byHits_, byHits_};
                    byHits_ = true;
                    destroyUnits(q, UnitType(t), canSustain ? damaged : f.n[t], s, -1, p, monName(card).c_str());
                }
                if (canSustain && fresh > 0) {
                    Force& g2 = G.systems[size_t(s)].space[size_t(q)];
                    for (int i = 0; i < fresh && g2.dmg[t] < g2.n[t]; ++i) {
                        g2.dmg[t] = uint8_t(g2.dmg[t] + 1);
                        Event e;
                        e.kind = EventKind::Damaged;
                        e.player = int8_t(q);
                        e.other = int8_t(p);
                        e.system = int16_t(s);
                        e.planet = -1;
                        e.unit = int8_t(t);
                        e.count = 1;
                        e.text = pn(q) + "'s " + unitLabel(G, q, UnitType(t)) + " sustains damage.";
                        E.emit(e);
                        if (q < G.numPlayers) sustained(q, p, s, -1, t);
                    }
                }
            }
        }
        return true;
    }
    if (!std::strcmp(k, "mortheus")) return mirrorforgeAction(p, card, sys);  // (0.82.3) rules_monuments6.cpp
    if (!std::strcmp(k, "titans")) {
        if (sleepersOnBoard(G) >= 5) return false;
        std::vector<Option> opts;
        for (int s : inOrNext(G, sys))
            for (int16_t x : G.systems[size_t(s)].planets)
                if (!G.planets[size_t(x)].sleeper && !G.planets[size_t(x)].destroyed && hasTrait(G, x))
                    opts.push_back({x, planetName(x), std::to_string(5 - sleepersOnBoard(G)) + " sleeper tokens left", true, s});
        if (opts.empty()) return false;
        opts.push_back({-1, "Back", ""});
        const int x = mpick(p, card, kMonAskTitans, monName(card) + ": a sleeper token on which planet?", opts, kMonuments[card].text, false,
                            true);
        if (x < 0) return false;
        monExhaust(p, card);
        G.planets[size_t(x)].sleeper = true;
        monEvent(p, card, pn(p) + " places a sleeper token on " + planetName(x) + " (" + monName(card) + ").", G.planets[size_t(x)].system);
        return true;
    }
    if (!std::strcmp(k, "yin")) {
        if (planet < 0) return false;
        const int k2 = G.planets[size_t(planet)].units[size_t(p)].count(UnitType::Infantry) / 2;
        if (k2 <= 0) return false;
        monExhaust(p, card);
        monEvent(p, card, pn(p) + " uses " + monName(card) + ": " + std::to_string(k2) + " infantry may leave " + planetName(planet) + ".",
                 sys);
        // "for every 2 infantry on this planet, move 1 of those infantry to any other planet you control"
        for (int i = 0; i < k2 && G.planets[size_t(planet)].units[size_t(p)].count(UnitType::Infantry) > 0; ++i) {
            std::vector<Option> opts;
            for (int x : controlledPlanets(G, p))
                if (x != planet && !isDMZ(G, x) && !G.planets[size_t(x)].destroyed)
                    opts.push_back({x, planetName(x), std::to_string(G.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry)) + " infantry there",
                                    true, G.planets[size_t(x)].system});
            if (opts.empty()) break;
            const int x = mpick(p, card, kMonAskYin,
                                monName(card) + ": move infantry " + std::to_string(i + 1) + " of " + std::to_string(k2) + " to which planet?",
                                opts, "", false, true);
            if (x < 0) break;
            G.planets[size_t(planet)].units[size_t(p)].remove(UnitType::Infantry, 1, true);
            G.planets[size_t(x)].units[size_t(p)].add(UnitType::Infantry, 1);
            say(p, pn(p) + " moves an infantry from " + planetName(planet) + " to " + planetName(x) + " (" + monName(card) + ").",
                G.planets[size_t(x)].system);
        }
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ the start and end of a turn
void Rules::monTurnStart(int p) {
    if (!monumentsOn()) return;
    // Erwan's Fist (Mentak): "At the start of a player's turn, exhaust this card to have each ship in this or an adjacent
    // system with SUSTAIN DAMAGE become damaged." (one system - RULES-CHECK)
    for (int q : holdersOf(G, "mentak")) {
        int sys = -1;
        if (!monCardReady(G, q) || !monAbilityAt(G, q, "mentak", &sys)) continue;
        const int card = monumentIndex("mentak");
        std::vector<Option> opts;
        for (int s : inOrNext(G, sys)) {
            int fresh = 0;
            for (int r = 0; r < kSides; ++r) {
                if (r >= G.numPlayers && r != kNeutral) continue;
                const Force& f = G.systems[size_t(s)].space[size_t(r)];
                for (int t = 0; t < kUnitTypes; ++t)
                    if (f.n[t] > f.dmg[t] && isShip(UnitType(t)) && unitDef(G, r, UnitType(t)).sustain) fresh += f.n[t] - f.dmg[t];
            }
            if (fresh) opts.push_back({s, sysName(s), std::to_string(fresh) + " undamaged ship(s) with SUSTAIN DAMAGE", true, s});
        }
        if (opts.empty()) continue;
        opts.push_back({-1, "Not now", ""});
        curFor_ = p;
        const int s = mpick(q, card, kMonAskMentak, "Start of " + pn(p) + "'s turn: use " + monName(card) + "?", opts, kMonuments[card].text,
                            true);
        curFor_ = -1;
        if (s < 0) continue;
        monExhaust(q, card);
        for (int r = 0; r < kSides; ++r) {
            if (r >= G.numPlayers && r != kNeutral) continue;
            Force& f = G.systems[size_t(s)].space[size_t(r)];
            for (int t = 0; t < kUnitTypes; ++t)
                if (f.n[t] > f.dmg[t] && isShip(UnitType(t)) && unitDef(G, r, UnitType(t)).sustain) f.dmg[t] = f.n[t];
        }
        monEvent(q, card, pn(q) + "'s " + monName(card) + ": every ship in " + sysName(s) + " with SUSTAIN DAMAGE is damaged.", s);
    }
    // L1Z1X: "At the start of your turn, you may exhaust this card to remove a player's command token from an adjacent system and
    // place it in this system."
    {
        int sys = -1;
        const int card = monumentIndex("l1z1x");
        if (monCardHas(G, p, "l1z1x") && monCardReady(G, p) && monAbilityAt(G, p, "l1z1x", &sys)) {
            std::vector<Option> opts;
            for (int16_t a : G.systems[size_t(sys)].adj)
                for (int q = 0; q < G.numPlayers; ++q)
                    if (G.systems[size_t(a)].hasToken(q) && !G.systems[size_t(sys)].hasToken(q))
                        opts.push_back({a * 16 + q, pn(q) + "'s token from " + sysName(a), "Into " + sysName(sys) + ".", true, a});
            if (!opts.empty()) {
                opts.push_back({-1, "Not now", ""});
                const int c = mpick(p, card, kMonAskL1z1x, monName(card) + ": move a command token?", opts, kMonuments[card].text);
                if (c >= 0) {
                    const int from = c / 16, q = c % 16;
                    monExhaust(p, card);
                    G.systems[size_t(from)].tokens = uint16_t(G.systems[size_t(from)].tokens & ~(1u << q));
                    G.systems[size_t(sys)].tokens = uint16_t(G.systems[size_t(sys)].tokens | (1u << q));
                    monEvent(p, card, pn(p) + " moves " + pn(q) + "'s command token from " + sysName(from) + " to " + sysName(sys) + ".", sys);
                }
            }
        }
    }
    // Beacon of Ragh (Saar): "You may treat this system as being adjacent to 1 of your space docks." - which dock, chosen at the
    // start of their turn (the only one: no question)
    if (monCardHas(G, p, "saar")) {
        int sys = -1;
        Player& pl = G.players[size_t(p)];
        if (!monAbilityAt(G, p, "saar", &sys)) {
            pl.saarBeacon = -1;
        } else {
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (int(s) != sys && dockIn(G, p, int(s))) opts.push_back({int(s), sysName(int(s)), "", true, int(s)});
            if (opts.empty()) pl.saarBeacon = -1;
            else if (opts.size() == 1) pl.saarBeacon = int16_t(opts[0].id);
            else {
                const int card = monumentIndex("saar");
                pl.saarBeacon = int16_t(mpick(p, card, kMonAskSaar, monName(card) + ": adjacent to which of your space docks this turn?", opts,
                                              kMonuments[card].text, true));
            }
        }
    }
    // Lodestar (Nomad): "At the start of your turn: You may return this unit to your reinforcements to produce, or remove and
    // produce, your flagship in this system." (produce: its cost is paid - RULES-CHECK)
    {
        int sys = -1;
        const int card = monumentIndex("nomad");
        if (monCardHas(G, p, "nomad") && monAbilityAt(G, p, "nomad", &sys) && !otherShipsIn(G, p, sys)) {
            const int cost = (unitCost2(G, p, UnitType::Flagship) + 1) / 2;
            const bool onBoard = unitsOnBoard(G, p, UnitType::Flagship) > 0;
            bool here = G.systems[size_t(sys)].space[size_t(p)].count(UnitType::Flagship) > 0;
            if (!here && (onBoard || unitsAvailable(G, p, UnitType::Flagship) > 0) && spendable(G, p, false) >= cost &&
                myes(p, card, kMonAskNomad,
                     monName(card) + ": return it to " + std::string(onBoard ? "move (remove and produce)" : "produce") + " your flagship in " +
                         sysName(sys) + " for " + std::to_string(cost) + " resources?",
                     kMonuments[card].text) &&
                pay(p, cost, false, monName(card) + ": your flagship", true)) {
                int pl = -1;
                monumentSystem(G, p, &pl);
                if (pl >= 0) G.planets[size_t(pl)].units[size_t(p)].remove(UnitType::Monument, 1, true);
                else G.systems[size_t(sys)].space[size_t(p)].remove(UnitType::Monument, 1, true);
                for (size_t s = 0; s < G.systems.size() && onBoard; ++s)
                    if (G.systems[s].space[size_t(p)].count(UnitType::Flagship)) {
                        G.systems[s].space[size_t(p)].remove(UnitType::Flagship, 1, true);
                        say(p, pn(p) + "'s flagship leaves " + sysName(int(s)) + ".", int(s));
                    }
                addUnits(p, UnitType::Flagship, 1, sys, -1);
                monEvent(p, card, pn(p) + " returns " + monName(card) + " and produces their flagship in " + sysName(sys) + ".", sys);
                fleetDirty_ = true;
            }
        }
    }
    monTurnStartDS(p);  // (0.82.3) Corsairs' Cove, Dawnstar HQ
    monTurnStartDS3(p);  // (0.82.4) Verdant Halo
}

void Rules::monTurnEnd(int p) {
    G.monLawsBlankFor = -1;  // (0.82.4) Verdant Halo: "until the end of this turn"
    if (!monumentsOn()) return;
    // G'hom Firefly Beacon (N'orr): "At the end of your turn, you may replace this unit with a different structure from your
    // reinforcements."
    int sys = -1, planet = -1;
    const int card = monumentIndex("sardakk");
    if (monCardHas(G, p, "sardakk") && monAbilityAt(G, p, "sardakk", &sys, &planet) && planet >= 0) {
        std::vector<Option> opts;
        const Force& f = G.planets[size_t(planet)].units[size_t(p)];
        if (unitsAvailable(G, p, UnitType::SpaceDock) > 0 && !f.count(UnitType::SpaceDock))
            opts.push_back({int(UnitType::SpaceDock), "A space dock", "The monument goes back to your reinforcements."});
        if (unitsAvailable(G, p, UnitType::PDS) > 0 && f.count(UnitType::PDS) < 2)
            opts.push_back({int(UnitType::PDS), "A PDS", "The monument goes back to your reinforcements."});
        if (!opts.empty()) {
            opts.push_back({-1, "Keep it", ""});
            const int t = mpick(p, card, kMonAskNorrReplace, "End of your turn: replace " + monName(card) + " on " + planetName(planet) + "?", opts,
                                kMonuments[card].text);
            if (t >= 0) {
                G.planets[size_t(planet)].units[size_t(p)].remove(UnitType::Monument, 1, true);
                addUnits(p, UnitType(t), 1, sys, planet);
                monEvent(p, card, pn(p) + " replaces " + monName(card) + " with a " + unitTypeName(UnitType(t)) + " on " + planetName(planet) + ".",
                         sys);
            }
        }
    }
    panopticonCheck();
    monCheckpoint();  // (0.82.1) The Pelagion's space combats, a station that lost its monument
}

// ------------------------------------------------------------------ passing, activating
// Flaah Orbitals (Arborec): "When you pass, place 1 infantry on a planet in or adjacent to this system other than Mecatol Rex.
// If that planet contains another player's units, place that unit into coexistence."
void Rules::monPassed(int p) {
    int sys = -1;
    if (!monumentsOn() || !monAbilityAt(G, p, "arborec", &sys) || unitsForPlacement(G, p, UnitType::Infantry) <= 0) return;
    const int card = monumentIndex("arborec");
    std::vector<Option> opts;
    for (int s : inOrNext(G, sys)) {
        if (s == G.mecatol()) continue;
        for (int16_t x : G.systems[size_t(s)].planets) {
            if (G.planets[size_t(x)].destroyed || isDMZ(G, x) || isStation(G, x)) continue;
            bool others = false;
            for (int q = 0; q < kSides; ++q)
                if (q != p && (q < G.numPlayers || q == kNeutral) && !G.planets[size_t(x)].units[size_t(q)].empty()) others = true;
            const int owner = G.planets[size_t(x)].owner;
            const std::string why = owner == p ? "Your planet." : others ? "Into coexistence with the units there." : owner >= 0 ? "You take it from " + pn(owner) + "." : "You take control of it.";
            opts.push_back({x, planetName(x), why, true, s});
        }
    }
    if (opts.empty()) return;
    // (fixes 43, the rules pass) LRR 1.8: "Abilities on components that remain in play are mandatory unless those abilities use
    // the word 'may'" - the card says "place 1 infantry", so there is no "No infantry" (older games keep it)
    if (G.fixes < 43) opts.push_back({-1, "No infantry", ""});
    const int x = opts.size() == 1 ? opts[0].id : mpick(p, card, kMonAskArborec, monName(card) + ": place 1 infantry on which planet?", opts, kMonuments[card].text, false, true);
    if (x < 0) return;
    PlanetState& ps = G.planets[size_t(x)];
    bool others = false;
    for (int q = 0; q < kSides; ++q)
        if (q != p && (q < G.numPlayers || q == kNeutral) && !ps.units[size_t(q)].empty()) others = true;
    addUnits(p, UnitType::Infantry, 1, ps.system, x);
    monEvent(p, card, pn(p) + " places an infantry on " + planetName(x) + " (" + monName(card) + ").", ps.system);
    if (ps.owner == p) return;
    if (others) {
        startCoexisting(p, x);  // (the planet stays with its controller - TE rulebook "Coexist")
    } else if (ps.units[size_t(p)].groundForces() > 0) {
        setControl(x, p);  // LRR 25.5a: "The player that placed units on the planet gains control of that planet."
    }
}

// Revenance Circuit (Creuss): "After a player activates this system: You may produce 1 hit against players' ships in each
// system adjacent to this planet that contains a wormhole." Owner 2026-11-22 (RULES-CHECK M5): every player's ships - the
// Creuss' own too - 1 hit against each player's ships in each system the Creuss chooses (any, all or none). The planet is
// adjacent to its own system (LRR 6.3), so that system counts too when it has a wormhole.
void Rules::monActivated(int p, int sysAct) {
    if (!monumentsOn()) return;
    monActivatedTE(p, sysAct);  // (0.82.1) Epiphany, Epiphany Hollow
    monActivatedDS(p, sysAct);  // (0.82.2) Jatta's Palace, Nightfall Fortress
    monActivatedDS2(p, sysAct);  // (0.82.3) Flotilla Cyclotron
    monActivatedDS3(p, sysAct);  // (0.82.4) Vault of New Phrad
    for (int q : holdersOf(G, "creuss")) {
        int sys = -1;
        if (!monAbilityAt(G, q, "creuss", &sys) || sys != sysAct) continue;
        const int card = monumentIndex("creuss");
        std::vector<Option> opts;
        std::vector<int> near{sys};
        for (int16_t a : G.systems[size_t(sys)].adj) near.push_back(a);
        for (int a : near) {
            if (!wormholesOf(G, a)) continue;
            std::string who;
            for (int r = 0; r < G.numPlayers; ++r)
                if (r != q && hasShips(G, r, a)) who += (who.empty() ? "" : ", ") + pn(r);
            if (hasShips(G, kNeutral, a)) who += (who.empty() ? "" : ", ") + std::string("neutral ships");
            if (hasShips(G, q, a)) who += (who.empty() ? "" : ", ") + std::string("YOUR OWN ships");
            if (!who.empty()) opts.push_back({a, sysName(a), "1 hit against each: " + who, true, a});
        }
        if (opts.empty()) continue;
        curFor_ = p;
        const std::vector<int> picked = mpickMany(q, card, kMonAskCreuss, monName(card) + ": hit the ships in which wormhole systems?", opts, 0,
                                                  int(opts.size()), kMonuments[card].text, true);
        curFor_ = -1;
        if (picked.empty()) continue;
        monEvent(q, card, pn(q) + "'s " + monName(card) + " strikes through the wormholes.", sys);
        for (int s : picked)
            for (int r = 0; r < kSides; ++r) {
                if ((r >= G.numPlayers && r != kNeutral) || !hasShips(G, r, s)) continue;  // (the Creuss' own ships too)
                int mask = 0;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (isShip(UnitType(t))) mask |= 1 << t;
                assignHits(r, s, -1, 1, mask, monName(card), q);
            }
    }
}

// ------------------------------------------------------------------ status phase, agenda phase
// Spire of Ixth (Mahact): "At the start of the status phase, the Mahact player may place 1 command token in this system into
// their fleet pool if it is their own token, or if it does not already have 1 of that player's tokens." (another player's token
// goes there only for the Mahact - their Edict; for a Nekro with the card only their own)
void Rules::monStatusStart() {
    if (!monumentsOn()) return;
    for (int p : holdersOf(G, "mahact")) {
        int sys = -1;
        if (!monAbilityAt(G, p, "mahact", &sys)) continue;
        const int card = monumentIndex("mahact");
        Player& pl = G.players[size_t(p)];
        const bool mahact = fac(p, "mahact");
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q) {
            if (!G.systems[size_t(sys)].hasToken(q)) continue;
            if (q == p) opts.push_back({q, "Your own token", "Your fleet pool gains a token."});
            else if (mahact && !((pl.edict >> q) & 1)) opts.push_back({q, pn(q) + "'s token", "Into your fleet pool (Edict)."});
        }
        if (opts.empty()) continue;
        opts.push_back({-1, "None", ""});
        const int q = mpick(p, card, kMonAskMahact, monName(card) + ": a command token from " + sysName(sys) + " into your fleet pool?", opts,
                            kMonuments[card].text);
        if (q < 0) continue;
        G.systems[size_t(sys)].tokens = uint16_t(G.systems[size_t(sys)].tokens & ~(1u << q));
        if (q == p) pl.fleet = int8_t(pl.fleet + 1);
        else pl.edict = uint8_t(pl.edict | (1u << q));
        monEvent(p, card,
                 pn(p) + " puts " + (q == p ? std::string("their own") : pn(q) + "'s") + " command token from " + sysName(sys) + " into their fleet pool.",
                 sys);
    }
}

// Imperial Vault (Winnu): "At the start of the agenda phase, if your monument is on the game board, gain any number of trade
// goods on this card. Otherwise, spend any number of resources to place 1 trade good from the supply on this card for every 3
// resources spent."
void Rules::monAgendaStart() {
    if (!monumentsOn()) return;
    for (int p : holdersOf(G, "winnu")) {
        const int card = monumentIndex("winnu");
        Player& pl = G.players[size_t(p)];
        int sys = -1;
        if (monumentSystem(G, p) >= 0 && monAbilityAt(G, p, "winnu", &sys)) {
            if (pl.monTG <= 0) continue;
            std::vector<Option> opts;
            for (int k = pl.monTG; k >= 0; --k) opts.push_back({k, k ? std::to_string(k) + " trade good" + (k > 1 ? "s" : "") : "None", ""});
            const int k = mpick(p, card, kMonAskWinnuTake, monName(card) + ": take how many trade goods off the card?", opts,
                                std::to_string(pl.monTG) + " on it. " + kMonuments[card].text);
            if (k > 0) {
                pl.monTG = int8_t(pl.monTG - k);
                gainTG(p, k);
                monEvent(p, card, pn(p) + " takes " + std::to_string(k) + " trade good(s) from " + monName(card) + ".", sys);
            }
        } else {
            const int most = spendable(G, p, false) / 3;
            if (most <= 0) continue;
            std::vector<Option> opts;
            for (int k = 0; k <= std::min(most, 10); ++k)
                opts.push_back({k, k ? std::to_string(k) + " (" + std::to_string(3 * k) + " resources)" : "None", ""});
            const int k = mpick(p, card, kMonAskWinnuStore, monName(card) + ": put trade goods on the card?", opts,
                                std::to_string(pl.monTG) + " on it now. " + kMonuments[card].text);
            if (k > 0 && pay(p, 3 * k, false, monName(card), true)) {
                pl.monTG = int8_t(pl.monTG + k);
                monEvent(p, card, pn(p) + " puts " + std::to_string(k) + " trade good(s) on " + monName(card) + " (" + std::to_string(pl.monTG) + " there).");
            }
        }
    }
}

// Calm Seas Sanatorium (Jol-Nar): "After an agenda outcome you voted for or predicted is resolved: Place up to 2 infantry on a
// planet you control in this system." Qanoj Shield Array (Xxcha): "When an agenda outcome you voted for or predicted is
// resolved, you may exhaust this card to place a structure on a planet you control in this system."
// "voted for" = at least 1 vote cast for it; "predicted" = any prediction on it (allPredictions: riders, Council Black Site,
// Galactic Threat, Odlynn Myrr, Discordant Stars' riders and predictions)
void Rules::monAgendaResolved(int outcome, const std::vector<int>& voted, const std::vector<int>& counts, const std::vector<int>* guessed) {
    if (!monumentsOn()) return;
    auto votedOrPredicted = [&](int q) {
        if (q < int(voted.size()) && q < int(counts.size()) && voted[size_t(q)] == outcome && counts[size_t(q)] > 0) return true;
        if (guessed && q >= 0 && q < kMaxPlayers)
            for (int o : guessed[q])
                if (o == outcome) return true;
        return false;
    };
    for (int q : holdersOf(G, "xxcha")) {
        int sys = -1;
        if (!votedOrPredicted(q) || !monCardReady(G, q) || !monAbilityAt(G, q, "xxcha", &sys)) continue;
        bool spot = false;
        for (int16_t x : G.systems[size_t(sys)].planets) {
            if (G.planets[size_t(x)].owner != q || isDMZ(G, x)) continue;
            const Force& f = G.planets[size_t(x)].units[size_t(q)];
            if ((unitsForPlacement(G, q, UnitType::PDS) > 0 && f.count(UnitType::PDS) < 2) ||
                (unitsForPlacement(G, q, UnitType::SpaceDock) > 0 && !f.count(UnitType::SpaceDock)))
                spot = true;
        }
        const int card = monumentIndex("xxcha");
        if (!spot || !myes(q, card, kMonAskXxcha, monName(card) + ": exhaust it to place a structure in " + sysName(sys) + "?", kMonuments[card].text))
            continue;
        monExhaust(q, card);
        monEvent(q, card, pn(q) + " uses " + monName(card) + ".", sys);
        placeStructure(q, sys, true, true, false, monName(card) + ": place which structure where?");
    }
    for (int q : holdersOf(G, "jolnar")) {
        int sys = -1;
        if (!votedOrPredicted(q) || !monAbilityAt(G, q, "jolnar", &sys)) continue;
        const int have = unitsForPlacement(G, q, UnitType::Infantry);
        if (have <= 0) continue;
        std::vector<Option> opts;
        for (int16_t x : G.systems[size_t(sys)].planets)
            if (G.planets[size_t(x)].owner == q && !isDMZ(G, x))
                for (int k = std::min(2, have); k >= 1; --k)
                    opts.push_back({x * 4 + k, std::to_string(k) + " on " + planetName(x), "", true, sys});
        if (opts.empty()) continue;
        opts.push_back({-1, "None", ""});
        const int card = monumentIndex("jolnar");
        const int c = mpick(q, card, kMonAskJolnar, monName(card) + ": place up to 2 infantry?", opts, kMonuments[card].text);
        if (c < 0) continue;
        addUnits(q, UnitType::Infantry, c % 4, sys, c / 4);
        monEvent(q, card, pn(q) + " places " + std::to_string(c % 4) + " infantry on " + planetName(c / 4) + " (" + monName(card) + ").", sys);
    }
    // (0.83.0 Blue Reverie) Deepmantle Interface (Atokera): "During the agenda phase, when an outcome you voted for or predicted is
    // resolved, produce up to 1 unit in this system."
    for (int q : holdersOf(G, "atokera")) {
        int sys = -1;
        if (G.phase != Phase::Agenda || !votedOrPredicted(q) || !monAbilityAt(G, q, "atokera", &sys)) continue;
        if (spendable(G, q, false) < 1) continue;
        const int card = monumentIndex("atokera");
        if (!myes(q, card, kMonAskAtokera, monName(card) + ": produce up to 1 unit in " + sysName(sys) + "?", kMonuments[card].text)) continue;
        monEvent(q, card, pn(q) + " uses " + monName(card) + " to produce in " + sysName(sys) + ".", sys);
        productionCore(q, sys, 1, false, false);
    }
    monCheckpoint();  // (0.82.1) a Pelagion the outcome removed: its space combats now
}

// Keleres: "After a law enters play, you may add a command token from your reinforcements to this card."
void Rules::monLawEntered() {
    if (!monumentsOn()) return;
    for (int p : holdersOf(G, "keleres")) {
        if (reinforcementTokens(p) <= 0) continue;
        const int card = monumentIndex("keleres");
        if (!myes(p, card, kMonAskKeleresLaw, monName(card) + ": add a command token from your reinforcements to the card?",
                  std::to_string(G.players[size_t(p)].monTokens) + " on it now. " + kMonuments[card].text))
            continue;
        G.players[size_t(p)].monTokens = int8_t(G.players[size_t(p)].monTokens + 1);
        monEvent(p, card, pn(p) + " adds a command token to " + monName(card) + " (" + std::to_string(G.players[size_t(p)].monTokens) + " there).");
    }
}

// ... "When you would place a command token in this or an adjacent system, you may place that token from this card instead."
bool Rules::monCommandToken(int p, int sys, bool activation) {
    int at = -1;
    if (!monumentsOn() || G.players[size_t(p)].monTokens <= 0 || !monAbilityAt(G, p, "keleres", &at) || !inList(inOrNext(G, at), sys)) return false;
    const int card = monumentIndex("keleres");
    if (!myes(p, card, kMonAskKeleresToken, monName(card) + ": place the command token in " + sysName(sys) + " from the card?",
              std::string(activation ? "Your tactic pool keeps its token. " : "") + std::to_string(G.players[size_t(p)].monTokens) + " on the card."))
        return false;
    G.players[size_t(p)].monTokens = int8_t(G.players[size_t(p)].monTokens - 1);
    monEvent(p, card, pn(p) + " places a command token from " + monName(card) + " in " + sysName(sys) + ".", sys);
    return true;
}

// Palatine Obelisk (Letnev): "When you exhaust this planet to cast votes, you may cast an additional number of votes equal to
// the number of your non-fighter ships in this system." Glory Furnace (Muaat): "When you exhaust this planet to cast votes, for
// each war sun or flagship you control in this system, and for each supernova adjacent to this system, choose: cast 3
// additional votes, or produce a hit against another player's ships in this or an adjacent system."
int Rules::monVotes(int p, const std::vector<int>& spent) {
    if (!monumentsOn()) return 0;
    int extra = 0, sys = -1, planet = -1;
    if (monAbilityAt(G, p, "letnev", &sys, &planet) && planet >= 0 && inList(spent, planet)) {
        const int k = G.systems[size_t(sys)].space[size_t(p)].nonFighterShips();
        if (k > 0) {
            extra += k;
            monEvent(p, monumentIndex("letnev"), pn(p) + " casts " + std::to_string(k) + (k == 1 ? " more vote (" : " more votes (") + monName(monumentIndex("letnev")) + ").", sys,
                     2, k);  // (owner M18: shown large to a person - "make sure the human player knows he's getting it")
        }
    }
    if (monAbilityAt(G, p, "muaat", &sys, &planet) && planet >= 0 && inList(spent, planet)) {
        const int card = monumentIndex("muaat");
        const Force& f = G.systems[size_t(sys)].space[size_t(p)];
        int n = f.count(UnitType::WarSun) + f.count(UnitType::Flagship) + f.count(UnitType::Flagship2);
        for (int16_t a : G.systems[size_t(sys)].adj)
            if (kSystems[G.systems[size_t(a)].def].anomalies & kSupernova) ++n;
        for (int i = 0; i < n; ++i) {
            std::vector<Option> opts{{0, "3 more votes", ""}};
            for (int s : inOrNext(G, sys))
                for (int q = 0; q < kSides; ++q)
                    if (q != p && (q < G.numPlayers || q == kNeutral) && hasShips(G, q, s))
                        opts.push_back({s * 16 + q, "A hit: " + (q == kNeutral ? std::string("neutral") : pn(q)) + " ships in " + sysName(s), "", true, s});
            const int c = opts.size() == 1 ? 0
                                           : mpick(p, card, kMonAskMuaat,
                                                   monName(card) + ": choice " + std::to_string(i + 1) + " of " + std::to_string(n) + " - votes or a hit?",
                                                   opts, kMonuments[card].text);
            if (c <= 0) {
                extra += 3;
                monEvent(p, card, pn(p) + " casts 3 more votes (" + monName(card) + ").", sys);
            } else {
                const int s = c / 16, q = c % 16;
                int mask = 0;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (isShip(UnitType(t))) mask |= 1 << t;
                monEvent(p, card, pn(p) + "'s " + monName(card) + " fires on " + (q == kNeutral ? std::string("neutral") : pn(q)) + " ships in " + sysName(s) + ".", s);
                assignHits(q, s, -1, 1, mask, monName(card), p);
            }
        }
    }
    return extra;
}

// (owner M18) what exhausting the monument's planet adds, said in the vote questions so a person sees it before choosing
std::string Rules::monVoteHint(int p) const {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers) return "";
    std::string out;
    int sys = -1, planet = -1;
    if (monAbilityAt(G, p, "letnev", &sys, &planet) && planet >= 0 && !G.planets[size_t(planet)].exhausted) {
        const int k = G.systems[size_t(sys)].space[size_t(p)].nonFighterShips();
        if (k > 0)
            out += " " + monName(monumentIndex("letnev")) + ": exhaust " + planetName(planet) + " for " + std::to_string(k) +
                   (k == 1 ? " more vote" : " more votes") + " (your non-fighter ships there).";
    }
    if (monAbilityAt(G, p, "muaat", &sys, &planet) && planet >= 0 && !G.planets[size_t(planet)].exhausted) {
        const Force& f = G.systems[size_t(sys)].space[size_t(p)];
        int n = f.count(UnitType::WarSun) + f.count(UnitType::Flagship) + f.count(UnitType::Flagship2);
        for (int16_t a : G.systems[size_t(sys)].adj)
            if (kSystems[G.systems[size_t(a)].def].anomalies & kSupernova) ++n;
        if (n > 0)
            out += " " + monName(monumentIndex("muaat")) + ": exhaust " + planetName(planet) + " for " + std::to_string(n) +
                   (n == 1 ? " choice" : " choices") + " - 3 more votes or a hit each.";
    }
    return out;
}

// Sector KVD-14 (Yssaril): "When another player plays an action card: If that player controls 1 or more units in this or an
// adjacent system, you may gain 2 trade goods." (always taken)
void Rules::monActionCardPlayed(int p) {
    if (!monumentsOn()) return;
    for (int q : holdersOf(G, "yssaril")) {
        int sys = -1;
        if (q == p || !monAbilityAt(G, q, "yssaril", &sys)) continue;
        bool there = false;
        for (int s : inOrNext(G, sys)) there = there || hasUnits(G, p, s);
        if (!there) continue;
        gainTG(q, 2);
        monEvent(q, monumentIndex("yssaril"), pn(q) + " gains 2 trade goods (" + monName(monumentIndex("yssaril")) + "): " + pn(p) + " played an action card.", sys,
                 1, 2);  // (owner M18: shown large to a person)
    }
}

// The Cenotaph (Sol): "When you place 1 or more infantry on a planet, exhaust this card to place any of those infantry on this
// planet instead." (production, abilities, Infantry II - every placement; commits: monSolCommit)
int Rules::monInfantryPlaced(int p, int planet, int k) {
    int sys = -1, here = -1;
    if (k <= 0 || monSolBusy_ || G.phase == Phase::Setup || p < 0 || p >= G.numPlayers || !monCardReady(G, p) ||
        !monAbilityAt(G, p, "sol", &sys, &here) || here < 0 || here == planet || G.planets[size_t(here)].owner != p)
        return 0;
    const int card = monumentIndex("sol");
    std::vector<Option> opts;
    for (int i = k; i >= 0; --i)
        opts.push_back({i, i ? std::to_string(i) + " to " + planetName(here) : "None", ""});
    curPlanet_ = planet;
    const int n = mpick(p, card, kMonAskSol,
                        std::to_string(k) + " infantry placed on " + planetName(planet) + ": exhaust " + monName(card) + " to place some on " +
                            planetName(here) + " instead?",
                        opts, kMonuments[card].text);
    curPlanet_ = -1;
    if (n <= 0) return 0;
    monExhaust(p, card);
    ++monSolBusy_;
    addUnits(p, UnitType::Infantry, n, sys, here);
    --monSolBusy_;
    monEvent(p, card, pn(p) + " places " + std::to_string(n) + " infantry on " + planetName(here) + " instead (" + monName(card) + ").", sys);
    return n;
}

// ... "place" also covers infantry committed to a planet (the Monuments+ notes: "'place' triggers off production, commit,
// Infantry II, etc"): they go to the Cenotaph's planet instead - off the planet being invaded
int Rules::monSolCommit(int p, int planet) {
    int sys = -1, here = -1;
    if (!monumentsOn() || !monCardReady(G, p) || !monAbilityAt(G, p, "sol", &sys, &here) || here < 0 || here == planet ||
        G.planets[size_t(here)].owner != p)
        return 0;
    const int k = G.planets[size_t(planet)].units[size_t(p)].count(UnitType::Infantry);
    if (k <= 0) return 0;
    const int card = monumentIndex("sol");
    std::vector<Option> opts;
    for (int i = 0; i <= k; ++i) opts.push_back({i, i ? std::to_string(i) + " to " + planetName(here) : "None - they stay", ""});
    const int n = mpick(p, card, kMonAskSolCommit,
                        std::to_string(k) + " infantry committed to " + planetName(planet) + ": exhaust " + monName(card) + " to place some on " +
                            planetName(here) + " instead?",
                        opts, kMonuments[card].text);
    if (n <= 0) return 0;
    monExhaust(p, card);
    G.planets[size_t(planet)].units[size_t(p)].remove(UnitType::Infantry, n, true);
    G.planets[size_t(here)].units[size_t(p)].add(UnitType::Infantry, n);
    monEvent(p, card, pn(p) + " places " + std::to_string(n) + " infantry on " + planetName(here) + " instead (" + monName(card) + ").", sys);
    return n;
}

// She Consumes Worlds (Vuil'raith): "DEPLOY: When you gain control of a planet that contains 1 or more structures, you may place
// this unit on that planet and capture each other structure on that planet as if it were a non-fighter ship." (DEPLOY only
// from the reinforcements - Monuments+ FAQ)
// Timing: setControl asks it the moment the planet changes hands - after the invasion's ground combat is won (or the landing on a
// planet with only structures), or when an ability / agenda gives the planet - BEFORE the old owner's structures there are
// destroyed (LRR 49.5a), so it captures them instead. The card's placement icons don't apply (the DEPLOY text places it).
bool Rules::monDeploy(int p, int planet) {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers || !monCardHas(G, p, "cabal") || unitsAvailable(G, p, UnitType::Monument) <= 0) return false;
    PlanetState& ps = G.planets[size_t(planet)];
    if (ps.destroyed || isDMZ(G, planet)) return false;
    int structures = 0;
    for (int q = 0; q < kSides; ++q)
        if (q != p && (q < G.numPlayers || q == kNeutral)) structures += ps.units[size_t(q)].structures();
    if (!structures) return false;
    const int card = monumentIndex("cabal");
    curFor_ = ps.owner;
    const bool go = myes(p, card, kMonAskCabal,
                         monName(card) + ": DEPLOY on " + planetName(planet) + " and capture the " + std::to_string(structures) + " structure(s) there?",
                         kMonuments[card].text);
    curFor_ = -1;
    if (!go) return false;
    for (int q = 0; q < kSides; ++q) {
        if (q == p || (q >= G.numPlayers && q != kNeutral)) continue;
        for (UnitType t : {UnitType::PDS, UnitType::SpaceDock, UnitType::Monument}) {
            const int k = ps.units[size_t(q)].count(t);
            if (!k) continue;
            ps.units[size_t(q)].remove(t, k, true);
            Event e;
            e.kind = EventKind::Destroyed;
            e.player = int8_t(q);
            e.other = int8_t(p);
            e.system = int16_t(ps.system);
            e.planet = int16_t(planet);
            e.unit = int8_t(t);
            e.count = int8_t(k);
            e.text = (q == kNeutral ? std::string("Neutral") : pn(q)) + " loses " + std::to_string(k) + " " + unitTypeName(t, k > 1) + " (captured).";
            E.emit(e);
            // (owner 2026-11-22, M15: neutral structures are captured too - kept as the Vuil'raith keep captured neutral ships)
            monCaptureStructures_ = true;
            capture(p, q, t, k, monName(card).c_str());
            monCaptureStructures_ = false;
        }
    }
    addUnits(p, UnitType::Monument, 1, ps.system, planet);
    monEvent(p, card, pn(p) + " deploys " + monName(card) + " on " + planetName(planet) + ".", ps.system);
    return true;
}

// Valefar Assimilator "M" (Nekro): "When you destroy another player's structure, you may place or move an "M" or "Z" assimilator
// token onto that player's monument card. While any of those tokens are on a monument card, this card gains that card's
// attributes, abilities, and ability text."
void Rules::monStructureDestroyed(int byPlayer, int owner) {
    if (!monumentsOn() || byPlayer < 0 || byPlayer >= G.numPlayers || owner < 0 || owner >= G.numPlayers || owner == byPlayer) return;
    if (!monCardHas(G, byPlayer, "nekro")) return;
    const int target = playerMonument(G, owner);
    if (target < 0 || target == monumentIndex("nekro")) return;
    Player& pl = G.players[size_t(byPlayer)];
    if (pl.nekroMon[0] == target || pl.nekroMon[1] == target) return;  // (a token on it already)
    const int card = monumentIndex("nekro");
    // (fixes 43, the rules pass) the "Z" token is the Valefar Assimilator Z breakthrough's (the Monuments+ notes: "stacks like the
    // Breakthrough (reusing that token in case you didn't want a flagship)"): only with that breakthrough, and only while one of
    // its 3 tokens is free (or this one is already on a monument card) - older games offer it always
    const bool zToken = G.fixes < 43 || pl.nekroMon[1] >= 0 || (breakthroughIs(G, byPlayer, "nekro") && pl.nekroZ.size() < 3);
    std::vector<Option> opts;
    for (int i = 0; i < 2; ++i) {
        if (i == 1 && !zToken) continue;
        const std::string tok = i ? "\"Z\"" : "\"M\"";
        opts.push_back({i, (pl.nekroMon[i] < 0 ? "Place " : "Move ") + tok + " onto " + monName(target),
                        pl.nekroMon[i] < 0 ? "" : "It leaves " + monName(pl.nekroMon[i]) + "."});
    }
    opts.push_back({-1, "No token", ""});
    curFor_ = owner;
    const int i = mpick(byPlayer, card, kMonAskNekro, monName(card) + ": an assimilator token onto " + pn(owner) + "'s monument card?", opts,
                        std::string(kMonuments[target].text));
    curFor_ = -1;
    if (i < 0) return;
    pl.nekroMon[i] = int8_t(target);
    monEvent(byPlayer, card, pn(byPlayer) + "'s " + monName(card) + " gains the text of " + monName(target) + ".");
}

// G'hom Firefly Beacon (N'orr): "When you place your monument, produce up to a total of 3 infantry or fighters in this or
// adjacent systems without spending resources."
void Rules::monPlaced(int p) {
    int sys = -1;
    if (!monumentsOn() || !monAbilityAt(G, p, "sardakk", &sys)) return;
    const int card = monumentIndex("sardakk");
    monEvent(p, card, pn(p) + "'s " + monName(card) + " calls: up to 3 infantry or fighters.", sys);
    for (int i = 0; i < 3; ++i) {
        std::vector<Option> opts;
        for (int s : inOrNext(G, sys)) {
            if (unitsForPlacement(G, p, UnitType::Infantry) > 0)
                for (int16_t x : G.systems[size_t(s)].planets)
                    if (G.planets[size_t(x)].owner == p && !isDMZ(G, x) && !G.planets[size_t(x)].destroyed)
                        opts.push_back({x, "Infantry on " + planetName(x), "", true, s});
            if (unitsForPlacement(G, p, UnitType::Fighter) > 0 && !otherShipsIn(G, p, s)) {
                Force more = G.systems[size_t(s)].space[size_t(p)];
                more.add(UnitType::Fighter, 1);
                if (overflowOf(G, p, s, more) <= 0) opts.push_back({50000 + s, "Fighter in " + sysName(s), "", true, s});
            }
        }
        if (opts.empty()) break;
        opts.push_back({-1, "Done", ""});
        const int c = mpick(p, card, kMonAskNorr, monName(card) + ": produce unit " + std::to_string(i + 1) + " of 3?", opts, kMonuments[card].text);
        if (c < 0) break;
        if (c >= 50000) {
            addUnits(p, UnitType::Fighter, 1, c - 50000, -1);
            say(p, pn(p) + " produces a fighter in " + sysName(c - 50000) + " (" + monName(card) + ").", c - 50000);
        } else {
            addUnits(p, UnitType::Infantry, 1, G.planets[size_t(c)].system, c);
            say(p, pn(p) + " produces an infantry on " + planetName(c) + " (" + monName(card) + ").", G.planets[size_t(c)].system);
        }
    }
}

// The Panopticon (Empyrean): "If this unit is blockaded, destroy it." (checked before every question)
void Rules::panopticonCheck() {
    if (!monumentsOn()) return;
    for (int p = 0; p < G.numPlayers; ++p) {
        int sys = -1, planet = -1;
        if (!monAbilityAt(G, p, "empyrean", &sys, &planet) || planet >= 0) continue;
        if (!otherShipsIn(G, p, sys) || hasShips(G, p, sys)) continue;
        int by = -1;
        for (int q = 0; q < G.numPlayers && by < 0; ++q)
            if (q != p && hasShips(G, q, sys)) by = q;
        monEvent(p, monumentIndex("empyrean"), pn(p) + "'s " + monName(monumentIndex("empyrean")) + " is blockaded in " + sysName(sys) + " - destroyed.", sys);
        destroyUnits(p, UnitType::Monument, 1, sys, -1, by, "blockaded");
    }
}

// Nefishh Projector (Naalu): "Infantry that you commit to planets in this and adjacent systems may be committed into
// coexistence, if able." Owner 2026-11-22 (M7): only when infantry alone were committed - with a mech they fight (the landing
// screen warns a person before they send one: nefishhCovers). Neutral ground forces too (q = kNeutral, nobody else's there).
bool Rules::monCoexist(int p, int q, int planet) {
    int sys = -1;
    if (!monumentsOn() || !monAbilityAt(G, p, "naalu", &sys)) return false;
    const PlanetState& ps = G.planets[size_t(planet)];
    if (!inList(inOrNext(G, sys), ps.system) || ((ps.coexist >> p) & 1) || ((ps.coexist >> q) & 1)) return false;
    const Force& f = ps.units[size_t(p)];
    if (!f.count(UnitType::Infantry) || f.count(UnitType::Mech)) return false;
    const int card = monumentIndex("naalu");
    curFor_ = q;
    curPlanet_ = planet;
    const bool co = myes(p, card, kMonAskNaalu, monName(card) + ": commit your infantry on " + planetName(planet) + " into coexistence?",
                         "No ground combat with " + (q == kNeutral ? std::string("the neutral units") : pn(q)) + ". Coexisting units don't control the planet. " + kMonuments[card].text);
    curFor_ = -1;
    curPlanet_ = -1;
    if (!co) return false;
    startCoexisting(p, planet);
    monEvent(p, card, pn(p) + "'s infantry coexist on " + planetName(planet) + " (" + monName(card) + ").", ps.system);
    return true;
}

// Mowshir Freeport (Hacan): "Outside of combat, players may trade their units in the space area of this and adjacent systems as
// part of a transaction. A player who gained units this way then replaces them and moves them to a system that contains their
// ships and 1 of their command tokens, if able; otherwise, remove those units."
void Rules::freeportTrade(int from, int to, const std::vector<int>& units) {
    const int card = monumentIndex("hacan");
    int got[kUnitTypes] = {};
    for (int u : units) {
        const int sys = u / 16, t = u % 16;
        if (sys < 0 || sys >= int(G.systems.size()) || t < 0 || t >= kUnitTypes) continue;
        Force& f = G.systems[size_t(sys)].space[size_t(from)];
        if (!f.count(UnitType(t))) continue;
        f.remove(UnitType(t), 1, true);  // (back to the giver's reinforcements)
        ++got[t];
        if (isShip(UnitType(t))) fleetDirty_ = true;
    }
    std::vector<Option> opts;
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (hasShips(G, to, int(s)) && G.systems[s].hasToken(to) && !otherShipsIn(G, to, int(s)))
            opts.push_back({int(s), sysName(int(s)), "Your ships and one of your command tokens are there.", true, int(s)});
    int dest = -1;
    if (opts.size() == 1) dest = opts[0].id;
    else if (opts.size() > 1)
        dest = mpick(to, card, kMonAskHacan, monName(card) + ": where do the units you gained go?", opts, kMonuments[card].text, true);
    std::string what;
    int placed[kUnitTypes] = {};
    for (int t = 0; t < kUnitTypes; ++t) {
        if (!got[t]) continue;
        const int k = dest >= 0 ? std::min(got[t], unitsForPlacement(G, to, UnitType(t))) : 0;
        if (k > 0) {
            const int before = G.systems[size_t(dest)].space[size_t(to)].count(UnitType(t));
            addUnits(to, UnitType(t), k, dest, -1);
            placed[t] = G.systems[size_t(dest)].space[size_t(to)].count(UnitType(t)) - before;
        }
        what += (what.empty() ? "" : ", ") + std::to_string(got[t]) + " " + unitTypeName(UnitType(t), got[t] > 1);
    }
    // (what doesn't fit the ships' capacity there is removed)
    for (UnitType t : {UnitType::Infantry, UnitType::Mech, UnitType::Fighter})
        while (dest >= 0 && placed[int(t)] > 0 && capacityOverflow(G, to, dest) > 0) {
            G.systems[size_t(dest)].space[size_t(to)].remove(t, 1, true);
            --placed[int(t)];
        }
    if (dest >= 0) {
        monEvent(to, card, pn(to) + " replaces the units from " + pn(from) + " (" + what + ") with their own in " + sysName(dest) + ".", dest);
        enforceFleetLimit(to);
    } else {
        monEvent(to, card, pn(to) + " has no system with their ships and a command token: the units from " + pn(from) + " (" + what + ") are removed.");
    }
}

}  // namespace ti
