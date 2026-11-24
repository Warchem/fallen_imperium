// Wording for action cards, agendas and promissory notes. (2026-11-14, the action cards review: the base and Prophecy
// of Kings action cards carry the cards' own wording - AsyncTI4 data/action_cards/action_cards.json, owner 2026-11-10:
// card text may be copied; the others are still our short wording.)
#include <cstring>

#include "engine/data.h"

namespace ti {

namespace {
struct KeyText2 {
    const char* key;
    const char* text;
};
struct CardText {
    const char* key;
    const char* when;  // timing window
    const char* what;  // effect
    bool done;         // implemented in the rules engine
};

const CardText kAcText[] = {
    // ---- Uncharted Space (0.33.0, only with its module; done = implemented; 0.61.0: the cards' wording - AsyncTI4
    // action_cards/uncharted_space.json)
    {"fulfillment_protocols_ds", "After a player activates a system that contains 1 or more of your ships",
     "Place up to 1 ship from your reinforcements whose cost is 4 or less in the active system.", true},
    {"secured_trove", "Action", "Purge any 2 of your relic fragments to gain 1 relic.", true},
    {"free_trade_network", "Action", "If you control at least 1 cultural, 1 hazardous, or 1 industrial planet, for each of those planet "
                                     "traits you control, gain 2 commodities or convert 2 of your commodities to trade goods.", true},
    {"carapace_plating", "After your opponent produces 1 or more hits during ground combat", "Cancel up to 2 of those hits.", true},
    {"psionic_hammer", "Action", "Choose 1 player with the most victory points. Look at up to 1 of their unscored secret objectives, at random.", true},
    {"special_session", "After an agenda is revealed", "Each player must vote on this agenda, if able.", true},
    {"freedom_fighters_ds", "At the start of a ground combat you are participating in",
     "You may place a number of infantry from your reinforcements on that planet equal to that planet's influence value.", true},
    {"abyssal_starpaths", "After you activate a system",
     "Choose any 2 systems that do not contain planets; those 2 systems are adjacent to each other until the end of this tactical action.", true},
    {"planetary_rigs", "Action", "Choose 1 non-home planet you control other than Mecatol Rex. Reveal cards from that planet's "
                                 "corresponding exploration deck until you reveal an attachment; attach that attachment to that planet and "
                                 "discard the rest.", true},
    {"ai_augury", "When you pass", "Return 1 of your unscored secret objectives to the secret objective deck, shuffle that deck, and draw "
                                   "1 secret objective.", true},
    {"aggressive_broker", "When another player purges a component",
     "That player must choose to allow you to either gain 2 trade goods or replenish your commodities.", true},
    {"safety_overrides_ds", "After a mech you control is destroyed during ground combat",
     "Your opponent must choose and destroy 2 of their own units on that planet.", true},
    {"contagion", "After you destroy 1 or more of another player's infantry on a planet",
     "Destroy half of that player's infantry on that planet, rounded up.", true},
    {"remnant_collection", "Action", "Purge any number of your relic fragments to draw an equal number of action cards.", true},
    {"hostile_world_ds", "After a player commits ground forces to land on a planet you control",
     "Roll a number of dice equal to that planet's resource value, for each result of 6 or greater, produce 1 hit against those ground "
     "forces.", true},
    {"echo_shielding", "At the start of an invasion", "Units you control in the active system gain PLANETARY SHIELD until the end of this "
                                                      "tactical action.", true},
    {"illusory_duplication_ds", "At the start of space combat",
     "Each of your non-fighter ships in the active system that do not have SUSTAIN DAMAGE gain that unit ability this combat round.", true},
    {"confused_sage", "After a die is rolled", "Apply +3 or -3 to the result before resolving the effects of that roll.", true},
    {"singularity_charge_ds", "At the start of a round of combat", "Each unit participating in that combat becomes damaged, if able.", true},
    {"renegotiation", "After a player resolves the effect of 1 of your faction promissory notes",
     "That player must give you 1 promissory note from their hand, if able.", true},
    {"forbidden_knowledge", "When you research a technology", "You may ignore 1 of that technology's prerequisites. If that technology "
                                                             "is not a unit upgrade technology, you may ignore 2 of that technology's "
                                                             "prerequisites.", true},
    {"bounty_contracts", "At the start of the status phase", "Choose 1 revealed public objective; during this status phase, when a player "
                                                             "scores that objective, gain 1 trade good.", true},
    {"arbitrage", "When you pass", "Gain 4 commodities; for each commodity you would gain in excess of your commodity value, gain 1 trade "
                                   "good instead.", true},
    {"micrometeoroid_storm_ds", "Action", "Choose 1 system that contains another player's ships. Roll 1 die for each fighter in that "
                                          "system. For each result of 6 or greater, destroy 1 of those units.", true},
    {"professional_archeologists", "Action", "For each exploration deck, look at the top card of that deck; if that card is a relic "
                                             "fragment, reveal and gain that card, otherwise discard that card and gain 1 commodity.", true},
    {"emergency_meeting", "At the start of the agenda phase", "Shuffle all discarded agendas into the agenda deck and look at the top 3 "
                                                              "cards of that deck. Place each of those cards on the top or bottom of that "
                                                              "deck in any order.", true},
    {"escape_clause", "When another player would resolve your \"Trade Agreement\" promissory note",
     "You do not give that player your commodities; that player returns that promissory note and gains 2 trade goods.", true},
    {"personnel_writ", "Action", "Ready your agent or spend 1 command token from your strategy pool to unlock your commander.", true},
    {"preparation", "Action", "Draw 1 action card.", true},
    {"classified_weapons", "Before 1 or more of your units would make a roll for a unit ability",
     "Apply +4 to the result of each of those rolls. Hits produced by that roll cannot be canceled.", true},
    // ---- Thunder's Edge (0.34.0, only with its module; 0.61.0: the cards' wording - AsyncTI4 action_cards/te_actioncards.json)
    {"strategize", "Action", "Perform the secondary ability of any readied or unchosen strategy card.", true},
    {"overrule", "Action", "Perform the primary ability of a readied or unchosen strategy card.", true},
    {"piratecontract", "Action", "Place 1 neutral destroyer in a non-home system that contains no non-neutral ships.", true},
    {"piratefleet", "Action", "Spend 3 resources to place 1 neutral carrier, 1 neutral cruiser, 1 neutral destroyer, and 2 neutral "
                    "fighters in a non-home system that contains no non-neutral ships.", true},
    {"mercenarycontract", "Action", "Spend 2 trade goods to place 2 neutral infantry on any non-home planet that contains no units; if "
                          "that planet was owned by another player, they return its planet card to the planet card deck.", true},
    {"brilliance", "Action", "Ready 1 of your planets that has a technology specialty or choose 1 player to gain their breakthrough.", true},
    {"exchangeprogram", "Action", "Choose another player. You and that player may agree to place 1 infantry from each of your "
                        "reinforcements into coexistence on a planet the other player controls that contains their ground forces; if no "
                        "agreement is reached, you each discard 1 token from your fleet pool.", true},
    {"rescue", "After another player moves ships into a system that contains your ships",
     "You may move 1 of your ships into the active system from any system that does not contain one of your command tokens.", true},
    {"crashlanding", "When your last ship in the active system is destroyed",
     "Place 1 of your ground forces from the space area of the active system onto a planet in that system other than Mecatol Rex; if "
     "the planet contains other players' units, place your ground force into coexistence.", true},
    {"puppetsonastring", "At the end of a player's turn, if you have passed", "Perform 1 action.", true},
    {"crisis", "At the end of any player's turn, if there are at least 2 players who have not passed", "Skip the next player's turn.", true},
    {"lieinwait", "After 2 of your neighbors resolve a transaction",
     "Look at each of those players' hands of action cards, then choose and take 1 action card from each.", true},
    {"extremeduress", "At the start of another player's turn, if they have a readied strategy card",
     "If that player's next action is not a strategic action, they discard all of their action cards, give you all of their trade "
     "goods, and show you all of their secret objectives.", true},
    {"blackmarketdealing", "When you are negotiating a transaction", "You and the other player may include relics, action cards, and "
                           "unscored secret objectives as part of the transaction. This card cannot be canceled.", true},
    {"abs", "At the start of the agenda phase", "Choose 1 player. Exhaust each cultural planet owned by that player.", true},
    {"arch_expedition", "Action", "Reveal the top 3 cards of an exploration deck that matches a planet you control; gain any relic fragments that you reveal and discard the rest.", true},
    {"assassin", "After an agenda is revealed", "Choose 1 player. That player cannot vote on this agenda.", true},
    {"blitz", "At the start of an invasion", "Each of your non-fighter ships in the active system that do not have BOMBARDMENT gain BOMBARDMENT 6 until the end of the invasion.", true},
    {"bribery", "After the speaker votes on an agenda", "Spend any number of trade goods. For each trade good spent, cast 1 additional vote for the outcome on which you voted.", true},
    {"bunker", "At the start of an invasion", "During this invasion, apply -4 to the result of each BOMBARDMENT roll against planets you control.", true},
    {"confounding", "When another player is elected as the outcome of an agenda", "You are the elected player instead.", true},
    {"confusing", "When you are elected as the outcome of an agenda", "Choose 1 player. That player is the elected player instead.", true},
    {"const_rider", "After an agenda is revealed", "You cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, place 1 space dock from your reinforcements on a planet you control.", true},
    {"counterstroke", "After another player activates a system that contains 1 of your command tokens", "Return that command token to your tactic pool.", true},
    {"coup", "When another player would perform a strategic action", "End that player's turn, the strategic action is not resolved and the strategy card is not exhausted.", true},
    {"courageous", "After 1 of your ships is destroyed during a space combat", "Roll 2 dice. For each result equal to or greater than that ship's combat value, your opponent must choose and destroy 1 of their ships.", true},
    {"cripple", "Action", "Choose 1 planet. Destroy each PDS on that planet.", true},
    {"deadly_plot", "During the agenda phase when an outcome would be resolved", "If you voted for or predicted another outcome, discard the agenda instead. The agenda is resolved with no effect and it is not replaced. Then, exhaust all of your planets.", true},
    {"decoy", "After another player activates a system that contains 1 or more of your structures", "Remove up to 2 of your ground forces from the game board and place them on a planet you control in the active system.", true},
    {"dh", "After another player's ship uses SUSTAIN DAMAGE to cancel a hit produced by your units or abilities", "Destroy that ship.", true},
    {"diplo_rider", "After an agenda is revealed", "You cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, choose 1 system that contains a planet you control. Each other player places a command token from their reinforcements in that system.", true},
    {"disable", "At the start of an invasion in a system that contains 1 or more of your opponents' PDS units", "Your opponents' PDS units lose PLANETARY SHIELD and SPACE CANNON during this invasion.", true},
    {"disgrace", "When another player chooses a strategy card during the strategy phase", "That player must choose a different strategy card instead, if able.", true},
    {"distinguished", "After you cast votes on an outcome of an agenda", "Cast 5 additional votes for that outcome.", true},
    {"divert_funding", "Action", "Return a non-unit upgrade, non-faction technology that you own to your technology deck. Then, research another technology.", true},
    {"dp", "When an agenda is revealed", "Choose another player. That player must give you 1 promissory note from their hand.", true},
    {"economic_initiative", "Action", "Ready each cultural planet you control.", true},
    {"emergency", "At the start or end of a combat round", "Repair all of your units that have SUSTAIN DAMAGE in the active system.", true},
    {"experimental", "After the active player moves ships into the active system during a tactical action", "Choose 1 of your space docks that is either in or adjacent to that system. That space dock uses SPACE CANNON 5(x3) against the active player's ships in the active system.", true},
    {"f_conscription", "Action", "Place 1 fighter from your reinforcements in each system that contains 1 or more of your space docks or units that have capacity. They cannot be placed in systems that contain other players' ships.", true},
    {"f_deployment", "Action", "Place 3 infantry from your reinforcements on 1 planet you control.", true},
    {"f_prototype", "At the start of the first round of a space combat", "Apply +2 to the result of each of your fighters' combat rolls during this combat round.", true},
    {"f_researched", "Action", "Spend 4 trade goods to research 1 technology.", true},
    {"fire_team", "After your ground forces make combat rolls during a round of ground combat", "Reroll any number of your dice.", true},
    {"fs", "After you activate a system", "Apply +1 to the move value of each of your ships during this tactical action.", true},
    {"fsb", "After another player activates a system that contains your units", "Gain 3 trade goods. Then, choose another player to gain 1 trade good.", true},
    {"ghost_ship", "Action", "Place 1 destroyer from your reinforcements in a non-home system that contains a wormhole and does not contain other players' ships.", true},
    {"ghost_squad", "After another player commits units to land on a planet you control", "Move any number of your ground forces from any planet you control in the active system to any other planet you control in the active system.", true},
    {"hack", "After an agenda is revealed", "During this agenda, you vote last.", true},
    {"harness", "After you activate an anomaly", "Replenish your commodities.", true},
    {"imp_rider", "After an agenda is revealed", "You cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, gain 1 victory point.", true},
    {"impersonation", "Action", "Spend 3 influence to draw 1 secret objective.", true},
    {"industrial_initiative", "Action", "Gain 1 trade good for each industrial planet you control.", true},
    {"infiltrate", "When you gain control of a planet", "Replace each PDS and space dock that is on that planet with a matching unit from your reinforcements.", true},
    {"insider", "After an agenda is revealed", "Look at the top 3 cards of the agenda deck.", true},
    {"insub", "Action", "Remove 1 token from another player's tactic pool and return it to their reinforcements.", true},
    {"intercept", "After your opponent declares a retreat during a space combat", "Your opponent cannot retreat during this round of space combat.", true},
    {"investments", "At the start of the strategy phase", "Place a total of 5 trade goods from the supply on strategy cards of your choice. You must place these tokens on at least 3 different cards.", true},
    {"jamming", "Action", "Choose 1 non-home system that contains or is adjacent to 1 of your ships. Place a command token from another player's reinforcements in that system.", true},
    {"lead_rider", "After an agenda is revealed", "You cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, gain 3 command tokens.", true},
    {"lost_star", "After you activate a system", "During this tactical action, systems that contain alpha and beta wormholes are adjacent to each other.", true},
    {"lucky", "Action", "Destroy 1 dreadnought, cruiser, or destroyer in a system that contains a planet you control.", true},
    {"master_plan", "After you perform an action", "Perform an additional action.", true},
    {"mb", "At the start of a combat round", "Apply +1 to the result of each of your unit's combat rolls during this combat round.", true},
    {"meltdown", "Action", "Destroy 1 space dock in a non-home system.", true},
    {"messiah", "Action", "Place 1 infantry from your reinforcements on each planet you control.", true},
    {"mining_initiative", "Action", "Gain trade goods equal to the resource value of 1 planet you control.", true},
    {"mjets", "Before you assign hits produced by another player's SPACE CANNON roll", "Cancel 1 hit.", true},
    {"nav_suite", "After you activate a system", "During the 'Movement' step of this tactical action, ignore the effect of anomalies.", true},
    {"parley", "After another player commits units to land on a planet you control", "Return the committed units to the space area.", true},
    {"plagiarize", "Action", "Spend 5 influence and choose a non-faction technology owned by 1 of your neighbors. Gain that technology.", true},
    {"plague", "Action", "Choose 1 planet that is controlled by another player. Roll 1 die for each infantry on that planet. For each result of 6 or greater, destroy 1 of those units.", true},
    {"politic_rider", "After an agenda is revealed", "You cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, draw 3 action cards and gain the speaker token.", true},
    {"probe", "Action", "Explore a frontier token that is in or adjacent to a system that contains 1 or more of your ships.", true},
    {"rally", "After you activate a system that contains another player's ships", "Place 2 command tokens from your reinforcements in your fleet pool.", true},
    {"refit", "Action", "Choose 1 or 2 of your infantry on the game board. Replace each of those infantry with mechs.", true},
    {"reflective", "When one of your ships uses SUSTAIN DAMAGE during combat", "Produce 2 hits against your opponent's ships in the active system.", true},
    {"reparations", "After another player gains control of a planet you control", "Exhaust 1 planet that player controls and ready 1 planet you control.", true},
    {"repeal", "Action", "Discard 1 law from play.", true},
    {"reveal_prototype", "At the start of a combat", "Spend 4 resources to research a unit upgrade technology of the same type as 1 of your units that is participating in this combat.", true},
    {"reverse_engineer", "After another player discards an action card that has a component action", "Take that action card from the discard pile.", true},
    {"rout", "At the start of the 'Announce Retreats' step of space combat, if you are the defender", "Your opponent must announce a retreat, if able.", true},
    {"s_retreat", "At the start of a combat round", "Move all of your ships from the active system into an adjacent system that does not contain another player's ships. The space combat ends in a draw. Then, place a command token from your reinforcements in that system.", true},
    {"sabo", "When another player plays an action card other than 'Sabotage'", "Cancel that action card.", true},
    {"salvage", "After you win a space combat", "Your opponent gives you all of their commodities.", true},
    {"sanction", "After an agenda is revealed", "You cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, each player that voted for that outcome returns 1 command token from their fleet supply to their reinforcements.", true},
    {"scramble", "After another player makes a BOMBARDMENT, SPACE CANNON, or ANTI-FIGHTER BARRAGE roll", "That player rerolls all of their dice.", true},
    {"scuttle", "Action", "Choose 1 or 2 of your non-fighter ships on the game board and return them to your reinforcements. Gain trade goods equal to the combined cost of those ships.", true},
    {"seize", "Action", "Choose 1 of your neighbors that has 1 or more relic fragments. That player must give you 1 relic fragment of your choice.", true},
    {"sh", "Before you assign hits to your ships during a space combat", "Cancel up to 2 hits.", true},
    {"silence_space", "After you activate a system", "Choose 1 system. During this tactical action, your ships in the chosen system can move through systems that contain other players' ships.", true},
    {"solar_flare", "After you activate a system", "During the \"Movement\" step of this tactical action, other players cannot use SPACE CANNON against your ships.", true},
    {"spy", "Action", "Choose 1 player. That player gives you 1 random action card from their hand.", true},
    {"stability", "When you would return your strategy card(s) during the status phase", "Do not return your strategy card(s). You do not choose strategy cards during the next strategy phase.", true},
    {"summit", "At the start of the strategy phase", "Gain 2 command tokens.", true},
    {"tactical", "Action", "Choose 1 system that contains 1 or more of your units that have BOMBARDMENT. Exhaust each planet controlled by other players in that system.", true},
    {"tech_rider", "After an agenda is revealed", "You cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, research 1 technology.", true},
    {"trade_rider", "After an agenda is revealed", "You cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, gain 5 trade goods.", true},
    {"unexpected", "Action", "Remove 1 of your command tokens from the game board and return it to your reinforcements.", true},
    {"unstable", "Action", "Choose 1 hazardous planet. Exhaust that planet and destroy up to 3 infantry on it.", true},
    {"upgrade", "After you activate a system that contains 1 or more of your ships", "Replace 1 of your cruisers in that system with 1 dreadnought from your reinforcements.", true},
    {"uprising", "Action", "Exhaust 1 non-home planet controlled by another player. Then gain trade goods equal to its resource value.", true},
    {"veto", "When an agenda is revealed", "Discard that agenda and reveal 1 agenda from the top of the deck. Players vote on this agenda instead.", true},
    {"war_effort", "Action", "Place 1 cruiser from your reinforcements in a system that contains 1 or more of your ships.", true},
    {"war_machine", "When 1 or more of your units use PRODUCTION", "Apply +4 to the total PRODUCTION value of your units and reduce the combined cost of the produced units by 1.", true},
    {"war_rider", "After an agenda is revealed", "You cannot vote on this agenda. Predict aloud an outcome of this agenda. If your prediction is correct, place 1 dreadnought from your reinforcements in a system that contains 1 or more of your ships.", true},
    {"waylay", "Before you roll dice for ANTI-FIGHTER BARRAGE", "Hits from this roll are produced against all ships (not just fighters).", true},
    // ---- Monuments+ (0.82.0, only with its module): the cards' own wording (the owner's v3.1 card files)
    {"festival", "Action", "Ready a non-home planet that contains 1 of your structures. If that planet contains your monument, place 4 "
                           "infantry or 1 mech on it.", true},
    {"rebelbombing", "Action", "Spend 2 trade goods to destroy 1 of your neighbor's structures in a non-home system.", true},
    {"renovation", "Action", "Replace 1 of your structures with 1 structure from your reinforcements.", true},
};

struct AgendaText {
    const char* key;
    const char* a;  // For / the elect effect
    const char* b;  // Against ("" for elections)
    bool done;
};

const AgendaText kAgText[] = {  // (0.60.0, the agenda pass: the cards' wording - AsyncTI4's texts, the Prophecy of Kings printings)
    {"abolishment", "(When this agenda is revealed, if there are no laws in play, discard this card and reveal another agenda from the top of the deck.) Discard the elected law from play.", "", true},
    {"arbiter", "The elected player gains this card. At the end of the strategy phase, the owner of this card may discard this card to swap 1 of their strategy cards with 1 of another player's strategy cards.", "", true},
    {"arms_reduction", "Each player destroys all but 2 of their dreadnoughts and all but 4 of their cruisers.", "At the start of the next strategy phase, each player exhausts each of their planets that have a technology specialty.", true},
    {"articles_war", "All mechs lose their printed abilities except for SUSTAIN DAMAGE.", "Each player that voted \"For\" gains 3 trade goods.", true},
    {"artifact", "The speaker rolls 1 die. If the result is 6-10, each player may research 2 technologies. If the result is 1-5, destroy all units in Mecatol Rex's system, and each player with units in systems adjacent to Mecatol Rex's system destroys 3 of their own units in each of those systems.", "No effect.", true},
    {"censure", "The elected player gains this card and 1 victory point. The elected player cannot play action cards. If the owner of this card loses this card, they lose 1 victory point.", "", true},
    {"checks", "When a player chooses a strategy card during the strategy phase, they give that strategy card to another player who does not have 1 (or a player who does not have 2 in a three- or four-player game), if able.", "Each player readies only 3 of their planets at the end of this agenda phase.", true},
    {"cladenstine", "Each player removes 2 command tokens from their command sheet and returns those tokens to their reinforcements.", "Each player removes 1 command token from their fleet pool and returns that token to their reinforcements.", true},
    {"classified", "(When this agenda is revealed, if there are no scored secret objectives, discard this card and reveal another agenda from the top of the deck.) The elected secret objective becomes a public objective - place it near the other public objectives in the common play area.", "", true},
    {"committee", "The elected player gains this card. Before players vote on an agenda that requires a player to be elected, the owner of this card may discard this card to choose a player to be elected. Players do not vote on that agenda.", "", true},
    {"conscription", "When a player produces units, they produce only 1 fighter and infantry for its cost instead of 2.", "No effect.", true},
    {"constitution", "(When this agenda is revealed, if there are no laws in play, discard this card and reveal another agenda from the top of the deck.) Discard all laws from play. At the start of the next strategy phase, each player exhausts each planet in their home system.", "No effect.", true},
    {"conventions", "Players cannot use BOMBARDMENT against units that are on cultural planets.", "Each player that voted \"Against\" discards all of their action cards.", true},
    {"covert", "When this agenda is revealed, the speaker draws the next card in the agenda deck but does not reveal it to the other players. Instead, the speaker reads the eligible outcomes aloud (for, against, elect player, etc.); the other players vote for these outcomes as if they were outcomes of this agenda, without knowing their effects.", "", true},
    {"crisis", "Each player may perform the secondary ability of the elected strategy card without spending a command token - command tokens placed by the ability are placed from a player's reinforcements instead.", "", true},
    {"defense_act", "Each player can have any number of PDS units on planets they control.", "Each player destroys 1 of their PDS units.", true},
    {"disarmament", "Destroy each ground force on the elected planet. For each unit that was destroyed, the player who controls that planet gains 1 trade good.", "", true},
    {"economic_equality", "Each player returns all of their trade goods to the supply. Then, each player gains 5 trade goods.", "Each player returns all of their trade goods to the supply.", true},
    {"execution", "The elected player discards all of their action cards. If they have the speaker token, they give it to the player on their left. The elected player cannot vote on any agendas during this agenda phase.", "", true},
    {"grant_reallocation", "The elected player gains any 1 technology of their choice. Then, for each prerequisite on that technology, they remove 1 token from their fleet pool and return it to their reinforcements.", "", true},
    {"incentive", "Draw and reveal 1 stage I public objective from the deck and place it near the public objectives.", "Draw and reveal 1 stage II public objective from the deck and place it near the public objectives.", true},
    {"minister_antiquities", "The elected player gains 1 relic.", "", true},
    {"minister_commerce", "The elected player gains this card. After the owner of this card replenishes commodities, they gain 1 trade good for each player that is their neighbor.", "", true},
    {"minister_exploration", "The elected player gains this card. When the owner of this card gains control of a planet, they gain 1 trade good.", "", true},
    {"minister_industry", "The elected player gains this card. When the owner of this card places a space dock in a system, their units in that system may use their PRODUCTION abilities.", "", true},
    {"minister_peace", "The elected player gains this card. After a player activates a system that contains 1 or more of a different player's units, the owner of this card may discard this card - immediately end the active player's turn.", "", true},
    {"minister_policy", "The elected player gains this card. At the end of the status phase, the owner of this card draws 1 action card.", "", true},
    {"minister_sciences", "The elected player gains this card. When the owner of this card resolves the primary or secondary ability of the \"Technology\" strategy card, they do not need to spend resources to research technology.", "", true},
    {"minister_war", "The elected player gains this card. The owner of this card may discard this card after performing an action to remove 1 of their command tokens from the game board and return it to their reinforcements - then they may perform 1 additional action.", "", true},
    {"miscount", "(When this agenda is revealed, if there are no laws in play, discard this card and reveal another agenda from the top of the deck.) Vote on the elected law as if it were just revealed from the top of the deck.", "", true},
    {"mutiny", "Each player that voted \"For\" gains 1 victory point.", "Each player that voted \"For\" loses 1 victory point.", true},
    {"nexus", "Alpha and beta wormholes in the wormhole nexus have no effect during movement.", "Place a gamma wormhole token in the Mecatol Rex system.", true},
    {"plowshares", "Each player destroys half of their infantry on each planet they control, rounded up. Then, each player gains trade goods equal to the number of their infantry that were destroyed.", "Each player places 1 infantry from their reinforcements on each planet they control.", true},
    {"prophecy", "The elected player gains this card. The owner of this card applies +1 to the result of their fighters' combat rolls. When the owner of this card uses PRODUCTION, they discard this card unless they produce 2 or more fighters.", "", true},
    {"rearmament", "Each player places 1 mech from their reinforcements on a planet they control in their home system.", "Each player replaces each of their mechs with 1 infantry from their reinforcements.", true},
    {"redistribution", "Destroy each unit on the elected planet. Then, the player who controls that planet chooses 1 player with the fewest victory points - that player may place 1 infantry from their reinforcements on that planet.", "", true},
    {"regulations", "Each player cannot have more than 4 tokens in their fleet pool.", "Each player places 1 command token from their reinforcements in their fleet pool.", true},
    {"rep_govt", "Players cannot exhaust planets to cast votes during the agenda phase. Each player may cast 1 vote on each agenda instead. Players cannot cast additional votes.", "At the start of the next strategy phase, each player that voted \"Against\" exhausts all of their cultural planets.", true},
    {"revolution", "After a player researches a technology, that player must destroy 1 of their non-fighter ships.", "At the start of the next strategy phase, each player chooses and exhausts 1 planet for each technology that they own.", true},
    {"sanctions", "Each player can have a maximum of 3 action cards in their hand.", "Each player discards 1 random action card from their hand.", true},
    {"schematics", "If any player owns a war sun technology, all players may ignore all prerequisites on war sun technologies. All war suns lose SUSTAIN DAMAGE.", "Each player that owns a war sun technology discards all of their action cards.", true},
    {"secret", "The elected player draws 1 secret objective.", "", true},
    {"seed_empire", "The player with the most victory points gains 1 victory point.", "The player with the fewest victory points gains 1 victory point.", true},
    {"shared_research", "Each player's units can move through nebulae.", "Each player places a command token from their reinforcements in their home system, if able.", true},
    {"standardization", "The elected player places command tokens from their reinforcements so that they have 3 tokens in their tactic pool, 3 tokens in their fleet pool, and 2 tokens in their strategy pool. They return any excess tokens to their reinforcements.", "", true},
    {"travel_ban", "Alpha and beta wormholes have no effect during movement.", "Destroy each PDS in or adjacent to a system that contains a wormhole.", true},
    {"unconventional", "Each player that voted \"For\" draws 2 action cards.", "Each player that voted \"For\" discards all of their action cards.", true},
    {"warrant", "The elected player gains this card and draws 2 secret objectives. The owner of this card plays with their secret objectives revealed.", "", true},
    {"wormhole_recon", "All systems that contain either an alpha or beta wormhole are adjacent to each other.", "Each player places a command token from their reinforcements in each system that contains a wormhole and 1 or more of their ships.", true},
    {"wormhole_research", "Each player who has 1 or more ships in a system that contains a wormhole may research 1 technology. Then, destroy all ships in systems that contain an alpha or beta wormhole.", "Each player that voted \"Against\" removes 1 command token from their command sheet and returns it to their reinforcements.", true},
    // (0.82.0) Monuments+ - the cards' own wording (the owner's v3.1 card files)
    {"cathedralofixth", "Attach this card to the elected planet. The owner of this planet gains 1 VP if their monument is on this planet.\n"
                        "When a player places their monument on this planet, they gain 1 VP. When a player's monument on this planet is "
                        "removed, they lose 1 VP.", "", true},
    {"ministerofculture", "As long as the owner of this card controls their monument their commodity value is increased by 2.\n"
                          "The elected player gains this card and replenishes their commodities.", "", true},
// (0.81.0) Absol's Agendas - the cards' own wording (tools/gen_absol_text.py)
#include "engine/gen/absol_text.inc"
};
}  // namespace

const char* actionCardWhen(int c) {
    if (c < 0 || c >= kActionCardCount) return "";
    for (const auto& t : kAcText)
        if (!std::strcmp(t.key, kActionCards[c].key)) return t.when;
    return "";
}
const char* actionCardText(int c) {
    if (c < 0 || c >= kActionCardCount) return "";
    for (const auto& t : kAcText)
        if (!std::strcmp(t.key, kActionCards[c].key)) return t.what;
    return "";
}
bool actionCardImplemented(int c) {
    if (c < 0 || c >= kActionCardCount) return false;
    for (const auto& t : kAcText)
        if (!std::strcmp(t.key, kActionCards[c].key)) return t.done;
    return false;
}
bool actionCardIsComponent(int c) { return !std::strcmp(actionCardWhen(c), "Action"); }

const char* agendaText(int a, bool against) {
    if (a < 0 || a >= kAgendaCount) return "";
    for (const auto& t : kAgText)
        if (!std::strcmp(t.key, kAgendas[a].key)) return against ? t.b : t.a;
    return "";
}
bool agendaImplemented(int a) {
    if (a < 0 || a >= kAgendaCount) return false;
    for (const auto& t : kAgText)
        if (!std::strcmp(t.key, kAgendas[a].key)) return t.done;
    return false;
}

// ---------------------------------------------------------------------------
// Exploration cards, relics and legendary planets (Prophecy of Kings)
namespace {
const KeyText2 kExText[] = {  // (0.61.0, the exploration pass: the cards' wording - AsyncTI4 explores/explores.json, uncharted_space.json, codex3.json)
    {"frln", "You may produce 1 unit in this system. You may spend influence as if it were resources to produce this unit."},
    {"mo", "You may place 1 infantry from your reinforcements on this planet."},
    {"gw", "Place a gamma wormhole token in this system. Then, purge this card."},
    {"dmz", "Return all structures on this planet to your reinforcements. Then, return all ground forces on this planet to the space area. "
            "ATTACH: Units cannot be committed to, produced on, or placed on this planet. During the agenda phase, this planet's planet card "
            "can be traded as part of a transaction."},
    {"ds", "ATTACH: This planet's resource value is increased by 2 and its influence value is increased by 1."},
    {"pw", "ATTACH: This planet's influence value is increased by 2."},
    {"toe", "ATTACH: This planet's influence value is increased by 1. If the player who has the \"Crown of Emphidia\" relic has control "
            "of this planet, they can use that relic to gain 1 victory point."},
    {"crf", "ACTION: Purge 3 of your cultural relic fragments to gain 1 relic."},
    {"vfs", "If you have at least 1 mech on this planet, or if you remove 1 infantry from this planet, gain 1 command token."},
    {"exp", "If you have at least 1 mech on this planet, or if you remove 1 infantry from this planet, ready this planet."},
    {"cm", "If you have at least 1 mech on this planet, or if you remove 1 infantry from this planet, gain 1 trade good."},
    {"ls", "ATTACH: This planet's resource value is increased by 1 and its influence value is increased by 2."},
    {"mw", "ATTACH: This planet's resource value is increased by 2."},
    {"rw", "ATTACH: This planet's resource value is increased by 1."},
    {"warfare", "ATTACH: This planet has a red technology specialty. If this planet already has a technology specialty, this planet's "
                "resource and influence values are each increased by 1 instead."},
    {"hrf", "ACTION: Purge 3 of your hazardous relic fragments to gain 1 relic."},
    {"aw", "You may gain 2 commodities, or you may convert up to 2 of your commodities to trade goods."},
    {"fb", "You may gain 1 commodity, or you may spend 1 trade good or 1 commodity to draw 1 action card."},
    {"lf", "You may gain 1 commodity, or you may spend 1 trade good or 1 commodity to place 1 mech from your reinforcements on this planet."},
    {"biotic", "ATTACH: This planet has a green technology specialty. If this planet already has a technology specialty, this planet's "
               "resource and influence values are each increased by 1 instead."},
    {"cybernetic", "ATTACH: This planet has a yellow technology specialty. If this planet already has a technology specialty, this planet's "
                   "resource and influence values are each increased by 1 instead."},
    {"propulsion", "ATTACH: This planet has a blue technology specialty. If this planet already has a technology specialty, this planet's "
                   "resource and influence values are each increased by 1 instead."},
    {"irf", "ACTION: Purge 3 of your industrial relic fragments to gain 1 relic."},
    {"dv", "Draw 1 secret objective."},
    {"ed", "Place this card faceup in your play area. ACTION: You may spend 6 resources and purge this card to research 1 technology."},
    {"gamma", "Place a gamma wormhole token in this system. Then, purge this card."},
    {"ion", "Place the ion storm token in this system with either side faceup. Then, place this card in the common play area. At the end "
            "of the \"Move Ships\" or \"Retreat\" substep of a tactical action during which 1 or more of your ships use the ion storm "
            "wormhole, flip the ion storm token to its opposing side."},
    {"lc", "Draw 2 action cards."},
    {"ms", "You may replenish your commodities, or you may convert your commodities to trade goods."},
    {"mirage", "Place the Mirage planet token in this system. Gain the Mirage planet card and ready it. Then, purge this card."},
    {"urf", "This card counts as a relic fragment of any type."},
    // Uncharted Space (0.33.0)
    {"starchartcultural", "This card may be exchanged as part of a transaction. ACTION: Purge 2 of your Star Charts to draw 1 system tile "
                          "with a blue-colored back at random; place that tile at the edge of the game board, adjacent to at least 2 other systems."},
    {"starcharthazardous", "This card may be exchanged as part of a transaction. ACTION: Purge 2 of your Star Charts to draw 1 system tile "
                           "with a blue-colored back at random; place that tile at the edge of the game board, adjacent to at least 2 other systems."},
    {"starchartindustrial", "This card may be exchanged as part of a transaction. ACTION: Purge 2 of your Star Charts to draw 1 system tile "
                            "with a blue-colored back at random; place that tile at the edge of the game board, adjacent to at least 2 other systems."},
    {"starchartfrontier", "This card may be exchanged as part of a transaction. ACTION: Purge 2 of your Star Charts to draw 1 system tile "
                          "with a blue-colored back at random; place that tile at the edge of the game board, adjacent to at least 2 other systems."},
    {"forgottentradestation", "For each planet in this system, gain 1 trade good."},
    {"distinguishedadmiral", "Redistribute up to 1 command token, or spend 2 influence to gain 1 command token."},
    {"councilpreserve", "ATTACH: When you exhaust this planet to cast votes, cast 5 additional votes."},
    {"scorcheddepot", "If you have at least 1 mech on this planet, or if you remove 1 infantry from this planet, remove 1 command token "
                      "from this system or redistribute 1 of your command tokens."},
    {"arcanecitadel", "ATTACH: When you exhaust this planet, place 1 infantry from your reinforcements on this planet. Apply +1 to each of "
                      "your unit's combat rolls on this planet."},
    {"hiddenlaboratory", "Explore the frontier deck in this system."},
    {"seedyspaceport", "If you have at least 1 mech on this planet, or if you remove 1 infantry from this planet, ready your agent or draw 1 action card."},
    {"ancientshipyard", "Place up to 1 cruiser from your reinforcements in this system if it contains no other players' ships."},
    {"orbitalfoundries", "ATTACH: This planet has a PRODUCTION value of 2 as if it were a unit."},
    {"folderspace", "Place 1 command token from your reinforcements in your fleet pool and move any number of your units in this system "
                    "to the space area of another system that contains no other players' ships."},
    {"suspiciouswreckage", "Choose 2 other players, each of those players must give you 1 promissory note from their hand."},
    {"darkvisions", "Look at the top card of each exploration deck, discard any number of those cards and explore up to 1 planet you "
                    "control. Then, shuffle each exploration deck."},
    {"warforgeruins", "If you have at least 1 mech on this planet, or if you remove 1 infantry from this planet, place 1 mech or 2 infantry "
                      "from your reinforcements on this planet."},
    {"arcaneshieldemitter", "ATTACH: Return all infantry on this planet to the space area or destroy them. This planet cannot be bombarded. "
                            "Infantry cannot be committed to, produced on, or placed on this planet."},
    // Codex III (0.61.0: in games with the Codex)
    {"minent", "Gain 1 command token and 1 trade good."},
    {"ent", "Gain 1 command token and 2 trade goods."},
    {"majent", "Gain 1 command token and 3 trade goods."},
    {"kel", "Gain 2 command tokens."},
    {"dw", "Draw 1 relic."},
};
const KeyText2 kRelicText[] = {  // (0.60.0, the relic pass: the cards' wording)
    {"dominusorb", "Before you move units during a tactical action, you may purge this card to move and transport units that are in systems that contain 1 of your command tokens."},
    {"mawofworlds", "At the start of the agenda phase, you may purge this card and exhaust all of your planets to gain any 1 technology."},
    {"emelpar", "When you would spend a token from your strategy pool, you may exhaust this card to spend a token from your reinforcements instead."},
    {"shard", "When you gain this card, gain 1 victory point. When you lose this card, lose 1 victory point. When a player gains control of a legendary planet you control or a planet you control in your home system, that player gains this card."},
    {"stellarconverter", "ACTION: Choose 1 non-home, non-legendary planet other than Mecatol Rex in a system that is adjacent to 1 or more of your units that have BOMBARDMENT, destroy all units on that planet and purge its attachments and its planet card. Then, place the destroyed planet token on that planet and purge this card."},
    {"codex", "ACTION: Purge this card to take up to 3 action cards of your choice from the action card discard pile."},
    {"emphidia", "After you perform a tactical action, you may exhaust this card to explore 1 planet you control. At the end of the status phase, if you control the \"Tomb of Emphidia,\" you may purge this card to gain 1 victory point."},
    {"thalnos", "During each combat round, this card's owner may reroll any number of their dice, applying +1 to the results; any units that reroll dice but do not produce at least 1 hit are destroyed."},
    {"obsidian", "When you gain this card, draw 1 secret objective. You can have 1 additional scored or unscored secret objective."},
    {"prophetstears", "When you research a technology, you may exhaust this card to ignore 1 prerequisite or draw 1 action card."},
    {"boon_of_the_cerulean_god", "Apply +1 to the PRODUCTION value of each of your units with PRODUCTION. When you produce 1 or more units reduce the combined cost of the produced units by 1."},
    {"decrypted_cartoglyph", "ACTION: Purge this card to draw 3 system tiles with a blue-colored back at random. Place 1 of those tiles at the edge of the game board, adjacent to at least 2 other systems. Purge the rest."},
    {"e6-g0_network", "You may have 2 additional action cards in your hand, game effects cannot prevent you from using this ability. At any time, you may exhaust this card to draw 1 action card."},
    {"eye_of_vogul", "After you activate a system: Purge this card to treat 2 of your ships as adjacent to the active system until the end of this tactical action."},
    {"starfall_array", "When 1 or more of your units roll dice for a unit ability, you may add +1 to the result of each of those dice, and 1 of those units may roll 1 additional die."},
    {"throne_of_the_false_emperor", "ACTION: Purge this card to either gain 1 relic or immediately score 1 scored secret objective, or 1 of your unscored secret objectives, if you fulfill its requirements."},
    {"twilight_mirror", "At the start of the agenda phase, you may purge this card to resolve 1 non-strategic action."},
    {"bookoflatvinia", "When you gain this card, research up to 2 technologies that have no prerequisites. ACTION: Purge this card; if you control planets that have all 4 types of technology specialties, gain 1 victory point. Otherwise, gain the speaker token."},
    {"circletofthevoid", "Your units do not roll for gravity rifts, and you ignore the effects of all other anomalies on movement. ACTION: Exhaust this card to explore a frontier token in a system that does not contain any other players' ships."},
    {"heartofixth", "After any die is rolled, you may exhaust this card to add or subtract 1 from its result."},
    {"lightrailordnance", "Your space docks gain SPACE CANNON 5 (x2). You may use your space dock's SPACE CANNON against ships that are adjacent to their systems."},
    {"metalivoidarmaments", "During the \"Anti-Fighter Barrage\" step of space combat, you may resolve ANTI-FIGHTER BARRAGE 6 (x3) against your opponent's units."},
    {"metalivoidshielding", "Each time hits are produced against 1 or more of your non-fighter ships, 1 of those ships may use SUSTAIN DAMAGE as if it had that ability."},
    {"neuraloop", "When a public objective is revealed, you may purge one of your relics to discard that objective and replace it with a random objective from any objective deck; that objective is a public objective, even if it is a secret objective."},
    {"quantumcore", "When you gain this card, gain your breakthrough. You have SYNERGY for all technology types."},
    {"thesilverflame", "The Silver Flame may be exchanged as part of a transaction. ACTION: Roll 1 die and purge this card; if the result is a 10, gain 1 victory point. Otherwise, purge your home system and all units in it; you cannot score public objectives. Put The Fracture into play if it is not already."},
    {"thetriad", "This card can be readied and spent as if it were a planet card. Its resource and influence values are equal to 3 plus the number of different types of relic fragments you own."},
    // Codex II (0.60.0, owner: in games with the Codex or Thunder's Edge)
    {"dynamiscore", "While this card is in your play area, your commodity value is increased by 2. ACTION: Gain trade goods equal to your commodity value, then purge this card."},
    {"titanprototype", "ACTION: Exhaust this agent and choose a player; that player may spend 3 resources to place a structure on a planet they control. If they do not, they gain 1 trade good."},
    {"nanoforge", "ACTION: Attach this card to a non-legendary, non-home planet you control; its resource and influence values are increased by 2 and it is a legendary planet. This action cannot be performed once attached."},
};
const KeyText2 kLegendText[] = {  // (0.61.0, the legendary planet pass: the cards' wording - AsyncTI4 planets/*.json)
    {"hopesend", "You may exhaust this card at the end of your turn to place 1 mech from your reinforcements on any planet you control "
                 "or draw 1 action card."},
    {"primor", "You may exhaust this card at the end of your turn to place up to 2 infantry from your reinforcements on any planet you control."},
    {"mirage", "You may exhaust this card at the end of your turn to place up to 2 fighters from your reinforcements in any system that "
               "contains 1 or more of your ships."},
    {"mallice", "You may exhaust this card at the end of your turn to gain 2 trade goods or convert all of your commodities to trade goods."},
    // Uncharted Space (0.33.0)
    {"silence", "You may exhaust this card at the end of your turn to place 1 cruiser from your reinforcements in any system that "
                "contains 1 or more of your ships."},
    {"echo", "You may exhaust this card at the end of your turn to place 1 frontier token in a system that does not contain a planet."},
    {"tarrock", "After an agenda is revealed, you may exhaust this card to predict aloud an outcome of that agenda. If your prediction "
                "is correct, draw 1 secret objective."},
    {"prism", "You may exhaust this card at the end of your turn and purge a non-faction, non-unit upgrade technology you own to gain 1 "
              "technology with the same number of prerequisites."},
    {"domna", "You may exhaust this card at the end of your turn to remove 1 of your ships from the game board and place that unit in "
              "an adjacent system that does not contain another player's ships."},
    // Thunder's Edge (0.34.0)
    {"emelpar", "You may exhaust this card at the end of your turn to ready another component that isn't a strategy card."},
    {"faunus", "You may exhaust this card when you pass to gain control of a non-home, non-legendary planet that contains no units and "
               "has no attachments."},
    {"garbozia", "You may exhaust this card when you pass to place 1 action card from the discard pile faceup on this card; you can "
                 "purge cards on this card to play them as if they were in your hand."},
    {"industrex", "You may exhaust this card when you pass to place 1 ship that matches a unit upgrade technology you own from your "
                  "reinforcements into a system that contains your ships."},
    {"tempesta", "You may exhaust this card after you activate a system to apply +1 to the move value of 1 of your ships during this "
                 "tactical action."},
    {"styx", "When you gain this card, gain 1 victory point. When you lose this card, lose 1 victory point."},
    {"thundersedge", "Gain your breakthrough when you gain this card if you do not already have it. You may exhaust this card at the end "
                     "of your turn to perform another action."},
    {"avernus", "ACTION: Exhaust this card to use the Embers of Muaat's STAR FORGE faction ability without spending a command token."},
    {"ordinian", "You may exhaust this card when you pass to draw 1 action card and gain 1 command token."},
    {"conviction", "At any time, you may exhaust this card to gain 2 commodities. When you exhaust a superweapon card, ready this card."},  // (0.84.0 Belkosea)
};
const char* lookup(const KeyText2* t, int n, const char* key) {
    for (int i = 0; i < n; ++i)
        if (!std::strcmp(t[i].key, key)) return t[i].text;
    return "";
}
}  // namespace

const char* exploreText(int c) {
    if (c < 0 || c >= kExploreCardCount) return "";
    return lookup(kExText, int(sizeof(kExText) / sizeof(kExText[0])), kExploreCards[c].key);
}
const char* relicText(int r) {
    if (r < 0 || r >= kRelicCount) return "";
    return lookup(kRelicText, int(sizeof(kRelicText) / sizeof(kRelicText[0])), kRelics[r].key);
}
const char* legendaryText(int planetDef) {
    if (planetDef < 0 || planetDef >= kPlanetCount) return "";
    return lookup(kLegendText, int(sizeof(kLegendText) / sizeof(kLegendText[0])), kPlanets[planetDef].key);
}
const char* legendaryName(int planetDef) {
    if (planetDef < 0 || planetDef >= kPlanetCount) return "";
    const char* k = kPlanets[planetDef].key;
    if (!std::strcmp(k, "hopesend")) return "Imperial Arms Vault";
    if (!std::strcmp(k, "primor")) return "The Atrament";
    if (!std::strcmp(k, "mirage")) return "Mirage Flight Academy";
    if (!std::strcmp(k, "mallice")) return "Exterrix Headquarters";
    if (!std::strcmp(k, "silence")) return "Imperial Salvage Yard";
    if (!std::strcmp(k, "echo")) return "Bi-Phasic Generators";
    if (!std::strcmp(k, "tarrock")) return "Council Black Site";
    if (!std::strcmp(k, "prism")) return "Mystic Library";
    if (!std::strcmp(k, "domna")) return "Fractal Gate";
    // Thunder's Edge (0.34.0)
    if (!std::strcmp(k, "emelpar")) return "The Acropolis";
    if (!std::strcmp(k, "faunus")) return "Maxis Central Control";
    if (!std::strcmp(k, "garbozia")) return "Dok 'N Pic's Salvage Yard";
    if (!std::strcmp(k, "industrex")) return "Aeurex Mechanica";
    if (!std::strcmp(k, "tempesta")) return "Ionian Fuel Refinery";
    if (!std::strcmp(k, "styx")) return "A Song Like Marrow";
    if (!std::strcmp(k, "thundersedge")) return "Jupiter Brain";
    if (!std::strcmp(k, "avernus")) return "The Nucleus";
    if (!std::strcmp(k, "ordinian")) return "4X41D \"Hyperion\" VI";
    if (!std::strcmp(k, "conviction")) return "S.O.L.G. Industries Group";  // (0.84.0 Blue Reverie: the Belkosea home station)
    return "";
}
const char* exploreTypeName(ExploreType t) {
    switch (t) {
        case ExploreType::Cultural: return "cultural";
        case ExploreType::Hazardous: return "hazardous";
        case ExploreType::Industrial: return "industrial";
        case ExploreType::Frontier: return "frontier";
    }
    return "";
}

const char* noteName(int type) {
    static const char* n[] = {"Ceasefire", "Trade Agreement", "Political Secret", "Support for the Throne"};
    return type >= 0 && type < kNoteTypes ? n[type] : "?";
}
const char* noteText(int type) {
    // (0.61.0, the generic notes' pass: the cards' wording - AsyncTI4 promissory_notes/color.json; "<color>" = the note's owner)
    static const char* t[] = {
        "After the owner of this card activates a system that contains 1 or more of your units: The owner cannot move units to the "
        "active system. Then return this card to its owner.",
        "When the owner of this card replenishes commodities: The owner gives you all of their commodities. Then, return this card to "
        "its owner.",
        "When an agenda is revealed: The owner of this card cannot vote, play action cards, or use faction abilities until after that "
        "agenda has been resolved. Then, return this card to its owner.",
        "When you receive this card, if you are not its owner, you must place it faceup in your play area and gain 1 victory point. "
        "When you activate a system that contains 1 or more of its owner's units, or if its owner is eliminated, lose 1 victory point "
        "and return this card to its owner.",
    };
    return type >= 0 && type < kNoteTypes ? t[type] : "";
}

}  // namespace ti
