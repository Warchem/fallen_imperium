// Monuments+ (0.82.4) - Discordant Stars, batch 3 (cards in tools/gen_monuments.py, the owner's v3.1 card files; set "DS3", new
// games only - fixes 42). Rulings: the Living Rules first, then the Monuments+ rulebook / the Discordant Stars rules, then AsyncTI4;
// the readings that needed a choice are RULES-CHECK 0.82.4 M63-M73.
//
//   Shades' Den (Kollecc)        "After movement, if this is the active system and contains no more than 1 other player's ships, you may
//                                place any of your captured units from your faction sheet into this system." (their own units on their
//                                sheet - Cloaked Fleets / the Nightingale's - into its space area; movement() -> monAfterMovement)
//   Tucc Academy (Bentor)        "DEPLOY: After another player explores a planet that is not adjacent to a home system, you may place this
//                                unit into coexistence on that planet and explore it." (explorePlanet -> monExploredDS; from the
//                                reinforcements only - the FAQ; a planet whose traits it fits; "adjacent": its own system counts - LRR 6)
//   Krotas Bannerhall (Kjalengard) "Once per action, when a player wins a combat in or adjacent to this system, you and that player may
//                                each gain 1 command token." (combatWon -> monCombatWon; Player::monBanner = the action it was used in)
//   Forbidden Library (Lanefir)  "Once per action, you may use the technology specialty of a planet in or adjacent to this system, even if it
//                                is exhausted." (query libraryColours: any planet's specialty there, not exhausted by it; researchStep and
//                                canResearchNow count it as 1 more prerequisite of that colour covered)
//   Polemarch's Hive (Cheiran)   "This unit's PRODUCTION value is equal to the number of structures you control in or adjacent to this
//                                system." (query hiveProduction in productionCapacity)
//   Kyro Reliquary (Kyro)        "This planet counts as a legendary planet with an attachment and a technology speciality of no type."
//                                (query reliquaryOn: isLegendaryPlanet, planetHasSkip, the attachment counts) "When this unit is destroyed,
//                                you must place it on a planet you control in this or an adjacent system, if able. If you do, draw a
//                                relic." (monUnitsDestroyed -> kyroPending_ -> kyroResolve at the next safe point)
//   Ghoti Anchorpoint (Ghoti)    "During movement, you may choose to treat this system as though it contains no planets." (query
//                                anchorpointBare: the Ghoti's planetless-system effects while moving - Ceie Doleegueaunm, Spawning
//                                Grounds) "You treat this system as though it contains a frontier token." (anchorpointFrontier: their
//                                frontier explorations there - exploreFrontier, Dark Energy Tap)
//   Wonell's Bastion (Kolume)    "When another player passes, choose this or an adjacent system that contains a unit with SPACE CANNON;
//                                either use this unit's SPACE CANNON against another player's ships in that system, or repeat this
//                                ability, starting in that system." (pass -> monPassedDS: a chain of hops, never back to a system it
//                                already started from)
//   Verdant Halo (Gledge)        "At the start of your turn, you may exhaust 2 planets you control that contain a Core token; 1 player
//                                treats all laws as blank until the end of this turn." (monTurnStartDS3 -> Game::monLawsBlankFor, read
//                                by lawInPlay / lawOwner / absolLaw)
//   Vault of New Phrad / Reliquat Unleashed (Rhodun) - the card's two sides (Player::monFlipped; giveMonument gives the side's unit):
//                                "when a player activates this system, you may flip this card" (monActivatedDS3); "Flip this card at any
//                                time" (free option 648 on their turn, and at the start of a combat it would help in); +1 / +2 to their
//                                combat rolls in and next to its system (combatMods -> rhodunBonus); "Other players with units in The
//                                Fracture cannot activate this system" (query canActivate)

#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
struct CardRef7 {
    int card = -1, unit = -1;
    explicit CardRef7(const char* key) {
        card = monumentIndex(key);
        unit = card >= 0 ? unitByKey(kMonuments[card].unit) : -1;
    }
};
bool holds7(const Game& g, int q, const CardRef7& r) {
    if (!(g.modules & kModMonuments) || r.card < 0 || q < 0 || q >= g.numPlayers) return false;
    const Player& pl = g.players[size_t(q)];
    if (pl.unit[int(UnitType::Monument)] < 0) return false;
    return pl.unit[int(UnitType::Monument)] == r.unit || pl.nekroMon[0] == r.card || pl.nekroMon[1] == r.card;
}
bool next7(const Game& g, int a, int b) {
    if (a < 0 || b < 0) return false;
    if (a == b) return true;
    for (int16_t x : g.systems[size_t(a)].adj)
        if (x == b) return true;
    return false;
}
std::vector<int> around7(const Game& g, int sys) {
    std::vector<int> v;
    if (sys < 0) return v;
    v.push_back(sys);
    for (int16_t a : g.systems[size_t(sys)].adj)
        if (std::find(v.begin(), v.end(), int(a)) == v.end() && !g.systems[size_t(a)].isHyperlane()) v.push_back(a);
    return v;
}
std::vector<int> holders7(const Game& g, const char* key) {
    std::vector<int> v;
    if (!(g.modules & kModMonuments)) return v;
    for (int q = 0; q < g.numPlayers; ++q)
        if (!g.players[size_t(q)].eliminated && monCardHas(g, q, key)) v.push_back(q);
    return v;
}
std::string count7(int k, const char* one, const char* many) { return std::to_string(k) + " " + (k == 1 ? one : many); }
// "once per action": the turn now being taken (Game::turnCount counts the turns already over)
int32_t actionKey(const Game& g) { return int32_t(g.turnCount) + 1; }
bool hasSpaceCannonUnit(const Game& g, int s) {
    for (int q = 0; q < kSides; ++q) {
        if (q >= g.numPlayers && q != kNeutral) continue;
        auto any = [&](const Force& f) {
            for (int t = 0; t < kUnitTypes; ++t)
                if (f.n[t] && unitDef(g, q, UnitType(t)).cannon > 0) return true;
            return false;
        };
        if (any(g.systems[size_t(s)].space[size_t(q)])) return true;
        for (int16_t x : g.systems[size_t(s)].planets)
            if (any(g.planets[size_t(x)].units[size_t(q)])) return true;
    }
    return false;
}
}  // namespace

// ------------------------------------------------------------------ queries
int libraryColours(const Game& g, int p) {
    static const CardRef7 lib("lanefir");
    if (!holds7(g, p, lib) || g.players[size_t(p)].monLibrary == actionKey(g)) return 0;
    const int m = monumentSystem(g, p);
    if (m < 0) return 0;
    int mask = 0;
    for (int s : around7(g, m))
        for (int16_t x : g.systems[size_t(s)].planets) {
            const PlanetState& ps = g.planets[size_t(x)];
            if (ps.destroyed) continue;
            const PlanetDef& d = kPlanets[ps.def];
            if (d.skip != TechColor::None) mask |= 1 << int(d.skip);  // (the printed specialties - "of a planet")
            if (d.skip2 != TechColor::None) mask |= 1 << int(d.skip2);
        }
    return mask;
}

int hiveProduction(const Game& g, int p, int sys) {
    static const CardRef7 hive("cheiran");
    if (!holds7(g, p, hive) || sys < 0) return 0;
    int planet = -1;
    if (monumentSystem(g, p, &planet) != sys) return 0;
    int n = 0;  // structures they control in or next to its system (their PDS, space docks and monuments - this one too)
    for (int s : around7(g, sys)) {
        n += g.systems[size_t(s)].space[size_t(p)].structures();
        for (int16_t x : g.systems[size_t(s)].planets) n += g.planets[size_t(x)].units[size_t(p)].structures();
    }
    return n;
}

bool reliquaryOn(const Game& g, int planet) {
    static const CardRef7 rel("kyro");
    if (!(g.modules & kModMonuments) || rel.card < 0 || planet < 0 || planet >= int(g.planets.size())) return false;
    const PlanetState& ps = g.planets[size_t(planet)];
    for (int q = 0; q < g.numPlayers; ++q)
        if (ps.units[size_t(q)].count(UnitType::Monument) && holds7(g, q, rel)) return true;
    return false;
}

bool anchorpointBare(const Game& g, int p, int sys) {
    static const CardRef7 anchor("ghoti");
    if (!holds7(g, p, anchor) || sys < 0) return false;
    return monumentSystem(g, p) == sys;
}
bool anchorpointFrontier(const Game& g, int p, int sys) { return anchorpointBare(g, p, sys); }

bool reliquatBlocks(const Game& g, int p, int sys) {
    static const CardRef7 back("rhodun_back");
    if (!(g.modules & kModMonuments) || back.card < 0 || p < 0 || p >= g.numPlayers) return false;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p || !holds7(g, q, back) || monumentSystem(g, q) != sys) continue;
        for (size_t s = 0; s < g.systems.size(); ++s)
            if (g.systems[s].fracture && hasUnits(g, p, int(s))) return true;
    }
    return false;
}

// +1 / +2 to p's combat rolls against opp in sys (the Vault of New Phrad / Reliquat Unleashed)
int rhodunBonus(const Game& g, int p, int opp, int sys) {
    static const CardRef7 front("rhodun_front"), back("rhodun_back");
    if (!(g.modules & kModMonuments) || p < 0 || p >= g.numPlayers || sys < 0 || opp < 0) return 0;
    const bool f = holds7(g, p, front), b = holds7(g, p, back);
    if (!f && !b) return 0;
    const int m = monumentSystem(g, p);
    if (m < 0 || !next7(g, m, sys)) return 0;
    int bonus = 0;
    if (f && opp < g.numPlayers)
        for (int16_t t : g.players[size_t(opp)].techs)
            if (kTechs[t].unitUpgrade) {
                bonus = 1;
                break;
            }
    if (b)
        for (size_t s = 0; s < g.systems.size(); ++s)
            if (g.systems[s].fracture && hasUnits(g, opp, int(s))) {
                bonus = std::max(bonus, 2);
                break;
            }
    return bonus;
}

// ------------------------------------------------------------------ Shades' Den (Kollecc)
void Rules::monAfterMovement(int active, int sys) {
    (void)active;
    if (!monumentsOn() || sys < 0 || sys != G.activeSystem) return;
    for (int k : holders7(G, "kollecc")) {
        int m = -1;
        if (!monAbilityAt(G, k, "kollecc", &m) || m != sys) continue;
        int others = 0;
        for (int q = 0; q < kSides; ++q)
            if (q != k && (q < G.numPlayers || q == kNeutral) && hasShips(G, q, sys)) ++others;
        if (others > 1) continue;
        Player& pl = G.players[size_t(k)];
        const int card = monumentIndex("kollecc");
        int placed = 0;
        for (int guard = 0; guard < 40; ++guard) {
            int n[kUnitTypes] = {}, total = 0;
            for (int16_t c : pl.captured)
                if (c / 16 == k) ++n[c % 16], ++total;
            if (!total) break;
            std::vector<Option> opts;
            opts.push_back({100, "All of them", count7(total, "unit", "units") + " into " + sysName(sys) + "."});
            for (int t = 0; t < kUnitTypes; ++t)
                if (n[t]) opts.push_back({t, std::string("1 ") + unitTypeName(UnitType(t)), std::to_string(n[t]) + " on your sheet."});
            opts.push_back({-1, placed ? "Done" : "Not now", ""});
            const int c = mpick(k, card, kMonAskKollecc, monName(card) + ": place your captured units from your sheet into " + sysName(sys) + "?", opts,
                                kMonuments[card].text);
            if (c < 0) break;
            for (size_t i = 0; i < pl.captured.size();) {
                const int16_t u = pl.captured[i];
                if (u / 16 != k || (c != 100 && u % 16 != c)) {
                    ++i;
                    continue;
                }
                pl.captured.erase(pl.captured.begin() + long(i));
                G.systems[size_t(sys)].space[size_t(k)].add(UnitType(u % 16), 1);
                ++placed;
                if (c != 100) break;
            }
            if (c == 100) break;
        }
        if (placed > 0) {
            monEvent(k, card, pn(k) + " places " + count7(placed, "captured unit", "captured units") + " into " + sysName(sys) + " (" + monName(card) + ").", sys);
            removeExcessCapacity(k, sys);
        }
    }
}

// ------------------------------------------------------------------ Tucc Academy (Bentor)
void Rules::monExploredDS(int p, int planet) {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers || planet < 0) return;
    for (int b : holders7(G, "bentor")) {
        if (b == p || unitsAvailable(G, b, UnitType::Monument) <= 0) continue;  // (DEPLOY: from the reinforcements only - the FAQ)
        const PlanetState& ps = G.planets[size_t(planet)];
        if (ps.destroyed || isDMZ(G, planet) || ps.units[size_t(b)].count(UnitType::Monument) || !monumentFitsPlanet(G, b, planet)) continue;
        bool nearHome = false;  // ("not adjacent to a home system": a planet is adjacent to the system that contains it - LRR 6)
        for (int s : around7(G, ps.system)) nearHome = nearHome || G.systems[size_t(s)].home >= 0;
        if (nearHome) continue;
        const int card = monumentIndex("bentor");
        curFor_ = p;
        curPlanet_ = planet;
        const bool go = myes(b, card, kMonAskBentor,
                             monName(card) + ": DEPLOY into coexistence on " + planetName(planet) + " (" + pn(p) + " explored it) and explore it?",
                             kMonuments[card].text);
        curFor_ = -1;
        curPlanet_ = -1;
        if (!go) continue;
        addUnits(b, UnitType::Monument, 1, ps.system, planet);
        G.planets[size_t(planet)].coexist = uint16_t(G.planets[size_t(planet)].coexist | (1u << b));
        monEvent(b, card, pn(b) + " deploys " + monName(card) + " into coexistence on " + planetName(planet) + " and explores it.", ps.system);
        explorePlanet(b, planet);
        return;  // (one monument)
    }
}

// ------------------------------------------------------------------ Krotas Bannerhall (Kjalengard)
void Rules::monCombatWon(int winner, int loser, int sys) {
    (void)loser;
    if (!monumentsOn() || sys < 0) return;
    for (int k : holders7(G, "kjalengard")) {
        int m = -1;
        Player& pk = G.players[size_t(k)];
        if (!monAbilityAt(G, k, "kjalengard", &m) || !next7(G, m, sys) || pk.monBanner == actionKey(G)) continue;
        if (reinforcementTokens(k) <= 0 && (winner == k || winner >= G.numPlayers || reinforcementTokens(winner) <= 0)) continue;
        const int card = monumentIndex("kjalengard");
        curFor_ = winner < G.numPlayers ? winner : -1;
        const bool use = myes(k, card, kMonAskKjalengard,
                              monName(card) + ": " + (winner == k ? std::string("you") : winner < G.numPlayers ? pn(winner) : std::string("the neutral units")) +
                                  " won the combat in " + sysName(sys) + " - you" + (winner != k && winner < G.numPlayers ? " and they" : "") +
                                  " gain 1 command token?",
                              kMonuments[card].text);
        curFor_ = -1;
        if (!use) continue;
        pk.monBanner = actionKey(G);
        monEvent(k, card, pn(k) + "'s " + monName(card) + " honours the victor in " + sysName(sys) + ".", sys);
        if (reinforcementTokens(k) > 0) gainTokens(k, 1, false, monName(card));
        if (winner != k && winner >= 0 && winner < G.numPlayers && reinforcementTokens(winner) > 0 &&
            myes(winner, card, kMonAskKjalengardWin, monName(card) + ": gain 1 command token for your victory?", kMonuments[card].text))
            gainTokens(winner, 1, false, monName(card));
    }
}

// ------------------------------------------------------------------ Forbidden Library (Lanefir)
// in a research, after the other waivers: one prerequisite of a colour the Library's planets have
void Rules::libraryUse(int p, int missing[4]) {
    const int mask = monumentsOn() ? libraryColours(G, p) : 0;
    if (!mask) return;
    int need = -1, could = -1;
    for (int c = 0; c < 4; ++c) {
        if (!((mask >> c) & 1) || missing[c] <= 0) continue;
        if (missing[c] > int(skipPlanets(G, p, TechColor(c)).size())) need = c;
        else if (could < 0) could = c;
    }
    const int card = monumentIndex("lanefir");
    int c = need;
    if (c < 0 && could >= 0 &&
        myes(p, card, kMonAskLanefir,
             monName(card) + ": use a " + colorName(TechColor(could)) + " specialty near it instead of exhausting one of your planets?",
             kMonuments[card].text))
        c = could;
    if (c < 0) return;
    --missing[c];
    G.players[size_t(p)].monLibrary = actionKey(G);
    monEvent(p, card, pn(p) + " uses a " + colorName(TechColor(c)) + " technology specialty through " + monName(card) + ".");
}

// ------------------------------------------------------------------ Kyro Reliquary (Kyro)
void Rules::kyroResolve() {
    if (kyroPending_.empty()) return;
    const std::vector<std::pair<int, int>> todo = kyroPending_;
    kyroPending_.clear();
    const int card = monumentIndex("kyro");
    for (const auto& e : todo) {
        const int p = e.first, sys = e.second;
        if (p < 0 || p >= G.numPlayers || G.players[size_t(p)].eliminated || unitsAvailable(G, p, UnitType::Monument) <= 0) continue;
        std::vector<Option> opts;
        for (int s : around7(G, sys))
            for (int16_t x : G.systems[size_t(s)].planets) {
                const PlanetState& ps = G.planets[size_t(x)];
                if (ps.owner != p || ps.destroyed || isDMZ(G, x) || !monumentFitsPlanet(G, p, x)) continue;
                opts.push_back({x, planetName(x), "", true, s});
            }
        if (opts.empty()) {
            monEvent(p, card, monName(card) + " is destroyed: no planet of " + pn(p) + "'s to take it in or next to " + sysName(sys) + ".", sys);
            continue;
        }
        const int x = opts.size() == 1 ? opts[0].id
                                       : mpick(p, card, kMonAskKyro, monName(card) + " was destroyed: place it on which planet?", opts, kMonuments[card].text,
                                               false, true);
        const int s = G.planets[size_t(x)].system;
        addUnits(p, UnitType::Monument, 1, s, x);
        monEvent(p, card, pn(p) + " places " + monName(card) + " on " + planetName(x) + " and draws a relic.", s);
        gainRelic(p, monName(card));
    }
}

// ------------------------------------------------------------------ Wonell's Bastion (Kolume)
void Rules::monPassedDS(int p) {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers) return;
    int shipMask = 0;
    for (int t = 0; t < kUnitTypes; ++t)
        if (isShip(UnitType(t))) shipMask |= 1 << t;
    for (int k : holders7(G, "kolume")) {
        int m = -1;
        if (k == p || !monAbilityAt(G, k, "kolume", &m) || unitDef(G, k, UnitType::Monument).cannon <= 0) continue;
        const int card = monumentIndex("kolume");
        std::vector<int> started{m};
        int from = m;
        for (int guard = 0; guard < 24; ++guard) {
            std::vector<Option> opts;
            for (int x : around7(G, from)) {
                if (!hasSpaceCannonUnit(G, x)) continue;
                for (int q = 0; q < kSides; ++q)
                    if (q != k && (q < G.numPlayers || q == kNeutral) && hasShips(G, q, x))
                        opts.push_back({x * 16 + q, "Fire at " + (q == kNeutral ? std::string("the neutral") : pn(q) + "'s") + " ships in " + sysName(x),
                                        count7(G.systems[size_t(x)].space[size_t(q)].ships(), "ship", "ships"), true, x});
                if (std::find(started.begin(), started.end(), x) == started.end())
                    opts.push_back({100000 + x, "Again, starting in " + sysName(x), "It has a unit with SPACE CANNON.", true, x});
            }
            if (opts.empty()) break;
            // (the rules pass) no "may" on the card: mandatory, if able (Living Rules 1.8)
            curFor_ = p;
            const int c = opts.size() == 1 ? opts[0].id : mpick(k, card, kMonAskKolume,
                                monName(card) + ": " + pn(p) + " passed - choose " + sysName(from) + " or a system next to it with SPACE CANNON.", opts,
                                kMonuments[card].text, true);
            curFor_ = -1;
            if (c < 0) break;
            if (c >= 100000) {
                from = c - 100000;
                started.push_back(from);
                continue;
            }
            const int x = c / 16, q = c % 16;
            Force guns;
            guns.add(UnitType::Monument, 1);
            const int mod = q < G.numPlayers && owns(q, "amd") ? -1 : 0;
            RollResult r = rollUnits(k, guns, RollKind::Cannon, mod, 0);
            reportRoll(k, x, monName(card) + " at " + (q == kNeutral ? std::string("the neutral ships") : pn(q)), r);
            if (r.hits > 0) {
                WinCtx w;  // (Maneuvering Jets, as for any space cannon)
                w.w = Win::BeforeCannonHits;
                w.a = q;
                w.b = k;
                w.sys = x;
                w.value = r.hits;
                if (q < G.numPlayers && cardHas(window(w), "mjets")) {
                    const int cnl = std::min(r.hits, eventOn(G, kEvWild) ? r.hits : 1);
                    r.hits -= cnl;
                    say(q, pn(q) + " cancels " + std::to_string(cnl) + " hit" + (cnl > 1 ? "s" : "") + ".", x);
                }
            }
            if (r.hits > 0) assignHits(q, x, -1, r.hits, shipMask, monName(card), k);
            checkEliminations();
            break;
        }
    }
}

// ------------------------------------------------------------------ start / end of a turn: Verdant Halo; the Rhodun's card
void Rules::monTurnStartDS3(int p) {
    G.monLawsBlankFor = -1;
    if (!monumentsOn() || p < 0 || p >= G.numPlayers) return;
    // Verdant Halo (Gledge)
    int m = -1;
    if (monCardHas(G, p, "gledge") && monAbilityAt(G, p, "gledge", &m) && !G.laws.empty()) {
        std::vector<Option> cores;
        for (int16_t x : G.dsCores)
            if (G.planets[size_t(x)].owner == p && !G.planets[size_t(x)].exhausted && !G.planets[size_t(x)].destroyed)
                cores.push_back({x, planetName(x), "", true, G.planets[size_t(x)].system});
        if (cores.size() >= 2) {
            const int card = monumentIndex("gledge");
            std::vector<Option> who;
            for (int q = 0; q < G.numPlayers; ++q)
                if (!G.players[size_t(q)].eliminated) who.push_back({q, q == p ? std::string("You") : pn(q), "Treats all laws as blank until the end of this turn."});
            who.push_back({-1, "Not now", ""});
            const int q = mpick(p, card, kMonAskGledge, monName(card) + ": exhaust 2 planets with a Core token - who treats all laws as blank this turn?", who,
                                kMonuments[card].text);
            if (q >= 0) {
                std::vector<int> two;
                if (cores.size() == 2) two = {cores[0].id, cores[1].id};
                else two = mpickMany(p, card, kMonAskGledgeCores, monName(card) + ": exhaust which 2 planets with a Core token?", cores, 2, 2);
                for (int x : two) G.planets[size_t(x)].exhausted = true;
                G.monLawsBlankFor = int8_t(q);
                monEvent(p, card, (q == p ? pn(p) + " treats" : pn(q) + " treats") + " all laws as blank until the end of this turn (" + monName(card) + ").", m);
            }
        }
    }
}

// The Rhodun's card: the Vault of New Phrad (front) <-> Reliquat Unleashed (back)
void Rules::rhodunFlip(int p) {
    Player& pl = G.players[size_t(p)];
    const int from = playerMonument(G, p);
    pl.monFlipped = !pl.monFlipped;
    giveMonument(p);
    const int to = playerMonument(G, p);
    if (from >= 0 && to >= 0) monEvent(p, to, pn(p) + " flips " + monName(from) + ": " + monName(to) + ".");
}
bool Rules::rhodunCanFlipBack(int p) const {
    return monumentsOn() && p >= 0 && p < G.numPlayers && playerMonument(G, p) == monumentIndex("rhodun_back") && isFaction(G, p, "zealots");
}
void Rules::monActivatedDS3(int p, int sys) {
    if (!monumentsOn()) return;
    const int front = monumentIndex("rhodun_front");
    for (int z = 0; z < G.numPlayers; ++z) {
        int m = -1;
        if (playerMonument(G, z) != front || !isFaction(G, z, "zealots") || !monAbilityAt(G, z, "rhodun_front", &m) || m != sys) continue;
        curFor_ = p;
        const bool go = myes(z, front, kMonAskRhodun,
                             (p == z ? std::string("You") : pn(p)) + " activated " + sysName(sys) + ": flip " + monName(front) + " to " +
                                 monName(monumentIndex("rhodun_back")) + "?",
                             kMonuments[monumentIndex("rhodun_back")].text);
        curFor_ = -1;
        if (go) rhodunFlip(z);
    }
}
// at the start of a combat in its range: the back side may turn to the front when that helps ("at any time")
void Rules::rhodunCombatStart(int a, int b, int sys) {
    if (!monumentsOn()) return;
    for (int side : {a, b}) {
        const int opp = side == a ? b : a;
        if (!rhodunCanFlipBack(side)) continue;
        int m = -1;
        if (!monAbilityAt(G, side, "rhodun_back", &m) || !next7(G, m, sys)) continue;
        const int now = rhodunBonus(G, side, opp, sys);
        bool upgrades = false;
        if (opp >= 0 && opp < G.numPlayers)
            for (int16_t t : G.players[size_t(opp)].techs) upgrades = upgrades || kTechs[t].unitUpgrade;
        if (now > 0 || !upgrades) continue;  // (the front would give +1 here, the back gives nothing)
        const int back = monumentIndex("rhodun_back");
        curFor_ = opp;
        const bool go = myes(side, back, kMonAskRhodunBack, monName(back) + ": flip it back now (+1 to your combat rolls here: " +
                                                                (opp < G.numPlayers ? pn(opp) : std::string("they")) + " have unit upgrades)?",
                             kMonuments[back].text);
        curFor_ = -1;
        if (go) rhodunFlip(side);
    }
}

}  // namespace ti
