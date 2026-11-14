#include "ui/map_view.h"

#include "ui/cards.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "engine/query.h"
#include "gfx/font.h"
#include "gfx/image.h"
#include "ui/decor_tiles.h"
#include "ui/icons.h"
#include "ui/play_area.h"
#include "ui/painted.h"
#include "ui/retro.h"
#include "ui/tile_view.h"
#include "ui/voidfall.h"
#include "ui/theme.h"

using namespace ti;

namespace {
constexpr float kSizes[MapView::kZoomLevels] = {24.f, 34.f, 50.f, 70.f};
// Retrowave (0.26.0): sizes that suit the art (corner radius 48): half, 0.7, 1:1, 1.5
constexpr float kRetroSizes[MapView::kZoomLevels] = {24.f, 34.f, 48.f, 72.f};
uint32_t g_mapNow = 0;  // the time of the map being drawn (the Retrowave animations)
constexpr float kPi = 3.14159265f;

void hexCorners(float cx, float cy, float s, float* xs, float* ys) {
    for (int i = 0; i < 6; ++i) {
        float a = kPi / 3.f * float(i);
        xs[i] = cx + s * std::cos(a);
        ys[i] = cy + s * std::sin(a);
    }
}

// midpoint of a flat-top hex edge; edges clockwise from the top (0 = north)
void edgeMid(float cx, float cy, float s, int e, float& x, float& y) {
    float a = -kPi / 2.f + float(e) * kPi / 3.f;
    float r = s * 0.8660254f;
    x = cx + r * std::cos(a);
    y = cy + r * std::sin(a);
}

uint32_t traitColor(Trait t) {
    switch (t) {
        case Trait::Cultural: return theme::cultural;
        case Trait::Hazardous: return theme::hazardous;
        case Trait::Industrial: return theme::industrial;
        default: return rgb(180, 170, 150);
    }
}

uint32_t playerColor(const Game& g, int p) { return p == kNeutral ? theme::neutral : theme::player[g.players[p].color % 8]; }

float rowOf(Hex h) { return float(h.r) + float(h.q) * 0.5f; }

// The border's highlight: the player's colour, a little desaturated and lifted
// toward white (owner: "off-toned highlighted edges").
uint32_t offTone(uint32_t c) {
    int r = int((c >> 16) & 255), g = int((c >> 8) & 255), b = int(c & 255);
    int l = (r * 3 + g * 6 + b) / 10;
    r = (r * 3 + l) / 4;
    g = (g * 3 + l) / 4;
    b = (b * 3 + l) / 4;
    return mix(rgb(uint8_t(r), uint8_t(g), uint8_t(b)), rgb(255, 255, 255), 100);
}

constexpr int kGridR = 8, kGridN = 2 * kGridR + 1;
int gridIndex(Hex h) {
    if (h.q < -kGridR || h.q > kGridR || h.r < -kGridR || h.r > kGridR) return -1;
    return (h.q + kGridR) * kGridN + (h.r + kGridR);
}

bool selectable(const Game& g, int s) { return !g.systems[s].isHyperlane(); }
}  // namespace

static float levelSize(int z) { return retro::on() ? kRetroSizes[z] : kSizes[z]; }
void MapView::setZoom(int z) {
    zoom_ = std::max(0, std::min(kZoomLevels - 1, z));
    if (size_ > 0.f) {  // a smooth zoom is going on: glide to the level (around the view's centre)
        sizeTarget_ = levelSize(zoom_);
        anchorX_ = float(view_.x) + float(view_.w) / 2.f;
        anchorY_ = float(view_.y) + float(view_.h) / 2.f;
    }
}
float MapView::tileSize() const { return size_ > 0.f ? size_ : levelSize(zoom_); }

// ------------------------------------------------------------ the free camera (0.27.0)
void MapView::bounds(const Game& g, float& minx, float& maxx, float& miny, float& maxy) const {
    minx = miny = 1e9f;
    maxx = maxy = -1e9f;
    auto extend = [&](Hex h) {
        float x, y;
        hexToPixel(h, 1.f, x, y);
        minx = std::min(minx, x);
        maxx = std::max(maxx, x);
        miny = std::min(miny, y);
        maxy = std::max(maxy, y);
    };
    for (const SystemState& s : g.systems) extend(s.pos);
    if (g.draft.active)
        for (const DraftCell& d : g.draft.cells) extend(d.pos);
    if (infoTiles_)
        for (const InfoCells& ic : infoCells(g)) {
            for (int k = 0; k < ic.n; ++k) extend(ic.cell[size_t(k)]);
            if (ic.hasPlay) extend(ic.play);
        }
    if (!g.draft.active)
        for (const decor::Tile& t : decorTiles(g)) extend(t.pos);
    if (minx > maxx) minx = maxx = miny = maxy = 0;
}

void MapView::clampFocus(const Game& g) {
    float minx, maxx, miny, maxy;
    bounds(g, minx, maxx, miny, maxy);
    focusX_ = std::max(minx, std::min(maxx, focusX_));
    focusY_ = std::max(miny, std::min(maxy, focusY_));
}

void MapView::startFree(const Game& g) {
    if (free_) return;
    const float size = tileSize();
    float ox, oy;
    camera(g, size, ox, oy);
    focusX_ = (float(view_.x) + float(view_.w) / 2.f - ox) / size;
    focusY_ = (float(view_.y) + float(view_.h) / 2.f - oy) / size;
    free_ = true;
}

void MapView::pan(const Game& g, float dx, float dy) {
    if (dx == 0.f && dy == 0.f) return;
    startFree(g);
    const float size = tileSize();
    focusX_ -= dx / size;
    focusY_ -= dy / size;
    clampFocus(g);
}

void MapView::zoomBy(const Game& g, float notches, float ax, float ay) {
    if (notches == 0.f) return;
    startFree(g);
    if (size_ <= 0.f) size_ = sizeTarget_ = levelSize(zoom_);
    float k = 1.f;  // 15% a notch (touchpads: a share of that)
    const float step = notches > 0 ? 1.15f : 1.f / 1.15f;
    for (float n = std::fabs(notches); n > 0.f; n -= 1.f) k *= n >= 1.f ? step : 1.f + (step - 1.f) * n;
    const float lo = levelSize(0) * 0.8f, hi = levelSize(kZoomLevels - 1) * 1.6f;
    sizeTarget_ = std::max(lo, std::min(hi, sizeTarget_ * k));
    anchorX_ = ax;
    anchorY_ = ay;
}

void MapView::tick(const Game& g, uint32_t now) {
    const uint32_t dt = lastTick_ ? std::min<uint32_t>(100u, now - lastTick_) : 16u;
    lastTick_ = now;
    if (size_ <= 0.f || std::fabs(sizeTarget_ - size_) < 0.01f) {
        if (size_ > 0.f) size_ = sizeTarget_;
        return;
    }
    const float s0 = size_;
    const float k = std::min(1.f, float(dt) / 90.f);  // most of the way in ~0.1 s
    float s1 = s0 + (sizeTarget_ - s0) * k;
    if (std::fabs(sizeTarget_ - s1) < 0.05f) s1 = sizeTarget_;
    if (free_) {  // the point under the anchor stays put
        const float vcx = float(view_.x) + float(view_.w) / 2.f, vcy = float(view_.y) + float(view_.h) / 2.f;
        focusX_ += (anchorX_ - vcx) * (1.f / s0 - 1.f / s1);
        focusY_ += (anchorY_ - vcy) * (1.f / s0 - 1.f / s1);
        clampFocus(g);
    }
    size_ = s1;
    // the zoom level nearest the size: the detail shown follows it
    int best = 0;
    for (int z = 1; z < kZoomLevels; ++z)
        if (std::fabs(levelSize(z) - size_) < std::fabs(levelSize(best) - size_)) best = z;
    zoom_ = best;
}

static bool insideHex(float px, float py, float cx, float cy, float size) {
    const float dx = std::fabs(px - cx), dy = std::fabs(py - cy);
    return dy <= size * 0.8660254f && dx <= size - dy * 0.57735027f;
}

int MapView::hitTest(const Game& g, float x, float y) const {
    if (g.systems.empty()) return -1;
    const float size = tileSize();
    float ox, oy;
    camera(g, size, ox, oy);
    auto over = [&](Hex h) {
        float hx, hy;
        hexToPixel(h, size, hx, hy);
        return insideHex(x, y, ox + hx, oy + hy, size);
    };
    for (size_t i = 0; i < g.systems.size(); ++i) {
        const SystemState& s = g.systems[i];
        if (s.isHyperlane() && !g.draft.active) continue;
        if (over(s.pos)) return int(i);
    }
    const bool infoStops = infoTiles_ && highlights_.empty() && !g.draft.active;  // as moveCursor
    if (infoStops) {
        const auto& info = infoCells(g);
        for (size_t p = 0; p < info.size(); ++p) {
            Hex cells[4];
            const int n = infoShown(g, int(p), cells);
            for (int k = 0; k < n; ++k)
                if (over(cells[k])) return (k >= info[p].n ? kPlayBase : kInfoBase) + int(p);  // (the 4th: the play area)
        }
    }
    if (highlights_.empty() && !g.draft.active) {
        const auto& dec = decorTiles(g);
        for (size_t i = 0; i < dec.size(); ++i)
            if (over(dec[i].pos)) return kDecorBase + int(i);
    }
    return -1;
}

int MapView::hitDraftCell(const Game& g, float x, float y) const {
    if (!g.draft.active) return -1;
    const float size = tileSize();
    float ox, oy;
    camera(g, size, ox, oy);
    for (size_t i = 0; i < g.draft.cells.size(); ++i) {
        float hx, hy;
        hexToPixel(g.draft.cells[i].pos, size, hx, hy);
        if (insideHex(x, y, ox + hx, oy + hy, size)) return int(i);
    }
    return -1;
}

bool MapView::draftCellAt(const Game& g, int cell, float& x, float& y, float& size) const {
    if (!g.draft.active || cell < 0 || cell >= int(g.draft.cells.size())) return false;
    size = tileSize();
    float ox, oy, hx, hy;
    camera(g, size, ox, oy);
    hexToPixel(g.draft.cells[size_t(cell)].pos, size, hx, hy);
    x = ox + hx;
    y = oy + hy;
    return true;
}

void MapView::pointAt(const Game& g, int what) {
    if (what < 0 || what == cursor_) return;
    startFree(g);  // the camera stays where it is -
    // even for a tile only partly on screen (its centre just off the view): re-centring on it made the
    // camera jump to the edge of the board when the mouse reached the edge of the screen (0.33.1)
    setCursor(g, what, true);
}

bool MapView::visible(const Game& g, Hex h) const {
    const float size = tileSize();
    float ox, oy, hx, hy;
    camera(g, size, ox, oy);
    hexToPixel(h, size, hx, hy);
    const float x = ox + hx, y = oy + hy;
    return x >= float(view_.x) && x <= float(view_.x + view_.w) && y >= float(view_.y) && y <= float(view_.y + view_.h);
}

namespace {
// Where a tile's planets and wormholes go (offsets from its centre; size = corner radius).
// Shared by both tilesets: with size = retro::kArtR they are the art's coordinates.
void planetSpot(int np, int i, bool mecatol, bool homeSingle, float size, float& px, float& py, float& pr) {
    pr = mecatol ? size * 0.34f : (np >= 3 ? size * 0.16f : size * 0.2f);
    if (homeSingle) pr = size * 0.28f;
    px = py = 0.f;
    if (np == 2) {
        px = (i == 0 ? -0.3f : 0.3f) * size;
        py = (i == 0 ? -0.25f : 0.25f) * size;
    } else if (np >= 3) {
        const float a = -kPi / 2.f + float(i) * 2.f * kPi / 3.f;
        px = std::cos(a) * size * 0.4f;
        py = std::sin(a) * size * 0.4f;
    }
}
void wormholeSpot(bool planets, int nw, int k, float size, float& wx, float& wy, float& r) {
    r = planets ? size * 0.18f : (nw > 1 ? size * 0.2f : size * 0.3f);
    wx = planets ? size * 0.45f : (float(k) - float(nw - 1) * 0.5f) * r * 2.2f;
    wy = planets ? -size * 0.45f + float(k) * r * 2.1f : 0.f;
}
int wormholeIndex(uint8_t bit) { return bit == kAlpha ? 0 : bit == kBeta ? 1 : bit == kGamma ? 2 : bit == kDelta ? 3 : 4; }

// The Retrowave picture of a system on the board (g != nullptr) or of a printed tile.
retro::Tile retroTile(const Game* g, int sys, int def) {
    retro::Tile t;
    const SystemDef& d = kSystems[def];
    const SystemState* s = g ? &g->systems[size_t(sys)] : nullptr;
    retro::setBackground(t, def, uint32_t(def), s && sys == g->nexus && s->nexusActive);
    if (s && sys == g->teSystem && !d.anomalies) t.sky = "te";  // 0.43.1 (Voidfall): Thunder's Edge in its storm
    const float R = retro::kArtR;
    if (s && s->isHyperlane()) {
        for (int a = 0; a < 6; ++a)
            for (int b = a + 1; b < 6; ++b)
                if ((d.lanes[((a - s->rotation) % 6 + 6) % 6] >> (((b - s->rotation) % 6 + 6) % 6)) & 1) t.lanes |= 1u << (a * 6 + b);
        return t;
    }
    if (!s && d.back == Back::Hyperlane) {
        for (int a = 0; a < 6; ++a)
            for (int b = a + 1; b < 6; ++b)
                if ((d.lanes[a] >> b) & 1) t.lanes |= 1u << (a * 6 + b);
        return t;
    }
    const uint8_t w = s ? wormholesOf(*g, sys) : d.wormholes;
    const bool planets = s ? !s->planets.empty() : d.planetCount > 0;
    int nw = 0, k = 0;
    for (uint8_t bit : {kAlpha, kBeta, kGamma, kDelta, kEpsilon})
        if (w & bit) ++nw;
    for (uint8_t bit : {kAlpha, kBeta, kGamma, kDelta, kEpsilon}) {
        if (!(w & bit)) continue;
        retro::Piece p;
        wormholeSpot(planets, nw, k, R, p.x, p.y, p.r);
        const bool storm = s && (s->tokenWormholes & bit) && sys == g->ionStorm;
        p.kind = storm ? retro::Piece::IonStorm : retro::Piece::Wormhole;
        p.id = storm ? (bit == kAlpha ? 0 : 1) : wormholeIndex(bit);
        if (storm) p.r *= 1.15f;
        t.pieces.push_back(p);
        ++k;
    }
    if (s && dimensionalTear(*g, sys)) {
        retro::Piece p;
        p.kind = retro::Piece::Tear;
        p.r = R * 0.3f;
        t.pieces.push_back(p);
    }
    const int np = s ? int(s->planets.size()) : d.planetCount;
    for (int i = 0; i < np; ++i) {
        const int pdef = s ? g->planets[size_t(s->planets[size_t(i)])].def : d.planets[i];
        retro::Piece p;
        p.kind = retro::Piece::Planet;
        p.id = pdef;
        const bool mec = !std::strcmp(kPlanets[pdef].key, "mr");
        planetSpot(np, i, mec, (s ? s->home >= 0 : d.back == Back::Home) && np == 1, R, p.x, p.y, p.r);
        p.dim = s && g->planets[size_t(s->planets[size_t(i)])].exhausted;
        t.pieces.push_back(p);
    }
    return t;
}
}  // namespace

bool MapView::highlighted(int sys) const {
    return std::find(highlights_.begin(), highlights_.end(), sys) != highlights_.end();
}

void MapView::flash(int sys, uint32_t color, uint32_t now, uint32_t ms, bool strong) {
    if (sys < 0 || ms == 0) return;
    for (Flash& f : flashes_)
        if (f.sys == sys) {
            f = Flash{sys, color, now, now + ms, strong};
            return;
        }
    flashes_.push_back(Flash{sys, color, now, now + ms, strong});
}

void MapView::arrow(int from, int to, uint32_t color, uint32_t now, uint32_t ms) {
    if (from < 0 || to < 0 || from == to || ms == 0) return;
    arrows_.push_back(Arrow{from, to, color, now, now + ms});
}

void MapView::setCursor(const Game& g, int sys, bool keepCamera) {
    if (sys >= kDecorBase) {  // a Thunder's Edge placeholder
        const auto& dec = decorTiles(g);
        if (sys - kDecorBase >= int(dec.size())) return;
        cursor_ = sys;
        rowY_ = rowOf(dec[size_t(sys - kDecorBase)].pos);
        return;
    }
    if (sys >= kPlayBase) {  // a player's play area (0.43.4)
        const auto& info = infoCells(g);
        const int p = sys - kPlayBase;
        Hex cells[4];
        if (p >= int(info.size()) || infoShown(g, p, cells) <= info[size_t(p)].n) return;
        cursor_ = sys;
        rowY_ = rowOf(info[size_t(p)].play);
        return;
    }
    if (sys >= kInfoBase) {  // a player's info tiles
        const auto& info = infoCells(g);
        const int p = sys - kInfoBase;
        if (p >= int(info.size()) || info[size_t(p)].n == 0) return;
        cursor_ = sys;
        rowY_ = rowOf(info[size_t(p)].cell[size_t(info[size_t(p)].n / 2)]);
        return;
    }
    if (sys < 0 || sys >= int(g.systems.size())) return;
    cursor_ = sys;
    rowY_ = rowOf(g.systems[sys].pos);
    // a free camera (0.27.0) stays put - unless what the game shows is out of sight
    if (free_ && !keepCamera && !visible(g, g.systems[size_t(sys)].pos)) {
        float x, y;
        hexToPixel(g.systems[size_t(sys)].pos, 1.f, x, y);
        focusX_ = x;
        focusY_ = y;
    }
}

// Three free spaces beside each home for that player's info tiles: next to the
// home, as far from Mecatol Rex as possible, joined to each other.
const std::vector<MapView::InfoCells>& MapView::infoCells(const Game& g) const {
    size_t key = size_t(g.numPlayers) * 7919u + g.systems.size() * 31u + (g.draft.active ? g.draft.cells.size() : 0u);
    for (const Player& pl : g.players) key = key * 131u + size_t(pl.homeSystem + 1);
    if (key == infoKey_ && info_.size() == size_t(g.numPlayers)) return info_;
    infoKey_ = key;
    info_.assign(size_t(g.numPlayers), InfoCells());
    std::vector<Hex> used;
    for (const SystemState& s : g.systems) used.push_back(s.pos);
    if (g.draft.active)
        for (const DraftCell& d : g.draft.cells) used.push_back(d.pos);
    auto taken = [&](Hex h) { return std::find(used.begin(), used.end(), h) != used.end(); };
    auto freeAround = [&](Hex h) {
        int n = 0;
        for (int d = 0; d < 6; ++d)
            if (!taken(hexNeighbor(h, d))) ++n;
        return n;
    };
    for (int p = 0; p < g.numPlayers; ++p) {
        const int hs = g.players[size_t(p)].homeSystem;
        if (hs < 0 || hs >= int(g.systems.size())) continue;
        const Hex home = g.systems[size_t(hs)].pos;
        InfoCells ic;
        auto best = [&](const std::vector<Hex>& from, bool wantHomeAdj) {
            Hex pick{};
            int score = -1;
            for (const Hex& f : from)
                for (int d = 0; d < 6; ++d) {
                    const Hex h = hexNeighbor(f, d);
                    if (taken(h)) continue;
                    bool dup = false;
                    for (int k = 0; k < ic.n; ++k)
                        if (ic.cell[size_t(k)] == h) dup = true;
                    if (dup) continue;
                    const bool homeAdj = hexDistance(h, home) == 1;
                    int adjChosen = 0;
                    for (int k = 0; k < ic.n; ++k)
                        if (hexDistance(h, ic.cell[size_t(k)]) == 1) ++adjChosen;
                    const int sc = hexRingOf(h) * 10 + (homeAdj && wantHomeAdj ? 25 : 0) + adjChosen * 6 + freeAround(h);
                    if (sc > score) {
                        score = sc;
                        pick = h;
                    }
                }
            if (score >= 0) {
                ic.cell[size_t(ic.n++)] = pick;
                used.push_back(pick);
            }
            return score >= 0;
        };
        if (best({home}, true)) {
            best({ic.cell[0]}, true);
            if (ic.n >= 2) best({ic.cell[0], ic.cell[1]}, true);
        }
        // top to bottom on screen: tokens, faction, resources (as in the owner's picture)
        for (int a = 0; a < ic.n; ++a)
            for (int b = a + 1; b < ic.n; ++b)
                if (rowOf(ic.cell[size_t(b)]) < rowOf(ic.cell[size_t(a)])) std::swap(ic.cell[size_t(a)], ic.cell[size_t(b)]);
        info_[size_t(p)] = ic;
    }
    // the play areas (0.43.0): one more free space touching each trio, outward - chosen after every trio so the
    // trios stay where they were
    for (int p = 0; p < g.numPlayers; ++p) {
        InfoCells& ic = info_[size_t(p)];
        if (ic.n < 3) continue;
        int score = -1;
        Hex pick{};
        for (int k = 0; k < ic.n; ++k)
            for (int d = 0; d < 6; ++d) {
                const Hex h = hexNeighbor(ic.cell[size_t(k)], d);
                if (taken(h)) continue;
                int adj = 0;
                for (int m = 0; m < ic.n; ++m) adj += hexDistance(h, ic.cell[size_t(m)]) == 1 ? 1 : 0;
                const int sc = adj * 12 + hexRingOf(h) * 10 + freeAround(h);
                if (sc > score) score = sc, pick = h;
            }
        if (score >= 0) {
            ic.play = pick;
            ic.hasPlay = true;
            used.push_back(pick);
        }
    }
    return info_;
}

int MapView::infoShown(const Game& g, int p, Hex out[4]) const {
    const auto& info = infoCells(g);
    if (p < 0 || p >= int(info.size())) return 0;
    const InfoCells& ic = info[size_t(p)];
    int n = 0;
    for (int k = 0; k < ic.n; ++k) out[n++] = ic.cell[size_t(k)];
    if (ic.hasPlay && !playarea::of(g, p, viewer_).empty()) out[n++] = ic.play;
    return n;
}

const std::vector<decor::Tile>& MapView::decorTiles(const Game& g) const {
    const auto& info = infoCells(g);
    size_t key = infoKey_ * 31u + 7u;
    key = key * 977u + size_t(g.fractureIn ? 1 : 0) * 13u + size_t(g.teSystem + 2) * 7u + size_t(g.modules) +
          size_t(g.fracturePos[3].q + 64) * 3u + size_t(g.tePos.r + 64);  // (Thunder's Edge: where they wait / in play)
    if (g.nexus >= 0) key = key * 131u + size_t(g.systems[size_t(g.nexus)].pos.q + 64) * 257u + size_t(g.systems[size_t(g.nexus)].pos.r + 64);
    if (key == decorKey_ && !decor_.empty()) return decor_;
    decorKey_ = key;
    std::vector<Hex> taken;
    for (const SystemState& s : g.systems) taken.push_back(s.pos);
    if (g.draft.active)
        for (const DraftCell& d : g.draft.cells) taken.push_back(d.pos);
    for (const InfoCells& ic : info) {
        for (int k = 0; k < ic.n; ++k) taken.push_back(ic.cell[size_t(k)]);
        if (ic.hasPlay) taken.push_back(ic.play);
    }
    decor_ = decor::place(g, taken);
    return decor_;
}

bool MapView::moveCursor(const Game& g, int dir) {
    if (g.systems.empty()) return false;
    free_ = false;  // the D-pad: the camera follows the cursor again
    const bool infoStops = infoTiles_ && highlights_.empty() && !g.draft.active;  // only while looking around
    const bool decorStops = highlights_.empty() && !g.draft.active;
    const auto& info = infoCells(g);
    const auto& dec = decorTiles(g);
    Hex cur;
    if (cursor_ >= kDecorBase) {
        const int k = cursor_ - kDecorBase;
        if (k >= int(dec.size())) {
            cursor_ = 0;
            return false;
        }
        cur = dec[size_t(k)].pos;
    } else if (cursor_ >= kPlayBase) {
        const int p = cursor_ - kPlayBase;
        if (p >= int(info.size()) || !info[size_t(p)].hasPlay) {
            cursor_ = 0;
            return false;
        }
        cur = info[size_t(p)].play;
    } else if (cursor_ >= kInfoBase) {
        const int p = cursor_ - kInfoBase;
        if (p >= int(info.size()) || info[size_t(p)].n == 0) {
            cursor_ = 0;
            return false;
        }
        cur = info[size_t(p)].cell[size_t(info[size_t(p)].n / 2)];
    } else {
        cur = g.systems[size_t(std::max(0, std::min(cursor_, int(g.systems.size()) - 1)))].pos;
    }
    // On a trio, its play area (0.43.4) is one press away in the direction it touches any of the three tiles
    if (infoStops && cursor_ >= kInfoBase && cursor_ < kPlayBase) {
        const int p = cursor_ - kInfoBase;
        const InfoCells& ic = info[size_t(p)];
        Hex cells[4];
        if (ic.hasPlay && infoShown(g, p, cells) > ic.n) {
            const int da = dir == 0 ? 0 : dir == 1 ? 1 : dir == 2 ? 3 : 5, db = dir == 0 ? 0 : dir == 1 ? 2 : dir == 2 ? 3 : 4;
            for (int k = 0; k < ic.n; ++k)
                if (hexNeighbor(ic.cell[size_t(k)], da) == ic.play || hexNeighbor(ic.cell[size_t(k)], db) == ic.play) {
                    cursor_ = kPlayBase + p;
                    rowY_ = rowOf(ic.play);
                    return true;
                }
        }
    }
    // Walk in the chosen direction, skipping hyperlanes and gaps (up to 3 steps).
    auto find = [&](Hex h) -> int {
        for (size_t i = 0; i < g.systems.size(); ++i)
            if (g.systems[i].pos == h && selectable(g, int(i))) return int(i);
        if (infoStops)
            for (size_t p = 0; p < info.size(); ++p) {
                for (int k = 0; k < info[p].n; ++k)
                    if (info[p].cell[size_t(k)] == h && kInfoBase + int(p) != cursor_) return kInfoBase + int(p);
                if (info[p].hasPlay && info[p].play == h && kPlayBase + int(p) != cursor_) {  // the play area, while it has cards
                    Hex cells[4];
                    if (infoShown(g, int(p), cells) > info[p].n) return kPlayBase + int(p);
                }
            }
        if (decorStops)
            for (size_t k = 0; k < dec.size(); ++k)
                if (dec[k].pos == h) return kDecorBase + int(k);
        return -1;
    };
    if (dir == 0 || dir == 2) {
        Hex h = cur;
        for (int step = 0; step < 3; ++step) {
            h = hexNeighbor(h, dir == 0 ? 0 : 3);
            int s = find(h);
            if (s >= 0) {
                cursor_ = s;
                rowY_ = rowOf(h);
                return true;
            }
        }
        return false;
    }
    int d1 = dir == 1 ? 1 : 5, d2 = dir == 1 ? 2 : 4;
    // try the two diagonal neighbours first, then further out
    for (int step = 1; step <= 3; ++step) {
        int best = -1;
        float bestErr = 1e9f;
        for (int a = 0; a <= step; ++a) {
            Hex h = cur;
            for (int k = 0; k < a; ++k) h = hexNeighbor(h, d1);
            for (int k = 0; k < step - a; ++k) h = hexNeighbor(h, d2);
            int s = find(h);
            if (s < 0) continue;
            float err = std::fabs(rowOf(h) - rowY_);
            if (err < bestErr - 0.01f || (std::fabs(err - bestErr) < 0.01f && best >= 0 && best < int(g.systems.size()) && rowOf(h) > rowOf(g.systems[size_t(best)].pos))) {
                bestErr = err;
                best = s;
            }
        }
        if (best >= 0) {
            cursor_ = best;
            return true;
        }
    }
    return false;
}

void MapView::camera(const Game& g, float size, float& ox, float& oy) const {
    if (free_) {  // the free camera (0.27.0)
        ox = float(view_.x) + float(view_.w) / 2.f - focusX_ * size;
        oy = float(view_.y) + float(view_.h) / 2.f - focusY_ * size;
        return;
    }
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    auto extend = [&](Hex h) {
        float x, y;
        hexToPixel(h, size, x, y);
        minx = std::min(minx, x - size);
        maxx = std::max(maxx, x + size);
        miny = std::min(miny, y - size * 0.8660254f);
        maxy = std::max(maxy, y + size * 0.8660254f);
    };
    for (const SystemState& s : g.systems) extend(s.pos);
    if (g.draft.active)
        for (const DraftCell& d : g.draft.cells) extend(d.pos);
    if (infoTiles_)
        for (const InfoCells& ic : infoCells(g))
            for (int k = 0; k < ic.n; ++k) extend(ic.cell[size_t(k)]);
    if (!g.draft.active)
        for (const decor::Tile& t : decorTiles(g)) extend(t.pos);
    const float pad = 6.f;
    // centre a map that fits the viewport; otherwise follow the cursor in the viewport, but let
    // the map run on to the far edge of the draw area (under the see-through panels)
    // (the tile at the cursor itself always stays inside the viewport, clear of the panels)
    auto axis = [&](float lo, float hi, float viewLo, float viewLen, float areaHi, float focus, float half) {
        if (hi - lo + 2 * pad <= viewLen) return viewLo + (viewLen - (hi - lo)) / 2.f - lo;
        float o = viewLo + viewLen / 2.f - focus;
        o = std::min(o, viewLo + pad - lo);
        o = std::max(o, areaHi - pad - hi);
        o = std::min(o, viewLo + viewLen - pad - (focus + half));
        o = std::max(o, viewLo + pad - (focus - half));
        return o;
    };
    float cx = 0, cy = 0;
    if (cursor_ >= 0 && cursor_ < int(g.systems.size())) hexToPixel(g.systems[cursor_].pos, size, cx, cy);
    if (cursor_ >= kInfoBase && cursor_ < kPlayBase) {
        const auto& info = infoCells(g);
        const int p = cursor_ - kInfoBase;
        if (p < int(info.size()) && info[size_t(p)].n) hexToPixel(info[size_t(p)].cell[size_t(info[size_t(p)].n / 2)], size, cx, cy);
    }
    if (const int p = playPlayer(); p >= 0) {
        const auto& info = infoCells(g);
        if (p < int(info.size()) && info[size_t(p)].hasPlay) hexToPixel(info[size_t(p)].play, size, cx, cy);
    }
    if (const decor::Tile* t = decorAt(g, cursor_)) hexToPixel(t->pos, size, cx, cy);
    if (g.draft.active && draftCell_ >= 0 && draftCell_ < int(g.draft.cells.size()))
        hexToPixel(g.draft.cells[size_t(draftCell_)].pos, size, cx, cy);
    const Rect a = drawArea();
    ox = axis(minx, maxx, float(view_.x), float(view_.w), float(std::max(a.x + a.w, view_.x + view_.w)), cx, size);
    oy = axis(miny, maxy, float(view_.y), float(view_.h), float(std::max(a.y + a.h, view_.y + view_.h)), cy, size * 0.8660254f);
}

void MapView::territory(const Game& g, std::vector<int>& owner) {
    const size_t n = g.systems.size();
    owner.assign(n, -1);
    std::vector<int> grid(size_t(kGridN * kGridN), -1);
    for (size_t i = 0; i < n; ++i)
        if (!g.systems[i].offBoard) {
            int k = gridIndex(g.systems[i].pos);
            if (k >= 0) grid[size_t(k)] = int(i);
        }
    // systems with planets: whoever controls most of them
    for (size_t i = 0; i < n; ++i) {
        const SystemState& s = g.systems[i];
        if (s.isHyperlane() || s.planets.empty()) continue;
        int count[8] = {}, value[8] = {};
        for (int pl : s.planets) {
            int o = g.planets[size_t(pl)].owner;
            if (o < 0 || o >= 8) continue;
            ++count[o];
            value[o] += kPlanets[g.planets[size_t(pl)].def].res + kPlanets[g.planets[size_t(pl)].def].inf;
        }
        int best = -1;
        for (int q = 0; q < g.numPlayers && q < 8; ++q) {
            if (!count[q]) continue;
            if (best < 0 || count[q] > count[best]) {
                best = q;
                continue;
            }
            if (count[q] < count[best]) continue;
            bool sq = s.space[q].ships() > 0, sb = s.space[best].ships() > 0;  // a tie: ships there, then value
            if ((sq && !sb) || (sq == sb && value[q] > value[best])) best = q;
        }
        owner[i] = best;
    }
    // empty systems inside someone's territory
    std::vector<int> first = owner;
    for (size_t i = 0; i < n; ++i) {
        const SystemState& s = g.systems[i];
        if (s.isHyperlane() || !s.planets.empty() || s.offBoard) continue;
        int mine[8] = {}, claim = -1;
        bool contested = false;
        for (int e = 0; e < 6; ++e) {
            int k = gridIndex(hexNeighbor(s.pos, e));
            int j = k >= 0 ? grid[size_t(k)] : -1;
            int o = j >= 0 ? first[size_t(j)] : -1;
            if (o < 0 || o >= 8) continue;
            ++mine[o];
        }
        for (int q = 0; q < g.numPlayers && q < 8; ++q) {
            if (!mine[q]) continue;
            if (claim >= 0) contested = true;
            if (claim < 0 || mine[q] > mine[claim]) claim = q;
        }
        if (claim < 0 || contested || mine[claim] < 3) continue;
        bool others = false;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != claim && s.space[q].ships() > 0) others = true;
        if (!others) owner[i] = claim;
    }
}

int MapView::ownerAt(Hex h) const {
    int k = gridIndex(h);
    if (k < 0 || size_t(k) >= grid_.size()) return -1;
    int s = grid_[size_t(k)];
    return s >= 0 && size_t(s) < owner_.size() ? owner_[size_t(s)] : -1;
}

// A soft band of the owner's colour inside each edge that faces another
// player's (or nobody's) space, and a bright off-tone line on the edge.
void MapView::drawBorders(Canvas& c, const Game& g, int sys, float cx, float cy, float size) const {
    const int p = owner_[size_t(sys)];
    if (p < 0) return;
    const SystemState& s = g.systems[size_t(sys)];
    const uint32_t col = playerColor(g, p), hi = offTone(col);
    const float th = std::max(2.f, size * 0.06f), r0 = size - 1.f, bw = size * 0.22f;
    // Strips between two "radii" of the hex (corners pulled toward the centre),
    // so neighbouring edges meet in clean mitres. Plain spans: cheap on the Miyoo.
    static const float kCos[6] = {1.f, 0.5f, -0.5f, -1.f, -0.5f, 0.5f};
    static const float kSin[6] = {0.f, 0.8660254f, 0.8660254f, 0.f, -0.8660254f, -0.8660254f};
    static const int kBand[3] = {70, 36, 14};
    for (int e = 0; e < 6; ++e) {
        if (!s.offBoard && ownerAt(hexNeighbor(s.pos, e)) == p) continue;
        const int i1 = (4 + e) % 6, i2 = (5 + e) % 6;
        auto strip = [&](float ra, float rb, uint32_t colr, int a) {
            float xs[4] = {cx + ra * kCos[i1], cx + ra * kCos[i2], cx + rb * kCos[i2], cx + rb * kCos[i1]};
            float ys[4] = {cy + ra * kSin[i1], cy + ra * kSin[i2], cy + rb * kSin[i2], cy + rb * kSin[i1]};
            c.fillPolygon(xs, ys, 4, colr, a);
        };
        const float rin = r0 - th;
        for (int k = 0; k < 3; ++k) strip(rin - bw * float(k) / 3.f, rin - bw * float(k + 1) / 3.f, col, kBand[k]);
        strip(size - 1.f, rin, hi, 235);  // the bright edge line
    }
}

void MapView::draw(Canvas& c, const Game& g, uint32_t now) const {
    const float size = tileSize();
    float ox, oy;
    camera(g, size, ox, oy);
    if (borders_) {
        territory(g, owner_);
        grid_.assign(size_t(kGridN * kGridN), -1);
        for (size_t i = 0; i < g.systems.size(); ++i)
            if (!g.systems[i].offBoard) {
                int k = gridIndex(g.systems[i].pos);
                if (k >= 0) grid_[size_t(k)] = int(i);
            }
    }
    const Rect area = drawArea();
    c.setClip(area);
    g_mapNow = now;
    // The wormhole nexus sits on a swirling purple warp (owner, 0.5.0 feedback; 0.16.0).
    // (Retrowave: the warp is in the tile's own art.)
    if (g.nexus >= 0 && g.nexus < int(g.systems.size()) && !retro::art()) {
        float x, y;
        hexToPixel(g.systems[size_t(g.nexus)].pos, size, x, y);
        x += ox;
        y += oy;
        const float R = size * 1.7f;
        if (x + R >= area.x && x - R <= area.x + area.w && y + R >= area.y && y - R <= area.y + area.h)
            drawWarp(c, x, y, size, now, g.systems[size_t(g.nexus)].nexusActive);
    }
    for (size_t i = 0; i < g.systems.size(); ++i) {
        float x, y;
        hexToPixel(g.systems[i].pos, size, x, y);
        x += ox;
        y += oy;
        if (x + size < area.x || x - size > area.x + area.w || y + size < area.y || y - size > area.y + area.h)
            continue;
        drawSystem(c, g, int(i), x, y, size);
    }
    float pulse = 0.5f + 0.5f * std::sin(float(now % 1000) / 1000.f * 2.f * kPi);
    auto outline = [&](int s, uint32_t col, float thick, int alpha, float inset) {
        float x, y;
        hexToPixel(g.systems[s].pos, size, x, y);
        x += ox;
        y += oy;
        float xs[6], ys[6];
        hexCorners(x, y, size - inset, xs, ys);
        for (int i = 0; i < 6; ++i) c.line(xs[i], ys[i], xs[(i + 1) % 6], ys[(i + 1) % 6], col, thick, alpha);
    };
    for (int s : highlights_)
        if (s >= 0 && s < int(g.systems.size())) outline(s, theme::cyan, 2.f, 120 + int(pulse * 100), 3.f);
    if (active_ >= 0 && active_ < int(g.systems.size())) outline(active_, rgb(255, 120, 60), 2.5f, 230, 2.f);

    // flashes and movement arrows
    flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(),
                                  [&](const Flash& f) { return int32_t(now - f.until) >= 0; }),
                   flashes_.end());
    arrows_.erase(std::remove_if(arrows_.begin(), arrows_.end(),
                                 [&](const Arrow& a) { return int32_t(now - a.until) >= 0; }),
                  arrows_.end());
    for (const Flash& f : flashes_) {
        if (f.sys >= int(g.systems.size())) continue;
        uint32_t t = now - f.start;
        float x, y;
        hexToPixel(g.systems[f.sys].pos, size, x, y);
        x += ox;
        y += oy;
        float xs[6], ys[6];
        if (f.strong) {
            // blink 4 times a second
            float b = 0.5f + 0.5f * std::cos(float(t % 250) / 250.f * 2.f * kPi);
            hexCorners(x, y, size - 2.f, xs, ys);
            c.fillPolygon(xs, ys, 6, f.color, 40 + int(b * 110));
            outline(f.sys, f.color, 4.f, 150 + int(b * 105), 2.f);
        } else {
            float b = 0.5f + 0.5f * std::cos(float(t % 500) / 500.f * 2.f * kPi);
            hexCorners(x, y, size - 2.f, xs, ys);
            c.fillPolygon(xs, ys, 6, f.color, 20 + int(b * 50));
            outline(f.sys, f.color, 3.f, 140 + int(b * 100), 2.f);
        }
    }
    for (const Arrow& a : arrows_) {
        if (a.from >= int(g.systems.size()) || a.to >= int(g.systems.size())) continue;
        float x0, y0, x1, y1;
        hexToPixel(g.systems[a.from].pos, size, x0, y0);
        hexToPixel(g.systems[a.to].pos, size, x1, y1);
        x0 += ox, y0 += oy, x1 += ox, y1 += oy;
        float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.f) continue;
        dx /= len, dy /= len;
        // grows from the origin during the first 40% of its life
        float life = float(a.until - a.start);
        float grow = std::min(1.f, float(now - a.start) / (life * 0.4f + 1.f));
        float sx = x0 + dx * size * 0.35f, sy = y0 + dy * size * 0.35f;
        float ex = sx + (x1 - dx * size * 0.45f - sx) * grow, ey = sy + (y1 - dy * size * 0.45f - sy) * grow;
        float th = std::max(2.f, size * 0.08f);
        c.line(sx, sy, ex, ey, rgb(0, 0, 0), th + 2.f, 140);
        c.line(sx, sy, ex, ey, a.color, th, 230);
        float hs = std::max(6.f, size * 0.22f);
        float hx[3] = {ex + dx * hs * 0.6f, ex - dx * hs * 0.5f - dy * hs * 0.55f, ex - dx * hs * 0.5f + dy * hs * 0.55f};
        float hy[3] = {ey + dy * hs * 0.6f, ey - dy * hs * 0.5f + dx * hs * 0.55f, ey - dy * hs * 0.5f - dx * hs * 0.55f};
        c.fillPolygon(hx, hy, 3, a.color, 240);
    }
    // Waypoint Mode (0.56.0): the routes being planned - a line from stop to stop, arrowheads, numbered stops, a red
    // ring where leaving rolls for a gravity rift
    for (const RouteLine& rl : routes_) {
        std::vector<int> pts{rl.from};
        pts.insert(pts.end(), rl.stops.begin(), rl.stops.end());
        if (rl.to >= 0) pts.push_back(rl.to);
        bool okPts = true;
        for (int s : pts) okPts = okPts && s >= 0 && s < int(g.systems.size());
        if (!okPts) continue;
        auto centre = [&](int s, float& x, float& y) {
            hexToPixel(g.systems[size_t(s)].pos, size, x, y);
            x += ox;
            y += oy;
        };
        const float th = std::max(2.f, size * (rl.editing ? 0.09f : 0.07f));
        const int alpha = rl.editing ? 245 : 190;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            float x0, y0, x1, y1;
            centre(pts[i], x0, y0);
            centre(pts[i + 1], x1, y1);
            float dx = x1 - x0, dy = y1 - y0;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1.f) continue;
            dx /= len, dy /= len;
            const float sx = x0 + dx * size * 0.22f, sy = y0 + dy * size * 0.22f;
            const float ex = x1 - dx * size * 0.3f, ey = y1 - dy * size * 0.3f;
            c.line(sx, sy, ex, ey, rgb(0, 0, 0), th + 2.f, 150);
            c.line(sx, sy, ex, ey, rl.color, th, alpha);
            const float hs = std::max(6.f, size * 0.2f);
            float hx[3] = {ex + dx * hs * 0.6f, ex - dx * hs * 0.5f - dy * hs * 0.55f, ex - dx * hs * 0.5f + dy * hs * 0.55f};
            float hy[3] = {ey + dy * hs * 0.6f, ey - dy * hs * 0.5f + dx * hs * 0.55f, ey - dy * hs * 0.5f - dx * hs * 0.55f};
            c.fillPolygon(hx, hy, 3, rl.color, alpha);
        }
        for (size_t i = 0; i < pts.size(); ++i) {
            if (i < rl.roll.size() && rl.roll[i]) {  // leaving here rolls a die
                float x, y;
                centre(pts[i], x, y);
                const float rr = size * 0.5f;
                for (int k = 0; k < 24; ++k) {
                    const float a0 = float(k) / 24.f * 2.f * kPi, a1 = float(k + 1) / 24.f * 2.f * kPi;
                    if (k % 2) continue;  // (dashed)
                    c.line(x + std::cos(a0) * rr, y + std::sin(a0) * rr, x + std::cos(a1) * rr, y + std::sin(a1) * rr, theme::red, 2.5f, 230);
                }
            }
            if (i == 0 || (rl.to >= 0 && i + 1 == pts.size())) continue;
            float x, y;
            centre(pts[i], x, y);
            const float r = std::max(7.f, size * 0.17f);
            c.fillCircle(x, y, r + 1.5f, rgb(0, 0, 0), 200);
            c.fillCircle(x, y, r, rl.color, 240);
            const std::string n = std::to_string(i);
            const FontData& f = size >= 48 ? FONT_SMB : FONT_XS;
            drawTextAligned(c, f, int(x - r), int(y) - f.lineHeight / 2, int(2 * r), Align::Center, n, rgb(10, 12, 20));
        }
        if (rl.editing && rl.to < 0) {  // the end being added to pulses
            float x, y;
            centre(pts.back(), x, y);
            const float rr = size * (0.3f + 0.08f * pulse);
            for (int k = 0; k < 24; ++k) {
                const float a0 = float(k) / 24.f * 2.f * kPi, a1 = float(k + 1) / 24.f * 2.f * kPi;
                c.line(x + std::cos(a0) * rr, y + std::sin(a0) * rr, x + std::cos(a1) * rr, y + std::sin(a1) * rr, rl.color, 2.f, 200);
            }
        }
    }
    if (g.draft.active) drawDraft(c, g, ox, oy, size, now);
    if (infoTiles_ && !g.draft.active) drawInfoTiles(c, g, ox, oy, size, now);
    if (!g.draft.active) {  // Thunder's Edge placeholders (0.24.0)
        const auto& dec = decorTiles(g);
        for (size_t k = 0; k < dec.size(); ++k) {
            float x, y;
            hexToPixel(dec[k].pos, size, x, y);
            x += ox;
            y += oy;
            if (x + size < area.x || x - size > area.x + area.w || y + size < area.y || y - size > area.y + area.h) continue;
            decor::paint(c, dec[k], x, y, size - 1.f, now, false);
            if (cursor_ == kDecorBase + int(k)) {
                float xs[6], ys[6];
                hexCorners(x, y, size - 1.5f, xs, ys);
                for (int e = 0; e < 6; ++e) c.line(xs[e], ys[e], xs[(e + 1) % 6], ys[(e + 1) % 6], theme::gold, 3.f, 160 + int(pulse * 95));
            }
        }
    }
    if (cursor_ >= 0 && cursor_ < int(g.systems.size()) && !(g.draft.active && draftCell_ >= 0))
        outline(cursor_, highlighted(cursor_) ? rgb(255, 255, 255) : theme::gold, 3.f, 160 + int(pulse * 95), 1.5f);
    if (cursor_ >= kInfoBase && cursor_ < kDecorBase && infoTiles_) {  // one highlight around the three info tiles
        const auto& info = infoCells(g);
        const bool play = playPlayer() >= 0;  // (0.43.4: or round the play area alone)
        const int p = infoPlayer();
        Hex cells[4];
        int nc = p < int(info.size()) ? infoShown(g, p, cells) : 0;
        if (p < int(info.size())) {
            if (play && nc > info[size_t(p)].n) cells[0] = info[size_t(p)].play, nc = 1;
            else nc = std::min(nc, info[size_t(p)].n);
        }
        if (p < int(info.size()))
            for (int k = 0; k < nc; ++k) {
                float x, y;
                hexToPixel(cells[k], size, x, y);
                float xs[6], ys[6];
                hexCorners(x + ox, y + oy, size - 1.5f, xs, ys);
                for (int e = 0; e < 6; ++e) {  // skip the edges shared with the other tiles
                    const Hex nb = hexNeighbor(cells[k], (e + 2) % 6);
                    bool inner = false;
                    for (int m = 0; m < nc; ++m)
                        if (cells[m] == nb) inner = true;
                    if (!inner) c.line(xs[e], ys[e], xs[(e + 1) % 6], ys[(e + 1) % 6], theme::gold, 3.f, 160 + int(pulse * 95));
                }
            }
    }
    c.resetClip();
}

// The faction info tiles (0.20.0): drawn like map tiles in the player's colour.
// Top: command tokens (tactic / fleet / strategy). Middle: the faction, its
// victory points, strategy cards and secret objectives. Bottom: trade goods,
// commodities and action cards. Fewer details when zoomed out.
void MapView::drawInfoTiles(Canvas& c, const Game& g, float ox, float oy, float size, uint32_t now) const {
    const auto& info = infoCells(g);
    for (int p = 0; p < g.numPlayers && p < int(info.size()); ++p) {
        const InfoCells& ic = info[size_t(p)];
        if (ic.n == 0) continue;
        const Player& pl = g.players[size_t(p)];
        const uint32_t col = playerColor(g, p);
        const int alpha = pl.eliminated ? 110 : 255;
        if (ic.hasPlay) {  // the play area (0.43.0): the cards this player has in play
            const std::vector<playarea::Item> items = playarea::of(g, p, viewer_);
            if (!items.empty()) {
                float x, y;
                hexToPixel(ic.play, size, x, y);
                x += ox;
                y += oy;
                float xs[6], ys[6];
                hexCorners(x, y, size - 1.f, xs, ys);
                c.fillPolygon(xs, ys, 6, shade(col, 0.35f), alpha * 200 / 255);
                hexCorners(x, y, size - 3.f, xs, ys);
                c.fillPolygon(xs, ys, 6, rgb(18, 14, 30), alpha * 235 / 255);
                if (size >= 50) drawTextAligned(c, FONT_XS, int(x - size), int(y - size * 0.86f) + 2, int(2 * size), Align::Center, "IN PLAY", theme::faint, alpha);
                playarea::drawCell(c, items, x, y + (size >= 50 ? size * 0.06f : 0.f), size, alpha, now);
            }
        }
        for (int k = 0; k < ic.n; ++k) {
            float x, y;
            hexToPixel(ic.cell[size_t(k)], size, x, y);
            x += ox;
            y += oy;
            float xs[6], ys[6];
            hexCorners(x, y, size - 1.f, xs, ys);
            c.fillPolygon(xs, ys, 6, shade(col, 0.35f), alpha * 200 / 255);
            hexCorners(x, y, size - 3.f, xs, ys);
            c.fillPolygon(xs, ys, 6, rgb(12, 16, 30), alpha * 235 / 255);
            const int role = ic.n == 3 ? k : k == 0 ? 1 : k == 1 ? 2 : 0;  // 0 tokens, 1 faction, 2 resources
            const FontData& f = size >= 50 ? FONT_SMB : FONT_XS;
            if (role == 0) {  // command tokens
                const int v[3] = {pl.tactic, pl.fleet, pl.strategy};
                if (size >= 34) {
                    const int tw = int(size * 0.42f);
                    for (int t = 0; t < 3; ++t) {
                        const float ty = y + (float(t) - 1.f) * size * 0.5f;
                        cards::commandToken(c, pl.faction, col, int(x + size * 0.12f), int(ty), tw, alpha, t == 1);
                        drawTextAligned(c, f, int(x - size * 0.75f), int(ty) - f.lineHeight / 2, int(size * 0.55f), Align::Right,
                                        std::to_string(v[t]), theme::text, alpha);
                    }
                } else {
                    drawTextAligned(c, FONT_XS, int(x - size), int(y) - 6, int(2 * size), Align::Center,
                                    std::to_string(v[0]) + "/" + std::to_string(v[1]) + "/" + std::to_string(v[2]), theme::text, alpha);
                }
            } else if (role == 1) {  // the faction
                const int is = int(size * (size >= 50 ? 0.62f : 0.9f));
                drawFactionIcon(c, pl.faction, int(x) - is / 2, int(y - size * (size >= 50 ? 0.62f : 0.45f)), is, alpha);
                if (size >= 34) {
                    drawTextAligned(c, f, int(x - size), int(y + size * 0.05f), int(2 * size), Align::Center,
                                    std::to_string(pl.vp) + "/" + std::to_string(g.vpGoal) + " VP", theme::gold, alpha);
                }
                if (size >= 50) {
                    // strategy cards (mini) and secret objectives (unscored, face down; scored ones count as VP above)
                    const int cw = int(size * 0.34f), chh = cards::strategyHeight(cw);
                    int sx = int(x) - (int(pl.cards.size()) * (cw + 2)) / 2;
                    for (int8_t cd : pl.cards) {
                        cards::strategy(c, cd, sx, int(y + size * 0.3f), cw, ((pl.cardsUsed >> cd) & 1) ? alpha * 2 / 5 : alpha);
                        sx += cw + 2;
                    }
                    const int secrets = int(pl.secrets.size());
                    if (secrets) {  // beside the faction icon, the count under it
                        const int ss = int(size * 0.2f), qy = int(y - size * 0.52f);
                        int qx = int(x + size * 0.47f) - ss / 2;
                        if (!icons::draw(c, "secret", qx, qy, ss, alpha))
                            c.fillCircle(float(qx + ss / 2), float(qy) + float(ss) / 2.f, float(ss) / 2.f, rgb(200, 60, 60), alpha);
                        drawTextAligned(c, FONT_XS, qx - 4, qy + ss, ss + 8, Align::Center, std::to_string(secrets), theme::text, alpha);
                    }
                    (void)chh;
                }
            } else {  // trade goods, commodities, action cards
                const std::string tg = std::to_string(pl.tradeGoods);
                const std::string cm = std::to_string(pl.commodities) + "/" + std::to_string(commodityValue(g, pl));
                const std::string ac = std::to_string(pl.actionCards.size());
                if (size >= 34) {
                    const int is = int(size * 0.28f);
                    const float rows[3] = {-0.45f, 0.f, 0.45f};
                    const char* names[3] = {"trade_good", "commodity", "action_card"};
                    const std::string vals[3] = {tg, cm, ac};
                    for (int t = 0; t < 3; ++t) {
                        const int ry = int(y + rows[t] * size);
                        if (!icons::draw(c, names[t], int(x - size * 0.45f) - is / 2, ry - is / 2, is, alpha, t == 2 ? rgb(240, 160, 140) : theme::text))
                            c.fillCircle(x - size * 0.45f, float(ry), float(is) / 3.f, t == 0 ? theme::gold : theme::dim, alpha);
                        drawText(c, f, int(x - size * 0.2f), ry - f.lineHeight / 2, vals[t], t == 0 ? theme::gold : theme::text, alpha);
                    }
                } else {
                    drawTextAligned(c, FONT_XS, int(x - size), int(y) - 6, int(2 * size), Align::Center, tg + "tg", theme::gold, alpha);
                }
            }
        }
    }
}

// A warp vortex: a soft violet glow and four spiral arms of dots turning
// slowly (faster and brighter once the nexus is active). Cheap enough to
// draw every frame on the Miyoo: a few big circles and ~70 small ones.
void MapView::drawWarp(Canvas& c, float cx, float cy, float size, uint32_t now, bool active) {
    const float R = size * 1.8f;
    // the glow is the same every frame: one premultiplied image per zoom level, blitted
    static std::map<int, Image> glowCache;
    const int ir = int(c.fx(R));  // real pixels (0.27.0)
    Image& glow = glowCache[ir];
    if (glow.empty()) {
        glow.w = glow.h = 2 * ir + 1;
        glow.px.assign(size_t(glow.w) * glow.h, 0);
        for (int y = 0; y < glow.h; ++y)
            for (int x = 0; x < glow.w; ++x) {
                const float d = std::sqrt(float((x - ir) * (x - ir) + (y - ir) * (y - ir))) / float(ir);
                if (d >= 1.f) continue;
                const float k = (1.f - d) * (1.f - d);
                const uint32_t col = mix(rgb(50, 12, 90), rgb(205, 130, 250), int(k * 255));
                const uint32_t a = uint32_t(30 + 170 * k) * uint32_t(d < 0.97f ? 255 : int((1.f - d) / 0.03f * 255)) / 255;
                const uint32_t r = ((col >> 16) & 255) * a / 255, gg = ((col >> 8) & 255) * a / 255, b = (col & 255) * a / 255;
                glow.px[size_t(y) * glow.w + x] = (a << 24) | (r << 16) | (gg << 8) | b;
            }
    }
    c.pxBlit(glow, int(c.fX(cx)) - ir, int(c.fY(cy)) - ir, active ? 255 : 200);
    const float spin = float(now % 100000u) * (active ? 0.0016f : 0.0009f);
    const int arms = 4, dots = 17;
    for (int arm = 0; arm < arms; ++arm)
        for (int i = 0; i < dots; ++i) {
            const float t = float(i) / float(dots - 1);
            const float r = R * (0.12f + 0.88f * t);
            const float ang = spin + arm * (2.f * kPi / arms) + t * 2.8f;
            const float dr = std::max(1.f, size * (0.085f * (1.f - t) + 0.025f));
            const uint32_t col = mix(rgb(245, 190, 255), rgb(120, 50, 200), int(t * 255));
            c.fillCircle(cx + std::cos(ang) * r, cy + std::sin(ang) * r * 0.92f, dr, col, int((active ? 230 : 190) * (1.f - t) + 30));
        }
    c.fillCircle(cx, cy, size * 0.22f, rgb(250, 220, 255), active ? 200 : 140);
}

// The open spaces of the galaxy draft, the spaces the tile may go to, and the
// chosen tile shown where it would go.
void MapView::drawDraft(Canvas& c, const Game& g, float ox, float oy, float size, uint32_t now) const {
    const float pulse = 0.5f + 0.5f * std::sin(float(now % 1000) / 1000.f * 2.f * kPi);
    auto at = [&](int cell, float& x, float& y) {
        hexToPixel(g.draft.cells[size_t(cell)].pos, size, x, y);
        x += ox;
        y += oy;
    };
    auto ring = [&](float x, float y, uint32_t col, float th, int a, float inset) {
        float xs[6], ys[6];
        hexCorners(x, y, size - inset, xs, ys);
        for (int i = 0; i < 6; ++i) c.line(xs[i], ys[i], xs[(i + 1) % 6], ys[(i + 1) % 6], col, th, a);
    };
    for (size_t i = 0; i < g.draft.cells.size(); ++i) {
        if (g.draft.cells[i].system >= 0) continue;
        float x, y, xs[6], ys[6];
        at(int(i), x, y);
        hexCorners(x, y, size - 2.f, xs, ys);
        c.fillPolygon(xs, ys, 6, rgb(10, 14, 26), 200);
        ring(x, y, rgb(70, 90, 130), 1.f, 150, 2.f);
        if (size >= 34)
            drawTextAligned(c, FONT_XS, int(x - size), int(y) - 6, int(2 * size), Align::Center, std::to_string(g.draft.cells[i].ring),
                            rgb(70, 90, 130));
    }
    for (int s : draftSpots_) {
        if (s < 0 || s >= int(g.draft.cells.size()) || g.draft.cells[size_t(s)].system >= 0) continue;
        float x, y;
        at(s, x, y);
        ring(x, y, theme::cyan, 2.f, 110 + int(pulse * 110), 3.f);
    }
    if (draftCell_ >= 0 && draftCell_ < int(g.draft.cells.size()) && g.draft.cells[size_t(draftCell_)].system < 0) {
        float x, y;
        at(draftCell_, x, y);
        if (draftDef_ >= 0) drawTile(c, draftDef_, x, y, size);
        ring(x, y, theme::gold, 3.f, 170 + int(pulse * 85), 1.5f);
    }
}

// The legendary planet mark: the legendary icon on a dark disc (a gold star
// without the icon pack), centred at (x, y), scaled to the tile.
static void drawLegendaryMark(Canvas& c, float x, float y, float size) {
    const float r = std::max(4.f, size * 0.13f);
    c.fillCircle(x, y, r + 1.f, rgb(10, 12, 20), 230);
    c.ring(x, y, r + 1.f, std::max(1.f, size * 0.025f), theme::gold);
    const int is = int(r * 1.7f);
    if (!icons::draw(c, "legendary_white", int(x) - is / 2, int(y) - is / 2, is, 255, theme::gold)) {
        float xs[10], ys[10];
        for (int i = 0; i < 10; ++i) {
            const float a = kPi / 5.f * float(i) - kPi / 2.f, rr = (i % 2) ? r * 0.4f : r * 0.85f;
            xs[i] = x + rr * std::cos(a);
            ys[i] = y + rr * std::sin(a);
        }
        c.fillPolygon(xs, ys, 10, theme::gold);
    }
}

// Thunder's Edge (0.34.0) in the Simplistic look: a planet with two traits is split in two colours
// (the second trait on the lower-right half), both technology specialties are shown, a space
// station is drawn as a station, the entropic scar as a glowing crack, epsilon like the other
// wormholes.
static const uint32_t kSkipCol[4] = {rgb(70, 140, 240), rgb(80, 200, 90), rgb(230, 70, 60), rgb(240, 210, 60)};

static void fillHalfDisc(Canvas& c, float x, float y, float r, uint32_t col, int alpha) {
    float xs[13], ys[13];
    for (int i = 0; i <= 12; ++i) {  // from the upper right, round the lower right, to the lower left
        const float a = -kPi / 4.f + kPi * float(i) / 12.f;
        xs[i] = x + r * std::cos(a);
        ys[i] = y + r * std::sin(a);
    }
    c.fillPolygon(xs, ys, 13, col, alpha);
}

static void drawPlanetDisc(Canvas& c, float px, float py, float pr, const PlanetDef& pd, uint32_t col, bool exhausted) {
    c.fillCircle(px, py, pr, exhausted ? shade(col, 0.5f) : col);
    if (pd.trait2 != Trait::None) {
        const uint32_t c2 = traitColor(pd.trait2);
        fillHalfDisc(c, px, py, pr, exhausted ? shade(c2, 0.5f) : c2, 255);
        c.line(px + pr * 0.7f, py - pr * 0.7f, px - pr * 0.7f, py + pr * 0.7f, rgb(10, 12, 20), std::max(1.f, pr * 0.08f), 150);
    }
    c.fillCircle(px - pr * 0.28f, py - pr * 0.28f, pr * 0.5f, shade(col, 1.35f), exhausted ? 40 : 90);
}

// A space station: a steel ring on three spokes round a hub, with running lights.
static void drawStation(Canvas& c, float x, float y, float r, bool dim) {
    const uint32_t steel = dim ? rgb(84, 90, 104) : rgb(168, 178, 196), light = dim ? rgb(120, 126, 140) : rgb(226, 234, 246);
    c.fillCircle(x, y, r * 0.95f, rgb(8, 10, 18), 150);
    const float th = std::max(1.5f, r * 0.24f);
    for (int i = 0; i < 3; ++i) {
        const float a = -kPi / 2.f + float(i) * 2.f * kPi / 3.f;
        c.line(x, y, x + std::cos(a) * r * 0.7f, y + std::sin(a) * r * 0.7f, steel, std::max(1.f, r * 0.12f));
    }
    c.ring(x, y, r * 0.74f, th, steel);
    c.ring(x - r * 0.04f, y - r * 0.04f, r * 0.74f, std::max(1.f, th * 0.35f), light, 120);
    c.fillCircle(x, y, r * 0.3f, steel);
    c.fillCircle(x - r * 0.06f, y - r * 0.06f, r * 0.14f, light);
    if (r >= 6.f)
        for (int i = 0; i < 6; ++i) {
            const float a = kPi / 6.f + float(i) * kPi / 3.f;
            c.fillCircle(x + std::cos(a) * r * 0.74f, y + std::sin(a) * r * 0.74f, std::max(0.8f, r * 0.06f), dim ? rgb(120, 90, 40) : rgb(255, 190, 80));
        }
}

// The entropic scar: a dark red wash with a jagged glowing crack across the tile.
// A breach (Crimson Rebellion, Thunder's Edge): a torn slit low in the tile - glowing magenta when active, grey when not.
void drawBreachMark(Canvas& c, float cx, float cy, float r, bool active, uint32_t now) {
    const float bx = cx - r * 0.42f, by = cy + r * 0.42f, w = r * 0.26f;
    const float pulse = active ? 0.6f + 0.4f * std::sin(float(now % 1600) / 1600.f * 6.2831853f) : 0.f;
    if (active) c.fillCircle(bx, by, w * 1.25f, rgb(200, 30, 120), int(60 + 50 * pulse));
    c.fillCircle(bx, by, w * 0.85f, rgb(16, 6, 14), 230);
    const uint32_t col = active ? mix(rgb(255, 80, 170), rgb(255, 220, 240), int(pulse * 255)) : rgb(120, 110, 125);
    const float xs[5] = {bx - w * 0.7f, bx - w * 0.2f, bx + w * 0.05f, bx + w * 0.3f, bx + w * 0.7f};
    const float ys[5] = {by + w * 0.35f, by - w * 0.1f, by + w * 0.2f, by - w * 0.35f, by - w * 0.05f};
    for (int i = 0; i < 4; ++i) c.line(xs[i], ys[i], xs[i + 1], ys[i + 1], col, std::max(1.5f, r * 0.05f), 240);
    c.ring(bx, by, w * 0.85f, std::max(1.f, r * 0.025f), active ? rgb(255, 120, 200) : rgb(90, 85, 95), 200);
}

// Galactic events (0.36.0): a monument (Monuments to the Ages) and Total War's commodities in a system
void drawEventMarks(Canvas& c, const Game& g, int sys, float cx, float cy, float r) {
    if (!g.events) return;
    int monument = -1, spoils = 0;
    for (int16_t x : g.systems[size_t(sys)].planets) {
        const PlanetState& ps = g.planets[size_t(x)];
        if (ps.monument >= 0) monument = std::max(monument, int(ps.monument));
        spoils += ps.spoils;
    }
    const FontData& font = r >= 60 ? FONT_SMB : FONT_XS;
    if (monument >= 0) {  // a gold obelisk, bottom right, with the commodities under it
        const float bx = cx + r * 0.42f, by = cy + r * 0.40f, h = r * 0.30f, w = r * 0.08f;
        c.fillCircle(bx, by + h * 0.5f, w * 2.2f, rgb(20, 16, 6), 170);
        for (int i = 0; i < 6; ++i) {
            const float t = float(i) / 6.f;
            c.line(bx - w * (1.f - t * 0.6f), by + h * 0.5f - h * t, bx + w * (1.f - t * 0.6f), by + h * 0.5f - h * t, rgb(230, 190, 90),
                   std::max(1.f, h / 6.f), 230);
        }
        c.line(bx, by - h * 0.5f, bx, by - h * 0.7f, rgb(255, 235, 160), std::max(1.f, w * 0.6f), 230);
        if (monument > 0) drawTextAligned(c, font, int(bx + w * 1.5f), int(by), 30, Align::Left, std::to_string(monument), rgb(255, 225, 140));
    }
    if (spoils > 0) {  // Total War: a heap of commodities, top left
        const float bx = cx - r * 0.45f, by = cy - r * 0.38f, w = r * 0.11f;
        c.fillCircle(bx, by, w * 1.3f, rgb(30, 20, 6), 180);
        c.fillCircle(bx, by, w, rgb(220, 160, 60), 230);
        c.ring(bx, by, w, std::max(1.f, r * 0.02f), rgb(255, 220, 140), 230);
        drawTextAligned(c, font, int(bx - 20), int(by - 6), 40, Align::Center, std::to_string(spoils), rgb(40, 24, 6));
    }
}

static void drawScar(Canvas& c, float cx, float cy, float size, uint32_t seed) {
    auto rnd = [&](float lo, float hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + (hi - lo) * float((seed >> 8) % 1000) / 1000.f;
    };
    c.fillCircle(cx, cy, size * 0.62f, rgb(120, 20, 40), 50);
    float px[9], py[9];
    for (int i = 0; i < 9; ++i) {
        const float t = float(i) / 8.f;
        px[i] = cx + (t - 0.5f) * size * 1.3f;
        py[i] = cy + (t - 0.5f) * size * 0.5f + (i % 2 ? 1.f : -1.f) * rnd(0.04f, 0.13f) * size;
    }
    for (int i = 0; i < 8; ++i) c.line(px[i], py[i], px[i + 1], py[i + 1], rgb(220, 40, 70), std::max(2.f, size * 0.12f), 70);
    for (int i = 0; i < 8; ++i) c.line(px[i], py[i], px[i + 1], py[i + 1], rgb(255, 120, 110), std::max(1.f, size * 0.035f), 230);
    for (int i = 2; i < 7; i += 2) {  // short side cracks
        const float a = rnd(0.f, 2.f * kPi), l = rnd(0.08f, 0.16f) * size;
        c.line(px[i], py[i], px[i] + std::cos(a) * l, py[i] + std::sin(a) * l, rgb(255, 110, 100), std::max(1.f, size * 0.02f), 170);
    }
}

// The specialties to show on a planet: the printed ones (two of a colour count twice) or, on the
// live map, the planet's current ones (attachments included).
static int skipList(const PlanetDef& pd, int mask, TechColor out[4]) {
    int n = 0;
    for (int sc = 0; sc < 4 && n < 4; ++sc)
        if ((mask >> sc) & 1) out[n++] = TechColor(sc);
    if (pd.skip2 != TechColor::None && pd.skip2 == pd.skip && n < 4) out[n++] = pd.skip2;
    return n;
}

// (mirror: on the planet's upper LEFT - 0.58.3, the right-hand planet of a three-planet system has its values on the right)
static void drawSkips(Canvas& c, const PlanetDef& pd, int mask, float px, float py, float pr, float size, bool mirror = false) {
    TechColor list[4];
    const int n = skipList(pd, mask, list);
    const float dir = mirror ? -1.f : 1.f;
    for (int k = 0; k < n; ++k) {
        if (size >= 50 && icons::available()) {
            const int is = int(size * 0.2f);
            drawSkipIcon(c, list[k], int(px + dir * (pr * 0.8f + k * is * 0.9f)) - is / 2, int(py - pr * 0.8f) - is / 2, is);
        } else {
            c.fillCircle(px + dir * (pr * 0.8f + k * size * 0.1f), py - pr * 0.8f, std::max(2.5f, size * 0.06f), kSkipCol[int(list[k]) & 3]);
        }
    }
}

// 0.58.3 (owner: "add influence and resource values next to the planet similar to how the tiles have them
// physically"): at the closest zoom, the planet's resources (yellow) over its influence (blue) on its left
// side. Returns the stack's left edge (the legendary mark goes beyond it).
// In a three-planet system the right-hand planet has them on its right (its left faces the other planet's badges)
// and its own marks mirrored (valuesRight).
constexpr float kValuesFrom = 56.f;  // (the closest zoom level: at 48-50 the tiles are too crowded)
static bool valuesRight(int np, float pxOff) { return np >= 3 && pxOff > 1.f; }
static float drawPlanetValues(Canvas& c, int res, int inf, float px, float py, float pr, float size, bool right = false) {
    const float is = std::min(18.f, std::max(12.f, size * 0.21f));
    const float off = pr + is * 0.5f + std::max(1.f, size * 0.02f);
    const float bx = right ? px + off : px - off;
    const int s = int(is + 0.5f), x = int(bx + 0.5f);
    for (int k = 0; k < 2; ++k) {
        const int y = int(py + (k ? 0.55f : -0.55f) * is + 0.5f);
        c.fillCircle(float(x), float(y), is * 0.42f, rgb(8, 10, 18), 190);  // (readable over the art tilesets)
        drawResourceIcon(c, x, y, s, k ? inf : res, FONT_XS, k == 1);
    }
    return right ? px : bx - is * 0.5f;
}
static void legendaryBeside(Canvas& c, float px, float py, float pr, float size, float valuesLeft) {
    if (valuesLeft < px) drawLegendaryMark(c, valuesLeft - std::max(4.f, size * 0.13f) - 1.f, py, size);
    else drawLegendaryMark(c, px - pr - size * 0.05f, py, size);
}

static int printedSkips(const PlanetDef& pd) {
    int m = 0;
    if (pd.skip != TechColor::None) m |= 1 << int(pd.skip);
    if (pd.skip2 != TechColor::None) m |= 1 << int(pd.skip2);
    return m;
}

static const uint8_t kWormholeBits[5] = {kAlpha, kBeta, kGamma, kDelta, kEpsilon};
static uint32_t wormholeColour(uint8_t bit) {
    return bit == kAlpha ? rgb(240, 150, 60) : bit == kBeta ? rgb(90, 200, 110) : bit == kGamma ? rgb(230, 90, 200)
         : bit == kDelta ? rgb(200, 120, 230) : rgb(235, 60, 70);
}
static const char* wormholeSymbol(uint8_t bit) {
    return bit == kAlpha ? "\xCE\xB1" : bit == kBeta ? "\xCE\xB2" : bit == kGamma ? "\xCE\xB3" : bit == kDelta ? "\xCE\xB4" : "\xCE\xB5";
}

// A tile from its printed contents: background, anomalies, wormholes, planets
// (trait colour, technology specialty, a gold ring for legendary planets).
// 0.43.0 (owner D44): the Zelian asteroid field (d36) and the Muaat's Nova Seed (81) look their own in Simplistic
static void zelianRubble(Canvas& c, int def, float cx, float cy, float size) {
    uint32_t seed = uint32_t(def * 2654435761u) ^ 0x5eedu;
    auto rnd = [&](float lo, float hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + (hi - lo) * float((seed >> 8) % 1000) / 1000.f;
    };
    c.fillCircle(cx, cy, size * 0.2f, rgb(120, 40, 20), 120);
    c.fillCircle(cx, cy, size * 0.09f, rgb(250, 140, 60), 220);
    for (int i = 0; i < 20; ++i) {
        const float a = float(i) / 20.f * 2.f * kPi + rnd(-0.12f, 0.12f), r = rnd(0.3f, 0.62f) * size;
        c.fillCircle(cx + std::cos(a) * r, cy + std::sin(a) * r * 0.9f, rnd(0.04f, 0.09f) * size, mix(rgb(120, 130, 110), rgb(150, 110, 90), int(rnd(0, 255))));
    }
}
static void novaCrown(Canvas& c, float cx, float cy, float size) {
    for (int i = 0; i < 10; ++i) {
        const float a = float(i) / 10.f * 2.f * kPi;
        c.fillCircle(cx + std::cos(a) * size * 0.5f, cy + std::sin(a) * size * 0.5f, size * 0.06f, rgb(255, 170, 60), 200);
    }
    c.fillCircle(cx, cy, size * 0.1f, rgb(255, 250, 230));
}

void MapView::drawTile(Canvas& c, int def, float cx, float cy, float size, uint32_t now) {
    if (def < 0 || def >= kSystemCount) return;
    const SystemDef& d = kSystems[def];
    if (retro::art()) {  // Retrowave / Voidfall: the tile's art, then the marks the Simplistic tile has on its planets
        retro::draw(c, retroTile(nullptr, -1, def), cx, cy, size, now ? now : g_mapNow);
        const int np = d.planetCount;
        for (int i = 0; i < np; ++i) {
            const PlanetDef& pd = kPlanets[d.planets[i]];
            float px, py, pr;
            planetSpot(np, i, !std::strcmp(pd.key, "mr"), d.back == Back::Home && np == 1, size, px, py, pr);
            const bool vr = size >= kValuesFrom && valuesRight(np, px);
            px += cx;
            py += cy;
            const float left = size >= kValuesFrom ? drawPlanetValues(c, pd.res, pd.inf, px, py, pr, size, vr) : px;
            if (pd.legendary && size >= 26) legendaryBeside(c, px, py, pr, size, left);
            if (pd.skip != TechColor::None && size >= 24) drawSkips(c, pd, printedSkips(pd), px, py, pr, size, vr);
        }
        return;
    }
    uint32_t fill = rgb(20, 28, 48);
    if (d.anomalies & kNebula) fill = rgb(44, 26, 62);
    else if (d.anomalies & kSupernova) fill = rgb(62, 24, 16);
    else if (d.anomalies & kRift) fill = rgb(14, 12, 24);
    else if (d.anomalies & kAsteroid) fill = rgb(34, 33, 36);
    else if (d.anomalies & kScar) fill = rgb(36, 12, 22);
    else if (d.planetCount == 0) fill = rgb(16, 22, 38);
    float xs[6], ys[6];
    hexCorners(cx, cy, size - 1.f, xs, ys);
    c.fillPolygon(xs, ys, 6, shade(fill, 1.9f));
    hexCorners(cx, cy, size - 2.f, xs, ys);
    c.fillPolygon(xs, ys, 6, fill);
    uint32_t seed = uint32_t(def * 2654435761u);
    auto rnd = [&](float lo, float hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + (hi - lo) * float((seed >> 8) % 1000) / 1000.f;
    };
    if (!std::strcmp(d.key, "d36")) zelianRubble(c, def, cx, cy, size);
    else if (d.anomalies & kAsteroid)
        for (int i = 0; i < 14; ++i) {
            float a = rnd(0, 2 * kPi), r = rnd(0, 0.66f) * size;
            c.fillCircle(cx + std::cos(a) * r, cy + std::sin(a) * r, rnd(0.04f, 0.08f) * size, rgb(130, 120, 100));
        }
    if (d.anomalies & kNebula)
        for (int i = 0; i < 6; ++i) {
            float a = rnd(0, 2 * kPi), r = rnd(0, 0.45f) * size;
            c.fillCircle(cx + std::cos(a) * r, cy + std::sin(a) * r, rnd(0.25f, 0.4f) * size, rgb(150, 80, 200), 40);
        }
    if (d.anomalies & kSupernova) {
        c.fillCircle(cx, cy, size * 0.6f, rgb(200, 60, 20), 60);
        c.fillCircle(cx, cy, size * 0.42f, rgb(240, 120, 30), 160);
        c.fillCircle(cx, cy, size * 0.24f, rgb(255, 220, 120));
        if (!std::strcmp(d.key, "81")) novaCrown(c, cx, cy, size);
    }
    if (d.anomalies & kRift) {
        for (int i = 0; i < 4; ++i) c.ring(cx, cy, size * (0.16f + 0.13f * i), 1.5f, rgb(90 + 30 * i, 70, 200), 200 - 35 * i);
        c.fillCircle(cx, cy, size * 0.1f, rgb(0, 0, 0));
    }
    if (d.anomalies & kScar) drawScar(c, cx, cy, size, uint32_t(def * 2246822519u));
    int nw = 0, k = 0;
    for (uint8_t bit : kWormholeBits)
        if (d.wormholes & bit) ++nw;
    for (uint8_t bit : kWormholeBits) {
        if (!(d.wormholes & bit)) continue;
        const uint32_t wc = wormholeColour(bit);
        const char* sym = wormholeSymbol(bit);
        const bool planets = d.planetCount > 0;
        const float r = planets ? size * 0.18f : (nw > 1 ? size * 0.2f : size * 0.3f);
        const float wx = planets ? cx + size * 0.45f : cx + (k - (nw - 1) * 0.5f) * r * 2.2f;
        const float wy = planets ? cy - size * 0.45f + k * r * 2.1f : cy;
        c.fillCircle(wx, wy, r, shade(wc, 0.25f));
        c.ring(wx, wy, r, std::max(1.5f, size * 0.05f), wc);
        const FontData& f = size >= 50 ? FONT_MDB : FONT_SMB;
        if (r >= 5.f) drawTextAligned(c, f, int(wx - r), int(wy - f.lineHeight / 2) - 1, int(2 * r), Align::Center, sym, wc);
        ++k;
    }
    const int np = d.planetCount;
    for (int i = 0; i < np; ++i) {
        const PlanetDef& pd = kPlanets[d.planets[i]];
        const float pr = np >= 3 ? size * 0.16f : size * 0.2f;
        float px = cx, py = cy;
        if (np == 2) {
            px += (i == 0 ? -0.3f : 0.3f) * size;
            py += (i == 0 ? -0.25f : 0.25f) * size;
        } else if (np >= 3) {
            const float a = -kPi / 2.f + float(i) * 2.f * kPi / 3.f;
            px += std::cos(a) * size * 0.4f;
            py += std::sin(a) * size * 0.4f;
        }
        const uint32_t col = traitColor(pd.trait);
        if (pd.flags & kPlanetStation) {
            drawStation(c, px, py, pr, false);
        } else {
            if (pd.legendary) c.fillCircle(px, py, pr + std::max(2.f, size * 0.07f), theme::gold);
            else c.fillCircle(px, py, pr + 1.2f, shade(col, 0.35f));
            drawPlanetDisc(c, px, py, pr, pd, col, false);
        }
        const bool vr = size >= kValuesFrom && valuesRight(np, px - cx);
        const float left = size >= kValuesFrom ? drawPlanetValues(c, pd.res, pd.inf, px, py, pr, size, vr) : px;
        if (pd.legendary && size >= 26) legendaryBeside(c, px, py, pr, size, left);
        if (pd.skip != TechColor::None && size >= 24) drawSkips(c, pd, printedSkips(pd), px, py, pr, size, vr);
    }
}

// 0.57.1 (tester): a flag on a pole planted on a planet, in its holder's colour
static void planetFlag(Canvas& c, uint32_t col, float bx, float by, float size) {
    const float h = std::max(7.f, size * 0.24f), w = h * 0.72f, fh = h * 0.46f;
    const float pole = std::max(1.f, size * 0.025f);
    c.line(bx + 0.6f, by + 0.6f, bx + 0.6f, by - h + 0.6f, rgb(10, 10, 16), pole + 1.2f, 200);  // (a dark edge)
    c.line(bx, by, bx, by - h, rgb(214, 196, 150), pole);
    const float top = by - h, wave = fh * 0.16f;
    float xs[5] = {bx, bx + w * 0.5f, bx + w, bx + w * 0.5f, bx};
    float ys[5] = {top, top - wave, top + fh * 0.5f, top + fh + wave * 0.4f, top + fh};
    float ox[5], oy[5];
    for (int i = 0; i < 5; ++i) ox[i] = xs[i] + (i ? 0.9f : 0.f), oy[i] = ys[i] + 0.9f;
    c.fillPolygon(ox, oy, 5, rgb(8, 8, 14), 190);
    c.fillPolygon(xs, ys, 5, col, 245);
    c.line(bx, top + fh * 0.32f, bx + w * 0.72f, top + fh * 0.38f, shade(col, 1.35f), std::max(1.f, size * 0.012f), 150);  // a light fold
    c.fillCircle(bx, top - pole * 0.4f, pole * 0.9f + 0.5f, rgb(240, 214, 120));
}

// a unit's picture on the map, made once per type, length and colour and then copied (as the ships along the tiles)
static void mapUnitPicture(Canvas& c, UnitType t, float x, float y, float len, uint32_t col) {
    const int block = retro::on() ? 2 : 0;
    painted::Light keep;
    keep.violet = keep.shade = keep.lift = keep.rim = keep.key = 0;
    const uint64_t key = ((((0x6d6170ull * 1099511628211ull) ^ uint64_t(t)) * 1099511628211ull ^ uint64_t(len * 4.f)) * 1099511628211ull ^
                          uint64_t(col)) * 1099511628211ull ^ uint64_t(block + 7);
    const painted::Lit* lit = painted::relight(c, key, -len, -len, 2.f * len, 2.f * len,
                                               [&](Canvas& o2, float ax, float ay) { tileview::drawMapUnit(o2, t, ax, ay, len, col, false, 0, block); },
                                               g_mapNow, keep);
    if (lit) painted::drawLit(c, *lit, x, y, 255);
    else tileview::drawMapUnit(c, t, x, y, len, col, false, g_mapNow, block);
}

void MapView::drawSystem(Canvas& c, const Game& g, int sys, float cx, float cy, float size) const {
    const SystemState& s = g.systems[sys];
    const SystemDef& d = s.d();
    float xs[6], ys[6];
    const bool art = retro::art();  // Retrowave / Voidfall: the art tile, the game's pieces on top
    if (s.isHyperlane() && art) {
        retro::draw(c, retroTile(&g, sys, s.def), cx, cy, size, g_mapNow);
        return;
    }
    if (s.isHyperlane()) {
        hexCorners(cx, cy, size - 1.f, xs, ys);
        c.fillPolygon(xs, ys, 6, rgb(12, 14, 26));
        for (int a = 0; a < 6; ++a)
            for (int b = a + 1; b < 6; ++b) {
                if (!((d.lanes[((a - s.rotation) % 6 + 6) % 6] >> (((b - s.rotation) % 6 + 6) % 6)) & 1)) continue;
                float x1, y1, x2, y2;
                edgeMid(cx, cy, size, a, x1, y1);
                edgeMid(cx, cy, size, b, x2, y2);
                c.line(x1, y1, x2, y2, rgb(90, 130, 200), std::max(1.5f, size * 0.08f), 200);
            }
        return;
    }
    uint32_t fill = rgb(20, 28, 48);
    if (!std::strcmp(d.key, "18")) fill = rgb(52, 44, 26);
    else if (s.home >= 0) fill = rgb(28, 36, 60);
    else if (d.anomalies & kNebula) fill = rgb(44, 26, 62);
    else if (d.anomalies & kSupernova) fill = rgb(62, 24, 16);
    else if (d.anomalies & kRift) fill = rgb(14, 12, 24);
    else if (d.anomalies & kAsteroid) fill = rgb(34, 33, 36);
    else if (d.anomalies & kScar) fill = rgb(36, 12, 22);
    else if (d.planetCount == 0) fill = rgb(16, 22, 38);
    if (sys == g.nexus) fill = s.nexusActive ? rgb(54, 26, 60) : rgb(30, 20, 36);
    uint32_t edge = s.home >= 0 ? playerColor(g, s.home) : shade(fill, 1.9f);
    const int fillA = sys == g.nexus ? 165 : 255;  // the nexus: its warp shows through
    const int fracNo = s.fracture ? std::atoi(d.key + 8) : 0;  // Thunder's Edge: fracture1..7 (0.34.0)
    // 0.44.1 (owner): the Fracture's and Thunder's Edge's painted (Voidfall) skies in every tileset
    // (0.46.2 - owner: "are there Simplistic tiles for the Fracture and Thunder's Edge?" - yes again: Simplistic draws its
    // own, the painted skies stay in Voidfall and Retrowave)
    const bool vfSpecial = false;
    if (vfSpecial) {
        retro::Tile t = retroTile(&g, sys, s.def);
        if (!art) t.pieces.clear();  // (Simplistic draws its own planets and wormholes on top)
        voidfall::draw(c, t, cx, cy, size, g_mapNow);
        if (fracNo) decor::fractureLook(c, fracNo, cx, cy, size - 1.f, g_mapNow, false);
    } else if (art) {
        retro::draw(c, retroTile(&g, sys, s.def), cx, cy, size, g_mapNow);
        if (fracNo) decor::fractureLook(c, fracNo, cx, cy, size - 1.f, g_mapNow, false);
        if (s.home >= 0) {  // the owner's colour round a home system
            hexCorners(cx, cy, size - 1.5f, xs, ys);
            const float th = std::max(2.f, size * 0.06f);
            for (int i = 0; i < 6; ++i) c.line(xs[i], ys[i], xs[(i + 1) % 6], ys[(i + 1) % 6], edge, th, 220);
        }
    } else if (fracNo) {
        decor::fractureLook(c, fracNo, cx, cy, size - 1.f, g_mapNow, true);
    } else {
        hexCorners(cx, cy, size - 1.f, xs, ys);
        c.fillPolygon(xs, ys, 6, edge, fillA);
        hexCorners(cx, cy, size - (s.home >= 0 ? 4.f : 2.f), xs, ys);
        c.fillPolygon(xs, ys, 6, fill, fillA);
    }
    // home systems: the owner's emblem, faint, behind the planets
    // (0.43.0, owner D132: the Ghoti's own tile - the Void, a home with no planet - always shows their emblem, clearly)
    int ghotiTile = -1;
    if (g.modules & kModDiscordant)
        for (int q = 0; q < g.numPlayers; ++q)
            if (!std::strcmp(kFactions[g.players[size_t(q)].faction].key, "ghoti")) ghotiTile = q;
    const bool ghotiVoid = ghotiTile >= 0 && s.def == kFactions[g.players[size_t(ghotiTile)].faction].homeSystem;
    if (ghotiVoid && size >= 16) {
        int is = int(size * 1.25f);
        drawFactionIcon(c, g.players[size_t(ghotiTile)].faction, int(cx) - is / 2, int(cy) - is / 2, is, art ? 120 : 150);
    } else if (s.home >= 0 && s.home < g.numPlayers && size >= 16) {
        int is = int(size * 1.25f);
        drawFactionIcon(c, g.players[s.home].faction, int(cx) - is / 2, int(cy) - is / 2, is, art ? 44 : 72);
    }
    if (borders_ && size_t(sys) < owner_.size()) drawBorders(c, g, sys, cx, cy, size);

    // anomaly decoration (deterministic per tile)
    uint32_t seed = uint32_t(s.def * 2654435761u);
    auto rnd = [&](float lo, float hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + (hi - lo) * float((seed >> 8) % 1000) / 1000.f;
    };
    const bool zelianRocks = !std::strcmp(d.key, "d36"), novaSeed = !std::strcmp(d.key, "81");  // 0.43.0 (owner D44)
    if (zelianRocks && !art) {  // the Zelian asteroid field: a cracked world's ember core, its rubble in a ring
        zelianRubble(c, s.def, cx, cy, size);
    } else if ((d.anomalies & kAsteroid) && !art)
        for (int i = 0; i < 14; ++i) {
            float a = rnd(0, 2 * kPi), r = rnd(0, 0.66f) * size;
            c.fillCircle(cx + std::cos(a) * r, cy + std::sin(a) * r, rnd(0.04f, 0.08f) * size, rgb(130, 120, 100));
        }
    if ((d.anomalies & kNebula) && !art)
        for (int i = 0; i < 6; ++i) {
            float a = rnd(0, 2 * kPi), r = rnd(0, 0.45f) * size;
            c.fillCircle(cx + std::cos(a) * r, cy + std::sin(a) * r, rnd(0.25f, 0.4f) * size, rgb(150, 80, 200), 40);
        }
    if ((d.anomalies & kSupernova) && !art) {
        c.fillCircle(cx, cy, size * 0.6f, rgb(200, 60, 20), 60);
        c.fillCircle(cx, cy, size * 0.42f, rgb(240, 120, 30), 160);
        c.fillCircle(cx, cy, size * 0.24f, rgb(255, 220, 120));
        if (novaSeed) novaCrown(c, cx, cy, size);  // the Muaat's Nova Seed: a crown of flares and a white-hot heart
    }
    if (((d.anomalies & kRift) || dimensionalTear(g, sys)) && !art) {  // Dimensional Tear: a gravity rift too
        for (int i = 0; i < 4; ++i) c.ring(cx, cy, size * (0.16f + 0.13f * i), 1.5f, rgb(90 + 30 * i, 70, 200), 200 - 35 * i);
        c.fillCircle(cx, cy, size * 0.1f, rgb(0, 0, 0));
    }
    if ((d.anomalies & kScar) && !art) drawScar(c, cx, cy, size, uint32_t(s.def * 2246822519u));
    if (s.breach) drawBreachMark(c, cx, cy, size, s.breach == 2, g_mapNow);  // Crimson Rebellion (Thunder's Edge)
    drawEventMarks(c, g, sys, cx, cy, size);  // galactic events: monuments, Total War's commodities
    // Wormholes: printed ones and tokens (gamma tokens, ion storm); the
    // wormhole nexus shows only its gamma wormhole until it activates.
    if (!art) {
        uint8_t w = wormholesOf(g, sys);
        int nw = 0;
        for (uint8_t bit : kWormholeBits)
            if (w & bit) ++nw;
        int k = 0;
        const bool planets = !s.planets.empty();
        for (uint8_t bit : kWormholeBits) {
            if (!(w & bit)) continue;
            const uint32_t wc = wormholeColour(bit);
            const char* sym = wormholeSymbol(bit);
            float r = planets ? size * 0.18f : (nw > 1 ? size * 0.2f : size * 0.3f);
            float wx = planets ? cx + size * 0.45f : cx + (k - (nw - 1) * 0.5f) * r * 2.2f;
            float wy = planets ? cy - size * 0.45f + k * r * 2.1f : cy;
            bool token = (s.tokenWormholes & bit) != 0;
            c.fillCircle(wx, wy, r, shade(wc, 0.25f));
            c.ring(wx, wy, r, std::max(1.5f, size * 0.05f), wc);
            if (token && sys == g.ionStorm) c.ring(wx, wy, r * 1.35f, 1.f, rgb(220, 220, 255), 160);
            const FontData& f = size >= 50 ? FONT_MDB : FONT_SMB;
            drawTextAligned(c, f, int(wx - r), int(wy - f.lineHeight / 2) - 1, int(2 * r), Align::Center, sym, wc);
            ++k;
        }
    }
    // Frontier token: a small dark star in systems without planets.
    if (s.frontier && size >= 24) {
        float fx = cx - size * 0.42f, fy = cy - size * 0.42f, r = std::max(3.f, size * 0.13f);
        float xs2[8], ys2[8];
        for (int i = 0; i < 8; ++i) {
            float a = kPi / 4.f * float(i) - kPi / 2.f;
            float rr = (i % 2) ? r * 0.45f : r;
            xs2[i] = fx + rr * std::cos(a);
            ys2[i] = fy + rr * std::sin(a);
        }
        c.fillPolygon(xs2, ys2, 8, rgb(150, 200, 230), 220);
    }
    // Ingress token (Thunder's Edge, 0.43.0): the way into the Fracture - every tileset
    if (s.ingress) {
        if (size >= 24) decor::drawIngress(c, cx - size * 0.34f, cy + size * 0.34f, std::max(3.5f, size * 0.155f), g_mapNow);
        else c.fillCircle(cx - size * 0.3f, cy + size * 0.3f, std::max(2.f, size * 0.12f), rgb(222, 32, 44));
    }
    // Glory token (Kjalengard, Discordant Stars): a small red-gold banner at the top right
    if (!g.dsGlory.empty() && size >= 24 && std::find(g.dsGlory.begin(), g.dsGlory.end(), int16_t(sys)) != g.dsGlory.end()) {
        const float gx = cx + size * 0.40f, gy = cy - size * 0.44f, r = std::max(3.f, size * 0.12f);
        float bx[5] = {gx - r * 0.7f, gx + r * 0.7f, gx + r * 0.7f, gx, gx - r * 0.7f};
        float by[5] = {gy - r, gy - r, gy + r, gy + r * 0.45f, gy + r};
        c.fillPolygon(bx, by, 5, rgb(170, 40, 30), 235);
        c.ring(gx, gy - r * 0.2f, r * 0.35f, std::max(1.f, size * 0.02f), theme::gold);
    }

    // planets
    const int np = int(s.planets.size());
    for (int i = 0; i < np; ++i) {
        int pl = s.planets[i];
        const PlanetState& ps = g.planets[pl];
        const PlanetDef& pd = kPlanets[ps.def];
        float pr = !std::strcmp(pd.key, "mr") ? size * 0.34f : (np >= 3 ? size * 0.16f : size * 0.2f);
        if (s.home >= 0 && np == 1) pr = size * 0.28f;
        float px = cx, py = cy;
        if (np == 2) {
            px += (i == 0 ? -0.3f : 0.3f) * size;
            py += (i == 0 ? -0.25f : 0.25f) * size;
        } else if (np >= 3) {
            float a = -kPi / 2.f + float(i) * 2.f * kPi / 3.f;
            px += std::cos(a) * size * 0.4f;
            py += std::sin(a) * size * 0.4f;
        }
        uint32_t col = !std::strcmp(pd.key, "mr") ? rgb(214, 176, 92)
                       : s.home >= 0             ? rgb(150, 160, 180)
                                                 : traitColor(pd.trait);
        const bool station = (pd.flags & kPlanetStation) != 0;
        if (art) {  // the planet is in the art: the owner's colour as a ring round it
            if (ps.owner >= 0) c.ring(px, py, pr + std::max(1.5f, size * 0.045f), std::max(1.5f, size * 0.05f), playerColor(g, ps.owner), 235);
        } else if (station) {  // a space station: its controller's colour as a ring round it
            if (ps.owner >= 0) c.ring(px, py, pr * 0.95f + std::max(1.f, size * 0.03f), std::max(1.5f, size * 0.05f), playerColor(g, ps.owner), 235);
            drawStation(c, px, py, pr, ps.exhausted);
        } else {
            if (ps.owner >= 0) c.fillCircle(px, py, pr + std::max(2.f, size * 0.07f), playerColor(g, ps.owner));
            else c.fillCircle(px, py, pr + 1.2f, shade(col, 0.35f));
            drawPlanetDisc(c, px, py, pr, pd, col, ps.exhausted);
        }
        // 0.58.3: resources / influence (with attachments) beside the planet when zoomed in; on the right-hand planet
        // of a three-planet system they go on its right and its marks swap sides (dir)
        const bool vr = size >= kValuesFrom && valuesRight(np, px - cx);
        const float dir = vr ? -1.f : 1.f;
        if (size >= 34) drawSkips(c, pd, planetSkips(g, pl), px, py, pr, size, vr);
        const float left = size >= kValuesFrom ? drawPlanetValues(c, planetResources(g, pl), planetInfluence(g, pl), px, py, pr, size, vr) : px;
        // legendary planets (owner, 0.19.0): the legendary icon on the planet's left side
        if (isLegendaryPlanet(g, pl) && size >= 26) legendaryBeside(c, px, py, pr, size, left);
        // who holds the planet (owner, RULES-CHECK X3 answer, 0.58.4: "show the flag when zoomed in, but the control
        // token when zoomed out"): zoomed out (the two farther levels) their control token on the planet; zoomed in
        // the flag (below). Units' badges are drawn over it.
        constexpr float kFlagFrom = 44.f;
        if (ps.owner >= 0 && ps.owner < g.numPlayers && size < kFlagFrom)
            cards::controlToken(c, g.players[ps.owner].faction, playerColor(g, ps.owner), int(px), int(py), std::max(12, int(size * 0.36f)));
        if (ps.sleeper) {  // Titans sleeper token: a small dark disc with a teal ring
            float sx = px - dir * pr * 0.85f, sy = py + pr * 0.75f, sr = std::max(2.5f, size * 0.08f);
            c.fillCircle(sx, sy, sr, rgb(20, 40, 44));
            c.ring(sx, sy, sr, std::max(1.f, size * 0.03f), rgb(90, 220, 200));
        }
        // Core token (Gledge, Discordant Stars): a small molten disc at the lower left
        if (!g.dsCores.empty() && std::find(g.dsCores.begin(), g.dsCores.end(), int16_t(pl)) != g.dsCores.end()) {
            const float kx = px - dir * pr * 0.85f, ky = py + pr * 0.85f, kr = std::max(2.5f, size * 0.07f);
            c.fillCircle(kx, ky, kr, rgb(230, 110, 30));
            c.ring(kx, ky, kr, std::max(1.f, size * 0.02f), rgb(60, 20, 10));
        }
        if (!ps.attachments.empty() || ps.terraform || ps.ulHero || pl == g.dsBasePlanet || pl == g.dsKeyPlanet) {  // attachment: a small gold diamond
            float ax = px - dir * pr * 0.85f, ay = py - pr * 0.85f, ar = std::max(2.5f, size * 0.07f);
            float dx[4] = {ax, ax + ar, ax, ax - ar}, dy[4] = {ay - ar, ay, ay + ar, ay};
            c.fillPolygon(dx, dy, 4, hasAttachment(g, pl, "dmz") ? rgb(230, 230, 230) : theme::gold);
        }
        // 0.57.1 (tester): who holds the planet - a flag in their colour planted on it (0.58.4: zoomed in only)
        if (ps.owner >= 0 && ps.owner < g.numPlayers && size >= kFlagFrom) planetFlag(c, playerColor(g, ps.owner), px + pr * 0.3f, py - pr * 0.78f, size);
        // ... and the mechs standing on it (from the third zoom level; the badge then counts the rest)
        const bool mechArt = size >= 44;
        int lines = 0, mechRows = 0;
        for (int q = 0; q < kSides && mechArt; ++q) {
            if (q >= g.numPlayers && q != kNeutral) continue;
            const int mechs = ps.units[q].count(UnitType::Mech);
            if (!mechs) continue;
            const float len = std::max(10.f, size * 0.26f);
            const float y = py + pr * 0.35f - float(mechRows) * len * 0.85f;
            const int shown = std::min(mechs, 3);
            for (int i = 0; i < shown; ++i)
                mapUnitPicture(c, UnitType::Mech, px - pr * 0.6f + float(i) * len * 0.62f, y, len, playerColor(g, q));
            if (mechs > shown) {
                const std::string t = "+" + std::to_string(mechs - shown);
                const float x = px - pr * 0.6f + float(shown) * len * 0.62f;
                c.fillRoundRect(int(x - 1), int(y - FONT_XS.lineHeight / 2.f), textWidth(FONT_XS, t) + 4, FONT_XS.lineHeight, 3, rgb(8, 10, 18), 210);
                drawText(c, FONT_XS, int(x) + 1, int(y - FONT_XS.lineHeight / 2.f), t, playerColor(g, q));
            }
            ++mechRows;
        }
        // ground forces and structures on the planet
        for (int q = 0; q < kSides; ++q) {  // (kNeutral: Thunder's Edge's neutral units)
            if (q >= g.numPlayers && q != kNeutral) continue;
            const Force& f = ps.units[q];
            if (f.empty()) continue;
            int gf = f.groundForces() - (mechArt ? f.count(UnitType::Mech) : 0);
            int st = f.structures();
            std::string label;
            if (gf) label += std::to_string(gf);
            if (st && size >= 34) {
                if (f.count(UnitType::SpaceDock)) label += "D";
                if (f.count(UnitType::PDS)) label += std::string(f.count(UnitType::PDS), 'P');
            }
            if (label.empty()) continue;
            const FontData& font = size >= 50 ? FONT_SMB : FONT_XS;
            int tw = textWidth(font, label);
            int bx = vr ? int(px - pr * 0.5f) - tw - 1 : int(px + pr * 0.5f), by = int(py + pr * 0.3f) + lines * (font.lineHeight + 1);
            c.fillRoundRect(bx - 2, by, tw + 5, font.lineHeight, 4, playerColor(g, q), 230);
            drawText(c, font, bx, by, label, rgb(10, 10, 10));
            ++lines;
        }
    }
    if (s.custodians) {
        float r = size * 0.12f;
        c.fillCircle(cx, cy + size * 0.42f, r, rgb(240, 220, 150));
        c.ring(cx, cy + size * 0.42f, r, 1.2f, rgb(120, 90, 30));
    }
    drawUnits(c, g, sys, cx, cy, size);

    // command tokens (board-game shape) in a row along the top edge
    int nt = 0;
    for (int q = 0; q < g.numPlayers; ++q)
        if (s.hasToken(q)) ++nt;
    if (nt) {
        int w = std::max(9, int(size * 0.3f));
        while (w > 9 && nt * (w + 1) > int(size * 1.45f)) --w;
        const int th = cards::commandTokenHeight(w);
        const float y = cy - size * 0.866f + 2.f + th / 2.f;
        float x = cx - (nt * (w + 1) - 1) / 2.f + w / 2.f;
        for (int q = 0; q < g.numPlayers; ++q) {
            if (!s.hasToken(q)) continue;
            cards::commandToken(c, g.players[q].faction, playerColor(g, q), int(std::lround(x)), int(std::lround(y)), w);
            x += float(w + 1);
        }
    }
    if (sys == g.nexus && size >= 34)
        drawTextAligned(c, FONT_XS, int(cx - size), int(cy + size * 0.55f), int(2 * size), Align::Center,
                        s.nexusActive ? "NEXUS" : "NEXUS (inactive)", rgb(220, 150, 230));
    else if (size >= 50 && s.home < 0 && std::strcmp(d.key, "18") != 0)  // (0.52.0: lower, under the ships along the bottom)
        drawText(c, FONT_XS, int(cx - size * 0.15f), int(cy + size * 0.866f) - FONT_XS.lineHeight - 1, d.key, theme::faint);
}

void MapView::drawUnits(Canvas& c, const Game& g, int sys, float cx, float cy, float size) const {
    const SystemState& s = g.systems[sys];
    std::vector<int> who;
    for (int q = 0; q < g.numPlayers; ++q)
        if (!s.space[q].empty()) who.push_back(q);
    if (!s.space[kNeutral].empty()) who.push_back(kNeutral);  // Thunder's Edge
    if (who.empty()) return;
    // 0.52.0 (owner, D117 answer: "ships drawn on the map tiles"): from the second zoom level the ships themselves, along
    // the bottom of the tile (clear of the planets in every layout) - biggest first, fighters as one with their count,
    // ground forces carried as "+N"; each player in the space area on a row of their own
    if (size >= 30.f) {
        const float L = std::max(8.f, size * 0.3f);  // a ship's length
        const float gap = L * 0.98f;
        const int block = retro::on() ? 2 : 0;
        static const UnitType kOrder[] = {UnitType::WarSun, UnitType::Flagship, UnitType::Flagship2, UnitType::Dreadnought,
                                          UnitType::Carrier, UnitType::Cruiser, UnitType::Destroyer};
        const FontData& font = FONT_XS;
        for (size_t k = 0; k < who.size() && k < 3; ++k) {
            const int q = who[k];
            const Force& f = s.space[q];
            const uint32_t col = playerColor(g, q);
            struct One {
                UnitType t;
                int n;
                bool dmg;
            };
            std::vector<One> row;
            for (UnitType t : kOrder) {
                const int n = f.count(t), dm = std::min<int>(n, f.dmg[int(t)]);
                for (int i = 0; i < n; ++i) row.push_back({t, 1, i < dm});
            }
            const int fighters = f.count(UnitType::Fighter);
            if (fighters) row.push_back({UnitType::Fighter, fighters, false});
            for (UnitType t : {UnitType::PDS, UnitType::SpaceDock})  // (Saar floating factories, Mirveda Gauss Cannons)
                if (f.count(t)) row.push_back({t, f.count(t), false});
            const int gf = f.groundForces();
            std::string extra = gf ? "+" + std::to_string(gf) : std::string();
            const float y = cy + size * (0.55f - 0.3f * float(k));
            const float half = size * (1.f - std::fabs(y - cy) / (size * 1.7320508f)) - L * 0.5f;  // inside the hex at that height
            const int fit = std::max(1, int((2.f * half - (extra.empty() ? 0.f : float(textWidth(font, extra)) + 3.f)) / gap) + 1);
            const int shown = int(row.size()) > fit ? fit - 1 : int(row.size());
            const int more = int(row.size()) - shown;
            std::string moreText = more > 0 ? "+" + std::to_string(more) : std::string();
            const float w = float(shown) * gap + (more > 0 ? float(textWidth(font, moreText)) + 3.f : 0.f) +
                            (extra.empty() ? 0.f : float(textWidth(font, extra)) + 3.f);
            float x = cx - w / 2.f + gap / 2.f;
            for (int i = 0; i < shown; ++i, x += gap) {
                const One& o = row[size_t(i)];
                const float len = o.t == UnitType::Fighter ? L * 0.62f : o.t == UnitType::Destroyer ? L * 0.8f : L;
                // each picture made once (per type, length, colour, tileset) and then copied; a damaged one drawn as is
                const painted::Lit* lit = nullptr;
                if (!o.dmg) {
                    painted::Light keep;
                    keep.violet = keep.shade = keep.lift = keep.rim = keep.key = 0;
                    const uint64_t key = ((((0x6d6170ull * 1099511628211ull) ^ uint64_t(o.t)) * 1099511628211ull ^ uint64_t(len * 4.f)) *
                                              1099511628211ull ^
                                          uint64_t(col)) * 1099511628211ull ^ uint64_t(block + 7);
                    lit = painted::relight(c, key, -len, -len, 2.f * len, 2.f * len,
                                           [&](Canvas& o2, float ax, float ay) { tileview::drawMapUnit(o2, o.t, ax, ay, len, col, false, 0, block); },
                                           g_mapNow, keep);
                }
                if (lit) painted::drawLit(c, *lit, x, y, 255);
                else tileview::drawMapUnit(c, o.t, x, y, len, col, o.dmg, g_mapNow, block);
                if (o.n > 1) {  // the count, small, under the picture
                    const std::string t = std::to_string(o.n);
                    const int tw = textWidth(FONT_XS, t);
                    c.fillRoundRect(int(x + len * 0.15f), int(y + L * 0.1f), tw + 4, FONT_XS.lineHeight, 3, rgb(8, 10, 18), 210);
                    drawText(c, FONT_XS, int(x + len * 0.15f) + 2, int(y + L * 0.1f), t, col);
                }
            }
            x -= gap / 2.f;
            for (const std::string* t : {&moreText, &extra}) {  // ships not shown, ground forces carried
                if (t->empty()) continue;
                const int tw = textWidth(font, *t);
                c.fillRoundRect(int(x) + 1, int(y - font.lineHeight / 2.f), tw + 4, font.lineHeight, 3, rgb(8, 10, 18), 210);
                drawText(c, font, int(x) + 3, int(y - font.lineHeight / 2.f), *t, col);
                x += float(tw) + 5.f;
            }
        }
        return;
    }
    const FontData& font = size >= 50 ? FONT_SMB : FONT_XS;
    float y = cy + size * 0.32f;
    float x = cx - size * 0.62f;
    for (size_t k = 0; k < who.size(); ++k) {
        int q = who[k];
        const Force& f = s.space[q];
        std::string label;
        if (size >= 70) {
            for (int t = 0; t < kUnitTypes; ++t) {
                if (!f.n[t]) continue;
                if (!label.empty()) label += " ";
                label += std::to_string(f.n[t]) + unitTypeShort(UnitType(t));
            }
        } else {
            int ships = f.nonFighterShips();
            label = std::to_string(ships);
            if (f.count(UnitType::Fighter)) label += "+" + std::to_string(f.count(UnitType::Fighter)) + "f";
            if (f.groundForces() && size >= 34) label += "+" + std::to_string(f.groundForces()) + "g";
        }
        int tw = textWidth(font, label);
        float bx = x, by = y + float(k) * (font.lineHeight + 2);
        // little ship-shaped marker
        float h = float(font.lineHeight);
        float txs[3] = {bx, bx + h * 0.7f, bx}, tys[3] = {by, by + h / 2, by + h};
        c.fillPolygon(txs, tys, 3, playerColor(g, q));
        c.fillRoundRect(int(bx + h * 0.7f), int(by), tw + 5, int(h), 4, rgb(8, 10, 18), 220);
        drawText(c, font, int(bx + h * 0.7f) + 2, int(by), label, playerColor(g, q));
    }
}
