// Read-only questions about a game state, shared by the rules, the AI and the
// screens (e.g. "can this ship reach that system?", "what can I afford?").
#pragma once
#include <map>
#include <string>
#include <vector>

#include "engine/state.h"

namespace ti {

// ---- players ----------------------------------------------------------------
std::string playerName(const Game& g, int p);
std::vector<int> initiativeOrder(const Game& g);   // action phase order
int initiativeOf(const Game& g, int p);            // lowest strategy card number (1..8), 9 if none
std::vector<int> fromSpeaker(const Game& g);       // speaker, then clockwise
std::vector<int> clockwiseFrom(const Game& g, int first);
bool areNeighbours(const Game& g, int a, int b);
bool neighboursWithAll(const Game& g, int p);  // Xuange (Empyrean commander) unlock

// ---- planets ------------------------------------------------------------------
int planetResources(const Game& g, int planet);   // with attachments
int planetInfluence(const Game& g, int planet);
bool planetHasSkip(const Game& g, int planet);
int planetSkips(const Game& g, int planet);       // bit per TechColor (printed + research facilities)
bool hasAttachment(const Game& g, int planet, const char* key);
// No units may be committed, produced or placed: a Demilitarized Zone, or a space station (Thunder's Edge)
bool isDMZ(const Game& g, int planet);
// Thunder's Edge (0.34.0) space stations: controlled by having the only ships in their system; spent and
// readied like planets, but no ground forces or structures, no votes, not counted for objectives or a home
bool isStation(const Game& g, int planet);
bool planetless(const Game& g, int sys);  // (fixes 26) "does not contain any planets": a space station isn't a planet
int stationsOf(const Game& g, int p);
// Dhonraz Installations (Gledge breakthrough, 0.43.0): their planets with a Core token count as stations too (not for
// control; units still land there) - the commodity value, station exhausting and Space Cannon 5.
bool dhonrazStation(const Game& g, int p, int planet);
std::vector<int> dhonrazTargets(const Game& g, int p);  // (fixes 28) planets the Reference card's Dhonraz Installations can bombard
int dhonrazDice(const Game& g, int p);                  // its Y: the structures p controls
bool stationFor(const Game& g, int p, int planet);
int forgeworldsOf(const Game& g, int p, bool readyOnly);  // (0.44.9) Omni-Forgeworlds: war suns that are space stations too  // isStation, or a Dhonraz one of p's
int commodityValue(const Game& g, int p);  // the faction's commodities + 1 per space station
int commodityValue(const Game& g, const Player& pl);  // (pl: one of g.players)
// Entropic scar (Thunder's Edge): no unit abilities in it or against units in it
bool quietusAt(const Game& g, int p, int sys);   // the Crimson flagship: p's units in sys lose their unit abilities
int wormholeCount(const Game& g, int sys);  // (fixes 24) each wormhole a system contains (Particle Synthesis)
uint8_t qeWormholes(const Game& g, int sys);              // Quantum Entanglement: alpha / beta a system contains (fixes 24)
bool qeLinked(const Game& g, int p, int a, int b);        // p is the Creuss and a, b both have alpha / beta wormholes
int colishOrigin(const Game& g, int p);                    // Hil Colish (fixes 24): where p's delta-wormhole flagship is, -1
bool noAbilities(const Game& g, int p, int sys);
// Anomaly Stabilization (Nivyn breakthrough, 0.44.4 fixes 8): p's units ignore entropic scars wherever they fire
bool ignoresScars(const Game& g, int p);
// Absolute Synergy (Naaz-Rokha breakthrough, 0.44.4 fixes 8): Eidolon Maximum is a ship as well as a ground force -
// it moves on its own (Move 3), needs no capacity, counts toward the fleet pool and as a ship in space
bool eidolonShip(const Game& g, int p);
// A flagship's text ability for p: their own faction's, or (0.44.4 fixes 8) one copied by Valefar Assimilator Z
// (Nekro breakthrough) - the older checks that only looked at the faction
bool flagshipText(const Game& g, int p, const char* factionKey); // an entropic scar, or the Quietus
bool scarAt(const Game& g, int sys);
// A legendary planet; with Thunder's Edge Mecatol Rex is one too (its tile has the legendary icon)
bool isLegendaryPlanet(const Game& g, int planet);
// +1 move for one ship: Gravity Drive, and (Thunder's Edge) the Ionian Fuel Refinery for one more
bool moveBoost(const Game& g, int p);
bool lightningDrives(const Game& g, int p);  // Lightning Drives (Gledge, Discordant Stars)
int upgradeWaivers(const Game& g, int p, int tech);
int upgradeUnitType(int tech);  // the unit type a unit upgrade technology upgrades (-1 if none)                   // Sdallari Tvungovot (Kjalengard commander)
int upgradeWaiversApply(const Game& g, int p, int tech, int missing[4]);
int boostShips(const Game& g, int p);
// 0.44.9 (fixes 13, the owner's answer P6): the most +move one ship can get - boosts stack (Gravity Drive + Ionian Fuel
// Refinery + Lightning Drives ...); older games: 1 with any boost
int moveBonusMax(const Game& g, int p);
bool curatorHalf(const Game& g, int p);                 // (0.44.9) Eye of the Curator fighters: half a capacity slot each
int fighterSlots(const Game& g, int p, int fighters);   // the capacity those fighters take
bool hasTraitOf(const Game& g, int planet, Trait t);  // printed trait, or Terraform (all three)
// (owner 2026-11-13: the screens showed only the printed traits) the planet's traits as they are now - the printed
// ones first, then the ones it gained (Terraform, a minor faction's planet); for display
std::vector<Trait> traitsOf(const Game& g, int planet);
uint8_t anomaliesFor(const Game& g, int p, int s);  // anomalies as they affect p (Nav Suite, Voidborn, tears, Circlet)
uint8_t wormholesOf(const Game& g, int sys);      // printed + tokens (nexus: gamma only while inactive)
int attachedPlanets(const Game& g, int p);        // planets with attachments the player controls
int relicFragments(const Game& g, int p, int type);  // ExploreType or -1 = any; unknown fragments count for every type
// Uncharted Space (0.33.0): Player::fragments also holds Star Charts, which are not relic fragments
bool isRelicFragment(int card);
int fragmentCount(const Game& g, int p);   // relic fragments only
int starChartCount(const Game& g, int p);
bool relicHeld(const Game& g, int p, const char* key);
bool isHomePlanet(const Game& g, int planet);   // in any home system
// 0.82.0 Monuments+: the player's monument (kMonuments index, -1 none - the module is off or their faction has none),
// whether its card lets it stand on a planet / in a system's space area (placement only - control is the caller's), where it is
int playerMonument(const Game& g, int p);
bool isMonumentOption(int id);  // a monument placement option id (rules_monuments.cpp)
int monumentOptionId(int planet, int sys);  // ... for that planet (or, planet < 0, that system's space area)
bool monumentFitsPlanet(const Game& g, int p, int planet);
bool monumentFitsSpace(const Game& g, int p, int sys);
bool monumentInSpace(const Game& g, int p);                  // The Panopticon: placed in a space area instead of on a planet
int monumentSystem(const Game& g, int p, int* planet = nullptr);
int epiphanyTokens(const Game& g, int p);
// (0.82.2) Discordant Stars' monuments
bool sleepingWorldOn(const Game& g, int p, int planet);  // Sleeping World (Khrask): their monument on this planet fights as a ground force
bool freeholdAt(const Game& g, int p, int sys);          // Freehold Starport (Nokar): their units may leave sys despite their token
// (0.82.3) Discordant Stars' monuments, batch 2 (rules_monuments6.cpp)
int keepFighterSlots(const Game& g, int p, int sys);   // Gravelord's Keep (Myko-Mentori): 3 fighters there / next door need no capacity
bool mawBlocks(const Game& g, int p, int sys);         // The Maw (Nivyn): another player's Maw there - p's ships can't leave / pass
void mawLinks(const Game& g, int p, int u, std::vector<int>& out);  // ... the Nivyn's ships: the Maw next to the Wound
bool debrisField(const Game& g, int s);                // Debris Field (Zelian): an asteroid field ships move into freely
uint8_t tileAnomaliesMove(const Game& g, int s);       // tileAnomalies without the Debris Field's asteroid field (movement)
int hoardCommodity(const Game& g, int p);              // Aylor Raider Hoard (Vaylerian): +1 commodity value
bool coveHolder(const Game& g, int p);                 // Corsairs' Cove (Florzen): produced fighters may go onto the card
// (0.82.4) Discordant Stars' monuments, batch 3 (rules_monuments7.cpp)
int libraryColours(const Game& g, int p);              // Forbidden Library (Lanefir): specialty colours near it, unused this action
int hiveProduction(const Game& g, int p, int sys);     // Polemarch's Hive (Cheiran): PRODUCTION X in its system
bool reliquaryOn(const Game& g, int planet);           // Kyro Reliquary: legendary, an attachment, a specialty of no type
bool anchorpointBare(const Game& g, int p, int sys);   // Ghoti Anchorpoint: no planets during their movement
bool anchorpointFrontier(const Game& g, int p, int sys);  // ... a frontier token there for them
bool reliquatBlocks(const Game& g, int p, int sys);    // Reliquat Unleashed: p (units in the Fracture) can't activate it
int rhodunBonus(const Game& g, int p, int opp, int sys);  // Vault of New Phrad +1 / Reliquat Unleashed +2 to p's combat rolls
void hallsOfValorLinks(const Game& g, int p, int u, std::vector<int>& out);  // Halls of Valor (Ghemina): movement links  // (0.82.1) control tokens on p's Epiphany card (0: none, or not the Firmament's card)
bool isMonumentStation(const Game& g, int planet, int* owner = nullptr);  // (0.82.1) the Seraph Data Center's station card  // where it stands (-1 = not on the board; *planet -1 = space)
int monumentGuards(const Game& g, int p);  // (0.82.0) p's ground forces on its planet + in its space area (Defend a Great Work)
int monumentIndex(const char* key);                          // kMonuments index of a card by its key, -1 none
// The monument cards whose text p's monument card has: its own and (the Nekro's Valefar Assimilator "M") the cards with their
// "M" / "Z" assimilator tokens. Returns how many (out[0] = their own).
int monumentCards(const Game& g, int p, int out[3]);
bool monCardHas(const Game& g, int p, const char* key);     // p's monument card has that card's ability (on the board or not)
// ... and p's monument is on the board: where (planet -1 = in a space area)
bool monAbilityAt(const Game& g, int p, const char* key, int* sys, int* planet = nullptr);
// (0.82.0, owner M7) Nefishh Projector (Naalu): p's infantry committed to this planet ALONE (no mech) may coexist with the other
// ground forces there (another player's or neutral) instead of fighting - for the landing screen's warning
bool nefishhCovers(const Game& g, int p, int planet);
bool monCardReady(const Game& g, int p);                     // the monument card isn't exhausted
bool monumentFitsPlanetCard(const Game& g, int card, int planet);  // that card's placement icons allow the planet
int homeOwner(const Game& g, int sys);           // -1 if not a home system
std::vector<int> controlledPlanets(const Game& g, int p);
std::vector<int> readyPlanets(const Game& g, int p);
int spendable(const Game& g, int p, bool influence, bool withTradeGoods = true);
int oceansReady(const Game& g, int p);  // Deepwrought ocean cards ready to spend (+ Kjalengard Bannerhalls)
int oceanCards(const Game& g, int p);
bool bannerhalls(const Game& g, int p);  // (0.44.9) the Kjalengard's Bannerhalls are in play for p   // ocean cards + Bannerhalls (Kjalengard breakthrough, 0.43.0: 1 per Glory token)
int triadValue(const Game& g, int p);  // The Triad (Thunder's Edge relic), ready: 3 + fragment kinds
int groveInfluence(const Game& g, int p);  // the Grove's influence (Khrask, fixes 28): planets with p's coexisting units
int spendableMixed(const Game& g, int p);  // each ready planet's higher value + trade goods
int xanderCommodities(const Game& g, int p);  // commodities p may spend as trade goods now (Keleres agent)
// What exhausting a planet is worth for a payment (mixed: the higher of its two values).
int planetPayValue(const Game& g, int planet, bool influence, bool mixed);

// ---- units --------------------------------------------------------------------
const UnitDef& unitDef(const Game& g, int p, UnitType t);
// (0.52.0) A unit type as named for player p: a player with two flagship types (the Ghemina: The Lord, The Lady) sees
// their names, everything else is unitTypeName.
std::string unitLabel(const Game& g, int p, UnitType t, bool plural = false);  // (Articles of War: mechs without their printed abilities)
bool mechAbilities(const Game& g);  // false while Articles of War (For) is a law: mechs keep only Sustain Damage
int unitLimit(UnitType t);                       // physical pieces (fighters/infantry unlimited)
int unitLimitOf(const Game& g, int p, UnitType t);  // with faction extras (Discordant Stars: Teeming, Industrialists)
int unitsOnBoard(const Game& g, int p, UnitType t);
int unitsAvailable(const Game& g, int p, UnitType t);  // in reinforcements (captured units excluded, LRR 17.5)
// 0.61.1 (fixes 32, owner 2026-11-18 W19) LRR 23.4a: with none of a type left in the reinforcements, a player may take one of
// theirs back from any system without their command token and place it at once (not fighters / infantry: unlimited)
bool reclaimAllowed(const Game& g, int p, UnitType t);
int reclaimable(const Game& g, int p, UnitType t);     // their units of that type in systems without their command token
int unitsPlaceable(const Game& g, int p, UnitType t);  // unitsAvailable + reclaimable (when allowed)
// 0.61.2 (fixes 33, owner 2026-11-18 X1 "it still covers it as it beholden to the living rules on unit placement"): what an ABILITY
// may place - unitsPlaceable from rules 33 (the player is asked, and may decline, in Rules::addUnits), else unitsAvailable
int unitsForPlacement(const Game& g, int p, UnitType t);
int capturedCount(const Game& g, int p, UnitType t);  // units of this type the Vuil'raith player p holds captured
int capturedFrom(const Game& g, int owner, UnitType t);  // owner's non-fighter ships / mechs held captured by anyone
// (fixes 35) the Vuil'raith's own units of that type on their own faction sheet: Amalgamation gives one back for each unit of the type
// they produce, so these count for their production (0 for everyone else, and in older games)
int amalgamationOwn(const Game& g, int p, UnitType t);
bool dimensionalTear(const Game& g, int sys);         // a Vuil'raith space dock makes the system a gravity rift
bool mortheusNebula(const Game& g, int s);  // (fixes 28) the Mortheus home system is a nebula
bool nivynRift(const Game& g, int s);       // (fixes 29) the Nivyn home system is a gravity rift
bool zelianAsteroids(const Game& g, int s);  // (fixes 29) the Zelian home system is an asteroid field
uint8_t tileAnomalies(const Game& g, int s);  // the tile's printed anomalies + the Zelian home's asteroid field (fixes 29)
bool isAnomaly(const Game& g, int s);                // an anomaly for objectives (incl. Dimensional Tears, the Wound)
int groundForcesOn(const Game& g, int p, int planet); // ground forces incl. Hel-Titans (Titans PDS)
int amalgamationSavings(const Game& g, int p, const int counts[kUnitTypes]);  // Vuil'raith: resources captured units pay
int edictTokensOf(const Game& g, int q);              // q's command tokens in Mahact fleet pools (Edict)
bool argentMechFree(const Game& g, int p, const Force& f);  // Aerie Sentinel: mechs need no capacity here
int forceCapacity(const Game& g, int p, const Force& f);   // capacity of ships (and floating factories)
// Ships, and Saar floating factories, which move and carry "as if they were ships".
bool movesAsShip(const Game& g, int p, UnitType t);
int shipCapacity(const Game& g, int p, int sys);           // capacity of ships in the space area
int dockFighterSlots(const Game& g, int p, int sys);       // fighters space docks can hold
int transportedInSpace(const Game& g, int p, int sys);     // fighters + ground forces in space
int capacityOverflow(const Game& g, int p, int sys);       // >0 = too many fighters/ground forces
// Fighter II: fighters that move on their own need no capacity; the excess counts against the fleet pool.
bool fightersFree(const Game& g, int p);
int overflowOf(const Game& g, int p, int sys, const Force& f);   // units of f that don't fit (must be removed)
int excessFighters(const Game& g, int p, int sys, const Force& f);  // fighters of f beyond capacity (Fighter II)
int fleetUse(const Game& g, int p, int sys, const Force& f);     // fleet pool use: non-fighter ships + excess fighters
int fleetUseIn(const Game& g, int p, int sys);
const char* trapName(int k);  // the Li-Zho Traps (Discordant Stars, rules_ds6.cpp): name and text of trap 0-4
const char* trapText(int k);
int dsBombardDiceOf(const Game& g, int p, int sys, UnitType t);  // the Lady, Raid Leaders (Discordant Stars): Bombardment 5 dice
int bombardAvailable(const Game& g, int p, int sys, UnitType t);  // units that can bombard (Annihilator: mechs on planets)
bool hasShips(const Game& g, int p, int sys);
bool hasUnits(const Game& g, int p, int sys);              // ships, or units on planets
bool otherShipsIn(const Game& g, int p, int sys);           // another player's ships present (neutral ones too)
// Thunder's Edge (0.34.0): neutral units (kNeutral slot)
const UnitDef& neutralUnitDef(UnitType t);
bool neutralShipsIn(const Game& g, int sys);
bool neutralUnitsIn(const Game& g, int sys);
bool neutralOn(const Game& g, int planet);      // any neutral unit on the planet
int neutralGroundOn(const Game& g, int planet);
bool anyNeutralUnits(const Game& g);
bool metaliSustain(const Game& g, int p, UnitType t, int sys, int planet);  // Metali Void Shielding (one ship per set of hits)
int extraSustains(const Game& g, int p, int sys, int planet);  // how many ships without Sustain Damage may use it (Metali, Splintering Gale)
int extraSustainsPooled(const Game& g, int p, int sys, int planet);  // ... of those, the ones any such ship may use (not per type)
int galeLeft(const Game& g, int p, int sys, UnitType t);  // (rules 28) the Splintering Gale's chosen ships of type t that can sustain
// Thunder's Edge breakthroughs (0.34.0): on with the module, off with Discordant Stars (owner)
bool breakthroughsOn(const Game& g);
bool hasBreakthrough(const Game& g, int p);
bool allSynergy(const Game& g, int p);  // The Quantumcore: synergy for every colour
bool breakthroughIs(const Game& g, int p, const char* factionKey);  // p has their breakthrough and is that faction
// Gravleash Maneuvers (0.44.4, fixes 8): while a move plan is checked, the units moving out of each system (nullptr:
// every ship there counts, as before)
extern const std::map<int, struct Force>* g_movingShips;
extern const std::map<int, int>* g_movingFromPlanets;  // (0.85.0 Sedtra) ... and the ground forces picked up from planets in each

// p's breakthrough card as the rules see it (0.44.4: the Creuss's only from fixes 8 - its key didn't match before)
const struct BreakthroughDef* breakthroughFor(const Game& g, int p);
bool synergyOf(const Game& g, int p, TechColor& a, TechColor& b);
void techColourCounts(const Game& g, int p, int k, int out[4]);
int bestColourCount(const Game& g, int p);
// LRR 14: other players' ships and none of p's: p's units there can't produce ships.
bool blockaded(const Game& g, int p, int sys);
std::vector<int> playersWithShips(const Game& g, int sys);
int productionCapacity(const Game& g, int p, int sys);
int supremacyCapacity(const Game& g, int p, int sys);  // Supremacy (Celdauri flagship): other players' docks here, for ships
int singleUnitProduction(const Game& g, int p, int sys);  // (0.44.5) the best PRODUCTION value of one unit there
bool hasProductionIn(const Game& g, int p, int sys);
bool custodiaMecatol(const Game& g, int p);  // Keleres breakthrough: they control Mecatol Rex with Custodia Vigilia
int custodiaValue(const Game& g, int p, bool influence);
bool arcaneShield(const Game& g, int planet);  // 0.43.0: Arcane Shield Emitter attached (no infantry, no bombardment)  // the Custodia Vigilia card, ready (2 resources / 3 influence)
bool flagshipAbility(const Game& g, int p, const char* factionKey);  // own faction or a Valefar Z token (Nekro)
bool tethered(const Game& g, int p, int a, int b);  // Void Tether: p doesn't treat a and b as adjacent
int particleSynthesis(const Game& g, int p, int sys);  // Creuss breakthrough: wormholes there (Production 1 / 1 cheaper each)
bool eventOn(const Game& g, int ev);  // a galactic event is in play (Thunder's Edge, 0.36.0)
int commodityCap(const Game& g, const Player& pl);  // most commodities it may hold (Age of Commerce: no limit)
int vadenDebtCap(const Game& g);  // most control tokens of one player on the Vaden's sheet (fixes 29: not limited, LRR 23.2)
int refreshTo(const Game& g, const Player& pl);     // commodities after a refill (Age of Commerce: + the value)
// The Firmament / the Obsidian (0.35.0)
const char* plotName(int plot);
const char* plotText(int plot);
int plotCount(const Player& pl);                       // plot cards in the play area
bool plotPuppet(const Game& g, int obs, int plot, int q);  // q is the puppet of the Obsidian's (revealed) plot
bool anyPuppet(const Game& g, int obs, int q);         // q's control token is on one of obs's plots (either side)
int obsidianOf(const Game& g);                         // the Obsidian player, -1 if none
int galvanizedOnBoard(const Game& g, int p);           // Last Bastion (rules_factions6.cpp)
// Where newly produced ground forces may go: planets with a producing unit
// (any controlled planet if the producer is in space), -1 = the space area.
std::vector<int> groundForceDestinations(const Game& g, int p, int sys);
bool shouldBeEliminated(const Game& g, int p);
bool controlsHomePlanets(const Game& g, int p);  // needed to score public objectives (LRR 61.16)
int fleetLimit(const Game& g, int p);
int unitCost2(const Game& g, int p, UnitType t);          // cost x2
int productionCost(const Game& g, int p, const int counts[kUnitTypes]);  // after Sarween Tools
int unitsCostValue(const Game& g, int p, const int counts[kUnitTypes]);  // printed costs only (combined-cost limits)
int terrafactoryExtra(const Game& g, int p, int sys, const int counts[kUnitTypes]);  // Roh'Dhna war suns: 1 per cost
int structuresOf(const Game& g, int p);
int combatStrength(const Game& g, int p, int sys, bool space);  // rough fighting value

// ---- movement -----------------------------------------------------------------
struct Reach {
    bool ok = false;
    bool viaRift = false;   // path leaves or crosses a gravity rift: the ship must roll
    bool needsBonus = false;  // only reachable with +1 move (Gravity Drive)
    int8_t bonus = 0;         // (0.44.9) how much +move it needs
    int riftRolls = 0;        // dice the ship must roll for gravity rifts
    bool viaStorm = false;    // the route uses the ion storm's wormhole (the token flips)
    bool viaWormhole = false; // the route goes through a wormhole (Sai Seravus)
    bool vogul = false;       // only through the Eye of Vogul (Uncharted Space relic): one ship per pick
    bool colish = false;      // (fixes 24) only through Hil Colish's delta wormhole in the active system: it moves first
};
// Eye of Vogul: how many ships of this type in 'from' are treated as adjacent to the active system
int vogulShips(const Game& g, int p, int from, UnitType t);
bool shipsMayEnter(const Game& g, int p, int s);  // supernovas / asteroid fields only with the technology
// Systems a ship of player p can step to from u: adjacency plus Quantum Entanglement,
// Emissary Taivra and Lazax Gate Folding.
std::vector<int> routeNeighbors(const Game& g, int p, int u);
int maxShipMove(const Game& g);                      // highest move value of any ship on the board (Captain Mendosa)
bool magmusProduction(const Game& g, int p, int sys);  // Magmus Reactor: Production 5 in a supernova
// (fixes 26, the Muaat review - owner 2026-11-12) games without the Codex and Thunder's Edge play the original Magmus
// Reactor (a trade good after PRODUCTION by a war sun / next to a supernova); with either of them the Omega card
bool magmusOriginal(const Game& g);
bool anyPlanetGFFor(const Game& g, int p);  // fx.anyPlanetGF applies to p (the active player, or Umbat's chosen one)
int warSunUpgradeFor(const Game& g, int p);  // p's war sun unit upgrade: their faction's own (Roh'Dhna) or War Sun
bool ownsTechKey(const Game& g, int p, const char* key);  // incl. Valefar Assimilator copies
// X-89 Bacterial Weapon as Thunder's Edge's Codex prints it (0.43.3, Codex games started with fixes >= 4): the owner's
// bombardment and ground combat rolls make twice the hits; each planet it bombards is exhausted
bool x89Omega(const Game& g, int p);
// 0.43.4 (owner, RULES-CHECK X1): the Codex's Omega cards are in play with the Codex ticked - or with Thunder's Edge,
// which comes with the updated Codex (games started with fixes >= 5)
bool codexOn(const Game& g);
bool codex3Explore(int card);
// (0.61.0, fixes 31) Demilitarized Zone: "During the agenda phase, this planet's planet card can be traded as part of a transaction"
bool dmzTradeable(const Game& g, int who, int planet);  // (0.61.0) Codex III's frontier cards: Entropic Fields, Keleres Ship, Dead World
bool codex2Relic(int r);  // (0.60.0) Dynamis Core, JR-XS455-O, Nano-Forge - in games with the Codex or Thunder's Edge
// (0.57.1, fixes 20 - tester) Thunder's Edge replaces the Xxcha hero: Xxekir Grom places PDS / mechs and readies planets
// (the Codex's resources-and-influence passive is off then)
bool teXxchaHero(const Game& g);
bool teNaalu(const Game& g);  // (fixes 27) Thunder's Edge's Z'eu and Iconoclast
// Does a step between these two systems go through the ion storm's wormhole?
bool ionStormStep(const Game& g, int a, int b);
int moveValue(const Game& g, int p, int sys, UnitType t);
// Can a ship of this type move from 'from' into 'to' (the active system)?
Reach shipReach(const Game& g, int p, int from, int to, UnitType t, int bonus = 0);
Reach shipReachNoColish(const Game& g, int p, int from, int to, UnitType t, int bonus);  // without Hil Colish moving first
Reach shipReachViaNoColish(const Game& g, int p, int from, const std::vector<int>& via, int to, UnitType t, int bonus);
Reach shipReachNoRift(const Game& g, int p, int from, int to, UnitType t, int bonus = 0);  // without Rift Engines' +2
// Waypoints (0.56.0): the same along a route the player chose (from -> via... -> to); *why: what is wrong with it
Reach shipReachVia(const Game& g, int p, int from, const std::vector<int>& via, int to, UnitType t, int bonus = 0,
                   std::string* why = nullptr);
bool riftRollsLeaving(const Game& g, int p, int s);  // leaving s is leaving a gravity rift p's ships roll for
// the systems a route may step to next (Waypoint Mode): next to its last stop - or 'to' itself
std::vector<int> routeNextSteps(const Game& g, int p, int from, const std::vector<int>& via, int to);
Reach shipReachNoMyru(const Game& g, int p, int from, int to, UnitType t, int bonus = 0);  // without Myru Vos's way through
std::vector<int> systemsWithin(const Game& g, int from, int steps);  // plain adjacency distance
bool passableFor(const Game& g, int p, int sys);
bool canEndMoveIn(const Game& g, int p, int s);  // (0.44.7) no supernova / asteroid field it can't enter   // can p's ships move through this system?
int travelCost(const Game& g, int p, int from, int to);  // move points on a rift-free route (huge if none)
int travelCostNoRoll(const Game& g, int p, int from, int to);  // (0.54.0) also through rifts the ship doesn't roll for
bool canActivate(const Game& g, int p, int sys);
bool adjacentSystems(const Game& g, int a, int b);

// ---- laws and cards -------------------------------------------------------------
bool lawInPlay(const Game& g, const char* key);
// (0.81.0) Absol's Agendas: one of the module's laws in play (nullptr in other games / when the laws are blank)
const Law* absolLaw(const Game& g, const char* key);
int fleetPoolCap(const Game& g, int p);     // most command tokens p's fleet pool may hold (Fleet Regulations: 4; Absol's: its token 5)
bool conventionsOfWar(const Game& g);       // no bombardment of cultural planets (Conventions of War, For - PoK or Absol)
int lawOwner(const Game& g, const char* key);       // -1 if not in play or in the common area
int actionCardLimit(const Game& g, int p);           // 7, or 3 with Executive Sanctions
bool holdsActionCard(const Game& g, int p, const char* key);
int voteValue(const Game& g, int p, int planet);     // votes a planet gives (its influence)
int availableVotes(const Game& g, int p);            // ready planets' influence
bool canPlayActionCards(const Game& g, int p);       // Political Censure stops it
bool ownsWarSunTech(const Game& g, int p);
bool unitSustains(const Game& g, int p, UnitType t); // Sustain Damage (Publicize Weapon Schematics)
bool unitSustainsAt(const Game& g, int p, UnitType t, int sys, int planet);  // + Fourth Moon / Moll Terminus
bool sustainBlockedAt(const Game& g, int p, UnitType t, int sys, int planet);  // no Sustain Damage there (scar, Fourth Moon, Moll)
int tgWorth(const Game& g, int p);                   // what one trade good pays (Mirror Computing: 2)

// ---- factions, leaders, notes (Stage 5) -----------------------------------------
bool isFaction(const Game& g, int p, const char* key);        // p plays this faction (and it is implemented)
int leaderIndex(const Game& g, int p, int type, int nth = 0);  // index into Player::leaders, -1 if none
int leaderStatus(const Game& g, int p, int type, int nth = 0); // LeaderStatus, kLocked if none
bool agentReady(const Game& g, int p, int nth = 0);
bool heroUnlocked(const Game& g, int p);                       // unlocked and not purged
// Can p use the commander ability of 'factionKey'? (their own unlocked commander,
// or an Alliance note from that faction's player whose commander is unlocked)
bool commanderUsable(const Game& g, int p, const char* factionKey);
int playerOfFaction(const Game& g, const char* key);           // -1 if nobody plays it
bool heroActive(const Game& g, int p, const char* factionKey);  // a hero "placed near the board" this round
int sustainWorth(const Game& g, int p);                         // hits one Sustain Damage cancels (Non-Euclidean Shielding: 2)
bool shieldsDown(const Game& g, int owner, int sys);           // owner's units here lose Planetary Shield (Arc Secundus)
bool immuneToDirectHit(const Game& g, int p, UnitType t);
bool flagshipIn(const Game& g, int p, int sys);
bool holdsNote(const Game& g, int p, int owner, int type);     // in hand
std::string noteNameOf(const Game& g, int note);
// 0.43.0 (D119): a Veldyr Branch Office the receiver has no planet for (non-home, not Mecatol Rex, none there yet)
bool branchNoSpot(const Game& g, int note, int receiver);
std::string noteTextOf(const Game& g, int note);
const char* olradinPolicyText(int k, bool minus);  // policy card k (People, Environment, Economy), + or - side (rules_ds4.cpp)
int favorColour(const Game& g, int p, int tech);  // (fixes 29) the colour Favor of Rhodun adds to, -1 none
int analyticalWaiver(const Game& g, int p, int tech);           // prerequisites Jol-Nar may ignore (0/1)

// ---- technology -----------------------------------------------------------------
int techCount(const Game& g, int p, TechColor c);
// Missing prerequisites per colour (blue, green, red, yellow) for researching tech t.
void missingPrereqs(const Game& g, int p, int t, int missing[4]);
bool techAvailableToResearch(const Game& g, int p, int t);  // not owned, own faction or generic, base/PoK
// Ready planets that can skip each colour.
std::vector<int> skipPlanets(const Game& g, int p, TechColor c);
bool canResearchNow(const Game& g, int p, int t);  // prerequisites met counting usable skips

// ---- objectives -----------------------------------------------------------------
struct SpendNeed {
    int resources = 0, influence = 0, tradeGoods = 0, tokens = 0;
    bool any() const { return resources || influence || tradeGoods || tokens; }
};
SpendNeed objectiveSpend(int obj);
// True if p currently meets the objective (for spend objectives: can afford it).
bool meetsObjective(const Game& g, int p, int obj);
int mirvedaPdsCap(const Game& g, int p);  // (fixes 28) Privileged Citizenry: the PDS a system can hold (-1: no limit)
bool hasScored(const Game& g, int p, int obj);
// (fixes 28, the tester's report) a scored secret objective that is one of the player's secret objectives - not a leaked one
// (Classified Document Leaks) they scored as a public objective
bool scoredAsSecret(const Game& g, int p, int obj);
bool leakedSecret(const Game& g, int obj);  // Classified Document Leaks made it a public objective
bool canPayBoth(const Game& g, int p, int resources, int influence);  // from ready planets + trade goods
// How far p is with a counting objective: 'have' of 'need' (planets, ships,
// technologies, systems...). need == 0 for objectives that aren't counted
// (spend objectives, action-phase secrets). Used by the AI to plan.
struct ObjProgress {
    int have = 0, need = 0;
    int missing() const { return need > have ? need - have : 0; }
};
ObjProgress objectiveProgress(const Game& g, int p, int obj);
void setProgressAroz(bool on);  // objectiveProgress sees Captain Aroz for secrets (the bots' planning only)

// ---- Blue Reverie (0.83.0, query_br.cpp) -----------------------------------------------
bool brAtokera(const Game& g, int p);                       // p plays the Atokera Legacy (with the module)
bool synthesisPlanet(const Game& g, int p, int planet);     // Synthesis: p's readied planet (Planetary Shield, Production 0)
bool kohiIn(const Game& g, int p, int sys);                 // Kohi (Atokera flagship, or a Valefar Z copy) is in sys
UnitDef kohiDef(const Game& g, int p, UnitType t, int sys); // p's unit of type t in sys with the abilities Kohi copies
bool modularAt(const Game& g, int p, UnitType t, int sys, int planet, int ability);  // Modular Designs' chosen unit has it
bool brSustains(const Game& g, int p, UnitType t, int sys); // Sustain Damage copied by Kohi
int modularSustainLeft(const Game& g, int p, UnitType t, int sys, int planet);  // Modular Designs' Sustain Damage (0/1)
bool brPlanetShield(const Game& g, int q, int planet);      // Synthesis / Kohi Planetary Shield for q's units on the planet
bool obeliskGuards(const Game& g, int planet, int by);      // Obelisk: `by` can't exhaust this planet
int brUnitProduction(const Game& g, int p, UnitType t, int sys, const UnitDef& d);  // Synthesis +1, Mirrorshard I, Kohi
// ---- the Belkosea Allied States (0.84.0, query_br.cpp): superweapons (PlanetState::superweapon, Player::swCards)
constexpr int kSuperweaponCount = 5;
enum : int { kSwAvailyn = 0, kSwCaled = 1, kSwGlatison = 2, kSwGrom = 3, kSwMors = 4 };
struct SuperweaponDef {
    const char* key;
    const char* name;
    const char* text;  // the card's wording
};
extern const SuperweaponDef kSuperweapons[kSuperweaponCount];
bool brBelkosea(const Game& g, int p);                      // p plays the Belkosea Allied States (with the module)
int superweaponPlanet(const Game& g, int k);                // the planet with superweapon k (-1: not on the board)
int superweaponSystem(const Game& g, int k);                // ... its system
int superweaponOwner(const Game& g, int k);                 // ... its controller (-1)
int superweaponsOf(const Game& g, int p);                   // Arrogance: superweapons p controls (structures)
bool availynNebula(const Game& g, int sys);                 // Availyn: "This system is a nebula."
int availynProduction(const Game& g, int p, int planet);    // Availyn: PRODUCTION 3 on its planet for its controller (0 none)
bool glatisonDenies(const Game& g, int p, int sys);         // Glatison: "Other players' units in this system lose SUSTAIN DAMAGE"
bool morsShield(const Game& g, int q, int planet);          // Mors: PLANETARY SHIELD on its planet for its controller
bool caledLine(const Game& g, int from, int to);            // Caled: a straight line crossing only parallel non-anomaly borders
bool swCardReady(const Game& g, int p, int k);              // p's superweapon card k is in their play area, readied
bool armageddonAt(const Game& g, int k, int sys, int* holder, int* planet);  // Armageddon Project: card k's abilities at its monument in sys
// ---- the Pharad'n Order (0.85.0, query_br.cpp): captured infantry pay for infantry (Mark of Pharad'n)
bool brPharadn(const Game& g, int p);                       // p plays the Pharad'n Order (with the module)
bool markOfPharadn(const Game& g, int p);                   // ... and the game has begun (setup places their starting infantry as usual)
int pharCaptured(const Game& g, int p);                     // their captured infantry (tokens on their faction sheet)
bool pyramidOn(const Game& g, int p, int planet);           // Black Pyramid (their monument): on this planet, a structure and a ground force

}  // namespace ti
