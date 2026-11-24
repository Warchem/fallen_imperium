// The expanded tile (0.20.0, owner's New Additions): one system drawn large -
// planets and anomalies in some detail, every ship on its own, infantry /
// mechs / PDS / fighters as one picture with the count under it, space
// docks, command tokens and other tokens - and a space combat played out on
// it (ships facing each other, dice, shots, explosions).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "engine/state.h"
#include "gfx/canvas.h"

namespace tileview {

// What a space combat shows on the tile, fed from the combat events.
struct CombatShow {
    int attacker = -1, defender = -1;
    ti::Force a, b;  // the ships still in the fight (attacker / defender)
    struct Roll {
        int player = -1;
        std::vector<int8_t> dice, need;
        int hits = 0;
        uint32_t time = 0;
    };
    std::vector<Roll> rolls;  // the latest rolls (newest last)
    struct Loss {
        int player = -1, unit = -1, count = 0;
        uint32_t time = 0;
    };
    std::vector<Loss> losses;  // ships destroyed (exploding for a moment)
    int winner = -1;
    bool ended = false;
    std::string result;
};

// Draws system `sys` large inside `area` (the hex fills it as far as it fits).
// combat: when given, the ships come from it (attacker left, defender right).
void draw(Canvas& c, const ti::Game& g, int sys, Rect area, uint32_t now, const CombatShow* combat = nullptr);

// A unit as a small picture in colour col, centred on (cx, cy), about `size`
// pixels long; ships face right unless faceLeft. outline: a dark rim (units
// standing on a planet).
void drawUnit(Canvas& c, ti::UnitType t, float cx, float cy, float size, uint32_t col, bool faceLeft = false, int alpha = 255,
              bool damaged = false, uint32_t now = 0, bool outline = false);

// (0.52.0, owner: ships drawn on the map tiles) A unit drawn small on a map tile, centred on (cx, cy), about `size`
// long: Retrowave (pixelBlock >= 2) pixel art like the expanded tile's, otherwise the picture with a dark rim.
void drawMapUnit(Canvas& c, ti::UnitType t, float cx, float cy, float size, uint32_t col, bool damaged, uint32_t now, int pixelBlock);

// (Monuments+ drafts, the 0.82.0 plan) monuments drawn on planets for the draft pictures: (planet index, player). Empty = none.
void setDraftMonuments(const std::vector<std::pair<int, int>>& m);

}  // namespace tileview
