// The Retrowave title backdrop (0.59.70, owner: "the background slowly pans to a highly detailed planet (in the same spot where
// the planet shows in the simplistic background for the title menu); a nuclear-battered world. On the dark side of the planet you
// can see a web of lights showing a massive (and only) city on the planet. Ship traffic to and from it and a space station can be
// seen. The entire background is animated and in a retrowave fashion"; his reference picture: a dark world with a web of orange
// city lights on its night side, teal clouds on the lit side, a ring station and ships around it).
//
// Drawn as pixel art on its own grid (1 or 2 art pixels per screen unit) and blown up to the screen: a deep purple sky with a
// nebula and a far sun, three layers of drifting stars, the planet (scorched land, poisoned seas, glowing blast craters, toxic
// teal clouds drifting, a neon rim of atmosphere), the city web on the night side (lights twinkling, pulses running out along its
// arteries), a ring station turning above it and ships coming and going with engine trails. The planet is made once, in a
// background thread (prepare); everything that moves is drawn every frame.
#pragma once
#include <cstdint>

class Canvas;

namespace titlebg {
// Start making the planet now (a background thread) - the title calls it at once, so it is ready before the pan reaches it.
void prepare();
bool ready();
// Screen tests: wait until the planet is made.
void finish();
// The whole canvas: pan 0 = looking at empty space (the planet below the screen), 1 = the planet in place; sky 0 = black (only
// the stars), 1 = the full sky.
void draw(Canvas& c, uint32_t now, float pan, float sky);
// Where the city's heart is on the screen (game units, the canvas's full width) - for the screen tests.
void cityOnScreen(const Canvas& c, float pan, float& x, float& y);
}  // namespace titlebg
