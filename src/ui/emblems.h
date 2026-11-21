// League emblems (0.80.0, owner 2026-11-19: "a row of the earned medals on the title screen, under the menu"; 2026-11-20:
// "each league can have an emblem and an unlockable"): sixteen medals drawn by the game - a metal rim, an enamel field in
// the league's colour and a symbol of its own. Keys as in the league table (src/game/leagues.h: LeagueDef::emblem).
#pragma once
#include <cstdint>
#include <string>

#include "gfx/canvas.h"

namespace emblems {

enum State { kEarned, kOpen, kLocked };  // earned: in full; open (not yet earned): a faint ghost; locked: a padlock

// key: "bronze", "silver", "gold", "ultimate", "crown", "laurel", "starfarer", "crest", "lantern", "bolt", "sun", "pearl",
// "circuit", "compass", "shard", "ember". r: the medal's radius in game units. now: for the glint that crosses an earned
// medal now and then (0 = none).
void draw(Canvas& c, const std::string& key, float cx, float cy, float r, uint32_t colour, State st = kEarned, uint32_t now = 0);
const char* name(const std::string& key);  // "Bronze emblem", ...
int count();
const char* keyAt(int i);

}  // namespace emblems
