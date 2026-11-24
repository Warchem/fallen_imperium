// Faction pieces on the faction sheet (0.44.1, owner: "use the bottom right of the faction sheet to add a section
// ... these should have physical representations on the faction sheet too"): the tokens, dice and cards a faction
// keeps beside its sheet, drawn as the pieces themselves - Vaden debt (other players' control tokens), the
// Firmament's plots, the Vuil'raith's captured units, Edyn Sigils, Myko-Mentori Omen dice, Creuss wormhole tokens,
// the Empyrean's Void Tethers, the Naalu "0" token, Nekro assimilator tokens, Titans sleepers, Last Bastion
// galvanize tokens, Crimson breaches, Kjalengard Glory tokens and Veldyr Branch Offices.
// The sheet shows them small in a tray; a click (or Y) opens them large, each with what it means now.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/state.h"
#include "gfx/canvas.h"

namespace pieces {

enum class Kind : uint8_t { Control, Plot, Unit, Die, Wormhole, Zero, Assim, Sleeper, Galvanize, Breach, Glory, Branch, Sigil, Tether, Superweapon, Tomb };

struct Piece {
    Kind kind = Kind::Control;
    int faction = -1;      // the symbol on it
    uint32_t col = 0;      // its colour
    int count = 1;         // shown as xN when more than 1
    int value = 0;         // die face / unit type / technology / plot card / wormhole (0 alpha, 1 beta, 2 gamma)
    char letter = 0;       // assimilator token: 'X', 'Y' or 'Z'
    bool dim = false;      // spent, off the board, in reserve
    bool hidden = false;   // face down (a plot other players can't read)
    std::vector<std::pair<int, uint32_t>> tokens;  // control tokens on it (faction, colour)
    std::string label;     // a word or two under it in the tray
    std::string name;      // the large view: its name...
    std::string detail;    // ...and what it means now
};

struct Group {
    std::string title;  // "Debt tokens"
    std::string note;   // the rule, in a line (our own words)
    std::string empty;  // shown when there are no pieces
    std::vector<Piece> pieces;
};

// Player p's faction pieces, as `viewer` may see them (empty: the faction has none).
std::vector<Group> of(const ti::Game& g, int p, int viewer);

void drawPiece(Canvas& c, const Piece& pc, float cx, float cy, float size, uint32_t now, int alpha = 255);
// The tray in the bottom right of the faction sheet.
void drawTray(Canvas& c, const std::vector<Group>& gs, Rect r, uint32_t now, bool hover);
// The large view (full screen). Returns the height of its contents (for scrolling).
int drawDetail(Canvas& c, const ti::Game& g, int p, const std::vector<Group>& gs, Rect r, uint32_t now, int scroll);

}  // namespace pieces
