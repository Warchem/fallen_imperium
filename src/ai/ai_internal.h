// Pieces shared between the AI's source files.
#pragma once
#include "engine/decision.h"
#include "engine/rng.h"
#include "engine/state.h"

#include <vector>

namespace ti {

// Action cards, agendas, notes and laws (ai_cards.cpp). Returns false if the
// decision isn't one of those.
bool aiCardsAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out);
// How much playing this component action (option id from the action menu) is worth now.
float aiComponentValue(const Game& g, int p, int option);

// Exploration, relics, legendary planets (ai_explore.cpp).
bool aiExploreAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out);
float aiExploreComponentValue(const Game& g, int p, int option);  // options 3000+

// Faction abilities, leaders, faction notes (ai_factions.cpp).
bool aiFactionAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out);
float aiFactionComponentValue(const Game& g, int p, int option);  // options 6000+ (and free 700+)
float aiFreeTacticalValue(const Game& g, int p);              // ai.cpp: what a free tactical action is worth
bool aiTeAnswer(const Game& g, const Decision& d, Answer& out);   // Thunder's Edge questions (ai_te.cpp)
bool aiTeComponentValue(const Game& g, int p, int ab, float& v);  // breakthrough component actions
bool aiCoupNow(const Game& g, int p);                              // the Firmament: take Puppets of the Blade now
int aiHomeSpentValue(const Game& g, int p);                        // resources + influence of exhausted home planets
bool aiDsAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out);  // Discordant Stars questions (ai_ds.cpp)
bool aiDsComponentValue(const Game& g, int p, int ab, float& v);           // their component actions
// From ai_cards.cpp, for the faction AI.
float aiOutcomeValue(const Game& g, int p, int agenda, int outcome);
int aiPredictOutcome(const Game& g, int agenda, float* confidence);
bool aiWantsCancel(const Game& g, int p, int card, int by = -1);  // would p cancel this action card (as with Sabotage)?
float aiCardKeepValue(int card);                     // how much the AI likes holding an action card

// ai.cpp
float aiTechWant(const Game& g, int p, int tech);
float aiPlanetValue(const Game& g, int p, int pl);
float aiStrategyCardValue(const Game& g, int p, int card);  // 0.34.0: what a strategy card is worth to p now

// Galaxy setup draft (ai_galaxy.cpp): where to place a tile.
bool aiGalaxyAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out);

// Negotiations (ai_deals.cpp)
bool aiDealAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out);
bool aiWantsToNegotiate(const Game& g, int p);         // the action-choice "Negotiate" option
float aiTradeConvoysValue(const Game& g, int p);      // 0.32.0: play Trade Convoys (a component action)?
float aiDarkPactValue(const Game& g, int p);          // 0.32.1: play Dark Pact (a component action)?
int aiSecretToReturn(const Game& g, const Decision& d);  // 0.33.0: the held secret worth least (AI Augury)
// Trade card (0.14.0): p holds Trade and decides about q; q answers p's X-1 offer.
bool aiTradeX1Offer(const Game& g, int p, int q);
bool aiTradeGift(const Game& g, int p, int q);
bool aiTradeX1Accept(const Game& g, int q, int p);
// 0.17.0 community X-1 (Setup::trade bit 3)
std::vector<int> aiTradeX1Picks(const Game& g, int p, const Decision& d);
int aiTradeX1Response(const Game& g, int q, int p, int kind);  // 1 accept, 2 pay 1 now, 0 decline
bool aiTradeX1Settle(const Game& g, int q, int p);             // send the commodities?
float aiDealNet(const Game& g, int p, const struct Deal& d);  // what the deal is worth to p, in trade goods
bool aiPromiseForbids(const Game& g, int p, int sys);  // p promised to stay out of this system (and means it)
bool aiMeansToKeep(const Game& g, int p, const struct Promise& pr, int index);  // personalities: some promises are hollow
float aiPromisePlanValue(const Game& g, int p, int sys, int enemy, bool fight);  // plan adjustments from deals
float aiMovePlanValue(const Game& g, int p, int sys);  // (0.44.7) the move planner's value of acting in sys now
bool isYesNoOptions(const Decision& d);

// ---- goals (ai_goals.cpp): what the AI is working towards this round ----------
// Tuning knobs for testing: TI4_AITUNE="name=value,name=value" overrides the defaults.
float aiTune(const char* name, float def);
// one victory point, in the AI's plan units (a planet is ~3-4)
#define kVP (aiTune("kvp", 10.f))
struct Goals {
    int focus = -1;  // Competitive: the objective this player concentrates on this round
    // planets: extra value for one more planet with this property
    float trait[4] = {};  // by int(Trait)
    float skip = 0, attach = 0, legendary = 0, nonHome = 0, otherHome = 0, nearOtherHome = 0, anyPlanet = 0, mecatol = 0;
    float sharedSystem = 0;           // a planet in a system where someone else has one (Stake Your Claim)
    float resPts = 0, infPts = 0;     // per resource / influence point of a planet
    // structures and units
    float structure = 0, structOutside = 0, pds = 0, dock = 0, dread = 0, mech = 0, flagshipOrWarSun = 0, supremacy = 0;
    float fleetShip = 0;              // one more non-fighter ship in our biggest fleet
    int fleetTarget = 0;              // ... until it has this many
    float production = 0, groundStack = 0;
    // technology
    float upgrade = 0, factionTech = 0, color[4] = {};
    // economy: keep this much unspent for the status phase
    int keepRes = 0, keepInf = 0, keepTg = 0, keepTokens = 0;
    float tg = 0;                     // extra value of each trade good (trade-good objectives)
    float actionCard = 0, fragment = 0, law = 0, neighbour = 0;
    float combat = 0, passLate = 0;   // action-phase secrets
    bool presence = false;            // some presence objective is open (see aiPresenceValue)
    int plannedPublic = -1;           // the public objective we expect to score this status phase
    int metPublics = 0;               // public objectives we meet now and haven't scored
    // the player running away with the game (-1: nobody yet) and how much slowing them is worth
    int leader = -1;
    float leaderThreat = 0;
    // per system, filled on first use: presence value with ships / value lost if our ships leave
    std::vector<float> presShips, presHold;
};
const Goals& aiGoals(const Game& g, int p);
float aiLevelScale(const Game& g);  // 0 Easy, 0.6 Normal, 1 Hard (and Competitive)
int aiLevelOf(const Game& g);       // 0-3: the difficulty of the computer player being answered for
struct AiSeat {                     // answer as player p for a while (their difficulty, Competitive...)
    explicit AiSeat(int p);
    ~AiSeat();
    int keep_;
};
// Competitive (difficulty 3): Hard plus sharper play - fewer wasted turns, personalities
// toned down where they would cost the game, objectives and Mecatol Rex pushed harder.
bool aiCompetitive(const Game& g);
bool aiCompetitiveSeat(const Game& g, int p);  // the same, for player p (head-to-head tests)
extern int g_aiSeat;
float aiUrgency(const Game& g, int p);  // close to winning, every point counts more
int controlledCountTrait(const Game& g, int p, Trait t);
// Value of getting units (ships if withShips) into a system for presence objectives.
float aiPresenceValue(const Game& g, int p, int sys, bool withShips);
// Value lost if our last ships leave a system that counts for a presence objective.
float aiPresenceHold(const Game& g, int p, int sys);
// Resources (or influence) p can spend now and still pay what it keeps for the status phase.
float aiExploreCardValue(int c);  // how good an exploration card is to whoever resolves it
int aiFreeSpend(const Game& g, int p, bool influence, const std::vector<int>& exclude = {}, int tgUsed = 0);
bool aiReserveOk(const Game& g, int p, const std::vector<int>& exclude, int tgUsed);

// ---- personalities (ai_style.cpp, 0.12.0) -------------------------------------
// How a computer player's archetype, evolution path, traits and betrayal
// objective shape its play this turn. Defaults (on = false) = the plain AI.
struct Style {
    bool on = false;
    // the board
    float aggression = 1.f;    // value of attacks
    float vs[8] = {1, 1, 1, 1, 1, 1, 1, 1};  // ... against each player
    float minOdds = 0.6f;      // fight odds needed to attack
    float expansion = 1.f;     // value of planets nobody owns
    float farPenalty = 0.f;    // plan units lost per step beyond 2 from home
    float mecatol = 1.f;       // Mecatol Rex
    int homeGuard = 0;         // extra ships kept at home
    int fleetTokens = 0;       // wanted fleet pool (0 = as usual)
    float stage = 1.f;         // moving units into position a turn ahead
    float anomaly = 0.f;       // value of going into anomalies (Reckless)
    float pres = 1.f;          // presence / empty systems
    float explore = 0.f;       // planets still to explore, frontier systems
    float smallFights = 1.f;   // vs fleets of 1-2 ships
    float bigFights = 1.f;     // vs fleets of 4+ ships
    float retreatOdds = 0.25f; // retreats below these odds
    bool neverRetreat = false;
    int homeHitTarget = -1;    // a player whose home system it wants (Kneecapper, Hive Command...)
    float homeHit = 0.f;
    // what to build / research / pick
    float card[8] = {};        // strategy card bonuses
    float unit[kUnitTypes];    // production preferences (1 = as usual)
    float research = 1.f;
    const char* techPref[3] = {nullptr, nullptr, nullptr};
    float pdsPref = 0.f;       // Construction: PDS
    int tgKeep = 0;            // trade goods it likes to keep
    // deals
    float tra = 1, cea = 2, sup = 4, all = 2, pol = 1, comm = 1, agent = 1, noteMult = 1;  // price list
    float noteAdj = 0.f, notePay = 1.f, acValue = 2.f, fragment = 1.f;
    float dealRate = 1.f;      // how readily it proposes
    bool persistent = false;   // re-offers after a refusal (Politician)
    float acceptBias = 0.f;    // more needed to accept (trade goods)
    float biasVs[8] = {};      // ... from each player
    float acceptChance = 1.f;  // share of merely fair deals it takes (Warlord: 0.3)
    bool refuseFrom[8] = {};   // declines everything from these players
    float keepPromise = 1.f;   // share of its promises it means to keep
    float threat = 0.f;        // demands backed by threats
    float peaceDeals = 0.f;    // proposes no-attack pacts
    float allianceDeals = 0.f; // proposes alliances it may break (Betrayer)
    float promiseSell = 0.f;   // sells promises for trade goods (Swindler)
    float lend = 0.f;          // lends trade goods (Merchant, War Bankers)
    float sweetener = 0.f;     // offers humans a little extra (Slithering Tongue)
    float sellCards = 0.f;     // sells action cards (Hacan Scarface)
    float spoiler = 0.f;       // votes against the points leader
    int voteAgainst = -1;      // votes against this player (Kingslayer, a betrayal)
    // its active betrayal objective (hidden; the AI knows its own)
    int betrayTarget = -1, betrayKind = -1, betrayArg = -1;
    Style() {
        for (float& u : unit) u = 1.f;
    }
};
const Style& aiStyle(const Game& g, int p);
int aiPersona(int p);  // test only (TI4_PERSONA): 1 breaker, 2 camper, 3 spammer, 4 turtle, 5 rusher, 0 none

// ---- the groundwork (ai_ground.cpp, 0.25.0) ------------------------------------
// What every good player does under any style: slice, equidistants, custodians,
// plastic, scoring tempo. Values added to the plans the other layers compare.
struct Ground {
    float scale = 0;         // how much of the groundwork this bot follows (difficulty; 0 = none)
    float open = 0;          // the opening's weight this round (1 in rounds 1-2, gone by round 5)
    int sliceOpen = 0;       // planets in our slice nobody holds yet
    int equiOpen = 0;        // ... in equidistant systems we are tied for
    bool custodian = false;  // we can land on Mecatol Rex this turn: keep 6 influence for the custodians
};
const Ground& aiGround(const Game& g, int p);
float aiGroundScale(const Game& g, int p);
float aiGroundOpening(const Game& g);
int aiSliceOf(const Game& g, int sys);              // the slice's player, -2 equidistant, -1 none (homes, Mecatol, empty)
bool aiSliceTied(const Game& g, int sys, int p);    // an equidistant system p is one of the nearest players to
int aiHomeMoves(const Game& g, int p, int sys);     // moves from p's home (99: no route)
float aiGroundPlanet(const Game& g, int p, int pl); // extra value of an unclaimed planet (planetValue)
bool aiCanLandOnMecatol(const Game& g, int p);      // ground forces that can land on Mecatol Rex this turn
// The seats of power (0.44.2, ai_seat.cpp): Mecatol Rex once the custodians are gone, and Styx.
bool aiSeatPlanet(const Game& g, int pl);         // a seat
bool aiSeatOpen(const Game& g, int pl);           // a seat its holder left open: no ships there, at most 1 ground force
bool aiSeatHeldIn(const Game& g, int p, int sys); // p holds a seat in sys
bool aiMecatolStaged(const Game& g, int p);       // a loaded ship with capacity in or next to Mecatol Rex
int aiStyxPlanet(const Game& g);                  // Styx on the board (Thunder's Edge), else -1
float aiDominusGain(const Game& g, int p, int sys);  // (0.55.0) what purging the Dominus Orb adds to this movement
// ---- The endgame (ai_endgame.cpp, 0.55.0): stop the winner, the last round all-in ---------------------------
struct Endgame {
    int threat = -1;            // another player who can reach the goal at the coming scoring (-1 none)
    float threatReach = 0;      // ... the points they can expect there
    float myReach = 0;
    bool lastRound = false;     // somebody will very likely reach the goal at this round's scoring
    std::vector<float> planet;  // per planet: worth of taking it from the threat (it breaks their win; empty: none)
    std::vector<float> system;  // per system: worth of destroying the threat's ships there
};
const Endgame& aiEndgame(const Game& g, int p);
float aiReach(const Game& g, int q, int viewer);  // q's expected points at the coming scoring, as viewer sees it
// ---- Plan Ahead (ai_ops.cpp, 0.53.0): the operation a bot works toward over turns and rounds ----------------
// A target out of one turn's reach - Styx, Mecatol Rex held by a fleet, a home system (Kingslay, a secret,
// Supremacy) - its fleet gathered in striking distance first, then the strike. Worked out from the board each time
// (bots keep no memory); what has gathered already makes the same choice win again. TI4_AITUNE ops=0 turns it off.
enum { kOpNone = 0, kOpStyx, kOpMecatol, kOpHome, kOpFleet, kOpCustodians };  // (0.54.0: Fleet, Custodians)
struct AiOp {
    int kind = kOpNone;
    int target = -1, foe = -1;  // the system; who defends it (a player, kNeutral, -1 nobody)
    float value = 0;            // what taking it is worth
    std::vector<int8_t> dist;   // per system: moves to the target for our ships (-1 far)
    Force theirs;               // the ships defending it
    int gfNeed = 0;             // ground forces the landing needs
    Force tier[3];              // our ships (and what they carry) by how soon they can strike: this turn, 1 / 2 moves more
    std::vector<int8_t> tierAt; // per system: the soonest tier of our ships there (-1 none)
    float readiness = 0;        // how far along the gathering is (0..~1.2: tier 0 counts fully, the others less)
    float oddsReady = 0, oddsPosted = 0, oddsLater = 0;  // with the ships that can go now, all in reach, all three tiers
    bool strike = false;        // enough is ready: go in now
    float watch = 0;            // Mecatol Rex: the best odds a third player would have against what our strike leaves
};
const AiOp& aiOp(const Game& g, int p);
float aiOpOdds(const Game& g, int p, const AiOp& op, const Force& f);  // f against the target's defenders
float aiMecatolHill(const Game& g, int p, int holder);  // (0.54.0) Mecatol Rex's worth to p, taken from its holder (Imperial)
float aiOpWatchers(const Game& g, int p, const AiOp& op, const Force& strike);  // a third player's odds after our strike
float aiOpProgress(const Game& g, int p, const AiOp& op, const Force& f);  // 0..1 toward a sure strike (about linear in ships)
float aiOpReadinessOf(const Game& g, int p, const AiOp& op, const Force (&tier)[3]);
// the readiness after this move into sys (ships leave their tiers for the one they reach from sys)
float aiOpReadinessAfter(const Game& g, int p, const AiOp& op, int sys, const std::vector<int>& move);
// ---- The Saarball (ai_ops.cpp; the Saar review, owner 2026-11-13: "we will need to figure out how to implement a
// Saarball strategy") - switch saarball3 (test, default off). The floating factory with the biggest escort (the ball)
// rolls toward a front - the system where a factory brings the most within reach (free and other players' planets
// around it, Mecatol Rex next door, an asteroid field once Chaos Mapping keeps others out), not under a fleet that
// would beat it - one step a turn with its whole escort, building where it arrives; parked there it builds every turn.
struct SaarBall {
    int ball = -1;   // the system of the ball's factory (-1: none)
    int front = -1;  // where it rolls to (== ball: parked)
    int next = -1;   // this turn's step toward the front (-1: none - parked, or no safe step)
    float score = 0; // the front's worth (planner units, 0..~8)
    int steps = 0;   // factory moves from the ball to the front
    std::vector<int8_t> toFront;  // per system: moves to the front (-1 far)
};
const SaarBall& aiSaarBall(const Game& g, int p);
float aiSaarFront(const Game& g, int p, int sys);  // what a factory parked in sys brings within reach
// Betrayal timing: how hard p pushes its betrayal objective now (1 = as before). Below
// Competitive: at once, by personality; Competitive: only when the economy allows.
float aiBetrayUrge(const Game& g, int p);
bool aiBetrayalMomentOk(const Game& g, int p);

// ---- the Strategy Core (ai_core.cpp, 0.18.0) -----------------------------------
// How the faction is best played (the owner's strategy guide): card priorities,
// technology paths, units, trading; it suggests, the personality mostly follows.
int aiCustodiansInf(const Game& g, int p);  // influence the custodians cost this bot (6; the Winnu with bloodties: 0)
bool aiCoreOn(const Game& g, int p);
std::vector<int> aiCoreTechPlan(const Game& g, int p);  // technologies in the order the plan wants them
float aiCoreTechBonus(const Game& g, int p, int tech);  // the plan's next steps + answers to the neighbours
int aiResolveTech(const Game& g, int p, const char* key);  // a generic key -> this faction's own (dn2 -> sdn2), or -1
void aiCoreApply(Style& st, const Game& g, int p);      // folds the core into the personality's style
bool aiCoreWarSuns(const Game& g, int p);               // build war suns when it can
float aiCoreSliceBias(const Game& g, int p, int slice);  // 0.43.0: the expedition slices the guide pays with
bool aiCoreExpeditionLater(const Game& g, int p);         // the guide starts the expedition in a later round
float aiCorePlanetBonus(const Game& g, int p, int pl);    // stations, the Fracture, factions to hit early
bool aiUmbatWorth(const Game& g, int p);                  // (Muaat review) Umbat's production would buy something
int aiUmbatSystem(const Game& g, int p);                  // ... and the system where it buys the most (-1: none)

}  // namespace ti
