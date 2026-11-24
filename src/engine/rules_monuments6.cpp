// Monuments+ (0.82.3) - Discordant Stars, batch 2 (cards in tools/gen_monuments.py, the owner's v3.1 card files; set "DS2", new
// games only - fixes 41). Rulings: the Living Rules first, then the Monuments+ rulebook / the Discordant Stars rules, then AsyncTI4;
// the readings that needed a choice are RULES-CHECK 0.82.3 M51-M62.
//
//   Gravelord's Keep (Myko-Mentori) "Up to 3 fighters in this and each adjacent system do not count against your ships' capacity."
//                                (query keepFighterSlots, through dockFighterSlots: 3 in each of those systems - AsyncTI4 the same)
//                                "After a player uses an omen die, you may produce 1 fighter in this system without spending
//                                resources." (omen28 -> monOmenUsed; the fighter at the next safe point - the hits being assigned,
//                                the end of the tactical action or turn: keepResolve)
//   Flotilla Cyclotron (Dih-Mohn) "After you activate a system, you may exhaust this card to apply +1 to the move value of ships in
//                                this system until the end of this tactical action." ("this system" = the Cyclotron's; fx.monCyclotron
//                                read by query moveValue)
//   Bounty Brokerage (Vaden)     "After another player with ships in this or an adjacent system replenishes commodities, if that player
//                                has control tokens on the Vaden player's faction sheet, you may take one of their commodities."
//                                (Rules::replenished -> monReplenished; the commodity becomes a trade good - LRR 21.5)
//   Corsairs' Cove (Florzen)     "When you produce fighters, you may place that many plastic fighter units on this card, instead."
//                                (the Monuments+ rulebook: this part works off the board too; productionCore -> coveTake, and
//                                checkProducePlan lets fighters beyond the capacity be produced for the card)
//                                "At the start of your turn, you may produce fighter units on this card in this system without
//                                spending resources." (monTurnStartDS)
//   Mirrorforge (Mortheus)       ACTION: an adjacent system with another player's non-fighter ships; that player moves them in, ship by
//                                ship of their choice, until 4 resources' worth or all have come ("if able": not out of a Maw, not
//                                into a system they can't enter); then a space combat there (Mortheus the attacker, as AsyncTI4)
//   The Maw (Nivyn)              "Other players' ships cannot move out of or through this system." (query mawBlocks: shipReach,
//                                canPassThrough, retreats, Foresight) "Your ships may treat this system as adjacent to the Wound."
//                                (query mawLinks in routeNeighbors)
//   AI Conclave (Augurs)         "When you would explore another planet, you may explore this planet instead. If you do, you may gain
//                                trade goods equal to the number of your scored secret objectives." (explorePlanet -> conclaveExplore)
//   Dawnstar HQ (Tnelis)         "When you lose a ground combat on this planet, you may destroy each unit on this planet." (groundCombatRun
//                                -> dawnstar, before the winner's "after you win" effects; nobody is left to take the planet)
//                                "At the start of your turn you may destroy 1 other player's coexisting unit in this system."
//                                (monTurnStartDS: another player's unit on a planet there where the Tnelis have units too)
//   Aylor Raider Hoard (Vaylerian) "When a player spends trade goods during a space combat in or adjacent to this system, you may exhaust
//                                this card to draw 1 action card." (spaceCombatRun: hoardMark / hoardCheck - each player's trade goods
//                                spent since the last check, gains counted apart) "While another player has ships in or adjacent to
//                                this system, your commodity value is increased by 1." (query hoardCommodity in commodityValue)
//   Independence Tower (Free Systems) "After you resolve the primary or secondary ability of the "Leadership" strategy card, each other
//                                player with units in or adjacent to this system who reveals 1 of your promissory notes from their hand
//                                gains 1 command token." (Rules::leadership -> monAfterLeadership)
//   Hyperlane Relay (Celdauri)   "During the "Commit Ground Forces" step: You can commit ground forces on this planet to planets that
//                                contain your space docks, if this system does not contain 1 of your command tokens." (invasion ->
//                                relayCommit: the planets in the active system with their space docks - AsyncTI4 the same)
//   Debris Field (Zelian)        "This system is treated as an asteroid field. Units can move into this system as if it were a
//                                non-anomaly tile." (query debrisField: tileAnomalies / isAnomaly count it, tileAnomaliesMove and
//                                anomaliesFor - movement - don't; the map draws the reminder token)

#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
// a card's kMonuments index and its unit's kUnits index, looked up once
struct CardRef {
    int card = -1, unit = -1;
    explicit CardRef(const char* key) {
        card = monumentIndex(key);
        unit = card >= 0 ? unitByKey(kMonuments[card].unit) : -1;
    }
};
// q's monument card has that card's ability (their own card, or a Nekro's copy - as monCardHas, without the search)
bool holds(const Game& g, int q, const CardRef& r) {
    if (!(g.modules & kModMonuments) || r.card < 0 || q < 0 || q >= g.numPlayers) return false;
    const Player& pl = g.players[size_t(q)];
    if (pl.unit[int(UnitType::Monument)] < 0) return false;
    return pl.unit[int(UnitType::Monument)] == r.unit || pl.nekroMon[0] == r.card || pl.nekroMon[1] == r.card;
}
// q's monument stands in this system (on a planet or in the space area)
bool monumentIn(const Game& g, int q, int sys) {
    if (sys < 0 || sys >= int(g.systems.size())) return false;
    const SystemState& s = g.systems[size_t(sys)];
    if (s.space[size_t(q)].count(UnitType::Monument)) return true;
    for (int16_t x : s.planets)
        if (g.planets[size_t(x)].units[size_t(q)].count(UnitType::Monument)) return true;
    return false;
}
bool nextTo(const Game& g, int a, int b) {
    if (a == b) return true;
    for (int16_t x : g.systems[size_t(a)].adj)
        if (x == b) return true;
    return false;
}
std::vector<int> holdersOf6(const Game& g, const char* key) {
    std::vector<int> v;
    if (!(g.modules & kModMonuments)) return v;
    for (int q = 0; q < g.numPlayers; ++q)
        if (!g.players[size_t(q)].eliminated && monCardHas(g, q, key)) v.push_back(q);
    return v;
}
std::vector<int> inOrNext6(const Game& g, int sys) {
    std::vector<int> v;
    if (sys < 0) return v;
    v.push_back(sys);
    for (int16_t a : g.systems[size_t(sys)].adj)
        if (std::find(v.begin(), v.end(), int(a)) == v.end()) v.push_back(a);
    return v;
}
std::string countOf(int k, const char* one, const char* many) { return std::to_string(k) + " " + (k == 1 ? one : many); }
std::string forceText(const Force& f) {
    std::string s;
    for (int t = 0; t < kUnitTypes; ++t) {
        if (!f.n[t]) continue;
        if (!s.empty()) s += ", ";
        s += std::to_string(f.n[t]) + " " + unitTypeName(UnitType(t), f.n[t] > 1);
    }
    return s;
}
bool explorable(Trait t) { return t == Trait::Cultural || t == Trait::Hazardous || t == Trait::Industrial; }
}  // namespace

// ------------------------------------------------------------------ queries
int keepFighterSlots(const Game& g, int p, int sys) {
    static const CardRef keep("myko");
    if (sys < 0 || !holds(g, p, keep)) return 0;
    const int s = monumentSystem(g, p);
    return s >= 0 && nextTo(g, s, sys) ? 3 : 0;
}

bool mawBlocks(const Game& g, int p, int sys) {
    static const CardRef maw("nivyn");
    if (!(g.modules & kModMonuments) || maw.card < 0 || sys < 0) return false;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && holds(g, q, maw) && monumentIn(g, q, sys)) return true;
    return false;
}

void mawLinks(const Game& g, int p, int u, std::vector<int>& out) {
    static const CardRef maw("nivyn");
    if (g.dsWound < 0 || !holds(g, p, maw)) return;
    const int m = monumentSystem(g, p);
    if (m < 0 || m == g.dsWound) return;
    const int v = u == m ? g.dsWound : u == g.dsWound ? m : -1;
    if (v >= 0 && std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
}

bool debrisField(const Game& g, int s) {
    static const CardRef debris("zelian");
    if (!(g.modules & kModMonuments) || debris.card < 0) return false;
    for (int q = 0; q < g.numPlayers; ++q)
        if (holds(g, q, debris) && monumentIn(g, q, s)) return true;
    return false;
}

int hoardCommodity(const Game& g, int p) {
    static const CardRef hoard("vaylerian");
    if (!holds(g, p, hoard)) return 0;
    const int m = monumentSystem(g, p);
    if (m < 0) return 0;
    for (int s : inOrNext6(g, m))
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p && hasShips(g, q, s)) return 1;
    return 0;
}

bool coveHolder(const Game& g, int p) {
    static const CardRef cove("florzen");
    return holds(g, p, cove);  // (the card's first ability works with the monument off the board - the Monuments+ rulebook)
}

// ------------------------------------------------------------------ safe points: Queen's Rest, Gravelord's Keep
void Rules::monPendingResolve() {
    if (!queenPending_.empty() && !queenBusy_) queenResolve();
    if (keepPending_ > 0) keepResolve();
    if (!kyroPending_.empty()) kyroResolve();  // (0.82.4) Kyro Reliquary
}

void Rules::monOmenUsed() {
    if (monumentsOn() && !holdersOf6(G, "myko").empty()) ++keepPending_;
}

void Rules::keepResolve() {
    static bool busy = false;
    if (busy || keepPending_ <= 0) return;
    busy = true;
    const int n = std::min(keepPending_, 16);
    keepPending_ = 0;
    const int card = monumentIndex("myko");
    for (int i = 0; i < n; ++i)
        for (int q : holdersOf6(G, "myko")) {
            int s = -1;
            if (!monAbilityAt(G, q, "myko", &s) || unitsForPlacement(G, q, UnitType::Fighter) <= 0) continue;
            Force after = G.systems[size_t(s)].space[size_t(q)];
            after.add(UnitType::Fighter, 1);
            if (overflowOf(G, q, s, after) > capacityOverflow(G, q, s)) continue;  // (no room for it there)
            if (fightersFree(G, q) && fleetUse(G, q, s, after) > std::max(fleetLimit(G, q), fleetUseIn(G, q, s))) continue;
            if (!myes(q, card, kMonAskMyko, monName(card) + ": an omen die was used - produce 1 fighter in " + sysName(s) + "?", kMonuments[card].text))
                continue;
            addUnits(q, UnitType::Fighter, 1, s, -1);
            event(EventKind::Produce, q, pn(q) + " produces 1 fighter in " + sysName(s) + " (" + monName(card) + ").", s);
        }
    busy = false;
}

// ------------------------------------------------------------------ Flotilla Cyclotron (Dih-Mohn)
void Rules::monActivatedDS2(int p, int sysAct) {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers || !monCardHas(G, p, "dihmohn") || !monCardReady(G, p)) return;
    int s = -1;
    if (!monAbilityAt(G, p, "dihmohn", &s) || s == sysAct || !hasShips(G, p, s)) return;
    if (G.systems[size_t(s)].hasToken(p) && !freeholdAt(G, p, s) && !G.fx.dominusOrb) return;  // (their ships there can't move anyway)
    const int card = monumentIndex("dihmohn");
    if (!myes(p, card, kMonAskDihmohn,
              monName(card) + ": exhaust it - your ships in " + sysName(s) + " move 1 further this tactical action?", kMonuments[card].text))
        return;
    monExhaust(p, card);
    G.fx.monCyclotron = int16_t(s);
    monEvent(p, card, pn(p) + "'s ships in " + sysName(s) + " move 1 further this tactical action (" + monName(card) + ").", s);
}

// ------------------------------------------------------------------ Bounty Brokerage (Vaden)
void Rules::monReplenished(int p) {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers) return;
    for (int v : holdersOf6(G, "vaden")) {
        int s = -1;
        if (v == p || !monAbilityAt(G, v, "vaden", &s)) continue;
        if (G.dsDebt[size_t(p)] <= 0 || G.players[size_t(p)].commodities <= 0) continue;  // (no tokens on the Vaden's sheet / nothing to take)
        bool near = false;
        for (int x : inOrNext6(G, s)) near = near || hasShips(G, p, x);
        if (!near) continue;
        const int card = monumentIndex("vaden");
        curFor_ = p;
        const bool take = myes(v, card, kMonAskVaden, monName(card) + ": take 1 of " + pn(p) + "'s commodities?",
                               pn(p) + " has " + countOf(G.dsDebt[size_t(p)], "control token", "control tokens") + " on the Vaden sheet.");
        curFor_ = -1;
        if (!take) continue;
        G.players[size_t(p)].commodities = int8_t(G.players[size_t(p)].commodities - 1);
        monEvent(v, card, pn(v) + " takes 1 of " + pn(p) + "'s commodities (" + monName(card) + "): a trade good.", s);
        gainTGNotSupply(v, 1);  // (a commodity received from another player becomes a trade good - LRR 21.5)
    }
}

// ------------------------------------------------------------------ Corsairs' Cove (Florzen)
// right after a production placed k fighters in sys: some (or all) of them go onto the card instead - at least those beyond the
// capacity there (checkProducePlan let them be produced for the card)
void Rules::coveTake(int p, int sys, int k, int overBefore) {
    if (!monumentsOn() || k <= 0 || !coveHolder(G, p)) return;
    Force& f = G.systems[size_t(sys)].space[size_t(p)];
    const int here = std::min(k, int(f.count(UnitType::Fighter)));
    if (here <= 0) return;
    int least = here;
    for (int j = 0; j <= here; ++j) {
        Force t = f;
        t.n[int(UnitType::Fighter)] = uint8_t(t.n[int(UnitType::Fighter)] - j);
        if (overflowOf(G, p, sys, t) <= overBefore) {
            least = j;
            break;
        }
    }
    const int card = monumentIndex("florzen");
    int j = least;
    if (least < here) {
        std::vector<Option> opts;
        for (int c = least; c <= here; ++c)
            opts.push_back({c, c == 0 ? std::string("None - they stay in ") + sysName(sys) : countOf(c, "fighter", "fighters") + " onto the card",
                            c == here ? "All of them." : ""});
        j = mpick(p, card, kMonAskFlorzen, monName(card) + ": place the fighters you produced on the card instead?", opts, kMonuments[card].text);
    }
    if (j <= 0) return;
    f.remove(UnitType::Fighter, j, false);
    G.players[size_t(p)].monCove = int8_t(std::min(99, G.players[size_t(p)].monCove + j));
    monEvent(p, card, pn(p) + " places " + countOf(j, "fighter", "fighters") + " on " + monName(card) + " instead (" +
                          std::to_string(G.players[size_t(p)].monCove) + " there).", sys);
}

// ------------------------------------------------------------------ start of the turn: Corsairs' Cove, Dawnstar HQ
void Rules::monTurnStartDS(int p) {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers) return;
    Player& pl = G.players[size_t(p)];
    // Corsairs' Cove: "At the start of your turn, you may produce fighter units on this card in this system without spending resources."
    {
        int s = -1;
        if (pl.monCove > 0 && monCardHas(G, p, "florzen") && monAbilityAt(G, p, "florzen", &s)) {
            Force f = G.systems[size_t(s)].space[size_t(p)];
            const int over = capacityOverflow(G, p, s), fleet = std::max(fleetLimit(G, p), fleetUseIn(G, p, s));
            int room = 0;
            for (int j = 1; j <= pl.monCove; ++j) {
                f.add(UnitType::Fighter, 1);
                if (overflowOf(G, p, s, f) > over || (fightersFree(G, p) && fleetUse(G, p, s, f) > fleet)) break;
                room = j;
            }
            if (room > 0) {
                const int card = monumentIndex("florzen");
                std::vector<Option> opts;
                for (int j = room; j >= 1; --j) opts.push_back({j, "Produce " + countOf(j, "fighter", "fighters"), "In " + sysName(s) + ", free."});
                opts.push_back({-1, "Not now", countOf(pl.monCove, "fighter waits", "fighters wait") + " on the card."});
                const int j = mpick(p, card, kMonAskFlorzenOut, monName(card) + ": produce fighters from the card in " + sysName(s) + "?", opts,
                                    kMonuments[card].text);
                if (j > 0) {
                    pl.monCove = int8_t(pl.monCove - j);
                    addUnits(p, UnitType::Fighter, j, s, -1);
                    event(EventKind::Produce, p, pn(p) + " produces " + countOf(j, "fighter", "fighters") + " from " + monName(card) + " in " + sysName(s) + ".", s);
                }
            }
        }
    }
    // Dawnstar HQ: "At the start of your turn you may destroy 1 other player's coexisting unit in this system."
    {
        int s = -1;
        if (monCardHas(G, p, "tnelis") && monAbilityAt(G, p, "tnelis", &s)) {
            std::vector<Option> opts;
            for (int16_t x : G.systems[size_t(s)].planets) {
                const PlanetState& ps = G.planets[size_t(x)];
                if (ps.destroyed || ps.units[size_t(p)].total() == 0) continue;
                for (int q = 0; q < kSides; ++q) {
                    if (q == p || (q >= G.numPlayers && q != kNeutral)) continue;
                    for (int t = 0; t < kUnitTypes; ++t)
                        if (ps.units[size_t(q)].n[t])
                            opts.push_back({(x * 16 + q) * 16 + t,
                                            "1 of " + (q == kNeutral ? std::string("the neutral") : pn(q) + "'s") + " " + unitTypeName(UnitType(t), ps.units[size_t(q)].n[t] > 1),
                                            "On " + planetName(x) + ".", true, s});
                }
            }
            if (!opts.empty()) {
                const int card = monumentIndex("tnelis");
                opts.push_back({-1, "Not now", ""});
                const int c = mpick(p, card, kMonAskTnelis, monName(card) + ": destroy 1 other player's coexisting unit in " + sysName(s) + "?", opts,
                                    kMonuments[card].text);
                if (c >= 0) {
                    const int t = c % 16, q = (c / 16) % 16, x = c / 256;
                    monEvent(p, card, pn(p) + " destroys 1 of " + (q == kNeutral ? std::string("the neutral") : pn(q) + "'s") + " " + unitTypeName(UnitType(t)) +
                                          " on " + planetName(x) + " (" + monName(card) + ").",
                             s);
                    destroyUnits(q, UnitType(t), 1, s, x, p, "Dawnstar HQ");
                    checkEliminations();
                }
            }
        }
    }
}

// ------------------------------------------------------------------ Mirrorforge (Mortheus)
bool Rules::mirrorforgeAction(int p, int card, int sys) {
    auto ableIn = [&](int q) {  // q's ships can enter the Mirrorforge's system ("if able")
        if (q == kNeutral) return !(tileAnomaliesMove(G, sys) & (kSupernova | kAsteroid));
        return canEndMoveIn(G, q, sys) && !(anomaliesFor(G, q, sys) & kNebula);  // (a nebula: only the active system - LRR 59.2)
    };
    auto movable = [&](int q, int a, int t) {
        const Force& f = G.systems[size_t(a)].space[size_t(q)];
        return f.n[t] > 0 && isShip(UnitType(t)) && UnitType(t) != UnitType::Fighter && unitDef(G, q, UnitType(t)).move > 0;
    };
    std::vector<Option> opts;
    std::vector<int> seen;
    for (int16_t a : G.systems[size_t(sys)].adj) {
        if (std::find(seen.begin(), seen.end(), int(a)) != seen.end() || G.systems[size_t(a)].isHyperlane()) continue;
        seen.push_back(a);
        for (int q = 0; q < kSides; ++q) {
            if (q == p || (q >= G.numPlayers && q != kNeutral)) continue;
            if (q < G.numPlayers && G.players[size_t(q)].eliminated) continue;
            if (G.systems[size_t(a)].space[size_t(q)].nonFighterShips() <= 0) continue;
            opts.push_back({a * 16 + q, (q == kNeutral ? std::string("Neutral") : pn(q) + "'s") + " ships in " + sysName(a),
                            forceText(G.systems[size_t(a)].space[size_t(q)]) + ".", true, a});
        }
    }
    if (opts.empty()) return false;
    opts.push_back({-1, "Back", ""});
    const int c = mpick(p, card, kMonAskMortheus, monName(card) + ": pull whose ships into " + sysName(sys) + "?", opts, kMonuments[card].text, true);
    if (c < 0) return false;
    const int a = c / 16, q = c % 16;
    monExhaust(p, card);
    monEvent(p, card, pn(p) + " uses " + monName(card) + ": " + (q == kNeutral ? std::string("the neutral") : pn(q) + "'s") + " ships in " + sysName(a) +
                          " are drawn into " + sysName(sys) + ".",
             sys);
    Force moved;
    int worth = 0;  // (half resources)
    if (ableIn(q) && !mawBlocks(G, q, a)) {
        for (int guard = 0; guard < 40 && worth < 8; ++guard) {
            std::vector<Option> ships;
            for (int t = 0; t < kUnitTypes; ++t)
                if (movable(q, a, t))
                    ships.push_back({t, std::string("1 ") + unitTypeName(UnitType(t)),
                                     "Costs " + std::to_string(unitCost2(G, q, UnitType(t)) / 2) + (unitCost2(G, q, UnitType(t)) % 2 ? ".5" : "") + "."});
            if (ships.empty()) break;
            int t = ships[0].id;
            if (q == kNeutral || ships.size() == 1) {
                for (const Option& o : ships)  // (neutral units: the cheapest first)
                    if (unitCost2(G, q, UnitType(o.id)) < unitCost2(G, q, UnitType(t))) t = o.id;
            } else {
                curFor_ = p;
                t = mpick(q, card, kMonAskMortheusShip,
                          monName(card) + ": move which ship into " + sysName(sys) + "? (" + std::to_string(worth / 2) + (worth % 2 ? ".5" : "") +
                              " of 4 resources moved)",
                          ships, kMonuments[card].text);
                curFor_ = -1;
            }
            Force& from = G.systems[size_t(a)].space[size_t(q)];
            Force& to = G.systems[size_t(sys)].space[size_t(q)];
            const int dmgBefore = from.dmg[t];
            from.remove(UnitType(t), 1, false);
            to.add(UnitType(t), 1);
            if (from.dmg[t] < dmgBefore) to.dmg[t] = uint8_t(to.dmg[t] + 1);
            moved.add(UnitType(t), 1);
            worth += unitCost2(G, q, UnitType(t));
        }
        // what they carried: the fighters and ground forces left without room go along with them (LRR 95: moving ships transport)
        if (q < G.numPlayers && moved.total() > 0)
            for (UnitType t : {UnitType::Fighter, UnitType::Infantry, UnitType::Mech})
                for (int guard = 0; guard < 40; ++guard) {
                    Force& from = G.systems[size_t(a)].space[size_t(q)];
                    Force& to = G.systems[size_t(sys)].space[size_t(q)];
                    if (from.count(t) <= 0 || overflowOf(G, q, a, from) <= 0) break;
                    Force test = to;
                    test.add(t, 1);
                    if (overflowOf(G, q, sys, test) > overflowOf(G, q, sys, to)) break;
                    const int dmgBefore = from.dmg[int(t)];
                    from.remove(t, 1, false);
                    to.add(t, 1);
                    if (from.dmg[int(t)] < dmgBefore) to.dmg[int(t)] = uint8_t(to.dmg[int(t)] + 1);
                    moved.add(t, 1);
                }
    }
    if (moved.total() == 0) {
        say(p, std::string("Nothing can move into ") + sysName(sys) + ".", sys);
        return true;
    }
    Event e;
    e.kind = EventKind::Move;
    e.player = int8_t(q < G.numPlayers ? q : -1);
    e.system = int16_t(sys);
    e.other = int8_t(p);
    e.from = {int16_t(a)};
    e.text = (q == kNeutral ? std::string("The neutral units move ") : pn(q) + " moves ") + forceText(moved) + " from " + sysName(a) + " into " + sysName(sys) +
             " (" + monName(card) + ").";
    E.emit(e);
    if (q < G.numPlayers) {
        removeExcessCapacity(q, a);
        enforceFleetIn(q, sys);
    }
    if (hasShips(G, p, sys) && hasShips(G, q, sys)) {
        monEvent(p, card, pn(p) + " and " + (q == kNeutral ? std::string("the neutral ships") : pn(q)) + " resolve a space combat in " + sysName(sys) + ".", sys);
        spaceCombat(p, q, sys);
        checkFloatingFactories(sys);
        removeExcessCapacity(p, sys);
        if (q < G.numPlayers) removeExcessCapacity(q, sys);
        stationControl(sys);
        checkEliminations();
    }
    return true;
}

// ------------------------------------------------------------------ AI Conclave (Augurs)
bool Rules::conclaveExplore(int p, int planet) {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers || !monCardHas(G, p, "augurs")) return false;
    int s = -1, x = -1;
    if (!monAbilityAt(G, p, "augurs", &s, &x) || x < 0 || x == planet) return false;
    {  // ("would explore": a planet with a trait - LRR 35.2b; Terraform gives all three)
        const PlanetState& o = G.planets[size_t(planet)];
        if (!(explorable(kPlanets[o.def].trait) || o.terraform || explorable(kPlanets[o.def].trait2))) return false;
    }
    const PlanetState& ps = G.planets[size_t(x)];
    const PlanetDef& d = kPlanets[ps.def];
    if (ps.destroyed || !(explorable(d.trait) || ps.terraform || explorable(d.trait2))) return false;
    int secrets = 0;
    for (int16_t o : G.players[size_t(p)].scored)
        if (scoredAsSecret(G, p, o)) ++secrets;
    const int card = monumentIndex("augurs");
    curPlanet_ = planet;
    const bool go = myes(p, card, kMonAskAugurs, monName(card) + ": explore " + planetName(x) + " instead of " + planetName(planet) + "?",
                         secrets > 0 ? "You would gain " + countOf(secrets, "trade good", "trade goods") + " (your scored secret objectives)."
                                     : std::string("You have no scored secret objectives: no trade goods."));
    curPlanet_ = -1;
    if (!go) return false;
    monEvent(p, card, pn(p) + " explores " + planetName(x) + " instead of " + planetName(planet) + " (" + monName(card) + ").", s);
    explorePlanet(p, x);
    if (secrets > 0) {
        say(p, pn(p) + " gains " + countOf(secrets, "trade good", "trade goods") + " (" + monName(card) + ").", s);
        gainTG(p, secrets);
    }
    return true;
}

// ------------------------------------------------------------------ Dawnstar HQ (Tnelis)
void Rules::dawnstar(int loser, int winner, int planet) {
    if (!monumentsOn() || loser < 0 || loser >= G.numPlayers || planet < 0 || !monCardHas(G, loser, "tnelis")) return;
    int s = -1, x = -1;
    if (!monAbilityAt(G, loser, "tnelis", &s, &x) || x != planet) return;
    const int card = monumentIndex("tnelis");
    curFor_ = winner;
    curPlanet_ = planet;
    const bool go = myes(loser, card, kMonAskTnelisLoss, monName(card) + ": you lost the ground combat on " + planetName(planet) + " - destroy each unit on it?",
                         "Theirs, yours, the structures and the monument itself.");
    curFor_ = -1;
    curPlanet_ = -1;
    if (!go) return;
    monEvent(loser, card, pn(loser) + " destroys every unit on " + planetName(planet) + " (" + monName(card) + ").", s);
    for (int q = 0; q < kSides; ++q) {
        if (q >= G.numPlayers && q != kNeutral) continue;
        for (int t = 0; t < kUnitTypes; ++t) {
            const int k = G.planets[size_t(planet)].units[size_t(q)].n[t];
            if (k > 0) destroyUnits(q, UnitType(t), k, s, planet, loser, "Dawnstar HQ");
        }
    }
}

// ------------------------------------------------------------------ Aylor Raider Hoard (Vaylerian)
void Rules::hoardMark() {
    for (int q = 0; q < G.numPlayers; ++q) {
        hoardTg_[q] = G.players[size_t(q)].tradeGoods;
        hoardGain_[q] = tgGained_[q];
    }
}

void Rules::hoardCheck(int sys) {
    if (!monumentsOn() || sys < 0) return;
    int spender = -1;
    for (int q = 0; q < G.numPlayers && spender < 0; ++q)
        if (hoardTg_[q] + (tgGained_[q] - hoardGain_[q]) - G.players[size_t(q)].tradeGoods > 0) spender = q;
    hoardMark();
    if (spender < 0) return;
    for (int v : holdersOf6(G, "vaylerian")) {
        int m = -1;
        if (!monCardReady(G, v) || !monAbilityAt(G, v, "vaylerian", &m) || !nextTo(G, m, sys)) continue;
        const int card = monumentIndex("vaylerian");
        curFor_ = spender;
        const bool draw = myes(v, card, kMonAskVaylerian,
                               monName(card) + ": " + pn(spender) + " spent trade goods in the space combat in " + sysName(sys) + " - exhaust it to draw 1 action card?",
                               kMonuments[card].text);
        curFor_ = -1;
        if (!draw) continue;
        monExhaust(v, card);
        monEvent(v, card, pn(v) + " draws 1 action card (" + monName(card) + ").", m);
        drawActionCards(v, 1, monName(card));
    }
}

// ------------------------------------------------------------------ Independence Tower (Free Systems)
void Rules::monAfterLeadership(int p) {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers || !monCardHas(G, p, "fsc")) return;
    int s = -1;
    if (!monAbilityAt(G, p, "fsc", &s)) return;
    const int card = monumentIndex("fsc");
    const std::vector<int> near = inOrNext6(G, s);
    for (int i = 1; i < G.numPlayers; ++i) {
        const int q = (p + i) % G.numPlayers;
        if (G.players[size_t(q)].eliminated) continue;
        bool there = false;
        for (int x : near) there = there || hasUnits(G, q, x);
        if (!there) continue;
        std::vector<Option> opts;
        for (int16_t n : G.players[size_t(q)].notes)
            if (noteOwner(n) == p) opts.push_back({n, "Reveal " + noteNameOf(G, n), "Gain 1 command token."});
        if (opts.empty()) continue;
        opts.push_back({-1, "Don't", "Keep it hidden."});
        curFor_ = p;
        const int n = mpick(q, card, kMonAskFsc, monName(card) + ": reveal 1 of " + pn(p) + "'s promissory notes to gain 1 command token?", opts,
                            kMonuments[card].text);
        curFor_ = -1;
        if (n < 0) continue;
        E.revealed();
        monEvent(p, card, pn(q) + " reveals " + noteNameOf(G, n) + " and gains 1 command token (" + monName(card) + ").", s);
        gainTokens(q, 1, false, monName(card));
    }
}

// ------------------------------------------------------------------ Hyperlane Relay (Celdauri)
void Rules::relayCommit(int p, int sys) {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers || !monCardHas(G, p, "celdauri")) return;
    int s = -1, x = -1;
    if (!monAbilityAt(G, p, "celdauri", &s, &x) || x < 0 || s == sys || G.systems[size_t(s)].hasToken(p)) return;
    const int card = monumentIndex("celdauri");
    for (int guard = 0; guard < 40; ++guard) {
        std::vector<Option> opts;
        for (int16_t y : G.systems[size_t(sys)].planets) {
            const PlanetState& ps = G.planets[size_t(y)];
            if (y == x || ps.destroyed || isDMZ(G, y) || ps.units[size_t(p)].count(UnitType::SpaceDock) <= 0) continue;
            for (UnitType t : {UnitType::Infantry, UnitType::Mech})
                if (G.planets[size_t(x)].units[size_t(p)].count(t) > 0)
                    opts.push_back({y * 16 + int(t), std::string("1 ") + unitTypeName(t) + " to " + planetName(y),
                                    "From " + planetName(x) + " (" + std::to_string(G.planets[size_t(x)].units[size_t(p)].count(t)) + " there).", true, sys});
        }
        if (opts.empty()) return;
        opts.push_back({-1, guard ? "Done" : "Not now", ""});
        const int c = mpick(p, card, kMonAskCeldauri, monName(card) + ": commit ground forces from " + planetName(x) + " to your space dock planets here?",
                            opts, kMonuments[card].text);
        if (c < 0) return;
        const int y = c / 16;
        const UnitType t = UnitType(c % 16);
        Force& from = G.planets[size_t(x)].units[size_t(p)];
        Force& to = G.planets[size_t(y)].units[size_t(p)];
        const int dmgBefore = from.dmg[int(t)];
        from.remove(t, 1, false);
        to.add(t, 1);
        if (from.dmg[int(t)] < dmgBefore) to.dmg[int(t)] = uint8_t(to.dmg[int(t)] + 1);
        monEvent(p, card, pn(p) + " commits 1 " + unitTypeName(t) + " from " + planetName(x) + " to " + planetName(y) + " (" + monName(card) + ").", sys);
    }
}

}  // namespace ti
