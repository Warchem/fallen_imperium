// Discordant Stars batch 3 (0.39.0): the abilities of six more factions, called from the ds* hooks in rules_ds.cpp /
// rules_ds2.cpp and from a few new ones (exploring, relics, objectives, movement, production, strategic actions).
// Texts in faction_text.cpp, the AI in ai/ai_ds.cpp.
//   Bentor Conglomerate   - Secret Maps, Fortune Seekers, Ancient Blueprints (Player::dsFrags), Wayfinder, Broker Network
//                           (query productionCapacity), Merged Replicators, Encryption Key (Game::dsKeyPlanet), C.O.O.
//                           Mgur, C.M.O. Ranc (query commodityValue), C.E.O. Ken Tucc, Auctioneer
//   Dih-Mohn Flotilla     - Capital Fleet (query fleetUse), Migrant Fleet, Flotilla (query overflowOf, removeExcess-
//                           Capacity), Maximus, Aegis I / II, Impressment Programs, Combat Drills, Jgin Faru, Clona
//                           Bathru, Verrisus Ypru, Repairitor
//   Florzen Profiteers    - Mercenaries, Data Leak, Black Markets, Man O' War (cardPlayable), Corsair (data: fighters that
//                           move on their own), Blackmail Programs, Underground Market, Sal Gavda, Quaxdol Junitas,
//                           Banua Gowen, Privateer
//   Gledge Union          - Mantle Cracking, Celestial Reclamation (Game::dsCores, query attachBonus), Deep Mining,
//                           Beg Bersha, Orion Platform I / II, Lightning Drives (query moveBoost, checkMovePlan), Gledge
//                           Base, Durran, Voldun (productionCapacity), Gorthrim, Exodriller (query planetPayValue)
//   Augurs of Ilyxum      - Oracle AI / Limited Vision / Probability Algorithms (Player::dsSheet), Nemsys,
//                           Psychographics, Sentient Datapool, Read the Fates, Clodho, Lachis, Atropha, Iledrith
//   Berserkers of Kjalengard - Glory (Game::dsGlory), Heroism (captured tokens), Valor (RollMods::valor), Hulgade's
//                           Hammer, Star Dragon I / II (query shipReach), Zhrgar Stimulants, Vassalage, Merkismathr
//                           Asvand, Sdallari Tvungovot (query upgradeWaivers), Ygegnad, Skald
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
constexpr int kShipMask3 = kShipMaskAll;  // Carrier .. WarSun, the second flagship

void ds3Event(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
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
void ds3Hero(Engine& E, const Game& G, int p, const std::string& text) {
    const int i = leaderIndex(G, p, kHero);
    Event e;
    e.kind = EventKind::Leader;
    e.player = int8_t(p);
    e.card = int16_t(i >= 0 ? G.players[size_t(p)].leaders[size_t(i)].leader : 0);
    e.label = "hero";
    e.text = text;
    E.emit(e);
}
bool explorable3(const Game& G, int x) {
    const PlanetState& ps = G.planets[size_t(x)];
    if (ps.destroyed || ps.system < 0) return false;
    return hasTraitOf(G, x, Trait::Cultural) || hasTraitOf(G, x, Trait::Hazardous) || hasTraitOf(G, x, Trait::Industrial);
}
int bits(unsigned v) {
    int n = 0;
    for (; v; v &= v - 1) ++n;
    return n;
}
bool exKey3(int card, const char* k) { return card >= 0 && card < kExploreCardCount && !std::strcmp(kExploreCards[card].key, k); }
int traitDeck(Trait t) {
    switch (t) {
        case Trait::Cultural: return int(ExploreType::Cultural);
        case Trait::Hazardous: return int(ExploreType::Hazardous);
        case Trait::Industrial: return int(ExploreType::Industrial);
        default: return -1;
    }
}
bool techPlanet(const Game& G, int x) { return isLegendaryPlanet(G, x) || planetHasSkip(G, x); }
int secretsScored(const Game& G, int p) {
    int n = 0;
    for (int16_t o : G.players[size_t(p)].scored) n += kObjectives[o].kind == 3 ? 1 : 0;
    return n;
}
int mechsIn(const Game& G, int sys) {
    int n = 0;
    for (int q = 0; q < G.numPlayers; ++q) n += G.systems[size_t(sys)].space[size_t(q)].count(UnitType::Mech);
    for (int16_t x : G.systems[size_t(sys)].planets)
        for (int q = 0; q < G.numPlayers; ++q) n += G.planets[size_t(x)].units[size_t(q)].count(UnitType::Mech);
    return n;
}
int mechsNear(const Game& G, int sys) {  // Beg Bersha: every mech in or next to the system
    int n = mechsIn(G, sys);
    for (int16_t a : G.systems[size_t(sys)].adj) n += mechsIn(G, a);
    return n;
}
bool gloryIn(const Game& G, int sys) { return std::find(G.dsGlory.begin(), G.dsGlory.end(), int16_t(sys)) != G.dsGlory.end(); }
bool gloryNear(const Game& G, int sys) {
    if (gloryIn(G, sys)) return true;
    for (int16_t a : G.systems[size_t(sys)].adj)
        if (gloryIn(G, a)) return true;
    return false;
}
bool mechIn(const Game& G, int p, int sys) {
    if (G.systems[size_t(sys)].space[size_t(p)].count(UnitType::Mech)) return true;
    for (int16_t x : G.systems[size_t(sys)].planets)
        if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech)) return true;
    return false;
}
bool isHomePlanet(const Game& G, int p, int x) { return G.planets[size_t(x)].system == G.players[size_t(p)].homeSystem; }
bool isMecatol(int x, const Game& G) { return !std::strcmp(kPlanets[G.planets[size_t(x)].def].key, "mr"); }
// Repairitor: repair the most expensive damaged unit in f (returns its type, or -1)
int repairBest(const Game& G, int p, Force& f) {
    int best = -1;
    for (int t = 0; t < kUnitTypes; ++t)
        if (f.dmg[t] > 0 && (best < 0 || unitCost2(G, p, UnitType(t)) > unitCost2(G, p, UnitType(best)))) best = t;
    if (best >= 0) f.dmg[best] = uint8_t(f.dmg[best] - 1);
    return best;
}
}  // namespace

// ================================================================== combat
void Rules::ds3CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m) {
    (void)opp;
    (void)planet;
    if (p < 0 || p >= G.numPlayers || sys < 0) return;
    if (gloryIn(G, sys)) m.valor = true;  // Valor (Kjalengard): every combat roll in a system with a Glory token
    if (!space) {
        ds4CombatMods(p, opp, sys, planet, space, m);  // batch 4 (Thussad Krath)
        ds5CombatMods(p, opp, sys, planet, space, m);  // batch 5
        ds6CombatMods(p, opp, sys, planet, space, m);  // batch 6
        ds7CombatMods(p, opp, sys, planet, space, m);
        return;
    }
    if (flagshipAbility(G, p, "bentor") && flagshipIn(G, p, sys)) m.perType[int(UnitType::Flagship)] += bits(G.players[size_t(p)].dsFrags);  // Wayfinder
    if (flagshipAbility(G, p, "gledge") && flagshipIn(G, p, sys)) m.extraDice[int(UnitType::Flagship)] += mechsNear(G, sys);  // Beg Bersha
    if (flagshipAbility(G, p, "augers") && flagshipIn(G, p, sys)) m.extraDice[int(UnitType::Flagship)] += secretsScored(G, p);  // Nemsys
    if ((ds3Vassal_ >> p) & 1) m.perType[int(UnitType::Fighter)] += 1;  // Vassalage (the Kjalengard's note)
    ds4CombatMods(p, opp, sys, planet, space, m);  // batch 4
    ds5CombatMods(p, opp, sys, planet, space, m);  // batch 5: Vox, Kaliburn, Annah Regia, Rule of Two
    ds6CombatMods(p, opp, sys, planet, space, m);  // batch 6: Richtyrian, Interference Grid, Dume Tathu
    ds7CombatMods(p, opp, sys, planet, space, m);
}

void Rules::ds3SpaceCombatStart(int att, int def, int sys) {
    ds3Vassal_ = 0;
    SystemState& s = G.systems[size_t(sys)];
    // Vassalage (the Kjalengard's note): +1 to the holder's fighters; the Kjalengard capture those destroyed
    const int kj = playerOfFaction(G, "kjalengard");
    for (int side : {att, def}) {
        if (side >= G.numPlayers || kj < 0 || side == kj || !holdsNote(G, side, kj, kNoteFaction) || !s.space[size_t(side)].count(UnitType::Fighter)) continue;
        curValue_ = s.space[size_t(side)].count(UnitType::Fighter);
        const bool use = fyes(side, Ab::DsVassalage, "Play " + pn(kj) + "'s Vassalage: +1 to your fighters' combat rolls in this combat?",
                              "The Kjalengard capture your fighters destroyed in this combat. The note goes back to them.");
        curValue_ = 0;
        if (!use) continue;
        returnNote(side, makeNote(kj, kNoteFaction));
        ds3Vassal_ = uint16_t(ds3Vassal_ | (1u << side));
        ds3Event(E, side, "Vassalage", pn(side) + "'s fighters fight harder under " + pn(kj) + "'s banner.", sys);
    }
    // Mercenaries (Florzen): up to 2 of their fighters from a neighbouring system - a side of the combat places as many
    for (int f = 0; f < G.numPlayers; ++f) {
        if (!fac(f, "florzen")) continue;
        std::vector<Option> from;
        for (int16_t a : s.adj) {
            const int n = G.systems[size_t(a)].space[size_t(f)].count(UnitType::Fighter);
            if (n > 0) from.push_back({a, sysName(a), std::to_string(n) + " fighters"});
        }
        if (from.empty()) continue;
        from.push_back({-1, "Don't", ""});
        curValue_ = (f == att || f == def) ? 1 : 0;
        const int a = fpick(f, Ab::DsMercenaries, "Mercenaries: remove up to 2 of your fighters next door - a side of this combat places as many here?", from, "", true);
        curValue_ = 0;
        if (a < 0) continue;
        const int k = std::min(2, int(G.systems[size_t(a)].space[size_t(f)].count(UnitType::Fighter)));
        std::vector<Option> who;
        for (int q : {att, def})
            if (q < G.numPlayers) who.push_back({q, pn(q), q == f ? "You" : ""});
        if (who.empty()) continue;
        const int q = who.size() == 1 ? who[0].id : fpick(f, Ab::DsMercTarget, "Mercenaries: who places " + std::to_string(k) + " fighters in the combat?", who);
        const bool was = removing_;
        removing_ = true;
        destroyUnits(f, UnitType::Fighter, k, a, -1, -1, "Mercenaries");
        removing_ = was;
        const int got = std::min(k, unitsAvailable(G, q, UnitType::Fighter));
        if (got > 0) addUnits(q, UnitType::Fighter, got, sys, -1);
        ds3Event(E, f, "Mercenaries", pn(f) + " sells " + std::to_string(k) + " fighters' service: " + pn(q) + " places " + std::to_string(got) + " fighters here.", sys);
    }
    for (int side : {att, def}) {
        if (side >= G.numPlayers) continue;
        const int other = side == att ? def : att;
        // Clona Bathru (Dih-Mohn commander): 3 or more different non-fighter ship types here - 1 hit on the opponent's ships
        if (commanderUsable(G, side, "dihmohn") && s.space[size_t(other)].ships() > 0) {
            int types = 0;
            for (int t : kShipIdx) types += t != int(UnitType::Fighter) && s.space[size_t(side)].n[t] ? 1 : 0;
            if (types >= 3) {
                ds3Event(E, side, "Clona Bathru", pn(side) + "'s mixed fleet opens with 1 hit.", sys);
                assignHits(other, sys, -1, 1, kShipMask3, "Clona Bathru", side);
            }
        }
        // Repairitor (Dih-Mohn mech): repair 1 of their ships in the combat
        if (fac(side, "dihmohn") && mechAbilities(G) && mechIn(G, side, sys)) {
            const int t = repairBest(G, side, s.space[size_t(side)]);
            if (t >= 0) ds3Event(E, side, "Repairitor", pn(side) + "'s Repairitor repairs a " + unitTypeName(UnitType(t)) + ".", sys);
        }
    }
    // Merkismathr Asvand (Kjalengard agent): a Glory token moves here; the active player gains commodities
    if (kj >= 0 && (kj == att || kj == def) && canUseAgent(kj, "kjalengard") && !gloryIn(G, sys) && !G.dsGlory.empty()) {
        if (fyes(kj, Ab::DsMerkismathr, "Exhaust Merkismathr Asvand: move a Glory token into this system?",
                 "Then the active player gains 1 commodity for each neighbour.")) {
            spendAgent(kj, "kjalengard");
            dsGlory(kj, sys, true, "Merkismathr Asvand");
            const int ap = G.activePlayer;
            if (ap >= 0 && ap < G.numPlayers) {
                int n = 0;
                for (int q = 0; q < G.numPlayers; ++q) n += q != ap && areNeighbours(G, ap, q) ? 1 : 0;
                Player& apl = G.players[size_t(ap)];
                const int got = std::max(0, std::min(n, commodityCap(G, apl) - apl.commodities));
                { const int cb_ = apl.commodities; apl.commodities = int8_t(apl.commodities + got); commodityGained(ap, cb_); }
                if (got) say(ap, pn(ap) + " gains " + std::to_string(got) + " commodities (Merkismathr Asvand).", sys);
            }
        }
    }
    ds4SpaceCombatStart(att, def, sys);  // batch 4: Facsimile, Komat, Shrouded Skirmishers
    ds5SpaceCombatStart(att, def, sys);  // batch 5: Nokar Navigator
    ds6SpaceCombatStart(att, def, sys);  // batch 6: Interference Grid, Trusted Counselor
}

void Rules::ds3GroundCombatStart(int att, int def, int planet) {
    ds3Vassal_ = 0;
    const int sys = G.planets[size_t(planet)].system;
    for (int side : {att, def}) {
        if (side >= G.numPlayers) continue;
        // Repairitor (Dih-Mohn mech): repair 1 of their ground forces in the combat
        if (fac(side, "dihmohn") && mechAbilities(G) && mechIn(G, side, sys)) {
            const int t = repairBest(G, side, G.planets[size_t(planet)].units[size_t(side)]);
            if (t >= 0) ds3Event(E, side, "Repairitor", pn(side) + "'s Repairitor repairs a " + unitTypeName(UnitType(t)) + ".", sys);
        }
    }
    const int kj = playerOfFaction(G, "kjalengard");
    if (kj >= 0 && (kj == att || kj == def) && canUseAgent(kj, "kjalengard") && !gloryIn(G, sys) && !G.dsGlory.empty() &&
        fyes(kj, Ab::DsMerkismathr, "Exhaust Merkismathr Asvand: move a Glory token into this system?", "Then the active player gains 1 commodity for each neighbour.")) {
        spendAgent(kj, "kjalengard");
        dsGlory(kj, sys, true, "Merkismathr Asvand");
        const int ap = G.activePlayer;
        if (ap >= 0 && ap < G.numPlayers) {
            int n = 0;
            for (int q = 0; q < G.numPlayers; ++q) n += q != ap && areNeighbours(G, ap, q) ? 1 : 0;
            Player& apl = G.players[size_t(ap)];
            const int got = std::max(0, std::min(n, commodityCap(G, apl) - apl.commodities));
            { const int cb_ = apl.commodities; apl.commodities = int8_t(apl.commodities + got); commodityGained(ap, cb_); }
            if (got) say(ap, pn(ap) + " gains " + std::to_string(got) + " commodities (Merkismathr Asvand).", sys);
        }
    }
}

// After both sides rolled a combat round: Verrisus Ypru (space), Zhrgar Stimulants (rerolls, a trade good).
void Rules::dsAfterCombatRolls(int att, int def, int sys, int planet, RollResult& rA, RollResult& rD) {
    const bool space = planet < 0;
    for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? att : def;
        if (side >= G.numPlayers) continue;
        RollResult& mine = i == 0 ? rA : rD;
        // Zhrgar Stimulants (Kjalengard): exhaust to reroll any of their own dice (the misses)
        const int misses = int(mine.dice.size()) - mine.hits;
        if (G.fixes >= 10) {
            // 0.44.6: "after a player rolls combat dice" - any owner may let that player reroll (their own dice too),
            // and with a single miss as well (the bots only bother with 2 or more of their own)
            if (misses < 1) continue;
            std::vector<int> owners;
            if (techReady(side, "dskjalg")) owners.push_back(side);
            for (int k : windowOrder())
                if (k != side && k < G.numPlayers && !G.players[size_t(k)].eliminated && techReady(k, "dskjalg") &&
                    zhrgarQuiet_[k] != G.round)
                    owners.push_back(k);
            for (int k : owners) {
                const int prevFor = curFor_;
                curValue_ = misses;
                curFor_ = side;
                bool use = false;
                if (k == side) {
                    use = fyes(k, Ab::DsZhrgar, "Exhaust Zhrgar Stimulants: reroll your " + std::to_string(misses) + " missed dice?", "");
                } else {  // (another player's roll: a person may stop these questions for the round)
                    const int c = fpick(k, Ab::DsZhrgar, "Exhaust Zhrgar Stimulants: let " + pn(side) + " reroll their " + std::to_string(misses) + " missed dice?",
                                        {{1, "Yes", ""}, {0, "No", ""}, {2, "No - not for other players' rolls this round", ""}});
                    if (c == 2) zhrgarQuiet_[k] = int16_t(G.round);
                    use = c == 1;
                }
                curValue_ = 0;
                curFor_ = prevFor;
                if (!use) continue;
                exhaustTech(k, "dskjalg");
                rerollDice(side, mine, false);
                reportRoll(side, sys, "Rerolled (Zhrgar Stimulants)", mine);
                break;
            }
            continue;
        }
        if (techReady(side, "dskjalg") && misses >= 2) {
            curValue_ = misses;
            const bool use = fyes(side, Ab::DsZhrgar, "Exhaust Zhrgar Stimulants: reroll your " + std::to_string(misses) + " missed dice?", "");
            curValue_ = 0;
            if (use) {
                exhaustTech(side, "dskjalg");
                rerollDice(side, mine, false);
                reportRoll(side, sys, "Rerolled (Zhrgar Stimulants)", mine);
            }
        }
    }
    // Zhrgar Stimulants: once per action, hits in a system with a Glory token - its owner gains 1 trade good
    if (gloryIn(G, sys) && (rA.hits > 0 || rD.hits > 0))
        for (int k = 0; k < G.numPlayers; ++k)
            if (owns(k, "dskjalg") && !G.players[size_t(k)].dsOnce) {
                G.players[size_t(k)].dsOnce = true;
                gainTG(k, 1);
                ds3Event(E, k, "Zhrgar Stimulants", pn(k) + " gains 1 trade good from the bloodshed.", sys);
            }
    ds4AfterCombatRolls(att, def, sys, planet, rA, rD);  // batch 4: Voidwake Missiles, Krovoz Strike Teams
    ds5AfterCombatRolls(att, def, sys, planet, rA, rD);  // batch 5: Psychoactive Armaments, Bearer of Heavens, Skarvald & Torvar...
    // Verrisus Ypru (Dih-Mohn hero): every ship here becomes damaged; theirs can't be destroyed this round
    if (!space) return;
    for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? att : def;
        if (side >= G.numPlayers || !fac(side, "dihmohn") || !heroUnlocked(G, side)) continue;
        RollResult& incoming = i == 0 ? rD : rA;
        if (incoming.hits <= 0) continue;
        curValue_ = incoming.hits;
        const bool use = fyes(side, Ab::DsYpru, "Purge Verrisus Ypru: every ship here becomes damaged, and yours can't be destroyed this round?",
                              std::to_string(incoming.hits) + " hits are coming at your ships.");
        curValue_ = 0;
        if (!use) continue;
        purgeHero(side);
        for (int q = 0; q < kSides; ++q) {
            Force& f = G.systems[size_t(sys)].space[size_t(q)];
            for (int t : kShipIdx)
                if (f.n[t] && q < G.numPlayers && unitSustains(G, q, UnitType(t))) f.dmg[t] = f.n[t];
        }
        incoming.hits = 0;
        ds3Hero(E, G, side, pn(side) + " overloads the defense matrix: every ship in the system is damaged, and " + pn(side) +
                                "'s ships can't be destroyed this round.");
    }
}


// Hulgade's Hammer (Kjalengard flagship): after the first round of a combat in its system, up to 2 captured units come back.
void Rules::ds3SpaceRoundEnd(int att, int def, int sys) {
    if (combatRound_ != 1) return;
    for (int side : {att, def}) {
        if (side >= G.numPlayers || !flagshipAbility(G, side, "kjalengard") || !flagshipIn(G, side, sys)) continue;
        Player& pl = G.players[size_t(side)];
        int placed = 0;
        for (UnitType t : {UnitType::Fighter, UnitType::Infantry})
            while (placed < 2 && capturedCount(G, side, t) > 0 && unitsAvailable(G, side, t) > 0) {
                returnCaptured(side, t);
                addUnits(side, t, 1, sys, -1);
                ++placed;
            }
        (void)pl;
        if (placed) ds3Event(E, side, "Hulgade's Hammer", pn(side) + "'s fallen return to the fight: " + std::to_string(placed) + " captured units placed here.", sys);
    }
}
void Rules::dsGroundRoundEnd(int att, int def, int planet) {
    ds4GroundRoundEnd(att, def, planet);  // batch 4: Collector (every round)
    ds5RoundEnd(att, def, G.planets[size_t(planet)].system, false);  // batch 5: Necrophage
    if (combatRound_ != 1) return;
    const int sys = G.planets[size_t(planet)].system;
    for (int side : {att, def}) {
        if (side >= G.numPlayers || !flagshipAbility(G, side, "kjalengard") || !flagshipIn(G, side, sys)) continue;
        int placed = 0;
        while (placed < 2 && capturedCount(G, side, UnitType::Infantry) > 0 && unitsAvailable(G, side, UnitType::Infantry) > 0) {
            returnCaptured(side, UnitType::Infantry);
            addUnits(side, UnitType::Infantry, 1, sys, planet);
            ++placed;
        }
        if (placed) ds3Event(E, side, "Hulgade's Hammer", pn(side) + "'s fallen return to the fight: " + std::to_string(placed) + " infantry on " + planetName(planet) + ".", sys);
    }
}

// Glory: place a Glory token (from the 3) into sys, or move one there from elsewhere.
void Rules::dsGlory(int p, int sys, bool moveOnly, const std::string& why) {
    if (gloryIn(G, sys)) return;
    if (!moveOnly && G.dsGlory.size() < 3) {
        G.dsGlory.push_back(int16_t(sys));
        ds3Event(E, p, "Glory", pn(p) + " raises a Glory token in " + sysName(sys) + " (" + why + ").", sys);
        return;
    }
    if (G.dsGlory.empty()) return;
    int from = G.dsGlory.front();
    if (G.dsGlory.size() > 1) {
        std::vector<Option> opts;
        for (int16_t g : G.dsGlory) opts.push_back({g, sysName(g), ""});
        from = fpick(p, Ab::DsGlory, "Move which Glory token to " + sysName(sys) + "?", opts, "", true);
    }
    for (int16_t& g : G.dsGlory)
        if (g == from) g = int16_t(sys);
    ds3Event(E, p, "Glory", pn(p) + " moves a Glory token from " + sysName(from) + " to " + sysName(sys) + " (" + why + ").", sys);
}

void Rules::ds3WonCombat(int winner, int loser, int sys, bool space) {
    (void)loser;
    Player& pl = G.players[size_t(winner)];
    // Lachis unlocks (Augurs commander): a combat won where there is a legendary planet or one with a specialty
    if (fac(winner, "augers"))
        for (int16_t x : G.systems[size_t(sys)].planets)
            if (techPlanet(G, x)) pl.dsUnlock = true;
    ds4WonCombat(winner, loser, sys, space);  // batch 4: Reprocessor Alpha
    ds5WonCombat(winner, loser, sys, space);  // batch 5: War Song Implants, Jarl Vel & Jarl Jotrun
    if (!fac(winner, "kjalengard")) return;
    // Glory: a Glory token here - or a strategy token for a unit upgrade of a type that fought
    int types = 0;
    if (space) {
        for (int t : kShipIdx) types |= G.systems[size_t(sys)].space[size_t(winner)].n[t] ? 1 << t : 0;
    } else if (G.groundCombatPlanet >= 0) {
        const Force& f = G.planets[size_t(G.groundCombatPlanet)].units[size_t(winner)];
        for (int t = 0; t < kUnitTypes; ++t) types |= f.n[t] ? 1 << t : 0;
    }
    bool research = false;
    if (pl.strategy > 0)
        for (int t = 0; t < kTechCount && !research; ++t) {
            const int u = upgradeUnitType(t);
            if (u >= 0 && ((types >> u) & 1) && !pl.hasTech(t) && canResearchNow(G, winner, t)) research = true;
        }
    std::vector<Option> opts;
    if (!gloryIn(G, sys)) {
        if (G.dsGlory.size() < 3) opts.push_back({1, "Place a Glory token here", std::to_string(G.dsGlory.size()) + " of 3 on the board"});
        else opts.push_back({1, "Move a Glory token here", ""});
    }
    if (research) opts.push_back({2, "Spend a strategy token: research a unit upgrade", "Of a unit type that fought here."});
    if (opts.empty()) return;
    opts.push_back({-1, "Nothing", ""});
    const int c = fpick(winner, Ab::DsGlory, "Glory: what do you take from this victory?", opts);
    if (c == 1) dsGlory(winner, sys, false, "Glory");
    if (c == 2) {
        spendStrategy(winner, 1);
        onlyUpgradeTypes_ = types;
        ds3Event(E, winner, "Glory", pn(winner) + " spends a strategy token to learn from the battle.", sys);
        researchStep(winner, 0, "Glory");
        onlyUpgradeTypes_ = 0;
    }
}

// Aegis I / II (Dih-Mohn dreadnoughts) and Combat Drills (their note): a ship that would be destroyed is saved.
int Rules::ds3SaveUnits(int p, UnitType t, int k, int sys, int byPlayer) {
    (void)byPlayer;
    if (p < 0 || p >= G.numPlayers || k <= 0) return 0;
    Force& f = G.systems[size_t(sys)].space[size_t(p)];
    int saved = 0;
    const int dm = playerOfFaction(G, "dihmohn");
    if (dm >= 0 && dm != p && holdsNote(G, p, dm, kNoteFaction)) {
        curValue_ = int(t);
        const bool use = fyes(p, Ab::DsCombatDrills, "Play " + pn(dm) + "'s Combat Drills: your " + unitTypeName(t) + " isn't destroyed?",
                              "The note goes face up; it goes back to the Dih-Mohn at the start of the status phase.");
        curValue_ = 0;
        if (use) {
            Player& pl = G.players[size_t(p)];
            const int16_t note = makeNote(dm, kNoteFaction);
            pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), note), pl.notes.end());
            pl.notesInPlay.push_back(note);
            ++saved;
            ds3Event(E, p, "Combat Drills", pn(p) + "'s " + unitTypeName(t) + " survives (Combat Drills).", sys);
        }
    }
    if (saved < k && fac(p, "dihmohn") && t != UnitType::Dreadnought && !std::strncmp(unitDef(G, p, UnitType::Dreadnought).key, "dihmohn_dreadnought", 19)) {
        const bool two = unitDef(G, p, UnitType::Dreadnought).key[19] == '2';  // Aegis II: any game effect; Aegis I: in combat
        if (two || inSpaceCombat_) {
            while (saved < k && f.n[int(UnitType::Dreadnought)] > f.dmg[int(UnitType::Dreadnought)]) {
                curValue_ = int(t);
                const bool use = fyes(p, Ab::DsAegis, std::string("Aegis: one of your dreadnoughts takes the damage so your ") + unitTypeName(t) + " isn't destroyed?", "");
                curValue_ = 0;
                if (!use) break;
                f.dmg[int(UnitType::Dreadnought)] = uint8_t(f.dmg[int(UnitType::Dreadnought)] + 1);
                ++saved;
                ds3Event(E, p, "Aegis", "An Aegis dreadnought shields " + pn(p) + "'s " + unitTypeName(t) + " and takes the damage.", sys);
            }
        }
    }
    return saved;
}

// Heroism (Kjalengard): their fighters and infantry destroyed in combat are captured; Vassalage: so are the holder's fighters.
void Rules::ds3UnitsDestroyed(int p, UnitType t, int k, int sys, int planet, int byPlayer) {
    (void)sys;
    (void)planet;
    (void)byPlayer;
    if ((t != UnitType::Fighter && t != UnitType::Infantry) || !(inSpaceCombat_ || inGroundCombat_) || p < 0 || p >= G.numPlayers) return;
    const int kj = playerOfFaction(G, "kjalengard");
    if (kj < 0) return;
    const bool own = p == kj, vassal = t == UnitType::Fighter && inSpaceCombat_ && ((ds3Vassal_ >> p) & 1);
    if (!own && !vassal) return;
    for (int i = 0; i < k; ++i) G.players[size_t(kj)].captured.push_back(int16_t(kCapturedToken * 16 + int(t)));
    ds3Event(E, kj, own ? "Heroism" : "Vassalage", pn(kj) + " carries " + std::to_string(k) + " fallen " + unitTypeName(t, k > 1) + " off the field (captured).");
}

// ================================================================== tactical actions and turns
void Rules::ds3Activated(int p, int sys) {
    ds3Explored_.clear();
    ds4Activated(p, sys);  // batch 4
    // Read the Fates (the Augurs' note) goes back when its holder activates a system with Augurs units
    const int au = playerOfFaction(G, "augers");
    if (au >= 0 && au != p && hasUnits(G, au, sys)) {
        Player& pl = G.players[size_t(p)];
        const int16_t note = makeNote(au, kNoteFaction);
        if (std::find(pl.notesInPlay.begin(), pl.notesInPlay.end(), note) != pl.notesInPlay.end()) {
            quietNoteReturn_ = true;
            returnNote(p, note);
            quietNoteReturn_ = false;
            say(p, pn(p) + "'s Read the Fates goes back to " + pn(au) + ".");
        }
    }
}

// Secret Maps (Bentor): explore a planet in the active system with their Production unit they didn't explore this action.
void Rules::ds3EndTactical(int p, int sys) {
    if (!fac(p, "bentor")) return;
    std::vector<Option> opts;
    for (int16_t x : G.systems[size_t(sys)].planets) {
        if (!explorable3(G, x) || std::find(ds3Explored_.begin(), ds3Explored_.end(), int(x)) != ds3Explored_.end()) continue;
        bool producer = false;
        const Force& f = G.planets[size_t(x)].units[size_t(p)];
        for (int t = 0; t < kUnitTypes; ++t) producer = producer || (f.n[t] && unitDef(G, p, UnitType(t)).production > 0);
        if (owns(p, "dsbentg") && G.planets[size_t(x)].owner == p && (planetHasSkip(G, x) || !G.planets[size_t(x)].attachments.empty())) producer = true;  // (Broker Network)
        if (producer) opts.push_back({x, planetName(x), ""});
    }
    ds3Explored_.clear();
    if (opts.empty()) return;
    opts.push_back({-1, "Don't", ""});
    const int x = fpick(p, Ab::DsSecretMaps, "Secret Maps: explore a planet here?", opts, "", false, true);
    if (x < 0) return;
    ds3Event(E, p, "Secret Maps", pn(p) + " follows a secret map on " + planetName(x) + ".", sys);
    explorePlanet(p, x);
}

// Sal Gavda (Florzen agent): at the start of a player's turn, one of their attachments moves to a planet next door.
void Rules::ds3TurnStart(int p) {
    const int user = agentUser(p, "florzen");
    if (user < 0) return;
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, p)) {
        const PlanetState& ps = G.planets[size_t(x)];
        if (isHomePlanet(G, p, x) || ps.attachments.empty()) continue;
        bool dest = false;
        for (int16_t a : G.systems[size_t(ps.system)].adj)
            for (int16_t y : G.systems[size_t(a)].planets)
                if (G.planets[size_t(y)].owner == p && !isHomePlanet(G, p, y) && !isMecatol(y, G)) dest = true;
        if (!dest) continue;
        for (size_t i = 0; i < ps.attachments.size(); ++i)
            if (!exKey3(ps.attachments[i], "dmz")) opts.push_back({x * 16 + int(i), std::string(kExploreCards[ps.attachments[i]].name) + " on " + planetName(x), ""});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "Don't", ""});
    agentFor(user, p);
    bool dmz = false;  // 0.43.0 (D70): say why a Demilitarized Zone isn't on the list
    for (int x : controlledPlanets(G, p))
        for (int16_t a : G.planets[size_t(x)].attachments) dmz = dmz || exKey3(a, "dmz");
    const int c = fpick(user, Ab::DsSalGavda, user == p ? "Exhaust Sal Gavda: move one of your attachments to a planet next door?"
                                                         : "Exhaust Sal Gavda: move one of " + pn(p) + "'s attachments to a planet next door?",
                        opts, dmz ? std::string(1, kRedNote) + "The Demilitarized Zone can't be moved." : std::string());
    curFor_ = -1;
    if (c < 0) return;
    const int x = c / 16, i = c % 16;
    std::vector<Option> to;
    for (int16_t a : G.systems[size_t(G.planets[size_t(x)].system)].adj)
        for (int16_t y : G.systems[size_t(a)].planets)
            if (G.planets[size_t(y)].owner == p && !isHomePlanet(G, p, y) && !isMecatol(y, G)) to.push_back({y, planetName(y), ""});
    if (to.empty()) return;
    const int y = to.size() == 1 ? to[0].id : fpick(p, Ab::DsSalGavda, "Sal Gavda: attach it to which planet?", to, "", false, true);
    spendAgent(user, "florzen");
    agentDone(user, p);
    auto& att = G.planets[size_t(x)].attachments;
    const int16_t card = att[size_t(i)];
    att.erase(att.begin() + long(i));
    G.planets[size_t(y)].attachments.push_back(card);
    ds3Event(E, p, "Sal Gavda", kExploreCards[card].name + std::string(" moves from ") + planetName(x) + " to " + planetName(y) + ".");
}

void Rules::ds3TurnEnd(int p) {
    if (G.fixes < 2) ds3AttachNotes(p);  // Encryption Key, Gledge Base (0.43.0 D60: component actions - ds3Options)
    ds3Fragments(p);    // Ancient Blueprints (fragments traded during the turn)
}

// Encryption Key (Bentor) / Gledge Base (Gledge): at the end of their turn the holder may attach it to a non-home planet.
// (0.43.0: "ACTION: attach it" - a component action; only = the one chosen there)
void Rules::ds3AttachNotes(int p, const char* only) {
    for (const char* key : {"bentor", "gledge"}) {
        if (only && std::strcmp(only, key)) continue;
        const int o = playerOfFaction(G, key);
        if (o < 0 || o == p || !holdsNote(G, p, o, kNoteFaction)) continue;
        const bool bentor = !std::strcmp(key, "bentor");
        if (bentor ? G.dsKeyPlanet >= 0 : G.dsBasePlanet >= 0) continue;
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p))
            if (!isHomePlanet(G, p, x) && !isDMZ(G, x)) opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + " res"});
        if (opts.empty()) continue;
        opts.push_back({-1, only ? "Cancel" : "Not now", ""});
        const int x = fpick(p, bentor ? Ab::DsEncryptionKey : Ab::DsGledgeBase,
                            bentor ? "Encryption Key: attach it to a planet (a technology specialty of any colour)?" : "Gledge Base: attach it to a planet (+2 resources)?",
                            opts, "", false, true);
        if (x < 0) continue;
        Player& pl = G.players[size_t(p)];
        const int16_t note = makeNote(o, kNoteFaction);
        pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), note), pl.notes.end());
        if (bentor) {
            std::vector<Option> cols;
            for (int c = 0; c < 4; ++c) cols.push_back({c, colorName(TechColor(c)), ""});
            G.dsKeyColor = int8_t(fpick(p, Ab::DsEncryptionKey, "Encryption Key: which colour of specialty?", cols));
            G.dsKeyPlanet = int16_t(x);
            ds3Event(E, p, "Encryption Key", pn(p) + " attaches the Encryption Key to " + planetName(x) + ": a " + colorName(TechColor(G.dsKeyColor)) +
                                                 " technology specialty.", G.planets[size_t(x)].system);
        } else {
            G.dsBasePlanet = int16_t(x);
            ds3Event(E, p, "Gledge Base", pn(p) + " attaches the Gledge Base to " + planetName(x) + ": +2 resources.", G.planets[size_t(x)].system);
        }
    }
}

// Ancient Blueprints (Bentor): the first fragment of each kind puts a Fragment token on their sheet (Auctioneer: a mech).
void Rules::ds3Fragments(int p) {
    if (p < 0 || p >= G.numPlayers || !fac(p, "bentor")) return;
    Player& pl = G.players[size_t(p)];
    for (int16_t c : std::vector<int16_t>(pl.fragments)) {
        if (!isRelicFragment(c)) continue;
        const int bit = 1 << int(kExploreCards[c].type);
        if (pl.dsFrags & bit) continue;
        pl.dsFrags = uint8_t(pl.dsFrags | bit);
        ds3Event(E, p, "Ancient Blueprints", pn(p) + " places a " + exploreTypeName(kExploreCards[c].type) + " Fragment token on their sheet (" +
                                                std::to_string(bits(pl.dsFrags)) + " of 4).");
        // Auctioneer (Bentor mech): Deploy - a mech on a planet they control
        if (mechAbilities(G) && unitsAvailable(G, p, UnitType::Mech) > 0) {
            std::vector<Option> opts;
            for (int x : controlledPlanets(G, p))
                if (!isDMZ(G, x)) opts.push_back({x, planetName(x), ""});
            if (opts.empty()) continue;
            opts.push_back({-1, "Don't", ""});
            const int x = fpick(p, Ab::DsAuctioneer, "Auctioneer: place a mech on one of your planets?", opts, "", false, true);
            if (x >= 0) {
                addUnits(p, UnitType::Mech, 1, G.planets[size_t(x)].system, x);
                say(p, pn(p) + " places a mech on " + planetName(x) + " (Auctioneer).", G.planets[size_t(x)].system);
            }
        }
    }
}

// ================================================================== exploring
void Rules::ds3Explored(int p, int planet) {
    if (planet < 0 || p < 0 || p >= G.numPlayers) return;
    Player& pl = G.players[size_t(p)];
    const int sys = G.planets[size_t(planet)].system;
    if (fac(p, "bentor")) {
        ds3Explored_.push_back(planet);
        if (!pl.dsOnce && pl.commodities < commodityCap(G, pl)) {  // Fortune Seekers: once per action, 1 commodity
            pl.dsOnce = true;
            { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + 1); commodityGained(p, cb_); }
            ds3Event(E, p, "Fortune Seekers", pn(p) + " gains 1 commodity.", sys);
        }
        ds3Fragments(p);
    }
    // Clodho (Augurs agent): a legendary planet or one with a specialty - 2 trade goods for the explorer
    if (techPlanet(G, planet)) {
        const int user = agentUser(p, "augers");
        if (user >= 0) {
            agentFor(user, p);
            const bool use = fyes(user, Ab::DsClodho, user == p ? "Exhaust Clodho: gain 2 trade goods?" : "Exhaust Clodho so " + pn(p) + " gains 2 trade goods?",
                                  leaderText(findLeader("augersagent")));
            curFor_ = -1;
            if (use) {
                spendAgent(user, "augers");
                agentDone(user, p);
                gainTG(p, 2);
                say(p, pn(p) + " gains 2 trade goods (Clodho).", sys);
            }
        }
    }
    // Impressment Programs (Dih-Mohn): produce 1 ship in that system
    if (owns(p, "dsdihmy") && !otherShipsIn(G, p, sys) &&
        fyes(p, Ab::DsImpressment, "Impressment Programs: produce 1 ship in " + sysName(sys) + "?", "")) {
        dsProduceOne(p, sys, 0, "Impressment Programs");
    }
    ds4Explored(p, planet);  // batch 4: Plunder
    ds5Explored(p, planet);  // batch 5: the Lord
}

void Rules::dsFrontierExplored(int p, int sys) {
    if (p < 0 || p >= G.numPlayers) return;
    Player& pl = G.players[size_t(p)];
    if (fac(p, "bentor")) {
        if (!pl.dsOnce && pl.commodities < commodityCap(G, pl)) {  // Fortune Seekers
            pl.dsOnce = true;
            { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + 1); commodityGained(p, cb_); }
            ds3Event(E, p, "Fortune Seekers", pn(p) + " gains 1 commodity.", sys);
        }
        ds3Fragments(p);
    }
    // Migrant Fleet (Dih-Mohn): then explore one of their planets next to that system
    if (fac(p, "dihmohn")) {
        std::vector<Option> opts;
        for (int16_t a : G.systems[size_t(sys)].adj)
            for (int16_t x : G.systems[size_t(a)].planets)
                if (G.planets[size_t(x)].owner == p && explorable3(G, x)) opts.push_back({x, planetName(x), ""});
        if (opts.empty()) return;
        opts.push_back({-1, "Don't", ""});
        const int x = fpick(p, Ab::DsMigrant, "Migrant Fleet: explore one of your planets next to that system?", opts, "", false, true);
        if (x < 0) return;
        ds3Event(E, p, "Migrant Fleet", pn(p) + "'s migrants settle on " + planetName(x) + ".", sys);
        explorePlanet(p, x);
    }
}

// Before p explores planet with deck 'type': Deep Mining (Gledge) - a trade good instead (-2); Black Markets (Florzen) -
// the deck of another of their planets' traits.
int Rules::dsExploreDeck(int p, int planet, int type) {
    if (p < 0 || p >= G.numPlayers) return type;
    const PlanetState& ps = G.planets[size_t(planet)];
    if (fac(p, "gledge") && (ps.units[size_t(p)].count(UnitType::Mech) || ps.units[size_t(p)].structures())) {
        curValue_ = int(G.exploreDeck[size_t(type)].size() + G.exploreDiscard[size_t(type)].size());
        const bool use = fyes(p, Ab::DsDeepMining, "Deep Mining: gain 1 trade good instead of exploring " + planetName(planet) + "?", "");
        curValue_ = 0;
        if (use) {
            gainTG(p, 1);
            ds3Event(E, p, "Deep Mining", pn(p) + " mines " + planetName(planet) + " for 1 trade good instead of exploring it.", ps.system);
            return -2;
        }
    }
    if (fac(p, "florzen")) {
        int mask = 0;
        for (int x : controlledPlanets(G, p)) {
            if (x == planet) continue;
            for (Trait t : {Trait::Cultural, Trait::Hazardous, Trait::Industrial})
                if (hasTraitOf(G, x, t) && traitDeck(t) >= 0) mask |= 1 << traitDeck(t);
        }
        mask &= ~(1 << type);
        if (mask) {
            std::vector<Option> opts{{type, std::string("Keep: ") + exploreTypeName(ExploreType(type)), ""}};
            for (int d = 0; d < 3; ++d)
                if ((mask >> d) & 1) opts.push_back({d, exploreTypeName(ExploreType(d)), "As one of your other planets' trait."});
            const int d = fpick(p, Ab::DsBlackMarkets, "Shadow Markets: explore " + planetName(planet) + " with which deck?", opts);
            if (d != type) ds3Event(E, p, "Shadow Markets", pn(p) + " explores " + planetName(planet) + " as a " + exploreTypeName(ExploreType(d)) + " planet.", ps.system);
            return d;
        }
    }
    return type;
}

// Data Leak (Florzen) / Underground Market (their note): draw 1 more relic, keep 1, shuffle the other back.
int Rules::dsRelicDraw(int p, int r) {
    if (p < 0 || p >= G.numPlayers || G.relicDeck.empty()) return r;
    const int fz = playerOfFaction(G, "florzen");
    const bool own = fac(p, "florzen");
    if (!own) {
        if (fz < 0 || !holdsNote(G, p, fz, kNoteFaction)) return r;
        if (!fyes(p, Ab::DsUnderground, "Play " + pn(fz) + "'s Underground Market: draw 1 more relic and keep one?", "The note goes back.")) return r;
        returnNote(p, makeNote(fz, kNoteFaction));
    }
    const int r2 = G.relicDeck.back();
    G.relicDeck.pop_back();
    E.revealed();
    std::vector<Option> opts{{r, kRelics[r].name, relicText(r)}, {r2, kRelics[r2].name, relicText(r2)}};
    const int keep = fpick(p, Ab::DsDataLeak, own ? "Data Leak: which relic do you keep?" : "Underground Market: which relic do you keep?", opts);
    G.relicDeck.push_back(int16_t(keep == r ? r2 : r));
    E.rng().shuffle(G.relicDeck);
    ds3Event(E, p, own ? "Data Leak" : "Underground Market", pn(p) + " looks at 2 relics and keeps one; the other is shuffled back.");
    return keep;
}

// ================================================================== objectives and phases
// Oracle AI (Augurs): after each reveal they put the next unrevealed public objective on their sheet (Limited Vision: a
// stage II one only once every stage I is out) and know it; Probability Algorithms: it is the next one revealed.
void Rules::dsObjectiveRevealed() {
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!fac(p, "augers")) continue;
        Player& pl = G.players[size_t(p)];
        const std::vector<int16_t>& deck = G.stage1Revealed < 5 ? G.stage1Deck : G.stage2Deck;
        if (deck.empty()) {
            pl.dsSheet = -1;
            continue;
        }
        pl.dsSheet = deck.front();
        ds3Event(E, p, "Oracle AI", pn(p) + " places an unrevealed public objective on their faction sheet.");
        if (pl.human) feedNote(p, std::string("Oracle AI: the next public objective will be ") + kObjectives[pl.dsSheet].name + " - " + objectiveSummary(pl.dsSheet));
    }
}

// Psychographics (Augurs): in the status phase, a second public objective instead of a secret one.
bool Rules::dsPsychographics(int p) {
    if (!owns(p, "dsaugug")) return false;
    std::vector<Option> opts;
    for (int o : scorablePublic(p)) opts.push_back({o, std::string(kObjectives[o].name) + " - " + std::to_string(kObjectives[o].points) + " VP", objectiveSummary(o)});
    if (opts.empty()) return false;
    opts.push_back({-1, "No - score a secret objective as usual", ""});
    const int o = fpick(p, Ab::DsPsychographics, "Psychographics: score another public objective instead of a secret one?", opts);
    if (o < 0) return false;
    ds3Event(E, p, "Psychographics", pn(p) + " reads the galaxy's mood: a second public objective this round.");
    scoreObjective(p, o);
    return true;
}

void Rules::ds3StatusStart() {
    // Combat Drills goes back to the Dih-Mohn
    const int dm = playerOfFaction(G, "dihmohn");
    if (dm >= 0)
        for (int q = 0; q < G.numPlayers; ++q) {
            const int16_t note = makeNote(dm, kNoteFaction);
            auto& v = G.players[size_t(q)].notesInPlay;
            if (q != dm && std::find(v.begin(), v.end(), note) != v.end()) {
                quietNoteReturn_ = true;
                returnNote(q, note);
                quietNoteReturn_ = false;
                say(q, pn(q) + "'s Combat Drills goes back to " + pn(dm) + ".");
            }
        }
    // Sentient Datapool (Augurs): 3 trade goods for a technology
    for (int p : initiativeOrder(G)) {
        if (!owns(p, "dsauguy") || G.players[size_t(p)].tradeGoods < 3) continue;
        if (!fyes(p, Ab::DsDatapool, "Sentient Datapool: spend 3 trade goods to research a technology?", "")) continue;
        if (!researchStep(p, 0, "Sentient Datapool")) continue;
        Player& pl = G.players[size_t(p)];
        pl.tradeGoods = int8_t(std::max(0, pl.tradeGoods - 3));
        ds3Event(E, p, "Sentient Datapool", pn(p) + " spends 3 trade goods on research.");
    }
    for (int p = 0; p < G.numPlayers; ++p) ds3Fragments(p);
}

// Quaxdol Junitas (Florzen commander): after the second agenda is revealed, ready a planet and explore it.
void Rules::ds3AgendaRevealed() {
    if (++ds3Agendas_ != 2) return;
    for (int p : initiativeOrder(G)) {
        if (!commanderUsable(G, p, "florzen")) continue;
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p))
            opts.push_back({x, planetName(x), std::string(G.planets[size_t(x)].exhausted ? "Exhausted" : "Ready") + (explorable3(G, x) ? ", can be explored" : "")});
        if (opts.empty()) continue;
        opts.push_back({-1, "Don't", ""});
        const int x = fpick(p, Ab::DsQuaxdol, "Quaxdol Junitas: ready one of your planets (and explore it)?", opts, "", false, true);
        if (x < 0) continue;
        G.planets[size_t(x)].exhausted = false;
        ds3Event(E, p, "Quaxdol Junitas", pn(p) + " readies " + planetName(x) + ".", G.planets[size_t(x)].system);
        if (explorable3(G, x)) explorePlanet(p, x);
    }
}

void Rules::ds3AgendaResolved(int outcome, const std::vector<int>& voted) {
    for (int p = 0; p < G.numPlayers; ++p) {  // Quaxdol Junitas unlocks: the outcome they voted for
        if (!fac(p, "florzen") || outcome < 0) continue;
        bool right = size_t(p) < voted.size() && voted[size_t(p)] == outcome;
        if (G.fixes >= 2) {  // 0.43.0 (D71): "or predicted" - a rider (action card, Kyro, Edyn) on the outcome counts too
            const std::vector<int>& pr = predictions_[p];
            for (size_t i = 0; i + 1 < pr.size(); i += 2) right = right || pr[i + 1] == outcome;
            right = right || dsRiderGuess_[p] == outcome || dsEdynRider_[p] == outcome;
        }
        if (right) G.players[size_t(p)].dsUnlock = true;
    }
}

// Lachis (Augurs commander): casting votes, 1 more for every 2 technologies they own.
void Rules::ds3AfterVoting(std::vector<int>& voted, std::vector<int>& counts) {
    for (int p = 0; p < G.numPlayers; ++p) {
        if (size_t(p) >= voted.size() || size_t(p) >= counts.size() || voted[size_t(p)] == kNoVote || counts[size_t(p)] <= 0) continue;
        if (!commanderUsable(G, p, "augers")) continue;
        const int more = int(G.players[size_t(p)].techs.size()) / 2;
        if (more <= 0) continue;
        counts[size_t(p)] += more;
        ds3Event(E, p, "Lachis", pn(p) + " casts " + std::to_string(more) + " more votes.");
    }
}

// When p passes: Heroism (captured units for command tokens), Skald (infantry near a Glory token).
void Rules::ds3Passed(int p) {
    if (!fac(p, "kjalengard")) return;
    Player& pl = G.players[size_t(p)];
    for (size_t i = 0; i < G.dsGlory.size(); ++i) {
        if (pl.captured.size() < 4 || reinforcementTokens(p) <= 0) break;
        curValue_ = int(pl.captured.size());
        const bool use = fyes(p, Ab::DsHeroism, "Heroism: return 4 captured units to gain 1 command token?",
                              std::to_string(pl.captured.size()) + " captured; once for each Glory token on the board.");
        curValue_ = 0;
        if (!use) break;
        for (int k = 0; k < 4 && !pl.captured.empty(); ++k) pl.captured.pop_back();
        ds3Event(E, p, "Heroism", pn(p) + " honours 4 of the fallen.");
        gainTokens(p, 1, false, "Heroism: 1 command token.");
    }
    if (!mechAbilities(G)) return;
    for (size_t x = 0; x < G.planets.size(); ++x) {  // Skald (Kjalengard mech)
        const int m = G.planets[x].units[size_t(p)].count(UnitType::Mech);
        if (!m || G.planets[x].system < 0 || !gloryNear(G, G.planets[x].system)) continue;
        const int k = std::min(m, unitsAvailable(G, p, UnitType::Infantry));
        if (k <= 0) continue;
        addUnits(p, UnitType::Infantry, k, G.planets[x].system, int(x));
        ds3Event(E, p, "Skald", "The Skald's song rallies " + std::to_string(k) + " infantry on " + planetName(int(x)) + ".", G.planets[x].system);
    }
}

void Rules::ds3Researched(int p, int t) {
    // Clona Bathru unlocks (Dih-Mohn commander): a ship unit upgrade
    const int u = upgradeUnitType(t);
    if (fac(p, "dihmohn") && u >= 0 && isShip(UnitType(u))) G.players[size_t(p)].dsUnlock = true;
    // Iledrith (Augurs mech): Deploy - a mech on a legendary planet or one with a specialty
    if (fac(p, "augers") && mechAbilities(G) && unitsAvailable(G, p, UnitType::Mech) > 0) {
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p))
            if (techPlanet(G, x) && !isDMZ(G, x)) opts.push_back({x, planetName(x), ""});
        if (opts.empty()) return;
        opts.push_back({-1, "Don't", ""});
        const int x = fpick(p, Ab::DsIledrith, "Iledrith: place a mech on one of your legendary or specialty planets?", opts, "", false, true);
        if (x < 0) return;
        addUnits(p, UnitType::Mech, 1, G.planets[size_t(x)].system, x);
        say(p, pn(p) + " places a mech on " + planetName(x) + " (Iledrith).", G.planets[size_t(x)].system);
    }
}

// ================================================================== production, movement, invasion, unit rolls
int Rules::dsProductionBonus(int p, int sys, int capacity) {
    (void)sys;
    int bonus = 0;
    curValue_ = spendable(G, p, false) - capacity;  // (for the AI: how much more could be paid for)
    const int user = agentUser(p, "gledge");
    if (user >= 0) {  // Durran (Gledge agent): +3
        agentFor(user, p);
        const bool use = fyes(user, Ab::DsDurran, user == p ? "Exhaust Durran: +3 to the Production of these units?" : "Exhaust Durran: +3 to " + pn(p) + "'s Production now?",
                              leaderText(findLeader("gledgeagent")));
        curFor_ = -1;
        if (use) {
            spendAgent(user, "gledge");
            agentDone(user, p);
            bonus += 3;
            say(p, pn(p) + "'s units produce 3 more (Durran).", sys);
        }
    }
    if (techReady(p, "dsbenty") && G.fixes >= 2) {  // Merged Replicators (Bentor, 0.43.0 D59: the whole card)
        // +2, or swap the Production of one of these units with another unit's on the board (the best one each way)
        auto per = [&](int q, int s2, int planet, int t) {
            const UnitDef& d = unitDef(G, q, UnitType(t));
            if (d.production <= 0 || (planet < 0 && isStructure(UnitType(t)) && !d.spaceOnly)) return 0;
            return d.production + (d.productionPlusResources && planet >= 0 ? planetResources(G, planet) : 0);
        };
        int mine = 99, mineT = -1;  // the weakest producer here
        auto scanMine = [&](const Force& f, int planet) {
            for (int t = 0; t < kUnitTypes; ++t)
                if (f.n[t] && per(p, sys, planet, t) > 0 && per(p, sys, planet, t) < mine) mine = per(p, sys, planet, t), mineT = t;
        };
        scanMine(G.systems[size_t(sys)].space[size_t(p)], -1);
        for (int16_t x : G.systems[size_t(sys)].planets) scanMine(G.planets[size_t(x)].units[size_t(p)], x);
        int best = 0, bestQ = -1, bestS = -1, bestT = -1;  // the strongest producer elsewhere
        for (size_t s2 = 0; s2 < G.systems.size() && mineT >= 0; ++s2)
            for (int q = 0; q < G.numPlayers; ++q) {
                auto scan = [&](const Force& f, int planet) {
                    for (int t = 0; t < kUnitTypes; ++t)
                        if (f.n[t] && (int(s2) != sys || q != p) && per(q, int(s2), planet, t) > best)
                            best = per(q, int(s2), planet, t), bestQ = q, bestS = int(s2), bestT = t;
                };
                scan(G.systems[s2].space[size_t(q)], -1);
                for (int16_t x : G.systems[s2].planets) scan(G.planets[size_t(x)].units[size_t(q)], x);
            }
        std::vector<Option> opts = {{2, "+2 Production", "To these units' total."}};
        if (mineT >= 0 && best - mine > 2)
            opts.push_back({100 + best - mine, "Match Production: your " + std::string(unitTypeName(UnitType(mineT))) + " (" + std::to_string(mine) + ") as " +
                                                   pn(bestQ) + "'s " + unitTypeName(UnitType(bestT)) + " in " + sysName(bestS) + " (" + std::to_string(best) + ")",
                            "+" + std::to_string(best - mine) + " this time."});
        opts.push_back({-1, "Don't", ""});
        const int c = fpick(p, Ab::DsReplicators, "Exhaust Merged Replicators?", opts, "When your units use Production: one of them matches the best Production on the board, or +2.");
        if (c >= 0) {
            exhaustTech(p, "dsbenty");
            const int more = c >= 100 ? c - 100 : 2;
            bonus += more;
            say(p, pn(p) + "'s units produce " + std::to_string(more) + " more (Merged Replicators" + (c >= 100 ? ", matched" : "") + ").", sys);
        }
    } else if (techReady(p, "dsbenty") && fyes(p, Ab::DsReplicators, "Exhaust Merged Replicators: +2 to the Production of these units?", "")) {  // Bentor
        exhaustTech(p, "dsbenty");
        bonus += 2;
        say(p, pn(p) + "'s units produce 2 more (Merged Replicators).", sys);
    }
    curValue_ = 0;
    bonus += ds5ProductionBonus(p, sys);  // Industry Secrets (batch 5)
    bonus += ds7ProductionBonus(p, sys);  // Becece (batch 6)
    return bonus;
}

// Privateer (Florzen mech): the mechs being produced may be paid for with influence. Returns the influence part.
int Rules::dsPrivateer(int p, const int counts[], int cost) {
    if (!fac(p, "florzen") || !mechAbilities(G)) return 0;
    const int mechCost = std::min(cost, (counts[int(UnitType::Mech)] * unitCost2(G, p, UnitType::Mech) + 1) / 2);
    std::vector<Option> opts;
    for (int k = mechCost; k >= 1; --k)
        if (canPayBoth(G, p, cost - k, k)) opts.push_back({k, std::to_string(k) + " influence", std::to_string(cost - k) + " resources for the rest"});
    if (opts.empty()) return 0;
    opts.push_back({0, "No - all in resources", ""});
    curValue_ = cost;
    const int k = fpick(p, Ab::DsPrivateer, "Privateer: pay for the mechs with influence?", opts);
    curValue_ = 0;
    if (k > 0) say(p, pn(p) + " pays " + std::to_string(k) + " influence for its mechs (Privateer).");
    return std::max(0, k);
}

// Blackmail Programs (Florzen): before another player's strategic action - their turn ends, the card stays unused.
bool Rules::dsBlackmail(int p, int card) {
    for (int f = 0; f < G.numPlayers; ++f) {
        if (f == p || !techReady(f, "dsflorg") || G.players[size_t(f)].strategy <= 0) continue;
        curValue_ = card * 16 + p;
        const bool use = fyes(f, Ab::DsBlackmail, "Blackmail Programs: end " + pn(p) + "'s turn before their " + strategyName(card) + " strategic action?",
                              "It costs a token from your strategy pool; their strategy card stays unused.");
        curValue_ = 0;
        if (!use) continue;
        exhaustTech(f, "dsflorg");
        spendStrategy(f, 1);
        ds3Event(E, f, "Blackmail Programs", pn(f) + " produces compromising files: " + pn(p) + "'s turn ends without the " + strategyName(card) + " strategic action.");
        return true;
    }
    return false;
}

// After a unit ability roll (barrage, space cannon, bombardment): Wayfinder (+1 per Fragment token), Beg Bersha (more
// bombardment dice), Orion Platform I / II (their planets are explored).
void Rules::dsUnitRolled(int roller, int target, int sys, RollResult& r, const char* what) {
    if (sys < 0) return;
    const bool flagDice = std::find(r.unit.begin(), r.unit.end(), int8_t(UnitType::Flagship)) != r.unit.end();
    if (flagDice && fac(roller, "bentor") && bits(G.players[size_t(roller)].dsFrags) > 0) {  // Wayfinder
        const int n = bits(G.players[size_t(roller)].dsFrags);
        for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i)
            if (r.unit[i] == int(UnitType::Flagship)) r.need[i] = int8_t(r.need[i] - n);
        recount(r);
        say(roller, pn(roller) + "'s Wayfinder rolls +" + std::to_string(n) + " (its Fragment tokens): " + std::to_string(r.hits) + " hits now.", sys);
    }
    if (flagDice && !std::strcmp(what, "bombardment") && flagshipAbility(G, roller, "gledge")) {  // Beg Bersha
        const int n = mechsNear(G, sys);
        int need = 10;
        for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i)
            if (r.unit[i] == int(UnitType::Flagship)) need = r.need[i];
        if (n > 0) {
            addDice(r, n, need, int(UnitType::Flagship));
            recount(r);
            reportRoll(roller, sys, "Beg Bersha (" + std::to_string(n) + " more dice)", r);
        }
    }
    // Orion Platform I (natural 9 or 10) / II (a hit) against another player's units: explore its planet
    ds4UnitRolled(roller, target, sys, r, what);  // batch 4: Aurum Vadra, Suldhan Wraeg
    if (!std::strcmp(what, "space cannon") && fac(roller, "gledge") && target >= 0 && target != roller) {
        const bool two = !std::strcmp(unitDef(G, roller, UnitType::PDS).key, "gledge_pds2");
        int k = 0;
        for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i)
            if (r.unit[i] == int(UnitType::PDS) && (two ? r.dice[i] >= r.need[i] : r.dice[i] >= 9)) ++k;
        std::vector<int> planets;
        auto collect = [&](int s) {
            for (int16_t x : G.systems[size_t(s)].planets)
                if (G.planets[size_t(x)].units[size_t(roller)].count(UnitType::PDS) && explorable3(G, x)) planets.push_back(x);
        };
        collect(sys);
        if (two)
            for (int16_t a : G.systems[size_t(sys)].adj) collect(a);
        for (int i = 0; i < k && i < int(planets.size()); ++i) {
            ds3Event(E, roller, two ? "Orion Platform II" : "Orion Platform", pn(roller) + "'s Orion Platform scans " + planetName(planets[size_t(i)]) + ".", sys);
            explorePlanet(roller, planets[size_t(i)]);
        }
    }
}

// Maximus (Dih-Mohn flagship): after it moves into the active system, produce up to 2 units with a combined cost of 4 or less.
void Rules::ds3AfterMovement(int p, int sys, bool flagshipMoved) {
    if (!flagshipMoved || !flagshipAbility(G, p, "dihmohn") || !flagshipIn(G, p, sys)) return;
    if (!fyes(p, Ab::DsMaximus, "Maximus: produce up to 2 units here with a combined cost of 4 or less?", "")) return;
    ds3Event(E, p, "Maximus", pn(p) + "'s Maximus opens its foundry bays.", sys);
    dsCostCap_ = 4;
    const bool na = notProductionAbility_;  // (0.58.2, fixes 21: "produce", not PRODUCTION - no Sarween Tools)
    if (G.fixes >= 21) notProductionAbility_ = true;
    productionCore(p, sys, 2, false, false);
    notProductionAbility_ = na;
    dsCostCap_ = 0;
}

// Jgin Faru (Dih-Mohn agent): when a player commits units to a planet, 1 more infantry from their reinforcements.
void Rules::dsLanded(int p, int sys, const std::vector<int>& landed) {
    // Plague Reservoir (Kyro, 0.43.0 D13): once per action, a ground combat on a planet of the invasion with nobody to
    // fight - theirs to win (Dacxive Animators: 1 more infantry there)
    if (G.fixes >= 2 && fac(p, "kyro") && !(dsOncePerAction_ & 4) && owns(p, "dxa") && unitsAvailable(G, p, UnitType::Infantry) > 0) {
        std::vector<Option> opts;
        for (int x : landed) {
            bool foes = neutralGroundOn(G, x) > 0;
            for (int q = 0; q < G.numPlayers; ++q) foes = foes || (q != p && groundForcesOn(G, q, x) > 0);
            if (!foes && !isDMZ(G, x)) opts.push_back({x, planetName(x), "You win it: Dacxive Animators places 1 infantry."});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            const int x = fpick(p, Ab::DsPlague, "Plague Reservoir: fight a ground combat with nobody there?", opts,
                                "Once per action, during an invasion on a planet with your units.", false, true);
            if (x >= 0) {
                dsOncePerAction_ = uint8_t(dsOncePerAction_ | 4);
                ds3Event(E, p, "Plague Reservoir", pn(p) + "'s plague sweeps " + planetName(x) + ": a ground combat won against nobody.", sys);
                addUnits(p, UnitType::Infantry, 1, sys, x);
                say(p, pn(p) + " places 1 infantry on " + planetName(x) + " (Dacxive Animators).", sys);
            }
        }
    }
    ds4Landed(p, sys, landed);  // batch 4: Duuban
    ds6Landed(p, sys, landed);  // batch 6: Minefields
    ds7Landed(p, sys, landed);  // Orbital Defense Grid
    const int user = agentUser(p, "dihmohn");
    if (user < 0) return;
    for (int pl : landed) {
        if (unitsAvailable(G, p, UnitType::Infantry) <= 0) return;
        int defenders = neutralGroundOn(G, pl);
        for (int q = 0; q < G.numPlayers; ++q) defenders += q != p ? G.planets[size_t(pl)].units[size_t(q)].groundForces() : 0;
        agentFor(user, p);
        curValue_ = defenders;
        const bool use = fyes(user, Ab::DsJginFaru, user == p ? "Exhaust Jgin Faru: 1 more infantry from your reinforcements on " + planetName(pl) + "?"
                                                              : "Exhaust Jgin Faru: 1 more of " + pn(p) + "'s infantry on " + planetName(pl) + "?",
                              leaderText(findLeader("dihmohnagent")));
        curValue_ = 0;
        curFor_ = -1;
        if (!use) continue;
        spendAgent(user, "dihmohn");
        agentDone(user, p);
        addUnits(p, UnitType::Infantry, 1, sys, pl);
        say(p, pn(p) + " lands 1 more infantry on " + planetName(pl) + " (Jgin Faru).", sys);
        return;
    }
}

// ================================================================== component actions: abilities, agents, heroes
void Rules::ds3Options(int p, std::vector<Option>& o) {
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    const Player& pl = G.players[size_t(p)];
    if (fac(p, "gledge") && G.dsCores.size() < 3) {
        bool any = false;
        for (int x : controlledPlanets(G, p))
            any = any || (!isHomePlanet(G, p, x) && !isMecatol(x, G) && std::find(G.dsCores.begin(), G.dsCores.end(), int16_t(x)) == G.dsCores.end());
        if (any) add(Ab::DsMantle, "Mantle Cracking", "Put a Core token on one of your non-home planets (2 resources, 0 influence from now on) and gain 4 trade goods.");
    }
    if (canUseAgent(p, "bentor") && fac(p, "bentor") && bits(pl.dsFrags) > 0)
        add(Ab::DsMgur, "Agent: C.O.O. Mgur", "Exhaust: a player gains 1 commodity for each of your Fragment tokens (beyond their limit: trade goods).");
    if (fac(p, "bentor") && heroUnlocked(G, p)) add(Ab::DsKenTucc, "Hero: C.E.O. Ken Tucc", "Explore each planet you control. Purged afterwards.");
    if (G.fixes >= 2)  // Encryption Key / Gledge Base (0.43.0 D60): "ACTION: attach this card to a non-home planet you control"
        for (const char* key : {"bentor", "gledge"}) {
            const int o = playerOfFaction(G, key);
            const bool bentor = !std::strcmp(key, "bentor");
            if (o < 0 || o == p || !holdsNote(G, p, o, kNoteFaction) || (bentor ? G.dsKeyPlanet >= 0 : G.dsBasePlanet >= 0)) continue;
            bool any = false;
            for (int x : controlledPlanets(G, p)) any = any || (!isHomePlanet(G, p, x) && !isDMZ(G, x));
            if (any)
                add(bentor ? Ab::DsEncryptionKey : Ab::DsGledgeBase, bentor ? "Encryption Key" : "Gledge Base",
                    bentor ? "Attach it to a non-home planet of yours: a technology specialty of any colour there."
                           : "Attach it to a non-home planet of yours: +2 resources there.");
        }
    if (techReady(p, "dsdihmy")) {
        bool any = false;
        for (int x : ti::readyPlanets(G, p)) any = any || explorable3(G, x);
        if (any) add(Ab::DsImpressAction, "Impressment Programs", "Exhaust it and one of your planets to explore that planet.");
    }
    if (fac(p, "gledge") && heroUnlocked(G, p)) add(Ab::DsGorthrim, "Hero: Gorthrim", "Reveal exploration cards until 3 attachments; attach them to your planets of that type. Purged afterwards.");
    if (fac(p, "augers") && heroUnlocked(G, p) && pl.dsSheet >= 0)
        add(Ab::DsAtropha, "Hero: Atropha", "Draw 3 public objectives; one may replace the objective on your sheet (the next one revealed). Purged afterwards.");
    if (fac(p, "kjalengard") && heroUnlocked(G, p) && !G.dsGlory.empty())
        add(Ab::DsYgegnad, "Hero: Ygegnad, The Thunder", "For each Glory token: take back 1 of your command tokens in or next to its system and gain 1. Purged afterwards.");
    if (fac(p, "florzen") && heroUnlocked(G, p))
        add(Ab::DsBanua, "Hero: Banua Gowen", "2 fighters in each system with your planets and no other ships; an attachment may move to your home. Purged afterwards.");
    ds4Options(p, o);  // batch 4
    ds5Options(p, o);  // batch 5
}

bool Rules::ds3Action(int p, Ab ab) {
    Player& pl = G.players[size_t(p)];
    switch (ab) {
        case Ab::DsEncryptionKey:
        case Ab::DsGledgeBase: {  // 0.43.0 (D60): attached as a component action
            const bool bentor = ab == Ab::DsEncryptionKey;
            const int before = bentor ? G.dsKeyPlanet : G.dsBasePlanet;
            ds3AttachNotes(p, bentor ? "bentor" : "gledge");
            return (bentor ? G.dsKeyPlanet : G.dsBasePlanet) != before;
        }
        case Ab::DsMantle: {
            if (!fac(p, "gledge") || G.dsCores.size() >= 3) return false;
            std::vector<Option> opts;
            for (int x : controlledPlanets(G, p))
                if (!isHomePlanet(G, p, x) && !isMecatol(x, G) && std::find(G.dsCores.begin(), G.dsCores.end(), int16_t(x)) == G.dsCores.end())
                    opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + "/" + std::to_string(planetInfluence(G, x))});
            if (opts.empty()) return false;
            const int x = fpick(p, Ab::DsMantle, "Mantle Cracking: crack which planet's mantle?", opts, "Its base values become 2 resources and 0 influence for good.", false, true);
            G.dsCores.push_back(int16_t(x));
            gainTG(p, 4);
            ds3Event(E, p, "Mantle Cracking", pn(p) + " cracks the mantle of " + planetName(x) + " and gains 4 trade goods.", G.planets[size_t(x)].system);
            return true;
        }
        case Ab::DsMgur: {
            if (!canUseAgent(p, "bentor")) return false;
            const int n = bits(pl.dsFrags);
            std::vector<Option> who;
            for (int q = 0; q < G.numPlayers; ++q)
                if (!G.players[size_t(q)].eliminated) who.push_back({q, pn(q), ""});
            const int q = fpick(p, Ab::DsMgur, "C.O.O. Mgur: which player gains " + std::to_string(n) + " commodities?", who);
            spendAgent(p, "bentor");
            Player& ql = G.players[size_t(q)];
            const int room = std::max(0, commodityCap(G, ql) - ql.commodities), c = std::min(n, room);
            { const int cb_ = ql.commodities; ql.commodities = int8_t(ql.commodities + c); commodityGained(q, cb_); }
            if (n - c > 0) gainTG(q, n - c);
            ds3Event(E, p, "C.O.O. Mgur", pn(q) + " gains " + std::to_string(c) + " commodities" + (n - c > 0 ? " and " + std::to_string(n - c) + " trade goods" : std::string()) + ".");
            return true;
        }
        case Ab::DsKenTucc: {
            if (!heroUnlocked(G, p)) return false;
            purgeHero(p);
            ds3Hero(E, G, p, pn(p) + " reveals the Primordial Data Core: every planet they control is explored.");
            std::vector<int> list;
            for (int x : controlledPlanets(G, p))
                if (explorable3(G, x)) list.push_back(x);
            for (int x : list) explorePlanet(p, x);
            return true;
        }
        case Ab::DsImpressAction: {
            if (!techReady(p, "dsdihmy")) return false;
            std::vector<Option> opts;
            for (int x : ti::readyPlanets(G, p))
                if (explorable3(G, x)) opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + "/" + std::to_string(planetInfluence(G, x))});
            if (opts.empty()) return false;
            const int x = fpick(p, Ab::DsImpressAction, "Impressment Programs: exhaust and explore which planet?", opts, "", false, true);
            exhaustTech(p, "dsdihmy");
            G.planets[size_t(x)].exhausted = true;
            if (G.fixes >= 11) planetsExhausted(p, {x});  // (0.44.7)
            ds3Event(E, p, "Impressment Programs", pn(p) + " sends recruiters to " + planetName(x) + ".", G.planets[size_t(x)].system);
            explorePlanet(p, x);
            return true;
        }
        case Ab::DsGorthrim: {
            if (!heroUnlocked(G, p)) return false;
            purgeHero(p);
            ds3Hero(E, G, p, pn(p) + " sends Gorthrim's expeditions out: up to 3 attachments for their planets.");
            for (int found = 0; found < 3; ++found) {
                std::vector<Option> decks;
                for (Trait t : {Trait::Cultural, Trait::Hazardous, Trait::Industrial}) {
                    const int d = traitDeck(t);
                    bool mine = false;
                    for (int x : controlledPlanets(G, p)) mine = mine || hasTraitOf(G, x, t);
                    if (d >= 0 && mine && G.exploreDeck[size_t(d)].size() + G.exploreDiscard[size_t(d)].size() > 0)
                        decks.push_back({d, exploreTypeName(ExploreType(d)), ""});
                }
                if (decks.empty()) break;
                const int d = decks.size() == 1 ? decks[0].id : fpick(p, Ab::DsGorthrim, "Gorthrim: reveal cards from which deck?", decks);
                int card = -1;
                for (int guard = 0; guard < 40; ++guard) {
                    const int c = drawExplore(d);
                    if (c < 0) break;
                    if (kExploreCards[c].kind == ExploreKind::Attach) {
                        card = c;
                        break;
                    }
                    G.exploreDiscard[size_t(d)].push_back(int16_t(c));
                }
                if (card < 0) break;
                std::vector<Option> opts;
                const Trait tr = d == traitDeck(Trait::Cultural) ? Trait::Cultural : d == traitDeck(Trait::Hazardous) ? Trait::Hazardous : Trait::Industrial;
                for (int x : controlledPlanets(G, p))
                    if (hasTraitOf(G, x, tr)) opts.push_back({x, planetName(x), ""});
                if (opts.empty()) {
                    G.exploreDiscard[size_t(d)].push_back(int16_t(card));
                    continue;
                }
                curValue_ = card;
                const int x = opts.size() == 1 ? opts[0].id : fpick(p, Ab::DsGorthrim, std::string("Gorthrim: attach ") + kExploreCards[card].name + " to which planet?", opts, "", false, true);
                curValue_ = 0;
                say(p, pn(p) + " attaches " + kExploreCards[card].name + " to " + planetName(x) + " (Gorthrim).", G.planets[size_t(x)].system);
                resolveExplore(p, card, G.planets[size_t(x)].system, x);
            }
            return true;
        }
        case Ab::DsAtropha: {
            if (!heroUnlocked(G, p) || pl.dsSheet < 0) return false;
            const bool one = G.stage1Revealed < 5;  // (Limited Vision: stage II only once every stage I is out)
            std::vector<int16_t>& rest = one ? G.stage1Rest : G.stage2Rest;
            std::vector<int16_t>& deck = one ? G.stage1Deck : G.stage2Deck;
            if (rest.empty() || deck.empty() || deck.front() != pl.dsSheet) return false;
            purgeHero(p);
            std::vector<int16_t> drawn;
            for (int i = 0; i < 3 && !rest.empty(); ++i) {
                drawn.push_back(rest.back());
                rest.pop_back();
            }
            E.revealed();
            std::vector<Option> opts;
            for (int16_t o : drawn) opts.push_back({o, std::string(kObjectives[o].name) + " - " + std::to_string(kObjectives[o].points) + " VP", objectiveSummary(o)});
            opts.push_back({-1, std::string("Keep ") + kObjectives[pl.dsSheet].name, objectiveSummary(pl.dsSheet)});
            const int o = fpick(p, Ab::DsAtropha, "Atropha: replace the objective on your sheet?", opts, "The one on your sheet is the next public objective revealed.");
            for (int16_t x : drawn)
                if (x != o) rest.insert(rest.begin(), x);
            if (o >= 0) {
                rest.insert(rest.begin(), deck.front());
                deck.front() = int16_t(o);
                pl.dsSheet = int16_t(o);
            }
            ds3Hero(E, G, p, pn(p) + " weaves fate: " + (o >= 0 ? std::string("the next public objective is rewritten.") : std::string("the threads hold.")));
            if (o >= 0 && pl.human) feedNote(p, std::string("Atropha: the next public objective will be ") + kObjectives[o].name + ".");
            return true;
        }
        case Ab::DsYgegnad: {
            if (!heroUnlocked(G, p) || G.dsGlory.empty()) return false;
            purgeHero(p);
            ds3Hero(E, G, p, pn(p) + " calls on Ygegnad, The Thunder: a tale of legends.");
            std::vector<int> cleared;
            for (int16_t g : std::vector<int16_t>(G.dsGlory)) {
                std::vector<Option> opts;
                std::vector<int> zone{g};
                for (int16_t a : G.systems[size_t(g)].adj) zone.push_back(a);
                for (int s : zone)
                    if (G.systems[size_t(s)].hasToken(p) && std::find(cleared.begin(), cleared.end(), s) == cleared.end())
                        opts.push_back({s, sysName(s), std::to_string(G.systems[size_t(s)].space[size_t(p)].ships()) + " of your ships"});
                if (opts.empty()) continue;
                opts.push_back({-1, "None", ""});
                const int s = fpick(p, Ab::DsYgegnad, "Ygegnad: take back your command token from which system near " + sysName(g) + "?", opts, "", true);
                if (s < 0) continue;
                cleared.push_back(s);
                G.systems[size_t(s)].tokens = uint16_t(G.systems[size_t(s)].tokens & ~(1u << p));
                say(p, pn(p) + " takes its command token back from " + sysName(s) + " (Ygegnad).", s);
                gainTokens(p, 1, false, "Ygegnad: 1 command token.");
            }
            return true;
        }
        case Ab::DsBanua: {
            if (!heroUnlocked(G, p)) return false;
            purgeHero(p);
            ds3Hero(E, G, p, pn(p) + " calls the Harmonic Convergence: fighters gather over their worlds.");
            for (size_t s = 0; s < G.systems.size(); ++s) {
                bool planet = false;
                for (int16_t x : G.systems[s].planets) planet = planet || G.planets[size_t(x)].owner == p;
                if (!planet || otherShipsIn(G, p, int(s)) || neutralShipsIn(G, int(s))) continue;
                const int k = std::min(2, unitsAvailable(G, p, UnitType::Fighter));
                if (k <= 0) break;
                addUnits(p, UnitType::Fighter, k, int(s), -1);
                removeExcessCapacity(p, int(s));
            }
            // then 1 attachment from a planet in a system with their ships to a planet in their home system
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                if (!G.systems[s].space[size_t(p)].ships() || int(s) == pl.homeSystem) continue;
                for (int16_t x : G.systems[s].planets)
                    for (size_t i = 0; i < G.planets[size_t(x)].attachments.size(); ++i) {
                        const int16_t c = G.planets[size_t(x)].attachments[i];
                        if (!exKey3(c, "dmz")) opts.push_back({x * 16 + int(i), std::string(kExploreCards[c].name) + " on " + planetName(x), ""});
                    }
            }
            std::vector<Option> home;
            if (pl.homeSystem >= 0)
                for (int16_t y : G.systems[size_t(pl.homeSystem)].planets)
                    if (G.planets[size_t(y)].owner == p) home.push_back({y, planetName(y), ""});
            if (opts.empty() || home.empty()) return true;
            opts.push_back({-1, "Leave them", ""});
            bool dmz = false;  // 0.43.0 (D70)
            for (int x : controlledPlanets(G, p))
                for (int16_t a : G.planets[size_t(x)].attachments) dmz = dmz || exKey3(a, "dmz");
            const int c = fpick(p, Ab::DsBanua, "Banua Gowen: move an attachment to your home system?", opts,
                                dmz ? std::string(1, kRedNote) + "The Demilitarized Zone can't be moved." : std::string());
            if (c < 0) return true;
            const int x = c / 16, i = c % 16;
            const int y = home.size() == 1 ? home[0].id : fpick(p, Ab::DsBanua, "Banua Gowen: attach it to which home planet?", home, "", false, true);
            auto& att = G.planets[size_t(x)].attachments;
            const int16_t card = att[size_t(i)];
            att.erase(att.begin() + long(i));
            G.planets[size_t(y)].attachments.push_back(card);
            say(p, pn(p) + " moves " + kExploreCards[card].name + " from " + planetName(x) + " to " + planetName(y) + " (Banua Gowen).");
            return true;
        }
        default: return ds4Action(p, ab);  // batch 4 (rules_ds4.cpp)
    }
}

}  // namespace ti
