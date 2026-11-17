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
    // ---- Uncharted Space (0.33.0, only with its module; done = implemented)
    {"fulfillment_protocols_ds", "After a player activates a system with your ships", "Place 1 ship costing 4 or less from your reinforcements there.", true},
    {"secured_trove", "Action", "Purge 2 of your relic fragments (any kinds) to gain a relic.", true},
    {"free_trade_network", "Action", "For each of cultural, hazardous and industrial you hold a planet of: gain 2 commodities, or turn 2 commodities into trade goods.", true},
    {"carapace_plating", "After your opponent rolls hits in ground combat", "Cancel up to 2 of those hits.", true},
    {"psionic_hammer", "Action", "Look at one random unscored secret objective of a player with the most victory points.", true},
    {"special_session", "After an agenda is revealed", "Every player who can vote must vote on this agenda.", true},
    {"freedom_fighters_ds", "Start of a ground combat you are in", "Place infantry on that planet equal to its influence.", true},
    {"abyssal_starpaths", "After you activate a system", "Two systems without planets count as adjacent until the end of this tactical action.", true},
    {"planetary_rigs", "Action", "Pick a non-home planet you control (not Mecatol Rex): reveal its exploration deck until an attachment and attach it there.", true},
    {"ai_augury", "When you pass", "Return 1 of your unscored secret objectives, shuffle, and draw a new one.", true},
    {"aggressive_broker", "When another player purges something", "They choose: you gain 2 trade goods, or you refill your commodities.", true},
    {"safety_overrides_ds", "After your mech is destroyed in ground combat", "Your opponent destroys 2 of their own units on that planet.", true},
    {"contagion", "After you destroy another player's infantry in a ground combat", "Half of their infantry there (rounded up) is destroyed too.", true},
    {"remnant_collection", "Action", "Purge any number of your relic fragments: draw that many action cards.", true},
    {"hostile_world_ds", "After ground forces land on a planet you control", "Roll dice equal to its resources: each 6 or more is a hit on them.", true},
    {"echo_shielding", "Start of an invasion", "Your units in the active system have Planetary Shield for this tactical action.", true},
    {"illusory_duplication_ds", "Start of a space combat", "Your non-fighter ships there without Sustain Damage have it this round.", true},
    {"confused_sage", "After a die is rolled", "Add 3 to it or take 3 from it.", true},
    {"singularity_charge_ds", "Start of a combat round", "Every unit in the combat that can be damaged becomes damaged.", true},
    {"renegotiation", "After a player uses one of your faction promissory notes", "They give you a promissory note from their hand.", true},
    {"forbidden_knowledge", "When you research a technology", "Ignore 1 prerequisite (2 if it isn't a unit upgrade).", true},
    {"bounty_contracts", "Start of the status phase", "Pick a public objective: gain 1 trade good whenever someone scores it this status phase.", true},
    {"arbitrage", "When you pass", "Gain 4 commodities; any beyond your commodity value are trade goods instead.", true},
    {"micrometeoroid_storm_ds", "Action", "Pick a system with another player's ships: roll a die for each fighter there, a 6 or more destroys one.", true},
    {"professional_archeologists", "Action", "Look at the top card of each exploration deck: keep relic fragments, else discard it for 1 commodity.", true},
    {"emergency_meeting", "Start of the agenda phase", "Shuffle the discarded agendas back in, look at the top 3 and put each on top or bottom.", true},
    {"escape_clause", "When another player plays your Trade Agreement", "Keep your commodities: they return the note and gain 2 trade goods.", true},
    {"personnel_writ", "Action", "Ready your agent, or spend a strategy token to unlock your commander.", true},
    {"preparation", "Action", "Draw 1 action card.", true},
    {"classified_weapons", "Before your units roll for a unit ability", "+4 to each of those rolls; their hits can't be cancelled.", true},
    // ---- Thunder's Edge (0.34.0, only with its module)
    {"strategize", "Action", "Resolve the secondary ability of a strategy card that is still readied or that nobody chose.", true},
    {"overrule", "Action", "Resolve the primary ability of a strategy card that is still readied or that nobody chose.", true},
    {"piratecontract", "Action", "Place 1 neutral destroyer in a system without any player's ships (not a home system).", true},
    {"piratefleet", "Action", "Spend 3 resources: place a neutral carrier, cruiser, destroyer and 2 fighters in a system without any "
                    "player's ships (not a home system).", true},
    {"mercenarycontract", "Action", "Spend 2 trade goods: place 2 neutral infantry on a planet with no units outside home systems; "
                          "whoever controlled it loses it.", true},
    {"brilliance", "Action", "Ready one of your planets that has a technology specialty.", true},
    {"exchangeprogram", "Action", "Pick a player: if you both agree, each of you places 1 infantry, coexisting, on a planet the other "
                        "controls with their ground forces; if not, you both discard 1 fleet pool token.", true},
    {"rescue", "After another player moves ships into a system with your ships",
     "Move 1 of your ships there from a system without your command token.", true},
    {"crashlanding", "When your last ship in the active system is destroyed",
     "Put 1 of your ground forces from its space area on a planet there (not Mecatol Rex); with other players' units there, "
     "it coexists.", true},
    {"puppetsonastring", "End of a player's turn, if you have passed", "Take 1 action.", true},
    {"crisis", "End of a player's turn, while 2 or more players haven't passed", "The next player's turn is skipped.", true},
    {"lieinwait", "After 2 of your neighbours trade with each other", "Look at both their hands of action cards and take 1 from each.", true},
    {"extremeduress", "Start of another player's turn, if they have a readied strategy card",
     "Unless their next action is a strategic action, they discard their action cards, give you their trade goods and show "
     "you their secret objectives.", true},
    {"blackmarketdealing", "While you negotiate a trade", "You and the other player may also trade relics, action cards and "
                           "unscored secret objectives. It can't be cancelled.", true},
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
const KeyText2 kExText[] = {
    {"frln", "You may produce 1 unit in this system, spending influence as if it were resources if you like."},
    {"mo", "You may place 1 infantry on this planet."},
    {"gw", "A gamma wormhole token is placed in this system."},
    {"dmz", "Attach: structures here go back to reinforcements and ground forces to space. Units can't land, be produced or be placed on this planet."},
    {"ds", "Attach: +2 resources and +1 influence."},
    {"pw", "Attach: +2 influence."},
    {"toe", "Attach: +1 influence. The holder of the Crown of Emphidia who controls it can turn the crown into 1 victory point."},
    {"crf", "Relic fragment. Purge 3 cultural fragments (as an action) to gain a relic."},
    {"vfs", "If you have a mech here, or remove 1 infantry from this planet: gain 1 command token."},
    {"exp", "If you have a mech here, or remove 1 infantry from this planet: ready this planet."},
    {"cm", "If you have a mech here, or remove 1 infantry from this planet: gain 1 trade good."},
    {"ls", "Attach: +1 resource and +2 influence."},
    {"mw", "Attach: +2 resources."},
    {"rw", "Attach: +1 resource."},
    {"warfare", "Attach: a red technology specialty (or +1 resource and +1 influence if the planet already has one)."},
    {"hrf", "Relic fragment. Purge 3 hazardous fragments (as an action) to gain a relic."},
    {"aw", "Gain 2 commodities, or turn up to 2 of your commodities into trade goods."},
    {"fb", "Gain 1 commodity, or spend 1 trade good or commodity to draw 1 action card."},
    {"lf", "Gain 1 commodity, or spend 1 trade good or commodity to place 1 mech on this planet."},
    {"biotic", "Attach: a green technology specialty (or +1 resource and +1 influence if the planet already has one)."},
    {"cybernetic", "Attach: a yellow technology specialty (or +1 resource and +1 influence if the planet already has one)."},
    {"propulsion", "Attach: a blue technology specialty (or +1 resource and +1 influence if the planet already has one)."},
    {"irf", "Relic fragment. Purge 3 industrial fragments (as an action) to gain a relic."},
    {"dv", "Draw 1 secret objective."},
    {"ed", "Keep this card. Action: spend 6 resources and purge it to research a technology."},
    {"gamma", "A gamma wormhole token is placed in this system."},
    {"ion", "The ion storm token goes here, alpha or beta side up. It flips each time ships travel through its wormhole."},
    {"lc", "Draw 2 action cards."},
    {"ms", "Refill your commodities, or turn your commodities into trade goods."},
    {"mirage", "The Mirage planet appears in this system; you gain it, ready."},
    {"urf", "Relic fragment of any kind: counts towards any set of 3."},
    // Uncharted Space (0.33.0)
    {"starchartcultural", "Keep this card; it can be traded. Action: purge 2 of your Star Charts to draw a random blue-backed system tile and place it at the edge of the board, next to at least 2 systems."},
    {"starcharthazardous", "Keep this card; it can be traded. Action: purge 2 of your Star Charts to draw a random blue-backed system tile and place it at the edge of the board, next to at least 2 systems."},
    {"starchartindustrial", "Keep this card; it can be traded. Action: purge 2 of your Star Charts to draw a random blue-backed system tile and place it at the edge of the board, next to at least 2 systems."},
    {"starchartfrontier", "Keep this card; it can be traded. Action: purge 2 of your Star Charts to draw a random blue-backed system tile and place it at the edge of the board, next to at least 2 systems."},
    {"forgottentradestation", "Gain 1 trade good for each planet in this system."},
    {"distinguishedadmiral", "Move 1 of your command tokens to another pool, or spend 2 influence to gain 1 command token."},
    {"councilpreserve", "Attach: when you exhaust this planet to vote, cast 5 more votes."},
    {"scorcheddepot", "If you have a mech here, or remove 1 infantry from this planet: remove 1 command token from this system, or move 1 of your command tokens to another pool."},
    {"arcanecitadel", "Attach: when you exhaust this planet, place 1 infantry on it. Your units get +1 on combat rolls on this planet."},
    {"hiddenlaboratory", "Explore the frontier deck in this system."},
    {"seedyspaceport", "If you have a mech here, or remove 1 infantry from this planet: ready your agent or draw 1 action card."},
    {"ancientshipyard", "Place 1 cruiser in this system if no other player has ships here."},
    {"orbitalfoundries", "Attach: this planet has Production 2, as if it were a unit."},
    {"folderspace", "Put 1 command token from your reinforcements in your fleet pool. You may move your units here to the space area of any system without other players' ships."},
    {"suspiciouswreckage", "Choose 2 other players: each gives you 1 promissory note from their hand."},
    {"darkvisions", "Look at the top card of each exploration deck and discard any of them. You may explore one of your planets. Then each deck is shuffled."},
    {"warforgeruins", "If you have a mech here, or remove 1 infantry from this planet: place 1 mech or 2 infantry on it."},
    {"arcaneshieldemitter", "Attach: all infantry here go up to the space area (or are destroyed). This planet can't be bombarded; no infantry can land, be produced or be placed on it."},
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
const KeyText2 kLegendText[] = {
    {"hopesend", "End of your turn: exhaust to place 1 mech on a planet you control, or draw 1 action card."},
    {"primor", "End of your turn: exhaust to place up to 2 infantry on planets you control."},
    {"mirage", "End of your turn: exhaust to place up to 2 fighters in a system with your ships."},
    {"mallice", "End of your turn: exhaust to gain 2 trade goods, or turn all your commodities into trade goods."},
    // Uncharted Space (0.33.0)
    {"silence", "End of your turn: exhaust to place 1 cruiser from your reinforcements in a system with your ships."},
    {"echo", "End of your turn: exhaust to put a frontier token in a system without planets."},
    {"tarrock", "After an agenda is revealed: exhaust to predict its outcome; if you are right, draw 1 secret objective."},
    {"prism", "End of your turn: exhaust and purge one of your technologies (not a faction or unit upgrade technology) to "
              "gain another with the same number of prerequisites."},
    {"domna", "End of your turn: exhaust to move 1 of your ships to an adjacent system without other players' ships."},
    // Thunder's Edge (0.34.0)
    {"emelpar", "End of your turn: exhaust to ready another of your components (not a strategy card)."},
    {"faunus", "When you pass: exhaust to take a planet outside home systems that isn't legendary and has no units and no attachments."},
    {"garbozia", "When you pass: exhaust to put 1 action card from the discard pile on this card; you may play it as if it were in "
                 "your hand, and it is purged."},
    {"industrex", "When you pass: exhaust to place 1 ship of a type you have the unit upgrade for in a system with your ships."},
    {"tempesta", "After you activate a system: exhaust so 1 of your ships moves 1 further this tactical action."},
    {"styx", "Worth 1 victory point while you hold it."},
    {"thundersedge", "Gain your breakthrough with it if you haven't. End of your turn: exhaust to take another action."},
    {"avernus", "ACTION: Exhaust this card to use the Embers of Muaat's STAR FORGE faction ability without spending a command token."},
    {"ordinian", "You may exhaust this card when you pass to draw 1 action card and gain 1 command token."},
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
    static const char* t[] = {
        "Play after its owner activates a system with your units: they can't move units into it. Then it goes back.",
        "When its owner refills commodities, they give them all to you. Then it goes back.",
        "Play when an agenda is revealed: its owner can't vote or play action cards on that agenda. Then it goes back.",
        "While you hold it: +1 victory point. You lose the point and return it if you activate a system with its owner's units.",
    };
    return type >= 0 && type < kNoteTypes ? t[type] : "";
}

}  // namespace ti
