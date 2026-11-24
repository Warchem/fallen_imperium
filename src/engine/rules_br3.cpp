// Blue Reverie (0.85.0, TacticBlue's homebrew - module kModBlueReverie): the Pharad'n Order. The cards' exact wording (owner
// 2026-11-21); the readings follow AsyncTI4's (src/main/java: ButtonHelper.resolveInfantryDestroy, ButtonHelperCommanders,
// ButtonHelperAgents.pharadnAgentKill, PharadnTechHandler, PharadnBreakthroughHandler) - sent to the owner 2026-11-24 09:41 CST (his
// answers 11:14: all OK, with 5 (Nokash sellable), 6 (Avhkan: any infantry), 10 (the tile on the map); 13:08: 1 as read - only the
// Pharad'n purge a tomb, when they take its planet):
//   Ancient Empire     "During setup, place 1 tomb token on each of up to 14 planets. When you gain control of a planet that contains a
//                      tomb token, purge that token to capture 2 infantry from the supply." (PlanetState::tomb: pharSetupTombs,
//                      pharControl from setControl - OWNER 13:08: "only when the pharad'n player takes control of a planet with the
//                      tomb token. The pharad'n player must purge it to capture the 2 infantry"; another player's taking leaves it)
//   Dark Purpose       "When you score an objective: Gain 1 command token." (pharScored)
//   Mark of Pharad'n   "Abilities that would allow you to place infantry on the game board instead allow you to produce those units.
//                      When you produce infantry, you must return 1 of your captured infantry to produce each of those units, instead
//                      of spending resources." - captured infantry are tokens (LRR 17.3); query unitsAvailable / productionCost, and
//                      addUnits returns one per infantry placed (pharPlace)
//   Immortal I / II    "After this unit is destroyed or removed, capture this unit. Game effects cannot prevent you from using this
//                      ability." II: "DEPLOY: At the start of your turn, you may place 1 infantry on a planet you control."
//                      (pharInfantryLost from destroyUnits - destroyed or removed; pharRemoved at the effects that take infantry off
//                      the board without it; pharTurnStart)
//   Sacrotitan (mech)  "DEPLOY: Once per action, after 2 or more of your infantry are destroyed on a planet, you may place 1 mech from
//                      your reinforcements on that planet." (2 or more in one go - one combat round, one effect)
//   Sedtra (flagship)  "During movement, apply +1 to this unit's move value for each ground force it transports." (query moveValue)
//   Nokash (agent)     "At any time: Exhaust this card to allow a player to destroy all their infantry on 1 planet they control. For
//                      each infantry destroyed, that player gains 1 commodity. Then, that player converts all of their commodities to
//                      trade goods." (their turn: option 670)
//   Avhkan (commander) "At the start of ground combat on a planet you control: You may destroy 1 infantry on that planet" - OWNER
//                      11:14: any infantry there, theirs too, their choice; unlocked by its "ACTION: Destroy 2 of your infantry on each
//                      of 5 planets."
//   Pharad'n the Immortal (hero) Black Star - Apocalypse: "After a player commits ground forces to a planet that contains 1 of your
//                      structures: You may purge this card to destroy all infantry on that planet, or commit any number of your
//                      captured infantry to that planet as if they were your own."
//   Death Binding      "When the Pharad'n player passes, remove 4 of your infantry from the game board to gain 1 technology with no
//                      prerequisites. Then, return this card to the Pharad'n player; that player captures 1 infantry from the supply."
//   Reality-Field Impactor "After you activate a system, you may choose 1 anomaly on the game board; that anomaly has no effect until
//                      the end of this tactical action." (TacticalEffects::rfiSys)
//   Hidden Vaults (breakthrough) "When you explore a planet, you may produce 1 infantry on that planet. At any time, you may exhaust this
//                      card to destroy any number of your units on the game board." (their turn: option 671)
//   Black Pyramid (Monuments+) "This unit is treated as a structure and a ground force. DEPLOY: During your "Commit Ground Forces" step,
//                      if you control a planet in the active system, place this unit on a planet in that system." (query pyramidOn;
//                      pharLanded)
#include <algorithm>
#include <cstring>
#include <map>

#include "engine/rules.h"

namespace ti {

namespace {
void phEvent(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    if (text.find(name) == std::string::npos) {  // the ability's name in the line (feed, game log, simulator counts)
        e.text = text.size() && text.back() == '.' ? text.substr(0, text.size() - 1) : text;
        e.text += " (" + name + ").";
    }
    e.system = int16_t(sys);
    E.emit(e);
}
int32_t actionNow(const Game& g) { return int32_t(g.turnCount) + 1; }  // "once per action"
std::string infantryCount(int k) { return std::to_string(k) + " infantry"; }
constexpr int kVaultSpace = 10000000;  // Hidden Vaults' unit ids: planet * 128 + type * 8 + i, or kVaultSpace + system * 128 + ...
}  // namespace

// ------------------------------------------------------------------ Ancient Empire
// "During setup, place 1 tomb token on each of up to 14 planets." Any planet on the board (AsyncTI4's list), their own home planets
// aside - they hold them already.
void Rules::pharSetupTombs() {
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!brPharadn(G, p)) continue;
        std::vector<Option> opts;
        for (size_t x = 0; x < G.planets.size(); ++x) {
            const PlanetState& ps = G.planets[x];
            if (ps.system < 0 || ps.destroyed || ps.tomb || ps.owner == p || isStation(G, int(x)) || G.systems[size_t(ps.system)].offBoard) continue;
            opts.push_back({int(x), planetName(int(x)), std::to_string(planetResources(G, int(x))) + "/" + std::to_string(planetInfluence(G, int(x))) +
                                                            ", " + sysName(ps.system), true, ps.system});
        }
        if (opts.empty()) continue;
        const int most = std::min<int>(14, int(opts.size()));
        std::vector<int> picks = fpickMany(p, Ab::PharTombs, "Ancient Empire: place tomb tokens on up to 14 planets", opts, 0, most,
                                           "When you gain control of a planet that contains a tomb token, purge that token to capture 2 "
                                           "infantry from the supply.",
                                           true);
        for (int x : picks)
            if (x >= 0 && x < int(G.planets.size())) G.planets[size_t(x)].tomb = true;
        phEvent(E, p, "Ancient Empire", pn(p) + " places " + std::to_string(picks.size()) + " tomb tokens (Ancient Empire).");
    }
}

// "When you gain control of a planet that contains a tomb token, purge that token to capture 2 infantry from the supply."
// (owner 2026-11-24 13:08: only the Pharad'n - another player taking the planet leaves the tomb there)
void Rules::pharControl(int planet, int p) {
    if (planet < 0 || !G.planets[size_t(planet)].tomb || p < 0 || p >= G.numPlayers) return;
    if (!brPharadn(G, p)) {
        if (G.fixes < 44) {  // (0.85.0 - 0.85.2 games: any player's taking purged it)
            G.planets[size_t(planet)].tomb = false;
            phEvent(E, p, "Ancient Empire", pn(p) + " takes " + planetName(planet) + ": its tomb token is purged (Ancient Empire).",
                    G.planets[size_t(planet)].system);
        }
        return;
    }
    G.planets[size_t(planet)].tomb = false;
    for (int i = 0; i < 2; ++i) G.players[size_t(p)].captured.push_back(int16_t(kCapturedToken * 16 + int(UnitType::Infantry)));  // (one line: below)
    phEvent(E, p, "Ancient Empire", pn(p) + " opens the tomb on " + planetName(planet) + " and captures 2 infantry (Ancient Empire).",
            G.planets[size_t(planet)].system);
}

// ------------------------------------------------------------------ Immortal, Sacrotitan
// destroyUnits: their infantry destroyed or removed (removing_) - captured; 2 or more destroyed on a planet - the Sacrotitan
void Rules::pharInfantryLost(int p, int k, int sys, int planet) {
    if (k <= 0 || !brPharadn(G, p)) return;
    {
        struct Anyway {  // "Game effects cannot prevent you from using this ability."
            bool& f;
            bool was;
            ~Anyway() { f = was; }
        } anyway{captureAnyway_, captureAnyway_};
        captureAnyway_ = true;
        capture(p, p, UnitType::Infantry, k, removing_ ? "Immortal: removed" : "Immortal");
    }
    if (removing_ || k < 2 || planet < 0 || sys < 0 || G.phase != Phase::Action || !mechAbilities(G)) return;
    Player& pl = G.players[size_t(p)];
    const PlanetState& ps = G.planets[size_t(planet)];
    if (pl.pharMech == actionNow(G) || ps.destroyed || unitsForPlacement(G, p, UnitType::Mech) <= 0) return;
    // (on a planet of theirs, or one they are fighting for - the mech joins that ground combat)
    if (ps.owner != p && G.groundCombatPlanet != planet) return;
    curValue_ = planet;
    const bool use = fyes(p, Ab::PharSacrotitan, "Sacrotitan: place 1 mech on " + planetName(planet) + "?",
                          "DEPLOY: Once per action, after 2 or more of your infantry are destroyed on a planet, you may place 1 mech from your "
                          "reinforcements on that planet.");
    curValue_ = 0;
    if (!use) return;
    pl.pharMech = actionNow(G);
    addUnits(p, UnitType::Mech, 1, sys, planet);
    phEvent(E, p, "Sacrotitan", pn(p) + "'s Sacrotitan rises on " + planetName(planet) + " (Sacrotitan).", sys);
}

// infantry taken off the board by an effect that doesn't go through destroyUnits (an exploration's cost, Refit Troops)
void Rules::pharRemoved(int p, int k) {
    if (k <= 0 || !brPharadn(G, p)) return;
    struct Anyway {
        bool& f;
        bool was;
        ~Anyway() { f = was; }
    } anyway{captureAnyway_, captureAnyway_};
    captureAnyway_ = true;
    capture(p, p, UnitType::Infantry, k, "Immortal: removed");
}

// Mark of Pharad'n, from addUnits: their infantry put on the board are produced - 1 captured infantry returned for each. The count
// placed (0: none captured).
int Rules::pharPlace(int p, int k) {
    if (k <= 0 || !markOfPharadn(G, p)) return k;
    const int have = pharCaptured(G, p);
    if (have < k)
        say(p, pn(p) + " has " + (have ? "only " + std::to_string(have) : std::string("no")) + " captured infantry to produce " +
                   (k == 1 ? "it" : "them") + " (Mark of Pharad'n).");
    k = std::min(k, have);
    for (int i = 0; i < k; ++i) returnCaptured(p, UnitType::Infantry);
    return k;
}

// ------------------------------------------------------------------ Dark Purpose
void Rules::pharScored(int p) {
    if (!brPharadn(G, p) || reinforcementTokens(p) <= 0) return;
    phEvent(E, p, "Dark Purpose", pn(p) + " gains 1 command token (Dark Purpose).");
    gainTokens(p, 1, false, "Dark Purpose: when you score an objective, gain 1 command token.", "Dark Purpose: place your new command token");
}

// ------------------------------------------------------------------ Immortal II: "DEPLOY: At the start of your turn, you may place 1
// infantry on a planet you control." (produced: 1 captured infantry)
void Rules::pharTurnStart(int p) {
    if (!brPharadn(G, p) || !owns(p, "dspharinf") || pharCaptured(G, p) <= 0) return;
    std::vector<Option> o;
    for (int x : controlledPlanets(G, p))
        if (!isDMZ(G, x) && !isStation(G, x) && !G.planets[size_t(x)].destroyed)
            o.push_back({x, planetName(x), std::to_string(G.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry)) + " infantry there", true,
                         G.planets[size_t(x)].system});
    if (o.empty()) return;
    o.push_back({-1, "None", ""});
    const int x = fpick(p, Ab::PharImmortal, "Immortal II: place 1 infantry on a planet you control?", o,
                        "You return 1 of your " + std::to_string(pharCaptured(G, p)) + " captured infantry to produce it (Mark of Pharad'n).", true,
                        true);
    if (x < 0) return;
    addUnits(p, UnitType::Infantry, 1, G.planets[size_t(x)].system, x);
    phEvent(E, p, "Immortal II", pn(p) + " places 1 infantry on " + planetName(x) + " (Immortal II).", G.planets[size_t(x)].system);
}

// ------------------------------------------------------------------ Death Binding: "When the Pharad'n player passes, remove 4 of your
// infantry from the game board to gain 1 technology with no prerequisites. Then, return this card to the Pharad'n player; that player
// captures 1 infantry from the supply."
void Rules::pharPassed(int p) {
    if (!brPharadn(G, p)) return;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p || G.players[size_t(q)].eliminated || !holdsNote(G, q, p, kNoteFaction)) continue;
        if (unitsOnBoard(G, q, UnitType::Infantry) < 4) continue;
        std::vector<Option> techs;
        for (int t = 0; t < kTechCount; ++t) {
            const TechDef& d = kTechs[size_t(t)];
            if (d.req[0] + d.req[1] + d.req[2] + d.req[3] == 0 && gainableTech(q, t)) techs.push_back({t, d.name, techSummary(t)});
        }
        if (techs.empty()) continue;
        curFor_ = p;
        const bool use = fyes(q, Ab::PharDeathBinding, "Play " + pn(p) + "'s Death Binding?",
                              "Remove 4 of your infantry from the game board to gain 1 technology with no prerequisites. Then the card goes back to " +
                                  pn(p) + ", who captures 1 infantry.");
        curFor_ = -1;
        if (!use) continue;
        // the 4 infantry: one option per infantry
        std::vector<Option> inf;
        for (size_t x = 0; x < G.planets.size(); ++x)
            for (int i = 0; i < G.planets[x].units[size_t(q)].count(UnitType::Infantry) && i < 4; ++i)
                inf.push_back({int(x) * 8 + i, "Infantry on " + planetName(int(x)), "", true, G.planets[x].system});
        for (size_t s = 0; s < G.systems.size(); ++s)
            for (int i = 0; i < G.systems[s].space[size_t(q)].count(UnitType::Infantry) && i < 4; ++i)
                inf.push_back({kVaultSpace + int(s) * 8 + i, "Infantry in space, " + sysName(int(s)), "", true, int(s)});
        std::vector<int> picks = fpickMany(q, Ab::PharDeathRemove, "Death Binding: remove which 4 infantry?", inf, 4, 4, "", true);
        {
            struct Removing {
                bool& f;
                bool was;
                ~Removing() { f = was; }
            } rem{removing_, removing_};
            removing_ = true;
            for (int id : picks) {
                if (id >= kVaultSpace) {
                    const int s = (id - kVaultSpace) / 8;
                    destroyUnits(q, UnitType::Infantry, 1, s, -1, q, "Death Binding");
                } else {
                    const int x = id / 8;
                    destroyUnits(q, UnitType::Infantry, 1, G.planets[size_t(x)].system, x, q, "Death Binding");
                }
            }
        }
        const int t = fpick(q, Ab::PharDeathTech, "Death Binding: gain which technology?", techs, "Any technology with no prerequisites.");
        if (t >= 0) researchTech(q, t, false);
        phEvent(E, q, "Death Binding", pn(q) + " removes 4 infantry and gains " + (t >= 0 ? std::string(kTechs[size_t(t)].name) : "nothing") +
                                           " (Death Binding).");
        returnNote(q, makeNote(p, kNoteFaction));
        capture(p, p, UnitType::Infantry, 1, "Death Binding");
    }
}

// ------------------------------------------------------------------ their turn, no action used: Nokash (670), Hidden Vaults (671)
void Rules::pharFreeOptions(int p, std::vector<Option>& o) {
    if (!brPharadn(G, p)) return;
    if (canUseAgent(p, "pharadn") && !pharNokashTargets().empty())
        o.push_back({670, "Nokash (agent)", "Allow a player to destroy all their infantry on 1 planet they control: 1 commodity each, then all their "
                                            "commodities become trade goods. Doesn't use your action."});
    if (breakthroughsOn(G) && breakthroughIs(G, p, "pharadn") && !G.players[size_t(p)].btExhausted)
        o.push_back({671, "Hidden Vaults (breakthrough)", "Exhaust it to destroy any number of your units on the game board. Doesn't use your action."});
}

// the players Nokash may allow: those with infantry on a planet they control
std::vector<Option> Rules::pharNokashTargets() const {
    std::vector<Option> o;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (G.players[size_t(q)].eliminated) continue;
        int best = 0;
        for (int x : controlledPlanets(G, q)) best = std::max(best, int(G.planets[size_t(x)].units[size_t(q)].count(UnitType::Infantry)));
        if (best > 0) o.push_back({q, pn(q), "Up to " + std::to_string(best) + " infantry on one planet; " + std::to_string(G.players[size_t(q)].commodities) + " commodities"});
    }
    return o;
}

bool Rules::pharFreeAction(int p, int id) {
    if (id == 670) {
        if (!canUseAgent(p, "pharadn")) return false;
        std::vector<Option> who = pharNokashTargets();
        if (who.empty()) return false;
        who.push_back({-1, "Cancel", ""});
        const int q = fpick(p, Ab::PharNokash, "Nokash: allow which player to destroy their infantry on 1 planet?", who,
                            "For each infantry destroyed, that player gains 1 commodity. Then, that player converts all of their commodities to trade goods.");
        if (q < 0) return false;
        std::vector<Option> where;
        for (int x : controlledPlanets(G, q)) {
            const int k = G.planets[size_t(x)].units[size_t(q)].count(UnitType::Infantry);
            if (k > 0) where.push_back({x, planetName(x), infantryCount(k), true, G.planets[size_t(x)].system});
        }
        if (where.empty()) return false;
        where.push_back({-1, "Don't", ""});
        curFor_ = p;
        const int x = fpick(q, Ab::PharNokashPlanet, pn(p) + "'s Nokash: destroy all your infantry on which planet?", where,
                            "1 commodity for each, then all your commodities become trade goods.", true, true);
        curFor_ = -1;
        if (x < 0) return q != p;  // (another player said no: the agent stays ready, the offer was made)
        spendAgent(p, "pharadn");
        const int sys = G.planets[size_t(x)].system;
        const int k = G.planets[size_t(x)].units[size_t(q)].count(UnitType::Infantry);
        destroyUnits(q, UnitType::Infantry, k, sys, x, q, "Nokash");
        Player& pq = G.players[size_t(q)];
        const int gained = std::max(0, std::min(k, commodityValue(G, q) - int(pq.commodities)));
        pq.commodities = int8_t(pq.commodities + gained);
        const int tg = pq.commodities;
        pq.commodities = 0;
        if (tg > 0) gainTGNotSupply(q, tg);
        phEvent(E, p, "Nokash", pn(q) + " destroys " + infantryCount(k) + " on " + planetName(x) + ", gains " + std::to_string(gained) +
                                    " commodities and converts " + std::to_string(tg) + " into trade goods (Nokash).", sys);
        agentUsedFor(p, q);  // (owner 11:14, reading 5: "make sure it's sellable" - a deal's "use your agent for me" kept)
        return true;
    }
    if (id == 671) {
        if (!(breakthroughsOn(G) && breakthroughIs(G, p, "pharadn") && !G.players[size_t(p)].btExhausted)) return false;
        std::vector<Option> o;
        for (size_t x = 0; x < G.planets.size(); ++x)
            for (int t = 0; t < kUnitTypes; ++t)
                for (int i = 0; i < G.planets[x].units[size_t(p)].n[size_t(t)] && i < 8; ++i)
                    o.push_back({int(x) * 128 + t * 8 + i, std::string(unitTypeName(UnitType(t))) + " on " + planetName(int(x)), "", true, G.planets[x].system});
        for (size_t s = 0; s < G.systems.size(); ++s)
            for (int t = 0; t < kUnitTypes; ++t)
                for (int i = 0; i < G.systems[s].space[size_t(p)].n[size_t(t)] && i < 8; ++i)
                    o.push_back({kVaultSpace + int(s) * 128 + t * 8 + i, std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)), "", true, int(s)});
        if (o.empty()) return false;
        std::vector<int> picks = fpickMany(p, Ab::PharVaultsDestroy, "Hidden Vaults: destroy which of your units?", o, 0, int(o.size()),
                                           "Exhaust Hidden Vaults to destroy any number of your units on the game board (none: keep it ready).", true);
        if (picks.empty()) return false;
        G.players[size_t(p)].btExhausted = true;
        // grouped by place and type, so 2 infantry on a planet go as one loss (Sacrotitan)
        std::map<std::pair<int, int>, int> groups;  // (location, type) -> count; location = planet, or kVaultSpace + system
        for (int id2 : picks) {
            const bool space = id2 >= kVaultSpace;
            const int base = space ? id2 - kVaultSpace : id2;
            groups[{space ? kVaultSpace + base / 128 : base / 128, (base % 128) / 8}]++;
        }
        int total = 0;
        for (const auto& gk : groups) {
            const bool space = gk.first.first >= kVaultSpace;
            const int loc = space ? gk.first.first - kVaultSpace : gk.first.first;
            const int sys = space ? loc : G.planets[size_t(loc)].system;
            destroyUnits(p, UnitType(gk.first.second), gk.second, sys, space ? -1 : loc, p, "Hidden Vaults");
            total += gk.second;
            if (space) removeExcessCapacity(p, sys);
        }
        phEvent(E, p, "Hidden Vaults", pn(p) + " destroys " + std::to_string(total) + " of their units (Hidden Vaults).");
        return true;
    }
    return false;
}

// "When you explore a planet, you may produce 1 infantry on that planet."
void Rules::pharExplored(int p, int planet) {
    if (planet < 0 || !breakthroughsOn(G) || !breakthroughIs(G, p, "pharadn") || pharCaptured(G, p) <= 0) return;
    const PlanetState& ps = G.planets[size_t(planet)];
    if (ps.destroyed || isDMZ(G, planet) || ps.owner != p) return;
    curValue_ = planet;
    const bool use = fyes(p, Ab::PharVaultsProduce, "Hidden Vaults: produce 1 infantry on " + planetName(planet) + "?",
                          "You return 1 of your " + std::to_string(pharCaptured(G, p)) + " captured infantry for it (Mark of Pharad'n).");
    curValue_ = 0;
    if (!use) return;
    addUnits(p, UnitType::Infantry, 1, ps.system, planet);
    phEvent(E, p, "Hidden Vaults", pn(p) + " produces 1 infantry on " + planetName(planet) + " (Hidden Vaults).", ps.system);
}

// ------------------------------------------------------------------ Avhkan's unlock: "ACTION: Destroy 2 of your infantry on each of 5 planets."
std::vector<int> Rules::pharAvhkanPlanets(int p) const {
    std::vector<int> out;
    for (size_t x = 0; x < G.planets.size(); ++x)
        if (G.planets[x].units[size_t(p)].count(UnitType::Infantry) >= 2 && !G.planets[x].destroyed) out.push_back(int(x));
    return out;
}

void Rules::pharActionOptions(int p, std::vector<Option>& o) {
    if (!brPharadn(G, p)) return;
    bool locked = false;
    for (const LeaderState& ls : G.players[size_t(p)].leaders)
        if (kLeaders[ls.leader].type == kCommander && ls.status == kLocked) locked = true;
    if (!locked || G.players[size_t(p)].pharUnlock || pharAvhkanPlanets(p).size() < 5) return;
    o.push_back({6000 + int(Ab::PharAvhkanUnlock), "Unlock Avhkan (commander)", "ACTION: Destroy 2 of your infantry on each of 5 planets."});
}

bool Rules::pharAction(int p, Ab ab) {
    if (ab != Ab::PharAvhkanUnlock) return false;
    std::vector<int> pls = pharAvhkanPlanets(p);
    if (pls.size() < 5 || G.players[size_t(p)].pharUnlock) return false;
    std::vector<Option> o;
    for (int x : pls)
        o.push_back({x, planetName(x), std::to_string(G.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry)) + " infantry there", true,
                     G.planets[size_t(x)].system});
    std::vector<int> picks = o.size() == 5 ? pls
                                           : fpickMany(p, Ab::PharAvhkanUnlock, "Avhkan: destroy 2 of your infantry on which 5 planets?", o, 5, 5,
                                                       "Your infantry are captured (Immortal).", true);
    if (picks.size() != 5) return false;
    for (int x : picks) destroyUnits(p, UnitType::Infantry, 2, G.planets[size_t(x)].system, x, p, "Avhkan's unlock");
    G.players[size_t(p)].pharUnlock = true;
    phEvent(E, p, "Avhkan", pn(p) + " destroys 2 infantry on each of 5 planets to unlock Avhkan, The Crow.");
    return true;
}

// ------------------------------------------------------------------ Avhkan: "At the start of ground combat on a planet you control: You
// may destroy 1 infantry on that planet"
void Rules::pharGroundStart(int att, int def, int planet) {
    if (!brOn() || planet < 0) return;
    const int sys = G.planets[size_t(planet)].system;
    // (owner 2026-11-24 11:14, reading 6: "it's any infantry, so either player. The Pharad'n player should choose" - their own too:
    // Immortal captures it back.) Options: 1 + the player whose infantry is destroyed; 0 = none.
    for (int side : {def, att}) {
        if (side < 0 || side >= G.numPlayers || G.planets[size_t(planet)].owner != side || !commanderUsable(G, side, "pharadn")) continue;
        const int opp = side == att ? def : att;
        std::vector<Option> o;
        for (int q : {opp, side}) {
            if (q < 0 || q >= G.numPlayers || G.planets[size_t(planet)].units[size_t(q)].count(UnitType::Infantry) <= 0) continue;
            o.push_back({1 + q, q == side ? std::string("1 of your own infantry") : "1 of " + pn(q) + "'s infantry",
                         q == side ? std::string("Immortal: it is captured.") : std::string()});
        }
        if (o.empty()) continue;
        o.push_back({0, "Don't", ""});
        curFor_ = opp;
        curValue_ = planet;
        const int pickd = fpick(side, Ab::PharAvhkan, "Avhkan: destroy 1 infantry on " + planetName(planet) + "?",
                                o, "At the start of ground combat on a planet you control: you may destroy 1 infantry on that planet.");
        curFor_ = -1;
        curValue_ = 0;
        if (pickd <= 0) continue;
        const int q = pickd - 1;
        destroyUnits(q, UnitType::Infantry, 1, sys, planet, side, "Avhkan");
        phEvent(E, side, "Avhkan", pn(side) + "'s crow strikes: 1 of " + (q == side ? std::string("their own") : pn(q) + "'s") + " infantry on " +
                                       planetName(planet) + " is destroyed (Avhkan).", sys);
    }
}

// ------------------------------------------------------------------ Reality-Field Impactor
void Rules::pharActivated(int p, int sys) {
    if (!brPharadn(G, p) || !owns(p, "dspharb") || p != G.activePlayer || G.fx.rfiSys >= 0) return;
    std::vector<Option> o;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (G.systems[s].offBoard || !isAnomaly(G, int(s))) continue;
        o.push_back({int(s), sysName(int(s)), int(s) == sys ? "the active system" : "", true, int(s)});
    }
    if (o.empty()) return;
    o.push_back({-1, "None", ""});
    curValue_ = sys;
    const int s = fpick(p, Ab::PharImpactor, "Reality-Field Impactor: choose 1 anomaly that has no effect this tactical action?", o,
                        "Until the end of this tactical action - for every player.", true);
    curValue_ = 0;
    if (s < 0) return;
    G.fx.rfiSys = int16_t(s);
    phEvent(E, p, "Reality-Field Impactor", pn(p) + " stills the anomaly in " + sysName(s) + " until the end of this tactical action (Reality-Field Impactor).", s);
}

// ------------------------------------------------------------------ the Commit Ground Forces step: the Black Pyramid's DEPLOY, then
// Pharad'n the Immortal for each planet that ground forces were committed to
void Rules::pharLanded(int p, int sys, std::vector<int>& landed) {
    if (!brOn()) return;
    // Black Pyramid: "DEPLOY: During your "Commit Ground Forces" step, if you control a planet in the active system, place this unit on a
    // planet in that system." - a planet its card allows (hazardous / legendary), theirs or one they commit ground forces to
    if (monumentsOn() && brPharadn(G, p) && mechAbilities(G) && playerMonument(G, p) >= 0 && unitsAvailable(G, p, UnitType::Monument) > 0) {
        bool mine = false;
        for (int16_t x : G.systems[size_t(sys)].planets) mine = mine || G.planets[size_t(x)].owner == p;
        if (mine) {
            std::vector<Option> o;
            for (int16_t x : G.systems[size_t(sys)].planets) {
                const bool committed = std::find(landed.begin(), landed.end(), int(x)) != landed.end();
                if (G.planets[size_t(x)].destroyed || isDMZ(G, x) || !monumentFitsPlanet(G, p, x)) continue;
                if (G.planets[size_t(x)].owner != p && !committed) continue;
                o.push_back({x, planetName(x), committed && G.planets[size_t(x)].owner != p ? "It fights with your committed ground forces." : "", true, sys});
            }
            if (!o.empty()) {
                o.push_back({-1, "No", ""});
                const int x = fpick(p, Ab::PharPyramid, "Black Pyramid: DEPLOY it on a planet in this system?", o,
                                    "This unit is treated as a structure and a ground force (combat 8, 2 dice, sustain damage).", true, true);
                if (x >= 0) placeMonument(p, monumentOptionId(x, -1));
            }
        }
    }
    // Pharad'n the Immortal (Black Star - Apocalypse)
    for (int pl : std::vector<int>(landed)) {
        for (int h = 0; h < G.numPlayers; ++h) {
            if (!brPharadn(G, h) || !heroUnlocked(G, h) || G.planets[size_t(pl)].units[size_t(h)].structures() <= 0) continue;
            if (G.planets[size_t(pl)].units[size_t(p)].groundForces() <= 0) continue;
            std::vector<Option> o;
            int inf = 0;
            for (int q = 0; q < kSides; ++q) inf += G.planets[size_t(pl)].units[size_t(q)].count(UnitType::Infantry);
            if (inf > 0) o.push_back({1, "Destroy all infantry on " + planetName(pl), std::to_string(inf) + " infantry"});
            if (pharCaptured(G, h) > 0 && h != p)
                o.push_back({2, "Commit captured infantry", "Up to " + std::to_string(pharCaptured(G, h)) + ", as if they were your own"});
            if (o.empty()) continue;
            o.push_back({0, "No", ""});
            curFor_ = p;
            curValue_ = pl;
            const int c = fpick(h, Ab::PharHero, "Pharad'n the Immortal: purge your hero on " + planetName(pl) + "?", o,
                                "Black Star - Apocalypse: destroy all infantry on that planet, or commit any number of your captured infantry to it as if "
                                "they were your own.");
            curFor_ = -1;
            if (c == 0) {
                curValue_ = 0;
                continue;
            }
            if (c == 1) {
                for (int q = 0; q < kSides; ++q) {
                    const int k = G.planets[size_t(pl)].units[size_t(q)].count(UnitType::Infantry);
                    if (k > 0) destroyUnits(q, UnitType::Infantry, k, sys, pl, h, "Black Star - Apocalypse");
                }
                phEvent(E, h, "Pharad'n the Immortal", "Black Star - Apocalypse: every infantry on " + planetName(pl) + " is destroyed (Pharad'n the Immortal).", sys);
            } else {
                std::vector<Option> n;
                for (int k = pharCaptured(G, h); k >= 1; --k) n.push_back({k, std::to_string(k), ""});
                const int k = n.size() == 1 ? 1 : fpick(h, Ab::PharHeroCount, "Pharad'n the Immortal: commit how many captured infantry?", n);
                const int placed = std::min(k, pharCaptured(G, h));
                addUnits(h, UnitType::Infantry, placed, sys, pl);
                phEvent(E, h, "Pharad'n the Immortal", "Black Star - Apocalypse: " + infantryCount(placed) + " rise on " + planetName(pl) +
                                                           " (Pharad'n the Immortal).", sys);
            }
            curValue_ = 0;
            purgeHero(h);
        }
    }
}

}  // namespace ti
