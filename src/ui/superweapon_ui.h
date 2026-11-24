// The Belkosea superweapons on screen (0.84.0, owner 2026-11-23): "Since superweapons are placed in the play area, make sure the play
// area is represented not only on the game map but is accessible in the faction sheet. When the player clicks on the area, the
// expanded superweapons window shows each superweapon card around a massive hi-def Retrowave superweapon animation in the center."
//   drawPiece  - a superweapon: the weathered grey sphere with its slit iris (each one its own colour) - on the planet it stands on
//                (map, expanded tile), in the play area and in the faction sheet's tray
//   drawWindow - the window: the megastructure's cannon cycle (assets/art/superweapon_fortress.png, tools/gen_superweapon_art.py) in
//                the middle, the five cards around it (ready / exhausted / purged / not built)
#pragma once
#include <cstdint>

#include "engine/state.h"
#include "gfx/canvas.h"

namespace superweapon {

uint32_t colour(int k);  // the iris colour of superweapon k
void drawPiece(Canvas& c, int k, float cx, float cy, float r, uint32_t now, bool exhausted, int alpha = 255);
void drawWindow(Canvas& c, const ti::Game& g, int p, uint32_t now);

}  // namespace superweapon
