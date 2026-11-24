#!/usr/bin/env python3
"""Monuments+ (0.82.0): the monument card table -> src/engine/gen/monuments.inc

Wording: exactly as printed on the Monuments+ v3.1 cards (owner 2026-11-21: the reference cards win over AsyncTI4; checked
card by card in docs/owner/monuments/wording_check_2026-11-22.txt). '\\n' starts a new paragraph as on the card.
Placement: AsyncTI4 data/units/monuments.json eligible / ineligible types (they agree with the cards' icons).
Colours: the card's frame and subtitle, sampled from the card pictures.
Stats (PRODUCTION, SPACE CANNON, PLANETARY SHIELD, ...) are the unit's numbers - tools/gen_data.py puts them in kUnits.

Only the monuments listed here exist in games; the rest of AsyncTI4's monuments come with their sets (0.82.2-0.82.4
Discordant Stars; Blue Reverie and Twilight's Fall when those are built). 0.82.1: Thunder's Edge's 6, after the 25 (their
indices never move).

Usage: python3 tools/gen_monuments.py [ASYNC_RESOURCES]
"""
import json
import os
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/ext/async/src/main/resources")
OUT = "src/engine/gen/monuments.inc"

# art key (tools/gen_monument_anim.py), our faction key, AsyncTI4 faction key, card text, flavour text, set
CARDS = [
    ("arborec", "arborec", "arborec",
     "When you pass, place 1 infantry on a planet in or adjacent to this system other than Mecatol Rex. If that planet contains "
     "another player's units, place that unit into coexistence.", "", "PoK"),
    ("argent", "argent", "argent",
     "ACTION: Exhaust this card to choose this or an adjacent system that does not contain 1 or more of your ships.\n"
     "Produce a hit against each ship in that system that has SUSTAIN DAMAGE.", "", "PoK"),
    ("creuss", "ghost", "ghost",
     "After a player activates this system:\n"
     "You may produce 1 hit against players' ships in each system adjacent to this planet that contains a wormhole.",
     "A kaleidoscope of dimensional fire.", "Base"),
    ("empyrean", "empyrean", "empyrean",
     "This unit is placed in the space area of any non-Fracture, non-supernova system that does not contain planets, instead of "
     "on a planet.\nIf this unit is blockaded, destroy it.", "", "PoK"),
    ("hacan", "hacan", "hacan",
     "Outside of combat, players may trade their units in the space area of this and adjacent systems as part of a transaction. "
     "A player who gained units this way then replaces them and moves them to a system that contains their ships and 1 of their "
     "command tokens, if able; otherwise, remove those units.", "", "Base"),
    ("jolnar", "jolnar", "jolnar",
     "After an agenda outcome you voted for or predicted is resolved:\nPlace up to 2 infantry on a planet you control in this "
     "system.", "The sussurus of the falls calms even the most traumatized troops.", "Base"),
    ("l1z1x", "l1z1x", "l1z1x",
     "At the start of your turn, you may exhaust this card to remove a player's command token from an adjacent system and place "
     "it in this system.", "This critical L1 node filters and redirects network traffic.", "Base"),
    ("letnev", "letnev", "letnev",
     "Double the PRODUCTION values of your units in this system.\nWhen you exhaust this planet to cast votes, you may cast an "
     "additional number of votes equal to the number of your non-fighter ships in this system.", "", "Base"),
    ("mahact", "mahact", "mahact",
     "At the start of the status phase, the Mahact player may place 1 command token in this system into their fleet pool if it "
     "is their own token, or if it does not already have 1 of that player's tokens.", "Visitors report only joy here.", "PoK"),
    ("mentak", "mentak", "mentak",
     "At the start of a player's turn, exhaust this card to have each ship in this or an adjacent system with SUSTAIN DAMAGE "
     "become damaged.", "Whether Erwan's Fist is a weapon or team of infiltrators is unknown.", "Base"),
    ("muaat", "muaat", "muaat",
     "When you exhaust this planet to cast votes, for each war sun or flagship you control in this system, and for each "
     "supernova adjacent to this system, choose: cast 3 additional votes, or produce a hit against another player's ships in "
     "this or an adjacent system.", "", "Base"),
    ("naaz", "naaz", "naaz",
     "When you are negotiating a transaction with a player who controls units in or adjacent to this system, relics can be "
     "exchanged as part of that transaction; ignore 'when you gain' effects of those relics.",
     "This vast hall of art and relics hides a more practical purpose.", "PoK"),
    ("sardakk", "sardakk", "sardakk",
     "When you place your monument, produce up to a total of 3 infantry or fighters in this or adjacent systems without "
     "spending resources.\nAt the end of your turn, you may replace this unit with a different structure from your "
     "reinforcements.", "", "Base"),
    ("naalu", "naalu", "naalu",
     "Infantry that you commit to planets in this and adjacent systems may be committed into coexistence, if able.",
     "Reports from nearby systems confirm that all citizens are calm.", "Base"),
    ("nekro", "nekro", "nekro",
     "When you destroy another player's structure, you may place or move an \"M\" or \"Z\" assimilator token onto that player's "
     "monument card.\nWhile any of those tokens are on a monument card, this card gains that card's attributes, abilities, and "
     "ability text.", "", "Base"),
    ("nomad", "nomad", "nomad",
     "At the start of your turn:\nYou may return this unit to your reinforcements to produce, or remove and produce, your "
     "flagship in this system.", "Reports of strange visions at this ornate shrine have been dismissed.", "PoK"),
    ("saar", "saar", "saar",
     "You may treat this system as being adjacent to 1 of your space docks.", "A monument to the Call, glowing in the dark.",
     "Base"),
    ("sol", "sol", "sol",
     "When you place 1 or more infantry on a planet, exhaust this card to place any of those infantry on this planet instead.",
     "The Cenotaph inevitably provides motivation for tired Sol troops.", "Base"),
    ("titans", "titans", "titans",
     "ACTION: Exhaust this card to place a sleeper token on a planet in this or an adjacent system that has a planetary trait.",
     "The towering scepter is both tool and rallying cry.", "PoK"),
    ("cabal", "cabal", "cabal",
     "DEPLOY: When you gain control of a planet that contains 1 or more structures, you may place this unit on that planet and "
     "capture each other structure on that planet as if it were a non-fighter ship.", "", "PoK"),
    ("winnu", "winnu", "winnu",
     "At the start of the agenda phase, if your monument is on the game board, gain any number of trade goods on this card. "
     "Otherwise, spend any number of resources to place 1 trade good from the supply on this card for every 3 resources spent.",
     "", "Base"),
    ("xxcha", "xxcha", "xxcha",
     "When an agenda outcome you voted for or predicted is resolved, you may exhaust this card to place a structure on a planet "
     "you control in this system.", "The array is an Xxcha infrastructure touchstone, and well defended.", "Base"),
    ("yin", "yin", "yin",
     "ACTION: Exhaust this card. Then, for every 2 infantry on this planet, move 1 of those infantry to any other planet you "
     "control.", "The sanctum is a site of pilgrimage that focuses Yin fervor.", "Base"),
    ("yssaril", "yssaril", "yssaril",
     "When another player plays an action card:\nIf that player controls 1 or more units in this or an adjacent system, you "
     "may gain 2 trade goods.", "The Yssaril keep a secret name for this place.", "Base"),
    ("keleres", "keleres", "keleresm",
     "After a law enters play, you may add a command token from your reinforcements to this card.\nWhen you would place a "
     "command token in this or an adjacent system, you may place that token from this card instead.", "", "Keleres"),
    # 0.82.1: Thunder's Edge (the owner's v3.1 card files, Monuments/TE)
    ("bastion", "bastion", "bastion",
     "This unit is a space station and placed in the space area of a system that contains your ships and exactly 1 "
     "non-legendary planet. Take its matching space station card while you control it.\n"
     "When you lose control of this station, destroy it and produce 2 hits against ships in this system.", "", "TE"),
    ("crimson", "crimson", "crimson",
     "After a breach becomes or is placed active side up in any system, if that system is adjacent to this planet, you may "
     "produce 1 hit in that system against another player's ships.", "More mausoleum than monument.", "TE"),
    ("deepwrought", "deepwrought", "deepwrought",
     "Other players' ships in this system do not trigger space combat. After this unit is removed from a planet, choose a "
     "player with ships in that system to resolve a space combat. Repeat this until it contains no more than 1 player's "
     "ships.", "", "TE"),
    ("firmament", "firmament", "firmament",
     "When a player activates this system, place their control token on this card if it does not have one. When you "
     "research a technology, you may remove a control token from this card to skip 1 prerequisite, or move a "
     "non-Firmament player's control token to a plot card.", "", "TE"),
    ("obsidian", "obsidian", "obsidian",
     "After a puppeted player activates this system, replace up to 2 of that player's infantry on a planet they control "
     "with your infantry; those units coexist.", "An unknown, twisted connection exists between visitors of this place.", "TE"),
    ("ralnel", "ralnel", "ralnel",
     "At the end of your tactical action, you may move 1 structure in the space area of this or an adjacent system to the "
     "space area of a system adjacent to that structure.", "The most practical of edifices.", "TE"),
    # 0.82.2: Discordant Stars, batch 1 (the owner's v3.1 card files, Monuments/DS; set "DS1" = fixes 40)
    ("axis", "axis", "axis",
     "When you produce 1 or more non-fighter ships in this system, gain 1 trade good.",
     "The Anvil's light, a beacon to the Axis; its hollow rumble, a summons.", "DS1"),
    ("khrask", "khrask", "khrask",
     "During ground combat in this system, this unit is treated as a structure and a ground force.", "", "DS1"),
    ("cymiae", "cymiae", "cymiae",
     "When another player activates this system they must discard 1 action card, if able.",
     "In the palace, the Voices are amplified, and Jatta's pain expands.", "DS1"),
    ("lizho", "lizho", "lizho",
     "When a player activates this system, remove a different player's command token from this system, or gain 1 command "
     "token.", "Nightfall Fortress is an endless maze of traps: physical and moral.", "DS1"),
    ("olradin", "olradin", "olradin",
     "After you resolve the primary or secondary ability of the \"Diplomacy\" strategy card, ready this planet.",
     "The Olradin are masters of repurposing abandoned structures.", "DS1"),
    ("mirveda", "mirveda", "mirveda",
     "During the first round of ground combat in this system, your opponent's units do not make combat rolls.",
     "The Citadel serves a purely strategic purpose for the Mirveda.", "DS1"),
    ("veldyr", "veldyr", "veldyr",
     "During movement, hits produced by the SPACE CANNON abilities of your units in this system cannot be canceled.",
     "The Veldyr have provided a litany of well-written reports detailing the array's cultural, artistic, and scientific "
     "importance.", "DS1"),
    ("nokar", "nokar", "nokar",
     "Your ships may move out of this system, and transport units, even if it contains 1 of your command tokens.",
     "Freehold by name, though nothing is truly free among the Nokar.", "DS1"),
    ("edyn", "edyn", "edyn",
     "At any time, you may exhaust this card to produce a mech on this planet without spending resources.",
     "The throne is raised only when the Edyn feel the time is right.", "DS1"),
    ("rohdhna", "rohdhna", "rohdhna",
     "When you produce any number of units in this system, reduce the combined cost of those units by 2.",
     "It stands in contrast to the Axis Anvil, a challenge to competitors.", "DS1"),
    ("kortali", "kortali", "kortali",
     "When a non-fighter, non-infantry unit you control is destroyed in this system, roll a die. If you roll equal to or "
     "higher than that unit's combat value, or 8 if that unit has no combat value, produce 1 hit against another player's "
     "ships in this system.", "", "DS1"),
    ("ghemina", "ghemina", "ghemina",
     "You may treat this system as adjacent to systems that contain 2 of your structures of the same unit type.", "", "DS1"),
    # 0.82.3: Discordant Stars, batch 2 (set "DS2" = fixes 41). Keys = the pictures' (myko, augurs, fsc: the card files' names)
    ("myko", "mykomentori", "mykomentori",
     "Up to 3 fighters in this and each adjacent system do not count against your ships' capacity.\n"
     "After a player uses an omen die, you may produce 1 fighter in this system without spending resources.", "", "DS2"),
    ("dihmohn", "dihmohn", "dihmohn",
     "After you activate a system, you may exhaust this card to apply +1 to the move value of ships in this system until the "
     "end of this tactical action.", "The Cyclotron is the glowing apex of Flotilla engineering.", "DS2"),
    ("vaden", "vaden", "vaden",
     "After another player with ships in this or an adjacent system replenishes commodities, if that player has control tokens "
     "on the Vaden player's faction sheet, you may take one of their commodities.", "", "DS2"),
    ("florzen", "florzen", "florzen",
     "When you produce fighters, you may place that many plastic fighter units on this card, instead.\n"
     "At the start of your turn, you may produce fighter units on this card in this system without spending resources.", "", "DS2"),
    ("mortheus", "mortheus", "mortheus",
     "ACTION: Exhaust this card and choose an adjacent system that contains another player's non-fighter ships. That player "
     "moves those ships into this system if able, until they have moved at least 4 resources worth of ships, or all ships. "
     "Then, resolve a space combat in this system.", "", "DS2"),
    ("nivyn", "nivyn", "nivyn",
     "Other players' ships cannot move out of or through this system.\n"
     "Your ships may treat this system as adjacent to the Wound.", "The Nivyn do not know how the Maw is bound to the Wound.", "DS2"),
    ("augurs", "augers", "augers",
     "When you would explore another planet, you may explore this planet instead.\n"
     "If you do, you may gain trade goods equal to the number of your scored secret objectives.",
     "The technological edge provided by the planet was ... clarifying.", "DS2"),
    ("tnelis", "tnelis", "tnelis",
     "When you lose a ground combat on this planet, you may destroy each unit on this planet.\n"
     "At the start of your turn you may destroy 1 other player's coexisting unit in this system.",
     "It abandons principle for practicality.", "DS2"),
    ("vaylerian", "vaylerian", "vaylerian",
     "When a player spends trade goods during a space combat in or adjacent to this system, you may exhaust this card to draw 1 "
     "action card.\nWhile another player has ships in or adjacent to this system, your commodity value is increased by 1.", "", "DS2"),
    ("fsc", "freesystems", "freesystems",
     "After you resolve the primary or secondary ability of the \"Leadership\" strategy card, each other player with units in or "
     "adjacent to this system who reveals 1 of your promissory notes from their hand gains 1 command token.", "", "DS2"),
    ("celdauri", "celdauri", "celdauri",
     "During the \"Commit Ground Forces\" step:\nYou can commit ground forces on this planet to planets that contain your space "
     "docks, if this system does not contain 1 of your command tokens.", "An accident, turned into an asset.", "DS2"),
    ("zelian", "zelian", "zelian",
     "This system is treated as an asteroid field. Units can move into this system as if it were a non-anomaly tile.\n"
     "Place the Zelian asteroid field token in this system as a reminder.", "\"I LAUGH AT THEIR WEAPONS!\"", "DS2"),
    # 0.82.4: Discordant Stars, batch 3 (set "DS3" = fixes 42). The Rhodun's card has two sides: two entries for one faction
    # (rhodun_front first - the faction's card; the AsyncTI4 key is the unit's id for these two)
    ("kollecc", "kollecc", "kollecc",
     "After movement, if this is the active system and contains no more than 1 other player's ships, you may place any of your "
     "captured units from your faction sheet into this system.", "The Kollecc secret their den on prominent worlds, almost as a dare.",
     "DS3"),
    ("bentor", "bentor", "bentor",
     "DEPLOY: After another player explores a planet that is not adjacent to a home system, you may place this unit into "
     "coexistence on that planet and explore it.", "", "DS3"),
    ("kjalengard", "kjalengard", "kjalengard",
     "Once per action, when a player wins a combat in or adjacent to this system, you and that player may each gain 1 command "
     "token.", "This is the most renowned of all Kjalengard bannerhalls.", "DS3"),
    ("lanefir", "lanefir", "lanefir",
     "Once per action, you may use the technology specialty of a planet in or adjacent to this system, even if it is exhausted.",
     "The Library permanently displays a reminder of Lanefir loss.", "DS3"),
    ("cheiran", "cheiran", "cheiran",
     "This unit's PRODUCTION value is equal to the number of structures you control in or adjacent to this system.",
     "\"Rest well, my hungry children.\" \u2014Thakt Clqua", "DS3"),
    ("kyro", "kyro", "kyro",
     "This planet counts as a legendary planet with an attachment and a technology speciality of no type.\n"
     "When this unit is destroyed, you must place it on a planet you control in this or an adjacent system, if able. If you do, "
     "draw a relic.", "", "DS3"),
    ("ghoti", "ghoti", "ghoti",
     "During movement, you may choose to treat this system as though it contains no planets.\n"
     "You treat this system as though it contains a frontier token.", "What the anchorpoint represents is unknown, but the Ghoti "
     "revere it.", "DS3"),
    ("kolume", "kolume", "kolume",
     "When another player passes, choose this or an adjacent system that contains a unit with SPACE CANNON; either use this "
     "unit's SPACE CANNON against another player's ships in that system, or repeat this ability, starting in that system.", "",
     "DS3"),
    ("gledge", "gledge", "gledge",
     "At the start of your turn, you may exhaust 2 planets you control that contain a Core token; 1 player treats all laws as "
     "blank until the end of this turn.", "\u201cThe GLEdge Verdant Halo Institute [GVHI] abides by all Galactic Council statutes "
     "that comply with \u00a7736 b. Lazax oversight.\u201d", "DS3"),
    ("rhodun_front", "zealots", "rhodun_monument",
     "Start with this side face up; when a player activates this system, you may flip this card.\n"
     "During combat, apply +1 to each of your units' combat rolls in this and adjacent systems if your opponent has any unit "
     "upgrade technologies.", "", "DS3"),
    ("rhodun_back", "zealots", "rhodun_monumentback",
     "Flip this card at any time.\nOther players with units in The Fracture cannot activate this system.\n"
     "During combat, apply +2 to each of your units' combat rolls in this and adjacent systems if your opponent has any units in "
     "The Fracture.", "", "DS3"),
    # Blue Reverie (0.83.0) - each with its faction (owner: on standby until then); after the 66 so no index moves
    ("atokera", "atokera", "atokera",
     "During the agenda phase, when an outcome you voted for or predicted is resolved, produce up to 1 unit in this system.",
     "The Deepmantle's spike drives the Atokera into a planet's heart.", "BR"),
    # (0.84.0) the Belkosea (the card has no flavour line)
    ("belkosea", "belkosea", "belkosea",
     "At the end of the strategy phase, choose and place 1 control token on a Superweapon card in a play area. This card has the text "
     "abilities of each Superweapon card in a play area that has 1 of your control tokens.", "", "BR"),
    # (0.85.0) the Pharad'n - a unit's text (rules_br3.cpp pharLanded, query pyramidOn); no flavour line
    ("pharadn", "pharadn", "pharadn",
     "This unit is treated as a structure and a ground force.\n"
     "DEPLOY: During your \"Commit Ground Forces\" step, if you control a planet in the active system, place this unit on a planet in "
     "that system.", "", "BR"),
]

# frame, subtitle (sampled from the cards: the frame's median saturated colour, the subtitle text)
COLOURS = {
    'arborec': ((36, 52, 26), (115, 141, 95)), 'argent': ((135, 69, 35), (222, 168, 91)),
    'creuss': ((40, 122, 139), (79, 161, 198)), 'empyrean': ((94, 68, 106), (192, 76, 70)),
    'hacan': ((124, 116, 41), (205, 189, 62)), 'jolnar': ((113, 66, 118), (156, 95, 132)),
    'l1z1x': ((13, 87, 111), (176, 71, 55)), 'letnev': ((58, 62, 62), (199, 65, 63)),
    'mahact': ((104, 107, 40), (165, 101, 159)), 'mentak': ((115, 86, 47), (226, 171, 94)),
    'muaat': ((124, 52, 33), (225, 176, 96)), 'naaz': ((7, 126, 71), (166, 184, 93)),
    'sardakk': ((120, 29, 32), (170, 38, 38)), 'naalu': ((113, 78, 32), (151, 164, 81)),
    'nekro': ((121, 30, 33), (196, 41, 44)), 'nomad': ((3, 100, 118), (94, 143, 161)),
    'saar': ((119, 75, 75), (237, 222, 114)), 'sol': ((23, 83, 105), (220, 220, 220)),
    'titans': ((126, 40, 52), (144, 106, 88)), 'cabal': ((126, 43, 41), (153, 4, 27)),
    'winnu': ((111, 67, 113), (223, 228, 133)), 'xxcha': ((0, 117, 91), (95, 158, 139)),
    'yin': ((58, 62, 62), (230, 225, 127)), 'yssaril': ((23, 118, 104), (212, 201, 83)),
    'keleres': ((117, 50, 107), (169, 23, 138)),
    # (0.82.1, sampled the same way from the TE cards: the frame's saturated edge colour, the subtitle text)
    'bastion': ((121, 67, 19), (192, 128, 88)), 'crimson': ((154, 31, 48), (125, 201, 227)),
    'deepwrought': ((22, 104, 112), (183, 207, 81)), 'firmament': ((49, 40, 94), (179, 30, 61)),
    'obsidian': ((101, 31, 47), (178, 31, 62)), 'ralnel': ((29, 111, 66), (220, 223, 55)),
    # (0.82.2-0.82.4: the Discordant Stars cards, sampled the same way)
    'axis': ((125, 48, 46), (244, 235, 42)), 'khrask': ((95, 73, 50), (57, 218, 214)), 'cymiae': ((51, 92, 101), (192, 114, 116)),
    'lizho': ((119, 89, 39), (251, 187, 6)), 'olradin': ((9, 124, 141), (185, 58, 55)), 'mirveda': ((54, 88, 136), (126, 96, 217)),
    'veldyr': ((66, 71, 74), (67, 194, 241)), 'nokar': ((51, 105, 127), (54, 221, 240)), 'edyn': ((64, 61, 52), (251, 250, 131)),
    'rohdhna': ((64, 52, 127), (145, 145, 111)), 'kortali': ((93, 58, 125), (200, 200, 210)), 'ghemina': ((40, 91, 152), (110, 130, 155)),
    'myko': ((135, 50, 131), (209, 67, 216)), 'dihmohn': ((45, 158, 225), (189, 18, 19)), 'vaden': ((48, 121, 81), (221, 248, 108)),
    'florzen': ((51, 108, 105), (23, 238, 173)), 'mortheus': ((144, 57, 84), (233, 107, 131)), 'nivyn': ((59, 64, 66), (243, 247, 162)),
    'augurs': ((93, 54, 106), (246, 240, 143)), 'tnelis': ((115, 105, 0), (40, 203, 48)), 'vaylerian': ((7, 129, 123), (38, 235, 175)),
    'fsc': ((45, 115, 115), (30, 169, 168)), 'celdauri': ((123, 125, 49), (42, 110, 236)), 'zelian': ((148, 49, 43), (176, 52, 25)),
    'kollecc': ((108, 77, 54), (191, 67, 16)), 'bentor': ((127, 46, 139), (166, 16, 208)), 'kjalengard': ((156, 57, 45), (199, 41, 41)),
    'lanefir': ((43, 124, 64), (198, 75, 24)), 'cheiran': ((164, 65, 3), (21, 154, 166)), 'kyro': ((35, 122, 93), (160, 237, 44)),
    'ghoti': ((1, 132, 131), (18, 180, 215)), 'kolume': ((101, 96, 52), (235, 236, 237)), 'gledge': ((138, 85, 1), (251, 187, 7)),
    'rhodun_front': ((46, 113, 124), (39, 224, 217)), 'rhodun_back': ((59, 155, 171), (38, 224, 217)),
    # (0.83.0: the Blue Reverie cards, sampled the same way)
    'atokera': ((22, 104, 0), (182, 190, 198)), 'belkosea': ((2, 44, 86), (5, 120, 164)), 'pharadn': ((86, 76, 5), (255, 145, 4)),
}

# Thunder's Edge placements that AsyncTI4's types don't say (the cards' own icons / text):
#   Seraph Data Center - a space station in the space area of a system with your ships and exactly 1 non-legendary planet
#     (AsyncTI4 "EMPTY"; the card's icon is the space station's)
#   Epiphany Hollow - no placement at all (Monuments+ FAQ: "once Obsidian commits to the relic's whispers that monument is
#     no longer valid to place on any planet. Flip wisely.")
SPECIAL_PLACE = {"bastion": (["STATION"], [], "A space station: your ships there and exactly 1 "
                                              "non-legendary planet."),
                 "obsidian": ([], [], "Can't be placed: the Epiphany, decayed, stays where it stood.")}
# the cards' subtitles where AsyncTI4's differ
SUBTITLE_FIX = {"crimson": "Rebellion Monument", "pharadn": "Pharad'n Monument"}
# the cards' printed numbers the online data can't hold (0.82.4: the Polemarch's Hive - "PRODUCTION X", query hiveProduction)
STATS_FIX = {"cheiran": "PRODUCTION X"}

PLACE = {"CULTURAL": "kMonCultural", "HAZARDOUS": "kMonHazardous", "INDUSTRIAL": "kMonIndustrial", "LEGENDARY": "kMonLegendary",
         "TECH_SPECIALTY": "kMonTechSpecialty", "MECATOL_REX": "kMonMecatol", "EMPTY": "kMonEmpty", "HOME_PLANET": "kMonHome",
         "LIGHTNING": "kMonFracture", "SUPERNOVA": "kMonSupernova", "STATION": "kMonStation"}
WORD = {"CULTURAL": "cultural", "HAZARDOUS": "hazardous", "INDUSTRIAL": "industrial", "LEGENDARY": "legendary"}


def where(elig, inel):
    """the placement in words, shown under the picture (owner 2026-11-21 22:10: keep the words)"""
    if elig == ["EMPTY"]:
        return "Stands in the space area of a system with no planets" + (", not a supernova." if "SUPERNOVA" in inel else ".")
    if elig == ["MECATOL_REX"]:
        return "Stands on Mecatol Rex."
    if elig == ["TECH_SPECIALTY"]:
        return "Stands on a planet with a technology specialty."
    kinds = [WORD[e] for e in elig if e in WORD]
    s = ", ".join(kinds[:-1]) + (" or " if len(kinds) > 1 else "") + kinds[-1]
    s = ("an " if s[0] in "aeiou" else "a ") + s + " planet"
    if "MECATOL_REX" in inel:
        s += ", not Mecatol Rex"
    return "Stands on " + s + "."


def bright(c, top=210):
    m = max(c)
    k = top / m if m > 0 else 1
    return tuple(min(255, int(v * k)) for v in c)


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def stats(u):
    lines = []
    if u.get("spaceCannonHitsOn"):
        n = int(u.get("spaceCannonDieCount", 1))
        lines.append(f"SPACE CANNON {u['spaceCannonHitsOn']}" + (f" (x{n})" if n > 1 else ""))
    if u.get("productionValue"):
        lines.append(f"PRODUCTION {u['productionValue']}")
    if u.get("planetaryShield"):
        lines.append("PLANETARY SHIELD")
    if u.get("sustainDamage"):
        lines.append("SUSTAIN DAMAGE")
    if u.get("combatHitsOn"):  # (0.82.2: the Sleeping World's combat box - "3" with two hit stars)
        n = int(u.get("combatDieCount", 1))
        lines.append(f"COMBAT {u['combatHitsOn']}" + (f" (x{n})" if n > 1 else ""))
    return "\n".join(lines)


def main():
    units = json.load(open(os.path.join(ROOT, "data/units/monuments.json")))
    data = {u["faction"]: u for u in units}
    data.update({u["id"]: u for u in units})  # (0.82.4: the Rhodun's two sides by their units' ids)
    out = ["// Generated by tools/gen_monuments.py - do not edit. Monuments+ v3.1 cards (Cacotopos / Blarknob).",
           f"const MonumentDef kMonuments[{len(CARDS)}] = {{"]
    for key, fac, akey, text, flav, src in CARDS:
        u = data[akey]
        elig, inel = u.get("eligiblePlanetTypes", []), u.get("ineligiblePlanetTypes", [])
        wh = None
        if key in SPECIAL_PLACE:
            elig, inel, wh = SPECIAL_PLACE[key]
        e = " | ".join(PLACE[x] for x in elig) or "0"
        i = " | ".join(PLACE[x] for x in inel) or "0"
        fr, sb = COLOURS[key]
        fr = bright(fr)
        sub = SUBTITLE_FIX.get(key, u["subtitle"].replace("Norr", "N'orr"))
        out.append("  {" + ", ".join([cstr(key), cstr(fac), cstr(u["id"]), cstr(sub), cstr(text), cstr(flav), cstr(STATS_FIX.get(key, stats(u))),
                                     f"uint16_t({e})", f"uint16_t({i})", cstr(wh or where(elig, inel)),
                                     f"0x{fr[0]:02X}{fr[1]:02X}{fr[2]:02X}u", f"0x{sb[0]:02X}{sb[1]:02X}{sb[2]:02X}u",
                                     cstr(src)]) + "},")
    out.append("};")
    with open(OUT, "w") as f:
        f.write("\n".join(out) + "\n")
    print("wrote", OUT, len(CARDS), "monuments")


if __name__ == "__main__":
    main()
