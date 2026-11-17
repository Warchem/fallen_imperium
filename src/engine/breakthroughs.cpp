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
    {"axis", "Arms Brokerage", B, R, "At the start of a player's turn, you may exhaust this card to allow that player to treat 1 of your unit "
                                    "upgrade technologies as their own until the end of that turn. After you replenish commodities, gain a number "
                                    "of trade goods equal to the number of unit upgrade technologies you own.", true},  // (the Axis review)
    {"bentor", "Historian Conclave", B, G_, "ACTION: Exhaust this card to allow all other players to explore 1 planet they control. Then, place "
                                           "up to 1 infantry from your reinforcements into coexistence on 1 of those planets. When you explore a "
                                           "planet, you may exhaust this card to instead explore the frontier deck in that system.", true},  // (the Bentor review)
    {"celdauri", "Trade Protectorate", Y, R, "When you gain this card, flip this card and place it on your faction sheet. Then, place the Celagrom token in a system that contains your ships, if able. The Celagrom token is a second flagship with the attributes, abilities, and ability text on the Celagrom flagship card.", true},
    {"cheiran", "Matriphagy", B, R, "At the end of your turn, you may exhaust this card to choose 1 of your non-fighter ships on the game board; replace that ship with 1 or more units from your reinforcements that have a total combined cost less than that ship's cost value.", true},
    {"cymiae", "Synaptic Interference", Y, R, "After you win a combat, you may purge 1 of your promissory notes from your hand to move 1 other "
                                             "player's command token in the active system to an adjacent system, or gain 2 trade goods. "
                                             "Action: exhaust this card to discard the top 3 cards of the action card deck.", true},
    {"dihmohn", "Exodus Engineering", B, Y, "Once per space combat, after 1 of your opponent's non-fighter ships is destroyed, you may exhaust "
                                           "this card to place 1 frontier token in the active system, or produce 1 or 2 non-fighter ships in that "
                                           "system to ready this card. You can produce ships in systems that contain both your ships and another "
                                           "player's ships.", true},
    {"edyn", "Celestial Ambassadors", G_, B, "Action: exhaust this card and show another player 1 of your unscored secret objectives; that "
                                            "player must show you 1 of their unscored secret objectives, at random, or allow you to place 1 "
                                            "ground force from your reinforcements into coexistence on a planet they control.", true},
    {"freesystems", "Galactic Movement", G_, Y, "After you spend a strategy token, you may place 1 infantry from your reinforcements into "
                                               "coexistence on any cultural, hazardous, or industrial planet that contains the active player's ground "
                                               "forces. Your infantry tokens on planets you control can be accompanied by your control tokens instead of "
                                               "plastic pieces.", true},
    {"ghemina", "Raiding Parties", G_, R, "During each round of combat, you may choose 1 of your participating units; roll that unit's combat "
                                         "roll twice and resolve the result of your choice. When you announce a retreat, you may exhaust this "
                                         "card to immediately resolve that retreat.", true},
    {"ghoti", "Cult of the All-Mother", G_, B, "When you pass, you may exhaust this card to move 1 ingress token into a non-Fracture system that "
                                              "contains no planets, or move 1 of your ships to an adjacent system that contains your ships. Your "
                                              "units may retreat to any system that contains no other player's ships and no planets.", true},
    {"gledge", "Dhonraz Installations", Y, B, "ACTION: Exhaust this card to resolve BOMBARDMENT 5(xY) against each player's units on a planet "
                                             "in a system adjacent to a system that contains a Core token, where Y is the number of structures you "
                                             "control.", true},
    {"augers", "Cryptic Insights", Y, G_, "When you explore a planet, you may discard that card to draw 1 action card instead.", true},  // (the Augurs review)
    {"kjalengard", "Bannerhalls", R, G_, "For each Glory token on the game board, you control 1 Bannerhall card. Once per action, when a player "
                                        "wins a combat in a system that contains a Glory token, ready each Bannerhall card. (Bannerhall - 1/1.)", true},
    {"kollecc", "The Collector's Museum", B, G_, "When you explore a planet, you may exhaust this card to resolve a card from that exploration "
                                                "deck's discard pile instead; if you would discard that card, instead shuffle it into the "
                                                "corresponding exploration deck.", true},
    {"kolume", "Synchronicity VI", B, Y, "ACTION: Exhaust this card to either gain 1 command token, or research a technology of the same color "
                                        "as one of your exhausted technologies. When you exhaust a technology, gain 1 commodity.", true},
    {"florzen", "Reverie Implants", B, G_, "At the end of your turn, you may exhaust this card and choose 1 other player; you and that player each "
                                          "secretly spend 0, 1, or 2 trade goods. Then, reveal the spent trade goods; if you spent a different number of "
                                          "trade goods, you may take 1 promissory note from that player's hand, at random, or explore 1 planet you control.", true},
    {"kortali", "The Queens' Wrath", B, R, "If you have the Dreadnought II technology, flip this card and place it on top of Dreadnought II. Tribune "
                                          "(cost 4, combat 5, move 2, capacity 1, sustain damage, bombardment 5): this unit cannot be destroyed by "
                                          "\"Direct Hit\" action cards. When this unit produces a hit, you may repair it. While this unit is not "
                                          "damaged, you may reroll its combat roll.", true},
    {"kyro", "Strain Dispersal", Y, R, "When researching a technology, you may discard an action card to ignore 1 of that technology's prerequisites. "
                                      "Once per action, when you win a combat, for each technology your opponent owns, if you do not own that technology, "
                                      "gain 1 commodity or convert 1 of your commodities to a trade good.", true},
    {"lanefir", "Erasure Corps", G_, R, "After another player purges 1 or more non-action card components, gain 2 commodities or ready this card. "
                                        "During the action phase, you may exhaust this card to explore 1 planet you control.", true},
    {"lizho", "Professional Intrigue", G_, R, "After you perform a tactical action in a non-home system, you may move 1 of your infantry on "
                                             "the game board into coexistence on a planet in that system. When a player ends your units' "
                                             "coexistence on a planet, they must discard 1 action card, if able.", true},
    {"khrask", "Mended Grove", G_, Y, "Gain the Grove card. When you resolve METEOR SLINGS, if you cancel all hits produced, you may place "
                                     "your units into coexistence; you cannot commit those coexisting ground forces during this action. "
                                     "(The Grove - 2/X: this card's influence value is equal to the number of planets that contain your "
                                     "coexisting units.)", true},
    {"mirveda", "Stabilization Arrays", B, Y, "If you have the Fighter II unit upgrade technology, flip this card and place it on top of Fighter "
                                             "II. (Eye of the Curator - 1 (x2), combat 8, move 3: this unit may move without being transported; "
                                             "fighters in excess of your ships' capacity count against your fleet pool; your fighters count as 1/2 a "
                                             "ship for the purposes of capacity and transportation.)", true},
    {"mortheus", "Mirage Craft", R, Y, "At the start of a space combat in which you are the defender, you may swap 1 of your ships in the "
                                      "active system with 1 of your ships in another system that contains 1 of your command tokens; the swapped "
                                      "ships cannot transport units.", true},
    {"mykomentori", "Dreamwalkers", G_, Y, "Capture other player's infantry that are destroyed during combat on planets you control, even if "
                                          "you are blockaded. After each round of ground combat on a planet you control, if that combat has not "
                                          "ended, roll a die. On a result of 7 or greater, place up to 1 of your captured infantry onto that "
                                          "planet, as if it were your own.", true},
    {"nivyn", "Anomaly Stabilization", B, R, "The system that contains the Wound token is also an entropic scar. You may ignore the effects "
                                            "of entropic scars. You may treat entropic scars as ingress tokens.", true},
    {"nokar", "Mercenary Captains", Y, R, "ACTION: Exhaust this card to place 1 neutral cruiser and 2 neutral destroyers into a "
                                        "non-fracture system that contains no non-neutral ships. After another player wins a space combat in "
                                        "a system that contains your ships, gain 2 commodities or 1 command token.", true},
    {"olradin", "Insurrectionist Networking", G_, R, "At the end of your turn, if you resolved a policy ability that turn, you may spend 1 "
                                                    "trade good to move 1 of your infantry on the game board into coexistence on a planet in an "
                                                    "adjacent system. When you pass, you may purge this card to resolve invasion on all planets "
                                                    "that contain your coexisting infantry.", true},
    {"zealots", "Rhodun's Reliquary", Y, R, "After you gain control of a legendary or relic planet, you may research 1 technology or remove "
                                           "1 of your command tokens in that system and return it to your reinforcements.", true},
    {"rohdhna", "The Prodigy's Triumph", Y, B, "If you have the Terrafactory II technology, flip this card and place it on top of "
                                              "Terrafactory II. Omni-Forgeworld: Other player's units in this system lose PLANETARY SHIELD. "
                                              "This unit is also a station. When you would exhaust this station's planet card, this unit "
                                              "becomes damaged instead.", true},
    {"tnelis", "Hidden Hardpoints", R, B, "At the end of a round of ground combat, you may discard 1 secret objective to have all your "
                                         "participating units enter coexistence; that combat ends in a draw. When another player ends your "
                                         "units' coexistence on a planet, you may draw 1 secret objective.", true},
    {"vaden", "High Security Vault", R, G_, "Other players cannot take or force you to give them any of your trade goods, commodities, or relic "
                                           "fragments, unless you allow it. When another player returns 1 of your promissory notes, place 1 of "
                                           "their control tokens on your faction sheet.", true},
    {"vaylerian", "Gravitic Disruption", R, B, "After you move 1 or more ships into the active system, you may discard an action card to move 1 "
                                              "of those ships into an adjacent system that does not contain another player's ships.", true},
    {"veldyr", "Aurora Research Base", Y, B, "Your home system is also a nebula. Hits produced by the ability rolls of your units in nebulae "
                                            "cannot be canceled. When you pass, explore the frontier deck in your home system.", true},
    {"zelian", "Awakened Planetoids", G_, R, "Your planets in the active system participate in space combat if that system contains 1 or more "
                                            "of your ships, and ground combat on themselves if they contain 1 or more of your ground forces. Your "
                                            "planets cannot be assigned hits and have a combat value equal to 10 minus their resource value, minimum 1.", true},
};
}  // namespace

const BreakthroughDef* breakthroughOf(const char* factionKey) {
    if (!factionKey) return nullptr;
    for (const BreakthroughDef& b : kBt)
        if (!std::strcmp(b.faction, factionKey)) return &b;
    return nullptr;
}

}  // namespace ti
