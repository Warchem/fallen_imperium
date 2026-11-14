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
int homeOwner(const Game& g, int sys);           // -1 if not a home system
std::vector<int> controlledPlanets(const Game& g, int p);
std::vector<int> readyPlanets(const Game& g, int p);
int spendable(const Game& g, int p, bool influence, bool withTradeGoods = true);
int oceansReady(const Game& g, int p);  // Deepwrought ocean cards ready to spend (+ Kjalengard Bannerhalls)
int oceanCards(const Game& g, int p);
bool bannerhalls(const Game& g, int p);  // (0.44.9) the Kjalengard's Bannerhalls are in play for p   // ocean cards + Bannerhalls (Kjalengard breakthrough, 0.43.0: 1 per Glory token)
int triadValue(const Game& g, int p);  // The Triad (Thunder's Edge relic), ready: 3 + fragment kinds
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
int capturedCount(const Game& g, int p, UnitType t);  // units of this type the Vuil'raith player p holds captured
int capturedFrom(const Game& g, int owner, UnitType t);  // owner's non-fighter ships / mechs held captured by anyone
bool dimensionalTear(const Game& g, int sys);         // a Vuil'raith space dock makes the system a gravity rift
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
// Thunder's Edge breakthroughs (0.34.0): on with the module, off with Discordant Stars (owner)
bool breakthroughsOn(const Game& g);
bool hasBreakthrough(const Game& g, int p);
bool allSynergy(const Game& g, int p);  // The Quantumcore: synergy for every colour
bool breakthroughIs(const Game& g, int p, const char* factionKey);  // p has their breakthrough and is that faction
// Gravleash Maneuvers (0.44.4, fixes 8): while a move plan is checked, the units moving out of each system (nullptr:
// every ship there counts, as before)
extern const std::map<int, struct Force>* g_movingShips;

// p's breakthrough card as the rules see it (0.44.4: the Creuss's only from fixes 8 - its key didn't match before)
const struct BreakthroughDef* breakthroughFor(const Game& g, int p);
bool synergyOf(const Game& g, int p, TechColor& a, TechColor& b);
void techColourCounts(const Game& g, int p, int k, int out[4]);
int bestColourCount(const Game& g, int p);
// LRR 14: other players' ships and none of p's: p's units there can't produce ships.
bool blockaded(const Game& g, int p, int sys);
std::vector<int> playersWithShips(const Game& g, int sys);
int productionCapacity(const Game& g, int p, int sys);
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
bool hasScored(const Game& g, int p, int obj);
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

}  // namespace ti
