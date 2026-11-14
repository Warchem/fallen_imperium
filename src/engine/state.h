// Complete state of a game in progress. Plain data: the rules code changes
// it, the UI and AI only read it.
#pragma once
#include <array>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/data.h"
#include "galaxy/hex.h"

namespace ti {

// Modules (0.31.0): Setup::modules / Game::modules bits
constexpr int kModCodex = 1, kModUncharted = 2, kModThundersEdge = 4, kModDiscordant = 8;

constexpr int kMaxPlayers = 8;
constexpr int kCommandTokens = 16;  // per player
// Thunder's Edge (0.34.0): neutral units - non-player forces (the Fracture, pirates, mercenaries). They sit in
// their own slot of every place's unit arrays; no Player stands behind it (playerName says "Neutral").
constexpr int kNeutral = kMaxPlayers;
constexpr int kSides = kMaxPlayers + 1;

// Units of one player in one place (a system's space area or a planet).
struct Force {
    uint8_t n[kUnitTypes] = {};
    uint8_t dmg[kUnitTypes] = {};  // how many of n are damaged (sustained damage)
    uint8_t galv[kUnitTypes] = {}; // Thunder's Edge (Last Bastion): how many of n are galvanized (1 more combat die)

    int count(UnitType t) const { return n[int(t)]; }
    int damaged(UnitType t) const { return dmg[int(t)]; }
    bool empty() const {
        for (int i = 0; i < kUnitTypes; ++i)
            if (n[i]) return false;
        return true;
    }
    int ships() const {
        int s = n[int(UnitType::Flagship2)];  // (0.52.0: the second flagship's type comes after the structures)
        for (int i = 0; i <= int(UnitType::WarSun); ++i) s += n[i];
        return s;
    }
    int flagships() const { return n[int(UnitType::Flagship)] + n[int(UnitType::Flagship2)]; }  // (0.52.0: both types)
    int nonFighterShips() const { return ships() - n[int(UnitType::Fighter)]; }
    int groundForces() const { return n[int(UnitType::Infantry)] + n[int(UnitType::Mech)]; }
    int total() const {  // every unit (0.33.0)
        int k = 0;
        for (int t = 0; t < kUnitTypes; ++t) k += n[t];
        return k;
    }
    int structures() const { return n[int(UnitType::PDS)] + n[int(UnitType::SpaceDock)]; }
    void add(UnitType t, int k) { n[int(t)] = uint8_t(n[int(t)] + k); }
    // Remove k units, taking undamaged ones first unless damagedFirst.
    void remove(UnitType t, int k, bool damagedFirst = false) {
        int i = int(t);
        if (k > n[i]) k = n[i];
        int undamaged = n[i] - dmg[i];
        int fromDamaged = damagedFirst ? (k < dmg[i] ? k : dmg[i]) : (k > undamaged ? k - undamaged : 0);
        dmg[i] = uint8_t(dmg[i] - fromDamaged);
        n[i] = uint8_t(n[i] - k);
        if (galv[i] > n[i]) galv[i] = n[i];  // (galvanized units go last)
    }
};

struct PlanetState {
    int16_t def = -1;     // kPlanets index
    int16_t system = -1;  // Game::systems index
    int8_t owner = -1;    // controlling player
    bool exhausted = false;
    bool abilityExhausted = false;     // legendary planet ability card
    bool destroyed = false;            // Stellar Converter: no longer a planet
    bool sleeper = false;              // Titans of Ul sleeper token (Terragenesis / Awaken)
    bool terraform = false;            // Terraform (Titans note): +1/+1 and all three traits
    bool ulHero = false;               // Ul the Progenitor (Titans hero) attached: +3/+3, Space Cannon 5 (x3)
    std::vector<int16_t> attachments;  // kExploreCards indices (attach cards)
    std::vector<int16_t> salvage;      // Dok 'N Pic's Salvage Yard (Thunder's Edge): action cards on its card
    bool relicTaken = false;           // Thunder's Edge: its relic icon has given its relic
    uint16_t coexist = 0;              // Thunder's Edge: players whose units here coexist (no combat, no control)
    int16_t spoils = 0;                // Total War (galactic event): commodities on the planet
    int8_t monument = -1;              // Monuments to the Ages: commodities under the monument here (-1 = none)
    int8_t minor = -1;                 // Minor Factions: the faction (not in play) whose home this planet was
    bool claimed = false;              // someone has controlled it (Dangerous Wilds: "from the deck")
    std::array<Force, kSides> units;  // [kNeutral] = neutral units
};

struct SystemState {
    int16_t def = -1;      // kSystems index
    Hex pos;
    int8_t rotation = 0;   // hyperlane rotation in 60 degree clockwise steps
    int8_t home = -1;      // home system of this player
    bool offBoard = false; // e.g. the Creuss home system
    bool edge = false;     // on the edge of the board (objectives)
    bool custodians = false;
    bool frontier = false;      // frontier token (PoK exploration)
    bool nexusActive = false;   // wormhole nexus: flipped to its active side
    uint8_t tokenWormholes = 0; // wormhole tokens placed here (gamma tokens, ion storm)
    uint16_t tokens = 0;   // command tokens, one bit per player
    bool fracture = false;      // Thunder's Edge: one of the Fracture's systems (adjacent only among themselves / via ingress)
    bool egress = false;        // ... with an egress (adjacent to every ingress system)
    bool ingress = false;       // an ingress token is here (adjacent to every egress system)
    int8_t breach = 0;          // Thunder's Edge (Crimson Rebellion): 1 an inactive breach, 2 an active one (active ones adjoin)
    std::array<Force, kSides> space;  // [kNeutral] = neutral units
    std::vector<int16_t> planets;  // Game::planets indices
    std::vector<int16_t> adj;      // adjacent systems (hex, hyperlane and wormhole links)

    bool hasToken(int p) const { return (tokens >> p) & 1; }
    const SystemDef& d() const { return kSystems[def]; }
    bool isHyperlane() const { return d().back == Back::Hyperlane; }
};

enum class Phase : uint8_t { Setup, Strategy, Action, Status, Agenda, GameOver };

constexpr int kNoVote = -9999;  // Game::votedFor: hasn't voted / abstained

// Leaders (Prophecy of Kings).
enum LeaderStatus : uint8_t { kLocked = 0, kReady = 1, kExhausted = 2, kPurged = 3 };
struct LeaderState {
    int16_t leader = -1;  // kLeaders index
    uint8_t status = kLocked;
};

// A promissory note: whose it is (the player who can give it away) and which one.
// (type 8, the Veldyr's fourth faction note, is stored as 64 + owner: the other ids stay as they were)
inline int16_t makeNote(int owner, int type) { return type >= 8 ? int16_t(64 + owner) : int16_t(owner * 8 + type); }
// Note types 0-3 are the generic notes (data.h), then:
constexpr int kNoteAlliance = 4;   // Alliance: use the owner's commander
constexpr int kNoteFaction = 5;    // the owner's faction note
constexpr int kNoteFaction2 = 6;   // a second faction note (Empyrean)
constexpr int kNoteFaction3 = 7;   // a third and a fourth one (the Veldyr's Branch Offices, Discordant Stars)
constexpr int kNoteFaction4 = 8;
// The Firmament's plot cards (Player::plotsIn bits)
enum PlotCard { kPlotEnervate = 0, kPlotSiphon = 1, kPlotSeethe = 2, kPlotAssail = 3, kPlotExtract = 4, kPlotCount = 5 };
constexpr int kCapturedToken = 15; // Player::captured owner for fighter / infantry tokens (LRR 17.3)
inline int noteOwner(int note) { return note >= 64 ? note - 64 : note / 8; }
inline int noteType(int note) { return note >= 64 ? 8 : note % 8; }

// A law in play (an agenda card that stays on the table).
struct Law {
    int16_t agenda = -1;
    int8_t owner = -1;    // player who holds it (elected), -1 = common play area
    int16_t target = -1;  // elected planet / objective, if any
};

// Agenda effects that wait for the start of the next strategy phase.
struct PendingEffect {
    int16_t agenda = -1;
    bool against = false;
    uint8_t players = 0;  // bit per player it applies to (voters), 0xff = everyone
};

// A computer player's personality (0.12.0, see personality.h). arch < 0 = a human.
struct Personality {
    int8_t arch = -1;     // core archetype (Arch)
    int8_t now = -1;      // archetype in play this round (Chaotic: its pick)
    int8_t evo = -1;      // evolution path (Evo)
    int8_t quirk = -1;    // generic trait (Quirk)
    int8_t fquirk = -1;   // faction trait 0 / 1
    std::array<float, 8> bf{};   // B-Factor toward each player (0-100, hidden)
    int8_t armed = -1;           // a betrayal armed against this player (the d100 came up)
    int8_t target = -1;          // active betrayal objective: against whom
    int8_t kind = -1;            //   which one (Betrayal)
    int16_t arg = -1;            //   its system / note / amount
    int8_t since = 0;            //   round it was given
    int16_t betrayals = 0;       // betrayal objectives completed (stats)
    // what each player has done to this bot (evolution paths, attitude)
    std::array<int8_t, 8> attacks{}, broke{}, kept{}, deals{}, refused{}, gifts{};
    std::array<int8_t, 8> lostBig{};  // round a player last lost 3+ ships in one fight (Scavenger)
    uint16_t nearTurn = 0;       // bits: players already counted as moving near this turn
    uint8_t flags = 0;           // 1 = Grey Fanatics used (Yin)
};

struct Player {
    int8_t faction = -1;
    int8_t tribunii = -1;  // the Council Keleres: the faction whose home system it took (The Tribunii)
    int8_t color = 0;
    bool human = false;
    int8_t aiLevel = -1;  // this computer player's difficulty (0.19.0 faction menu); -1 = Game::aiLevel
    std::string name;

    int8_t tactic = 3, fleet = 3, strategy = 2;
    int8_t tradeGoods = 0, commodities = 0;
    int16_t commTg = 0;  // statistics: trade goods received as other players' commodities
    std::vector<int16_t> techs;
    std::vector<int16_t> exhaustedTechs;
    std::vector<int8_t> cards;  // strategy cards held this round (0..7)
    uint8_t cardsUsed = 0;      // bit per strategy card
    bool passed = false;
    int8_t vp = 0;
    int16_t vpLost = 0;  // (simulator invariant only) victory points lost - Mutiny, Support for the Throne going back, ...
    std::vector<int16_t> scored;   // objectives scored (public and secret)
    std::vector<int16_t> secrets;  // secret objectives in hand, not yet scored
    std::array<int16_t, kUnitTypes> unit{};  // current definition per unit type
    int16_t homeSystem = -1;
    bool eliminated = false;
    std::vector<int16_t> actionCards;  // hand (kActionCards indices), hidden from the others
    std::vector<int16_t> notes;        // promissory notes in hand (makeNote)
    std::vector<int16_t> notesInPlay;  // other players' notes face up in this player's play area
    bool keepsCards = false;           // Political Stability: keeps strategy cards, skips the next pick
    std::vector<int16_t> fragments;    // relic fragments (kExploreCards indices) in the play area
    std::vector<int16_t> relics;       // kRelics indices
    std::vector<int16_t> exhaustedRelics;
    int8_t enigmatic = 0;              // Enigmatic Device cards in the play area
    int8_t duress = -1;                // Thunder's Edge: Extreme Duress played on this player's turn by this player
    bool breakthrough = false;         // Thunder's Edge: this faction's breakthrough is gained
    bool btExhausted = false;          // ... and exhausted (the ones with an exhaust ability)
    bool btPurged = false;             // 0.44.4: purged (Insurrectionist Networking) - never gained again
    bool noPublics = false;            // Thunder's Edge: The Silver Flame burnt the home system - no public objectives
    // Thunder's Edge breakthroughs (0.34.0, part 3)
    std::vector<int8_t> yinAlliances;  // Yin Ascendant: factions (kFactions index) whose Alliance ability the Yin have
    std::vector<int8_t> nekroZ;        // Valefar Assimilator Z: factions whose flagship abilities the Nekro flagship has
    bool eidolonMax = false;           // Absolute Synergy (Naaz-Rokha): the mech card is flipped to Eidolon Maximum
    bool custodia = false;             // I.I.H.Q. Modernization (Keleres): holds the Custodia Vigilia planet card
    int8_t oceans = 0, oceansExhausted = 0;  // Oceanbound (Deepwrought): ocean cards (1 resource / 1 influence each)
    int16_t sharedTech = -1;           // Share Knowledge (Deepwrought note): the technology borrowed until the status phase
    Force phantom;                     // Homesick Phantom (Crimson hero): ships waiting on the card
    bool breachedOthers = false;       // put a breach in a system with another player's unit (Ahk Siever unlocks)
    bool custodiaExhausted = false;
    // The Firmament / The Obsidian (0.35.0): plot cards (kPlotEnervate..) in the play area, the players whose control
    // tokens are on each (their puppets once the Obsidian reveal them), secrets scored with Plots Within Plots and the
    // trade goods on The Sowing / The Reaping
    uint8_t plotsIn = 0;
    std::array<uint8_t, 5> plotTok{};
    std::vector<int16_t> plotted;
    int8_t sowing = 0;
    // galactic events (0.36.0)
    bool atomics = false;              // Stellar Atomics: still holds its atomics token
    std::array<uint8_t, kUnitTypes> purgedUnits{};  // Age of Fighters: destroyed non-fighter ships are out of the game
    int8_t monumentVP = 0;             // Monuments to the Ages: points it holds from monuments now
    std::vector<int8_t> alliances;     // Mercenaries for Hire / Minor Factions: factions (not in play) whose commander it uses
    // Discordant Stars (0.37.0)
    int8_t dsTokens = 0;               // ATS Armaments (Lanefir): commodity tokens on the card
    bool dsOnce = false;               // once-per-action abilities (Scour, War Stories, Scavenger Exos, ...): reset each turn
    bool dsExosOnce = false;           // 0.44.8 (fixes 12): Scavenger Exos's own once-per-action mark (it blocked Scour)
    bool dsUnlock = false;             // a commander's condition met at some moment (Queen Lorena: destroyed a player's last units)
    int8_t dsHeroSys = -1;             // Spear of Vaylar (Vaylerian hero): +1 move / +1 combat until the end of this turn
    int16_t dsPubSecret = -1;          // Contract Network (Tnelis hero): another player's secret, scored like a public
    std::vector<int16_t> dsTempTechs;  // Blessing of the Ancients (Rhodun hero): all but 1 purged next strategy phase
    bool dsPilgrim = false;            // Pilgrimage Beacons used this turn
    uint8_t dsFrags = 0;               // Ancient Blueprints (Bentor): Fragment tokens on the sheet (bit per ExploreType)
    int16_t dsSheet = -1;              // Oracle AI (Augurs): the public objective on their faction sheet
    uint8_t dsPolicy = 0;              // Policies (Olradin): bit 0 People, 1 Environment, 2 Economy - set = the "-" side
    uint8_t dsPolOnce = 0;             // Olradin policies used this action (same bits)
    // Discordant Stars batch 5 (0.41.0)
    int8_t dsOmen[4] = {};             // Prescient Memories (Myko-Mentori): the Omen dice near the sheet (0 = used)
    int8_t dsMykoMechs = 0;            // Amandia Pholdis (Myko-Mentori mech): mechs waiting on the card
    int16_t dsGraceRound = -1;         // Grace (Edyn): the round it was last used (once per action phase)
    int16_t dsUnityRound = -1;         // Unity Algorithm (Edyn): once per agenda phase
    int16_t dsInsightTurn = -1;        // Gift of Insight (Myko's note): the turn of its last reroll
    bool dsHired = false;              // Hired Guns (Nokar): another player has taken their ships (Jack Hallard unlocks)
    // Discordant Stars batch 6 (0.42.0)
    bool dsGhotiExhausted = false;     // Spawning Grounds (Ghoti): their planet card (3/3, not on the board) is exhausted
                                       // (also The Grove: Khrask breakthrough, 2/0)
    // Faction abilities (Stage 5)
    std::vector<LeaderState> leaders;  // agents first, then commander, then hero
    int8_t infantryOnCard = 0;         // Infantry II / Spec Ops II waiting to return home
    int16_t heroRound = -1;            // hero "placed near the board" until the end of this round (Darktalon)
    bool usedFactionAbility = false;   // Yin commander unlock (later batch)
    bool producedWarSun = false;       // Magmus (Muaat commander) unlock
    bool mecatolCombat = false;        // Rickar Rickani (Winnu commander) unlock
    int16_t assimX = -1, assimY = -1;  // Valefar Assimilator X / Y: the technology the token is on
    // batch 4
    uint8_t edict = 0;                 // Mahact: players whose command token is in this player's fleet pool (Edict)
    int8_t agentTG = 0;                // Artuno the Betrayer (Nomad agent): trade goods waiting on the card
    std::vector<int16_t> captured;     // Vuil'raith: captured units, owner * 16 + unit type (owner 15 = fighter / infantry token)
    // Negotiations
    int16_t turns = 0;                 // turns this player has started (LeaveSystem deadlines)
    int8_t promisesBroken = 0;         // reputation: promises this player broke
    int16_t raids = 0;                 // 0.54.0 (habits): combats this player started in another player's home system
    int16_t mecHeld = 0;               // ... turns this player ended holding Mecatol Rex
    Personality pers;                  // computer players only (0.12.0)

    bool hasRelic(int r) const {
        for (int16_t x : relics)
            if (x == r) return true;
        return false;
    }
    bool relicExhausted(int r) const {
        for (int16_t x : exhaustedRelics)
            if (x == r) return true;
        return false;
    }

    bool hasTech(int t) const {
        for (int16_t x : techs)
            if (x == t) return true;
        return false;
    }
    bool techExhausted(int t) const {
        for (int16_t x : exhaustedTechs)
            if (x == t) return true;
        return false;
    }
    bool holdsCard(int c) const {
        for (int8_t x : cards)
            if (x == c) return true;
        return false;
    }
    const UnitDef& u(UnitType t) const { return kUnits[unit[int(t)]]; }
};

// Things that happened this turn / round that secret objectives care about.
struct Trackers {
    int8_t lastToPass = -1;
};

// Effects of action cards that last for the current tactical action.
struct TacticalEffects {
    bool flankSpeed = false;   // +1 move
    bool navSuite = false;     // ignore anomalies while moving
    bool solarFlare = false;   // no space cannon against the active player's ships during movement
    int8_t myruThrough = -1;   // 0.43.0 (T30) Myru Vos: this player's ships that carry nothing move through others' ships
    bool lostStarChart = false;  // alpha and beta wormholes adjacent
    int16_t silenceSystem = -1;  // ships from here may move through enemy ships
    // Discordant Stars (0.37.0): Sanctification Field (a technology with no text), Omniscience Field (+/-1 to one
    // player's rolls), Combinatorial Bypass (others' units lose space cannon / planetary shield in this invasion)
    int16_t blankTech = -1;
    int8_t blankOwner = -1;
    int8_t omniPlayer = -1, omniMod = 0;
    int8_t bypass = -1;
    bool noBlockade = false;  // Primespace Actualization: production here ignores the blockade
    int8_t dsMove = 0;        // Geosympathic Impeller (Olradin): +1 move to every ship of the active player
    int8_t dsBoost = 0;       // Midas Turbine / Vaden Handshake: this many more ships may move 1 further
    int8_t dsBoostOwner = -1;
    int8_t dsRaidType = -1;   // Raid Leaders (Ghemina's note): this ship type of the holder gains capacity 2 and Bombardment 5 (x2)
    int8_t dsPyndilType = -1; // Pyndil Gonsuul (0.43.4, D7, fixes >= 5): the chosen ship (of this type) has +2 capacity
    int8_t dsRaidOwner = -1;
    int8_t dsRiftType = -1;   // Rift Engines (Axis): ships of this type in this system move 2 further
    int8_t dsRiftOwner = -1;
    int16_t dsRiftSys = -1;
    int8_t dsInhibit = -1;    // Gravitic Inhibitor (Li-Zho trap): this player moves at most fleet limit - 1 ships in
    int8_t dsWraithType = -1; // Wraith Engine (Li-Zho): ships of this type in this system count as next to the active system
    int8_t dsWraithOwner = -1;
    int16_t dsWraithSys = -1;
    int16_t dsShieldPlanet = -1;  // Orbital Defense Grid (Mirveda): this planet's units have Planetary Shield this action
    int8_t dsCeie = -1;           // Ceie Doleegueaunm (Ghoti commander): planetless systems are next to each other
    int8_t fillipo = -1;      // Fillipo Rois (Tnelis commander): this player has 1 more ship that may move 1 further
    bool noMoveIn = false;     // Ceasefire / Minister of Peace: nobody may move in
    bool dominusOrb = false;   // units may move out of systems with the active player's tokens
    bool conduit = false;      // Spatial Conduit Cylinder: the active system is next to all of the active player's systems
    bool anyPlanetGF = false;  // Letani Miasmiala: produced ground forces may go on any of your planets in the system
    // batch 3
    bool taivra = false;        // Emissary Taivra: the active system is next to every system with a wormhole
    bool mendosa = false;       // Captain Mendosa: 1 ship moves as far as the fastest ship on the board
    bool aetherstream = false;  // Aetherstream: +1 move
    int8_t aetherFrom = -1;     // Aetherpassage: the active player's ships may pass this (Empyrean) player's ships
    uint8_t tetherAllow = 0;    // (fixes 26) Void Tether: players the Empyrean let treat tethered borders as adjacent
    int8_t anyPlanetGFFor = -1; // (fixes 26) anyPlanetGF for this player too, not only the active one (Umbat for another)
    int16_t swapPlanet = -1;    // Hegemonic Trade Policy: this planet's resources and influence are swapped
    // Sarween Tools (0.44.5, fixes 9): set while units are produced by an ability other than PRODUCTION (Living Rules
    // FAQ: Sarween Tools is only used with the Production ability)
    bool noSarween = false;
    std::vector<int16_t> gained;  // planets the active player gained this tactical action (Reclamation)
    // batch 4
    bool crucible = false;        // Crucible (Vuil'raith note): the active player's ships don't roll for gravity rifts
    // Uncharted Space (0.33.0)
    uint8_t echoShield = 0;  // Echo Shielding: these players' units in the active system have Planetary Shield
    uint8_t illusory = 0;    // Illusory Duplication: these players' non-fighter ships have Sustain Damage this round
    int16_t starA = -1, starB = -1;  // Abyssal Starpaths: these two systems are adjacent
    std::vector<int16_t> vogul;      // Eye of Vogul: ships (system * 16 + unit type, one each) adjacent to the active system
    bool tempesta = false;           // Ionian Fuel Refinery (Thunder's Edge): 1 more ship may move 1 further
    bool imperator = false;          // Imperator (Winnu breakthrough): the same, after activating a legendary planet's system
    std::vector<int16_t> wing;       // Wing Transfer (Argent breakthrough): systems given a token - ships may move among them
};

// ---- Negotiations (Stage 6) --------------------------------------------------
// Promises are what players agree to do later. The game never forces them
// (LRR 28.4: deals about the future are not binding) but it watches each one
// and records whether it was kept or broken.
enum class PromiseKind : uint8_t {
    None,
    AvoidSystem,   // won't activate this system (target) until the end of round untilRound
    LeaveSystem,   // takes its ships out of this system (target) by the end of its next turn
    AttackSystem,  // attacks in this system (target) by the end of round untilRound
    LetPass,       // won't attack the other player's units in this system (target) until untilRound
    VoteFor,       // votes for outcome 'target' on agenda 'agenda'
    Repay,         // gives back 'amount' trade goods by the end of round untilRound (a loan)
    UseAgent,      // uses its agent for the other player at the next chance, until untilRound
    NoAttack,      // doesn't attack the other player anywhere until untilRound
    Contract,      // 0.43.0 (Roh'Dhna, Contractual Obligations): picks the other player at the next agenda phase
    Count
};
enum PromiseState : uint8_t { kPromiseActive = 0, kPromiseKept = 1, kPromiseBroken = 2, kPromiseExpired = 3 };
struct Promise {
    PromiseKind kind = PromiseKind::None;
    int8_t by = -1, to = -1;   // who promised, to whom
    int16_t target = -1;       // system / outcome
    int8_t amount = 0;         // Repay: trade goods
    int8_t rounds = 1;         // how many rounds it lasts (in a deal; untilRound is set when the deal is made)
    int8_t untilRound = 0;
    int16_t agenda = -1;       // VoteFor: the agenda
    int16_t deadlineTurn = -1; // LeaveSystem: by the end of this turn of 'by' (Player::turns)
    bool contract = false;     // part of a deal where components changed hands
    uint8_t state = kPromiseActive;
};
// What one side of a deal hands over now (a transaction, LRR 94).
struct DealSide {
    int tg = 0, comm = 0;
    int note = -1;                  // at most 1 promissory note (LRR 94.3)
    std::vector<int> fragments;     // relic fragments (kExploreCards indices)
    std::vector<int> actionCards;   // only in trades with the Hacan (Arbiters)
    int planet = -1;                // a Hacan planet with the Pride of Kenara mech
    std::vector<int> captured;      // Vuil'raith: captured units given back (owner * 16 + type)
    std::vector<int> relics;        // Thunder's Edge: The Silver Flame, or any relic with Black Market Dealings
    std::vector<int> secrets;       // ... unscored secret objectives (Black Market Dealings)
    std::vector<int> techs;         // Age of Commerce (galactic event): technologies shared (the giver keeps them)
    int debt = 0;                   // Discordant Stars (Binding Debts): the giver's control tokens put on the Vaden's sheet
    std::vector<int> orders;        // Discordant Stars (Arms Dealers): Axis Order cards from the giver's play area (0-7)
    int hire = 0;                   // Discordant Stars (Hired Guns, 0.43.0): up to this many of the Nokar's ships join the
                                    // receiver in the active system (only offered right after a system is activated)
    bool empty() const {
        return !tg && !comm && note < 0 && fragments.empty() && actionCards.empty() && planet < 0 && captured.empty() && relics.empty() &&
               secrets.empty() && techs.empty() && !debt && orders.empty() && !hire;
    }
};
struct Deal {
    int from = -1, to = -1;         // proposer and receiver
    DealSide give, get;             // 'from' gives 'give'; 'to' gives 'get'
    std::vector<Promise> promises;  // by = from or to
    int threat = -1;                // a threat: the system 'from' says it attacks if refused (-1 = none)
    bool empty() const { return give.empty() && get.empty() && promises.empty(); }
    bool contract() const { return !give.empty() || !get.empty(); }  // components change hands
};
// One line of the deal history (Negotiations screen).
struct DealRecord {
    int8_t round = 0, from = -1, to = -1;
    uint8_t result = 0;  // 1 accepted, 2 declined, 3 couldn't be made
    bool contract = false;
    std::string text;
};

// Galaxy setup draft (0.15.0, LRR setup step 6): the open spaces of the map
// and the tiles each player still holds. Empty once the galaxy is built.
struct DraftCell {
    Hex pos;
    int8_t ring = 0;
    int16_t system = -1;  // Game::systems index once a tile is placed here
};
struct GalaxyDraft {
    bool active = false;
    std::vector<DraftCell> cells;
    std::vector<std::vector<int16_t>> hands;  // per player: kSystems indices still to place
    int8_t placing = -1;                      // player placing a tile now
    int16_t placed = 0, total = 0;            // tiles placed so far / in all
};

// 0.54.0 (owner: "adapting to the habits of all players ... the profile being fixed at the start of each game"): what
// the people playing at this device did in their earlier games (game/habits.cpp keeps it in the settings), copied into
// a new game's Setup and saved with it, so a loaded game replays with the same profile. games = 0: nothing known.
struct HabitProfile {
    int games = 0;
    float attacks = 0;  // their attacks on computer players per round
    float raids = 0;    // their combats in other players' home systems per game
    float mecatol = 0;  // turns they ended holding Mecatol Rex per game
    float broken = 0;   // promises broken per game
    float kept = 0;     // promises kept per game
    float deals = 0;    // deals made with computer players per game
};

struct Game {
    int numPlayers = 0;
    std::vector<Player> players;
    std::vector<SystemState> systems;
    std::vector<PlanetState> planets;

    Phase phase = Phase::Setup;
    int round = 0;
    int8_t speaker = 0;
    int vpGoal = 10;
    int8_t aiLevel = 2;  // computer players' difficulty: 0 Easy, 1 Normal, 2 Hard, 3 Competitive (Setup::aiLevel)
    int8_t tradeRules = 0;  // Setup::trade: bit 0 X-1 trades, bit 1 round-1 free refills, bit 2 humans choose in round 1,
                            // bit 3 the community X-1 (0.17.0: wash X commodities for X-1, Trade Agreement buy, debts)
    uint32_t seed = 0;   // Setup::seed (personality randomness: prand)
    bool archRevealed = false;  // bots' archetypes shown (end of round 3)
    uint8_t extras = 0;         // Setup::extras: bit 0 Kingslay (0.19.0)
    uint8_t fixes = 0;          // Setup::fixes (0.33.0): rule fixes this game was started with
    uint8_t layout = 0;         // Setup::layout (0.58.0): 1 = the printed galaxy without hyperlanes
    uint8_t modules = 0;        // Setup::modules (0.31.0): kModCodex, kModUncharted, kModThundersEdge, kModDiscordant
    uint32_t events = 0;        // Setup::events (0.36.0): galactic events in play (Thunder's Edge)
    int8_t dsLevel = 0;         // Setup::dsLevel (0.37.0): Discordant Stars batches the random pools draw from
    bool lawsBlank = false;     // Law's Order (Keleres): all laws are blank until the end of this turn
    int8_t mecHolder = -1;      // Kingslay: who holds Mecatol Rex ...
    int16_t mecTurns = 0;       // ... and for how many of their turns
    int8_t winner = -1;
    HabitProfile habits;        // Setup::habits (0.54.0): the human players' habits from earlier games
    GalaxyDraft draft;

    int8_t cardOwner[kStrategyCards];
    int8_t cardTradeGoods[kStrategyCards] = {};

    std::vector<int16_t> publicObjectives;  // revealed, in order
    std::vector<int16_t> stage1Deck, stage2Deck, secretDeck;
    int stage1Revealed = 0;

    bool custodiansRemoved = false;
    bool agendaNoticeShown = false;

    // Cards (Stage 3)
    std::vector<int16_t> actionDeck, actionDiscard;  // kActionCards indices, top = back()
    std::vector<int16_t> agendaDeck, agendaDiscard;  // kAgendas indices, top = back()
    std::vector<Law> laws;
    std::vector<PendingEffect> pending;              // at the start of the next strategy phase
    std::vector<int16_t> stage1Rest, stage2Rest;     // objectives not used in this game (Incentive Program)
    uint8_t noVote = 0;            // players who can't vote for the rest of this agenda phase
    int16_t currentAgenda = -1;    // agenda being voted on (for the screens)
    int16_t covertAgenda = -1;     // Covert Legislation: the hidden agenda (only the speaker knows)
    int8_t agendaNumber = 0;       // 0 = first agenda of this agenda phase, 1 = second
    std::vector<int> votedFor;     // per player: outcome voted for on the current agenda (kNoVote = none)
    std::vector<int> voteCount;    // per player: votes cast on the current agenda
    TacticalEffects fx;
    // Confused Sage (Uncharted Space, 0.33.0): the roll that may be changed now (for the computer players)
    std::vector<int8_t> sageDice, sageNeed;
    int8_t sageRoller = -1, sageTarget = -1;
    // Exploration (Stage 4)
    std::array<std::vector<int16_t>, 4> exploreDeck, exploreDiscard;  // per ExploreType, top = back()
    std::vector<int16_t> relicDeck;
    std::vector<int16_t> purgedTiles;  // Azdel's Key (Uncharted Space): system tiles drawn and purged
    int16_t ionStorm = -1;      // system with the ion storm token
    bool ionAlpha = true;       // its alpha side is up
    int16_t nexus = -1;         // the wormhole nexus system
    // Thunder's Edge (0.34.0): the expedition (slice owners), the Thunder's Edge planet, the Fracture
    std::array<int8_t, 6> expedition{{-1, -1, -1, -1, -1, -1}};
    int16_t teSystem = -1;      // where the Thunder's Edge planet was placed (-1: the expedition goes on)
    bool fractureIn = false;    // the Fracture is in play
    std::array<Hex, 7> fracturePos{};  // where it goes (set when the galaxy is built, Thunder's Edge games)
    Hex tePos{};                // where the Thunder's Edge token sits beside the board
    std::vector<std::pair<int16_t, int16_t>> tethers;  // Void Tether (Empyrean breakthrough): borders others can't cross
    int8_t blackMarket[2] = {-1, -1};  // Black Market Dealings: these two may trade relics, action cards, secrets now
    std::vector<int16_t> skimmer;      // Data Skimmer (Ral Nel breakthrough): discarded action cards waiting on it
    std::vector<int16_t> purgedTechs;  // Ta Zern (Deepwrought hero): technologies gone from the game
    int16_t severSystem = -1;          // Sever (Crimson note): wormholes here do nothing during movement
    // galactic events (0.36.0)
    std::vector<int8_t> mercDeck;      // Mercenaries for Hire: factions not in play (their Alliances), top = back
    int8_t orthodoxy = -1;             // Zealous Orthodoxy: the faction whose Alliance everyone has
    // Discordant Stars (0.37.0)
    int16_t fragsPurged = 0;           // relic fragments purged in this game (Master Halbert unlocks at 7)
    int8_t blight = -1;                // Brikk Strain (Kyro hero): the strategy card it is attached to
    int8_t blightOwner = -1;
    int8_t dsLendP = -1;               // Arms Brokerage (Axis breakthrough, 0.43.0): the player using a lent unit upgrade this turn
    int16_t dsLendTech = -1;           // ... which one
    int8_t dsCapBonus = -1;            // Strike Team Coordinator (Vaylerian commander): this player has +2 capacity this action
    int8_t dsGale = -1;                // Splintering Gale (Khrask flagship): the system of the space combat it fights in
    int16_t dsAutomatons = -1;         // Automatons (the Roh'Dhna's note): the planet it is attached to
    int8_t dsAutomatonsOwner = -1;
    int16_t dsConspired = -1;          // Conspirators (Rhodun): the round it was last used
    std::vector<int16_t> dsCores;      // Mantle Cracking (Gledge): planets with a Core token (2 resources, 0 influence)
    std::vector<int16_t> dsGlory;      // Glory (Kjalengard): systems with a Glory token
    int16_t dsKeyPlanet = -1;          // Encryption Key (the Bentor's note): its planet and the specialty colour
    int8_t dsKeyColor = -1;
    int16_t dsBasePlanet = -1;         // Gledge Base (the Gledge's note): +2 resources
    int8_t dsDebt[8] = {};             // Fine Print (Vaden): each player's control tokens on the Vaden faction sheet
    int16_t dsWound = -1;              // Singularity Point (Nivyn): the system with the Wound token (a nebula and a gravity rift)
    int16_t dsKomatSys = -1;           // Komat (Mortheus commander): one of this player's ships sustains damage in this combat
    int8_t dsKomatOwner = -1;
    // Discordant Stars batch 5 (0.41.0)
    int16_t dsLadySys = -1;            // The Lady & The Lord (Ghemina): the system with the Lady flagship (-1 = not on the board)
    int16_t dsHeart = -1;              // Heart of Rebellion (Free Systems hero): the planet it is attached to
    int8_t dsOrders[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // Axis Order cards (2 per type: dreadnought, carrier, cruiser,
                                       // destroyers): who has each in their play area (-1 = the Axis reinforcements)
    uint8_t dsOrderEx = 0;             // ... exhausted (bit per card)
    std::vector<int16_t> dsSoulCage;   // Soul Cage (Myko-Mentori hero): other players' heroes attached to it
    int8_t dsNavigator = -1;           // Nokar Navigator: the player who played it in the current space combat
    // Discordant Stars batch 6 (0.42.0)
    int16_t dsBranch[4] = {-1, -1, -1, -1};  // Branch Offices (Veldyr notes): the planet each one is attached to
    int16_t dsTrap[5] = {-1, -1, -1, -1, -1};  // Li-Zho Traps: the planet each one is attached to (-1 = their reinforcements)
    uint8_t dsTrapShown = 0;           // ... revealed during this tactical action (they go back at its end)
    int8_t lastPasser = -1;            // the last player to pass this round (Watchful Ojz unlocks)
    int8_t activePlayer = -1;   // player taking a turn in the action phase
    int16_t activeSystem = -1;  // system activated by the current tactical action
    Trackers track;
    // Faction abilities (Stage 5)
    int8_t naaluToken = -1;           // player holding the Naalu "0" token this round (initiative 0)
    int16_t groundCombatPlanet = -1;  // planet of the ground combat being fought (-1 = none)
    uint16_t zgrav = 0;               // (fixes 27) sides whose Eidolons fight as Z-Grav Eidolons in the current space combat
    std::array<int16_t, 3> creussTokens{{-1, -1, -1}};  // Creuss alpha / beta / gamma wormhole tokens (system, -1 = off the board)
    int16_t adjFlagship = -1;   // system of the Creuss flagship (delta wormhole) when adjacency was last computed

    // Negotiations (Stage 6)
    std::vector<Promise> promises;    // every promise made, with its state
    std::vector<DealRecord> deals;    // every offer and its result
    std::vector<std::pair<int16_t, int8_t>> dealNotes;  // (note, holder): notes that changed hands in deals (green lines)
    uint64_t tradeAnyway = 0;         // bit a*8+b: a and b may trade now whatever the usual limits (Temporal Command Suite)
    uint64_t hostile = 0;             // bit a*8+b: a fight (or a broken promise) between a and b since their last contract (red line)
    int8_t offers[kMaxPlayers][kMaxPlayers] = {};        // offers a made to b this turn / agenda
    int8_t transactions[kMaxPlayers][kMaxPlayers] = {};  // components exchanged between a and b this turn / agenda
    int8_t agentFor[kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};  // agent deals: this player's agent may be used for that player
    int8_t urgeAttack[kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};  // a refused "leave Mecatol / the Nexus" request: system to attack
    int8_t urgeRound[kMaxPlayers] = {};                  // ... until the end of this round
    int16_t turnCount = 0;            // turns taken in the game (any player)

    std::vector<std::string> log;  // full game history, newest last

    // --- convenience ---------------------------------------------------
    const SystemState& sys(int i) const { return systems[i]; }
    const PlanetState& planet(int i) const { return planets[i]; }
    int findSystemAt(Hex h) const {
        for (size_t i = 0; i < systems.size(); ++i)
            if (!systems[i].offBoard && systems[i].pos == h) return int(i);
        return -1;
    }
    int mecatol() const {
        if (mecatolCache_ >= -1 && mecatolSystems_ == systems.size()) return mecatolCache_;
        mecatolCache_ = -1;
        for (size_t i = 0; i < systems.size(); ++i)
            if (!std::strcmp(kSystems[systems[i].def].key, "18")) mecatolCache_ = int(i);
        mecatolSystems_ = systems.size();
        return mecatolCache_;
    }
    mutable int mecatolCache_ = -2;
    mutable size_t mecatolSystems_ = 0;
};

}  // namespace ti
