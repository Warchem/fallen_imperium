#pragma once
// Monuments+ (0.82.0): a monument's card, drawn by the game in Monuments+'s own landscape layout (owner 2026-11-21: "If we
// used the card format that's in the reference, we should be fine"; 22:10: keep the placement words under the picture, the
// credit line only on the Credits screen) with its animated pixel picture (tools/gen_monument_anim.py ->
// assets/monuments/<key>.png: 64 frames of 160 x 120 side by side, 100 ms each).

#include <cstdint>

#include "gfx/canvas.h"

namespace monumentcard {

constexpr int kArtW = 160, kArtH = 120, kArtFrames = 64;

// The card (monument = kMonuments index) in r. 574 x 388 game units is the size it was laid out for; it scales its text to fit.
void draw(Canvas& c, int monument, Rect r, uint32_t now);

// One frame of the animated picture: contain = the whole picture (whole-number enlargement from 2x), cover = the box filled,
// cut top and bottom around focusY (0 top .. 1 bottom). Draws a dark box when the picture is missing.
void art(Canvas& c, int monument, Rect box, uint32_t now, bool cover = false, float focusY = 0.45f);

// (0.82.0) The card's placement icons in a row (crossed-out kinds first, as on the card), s units each, ending at right; returns
// the x where the row starts.
int placeIcons(Canvas& c, int monument, int right, int y, int s, int gap = 4);

// The Monuments+ mark: a temple (pediment, three columns, steps)
void temple(Canvas& c, float cx, float cy, float s, uint32_t col, int alpha = 255);

}  // namespace monumentcard
