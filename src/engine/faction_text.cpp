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
                       "veldyr", "lizho", "mirveda", "ghoti"};                                // Discordant Stars batch 6 (0.42.0)

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
    {"Zealous", "When you spend a command token on Warfare's secondary ability, you may resolve its primary ability instead."},
    {"Ruthless", "At the start of a ground combat on an exhausted planet you don't control: 1 hit on one of your opponent's ground forces there."},
    // Vaylerian Scourge
    {"Cargo Raiders", "In the first round of a space combat, your opponent can only announce a retreat by spending 1 trade good."},
    {"Scour", "Once per tactical action, after you gain control of a planet: discard 1 action card to ready it."},
    {"Raze", "After another player's structures are destroyed on a planet where you have units: you may refill your commodities."},
    // Lanefir Remnants
    {"A New Edifice", "You can't use technology specialties. When you would gain a relic, purge it instead and explore up to 3 of your planets."},
    {"Iconoclasm", "Researching a technology that isn't a unit upgrade, you may purge 1 relic fragment to ignore 1 prerequisite."},
    {"War Stories", "Once per action, after you win a combat: explore one of your planets - or, if the system has no planets, its frontier token."},
    // Kyro Sodality
    {"Contagion", "After you resolve either ability of Politics: 1 infantry from your reinforcements invades a planet in or next to a system with one of your planets."},
    {"Plague Reservoir", "Once per action, invading a planet with your units, you may fight a ground combat there even without enemy ground forces. You win it (Dacxive Animators: 1 more infantry)."},
    {"Subversive", "A combat you take part in that would end in a draw counts as won by you."},
    // Cheiran Hordes
    {"Teeming", "You have 2 more dreadnoughts and 1 more mech in your reinforcements."},
    {"Moult", "After you win a space combat as the defender: produce 1 ship there, 1 cheaper for each of your non-fighter ships destroyed in it."},
    {"Byssus", "Your mechs on planets you control count as structures for your own abilities (not for objectives)."},
    // Celdauri Trade Confederation
    {"Projection of Power", "At the start of a space combat in or next to a system with your space dock, one of your ships there gains Anti-Fighter Barrage 6."},
    {"Industrialists", "You have 1 more space dock in your reinforcements."},
    // Discordant Stars batch 2 (0.38.0) - L'tokk Khrask
    {"Lithoids", "In the agenda phase your planets cast votes equal to their resources instead of their influence."},
    {"Garden Worlds", "Your planets with no ground forces on them have 1 more resource."},
    {"Meteor Slings", "Bombarding another player's planet, you may turn any of your hits into infantry from your reinforcements landing there (they fight in the ground combat)."},
    // Zealots of Rhodun
    {"Conspirators", "Once per agenda phase, after an agenda is revealed and if you aren't the speaker: you may vote after the speaker."},
    {"Ancient Knowledge", "Each technology specialty you use ignores 1 more prerequisite of its colour, and exhausting a planet for its specialty gives you 1 commodity."},
    // Zelian Purifier
    {"Obsessive Designs", "In the action phase, after you research a unit upgrade: produce units of that type at a space dock in your home system, 2 cheaper in all."},
    {"Biophobic", "In the agenda phase each planet you exhaust casts exactly 1 vote."},
    {"Paranoia", "Nothing but your own command tokens can stop you activating, or moving ships into, your home system."},
    // Roh'Dhna Mechatronics
    {"Industrious", "After you place a space dock in a system without other players' ships: pay 6 resources to replace it with a war sun in space."},
    {"Recycled Materials", "After you activate a system: return a cruiser, carrier or dreadnought of yours there to your reinforcements for trade goods equal to its cost minus 1."},
    {"Orbital Foundries", "For objectives, each of your war suns counts as a structure on one of your planets."},
    // Tnelis Syndicate
    {"Plausible Deniability", "When you draw secret objectives, draw 1 more, then shuffle 1 of yours back into the deck."},
    {"Information Brokers", "You may hold 1 more unscored secret objective."},
    {"Stealth Insertion", "Your units put onto a planet with another player's units must fight in its ground combat."},
    // Monks of Kolume
    {"Starfall Gunnery", "On your own actions up to 3 of your non-fighter ships have Space Cannon 8; on other players' actions only 1 of your units fires its space cannon."},
    {"Deliberate Action", "You can't rearrange your command tokens in the status phase. When you pass, you may put 1 token from your reinforcements into an empty pool."},
    {"Meditation", "Action: spend 1 strategy token to ready one of your technologies."},
    // Bentor Conglomerate
    {"Secret Maps", "At the end of your tactical action: you may explore 1 planet in the active system that has your unit with Production (or is one) and that you didn't explore during it."},
    {"Ancient Blueprints", "The first time you gain a cultural, hazardous, industrial and unknown relic fragment, a Fragment token of that kind goes on your faction sheet."},
    {"Fortune Seekers", "Once per action, after you explore a planet or a frontier token: gain 1 commodity."},
    // Dih-Mohn Flotilla
    {"Capital Fleet", "Your destroyers count as half a ship against your fleet pool."},
    {"Migrant Fleet", "After you explore a frontier token: you may explore one of your planets in a system next to it."},
    {"Flotilla", "You can't have more infantry than non-fighter ships in the space area of a system."},
    // Florzen Profiteers
    {"Mercenaries", "At the start of a space combat: you may remove up to 2 of your fighters from a system next to it; a player in the combat places that many fighters there."},
    {"Data Leak", "When you would gain a relic: draw 1 more, keep 1 and shuffle the other back into the relic deck."},
    {"Shadow Markets", "When you explore a planet, you may use the deck of a trait one of your other planets has."},
    // Gledge Union
    {"Mantle Cracking", "Action: put a Core token (3 in all) on one of your non-home planets other than Mecatol Rex and gain 4 trade goods."},
    {"Celestial Reclamation", "A planet with a Core token has base values of 2 resources and 0 influence; the token never leaves it."},
    {"Deep Mining", "When you would explore a planet with your mech or structure on it, you may gain 1 trade good instead."},
    // Augurs of Ilyxum
    {"Oracle AI", "After the speaker reveals a public objective, the next unrevealed one goes on your faction sheet - you may look at it."},
    {"Limited Vision", "A stage II objective can go on your sheet only once all stage I objectives are revealed."},
    {"Probability Algorithms", "The objective on your faction sheet is the next public objective the speaker reveals."},
    // Berserkers of Kjalengard
    {"Heroism", "Your fighters and infantry destroyed in combat are captured. When you pass: for each Glory token on the board, you may return 4 captured units for 1 command token."},
    {"Glory", "After you win a combat: place or move a Glory token (3 in all) into that system - or spend a strategy token to research a unit upgrade of a unit type that fought."},
    {"Valor", "In a system with a Glory token, each natural 10 on a combat roll makes 1 more hit."},
    // Kollecc Society
    {"Cloaked Fleets", "After you produce ships, up to 2 of them may go onto your faction sheet (captured - cloaked)."},
    {"Shroud of Lith", "After you move into a system with your non-fighter ships: up to 2 cloaked ships and 2 cloaked ground forces appear there."},
    {"Treasure Hunters", "At the start of your turn you may look at the top card of a cultural, hazardous or industrial deck, and at the top relic."},
    // Glimmer of Mortheus
    {"Facsimile", "At the start of a space combat you defend: pay influence equal to the cost of one of the attacker's ships to put one of that type in."},
    {"Illusory Presence", "In the agenda phase, after another player votes: exhaust up to 2 planets - they cast 1 vote more than those planets' influence."},
    // Vaden Banking Clans
    {"Fine Print", "After a player resolves the secondary of your strategy card: 1 of their control tokens goes on your faction sheet."},
    {"Collateralized Loans", "After an opponent's ship is destroyed in space combat: take back 1 of their tokens to put a ship of that type in the system."},
    {"Binding Debts", "At the start of the status phase each neighbour may pay you 1 trade good to take back up to 2 of their tokens."},
    // Savages of Cymiae
    {"Autonetic Memory", "When you draw action cards, draw 1 less: take 1 from the discard pile instead, or put 1 infantry on one of your planets."},
    {"Cybernetic Madness", "After taking a card from the discard pile, discard 1. Your resolved action cards are purged, not discarded."},
    // Nivyn Star Kings
    {"Celestial Guides", "Your units don't roll for gravity rifts; anomalies in or next to systems with your structures don't slow you."},
    {"Singularity Point", "The system with the Wound token is both a nebula and a gravity rift."},
    {"Voidsailors", "When you explore a frontier token, draw 1 more card, resolve one and shuffle the other back."},
    // Olradin League
    {"Policies", "Three policy cards, each on its + or - side (chosen at the start): The People, The Environment, The Economy."},
    // Free Systems Compact
    {"Rally to the Cause", "Once per action, after you produce ships at home: produce up to 2 ships in a system with a cultural, hazardous or industrial planet, no legendary planet and nobody else's units."},
    {"Diplomats", "Once per action, when you pay: exhaust the card of a planet nobody controls (not Mecatol Rex) for its resources or influence."},
    {"Free People", "The cards of the planets nobody controls lie face up on the board, ready for your Diplomats."},
    // Edyn Mandate
    {"Grace", "Once per action phase, after a strategy card's primary: resolve the secondary of a lower card nobody has used yet, without a command token."},
    {"Decree", "Other players' ships can't move through anomalies with your ground forces in them - your Sigils included."},
    {"Radiance", "After an agenda is revealed you may predict its outcome; if you're right, another player's command token goes into one of your Sigils."},
    // Nokar Sellships
    {"Hired Guns", "When another player activates a system with your ships: offer them up to 3 for 1 trade good each - they fight as theirs and may be kept at the end of the action."},
    {"Private Fleet", "4 more destroyers in your reinforcements."},
    {"Desperados", "When you produce, 1 destroyer doesn't count against the limit. Your destroyers move 1 further when you aren't the active player."},
    // Shipwrights of Axis
    {"Military Industrial Complex", "After you gain or refill commodities: spend them to put Axis Order cards in your play area. You can't give commodities in transactions."},
    {"Arms Dealers", "Axis Order cards change hands in deals; their holders call in the ships at the end of their turn. You can't use them yourself."},
    // Myko-Mentori
    {"Prescient Memories", "At the start of each strategy phase, roll your 4 Omen dice."},
    {"Divination", "Before you roll a die you may spend an Omen die instead: the roll takes its result."},
    {"Necrophage", "+1 commodity value for each space dock you have. After the first round of a combat: gain 1 commodity or turn 1 into a trade good."},
    // Ghemina Raiders
    {"The Lady & The Lord", "Two flagships - the Lady and the Lord - and two heroes."},
    {"Rule of Two", "In a space combat with exactly 2 of your non-fighter ships, both of the same type: +2 to their rolls."},
    // Veldyr Sovereignty
    {"Corporate Entity", "You have 4 faction promissory notes: the Branch Offices. They don't count toward objectives."},
    {"Holding Company", "At the start of the status phase, for each planet with a Branch Office: gain 1 commodity or turn 1 into a trade good."},
    {"Targeted Acquisition", "At the start of the status phase you may give one of your Branch Offices to a neighbour."},
    // Li-Zho Dynasty
    {"Cunning", "After your tactical action in a system: attach a Trap from your reinforcements to a planet there with your infantry."},
    {"Subterfuge", "Traps lie face down - only you know which is which. They don't count toward objectives."},
    // Mirveda Protectorate
    {"Privileged Citizenry", "Each system holds as many of your PDS as you have fleet tokens; for objectives your PDS count as structures on your planets."},
    {"Combat Drones", "Your space docks can't produce infantry. At the start of an invasion your fighters there may become infantry (no capacity needed during it)."},
    // Ghoti Wayfarers
    {"Abyssal Embrace", "Your home tile is Ghoti Space - an empty system."},
    {"Mobile Command", "The system with your flagship is your home system. While your flagship is off the board you can't score public objectives."},
    {"Spawning Grounds", "You hold the Ghoti planet card (3/3) - it isn't on the board and can't be lost. Ships moving into a system without planets: 1 commodity."},
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
    {"kortaliagent", "When a player wins a ground combat: exhaust to let them take 1 of their opponent's relic fragments at random."},
    {"kortalicommander", "In the first round of a combat, you may cancel 1 hit against your units."},
    {"kortalihero", "When you win a combat: purge her to take 1 of your opponent's relics, then gain 1 command token for each legendary planet or planet with a technology specialty you control."},
    {"vaylerianagent", "After a player gains control of a planet: exhaust to let them draw 1 action card."},
    {"vayleriancommander", "After you activate a system: your ships have 2 more capacity this tactical action."},
    {"vaylerianhero", "After you activate a system: purge him to take back 1 of your command tokens from the board and gain 1 command token; your ships get +1 move and +1 to combat rolls this tactical action."},
    {"lanefiragent", "When a player explores: exhaust to let them discard the card they drew and draw another."},
    {"lanefircommander", "After you explore a planet: you may put 1 infantry from your reinforcements on it."},
    {"lanefirhero", "Action: reveal 3 relics. Gain 1 command token for each relic in play, and you may swap other players' relics for revealed ones. The rest are shuffled back; purged."},
    {"kyroagent", "After a player loses or draws a ground combat: exhaust to let them refill their commodities; you put infantry equal to their commodity value minus 1 on one of your planets."},
    {"kyrocommander", "When you cast votes: remove up to 4 of your infantry from the board for 1 more vote each."},
    {"kyrohero", "At the start of the status phase: attach it to one of your strategy cards for good - that card has initiative 9, and trade goods placed on it go to you."},
    {"cheiranagent", "At the end of a player's turn: exhaust to let them move 1 of their command tokens to a neighbouring system with their structure and none of their tokens."},
    {"cheirancommander", "While defending a ground combat: cancel hits against you for 1 commodity or trade good each."},
    {"cheiranhero", "Action: your starting units again, from your reinforcements, into a system with your structure and no other ships (ground forces on your planets). Purged afterwards."},
    {"celdauriagent", "At the end of a player's tactical action: exhaust to let them pay 2 trade goods or commodities for a space dock on their planet in that system."},
    {"celdauricommander", "After you activate a system with your space dock, gain 1 commodity. Your systems with space docks are next to each other for your ships."},
    {"celdaurihero", "After a player activates a system other than Mecatol Rex: purge him to place a space dock on your planet there, turn your commodities into trade goods and use Production there."},
    // Discordant Stars batch 2 (0.38.0)
    {"khraskagent", "When a player spends influence: exhaust to let them use the resources of 1 of their non-home planets as influence as well."},
    {"khraskcommander", "At the start of an invasion in a system with planets: put 1 infantry from your reinforcements in its space area, if you have room."},
    {"khraskhero", "Action: purge it to exhaust or ready up to 4 planets."},
    {"zealotsagent", "After a player researches a technology: exhaust to let them produce 1 ship at home or in a system with their planet that has a technology specialty."},
    {"zealotscommander", "Researching, you may use 1 of your planets' technology specialties to ignore any one prerequisite - of any "
                         "colour, not only its own. (Using a specialty exhausts that planet, as always.)"},
    {"zealotshero", "Action: gain up to 3 technologies that aren't faction technologies. At the start of the next strategy phase you keep only 1 of them."},
    {"zelianagent", "At the start of an invasion: exhaust to let the active player swap 1 of their infantry in the space area for a mech."},
    {"zeliancommander", "Your dreadnoughts and war suns without Anti-Fighter Barrage have Anti-Fighter Barrage 5."},
    {"zelianhero", "After you move a unit with Bombardment into a non-home system (not Mecatol Rex): gain trade goods equal to its planets' resources, destroy every unit on them and the planets themselves; purged."},
    {"rohdhnaagent", "After a player's units use Production: exhaust to let them gain 1 command token."},
    {"rohdhnacommander", "When you spend a command token on Construction's secondary ability, you may resolve its primary ability instead."},
    {"rohdhnahero", "After you move ships into the active system: purge him to produce ships there with every unit's Production as if it were yours, blockade or not."},
    {"tnelisagent", "At the start of an invasion: exhaust to let one of the active player's ships use its Anti-Fighter Barrage as Bombardment."},
    {"tneliscommander", "After you activate a system: you may damage one of your ships to let one ship move 1 further this tactical action."},
    {"tnelishero", "After another player scores a secret objective: attach it there - you can score that objective like a public one; purged when you do."},
    {"kolumeagent", "Action: exhaust to let a player move 1 of their command tokens to another pool."},
    {"kolumecommander", "+1 to your units' unit ability rolls (space cannon, bombardment, anti-fighter barrage)."},
    {"kolumehero", "Action: this round, after each of your component actions, one of your units fires its space cannon at ships in or next to its system. Purged at the end of the round."},
    {"bentoragent", "Action: exhaust to give a player 1 commodity for each of your Fragment tokens (any beyond their commodity value become trade goods)."},
    {"bentorcommander", "Your commodity value is 1 higher."},
    {"bentorhero", "Action: explore each planet you control, in any order. Then purge him."},
    {"dihmohnagent", "When a player commits units to a planet: exhaust to let them put 1 infantry from their reinforcements there too."},
    {"dihmohncommander", "Counts as a red technology prerequisite. At the start of a space combat where you have 3 or more kinds of non-fighter ships: 1 hit on your opponent's ships."},
    {"dihmohnhero", "After the dice of a space combat round are rolled: purge him - every ship in the system becomes damaged, and your ships can't be destroyed this round."},
    {"florzenagent", "At the start of a player's turn: exhaust to let them move an attachment from one of their non-home planets to one of theirs in a system next to it (not Mecatol Rex)."},
    {"florzencommander", "After the second agenda is revealed: ready one of your planets and explore it."},
    {"florzenhero", "Action: 2 fighters in each system with your planets and no other ships; then move 1 attachment from a planet in a system with your ships to your home system. Purge her."},
    {"gledgeagent", "When a player's units use Production: exhaust to give them 3 more Production."},
    {"gledgecommander", "When your units use Production: 1 more for each space dock you control."},
    {"gledgehero", "Action: reveal exploration cards (not frontier) until 3 attachments; attach each to one of your planets of its type, discard the rest. Then purge him."},
    {"augersagent", "After a player explores a legendary planet or one with a technology specialty: exhaust to give them 2 trade goods."},
    {"augerscommander", "Counts as a yellow technology prerequisite. When you cast votes, cast 1 more for every 2 technologies you own."},
    {"augershero", "Action: draw 3 public objectives; one may replace the objective on your faction sheet, the rest go to the bottom. Then purge her."},
    {"kjalengardagent", "At the start of a combat: exhaust to move a Glory token into that system; the active player gains 1 commodity per neighbour."},
    {"kjalengardcommander", "Researching a unit upgrade: each of your unit upgrades covers 1 prerequisite of a colour both ask for."},
    {"kjalengardhero", "Action: for each Glory token, take back 1 of your command tokens in or next to its system and gain 1 command token. Then purge him."},
    {"kolleccagent", "At the start of a player's turn: exhaust to let them reveal 2 cards of an exploration deck matching one of their planets - they keep the relic fragments."},
    {"kollecccommander", "Counts as a blue technology prerequisite. Retreating from a system without your command token places no token."},
    {"kollecchero", "Action: draw a relic for each player, keep 1, shuffle the rest back. Then purge him."},
    {"mortheusagent", "When a player produces units: exhaust to let one of the new ships go to a neighbouring system without other players' ships."},
    {"mortheuscommander", "At the start of a space combat on another player's turn: one of your non-fighter ships has Sustain Damage for the combat."},
    {"mortheushero", "Action: in each system with your ships, put one more of one of those ship types. Then purge him."},
    {"vadenagent", "At the start of a player's turn: exhaust to give them commodities equal to one of their planets' influence."},
    {"vadencommander", "At the start of the status phase: 1 trade good for each secret objective you scored, 1 commodity for each public one."},
    {"vadenhero", "Action: each token on your sheet goes back - its owner gives you 1 trade good, 2 commodities or a promissory note. Then purge him."},
    {"cymiaeagent", "When a player discards or purges an action card: exhaust to let them draw 1."},
    {"cymiaecommander", "After you take a planet in a tactical action: discard an action card to put a mech on it."},
    {"cymiaehero", "Action: reveal 1 action card more than there are players; give 1 to each other player, keep the rest; they each discard 1. Then purge it."},
    {"nivynagent", "When a player's unit would use an ability (not Production) in or next to an anomaly: exhaust to stop that unit."},
    {"freesystemsagent", "While ready, it has the abilities of the legendary planets any player controls: exhaust it to use one."},
    {"freesystemscommander", "After you take a non-home planet in a tactical action: produce 1 ship in that system if your ships are there."},
    {"freesystemshero", "Action: ready one of your non-home planets (not Mecatol Rex) and clear every unit from it - no units can be put there again. Then purge him."},
    {"edynagent", "After a player passes: exhaust to let a player of your choice perform 1 action; then look at the top agenda and you may discard it."},
    {"edyncommander", "When you could score a public objective, you may draw 1 secret objective instead."},
    {"edynhero", "Action: for each Sigil, draw an agenda and resolve it as if you alone voted, for the outcome you choose. Then purge him."},
    {"nokaragent", "After movement: exhaust to let the active player put 1 destroyer in the active system - then you may trade with them."},
    {"nokarcommander", "In a space combat after you announce a retreat: cancel up to 2 hits. You may retreat to systems next to your home."},
    {"nokarhero", "Action: until the end of the round, each of your planets has Production 3. Then purge him."},
    {"axisagent", "Action: exhaust to let a player put a cruiser or destroyer with their ships - another player: you gain 2 commodities."},
    {"axiscommander", "After a player uses an Axis Order: you may spend 6 resources to gain that unit's upgrade."},
    {"axishero", "Action: give Axis Orders from your play area to other players; each one gives you a promissory note. Then purge her."},
    {"mykomentoriagent", "Before another player rolls: exhaust to make one of their dice one of your Omen dice (it stays near your sheet)."},
    {"mykomentoricommander", "When another player's space combat hits come at you: spend 1 commodity or trade good to cancel 1."},
    {"mykomentorihero", "Other players' heroes that would be purged are bound to it instead; resolve one of them as your own. Purged with everything bound to it."},
    {"gheminaagent", "When the active player makes hits in a combat round: exhaust for 1 more hit."},
    {"gheminacommander", "After you win a space combat where there are no planets, or a ground combat on a planet with a structure: gain 1 trade good."},
    {"gheminaherolord", "Action: take a non-home planet (not Mecatol Rex) without other players' units, in or next to a system with your flagship; ready it. Then purge him."},
    {"gheminaherolady", "Action: destroy every unit on a non-home planet (not Mecatol Rex) in or next to a system with your flagship. Then purge her."},
    {"veldyragent", "When a player researches: exhaust to let them pay with one planet's influence as resources."},
    {"veldyrcommander", "Researching a technology owned by a player with a Branch Office planet: ignore 1 prerequisite."},
    {"veldyrhero", "Action: for each Branch Office, gain a unit upgrade of a unit type its planet's owner has upgraded. Then purge him."},
    {"lizhoagent", "At the end of a player's turn: exhaust to let them put 2 fighters with their ships."},
    {"lizhocommander", "In a combat round with only 1 of your non-fighter ships (or 1 infantry) fighting: 1 unit rolls 1 more die."},
    {"lizhohero", "Action: attach or move each Trap to any planet; up to 12 fighters in systems with your Traps and no other ships. Then purge her."},
    {"mirvedaagent", "After a player gains a unit upgrade: exhaust - they may spend a strategy token to research a technology of one of its prerequisite colours."},
    {"mirvedacommander", "Your space cannon may also fire at units next to their system."},
    {"mirvedahero", "Action: up to 3 PDS; then all your PDS fire at one system - space cannon at the ships, bombardment at one planet. Then purge it."},
    {"ghotiagent", "When a player produces: exhaust for 2 more units, or 1 trade good."},
    {"ghoticommander", "After you activate a system: systems without planets are next to each other during this action."},
    {"ghotihero", "Action: in each system with your ships, one of them becomes a ship costing up to 2 more. Then purge her."},
    {"nivyncommander", "When your damaged units roll for combat, up to 2 of them roll 1 more die."},
    {"nivynhero", "Action: every unit that can sustain damage becomes damaged (not your mechs); this round your units are next to the Wound. Purged at the end of the round."},
    {"olradinagent", "After a player exhausts a cultural, hazardous or industrial planet: exhaust to let them ready one of theirs of another trait."},
    {"olradincommander", "When you pass: exhaust a non-home planet (not Mecatol Rex) for trade goods equal to its resources or influence."},
    {"olradinhero", "Action: 1 infantry on each planet you control; you may flip 1 policy. Then purge him."},
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
    {"vayleriancommander", "Control 3 planets with the same trait."},
    {"lanefircommander", "7 relic fragments have been purged in this game."},
    {"kyrocommander", "Have 6 infantry and 6 fighters on the board."},
    {"cheirancommander", "Have 4 structures on non-home planets."},
    {"celdauricommander", "Have a space dock outside and not next to your home system."},
    {"khraskcommander", "Win a ground combat on a planet you don't control."},
    {"zealotscommander", "Own a technology with 2 or more prerequisites."},
    {"zeliancommander", "Have ships in 2 systems with asteroid fields."},
    {"rohdhnacommander", "Have units with Production 7 or more in one system."},
    {"tneliscommander", "Have 6 destroyers on the board."},
    {"kolumecommander", "Ready one of your technologies in the action phase."},
    {"bentorcommander", "Have 3 Fragment tokens on your faction sheet."},
    {"dihmohncommander", "Research a ship unit upgrade."},
    {"florzencommander", "Have the outcome you voted for resolved."},
    {"gledgecommander", "Have non-fighter ships costing 10 or more in one system."},
    {"augerscommander", "Win a combat in a system with a legendary planet or one with a technology specialty."},
    {"kjalengardcommander", "Have 2 Glory tokens on the board."},
    {"kollecccommander", "Have 4 relic fragments."},
    {"mortheuscommander", "Have ships in 3 systems that aren't your home or next to it."},
    {"vadencommander", "Have tokens of different players on your sheet - one for every 2 players in the game."},
    {"cymiaecommander", "Have 10 infantry on the board."},
    {"nivyncommander", "Have units in 2 anomalies other than your home."},
    {"olradincommander", "Control a cultural, a hazardous and an industrial planet."},
    {"freesystemscommander", "Every non-legendary planet on the board is controlled."},
    {"edyncommander", "Have a law in play."},
    {"nokarcommander", "Another player takes over your ships (Hired Guns)."},
    {"axiscommander", "Have Axis Orders of all 4 kinds in players' play areas."},
    {"mykomentoricommander", "Have 4 commodities."},
    {"gheminacommander", "Have both of your flagships on the board."},
    {"veldyrcommander", "Two other players control planets with Branch Offices."},
    {"lizhocommander", "Have 4 Traps on the board."},
    {"mirvedacommander", "Own 2 unit upgrades."},
    {"ghoticommander", "Have units in 3 systems without planets."},
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
    {"kortali_flagship", "After you win a space combat here: let it become damaged to gain 1 command token."},
    {"kortali_mech", "Its Planetary Shield can't be taken away (not even by war suns)."},
    {"vaylerian_flagship", "At the start of a space combat here: choose a neighbouring system your opponent can't retreat to."},
    {"vaylerian_cruiser", "Raider I - if your opponent can't announce a retreat this round, its hits go to non-fighter ships. It carries ground forces only."},
    {"vaylerian_cruiser2", "Raider II - if your opponent can't announce a retreat this round, its hits can't be cancelled and go to non-fighter ships."},
    {"vaylerian_mech", "During your tactical actions, space cannon hits can't be assigned to one of your ships in its system."},
    {"lanefir_flagship", "At the end of a tactical action here: you may explore one of your planets in this system."},
    {"lanefir_mech", "Deploy: at the start of your turn, purge 1 relic fragment to put a mech on one of your planets."},
    {"kyro_flagship", "When you commit units to a planet here, you may commit 1 more infantry from your reinforcements."},
    {"kyro_mech", "When it would be destroyed while damaged: discard 1 action card to repair it instead."},
    {"cheiran_flagship", "Rolls 1 more die in combat if its system has or is next to one of your structures."},
    {"cheiran_dreadnought", "Chitin Hulk I - when it is destroyed, you may put 1 fighter in its system."},
    {"cheiran_dreadnought2", "Chitin Hulk II - Direct Hit can't destroy it; when it is destroyed, you may put 1 fighter or 1 destroyer in its system."},
    {"cheiran_mech", "When it is destroyed, you may put 1 infantry on its planet."},
    {"celdauri_flagship", "You may produce ships with other players' space docks in its system."},
    {"celdauri_mech", "Deploy: after a player activates a system, pay 1 commodity or trade good to put a mech on your planet with a space dock there."},
    {"celdauri_spacedock", "Trade Port I - Production = 2 + its planet's resources or influence. Up to 3 fighters here need no capacity. Anti-Fighter Barrage 6 (x2)."},
    {"celdauri_spacedock2", "Trade Port II - Production = 4 + its planet's resources or influence. Up to 3 fighters need no capacity. Its Anti-Fighter Barrage 6 (x2) fires every round."},
    // Discordant Stars batch 2 (0.38.0)
    {"khrask_flagship", "At the start of a space combat here: 2 of your ships without Sustain Damage gain it for this combat."},
    {"khrask_cruiser", "Shattered Sky I - Bombardment 8."},
    {"khrask_cruiser2", "Shattered Sky II - 2 capacity, Bombardment 6."},
    {"khrask_mech", "In the first round of a ground combat on its planet, only your mechs roll - nobody else."},
    {"zealots_flagship", "+1 to its combat rolls for each unit upgrade your opponent owns."},
    {"zealots_mech", "+1 to its combat rolls for each faction technology your opponent owns."},
    {"zelian_flagship", "Rolls 1 more combat die for each asteroid field next to it."},
    {"zelian_infantry", "Impactor I - Bombardment 9 (from the space area, before landing)."},
    {"zelian_infantry2", "Impactor II - Bombardment 8. When destroyed: on 6+ it comes back home at the start of your next turn."},
    {"zelian_mech", "At the start of an invasion, if it's in the space area: remove it to destroy 1 unit on a planet there."},
    {"rohdhna_flagship", "Deploy: after you activate a system, pay 4 resources to replace 1 of your non-fighter ships there with it."},
    {"rohdhna_warsun", "Terrafactory I - Production 5; it can't move and can't be produced (Industrious makes it)."},
    {"rohdhna_warsun2", "Terrafactory II - moves 2, Production 5, Bombardment 3 (x3); other players' units here lose Planetary Shield."},
    {"rohdhna_mech", "Deploy: after Recycled Materials, put a mech in that system's space area or on your planet there."},
    {"tnelis_flagship", "At the start of each combat round: one of your opponent's ships rolls 1 die less."},
    {"tnelis_destroyer", "Blockade Runner I - Anti-Fighter Barrage 9 (x3)."},
    {"tnelis_destroyer2", "Blockade Runner II - Anti-Fighter Barrage 6 (x4); it moves through systems with other players' ships."},
    {"tnelis_mech", "Deploy: after you move a destroyer into a non-home system (not Mecatol Rex), pay 3 resources to put a mech on a planet there."},
    {"kolume_flagship", "Space Cannon 7; your units' space cannon hits in its system can't be cancelled."},
    {"kolume_mech", "Space Cannon 8 (x2), but its hits only destroy fighters. When you spend a strategy token, it is repaired."},
    {"bentor_flagship", "+1 to its combat and unit ability rolls for each Fragment token on your sheet."},
    {"bentor_mech", "Deploy: when a Fragment token goes on your sheet, put a mech on one of your planets."},
    {"dihmohn_flagship", "After it moves into the active system: produce up to 2 units there with a combined cost of 4 or less."},
    {"dihmohn_dreadnought", "Aegis I - when another of your ships here would be destroyed in combat, this ship may become damaged instead."},
    {"dihmohn_dreadnought2", "Aegis II - combat 4, move 2; when another of your ships here would be destroyed by anything, this ship may become damaged instead."},
    {"dihmohn_mech", "At the start of a combat in its system: repair 1 of your units in that combat."},
    {"florzen_flagship", "Other players can't play action cards during a space combat in its system."},
    {"florzen_fighter", "Corsair I - moves on its own (move 1); those beyond your ships' capacity count against your fleet pool. Anti-Fighter Barrage 9."},
    {"florzen_fighter2", "Corsair II - combat 8, move 3, moves on its own; those beyond capacity count against your fleet pool. Anti-Fighter Barrage 8."},
    {"florzen_mech", "You may pay for it with influence instead of resources."},
    {"gledge_flagship", "1 more die on its combat and ability rolls for each mech in or next to its system."},
    {"gledge_mech", "When you exhaust its planet for resources, 1 of its influence counts as a resource too."},
    {"gledge_pds", "Orion Platform I - when its space cannon rolls a natural 9 or 10 against another player, explore its planet."},
    {"gledge_pds2", "Orion Platform II - Space Cannon 5, also at ships next door; when it hits another player's units, explore its planet."},
    {"augers_flagship", "1 more die on its combat rolls for each secret objective you have scored."},
    {"augers_mech", "Deploy: after you research a technology, put a mech on one of your legendary or specialty planets."},
    {"kjalengard_flagship", "After the first round of a combat in its system: put up to 2 of your captured units back into the fight."},
    {"kjalengard_carrier", "Star Dragon I - it may ignore the movement effects of anomalies (not supernovas)."},
    {"kjalengard_carrier2", "Star Dragon II - move 2, capacity 6, combat 7; it may ignore the movement effects of all anomalies."},
    {"kjalengard_mech", "When you pass: 1 infantry from your reinforcements on its planet if a Glory token is in or next to its system."},
    {"kollecc_flagship", "When it retreats, every retreating unit may go onto your faction sheet (cloaked) instead."},
    {"kollecc_mech", "At the end of a tactical action in its system: up to 2 ground forces from its planet go onto your faction sheet (cloaked)."},
    {"mortheus_flagship", "After you activate its system: a frontier token there if it has no planets."},
    {"mortheus_mech", "When a player lands on a planet of yours next to its system: it may swap places with 1 of your infantry there."},
    {"vaden_flagship", "After its bombardment hits: gain 1 trade good."},
    {"vaden_mech", "At the end of a ground combat round: take back 1 of your opponent's tokens to put 1 infantry on its planet."},
    {"cymiae_flagship", "After you win a combat in its system: take 1 of your opponent's action cards at random."},
    {"cymiae_infantry", "Unholy Abomination I - combat 5."},
    {"cymiae_infantry2", "Unholy Abomination II - combat 4; destroyed, it comes back on a 6+ at the start of your turn, onto any of your planets."},
    {"cymiae_mech", "At the start of each ground combat round: repaired, if it is your only mech on the planet."},
    {"nivyn_flagship", "Other players' units in its system are removed instead of destroyed."},
    {"nivyn_mech", "Voidflare Warden I - after its system is activated: become damaged to put the Wound token there."},
    {"nivyn_mech2", "Voidflare Warden II - combat 4; after any system is activated: become damaged to put the Wound token there."},
    {"olradin_flagship", "When it moves into the active system, your other ships move 1 further this action."},
    {"olradin_mech", "Exemplar - a lone one on a planet: +1 resource (2+ \"+\" policies) or +1 influence (2+ \"-\" policies)."},
    {"olradin_mech_positive", "Exemplar Bonum - a lone one on a planet: +1 resource."},
    {"olradin_mech_negative", "Exemplar Malus - a lone one on a planet: +1 influence."},
    {"freesystems_flagship", "1 more die on its combat rolls for each planet in its system of the most common trait there."},
    {"freesystems_mech", "Deploy: after Rally to the Cause, 1 trade good for a mech on one of your planets in or next to that system."},
    {"edyn_flagship", "+1 to its combat rolls for each law in play."},
    {"edyn_mech", "Its system is a Sigil - an anomaly."},
    {"nokar_flagship", "+1 to its combat rolls for every 2 destroyers you have on the board."},
    {"nokar_destroyer", "Sabre I - destroyed in combat: roll a die, 9+ makes 1 hit on your opponent's ships."},
    {"nokar_destroyer2", "Sabre II - combat 7; destroyed in combat: roll a die, 7+ makes 1 hit on your opponent's ships."},
    {"nokar_mech", "For your retreats, its system counts as next to the active system."},
    {"axis_flagship", "After it hits in a round of space combat: repair 1 of your ships in its system."},
    {"axis_mech", "A space dock on its planet has Production 5."},
    {"mykomentori_flagship", "Once per round of space combat, when a non-fighter ship in its system is destroyed: gain 1 commodity."},
    {"mykomentori_mech", "Destroyed: on a 6+ it waits on its card; at the start of your turn it may replace one of your infantry."},
    {"mykomentori_spacedock", "Mycelium Ring I - Production: planet's resources + 2. Deploy: when you take a planet, 4 infantry there may become a space dock."},
    {"mykomentori_spacedock2", "Mycelium Ring II - Production: planet's resources + 5. Deploy: 3 infantry may become a space dock."},
    {"ghemina_flagship_lord", "The Lord: capacity 7; after a planet in its system is explored, the explorer puts 1 infantry there. The Lady: capacity 3, Bombardment 5 (x4); 1 trade good when someone else's structure in her system is destroyed."},
    {"celdauri_celagrom", "The Celagrom: at the start of a space combat it may produce 1 hit on one of the opponent's ships (you pick the ship type). It rolls 1 more die while you have another flagship on the board."},
    {"ghemina_flagship_lady", "The Lady: capacity 3, Bombardment 5 (x4); 1 trade good when someone else's structure in her system is destroyed."},
    {"ghemina_mech", "After you win a ground combat on its planet with exactly 1 other mech there: explore the planet."},
    {"ghemina_carrier", "Combat Transport I - move 2."},
    {"ghemina_carrier2", "Combat Transport II - move 2, capacity 6; in a ground combat on a planet in its system with 2 or fewer of your infantry: 1 of your dice rolls again."},
    {"veldyr_flagship", "1 more die on its combat rolls for each round of the combat already fought."},
    {"veldyr_dreadnought", "Lancer Dreadnought I - Bombardment 5, Space Cannon 8."},
    {"veldyr_dreadnought2", "Lancer Dreadnought II - move 2, Bombardment 5, Space Cannon 5; Direct Hit can't destroy it."},
    {"veldyr_mech", "After its system is activated: remove it to put a PDS on its planet."},
    {"lizho_flagship", "Only a hit assigned to it can destroy it."},
    {"lizho_fighter", "Heavy Bomber I - combat 9, Bombardment 9."},
    {"lizho_fighter2", "Heavy Bomber II - combat 8, move 2, Bombardment 8; moves on its own (beyond capacity they count against the fleet pool)."},
    {"lizho_mech", "At the start of your turn: move a Trap from the board onto its planet."},
    {"mirveda_flagship", "After each round of space combat in its system: 1 fighter."},
    {"mirveda_pds", "Gauss Cannon I - in the space area; moves and retreats like a ship (move 1); Space Cannon 6, Bombardment 6. Blockaded, it is destroyed."},
    {"mirveda_pds2", "Gauss Cannon II - move 2, capacity 1, Space Cannon 4, Bombardment 4; blockaded, it is destroyed."},
    {"mirveda_mech", "+1 to its combat rolls for every 2 unit upgrades you own."},
    {"ghoti_flagship", "It is also a space dock with Production equal to your fleet tokens."},
    {"ghoti_mech", "When you produce ships at home: 1 of them may appear in its system (unless blockaded)."},
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
    {"kortali", "Action: play it face up and take 1 of your command tokens back from the board. It goes back to the Kortali at the start of the status phase."},
    {"vaylerian", "At the start of a space combat round: the Vaylerian discard 1 action card; your ships move to a neighbouring system without other ships (a command token there) and the combat is a draw. The note goes back."},
    {"lanefir", "After you win a combat: play it face up to explore one of your planets. It goes back at the start of the Lanefir's turn."},
    {"kyro", "After an agenda is revealed: you don't vote on it, but predict its outcome. If you're right, put 3 infantry on one of your planets. The note goes back to the Kyro."},
    {"cheiran", "When one of your non-fighter ships is destroyed in a combat: put 2 fighters in that system. The note goes back to the Cheiran."},
    {"celdauri", "Goes face up: you can use the Celdauri commander (once unlocked). It goes back if you activate a system with Celdauri units."},
    {"khrask", "During your tactical action, when your bombardment hits: turn any of those hits into infantry landing on that planet. The note goes back to the Khrask."},
    {"zealots", "When you ignore a prerequisite while researching: ignore 1 more of the same colour. The note goes back to the Rhodun."},
    {"zelian", "When your bombardment makes hits: 1 hit more. The note goes back to the Zelian."},
    {"rohdhna", "At the end of your turn: attach it for good to one of your non-home planets (not Mecatol Rex) - that planet has Production 3."},
    {"tnelis", "When you draw secret objectives: draw 1 more, then shuffle 1 of yours back into the deck. The note goes back to the Tnelis."},
    {"kolume", "At the start of an invasion: play it - in this invasion other players' units lose Space Cannon and Planetary Shield. It goes back at the start of your next turn."},
    {"bentor", "At the end of your turn: attach it for good to one of your non-home planets - that planet has a technology specialty of the colour you choose."},
    {"dihmohn", "When one of your ships would be destroyed: it isn't - the note goes face up instead, and back to the Dih-Mohn at the start of the status phase."},
    {"florzen", "When you would gain a relic: draw 1 more, keep 1, shuffle the other back. The note goes back to the Florzen."},
    {"gledge", "At the end of your turn: attach it for good to one of your non-home planets - that planet has 2 more resources."},
    {"augers", "Goes face up: you know the objective on the Augurs' faction sheet. It goes back if you activate a system with their units."},
    {"kjalengard", "At the start of a space combat: +1 to your fighters' combat rolls this combat; the Kjalengard capture your fighters destroyed in it. The note goes back."},
    {"kollecc", "At the start of your turn: look at the top card of an exploration deck. The note goes back to the Kollecc."},
    {"mortheus", "At the start of a space combat you defend: pay influence equal to the cost of one of the attacker's ships to put one of that type in. The note goes back."},
    {"vaden", "After you activate a system: each trade good you spend lets one of your non-fighter ships move 1 further. The note goes back."},
    {"cymiae", "Action: take 1 action card from the discard pile. Then the note is purged."},
    {"nivyn", "After you activate a system: ignore every anomaly during this activation. The note goes back."},
    {"olradin", "Action: play it face up - exhaust one of your planets and ready one of your non-home planets (not Mecatol Rex). It goes back at the start of the status phase."},
    {"freesystems", "When you take a planet in a tactical action with no other ships there: produce up to 2 ships in that system. The note goes back."},
    {"edyn", "After an agenda is revealed: you don't vote on it, but predict its outcome. If you're right, another player's command token goes into a system with your units. The note goes back."},
    {"nokar", "At the start of a space combat: this combat you may retreat to any system with a planet you control. The note goes back."},
    {"axis", "When your units produce: +4 Production and 1 cheaper. It goes back at the start of the status phase."},
    {"mykomentori", "Action: play it face up - once per turn you may roll one of your dice again. It goes back if you activate a system with their units."},
    {"ghemina", "After you activate a system: one of your non-fighter ships gains capacity 2 and Bombardment 5 (x2) this action. The note goes back."},
    {"lizho", "At the start of a space combat you take part in: 2 fighters into the active system. The note goes back."},
    {"mirveda", "At the start of your turn: move up to 2 of your PDS to a planet you control. The note goes back."},
    {"ghoti", "At the start of your turn: produce up to 2 ships in a system with your ships. It goes back at the start of the status phase."},
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
        return "Goes face up in your play area: while you hold it you can use its owner's commander ability (once their commander is unlocked).";
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
                   ? "When you receive it: attach it to one of your non-home planets (not Mecatol Rex) without a Branch Office - +1 influence."
                   : "When you receive it: attach it to one of your non-home planets (not Mecatol Rex) without a Branch Office - +1 resource.";
    if (type == kNoteFaction) return FIND(kNoteText, kFactions[faction].key);
    if (type == kNoteFaction2 && !std::strcmp(kFactions[faction].key, "empyrean"))
        return "ACTION: Place this card faceup in your play area. When you and the Empyrean player cast votes for the same outcome, cast 4 "
               "additional votes for that outcome. If you activate a system that contains 1 or more of the Empyrean player's units, return "
               "this card to the Empyrean player.";  // (the Empyrean review: the card's wording)
    return "";
}

}  // namespace ti
