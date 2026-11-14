// Discordant Stars breakthroughs (0.43.0, the owner's RULES-CHECK D1: "incorporate breakthroughs for Thunder's Edge").
// With both Thunder's Edge and Discordant Stars on (new games: Game::fixes >= 2), each Discordant Stars faction gains
// its own breakthrough the same way as the others (the first expedition slice, Thunder's Edge itself, ...). Their texts
// are in breakthroughs.cpp (our own words); the synergy always works; this file adds the abilities.
//
// Batch 1: Arms Brokerage (Axis), Trade Protectorate (Celdauri), Synaptic Interference (Cymiae), Cult of the All-Mother
// (Ghoti), Cryptic Insights (Augurs), Synchronicity VI (Kolume), Reverie Implants (Florzen), The Queens' Wrath
// (Kortali), Strain Dispersal (Kyro), Erasure Corps (Lanefir), Stabilization Arrays (Mirveda), Mercenary Captains
// (Nokar), Rhodun's Reliquary (Zealots), The Prodigy's Triumph (Roh'Dhna), Aurora Research Base (Veldyr).
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

#include "engine/breakthroughs.h"
#include "engine/data.h"
#include "engine/map.h"
#include "engine/query.h"

namespace ti {

namespace {
void dsbtEvent(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    e.system = int16_t(sys);
    E.emit(e);
}
int traitDeck2(Trait t) {
    switch (t) {
        case Trait::Cultural: return int(ExploreType::Cultural);
        case Trait::Hazardous: return int(ExploreType::Hazardous);
        case Trait::Industrial: return int(ExploreType::Industrial);
        default: return -1;
    }
}
int upgradeCount(const Game& G, int p) {
    int n = 0;
    for (int16_t t : G.players[size_t(p)].techs) n += kTechs[t].unitUpgrade ? 1 : 0;
    return n;
}
}  // namespace

bool Rules::dsbtOn(int p, const char* key) const {
    return (G.modules & kModDiscordant) && p >= 0 && p < G.numPlayers && breakthroughIs(G, p, key);
}
bool Rules::dsbtReady(int p, const char* key) const { return dsbtOn(p, key) && !G.players[size_t(p)].btExhausted; }

// When gained: the Celagrom (a second flagship), the flip cards (refreshUnits puts the new units in play).
void Rules::dsbtGained(int p) {
    if (!(G.modules & kModDiscordant)) return;
    const UnitType celagrom = G.fixes >= 17 ? UnitType::Flagship2 : UnitType::Flagship;  // (0.52.1: its own unit)
    if (G.fixes >= 17 && dsbtOn(p, "celdauri")) refreshUnits(p);  // (its unit card first)
    if (dsbtOn(p, "celdauri") && unitsAvailable(G, p, celagrom) > 0) {  // (0.44.4 fixes 8: one more flagship allowed)
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (hasShips(G, p, int(s))) opts.push_back({int(s), sysName(int(s)), ""});
        if (!opts.empty()) {
            const int s = opts.size() == 1 ? opts[0].id
                                           : fpick(p, Ab::DsbtCeldauri, "Trade Protectorate: the Celagrom joins your ships in which system?", opts, "", true);
            addUnits(p, celagrom, 1, s, -1);
            dsbtEvent(E, p, "Trade Protectorate", pn(p) + "'s second flagship, the Celagrom, arrives in " + sysName(s) + ".", s);
        }
    }
    refreshUnits(p);  // Tribune, Eye of the Curator, Omni-Forgeworld
}

// The flip cards: with their unit upgrade owned, the breakthrough's unit replaces it (called at the end of refreshUnits).
void Rules::dsbtUnits(int p) {
    if (!(G.modules & kModDiscordant) || !hasBreakthrough(G, p)) return;
    Player& pl = G.players[size_t(p)];
    auto swap = [&](const char* faction, UnitType t, const char* key) {
        if (!fac(p, faction)) return;
        const UnitDef& cur = kUnits[pl.unit[int(t)]];
        if (cur.tech < 0) return;  // (only on top of the upgrade)
        const int u = unitByKey(key);
        if (u >= 0) pl.unit[int(t)] = int16_t(u);
    };
    swap("kortali", UnitType::Dreadnought, "tribune3");
    swap("mirveda", UnitType::Fighter, "mirveda_fighter3");
    if (fac(p, "rohdhna") && ownsTechKey(G, p, "dsrohdws")) {
        const int u = unitByKey("rohdhna_warsun3");
        if (u >= 0) pl.unit[int(UnitType::WarSun)] = int16_t(u);
    }
}

// Start of q's turn: Arms Brokerage (Axis) - lend one of your unit upgrades for the turn.
void Rules::dsbtTurnStart(int q) {
    G.dsLendP = -1;
    G.dsLendTech = -1;
    for (int a = 0; a < G.numPlayers; ++a) {
        if (a == q || !dsbtReady(a, "axis") || dsLendQuiet_[a] == G.round) continue;
        std::vector<Option> opts;
        for (int16_t t : G.players[size_t(a)].techs)
            if (kTechs[t].unitUpgrade && !G.players[size_t(q)].hasTech(t)) opts.push_back({t, kTechs[t].name, techSummary(t)});
        if (opts.empty()) continue;
        // (a bot lends only as promised in a deal: the UseAgent-like promise isn't there, so bots keep it for deals later)
        if (!G.players[size_t(a)].human) continue;
        opts.push_back({-1, "Not now", ""});
        opts.push_back({-2, "Don't ask again this round", ""});
        const int t = fpick(a, Ab::DsbtAxis, "Arms Brokerage: lend " + pn(q) + " one of your unit upgrades for their turn?", opts,
                            "They use it as their own until the end of this turn; the card is exhausted.");
        if (t == -2) dsLendQuiet_[a] = int16_t(G.round);
        if (t < 0) continue;
        G.players[size_t(a)].btExhausted = true;
        G.dsLendP = int8_t(q);
        G.dsLendTech = int16_t(t);
        refreshUnits(q);
        dsbtEvent(E, a, "Arms Brokerage", pn(a) + " lends " + pn(q) + " its " + kTechs[t].name + " for this turn.");
        break;
    }
}

// End of p's turn: the loan goes back; Reverie Implants (Florzen).
void Rules::dsbtTurnEnd(int p) {
    if (G.dsLendP >= 0) {
        const int q = G.dsLendP;
        G.dsLendP = -1;
        G.dsLendTech = -1;
        refreshUnits(q);
    }
    if (dsbtReady(p, "florzen") && !(G.fixes >= 8 && florzenQuiet_[p] == G.round)) {
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[size_t(q)].eliminated) opts.push_back({q, pn(q), std::to_string(G.players[size_t(q)].tradeGoods) + " trade goods"});
        opts.push_back({-1, "Not now", ""});
        if (G.fixes >= 8) opts.push_back({-2, "Not now - don't ask again this round", ""});  // (0.44.4)
        const int q0 = fpick(p, Ab::DsbtFlorzen, "Reverie Implants: a blind bid against another player?", opts,
                            "Each of you secretly spends 0, 1 or 2 trade goods. If you spent different amounts, take a random promissory "
                            "note from them or explore one of your planets.");
        if (q0 == -2) florzenQuiet_[p] = int16_t(G.round);
        const int q = q0;
        if (q >= 0) {
            G.players[size_t(p)].btExhausted = true;
            auto bid = [&](int who) {
                const int tg = G.players[size_t(who)].tradeGoods;
                std::vector<Option> b = {{0, "0 trade goods", ""}};
                if (tg >= 1) b.push_back({1, "1 trade good", ""});
                if (tg >= 2) b.push_back({2, "2 trade goods", ""});
                curValue_ = who == p ? 1 : 0;  // (for the AI: 1 = the Florzen)
                curFor_ = who == p ? q : p;   // (0.44.4: and the other bidder)
                const int k = pick(who, Tag::Generic, "Reverie Implants: spend how many trade goods (secretly)?", b,
                                   who == p ? "If " + pn(q) + " spends a different amount, you win the bid." : "If you spend the same as " + pn(p) + ", they get nothing.");
                curFor_ = -1;
                curValue_ = 0;
                return k;
            };
            const int mine = bid(p), theirs = bid(q);
            G.players[size_t(p)].tradeGoods = int8_t(G.players[size_t(p)].tradeGoods - mine);
            G.players[size_t(q)].tradeGoods = int8_t(G.players[size_t(q)].tradeGoods - theirs);
            dsbtEvent(E, p, "Reverie Implants", pn(p) + " spends " + std::to_string(mine) + ", " + pn(q) + " spends " + std::to_string(theirs) + ".");
            if (mine != theirs) {
                std::vector<Option> win;
                if (!G.players[size_t(q)].notes.empty()) win.push_back({0, "A random promissory note from " + pn(q), ""});
                for (int x : controlledPlanets(G, p))
                    if (traitDeck2(kPlanets[G.planets[size_t(x)].def].trait) >= 0 && !G.planets[size_t(x)].destroyed) {
                        win.push_back({1, "Explore one of your planets", ""});
                        break;
                    }
                if (!win.empty()) {
                    const int w = win.size() == 1 ? win[0].id : pick(p, Tag::Generic, "Reverie Implants: your prize?", win);
                    if (w == 0) {
                        auto& hand = G.players[size_t(q)].notes;
                        const int16_t n = hand[size_t(E.rng().below(int(hand.size())))];
                        giveNote(q, p, n);
                        say(p, pn(p) + " takes " + noteNameOf(G, n) + " from " + pn(q) + " (Reverie Implants).");
                    } else {
                        std::vector<Option> pl;
                        for (int x : controlledPlanets(G, p))
                            if (traitDeck2(kPlanets[G.planets[size_t(x)].def].trait) >= 0 && !G.planets[size_t(x)].destroyed)
                                pl.push_back({x, planetName(x), ""});
                        const int x = pl.size() == 1 ? pl[0].id : pick(p, Tag::Generic, "Reverie Implants: explore which planet?", pl, "", true);
                        explorePlanet(p, x);
                    }
                }
            }
        }
    }
}

// After refilling commodities: Arms Brokerage (Axis) - a trade good per unit upgrade owned.
void Rules::dsbtReplenished(int p) {
    if (!dsbtOn(p, "axis")) return;
    const int n = upgradeCount(G, p);
    if (n <= 0) return;
    gainTG(p, n);
    dsbtEvent(E, p, "Arms Brokerage", pn(p) + " collects " + std::to_string(n) + " trade good" + (n > 1 ? "s" : "") + " in licence fees.");
}

// A card from exploring a planet, before it resolves: Cryptic Insights (Augurs) - an action card instead.
// Returns true if the card was set aside (not resolved).
bool Rules::dsbtExploreCard(int p, int planet, int c) {
    if (!dsbtOn(p, "augers") || c < 0) return false;
    curValue_ = c;  // (0.44.4: the card, for the AI)
    const bool use = fyes(p, Ab::DsbtAugers, std::string("Cryptic Insights: discard ") + kExploreCards[c].name + " to draw 1 action card instead?",
                          exploreText(c));
    curValue_ = 0;
    if (!use) return false;
    G.exploreDiscard[int(kExploreCards[c].type)].push_back(int16_t(c));
    drawActionCards(p, 1, "Cryptic Insights");
    dsbtEvent(E, p, "Cryptic Insights", pn(p) + " reads the signs on " + planetName(planet) + " and draws an action card.");
    return true;
}

// A technology exhausted: Synchronicity VI (Kolume) - 1 commodity.
void Rules::dsbtTechExhausted(int p) {
    if (!dsbtOn(p, "kolume")) return;
    Player& pl = G.players[size_t(p)];
    if (pl.commodities >= commodityCap(G, pl)) return;
    { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + 1); commodityGained(p, cb_); }
    say(p, pn(p) + " gains 1 commodity (Synchronicity VI).");
}

// Winning a combat: Synaptic Interference (Cymiae), Strain Dispersal (Kyro), Mercenary Captains (Nokar).
void Rules::dsbtWonCombat(int winner, int loser, int sys, bool space) {
    if (winner < 0 || winner >= G.numPlayers) return;
    // Synaptic Interference: purge a note from the hand - push another player's command token out, or 2 trade goods
    // 0.44.4 (fixes 8): "1 of your promissory notes" - their own colour only; a push only when there is somewhere to push to
    auto ownNotes = [&]() {
        std::vector<int16_t> v;
        for (int16_t n : G.players[size_t(winner)].notes)
            if (G.fixes < 8 || noteOwner(n) == winner) v.push_back(n);
        return v;
    };
    if (dsbtOn(winner, "cymiae") && !ownNotes().empty()) {
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == winner || !G.systems[size_t(sys)].hasToken(q)) continue;
            bool room = G.fixes < 8;
            for (int16_t a : G.systems[size_t(sys)].adj) room = room || (!G.systems[size_t(a)].hasToken(q) && !G.systems[size_t(a)].isHyperlane());
            if (room) opts.push_back({q, "Push " + pn(q) + "'s command token next door", ""});
        }
        opts.push_back({100, "Gain 2 trade goods", ""});
        opts.push_back({-1, "No", ""});
        const int c = fpick(winner, Ab::DsbtCymiae, "Synaptic Interference: purge a promissory note from your hand?", opts);
        if (c >= 0) {
            std::vector<Option> notes;
            for (int16_t n : ownNotes()) notes.push_back({n, noteNameOf(G, n), ""});
            const int n = notes.size() == 1 ? notes[0].id : fpick(winner, Ab::DsbtCymiae, "Purge which promissory note?", notes);
            auto& hand = G.players[size_t(winner)].notes;
            hand.erase(std::find(hand.begin(), hand.end(), int16_t(n)));
            say(winner, pn(winner) + " purges " + noteNameOf(G, n) + " (Synaptic Interference).", sys);
            if (G.fixes >= 8) purged(winner);  // (0.44.4: Erasure Corps, Aggressive Broker)
            if (c == 100) {
                gainTG(winner, 2);
            } else {
                std::vector<Option> to;
                for (int16_t a : G.systems[size_t(sys)].adj)
                    if (!G.systems[size_t(a)].hasToken(c) && !G.systems[size_t(a)].isHyperlane()) to.push_back({a, sysName(a), ""});
                if (!to.empty()) {
                    const int a = to.size() == 1 ? to[0].id : fpick(winner, Ab::DsbtCymiae, "Move " + pn(c) + "'s command token to which system?", to, "", true);
                    G.systems[size_t(sys)].tokens &= uint16_t(~(1u << c));
                    G.systems[size_t(a)].tokens |= uint16_t(1u << c);
                    dsbtEvent(E, winner, "Synaptic Interference", pn(c) + "'s command token is pushed into " + sysName(a) + ".", a);
                }
            }
        }
    }
    // Strain Dispersal: once per action - for each technology the loser has and we don't, a commodity (or a conversion)
    if (dsbtOn(winner, "kyro") && loser >= 0 && loser < G.numPlayers && !(dsOncePerAction_ & 8)) {
        int n = 0;
        for (int16_t t : G.players[size_t(loser)].techs) n += G.players[size_t(winner)].hasTech(t) ? 0 : 1;
        if (n > 0) {
            dsOncePerAction_ = uint8_t(dsOncePerAction_ | 8);
            Player& pl = G.players[size_t(winner)];
            int gained = 0, converted = 0;
            for (int i = 0; i < n; ++i) {
                if (pl.commodities < commodityCap(G, pl)) {
                    { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + 1); commodityGained(winner, cb_); }
                    ++gained;
                } else if (pl.commodities > 0) {
                    pl.commodities = int8_t(pl.commodities - 1);
                    gainTG(winner, 1);
                    ++converted;
                }
            }
            dsbtEvent(E, winner, "Strain Dispersal", pn(winner) + " studies " + pn(loser) + "'s technology: " + std::to_string(gained) +
                                                         " commodities" + (converted ? ", " + std::to_string(converted) + " turned into trade goods" : "") + ".", sys);
        }
    }
    // Bannerhalls (Kjalengard): once per action, anyone winning a combat where a Glory token is readies every Bannerhall
    if (!(dsOncePerAction_ & 32) && std::find(G.dsGlory.begin(), G.dsGlory.end(), int16_t(sys)) != G.dsGlory.end()) {
        const int kj = playerOfFaction(G, "kjalengard");
        if (kj >= 0 && dsbtOn(kj, "kjalengard") && G.players[size_t(kj)].oceansExhausted > 0) {
            dsOncePerAction_ = uint8_t(dsOncePerAction_ | 32);
            G.players[size_t(kj)].oceansExhausted = 0;
            dsbtEvent(E, kj, "Bannerhalls", "Victory at a Glory site: " + pn(kj) + "'s Bannerhalls ready.", sys);
        }
    }
    // Mercenary Captains: another player wins a space combat where the Nokar have ships - 2 commodities or 1 command token
    if (space)
        for (int nk = 0; nk < G.numPlayers; ++nk) {
            if (nk == winner || !dsbtOn(nk, "nokar")) continue;
            bool hired = false;  // 0.44.4 (fixes 8): the Nokar's ships hired out (Hired Guns) fight in the buyer's fleet
            if (G.fixes >= 8 && hired_.buyer == winner && hired_.sys == sys)
                for (int t = 0; t < kUnitTypes; ++t) hired = hired || (hired_.n[t] > 0 && G.systems[size_t(sys)].space[size_t(winner)].n[t] > 0);
            if (!hasShips(G, nk, sys) && !hired) continue;
            Player& pl = G.players[size_t(nk)];
            std::vector<Option> opts;
            if (pl.commodities < commodityCap(G, pl)) opts.push_back({0, "Gain 2 commodities", ""});
            if (reinforcementTokens(nk) > 0) opts.push_back({1, "Gain 1 command token", ""});
            if (opts.empty()) continue;
            const int c = opts.size() == 1 ? opts[0].id : fpick(nk, Ab::DsbtNokar, "Mercenary Captains: " + pn(winner) + " won a space combat beside your ships", opts);
            if (c == 0) {
                { const int cb_ = pl.commodities; pl.commodities = int8_t(std::min<int>(commodityCap(G, pl), pl.commodities + 2)); commodityGained(nk, cb_); }
                say(nk, pn(nk) + " gains 2 commodities (Mercenary Captains).", sys);
            } else {
                gainTokens(nk, 1, false, "Mercenary Captains: 1 command token.");
            }
        }
}

// A planet taken: Rhodun's Reliquary (Zealots) - legendary or Fracture: a technology, or a command token back.
void Rules::dsbtControl(int planet, int p) {
    if (!dsbtOn(p, "zealots") || planet < 0) return;
    const int sys = G.planets[size_t(planet)].system;
    if (!isLegendaryPlanet(G, planet) && !G.systems[size_t(sys)].fracture) return;
    std::vector<Option> opts = {{0, "Research 1 technology", "Paid as usual."}};
    if (G.fixes >= 8) {  // 0.44.4: it's free, and only offered when something can be researched
        researchDry_ = true;
        const bool can = researchStep(p, 0, "Rhodun's Reliquary");
        researchDry_ = false;
        opts.clear();
        if (can) opts.push_back({0, "Research 1 technology", "Free."});
    }
    if (G.systems[size_t(sys)].hasToken(p)) opts.push_back({1, "Take back your command token in " + sysName(sys), ""});
    if (G.fixes >= 8 && opts.empty()) return;
    opts.push_back({-1, "Nothing", ""});
    const int c = fpick(p, Ab::DsbtZealots, "Rhodun's Reliquary: " + planetName(planet) + " is yours", opts);
    if (c == 0) {
        researchStep(p, 0, "Rhodun's Reliquary");
    } else if (c == 1) {
        G.systems[size_t(sys)].tokens &= uint16_t(~(1u << p));
        say(p, pn(p) + " takes back its command token in " + sysName(sys) + " (Rhodun's Reliquary).", sys);
    }
}

// Passing: Aurora Research Base (Veldyr) - explore the frontier at home; Cult of the All-Mother (Ghoti).
void Rules::dsbtPass(int p) {
    if (dsbtOn(p, "veldyr") && G.players[size_t(p)].homeSystem >= 0) {
        dsbtEvent(E, p, "Aurora Research Base", pn(p) + "'s research base scans the frontier at home.");
        exploreFrontierDeck(p, G.players[size_t(p)].homeSystem);
    }
    // Insurrectionist Networking (Olradin): purge it to invade every planet with their coexisting infantry
    if (dsbtOn(p, "olradin")) {
        std::vector<int> spots;
        for (size_t x = 0; x < G.planets.size(); ++x)
            if (((G.planets[x].coexist >> p) & 1) && G.planets[x].units[size_t(p)].count(UnitType::Infantry) > 0) spots.push_back(int(x));
        if (!spots.empty() && fyes(p, Ab::DsbtOlradin, "Insurrectionist Networking: purge it to rise up - invade the " + std::to_string(spots.size()) +
                                                          " planets with your coexisting infantry?", "")) {
            G.players[size_t(p)].breakthrough = false;  // (purged)
            G.players[size_t(p)].btPurged = true;
            dsbtEvent(E, p, "Insurrectionist Networking", pn(p) + "'s insurgents rise up!");
            if (G.fixes >= 8) purged(p);  // (0.44.4: Erasure Corps, Aggressive Broker)
            for (int x : spots) {
                PlanetState& ps = G.planets[size_t(x)];
                ps.coexist = uint16_t(ps.coexist & ~(1u << p));
                for (int q = 0; q < G.numPlayers; ++q)
                    if (q != p && groundForcesOn(G, q, x) > 0 && groundForcesOn(G, p, x) > 0) groundCombat(p, q, x);
                bool foes = neutralGroundOn(G, x) > 0;
                for (int q = 0; q < G.numPlayers; ++q) foes = foes || (q != p && groundForcesOn(G, q, x) > 0);
                if (!foes && groundForcesOn(G, p, x) > 0 && ps.owner != p) setControl(x, p);
            }
        }
    }
    if (dsbtReady(p, "ghoti")) {
        std::vector<Option> opts;
        if (G.fractureIn)
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (G.systems[s].ingress) {
                    opts.push_back({0, "Move an ingress token into an empty system", ""});
                    break;
                }
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (!hasShips(G, p, int(s))) continue;
            for (int16_t a : G.systems[s].adj)
                if (hasShips(G, p, a) && !otherShipsIn(G, p, a)) {
                    opts.push_back({1, "Move one of your ships next door to your ships", ""});
                    s = G.systems.size() - 1;
                    break;
                }
        }
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            const int c = fpick(p, Ab::DsbtGhoti, "Cult of the All-Mother: exhaust it?", opts);
            if (c == 0) {
                std::vector<Option> from, to;
                for (size_t s = 0; s < G.systems.size(); ++s) {
                    if (G.systems[s].ingress) from.push_back({int(s), sysName(int(s)), ""});
                    bool empty = !G.systems[s].isHyperlane() && !G.systems[s].fracture && !G.systems[s].ingress && G.systems[s].planets.empty();
                    for (int q = 0; q < kSides && empty && G.fixes < 8; ++q) empty = !hasUnits(G, q, int(s));  // (0.44.4: empty = no planets)
                    if (G.fixes >= 8 && G.systems[s].offBoard) empty = false;
                    if (empty) to.push_back({int(s), sysName(int(s)), ""});
                }
                if (!from.empty() && !to.empty()) {
                    G.players[size_t(p)].btExhausted = true;
                    const int a = from.size() == 1 ? from[0].id : fpick(p, Ab::DsbtGhoti, "Move which ingress token?", from, "", true);
                    const int b = fpick(p, Ab::DsbtGhoti, "Into which empty system?", to, "", true);
                    G.systems[size_t(a)].ingress = false;
                    G.systems[size_t(b)].ingress = true;
                    computeAdjacency(G);
                    dsbtEvent(E, p, "Cult of the All-Mother", "An ingress token drifts into " + sysName(b) + ".", b);
                }
            } else if (c == 1) {
                std::vector<Option> mv;  // id = from * 4096 + to * 16 + type
                for (size_t s = 0; s < G.systems.size(); ++s)
                    for (int t : kShipIdx) {
                        if (t == int(UnitType::Fighter) || !G.systems[s].space[size_t(p)].n[t]) continue;
                        for (int16_t a : G.systems[s].adj)
                            if (hasShips(G, p, a) && !otherShipsIn(G, p, a))
                                mv.push_back({int(s) * 4096 + a * 16 + t, std::string(unitTypeName(UnitType(t))) + ": " + sysName(int(s)) + " to " + sysName(a), ""});
                    }
                if (!mv.empty()) {
                    G.players[size_t(p)].btExhausted = true;
                    const int m = fpick(p, Ab::DsbtGhoti, "Move which ship?", mv);
                    const int s = m / 4096, a = (m / 16) % 256, t = m % 16;
                    Force& f = G.systems[size_t(s)].space[size_t(p)];
                    const bool dmg = f.dmg[t] >= f.n[t];
                    f.remove(UnitType(t), 1, false);
                    G.systems[size_t(a)].space[size_t(p)].add(UnitType(t), 1);
                    if (dmg) G.systems[size_t(a)].space[size_t(p)].dmg[t]++;
                    removeExcessCapacity(p, s);
                    if (G.fixes >= 8) enforceFleetLimit(p);  // (0.44.4)
                    dsbtEvent(E, p, "Cult of the All-Mother", pn(p) + "'s " + unitTypeName(UnitType(t)) + " drifts into " + sysName(a) + ".", a);
                }
            }
        }
    }
}

// Another player purged components (not action cards): Erasure Corps (Lanefir) - a command token, or it readies.
void Rules::dsbtPurged(int q) {
    for (int l = 0; l < G.numPlayers; ++l) {
        if (l == q || !dsbtOn(l, "lanefir")) continue;
        Player& pl = G.players[size_t(l)];
        if (pl.btExhausted && fyes(l, Ab::DsbtLanefir, "Erasure Corps: " + pn(q) + " purged something - ready your breakthrough?",
                                   "Otherwise you gain 1 command token.")) {
            pl.btExhausted = false;
            say(l, pn(l) + " readies Erasure Corps.");
        } else if (reinforcementTokens(l) > 0) {
            gainTokens(l, 1, false, "Erasure Corps: 1 command token.");
        }
    }
}

// Their component actions (and the free ones, id 700 + Ab).
void Rules::dsbtOptions(int p, std::vector<Option>& o) const {
    if (!(G.modules & kModDiscordant) || !hasBreakthrough(G, p) || G.players[size_t(p)].btExhausted) return;
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    if (fac(p, "cymiae") && !G.actionDeck.empty()) add(Ab::DsbtCymiae, "Breakthrough: Synaptic Interference", "Exhaust it to discard the top 3 action cards.");
    if (fac(p, "kolume")) add(Ab::DsbtKolume, "Breakthrough: Synchronicity VI", "Exhaust it: gain 1 command token, or research a technology of an exhausted one's colour.");
    if (fac(p, "nokar")) add(Ab::DsbtNokar, "Breakthrough: Mercenary Captains", "Exhaust it: 1 neutral cruiser and 2 neutral destroyers in a system without other ships.");
    if (fac(p, "bentor")) add(Ab::DsbtBentor, "Breakthrough: Historian Conclave", "Exhaust it: each other player explores 1 of their planets; 1 of your infantry may coexist on one.");
    if (fac(p, "edyn") && !G.players[size_t(p)].secrets.empty())
        add(Ab::DsbtEdyn, "Breakthrough: Celestial Ambassadors", "Exhaust it: show a player one of your secrets - they show you one of theirs, or let 1 of your ground forces coexist on their planet.");
}

bool Rules::dsbtAction(int p, int ab) {
    Player& pl = G.players[size_t(p)];
    switch (Ab(ab)) {
        case Ab::DsbtCymiae: {
            if (G.actionDeck.empty()) return false;
            pl.btExhausted = true;
            std::string names;
            for (int i = 0; i < 3 && !G.actionDeck.empty(); ++i) {
                const int c = G.actionDeck.back();
                G.actionDeck.pop_back();
                G.actionDiscard.push_back(int16_t(c));
                names += (names.empty() ? "" : ", ") + std::string(kActionCards[c].name);
            }
            dsbtEvent(E, p, "Synaptic Interference", pn(p) + " scrambles the galaxy's plans: " + names + " go to the discard pile.");
            return true;
        }
        case Ab::DsbtKolume: {
            std::vector<Option> opts;
            if (reinforcementTokens(p) > 0) opts.push_back({0, "Gain 1 command token", ""});
            int colours = 0;
            for (int16_t t : pl.exhaustedTechs)
                if (kTechs[t].color != TechColor::None) colours |= 1 << int(kTechs[t].color);
            if (colours && G.fixes >= 8) {  // 0.44.4: only when something of those colours can be researched (it's free)
                onlyColours_ = colours;
                researchDry_ = true;
                const bool can = researchStep(p, 0, "Synchronicity VI");
                researchDry_ = false;
                onlyColours_ = 0;
                if (can) opts.push_back({1, "Research a technology of an exhausted one's colour", "Free - any of those colours."});
            } else if (colours) {
                opts.push_back({1, "Research a technology of an exhausted one's colour", "Paid as usual."});
            }
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            const int c = fpick(p, Ab::DsbtKolume, "Synchronicity VI:", opts);
            if (c < 0) return false;
            pl.btExhausted = true;
            if (c == 0) {
                gainTokens(p, 1, false, "Synchronicity VI: 1 command token.");
            } else {
                if (G.fixes >= 8) {  // 0.44.4: every colour of an exhausted technology
                    onlyColours_ = colours;
                    researchStep(p, 0, "Synchronicity VI");
                    onlyColours_ = 0;
                    return true;
                }
                // (any colour of an exhausted technology: the research screen shows them all; the first exhausted colour leads)
                TechColor col = TechColor::None;
                for (int k = 0; k < 4 && col == TechColor::None; ++k)
                    if (colours & (1 << k)) col = TechColor(k);
                onlyColour_ = int(col);
                researchStep(p, 0, "Synchronicity VI");
                onlyColour_ = -1;
            }
            return true;
        }
        case Ab::DsbtNokar: {
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                if (G.systems[s].isHyperlane() || G.systems[s].fracture) continue;
                if (G.fixes >= 8 && G.systems[s].offBoard) continue;  // (0.44.4: on the board)
                bool ships = false;
                for (int q = 0; q < G.numPlayers && !ships; ++q) ships = hasShips(G, q, int(s));
                if (!ships) opts.push_back({int(s), sysName(int(s)), ""});
            }
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            const int s = fpick(p, Ab::DsbtNokar, "Mercenary Captains: the neutral fleet goes where?", opts, "1 neutral cruiser and 2 neutral destroyers.", true);
            if (s < 0) return false;
            pl.btExhausted = true;
            placeNeutral(UnitType::Cruiser, 1, s, -1, "Mercenary Captains");
            placeNeutral(UnitType::Destroyer, 2, s, -1, "Mercenary Captains");
            dsbtEvent(E, p, "Mercenary Captains", pn(p) + " hires a neutral fleet into " + sysName(s) + ".", s);
            return true;
        }
        case Ab::DsbtBentor: {  // Historian Conclave: everyone else explores; one infantry of ours coexists on one of those planets
            pl.btExhausted = true;
            dsbtEvent(E, p, "Historian Conclave", pn(p) + " convenes the historians: every other player explores a planet.");
            std::vector<int> explored;
            for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers)) {
                if (q == p || G.players[size_t(q)].eliminated) continue;
                std::vector<Option> opts;
                for (int x : controlledPlanets(G, q))
                    if (traitDeck2(kPlanets[G.planets[size_t(x)].def].trait) >= 0 && !G.planets[size_t(x)].destroyed) opts.push_back({x, planetName(x), ""});
                if (opts.empty()) continue;
                if (G.fixes >= 8) opts.push_back({-1, "Don't explore", ""});  // (0.44.4: "allow" - they may decline)
                const int x = opts.size() == 1 ? opts[0].id : fpick(q, Ab::DsbtBentor, "Historian Conclave: explore which of your planets?", opts, "", false, true);
                if (x < 0) continue;
                explorePlanet(q, x);
                explored.push_back(x);
            }
            std::vector<Option> to;
            for (int x : explored)
                if (G.planets[size_t(x)].owner != p && !isDMZ(G, x) && !arcaneShield(G, x) && unitsAvailable(G, p, UnitType::Infantry) > 0)
                    to.push_back({x, planetName(x), ""});
            if (!to.empty()) {
                to.push_back({-1, "No", ""});
                const int x = fpick(p, Ab::DsbtBentor, "Historian Conclave: 1 infantry coexisting on one of those planets?", to, "", false, true);
                if (x >= 0) {
                    addUnits(p, UnitType::Infantry, 1, G.planets[size_t(x)].system, x);
                    startCoexisting(p, x);
                }
            }
            return true;
        }
        case Ab::DsbtEdyn: {  // Celestial Ambassadors
            std::vector<Option> who;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && !G.players[size_t(q)].eliminated && (!G.players[size_t(q)].secrets.empty() || !controlledPlanets(G, q).empty()))
                    who.push_back({q, pn(q), std::to_string(G.players[size_t(q)].secrets.size()) + " unscored secrets"});
            if (who.empty() || pl.secrets.empty()) return false;
            who.push_back({-1, "Cancel", ""});
            const int q = fpick(p, Ab::DsbtEdyn, "Celestial Ambassadors: to which player?", who);
            if (q < 0) return false;
            std::vector<Option> mine;
            for (int16_t o : pl.secrets) mine.push_back({o, kObjectives[o].name, objectiveSummary(o)});
            const int shown = mine.size() == 1 ? mine[0].id : fpick(p, Ab::DsbtEdyn, "Show " + pn(q) + " which of your secrets?", mine);
            pl.btExhausted = true;
            dsbtEvent(E, p, "Celestial Ambassadors", pn(p) + "'s ambassadors show " + pn(q) + " a secret objective.");
            std::vector<Option> choice;
            if (!G.players[size_t(q)].secrets.empty()) choice.push_back({0, "Show them one of your secrets, at random", ""});
            bool planets = false;
            for (int x : controlledPlanets(G, q)) planets = planets || (!isDMZ(G, x) && !arcaneShield(G, x));
            if (planets && (unitsAvailable(G, p, UnitType::Infantry) > 0 || unitsAvailable(G, p, UnitType::Mech) > 0))
                choice.push_back({1, "Let 1 of their ground forces coexist on a planet of yours", ""});
            if (choice.empty()) return true;
            curFor_ = p;
            const int c = choice.size() == 1 ? choice[0].id
                          : G.fixes >= 8 ? fpick(q, Ab::DsbtEdyn, pn(p) + " showed you " + kObjectives[shown].name + ". In return:", choice)  // (0.44.4: the AI knows it)
                                         : pick(q, Tag::Generic, pn(p) + " showed you " + kObjectives[shown].name + ". In return:", choice);
            curFor_ = -1;
            if (c == 0) {
                const auto& sec = G.players[size_t(q)].secrets;
                const int o = sec[size_t(E.rng().below(int(sec.size())))];
                if (pl.human) pick(p, Tag::Generic, pn(q) + " shows you a secret objective", {{0, kObjectives[o].name, objectiveSummary(o)}});
                dsbtEvent(E, q, "Celestial Ambassadors", pn(q) + " shows " + pn(p) + " a secret objective in return.");
            } else {
                std::vector<Option> to;
                for (int x : controlledPlanets(G, q))
                    if (!isDMZ(G, x) && !arcaneShield(G, x)) to.push_back({x, planetName(x), ""});
                const int x = to.size() == 1 ? to[0].id : fpick(p, Ab::DsbtEdyn, "Celestial Ambassadors: coexist on which of " + pn(q) + "'s planets?", to, "", false, true);
                const UnitType t = unitsAvailable(G, p, UnitType::Infantry) > 0 ? UnitType::Infantry : UnitType::Mech;
                addUnits(p, t, 1, G.planets[size_t(x)].system, x);
                startCoexisting(p, x);
            }
            return true;
        }
        default: break;
    }
    return false;
}

// Erasure Corps (Lanefir): during the action phase, exhaust it to explore one of your planets (free option 643).
bool Rules::dsbtLanefirExplore(int p) {
    if (!dsbtReady(p, "lanefir")) return false;
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, p))
        if (traitDeck2(kPlanets[G.planets[size_t(x)].def].trait) >= 0 && !G.planets[size_t(x)].destroyed) opts.push_back({x, planetName(x), ""});
    if (opts.empty()) return false;
    opts.push_back({-1, "Cancel", ""});
    const int x = fpick(p, Ab::DsbtLanefir, "Erasure Corps: explore which of your planets?", opts, "", false, true);
    if (x < 0) return false;
    G.players[size_t(p)].btExhausted = true;
    explorePlanet(p, x);
    return true;
}

// The Tribune (Kortali, The Queens' Wrath): while undamaged its missed combat roll is rolled again; when it hits, it is
// repaired.
void Rules::dsbtTribune(int p, RollResult& r) {
    if (p < 0 || p >= G.numPlayers || std::strcmp(unitDef(G, p, UnitType::Dreadnought).key, "tribune3") || combatSys_ < 0) return;
    Force& f = G.systems[size_t(combatSys_)].space[size_t(p)];
    int undamaged = f.count(UnitType::Dreadnought) - f.dmg[int(UnitType::Dreadnought)];
    if (G.fixes >= 8) {  // 0.44.4: per Tribune (1 die each) - the undamaged ones reroll their misses, a damaged one that hits repairs itself
        bool rerolled = false;
        int fixed = 0, k = 0;
        for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i) {
            if (r.unit[i] != int(UnitType::Dreadnought)) continue;
            if (k < undamaged) {
                if (r.dice[i] < r.need[i]) {
                    r.dice[i] = int8_t(E.rng().d10());
                    rerolled = true;
                }
            } else if (r.dice[i] >= r.need[i] && fixed < f.dmg[int(UnitType::Dreadnought)]) {
                ++fixed;
            }
            ++k;
        }
        if (fixed > 0) f.dmg[int(UnitType::Dreadnought)] = uint8_t(f.dmg[int(UnitType::Dreadnought)] - fixed);
        if (rerolled || fixed) dsbtEvent(E, p, "Tribune", pn(p) + "'s Tribunes " + (rerolled ? "fire again" : "") + (rerolled && fixed ? " and " : "") +
                                                  (fixed ? "repair " + std::to_string(fixed) : "") + ".", combatSys_);
        return;
    }
    bool rerolled = false;
    for (size_t i = 0; i < r.dice.size(); ++i) {
        if (i >= r.unit.size() || r.unit[i] != int(UnitType::Dreadnought)) continue;
        if (r.dice[i] < r.need[i] && undamaged > 0) {
            r.dice[i] = int8_t(E.rng().d10());
            --undamaged;
            rerolled = true;
        }
    }
    int hits = 0;
    for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i) hits += r.unit[i] == int(UnitType::Dreadnought) && r.dice[i] >= r.need[i] ? 1 : 0;
    const int fixed = std::min<int>(hits, f.dmg[int(UnitType::Dreadnought)]);
    if (fixed > 0) f.dmg[int(UnitType::Dreadnought)] = uint8_t(f.dmg[int(UnitType::Dreadnought)] - fixed);
    if (rerolled || fixed) dsbtEvent(E, p, "Tribune", pn(p) + "'s Tribunes " + (rerolled ? "fire again" : "") + (rerolled && fixed ? " and " : "") +
                                              (fixed ? "repair " + std::to_string(fixed) : "") + ".", combatSys_);
}


// ================================================================== batch 2
// The frontier deck in a system, whether or not it has a frontier token (Historian Conclave, Aurora Research Base).
void Rules::exploreFrontierDeck(int p, int sys) {
    if (sys < 0) return;
    SystemState& s = G.systems[size_t(sys)];
    const bool had = s.frontier;
    s.frontier = true;
    exploreFrontier(p, sys);
    s.frontier = had && s.frontier;
}

// Historian Conclave (Bentor): exploring a planet - exhaust it to explore that system's frontier deck instead.
bool Rules::dsbtExploreInstead(int p, int planet) {
    if (!dsbtReady(p, "bentor") || planet < 0) return false;
    const int sys = G.planets[size_t(planet)].system;
    if (!fyes(p, Ab::DsbtBentor, "Historian Conclave: explore the frontier deck in " + sysName(sys) + " instead of " + planetName(planet) + "?",
              "The breakthrough is exhausted."))
        return false;
    G.players[size_t(p)].btExhausted = true;
    dsbtEvent(E, p, "Historian Conclave", pn(p) + "'s historians look past " + planetName(planet) + " to the frontier.", sys);
    exploreFrontierDeck(p, sys);
    return true;
}

// The Collector's Museum (Kollecc): exploring a planet - a card from that deck's discard pile instead (exhausted); it
// goes back into the deck afterwards. Returns the card, or -1.
int Rules::dsbtMuseum(int p, int type) {
    if (!dsbtReady(p, "kollecc") || type < 0 || G.exploreDiscard[type].empty()) return -1;
    std::vector<Option> opts;
    for (int16_t c : G.exploreDiscard[type]) {
        bool dup = false;
        for (const Option& o : opts) dup = dup || o.id == c;
        if (!dup) opts.push_back({c, kExploreCards[c].name, exploreText(c)});
    }
    opts.push_back({-1, "Draw as usual", ""});
    const int c = fpick(p, Ab::DsbtKollecc, "The Collector's Museum: resolve a card from the discard pile instead?", opts,
                        "The breakthrough is exhausted; the card is shuffled back into its deck afterwards.");
    if (c < 0) return -1;
    G.players[size_t(p)].btExhausted = true;
    auto& d = G.exploreDiscard[type];
    d.erase(std::find(d.begin(), d.end(), int16_t(c)));
    dsbtEvent(E, p, "The Collector's Museum", pn(p) + " takes " + kExploreCards[c].name + " out of the museum.");
    return c;
}

// Galactic Movement (Free Systems): after spending a strategy token (a secondary in someone's strategic action) - 1
// infantry coexisting on a cultural, hazardous or industrial planet with the active player's ground forces.
void Rules::dsbtSpentStrategy(int p) {
    const int a = G.activePlayer;
    if (!dsbtOn(p, "freesystems") || a < 0 || a == p || unitsAvailable(G, p, UnitType::Infantry) <= 0) return;
    std::vector<Option> opts;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.destroyed || isDMZ(G, int(x)) || arcaneShield(G, int(x)) || groundForcesOn(G, a, int(x)) == 0 || groundForcesOn(G, p, int(x)) > 0) continue;
        if (!hasTraitOf(G, int(x), Trait::Cultural) && !hasTraitOf(G, int(x), Trait::Hazardous) && !hasTraitOf(G, int(x), Trait::Industrial)) continue;
        opts.push_back({int(x), planetName(int(x)), ""});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "No", ""});
    const int x = fpick(p, Ab::DsbtFreesys, "Galactic Movement: 1 infantry coexisting on one of " + pn(a) + "'s planets?", opts, "", false, true);
    if (x < 0) return;
    addUnits(p, UnitType::Infantry, 1, G.planets[size_t(x)].system, x);
    startCoexisting(p, x);
    dsbtEvent(E, p, "Galactic Movement", pn(p) + "'s people settle among " + pn(a) + "'s on " + planetName(x) + ".", G.planets[size_t(x)].system);
}

// (0.44.9, fixes 13 - the owner's answer B1) infantry moved "into coexistence" onto a planet with nobody else's units
// on it: there is nothing to coexist with - the planet changes hands without a fight. True when it did.
bool Rules::takeEmptyPlanet(int p, int x) {
    if (G.fixes < 13) return false;
    for (int q = 0; q < kSides; ++q)
        if (q != p && G.planets[size_t(x)].units[size_t(q)].total() > 0) return false;
    const int old = G.planets[size_t(x)].owner;
    setControl(x, p);
    say(p, pn(p) + "'s infantry take " + planetName(x) + (old >= 0 ? " from " + pn(old) : std::string()) + " - nobody was there to stop them.",
        G.planets[size_t(x)].system);
    return true;
}

// Professional Intrigue (Li-Zho): after a tactical action in a non-home system, 1 of their infantry from the board moves
// there, coexisting on a planet.
void Rules::dsbtEndTactical(int p, int sys) {
    if (!dsbtOn(p, "lizho") || sys < 0 || G.systems[size_t(sys)].home >= 0) return;
    std::vector<Option> to;
    for (int16_t x : G.systems[size_t(sys)].planets) {
        const PlanetState& ps = G.planets[size_t(x)];
        if (ps.destroyed || isDMZ(G, x) || arcaneShield(G, x) || ps.owner == p || ps.owner < 0) continue;
        if ((ps.coexist >> p) & 1) continue;
        if (G.fixes >= 8) {  // 0.44.4: coexisting needs another player's units there (an empty planet would just be taken)
            bool units = false;
            for (int q = 0; q < kSides; ++q) units = units || (q != p && ps.units[size_t(q)].total() > 0);
            if (!units && G.fixes < 13) continue;  // (0.44.9, fixes 13 - the owner's answer B1: an empty one is simply taken)
        }
        to.push_back({x, planetName(x) + " (" + pn(ps.owner) + ")", ""});
    }
    std::vector<Option> from;  // planets of theirs with infantry elsewhere
    if (G.fixes >= 8) {  // 0.44.4: "1 of your infantry on the game board" - any planet, or a space area (id 100000 + system)
        for (size_t y = 0; y < G.planets.size(); ++y)
            if (G.planets[y].units[size_t(p)].count(UnitType::Infantry) > 0 && G.planets[y].system != sys) from.push_back({int(y), planetName(int(y)), ""});
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (G.systems[s].space[size_t(p)].count(UnitType::Infantry) > 0) from.push_back({100000 + int(s), "Space: " + sysName(int(s)), ""});
    } else {
        for (int y : controlledPlanets(G, p))
            if (G.planets[size_t(y)].system != sys && G.planets[size_t(y)].units[size_t(p)].count(UnitType::Infantry) > 0) from.push_back({y, planetName(y), ""});
    }
    if (to.empty() || from.empty()) return;
    to.push_back({-1, "No", ""});
    const int x = fpick(p, Ab::DsbtLizho, "Professional Intrigue: slip 1 infantry onto a planet here, coexisting?", to, "", false, true);
    if (x < 0) return;
    const int y = from.size() == 1 ? from[0].id : fpick(p, Ab::DsbtLizho, "Professional Intrigue: the infantry comes from which planet?", from, "", false, true);
    if (y >= 100000) G.systems[size_t(y - 100000)].space[size_t(p)].remove(UnitType::Infantry, 1, false);
    else G.planets[size_t(y)].units[size_t(p)].remove(UnitType::Infantry, 1, false);
    G.planets[size_t(x)].units[size_t(p)].add(UnitType::Infantry, 1);
    if (takeEmptyPlanet(p, x)) return;  // (0.44.9: nobody there - the planet is theirs, no coexistence)
    startCoexisting(p, x);
    dsbtEvent(E, p, "Professional Intrigue", pn(p) + " plants an agent on " + planetName(x) + ".", sys);
}

// Someone ended owner's coexistence (they attacked it): Professional Intrigue - they discard an action card;
// Hidden Hardpoints - the owner draws a secret objective.
void Rules::dsbtCoexistEnded(int owner, int by) {
    if (owner < 0 || owner >= G.numPlayers || by < 0 || by >= G.numPlayers) return;
    if (dsbtOn(owner, "lizho") && !G.players[size_t(by)].actionCards.empty()) {
        std::vector<Option> opts;
        for (int16_t c : G.players[size_t(by)].actionCards) opts.push_back({c, kActionCards[c].name, ""});
        const int c = opts.size() == 1 ? opts[0].id : pick(by, Tag::DiscardActionCard, "Professional Intrigue: discard an action card", opts,
                                                             "You ended " + pn(owner) + "'s coexistence.");
        auto& hand = G.players[size_t(by)].actionCards;
        hand.erase(std::find(hand.begin(), hand.end(), int16_t(c)));
        discardActionCard(by, c, false);
    }
    if (dsbtOn(owner, "tnelis")) {
        drawSecret(owner);
        say(owner, pn(owner) + " draws a secret objective (Hidden Hardpoints).");
    }
}

// End of a ground combat round: Hidden Hardpoints (Tnelis) - discard a secret, their units there coexist instead (the
// combat ends: true); Dreamwalkers (Myko-Mentori) - on their planet, a 7+ brings back a captured infantry as theirs.
bool Rules::dsbtGroundRoundEnd(int att, int def, int planet) {
    const int sys = G.planets[size_t(planet)].system;
    for (int side : {att, def}) {
        if (side >= G.numPlayers || !dsbtOn(side, "tnelis") || G.players[size_t(side)].secrets.empty()) continue;
        const int other = side == att ? def : att;
        if (groundForcesOn(G, side, planet) == 0 || (other < G.numPlayers ? groundForcesOn(G, other, planet) : neutralGroundOn(G, planet)) == 0) continue;
        std::vector<Option> opts;
        for (int16_t o : G.players[size_t(side)].secrets) opts.push_back({o, kObjectives[o].name, objectiveSummary(o)});
        opts.push_back({-1, "Keep fighting", ""});
        curValue_ = groundForcesOn(G, side, planet) - (other < G.numPlayers ? groundForcesOn(G, other, planet) : neutralGroundOn(G, planet));
        const int o = fpick(side, Ab::DsbtTnelis, "Hidden Hardpoints: discard a secret objective so your units here coexist?", opts,
                            "The ground combat ends; your units stay on " + planetName(planet) + " beside theirs.");
        curValue_ = 0;
        if (o < 0) continue;
        auto& sec = G.players[size_t(side)].secrets;
        sec.erase(std::find(sec.begin(), sec.end(), int16_t(o)));
        G.secretDeck.insert(G.secretDeck.begin(), int16_t(o));
        startCoexisting(side, planet);
        dsbtEvent(E, side, "Hidden Hardpoints", pn(side) + "'s forces vanish into hidden hardpoints on " + planetName(planet) + ".", sys);
        return true;
    }
    const int my = G.planets[size_t(planet)].owner;
    if (my >= 0 && (my == att || my == def) && dsbtOn(my, "mykomentori") && unitsAvailable(G, my, UnitType::Infantry) > 0) {
        auto& cap = G.players[size_t(my)].captured;
        auto it = std::find_if(cap.begin(), cap.end(), [](int16_t c) { return c % 16 == int(UnitType::Infantry); });
        if (it != cap.end()) {
            const int v = E.rng().d10();
            if (G.fixes >= 8) {  // 0.44.4: the roll is shown, hit or miss
                RollResult r;
                r.dice.push_back(int8_t(v));
                r.need.push_back(7);
                r.hits = v >= 7 ? 1 : 0;
                reportRoll(my, sys, "Dreamwalkers", r);
            }
            if (v >= 7) {
                cap.erase(it);
                addUnits(my, UnitType::Infantry, 1, sys, planet);
                dsbtEvent(E, my, "Dreamwalkers", pn(my) + " rolls " + std::to_string(v) + ": a captured soldier walks out of the dream as theirs.", sys);
            }
        }
    }
    return false;
}

// Dreamwalkers (Myko-Mentori): other players' infantry destroyed in a combat on their planet are captured.
void Rules::dsbtInfantryDied(int owner, int planet, int k) {
    if (planet < 0 || k <= 0 || owner < 0 || owner >= G.numPlayers || G.groundCombatPlanet != planet) return;
    const int my = G.planets[size_t(planet)].owner;
    if (my < 0 || my == owner || !dsbtOn(my, "mykomentori")) return;
    if (G.fixes >= 8) {  // 0.44.4: through capture() (the blockade rule, LRR 17.6)
        capture(my, owner, UnitType::Infantry, k, "Dreamwalkers");
        return;
    }
    for (int i = 0; i < k; ++i) G.players[size_t(my)].captured.push_back(int16_t(kCapturedToken * 16 + int(UnitType::Infantry)));
    say(my, pn(my) + " captures " + std::to_string(k) + " infantry (Dreamwalkers).", G.planets[size_t(planet)].system);
}

// Insurrectionist Networking (Olradin): once per action, after a policy ability - 1 trade good: 1 of their infantry
// moves, coexisting, onto a planet next door.
void Rules::dsbtPolicy(int p) {
    if (!dsbtOn(p, "olradin") || (dsOncePerAction_ & 16) || G.players[size_t(p)].tradeGoods < 1) return;
    std::vector<Option> opts;  // id = from * 1000 + to
    for (int y : controlledPlanets(G, p)) {
        if (G.planets[size_t(y)].units[size_t(p)].count(UnitType::Infantry) == 0) continue;
        for (int16_t a : G.systems[size_t(G.planets[size_t(y)].system)].adj)
            for (int16_t x : G.systems[size_t(a)].planets) {
                const PlanetState& ps = G.planets[size_t(x)];
                if (ps.owner < 0 || ps.owner == p || ps.destroyed || isDMZ(G, x) || arcaneShield(G, x) || ((ps.coexist >> p) & 1)) continue;
                if (G.fixes >= 8) {  // 0.44.4: coexisting needs another player's units there (as Professional Intrigue)
                    bool units = false;
                    for (int q = 0; q < kSides; ++q) units = units || (q != p && ps.units[size_t(q)].total() > 0);
                    if (!units && G.fixes < 13) continue;  // (0.44.9: an empty one is taken - B1)
                }
                opts.push_back({y * 1000 + x, planetName(y) + " to " + planetName(x) + " (" + pn(ps.owner) + ")", ""});
            }
    }
    if (opts.empty()) return;
    opts.push_back({-1, "No", ""});
    const int c = fpick(p, Ab::DsbtOlradin, "Insurrectionist Networking: spend 1 trade good to move 1 infantry, coexisting, next door?", opts);
    if (c < 0) return;
    dsOncePerAction_ = uint8_t(dsOncePerAction_ | 16);
    const int y = c / 1000, x = c % 1000;
    G.players[size_t(p)].tradeGoods = int8_t(G.players[size_t(p)].tradeGoods - 1);
    G.planets[size_t(y)].units[size_t(p)].remove(UnitType::Infantry, 1, false);
    G.planets[size_t(x)].units[size_t(p)].add(UnitType::Infantry, 1);
    if (takeEmptyPlanet(p, x)) return;  // (0.44.9)
    startCoexisting(p, x);
    dsbtEvent(E, p, "Insurrectionist Networking", pn(p) + "'s insurgents slip onto " + planetName(x) + ".", G.planets[size_t(x)].system);
}

// Gravitic Disruption (Vaylerian): after moving ships into the active system - discard an action card to send one of
// them on to a system next door without other players' ships.
void Rules::dsbtAfterMovement(int p, int sys) {
    if (!dsbtOn(p, "vaylerian") || G.players[size_t(p)].actionCards.empty()) return;
    std::vector<Option> opts;  // id = to * 16 + type
    for (int t : kShipIdx) {
        if (t == int(UnitType::Fighter) || !G.systems[size_t(sys)].space[size_t(p)].n[t]) continue;
        for (int16_t a : G.systems[size_t(sys)].adj)
            if (!G.systems[size_t(a)].isHyperlane() && !otherShipsIn(G, p, a) && shipsMayEnter(G, p, a))
                opts.push_back({a * 16 + t, std::string(unitTypeName(UnitType(t))) + " to " + sysName(a), ""});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "No", ""});
    const int c = fpick(p, Ab::DsbtVaylerian, "Gravitic Disruption: discard an action card to send one of these ships next door?", opts, "", false);
    if (c < 0) return;
    std::vector<Option> cards;
    for (int16_t k : G.players[size_t(p)].actionCards) cards.push_back({k, kActionCards[k].name, ""});
    const int k = cards.size() == 1 ? cards[0].id : fpick(p, Ab::DsbtVaylerian, "Gravitic Disruption: discard which action card?", cards);
    auto& hand = G.players[size_t(p)].actionCards;
    hand.erase(std::find(hand.begin(), hand.end(), int16_t(k)));
    discardActionCard(p, k, false);
    const int a = c / 16, t = c % 16;
    Force& f = G.systems[size_t(sys)].space[size_t(p)];
    const bool dmg = f.dmg[t] >= f.n[t];
    f.remove(UnitType(t), 1, false);
    G.systems[size_t(a)].space[size_t(p)].add(UnitType(t), 1);
    if (dmg) G.systems[size_t(a)].space[size_t(p)].dmg[t]++;
    removeExcessCapacity(p, sys);
    dsbtEvent(E, p, "Gravitic Disruption", pn(p) + "'s " + unitTypeName(UnitType(t)) + " rides the gravity wake into " + sysName(a) + ".", a);
}

// Mirage Craft (Mortheus): defending a space combat - swap one of their ships there with one of theirs in a system with
// their command token (the ships move alone).
void Rules::dsbtSpaceCombatStart(int att, int def, int sys) {
    (void)att;
    if (def < 0 || def >= G.numPlayers || !dsbtOn(def, "mortheus")) return;
    std::vector<Option> out, in;
    const bool all = G.fixes >= 8;  // 0.44.4: "1 of your ships" - fighters too; the other system may hold other ships
    for (int t : kShipIdx)
        if ((all || t != int(UnitType::Fighter)) && G.systems[size_t(sys)].space[size_t(def)].n[t])
            out.push_back({t, std::string(unitTypeName(UnitType(t))) + " here", ""});
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (int(s) == sys || !G.systems[s].hasToken(def) || (!all && otherShipsIn(G, def, int(s)))) continue;
        for (int t : kShipIdx)
            if ((all || t != int(UnitType::Fighter)) && G.systems[s].space[size_t(def)].n[t])
                in.push_back({int(s) * 16 + t, std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)), ""});
    }
    if (out.empty() || in.empty()) return;
    in.push_back({-1, "No swap", ""});
    const int c = fpick(def, Ab::DsbtMortheus, "Mirage Craft: swap in one of your ships from a system with your command token?", in,
                        "It changes places with one of your ships here (neither carries anything).");
    if (c < 0) return;
    const int t2 = out.size() == 1 ? out[0].id : fpick(def, Ab::DsbtMortheus, "Mirage Craft: which ship here changes places?", out);
    const int s = c / 16, t1 = c % 16;
    Force& here = G.systems[size_t(sys)].space[size_t(def)];
    Force& there = G.systems[size_t(s)].space[size_t(def)];
    const bool d1 = there.dmg[t1] >= there.n[t1], d2 = here.dmg[t2] >= here.n[t2];
    there.remove(UnitType(t1), 1, false);
    here.remove(UnitType(t2), 1, false);
    here.add(UnitType(t1), 1);
    there.add(UnitType(t2), 1);
    if (d1) here.dmg[t1]++;
    if (d2) there.dmg[t2]++;
    removeExcessCapacity(def, sys);
    removeExcessCapacity(def, s);
    dsbtEvent(E, def, "Mirage Craft", pn(def) + "'s " + unitTypeName(UnitType(t2)) + " shimmers away - a " + unitTypeName(UnitType(t1)) + " stands in its place.", sys);
}

// Mended Grove (Khrask): Meteor Slings with every hit cancelled - the infantry may coexist instead (not committed).
bool Rules::dsbtSlingsCoexist(int p, int planet, int n) {
    if (!dsbtOn(p, "khrask") || n <= 0) return false;
    if (!fyes(p, Ab::DsbtKhrask, "Mended Grove: your " + std::to_string(n) + " infantry on " + planetName(planet) + " coexist instead of fighting?",
              "They can't be committed in this action."))
        return false;
    startCoexisting(p, planet);
    groveNoCommit_ = planet;  // (0.44.4: not committed again this action)
    groveTurn_ = G.turnCount;
    return true;
}


// ================================================================== batch 3
// Exodus Engineering (Dih-Mohn): once per space combat, after an opponent's non-fighter ship is destroyed - exhaust it to
// put a frontier token there, or produce 1 or 2 non-fighter ships there to ready it (read: producing needs no exhausting
// and readies an exhausted card).
void Rules::dsbtShipsLost(int victim, int opp, int sys, const Force& before) {
    const int exodusBit = G.fixes >= 8 ? 16 : 15;  // (0.44.4: bit 15 was also Queen Lorena's for seat 7)
    if (opp < 0 || opp >= G.numPlayers || !dsbtOn(opp, "dihmohn") || ((dsOncePerCombat_ >> exodusBit) & 1)) return;
    if (before.nonFighterShips() <= G.systems[size_t(sys)].space[size_t(victim)].nonFighterShips()) return;
    const bool ready = !G.players[size_t(opp)].btExhausted;
    std::vector<Option> opts;
    if (ready && !G.systems[size_t(sys)].frontier && G.systems[size_t(sys)].planets.empty())
        opts.push_back({0, "Exhaust it: a frontier token here", ""});
    if (spendable(G, opp, false) >= 1)
        opts.push_back({1, "Produce 1 or 2 non-fighter ships here", ready ? "Paid as usual." : "Paid as usual; the card readies."});
    if (opts.empty()) return;
    opts.push_back({-1, "No", ""});
    const int c = fpick(opp, Ab::DsbtDihmohn, "Exodus Engineering: one of " + pn(victim) + "'s ships is gone - use it?", opts);
    if (c < 0) return;
    dsOncePerCombat_ = dsOncePerCombat_ | (1u << exodusBit);
    const bool wasExhausted = G.players[size_t(opp)].btExhausted;
    G.players[size_t(opp)].btExhausted = c == 0;
    if (c == 0) {
        G.systems[size_t(sys)].frontier = true;
        dsbtEvent(E, opp, "Exodus Engineering", pn(opp) + " marks the wreck field in " + sysName(sys) + ": a frontier token.", sys);
    } else {
        dsbtEvent(E, opp, "Exodus Engineering", pn(opp) + "'s flotilla builds in the middle of the battle.", sys);
        G.fx.noBlockade = true;
        const int was = dsTypeMask_;
        dsTypeMask_ = (kShipMaskAll) & ~(1 << int(UnitType::Fighter));  // (non-fighter ships)
        const int shipsBefore = G.systems[size_t(sys)].space[size_t(opp)].nonFighterShips();
        productionCore(opp, sys, 2, false, true);
        dsTypeMask_ = was;
        G.fx.noBlockade = false;
        // 0.44.4 (fixes 8): the card readies only if ships were produced
        if (G.fixes >= 8 && G.systems[size_t(sys)].space[size_t(opp)].nonFighterShips() <= shipsBefore)
            G.players[size_t(opp)].btExhausted = wasExhausted;
    }
}

// Raiding Parties (Ghemina): announcing a retreat - exhaust it to retreat at once (no more dice this round).
bool Rules::dsbtQuickRetreat(int who, int sys) {
    if (!dsbtReady(who, "ghemina")) return false;
    const std::vector<int> dest = retreatOptions(who, sys);
    if (dest.empty() || !fyes(who, Ab::DsbtGhemina, "Raiding Parties: exhaust it to retreat at once?", "No more dice this round.")) return false;
    G.players[size_t(who)].btExhausted = true;
    std::vector<Option> opts;
    for (int x : dest) opts.push_back({x, sysName(x), ""});
    const int to = opts.size() == 1 ? opts[0].id : pick(who, Tag::RetreatTo, "Retreat to which system?", opts, "", true);
    dsbtEvent(E, who, "Raiding Parties", pn(who) + "'s raiders vanish before the next volley.", sys);
    doRetreat(who, sys, to);
    return true;
}

// Combat dice: Raiding Parties (Ghemina) - one unit rolls twice and keeps the better; Awakened Planetoids (Zelian) - their
// planets in the active system add a die each (combat 10 minus resources, at least 1).
void Rules::dsbtCombatDice(int p, RollResult& r) {
    if (p < 0 || p >= G.numPlayers) return;
    if (dsbtOn(p, "ghemina") && !r.dice.empty()) {
        int best = -1;  // the unit with the most dice that can still miss (the surest gain)
        if (G.fixes >= 8) {  // 0.44.4: the unit whose second roll gains most - dice x chance to hit x chance to miss
            float bestGain = -1.f;
            for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i) {
                if (r.unit[i] < 0) continue;
                const float hit = std::min(1.f, std::max(0.f, float(11 - r.need[i]) / 10.f));
                const float gain = float(std::max(1, int(unitDef(G, p, UnitType(r.unit[i])).combatDice))) * hit * (1.f - hit);
                if (gain > bestGain + 1e-4f) bestGain = gain, best = int(i);
            }
        } else {
            for (size_t i = 0; i < r.dice.size(); ++i)
                if (i < r.unit.size() && r.unit[i] >= 0 && (best < 0 || r.need[i] < r.need[size_t(best)])) best = int(i);
        }
        if (best >= 0) {
            const int t = r.unit[size_t(best)];
            int dice = std::max(1, int(unitDef(G, p, UnitType(t)).combatDice));
            std::vector<size_t> idx;
            for (size_t i = 0; i < r.dice.size() && int(idx.size()) < dice; ++i)
                if (i < r.unit.size() && r.unit[i] == t) idx.push_back(i);
            int now = 0, alt = 0;
            std::vector<int8_t> second;
            for (size_t i : idx) {
                now += r.dice[i] >= r.need[i] ? 1 : 0;
                second.push_back(int8_t(E.rng().d10()));
                alt += second.back() >= r.need[i] ? 1 : 0;
            }
            if (alt > now)
                for (size_t k = 0; k < idx.size(); ++k) r.dice[idx[k]] = second[k];
            if (G.fixes >= 8)  // 0.44.4: say so
                say(p, pn(p) + "'s " + unitTypeName(UnitType(t)) + " rolls twice (Raiding Parties): " + std::to_string(now) + " or " +
                           std::to_string(alt) + " hits - " + std::to_string(std::max(now, alt)) + " kept.", combatSys_);
        }
    }
    if (dsbtOn(p, "zelian")) {
        int sys = -1, planet = -1;
        if (inGroundCombat_) planet = G.groundCombatPlanet, sys = planet >= 0 ? G.planets[size_t(planet)].system : -1;
        else if (inSpaceCombat_) sys = combatSys_;
        if (sys >= 0 && sys == G.activeSystem) {
            int added = 0;
            for (int16_t x : G.systems[size_t(sys)].planets) {
                if (G.planets[size_t(x)].owner != p || G.planets[size_t(x)].destroyed) continue;
                if (planet >= 0 && (x != planet || groundForcesOn(G, p, x) == 0)) continue;
                const int need = std::max(1, 10 - planetResources(G, x));
                r.dice.push_back(int8_t(E.rng().d10()));
                r.need.push_back(int8_t(need));
                r.unit.push_back(-1);
                r.bonus9.push_back(0);
                ++added;
            }
            if (added) say(p, pn(p) + "'s planets join the fight (Awakened Planetoids).", sys);
        }
    }
}

// Matriphagy (Cheiran): end of their turn - one of their non-fighter ships becomes units costing less in total.
void Rules::dsbtTurnEndCheiran(int p) {
    if (!dsbtReady(p, "cheiran")) return;
    std::vector<Option> opts;  // id = system * 16 + type
    for (size_t s = 0; s < G.systems.size(); ++s)
        for (int t : kShipIdx)
            if (t != int(UnitType::Fighter) && G.systems[s].space[size_t(p)].n[t] && unitCost2(G, p, UnitType(t)) >= 4)
                opts.push_back({int(s) * 16 + t, std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)),
                                "Units costing up to " + std::to_string(unitCost2(G, p, UnitType(t)) / 2 - 1) + " in total replace it."});
    if (opts.empty()) return;
    opts.push_back({-1, "No", ""});
    const int c = fpick(p, Ab::DsbtCheiran, "Matriphagy: one of your ships gives birth to cheaper units?", opts);
    if (c < 0) return;
    const int s = c / 16, t = c % 16;
    const int cap = unitCost2(G, p, UnitType(t)) / 2 - 1;
    G.players[size_t(p)].btExhausted = true;
    const bool was = removing_;
    removing_ = true;
    destroyUnits(p, UnitType(t), 1, s, -1, -1, "Matriphagy");
    removing_ = was;
    dsbtEvent(E, p, "Matriphagy", pn(p) + "'s " + unitTypeName(UnitType(t)) + " splits open in " + sysName(s) + ".", s);
    dsFreeProd_ = true;
    dsCostCap_ = std::max(1, cap);
    G.fx.noBlockade = true;
    productionCore(p, s, 8, false, false);
    G.fx.noBlockade = false;
    dsCostCap_ = 0;
    dsFreeProd_ = false;
}

// High Security Vault (Vaden): another player taking their trade goods, commodities or relic fragments needs their
// leave. Returns true if it is blocked.
bool Rules::dsbtVault(int victim, int taker, const std::string& what) {
    if (victim < 0 || victim >= G.numPlayers || victim == taker || !dsbtOn(victim, "vaden")) return false;
    curFor_ = taker;
    const bool allow = G.players[size_t(victim)].human &&
                       fyes(victim, Ab::DsbtVaden, "High Security Vault: let " + pn(taker) + " take " + what + "?", "Your vault keeps it unless you allow it.");
    curFor_ = -1;
    if (!allow) dsbtEvent(E, victim, "High Security Vault", pn(victim) + "'s vault stays shut: " + pn(taker) + " takes nothing.");
    return !allow;
}

}  // namespace ti
