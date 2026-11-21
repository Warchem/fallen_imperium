// Player colours with a finish (0.62.0 draft, owner 2026-11-18): the 8 classic colours plus 8 new ones that move - metals
// that glint (Gold, Silver, Bronze), colours that glow and pulse (Luminescent Green, Hatred Crimson), Pearlescent (a sheen
// that drifts through pastel tints) and two of mine: Ion Cyan (electric arcs) and Void (black with stars in it). Owner's
// plan: later (near 0.9) colours, tilesets and some factions become unlockables (achievements).
// 0.75.0 (owner 2026-11-20 17:40: "make 8 new colors"): Brown (plain, like the classic 8), Retro Sunset (a purple-orange
// blend with the sun's bands), Crystal (cut facets that catch a turning light), Ash (grey crust with ember cracks that
// smoulder), Prismatic (a rainbow that drifts), Wireframe Green / Blue (dark inside, every edge a glowing vector line, a
// scan line running down) and Rose Gold (a metal like Gold).
//
// How a finish works: anything drawn in a player's colour is drawn as before in the colour's flat value (base(c)); a Finish
// placed around that drawing remembers the pixels under it first and, when it goes out of scope, finds the pixels that the
// drawing changed toward the colour and works the effect into them (and around them, for the glows). The classic 8 have
// no finish (it costs nothing for them).
#pragma once
#include <cstdint>
#include <vector>

#include "gfx/canvas.h"

namespace ti {
struct Game;
}

namespace pcolours {

constexpr int kClassic = 8;  // Red .. Pink: the colours every game had until now
constexpr int kCount = 24;  // = ti::kPlayerColours (engine/state.h)

enum class Fx : uint8_t { None, Sparkle, Glow, Heartbeat, Pearl, Arcs, Void, Sunset, Crystal, Ash, Prism, Wire };

struct Colour {
    const char* name;   // in full (the colour window, tooltips)
    const char* brief;  // short (table cells, small labels)
    uint32_t base;      // the flat value (what the game draws with)
    uint32_t hi, lo;    // its light tone and its dark (or glow) tone
    Fx fx;
    const char* look;  // a few words about the finish
};

const Colour& info(int c);
inline uint32_t base(int c) { return info(c).base; }
// the colour for words written in it (lighter where the colour itself is too dark to read: Void, Hatred Crimson, Brown)
uint32_t text(int c);
inline bool animated(int c) { return info(c).fx != Fx::None; }
// (0.75.0) words written ON a fill of colour c: `usual` (dark), except on the Wireframe colours, whose fills go dark - there
// in the line colour
uint32_t ink(int c, uint32_t usual);

// The clock the finishes run on: the app sets it before drawing each frame (tests set their own).
void setNow(uint32_t ms);
uint32_t now();

// player p's colour index in game g, or -1 (neutral units, nobody)
int of(const ti::Game& g, int p);

// the rect round a point (game units) - where a Finish goes round a disc, a token or a unit
inline Rect around(float x, float y, float rx, float ry) {
    return Rect{int(x - rx) - (x - rx < 0.f ? 1 : 0), int(y - ry) - (y - ry < 0.f ? 1 : 0), int(2.f * rx) + 2, int(2.f * ry) + 2};
}

// Words: a colour value as it should be written (Void, Hatred Crimson and Brown lighter so they can be read; others unchanged)
uint32_t readable(uint32_t flat);

// Outlines too big for a Finish (territory borders, a home's outline): each edge takes the colour's look directly.
// edgeTone: the colour for a strip of the edge at (x, y) (game units) - `flat` is what it would be, `line` = the bright edge
// line (not the soft band); edgeGlow: how much stronger the soft bands are right now (1 = as drawn); edgeExtras: the glints,
// arcs, stars or outer glow along one edge from (x1, y1) to (x2, y2), with (nx, ny) the unit normal pointing inside.
uint32_t edgeTone(int colour, uint32_t flat, float x, float y, bool line);
float edgeGlow(int colour);
void edgeExtras(Canvas& c, int colour, float x1, float y1, float x2, float y2, float nx, float ny, float band, uint32_t seed);

// The finish over what is drawn in colour `colour` inside `r` (game units) while it lives. seed: something that tells two
// elements apart (so their glints differ). margin: room outside r for a glow (game units; -1 = the effect's own).
class Finish {
public:
    Finish(Canvas& c, Rect r, int colour, uint32_t seed = 0, int margin = -1);
    ~Finish();
    Finish(const Finish&) = delete;
    Finish& operator=(const Finish&) = delete;

private:
    Canvas* c_ = nullptr;
    int colour_ = 0, x0_ = 0, y0_ = 0, w_ = 0, h_ = 0;  // the area, in real pixels
    uint32_t now_ = 0, seed_ = 0;
    float margin_ = 0.f;  // real pixels
    std::vector<uint32_t> before_;
};

}  // namespace pcolours
