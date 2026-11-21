#include "ui/decor_tiles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gfx/font.h"
#include "ui/cards.h"
#include "ui/icons.h"
#include "ui/retro.h"
#include "ui/voidfall.h"
#include "ui/theme.h"

using namespace ti;

namespace decor {

namespace {

constexpr float kPi = 3.14159265f;

struct Rnd {
    uint32_t s;
    explicit Rnd(uint32_t seed) : s(seed * 2654435761u + 977u) {}
    float next(float lo, float hi) {
        s = s * 1664525u + 1013904223u;
        return lo + (hi - lo) * float((s >> 8) % 10000) / 10000.f;
    }
};

uint32_t lighter(uint32_t c, float k) { return mix(c, rgb(255, 255, 255), int(255.f * std::max(0.f, std::min(1.f, k)))); }

void corners(float cx, float cy, float r, float* xs, float* ys) {
    for (int i = 0; i < 6; ++i) {
        const float a = kPi / 3.f * float(i);
        xs[i] = cx + r * std::cos(a);
        ys[i] = cy + r * std::sin(a);
    }
}

void hexFill(Canvas& c, float cx, float cy, float r, uint32_t col, int a = 255) {
    float xs[6], ys[6];
    corners(cx, cy, r, xs, ys);
    c.fillPolygon(xs, ys, 6, col, a);
}

void hexEdge(Canvas& c, float cx, float cy, float r, uint32_t col, float th, int a) {
    float xs[6], ys[6];
    corners(cx, cy, r, xs, ys);
    for (int i = 0; i < 6; ++i) c.line(xs[i], ys[i], xs[(i + 1) % 6], ys[(i + 1) % 6], col, th, a);
}

// A jagged bolt from (x0, y0) to (x1, y1); seed picks its shape.
void bolt(Canvas& c, float x0, float y0, float x1, float y1, uint32_t seed, float wiggle, int a, float scale, bool electric = false) {
    // pink (the Fracture) or blue-white electricity (Thunder's Edge, 0.28.1)
    const uint32_t glow = electric ? rgb(90, 150, 255) : rgb(255, 40, 150), mid = electric ? rgb(170, 210, 255) : rgb(255, 110, 200),
                   core = electric ? rgb(240, 248, 255) : rgb(255, 225, 245);
    Rnd r(seed);
    const int n = 9;
    float px = x0, py = y0;
    const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy) + 1e-3f;
    const float nx = -dy / len, ny = dx / len;
    for (int i = 1; i <= n; ++i) {
        const float k = float(i) / float(n);
        const float off = i == n ? 0.f : r.next(-wiggle, wiggle);
        const float x = x0 + dx * k + nx * off, y = y0 + dy * k + ny * off;
        c.line(px, py, x, y, glow, std::max(2.f, 4.f * scale), a / 4);
        c.line(px, py, x, y, mid, std::max(1.f, 1.8f * scale), a * 3 / 4);
        c.line(px, py, x, y, core, std::max(1.f, 0.8f * scale), a);
        if (i == n / 2 && r.next(0, 1) < 0.6f) {  // a fork
            const float fx = x + nx * len * 0.18f + dx * 0.12f, fy = y + ny * len * 0.18f + dy * 0.12f;
            c.line(x, y, fx, fy, mid, std::max(1.f, 1.2f * scale), a / 2);
        }
        px = x;
        py = y;
    }
}

// The red nebula all Fracture tiles share.
void nebula(Canvas& c, float cx, float cy, float r, uint32_t seed, uint32_t now) {
    hexFill(c, cx, cy, r, rgb(34, 4, 7));  // the Fracture is red (owner, 0.27.0)
    Rnd rd(seed);
    const float t = float(now % 20000) / 20000.f;
    for (int i = 0; i < 7; ++i) {
        const float a = rd.next(0, 2 * kPi), d = rd.next(0, 0.5f) * r;
        const float drift = std::sin(t * 2 * kPi + float(i)) * r * 0.03f;
        c.fillCircle(cx + std::cos(a) * d + drift, cy + std::sin(a) * d, rd.next(0.18f, 0.36f) * r, i % 3 ? rgb(175, 18, 26) : rgb(235, 60, 50), 40);
    }
    const int stars = int(std::min(60.f, r * 0.6f));
    for (int i = 0; i < stars; ++i) {
        const float a = rd.next(0, 2 * kPi), d = std::sqrt(rd.next(0, 1)) * r * 0.82f;
        const int v = int(rd.next(120, 255));
        c.pixel(int(cx + std::cos(a) * d), int(cy + std::sin(a) * d), rgb(v, v * 4 / 5, v * 4 / 5), int(rd.next(120, 255)));
    }
}

void planetBody(Canvas& c, float x, float y, float r, uint32_t base, uint32_t seed, int kind) {
    // kind: 0 ice (Cocytus), 1 storm-ocean (Styx), 2 fire (Lethe), 3 moon (Phlegethon), 4 storm (Thunder's Edge)
    c.fillCircle(x, y, r * 1.12f, lighter(base, 0.3f), 40);
    c.fillCircle(x, y, r, base);
    Rnd rd(seed);
    if (kind == 0 || kind == 3) {  // craters
        for (int i = 0; i < 9; ++i) {
            const float a = rd.next(0, 2 * kPi), d = rd.next(0, 0.7f) * r, s = rd.next(0.08f, 0.18f) * r;
            c.fillCircle(x + std::cos(a) * d, y + std::sin(a) * d, s, shade(base, 0.72f), 200);
            c.fillCircle(x + std::cos(a) * d - s * 0.25f, y + std::sin(a) * d - s * 0.25f, s * 0.55f, shade(base, 1.15f), 120);
        }
    } else if (kind == 1) {  // dark sea, cracked pale continents
        for (int i = 0; i < 7; ++i) {
            const float a = rd.next(0, 2 * kPi), d = rd.next(0, 0.65f) * r, s = rd.next(0.12f, 0.26f) * r;
            c.fillCircle(x + std::cos(a) * d, y + std::sin(a) * d, s, rgb(70, 110, 170), 170);
        }
        for (int i = 0; i < 6; ++i) {
            const float a = rd.next(0, 2 * kPi);
            c.line(x + std::cos(a) * r * 0.2f, y + std::sin(a) * r * 0.2f, x + std::cos(a + 0.4f) * r * 0.8f, y + std::sin(a + 0.4f) * r * 0.8f,
                   rgb(150, 200, 255), 1.f, 150);
        }
    } else if (kind == 2) {  // burning
        for (int i = 0; i < 10; ++i) {
            const float a = rd.next(0, 2 * kPi), d = rd.next(0, 0.7f) * r, s = rd.next(0.07f, 0.17f) * r;
            c.fillCircle(x + std::cos(a) * d, y + std::sin(a) * d, s, i % 2 ? rgb(255, 210, 90) : rgb(120, 30, 10), 200);
        }
    }
    c.fillCircle(x - r * 0.3f, y - r * 0.3f, r * 0.55f, rgb(255, 255, 255), 36);  // light from the upper left
    for (int k = 0; k < 3; ++k) c.ring(x + r * 0.07f * float(k), y + r * 0.07f * float(k), r * (1.f - 0.08f * float(k)), r * 0.12f, rgb(0, 0, 0), 20);
}

void label(Canvas& c, float x, float y, const std::string& name, int res, int inf, bool legendary, float r) {
    const FontData& f = r > 120 ? FONT_SMB : FONT_XS;
    const int w = textWidth(f, name) + 10;
    c.fillRoundRect(int(x) - w / 2, int(y), w, f.lineHeight + 2, 4, rgb(10, 6, 12), 210);
    drawTextAligned(c, f, int(x) - w / 2, int(y) + 1, w, Align::Center, name, theme::text);
    const int iy = int(y) + f.lineHeight + 12;
    drawResourceIcon(c, int(x) - 11, iy, 16, res, FONT_XS, false);
    drawResourceIcon(c, int(x) + 11, iy, 16, inf, FONT_XS, true);
    icons::draw(c, "relic", int(x) + 22, iy - 8, 15);
    if (legendary) icons::draw(c, "legendary_white", int(x) - 38, iy - 8, 15, 255, theme::gold);
}

// ---------------------------------------------------------------- Retrowave (0.28.1, owner)
// The same tiles in the pixel-art set: the Retrowave backgrounds and lit planet
// spheres, the Fracture's red wash, rifts and egress vortex, and for Thunder's
// Edge a shrouded storm - dithered clouds turning round a storm world.
int systemByKey(const char* key) {
    for (int i = 0; i < kSystemCount; ++i)
        if (!std::strcmp(kSystems[i].key, key)) return i;
    return -1;
}
int planetByName(const char* name) {
    for (int i = 0; i < kPlanetCount; ++i)
        if (!std::strcmp(kPlanets[i].name, name)) return i;
    return -1;
}

// 4x4 ordered dither threshold, 0..15
int bayer4(int x, int y) {
    static const int m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return m[(y & 3) * 4 + (x & 3)];
}

// Thunder's Edge, expedition side (0.28.1, owner): a grey giant under perpetual storms,
// mostly hidden in cloud, on misty grey space crackling with electricity. Everything
// is drawn in art-sized cells (48 across the tile) with ordered dithering.
struct Cells {
    float cx, cy, r, px;
    int n;
    // the Retrowave art is 96 pixels across: that many cells in the large views, half on the map
    // (0.48.0: with the sharp PC art, twice as many - put in real pixels)
    int k;
    Cells(float cx_, float cy_, float r_) : cx(cx_), cy(cy_), r(r_), px(0), n(r_ >= 100.f ? 96 : 48), k(retro::artScale()) {
        n *= k;
        px = 2.f * r / float(n);
    }
    bool inHex(float nx, float ny) const { return std::fabs(ny) <= 0.86f && std::fabs(nx) * 0.866f + std::fabs(ny) * 0.5f <= 0.866f; }
    void put(Canvas& c, int i, int j, uint32_t col, int a) const {
        const float x = cx - r + float(i) * px, y = cy - r * 0.866f + float(j) * px;
        if (k > 1) {
            const int x0 = int(std::floor(c.fX(x))), x1 = std::max(x0 + 1, int(std::floor(c.fX(x + px))));
            const int y0 = int(std::floor(c.fY(y))), y1 = std::max(y0 + 1, int(std::floor(c.fY(y + px))));
            for (int yy = y0; yy < y1; ++yy) c.pxSpan(x0, x1, yy, col, a);
            return;
        }
        const int w = std::max(1, int(px + 0.99f));
        c.fillRect(int(x), int(y), w, w, col, a);
    }
    float nx(int i) const { return (-r + (float(i) + 0.5f) * px) / r; }
    float ny(int j) const { return (-r * 0.866f + (float(j) + 0.5f) * px) / r; }
};

// misty grey space: three greys, drifting
void greyMist(Canvas& c, float cx, float cy, float r, uint32_t now) {
    Cells g(cx, cy, r);
    const float t = float(now % 120000u) / 1000.f;
    static const uint32_t grey[4] = {rgb(18, 20, 26), rgb(28, 31, 38), rgb(40, 44, 52), rgb(54, 58, 68)};
    for (int j = 0; j < g.n; ++j)
        for (int i = 0; i < g.n; ++i) {
            const float x = g.nx(i), y = g.ny(j);
            if (!g.inHex(x, y)) continue;
            float m = 0.5f + 0.25f * std::sin(x * 4.f + t * 0.35f + std::sin(y * 3.f - t * 0.2f) * 1.5f) +
                      0.18f * std::sin(y * 6.f - t * 0.5f + x * 2.f) + 0.07f * std::sin((x + y) * 13.f + t);
            const int lv = std::max(0, std::min(3, int(m * 3.f + float(bayer4(i, j)) / 16.f)));
            g.put(c, i, j, grey[lv], 255);
        }
}

// the grey giant: a lit sphere with storm bands and spirals turning on it
void stormWorld(Canvas& c, float cx, float cy, float pr, float r, uint32_t now) {
    Cells g(cx, cy, r);
    const float t = float(now % 200000u) / 1000.f;
    static const uint32_t ramp[6] = {rgb(28, 30, 36), rgb(52, 55, 62), rgb(82, 86, 94), rgb(116, 120, 128), rgb(156, 160, 168), rgb(205, 208, 214)};
    const float rr = pr / r;
    for (int j = 0; j < g.n; ++j)
        for (int i = 0; i < g.n; ++i) {
            const float x = g.nx(i) / rr, y = g.ny(j) / rr;
            const float d2 = x * x + y * y;
            if (d2 > 1.f) continue;
            const float z = std::sqrt(1.f - d2);
            float light = 0.25f + 0.75f * std::max(0.f, -0.55f * x - 0.45f * y + 0.7f * z);  // from the upper left
            // storms: latitude bands sheared by longitude and time, plus two turning eyes
            const float lon = std::atan2(x, z) + t * 0.08f;
            float st = 0.5f + 0.3f * std::sin(y * 9.f + std::sin(lon * 3.f + y * 4.f) * 1.4f) + 0.15f * std::sin(lon * 7.f - y * 5.f - t * 0.3f);
            for (int e = 0; e < 2; ++e) {
                const float ex = e ? 0.35f : -0.3f, ey = e ? 0.3f : -0.25f;
                const float dx = x - ex, dy = y - ey, de = std::sqrt(dx * dx + dy * dy);
                if (de < 0.32f) st += 0.35f * std::sin(std::atan2(dy, dx) * 2.f + de * 18.f - t * (e ? 1.1f : -0.9f)) * (1.f - de / 0.32f);
            }
            float v = light * (0.55f + 0.45f * st);
            if (d2 > 0.86f) v *= 0.75f;  // the limb
            int lv = std::max(1, std::min(5, int(v * 6.f + float(bayer4(i, j)) / 16.f + 0.4f)));
            if (d2 > 0.9f) lv = 0;  // a dark edge so the giant stands out from the mist
            g.put(c, i, j, ramp[lv], 255);
        }
}

// The shroud: clouds in a spiral round the centre. eye = the clear middle (0..1 of r),
// cover = how much they hide (higher = thicker).
void shroud(Canvas& c, float cx, float cy, float r, uint32_t now, float eye = 0.3f, float cover = 1.f) {
    Cells g(cx, cy, r);
    const float t = float(now % 60000u) / 1000.f;
    for (int j = 0; j < g.n; ++j)
        for (int i = 0; i < g.n; ++i) {
            const float nx = g.nx(i), ny = g.ny(j);
            if (!g.inHex(nx, ny)) continue;
            const float rho = std::sqrt(nx * nx + ny * ny), th = std::atan2(ny, nx);
            if (rho < eye) continue;
            float d = 0.5f * std::sin(3.f * th + 6.f * rho - t * 0.9f) + 0.32f * std::sin(5.f * th - 9.f * rho + t * 0.55f) +
                      0.18f * std::sin(11.f * th + 3.f * rho + t * 1.7f);
            d = d * 0.5f + 0.5f;
            if (eye > 0.f) d *= std::min(1.f, (rho - eye) / 0.18f) * (1.f - 0.35f * rho);
            d = std::min(1.f, d * cover);
            if (eye <= 0.f) {  // Thunder's Edge: soft clouds in a few steps of thickness, thinner over the planet
                if (rho < 0.5f) d *= 0.7f + 0.3f * rho / 0.5f;
                const int q = int(d * 5.f + float(bayer4(i, j)) / 32.f);
                if (q <= 1) continue;
                static const uint32_t cl[5] = {0, 0, rgb(120, 124, 136), rgb(170, 174, 186), rgb(222, 225, 232)};
                static const int al[5] = {0, 0, 120, 175, 225};
                g.put(c, i, j, cl[std::min(4, q)], al[std::min(4, q)]);
                continue;
            }
            const int level = int(d * 22.f);
            if (level <= bayer4(i, j)) continue;
            const uint32_t col = d > 0.78f ? rgb(226, 229, 236) : d > 0.6f ? rgb(176, 180, 192) : rgb(124, 128, 140);
            g.put(c, i, j, col, 235);
        }
}

// 0.30.0 (owner, "Waves of the Warp"): the Fracture's empty systems are warp storms - magenta and crimson
// clouds churning and folding over each other, white-hot where they break - in place of the black holes
void warpStorm(Canvas& c, float cx, float cy, float r, uint32_t now, uint32_t seed) {
    Cells g(cx, cy, r);
    const float t = float(now % 240000u) / 1000.f;
    const float sx = float(seed % 7u) * 0.9f, sy = float(seed % 5u) * 1.3f;
    static const uint32_t ramp[7] = {rgb(26, 2, 18),   rgb(64, 6, 40),    rgb(112, 12, 66),  rgb(168, 22, 92),
                                     rgb(214, 52, 128), rgb(244, 118, 172), rgb(255, 214, 232)};
    for (int j = 0; j < g.n; ++j)
        for (int i = 0; i < g.n; ++i) {
            const float x = g.nx(i), y = g.ny(j);
            if (!g.inHex(x, y)) continue;
            // a domain-warped flow: the clouds roll along a long diagonal wave and fold back on themselves
            const float wx = x + 0.35f * std::sin(y * 3.1f + t * 0.42f + sx) + 0.18f * std::sin(y * 7.f - t * 0.9f);
            const float wy = y + 0.30f * std::sin(x * 2.7f - t * 0.37f + sy) + 0.15f * std::sin(x * 6.3f + t * 0.8f);
            float f = 0.5f + 0.28f * std::sin(wx * 4.2f + wy * 1.7f - t * 0.55f) + 0.17f * std::sin(wx * 9.f - wy * 5.f + t * 0.9f) +
                      0.09f * std::sin((wx + wy) * 17.f - t * 1.6f) + 0.06f * std::sin(wx * 23.f + t * 2.1f + wy * 3.f);
            // darker towards the rim: the storm sits in the tile
            const float rim = std::sqrt(x * x + y * y);
            f *= 1.f - 0.28f * std::max(0.f, rim - 0.55f);
            int lv = int(f * 6.2f + float(bayer4(i, j)) / 16.f - 0.4f);
            lv = std::max(0, std::min(6, lv));
            g.put(c, i, j, ramp[lv], 255);
        }
}

void paintRetro(Canvas& c, const Tile& t, float cx, float cy, float r, uint32_t now, bool large) {
    const float sc = r / 60.f;
    retro::Tile rt;
    auto planet = [&](const char* name, float x, float y, float pr) {
        retro::Piece p;
        p.kind = retro::Piece::Planet;
        p.id = planetByName(name);
        p.x = x;
        p.y = y;
        p.r = pr;
        if (p.id >= 0) rt.pieces.push_back(p);
    };
    if (t.kind == ThundersEdge) {  // the expedition side: mostly hidden in cloud (0.28.1, owner)
        greyMist(c, cx, cy, r, now);
        for (int k = 0; k < 2; ++k) {  // electricity crawling through the mist
            const uint32_t per = 1300u + uint32_t(k) * 700u, ph = (now + uint32_t(k) * 500u) % per;
            if (ph < 220) {
                const float a0 = float((now / per + uint32_t(k) * 3u) % 6u) * kPi / 3.f + 0.4f;
                bolt(c, cx + std::cos(a0) * r * 0.85f, cy + std::sin(a0) * r * 0.7f, cx + std::cos(a0 + 1.4f) * r * 0.5f,
                     cy + std::sin(a0 + 1.4f) * r * 0.45f, now / per + uint32_t(k) * 17u, r * 0.1f, 255 - int(ph) * 255 / 220, sc * 0.8f, true);
            }
        }
        stormWorld(c, cx, cy, r * 0.46f, r, now);
        shroud(c, cx, cy, r, now, 0.f, 0.95f);  // the clouds hide most of it; the giant shows through the gaps
        const uint32_t phase = now % 2300;
        if (phase < 180) {  // thunder inside the clouds
            const int a = 255 - int(phase) * 255 / 180;
            bolt(c, cx - r * 0.45f, cy - r * 0.3f, cx + r * 0.2f, cy + r * 0.4f, now / 2300, r * 0.12f, a, sc, true);
            c.fillCircle(cx, cy, r * 0.55f, rgb(220, 230, 255), a / 6);
        }
        hexEdge(c, cx, cy, r, rgb(220, 190, 110), std::max(1.5f, 2.5f * sc), 230);  // (already in art-sized cells: no chunky pass)
        if (large) {
            const float pr = r * 0.3f;
            const int w = 150;
            c.fillRoundRect(int(cx) - w / 2, int(cy + pr * 1.3f), w, 18, 5, rgb(10, 12, 20), 220);
            drawTextAligned(c, FONT_SMB, int(cx) - w / 2, int(cy + pr * 1.3f) + 1, w, Align::Center, "THUNDER'S EDGE", rgb(230, 200, 120));
            drawTextAligned(c, FONT_XS, int(cx) - 120, int(cy + pr * 1.3f) + 22, 240, Align::Center, "Hidden in the storm",
                            rgb(190, 196, 210));
        }
        return;
    }
    // ---- the Fracture
    switch (t.kind) {
        case Rift: retro::setBackground(rt, systemByKey("19"), 47u + uint32_t(t.index)); break;  // stars under the warp (0.43.0, P13: no rift)
        case Egress: retro::setBackground(rt, systemByKey("19"), 29u + uint32_t(t.index)); break;  // stars round the vortex
        default: retro::setBackground(rt, systemByKey("19"), 11u + uint32_t(t.index)); break;  // stars
    }
    switch (t.kind) {
        case Cocytus: planet("Arcturus", -2.4f, -2.4f, 16.f); break;
        case Styx: planet("Quann", 0.f, 0.f, 20.f); break;
        case LethePhlegethon:
            planet("Muaat", 5.8f, -14.4f, 13.f);
            planet("Ixth", -1.f, 15.4f, 11.5f);
            break;
        default: break;
    }
    if (t.kind == Rift && retro::on()) warpStorm(c, cx, cy, r, now, uint32_t(t.index) * 31u + 7u);  // 0.30.0: no more black holes
    else retro::draw(c, rt, cx, cy, r, now);
    // 0.43.0 (owner, P13): Voidfall too - the warp, never a gravity rift (it would look like one's rules)
    if (t.kind == Rift && retro::hd()) warpStorm(c, cx, cy, r, now, uint32_t(t.index) * 31u + 7u);
    // the Fracture is red: a wash over the art, darker towards the rim
    float xs[6], ys[6];
    corners(cx, cy, r, xs, ys);
    c.fillPolygon(xs, ys, 6, rgb(150, 10, 24), t.kind == Rift ? 30 : 70);
    const float p = retro::on() ? std::max(1.f, r / (retro::kArtR * float(retro::artScale()))) : std::max(1.f, r / 90.f);  // one art pixel on screen (Voidfall: fine)
    switch (t.kind) {
        case Rift: {  // lightning tearing through the warp (white-gold, like the picture), in bursts
            const uint32_t per = 1700u + uint32_t(t.index) * 400u, ph = now % per;
            if (ph < 420) {
                const uint32_t slot = now / per;
                const int a = 255 - int(ph) * 200 / 420;
                const float a0 = float((slot * 7u + uint32_t(t.index)) % 6u) * kPi / 3.f + 0.3f;
                bolt(c, cx + std::cos(a0) * r * 0.8f, cy + std::sin(a0) * r * 0.65f, cx - std::cos(a0) * r * 0.55f,
                     cy - std::sin(a0) * r * 0.4f, slot * 3u + uint32_t(t.index), r * 0.18f, a, sc);
                c.fillCircle(cx, cy, r * 0.6f, rgb(255, 200, 230), a / 7);
            }
            const uint32_t slot2 = now / 380;  // and a faint crackle all the time
            bolt(c, cx - r * 0.6f, cy + r * 0.25f, cx + r * 0.1f, cy + r * 0.5f, slot2 * 5u + uint32_t(t.index), r * 0.1f, 90, sc * 0.7f);
            break;
        }
        case Egress: {  // the vortex: blocky dots in three arms, turning
            const float spin = float(now % 100000u) * 0.0026f;
            c.fillCircle(cx, cy, r * 0.5f, rgb(230, 40, 130), 45);  // its pink glow
            c.fillCircle(cx, cy, r * 0.28f, rgb(255, 90, 170), 60);
            for (int arm = 0; arm < 3; ++arm)
                for (int i = 0; i < 26; ++i) {
                    const float k = float(i) / 25.f;
                    const float a = spin + float(arm) * 2.f * kPi / 3.f + k * 3.4f;
                    const float rr = r * (0.06f + 0.66f * k);
                    const int q = std::max(2, int(p * (k < 0.5f ? 3.f : 2.f)));
                    c.fillRect(int(cx + std::cos(a) * rr) - q / 2, int(cy + std::sin(a) * rr * 0.85f) - q / 2, q, q,
                               mix(rgb(255, 170, 220), rgb(235, 30, 120), int(k * 255)), int(250 * (1.f - k * 0.7f)));
                }
            const int core = std::max(2, int(p * 4.f));
            c.fillRect(int(cx) - core / 2, int(cy) - core / 2, core, core, rgb(255, 240, 250), 230);
            break;
        }
        case Styx: {  // storms crackling round the legendary ocean
            const uint32_t slot = now / 500;
            bolt(c, cx - r * 0.85f, cy + r * 0.1f, cx - r * 0.38f, cy, slot * 7u + 1u, r * 0.08f, 200, sc);
            bolt(c, cx + r * 0.38f, cy + r * 0.05f, cx + r * 0.85f, cy + r * 0.2f, slot * 7u + 3u, r * 0.08f, 200, sc);
            break;
        }
        default: break;
    }
    // large: the overlays into the same chunky pixels as the art
    if (large) {
        const int f = int(r / retro::kArtR + 0.5f);
        if (f >= 2 && retro::on()) retro::chunky(c, Rect{int(cx - r), int(cy - r * 0.87f), int(2.f * r) + 1, int(1.74f * r) + 1}, f);
    }
    hexEdge(c, cx, cy, r, rgb(232, 44, 48), std::max(1.5f, 2.6f * sc), 230);
    if (!large) return;
    switch (t.kind) {
        case Cocytus: label(c, cx - r * 0.05f, cy + r * 0.32f, "COCYTUS", 3, 0, false, r); break;
        case Styx: label(c, cx, cy + r * 0.48f, "STYX", 4, 0, true, r); break;
        case LethePhlegethon:
            label(c, cx + r * 0.5f, cy - r * 0.42f, "LETHE", 0, 2, false, r);
            label(c, cx + r * 0.4f, cy + r * 0.22f, "PHLEGETHON", 1, 2, false, r);
            break;
        case Egress: {
            const int w = 90;
            c.fillRoundRect(int(cx) - w / 2, int(cy + r * 0.62f), w, 18, 5, rgb(10, 6, 12), 210);
            drawTextAligned(c, FONT_SMB, int(cx) - w / 2, int(cy + r * 0.62f) + 1, w, Align::Center, "EGRESS", rgb(255, 200, 230));
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------- Voidfall (0.43.1, owner)
// The Fracture is shattered space: each of its seven systems has its own sky of crimson
// shards with molten light running down the cracks (tools/gen_voidfall.py: frac1..7 and
// their crack masks; the egresses are white slits torn in it), and Thunder's Edge is a
// storm turning round a calm eye with the planet in it (the "te" sky). No anomaly or
// black-hole art: the voids are empty systems, not rifts.
// Which of fracture1..7 a placeholder becomes (Rules::fractureEnter): Cocytus 1, Styx 4, Lethe / Phlegethon 7, the
// egress and the void nearer the chain's start 2 and 3, the others 6 and 5 (either order of the chain).
int fracNoOf(const Tile& t) {
    switch (t.kind) {
        case Cocytus: return 1;
        case Styx: return 4;
        case LethePhlegethon: return 7;
        case Egress: return t.index < 3 ? 2 : 6;
        default: return t.index < 3 ? 3 : 5;
    }
}

// 0.44.3: in Retrowave the painting goes through retro::draw, which puts it into pixel art (retro.cpp paintedSky);
// Simplistic and Voidfall draw the painting itself
void drawPainted(Canvas& c, const retro::Tile& rt, float cx, float cy, float r, uint32_t now) {
    if (retro::on()) retro::draw(c, rt, cx, cy, r, now);
    else voidfall::draw(c, rt, cx, cy, r, now);
}

void paintVoidfall(Canvas& c, const Tile& t, float cx, float cy, float r, uint32_t now, bool large) {
    const float sc = r / 60.f;
    const float R = retro::kArtR;
    retro::Tile rt;
    rt.seed = 7u + uint32_t(t.index);
    auto planet = [&](const char* name, float x, float y, float pr) {
        retro::Piece p;
        p.kind = retro::Piece::Planet;
        p.id = planetByName(name);
        p.x = x;
        p.y = y;
        p.r = pr;
        if (p.id >= 0) rt.pieces.push_back(p);
    };
    if (t.kind == ThundersEdge) {  // the expedition side: the storm, the planet waiting in its eye
        rt.sky = "te";
        planet("Thunder's Edge", 0.f, 0.f, R * 0.2f);  // where it is when it comes into play (one planet: the middle)
        drawPainted(c, rt, cx, cy, r, now);
        for (int k = 0; k < 2; ++k) {  // lightning in the storm's arms, in bursts
            const uint32_t per = 1500u + uint32_t(k) * 900u, ph = (now + uint32_t(k) * 650u) % per;
            if (ph < 260) {
                const uint32_t slot = (now + uint32_t(k) * 650u) / per;
                const float a0 = float((slot * 5u + uint32_t(k) * 3u) % 6u) * kPi / 3.f + 0.5f;
                const int a = 255 - int(ph) * 255 / 260;
                bolt(c, cx + std::cos(a0) * r * 0.82f, cy + std::sin(a0) * r * 0.66f, cx + std::cos(a0 + 0.9f) * r * 0.46f,
                     cy + std::sin(a0 + 0.9f) * r * 0.4f, slot * 11u + uint32_t(k) * 17u, r * 0.08f, a, sc * 0.75f, true);
                c.fillCircle(cx + std::cos(a0 + 0.45f) * r * 0.62f, cy + std::sin(a0 + 0.45f) * r * 0.52f, r * 0.22f, rgb(190, 215, 255),
                             a / 7);
            }
        }
        hexEdge(c, cx, cy, r, rgb(220, 190, 110), std::max(1.5f, 2.2f * sc), 230);
        if (large) {
            const float pr = r * 0.2f;
            const int w = 150;
            c.fillRoundRect(int(cx) - w / 2, int(cy + pr * 1.9f), w, 18, 5, rgb(8, 12, 22), 220);
            drawTextAligned(c, FONT_SMB, int(cx) - w / 2, int(cy + pr * 1.9f) + 1, w, Align::Center, "THUNDER'S EDGE", rgb(230, 200, 120));
            drawTextAligned(c, FONT_XS, int(cx) - 120, int(cy + pr * 1.9f) + 22, 240, Align::Center, "Hidden in the storm",
                            rgb(196, 206, 226));
        }
        return;
    }
    // ---- the Fracture: the sky its system will have, its real planets where the board puts them
    char key[16];
    std::snprintf(key, sizeof key, "fracture%d", fracNoOf(t));
    rt.def = systemByKey(key);
    switch (t.kind) {
        case Cocytus: planet("Cocytus", 0.f, 0.f, R * 0.2f); break;
        case Styx: planet("Styx", 0.f, 0.f, R * 0.2f); break;
        case LethePhlegethon:
            planet("Lethe", -0.3f * R, -0.25f * R, R * 0.2f);
            planet("Phlegethon", 0.3f * R, 0.25f * R, R * 0.2f);
            break;
        default: break;
    }
    drawPainted(c, rt, cx, cy, r, now);
    hexEdge(c, cx, cy, r, rgb(232, 44, 48), std::max(1.5f, 2.2f * sc), 230);
    if (!large) return;
    switch (t.kind) {
        case Cocytus: label(c, cx, cy + r * 0.26f, "COCYTUS", 3, 0, false, r); break;
        case Styx: label(c, cx, cy + r * 0.26f, "STYX", 4, 0, true, r); break;
        case LethePhlegethon:
            label(c, cx + r * 0.24f, cy - r * 0.48f, "LETHE", 0, 2, false, r);
            label(c, cx - r * 0.26f, cy + r * 0.16f, "PHLEGETHON", 1, 2, false, r);
            break;
        case Egress: {
            const int w = 90;
            c.fillRoundRect(int(cx) - w / 2, int(cy + r * 0.62f), w, 18, 5, rgb(10, 6, 12), 210);
            drawTextAligned(c, FONT_SMB, int(cx) - w / 2, int(cy + r * 0.62f) + 1, w, Align::Center, "EGRESS", rgb(255, 220, 210));
            break;
        }
        default: break;
    }
}

}  // namespace

std::vector<Tile> place(const Game& g, const std::vector<Hex>& taken) {
    std::vector<Tile> out;
    if (g.systems.empty()) return out;
    // 0.80.0 (owner: "Unless the Thunder's Edge module is active, the Fracture and Thunder's Edge should remain hidden on the
    // game map"): nothing of them in other games (0.24.0 - 0.75.0 drew both beside every map as a preview)
    if (!(g.modules & kModThundersEdge)) return out;
    // owner 0.43.2: the planets at the ends again (0.28.0 to 0.43.1 had the egress points there - games started
    // then keep it: Setup::fixes < 3)
    static const Kind kOrderOld[7] = {Egress, Cocytus, Rift, Styx, Rift, LethePhlegethon, Egress};
    static const Kind kOrderNew[7] = {Cocytus, Egress, Rift, Styx, Rift, Egress, LethePhlegethon};
    const Kind* kOrder = (g.modules & kModThundersEdge) && g.fixes < 3 ? kOrderOld : kOrderNew;  // (only TE games place it for real)
    // Thunder's Edge games (0.34.0): Thunder's Edge where the engine will put it (tePlacement) until it comes into play.
    // 0.73.5 (owner: "When a game would start that uses Thunder's Edge, the Fracture is hidden"): the Fracture isn't shown
    // until it appears - then as real systems, with the ripple (scene_game); the map keeps its room (map_view hiddenFracture).
    if ((g.modules & kModThundersEdge) && !(g.fracturePos[3] == Hex{})) {
        (void)kOrder;
        if (g.teSystem < 0 && !(g.tePos == Hex{})) out.push_back({ThundersEdge, g.tePos, 7});
        return out;
    }
    int maxRing = 0;
    std::vector<Hex> board, homes;
    for (const SystemState& s : g.systems) {
        if (!s.offBoard) {
            board.push_back(s.pos);
            maxRing = std::max(maxRing, hexRingOf(s.pos));
        }
        if (s.home >= 0) homes.push_back(s.pos);
    }
    Hex nexus{9999, 9999};
    if (g.nexus >= 0) nexus = g.systems[size_t(g.nexus)].pos;
    auto isTaken = [&](Hex h) { return std::find(taken.begin(), taken.end(), h) != taken.end(); };
    auto minDist = [&](Hex h, const std::vector<Hex>& v) {
        int d = 99;
        for (const Hex& x : v) d = std::min(d, hexDistance(h, x));
        return d;
    };
    // the Fracture (0.27.0, owner's picture of the Thunder's Edge setup): a chain of seven
    // systems on three red tiles (2 + 3 + 2) that bends round a corner of the board, one
    // space out - Styx at the bend, an egress near each end, the planets at the ends:
    // Cocytus, egress, empty, Styx, empty, egress, Lethe / Phlegethon (0.28.0-0.43.1: egress points at the ends).
    auto pattern = [](int corner, int ring) {
        const Hex apex{hexDir(corner).q * ring, hexDir(corner).r * ring};
        std::vector<Hex> v(7);
        v[3] = apex;
        Hex a = apex, b = apex;
        for (int k = 1; k <= 3; ++k) {
            a = hexNeighbor(a, corner + 4);  // along one edge of the ring...
            b = hexNeighbor(b, corner + 2);  // ...and along the other
            v[size_t(3 - k)] = a;
            v[size_t(3 + k)] = b;
        }
        return v;
    };
    std::vector<Hex> frac;
    {
        int bestScore = -(1 << 30);
        for (int ring = maxRing + 2; ring <= maxRing + 5; ++ring)
            for (int corner = 0; corner < 6; ++corner) {
                const std::vector<Hex> pt = pattern(corner, ring);
                bool ok = true;
                int homeD = 99, nexD = 99;
                for (const Hex& h : pt) {
                    if (isTaken(h) || minDist(h, taken) < 2) ok = false;  // a space between it and the board / info tiles
                    homeD = std::min(homeD, minDist(h, homes));
                    nexD = std::min(nexD, hexDistance(h, nexus));
                }
                if (!ok || homeD < 2 || nexD < 2) continue;
                // as close to the board as it fits, away from the homes and the nexus; the top first
                const int sc = -(ring - maxRing) * 100 + std::min(homeD, 6) * 10 + std::min(nexD, 6) * 3 + (corner == 0 ? 2 : 0);
                if (sc > bestScore) {
                    bestScore = sc;
                    frac = pt;
                }
            }
    }
    // (0.44.1, owner: before the game started - during galaxy setup, before the engine has placed it - this still drew
    // the 0.28.0 order, the egress points at the ends)
    for (size_t i = 0; i < frac.size(); ++i) out.push_back({kOrder[i], frac[i], int(i)});
    // Thunder's Edge: one space off the board, far from the homes, the nexus and the Fracture
    {
        Hex best{};
        int bestScore = -(1 << 30);
        bool found = false;
        for (int ring = maxRing + 1; ring <= maxRing + 3; ++ring)
            for (Hex h : hexRing(ring)) {
                if (isTaken(h) || minDist(h, taken) < 2 || hexDistance(h, nexus) < 2) continue;  // off the board, with a gap
                bool clash = false;
                for (const Hex& f : frac)
                    if (hexDistance(h, f) < 2) clash = true;
                if (clash) continue;
                const int homeD = minDist(h, homes), fracD = frac.empty() ? 9 : minDist(h, frac);
                const int sc = -std::abs(minDist(h, taken) - 2) * 200 + std::min(homeD, 4) * 20 + std::min(hexDistance(h, nexus), 6) * 5 +
                               std::min(fracD, 8) * 2 - ring * 30;
                if (sc > bestScore) {
                    bestScore = sc;
                    best = h;
                    found = true;
                }
            }
        if (found) out.push_back({ThundersEdge, best, 7});
    }
    return out;
}

std::string name(const Tile& t) {
    switch (t.kind) {
        case Cocytus: return "Cocytus";
        case Egress: return "Egress";
        case Rift: return "The Fracture";
        case Styx: return "Styx";
        case LethePhlegethon: return "Lethe / Phlegethon";
        case ThundersEdge: return "Thunder's Edge";
    }
    return "";
}

std::string subtitle(const Game& g, const Tile& t) {
    const bool te = (g.modules & kModThundersEdge) != 0;
    if (t.kind == ThundersEdge) {
        if (!te) return "Thunder's Edge \xC2\xB7 not in this game (the Thunder's Edge module is off)";
        int n = 0;
        for (int8_t q : g.expedition) n += q >= 0 ? 1 : 0;
        return "Thunder's Edge \xC2\xB7 expedition side, face down \xC2\xB7 " + std::to_string(n) + " of 6 slices claimed";
    }
    if (!te) return "The Fracture \xC2\xB7 not in this game (the Thunder's Edge module is off)";
    return g.fractureIn ? "The Fracture \xC2\xB7 ACTIVE: in play" : "The Fracture \xC2\xB7 INACTIVE: not in play yet";
}

std::string about(const Game& g, const Tile& t) {
    const bool te = (g.modules & kModThundersEdge) != 0;
    if (t.kind == ThundersEdge) {
        if (!te) return "Tick Thunder's Edge under Modules (title screen) to play with it.";
        return "Join the expedition at the end of your turn by paying for one of its six slices; your first slice gains your "
               "breakthrough. Whoever claims the sixth slice turns the tile over and places the Thunder's Edge planet.";
    }
    if (!te) return "Seven systems that enter play during a Thunder's Edge game. Their planets give relics.";
    return "Its seven systems join the board when a player gains a breakthrough and rolls a 1 or a 10 (the Vuil'raith's "
           "breakthrough brings it at once). Then ships reach it through the ingress and egress points; its planets give relics.";
}

void paint(Canvas& c, const Tile& t, float cx, float cy, float r, uint32_t now, bool large) {
    // 0.43.1 (owner): Voidfall's own Fracture and Thunder's Edge - 0.44.1: in every tileset (the drawn ones below only
    // when the Voidfall art is missing)
    // 0.44.3 (owner): in Retrowave, Thunder's Edge in its storm clouds of 0.28.1 again (the Fracture keeps the new design)
    // 0.46.2 (owner): Simplistic draws its own again (the drawn ones below)
    if ((retro::hd() || (retro::on() && voidfall::available())) && !(retro::on() && t.kind == ThundersEdge)) {
        paintVoidfall(c, t, cx, cy, r, now, large);
        return;
    }
    if (retro::on()) {
        paintRetro(c, t, cx, cy, r, now, large);
        return;
    }
    const float sc = r / 60.f;  // line widths follow the size
    if (t.kind == ThundersEdge) {
        // six slices round a storm: the expedition side
        float xs[6], ys[6];
        corners(cx, cy, r, xs, ys);
        hexFill(c, cx, cy, r, rgb(12, 16, 28));
        for (int i = 0; i < 6; ++i) {
            float tx[3] = {cx, xs[i], xs[(i + 1) % 6]}, ty[3] = {cy, ys[i], ys[(i + 1) % 6]};
            c.fillPolygon(tx, ty, 3, i % 2 ? rgb(30, 36, 58) : rgb(22, 26, 44));
            c.line(cx, cy, xs[i], ys[i], rgb(190, 160, 90), std::max(1.f, 1.5f * sc), 200);
        }
        // the storm in the middle
        const float pr = r * 0.3f;
        c.fillCircle(cx, cy, pr * 1.35f, rgb(120, 140, 255), 30);
        planetBody(c, cx, cy, pr, rgb(48, 56, 100), 77u, 4);
        const float t = float(now % 6000) / 6000.f;
        for (int k = 0; k < 3; ++k)  // clouds going round
            for (int i = 0; i < 8; ++i) {
                const float a = t * 2 * kPi * (k % 2 ? -1.f : 1.f) + float(i) * 0.785f + float(k);
                c.fillCircle(cx + std::cos(a) * pr * (0.35f + 0.22f * float(k)), cy + std::sin(a) * pr * (0.2f + 0.15f * float(k)),
                             pr * 0.13f, rgb(170, 180, 220), 70);
            }
        const uint32_t phase = now % 1700;
        if (phase < 160) {  // thunder
            const int a = 255 - int(phase) * 255 / 160;
            bolt(c, cx - pr * 0.6f, cy - pr * 0.5f, cx + pr * 0.3f, cy + pr * 0.6f, now / 1700, pr * 0.25f, a, sc);
            c.fillCircle(cx, cy, pr * 1.1f, rgb(200, 210, 255), a / 5);
        }
        c.ring(cx, cy, pr * 1.18f, std::max(1.5f, 2.f * sc), rgb(220, 190, 110), 220);
        hexEdge(c, cx, cy, r, rgb(220, 190, 110), std::max(1.5f, 2.5f * sc), 230);
        if (large && false) {  // (0.43.0: the winglets round the tile show the six slices now - drawWinglets)
            static const char* what[6] = {"5 resources", "2 action cards", "5 influence", "1 secret objective", "1 specialty planet", "3 trade goods"};
            for (int i = 0; i < 6; ++i) {
                const float a = kPi / 3.f * float(i) + kPi / 6.f;
                const float wx = cx + std::cos(a) * r * 0.62f, wy = cy + std::sin(a) * r * 0.62f;
                const int is = 22;
                switch (i) {
                    case 0: drawResourceIcon(c, int(wx), int(wy) - 8, is, 5, FONT_SMB, false); break;
                    case 1: icons::draw(c, "action_card", int(wx) - is / 2, int(wy) - 8 - is / 2, is); break;
                    case 2: drawResourceIcon(c, int(wx), int(wy) - 8, is, 5, FONT_SMB, true); break;
                    case 3: icons::draw(c, "secret", int(wx) - is / 2, int(wy) - 8 - is / 2, is); break;
                    case 4: drawSkipIcon(c, TechColor::Blue, int(wx) - is / 2, int(wy) - 8 - is / 2, is); break;
                    default: icons::draw(c, "trade_goods", int(wx) - is / 2, int(wy) - 8 - is / 2, is); break;
                }
                drawTextAligned(c, FONT_XS, int(wx) - 50, int(wy) + 6, 100, Align::Center, what[i], rgb(220, 210, 180));
            }
            const int w = 150;
            c.fillRoundRect(int(cx) - w / 2, int(cy + pr * 1.3f), w, 18, 5, rgb(10, 12, 20), 220);
            drawTextAligned(c, FONT_SMB, int(cx) - w / 2, int(cy + pr * 1.3f) + 1, w, Align::Center, "THUNDER'S EDGE", rgb(230, 200, 120));
        }
        return;
    }
    // ---- The Fracture
    nebula(c, cx, cy, r, uint32_t(t.index) * 31u + 5u, now);
    switch (t.kind) {
        case Rift: {  // pink lightning across the empty systems
            const uint32_t slot = now / 380;
            const int flick = int(160 + 95 * std::fabs(std::sin(float(now % 900) / 900.f * kPi * 3.f)));
            const float dir = t.index == 2 ? 1.f : -1.f;
            bolt(c, cx - dir * r * 0.75f, cy - r * 0.2f, cx + dir * r * 0.7f, cy + r * 0.15f, slot * 3u + uint32_t(t.index), r * 0.16f, flick, sc);
            bolt(c, cx - dir * r * 0.2f, cy + r * 0.55f, cx + dir * r * 0.35f, cy - r * 0.5f, slot * 5u + 7u + uint32_t(t.index), r * 0.12f,
                 flick * 2 / 3, sc * 0.8f);
            break;
        }
        case Egress: {  // the vortex the Fracture is left by
            const float spin = float(now % 100000u) * 0.0026f;
            for (int arm = 0; arm < 3; ++arm)
                for (int i = 0; i < 16; ++i) {
                    const float k = float(i) / 15.f;
                    const float a = spin + float(arm) * 2.f * kPi / 3.f + k * 3.2f;
                    const float rr = r * (0.08f + 0.6f * k);
                    c.fillCircle(cx + std::cos(a) * rr, cy + std::sin(a) * rr * 0.85f, std::max(1.f, r * (0.06f * (1.f - k) + 0.015f)),
                                 mix(rgb(255, 235, 245), rgb(200, 40, 100), int(k * 255)), int(230 * (1.f - k) + 25));
                }
            c.fillCircle(cx, cy, r * 0.12f, rgb(255, 240, 250), 220);
            c.ring(cx, cy, r * 0.2f, std::max(1.f, 1.5f * sc), rgb(255, 255, 255), 200);
            bolt(c, cx + r * 0.15f, cy, cx - r * 0.8f, cy + r * 0.35f, now / 450 + uint32_t(t.index), r * 0.1f, 150, sc * 0.7f);
            break;
        }
        case Cocytus:
            planetBody(c, cx - r * 0.05f, cy - r * 0.05f, r * 0.34f, rgb(206, 210, 222), 11u, 0);
            break;
        case Styx: {
            const float pr = r * 0.44f;
            c.fillCircle(cx, cy, pr * 1.25f, rgb(80, 140, 255), int(30 + 20 * std::sin(float(now % 2000) / 2000.f * 2 * kPi)));
            planetBody(c, cx, cy, pr, rgb(22, 44, 90), 23u, 1);
            const uint32_t slot = now / 500;
            bolt(c, cx - r * 0.85f, cy + r * 0.1f, cx - pr * 0.8f, cy, slot * 7u + 1u, r * 0.08f, 200, sc);
            bolt(c, cx + pr * 0.8f, cy + r * 0.05f, cx + r * 0.85f, cy + r * 0.2f, slot * 7u + 3u, r * 0.08f, 200, sc);
            break;
        }
        case LethePhlegethon:
            planetBody(c, cx + r * 0.12f, cy - r * 0.3f, r * 0.27f, rgb(220, 90, 30), 31u, 2);
            planetBody(c, cx - r * 0.02f, cy + r * 0.32f, r * 0.24f, rgb(190, 194, 206), 37u, 3);
            break;
        default: break;
    }
    hexEdge(c, cx, cy, r, rgb(232, 44, 48), std::max(1.5f, 2.6f * sc), 230);
    if (!large) return;
    switch (t.kind) {
        case Cocytus: label(c, cx - r * 0.05f, cy + r * 0.32f, "COCYTUS", 3, 0, false, r); break;
        case Styx: label(c, cx, cy + r * 0.48f, "STYX", 4, 0, true, r); break;
        case LethePhlegethon:
            label(c, cx + r * 0.5f, cy - r * 0.42f, "LETHE", 0, 2, false, r);
            label(c, cx + r * 0.4f, cy + r * 0.22f, "PHLEGETHON", 1, 2, false, r);
            break;
        case Egress: {
            const int w = 90;
            c.fillRoundRect(int(cx) - w / 2, int(cy + r * 0.62f), w, 18, 5, rgb(10, 6, 12), 210);
            drawTextAligned(c, FONT_SMB, int(cx) - w / 2, int(cy + r * 0.62f) + 1, w, Align::Center, "EGRESS", rgb(255, 200, 230));
            break;
        }
        default: break;
    }
}

void drawWinglets(Canvas& c, float cx, float cy, float r, const uint32_t claimCol[6], uint32_t now, int selected, const int* claimFaction,
                  const bool* open, float* wxOut, float* wyOut) {
    static const char* what[6] = {"5 resources", "5 influence", "2 action cards", "1 secret", "1 specialty", "3 trade goods"};
    float xs[6], ys[6];
    corners(cx, cy, r, xs, ys);
    const float depth = r * 0.34f;
    const float pulse = 0.5f + 0.5f * std::sin(float(now % 2400) / 2400.f * 2.f * kPi);
    for (int k = 0; k < 6; ++k) {
        const int i = (k + 4) % 6;  // slice 0 on the top edge, then clockwise
        const float nx = std::cos(kPi / 3.f * float(i) + kPi / 6.f), ny = std::sin(kPi / 3.f * float(i) + kPi / 6.f);
        const float ax = xs[i] + nx * 2.f, ay = ys[i] + ny * 2.f, bx = xs[(i + 1) % 6] + nx * 2.f, by = ys[(i + 1) % 6] + ny * 2.f;
        const float mx = (ax + bx) / 2.f, my = (ay + by) / 2.f;
        const float ox = nx * depth, oy = ny * depth;
        float px[4] = {ax, bx, mx + (bx - mx) * 0.62f + ox, mx + (ax - mx) * 0.62f + ox};
        float py[4] = {ay, by, my + (by - my) * 0.62f + oy, my + (ay - my) * 0.62f + oy};
        const bool taken = claimCol[k] != 0;
        const bool closed = open && !open[k] && !taken;  // (the menu: a slice this player can't take now)
        const bool sel = selected == k;
        if (sel) {  // the chosen one stands out a little, glowing
            for (int e = 0; e < 4; ++e) {
                px[e] += nx * depth * 0.12f;
                py[e] += ny * depth * 0.12f;
            }
        }
        c.fillPolygon(px, py, 4, taken ? shade(claimCol[k], 0.45f) : sel ? rgb(46, 40, 22) : rgb(16, 20, 34), closed ? 140 : 235);
        for (int e = 0; e < 4; ++e)
            c.line(px[e], py[e], px[(e + 1) % 4], py[(e + 1) % 4],
                   taken ? claimCol[k] : sel ? mix(rgb(255, 220, 120), rgb(255, 255, 230), int(pulse * 255))
                                         : mix(rgb(150, 125, 70), rgb(230, 200, 120), int(pulse * 255)),
                   sel ? 3.f : 2.f, closed ? 110 : 230);
        // the icon and the cost, in the middle of the tab
        const float wx = mx + ox * 0.5f + (sel ? nx * depth * 0.12f : 0.f), wy = my + oy * 0.5f + (sel ? ny * depth * 0.12f : 0.f);
        if (wxOut) wxOut[k] = wx;
        if (wyOut) wyOut[k] = wy;
        if (taken && claimFaction && claimFaction[k] >= 0) {  // the joiner's control token on it
            cards::controlToken(c, claimFaction[k], claimCol[k], int(wx), int(wy) - 2, std::max(12, int(depth * 0.55f)));
            continue;
        }
        const int is = std::max(10, int(depth * 0.42f));
        switch (k) {
            case 0: drawResourceIcon(c, int(wx), int(wy) - is / 3, is, 5, FONT_SMB, false); break;
            case 1: drawResourceIcon(c, int(wx), int(wy) - is / 3, is, 5, FONT_SMB, true); break;
            case 2: icons::draw(c, "action_card", int(wx) - is / 2, int(wy) - is / 3 - is / 2, is); break;
            case 3: icons::draw(c, "secret", int(wx) - is / 2, int(wy) - is / 3 - is / 2, is); break;
            case 4: drawSkipIcon(c, TechColor::Blue, int(wx) - is / 2, int(wy) - is / 3 - is / 2, is); break;
            default: icons::draw(c, "trade_good", int(wx) - is / 2, int(wy) - is / 3 - is / 2, is); break;
        }
        const int tw = int(r * 0.62f);  // (inside the tab, also on the slanted ones)
        drawTextAligned(c, FONT_XS, int(wx) - tw / 2, int(wy) + is / 3 + 1, tw, Align::Center, fitText(FONT_XS, taken ? "joined" : what[k], tw),
                        taken ? rgb(235, 235, 235) : rgb(220, 210, 180));
    }
}

// A real Fracture system (in play, Thunder's Edge 0.34.0): fracNo = 1..7 (its tile, fracture1..7). under: drawn
// before the planets (Simplistic: the red nebula as the tile's ground); otherwise over the art tile (a red wash).
// Then the lightning of the empty systems, the egress vortex and the red rim.
void fractureLook(Canvas& c, int fracNo, float cx, float cy, float r, uint32_t now, bool under) {
    static const Kind kinds[7] = {Cocytus, Egress, Rift, Styx, Rift, Egress, LethePhlegethon};
    const Kind kind = kinds[std::max(0, std::min(6, fracNo - 1))];
    const int index = fracNo - 1;
    const float sc = r / 60.f;
    if (!under && (retro::hd() || voidfall::available())) {  // 0.43.1 (owner): Voidfall's cracked skies are the look - only the red rim on top
        hexEdge(c, cx, cy, r, rgb(232, 44, 48), std::max(1.5f, 2.2f * sc), 220);
        return;
    }
    if (under) {
        nebula(c, cx, cy, r, uint32_t(index) * 31u + 5u, now);
    } else {
        if (kind == Rift) warpStorm(c, cx, cy, r, now, uint32_t(index) * 31u + 7u);  // 0.43.0 (P13): the warp in the art tilesets
        float xs[6], ys[6];
        corners(cx, cy, r, xs, ys);
        c.fillPolygon(xs, ys, 6, rgb(150, 10, 24), kind == Rift ? 40 : 60);
    }
    if (kind == Rift) {
        const uint32_t slot = now / 380;
        const int flick = int(160 + 95 * std::fabs(std::sin(float(now % 900) / 900.f * kPi * 3.f)));
        const float dir = index == 2 ? 1.f : -1.f;
        bolt(c, cx - dir * r * 0.75f, cy - r * 0.2f, cx + dir * r * 0.7f, cy + r * 0.15f, slot * 3u + uint32_t(index), r * 0.16f, flick, sc);
        bolt(c, cx - dir * r * 0.2f, cy + r * 0.55f, cx + dir * r * 0.35f, cy - r * 0.5f, slot * 5u + 7u + uint32_t(index), r * 0.12f,
             flick * 2 / 3, sc * 0.8f);
    } else if (kind == Egress) {
        const float spin = float(now % 100000u) * 0.0026f;
        for (int arm = 0; arm < 3; ++arm)
            for (int i = 0; i < 16; ++i) {
                const float k = float(i) / 15.f;
                const float a = spin + float(arm) * 2.f * kPi / 3.f + k * 3.2f;
                const float rr = r * (0.08f + 0.5f * k);
                c.fillCircle(cx + std::cos(a) * rr, cy + std::sin(a) * rr * 0.85f, std::max(1.f, r * (0.05f * (1.f - k) + 0.012f)),
                             mix(rgb(255, 235, 245), rgb(200, 40, 100), int(k * 255)), int(200 * (1.f - k) + 25));
            }
        c.fillCircle(cx, cy, r * 0.1f, rgb(255, 240, 250), 200);
    } else if (kind == Styx && under) {
        c.fillCircle(cx, cy, r * 0.55f, rgb(80, 140, 255), int(26 + 18 * std::sin(float(now % 2000) / 2000.f * 2 * kPi)));
    }
    hexEdge(c, cx, cy, r, rgb(232, 44, 48), std::max(1.5f, 2.2f * sc), 220);
}

void drawIngress(Canvas& c, float x, float y, float r, uint32_t now) {
    const float t = float(now % 600000u) / 1000.f;
    const float pulse = 0.5f + 0.5f * std::sin(t * 4.f);
    const uint32_t white = rgb(255, 238, 238), red = rgb(222, 32, 44);
    c.fillCircle(x, y, r * 1.05f, rgb(26, 4, 8), 210);
    if (r >= 5.f) {  // three arms winding in, white at the eye, red at the rim
        for (int a = 0; a < 3; ++a)
            for (int i = 0; i < 16; ++i) {
                const float u = float(i) / 15.f;
                const float ang = -t * 2.4f + float(a) * 2.f * kPi / 3.f + u * 3.4f;
                const float rr = r * (0.12f + 0.86f * u);
                c.fillCircle(x + rr * std::cos(ang), y + rr * std::sin(ang), std::max(0.8f, r * 0.12f * (1.f - 0.45f * u)),
                             mix(white, red, int(255.f * u)), uint8_t(110 + int(130.f * pulse)));
            }
    }
    c.fillCircle(x, y, std::max(1.f, r * (0.16f + 0.08f * pulse)), white, 240);
    c.ring(x, y, r, std::max(1.f, r * 0.13f), mix(red, white, int(200.f * pulse)), 235);
    // the arrow above, bobbing down into the vortex
    const float aw = r * 0.62f, top = y - r * (2.05f - 0.3f * pulse), tip = y - r * (1.05f - 0.25f * pulse);
    const float sh = aw * 0.42f;
    float xs[7] = {x - sh, x + sh, x + sh, x + aw, x, x - aw, x - sh};
    float ys[7] = {top, top, tip - aw * 0.9f, tip - aw * 0.9f, tip, tip - aw * 0.9f, tip - aw * 0.9f};
    float xo[7], yo[7];
    for (int i = 0; i < 7; ++i) {  // a dark outline so it reads on any tile
        xo[i] = x + (xs[i] - x) * 1.25f;
        yo[i] = (tip - aw * 0.45f) + (ys[i] - (tip - aw * 0.45f)) * 1.18f;
    }
    c.fillPolygon(xo, yo, 7, rgb(20, 2, 6), 200);
    c.fillPolygon(xs, ys, 7, mix(red, white, int(255.f * pulse)), 245);
}

}  // namespace decor
