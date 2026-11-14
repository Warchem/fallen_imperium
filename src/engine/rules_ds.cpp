// Discordant Stars (0.37.0, a module - fan-made by the Discordant Stars team): its 34 factions. The data comes from
// the AsyncTI4 set (gen_data.py) checked against the owner's Discordant Stars reference; what each ability does is
// written in our own words in faction_text.cpp. A faction plays only once it is in kDone; until then it is shown as
// "coming soon" in the faction picker. With the module on, faction breakthroughs are off (breakthroughsOn).
// The AI for these questions: ai/ai_ds.cpp (Ab::Ds*).
//   Batch 1 (0.37.0):
//   Kortali Tribunal     - Zealous, Ruthless, Magistrate, Tempest Drive, Deliverance Engine, Blessing of the Queens,
//                          Queen Lucreia / Lorena / Nadalia, Justicar (shield kept: rules_tactical bombardment)
//   Vaylerian Scourge    - Cargo Raiders, Scour, Raze, Lost Cause, Raider I / II (query raiderCapacity, restricted
//                          hits), Scavenger Exos, Clan's Favor, Yvin Korduul, Pyndil Gonsuul (Game::dsCapBonus), Dyln
//                          Harthuul (Player::dsHeroSys), Eclipse
//   Lanefir Remnants     - A New Edifice (no specialties: skipPlanets; relics purged), Iconoclasm (analyticalWaiver),
//                          War Stories, Memory of Dusk, Spark Thrusters, ATS Armaments (Player::dsTokens), Spoils of
//                          War, Vassa Hagi, Master Halbert (Game::fragsPurged), The Venerable, Troubadour
//   Kyro Sodality        - Contagion, Subversive, Auriga, Indoctrination Teams, Vector Programs, Kyro Rider, Tox,
//                          Silas Deriga, Speygh (Game::blight: initiative 9), Pustule
//   Cheiran Hordes       - Teeming (query unitLimitOf), Moult, Byssus (mechs count as structures here), Lithodax,
//                          Chitin Hulk, Brood Pod, Carcinisation, Kkavras, Phquaiset, Thakt Clqua, Nauplius
//   Celdauri Trade Conf. - Projection of Power, Industrialists (unitLimitOf), Supremacy (productionCapacity), Trade Port
//                          (query), Emergency Mobilization, Trade Alliance (commanderUsable), George Nobin, Henry
//                          Storcher (routeNeighbors), Titus Flavius, Minuteman
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
constexpr int kShipMaskD = kShipMaskAll;  // Carrier .. WarSun, the second flagship
constexpr int kNonFighterD = kShipMaskD & ~(1 << int(UnitType::Fighter));
constexpr int kGroundMaskD = (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech));

void dsEvent(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
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
void dsHeroEvent(Engine& E, const Game& G, int p, const std::string& text) {
    const int i = leaderIndex(G, p, kHero);
    Event e;
    e.kind = EventKind::Leader;
    e.player = int8_t(p);
    e.card = int16_t(i >= 0 ? G.players[size_t(p)].leaders[size_t(i)].leader : 0);
    e.label = "hero";
    e.text = text;
    E.emit(e);
}
bool dsExplorable(const Game& G, int x) {
    const PlanetState& ps = G.planets[size_t(x)];
    if (ps.destroyed || ps.system < 0) return false;
    return hasTraitOf(G, x, Trait::Cultural) || hasTraitOf(G, x, Trait::Hazardous) || hasTraitOf(G, x, Trait::Industrial);
}
// Byssus (Cheiran): their mechs on planets they control count as structures (not for objectives)
bool structureIn(const Game& G, int p, int sys) {
    for (int16_t x : G.systems[size_t(sys)].planets) {
        const Force& f = G.planets[size_t(x)].units[size_t(p)];
        if (f.structures()) return true;
        if (f.count(UnitType::Mech) && G.planets[size_t(x)].owner == p && isFaction(G, p, "cheiran")) return true;
    }
    return false;
}
bool structureNear(const Game& G, int p, int sys) {
    if (structureIn(G, p, sys)) return true;
    for (int16_t a : G.systems[size_t(sys)].adj)
        if (structureIn(G, p, a)) return true;
    return false;
}
int docksIn(const Game& G, int p, int sys) {
    int n = 0;
    for (int16_t x : G.systems[size_t(sys)].planets) n += G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock);
    return n;
}
bool dockNear(const Game& G, int p, int sys) {
    if (docksIn(G, p, sys)) return true;
    for (int16_t a : G.systems[size_t(sys)].adj)
        if (docksIn(G, p, a)) return true;
    return false;
}
std::string unitWords(const Force& f) {
    std::string s;
    for (int t = 0; t < kUnitTypes; ++t)
        if (f.n[t]) s += (s.empty() ? "" : ", ") + std::to_string(f.n[t]) + " " + unitTypeName(UnitType(t), f.n[t] > 1);
    return s.empty() ? "nothing" : s;
}
}  // namespace

// Starting technologies the faction chooses (kStartChoices): from its list; the Edyn any 3 of different colours
// without prerequisites; the Kjalengard 1 non-faction unit upgrade.
void Rules::dsStartingTechs(int p) {
    ds4Setup(p);  // Policies (Olradin)
    Player& pl = G.players[size_t(p)];
    for (int i = 0; i < kStartChoiceCount; ++i) {
        const StartChoiceDef& c = kStartChoices[i];
        if (c.faction != pl.faction) continue;
        const char* key = kFactions[pl.faction].key;
        std::vector<Option> opts;
        auto noPrereq = [](int t) { return kTechs[t].req[0] + kTechs[t].req[1] + kTechs[t].req[2] + kTechs[t].req[3] == 0; };
        if (!std::strcmp(key, "edyn")) {
            int used = 0;  // colours taken
            for (int k = 0; k < c.count; ++k) {
                opts.clear();
                for (int t = 0; t < kTechCount; ++t) {
                    const TechDef& d = kTechs[t];
                    if (d.faction >= 0 || d.unitUpgrade || d.color == TechColor::None || !noPrereq(t) || pl.hasTech(t) || !techImplemented(t)) continue;
                    if ((used >> int(d.color)) & 1) continue;
                    if (d.source != Source::Base && d.source != Source::PoK) continue;
                    opts.push_back({t, d.name, techSummary(t)});
                }
                if (opts.empty()) break;
                const int t = pick(p, Tag::SetupTech, "Edyn: choose a starting technology (" + std::to_string(k + 1) + " of 3)", opts,
                                   "Three technologies of different colours without prerequisites.");
                used |= 1 << int(kTechs[t].color);
                researchTech(p, t);
            }
            return;
        }
        if (c.techs[0] < 0) {  // the Kjalengard: a non-faction unit upgrade
            for (int t = 0; t < kTechCount; ++t)
                if (kTechs[t].unitUpgrade && kTechs[t].faction < 0 && !pl.hasTech(t) && (kTechs[t].source == Source::Base || kTechs[t].source == Source::PoK))
                    opts.push_back({t, kTechs[t].name, techSummary(t)});
        } else {
            for (int16_t t : c.techs)
                if (t >= 0 && !pl.hasTech(t)) opts.push_back({t, kTechs[t].name, techSummary(t)});
        }
        if (opts.empty()) return;
        const int n = std::min<int>(c.count, int(opts.size()));
        for (int t : pickMany(p, Tag::SetupTech, std::string(factionShortName(pl.faction)) + ": choose " + std::to_string(n) +
                                                      (n > 1 ? " starting technologies" : " starting technology"),
                              opts, n, n))
            researchTech(p, t);
        return;
    }
}

// ================================================================== shared helpers
// Produce 1 ship in a system for 'discount' less (Moult, Brood Pod). Returns true when something was made.
bool Rules::dsProduceOne(int p, int sys, int discount, const std::string& why) {
    const int before = G.systems[size_t(sys)].space[size_t(p)].ships();
    dsDiscount_ = discount;
    say(p, pn(p) + " may produce 1 ship in " + sysName(sys) + " (" + why + (discount > 0 ? ", " + std::to_string(discount) + " cheaper" : std::string()) + ").", sys);
    const bool na = notProductionAbility_;  // (0.44.7, fixes 11: an ability's production - no Sarween Tools / AIDA)
    if (G.fixes >= 11) notProductionAbility_ = true;
    productionCore(p, sys, 1, false, true);
    notProductionAbility_ = na;
    dsDiscount_ = 0;
    return G.systems[size_t(sys)].space[size_t(p)].ships() > before;
}

// Put 1 infantry from the reinforcements on a planet and resolve an invasion there (Contagion): a ground combat
// against each other player's ground forces, then control if nobody else is left.
void Rules::dsInvade(int p, int planet) {
    if (unitsAvailable(G, p, UnitType::Infantry) <= 0) return;
    const int sys = G.planets[size_t(planet)].system;
    addUnits(p, UnitType::Infantry, 1, sys, planet);
    dsResolvePlanet(p, planet);
}

// p's ground forces were put on a planet: ground combats against everyone else's there, then control.
void Rules::dsResolvePlanet(int p, int planet) {
    if (G.planets[size_t(planet)].units[size_t(p)].groundForces() == 0) return;
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != p && G.planets[size_t(planet)].units[size_t(p)].groundForces() > 0 && groundForcesOn(G, q, planet) > 0) groundCombat(p, q, planet);
    if (G.planets[size_t(planet)].units[size_t(p)].groundForces() > 0 && neutralGroundOn(G, planet) > 0) groundCombat(p, kNeutral, planet);
    if (G.planets[size_t(planet)].units[size_t(p)].groundForces() == 0) return;
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != p && groundForcesOn(G, q, planet) > 0) return;
    if (neutralGroundOn(G, planet) > 0) return;
    if (G.planets[size_t(planet)].owner != p) setControl(planet, p);
}

// ================================================================== combat
void Rules::dsCombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m) {
    (void)opp;
    (void)planet;
    if (p < 0 || p >= G.numPlayers || sys < 0) return;
    // Lithodax (Cheiran flagship): 1 more die beside or among their structures
    if (space && flagshipAbility(G, p, "cheiran") && flagshipIn(G, p, sys) && structureNear(G, p, sys)) m.extraDice[int(UnitType::Flagship)] += 1;
    // The Celagrom (Celdauri, Trade Protectorate - 0.52.1 its own unit): 1 more die while they control another flagship
    if (space && G.fixes >= 17 && dsbtOn(p, "celdauri") && G.systems[size_t(sys)].space[size_t(p)].count(UnitType::Flagship2) &&
        unitsOnBoard(G, p, UnitType::Flagship) > 0)
        m.extraDice[int(UnitType::Flagship2)] += 1;
    // Spear of Vaylar (Vaylerian hero): +1 to their ships' rolls this tactical action
    if (space && G.players[size_t(p)].dsHeroSys >= 0 && p == G.activePlayer)
        for (int t : kShipIdx) m.perType[t] += 1;
    ds2CombatMods(p, opp, sys, planet, space, m);  // batch 2 (rules_ds2.cpp)
}

void Rules::dsSpaceCombatStart(int att, int def, int sys) {
    dsOncePerCombat_ = 0;
    dsProjection_ = 0;
    for (int& b : dsBlockRetreat_) b = -1;
    G.dsGale = -1;
    for (int side : {att, def}) {
        dsShipsAtStart_[side] = G.systems[size_t(sys)].space[size_t(side)].nonFighterShips();
        if (side >= G.numPlayers) continue;
        if (flagshipAbility(G, side, "khrask") && flagshipIn(G, side, sys)) {  // Splintering Gale: 2 ships sustain damage
            G.dsGale = int8_t(sys);
            dsEvent(E, side, "Splintering Gale", "Two of " + pn(side) + "'s ships can sustain damage in this combat.", sys);
        }
        const int other = side == att ? def : att;
        // Lost Cause (Vaylerian flagship): one neighbouring system the opponent can't retreat to
        if (flagshipAbility(G, side, "vaylerian") && flagshipIn(G, side, sys) && other < G.numPlayers) {
            std::vector<Option> opts;
            for (int a : retreatOptions(other, sys)) opts.push_back({a, sysName(a), ""});
            if (!opts.empty()) {
                const int a = opts.size() == 1 ? opts[0].id
                                               : fpick(side, Ab::DsLostCause, "Lost Cause: which system can't " + pn(other) + " retreat to?", opts, "", true);
                dsBlockRetreat_[other] = a;
                dsEvent(E, side, "Lost Cause", pn(other) + " can't retreat to " + sysName(a) + ".", sys);
            }
        }
        // The Celagrom (Celdauri, Trade Protectorate - 0.52.1 its own unit): at the start of a space combat it may produce 1 hit,
        // assigned by the Celdauri to one of the opponent's ships (the owner may still use Sustain Damage)
        if (G.fixes >= 17 && dsbtOn(side, "celdauri") && G.systems[size_t(sys)].space[size_t(side)].count(UnitType::Flagship2) && !noAbilities(G, side, sys)) {
            const Force& them = G.systems[size_t(sys)].space[size_t(other)];
            std::vector<Option> opts;
            for (int t : kShipIdx)
                if (them.n[t]) opts.push_back({t, unitLabel(G, other, UnitType(t)), std::to_string(them.n[t]) + " in the fight"});
            if (!opts.empty()) {
                opts.push_back({-1, "No hit", ""});
                curValue_ = other;  // (for the AI: whose ships)
                const int t = fpick(side, Ab::DsCelagrom, "The Celagrom: 1 hit on which of " + pn(other) + "'s ships?", opts);
                curValue_ = 0;
                if (t >= 0) {
                    dsEvent(E, side, "The Celagrom", pn(side) + "'s Celagrom opens fire on " + pn(other) + "'s " + unitLabel(G, other, UnitType(t)) + ".", sys);
                    assignHits(other, sys, -1, 1, 1 << t, "The Celagrom", side);
                }
            }
        }
        // Projection of Power (Celdauri): a ship here gains Anti-Fighter Barrage 6
        if (fac(side, "celdauri") && dockNear(G, side, sys) && G.systems[size_t(sys)].space[size_t(side)].ships()) {
            dsProjection_ = uint16_t(dsProjection_ | (1u << side));
            dsEvent(E, side, "Projection of Power", pn(side) + "'s ship gains Anti-Fighter Barrage 6 for this combat.", sys);
        }
    }
    ds3SpaceCombatStart(att, def, sys);  // batch 3: Mercenaries, Clona Bathru, Repairitor, Merkismathr Asvand, Vassalage
}

// Extra anti-fighter barrage dice at 6 for this side (round 1): Projection of Power, Trade Port docks on planets here.
int Rules::dsAfbDice(int side, int sys) const {
    if (side < 0 || side >= G.numPlayers || !fac(side, "celdauri")) return 0;
    int n = ((dsProjection_ >> side) & 1) ? 1 : 0;
    if (!noAbilities(G, side, sys)) n += 2 * docksIn(G, side, sys);
    return n;
}

void Rules::dsCombatRoundStart(int att, int def, int sys, int planet, bool space) {
    if (!space && planet >= 0) ds4GroundRoundStart(att, def, planet);  // Revenant (batch 4)
    dsRerolls_[0] = dsRerolls_[1] = 0;
    const int side[2] = {att, def};
    for (int i = 0; i < 2; ++i) {
        const int p = side[i], o = side[1 - i];
        if (p >= G.numPlayers) continue;
        Player& pl = G.players[size_t(p)];
        // ATS Armaments (Lanefir): tokens off the card = rerolls this round
        if (owns(p, "dslaner") && pl.dsTokens > 0) {
            std::vector<Option> opts;
            for (int k = 0; k <= pl.dsTokens; ++k) opts.push_back({k, std::to_string(k), k ? "Reroll up to " + std::to_string(k) + " missed dice." : "Keep them."});
            curValue_ = space ? 1 : 0;
            const int k = fpick(p, Ab::DsAts, "ATS Armaments: remove how many tokens for rerolls this round?", opts);
            curValue_ = 0;
            if (k > 0) {
                pl.dsTokens = int8_t(pl.dsTokens - k);
                dsRerolls_[i] = k;
            }
        }
        if (!space) continue;
        // Trade Port II (Celdauri): its anti-fighter barrage fires every round
        if (combatRound_ >= 2 && fac(p, "celdauri") && !std::strcmp(unitDef(G, p, UnitType::SpaceDock).key, "celdauri_spacedock2") &&
            docksIn(G, p, sys) && G.systems[size_t(sys)].space[size_t(o)].count(UnitType::Fighter) && !noAbilities(G, p, sys)) {
            RollResult r;
            addDice(r, 2 * docksIn(G, p, sys), 6, int(UnitType::SpaceDock));
            reportRoll(p, sys, "Trade Port anti-fighter barrage", r);
            if (r.hits) destroyUnits(o, UnitType::Fighter, std::min(r.hits, G.systems[size_t(sys)].space[size_t(o)].count(UnitType::Fighter)), sys, -1, p, "anti-fighter barrage");
        }
        // Clan's Favor (the Vaylerian's note): the holder's fleet slips away, the combat is a draw
        const int v = playerOfFaction(G, "vaylerian");
        if (v >= 0 && v != p && holdsNote(G, p, v, kNoteFaction) && hasShips(G, p, sys) && !combatDraw_ && !(G.fixes >= 26 && noRetreat_)) {
            std::vector<Option> opts;
            for (int a : routeNeighbors(G, p, sys)) {
                if (G.systems[size_t(a)].isHyperlane() || otherShipsIn(G, p, a)) continue;
                opts.push_back({a, sysName(a), ""});
            }
            if (!opts.empty() && fyes(p, Ab::DsClansFavor, "Play " + pn(v) + "'s Clan's Favor: move your ships out and end the combat in a draw?",
                                      factionNoteText(G.players[size_t(v)].faction, kNoteFaction))) {
                const int to = opts.size() == 1 ? opts[0].id : fpick(p, Ab::DsClansFavor, "Clan's Favor: move your ships to which system?", opts, "", true);
                Player& vp = G.players[size_t(v)];
                if (!vp.actionCards.empty()) {
                    const int card = vp.actionCards[size_t(E.rng().below(int(vp.actionCards.size())))];
                    discardActionCard(v, card, false);
                    say(v, pn(v) + " discards an action card (Clan's Favor).", sys);
                }
                returnNote(p, makeNote(v, kNoteFaction));
                dsEvent(E, p, "Clan's Favor", pn(p) + " plays Clan's Favor: its fleet slips away to " + sysName(to) + ".", sys);
                doRetreat(p, sys, to);  // (places a command token from the reinforcements there)
                combatDraw_ = true;
                return;
            }
        }
    }
    (void)planet;
}

// Cargo Raiders (Vaylerian): in the first round the opponent may only announce a retreat by spending 1 trade good.
bool Rules::dsCargoRaiders(int who, int opp, int sys) {
    if (combatRound_ != 1 || opp < 0 || opp >= G.numPlayers || who >= G.numPlayers || !fac(opp, "vaylerian")) return false;
    if (!fyes(opp, Ab::DsCargoRaiders, "Cargo Raiders: " + pn(who) + " wants to retreat - make them pay 1 trade good for it?",
              "Without the trade good they can't retreat this round.")) return false;
    Player& w = G.players[size_t(who)];
    if (w.tradeGoods > 0 && fyes(who, Ab::DsCargoPay, "Cargo Raiders: spend 1 trade good to retreat anyway?", "Otherwise you stay and fight this round.")) {
        w.tradeGoods = int8_t(w.tradeGoods - 1);
        dsEvent(E, opp, "Cargo Raiders", pn(who) + " pays 1 trade good to break away.", sys);
        return false;
    }
    dsEvent(E, opp, "Cargo Raiders", pn(who) + " can't retreat this round (Cargo Raiders).", sys);
    return true;
}

bool Rules::dsCantRetreat(int who, int opp, int sys) const {
    if (who >= G.numPlayers) return true;  // (neutral units never retreat)
    if (retreatOptions(who, sys).empty()) return true;
    return combatRound_ == 1 && opp >= 0 && opp < G.numPlayers && fac(opp, "vaylerian") && G.players[size_t(who)].tradeGoods == 0;
}

// Raider I / II (Vaylerian cruisers): against an opponent who can't retreat, their hits go to non-fighter ships.
int Rules::dsRestrictedHits(int shooter, int victim, int sys, const RollResult& r) const {
    if (shooter >= G.numPlayers || !fac(shooter, "vaylerian") || !dsCantRetreat(victim, shooter, sys)) return 0;
    const char* k = unitDef(G, shooter, UnitType::Cruiser).key;
    if (std::strncmp(k, "vaylerian_cruiser", 17) != 0) return 0;
    int n = 0;
    for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i)
        if (r.dice[i] >= r.need[i] && r.unit[i] == int(UnitType::Cruiser)) ++n;
    return std::min(n, r.hits);
}
// Raider II: and they can't be cancelled
int Rules::dsUncancellable(int shooter, int victim, int sys, const RollResult& r) const {
    if (shooter >= G.numPlayers || std::strcmp(unitDef(G, shooter, UnitType::Cruiser).key, "vaylerian_cruiser2") != 0) return 0;
    return dsRestrictedHits(shooter, victim, sys, r);
}

// After the hits of a space combat round: Deliverance Engine, Carcinisation.
void Rules::dsShipsLost(int victim, int opp, int sys, const Force& before) {
    ds4ShipsLost(victim, opp, sys, before);  // Collateralized Loans (batch 4)
    if (victim >= G.numPlayers) return;
    const Force& now = G.systems[size_t(sys)].space[size_t(victim)];
    const int lost = std::max(0, before.nonFighterShips() - now.nonFighterShips());
    if (lost <= 0) return;
    // Deliverance Engine (Kortali): once per combat, 1 hit on a non-carrier ship
    // (0.44.8, fixes 12: asked only with a non-carrier ship to hit; the Kortali choose which kind takes it - "you
    // assign it" - and its owner may still use Sustain Damage)
    const Force& tf = G.systems[size_t(sys)].space[size_t(opp >= 0 ? opp : 0)];
    const bool target12 = opp >= 0 && (tf.ships() - tf.count(UnitType::Carrier)) > 0;
    if (owns(victim, "dskortr") && !((dsOncePerCombat_ >> victim) & 1) && opp >= 0 && G.systems[size_t(sys)].space[size_t(opp)].ships() &&
        (G.fixes < 12 || target12) &&
        fyes(victim, Ab::DsDeliverance, "Deliverance Engine: produce 1 hit against " + pn(opp) + "'s non-carrier ships?", "Once per combat.")) {
        dsOncePerCombat_ = dsOncePerCombat_ | (1u << victim);
        dsEvent(E, victim, "Deliverance Engine", pn(victim) + " strikes back: 1 hit.", sys);
        int mask = kShipMaskD & ~(1 << int(UnitType::Carrier));
        if (G.fixes >= 12) {
            std::vector<Option> kinds;
            for (int t : kShipIdx)
                if (t != int(UnitType::Carrier) && tf.n[t]) kinds.push_back({t, unitTypeName(UnitType(t)), std::to_string(tf.n[t]) + " there"});
            if (!kinds.empty()) {
                const int t = kinds.size() == 1 ? kinds[0].id : fpick(victim, Ab::DsDeliverance, "Deliverance Engine: the hit goes to which ship?", kinds);
                mask = 1 << t;
            }
        }
        assignHits(opp, sys, -1, 1, mask, "Deliverance Engine", victim);
    }
    // Carcinisation (the Cheiran's note): 2 fighters for a lost ship
    const int c = playerOfFaction(G, "cheiran");
    if (c >= 0 && c != victim && holdsNote(G, victim, c, kNoteFaction) && unitsAvailable(G, victim, UnitType::Fighter) > 0 &&
        fyes(victim, Ab::DsCarcinisation, "Play " + pn(c) + "'s Carcinisation: place 2 fighters here?", factionNoteText(G.players[size_t(c)].faction, kNoteFaction))) {
        addUnits(victim, UnitType::Fighter, 2, sys, -1);
        returnNote(victim, makeNote(c, kNoteFaction));
        dsEvent(E, victim, "Carcinisation", pn(victim) + " places 2 fighters (Carcinisation).", sys);
    }
}

// Hits about to be assigned to 'victim': Queen Lorena (first round), Spc. Phquaiset (ground combat, defending).
int Rules::dsCancelHits(int victim, int opp, int sys, int planet, int hits) {
    (void)opp;
    if (hits <= 0 || victim >= G.numPlayers) return hits;
    if (combatRound_ == 1 && commanderUsable(G, victim, "kortali") && !((dsOncePerCombat_ >> (victim + 8)) & 1) &&
        fyes(victim, Ab::DsLorena, "Queen Lorena: cancel 1 of the " + std::to_string(hits) + " hits against you?", "In the first round of a combat.")) {
        dsOncePerCombat_ = dsOncePerCombat_ | (1u << (victim + 8));
        dsEvent(E, victim, "Queen Lorena", pn(victim) + " cancels 1 hit (Queen Lorena).", sys);
        --hits;
    }
    if (planet >= 0 && hits > 0 && victim != G.activePlayer && commanderUsable(G, victim, "cheiran")) {
        Player& pl = G.players[size_t(victim)];
        const int can = std::min(hits, pl.commodities + pl.tradeGoods);
        if (can > 0) {
            std::vector<Option> opts;
            for (int k = 0; k <= can; ++k) opts.push_back({k, std::to_string(k), k ? "Pay " + std::to_string(k) + " (commodities first)." : "Take the hits."});
            const int k = fpick(victim, Ab::DsPhquaiset, "Spc. Phquaiset: cancel hits for 1 commodity or trade good each?", opts);
            for (int i = 0; i < k; ++i) {
                if (pl.commodities > 0) pl.commodities = int8_t(pl.commodities - 1);
                else pl.tradeGoods = int8_t(pl.tradeGoods - 1);
            }
            if (k > 0) dsEvent(E, victim, "Spc. Phquaiset", pn(victim) + " cancels " + std::to_string(k) + " hit" + (k > 1 ? "s." : "."), sys);
            hits -= k;
        }
    }
    return ds5CancelHits(victim, opp, sys, planet, hits);  // Amanita Muscaria, Jack Hallard (batch 5)
}

void Rules::dsGroundCombatStart(int att, int def, int planet) {
    ds3GroundCombatStart(att, def, planet);  // batch 3: Repairitor, Merkismathr Asvand
    ds5GroundCombatStart(att, def, planet);  // batch 5: Covert Strike Teams
    // Ruthless (Kortali): invading an exhausted planet they don't control - 1 hit first
    if (att < G.numPlayers && fac(att, "kortali") && G.planets[size_t(planet)].exhausted && G.planets[size_t(planet)].owner != att &&
        G.planets[size_t(planet)].units[size_t(def)].groundForces() > 0 &&
        fyes(att, Ab::DsRuthless, "Ruthless: produce 1 hit against " + pn(def) + "'s ground forces on " + planetName(planet) + "?",
             "The planet is exhausted.")) {
        dsEvent(E, att, "Ruthless", pn(att) + " strikes first on the exhausted " + planetName(planet) + ".", G.planets[size_t(planet)].system);
        assignHits(def, G.planets[size_t(planet)].system, planet, 1, kGroundMaskD, "Ruthless", att);
    }
}

void Rules::dsGroundCombatOver(int att, int def, int planet, int winner) {
    ds5GroundCombatOver(att, def, planet, winner);  // batch 5: Jarl Vel & Jarl Jotrun, Jotun
    // Tox (Kyro agent): a player who lost or drew a ground combat refills commodities; the Kyro get infantry
    for (int q : {att, def}) {
        if (q >= G.numPlayers || q == winner) continue;
        const int user = agentUser(q, "kyro");
        if (user < 0) continue;
        const int k = playerOfFaction(G, "kyro");
        if (k < 0) continue;
        agentFor(user, q);
        const bool use = fyes(user, Ab::DsTox, user == q ? "Exhaust Tox: refill your commodities and place infantry?"
                                                         : "Exhaust Tox for " + pn(q) + ": they refill commodities, you place infantry?",
                              leaderText(findLeader("kyroagent")));
        curFor_ = -1;
        if (!use) continue;
        spendAgent(user, "kyro");
        agentDone(user, q);
        Player& pl = G.players[size_t(q)];
        const int before = pl.commodities;
        pl.commodities = int8_t(refreshTo(G, pl));
        replenished(q, pl.commodities - before);
        const int n = std::min(commodityValue(G, q) - 1, unitsAvailable(G, k, UnitType::Infantry));
        std::vector<Option> where;  // (not the fought-over planet, nor one with other players' ground forces)
        for (int x : controlledPlanets(G, k)) {
            bool foes = x == planet;
            for (int o = 0; o < G.numPlayers; ++o)
                if (o != k && groundForcesOn(G, o, x) > 0) foes = true;
            if (!isDMZ(G, x) && !foes) where.push_back({x, planetName(x), ""});
        }
        if (n > 0 && !where.empty()) {
            const int x = where.size() == 1 ? where[0].id : fpick(k, Ab::DsTox, "Tox: place " + std::to_string(n) + " infantry on which planet?", where, "", false, true);
            addUnits(k, UnitType::Infantry, n, G.planets[size_t(x)].system, x);
        }
        dsEvent(E, user, "Tox", pn(q) + " refills commodities" + (n > 0 ? "; " + pn(k) + " places " + std::to_string(n) + " infantry." : "."), G.planets[size_t(planet)].system);
    }
}

int Rules::dsDrawWinner(int att, int def) const {
    if (att < G.numPlayers && fac(att, "kyro")) return att;  // Subversive
    if (def < G.numPlayers && fac(def, "kyro")) return def;
    return -1;
}

void Rules::dsWonCombat(int winner, int loser, int sys, bool space) {
    if (winner < 0 || winner >= G.numPlayers) return;
    ds3WonCombat(winner, loser, sys, space);  // Glory, Lachis (batch 3)
    Player& pl = G.players[size_t(winner)];
    // Hkot Tokal unlocks (Khrask commander): a ground combat won on a planet they don't control
    if (!space && fac(winner, "khrask") && G.groundCombatPlanet >= 0 && G.planets[size_t(G.groundCombatPlanet)].owner != winner) pl.dsUnlock = true;
    // Magistrate (Kortali flagship): take damage for a command token
    if (space && flagshipAbility(G, winner, "kortali") && flagshipIn(G, winner, sys) && unitSustains(G, winner, UnitType::Flagship)) {
        Force& f = G.systems[size_t(sys)].space[size_t(winner)];
        if (f.dmg[int(UnitType::Flagship)] < f.n[int(UnitType::Flagship)] && reinforcementTokens(winner) > 0 &&
            fyes(winner, Ab::DsMagistrate, "Magistrate: let the flagship become damaged to gain 1 command token?", "")) {
            f.dmg[int(UnitType::Flagship)] = uint8_t(f.dmg[int(UnitType::Flagship)] + 1);
            say(winner, pn(winner) + "'s flagship takes damage for a command token (Magistrate).", sys);
            gainTokens(winner, 1, false, "Magistrate: 1 command token.");
        }
    }
    // Tempest Drive (Kortali): take your command token back from the system
    if (techReady(winner, "dskortg") && G.systems[size_t(sys)].hasToken(winner) &&
        fyes(winner, Ab::DsTempest, "Exhaust Tempest Drive to take your command token back from " + sysName(sys) + "?", "")) {
        exhaustTech(winner, "dskortg");
        G.systems[size_t(sys)].tokens &= uint16_t(~(1u << winner));
        dsEvent(E, winner, "Tempest Drive", pn(winner) + " takes its command token back from " + sysName(sys) + ".", sys);
    }
    // Queen Lucreia (Kortali agent): a ground combat's winner takes a relic fragment from the loser
    if (!space && loser >= 0 && loser < G.numPlayers && fragmentCount(G, loser) > 0) {
        const int user = agentUser(winner, "kortali");
        if (user >= 0) {
            agentFor(user, winner);
            const bool use = fyes(user, Ab::DsLucreia, user == winner ? "Exhaust Queen Lucreia: take 1 of " + pn(loser) + "'s relic fragments?"
                                                                      : "Exhaust Queen Lucreia for " + pn(winner) + ": they take a fragment from " + pn(loser) + "?",
                                  leaderText(findLeader("kortaliagent")));
            curFor_ = -1;
            if (use) {
                spendAgent(user, "kortali");
                agentDone(user, winner);
                Player& lp = G.players[size_t(loser)];
                std::vector<size_t> frag;
                for (size_t i = 0; i < lp.fragments.size(); ++i)
                    if (isRelicFragment(lp.fragments[i])) frag.push_back(i);
                if (!frag.empty() && vaultShut(loser, winner, "a relic fragment")) frag.clear();  // (High Security Vault)
                if (!frag.empty()) {
                    const size_t i = frag[size_t(E.rng().below(int(frag.size())))];
                    pl.fragments.push_back(lp.fragments[i]);
                    lp.fragments.erase(lp.fragments.begin() + long(i));
                    dsEvent(E, user, "Queen Lucreia", pn(winner) + " takes a relic fragment from " + pn(loser) + ".", sys);
                }
            }
        }
    }
    // Queen Nadalia (Kortali hero): a relic from the loser, then command tokens
    if (fac(winner, "kortali") && heroUnlocked(G, winner)) {
        int n = 0;
        for (int x : controlledPlanets(G, winner))
            if (isLegendaryPlanet(G, x) || planetSkips(G, x)) ++n;
        const bool relic = loser >= 0 && loser < G.numPlayers && !G.players[size_t(loser)].relics.empty();
        curValue_ = n + (relic ? 3 : 0);
        const bool use = (relic || n > 0) && fyes(winner, Ab::DsNadalia, "Purge Queen Nadalia: take a relic from " + (loser < G.numPlayers ? pn(loser) : std::string("them")) + " and gain " +
                                                                  std::to_string(n) + " command token" + (n == 1 ? "?" : "s?"),
                                                  leaderText(findLeader("kortalihero")));
        curValue_ = 0;
        if (use) {
            purgeHero(winner);
            std::string got;
            if (relic) {
                std::vector<Option> opts;
                for (int16_t r : G.players[size_t(loser)].relics) opts.push_back({r, kRelics[r].name, ""});
                const int r = opts.size() == 1 ? opts[0].id : fpick(winner, Ab::DsNadalia, "Queen Nadalia: take which relic?", opts);
                loseRelic(loser, r, false);
                takeRelic(winner, r);
                got = " takes " + std::string(kRelics[r].name) + " and";
            }
            dsHeroEvent(E, G, winner, pn(winner) + got + " gains " + std::to_string(n) + " command tokens (Queen Nadalia).");
            if (n > 0) gainTokens(winner, n, false, "Queen Nadalia: command tokens.");
        }
    }
    // Scavenger Exos (Vaylerian): a ground combat won - an action card (once per action)
    bool& exosOnce = G.fixes >= 12 ? pl.dsExosOnce : pl.dsOnce;
    if (!space && owns(winner, "dsvaylr") && !exosOnce) {
        exosOnce = true;
        say(winner, pn(winner) + " salvages an action card (Scavenger Exos).", sys);
        drawActionCards(winner, 1, "Scavenger Exos");
    }
    // War Stories (Lanefir): explore one of their planets, or the frontier here (once per action)
    if (fac(winner, "lanefir") && !(pl.dsOnce && !space)) {
        std::vector<Option> opts;
        if (G.systems[size_t(sys)].planets.empty()) {
            if (G.systems[size_t(sys)].frontier) opts.push_back({-2 - sys, "The frontier token in " + sysName(sys), ""});
        } else {
            for (int x : controlledPlanets(G, winner))
                if (dsExplorable(G, x)) opts.push_back({x, planetName(x), ""});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Not now", ""});
            const int x = fpick(winner, Ab::DsWarStories, "War Stories: explore what?", opts, "", false, true);
            if (x >= 0) {
                dsEvent(E, winner, "War Stories", pn(winner) + " tells of the battle: it explores " + planetName(x) + ".", sys);
                explorePlanet(winner, x);
            } else if (x <= -2) {
                dsEvent(E, winner, "War Stories", pn(winner) + " explores the frontier in " + sysName(sys) + ".", sys);
                exploreFrontier(winner, sys);
            }
        }
    }
    // Spoils of War (the Lanefir's note): explore one of your planets
    const int lan = playerOfFaction(G, "lanefir");
    if (lan >= 0 && lan != winner && holdsNote(G, winner, lan, kNoteFaction)) {
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, winner))
            if (dsExplorable(G, x)) opts.push_back({x, planetName(x), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Keep the note", ""});
            const int x = fpick(winner, Ab::DsSpoils, "Play " + pn(lan) + "'s Spoils of War: explore which planet?", opts, "", false, true);
            if (x >= 0) {
                auto& h = pl.notes;
                const int16_t note = int16_t(makeNote(lan, kNoteFaction));
                h.erase(std::remove(h.begin(), h.end(), note), h.end());
                pl.notesInPlay.push_back(note);  // (back to the Lanefir at the start of their turn)
                dsEvent(E, winner, "Spoils of War", pn(winner) + " plays Spoils of War and explores " + planetName(x) + ".", sys);
                explorePlanet(winner, x);
            }
        }
    }
    // Moult (Cheiran): won as the defender - produce 1 ship, cheaper for each ship lost
    if (space && fac(winner, "cheiran") && winner != G.activePlayer) {
        const int lost = std::max(0, dsShipsAtStart_[winner] - G.systems[size_t(sys)].space[size_t(winner)].nonFighterShips());
        if (fyes(winner, Ab::DsMoult, "Moult: produce 1 ship in " + sysName(sys) + (lost ? " (" + std::to_string(lost) + " cheaper)?" : "?"), ""))
            dsProduceOne(winner, sys, lost, "Moult");
    }
}

// Pustule (Kyro mech): a damaged one about to be destroyed is repaired instead for an action card. Returns how many.
int Rules::dsSaveUnits(int p, UnitType t, int k, int sys, int planet, int byPlayer) {
    if (isShip(t) && planet < 0) return ds3SaveUnits(p, t, k, sys, byPlayer);  // Aegis, Combat Drills (batch 3)
    if (t != UnitType::Mech || byPlayer < 0 || !fac(p, "kyro") || !mechAbilities(G)) return 0;
    Force& f = planet >= 0 ? G.planets[size_t(planet)].units[size_t(p)] : G.systems[size_t(sys)].space[size_t(p)];
    Player& pl = G.players[size_t(p)];
    int saved = 0;
    while (saved < k && f.dmg[int(UnitType::Mech)] > 0 && !pl.actionCards.empty()) {
        std::vector<Option> opts;
        for (int16_t c : pl.actionCards) opts.push_back({c, kActionCards[c].name, "Discard it to repair the mech."});
        opts.push_back({-1, "Let it be destroyed", ""});
        const int c = fpick(p, Ab::DsPustule, "Pustule: discard an action card to repair your damaged mech instead?", opts);
        if (c < 0) break;
        discardActionCard(p, c, false);
        f.dmg[int(UnitType::Mech)] = uint8_t(f.dmg[int(UnitType::Mech)] - 1);
        ++saved;
        dsEvent(E, p, "Pustule", pn(p) + "'s mech knits itself back together.", sys);
    }
    return saved;
}

void Rules::dsUnitsDestroyed(int p, UnitType t, int k, int sys, int planet, int byPlayer) {
    if (removing_ || k <= 0) return;
    ds3UnitsDestroyed(p, t, k, sys, planet, byPlayer);  // Heroism, Vassalage (batch 3)
    ds4UnitsDestroyed(p, t, k, sys, planet, byPlayer);  // Fractal Plating (batch 4)
    ds5UnitsDestroyed(p, t, k, sys, planet, byPlayer);  // Sabre, the Lady, Psyclobea Qarnyx, Amandia Pholdis (batch 5)
    // Queen Lorena unlocks (Kortali commander): they destroyed the last of a player's units in a system
    if (byPlayer >= 0 && byPlayer < G.numPlayers && byPlayer != p && fac(byPlayer, "kortali") && p < G.numPlayers && !hasUnits(G, p, sys))
        G.players[size_t(byPlayer)].dsUnlock = true;
    // Raze (Vaylerian): another player's structures gone on a planet with their units - refill commodities
    if (isStructure(t) && planet >= 0) {
        const int v = playerOfFaction(G, "vaylerian");
        if (v >= 0 && v != p && G.planets[size_t(planet)].units[size_t(v)].total() > 0) {
            Player& vp = G.players[size_t(v)];
            if (vp.commodities < commodityCap(G, vp) && fyes(v, Ab::DsRaze, "Raze: refill your commodities?", "")) {
                const int before = vp.commodities;
                vp.commodities = int8_t(refreshTo(G, vp));
                dsEvent(E, v, "Raze", pn(v) + " refills its commodities from the ruins.", sys);
                replenished(v, vp.commodities - before);
            }
        }
    }
    if (p >= G.numPlayers || !fac(p, "cheiran")) return;
    // Chitin Hulk (Cheiran dreadnoughts): a fighter (II: or a destroyer) from each wreck
    if (t == UnitType::Dreadnought) {
        const bool two = !std::strcmp(unitDef(G, p, UnitType::Dreadnought).key, "cheiran_dreadnought2");
        for (int i = 0; i < k; ++i) {
            std::vector<Option> opts;
            if (unitsAvailable(G, p, UnitType::Fighter) > 0) opts.push_back({int(UnitType::Fighter), "1 fighter", ""});
            if (two && unitsAvailable(G, p, UnitType::Destroyer) > 0) opts.push_back({int(UnitType::Destroyer), "1 destroyer", ""});
            if (opts.empty()) break;
            opts.push_back({-1, "Nothing", ""});
            const int u = fpick(p, Ab::DsChitin, "Chitin Hulk: what crawls out of the wreck?", opts);
            if (u < 0) break;
            addUnits(p, UnitType(u), 1, sys, -1);
            dsEvent(E, p, "Chitin Hulk", pn(p) + " places " + (u == int(UnitType::Fighter) ? "a fighter" : "a destroyer") + " from the wreck.", sys);
        }
    }
    // Nauplius (Cheiran mech): an infantry where it fell
    if (t == UnitType::Mech && planet >= 0 && mechAbilities(G)) {
        const int n = std::min(k, unitsAvailable(G, p, UnitType::Infantry));
        if (n > 0 && fyes(p, Ab::DsNauplius, "Nauplius: place " + std::to_string(n) + " infantry on " + planetName(planet) + "?", "")) {
            addUnits(p, UnitType::Infantry, n, sys, planet);
            dsEvent(E, p, "Nauplius", pn(p) + "'s mech leaves infantry behind on " + planetName(planet) + ".", sys);
        }
    }
}

// ================================================================== control, activation, turns
void Rules::dsControl(int planet, int p, int old) {
    ds4Control(planet, p, old);  // Koryl Ferax (batch 4)
    ds5Control(planet, p, old);  // Broadcast Teams, President Cyhn, Mycelium Ring (batch 5)
    ds6Control(planet, p, old);  // Account Siphon (batch 6)
    if (p < 0 || p >= G.numPlayers || p == old || G.phase == Phase::Setup) return;
    Player& pl = G.players[size_t(p)];
    const int sys = G.planets[size_t(planet)].system;
    // Scour (Vaylerian): discard an action card to ready the planet (once per tactical action)
    if (fac(p, "vaylerian") && !pl.dsOnce && p == G.activePlayer && G.activeSystem >= 0 && G.planets[size_t(planet)].exhausted &&
        !pl.actionCards.empty()) {
        std::vector<Option> opts;
        for (int16_t c : pl.actionCards) opts.push_back({c, kActionCards[c].name, "Discard it to ready " + planetName(planet) + "."});
        opts.push_back({-1, "Keep your cards", ""});
        curValue_ = planetResources(G, planet) + planetInfluence(G, planet);
        const int c = fpick(p, Ab::DsScour, "Scour: discard an action card to ready " + planetName(planet) + "?", opts);
        curValue_ = 0;
        if (c >= 0) {
            discardActionCard(p, c, false);
            G.planets[size_t(planet)].exhausted = false;
            pl.dsOnce = true;
            dsEvent(E, p, "Scour", pn(p) + " readies " + planetName(planet) + ".", sys);
        }
    }
    // Yvin Korduul (Vaylerian agent): the new owner draws an action card
    const int user = agentUser(p, "vaylerian");
    if (user >= 0) {
        agentFor(user, p);
        const bool use = fyes(user, Ab::DsYvin, user == p ? "Exhaust Yvin Korduul: draw 1 action card?" : "Exhaust Yvin Korduul so " + pn(p) + " draws 1 action card?",
                              leaderText(findLeader("vaylerianagent")));
        curFor_ = -1;
        if (use) {
            spendAgent(user, "vaylerian");
            agentDone(user, p);
            drawActionCards(p, 1, "Yvin Korduul");
        }
    }
}

void Rules::dsActivated(int p, int sys) {
    if (p < 0 || p >= G.numPlayers) return;
    ds3Activated(p, sys);  // batch 3: Read the Fates, Secret Maps
    ds5Activated(p, sys);  // batch 5: Hired Guns, Raid Leaders, Rift Engines, Gift of Insight returns
    ds6Activated(p, sys);  // batch 6: Gravitic Inhibitor, Wraith Engine, Aurora Stormcaller
    ds7Activated(p, sys);  // Ceie Doleegueaunm
    Player& pl = G.players[size_t(p)];
    // Trade Alliance (the Celdauri's note) goes back when its holder activates a system with Celdauri units
    const int cel = playerOfFaction(G, "celdauri");
    if (cel >= 0 && cel != p && hasUnits(G, cel, sys)) {
        const int16_t note = int16_t(makeNote(cel, kNoteFaction));
        if (std::find(pl.notesInPlay.begin(), pl.notesInPlay.end(), note) != pl.notesInPlay.end()) {
            returnNote(p, note);
            say(p, pn(p) + "'s Trade Alliance goes back to " + pn(cel) + ".", sys);
        }
    }
    // Strike Team Coordinator (Vaylerian commander): +2 capacity this tactical action
    if (commanderUsable(G, p, "vaylerian") && shipCapacity(G, p, sys) >= 0) {
        G.dsCapBonus = int8_t(p);
        if (G.fixes >= 5) {  // 0.43.4 (owner, D7): exactly as the card - choose 1 ship with a capacity value
            std::vector<Option> opts;
            for (int t : kShipIdx) {
                if (t == int(UnitType::Fighter) || unitDef(G, p, UnitType(t)).capacity <= 0) continue;
                std::string where;
                int n = 0;
                for (size_t s = 0; s < G.systems.size(); ++s)
                    if (G.systems[s].space[size_t(p)].n[size_t(t)] > 0) {
                        if (n++ < 3) where += (where.empty() ? "" : ", ") + sysName(int(s));
                    }
                if (n) opts.push_back({t, std::string(unitTypeName(UnitType(t))), "In " + where + (n > 3 ? "..." : "") + "."});
            }
            G.fx.dsPyndilType = int8_t(opts.empty() ? -1
                                       : opts.size() == 1
                                           ? opts[0].id
                                           : fpick(p, Ab::DsPyndil, "Pyndil Gonsuul: which ship gets +2 capacity this tactical action?", opts));
            if (G.fx.dsPyndilType >= 0)
                say(p, pn(p) + "'s " + unitTypeName(UnitType(G.fx.dsPyndilType)) + " has +2 capacity this tactical action (Pyndil Gonsuul).", sys);
        }
    }
    // Spear of Vaylar (Vaylerian hero)
    if (fac(p, "vaylerian") && heroUnlocked(G, p)) {
        bool foes = false;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && hasUnits(G, q, sys)) foes = true;
        curValue_ = foes ? 1 : 0;
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (int(s) != sys && G.systems[s].hasToken(p)) opts.push_back({int(s), sysName(int(s)), ""});
        const bool use = fyes(p, Ab::DsDyln, "Purge Dyln Harthuul: +1 move and +1 to your ships' combat rolls this tactical action, and a command token back?",
                              leaderText(findLeader("vaylerianhero")));
        curValue_ = 0;
        if (use) {
            purgeHero(p);
            if (!opts.empty()) {
                const int s = opts.size() == 1 ? opts[0].id : fpick(p, Ab::DsDyln, "Spear of Vaylar: take back your command token from which system?", opts, "", true);
                G.systems[size_t(s)].tokens &= uint16_t(~(1u << p));
            }
            gainTokens(p, 1, false, "Spear of Vaylar: 1 command token.");
            pl.dsHeroSys = int8_t(sys);
            dsHeroEvent(E, G, p, pn(p) + " unleashes the Spear of Vaylar: +1 move and +1 to its ships' rolls.");
        }
    }
    for (int q = 0; q < G.numPlayers; ++q) {
        if (G.players[size_t(q)].eliminated) continue;
        Player& ql = G.players[size_t(q)];
        // Brood Pod (Cheiran): another player activates a system with their structure - produce 1 ship there
        if (q != p && techReady(q, "dscheir") && structureIn(G, q, sys) && !otherShipsIn(G, q, sys)) {
            curValue_ = p;
            const bool use = fyes(q, Ab::DsBroodPod, "Exhaust Brood Pod: produce 1 ship in " + sysName(sys) + " (" + pn(p) + " activated it)?", "");
            curValue_ = 0;
            if (use) {
                exhaustTech(q, "dscheir");
                int discount = 0;
                std::vector<Option> rem;  // id = planet * 16 + type
                const bool v12 = G.fixes >= 12;
                for (int16_t x : G.systems[size_t(sys)].planets) {
                    // (0.44.8, fixes 12: each piece its own choice - two PDS on a planet were one option; Byssus: the
                    // Cheiran's mechs on their planets count as structures)
                    std::vector<UnitType> kinds = {UnitType::PDS, UnitType::SpaceDock};
                    if (v12 && fac(q, "cheiran") && G.planets[size_t(x)].owner == q) kinds.push_back(UnitType::Mech);
                    for (UnitType u : kinds)
                        for (int i = 0; i < G.planets[size_t(x)].units[size_t(q)].count(u); ++i)
                            rem.push_back({v12 ? (x * 16 + int(u)) * 8 + std::min(i, 7) : x * 16 + int(u),
                                           std::string(unitTypeName(u)) + " on " + planetName(x), "3 cheaper"});
                }
                if (!rem.empty()) {
                    std::vector<Option> uniq;
                    for (const Option& o : rem)
                        if (std::none_of(uniq.begin(), uniq.end(), [&](const Option& u) { return u.id == o.id; })) uniq.push_back(o);
                    for (int id0 : fpickMany(q, Ab::DsBroodPod, "Brood Pod: remove structures here to make it cheaper (3 each)?", uniq, 0, int(uniq.size()))) {
                        const int id = v12 ? id0 / 8 : id0;
                        const int x = id / 16;
                        removing_ = true;
                        destroyUnits(q, UnitType(id % 16), 1, sys, x, -1, "Brood Pod");
                        removing_ = false;
                        discount += 3;
                    }
                }
                dsProduceOne(q, sys, discount, "Brood Pod");
            }
        }
        // Minuteman (Celdauri mech): 1 trade good or commodity for a mech on a planet with their dock here
        if (fac(q, "celdauri") && mechAbilities(G) && unitsAvailable(G, q, UnitType::Mech) > 0 && ql.tradeGoods + ql.commodities > 0) {
            std::vector<Option> opts;
            for (int16_t x : G.systems[size_t(sys)].planets)
                if (G.planets[size_t(x)].units[size_t(q)].count(UnitType::SpaceDock)) opts.push_back({x, planetName(x), ""});
            if (!opts.empty()) {
                opts.push_back({-1, "No", ""});
                curValue_ = p;
                const int x = fpick(q, Ab::DsMinuteman, "Minuteman: pay 1 commodity or trade good to place a mech in " + sysName(sys) + "?", opts, "", false, true);
                curValue_ = 0;
                if (x >= 0) {
                    if (ql.commodities > 0) ql.commodities = int8_t(ql.commodities - 1);
                    else ql.tradeGoods = int8_t(ql.tradeGoods - 1);
                    addUnits(q, UnitType::Mech, 1, sys, x);
                    dsEvent(E, q, "Minuteman", pn(q) + " calls up a mech on " + planetName(x) + ".", sys);
                }
            }
        }
        // Titus Flavius (Celdauri hero): a space dock, commodities to trade goods, and Production here
        if (fac(q, "celdauri") && heroUnlocked(G, q) && sys != G.mecatol()) {
            std::vector<Option> opts;
            for (int16_t x : G.systems[size_t(sys)].planets)
                if (G.planets[size_t(x)].owner == q && !G.planets[size_t(x)].units[size_t(q)].count(UnitType::SpaceDock) && !isDMZ(G, x))
                    opts.push_back({x, planetName(x), ""});
            if (!opts.empty() && unitsAvailable(G, q, UnitType::SpaceDock) > 0) {
                opts.push_back({-1, "Not now", ""});
                curValue_ = p;
                const int x = fpick(q, Ab::DsTitus, "Purge Titus Flavius: a space dock on which planet (then trade goods and Production here)?", opts,
                                    leaderText(findLeader("celdaurihero")), false, true);
                curValue_ = 0;
                if (x >= 0) {
                    purgeHero(q);
                    addUnits(q, UnitType::SpaceDock, 1, sys, x);
                    const int c = ql.commodities;
                    ql.commodities = 0;
                    if (c > 0) gainTG(q, c);
                    dsHeroEvent(E, G, q, pn(q) + " founds a trade port on " + planetName(x) + " and converts " + std::to_string(c) + " commodities.");
                    production(q, sys, productionCapacity(G, q, sys), false);
                }
            }
        }
    }
    ds2Activated(p, sys);  // batch 2: Sanctification Field, Omniscience Field, Recycled Materials, Ky'vir
    // Fillipo Rois (Tnelis commander): a hit on one of their non-fighter ships for 1 more move on one ship
    if (commanderUsable(G, p, "tnelis") && fac(p, "tnelis")) {
        std::vector<Option> opts;  // id = system * 16 + type
        for (size_t s = 0; s < G.systems.size(); ++s)
            for (int t : kShipIdx)
                if (t != int(UnitType::Fighter) && G.systems[s].space[size_t(p)].n[t] > G.systems[s].space[size_t(p)].dmg[t] && unitSustains(G, p, UnitType(t)))
                    opts.push_back({int(s) * 16 + t, std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)), "It becomes damaged."});
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            const int c = fpick(p, Ab::DsFillipo, "Fillipo Rois: damage one of your ships to give a ship +1 move this tactical action?", opts);
            if (c >= 0) {
                G.systems[size_t(c / 16)].space[size_t(p)].dmg[c % 16]++;
                G.fx.fillipo = int8_t(p);
                dsEvent(E, p, "Fillipo Rois", pn(p) + " pushes its engines: one ship may move 1 further.", sys);
            }
        }
    }
    // Henry Storcher (Celdauri commander): a commodity when activating a system with their dock
    if (commanderUsable(G, p, "celdauri") && docksIn(G, p, sys) && pl.commodities < commodityCap(G, pl)) {
        { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + 1); commodityGained(p, cb_); }
        say(p, pn(p) + " gains 1 commodity (Henry Storcher).", sys);
    }
}

void Rules::dsEndTactical(int p, int sys) {
    G.dsCapBonus = -1;
    // 0.43.0: units put on a defended planet (Daedalon, Meteor Slings) whose invasion never came (the space combat was
    // lost) still fight there - two players' ground forces can't share a planet
    if (p >= 0 && p < G.numPlayers)
        for (int pl : std::vector<int>(dsSlingPlanets_)) {
            bool foes = neutralGroundOn(G, pl) > 0;
            for (int q = 0; q < G.numPlayers; ++q) foes = foes || (q != p && groundForcesOn(G, q, pl) > 0);
            if (foes && G.planets[size_t(pl)].units[size_t(p)].groundForces() > 0) dsResolvePlanet(p, pl);
            // (0.43.0 debug passes: with nobody there to fight, the invasion's last step still gives them the planet -
            // a Daedalon mech on a planet with only a space dock used to sit there beside its owner; fixes >= 2)
            else if (!foes && G.fixes >= 2 && G.planets[size_t(pl)].units[size_t(p)].groundForces() > 0 && G.planets[size_t(pl)].owner != p &&
                     !((G.planets[size_t(pl)].coexist >> p) & 1) && !G.planets[size_t(pl)].destroyed)
                setControl(pl, p);
        }
    dsSlingPlanets_.clear();
    ds3Vassal_ = 0;
    if (p < 0 || p >= G.numPlayers || sys < 0) return;
    if (breakthroughsOn(G)) dsbtEndTactical(p, sys);  // Professional Intrigue (0.43.0)
    ds3EndTactical(p, sys);  // Secret Maps (batch 3)
    ds4EndTactical(p, sys);  // Nightshade Vanguard (batch 4)
    ds5EndTactical(p, sys);  // Hired Guns, Raid Leaders (batch 5)
    ds6EndTactical(p, sys);  // Cunning, Traps go back (batch 6)
    ds7EndTactical(p, sys);  // the Ghoti home, Gauss Cannons blockaded
    Player& pl = G.players[size_t(p)];
    // Memory of Dusk (Lanefir flagship): explore one of their planets here
    if (flagshipAbility(G, p, "lanefir") && flagshipIn(G, p, sys)) {
        std::vector<Option> opts;
        for (int16_t x : G.systems[size_t(sys)].planets)
            if (G.planets[size_t(x)].owner == p && dsExplorable(G, x)) opts.push_back({x, planetName(x), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Not now", ""});
            const int x = fpick(p, Ab::DsMemoryDusk, "Memory of Dusk: explore a planet here?", opts, "", false, true);
            if (x >= 0) {
                dsEvent(E, p, "Memory of Dusk", pn(p) + "'s flagship explores " + planetName(x) + ".", sys);
                explorePlanet(p, x);
            }
        }
    }
    // George Nobin (Celdauri agent): 2 trade goods or commodities for a space dock here
    const int user = agentUser(p, "celdauri");
    if (user >= 0 && unitsAvailable(G, p, UnitType::SpaceDock) > 0 && pl.tradeGoods + pl.commodities >= 2) {
        std::vector<Option> opts;
        for (int16_t x : G.systems[size_t(sys)].planets)
            if (G.planets[size_t(x)].owner == p && !G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock) && !isDMZ(G, x))
                opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + " res / " + std::to_string(planetInfluence(G, x)) + " inf"});
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            agentFor(user, p);
            const int x = fpick(user, Ab::DsNobin, user == p ? "Exhaust George Nobin: pay 2 to place a space dock here?" : "Exhaust George Nobin for " + pn(p) + "?", opts,
                                leaderText(findLeader("celdauriagent")), false, true);
            curFor_ = -1;
            if (x >= 0) {
                spendAgent(user, "celdauri");
                agentDone(user, p);
                for (int i = 0; i < 2; ++i) {
                    if (pl.commodities > 0) pl.commodities = int8_t(pl.commodities - 1);
                    else pl.tradeGoods = int8_t(pl.tradeGoods - 1);
                }
                addUnits(p, UnitType::SpaceDock, 1, sys, x);
                dsEvent(E, user, "George Nobin", pn(p) + " opens a space dock on " + planetName(x) + ".", sys);
            }
        }
    }
}

void Rules::dsTurnStart(int p) {
    for (Player& q : G.players) q.dsOnce = q.dsExosOnce = false;
    if (p < 0 || p >= G.numPlayers) return;
    Player& pl = G.players[size_t(p)];
    pl.dsPilgrim = false;
    ds3TurnStart(p);  // Sal Gavda (batch 3)
    ds4TurnStart(p);  // batch 4: Captain Dust, Yudri Sukhov, Recursive Worm, False Flag Operations, Treasure Hunters
    ds5TurnStart(p);  // batch 5: Amandia Pholdis
    ds6TurnStart(p);  // batch 6: Oro-Zhin Elite
    ds7TurnStart(p);  // Ghoti Relay, Rapid Excavation
    // Combinatorial Bypass goes back to the Kolume at the start of its holder's next turn
    const int ko = playerOfFaction(G, "kolume");
    if (ko >= 0 && ko != p) {
        const int16_t note = int16_t(makeNote(ko, kNoteFaction));
        if (std::find(pl.notesInPlay.begin(), pl.notesInPlay.end(), note) != pl.notesInPlay.end()) returnNote(p, note);
    }
    // Spoils of War goes back to the Lanefir at the start of their turn
    if (fac(p, "lanefir"))
        for (int q = 0; q < G.numPlayers; ++q) {
            const int16_t note = int16_t(makeNote(p, kNoteFaction));
            auto& v = G.players[size_t(q)].notesInPlay;
            if (q != p && std::find(v.begin(), v.end(), note) != v.end()) returnNote(q, note);
        }
    // Troubadour (Lanefir mech): a relic fragment for a mech on one of their planets
    if (fac(p, "lanefir") && mechAbilities(G) && fragmentCount(G, p) > 0 && unitsAvailable(G, p, UnitType::Mech) > 0) {
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p))
            if (!isDMZ(G, x)) opts.push_back({x, planetName(x), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Not now", ""});
            const int x = fpick(p, Ab::DsTroubadour, "Troubadour: purge 1 relic fragment to place a mech?", opts, "", false, true);
            if (x >= 0) {
                purgeFragments(p, 1, -1, "Troubadour");
                addUnits(p, UnitType::Mech, 1, G.planets[size_t(x)].system, x);
                dsEvent(E, p, "Troubadour", pn(p) + " places a mech on " + planetName(x) + ".", G.planets[size_t(x)].system);
                if (G.fixes >= 2 && fyes(p, Ab::DsTroubadour, "Troubadour: end your turn now?",
                                         "You take no action this turn but you don't pass - you play again when your turn comes round."))
                    dsEndTurnNow_ = true;
            }
        }
    }
    (void)pl;
}

void Rules::dsTurnEnd(int p) {
    if (p < 0 || p >= G.numPlayers) return;
    if (breakthroughsOn(G)) {
        dsbtTurnEnd(p);         // Arms Brokerage's loan back, Reverie Implants (0.43.0)
        dsbtTurnEndCheiran(p);  // Matriphagy
    }
    Player& pl = G.players[size_t(p)];
    pl.dsHeroSys = -1;  // Spear of Vaylar ends with the turn
    G.dsCapBonus = -1;
    ds2TurnEnd(p);  // Automatons
    ds3TurnEnd(p);  // Encryption Key, Gledge Base, Ancient Blueprints (batch 3)
    ds5TurnEnd(p);  // Axis Orders (batch 5)
    ds6TurnEnd(p);  // Vasra Ivo (batch 6)
    // Emergency Mobilization (Celdauri): a strategy token to take a command token back from a system with a dock
    if (techReady(p, "dsceldr") && pl.strategy > 0) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (G.systems[s].hasToken(p) && docksIn(G, p, int(s))) opts.push_back({int(s), sysName(int(s)), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            const int s = fpick(p, Ab::DsEmergencyMob, "Emergency Mobilization: spend 1 strategy token to remove your command token from which system?", opts,
                                "It goes back to your reinforcements - you may activate that system again.", true);
            if (s >= 0) {
                exhaustTech(p, "dsceldr");
                spendStrategy(p, 1);
                G.systems[size_t(s)].tokens &= uint16_t(~(1u << p));
                dsEvent(E, p, "Emergency Mobilization",
                        G.fixes >= 12 ? pn(p) + " removes its command token from " + sysName(s) + " - it may activate it again."
                                      : pn(p) + " takes its command token back from " + sysName(s) + ".",
                        s);
            }
        }
    }
    // Operator Kkavras (Cheiran agent): move one of their command tokens to a neighbouring system with their structure
    const int user = agentUser(p, "cheiran");
    if (user >= 0) {
        std::vector<Option> opts;  // id = from * 1000 + to
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (!G.systems[s].hasToken(p)) continue;
            for (int16_t a : G.systems[s].adj)
                if (!G.systems[size_t(a)].hasToken(p) && structureIn(G, p, a)) opts.push_back({int(s) * 1000 + a, sysName(int(s)) + " -> " + sysName(a), ""});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            agentFor(user, p);
            const int c = fpick(user, Ab::DsKkavras, "Exhaust Operator Kkavras: move a command token?", opts, leaderText(findLeader("cheiranagent")));
            curFor_ = -1;
            if (c >= 0) {
                spendAgent(user, "cheiran");
                agentDone(user, p);
                G.systems[size_t(c / 1000)].tokens &= uint16_t(~(1u << p));
                G.systems[size_t(c % 1000)].tokens = uint16_t(G.systems[size_t(c % 1000)].tokens | (1u << p));
                dsEvent(E, user, "Operator Kkavras", pn(p) + " moves a command token from " + sysName(c / 1000) + " to " + sysName(c % 1000) + ".");
            }
        }
    }
}

// ================================================================== exploration, relics
void Rules::dsExplored(int p, int planet) {
    if (p < 0 || p >= G.numPlayers) return;
    ds3Explored(p, planet);  // batch 3: Fortune Seekers, Ancient Blueprints, Clodho, Impressment Programs
    Player& pl = G.players[size_t(p)];
    if (owns(p, "dslaner") && pl.dsTokens < 12) pl.dsTokens = int8_t(pl.dsTokens + 1);  // ATS Armaments
    // Master Halbert (Lanefir commander): 1 infantry on the explored planet
    if (planet >= 0 && commanderUsable(G, p, "lanefir") && unitsAvailable(G, p, UnitType::Infantry) > 0 && G.planets[size_t(planet)].owner == p &&
        !isDMZ(G, planet)) {
        addUnits(p, UnitType::Infantry, 1, G.planets[size_t(planet)].system, planet);
        say(p, pn(p) + " places 1 infantry on " + planetName(planet) + " (Master Halbert).", G.planets[size_t(planet)].system);
    }
}

int Rules::dsRedraw(int p, int type, int c) {
    const int user = agentUser(p, "lanefir");
    if (user < 0 || c < 0) return c;
    agentFor(user, p);
    curValue_ = c;
    const bool use = fyes(user, Ab::DsVassa, std::string("Exhaust Vassa Hagi: discard ") + kExploreCards[c].name + " and draw another?",
                          leaderText(findLeader("lanefiragent")));
    curValue_ = 0;
    curFor_ = -1;
    if (!use) return c;
    spendAgent(user, "lanefir");
    agentDone(user, p);
    G.exploreDiscard[size_t(type)].push_back(int16_t(c));
    const int n = drawExplore(type);
    say(p, pn(p) + " discards " + kExploreCards[c].name + " and draws again (Vassa Hagi).");
    return n >= 0 ? n : c;
}

bool Rules::dsRelicGained(int p, int r) {
    if (!fac(p, "lanefir")) return false;
    // A New Edifice: the relic is purged; explore up to 3 of their planets
    dsEvent(E, p, "A New Edifice", pn(p) + " purges " + kRelics[r].name + " and explores up to 3 planets instead.");
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, p))
        if (dsExplorable(G, x)) opts.push_back({x, planetName(x), ""});
    if (!opts.empty())
        for (int x : fpickMany(p, Ab::DsEdifice, "A New Edifice: explore up to 3 of your planets", opts, 0, std::min<int>(3, int(opts.size())),
                               // 0.43.0 (D10): say what happened to the relic, and why the Lanefir have no specialties
                               "The Lanefir never keep relics: " + std::string(kRelics[r].name) + " is purged (out of the game) instead. "
                               "Their planets have no technology specialties; Iconoclasm (purge a fragment) is used for them when you research.",
                               true))
            explorePlanet(p, x);
    return true;
}

void Rules::dsFragmentsPurged(int p, int n) {
    if (n <= 0) return;
    G.fragsPurged = int16_t(G.fragsPurged + n);
    if (p >= 0 && p < G.numPlayers && owns(p, "dslaner") && G.players[size_t(p)].dsTokens < 12)  // ATS Armaments
        G.players[size_t(p)].dsTokens = int8_t(G.players[size_t(p)].dsTokens + 1);
}

// ================================================================== strategy cards, status, agendas
bool Rules::dsZealous(int q) {
    if (!fac(q, "kortali") || freeSecondary_ || !fyes(q, Ab::DsZealous, "Zealous: resolve Warfare's primary ability instead?", strategyPrimary(5)))
        return false;
    say(q, pn(q) + " resolves Warfare's primary ability (Zealous).");
    return true;
}

// Contagion (Kyro): after either ability of Politics - 1 infantry invades a planet in or next to a system with theirs.
void Rules::dsAfterPolitics(int p) {
    if (!fac(p, "kyro") || unitsAvailable(G, p, UnitType::Infantry) <= 0) return;
    std::vector<char> zone(G.systems.size(), 0);
    for (int x : controlledPlanets(G, p)) {
        const int s = G.planets[size_t(x)].system;
        if (s < 0) continue;
        zone[size_t(s)] = 1;
        for (int16_t a : G.systems[size_t(s)].adj) zone[size_t(a)] = 1;
    }
    std::vector<Option> opts;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.system < 0 || !zone[size_t(ps.system)] || ps.destroyed || isDMZ(G, int(x)) || ps.owner == p) continue;
        if (int(x) >= 0 && G.systems[size_t(ps.system)].isHyperlane()) continue;
        if (ps.system == G.players[size_t(ps.owner >= 0 ? ps.owner : p)].homeSystem && ps.owner >= 0) continue;
        int foes = neutralGroundOn(G, int(x));
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p) foes += groundForcesOn(G, q, int(x));
        opts.push_back({int(x), planetName(int(x)), foes ? std::to_string(foes) + " ground forces to fight" : "No defenders"});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "Hold back", ""});
    const int x = fpick(p, Ab::DsContagion, "Contagion: commit 1 infantry to which planet?", opts, "It invades at once.", false, true);
    if (x < 0) return;
    dsEvent(E, p, "Contagion", pn(p) + " seeds " + planetName(x) + " with 1 infantry.", G.planets[size_t(x)].system);
    dsInvade(p, x);
}

void Rules::dsStatusStart() {
    ds3StatusStart();  // batch 3: Sentient Datapool, Combat Drills returns
    ds4StatusStart();  // batch 4: Binding Debts, Komdar Borodin, Incite Revolution returns
    ds5StatusStart();  // batch 5: Axis Orders and Industry Secrets go back
    ds6StatusStart();  // batch 6: Holding Company, Targeted Acquisition
    ds7StatusStart();  // Ghoti Relay goes back
    // Blessing of the Queens goes back to the Kortali
    const int k = playerOfFaction(G, "kortali");
    if (k >= 0)
        for (int q = 0; q < G.numPlayers; ++q) {
            const int16_t note = int16_t(makeNote(k, kNoteFaction));
            auto& v = G.players[size_t(q)].notesInPlay;
            if (q != k && std::find(v.begin(), v.end(), note) != v.end()) returnNote(q, note);
        }
    // Brikk Strain (Kyro hero): attach it to one of their strategy cards
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!fac(p, "kyro") || !heroUnlocked(G, p) || G.players[size_t(p)].cards.empty()) continue;
        std::vector<Option> opts;
        for (int8_t c : G.players[size_t(p)].cards) opts.push_back({c, std::to_string(c + 1) + " " + strategyName(c), ""});
        opts.push_back({-1, "Not now", ""});
        const int c = fpick(p, Ab::DsSpeygh, "Speygh: attach Brikk Strain to which of your strategy cards?", opts, leaderText(findLeader("kyrohero")));
        if (c < 0) continue;
        purgeHero(p);
        G.blight = int8_t(c);
        G.blightOwner = int8_t(p);
        dsHeroEvent(E, G, p, pn(p) + " poisons " + strategyName(c) + ": it is initiative 9 now, and trade goods on it go to " + pn(p) + ".");
    }
}

// Vector Programs (Kyro): trade goods onto the strategy cards before they are chosen.
void Rules::dsStrategyStart() {
    ds5StrategyStart();  // Prescient Memories (batch 5)
    // Blessing of the Ancients (Rhodun hero): keep 1 of the borrowed technologies
    for (int p = 0; p < G.numPlayers; ++p) {
        Player& pl = G.players[size_t(p)];
        if (pl.dsTempTechs.empty()) continue;
        std::vector<Option> opts;
        for (int16_t t : pl.dsTempTechs) opts.push_back({t, kTechs[t].name, techSummary(t)});
        const int keep = opts.size() == 1 ? opts[0].id : fpick(p, Ab::DsSaintKeep, "Blessing of the Ancients: keep which technology?", opts);
        for (int16_t t : pl.dsTempTechs)
            if (t != keep) {
                pl.techs.erase(std::remove(pl.techs.begin(), pl.techs.end(), t), pl.techs.end());
                pl.exhaustedTechs.erase(std::remove(pl.exhaustedTechs.begin(), pl.exhaustedTechs.end(), t), pl.exhaustedTechs.end());
            }
        pl.dsTempTechs.clear();
        refreshUnits(p);
        say(p, pn(p) + " keeps " + kTechs[keep].name + " (Blessing of the Ancients).");
    }
    for (int p : fromSpeaker(G)) {
        if (!owns(p, "dskyroy")) continue;
        std::vector<Option> opts;
        for (int c = 0; c < kStrategyCards; ++c)
            if (G.cardOwner[c] < 0) opts.push_back({c, std::to_string(c + 1) + " " + strategyName(c), std::to_string(G.cardTradeGoods[c]) + " trade goods on it"});
        if (opts.empty()) continue;
        // 0.43.0 (D18): first any number of their own trade goods, on any combination of cards (one at a time)
        Player& kp = G.players[size_t(p)];
        for (int k = 0; G.fixes >= 2 && k < 20 && kp.tradeGoods > 0; ++k) {
            std::vector<Option> own = opts;
            for (Option& o : own) o.detail = std::to_string(G.cardTradeGoods[o.id]) + " trade goods on it";
            own.push_back({-1, "Done", ""});
            const int c = fpick(p, Ab::DsVector, "Vector Programs: put 1 of your own trade goods on a strategy card?", own,
                                "You have " + std::to_string(kp.tradeGoods) + ". Whoever takes the card takes them (Brikk Strain's card: they go to you).");
            if (c < 0) break;
            kp.tradeGoods = int8_t(kp.tradeGoods - 1);
            G.cardTradeGoods[c] = int8_t(G.cardTradeGoods[c] + 1);
            dsEvent(E, p, "Vector Programs", pn(p) + " puts 1 of its trade goods on " + strategyName(c) + ".");
        }
        for (Option& o : opts) o.detail = std::to_string(G.cardTradeGoods[o.id]) + " trade goods on it";
        const int c = fpick(p, Ab::DsVector, "Vector Programs: put 2 trade goods from the supply on which strategy card?", opts);
        G.cardTradeGoods[c] = int8_t(G.cardTradeGoods[c] + 2);
        dsEvent(E, p, "Vector Programs", pn(p) + " puts 2 trade goods on " + strategyName(c) + ".");
    }
}

// Kyro Rider (the Kyro's note): no vote, a prediction - 3 infantry if right.
void Rules::dsAgendaRevealed(int a) {
    for (int& r : dsRiderGuess_) r = -1;
    ds3AgendaRevealed();  // Quaxdol Junitas (batch 3)
    ds5AgendaRevealed(a);  // Radiance, Edyn Rider, Encrypted Trade Hub (batch 5)
    // Conspirators (Rhodun): once per agenda phase, vote after the speaker
    for (int z = 0; z < G.numPlayers; ++z) {
        if (!fac(z, "zealots") || z == G.speaker || G.dsConspired == G.round || lastVoter_ >= 0) continue;
        const int prev = curAgenda_;
        curAgenda_ = a;
        const bool use = fyes(z, Ab::DsConspirators, "Conspirators: vote after the speaker on " + std::string(kAgendas[a].name) + "?", "Once per agenda phase.");
        curAgenda_ = prev;
        if (!use) continue;
        G.dsConspired = int16_t(G.round);
        lastVoter_ = z;
        dsEvent(E, z, "Conspirators", pn(z) + " will vote last on this agenda.");
    }
    const int k = playerOfFaction(G, "kyro");
    if (k < 0) return;
    const std::vector<Option> outs = agendaOutcomes(a);
    if (outs.empty()) return;
    for (int q : clockwiseFrom(G, G.speaker)) {
        if (q == k || G.players[size_t(q)].eliminated || !holdsNote(G, q, k, kNoteFaction)) continue;
        const int prev = curAgenda_;
        curAgenda_ = a;
        const bool play = fyes(q, Ab::DsKyroRider, "Play " + pn(k) + "'s Kyro Rider on " + kAgendas[a].name + "?",
                               "You won't vote on this agenda. Predict the outcome: if you're right, place 3 infantry on a planet you control.");
        if (play) {
            const int guess = fpick(q, Ab::DsKyroRider, "Kyro Rider: predict the outcome of " + std::string(kAgendas[a].name), outs);
            dsRiderGuess_[q] = guess;
            cannotVote_ = uint8_t(cannotVote_ | (1u << q));
            returnNote(q, makeNote(k, kNoteFaction));
            dsEvent(E, q, "Kyro Rider", pn(q) + " plays the Kyro Rider and predicts: " + outcomeName(a, guess) + ".");
        }
        curAgenda_ = prev;
    }
}

// Silas Deriga (Kyro commander): infantry off the board for more votes.
void Rules::dsAfterVoting(std::vector<int>& voted, std::vector<int>& counts) {
    ds3AfterVoting(voted, counts);  // Lachis (batch 3)
    ds4AfterVoting(voted, counts);  // Illusory Presence (batch 4)
    for (int p = 0; p < G.numPlayers; ++p) {
        if (size_t(p) >= voted.size() || voted[size_t(p)] == kNoVote || !commanderUsable(G, p, "kyro") || !fac(p, "kyro")) continue;
        const int inf = std::min(4, unitsOnBoard(G, p, UnitType::Infantry));
        if (inf <= 0) continue;
        std::vector<Option> opts;
        for (int k = 0; k <= inf; ++k) opts.push_back({k, std::to_string(k), k ? std::to_string(k) + " more votes" : "None"});
        const int k = fpick(p, Ab::DsSilas, "Silas Deriga: remove infantry from the board for 1 more vote each?", opts,
                            // 0.43.0 (D16): when the votes come in
                            "These votes are added after everyone has voted, before the outcome is counted.");
        int done = 0;
        removing_ = true;
        for (size_t x = 0; x < G.planets.size() && done < k; ++x) {
            const int here = std::min(k - done, int(G.planets[x].units[size_t(p)].count(UnitType::Infantry)));
            if (here > 0) {
                destroyUnits(p, UnitType::Infantry, here, G.planets[x].system, int(x), -1, "Silas Deriga");
                done += here;
            }
        }
        removing_ = false;
        if (done > 0) {
            counts[size_t(p)] += done;
            dsEvent(E, p, "Silas Deriga", pn(p) + " casts " + std::to_string(done) + " more votes (added after everyone voted).");
        }
    }
}

void Rules::dsAgendaResolved(int outcome, const std::vector<int>& voted) {
    ds3AgendaResolved(outcome, voted);  // Quaxdol Junitas unlocks (batch 3)
    ds5AgendaResolved(outcome, voted);  // Radiance, Unity Algorithm, Edyn Rider (batch 5)
    for (int q = 0; q < G.numPlayers; ++q) {
        Player& pl = G.players[size_t(q)];
        // Kyro Rider pays out
        if (dsRiderGuess_[q] >= 0) {
            const int k = playerOfFaction(G, "kyro");
            if (dsRiderGuess_[q] == outcome) {
                std::vector<Option> where;
                for (int x : controlledPlanets(G, q))
                    if (!isDMZ(G, x)) where.push_back({x, planetName(x), ""});
                const int n = std::min(3, unitsAvailable(G, q, UnitType::Infantry));
                if (!where.empty() && n > 0) {
                    const int x = where.size() == 1 ? where[0].id : fpick(q, Ab::DsKyroRider, "Kyro Rider was right: place 3 infantry on which planet?", where, "", false, true);
                    addUnits(q, UnitType::Infantry, n, G.planets[size_t(x)].system, x);
                }
                say(q, pn(q) + "'s Kyro Rider was right: " + std::to_string(n) + " infantry.");
            } else {
                say(q, pn(q) + "'s Kyro Rider was wrong.");
            }
            (void)k;
            dsRiderGuess_[q] = -1;
        }
        // Indoctrination Teams (Kyro): 2 infantry after an outcome they voted for
        if (owns(q, "dskyrog") && size_t(q) < voted.size() && voted[size_t(q)] == outcome && outcome != kNoVote && unitsAvailable(G, q, UnitType::Infantry) > 0) {
            std::vector<Option> where;
            for (int x : controlledPlanets(G, q))
                if (!isDMZ(G, x)) where.push_back({x, planetName(x), ""});
            if (!where.empty()) {
                const int x = where.size() == 1 ? where[0].id : fpick(q, Ab::DsIndoctrination, "Indoctrination Teams: place 2 infantry on which planet?", where, "", false, true);
                const int k = std::min(2, unitsAvailable(G, q, UnitType::Infantry));
                addUnits(q, UnitType::Infantry, k, G.planets[size_t(x)].system, x);
                say(q, pn(q) + " places " + std::to_string(G.fixes >= 10 ? k : 2) + " infantry on " + planetName(x) + " (Indoctrination Teams).");
            }
        }
        (void)pl;
    }
}

// ================================================================== component actions
void Rules::dsOptions(int p, std::vector<Option>& o) {
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    const Player& pl = G.players[size_t(p)];
    // Blessing of the Queens (the Kortali's note): take a command token back
    const int k = playerOfFaction(G, "kortali");
    if (k >= 0 && k != p && holdsNote(G, p, k, kNoteFaction)) {
        bool any = false;
        for (const SystemState& s : G.systems) any = any || s.hasToken(p);
        if (any) add(Ab::DsBlessing, "Blessing of the Queens", "Play it face up: take 1 of your command tokens back from the board.");
    }
    // Spark Thrusters (Lanefir)
    if (owns(p, "dslaneb") && (techReady(p, "dslaneb") ? pl.strategy > 0 || fragmentCount(G, p) > 0 : fragmentCount(G, p) > 0))
        add(Ab::DsSpark, "Spark Thrusters", "Spend a strategy token (and exhaust it) or a relic fragment: move 1 ship to a neighbouring system without other ships.");
    // Thakt Clqua (Cheiran hero)
    if (fac(p, "cheiran") && heroUnlocked(G, p)) add(Ab::DsThakt, "Hero: Thakt Clqua", "Your starting units again, placed where your structures are. Purged afterwards.");
    // The Venerable (Lanefir hero)
    if (fac(p, "lanefir") && heroUnlocked(G, p) && !G.relicDeck.empty())
        add(Ab::DsVenerable, "Hero: The Venerable", "Reveal 3 relics: a command token for each relic in play, and swaps. Purged afterwards.");
    ds2Options(p, o);  // batch 2 (rules_ds2.cpp)
}

bool Rules::dsAction(int p, Ab ab) {
    Player& pl = G.players[size_t(p)];
    switch (ab) {
        case Ab::DsBlessing: {
            const int k = playerOfFaction(G, "kortali");
            if (k < 0 || !holdsNote(G, p, k, kNoteFaction)) return false;
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (G.systems[s].hasToken(p)) opts.push_back({int(s), sysName(int(s)), ""});
            if (opts.empty()) return false;
            const int s = opts.size() == 1 ? opts[0].id : fpick(p, Ab::DsBlessing, "Blessing of the Queens: take back your command token from which system?", opts, "", true);
            G.systems[size_t(s)].tokens &= uint16_t(~(1u << p));
            const int16_t note = int16_t(makeNote(k, kNoteFaction));
            pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), note), pl.notes.end());
            pl.notesInPlay.push_back(note);  // (back to the Kortali at the start of the status phase)
            dsEvent(E, p, "Blessing of the Queens", pn(p) + " takes its command token back from " + sysName(s) + ".", s);
            return true;
        }
        case Ab::DsSpark: {
            std::vector<Option> opts;  // id = (from * 100 + to) * 16 + type
            for (size_t s = 0; s < G.systems.size(); ++s) {
                const Force& f = G.systems[s].space[size_t(p)];
                if (!f.ships()) continue;
                for (int a : routeNeighbors(G, p, int(s))) {
                    if (G.systems[size_t(a)].isHyperlane() || otherShipsIn(G, p, a)) continue;
                    if (G.fixes >= 11 && !canEndMoveIn(G, p, a)) continue;  // (0.44.7: not into a supernova, an asteroid field without Antimass...)
                    for (int t : kShipIdx)
                        if (f.n[t]) opts.push_back({(int(s) * 100 + a) * 16 + t, std::string(unitTypeName(UnitType(t))) + ": " + sysName(int(s)) + " -> " + sysName(a), ""});
                }
            }
            if (opts.empty()) return false;
            const int c = fpick(p, Ab::DsSparkMove, "Spark Thrusters: move which ship where?", opts);
            const bool canToken = techReady(p, "dslaneb") && pl.strategy > 0, canFrag = fragmentCount(G, p) > 0;
            bool token = canToken;
            if (canToken && canFrag)
                token = fpick(p, Ab::DsSpark, "Spark Thrusters: pay with?", {{1, "A strategy token (exhausts the card)", ""}, {0, "A relic fragment", ""}}) == 1;
            if (token) {
                spendStrategy(p, 1);
                exhaustTech(p, "dslaneb");
            } else {
                purgeFragments(p, 1, -1, "Spark Thrusters");
            }
            const int from = (c / 16) / 100, to = (c / 16) % 100;
            const UnitType t = UnitType(c % 16);
            Force& src = G.systems[size_t(from)].space[size_t(p)];
            const bool dmg = src.dmg[int(t)] > 0 && src.dmg[int(t)] >= src.n[int(t)];
            src.remove(t, 1, dmg);
            G.systems[size_t(to)].space[size_t(p)].add(t, 1);
            if (dmg) G.systems[size_t(to)].space[size_t(p)].dmg[int(t)]++;
            removeExcessCapacity(p, from);
            if (G.fixes >= 2) enforceFleetIn(p, to);  // 0.43.0 (debug passes): the fleet pool counts there too
            dsEvent(E, p, "Spark Thrusters", pn(p) + "'s " + unitTypeName(t) + " jumps from " + sysName(from) + " to " + sysName(to) + ".", to);
            return true;
        }
        case Ab::DsThakt: {
            if (!heroUnlocked(G, p)) return false;
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (structureIn(G, p, int(s)) && !otherShipsIn(G, p, int(s))) opts.push_back({int(s), sysName(int(s)), ""});
            if (opts.empty()) return false;
            const int s = opts.size() == 1 ? opts[0].id : fpick(p, Ab::DsThakt, "Thakt Clqua: the new swarm gathers in which system?", opts, "", true);
            purgeHero(p);
            std::vector<int> planets;
            for (int16_t x : G.systems[size_t(s)].planets)
                if (G.planets[size_t(x)].owner == p && !isDMZ(G, x)) planets.push_back(x);
            const int home = pl.homeSystem;
            Force made;
            for (const StartUnit& u : kFactions[pl.faction].fleet) {
                if (u.count <= 0) continue;
                const int n = std::min<int>(u.count, unitsAvailable(G, p, u.type));
                if (n <= 0) continue;
                if (isShip(u.type)) {
                    addUnits(p, u.type, n, s, -1);
                } else {
                    int x = planets.empty() ? -1 : planets[0];
                    if (x < 0 && home >= 0)
                        for (int16_t y : G.systems[size_t(home)].planets)
                            if (G.planets[size_t(y)].owner == p) x = y;
                    if (x < 0) continue;
                    addUnits(p, u.type, n, G.planets[size_t(x)].system, x);
                }
                made.add(u.type, n);
            }
            removeExcessCapacity(p, s);
            dsHeroEvent(E, G, p, pn(p) + " calls the Biomass Event: " + unitWords(made) + " gather at " + sysName(s) + ".");
            return true;
        }
        case Ab::DsVenerable: {
            if (!heroUnlocked(G, p)) return false;
            purgeHero(p);
            std::vector<int> revealed;
            for (int i = 0; i < 3 && !G.relicDeck.empty(); ++i) {
                revealed.push_back(G.relicDeck.back());
                G.relicDeck.pop_back();
            }
            E.revealed();
            int inPlay = 0;
            for (const Player& q : G.players) inPlay += int(q.relics.size());
            std::string names;
            for (int r : revealed) names += (names.empty() ? "" : ", ") + std::string(kRelics[r].name);
            dsHeroEvent(E, G, p, pn(p) + " reveals " + names + " (Resonance Cascade).");
            if (inPlay > 0) gainTokens(p, std::min(inPlay, reinforcementTokens(p)), false, "Resonance Cascade: a command token per relic in play.");
            // swaps: another player's relic for one of the revealed ones
            for (int round = 0; round < 3 && !revealed.empty(); ++round) {
                std::vector<Option> opts;  // id = holder * 1000 + relic
                for (int q = 0; q < G.numPlayers; ++q)
                    if (q != p)
                        for (int16_t r : G.players[size_t(q)].relics) opts.push_back({q * 1000 + r, std::string(kRelics[r].name) + " (" + pn(q) + ")", ""});
                if (opts.empty()) break;
                opts.push_back({-1, "No more swaps", ""});
                const int c = fpick(p, Ab::DsVenerable, "Resonance Cascade: swap one of these relics for a revealed one?", opts,
                                    // 0.43.0 (D11): the swap explained
                                    "The other player gets one of the revealed relics; theirs goes back into the relic deck, which is shuffled.");
                if (c < 0) break;
                std::vector<Option> rv;
                for (int r : revealed) rv.push_back({r, kRelics[r].name, ""});
                const int nr = rv.size() == 1 ? rv[0].id : fpick(p, Ab::DsVenerable, "Give them which revealed relic?", rv);
                const int q = c / 1000, old = c % 1000;
                loseRelic(q, old, false);
                takeRelic(q, nr);
                revealed.erase(std::find(revealed.begin(), revealed.end(), nr));
                revealed.push_back(old);
                dsEvent(E, p, "Resonance Cascade", pn(q) + "'s " + kRelics[old].name + " becomes " + kRelics[nr].name + ".");
            }
            for (int r : revealed) G.relicDeck.push_back(int16_t(r));
            E.rng().shuffle(G.relicDeck);
            return true;
        }
        default: return ds2Action(p, ab);  // batch 2 (rules_ds2.cpp)
    }
}

// B-Unit 205643a (Roh'Dhna commander): Construction's secondary for a token can be its primary.
bool Rules::dsConstructionPrimary(int q) {
    if (!commanderUsable(G, q, "rohdhna") || !fac(q, "rohdhna") || freeSecondary_ ||
        !fyes(q, Ab::DsBUnit, "B-Unit 205643a: resolve Construction's primary ability instead?", strategyPrimary(3)))
        return false;
    say(q, pn(q) + " resolves Construction's primary ability (B-Unit 205643a).");
    return true;
}

}  // namespace ti
