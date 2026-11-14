// Thunder's Edge breakthroughs (0.34.0): one per faction, gained with the first expedition slice (or Thunder's Edge
// itself, Brilliance, The Quantumcore...). Each grants synergy between two technology colours (Nekro: none) and a
// faction ability. Names from the data set; the wording is our own. done = the ability is implemented
// (rules_breakthroughs.cpp); a breakthrough that isn't still gives its synergy.
#include "engine/breakthroughs.h"

#include <cstring>

namespace ti {

namespace {
constexpr TechColor B = TechColor::Blue, G_ = TechColor::Green, R = TechColor::Red, Y = TechColor::Yellow, N = TechColor::None;
const BreakthroughDef kBt[] = {
    {"arborec", "Psychospore", G_, R, "ACTION: Exhaust this card to remove a command token from a system that contains 1 or more of "
                                     "your infantry and return it to your reinforcements. Then, place 1 infantry in that system.", true},
    {"argent", "Wing Transfer", B, Y, "When you activate a system that contains only your units, you may place command tokens from your "
                                     "reinforcements into any systems adjacent to that system that contain only your units; at the end of "
                                     "this action, you may move ships among the active system and systems adjacent to it that contain your "
                                     "command tokens.", true},  // (the Argent review, 2026-11-13: the card)
    {"empyrean", "Void Tether", B, G_, "When you activate a system that contains or is adjacent to a unit or planet you control, you may "
                                      "place or move 1 of your Void Tether tokens onto a border that system shares with another system; "
                                      "other players do not treat those systems as adjacent to each other unless you allow it.",
     true},  // (the Empyrean review: the card's wording)
    {"letnev", "Gravleash Maneuvers", B, R, "Before you roll dice during space combat, apply +X to the results of 1 of your ship's rolls, "
                                           "where X is the number of ship types you have in the combat. During movement, your non-fighter "
                                           "ships' move values are equal to the highest move value amongst moving ships in the system they "
                                           "started in.", true},
    {"muaat", "Stellar Genesis", Y, R, "When you gain this card, place the Avernus planet token into a non-home system that is adjacent "
                                      "to a planet you control; gain the Avernus planet card and ready it. After you move one of your war suns "
                                      "out of or through Avernus's system and into a non-home system, you may move the Avernus token with it.", true},
    {"cabal", "Al'Raith Ix Ianovar", G_, R, "This breakthrough causes The Fracture to enter play without a roll, if it is not already in "
                                           "play. After this card enters play, move up to 2 ingress tokens into systems that contain "
                                           "gravity rifts. Apply +1 to the MOVE value of each of your ships that start their movement in "
                                           "The Fracture.", true},
    {"l1z1x", "Fealty Uplink", G_, R, "When you gain control of a planet, place infantry from your reinforcements equal to that planet's influence value on that planet.", true},
    {"sardakk", "N'orr Supremacy", B, R, "After you win a combat, either gain 1 command token or research a unit upgrade technology.", true},
    {"sol", "Bellum Gloriosum", G_, Y, "When you produce a ship that has capacity, you may also produce any combination of ground "
                                      "forces or fighters up to that ship's capacity; they do not count against your PRODUCTION "
                                      "limit.", true},  // (the Sol review: the card's wording)
    {"winnu", "Imperator", B, R, "Apply +1 to the results of each of your unit's combat rolls for each \"Support for the Throne\" "
                                "in your opponent's play area. After you activate a system that contains a legendary planet, apply +1 "
                                "to the move value of 1 of your ships during this tactical action.", true},  // (the Winnu review: the card's wording)
    {"yin", "Yin Ascendant", G_, Y, "When you gain this card or score a public objective, gain the alliance ability of a random, "
                                   "unused faction.", true},
    {"ghost", "Particle Synthesis", B, Y, "Each wormhole in a system that contains your ships gains PRODUCTION 1 as if it were a "
                                          "unit you control. Reduce the combined cost of units you produce in systems that contain "
                                          "wormholes by 1 for each wormhole in that system.", true},
    {"hacan", "Auto-Factories", Y, R, "When you produce 3 or more non-fighter ships, place 1 command token from your reinforcements "
                                     "into your fleet pool.", true},  // (the Hacan review: the card's wording)
    {"mahact", "Vaults of the Heir", G_, Y, "ACTION: Exhaust this card and purge 1 of your technologies to gain 1 relic.", true},
    {"mentak", "The Table's Grace", G_, Y, "If you have the Cruiser II unit upgrade technology, flip this card and place it on top of "
                                          "Cruiser II. Corsair: Cost 2, Combat 6, Move 3, Capacity 2. If the active system contains another "
                                          "player's non-fighter ships, this unit can move through systems that contain other players' ships.",
     true},  // (the Mentak review, 2026-11-13: the card)
    {"nekro", "Valefar Assimilator Z", N, N, "When you would gain another player's technology using one of your faction abilities, you "
                                            "may instead place one of your \"Z\" assimilator tokens on that player's faction sheet. Your "
                                            "flagship gains the text abilities of that faction's flagship in addition to its own.",
     true},  // (the Nekro review, 2026-11-14: the card)
    {"titans", "Slumberstate Computing", G_, Y, "When COALESCENCE results in a ground combat, if you commit no other units, you may "
                                               "choose for your units to coexist instead. During the status phase, for each player you "
                                               "are coexisting with, you and that player each draw 1 additional action card. Other "
                                               "players may allow you to place a sleeper token on a planet they control.", true},
    {"xxcha", "Archon's Gift", G_, Y, "You can spend influence as if it were resources. You can spend resources as if it were influence.",
     true},  // (the Xxcha review, 2026-11-13: the card)
    {"nomad", "Thunder's Paradox", G_, Y, "At the start of any player's turn, you may exhaust 1 of your agents to ready any other agent.", true},  // (the Nomad review: the card)
    {"jolnar", "Specialist Compounds", G_, Y, "When you research technology using the 'Technology' strategy card, you may exhaust a "
                                             "planet that has a technology specialty instead of spending resources; if you do, you "
                                             "must research a technology of that color.",
     true},
    {"naalu", "Mindsieve", G_, R, "When you would resolve the secondary ability of another player's strategy card, you may give them a "
                                 "promissory note to resolve it without spending a command token.", true},
    {"saar", "Deorbit Barrage", B, R, "ACTION: Exhaust this card and spend any amount of resources to choose a planet up to 2 systems "
                                     "away from an asteroid field that contains your ships; roll a number of dice equal to the amount "
                                     "spent and assign 1 hit to a ground force on that planet for each roll of 4 or greater.", true},
    {"yssaril", "Deepgloom Executable", G_, Y, "You can allow other players to use your STALL TACTICS or SCHEMING faction abilities; "
                                              "when you do, you may resolve a transaction with that player. During the action phase, "
                                              "that transaction does not count against the once-per-player transaction limit for that "
                                              "turn.", true},  // (the Yssaril review: the card's wording)
    {"keleres", "I.I.H.Q. Modernization", G_, Y, "When you gain this card, gain the Custodia Vigilia planet card and its legendary planet "
                                                "ability card. You are neighbors with all players that have units or control planets in or "
                                                "adjacent to the Mecatol Rex system. (Custodia Vigilia: 2 resources, 3 influence. Custodian's "
                                                "Favour: While you control Mecatol Rex, it gains SPACE CANNON 5 and PRODUCTION 3. Gain 2 command "
                                                "tokens when another player scores a victory point with the second clause of the \"Imperial\" "
                                                "strategy card.)", true},  // (the Keleres review: the card's wording)
    {"naaz", "Absolute Synergy", B, G_, "When you have 4 mechs in the same system, you may return 3 of those mechs to your "
                                       "reinforcements to flip this card and place it on top of your mech card. Eidolon Maximum: "
                                       "[Combat 4 (x4), Move 3, Sustain Damage] This unit is both a ship and ground force. It cannot "
                                       "be assigned hits from unit abilities. Repair it at the start of every combat round. Game "
                                       "effects cannot place or produce your mechs. When this unit is destroyed or removed, flip "
                                       "this card and return it to your play area.", true},
    // Thunder's Edge factions (part 4)
    {"bastion", "The Icon", Y, R, "When you produce ships, you may exhaust this card to place those ships in a system that contains 1 "
                                 "of your command tokens, at least 1 of your ground forces, and no other player's ships.", true},
    {"crimson", "Resonance Generator", B, R, "During your tactical actions, your ships that start their move in your home system or "
                                            "in a system with an active breach get +1 move. Action: exhaust it to flip any breach, or to "
                                            "place an active breach in a non-home system with your units.",
     true},
    {"firmament", "The Sowing", G_, Y, "When you gain this card and at the start of the status phase, you may place up to 3 of your trade "
                                      "goods on this card. Flip this card if you become The Obsidian faction.", true},
    {"obsidian", "The Reaping", G_, Y, "Place 1 trade good from the supply onto this card each time you win a combat against a puppeted "
                                      "player. At the start of the status phase, gain all trade goods on this card, then gain an equal "
                                      "number of trade goods from the supply.", true},
    {"deepwrought", "Visionaria Select", G_, Y, "ACTION: Exhaust this card to allow each other player to spend 3 trade goods and "
                                               "give you 1 promissory note. Each player that does may research a non-faction, non-unit "
                                               "upgrade technology. You also gain each technology researched in this way.", true},  // (the Deepwrought review: the card's wording)
    {"ralnel", "Data Skimmer", G_, Y, "During the action phase, if you have not passed, when other players would discard action cards, "
                                     "they are placed on this card instead. When you pass, take 1 action card from this card and "
                                     "discard the rest.", true},  // (the Ral Nel review: the card's wording)
    // Discordant Stars (0.43.0, owner D1: with Thunder's Edge too) - done is set as each is implemented (rules_dsbt.cpp)
    {"axis", "Arms Brokerage", B, R, "At the start of another player's turn, exhaust it: that player uses 1 of your unit upgrades as their own "
                                    "this turn. After you refill commodities, gain a trade good per unit upgrade you own.", true},
    {"bentor", "Historian Conclave", B, G_, "Action: exhaust it - each other player explores 1 of their planets; you may put 1 infantry, "
                                           "coexisting, on one of those planets. Exploring a planet, you may exhaust it to explore the "
                                           "frontier deck in that system instead.", true},
    {"celdauri", "Trade Protectorate", Y, R, "When you gain it: a second flagship, the Celagrom token, goes in a system with your ships.", true},
    {"cheiran", "Matriphagy", B, R, "End of your turn: exhaust it to swap one of your non-fighter ships for units from your reinforcements "
                                   "costing less in total.", true},
    {"cymiae", "Synaptic Interference", Y, R, "After you win a combat: purge a promissory note from your hand to push another player's "
                                             "command token out of the active system, or gain 2 trade goods. Action: exhaust it to discard "
                                             "the top 3 action cards.", true},
    {"dihmohn", "Exodus Engineering", B, Y, "Once per space combat, after an opponent's non-fighter ship is destroyed: exhaust it to put a "
                                           "frontier token there - or produce 1 or 2 non-fighter ships there (beside the enemy's ships), which readies it.", true},
    {"edyn", "Celestial Ambassadors", G_, B, "Action: exhaust it and show another player one of your unscored secrets - they show you one "
                                            "of theirs at random, or let you put 1 ground force, coexisting, on one of their planets.", true},
    {"freesystems", "Galactic Movement", G_, Y, "After you spend a strategy token: put 1 infantry, coexisting, on a cultural, hazardous or "
                                               "industrial planet with the active player's ground forces.", true},
    {"ghemina", "Raiding Parties", G_, R, "Each combat round one of your units rolls twice and keeps the better roll. Announcing a retreat, "
                                         "exhaust it to retreat at once.", true},
    {"ghoti", "Cult of the All-Mother", G_, B, "When you pass, exhaust it: move an ingress token into an empty system, or one of your ships "
                                              "next door to your ships. Your units may retreat into any empty system.", true},
    {"gledge", "Dhonraz Installations", Y, B, "Your planets with Core tokens also count as space stations (not for control). Your space "
                                             "stations have Space Cannon 5 (offence, in their own system).", true},
    {"augers", "Cryptic Insights", Y, G_, "When you explore a planet, you may discard the card to draw 1 action card instead.", true},
    {"kjalengard", "Bannerhalls", R, G_, "A Bannerhall for each Glory token on the board. Once per action, when anyone wins a combat where "
                                        "a Glory token is, every Bannerhall readies. (A Bannerhall: a planet card worth 1 resource or 1 influence, not for votes.)", true},
    {"kollecc", "The Collector's Museum", B, G_, "When you explore a planet: exhaust it to resolve a card from that deck's discard pile "
                                                "instead; it is shuffled back into the deck afterwards.", true},
    {"kolume", "Synchronicity VI", B, Y, "Action: exhaust it to gain 1 command token, or research a technology of the colour of one of your "
                                        "exhausted ones. Each time you exhaust a technology, gain 1 commodity.", true},
    {"florzen", "Reverie Implants", B, G_, "End of your turn: exhaust it - you and another player each secretly spend 0, 1 or 2 trade goods; "
                                          "if the amounts differ, take a random promissory note from them or explore one of your planets.", true},
    {"kortali", "The Queens' Wrath", B, R, "With Dreadnought II: flip it - your dreadnoughts become Tribunes (move 2, capacity 1, combat 5, "
                                          "bombardment 5, sustain damage; they repair when they hit, reroll while undamaged).", true},
    {"kyro", "Strain Dispersal", Y, R, "Researching: discard an action card to ignore 1 prerequisite. Once per action, winning a combat: a "
                                      "commodity (or a commodity into a trade good) for each technology the loser has and you don't.", true},
    {"lanefir", "Erasure Corps", G_, R, "When another player purges components (not action cards): gain 1 command token or ready it. In the "
                                       "action phase: exhaust it to explore one of your planets.", true},
    {"lizho", "Professional Intrigue", G_, R, "After a tactical action in a non-home system: move 1 of your infantry there, coexisting, onto "
                                             "a planet. Whoever ends your coexistence discards an action card.", true},
    {"khrask", "Mended Grove", G_, Y, "Meteor Slings: when every hit is cancelled, your infantry may coexist instead (not committed this "
                                     "action). The Grove: a planet card worth 2 resources.", true},
    {"mirveda", "Stabilization Arrays", B, Y, "With Fighter II: flip it - your fighters become the Eye of the Curator (move 3, combat 8, "
                                             "they move without carriers and take half a slot of capacity each).", true},
    {"mortheus", "Mirage Craft", R, Y, "Defending a space combat: swap one of your ships there with one of yours in a system with your "
                                      "command token (the swapped ships carry nothing).", true},
    {"mykomentori", "Dreamwalkers", G_, Y, "Other players' infantry destroyed in combat on your planets are captured. After each ground "
                                          "combat round on your planet, roll: 7+ puts a captured infantry there as yours.", true},
    {"nivyn", "Anomaly Stabilization", B, R, "The Wound's system is also an entropic scar; you ignore entropic scars, and they count as "
                                            "ingress tokens for you.", true},
    {"nokar", "Mercenary Captains", Y, R, "Action: exhaust it to put 1 neutral cruiser and 2 neutral destroyers in a system (not the "
                                        "Fracture) without other ships. When another player wins a space combat where your ships are: "
                                        "gain 2 commodities or 1 command token.", true},
    {"olradin", "Insurrectionist Networking", G_, R, "Once per action, after a policy ability: spend 1 trade good to move 1 of your "
                                                    "infantry, coexisting, onto a planet next door. When you pass: purge it to invade every "
                                                    "planet with your coexisting infantry.", true},
    {"zealots", "Rhodun's Reliquary", Y, R, "After you take a legendary or Fracture planet: research 1 technology, or take back 1 of your "
                                           "command tokens in that system.", true},
    {"rohdhna", "The Prodigy's Triumph", Y, B, "With Terrafactory II: flip it - your war suns become Omni-Forgeworlds (production 7, "
                                              "capacity 8, combat 3 x3).", true},
    {"tnelis", "Hidden Hardpoints", R, B, "End of a ground combat round: discard a secret objective - your units there coexist instead. "
                                         "Whenever someone ends your coexistence, draw a secret objective.", true},
    {"vaden", "High Security Vault", R, G_, "Nobody takes your trade goods, commodities or relic fragments without your leave. When another "
                                           "player gives back one of your promissory notes, 1 of their control tokens goes on your sheet.", true},
    {"vaylerian", "Gravitic Disruption", R, B, "After moving ships into the active system: discard an action card to move one of them on to "
                                              "a system next door without other players' ships.", true},
    {"veldyr", "Aurora Research Base", Y, B, "Your home system is also a nebula. When you pass, explore the frontier deck in your home "
                                            "system. Their space cannon hits in a nebula can't be cancelled.", true},
    {"zelian", "Awakened Planetoids", G_, R, "Your planets in the active system fight beside your ships (space) and ground forces (ground): "
                                            "combat 10 minus their resources (at least 1); they can't be hit.", true},
};
}  // namespace

const BreakthroughDef* breakthroughOf(const char* factionKey) {
    if (!factionKey) return nullptr;
    for (const BreakthroughDef& b : kBt)
        if (!std::strcmp(b.faction, factionKey)) return &b;
    return nullptr;
}

}  // namespace ti
