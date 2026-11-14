// The rules of the game, written as plain procedures that run inside the
// engine's coroutine. Whenever a player has to choose, the code calls one of
// the ask helpers, which pauses the game until an answer arrives.
//
// Plan encodings used in Answer::data:
//   Move:    groups of 4 ints  [fromSystem, location(-1 space / planet), unitType, count]
//            (fighters / ground forces may come from any system a moving ship
//             with capacity starts in or passes through)
//   Produce: 11 counts (one per UnitType), then the planet for ground forces (-1 = space)
//   Hits:    pairs [unitType, mode]   mode 0 = destroy, 1 = sustain damage
//   Land:    groups of 3 ints  [planet, unitType, count]   (landing, or bombardment targets)
//   Tokens:  [tactic, fleet, strategy] totals after the change
//   Pay:     picks = planets to exhaust, number = trade goods
#pragma once
#include <map>
#include <string>
#include <vector>

#include "engine/deal.h"
#include "engine/engine.h"
#include "engine/query.h"

namespace ti {

struct GameOverSignal {};

// Timing windows in which action cards (and similar abilities) can be played.
enum class Win : uint8_t {
    None,
    StrategyStart,        // Summit, Manipulate Investments
    PickStrategy,         // Public Disgrace (a = picker, value = card)
    AfterActivate,        // Flank Speed ... (active) / Counterstroke, Decoy, Forward Supply Base (others)
    StrategicAction,      // Coup d'Etat (a = active player, value = card)
    AfterAction,          // Master Plan
    AfterMove,            // Experimental Battlestation
    BeforeCannonHits,     // Maneuvering Jets (a = victim, b = shooter, value = hits)
    AfterUnitRoll,        // Scramble Frequency (a = roller, value = roll kind)
    CombatStart,          // Reveal Prototype (a = attacker, b = defender)
    CombatRoundStart,     // Morale Boost, Skilled Retreat, Emergency Repairs, Fighter Prototype
    CombatRoundEnd,       // Emergency Repairs
    BeforeAFB,            // Waylay (a = roller)
    AnnounceRetreats,     // Rout (b = defender)
    AfterRetreat,         // Intercept (a = retreating player)
    BeforeSpaceHits,      // Shields Holding (a = victim, value = hits)
    AfterSustain,         // Direct Hit (b = shooter), Reflective Shielding (a = owner)
    ShipDestroyed,        // Courageous to the End (a = owner, b = opponent, value = unit type)
    WonSpaceCombat,       // Salvage (a = winner, b = loser)
    InvasionStart,        // Blitz, Disable (active) / Bunker (defenders)
    AfterLanding,         // Parley, Ghost Squad (a = lander, b = planet owner)
    AfterGroundRoll,      // Fire Team (a = roller)
    GainControl,          // Infiltrate (a = new owner, b = old owner)
    LoseControl,          // Reparations (a = new owner, b = old owner)
    Production,           // War Machine (a = producer)
    ReturnStrategyCards,  // Political Stability
    AgendaStart,          // Ancient Burial Sites
    AgendaRevealed,       // Veto, Diplomatic Pressure ("when revealed")
    AfterAgendaRevealed,  // riders, Assassinate Representative, Hack Election, Insider Information
    AfterSpeakerVotes,    // Bribery
    AfterCastVotes,       // Distinguished Councilor (a = voter)
    Elected,              // Confusing / Confounding Legal Text (a = elected player)
    OutcomeResolve,       // Deadly Plot
    Pass,                 // (0.33.0, Uncharted Space) AI Augury, Arbitrage (a = the player passing)
    TradeAgreement,       // Escape Clause (a = the note's owner, b = its holder)
    Research,             // Forbidden Knowledge (a = researcher)
    StatusStart,          // Bounty Contracts
    GroundHits,           // Carapace Plating (a = the side being hit, b = the roller, value = hits)
    InfantryKilled,       // Contagion (a = the killer, b = the loser, planet)
    MechKilled,           // Safety Overrides (a = the mech's owner, b = the opponent, planet)
    BeforeAbilityRoll,    // Classified Weapons (a = roller, value = dice)
    AfterDieRolled,       // Confused Sage (a = roller, b = their target; Game::sage* hold the dice)
    Purged,               // Aggressive Broker (a = the player who purged something)
    NoteResolved,         // Renegotiation (a = the note's owner, b = the player who used it)
    // Thunder's Edge (0.34.0)
    AfterTransaction,     // Lie in Wait (a, b = the two players)
    TurnStart,            // Extreme Duress (a = the player whose turn starts)
    TurnEnd,              // Puppets on a String, Crisis (a = the player whose turn ended)
    LastShipLost,         // Crash Landing (a = the player who lost their last ship in the active system)
    DealNegotiation,      // Black Market Dealings (a = the player making an offer, b = the other player)
    Count
};

struct WinCtx {
    Win w = Win::None;
    int active = -1;          // active player (or the player whose event it is)
    int sys = -1, planet = -1;
    int a = -1, b = -1;       // players involved (see Win)
    int value = 0;
    bool space = true;        // space or ground combat
    bool first = false;       // first combat round
};

struct Played {
    int player;
    int card;
};
bool usCard(int card);  // an Uncharted Space action card (0.33.0)
bool teCard(int card);  // a Thunder's Edge action card (0.34.0)
bool teRelicReady(int r);  // a relic that goes into the deck (Thunder's Edge ones wait for their parts)

// Combat roll adjustments from faction abilities, leaders and notes.
struct RollMods {
    int perType[kUnitTypes] = {};    // added to the result of each die of that unit type
    int extraDice[kUnitTypes] = {};  // extra dice for one unit of that type (agents)
    int oneType = -1, oneBonus = 0;  // Gravleash Maneuvers (Letnev breakthrough): +X to one ship's dice
    bool flagshipBonus = false;      // J.N.S. Hylarim: its natural 9s and 10s make 2 extra hits
    bool valor = false;              // Valor (Kjalengard, Discordant Stars): each natural 10 makes 1 extra hit
    void all(int v) {
        for (int& x : perType) x += v;
    }
};

// One player's space cannon against ships in a system during the space cannon offense step (Rules::cannonGuns).
struct CannonGuns {
    Force guns;        // units that fire (their own Space Cannon values)
    RollMods galv;     // Galvanize: their extra dice
    int ul = 0;        // Ul the Progenitor's dice (Space Cannon 5)
    int starfall = 0;  // Starfall Gunnery's dice (Space Cannon 8)
    int dhonraz = 0;   // Dhonraz Installations' dice (Space Cannon 5)
    bool empty() const { return guns.empty() && !ul && !starfall && !dhonraz; }
};

struct LandSpec {
    int typeMask = 0;          // unit types that may be assigned (0 = ground forces; Matriarch: + fighters)
    std::vector<int> targets;  // allowed planets (empty = every planet in the system)
    int mustLand = -1;         // planet that must receive at least 1 unit (custodians)
    bool bombard = false;
};

// Waypoints (0.56.0, owner): a move plan may carry the route the player chose for the ships moving out of a system -
// one tuple per stop, in order: {kWaypointTuple, from system, stop system, 0}. Plans without them (the AI's, every
// older save's) are as before; the ships with a route roll for each gravity rift they leave on it.
constexpr int kWaypointTuple = -50;
std::map<int, std::vector<int>> moveRoutes(const std::vector<int>& plan);  // from -> its stops in order
std::vector<int> moveUnitsOnly(const std::vector<int>& plan);              // the plan without its route tuples

// Validators shared with the UI and AI (return "" if the plan is legal).
// fleetStrict (0.58.5): refuse a move that puts more ships in the active system than the fleet pool allows - the rule
// before fixes 22, and the bots' own planning since (the game allows it from fixes 22; the extra ships are removed
// right after the move - fixes 22 alone waited for a combat there)
std::string checkMovePlan(const Game& g, int p, int active, const std::vector<int>& data, bool allowStrand = false, bool fleetStrict = false);
std::string strandWarning(const Game& g, int p, int active, const std::vector<int>& data);
// 0.58.5: a legal move that takes the active system over the fleet pool - what happens then ("" when it doesn't)
std::string fleetOverWarning(const Game& g, int p, int active, const std::vector<int>& data);
// 0.58.5: will a combat (space or ground) happen in the active system after this player's move?
bool combatAhead(const Game& g, int p, int sys);
// fleetStrict (0.45.0): keep within the fleet pool even where the rules allow going over (the bots' own planning)
std::string checkProducePlan(const Game& g, int p, int sys, const std::vector<int>& data, int capacityLimit, int typeMask = 0,
                             bool fleetStrict = false);
std::string checkHits(const Game& g, int p, int sys, int planet, int hits, int mask, const std::vector<int>& data,
                      bool noSustain = false);
std::string checkLandPlan(const Game& g, int p, int sys, const std::vector<int>& data, const LandSpec& spec = LandSpec());
std::string checkPayment(const Game& g, int p, const Decision& d, const Answer& a);
constexpr int kTdSpace = 1 << 20;  // Transit Diodes option ids (0.44.5): ground forces in a space area
int maxAbsorbableHits(const Game& g, int p, int sys, int planet, int mask, bool noSustain = false);

int galvanizedOnBoard(const Game& g, int p);  // Last Bastion (rules_factions6.cpp)
int egeiroBonus(const Game& g, int p);         // The Egeiro: non-home systems with our planets
int coexistPlanets(const Game& g, int p);      // planets where p's units coexist (Oceanbound)

class Rules {
public:
    explicit Rules(Engine& e) : E(e), G(e.g()) {}
    void playGame();

private:
    Engine& E;
    Game& G;
    int turnSystemsMoved_ = 0;
    bool turnEnded_ = false;     // Coup d'Etat / Minister of Peace ended the active player's turn
    bool combatDraw_ = false;    // Skilled Retreat ended the space combat
    bool inSpaceCombat_ = false;
    int bunkerOwner_ = -1;       // Bunker: bombardment against this player's planets gets -4
    uint8_t notesHeld_ = 0;      // whose notes the active player held when the tactical action began
    bool blitz_ = false;         // Blitz: non-fighter ships get bombardment 6
    bool disabled_ = false;      // Disable: enemy PDS lose shield and cannon
    int warMachine_ = 0;         // War Machine played this production
    std::vector<int> predictions_[kMaxPlayers];  // riders: (card, outcome) pairs per player
    int curCard_ = -1;           // action card being resolved (copied into decisions)
    int curAgenda_ = -1;         // agenda being resolved (copied into decisions)
    uint8_t silenced_ = 0;       // Political Secret: can't vote or play action cards this agenda
    uint8_t cannotVote_ = 0;     // Assassinate Representative / riders: can't vote on this agenda
    int lastVoter_ = -1;         // Hack Election
    bool readyOnly3_ = false;    // Checks and Balances (Against)
    bool piUsed_[kMaxPlayers] = {};  // Predictive Intelligence votes this agenda
    bool freeSecondary_ = false; // Galactic Crisis Pact
    std::vector<int> retreatedFrom_;  // players who retreated out of the current combat

    struct RollResult {
        int hits = 0;
        std::vector<int8_t> dice, need;
        std::vector<int8_t> unit;   // unit type that rolled each die (-1 = extra die)
        std::vector<int8_t> bonus9; // extra hits when this die shows a natural 9 or 10 (J.N.S. Hylarim)
        bool valor = false;         // Valor (Kjalengard): each natural 10 makes 1 more hit
    };
    enum class RollKind { Combat, AFB, Bombard, Cannon };
    void recount(RollResult& r) const;

    // ---- action cards (rules_actions.cpp) ----
    std::vector<Played> window(const WinCtx& c);
    bool cardPlayable(int p, int card, const WinCtx& c) const;
    bool playActionCard(int p, int card, const WinCtx* c);  // false if it was cancelled
    void resolveCard(int p, int card, const WinCtx* c);
    void drawActionCards(int p, int n, const std::string& why);
    void discardActionCard(int p, int card, bool played);
    void discardAllActionCards(int p, const std::string& why);
    void enforceHandLimit(int p);
    bool cardHas(const std::vector<Played>& v, const char* key, int player = -1) const;
    std::vector<Option> componentActionOptions(int p);
    bool componentAction(int p, int option);  // false if cancelled (no action used)
    bool componentCardUsable(int p, int card) const;
    // Uncharted Space action cards (0.33.0, rules_uncharted.cpp)
    bool usCardPlayable(int p, int card, const WinCtx& c) const;
    bool usComponentUsable(int p, int card) const;
    bool usResolve(int p, int card, const WinCtx* c);
    std::vector<int> rigsPlanets(int p) const;
    void passWindow(int p);
    bool mustVote_ = false;  // Special Session: nobody may abstain on this agenda
    void confusedSage(int roller, int other, int sys, RollResult& r);  // after dice: +3 / -3 on one of them
    int classifiedWeapons(int p, int sys, int dice);                  // +4 for unit ability rolls (0 if not played)
    bool classified_ = false;                                         // ... and those hits can't be cancelled
    void purged(int p);                                               // Aggressive Broker
    bool fkReady(int p) const;                                        // Forbidden Knowledge in hand, playable
    std::vector<int> starpathSystems() const;                         // Abyssal Starpaths: systems without planets
    RollResult* sageRoll_ = nullptr;
    void bountyWindow();                                              // Bounty Contracts at the start of the status phase
    std::vector<std::pair<int, int>> bounty_;                         // (player, public objective)
    bool quietNoteReturn_ = false;                                    // a note going back without being used
    void startOfTurn(int p);
    std::vector<int> windowOrder() const;
    // tech component actions / abilities
    bool slingRelay(int p);
    void transitDiodes(int p);  // (0.44.5, fixes 9)
    void bioStims(int p);

    // ---- agendas (rules_agenda.cpp) ----
    void resolveAgenda(int index);
    void runAgenda(int a, bool miscount);
    int revealAgenda();
    bool agendaRevealable(int a) const;
    std::vector<Option> agendaOutcomes(int a) const;
    std::string outcomeName(int a, int outcome) const;
    void resolveOutcome(int a, int outcome, const std::vector<int>& votedFor, const std::vector<int>& votes);
    void addLaw(int a, int owner, int target);
    void discardLaw(int lawIndex, const std::string& why);
    void politicsPeek(int p);
    void applyPendingEffects();
    void agendaSecrets(int p);
    void votingStep(int a, std::vector<int>& votedFor, std::vector<int>& votes, uint8_t cannotVote, int lastVoter);
    void secondaryForFree(int p, int card);

    // ---- exploration and relics (rules_explore.cpp) ----
    int curExplore_ = -1;        // exploration card being resolved (copied into decisions)
    int curRelic_ = -1;          // relic being used (copied into decisions)
    void setupExploration();
    int drawExplore(int type);
    void explorePlanet(int p, int planet);
    void exploreFrontier(int p, int sys);
    void resolveExplore(int p, int card, int sys, int planet);
    void afterGainControl(int planet, int p, int old);
    void gainRelic(int p, const std::string& why);
    void takeRelic(int p, int relic);       // put a specific relic in p's play area (with its "when gained")
    void loseRelic(int p, int relic, bool purge);
    bool relicReady(int p, const char* key) const;
    bool holdsRelic(int p, const char* key) const;
    void exhaustRelic(int p, const char* key);
    void checkNexus(int sys);
    void legendaryAbilities(int p);
    // Uncharted Space legendary planets (0.33.0)
    std::vector<int> echoSystems(int p) const;
    std::vector<int> prismPurgeable(int p) const;
    std::vector<int> prismTargets(int p, int from) const;
    std::vector<Option> domnaShips(int p) const;
    std::vector<int> domnaDestinations(int p, int from) const;
    void tarrockPredict(int agenda);
    void tarrockCheck(int outcome);
    std::vector<std::pair<int, int>> tarrockPicks_;
    // Thunder's Edge (0.34.0, rules_te.cpp)
    void stationControl(int sys);  // the only player with ships in a system takes its space stations
    std::vector<int16_t> salvageOf(int p) const;  // action cards on the Salvage Yard p controls
    bool takeSalvage(int p, int card);            // ... one of them, taken off to be played
    void teEndOfTurn(int p);                      // The Acropolis, The Galactic Council
    void tePass(int p);                           // Maxis Central Control, Dok 'N Pic's Salvage Yard, Aeurex Mechanica
    void tempestaWindow(int p, int sys);          // Ionian Fuel Refinery, after activating
    bool stationConvert(int p);    // exhaust a space station: commodities -> trade goods
    // Thunder's Edge action cards and neutral units (0.34.0, rules_te2.cpp)
    bool teCardPlayable(int p, int card, const WinCtx& c) const;
    bool teComponentUsable(int p, int card) const;
    bool teResolve(int p, int card, const WinCtx* c);
    void placeNeutral(UnitType t, int k, int sys, int planet, const std::string& why);
    std::vector<int> pirateSystems() const;           // non-home systems without any player's ships
    std::vector<int> mercenaryPlanets() const;        // non-home planets without units
    std::vector<int> strategizeCards(int p, bool primary) const;  // readied or unchosen strategy cards worth it
    std::vector<std::pair<int, int>> rescueShips(int p, int sys) const;  // (system, unit type) that may come
    void teTurnStart(int p);                          // Extreme Duress window
    bool teTurnEnd(int p);                            // Puppets on a String, Crisis (true: the next turn is skipped)
    void teTransaction(int a, int b);                 // Lie in Wait
    void lastShipLost(int p, int sys);                // Crash Landing
    void duressCheck(int p, bool strategic);          // Extreme Duress: the duressed player's next action
    void coexistCleanup();                            // coexisting units left alone take the planet
    void coexistStructures();                         // LRR 63.4 / 79.4 for coexisting structures (fixes 25)
    void troAgent(int p, int sys);                    // T'ro (Sardakk agent), at the end of a tactical action
    void engageCoexisting(int p, int sys, const std::vector<int>& skip);  // 0.44.2: end a coexistence with the units there
    bool skipNext_ = false;                           // Crisis: the next player's turn is skipped
    // Thunder's Edge relics (0.34.0, rules_te3.cpp)
    void teTakeRelic(int p, int r);                   // Book of Latvinia
    void teRelicOptions(int p, std::vector<Option>& opts) const;
    bool teRelicAction(int p, int r);
    std::vector<int> circletSystems(int p) const;
    void heartOfIxth(int roller, int other, int sys, RollResult& r);
    void neuraloop(int obj);
    // Thunder's Edge part 3 (0.34.0, rules_te4.cpp / rules_breakthroughs.cpp)
    void gainBreakthrough(int p, const std::string& why);
    void btGained(int p);                             // "when you gain this card"
    std::vector<int> expeditionSlices(int p) const;   // slices p could pay for now
    void teExpedition(int p);                         // end of the turn: commit to the expedition?
    std::vector<int> teSpots() const;                 // where Thunder's Edge may be placed
    void completeExpedition(int last);
    std::vector<int> ingressCandidates(TechColor c) const;
    void fractureEnter(int p, bool byBreakthrough);
    void relicIcon(int planet, int p);
    // breakthrough abilities (rules_breakthroughs.cpp)
    void btOptions(int p, std::vector<Option>& opts) const;  // their component actions (6000 + Ab)
    bool btAction(int p, int ab);
    std::vector<Option> saarTargets(int p) const;     // Deorbit Barrage: planets in reach
    void btControl(int planet, int p);                // Fealty Uplink
    void btWonCombat(int winner, int loser, int sys); // N'orr Supremacy
    void btProduced(int p, int sys, const int counts[kUnitTypes]);  // Auto-Factories
    void btTurnStart(int p);                          // Thunder's Paradox
    int btCombatBonus(int p, int opp, int sys, bool space);  // Imperator
    void btActivated(int p, int sys);                 // Imperator's move
    int btLetnevBonus(int p, int sys);                // Gravleash Maneuvers: X
    // Thunder's Edge factions (rules_factions6.cpp, 0.35.0)
    // kind (fixes 27, the Last Bastion review): 0 any unit but structures; 1 a unit that took part in the combat (space: ships;
    // a planet: its ground forces); 2 Dame Briar - any unit but a space dock (a PDS is a unit: 1 more space cannon die)
    bool galvanize(int p, int sys, int planet, const std::string& why, int chooser = -1, int kind = 0);
    bool galvanizeInfantry(int p, int sys);
    void liberate(int planet, int p);
    void combatEnded6(int att, int def, int sys, int planet);  // end of any combat (Phoenix Standard, ...)
    void galvanizedLost(int p, UnitType t, int n, int sys, int planet);
    void dameBriar(int victim, int sys);
    void proximaTargeting(int att, int def, int planet);
    int proximaCancel(int q, int sys, int planet, int hits);
    int duranium(int p, Force& f, const Force& before);
    void alastorChoice(int p, int sys);                    // The Alastor: Nekro ground forces in space fight as ships
    bool nekroMalleon(int p);                              // (fixes 27) Nekro Malleon on any player
    bool splicerReady(int p, int o, int sys) const;        // Dimensional Splicer can fire now
    void dimensionalSplicer(int p, int o, int sys, bool beforeCannon = false);  // (beforeCannon: the context says so)
    void zgravFlip(int p, int sys);
    void wormholeGeneratorStatus();  // the original Wormhole Generator (no Codex): start of the status phase
    void nexusTokensBack();
    bool takeEmptyPlanet(int p, int x);
    int adjFlagshipZ_ = 0;
    uint16_t sleeperAsked_ = 0;
    std::vector<int16_t> activationSleepers_;  // (0.45.0) sleeper planets in the system when it was activated  // (0.44.9) players the Titans asked for a sleeper this turn
    std::vector<Option> sleeperAskSpots(int p) const;
    bool sleeperAsk(int p);  // Slumberstate Computing: ask another player for a sleeper on their planet  // (0.44.9) where Valefar Z copies of Hil Colish were when adjacency was last computed  // (0.44.9) "coexisting" infantry on an empty planet take it          // Creuss tokens leave a Wormhole Nexus that became active                        // Z-Grav Eidolon / Eidolon Maximum: mechs fight as ships  // Duranium Armor's repair (the type repaired, -1 none)  // Proxima Targeting VI: 1 bombardment hit less per galvanized unit
    void theIcon(int p, int sys, const int counts[kUnitTypes]);
    void survivalInstinct(int active, int sys);
    void miniaturizationEnd(int p);
    void nanoLink(int p, int sys);
    void ralnelRetreat(int p, int opp, int sys, int to);
    void alarum(int att, int def, int planet);
    bool directorNel();                       // true = the Ral Nel are no longer passed
    bool dataSkimmer(int discarder, int card);  // true = the card went onto Data Skimmer
    void skimmerPass(int p);
    std::vector<Option> factionActionOptions6(int p);
    bool factionAction6(int p, Ab ab);
    void startCoexisting(int p, int planet);
    void oceanLimit(int p);
    bool coexistInstead(int invader, int q, int planet, int skipWho = -1);  // Research Team / Viper EX-23: true = no combat
    bool carrina(int researcher);
    bool carrinaSpare(int researcher, int tech);  // (fixes 27) Doctor Carrina when a specialty planet would do instead
    void carryAlong(int p, int from, int to);     // (fixes 27) a ship moved by an ability takes its fighters / ground forces
    void ojz(int p, int opp, int sys, int notTo); // Watchful Ojz (fixes 27: at the retreat's declaration)
    void carrinaInfantry(int d, int researcher);
    void oceanCheck();                            // (fixes 27) Oceanbound's limit "any time" (before every question)
    int aello(int researcher, int cost);
    void statusStart6();
    void scarStatusStart();  // 0.43.4 (tester): entropic scars at the start of the status phase
    void statusEnd6();
    void eanautic(int active, int sys);
    std::vector<Option> factionActionOptions7(int p);
    bool factionAction7(int p, Ab ab);
    void placeBreach(int p, int sys, bool active);
    void flipBreach(int p, int sys);
    void incursion(int p, int sys);
    void sundered(int p, int sys);
    void sunderedCheck();  // (fixes 24: before every question - "move or are placed into your home system")
    bool breachPlaceable(int sys) const;  // (fixes 24: the 7 breach tokens - a free one, or an inactive one to move)
    void crimsonCombatEnd(int att, int def, int sys);
    void breachStatus();
    void subatomicSplicer(int p, UnitType t, int k);
    void phantomProduced(int p, int sys, const int counts[kUnitTypes]);
    void phantomProducedOne(int p, int sys, UnitType t, int k);  // (fixes 24: productions outside productionCore)
    void phantomCombat(int att, int def, int sys);
    std::vector<int> ravinPlayers() const;  // Ahk Ravin (fixes 24): players with ships in 2+ systems
    Force ravinCargo(int q, int sys, int type);
    bool ahkRavin(int p);
    int revenant(int p, int sys);  // (returns the mechs in space before it, -1 if none came)
    std::vector<Option> factionActionOptions8(int p);
    bool factionAction8(int p, Ab ab);
    // the Firmament / the Obsidian (rules_factions7.cpp)
    bool placePlot(int p, int victim, const std::string& why);   // a facedown plot card with victim's token
    void plotsWithinPlots(int p, const char* actionKey);         // score other players' secrets (nullptr: status phase)
    void blackOps(int receiver, int note);                       // Black Ops received: purged
    void becomeObsidian(int p);                                  // Puppets of the Blade
    void siphon(int q, int gained);                              // the puppet gained commodities
    void extractCopy(int q, int tech);                           // the puppet gained a technology
    void firmamentGained(int p, int tech);                       // Planesplitter: the Fracture comes
    int assail(int p, int opp) const;                            // +1 to rolls against the puppet
    void heavensEye(int att, int def, int sys);                  // repair at the end of a combat round
    void myruVos(int p, int sys);                                // no space cannon against the moving ships
    void myruVos25(int p, int sys);                              // ... fixes 25 (exact card: see rules_factions7.cpp)
    int16_t myruQuiet_[kMaxPlayers] = {};                        // Myru Vos for another player (fixes 25): "don't ask again this round"
    int16_t vosQuiet_[kMaxPlayers] = {};                         // Vos Hollow as a bystander (fixes 25): the same
    void vosHollow(int att, int def, int sys, const Force& beforeA, const Force& beforeD);
    void reapingWon(int att, int def, int sys, int planet);      // a combat won against a puppet
    void reapingWin(int w, int l, int sys);                      // ... fixes 25: the combat's own winner (combatWon)
    void firmamentStatusStart();
    void firmamentStatusEnd();
    void obsidianTurnStart(int p);                               // Neural Parasite (Obsidian)
    void planesplitter(int p);                                   // after a strategic action (Obsidian)
    void malevolency(int p);                                     // end of a tactical action: pass it on
    bool enervate(int q, int card);                              // true: the Leadership primary was used
    bool enervateFree(int q, int card) const;                    // no command token for the secondary
    void commodityGained(int p, int before);                     // fixes 25: Siphon for a commodity gain outside replenished()
    void sowingBank(int p);                                      // The Sowing: trade goods onto the card
    // Discordant Stars (rules_ds.cpp, 0.37.0)
    void dsStartingTechs(int p);
    void dsCombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m);
    void dsCombatRoundStart(int att, int def, int sys, int planet, bool space);
    void dsSpaceCombatStart(int att, int def, int sys);
    int dsAfbDice(int side, int sys) const;                 // Projection of Power, Trade Port
    bool dsCargoRaiders(int who, int opp, int sys);         // true = the retreat is off
    bool dsCantRetreat(int who, int opp, int sys) const;    // (Raider cruisers)
    int dsRestrictedHits(int shooter, int victim, int sys, const RollResult& r) const;
    int dsUncancellable(int shooter, int victim, int sys, const RollResult& r) const;
    void dsShipsLost(int victim, int opp, int sys, const Force& before);
    int dsCancelHits(int victim, int opp, int sys, int planet, int hits);
    void dsWonCombat(int winner, int loser, int sys, bool space);
    void dsGroundCombatStart(int att, int def, int planet);
    void dsGroundCombatOver(int att, int def, int planet, int winner);
    int dsDrawWinner(int att, int def) const;               // Subversive (Kyro): -1 = nobody
    int dsSaveUnits(int p, UnitType t, int k, int sys, int planet, int byPlayer);
    void dsUnitsDestroyed(int p, UnitType t, int k, int sys, int planet, int byPlayer);
    void dsControl(int planet, int p, int old);
    void dsActivated(int p, int sys);
    void dsEndTactical(int p, int sys);
    void dsTurnStart(int p);
    void dsTurnEnd(int p);
    void dsExplored(int p, int planet);
    int dsRedraw(int p, int type, int c);                   // Vassa Hagi: another exploration card
    bool dsRelicGained(int p, int r);                       // A New Edifice: true = purged instead
    void dsFragmentsPurged(int p, int n);
    void dsAfterPolitics(int p);
    void dsStatusStart();
    void dsStrategyStart();
    void dsAgendaRevealed(int a);
    void dsAfterVoting(std::vector<int>& voted, std::vector<int>& counts);
    void dsAgendaResolved(int outcome, const std::vector<int>& voted);
    bool dsZealous(int q);                                  // Warfare secondary as the primary
    void dsOptions(int p, std::vector<Option>& o);
    bool dsAction(int p, Ab ab);
    void dsInvade(int p, int planet);
    bool dsProduceOne(int p, int sys, int discount, const std::string& why);
    int dsBlockRetreat_[kSides] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};  // Lost Cause: no retreat to this system
    int dsRerolls_[2] = {0, 0};      // ATS Armaments: misses to reroll this round
    int dsDiscount_ = 0;             // production cost reduction for the next single production (Moult, Brood Pod)
    uint32_t dsOncePerCombat_ = 0;   // Deliverance Engine, Queen Lorena (bit per player); Exodus Engineering (bit 15, fixes 8: 16)
    uint16_t dsProjection_ = 0;      // Projection of Power: a ship with Anti-Fighter Barrage 6 (bit per player)
    int dsShipsAtStart_[kSides] = {};  // non-fighter ships at the start of the space combat (Moult)
    int dsRiderGuess_[kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};  // Kyro Rider predictions
    int combatRound_ = 0;
    // Discordant Stars batch 2 (rules_ds2.cpp, 0.38.0)
    void ds2CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m);
    void dsBombardDice(int p, int sys, RollResult& r, int mod, const Force& shooters);
    void dsBombardHits(int p, int q, int sys, int pl, RollResult& r);
    void dsInvasionStart(int p, int sys);
    void dsGroundRollers(int att, int def, int planet, Force& fa, Force& fd);
    int dsCannonDice(int q, int p, int sys) const;
    void dsCannonLimit(int q, int p, Force& guns) const;
    int dsAbilityMod(int p) const;
    void dsSpaceRoundEnd(int att, int def, int sys);
    void dsResearched(int p, int t);
    bool docksIn2(int p, int sys) const;
    void dsSpecialtyUsed(int p, const std::vector<int>& planets);
    int dsWaivers(int p, int t, bool use);
    void dsSecretDrawn(int p);
    void dsSecretScored(int scorer, int obj);
    void dsPassed(int p);
    void dsStrategySpent(int p);
    bool dsStoneEmbrace(int p);
    void dsDockPlaced(int p, int planet);
    void dsAfterMovement(int p, int sys, bool destroyerMoved);
    void dsAfterProduction(int p, int sys);
    void ds2TurnEnd(int p);
    void dsAgendaPhaseStart();
    void ds2Options(int p, std::vector<Option>& o);
    bool ds2Action(int p, Ab ab);
    void dsAfterComponent(int p);
    void ds2Activated(int p, int sys);
    bool dsConstructionPrimary(int q);
    int dsUdosh(int payer, int amount);
    void dsResolvePlanet(int p, int planet);
    int dsAfbDice5(int side, int sys) const;
    int dsAfbBombard_ = -1;            // Davish S'Norri: the ship type whose barrage bombards this invasion
    std::vector<int> dsSlingPlanets_;  // Meteor Slings: planets with infantry thrown down this invasion
    int dsTypeMask_ = 0;               // Obsessive Designs: production limited to these unit types
    bool dsDrawing_ = false;
    // Discordant Stars batch 3 (rules_ds3.cpp, 0.39.0)
    void ds3CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m);
    void ds3SpaceCombatStart(int att, int def, int sys);
    void ds3GroundCombatStart(int att, int def, int planet);
    void dsAfterCombatRolls(int att, int def, int sys, int planet, RollResult& rA, RollResult& rD);
    void ds3SpaceRoundEnd(int att, int def, int sys);
    void dsGroundRoundEnd(int att, int def, int planet);
    void ds3WonCombat(int winner, int loser, int sys, bool space);
    int ds3SaveUnits(int p, UnitType t, int k, int sys, int byPlayer);
    void ds3UnitsDestroyed(int p, UnitType t, int k, int sys, int planet, int byPlayer);
    void ds3Activated(int p, int sys);
    void ds3EndTactical(int p, int sys);
    void ds3TurnStart(int p);
    void ds3TurnEnd(int p);
    void ds3Explored(int p, int planet);
    void dsFrontierExplored(int p, int sys);
    int dsExploreDeck(int p, int planet, int type);  // Deep Mining (-2: a trade good instead), Black Markets
    int dsRelicDraw(int p, int r);                    // Data Leak, Underground Market
    void dsObjectiveRevealed();                       // Oracle AI
    bool dsPsychographics(int p);                     // true = a second public objective was scored
    void ds3StatusStart();
    void ds3AgendaRevealed();
    void ds3AgendaResolved(int outcome, const std::vector<int>& voted);
    void ds3AfterVoting(std::vector<int>& voted, std::vector<int>& counts);
    void ds3Passed(int p);
    void ds3Researched(int p, int t);
    void ds3Options(int p, std::vector<Option>& o);
    bool ds3Action(int p, Ab ab);
    int dsProductionBonus(int p, int sys, int capacity);  // Durran, Merged Replicators
    int dsPrivateer(int p, const int counts[], int cost);  // the part of the cost paid with influence
    bool dsBlackmail(int p, int card);                     // Blackmail Programs: true = the turn ends
    void dsUnitRolled(int roller, int target, int sys, RollResult& r, const char* what);  // Wayfinder, Orion Platform
    void ds3AfterMovement(int p, int sys, bool flagshipMoved);
    void dsLanded(int p, int sys, const std::vector<int>& landed);
    void ds3Fragments(int p);
    void ds3AttachNotes(int p, const char* only = nullptr);
    void dsGlory(int p, int sys, bool moveOnly, const std::string& why);
    std::vector<int> ds3Explored_;  // Secret Maps: planets the Bentor explored this tactical action
    uint16_t ds3Vassal_ = 0;        // Vassalage: players who played it in this combat (bit per player)
    int ds3Agendas_ = 0;            // agendas revealed in this agenda phase (Quaxdol Junitas)
    int onlyUpgradeTypes_ = 0;      // Glory: research only unit upgrades of these unit types (bit per type)
    bool researchDry_ = false;      // 0.43.4: researchStep only says whether anything could be researched
    int dsCostCap_ = 0;             // Maximus: combined cost limit of this production
    // Discordant Stars batch 4 (rules_ds4.cpp, 0.40.0)
    void ds4Setup(int p);
    void ds4CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m);
    void ds4SpaceCombatStart(int att, int def, int sys);
    void ds4AfterCombatRolls(int att, int def, int sys, int planet, RollResult& rA, RollResult& rD);
    void ds4GroundRoundStart(int att, int def, int planet);
    void ds4GroundRoundEnd(int att, int def, int planet);
    void ds4WonCombat(int winner, int loser, int sys, bool space);
    void ds4ShipsLost(int victim, int opp, int sys, const Force& before);
    void ds4UnitsDestroyed(int p, UnitType t, int k, int sys, int planet, int byPlayer);
    void ds4Activated(int p, int sys);
    void ds4EndTactical(int p, int sys);
    void ds4TurnStart(int p);
    void ds4Explored(int p, int planet);
    int dsFrontierRedraw(int p, int c);  // Voidsailors
    void ds4StatusStart();
    void ds4AfterVoting(std::vector<int>& voted, std::vector<int>& counts);
    int dsVoteBonus(int p, const std::vector<int>& spent);  // Control (Olradin policy)
    void ds4Passed(int p);
    void ds4Control(int planet, int p, int old);
    void ds4Landed(int p, int sys, const std::vector<int>& landed);
    void ds4AfterMovement(int p, int sys);
    void ds4Produced(int p, int sys, const Force& made);
    bool dsDiscarded(int p, int card, bool played);    // Cybernetic Madness (true = purged), Skhot Unit X-12
    int dsAutonetic(int p, int n);                     // Autonetic Memory: cards still to draw
    void dsSecondaryDone(int q, int card);             // Fine Print
    void dsSustained(int victim, int attacker, int sys, int planet, int t);  // Krovoz Strike Teams
    void ds4PlanetsExhausted(int p, const std::vector<int>& planets);
    bool dsRetreatCapture(int p, int sys);             // Nightingale V
    void ds4UnitRolled(int roller, int target, int sys, RollResult& r, const char* what);
    bool dsEradica(int p, int sys) const;              // Eradica: removed instead of destroyed
    void ds4Options(int p, std::vector<Option>& o);
    bool ds4Action(int p, Ab ab);
    void dsReturnCaptured(int p, UnitType t, int k, int sys, int planet);
    Force ds4StartForce_;                              // Shrouded Skirmishers: the Kollecc ships here at the activation
    bool acCancelled_ = false;  // playActionCard: the card being discarded was cancelled (Cybernetic Madness doesn't purge it)
    // Discordant Stars batch 5 (rules_ds5.cpp, 0.41.0)
    void ds5CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m);
    void ds5SpaceCombatStart(int att, int def, int sys);
    void ds5GroundCombatStart(int att, int def, int planet);
    void ds5AfterCombatRolls(int att, int def, int sys, int planet, RollResult& rA, RollResult& rD);
    void ds5RoundEnd(int att, int def, int sys, bool space);
    void ds5UnitsDestroyed(int p, UnitType t, int k, int sys, int planet, int byPlayer);
    void ds5WonCombat(int winner, int loser, int sys, bool space);
    void ds5GroundCombatOver(int att, int def, int planet, int winner);
    void ds5Activated(int p, int sys);
    void ds5AfterMovement(int p, int sys);
    void ds5EndTactical(int p, int sys);
    void ds5Produced(int p, int sys, const Force& made);
    int ds5ProductionBonus(int p, int sys);              // Industry Secrets
    void ds5Control(int planet, int p, int old);
    void ds5Explored(int p, int planet);
    void ds5TurnStart(int p);
    void ds5TurnEnd(int p);
    void ds5Passed(int p);
    void ds5StatusStart();
    void ds5StrategyStart();                              // Prescient Memories: the Omen dice
    void ds5AgendaRevealed(int a);
    void ds5AgendaResolved(int outcome, const std::vector<int>& voted);
    int ds5VoteBonus(int p, const std::vector<int>& spent);  // Envoy Network
    int ds5CancelHits(int victim, int opp, int sys, int planet, int hits);
    void dsAfterPrimary(int p, int card);                 // Grace (Edyn)
    void dsReplenished(int p, bool replenish = true);                         // Military Industrial Complex (Axis)
    void dsOmen(int p, RollKind kind, RollResult& r);     // Divination, Lactarius Indigo (Myko-Mentori)
    void dsAfterRoll(int p, RollKind kind, RollResult& r);  // 0.43.0: Lactarius Indigo and Gift of Insight on any roll
    bool dsKadryn(int p, int scorable);                   // Kadryn (Edyn commander): a secret instead of a public
    void dsSoulCaged(int p, int leader);                  // Soul Cage: another player's hero attached instead of purged
    void dsRetreatExtra(int p, int sys, std::vector<int>& out) const;  // Nokar Navigator, Jack Hallard, Freelance Outfit
    int dsDiplomats(int p, int amount, bool influence, const std::string& why, int& planet);  // Diplomats (Free Systems)
    void ds5Options(int p, std::vector<Option>& o);
    bool ds5Action(int p, Ab ab);
    void ds5Hero(int p, int nth, const std::string& text);  // purge hero nth (the Ghemina have two) + feed line
    void ghemSync();                                      // The Lady & The Lord: keep track of the Lady
    void extraAction(int q);                              // Allant: 1 action out of turn
    int dsEdynGuess_ = -1;                                // Radiance / Unity Algorithm: the Edyn's prediction
    int dsUnityGuess_[kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};  // 0.44.6: Unity Algorithm copies' predictions
    int dsPredictionOf(int p) const;  // (the AI votes for it)
    int dsEdynRider_[kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};  // Edyn Rider predictions
    int dsRetreating_ = -1;                               // who announced a retreat this round (Jack Hallard)
    int dsDiscount2_ = 0;                                 // Industry Secrets: 1 cheaper this production
    uint8_t dsOncePerAction_ = 0;                         // bit 0 Rally to the Cause, bit 1 Diplomats
    bool inDiplomats_ = false;
    bool graceFree_ = false;                              // Grace: this secondary costs no command token
    int ladyCount_ = 0;                                   // flagships in the Lady's system at the activation
    int qarnyxRound_ = -1;                                // Psyclobea Qarnyx: once per round of space combat
    // Discordant Stars batch 6 (rules_ds6.cpp / rules_ds7.cpp, 0.42.0)
    void dsBranchReceived(int p, int note);
    int dsSolis(int p, int amount, int& planet);          // Solis Morden: research paid with influence
    void ds6CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m);
    void ds6SpaceCombatStart(int att, int def, int sys);
    void ds6SpaceRoundEnd(int att, int def, int sys);
    void ds6Activated(int p, int sys);
    void ds6EndTactical(int p, int sys);
    void ds6Landed(int p, int sys, const std::vector<int>& landed);
    void ds6Control(int planet, int p, int old);
    void ds6TurnStart(int p);
    void ds6TurnEnd(int p);
    void ds6StatusStart();
    void ds6Options(int p, std::vector<Option>& o);
    bool ds6Action(int p, Ab ab);
    void ds7Options(int p, std::vector<Option>& o);
    bool ds7Action(int p, Ab ab);
    void ghotiSync();                                     // Mobile Command: the Ghoti home follows their flagship
    void mirvedaBlockade();                               // Gauss Cannons blockaded are destroyed
    void ds7CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m);
    void ds7SpaceRoundEnd(int att, int def, int sys);
    void ds7InvasionStart(int p, int sys);
    void ds7Landed(int p, int sys, const std::vector<int>& landed);
    void ds7Activated(int p, int sys);
    void ds7AfterMovement(int p, int sys);
    void ds7EndTactical(int p, int sys);
    void ds7Produced(int p, int sys, const Force& made);
    int ds7ProductionBonus(int p, int sys);
    void ds7TurnStart(int p);
    void ds7StatusStart();
    void ds7Researched(int p, int t);
    int ds7VoteBonus(int p, const std::vector<int>& spent);
    int dsGhotiPlanet(int p, int amount, bool influence, const std::string& why);
    bool inParallel_ = false, inLogic_ = false, inGhoti_ = false, inSolis_ = false;
    int onlyColours_ = 0;                                 // Logic Machina: research only these colours (bit per colour)
    int dsGrid_ = -1;                                     // Interference Grid: this player rolls 1 lower this combat
    struct Hired {
        int buyer = -1, sys = -1;
        int n[kUnitTypes] = {};
    } hired_;                                             // Hired Guns: the Nokar ships fighting for the active player
    struct HireCtx {                                      // 0.43.0 (D110): Hired Guns negotiated as a deal
        int nokar = -1, buyer = -1, sys = -1, max = 0, agreed = 0;
    } hireCtx_;
    const Deal* prefill_ = nullptr;                       // an offer that starts filled in (Hired Guns)
    void hiredGunsDeal(int nk, int p, int sys);
    // galactic events (rules_events.cpp, 0.36.0)
    void setupEvents();
    void minorFactions();
    void wildsGuard(bool setup);
    void eventOptions(int p, std::vector<Option>& o);
    bool eventAction(int p, Ab ab);
    bool darkEnergyExplore(int p);
    void eventDestroyed(int p, UnitType t, int k, int byPlayer);
    void eventControl(int planet, int p, int old);
    void orthodoxyCheck(int p);
    void voidMoved(int p, int sys, bool moved);
    void eventsStatusEnd();
    void eventsRoundEnd();
    void eventsStatusStart();
    bool monumentInstead(int p, int planet);  // Monuments to the Ages: 5 trade goods for a monument
    void monumentPoints();                    // each player's points from the monuments they hold
    void weirdWormholes(int p, int sys, const std::vector<std::pair<int, int>>& ships);
    std::vector<Option> factionActionOptions9(int p);
    bool factionAction9(int p, Ab ab);
    // part 3 (the rest)
    int avernusPlanet() const;                        // Stellar Genesis (Muaat): the Avernus planet (-1: not in play)
    void avernusCarry(int p, int sys, int avSys, int wsBefore);  // a war sun takes Avernus along
    bool avernusThrough_ = false;  // (fixes 26) a war sun of the mover went through Avernus's system (set by movement)
    void yinAscendant(int p);                         // gain a random unused faction's Alliance ability
    bool slumberstate(int p, int planet);             // Titans: coexist instead of a Coalescence ground combat
    void btStatusDraws();                             // Slumberstate Computing: action cards for coexisting pairs
    void wingTransferEnd(int p, int sys);             // Argent: ships move among the Wing Transfer systems
    void voidTether(int p, int sys);                  // Empyrean: place or move a Void Tether
    void voidTetherAllow(int p, int sys);             // (fixes 26) a person playing the Empyrean may let p cross tethers
    void tetherCannonAllow(int q, int p, int sys);    // (fixes 26) ... and q's space cannon fire across one
    void custodiaTokens(int scorer);                  // Custodian's Favour: Imperial's Mecatol point gives 2 tokens
    void eidolonLost(int p);                          // Eidolon Maximum destroyed: the card flips back
    bool forcedSecondary_ = false;                    // Strategize: the secondary without the yes / no question
    // Uncharted Space exploration cards and relics (0.33.0, rules_uncharted2.cpp)
    bool usExplore(int p, int card, int sys, int planet);  // its Instant cards (true if it was one)
    bool redistributeOne(int p, Tag tag, const std::string& why);
    void planetsExhausted(int p, const std::vector<int>& planets);  // Arcane Citadel
    int placeNewSystem(int p, int def, Tag tag, const std::string& why);
    bool starChartAction(int p);
    bool azdelsKey(int p);
    std::vector<int> throneSecrets(int p) const;
    bool forgottenThrone(int p);
    void e6g0(int p);
    void eyeOfVogul(int p, int sys);
    void twilightMirror();
    bool fragmentAction(int p, int type);
    bool relicAction(int p, int relic);
    bool enigmaticAction(int p);
    void psychoarchaeology(int p);
    void afterTactical(int p, int sys);
    void scanlink(int p, int sys);
    void destroyPlanet(int planet, int by, const char* how = "the Stellar Converter");
    void ionStormUsed(int sys);
    void statusRelics();
    void mawOfWorlds();
    bool scepter(int p);                    // Scepter of Emelpar: spend from reinforcements instead?
    bool prophetsTears(int p, bool needPrereq);
    void purgeFragments(int p, int count, int type, const std::string& why);

    // ---- factions, leaders, faction notes (rules_factions.cpp) ----
    int curAbility_ = 0;          // Ab being resolved (copied into decisions)
    size_t unlockLogSize_ = size_t(-1);  // checkUnlocks: game log length at the last check
    int unlockRound_ = -1;
    bool inGroundCombat_ = false;
    bool removing_ = false;       // units are being removed, not destroyed (no "when destroyed" effects)
    // The fleet pool at all times (0.46.1, fixes 15): fleetStateCheck runs before every question
    int fleetHold_ = 0;                    // ships are moving: the check waits for the end of the move
    int fleetDeferSys_ = -1;               // 0.58.5 (fixes 22): the active system's fleet pool waits for its combat and invasion
    bool inFleetCheck_ = false;
    bool inSunderedCheck_ = false;
    uint64_t sunderedSig_[8] = {};  // (what other players have in each Crimson home after the last check)
    bool fleetDirty_ = false;              // a ship was placed in a space area since the last check
    size_t fleetLogSize_ = size_t(-1);     // game log length at the last check
    void fleetStateCheck();
    bool byHits_ = false;
    bool dsEndTurnNow_ = false;
    // Discordant Stars breakthroughs (0.43.0, rules_dsbt.cpp)
    int16_t dsLendQuiet_[kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};  // Arms Brokerage: "don't ask again this round"
    int16_t nomadQuiet_[kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};   // Thunder's Paradox (0.44.4): the same
    int16_t florzenQuiet_[kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1}; // Reverie Implants (0.44.4): the same
    int sabotaged_ = -1;  // 0.44.6: while a Sabotage is played - player * 1000 + the card it would cancel
    int16_t zhrgarQuiet_[kMaxPlayers] = {};  // 0.44.6: Zhrgar Stimulants - no more questions about others' rolls this round
    bool mustProduce_ = false;  // 0.44.5: the next production must make at least 1 unit if it can (Contractual Obligations)
    bool notProductionAbility_ = false;  // 0.44.5: the next production comes from another ability than PRODUCTION (no Sarween / AIDA)
    int groveNoCommit_ = -1, groveTurn_ = -1;  // Mended Grove (0.44.4): the planet whose slung infantry can't be committed this action
    // High Security Vault (Vaden, 0.44.4 fixes 8): true = the Vaden's vault keeps it (asks a human Vaden)
    bool vaultShut(int victim, int taker, const std::string& what) {
        return G.fixes >= 8 && (G.modules & kModDiscordant) && breakthroughsOn(G) && dsbtVault(victim, taker, what);
    }
    bool groveBlocks(int p, int planet) const {
        return G.fixes >= 8 && planet == groveNoCommit_ && groveTurn_ == G.turnCount && dsbtOn(p, "khrask");
    }
    bool dsbtOn(int p, const char* key) const;
    bool dsbtReady(int p, const char* key) const;
    void dsbtGained(int p);
    void dsbtUnits(int p);
    void dsbtTurnStart(int q);
    void dsbtTurnEnd(int p);
    void dsbtReplenished(int p);
    bool dsbtExploreCard(int p, int planet, int c);
    void dsbtTechExhausted(int p);
    void dsbtWonCombat(int winner, int loser, int sys, bool space);
    void dsbtControl(int planet, int p);
    void dsbtPass(int p);
    void dsbtPurged(int q);
    void dsbtOptions(int p, std::vector<Option>& o) const;
    bool dsbtAction(int p, int ab);
    bool dsbtLanefirExplore(int p);
    void dsbtTribune(int p, RollResult& r);
    // batch 2
    void exploreFrontierDeck(int p, int sys);         // the frontier deck there, token or not
    bool dsbtExploreInstead(int p, int planet);       // Historian Conclave: the frontier deck instead (true = done)
    int dsbtMuseum(int p, int type);                  // The Collector's Museum: a card from the discard pile (-1 = none)
    void dsbtSpentStrategy(int p);                    // Galactic Movement
    void dsbtEndTactical(int p, int sys);             // Professional Intrigue
    void dsbtCoexistEnded(int owner, int by);         // Professional Intrigue / Hidden Hardpoints
    bool dsbtGroundRoundEnd(int att, int def, int planet);  // Hidden Hardpoints (true = the combat ends), Dreamwalkers
    void dsbtPolicy(int p);                           // Insurrectionist Networking
    void dsbtAfterMovement(int p, int sys);           // Gravitic Disruption
    void dsbtSpaceCombatStart(int att, int def, int sys);  // Mirage Craft
    void dsbtInfantryDied(int owner, int planet, int k);   // Dreamwalkers: captured
    bool dsbtSlingsCoexist(int p, int planet, int n);  // Mended Grove
    // batch 3
    void dsbtShipsLost(int victim, int opp, int sys, const Force& before);  // Exodus Engineering
    bool dsbtQuickRetreat(int who, int sys);           // Raiding Parties: the retreat at once (true = done)
    void dsbtCombatDice(int p, RollResult& r);        // Raiding Parties (roll twice), Awakened Planetoids (planets fight)
    void dsbtTurnEndCheiran(int p);                   // Matriphagy
    bool dsbtVault(int victim, int taker, const std::string& what);  // High Security Vault: true = blocked
    bool dsFreeProd_ = false;                         // Matriphagy: the next production costs nothing
    int e6g0Quiet_[kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};  // 0.43.0 (U9): "don't ask again this turn" (turnCount)
    int brikkClause_ = -1;        // 0.43.0 (D17): Brikk Strain - the one clause of the primary a non-Kyro player resolves
    bool brikkOk(int k) const { return brikkClause_ < 0 || brikkClause_ == k; }   // 0.43.0 (D12): Troubadour - "then you may end your turn" (no action, not passed)         // 0.43.0: units are being destroyed by assigned hits (Silence of Stars needs them)
    bool noRetreatToken_ = false; // Skilled Retreat under Wild, Wild Galaxy
    bool shval_ = false;          // Sh'val: skip to committing ground forces
    struct GhomUnit {             // (fixes 25) a ground force G'hom Sek'kus brought in, and where from
        int16_t planet;
        int8_t type;
        bool damaged;
    };
    std::vector<GhomUnit> ghomBrought_;
    int ghomBefore_[2] = {0, 0};  // infantry / mechs in the active system's space before G'hom Sek'kus
    bool warfareTac_ = false;     // Thunder's Edge Warfare: this tactical action places no token, any system
    bool zeu_ = false;            // Z'eu Omega (Codex): this tactical action places no command token, homes excluded
    bool activated_ = false;      // 0.43.3: the last tacticalAction activated a system (it wasn't backed out of)
    int tekklar_[kSides] = {};  // Tekklar Legion modifier for the current ground combat
    bool warFunding_[2] = {};        // War Funding played this space combat round (attacker, defender)
    int fpick(int p, Ab ab, const std::string& prompt, std::vector<Option> opts, const std::string& context = "",
              bool onMap = false, bool onPlanets = false);
    std::vector<int> fpickMany(int p, Ab ab, const std::string& prompt, std::vector<Option> opts, int minPick, int maxPick,
                               const std::string& context = "", bool onPlanets = false);
    bool fyes(int p, Ab ab, const std::string& prompt, const std::string& context = "");
    bool fac(int p, const char* key) const { return isFaction(G, p, key); }
    void factionSetup(int p);
    void checkUnlocks();
    void exhaustAgent(int p, int nth = 0);
    void purgeHero(int p);
    std::vector<Option> xxchaHeroSpots(int p) const;  // (0.57.1) Thunder's Edge Xxekir Grom: "PDS on X" (id 2x) / "Mech on X" (2x+1)
    void readyLeaders();
    std::vector<Option> factionActionOptions(int p, bool freeOnly);  // component actions (6000+) / free options (700+)
    bool factionAction(int p, int option);   // false if cancelled
    void statusFactions();                   // end of the status phase (Genesis)
    void roundEndFactions();                 // end of the round (Darktalon Treilla)
    void startOfTurnFactions(int p);         // Military Support, infantry coming back
    bool afterActivation(int p, int sys);    // E-Res Siphons, Nullification Field, Spatial Conduit; true = turn ended
    void endOfTacticalFactions(int p, int sys);  // T'ro
    bool shvalCheck(int p, int sys);         // Sh'val Harbinger
    void ghomSekkus(int p, int sys);         // move ground forces in before landing
    void afterDiplomacy(int p);              // Peace Accords
    void endOfStrategyPhase();               // Quantum Datahub Node
    bool quashCheck(int agenda);             // Quash / Political Favor: true = discard and reveal another
    int extraVotes(int p, int outcome);      // Gila the Silvertongue
    bool cancelActionCard(int player, int card);  // Instinct Training
    bool harruhgFree(int p);                 // Hacan hero: units free this production
    int sucaban(int p, int cost);            // Doctor Sucaban: reduced research cost
    void researchAgreement(int p, int tech);
    void infantryDestroyed(int p, int k);    // Infantry II: roll to come back
    RollMods combatMods(int p, int opp, int sys, int planet, bool space);
    void combatRoundStartFactions(int att, int def, int sys, int planet, bool space, RollMods mods[2], bool munitions[2]);
    void afterCombatRolls(int att, int def, int sys, RollResult& rA, RollResult& rD, bool munitions[2]);
    void afterSpaceRound(int att, int def, int sys);  // Exotrireme II
    void groundCombatStartFactions(int att, int def, int planet);
    void sustained(int victim, int attacker, int sys, int planet, int t);  // after a unit uses Sustain Damage
    void afterUnitAbilityRoll(int roller, int sys, RollResult& r);        // Agnlan Oln
    // batch 2 (rules_factions2.cpp)
    uint16_t destroyedShipTypes_ = 0;  // ship types destroyed in the current space combat (Salvage Operations)
    int combatSys_ = -1;               // system of the current space combat
    int ipswitch_ = -1;                // player using Ipswitch, Loose Cannon in the current space combat
    void gainTG(int p, int n);         // every trade good gain goes through here (Pillage)
    bool canUseAgent(int who, const char* factionKey) const;  // own agent ready, or Ssruu (Yssaril)
    void spendAgent(int who, const char* factionKey);
    void mitosis();                          // start of the status phase
    void bioplasmosis();                     // end of the status phase
    void oracle();                           // end of the status phase (Naalu hero)
    int extraStatusTokens(int p);            // Cybernetic Enhancements
    void telepathic();                       // end of the strategy phase: Naalu "0" token, Gift of Prescience
    void afterMoveFactions(int p, int sys);  // Stymie, Foresight
    bool assimilate(int planet, int p, int old);  // L1Z1X: true = the structures became p's
    void harrow(int att, int def, int planet);
    void spaceCombatStart2(int att, int def, int sys);  // Ipswitch, Ambush, Impulse Core
    void devotion(int att, int def, int sys);
    void spaceCombatEnded(int att, int def, int sys, int winner);  // Salvage Operations, S'ula Mentarion
    void groundCombatStart2(int att, int def, int planet);         // Indoctrination, Greyfire Mutagen
    void yinSpinner(int p, int sys);
    void unitDestroyedFactions(int p, UnitType t, int k, int sys, int planet);  // Van Hauge, Brother Milor, Ipswitch
    int restrictedHits(int p, int sys, const RollResult& r) const;  // [0.0.1]: hits that must go to non-fighters
    void afterActivation2(int p, int sys);   // Neuroglaive, So Ata, Dirzuga Rophal, Duha Menaimon, I48S
    void startOfTurn2(int p);                // Spy Net
    void agendaRevealed2(int agenda);        // Z'eu
    bool fightersLand(int p, int sys) const; // Matriarch: fighters take part in this invasion
    void returnLandedFighters(int p, int sys);
    void dannelInvade(int p, const std::vector<int>& planets);
    std::vector<Option> factionActionOptions2(int p);
    bool factionAction2(int p, Ab ab);
    // batch 3 (rules_factions3.cpp)
    int strategicPlayer_ = -1;        // player resolving the current strategic action (Acquiescence)
    bool mathis_ = false;             // (fixes 26, the Winnu review) Mathis Mathinus' secondaries: not a strategic action
    int jaeToken_ = -1;               // (fixes 26) Jae Mir Kan paid this secondary with this player's token from the board
    bool alastor_[kSides] = {};  // The Alastor: Nekro's ground forces in space fight in this space combat
    // (fixes 27, the Nekro review - owner 2026-11-14) The Alastor: "choose any number of your ground forces in this system" -
    // those chosen on the system's planets fight from the space area meanwhile (they never count for capacity and go back to
    // their planets after the combat or a retreat), the space area's own left out wait aside (alastorAside_)
    struct AlastorFrom {
        int8_t player = -1;
        int16_t planet = -1;
        uint8_t n[2] = {};  // infantry, mechs
    };
    std::vector<AlastorFrom> alastorFrom_;
    Force alastorAside_[kSides];
    uint8_t alastorOwn_[kSides][2] = {};  // the space area's own ground forces fighting (infantry, mechs)
    int alastorSys_ = -1;
    void alastorReturn(int p);                       // they go back (idempotent)
    void alastorChoice27(int p, int sys);
    int alastorMask(int p) const;                    // the ground forces fighting as ships, as a unit mask (fixes 27)
    bool shipsOrAlastor(int p, int sys) const;       // ships - or ground forces fighting as ships (fixes 27)
    int alastorFromPlanets(int p, int ti) const;     // of them in the space area now, from the planets (ti 0 infantry, 1 mechs)
    bool singularityUsed_ = false;    // Technological Singularity asked in this combat
    int combatSide_[2] = {-1, -1};    // the two players of the current combat
    int singularitySys_ = -1;         // (fixes 27) the system of the space combat from its very start (nekroCombatStart)
    int nekroGuess_ = -1;             // Galactic Threat: predicted outcome of the current agenda (-1 = none)
    int nekroRound_ = -1;             // round of the last Galactic Threat prediction (once per agenda phase)
    int maxUnitCost_ = 0;             // Umbat: produced units must cost at most this (0 = no limit)
    void spendStrategy(int p, int n = 1);  // spend strategy tokens (Magmus gives trade goods)
    void refreshUnits(int p);              // unit cards from technologies (Valefar Assimilator)
    void secondFlagship(int p);            // (0.52.0) the Ghemina's Lady: a unit of her own
    bool moveCreussToken(int p, Ab ab, int intoSys, bool optional);
    void afterActivation3(int p, int sys);  // Icarus Drive, Taivra, Mendosa, Aetherstream, Aetherpassage
    void afterMove3(int p, int sys, int wormholeCarriers, bool warSunMoved);  // Sai Seravus, Ba'al, Acamar, Xuange, Voidwatch
    void startOfTurn3(int p);               // Creuss Iff, Chaos Mapping
    void gainedControl3(int planet, int p, int old);  // Scavenge, Scavenger Zeta
    void endOfTactical3(int p, int sys);    // Reclamation, Reclaimer
    void spaceCombatStart3(int att, int def, int sys);   // Alastor, Dimensional Splicer, Antivirus
    void nekroCombatStart(int att, int def, int sys);    // (fixes 27) Singularity's sides + Antivirus, before any kill
    void groundCombatStart3(int att, int def, int planet);  // Antivirus
    void singularity(int victim, int sys, int planet);  // Technological Singularity
    void dynamo(int owner, int sys, int planet, int t, int unitOwner = -1);  // (fixes 26: any player's unit)
    void acamar(int p, int sys);  // (fixes 26) Acamar (Empyrean agent) after any player's move into a system without planets
    bool watcher(int player, int card);
    void combatMods3(int p, int opp, int sys, int planet, bool space, RollMods& m);
    int productionDiscount3(int p, int sys);  // Berekar Berekon, Hegemonic Trade Policy
    void afterProduction3(int p, int sys, const Force& made);  // Rowl Sarrig
    bool raghsCall(int lander, int planet);
    bool propagation(int p, int resourceCost);
    bool gainTechFrom(int p, const std::vector<int>& from, const std::string& why);
    void nekroPredict(int agenda);
    void nekroResolve(int agenda, int outcome, const std::vector<int>& votedFor);
    void bloodPact(std::vector<int>& votedFor, std::vector<int>& votes);
    std::vector<Option> factionActionOptions3(int p);
    bool factionAction3(int p, Ab ab);
    // batch 4 (rules_factions4.cpp)
    bool heshPrit_ = false;           // Hesh and Prit: secondaries paid with tokens from reinforcements
    bool zgrav_[kSides] = {};    // Z-Grav Eidolon: the Naaz-Rokha mechs in space fight as ships in this combat
    int cavalry_[2] = {-1, -1};       // The Cavalry: ship type with the Nomad flagship's values (attacker, defender)
    bool cavalrySustain_[2] = {};     // The Cavalry: that ship can still absorb a hit
    int lastActiveSystem_ = -1;       // system activated in the current turn (Field Marshal Mercer)
    bool ilNa_ = false;               // Il Na Viroset: the active system already held the active player's token
    std::vector<int> awakened_;       // planets where Awaken put Hel-Titans this tactical action (Coalescence)
    bool noRetreat_ = false;          // Airo Shir Aur: nobody can retreat from this combat
    int curValue_ = 0;                // extra number copied into decisions (Decision::value) for the AI
    int curFor_ = -1;                 // an agent used for another player (deal): Decision::targetPlayer
    bool tgFromPlayer_ = false;       // 0.43.0: these trade goods come from another player (deal, loan), not the supply
    bool artunoPay_ = false;          // Artuno's trade goods are being paid out (not from the supply)
    bool canUseNomadAgent(int who, int nth) const;  // 0 Mercer, 1 Thundarian, 2 Artuno (or Ssruu)
    void spendNomadAgent(int who, int nth);
    void readyAgent(int p, int index);              // ready an exhausted agent (Artuno pays out)
    void capture(int by, int owner, UnitType t, int k, const char* why);
    bool returnCaptured(int p, UnitType t);         // give back 1 captured unit of that type
    void capturedBlockade();                        // LRR 17.2c: blockaded docks give captured units back
    bool cannotCaptureFrom(int by, int owner) const;  // LRR 17.6
    int abilityDice(int p, int sys, bool plasma = true);  // extra unit-ability dice: Plasma Scoring, Trrakan Aun Zulok, Ambuscade
    void addDice(RollResult& r, int count, int need, int unit = -1);
    void raidFormation(int p, int o, int sys, int excess);
    void strikeWingAlpha(int p, int o, int sys, const RollResult& r);
    int ulCannonDice(int q, int sys) const;         // Ul the Progenitor: Elysium's Space Cannon 5 (x3)
    int zeal(int p) const;                          // extra votes (Argent)
    int geneticRecombination(int voter, int agenda);  // forced outcome, or kNoVote
    void futureSight(int outcome, const std::vector<int>& votedFor, const std::vector<int>* preds);
    void edictWon(int winner, int loser);
    bool afterActivation4(int p, int sys);          // Starlancer, Awaken, Ouranos, Crucible; true = turn ended
    void endOfTactical4(int p, int sys);            // Il Na Viroset, captured units and blockades
    void coalescence(int p, int sys);               // ground combat where Awaken put Hel-Titans
    void endOfTurn4(int p);                         // Garv and Gunn, Field Marshal Mercer
    void strategyStart4();                          // Scepter of Dominion
    bool jaeMirKan(int q, bool ask);                // Mahact agent pays the secondary's token
    void explored4(int p, int planet);              // Terragenesis, Pre-Fab Arcologies
    int distantSuns(int p, int planet, int type, int first);  // second card to choose from: the card kept
    void gainedControl4(int planet, int p, int old);  // Dart and Tai
    void unitDestroyed4(int p, UnitType t, int k, int sys, int planet);  // Devour, The Terror Between, Reanimator, Crimson Legionnaire
    void combatMods4(int p, int opp, int sys, int planet, bool space, RollMods& m);  // Arvicon Rex, Visz El Vir, Z-Grav
    void combatRoundStart4(int att, int def, int sys, int planet, bool space, RollMods mods[2]);  // Supercharge
    bool thundarian(int att, int def, int sys, int hitsA, int hitsD);
    int spaceHitsCancel(int victim, int side, int sys, int hits, int attacker = -1);  // Quantum Manipulator, The Cavalry
    int quantumManipulator(int victim, int sys, int hits, bool afb);  // (fixes 26: also against anti-fighter barrage)
    void mercer26(int p, int sys);  // (fixes 26) Field Marshal Mercer at the end of any player's turn
    void garvGunn27(int p);         // (fixes 27) Garv and Gunn at the end of any player's turn
    void mendosa27(int p, int sys);  // (fixes 27) Captain Mendosa when any player activates a system
    int sucaban27(int p, int cost);  // (fixes 27) Doctor Sucaban for any player's research; a person picks the infantry
    void dameBriar27(int victim, int sys);  // (fixes 27) Dame Briar when any player's unit is destroyed
    void rowlSarrig27(int p, int sys, int fighters, int inf);  // (fixes 27) Rowl Sarrig: each new unit at any dock
    void floatingSweep();            // (fixes 27) every blockaded floating factory destroyed (end of a turn)
    int tellurian27(int victim, int sys, int planet, int hits, int mask);  // (fixes 27) Tellurian for any player's units
    void zeuTE(int who, int sys);    // (fixes 27) Z'eu (Thunder's Edge): a command token placed - back to the reinforcements
    void iconoclastTE(int gainer);   // (fixes 27) Iconoclast (Thunder's Edge): a mech when another player gains a relic
    void foresightPickup(int q, int from, int to);  // (fixes 27) Foresight: ground forces along
    bool absoluteSynergyReady(int p) const;  // (fixes 27) Absolute Synergy offered without using the action (option 645)
    void cavalryDice(int side, int p, RollResult& r, RollKind kind);
    void spaceCombatStart4(int att, int def, int sys);  // Z-Grav Eidolon, The Cavalry
    void spaceCombatEnd4(int att, int def, int sys);
    int tellurian(int victim, int sys, int planet, int hits, int mask);
    void afterProduction4(int p, int sys, const Force& made, int gfPlanet);  // Trilossa Aun Mirik, Tungstantus
    int amalgamation(int p, int sys, const int counts[kUnitTypes]);  // resources saved
    bool riftmeld(int p, int tech);
    void replenished4(int p);                       // The Stillness of Stars
    bool hecatoncheires(int p, int planet);         // a mech and an infantry instead of a PDS
    std::vector<Option> notesToGive(int from, int to) const;  // Hubris: nobody gives Mahact their Alliance
    std::vector<Option> factionActionOptions4(int p);
    bool factionAction4(int p, Ab ab);

    // batch 5: the Council Keleres (rules_keleres.cpp, 0.21.0)
    bool asnUsed_ = false;            // Agency Supply Network used in the current action
    bool inAsn_ = false;              // ... and its extra production is running
    int execVoter_ = -1;              // Executive Order: the Keleres vote as speaker (trade goods and resources count)
    int odlynn_ = -1, odlynnGuess_ = -1;  // Odlynn Myrr used on the current agenda: by whom, and the prediction
    int riderGuess_[kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};  // Keleres Rider predictions on the current agenda
    int16_t lawsOrderQuiet_[kMaxPlayers] = {};  // Law's Order: a human said "don't ask again" this round
    int16_t xanderQuiet_[kMaxPlayers] = {};     // Xander when not needed (fixes 27): the same
    int16_t carrinaQuiet_[kMaxPlayers] = {};    // Doctor Carrina when not needed (fixes 27): the same
    bool carrinaNo_ = false;                    // (fixes 27) the Deepwrought said no: the researcher picks again
    int16_t peaceQuiet_[kMaxPlayers] = {};      // Minister of Peace: the same (0.28.0)
    int16_t icarusQuiet_[kMaxPlayers] = {};     // Icarus Drive (fixes 24): the same
    int16_t taivraQuiet_[kMaxPlayers] = {};     // Emissary Taivra for another player (fixes 24): the same
    int16_t acamarQuiet_[kMaxPlayers] = {};     // Acamar for another player (fixes 26): the same
    int16_t mercerQuiet_[kMaxPlayers] = {};     // Field Marshal Mercer for another player (fixes 26): the same
    int16_t thundarianQuiet_[kMaxPlayers] = {}; // The Thundarian in another player's combat (fixes 26): the same
    int16_t tetherQuiet_[kMaxPlayers] = {};     // Void Tether "allow it?" (fixes 26): the same
    int16_t trilossaQuiet_[kMaxPlayers] = {};   // Trilossa Aun Mirik for another player (fixes 26): the same
    int16_t garvQuiet_[kMaxPlayers] = {};       // Garv and Gunn for another player (fixes 27): the same
    int16_t mendosaQuiet_[kMaxPlayers] = {};    // Captain Mendosa for another player (fixes 27): the same
    int16_t sucabanQuiet_[kMaxPlayers] = {};    // Doctor Sucaban for another player's research (fixes 27): the same
    int16_t briarQuiet_[kMaxPlayers] = {};      // Dame Briar for another player's units (fixes 27): the same
    int16_t tellurianQuiet_[kMaxPlayers] = {};  // Tellurian for another player's units (fixes 27): the same
    int16_t zeuQuiet_[kMaxPlayers] = {};        // Z'eu (Thunder's Edge) on another player's token (fixes 27): the same
    bool zeuBusy_ = false;
    int mutedFor_ = -1;  // (fixes 27, Xxekir Grom of Prophecy of Kings) only this player resolves abilities - the others are
                         // not asked (faction questions declined, no action cards)
    void wingTransfer(int p, int sys);          // Wing Transfer (Argent breakthrough): the tokens (fixes 26: right after activation)
    void emissaryTaivra(int p, int sys);        // Emissary Taivra (fixes 24): after p activated sys
    void icarusDrive(int c, int p, int sys);    // Icarus Drive (fixes 24): after p activated sys
    int16_t schemeQuiet_[kMaxPlayers] = {};     // (0.44.9) Deepgloom Executable's Scheming: "don't ask again this round"
    int stallNoTurn_[kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};  // (fixes 27) the Yssaril refused Stall Tactics this turn
    int16_t schemeAsked_[kMaxPlayers] = {};     // (0.44.9) ... a bot asked the Yssaril this round already
    void sharedScheming(int p);                 // Deepgloom Executable: another player borrows Scheming
    uint32_t tradeChosen_ = 0;                  // players the Trade primary refreshed this time (0.29.0)
    bool lastComponent_ = false;      // the action just taken was a component action (Suffi An)
    bool inXander_ = false;           // a payment with commodities as trade goods is running
    void councilPatronage();
    void lawsOrder(int active);
    bool artemirisToll(int p, int sys);
    int omniopiaresToll(int p, int planet) const;
    void agencySupply(int p, int sys = -1);
    bool slingRelayCan(int p, int s) const;  // (0.44.7)
    void emberColossus(int p, int s);        // (0.44.8)
    std::vector<int> pilgrimSystems_;  // 0.44.7: Pilgrimage Beacons - systems of specialty planets exhausted this action
    void pilgrimage(int p);
    void eresSiphons(int p, int sys);
    void neuroglaive(int p, int sys);
    void magenActivation(int sys);  // Magen Defense Grid (Codex): infantry with each structure in an activated system
    void contractualProduce(int q);
    bool executiveOrder(int p);
    void keleresAgendaRevealed(int agenda);
    void keleresAfterVoting();
    void keleresAgendaResolved(int outcome);
    void kuuasiHero(int att, int def, int sys);
    void suffiAnUnlock(int p, int card);
    bool suffiAnExtra(int p);
    int xanderOffer(int payer, int amount, bool influence, bool mixed);
    bool payCore(int p, int amount, bool influence, const std::string& why, bool optional, int extraInfluence, int tradeGoodsToo, bool mixed);
    void productionCore(int p, int sys, int capacityLimit, bool viaWarfare, bool shipsOnly);
    std::vector<Option> factionActionOptions5(int p);
    bool factionAction5(int p, Ab ab);

    // ---- negotiations (rules_deals.cpp) ----
    bool dealInProgress_ = false;  // components are changing hands (Pillage waits until the deal is done)
    void resetDealCounters();
    void markHostile(int a, int b);
    void clearHostile(int a, int b);
    std::vector<Option> negotiationPartners(int p) const;
    bool canNegotiate(int p) const;
    int dealFlagsFor(int p, int q) const;
    bool askDeal(int p, int q, Tag tag, const Deal* base, Deal& out);
    int askResponse(int q, const Deal& deal, bool allowCounter);
    void recordDeal(const Deal& d, int result);
    void negotiate(int p, int partner);
    void negotiateWith(int p, int q);  // (negotiate, after Black Market Dealings may have been played)
    void finishDeal(const Deal& d, int r, bool original);
    void refusedEffects(const Deal& d);
    bool executeDeal(const Deal& d);
    void breakPromise(Promise& p, const std::string& how);
    void keepPromise(Promise& p);
    void promiseActivate(int p, int sys);
    void promiseCombat(int att, int def, int sys);
    void promiseVote(int p, int agenda, int outcome, int votes);
    void promiseEndOfTurn(int p);
    void promiseUsedAgent(int owner, int beneficiary);
    // Who may use this faction's agent for player b: b itself (own agent or
    // Ssruu), else a player who promised b its agent in a deal; -1 = no one.
    int agentUser(int b, const char* factionKey) const;
    // Before / after asking 'user' about an agent for 'b' (marks the question
    // and the promise as kept).
    void agentFor(int user, int b) { curFor_ = user != b ? b : -1; }
    void agentDone(int user, int b);
    void promiseRoundEnd();
    void loansDue();
    void agendaNegotiations(const std::vector<int>& order);
    void pillageCheck(int p);  // Pillage (Mentak) after p gains trade goods or resolves a transaction

    // ---- personalities (rules_personality.cpp, 0.12.0) ----
    void setupPersonalities();
    void bfChange(int bot, int toward, float amount);    // raw change (locked while a betrayal is armed / active)
    void bfEvent(int bot, int toward, float weight);     // + archetype gain x weight
    void bfEveryone(int toward, float weight);           // every other bot
    void persTurnStart(int p);
    void persEndOfTurn(int p);
    void kingslayEndOfTurn(int p);
    void persRoundEnd();
    void persActivate(int p, int sys);
    void persMoveNear(int p, int sys);
    void persCombatStart(int att, int def, int sys, int planet);
    void persSpaceCombatEnd(int att, int def, int sys);
    void persControl(int planet, int oldOwner, int p);
    void persDealRefused(const Deal& d);
    void persDealMade(const Deal& d);
    void persOffered(const Deal& d);
    void persPromise(const Promise& p, bool kept);
    void persLoanUnpaid(int by, int to);
    void persVotes();
    void giveBetrayal(int bot, int target);
    void announceBetrayal(int bot, int target, int kind, int arg);  // kind = Betrayal
    void betrayalDone(int bot, bool completed);
    int persShips_[2] = {0, 0};  // non-fighter ships at the start of the current space combat (Scavenger)
    int persCombatKey_ = -1;     // the last attack counted (one grudge per attack)

    // ---- promissory notes (rules_notes.cpp) ----
    void giveNote(int from, int to, int note);
    void returnNote(int holder, int note);
    void receiveNote(int p, int note);
    bool playNoteWindow(int holder, int type, int owner, const std::string& prompt);
    void supportForTheThroneCheck(int p, int sys);
    void tradeAgreement(int owner);
    bool ceasefire(int active, int sys);

    // ---- helpers (rules_common.cpp) ----
    std::string pn(int p) const { return playerName(G, p); }
    // 0.30.0: a feed line kept out of the game log (EventKind::Note), so old saves replay unchanged
    void feedNote(int player, const std::string& text);
    std::string sysName(int s) const;
    std::string planetName(int pl) const;
    void say(int p, const std::string& text, int sys = -1);
    void event(EventKind kind, int p, const std::string& text, int sys = -1, int planet = -1);
    int pick(int p, Tag tag, const std::string& prompt, std::vector<Option> opts, const std::string& context = "",
             bool onMap = false, bool onPlanets = false);
    std::vector<int> pickMany(int p, Tag tag, const std::string& prompt, std::vector<Option> opts, int minPick,
                              int maxPick, const std::string& context = "", bool onPlanets = false);
    bool yesNo(int p, Tag tag, const std::string& prompt, const std::string& context = "");
    // Pay a cost. Returns false if the player can't (or, when optional, won't) pay.
    // mixed: each planet may pay its resources or its influence (Freelancers).
    bool pay(int p, int amount, bool influence, const std::string& why, bool optional, int extraInfluence = 0,
             int tradeGoodsToo = 0, bool mixed = false);
    void gainVP(int p, int n, const std::string& why);
    void loseVP(int p, int n, const std::string& why);
    void gainTokens(int p, int n, bool redistribute, const std::string& why);
    int tokensInPools(int p) const;
    int tokensOnBoard(int p) const;
    int reinforcementTokens(int p) const;
    void placeTokenFromReinforcements(int p, int sys, const std::string& why);
    void chooseRemovals(int p, int sys, int count, int mask, const std::string& why);
    void checkEliminations();
    void eliminate(int p);
    void addUnits(int p, UnitType t, int k, int sys, int planet);
    void destroyUnits(int p, UnitType t, int k, int sys, int planet, int byPlayer, const char* how);
    void setControl(int planet, int p);
    void researchTech(int p, int tech, bool researched = true);
    void exhaustTech(int p, const char* key);
    bool techReady(int p, const char* key) const;
    bool owns(int p, const char* key) const;
    void checkWin();
    void removeExcessCapacity(int p, int sys);
    void enforceFleetLimit(int p);  // remove ships beyond the fleet pool
    void enforceFleetIn(int p, int sys);

    // ---- setup and rounds (rules_round.cpp) ----
    void setupGame();
    void galaxyDraft(const std::vector<int>& factionBySeat);  // setup: players place the dealt system tiles
    void relicUsedEvent(int p, int relic, bool purge);         // EventKind::RelicUsed
    void setupFaction(int p);
    void keleresStartingTechs(int p);
    void strategyPhase();
    void actionPhase();
    bool takeTurn(int p, bool secondAction, bool mayPass = false);  // mayPass: an extra action in their own turn (0.46.1)
    void endOfTurn(int p);
    void statusPhase();
    void agendaPhase();
    void revealObjective();

    // ---- tactical action (rules_tactical.cpp) ----
    void tacticalAction(int p);
    void movement(int p, int sys);
    void spaceCannonOffense(int p, int sys);
    CannonGuns cannonGuns(int q, int p, int sys) const;  // q's space cannon offense in p's tactical action (fixes 25: Myru Vos asks too)
    // 0.44.5 (fixes 9): Graviton Laser System / Antimass Deflectors with every space cannon shot at ships
    bool askGraviton(int q, int target, int sys);
    void cannonHits(int target, int sys, int hits, int mask, bool gls, const std::string& why, int q);
    int antimass(int q, int target) const;
    void invasion(int p, int sys);
    void production(int p, int sys, int capacityLimit, bool viaWarfare, bool shipsOnly = false);
    void integratedEconomy(int p, int planet);
    void checkFloatingFactories(int sys);
    void endTactical();

    // ---- combat (rules_combat.cpp) ----
    void crownOfThalnos(int p, int sys, int planet, RollResult& r);
    RollResult rollUnits(int p, const Force& f, RollKind kind, int modifier, int extraDice, int fighterBonus = 0,
                         const RollMods* mods = nullptr);
    void rerollDice(int p, RollResult& r, bool allDice);
    void afterUnitRoll(int roller, int target, int sys, RollResult& r, const char* what);
    void reportRoll(int p, int sys, const std::string& what, const RollResult& r);
    void neutralHits(int sys, int planet, int hits, int mask, const std::string& why, int attacker);  // Thunder's Edge
    void assignHits(int victim, int sys, int planet, int hits, int mask, const std::string& why, int attacker);
    void spaceCombat(int attacker, int defender, int sys);
    void groundCombat(int attacker, int defender, int planet);
    std::vector<int> retreatOptions(int p, int sys) const;
    void doRetreat(int p, int from, int to);
    void retreatCargo(int p, int from);  // (0.54.0) ground forces from the planets go along
    void combatWon(int winner, int loser, int sys, bool space);
    void combatStart(int attacker, int defender, int sys, int planet);
    void bombardment(int p, int sys);
    void spendTokensForObjective(int p, int count, const std::string& why);

    // ---- strategy cards (rules_strategy.cpp) ----
    void strategicAction(int p, int card);
    void primaryAbility(int p, int card);
    void secondaryAbility(int p, int card);
    void leadership(int p, bool primary);
    void diplomacy(int p, bool primary);
    void politics(int p, bool primary);
    void construction(int p, bool primary);
    void trade(int p, bool primary);
    void tradeX1(int p, std::vector<int>& done);  // 0.17.0: the community X-1 (Setup::trade bit 3)
    void replenished(int p, int gained = 0);  // gained: how many commodities (Siphon)
    void warfare(int p, bool primary);
    void technology(int p, bool primary);
    void imperial(int p, bool primary);
    bool researchStep(int p, int resourceCost, const std::string& why, int exclude = -1);
    bool specialistCompounds(int p, int cost);        // Jol-Nar breakthrough (Thunder's Edge)
    int onlyColour_ = -1;
    bool onlyGeneric_ = false;   // researchStep: generic technologies only (Visionaria Select)
    bool inTriad_ = false;
    bool inCustodia_ = false;
    bool inOcean_ = false;                            // pay(): The Triad already offered                             // researchStep: only this colour (Specialist Compounds)
    void readyPlanets(int p, int n);
    void placeStructure(int p, int sys, bool allowDock, bool allowPds, bool optional);

    // ---- objectives (rules_objectives.cpp) ----
    void scoringStep();
    bool scoreObjective(int p, int obj);  // handles spending; returns true if scored
    bool payObjectiveCost(int p, int obj);  // what scoring it costs (discards, spending); false = can't
    std::vector<int> scorablePublic(int p) const;
    std::vector<int> scorableSecrets(int p, ObjPhase phase) const;
    void actionSecret(int p, const char* key);  // score an action-phase secret now, if held
    void drawSecret(int p);
};

}  // namespace ti
