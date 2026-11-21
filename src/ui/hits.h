// Things the mouse can point at (Windows / Steam Deck, 0.27.0). While a screen
// draws, its lists, rows and buttons say where they are; the app then turns
// the mouse into the same button presses the Miyoo makes: pointing at a row
// selects it (as the D-pad would), clicking it also confirms (A), clicking a
// button in the hint bar presses that button. Cleared before every draw.
#pragma once
#include <functional>

#include "core/input.h"
#include "gfx/canvas.h"

namespace hits {

void clear();
// Widescreen (0.46.0): added to every rect from here on - a screen laid out for 640
// units drawn centred on a wider one says where its rows are (set by CentredView).
int& origin();
// Map button layout (0.47.0): a screen drawn shrunk in a pop-up window - its rows land at
// (origin, originY) + k x where they were drawn. (k 1 and originY 0 otherwise.)
int& originY();
float& scale();
// (0.72.0, owner: "every button / row lights up under the pointer the same way") how a button or action lights up under the
// pointer (the app draws it over the screen): not at all, a box round its rect, or a ring (round things). Rows don't: pointing
// at one selects it, and the list shows its selection.
enum class Light { None, Box, Round };
// A row / choice: pointing at it runs `select`; a click selects it and presses A.
void add(Rect r, std::function<void()> select);
// A button: a click presses `b` (hint bar badges, on-screen buttons).
void addButton(Rect r, Btn b);
// Something with its own click: pointing does nothing, a click runs `act` (no A).
void addAction(Rect r, std::function<void()> act, Light light = Light::Box);
// (0.72.0) A window's or panel's own area: it takes the click (nothing happens, nothing under it gets it) and never lights up.
void addBlock(Rect r);
// (0.61.2) the same for a whole-screen catcher under a window or a question ("a click elsewhere does nothing / closes it"): it takes
// the click, but the map under it still follows the pointer and the wheel (overUi ignores it)
void addBackdrop(Rect r, std::function<void()> act);

// The app's side. hover: runs the select of what is under (x, y); true if anything is.
bool hover(float x, float y);
// any: is there anything to click at (x, y)? (0.58.4: a click on nothing may be ignored - Scene::bareClickIsA)
bool any(float x, float y);
// 0.61.2 (owner's document: "no interactions can be done on the background of a menu while playing the game"): the panels drawn
// this frame (drawPanel, the decision frames) - the map underneath them takes no pointing and no clicks
void addPanel(Rect r);
bool onPanel(float x, float y);
// onPanel or any: the mouse is on something of the interface, not on the map
bool overUi(float x, float y);
// (0.72.0) what lights up at (x, y): the topmost thing there, if it is a button or an action that lights up
bool lit(float x, float y, Rect& r, Light& light);
// press: what a left click at (x, y) means - the button to press (Btn::A for
// a row or empty space), or Btn::None when an action took the click.
Btn press(float x, float y);

}  // namespace hits
