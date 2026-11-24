#include "ui/cards.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "engine/data.h"
#include "gfx/font.h"
#include "core/log.h"
#include "gfx/image.h"
#include "gfx/picture.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

using namespace ti;

namespace cards {
namespace {
// ------------------------------------------------------------------ baking
// A design is drawn twice, on black and on white; the difference gives each
// pixel's coverage, so the result can be laid over anything (and faded).
std::map<std::string, Image>& cache() {
    static std::map<std::string, Image> m;
    return m;
}
size_t& cacheBytes() {
    static size_t n = 0;
    return n;
}

// (0.27.0) made at the target canvas's scale: sharp on a big screen
const Image* bake(const Canvas& target, const std::string& key0, int w, int h, const std::function<void(Canvas&)>& draw) {
    auto& m = cache();
    const std::string key = target.unscaled() ? key0 : key0 + "@" + std::to_string(int(target.scale() * 1000.f));
    auto it = m.find(key);
    if (it != m.end()) return &it->second;
    if (cacheBytes() > (target.unscaled() ? 12u : 48u) * 1024 * 1024) {  // a guard: screens use a handful of sizes
        m.clear();
        cacheBytes() = 0;
    }
    w = std::max(1, w);
    h = std::max(1, h);
    Canvas blk(w, h, target.scale()), wht(w, h, target.scale());
    blk.clear(0);
    wht.clear(rgb(255, 255, 255));
    draw(blk);
    draw(wht);
    Image img;
    img.w = blk.pw();
    img.h = blk.ph();
    img.px.resize(size_t(img.w) * size_t(img.h));
    const uint32_t* pb = blk.pixels();
    const uint32_t* pw = wht.pixels();
    for (size_t i = 0; i < img.px.size(); ++i) {
        const uint32_t b = pb[i], q = pw[i];
        int d = (int((q >> 16) & 255) - int((b >> 16) & 255) + int((q >> 8) & 255) - int((b >> 8) & 255) + int(q & 255) - int(b & 255)) / 3;
        int a = std::max(0, std::min(255, 255 - d));
        uint32_t r = std::min<uint32_t>((b >> 16) & 255, a), g = std::min<uint32_t>((b >> 8) & 255, a), bl = std::min<uint32_t>(b & 255, a);
        img.px[i] = (uint32_t(a) << 24) | (r << 16) | (g << 8) | bl;
    }
    cacheBytes() += img.px.size() * 4;
    return &(m[key] = std::move(img));
}

// ------------------------------------------------------------------ drawing helpers
uint32_t lighter(uint32_t c, float k) { return mix(c, rgb(255, 255, 255), int(k * 255)); }

// A glowing line: a wide faint halo, a medium glow and a bright core.
void glow(Canvas& c, float x0, float y0, float x1, float y1, uint32_t col, float t, int a = 255) {
    c.line(x0, y0, x1, y1, col, t * 4.f, a * 40 / 255);
    c.line(x0, y0, x1, y1, col, t * 2.f, a * 100 / 255);
    c.line(x0, y0, x1, y1, lighter(col, 0.35f), t, a);
}
void glowPath(Canvas& c, const std::vector<float>& p, uint32_t col, float t, int a = 255, bool closed = false) {
    const size_t n = p.size() / 2;
    for (size_t i = 0; i + 1 < n; ++i) glow(c, p[2 * i], p[2 * i + 1], p[2 * i + 2], p[2 * i + 3], col, t, a);
    if (closed && n > 2) glow(c, p[2 * n - 2], p[2 * n - 1], p[0], p[1], col, t, a);
}
void glowRing(Canvas& c, float cx, float cy, float r, uint32_t col, float t, int a = 255) {
    c.ring(cx, cy, r, t * 4.f, col, a * 40 / 255);
    c.ring(cx, cy, r, t * 2.f, col, a * 100 / 255);
    c.ring(cx, cy, r, t, lighter(col, 0.35f), a);
}

// Deterministic star dots.
void stars(Canvas& c, Rect r, int count, uint32_t seed) {
    uint32_t s = seed * 2654435761u + 12345u;
    auto next = [&]() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    };
    for (int i = 0; i < count; ++i) {
        int x = r.x + int(next() % uint32_t(std::max(1, r.w)));
        int y = r.y + int(next() % uint32_t(std::max(1, r.h)));
        int a = 50 + int(next() % 150);
        c.pixel(x, y, rgb(225, 230, 255), a);
        if (next() % 9 == 0) {  // a few brighter ones with a soft cross
            c.pixel(x + 1, y, rgb(225, 230, 255), a / 3);
            c.pixel(x - 1, y, rgb(225, 230, 255), a / 3);
            c.pixel(x, y + 1, rgb(225, 230, 255), a / 3);
            c.pixel(x, y - 1, rgb(225, 230, 255), a / 3);
        }
    }
}

// A dark card face with a faint glow of colour from the top and some stars.
void cardFace(Canvas& c, int w, int h, uint32_t col, uint32_t seed) {
    const int rad = std::max(3, w / 16);
    c.fillRoundRect(0, 0, w, h, rad, rgb(8, 9, 15));
    for (int y = 1; y < h - 1; ++y) {  // colour haze, strongest at the top
        int a = std::max(0, 34 - y * 60 / std::max(1, h));
        if (a > 0) c.fillRect(rad / 2, y, w - rad, 1, col, a);
    }
    stars(c, Rect{2, 2, w - 4, h - 4}, w * h / 260, seed);
    c.frameRoundRect(0, 0, w, h, rad, shade(col, 0.55f), 1);
}

// Octagon outline points around (cx, cy).
std::vector<float> octagon(float cx, float cy, float rx, float ry) {
    std::vector<float> p;
    for (int k = 0; k < 8; ++k) {
        float ang = (k * 45.f + 22.5f) * 3.14159265f / 180.f;
        p.push_back(cx + rx * std::cos(ang));
        p.push_back(cy + ry * std::sin(ang));
    }
    return p;
}
void fillPoly(Canvas& c, const std::vector<float>& p, uint32_t col, int a) {
    std::vector<float> xs, ys;
    for (size_t i = 0; i + 1 < p.size(); i += 2) {
        xs.push_back(p[i]);
        ys.push_back(p[i + 1]);
    }
    c.fillPolygon(xs.data(), ys.data(), int(xs.size()), col, a);
}

uint32_t hashKey(const std::string& s) {
    uint32_t h = 2166136261u;
    for (char ch : s) h = (h ^ uint8_t(ch)) * 16777619u;
    return h;
}

uint32_t objColour(int kind) { return kind == 1 ? rgb(245, 150, 45) : kind == 2 ? rgb(60, 145, 255) : rgb(235, 55, 50); }

uint32_t techColour(TechColor t) {
    switch (t) {
        case TechColor::Blue: return rgb(55, 150, 255);
        case TechColor::Green: return rgb(70, 205, 90);
        case TechColor::Red: return rgb(235, 70, 60);
        case TechColor::Yellow: return rgb(240, 200, 60);
        default: return rgb(175, 180, 200);
    }
}

uint32_t strategyColour(int card) {
    static const uint32_t k[8] = {rgb(235, 50, 45),  rgb(245, 140, 40), rgb(240, 205, 55), rgb(60, 195, 80),
                                  rgb(40, 205, 215), rgb(60, 135, 240), rgb(85, 85, 240),  rgb(160, 75, 230)};
    return k[card & 7];
}

// Text that fits a width, or nothing.
void textIfFits(Canvas& c, const FontData& f, int x, int y, int w, const std::string& s, uint32_t col, int a) {
    if (textWidth(f, s) <= w) drawTextAligned(c, f, x, y, w, Align::Center, s, col, a);
}

// The biggest font a short label (a number) fits in.
const FontData& numberFont(int box) { return box >= 40 ? FONT_XL : box >= 26 ? FONT_LG : box >= 18 ? FONT_MDB : FONT_SMB; }
}  // namespace

// ------------------------------------------------------------------ objectives
void objective(Canvas& c, int o, Rect r, int alpha) {
    if (o < 0 || o >= kObjectiveCount || r.w < 8 || r.h < 8) return;
    const ObjectiveDef& d = kObjectives[o];
    const uint32_t col = objColour(d.kind);
    const int w = r.w, h = r.h;
    const float t = std::max(0.8f, w / 90.f);
    const std::string key = "obj" + std::to_string(d.kind) + "_" + std::to_string(w) + "x" + std::to_string(h);
    const Image* face = bake(c, key, w, h, [&](Canvas& k) {
        cardFace(k, w, h, col, hashKey(key));
        // header tab
        glowPath(k, {w * .05f, h * .19f, w * .17f, h * .10f, w * .83f, h * .10f, w * .95f, h * .19f}, col, t);
        glow(k, w * .05f, h * .19f, w * .95f, h * .19f, col, t);
        // sides down to the points circle
        glow(k, w * .05f, h * .19f, w * .05f, h * .86f, col, t);
        glow(k, w * .95f, h * .19f, w * .95f, h * .86f, col, t);
        // the text window
        k.fillRoundRect(int(w * .10f), int(h * .23f), int(w * .80f), int(h * .43f), std::max(2, w / 30), col, 16);
        k.frameRoundRect(int(w * .10f), int(h * .23f), int(w * .80f), int(h * .43f), std::max(2, w / 30), shade(col, 0.8f), 1, 110);
        // the points circle with braces to each side
        const float cx = w * .5f, cy = h * .785f, cr = w * .115f;
        k.fillCircle(cx, cy, cr, col, 45);
        glowRing(k, cx, cy, cr, col, t);
        glow(k, w * .05f, h * .70f, cx - cr * 1.05f, h * .70f, col, t * 0.8f, 200);
        glow(k, cx + cr * 1.05f, h * .70f, w * .95f, h * .70f, col, t * 0.8f, 200);
        // the footer tab
        glowPath(k, {w * .05f, h * .86f, w * .16f, h * .95f, w * .84f, h * .95f, w * .95f, h * .86f}, col, t);
        glow(k, cx - cr * 1.25f, h * .88f, w * .16f, h * .88f, col, t * 0.6f, 150);
        glow(k, cx + cr * 1.25f, h * .88f, w * .84f, h * .88f, col, t * 0.6f, 150);
    });
    c.blit(*face, r.x, r.y, alpha);
    // header, points and footer text
    const bool action = d.phase == ObjPhase::Action;
    const FontData& hf = w >= 150 ? FONT_SMB : FONT_XS;
    textIfFits(c, hf, r.x + int(w * .17f), r.y + int(h * .145f) - hf.lineHeight / 2, int(w * .66f), action ? "ACTION PHASE" : "STATUS PHASE",
               action ? lighter(col, 0.25f) : theme::text, alpha);
    const FontData& nf = numberFont(int(w * .23f));
    drawTextAligned(c, nf, r.x, r.y + int(h * .785f) - nf.lineHeight / 2, w, Align::Center, std::to_string(d.points), theme::text, alpha);
    textIfFits(c, hf, r.x + int(w * .16f), r.y + int(h * .905f) - hf.lineHeight / 2 + 1, int(w * .68f),
               d.points == 1 ? "VICTORY POINT" : "VICTORY POINTS", lighter(col, 0.45f), alpha);

    const bool small = w < 90, big = w >= 170;
    Rect box = small ? Rect{r.x + w * 8 / 100, r.y + h * 22 / 100, w * 84 / 100, h * 46 / 100}
                     : Rect{r.x + w * 12 / 100, r.y + h * 25 / 100, w * 76 / 100, h * 40 / 100};
    const FontData& tf = small ? FONT_XS : big ? FONT_MDB : FONT_SMB;
    auto nameLines = wrapText(tf, d.name, box.w);
    if (nameLines.size() > 3) nameLines.resize(3);
    int nameH = int(nameLines.size()) * tf.lineHeight;
    std::vector<std::string> textLines;
    if (!small) {
        textLines = wrapText(FONT_XS, objectiveSummary(o), box.w);
        size_t room = size_t(std::max(0, (box.h - nameH - 5) / FONT_XS.lineHeight));
        if (textLines.size() > room) {
            textLines.resize(room);
            if (room) textLines.back() = fitText(FONT_XS, textLines.back() + " ...", box.w);
        }
    }
    int textH = int(textLines.size()) * FONT_XS.lineHeight;
    int y = box.y + std::max(0, (box.h - nameH - (textH ? textH + 5 : 0)) / 2);
    for (const std::string& l : nameLines) {
        drawTextAligned(c, tf, box.x, y, box.w, Align::Center, l, theme::text, alpha);
        y += tf.lineHeight;
    }
    y += 5;
    for (const std::string& l : textLines) {
        drawTextAligned(c, FONT_XS, box.x, y, box.w, Align::Center, l, rgb(200, 205, 222), alpha);
        y += FONT_XS.lineHeight;
    }
}

void objectiveBack(Canvas& c, int kind, Rect r, int alpha) {
    if (r.w < 8 || r.h < 8) return;
    const uint32_t col = objColour(kind);
    const int w = r.w, h = r.h;
    const float t = std::max(0.8f, w / 90.f);
    const std::string key = "objback" + std::to_string(kind) + "_" + std::to_string(w) + "x" + std::to_string(h);
    const Image* back = bake(c, key, w, h, [&](Canvas& k) {
        cardFace(k, w, h, col, hashKey(key));
        // corner brackets
        const float m = w * .07f, L = w * .18f;
        for (int sx = 0; sx < 2; ++sx)
            for (int sy = 0; sy < 2; ++sy) {
                float x = sx ? w - m : m, y = sy ? h - m : m, dx = sx ? -L : L, dy = sy ? -L : L;
                glowPath(k, {x + dx, y, x, y, x, y + dy}, col, t);
            }
        // the emblem: a glowing octagon with the deck's icon
        const float cx = w * .5f, cy = h * .47f, rr = w * .30f;
        fillPoly(k, octagon(cx, cy, rr, rr), col, 30);
        glowPath(k, octagon(cx, cy, rr, rr), col, t * 1.2f, 255, true);
        glowPath(k, octagon(cx, cy, rr * .82f, rr * .82f), col, t * 0.6f, 120, true);
        const int s = int(rr * 1.15f);
        if (!icons::draw(k, kind == 3 ? "secret" : kind == 2 ? "public2" : "public1", int(cx) - s / 2, int(cy) - s / 2, s, 255,
                         lighter(col, 0.4f))) {
            const FontData& f = numberFont(int(rr));
            drawTextAligned(k, f, 0, int(cy) - f.lineHeight / 2, w, Align::Center, kind == 3 ? "?" : kind == 2 ? "II" : "I", lighter(col, 0.5f));
        }
    });
    c.blit(*back, r.x, r.y, alpha);
    if (w >= 100) {
        const FontData& f = w >= 150 ? FONT_SMB : FONT_XS;
        textIfFits(c, f, r.x + int(w * .2f), r.y + int(h * .80f), int(w * .6f), kind == 3 ? "SECRET" : kind == 2 ? "STAGE II" : "STAGE I",
                   lighter(col, 0.45f), alpha);
    }
}

// ------------------------------------------------------------------ technology
namespace {
// Circuit traces: horizontal runs with 45-degree turns and little nodes.
void circuits(Canvas& k, int w, int h, uint32_t seed) {
    uint32_t s = seed | 1;
    auto rnd = [&](int n) {
        s = s * 1664525u + 1013904223u;
        return int((s >> 8) % uint32_t(std::max(1, n)));
    };
    const uint32_t tc = rgb(80, 100, 125);
    const int n = 6 + w / 60;
    for (int i = 0; i < n; ++i) {
        float x = float(rnd(w)), y = float(h * 0.25f + rnd(int(h * 0.7f)));
        const int segs = 2 + rnd(3);
        for (int j = 0; j < segs; ++j) {
            float len = float(w / 12 + rnd(w / 5));
            int dir = rnd(4);  // 0 right, 1 left, 2 down-right, 3 up-right
            float nx = x + (dir == 1 ? -len : len), ny = y + (dir == 2 ? len * .6f : dir == 3 ? -len * .6f : 0.f);
            ny = std::max(h * 0.2f, std::min(h * 0.97f, ny));
            k.line(x, y, nx, ny, tc, 1.f, 60);
            x = nx;
            y = ny;
        }
        k.ring(x, y, 2.2f, 1.f, tc, 90);
    }
}
}  // namespace

// ------------------------------------------------------------------ wireframes
namespace {
struct WirePoly {
    int first, n;  // into kWirePts (point pairs)
};
struct WireArt {
    const char* key;
    float aspect;  // height / width
    int first, n;  // into kWirePolys
};
#include "ui/gen/wireframes.inc"

const WireArt* findWire(const std::string& key) {
    for (const WireArt& a : kWireArts)
        if (key == a.key) return &a;
    return nullptr;
}

const char* const kUnitWire[kUnitTypes] = {"carrier", "cruiser",  "destroyer", "dreadnought", "fighter",  "flagship",
                                           "warsun",  "infantry", "mech",      "pds",         "spacedock", "flagship"};

// The art fitted (centred, keeping its shape) into r: thin lines in col.
void drawWire(Canvas& c, const WireArt& a, Rect r, uint32_t col, int alpha) {
    const float s = std::min(float(r.w), r.h / a.aspect);
    if (s < 12.f || alpha <= 0) return;
    const int w = int(s) + 4, h = int(s * a.aspect) + 4;
    const float t = std::max(0.75f, s / 380.f), k = s / 4000.f;
    const std::string key = std::string("wire_") + a.key + "_" + std::to_string(w) + "x" + std::to_string(h) + "_" + std::to_string(col);
    const Image* img = bake(c, key, w, h, [&](Canvas& q) {
        for (int i = a.first; i < a.first + a.n; ++i) {
            const WirePoly& p = kWirePolys[i];
            const int16_t* v = kWirePts + 2 * p.first;
            for (int j = 0; j + 1 < p.n; ++j)
                q.line(2.f + v[2 * j] * k, 2.f + v[2 * j + 1] * k, 2.f + v[2 * j + 2] * k, 2.f + v[2 * j + 3] * k, col, t);
        }
    });
    c.blit(*img, r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, alpha);
}

// Unit upgrades show their unit; other technologies have their own drawing.
const WireArt* techWire(int t) {
    if (t < 0 || t >= kTechCount) return nullptr;
    if (kTechs[t].unitUpgrade) {
        for (int u = 0; u < kUnitCount; ++u)
            if (kUnits[u].tech == t) return findWire(kUnitWire[int(kUnits[u].type)]);
        return nullptr;
    }
    return findWire(std::string("t:") + kTechs[t].key);
}
}  // namespace

void unitWireframe(Canvas& c, UnitType type, Rect r, uint32_t col, int alpha) {
    if (int(type) >= kUnitTypes || !kUnitWire[int(type)]) return;  // (0.82.0: no wireframe for monuments)
    if (const WireArt* a = findWire(kUnitWire[int(type)])) drawWire(c, *a, r, col, alpha);
}

void techWireframe(Canvas& c, int tech, Rect r, uint32_t col, int alpha) {
    if (const WireArt* a = techWire(tech)) drawWire(c, *a, r, col, alpha);
}

void techTile(Canvas& c, int t, Rect r, int alpha) {
    if (t < 0 || t >= kTechCount || r.w < 8 || r.h < 8) return;
    const TechDef& d = kTechs[t];
    const uint32_t col = techColour(d.unitUpgrade ? TechColor::None : d.color);
    const int w = r.w, h = r.h;
    const bool coloured = !d.unitUpgrade && d.color != TechColor::None;
    const std::string key = "techtile" + std::to_string(int(d.color)) + (d.unitUpgrade ? "u" : "") + "_" + std::to_string(w) + "x" + std::to_string(h);
    const Image* face = bake(c, key, w, h, [&](Canvas& k) {
        cardFace(k, w, h, col, hashKey(key));
        circuits(k, w, h, hashKey(key) ^ 77u);
        // the name bar
        glowPath(k, {w * .02f, h * .40f, w * .70f, h * .40f, w * .76f, h * .10f, w * .97f, h * .10f}, col, 0.8f, 200);
        if (coloured) {
            const float cx = w - h * .45f - 4, cy = h * .55f, rr = h * .36f;
            fillPoly(k, octagon(cx, cy, rr, rr), col, 35);
            glowPath(k, octagon(cx, cy, rr, rr), col, 0.9f, 255, true);
        }
    });
    c.blit(*face, r.x, r.y, alpha);
    {  // the technology's wireframe, faint, to the right of the name
        const int ww = std::min(w * 55 / 100, h * 2);
        techWireframe(c, t, Rect{r.x + w - ww - (coloured ? int(h * .55f) : 4), r.y + h * 30 / 100, ww, h * 64 / 100},
                      lighter(col, 0.45f), alpha * 70 / 255);
    }
    if (coloured) {
        const int s = int(h * .46f);
        drawSkipIcon(c, d.color, r.x + w - int(h * .45f) - 4 - s / 2, r.y + int(h * .55f) - s / 2, s, alpha);
    }
}

void tech(Canvas& c, int t, Rect r, int alpha) {
    if (t < 0 || t >= kTechCount || r.w < 8 || r.h < 8) return;
    const TechDef& d = kTechs[t];
    const uint32_t col = techColour(d.unitUpgrade ? TechColor::None : d.color);
    const int w = r.w, h = r.h;
    const float th = std::max(0.9f, w / 220.f);
    const bool coloured = !d.unitUpgrade && d.color != TechColor::None;
    const std::string key = "tech" + std::to_string(int(d.color)) + (d.unitUpgrade ? "u" : "") + "_" + std::to_string(w) + "x" + std::to_string(h);
    const float scx = w * .885f, scy = h * .80f, sr = h * .15f;
    const Image* face = bake(c, key, w, h, [&](Canvas& k) {
        cardFace(k, w, h, col, hashKey(key));
        circuits(k, w, h, hashKey(key) ^ 99u);
        // the name bar across the top, stepping up at the right
        glowPath(k, {w * .03f, h * .17f, w * .74f, h * .17f, w * .79f, h * .07f, w * .97f, h * .07f}, col, th);
        // the prerequisite rail down the left
        glow(k, w * .115f, h * .22f, w * .115f, h * .93f, col, th * 0.7f, 150);
        // the colour badge, bottom right
        if (coloured) {
            fillPoly(k, octagon(scx, scy, sr, sr), col, 40);
            glowPath(k, octagon(scx, scy, sr, sr), col, th * 1.2f, 255, true);
            glowPath(k, octagon(scx, scy, sr * .78f, sr * .78f), col, th * .6f, 110, true);
        } else {
            glowPath(k, {w * .80f, h * .93f, w * .97f, h * .93f, w * .97f, h * .66f}, col, th, 200);
        }
    });
    c.blit(*face, r.x, r.y, alpha);
    // the wireframe behind the text, where the printed cards have it
    techWireframe(c, t, Rect{r.x + int(w * .15f), r.y + int(h * .22f), int(w * .80f), int(h * .70f)}, lighter(col, 0.5f),
                  alpha * (d.unitUpgrade ? 95 : 80) / 255);
    if (coloured) {
        const int s = int(sr * 1.25f);
        drawSkipIcon(c, d.color, r.x + int(scx) - s / 2, r.y + int(scy) - s / 2, s, alpha);
    }
    // prerequisites: one small badge per required colour, up the rail
    {
        const int s = std::max(9, int(h * .085f));
        int y = r.y + int(h * .90f) - s;
        for (int k = 0; k < 4; ++k)
            for (int n = 0; n < d.req[k]; ++n) {
                const float cx = r.x + w * .115f, cy = y + s / 2.f;
                c.fillCircle(cx, cy, s * .62f, rgb(8, 9, 15), alpha);
                c.ring(cx, cy, s * .62f, 1.2f, techColour(TechColor(k)), alpha);
                drawSkipIcon(c, TechColor(k), int(cx) - s / 2 + 1, y + 1, s - 2, alpha);
                y -= s + 4;
            }
    }
    // name and text
    const FontData& nf = w >= 400 ? FONT_LG : w >= 260 ? FONT_MDB : FONT_SMB;
    drawText(c, nf, r.x + int(w * .05f), r.y + int(h * .12f) - nf.lineHeight / 2 - 2, fitText(nf, d.name, int(w * .68f)), theme::text, alpha);
    if (w < 260) return;
    Rect box{r.x + int(w * .17f), r.y + int(h * .25f), int(w * .62f), int(h * .62f)};
    const FontData& tf = w >= 400 ? FONT_SM : FONT_XS;
    int y = box.y;
    for (const std::string& l : wrapText(tf, techSummary(t), box.w)) {
        if (y + tf.lineHeight > box.y + box.h) break;
        drawText(c, tf, box.x, y, l, theme::text, alpha);
        y += tf.lineHeight + 1;
    }
    std::string kind = d.unitUpgrade ? "UNIT UPGRADE" : d.color == TechColor::None ? "FACTION TECHNOLOGY" : "";
    if (!kind.empty()) drawText(c, FONT_XS, box.x, r.y + int(h * .91f), kind, lighter(col, 0.3f), alpha);
}

// ------------------------------------------------------------------ strategy cards
int strategyHeight(int w) { return std::max(1, w * 62 / 100); }

void strategy(Canvas& c, int card, int x, int y, int w, int alpha) {
    if (card < 0 || card >= kStrategyCards || w < 20) return;
    const int h = strategyHeight(w);
    const uint32_t col = strategyColour(card);
    const float t = std::max(0.9f, w / 170.f);
    const std::string key = "strat" + std::to_string(card) + "_" + std::to_string(w);
    const float rcx = w * .30f, rcy = h * .37f, rr = w * .135f;
    if (w < 70) {  // a mini card (faction web, 0.19.0): the shape, the colour and a big number
        const Image* mini = bake(c, key, w, h, [&](Canvas& k) {
            std::vector<float> left = {w * .01f, h * .02f, w * .52f, h * .02f, w * .52f, h * .98f, w * .20f, h * .98f};
            std::vector<float> right = {w * .56f, h * .02f, w * .93f, h * .02f, w * .99f, h * .14f, w * .84f, h * .98f, w * .56f, h * .98f};
            fillPoly(k, left, shade(col, 0.45f), 255);
            fillPoly(k, right, rgb(9, 10, 16), 255);
            glowPath(k, left, col, 0.9f, 255, true);
            glowPath(k, right, col, 0.9f, 255, true);
            glow(k, w * .60f, h * .56f, w * .88f, h * .56f, col, 0.6f, 200);
        });
        c.blit(*mini, x, y, alpha);
        const FontData& f = h >= 26 ? FONT_MDB : FONT_SMB;
        drawTextAligned(c, f, x + int(w * .06f), y + h / 2 - f.lineHeight / 2, int(w * .44f), Align::Center, std::to_string(card + 1),
                        theme::text, alpha);
        return;
    }
    const Image* face = bake(c, key, w, h, [&](Canvas& k) {
        // two panels: the left one (number) and the right one (abilities); the
        // card narrows towards the bottom like the printed strategy cards
        std::vector<float> left = {w * .01f, h * .02f, w * .49f, h * .02f, w * .49f, h * .98f, w * .20f, h * .98f};
        std::vector<float> right = {w * .51f, h * .02f, w * .93f, h * .02f, w * .99f, h * .12f, w * .82f, h * .98f, w * .51f, h * .98f};
        fillPoly(k, left, rgb(9, 10, 16), 255);
        fillPoly(k, right, rgb(9, 10, 16), 255);
        // colour haze from the top of each panel
        for (int yy = 0; yy < h; ++yy) {
            int a = std::max(0, 40 - yy * 70 / std::max(1, h));
            if (!a) continue;
            float lx0 = w * .01f + (w * .19f) * yy / h, rx1 = w * .99f - (w * .17f) * yy / h;
            k.fillRect(int(lx0) + 1, yy, int(w * .48f - lx0), 1, col, a);
            k.fillRect(int(w * .52f), yy, int(rx1 - w * .52f), 1, col, a);
        }
        stars(k, Rect{int(w * .08f), int(h * .05f), int(w * .38f), int(h * .85f)}, w * h / 500, hashKey(key));
        stars(k, Rect{int(w * .55f), int(h * .05f), int(w * .3f), int(h * .85f)}, w * h / 700, hashKey(key) ^ 5u);
        glowPath(k, left, col, t, 255, true);
        glowPath(k, right, col, t, 255, true);
        // the ring for the number, with a fainter outer ring and cross-hairs
        k.fillCircle(rcx, rcy, rr, col, 35);
        glowRing(k, rcx, rcy, rr, col, t * 1.2f);
        k.ring(rcx, rcy, rr * 1.35f, 1.f, col, 90);
        k.line(rcx - rr * 1.7f, rcy, rcx - rr * 1.15f, rcy, col, 1.f, 120);
        k.line(rcx + rr * 1.15f, rcy, rcx + rr * 1.7f, rcy, col, 1.f, 120);
        // the dividing line between primary and secondary abilities
        glow(k, w * .53f, h * .56f, w * .89f, h * .56f, col, t * 0.7f, 200);
    });
    c.blit(*face, x, y, alpha);
    const FontData& numF = numberFont(int(rr * 1.4f));
    drawTextAligned(c, numF, x + int(rcx) - 40, y + int(rcy) - numF.lineHeight / 2, 80, Align::Center, std::to_string(card + 1), theme::text, alpha);
    const FontData& nameF = w >= 260 ? FONT_MDB : w >= 150 ? FONT_SMB : FONT_XS;
    std::string name = strategyName(card);
    for (char& ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    drawTextAligned(c, nameF, x + int(w * .05f), y + int(h * .68f), int(w * .42f), Align::Center, fitText(nameF, name, int(w * .42f)),
                    lighter(col, 0.3f), alpha);
    if (w >= 150) {
        drawText(c, FONT_XS, x + int(w * .56f), y + int(h * .07f), "PRIMARY", lighter(col, 0.35f), alpha);
        drawText(c, FONT_XS, x + int(w * .56f), y + int(h * .60f), "SECONDARY", lighter(col, 0.35f), alpha);
    }
    if (w < 380) return;  // abilities: only on big cards (callers print them beside small ones)
    Rect prim{x + w * 56 / 100, y + h * 15 / 100, w * 37 / 100, h * 40 / 100};
    Rect sec{x + w * 56 / 100, y + h * 68 / 100, w * 30 / 100, h * 29 / 100};
    for (const auto& pr : {std::make_pair(prim, strategyPrimary(card)), std::make_pair(sec, strategySecondary(card))}) {
        int yy = pr.first.y;
        for (const std::string& l : wrapText(FONT_XS, pr.second, pr.first.w)) {
            if (yy + FONT_XS.lineHeight > pr.first.y + pr.first.h) break;
            drawText(c, FONT_XS, pr.first.x, yy, l, theme::text, alpha);
            yy += FONT_XS.lineHeight;
        }
    }
}

// ------------------------------------------------------------------ control tokens
void controlToken(Canvas& c, int faction, uint32_t colour, int cx, int cy, int diameter, int alpha) {
    if (diameter < 6) return;
    const int d = diameter;
    const std::string key = "ctl" + std::to_string(faction) + "_" + std::to_string(colour) + "_" + std::to_string(d);
    const Image* tok = bake(c, key, d + 2, d + 2, [&](Canvas& k) {
        const float r = d / 2.f, o = (d + 2) / 2.f;
        k.fillCircle(o, o, r, rgb(5, 7, 14));
        // stars inside the disc only
        uint32_t s = hashKey(key) | 1;
        for (int i = 0; i < d * d / 30; ++i) {
            s = s * 1664525u + 1013904223u;
            float ang = float((s >> 8) % 6283) / 1000.f;
            s = s * 1664525u + 1013904223u;
            float rad = r * 0.92f * std::sqrt(float((s >> 8) % 1000) / 1000.f);
            k.pixel(int(o + rad * std::cos(ang)), int(o + rad * std::sin(ang)), rgb(220, 225, 255), 70 + int((s >> 4) % 140));
        }
        // a glow in the player's colour, brightest in the middle
        for (int i = 6; i >= 1; --i) k.fillCircle(o, o, r * (0.25f + 0.11f * i), colour, 22);
        k.ring(o, o, r - 0.6f, std::max(1.2f, d / 12.f), colour);
        k.ring(o, o, r - std::max(1.2f, d / 12.f) - 0.8f, 1.f, lighter(colour, 0.5f), 120);
        const int is = std::max(4, int(d * 0.62f));
        drawFactionIcon(k, faction, int(o) - is / 2, int(o) - is / 2, is);
    });
    c.blit(*tok, cx - (d + 2) / 2, cy - (d + 2) / 2, alpha);
}

// ------------------------------------------------------------------ command tokens
namespace {
// The token outline: an upward triangle (side s) with the top corner clipped
// to a short flat and the bottom corners clipped a little, as on the board
// game's tokens. Points clockwise from the top left; bounding box w x h with
// its top left at (x0, y0).
struct TokenShape {
    float px[6], py[6];
    float cx, cy;   // centroid of the triangle (where the symbol goes)
    float s;        // triangle side
};
TokenShape tokenShape(float x0, float y0, float w) {
    TokenShape t;
    const float s = w / 0.92f, ct = 0.15f * s, cb = 0.08f * s, h = 0.866f * s - ct * 0.866f;
    const float xc = x0 + w / 2.f, y1 = y0 + h;
    const float xs[6] = {xc - ct / 2, xc + ct / 2, xc + s / 2 - cb / 2, xc + s / 2 - cb, xc - s / 2 + cb, xc - s / 2 + cb / 2};
    const float ys[6] = {y0, y0, y1 - cb * 0.866f, y1, y1, y1 - cb * 0.866f};
    for (int i = 0; i < 6; ++i) {
        t.px[i] = xs[i];
        t.py[i] = ys[i];
    }
    t.cx = xc;
    t.cy = y1 - 0.2887f * s;
    t.s = s;
    return t;
}
void scaled(const TokenShape& t, float k, float* xs, float* ys) {
    for (int i = 0; i < 6; ++i) {
        xs[i] = t.cx + (t.px[i] - t.cx) * k;
        ys[i] = t.cy + (t.py[i] - t.cy) * k;
    }
}
void outlinePoly(Canvas& c, const float* xs, const float* ys, uint32_t col, float th, int a) {
    for (int i = 0; i < 6; ++i) c.line(xs[i], ys[i], xs[(i + 1) % 6], ys[(i + 1) % 6], col, th, a);
}
// Our own little ship for the fleet-pool side: nose up, swept wings, engine glow.
void shipSilhouette(Canvas& k, float cx, float cy, float r, uint32_t colour) {
    static const float sx[13] = {0.f, 0.17f, 0.2f, 0.55f, 0.55f, 0.2f, 0.13f, -0.13f, -0.2f, -0.55f, -0.55f, -0.2f, -0.17f};
    static const float sy[13] = {-1.f, -0.5f, 0.2f, 0.5f, 0.78f, 0.68f, 0.9f, 0.9f, 0.68f, 0.78f, 0.5f, 0.2f, -0.5f};
    float xs[13], ys[13];
    for (int i = 0; i < 13; ++i) {
        xs[i] = cx + sx[i] * r;
        ys[i] = cy + sy[i] * r;
    }
    k.fillPolygon(xs, ys, 13, rgb(214, 222, 238));
    k.line(cx, cy - r * 0.7f, cx, cy + r * 0.55f, rgb(120, 130, 150), std::max(1.f, r * 0.08f));
    k.fillCircle(cx, cy + r * 0.98f, std::max(1.2f, r * 0.14f), lighter(colour, 0.5f), 220);
}
}  // namespace

int commandTokenHeight(int w) { return int(std::lround(0.866f * (w / 0.92f) * 0.85f)); }

void commandToken(Canvas& c, int faction, uint32_t colour, int cx, int cy, int w, int alpha, bool shipSide) {
    if (w < 5) return;
    const int h = commandTokenHeight(w);
    const int m = 2 + w / 24;  // room for the rim
    const std::string key = "cmd" + std::to_string(faction) + "_" + std::to_string(colour) + "_" + std::to_string(w) + (shipSide ? "s" : "f");
    const Image* tok = bake(c, key, w + 2 * m, h + 2 * m, [&](Canvas& k) {
        const TokenShape t = tokenShape(float(m), float(m), float(w));
        float xs[6], ys[6];
        if (w < 14) {  // tiny: a plain token in the player's colour
            k.fillPolygon(t.px, t.py, 6, colour);
            scaled(t, 0.55f, xs, ys);
            k.fillPolygon(xs, ys, 6, lighter(colour, 0.35f));
            outlinePoly(k, t.px, t.py, shade(colour, 0.45f), 1.f, 255);
            return;
        }
        k.fillPolygon(t.px, t.py, 6, rgb(5, 7, 14));
        // stars on the face
        uint32_t sd = hashKey(key) | 1;
        auto rnd = [&]() {
            sd = sd * 1664525u + 1013904223u;
            return float((sd >> 8) % 1000) / 1000.f;
        };
        scaled(t, 0.86f, xs, ys);
        for (int i = 0; i < w * h / 34; ++i) {
            float u = rnd(), v = rnd();
            if (u + v > 1.f) {
                u = 1.f - u;
                v = 1.f - v;
            }
            // a point in the triangle, pulled in so it stays off the rim
            const float x = xs[4] + u * (xs[3] - xs[4]) + v * ((xs[0] + xs[1]) / 2 - xs[4]);
            float y = ys[4] + v * (ys[0] - ys[4]);
            k.pixel(int(x), int(y), rgb(220, 225, 255), 60 + int(rnd() * 150));
        }
        // a glow in the player's colour, brightest in the middle
        for (int i = 6; i >= 1; --i) {
            scaled(t, 0.22f + 0.11f * i, xs, ys);
            k.fillPolygon(xs, ys, 6, colour, 24);
        }
        const float th = std::max(1.2f, w / 24.f);
        scaled(t, 1.f - (th * 0.5f) / (0.2887f * t.s), xs, ys);
        outlinePoly(k, xs, ys, colour, th, 255);
        scaled(t, 1.f - (th + 1.2f) / (0.2887f * t.s), xs, ys);
        outlinePoly(k, xs, ys, lighter(colour, 0.55f), 1.f, 130);
        const float inr = 0.2887f * t.s;  // inscribed circle of the triangle
        if (shipSide) {
            shipSilhouette(k, t.cx, t.cy + inr * 0.05f, inr * 0.86f, colour);
        } else {
            const int is = std::max(5, int(inr * 1.6f));
            drawFactionIcon(k, faction, int(t.cx) - is / 2, int(t.cy) - is / 2 + int(inr * 0.06f), is);
        }
    });
    c.blit(*tok, cx - (w + 2 * m) / 2, cy - (h + 2 * m) / 2, alpha);
}

// ------------------------------------------------------------------ exploration backs
void exploreBack(Canvas& c, int deck, Rect r, int alpha) {
    if (r.w < 8 || r.h < 8) return;
    static const char* names[4] = {"cultural", "hazardous", "industrial", "frontier"};
    static const char* titles[4] = {"CULTURAL", "HAZARDOUS", "INDUSTRIAL", "FRONTIER"};
    static const uint32_t cols[4] = {rgb(60, 140, 255), rgb(235, 60, 50), rgb(60, 200, 90), rgb(175, 175, 200)};
    deck &= 3;
    const uint32_t col = cols[deck];
    const int w = r.w, h = r.h;
    const float t = std::max(0.8f, w / 90.f);
    const std::string key = "explore" + std::to_string(deck) + "_" + std::to_string(w) + "x" + std::to_string(h);
    const Image* back = bake(c, key, w, h, [&](Canvas& k) {
        cardFace(k, w, h, col, hashKey(key));
        // a frame with clipped corners, and a second, fainter one inside it
        const float m = w * .07f, cut = w * .12f;
        glowPath(k, {m + cut, m, w - m - cut, m, w - m, m + cut, w - m, h - m - cut, w - m - cut, h - m, m + cut, h - m, m, h - m - cut, m, m + cut},
                 col, t, 255, true);
        const float m2 = m + w * .05f;
        glowPath(k, {m2 + cut, m2, w - m2 - cut, m2, w - m2, m2 + cut, w - m2, h - m2 - cut, w - m2 - cut, h - m2, m2 + cut, h - m2, m2, h - m2 - cut, m2, m2 + cut},
                 col, t * .5f, 90, true);
        // the deck's emblem in a glowing circle
        const float cx = w * .5f, cy = h * .46f, rr = w * .25f;
        for (int i = 5; i >= 1; --i) k.fillCircle(cx, cy, rr * (0.6f + 0.12f * i), col, 18);
        glowRing(k, cx, cy, rr, col, t * 1.2f);
        const int s = int(rr * 1.3f);
        icons::draw(k, names[deck], int(cx) - s / 2, int(cy) - s / 2, s);
    });
    c.blit(*back, r.x, r.y, alpha);
    if (w >= 100) {  // the deck's name inside the inner frame
        const FontData& f = w >= 150 ? FONT_SMB : FONT_XS;
        textIfFits(c, f, r.x + int(w * .14f), r.y + int(h * .76f), int(w * .72f), titles[deck], lighter(col, 0.45f), alpha);
    }
}

void actionBack(Canvas& c, Rect r, int alpha) {
    if (r.w < 8 || r.h < 8) return;
    static Image src;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (!loadPicture("action_back", src)) LOG("cards: no action card back picture (art/action_back-*.png)");
    }
    if (src.empty()) {  // drawn instead: a red card with the action card emblem
        const uint32_t col = rgb(235, 90, 40);
        const std::string key = "actionback_" + std::to_string(r.w) + "x" + std::to_string(r.h);
        const Image* back = bake(c, key, r.w, r.h, [&](Canvas& k) {
            cardFace(k, r.w, r.h, col, hashKey(key));
            const float m = r.w * .07f, cut = r.w * .12f, w = float(r.w), h = float(r.h);
            glowPath(k, {m + cut, m, w - m - cut, m, w - m, m + cut, w - m, h - m - cut, w - m - cut, h - m, m + cut, h - m, m, h - m - cut, m, m + cut},
                     col, std::max(0.8f, w / 90.f), 255, true);
            const int s = int(w * .42f);
            icons::draw(k, "action_card", int(w * .5f) - s / 2, int(h * .46f) - s / 2, s);
        });
        c.blit(*back, r.x, r.y, alpha);
        return;
    }
    // fitted once to the size in real pixels (sharp on the PC picture)
    static Image fit;
    const Rect p = c.toPx(r);
    if (fit.w != p.w || fit.h != p.h) fit = resizeImage(src, p.w, p.h);
    c.pxBlit(fit, p.x, p.y, alpha);
}

// ------------------------------------------------------------------ faction labels
void factionLabel(Canvas& c, int faction, uint32_t colour, int x, int y, int w, int h, int alpha) {
    if (faction < 0 || faction >= kFactionCount || w < 40 || h < 12) return;
    const std::string key = "label" + std::to_string(faction) + "_" + std::to_string(colour) + "_" + std::to_string(w) + "x" + std::to_string(h);
    const Image* lab = bake(c, key, w, h, [&](Canvas& k) {
        const float s = h * 0.55f;      // the slant of both ends
        const float split = w * 0.27f;  // where the symbol panel ends (at the top)
        // the banner: deep space, lighter at the left
        for (int xx = 0; xx < w; ++xx) {
            float top = xx < s ? h * (s - xx) / s : 0.f;           // left end slants "/"
            float bot = xx > w - s ? h * (w - xx) / s : float(h);  // right end slants "\"
            if (bot <= top) continue;
            float u = float(xx) / w;
            uint32_t col = mix(rgb(40, 95, 175), rgb(14, 20, 52), int(std::min(1.f, u * 1.25f) * 255));
            int y0 = int(std::ceil(top)), y1 = int(std::floor(bot));
            k.fillRect(xx, y0, 1, std::max(0, y1 - y0), col);
            if (y0 > 0) k.pixel(xx, y0 - 1, col, int((y0 - top) * 255));  // soft edges along the slants
            if (y1 < h) k.pixel(xx, y1, col, int((bot - y1) * 255));
        }
        // a diagonal light streak
        for (int xx = int(w * .45f); xx < int(w * .72f); ++xx)
            for (int yy = 0; yy < h; ++yy) {
                float d = std::fabs((xx - w * .58f) + (yy - h * .5f) * 0.9f);
                int a = int(std::max(0.f, 28.f - d * 1.4f));
                if (a > 0) k.pixel(xx, yy, rgb(170, 200, 255), a);
            }
        stars(k, Rect{int(split), 2, int(w - split - s), h - 4}, w * h / 140, hashKey(key));
        // the symbol panel in the player's colour, darker towards its edge
        const float slope = s * 0.55f;
        for (int yy = 0; yy < h; ++yy) {
            float left = yy < h ? s * (1.f - float(yy) / h) : 0.f;  // follows the "/" end
            float right = split - slope * float(yy) / h;
            for (int xx = int(std::ceil(left)); xx <= int(right); ++xx) {
                float u = (xx - left) / std::max(1.f, right - left);
                k.pixel(xx, yy, mix(shade(colour, 0.40f), shade(colour, 1.0f), int(u * 210)), 235);
            }
        }
        // metal edges: the outline and the panel's edge
        const uint32_t edgeCol = rgb(150, 175, 210);
        k.line(s, 0.5f, w - 0.5f, 0.5f, edgeCol, 1.f, 170);
        k.line(0.5f, h - 0.5f, w - s, h - 0.5f, edgeCol, 1.f, 170);
        k.line(s, 0.5f, 0.5f, h - 0.5f, edgeCol, 1.2f, 200);
        k.line(w - 0.5f, 0.5f, w - s, h - 0.5f, edgeCol, 1.2f, 200);
        k.line(split, 0.5f, split - slope, h - 0.5f, lighter(colour, 0.4f), 1.6f, 235);
        k.line(split + 3, 0.5f, split + 3 - slope, h - 0.5f, edgeCol, 1.f, 110);
        // the faction's symbol
        const int is = int(h * 0.76f);
        drawFactionIcon(k, faction, int((s * 0.5f + split - slope * 0.5f) * 0.5f + s * 0.25f) - is / 2, (h - is) / 2, is);
        // the full name in capitals, one or two lines, with a dark shadow
        std::string name = kFactions[faction].name;
        for (char& ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        const int tx = int(split + 4), tw = int(w - s * 0.8f - tx);
        const FontData* f = h >= 44 ? &FONT_MDB : &FONT_SMB;
        auto lines = wrapText(*f, name, tw);
        if (lines.size() > 2 || (h < 30 && lines.size() > 1)) {
            f = &FONT_XS;
            lines = wrapText(*f, name, tw);
        }
        if (lines.size() > 2) lines.resize(2);
        int ty = (h - int(lines.size()) * f->lineHeight) / 2;
        for (const std::string& l : lines) {
            drawTextAligned(k, *f, tx + 1, ty + 1, tw, Align::Center, l, rgb(0, 0, 10), 200);
            drawTextAligned(k, *f, tx, ty, tw, Align::Center, l, theme::text);
            ty += f->lineHeight;
        }
    });
    c.blit(*lab, x, y, alpha);
}
}  // namespace cards
