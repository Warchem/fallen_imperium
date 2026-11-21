// The War Sun scene (0.73.0, owner 2026-11-19: "the animation for when the Warsun technology is gained or researched ... in a
// hi-def retrowave fashion consistent with the main menu. When researched or gained for the first time of the game ... The
// scene starts off with the camera in space, looking at the horizon of an ice planet. A spaceship flies from behind the camera
// into view, at first appearing to move towards the planet, but then curving upward to space. Another spaceship flies from
// behind the camera into view, at a different direction but then migrates up to space. The camera pans slowly upwards as more
// spaceship traffic is being seen. It eventually stops with a gargantuan moon-sized war-sun being constructed. In the distance,
// spaceships are moving to and from the warsun and the planet below. Text appears above saying: "Technology Discovered. The
// Age of the War Sun Approaches."" - with his three pictures of sphere stations for the war sun's look).
//
// Drawn like the Retrowave title backdrop (title_bg.*): pixel art on its own grid (1 or 2 art pixels per screen unit),
// ordered dithering through colour ramps, blown up to the screen. The sky, the ice planet and the war sun are made once, in a
// background thread (prepare - the game screen starts it when a game begins, so it is ready long before it is needed);
// everything that moves is drawn every frame: the stars, the aurora, the two ships, the traffic, the sparks of the builders,
// the war sun's lights, its eye and its rings.
#pragma once
#include <cstdint>
#include <string>

class Canvas;

namespace warsun {

// the timeline, ms into the scene
constexpr uint32_t kShipA = 450;                  // the first ship comes past the camera (audio Sfx::ShipPassA)
constexpr uint32_t kShipB = 2150;                 // the second ship (Sfx::ShipPassB)
constexpr uint32_t kPanStart = 4300;              // the camera starts to look up (Sfx::WarSunDrums)...
constexpr uint32_t kPanEnd = kPanStart + 8000;    // ...and stops on the war sun (the drums' crescendo: audio::kWarSunDrumsPeakMs)
constexpr uint32_t kTitleAt = kPanEnd + 250;      // "Technology Discovered."
constexpr uint32_t kLineAt = kTitleAt + 1000;     // "The Age of the War Sun Approaches."
constexpr uint32_t kContinueAt = kLineAt + 1900;  // the scene waits for A from here
constexpr uint32_t kLength = kContinueAt + 2600;  // the whole scene when it isn't held (MENU > Cards: timed)

// Start making the art for a screen of this canvas's size (a background thread); nothing to do when it is made.
void prepare(const Canvas& c);
bool ready(const Canvas& c);
// Tests: make it now and wait.
void finish();
// The whole canvas at t ms into the scene (t is held at 0 while the art isn't ready - the caller's clock should wait: ready()).
// The words are drawn too; the caller adds its own line along the bottom (who found it, A: Continue).
void draw(Canvas& c, uint32_t t);
// The letterbox bars' height (game units) at t: the caller's bottom line goes in the lower one.
int barHeight(uint32_t t);
// In the lower bar, from kTitleAt: who found it (their emblem and name, "researched War Sun"); faction -1: none.
void drawCaption(Canvas& c, uint32_t t, int faction, const std::string& who, uint32_t colour);

}  // namespace warsun
