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

const AgendaText kAgText[] = {
    {"abolishment", "Discard the elected law.", "", true},
    {"arbiter", "Elected player keeps this card: after the strategy phase they may discard it to swap a strategy card with another player.", "", true},
    {"arms_reduction", "Everyone destroys all but 2 dreadnoughts and all but 4 cruisers.", "Next strategy phase, everyone exhausts their tech-specialty planets.", true},
    {"articles_war", "Mechs lose their printed abilities except Sustain Damage.", "Everyone who voted For gains 3 trade goods.", true},
    {"artifact", "Speaker rolls: 6-10, everyone may research 2 technologies; 1-5, everything at Mecatol Rex is destroyed and units next to it suffer.", "Nothing happens.", true},
    {"censure", "Elected player gains 1 victory point but can't play action cards (loses the point if they lose this card).", "", true},
    {"checks", "When you pick a strategy card you must give it to a player who has none (fewer than 2 with 3-4 players).", "This agenda phase, everyone readies only 3 planets at the end.", true},
    {"cladenstine", "Everyone returns 2 tokens from their command sheet to reinforcements.", "Everyone returns 1 fleet token to reinforcements.", true},
    {"classified", "The elected scored secret objective becomes a public objective.", "", true},
    {"committee", "Elected player keeps this card: they may discard it to decide an election without a vote.", "", true},
    {"conscription", "Fighters and infantry come one at a time for their cost, not two.", "Nothing happens.", true},
    {"constitution", "Discard all laws; next strategy phase everyone exhausts their home planets.", "Nothing happens.", true},
    {"conventions", "No bombardment against units on cultural planets.", "Everyone who voted Against discards all their action cards.", true},
    {"covert", "The speaker secretly draws the next agenda; everyone votes on its outcomes without seeing it.", "", true},
    {"crisis", "Everyone may use the elected strategy card's secondary ability for free.", "", true},
    {"defense_act", "No limit on PDS per planet.", "Everyone destroys 1 of their PDS.", true},
    {"disarmament", "Destroy all ground forces on the elected planet; its owner gains a trade good for each.", "", true},
    {"economic_equality", "Everyone discards their trade goods, then gains 5.", "Everyone discards their trade goods.", true},
    {"execution", "Elected player discards their action cards, loses the speaker token and can't vote again this agenda phase.", "", true},
    {"grant_reallocation", "Elected player gains any technology, then loses a fleet token per prerequisite.", "", true},
    {"incentive", "Reveal an extra stage I public objective.", "Reveal an extra stage II public objective.", true},
    {"minister_antiquities", "Elected player gains a relic.", "", true},
    {"minister_commerce", "Elected player: after refilling commodities, gain 1 trade good per neighbour.", "", true},
    {"minister_exploration", "Elected player: gain 1 trade good whenever you gain control of a planet.", "", true},
    {"minister_industry", "Elected player: when you place a space dock, your units in that system may produce.", "", true},
    {"minister_peace", "Elected player may discard it to end the turn of a player who activates a system with another player's units.", "", true},
    {"minister_policy", "Elected player draws 1 action card at the end of each status phase.", "", true},
    {"minister_sciences", "Elected player researches with the Technology card without paying resources.", "", true},
    {"minister_war", "Elected player may discard it after an action: return a command token from the board and take another action.", "", true},
    {"miscount", "Vote again on the elected law as if it were just revealed.", "", true},
    {"mutiny", "Everyone who voted For gains 1 victory point.", "Everyone who voted For loses 1 victory point.", true},
    {"nexus", "The wormhole nexus's alpha and beta wormholes don't work for movement.", "A gamma wormhole token is placed in the Mecatol Rex system.", true},
    {"plowshares", "Everyone destroys half their infantry on each planet (round up) and gains a trade good for each.", "Everyone places 1 infantry on each planet they control.", true},
    {"prophecy", "Elected player's fighters get +1 in combat; they lose this card when they produce fewer than 2 fighters.", "", true},
    {"rearmament", "Everyone places a mech on a home planet.", "Everyone replaces each mech with an infantry.", true},
    {"redistribution", "Destroy every unit on the elected planet; its owner picks a player with the fewest points, who may put 1 infantry there.", "", true},
    {"regulations", "Nobody may have more than 4 fleet tokens.", "Everyone adds 1 token to their fleet pool.", true},
    {"rep_govt", "Votes: 1 per player; planets aren't exhausted to vote.", "Next strategy phase, everyone who voted Against exhausts their cultural planets.", true},
    {"revolution", "After researching a technology, a player destroys 1 of their non-fighter ships.", "Next strategy phase, everyone exhausts 1 planet per technology they own.", true},
    {"sanctions", "Action card hand limit becomes 3.", "Everyone discards 1 random action card.", true},
    {"schematics", "If anyone owns War Sun, everyone ignores its prerequisites; war suns lose Sustain Damage.", "Everyone who owns War Sun discards all their action cards.", true},
    {"secret", "Elected player draws a secret objective.", "", true},
    {"seed_empire", "The player with the most victory points gains 1.", "The player with the fewest victory points gains 1.", true},
    {"shared_research", "Units can move through nebulae.", "Everyone puts a command token in their home system.", true},
    {"standardization", "Elected player resets their command sheet to 3 tactic, 3 fleet, 2 strategy.", "", true},
    {"travel_ban", "Alpha and beta wormholes don't work for movement.", "Destroy every PDS in or next to a wormhole system.", true},
    {"unconventional", "Everyone who voted For draws 2 action cards.", "Everyone who voted For discards all their action cards.", true},
    {"warrant", "Elected player draws 2 secret objectives and plays with their secrets revealed.", "", true},
    {"wormhole_recon", "All alpha and beta wormhole systems are adjacent to each other.", "Everyone puts a command token in each wormhole system that holds their ships.", true},
    {"wormhole_research", "Players with ships at a wormhole may research a technology; then destroy all ships at alpha/beta wormholes.", "Everyone who voted Against returns a command token from their sheet.", true},
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
const KeyText2 kRelicText[] = {
    {"dominusorb", "Before moving in a tactical action, purge it: this movement your units may leave systems that hold your command tokens."},
    {"mawofworlds", "At the start of the agenda phase, purge it and exhaust all your planets to gain any 1 technology."},
    {"emelpar", "When you would spend a strategy token, exhaust it to spend one from your reinforcements instead."},
    {"shard", "Worth 1 victory point while you hold it. Whoever takes one of your legendary or home planets takes this card too."},
    {"stellarconverter", "Action: purge it to destroy a planet (not home, legendary or Mecatol Rex) next to your bombardment units - every unit on it too."},
    {"codex", "Action: purge it to take up to 3 action cards of your choice from the discard pile."},
    {"emphidia", "After your tactical action, exhaust it to explore one of your planets. End of the status phase: if you control the Tomb of Emphidia, purge it for 1 victory point."},
    {"thalnos", "In each combat round you may reroll any of your dice with +1; a unit whose reroll misses is destroyed."},
    {"obsidian", "When you gain it, draw a secret objective. You may hold 1 extra secret objective."},
    {"prophetstears", "When you research a technology, exhaust it to ignore 1 prerequisite or to draw 1 action card."},
    // Uncharted Space (0.33.0)
    {"boon_of_the_cerulean_god", "Your units with Production have +1 Production. Each time you produce units, they cost 1 less in total."},
    {"decrypted_cartoglyph", "Action: purge it to draw 3 random blue-backed system tiles; place 1 at the edge of the board (next to at least 2 systems) and purge the others."},
    {"e6-g0_network", "You may hold 2 more action cards, whatever else applies. At any time, exhaust it to draw 1 action card."},
    {"eye_of_vogul", "After you activate a system, purge it: 2 of your ships count as adjacent to the active system for this tactical action."},
    {"starfall_array", "When your units roll for a unit ability (space cannon, bombardment, anti-fighter barrage), +1 to each die and 1 of them rolls 1 more die."},
    {"throne_of_the_false_emperor", "Action: purge it to gain 1 relic, or to score now a secret objective you meet - one of yours, or one another player has scored."},
    {"twilight_mirror", "At the start of the agenda phase, you may purge it to take 1 action (tactical or component, not strategic)."},
    // Thunder's Edge (0.34.0)
    {"bookoflatvinia", "When you gain it, research up to 2 technologies without prerequisites. Action: purge it - with planets of "
                       "all 4 specialties, gain 1 victory point; otherwise take the speaker token."},
    {"circletofthevoid", "Your units ignore gravity rift rolls and the other anomalies' effects on movement. Action: exhaust it to "
                         "explore a frontier token in a system without other players' ships."},
    {"heartofixth", "After any die is rolled, exhaust it to add 1 to it or take 1 from it."},
    {"lightrailordnance", "Your space docks have Space Cannon 5 (x2), and may fire at ships in adjacent systems."},
    {"metalivoidarmaments", "In space combat's anti-fighter barrage step, you may roll Anti-Fighter Barrage 6 (x3) against your "
                            "opponent's units."},
    {"metalivoidshielding", "Each time hits are made against your non-fighter ships, one of them may use Sustain Damage as if "
                            "it had it."},
    {"neuraloop", "When a public objective is revealed, you may purge one of your relics to replace it with a random objective "
                  "from any deck (a public one now)."},
    {"quantumcore", "When you gain it, gain your breakthrough. You have synergy for every technology colour."},
    {"thesilverflame", "May be traded. Action: roll a die and purge it - on a 10 gain 1 victory point; otherwise your home system "
                       "and everything in it is purged and you can't score public objectives."},
    {"thetriad", "Spend or exhaust it like a planet: its resources and influence are 3 plus the kinds of relic fragment you hold."},
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
