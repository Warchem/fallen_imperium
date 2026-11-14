// The rules engine. The game itself runs as straight-line code on its own
// stack (a coroutine); whenever a player must decide something it pauses and
// hands a Decision to whoever drives the engine (UI or AI). answer() resumes it.
//
// Saving = the setup plus the list of answers. Loading replays them, which
// reproduces the game exactly because all randomness comes from a seeded Rng.
// Undo works the same way: replay every answer except the last one.
#pragma once
#ifndef _WIN32
#include <ucontext.h>
#endif

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/decision.h"
#include "engine/rng.h"
#include "engine/state.h"

namespace ti {

struct Setup {
    int players = 6;
    int human = 0;               // seat played by the human, -1 = all AI
    std::vector<int> factions;   // per seat; -1 = random
    uint32_t seed = 1;
    int vpGoal = 10;
    int humanColor = -1;         // colour index for the human seat, -1 = default
    int aiLevel = 2;             // computer players: 0 Easy, 1 Normal, 2 Hard, 3 Competitive
    bool autoplay = false;       // the human seat is played by the computer too (watch a game)
    std::vector<int> archetypes; // per seat: computer player's archetype (Arch), -1 = random
    // New Game faction menu (0.19.0, owner). Saves without these lines load empty / 0.
    std::vector<int> colors;     // per seat: colour index, -1 = the next free one
    std::vector<int> levels;     // per seat: computer difficulty (0-3), -1 = aiLevel
    int humans = 0;              // bit per seat: more seats played by people (hot-seat), besides 'human'
    bool isHuman(int seat) const { return seat >= 0 && (seat == human || ((humans >> seat) & 1)); }
    // Trade card rules (0.14.0): bit 0 = X-1 trades offered with the primary,
    // bit 1 = house rule: round 1, everyone refills for free, bit 2 (0.15.0) =
    // ...only when a computer player plays it (a human chooses as usual), bit 3
    // (0.17.0) = the community X-1 (a neighbour washes its X commodities for X-1
    // trade goods; or the holder buys its Trade Agreement; non-neighbours owe 1).
    // Saves without the line (0.13 and older) load as 0, the old rules; 0.14-0.16
    // saves keep their 0.14 X-1 (bit 3 off).
    int trade = 15;
    // Galaxy setup (0.15.0): 1 = players draft the galaxy (deal tiles, place
    // them in turn as the rulebook describes), 0 = the balanced random map of
    // older versions. Saves without the line load as 0.
    int draft = 1;
    // Seeded galaxy (0.43.0, owner): with draft 0, a number the player types makes the map - the same seed and
    // player count give the same tiles in the same places; slices balanced harder (resources, influence,
    // legendary planets, wormholes, anomalies). 0 = the random map from the game's own seed. Saved as "mapseed".
    uint32_t mapSeed = 0;
    // Galaxy layout (0.58.0, owner): 0 = the usual layouts (hyperlanes at 3, 4, 5, 7 and 8 players), 1 = "Hyperlanes:
    // Off" - the printed board without hyperlanes for every player count (6 players: the same map). Saved as "layout".
    int layout = 0;
    // Extra rules (0.19.0): bit 0 = Kingslay (a bot is sent to take Mecatol Rex
    // from a player who has held it for more than 5 of their turns), bit 1
    // (0.21.0) = the Council Keleres can be dealt as a random faction, bit 2
    // (0.27.0) = a Competitive bot with a random personality is never a Rusher
    // or Chaotic. Saves without the line load as 0.
    int extras = 255;  // bit 4 (0.30.0): AI Development Algorithm choice, X-1 paybacks in the agenda phase;
                      // bit 5 (0.32.0): Trade Convoys is played as an ACTION instead of placed at once;
                      // bit 6 (0.32.1): Dark Pact too;
                      // bit 7 (0.54.0): "Mecatol Rex: Contested" - bots fight over its holder's fleet (Imperial's
                      // points; off = "Calm": they only take it when left open)
    // Modules (0.31.0, roadmap Stage 11): the content in play besides the base game + Prophecy of Kings -
    // kModCodex (the Codex Omega cards replace their originals), kModUncharted (Uncharted Space),
    // kModThundersEdge, kModDiscordant. Saves without the line load as 0 (the content they were made with).
    int modules = 1;
    // Galactic events (0.36.0, Thunder's Edge): bit per GalacticEvent. Saves without the line load as 0.
    int events = 0;
    // Discordant Stars (0.37.0): how many of its faction batches the random pools may draw from (saved as "dslevel"
    // with the module, so a game keeps dealing the same factions when later batches arrive)
    int dsLevel = kDsLevel;
    // Rule fixes (0.33.0) that change what happened in older games, so they only apply to games started
    // with them: 1 = Lucky Shot destroys the ship that was picked (it misread odd-numbered systems).
    // Saves without the line load as 0.
    int fixes = 27;  // 1 Lucky Shot target (0.33.0), 2 the 0.43.0 fixes, 3 the Fracture's planets at its ends (0.43.2),
                    // 4 a token gained in a tactical action no longer gives another action (0.43.3),
                    // 5 the 0.43.4 fixes (entropic scars at the start of the status phase, ...),
                    // 6 the Veldyr start with all 4 Branch Offices (0.44.1: Tax Haven was missing),
                    // 7 the 0.44.2 fixes (G'hom Sek'kus asks infantry or mech, ...),
                    // 8 the 0.44.4 breakthrough fixes (the Creuss's Particle Synthesis was never found, ...),
                    // 9 the 0.44.5 cybernetic technology fixes (Sarween Tools only with Production, ...),
                    // 10 the 0.44.6 biotic technology fixes,
                    // 11 the 0.44.7 propulsion technology fixes,
                    // 12 the 0.44.8 fixes (The Nucleus for Avernus's controller, Ember Colossus with it; warfare techs)
                    // 13 the 0.44.9 RULES-CHECK answers (W / P / G / C / B lists of 0.44.4-0.44.8)
                    // 14 the 0.45.0 bug fixes (owner's 0.44.9 list: capacity timing, X-1 commodity value, ...)
                    // 15 the 0.46.1 fixes (the fleet pool checked at all times; Pass after an extra action, ...)
                    // 16 the Ghemina's Lady is her own unit (UnitType::Flagship2, 0.52.0, owner D117)
                    // 17 the Celdauri's Celagrom is its own unit (UnitType::Flagship2, 0.52.1)
                    // 18 the 0.54.0 tester fixes (a retreat takes ground forces from the planets; pick-ups on a
                    //    detour through a gravity rift the ship doesn't roll for - the Vuil'raith's own Tears)
                    // 19 the 0.55.0 fixes (a ship the Vuil'raith capture can't also be rebuilt by Subatomic Splicer)
                    // 20 the 0.57.1 tester fixes (Peace Accords / Kantrus don't take space stations; ...)
                    // 21 the 0.58.2 tester fixes (Ahk Siever for every holder of the Crimson's commander, with a
                    //    choice; Brother Milor isn't offered for your combat opponent's units; no Sarween Tools for
                    //    Maximus / Rally to the Cause / Broadcast Teams)
                    // 22 the 0.58.5 tester fixes (ships may move in beyond the fleet pool - the extra go after the
                    //    combat and invasion there, else right after the move; Faunus can't take planets with
                    //    neutral units)
                    // 23 0.58.51: the extra ships go right after the move again, before any combat (LRR 37.3; the
                    //    move beyond the fleet pool stays allowed)
                    // 24 the faction debug pass (0.59.0, owner: the cards' exact wording) - Crimson Rebellion:
                    //    Sundered destroys other players' units however they reach the home (moved or placed);
                    //    7 breach tokens (an inactive one moves at the limit); the Revenant in the Commit Ground
                    //    Forces step (after bombardment), own planets too, back to reinforcements if not landed;
                    //    Subatomic Splicer asks how many fighters; gravity rift losses are REMOVED (LRR 41.2) for
                    //    everyone - no "destroyed" effects (Splicer, Van Hauge, Devour, Crash Landing, galvanized...);
                    //    Ahk Ravin: may decline, fighters too, cargo transported (not from a command token, LRR 95.3);
                    //    Ahk Siever only after combats between players (not against neutral units); Homesick
                    //    Phantom: All / Some / None, also after Splicer, Salvage Ops, Freelancers, Integrated Economy;
                    //    Sever / Sundered: board links (hyperlanes) stay, every link through a severed wormhole goes
                    //    (alpha-beta, Taivra); Sever on any system with your units - Ghosts of Creuss: Quantum
                    //    Entanglement neighbours (LRR 60.3), unstoppable (Sever, Nexus Sovereignty, Void Tether),
                    //    deep space cannon and Domna through it; Hil Colish may move first: its delta wormhole in
                    //    the active system lets the other ships through (its own gravity rift roll first); Icarus
                    //    Drive after ANY activation, any mech on the board, the token to the MECH's system; the
                    //    original Wormhole Generator (no Codex / TE) must place or move a token (no "may");
                    //    Dimensional Splicer may hit units fighting as ships (The Alastor, Z-Grav Eidolon); Emissary
                    //    Taivra: epsilon counts, after other players' activations (people; bots when promised - lendable),
                    //    its adjacency for everyone (retreats, deep space cannon, neighbours); Sai Seravus: how many
                    //    fighters, the unlock counts Mallice under Nexus Sovereignty; Riftwalker Meian: not the
                    //    Fracture's systems (TE rulebook); Particle Synthesis counts each wormhole (not each kind)
                    //    (24 is FROZEN: 0.59.0 games carry it - later changes go in 25 and up)
                    // 25 the faction debug pass, part 2 (0.59.05 - FROZEN) - the Firmament: Plots Within Plots also for the
                    //    agenda-phase secrets (Dictate Policy, Drive the Debate), never an eliminated player's; Puppets of
                    //    the Blade: the Obsidian control Cronos Hollow / Tallin Hollow (others' units there coexist);
                    //    Heaven's Eye is repaired after ground combat rounds too; Viper EX-23 / Research Team decide to
                    //    coexist at the commit (before space cannon defense), the Viper with neutral units too; Neural
                    //    Parasite (Obsidian) may destroy neutral infantry (option ids * 4096 + system * 16 + side);
                    //    Planesplitter (Obsidian) moves ingress tokens only on the board (not the off-board homes); Myru
                    //    Vos: offered with ships that can move in and real space cannon (neutral too, the space cannon
                    //    step's own gun list) or a route through, a person is asked on others' moves too ("don't ask
                    //    again this round"), Experimental Battlestation / Seidr Project can't fire at those ships;
                    //    Solar Flare blocks Experimental Battlestation too (LRR 58.7: part of the movement step); Captain
                    //    Aroz counts for Occupy the Seat of the Empire and an elected planet (Drive the Debate); space
                    //    stations out of Establish Hegemony / Hoard Raw Materials / Stake Your Claim (TE rulebook);
                    //    Nocturne: the Firmament / the Obsidian are one faction for Yin Ascendant and Mercenaries for
                    //    Hire (one entry, a random side; none for Yin while either is played); secrets scored through
                    //    Plots Within Plots count for "Have 3 scored objectives" (heroes); Sharsiss purged last;
                    //    control passing because of coexistence keeps the coexisting player's structures / neutral
                    //    units (setControl), coexisting docks are blockaded (TE rulebook); The Reaping counts the
                    //    combats combatWon declares (not a Skilled Retreat draw) and pays out in two gains; Vos Hollow
                    //    also after the start-of-combat effects and the first barrage, "don't ask again" as a bystander;
                    //    Sharsiss Hollow purged last; Malevolency also asked when a tactical action ends early;
                    //    Enervate only for the puppet's own strategy cards (not Mathis Mathinus on another); Siphon for
                    //    every commodity gain (commodityGained at the sites outside replenished()); Seethe's reveal
                    //    doesn't offer space stations; Assail +1 to Experimental Battlestation against a puppet; LRR
                    //    63.4 / 79.4 for coexisting structures: a player's PDS / space docks on another player's planet
                    //    with none of their ground forces left there are destroyed (coexistStructures); Sardakk:
                    //    each Exotrireme II can be sacrificed after the same round, its targets listed damaged /
                    //    undamaged (ids type * 16 + k, + 8 damaged), and it comes after the Retreat step (LRR 78.7/78.8);
                    //    Valkyrie Particle Weave looks at the hits the opponent's dice produced (not their own extra hit);
                    //    T'ro is also offered when a tactical action ends early (Minister of Peace, afterActivation);
                    //    G'hom Sek'kus keeps a damaged mech damaged, and what it brought but didn't land goes back;
                    //    Sh'val is also offered when only G'hom Sek'kus can bring ground forces in, and only when ships
                    //    of theirs moved into the active system
                    // 26 the faction debug pass, part 3 (0.59.15 - FROZEN; the next rule fix goes behind 27) - the Embers of Muaat: games without the Codex and
                    //    Thunder's Edge play the original Magmus Reactor (1 trade good after PRODUCTION in a system with
                    //    a war sun / next to a supernova; no Production 5 in supernovas - magmusOriginal); Umbat asks
                    //    which player produces (that player picks the system, fx.anyPlanetGFFor); Adjudicator Ba'al
                    //    destroys neutral units too, is never offered in the Fracture, returns the Muaat's own units on
                    //    the purged planets to their reinforcements and leaves a frontier token; Stellar Genesis after
                    //    a war sun moved through Avernus's system too (avernusThrough_); Fires of the Gashlai / Advent of
                    //    the War Sun give a faction its own war sun upgrade (the Roh'Dhna's Terrafactory II); the Arborec:
                    //    Letani Ospha picks any player's non-fighter ship and that player chooses the replacement (any
                    //    other non-fighter ship costing up to 2 more - equal or cheaper too; a damaged one goes first);
                    //    the Yin: Devotion after the Retreat step, the Yin choose the ship hit (a damaged one apart);
                    //    Van Hauge destroys neutral ships too; when every unit is destroyed at once, units placed while
                    //    they go (Brother Milor's fighters) aren't destroyed by the same hits (assignHits); the original
                    //    Brother Milor for any player's destroyer / cruiser (asked as the Omega; a declined Omega offer
                    //    passes on); Dannel of the Tenth fights neutral ground forces, may land on the Yin's own planets
                    //    and on Mallice (the nexus flips); Brother Omar Omega asks a person which infantry goes back;
                    //    Indoctrination's mech version without an infantry in the reinforcements; the Mahact: Il Na Viroset
                    //    ends the turn at the activation (the newer ruling) and works for anyone who has it; Skilled
                    //    Retreat / Clan's Favor can't be used in Airo Shir Aur's combat; Scepter of Dominion may pick
                    //    the wormhole nexus; Jae Mir Kan with Construction's secondary places the active player's token;
                    //    the Letnev: War Funding Omega's two choices (the holder's own dice; the opponent's only if they
                    //    say so); when the hits destroy every unit, units that can sustain still use Sustain Damage first
                    //    with Rear Admiral Farran (a trade good each) or the Sardakk's Valkyrie Exoskeleton (a hit back);
                    //    the Vuil'raith: Crucible's additional +1 move through gravity rifts (and offered for it); the
                    //    combat's two sides are known from its very start (Devour - and Technological Singularity - by
                    //    Ambush / Impulse Core hits, not the last combat's sides); the L1Z1X: The Helmsman's ships bring what
                    //    would be stranded; the Annihilator line only before a bombardment; the original Cybernetic
                    //    Enhancements without the Codex (start of the holder's turn); I48S on another player's activation (a
                    //    person playing the L1Z1X); Annihilators on other planets of the system join Harrow; the Sol:
                    //    Claire Gibson after the active player's start-of-ground-combat abilities (LRR 1.19); the Hacan:
                    //    Gila the Silvertongue takes any number of trade goods; the Hacan choose where Pride of Kenara's
                    //    units go; Harrugh Gefhara only when units use PRODUCTION (not Dirzuga Rophal, Sling Relay...);
                    //    the Winnu: Mathis Mathinus' players chosen by the Winnu (no Acquiescence there), the original
                    //    Acquiescence without the Codex (end of the strategy phase), Reclaimer after Reclamation, Berekar
                    //    Berekon / Hegemonic Trade Policy only on PRODUCTION; the Yssaril: So Ata looks at action cards,
                    //    promissory notes or secret objectives; the Empyrean: Acamar for any player's move, Dynamo for any
                    //    player's unit, Dark Pact through a Trade Agreement, Procyon purged last, a space station isn't a
                    //    planet (frontier tokens at setup / Procyon / Acamar), Void Tether when you activate - the Empyrean
                    //    choose which token moves, no deep space cannon or neighbours across it, a person may allow it;
                    //    the Nomad: Field Marshal Mercer / The Thundarian for any player (Mercer: from anywhere, onto
                    //    each planet there), Quantum Manipulator against anti-fighter barrage, Future Sight only in the
                    //    agenda phase, Ssruu as Artuno, Ahk-Syl Siven without a flagship out, Memoria retreats into a
                    //    mech system, The Cavalry's real Sustain Damage; the Argent Flight: Zeal's first vote only in the
                    //    agenda phase, Trilossa Aun Mirik for any player's production, Mirik Aun Sissiri purged last and
                    //    carrying cargo, Wing Transfer when activating (only units count)
                    // 27 the faction debug pass, part 4 (0.59.30 - FROZEN; the next rule fix goes behind 28) - the Mentak Coalition: Ssruu as Suffi An after the Mentak
                    //    pillage another player; Pillage after commodities become trade goods (The Stillness of Stars,
                    //    Crimson Legionnaire - gainTG); The Cavalry's Sustain Damage is blocked where Sustain Damage is (the
                    //    Fourth Moon, Moll Terminus, entropic scars); the Xxcha Kingdom: Peace Accords not on a planet with neutral
                    //    units, Political Favor only with a token in the Xxcha's strategy pool (LRR 1.17), Ggrocuto Rinn REMOVES
                    //    the infantry, Xxekir Grom (Prophecy of Kings) purged last with the other players' abilities silenced
                    //    (mutedFor_), Xxekir Grom Omega's combined value for votes too; the Naaz-Rokha Alliance: Garv and
                    //    Gunn at the end of any player's turn (garvGunn27, lendable), Hesh and Prit purged last, Absolute
                    //    Synergy without using the action (option 645), the Z-Grav Eidolon without Sustain Damage (Game::zgrav); the
                    //    Titans of Ul: Tellurian for any player's units (tellurian27), Ul the Progenitor attached (not purged);
                    //    the Naalu: Foresight not into a nebula and taking ground forces along, the Matriarch's fighters
                    //    committed alone (a draw), Thunder's Edge's Z'eu and Iconoclast (teNaalu); the Clan of Saar: Captain
                    //    Mendosa for any player's activation (mendosa27, lendable), Gurno Aggero purged last and destroying
                    //    neutral units, Rowl Sarrig dock by dock (rowlSarrig27), floating factories destroyed when blockaded
                    //    by a retreat / Foresight / a production / at the end of a turn (floatingSweep), Deorbit Barrage without
                    //    a 10-dice cap; Thunder's Edge coexistence ended as one decision per planet; the Universities of
                    //    Jol-Nar: Brilliant below 4 resources (and only when a command token was spent), Doctor Sucaban for
                    //    any player's research (sucaban27; a person picks the infantry), Research Agreement only for a
                    //    technology the holder could own; the Last Bastion: Phoenix Standard / Raise the Standard only for
                    //    units that took part, Dame Briar for any player's units (dameBriar27; a PDS may be galvanized),
                    //    Nip and Tuck unlocked by anyone's galvanized units; the Council Keleres: Suffi An's extra action
                    //    logged, Xander offered to a person on any payment their commodities can go toward (xanderQuiet_);
                    //    the Deepwrought: a research the Deepwrought refuse Carrina for is picked again (carrinaNo_),
                    //    Carrina also offered to keep a specialty planet ready (carrinaSpare), Research Team with neutral
                    //    units, Oceanbound's limit at any time (oceanCheck) with exhausted cards first, Aello for every
                    //    holder + a person's gain / convert choice, Share Knowledge gained, ocean cards vote and ready
                    //    after the agenda phase, D.W.S. Luminous +1 through every system with their units; the Ral Nel:
                    //    Survival Instinct / Watchful Ojz ships take their cargo (carryAlong), Ojz at the retreat's
                    //    declaration (+ The Last Dispatch with it), Alarum from the same system's other planets;
                    //    Deepgloom Executable's Stall Tactics not offered again in a turn the Yssaril refused it; the
                    //    shared technologies: Dreadnought II immune to Direct Hit, Predictive Intelligence's votes and
                    //    its exhaust in the log, Integrated Economy in every phase but the setup; the action cards:
                    //    Manipulate Investments a trade good at a time (any split over 3+ cards), Scramble Frequency for
                    //    anyone but the roller, Decoy Operation from space areas and the system's other planets, Ghost
                    //    Squad any number from / to any of your planets in the system; the Nekro Virus: The Alastor any
                    //    number of ground forces, the system's planets' too (alastorChoice27: they fight from the space
                    //    area, go back afterwards or on a retreat, no capacity; they fight on without ships, and Ambush /
                    //    Impulse Core / Devotion / Waylay / Reflective Shielding / Direct Hit on a mech take them),
                    //    Mordred in a space combat too, Nekro Malleon on any player (nekroMalleon), Technological
                    //    Singularity + Antivirus from the very start of a space combat (nekroCombatStart), Propagation for
                    //    Reveal Prototype and the Book of Latvinia, no Nekro Acidos card for a Valefar token, Plagiarize
                    //    and the Maw of Worlds a gain (not research); the entropic scar paid with the Scepter of Emelpar
                    //    (+ a feed note for a person with ships there and nothing to pay with)
    // 0.54.0: the human players' habits from their earlier games (the bots adapt to them); saved as "habits" when known
    HabitProfile habits;
    // The Council Keleres (0.21.0): per seat, the faction whose home system a
    // Keleres seat takes (The Tribunii: Mentak, Xxcha or Argent; -1 = chosen
    // at random among the ones not in the game). Saves without it load empty.
    std::vector<int> tribunii;
};

enum class EventKind : uint8_t {
    Log,          // plain message
    Dice,         // a roll (dice / need / hits filled in)
    Destroyed,    // unit + count destroyed
    Damaged,      // unit used Sustain Damage
    Score,        // victory point(s) gained
    Phase,        // phase change / objective revealed
    StrategyPick, // player takes strategy card 'card' in the strategy phase
    TurnStart,    // player's turn begins
    TurnEnd,      // player's turn ends
    RoundEnd,
    ActionChosen, // card: 0..7 strategic, 20 tactical, 21 pass
    Activate,     // tactical action activates 'system' (hostile = enemy units there)
    Move,         // ships moved into 'system' from 'from'
    Land,         // ground forces landed on 'planet'
    Produce,
    Secondary,    // player uses a strategy card's secondary ability (card)
    CombatStart,  // a = attacker forces, b = defender forces; space or ground
    CombatEnd,    // player = winner (-1 draw)
    Retreat,
    Control,      // player gains control of 'planet'
    Research,
    Eliminated,
    Warning,
    ActionCard,   // player plays action card 'card' (other = player it targets, if any)
    Agenda,       // agenda 'card' is revealed
    Vote,         // player casts 'count' votes (text says for what)
    AgendaResult, // the agenda is resolved (text says how)
    Explore,      // player explores (card = exploration card, planet / system)
    Relic,        // player gains relic 'card'
    Leader,       // a leader ('card' = kLeaders index) is unlocked / used (label says which)
    Ability,      // a faction ability, faction technology or faction note is used (label = its name)
    AgendaPeek,   // the human player looks at agenda 'card' (Politics, Z'eu) - shown large until A (label = where from)
    Deal,         // negotiations: an offer, a deal made / declined, a promise broken (player, other)
    Reveal,       // end of round 3: the computer players' archetypes are revealed (text lists them)
    Betrayal,     // a computer player turns on another: player = offended bot, other = target, card = Betrayal kind,
                  // label = its name, text = what it will do (system = where, count = third player for a rumour)
    TilePlaced,   // galaxy setup: player placed a system tile ('system' = where it now is)
    // 0.16.0 (owner: cards shown large when used)
    ObjectiveScored,  // player scored objective 'count' (kObjectives index; before the Score event)
    RelicUsed,        // player exhausts or purges relic 'card' (label = "uses" / "purges")
    ComponentUsed,    // player uses a component action (not an action card or relic): label = name
                      // + '\n' + what it does, count = the action option id
    Planetfall,       // 0.24.0: the custodians token comes off Mecatol Rex (player = who) - the planetfall scene
    CardDrawn,        // 0.29.0: player drew action card 'count' (display only; shown only to that player)
    ObjectiveRevealed,// 0.29.0: public objective 'count' revealed (display only; the Phase log line stays)
    Note,             // 0.29.0: a feed line that stays out of the game log (old saves replay unchanged)
};

constexpr int8_t kActionTactical = 20;
constexpr int8_t kActionPass = 21;

struct Event {
    EventKind kind = EventKind::Log;
    int8_t player = -1;
    int8_t other = -1;   // opponent / second player
    int16_t system = -1;
    int16_t planet = -1;
    std::string text;
    std::string label;         // Dice: what was rolled ("Combat round 2", "Space cannon", ...)
    std::vector<int8_t> dice;  // rolled values
    std::vector<int8_t> need;  // value each die needed to hit
    int hits = 0;
    int16_t card = -1;   // strategy card (0..7), action kind, action card or agenda (int16: 0.34.0 - over 127 action cards)
    int8_t unit = -1;    // UnitType for Destroyed / Damaged
    int16_t count = 0;
    bool hostile = false;
    bool space = true;   // space or ground combat
    Force a, b;          // combat snapshots
    std::vector<int16_t> from;
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void start(const Setup& s);
    bool started() const { return started_; }
    bool finished() const { return finished_; }
    bool waiting() const { return hasPending_; }
    const Decision& decision() const { return pending_; }
    // Returns false (and fills err) if the answer is not acceptable.
    bool answer(const Answer& a, std::string* err = nullptr);

    // Undo: go back to the previous decision if it belonged to the same
    // player and nothing random or hidden happened since.
    bool canUndo() const;
    bool undo();
    // 0.43.0 (owner: "I cannot undo an activation when I have a second decision" - computer players were asked
    // something in between): answers to drop to get back to this player's previous decision, passing over the
    // answers of players `computer` names; 0 when that can't be done (dice rolled or something revealed since)
    int undoDepth(const std::function<bool(int)>& computer) const;
    bool undoSteps(int n);

    const Game& game() const { return g_; }
    const Setup& setup() const { return setup_; }
    int answerCount() const { return int(answers_.size()); }
    std::vector<Event> takeEvents();
    std::string crashMessage() const { return crash_; }

    // Save / load (text). load() replays every answer.
    std::string saveText() const;
    bool loadText(const std::string& text, std::string* err);

    // --- used by the rules code (runs inside the coroutine) -------------
    Game& g() { return g_; }
    Rng& rng() { return rng_; }
    Answer ask(Decision d);
    void emit(Event e);
    void log(int player, const std::string& text, EventKind kind = EventKind::Log, int system = -1);
    void revealed() { ++reveals_; }  // hidden information became known: undo can't cross this
    std::function<void()> beforeAsk;  // the rules check state-based triggers (leader unlocks) before each question

private:
    struct AskRecord {
        int8_t player;
        uint32_t rng;
        uint32_t reveals;
    };
    void fiberMain();
    void resume();
    void switchToGame();  // main code -> the game's stack
    void switchToMain();  // the game's stack -> main code
    bool replay(const std::vector<Answer>& answers, std::string* err);

    Setup setup_;
    Game g_;
    Rng rng_;
    std::vector<Answer> answers_;
    std::vector<AskRecord> asks_;
    std::vector<Event> events_;
    uint32_t reveals_ = 0;

    bool started_ = false, finished_ = false, hasPending_ = false, aborting_ = false;
    Decision pending_;
    Answer current_;
    std::string crash_;

#ifdef _WIN32
    // Windows has no ucontext: the game runs on a Win32 fiber instead.
    static void __stdcall fiberProc(void* self);
    void* mainFiber_ = nullptr;
    void* fiber_ = nullptr;
#else
    static void trampoline(uint32_t lo, uint32_t hi);
    ucontext_t mainCtx_{}, fiberCtx_{};
    std::vector<char> stack_;
#endif
};

}  // namespace ti
