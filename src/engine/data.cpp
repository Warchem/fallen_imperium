#include "engine/data.h"

#include <cstring>

namespace ti {

#include "engine/gen/gamedata.inc"
#include "engine/gen/std_layouts.inc"  // (0.58.0)
#include "engine/gen/monuments.inc"    // (0.82.0 Monuments+)

const int kPlanetCount = int(sizeof(kPlanets) / sizeof(kPlanets[0]));
const int kSystemCount = int(sizeof(kSystems) / sizeof(kSystems[0]));
const int kUnitCount = int(sizeof(kUnits) / sizeof(kUnits[0]));
const int kTechCount = int(sizeof(kTechs) / sizeof(kTechs[0]));
const int kFactionCount = int(sizeof(kFactions) / sizeof(kFactions[0]));
const int kObjectiveCount = int(sizeof(kObjectives) / sizeof(kObjectives[0]));
const int kLayoutCount = int(sizeof(kLayouts) / sizeof(kLayouts[0]));
const int kStdLayoutCount = int(sizeof(kStdLayouts) / sizeof(kStdLayouts[0]));
const int kActionCardCount = int(sizeof(kActionCards) / sizeof(kActionCards[0]));
const int kAgendaCount = int(sizeof(kAgendas) / sizeof(kAgendas[0]));
const int kExploreCardCount = int(sizeof(kExploreCards) / sizeof(kExploreCards[0]));
const int kRelicCount = int(sizeof(kRelics) / sizeof(kRelics[0]));
const int kLeaderCount = int(sizeof(kLeaders) / sizeof(kLeaders[0]));

int findLeader(const char* key) {
    for (int i = 0; i < kLeaderCount; ++i)
        if (!std::strcmp(kLeaders[i].key, key)) return i;
    return -1;
}

int findSystem(const char* key) {
    for (int i = 0; i < kSystemCount; ++i)
        if (!std::strcmp(kSystems[i].key, key)) return i;
    return -1;
}
int findTech(const char* key) {
    for (int i = 0; i < kTechCount; ++i)
        if (!std::strcmp(kTechs[i].key, key)) return i;
    return -1;
}
int findUnit(const char* key) {
    for (int i = 0; i < kUnitCount; ++i)
        if (!std::strcmp(kUnits[i].key, key)) return i;
    return -1;
}
int findFaction(const char* key) {
    for (int i = 0; i < kFactionCount; ++i)
        if (!std::strcmp(kFactions[i].key, key)) return i;
    return -1;
}
int findObjective(const char* key) {
    for (int i = 0; i < kObjectiveCount; ++i)
        if (!std::strcmp(kObjectives[i].key, key)) return i;
    return -1;
}

int findActionCard(const char* key) {
    for (int i = 0; i < kActionCardCount; ++i)
        if (!std::strcmp(kActionCards[i].key, key)) return i;
    return -1;
}
int findAgenda(const char* key) {
    for (int i = 0; i < kAgendaCount; ++i)
        if (!std::strcmp(kAgendas[i].key, key)) return i;
    return -1;
}

int findExploreCard(const char* key) {
    for (int i = 0; i < kExploreCardCount; ++i)
        if (!std::strcmp(kExploreCards[i].key, key)) return i;
    return -1;
}
int findRelic(const char* key) {
    for (int i = 0; i < kRelicCount; ++i)
        if (!std::strcmp(kRelics[i].key, key)) return i;
    return -1;
}
int findPlanet(const char* key) {
    for (int i = 0; i < kPlanetCount; ++i)
        if (!std::strcmp(kPlanets[i].key, key)) return i;
    return -1;
}

int genericUnit(UnitType t) {
    static int cache[kUnitTypes];
    static bool ready = false;
    if (!ready) {
        for (int k = 0; k < kUnitTypes; ++k) {
            cache[k] = -1;
            for (int i = 0; i < kUnitCount; ++i)
                if (kUnits[i].type == UnitType(k) && kUnits[i].faction < 0 && kUnits[i].upgradeOf < 0) {
                    cache[k] = i;
                    break;
                }
        }
        ready = true;
    }
    return cache[int(t)];
}

int unitByKey(const char* key) {
    for (int i = 0; i < kUnitCount; ++i)
        if (!std::strcmp(kUnits[i].key, key)) return i;
    return -1;
}

const char* unitTypeName(UnitType t, bool plural) {
    static const char* one[] = {"Carrier", "Cruiser", "Destroyer", "Dreadnought", "Fighter", "Flagship",
                                "War Sun", "Infantry", "Mech", "PDS", "Space Dock", "Second flagship", "Monument"};
    static const char* many[] = {"Carriers", "Cruisers", "Destroyers", "Dreadnoughts", "Fighters", "Flagships",
                                 "War Suns", "Infantry", "Mechs", "PDS", "Space Docks", "Second flagships", "Monuments"};
    int i = int(t);
    if (i < 0 || i >= kUnitTypes) return "?";
    return plural ? many[i] : one[i];
}

const char* unitTypeShort(UnitType t) {
    static const char* s[] = {"CV", "CA", "DD", "DN", "FF", "FS", "WS", "IN", "MC", "PD", "SD", "FS2", "MN"};
    int i = int(t);
    return (i >= 0 && i < kUnitTypes) ? s[i] : "?";
}

const char* traitName(Trait t) {
    switch (t) {
        case Trait::Cultural: return "Cultural";
        case Trait::Hazardous: return "Hazardous";
        case Trait::Industrial: return "Industrial";
        default: return "";
    }
}

const char* colorName(TechColor c) {
    switch (c) {
        case TechColor::Blue: return "Propulsion";
        case TechColor::Green: return "Biotic";
        case TechColor::Red: return "Warfare";
        case TechColor::Yellow: return "Cybernetic";
        default: return "Faction";
    }
}

const char* factionShortName(int f) {
    static const struct {
        const char* key;
        const char* name;
    } names[] = {{"arborec", "Arborec"}, {"ghost", "Creuss"},   {"hacan", "Hacan"},       {"jolnar", "Jol-Nar"},
                 {"l1z1x", "L1Z1X"},     {"letnev", "Letnev"},  {"mentak", "Mentak"},     {"muaat", "Muaat"},
                 {"naalu", "Naalu"},     {"nekro", "Nekro"},    {"saar", "Saar"},         {"sardakk", "Sardakk N'orr"},
                 {"sol", "Sol"},         {"winnu", "Winnu"},    {"xxcha", "Xxcha"},       {"yin", "Yin"},
                 {"yssaril", "Yssaril"}, {"argent", "Argent"},  {"cabal", "Vuil'raith"},  {"empyrean", "Empyrean"},
                 {"mahact", "Mahact"},   {"nomad", "Nomad"},    {"naaz", "Naaz-Rokha"},   {"titans", "Titans of Ul"},
                 {"keleres", "Keleres"}, {"bastion", "Last Bastion"}, {"ralnel", "Ral Nel"}, {"crimson", "Crimson"},
                 {"deepwrought", "Deepwrought"}, {"firmament", "Firmament"}, {"obsidian", "Obsidian"},
                 {"atokera", "Atokera"}, {"belkosea", "Belkosea"}, {"pharadn", "Pharad'n"}};  // (0.85.2: Blue Reverie)
    if (f < 0 || f >= kFactionCount) return "?";
    for (const auto& n : names)
        if (!std::strcmp(n.key, kFactions[f].key)) return n.name;
    return kFactions[f].name;
}

// ---------------------------------------------------------------------------
// Technology descriptions (our own short wording).
namespace {
struct KeyText {
    const char* key;
    const char* text;
    bool done;  // effect implemented in the rules engine
};

const KeyText kTechText[] = {
    // (the shared technologies review, 2026-11-14: the cards' wording - AsyncTI4 technologies/pok.json; X-89 = Codex I's
    // version, which the game plays without the Codex)
    {"amd", "Your ships can move into and through asteroid fields. When other players' units use SPACE CANNON against your units, apply -1 to the result of each die roll.", true},
    {"gd", "After you activate a system, apply +1 to the move value of 1 of your ships during this tactical action.", true},
    {"fl", "During each of your turns of the action phase, you may perform 2 actions instead of 1.", true},
    {"lwd", "Your ships can move through systems that contain other players' ships.", true},
    {"det", "After you perform a tactical action in a system that contains a frontier token, if you have 1 or more ships in that system, explore that token. Your ships can retreat into adjacent systems that do not contain other players' units, even if you do not have units or control planets in that system.", true},
    {"sr", "ACTION: Exhaust this card to produce 1 ship in any system that contains one of your space docks.", true},
    {"nm", "During the status phase, draw 2 action cards instead of 1.", true},
    {"dxa", "After you win a ground combat, you may place 1 infantry from your reinforcements on that planet.", true},
    {"hm", "During the status phase, gain 3 command tokens instead of 2.", true},
    {"x89_base", "After 1 or more of your units use BOMBARDMENT against a planet, if at least 1 of your opponent's infantry was destroyed, you may destroy all of your opponent's infantry on that planet.", true},
    {"pa", "You can use technology specialties on planets you control without exhausting them, even if those planets are exhausted. During the action phase, you can exhaust planets you control that have technology specialties to gain 1 trade good.", true},
    {"bs", "You may exhaust this card at the end of your turn to ready 1 of your planets that has a technology specialty or 1 of your other technologies.", true},
    {"ps", "When 1 or more of your units use BOMBARDMENT or SPACE CANNON, 1 of those units may roll 1 additional die.", true},
    {"md_base", "You may exhaust this card at the start of a round of ground combat on a planet that contains 1 or more of your units that have PLANETARY SHIELD; your opponent cannot make combat rolls this combat round.", true},
    {"da", "During each combat round, after you assign hits to your units, repair 1 of your damaged units that did not use SUSTAIN DAMAGE during this combat round.", true},
    {"asc", "At the start of a space combat in a system that contains 3 or more of your non-fighter ships, your opponent must destroy 1 of their non-fighter ships.", true},
    {"aida", "When you research a unit upgrade technology, you may exhaust this card to ignore any 1 prerequisite. When 1 or more of your units use PRODUCTION, you may exhaust this card to reduce the combined cost of the produced units by the number of unit upgrade technologies that you own.", true},
    {"sar", "After 1 or more of your units use PRODUCTION, you may exhaust this card to place 1 mech from your reinforcements on a planet you control in that system. After 1 of your mechs is destroyed, gain 1 trade good.", true},
    {"st", "When 1 or more of your units use PRODUCTION, reduce the combined cost of the produced units by 1.", true},
    {"gls", "You may exhaust this card before 1 or more of your units uses SPACE CANNON; hits produced by those units must be assigned to non-fighter ships if able.", true},
    {"td", "You may exhaust this card at the start of your turn during the action phase; remove up to 4 of your ground forces from the game board and place them on 1 or more planets you control.", true},
    {"ie", "After you gain control of a planet, you may produce any number of units on that planet that have a combined cost equal to or less than that planet's resource value.", true},
    {"sdn", "When you activate a system, you may explore 1 planet in that system which contains 1 or more of your units.", true},
    {"pi", "At the end of your turn, you may exhaust this card to redistribute your command tokens. When you cast votes during the agenda phase, you may cast 3 additional votes; if you do, and the outcome you voted for is not resolved, exhaust this card.", true},
    // Faction technologies (Stage 5, batch 1)
    {"l4", "During an invasion, units cannot use SPACE CANNON against your units.", true},  // (the Letnev review: the cards' wording)
    {"nes", "When 1 of your units uses SUSTAIN DAMAGE, cancel 2 hits instead of 1.", true},
    // (the Hacan review, 2026-11-12: the cards' wording)
    {"qdn", "At the end of the strategy phase, you may spend 1 token from your strategy pool and give another player 3 of your trade goods. If you do, give 1 of your strategy cards to that player and take 1 of their strategy cards.", true},
    {"pm", "ACTION: Exhaust this card and spend 1 token from your strategy pool to gain 4 trade goods and choose 1 other player; that player gains 2 trade goods.", true},
    {"ers", "After another player activates a system that contains 1 or more of your ships, gain 4 trade goods.", true},
    {"scc", "You may exhaust this card after you activate a system that contains 1 or more of your units; that system is adjacent "
            "to all other systems that contain 1 or more of your units during this activation.", true},
    {"vpw", "After making combat rolls during a round of ground combat, if your opponent produced 1 or more hits, you produce 1 additional hit.", true},
    // (the Xxcha review, 2026-11-13: the cards' wording)
    {"it", "You may exhaust this card and spend 1 token from your strategy pool when another player plays an action card; cancel that "
           "action card.", true},
    {"nf", "After another player activates a system that contains 1 or more of your ships, you may exhaust this card and spend 1 token "
           "from your strategy pool; immediately end that player's turn.", true},
    // Faction technologies (Stage 5, batch 2)
    {"bio", "At the end of the status phase, you may remove any number of infantry from planets you control and place them on 1 or more planets you control in the same or adjacent systems.", true},
    {"is", "You may exhaust this card and spend 2 resources when you research a technology; ignore all of that technology's prerequisites.", true},  // (the L1Z1X review: the card's wording)
    // (the Mentak review, 2026-11-13: the cards' wording)
    {"mc", "When you spend trade goods, each trade good is worth 2 resources or influence instead of 1.", true},
    {"so", "After you win or lose a space combat, gain 1 trade good; if you won the combat, you may also produce 1 ship in that system "
           "of any ship type that was destroyed during the combat.", true},
    {"ng", "After another player activates a system that contains 1 or more of your ships, that player removes 1 token from their fleet "
           "pool and returns it to their reinforcements.", true},
    // (the Yin review, 2026-11-12: the cards' wording - Yin Spinner without the Codex / Thunder's Edge)
    {"yso", "After 1 or more of your units use PRODUCTION, place 1 infantry from your reinforcements on a planet you control in that system.", true},
    {"ic", "At the start of a space combat, you may destroy 1 of your cruisers or destroyers in the active system to produce 1 hit against your opponent's ships; that hit must be assigned by your opponent to 1 of their non-fighter ships if able.", true},
    // (the Yssaril review, 2026-11-12: the cards' wording)
    {"tp", "During your turn of the action phase, players that have passed cannot play action cards.", true},
    {"mi", "ACTION: Exhaust this card to look at another player's hand of action cards. Choose 1 of those cards and add it to your hand.", true},
    // Faction technologies (Stage 5, batch 3)
    {"wg", "ACTION: Exhaust this card to place or move a Creuss wormhole token into either a system that contains a planet you control or a non-home system that does not contain another player's ships.", true},
    {"ds", "At the start of space combat in a system that contains a wormhole and 1 or more of your ships, you may produce 1 hit and assign it to 1 of your opponent's ships.", true},
    {"mr", "Your ships can move into supernovas. Each supernova that contains 1 or more of your units gains the PRODUCTION 5 ability as if it were 1 of your units.", true},
    {"cm", "Other players cannot activate asteroid fields that contain 1 or more of your ships. At the start of your turn during the action "
           "phase, you may produce 1 unit in a system that contains at least 1 of your units that has PRODUCTION.", true},
    // (the Winnu review, 2026-11-12: the cards' wording)
    {"lgf", "During your tactical actions, if you do not control Mecatol Rex, treat its system as if it has both an \xCE\xB1 and \xCE\xB2 wormhole. ACTION: If you control Mecatol Rex, exhaust this card to place 1 infantry from your reinforcements on Mecatol Rex.", true},
    {"htp", "Exhaust this card when 1 or more of your units use PRODUCTION; swap the resource and influence values of 1 planet you control during that use of Production.", true},
    // (the Nekro review, 2026-11-14: the cards' wording)
    {"vax", "When you would gain another player's technology using 1 of your faction abilities, you may place the \"X\" assimilator token "
            "on a faction technology owned by that player instead. While that token is on a technology, this card gains that technology's "
            "text. You cannot place an assimilator token on technology that already has an assimilator token.", true},
    {"vay", "When you would gain another player's technology using 1 of your faction abilities, you may place the \"Y\" assimilator token "
            "on a faction technology owned by that player instead. While that token is on a technology, this card gains that technology's "
            "text. You cannot place an assimilator token on technology that already has an assimilator token.", true},
    // (the Empyrean review, 2026-11-12: the cards' wording)
    {"as", "After you or one of your neighbors activates a system that is adjacent to an anomaly, you may apply +1 to the move value of all of that player's ships during this tactical action.", true},
    {"vw", "After a player moves ships into a system that contains 1 or more of your units, they must give you 1 promissory note from their hand, if able.", true},
    // Faction technologies (Stage 5, batch 4)
    {"ah", "Other players cannot move ships through systems that contain your structures. Each planet that contains 1 or more of your "
           "structures gains the PRODUCTION 1 ability as if it were a unit.", true},  // (the Argent review, 2026-11-13: the card)
    {"gr", "You may exhaust this card before a player casts votes; that player must cast at least 1 vote for an outcome of your choice or remove 1 token from their fleet pool and return it to their reinforcements.", true},
    {"pfa", "After you explore a planet, ready that planet.", true},
    {"sc", "At the start of a combat round, you may exhaust this card to apply +1 to the result of each of your unit's combat rolls "
           "during this combat round.", true},
    // (the Nomad review, 2026-11-13: the card's wording)
    {"tcs", "After any player's agent becomes exhausted, you may exhaust this card to ready that agent; if you ready another player's agent, you may perform a transaction with that player.", true},
    {"vtx", "ACTION: Exhaust this card to choose another player's non-structure unit in a system that is adjacent to 1 or more of your space docks. Capture 1 unit of that type from that player's reinforcements.", true},  // (the Vuil'raith review: the card's wording)
    // Council Keleres (Thunder's Edge, 0.21.0)
    {"asn", "Once per action, when you resolve a unit's PRODUCTION ability, you may resolve another of your unit's PRODUCTION abilities in any system. (In another system: the units in this one have produced already.)", true},  // (the Keleres review: the card's wording)
    {"executiveorder", "ACTION: Exhaust this card and draw the top or bottom card of the agenda deck. Players immediately vote on this agenda as if you were the speaker; you can spend trade goods and resources on this agenda as if they were votes.", true},  // (the Keleres review: the card's wording)
    // Thunder's Edge factions (0.35.0)
    {"proxima", "Cancel 1 hit produced by BOMBARDMENT rolls made against your ground forces for each of your galvanized units present. "
                "At the start of a round of ground combat, you may resolve BOMBARDMENT 8 (x3) against your opponent's ground forces; if "
                "you do, make an identical roll against your ground forces.", true},
    {"helios2", "This unit's PRODUCTION value is equal to 4 more than the resource value of this planet. The resource value of this "
                "planet is increased by 2. Up to 3 fighters in this system do not count against your ships' capacity.", true},
    {"hydrothermal", "At the start of the status phase, gain 1 trade good for each ocean card in play.", true},  // (the Deepwrought review: the card's wording)
    {"radical", "At the start of the status phase, you may replace one of your non-unit upgrade technologies with a technology of the same color that has exactly 1 more prerequisite.", true},  // (the Deepwrought review: the card's wording)
    {"subatomic", "When one of your ships is destroyed: you may produce one of the same type at a space dock in your home system.", true},
    {"exile2", "Unit upgrade (destroyer): Exile II - combat 7, anti-fighter barrage 6 (x3); after any combat within 2 systems it may open a breach (active or not).", true},
    {"nanomachines", "ACTION: Exhaust this card to place 1 PDS on a planet you control. ACTION: Exhaust this card to repair all of your damaged units. ACTION: Exhaust this card and discard 1 action card to draw 1 action card.", true},  // (the Ral Nel review: the card's wording)
    {"linkship2", "Cost 1, Combat 8, Move 4, ANTI-FIGHTER BARRAGE 6 (x3). This unit can use the SPACE CANNON ability of one of your structures in its space area; each linkship can trigger the same structure.", true},  // (the Ral Nel review: the card's wording)
    {"planesplitter-firm", "When you gain this card, put The Fracture into play. Flip this card if the Obsidian faction is in play.", true},
    {"parasite-firm", "At the start of the status phase, you may place 1 infantry from your reinforcements on a planet you control in your home system. Flip this card if the Obsidian faction is in play.", true},
    {"planesplitter-obs", "When you perform a strategic action, you may move an ingress token into a system that contains or is adjacent to your units. This technology cannot be researched.", true},
    {"parasite-obs", "At the start of your turn, destroy 1 of another player's infantry in or adjacent to a system that contains your infantry. This technology cannot be researched.", true},
    // Discordant Stars batch 1 (0.37.0)
    {"dskortg", "After you win a combat in a system, you may exhaust this card to remove 1 command token from that system.", true},
    {"dskortr", "Once per space combat, after 1 of your non-fighter ships is destroyed, you may produce 1 hit and assign it to 1 of your opponent's non-carrier ships.", true},
    {"dsvaylr", "Once per action, after you win a ground combat, you may draw 1 action card.", true},
    {"dslaneb", "ACTION: Spend 1 command token from your strategy pool or purge 1 relic fragment to move 1 of your ships to an adjacent system that contains no other player's ships. If you spent a command token, exhaust this card.", true},
    {"dslaner", "After you explore, or purge 1 or more relic fragments, place 1 commodity token on this card. At the start of a round of combat, remove any number of tokens from this card to reroll up to that many of your dice during this round of combat.", true},
    {"dskyrog", "During the agenda phase, after an outcome you voted for is resolved, place 2 infantry from your reinforcements on a planet you control.", true},
    {"dskyroy", "At the start of the strategy phase, you may place any number of your trade goods on any combination of strategy cards. Then, place 2 trade goods from the supply on 1 strategy card.", true},
    {"dscheir", "After another player activates a system that contains 1 of your structures, you may exhaust this card to produce 1 ship in the active system. For each of your structures in the active system, you may remove that unit from the game board to reduce the cost of the produced ship by 3.", true},
    {"dsceldr", "At the end of your turn, you may exhaust this card and spend 1 token from your strategy pool to remove a command token from a system that contains 1 or more of your space docks.", true},
    // Discordant Stars batch 2 (0.38.0)
    {"dskhrag", "When you would spend a token from your strategy pool, you may exhaust 1 planet you control to spend 1 token from your reinforcements instead.", true},
    {"dsrhodb", "Once during your turn, when you exhaust 1 or more planets you control that have a technology specialty, you may produce 1 ship "
                "in 1 system that contains 1 of those planets.", true},
    {"dsrhody", "When you activate a system, or a system that contains 1 or more of your units is activated, you may choose 1 technology or "
                "non-fighter, non-structure unit upgrade technology another player controls; until the end of this tactical action, that card is "
                "treated as though it had no ability text.", true},
    {"dszelir", "When 1 or more of your units produce 1 or more hits during a BOMBARDMENT roll against a planet, produce 1 additional hit "
                "for that roll.", true},
    {"dsrohdy", "At the start of the agenda phase, for each unit upgrade technology you control, you may choose 1 player. Each of those players "
                "must produce 1 ship in a system that contains 1 or more of their space docks or war suns.", true},
    {"dstnely", "After a round of space combat in a system that contains 1 or more of your mechs, you may have 1 of those units become damaged "
                "to produce 1 hit and assign it to a cruiser, dreadnought, or destroyer you do not control in that system.", true},
    {"dskolug", "When you pass, you may gain 1 commodity for each other player who has already passed. ACTION: Exhaust this card.", true},
    {"dskolur", "After a system is activated, you may exhaust this card to choose 1 player. Apply +1 or -1 to all rolls that player makes during this tactical action.", true},
    // Discordant Stars batch 3 (0.39.0)
    {"dsbentg", "Each planet you control that has a technology specialty or an attachment gains the PRODUCTION 1 ability as if it were a unit.", true},  // (the Bentor review)
    {"dsbenty", "When 1 or more of your units use PRODUCTION, you may exhaust this card to increase the PRODUCTION value of 1 of those units to "
                "match the PRODUCTION value of the unit on the game board with the highest PRODUCTION value, or apply +2 to the total PRODUCTION "
                "value of those units.", true},
    {"dsdihmy", "When you explore a planet, you may produce 1 ship in that planet's system. Action: exhaust this card and 1 planet you control to explore that planet.", true},
    {"dsflorg", "When a player would perform a strategic action, you may exhaust this card and spend 1 token from your strategy pool to end that player's turn; the strategic action is not resolved and the strategy card is not exhausted.", true},
    {"dsgledb", "During movement, you may increase the move value of each of your non-fighter ships not transporting any fighters or infantry by 1.", true},
    {"dsaugug", "During the status phase, you may score 1 additional public objective instead of a secret objective.", true},  // (the Augurs review)
    {"dsauguy", "At the start of the status phase, you may spend 3 trade goods to research 1 technology.", true},
    {"dskjalg", "Once per action, after a player produces 1 or more hits during a round of combat in a system that contains a Glory token, gain 1 trade good. After a player rolls combat dice, you may exhaust this card to allow that player to reroll any number of those dice.", true},
    // Discordant Stars batch 4 (0.40.0)
    {"dskollb", "At the start of a space combat, you may choose 1 of your ships in the active system that did not begin this action in that system; roll a die. On a result equal to or greater than that ship's combat value, produce 1 hit; your opponent must assign it to 1 of their ships.", true},
    {"dskolly", "ACTION: Exhaust this card to choose 1 of your neighbors that has 1 or more relic fragments. That player gains 2 trade goods and must give you 1 of those relic fragments of your choice.", true},
    {"dsmortr", "After a unit you control in a system is destroyed, you may exhaust this card to place 1 unit of that type from your "
                "reinforcements in the space area of a system adjacent to that system that contains 1 or more of your ships.", true},
    {"dsmorty", "Each system that contains 1 or more of your ships and no planets gains PRODUCTION 2 as if it were a unit you control.", true},
    {"dsvadeb", "After you activate a system, you may exhaust this card and spend any number of trade goods; for each trade good you spent, apply "
                "+1 to the move value of 1 non-fighter ship you control.", true},
    {"dsvadey", "After another player's unit uses SUSTAIN DAMAGE to cancel a hit produced by your units or abilities, you may exhaust this card to "
                "destroy that unit. After you produce 1 or more hits during a round of combat, you may spend 1 trade good to produce 1 additional "
                "hit.", true},
    {"dscymiy", "At the start of your turn, you may exhaust this card to choose 1 non-home system other than Mecatol Rex that contains 1 or more of your units; place 1 other player's command token from their reinforcements in that system.", true},
    {"dsnivyy", "After 1 or more of your units with SUSTAIN DAMAGE makes a combat roll, you may choose 1 of those units to become damaged to "
                "reroll its combat roll.", true},
    {"dsolrab", "After you activate a system, you may exhaust a planet you control of the same type as a planet in that system to apply +1 to the "
                "move value of each of your ships during this tactical action.", true},
    {"dsolrar", "At the start of your turn, you may exhaust this card and 1 planet you control to ready 1 non-home planet other than Mecatol Rex.", true},
    // Discordant Stars batch 5 (0.41.0)
    {"dsfreeg", "At the start of the agenda phase, you may choose and exhaust 1 cultural, 1 hazardous, and 1 industrial planet. When you cast 1 or more votes, if you exhaust at least 1 cultural, 1 hazardous, and 1 industrial planet to cast votes, you may cast 4 additional votes.", true},
    {"dsfreey", "At the start of a ground combat, you may roll 1 die for each of up to 2 of your ground forces on that planet. For each result equal to or greater than that unit's combat value, produce 1 hit; your opponent must assign it to 1 of their units on that planet.", true},
    {"dsedyng", "Once per agenda phase, after an agenda is revealed, you may predict aloud an outcome of that agenda. If your prediction is correct, you may score 1 public objective if you fulfill its requirements; each other player who voted for that outcome draws 1 secret objective.", true},
    {"dsedyny", "You may exhaust this card to allow a player to exchange 1 of their relics or agendas as part of a transaction. You always vote last during the agenda phase. When an outcome you voted for or predicted is resolved, each player who voted for that outcome gains 1 commodity.", true},
    {"dsnokay", "During the action phase: You may exhaust this card to use the PRODUCTION ability of 1 of your units. Then, place 1 command "
                "token from your reinforcements in that unit's system.", true},
    {"dsaxisb", "After you activate a system, you may exhaust this card to choose 1 ship you control and roll a die. On a result of 1-3, remove "
                "that ship from the game board; on a result of 4-10, apply +2 to that ship's move value during this tactical action.", true},  // (the Axis review)
    {"dsaxisy", "ACTION: Exhaust this card to place or move 1 of your space docks onto a planet you control.", true},
    {"dsmykog", "After your opponent makes a combat roll, you may exhaust this card. If you do, for each of their units that did not produce a hit, "
                "you may reroll that unit's combat roll; any hits that roll produces are produced against your opponent's units instead.", true},
    {"dsghemg", "After you win a space combat, you may ready 1 planet you control.", true},
    // Discordant Stars batch 6 (0.42.0)
    {"dsveldr", "At the end of each round of space combat, you may use the SPACE CANNON of 1 of your units in the active system against your "
                "opponent's ships, without rolling additional dice.", true},
    {"dslizhb", "After you activate a non-home system, you may exhaust this card to choose 1 ship you control; treat that ship as adjacent "
                "to the active system until the end of this tactical action.", true},
    {"dsmirvr", "At the start of invasion, choose up to 1 unit you control in the active system; that unit gains PLANETARY SHIELD. When a player "
                "commits a unit to land on a planet you control, you may exhaust this card to destroy that unit.", true},
    {"dsghotg", "Up to 3 ships in your home system do not count against your fleet pool. When you cast 1 or more votes, you may exhaust this card to cast an additional number of votes equal to the number of command tokens in your fleet pool.", true},
    {"dsghoty", "When you produce 1 or more units, you may produce 1 additional unit in each of 2 systems that contain 1 or more of your ships and 1 of your command tokens.", true},
    // Blue Reverie (0.83.0, the card's wording): the Atokera Legacy
    {"dsatoky", "Once during each of your actions, choose 1 of your units and 1 unit ability. That unit gains that unit ability with a value of 3, if applicable, until the end of that action.", true},
    {"dsbelky", "At any time, you may exhaust this card to ready 1 non-technology component other than a strategy card in your play area.", true},  // (0.84.0)
    {"dspharb", "After you activate a system, you may choose 1 anomaly on the game board; that anomaly has no effect until the end of this tactical action.", true},  // (0.85.0)
};
}  // namespace

// 0.31.0: the Codex Omega wording, shown while a Codex game is on (setCodexTexts)
bool g_codexTexts = false;
bool g_codexLatest = true;  // 0.43.3: Thunder's Edge's Codex (the game's Setup::fixes >= 4)
namespace {
// 0.43.3 (owner): the Omega cards carry the sign after their names while the Codex is on - the cards Thunder's Edge's
// Codex puts in place of the originals: 3 faction technologies, Magen Defense Grid, X-89 Bacterial Weapon, 3 secret
// objectives, 5 promissory notes (factionNoteName), 1 mech and 6 leaders
struct OmegaName {
    char table;  // t technology, u unit, l leader, o objective
    const char* key;
    const char* omega;
    const char* plain;  // (filled in on the first switch)
};
OmegaName kOmegaNames[] = {
    {'t', "md_base", "Magen Defense Grid \xCE\xA9", nullptr},  {'t', "x89_base", "X-89 Bacterial Weapon \xCE\xA9", nullptr},
    {'t', "wg", "Wormhole Generator \xCE\xA9", nullptr},       {'t', "yso", "Yin Spinner \xCE\xA9", "Yin Spinner"},
    {'t', "mr", "Magmus Reactor \xCE\xA9", nullptr},           {'u', "naalu_mech", "Iconoclast \xCE\xA9", nullptr},
    {'l', "naaluagent", "Z'eu \xCE\xA9", nullptr},             {'l', "naalucommander", "M'aban \xCE\xA9", nullptr},
    {'l', "yinagent", "Brother Milor \xCE\xA9", nullptr},      {'l', "yincommander", "Brother Omar \xCE\xA9", nullptr},
    {'l', "yinhero", "Dannel of the Tenth \xCE\xA9", nullptr}, {'l', "xxchahero", "Xxekir Grom \xCE\xA9", nullptr},
    {'o', "mew", "Make an Example of Their World \xCE\xA9", nullptr},
    {'o', "ttfd", "Turn Their Fleets to Dust \xCE\xA9", nullptr},
    {'o', "fwp", "Fight with Precision \xCE\xA9", nullptr},
};
const char** omegaSlot(char table, const char* key) {
    switch (table) {
        case 't':
            for (int i = 0; i < kTechCount; ++i)
                if (!std::strcmp(kTechs[i].key, key)) return &kTechs[i].name;
            break;
        case 'u':
            for (int i = 0; i < kUnitCount; ++i)
                if (!std::strcmp(kUnits[i].key, key)) return &kUnits[i].name;
            break;
        case 'l':
            for (int i = 0; i < kLeaderCount; ++i)
                if (!std::strcmp(kLeaders[i].key, key)) return &kLeaders[i].name;
            break;
        default:
            for (int i = 0; i < kObjectiveCount; ++i)
                if (!std::strcmp(kObjectives[i].key, key)) return &kObjectives[i].name;
            break;
    }
    return nullptr;
}
}  // namespace
void setCodexTexts(bool on, bool latest) {
    g_codexTexts = on;
    g_codexLatest = latest;
    for (OmegaName& n : kOmegaNames) {
        const char** slot = omegaSlot(n.table, n.key);
        if (!slot) continue;
        if (!n.plain) n.plain = *slot;  // (Yin Spinner: the data's name says "Omega" - 0.44.6 shows the plain card's name)
        *slot = on ? n.omega : n.plain;
        // 0.44.6: the Codex IV versions (Thunder's Edge / the latest Codex) are Omega Omega
        if (on && latest && n.table == 't' && (!std::strcmp(n.key, "md_base") || !std::strcmp(n.key, "x89_base")))
            *slot = !std::strcmp(n.key, "md_base") ? "Magen Defense Grid \xCE\xA9\xCE\xA9" : "X-89 Bacterial Weapon \xCE\xA9\xCE\xA9";
    }
}
bool codexTexts() { return g_codexTexts; }
bool g_teXxchaTexts = false, g_teNaaluTexts = false;
void setThundersEdgeTexts(bool xxchaHero, bool naalu) {
    g_teXxchaTexts = xxchaHero;
    g_teNaaluTexts = naalu;
    if (const char** slot = omegaSlot('l', "xxchahero"))
        *slot = xxchaHero ? "Xxekir Grom" : g_codexTexts ? "Xxekir Grom \xCE\xA9" : "Xxekir Grom";
    // (fixes 27, the Naalu review) Thunder's Edge's own Z'eu and Iconoclast: their names without the Omega sign
    if (const char** slot = omegaSlot('l', "naaluagent")) *slot = naalu ? "Z'eu" : g_codexTexts ? "Z'eu \xCE\xA9" : "Z'eu";
    if (const char** slot = omegaSlot('u', "naalu_mech")) *slot = naalu ? "Iconoclast" : g_codexTexts ? "Iconoclast \xCE\xA9" : "Iconoclast";
}
bool teXxchaHeroTexts() { return g_teXxchaTexts; }
bool teNaaluTexts() { return g_teNaaluTexts; }
// 0.44.9 (owner's answer P1): games without the Codex / Thunder's Edge play the original Wormhole Generator
bool g_wgOriginal = false;
void setWormholeGeneratorOriginal(bool on) { g_wgOriginal = on; }
// (fixes 26, the Muaat review - owner 2026-11-12): games without the Codex / Thunder's Edge play the original Magmus Reactor
bool g_mrOriginal = false;
void setMagmusReactorOriginal(bool on) { g_mrOriginal = on; }
std::string plainName(const char* name) {
    std::string s = name ? name : "";
    while (s.size() >= 2 && s.compare(s.size() - 2, 2, "\xCE\xA9") == 0) s.resize(s.size() - 2);  // (0.44.6: Omega Omega too)
    if (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

const char* techSummary(int t) {
    if (t < 0 || t >= kTechCount) return "";
    if (g_mrOriginal && !std::strcmp(kTechs[t].key, "mr"))
        return "Your ships can move into supernovas. After 1 or more of your units use PRODUCTION in a system that either "
               "contains a war sun or is adjacent to a supernova, gain 1 trade good.";
    if (g_wgOriginal && !std::strcmp(kTechs[t].key, "wg"))
        return "At the start of the status phase, place or move a Creuss wormhole token into either a system that contains a "
               "planet you control or a non-home system that does not contain another player's ships.";
    if (g_codexTexts) {
        if (!std::strcmp(kTechs[t].key, "md_base"))  // (the shared technologies review: the card, Magen Defense Grid ΩΩ)
            return "When any player activates a system that contains 1 or more of your structures, place 1 infantry from your "
                   "reinforcements with each of those structures. At the start of ground combat on a planet that contains 1 or "
                   "more of your structures, produce 1 hit and assign it to 1 of your opponent's ground forces.";
        if (!std::strcmp(kTechs[t].key, "yso"))
            return "After you produce units, place up to 2 infantry from your reinforcements on any planet you control or in "
                   "any space area that contains 1 or more of your ships.";
        if (!std::strcmp(kTechs[t].key, "x89_base") && g_codexLatest)  // (the shared technologies review: the card, ΩΩ)
            return "Double the hits produced by your units' BOMBARDMENT and ground combat rolls. Exhaust each planet you use "
                   "BOMBARDMENT against.";
    }
    for (const auto& k : kTechText)
        if (!std::strcmp(k.key, kTechs[t].key)) return k.text;
    if (kTechs[t].unitUpgrade) {
        // (the shared technologies review, 2026-11-14) the upgraded unit's card text when it has one (Dreadnought II, PDS II...)
        for (int u = 0; u < kUnitCount; ++u)
            if (kUnits[u].tech == t && *unitAbilityText(u)) return unitAbilityText(u);
        return "Unit upgrade: improves the matching unit's stats.";
    }
    return "Faction technology. Its special effect arrives with faction abilities (Stage 5).";
}

bool techImplemented(int t) {
    if (t < 0 || t >= kTechCount) return false;
    if (kTechs[t].unitUpgrade) return true;
    for (const auto& k : kTechText)
        if (!std::strcmp(k.key, kTechs[t].key)) return k.done;
    return false;
}

// ---------------------------------------------------------------------------
// Objective descriptions. 0.61.0 (owner 2026-11-17: the card-wording pass): the public objectives in the cards' wording
// (AsyncTI4 public_objectives.json, base + PoK printings).
namespace {
const KeyText kObjText[] = {
    // Stage I (base)
    {"corner", "Control 4 planets that each have the same planet trait.", true},
    {"develop", "Own 2 unit upgrade technologies.", true},
    {"diversify", "Own 2 technologies in each of 2 colors.", true},
    {"monument", "Spend 8 resources.", true},
    {"expand_borders", "Control 6 planets in non-home systems.", true},
    {"research_outposts", "Control 3 planets that have technology specialties.", true},
    {"intimidate", "Have 1 or more ships in 2 systems that are adjacent to Mecatol Rex's system.", true},
    {"lead", "Spend a total of 3 tokens from your tactic and/or strategy pools.", true},
    {"trade_routes", "Spend 5 trade goods.", true},
    {"sway_council", "Spend 8 influence.", true},
    // Stage I (PoK)
    {"amass_wealth", "Spend 3 influence, 3 resources, and 3 trade goods.", true},
    {"build_defenses", "Have 4 or more structures.", true},
    {"lost_outposts", "Control 2 planets that have attachments.", true},
    {"engineer_marvel", "Have your flagship or a war sun on the game board.", true},
    {"deep_space", "Have units in 3 systems that do not contain planets.", true},
    {"infrastructure", "Have structures on 3 planets outside of your home system.", true},
    {"make_history", "Have units in 2 systems that contain legendary planets, Mecatol Rex, or anomalies.", true},
    {"outer_rim", "Have units in 3 systems on the edge of the game board other than your home system.", true},
    {"push_boundaries", "Control more planets than each of 2 of your neighbors.", true},
    {"raise_fleet", "Have 5 or more non-fighter ships in 1 system.", true},
    // Stage II (base)
    {"centralize_trade", "Spend 10 trade goods.", true},
    {"conquer", "Control 1 planet that is in another player's home system.", true},
    {"brain_trust", "Control 5 planets that have technology specialties.", true},
    {"golden_age", "Spend 16 resources.", true},
    {"galvanize", "Spend a total of 6 tokens from your tactic and/or strategy pools.", true},
    {"manipulate_law", "Spend 16 influence.", true},
    {"master_science", "Own 2 technologies in each of 4 colors.", true},
    {"revolutionize", "Own 3 unit upgrade technologies.", true},
    {"subdue", "Control 11 planets in non-home systems.", true},
    {"unify_colonies", "Control 6 planets that each have the same planet trait.", true},
    // Stage II (PoK)
    {"supremacy", "Have your flagship or war sun in another player's home system or the Mecatol Rex system.", true},
    {"become_legend", "Have units in 4 systems that contain legendary planets, Mecatol Rex, or anomalies.", true},
    {"command_armada", "Have 8 or more non-fighter ships in 1 system.", true},
    {"massive_cities", "Have 7 or more structures.", true},
    {"control_borderlands", "Have units in 5 systems on the edge of the game board other than your home system.", true},
    {"vast_reserves", "Spend 6 influence, 6 resources, and 6 trade goods.", true},
    {"vast_territories", "Have units in 5 systems that do not contain planets.", true},
    {"protect_border", "Have structures on 5 planets outside of your home system.", true},
    {"ancient_monuments", "Control 3 planets that have attachments.", true},
    {"distant_lands", "Control 2 planets that are each in or adjacent to a different, other player's home system.", true},
    // Secret (base) - (0.61.0: the cards' wording, AsyncTI4 secret_objectives.json; Make an Example of Their World, Turn Their Fleets
    // to Dust and Fight with Precision as first printed - the Codex wording is in objectiveSummary)
    {"ans", "Own 2 faction technologies. \"Valefar Assimilator\" technologies do not count toward this objective.", true},
    {"btgk", "Have 1 or more ships in a system that contains an alpha wormhole and 1 or more ships in a system that contains a beta wormhole.", true},
    {"ctr", "Have 1 or more ships in 6 systems.", true},
    {"csl", "Have 1 or more ships in the same system as another player's space dock.", true},
    {"dtgs", "Destroy another player's war sun or flagship.", true},
    {"eap", "Have 4 PDS units on the game board.", true},
    {"faa", "Control 4 cultural planets.", true},
    {"fsn", "Discard 5 action cards.", true},
    {"fwm", "Have 3 space docks on the game board.", true},
    {"gamf", "Have 5 dreadnoughts on the game board.", true},
    {"lsc", "Have 1 or more ships in 3 systems that are each adjacent to an anomaly.", true},
    {"mew", "Use BOMBARDMENT to destroy the last of a player's ground forces on a planet.", true},
    {"mlp", "Own 4 technologies of the same color.", true},
    {"mrm", "Control 4 hazardous planets.", true},
    {"mp", "Control 4 industrial planets.", true},
    {"ose", "Control Mecatol Rex and have 3 or more ships in its system.", true},
    {"sar", "Win a combat against a player who has the most victory points.", true},
    {"te", "Have 1 or more ships in a system that is adjacent to another player's home system.", true},
    {"ttfd", "Use SPACE CANNON to destroy the last of a player's ships in a system.", true},
    {"uf", "Win a space combat in a system that contains your flagship. You cannot score this objective if your flagship is destroyed in the combat.", true},
    // Secret (PoK)
    {"bam", "Lose control of a planet in a home system.", true},
    {"baf", "Win a combat against a player whose promissory note you had in your play area at the start of your tactical action.", true},
    {"btv", "Win a combat in an anomaly.", true},
    {"dts", "Win a combat in another player's home system.", true},
    {"dfat", "Have units in the wormhole nexus.", true},
    {"dyp", "Have 3 or more non-fighter ships in the active system at the end of a space combat.", true},
    {"dhw", "Purge 2 of your relic fragments of any type.", true},
    {"dp", "There are 3 or more laws in play.", true},
    {"dtd", "You or a planet you control are elected by an agenda.", true},
    {"eh", "Control planets that have a combined influence value of at least 12.", true},
    {"fwp", "Use ANTI-FIGHTER BARRAGE to destroy the last of a player's fighters in a system.", true},
    {"fc", "Be neighbors with all other players.", true},
    {"hrm", "Control planets that have a combined resource value of at least 12.", true},
    {"mtm", "Have 1 mech on each of 4 planets.", true},
    {"otf", "Have 9 or more ground forces on a planet that does not contain 1 of your space docks.", true},
    {"pem", "Have units with a combined PRODUCTION value of at least 8 in a single system.", true},
    {"pe", "Be the last player to pass during a game round.", true},
    {"sai", "Control a legendary planet.", true},
    {"syc", "Control a planet in a system that contains a planet controlled by another player.", true},
    {"sb", "Have another player's promissory note in your play area.", true},
    // (0.82.0) Monuments+ - the cards' own wording (the owner's v3.1 card files)
    {"dagw", "Have a monument on the game board and at least 3 ground forces on its planet or in its space area.", true},
    {"tam", "Destroy another player's monument during a tactical action.", true},
};
}  // namespace

const char* objectiveSummary(int o) {
    if (o < 0 || o >= kObjectiveCount) return "";
    if (g_codexTexts) {  // the Omega versions (0.43.3): whatever destroys them during that step counts
        if (!std::strcmp(kObjectives[o].key, "mew")) return "Destroy the last of a player's ground forces on a planet during the bombardment step.";
        if (!std::strcmp(kObjectives[o].key, "ttfd"))
            return "Destroy the last of a player's non-fighter ships in the active system during the space cannon offense step.";
        if (!std::strcmp(kObjectives[o].key, "fwp"))
            return "Destroy the last of a player's fighters in the active system during the anti-fighter barrage step.";
    }
    for (const auto& k : kObjText)
        if (!std::strcmp(k.key, kObjectives[o].key)) return k.text;
    return "";
}

bool objectiveImplemented(int o) {
    if (o < 0 || o >= kObjectiveCount) return false;
    for (const auto& k : kObjText)
        if (!std::strcmp(k.key, kObjectives[o].key)) return k.done;
    return false;
}

// ---------------------------------------------------------------------------
const char* strategyName(int c) {
    static const char* n[] = {"Leadership", "Diplomacy", "Politics", "Construction",
                              "Trade",      "Warfare",   "Technology", "Imperial"};
    return (c >= 0 && c < kStrategyCards) ? n[c] : "?";
}

// 0.43.3 (tester): with Thunder's Edge, its revised Construction and Warfare (set when a game starts or opens)
bool g_teStrategy = false;
bool g_monStrategy = false;  // (0.82.0) Monuments+'s Construction card
void setStrategyTexts(bool thundersEdge, bool monuments) {
    g_teStrategy = thundersEdge;
    g_monStrategy = monuments;
}

// (0.61.0, the strategy card pass: the cards' wording - AsyncTI4 strategy_cards/pok.json and te.json)
const char* strategyPrimary(int c) {
    // (0.82.0) Monuments+'s Construction replaces the others (owner 2026-11-21) - the card's wording
    if (g_monStrategy && c == 3)
        return "Either place 1 structure on a planet you control or use the PRODUCTION ability of 1 of your space docks. Place 1 "
               "structure on a planet you control. If your monument is not on the board, you may place it.";
    if (g_teStrategy && c == 3)
        return "Either place 1 structure on a planet you control, or use the PRODUCTION ability of 1 of your space docks. Place 1 "
               "structure on a planet you control.";
    if (g_teStrategy && c == 5)
        return "Perform a tactical action in any system without placing a command token, even if the system already has your command "
               "token in it: that system still counts as being activated. You may redistribute your command tokens before and after "
               "this action.";
    static const char* t[] = {
        "Gain 3 command tokens. Spend any amount of influence to gain 1 command token for every 3 influence spent.",
        "Choose 1 system other than the Mecatol Rex system that contains a planet you control; each other player places a command "
        "token from their reinforcements in the chosen system. Then, ready up to 2 exhausted planets you control.",
        "Choose a player other than the speaker. That player gains the speaker token. Draw 2 action cards. Look at the top 2 cards of "
        "the agenda deck. Place each card on the top or bottom of the deck in any order.",
        "Place 1 PDS or 1 space dock on a planet you control. Place 1 PDS on a planet you control.",
        "Gain 3 trade goods. Replenish commodities. Choose any number of other players. Those players use the secondary ability of "
        "this strategy card without spending a command token.",
        "Remove 1 of your command tokens from the game board; then, gain 1 command token. Redistribute any number of the command "
        "tokens on your command sheet.",
        "Research 1 technology. Spend 6 resources to research 1 technology.",
        "Immediately score 1 public objective if you fulfill its requirements. Gain 1 victory point if you control Mecatol Rex; "
        "otherwise, draw 1 secret objective.",
    };
    return (c >= 0 && c < kStrategyCards) ? t[c] : "";
}

const char* strategySecondary(int c) {
    if (g_monStrategy && c == 3) return "Spend 1 token from your strategy pool to place 1 non-monument structure on a planet you control.";
    if (g_teStrategy && c == 3) return "Spend 1 token from your strategy pool to place 1 structure on a planet you control.";
    if (g_teStrategy && c == 5)
        return "Spend 1 token from your strategy pool to use the PRODUCTION abilities of the units in your home system (this token is "
               "not placed in your home system).";
    static const char* t[] = {
        "Spend any amount of influence to gain 1 command token for every 3 influence spent.",
        "Spend 1 token from your strategy pool to ready up to 2 exhausted planets you control.",
        "Spend 1 token from your strategy pool to draw 2 action cards.",
        "Spend 1 token from your strategy pool and place it in any system; you may place either 1 space dock or 1 PDS on a planet you "
        "control in that system.",
        "Spend 1 token from your strategy pool to replenish your commodities.",
        "Spend 1 token from your strategy pool to use the PRODUCTION ability of 1 of your space docks in your home system (this token "
        "is not placed in your home system).",
        "Spend 1 token from your strategy pool and 4 resources to research 1 technology.",
        "Spend 1 token from your strategy pool to draw 1 secret objective.",
    };
    return (c >= 0 && c < kStrategyCards) ? t[c] : "";
}

// Thunder's Edge galactic events (0.36.0), in our own words; done = playable (ticked on the Modules screen)
bool g_monumentsTexts = false;
void setMonumentsTexts(bool on) { g_monumentsTexts = on; }
bool monumentsTexts() { return g_monumentsTexts; }

const GalacticEventDef& galacticEvent(int ev) {
    // (0.82.0) Monuments+'s Monuments to the Ages Omega - the card's own wording (the owner's v3.1 card file): "monoliths", and
    // the points go to the player that controls its planet (as the game already scored them)
    static const GalacticEventDef kOmega{
        "monuments", "Monuments to the Ages \xCE\xA9",
        "When you would place a structure on a non-home planet, you may spend 5 trade goods to place a neutral space dock instead; this "
        "is a monolith, and is not considered to be a unit of any type. There can be only 3 monoliths in play at once. At the start of "
        "the status phase, place 1 commodity beneath each monolith; each monolith is worth 1 victory point to the player that controls "
        "its planet for every 3 commodities beneath it. When a player gains control of a planet that contains a monolith, they may "
        "destroy that monolith.",
        true};
    if (ev == kEvMonuments && g_monumentsTexts) return kOmega;
    // (0.61.0, the galactic event pass: the cards' wording - AsyncTI4 galactic_events/te_galactic_events.json; their bullets as
    // sentences)
    static const GalacticEventDef k[kEventCount] = {
        {"commerce", "Age of Commerce",
         "Players do not have to be neighbors to perform transactions with each other. Players do not have a maximum number of "
         "commodities; when they refresh commodities, they gain a number of commodities equal to their commodity value instead. Players "
         "can share non-faction technology with other players as part of a transaction. When sharing tech in this way, the receiving "
         "player gains that technology from their own deck; the sharing player does not lose the technology.", true},
        {"void", "Call of the Void",
         "After you move 1 or more units into the active system, if that system is in The Fracture, gain 1 command token. When you "
         "activate a system in The Fracture, apply +1 to the move values of each of your ships.", true},
        {"wilds", "Dangerous Wilds",
         "During setup, place neutral infantry on each hazardous planet equal to that planet's resource value. At the end of each round, "
         "for each hazardous planet that is not controlled, replenish any neutral units that were destroyed during the game round. When a "
         "player gains control of a hazardous planet from the deck, they may research 1 technology; they may ignore a number of "
         "prerequisites equal to that planet's resource value.", true},
        {"hidden", "Hidden Agenda",
         "During the agenda phase, only the speaker can talk; all other players must remain silent except when declaring action cards / "
         "abilities. Transactions cannot be performed during this phase. When voting, players secretly and simultaneously choose their "
         "outcome and number of votes; after all players have voted, only the totals are revealed. The Argent Flight's votes are public "
         "and are known before the other players vote.", true},
        {"exploration", "Age of Exploration",
         "Relics require only 2 matching fragments be purged instead of 3. The Naaz-Rokha Alliance's Fabrication faction ability and "
         "Black Market Forgery promissory note do not require purged fragments to match. All players can perform the following action - "
         "ACTION: Exhaust Dark Energy Tap and choose a non-home edge system that contains your ships to roll 1 die. On a result of 1-4, "
         "draw a random unused red tile; on a result of 5-10, draw a random unused blue tile. Place that tile adjacent to the chosen "
         "system so that it is touching at least 2 non-home systems. Place a frontier token in the system if it does not contain any "
         "planets.", true},
        {"civilized", "Civilized Society",
         "During setup, turn all public objectives face up. There is no limit on the number of public objectives a player may score "
         "during the status phase. The game does not immediately end when a player reaches the required number of victory points; "
         "instead, it ends at the end of that round's status phase, and the player with the most victory points wins. In the case of a "
         "tie, the tied players total the influence values of their controlled planets and their unspent trade goods; the player or "
         "players with the highest total win the game.", true},
        {"minor", "Minor Factions",
         "During setup, players are dealt 1 fewer blue tile. The home systems of factions not being played are placed in the second "
         "ring, equidistant from players' home systems, each with 3 neutral infantry on its planets, split as evenly as possible. These "
         "are minor faction systems and do not count as home systems. When a player controls each planet in a minor faction system, they "
         "take that faction's alliance card from the deck or from the player that owned it previously. Planets in minor faction systems "
         "gain all three planet traits (cultural, industrial, hazardous).", true},
        {"atomics", "Stellar Atomics",
         "During setup, each player places one of their control markers on this card. All players can perform the following action - "
         "ACTION: Discard your control marker from this card to destroy all ground forces and structures on any non-home planet. If you "
         "do not have a control marker on this card, you cannot vote or play action cards during the agenda phase.", true},
        {"fighters", "Age of Fighters",
         "During setup, all players gain the Fighter II unit upgrade technology; the Naalu Collective player gains Hybrid Crystal "
         "Fighter II technology instead. All fighters that are counting against fleet pool gain CAPACITY 1 that cannot be used to "
         "support fighters. Non-fighter ships are purged when they are destroyed.", true},
        {"totalwar", "Total War",
         "When a player destroys another player's units, they place commodities from the supply equal to the combined cost of those "
         "units on a planet they control in their home system (infantry and fighters are worth 1 each). When a player gains control of "
         "a planet that has commodities on it, they may move those commodities to a planet they control in their home system. All "
         "players can perform the following action - ACTION: Discard 10 commodities from planets in your home system to gain 1 "
         "victory point.", true},
        {"wild", "Wild, Wild Galaxy",
         "Action cards are adjusted as follows: Direct Hit can be used against mechs; Flank Speed applies +2 move instead of +1; "
         "Maneuvering Jets cancels all SPACE CANNON hits; Morale Boost applies +2 to die rolls instead of +1; Sabotage also takes the "
         "canceled action card; Shields Holding can be used in ground combat; Skilled Retreat does not place a command token; War "
         "Machine reduces cost by 5 instead of 1; Diplomatic Pressure: the player must give 3 notes. Additionally, Stellar Converter "
         "and Nova Seed can be used against any planets and systems.", true},
        {"wormholes", "Weird Wormholes",
         "After a ship moves using at least 1 alpha, beta, or gamma wormhole, roll 1 die and consult the list Fighter - Destroyer - "
         "Cruiser - Dreadnought - Carrier - Flagship - War Sun (if researched). For each result of 1-5, replace that ship with the ship "
         "before it from your reinforcements, if able; for each result of 6-10, with the ship after it. If there are no ships of that "
         "type remaining in your reinforcements, skip that type and replace it with the next available type.", true},
        {"monuments", "Monuments to the Ages",
         "When you would place a structure on a non-home planet, you may spend 5 trade goods to place a neutral space dock instead; "
         "this is a monument, and is not considered to be a unit of any type. There can only be 3 monuments in play at once. At the "
         "start of the status phase, place 1 commodity beneath each monument. A monument is worth 1 victory point to the player who "
         "controls it for every 3 commodities placed beneath it. When a player gains control of a planet that contains a monument, they "
         "may destroy it.", true},
        {"cosmic", "Cosmic Phenomenae",
         "Nebulae: defending ships in nebulae apply an additional +3 to their combat rolls instead of +1. Asteroid fields: Fighter I's "
         "do not participate in space combat in asteroid fields. Supernovas: units with PRODUCTION in or adjacent to supernovas have "
         "their PRODUCTION values increased by 1. Gravity rifts: you may apply an additional +1 to the MOVE value of any of your ships "
         "moving out of gravity rifts; if you do, those ships are destroyed on a roll of 5 or lower. Entropic scars: systems that "
         "contain entropic scars are adjacent to each other.", true},
        {"cultural", "Cultural Exchange Program",
         "At the end of setup, shuffle the reference cards that correspond to each faction in play. Each player draws 1 of those cards "
         "and takes all leaders that correspond to that faction. They belong to that player for the remainder of the game. Then, each "
         "player unlocks their commander. The Obsidian / Firmament player does not participate; instead, they begin the game with \"The "
         "Obsidian\" relic.", false},
        {"mercenaries", "Mercenaries for Hire",
         "At the start of the game, shuffle the alliance cards that correspond to the factions not in play and place them in the common "
         "play area; this is the Mercenary deck. All players can perform the following action - ACTION: Spend 3 trade goods to gain the "
         "top card of the mercenary deck and place it in your play area. Players can use the abilities of the mercenaries in their play "
         "area.", true},
        {"mobilization", "Rapid Mobilization",
         "After setup, put The Fracture into play. Then, each player simultaneously resolves the following effects in order: place 1 "
         "infantry onto each planet adjacent to your home system (or the Creuss Gate / the Sorrow) - do not explore those planets - "
         "then ready each of those planets; place 1 space dock on any planet you control and place your flagship and 3 fighters in its "
         "system; gain your breakthrough; research 1 technology (the Nomad player researches 1 additional technology).", true},
        {"orthodoxy", "Zealous Orthodoxy",
         "The first player to score 2 secret objectives gains 1 victory point. Then, place that faction's alliance card on this card; "
         "all players gain that ability. That faction's alliance promissory note is then purged.", true},
        {"warsun", "Advent of the War Sun",
         "At the end of setup, all players other than the Embers of Muaat player gain the War Sun unit upgrade technology. The Embers "
         "of Muaat player purges their faction specific promissory note, unlocks their commander, and places 1 additional War Sun in "
         "their home system.", true},
        {"conventions", "Conventions of War Abandoned",
         "Each hit produced by BOMBARDMENT rolls destroys 3 units instead of 1. X-89 Bacterial Weapon technology gains the following "
         "ACTION: Exhaust to choose 1 planet in a system that contains 1 of your units that has bombardment; purge its planet card and "
         "all attachments or legendary planet ability cards associated with it. You are eliminated if all your home system's planet "
         "cards are purged.", true},
    };
    static const GalacticEventDef none{"", "", "", false};
    return ev >= 0 && ev < kEventCount ? k[ev] : none;
}

// ---------------------------------------------------------------------------
// 0.82.0 Monuments+
const int kMonumentCount = int(sizeof(kMonuments) / sizeof(kMonuments[0]));
static_assert(sizeof(kMonuments) / sizeof(kMonuments[0]) <= size_t(kMonumentMax), "kMonumentMax (data.h) is too small");

int monumentForUnit(int unit) {
    if (unit < 0 || unit >= kUnitCount || kUnits[unit].type != UnitType::Monument) return -1;
    for (int i = 0; i < kMonumentCount; ++i)
        if (!std::strcmp(kMonuments[i].unit, kUnits[unit].key)) return i;
    return -1;
}

int monumentForFaction(int faction) {
    if (faction < 0 || faction >= kFactionCount) return -1;
    for (int i = 0; i < kMonumentCount; ++i)
        if (!std::strcmp(kMonuments[i].faction, kFactions[faction].key)) return i;
    return -1;
}

}  // namespace ti
