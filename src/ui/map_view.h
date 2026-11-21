// Draws the live galaxy: tiles, planets with their owners, units, command
// tokens, the cursor and highlighted (selectable) systems.
#pragma once
#include <cstdint>
#include <vector>

#include <array>

#include "engine/state.h"
#include "galaxy/hex.h"
#include "gfx/canvas.h"
#include "ui/decor_tiles.h"

class MapView {
public:
    static constexpr int kZoomLevels = 4;

    // The viewport is where the camera keeps the cursor (and centres a map that
    // fits); the draw area (0.20.0, default = the viewport) may be larger - the
    // map then also shows under see-through panels around it.
    void setViewport(Rect r) {
        view_ = r;
        if (area_.w <= 0) area_ = r;
    }
    void setDrawArea(Rect r) { area_ = r; }
    Rect viewport() const { return view_; }
    Rect drawArea() const { return area_.w > 0 ? area_ : view_; }
    void setZoom(int z);
    int zoom() const { return zoom_; }
    // (0.63.0) the New Game previews: the whole map at the tile size that fits `area` (between zoom levels; the camera
    // centres it there)
    void fitTo(const ti::Game& g, Rect area);
    float tileSize() const;

    // Keyboard & mouse (0.27.0, owner: "like Hearts of Iron 4 / Total War"): a free
    // camera that WASD and a right-button drag move and the wheel zooms smoothly
    // (around the mouse); pointing at a tile puts the cursor there without moving
    // the camera. The D-pad brings back the camera that follows the cursor.
    void pan(const ti::Game& g, float dx, float dy);  // the map moves by (dx, dy) game units
    void zoomBy(const ti::Game& g, float notches, float ax, float ay);
    void tick(const ti::Game& g, uint32_t now);       // eases a zoom; call every frame
    // What is under a point of the screen: a system, kInfoBase + player, kPlayBase + player, kDecorBase + i; -1 nothing.
    int hitTest(const ti::Game& g, float x, float y) const;
    // The draft: the open space under a point (-1 none).
    int hitDraftCell(const ti::Game& g, float x, float y) const;
    // Where an open space is drawn now (its centre and hex radius); false if it isn't one.
    bool draftCellAt(const ti::Game& g, int cell, float& x, float& y, float& size) const;
    // (0.73.5) where a system is drawn now (its centre and hex radius); false if there is none
    bool systemAt(const ti::Game& g, int sys, float& x, float& y, float& size) const;
    void pointAt(const ti::Game& g, int what);  // the cursor goes there; the camera stays
    bool freeCamera() const { return free_; }

    int cursor() const { return cursor_; }
    void setCursor(const ti::Game& g, int sys, bool keepCamera = false);
    // Faction info tiles (0.20.0, owner): three joined tiles beside each home -
    // command tokens; faction, VP, secrets, strategy card; trade goods,
    // commodities, action cards. The cursor can stand on them: cursor() is
    // then kInfoBase + player.
    static constexpr int kInfoBase = 1000;
    // The play area beside them (0.43.4, owner: "make that play area interactable") is a stop of its own:
    // kPlayBase + player. infoPlayer() is that player on either; playPlayer() only on the play area.
    static constexpr int kPlayBase = 1500;
    int infoPlayer() const {
        return cursor_ >= kPlayBase && cursor_ < kDecorBase ? cursor_ - kPlayBase : cursor_ >= kInfoBase && cursor_ < kPlayBase ? cursor_ - kInfoBase : -1;
    }
    int playPlayer() const { return cursor_ >= kPlayBase && cursor_ < kDecorBase ? cursor_ - kPlayBase : -1; }
    // Thunder's Edge placeholders (0.24.0): the Fracture and Thunder's Edge beside the
    // board - the cursor is then kDecorBase + index into decorTiles().
    static constexpr int kDecorBase = 2000;
    const std::vector<decor::Tile>& decorTiles(const ti::Game& g) const;
    const decor::Tile* decorAt(const ti::Game& g, int cursor) const {
        const auto& d = decorTiles(g);
        return cursor >= kDecorBase && cursor - kDecorBase < int(d.size()) ? &d[size_t(cursor - kDecorBase)] : nullptr;
    }
    void setInfoTiles(bool on) { infoTiles_ = on; }
    bool infoTiles() const { return infoTiles_; }
    // The three spaces of each player's info tiles (count in n, 0 if none fit), and a fourth one beside them for the
    // player's play area (0.43.0: shown only while something is in play - play_area.h).
    struct InfoCells {
        std::array<Hex, 3> cell;
        int n = 0;
        Hex play{};
        bool hasPlay = false;
    };
    const std::vector<InfoCells>& infoCells(const ti::Game& g) const;
    // The cells of p's info tiles drawn now (the play area's too when it has cards); returns how many.
    int infoShown(const ti::Game& g, int p, Hex out[4]) const;
    void setViewer(int p) { viewer_ = p; }  // whose eyes the play areas are drawn for (face-down cards)
    // dir: 0 up, 1 right, 2 down, 3 left. Returns true if the cursor moved.
    bool moveCursor(const ti::Game& g, int dir);

    void setHighlights(const std::vector<int>& systems) { highlights_ = systems; }
    void clearHighlights() { highlights_.clear(); }
    bool highlighted(int sys) const;
    void setActiveSystem(int sys) { active_ = sys; }

    // Short-lived effects used to show what the players are doing.
    // strong = filled, fast-blinking flash (e.g. red for an attack).
    void flash(int sys, uint32_t color, uint32_t now, uint32_t ms, bool strong = false);
    void arrow(int from, int to, uint32_t color, uint32_t now, uint32_t ms);
    void clearEffects() {
        flashes_.clear();
        arrows_.clear();
    }
    // Waypoint Mode (0.56.0): routes drawn while a move is being planned - from, numbered stops, the active system
    // (to = -1: not reached yet). roll[i]: leaving that point (0 = from, 1.. = the stops) rolls for a gravity rift.
    struct RouteLine {
        int from = -1, to = -1;
        std::vector<int> stops;
        std::vector<uint8_t> roll;
        uint32_t color = 0;
        bool editing = false;  // the one being edited: brighter, its end pulses
    };
    void setRoutes(const std::vector<RouteLine>& r) { routes_ = r; }
    void clearRoutes() { routes_.clear(); }

    void draw(Canvas& c, const ti::Game& g, uint32_t now) const;

    // Faction borders (Stellaris-like territory outlines); on by default.
    void setBorders(bool on) { borders_ = on; }
    bool borders() const { return borders_; }
    // Who each system belongs to for the borders (-1 = nobody): the player
    // controlling most of its planets (ties: ships there, then planet value);
    // a system without planets belongs to a player who owns 3+ of its
    // neighbours when no one else owns a neighbour or has ships in it.
    static void territory(const ti::Game& g, std::vector<int>& owner);

    // Galaxy setup draft: the open spaces (g.draft) are drawn as empty slots;
    // the draft screen marks where its tile may go and shows it in place.
    void setDraftSpots(const std::vector<int>& cells) { draftSpots_ = cells; }
    void setDraftGhost(int cell, int def) {
        draftCell_ = cell;
        draftDef_ = def;
    }
    void clearDraft() {
        draftSpots_.clear();
        draftCell_ = draftDef_ = -1;
    }
    // 0.61.2: empty spaces to choose at the edge of the board (Star Charts, Age of Exploration...): drawn as lit-up slots,
    // `sel` with a gold rim; hitFreeSpot = the one under a point (-1 none)
    void setFreeSpots(const std::vector<Hex>& spots, int sel) {
        freeSpots_ = spots;
        freeSel_ = sel;
    }
    void clearFreeSpots() {
        freeSpots_.clear();
        freeSel_ = -1;
    }
    int hitFreeSpot(const ti::Game& g, float x, float y) const;
    // A system tile on its own (not on the map): the hand of tiles, previews.
    // now: the Retrowave animations' time (0 = the map's)
    static void drawTile(Canvas& c, int def, float cx, float cy, float size, uint32_t now = 0);

private:
    void camera(const ti::Game& g, float size, float& ox, float& oy) const;
    void drawSystem(Canvas& c, const ti::Game& g, int sys, float cx, float cy, float size) const;
    void drawUnits(Canvas& c, const ti::Game& g, int sys, float cx, float cy, float size) const;

    Rect view_{0, 0, 420, 452};
    Rect area_{0, 0, 0, 0};
    int zoom_ = 1;
    // the free camera (0.27.0): the map point at the viewport's centre, in tile-size units
    bool free_ = false;
    float focusX_ = 0, focusY_ = 0;
    float size_ = 0, sizeTarget_ = 0;  // smooth zoom (0 = the zoom level's size)
    float anchorX_ = 0, anchorY_ = 0;
    uint32_t lastTick_ = 0;
    void startFree(const ti::Game& g);
    void clampFocus(const ti::Game& g);
    void bounds(const ti::Game& g, float& minx, float& maxx, float& miny, float& maxy) const;  // tile-size units
    bool visible(const ti::Game& g, Hex h) const;
    int cursor_ = 0;
    float rowY_ = 0;
    int active_ = -1;
    std::vector<int> highlights_;
    struct Flash {
        int sys;
        uint32_t color, start, until;
        bool strong;
    };
    struct Arrow {
        int from, to;
        uint32_t color, start, until;
    };
    mutable std::vector<Flash> flashes_;
    mutable std::vector<Arrow> arrows_;
    std::vector<RouteLine> routes_;
    bool borders_ = true;
    bool infoTiles_ = false;
    int viewer_ = -1;
    mutable std::vector<InfoCells> info_;
    mutable size_t infoKey_ = 0;
    mutable std::vector<decor::Tile> decor_;
    mutable size_t decorKey_ = 0;
    void drawInfoTiles(Canvas& c, const ti::Game& g, float ox, float oy, float size, uint32_t now) const;
    mutable std::vector<int> owner_;     // territory, worked out once per draw
    mutable std::vector<int> grid_;      // board position -> system (for neighbours)
    int ownerAt(Hex h) const;
    std::vector<int> draftSpots_;
    std::vector<Hex> freeSpots_;  // (0.61.2)
    int freeSel_ = -1;
    void drawFreeSpots(Canvas& c, float ox, float oy, float size, uint32_t now) const;
    int draftCell_ = -1, draftDef_ = -1;
    void drawDraft(Canvas& c, const ti::Game& g, float ox, float oy, float size, uint32_t now) const;
    static void drawWarp(Canvas& c, float cx, float cy, float size, uint32_t now, bool active);
    void drawBorders(Canvas& c, const ti::Game& g, int sys, float cx, float cy, float size) const;
};
