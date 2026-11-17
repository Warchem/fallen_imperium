// The Voidfall title backdrop (0.59.70, owner 2026-11-17 12:45): "the backdrop is a starry sky, a neon twilight of orange and
// purple. After the font transformation, the screen pans down to an open battlefield with detailed dreadnoughts and cruisers
// above. The battlescape is a dead, cratered, gray field full of ruins and rubble. A fierce battle is taking place; human soldiers
// in greyish blue combat gear are running in from the left ... shooting las-guns at the enemy (red pulse laser fire). The enemy is
// entering from the right, meeting them in the center. The enemy are blue skinned humans with long silver hair and gilded combat
// armor ... plasma bolts from plasma rifles, and sustained blue laser beams from their giant biped mechs. The humans' biped mech
// ... shoots a Gatling laser at the enemy mech. ... shots killing soldiers within one to two hits, soldiers using the ruins and
// craters as cover, and more mechs / soldiers filling in the scene. Dead bodies persist for more than 3 mins, and roughly up to
// 100 total living soldiers (50 per side max) ... Occasionally in the distance, the Sol dreadnought will bombard ... a massive
// futuristic bomb payload will land on the battle scene, killing anyone in the blast radius." Voidfall colours, Retrowave units
// and ships; his reference pictures are in docs/owner/title_voidfall.
//
// Pixel art on its own grid (as title_bg), graded with the Voidfall look at the end. The battle runs on its own clock in fixed
// steps (deterministic: the same frames give the same picture), started a while before the title shows so it is under way.
#pragma once
#include <cstdint>

class Canvas;

namespace titlewar {
void prepare();  // start the battle (it runs a little ahead so the field is full when the pan arrives)
// The whole canvas: pan 0 = the starry sky (the field below the screen), 1 = the battlefield; sky 0 = black (only stars).
void draw(Canvas& c, uint32_t now, float pan, float sky);
// Screen tests: how many soldiers stand / lie on each side, mechs, bombardments so far.
struct Stats {
    int alive[2] = {0, 0}, bodies = 0, mechs[2] = {0, 0}, bombs = 0, shots = 0;
    int mechsLost[2] = {0, 0};  // walkers blown up so far
    float beamOn = 0.f, clock = 0.f;  // seconds with an enemy beam firing, battle time
};
Stats stats();
// The units and ships on one page, large (for the owner to fine-tune them; screen tests only).
void assetSheet(Canvas& c, uint32_t now);
}  // namespace titlewar
