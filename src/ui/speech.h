// What the computer players say over the comms codec (0.57.0, owner: a voice for every faction - docs/owner/
// negotiation/Negotiation_Line_Gists.txt). The lines live in tools/lines/*.txt (tools/gen_lines.py ->
// ui/gen/lines.inc). Display only: a line is chosen by a hash the caller makes from the game's seed, the round
// and the pair - never from the game's dice, so saves replay exactly as before.
//
// Ways of speaking drawn here: the L1Z1X's words wrapped in machine junk with Lazaxian glyphs at both ends, the
// Nekro's two readable words buried in junk, the Obsidian entirely in the Lazaxian alphabet, the Glimmer of
// Mortheus fading in and out, the Ghemina's Lord and Lady finishing each other's sentences, the Arborec's
// words swaying like leaves, the Creuss / Crimson whispering, the Zelian's growl grinding.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gfx/canvas.h"
#include "gfx/font.h"

namespace speech {

// Yield (owner 2026-11-15): it gives in to a threat - grudging, not pleased
// (0.64.0, owner: "an animated opponent who talks about you") the line it opens your offer screen with - what lies between you:
// Hello (nothing yet), Neighbour, Partner (deals made), Kept / Broken (your promises to it), Clash (you attacked it), Leader (you
// lead), and after the round-3 reveal Warm / Wary / Hostile (how it feels about you). Same order as negotalk::Why.
enum class Cat : int {
    Offer, Friendly, Threat, Vote, Accept, Decline, Counter, Betray, Betrayed, Yield,
    Hello, Neighbour, Partner, Kept, Broken, Clash, Leader, Warm, Wary, Hostile,
    // (0.71.0) combat reactions: attacking (space) / attacked / invading / invaded, then the situations
    Attack, Defend, Invade, Hold, Rematch, Mecatol, Home,
    Count
};

// How other factions address faction f in speech ("Hacan", "Star Kings").
std::string address(int faction);
// The words faction `speaker` says to faction `listener` (a line written for that pair first, when there is one),
// {them} filled in. `seed` chooses among them. Empty when there is nothing (unknown faction).
std::string pick(int speaker, int listener, Cat cat, uint32_t seed);
// (0.71.0) Every line of kind `cat` faction `speaker` has for `listener`: the ones written for that pair (pairOnly) or the
// generic ones; with {them} filled in, or as written (fill = false: then fillThem() does it).
std::vector<std::string> options(int speaker, int listener, Cat cat, bool pairOnly, bool fill = true);
std::string fillThem(const std::string& text, int listener);
// A seed from numbers that stay the same while a moment is on screen.
uint32_t seedOf(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0);
uint32_t seedOf(const std::string& s, uint32_t salt = 0);

// A line laid out in a box: words with their font, colour and effect.
struct Word {
    std::string text;
    const FontData* font = nullptr;
    uint32_t col = 0;
    uint8_t fx = 0;  // 0 plain, 1 fading, 2 swaying, 3 grinding, 4 whisper, 5 flickering junk
    int x = 0, y = 0, start = 0;
};
struct Speech {
    std::vector<Word> words;
    int width = 0, height = 0, chars = 0;
    const FontData* font = nullptr;
};
// Lays out `text` the way faction `speaker` talks, in a box `width` wide, with `font` for the words (FONT_MD or
// smaller); `accent` colours the machine junk and the Ghemina's names.
Speech layout(int speaker, const std::string& text, int width, const FontData& font, uint32_t accent, uint32_t textCol,
              uint32_t seed);
// The largest of FONT_MD / FONT_SM / FONT_XS whose layout fits `maxH`.
Speech layoutFit(int speaker, const std::string& text, int width, int maxH, uint32_t accent, uint32_t textCol, uint32_t seed);
// Draws it with its top left at (x, y): `shown` characters typed out (-1 = all), `now` (ms) moves the effects.
void draw(Canvas& c, const Speech& s, int x, int y, int shown, uint32_t now, int alpha = 255);
// Characters typed out after `ms` (about 55 a second).
int typed(const Speech& s, uint32_t ms);

}  // namespace speech
