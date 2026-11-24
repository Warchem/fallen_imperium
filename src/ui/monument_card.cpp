#include "ui/monument_card.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/data.h"
#include "gfx/font.h"
#include "gfx/image.h"
#include "gfx/picture.h"
#include "ui/icons.h"
#include "ui/theme.h"

namespace monumentcard {
using namespace ti;

namespace {

uint32_t hex(uint32_t v) { return rgb(int(v >> 16) & 255, int(v >> 8) & 255, int(v) & 255); }

// the pictures, loaded once (monuments/<key>.png next to the game, else assets/monuments/<key>.png)
struct Strip {
    std::string key;
    Image img;
    bool ok = false;
};
std::vector<Strip> g_strips;

bool loadFile(const std::string& path, Image& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> b;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    return decodePicture(b.data(), b.size(), out);
}

const Image* stripFor(const char* key) {
    for (const Strip& s : g_strips)
        if (s.key == key) return s.ok ? &s.img : nullptr;
    Strip s;
    s.key = key;
    s.ok = loadFile(std::string("monuments/") + key + ".png", s.img) || loadFile(std::string("assets/monuments/") + key + ".png", s.img);
    s.ok = s.ok && s.img.w >= kArtW * kArtFrames && s.img.h >= kArtH;
    g_strips.push_back(std::move(s));
    return g_strips.back().ok ? &g_strips.back().img : nullptr;
}

std::string upper(std::string s) {
    for (char& ch : s) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
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

// The placement icons, as on the card: traits, technology specialty, legendary, Mecatol Rex, space, home, the Fracture; crossed = not
void placeIcon(Canvas& c, uint16_t bit, int x, int y, int s, bool crossed) {
    switch (bit) {
        case kMonIndustrial: drawTraitIcon(c, Trait::Industrial, x, y, s); break;
        case kMonCultural: drawTraitIcon(c, Trait::Cultural, x, y, s); break;
        case kMonHazardous: drawTraitIcon(c, Trait::Hazardous, x, y, s); break;
        case kMonTechSpecialty: {
            const int h = s / 2;
            drawSkipIcon(c, TechColor::Blue, x, y, h);
            drawSkipIcon(c, TechColor::Green, x + h, y, h);
            drawSkipIcon(c, TechColor::Red, x, y + h, h);
            drawSkipIcon(c, TechColor::Yellow, x + h, y + h, h);
            break;
        }
        case kMonLegendary:
            c.fillCircle(float(x + s / 2), float(y + s / 2), float(s) * 0.5f, rgb(10, 12, 20));
            c.ring(float(x + s / 2), float(y + s / 2), float(s) * 0.5f, 1.5f, theme::gold);
            icons::draw(c, "legendary_white", x + s / 6, y + s / 6, s * 2 / 3, 255, theme::gold);
            break;
        case kMonMecatol:
            c.fillCircle(float(x + s / 2), float(y + s / 2), float(s) * 0.46f, rgb(70, 64, 56));
            c.ring(float(x + s / 2), float(y + s / 2), float(s) * 0.46f, 1.5f, rgb(255, 215, 120));
            drawTextAligned(c, FONT_XS, x, y + s / 2 - 6, s, Align::Center, "MR", rgb(255, 225, 150));
            break;
        case kMonHome:
            c.fillCircle(float(x + s / 2), float(y + s / 2), float(s) * 0.46f, rgb(30, 40, 60));
            c.ring(float(x + s / 2), float(y + s / 2), float(s) * 0.46f, 1.5f, rgb(200, 220, 255));
            drawTextAligned(c, FONT_XS, x, y + s / 2 - 6, s, Align::Center, "H", rgb(220, 230, 255));
            break;
        case kMonFracture: {  // a lightning bolt (the Fracture's mark)
            const float cx = float(x + s / 2), cy = float(y + s / 2), r = float(s) * 0.5f;
            c.fillCircle(cx, cy, r, rgb(40, 10, 20));
            float bx[6] = {cx + r * 0.15f, cx - r * 0.35f, cx - r * 0.02f, cx - r * 0.2f, cx + r * 0.35f, cx + r * 0.05f};
            float by[6] = {cy - r * 0.75f, cy + r * 0.1f, cy + r * 0.1f, cy + r * 0.75f, cy - r * 0.15f, cy - r * 0.15f};
            c.fillPolygon(bx, by, 6, rgb(255, 90, 120));
            break;
        }
        case kMonSupernova:
            c.fillCircle(float(x + s / 2), float(y + s / 2), float(s) * 0.42f, rgb(255, 140, 40));
            c.ring(float(x + s / 2), float(y + s / 2), float(s) * 0.48f, 2.f, rgb(255, 220, 120));
            break;
        case kMonStation: {  // (0.82.1) a space station: a hex station ringed by its orbit, as the station cards' mark
            const float cx = float(x + s / 2), cy = float(y + s / 2), r = float(s) * 0.5f;
            c.fillCircle(cx, cy, r, rgb(26, 30, 38));
            c.ring(cx, cy, r * 0.84f, 1.4f, rgb(150, 200, 230));
            float hx[6], hy[6];
            for (int k = 0; k < 6; ++k) hx[k] = cx + r * 0.42f * std::cos(float(k) * 1.0472f + 0.5236f), hy[k] = cy + r * 0.42f * std::sin(float(k) * 1.0472f + 0.5236f);
            c.fillPolygon(hx, hy, 6, rgb(230, 234, 240));
            c.fillCircle(cx, cy, r * 0.17f, rgb(26, 30, 38));
            c.fillCircle(cx + r * 0.6f, cy - r * 0.6f, r * 0.13f, rgb(150, 200, 230));
            break;
        }
        case kMonEmpty: {
            const float cx = float(x + s / 2), cy = float(y + s / 2), r = float(s) * 0.5f;
            float hx[6], hy[6];
            for (int k = 0; k < 6; ++k) hx[k] = cx + r * std::cos(float(k) * 1.0472f), hy[k] = cy + r * 0.9f * std::sin(float(k) * 1.0472f);
            c.fillPolygon(hx, hy, 6, rgb(14, 20, 40));
            for (int k = 0; k < 6; ++k) c.line(hx[k], hy[k], hx[(k + 1) % 6], hy[(k + 1) % 6], rgb(150, 190, 255), 1.3f);
            c.fillCircle(cx, cy, 1.6f, rgb(255, 255, 255));
            break;
        }
        default: break;
    }
    if (crossed) {
        c.line(float(x), float(y), float(x + s), float(y + s), rgb(230, 60, 50), 2.5f);
        c.line(float(x + s), float(y), float(x), float(y + s), rgb(230, 60, 50), 2.5f);
    }
}

}  // namespace

int placeIcons(Canvas& c, int monument, int right, int y, int s, int gap) {
    if (monument < 0 || monument >= ti::kMonumentCount) return right;
    const ti::MonumentDef& m = ti::kMonuments[monument];
    std::vector<std::pair<uint16_t, bool>> icons;
    for (uint16_t b = 1; b && b <= ti::kMonSupernova; b = uint16_t(b << 1))
        if (m.notPlaces & b) icons.push_back({b, true});
    static const uint16_t kOrder[] = {ti::kMonIndustrial, ti::kMonCultural, ti::kMonHazardous, ti::kMonTechSpecialty, ti::kMonLegendary, ti::kMonMecatol,
                                      ti::kMonHome,       ti::kMonEmpty,    ti::kMonStation,   ti::kMonFracture,      ti::kMonSupernova};
    for (uint16_t b : kOrder)
        if (m.places & b) icons.push_back({b, false});
    int x = right - int(icons.size()) * s - std::max(0, int(icons.size()) - 1) * gap;
    const int left = x;
    for (const auto& ic : icons) {
        placeIcon(c, ic.first, x, y, s, ic.second);
        x += s + gap;
    }
    return left;
}

void temple(Canvas& c, float cx, float cy, float s, uint32_t col, int a) {
    float tx[3] = {cx - s * 0.62f, cx, cx + s * 0.62f}, ty[3] = {cy - s * 0.18f, cy - s * 0.58f, cy - s * 0.18f};
    c.fillPolygon(tx, ty, 3, col, a);
    c.fillCircle(cx, cy - s * 0.31f, s * 0.07f, rgb(10, 12, 20), a);
    c.fillRect(int(cx - s * 0.6f), int(cy - s * 0.14f), int(s * 1.2f), std::max(1, int(s * 0.08f)), col, a);
    for (int k = -1; k <= 1; ++k) c.fillRect(int(cx + float(k) * s * 0.36f - s * 0.09f), int(cy - s * 0.02f), std::max(1, int(s * 0.18f)), int(s * 0.42f), col, a);
    c.fillRect(int(cx - s * 0.64f), int(cy + s * 0.42f), int(s * 1.28f), std::max(1, int(s * 0.09f)), col, a);
    c.fillRect(int(cx - s * 0.72f), int(cy + s * 0.54f), int(s * 1.44f), std::max(1, int(s * 0.09f)), col, a);
}

void art(Canvas& c, int monument, Rect box, uint32_t now, bool cover, float focusY) {
    const Rect p = c.toPx(box);
    if (p.w <= 0 || p.h <= 0) return;
    const Image* strip = monument >= 0 && monument < kMonumentCount ? stripFor(kMonuments[monument].key) : nullptr;
    const int frame = int((now / 100u) % uint32_t(kArtFrames));
    Image out;
    out.w = p.w;
    out.h = p.h;
    out.px.assign(size_t(p.w) * size_t(p.h), 0xFF0A0C14u);
    if (strip) {
        float k = cover ? std::max(float(p.w) / kArtW, float(p.h) / kArtH) : std::min(float(p.w) / kArtW, float(p.h) / kArtH);
        if (!cover && k >= 2.f) k = std::floor(k);
        const int dw = int(float(kArtW) * k + 0.5f), dh = int(float(kArtH) * k + 0.5f);
        const int ox = (p.w - dw) / 2, oy = cover ? -int(float(dh - p.h) * focusY) : (p.h - dh) / 2;
        for (int y = 0; y < p.h; ++y) {
            if (y < oy) continue;
            const int sy = int(float(y - oy) / k);
            if (sy >= kArtH) continue;
            const uint32_t* row = strip->px.data() + size_t(sy) * size_t(strip->w) + size_t(frame * kArtW);
            uint32_t* dst = out.px.data() + size_t(y) * size_t(p.w);
            for (int x = std::max(0, ox); x < p.w; ++x) {
                const int sx = int(float(x - ox) / k);
                if (sx >= kArtW) break;
                dst[x] = row[sx] | 0xFF000000u;
            }
        }
    }
    c.pxBlit(out, p.x, p.y, 255);
}

void draw(Canvas& c, int monument, Rect r, uint32_t now) {
    if (monument < 0 || monument >= kMonumentCount) return;
    const MonumentDef& m = kMonuments[monument];
    const uint32_t frame = hex(m.frame), sub = hex(m.sub);
    const int unit = unitByKey(m.unit);
    const std::string name = unit >= 0 ? kUnits[unit].name : m.subtitle;
    // the card, its glow and a scatter of stars in the border
    c.frameRoundRect(r.x - 4, r.y - 4, r.w + 8, r.h + 8, 18, frame, 4, 70);
    c.fillRoundRect(r.x, r.y, r.w, r.h, 14, rgb(8, 10, 18));
    for (int k = 0; k < 90; ++k) {
        const int sx = r.x + 3 + int((uint32_t(k) * 2654435761u >> 7) % uint32_t(std::max(1, r.w - 6)));
        const int sy = r.y + 3 + int((uint32_t(k) * 40503u * 977u >> 5) % uint32_t(std::max(1, r.h - 6)));
        const bool edge = sx < r.x + 9 || sx > r.x + r.w - 10 || sy < r.y + 9 || sy > r.y + r.h - 10;
        if (edge) c.fillRect(sx, sy, 1, 1, rgb(210, 220, 255), 120 + (k * 37) % 120);
    }
    c.frameRoundRect(r.x + 7, r.y + 7, r.w - 14, r.h - 14, 11, frame, 2, 235);
    c.frameRoundRect(r.x + 9, r.y + 9, r.w - 18, r.h - 18, 10, shade(frame, 0.5f), 1, 160);
    // header: emblem, name, "+ temple"
    const Rect h{r.x + 12, r.y + 12, r.w - 24, 60};
    c.fillRoundRect(h.x, h.y, h.w, h.h, 9, shade(frame, 0.2f));
    c.fillRect(h.x + h.w / 2, h.y + 2, h.w / 2 - 8, h.h - 4, shade(frame, 0.32f), 120);
    c.fillRect(h.x + 6, h.y + h.h - 3, h.w - 12, 2, frame, 200);
    const int fi = findFaction(m.faction);
    const float ecx = float(h.x + 34), ecy = float(h.y + 30);
    c.fillCircle(ecx, ecy, 25.f, rgb(10, 12, 20));
    c.ring(ecx, ecy, 25.f, 2.f, shade(frame, 1.3f));
    if (fi >= 0) drawFactionIcon(c, fi, int(ecx) - 19, int(ecy) - 19, 38);
    const std::string title = upper(name);
    const FontData& tf = textWidth(FONT_XL, title) <= h.w - 120 ? FONT_XL : textWidth(FONT_LG, title) <= h.w - 110 ? FONT_LG : FONT_MDB;
    drawText(c, tf, h.x + 70, h.y + (&tf == &FONT_XL ? 8 : &tf == &FONT_LG ? 14 : 18), title, rgb(246, 248, 252));
    drawText(c, FONT_SMB, h.x + h.w - 40, h.y + 6, "+", rgb(240, 244, 250));
    temple(c, float(h.x + h.w - 22), float(h.y + 15), 12.f, rgb(240, 244, 250));
    {  // the subtitle in a slanted tab hanging under the header
        const std::string st = upper(m.subtitle);
        const int tw = textWidth(FONT_SMB, st) + 34;
        const float tx = float(h.x + 62), ty = float(h.y + h.h - 4);
        float xs[4] = {tx, tx + float(tw), tx + float(tw) - 12.f, tx}, ys[4] = {ty, ty, ty + 20.f, ty + 20.f};
        c.fillPolygon(xs, ys, 4, shade(frame, 0.16f));
        c.line(tx, ty + 20.f, tx + float(tw) - 12.f, ty + 20.f, shade(frame, 0.8f), 1.f);
        drawText(c, FONT_SMB, int(tx) + 10, int(ty) + 3, st, sub);
    }
    const int by = h.y + h.h + 22;
    // the picture, the placement in words, the placement icons at the bottom of the column
    const Rect a{r.x + 16, by, 214, 160};
    c.fillRect(a.x - 1, a.y - 1, a.w + 2, a.h + 2, shade(frame, 0.9f));
    art(c, monument, a, now, false);
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
        c.line(xs[0], ys[0], xs[1], ys[1], shade(frame, 0.9f), 1.f);
        std::vector<std::pair<uint16_t, bool>> icons;  // crossed-out kinds first, as the cards show them
        for (uint16_t b = 1; b && b <= kMonSupernova; b = uint16_t(b << 1))
            if (m.notPlaces & b) icons.push_back({b, true});
        static const uint16_t kOrder[] = {kMonIndustrial, kMonCultural, kMonHazardous, kMonTechSpecialty, kMonLegendary, kMonMecatol, kMonHome,
                                          kMonEmpty, kMonStation, kMonFracture, kMonSupernova};
        for (uint16_t b : kOrder)
            if (m.places & b) icons.push_back({b, false});
        const int is = 28, gap = 8;
        const int total = int(icons.size()) * is + std::max(0, int(icons.size()) - 1) * gap;
        int x = pp.x + (pp.w - 16 - total) / 2;
        for (const auto& ic : icons) {
            placeIcon(c, ic.first, x, pp.y + 7, is, ic.second);
            x += is + gap;
        }
    }
    // the text panel: ability, stat lines, flavour
    const Rect tp{r.x + 242, by, r.w - 258, r.y + r.h - 28 - by};
    c.fillRoundRect(tp.x, tp.y, tp.w, tp.h, 8, rgb(12, 14, 22), 245);
    c.frameRoundRect(tp.x, tp.y, tp.w, tp.h, 8, shade(frame, 0.45f), 1, 200);
    for (int k = 0; k < 4; ++k) {  // faint circuit traces (as the card's panel)
        const float y0 = float(tp.y + tp.h * (k + 1) / 5), x0 = float(tp.x + 10 + k * 23);
        c.line(x0, y0, x0 + 70.f, y0, shade(frame, 0.35f), 1.f, 60);
        c.line(x0 + 70.f, y0, x0 + 86.f, y0 - 12.f, shade(frame, 0.35f), 1.f, 60);
        c.line(x0 + 86.f, y0 - 12.f, float(tp.x + tp.w - 12), y0 - 12.f, shade(frame, 0.35f), 1.f, 60);
    }
    std::vector<std::string> paras, stats;
    {
        std::string cur;
        for (const char* q = m.text;; ++q) {
            if (*q == '\n' || !*q) {
                paras.push_back(cur);
                cur.clear();
                if (!*q) break;
            } else cur += *q;
        }
        for (const char* q = m.stats; *m.stats; ++q) {
            if (*q == '\n' || !*q) {
                stats.push_back(cur);
                cur.clear();
                if (!*q) break;
            } else cur += *q;
        }
    }
    const int tw = tp.w - 26;
    const std::vector<std::string> flav = m.flavour[0] ? wrapText(FONT_SM, m.flavour, tw - 10) : std::vector<std::string>();
    const int room = tp.h - 24 - int(stats.size()) * 18 - (flav.empty() ? 0 : int(flav.size()) * 14 + 22);
    const FontData* af = &FONT_MDB;
    for (const FontData* f : {&FONT_MDB, &FONT_MD, &FONT_SM}) {
        af = f;
        int hgt = 0;
        for (const std::string& pr : paras) hgt += int(wrapText(*f, pr, tw).size()) * (f->lineHeight + 2) + 6;
        if (hgt <= room) break;
    }
    int y = tp.y + 12;
    for (const std::string& pr : paras) {
        const auto lines = wrapText(*af, pr, tw);
        for (size_t l = 0; l < lines.size(); ++l) {
            drawText(c, *af, tp.x + 13, y, lines[l], rgb(242, 244, 248));
            if (l == 0) {  // ACTION: / DEPLOY: in gold
                const size_t colon = lines[l].find(':');
                if (colon != std::string::npos && colon < 10 && upper(lines[l].substr(0, colon)) == lines[l].substr(0, colon))
                    drawText(c, *af, tp.x + 13, y, lines[l].substr(0, colon + 1), theme::gold);
            }
            y += af->lineHeight + 2;
        }
        y += 6;
    }
    for (const std::string& st : stats) {
        statStar(c, float(tp.x + 20), float(y + 7), 6.f, rgb(240, 244, 250));
        drawText(c, FONT_SMB, tp.x + 31, y, st, rgb(240, 244, 250));
        y += 18;
    }
    if (!flav.empty()) {
        int fy = tp.y + tp.h - 10 - int(flav.size()) * 14;
        c.fillRect(tp.x + tp.w / 6, fy - 9, tp.w * 2 / 3, 1, rgb(220, 226, 236), 200);
        for (const std::string& l : flav) {
            drawTextAligned(c, FONT_SM, tp.x, fy, tp.w, Align::Center, l, rgb(196, 204, 218));
            fy += 14;
        }
    }
}

}  // namespace monumentcard
