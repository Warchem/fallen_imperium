// Monuments+ drafts (the 0.82.0 plan, owner 2026-11-21 14:48: "send me the draft of how it is to be implemented ... the menu
// designs, how the monuments are placed, the new monuments structure design that will go on planets, a card example for the
// monument card with the art (Let's use Revenance Circuit as the example), any token designs"). Drawn by the game's own code on a
// real game (the Ghosts of Creuss in seat 0, round 2): TI4_UITEST_ONLY=monumentdrafts. The pictures: TI4_MON_ARTDIR = a folder of
// <key>_strip.png from tools/gen_monument_anim.py (64 frames of 160 x 120 side by side); TI4_MON_ANIM=1 also writes every frame
// of the Creuss card and of the Monuments screen (anim_card_NN / anim_screen_NN).
// Nothing here is part of the game yet: the hooks it uses (tileview::setDraftMonuments, setMapDraftMonuments) draw only for it.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ai/ai.h"
#include "engine/data.h"
#include "engine/engine.h"
#include "engine/query.h"
#include "engine/state.h"
#include "gfx/canvas.h"
#include "gfx/font.h"
#include "gfx/image.h"
#include "gfx/picture.h"
#include "ui/decision_ui.h"
#include "ui/hits.h"
#include "ui/icons.h"
#include "ui/map_view.h"
#include "ui/pcolors.h"
#include "ui/retro.h"
#include "ui/theme.h"
#include "ui/tile_view.h"
#include "ui/widgets.h"

using namespace ti;

namespace {
float testScale() {  // (as uitest.cpp: TI4_SCALE=2.25 draws as on a 1080p monitor)
    const char* e = std::getenv("TI4_SCALE");
    const float v = e ? float(std::atof(e)) : 1.f;
    Canvas::screenScale() = v > 1.f ? v : 1.f;
    return v > 1.f ? v : 1.f;
}
void savePpm(const Canvas& c, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", c.pw(), c.ph());
    for (int i = 0; i < c.pw() * c.ph(); ++i) {
        const uint32_t p = c.pixels()[i];
        const unsigned char v[3] = {uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p)};
        std::fwrite(v, 1, 3, f);
    }
    std::fclose(f);
}
bool loadFile(const char* path, Image& out) {
    if (!path || !*path) return false;
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> b;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    return decodePicture(b.data(), b.size(), out);
}
uint32_t pc(const Game& g, int p) { return p == kNeutral ? theme::neutral : pcolours::base(g.players[size_t(p)].color); }

// The monument colour: weathered stone with a teal sheen (Monuments+'s card frames), one for every faction
const uint32_t kMon = rgb(64, 168, 176);

// The Monuments+ mark: a temple (pediment, three columns, steps)
void temple(Canvas& c, float cx, float cy, float s, uint32_t col, int a = 255) {
    float tx[3] = {cx - s * 0.62f, cx, cx + s * 0.62f}, ty[3] = {cy - s * 0.18f, cy - s * 0.58f, cy - s * 0.18f};
    c.fillPolygon(tx, ty, 3, col, a);
    c.fillCircle(cx, cy - s * 0.31f, s * 0.07f, rgb(10, 12, 20), a);
    c.fillRect(int(cx - s * 0.6f), int(cy - s * 0.14f), int(s * 1.2f), std::max(1, int(s * 0.08f)), col, a);
    for (int k = -1; k <= 1; ++k) c.fillRect(int(cx + float(k) * s * 0.36f - s * 0.09f), int(cy - s * 0.02f), std::max(1, int(s * 0.18f)), int(s * 0.42f), col, a);
    c.fillRect(int(cx - s * 0.64f), int(cy + s * 0.42f), int(s * 1.28f), std::max(1, int(s * 0.09f)), col, a);
    c.fillRect(int(cx - s * 0.72f), int(cy + s * 0.54f), int(s * 1.44f), std::max(1, int(s * 0.09f)), col, a);
}

// The placement icons (as on Monuments+'s reference card): traits, technology specialty, legendary, Mecatol Rex, space, relic, "not"
enum Place { kIndustrial, kCultural, kHazardous, kSpecialty, kLegendary, kMecatol, kSpace, kRelic };
void placeIcon(Canvas& c, Place p, int x, int y, int s, bool crossed = false) {
    switch (p) {
        case kIndustrial: drawTraitIcon(c, Trait::Industrial, x, y, s); break;
        case kCultural: drawTraitIcon(c, Trait::Cultural, x, y, s); break;
        case kHazardous: drawTraitIcon(c, Trait::Hazardous, x, y, s); break;
        case kSpecialty: {
            const int h = s / 2;
            drawSkipIcon(c, TechColor::Blue, x, y, h);
            drawSkipIcon(c, TechColor::Green, x + h, y, h);
            drawSkipIcon(c, TechColor::Red, x, y + h, h);
            drawSkipIcon(c, TechColor::Yellow, x + h, y + h, h);
            break;
        }
        case kLegendary:
            c.fillCircle(float(x + s / 2), float(y + s / 2), float(s) * 0.5f, rgb(10, 12, 20));
            c.ring(float(x + s / 2), float(y + s / 2), float(s) * 0.5f, 1.5f, theme::gold);
            icons::draw(c, "legendary_white", x + s / 6, y + s / 6, s * 2 / 3, 255, theme::gold);
            break;
        case kMecatol:
            c.fillCircle(float(x + s / 2), float(y + s / 2), float(s) * 0.46f, rgb(70, 64, 56));
            c.ring(float(x + s / 2), float(y + s / 2), float(s) * 0.46f, 1.5f, rgb(255, 215, 120));
            drawTextAligned(c, FONT_XS, x, y + s / 2 - 6, s, Align::Center, "MR", rgb(255, 225, 150));
            break;
        case kSpace: {
            const float cx = float(x + s / 2), cy = float(y + s / 2), r = float(s) * 0.5f;
            float hx[6], hy[6];
            for (int k = 0; k < 6; ++k) hx[k] = cx + r * std::cos(float(k) * 1.0472f), hy[k] = cy + r * 0.9f * std::sin(float(k) * 1.0472f);
            c.fillPolygon(hx, hy, 6, rgb(14, 20, 40));
            for (int k = 0; k < 6; ++k) c.line(hx[k], hy[k], hx[(k + 1) % 6], hy[(k + 1) % 6], rgb(150, 190, 255), 1.3f);
            c.fillCircle(cx, cy, 1.6f, rgb(255, 255, 255));
            break;
        }
        case kRelic: icons::draw(c, "relic", x, y, s, 255, rgb(255, 200, 120)); break;
    }
    if (crossed) {
        c.line(float(x), float(y), float(x + s), float(y + s), rgb(230, 60, 50), 2.5f);
        c.line(float(x + s), float(y), float(x), float(y + s), rgb(230, 60, 50), 2.5f);
    }
}

// a round token: a dark disc with a coloured rim and a picture
void token(Canvas& c, float cx, float cy, float r, uint32_t rim) {
    c.fillCircle(cx, cy, r + 3.f, rgb(0, 0, 0), 150);
    c.fillCircle(cx, cy, r, rgb(12, 16, 30));
    for (int k = 0; k < 18; ++k) {  // a few stars
        const float a = float(k) * 2.4f, d = r * (0.25f + 0.7f * float((k * 37) % 10) / 10.f);
        c.fillRect(int(cx + std::cos(a) * d), int(cy + std::sin(a) * d), 1, 1, rgb(200, 210, 240), 160);
    }
    c.ring(cx, cy, r, 3.f, rim);
    c.ring(cx, cy, r - 5.f, 1.f, shade(rim, 0.6f));
}

// ---------------------------------------------------------------- the card in Monuments+'s own layout (owner 2026-11-21 19:03:
// "If we used the card format that's in the reference, we should be fine"): a landscape card - header with the faction's emblem,
// the name and "<FACTION> MONUMENT", the animated picture on the left with the placement icons under it, the ability text (and its
// PRODUCTION / SPACE CANNON lines) on the right, the flavour text under a rule. Each faction's frame and subtitle colours come from
// its Monuments+ card.
const int kArtW = 160, kArtH = 120, kArtFrames = 64;
struct MonRef {
    const char* key;      // the picture: <TI4_MON_ARTDIR>/<key>_strip.png (tools/gen_monument_anim.py)
    const char* name;
    const char* faction;  // findFaction key
    const char* subtitle;
    const char* ability;  // paragraphs split by '\n'
    std::vector<const char*> stats;
    const char* flavour;
    std::vector<std::pair<Place, bool>> places;  // (icon, crossed out)
    const char* where;                            // the placement in words, under the picture
    uint32_t frame, sub;
    float focusY;  // where the subject sits (0 top .. 1 bottom), for the Monuments screen's strip of the picture
};
std::vector<std::pair<std::string, Image>> g_strips;
const Image* stripFor(const char* key) {
    for (auto& s : g_strips)
        if (s.first == key) return &s.second;
    const char* dir = std::getenv("TI4_MON_ARTDIR");
    Image img;
    if (dir) loadFile((std::string(dir) + "/" + key + "_strip.png").c_str(), img);
    g_strips.push_back({key, img});
    return g_strips.back().second.empty() ? nullptr : &g_strips.back().second;
}
int artFrame(uint32_t now) { return int((now / 100u) % uint32_t(kArtFrames)); }

// One frame of the animated picture into box: contain = whole picture (whole-number enlargement when it is at least 2x), cover = the
// box filled, cut top and bottom around focusY
void blitArt(Canvas& c, const Image* strip, int frame, Rect box, bool cover, float focusY = 0.5f) {
    const Rect p = c.toPx(box);
    if (p.w <= 0 || p.h <= 0) return;
    Image out;
    out.w = p.w;
    out.h = p.h;
    out.px.assign(size_t(p.w) * size_t(p.h), 0xFF0A0C14u);
    if (strip && strip->w >= kArtW * kArtFrames && strip->h >= kArtH) {
        float k = cover ? std::max(float(p.w) / kArtW, float(p.h) / kArtH) : std::min(float(p.w) / kArtW, float(p.h) / kArtH);
        if (!cover && k >= 2.f) k = std::floor(k);
        const int dw = int(float(kArtW) * k + 0.5f), dh = int(float(kArtH) * k + 0.5f);
        const int ox = (p.w - dw) / 2, oy = cover ? -int(float(dh - p.h) * focusY) : (p.h - dh) / 2;
        for (int y = 0; y < p.h; ++y) {
            const int sy = int(float(y - oy) / k);
            if (y < oy || sy >= kArtH) continue;
            for (int x = 0; x < p.w; ++x) {
                const int sx = int(float(x - ox) / k);
                if (x < ox || sx >= kArtW) continue;
                out.px[size_t(y) * size_t(p.w) + size_t(x)] = strip->px[size_t(sy) * size_t(strip->w) + size_t(frame * kArtW + sx)] | 0xFF000000u;
            }
        }
    }
    c.pxBlit(out, p.x, p.y, 255);
}

// the four-pointed star in front of PRODUCTION / SPACE CANNON lines
void statStar(Canvas& c, float cx, float cy, float r, uint32_t col) {
    float xs[8], ys[8];
    for (int i = 0; i < 8; ++i) {
        const float a = float(i) * 0.785398f - 1.5708f, rr = (i % 2) ? r * 0.3f : r;
        xs[i] = cx + std::cos(a) * rr, ys[i] = cy + std::sin(a) * rr;
    }
    c.fillPolygon(xs, ys, 8, col);
}

std::string upper(std::string s) {
    for (char& ch : s) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

void drawRefCard(Canvas& c, const MonRef& m, Rect r, uint32_t now) {
    // the card, its glow and a scatter of stars in the border
    c.frameRoundRect(r.x - 4, r.y - 4, r.w + 8, r.h + 8, 18, m.frame, 4, 70);
    c.fillRoundRect(r.x, r.y, r.w, r.h, 14, rgb(8, 10, 18));
    for (int k = 0; k < 90; ++k) {
        const int sx = r.x + 3 + int((uint32_t(k) * 2654435761u >> 7) % uint32_t(r.w - 6));
        const int sy = r.y + 3 + int((uint32_t(k) * 40503u * 977u >> 5) % uint32_t(r.h - 6));
        const bool edge = sx < r.x + 9 || sx > r.x + r.w - 10 || sy < r.y + 9 || sy > r.y + r.h - 10;
        if (edge) c.fillRect(sx, sy, 1, 1, rgb(210, 220, 255), 120 + (k * 37) % 120);
    }
    c.frameRoundRect(r.x + 7, r.y + 7, r.w - 14, r.h - 14, 11, m.frame, 2, 235);
    c.frameRoundRect(r.x + 9, r.y + 9, r.w - 18, r.h - 18, 10, shade(m.frame, 0.5f), 1, 160);
    // header: emblem, name, "+ temple"
    const Rect h{r.x + 12, r.y + 12, r.w - 24, 60};
    c.fillRoundRect(h.x, h.y, h.w, h.h, 9, shade(m.frame, 0.2f));
    c.fillRect(h.x + h.w / 2, h.y + 2, h.w / 2 - 8, h.h - 4, shade(m.frame, 0.32f), 120);
    c.fillRect(h.x + 6, h.y + h.h - 3, h.w - 12, 2, m.frame, 200);
    const int fi = findFaction(m.faction);
    const float ecx = float(h.x + 34), ecy = float(h.y + 30);
    c.fillCircle(ecx, ecy, 25.f, rgb(10, 12, 20));
    c.ring(ecx, ecy, 25.f, 2.f, shade(m.frame, 1.3f));
    if (fi >= 0) drawFactionIcon(c, fi, int(ecx) - 19, int(ecy) - 19, 38);
    const std::string title = upper(m.name);
    const FontData& tf = textWidth(FONT_XL, title) <= h.w - 120 ? FONT_XL : FONT_LG;
    drawText(c, tf, h.x + 70, h.y + (&tf == &FONT_XL ? 8 : 14), title, rgb(246, 248, 252));
    drawText(c, FONT_SMB, h.x + h.w - 40, h.y + 6, "+", rgb(240, 244, 250));
    temple(c, float(h.x + h.w - 22), float(h.y + 15), 12.f, rgb(240, 244, 250));
    // the subtitle in a slanted tab hanging under the header
    {
        const std::string st = upper(m.subtitle);
        const int tw = textWidth(FONT_SMB, st) + 34;
        const float tx = float(h.x + 62), ty = float(h.y + h.h - 4);
        float xs[4] = {tx, tx + float(tw), tx + float(tw) - 12.f, tx}, ys[4] = {ty, ty, ty + 20.f, ty + 20.f};
        c.fillPolygon(xs, ys, 4, shade(m.frame, 0.16f));
        c.line(tx, ty + 20.f, tx + float(tw) - 12.f, ty + 20.f, shade(m.frame, 0.8f), 1.f);
        drawText(c, FONT_SMB, int(tx) + 10, int(ty) + 3, st, m.sub);
    }
    const int by = h.y + h.h + 22;
    // the picture, the placement icons under it
    const Rect a{r.x + 16, by, 214, 160};
    c.fillRect(a.x - 1, a.y - 1, a.w + 2, a.h + 2, shade(m.frame, 0.9f));
    blitArt(c, stripFor(m.key), artFrame(now), a, false);
    const int colBottom = r.y + r.h - 28;
    {
        int wy = a.y + a.h + 8;
        for (const std::string& l : wrapText(FONT_SM, m.where, a.w - 4)) {
            drawTextAligned(c, FONT_SM, a.x, wy, a.w, Align::Center, l, rgb(190, 198, 214));
            wy += 14;
        }
        const Rect pp{a.x, colBottom - 42, a.w, 42};
        float xs[4] = {float(pp.x), float(pp.x + pp.w), float(pp.x + pp.w - 16), float(pp.x)};
        float ys[4] = {float(pp.y), float(pp.y), float(pp.y + pp.h), float(pp.y + pp.h)};
        c.fillPolygon(xs, ys, 4, rgb(13, 16, 26));
        c.line(xs[0], ys[0], xs[1], ys[1], shade(m.frame, 0.9f), 1.f);
        const int is = 28, gap = 8;
        const int total = int(m.places.size()) * is + int(m.places.size() - 1) * gap;
        int x = pp.x + (pp.w - 16 - total) / 2;
        for (const auto& pl : m.places) {
            placeIcon(c, pl.first, x, pp.y + 7, is, pl.second);
            x += is + gap;
        }
    }
    // the text panel: ability, stat lines, flavour
    const Rect tp{r.x + 242, by, r.w - 258, r.y + r.h - 28 - by};
    c.fillRoundRect(tp.x, tp.y, tp.w, tp.h, 8, rgb(12, 14, 22), 245);
    c.frameRoundRect(tp.x, tp.y, tp.w, tp.h, 8, shade(m.frame, 0.45f), 1, 200);
    for (int k = 0; k < 4; ++k) {  // faint circuit traces (as the card's panel)
        const float y0 = float(tp.y + tp.h * (k + 1) / 5), x0 = float(tp.x + 10 + k * 23);
        c.line(x0, y0, x0 + 70.f, y0, shade(m.frame, 0.35f), 1.f, 60);
        c.line(x0 + 70.f, y0, x0 + 86.f, y0 - 12.f, shade(m.frame, 0.35f), 1.f, 60);
        c.line(x0 + 86.f, y0 - 12.f, float(tp.x + tp.w - 12), y0 - 12.f, shade(m.frame, 0.35f), 1.f, 60);
    }
    std::vector<std::vector<std::string>> paras;
    {
        std::string cur;
        for (const char* q = m.ability;; ++q) {
            if (*q == '\n' || !*q) {
                paras.push_back({cur});
                cur.clear();
                if (!*q) break;
            } else cur += *q;
        }
    }
    const int tw = tp.w - 26;
    const FontData* af = &FONT_MDB;
    auto measure = [&](const FontData& f) {
        int hgt = 0;
        for (auto& pr : paras) hgt += int(wrapText(f, pr[0], tw).size()) * (f.lineHeight + 2) + 6;
        return hgt;
    };
    const int room = tp.h - 24 - int(m.stats.size()) * 18 - (m.flavour[0] ? 50 : 0);
    if (measure(*af) > room) af = &FONT_MD;
    int y = tp.y + 12;
    for (auto& pr : paras) {
        const auto lines = wrapText(*af, pr[0], tw);
        for (size_t l = 0; l < lines.size(); ++l) {
            drawText(c, *af, tp.x + 13, y, lines[l], rgb(242, 244, 248));
            if (l == 0) {  // ACTION: / DEPLOY: in the subtitle's colour
                const size_t colon = lines[l].find(':');
                if (colon != std::string::npos && colon < 10 && upper(lines[l].substr(0, colon)) == lines[l].substr(0, colon))
                    drawText(c, *af, tp.x + 13, y, lines[l].substr(0, colon + 1), theme::gold);
            }
            y += af->lineHeight + 2;
        }
        y += 6;
    }
    for (const char* st : m.stats) {
        statStar(c, float(tp.x + 20), float(y + 7), 6.f, rgb(240, 244, 250));
        drawText(c, FONT_SMB, tp.x + 31, y, st, rgb(240, 244, 250));
        y += 18;
    }
    if (m.flavour[0]) {
        const auto fl = wrapText(FONT_SM, m.flavour, tw - 10);
        int fy = tp.y + tp.h - 10 - int(fl.size()) * 14;
        c.fillRect(tp.x + tp.w / 6, fy - 9, tp.w * 2 / 3, 1, rgb(220, 226, 236), 200);
        for (const std::string& l : fl) {
            drawTextAligned(c, FONT_SM, tp.x, fy, tp.w, Align::Center, l, rgb(196, 204, 218));
            fy += 14;
        }
    }
    // (no credit line on the card - owner 2026-11-21 22:10: the Credits screen only)
}

// The whole screen when a monument is placed: a line above the card, the card, the buttons under it
void drawPlacedScreen(Canvas& c, const Game& g, const MonRef& m, const std::string& line, const std::vector<std::pair<const char*, const char*>>& buttons,
                      uint32_t now, int sys) {
    MapView map;
    map.setViewport(Rect{0, 22, 640, 440});
    map.setZoom(1);
    map.setCursor(g, sys);
    drawSpaceBackground(c, false);
    map.draw(c, g, now);
    c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 150);
    drawTextAligned(c, FONT_MDB, 0, 8, 640, Align::Center, line, theme::text);
    drawRefCard(c, m, Rect{33, 32, 574, 388}, now);
    int total = 0;
    for (const auto& b : buttons) total += textWidth(FONT_MDB, b.second) + 60;
    int bx = (640 - total) / 2;
    for (const auto& b : buttons) {
        const int bw = textWidth(FONT_MDB, b.second) + 52;
        c.fillRoundRect(bx, 428, bw, 24, 8, shade(m.frame, 0.45f));
        c.frameRoundRect(bx, 428, bw, 24, 8, shade(m.frame, 1.3f), 1, 200 + int(55 * std::sin(float(now) / 260.f)));
        drawButtonBadge(c, bx + 10, 432, b.first);
        drawText(c, FONT_MDB, bx + 36, 431, b.second, theme::text);
        bx += bw + 8;
    }
}

const std::vector<MonRef>& refCards() {
    static const std::vector<MonRef> v = {
        {"creuss", "Revenance Circuit", "ghost", "Creuss Monument",
         "After a player activates this system:\nYou may produce 1 hit against players' ships in each system adjacent to this planet that contains a wormhole.",
         {}, "A kaleidoscope of dimensional fire.", {{kIndustrial, false}, {kCultural, false}}, "Stands on an industrial or cultural planet.", rgb(70, 160, 200), rgb(110, 190, 225), 0.42f},
        {"saar", "Beacon of Ragh", "saar", "Saar Monument", "You may treat this system as being adjacent to 1 of your space docks.",
         {"SPACE CANNON 8 (x2)", "PRODUCTION 1"}, "A monument to the Call, glowing in the dark.", {{kIndustrial, false}}, "Stands on an industrial planet.", rgb(176, 132, 96),
         rgb(236, 220, 125), 0.5f},
        {"empyrean", "The Panopticon", "empyrean", "Empyrean Monument",
         "This unit is placed in the space area of any non-Fracture, non-supernova system that does not contain planets, instead of on a planet.\n"
         "If this unit is blockaded, destroy it.",
         {"PRODUCTION 1"}, "", {{kSpace, false}}, "Stands in the space area of a system with no planets.", rgb(150, 104, 176), rgb(214, 96, 90), 0.5f},
        {"keleres", "Pharus Iustitiae", "keleres", "Keleres Monument",
         "After a law enters play, you may add a command token from your reinforcements to this card.\n"
         "When you would place a command token in this or an adjacent system, you may place that token from this card instead.",
         {"PRODUCTION 1"}, "", {{kMecatol, true}, {kCultural, false}, {kLegendary, false}}, "Stands on a cultural or legendary planet, not Mecatol Rex.", rgb(110, 118, 190), rgb(214, 70, 180), 0.35f},
        {"muaat", "Glory Furnace", "muaat", "Muaat Monument",
         "When you exhaust this planet to cast votes, for each war sun or flagship you control in this system, and for each supernova adjacent to "
         "this system, choose: cast 3 additional votes, or produce a hit against another player's ships in this or an adjacent system.",
         {}, "", {{kHazardous, false}}, "Stands on a hazardous planet.", rgb(210, 104, 56), rgb(226, 160, 90), 0.5f},
    };
    return v;
}
}  // namespace

int monumentDrafts(const std::string& outDir) {
    const float sc = testScale();
    // ---------------------------------------------------------------- a real game: the Ghosts of Creuss in seat 0, into round 2
    Engine e;
    Setup s;
    s.players = 6;
    s.human = -1;
    s.seed = 4402;
    s.draft = 0;
    s.modules = kModCodex;
    const char* six[] = {"ghost", "sol", "hacan", "jolnar", "letnev", "muaat"};
    for (const char* f : six) s.factions.push_back(findFaction(f));
    s.colors = {1, 0, 3, 2, 4, 6};  // (the Creuss in blue, as usual)
    e.start(s);
    Rng rng(9);
    for (int steps = 0; e.waiting() && steps < 200000; ++steps) {
        const Game& g = e.game();
        if (g.round >= 2 && g.phase == Phase::Action) break;
        Answer a = aiAnswer(g, e.decision(), rng);
        if (!e.answer(a)) break;
        e.takeEvents();
    }
    Game& g = e.g();
    const int me = 0;
    // the monument's planet: an industrial or cultural planet the Creuss hold outside home (one next to home if they hold none)
    int planet = -1, sys = -1;
    for (size_t x = 0; x < g.planets.size() && planet < 0; ++x) {
        const PlanetState& ps = g.planets[x];
        if (ps.owner != me || int(ps.system) == g.players[size_t(me)].homeSystem) continue;
        const PlanetDef& d = kPlanets[ps.def];
        if (d.trait == Trait::Industrial || d.trait == Trait::Cultural) planet = int(x), sys = int(ps.system);
    }
    if (planet < 0) {  // (the Creuss home is off the board: a system with their ships, else any non-home planet nobody holds)
        std::vector<int> near;
        for (size_t sy = 0; sy < g.systems.size(); ++sy)
            if (!g.systems[sy].space[size_t(me)].empty() && int(sy) != g.players[size_t(me)].homeSystem) near.push_back(int(sy));
        for (size_t sy = 0; sy < g.systems.size(); ++sy) {
            bool home = false;
            for (const Player& p : g.players) home = home || p.homeSystem == int(sy);
            if (!home) near.push_back(int(sy));
        }
        for (int n : near)
            for (int16_t x : g.systems[size_t(n)].planets)
                if (planet < 0 && (kPlanets[g.planets[size_t(x)].def].trait == Trait::Industrial || kPlanets[g.planets[size_t(x)].def].trait == Trait::Cultural)) {
                    planet = x, sys = n;
                    for (int q = 0; q < g.numPlayers; ++q) g.planets[size_t(x)].units[size_t(q)] = Force();
                    g.planets[size_t(x)].owner = int8_t(me);
                }
    }
    if (planet < 0) {
        std::printf("monument drafts: no planet for the monument\n");
        return 0;
    }
    {  // two more industrial / cultural planets for the Creuss (the placement screen shows a choice)
        int more = 0;
        for (size_t x = 0; x < g.planets.size() && more < 2; ++x) {
            PlanetState& ps = g.planets[x];
            const Trait t = kPlanets[ps.def].trait;
            if (int(x) == planet || ps.owner == me || (t != Trait::Industrial && t != Trait::Cultural)) continue;
            bool near = false;
            for (int nb : g.systems[size_t(sys)].adj) near = near || nb == int(ps.system);
            if (!near && int(ps.system) != sys) continue;
            for (int q = 0; q < g.numPlayers; ++q) ps.units[size_t(q)] = Force();
            ps.owner = int8_t(me);
            ps.units[size_t(me)].n[int(UnitType::Infantry)] = 1;
            ++more;
        }
    }
    g.planets[size_t(planet)].units[size_t(me)].n[int(UnitType::Infantry)] = 2;
    g.planets[size_t(planet)].units[size_t(me)].n[int(UnitType::PDS)] = 1;
    std::printf("monument drafts: Revenance Circuit on %s (system %d)\n", kPlanets[g.planets[size_t(planet)].def].name, sys);
    tileview::setDraftMonuments({{planet, me}});
    setMapDraftMonuments({{planet, me}});
    int n = 0;

    // ---------------------------------------------------------------- 1. the structure on the planet, every tileset
    for (int ts : {retro::kSimplistic, retro::kRetrowave, retro::kVoidfall}) {
        if (!retro::tilesetAvailable(ts)) continue;
        retro::setTileset(ts);
        for (int f = 0; f < 4; ++f) {
            Canvas c(640, 480, sc);
            drawSpaceBackground(c, false);
            tileview::draw(c, g, sys, Rect{20, 10, 600, 460}, 1000 + uint32_t(f) * 200);
            if (f == 3) savePpm(c, outDir + "/mon_tile_" + std::to_string(ts) + ".ppm"), ++n;
        }
        MapView map;
        map.setViewport(Rect{0, 22, 640, 440});
        map.setZoom(3);
        map.setCursor(g, sys);
        for (int f = 0; f < 4; ++f) {
            Canvas c(640, 480, sc);
            drawSpaceBackground(c, false);
            map.draw(c, g, 1000 + uint32_t(f) * 200);
            if (f == 3) savePpm(c, outDir + "/mon_map_" + std::to_string(ts) + ".ppm"), ++n;
        }
    }
    retro::setTileset(retro::kSimplistic);

    // ---------------------------------------------------------------- 2. the card in Monuments+'s layout (owner 19:03), five examples
    {
        const std::string onPl = kPlanets[g.planets[size_t(planet)].def].name;
        const auto& cards = refCards();
        const char* lines[] = {"", "The Clan of Saar placed their monument on Lodor", "The Empyrean placed their monument in an empty system",
                               "The Council Keleres placed their monument on Abyz", "The Embers of Muaat placed their monument on Lisis II"};
        for (size_t i = 0; i < cards.size(); ++i) {
            Canvas c(640, 480, sc);
            const std::string line = i == 0 ? "The Ghosts of Creuss placed their monument on " + onPl : std::string(lines[i]);
            drawPlacedScreen(c, g, cards[i], line, {{"A", "Continue"}}, 1400 + uint32_t(i) * 700, sys);
            savePpm(c, outDir + "/mon_ref_" + cards[i].key + ".ppm");
            ++n;
        }
        // yours: the card first, then the planet pick (section 3)
        Canvas c(640, 480, sc);
        drawPlacedScreen(c, g, cards[0], "Construction: you may place your monument now", {{"A", "Choose a planet"}, {"B", "Not now"}}, 2000, sys);
        savePpm(c, outDir + "/mon_ref_yours.ppm");
        ++n;
        if (std::getenv("TI4_MON_ANIM"))  // the loop, frame by frame
            for (int f = 0; f < kArtFrames; ++f) {
                Canvas a(640, 480, sc);
                drawPlacedScreen(a, g, cards[0], "The Ghosts of Creuss placed their monument on " + onPl, {{"A", "Continue"}}, uint32_t(f) * 100, sys);
                char nm[64];
                std::snprintf(nm, sizeof nm, "/anim_card_%02d.ppm", f);
                savePpm(a, outDir + nm);
            }
    }

    // ---------------------------------------------------------------- 3. placing it: the planet pick after the Construction primary
    {
        Decision d;
        d.player = me;
        d.kind = DecisionKind::Pick;
        d.tag = Tag::ConstructionWhere;
        d.onPlanets = true;
        d.prompt = "Revenance Circuit: place your monument on which planet?";
        d.context = "Monument Funding: after the Construction primary, your monument may go on an industrial or cultural planet you "
                    "control. Highlighted: where it may stand.";
        for (size_t x = 0; x < g.planets.size(); ++x) {
            const PlanetState& ps = g.planets[x];
            if (ps.owner != me) continue;
            const Trait t = kPlanets[ps.def].trait;
            if (t == Trait::Industrial || t == Trait::Cultural)
                d.options.push_back({int(x), kPlanets[ps.def].name, std::string(t == Trait::Industrial ? "Industrial" : "Cultural")});
        }
        d.options.push_back({-1, "Not now", "Keep it for a later Construction primary."});
        tileview::setDraftMonuments({});
        setMapDraftMonuments({});
        MapView map;
        map.setViewport(Rect{0, 22, 420, 430});
        map.setZoom(1);
        map.setCursor(g, sys);
        std::unique_ptr<DecisionUI> ui = makeDecisionUI(g, d, map);
        map.setZoom(2);
        map.setCursor(g, sys);
        for (int f = 0; f < 3; ++f) {
            Canvas c(640, 480, sc);
            drawSpaceBackground(c, false);
            map.draw(c, g, 1000 + uint32_t(f) * 300);
            ui->draw(c, 1000 + uint32_t(f) * 300);
            {  // the side panel while choosing: the monument's card in short (as a leader's when a question names it)
                const Rect sp{424, 26, 212, 418};
                drawPanel(c, sp, rgb(14, 22, 30), kMon, 8, 225);
                drawFactionIcon(c, g.players[size_t(me)].faction, sp.x + 8, sp.y + 8, 22);
                drawText(c, FONT_XS, sp.x + 36, sp.y + 8, "CREUSS MONUMENT", shade(kMon, 1.5f));
                drawText(c, FONT_SMB, sp.x + 36, sp.y + 20, "Revenance Circuit", theme::text);
                blitArt(c, stripFor("creuss"), artFrame(1000 + uint32_t(f) * 300), Rect{sp.x + 8, sp.y + 42, sp.w - 16, 70}, true, 0.42f);
                int yy = sp.y + 120;
                for (const std::string& l : wrapText(FONT_XS, "After a player activates this system: You may produce 1 hit against players' ships in each system "
                                                              "adjacent to this planet that contains a wormhole.", sp.w - 16)) {
                    drawText(c, FONT_XS, sp.x + 8, yy, l, rgb(220, 228, 232));
                    yy += 12;
                }
                yy += 8;
                drawText(c, FONT_SMB, sp.x + 8, yy, "May stand on", theme::gold);
                yy += 16;
                placeIcon(c, kIndustrial, sp.x + 8, yy, 18);
                placeIcon(c, kCultural, sp.x + 30, yy, 18);
                drawText(c, FONT_XS, sp.x + 54, yy + 3, "industrial or cultural", theme::dim);
                yy += 28;
                drawText(c, FONT_SMB, sp.x + 8, yy, "Can't go on", theme::gold);
                yy += 16;
                for (const char* w : {"Home planets, Mecatol Rex: no trait", "Hazardous planets: wrong trait"}) {
                    drawText(c, FONT_XS, sp.x + 8, yy, w, theme::faint);
                    yy += 12;
                }
                yy += 10;
                for (const std::string& l : wrapText(FONT_XS, "One monument per player. Once placed it stays until it is destroyed (Rebel Bombing, an invasion "
                                                              "that takes the planet) - then it may be placed again.", sp.w - 16)) {
                    drawText(c, FONT_XS, sp.x + 8, yy, l, theme::dim);
                    yy += 12;
                }
            }
            drawHintBar(c, ui->hints());
            if (f == 2) savePpm(c, outDir + "/mon_place.ppm"), ++n;
        }
        tileview::setDraftMonuments({{planet, me}});
        setMapDraftMonuments({{planet, me}});
    }

    // ---------------------------------------------------------------- 4. the Monuments screen (game menu): every player's monument
    {
        struct Row {
            int seat;
            const char* name;
            std::vector<Place> places;
            const char* where;  // nullptr = not on the board
            const char* text;
            const char* art;
            float fy;
        };
        const std::string onPl = std::string("On ") + kPlanets[g.planets[size_t(planet)].def].name;
        const Row rows[] = {
            {0, "Revenance Circuit", {kIndustrial, kCultural}, onPl.c_str(),
             "After a player activates this system: You may produce 1 hit against players' ships in each system adjacent to this planet that contains a wormhole.", "creuss", 0.42f},
            {1, "The Cenotaph", {kIndustrial, kCultural}, nullptr,
             "When you place 1 or more infantry on a planet, exhaust this card to place any of those infantry on this planet instead.", "sol", 0.45f},
            {2, "Mowshir Freeport", {kIndustrial, kCultural}, "On Arretze",
             "Outside of combat, players may trade their units in the space area of this and adjacent systems as part of a transaction.", "hacan", 0.4f},
            {3, "Calm Seas Sanatorium", {kSpecialty}, nullptr,
             "After an agenda outcome you voted for or predicted is resolved: Place up to 2 infantry on a planet you control in this system.", "jolnar", 0.6f},
            {4, "Palatine Obelisk", {kHazardous, kIndustrial}, nullptr,
             "Double the PRODUCTION values of your units in this system. When you exhaust this planet to cast votes, you may cast ...", "letnev", 0.5f},
            {5, "Glory Furnace", {kHazardous}, "On Lisis II",
             "When you exhaust this planet to cast votes, for each war sun or flagship you control in this system, and for each ...", "muaat", 0.5f},
        };
        auto screen = [&](Canvas& c, uint32_t now) {
        drawSpaceBackground(c, false);
        c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 150);
        const Rect r{10, 10, 620, 436};
        drawPanel(c, r, theme::panel, theme::gold, 12);
        temple(c, float(r.x + 28), float(r.y + 24), 16.f, theme::gold);
        drawText(c, FONT_LG, r.x + 48, r.y + 10, "Monuments", theme::gold);
        // tabs, right
        const char* tabs[2] = {"Everyone's", "Yours"};
        int tx = r.x + r.w - 12;
        for (int t = 1; t >= 0; --t) {
            const int w = textWidth(FONT_SMB, tabs[t]) + 18;
            tx -= w + 6;
            c.fillRoundRect(tx, r.y + 12, w, 22, 6, t == 0 ? theme::gold : rgb(30, 40, 64));
            drawTextAligned(c, FONT_SMB, tx, r.y + 15, w, Align::Center, tabs[t], t == 0 ? rgb(20, 20, 24) : theme::dim);
        }
        drawText(c, FONT_SM, r.x + 14, r.y + 40, "6 monuments - 3 on the board. Placed after the Construction card's primary ability.", theme::dim);
        const int cw = 296, ch = 116, gap = 8;
        for (int i = 0; i < 6; ++i) {
            const Row& w = rows[i];
            const Rect cr{r.x + 8 + (i % 2) * (cw + gap + 4), r.y + 60 + (i / 2) * (ch + gap), cw, ch};
            const bool sel = i == 0;
            c.fillRoundRect(cr.x, cr.y, cr.w, cr.h, 8, sel ? rgb(30, 40, 68) : rgb(20, 27, 46));
            c.fillRoundRect(cr.x, cr.y, 7, cr.h, 4, pc(g, w.seat));
            c.frameRoundRect(cr.x, cr.y, cr.w, cr.h, 8, sel ? theme::gold : theme::border, sel ? 2 : 1);
            drawFactionIcon(c, g.players[size_t(w.seat)].faction, cr.x + 14, cr.y + 8, 22);
            drawText(c, FONT_SMB, cr.x + 42, cr.y + 7, w.name, theme::text);
            drawText(c, FONT_XS, cr.x + 42, cr.y + 24, std::string(factionShortName(g.players[size_t(w.seat)].faction)) + " monument", pcolours::readable(pc(g, w.seat)));
            int px = cr.x + cr.w - 10;
            for (auto it = w.places.rbegin(); it != w.places.rend(); ++it) {
                px -= 20;
                placeIcon(c, *it, px, cr.y + 8, 18);
            }
            // the animated picture in place of the ability text (owner 18:03; the full text is on the card, A)
            const Rect ar{cr.x + 14, cr.y + 38, cr.w - 50, 58};
            c.fillRect(ar.x - 1, ar.y - 1, ar.w + 2, ar.h + 2, sel ? theme::gold : theme::border);
            blitArt(c, stripFor(w.art), artFrame(now + uint32_t(i) * 1300), ar, true, w.fy);
            // the monument itself, small, in its colour; and where it is
            const bool on = w.where != nullptr;
            icons::drawUnitArtOn(c, "unit/monument", float(cr.x + cr.w - 20), float(cr.y + cr.h - 6), 22, 30, on ? pc(g, w.seat) : rgb(70, 76, 92), false, on ? 255 : 160);
            drawText(c, FONT_XS, cr.x + 14, cr.y + cr.h - 16, on ? w.where : "Not on the board", on ? theme::green : theme::faint);
        }
        drawHintBar(c, {{"D-PAD", "Select"}, {"A", "Read card"}, {"L1/R1", "Switch list"}, {"B", "Back"}});
        };
        Canvas c(640, 480, sc);
        screen(c, 2200);
        savePpm(c, outDir + "/mon_screen.ppm");
        ++n;
        if (std::getenv("TI4_MON_ANIM"))
            for (int f = 0; f < kArtFrames; ++f) {
                Canvas a(640, 480, sc);
                screen(a, uint32_t(f) * 100);
                char nm[64];
                std::snprintf(nm, sizeof nm, "/anim_screen_%02d.ppm", f);
                savePpm(a, outDir + nm);
            }
    }

    // ---------------------------------------------------------------- 5. tokens and marks
    {
        Canvas c(640, 480, sc);
        drawSpaceBackground(c, false);
        c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 120);
        drawText(c, FONT_LG, 20, 12, "Monuments+ - tokens and marks", theme::gold);
        auto label = [&](float cx, float y, const char* a, const char* b) {
            drawTextAligned(c, FONT_SMB, int(cx) - 70, int(y), 140, Align::Center, a, theme::text);
            drawTextAligned(c, FONT_XS, int(cx) - 75, int(y) + 16, 150, Align::Center, b, theme::dim);
        };
        // row 1: the tokens the cards need
        const float y1 = 104.f, R = 40.f;
        float x = 84.f;
        token(c, x, y1, R, theme::gold);
        temple(c, x, y1 - 6.f, 26.f, rgb(245, 240, 225));
        drawTextAligned(c, FONT_SMB, int(x) - 30, int(y1) + 14, 60, Align::Center, "+1 VP", theme::gold);
        label(x, y1 + R + 10.f, "Cathedral of Ixth", "on the elected planet");
        x += 158.f;
        token(c, x, y1, R, rgb(90, 200, 210));
        drawText(c, FONT_LG, int(x) - 27, int(y1) - 12, "+2", rgb(240, 248, 250));
        icons::draw(c, "commodity", int(x) + 3, int(y1) - 12, 22);
        label(x, y1 + R + 10.f, "Minister of Culture", "commodity value +2 (sheet)");
        x += 158.f;
        token(c, x, y1, R, rgb(220, 60, 60));
        drawFactionIcon(c, findFaction("nekro"), int(x) - 22, int(y1) - 26, 44);
        drawTextAligned(c, FONT_XL, int(x) - 20, int(y1) + 2, 40, Align::Center, "M", rgb(255, 120, 110));
        label(x, y1 + R + 10.f, "Assimilator M", "Nekro: a copied monument");
        x += 158.f;
        token(c, x, y1, R, rgb(200, 170, 90));
        {  // a monolith (Monuments to the Ages, Omega): the gold obelisk with its commodities
            float ox[4] = {x - 7.f, x + 7.f, x + 10.f, x - 10.f}, oy[4] = {y1 - 26.f, y1 - 26.f, y1 + 18.f, y1 + 18.f};
            c.fillPolygon(ox, oy, 4, rgb(215, 180, 90));
            float px2[3] = {x - 7.f, x, x + 7.f}, py2[3] = {y1 - 26.f, y1 - 34.f, y1 - 26.f};
            c.fillPolygon(px2, py2, 3, rgb(240, 210, 130));
            c.fillRect(int(x) - 1, int(y1) - 24, 2, 40, rgb(150, 120, 60));
            icons::draw(c, "commodity", int(x) + 12, int(y1) + 2, 16);
            drawText(c, FONT_SMB, int(x) + 14, int(y1) + 16, "x2", rgb(255, 225, 150));
        }
        label(x, y1 + R + 10.f, "Monolith", "Monuments to the Ages \xCE\xA9");
        // row 2: the monument in every colour (the picture the game paints), and the map badge
        drawText(c, FONT_SMB, 20, 210, "The monument structure, painted in the player's colour (as the other units):", theme::gold);
        for (int col = 0; col < 8; ++col) {
            const float mx = 50.f + float(col) * 76.f;
            icons::drawUnitArtOn(c, "unit/monument", mx, 312.f, 52, 78, pcolours::base(col));
        }
        drawText(c, FONT_SMB, 20, 330, "On the map: \"M\" beside D (space dock) and P (PDS) in the owner's badge:", theme::gold);
        {
            const int bx = 30, by = 352;
            const char* labs[3] = {"2PM", "1DM", "M"};
            for (int i = 0; i < 3; ++i) {
                const int tw = textWidth(FONT_SMB, labs[i]);
                c.fillRoundRect(bx + i * 70 - 2, by, tw + 5, FONT_SMB.lineHeight, 4, pcolours::base(i == 0 ? 1 : i == 1 ? 0 : 3), 230);
                drawText(c, FONT_SMB, bx + i * 70, by, labs[i], rgb(10, 10, 10));
            }
        }
        // the placement icons
        drawText(c, FONT_SMB, 20, 384, "Placement icons on the cards (as Monuments+'s reference card):", theme::gold);
        {
            const Place ps[] = {kIndustrial, kCultural, kHazardous, kSpecialty, kLegendary, kMecatol, kSpace, kRelic};
            const char* words[] = {"industrial", "cultural", "hazardous", "tech specialty", "legendary", "Mecatol Rex", "space", "relic"};
            for (int i = 0; i < 8; ++i) {
                const int ix = 14 + i * 68;
                placeIcon(c, ps[i], ix + 22, 404, 26);
                drawTextAligned(c, FONT_XS, ix - 4, 434, 74, Align::Center, words[i], theme::dim);
            }
            placeIcon(c, kHazardous, 580, 404, 26, true);
            drawTextAligned(c, FONT_XS, 560, 434, 66, Align::Center, "not this", theme::dim);
        }
        savePpm(c, outDir + "/mon_tokens.ppm");
        ++n;
    }
    tileview::setDraftMonuments({});
    setMapDraftMonuments({});
    std::printf("monument drafts: %d pictures\n", n);
    return n;
}
