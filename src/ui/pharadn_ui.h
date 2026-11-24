// The Pharad'n Order on screen (0.85.0 Blue Reverie).
//   drawTomb - a tomb token (Ancient Empire): a dark disc with a stepped pyramid and a glowing doorway - on the planets that hold one
//              (map, expanded tile) and in the faction sheet's tray
#pragma once
#include <cstdint>

#include "gfx/canvas.h"

namespace pharadn {

uint32_t gold();    // the Pharad'n gold (tomb doorways, captured-infantry rings)
void drawTomb(Canvas& c, float cx, float cy, float r, uint32_t now, int alpha = 255);

}  // namespace pharadn
