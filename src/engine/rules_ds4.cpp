// Discordant Stars batch 4 (0.40.0): six more factions, called from the ds / ds2 / ds3 hooks and a few new ones
// (action card draws and discards, secondaries, sustain damage, planet exhaustion, retreats, votes). Texts in
// faction_text.cpp, the AI in ai/ai_ds.cpp.
//   Kollecc Society       - Cloaked Fleets / Shroud of Lith (their own units captured on the sheet: Player::captured,
//                           owner = themselves), Treasure Hunters, Nightingale V, Seeker Drones, Shrouded Skirmishers
//                           (ds4StartForce_), AI Survey, Captain Dust, Kado S'mah-Qar (missingPrereqs, doRetreat),
//                           Dorrahn Griphyn, Nightshade Vanguard
//   Glimmer of Mortheus   - Facsimile / Secrets of the Weave, Illusory Presence, Particle Sieve, Fractal Plating,
//                           Fabrication Grid (query productionCapacity), Walik, Komat (Game::dsKomatSys, query
//                           extraSustains), Bayan, Duuban
//   Vaden Banking Clans   - Fine Print (Game::dsDebt), Collateralized Loans, Binding Debts, Aurum Vadra, Midas Turbine /
//                           Vaden Handshake (G.fx.dsBoost, query boostShips), Krovoz Strike Teams, Yudri Sukhov, Komdar
//                           Borodin, Putriv Sirvonsk, Collector
//   Savages of Cymiae     - Autonetic Memory, Cybernetic Madness (purged action cards), Reprocessor Alpha, Unholy
//                           Abomination II (infantryDestroyed), Recursive Worm, Algorithmic Replication, Skhot Unit X-12,
//                           Koryl Ferax, The Voice United, Revenant
//   Nivyn Star Kings      - Celestial Guides / Singularity Point (query anomaliesFor, riftRolls; Game::dsWound),
//                           Voidsailors, Eradica (destroyUnits), Voidwake Missiles, Voidflare Warden I / II, Nivyn
//                           Guidance, Suldhan Wraeg, Thussad Krath, Krill Drakkon (routeNeighbors)
//   Olradin League        - Policies (Player::dsPolicy; Connect / Control, Preserve / Plunder, Empower / Exploit),
//                           Rallypoint (query moveValue), False Flag Operations, Geosympathic Impeller (G.fx.dsMove),
//                           Incite Revolution, Baggil Wildpaw, Knak Half-Ear, Pahn Silverfur, Exemplar (attachBonus)
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
constexpr int kShipMask4 = kShipMaskAll;  // Carrier .. WarSun, the second flagship

void ds4Event(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
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
void ds4Hero(Engine& E, const Game& G, int p, const std::string& text) {
    const int i = leaderIndex(G, p, kHero);
    Event e;
    e.kind = EventKind::Leader;
    e.player = int8_t(p);
    e.card = int16_t(i >= 0 ? G.players[size_t(p)].leaders[size_t(i)].leader : 0);
    e.label = "hero";
    e.text = text;
    E.emit(e);
}
int deckOf(Trait t) {
    switch (t) {
        case Trait::Cultural: return int(ExploreType::Cultural);
        case Trait::Hazardous: return int(ExploreType::Hazardous);
        case Trait::Industrial: return int(ExploreType::Industrial);
        default: return -1;
    }
}
bool isHome4(const Game& G, int p, int x) { return G.planets[size_t(x)].system == G.players[size_t(p)].homeSystem; }
bool isMecatol4(const Game& G, int x) { return !std::strcmp(kPlanets[G.planets[size_t(x)].def].key, "mr"); }
bool anomalyIn(const Game& G, int s) { return G.systems[size_t(s)].d().anomalies != 0 || s == G.dsWound; }
bool anomalyNear(const Game& G, int s) {
    if (anomalyIn(G, s)) return true;
    for (int16_t a : G.systems[size_t(s)].adj)
        if (anomalyIn(G, a)) return true;
    return false;
}
int costOf(const Game& G, int p, UnitType t) { return (unitCost2(G, p, t) + 1) / 2; }
bool policyMinus(const Game& G, int p, int k) { return (G.players[size_t(p)].dsPolicy >> k) & 1; }
}  // namespace

// The Olradin policy cards' sides (also shown in their play area, 0.43.4).
const char* olradinPolicyText(int k, bool minus) {
    static const char* kPlus[3] = {"Connect (+): after you exhaust a cultural or legendary planet, move 1 of its infantry to another of your planets",
                                   "Preserve (+): after you exhaust a hazardous or legendary planet, reveal the top exploration card of its kind - a relic fragment is yours",
                                   "Empower (+): after you exhaust an industrial or legendary planet, gain 1 commodity"};
    static const char* kMinus[3] = {"Control (-): no transactions in the agenda phase; 2 more votes for each cultural planet you exhaust to vote",
                                    "Plunder (-): your units produce 2 less; after you explore a hazardous planet, remove 1 unit there to explore it again",
                                    "Exploit (-): commodity value 2; after you exhaust an industrial planet, 1 fighter in a system with your ships"};
    return k < 0 || k > 2 ? "" : minus ? kMinus[k] : kPlus[k];
}

// ================================================================== setup
// Policies (Olradin): each of the three cards face up on the side they choose.
void Rules::ds4Setup(int p) {
    if (!fac(p, "olradin")) return;
    Player& pl = G.players[size_t(p)];
    const char* kName[3] = {"The People", "The Environment", "The Economy"};
    const char* kPlus[3] = {olradinPolicyText(0, false), olradinPolicyText(1, false), olradinPolicyText(2, false)};
    const char* kMinus[3] = {olradinPolicyText(0, true), olradinPolicyText(1, true), olradinPolicyText(2, true)};
    pl.dsPolicy = 0;
    for (int k = 0; k < 3; ++k) {
        const int c = fpick(p, Ab::DsPolicy, std::string("Policies: which side of ") + kName[k] + "?",
                            {{0, std::string(kPlus[k]).substr(0, std::string(kPlus[k]).find(':')), kPlus[k]},
                             {1, std::string(kMinus[k]).substr(0, std::string(kMinus[k]).find(':')), kMinus[k]}});
        if (c == 1) pl.dsPolicy = uint8_t(pl.dsPolicy | (1 << k));
    }
    ds4Event(E, p, "Policies", pn(p) + " sets its policies: " + (policyMinus(G, p, 0) ? "Control" : "Connect") + ", " +
                                   (policyMinus(G, p, 1) ? "Plunder" : "Preserve") + ", " + (policyMinus(G, p, 2) ? "Exploit" : "Empower") + ".");
}

// ================================================================== combat
void Rules::ds4CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m) {
    (void)opp;
    // Thussad Krath (Nivyn commander): up to 2 of their damaged units roll 1 more die
    if (!commanderUsable(G, p, "nivyn")) return;
    const Force& f = space ? G.systems[size_t(sys)].space[size_t(p)] : G.planets[size_t(planet)].units[size_t(p)];
    int left = 2;
    for (UnitType t : {UnitType::WarSun, UnitType::Flagship, UnitType::Flagship2, UnitType::Dreadnought, UnitType::Mech, UnitType::Cruiser,
                       UnitType::Carrier}) {
        const int k = std::min<int>(left, f.dmg[int(t)]);
        if (k <= 0) continue;
        m.extraDice[int(t)] += k;
        left -= k;
    }
}

void Rules::ds4SpaceCombatStart(int att, int def, int sys) {
    SystemState& s = G.systems[size_t(sys)];
    G.dsKomatSys = -1;
    G.dsKomatOwner = -1;
    // Facsimile (Mortheus) / Secrets of the Weave (their note): the defender copies one of the attacker's ships
    if (def < G.numPlayers && att < G.numPlayers) {
        const int mo = playerOfFaction(G, "mortheus");
        const bool own = fac(def, "mortheus");
        const bool note = mo >= 0 && mo != def && holdsNote(G, def, mo, kNoteFaction);
        if (own || note) {
            std::vector<Option> opts;
            for (int t : kShipIdx) {
                if (!s.space[size_t(att)].n[t] || unitsAvailable(G, def, UnitType(t)) <= 0) continue;
                const int c = costOf(G, att, UnitType(t));
                if (c > spendable(G, def, true)) continue;
                opts.push_back({t, std::string(unitTypeName(UnitType(t))) + " for " + std::to_string(c) + " influence", ""});
            }
            if (!opts.empty()) {
                opts.push_back({-1, "Don't", ""});
                const int t = fpick(def, Ab::DsFacsimile, own ? "Facsimile: copy one of the attacker's ships?" : "Play " + pn(mo) + "'s Secrets of the Weave: copy one of the attacker's ships?",
                                    opts, "Pay its cost in influence; a ship of that type from your reinforcements joins the defence.");
                if (t >= 0 && pay(def, costOf(G, att, UnitType(t)), true, own ? "Facsimile" : "Secrets of the Weave", true)) {
                    if (!own) returnNote(def, makeNote(mo, kNoteFaction));
                    addUnits(def, UnitType(t), 1, sys, -1);
                    ds4Event(E, def, own ? "Facsimile" : "Secrets of the Weave", pn(def) + " weaves a copy of a " + unitTypeName(UnitType(t)) + " into the defence.", sys);
                }
            }
        }
    }
    for (int side : {att, def}) {
        if (side >= G.numPlayers) continue;
        const int other = side == att ? def : att;
        // Komat (Mortheus commander): not the active player - one of their non-fighter ships sustains damage this combat
        if (side != G.activePlayer && commanderUsable(G, side, "mortheus") && s.space[size_t(side)].nonFighterShips() > 0) {
            G.dsKomatSys = int16_t(sys);
            G.dsKomatOwner = int8_t(side);
            ds4Event(E, side, "Komat", "One of " + pn(side) + "'s ships gains Sustain Damage for this combat.", sys);
        }
        // Shrouded Skirmishers (Kollecc): a ship that wasn't here at the start of the action rolls for 1 hit
        // (0.44.7, fixes 11: the card doesn't exhaust - every space combat)
        if ((G.fixes >= 11 ? owns(side, "dskollb") : techReady(side, "dskollb")) && side == G.activePlayer && s.space[size_t(other)].ships() > 0) {
            int best = -1;
            for (int t : kShipIdx)
                if (s.space[size_t(side)].n[t] > ds4StartForce_.n[t] && (best < 0 || unitDef(G, side, UnitType(t)).combat < unitDef(G, side, UnitType(best)).combat))
                    best = t;
            if (best >= 0 && fyes(side, Ab::DsSkirmishers, std::string("Shrouded Skirmishers: your ") + unitTypeName(UnitType(best)) + " rolls for 1 hit?",
                                  "It hits on " + std::to_string(unitDef(G, side, UnitType(best)).combat) + " or more.")) {
                if (G.fixes < 11) exhaustTech(side, "dskollb");
                RollResult r;
                addDice(r, 1, unitDef(G, side, UnitType(best)).combat, best);
                recount(r);
                reportRoll(side, sys, "Shrouded Skirmishers", r);
                if (r.hits > 0) assignHits(other, sys, -1, 1, kShipMask4, "Shrouded Skirmishers", side);
            }
        }
    }
}

// After the dice of a combat round: Voidwake Missiles (Nivyn), Krovoz Strike Teams' extra hit (Vaden).
void Rules::ds4AfterCombatRolls(int att, int def, int sys, int planet, RollResult& rA, RollResult& rD) {
    for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? att : def;
        if (side >= G.numPlayers) continue;
        RollResult& r = i == 0 ? rA : rD;
        Player& pl = G.players[size_t(side)];
        // Voidwake Missiles: one unit with Sustain Damage becomes damaged to reroll its dice
        if (owns(side, "dsnivyy")) {
            Force& f = planet < 0 ? G.systems[size_t(sys)].space[size_t(side)] : G.planets[size_t(planet)].units[size_t(side)];
            int best = -1, bestMiss = 0;
            for (int t = 0; t < kUnitTypes; ++t) {
                if (!f.n[t] || f.dmg[t] >= f.n[t] || !unitSustains(G, side, UnitType(t))) continue;
                int miss = 0;
                for (size_t d = 0; d < r.dice.size() && d < r.unit.size(); ++d)
                    if (r.unit[d] == t && r.dice[d] < r.need[d]) ++miss;
                miss = std::min(miss, std::max(1, int(unitDef(G, side, UnitType(t)).combatDice)));  // (one unit's dice)
                if (miss > bestMiss) {
                    bestMiss = miss;
                    best = t;
                }
            }
            if (best >= 0) {
                curValue_ = bestMiss;
                const bool use = fyes(side, Ab::DsVoidwake, std::string("Voidwake Missiles: damage a ") + unitTypeName(UnitType(best)) + " to reroll its " +
                                                                std::to_string(bestMiss) + " missed dice?", "");
                curValue_ = 0;
                if (use) {
                    f.dmg[best] = uint8_t(f.dmg[best] + 1);
                    int left = bestMiss;
                    for (size_t d = 0; d < r.dice.size() && d < r.unit.size() && left > 0; ++d)
                        if (r.unit[d] == best && r.dice[d] < r.need[d]) {
                            r.dice[d] = int8_t(E.rng().d10());
                            --left;
                        }
                    recount(r);
                    reportRoll(side, sys, "Rerolled (Voidwake Missiles)", r);
                }
            }
        }
        // Krovoz Strike Teams: 1 trade good for 1 more hit
        if (owns(side, "dsvadey") && r.hits > 0 && pl.tradeGoods > 0) {
            curValue_ = pl.tradeGoods;
            const bool use = fyes(side, Ab::DsKrovozHit, "Krovoz Strike Teams: spend 1 trade good for 1 more hit?", "");
            curValue_ = 0;
            if (use) {
                pl.tradeGoods = int8_t(pl.tradeGoods - 1);
                ++r.hits;
                ds4Event(E, side, "Krovoz Strike Teams", pn(side) + " pays a strike team: 1 more hit.", sys);
            }
        }
    }
}

// Start of a ground combat round: Revenant (Cymiae mech) repairs itself.
void Rules::ds4GroundRoundStart(int att, int def, int planet) {
    for (int side : {att, def}) {
        if (side >= G.numPlayers || !fac(side, "cymiae") || !mechAbilities(G)) continue;
        Force& f = G.planets[size_t(planet)].units[size_t(side)];
        if (f.count(UnitType::Mech) == 1 && f.dmg[int(UnitType::Mech)] > 0) {
            f.dmg[int(UnitType::Mech)] = 0;
            ds4Event(E, side, "Revenant", pn(side) + "'s Revenant knits itself back together.", G.planets[size_t(planet)].system);
        }
    }
}

// End of a ground combat round: Collector (Vaden mech) - a debt token for 1 infantry.
void Rules::ds4GroundRoundEnd(int att, int def, int planet) {
    for (int side : {att, def}) {
        if (side >= G.numPlayers || !fac(side, "vaden") || !mechAbilities(G)) continue;
        const int other = side == att ? def : att;
        if (other >= G.numPlayers || G.dsDebt[other] <= 0) continue;
        if (!G.planets[size_t(planet)].units[size_t(side)].count(UnitType::Mech) || unitsAvailable(G, side, UnitType::Infantry) <= 0) continue;
        if (!fyes(side, Ab::DsCollector, "Collector: cash in 1 of " + pn(other) + "'s tokens for 1 infantry here?", "")) continue;
        --G.dsDebt[other];
        addUnits(side, UnitType::Infantry, 1, G.planets[size_t(planet)].system, planet);
        ds4Event(E, side, "Collector", pn(side) + " collects on " + pn(other) + "'s debt: 1 infantry.", G.planets[size_t(planet)].system);
    }
}

void Rules::ds4WonCombat(int winner, int loser, int sys, bool space) {
    (void)space;
    // Reprocessor Alpha (Cymiae flagship): 1 of the loser's action cards at random
    if (flagshipAbility(G, winner, "cymiae") && flagshipIn(G, winner, sys) && loser >= 0 && loser < G.numPlayers && !G.players[size_t(loser)].actionCards.empty()) {
        auto& hand = G.players[size_t(loser)].actionCards;
        const int i = int(E.rng().below(int(hand.size())));
        const int16_t c = hand[size_t(i)];
        hand.erase(hand.begin() + i);
        G.players[size_t(winner)].actionCards.push_back(c);
        E.revealed();
        ds4Event(E, winner, "Reprocessor Alpha", pn(winner) + " takes 1 of " + pn(loser) + "'s action cards at random.", sys);
        enforceHandLimit(winner);
    }
}

// Collateralized Loans (Vaden): an opponent's ship destroyed - a debt token buys a ship of that type here.
void Rules::ds4ShipsLost(int victim, int opp, int sys, const Force& before) {
    if (opp < 0 || opp >= G.numPlayers || victim < 0 || victim >= G.numPlayers || !fac(opp, "vaden")) return;
    const Force& now = G.systems[size_t(sys)].space[size_t(victim)];
    for (int t : kShipIdx)
        for (int k = before.n[t] - now.n[t]; k > 0 && G.dsDebt[victim] > 0 && unitsAvailable(G, opp, UnitType(t)) > 0; --k) {
            curValue_ = t;
            const bool use = fyes(opp, Ab::DsCollateral, "Collateralized Loans: cash in 1 of " + pn(victim) + "'s tokens for a " + unitTypeName(UnitType(t)) + " here?", "");
            curValue_ = 0;
            if (!use) break;
            --G.dsDebt[victim];
            addUnits(opp, UnitType(t), 1, sys, -1);
            ds4Event(E, opp, "Collateralized Loans", pn(opp) + " forecloses: a " + unitTypeName(UnitType(t)) + " joins its fleet.", sys);
        }
}

// Fractal Plating (Mortheus): a unit destroyed - one of that type in a neighbouring system with their ships.
void Rules::ds4UnitsDestroyed(int p, UnitType t, int k, int sys, int planet, int byPlayer) {
    (void)k;
    (void)planet;
    (void)byPlayer;
    if (p < 0 || p >= G.numPlayers || sys < 0 || !techReady(p, "dsmortr") || isStructure(t) || unitsAvailable(G, p, t) <= 0) return;
    std::vector<Option> opts;
    // (0.44.8, fixes 12: a ground force or fighter only where there is capacity for it - else it is removed at once)
    const bool cargo = t == UnitType::Infantry || t == UnitType::Mech || t == UnitType::Fighter;
    for (int16_t a : G.systems[size_t(sys)].adj)
        if (G.systems[size_t(a)].space[size_t(p)].ships() > 0 &&
            !(G.fixes >= 12 && cargo && shipCapacity(G, p, a) <= transportedInSpace(G, p, a)))
            opts.push_back({a, sysName(a), ""});
    if (opts.empty()) return;
    opts.push_back({-1, "Don't", ""});
    curValue_ = int(t);
    const int a = fpick(p, Ab::DsFractal, std::string("Fractal Plating: a new ") + unitTypeName(t) + " in a neighbouring system with your ships?", opts, "", true);
    curValue_ = 0;
    if (a < 0) return;
    exhaustTech(p, "dsmortr");
    addUnits(p, t, 1, a, -1);
    ds4Event(E, p, "Fractal Plating", pn(p) + "'s " + unitTypeName(t) + " reforms in " + sysName(a) + ".", a);
    removeExcessCapacity(p, a);
}

// ================================================================== tactical actions and turns
void Rules::ds4Activated(int p, int sys) {
    SystemState& s = G.systems[size_t(sys)];
    ds4StartForce_ = s.space[size_t(p)];
    // Particle Sieve (Mortheus flagship): a frontier token in an empty system
    if (flagshipAbility(G, p, "mortheus") && flagshipIn(G, p, sys) && s.planets.empty() && !s.frontier && !s.isHyperlane() &&
        fyes(p, Ab::DsSieve, "Particle Sieve: place a frontier token in " + sysName(sys) + "?", "")) {
        s.frontier = true;
        ds4Event(E, p, "Particle Sieve", pn(p) + "'s flagship sifts a frontier out of the void.", sys);
    }
    // Voidflare Warden I (its system activated) / II (any system activated): the Wound token moves into that system
    const int nv = playerOfFaction(G, "nivyn");
    if (nv >= 0 && mechAbilities(G) && G.dsWound != sys) {
        const bool two = !std::strcmp(unitDef(G, nv, UnitType::Mech).key, "nivyn_mech2");
        int mechPlanet = -1;
        for (size_t x = 0; x < G.planets.size() && mechPlanet < 0; ++x) {
            const Force& f = G.planets[x].units[size_t(nv)];
            if (f.count(UnitType::Mech) > f.dmg[int(UnitType::Mech)] && (two || G.planets[x].system == sys)) mechPlanet = int(x);
        }
        if (mechPlanet >= 0) {
            curValue_ = p == nv ? 1 : (hasUnits(G, nv, sys) ? 2 : 0);
            const bool use = fyes(nv, Ab::DsWarden, "Voidflare Warden: damage your mech to put the Wound token in " + sysName(sys) + "?",
                                  "That system becomes a nebula and a gravity rift.");
            curValue_ = 0;
            if (use) {
                Force& f = G.planets[size_t(mechPlanet)].units[size_t(nv)];
                f.dmg[int(UnitType::Mech)] = uint8_t(f.dmg[int(UnitType::Mech)] + 1);
                G.dsWound = int16_t(sys);
                ds4Event(E, nv, "Voidflare Warden", "The Wound opens in " + sysName(sys) + ": a nebula and a gravity rift.", sys);
            }
        }
    }
    // Nivyn Guidance (the Nivyn's note): the holder ignores anomalies this activation
    if (nv >= 0 && nv != p && holdsNote(G, p, nv, kNoteFaction) && anomalyNear(G, sys) &&
        fyes(p, Ab::DsGuidance, "Play " + pn(nv) + "'s Nivyn Guidance: ignore every anomaly during this activation?", "The note goes back at the end of it.")) {
        returnNote(p, makeNote(nv, kNoteFaction));
        G.fx.navSuite = true;
        ds4Event(E, p, "Nivyn Guidance", pn(p) + " follows the Nivyn's guidance through the anomalies.", sys);
    }
    // Midas Turbine (Vaden) / Vaden Handshake (their note): trade goods for 1 more move each
    const int vd = playerOfFaction(G, "vaden");
    const bool midas = techReady(p, "dsvadeb");
    const bool shake = vd >= 0 && vd != p && holdsNote(G, p, vd, kNoteFaction);
    if ((midas || shake) && G.players[size_t(p)].tradeGoods > 0) {
        std::vector<Option> opts{{0, "None", ""}};
        for (int k = 1; k <= std::min<int>(G.players[size_t(p)].tradeGoods, 6); ++k) opts.push_back({k, std::to_string(k) + " trade good" + (k > 1 ? "s" : ""), ""});
        const int k = fpick(p, Ab::DsMidas, midas ? "Midas Turbine: spend trade goods to move ships 1 further?" : "Play Vaden Handshake: spend trade goods to move ships 1 further?",
                            opts, "1 trade good = 1 non-fighter ship may move 1 further this activation.");
        if (k > 0) {
            if (midas) exhaustTech(p, "dsvadeb");
            else returnNote(p, makeNote(vd, kNoteFaction));
            G.players[size_t(p)].tradeGoods = int8_t(G.players[size_t(p)].tradeGoods - k);
            G.fx.dsBoost = int8_t(k);
            G.fx.dsBoostOwner = int8_t(p);
            ds4Event(E, p, midas ? "Midas Turbine" : "Vaden Handshake", pn(p) + " spends " + std::to_string(k) + " trade goods on engines.", sys);
        }
    }
    // Geosympathic Impeller (Olradin): exhaust a planet of a trait found here - +1 move to all their ships
    if ((G.fixes >= 11 ? owns(p, "dsolrab") : techReady(p, "dsolrab")) && !s.planets.empty()) {
        std::vector<Option> opts;
        for (int x : ti::readyPlanets(G, p)) {
            bool match = false;
            for (int16_t y : s.planets)
                for (Trait t : {Trait::Cultural, Trait::Hazardous, Trait::Industrial})
                    match = match || (hasTraitOf(G, x, t) && hasTraitOf(G, y, t));
            if (match) opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + "/" + std::to_string(planetInfluence(G, x))});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Don't", ""});
            const int x = fpick(p, Ab::DsGeosympathic, "Geosympathic Impeller: exhaust a planet for +1 move to all your ships this action?", opts, "", false, true);
            if (x >= 0) {
                if (G.fixes < 11) exhaustTech(p, "dsolrab");  // (0.44.7, fixes 11: only the planet is exhausted)
                G.planets[size_t(x)].exhausted = true;
                planetsExhausted(p, {x});
                G.fx.dsMove = 1;
                ds4Event(E, p, "Geosympathic Impeller", pn(p) + "'s ships ride the planet's pull: +1 move.", sys);
            }
        }
    }
}

// End of a tactical action: Nightshade Vanguard (Kollecc mech) - up to 2 ground forces onto the sheet.
void Rules::ds4EndTactical(int p, int sys) {
    (void)p;
    ds4StartForce_ = Force();
    const int ko = playerOfFaction(G, "kollecc");
    if (ko < 0 || !mechAbilities(G)) return;
    for (int16_t x : G.systems[size_t(sys)].planets) {
        Force& f = G.planets[size_t(x)].units[size_t(ko)];
        if (!f.count(UnitType::Mech) || f.groundForces() < 2) continue;
        std::vector<Option> opts{{0, "None", ""}};
        const int most = std::min(2, f.groundForces() - 1);
        for (int k = 1; k <= most; ++k) opts.push_back({k, std::to_string(k), ""});
        if (most <= 0) continue;
        int threat = 0;  // (for the AI: other players' non-fighter ships in or next to the system)
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == ko) continue;
            threat += G.systems[size_t(sys)].space[size_t(q)].nonFighterShips();
            for (int16_t a : G.systems[size_t(sys)].adj) threat += G.systems[size_t(a)].space[size_t(q)].nonFighterShips();
        }
        curValue_ = threat;
        const int k = fpick(ko, Ab::DsNightshade, "Nightshade Vanguard: cloak ground forces from " + planetName(x) + " (captured on your sheet)?", opts);
        curValue_ = 0;
        int left = k;
        for (UnitType t : {UnitType::Infantry, UnitType::Mech})
            while (left > 0 && f.count(t) > (t == UnitType::Mech ? 1 : 0)) {
                f.remove(t, 1, false);
                G.players[size_t(ko)].captured.push_back(int16_t(ko * 16 + int(t)));
                --left;
            }
        if (k - left > 0) ds4Event(E, ko, "Nightshade Vanguard", pn(ko) + " cloaks " + std::to_string(k - left) + " ground forces from " + planetName(x) + ".", sys);
    }
}

void Rules::ds4TurnStart(int p) {
    Player& pl = G.players[size_t(p)];
    pl.dsPolOnce = 0;
    // Treasure Hunters (Kollecc) / AI Survey (their note): a look at the decks - only what a person can use
    const int ko = playerOfFaction(G, "kollecc");
    const bool hunter = fac(p, "kollecc");
    const bool survey = ko >= 0 && ko != p && holdsNote(G, p, ko, kNoteFaction);
    if ((hunter || survey) && pl.human) {
        std::vector<Option> opts{{-1, "Don't look", ""}};
        for (Trait t : {Trait::Cultural, Trait::Hazardous, Trait::Industrial})
            if (!G.exploreDeck[size_t(deckOf(t))].empty()) opts.push_back({deckOf(t), exploreTypeName(ExploreType(deckOf(t))), ""});
        const int d = fpick(p, hunter ? Ab::DsTreasure : Ab::DsAiSurvey, hunter ? "Treasure Hunters: look at the top card of which exploration deck?" : "Play AI Survey: look at the top card of which exploration deck?", opts);
        if (d >= 0) {
            if (survey && !hunter) returnNote(p, makeNote(ko, kNoteFaction));
            std::string seen = std::string("Top of the ") + exploreTypeName(ExploreType(d)) + " deck: " + kExploreCards[G.exploreDeck[size_t(d)].back()].name + ".";
            if (hunter && !G.relicDeck.empty()) seen += std::string(" Top relic: ") + kRelics[G.relicDeck.back()].name + ".";
            feedNote(p, seen);
        }
    }
    // Captain Dust (Kollecc agent): reveal 2 exploration cards of a kind they have a planet of - the fragments are theirs
    if (const int user = agentUser(p, "kollecc"); user >= 0) {
        std::vector<Option> opts;
        for (Trait t : {Trait::Cultural, Trait::Hazardous, Trait::Industrial}) {
            bool mine = false;
            for (int x : controlledPlanets(G, p)) mine = mine || hasTraitOf(G, x, t);
            const int d = deckOf(t);
            if (mine && G.exploreDeck[size_t(d)].size() + G.exploreDiscard[size_t(d)].size() > 0) opts.push_back({d, exploreTypeName(ExploreType(d)), ""});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Don't", ""});
            agentFor(user, p);
            const int d = fpick(user, Ab::DsCaptainDust, user == p ? "Exhaust Captain Dust: dig 2 cards from which exploration deck for relic fragments?"
                                                                   : "Exhaust Captain Dust so " + pn(p) + " digs for relic fragments?", opts);
            curFor_ = -1;
            if (d >= 0) {
                spendAgent(user, "kollecc");
                agentDone(user, p);
                int got = 0;
                for (int i = 0; i < 2; ++i) {
                    const int c = drawExplore(d);
                    if (c < 0) break;
                    if (isRelicFragment(c)) {
                        pl.fragments.push_back(int16_t(c));
                        ++got;
                    } else {
                        G.exploreDiscard[size_t(d)].push_back(int16_t(c));
                    }
                }
                ds4Event(E, p, "Captain Dust", pn(p) + " digs through the " + exploreTypeName(ExploreType(d)) + " deck: " + std::to_string(got) + " relic fragment" +
                                                   (got == 1 ? "" : "s") + ".");
            }
        }
    }
    // Yudri Sukhov (Vaden agent): commodities equal to one of their planets' influence
    if (const int user = agentUser(p, "vaden"); user >= 0 && pl.commodities < commodityCap(G, pl)) {
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p))
            if (planetInfluence(G, x) > 0) opts.push_back({x, planetName(x), std::to_string(planetInfluence(G, x)) + " influence"});
        if (!opts.empty()) {
            opts.push_back({-1, "Don't", ""});
            agentFor(user, p);
            const int x = fpick(user, Ab::DsYudri, user == p ? "Exhaust Yudri Sukhov: commodities equal to a planet's influence?" : "Exhaust Yudri Sukhov for " + pn(p) + "?", opts, "", false, true);
            curFor_ = -1;
            if (x >= 0) {
                spendAgent(user, "vaden");
                agentDone(user, p);
                const int got = std::min(planetInfluence(G, x), commodityCap(G, pl) - pl.commodities);
                { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + got); commodityGained(p, cb_); }
                ds4Event(E, p, "Yudri Sukhov", pn(p) + " gains " + std::to_string(got) + " commodities.");
            }
        }
    }
    // Recursive Worm (Cymiae): another player's command token into a system with their units
    if (techReady(p, "dscymiy")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (int(s) == pl.homeSystem || int(s) == G.mecatol() || !hasUnits(G, p, int(s)) || G.systems[s].home >= 0) continue;
            for (int q = 0; q < G.numPlayers; ++q)
                // (0.44.9, fixes 13 - the owner's answer C11: a player with no token in reinforcements gives one from a
                // pool - LRR 20.3b)
                if (q != p && !G.players[size_t(q)].eliminated && !G.systems[s].hasToken(q) &&
                    (reinforcementTokens(q) > 0 ||
                     (G.fixes >= 13 && G.players[size_t(q)].tactic + G.players[size_t(q)].fleet + G.players[size_t(q)].strategy > 0)))
                    opts.push_back({int(s) * 16 + q, pn(q) + "'s token in " + sysName(int(s)), ""});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Don't", ""});
            const int c = fpick(p, Ab::DsRecursiveWorm, "Recursive Worm: put another player's command token in a system with your units?", opts,
                                "They can't activate that system while it is there.");
            if (c >= 0) {
                exhaustTech(p, "dscymiy");
                const int s = c / 16, q = c % 16;
                if (G.fixes >= 13) placeTokenFromReinforcements(q, s, "Recursive Worm (Cymiae) puts one of your command tokens in " + sysName(s) + ".");
                else G.systems[size_t(s)].tokens = uint16_t(G.systems[size_t(s)].tokens | (1u << q));
                ds4Event(E, p, "Recursive Worm", pn(p) + "'s worm plants " + pn(q) + "'s command token in " + sysName(s) + ".", s);
            }
        }
    }
    // False Flag Operations (Olradin): exhaust one of their planets to ready a non-home planet (not Mecatol Rex)
    if (techReady(p, "dsolrar")) {
        std::vector<Option> spent;
        for (size_t x = 0; x < G.planets.size(); ++x) {
            const PlanetState& ps = G.planets[x];
            if (ps.owner < 0 || !ps.exhausted || ps.destroyed || isMecatol4(G, int(x)) || G.systems[size_t(ps.system)].home >= 0) continue;
            spent.push_back({int(x), planetName(int(x)) + (ps.owner == p ? "" : " (" + pn(ps.owner) + ")"),
                             std::to_string(planetResources(G, int(x))) + "/" + std::to_string(planetInfluence(G, int(x)))});
        }
        std::vector<int> ready = ti::readyPlanets(G, p);
        if (!spent.empty() && !ready.empty()) {
            spent.push_back({-1, "Don't", ""});
            const int y = fpick(p, Ab::DsFalseFlag, "False Flag Operations: ready which planet?", spent, "You exhaust one of yours for it.", false, true);
            if (y >= 0) {
                std::vector<Option> pay;
                for (int x : ready) pay.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + "/" + std::to_string(planetInfluence(G, x))});
                const int x = pay.size() == 1 ? pay[0].id : fpick(p, Ab::DsFalseFlag, "False Flag Operations: exhaust which of your planets?", pay, "", false, true);
                exhaustTech(p, "dsolrar");
                G.planets[size_t(x)].exhausted = true;
                planetsExhausted(p, {x});
                G.planets[size_t(y)].exhausted = false;
                ds4Event(E, p, "False Flag Operations", pn(p) + " exhausts " + planetName(x) + " and readies " + planetName(y) + ".");
            }
        }
    }
}

// After an exploration: Plunder (Olradin) - a unit leaves a hazardous planet to explore it again.
void Rules::ds4Explored(int p, int planet) {
    if (planet < 0 || !fac(p, "olradin") || !policyMinus(G, p, 1) || !hasTraitOf(G, planet, Trait::Hazardous)) return;
    Player& pl = G.players[size_t(p)];
    if (pl.dsPolOnce & 2) return;
    Force& f = G.planets[size_t(planet)].units[size_t(p)];
    if (f.count(UnitType::Infantry) == 0) return;
    curValue_ = f.groundForces();
    const bool use = fyes(p, Ab::DsPlunder, "Plunder: remove 1 infantry from " + planetName(planet) + " to explore it again?", "");
    curValue_ = 0;
    if (!use) return;
    pl.dsPolOnce = uint8_t(pl.dsPolOnce | 2);
    const bool was = removing_;
    removing_ = true;
    destroyUnits(p, UnitType::Infantry, 1, G.planets[size_t(planet)].system, planet, -1, "Plunder");
    removing_ = was;
    ds4Event(E, p, "Plunder", pn(p) + " strips " + planetName(planet) + " for more.", G.planets[size_t(planet)].system);
    explorePlanet(p, planet);
    if (G.fixes >= 8 && breakthroughsOn(G)) dsbtPolicy(p);  // Insurrectionist Networking (0.44.4: after every policy ability)
}

// Voidsailors (Nivyn): exploring a frontier token, 1 more card - keep one, the other goes back.
int Rules::dsFrontierRedraw(int p, int c) {
    if (c < 0 || !fac(p, "nivyn")) return c;
    const int t = int(ExploreType::Frontier);
    const int c2 = drawExplore(t);
    if (c2 < 0) return c;
    const int keep = fpick(p, Ab::DsVoidsailors, "Voidsailors: which frontier card do you resolve?", {{c, kExploreCards[c].name, ""}, {c2, kExploreCards[c2].name, ""}});
    G.exploreDeck[size_t(t)].push_back(int16_t(keep == c ? c2 : c));
    E.rng().shuffle(G.exploreDeck[size_t(t)]);
    ds4Event(E, p, "Voidsailors", pn(p) + " reads two frontiers and follows one.");
    return keep;
}

// ================================================================== phases
void Rules::ds4StatusStart() {
    // Incite Revolution (Olradin's note) goes back
    const int ol = playerOfFaction(G, "olradin");
    if (ol >= 0)
        for (int q = 0; q < G.numPlayers; ++q) {
            const int16_t note = makeNote(ol, kNoteFaction);
            auto& v = G.players[size_t(q)].notesInPlay;
            if (q != ol && std::find(v.begin(), v.end(), note) != v.end()) {
                quietNoteReturn_ = true;
                returnNote(q, note);
                quietNoteReturn_ = false;
            }
        }
    const int vd = playerOfFaction(G, "vaden");
    if (vd < 0) return;
    // Binding Debts: each neighbour may pay 1 trade good to take back up to 2 of their tokens
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == vd || G.dsDebt[q] <= 0 || !areNeighbours(G, q, vd) || G.players[size_t(q)].tradeGoods <= 0) continue;
        curValue_ = G.dsDebt[q];
        const bool pay = fyes(q, Ab::DsBindingDebts, "Binding Debts: give " + pn(vd) + " 1 trade good to take back up to 2 of your tokens from their sheet?",
                              std::to_string(G.dsDebt[q]) + " of your tokens are there.");
        curValue_ = 0;
        if (!pay) continue;
        G.players[size_t(q)].tradeGoods = int8_t(G.players[size_t(q)].tradeGoods - 1);
        gainTG(vd, 1);
        G.dsDebt[q] = int8_t(std::max(0, G.dsDebt[q] - 2));
        ds4Event(E, q, "Binding Debts", pn(q) + " pays off part of its debt to " + pn(vd) + ".");
    }
    // Komdar Borodin (Vaden commander): 1 trade good per secret objective scored, 1 commodity per public one
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!commanderUsable(G, p, "vaden")) continue;
        Player& pl = G.players[size_t(p)];
        int sec = 0, pub = 0;
        for (int16_t o : pl.scored) (kObjectives[o].kind == 3 ? sec : pub) += 1;
        if (sec) gainTG(p, sec);
        const int c = std::min(pub, commodityCap(G, pl) - pl.commodities);
        if (c > 0) { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + c); commodityGained(p, cb_); }
        if (sec || c > 0) ds4Event(E, p, "Komdar Borodin", pn(p) + " collects interest: " + std::to_string(sec) + " trade goods, " + std::to_string(std::max(0, c)) + " commodities.");
    }
}

// Illusory Presence (Mortheus): after another player votes for the outcome they voted for, exhaust up to 2 planets - that
// player casts 1 + their influence more.
void Rules::ds4AfterVoting(std::vector<int>& voted, std::vector<int>& counts) {
    const int mo = playerOfFaction(G, "mortheus");
    if (mo < 0 || size_t(mo) >= voted.size() || voted[size_t(mo)] == kNoVote) return;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == mo || size_t(q) >= voted.size() || voted[size_t(q)] == kNoVote || counts[size_t(q)] <= 0) continue;
        std::vector<Option> opts;
        for (int x : ti::readyPlanets(G, mo)) opts.push_back({x, planetName(x), std::to_string(planetInfluence(G, x)) + " influence"});
        if (opts.empty()) return;
        curValue_ = voted[size_t(q)] == voted[size_t(mo)] ? 1 : 0;
        const std::vector<int> picks = fpickMany(mo, Ab::DsIllusory, "Illusory Presence: exhaust up to 2 planets so " + pn(q) + " casts more votes?", opts, 0,
                                                 std::min<int>(2, int(opts.size())), "They cast 1 more than the planets' influence.", true);
        curValue_ = 0;
        if (picks.empty()) continue;
        int more = 1;
        for (int x : picks) {
            G.planets[size_t(x)].exhausted = true;
            more += planetInfluence(G, x);
        }
        counts[size_t(q)] += more;
        ds4Event(E, mo, "Illusory Presence", pn(q) + " casts " + std::to_string(more) + " more votes, an illusion of support.");
    }
}

// Control (Olradin, "-" side of The People): 2 more votes for each cultural planet exhausted to vote.
int Rules::dsVoteBonus(int p, const std::vector<int>& spent) {
    const int envoy = ds5VoteBonus(p, spent) + ds7VoteBonus(p, spent);  // Envoy Network (batch 5), Networked Command (batch 6)
    if (!fac(p, "olradin") || !policyMinus(G, p, 0)) return envoy;
    int n = envoy;
    for (int x : spent) n += hasTraitOf(G, x, Trait::Cultural) ? 2 : 0;
    if (n > envoy) ds4Event(E, p, "Control", pn(p) + " casts " + std::to_string(n - envoy) + " more votes from its cultural worlds.");
    if (n > envoy && G.fixes >= 8 && breakthroughsOn(G)) dsbtPolicy(p);  // Insurrectionist Networking (0.44.4)
    return n;
}

// Knak Half-Ear (Olradin commander): when they pass, a non-home planet pays out as trade goods.
void Rules::ds4Passed(int p) {
    if (!commanderUsable(G, p, "olradin")) return;
    std::vector<Option> opts;
    for (int x : ti::readyPlanets(G, p))
        if (!isHome4(G, p, x) && !isMecatol4(G, x))
            opts.push_back({x, planetName(x), std::to_string(std::max(planetResources(G, x), planetInfluence(G, x))) + " trade goods"});
    if (opts.empty()) return;
    opts.push_back({-1, "Don't", ""});
    const int x = fpick(p, Ab::DsKnak, "Knak Half-Ear: exhaust a planet for trade goods?", opts, "", false, true);
    if (x < 0) return;
    G.planets[size_t(x)].exhausted = true;
    const int n = std::max(planetResources(G, x), planetInfluence(G, x));
    gainTG(p, n);
    ds4Event(E, p, "Knak Half-Ear", pn(p) + " cashes in " + planetName(x) + ": " + std::to_string(n) + " trade goods.");
    planetsExhausted(p, {x});
}

// Koryl Ferax (Cymiae commander): a planet taken during a tactical action - discard an action card for a mech there.
void Rules::ds4Control(int planet, int p, int old) {
    (void)old;
    if (p < 0 || p >= G.numPlayers || p != G.activePlayer || G.phase != Phase::Action || !commanderUsable(G, p, "cymiae") || !mechAbilities(G)) return;
    Player& pl = G.players[size_t(p)];
    if (pl.actionCards.empty() || unitsAvailable(G, p, UnitType::Mech) <= 0 || isDMZ(G, planet)) return;
    std::vector<Option> opts;
    for (int16_t c : pl.actionCards) opts.push_back({c, kActionCards[c].name, "Discard it for a mech on the planet."});
    opts.push_back({-1, "Keep my cards", ""});
    const int c = fpick(p, Ab::DsKoryl, "Koryl Ferax: discard an action card to put a mech on " + planetName(planet) + "?", opts);
    if (c < 0) return;
    pl.actionCards.erase(std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(c)));
    discardActionCard(p, c, false);
    addUnits(p, UnitType::Mech, 1, G.planets[size_t(planet)].system, planet);
    ds4Event(E, p, "Koryl Ferax", pn(p) + " drops a mech on " + planetName(planet) + ".", G.planets[size_t(planet)].system);
}

// Duuban (Mortheus mech): units committed to a planet of theirs next to its system - it swaps places with 1 infantry there.
void Rules::ds4Landed(int p, int sys, const std::vector<int>& landed) {
    const int mo = playerOfFaction(G, "mortheus");
    if (mo < 0 || mo == p || !mechAbilities(G)) return;
    for (int pl : landed) {
        if (G.planets[size_t(pl)].owner != mo || G.planets[size_t(pl)].units[size_t(mo)].count(UnitType::Infantry) == 0) continue;
        int from = -1;
        for (int16_t a : G.systems[size_t(sys)].adj)
            for (int16_t x : G.systems[size_t(a)].planets)
                if (from < 0 && G.planets[size_t(x)].units[size_t(mo)].count(UnitType::Mech)) from = x;
        if (from < 0) continue;
        if (!fyes(mo, Ab::DsDuuban, "Duuban: swap your mech on " + planetName(from) + " with 1 infantry on " + planetName(pl) + "?", "")) continue;
        Force& a = G.planets[size_t(from)].units[size_t(mo)];
        Force& b = G.planets[size_t(pl)].units[size_t(mo)];
        const bool dmg = a.dmg[int(UnitType::Mech)] > 0;
        a.remove(UnitType::Mech, 1, false);
        b.remove(UnitType::Infantry, 1, false);
        a.add(UnitType::Infantry, 1);
        b.add(UnitType::Mech, 1);
        if (dmg) b.dmg[int(UnitType::Mech)] = uint8_t(b.dmg[int(UnitType::Mech)] + 1);
        ds4Event(E, mo, "Duuban", pn(mo) + "'s mech blinks onto " + planetName(pl) + ".", sys);
    }
}

// Shroud of Lith (Kollecc): after their movement, up to 2 cloaked ships and 2 cloaked ground forces appear in the active system.
void Rules::ds4AfterMovement(int p, int sys) {
    if (!fac(p, "kollecc") || G.systems[size_t(sys)].space[size_t(p)].nonFighterShips() == 0) return;
    Player& pl = G.players[size_t(p)];
    int ships = 0, gf = 0;
    for (int16_t c : pl.captured)
        if (c / 16 == p) (isGroundForce(UnitType(c % 16)) ? gf : ships) += 1;
    if (!ships && !gf) return;
    int enemy = 0;
    for (int q = 0; q < kSides; ++q) enemy += q != p ? G.systems[size_t(sys)].space[size_t(q)].ships() : 0;
    for (int16_t x : G.systems[size_t(sys)].planets) enemy += G.planets[size_t(x)].owner != p ? 1 : 0;
    curValue_ = enemy;
    const bool use = fyes(p, Ab::DsShroud, "Shroud of Lith: bring up to 2 cloaked ships and 2 cloaked ground forces into " + sysName(sys) + "?",
                          std::to_string(ships) + " ships and " + std::to_string(gf) + " ground forces are on your sheet.");
    curValue_ = 0;
    if (!use) return;
    int s = 0, g = 0;
    for (size_t i = 0; i < pl.captured.size();) {
        const int16_t c = pl.captured[i];
        const UnitType t = UnitType(c % 16);
        const bool gfT = isGroundForce(t);
        if (c / 16 != p || (gfT ? g >= 2 : s >= 2)) {
            ++i;
            continue;
        }
        pl.captured.erase(pl.captured.begin() + long(i));
        G.systems[size_t(sys)].space[size_t(p)].add(t, 1);
        (gfT ? g : s) += 1;
    }
    ds4Event(E, p, "Shroud of Lith", pn(p) + "'s cloaked fleet appears: " + std::to_string(s) + " ships, " + std::to_string(g) + " ground forces.", sys);
    removeExcessCapacity(p, sys);
}

// After production: Cloaked Fleets (Kollecc) - up to 2 new ships onto the sheet; Walik (Mortheus agent) - 1 new unit next door.
void Rules::ds4Produced(int p, int sys, const Force& made) {
    if (fac(p, "kollecc")) {
        const int ships = made.nonFighterShips();
        if (ships > 0) {
            std::vector<Option> opts{{0, "None", ""}};
            for (int k = 1; k <= std::min(2, ships); ++k) opts.push_back({k, std::to_string(k), ""});
            curValue_ = sys == G.players[size_t(p)].homeSystem ? 1 : 0;
            const int k = fpick(p, Ab::DsCloaked, "Cloaked Fleets: cloak how many of the new ships (captured on your sheet)?", opts,
                                "Shroud of Lith brings them back where your ships move.");
            curValue_ = 0;
            int left = k;
            Force& f = G.systems[size_t(sys)].space[size_t(p)];
            for (int t = int(UnitType::WarSun); t >= 0 && left > 0; --t) {
                if (t == int(UnitType::Fighter)) continue;
                int can = std::min<int>(made.n[t], f.n[t]);
                while (can-- > 0 && left > 0) {
                    f.remove(UnitType(t), 1, false);
                    G.players[size_t(p)].captured.push_back(int16_t(p * 16 + t));
                    --left;
                }
            }
            if (k - left > 0) ds4Event(E, p, "Cloaked Fleets", pn(p) + " cloaks " + std::to_string(k - left) + " new ships.", sys);
            removeExcessCapacity(p, sys);
        }
    }
    const int user = agentUser(p, "mortheus");
    if (user < 0) return;
    std::vector<Option> to;
    for (int16_t a : G.systems[size_t(sys)].adj)
        if (!otherShipsIn(G, p, a) && !G.systems[size_t(a)].isHyperlane()) to.push_back({a, sysName(a), ""});
    int type = -1;
    for (int t = int(UnitType::WarSun); t >= 0 && type < 0; --t)
        if (made.n[t] && G.systems[size_t(sys)].space[size_t(p)].n[t] && isShip(UnitType(t)) && t != int(UnitType::Fighter)) type = t;
    if (to.empty() || type < 0) return;
    to.push_back({-1, "Don't", ""});
    agentFor(user, p);
    curValue_ = made.nonFighterShips();
    const int a = fpick(user, Ab::DsWalik, std::string("Exhaust Walik: one of the new ") + unitTypeName(UnitType(type), true) + " goes to a neighbouring system?", to, "", true);
    curValue_ = 0;
    curFor_ = -1;
    if (a < 0) return;
    spendAgent(user, "mortheus");
    agentDone(user, p);
    G.systems[size_t(sys)].space[size_t(p)].remove(UnitType(type), 1, false);
    G.systems[size_t(a)].space[size_t(p)].add(UnitType(type), 1);
    ds4Event(E, p, "Walik", pn(p) + "'s new " + unitTypeName(UnitType(type)) + " slips into " + sysName(a) + ".", a);
}

// ================================================================== action cards
// Cybernetic Madness (Cymiae): their resolved action cards are purged. Skhot Unit X-12 (agent): a card is drawn instead.
bool Rules::dsDiscarded(int p, int card, bool played) {
    bool purged = false;
    if (played && !acCancelled_ && fac(p, "cymiae")) {
        purged = true;
        ds4Event(E, p, "Cybernetic Madness", pn(p) + "'s " + kActionCards[card].name + " is purged, not discarded.");
    }
    const int user = agentUser(p, "cymiae");
    if (user >= 0 && !G.players[size_t(p)].eliminated && int(G.players[size_t(p)].actionCards.size()) < actionCardLimit(G, p)) {
        agentFor(user, p);
        const bool use = fyes(user, Ab::DsSkhot, user == p ? "Exhaust Skhot Unit X-12: draw 1 action card?" : "Exhaust Skhot Unit X-12 so " + pn(p) + " draws 1 action card?",
                              leaderText(findLeader("cymiaeagent")));
        curFor_ = -1;
        if (use) {
            spendAgent(user, "cymiae");
            agentDone(user, p);
            drawActionCards(p, 1, "Skhot Unit X-12");
        }
    }
    return purged;
}

// Autonetic Memory (Cymiae): 1 card less - one from the discard pile (then discard 1), or 1 infantry on a planet.
int Rules::dsAutonetic(int p, int n) {
    if (n <= 0 || !fac(p, "cymiae")) return n;
    Player& pl = G.players[size_t(p)];
    std::vector<Option> opts{{0, "Draw them all", ""}};
    if (!G.actionDiscard.empty()) opts.push_back({1, "1 from the discard pile", "Then you discard 1 action card (Cybernetic Madness)."});
    if (unitsAvailable(G, p, UnitType::Infantry) > 0 && !controlledPlanets(G, p).empty()) opts.push_back({2, "1 infantry on one of your planets", ""});
    if (opts.size() == 1) return n;
    const int c = fpick(p, Ab::DsAutonetic, "Autonetic Memory: draw 1 action card less?", opts);
    if (c == 1) {
        std::vector<Option> cards;
        for (int16_t a : G.actionDiscard) {
            bool dup = false;
            for (const Option& o : cards) dup = dup || o.id == a;
            if (!dup) cards.push_back({a, kActionCards[a].name, actionCardText(a)});
        }
        const int a = fpick(p, Ab::DsAutonetic, "Autonetic Memory: which card from the discard pile?", cards);
        G.actionDiscard.erase(std::find(G.actionDiscard.begin(), G.actionDiscard.end(), int16_t(a)));
        pl.actionCards.push_back(int16_t(a));
        ds4Event(E, p, "Autonetic Memory", pn(p) + " remembers " + kActionCards[a].name + " from the discard pile.");
        std::vector<Option> drop;
        for (int16_t x : pl.actionCards) drop.push_back({x, kActionCards[x].name, actionCardText(x)});
        const int d = pick(p, Tag::DiscardActionCard, "Cybernetic Madness: discard 1 action card", drop);
        pl.actionCards.erase(std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(d)));
        discardActionCard(p, d, false);
        return n - 1;
    }
    if (c == 2) {
        std::vector<Option> where;
        for (int x : controlledPlanets(G, p))
            if (!isDMZ(G, x)) where.push_back({x, planetName(x), ""});
        if (where.empty()) return n;
        const int x = where.size() == 1 ? where[0].id : fpick(p, Ab::DsAutonetic, "Autonetic Memory: the infantry goes on which planet?", where, "", false, true);
        addUnits(p, UnitType::Infantry, 1, G.planets[size_t(x)].system, x);
        ds4Event(E, p, "Autonetic Memory", pn(p) + " grows 1 infantry on " + planetName(x) + " instead of a card.", G.planets[size_t(x)].system);
        return n - 1;
    }
    return n;
}

// ================================================================== strategy cards, sustain, planets, retreats, rolls
// Fine Print (Vaden): after a player resolves a secondary of their card, 1 of that player's control tokens on the sheet.
void Rules::dsSecondaryDone(int q, int card) {
    (void)card;
    const int w = strategicPlayer_;
    if (w < 0 || w == q || q >= G.numPlayers || !fac(w, "vaden") || G.dsDebt[q] >= 6) return;
    if (!fyes(w, Ab::DsFinePrint, "Fine Print: place 1 of " + pn(q) + "'s control tokens on your faction sheet?", "")) return;
    ++G.dsDebt[q];
    ds4Event(E, w, "Fine Print", pn(q) + " signs the fine print: 1 control token on " + pn(w) + "'s sheet.");
}

// Krovoz Strike Teams (Vaden): their hit was cancelled by Sustain Damage - exhaust to destroy that unit.
void Rules::dsSustained(int victim, int attacker, int sys, int planet, int t) {
    if (attacker < 0 || attacker >= G.numPlayers || attacker == victim || !techReady(attacker, "dsvadey")) return;
    curValue_ = t;
    const bool use = fyes(attacker, Ab::DsKrovoz, std::string("Krovoz Strike Teams: destroy the ") + unitTypeName(UnitType(t)) + " that just sustained damage?", "");
    curValue_ = 0;
    if (!use) return;
    exhaustTech(attacker, "dsvadey");
    ds4Event(E, attacker, "Krovoz Strike Teams", pn(attacker) + "'s strike team finishes off a " + unitTypeName(UnitType(t)) + ".", sys);
    destroyUnits(victim, UnitType(t), 1, sys, planet, attacker, "Krovoz Strike Teams");
}

// Planets exhausted: Olradin policies (Connect, Preserve, Empower, Exploit), Baggil Wildpaw (Olradin agent).
void Rules::ds4PlanetsExhausted(int p, const std::vector<int>& planets) {
    if (p < 0 || p >= G.numPlayers || planets.empty()) return;
    Player& pl = G.players[size_t(p)];
    if (fac(p, "olradin")) {
        auto first = [&](Trait t) {
            for (int x : planets)
                if (hasTraitOf(G, x, t) || isLegendaryPlanet(G, x)) return x;
            return -1;
        };
        // Connect (+People): move 1 infantry from that planet to another of theirs
        if (!policyMinus(G, p, 0) && !(pl.dsPolOnce & 1)) {
            const int x = first(Trait::Cultural);
            if (x >= 0 && G.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry) > 0) {
                std::vector<Option> to;
                for (int y : controlledPlanets(G, p))
                    if (y != x && !isDMZ(G, y)) to.push_back({y, planetName(y), ""});
                if (!to.empty()) {
                    to.push_back({-1, "Don't", ""});
                    const int y = fpick(p, Ab::DsConnect, "Connect: move 1 infantry from " + planetName(x) + " to another of your planets?", to, "", false, true);
                    if (y >= 0) {
                        pl.dsPolOnce = uint8_t(pl.dsPolOnce | 1);
                        G.planets[size_t(x)].units[size_t(p)].remove(UnitType::Infantry, 1, false);
                        G.planets[size_t(y)].units[size_t(p)].add(UnitType::Infantry, 1);
                        ds4Event(E, p, "Connect", pn(p) + " moves 1 infantry from " + planetName(x) + " to " + planetName(y) + ".");
                        if (breakthroughsOn(G)) dsbtPolicy(p);  // Insurrectionist Networking (0.43.0)
                    }
                }
            }
        }
        // Preserve (+Environment): the top card of that planet's deck - a relic fragment is theirs
        if (!policyMinus(G, p, 1) && !(pl.dsPolOnce & 2)) {
            const int x = first(Trait::Hazardous);
            int d = -1;
            if (x >= 0)
                for (Trait t : {Trait::Hazardous, Trait::Cultural, Trait::Industrial})
                    if (d < 0 && hasTraitOf(G, x, t)) d = deckOf(t);
            if (d >= 0 && fyes(p, Ab::DsPreserve, "Preserve: reveal the top " + std::string(exploreTypeName(ExploreType(d))) + " exploration card (a relic fragment is yours)?", "")) {
                pl.dsPolOnce = uint8_t(pl.dsPolOnce | 2);
                const int c = drawExplore(d);
                if (c >= 0) {
                    if (isRelicFragment(c)) pl.fragments.push_back(int16_t(c));
                    else G.exploreDiscard[size_t(d)].push_back(int16_t(c));
                    ds4Event(E, p, "Preserve", pn(p) + " surveys a preserve: " + kExploreCards[c].name + (isRelicFragment(c) ? " - kept." : " - discarded."));
                        if (breakthroughsOn(G)) dsbtPolicy(p);  // Insurrectionist Networking (0.43.0)
                }
            }
        }
        // Empower (+Economy): 1 commodity / Exploit (-Economy): 1 fighter in a system with their ships
        if (!(pl.dsPolOnce & 4)) {
            if (!policyMinus(G, p, 2)) {
                if (first(Trait::Industrial) >= 0 && pl.commodities < commodityCap(G, pl)) {
                    pl.dsPolOnce = uint8_t(pl.dsPolOnce | 4);
                    { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + 1); commodityGained(p, cb_); }
                    ds4Event(E, p, "Empower", pn(p) + " gains 1 commodity.");
                        if (breakthroughsOn(G)) dsbtPolicy(p);  // Insurrectionist Networking (0.43.0)
                }
            } else {
                bool industrial = false;
                for (int x : planets) industrial = industrial || hasTraitOf(G, x, Trait::Industrial);
                if (industrial && unitsAvailable(G, p, UnitType::Fighter) > 0) {
                    std::vector<Option> to;
                    for (size_t s = 0; s < G.systems.size(); ++s)
                        if (G.systems[s].space[size_t(p)].ships() > 0) to.push_back({int(s), sysName(int(s)), ""});
                    if (!to.empty()) {
                        to.push_back({-1, "Don't", ""});
                        const int s = fpick(p, Ab::DsExploit, "Exploit: place 1 fighter in a system with your ships?", to, "", true);
                        if (s >= 0) {
                            pl.dsPolOnce = uint8_t(pl.dsPolOnce | 4);
                            addUnits(p, UnitType::Fighter, 1, s, -1);
                            removeExcessCapacity(p, s);
                            ds4Event(E, p, "Exploit", pn(p) + " places 1 fighter in " + sysName(s) + ".", s);
                        if (breakthroughsOn(G)) dsbtPolicy(p);  // Insurrectionist Networking (0.43.0)
                        }
                    }
                }
            }
        }
    }
    // Baggil Wildpaw (Olradin agent): ready one of their planets of another trait
    const int user = agentUser(p, "olradin");
    if (user < 0) return;
    int traits = 0;
    for (int x : planets)
        for (Trait t : {Trait::Cultural, Trait::Hazardous, Trait::Industrial})
            traits |= hasTraitOf(G, x, t) ? 1 << int(t) : 0;
    if (!traits) return;
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, p)) {
        if (!G.planets[size_t(x)].exhausted || std::find(planets.begin(), planets.end(), x) != planets.end()) continue;
        bool other = false;
        for (Trait t : {Trait::Cultural, Trait::Hazardous, Trait::Industrial})
            other = other || (hasTraitOf(G, x, t) && !((traits >> int(t)) & 1));
        if (other) opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + "/" + std::to_string(planetInfluence(G, x))});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "Don't", ""});
    agentFor(user, p);
    const int x = fpick(user, Ab::DsBaggil, user == p ? "Exhaust Baggil Wildpaw: ready one of your planets of another trait?" : "Exhaust Baggil Wildpaw for " + pn(p) + "?",
                        opts, "", false, true);
    curFor_ = -1;
    if (x < 0) return;
    spendAgent(user, "olradin");
    agentDone(user, p);
    G.planets[size_t(x)].exhausted = false;
    ds4Event(E, p, "Baggil Wildpaw", pn(p) + " readies " + planetName(x) + ".");
}

// Nightingale V (Kollecc flagship): when it retreats, every retreating unit may go onto the sheet (captured) instead.
bool Rules::dsRetreatCapture(int p, int sys) {
    if (!flagshipAbility(G, p, "kollecc") || !flagshipIn(G, p, sys) || !fac(p, "kollecc")) return false;
    if (!fyes(p, Ab::DsNightingale, "Nightingale V: cloak every retreating unit (captured on your sheet) instead of moving them?",
              "Shroud of Lith brings them back later.")) return false;
    Force& f = G.systems[size_t(sys)].space[size_t(p)];
    int n = 0;
    for (int t = 0; t < kUnitTypes; ++t)
        for (; f.n[t] > 0; ++n) {
            f.remove(UnitType(t), 1, false);
            G.players[size_t(p)].captured.push_back(int16_t(p * 16 + t));
        }
    ds4Event(E, p, "Nightingale V", pn(p) + "'s fleet vanishes into the shroud: " + std::to_string(n) + " units cloaked.", sys);
    return true;
}

// After a unit ability roll: Aurum Vadra (Vaden flagship) - bombardment hits pay 1 trade good; Suldhan Wraeg (Nivyn agent) -
// one unit's dice against them near an anomaly don't count.
void Rules::ds4UnitRolled(int roller, int target, int sys, RollResult& r, const char* what) {
    if (!std::strcmp(what, "bombardment") && flagshipAbility(G, roller, "vaden") && flagshipIn(G, roller, sys)) {
        bool hit = false;
        for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i) hit = hit || (r.unit[i] == int(UnitType::Flagship) && r.dice[i] >= r.need[i]);
        if (hit) {
            gainTG(roller, 1);
            ds4Event(E, roller, "Aurum Vadra", pn(roller) + " gains 1 trade good from the bombardment.", sys);
        }
    }
    if (target < 0 || target >= G.numPlayers || target == roller || r.hits <= 0 || !anomalyNear(G, sys)) return;
    const int user = agentUser(target, "nivyn");
    if (user < 0 || user != target) return;
    int best = -1, bestHits = 0;
    for (int t = 0; t < kUnitTypes; ++t) {
        int h = 0, seen = 0;
        const int per = std::max(1, int(t == int(UnitType::PDS) ? unitDef(G, roller, UnitType::PDS).cannonDice : 1));
        for (size_t i = 0; i < r.dice.size() && i < r.unit.size() && seen < per; ++i)
            if (r.unit[i] == t) {
                ++seen;
                h += r.dice[i] >= r.need[i] ? 1 : 0;
            }
        if (h > bestHits) {
            bestHits = h;
            best = t;
        }
    }
    if (best < 0) return;
    curValue_ = bestHits;
    const bool use = fyes(user, Ab::DsSuldhan, std::string("Exhaust Suldhan Wraeg: stop one of ") + pn(roller) + "'s " + unitTypeName(UnitType(best), true) +
                                                    " using its " + what + "?",
                          std::to_string(bestHits) + " of its hits are cancelled.");
    curValue_ = 0;
    if (!use) return;
    spendAgent(user, "nivyn");
    int left = bestHits;
    for (size_t i = 0; i < r.dice.size() && i < r.unit.size() && left > 0; ++i)
        if (r.unit[i] == best && r.dice[i] >= r.need[i]) {
            r.dice[i] = 1;
            r.need[i] = 11;
            --left;
        }
    recount(r);
    ds4Event(E, user, "Suldhan Wraeg", pn(user) + "'s advisor jams a " + unitTypeName(UnitType(best)) + ": " + std::to_string(bestHits) + " hits lost.", sys);
}

// Eradica (Nivyn flagship): other players' units in its system are removed instead of destroyed (no "when destroyed"
// effects) - the Nivyn always use it; their own units are destroyed as usual.
bool Rules::dsEradica(int p, int sys) const {
    if (sys < 0) return false;
    for (int q = 0; q < G.numPlayers; ++q)
        if (flagshipAbility(G, q, "nivyn") && flagshipIn(G, q, sys)) return !(q == p);
    return false;
}

// Units captured on a faction sheet come back to the board (Shroud of Lith, Hulgade's Hammer helpers).
void Rules::dsReturnCaptured(int p, UnitType t, int k, int sys, int planet) {
    for (int i = 0; i < k && returnCaptured(p, t); ++i) addUnits(p, t, 1, sys, planet);
}

// ================================================================== component actions: abilities, agents, heroes, notes
void Rules::ds4Options(int p, std::vector<Option>& o) {
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    const Player& pl = G.players[size_t(p)];
    if (techReady(p, "dskolly")) {  // Seeker Drones
        bool any = false;
        for (int q = 0; q < G.numPlayers; ++q) any = any || (q != p && areNeighbours(G, p, q) && fragmentCount(G, q) > 0);
        if (any) add(Ab::DsSeeker, "Seeker Drones", "Exhaust: a neighbour with relic fragments gains 2 trade goods and gives you one of their fragments.");
    }
    if (fac(p, "kollecc") && heroUnlocked(G, p) && !G.relicDeck.empty()) add(Ab::DsGriphyn, "Hero: Dorrahn Griphyn", "Draw relics (one per player), keep 1. Purged afterwards.");
    if (fac(p, "mortheus") && heroUnlocked(G, p)) add(Ab::DsBayan, "Hero: Bayan", "In each system with your ships, a copy of one of them. Purged afterwards.");
    if (fac(p, "vaden") && heroUnlocked(G, p)) {
        int n = 0;
        for (int q = 0; q < G.numPlayers; ++q) n += q != p ? G.dsDebt[q] : 0;
        if (n > 0) add(Ab::DsPutriv, "Hero: Putriv Sirvonsk", "Each token on your sheet goes back for 1 trade good, 2 commodities or a promissory note. Purged afterwards.");
    }
    if (fac(p, "cymiae") && heroUnlocked(G, p)) add(Ab::DsVoiceUnited, "Hero: The Voice United", "Reveal action cards: 1 to every other player, the rest are yours; they each discard 1. Purged afterwards.");
    const int cy = playerOfFaction(G, "cymiae");
    if (cy >= 0 && cy != p && holdsNote(G, p, cy, kNoteFaction) && !G.actionDiscard.empty())
        add(Ab::DsAlgorithmic, "Algorithmic Replication", "Take 1 action card from the discard pile; the note is purged.");
    if (fac(p, "nivyn") && heroUnlocked(G, p)) add(Ab::DsKwill, "Hero: Krill Drakkon", "Every unit that can sustain damage becomes damaged (not your mechs); this round your units are next to the Wound. Purged afterwards.");
    if (fac(p, "olradin") && heroUnlocked(G, p)) add(Ab::DsPahn, "Hero: Pahn Silverfur", "1 infantry on each of your planets; you may flip 1 policy. Purged afterwards.");
    const int ol = playerOfFaction(G, "olradin");
    if (ol >= 0 && ol != p && holdsNote(G, p, ol, kNoteFaction)) {
        bool spent = false;
        for (int x : controlledPlanets(G, p)) spent = spent || (G.planets[size_t(x)].exhausted && !isHome4(G, p, x) && !isMecatol4(G, x));
        if (spent && !ti::readyPlanets(G, p).empty())
            add(Ab::DsIncite, "Incite Revolution", "Play it face up: exhaust one of your planets and ready a non-home one (not Mecatol Rex).");
    }
    (void)pl;
}

bool Rules::ds4Action(int p, Ab ab) {
    Player& pl = G.players[size_t(p)];
    switch (ab) {
        case Ab::DsSeeker: {
            if (!techReady(p, "dskolly")) return false;
            std::vector<Option> opts;
            for (int q = 0; q < G.numPlayers; ++q)
                for (int16_t f : G.players[size_t(q)].fragments)
                    if (q != p && areNeighbours(G, p, q) && isRelicFragment(f) &&
                        !(G.fixes >= 8 && breakthroughsOn(G) && dsbtOn(q, "vaden") && !G.players[size_t(q)].human)) {  // (a bot's vault stays shut)
                        bool dup = false;
                        for (const Option& x : opts) dup = dup || x.id == q * 1000 + f;
                        if (!dup) opts.push_back({q * 1000 + f, std::string(kExploreCards[f].name) + " from " + pn(q), ""});
                    }
            if (opts.empty()) return false;
            const int c = fpick(p, Ab::DsSeeker, "Seeker Drones: which relic fragment do you take?", opts, "They gain 2 trade goods.");
            const int q = c / 1000, f = c % 1000;
            if (vaultShut(q, p, "a relic fragment")) return false;  // (High Security Vault)
            exhaustTech(p, "dskolly");
            auto& fr = G.players[size_t(q)].fragments;
            fr.erase(std::find(fr.begin(), fr.end(), int16_t(f)));
            pl.fragments.push_back(int16_t(f));
            gainTG(q, 2);
            ds4Event(E, p, "Seeker Drones", pn(p) + "'s drones bring back a " + kExploreCards[f].name + " from " + pn(q) + " (who gains 2 trade goods).");
            return true;
        }
        case Ab::DsGriphyn: {
            if (!heroUnlocked(G, p) || G.relicDeck.empty()) return false;
            purgeHero(p);
            std::vector<int16_t> drawn;
            for (int i = 0; i < G.numPlayers && !G.relicDeck.empty(); ++i) {
                drawn.push_back(G.relicDeck.back());
                G.relicDeck.pop_back();
            }
            E.revealed();
            std::vector<Option> opts;
            for (int16_t r : drawn) opts.push_back({r, kRelics[r].name, relicText(r)});
            const int keep = fpick(p, Ab::DsGriphyn, "Dorrahn Griphyn: which relic do you keep?", opts);
            for (int16_t r : drawn)
                if (r != keep) G.relicDeck.push_back(r);
            E.rng().shuffle(G.relicDeck);
            ds4Hero(E, G, p, pn(p) + " sends the Shrouded Expedition: " + std::to_string(drawn.size()) + " relics found, one kept.");
            if (!dsRelicGained(p, keep)) takeRelic(p, keep);
            return true;
        }
        case Ab::DsBayan: {
            if (!heroUnlocked(G, p)) return false;
            purgeHero(p);
            ds4Hero(E, G, p, pn(p) + " weaves the Two by Two: ships double across the galaxy.");
            for (size_t s = 0; s < G.systems.size(); ++s) {
                const Force& f = G.systems[s].space[size_t(p)];
                if (!f.ships()) continue;
                std::vector<Option> opts;
                for (int t : kShipIdx)
                    if (f.n[t] && unitsAvailable(G, p, UnitType(t)) > 0) opts.push_back({t, unitTypeName(UnitType(t)), ""});
                if (opts.empty()) continue;
                opts.push_back({-1, "None here", ""});
                const int t = fpick(p, Ab::DsBayan, "Bayan: copy which ship in " + sysName(int(s)) + "?", opts);
                if (t < 0) continue;
                addUnits(p, UnitType(t), 1, int(s), -1);
            }
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (G.systems[s].space[size_t(p)].ships()) removeExcessCapacity(p, int(s));
            return true;
        }
        case Ab::DsPutriv: {
            if (!heroUnlocked(G, p)) return false;
            purgeHero(p);
            ds4Hero(E, G, p, pn(p) + " calls in every debt.");
            for (int q = 0; q < G.numPlayers; ++q) {
                Player& ql = G.players[size_t(q)];
                for (; q != p && G.dsDebt[q] > 0; --G.dsDebt[q]) {
                    std::vector<Option> opts;
                    if (ql.tradeGoods > 0) opts.push_back({0, "1 trade good", ""});
                    if (ql.commodities >= 2) opts.push_back({1, "2 commodities", ""});
                    for (int16_t n : ql.notes)
                        if (noteOwner(n) != p) opts.push_back({100 + n, noteNameOf(G, n), "A promissory note."});
                    if (opts.empty()) continue;
                    const int c = opts.size() == 1 ? opts[0].id : fpick(q, Ab::DsPutrivPay, "Debts Come Due: what do you give " + pn(p) + "?", opts);
                    if (c == 0) {
                        ql.tradeGoods = int8_t(ql.tradeGoods - 1);
                        gainTG(p, 1);
                    } else if (c == 1) {
                        ql.commodities = int8_t(ql.commodities - 2);
                        gainTG(p, 2);  // (commodities given in a transaction become trade goods, LRR 21.4)
                    } else {
                        giveNote(q, p, c - 100);
                    }
                    say(q, pn(q) + " pays " + pn(p) + " what it owes.");
                }
            }
            return true;
        }
        case Ab::DsVoiceUnited: {
            if (!heroUnlocked(G, p)) return false;
            purgeHero(p);
            ds4Hero(E, G, p, pn(p) + " lets the Scream out: action cards for everyone, and losses for all.");
            std::vector<int16_t> drawn;
            for (int i = 0; i < G.numPlayers + 1; ++i) {
                if (G.actionDeck.empty()) {
                    if (G.actionDiscard.empty()) break;
                    G.actionDeck = G.actionDiscard;
                    G.actionDiscard.clear();
                    E.rng().shuffle(G.actionDeck);
                }
                drawn.push_back(G.actionDeck.back());
                G.actionDeck.pop_back();
            }
            E.revealed();
            for (int q = 0; q < G.numPlayers; ++q) {
                if (q == p || G.players[size_t(q)].eliminated || drawn.empty()) continue;
                std::vector<Option> opts;
                for (int16_t c : drawn) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
                const int c = fpick(p, Ab::DsVoiceUnited, "The Voice United: which card does " + pn(q) + " get?", opts);
                drawn.erase(std::find(drawn.begin(), drawn.end(), int16_t(c)));
                G.players[size_t(q)].actionCards.push_back(int16_t(c));
            }
            for (int16_t c : drawn) pl.actionCards.push_back(c);
            for (int q = 0; q < G.numPlayers; ++q) {
                Player& ql = G.players[size_t(q)];
                if (q == p || ql.eliminated || ql.actionCards.empty()) continue;
                std::vector<Option> opts;
                for (int16_t c : ql.actionCards) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
                const int c = pick(q, Tag::DiscardActionCard, "The Scream: discard 1 action card", opts);
                ql.actionCards.erase(std::find(ql.actionCards.begin(), ql.actionCards.end(), int16_t(c)));
                discardActionCard(q, c, false);
                enforceHandLimit(q);
            }
            enforceHandLimit(p);
            return true;
        }
        case Ab::DsAlgorithmic: {
            const int cy = playerOfFaction(G, "cymiae");
            if (cy < 0 || !holdsNote(G, p, cy, kNoteFaction) || G.actionDiscard.empty()) return false;
            std::vector<Option> cards;
            for (int16_t a : G.actionDiscard) {
                bool dup = false;
                for (const Option& x : cards) dup = dup || x.id == a;
                if (!dup) cards.push_back({a, kActionCards[a].name, actionCardText(a)});
            }
            const int a = fpick(p, Ab::DsAlgorithmic, "Algorithmic Replication: which card from the discard pile?", cards);
            pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), makeNote(cy, kNoteFaction)), pl.notes.end());  // (purged)
            G.actionDiscard.erase(std::find(G.actionDiscard.begin(), G.actionDiscard.end(), int16_t(a)));
            pl.actionCards.push_back(int16_t(a));
            ds4Event(E, p, "Algorithmic Replication", pn(p) + " replicates " + kActionCards[a].name + " from the discard pile.");
            if (G.fixes >= 8) purged(p);  // (0.44.4: the note was purged - Erasure Corps, Aggressive Broker)
            enforceHandLimit(p);
            return true;
        }
        case Ab::DsKwill: {
            if (!heroUnlocked(G, p)) return false;
            purgeHero(p);
            pl.heroRound = int16_t(G.round);
            for (size_t s = 0; s < G.systems.size(); ++s)
                for (int q = 0; q < G.numPlayers; ++q) {
                    Force& f = G.systems[s].space[size_t(q)];
                    for (int t = 0; t < kUnitTypes; ++t)
                        if (f.n[t] && unitSustains(G, q, UnitType(t))) f.dmg[t] = f.n[t];
                }
            for (PlanetState& ps : G.planets)
                for (int q = 0; q < G.numPlayers; ++q) {
                    Force& f = ps.units[size_t(q)];
                    for (int t = 0; t < kUnitTypes; ++t)
                        if (f.n[t] && unitSustains(G, q, UnitType(t)) && !(q == p && t == int(UnitType::Mech))) f.dmg[t] = f.n[t];
                }
            ds4Hero(E, G, p, pn(p) + " opens the Eye of the Abyss: the galaxy's hulls crack, and the Wound is next to every Nivyn unit this round.");
            return true;
        }
        case Ab::DsPahn: {
            if (!heroUnlocked(G, p)) return false;
            purgeHero(p);
            int n = 0;
            for (int x : controlledPlanets(G, p))
                if (!isDMZ(G, x) && unitsAvailable(G, p, UnitType::Infantry) > 0) {
                    addUnits(p, UnitType::Infantry, 1, G.planets[size_t(x)].system, x);
                    ++n;
                }
            ds4Hero(E, G, p, pn(p) + " rallies the Shadow Council: " + std::to_string(n) + " infantry raised.");
            static const char* kName[3] = {"The People", "The Environment", "The Economy"};
            std::vector<Option> opts{{-1, "Flip none", ""}};
            for (int k = 0; k < 3; ++k) opts.push_back({k, std::string("Flip ") + kName[k], policyMinus(G, p, k) ? "To its + side" : "To its - side"});
            const int k = fpick(p, Ab::DsPahn, "Pahn Silverfur: flip a policy?", opts);
            if (k >= 0) {
                pl.dsPolicy = uint8_t(pl.dsPolicy ^ (1 << k));
                ds4Event(E, p, "Policies", pn(p) + " flips " + kName[k] + ".");
            }
            return true;
        }
        case Ab::DsIncite: {
            const int ol = playerOfFaction(G, "olradin");
            if (ol < 0 || !holdsNote(G, p, ol, kNoteFaction)) return false;
            std::vector<Option> spent, ready;
            for (int x : controlledPlanets(G, p))
                if (G.planets[size_t(x)].exhausted && !isHome4(G, p, x) && !isMecatol4(G, x)) spent.push_back({x, planetName(x), ""});
            for (int x : ti::readyPlanets(G, p)) ready.push_back({x, planetName(x), ""});
            if (spent.empty() || ready.empty()) return false;
            const int a = fpick(p, Ab::DsIncite, "Incite Revolution: exhaust which of your planets?", ready, "", false, true);
            const int b = fpick(p, Ab::DsIncite, "Incite Revolution: ready which planet?", spent, "", false, true);
            pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), makeNote(ol, kNoteFaction)), pl.notes.end());
            pl.notesInPlay.push_back(makeNote(ol, kNoteFaction));
            G.planets[size_t(a)].exhausted = true;
            G.planets[size_t(b)].exhausted = false;
            if (G.fixes >= 11) planetsExhausted(p, {a});  // (0.44.7)
            ds4Event(E, p, "Incite Revolution", pn(p) + " exhausts " + planetName(a) + " and readies " + planetName(b) + ".");
            return true;
        }
        default: return ds5Action(p, ab);  // batch 5 (rules_ds5.cpp)
    }
}

}  // namespace ti
