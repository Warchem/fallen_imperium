// The Retrowave New Game backdrop in the title's detail (0.60.1, owner: "update the new game background of retrowave to be as
// high detailed as the Main menu"). The same scene as the 0.30.0 / 0.46.2 backdrop (backdrops::drawRetroNewGame) - a great tear
// bleeding in the cosmos at the left with a Vuil'raith tentacle reaching out of it, smaller rifts, a war sun firing on a void
// dragon rising at the right, a cruiser crossing the top - now drawn as fine pixel art on the title's grid (one art pixel per
// real pixel, two on a big sharp screen; titlebg::draw) over a nebula sky: dithered shading on every round thing, the tear's
// edge flickering and its inside swirling, rocks drawn into it, eyes opening in the dark beyond, a detailed cruiser with escorts.
#pragma once
#include <cstdint>

class Canvas;

namespace newgamebg {
// The whole canvas (every frame; the New Game panel is drawn over it).
void draw(Canvas& c, uint32_t now);
}  // namespace newgamebg
