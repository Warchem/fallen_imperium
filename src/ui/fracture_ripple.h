// The Fracture appears (0.73.5, owner 2026-11-19: "Once the fracture appears by roll or ability, The game map ripples with
// the epicenter of the ripple at Styx. The ripple spread across the whole galactic map with a cosmic eldritch screech noise,
// as if the fabric of spacetime just ripped apart. A line of Text appears at the top of the screen that says the fracture
// appears, using the text in the game log.")
//
// apply() bends the picture already drawn: a ring of waves running out from the epicentre over the whole map, the colours
// splitting and a crimson light along its crest, a second weaker ring behind it, a flash and a tear of light where it
// began. drawLine() is the line of text at the top.
#pragma once
#include <cstdint>
#include <string>

#include "gfx/canvas.h"

namespace ripple {

constexpr uint32_t kLength = 3800;  // ms: the ripple has crossed the map and the line has faded

// Bend the canvas's pixels inside `area` (game units) - the ripple t ms after it began at (cx, cy) (game units).
void apply(Canvas& c, Rect area, float cx, float cy, uint32_t t);
// The line of text along the top of `area`, t ms in (and a second, smaller one under it).
void drawLine(Canvas& c, Rect area, const std::string& text, uint32_t t, const std::string& sub = "");
// (0.73.8, the Obsidian) The whole canvas turned toward crimson by `amount` (0..1) - called on the space behind the map,
// before the map is drawn; t (ms) makes it throb.
void crimson(Canvas& c, float amount, uint32_t t);

}  // namespace ripple
