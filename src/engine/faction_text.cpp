// Faction abilities, leaders, faction promissory notes and special unit
// abilities - in our own short wording (never the printed card text).
// Factions are switched on in batches (Stage 5); factionImplemented() says
// which ones work so far.
#include <cstring>
#include <vector>

#include "engine/data.h"
#include "engine/state.h"

namespace ti {

namespace {

struct KT {
    const char* key;
    const char* text;
};

const char* find(const KT* t, int n, const char* key) {
    for (int i = 0; i < n; ++i)
        if (!std::strcmp(t[i].key, key)) return t[i].text;
    return "";
}
#define FIND(table, key) find(table, int(sizeof(table) / sizeof(table[0])), key)

// Factions whose abilities work (Stage 5 batches).
const char* kDone[] = {"sol", "letnev", "hacan", "jolnar", "sardakk", "xxcha",                // batch 1 (0.6.0)
                       "arborec", "l1z1x", "mentak", "naalu", "yin", "yssaril",             // batch 2 (0.7.0)
                       "ghost", "muaat", "saar", "winnu", "nekro", "empyrean",              // batch 3 (0.8.0)
                       "argent", "mahact", "naaz", "nomad", "titans", "cabal",              // batch 4 (0.9.0)
                       "keleres",                                                           // batch 5 (0.21.0)
                       "bastion", "ralnel", "deepwrought", "crimson", "firmament", "obsidian",  // batch 6 (0.35.0, Thunder's Edge)
                       "kortali", "vaylerian", "lanefir", "kyro", "cheiran", "celdauri",        // Discordant Stars batch 1 (0.37.0)
                       "khrask", "zealots", "zelian", "rohdhna", "tnelis", "kolume",           // Discordant Stars batch 2 (0.38.0)
                       "bentor", "dihmohn", "florzen", "gledge", "augers", "kjalengard",       // Discordant Stars batch 3 (0.39.0)
                       "kollecc", "mortheus", "vaden", "cymiae", "nivyn", "olradin",           // Discordant Stars batch 4 (0.40.0)
                       "freesystems", "edyn", "nokar", "axis", "mykomentori", "ghemina",      // Discordant Stars batch 5 (0.41.0)
                       "veldyr", "lizho", "mirveda", "ghoti",                                 // Discordant Stars batch 6 (0.42.0)
                       "atokera",                                                             // Blue Reverie 1 (0.83.0)
                       "belkosea",                                                            // Blue Reverie 2 (0.84.0)
                       "pharadn"};                                                            // Blue Reverie 3 (0.85.0)

// Faction abilities, keyed by their name.
const KT kAbility[] = {
    // Federation of Sol (the Sol review, 2026-11-12: the cards' wording)
    {"Orbital Drop", "ACTION: Spend 1 token from your strategy pool to place 2 infantry from your reinforcements on 1 planet you control."},
    {"Versatile", "When you gain command tokens during the status phase, gain 1 additional command token."},
    // Barony of Letnev
    // (the Letnev review, 2026-11-12: the cards' wording)
    {"Munitions Reserves", "At the start of each round of space combat, you may spend 2 trade goods; you may re-roll any number of your dice during that combat round."},
    {"Armada", "The maximum number of non-fighter ships you can have in each system is equal to 2 more than the number of tokens in your fleet pool."},
    // Emirates of Hacan
    // (the Hacan review, 2026-11-12: the cards' wording)
    {"Masters of Trade", "You do not have to spend a command token to resolve the secondary ability of the \"Trade\" strategy card."},
    {"Guild Ships", "You can negotiate transactions with players who are not your neighbor."},
    {"Arbiters", "When you are negotiating a transaction, action cards can be exchanged as part of that transaction."},
    // Universities of Jol-Nar
    // (the Jol-Nar review, 2026-11-13: the cards' wording)
    {"Fragile", "Apply -1 to the result of each of your unit's combat rolls."},
    {"Brilliant", "When you spend a command token to resolve the secondary ability of the \"Technology\" strategy card: You may "
                  "resolve the primary ability instead."},
    {"Analytical", "When you research a technology that is not a unit upgrade technology: You may ignore 1 prerequisite."},
    // Sardakk N'orr
    {"Unrelenting", "Apply +1 to the result of each of your unit's combat rolls."},
    // Xxcha Kingdom
    // (the Xxcha review, 2026-11-13: the cards' wording)
    {"Peace Accords", "After you resolve the primary or secondary ability of the \"Diplomacy\" strategy card: You may gain control of 1 "
                      "planet other than Mecatol Rex that does not contain any units and is in a system that is adjacent to a planet you "
                      "control."},
    {"Quash", "When an agenda is revealed: You may spend 1 token from your strategy pool to discard that agenda and reveal 1 agenda from "
              "the top of the deck. Players vote on this agenda instead."},
    // Arborec
    {"Mitosis", "Your space docks cannot produce infantry. At the start of the status phase, place 1 infantry from your reinforcements on any planet you control."},
    // L1Z1X Mindnet
    // (the L1Z1X review, 2026-11-12: the cards' wording)
    {"Assimilate", "When you gain control of a planet, replace each PDS and space dock that is on that planet with a matching unit from your reinforcements."},
    {"Harrow", "At the end of each round of ground combat, your ships in the active system may use their BOMBARDMENT abilities against your opponent's ground forces on the planet."},
    // Mentak Coalition
    // (the Mentak review, 2026-11-13: the cards' wording)
    {"Ambush", "At the start of a space combat, you may roll 1 die for each of up to 2 of your cruisers or destroyers in the system. For "
               "each result equal to or greater than that ship's combat value, produce 1 hit; your opponent must assign it to 1 of their "
               "ships."},
    {"Pillage", "After 1 of your neighbors gains trade goods or resolves a transaction, if they have 3 or more trade goods, you may take "
                "1 of their trade goods or commodities."},
    // Naalu Collective
    {"Telepathic", "At the end of the strategy phase, place the Naalu \"0\" token on your strategy card; you are first in initiative "
                   "order."},
    {"Foresight", "After another player moves ships into a system that contains 1 or more of your ships, you may place 1 token from "
                  "your strategy pool in an adjacent system that does not contain another player's ships; move your ships from the "
                  "active system into that system."},
    // Yin Brotherhood
    // (the Yin review, 2026-11-12: the cards' wording)
    {"Indoctrination", "At the start of a ground combat, you may spend 2 influence to replace 1 of your opponent's participating infantry with 1 infantry from your reinforcements."},
    {"Devotion", "After each space combat round, you may destroy 1 of your cruisers or destroyers in the active system to produce 1 hit and assign it to 1 of your opponent's ships in that system."},
    // Yssaril Tribes
    // (the Yssaril review, 2026-11-12: the cards' wording)
    {"Stall Tactics", "ACTION: Discard 1 action card from your hand."},
    {"Scheming", "When you draw 1 or more action cards, draw 1 additional action card. Then, choose and discard 1 action card from your hand."},
    {"Crafty", "You can have any number of action cards in your hand. Game effects cannot prevent you from using this ability."},
    // Ghosts of Creuss
    {"Quantum Entanglement", "You treat every system with an alpha or beta wormhole as adjacent to every other one. No game effect can prevent you from using this ability."},
    {"Slipstream", "During your tactical actions, each of your ships that starts its move in your home system or in a system with an alpha or beta wormhole gets +1 to its move value."},
    {"Creuss Gate", "Your home system (Creuss) is kept off the board, in your play area. The Creuss Gate takes its place on the board. The Creuss Gate is not a home system. (Both tiles have a delta wormhole, so they are adjacent.)"},
    // Embers of Muaat
    {"Star Forge", "ACTION: Spend 1 token from your strategy pool to place either 2 fighters or 1 destroyer from your reinforcements in a system that contains 1 or more of your war suns."},
    {"Gashlai Physiology", "Your ships can move through supernovas."},
    // Clan of Saar
    {"Scavenge", "After you gain control of a planet: Gain 1 trade good."},
    {"Nomadic", "You can score objectives even if you do not control the planets in your home system."},
    // Winnu
    // (the Winnu review, 2026-11-12: the cards' wording)
    {"Blood Ties", "You do not have to spend influence to remove the custodians token from Mecatol Rex."},
    {"Reclamation", "After you resolve a tactical action during which you gained control of Mecatol Rex, you may place 1 PDS and 1 space dock from your reinforcements on Mecatol Rex."},
    // Nekro Virus
    // (the Nekro review, 2026-11-14: the cards' wording)
    {"Galactic Threat", "You cannot vote on agendas. Once per agenda phase, after an agenda is revealed: You may predict aloud the outcome "
                        "of that agenda. If your prediction is correct, gain 1 technology that is owned by a player who voted how you "
                        "predicted."},
    {"Technological Singularity", "Once per combat, after 1 of your opponent's units is destroyed: You may gain 1 technology that is "
                                  "owned by that player."},
    {"Propagation", "You cannot research technology. When you would research a technology: Gain 3 command tokens instead."},
    // Empyrean
    // (the Empyrean review, 2026-11-12: the cards' wording)
    {"Voidborn", "Nebulae do not affect your ships' movement."},
    {"Aetherpassage", "After a player activates a system, you may allow that player to move their ships through systems that contain your ships."},
    {"Dark Whispers", "During setup, take the additional Empyrean faction promissory note; you have 2 faction promissory notes."},
    // Argent Flight
    // (the Argent review, 2026-11-13: the cards' wording)
    {"Zeal", "You always vote first during the agenda phase. When you cast at least 1 vote, cast 1 additional vote for each player in the game, "
             "including you."},
    {"Raid Formation", "When 1 or more of your units uses ANTI-FIGHTER BARRAGE, for each hit produced in excess of your opponent's fighters, "
                       "choose 1 of your opponent's ships that has SUSTAIN DAMAGE to become damaged."},
    // Mahact Gene-Sorcerers
    // (the Mahact review, 2026-11-12: the cards' wording)
    {"Edict", "When you win a combat, place 1 command token from your opponent's reinforcements in your fleet pool if it does not already contain 1 of that player's tokens; other player's tokens in your fleet pool increase your fleet limit but cannot be redistributed."},
    {"Imperia", "While another player's command token is in your fleet pool, you can use the ability of that player's commander, if it is unlocked."},
    {"Hubris", "During setup, purge your \"Alliance\" promissory note. Other players cannot give you their \"Alliance\" promissory note."},
    // Naaz-Rokha Alliance
    {"Distant Suns", "When you explore a planet that contains 1 of your mechs: You may draw 1 additional card; choose 1 to resolve and "
                     "discard the rest."},
    {"Fabrication", "ACTION: Either purge 2 of your relic fragments of the same type to gain 1 relic; or purge 1 of your relic fragments "
                    "to gain 1 command token."},
    // Nomad
    // (the Nomad review, 2026-11-13: the cards' wording)
    {"The Company", "During setup, take the 2 additional Nomad faction agents and place them next to your faction sheet; you have 3 agents."},
    {"Future Sight", "During the agenda phase, after an outcome that you voted for or predicted is resolved, gain 1 trade good."},
    // Titans of Ul
    {"Terragenesis", "After you explore a planet that does not have a sleeper token, you may place or move 1 sleeper token onto that "
                     "planet."},
    {"Awaken", "After you activate a system that contains 1 or more of your sleeper tokens, you may replace each of those tokens with "
               "1 PDS from your reinforcements."},
    {"Coalescence", "If your flagship or your AWAKEN faction ability places your units into the same space area or onto the same planet "
                    "as another player's units, your units must participate in combat during \"Space Combat\" or \"Ground Combat\" "
                    "steps."},
    // Vuil'raith Cabal
    // (the Vuil'raith review, 2026-11-12: the cards' wording)
    {"Devour", "Capture your opponent's non-structure units that are destroyed during combat."},
    {"Amalgamation", "When you produce a unit, you may return 1 captured unit of that type to produce that unit without spending resources."},
    {"Riftmeld", "When you research a unit upgrade technology, you may return 1 captured unit of that type to ignore all of the technology's prerequisites."},
    // Council Keleres (Thunder's Edge version)
    {"The Tribunii", "During setup, choose an unplayed faction from among the Mentak, the Xxcha and The Argent Flight; take that faction's home system, command tokens and control markers. Additionally, take the Keleres Hero that corresponds to that faction."},  // (the Keleres review: the card's wording)
    {"Council Patronage", "At the start of the strategy phase: Replenish your commodities, then gain 1 trade good."},  // (the Keleres review: the card's wording)
    {"Law's Order", "At the start of any player's turn: You may spend 1 trade good or 1 commodity to treat all laws as blank until the end of that turn."},  // (the Keleres review: the card's wording)
    // Thunder's Edge factions (0.35.0)
    // (the Last Bastion review, 2026-11-13: the cards' wording; the last sentence of Galvanize is this game's rule - owner:
    // "keep the automatic rule for now ... this may change in the future")
    {"Liberate", "When you gain control of a planet: ready that planet if it contains a number of your infantry equal to or greater "
                 "than that planet's resource value; otherwise, place 1 infantry on that planet."},
    {"Galvanize", "When a game effect instructs a player to galvanize a unit, they place a galvanize token beneath it if it does not "
                  "have one. Galvanized units roll 1 additional die for combat rolls and unit abilities. (When some units of a kind "
                  "are galvanized, the others are lost first.)"},
    {"Phoenix Standard", "At the end of combat: you may galvanize 1 of your units that participated."},
    {"Survival Instinct", "After another player activates a system that contains your ships: You may move up to 2 of your ships into the active system from adjacent systems that do not contain your command tokens. (They bring the fighters and ground forces they transport.)"},  // (the Ral Nel review: the card's wording)
    {"Miniaturization", "Your structures can be transported by any ship; this does not require or count against capacity. While your structures are in the space area, they cannot use their unit abilities. At the end of your tactical actions: You may place your structures that are in space areas onto planets you control in their respective systems."},  // (the Ral Nel review: the card's wording)
    {"Research Team", "When ground forces are committed: If your units on that planet are not already coexisting, you may choose for your units to coexist."},  // (the Deepwrought review: the card's wording)
    {"Oceanbound", "When your units begin coexisting on a planet: Gain an ocean card and ready it. Any time you have more ocean cards than there are planets that contain your coexisting units, discard ocean cards until you do not. (Ocean cards are planet cards: 1 resource, 1 influence.)"},  // (the Deepwrought review: the card's wording)
    {"Sundered", "You can't use any wormholes except epsilon wormholes. Other players' units that move or are placed into your home system are destroyed."},
    {"Incursion", "When you activate a system with a breach, you may flip it. Systems with active breaches are adjacent to each other. At the end of the status phase, a player with ships where an active breach is may remove it."},
    {"Sorrow", "Your home system (Ahk Creuxx) is kept off the board, in your play area. The Sorrow takes its place on the board and starts with an inactive breach. The Sorrow is not a home system. (Both tiles have an epsilon wormhole, so they are adjacent.)"},
    {"Plots Within Plots", "You can score secret objectives already scored by other players if you fulfill their requirements; this does not count against your secret objective limit or the number you can score in a round. When you score another player's secret objective, do not gain a victory point; instead, place a facedown plot card into your play area with that player's control token on it."},
    {"Puppets of the Blade", "If you have at least 1 plot card in your play area, gain the following ability: ACTION: Purge The Firmament's faction sheet, leaders, planet cards, and promissory note. Then, gain all of the faction components for The Obsidian."},
    {"Nocturne", "This faction cannot be chosen during setup."},
    {"The Blade's Orchestra", "When this faction comes into play: flip your home system, double-sided faction components, and all of your in-play plot cards. Then, ready Cronos Hollow and Tallin Hollow if you control them."},
    {"Marionettes", "The player or players whose control tokens are on each plot card are the puppeted players for that plot."},
    // Discordant Stars (0.37.0) - Kortali Tribunal
    {"Zealous", "When you spend a command token to resolve the secondary ability of the \"Warfare\" strategy card, you may resolve the primary ability instead."},
    {"Ruthless", "At the start of a ground combat on an exhausted planet you do not control, you may produce 1 hit and assign it to 1 of your opponent's ground forces on that planet."},
    // Vaylerian Scourge
    {"Cargo Raiders", "During the first round of a space combat, you may prevent your opponent from declaring a retreat unless they spend 1 trade good."},
    {"Scour", "Once per tactical action, after you gain control of a planet, you may discard 1 action card to ready that planet."},
    {"Raze", "After 1 or more of another player's structures are destroyed on a planet that contains your units, you may replenish your commodities."},
    // Lanefir Remnants
    {"A New Edifice", "You may not use technology specialties. When you would gain a relic, instead purge it and explore up to 3 planets you control."},
    {"Iconoclasm", "When researching a non-unit upgrade technology, you may purge 1 of your relic fragments to ignore 1 prerequisite on the technology you are researching."},
    {"War Stories", "Once per action, after you win a combat, you may explore 1 planet you control, or if the active system does not contain any planets, the frontier exploration deck in the active system."},
    // Kyro Sodality
    {"Contagion", "After you resolve the primary or secondary ability of the \"Politics\" strategy card, commit 1 infantry from your reinforcements to a planet that is in or adjacent to a system that contains a planet you control; resolve invasion on that planet."},
    {"Plague Reservoir", "Once per action, during invasion on a planet that contains your units, you may resolve ground combat on that planet, even if it does not contain another player's ground forces."},
    {"Subversive", "When participating in a combat that would end in a draw, you are treated as the winner instead."},
    // Cheiran Hordes
    {"Teeming", "During setup place the 2 Cheiran dreadnought and 1 Cheiran mech tokens in your reinforcements. The Cheiran unit tokens are additional units of their type."},
    {"Moult", "After you win a space combat as the defender: You may produce 1 ship in the active system, reducing the cost by 1 for each of your non-fighter ships destroyed during that combat."},
    {"Byssus", "You may treat your mechs on planets you control as structures for any purpose other than scoring objectives."},
    // Celdauri Trade Confederation
    {"Projection of Power", "At the start of a space combat in a system that is adjacent to or contains 1 or more of your space docks: Choose up to 1 ship in that system to gain ANTI-FIGHTER BARRAGE 6 until the end of that combat."},
    {"Industrialists", "During setup, place the Celdauri space dock token in your reinforcements; the Celdauri space dock token is a fourth space dock unit."},
    // Discordant Stars batch 2 (0.38.0) - L'tokk Khrask
    {"Lithoids", "During the agenda phase, the number of votes you cast is instead equal to the combined resource value of the planets that you exhaust."},
    {"Garden Worlds", "Apply +1 to the resource values of your planets that do not contain 1 or more ground forces."},
    {"Meteor Slings", "When your units use BOMBARDMENT against a planet another player controls, you may cancel any number of hits you produce to place that many infantry from your reinforcements onto that planet. Those units participate in ground combat, if able."},
    // Zealots of Rhodun
    {"Conspirators", "Once per agenda phase, after an agenda is revealed, if you are not the speaker, you may choose to vote after the speaker on that agenda."},
    {"Ancient Knowledge", "When you use a technology specialty to ignore a prerequisite on a technology card you are researching, you may ignore 1 "
                          "additional prerequisite of the same color. After you exhaust a planet to use its technology specialty, you may gain 1 "
                          "commodity."},
    // Zelian Purifier
    {"Obsessive Designs", "During the action phase, after you research a unit upgrade technology, you may use the PRODUCTION ability of 1 of "
                          "your space docks in your home system to produce units of that type, reducing the combined cost of the produced units "
                          "by 2."},
    {"Biophobic", "During the agenda phase, the number of votes you cast is instead equal to the number of planets you exhaust to cast votes."},
    {"Paranoia", "Game effects other than your command tokens cannot prevent you from activating, or moving ships into, your home system."},
    // Roh'Dhna Mechatronics
    {"Industrious", "After you place a space dock in a system that contains no other players' ships, you may spend 6 resources and remove that "
                    "space dock to place 1 war sun in that system's space area."},
    {"Recycled Materials", "After you activate a system, you may return 1 cruiser, carrier, or dreadnought you control in that system to your "
                           "reinforcements to gain a number of trade goods equal to 1 less than that unit's cost value."},
    {"Orbital Foundries", "For the purpose of scoring objectives, you may treat each of your war sun units as though it is a structure on any "
                          "planet you control."},
    // Tnelis Syndicate
    {"Plausible Deniability", "When you draw 1 or more secret objective cards, draw 1 additional secret objective card. Then, return 1 secret "
                              "objective card to the secret objective deck; shuffle that deck."},
    {"Information Brokers", "You may have 1 additional unscored secret objective."},
    {"Stealth Insertion", "If you place units onto the same planet as another player's units, your units must participate in combat during "
                          "the \"Ground Combat\" step."},
    // Monks of Kolume
    {"Starfall Gunnery", "During Movement, while you are not the active player, you may only use 1 of your unit's SPACE CANNON. During each of your actions, up to 3 of your non-fighter ships gain SPACE CANNON 8."},
    {"Deliberate Action", "You cannot redistribute command tokens during the status phase. When you pass, you may place 1 command token from your reinforcements in 1 pool on your command sheet that contains no command tokens."},
    {"Meditation", "ACTION: Spend 1 command token from your strategy pool to ready 1 of your technologies."},
    // Bentor Conglomerate
    {"Secret Maps", "At the end of your tactical actions: You may explore 1 planet in the active system that is or contains 1 of your units with "
                    "PRODUCTION and that you did not explore during that tactical action."},  // (the Bentor review: the card's wording)
    {"Ancient Blueprints", "The first time you gain a cultural, hazardous, industrial, or unknown relic fragment: Place the corresponding "
                           "\"Fragment\" token on your faction sheet."},
    {"Fortune Seekers", "Once per action, after you explore a planet or frontier token: You may gain 1 commodity."},
    // Dih-Mohn Flotilla
    {"Capital Fleet", "Destroyers count as 1/2 of a ship against your fleet pool."},
    {"Migrant Fleet", "After you explore a frontier token in a system: you may explore a planet you control that is adjacent to that system."},
    {"Flotilla", "You cannot have more infantry than non-fighter ships in the space area of a system."},
    // Florzen Profiteers
    {"Mercenaries", "At the start of a space combat: You may remove up to 2 fighters you control in a system adjacent to the active system. Then, choose 1 player participating in that combat; that player places the same number of fighters from their reinforcements in the active system."},
    {"Data Leak", "When you would gain a relic: You may draw 1 additional card; choose 1 to gain and return the rest to the relic deck. Then, shuffle the relic deck."},
    {"Shadow Markets", "When you explore a planet: You may treat that planet as if it had the same trait as another planet you control."},
    // Gledge Union
    {"Mantle Cracking", "ACTION: Place 1 \"Core\" token on a non-home planet you control other than Mecatol Rex that does not contain a Core token to gain up to 4 trade goods. (3 Core tokens in all.)"},
    {"Celestial Reclamation", "Planets that contain Core tokens have a base resource value of 2 and influence value of 0. Core tokens cannot be removed from the planet that contains them. (They are not attachments: attachments still modify the planet.)"},
    {"Deep Mining", "When you would explore a planet that contains 1 of your mechs or structures, you may instead gain 1 trade good."},
    // Augurs of Ilyxum
    {"Oracle AI", "After the speaker reveals an unrevealed public objective: Choose and look at 1 unrevealed public objective card; place that "
                  "objective card on your faction sheet. You may look at the card on your faction sheet at any time."},  // (the Augurs review: the card's wording)
    {"Limited Vision", "You may not place a Stage II objective card on your faction sheet until all Stage I objectives have been revealed."},
    {"Probability Algorithms", "When the speaker would reveal the next public objective: They instead reveal the objective card on your faction "
                               "sheet and place that objective card near the public objectives as the next public objective."},
    // Berserkers of Kjalengard
    {"Heroism", "Capture your fighters and infantry that are destroyed during combat. When you pass, for each Glory token on the game board, you may return 4 of your captured units to gain 1 command token."},
    {"Glory", "After you win a combat, you may place or move a Glory token into the active system, or spend 1 token from your strategy pool to research a unit upgrade technology of the same type as 1 of your units that participated in that combat."},
    {"Valor", "When units make a combat roll in a system that contains a Glory token, each result of 10, before applying modifiers, produces 1 additional hit."},
    // Kollecc Society
    {"Cloaked Fleets", "After you produce 1 or more ships, you may place up to 2 of those ships on your faction sheet, those ships are captured."},
    {"Shroud of Lith", "After movement, if the active system contains 1 or more of your non-fighter ships, you may place up to 2 of your captured ships and 2 of your captured ground forces from your faction sheet in that system's space area."},
    {"Treasure Hunters", "At the start of your turn, you may look at the top card of the cultural, hazardous, or industrial exploration deck. Then, look at the top card of the relic deck."},
    // Glimmer of Mortheus
    {"Facsimile", "At the start of a space combat while you are the defender, you may spend influence equal to the cost value of 1 of your opponent's "
                  "ships in the active system to place 1 unit of that type from your reinforcements in that system."},
    {"Illusory Presence", "During the agenda phase, after another player casts votes, you may exhaust up to 2 planets you control; that player casts an "
                          "additional number of votes equal to 1 more than the combined influence values of those planets."},
    // Vaden Banking Clans
    {"Fine Print", "After a player resolves the secondary ability of 1 of your strategy cards, place up to 1 of their control tokens on your faction sheet."},
    {"Collateralized Loans", "After 1 of your opponent's ships is destroyed during a round of space combat, you may remove 1 of that player's control tokens "
                             "from your faction sheet to place 1 ship of that type from your reinforcements in the active system."},
    {"Binding Debts", "Other players may place their control tokens on your faction sheet at any time. At the start of the status phase, each of "
                      "your neighbors may give you 1 trade good to remove up to 2 of their control tokens from your faction sheet."},
    // Savages of Cymiae
    {"Autonetic Memory", "When you draw 1 or more action cards: you may draw 1 less card to choose 1 card from the action card discard pile and add it to your hand, or to place 1 infantry on a planet you control."},
    {"Cybernetic Madness", "After you resolve an action card's ability text, purge that card instead of discarding it. After you add 1 or more action cards from the discard pile to your hand: discard 1 action card."},
    // Nivyn Star Kings
    {"Celestial Guides", "Your units do not roll for gravity rifts. You may ignore the movement effects of anomalies in systems that contain or are "
                         "adjacent to 1 or more of your structures."},
    {"Singularity Point", "The system that contains the \"Wound\" token is both a nebula and a gravity rift."},
    {"Voidsailors", "When you explore a frontier token, you may draw 1 additional card; choose 1 to resolve and return the rest to the frontier "
                    "exploration deck. Then, shuffle that deck."},
    // Olradin League
    {"Policies", "When you gather your starting components, place the 3 \"Policy\" cards near your faction sheet, choose which side of each card "
                 "to place face up. This faction has the abilities and effects listed on the face up side of each Policy card."},
    // Free Systems Compact
    {"Rally to the Cause", "Once per action, after you produce 1 or more ships in your home system, you may produce up to 2 ships in a system that contains a cultural, hazardous, or industrial planet and does not contain a legendary planet or other players' units."},
    {"Diplomats", "Once per action, you may exhaust 1 uncontrolled planet's planet card that is on the game board to spend its resources or influence."},
    {"Free People", "During setup, for each non-home planet other than Mecatol Rex on the game board, place that planet's planet card face up on the game board."},
    // Edyn Mandate
    {"Grace", "Once per action phase, after you resolve the primary ability of a strategy card: you may resolve the secondary ability of 1 unexhausted strategy card with a lower printed initiative number than that strategy card."},
    {"Decree", "You may prevent ships from moving through anomalies that contain your ground forces."},
    {"Radiance", "After an agenda is revealed: you may predict aloud the outcome of that agenda. If your prediction is correct, place 1 command token from another player's reinforcements in a Sigil."},
    // Nokar Sellships
    {"Hired Guns", "After a system is activated, you may choose up to 3 of your ships. Those ships are also treated as the active player's and "
                   "participate in combat as their units. At the end of this tactical action, that player may replace each of those ships in the "
                   "active system with their own of the same unit type. Then, remove the rest from the game board."},
    {"Private Fleet", "During setup, place 4 additional destroyers in your reinforcements."},
    {"Desperados", "When 1 or more of your units use PRODUCTION, 1 destroyer does not count against your PRODUCTION limit. Apply +1 to your "
                   "destroyers' move values while you are not the active player."},
    // Shipwrights of Axis
    {"Military Industrial Complex", "After you gain 1 or more commodities: You may spend a number of commodities equal to the combined cost listed on "
                                    "any number of Axis Order cards in your reinforcements to place those cards in your play area. You may not give "
                                    "your commodities to other players as part of a transaction."},  // (the Axis review: the card's wording)
    {"Arms Dealers", "When a player negotiates a transaction: They may exchange Axis Order cards in their play area as part of that transaction. "
                     "You cannot resolve the effects of Axis Order cards."},
    // Myko-Mentori
    {"Prescient Memories", "You have 4 \"Omen\" dice. At the start of the strategy phase, roll all 4 Omen dice and place them near your faction sheet."},
    {"Divination", "Before you would roll a die, you may instead return 1 Omen die near your faction sheet to your reinforcements to resolve that "
                   "roll as if it had the result of that die."},
    {"Necrophage", "Apply +1 to your commodity value for each space dock you control. After the first round of combat, gain 1 commodity or convert 1 "
                   "of your commodities to a trade good."},
    // Ghemina Raiders
    {"The Lady & The Lord", "You have 2 flagships: the Lady and the Lord - a second flagship unit with the abilities and attributes listed on the Lord flagship card. You have 2 heroes."},
    {"Rule of Two", "During a round of combat in a system that contains exactly 2 of your non-fighter ships, if those ships have the same unit type, apply +2 to the result of each of those units' combat rolls (the Lady and the Lord are both flagships). Infantry and fighters don't stop it."},
    // Veldyr Sovereignty
    {"Corporate Entity", "During setup, take the additional Veldyr faction promissory notes; you have 4 faction promissory notes. \"Branch Office\" "
                         "attachments do not count toward scoring objectives."},
    {"Holding Company", "At the start of the status phase, for each planet that has a Branch Office attachment, you may gain 1 commodity or "
                        "convert 1 of your commodities to a trade good."},
    {"Targeted Acquisition", "At the start of the status phase, you may give 1 of your faction promissory notes in your hand to 1 of your neighbors."},
    // Li-Zho Dynasty
    {"Cunning", "After you perform a tactical action in a system, you may attach 1 Trap card from your reinforcements to a planet in that "
                "system that contains 1 or more of your infantry."},
    {"Subterfuge", "Trap attachments are attached face down and stay hidden until revealed; you may look at them at any time. You don't "
                   "count Trap attachments toward objectives."},
    // Mirveda Protectorate
    {"Privileged Citizenry", "Each system can contain a number of your PDS equal to the number of command tokens in your fleet pool. For the purpose of "
                             "scoring objectives, you may treat each of your PDS units as though it is a structure on any planet you control."},
    {"Combat Drones", "Your space docks cannot produce infantry. At the start of invasion, you may replace each of your fighters in the active system "
                      "with 1 infantry unit. During invasion, your infantry in the space area of the active system do not count against your ships' "
                      "capacity."},
    // Ghoti Wayfarers
    {"Abyssal Embrace", "When you create the game board, place the Ghoti Space tile where your home system would normally be placed. The Ghoti Space system is not a home system."},
    {"Mobile Command", "The system that contains your flagship is your home system. Your flagship cannot be captured and you cannot score public objectives if your flagship is not on the game board."},
    // Blue Reverie (0.83.0, the cards' wording): the Atokera Legacy
    {"Synthesis", "Readied planets you control have PLANETARY SHIELD and PRODUCTION 0 as if they were units. Apply +1 to the PRODUCTION value of each of your units with PRODUCTION."},
    {"Living Metals", "Agenda outcomes and action cards that have component actions cannot remove or destroy your units, or place other player's units on planets you control."},
    // the Belkosea Allied States (Blue Reverie, 0.84.0)
    {"Classified Developments", "ACTION: Spend a combined total of 5 resources or influence to place 1 superweapon on a planet you control, and gain its superweapon card."},
    {"Arrogance", "Superweapons you control are structures and cannot be destroyed by abilities. Each planet can contain a maximum of 1 superweapon. When a superweapon is destroyed, instead purge it and its superweapon card."},
    // the Pharad'n Order (Blue Reverie, 0.85.0)
    {"Ancient Empire", "During setup, place 1 tomb token on each of up to 14 planets. When you gain control of a planet that contains a tomb token, purge that token to capture 2 infantry from the supply."},
    {"Dark Purpose", "When you score an objective: Gain 1 command token."},
    {"Mark of Pharad'n", "Abilities that would allow you to place infantry on the game board instead allow you to produce those units. When you produce infantry, you must return 1 of your captured infantry to produce each of those units, instead of spending resources."},
    {"Spawning Grounds", "During setup, gain and ready the Ghoti planet card (3/3) and its planet ability card; you cannot lose those cards. The ability: When you move 1 or more ships into a system that contains no planets, gain 1 commodity. You may score objectives if your home system contains no planets."},
};

// Leaders, keyed by leader key: what they do.
const KT kLeaderText[] = {
    // Council Keleres
    {"keleresagent", "At any time: You may exhaust this card to allow any player to spend commodities as if they were trade goods. (For one payment.)"},  // (the Keleres review: the card's wording)
    {"kelerescommander", "After you perform a component action: You may perform an additional action."},  // (the Keleres review: the card's wording)
    {"keleresheroharka", "ACTION: Reveal cards from the action card deck until you reveal 3 action cards that have component actions. Draw those cards and shuffle the rest back into the action card deck. Then, purge this card."},  // (the Keleres review: the card's wording)
    {"keleresheroodlynn", "After an agenda is revealed: You may cast up to 6 additional votes on this agenda. Predict aloud an outcome of this agenda. For each player that abstains or votes for another outcome, gain 1 trade good and 1 command token. Then, purge this card."},  // (the Keleres review: the card's wording)
    {"keleresherokuuasi", "At the start of a round of space combat in a system that contains a planet you control: Place your flagship and up to a total of 2 cruisers and/or destroyers from your reinforcements in the active system. Then, purge this card."},  // (the Keleres review: the card's wording)
    {"solagent", "At the start of a ground combat round: You may exhaust this card to choose 1 ground force in the active system: that ground force rolls 1 additional die during this combat round."},
    {"solcommander", "At the start of a ground combat on a planet you control: You may place 1 infantry from your reinforcements on that planet."},
    {"solhero", "ACTION: Remove each of your command tokens from the game board and return them to your reinforcements. Then, purge this card."},
    {"letnevagent", "At the start of a space combat round: You may exhaust this card to choose 1 ship in the active system: that ship rolls 1 additional die during this combat round."},
    {"letnevcommander", "After 1 of your units uses SUSTAIN DAMAGE: You may gain 1 trade good."},
    {"letnevhero", "ACTION: Place this card near the game board; the number of non-fighter ships you can have in systems is not limited by laws or by the number of command tokens in your fleet pool during this game round. At the end of that game round, purge this card."},
    {"hacanagent", "During the action phase: You may exhaust this card to gain 2 commodities or replenish another player's commodities."},
    {"hacancommander", "When you cast votes: You may spend any number of trade goods: cast 2 additional votes for each trade good spent."},
    {"hacanhero", "When 1 or more of your units use PRODUCTION: You may reduce the cost of each of your units to 0 during this use of PRODUCTION. If you do, purge this card."},
    {"jolnaragent", "When a player spends resources to research: You may exhaust this card to allow that player to remove any number "
                    "of their infantry from the game board. For each unit removed, reduce the resources spent by 1."},
    {"jolnarcommander", "After you roll dice for a unit ability: You may reroll any of those dice."},
    {"jolnarhero", "ACTION: For each non-unit upgrade technology you own, you may replace that technology with any technology of "
                   "the same color from the deck. Then, purge this card."},
    {"sardakkagent", "At the end of a player's tactical action: You may exhaust this card; if you do, that player may place 2 infantry from their reinforcements on a planet they control in the active system."},
    {"sardakkcommander", "During the \"Commit Ground Forces\" step: You can commit (move) up to 1 ground force from each planet in the active system and each planet in adjacent systems that do not contain 1 of your command tokens."},
    {"sardakkhero", "After you move ships into the active system: You may skip directly to the \"Commit Ground Forces\" step. If you do, after you commit ground forces to land on planets, purge this card and return each of your ships in the active system to your reinforcements."},
    // (the Xxcha review, 2026-11-13: the cards' wording - the hero's Prophecy of Kings printing checked by the owner)
    {"xxchaagent", "ACTION: Exhaust this card to ready any planet; if that planet is in a system that is adjacent to a planet you control, "
                   "you may remove 1 infantry from that planet and return it to its reinforcements."},
    {"xxchacommander", "When you vote: Each planet you exhaust to cast votes provides 1 additional vote. Game effects cannot prevent you "
                       "from voting on an agenda."},
    {"xxchahero", "ACTION: Look at the top 5 cards of the agenda deck. Choose 2 to reveal, and resolve each as if you had cast 1 vote for "
                  "an outcome of your choice; discard the rest. Other players cannot resolve abilities during this action. Then, purge "
                  "this card."},
    {"arborecagent", "ACTION: Exhaust this card and choose a player's non-fighter ship: that player may replace that ship with one from their reinforcements that costs up to 2 more than the replaced ship."},
    {"arboreccommander", "After another player activates a system that contains 1 or more of your units that have PRODUCTION: You may produce 1 unit in that system."},
    {"arborechero", "ACTION: Produce any number of units in any number of systems that contain 1 or more of your ground forces. Then, purge this card."},
    {"l1z1xagent", "After a player activates a system: You may exhaust this card to allow that player to replace 1 of their infantry in the active system with 1 mech from their reinforcements."},
    {"l1z1xcommander", "Units that have PLANETARY SHIELD do not prevent you from using BOMBARDMENT."},
    {"l1z1xhero", "ACTION: Choose 1 system that does not contain other players' ships; you may move your flagship and any number of your dreadnoughts from other systems into the chosen system. Then, purge this card."},
    // (the Mentak review, 2026-11-13: the cards' wording)
    {"mentakagent", "After the Pillage faction ability is used against another player: You may exhaust this card; if you do, you and "
                    "that player each draw 1 action card."},
    {"mentakcommander", "After you win a space combat: You may force your opponent to give you 1 promissory note from their hand."},
    {"mentakhero", "At the start of a space combat that you are participating in: You may purge this card; if you do, for each other "
                   "player's ship that is destroyed during this combat, place 1 ship of that type from your reinforcements in the active "
                   "system."},
    {"naaluagent", "After an agenda is revealed: You may exhaust this card to look at the top card of the agenda deck. Then, you may "
                   "show that card to 1 other player."},  // (the Naalu review: the original card, owner 2026-11-13)
    {"naalucommander", "At any time: You may look at your neighbors' hands of promissory notes and the top and bottom card of the "
                       "agenda deck."},
    {"naaluhero", "At the end of the status phase: You may force each other player to give you 1 promissory note from their hand. "
                  "If you do, purge this card."},
    {"yinagent", "After a player's destroyer or cruiser is destroyed: You may exhaust this card; if you do, that player may place up to 2 fighters from their reinforcements in that unit's system."},
    {"yincommander", "This card satisfies a green technology prerequisite."},
    {"yinhero", "ACTION: Commit up to 3 infantry from your reinforcements to any non-home planets and resolve invasions on those planets; players cannot use SPACE CANNON against those units. Then, purge this card."},
    {"yssarilagent", "At any time: This card has the text ability of each other player's agent, even if that agent is exhausted."},
    {"yssarilcommander", "After another player activates a system that contains your units: You may look at that player's action cards, promissory notes, or secret objectives."},
    {"yssarilhero", "ACTION: Each other player shows you 1 action card from their hand. For each player, you may either take that card or force that player to discard 3 random action cards from their hand. Then, purge this card."},
    {"ghostagent", "After a player activates a system that contains a non-delta wormhole: You may exhaust this card; if you do, that system is adjacent to all other systems that contain a wormhole during this tactical action."},
    {"ghostcommander", "After your ships move: For each ship that has a capacity value and moved through 1 or more wormholes, you may place 1 fighter from your reinforcements with that ship if you have unused capacity in the active system."},
    {"ghosthero", "ACTION: Swap the positions of any 2 non-Fracture systems that contain wormholes or your units, other than the Creuss system, Ahk Creuxx system, or the Wormhole Nexus. Then, purge this card."},
    {"muaatagent", "ACTION: Exhaust this card to choose a player; that player may produce up to 2 units that each have a cost of 4 or less in a system that contains one of their war suns or their flagship."},
    {"muaatcommander", "After you spend a token from your strategy pool: You may gain 1 trade good."},
    {"muaathero", "After you move a war sun into a non-home system other than Mecatol Rex: You may destroy all other players' units in that system and replace that system tile with the Muaat supernova tile. If you do, purge this card and each planet card that corresponds to the replaced system tile."},
    {"saaragent", "When a player activates a system: You may exhaust this card to increase the move value of 1 of that player's ships to "
                  "match the move value of the ship on the game board that has the highest move value."},
    {"saarcommander", "When you produce fighters or infantry: You may place each of those units at any of your space docks that are not "
                      "blockaded."},
    {"saarhero", "ACTION: Choose 1 system that is adjacent to 1 of your space docks. Destroy all other players' infantry and fighters in "
                 "that system. Then, purge this card."},
    {"winnuagent", "When 1 or more of a player's units use PRODUCTION: You may exhaust this card to reduce the combined cost of the produced units by 2."},
    {"winnucommander", "During combat: Apply +2 to the result of each of your unit's combat rolls in the Mecatol Rex system, your home system, and each system that contains a legendary planet."},
    {"winnuhero", "ACTION: Perform the primary ability of any strategy card. Then, choose any number of other players. Those players may perform the secondary ability of that strategy card. Then, purge this card."},
    {"nekroagent", "During the action phase: You may exhaust this card to choose a player: that player may discard 1 action card or "
                   "spend 1 command token from their command sheet to gain 2 trade goods."},  // (the Nekro review: the cards)
    {"nekrocommander", "After you gain a technology: You may draw 1 action card."},
    {"nekrohero", "ACTION: Choose a planet that has a technology specialty in a system that contains your units. Destroy any other "
                  "player's units on that planet. Gain trade goods equal to the planet's combined resource and influence values and gain "
                  "1 technology that matches the specialty of that planet. Then, purge this card."},
    {"empyreanagent", "After a player moves ships into a system that does not contain any planets: You may exhaust this card; that player gains 1 command token."},
    {"empyreancommander", "After another player moves ships into a system that contains 1 of your command tokens: You may return that token to your reinforcements."},
    {"empyreanhero", "ACTION: Place 1 frontier token in each system that does not contain any planets and does not already have a frontier token. Then, explore each frontier token that is in a system that contains 1 or more of your ships. Then, purge this card."},
    // (the Argent review, 2026-11-13: the cards' wording)
    {"argentagent", "When a player produces ground forces in a system: You may exhaust this card; that player may place those units on any "
                    "planets they control in that system and any adjacent systems."},
    {"argentcommander", "When 1 or more of your units make a roll for a unit ability: You may choose 1 of those units to roll 1 additional die."},
    {"argenthero", "ACTION: Move any number of your ships from any systems to any number of other systems that contain 1 of your command "
                   "tokens and no other players' ships. Then, purge this card."},
    {"mahactagent", "When you would spend a command token during the secondary ability of a strategic action: You may exhaust this card to remove 1 of the active player's command tokens from the board and use it instead."},
    {"mahactcommander", "During your tactical actions, you can activate systems that contain your command tokens. If you do, return both command tokens to your reinforcements and end your turn."},
    {"mahacthero", "ACTION: Move all units in the space area of any system to an adjacent system that contains a different player's ships. Space combat is resolved in that system; neither player can retreat or resolve abilities that would move their ships. Then, purge this card."},
    {"naazagent", "At the end of a player's turn: You may exhaust this card to allow that player to explore 1 of their planets."},
    {"naazcommander", "After you gain control of a planet that was controlled by another player: You may explore that planet."},
    {"naazhero", "ACTION: Gain 1 relic and perform the secondary ability of up to 2 readied or unchosen strategy cards; during this "
                 "action, spend command tokens from your reinforcements instead of your strategy pool. Then, purge this card."},
    {"nomadagentmercer", "At the end of a player's turn: You may exhaust this card to allow that player to remove up to 2 of their ground forces from the game board and place them on planets they control in the active system."},
    {"nomadagentthundarian", "After the \"Roll Dice\" step of combat: You may exhaust this card. If you do, hits are not assigned to either player's units. Return to the start of this combat round's \"Roll Dice\" step."},
    {"nomadagentartuno", "When you gain trade goods from the supply: You may exhaust this card to place an equal number of trade goods on this card. When this card readies, gain the trade goods on this card."},
    {"nomadcommander", "You can produce your flagship without spending resources."},
    {"nomadhero", "ACTION: Place this card near the game board; your flagship and units it transports can move out of systems that contain your command tokens during this game round. At the end of that game round, purge this card."},
    {"titansagent", "Before a hit would be assigned: You may exhaust this card to cancel that hit."},  // (the errata - owner 2026-11-13)
    {"titanscommander", "When 1 or more of your units use PRODUCTION: You may gain 1 trade good."},
    {"titanshero", "ACTION: Ready Elysium and attach this card to it. Its resource and influence values are each increased by 3, and it "
                   "gains the SPACE CANNON 5 (x3) ability as if it were a unit."},
    {"cabalagent", "After another player replenishes commodities: You may exhaust this card to convert their commodities to trade goods and capture 1 unit from their reinforcements that has a cost equal to or lower than their commodity value."},
    {"cabalcommander", "When you produce fighter or infantry units: Up to 2 of those units do not count against your PRODUCTION limit."},
    {"cabalhero", "ACTION: Each other player rolls a die for each of their non-fighter ships that are in or adjacent to a system that contains a dimensional tear; on a 1-3, capture that unit. If this causes a player's ground forces or fighters to be removed, also capture those units. Then, purge this card."},
    // Thunder's Edge factions (0.35.0)
    {"bastionagent", "When a player's unit is destroyed: You may exhaust this card to galvanize another of that player's units in "
                     "the destroyed unit's system."},
    {"bastioncommander", "Your action cards cannot be canceled by \"Sabotage\" action cards. The Nekro Virus cannot place assimilator "
                         "tokens on your components."},
    {"bastionhero", "When one of your galvanized units is destroyed: You may purge this card to roll 1 die for each unit in its system "
                    "that belongs to another player; if the result is equal to or greater than the galvanized unit's combat value, "
                    "destroy that unit."},
    {"ralnelagent", "ACTION: Exhaust this card and draw 2 action cards; give 1 of those cards to another player."},  // (the Ral Nel review: the card's wording)
    {"ralnelcommander", "When you declare a retreat: Immediately retreat up to 2 of your ships from the active system to an adjacent system that does not contain another player's ships. Place a command token from your reinforcements into that system."},  // (the Ral Nel review: the card's wording)
    {"ralnelhero", "After the last player passes: You may choose to no longer be passed; if you do, gain 2 command tokens, draw 1 action card, and purge this card."},  // (the Ral Nel review: the card's wording)
    {"deepwroughtagent", "When another player researches a technology: You may exhaust this card to allow that player to ignore 1 prerequisite; if they do, you may place 1 infantry from your reinforcements into coexistence on a non-home planet they control."},  // (the Deepwrought review: the card's wording)
    {"deepwroughtcommander", "When another player spends resources to research a technology: That player may reduce the cost by 1; if they do, gain 1 commodity or convert 1 of your commodities to a trade good."},  // (the Deepwrought review: the card's wording)
    {"deepwroughthero", "ACTION: Purge this card and a non-unit upgrade technology you own or from your deck; then, purge all cards with the same name owned by other players and in other players' decks. Then, each player that purged a technology they owned researches another technology."},  // (the Deepwrought review: the card's wording)
    {"crimsonagent", "Action: exhaust to choose a player: they may swap the places of 2 of their ships in any systems, and may transport units with them when they swap."},
    {"crimsoncommander", "At the end of a combat between any players: gain 1 commodity, or turn 1 of your commodities into a trade good."},
    {"crimsonhero", "When you produce ships: you may put any of them on this card. At the start of a space combat: purge it to bring every ship on it into the active system."},
    {"firmamentagent", "When a player moves ships: You may exhaust this card; if you do, SPACE CANNON cannot be used against those ships. If they are not transporting units, they can also move through other players' ships."},
    {"firmamentcommander", "You can treat planets in systems that contain your ships as if they were controlled by you for the purpose of scoring secret objectives."},
    {"firmamenthero", "ACTION: Place 1 of your plot cards in play with any other player's control token on it. Then, you may place any player's control token on 1 of your in-play plot cards; one plot cannot have two of the same player's tokens. Then, purge this card."},
    {"obsidianagent", "When a player's ship is destroyed during any combat: You may exhaust this card; if you do, that player's opponent must destroy 1 of their ships of the same type in the active system."},
    {"obsidiancommander", "Apply +1 to the result of each of your units' combat rolls in The Fracture."},
    {"obsidianhero", "ACTION: Ready all of your planets. Then, purge this card."},
    // Discordant Stars (0.37.0)
    {"kortaliagent", "When a player wins a ground combat: You may exhaust this card to allow that player to take 1 relic fragment their opponent owns, at random, if able."},
    {"kortalicommander", "During the first round of a combat, you may cancel 1 hit produced against your units."},
    {"kortalihero", "When you win a combat: You may purge this card to choose and take up to 1 of your opponent's relics. Then, for each planet you control that is a legendary planet or has a technology specialty, gain 1 command token."},
    {"vaylerianagent", "After a player gains control of a planet, you may exhaust this card to allow that player to draw 1 action card."},
    {"vayleriancommander", "After you activate a system: Choose 1 ship you control with a capacity value, apply +2 to that value during this tactical action."},
    {"vaylerianhero", "After you activate a system: Remove 1 of your command tokens from the board and gain 1 command token. Until the end of this tactical "
                   "action, apply +1 to the move value of each of your ships and the result of each of your ships' combat rolls. At the end "
                   "of this turn, purge this card."},
    {"lanefiragent", "When a player explores, you may exhaust this card to allow that player to discard 1 exploration card they drew and draw another."},
    {"lanefircommander", "After you explore a planet: You may place 1 infantry from your reinforcements on that planet."},
    {"lanefirhero", "ACTION: Draw and reveal up to 3 relics. For each relic that started this action in a player's play area, gain 1 command token; you may swap that relic for 1 of the revealed relics or a relic in a player's play area. Then, shuffle the remaining relics into the relic deck and purge this card."},
    {"kyroagent", "After a player loses or draws a ground combat: You may exhaust this card to allow that player to replenish their commodities; place a number of infantry equal to 1 less than their commodity value on 1 planet you control."},
    {"kyrocommander", "When you cast votes: You may remove up to 4 of your infantry from the game board to cast 1 additional vote for each unit removed."},
    {"kyrohero", "At the start of the status phase: You may attach this card to 1 of your strategy cards. Its initiative value is 9. Non-Kyro players may only resolve 1 clause of this strategy card's primary ability. The Kyro player gains any trade goods placed on this card."},
    {"cheiranagent", "At the end of a player's turn: Exhaust this card to allow that player to remove 1 of their command tokens from the game board and place that token in an adjacent system that contains 1 or more of their structures and none of their command tokens."},
    {"cheirancommander", "While you are the defender: After another player produces 1 or more hits against your units during ground combat, you may spend 1 commodity or 1 trade good to cancel 1 of those hits."},
    {"cheiranhero", "ACTION: Gather units from your reinforcements up to equal to your starting units. Place these units, in any combination, in the space area of any systems that contain 1 of your structures and no other players' ships, or on planets you control. Then, purge this card."},
    {"celdauriagent", "At the end of a player's tactical action: You may exhaust this card to allow that player to spend 2 trade goods or 2 commodities to place 1 space dock from their reinforcements on a planet they control in the active system."},
    {"celdauricommander", "After you activate a system that contains 1 or more of your space docks, gain 1 commodity. Your ships may treat systems that contain 1 or more of your space docks as adjacent to each other."},
    {"celdaurihero", "After a player activates a system other than Mecatol Rex: You may place 1 space dock on a planet you control in the active system, convert each of your commodities to a trade good, and use the PRODUCTION ability of each of your units in the active system. Then, purge this card."},
    // Discordant Stars batch 2 (0.38.0)
    {"khraskagent", "When a player would spend influence: You may exhaust this card to allow that player to spend the resources of 1 non-home planet they control as influence, in addition to its influence."},
    {"khraskcommander", "At the start of invasion in a system that contains 1 or more planets: You may place 1 infantry unit from your reinforcements in that system's space area if you have unused capacity in that system."},
    {"khraskhero", "ACTION: Purge this card to choose up to 4 planets; exhaust or ready each of those planets."},
    {"zealotsagent", "After a player researches a technology: You may exhaust this card to allow that player to produce 1 ship in their home "
                     "system or a system that contains a planet they control that has a technology specialty."},
    {"zealotscommander", "When researching a technology, you may use the technology specialty of 1 planet you control to ignore any 1 "
                         "prerequisite on the technology card you are researching."},
    {"zealotshero", "ACTION: Place this card and up to 3 non-faction technologies near the game board, you gain those technologies. At the "
                    "start of the next strategy phase, purge all but 1 of those technologies and this card."},
    {"zelianagent", "At the start of invasion: You may exhaust this card to allow the active player to replace 1 of their infantry in the "
                    "space area of the active system with 1 mech from their reinforcements."},
    {"zeliancommander", "Each of your Dreadnoughts and War Suns without ANTI-FIGHTER BARRAGE gains ANTI-FIGHTER BARRAGE 5."},
    {"zelianhero", "After you move a unit with BOMBARDMENT into a non-home system other than Mecatol Rex: You may gain trade goods equal to the "
                   "total combined resource values of planets in that system. If you do, destroy all units on planets in that system and replace "
                   "that system tile with the Zelian asteroid tile. Then, purge this card and each planet card that corresponds to the replaced "
                   "system tile."},
    {"rohdhnaagent", "After 1 or more of a player's units use PRODUCTION: You may exhaust this card to allow that player to gain 1 command token."},
    {"rohdhnacommander", "When you spend a command token to resolve the secondary ability of the \"Construction\" strategy card, you may resolve "
                         "the primary ability instead."},
    {"rohdhnahero", "After you move 1 or more of your ships into the active system: You may purge this card to produce ships using the "
                    "PRODUCTION abilities of any units in the active system as if they were your own, regardless of whether that system contains "
                    "another player's ships."},
    {"tnelisagent", "At the start of invasion: You may exhaust this card to choose 1 ship in the active system. During this invasion, that "
                    "ship may use its ANTI-FIGHTER BARRAGE as if it were BOMBARDMENT."},
    {"tneliscommander", "After you activate a system, you may produce and assign 1 hit to a non-fighter ship you control in a system to apply "
                        "+1 to the move value of 1 of your ships in that system until the end of this tactical action."},
    {"tnelishero", "After another player scores a secret objective: You may attach, or remove and attach, this card to that secret objective. "
                   "You treat the attached secret objective as if it were a public objective. After you score the attached objective, purge "
                   "this card."},
    {"kolumeagent", "ACTION: Exhaust this card to allow a player to redistribute 1 of their command tokens on their command sheet."},
    {"kolumecommander", "Apply +1 to each of your unit's ability rolls."},
    {"kolumehero", "ACTION: Place this card near the game board; during this game round, after you perform a component action, you may use 1 of your unit's SPACE CANNON against ships in or adjacent to that unit's system, without rolling additional dice. At the end of this game round, purge this card."},
    {"bentoragent", "ACTION: Exhaust this card and choose 1 player. For each Fragment token on your faction sheet, that player gains 1 commodity. "
                    "For each commodity they would gain in excess of their commodity value, they gain 1 trade good instead."},  // (the Bentor review: the card's wording)
    {"bentorcommander", "Your commodity value is increased by 1."},
    {"bentorhero", "ACTION: Purge this card to explore each planet you control in any order."},
    {"dihmohnagent", "When a player commits 1 or more units to a planet during invasion: you may exhaust this card to allow that player to place 1 infantry from their reinforcements on that planet."},
    {"dihmohncommander", "This card satisfies a red technology prerequisite. At the start of a space combat in a system that contains 3 or more different types of your non-fighter ships, produce 1 hit against your opponent's ships."},
    {"dihmohnhero", "After the \"Roll Dice\" step of space combat: you may purge this card. If you do, all ships in the active system become damaged. During this round of space combat your ships cannot be destroyed."},
    {"florzenagent", "At the start of a player's turn: You may exhaust this card to allow that player to remove an attachment from a non-home planet they control and attach it to a non-home planet they control other than Mecatol Rex adjacent to that planet's system."},
    {"florzencommander", "After the second agenda is revealed during the agenda phase: You may ready 1 planet you control. Explore that planet, if able."},
    {"florzenhero", "Action: Place 2 fighters from your reinforcements in each system that contains 1 or more planets you control and no other player's ships. Then, you may remove 1 attachment from any planet in a non-home system that contains 1 or more of your ships and attach it to 1 planet in your home system. Then, purge this card."},
    {"gledgeagent", "When 1 or more of a player's units use PRODUCTION: You may exhaust this card to apply +3 to the total PRODUCTION value of that player's units."},
    {"gledgecommander", "When you use the PRODUCTION of 1 or more of your units, you may increase the total PRODUCTION value of those units by 1 for each space dock you control."},
    {"gledgehero", "ACTION: Reveal cards from any non-frontier exploration decks until you reveal up to 3 attachments, if able; attach those cards to any planets you control of the corresponding planet type, and discard the rest. Then, purge this card."},
    {"augersagent", "After a player explores a legendary planet or a planet that has a technology specialty: You may exhaust this card to "
                    "allow that player to gain 2 trade goods."},  // (the Augurs review: the card's wording)
    {"augerscommander", "This card satisfies a yellow technology prerequisite. When you cast at least 1 vote, cast 1 additional vote for every 2 "
                        "technologies you own."},
    {"augershero", "ACTION: Draw 3 objective cards from 1 of the public objective decks. Choose up to 1 of those cards to replace an objective "
                   "card on a player's faction sheet, and return the others to the bottom of their corresponding decks. Then, purge this card. "
                   "(The game draws from the deck of the stage revealed next.)"},
    {"kjalengardagent", "At the start of a combat: Exhaust this card to move a Glory token to the active system, if able. Then, the active player may gain a number of commodities equal to the number of neighbors they have."},
    {"kjalengardcommander", "When researching a unit upgrade technology, each of your unit upgrade technologies may satisfy 1 prerequisite it shares with the technology you are researching."},
    {"kjalengardhero", "ACTION: For each system that contains a Glory token, remove up to 1 command token in or adjacent to that system from the game board and gain 1 command token, if able. Then, purge this card."},
    {"kolleccagent", "At the start of a player's turn: You may exhaust this card to allow that player to reveal the top 2 cards of an exploration deck that matches a planet they control; they gain any relic fragments that they revealed and discard the rest."},
    {"kollecccommander", "This card satisfies a blue technology prerequisite. When you retreat, if the active system does not contain 1 of your command tokens, you do not place a command token in the system your ships retreated to."},
    {"kollecchero", "ACTION: Draw a number of relic cards up to an amount equal to the number of players in the game, choose 1 to gain, and return the rest to the relic deck. Then, shuffle that deck and purge this card."},
    {"mortheusagent", "When a player produces 1 or more units in a system: you may exhaust this card to allow that player to place 1 of the produced "
                      "units in the space area of an adjacent system that does not contain another player's ships."},
    {"mortheuscommander", "At the start of a space combat while you are not the active player: choose 1 of your non-fighter ships in the active "
                          "system to gain SUSTAIN DAMAGE until the end of combat."},
    {"mortheushero", "Action: purge this card to choose 1 ship you control in each system; for each of those units, you may place 1 unit of that "
                     "unit type from your reinforcements in the space area of that unit's system, if able."},
    {"vadenagent", "At the start of a player's turn: You may exhaust this card to allow that player to choose 1 planet they control; that player "
                   "gains a number of commodities equal to that planet's influence value."},
    {"vadencommander", "At the start of the status phase: Gain 1 trade good for each secret objective you have scored and 1 commodity for each "
                       "public objective you have scored."},
    {"vadenhero", "ACTION: For each control token that belongs to another player on your faction sheet, you may return that token. If you do, "
                  "that player must give you 1 of their trade goods, 2 of their commodities, or 1 promissory note from their hand, if able. "
                  "Then, purge this card."},
    {"cymiaeagent", "When a player discards or purges an action card: you may exhaust this card to allow that player to draw 1 action card."},
    {"cymiaecommander", "After you gain control of a planet during a tactical action: you may discard 1 action card to place or move 1 mech onto that planet."},
    {"cymiaehero", "Action: draw and reveal a number of action cards from the action card deck equal to 1 more than the number of players in the game; give 1 to each other player in the game and keep the rest. Each other player must discard 1 action card. Then, purge this card."},
    {"nivynagent", "When a player would use 1 unit's non-PRODUCTION unit ability in a system that contains or is adjacent to an anomaly, you may "
                   "exhaust this card to prevent that unit from using that unit ability."},
    {"freesystemsagent", "While ready, this card has the text ability of each legendary planet ability card any player controls, even if that card is exhausted. You may allow another player to use this card's ability."},
    {"freesystemscommander", "After you gain control of a non-home planet during a tactical action: If you have 1 or more ships in the active system, you may produce 1 ship in that system."},
    {"freesystemshero", "Action: Ready a non-home planet other than Mecatol Rex that you control, remove all units on that planet and attach this card to it. Units cannot be committed to, produced on, or placed on this planet."},
    {"edynagent", "After a player passes: you may exhaust this card to choose 1 player. That player may perform up to 1 action. Then, look at the top card of the agenda deck; you may discard that agenda card."},
    {"edyncommander", "When any game effect would allow you to score a public objective: you may instead draw 1 secret objective."},
    {"edynhero", "Action: for each Sigil on the game board, draw 1 agenda. Reveal and resolve each agenda in any order as if you had cast 1 vote for an outcome of your choice. Other players cannot resolve abilities during this action. Then, purge this card."},
    {"nokaragent", "After movement: You may exhaust this card to allow 1 player with 1 or more ships in the active system to place 1 destroyer "
                   "from their reinforcements in that system. Then, you may resolve 1 transaction with that player."},
    {"nokarcommander", "Before you assign hits to your ships during a space combat, if you announced a retreat, cancel up to 2 hits. During "
                       "combat, you may treat the active system as adjacent to each system adjacent to your home system."},
    {"nokarhero", "ACTION: Place this card near the game board, until the end of this game round, each of your planets gains the PRODUCTION 3 "
                  "ability as if it were a unit. At the end of this game round, purge this card."},
    {"axisagent", "ACTION: Exhaust this card to choose 1 player: that player may place 1 cruiser or 1 destroyer from their reinforcements in a "
                  "system that contains their ships. Then, if you chose another player, gain 2 commodities."},  // (the Axis review: the card's wording)
    {"axiscommander", "After a player resolves an Axis Order card: You may spend 6 resources to gain the corresponding unit's unit upgrade "
                      "technology."},
    {"axishero", "ACTION: Purge this card and give 1 or more Axis Order cards in your play area to 1 or more other players: for each Axis Order "
                 "card you give to another player, you may force that player to give you 1 promissory note from their hand."},
    {"mykomentoriagent", "Before a player rolls a die, you may instead exhaust this card and choose 1 Omen die near the Myko-Mentori player's faction "
                         "sheet; resolve that die roll as if it had the result of that Omen die."},
    {"mykomentoricommander", "After another player produces 1 or more hits against your units during space combat, you may spend 1 commodity or 1 "
                             "trade good to cancel 1 of those hits."},
    {"mykomentorihero", "When another player's hero would be purged, instead attach it to this card. You may resolve this card as if it instead had "
                        "the text of any single hero attached to this card. When this card is purged, purge each of its attachments as well."},
    {"gheminaagent", "When the active player produces 1 or more hits during a round of combat: You may exhaust this card to allow that player to produce 1 additional hit."},
    {"gheminacommander", "After you win a combat in a system that contains no planets or on a planet that contains a structure, you may gain 1 trade good."},
    {"gheminaherolord", "ACTION: Purge this card to choose 1 non-home planet other than Mecatol Rex that does not contain another player's units and is adjacent to a system that contains 1 of your flagships (a planet is adjacent to its own system); gain control of that planet. Then, ready that planet."},
    {"gheminaherolady", "ACTION: Purge this card to destroy all units on a non-home planet other than Mecatol Rex that is adjacent to a system that contains 1 of your flagships (a planet is adjacent to its own system)."},
    {"veldyragent", "When a player spends resources to research: You may exhaust this card to allow that player to spend the influence of 1 planet "
                    "they control as resources."},
    {"veldyrcommander", "When you research a technology that is owned by a player who controls a planet with a Branch Office attachment, you may "
                        "ignore 1 prerequisite on that technology."},
    {"veldyrhero", "ACTION: For each planet that has a Branch Office attachment, you may gain 1 unit upgrade technology of the same unit type as 1 "
                   "owned by the player that controls that planet. Then, purge this card."},
    {"lizhoagent", "At the end of a player's turn: you may exhaust this card to let that player place 2 fighters from their reinforcements "
                   "in the space area of a system that contains 1 or more of their ships."},
    {"lizhocommander", "During a round of combat in which no more than 1 of your non-fighter ships or 1 of your infantry is fighting: choose 1 "
                       "of your units in that combat - it rolls 1 additional die."},
    {"lizhohero", "Action: attach, or remove and attach, each Trap to a planet on the game board; place a total of up to 12 fighters in "
                  "systems that contain 1 or more planets with Traps and no other player's ships. Then purge this card."},
    {"mirvedaagent", "After a player gains a unit upgrade technology: you may exhaust this card to allow that player to spend 1 command token from "
                     "their strategy pool to research 1 technology of the same color as any 1 of that unit upgrade's prerequisites."},
    {"mirvedacommander", "During movement, for each infantry you transport, you may replace that unit with 1 fighter. You may use the SPACE CANNON "
                         "of 1 of your units against units that are adjacent to that unit's system."},
    {"mirvedahero", "Action: place up to 3 PDS from your reinforcements on planets you control and choose 1 system; your PDS may use their SPACE "
                    "CANNON on ships in that system, and BOMBARDMENT against units on 1 planet in that system. Then purge this card."},
    {"ghotiagent", "When a player produces 1 or more units: You may exhaust this card to allow that player to either produce 2 additional units or gain 1 trade good."},
    {"ghoticommander", "After you activate a system: You may treat systems that contain no planets as adjacent to each other during this tactical action."},
    {"ghotihero", "ACTION: For each system that contains 1 or more of your ships, you may replace 1 of your non-fighter ships in that system with a ship from your reinforcements that has a cost value of up to 2 greater than the cost value of that ship. Then, purge this card."},
    // Blue Reverie (0.83.0, the cards' wording): the Atokera Legacy
    {"atokeraagent", "When a player exhausts a planet during the agenda phase: Exhaust this card to allow that player to treat the combined value of that planet's resources and influence as if it were resources or influence. Then, ready that planet."},
    {"atokeracommander", "Once per agenda phase, after an agenda is revealed: You may predict aloud an outcome of that agenda. If you do not vote on that agenda, and your prediction is correct, gain 1 command token."},
    {"belkoseaagent", "After a system is activated: You may exhaust this card to choose 1 system; until the end of movement, other players' ships in that system do not affect ships' movement."},
    {"belkoseacommander", "When one of your units would make a combat roll: You may use that unit's ANTI-FIGHTER BARRAGE instead. You cannot use this ability during the first round of space combat."},
    {"belkoseahero", "Ace of Aces - Inspirational Pilot. At the start of combat: You may exhaust this card; if you do, during this combat, hits produced by your fighters' ANTI-FIGHTER BARRAGE are instead produced against all ships (not just fighters). After you lose a combat, ready this card."},
    {"pharadnagent", "At any time: Exhaust this card to allow a player to destroy all their infantry on 1 planet they control. For each infantry destroyed, that player gains 1 commodity. Then, that player converts all of their commodities to trade goods."},
    {"pharadncommander", "At the start of ground combat on a planet you control: You may destroy 1 infantry on that planet."},
    {"pharadnhero", "Black Star - Apocalypse. After a player commits ground forces to a planet that contains 1 of your structures: You may purge this card to destroy all infantry on that planet, or commit any number of your captured infantry to that planet as if they were your own."},
    {"atokerahero", "Fallen Sky - Judgment. At the start of invasion: You may purge this card to commit any of your ships in the active system to planets as if they were ground forces; those units participate in combat. At the end of this tactical action, return those ships to the space area of the active system."},
    {"nivyncommander", "When 1 or more of your damaged units make a combat roll, up to 2 of those units may roll an additional combat die."},
    {"nivynhero", "ACTION: Each unit on the game board with SUSTAIN DAMAGE, other than your mechs, becomes damaged, if able. Place this card near the "
                  "game board; you may treat each unit you control as adjacent to the system that contains the Wound token until the end of this "
                  "game round. At the end of this game round, purge this card."},
    {"olradinagent", "After a player exhausts a cultural, hazardous, or industrial planet they control, you may exhaust this card to allow that "
                     "player to ready a planet they control of a different trait than that planet."},
    {"olradincommander", "When you pass: You may exhaust 1 non-home planet you control other than Mecatol Rex to gain a number of trade goods "
                         "equal to that planet's resource or influence value."},
    {"olradinhero", "ACTION: Place 1 infantry from your reinforcements on each planet you control; you may choose and flip 1 policy card. Then, "
                    "purge this card."},
};

// Commander / hero unlock conditions.
const KT kUnlock[] = {
    {"kelerescommander", "Spend 1 trade good after you play an action card that has a component action."},  // (the Keleres review: the card's wording)
    {"solcommander", "Control planets that have a combined total of at least 12 resources."},
    {"letnevcommander", "Have 5 non-fighter ships in 1 system."},
    {"hacancommander", "Have 10 trade goods."},
    {"jolnarcommander", "Own 8 technologies."},
    {"sardakkcommander", "Control 5 planets in non-home systems."},
    {"xxchacommander", "Control planets that have a combined total of at least 12 influence."},  // (the Xxcha review: the card)
    {"arboreccommander", "Have 12 ground forces on planets you control."},
    {"l1z1xcommander", "Have 4 dreadnoughts on the game board."},
    {"mentakcommander", "Have 4 cruisers on the game board."},  // (the Mentak review: the card)
    {"naalucommander", "Have ground forces in or adjacent to the Mecatol Rex system."},
    {"yincommander", "Use one of your faction abilities."},
    {"yssarilcommander", "Have 7 action cards."},  // (the card's wording)
    {"ghostcommander", "Have units in 3 systems that contain alpha or beta wormholes."},
    {"muaatcommander", "Produce a war sun."},
    {"saarcommander", "Have 3 space docks on the game board."},
    {"winnucommander", "Control Mecatol Rex or enter into a combat in the Mecatol Rex system."},
    {"nekrocommander", "Own 3 technologies. A \"Valefar Assimilator\" technology counts only if its X or Y token is on a technology."},  // (the Nekro review: the card)
    {"empyreancommander", "Be neighbors with all other players."},
    {"argentcommander", "Have 6 units that have ANTI-FIGHTER BARRAGE, SPACE CANNON, or BOMBARDMENT on the game board."},  // (the Argent review: the card)
    {"mahactcommander", "Have 2 other factions' command tokens in your fleet pool."},
    {"naazcommander", "Have mechs in 3 systems."},
    {"nomadcommander", "Have 1 scored secret objective."},
    {"titanscommander", "Have 5 structures on the game board."},
    {"cabalcommander", "Have units in 3 gravity rifts."},
    {"bastioncommander", "There are 3 galvanized units on the game board."},
    {"ralnelcommander", "Be the last person to pass during the action phase."},  // (the Ral Nel review: the card's wording)
    {"deepwroughtcommander", "Have an ocean card in play."},  // (the Deepwrought review: the card's wording)
    {"crimsoncommander", "Put a breach in a system with another player's unit."},
    {"firmamentcommander", "Have a plot card in play."},
    {"obsidiancommander", "Have units in The Fracture."},
    {"kortalicommander", "Destroy the last of a player's units in a system."},
    {"vayleriancommander", "Control 3 planets that each have the same trait."},
    {"lanefircommander", "7 relic fragments have been purged."},
    {"kyrocommander", "Have 6 infantry and 6 fighters on the game board."},
    {"cheirancommander", "Have 4 structures on non-home planets."},
    {"celdauricommander", "Have 1 space dock on the game board that is not in or adjacent to your home system."},
    {"khraskcommander", "Win a ground combat on a planet you do not control."},
    {"zealotscommander", "Have a technology with 2 or more prerequisites."},
    {"zeliancommander", "Have 1 or more ships in 2 systems that contain an asteroid field."},
    {"rohdhnacommander", "Have units with a combined PRODUCTION value of at least 7 in a single system."},
    {"tneliscommander", "Have 6 of your destroyers on the game board."},
    {"kolumecommander", "Ready 1 of your technologies during the action phase."},
    {"bentorcommander", "Have 3 Fragment tokens on your faction sheet."},
    {"dihmohncommander", "Research a ship unit upgrade technology."},
    {"florzencommander", "Have the outcome you voted for or predicted on an agenda resolved."},
    {"gledgecommander", "Control non-fighter ships with a total combined cost value of at least 10 in 1 system."},
    {"augerscommander", "Win a combat in a system that contains a legendary planet or a planet that has a technology specialty."},  // (the Augurs review: the card's wording)
    {"kjalengardcommander", "Have 2 Glory tokens on the game board."},
    {"kollecccommander", "Have 4 relic fragments."},
    {"mortheuscommander", "Have ships in 3 systems not adjacent to or including your home system."},
    {"vadencommander", "Have 1 or more different players' control tokens on your faction sheet for every 2 players in this game."},
    {"cymiaecommander", "Have 10 or more infantry on the board."},
    {"nivyncommander", "Have units in 2 different non-home anomalies."},
    {"olradincommander", "Control 1 cultural planet, 1 hazardous planet, and 1 industrial planet."},
    {"freesystemscommander", "Each non-legendary planet on the game board is controlled."},
    {"edyncommander", "Have 1 or more laws in play."},
    {"nokarcommander", "Another player moves 1 of your ships."},
    {"axiscommander", "Have 4 Axis Order cards of different unit types in 1 or more players' play areas."},  // (the Axis review: the card's wording)
    {"mykomentoricommander", "Have 4 commodities on your faction sheet."},
    {"gheminacommander", "Have 2 flagships on the game board."},
    {"veldyrcommander", "2 other players control 1 or more planets that have Branch Office attachments."},
    {"lizhocommander", "Have 4 Trap attachments on the game board."},
    {"mirvedacommander", "Own 2 unit upgrade technologies."},
    {"ghoticommander", "Have units in 3 systems that do not contain planets."},
    {"atokeracommander", "Cast more votes for an outcome than any other player."},  // (Blue Reverie, 0.83.0)
    {"belkoseacommander", "Have 1 superweapon in your play area."},              // (Blue Reverie, 0.84.0)
    {"pharadncommander", "ACTION: Destroy 2 of your infantry on each of 5 planets."},  // (Blue Reverie, 0.85.0: rules_br3.cpp pharAction)
};

// Flagships, mechs and other special units.
const KT kUnitText[] = {
    {"keleres_flagship", "Other players must spend 2 influence to activate the system that contains this ship."},  // (the Keleres review: the card's wording)
    {"keleres_mech", "Other players must spend 1 influence to commit ground forces to the planet that contains this unit. (Each of these mechs on the planet adds 1 influence.)"},  // (the Keleres review: the card's wording)
    {"sol_flagship", "At the end of the status phase, place 1 infantry from your reinforcements in this system's space area."},
    {"sol_mech", "DEPLOY: After you use your Orbital Drop faction ability, you may spend 3 resources to place 1 mech on that planet."},
    {"sol_infantry2", "After this unit is destroyed, roll 1 die. If the result is 5 or greater, place the unit on this card. At the start of your next turn, place each unit that is on this card on a planet you control in your home system."},
    // (the shared technologies review, 2026-11-14: the generic units' cards - AsyncTI4 units/baseUnits.json)
    {"infantry2", "After this unit is destroyed, roll 1 die. If the result is 6 or greater, place the unit on this card. At the start of your next turn, place each unit that is on this card on a planet you control in your home system."},
    {"dreadnought2", "This unit cannot be destroyed by \"Direct Hit\" action cards."},
    {"pds2", "You may use this unit's SPACE CANNON against ships that are in adjacent systems."},
    {"spacedock", "This unit's PRODUCTION value is equal to 2 more than the resource value of this planet. Up to 3 fighters in this system do not count against your ships' capacity."},
    {"spacedock2", "This unit's PRODUCTION value is equal to 4 more than the resource value of this planet. Up to 3 fighters in this system do not count against your ships' capacity."},
    {"warsun", "Other players' units in this system lose PLANETARY SHIELD."},
    {"letnev_flagship", "Other players' units in this system lose PLANETARY SHIELD. At the start of each space combat round, repair this ship."},
    {"letnev_mech", "DEPLOY: At the start of a round of ground combat, you may spend 2 resources to replace 1 of your infantry in that combat with 1 mech."},
    {"hacan_flagship", "After you roll a die during a space combat in this system, you may spend 1 trade good to apply +1 to the result."},
    {"hacan_mech", "This planet's card may be traded as part of a transaction; if you do, move all of your units from this planet to another planet you control."},
    {"jolnar_flagship", "When making a combat roll for this ship, each result of 9 or 10, before applying modifiers, produces 2 "
                        "additional hits."},
    {"jolnar_mech", "Your infantry on this planet are not affected by your FRAGILE faction ability."},
    {"sardakk_flagship", "Apply +1 to the result of each of your other ship's combat rolls in this system."},
    {"sardakk_mech", "After this unit uses its SUSTAIN DAMAGE ability during ground combat, it produces 1 hit against your opponent's ground forces on this planet."},
    {"sardakk_dreadnought2", "This unit cannot be destroyed by \"Direct Hit\" action cards. After a round of space combat, you may destroy this unit to destroy up to 2 ships in this system."},
    {"l1z1x_dreadnought2", "This unit cannot be destroyed by \"Direct Hit\" action cards."},
    // (the Xxcha review, 2026-11-13: the cards' wording)
    {"xxcha_flagship", "You may use this unit's SPACE CANNON against ships that are in adjacent systems."},
    {"xxcha_mech", "You may use this unit's SPACE CANNON against ships that are in adjacent systems."},
    {"arborec_flagship", "After you activate this system, you may produce up to 5 units in this system."},
    {"arborec_mech", "DEPLOY: When you would use your MITOSIS faction ability you may replace 1 of your infantry with 1 mech from your reinforcements instead."},
    {"arborec_infantry", "PRODUCTION 1"},
    {"arborec_infantry2", "PRODUCTION 2. After this unit is destroyed, roll 1 die. If the result is 6 or greater, place the unit on this card. At the start of your next turn, place each unit that is on this card on a planet you control in your home system."},
    {"l1z1x_flagship", "During a space combat, hits produced by this ship and by your dreadnoughts in this system must be assigned to non-fighter ships if able."},
    {"l1z1x_mech", "While not participating in ground combat, this unit can use its BOMBARDMENT ability on planets in its system as if it were a ship."},
    // (the Mentak review, 2026-11-13: the cards' wording)
    {"mentak_flagship", "Other players' ships in this system cannot use SUSTAIN DAMAGE."},
    {"mentak_mech", "Other players' ground forces on this planet cannot use SUSTAIN DAMAGE."},
    {"naalu_flagship", "During an invasion in this system, you may commit fighters to planets as if they were ground forces. When "
                       "combat ends, return those units to the space area."},  // (the LRR errata)
    {"naalu_mech", "During combat against an opponent who has at least 1 relic fragment, apply +2 to the results of this unit's combat "
                   "rolls."},
    {"naalu_fighter2", "This unit may move without being transported. Each fighter in excess of your ships' capacity counts as 1/2 of a "
                       "ship against your fleet pool."},
    {"fighter2", "This unit may move without being transported. Fighters in excess of your ships' capacity count against your fleet pool."},
    {"yin_flagship", "When this ship is destroyed, destroy all ships in this system."},
    {"yin_mech", "DEPLOY: When you use your Indoctrination faction ability, you may spend 1 additional influence to replace your opponent's unit with 1 mech instead of 1 infantry."},
    {"yssaril_flagship", "This ship can move through systems that contain other players' ships."},
    {"yssaril_mech", "DEPLOY: After you use your Stall Tactics faction ability, you may place 1 mech on a planet you control."},
    {"ghost_flagship", "This ship's system contains a delta wormhole. During movement, this ship may move before or after your other ships."},
    {"ghost_mech", "After any player activates a system, you may remove this unit from the game board to place or move a Creuss "
                   "wormhole token into this system. (\"This system\" is the system this mech is in - not the active system.)"},
    {"muaat_flagship", "ACTION: Spend 1 token from your strategy pool to place 1 cruiser in this system."},
    {"muaat_mech", "When you use your STAR FORGE faction ability in this system or an adjacent system, you may place 1 infantry from your reinforcements with this unit."},
    {"nekro_flagship", "At the start of a space combat, choose any number of your ground forces in this system to participate in that "
                       "combat as if they were ships."},  // (the Nekro review, 2026-11-14: the cards)
    {"nekro_mech", "During combat against an opponent who has an \"X\" or \"Y\" token on 1 or more of their technologies, apply +2 to "
                   "the result of each of this unit's combat rolls."},
    {"saar_mech", "DEPLOY: After you gain control of a planet, you may spend 1 trade good to place 1 mech on that planet."},
    {"saar_spacedock", "This unit is placed in the space area instead of on a planet. This unit can move and retreat as if it were a ship. "
                       "If this unit is blockaded, it is destroyed."},
    {"saar_spacedock2", "This unit is placed in the space area instead of on a planet. This unit can move and retreat as if it were a ship. "
                        "If this unit is blockaded, it is destroyed."},
    {"winnu_flagship", "When this unit makes a combat roll, it rolls a number of dice equal to the number of your opponent's non-fighter ships in this system."},
    {"winnu_mech", "After you resolve a tactical action where you gained control of this planet, you may place 1 PDS or 1 Space Dock from your reinforcements on this planet."},
    {"empyrean_flagship", "After any player's unit in this system or an adjacent system uses SUSTAIN DAMAGE, you may spend 2 influence to repair that unit."},
    {"empyrean_mech", "You may remove this unit from a system that contains or is adjacent to another player's units to cancel an action card played by that player."},
    // (the Argent review, 2026-11-13: the cards' wording)
    {"argent_destroyer2", "When this unit uses ANTI-FIGHTER BARRAGE, each result of 9 or 10 also destroys 1 of your opponent's infantry in "
                          "the space area of the active system."},
    {"argent_flagship", "Other players cannot use SPACE CANNON against your ships in this system."},
    {"argent_mech", "This unit does not count against capacity if it is being transported or if it is in a space area with 1 or more of "
                    "your ships that have capacity values."},
    {"cabal_flagship", "Capture all other non-structure units that are destroyed in this system, including your own."},
    {"cabal_mech", "When your infantry on this planet are destroyed, place them on your faction sheet; those units are captured."},
    {"cabal_spacedock", "This system is a gravity rift; your ships do not roll for this gravity rift. Place a dimensional tear token beneath this unit as a reminder. Up to 6 fighters in this system do not count against your ships' capacity."},
    {"cabal_spacedock2", "This system is a gravity rift; your ships do not roll for this gravity rift. Place a dimensional tear token beneath this unit as a reminder. Up to 12 fighters in this system do not count against your ships' capacity."},
    {"mahact_flagship", "During combat against an opponent whose command token is not in your fleet pool, apply +2 to the results of this unit's combat rolls."},
    {"mahact_infantry", "After this unit is destroyed, gain 1 commodity or convert 1 of your commodities to a trade good."},
    {"mahact_infantry2", "After this unit is destroyed, gain 1 commodity or convert 1 of your commodities to a trade good. Then, place the unit on this card. At the start of your next turn, place each unit that is on this card on a planet you control in your home system."},
    {"mahact_mech", "After a player whose command token is in your fleet pool activates this system, you may spend their token from your fleet pool to end their turn; they gain that token."},
    {"naaz_flagship", "Your mechs in this system roll 1 additional die during combat."},
    {"naaz_mech", "If this unit is in the space area of the active system at the start of a space combat, flip this card. Z-Grav "
                  "Eidolon (combat 8 x2, no Sustain Damage): If this unit is in the space area of the active system, it is also a "
                  "ship. At the end of a space battle in the active system, flip this card."},
    {"nomad_flagship", "You may treat this unit as if it were adjacent to systems that contain 1 or more of your mechs."},
    {"nomad_flagship2", "You may treat this unit as if it were adjacent to systems that contain 1 or more of your mechs."},
    {"nomad_mech", "While this unit is in a space area during combat, you may use its SUSTAIN DAMAGE ability to cancel a hit that is produced against your ships in this system."},
    {"titans_flagship", "DEPLOY: After you activate a system that contains 1 or more of your PDS, you may replace 1 of those PDS with this "
                        "unit."},
    {"titans_mech", "DEPLOY: When you would place a PDS on a planet, you may place 1 mech and 1 infantry on that planet instead."},
    {"titans_pds", "This unit is treated as both a structure and a ground force. It cannot be transported."},
    {"titans_pds2", "This unit is treated as both a structure and a ground force. It cannot be transported. You may use this unit's SPACE "
                    "CANNON against ships that are adjacent to this unit's systems."},
    // Thunder's Edge factions (0.35.0)
    {"bastion_flagship", "Apply +1 to the result of each of this unit's combat rolls for each non-home system that contains a planet "
                         "you control."},
    {"bastion_mech", "When this unit is destroyed, if it was galvanized, galvanize up to 3 of your infantry in its system."},
    {"bastion_spacedock", "The resource value of this planet is increased by 1. This unit's PRODUCTION value is equal to 2 more than the "
                          "resource value of this planet. Up to 3 fighters in this system do not count against your ships' capacity."},
    {"bastion_spacedock2", "The resource value of this planet is increased by 2. This unit's PRODUCTION value is equal to 4 more than the "
                           "resource value of this planet. Up to 3 fighters in this system do not count against your ships' capacity."},
    {"ralnel_flagship", "When this unit retreats, you may destroy 1 ship in the active system that does not have SUSTAIN DAMAGE."},  // (the Ral Nel review: the card's wording)
    {"ralnel_mech", "At the end of a round of combat on this planet, you may move up to 2 of your ground forces to this planet from planets in adjacent systems. (The other planets of its own system count too.)"},  // (the Ral Nel review: the card's wording)
    {"ralnel_destroyer", "This unit can use the SPACE CANNON ability of one of your structures in its space area; each structure can only be triggered once."},  // (the Ral Nel review: the card's wording)
    {"ralnel_destroyer2", "This unit can use the SPACE CANNON ability of one of your structures in its space area; each linkship can trigger the same structure."},  // (the Ral Nel review: the card's wording)
    {"deepwrought_flagship", "This ship can move through systems that contain your units, even if other players' units are present; if it would, apply +1 to its move value for each of those systems."},  // (the Deepwrought review: the card's wording)
    {"deepwrought_mech", "When another player activates this system, if this unit is coexisting, you may move it and any of your infantry on its planet to a planet you control in your home system."},  // (the Deepwrought review: the card's wording)
    {"crimson_destroyer", "Exile I - at the end of any combat in its system or an adjacent one: you may put an inactive breach in that system."},
    {"crimson_destroyer2", "Exile II - at the end of any combat within 2 systems: you may put an active or inactive breach in that system."},
    {"crimson_flagship", "While it is in a system with an active breach, other players' units in systems with active breaches lose their unit abilities."},
    {"crimson_mech", "Deploy: during the Commit Ground Forces step of your tactical action in a system with an active breach, you may commit 1 mech from your reinforcements, even if you have no units there."},
    {"firmament_flagship", "If the active system contains units that belong to a player who has a control marker on 1 of your plots, apply +1 to this ship's move value and repair it at the end of every combat round."},
    {"firmament_mech", "When ground forces are committed to this planet, you may choose for your units to coexist, if they were not already. Flip this card if your faction becomes the Obsidian."},
    {"obsidian_mech", "If this unit was coexisting when this card flipped to this side, gain control of its planet; the other player's units are now coexisting."},
    // Discordant Stars (0.37.0)
    {"kortali_flagship", "After you win a space combat in this system, you may have this ship become damaged to gain 1 command token."},
    {"kortali_mech", "This unit cannot lose its PLANETARY SHIELD."},
    {"tribune3", "This unit cannot be destroyed by \"Direct Hit\" action cards. When this unit produces a hit, you may repair it. While this unit is not damaged, you may reroll its combat roll."},
    {"vaylerian_flagship", "At the start of a space combat in this system, you may choose 1 adjacent system. Your opponent cannot retreat to that system."},
    {"vaylerian_cruiser", "During a round of space combat, if your opponent cannot declare a retreat, hits produced by this ship must be assigned to "
                          "non-fighter ships, if able. This unit may only transport ground forces."},
    {"vaylerian_cruiser2", "During a round of space combat, if your opponent cannot declare a retreat, hits produced by this ship cannot be canceled "
                           "and must be assigned to non-fighter ships, if able."},
    {"vaylerian_mech", "During your tactical actions, hits produced by SPACE CANNON cannot be assigned to 1 of your ships in this system."},
    {"lanefir_flagship", "At the end of a tactical action in this system, you may explore 1 planet you control in this system."},
    {"lanefir_mech", "DEPLOY: At the start of your turn, purge 1 of your relic fragments to place 1 mech on a planet you control, if you do, you may end your turn."},
    {"kyro_flagship", "When you commit units to a planet in this system, you may commit 1 infantry from your reinforcements to that planet."},
    {"kyro_mech", "When this unit would be destroyed, if it is damaged, you may discard 1 action card to repair it instead."},
    {"cheiran_flagship", "When this unit makes a combat or ability roll, it rolls 1 additional die if this system is adjacent to or contains 1 of your structures."},
    {"cheiran_dreadnought", "Chitin Hulk I - When this unit is destroyed, you may place 1 fighter from your reinforcements in this system's space area."},
    {"cheiran_dreadnought2", "Chitin Hulk II - This unit cannot be destroyed by \"Direct Hit\" action cards. When this unit is destroyed, you may place 1 fighter or 1 destroyer from your reinforcements in this system's space area."},
    {"cheiran_mech", "When this unit is destroyed, you may place 1 infantry from your reinforcements on this planet."},
    {"celdauri_flagship", "You may use the PRODUCTION ability of other players' space docks in this system to produce ships."},
    {"celdauri_mech", "DEPLOY: After a player activates a system, you may spend 1 trade good or 1 commodity to place 1 mech on a planet in that system that contains 1 of your space docks."},
    {"celdauri_spacedock", "Trade Port I - PRODUCTION X, ANTI-FIGHTER BARRAGE 6 (x2). This unit's PRODUCTION value is equal to 2 more than the resource or influence value of this planet. Up to 3 fighters in this system do not count against your ships' capacity."},
    {"celdauri_spacedock2", "Trade Port II - PRODUCTION X, ANTI-FIGHTER BARRAGE 6 (x2). This unit's PRODUCTION value is equal to 4 more than the resource or influence value of this planet. Up to 3 fighters in this system do not count against your ships' capacity. This unit may use its ANTI-FIGHTER BARRAGE during each round of space combat."},
    // Discordant Stars batch 2 (0.38.0)
    {"khrask_flagship", "At the start of a space combat in this system, choose up to 2 non-fighter ships to gain SUSTAIN DAMAGE until the end of combat."},
    {"khrask_cruiser", "Shattered Sky I - Bombardment 8."},
    {"khrask_cruiser2", "Shattered Sky II - 2 capacity, Bombardment 6."},
    {"khrask_mech", "Units other than your mechs do not make combat rolls during the first round of ground combat on this planet."},
    {"zealots_flagship", "For each unit upgrade technology your opponent owns, apply +1 to the results of this unit's combat rolls."},
    {"zealots_mech", "Apply +1 to this unit's combat rolls for each faction technology your opponent owns."},
    {"zelian_flagship", "When this unit makes a combat or unit ability roll, it rolls 1 additional die for each asteroid field adjacent to this "
                        "unit."},
    {"zelian_infantry", "During invasion, this unit must commit to a planet it bombards."},
    {"zelian_infantry2", "After this unit is destroyed, roll 1 die. If the result is 6 or greater, place the unit on this card. At the start of "
                         "your next turn, place each unit that is on this card on a planet you control in your home system."},
    {"zelian_mech", "At the start of invasion, if this unit is in the space area of the active system, you may remove this unit from the game "
                    "board to destroy 1 unit on a planet in that system."},
    {"rohdhna_flagship", "DEPLOY: After you activate a system, you may spend 4 resources to replace 1 of your non-fighter ships in that system "
                         "with your flagship."},
    {"rohdhna_warsun", "Terrafactory I - This unit produces only 1 fighter or infantry for their cost instead of 2. This unit cannot move or be "
                       "produced."},
    {"rohdhna_warsun2", "Terrafactory II - Other player's units in this system lose PLANETARY SHIELD. This unit produces only 1 fighter or "
                        "infantry for their cost instead of 2."},
    {"rohdhna_mech", "DEPLOY: After you use your RECYCLED MATERIALS faction ability in a system, you may place 1 mech in that system's space "
                     "area or on a planet you control in that system."},
    {"tnelis_flagship", "At the start of a round of combat, choose 1 ship in this system, during this combat round, that ship rolls 1 less "
                        "combat die."},
    {"tnelis_destroyer", "Blockade Runner I - ANTI-FIGHTER BARRAGE 9 (x3)."},
    {"tnelis_destroyer2", "Blockade Runner II - This ship can move through systems that contain other players' ships. ANTI-FIGHTER BARRAGE 6 (x4)."},
    {"tnelis_mech", "DEPLOY: After you move a destroyer into a non-home system other than Mecatol Rex, you may spend 3 resources to place 1 mech "
                    "on a planet in that system."},
    {"kolume_flagship", "Hits produced by the SPACE CANNON abilities of your units in this system cannot be canceled."},
    {"kolume_mech", "Hits produced by this unit cannot be assigned to non-fighter ships. After you spend a command token from your strategy pool, repair this unit."},
    {"bentor_flagship", "Apply +1 to the results of this ship's combat and ability rolls for each Fragment token on your faction sheet."},  // (the Bentor review: the card's wording)
    {"bentor_mech", "DEPLOY: When you place a Fragment token on your faction sheet, you may place 1 mech on a planet you control."},
    {"dihmohn_flagship", "After this unit moves into the active system, you may produce up to 2 units that have a combined cost of 4 or less in that system."},
    {"dihmohn_dreadnought", "Aegis I - when another ship in this system would be destroyed during combat, you may have this ship become damaged instead."},
    {"dihmohn_dreadnought2", "Aegis II - combat 4, move 2; when another ship in this system would be destroyed by a game effect, you may have this ship become damaged instead."},
    {"dihmohn_mech", "At the start of a combat in this system, you may repair 1 unit you control that is participating in that combat."},
    {"florzen_flagship", "Other players cannot play action cards during a space combat in this system."},
    {"florzen_fighter", "Corsair I - move 1, Anti-Fighter Barrage 9. This unit may move without being transported. Fighters in excess of your ships' capacity count against your fleet pool."},
    {"florzen_fighter2", "Corsair II - combat 8, move 3, Anti-Fighter Barrage 8. This unit may move without being transported. Fighters in excess of your ships' capacity count against your fleet pool."},
    {"florzen_mech", "You may spend influence as resources to produce this unit."},
    {"gledge_flagship", "When this unit makes a combat or ability roll, it rolls 1 additional die for each mech in or adjacent to this system."},
    {"gledge_mech", "When you exhaust this planet to spend resources, you may also spend 1 of its influence as a resource."},
    {"gledge_pds", "Orion Platform I: When this unit makes a SPACE CANNON roll against another player's units, if it rolls at least 1 result of 9 or 10, explore this planet."},
    {"gledge_pds2", "Orion Platform II: When this unit produces 1 or more hits against another player's units, explore this planet. You may use this unit's SPACE CANNON against ships that are adjacent to this unit's system."},
    {"augers_flagship", "When this unit makes a combat roll, it rolls 1 additional die for each secret objective you have scored."},  // (the Augurs review: the card's wording)
    {"augers_mech", "DEPLOY: After researching a technology, you may place 1 mech on a legendary planet, or a planet that has a technology "
                    "specialty, that you control."},
    {"kjalengard_flagship", "After the first round of a combat in this system, place up to 2 of your captured units in this system or on that planet."},
    {"kjalengard_carrier", "This unit may ignore the movement effects of non-supernova anomalies."},
    {"kjalengard_carrier2", "This unit may ignore the movement effects of anomalies."},
    {"kjalengard_mech", "When you pass, place 1 infantry from your reinforcements on this planet if there is a Glory token in or adjacent to this system."},
    {"kollecc_flagship", "When this unit retreats, you may capture each of your units that retreat."},
    {"kollecc_mech", "At the end of a tactical action in this system, you may place up to 2 ground forces from this planet onto your faction sheet, those units are captured."},
    {"mortheus_flagship", "After you activate this system, if it does not contain any planets, you may place 1 frontier token in this system."},
    {"mortheus_mech", "When a player commits 1 or more units to a planet you control adjacent to this system, you may swap this unit with 1 of your "
                      "infantry on that planet."},
    {"vaden_flagship", "After this unit produces 1 or more hits during a BOMBARDMENT roll, gain 1 trade good."},
    {"vaden_mech", "At the end of a round of ground combat, you may remove 1 of your opponent's control tokens from your faction sheet to place "
                   "1 infantry unit from your reinforcements on this planet."},
    {"cymiae_flagship", "After you win a combat in this system, you may take 1 of your opponent's action cards, at random."},
    {"cymiae_infantry", "Unholy Abomination I - combat 5."},
    {"cymiae_infantry2", "Unholy Abomination II - combat 4. After this unit is destroyed, roll 1 die. If the result is 6 or greater, place the unit on this card. At the start of your turn, place each unit that is on this card on a planet you control, if able."},
    {"cymiae_mech", "At the start of each ground combat round, if this planet contains no more than 1 of your mechs, repair this unit."},
    {"nivyn_flagship", "When a unit in this system would be destroyed, you may remove it from the game board instead."},
    {"nivyn_mech", "Voidflare Warden I - After this system is activated, you may have this unit become damaged to place or move the Wound token "
                   "into this system."},
    {"nivyn_mech2", "Voidflare Warden II - combat 4. After a system is activated, you may have this unit become damaged to place or move the "
                    "Wound token into this system."},
    {"olradin_flagship", "When you move this ship, apply +1 to the move value of each of your other ships during this tactical action."},
    {"olradin_mech", "Exemplar - If you have at least 2 \"+\" policies, and if this planet contains no more than 1 of your mechs, apply +1 to its "
                     "influence value. If you have at least 2 \"-\" policies, and if this planet contains no more than 1 of your mechs, apply +1 "
                     "to its resource value."},
    {"olradin_mech_positive", "Exemplar Bonum - If this planet contains no more than 1 of your mechs, apply +1 to its influence value."},
    {"olradin_mech_negative", "Exemplar Malus - If this planet contains no more than 1 of your mechs, apply +1 to its resource value."},
    {"freesystems_flagship", "When this unit makes a combat roll, it rolls 1 additional die for each planet in this system of any single trait."},
    {"freesystems_mech", "Deploy: After you use your Rally to the Cause faction ability in a system, you may spend 1 trade good to place 1 mech on a planet you control adjacent to that system."},
    {"edyn_flagship", "Apply +1 to the results of this unit's combat rolls for each law in play."},
    {"edyn_mech", "This system is a Sigil anomaly. Place an Edyn Sigil token beneath this unit as a reminder. Game effects cannot prevent you from using this ability."},
    {"nokar_flagship", "Apply +1 to the results of this unit's combat rolls for every 2 destroyers you control."},
    {"nokar_destroyer", "Sabre I - After this unit is destroyed during combat, roll a die, on a result of 9 or 10, produce up to 1 hit against "
                        "your opponent's ships."},
    {"nokar_destroyer2", "Sabre II - combat 7. After this unit is destroyed during combat, roll a die, on a result equal to or greater than 7, "
                         "produce up to 1 hit against your opponent's ships."},
    {"nokar_mech", "You may treat this system as adjacent to the active system for the purposes of declaring and resolving retreats."},
    {"axis_flagship", "After this ship produces 1 or more hits during a round of space combat, you may repair 1 ship you control in this system."},  // (the Axis review: the card's wording)
    {"axis_mech", "You may treat a spacedock on this planet as if it has PRODUCTION 5."},
    {"mykomentori_flagship", "Once per round of space combat, when a non-fighter ship in this system is destroyed, you may gain 1 commodity."},
    {"mykomentori_mech", "After this unit is destroyed, roll a die. If the result is 6 or greater, place the unit on this card. At the start of your "
                         "turn, you may replace 1 infantry you control with a unit that is on this card."},
    {"mykomentori_spacedock", "This unit's PRODUCTION value is equal to 2 more than the resource value of this planet. DEPLOY: When you gain control "
                              "of a planet, you may replace 4 infantry on that planet with 1 space dock."},
    {"mykomentori_spacedock2", "This unit's PRODUCTION value is equal to 5 more than the resource value of this planet. DEPLOY: When you gain control "
                               "of a planet, you may replace 3 infantry on that planet with 1 space dock."},
    {"ghemina_flagship_lord", "The Lord: After a player explores a planet in this system, place 1 infantry from their reinforcements on that planet."},
    {"celdauri_celagrom", "The Celagrom: At the start of space combat, you may produce 1 hit and assign it to 1 of your opponent's ships. While you control another flagship, this unit rolls 1 additional combat die."},
    {"ghemina_flagship_lady", "The Lady: When a structure you do not control in this system is destroyed, gain 1 trade good."},
    {"ghemina_mech", "After you win a ground combat on this planet, if this planet contains exactly 1 other mech, explore this planet. (Infantry and fighters don't stop it.)"},
    {"ghemina_carrier", "Combat Transport I - move 2."},
    {"ghemina_carrier2", "Combat Transport II: You may reroll 1 of your unit's combat dice during each round of ground combat on a planet in this system that contains 2 or fewer of your infantry."},
    {"veldyr_flagship", "When this ship makes a combat roll, it rolls 1 additional die for each round of combat that has been resolved this combat."},
    {"veldyr_dreadnought", "Lancer Dreadnought I - SUSTAIN DAMAGE, BOMBARDMENT 5, SPACE CANNON 8."},
    {"veldyr_dreadnought2", "This unit cannot be destroyed by \"Direct Hit\" action cards. SUSTAIN DAMAGE, BOMBARDMENT 5, SPACE CANNON 5."},
    {"veldyr_mech", "After a player activates this system, you may remove this unit from the game board to place 1 PDS from your reinforcements "
                    "on this planet."},
    {"lizho_flagship", "This unit can only be destroyed by an uncanceled hit being assigned to it."},
    {"lizho_fighter", "Heavy Bomber I - combat 9, Bombardment 9."},
    {"lizho_fighter2", "Heavy Bomber II - combat 8, move 2, Bombardment 8. This unit may move without being transported. Fighters in excess "
                       "of your ships' capacity count against your fleet pool."},
    {"lizho_mech", "At the start of your turn, you may remove 1 Trap from the game board and attach it to this planet, or swap any Trap with 1 "
                   "on this planet."},
    {"mirveda_flagship", "After each round of space combat in this system, place 1 fighter from your reinforcements in this system."},
    {"mirveda_pds", "Gauss Cannon I - move 1, Space Cannon 6, Bombardment 6. This unit is placed in a space area instead of on a planet. This unit can "
                    "move and retreat as if it were a ship. This unit can be blockaded; if it is blockaded, it is destroyed."},
    {"mirveda_pds2", "Gauss Cannon II - move 2, capacity 1, Space Cannon 4, Bombardment 4. This unit is placed in a space area instead of on a planet. "
                     "This unit can move and retreat as if it were a ship. This unit can be blockaded; if it is blockaded, it is destroyed."},
    {"mirveda_mech", "For every 2 unit upgrade technologies you own, apply +1 to the result of this unit's combat rolls."},
    {"mirveda_fighter3", "Eye of the Curator - combat 8, move 3. This unit may move without being transported. Fighters in excess of your ships' "
                         "capacity count against your fleet pool. Your fighters count as 1/2 a ship for the purposes of capacity and transportation."},
    {"ghoti_flagship", "While in play, this unit is also treated as a space dock with a PRODUCTION value equal to the number of tokens in your fleet pool."},
    {"ghoti_mech", "This unit can be blockaded. When producing ships in your home system, place up to 1 of those units in this system's space area if it is not blockaded."},
    // Blue Reverie (0.83.0, the cards' wording): the Atokera Legacy
    {"atokera_cruiser", "This unit has PRODUCTION 0."},
    {"atokera_cruiser2", "DEPLOY: During the agenda phase, after an outcome you voted for or predicted is resolved, place up to 1 cruiser in a system that contains your ships."},
    {"atokera_mech", "Other players cannot exhaust this planet. During combat as the defender, you may exhaust this planet to repair each of your units on this planet."},
    {"atokera_flagship", "Your other units in this system also have the unit and text abilities of other players' units of the same type in this system."},
    {"belkosea_fighter", "You may commit this unit to a planet as if it were a ground force. When combat ends, return it to the space area."},
    {"belkosea_fighter2", "You may commit this unit to a planet as if it were a ground force. When combat ends, return it to the space area. Fighters in excess of your ships' capacity count against your fleet pool."},
    {"belkosea_mech", "When this unit produces a hit, gain 1 commodity or convert 1 of your commodities to a trade good."},
    {"belkosea_flagship", "During combat, you may have this ship become damaged to produce 1 hit against another player's non-fighter ships in this system."},
    {"pharadn_infantry", "After this unit is destroyed or removed, capture this unit. Game effects cannot prevent you from using this ability."},
    {"pharadn_infantry2", "After this unit is destroyed or removed, capture this unit. Game effects cannot prevent you from using this ability. DEPLOY: At the start of your turn, you may place 1 infantry on a planet you control."},
    {"pharadn_mech", "DEPLOY: Once per action, after 2 or more of your infantry are destroyed on a planet, you may place 1 mech from your reinforcements on that planet."},
    {"pharadn_flagship", "During movement, apply +1 to this unit's move value for each ground force it transports."},
};

// Faction promissory notes, keyed by faction key.
const KT kNoteName[] = {
    {"sol", "Military Support"}, {"letnev", "War Funding"}, {"hacan", "Trade Convoys"},
    {"jolnar", "Research Agreement"}, {"sardakk", "Tekklar Legion"}, {"xxcha", "Political Favor"},
    {"arborec", "Stymie"}, {"ghost", "Creuss Iff"}, {"l1z1x", "Cybernetic Enhancements"}, {"mentak", "Promise of Protection"},
    {"muaat", "Fires of the Gashlai"}, {"naalu", "Gift of Prescience"}, {"nekro", "Antivirus"}, {"saar", "Ragh's Call"},
    {"winnu", "Acquiescence"}, {"yin", "Greyfire Mutagen"}, {"yssaril", "Spy Net"}, {"argent", "Strike Wing Ambuscade"},
    {"cabal", "Crucible"}, {"empyrean", "Dark Pact"}, {"mahact", "Scepter of Dominion"}, {"nomad", "The Cavalry"},
    {"naaz", "Black Market Forgery"}, {"titans", "Terraform"}, {"keleres", "Keleres Rider"},
    {"bastion", "Raise the Standard"}, {"ralnel", "Nano-Link Permit"}, {"crimson", "Sever"}, {"deepwrought", "Share Knowledge"},
    {"firmament", "Black Ops"}, {"obsidian", "Malevolency"},
    {"kortali", "Blessing of the Queens"}, {"vaylerian", "Clan's Favor"}, {"lanefir", "Spoils of War"}, {"kyro", "Kyro Rider"},
    {"cheiran", "Carcinisation"}, {"celdauri", "Trade Alliance"},
    {"khrask", "Stone Speakers"}, {"zealots", "Favor of Rhodun"}, {"zelian", "Hyperkinetic Ordnance"}, {"rohdhna", "Automatons"},
    {"tnelis", "Plots Within Plots"}, {"kolume", "Combinatorial Bypass"},
    {"bentor", "Encryption Key"}, {"dihmohn", "Combat Drills"}, {"florzen", "Underground Market"}, {"gledge", "Gledge Base"},
    {"augers", "Read the Fates"}, {"kjalengard", "Vassalage"},
    {"kollecc", "AI Survey"}, {"mortheus", "Secrets of the Weave"}, {"vaden", "Vaden Handshake"}, {"cymiae", "Algorithmic Replication"},
    {"nivyn", "Nivyn Guidance"}, {"olradin", "Incite Revolution"},
    {"freesystems", "Broadcast Teams"}, {"edyn", "Edyn Rider"}, {"nokar", "Nokar Navigator"}, {"axis", "Industry Secrets"},
    {"mykomentori", "Gift of Insight"}, {"ghemina", "Raid Leaders"},
    {"lizho", "Trusted Counselor"}, {"mirveda", "Rapid Excavation"}, {"ghoti", "Ghoti Relay"},
    {"atokera", "Tri-Weave Shielding"},  // (Blue Reverie, 0.83.0)
    {"belkosea", "Mercenary Sortie"},     // (Blue Reverie, 0.84.0)
    {"pharadn", "Death Binding"},         // (Blue Reverie, 0.85.0)
};
const KT kNoteText[] = {
    {"sol", "At the start of the Sol player's turn: Remove 1 token from the Sol player's strategy pool, if able, and return it to their reinforcements. Then, you may place 2 infantry from your reinforcements on any planet you control. Then, return this card to the Sol player."},
    {"letnev", "At the start of a round of space combat: The Letnev player loses 2 trade goods. During this combat round, reroll any number of your dice. Then, return this card to the Letnev player."},
    {"hacan", "Played face up: you may trade with players who aren't your neighbours. It goes back to Hacan if you activate a system with their units."},  // (games before 0.32.0: placed at once - the card's wording is in noteTextOf)
    {"jolnar", "After the Jol-Nar player researches a technology that is not a faction technology: Gain that technology. Then, return "
               "this card to the Jol-Nar player."},
    {"sardakk", "At the start of an invasion combat: Apply +1 to the result of each of your unit's combat rolls during this combat. If your opponent is the N'orr player, apply -1 to the result of each of their unit's combat rolls during this combat. Then, return this card to the N'orr player."},
    {"xxcha", "When an agenda is revealed: Remove 1 token from the Xxcha player's strategy pool and return it to their reinforcements. "
              "Then, discard the revealed agenda and reveal 1 agenda from the top of the deck. Players vote on this agenda instead. Then, "
              "return this card to the Xxcha player."},  // (the Xxcha review, 2026-11-13: the card)
    {"arborec", "After the Arborec player moves ships into a system that contains 1 or more of your units: You may place 1 command token from the Arborec player's reinforcements in any non-home system. Then, return this card to the Arborec player."},
    {"l1z1x", "At the start of your turn: Remove 1 token from the L1Z1X player's strategy pool and return it to their reinforcements. Then, place 1 command token from your reinforcements in your strategy pool. Then, return this card to the L1Z1X player."},
    {"mentak", "ACTION: Place this card faceup in your play area. While this card is in your play area, the Mentak player cannot use "
               "their Pillage faction ability against you. If you activate a system that contains 1 or more of the Mentak player's units, "
               "return this card to the Mentak player."},  // (the Mentak review, 2026-11-13: the card)
    {"naalu", "At the end of the Strategy Phase: Place this card faceup in your play area and place the Naalu \"0\" token on your "
              "strategy card, you are the first in initiative order. The Naalu player cannot use their Telepathic faction ability "
              "during this game round. Return this card to the Naalu player at the end of the status phase."},
    {"yin", "At the start of a ground combat against 2 or more ground forces that are not controlled by the Yin player: Replace 1 of your opponent's infantry with 1 infantry from your reinforcements. Then, return this card to the Yin player."},
    {"yssaril", "At the start of your turn: Look at the Yssaril player's hand of action cards. Choose 1 of those cards and add it to your hand. Then, return this card to the Yssaril player."},
    {"ghost", "At the start of your turn during the action phase: Place or move a Creuss wormhole token into either a system that contains a planet you control or a non-home system that does not contain another player's ships. Then, return this card to the Creuss player."},
    {"muaat", "ACTION: Remove 1 token from the Muaat player's fleet pool and return it to their reinforcements. Then, gain your war sun unit upgrade technology card. Then, return this card to the Muaat Player."},
    {"saar", "After you commit 1 or more units to land on a planet: Remove all of the Saar player's ground forces from that planet and "
             "place them on a planet controlled by the Saar player. Then, return this card to the Saar player."},
    {"winnu", "At the end of the strategy phase: Exchange 1 of your strategy cards with a strategy card that was chosen by the Winnu player. Then, return this card to the Winnu player."},  // (the Winnu review: the original card; the Omega below with the Codex)
    {"nekro", "At the start of a combat: Place this card faceup in your play area. While this card is in your play area, the Nekro "
              "player cannot use their Technological Singularity faction ability against you. If you activate a system that contains 1 "
              "or more of the Nekro player's units, return this card to the Nekro player."},  // (the Nekro review: the card)
    {"empyrean", "Played face up: when you give the Empyrean as many commodities as your commodity value, you each gain 1 trade good. It goes back to the Empyrean if you activate a system with their units."},
    {"argent", "When 1 or more of your units make a roll for a unit ability: Choose 1 of those units to roll 1 additional die. Then, return "
               "this card to the Argent player."},  // (the Argent review, 2026-11-13: the card)
    {"mahact", "At the start of the strategy phase: Choose 1 non-home system that contains your units, each other player who has a token on the Mahact player's command sheet places a token from their reinforcements in that system. Then, return this card to the Mahact player."},
    {"naaz", "ACTION: Purge 2 of your relic fragments of the same type to gain 1 relic. Then, return this card to the Naaz-Rokha "
             "player."},
    {"nomad", "At the start of a space combat against a player other than the Nomad: During this combat, treat 1 of your non-fighter ships as if it has the SUSTAIN DAMAGE ability, combat value, and ANTI-FIGHTER BARRAGE value of the Nomad's flagship. Return this card to the Nomad player at the end of the combat."},
    {"titans", "ACTION: Attach this card to a non-home planet you control other than Mecatol Rex. Its resource and influence values "
               "are each increased by 1, and it is treated as having all 3 planet traits (cultural, hazardous, and industrial)."},
    {"cabal", "After you activate a system: Your ships do not roll for gravity rifts during this movement; apply an additional +1 to the move values of your ships that would move out of or through a gravity rift instead. Then, return this card to the Vuil'raith player."},
    {"keleres", "After an agenda is revealed: You cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, draw 1 action card and gain 2 trade goods. Then, return this card to the Keleres player."},  // (the Keleres review: the card's wording)
    {"bastion", "At the end of a combat: Galvanize 1 of your units that participated. Then, return this card to the Last Bastion player."},
    {"ralnel", "After you activate a system: You may move your structures from adjacent systems that do not contain your command tokens onto planets you control in the active system. Then, return this card to the Ral Nel player."},  // (the Ral Nel review: the card's wording)
    {"deepwrought", "ACTION: Place this card faceup in your play area and gain 1 non-faction, non-unit upgrade technology that the Deepwrought player owns; place that technology on this card. Return that technology to the deck and this card to the Deepwrought player at the end of the status phase."},  // (the Deepwrought review: the card's wording)
    {"crimson", "Action: play it face up and pick a system with your units: its wormholes do nothing during movement until the end of the status phase; then the note goes back."},
    {"firmament", "When you receive this card, if you are not the Firmament: The Firmament player may place 1 facedown plot card in their play area with your control token on it. Then, gain 2 command tokens, gain 2 trade goods, and purge this card."},
    {"obsidian", "At the end of one of your tactical actions: Spend 1 influence to give this card to one of your neighbors; you can use this ability even if you are the Obsidian player. At the end of the status phase, if you are not the Obsidian player, you must remove 1 command token from your fleet pool and return it to your reinforcements."},
    {"kortali", "ACTION: Place this card face up in your play area and remove 1 of your command tokens from the game board. At the start of the status phase, return this card to the Kortali player."},
    {"vaylerian", "At the start of a round of space combat: The Vaylerian player discards 1 action card. Then, move all of your ships from the "
                  "active system to an adjacent system that does not contain another player's ships; the space combat ends in a draw. Then, place "
                  "a command token from your reinforcements in that system and return this card to the Vaylerian player."},
    {"lanefir", "After you win a combat: Place this card face-up in your play area to explore 1 planet you control. At the start of the Lanefir player's turn, return this card to the Lanefir player."},
    {"kyro", "After an agenda is revealed: You cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, place 3 infantry from your reinforcements on a planet you control. Then, return this card to the Kyro player."},
    {"cheiran", "When 1 of your non-fighter ships is destroyed during combat: You may place 2 fighters from your reinforcements in the space area of the active system. At the end of that combat, return this card to the Cheiran Hordes player."},
    {"celdauri", "When you receive this card, if you are not the Celdauri player, you must place it face up in your play area. While this card is in your play area, you can use the Celdauri player's commander ability, if it is unlocked. If you activate a system that contains 1 or more of the Celdauri player's units, return this card to the Celdauri player."},
    {"khrask", "During a tactical action, when you produce 1 or more hits during a BOMBARDMENT roll: For each hit you produced, you may instead place 1 infantry from your reinforcements on the planet being bombarded. Then, return this card to the L'Tokk Khrask player."},
    {"zealots", "When you ignore a prerequisite on a technology card you are researching: You may ignore 1 additional prerequisite of the "
                "same color. Then, return this card to the Rhodun player."},
    {"zelian", "When 1 or more of your units would make a BOMBARDMENT roll: If you produce 1 or more hits during this BOMBARDMENT roll, "
               "produce 1 additional hit for that roll. Then, return this card to the Zelian player."},
    {"rohdhna", "At the end of your turn, you may attach this card to a non-home planet you control other than Mecatol Rex. This planet has "
                "PRODUCTION 3 as if it were a unit."},
    {"tnelis", "When you draw 1 or more secret objective cards: Draw 1 additional secret objective card. Then, return 1 secret objective card to "
               "the secret objective deck; shuffle that deck. Then, return this card to the Tnelis player."},
    {"kolume", "At the start of invasion, place this card in your play area. During this invasion, all other player's units lose SPACE CANNON and PLANETARY SHIELD. At the start of your next turn, return this card to the Kolume player."},
    {"bentor", "ACTION: Attach this card to a non-home planet you control. This planet has 1 technology specialty of any color."},  // (the Bentor review: the card's wording)
    {"dihmohn", "When 1 of your ships would be destroyed: instead of destroying that ship, place this card face up in your play area. Return this card to the Dih-Mohn player at the start of the status phase."},
    {"florzen", "When you would gain a relic: You may draw 1 additional card. Choose 1 to gain and return the rest to the relic deck. Then, shuffle the relic deck. Then, return this card to the Florzen player."},
    {"gledge", "ACTION: Attach this card to a non-home planet you control. This planet's resource value is increased by 2."},
    {"augers", "ACTION: Place this face up in your play area. While this card is in your play area, you may look at the public objective card "
               "on the Ilyxum player's faction sheet. If you activate a system that contains 1 or more of the Ilyxum player's units, return "
               "this card to the Ilyxum player."},  // (the Augurs review: the card's wording)
    {"kjalengard", "At the start of combat: Apply +1 to the results of each of your fighters' combat rolls during this combat. The Kjalengard player captures each of your fighters destroyed during this combat. Then, return this card to the Kjalengard player."},
    {"kollecc", "At the start of your turn: You may look at either the top card of the cultural, hazardous, and industrial exploration decks, or the top card of the relic deck. Then, return this card to the Kollecc player."},
    {"mortheus", "At the start of a space combat while you are the defender: you may spend influence equal to the cost value of 1 of your "
                 "opponent's ships in the active system to place 1 unit of that type from your reinforcements in that system. Then return this "
                 "card to the Mortheus player."},
    {"vaden", "After you activate a system: You may spend any number of trade goods; for each trade good spent, apply +1 to the move value of 1 "
              "non-fighter ship you control during this activation. Return this card to the Vaden player at the end of this activation."},
    {"cymiae", "Action: choose 1 action card from the action card discard pile and add it to your hand. Then, purge this card."},
    {"nivyn", "After you activate a system: During this activation, you may ignore the effects of each anomaly. Return this card to the Nivyn "
              "player at the end of this activation."},
    {"olradin", "ACTION: Place this card face up in your play area to choose and exhaust 1 planet you control. Then, ready 1 non-home planet you "
                "control other than Mecatol Rex. At the start of the status phase, return this card to the Olradin player."},
    {"freesystems", "When you gain control of a planet during a tactical action: If the active system does not contain another player's ships, you may produce up to 2 ships in that system. Then, return this card to the Free Systems player."},
    {"edyn", "After an agenda is revealed: you cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, place 1 command token from another player's reinforcements in a system that contains your units. Then, return this card to the Edyn player."},
    {"nokar", "At the start of a space combat: During this combat, the active system is adjacent to each system that contains a planet you "
              "control for the purposes of announcing and resolving retreats. At the end of this combat, return this card to the Nokar player."},
    {"axis", "When 1 or more of your units use PRODUCTION: Place this card face up in your play area to apply +4 to the PRODUCTION value of "
             "those units and reduce the combined cost of the produced units by 1 for this use of PRODUCTION. At the beginning of the status "
             "phase, return this card to the Axis player."},  // (the Axis review: the card's wording)
    {"mykomentori", "ACTION: Place this card face up in your play area. While this card is in your play area, once per turn, after you roll a die, "
                    "you may reroll that die. If you activate a system that contains 1 or more of the Myko-Mentori player's units, return this "
                    "card to the Myko-Mentori player."},
    {"ghemina", "After you activate a system: Choose 1 non-fighter ship you control. During this action, that unit gains BOMBARDMENT 5 (x2) and a capacity value of 2. Return this card to the Ghemina player at the end of this action."},
    {"lizho", "At the start of a space combat you are participating in: you may place up to 2 fighters from your reinforcements into the "
              "space area of the active system. Then return this card to the Li-Zho player."},
    {"mirveda", "At the start of your turn: you may move up to 2 PDS units you control to a planet you control. Then return this card to the "
                "Mirveda player."},
    {"ghoti", "At the start of your turn: Place this card in your play area to produce up to 2 ships in a system that contains 1 or more of your ships. At the start of the status phase, return this card to the Ghoti player."},
    {"atokera", "At the start of invasion: Choose 1 unit you control in the active system; that unit gains PLANETARY SHIELD until the end of this action. Then, return this card to the Atokera player."},
    {"belkosea", "At the start of combat: Roll 3 dice, for each result of 8 or greater, destroy up to 1 of your opponent's fighters or infantry participating in that combat. After the first round of this combat, return this card to the Belkosea player."},
    {"pharadn", "When the Pharad'n player passes, remove 4 of your infantry from the game board to gain 1 technology with no prerequisites. Then, return this card to the Pharad'n player; that player captures 1 infantry from the supply."},
};

}  // namespace

// Discordant Stars batches (0.37.0 = 1): a game's random pools only use the batches it was started with.
int dsBatch(int faction) {
    if (faction < 0 || faction >= kFactionCount || kFactions[faction].source != Source::Discordant || !factionImplemented(faction)) return 0;
    static const char* kBatch1[] = {"kortali", "vaylerian", "lanefir", "kyro", "cheiran", "celdauri"};
    static const char* kBatch2[] = {"khrask", "zealots", "zelian", "rohdhna", "tnelis", "kolume"};
    static const char* kBatch3[] = {"bentor", "dihmohn", "florzen", "gledge", "augers", "kjalengard"};
    static const char* kBatch4[] = {"kollecc", "mortheus", "vaden", "cymiae", "nivyn", "olradin"};
    static const char* kBatch5[] = {"freesystems", "edyn", "nokar", "axis", "mykomentori", "ghemina"};
    static const char* kBatch6[] = {"veldyr", "lizho", "mirveda", "ghoti"};
    for (const char* k : kBatch1)
        if (!std::strcmp(k, kFactions[faction].key)) return 1;
    for (const char* k : kBatch2)
        if (!std::strcmp(k, kFactions[faction].key)) return 2;
    for (const char* k : kBatch3)
        if (!std::strcmp(k, kFactions[faction].key)) return 3;
    for (const char* k : kBatch4)
        if (!std::strcmp(k, kFactions[faction].key)) return 4;
    for (const char* k : kBatch5)
        if (!std::strcmp(k, kFactions[faction].key)) return 5;
    for (const char* k : kBatch6)
        if (!std::strcmp(k, kFactions[faction].key)) return 6;
    return 99;
}
bool factionInPool(int faction, int dsLevel) {
    if (!factionImplemented(faction)) return false;
    if (faction >= 0 && faction < kFactionCount && kFactions[faction].source == Source::Discordant) {
        const int b = dsBatch(faction);
        return b > 0 && b <= dsLevel;
    }
    return true;
}

// Blue Reverie (0.83.0): the version step each faction came in - a game's random pools only use those of its level
int brBatch(int faction) {
    if (faction < 0 || faction >= kFactionCount || kFactions[faction].source != Source::BlueReverie || !factionImplemented(faction)) return 0;
    static const char* kBatch[] = {"atokera", "belkosea", "pharadn"};  // level 1 (0.83.0), 2 (0.84.0), 3 (0.85.0)
    for (size_t i = 0; i < sizeof(kBatch) / sizeof(kBatch[0]); ++i)
        if (!std::strcmp(kBatch[i], kFactions[faction].key)) return int(i) + 1;
    return 99;
}
bool brInPool(int faction, int brLevel, uint32_t brMask) {
    const int b = brBatch(faction);
    return b > 0 && b <= brLevel && b <= 32 && ((brMask >> (b - 1)) & 1u);  // (0.85.3: the ones unlocked - Setup::brMask)
}

bool factionImplemented(int faction) {
    if (faction < 0 || faction >= kFactionCount) return false;
    static std::vector<int8_t> done;  // looked up once (this is called very often)
    if (done.empty()) {
        done.assign(size_t(kFactionCount), 0);
        for (int f = 0; f < kFactionCount; ++f)
            for (const char* k : kDone)
                if (!std::strcmp(k, kFactions[f].key)) done[size_t(f)] = 1;
    }
    return done[size_t(faction)] != 0;
}

const char* abilityText(int faction, int i) {
    if (faction < 0 || faction >= kFactionCount || i < 0 || i > 2 || !kFactionInfo[faction].abilities[i]) return "";
    return FIND(kAbility, kFactionInfo[faction].abilities[i]);
}

const KT kLeaderOmega[] = {
    {"naaluagent", "ACTION: Exhaust this card and choose a player. That player may perform a tactical action in a non-home system "
                   "without placing a command token. That system still counts as being activated."},
    {"yinagent", "After a player's unit is destroyed during combat: You may exhaust this card to allow that player to place 2 fighters in the destroyed unit's system if it was a ship, or 2 infantry on its planet if it was a ground force."},
    {"yincommander", "This card satisfies a green technology prerequisite. When you research a tech owned by another player, you may return 1 of your infantry to reinforcements to ignore its prerequisites."},
    {"yinhero", "ACTION: Commit up to 3 infantry from your reinforcements to any non-home planets and resolve ground combats on those planets. Players cannot use SPACE CANNON against these units. Then, purge this card."},
    {"xxchahero", "When you exhaust planets, combine the values of their resources and influence. Treat the combined value as if it were "
                  "both resources and influence."},  // (the Xxcha review, 2026-11-13: the Codex III card)
};
const char* leaderText(int leader) {
    if (leader < 0 || leader >= kLeaderCount) return "";
    if (teNaaluTexts() && !std::strcmp(kLeaders[leader].key, "naaluagent"))  // (fixes 27, the Naalu review: Thunder's Edge's version)
        return "After any player's command token is placed in a system: You may exhaust this card to return that token to that "
               "player's reinforcements.";
    if (teXxchaHeroTexts() && !std::strcmp(kLeaders[leader].key, "xxchahero"))  // (0.57.1: Thunder's Edge's version)
        return "ACTION: Place any combination of up to 4 PDS or mechs onto planets you control; ready each planet that you place a unit "
               "on. Then, purge this card.";  // (the Xxcha review, 2026-11-13: the card)
    if (codexTexts())
        for (const KT& k : kLeaderOmega)
            if (!std::strcmp(k.key, kLeaders[leader].key)) return k.text;
    return FIND(kLeaderText, kLeaders[leader].key);
}
const char* leaderUnlock(int leader) {
    if (leader < 0 || leader >= kLeaderCount) return "";
    if (kLeaders[leader].type == kAgent) return "Ready from the start of the game.";
    if (kLeaders[leader].type == kHero) return "Have 3 scored objectives.";
    return FIND(kUnlock, kLeaders[leader].key);
}

const char* unitAbilityText(int unit) {
    if (unit < 0 || unit >= kUnitCount) return "";
    if (teNaaluTexts() && !std::strcmp(kUnits[unit].key, "naalu_mech"))  // (fixes 27, the Naalu review: Thunder's Edge's version)
        return "DEPLOY: When another player gains a relic, place 1 mech on any planet you control.";
    if (codexTexts() && !std::strcmp(kUnits[unit].key, "naalu_mech"))
        return "Other players cannot use ANTI-FIGHTER BARRAGE against your units in this system.";
    return FIND(kUnitText, kUnits[unit].key);
}

const char* factionNoteName(int faction, int type) {
    if (faction < 0 || faction >= kFactionCount) return "";
    if (type == kNoteAlliance) return "Alliance";
    if (type == kNoteFaction && codexTexts()) {  // 0.43.3: the Codex's Omega notes carry the sign
        static const char* kOmegaNotes[][2] = {{"letnev", "War Funding \xCE\xA9"},        {"yin", "Greyfire Mutagen \xCE\xA9"},
                                               {"arborec", "Stymie \xCE\xA9"},            {"winnu", "Acquiescence \xCE\xA9"},
                                               {"l1z1x", "Cybernetic Enhancements \xCE\xA9"}};
        for (const auto& n : kOmegaNotes)
            if (!std::strcmp(kFactions[faction].key, n[0])) return n[1];
    }
    // (0.44.1: the Veldyr before the generic faction note - their first Branch Office, Tax Haven, had no name)
    if (!std::strcmp(kFactions[faction].key, "veldyr") && type >= kNoteFaction && type <= kNoteFaction4) {  // Discordant Stars (0.42.0)
        static const char* kBranch[4] = {"Branch Office: Tax Haven", "Branch Office: Broadcast Hub", "Branch Office: Reserve Bank",
                                         "Branch Office: Orbital Shipyard"};
        return kBranch[type - kNoteFaction];
    }
    if (type == kNoteFaction) return FIND(kNoteName, kFactions[faction].key);
    if (type == kNoteFaction2 && !std::strcmp(kFactions[faction].key, "empyrean")) return "Blood Pact";
    return "";
}

const char* factionNoteText(int faction, int type) {
    if (faction < 0 || faction >= kFactionCount) return "";
    if (type == kNoteAlliance)
        return "When you receive this card, if you are not its owner, you must place it faceup in your play area. While this card is in "
               "your play area, you can use its owner's commander ability, if it is unlocked. When you activate a system that contains 1 "
               "or more of its owner's units, return this card to its owner.";
    if (type == kNoteFaction && codexTexts()) {
        if (!std::strcmp(kFactions[faction].key, "letnev"))
            return "After you and your opponent roll dice during space combat: You may reroll all of your opponent's dice. You may reroll any number of your dice. Then, return this card to the Letnev player.";
        if (!std::strcmp(kFactions[faction].key, "arborec"))
            return "After another player moves ships into a system that contains 1 or more of your units: You may place 1 command token from that player's reinforcements in any non-home system. Then, return this card to the Arborec player.";
        if (!std::strcmp(kFactions[faction].key, "l1z1x"))  // (the L1Z1X review: the Omega card; the original above without the Codex)
            return "When you gain command tokens during the status phase: Gain 1 additional command token. Then, return this card to the L1Z1X player.";
        if (!std::strcmp(kFactions[faction].key, "winnu"))  // (the Winnu review: the Omega card; the original above without the Codex)
            return "When the Winnu player resolves a strategic action: You do not have to spend or place a command token to resolve the secondary ability of that strategy card. Then, return this card to the Winnu player.";
    }
    if (!std::strcmp(kFactions[faction].key, "veldyr") && type >= kNoteFaction && type <= kNoteFaction4)  // Discordant Stars (0.42.0)
        return type <= kNoteFaction2
                   ? "When you receive this card, you must attach it to a non-home planet you control other than Mecatol Rex that does not have a "
                     "Branch Office attachment. Its influence value is increased by 1."
                   : "When you receive this card, you must attach it to a non-home planet you control other than Mecatol Rex that does not have a "
                     "Branch Office attachment. Its resource value is increased by 1.";
    if (type == kNoteFaction) return FIND(kNoteText, kFactions[faction].key);
    if (type == kNoteFaction2 && !std::strcmp(kFactions[faction].key, "empyrean"))
        return "ACTION: Place this card faceup in your play area. When you and the Empyrean player cast votes for the same outcome, cast 4 "
               "additional votes for that outcome. If you activate a system that contains 1 or more of the Empyrean player's units, return "
               "this card to the Empyrean player.";  // (the Empyrean review: the card's wording)
    return "";
}

}  // namespace ti
