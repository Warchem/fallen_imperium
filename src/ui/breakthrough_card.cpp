// Breakthrough cards (0.44.3) - see breakthrough_card.h.
#include "ui/breakthrough_card.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "engine/breakthroughs.h"
#include "engine/query.h"
#include "gfx/font.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/pcolors.h"

using namespace ti;

namespace btcard {

namespace {

const uint32_t kSyn[4] = {rgb(80, 146, 240), rgb(84, 196, 100), rgb(228, 80, 70), rgb(238, 206, 70)};
const uint32_t kHead = rgb(22, 27, 36), kBody = rgb(236, 238, 242), kInk = rgb(30, 34, 42), kTrace = rgb(214, 219, 228);

uint32_t synCol(TechColor t) { return int(t) >= 0 && int(t) < 4 ? kSyn[int(t)] : rgb(150, 154, 166); }

std::string upper(std::string s) {
    for (char& ch : s)
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
    return s;
}

const BreakthroughDef* defOf(int faction) {
    return faction >= 0 && faction < kFactionCount ? breakthroughOf(kFactions[faction].key) : nullptr;
}

bool exhausts(const BreakthroughDef& b) { return std::strstr(b.text, "xhaust it") != nullptr; }

// The frame: the card shading from colour a (left) to colour b (right), in strips clipped to the card's outline.
void frame(Canvas& c, Rect r, int rad, uint32_t a, uint32_t b) {
    const Rect old = c.clip();
    const int x0 = std::max(r.x, old.x), x1 = std::min(r.x + r.w, old.x + old.w);
    for (int x = x0; x < x1; x += 2) {
        const float t = r.w > 1 ? float(x - r.x) / float(r.w - 1) : 0.f;
        // a little brighter at the edges, the two colours meeting in a darker middle
        const float edge = std::fabs(t - 0.5f) * 2.f;
        uint32_t col = mix(a, b, int(t * 255.f));
        col = shade(col, 0.55f + 0.45f * edge);
        Rect s{x, old.y, std::min(2, x1 - x), old.h};
        const int sy0 = std::max(s.y, old.y), sy1 = std::min(s.y + s.h, old.y + old.h);
        c.setClip({s.x, sy0, s.w, sy1 - sy0});
        c.fillRoundRect(r.x, r.y, r.w, r.h, rad, col);
    }
    c.setClip(old);
}

// Faint circuit traces in the text panel (the printed cards have them).
void traces(Canvas& c, Rect b, int faction) {
    uint32_t s = 0x9E3779B9u ^ uint32_t(faction * 2654435761u);
    auto rnd = [&s](int n) {
        s = s * 1664525u + 1013904223u;
        return n > 0 ? int((s >> 8) % uint32_t(n)) : 0;
    };
    const int n = std::max(3, b.w / 40);
    for (int k = 0; k < n; ++k) {
        int x = b.x + rnd(b.w), y = b.y + rnd(b.h);
        const int len1 = b.w / 8 + rnd(b.w / 4), len2 = b.h / 10 + rnd(b.h / 5);
        const int dir = rnd(2) ? 1 : -1;
        int x2 = std::max(b.x + 2, std::min(b.x + b.w - 3, x + dir * len1));
        c.hline(std::min(x, x2), std::max(x, x2), y, kTrace);
        const int y2 = std::max(b.y + 2, std::min(b.y + b.h - 3, y + (rnd(2) ? len2 : -len2)));
        c.vline(x2, std::min(y, y2), std::max(y, y2), kTrace);
        c.ring(float(x), float(y), 2.2f, 1.f, kTrace);
        c.fillCircle(float(x2), float(y2), 1.8f, kTrace);
    }
}

// The exhaust mark (a bolt in a ring), bottom right of cards whose ability exhausts them.
void exhaustMark(Canvas& c, float cx, float cy, float s, uint32_t col, int alpha) {
    c.ring(cx, cy, s * 0.5f, std::max(1.f, s * 0.09f), col, alpha);
    const float xs[6] = {cx + s * 0.08f, cx - s * 0.20f, cx - s * 0.01f, cx - s * 0.10f, cx + s * 0.20f, cx + s * 0.01f};
    const float ys[6] = {cy - s * 0.34f, cy + s * 0.05f, cy + s * 0.05f, cy + s * 0.34f, cy - s * 0.06f, cy - s * 0.06f};
    c.fillPolygon(xs, ys, 6, col, alpha);
}

// a <-> b: two skip symbols and the exchange arrows between them, centred on (cx, cy).
// (The printed cards put the colours the other way round from the data set's order: Psychospore shows warfare on the
// left and biotic on the right - left() / right() below.)
TechColor left(const BreakthroughDef& b) { return b.b; }
TechColor right(const BreakthroughDef& b) { return b.a; }

void synergy(Canvas& c, const BreakthroughDef& b, float cx, float cy, int icon, uint32_t arrowCol) {
    if (b.a == TechColor::None || b.b == TechColor::None) {
        drawTextAligned(c, FONT_XS, int(cx) - 60, int(cy) - 7, 120, Align::Center, "NO SYNERGY", rgb(150, 154, 166));
        return;
    }
    const float gap = float(icon) * 0.35f, aw = float(icon) * 1.1f;
    const int lx = int(cx - aw / 2 - gap - float(icon)), rx = int(cx + aw / 2 + gap);
    const int iy = int(cy - float(icon) / 2);
    if (icons::available()) {
        drawSkipIcon(c, left(b), lx, iy, icon);
        drawSkipIcon(c, right(b), rx, iy, icon);
    } else {
        c.fillCircle(float(lx) + icon / 2.f, cy, icon * 0.4f, synCol(left(b)));
        c.fillCircle(float(rx) + icon / 2.f, cy, icon * 0.4f, synCol(right(b)));
    }
    const float t = std::max(1.f, float(icon) * 0.08f), hd = float(icon) * 0.2f;
    const float x0 = cx - aw / 2, x1 = cx + aw / 2, y0 = cy - float(icon) * 0.16f, y1 = cy + float(icon) * 0.16f;
    c.line(x0, y0, x1 - hd * 0.5f, y0, arrowCol, t);
    const float ax[3] = {x1, x1 - hd, x1 - hd}, ay[3] = {y0, y0 - hd * 0.7f, y0 + hd * 0.7f};
    c.fillPolygon(ax, ay, 3, arrowCol);
    c.line(x0 + hd * 0.5f, y1, x1, y1, arrowCol, t);
    const float bx[3] = {x0, x0 + hd, x0 + hd}, by[3] = {y1, y1 - hd * 0.7f, y1 + hd * 0.7f};
    c.fillPolygon(bx, by, 3, arrowCol);
}

// The ability text: word-wrapped, "Action:" in bold capitals as on the cards. Returns the height used, or -1 if
// it doesn't fit in maxH.
struct Word {
    std::string s;
    bool bold;
};
int layout(const std::vector<Word>& ws, const FontData& f, const FontData& fb, int w, std::vector<std::vector<int>>& lines) {
    lines.clear();
    lines.emplace_back();
    const int sp = textWidth(f, " ");
    int x = 0;
    for (size_t i = 0; i < ws.size(); ++i) {
        const int ww = textWidth(ws[i].bold ? fb : f, ws[i].s);
        if (x > 0 && x + sp + ww > w) {
            lines.emplace_back();
            x = 0;
        }
        x += (x > 0 ? sp : 0) + ww;
        lines.back().push_back(int(i));
    }
    return int(lines.size()) * f.lineHeight;
}
void abilityText(Canvas& c, const std::string& text, Rect r, int alpha, int first) {
    std::vector<Word> ws;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        size_t j = i;
        while (j < text.size() && text[j] != ' ') ++j;
        if (j > i) {
            std::string w = text.substr(i, j - i);
            const bool bold = w == "Action:";
            ws.push_back({bold ? upper(w) : w, bold});
        }
        i = j;
    }
    static const FontData* const kF[4][2] = {{&FONT_MD, &FONT_MDB}, {&FONT_SM, &FONT_SMB}, {&FONT_XS, &FONT_SMB}, {&FONT_XS, &FONT_XS}};
    std::vector<std::vector<int>> lines;
    int k = first;  // the largest size the card allows, smaller if the text needs it
    for (; k < 3; ++k)
        if (layout(ws, *kF[k][0], *kF[k][1], r.w, lines) <= r.h) break;
    if (k == 3) layout(ws, *kF[3][0], *kF[3][1], r.w, lines);
    const FontData& f = *kF[k][0];
    const FontData& fb = *kF[k][1];
    const int sp = textWidth(f, " ");
    int y = r.y;
    for (const auto& ln : lines) {
        if (y + f.lineHeight > r.y + r.h + f.lineHeight / 3) break;
        int x = r.x;
        for (int wi : ln) {
            const Word& w = ws[size_t(wi)];
            const FontData& ff = w.bold ? fb : f;
            drawText(c, ff, x, y + (w.bold ? f.ascent - ff.ascent : 0), w.s, kInk, alpha);
            x += textWidth(ff, w.s) + sp;
        }
        y += f.lineHeight;
    }
}

void selFrame(Canvas& c, Rect r, int rad, uint32_t now) {
    const float pulse = 0.5f + 0.5f * std::sin(float(now % 1500) / 1500.f * 6.2831853f);
    c.frameRoundRect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, rad + 2, mix(theme::goldDim, theme::gold, int(pulse * 255)), 2);
}

// Portrait: the card as printed.
void drawFull(Canvas& c, int faction, const BreakthroughDef& b, Rect r, State st, bool sel, uint32_t now) {
    const int W = r.w, H = r.h;
    const int rad = std::max(4, W * 5 / 100), bw = std::max(2, W * 22 / 1000);
    frame(c, r, rad, synCol(left(b)), synCol(right(b)));
    const Rect in{r.x + bw, r.y + bw, W - 2 * bw, H - 2 * bw};
    const int irad = std::max(2, rad - bw);
    c.fillRoundRect(in.x, in.y, in.w, in.h, irad, kHead);
    // the text panel, under the title band (deeper on the left, a slanted step on the right)
    const int topR = r.y + H * 165 / 1000, topL = r.y + H * 235 / 1000;
    c.fillRoundRect(in.x, topR, in.w, in.y + in.h - topR, irad, kBody);
    c.fillRect(in.x, topR, in.w, irad + 1, kBody);
    {
        const float sx0 = float(r.x + W * 80 / 100), sx1 = float(r.x + W * 87 / 100);
        const float xs[4] = {float(in.x), sx1, sx0, float(in.x)};
        const float ys[4] = {float(topR - 1), float(topR - 1), float(topL), float(topL)};
        c.fillPolygon(xs, ys, 4, kHead);
    }
    c.hline(in.x + 1, in.x + in.w - 2, in.y + 1, rgb(48, 56, 70));
    traces(c, {in.x + 4, topL + 4, in.w - 8, in.y + in.h - topL - 8}, faction);
    // emblem in its box
    const int es = std::max(14, H * 125 / 1000), ex = in.x + W * 3 / 100, ey = in.y + H * 25 / 1000;
    c.fillRoundRect(ex, ey, es, es, std::max(2, es / 7), rgb(40, 47, 60));
    c.frameRoundRect(ex, ey, es, es, std::max(2, es / 7), rgb(118, 128, 148), 1);
    if (icons::available()) drawFactionIcon(c, faction, ex + es / 10, ey + es / 10, es - es / 5);
    // name and faction
    const int nx = ex + es + W * 3 / 100, nw = in.x + in.w - nx - W * 3 / 100;
    const std::string name = upper(b.name);
    const FontData& ff = H >= 300 ? FONT_SMB : FONT_XS;
    const int facW = W * 78 / 100 - nx + r.x;
    std::string fac = upper(kFactions[faction].name);
    if (textWidth(ff, fac) > facW) fac = upper(factionShortName(faction));
    // the name: one line as large as fits, else two lines (the header has room for them above the faction)
    const int room = topL - in.y - ff.lineHeight - 3;
    static const struct {
        const FontData* f;
        int lines, minW;  // (the big fonts only on big cards)
    } kN[] = {{&FONT_XL, 1, 280}, {&FONT_LG, 1, 200}, {&FONT_MDB, 1, 0}, {&FONT_LG, 2, 200}, {&FONT_SMB, 1, 0},
              {&FONT_MDB, 2, 0},  {&FONT_SMB, 2, 0},  {&FONT_XS, 1, 0},  {&FONT_XS, 2, 0}};
    std::vector<std::string> nl;
    const FontData* nf = &FONT_XS;
    for (const auto& k : kN) {
        if (k.f->lineHeight * k.lines > room || W < k.minW) continue;
        std::vector<std::string> ls = k.lines == 1 ? std::vector<std::string>{name} : wrapText(*k.f, name, nw);
        bool fits = int(ls.size()) == k.lines;
        for (const std::string& l : ls) fits = fits && textWidth(*k.f, l) <= nw;
        if (fits) {
            nl = ls;
            nf = k.f;
            break;
        }
    }
    if (nl.empty()) nl = {fitText(FONT_XS, name, nw)};
    const int nameH = int(nl.size()) * nf->lineHeight;
    const int nameY = in.y + std::max(1, (topL - in.y - nameH - ff.lineHeight) / 2 - 1);
    for (size_t k = 0; k < nl.size(); ++k) drawText(c, *nf, nx, nameY + int(k) * nf->lineHeight, nl[k], rgb(242, 244, 248));
    drawText(c, ff, nx, std::min(topL - ff.lineHeight - 1, nameY + nameH + 1), fitText(ff, fac, facW), rgb(188, 196, 208));
    // the foot: synergy in a dark trapezoid on the bottom edge
    const int bandH = std::max(14, H * 95 / 1000), by1 = in.y + in.h, by0 = by1 - bandH;
    {
        const float cx = float(r.x) + float(W) / 2.f, wb = float(W) * 0.52f, wt = float(W) * 0.40f;
        const float xs[4] = {cx - wt / 2, cx + wt / 2, cx + wb / 2, cx - wb / 2};
        const float ys[4] = {float(by0), float(by0), float(by1), float(by1)};
        c.fillPolygon(xs, ys, 4, kHead);
        synergy(c, b, cx, float(by0 + by1) / 2.f + 1.f, std::max(10, bandH * 64 / 100), rgb(200, 206, 216));
    }
    if (exhausts(b)) {
        const float s = float(std::max(10, W * 9 / 100));
        exhaustMark(c, float(in.x + in.w) - s * 0.9f, float(by1) - s * 0.85f, s, rgb(46, 52, 64), 255);
    }
    // the state: a band across the foot of the text panel (the text keeps clear of it)
    const FontData& pf = H >= 300 ? FONT_SMB : FONT_XS;
    const int ribH = st == Ready ? 0 : pf.lineHeight + 4, ribY = by0 - ribH - 3;
    // the ability
    const int tx = in.x + W * 6 / 100, tw = in.w - W * 12 / 100, ty = topL + H * 3 / 100;
    abilityText(c, b.text, {tx, ty, tw, (st == Ready ? by0 - H * 2 / 100 : ribY - 2) - ty}, 255, W >= 260 ? 0 : W >= 140 ? 1 : 2);
    if (st != Ready) {
        c.fillRoundRect(r.x, r.y, r.w, r.h, rad, rgb(0, 0, 0), st == Exhausted ? 100 : 50);
        c.fillRect(in.x, ribY, in.w, ribH, rgb(24, 28, 36), 230);
        drawTextAligned(c, pf, in.x, ribY + 2, in.w, Align::Center, st == Exhausted ? "EXHAUSTED" : "NOT GAINED YET",
                        st == Exhausted ? rgb(176, 182, 196) : theme::gold);
    }
    if (sel) selFrame(c, r, rad, now);
}

// Landscape / small: the title band and the synergy, for card grids and the faction sheet.
void drawCompact(Canvas& c, int faction, const BreakthroughDef& b, Rect r, State st, bool sel, uint32_t now) {
    const int W = r.w, H = r.h;
    const int rad = std::max(4, H * 9 / 100), bw = std::max(2, H * 4 / 100);
    frame(c, r, rad, synCol(left(b)), synCol(right(b)));
    const Rect in{r.x + bw, r.y + bw, W - 2 * bw, H - 2 * bw};
    const int irad = std::max(2, rad - bw);
    c.fillRoundRect(in.x, in.y, in.w, in.h, irad, kHead);
    const int topL = r.y + H * 56 / 100, topR = r.y + H * 46 / 100;
    c.fillRoundRect(in.x, topR, in.w, in.y + in.h - topR, irad, kBody);
    c.fillRect(in.x, topR, in.w, irad + 1, kBody);
    {
        const float sx0 = float(r.x + W * 66 / 100), sx1 = float(r.x + W * 72 / 100);
        const float xs[4] = {float(in.x), sx1, sx0, float(in.x)};
        const float ys[4] = {float(topR - 1), float(topR - 1), float(topL), float(topL)};
        c.fillPolygon(xs, ys, 4, kHead);
    }
    traces(c, {in.x + 3, topL + 2, in.w - 6, in.y + in.h - topL - 4}, faction);
    const int es = std::min(topL - in.y - 6, H * 44 / 100), ex = in.x + 4, ey = in.y + 3;
    c.fillRoundRect(ex, ey, es, es, std::max(2, es / 7), rgb(40, 47, 60));
    c.frameRoundRect(ex, ey, es, es, std::max(2, es / 7), rgb(118, 128, 148), 1);
    if (icons::available()) drawFactionIcon(c, faction, ex + es / 10, ey + es / 10, es - es / 5);
    const int nx = ex + es + 6, nw = in.x + in.w - nx - 4;
    const std::string name = upper(b.name);
    const FontData* nf = H >= 90 && textWidth(FONT_LG, name) <= nw ? &FONT_LG : &FONT_SMB;
    if (textWidth(*nf, name) > nw && nf == &FONT_SMB && textWidth(FONT_XS, name) <= nw) nf = &FONT_XS;
    drawText(c, *nf, nx, in.y + 2, fitText(*nf, name, nw), rgb(242, 244, 248));
    std::string fac = upper(kFactions[faction].name);
    if (textWidth(FONT_XS, fac) > W * 64 / 100 - nx + r.x) fac = upper(factionShortName(faction));
    drawText(c, FONT_XS, nx, std::min(topL - 14, in.y + 2 + nf->lineHeight), fitText(FONT_XS, fac, W * 64 / 100 - nx + r.x), rgb(188, 196, 208));
    // body: what it is and its state, the synergy in a dark pill on the right
    const int bodyY = topL, bodyH = in.y + in.h - topL;
    const int icon = std::max(10, std::min(16, bodyH * 55 / 100));
    const bool none = b.a == TechColor::None || b.b == TechColor::None;
    const int pw = none ? textWidth(FONT_XS, "NO SYNERGY") + 14 : icon * 4, px = in.x + in.w - pw - 4, py = bodyY + (bodyH - icon - 6) / 2;
    c.fillRoundRect(px, py, pw, icon + 6, (icon + 6) / 2, kHead);
    synergy(c, b, float(px) + float(pw) / 2.f, float(py) + float(icon + 6) / 2.f, icon, rgb(200, 206, 216));
    const char* s = st == NotGained ? "Not gained yet" : st == Exhausted ? "Exhausted" : exhausts(b) ? "Gained \xC2\xB7 ready" : "Gained";
    const uint32_t sc = st == NotGained ? rgb(150, 110, 30) : st == Exhausted ? rgb(110, 114, 124) : rgb(30, 130, 60);
    const int tx = in.x + 6, tw = px - tx - 4;
    if (bodyH >= 30) {
        drawText(c, FONT_XS, tx, bodyY + bodyH / 2 - 14, fitText(FONT_XS, "BREAKTHROUGH", tw), rgb(96, 102, 116));
        drawText(c, FONT_SMB, tx, bodyY + bodyH / 2 - 1, fitText(FONT_SMB, s, tw), sc);
    } else {
        drawText(c, FONT_XS, tx, bodyY + (bodyH - 14) / 2, fitText(FONT_XS, s, tw), sc);
    }
    if (st != Ready) c.fillRoundRect(r.x, r.y, r.w, r.h, rad, rgb(0, 0, 0), st == Exhausted ? 90 : 60);
    if (sel) selFrame(c, r, rad, now);
}

}  // namespace

bool draw(Canvas& c, int faction, Rect r, State st, bool sel, uint32_t now) {
    const BreakthroughDef* b = defOf(faction);
    if (!b || r.w < 20 || r.h < 20) return false;
    if (r.h >= r.w) drawFull(c, faction, *b, r, st, sel, now);
    else drawCompact(c, faction, *b, r, st, sel, now);
    return true;
}

bool has(const Game& g, int p) {
    return p >= 0 && p < g.numPlayers && breakthroughsOn(g) && breakthroughFor(g, p) != nullptr;
}

State stateOf(const Game& g, int p) {
    if (!has(g, p)) return NotGained;
    const Player& pl = g.players[size_t(p)];
    return !pl.breakthrough ? NotGained : pl.btExhausted ? Exhausted : Ready;
}

std::string statusLine(const Game& g, int p, int viewer) {
    if (!has(g, p)) return "";
    const BreakthroughDef* b = defOf(g.players[size_t(p)].faction);
    switch (stateOf(g, p)) {
        case NotGained:
            if (p != viewer)
                return "Not gained yet. Their first slice of the expedition to Thunder's Edge gains it (so does taking the Thunder's "
                       "Edge planet, or The Quantumcore relic).";
            return "Not gained yet. Your first slice of the expedition to Thunder's Edge gains it (so does taking the Thunder's Edge "
                   "planet, or The Quantumcore relic).";
        case Exhausted: return "Gained - exhausted, readied again in the status phase.";
        default: return b && exhausts(*b) ? "Gained - ready to use." : "Gained - in effect.";
    }
}

std::string synergyLine(int faction) {
    const BreakthroughDef* b = defOf(faction);
    if (!b) return "";
    if (b->a == TechColor::None || b->b == TechColor::None) return "No synergy: this breakthrough only grants its ability.";
    return std::string("Synergy: once gained, your ") + colorName(left(*b)) + " and " + colorName(right(*b)) +
           " technologies stand in for each other as prerequisites.";
}

void drawLarge(Canvas& c, const Game& g, int p, int viewer, Rect r, uint32_t now) {
    if (p < 0 || p >= g.numPlayers) return;
    const int f = g.players[size_t(p)].faction;
    const BreakthroughDef* b = defOf(f);
    if (!b) return;
    const uint32_t pc = pcolours::base(g.players[size_t(p)].color);
    drawPanel(c, r, rgb(10, 14, 26), pc, 12);
    const int ch = r.h - 20, cw = std::min(ch * 716 / 1000, r.w / 2);
    const Rect card{r.x + 10, r.y + 10, cw, ch};
    draw(c, f, card, stateOf(g, p), false, now);
    const int x = card.x + card.w + 16, w = r.x + r.w - 14 - x;
    int y = r.y + 16;
    auto para = [&](const FontData& fn, const std::string& s, uint32_t col, int after) {
        for (const std::string& l : wrapText(fn, s, w)) {
            if (y + fn.lineHeight > r.y + r.h - 8) return;
            drawText(c, fn, x, y, l, col);
            y += fn.lineHeight;
        }
        y += after;
    };
    para(FONT_XS, "BREAKTHROUGH \xC2\xB7 THUNDER'S EDGE", theme::gold, 2);
    para(FONT_LG, b->name, theme::text, 0);
    para(FONT_SMB, playerName(g, p), pc, 10);
    const State st = stateOf(g, p);
    para(FONT_SMB, statusLine(g, p, viewer), st == NotGained ? theme::gold : st == Exhausted ? theme::dim : theme::green, 8);
    para(FONT_SM, synergyLine(f), theme::text, 6);
    if (b->a != TechColor::None && b->b != TechColor::None && icons::available()) {
        drawSkipIcon(c, left(*b), x, y, 22);
        drawText(c, FONT_MDB, x + 27, y + 2, "\xE2\x86\x90\xE2\x86\x92", theme::dim);
        drawSkipIcon(c, right(*b), x + 30 + textWidth(FONT_MDB, "\xE2\x86\x90\xE2\x86\x92"), y, 22);
        y += 30;
    }
    if (allSynergy(g, p)) para(FONT_SM, "The Quantumcore: every colour stands in for every other.", theme::cyan, 6);
    if (!std::strncmp(b->text, "Action:", 7))
        para(FONT_XS, "Use it from your action list when you can - it is a component action.", theme::dim, 4);
    else para(FONT_XS, "The game asks you when it can be used, or applies it by itself.", theme::dim, 4);
    if (exhausts(*b)) para(FONT_XS, "Using it exhausts the card; it readies again in the status phase.", theme::dim, 4);
    if (!b->done) para(FONT_XS, "This ability isn't active in this version yet - the synergy is.", theme::gold, 0);
}

}  // namespace btcard
