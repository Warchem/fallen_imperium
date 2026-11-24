// The unlock banner (0.81.0 drafts; owner 2026-11-21 09:20: "a flashy banner whenever something is unlocked ... a hi-def
// retrowave emblematic graphic with the banner that shows what was unlocked"). A neon band sweeps across the screen: a
// Retrowave sky, a striped sun rising on the horizon, a perspective grid rolling toward you, and in front of the sun the thing
// unlocked - a rival commander's silhouette, a faction colour on a ship / border / structure, an emblem, or a faction's picture
// with its codec portrait - beside chrome "UNLOCKED" letters and what it is. Drawing only, from `t` = ms since it appeared.
#pragma once
#include <cstdint>
#include <string>

#include "gfx/canvas.h"

class Image;

namespace unlockbanner {

enum Kind { kRival, kColour, kEmblem, kFaction };

struct Unlock {
    Kind kind = kRival;
    std::string category;  // "RIVAL COMMANDER", "FACTION COLOUR", ...
    std::string name;      // what was unlocked
    std::string line;      // what it means ("Seat them in a Casual Game")
    std::string source;    // how it was earned ("Beaten in the Bronze League")
    int colour = -1;       // kColour: the player colour
    std::string emblem;    // kEmblem: the emblem's key
    uint32_t emblemColour = 0;
    int faction = -1;      // kRival: their faction (the mark on the chest); kFaction: the codec portrait
    const Image* picture = nullptr;  // kFaction: the faction's picture (nullptr: a placeholder frame)
};

// The banner over whatever is on screen; `now` drives the endless motion (grid, stars, glints), `t` the entrance.
void draw(Canvas& c, const Unlock& u, uint32_t t, uint32_t now);
// The entrance is over and A may close it.
inline bool ready(uint32_t t) { return t >= 1500; }

}  // namespace unlockbanner
