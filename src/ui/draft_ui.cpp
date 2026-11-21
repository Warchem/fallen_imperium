// Galaxy setup draft (0.15.0): the player places a system tile from their
// hand (owner's Polish list: the tiles along the bottom, the legal spaces
// marked on the map, "Place the tile here?" to confirm).
//   Hand:    LEFT / RIGHT choose a tile, A goes to the map.
//   Map:     the D-PAD moves between the spaces it may go to (X: next), the
//            tile is shown in place; A asks to confirm, B goes back.
//   Confirm: A places it, B goes back to the map.
#include "ui/draft_ui.h"
#include "ui/hits.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/query.h"
#include "gfx/font.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

using namespace ti;

namespace {

constexpr Rect kHandRect{6, 300, 408, 84};
// (widescreen, 0.46.0: the tile's panel stays at the right edge of the screen)
Rect infoRect() { return Rect{theme::screenW() - 216, 26, 212, 422}; }

const char* skipName(TechColor t) {
    switch (t) {
        case TechColor::Blue: return "blue";
        case TechColor::Green: return "green";
        case TechColor::Red: return "red";
        case TechColor::Yellow: return "yellow";
        default: return "";
    }
}

std::string tileName(int def) {
    const SystemDef& d = kSystems[def];
    if (d.planetCount) return d.name;
    return std::string(d.name) + " (" + d.key + ")";
}

// Who places the next tiles (the same snake order as Rules::galaxyDraft).
std::vector<int> draftOrder(const Game& g) {
    std::vector<int> fwd = fromSpeaker(g), order;
    const size_t n = std::max<size_t>(1, fwd.size()), passes = (size_t(g.draft.total) + n - 1) / n;
    for (size_t pass = 0; pass < passes; ++pass) {
        if (pass % 2 == 0) order.insert(order.end(), fwd.begin(), fwd.end());
        else order.insert(order.end(), fwd.rbegin(), fwd.rend());
    }
    return order;
}

class GalaxyDraftUI : public DecisionUI {
public:
    GalaxyDraftUI(const Game& g, const Decision& d, MapView& map)
        : DecisionUI(g, d), map_(map), savedView_(map.viewport()), savedZoom_(map.zoom()) {
        const int p = d_.player;
        if (p >= 0 && p < int(g.draft.hands.size()))
            for (int16_t t : g.draft.hands[size_t(p)])
                if (std::find(tiles_.begin(), tiles_.end(), int(t)) == tiles_.end()) tiles_.push_back(t);
        cells_.assign(tiles_.size(), {});
        for (const Option& o : d_.options) {
            if (!o.enabled) continue;
            auto it = std::find(tiles_.begin(), tiles_.end(), o.id / 1000);
            if (it == tiles_.end()) {  // (not in the hand list: add it anyway)
                tiles_.push_back(o.id / 1000);
                cells_.push_back({});
                it = tiles_.end() - 1;
            }
            cells_[size_t(it - tiles_.begin())].push_back(o.id % 1000);
        }
        for (size_t i = 0; i < tiles_.size(); ++i)
            if (!cells_[i].empty()) {
                sel_ = int(i);
                break;
            }
        Rect v = savedView_;
        v.h = kHandRect.y - v.y - 2;  // the hand sits below the map
        map_.setViewport(v);
        savedArea_ = map_.drawArea();
        map_.setDrawArea(Rect{savedArea_.x, v.y, savedArea_.w, v.h});  // not under the hand strip
        map_.setZoom(0);  // the whole galaxy in view
        if (p >= 0 && g.players[p].homeSystem >= 0) map_.setCursor(g, g.players[p].homeSystem);
        showSpots();
    }
    ~GalaxyDraftUI() override {
        map_.clearDraft();
        map_.setViewport(savedView_);
        map_.setDrawArea(savedArea_);
        map_.setZoom(savedZoom_);
    }

    bool mapMode() const override { return true; }

    std::vector<std::pair<std::string, std::string>> hints() const override {
        if (mode_ == Mode::Confirm) return {{"A", "Place it"}, {"B", "No"}};
        if (mode_ == Mode::Spot) return {{"D-PAD", "Space"}, {"X", "Next"}, {"A", "Place here"}, {"B", "Tiles"}, {"L1/R1", "Zoom"}};
        return {{"\xE2\x97\x80\xE2\x96\xB6", "Tile"}, {"A", "Choose a space"}, {"L1/R1", "Zoom"}};
    }

    bool backAvailable() const override { return mode_ != Mode::Hand; }  // the hand: B undoes your previous step

    bool update(const Input& in, uint32_t now) override {
        (void)now;
        mouse_ = &in.mouse();
        if (int z = in.zoom()) map_.setZoom(map_.zoom() + z);  // L1 / R1 (the wheel: the game screen)
        if (tiles_.empty()) return false;
        // the mouse (0.27.0): pointing at a marked space picks it; a click on one in the
        // hand view goes straight to it (the click's A then asks "Place it here?")
        const Mouse& m = in.mouse();
        // (0.44.1, owner: after choosing a second tile its preview no longer showed under the pointer) pointing at a
        // marked space always shows the tile there, whether the tile was chosen with a click, the keys or not yet
        const bool onHand = m.x >= float(kHandRect.x) && m.x < float(kHandRect.x + kHandRect.w) && m.y >= float(kHandRect.y) &&
                            m.y < float(kHandRect.y + kHandRect.h);
        const Rect vp = map_.viewport();
        const bool onMap = m.active && m.x >= float(vp.x) && m.x < float(vp.x + vp.w) && m.y >= float(vp.y) && m.y < float(vp.y + vp.h) && !onHand &&
                           m.x < float(infoRect().x);
        bool onSpot = false;
        if ((m.moved || m.leftPress) && mode_ != Mode::Confirm && onMap) {
            const int cell = map_.hitDraftCell(g_, m.x, m.y);
            const std::vector<int>& cs = cells_[size_t(sel_)];
            const auto it = std::find(cs.begin(), cs.end(), cell);
            if (cell >= 0 && it != cs.end()) {
                onSpot = true;
                mode_ = Mode::Spot;
                spot_ = int(it - cs.begin());
                showSpots();
            }
        }
        // a click on the map only places the tile on a marked space (a click elsewhere used to mean "yes, the last one")
        const bool clickOffSpot = m.leftPress && onMap && !onSpot;
        switch (mode_) {
            case Mode::Hand: {
                const int n = int(tiles_.size());
                if (in.pressed(Btn::Left)) sel_ = (sel_ + n - 1) % n;
                if (in.pressed(Btn::Right)) sel_ = (sel_ + 1) % n;
                if (in.pressed(Btn::Left) || in.pressed(Btn::Right)) {
                    error_.clear();
                    showSpots();
                }
                if ((in.pressed(Btn::A) && !clickOffSpot) || in.pressed(Btn::Start)) {
                    if (cells_[size_t(sel_)].empty()) {
                        setError("This tile can't go anywhere this turn (it would touch an anomaly or the same wormhole) - choose another.");
                        return false;
                    }
                    mode_ = Mode::Spot;
                    spot_ = 0;
                    showSpots();
                }
                return false;
            }
            case Mode::Spot: {
                const std::vector<int>& cs = cells_[size_t(sel_)];
                if (in.pressed(Btn::B)) {
                    mode_ = Mode::Hand;
                    showSpots();
                    return false;
                }
                if (in.pressed(Btn::X)) spot_ = (spot_ + 1) % int(cs.size());
                const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
                const Btn keys[4] = {Btn::Up, Btn::Right, Btn::Down, Btn::Left};
                for (int k = 0; k < 4; ++k)
                    if (in.pressed(keys[k])) step(float(dirs[k][0]), float(dirs[k][1]));
                showSpots();
                if ((in.pressed(Btn::A) && !clickOffSpot) || in.pressed(Btn::Start)) mode_ = Mode::Confirm;
                return false;
            }
            case Mode::Confirm:
                if (in.pressed(Btn::B)) {
                    mode_ = Mode::Spot;
                    return false;
                }
                if (in.pressed(Btn::A) || in.pressed(Btn::Start)) {
                    answer_ = Answer();
                    answer_.picks.push_back(tiles_[size_t(sel_)] * 1000 + cells_[size_t(sel_)][size_t(spot_)]);
                    return true;
                }
                return false;
        }
        return false;
    }

    void draw(Canvas& c, uint32_t now) override {
        drawHand(c, now);
        drawInfo(c);
        if (mode_ == Mode::Confirm) drawConfirm(c);
        if (!error_.empty()) {
            auto lines = wrapText(FONT_SM, error_, 380);
            int h = int(lines.size()) * 15 + 8, y = kHandRect.y - h - 4;
            c.fillRoundRect(16, y, 388, h, 6, rgb(70, 18, 18), 235);
            for (const std::string& l : lines) {
                drawText(c, FONT_SM, 24, y + 4, l, rgb(255, 200, 190));
                y += 15;
            }
        }
    }

private:
    enum class Mode { Hand, Spot, Confirm };

    void showSpots() {
        if (tiles_.empty()) return;
        map_.setDraftSpots(cells_[size_t(sel_)]);
        if (mode_ == Mode::Hand || cells_[size_t(sel_)].empty()) map_.setDraftGhost(-1, -1);
        else map_.setDraftGhost(cells_[size_t(sel_)][size_t(spot_)], tiles_[size_t(sel_)]);
    }

    // Move to the space that lies best in this direction on the screen.
    void step(float dx, float dy) {
        const std::vector<int>& cs = cells_[size_t(sel_)];
        float x0, y0;
        hexToPixel(g_.draft.cells[size_t(cs[size_t(spot_)])].pos, 1.f, x0, y0);
        int best = -1;
        float bestScore = 1e9f;
        for (size_t i = 0; i < cs.size(); ++i) {
            if (int(i) == spot_) continue;
            float x, y;
            hexToPixel(g_.draft.cells[size_t(cs[i])].pos, 1.f, x, y);
            const float vx = x - x0, vy = y - y0, len = std::sqrt(vx * vx + vy * vy);
            if (len < 1e-3f) continue;
            const float cosA = (vx * dx + vy * dy) / len;
            if (cosA < 0.3f) continue;
            const float score = len * (1.f + 2.f * (1.f - cosA));
            if (score < bestScore) {
                bestScore = score;
                best = int(i);
            }
        }
        if (best >= 0) spot_ = best;
    }

    // The question, at the top of the right-hand panel (the map keeps its height).
    int drawPrompt(Canvas& c) {
        std::string title, sub;
        const int ring = !cells_.empty() && !cells_[size_t(sel_)].empty() ? g_.draft.cells[size_t(cells_[size_t(sel_)][0])].ring : 0;
        if (mode_ == Mode::Hand) {
            title = "Galaxy setup: choose a tile to place";
            sub = "Ring " + std::to_string(ring) + " around Mecatol Rex is being built (the marked spaces).";
        } else {
            title = "Where should " + tileName(tiles_[size_t(sel_)]) + " go?";
            sub = "Ring " + std::to_string(ring) + ", a marked space. Anomalies and same wormholes may not touch.";
        }
        const int x = infoRect().x + 8, w = infoRect().w - 16;
        int y = infoRect().y + 6;
        for (const std::string& l : wrapText(FONT_MDB, title, w)) {
            drawText(c, FONT_MDB, x, y, l, theme::cyan);
            y += 18;
        }
        for (const std::string& l : wrapText(FONT_XS, sub, w)) {
            drawText(c, FONT_XS, x, y, l, theme::dim);
            y += 12;
        }
        y += 3;
        c.hline(x, x + w, y, theme::border);
        return y + 5;
    }

    void drawHand(Canvas& c, uint32_t now) {
        drawPanel(c, kHandRect, rgb(10, 14, 26), mode_ == Mode::Hand ? theme::gold : theme::border, 8);
        drawText(c, FONT_XS, kHandRect.x + 8, kHandRect.y + 3, "YOUR TILES", theme::faint);
        const int n = int(tiles_.size());
        if (!n) return;
        const float pulse = 0.5f + 0.5f * std::sin(float(now % 1000) / 1000.f * 6.2831853f);
        const int slotW = std::min(78, (kHandRect.w - 16) / n);
        const int x0 = kHandRect.x + (kHandRect.w - slotW * n) / 2;
        for (int i = 0; i < n; ++i) {
            const bool sel = i == sel_, can = !cells_[size_t(i)].empty();
            const float cx = float(x0 + slotW * i + slotW / 2), cy = float(kHandRect.y + 36 - (sel ? 3 : 0));
            // the mouse: a click chooses the tile (0.44.1 - pointing at the tiles used to choose them, so the pointer's
            // way up to the map could change the tile, and pointing at the chosen one dropped the preview)
            const Rect slot{x0 + slotW * i, kHandRect.y + 4, slotW, kHandRect.h - 8};
            if (mode_ != Mode::Confirm)
                hits::addAction(slot, [this, i]() {
                    if (sel_ == i) return;
                    sel_ = i;
                    mode_ = Mode::Hand;
                    spot_ = 0;
                    error_.clear();
                    showSpots();
                });
            const bool pointed = mouse_ && mouse_->active && mode_ != Mode::Confirm && mouse_->x >= float(slot.x) && mouse_->x < float(slot.x + slot.w) &&
                                 mouse_->y >= float(slot.y) && mouse_->y < float(slot.y + slot.h);
            if (pointed && !sel) c.fillRoundRect(slot.x + 1, slot.y + 2, slot.w - 2, slot.h - 4, 6, rgb(60, 80, 120), 70);
            const float size = sel ? 26.f : 22.f;
            MapView::drawTile(c, tiles_[size_t(i)], cx, cy, size);
            if (!can) {  // can't be placed this turn
                float xs[6], ys[6];
                for (int k = 0; k < 6; ++k) {
                    xs[k] = cx + size * std::cos(1.0471976f * k);
                    ys[k] = cy + size * std::sin(1.0471976f * k);
                }
                c.fillPolygon(xs, ys, 6, rgb(0, 0, 0), 150);
            }
            if (sel) {
                float xs[6], ys[6];
                for (int k = 0; k < 6; ++k) {
                    xs[k] = cx + (size + 2.f) * std::cos(1.0471976f * k);
                    ys[k] = cy + (size + 2.f) * std::sin(1.0471976f * k);
                }
                for (int k = 0; k < 6; ++k)
                    c.line(xs[k], ys[k], xs[(k + 1) % 6], ys[(k + 1) % 6], theme::gold, 2.f, mode_ == Mode::Hand ? 160 + int(pulse * 95) : 200);
            }
            const std::string name = kSystems[tiles_[size_t(i)]].name;
            drawTextAligned(c, FONT_XS, x0 + slotW * i, kHandRect.y + kHandRect.h - 16, slotW, Align::Center, fitText(FONT_XS, name, slotW - 4),
                            sel ? theme::text : can ? theme::dim : theme::faint);
        }
    }

    void drawInfo(Canvas& c) {
        drawPanel(c, infoRect(), theme::panel, theme::border, 8);
        if (tiles_.empty()) return;
        const int def = tiles_[size_t(sel_)];
        const SystemDef& d = kSystems[def];
        int x = infoRect().x + 8, w = infoRect().w - 16, y = drawPrompt(c);
        drawText(c, FONT_MDB, x, y, fitText(FONT_MDB, tileName(def), w), theme::gold);
        y += 19;
        drawText(c, FONT_XS, x, y, std::string("Tile ") + d.key + (d.back == Back::Red ? " \xC2\xB7 red back" : " \xC2\xB7 blue back"), theme::faint);
        y += 14;
        MapView::drawTile(c, def, float(infoRect().x + infoRect().w / 2), float(y + 32), 36.f);
        y += 70;
        auto para = [&](const std::string& s, uint32_t col, const FontData& f = FONT_SM) {
            for (const std::string& l : wrapText(f, s, w)) {
                if (y > infoRect().y + infoRect().h - 60) return;
                drawText(c, f, x, y, l, col);
                y += f.lineHeight + 1;
            }
        };
        for (int i = 0; i < d.planetCount; ++i) {
            const PlanetDef& pd = kPlanets[d.planets[i]];
            drawText(c, FONT_SMB, x, y, pd.name, pd.legendary ? theme::gold : theme::text);
            const std::string ri = std::to_string(pd.res) + " / " + std::to_string(pd.inf);
            drawTextAligned(c, FONT_SMB, x, y, w, Align::Right, ri, theme::gold);
            y += 15;
            std::string line = traitName(pd.trait);
            if (pd.skip != TechColor::None) line += std::string(line.empty() ? "" : " \xC2\xB7 ") + skipName(pd.skip) + " specialty";
            if (pd.legendary) line += std::string(line.empty() ? "" : " \xC2\xB7 ") + "legendary";
            if (!line.empty()) para(line, theme::dim, FONT_XS);
            y += 2;
        }
        if (d.anomalies & kAsteroid) para("Asteroid field: ships can't move into or through it (Antimass Deflectors allows it).", theme::text);
        if (d.anomalies & kNebula) para("Nebula: ships must stop in it; defenders there get +1 in space combat.", theme::text);
        if (d.anomalies & kSupernova) para("Supernova: ships can't move into or through it.", theme::text);
        if (d.anomalies & kRift) para("Gravity rift: +1 move for ships leaving it, but each may be lost (roll 1-3).", theme::text);
        if (d.wormholes & kAlpha) para("Alpha wormhole: next to every other alpha wormhole.", theme::text);
        if (d.wormholes & kBeta) para("Beta wormhole: next to every other beta wormhole.", theme::text);
        if (!d.planetCount && !d.anomalies && !d.wormholes) para("Empty space.", theme::text);
        y += 4;
        // who places next
        std::vector<int> order = draftOrder(g_);
        std::string next;
        int shown = 0;
        for (size_t i = size_t(g_.draft.placed) + 1; i < order.size() && shown < 3; ++i) {
            const int q = order[i];
            if (q < 0 || q >= int(g_.draft.hands.size())) continue;
            next += (next.empty() ? "" : ", ") + (q == d_.player ? std::string("you") : playerName(g_, q));
            ++shown;
        }
        const int left = g_.draft.total - g_.draft.placed;
        const int by = infoRect().y + infoRect().h - 56;
        c.hline(x, x + w, by - 4, theme::border);
        drawText(c, FONT_XS, x, by, fitText(FONT_XS, std::to_string(left) + " tiles left to place" + (next.empty() || left <= 1 ? "" : "; then " + next), w),
                 theme::dim);
        int ty = by + 14;
        for (const std::string& l : wrapText(FONT_XS, "Good planets next to your home; anomalies and empty space far from it, toward your rivals.", w)) {
            drawText(c, FONT_XS, x, ty, l, theme::faint);
            ty += 12;
        }
    }

    // "Place the tile here?" with Yes / No buttons (0.44.1, owner: he clicked the "no" text thinking it was a button). While it
    // is open a click anywhere else on the map does nothing (a click used to mean "yes").
    void drawConfirm(Canvas& c) {
        hits::addBackdrop(Rect{0, 0, theme::screenW(), kHandRect.y + kHandRect.h}, []() {});
        // the box sits above (or below) the space, so the tile it asks about stays in view
        Rect r{80, 140, 260, 104};
        float tx, ty, ts;
        if (!cells_[size_t(sel_)].empty() && map_.draftCellAt(g_, cells_[size_t(sel_)][size_t(spot_)], tx, ty, ts)) {
            const Rect v = map_.viewport();
            const int above = int(ty - ts) - 8 - r.h, below = int(ty + ts) + 8;
            r.y = above >= v.y + 2 ? above : below + r.h <= v.y + v.h - 2 ? below : r.y;
            r.x = std::max(v.x + 6, std::min(int(tx) - r.w / 2, std::min(v.x + v.w, infoRect().x) - 6 - r.w));
        }
        c.fillRoundRect(r.x, r.y, r.w, r.h, 10, rgb(12, 17, 32), 240);
        c.frameRoundRect(r.x, r.y, r.w, r.h, 10, theme::gold, 2);
        drawTextAligned(c, FONT_MDB, r.x, r.y + 10, r.w, Align::Center, "Place the tile here?", theme::gold);
        drawTextAligned(c, FONT_SM, r.x, r.y + 32, r.w, Align::Center, fitText(FONT_SM, tileName(tiles_[size_t(sel_)]), r.w - 16), theme::text);
        const Rect yes{r.x + 20, r.y + 58, 100, 32}, no{r.x + r.w - 120, r.y + 58, 100, 32};
        const Mouse& m = mouse_ ? *mouse_ : Mouse();
        auto button = [&](const Rect& b, const char* label, const char* key, uint32_t col) {
            const bool over = m.x >= float(b.x) && m.x < float(b.x + b.w) && m.y >= float(b.y) && m.y < float(b.y + b.h);
            c.fillRoundRect(b.x, b.y, b.w, b.h, 7, over ? mix(col, rgb(255, 255, 255), 50) : col, 235);
            c.frameRoundRect(b.x, b.y, b.w, b.h, 7, over ? theme::gold : theme::border, over ? 2 : 1);
            drawTextAligned(c, FONT_MDB, b.x, b.y + 4, b.w, Align::Center, label, theme::text);
            drawTextAligned(c, FONT_XS, b.x, b.y + 19, b.w, Align::Center, keyLabel(key), theme::dim);
        };
        button(yes, "Yes", "A", rgb(30, 92, 52));
        button(no, "No", "B", rgb(96, 34, 34));
        hits::addButton(yes, Btn::A);
        hits::addButton(no, Btn::B);
    }
    const Mouse* mouse_ = nullptr;  // (the pointer, for the buttons' hover)

    MapView& map_;
    Rect savedView_;
    Rect savedArea_;
    int savedZoom_ = 1;
    std::vector<int> tiles_;
    std::vector<std::vector<int>> cells_;
    int sel_ = 0, spot_ = 0;
    Mode mode_ = Mode::Hand;
};

}  // namespace

std::unique_ptr<DecisionUI> makeGalaxyDraftUI(const Game& g, const Decision& d, MapView& map) {
    return std::unique_ptr<DecisionUI>(new GalaxyDraftUI(g, d, map));
}
