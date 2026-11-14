// Discordant Stars batch 5 (0.41.0): six more factions, called from the ds / ds2 / ds3 / ds4 hooks and a few new ones
// (dice rolls, payments, strategy primaries, commodity refills, retreats, the scoring step). Texts in faction_text.cpp,
// the AI in ai/ai_ds.cpp.
//   Free Systems Compact - Rally to the Cause (ds5Produced), Diplomats (pay -> dsDiplomats: an uncontrolled planet's card),
//                          Free People, Vox, Envoy Network (ds5VoteBonus), Covert Strike Teams, Broadcast Teams, Cordo
//                          Haved (legendary abilities), President Cyhn, Count Otto P'may (Game::dsHeart, query isDMZ),
//                          Liberator
//   Edyn Mandate         - Grace (dsAfterPrimary), Decree (query canPassThrough), Radiance / Unity Algorithm / Edyn Rider
//                          (predictions), Encrypted Trade Hub (votes last, commodities), Kaliburn, Allant (extraAction),
//                          Kadryn (scoringStep), Midir, Rune Bearer (Sigils = systems with their mechs)
//   Nokar Sellships      - Hired Guns (hired_), Private Fleet (query unitLimitOf), Desperados (checkProducePlan, moveValue),
//                          Annah Regia, Sabre I / II, Local Contracts, Nokar Navigator / Jack Hallard / Freelance Outfit
//                          (dsRetreatExtra), Sal Sparrow, Starsails (query productionCapacity)
//   Shipwrights of Axis  - Military Industrial Complex (dsReplenished), Arms Dealers (DealSide::orders), Axis Orders
//                          (Game::dsOrders), Bearer of Heavens, Rift Engines (fx.dsRift*), Emergency Deployment,
//                          Industry Secrets, Shipmonger Zsknck, Designer TckVsk, Demi-Queen Mdcksssk, Forgetender
//   Myko-Mentori         - Prescient Memories / Divination (dsOmen in rollUnits), Necrophage (query commodityValue),
//                          Psyclobea Qarnyx, Mycelium Ring, Psychoactive Armaments, Gift of Insight, Lactarius Indigo,
//                          Amanita Muscaria, Coprinus Comatus (Soul Cage: Game::dsSoulCage), Amandia Pholdis
//   Ghemina Raiders      - The Lady & The Lord (two flagships, two heroes: Game::dsLadySys, query overflowOf / unitLimitOf),
//                          Rule of Two, Combat Transport II, War Song Implants, Raid Leaders (fx.dsRaid*), Skarvald &
//                          Torvar, Jarl Vel & Jarl Jotrun, Korela, Kantrus, Jotun
#include <algorithm>
#include <cstring>

#include "engine/deal.h"
#include "engine/rules.h"

namespace ti {

namespace {
constexpr int kShipMask5 = kShipMaskAll;  // Carrier .. WarSun, the second flagship
constexpr int kGroundMask5 = (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech));

void ds5Event(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
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
bool isHome5(const Game& G, int x) { return G.systems[size_t(G.planets[size_t(x)].system)].home >= 0; }
bool isMecatol5(const Game& G, int x) { return !std::strcmp(kPlanets[G.planets[size_t(x)].def].key, "mr"); }
bool isLegendary5(const Game& G, int x) { return *legendaryName(G.planets[size_t(x)].def) != 0; }
// Rune Bearer (Edyn mech): the systems with their mechs are Sigils
bool sigilAt(const Game& G, int s) {
    for (int q = 0; q < G.numPlayers; ++q) {
        if (!isFaction(G, q, "edyn") || !mechAbilities(G)) continue;
        if (G.systems[size_t(s)].space[size_t(q)].count(UnitType::Mech)) return true;
        for (int16_t x : G.systems[size_t(s)].planets)
            if (G.planets[size_t(x)].units[size_t(q)].count(UnitType::Mech)) return true;
    }
    return false;
}
std::vector<int> sigils(const Game& G) {
    std::vector<int> v;
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (sigilAt(G, int(s))) v.push_back(int(s));
    return v;
}
int flagshipsIn(const Game& G, int p, int s, UnitType only = UnitType::Count) {  // (only: one of the two flagship types)
    int n = 0;
    for (UnitType t : {UnitType::Flagship, UnitType::Flagship2}) {
        if (only != UnitType::Count && t != only) continue;
        n += G.systems[size_t(s)].space[size_t(p)].count(t);
        for (int16_t x : G.systems[size_t(s)].planets) n += G.planets[size_t(x)].units[size_t(p)].count(t);
    }
    return n;
}
// The Lord is in a system with a Ghemina flagship that isn't (only) the Lady. 0.52.0 (fixes 16): the Lady is her own unit
// type (Flagship2), the Lord the Flagship.
bool lordIn(const Game& G, int p, int s) {
    if (G.fixes >= 16) return flagshipsIn(G, p, s, UnitType::Flagship) > 0;
    return flagshipsIn(G, p, s) - (s == G.dsLadySys ? 1 : 0) > 0;
}
bool ladyIn(const Game& G, int p, int s) {
    if (G.fixes >= 16) return p >= 0 && flagshipsIn(G, p, s, UnitType::Flagship2) > 0;
    return s == G.dsLadySys;
}
// Axis Order cards: 0-1 dreadnought (cost 3), 2-3 carrier (2), 4-5 cruiser (1), 6-7 two destroyers (1)
UnitType orderType(int i) {
    static const UnitType k[4] = {UnitType::Dreadnought, UnitType::Carrier, UnitType::Cruiser, UnitType::Destroyer};
    return k[i / 2];
}
int orderCost(int i) {
    static const int k[4] = {3, 2, 1, 1};
    return k[i / 2];
}
std::string orderName(int i) {
    static const char* k[4] = {"Dreadnought", "Carrier", "Cruiser", "Destroyers"};
    return std::string("Axis Order: ") + k[i / 2];
}
}  // namespace

// Player-facing names and texts of the Axis Order cards (deal screen, faction sheet) - our own words.
std::string axisOrderName(int i) { return i >= 0 && i < 8 ? orderName(i) : std::string(); }
std::string axisOrderText(int i) {
    if (i < 0 || i >= 8) return "";
    const std::string what = i >= 6 ? "2 destroyers" : std::string("1 ") + unitTypeName(orderType(i));
    return "At the end of your turn: exhaust it to place " + what + " in a system with your ships and one of your command tokens. "
           "It goes back to the Axis at the start of the status phase once used.";
}

// ================================================================== combat
void Rules::ds5CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m) {
    (void)opp;
    (void)planet;
    if (p < 0 || p >= G.numPlayers || sys < 0) return;
    const Force& f = G.systems[size_t(sys)].space[size_t(p)];
    if (space && flagshipIn(G, p, sys)) {
        // Vox (Free Systems flagship): 1 more die for each planet here of the same trait (the most common one)
        if (flagshipAbility(G, p, "freesystems")) {
            int best = 0;
            for (Trait t : {Trait::Cultural, Trait::Hazardous, Trait::Industrial}) {
                int n = 0;
                for (int16_t x : G.systems[size_t(sys)].planets) n += hasTraitOf(G, x, t) ? 1 : 0;
                best = std::max(best, n);
            }
            m.extraDice[int(UnitType::Flagship)] += best;
        }
        // Kaliburn (Edyn flagship): +1 for each law in play
        if (flagshipAbility(G, p, "edyn")) m.perType[int(UnitType::Flagship)] += int(G.laws.size());
        // Annah Regia (Nokar flagship): +1 for every 2 destroyers they have on the board
        if (flagshipAbility(G, p, "nokar")) m.perType[int(UnitType::Flagship)] += unitsOnBoard(G, p, UnitType::Destroyer) / 2;
    }
    // Rule of Two (Ghemina): exactly 2 non-fighter ships here, of the same type - +2 to each
    if (space && fac(p, "ghemina") && f.nonFighterShips() == 2)
        for (int t : kShipIdx)
            if (t != int(UnitType::Fighter) && f.n[t] == 2) m.perType[t] += 2;
}

void Rules::ds5SpaceCombatStart(int att, int def, int sys) {
    G.dsNavigator = -1;
    qarnyxRound_ = -1;
    dsRetreating_ = -1;
    // Nokar Navigator (the Nokar's note): retreats reach every system with one of the holder's planets
    const int nk = playerOfFaction(G, "nokar");
    if (nk < 0) return;
    for (int side : {att, def}) {
        if (side >= G.numPlayers || side == nk || !holdsNote(G, side, nk, kNoteFaction)) continue;
        if (!fyes(side, Ab::DsNavigator, "Play " + pn(nk) + "'s Nokar Navigator?", "During this combat you may retreat to any system with a planet you control."))
            continue;
        returnNote(side, makeNote(nk, kNoteFaction));
        G.dsNavigator = int8_t(side);
        ds5Event(E, side, "Nokar Navigator", pn(side) + " charts an escape route through its own worlds.", sys);
    }
}

// Covert Strike Teams (Free Systems): up to 2 of their ground forces fire first.
void Rules::ds5GroundCombatStart(int att, int def, int planet) {
    for (int side : {att, def}) {
        if (side >= G.numPlayers || !techReady(side, "dsfreey")) continue;
        const int other = side == att ? def : att;
        const Force& f = G.planets[size_t(planet)].units[size_t(side)];
        if (!f.groundForces()) continue;
        if (!fyes(side, Ab::DsCovert, "Covert Strike Teams: up to 2 of your ground forces on " + planetName(planet) + " fire before the combat?", ""))
            continue;
        RollResult r;
        int left = 2;
        for (UnitType t : {UnitType::Mech, UnitType::Infantry}) {
            const int k = std::min<int>(left, f.count(t));
            if (k > 0) addDice(r, k, unitDef(G, side, t).combat, int(t));
            left -= std::max(0, k);
        }
        recount(r);
        const int sys = G.planets[size_t(planet)].system;
        reportRoll(side, sys, "Covert Strike Teams", r);
        if (r.hits > 0) assignHits(other, sys, planet, r.hits, kGroundMask5, "Covert Strike Teams", side);
    }
}

// After the dice of a combat round: Psychoactive Armaments, Gift of Insight (Myko-Mentori), Bearer of Heavens (Axis),
// Skarvald & Torvar (Ghemina agent), Combat Transport II (Ghemina).
void Rules::ds5AfterCombatRolls(int att, int def, int sys, int planet, RollResult& rA, RollResult& rD) {
    const bool space = planet < 0;
    for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? att : def, other = i == 0 ? def : att;
        if (side >= G.numPlayers) continue;
        RollResult& mine = i == 0 ? rA : rD;
        RollResult& theirs = i == 0 ? rD : rA;
        // Psychoactive Armaments: the opponent's misses are rolled again - for us
        if (techReady(side, "dsmykog") && other < kSides) {
            int miss = 0;
            for (size_t d = 0; d < theirs.dice.size(); ++d) miss += theirs.dice[d] < theirs.need[d] ? 1 : 0;
            if (miss >= (G.fixes >= 2 ? 1 : 2)) {  // (0.43.0, D115: the Myko-Mentori decide whenever there is a miss)
                curValue_ = miss;
                const bool use = fyes(side, Ab::DsPsychoactive, "Psychoactive Armaments: roll " + pn(other) + "'s " + std::to_string(miss) + " missed dice again, against them?", "");
                curValue_ = 0;
                if (use) {
                    exhaustTech(side, "dsmykog");
                    RollResult r;
                    for (size_t d = 0; d < theirs.dice.size(); ++d)
                        if (theirs.dice[d] < theirs.need[d]) addDice(r, 1, theirs.need[d], d < theirs.unit.size() ? theirs.unit[d] : -1);
                    recount(r);
                    reportRoll(side, sys, "Psychoactive Armaments", r);
                    if (r.hits > 0) assignHits(other, sys, planet, r.hits, space ? kShipMask5 : kGroundMask5, "Psychoactive Armaments", side);
                }
            }
        }
        // Gift of Insight (the Myko-Mentori's note, face up): once per turn, 1 die rolled again
        const int my = playerOfFaction(G, "mykomentori");
        Player& pl = G.players[size_t(side)];
        if (G.fixes < 2 && my >= 0 && my != side && pl.dsInsightTurn != G.turnCount) {  // (0.43.0: any roll - dsAfterRoll)
            const int16_t note = makeNote(my, kNoteFaction);
            if (std::find(pl.notesInPlay.begin(), pl.notesInPlay.end(), note) != pl.notesInPlay.end()) {
                int d = -1;
                for (size_t k = 0; k < mine.dice.size(); ++k)
                    if (mine.dice[k] < mine.need[k] && (d < 0 || mine.need[k] < mine.need[size_t(d)])) d = int(k);
                if (d >= 0 && fyes(side, Ab::DsInsightReroll, "Gift of Insight: roll your best missed die again?", "Once per turn.")) {
                    pl.dsInsightTurn = G.turnCount;
                    mine.dice[size_t(d)] = int8_t(E.rng().d10());
                    recount(mine);
                    reportRoll(side, sys, "Rerolled (Gift of Insight)", mine);
                }
            }
        }
        // Bearer of Heavens (Axis flagship): its hits repair 1 of their ships here
        if (space && flagshipAbility(G, side, "axis") && flagshipIn(G, side, sys)) {
            bool hit = false;
            for (size_t d = 0; d < mine.dice.size() && d < mine.unit.size(); ++d)
                hit = hit || (mine.unit[d] == int(UnitType::Flagship) && mine.dice[d] >= mine.need[d]);
            Force& f = G.systems[size_t(sys)].space[size_t(side)];
            int best = -1;
            for (int t : {int(UnitType::WarSun), int(UnitType::Flagship), int(UnitType::Dreadnought), int(UnitType::Cruiser), int(UnitType::Carrier)})
                if (best < 0 && f.dmg[t] > 0) best = t;
            if (hit && best >= 0) {
                f.dmg[best] = uint8_t(f.dmg[best] - 1);
                ds5Event(E, side, "Bearer of Heavens", pn(side) + "'s Bearer of Heavens repairs a " + unitTypeName(UnitType(best)) + ".", sys);
            }
        }
        // Combat Transport II (Ghemina carriers): 1 die again in a ground combat on a planet with 2 or fewer of their infantry
        if (!space && owns(side, "dsghemcv") && G.systems[size_t(sys)].space[size_t(side)].count(UnitType::Carrier) > 0 &&
            G.planets[size_t(planet)].units[size_t(side)].count(UnitType::Infantry) <= 2) {
            int d = -1;
            for (size_t k = 0; k < mine.dice.size(); ++k)
                if (mine.dice[k] < mine.need[k] && (d < 0 || mine.need[k] < mine.need[size_t(d)])) d = int(k);
            if (d >= 0) {
                mine.dice[size_t(d)] = int8_t(E.rng().d10());
                recount(mine);
                reportRoll(side, sys, "Rerolled (Combat Transport II)", mine);
            }
        }
    }
    // Skarvald & Torvar (Ghemina agent): the active player's hits + 1
    const int act = G.activePlayer;
    if (act >= 0 && act < G.numPlayers && (act == att || act == def)) {
        RollResult& r = act == att ? rA : rD;
        if (r.hits > 0) {
            if (const int user = agentUser(act, "ghemina"); user >= 0) {
                agentFor(user, act);
                const bool use = fyes(user, Ab::DsSkarvald, user == act ? "Exhaust Skarvald & Torvar: 1 more hit?" : "Exhaust Skarvald & Torvar: 1 more hit for " + pn(act) + "?",
                                      leaderText(findLeader("gheminaagent")));
                curFor_ = -1;
                if (use) {
                    spendAgent(user, "ghemina");
                    agentDone(user, act);
                    ++r.hits;
                    ds5Event(E, act, "Skarvald & Torvar", "The raid heralds sound: " + pn(act) + " scores 1 more hit.", sys);
                }
            }
        }
    }
}

// End of a combat round (space or ground): Necrophage (Myko-Mentori) after the first round.
void Rules::ds5RoundEnd(int att, int def, int sys, bool space) {
    (void)space;
    if (combatRound_ != 1) return;
    for (int side : {att, def}) {
        if (side >= G.numPlayers || !fac(side, "mykomentori")) continue;
        Player& pl = G.players[size_t(side)];
        std::vector<Option> opts;
        if (pl.commodities < commodityCap(G, pl)) opts.push_back({0, "Gain 1 commodity", ""});
        if (pl.commodities > 0) opts.push_back({1, "Turn 1 commodity into a trade good", ""});
        if (opts.empty()) continue;
        const int c = opts.size() == 1 ? opts[0].id : fpick(side, Ab::DsNecrophage, "Necrophage: what do you take from the fallen?", opts);
        if (c == 0) { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + 1); commodityGained(side, cb_); }
        else {
            pl.commodities = int8_t(pl.commodities - 1);
            gainTG(side, 1);
        }
        ds5Event(E, side, "Necrophage", pn(side) + " feeds on the battle: " + (c == 0 ? "1 commodity." : "1 trade good."), sys);
    }
}

// Units destroyed: Sabre (Nokar destroyers), the Lady (Ghemina flagship), Psyclobea Qarnyx and Amandia Pholdis
// (Myko-Mentori), and which Ghemina flagship it was.
void Rules::ds5UnitsDestroyed(int p, UnitType t, int k, int sys, int planet, int byPlayer) {
    if (p < 0 || sys < 0) return;
    const bool inCombat = combatSys_ == sys || planet >= 0;
    // Sabre I / II: a destroyed Nokar destroyer may still hit (9+, II: 7+)
    if (p < G.numPlayers && t == UnitType::Destroyer && fac(p, "nokar") && byPlayer >= 0 && byPlayer < kSides && byPlayer != p && combatSys_ == sys) {
        const UnitDef& d = unitDef(G, p, UnitType::Destroyer);
        const int need = !std::strcmp(d.key, "nokar_destroyer2") ? 7 : !std::strcmp(d.key, "nokar_destroyer") ? 9 : 0;
        if (need > 0) {
            RollResult r;
            addDice(r, k, need, int(UnitType::Destroyer));
            recount(r);
            reportRoll(p, sys, "Sabre", r);
            if (r.hits > 0 && G.systems[size_t(sys)].space[size_t(byPlayer)].ships() > 0) assignHits(byPlayer, sys, -1, r.hits, kShipMask5, "Sabre", p);
        }
    }
    // The Lady (Ghemina flagship): a structure of someone else's destroyed in its system - 1 trade good
    if (isStructure(t) && sys >= 0 && (G.fixes >= 16 || sys == G.dsLadySys)) {
        const int gh = playerOfFaction(G, "ghemina");
        if (gh >= 0 && gh != p && ladyIn(G, gh, sys)) {
            gainTG(gh, k);
            ds5Event(E, gh, "The Lady", pn(gh) + " gains " + std::to_string(k) + " trade good" + (k > 1 ? "s" : "") + " from the ruins.", sys);
        }
    }
    // Which Ghemina flagship fell?
    if (p < G.numPlayers && t == UnitType::Flagship && fac(p, "ghemina") && sys == G.dsLadySys && G.fixes < 16) {  // (older games)
        const int left = flagshipsIn(G, p, sys);
        if (left == 0) {
            G.dsLadySys = -1;
        } else if (k == 1) {
            const int c = fpick(p, Ab::DsWhichFlagship, "Which of your flagships was destroyed?", {{0, "The Lord", "The Lady stays."}, {1, "The Lady", "The Lord stays."}});
            if (c == 1) G.dsLadySys = -1;
        }
    }
    // Psyclobea Qarnyx (Myko-Mentori flagship): once per round of space combat, a ship lost here - 1 commodity
    if (planet < 0 && combatSys_ == sys && isShip(t) && t != UnitType::Fighter && qarnyxRound_ != combatRound_) {
        const int my = playerOfFaction(G, "mykomentori");
        if (my >= 0 && flagshipAbility(G, my, "mykomentori") && flagshipIn(G, my, sys)) {
            Player& mp = G.players[size_t(my)];
            qarnyxRound_ = combatRound_;
            if (mp.commodities < commodityCap(G, mp)) {
                { const int cb_ = mp.commodities; mp.commodities = int8_t(mp.commodities + 1); commodityGained(my, cb_); }
                ds5Event(E, my, "Psyclobea Qarnyx", pn(my) + " gains 1 commodity from the wreckage.", sys);
            }
        }
    }
    // Amandia Pholdis (Myko-Mentori mech): on a 6 or better it waits on the card
    if (p < G.numPlayers && t == UnitType::Mech && fac(p, "mykomentori") && mechAbilities(G)) {
        RollResult r;
        addDice(r, k, 6, int(UnitType::Mech));
        recount(r);
        reportRoll(p, sys, "Amandia Pholdis", r);
        if (r.hits > 0) {
            G.players[size_t(p)].dsMykoMechs = int8_t(G.players[size_t(p)].dsMykoMechs + r.hits);
            ds5Event(E, p, "Amandia Pholdis", pn(p) + "'s mech regrows on its card.", sys);
        }
    }
    (void)inCombat;
}

void Rules::ds5WonCombat(int winner, int loser, int sys, bool space) {
    (void)loser;
    if (winner < 0 || winner >= G.numPlayers) return;
    // War Song Implants (Ghemina): after a space combat win, ready 1 of their planets
    if (space && owns(winner, "dsghemg")) {
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, winner))
            if (G.planets[size_t(x)].exhausted) opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + "/" + std::to_string(planetInfluence(G, x))});
        if (!opts.empty()) {
            opts.push_back({-1, "Don't", ""});
            const int x = fpick(winner, Ab::DsWarSong, "War Song Implants: ready which planet?", opts, "", false, true);
            if (x >= 0) {
                G.planets[size_t(x)].exhausted = false;
                ds5Event(E, winner, "War Song Implants", pn(winner) + "'s war song readies " + planetName(x) + ".", sys);
            }
        }
    }
    // Jarl Vel & Jarl Jotrun (Ghemina commander): a space combat won where there are no planets - 1 trade good
    if (space && commanderUsable(G, winner, "ghemina") && G.systems[size_t(sys)].planets.empty()) {
        gainTG(winner, 1);
        ds5Event(E, winner, "Jarl Vel & Jarl Jotrun", pn(winner) + " takes 1 trade good from the raid.", sys);
    }
}

void Rules::ds5GroundCombatOver(int att, int def, int planet, int winner) {
    (void)att;
    (void)def;
    if (winner < 0 || winner >= G.numPlayers) return;
    const int sys = G.planets[size_t(planet)].system;
    // Jarl Vel & Jarl Jotrun: a ground combat won on a planet with a structure - 1 trade good
    bool structure = false;
    for (int q = 0; q < kSides; ++q) structure = structure || G.planets[size_t(planet)].units[size_t(q)].structures() > 0;
    if (structure && commanderUsable(G, winner, "ghemina")) {
        gainTG(winner, 1);
        ds5Event(E, winner, "Jarl Vel & Jarl Jotrun", pn(winner) + " takes 1 trade good from the raid.", sys);
    }
    // Jotun (Ghemina mech): won here with exactly 1 other mech on the planet - explore it
    if (fac(winner, "ghemina") && mechAbilities(G) && G.planets[size_t(planet)].units[size_t(winner)].count(UnitType::Mech) >= 1) {
        int mechs = 0;
        for (int q = 0; q < kSides; ++q) mechs += G.planets[size_t(planet)].units[size_t(q)].count(UnitType::Mech);
        if (mechs == 2 && hasTraitOf(G, planet, Trait::Cultural) + hasTraitOf(G, planet, Trait::Hazardous) + hasTraitOf(G, planet, Trait::Industrial) > 0) {
            ds5Event(E, winner, "Jotun", pn(winner) + "'s Jotun digs into " + planetName(planet) + ".", sys);
            explorePlanet(winner, planet);
        }
    }
}

// ================================================================== retreats, hits
// Nokar Navigator (the holder: systems with their planets), Jack Hallard (Nokar commander: systems next to their home),
// Freelance Outfit (Nokar mech: its system) - more places to retreat to from the active system.
void Rules::dsRetreatExtra(int p, int sys, std::vector<int>& out) const {
    if (p < 0 || p >= G.numPlayers) return;
    std::vector<int> c;
    if (G.dsNavigator == p)
        for (size_t s = 0; s < G.systems.size(); ++s)
            for (int16_t x : G.systems[s].planets)
                if (G.planets[size_t(x)].owner == p) c.push_back(int(s));
    if (isFaction(G, p, "nokar")) {
        const int hs = G.players[size_t(p)].homeSystem;
        if (commanderUsable(G, p, "nokar") && hs >= 0)
            for (int16_t a : G.systems[size_t(hs)].adj) c.push_back(a);
        if (mechAbilities(G) && sys == G.activeSystem)
            for (size_t s = 0; s < G.systems.size(); ++s) {
                bool mech = G.systems[s].space[size_t(p)].count(UnitType::Mech) > 0;
                for (int16_t x : G.systems[s].planets) mech = mech || G.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech) > 0;
                if (mech) c.push_back(int(s));
            }
    }
    for (int a : c) {
        if (a == sys || std::find(out.begin(), out.end(), a) != out.end()) continue;
        const SystemState& s = G.systems[size_t(a)];
        if (s.isHyperlane() || otherShipsIn(G, p, a)) continue;
        const uint8_t an = s.d().anomalies;
        if ((an & kSupernova) && !owns(p, "mr")) continue;
        if ((an & kAsteroid) && !owns(p, "amd")) continue;
        bool mine = hasUnits(G, p, a);
        for (int16_t x : s.planets) mine = mine || G.planets[size_t(x)].owner == p;
        if (mine) out.push_back(a);
    }
}

// Hits about to be assigned: Amanita Muscaria (Myko-Mentori commander), Jack Hallard (Nokar commander).
int Rules::ds5CancelHits(int victim, int opp, int sys, int planet, int hits) {
    (void)opp;
    if (hits <= 0 || victim < 0 || victim >= G.numPlayers || planet >= 0) return hits;
    Player& pl = G.players[size_t(victim)];
    if (commanderUsable(G, victim, "mykomentori") && pl.commodities + pl.tradeGoods > 0 &&
        fyes(victim, Ab::DsAmanita, "Amanita Muscaria: spend 1 commodity or trade good to cancel 1 of the " + std::to_string(hits) + " hits?", "")) {
        if (pl.commodities > 0) pl.commodities = int8_t(pl.commodities - 1);
        else pl.tradeGoods = int8_t(pl.tradeGoods - 1);
        --hits;
        ds5Event(E, victim, "Amanita Muscaria", pn(victim) + "'s spores soak up 1 hit.", sys);
    }
    if (hits > 0 && dsRetreating_ == victim && commanderUsable(G, victim, "nokar")) {
        const int k = std::min(2, hits);
        hits -= k;
        ds5Event(E, victim, "Jack Hallard", pn(victim) + " slips away under fire: " + std::to_string(k) + " hit" + (k > 1 ? "s" : "") + " cancelled.", sys);
    }
    return hits;
}

// ================================================================== dice: Prescient Memories
// Divination (Myko-Mentori): one of their dice is an Omen die instead. Lactarius Indigo (their agent): one of another
// player's dice is an Omen die (the Omen die stays). Called by rollUnits before the hits are counted - the choice
// doesn't see the dice.
void Rules::dsOmen(int p, RollKind kind, RollResult& r) {
    (void)kind;
    if (r.dice.empty() || p < 0 || p >= G.numPlayers) return;
    const int my = playerOfFaction(G, "mykomentori");
    if (my < 0) return;
    Player& mp = G.players[size_t(my)];
    std::vector<int> vals;
    for (int v : mp.dsOmen)
        if (v > 0 && std::find(vals.begin(), vals.end(), v) == vals.end()) vals.push_back(v);
    if (vals.empty()) return;
    std::sort(vals.begin(), vals.end());
    if (p == my) {
        // each Omen value goes on the hardest die it still hits
        std::vector<Option> opts;
        for (int v : vals) {
            int die = -1;
            for (size_t i = 0; i < r.dice.size(); ++i)
                if (r.need[i] <= v && (die < 0 || r.need[i] > r.need[size_t(die)])) die = int(i);
            if (die < 0) continue;
            const int u = die < int(r.unit.size()) ? r.unit[size_t(die)] : -1;
            opts.push_back({v * 100 + r.need[size_t(die)], "Omen " + std::to_string(v) + " for " + (u >= 0 ? std::string(unitTypeName(UnitType(u))) : std::string("a die")),
                            "It needs " + std::to_string(r.need[size_t(die)]) + ": a sure hit."});
        }
        if (opts.empty()) return;
        opts.push_back({-1, "Roll as usual", ""});
        const int c = fpick(my, Ab::DsOmen, "Divination: use an Omen die instead of rolling?", opts);
        if (c < 0) return;
        const int v = c / 100, need = c % 100;
        for (size_t i = 0; i < r.dice.size(); ++i)
            if (r.need[i] == need) {
                r.dice[i] = int8_t(v);
                break;
            }
        for (int8_t& o : mp.dsOmen)
            if (o == v) {
                o = 0;
                break;
            }
        ds5Event(E, my, "Divination", pn(my) + " spends an Omen die: a " + std::to_string(v) + ".");
        return;
    }
    // Lactarius Indigo: against someone attacking the Myko-Mentori's ships, the lowest Omen on their surest die
    // (0.43.0: any player's roll, once the dice are seen - dsAfterRoll)
    const int user = agentUser(my, "mykomentori");
    if (G.fixes >= 2 || user != my || combatSys_ < 0 || !hasUnits(G, my, combatSys_) || !hasUnits(G, p, combatSys_)) return;
    int die = -1;
    for (size_t i = 0; i < r.dice.size(); ++i)
        if (die < 0 || r.need[i] < r.need[size_t(die)]) die = int(i);
    const int v = vals.front();
    if (die < 0 || v >= r.need[size_t(die)]) return;
    curValue_ = r.need[size_t(die)];
    const bool use = fyes(my, Ab::DsLactarius, "Exhaust Lactarius Indigo: one of " + pn(p) + "'s dice becomes your Omen " + std::to_string(v) + "?",
                          "Their surest die needs " + std::to_string(r.need[size_t(die)]) + ".");
    curValue_ = 0;
    if (!use) return;
    spendAgent(my, "mykomentori");
    r.dice[size_t(die)] = int8_t(v);
    ds5Event(E, my, "Lactarius Indigo", pn(my) + " calls an omen over " + pn(p) + "'s roll: a " + std::to_string(v) + ".");
}

// 0.43.0 (the owner's D114 / D115), after any roll of units (combat in space or on the ground, space cannon,
// bombardment, anti-fighter barrage), with the dice in view:
// - Lactarius Indigo (Myko-Mentori agent): one die of anyone's roll takes the value of one of their Omen dice - an
//   opponent's hit spoiled, or a miss of their own (or a friend's) saved;
// - Gift of Insight (their note, face up): once per turn its holder may roll one of their dice again.
void Rules::dsAfterRoll(int p, RollKind kind, RollResult& r) {
    (void)kind;
    if (r.dice.empty() || p < 0 || p >= G.numPlayers) return;
    const int my = playerOfFaction(G, "mykomentori");
    if (my < 0) return;
    Player& mp = G.players[size_t(my)];
    const int sys = combatSys_ >= 0 ? combatSys_ : G.activeSystem;
    auto unitOf = [&](size_t i) { return i < r.unit.size() && r.unit[i] >= 0 ? std::string(unitTypeName(UnitType(r.unit[i]))) : std::string("a die"); };
    // Lactarius Indigo
    if (const int user = agentUser(my, "mykomentori"); user >= 0) {
        std::vector<int> vals;
        for (int v : mp.dsOmen)
            if (v > 0 && std::find(vals.begin(), vals.end(), v) == vals.end()) vals.push_back(v);
        std::vector<Option> opts;
        for (size_t i = 0; i < r.dice.size(); ++i) {
            const bool hit = r.dice[i] >= r.need[i];
            for (int v : vals) {
                if (hit == (v >= r.need[i])) continue;  // it has to change the result
                const int id = int(i) * 16 + v;
                bool dup = false;  // (one line per unit type, need and outcome)
                for (const Option& o : opts) {
                    const size_t j = size_t(o.id / 16);
                    dup = dup || (o.id % 16 == v && r.need[j] == r.need[i] && (r.dice[j] >= r.need[j]) == hit && unitOf(j) == unitOf(i));
                }
                if (dup) continue;
                opts.push_back({id, "Omen " + std::to_string(v) + " for " + pn(p) + "'s " + unitOf(i) + " (rolled " + std::to_string(r.dice[i]) + ")",
                                std::string(hit ? "A hit becomes a miss" : "A miss becomes a hit") + " - it needs " + std::to_string(r.need[i]) + "."});
            }
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Don't", ""});
            curValue_ = p;  // (for the AI: whose roll it is)
            const int c = fpick(user, Ab::DsLactarius, "Exhaust Lactarius Indigo: change one of " + (p == user ? std::string("your") : pn(p) + "'s") + " dice to an Omen die?",
                                opts, leaderText(findLeader("mykomentoriagent")));
            curValue_ = 0;
            if (c >= 0) {
                spendAgent(user, "mykomentori");
                if (user != my) agentDone(user, my);
                const size_t i = size_t(c / 16);
                const int v = c % 16;
                r.dice[i] = int8_t(v);
                recount(r);
                ds5Event(E, my, "Lactarius Indigo", pn(my) + " calls an omen over " + pn(p) + "'s roll: a " + std::to_string(v) + ".", sys);
            }
        }
    }
    // Gift of Insight
    Player& pl = G.players[size_t(p)];
    if (my != p && pl.dsInsightTurn != G.turnCount) {
        const int16_t note = makeNote(my, kNoteFaction);
        if (std::find(pl.notesInPlay.begin(), pl.notesInPlay.end(), note) != pl.notesInPlay.end()) {
            std::vector<Option> opts;
            for (size_t i = 0; i < r.dice.size(); ++i) {
                if (r.dice[i] >= r.need[i]) continue;
                bool dup = false;
                for (const Option& o : opts) dup = dup || (r.need[size_t(o.id)] == r.need[i] && unitOf(size_t(o.id)) == unitOf(i));
                if (!dup)
                    opts.push_back({int(i), "Roll again: " + unitOf(i) + " (rolled " + std::to_string(r.dice[i]) + ")", "It needs " + std::to_string(r.need[i]) + "."});
            }
            if (!opts.empty()) {
                opts.push_back({-1, "Don't", "Once per turn."});
                const int c = fpick(p, Ab::DsInsightReroll, "Gift of Insight: roll one of your dice again?", opts, "Once per turn, after any roll of yours.");
                if (c >= 0) {
                    pl.dsInsightTurn = G.turnCount;
                    r.dice[size_t(c)] = int8_t(E.rng().d10());
                    recount(r);
                    reportRoll(p, sys, "Rerolled (Gift of Insight)", r);
                }
            }
        }
    }
}

// ================================================================== tactical actions and turns
void Rules::ds5Activated(int p, int sys) {
    if (p < 0 || p >= G.numPlayers) return;
    Player& pl = G.players[size_t(p)];
    // Gift of Insight (the Myko-Mentori's note) goes back when its holder activates a system with their units
    const int my = playerOfFaction(G, "mykomentori");
    if (my >= 0 && my != p && hasUnits(G, my, sys)) {
        const int16_t note = makeNote(my, kNoteFaction);
        if (std::find(pl.notesInPlay.begin(), pl.notesInPlay.end(), note) != pl.notesInPlay.end()) {
            returnNote(p, note);
            say(p, pn(p) + "'s Gift of Insight goes back to " + pn(my) + ".", sys);
        }
    }
    // Raid Leaders (the Ghemina's note): one ship gains capacity 2 and Bombardment 5 (x2) this action
    const int gh = playerOfFaction(G, "ghemina");
    if (gh >= 0 && gh != p && holdsNote(G, p, gh, kNoteFaction)) {
        std::vector<Option> opts;
        for (int t : kShipIdx) {
            if (t == int(UnitType::Fighter)) continue;
            bool any = false;
            for (const SystemState& s : G.systems) any = any || s.space[size_t(p)].n[t] > 0;
            if (any) opts.push_back({t, unitTypeName(UnitType(t)), ""});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Keep the note", ""});
            const int t = fpick(p, Ab::DsRaidLeaders, "Play " + pn(gh) + "'s Raid Leaders: which of your ships leads the raid?", opts,
                                "It gains capacity 2 and Bombardment 5 (x2) during this action.");
            if (t >= 0) {
                pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), makeNote(gh, kNoteFaction)), pl.notes.end());
                pl.notesInPlay.push_back(makeNote(gh, kNoteFaction));
                G.fx.dsRaidType = int8_t(t);
                G.fx.dsRaidOwner = int8_t(p);
                ds5Event(E, p, "Raid Leaders", pn(p) + "'s " + unitTypeName(UnitType(t)) + " leads the raid.", sys);
            }
        }
    }
    // Rift Engines (Axis): one ship rolls - 1-3 it is lost, 4-10 it moves 2 further this action
    if (techReady(p, "dsaxisb")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            for (int t : kShipIdx)
                if (t != int(UnitType::Fighter) && G.systems[s].space[size_t(p)].n[t] > 0 && int(s) != sys)
                    opts.push_back({int(s) * 16 + t, std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Don't", ""});
            const int c = fpick(p, Ab::DsRiftEngines, "Rift Engines: push one of your ships through the rift?", opts,
                                "Roll: 1-3 it is lost, 4-10 it moves 2 further during this tactical action.", true);
            if (c >= 0) {
                exhaustTech(p, "dsaxisb");
                const int s = c / 16, t = c % 16;
                RollResult r;
                addDice(r, 1, 4, t);
                recount(r);
                reportRoll(p, s, "Rift Engines", r);
                if (r.hits == 0) {
                    const bool was = removing_;
                    removing_ = true;
                    destroyUnits(p, UnitType(t), 1, s, -1, -1, "Rift Engines");
                    removing_ = was;
                    ds5Event(E, p, "Rift Engines", pn(p) + "'s " + unitTypeName(UnitType(t)) + " is lost in the rift.", s);
                } else {
                    G.fx.dsRiftType = int8_t(t);
                    G.fx.dsRiftOwner = int8_t(p);
                    G.fx.dsRiftSys = int16_t(s);
                    ds5Event(E, p, "Rift Engines", pn(p) + "'s " + unitTypeName(UnitType(t)) + " in " + sysName(s) + " gains 2 move.", s);
                }
            }
        }
    }
    // The Lady & The Lord: how many flagships stand with the Lady (to tell later which one moved)
    if (fac(p, "ghemina") && G.dsLadySys >= 0 && G.fixes < 16) ladyCount_ = flagshipsIn(G, p, G.dsLadySys);  // (older games)
    // Hired Guns (Nokar): up to 3 of their ships here fight for the active player - who may keep them
    const int nk = playerOfFaction(G, "nokar");
    if (nk >= 0 && nk != p && G.fixes >= 2) {
        hiredGunsDeal(nk, p, sys);  // 0.43.0 (D110): ships that can reach the system, the price agreed in Negotiations
    } else if (nk >= 0 && nk != p && G.systems[size_t(sys)].space[size_t(nk)].ships() > 0) {
        Force& f = G.systems[size_t(sys)].space[size_t(nk)];
        int third = 0;  // (for the AI: 1 = someone else to fight here, +2 = up to 3 of their ships - none left to fight)
        for (int q = 0; q < kSides; ++q)
            if (q != p && q != nk && hasUnits(G, q, sys)) third = 1;
        if (f.ships() <= 3) third += 2;
        int picked[kUnitTypes] = {};
        int total = 0;
        for (int k = 0; k < 3; ++k) {
            std::vector<Option> opts;
            for (int t : kShipIdx)
                if (f.n[t] - picked[t] > 0) opts.push_back({t, unitTypeName(UnitType(t)), ""});
            if (opts.empty() || G.players[size_t(p)].tradeGoods <= total) break;
            opts.push_back({-1, k ? "No more" : "Don't offer any", ""});
            curValue_ = third;
            const int t = fpick(nk, Ab::DsHired, k ? "Hired Guns: offer another ship?" : "Hired Guns: offer your ships in " + sysName(sys) + " to " + pn(p) + "?", opts,
                                "1 trade good each. They fight as " + pn(p) + "'s, who may keep them at the end of this action.");
            curValue_ = 0;
            if (t < 0) break;
            ++picked[t];
            ++total;
        }
        if (total > 0) {
            std::string what;
            for (int t : kShipIdx)
                if (picked[t]) what += (what.empty() ? "" : ", ") + std::to_string(picked[t]) + " " + unitTypeName(UnitType(t), picked[t] > 1);
            curValue_ = third;
            const bool buy = fyes(p, Ab::DsHiredBuy, "Hire " + what + " from " + pn(nk) + " for " + std::to_string(total) + " trade goods?",
                                  "They fight for you in this action; at its end you may keep them (from your reinforcements).");
            curValue_ = 0;
            if (buy) {
                pl.tradeGoods = int8_t(pl.tradeGoods - total);
                gainTG(nk, total);
                Force& to = G.systems[size_t(sys)].space[size_t(p)];
                hired_ = Hired();
                hired_.buyer = p;
                hired_.sys = sys;
                for (int t = 0; t < kUnitTypes; ++t) {
                    if (!picked[t]) continue;
                    const int dmg = std::max(0, int(f.dmg[t]) - (int(f.n[t]) - picked[t]));
                    f.n[t] = uint8_t(f.n[t] - picked[t]);
                    f.dmg[t] = uint8_t(f.dmg[t] - dmg);
                    to.n[t] = uint8_t(to.n[t] + picked[t]);
                    to.dmg[t] = uint8_t(to.dmg[t] + dmg);
                    hired_.n[t] = picked[t];
                }
                G.players[size_t(nk)].dsHired = true;
                ds5Event(E, nk, "Hired Guns", pn(nk) + " hires out " + what + " to " + pn(p) + ".", sys);
            }
        }
    }
}

// Hired Guns (0.43.0, the owner's D110): after p activates sys, the Nokar may offer up to 3 of their ships that are in
// it or can reach it; the price is a deal (trade goods, notes, fragments...); once it is made the Nokar pick the ships,
// which fly to the system and fight as p's. At the end of the action p keeps what its reinforcements allow.
void Rules::hiredGunsDeal(int nk, int p, int sys) {
    if (G.players[size_t(nk)].eliminated || G.players[size_t(p)].eliminated || G.systems[size_t(sys)].isHyperlane()) return;
    // the ships that can get there: in the system, or able to move there (not fighters - they need a carrier; not out of
    // a system with a Nokar command token)
    auto sources = [&]() {
        std::vector<std::pair<int, int>> v;  // (system, unit type) per ship
        for (size_t s = 0; s < G.systems.size(); ++s) {
            const Force& f = G.systems[s].space[size_t(nk)];
            if (!f.ships()) continue;
            const bool here = int(s) == sys;
            if (!here && G.systems[s].hasToken(nk)) continue;
            for (int t : kShipIdx) {
                if (!f.n[t] || !isShip(UnitType(t))) continue;
                if (!here) {
                    if (UnitType(t) == UnitType::Fighter) continue;
                    const std::vector<int> near = systemsWithin(G, int(s), moveValue(G, nk, int(s), UnitType(t)));
                    if (std::find(near.begin(), near.end(), sys) == near.end()) continue;
                }
                for (int k = 0; k < f.n[t]; ++k) v.push_back({int(s), t});
            }
        }
        return v;
    };
    std::vector<std::pair<int, int>> pool = sources();
    if (pool.empty()) return;
    int fight = 0;  // (for the AI: 1 = the buyer fights someone else there)
    for (int q = 0; q < kSides; ++q)
        if (q != p && q != nk && hasUnits(G, q, sys)) fight = 1;
    const int max = std::min<int>(3, int(pool.size()));
    curValue_ = fight;
    const bool offer = fyes(nk, Ab::DsHireOffer, "Hired Guns: offer " + pn(p) + " up to " + std::to_string(max) + " of your ships for " + sysName(sys) + "?",
                            "They fly there and fight as " + pn(p) + "'s in this action, who may keep them at its end. You agree the price in "
                            "Negotiations (trade goods, notes, fragments...).");
    curValue_ = 0;
    if (!offer) return;
    hireCtx_ = HireCtx();
    hireCtx_.nokar = nk;
    hireCtx_.buyer = p;
    hireCtx_.sys = sys;
    hireCtx_.max = max;
    Deal pre;
    pre.from = nk;
    pre.to = p;
    pre.give.hire = max;
    prefill_ = &pre;
    const uint64_t bits = (uint64_t(1) << (nk * 8 + p)) | (uint64_t(1) << (p * 8 + nk));
    const uint64_t had = G.tradeAnyway & bits;
    G.tradeAnyway |= bits;
    // (the sale is the ability's own deal: an earlier trade between them this turn doesn't stop it)
    const auto t1 = G.transactions[nk][p], t2 = G.transactions[p][nk];
    G.transactions[nk][p] = G.transactions[p][nk] = 0;
    const auto o1 = G.offers[nk][p];
    G.offers[nk][p] = 0;
    negotiateWith(nk, p);
    G.transactions[nk][p] += t1;
    G.transactions[p][nk] += t2;
    if (G.offers[nk][p] < o1) G.offers[nk][p] = o1;
    G.tradeAnyway = (G.tradeAnyway & ~bits) | had;
    prefill_ = nullptr;
    const int n = hireCtx_.agreed;
    hireCtx_ = HireCtx();
    if (n <= 0) return;
    // the Nokar pick the ships; they fly in and become the buyer's for this action
    hired_ = Hired();
    hired_.buyer = p;
    hired_.sys = sys;
    Force& to = G.systems[size_t(sys)].space[size_t(p)];
    int sold = 0;
    std::string what;
    for (int k = 0; k < n; ++k) {
        pool = sources();
        std::vector<Option> opts;
        for (const auto& sh : pool) {
            const int id = sh.first * 16 + sh.second;
            bool dup = false;
            for (const Option& o : opts) dup = dup || o.id == id;
            if (!dup) opts.push_back({id, std::string(unitTypeName(UnitType(sh.second))) + (sh.first == sys ? " already there" : " from " + sysName(sh.first)), ""});
        }
        if (opts.empty()) break;
        if (k > 0) opts.push_back({-1, "No more", ""});
        const int c = fpick(nk, Ab::DsHirePick, "Hired Guns: which ship goes to " + pn(p) + "? (" + std::to_string(k + 1) + " of up to " + std::to_string(n) + ")", opts);
        if (c < 0) break;
        const int s = c / 16, t = c % 16;
        Force& f = G.systems[size_t(s)].space[size_t(nk)];
        if (!f.n[t]) break;
        const bool dmg = f.dmg[t] >= f.n[t];  // (a damaged one only when all of them are)
        f.n[t] = uint8_t(f.n[t] - 1);
        if (dmg) f.dmg[t] = uint8_t(f.dmg[t] - 1);
        to.n[t] = uint8_t(to.n[t] + 1);
        if (dmg) to.dmg[t] = uint8_t(to.dmg[t] + 1);
        ++hired_.n[t];
        if (s != sys) removeExcessCapacity(nk, s);
        ++sold;
        what += (what.empty() ? "" : ", ") + std::string(unitTypeName(UnitType(t)));
    }
    if (!sold) {
        hired_ = Hired();
        return;
    }
    G.players[size_t(nk)].dsHired = true;
    ds5Event(E, nk, "Hired Guns", pn(nk) + " hires out " + what + " to " + pn(p) + " in " + sysName(sys) + ".", sys);
}

void Rules::ds5AfterMovement(int p, int sys) {
    if (p < 0 || p >= G.numPlayers) return;
    // The Lady & The Lord: which flagship moved in
    if (fac(p, "ghemina") && G.dsLadySys >= 0 && G.dsLadySys != sys && G.fixes < 16) {  // (older games)
        const int now = flagshipsIn(G, p, G.dsLadySys);
        if (now == 0) {
            G.dsLadySys = int16_t(sys);
            removeExcessCapacity(p, sys);
        } else if (now < ladyCount_ && flagshipsIn(G, p, sys) > 0) {
            int targets = 0;  // (for the AI: other players' planets here - the Lady bombards)
            for (int16_t x : G.systems[size_t(sys)].planets) targets += G.planets[size_t(x)].owner >= 0 && G.planets[size_t(x)].owner != p ? 1 : 0;
            curValue_ = targets;
            const int c = fpick(p, Ab::DsWhichFlagship, "Which of your flagships moved into " + sysName(sys) + "?",
                                {{0, "The Lord", "Capacity 7; explorers here gain an infantry."}, {1, "The Lady", "Capacity 3, Bombardment 5 (x4)."}});
            curValue_ = 0;
            if (c == 1) {
                G.dsLadySys = int16_t(sys);
                removeExcessCapacity(p, sys);
            }
        }
    }
    // Sal Sparrow (Nokar agent): a destroyer for the active player here, then a trade
    if (const int user = agentUser(p, "nokar"); user >= 0 && hasShips(G, p, sys) && unitsAvailable(G, p, UnitType::Destroyer) > 0) {
        agentFor(user, p);
        curValue_ = fleetLimit(G, p) - fleetUseIn(G, p, sys);  // (for the AI: room in the fleet pool)
        const bool use = fyes(user, Ab::DsSalSparrow, user == p ? "Exhaust Sal Sparrow: place 1 destroyer here?" : "Exhaust Sal Sparrow: " + pn(p) + " places 1 destroyer here?",
                              leaderText(findLeader("nokaragent")));
        curFor_ = -1;
        curValue_ = 0;
        if (use) {
            spendAgent(user, "nokar");
            agentDone(user, p);
            addUnits(p, UnitType::Destroyer, 1, sys, -1);
            ds5Event(E, p, "Sal Sparrow", pn(p) + " takes on a privateer destroyer.", sys);
            enforceFleetLimit(p);
            if (user != p && canOffer(G, user, p)) {
                const uint64_t bits = (uint64_t(1) << (user * 8 + p)) | (uint64_t(1) << (p * 8 + user));
                G.tradeAnyway |= bits;
                negotiate(user, p);
                G.tradeAnyway &= ~bits;
            }
        }
    }
}

void Rules::ds5EndTactical(int p, int sys) {
    (void)sys;
    // Raid Leaders goes back
    const int gh = playerOfFaction(G, "ghemina");
    if (gh >= 0 && p >= 0 && p < G.numPlayers) {
        const int16_t note = makeNote(gh, kNoteFaction);
        auto& v = G.players[size_t(p)].notesInPlay;
        if (p != gh && std::find(v.begin(), v.end(), note) != v.end()) returnNote(p, note);
    }
    // Hired Guns: the buyer keeps what their reinforcements allow
    if (hired_.buyer >= 0) {
        const int b = hired_.buyer, s = hired_.sys;
        Force& f = G.systems[size_t(s)].space[size_t(b)];
        int kept = 0, lost = 0;
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!hired_.n[t]) continue;
            const int here = std::min<int>(hired_.n[t], f.n[t]);
            const int over = std::max(0, unitsOnBoard(G, b, UnitType(t)) - unitLimitOf(G, b, UnitType(t)));
            const int drop = std::min(here, over);
            if (drop > 0) f.remove(UnitType(t), drop, false);
            kept += here - drop;
            lost += drop;
            // 0.43.0 (debug pass): hired ships that retreated (or moved) out of the active system - still more than
            // the buyer's reinforcements allow: the extra ones go home from wherever they are
            int left = G.fixes >= 2 ? unitsOnBoard(G, b, UnitType(t)) - unitLimitOf(G, b, UnitType(t)) : 0;
            for (size_t z = 0; z < G.systems.size() && left > 0; ++z) {
                Force& g2 = G.systems[z].space[size_t(b)];
                const int k = std::min<int>(left, g2.n[t]);
                if (k <= 0) continue;
                g2.remove(UnitType(t), k, false);
                left -= k;
                lost += k;
                kept = std::max(0, kept - k);
                removeExcessCapacity(b, int(z));
            }
        }
        if (kept + lost > 0)
            ds5Event(E, b, "Hired Guns", pn(b) + " keeps " + std::to_string(kept) + " hired ship" + (kept == 1 ? "" : "s") +
                                             (lost ? " (" + std::to_string(lost) + " sent home)." : "."), s);
        hired_ = Hired();
        enforceFleetLimit(b);
    }
    ghemSync();
}

// After production: Rally to the Cause and Liberator (Free Systems), and which Ghemina flagship was built.
void Rules::ds5Produced(int p, int sys, const Force& made) {
    if (p < 0 || p >= G.numPlayers) return;
    Player& pl = G.players[size_t(p)];
    if (fac(p, "freesystems") && sys == pl.homeSystem && made.ships() > 0 && !(dsOncePerAction_ & 1)) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (int(s) == sys || G.systems[s].isHyperlane()) continue;
            bool trait = false, legendary = false, others = false;
            for (int16_t x : G.systems[s].planets) {
                trait = trait || hasTraitOf(G, x, Trait::Cultural) || hasTraitOf(G, x, Trait::Hazardous) || hasTraitOf(G, x, Trait::Industrial);
                legendary = legendary || isLegendary5(G, x);
            }
            for (int q = 0; q < kSides; ++q) others = others || (q != p && hasUnits(G, q, int(s)));
            if (trait && !legendary && !others) opts.push_back({int(s), sysName(int(s)), ""});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Don't", ""});
            const int s = fpick(p, Ab::DsRally, "Rally to the Cause: produce up to 2 ships in which system?", opts,
                                "A system with a cultural, hazardous or industrial planet, no legendary planet and nobody else's units.", true);
            if (s >= 0) {
                dsOncePerAction_ = uint8_t(dsOncePerAction_ | 1);
                ds5Event(E, p, "Rally to the Cause", pn(p) + " rallies the free worlds of " + sysName(s) + ".", s);
                const bool na = notProductionAbility_;  // (0.58.2, fixes 21: "produce", not PRODUCTION - no Sarween Tools)
                if (G.fixes >= 21) notProductionAbility_ = true;
                productionCore(p, s, 2, false, true);
                notProductionAbility_ = na;
                // Liberator (mech): 1 trade good for a mech on one of their planets next to it
                if (mechAbilities(G) && unitsAvailable(G, p, UnitType::Mech) > 0 && pl.tradeGoods > 0) {
                    std::vector<Option> where;
                    std::vector<int> zone{s};
                    for (int16_t a : G.systems[size_t(s)].adj) zone.push_back(a);
                    for (int z : zone)
                        for (int16_t x : G.systems[size_t(z)].planets)
                            if (G.planets[size_t(x)].owner == p && !isDMZ(G, x)) where.push_back({x, planetName(x), ""});
                    if (!where.empty()) {
                        where.push_back({-1, "Don't", ""});
                        const int x = fpick(p, Ab::DsLiberator, "Liberator: 1 trade good for a mech on which planet?", where, "", false, true);
                        if (x >= 0) {
                            pl.tradeGoods = int8_t(pl.tradeGoods - 1);
                            addUnits(p, UnitType::Mech, 1, G.planets[size_t(x)].system, x);
                            ds5Event(E, p, "Liberator", pn(p) + " lands a Liberator on " + planetName(x) + ".", G.planets[size_t(x)].system);
                        }
                    }
                }
            }
        }
    }
    // The Lady & The Lord: a new flagship
    if (fac(p, "ghemina") && made.n[int(UnitType::Flagship)] > 0 && G.dsLadySys < 0 && G.fixes < 16) {  // (older games)
        if (unitsOnBoard(G, p, UnitType::Flagship) >= 2 || made.n[int(UnitType::Flagship)] >= 2) {
            G.dsLadySys = int16_t(sys);  // (the other one is the Lord)
        } else {
            const int c = fpick(p, Ab::DsWhichFlagship, "Which flagship did you build?",
                                {{1, "The Lady", "Capacity 3, Bombardment 5 (x4)."}, {0, "The Lord", "Capacity 7; explorers in its system gain an infantry."}});
            if (c == 1) G.dsLadySys = int16_t(sys);
        }
        removeExcessCapacity(p, sys);
    }
}

// Industry Secrets (the Axis's note): +4 production and 1 cheaper this time.
int Rules::ds5ProductionBonus(int p, int sys) {
    const int ax = playerOfFaction(G, "axis");
    if (ax < 0 || ax == p || p < 0 || p >= G.numPlayers || !holdsNote(G, p, ax, kNoteFaction)) return 0;
    if (!fyes(p, Ab::DsIndustrySecrets, "Play " + pn(ax) + "'s Industry Secrets?", "+4 to this production and 1 cheaper; it goes back at the start of the status phase."))
        return 0;
    Player& pl = G.players[size_t(p)];
    pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), makeNote(ax, kNoteFaction)), pl.notes.end());
    pl.notesInPlay.push_back(makeNote(ax, kNoteFaction));
    dsDiscount2_ += 1;
    ds5Event(E, p, "Industry Secrets", pn(p) + " uses the Axis's industry secrets: +4 production, 1 cheaper.", sys);
    return 4;
}

void Rules::ds5Control(int planet, int p, int old) {
    if (p < 0 || p >= G.numPlayers || p == old || G.phase == Phase::Setup) return;
    const int sys = G.planets[size_t(planet)].system;
    const bool tactical = p == G.activePlayer && G.phase == Phase::Action && G.activeSystem == sys;
    // Broadcast Teams (the Free Systems' note): up to 2 ships here
    const int fs = playerOfFaction(G, "freesystems");
    if (tactical && fs >= 0 && fs != p && holdsNote(G, p, fs, kNoteFaction) && !otherShipsIn(G, p, sys) &&
        fyes(p, Ab::DsBroadcast, "Play " + pn(fs) + "'s Broadcast Teams: produce up to 2 ships in " + sysName(sys) + "?", "The note goes back.")) {
        returnNote(p, makeNote(fs, kNoteFaction));
        ds5Event(E, p, "Broadcast Teams", pn(p) + "'s broadcast rallies a fleet over " + planetName(planet) + ".", sys);
        const bool na = notProductionAbility_;  // (0.58.2, fixes 21: "produce", not PRODUCTION - no Sarween Tools)
        if (G.fixes >= 21) notProductionAbility_ = true;
        productionCore(p, sys, 2, false, true);
        notProductionAbility_ = na;
    }
    // President Cyhn (Free Systems commander): a non-home planet taken in a tactical action - 1 ship with their ships here
    if (tactical && commanderUsable(G, p, "freesystems") && !isHome5(G, planet) && hasShips(G, p, sys) &&
        fyes(p, Ab::DsCyhn, "President Cyhn: produce 1 ship in " + sysName(sys) + "?", ""))
        dsProduceOne(p, sys, 0, "President Cyhn");
    // Mycelium Ring (Myko-Mentori space docks): DEPLOY - 4 infantry (II: 3) become a space dock
    if (fac(p, "mykomentori") && unitsAvailable(G, p, UnitType::SpaceDock) > 0 && !isDMZ(G, planet)) {
        Force& f = G.planets[size_t(planet)].units[size_t(p)];
        const int need = owns(p, "dsmykosd") ? 3 : 4;
        if (!f.count(UnitType::SpaceDock) && f.count(UnitType::Infantry) >= need &&
            fyes(p, Ab::DsMycelium, "Mycelium Ring: " + std::to_string(need) + " infantry on " + planetName(planet) + " become a space dock?", "")) {
            f.remove(UnitType::Infantry, need, false);
            addUnits(p, UnitType::SpaceDock, 1, sys, planet);
            ds5Event(E, p, "Mycelium Ring", "A mycelium ring sprouts on " + planetName(planet) + ".", sys);
        }
    }
}

// The Lord (Ghemina flagship): a planet explored in its system - the explorer places 1 infantry there.
void Rules::ds5Explored(int p, int planet) {
    if (planet < 0 || p < 0 || p >= G.numPlayers) return;
    const int gh = playerOfFaction(G, "ghemina");
    const int sys = G.planets[size_t(planet)].system;
    if (gh < 0 || !flagshipAbility(G, gh, "ghemina") || !lordIn(G, gh, sys) || unitsAvailable(G, p, UnitType::Infantry) <= 0 || isDMZ(G, planet)) return;
    addUnits(p, UnitType::Infantry, 1, sys, planet);
    ds5Event(E, p, "The Lord", pn(p) + " finds a recruit on " + planetName(planet) + ".", sys);
}

void Rules::ds5TurnStart(int p) {
    Player& pl = G.players[size_t(p)];
    // Amandia Pholdis (Myko-Mentori mech): a mech from the card replaces 1 of their infantry
    if (fac(p, "mykomentori") && pl.dsMykoMechs > 0 && unitsAvailable(G, p, UnitType::Mech) > 0) {
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p))
            if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry) && !isDMZ(G, x)) opts.push_back({x, planetName(x), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Not now", ""});
            const int x = fpick(p, Ab::DsAmandia, "Amandia Pholdis: one of your infantry becomes a mech - on which planet?", opts, "", false, true);
            if (x >= 0) {
                const int sys = G.planets[size_t(x)].system;
                G.planets[size_t(x)].units[size_t(p)].remove(UnitType::Infantry, 1, false);
                addUnits(p, UnitType::Mech, 1, sys, x);
                pl.dsMykoMechs = int8_t(pl.dsMykoMechs - 1);
                ds5Event(E, p, "Amandia Pholdis", pn(p) + "'s mech regrows on " + planetName(x) + ".", sys);
            }
        }
    }
}

// End of a turn: the holder of an Axis Order may use it.
void Rules::ds5TurnEnd(int p) {
    if (p < 0 || p >= G.numPlayers || fac(p, "axis")) return;
    for (int i = 0; i < 8; ++i) {
        if (G.dsOrders[i] != p || ((G.dsOrderEx >> i) & 1)) continue;
        const UnitType t = orderType(i);
        const int n = i >= 6 ? 2 : 1;
        if (unitsAvailable(G, p, t) <= 0) continue;
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (hasShips(G, p, int(s)) && G.systems[s].hasToken(p)) opts.push_back({int(s), sysName(int(s)), ""});
        if (opts.empty()) continue;
        opts.push_back({-1, "Keep it for later", ""});
        const int s = fpick(p, Ab::DsOrderUse, orderName(i) + ": place " + (n > 1 ? "2 destroyers" : std::string("1 ") + unitTypeName(t)) + " where?", opts,
                            axisOrderText(i), true);
        if (s < 0) continue;
        G.dsOrderEx = uint8_t(G.dsOrderEx | (1u << i));
        addUnits(p, t, n, s, -1);
        ds5Event(E, p, "Axis Order", pn(p) + " calls in an " + orderName(i) + ".", s);
        enforceFleetLimit(p);
        // Designer TckVsk (Axis commander): that unit's upgrade for 6 resources
        const int ax = playerOfFaction(G, "axis");
        if (ax >= 0 && commanderUsable(G, ax, "axis")) {
            int up = -1;
            for (int k = 0; k < kTechCount && up < 0; ++k)
                if (kTechs[k].unitUpgrade && kTechs[k].faction < 0 && upgradeUnitType(k) == int(t) && techImplemented(k) &&
                    (kTechs[k].source == Source::Base || kTechs[k].source == Source::PoK))
                    up = k;
            if (up >= 0 && !G.players[size_t(ax)].hasTech(up) && spendable(G, ax, false) >= 6 &&
                fyes(ax, Ab::DsTckVsk, std::string("Designer TckVsk: spend 6 resources for ") + kTechs[up].name + "?", "") && pay(ax, 6, false, "Designer TckVsk", true)) {
                researchTech(ax, up, false);
                ds5Event(E, ax, "Designer TckVsk", pn(ax) + " reverse-engineers " + kTechs[up].name + ".");
            }
        }
    }
}

// Allant (Edyn agent): after a player passes, a player of their choice may perform 1 action; then a look at the agenda deck.
void Rules::ds5Passed(int p) {
    (void)p;
    const int ed = playerOfFaction(G, "edyn");
    if (ed < 0 || !canUseAgent(ed, "edyn")) return;
    std::vector<Option> opts;
    for (int q = 0; q < G.numPlayers; ++q)
        if (!G.players[size_t(q)].eliminated) opts.push_back({q, pn(q), q == p ? "The player who just passed." : ""});
    opts.push_back({-1, "Don't", ""});
    curValue_ = p;
    const int q = fpick(ed, Ab::DsAllant, "Exhaust Allant: which player may perform 1 action now?", opts, leaderText(findLeader("edynagent")));
    curValue_ = 0;
    if (q < 0) return;
    spendAgent(ed, "edyn");
    ds5Event(E, ed, "Allant", pn(ed) + "'s Allant grants " + pn(q) + " one more action.");
    extraAction(q);
    if (G.agendaDeck.empty()) return;
    const int a = G.agendaDeck.back();
    feedNote(ed, std::string("Allant: the top agenda is ") + kAgendas[a].name + ".");
    const int prev = curAgenda_;
    curAgenda_ = a;
    const bool drop = fyes(ed, Ab::DsAllantPeek, std::string("Allant: discard ") + kAgendas[a].name + " from the top of the agenda deck?", agendaText(a, false));
    curAgenda_ = prev;
    if (drop) {
        G.agendaDeck.pop_back();
        G.agendaDiscard.push_back(int16_t(a));
        say(ed, pn(ed) + " discards the top agenda (Allant).");
    }
}

void Rules::extraAction(int q) {
    if (q < 0 || q >= G.numPlayers || G.players[size_t(q)].eliminated) return;
    const int prevActive = G.activePlayer, prevLast = lastActiveSystem_;
    const bool prevEnded = turnEnded_;
    G.activePlayer = int8_t(q);
    lastActiveSystem_ = -1;
    turnEnded_ = false;
    takeTurn(q, true);
    G.activePlayer = int8_t(prevActive);
    lastActiveSystem_ = prevLast;
    turnEnded_ = prevEnded;
}

// ================================================================== phases
void Rules::ds5StatusStart() {
    // Axis Orders that were used go back to the Axis - into their play area, for sale again (owner, D112: fixes >= 2;
    // before, to their reinforcements, to be bought again with commodities)
    const int axis = playerOfFaction(G, "axis");
    for (int i = 0; i < 8; ++i)
        if ((G.dsOrderEx >> i) & 1) G.dsOrders[i] = int8_t(G.fixes >= 2 && axis >= 0 ? axis : -1);
    G.dsOrderEx = 0;
    // Industry Secrets (the Axis's note) goes back
    const int ax = playerOfFaction(G, "axis");
    if (ax >= 0)
        for (int q = 0; q < G.numPlayers; ++q) {
            const int16_t note = makeNote(ax, kNoteFaction);
            auto& v = G.players[size_t(q)].notesInPlay;
            if (q != ax && std::find(v.begin(), v.end(), note) != v.end()) {
                quietNoteReturn_ = true;
                returnNote(q, note);
                quietNoteReturn_ = false;
            }
        }
    ghemSync();
}

// Prescient Memories (Myko-Mentori): all 4 Omen dice rolled again.
void Rules::ds5StrategyStart() {
    dsOncePerAction_ = 0;
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!fac(p, "mykomentori")) continue;
        Player& pl = G.players[size_t(p)];
        std::string v;
        for (int8_t& o : pl.dsOmen) {
            o = int8_t(E.rng().d10());
            v += (v.empty() ? "" : " ") + std::to_string(o);
        }
        ds5Event(E, p, "Prescient Memories", pn(p) + " casts its Omen dice: " + v + ".");
    }
}

// Kadryn (Edyn commander): a secret objective instead of scoring a public one.
bool Rules::dsKadryn(int p, int scorable) {
    if (!commanderUsable(G, p, "edyn") || G.secretDeck.empty()) return false;
    curValue_ = scorable;
    const bool use = fyes(p, Ab::DsKadryn, "Kadryn: draw 1 secret objective instead of scoring a public objective?", "");
    curValue_ = 0;
    if (!use) return false;
    drawSecret(p);
    ds5Event(E, p, "Kadryn", pn(p) + " takes a secret path instead of a public one.");
    return true;
}

// ================================================================== agendas
int Rules::dsPredictionOf(int p) const {
    if (p < 0 || p >= G.numPlayers) return -1;
    if (dsUnityGuess_[p] != -1) return dsUnityGuess_[p];
    return p == playerOfFaction(G, "edyn") ? dsEdynGuess_ : -1;
}

void Rules::ds5AgendaRevealed(int a) {
    dsEdynGuess_ = -1;
    for (int& r : dsEdynRider_) r = -1;
    for (int& r : dsUnityGuess_) r = -1;
    const std::vector<Option> outs = agendaOutcomes(a);
    if (outs.empty()) return;
    const int ed = playerOfFaction(G, "edyn");
    // Unity Algorithm for another owner - a Nekro copy (0.44.6, fixes 10): their own prediction, once per agenda phase
    if (G.fixes >= 10)
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == ed || G.players[size_t(q)].eliminated || !owns(q, "dsedyng") || G.players[size_t(q)].dsUnityRound == G.round) continue;
            std::vector<Option> guess = outs;
            guess.push_back({-999, "Don't predict", ""});
            const int prevA = curAgenda_;
            curAgenda_ = a;
            const int gq = fpick(q, Ab::DsRadiance, "Unity Algorithm: predict the outcome of " + std::string(kAgendas[a].name), guess,
                                 "If you are right, you may score a public objective you fulfil; those who voted for it draw a secret objective.");
            curAgenda_ = prevA;
            if (gq != -999) {
                dsUnityGuess_[q] = gq;
                ds5Event(E, q, "Unity Algorithm", pn(q) + " predicts: " + outcomeName(a, gq) + ".");
            }
        }
    // Encrypted Trade Hub: the Edyn always vote last (0.44.5, fixes 9: or a Nekro with a Valefar copy of it)
    if (G.fixes >= 9) {
        if (ed >= 0 && owns(ed, "dsedyny") && lastVoter_ < 0) lastVoter_ = ed;
        for (int q = 0; q < G.numPlayers && lastVoter_ < 0; ++q)
            if (!G.players[size_t(q)].eliminated && owns(q, "dsedyny")) lastVoter_ = q;
    }
    if (ed < 0) return;
    if (G.fixes < 9 && owns(ed, "dsedyny") && lastVoter_ < 0) lastVoter_ = ed;
    const int prev = curAgenda_;
    curAgenda_ = a;
    // Radiance (and Unity Algorithm): a prediction
    std::vector<Option> guess = outs;
    guess.push_back({-999, "Don't predict", ""});
    const int g = fpick(ed, Ab::DsRadiance, "Radiance: predict the outcome of " + std::string(kAgendas[a].name), guess,
                        owns(ed, "dsedyng") && G.players[size_t(ed)].dsUnityRound != G.round
                            ? "If you are right, another player's command token goes into one of your Sigils - and Unity Algorithm: you may "
                              "score a public objective you fulfil; those who voted for it draw a secret objective."
                            : "If you are right, another player's command token goes into one of your Sigils.");
    if (g != -999) {
        dsEdynGuess_ = g;
        ds5Event(E, ed, "Radiance", pn(ed) + " predicts: " + outcomeName(a, g) + ".");
    }
    // Edyn Rider (the Edyn's note): no vote, a prediction
    for (int q : clockwiseFrom(G, G.speaker)) {
        if (q == ed || G.players[size_t(q)].eliminated || !holdsNote(G, q, ed, kNoteFaction)) continue;
        if (!fyes(q, Ab::DsEdynRider, "Play " + pn(ed) + "'s Edyn Rider on " + kAgendas[a].name + "?",
                  "You won't vote on this agenda. Predict the outcome: if you're right, another player's command token goes into a system with your units."))
            continue;
        const int r = fpick(q, Ab::DsEdynRider, "Edyn Rider: predict the outcome of " + std::string(kAgendas[a].name), outs);
        dsEdynRider_[q] = r;
        cannotVote_ = uint8_t(cannotVote_ | (1u << q));
        returnNote(q, makeNote(ed, kNoteFaction));
        ds5Event(E, q, "Edyn Rider", pn(q) + " plays the Edyn Rider and predicts: " + outcomeName(a, r) + ".");
    }
    curAgenda_ = prev;
}

void Rules::ds5AgendaResolved(int outcome, const std::vector<int>& voted) {
    const int ed = playerOfFaction(G, "edyn");
    const bool right = ed >= 0 && dsEdynGuess_ != -1 && dsEdynGuess_ == outcome;
    // a command token from another player's reinforcements into one of 'zone' (Radiance: Sigils; Edyn Rider: the holder's systems)
    auto token = [&](int who, const std::vector<int>& zone, Ab ab, const std::string& why) {
        std::vector<Option> opts;
        for (int s : zone)
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != who && !G.players[size_t(q)].eliminated && reinforcementTokens(q) > 0 && !G.systems[size_t(s)].hasToken(q))
                    opts.push_back({s * 16 + q, pn(q) + "'s token in " + sysName(s), ""});
        if (opts.empty()) return;
        opts.push_back({-1, "Don't", ""});
        const int c = fpick(who, ab, why, opts, "They can't activate that system while it is there.", true);
        if (c < 0) return;
        const int s = c / 16, q = c % 16;
        G.systems[size_t(s)].tokens = uint16_t(G.systems[size_t(s)].tokens | (1u << q));
        Player& ql = G.players[size_t(q)];  // (from their reinforcements: nothing leaves their pools)
        (void)ql;
        say(who, pn(who) + " places " + pn(q) + "'s command token in " + sysName(s) + ".", s);
    };
    if (right) {
        say(ed, pn(ed) + "'s prediction was right.");
        token(ed, sigils(G), Ab::DsSigilToken, "Radiance: whose command token goes into which Sigil?");
        // Unity Algorithm: once per agenda phase, a public objective - and the voters for it draw a secret
        Player& el = G.players[size_t(ed)];
        if (owns(ed, "dsedyng") && el.dsUnityRound != G.round) {
            el.dsUnityRound = int16_t(G.round);
            std::vector<Option> opts;
            for (int o : scorablePublic(ed))
                opts.push_back({o, std::string(kObjectives[o].name) + " - " + std::to_string(kObjectives[o].points) + " VP", objectiveSummary(o)});
            if (!opts.empty()) {
                opts.push_back({-1, "Don't score", ""});
                const int o = fpick(ed, Ab::DsUnity, "Unity Algorithm: score a public objective?", opts);
                if (o >= 0) scoreObjective(ed, o);
            }
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != ed && size_t(q) < voted.size() && voted[size_t(q)] == outcome) drawSecret(q);
            ds5Event(E, ed, "Unity Algorithm", "The Unity Algorithm resonates: those who voted with it draw a secret objective.");
        }
    }
    // Unity Algorithm for other owners - a Nekro copy (0.44.6, fixes 10)
    for (int u = 0; u < G.numPlayers && G.fixes >= 10; ++u) {
        if (dsUnityGuess_[u] == -1 || u == ed) continue;
        const bool hit = dsUnityGuess_[u] == outcome;
        dsUnityGuess_[u] = -1;
        Player& ul = G.players[size_t(u)];
        if (!hit || ul.eliminated || !owns(u, "dsedyng") || ul.dsUnityRound == G.round) continue;
        ul.dsUnityRound = int16_t(G.round);
        say(u, pn(u) + "'s prediction was right.");
        std::vector<Option> opts;
        for (int o : scorablePublic(u))
            opts.push_back({o, std::string(kObjectives[o].name) + " - " + std::to_string(kObjectives[o].points) + " VP", objectiveSummary(o)});
        if (!opts.empty()) {
            opts.push_back({-1, "Don't score", ""});
            const int o = fpick(u, Ab::DsUnity, "Unity Algorithm: score a public objective?", opts);
            if (o >= 0) scoreObjective(u, o);
        }
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != u && size_t(q) < voted.size() && voted[size_t(q)] == outcome) drawSecret(q);
        ds5Event(E, u, "Unity Algorithm", "The Unity Algorithm resonates: those who voted with it draw a secret objective.");
    }
    // Encrypted Trade Hub: an outcome the Edyn voted for or predicted - each of its voters gains 1 commodity
    // (0.44.5, fixes 9: every owner - a Nekro copy too - and riders count as predictions)
    std::vector<int> hubs;
    if (G.fixes >= 9) {
        for (int q = 0; q < G.numPlayers; ++q) {
            if (G.players[size_t(q)].eliminated || !owns(q, "dsedyny")) continue;
            bool hit = (q == ed && right) || (size_t(q) < voted.size() && voted[size_t(q)] == outcome);
            for (size_t i = 0; i + 1 < predictions_[q].size(); i += 2) hit = hit || predictions_[q][i + 1] == outcome;
            if (hit) hubs.push_back(q);
        }
    } else if (ed >= 0 && owns(ed, "dsedyny") && (right || (size_t(ed) < voted.size() && voted[size_t(ed)] == outcome))) {
        hubs.push_back(ed);
    }
    for (int hub : hubs) {
        int n = 0;
        for (int q = 0; q < G.numPlayers; ++q) {
            Player& ql = G.players[size_t(q)];
            if (size_t(q) < voted.size() && voted[size_t(q)] == outcome && ql.commodities < commodityCap(G, ql)) {
                { const int cb_ = ql.commodities; ql.commodities = int8_t(ql.commodities + 1); commodityGained(q, cb_); }
                ++n;
            }
        }
        if (n) ds5Event(E, hub, "Encrypted Trade Hub", std::to_string(n) + " players who voted for it gain 1 commodity each.");
    }
    // Edyn Rider pays out
    for (int q = 0; q < G.numPlayers; ++q) {
        if (dsEdynRider_[q] == -1) continue;
        if (dsEdynRider_[q] == outcome) {
            std::vector<int> zone;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (hasUnits(G, q, int(s))) zone.push_back(int(s));
            say(q, pn(q) + "'s Edyn Rider was right.");
            token(q, zone, Ab::DsEdynRider, "Edyn Rider: whose command token goes into which of your systems?");
        } else {
            say(q, pn(q) + "'s Edyn Rider was wrong.");
        }
        dsEdynRider_[q] = -1;
    }
    dsEdynGuess_ = -1;
}

// Envoy Network (Free Systems): +4 votes when the planets spent include a cultural, a hazardous and an industrial one.
int Rules::ds5VoteBonus(int p, const std::vector<int>& spent) {
    if (!owns(p, "dsfreeg")) return 0;
    bool c = false, h = false, i = false;
    for (int x : spent) {
        c = c || hasTraitOf(G, x, Trait::Cultural);
        h = h || hasTraitOf(G, x, Trait::Hazardous);
        i = i || hasTraitOf(G, x, Trait::Industrial);
    }
    if (!(c && h && i)) return 0;
    ds5Event(E, p, "Envoy Network", pn(p) + "'s envoys add 4 votes.");
    return 4;
}

// Grace (Edyn): after a primary, the secondary of a lower strategy card nobody has used yet - free.
void Rules::dsAfterPrimary(int p, int card) {
    if (p < 0 || p >= G.numPlayers || !fac(p, "edyn") || card <= 0) return;
    Player& pl = G.players[size_t(p)];
    if (pl.dsGraceRound == G.round) return;
    std::vector<Option> opts;
    for (int c = 0; c < card; ++c) {
        bool used = false;
        for (const Player& q : G.players)
            for (int8_t x : q.cards) used = used || (x == c && ((q.cardsUsed >> c) & 1));
        if (!used) opts.push_back({c, std::to_string(c + 1) + " " + strategyName(c), strategySecondary(c)});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "Don't", ""});
    const int c = fpick(p, Ab::DsGrace, "Grace: resolve the secondary ability of a lower strategy card?", opts, "Without a command token; once per action phase.");
    if (c < 0) return;
    pl.dsGraceRound = int16_t(G.round);
    ds5Event(E, p, "Grace", pn(p) + " extends its grace: the secondary of " + strategyName(c) + ".");
    const bool prevForced = forcedSecondary_, prevGrace = graceFree_;
    forcedSecondary_ = true;
    graceFree_ = true;
    secondaryAbility(p, c);
    forcedSecondary_ = prevForced;
    graceFree_ = prevGrace;
}

// Military Industrial Complex (Axis): after gaining or refilling commodities, Axis Orders bought with them.
void Rules::dsReplenished(int p, bool replenish) {
    if (breakthroughsOn(G) && (replenish || G.fixes < 8)) dsbtReplenished(p);  // Arms Brokerage (0.43.0; 0.44.4: only a replenish)
    if (p < 0 || p >= G.numPlayers || !fac(p, "axis")) return;
    Player& pl = G.players[size_t(p)];
    for (int guard = 0; guard < 8; ++guard) {
        std::vector<Option> opts;
        for (int i = 0; i < 8; ++i) {
            if (G.dsOrders[i] != -1 || orderCost(i) > pl.commodities) continue;
            bool dup = false;
            for (const Option& o : opts) dup = dup || orderType(o.id) == orderType(i);
            if (!dup) opts.push_back({i, orderName(i) + " (" + std::to_string(orderCost(i)) + ")", axisOrderText(i)});
        }
        if (opts.empty()) return;
        opts.push_back({-1, "Keep the commodities", ""});
        const int i = fpick(p, Ab::DsOrderBuy, "Military Industrial Complex: put an Axis Order in your play area?", opts,
                            std::to_string(pl.commodities) + " commodities; other players buy the orders from you in deals.");
        if (i < 0) return;
        pl.commodities = int8_t(pl.commodities - orderCost(i));
        G.dsOrders[i] = int8_t(p);
        ds5Event(E, p, "Military Industrial Complex", pn(p) + " readies an " + orderName(i) + " for sale.");
    }
}

// Diplomats (Free Systems): once per action, an uncontrolled planet's card toward a payment. Returns the value used.
int Rules::dsDiplomats(int p, int amount, bool influence, const std::string& why, int& planet) {
    planet = -1;
    if (!fac(p, "freesystems") || (dsOncePerAction_ & 2) || amount <= 0) return 0;
    std::vector<Option> opts;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.owner >= 0 || ps.exhausted || ps.destroyed || isMecatol5(G, int(x)) || isHome5(G, int(x))) continue;
        const int v = influence ? planetInfluence(G, int(x)) : planetResources(G, int(x));
        if (v > 0) opts.push_back({int(x), planetName(int(x)), std::to_string(v) + (influence ? " influence" : " resources")});
    }
    if (opts.empty()) return 0;
    opts.push_back({-1, "Don't", ""});
    curValue_ = amount;
    const int x = fpick(p, Ab::DsDiplomats, "Diplomats: exhaust an uncontrolled planet toward " + why + "?", opts,
                        std::to_string(amount) + (influence ? " influence" : " resources") + " needed; once per action." + kRedNote +
                            "Mecatol Rex and home planets can't be called on (Free People).",
                        false, true);
    curValue_ = 0;
    if (x < 0) return 0;
    planet = x;
    G.planets[size_t(x)].exhausted = true;
    dsOncePerAction_ = uint8_t(dsOncePerAction_ | 2);
    ds5Event(E, p, "Diplomats", pn(p) + "'s diplomats call on " + planetName(x) + ".");
    return influence ? planetInfluence(G, x) : planetResources(G, x);
}

// Soul Cage (Coprinus Comatus, Myko-Mentori hero): another player's hero - one of those it can use - is attached to it
// instead of purged. Their own hero purged: the attachments go too.
void Rules::dsSoulCaged(int p, int leader) {
    const int my = playerOfFaction(G, "mykomentori");
    if (my < 0) return;
    if (p == my) {
        G.dsSoulCage.clear();
        return;
    }
    if (!heroUnlocked(G, my) || leader < 0) return;
    static const char* kCageable[] = {"kollecchero", "mortheushero", "cymiaehero", "gheminaherolady", "gheminaherolord", "freesystemshero"};
    bool ok = G.fixes >= 2;  // 0.43.0 (D116): every other player's hero
    for (const char* k : kCageable) ok = ok || !std::strcmp(kLeaders[leader].key, k);
    if (!ok || kLeaders[leader].type != kHero) return;
    if (std::find(G.dsSoulCage.begin(), G.dsSoulCage.end(), int16_t(leader)) != G.dsSoulCage.end()) return;
    G.dsSoulCage.push_back(int16_t(leader));
    ds5Event(E, my, "Soul Cage", std::string(kLeaders[leader].name) + " is bound to " + pn(my) + "'s Soul Cage.");
}

// The Lady & The Lord: the Lady's system lost its Ghemina flagships some other way (a retreat...) - follow her.
void Rules::ghemSync() {
    const int gh = playerOfFaction(G, "ghemina");
    if (gh < 0 || G.dsLadySys < 0 || G.fixes >= 16 || flagshipsIn(G, gh, G.dsLadySys) > 0) return;
    int found = -1;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const int n = flagshipsIn(G, gh, int(s));
        if (n > 0 && (found < 0 || n > flagshipsIn(G, gh, found))) found = int(s);
    }
    G.dsLadySys = int16_t(found);
}

// ================================================================== component actions
namespace {
// the leader index of a hero by its key (the Ghemina have two)
int heroIdx(const Game& G, int p, const char* key) {
    const auto& ls = G.players[size_t(p)].leaders;
    for (size_t i = 0; i < ls.size(); ++i)
        if (!std::strcmp(kLeaders[ls[i].leader].key, key)) return int(i);
    return -1;
}
// Cordo Haved (Free Systems agent): the legendary planet abilities it can lend
std::vector<Option> cordoOptions(const Game& G, int p) {
    std::vector<Option> o;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.owner < 0 || ps.destroyed || !*legendaryName(ps.def)) continue;
        const char* k = kPlanets[ps.def].key;
        const std::string n = std::string(legendaryName(ps.def)) + ": ";
        const int id = int(x) * 10;
        if (!std::strcmp(k, "hopesend")) {
            if (unitsAvailable(G, p, UnitType::Mech) > 0) o.push_back({id + 1, n + "1 mech on a planet you control", ""});
            o.push_back({id + 2, n + "draw 1 action card", ""});
        } else if (!std::strcmp(k, "primor")) {
            if (unitsAvailable(G, p, UnitType::Infantry) > 0) o.push_back({id + 1, n + "2 infantry on planets you control", ""});
        } else if (!std::strcmp(k, "mirage")) {
            o.push_back({id + 1, n + "2 fighters with your ships", ""});
        } else if (!std::strcmp(k, "mallice")) {
            o.push_back({id + 1, n + "gain 2 trade goods", ""});
            if (G.players[size_t(p)].commodities > 0) o.push_back({id + 2, n + "convert your commodities", ""});
        } else if (!std::strcmp(k, "silence")) {
            if (unitsAvailable(G, p, UnitType::Cruiser) > 0) o.push_back({id + 1, n + "1 cruiser with your ships", ""});
        }
    }
    return o;
}
// planets a Ghemina hero can reach: non-home, not Mecatol Rex, in or next to a system with one of p's flagships
std::vector<int> raidPlanets(const Game& G, int p) {
    std::vector<int> out;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.destroyed || isHome5(G, int(x)) || isMecatol5(G, int(x))) continue;
        bool near = flagshipsIn(G, p, ps.system) > 0;
        for (int16_t a : G.systems[size_t(ps.system)].adj) near = near || flagshipsIn(G, p, a) > 0;
        if (near) out.push_back(int(x));
    }
    return out;
}
// The planets each Ghemina hero can take: Korela - any with units on it; Kantrus - not ours, nobody else's units, no DMZ
std::vector<Option> korelaTargets(const Game& G, int p) {
    std::vector<Option> opts;
    for (int x : raidPlanets(G, p)) {
        int n = 0;
        for (int q = 0; q < kSides; ++q) n += G.planets[size_t(x)].units[size_t(q)].total();
        if (n > 0) opts.push_back({x, std::string(kPlanets[size_t(G.planets[size_t(x)].def)].name), std::to_string(n) + " units"});
    }
    return opts;
}
std::vector<Option> kantrusTargets(const Game& G, int p) {
    std::vector<Option> opts;
    for (int x : raidPlanets(G, p)) {
        bool others = false;
        for (int q = 0; q < kSides; ++q) others = others || (q != p && G.planets[size_t(x)].units[size_t(q)].total() > 0);
        if (G.fixes >= 20 && isStation(G, x)) continue;  // (0.57.1, as Peace Accords: stations aren't taken as empty planets)
        if (!others && G.planets[size_t(x)].owner != p && !isDMZ(G, x))
            opts.push_back({x, std::string(kPlanets[size_t(G.planets[size_t(x)].def)].name), std::to_string(planetResources(G, x)) + "/" + std::to_string(planetInfluence(G, x))});
    }
    return opts;
}
}  // namespace

void Rules::ds5Hero(int p, int nth, const std::string& text) {
    (void)nth;
    const auto& ls = G.players[size_t(p)].leaders;
    const int i = nth;
    if (i >= 0 && i < int(ls.size())) {
        LeaderState& l = G.players[size_t(p)].leaders[size_t(i)];
        l.status = kPurged;
        if (G.modules & kModDiscordant) dsSoulCaged(p, l.leader);
        purged(p);
        Event e;
        e.kind = EventKind::Leader;
        e.player = int8_t(p);
        e.card = int16_t(l.leader);
        e.label = "hero";
        e.text = text;
        E.emit(e);
    }
}

void Rules::ds5Options(int p, std::vector<Option>& o) {
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    const Player& pl = G.players[size_t(p)];
    // Free Systems
    if (fac(p, "freesystems") && canUseAgent(p, "freesystems") && !cordoOptions(G, p).empty())
        add(Ab::DsCordo, "Agent: Cordo Haved", "Exhaust: use the ability of a legendary planet any player controls.");
    else if (G.fixes >= 2 && !fac(p, "freesystems") && agentUser(p, "freesystems") >= 0 && !cordoOptions(G, p).empty())  // 0.43.0 (D102): lent
        add(Ab::DsCordo, "Agent: Cordo Haved (lent by " + pn(agentUser(p, "freesystems")) + ")",
            "Use the ability of a legendary planet any player controls; the Free Systems' agent is exhausted.");
    auto ottoPlanets = [&]() {
        std::vector<int> v;
        for (int x : controlledPlanets(G, p))
            if (!isHome5(G, x) && !isMecatol5(G, x) && x != G.dsHeart) v.push_back(x);
        return v;
    };
    if (fac(p, "freesystems") && heroUnlocked(G, p) && !ottoPlanets().empty())
        add(Ab::DsOtto, "Hero: Count Otto P'may", "Ready a planet of yours and clear it: nobody can put units there again. Purged afterwards.");
    // Edyn
    if (fac(p, "edyn") && heroUnlocked(G, p) && !sigils(G).empty() && !G.agendaDeck.empty())
        add(Ab::DsMidir, "Hero: Midir", "An agenda for each Sigil, resolved the way you choose. Purged afterwards.");
    // Nokar
    if (G.fixes < 2 && techReady(p, "dsnokay") && reinforcementTokens(p) > 0) {  // (0.43.0: a free action - takeTurn)
        bool any = false;
        for (size_t s = 0; s < G.systems.size() && !any; ++s) any = !G.systems[s].hasToken(p) && productionCapacity(G, p, int(s)) > 0;
        if (any) add(Ab::DsLocalContracts, "Local Contracts", "Exhaust: produce with one of your units, then put a command token in its system.");
    }
    if (fac(p, "nokar") && heroUnlocked(G, p)) add(Ab::DsStarsails, "Hero: Starsails", "This round each of your planets has Production 3. Purged at the end of the round.");
    // Axis
    if (techReady(p, "dsaxisy")) {
        bool spot = false;
        for (int x : controlledPlanets(G, p)) spot = spot || (!G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock) && !isDMZ(G, x));
        if (G.fixes >= 9 && unitsAvailable(G, p, UnitType::SpaceDock) <= 0) {  // (0.44.5: a dock to place or to move)
            bool dock = false;
            for (int x : controlledPlanets(G, p)) dock = dock || G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock) > 0;
            spot = spot && dock;
        }
        if (spot) add(Ab::DsEmergency, "Emergency Deployment", "Exhaust: place or move one of your space docks onto a planet you control.");
    }
    if (fac(p, "axis") && canUseAgent(p, "axis")) add(Ab::DsShipmonger, "Agent: Shipmonger Zsknck", "Exhaust: a player places a cruiser or destroyer with their ships (another player: you gain 2 commodities).");
    if (fac(p, "axis") && heroUnlocked(G, p)) {
        bool any = false;
        for (int8_t h : G.dsOrders) any = any || h == p;
        if (any) add(Ab::DsDemiQueen, "Hero: Demi-Queen Mdcksssk", "Give away Axis Orders from your play area - a promissory note back for each. Purged afterwards.");
    }
    // Myko-Mentori
    const int my = playerOfFaction(G, "mykomentori");
    if (my >= 0 && my != p && holdsNote(G, p, my, kNoteFaction)) add(Ab::DsInsight, "Gift of Insight", "Play it face up: once per turn you may roll one of your dice again.");
    if (fac(p, "mykomentori") && heroUnlocked(G, p) && G.fixes >= 2) {
        // 0.43.0 (D116): any bound hero whose ability is an action - matched by name to its action
        static const struct {
            const char* name;
            Ab ab;
        } kHeroActions[] = {
    {"Ahk-Syl Siven", Ab::NomadHero},
    {"Airo Shir Aur", Ab::MahactHero},
    {"Atropha", Ab::DsAtropha},
    {"Auberon Elyrin", Ab::DsAuberon},
    {"Banua Gowen", Ab::DsBanua},
    {"Bayan", Ab::DsBayan},
    {"C.E.O. Ken Tucc", Ab::DsKenTucc},
    {"Conservator Procyon", Ab::EmpyreanHero},
    {"Count Otto P'may", Ab::DsOtto},
    {"Dannel of the Tenth", Ab::YinHero},
    {"Darktalon Treilla", Ab::LetnevHero},
    {"Demi-Queen Mdcksssk", Ab::DsDemiQueen},
    {"Dorrahn Griphyn", Ab::DsGriphyn},
    {"Gorthrim", Ab::DsGorthrim},
    {"Gurno Aggero", Ab::SaarHero},
    {"Harka Leeds", Ab::KeleresHarka},
    {"Hesh and Prit", Ab::NaazHero},
    {"It Feeds on Carrion", Ab::CabalHero},
    {"Jace X", Ab::SolHero},
    {"Kantrus", Ab::DsKantrus},
    {"Khaz-Rin Li-Zho", Ab::DsKhazRin},
    {"Korela", Ab::DsKorela},
    {"Krill Drakkon", Ab::DsKwill},
    {"Kyver, Blade and Key", Ab::YssarilHero},
    {"Letani Miasmiala", Ab::ArborecHero},
    {"Mathis Mathinus", Ab::WinnuHero},
    {"Midir", Ab::DsMidir},
    {"Mirik Aun Sissiri", Ab::ArgentHero},
    {"Nmenmede", Ab::DsNmenmede},
    {"Pahn Silverfur", Ab::DsPahn},
    {"Putriv Sirvonsk", Ab::DsPutriv},
    {"Riftwalker Meian", Ab::CreussHero},
    {"Rin", Ab::JolnarHero},
    {"Saint Binal", Ab::DsSaintBinal},
    {"Sharsiss", Ab::FirmamentHero},
    {"Sharsiss Hollow", Ab::ObsidianHero},
    {"Starsails", Ab::DsStarsails},
    {"Ta Zern", Ab::DeepwroughtHero},
    {"Thakt Clqua", Ab::DsThakt},
    {"The Helmsman", Ab::L1Z1XHero},
    {"The Venerable", Ab::DsVenerable},
    {"The Voice United", Ab::DsVoiceUnited},
    {"UNIT.DSGN.FLAYESH", Ab::NekroHero},
    {"Ul the Progenitor", Ab::TitansHero},
    {"Vehl Tikar", Ab::DsVehlTikar},
    {"Wonell the Silent", Ab::DsWonell},
    {"Wrath Machina", Ab::DsWrathMachina},
    {"Xxekir Grom", Ab::XxchaHero},
    {"Ygegnad, The Thunder", Ab::DsYgegnad}
        };
        for (int16_t l : G.dsSoulCage)
            for (const auto& h : kHeroActions)
                if (!std::strncmp(kLeaders[l].name, h.name, std::strlen(h.name))) {
                    add(h.ab, std::string("Soul Cage: ") + kLeaders[l].name,
                        "Resolve that hero's ability as your own. Your hero and everything bound to it are purged.");
                    break;
                }
    } else if (fac(p, "mykomentori") && heroUnlocked(G, p))
        for (int16_t l : G.dsSoulCage) {
            const char* k = kLeaders[l].key;
            const Ab ab = !std::strcmp(k, "kollecchero")       ? Ab::DsGriphyn
                          : !std::strcmp(k, "mortheushero")    ? Ab::DsBayan
                          : !std::strcmp(k, "cymiaehero")      ? Ab::DsVoiceUnited
                          : !std::strcmp(k, "gheminaherolady") ? Ab::DsKorela
                          : !std::strcmp(k, "gheminaherolord") ? Ab::DsKantrus
                                                               : Ab::DsOtto;
            add(ab, std::string("Soul Cage: ") + kLeaders[l].name, "Resolve that hero's ability as your own. Your hero and everything bound to it are purged.");
        }
    // Ghemina
    if (fac(p, "ghemina")) {
        const int lady = heroIdx(G, p, "gheminaherolady"), lord = heroIdx(G, p, "gheminaherolord");
        // (0.52.0, fixes 16: only when the hero has a planet to act on - picking one with none did nothing and was offered
        // again, so a player could pick it forever)
        if (lady >= 0 && pl.leaders[size_t(lady)].status == kReady && !raidPlanets(G, p).empty() && (G.fixes < 16 || !korelaTargets(G, p).empty()))
            add(Ab::DsKorela, "Hero: Korela (the Lady)", "Destroy every unit on a planet next to one of your flagships. Purged afterwards.");
        if (lord >= 0 && pl.leaders[size_t(lord)].status == kReady && !raidPlanets(G, p).empty() && (G.fixes < 16 || !kantrusTargets(G, p).empty()))
            add(Ab::DsKantrus, "Hero: Kantrus (the Lord)", "Take a planet next to one of your flagships that has no one else's units. Purged afterwards.");
    }
    ds6Options(p, o);  // batch 6
    ds7Options(p, o);
}

bool Rules::ds5Action(int p, Ab ab) {
    Player& pl = G.players[size_t(p)];
    // the hero being used: the Ghemina pick theirs by name; anyone else (Soul Cage) has one
    auto heroReady = [&](const char* key) {
        const int i = fac(p, "ghemina") ? heroIdx(G, p, key) : leaderIndex(G, p, kHero);
        return i >= 0 && pl.leaders[size_t(i)].status == kReady && !pl.eliminated ? i : -1;
    };
    switch (ab) {
        case Ab::DsCordo: {
            const int user = fac(p, "freesystems") ? (canUseAgent(p, "freesystems") ? p : -1) : agentUser(p, "freesystems");
            if (user < 0) return false;
            std::vector<Option> opts = cordoOptions(G, p);
            if (opts.empty()) return false;
            const int c = fpick(p, Ab::DsCordo, "Cordo Haved: which legendary ability?", opts);
            spendAgent(user, "freesystems");
            if (user != p) agentDone(user, p);  // a lent agent: the promise is kept
            const int x = c / 10, e = c % 10;
            const char* k = kPlanets[G.planets[size_t(x)].def].key;
            std::vector<Option> mine;
            for (int y : controlledPlanets(G, p))
                if (!isDMZ(G, y)) mine.push_back({y, planetName(y), ""});
            std::vector<Option> ships;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (hasShips(G, p, int(s))) ships.push_back({int(s), sysName(int(s)), ""});
            if (!std::strcmp(k, "hopesend")) {
                if (e == 2) drawActionCards(p, 1, "Cordo Haved");
                else if (!mine.empty()) {
                    const int y = fpick(p, Ab::DsCordo, "Cordo Haved: the mech goes on which planet?", mine, "", false, true);
                    addUnits(p, UnitType::Mech, 1, G.planets[size_t(y)].system, y);
                }
            } else if (!std::strcmp(k, "primor")) {
                for (int i = 0; i < 2 && !mine.empty(); ++i) {
                    const int y = fpick(p, Ab::DsCordo, "Cordo Haved: an infantry on which planet?", mine, "", false, true);
                    addUnits(p, UnitType::Infantry, 1, G.planets[size_t(y)].system, y);
                }
            } else if (!std::strcmp(k, "mirage") && !ships.empty()) {
                const int s = fpick(p, Ab::DsCordo, "Cordo Haved: 2 fighters where?", ships, "", true);
                addUnits(p, UnitType::Fighter, 2, s, -1);
                removeExcessCapacity(p, s);
            } else if (!std::strcmp(k, "mallice")) {
                if (e == 2) {
                    const int n = pl.commodities;
                    pl.commodities = 0;
                    gainTG(p, n);
                } else {
                    gainTG(p, 2);
                }
            } else if (!std::strcmp(k, "silence") && !ships.empty()) {
                const int s = fpick(p, Ab::DsCordo, "Cordo Haved: the cruiser goes where?", ships, "", true);
                addUnits(p, UnitType::Cruiser, 1, s, -1);
                enforceFleetLimit(p);
            }
            ds5Event(E, p, "Cordo Haved", pn(p) + "'s envoy borrows the power of " + legendaryName(G.planets[size_t(x)].def) + ".");
            return true;
        }
        case Ab::DsOtto: {
            const int h = heroReady("freesystemshero");
            if (h < 0) return false;
            std::vector<Option> opts;
            for (int x : controlledPlanets(G, p))
                if (!isHome5(G, x) && !isMecatol5(G, x) && x != G.dsHeart)
                    opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + "/" + std::to_string(planetInfluence(G, x))});
            if (opts.empty()) return false;
            const int x = fpick(p, Ab::DsOtto, "Count Otto P'may: attach the Heart of Rebellion to which planet?", opts,
                                "It is readied, every unit on it is removed, and no units can go there again.", false, true);
            ds5Hero(p, h, pn(p) + " raises the Heart of Rebellion on " + planetName(x) + ": freedom or death.");
            PlanetState& ps = G.planets[size_t(x)];
            for (int q = 0; q < kSides; ++q) ps.units[size_t(q)] = Force();
            ps.exhausted = false;
            G.dsHeart = int16_t(x);
            return true;
        }
        case Ab::DsMidir: {
            if (!heroUnlocked(G, p)) return false;
            const std::vector<int> sig = sigils(G);
            if (sig.empty() || G.agendaDeck.empty()) return false;
            ds5Hero(p, leaderIndex(G, p, kHero), pn(p) + " speaks with the Living Will: " + std::to_string(sig.size()) + " agendas, as it decrees.");
            // 0.43.0 (D109): nobody else resolves abilities during it (their action cards stay in their hands)
            struct SilenceGuard {
                decltype(silenced_)& f;
                decltype(silenced_) was;
                ~SilenceGuard() { f = was; }
            } silence{silenced_, silenced_};
            if (G.fixes >= 2)
                for (int q = 0; q < G.numPlayers; ++q)
                    if (q != p) silenced_ = decltype(silenced_)(silenced_ | (1 << q));
            for (size_t i = 0; i < sig.size() && !G.agendaDeck.empty(); ++i) {
                const int a = G.agendaDeck.back();
                G.agendaDeck.pop_back();
                E.revealed();
                std::vector<Option> outs = agendaOutcomes(a);
                if (outs.empty()) {
                    G.agendaDiscard.push_back(int16_t(a));
                    continue;
                }
                const int prev = curAgenda_;
                curAgenda_ = a;
                const int o = fpick(p, Ab::DsMidir, std::string("Midir: the outcome of ") + kAgendas[a].name, outs, agendaText(a, false));
                std::vector<int> voted(size_t(G.numPlayers), kNoVote), counts(size_t(G.numPlayers), 0);
                voted[size_t(p)] = o;
                counts[size_t(p)] = 1;
                resolveOutcome(a, o, voted, counts);
                curAgenda_ = prev;
                bool law = false;
                for (const Law& l : G.laws) law = law || l.agenda == a;
                if (!law) G.agendaDiscard.push_back(int16_t(a));
            }
            return true;
        }
        case Ab::DsLocalContracts: {
            if (!techReady(p, "dsnokay")) return false;
            std::vector<Option> opts;
            // (0.44.5, fixes 9: also a system with our token already - nothing more is placed, LRR 20.6 - and the
            // Production of 1 unit, not the whole system's)
            const bool v9 = G.fixes >= 9;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if ((v9 || !G.systems[s].hasToken(p)) && productionCapacity(G, p, int(s)) > 0)
                    opts.push_back({int(s), sysName(int(s)), v9 ? "Production " + std::to_string(singleUnitProduction(G, p, int(s))) : ""});
            if (opts.empty()) return false;
            const int s = fpick(p, Ab::DsLocalContracts, "Local Contracts: produce in which system?", opts, "A command token from your reinforcements goes there afterwards.", true);
            exhaustTech(p, "dsnokay");
            ds5Event(E, p, "Local Contracts", pn(p) + " calls in a local contract in " + sysName(s) + ".", s);
            productionCore(p, s, v9 ? singleUnitProduction(G, p, s) : productionCapacity(G, p, s), false, false);
            placeTokenFromReinforcements(p, s, "Local Contracts: a command token from your reinforcements.");
            return true;
        }
        case Ab::DsStarsails: {
            if (!heroUnlocked(G, p)) return false;
            ds5Hero(p, leaderIndex(G, p, kHero), pn(p) + " makes the Irrefutable Offer: every planet of theirs builds ships this round.");
            pl.heroRound = int16_t(G.round);
            return true;
        }
        case Ab::DsEmergency: {
            if (!techReady(p, "dsaxisy")) return false;
            std::vector<Option> to;
            for (int x : controlledPlanets(G, p))
                if (!G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock) && !isDMZ(G, x)) to.push_back({x, planetName(x), ""});
            if (to.empty()) return false;
            std::vector<Option> from;
            if (unitsAvailable(G, p, UnitType::SpaceDock) > 0) from.push_back({-1, "A new one from your reinforcements", ""});
            for (int x : controlledPlanets(G, p))
                if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock)) from.push_back({x, "Move the one on " + planetName(x), ""});
            if (from.empty()) return false;
            const int f = from.size() == 1 ? from[0].id : fpick(p, Ab::DsEmergency, "Emergency Deployment: which space dock?", from);
            const int x = fpick(p, Ab::DsEmergency, "Emergency Deployment: onto which planet?", to, "", false, true);
            exhaustTech(p, "dsaxisy");
            if (f >= 0) G.planets[size_t(f)].units[size_t(p)].remove(UnitType::SpaceDock, 1, false);
            addUnits(p, UnitType::SpaceDock, 1, G.planets[size_t(x)].system, x);
            if (f >= 0 && G.fixes >= 9) removeExcessCapacity(p, G.planets[size_t(f)].system);  // (0.44.5: the fighters it held)
            ds5Event(E, p, "Emergency Deployment", pn(p) + " deploys a space dock on " + planetName(x) + ".", G.planets[size_t(x)].system);
            return true;
        }
        case Ab::DsShipmonger: {
            if (!canUseAgent(p, "axis")) return false;
            std::vector<Option> opts;
            for (int q = 0; q < G.numPlayers; ++q) {
                if (G.players[size_t(q)].eliminated) continue;
                bool ships = false;
                for (size_t s = 0; s < G.systems.size() && !ships; ++s) ships = hasShips(G, q, int(s));
                if (ships && (unitsAvailable(G, q, UnitType::Cruiser) > 0 || unitsAvailable(G, q, UnitType::Destroyer) > 0))
                    opts.push_back({q, pn(q), q == p ? "" : "You gain 2 commodities."});
            }
            if (opts.empty()) return false;
            const int q = fpick(p, Ab::DsShipmonger, "Shipmonger Zsknck: which player places a ship?", opts);
            spendAgent(p, "axis");
            std::vector<Option> what;
            if (unitsAvailable(G, q, UnitType::Cruiser) > 0) what.push_back({int(UnitType::Cruiser), "1 cruiser", ""});
            if (unitsAvailable(G, q, UnitType::Destroyer) > 0) what.push_back({int(UnitType::Destroyer), "1 destroyer", ""});
            std::vector<Option> where;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (hasShips(G, q, int(s))) where.push_back({int(s), sysName(int(s)), ""});
            const int t = what.size() == 1 ? what[0].id : fpick(q, Ab::DsShipmonger, "Shipmonger Zsknck: which ship?", what);
            const int s = fpick(q, Ab::DsShipmonger, "Shipmonger Zsknck: place it where?", where, "", true);
            addUnits(q, UnitType(t), 1, s, -1);
            enforceFleetLimit(q);
            ds5Event(E, p, "Shipmonger Zsknck", pn(q) + " receives a " + unitTypeName(UnitType(t)) + " from the shipmonger.", s);
            if (q != p) {
                { const int cb_ = pl.commodities; pl.commodities = int8_t(std::min(commodityCap(G, pl), pl.commodities + 2)); commodityGained(p, cb_); }
                dsReplenished(p, false);  // (gained, not replenished)
            }
            return true;
        }
        case Ab::DsDemiQueen: {
            if (!heroUnlocked(G, p)) return false;
            std::vector<int> mine;
            for (int i = 0; i < 8; ++i)
                if (G.dsOrders[i] == p) mine.push_back(i);
            if (mine.empty()) return false;
            ds5Hero(p, leaderIndex(G, p, kHero), pn(p) + " launches Aggressive Marketing: Axis Orders for promissory notes.");
            for (int i : mine) {
                std::vector<Option> to;
                for (int q = 0; q < G.numPlayers; ++q)
                    if (q != p && !G.players[size_t(q)].eliminated) to.push_back({q, pn(q), G.players[size_t(q)].notes.empty() ? "No notes in hand." : ""});
                to.push_back({-1, "Keep it", ""});
                const int q = fpick(p, Ab::DsDemiQueen, "Demi-Queen Mdcksssk: give the " + orderName(i) + " to whom?", to);
                if (q < 0) continue;
                G.dsOrders[i] = int8_t(q);
                say(p, pn(q) + " receives an " + orderName(i) + ".");
                Player& ql = G.players[size_t(q)];
                std::vector<Option> notes;
                for (int16_t n : ql.notes)
                    if (noteOwner(n) != p) notes.push_back({n, noteNameOf(G, n), ""});
                if (notes.empty()) continue;
                const int n = notes.size() == 1 ? notes[0].id : fpick(q, Ab::DsDemiQueen, "Aggressive Marketing: which promissory note do you give " + pn(p) + "?", notes);
                giveNote(q, p, n);
            }
            return true;
        }
        case Ab::DsInsight: {
            const int my = playerOfFaction(G, "mykomentori");
            if (my < 0 || my == p || !holdsNote(G, p, my, kNoteFaction)) return false;
            pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), makeNote(my, kNoteFaction)), pl.notes.end());
            pl.notesInPlay.push_back(makeNote(my, kNoteFaction));
            ds5Event(E, p, "Gift of Insight", pn(p) + " accepts the Myko-Mentori's gift of insight.");
            return true;
        }
        case Ab::DsKorela: {
            const int h = heroReady("gheminaherolady");
            if (h < 0) return false;
            const std::vector<Option> opts = korelaTargets(G, p);
            if (opts.empty()) return false;
            const int x = fpick(p, Ab::DsKorela, "Korela: destroy every unit on which planet?", opts, "", false, true);
            ds5Hero(p, h, pn(p) + " gives the Raze Order: fear and ash on " + planetName(x) + ".");
            const int sys = G.planets[size_t(x)].system;
            for (int q = 0; q < kSides; ++q)
                for (int t = 0; t < kUnitTypes; ++t) {
                    const int k = G.planets[size_t(x)].units[size_t(q)].n[t];
                    if (k > 0) destroyUnits(q, UnitType(t), k, sys, x, p, "Korela");
                }
            return true;
        }
        case Ab::DsKantrus: {
            const int h = heroReady("gheminaherolord");
            if (h < 0) return false;
            const std::vector<Option> opts = kantrusTargets(G, p);
            if (opts.empty()) return false;
            const int x = fpick(p, Ab::DsKantrus, "Kantrus: take which planet?", opts, "", false, true);
            ds5Hero(p, h, pn(p) + " claims its Alleged Birthright: " + planetName(x) + ".");
            setControl(x, p);
            G.planets[size_t(x)].exhausted = false;
            return true;
        }
        default: return ds6Action(p, ab);  // batch 6 (rules_ds6.cpp)
    }
}

}  // namespace ti
