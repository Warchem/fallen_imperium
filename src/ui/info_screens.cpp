#include "ui/info_screens.h"
#include "ui/hits.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "engine/breakthroughs.h"
#include "engine/deal.h"
#include "engine/query.h"
#include "gfx/font.h"
#include "ui/cards.h"
#include "ui/breakthrough_card.h"
#include "ui/faction_pieces.h"
#include "core/log.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

using namespace ti;

uint32_t strategyCardColor(int card) {
    static const uint32_t k[8] = {rgb(206, 58, 52),  rgb(232, 132, 44), rgb(226, 196, 58), rgb(64, 170, 84),
                                  rgb(38, 168, 158), rgb(70, 146, 226), rgb(64, 88, 214),  rgb(146, 76, 196)};
    return card >= 0 && card < 8 ? k[card] : theme::gold;
}

uint32_t techColor(int tech) {
    if (tech < 0 || tech >= kTechCount) return theme::dim;
    const TechDef& d = kTechs[tech];
    if (d.unitUpgrade) return rgb(176, 182, 198);
    switch (d.color) {
        case TechColor::Blue: return rgb(80, 146, 240);
        case TechColor::Green: return rgb(84, 196, 100);
        case TechColor::Red: return rgb(228, 80, 70);
        case TechColor::Yellow: return rgb(238, 206, 70);
        default: return rgb(196, 150, 236);
    }
}

namespace {

uint32_t pcol(const Game& g, int p) {
    return p == kNeutral ? theme::neutral : p >= 0 && p < g.numPlayers ? theme::player[g.players[p].color % 8] : theme::text;
}

std::string capitalize(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = char(s[0] - 32);
    return s;
}

const uint32_t kColorDot[4] = {rgb(80, 146, 240), rgb(84, 196, 100), rgb(228, 80, 70), rgb(238, 206, 70)};
const char* kColorWord[4] = {"blue", "green", "red", "yellow"};

uint32_t traitColor(Trait t) {
    switch (t) {
        case Trait::Cultural: return theme::cultural;
        case Trait::Hazardous: return theme::hazardous;
        case Trait::Industrial: return theme::industrial;
        default: return rgb(170, 160, 140);
    }
}

// Draws wrapped text, returns the y after it. Stops at maxY.
int drawWrapped(Canvas& c, const FontData& f, int x, int y, int w, const std::string& s, uint32_t col, int maxY = 10000) {
    for (const std::string& l : wrapText(f, s, w)) {
        if (y + f.lineHeight > maxY) break;
        drawText(c, f, x, y, l, col);
        y += f.lineHeight + 1;
    }
    return y;
}

// onClick (0.30.0, owner): clicking a tab's title opens it
void drawTabs(Canvas& c, const std::vector<std::string>& tabs, int sel, int right, int y,
              const std::function<void(int)>& onClick = nullptr) {
    if (tabs.size() < 2) return;
    int x = right;
    for (int i = int(tabs.size()) - 1; i >= 0; --i) {
        int w = textWidth(FONT_SMB, tabs[i]) + 18;
        x -= w;
        bool on = i == sel;
        if (onClick) hits::addAction(Rect{x, y, w, 22}, [onClick, i]() { onClick(i); });
        c.fillRoundRect(x, y, w, 22, 6, on ? theme::gold : theme::panelHi);
        drawTextAligned(c, FONT_SMB, x, y + 3, w, Align::Center, tabs[i], on ? theme::black : theme::dim);
        x -= 6;
    }
}

std::string prereqText(const TechDef& d) {
    std::string s;
    for (int k = 0; k < 4; ++k) {
        if (!d.req[k]) continue;
        if (!s.empty()) s += ", ";
        s += std::to_string(d.req[k]) + " " + kColorWord[k];
    }
    return s.empty() ? "No prerequisites" : "Requires " + s + " technolog" + (d.req[0] + d.req[1] + d.req[2] + d.req[3] > 1 ? "ies" : "y");
}

std::string costText(const UnitDef& u) {
    if (u.cost2 <= 0) return "-";
    if (u.cost2 == 1) return "1 for 2";
    return std::to_string(u.cost2 / 2);
}

std::string rollText(int value, int dice) {
    if (value <= 0) return "-";
    return std::to_string(value) + (dice > 1 ? " x" + std::to_string(dice) : "");
}

// ======================================================================== grid base
class GridScreen : public InfoScreen {
public:
    GridScreen(const Game& g, int human) : g_(g), human_(human) {}

    bool update(const Input& in) override {
        if (detail_) {
            if (in.pressed(Btn::B) || in.pressed(Btn::A) || in.pressed(Btn::Menu)) detail_ = false;
            if (in.pressed(Btn::Left) && sel_ > 0) --sel_;
            if (in.pressed(Btn::Right) && sel_ + 1 < count_) ++sel_;
            return true;
        }
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) return false;
        if (tabs_.size() > 1 && (in.pressed(Btn::L1) || in.pressed(Btn::R1))) {
            int n = int(tabs_.size());
            tab_ = (tab_ + (in.pressed(Btn::R1) ? 1 : n - 1)) % n;
            sel_ = scroll_ = 0;
            rebuild();
        }
        if (count_ > 0) {
            if (in.pressed(Btn::Right)) sel_ = std::min(count_ - 1, sel_ + 1);
            if (in.pressed(Btn::Left)) sel_ = std::max(0, sel_ - 1);
            if (in.pressed(Btn::Down) && sel_ + cols_ < count_) sel_ += cols_;
            else if (in.pressed(Btn::Down) && sel_ / cols_ < (count_ - 1) / cols_) sel_ = count_ - 1;
            if (in.pressed(Btn::Up) && sel_ - cols_ >= 0) sel_ -= cols_;
            if (in.pressed(Btn::A)) detail_ = true;
        }
        return true;
    }

    void draw(Canvas& c, uint32_t now) override {
        now_ = now;
        c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 150);
        Rect r{10, 10, 620, 436};
        drawPanel(c, r, theme::panel, theme::gold, 12);
        int tx = r.x + 14;
        if (titleFaction_ >= 0 && icons::available()) {
            drawFactionIcon(c, titleFaction_, tx, r.y + 7, 28);
            tx += 34;
        } else if (const char* ic = titleIcon()) {
            if (icons::draw(c, ic, tx, r.y + 9, 24, 255, theme::gold)) tx += 30;
        }
        drawText(c, FONT_LG, tx, r.y + 10, title_, theme::gold);
        drawTabs(c, tabs_, tab_, r.x + r.w - 12, r.y + 12, [this](int i) {
            if (detail_ || i == tab_) return;
            tab_ = i;
            sel_ = scroll_ = 0;
            rebuild();
        });
        std::string h = header();
        if (!h.empty()) drawText(c, FONT_SM, r.x + 14, r.y + 40, fitText(FONT_SM, h, r.w - 28), theme::dim);
        const int gap = 8;
        Rect area{r.x + 8, r.y + 60, r.w - 16, r.h - 66};
        int rowsVisible = std::max(1, (area.h + gap) / (cardH_ + gap));
        int selRow = sel_ / cols_;
        if (selRow < scroll_) scroll_ = selRow;
        if (selRow >= scroll_ + rowsVisible) scroll_ = selRow - rowsVisible + 1;
        if (count_ == 0) drawTextAligned(c, FONT_MD, area.x, area.y + 60, area.w, Align::Center, emptyText(), theme::dim);
        int cw = (area.w - gap * (cols_ - 1)) / cols_;
        for (int i = scroll_ * cols_; i < count_ && i < (scroll_ + rowsVisible) * cols_; ++i) {
            int row = i / cols_ - scroll_, col = i % cols_;
            Rect cr{area.x + col * (cw + gap), area.y + row * (cardH_ + gap), cw, cardH_};
            if (!detail_) hits::add(cr, [this, i]() { sel_ = i; });  // the mouse (0.27.0): point at a card, click to read it
            drawCard(c, i, cr, i == sel_ && !detail_, now);
        }
        int totalRows = (count_ + cols_ - 1) / cols_;
        if (scroll_ > 0) drawTextAligned(c, FONT_SM, r.x, r.y + 44, r.w - 16, Align::Right, "\xE2\x96\xB2 more", theme::dim);
        if (scroll_ + rowsVisible < totalRows)
            drawTextAligned(c, FONT_SM, r.x, r.y + r.h - 18, r.w - 16, Align::Right, "\xE2\x96\xBC more", theme::dim);
        if (detail_ && sel_ < count_) {
            c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 120);
            Rect d{50, 30, 540, 404};
            drawDetail(c, sel_, d);
            if (!noHints_) drawHintBar(c, {{"\xE2\x97\x80\xE2\x96\xB6", "Previous / next"}, {"B", "Close"}});
        } else if (noHints_) {
        } else {
            std::vector<std::pair<std::string, std::string>> hints = {{"D-PAD", "Select"}, {"A", "Read card"}};
            if (tabs_.size() > 1) hints.push_back({"L1/R1", "Switch list"});
            hints.push_back({"B", "Back"});
            drawHintBar(c, hints);
        }
    }

protected:
    virtual void rebuild() = 0;
    virtual void drawCard(Canvas& c, int i, Rect r, bool sel, uint32_t now) = 0;
    virtual void drawDetail(Canvas& c, int i, Rect r) = 0;
    virtual std::string header() const { return ""; }
    virtual std::string emptyText() const { return "Nothing here yet."; }
    virtual const char* titleIcon() const { return nullptr; }
    // An icon in a card's top-right corner; returns the width it takes from the title.
    static int cornerIcon(Canvas& c, Rect r, const char* name, uint32_t tint, int size = 20) {
        return icons::draw(c, name, r.x + r.w - size - 7, r.y + 5, size, 255, tint) ? size + 4 : 0;
    }

    void cardFrame(Canvas& c, Rect r, bool sel, uint32_t stripe, uint32_t now) const {
        c.fillRoundRect(r.x, r.y, r.w, r.h, 8, sel ? rgb(30, 40, 68) : rgb(20, 27, 46));
        c.fillRoundRect(r.x, r.y, 7, r.h, 4, stripe);
        if (sel) {
            float pulse = 0.5f + 0.5f * std::sin(float(now % 1500) / 1500.f * 6.2831853f);
            c.frameRoundRect(r.x, r.y, r.w, r.h, 8, mix(theme::goldDim, theme::gold, int(pulse * 255)), 2);
        } else {
            c.frameRoundRect(r.x, r.y, r.w, r.h, 8, theme::border, 1);
        }
    }
    void detailFrame(Canvas& c, Rect r, uint32_t stripe) const {
        drawPanel(c, r, rgb(16, 22, 40), stripe, 12);
        c.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, 8, 6, stripe);
    }

    const Game& g_;
    int human_;
    std::string title_;
    std::vector<std::string> tabs_;
    int tab_ = 0, count_ = 0, sel_ = 0, scroll_ = 0;
    int cols_ = 3, cardH_ = 84;
    int titleFaction_ = -1;
    bool detail_ = false;
    bool noHints_ = false;  // the research picker: the game screen draws the hints
    uint32_t now_ = 0;      // (the clock of the frame being drawn)
};

// ======================================================================== technology
class TechScreen : public GridScreen {
public:
    TechScreen(const Game& g, int human) : GridScreen(g, human) {
        title_ = "Technology";
        tabs_ = {"Yours", "Can research", "All"};
        if (breakthroughsOn(g)) {  // 0.44.3 (owner): breakthroughs shown like technologies
            title_ = "Tech & Breakthroughs";
            tabs_.push_back("Breakthroughs");
        }
        cols_ = 3;
        cardH_ = 78;
        rebuild();
    }

protected:
    static constexpr int kBtItem = 20000;  // items_: a player's breakthrough card (kBtItem + player)
    static int order(int t) {
        const TechDef& d = kTechs[t];
        int col = d.unitUpgrade ? 4 : d.color == TechColor::None ? 5 : int(d.color);
        return col * 100 + d.req[0] + d.req[1] + d.req[2] + d.req[3];
    }
    // Valefar Assimilator (0.29.0, owner): the X / Y token on a technology the Nekro copied -
    // shown on the Nekro's card and on the owner's card. 'X' / 'Y', or 0.
    char valefar(int t) const {
        for (int q = 0; q < g_.numPlayers; ++q) {
            const Player& n = g_.players[size_t(q)];
            if (n.assimX != t && n.assimY != t) continue;
            if (q == human_ || (human_ >= 0 && g_.players[size_t(human_)].hasTech(t))) return n.assimX == t ? 'X' : 'Y';
        }
        return 0;
    }
    static void valefarMark(Canvas& c, int x, int y, char letter, int size) {
        c.fillCircle(float(x), float(y), float(size) * 0.5f + 1.5f, rgb(20, 4, 6));
        c.fillCircle(float(x), float(y), float(size) * 0.5f, rgb(196, 30, 40));
        c.ring(float(x), float(y), float(size) * 0.5f, 1.5f, rgb(255, 170, 160), 230);
        drawTextAligned(c, size >= 26 ? FONT_LG : FONT_SMB, x - size, y - (size >= 26 ? 10 : 7), 2 * size, Align::Center,
                        std::string(1, letter), theme::text);
    }
    void rebuild() override {
        items_.clear();
        if (human_ < 0) return;
        const Player& p = g_.players[human_];
        for (int t = 0; t < kTechCount; ++t) {
            bool owned = p.hasTech(t) || p.assimX == t || p.assimY == t;  // (the Nekro's copied technologies too)
            bool ok = false;
            if (tab_ == 0) ok = owned;
            else if (tab_ == 1) ok = canResearchNow(g_, human_, t);
            else ok = owned || techAvailableToResearch(g_, human_, t);
            if (ok && tab_ != 3) items_.push_back(t);
        }
        std::stable_sort(items_.begin(), items_.end(), [](int a, int b) { return order(a) < order(b); });
        // 0.44.3: your breakthrough first (Yours / All), every player's on their own tab
        if ((tab_ == 0 || tab_ == 2) && btcard::has(g_, human_)) items_.insert(items_.begin(), kBtItem + human_);
        if (tab_ == 3) {
            items_.clear();
            for (int k = 0; k < g_.numPlayers; ++k) {
                const int q = (human_ + k) % g_.numPlayers;
                if (btcard::has(g_, q)) items_.push_back(kBtItem + q);
            }
        }
        count_ = int(items_.size());
    }
    std::string header() const override {
        if (tab_ == 3) return "Every player's breakthrough \xC2\xB7 gained ones give their synergy and ability";
        if (human_ < 0) return "";
        const Player& p = g_.players[human_];
        std::string s = "You own " + std::to_string(p.techs.size()) + ":";
        for (int k = 0; k < 4; ++k) s += " " + std::to_string(techCount(g_, human_, TechColor(k))) + " " + kColorWord[k];
        if (btcard::has(g_, human_))
            s += btcard::stateOf(g_, human_) == btcard::NotGained ? " \xC2\xB7 breakthrough not gained yet" : " \xC2\xB7 breakthrough gained";
        if (tab_ == 1) s = "Prerequisites met now (counting planets that can skip one)";
        return s;
    }
    std::string emptyText() const override { return tab_ == 1 ? "Nothing can be researched right now." : "No technology yet."; }

    virtual std::string status(int t, uint32_t& col) const {
        const Player& p = g_.players[human_];
        if (!p.hasTech(t) && (p.assimX == t || p.assimY == t)) {
            col = theme::green;
            return p.assimX == t ? "Copied (Valefar X)" : "Copied (Valefar Y)";
        }
        if (p.hasTech(t)) {
            col = p.techExhausted(t) ? theme::dim : theme::green;
            return p.techExhausted(t) ? "Owned (exhausted)" : "Owned";
        }
        if (canResearchNow(g_, human_, t)) {
            col = theme::cyan;
            return "Can research";
        }
        col = theme::faint;
        return "Locked";
    }

    void drawCard(Canvas& c, int i, Rect r, bool sel, uint32_t now) override {
        int t = items_[i];
        if (t >= kBtItem) {  // 0.44.3: a breakthrough card
            const int q = t - kBtItem;
            btcard::draw(c, g_.players[size_t(q)].faction, r, btcard::stateOf(g_, q), sel, now);
            return;
        }
        const TechDef& d = kTechs[t];
        uint32_t tc = techColor(t);
        const bool colored = !d.unitUpgrade && d.color != TechColor::None;
        cards::techTile(c, t, r);  // the card design
        c.fillRoundRect(r.x, r.y, 5, r.h, 3, tc);
        if (const char v = valefar(t)) valefarMark(c, r.x + r.w - 14, r.y + r.h - 14, v, 18);
        if (sel) {
            float pulse = 0.5f + 0.5f * std::sin(float(now % 1500) / 1500.f * 6.2831853f);
            c.frameRoundRect(r.x, r.y, r.w, r.h, 6, mix(theme::goldDim, theme::gold, int(pulse * 255)), 2);
        }
        int x = r.x + 14, w = r.w - (colored ? 52 : 22);
        auto lines = wrapText(FONT_SMB, d.name, w);
        int y = r.y + 6;
        for (size_t k = 0; k < lines.size() && k < 2; ++k) {
            drawText(c, FONT_SMB, x, y, lines[k], theme::text);
            y += 16;
        }
        std::string kind = d.unitUpgrade ? "Unit upgrade" : d.color == TechColor::None ? "Faction" : colorName(d.color);
        if (d.faction >= 0) kind += " \xC2\xB7 faction";
        drawText(c, FONT_XS, x, r.y + r.h - 34, kind, shade(tc, 0.85f));
        // prerequisite pips
        int px = x;
        for (int k = 0; k < 4; ++k)
            for (int n = 0; n < d.req[k]; ++n) {
                if (icons::available()) drawSkipIcon(c, TechColor(k), px, r.y + r.h - 19, 13);
                else c.fillCircle(float(px + 5), float(r.y + r.h - 12), 4.5f, kColorDot[k]);
                px += icons::available() ? 14 : 12;
            }
        uint32_t sc;
        std::string st = status(t, sc);
        drawTextAligned(c, FONT_XS, r.x, r.y + r.h - 19, r.w - 8 - (colored ? r.h * 9 / 10 : 0), Align::Right, st, sc);
        if (!techImplemented(t)) {
            int ex = r.x + r.w - 12 - (colored && icons::available() ? 22 : 0);
            c.fillCircle(float(ex), float(r.y + 12), 7.f, rgb(90, 70, 20));
            drawTextAligned(c, FONT_XS, ex - 7, r.y + 5, 14, Align::Center, "!", theme::gold);
        }
    }

    void drawDetail(Canvas& c, int i, Rect r) override {
        int t = items_[i];
        if (t >= kBtItem) {  // 0.44.3: the breakthrough card, large
            btcard::drawLarge(c, g_, t - kBtItem, human_, r, now_);
            return;
        }
        const TechDef& d = kTechs[t];
        uint32_t tc = techColor(t);
        drawArtDetail(c, t, r);
        if (const char v = valefar(t)) {
            valefarMark(c, r.x + r.w - 30, r.y + 30, v, 30);
            drawTextAligned(c, FONT_XS, r.x, r.y + r.h - 16, r.w - 14, Align::Right,
                            std::string("Valefar Assimilator ") + v + " is on this technology (the Nekro use it too).", rgb(255, 170, 160));
        }
        return;
        detailFrame(c, r, tc);
        int x = r.x + 20, w = r.w - 40, y = r.y + 20;
        if (!d.unitUpgrade && d.color != TechColor::None && icons::available()) {
            drawSkipIcon(c, d.color, r.x + r.w - 62, r.y + 18, 42);
            w -= 48;
        }
        y = drawWrapped(c, FONT_XL, x, y, w, d.name, theme::text);
        w = r.w - 40;
        std::string kind = d.unitUpgrade ? "Unit upgrade technology" : d.color == TechColor::None ? "Faction technology"
                                                                                                    : std::string(colorName(d.color)) + " technology";
        if (d.faction >= 0) kind += " \xC2\xB7 " + std::string(kFactions[d.faction].name);
        drawText(c, FONT_MDB, x, y + 2, kind, tc);
        y += 26;
        int px = x;
        for (int k = 0; k < 4; ++k)
            for (int n = 0; n < d.req[k]; ++n) {
                if (icons::available()) drawSkipIcon(c, TechColor(k), px, y, 17);
                else c.fillCircle(float(px + 7), float(y + 8), 6.5f, kColorDot[k]);
                px += icons::available() ? 19 : 16;
            }
        drawText(c, FONT_SM, px + (px > x ? 6 : 0), y + 1, prereqText(d), theme::dim);
        y += 24;
        uint32_t sc;
        std::string st = status(t, sc);
        if (st == "Locked" && human_ >= 0) {
            int miss[4];
            missingPrereqs(g_, human_, t, miss);
            std::string m;
            for (int k = 0; k < 4; ++k)
                if (miss[k]) m += (m.empty() ? "" : ", ") + std::to_string(miss[k]) + " more " + kColorWord[k];
            if (!m.empty()) st += " - you need " + m;
        }
        drawText(c, FONT_SMB, x, y, st, sc);
        y += 24;
        c.hline(x, x + w, y, theme::border);
        y += 10;
        y = drawWrapped(c, FONT_MD, x, y, w, techSummary(t), theme::text, r.y + r.h - 60);
        y += 8;
        if (d.unitUpgrade) {
            for (int u = 0; u < kUnitCount; ++u) {
                if (kUnits[u].tech != t) continue;
                const UnitDef& ud = kUnits[u];
                std::string s = std::string(ud.name) + ": cost " + costText(ud) + ", combat " + rollText(ud.combat, ud.combatDice) +
                                ", move " + (ud.move ? std::to_string(ud.move) : "-") + ", capacity " + std::to_string(ud.capacity);
                if (ud.afb) s += ", anti-fighter barrage " + rollText(ud.afb, ud.afbDice);
                if (ud.cannon) s += ", space cannon " + rollText(ud.cannon, ud.cannonDice);
                if (ud.production) s += ", production " + std::to_string(ud.production);
                y = drawWrapped(c, FONT_SM, x, y, w, s, theme::cyan, r.y + r.h - 40);
                break;
            }
        }
        if (!techImplemented(t))
            drawWrapped(c, FONT_SM, x, r.y + r.h - 40, w, "This technology's special effect isn't active in this version yet.",
                        theme::gold);
    }

    // The card itself, with what it means for you below.
    void drawArtDetail(Canvas& c, int t, Rect r) {
        const TechDef& d = kTechs[t];
        uint32_t tc = techColor(t);
        drawPanel(c, r, rgb(10, 14, 26), tc, 12);
        const int cw = r.w - 40, ch = cw * 494 / 759;
        Rect card{r.x + (r.w - cw) / 2, r.y + 10, cw, ch};
        cards::tech(c, t, card);
        int x = r.x + 20, w = r.w - 40, y = card.y + card.h + 8;
        std::string kind = d.unitUpgrade ? "Unit upgrade technology" : d.color == TechColor::None ? "Faction technology"
                                                                                                    : std::string(colorName(d.color)) + " technology";
        if (d.faction >= 0) kind += " \xC2\xB7 " + std::string(kFactions[d.faction].name);
        std::string pre = prereqText(d);
        drawText(c, FONT_SMB, x, y, fitText(FONT_SMB, kind + (pre.empty() ? "" : " \xC2\xB7 " + pre), w), tc);
        y += 18;
        uint32_t sc;
        std::string st = status(t, sc);
        if (st == "Locked" && human_ >= 0) {
            int miss[4];
            missingPrereqs(g_, human_, t, miss);
            std::string m;
            for (int k = 0; k < 4; ++k)
                if (miss[k]) m += (m.empty() ? "" : ", ") + std::to_string(miss[k]) + " more " + kColorWord[k];
            if (!m.empty()) st += " - you need " + m;
        }
        drawText(c, FONT_SMB, x, y, fitText(FONT_SMB, st, w), sc);
        y += 18;
        if (d.unitUpgrade) {
            for (int u = 0; u < kUnitCount; ++u) {
                if (kUnits[u].tech != t) continue;
                const UnitDef& ud = kUnits[u];
                std::string s = std::string(ud.name) + ": cost " + costText(ud) + ", combat " + rollText(ud.combat, ud.combatDice) +
                                ", move " + (ud.move ? std::to_string(ud.move) : "-") + ", capacity " + std::to_string(ud.capacity);
                if (ud.afb) s += ", anti-fighter barrage " + rollText(ud.afb, ud.afbDice);
                if (ud.cannon) s += ", space cannon " + rollText(ud.cannon, ud.cannonDice);
                if (ud.production) s += ", production " + std::to_string(ud.production);
                y = drawWrapped(c, FONT_XS, x, y, w, s, theme::cyan, r.y + r.h - 6);
                break;
            }
        }
        if (!techImplemented(t)) drawWrapped(c, FONT_XS, x, y, w, "This technology's special effect isn't active in this version yet.", theme::gold, r.y + r.h - 4);
    }

protected:
    std::vector<int> items_;
};

// ================================================================ research picker (0.28.0)
// Owner: research from the technology cards instead of a list - the card opens
// large with its text, and A there researches it.
class ResearchPicker : public TechScreen, public ResearchPick {
public:
    ResearchPicker(const Game& g, int player, const std::vector<int>& ids, const std::vector<std::string>& notes,
                   const std::string& prompt)
        : TechScreen(g, player), ids_(ids), notes_(notes), prompt_(prompt) {
        title_ = "Research a technology";
        tabs_.clear();
        noHints_ = true;
        rebuild();
    }
    int pick(const Input& in) override {
        if (detail_) {
            if (in.pressed(Btn::A) && sel_ < count_) return items_[size_t(sel_)];
            if (in.pressed(Btn::B)) detail_ = false;
            if (in.pressed(Btn::Left) && sel_ > 0) --sel_;
            if (in.pressed(Btn::Right) && sel_ + 1 < count_) ++sel_;
            return -2;
        }
        if (!in.pressed(Btn::B) && !in.pressed(Btn::Menu)) GridScreen::update(in);
        return -2;
    }
    void paint(Canvas& c, uint32_t now) override { GridScreen::draw(c, now); }
    bool detailOpen() const override { return detail_; }

protected:
    void rebuild() override {
        items_ = ids_;
        std::stable_sort(items_.begin(), items_.end(), [](int a, int b) { return order(a) < order(b); });
        count_ = int(items_.size());
    }
    std::string header() const override {
        std::string h = prompt_;
        if (sel_ < count_)
            for (size_t k = 0; k < ids_.size() && k < notes_.size(); ++k)
                if (ids_[k] == items_[size_t(sel_)] && notes_[k].find("(uses") != std::string::npos) {
                    size_t a = notes_[k].find("(uses"), b = notes_[k].find(')', a);
                    h += "  " + notes_[k].substr(a, b == std::string::npos ? std::string::npos : b - a + 1);
                }
        return h;
    }

    // 0.43.0 (owner: with AI Development Algorithm the cards said "Locked"): everything this question offers can be
    // researched - some only through an ability (AI Development Algorithm, The Prophet's Tears, Riftmeld...)
    std::string status(int t, uint32_t& col) const override {
        if (std::find(ids_.begin(), ids_.end(), t) != ids_.end() && !g_.players[human_].hasTech(t)) {
            col = theme::cyan;
            for (size_t k = 0; k < ids_.size() && k < notes_.size(); ++k)
                if (ids_[k] == t && notes_[k].find("(uses") != std::string::npos) return "Can research (ability)";
            return canResearchNow(g_, human_, t) ? "Can research" : "Can research (ability)";
        }
        return TechScreen::status(t, col);
    }

private:
    std::vector<int> ids_;
    std::vector<std::string> notes_;
    std::string prompt_;
};

// ======================================================================== planets
class PlanetScreen : public GridScreen {
public:
    PlanetScreen(const Game& g, int human) : GridScreen(g, human) {
        title_ = "Planets";
        tabs_ = {"Yours", "Everyone's"};
        cols_ = 3;
        cardH_ = 74;
        rebuild();
    }

protected:
    void rebuild() override {
        items_.clear();
        for (int pl = 0; pl < int(g_.planets.size()); ++pl) {
            int o = g_.planets[pl].owner;
            if (tab_ == 0 ? o == human_ && human_ >= 0 : o >= 0) items_.push_back(pl);
        }
        if (tab_ == 1)
            std::stable_sort(items_.begin(), items_.end(),
                             [&](int a, int b) { return g_.planets[a].owner < g_.planets[b].owner; });
        count_ = int(items_.size());
    }
    std::string header() const override {
        if (tab_ != 0 || human_ < 0) return "Planets controlled by every player";
        int res = 0, inf = 0, rr = 0, ri = 0;
        for (int pl : items_) {
            res += planetResources(g_, pl);
            inf += planetInfluence(g_, pl);
            if (!g_.planets[pl].exhausted) {
                rr += planetResources(g_, pl);
                ri += planetInfluence(g_, pl);
            }
        }
        return std::to_string(items_.size()) + " planets \xC2\xB7 resources " + std::to_string(res) + " (" + std::to_string(rr) +
               " ready) \xC2\xB7 influence " + std::to_string(inf) + " (" + std::to_string(ri) + " ready)";
    }
    std::string emptyText() const override { return "You don't control any planets."; }

    void drawCard(Canvas& c, int i, Rect r, bool sel, uint32_t now) override {
        int pl = items_[i];
        const PlanetState& ps = g_.planets[pl];
        const PlanetDef& d = kPlanets[ps.def];
        cardFrame(c, r, sel, traitColor(d.trait), now);
        int x = r.x + 14;
        drawText(c, FONT_SMB, x, r.y + 6, fitText(FONT_SMB, d.name, r.w - 74), ps.exhausted ? theme::dim : theme::text);
        drawResourceIcon(c, r.x + r.w - 43, r.y + 15, 23, planetResources(g_, pl), FONT_SMB, false);
        drawResourceIcon(c, r.x + r.w - 17, r.y + 15, 23, planetInfluence(g_, pl), FONT_SMB, true);
        const bool ic = icons::available();
        int bx = x;
        const std::vector<Trait> traits = traitsOf(g_, pl);  // (owner 2026-11-13: as they are now - Terraform gives all three)
        if (ic)
            for (Trait tr : traits) {  // (Thunder's Edge: a second trait)
                drawTraitIcon(c, tr, bx - 1, r.y + 27, 15);
                bx += 16;
            }
        std::string sub;
        for (size_t k = 0; k < traits.size(); ++k) sub += std::string(k ? (ic ? "/" : " / ") : "") + traitName(traits[k]);
        if (d.flags & kPlanetStation) sub = "Space station";
        if (d.home) sub = "Home planet";
        if (!std::strcmp(d.key, "mr")) sub = "Galactic capital";
        drawText(c, FONT_XS, bx, r.y + 28, fitText(FONT_XS, sub, r.w / 2), !traits.empty() ? traitColor(traits[0]) : theme::faint);
        bx += textWidth(FONT_XS, fitText(FONT_XS, sub, r.w / 2)) + 8;
        const int skips = planetSkips(g_, pl);
        for (int k = 0; k < 4; ++k)
            for (int twice = 0; twice < ((d.skip == TechColor(k) && d.skip2 == TechColor(k)) ? 2 : 1); ++twice)
                if (skips & (1 << k)) {
                    if (ic) drawSkipIcon(c, TechColor(k), bx, r.y + 27, 15);
                    else c.fillCircle(float(bx + 5), float(r.y + 35), 5.f, kColorDot[k]);
                    bx += ic ? 17 : 14;
                }
        if (isLegendaryPlanet(g_, pl)) {
            if (!(ic && icons::draw(c, "legendary_complete", bx, r.y + 27, 15, ps.abilityExhausted ? 120 : 255))) drawText(c, FONT_XS, bx, r.y + 28, "\xE2\x98\x85", theme::gold);
            bx += ic ? 17 : 14;
        }
        if (!ps.attachments.empty())
            drawText(c, FONT_XS, bx, r.y + 28, hasAttachment(g_, pl, "dmz") ? "DMZ" : "+" + std::to_string(ps.attachments.size()) + " attached",
                     hasAttachment(g_, pl, "dmz") ? theme::text : theme::cyan);
        if (ps.owner >= 0)  // the owner's control token
            cards::controlToken(c, g_.players[ps.owner].faction, pcol(g_, ps.owner), r.x + r.w - 16, r.y + r.h - 15, 22);
        std::string st = tab_ == 1 ? playerName(g_, ps.owner) : "";
        if (ps.exhausted) st += std::string(st.empty() ? "" : " \xC2\xB7 ") + "Exhausted";
        else st += std::string(st.empty() ? "" : " \xC2\xB7 ") + "Ready";
        drawText(c, FONT_XS, x, r.y + r.h - 20, fitText(FONT_XS, st, r.w - 22),
                 tab_ == 1 ? pcol(g_, ps.owner) : ps.exhausted ? theme::faint : theme::green);
    }

    void drawDetail(Canvas& c, int i, Rect r) override {
        int pl = items_[i];
        const PlanetState& ps = g_.planets[pl];
        const PlanetDef& d = kPlanets[ps.def];
        uint32_t tc = traitColor(d.trait);
        detailFrame(c, r, tc);
        int x = r.x + 20, w = r.w - 40, y = r.y + 20;
        drawText(c, FONT_XL, x, y, d.name, theme::text);
        if (ps.owner >= 0)
            cards::controlToken(c, g_.players[ps.owner].faction, pcol(g_, ps.owner), r.x + r.w - 130, y + 16, 38);
        drawResourceIcon(c, r.x + r.w - 84, y + 16, 38, planetResources(g_, pl), FONT_LG, false);
        drawResourceIcon(c, r.x + r.w - 40, y + 16, 38, planetInfluence(g_, pl), FONT_LG, true);
        y += 40;
        const SystemState& s = g_.systems[ps.system];
        std::string where = s.home >= 0 ? playerName(g_, s.home) + "'s home system" : std::string("System ") + s.d().name;
        drawText(c, FONT_SM, x, y, where, theme::dim);
        y += 20;
        drawText(c, FONT_SMB, x, y,
                 std::to_string(planetResources(g_, pl)) + " resources \xC2\xB7 " + std::to_string(planetInfluence(g_, pl)) +
                     " influence",
                 theme::text);
        y += 20;
        const int ix = icons::available() ? 22 : 0;
        const std::vector<Trait> traits = traitsOf(g_, pl);  // (owner 2026-11-13: as they are now - Terraform gives all three)
        if (!traits.empty()) {
            if (ix)
                for (size_t k = 0; k < traits.size(); ++k) drawTraitIcon(c, traits[k], x + ix * int(k), y - 2, 18);
            std::string tn = traitName(traits[0]);
            for (size_t k = 1; k < traits.size(); ++k) tn += std::string(k + 1 == traits.size() ? " and " : ", ") + traitName(traits[k]);
            tn += traits.size() == 3 ? " planet (all three traits)" : traits.size() == 2 ? " planet (both traits)" : " planet";
            drawText(c, FONT_SM, x + ix * int(traits.size()), y, tn, tc);
            y += 20;
        }
        if (ps.terraform || ps.ulHero || ps.sleeper) {  // (Titans: what's on it beside exploration attachments)
            std::string on;
            if (ps.terraform) on = "Terraform attached: +1 resource, +1 influence, all three traits.";
            if (ps.ulHero) on += std::string(on.empty() ? "" : " ") + "Ul the Progenitor attached: +3 resources, +3 influence, Space Cannon 5 (x3).";
            if (ps.sleeper) on += std::string(on.empty() ? "" : " ") + "A Titans sleeper token (Awaken turns it into a PDS).";
            y = drawWrapped(c, FONT_SM, x, y, w, on, theme::gold);
        }
        if (d.flags & kPlanetStation) {  // Thunder's Edge: space stations (our wording)
            y = drawWrapped(c, FONT_SM, x, y, w,
                            "Space station: you take it by being the only player with ships in its system, and keep it when they leave. "
                            "No ground forces or structures on it; it doesn't count as a planet for votes or objectives. "
                            "+1 commodity value; trade with any other station holder; exhaust it to turn your commodities into trade goods.",
                            theme::cyan);
        }
        {
            const int skips = planetSkips(g_, pl);
            for (int k = 0; k < 4; ++k)
                if (skips & (1 << k)) {
                    if (d.skip == TechColor(k) && d.skip2 == TechColor(k)) {  // two of one colour
                        if (ix) drawSkipIcon(c, TechColor(k), x, y - 1, 18);
                        y = drawWrapped(c, FONT_SM, x + ix, y, w - ix,
                                        std::string("Two ") + kColorWord[k] + " specialties: exhaust it to ignore up to two " + kColorWord[k] +
                                            " prerequisites when researching.",
                                        kColorDot[k]);
                        continue;
                    }
                    if (ix) drawSkipIcon(c, TechColor(k), x, y - 1, 18);
                    y = drawWrapped(c, FONT_SM, x + ix, y, w - ix,
                                    std::string("Technology specialty (") + kColorWord[k] + "): exhaust it to ignore one " +
                                        kColorWord[k] + " prerequisite when researching.",
                                    kColorDot[k]);
                }
        }
        for (int16_t a : ps.attachments) {
            std::string txt = exploreText(a);
            if (txt.compare(0, 8, "Attach: ") == 0) txt = txt.substr(8);
            std::string t = "Attached - " + std::string(kExploreCards[a].name) + ": " + txt;
            y = drawWrapped(c, FONT_SM, x, y, w, t, theme::cyan, r.y + r.h - 90);
        }
        if (*legendaryName(ps.def)) {
            std::string t = std::string(ix ? "" : "\xE2\x98\x85 ") + legendaryName(ps.def) + " - " + legendaryText(ps.def);
            if (ps.owner >= 0) t += ps.abilityExhausted ? "  (exhausted)" : "  (ready)";
            if (ix) icons::draw(c, "legendary_complete", x, y - 1, 18, ps.owner >= 0 && ps.abilityExhausted ? 120 : 255);
            y = drawWrapped(c, FONT_SM, x + ix, y, w - ix, t, theme::gold, r.y + r.h - 70);
        } else if (isLegendaryPlanet(g_, pl)) {
            if (ix) icons::draw(c, "legendary_complete", x, y - 1, 18);
            y = drawWrapped(c, FONT_SM, x + ix, y, w - ix, std::string(ix ? "" : "\xE2\x98\x85 ") + "Legendary planet", theme::gold);
        }
        y += 6;
        drawText(c, FONT_SMB, x, y,
                 "Controlled by " + playerName(g_, ps.owner) + (ps.exhausted ? " \xC2\xB7 exhausted until the status phase" : " \xC2\xB7 ready"),
                 pcol(g_, ps.owner));
        y += 22;
        c.hline(x, x + w, y, theme::border);
        y += 8;
        bool any = false;
        for (int q = 0; q < g_.numPlayers; ++q) {
            const Force& f = ps.units[q];
            if (f.empty()) continue;
            std::string u;
            for (int t = 0; t < kUnitTypes; ++t)
                if (f.n[t]) u += (u.empty() ? "" : ", ") + std::to_string(f.n[t]) + " " + unitTypeName(UnitType(t), f.n[t] > 1);
            y = drawWrapped(c, FONT_SM, x, y, w, playerName(g_, q) + ": " + u, pcol(g_, q), r.y + r.h - 14);
            any = true;
        }
        if (!any) drawText(c, FONT_SM, x, y, "No units on this planet.", theme::faint);
    }

private:
    std::vector<int> items_;
};

// ======================================================================== units
class UnitScreen : public GridScreen {
public:
    UnitScreen(const Game& g, int human) : GridScreen(g, human) {
        title_ = "Units";
        for (int q = 0; q < g.numPlayers; ++q) {
            players_.push_back(q);
            tabs_.push_back(q == human ? std::string("Yours") : std::string(factionShortName(g.players[q].faction)));
        }
        if (human >= 0) {  // your units first
            for (size_t k = 0; k < players_.size(); ++k)
                if (players_[k] == human) {
                    std::swap(players_[k], players_[0]);
                    std::swap(tabs_[k], tabs_[0]);
                }
        }
        if (tabs_.size() > 4) {  // too many tabs to show: name only the current one
            fullTabs_ = tabs_;
            tabs_.assign(fullTabs_.size(), "");
        }
        cols_ = 4;
        cardH_ = 120;
        rebuild();
    }

    void draw(Canvas& c, uint32_t now) override {
        if (!fullTabs_.empty()) {
            for (size_t k = 0; k < tabs_.size(); ++k) tabs_[k] = int(k) == tab_ ? fullTabs_[k] : "\xC2\xB7";
        }
        GridScreen::draw(c, now);
    }

protected:
    int who() const { return players_.empty() ? 0 : players_[tab_]; }
    // (0.52.0) a player with a second flagship (the Ghemina's Lady) has one more card, next to the first flagship
    bool second() const { return g_.players[size_t(who())].unit[int(UnitType::Flagship2)] >= 0; }
    UnitType typeAt(int i) const {
        if (!second() || i <= int(UnitType::Flagship)) return UnitType(i);
        return i == int(UnitType::Flagship) + 1 ? UnitType::Flagship2 : UnitType(i - 1);
    }
    void rebuild() override { count_ = second() ? kUnitTypes : kUnitTypes - 1; }
    std::string header() const override {
        int p = who();
        return playerName(g_, p) + " \xC2\xB7 " + kFactions[g_.players[p].faction].name + keyText("  (L1/R1: other players)");
    }

    void drawCard(Canvas& c, int i, Rect r, bool sel, uint32_t now) override {
        int p = who();
        UnitType t = typeAt(i);
        const UnitDef& u = unitDef(g_, p, t);
        bool locked = u.tech >= 0 && t == UnitType::WarSun && unitsAvailable(g_, p, t) == 0 && unitsOnBoard(g_, p, t) == 0;
        cardFrame(c, r, sel, pcol(g_, p), now);
        // the unit's wireframe behind the text, as on the printed unit cards
        cards::unitWireframe(c, t, Rect{r.x + 10, r.y + 34, r.w - 16, r.h - 46}, mix(pcol(g_, p), rgb(225, 235, 255), 150),
                             locked ? 28 : 56);
        int x = r.x + 13, w = r.w - 18;
        auto lines = wrapText(FONT_SMB, u.name, w);
        int y = r.y + 5;
        for (size_t k = 0; k < lines.size() && k < 2; ++k) {
            drawText(c, FONT_SMB, x, y, lines[k], locked ? theme::dim : theme::text);
            y += 15;
        }
        if (lines.size() < 2) y += 4;
        auto stat = [&](int col, int row, const char* label, const std::string& v) {
            int sx = x + col * (w / 2), sy = y + row * 15;
            drawText(c, FONT_XS, sx, sy, label, theme::faint);
            drawText(c, FONT_XS, sx + 30, sy, v, theme::gold);
        };
        stat(0, 0, "Cost", costText(u));
        stat(1, 0, "Hit", rollText(u.combat, u.combatDice));
        stat(0, 1, "Move", u.move ? std::to_string(u.move) : "-");
        stat(1, 1, "Cap", u.capacity ? std::to_string(u.capacity) : "-");
        std::string ab;
        if (u.sustain) ab += "Sustain ";
        if (u.bombard) ab += "Bombard ";
        if (u.afb) ab += "Barrage ";
        if (u.cannon) ab += "Cannon ";
        if (u.shield) ab += "Shield ";
        if (u.production) ab += "Prod ";
        if (locked) ab = "Needs War Sun tech";
        drawText(c, FONT_XS, x, r.y + r.h - 34, fitText(FONT_XS, ab, w), locked ? theme::red : theme::cyan);
        int on = unitsOnBoard(g_, p, t);
        int lim = unitLimitOf(g_, p, t);
        std::string cnt = lim >= 99 ? std::to_string(on) + " on board" : std::to_string(on) + " / " + std::to_string(lim) + " on board";
        drawText(c, FONT_XS, x, r.y + r.h - 19, cnt, theme::dim);
    }

    void drawDetail(Canvas& c, int i, Rect r) override {
        int p = who();
        UnitType t = typeAt(i);
        int idx = g_.players[p].unit[int(t)];
        if (idx < 0) return;
        const UnitDef& u = kUnits[idx];
        detailFrame(c, r, pcol(g_, p));
        cards::unitWireframe(c, t, Rect{r.x + 24, r.y + 120, r.w - 48, r.h - 175}, mix(pcol(g_, p), rgb(225, 235, 255), 150), 58);
        int x = r.x + 20, w = r.w - 40, y = r.y + 20;
        drawText(c, FONT_XL, x, y, u.name, theme::text);
        y += 34;
        std::string kind = std::string(unitTypeName(t)) + (isShip(t) ? " \xC2\xB7 ship" : isGroundForce(t) ? " \xC2\xB7 ground force" : " \xC2\xB7 structure");
        if (u.faction >= 0) kind += " \xC2\xB7 " + std::string(factionShortName(u.faction)) + " special unit";
        drawText(c, FONT_SM, x, y, kind, pcol(g_, p));
        y += 22;
        // stat row
        const char* labels[4] = {"Cost", "Combat", "Move", "Capacity"};
        std::string vals[4] = {costText(u), rollText(u.combat, u.combatDice), u.move ? std::to_string(u.move) : "-",
                               u.capacity && !(t == UnitType::SpaceDock && !u.spaceOnly) ? std::to_string(u.capacity) : "-"};  // (a dock carries nothing)
        for (int k = 0; k < 4; ++k) {
            int bx = x + k * (w / 4);
            c.fillRoundRect(bx, y, w / 4 - 8, 44, 6, rgb(26, 34, 58));
            drawTextAligned(c, FONT_XS, bx, y + 4, w / 4 - 8, Align::Center, labels[k], theme::faint);
            drawTextAligned(c, FONT_LG, bx, y + 18, w / 4 - 8, Align::Center, vals[k], theme::gold);
        }
        y += 54;
        std::vector<std::string> notes;
        if (u.combat) notes.push_back("Combat " + rollText(u.combat, u.combatDice) + ": rolls " + std::to_string(std::max<int>(1, u.combatDice)) +
                                      (u.combatDice > 1 ? " dice" : " die") + " each combat round; each " +
                                      std::to_string(u.combat) + " or higher is a hit.");
        if (u.cost2 == 1) notes.push_back("Costs 1 resource for 2 of them.");
        if (u.move)
            notes.push_back("Move " + std::to_string(u.move) + ": can move up to " + std::to_string(u.move) +
                            (u.move == 1 ? " system away." : " systems away."));
        // (the shared technologies review, 2026-11-14) a space dock on a planet carries nothing: its "capacity" is the fighters
        // in its system that don't count against your ships' capacity (its card text says so when it has one)
        const bool dockFighters = t == UnitType::SpaceDock && !u.spaceOnly;
        if (u.capacity && !dockFighters)
            notes.push_back("Capacity " + std::to_string(u.capacity) + ": carries up to " + std::to_string(u.capacity) + " fighters and ground forces.");
        else if (u.capacity && !*unitAbilityText(idx))
            notes.push_back("Up to " + std::to_string(u.capacity) + " fighters in this system do not count against your ships' capacity.");
        if (u.sustain) notes.push_back("Sustain Damage: when it would be hit, it can become damaged instead of destroyed. "
                                       "It is repaired in the status phase.");
        if (u.bombard) notes.push_back("Bombardment " + rollText(u.bombard, u.bombardDice) + ": before landing on a planet, it fires at "
                                       "the defending ground forces; each " + std::to_string(u.bombard) +
                                       "+ destroys one. Planetary shields stop it.");
        if (u.afb) notes.push_back("Anti-fighter barrage " + rollText(u.afb, u.afbDice) + ": at the start of space combat it fires at "
                                   "enemy fighters; each " + std::to_string(u.afb) + "+ destroys one.");
        if (u.cannon) notes.push_back("Space cannon " + rollText(u.cannon, u.cannonDice) + ": fires at enemy ships that move into "
                                      "its system" + std::string(u.deepCannon ? " or an adjacent one" : "") +
                                      ", and at ground forces landing on its planet.");
        if (u.shield) notes.push_back("Planetary shield: its planet can't be bombarded (war suns ignore this).");
        if (u.ignoresShield) notes.push_back("Ignores planetary shields when bombarding.");
        if (u.production) notes.push_back(std::string("Production ") + (u.productionPlusResources ? "(planet's resources + " : "(") +
                                          std::to_string(u.production) + "): can build that many units in its system.");
        const bool implemented = factionImplemented(g_.players[p].faction) || u.faction < 0;
        if (u.spaceOnly && !(*unitAbilityText(idx) && implemented)) notes.push_back("Sits in the space area instead of on a planet.");
        if (*unitAbilityText(idx) && implemented)
            notes.push_back(unitAbilityText(idx));
        else if (!implemented && (u.faction >= 0 || t == UnitType::Flagship || t == UnitType::Mech))  // (the Saar review: only
            // for factions not built yet - Son of Ragh, Super Dreadnought I, ... have no text on their cards)
            notes.push_back("Its special faction ability isn't active yet (a later update adds it).");
        for (const std::string& n : notes) {
            if (y > r.y + r.h - 70) break;
            y = drawWrapped(c, FONT_SM, x, y, w, "\xE2\x80\xA2 " + n, theme::text, r.y + r.h - 50);
            y += 2;
        }
        // upgrade
        std::string up;
        for (int k = 0; k < kUnitCount; ++k) {
            if (kUnits[k].upgradeOf != idx) continue;
            const UnitDef& v = kUnits[k];
            std::string techName = v.tech >= 0 ? kTechs[v.tech].name : v.name;
            up = "Upgrade: " + techName + " - cost " + costText(v) + ", combat " + rollText(v.combat, v.combatDice) + ", move " +
                 (v.move ? std::to_string(v.move) : "-") + ", capacity " + std::to_string(v.capacity);
            if (v.production) up += ", production " + std::to_string(v.production);
            break;
        }
        if (u.tech >= 0 && u.upgradeOf >= 0) up = "Upgraded with " + std::string(kTechs[u.tech].name) + ".";
        if (t == UnitType::WarSun && u.tech >= 0 && !g_.players[p].hasTech(u.tech) && u.upgradeOf < 0)
            up = "Can't be built until you research " + std::string(kTechs[u.tech].name) + ".";
        int on = unitsOnBoard(g_, p, t), lim = unitLimitOf(g_, p, t);
        std::string cnt = lim >= 99 ? std::to_string(on) + " on the board (no limit)"
                                    : std::to_string(on) + " of " + std::to_string(lim) + " on the board";
        drawText(c, FONT_SMB, x, r.y + r.h - 44, cnt, theme::dim);
        if (!up.empty()) drawText(c, FONT_SM, x, r.y + r.h - 24, fitText(FONT_SM, up, w), theme::cyan);
    }

private:
    std::vector<int> players_;
    std::vector<std::string> fullTabs_;
};

// ======================================================================== action cards & notes
class HandScreen : public GridScreen {
public:
    HandScreen(const Game& g, int human) : GridScreen(g, human) {
        title_ = "Your cards";
        tabs_ = {"Action cards", "Promissory notes", "Relics", "Discards"};  // discard pile (0.30.0, owner)
        // 0.44.1 (owner: "I can't see my unplayed plot cards anywhere ... Same issue with my Axis orders"): a fifth tab
        // with the cards only some factions have
        if (human >= 0 && !factionCards().empty()) tabs_.push_back("Faction");
        cols_ = 2;
        cardH_ = 92;
        rebuild();
    }

protected:
    void rebuild() override {
        items_.clear();
        if (human_ < 0) {
            count_ = 0;
            return;
        }
        const Player& p = g_.players[human_];
        if (tab_ == 4) {
            items_ = factionCards();
        } else if (tab_ == 3) {  // the action card discard pile, newest first
            for (auto it = g_.actionDiscard.rbegin(); it != g_.actionDiscard.rend(); ++it) items_.push_back(*it);
        } else if (tab_ == 0)
            for (int16_t c : p.actionCards) items_.push_back(c);
        else if (tab_ == 1) {
            for (int16_t n : p.notes) items_.push_back(n);
            for (int16_t n : p.notesInPlay) items_.push_back(10000 + n);
            // M'aban (Naalu commander): the neighbours' promissory notes
            if (commanderUsable(g_, human_, "naalu"))
                for (int q = 0; q < g_.numPlayers; ++q)
                    if (q != human_ && !g_.players[q].eliminated && areNeighbours(g_, human_, q))
                        for (int16_t n : g_.players[q].notes) items_.push_back(kSeen + q * 100 + n);
        } else {
            for (int16_t r : p.relics) items_.push_back(r);
            for (int16_t f : p.fragments) items_.push_back(kFragmentBase + f);
            for (int k = 0; k < p.enigmatic; ++k) items_.push_back(kEnigmatic);
        }
        count_ = int(items_.size());
    }
    std::string header() const override {
        if (human_ < 0) return "";
        if (tab_ == 0)
            return std::to_string(g_.players[human_].actionCards.size()) + " of " + std::to_string(actionCardLimit(g_, human_)) +
                   " action cards. They are played when their timing comes up - the game asks you.";
        if (tab_ == 1) return "Your own notes are for giving away; notes from other players can be played.";
        if (tab_ == 4) return "Cards of your faction (and ones you bought from another faction), wherever they are now.";
        if (tab_ == 3)
            return std::to_string(g_.actionDiscard.size()) + " action card" + (g_.actionDiscard.size() == 1 ? "" : "s") +
                   " played or discarded so far, the latest first.";
        const Player& p = g_.players[human_];
        const int frags = fragmentCount(g_, human_), charts = starChartCount(g_, human_);
        return std::to_string(p.relics.size()) + " relic" + (p.relics.size() == 1 ? "" : "s") + " \xC2\xB7 " + std::to_string(frags) +
               " relic fragment" + (frags == 1 ? "" : "s") +
               (charts ? " \xC2\xB7 " + std::to_string(charts) + " Star Chart" + (charts == 1 ? "" : "s") : std::string()) +
               ". 3 fragments of one type (unknown ones count as any) buy a relic.";
    }
    const char* titleIcon() const override {
        return tab_ == 0 || tab_ == 3 ? "action_card" : tab_ == 1 ? "promissory" : tab_ == 4 ? nullptr : "relic";
    }
    std::string emptyText() const override {
        if (tab_ == 4) return "No faction cards.";
        if (tab_ == 0) return "No action cards. You draw one each status phase (Politics draws more).";
        if (tab_ == 3) return "No action cards have been discarded yet.";
        if (tab_ == 1) return "No promissory notes.";
        return "No relics or fragments yet. Exploring planets can turn up relic fragments.";
    }
    static constexpr int kFragmentBase = 30000, kEnigmatic = 40000, kSeen = 20000;
    // ---- the faction cards (0.44.1): plots (Firmament / Obsidian), Axis Orders, Olradin policies, Li-Zho traps,
    // ocean cards (Deepwrought) and the faction's breakthrough (Thunder's Edge)
    static constexpr int kPlotBase = 50000, kOrderBase = 51000, kPolicyBase = 52000, kTrapBase = 53000, kOceanBase = 54000,
                         kBreakthrough = 55000;
    std::vector<int> factionCards() const {
        std::vector<int> v;
        if (human_ < 0 || human_ >= g_.numPlayers) return v;
        const Player& p = g_.players[size_t(human_)];
        const bool te = (g_.modules & kModThundersEdge) != 0, ds = (g_.modules & kModDiscordant) != 0;
        if (te && (isFaction(g_, human_, "firmament") || isFaction(g_, human_, "obsidian")))
            for (int k = 0; k < kPlotCount; ++k) v.push_back(kPlotBase + k);
        if (ds) {
            const bool axis = isFaction(g_, human_, "axis");
            for (int i = 0; i < 8; ++i)
                if (axis || g_.dsOrders[i] == human_) v.push_back(kOrderBase + i);
            if (isFaction(g_, human_, "olradin"))
                for (int k = 0; k < 3; ++k) v.push_back(kPolicyBase + k);
            if (isFaction(g_, human_, "lizho"))
                for (int k = 0; k < 5; ++k) v.push_back(kTrapBase + k);
        }
        for (int k = 0; k < p.oceans; ++k) v.push_back(kOceanBase + k);
        if (breakthroughsOn(g_) && p.faction >= 0 && breakthroughOf(kFactions[size_t(p.faction)].key)) v.push_back(kBreakthrough);
        return v;
    }
    struct FCard {
        std::string name, kind, status, text, use;
        uint32_t col = 0;
        bool dim = false;
        int faction = -1;
    };
    std::string names(uint32_t bits) const {
        std::string out;
        for (int q = 0; q < g_.numPlayers; ++q)
            if ((bits >> q) & 1) out += (out.empty() ? "" : ", ") + playerName(g_, q);
        return out;
    }
    FCard factionCard(int id) const {
        FCard f;
        const Player& p = g_.players[size_t(human_)];
        f.faction = p.faction;
        if (id >= kBreakthrough) {
            const BreakthroughDef* b = breakthroughOf(kFactions[size_t(p.faction)].key);
            f.name = b ? b->name : "Breakthrough";
            f.kind = "BREAKTHROUGH";
            f.col = rgb(236, 196, 90);
            f.dim = !p.breakthrough || p.btExhausted;
            f.status = !p.breakthrough ? "Not gained yet" : p.btExhausted ? "Gained - exhausted until the status phase" : "Gained";
            f.text = b ? b->text : "";
            if (b && b->a != TechColor::None)
                f.use = std::string("Synergy (once gained): your ") + colorName(b->a) + " and " + colorName(b->b) +
                        " technologies stand in for each other as prerequisites.";
        } else if (id >= kOceanBase) {
            const int k = id - kOceanBase;
            f.name = "Ocean card";
            f.kind = "OCEANBOUND Â· 1 resource or 1 influence";
            f.col = rgb(70, 150, 230);
            f.dim = k < p.oceansExhausted;
            f.status = f.dim ? "Exhausted - readied in the status phase" : "Ready to spend";
            f.text = "Spend it like a planet: 1 resource or 1 influence. The game offers it when you pay.";
        } else if (id >= kTrapBase) {
            const int k = id - kTrapBase, at = g_.dsTrap[k];
            f.name = trapName(k);
            f.kind = "TRAP";
            f.col = rgb(60, 170, 160);
            f.status = at >= 0 && at < int(g_.planets.size()) ? std::string("Set on ") + kPlanets[size_t(g_.planets[size_t(at)].def)].name + " (face down)"
                                                              : "In your reinforcements";
            f.text = trapText(k);
        } else if (id >= kPolicyBase) {
            static const char* kPlus[3] = {"Connect", "Preserve", "Empower"};
            static const char* kMinus[3] = {"Control", "Plunder", "Exploit"};
            static const char* kCard[3] = {"The People", "The Environment", "The Economy"};
            const int k = id - kPolicyBase;
            const bool minus = (p.dsPolicy >> k) & 1;
            f.name = std::string(kCard[k]) + ": " + (minus ? kMinus[k] : kPlus[k]);
            f.kind = minus ? "POLICY Â· - side" : "POLICY Â· + side";
            f.col = minus ? rgb(220, 90, 80) : rgb(90, 200, 120);
            f.status = minus ? "On its - side" : "On its + side";
            f.text = olradinPolicyText(k, minus);
        } else if (id >= kOrderBase) {
            const int i = id - kOrderBase, at = g_.dsOrders[i], ax = playerOfFaction(g_, "axis");
            const bool used = (g_.dsOrderEx >> i) & 1;
            f.name = axisOrderName(i);
            f.kind = "AXIS ORDER";
            f.col = rgb(200, 150, 80);
            if (ax >= 0) f.faction = g_.players[size_t(ax)].faction;
            f.dim = used || at < 0;
            if (human_ == ax)
                f.status = at < 0 ? "In your reinforcements - Military Industrial Complex buys it with commodities"
                           : at == human_ ? (used ? "Used - back to you at the status phase" : "In your play area - for sale in deals")
                                          : "Sold to " + playerName(g_, at) + (used ? " (used - comes back at the status phase)" : "");
            else
                f.status = used ? "Used - goes back to the Axis at the status phase" : "In your play area - bought from the Axis";
            f.text = axisOrderText(i);
        } else {
            const int k = id - kPlotBase;
            const bool in = (p.plotsIn >> k) & 1, obs = isFaction(g_, human_, "obsidian");
            f.name = plotName(k);
            f.kind = obs ? "PLOT Â· the Obsidian" : "PLOT Â· the Firmament";
            f.col = rgb(150, 90, 210);
            f.dim = !in;
            const std::string who = names(p.plotTok[size_t(k)]);
            f.status = !in ? "Not played yet - in your hand"
                       : obs ? "Revealed - puppets: " + (who.empty() ? std::string("nobody") : who)
                             : "In your play area, face down - " + (who.empty() ? std::string("no tokens") : who + "'s token");
            f.text = plotText(k);
            f.use = obs ? "Its puppets (the players whose control tokens are on it) suffer it."
                        : "Plots go face down with another player's control token (Plots Within Plots, Black Ops). Puppets of the Blade "
                          "reveals them all.";
        }
        return f;
    }
    void drawFactionCard(Canvas& c, int id, Rect r, bool sel, uint32_t now) const {
        if (id >= kBreakthrough && btcard::has(g_, human_)) {  // 0.44.3: the breakthrough card as printed
            btcard::draw(c, g_.players[size_t(human_)].faction, r, btcard::stateOf(g_, human_), sel, now);
            return;
        }
        const FCard f = factionCard(id);
        cardFrame(c, r, sel, f.col, now);
        int iw = 0;
        if (f.faction >= 0 && icons::available()) {
            drawFactionIcon(c, f.faction, r.x + r.w - 27, r.y + 5, 20, f.dim ? 120 : 255);
            iw = 24;
        }
        drawText(c, FONT_SMB, r.x + 14, r.y + 6, fitText(FONT_SMB, f.name, r.w - 22 - iw), f.dim ? theme::dim : theme::text);
        drawText(c, FONT_XS, r.x + 14, r.y + 23, fitText(FONT_XS, f.status, r.w - 22), f.dim ? theme::faint : shade(f.col, 1.25f));
        drawWrapped(c, FONT_XS, r.x + 14, r.y + 39, r.w - 22, f.text, theme::dim, r.y + r.h - 4);
    }
    void drawFactionDetail(Canvas& c, int id, Rect r) const {
        if (id >= kBreakthrough && btcard::has(g_, human_)) {
            btcard::drawLarge(c, g_, human_, human_, r, now_);
            return;
        }
        const FCard f = factionCard(id);
        const int x = r.x + 20, w = r.w - 40;
        detailFrame(c, r, f.col);
        int iw = 0;
        if (f.faction >= 0 && icons::available()) {
            drawFactionIcon(c, f.faction, r.x + r.w - 62, r.y + 18, 42);
            iw = 48;
        }
        int y = drawWrapped(c, FONT_XL, x, r.y + 20, w - iw, f.name, theme::text);
        drawText(c, FONT_SMB, x, y + 4, f.kind, shade(f.col, 1.3f));
        y += 26;
        y = drawWrapped(c, FONT_SM, x, y, w, f.status, f.dim ? theme::dim : theme::gold) + 10;
        drawWrapped(c, FONT_MD, x, y, w, f.text, theme::text);
        if (!f.use.empty()) drawWrapped(c, FONT_SM, x, r.y + r.h - 60, w, f.use, theme::dim);
    }
    static const char* fragmentIcon(int card) {
        static const char* k[4] = {"cultural", "hazardous", "industrial", "frontier"};
        return card >= 0 && card < kExploreCardCount ? k[int(kExploreCards[card].type) & 3] : "frontier";
    }
    static uint32_t exploreColor(int card) {
        static const uint32_t k[4] = {rgb(70, 130, 220), rgb(210, 70, 60), rgb(70, 170, 90), rgb(150, 90, 200)};
        return card >= 0 && card < kExploreCardCount ? k[int(kExploreCards[card].type)] : theme::dim;
    }
    static const char* relicUse(int r) {
        const char* k = kRelics[r].key;
        if (!std::strcmp(k, "codex") || !std::strcmp(k, "stellarconverter") || !std::strcmp(k, "decrypted_cartoglyph") ||
            !std::strcmp(k, "throne_of_the_false_emperor"))
            return "Use it as your action: choose it from the action menu (Component action) on your turn.";
        if (!std::strcmp(k, "shard") || !std::strcmp(k, "obsidian") || !std::strcmp(k, "boon_of_the_cerulean_god") ||
            !std::strcmp(k, "starfall_array"))
            return "Always active while you hold it.";
        if (!std::strcmp(k, "e6-g0_network"))
            return "Always active: 2 more action cards. To draw one, choose it from the action menu on your turn (it doesn't use your action).";
        return "When its moment comes, the game asks whether you want to use it.";
    }
    void drawRelicCard(Canvas& c, int id, Rect r, bool sel, uint32_t now) {
        const Player& p = g_.players[human_];
        if (id >= kEnigmatic) {
            cardFrame(c, r, sel, exploreColor(findExploreCard("ed")), now);
            cornerIcon(c, r, "frontier", theme::text);
            drawText(c, FONT_SMB, r.x + 14, r.y + 6, "Enigmatic Device", theme::text);
            drawText(c, FONT_XS, r.x + 14, r.y + 23, "Frontier exploration \xC2\xB7 action", rgb(190, 150, 236));
            drawWrapped(c, FONT_XS, r.x + 14, r.y + 39, r.w - 22, exploreText(findExploreCard("ed")), theme::dim, r.y + r.h - 4);
        } else if (id >= kFragmentBase && kExploreCards[id - kFragmentBase].kind == ExploreKind::Chart) {  // Uncharted Space
            const int f = id - kFragmentBase;
            cardFrame(c, r, sel, exploreColor(f), now);
            int iw = cornerIcon(c, r, fragmentIcon(f), theme::text);
            drawText(c, FONT_SMB, r.x + 14, r.y + 6, fitText(FONT_SMB, kExploreCards[f].name, r.w - 22 - iw), theme::text);
            drawText(c, FONT_XS, r.x + 14, r.y + 23, "Star Chart \xC2\xB7 can be traded", shade(exploreColor(f), 1.4f));
            drawWrapped(c, FONT_XS, r.x + 14, r.y + 39, r.w - 22,
                        "Purge 2 for a new system at the edge of the board. You have " + std::to_string(starChartCount(g_, human_)) + ".",
                        theme::dim, r.y + r.h - 4);
        } else if (id >= kFragmentBase) {
            int f = id - kFragmentBase;
            cardFrame(c, r, sel, exploreColor(f), now);
            int iw = cornerIcon(c, r, fragmentIcon(f), theme::text);
            drawText(c, FONT_SMB, r.x + 14, r.y + 6, fitText(FONT_SMB, kExploreCards[f].name, r.w - 22 - iw), theme::text);
            std::string t = kExploreCards[f].type == ExploreType::Frontier ? "Unknown - counts as any type"
                                                                           : capitalize(exploreTypeName(kExploreCards[f].type)) + " fragment";
            drawText(c, FONT_XS, r.x + 14, r.y + 23, t, shade(exploreColor(f), 1.4f));
            int same = relicFragments(g_, human_, kExploreCards[f].type == ExploreType::Frontier ? -1 : int(kExploreCards[f].type));
            drawWrapped(c, FONT_XS, r.x + 14, r.y + 39, r.w - 22,
                        "Purge 3 of a type to gain a relic. You can count " + std::to_string(same) + " for this type.", theme::dim,
                        r.y + r.h - 4);
        } else {
            bool ex = p.relicExhausted(id);
            cardFrame(c, r, sel, rgb(222, 180, 70), now);
            int iw = cornerIcon(c, r, "relic", theme::text);
            drawText(c, FONT_SMB, r.x + 14, r.y + 6, fitText(FONT_SMB, kRelics[id].name, r.w - 22 - iw), ex ? theme::dim : theme::text);
            drawText(c, FONT_XS, r.x + 14, r.y + 23, ex ? "Relic \xC2\xB7 exhausted until the status phase" : "Relic",
                     ex ? theme::faint : theme::gold);
            drawWrapped(c, FONT_XS, r.x + 14, r.y + 39, r.w - 22, relicText(id), theme::dim, r.y + r.h - 4);
        }
    }
    void drawRelicDetail(Canvas& c, int id, Rect r) {
        int x = r.x + 20, w = r.w - 40, y = r.y + 20;
        const Player& p = g_.players[human_];
        std::string name, kind, text, use;
        const char* ic = "relic";
        uint32_t col;
        if (id >= kEnigmatic) {
            int e = findExploreCard("ed");
            col = exploreColor(e);
            ic = "frontier";
            name = "Enigmatic Device";
            kind = "FRONTIER EXPLORATION CARD";
            text = exploreText(e);
            use = "Use it as your action: choose it from the action menu (Component action) on your turn.";
        } else if (id >= kFragmentBase && kExploreCards[id - kFragmentBase].kind == ExploreKind::Chart) {
            const int f = id - kFragmentBase;
            col = exploreColor(f);
            ic = fragmentIcon(f);
            name = kExploreCards[f].name;
            kind = std::string("STAR CHART \xC2\xB7 ") + capitalize(exploreTypeName(kExploreCards[f].type)) + " exploration (Uncharted Space)";
            text = exploreText(f);
            use = "With 2 of them, choose \"Star Charts\" from the action menu (Component action). They can be traded in a deal.";
        } else if (id >= kFragmentBase) {
            int f = id - kFragmentBase;
            col = exploreColor(f);
            ic = fragmentIcon(f);
            name = kExploreCards[f].name;
            kind = std::string("RELIC FRAGMENT \xC2\xB7 ") +
                   (kExploreCards[f].type == ExploreType::Frontier ? "unknown (counts as any type)" : capitalize(exploreTypeName(kExploreCards[f].type)));
            text = "As your action, purge 3 relic fragments of the same type to gain the top card of the relic deck. Unknown "
                   "fragments can stand in for any type.";
            use = "When you have enough, choose \"Relic fragments\" from the action menu (Component action).";
        } else {
            col = rgb(222, 180, 70);
            name = kRelics[id].name;
            kind = p.relicExhausted(id) ? "RELIC \xC2\xB7 exhausted until the status phase" : "RELIC \xC2\xB7 ready";
            text = relicText(id);
            use = relicUse(id);
        }
        detailFrame(c, r, col);
        if (icons::draw(c, ic, r.x + r.w - 62, r.y + 18, 42)) w -= 48;
        y = drawWrapped(c, FONT_XL, x, y, w, name, theme::text);
        w = r.w - 40;
        drawText(c, FONT_SMB, x, y + 4, kind, shade(col, 1.3f));
        y += 30;
        drawWrapped(c, FONT_MD, x, y, w, text, theme::text);
        drawWrapped(c, FONT_SM, x, r.y + r.h - 60, w, use, theme::dim);
    }
    void drawCard(Canvas& c, int i, Rect r, bool sel, uint32_t now) override {
        int id = items_[i];
        if (tab_ == 4) {
            drawFactionCard(c, id, r, sel, now);
        } else if (tab_ == 2) {
            drawRelicCard(c, id, r, sel, now);
        } else if (tab_ == 0 || tab_ == 3) {
            cardFrame(c, r, sel, rgb(200, 70, 60), now);
            int iw = cornerIcon(c, r, "action_card", rgb(240, 160, 140), 18);
            drawText(c, FONT_SMB, r.x + 14, r.y + 6, fitText(FONT_SMB, kActionCards[id].name, r.w - 22 - iw), theme::text);
            drawText(c, FONT_XS, r.x + 14, r.y + 23, fitText(FONT_XS, actionCardWhen(id), r.w - 22), rgb(240, 160, 140));
            drawWrapped(c, FONT_XS, r.x + 14, r.y + 39, r.w - 22, actionCardText(id), theme::dim, r.y + r.h - 4);
        } else {
            const bool seen = id >= kSeen;
            bool inPlay = !seen && id >= 10000;
            int n = seen ? (id - kSeen) % 100 : inPlay ? id - 10000 : id;
            int owner = noteOwner(n);
            cardFrame(c, r, sel, pcol(g_, owner), now);
            int iw = 0;
            if (owner >= 0 && icons::available()) {
                drawFactionIcon(c, g_.players[owner].faction, r.x + r.w - 27, r.y + 5, 20);
                iw = 24;
            }
            drawText(c, FONT_SMB, r.x + 14, r.y + 6, fitText(FONT_SMB, noteNameOf(g_, n), r.w - 22 - iw), theme::text);
            std::string who = seen ? "Held by " + playerName(g_, (id - kSeen) / 100) + " (seen with M'aban)"
                              : owner == human_ ? "Yours - to give away" : "From " + playerName(g_, owner);
            if (inPlay) who += " (in play)";
            drawText(c, FONT_XS, r.x + 14, r.y + 23, who, pcol(g_, owner));
            drawWrapped(c, FONT_XS, r.x + 14, r.y + 39, r.w - 22, noteTextOf(g_, n), theme::dim, r.y + r.h - 4);
        }
    }
    void drawDetail(Canvas& c, int i, Rect r) override {
        int id = items_[i];
        int x = r.x + 20, w = r.w - 40, y = r.y + 20;
        if (tab_ == 4) {
            drawFactionDetail(c, id, r);
        } else if (tab_ == 2) {
            drawRelicDetail(c, id, r);
        } else if (tab_ == 0 || tab_ == 3) {
            detailFrame(c, r, rgb(200, 70, 60));
            int iw = icons::draw(c, "action_card", r.x + r.w - 58, r.y + 18, 38, 255, rgb(240, 160, 140)) ? 46 : 0;
            y = drawWrapped(c, FONT_XL, x, y, w - iw, kActionCards[id].name, theme::text);
            drawText(c, FONT_SMB, x, y + 4, "ACTION CARD", rgb(240, 160, 140));
            y += 28;
            drawText(c, FONT_SMB, x, y, "When:", theme::gold);
            y = drawWrapped(c, FONT_MD, x + 60, y, w - 60, actionCardWhen(id), theme::text) + 10;
            drawText(c, FONT_SMB, x, y, "Effect:", theme::gold);
            y = drawWrapped(c, FONT_MD, x + 60, y, w - 60, actionCardText(id), theme::text) + 14;
            drawWrapped(c, FONT_SM, x, r.y + r.h - 60, w,
                        tab_ == 3 ? "In the discard pile."
                        : actionCardIsComponent(id) ? "Play it as your action: choose it from the action menu on your turn."
                                                    : "When this moment comes, the game asks whether you want to play it.",
                        theme::dim);
        } else {
            int n = id >= kSeen ? (id - kSeen) % 100 : id >= 10000 ? id - 10000 : id;
            detailFrame(c, r, pcol(g_, noteOwner(n)));
            int iw = 0;
            if (noteOwner(n) >= 0 && icons::available()) {
                drawFactionIcon(c, g_.players[noteOwner(n)].faction, r.x + r.w - 62, r.y + 18, 42);
                iw = 48;
            }
            y = drawWrapped(c, FONT_XL, x, y, w - iw, noteNameOf(g_, n), theme::text);
            drawText(c, FONT_SMB, x, y + 4, "PROMISSORY NOTE \xC2\xB7 " + playerName(g_, noteOwner(n)), pcol(g_, noteOwner(n)));
            y += 30;
            drawWrapped(c, FONT_MD, x, y, w, noteTextOf(g_, n), theme::text);
            drawWrapped(c, FONT_SM, x, r.y + r.h - 60, w,
                        keyText("Promissory notes change hands in deals (MENU, Negotiations) and through cards like Diplomatic Pressure."),
                        theme::dim);
        }
    }

private:
    std::vector<int> items_;
};

// ======================================================================== laws & agendas
class LawScreen : public GridScreen {
public:
    LawScreen(const Game& g, int human) : GridScreen(g, human) {
        title_ = "Laws";
        tabs_ = {"In play", "All agendas"};
        if (human >= 0 && commanderUsable(g, human, "naalu")) tabs_.push_back("M'aban");
        cols_ = 2;
        cardH_ = 92;
        rebuild();
    }

protected:
    void rebuild() override {
        items_.clear();
        if (tab_ == 0) {
            for (size_t i = 0; i < g_.laws.size(); ++i) items_.push_back(int(i));
        } else if (tab_ == 2) {  // M'aban: top and bottom of the agenda deck (top = back)
            if (!g_.agendaDeck.empty()) items_.push_back(g_.agendaDeck.back());
            if (g_.agendaDeck.size() > 1) items_.push_back(g_.agendaDeck.front());
        } else {
            for (int a = 0; a < kAgendaCount; ++a)
                if (agendaImplemented(a)) items_.push_back(a);
        }
        count_ = int(items_.size());
    }
    std::string header() const override {
        if (tab_ == 2) return "M'aban (your commander): the top and bottom cards of the agenda deck";
        if (tab_ == 1) return "Every agenda in the deck (" + std::to_string(g_.agendaDeck.size()) + " still to come)";
        if (!g_.custodiansRemoved) return "No agenda phase yet: it starts after someone removes the custodians token from Mecatol Rex.";
        return std::to_string(g_.laws.size()) + " laws in play";
    }
    std::string emptyText() const override { return "No laws in play."; }
    const char* titleIcon() const override { return "agenda"; }
    int agendaOf(int i) const { return tab_ == 0 ? g_.laws[items_[i]].agenda : items_[i]; }
    void drawCard(Canvas& c, int i, Rect r, bool sel, uint32_t now) override {
        int a = agendaOf(i);
        cardFrame(c, r, sel, kAgendas[a].law ? rgb(70, 120, 220) : rgb(110, 110, 150), now);
        int iw = cornerIcon(c, r, "agenda", rgb(150, 180, 240), 18);
        drawText(c, FONT_SMB, r.x + 14, r.y + 6, fitText(FONT_SMB, kAgendas[a].name, r.w - 22 - iw), theme::text);
        std::string sub = kAgendas[a].law ? "Law" : "Directive";
        if (tab_ == 2) sub += i == 0 ? " \xC2\xB7 top of the deck (next)" : " \xC2\xB7 bottom of the deck";
        if (tab_ == 0) {
            const Law& l = g_.laws[items_[i]];
            if (l.owner >= 0) sub += " \xC2\xB7 held by " + playerName(g_, l.owner);
            if (l.target >= 0 && kAgendas[a].elect == Elect::ScoredSecret) sub += std::string(" \xC2\xB7 ") + kObjectives[l.target].name;
        }
        drawText(c, FONT_XS, r.x + 14, r.y + 23, fitText(FONT_XS, sub, r.w - 22), rgb(150, 180, 240));
        drawWrapped(c, FONT_XS, r.x + 14, r.y + 39, r.w - 22, agendaText(a, false), theme::dim, r.y + r.h - 4);
    }
    void drawDetail(Canvas& c, int i, Rect r) override {
        int a = agendaOf(i);
        detailFrame(c, r, kAgendas[a].law ? rgb(70, 120, 220) : rgb(110, 110, 150));
        int x = r.x + 20, w = r.w - 40, y = r.y + 20;
        int iw = icons::draw(c, "agenda", r.x + r.w - 60, r.y + 18, 40, 255, rgb(150, 180, 240)) ? 48 : 0;
        y = drawWrapped(c, FONT_XL, x, y, w - iw, kAgendas[a].name, theme::text);
        static const char* kElect[] = {"For / Against", "Elect a player", "Elect a planet", "Elect a non-home planet (not Mecatol Rex)",
                                       "Elect a law", "Elect a scored secret objective", "Elect a strategy card", "Covert"};
        drawText(c, FONT_SMB, x, y + 4, std::string(kAgendas[a].law ? "LAW" : "DIRECTIVE") + " \xC2\xB7 " + kElect[int(kAgendas[a].elect)],
                 rgb(150, 180, 240));
        y += 30;
        bool fa = kAgendas[a].elect == Elect::ForAgainst;
        drawText(c, FONT_SMB, x, y, fa ? "For:" : "Outcome:", theme::gold);
        y = drawWrapped(c, FONT_MD, x + 80, y, w - 80, agendaText(a, false), theme::text) + 10;
        if (fa) {
            drawText(c, FONT_SMB, x, y, "Against:", theme::gold);
            y = drawWrapped(c, FONT_MD, x + 80, y, w - 80, agendaText(a, true), theme::text) + 10;
        }
        if (tab_ == 0) {
            const Law& l = g_.laws[items_[i]];
            if (l.owner >= 0) drawText(c, FONT_SMB, x, r.y + r.h - 40, "Held by " + playerName(g_, l.owner), pcol(g_, l.owner));
        }
    }

private:
    std::vector<int> items_;
};

// ======================================================================== faction cards
// Faction abilities, leaders, faction notes, special units and faction
// technologies of one player, with their full text (opened from the faction sheet).
class FactionCards : public GridScreen {
public:
    FactionCards(const Game& g, int human, int who) : GridScreen(g, human), who_(who) {
        title_ = std::string(factionShortName(g.players[who].faction)) + " - abilities and leaders";
        titleFaction_ = g.players[who].faction;
        cols_ = 2;
        cardH_ = 96;
        rebuild();
    }

protected:
    enum { kAbility = 0, kLeader = 100, kNote = 200, kUnit = 1000, kTech = 2000 };
    void rebuild() override {
        items_.clear();
        const Player& pl = g_.players[who_];
        const FactionInfo& fi = kFactionInfo[pl.faction];
        for (int i = 0; i < 3; ++i)
            if (fi.abilities[i]) items_.push_back(kAbility + i);
        for (size_t i = 0; i < pl.leaders.size(); ++i) items_.push_back(kLeader + int(i));
        for (int t : {kNoteFaction, kNoteFaction2, kNoteFaction3, kNoteFaction4})
            if (*factionNoteName(pl.faction, t)) items_.push_back(kNote + t);
        for (int u : kFactions[pl.faction].units)
            if (u >= 0 && *unitAbilityText(u)) items_.push_back(kUnit + u);
        for (int16_t t : kFactions[pl.faction].factionTechs)
            if (t >= 0) items_.push_back(kTech + t);
        count_ = int(items_.size());
    }
    std::string header() const override {
        const Player& pl = g_.players[who_];
        return factionImplemented(pl.faction) ? std::string(kFactions[pl.faction].name) + keyText(" \xC2\xB7 press A to read a card")
                                              : std::string(kFactions[pl.faction].name) +
                                                    " \xC2\xB7 these abilities aren't active yet (a later update adds them)";
    }
    struct Info {
        std::string title, kind, text, extra;
        uint32_t stripe;
        bool dim = false;
    };
    Info info(int id) const {
        const Player& pl = g_.players[who_];
        const bool on = factionImplemented(pl.faction);
        const uint32_t col = pcol(g_, who_);
        Info r;
        if (id >= kTech) {
            int t = id - kTech;
            r.title = kTechs[t].name;
            r.kind = std::string(kTechs[t].unitUpgrade ? "Unit upgrade" : "Faction technology") + (pl.hasTech(t) ? " \xC2\xB7 owned" : "");
            r.text = techSummary(t);
            if (kTechs[t].unitUpgrade)  // the upgraded unit's own ability text, if it has one
                for (int u = 0; u < kUnitCount; ++u)
                    if (kUnits[u].tech == t && *unitAbilityText(u)) r.text = std::string(kUnits[u].name) + ": " + unitAbilityText(u);
            r.stripe = techColor(t);
            r.dim = !pl.hasTech(t);
            if (!techImplemented(t) && !kTechs[t].unitUpgrade) r.extra = "Not active yet.";
            // Valefar Assimilator: which technology its token is on
            int on2 = !std::strcmp(kTechs[t].key, "vax") ? pl.assimX : !std::strcmp(kTechs[t].key, "vay") ? pl.assimY : -1;
            if (on2 >= 0) r.extra = std::string("Token on ") + kTechs[on2].name + ": " + techSummary(on2);
        } else if (id >= kUnit) {
            int u = id - kUnit;
            r.title = kUnits[u].name;
            r.kind = std::string("Special unit \xC2\xB7 ") + unitTypeName(kUnits[u].type);
            r.text = unitAbilityText(u);
            r.stripe = theme::cyan;
            if (!on) r.extra = "Not active yet.";
        } else if (id >= kNote) {
            int t = id - kNote;
            r.title = factionNoteName(pl.faction, t);
            r.kind = "Faction promissory note";
            r.text = on ? factionNoteText(pl.faction, t) : "";
            r.stripe = col;
            if (!on) r.extra = "Not active yet.";
        } else if (id >= kLeader) {
            static const char* kType[3] = {"Agent", "Commander", "Hero"};
            const LeaderState& ls = pl.leaders[id - kLeader];
            const LeaderDef& L = kLeaders[ls.leader];
            r.title = L.name;
            const char* st = "";
            switch (ls.status) {
                case kLocked: st = "locked"; break;
                case kReady: st = L.type == kAgent ? "ready" : "unlocked"; break;
                case kExhausted: st = L.type == kHero ? "in play" : "exhausted"; break;
                case kPurged: {
                    // (the Titans review, fixes 27) Ul the Progenitor is attached to Elysium, not purged
                    bool ul = false;
                    for (const PlanetState& q : g_.planets) ul = ul || q.ulHero;
                    st = ul && !std::strcmp(L.key, "titanshero") ? "attached to Elysium" : "purged";
                    break;
                }
            }
            r.kind = std::string(kType[L.type]) + " \xC2\xB7 " + st;
            r.text = on ? leaderText(ls.leader) : "";
            if (on && ls.status == kLocked) r.extra = std::string("Unlock: ") + leaderUnlock(ls.leader);
            if (!on) r.extra = "Not active yet.";
            r.stripe = rgb(170, 120, 220);
            r.dim = ls.status == kLocked || ls.status == kPurged;
        } else {
            r.title = kFactionInfo[pl.faction].abilities[id];
            r.kind = "Faction ability";
            r.text = abilityText(pl.faction, id);
            r.stripe = theme::gold;
            if (!on) {
                r.text.clear();
                r.extra = "Not active yet.";
            }
        }
        return r;
    }
    void drawCard(Canvas& c, int i, Rect r, bool sel, uint32_t now) override {
        Info in = info(items_[i]);
        cardFrame(c, r, sel, in.stripe, now);
        drawText(c, FONT_SMB, r.x + 14, r.y + 6, fitText(FONT_SMB, in.title, r.w - 22), in.dim ? theme::dim : theme::text);
        drawText(c, FONT_XS, r.x + 14, r.y + 23, fitText(FONT_XS, in.kind, r.w - 22), shade(in.stripe, 1.2f));
        int y = drawWrapped(c, FONT_XS, r.x + 14, r.y + 39, r.w - 22, in.text, theme::dim, r.y + r.h - 16);
        if (!in.extra.empty()) drawText(c, FONT_XS, r.x + 14, std::min(y, r.y + r.h - 16), fitText(FONT_XS, in.extra, r.w - 22), theme::faint);
    }
    void drawDetail(Canvas& c, int i, Rect r) override {
        Info in = info(items_[i]);
        detailFrame(c, r, in.stripe);
        int x = r.x + 20, w = r.w - 40, y = r.y + 20;
        y = drawWrapped(c, FONT_XL, x, y, w, in.title, theme::text);
        drawText(c, FONT_SMB, x, y + 4, in.kind, shade(in.stripe, 1.3f));
        y += 30;
        y = drawWrapped(c, FONT_MD, x, y, w, in.text, theme::text) + 10;
        if (!in.extra.empty()) drawWrapped(c, FONT_SM, x, y, w, in.extra, theme::dim);
    }

private:
    int who_;
    std::vector<int> items_;
};

// ======================================================================== faction sheet
class FactionSheet : public InfoScreen {
    // 0.43.0 (P12): the "◀ ▶" right-aligned at (right, y): each one clickable
    static void arrowHits(int right, int y) {
        const int w = textWidth(FONT_SM, "\xE2\x96\xB6"), sp = textWidth(FONT_SM, " ");
        hits::addButton(Rect{right - w - 2, y, w + 4, 18}, Btn::Right);
        hits::addButton(Rect{right - 2 * w - sp - 3, y, w + 3, 18}, Btn::Left);
    }

public:
    FactionSheet(const Game& g, int human, int start = -1, bool objectivesPage = false) : g_(g), human_(human), page_(objectivesPage ? 1 : 0) {
        for (int q = 0; q < g.numPlayers; ++q) order_.push_back(q);
        if (human >= 0) std::rotate(order_.begin(), order_.begin() + human, order_.end());
        for (size_t i = 0; i < order_.size(); ++i)
            if (order_[i] == start) sel_ = int(i);
    }

    bool update(const Input& in) override {
        mouseX_ = in.mouse().active ? in.mouse().x - float(theme::legacyX()) : -1.f;  // (widescreen: drawn centred)
        mouseY_ = in.mouse().y;
        if (cards_) {
            if (!cards_->update(in)) cards_.reset();
            return true;
        }
        if (piecesOpen_) {  // 0.44.1: the faction pieces, large
            if (in.pressed(Btn::B) || in.pressed(Btn::A) || in.pressed(Btn::Y) || in.pressed(Btn::Menu)) piecesOpen_ = false;
            if (in.pressed(Btn::Down)) piecesScroll_ = std::min(std::max(0, piecesH_ - 380), piecesScroll_ + 40);
            if (in.pressed(Btn::Up)) piecesScroll_ = std::max(0, piecesScroll_ - 40);
            const int n = int(order_.size());  // (L1 / R1: the next player's pieces)
            if (in.pressed(Btn::R1) || in.pressed(Btn::L1)) {
                for (int k = 1; k < n; ++k) {
                    const int s = (sel_ + (in.pressed(Btn::R1) ? k : n - k)) % n;
                    if (!pieces::of(g_, order_[size_t(s)], human_).empty()) {
                        sel_ = s;
                        piecesScroll_ = 0;
                        break;
                    }
                }
            }
            return true;
        }
        if (btOpen_) {  // 0.44.3: the breakthrough card, large
            if (in.pressed(Btn::B) || in.pressed(Btn::A) || in.pressed(Btn::Y) || in.pressed(Btn::Menu)) btOpen_ = false;
            const int n = int(order_.size());  // (L1 / R1: the next player's)
            if (in.pressed(Btn::R1) || in.pressed(Btn::L1) || in.pressed(Btn::Right) || in.pressed(Btn::Left)) {
                const bool fwd = in.pressed(Btn::R1) || in.pressed(Btn::Right);
                for (int k = 1; k < n; ++k) {
                    const int s = (sel_ + (fwd ? k : n - k)) % n;
                    if (btcard::has(g_, order_[size_t(s)])) {
                        sel_ = s;
                        break;
                    }
                }
            }
            return true;
        }
        if (in.pressed(Btn::Y) && page_ == 1 && btcard::has(g_, order_[size_t(sel_)])) {
            btOpen_ = true;
            return true;
        }
        if (in.pressed(Btn::Y) && page_ == 0 && !pieces::of(g_, order_[size_t(sel_)], human_).empty()) {
            piecesOpen_ = true;
            piecesScroll_ = 0;
            return true;
        }
        if (in.pressed(Btn::A)) {
            cards_.reset(new FactionCards(g_, human_, order_[sel_]));
            return true;
        }
        if (in.pressed(Btn::X)) page_ = 1 - page_;  // 0.20.0: secret objectives and technology
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) return false;
        int n = int(order_.size());
        if (in.pressed(Btn::R1) || in.pressed(Btn::Right)) sel_ = (sel_ + 1) % n;
        if (in.pressed(Btn::L1) || in.pressed(Btn::Left)) sel_ = (sel_ + n - 1) % n;
        return true;
    }

    void draw(Canvas& c, uint32_t now) override {
        if (cards_) {
            cards_->draw(c, now);
            return;
        }
        if (btOpen_) {
            c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 170);
            btcard::drawLarge(c, g_, order_[size_t(sel_)], human_, Rect{30, 20, 580, 420}, now);
            drawHintBar(c, {{"L1/R1", "Other players"}, {"B", "Close"}});
            return;
        }
        if (page_ == 1) {
            drawObjectivesPage(c, now);
            return;
        }
        if (piecesOpen_) {
            drawPieces(c, now);
            return;
        }
        c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 150);
        const int p = order_[sel_];
        const Player& pl = g_.players[p];
        const FactionDef& fd = kFactions[pl.faction];
        const FactionInfo& fi = kFactionInfo[pl.faction];
        const uint32_t col = pcol(g_, p);
        Rect r{10, 10, 620, 436};
        drawPanel(c, r, theme::panel, col, 12);
        c.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, 6, 5, col);
        std::string title = fd.name;
        int titleX = r.x + 14;
        if (icons::available()) {
            drawFactionIcon(c, pl.faction, titleX, r.y + 9, 30);
            titleX += 36;
        }
        drawText(c, FONT_LG, titleX, r.y + 12, fitText(FONT_LG, title, 400), col);
        std::string who = p == human_ ? "You" : "Computer player";
        if (p == g_.speaker) who += " \xC2\xB7 speaker";
        if (pl.eliminated) who += " \xC2\xB7 eliminated";
        drawTextAligned(c, FONT_SM, r.x, r.y + 16, r.w - 16, Align::Right, who + "  \xE2\x97\x80 \xE2\x96\xB6", theme::dim);
        arrowHits(r.x + r.w - 16, r.y + 14);  // 0.43.0 (P12): the arrows switch players with the mouse too

        // ---------------- left: command sheet
        Rect L{r.x + 12, r.y + 44, 300, 380};
        c.fillRoundRect(L.x, L.y, L.w, L.h, 10, rgb(22, 30, 52));
        drawText(c, FONT_MDB, L.x + 12, L.y + 8, "Command sheet", theme::gold);
        int onBoard = 0;
        for (const SystemState& s : g_.systems)
            if (s.hasToken(p)) ++onBoard;
        int reinf = kCommandTokens - pl.tactic - pl.fleet - pl.strategy - onBoard - edictTokensOf(g_, p);
        int edict = 0;  // Mahact: other players' tokens in the fleet pool (Edict)
        for (int q = 0; q < g_.numPlayers; ++q)
            if ((pl.edict >> q) & 1) ++edict;
        struct Pool {
            const char* name;
            const char* what;
            int n;
        } pools[3] = {{"Tactic pool", "Spend 1 to activate a system", pl.tactic},
                      {"Fleet pool", edict ? "Max non-fighter ships in one system (+ Edict tokens)" : "Max non-fighter ships in one system",
                       pl.fleet + edict},
                      {"Strategy pool", "Spend 1 to use another player's strategy card", pl.strategy}};
        int y = L.y + 32;
        for (const Pool& po : pools) {
            c.fillRoundRect(L.x + 8, y, L.w - 16, 61, 8, rgb(14, 20, 36));
            drawText(c, FONT_SMB, L.x + 16, y + 4, po.name, theme::text);
            drawTextAligned(c, FONT_LG, L.x + 8, y + 2, L.w - 28, Align::Right, std::to_string(po.n), col);
            drawText(c, FONT_XS, L.x + 16, y + 21, po.what, theme::faint);
            float tx = float(L.x + 28), ty = float(y + 46);
            std::vector<uint32_t> tokCol(size_t(std::max(0, po.n)), col);
            std::vector<int> tokFac(size_t(std::max(0, po.n)), pl.faction);
            const bool fleet = &po == &pools[1];  // fleet pool tokens sit ship side up
            if (fleet) {  // Edict tokens in their owners' colours (faction side, so you see whose)
                size_t k = size_t(pl.fleet);
                for (int q = 0; q < g_.numPlayers && k < tokCol.size(); ++q)
                    if ((pl.edict >> q) & 1) {
                        tokFac[k] = g_.players[q].faction;
                        tokCol[k++] = pcol(g_, q);
                    }
            }
            for (int k = 0; k < po.n; ++k) {
                cards::commandToken(c, tokFac[size_t(k)], tokCol[size_t(k)], int(tx), int(ty), 23, 255,
                                    fleet && k < pl.fleet);
                tx += 25.f;
                if (tx > L.x + L.w - 20) {
                    tx = float(L.x + 28);
                    ty += 6.f;
                }
            }
            if (po.n == 0) drawText(c, FONT_XS, L.x + 16, y + 40, "empty", theme::faint);
            y += 63;
        }
        drawText(c, FONT_SMB, L.x + 12, y + 2, "Reinforcements", theme::text);
        drawTextAligned(c, FONT_SMB, L.x, y + 2, L.w - 16, Align::Right, std::to_string(std::max(0, reinf)) + " tokens", theme::dim);
        y += 18;
        float tx = float(L.x + 22);
        for (int k = 0; k < std::max(0, reinf) && k < 16; ++k) {
            cards::commandToken(c, pl.faction, col, int(tx), y + 10, 16, 150);
            tx += 18.f;
        }
        y += 24;
        drawText(c, FONT_XS, L.x + 12, y,
                 fitText(FONT_XS, "Plus " + std::to_string(onBoard) + " on the board, back in the status phase.", L.w - 24),
                 theme::faint);
        y += 16;
        // rows with an icon in front of the label when the icon pack is there
        const int ix = icons::available() ? 19 : 0;
        auto rowIcon = [&](const char* name, int yy, uint32_t tint) {
            if (ix) icons::draw(c, name, L.x + 11, yy, 15, 255, tint);
        };
        rowIcon("trade_good", y, theme::text);
        drawText(c, FONT_SMB, L.x + 12 + ix, y, "Trade goods", theme::text);
        drawTextAligned(c, FONT_SMB, L.x, y, L.w - 16, Align::Right, std::to_string(pl.tradeGoods), theme::gold);
        y += 17;
        rowIcon("commodity", y, theme::text);
        drawText(c, FONT_SMB, L.x + 12 + ix, y, "Commodities", theme::text);
        drawTextAligned(c, FONT_SMB, L.x, y, L.w - 16, Align::Right,
                        std::to_string(pl.commodities) + " / " + std::to_string(fd.commodities), theme::dim);
        y += 17;
        rowIcon("action_card", y, rgb(240, 160, 140));
        drawText(c, FONT_SMB, L.x + 12 + ix, y, "Action cards", theme::text);
        drawTextAligned(c, FONT_SMB, L.x, y, L.w - 16, Align::Right, std::to_string(pl.actionCards.size()), theme::dim);
        y += 17;
        rowIcon("promissory", y, theme::text);
        drawText(c, FONT_SMB, L.x + 12 + ix, y, "Promissory notes", theme::text);
        std::string notes = std::to_string(pl.notes.size()) + " in hand";
        if (!pl.notesInPlay.empty()) notes += ", " + std::to_string(pl.notesInPlay.size()) + " in play";
        drawTextAligned(c, FONT_SM, L.x, y + 1, L.w - 16, Align::Right, notes, theme::dim);
        y += 17;
        rowIcon("relic", y, theme::text);
        drawText(c, FONT_SMB, L.x + 12 + ix, y, "Relics", theme::text);
        const int frags = fragmentCount(g_, p), charts = starChartCount(g_, p);
        std::string rel = std::to_string(pl.relics.size()) + (pl.relics.size() == 1 ? " relic \xC2\xB7 " : " relics \xC2\xB7 ") + std::to_string(frags) + " fragment" +
                          (frags == 1 ? "" : "s");
        if (charts) rel += " \xC2\xB7 " + std::to_string(charts) + " chart" + (charts == 1 ? "" : "s");
        if (pl.enigmatic) rel += " \xC2\xB7 " + std::to_string(pl.enigmatic) + " device";
        drawTextAligned(c, FONT_SM, L.x, y + 1, L.w - 16, Align::Right, rel, pl.relics.empty() ? theme::dim : theme::gold);
        // faction-specific pieces (batch 4)
        std::string special, value;
        if (isFaction(g_, p, "cabal")) {
            special = "Captured units";
            int n[kUnitTypes] = {};
            for (int16_t x : pl.captured) ++n[x % 16];
            for (int t = 0; t < kUnitTypes; ++t)
                if (n[t]) value += (value.empty() ? "" : ", ") + std::to_string(n[t]) + " " + unitTypeShort(UnitType(t));
            if (value.empty()) value = "none";
        } else if (isFaction(g_, p, "titans")) {
            int n = 0;
            for (const PlanetState& ps : g_.planets)
                if (ps.sleeper) ++n;
            special = "Sleeper tokens";
            value = std::to_string(n) + " of 5 on planets";
        } else if (isFaction(g_, p, "nomad")) {
            special = "On Artuno";
            value = std::to_string(pl.agentTG) + " trade goods";
        } else if (isFaction(g_, p, "keleres") && pl.tribunii >= 0) {
            special = "The Tribunii";
            value = std::string(factionShortName(pl.tribunii)) + " home";
        } else if (isFaction(g_, p, "mahact")) {
            special = "Edict tokens";
            for (int q = 0; q < g_.numPlayers; ++q)
                if ((pl.edict >> q) & 1) value += (value.empty() ? "" : ", ") + playerName(g_, q);
            if (value.empty()) value = "none";
        } else if (isFaction(g_, p, "deepwrought")) {  // Thunder's Edge factions (0.35.0)
            special = "Ocean cards";
            value = std::to_string(pl.oceans) + " (" + std::to_string(pl.oceans - pl.oceansExhausted) + " ready)";
        } else if (isFaction(g_, p, "bastion")) {
            special = "Galvanized";
            value = std::to_string(galvanizedOnBoard(g_, p)) + " units";
        } else if (isFaction(g_, p, "crimson") && !pl.phantom.empty()) {
            special = "Homesick Phantom";
            value = std::to_string(pl.phantom.ships()) + " ships waiting";
        } else if (isFaction(g_, p, "firmament") || isFaction(g_, p, "obsidian")) {
            // plots: named once the Obsidian reveal them (or to their own player); tokens are public
            const bool open = isFaction(g_, p, "obsidian") || p == human_;
            special = "Plots";
            for (int k = 0; k < kPlotCount; ++k) {
                if (!((pl.plotsIn >> k) & 1)) continue;
                std::string who;
                for (int q = 0; q < g_.numPlayers; ++q)
                    if ((pl.plotTok[size_t(k)] >> q) & 1) who += (who.empty() ? "" : "+") + playerName(g_, q);
                value += (value.empty() ? "" : ", ") + (open ? std::string(plotName(k)) : std::string("?")) + ": " + (who.empty() ? "-" : who);
            }
            if (value.empty()) value = "none";
            if (pl.sowing) value += " \xC2\xB7 " + std::to_string(pl.sowing) + " TG on the card";
        } else if (isFaction(g_, p, "bentor")) {  // Discordant Stars batch 3 (0.39.0)
            special = "Fragment tokens";
            static const char* kKind[4] = {"cultural", "hazardous", "industrial", "unknown"};
            for (int k = 0; k < 4; ++k)
                if ((pl.dsFrags >> k) & 1) value += (value.empty() ? "" : ", ") + std::string(kKind[k]);
            if (value.empty()) value = "none yet";
        } else if (isFaction(g_, p, "augers")) {
            special = "Faction sheet";  // (Oracle AI: only its own player may look at it)
            bool sees = p == human_;  // (or Read the Fates face up in front of the human player)
            if (human_ >= 0 && human_ < g_.numPlayers) {
                const auto& v = g_.players[size_t(human_)].notesInPlay;
                sees = sees || std::find(v.begin(), v.end(), makeNote(p, kNoteFaction)) != v.end();
            }
            value = pl.dsSheet < 0 ? std::string("no objective") : sees ? std::string(kObjectives[pl.dsSheet].name) + " (next)" : std::string("1 objective, face down");
        } else if (isFaction(g_, p, "kjalengard")) {
            special = "Glory tokens";
            value = std::to_string(g_.dsGlory.size()) + "/3 \xC2\xB7 " + std::to_string(pl.captured.size()) + " captured";
            if (breakthroughIs(g_, p, "kjalengard") && !g_.dsGlory.empty())  // Bannerhalls (0.43.0)
                value += " \xC2\xB7 Bannerhalls " + std::to_string(oceansReady(g_, p)) + "/" + std::to_string(oceanCards(g_, p)) + " ready";
        } else if (isFaction(g_, p, "gledge")) {
            special = "Core tokens";
            value = std::to_string(g_.dsCores.size()) + " of 3 placed";
        } else if (isFaction(g_, p, "kollecc")) {  // Discordant Stars batch 4 (0.40.0)
            special = "Cloaked units";
            int ships = 0, gf = 0;
            for (int16_t x : pl.captured)
                if (x / 16 == p) (isGroundForce(UnitType(x % 16)) ? gf : ships) += 1;
            value = std::to_string(ships) + " ships, " + std::to_string(gf) + " ground forces";
        } else if (isFaction(g_, p, "vaden")) {
            special = "Debt tokens";
            for (int q = 0; q < g_.numPlayers; ++q)
                if (q != p && g_.dsDebt[q] > 0) {  // (3 letters of the faction key, capitalised: the full names don't fit)
                    std::string k = std::string(kFactions[g_.players[size_t(q)].faction].key).substr(0, 3);
                    if (!k.empty()) k[0] = char(std::toupper(static_cast<unsigned char>(k[0])));
                    value += (value.empty() ? "" : ", ") + k + " " + std::to_string(g_.dsDebt[q]);
                }
            if (value.empty()) value = "none";
        } else if (isFaction(g_, p, "olradin")) {
            special = "Policies";
            value = std::string((pl.dsPolicy & 1) ? "Control" : "Connect") + ", " + ((pl.dsPolicy & 2) ? "Plunder" : "Preserve") + ", " +
                    ((pl.dsPolicy & 4) ? "Exploit" : "Empower");
        } else if (isFaction(g_, p, "mykomentori")) {  // Discordant Stars batch 5 (0.41.0)
            special = "Omen dice";
            for (int8_t o : pl.dsOmen)
                if (o > 0) value += (value.empty() ? "" : " ") + std::to_string(o);
            if (value.empty()) value = "none left";
            if (pl.dsMykoMechs) value += " \xC2\xB7 " + std::to_string(pl.dsMykoMechs) + " mech on the card";
            if (!g_.dsSoulCage.empty()) value += " \xC2\xB7 " + std::to_string(g_.dsSoulCage.size()) + " hero bound";
        } else if (isFaction(g_, p, "axis")) {
            special = "Axis Orders";
            int mine = 0, sold = 0, left = 0;
            for (int8_t h : g_.dsOrders) (h < 0 ? left : h == p ? mine : sold) += 1;
            value = std::to_string(mine) + " for sale, " + std::to_string(sold) + " sold, " + std::to_string(left) + " left";
        } else if (isFaction(g_, p, "ghemina")) {
            special = "The Lady";
            int ls = g_.dsLadySys;
            if (g_.fixes >= 16) {  // (0.52.0: her own unit type)
                ls = -1;
                for (size_t sy = 0; sy < g_.systems.size() && ls < 0; ++sy)
                    if (g_.systems[sy].space[size_t(p)].count(UnitType::Flagship2)) ls = int(sy);
            }
            value = ls < 0 ? std::string("not on the board")
                           : g_.systems[size_t(ls)].home >= 0 ? playerName(g_, g_.systems[size_t(ls)].home) + " home"
                                                              : std::string(g_.systems[size_t(ls)].d().name);
        } else if (isFaction(g_, p, "edyn")) {
            special = "Sigils";
            int n = 0;
            for (size_t sy = 0; sy < g_.systems.size(); ++sy) {
                bool m = g_.systems[sy].space[size_t(p)].count(UnitType::Mech) > 0;
                for (int16_t x : g_.systems[sy].planets) m = m || g_.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech) > 0;
                n += m ? 1 : 0;
            }
            value = std::to_string(n) + (n == 1 ? " system with a mech" : " systems with mechs");
        } else if (isFaction(g_, p, "freesystems")) {
            special = "Heart of Rebellion";
            value = g_.dsHeart < 0 ? std::string("not attached") : std::string(kPlanets[g_.planets[size_t(g_.dsHeart)].def].name);
        } else if (isFaction(g_, p, "nokar")) {
            special = "Destroyers";
            value = std::to_string(unitsOnBoard(g_, p, UnitType::Destroyer)) + " of 12 on the board";
        } else if (isFaction(g_, p, "veldyr")) {  // Discordant Stars batch 6 (0.42.0)
            special = "Branch Offices";
            int on = 0;
            for (int16_t b : g_.dsBranch) on += b >= 0 ? 1 : 0;
            value = std::to_string(on) + " open, " + std::to_string(4 - on) + " to place";
        } else if (isFaction(g_, p, "lizho")) {
            special = "Traps";
            int on = 0;
            std::string names;
            for (int k = 0; k < 5; ++k)
                if (g_.dsTrap[k] >= 0) {
                    ++on;
                    names += (names.empty() ? "" : ", ") + std::string(trapName(k));
                }
            value = p == human_ && on ? names : std::to_string(on) + " hidden, " + std::to_string(5 - on) + " in reserve";
        } else if (isFaction(g_, p, "mirveda")) {
            special = "Gauss Cannons";
            int n = 0;
            for (const SystemState& sy : g_.systems) n += sy.space[size_t(p)].count(UnitType::PDS);
            value = std::to_string(n) + " in space";
        } else if (isFaction(g_, p, "ghoti")) {
            special = "Ghoti planet";
            value = std::string(pl.dsGhotiExhausted ? "exhausted" : "ready") + " (3/3)";
        } else if (isFaction(g_, p, "nivyn")) {
            special = "The Wound";
            value = g_.dsWound < 0 ? std::string("not placed")
                    : g_.systems[size_t(g_.dsWound)].home >= 0 ? playerName(g_, g_.systems[size_t(g_.dsWound)].home) + " home"
                                                               : std::string(g_.systems[size_t(g_.dsWound)].d().name);
        }
        if (!special.empty()) {
            y += 17;
            drawText(c, FONT_SMB, L.x + 12, y, special, theme::text);
            drawTextAligned(c, FONT_SM, L.x + 120, y + 1, L.w - 136, Align::Right, fitText(FONT_SM, value, L.w - 136), theme::dim);
        }
        if (g_.events) {  // galactic events (0.36.0): what this player holds from them
            std::string ev;
            if (eventOn(g_, kEvAtomics)) ev += pl.atomics ? "atomics ready" : "atomics spent";
            for (int8_t f : pl.alliances) ev += (ev.empty() ? "" : " \xC2\xB7 ") + std::string(factionShortName(f)) + " Alliance";
            int purged = 0;
            for (int t = 0; t < kUnitTypes; ++t) purged += pl.purgedUnits[size_t(t)];
            if (purged) ev += (ev.empty() ? "" : " \xC2\xB7 ") + std::to_string(purged) + " ships purged";
            if (pl.monumentVP) ev += (ev.empty() ? "" : " \xC2\xB7 ") + std::to_string(pl.monumentVP) + " VP from monuments";
            if (!ev.empty() && !special.empty()) {  // (no room for two rows: under the faction's own one)
                drawTextAligned(c, FONT_XS, L.x + 12, y + 15, L.w - 28, Align::Right, fitText(FONT_XS, ev, L.w - 28), theme::faint);
            } else if (!ev.empty()) {
                y += 17;
                drawText(c, FONT_SMB, L.x + 12, y, "Events", theme::text);
                drawTextAligned(c, FONT_SM, L.x + 70, y + 1, L.w - 86, Align::Right, fitText(FONT_SM, ev, L.w - 86), theme::dim);
            }
        }

        // ---------------- right: faction details
        int x = L.x + L.w + 14, w = r.x + r.w - 14 - x;
        y = L.y + 4;
        auto section = [&](const std::string& s) {
            drawText(c, FONT_MDB, x, y, s, theme::gold);
            y += 19;
        };
        drawText(c, FONT_SMB, x, y, std::to_string(pl.vp) + " / " + std::to_string(g_.vpGoal) + " victory points", theme::text);
        y += 18;
        int hs = pl.homeSystem;
        if (hs >= 0) {
            std::string hp;
            for (int16_t q : g_.systems[hs].planets) hp += (hp.empty() ? "" : ", ") + std::string(kPlanets[g_.planets[q].def].name);
            if (hp.empty()) hp = "deep space (no planets)";  // 0.42.0: Ghoti
            drawText(c, FONT_XS, x, y, fitText(FONT_XS, "Home: " + hp, w), theme::dim);
            y += 16;
        }
        y += 4;
        section("Strategy cards");
        if (pl.cards.empty()) {
            drawText(c, FONT_XS, x, y, "None this round", theme::faint);
            y += 16;
        }
        for (int8_t cd : pl.cards) {
            bool used = (pl.cardsUsed >> cd) & 1;
            uint32_t cc = strategyCardColor(cd);
            c.fillRoundRect(x, y, w, 18, 5, used ? shade(cc, 0.35f) : shade(cc, 0.7f));
            drawText(c, FONT_SMB, x + 6, y + 1, std::to_string(cd + 1) + " " + strategyName(cd), used ? theme::dim : theme::text);
            drawTextAligned(c, FONT_XS, x, y + 3, w - 6, Align::Right, used ? "used" : "ready", used ? theme::faint : theme::text);
            y += 21;
        }
        y += 4;
        const bool active = factionImplemented(pl.faction);
        section("Faction abilities");
        for (const char* a : fi.abilities) {
            if (!a) continue;
            drawText(c, FONT_SM, x + 4, y, a, theme::text);
            y += 15;
        }
        if (!active) {
            drawText(c, FONT_XS, x + 4, y, fitText(FONT_XS, "Not active yet - coming in a later update.", w - 4), theme::faint);
            y += 15;
        }
        y += 3;
        section("Leaders");
        static const char* kType[3] = {"Agent", "Commander", "Hero"};
        for (const LeaderState& ls : pl.leaders) {
            const LeaderDef& L = kLeaders[ls.leader];
            const char* st = ls.status == kLocked ? "locked" : ls.status == kReady ? (L.type == kAgent ? "ready" : "unlocked")
                           : ls.status == kExhausted ? (L.type == kHero ? "in play" : "exhausted") : "purged";
            bool on = active && ls.status != kLocked && ls.status != kPurged;
            drawText(c, FONT_XS, x + 4, y + 1, kType[L.type], theme::faint);
            drawText(c, FONT_SM, x + 72, y, fitText(FONT_SM, L.name, w - 130), on ? theme::text : theme::dim);
            if (active) drawTextAligned(c, FONT_XS, x, y + 1, w, Align::Right, st, ls.status == kReady ? theme::green : theme::faint);
            y += 15;
        }
        if (!active) {
            drawText(c, FONT_XS, x + 4, y, fitText(FONT_XS, "Not active yet - coming in a later update.", w - 4), theme::faint);
            y += 15;
        }
        y += 3;
        section("Promissory notes");
        for (const char* n : fi.notes)
            if (n) {
                drawText(c, FONT_SM, x + 4, y, n, theme::text);
                y += 15;
            }
        for (int16_t n : pl.notesInPlay)
            if (noteType(n) == kNoteAlliance) {
                drawText(c, FONT_XS, x + 4, y + 1, fitText(FONT_XS, "Alliance with " + playerName(g_, noteOwner(n)), w - 4), theme::cyan);
                y += 14;
            }
        y += 3;
        section("Faction technology");
        for (int16_t t : fd.factionTechs) {
            if (t < 0) continue;
            int sx = 0;
            if (!kTechs[t].unitUpgrade && kTechs[t].color != TechColor::None && icons::available()) {
                drawSkipIcon(c, kTechs[t].color, x + 4, y, 14);
                sx = 18;
            }
            drawText(c, FONT_SM, x + 4 + sx, y, fitText(FONT_SM, std::string(kTechs[t].name) + (pl.hasTech(t) ? "  (owned)" : ""), w - 4 - sx),
                     techColor(t));
            y += 15;
        }
        // ---------------- bottom right: the faction's pieces (0.44.1, owner) - click / Y opens them large
        const std::vector<pieces::Group> pcs = pieces::of(g_, p, human_);
        if (!pcs.empty()) {
            const int bottom = r.y + r.h - 8;
            const int top = std::max(y + 4, bottom - 66);
            const Rect T{x - 6, top, w + 6, bottom - top};
            trayRect_ = T;
            if (T.h >= 34) {
                const bool hover = mouseX_ >= float(T.x) && mouseX_ < float(T.x + T.w) && mouseY_ >= float(T.y) && mouseY_ < float(T.y + T.h);
                pieces::drawTray(c, pcs, T, now, hover);
                hits::addAction(T, [this]() {
                    piecesOpen_ = true;
                    piecesScroll_ = 0;
                });
            } else {
                LOG("faction sheet: no room for the pieces tray (%s, %d px)", kFactions[pl.faction].key, T.h);
            }
            drawHintBar(c, {{"A", "Abilities"}, {"X", "Secrets, tech"}, {"Y", "Pieces"}, {"L1/R1", "Other players"}, {"B", "Back"}});
        } else {
            trayRect_ = Rect{};
            drawHintBar(c, {{"A", "Abilities and leaders"}, {"X", "Secrets and technology"}, {"L1/R1", "Other players"}, {"B", "Back"}});
        }
    }

    // 0.44.1: the faction pieces, large (a click or B closes; Up / Down scroll; L1 / R1 the next faction with pieces)
    void drawPieces(Canvas& c, uint32_t now) {
        c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 150);
        const int p = order_[size_t(sel_)];
        const Player& pl = g_.players[size_t(p)];
        const uint32_t col = pcol(g_, p);
        Rect r{10, 10, 620, 436};
        drawPanel(c, r, theme::panel, col, 12);
        c.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, 6, 5, col);
        int tx = r.x + 14;
        if (icons::available()) {
            drawFactionIcon(c, pl.faction, tx, r.y + 9, 30);
            tx += 36;
        }
        drawText(c, FONT_LG, tx, r.y + 12, fitText(FONT_LG, std::string(factionShortName(pl.faction)) + " - faction pieces", 520), col);
        drawTextAligned(c, FONT_SM, r.x, r.y + 16, r.w - 16, Align::Right, p == human_ ? "You" : "Computer player", theme::dim);
        hits::addAction(Rect{0, 0, 640, 456}, [this]() { piecesOpen_ = false; });
        piecesH_ = pieces::drawDetail(c, g_, p, pieces::of(g_, p, human_), r, now, piecesScroll_);
        if (piecesScroll_ > 0) drawTextAligned(c, FONT_XS, r.x, r.y + 32, r.w - 16, Align::Right, "\xE2\x96\xB2 more", theme::dim);
        if (piecesScroll_ + 380 < piecesH_) drawTextAligned(c, FONT_XS, r.x, r.y + r.h - 12, r.w - 16, Align::Right, "\xE2\x96\xBC more", theme::dim);
        drawHintBar(c, {{"B", "Back"}, {"UP/DOWN", "Scroll"}, {"L1/R1", "Other factions"}});
    }

    // Page 2 (0.20.0, owner): secret objectives - yours in full, other players'
    // unscored ones only as a count (face down), scored ones in full - public
    // objectives scored, and the technology the faction owns.
    void drawObjectivesPage(Canvas& c, uint32_t now) {
        c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 150);
        const int p = order_[sel_];
        const Player& pl = g_.players[size_t(p)];
        const uint32_t col = pcol(g_, p);
        Rect r{10, 10, 620, 436};
        drawPanel(c, r, theme::panel, col, 12);
        c.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, 6, 5, col);
        int titleX = r.x + 14;
        if (icons::available()) {
            drawFactionIcon(c, pl.faction, titleX, r.y + 9, 30);
            titleX += 36;
        }
        drawText(c, FONT_LG, titleX, r.y + 12, fitText(FONT_LG, playerName(g_, p) + ": secrets and technology", 470), col);
        drawTextAligned(c, FONT_SM, r.x, r.y + 16, r.w - 16, Align::Right, "\xE2\x97\x80 \xE2\x96\xB6", theme::dim);
        arrowHits(r.x + r.w - 16, r.y + 14);
        // left: objectives
        const int lx = r.x + 14, lw = 290;
        int y = r.y + 50;
        drawText(c, FONT_MDB, lx, y, "Secret objectives", theme::gold);
        y += 20;
        std::vector<int> scoredSecrets, scoredPublic;
        for (int16_t o : pl.scored) (kObjectives[o].kind == 3 ? scoredSecrets : scoredPublic).push_back(o);
        auto objective = [&](int o, uint32_t tc, const char* tag) {
            drawText(c, FONT_SMB, lx + 4, y, fitText(FONT_SMB, kObjectives[o].name, lw - 60), tc);
            drawTextAligned(c, FONT_XS, lx, y + 2, lw, Align::Right, tag, theme::faint);
            y += 16;
            for (const std::string& l : wrapText(FONT_XS, objectiveSummary(o), lw - 8)) {
                if (y > r.y + r.h - 40) break;
                drawText(c, FONT_XS, lx + 8, y, l, theme::dim);
                y += 13;
            }
            y += 4;
        };
        for (int o : scoredSecrets) objective(o, theme::green, "scored");
        if (p == human_ || human_ < 0 || lawOwner(g_, "warrant") == p) {  // Search Warrant: played face up
            for (int16_t o : pl.secrets) objective(o, theme::text, p == human_ || human_ < 0 ? "held" : "held (Search Warrant)");
        } else if (!pl.secrets.empty()) {
            const int n = int(pl.secrets.size());
            for (int k = 0; k < n && k < 4; ++k) {  // face down
                c.fillRoundRect(lx + 4 + k * 30, y, 26, 34, 4, rgb(70, 20, 24));
                c.frameRoundRect(lx + 4 + k * 30, y, 26, 34, 4, rgb(200, 70, 70), 1);
                if (!icons::draw(c, "secret", lx + 8 + k * 30, y + 8, 18)) c.fillCircle(float(lx + 17 + k * 30), float(y + 17), 6.f, rgb(200, 70, 70));
            }
            drawText(c, FONT_SM, lx + 8 + std::min(n, 4) * 30, y + 9, std::to_string(n) + (n == 1 ? " held - hidden" : " held - hidden"),
                     theme::dim);
            y += 42;
        }
        if (scoredSecrets.empty() && pl.secrets.empty()) {
            drawText(c, FONT_XS, lx + 4, y, "None", theme::faint);
            y += 16;
        }
        y += 6;
        drawText(c, FONT_MDB, lx, y, "Public objectives scored", theme::gold);
        y += 20;
        if (scoredPublic.empty()) {
            drawText(c, FONT_XS, lx + 4, y, "None yet", theme::faint);
            y += 16;
        }
        for (int o : scoredPublic) {
            if (y > r.y + r.h - 30) break;
            drawText(c, FONT_SM, lx + 4, y, fitText(FONT_SM, kObjectives[o].name, lw - 40), kObjectives[o].kind == 2 ? rgb(130, 170, 255) : theme::text);
            drawTextAligned(c, FONT_XS, lx, y + 1, lw, Align::Right, std::to_string(kObjectives[o].points) + " VP", theme::faint);
            y += 15;
        }
        drawText(c, FONT_SMB, lx, r.y + r.h - 24, std::to_string(pl.vp) + " / " + std::to_string(g_.vpGoal) + " victory points", theme::text);
        // right: technology by colour
        const int rx = r.x + 326, rw = r.x + r.w - 14 - rx;
        // 0.44.3 (owner): the breakthrough card under the technology - click / Y opens it large
        const bool bt = btcard::has(g_, p);
        const Rect btR{rx, r.y + r.h - 12 - 82, rw, 82};
        const int limY = bt ? btR.y - 12 : r.y + r.h;  // where the technology list must stop
        if (bt) {
            drawText(c, FONT_MDB, rx, btR.y - 20, "Breakthrough", theme::gold);
            const bool hover = mouseX_ >= float(btR.x) && mouseX_ < float(btR.x + btR.w) && mouseY_ >= float(btR.y) && mouseY_ < float(btR.y + btR.h);
            btcard::draw(c, pl.faction, btR, btcard::stateOf(g_, p), hover, now);
            hits::addAction(btR, [this]() { btOpen_ = true; });
        }
        y = r.y + 50;
        drawText(c, FONT_MDB, rx, y, "Technology (" + std::to_string(pl.techs.size()) + ")", theme::gold);
        y += 20;
        static const char* kCol[4] = {"Propulsion", "Biotic", "Warfare", "Cybernetic"};
        for (int colour = -1; colour < 5; ++colour) {  // the four colours, then unit upgrades, then the rest
            std::vector<int> list;
            for (int16_t t : pl.techs) {
                const TechDef& d = kTechs[size_t(t)];
                const bool upg = d.unitUpgrade;
                if (colour >= 0 && colour < 4 ? (!upg && int(d.color) == colour) : colour == 4 ? upg : (!upg && d.color == TechColor::None))
                    list.push_back(t);
            }
            if (list.empty()) continue;
            if (y > limY - 40) break;
            const char* head = colour >= 0 && colour < 4 ? kCol[colour] : colour == 4 ? "Unit upgrades" : "Other";
            int hx = rx;
            if (colour >= 0 && colour < 4 && icons::available()) {
                drawSkipIcon(c, TechColor(colour), rx, y, 14);
                hx += 18;
            }
            drawText(c, FONT_SMB, hx, y, head, colour >= 0 && colour < 4 ? techColor(list[0]) : theme::text);
            y += 16;
            for (int t : list) {
                if (y > limY - 24) break;
                drawText(c, FONT_SM, rx + 10, y, fitText(FONT_SM, std::string(kTechs[size_t(t)].name) + (kTechs[size_t(t)].faction >= 0 ? "  (faction)" : ""), rw - 10),
                         techColor(t));
                y += 14;
            }
            y += 4;
        }
        if (pl.techs.empty()) drawText(c, FONT_XS, rx + 4, y, "None", theme::faint);
        if (bt) drawHintBar(c, {{"X", "Faction sheet"}, {"Y", "Breakthrough"}, {"L1/R1", "Other players"}, {"B", "Back"}});
        else drawHintBar(c, {{"X", "Faction sheet"}, {"L1/R1", "Other players"}, {"B", "Back"}});
    }

private:
    const Game& g_;
    int human_;
    int page_ = 0;
    std::vector<int> order_;
    int sel_ = 0;
    std::unique_ptr<InfoScreen> cards_;
    bool piecesOpen_ = false;  // 0.44.1: the faction pieces, large
    bool btOpen_ = false;      // 0.44.3: the breakthrough card, large
    int piecesScroll_ = 0, piecesH_ = 0;
    Rect trayRect_{};
    float mouseX_ = -1.f, mouseY_ = -1.f;
};

}  // namespace

std::unique_ptr<InfoScreen> makeFactionSheet(const Game& g, int human, int start, bool objectivesPage) {
    return std::unique_ptr<InfoScreen>(new FactionSheet(g, human, start, objectivesPage));
}
std::unique_ptr<InfoScreen> makeTechScreen(const Game& g, int human) { return std::unique_ptr<InfoScreen>(new TechScreen(g, human)); }
std::unique_ptr<ResearchPick> makeResearchPick(const Game& g, int player, const std::vector<int>& techs,
                                               const std::vector<std::string>& notes, const std::string& prompt) {
    return std::unique_ptr<ResearchPick>(new ResearchPicker(g, player, techs, notes, prompt));
}
std::unique_ptr<InfoScreen> makeHandScreen(const Game& g, int human) { return std::unique_ptr<InfoScreen>(new HandScreen(g, human)); }
std::unique_ptr<InfoScreen> makeLawScreen(const Game& g, int human) { return std::unique_ptr<InfoScreen>(new LawScreen(g, human)); }
std::unique_ptr<InfoScreen> makePlanetScreen(const Game& g, int human) {
    return std::unique_ptr<InfoScreen>(new PlanetScreen(g, human));
}
std::unique_ptr<InfoScreen> makeUnitScreen(const Game& g, int human) { return std::unique_ptr<InfoScreen>(new UnitScreen(g, human)); }
