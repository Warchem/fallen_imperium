// Play areas (0.43.0, owner's 0.42 feedback): the cards a player has "in play" near their faction tiles on the map -
// other players' promissory notes face up in front of them (Alliances, Trade Alliance, Support for the Throne...), a
// hero placed near the board for this round (Coil Overcharge, Krill Drakkon, Starsails...), Saint Binal's
// technologies, the Oracle AI objective (face down unless you may see it), the Olradin policies, Axis Orders, the
// Myko-Mentori Omen dice and the trade goods on The Sowing.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/state.h"

class Canvas;

namespace playarea {

enum class Kind { Note, Hero, Techs, Objective, Policy, Order, Omen, Sowing, Relic, Law, Superweapon };

struct Item {
    Kind kind = Kind::Note;
    std::string name;    // the card's title
    std::string detail;  // one line for the side panel
    uint32_t col = 0;    // frame colour
    int faction = -1;    // emblem (kFactions index)
    int unit = -1;       // UnitType (Axis Orders)
    int value = 0;       // die face (Omen), trade goods (Sowing), objective (Objective; -1 face down), side (Policy: 1 = "-")
    bool dim = false;    // used / exhausted
    bool glow = false;   // a card only the viewer may see (Oracle AI)
    std::string kindLine;  // what kind of card, for the play-area window (0.43.4)
    std::string text;      // its full text there
};

// What player p has in play, as seen by `viewer` (-1: nobody in particular).
std::vector<Item> of(const ti::Game& g, int p, int viewer);

// The cards drawn inside one map hex centred on (x, y) (flat-top, corner radius `size`).
void drawCell(Canvas& c, const std::vector<Item>& items, float x, float y, float size, int alpha, uint32_t now);

// One card drawn in a box (also used by the side panel).
void drawCard(Canvas& c, const Item& it, int x, int y, int w, int h, int alpha, uint32_t now);

}  // namespace playarea

// The play-area window (0.43.4, owner: "click on that play area on the map to open a window and view those cards"):
// player's cards large with their full text, left / right to flip, L1 / R1 the next player with cards in play.
#include <memory>
class InfoScreen;
std::unique_ptr<InfoScreen> makePlayAreaScreen(const ti::Game& g, int viewer, int player);

namespace playarea {

}  // namespace playarea
