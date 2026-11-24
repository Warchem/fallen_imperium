#include "ui/superweapon_ui.h"
#include "ui/pharadn_ui.h"
#include "ui/tile_view.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "engine/query.h"
#include "gfx/font.h"
#include "ui/cards.h"
#include "ui/decor_tiles.h"
#include "ui/icons.h"
#include "ui/pixel_ships.h"
#include "ui/retro.h"
#include "ui/voidfall.h"
#include "ui/theme.h"
#include "ui/pcolors.h"

using namespace ti;

void drawBreachMark(Canvas& c, float cx, float cy, float r, bool active, uint32_t now);  // map_view.cpp
void drawEventMarks(Canvas& c, const Game& g, int sys, float cx, float cy, float r);       // map_view.cpp

namespace tileview {

namespace {

constexpr float kPi = 3.14159265f;

uint32_t lighter(uint32_t c, float k) { return mix(c, rgb(255, 255, 255), int(255.f * k)); }

uint32_t pcol(const Game& g, int p) {
    return p == kNeutral ? theme::neutral : p >= 0 && p < g.numPlayers ? pcolours::base(g.players[size_t(p)].color) : theme::text;
}

uint32_t traitCol(Trait t) {
    switch (t) {
        case Trait::Cultural: return theme::cultural;
        case Trait::Hazardous: return theme::hazardous;
        case Trait::Industrial: return theme::industrial;
        default: return rgb(180, 170, 150);
    }
}

struct Rnd {  // a small deterministic generator for the decorations
    uint32_t s;
    explicit Rnd(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    float next(float lo, float hi) {
        s = s * 1664525u + 1013904223u;
        return lo + (hi - lo) * float((s >> 8) % 10000) / 10000.f;
    }
};

void hexPoints(float cx, float cy, float r, float* xs, float* ys) {
    for (int i = 0; i < 6; ++i) {
        const float a = kPi / 3.f * float(i);
        xs[i] = cx + r * std::cos(a);
        ys[i] = cy + r * std::sin(a);
    }
}

void poly(Canvas& c, const float (*pts)[2], int n, float cx, float cy, float sx, float sy, bool flip, uint32_t col, int alpha) {
    float xs[16], ys[16];
    for (int i = 0; i < n && i < 16; ++i) {
        xs[i] = cx + (flip ? -pts[i][0] : pts[i][0]) * sx;
        ys[i] = cy + pts[i][1] * sy;
    }
    c.fillPolygon(xs, ys, n, col, alpha);
}

// ------------------------------------------------------------------ backgrounds
void drawBackground(Canvas& c, const SystemDef& d, int home, uint32_t homeCol, float cx, float cy, float R, uint32_t now, int def) {
    uint32_t fill = rgb(16, 22, 40);
    if (home >= 0) fill = rgb(22, 30, 54);
    if (!std::strcmp(d.key, "18")) fill = rgb(40, 34, 20);
    if (d.anomalies & kNebula) fill = rgb(40, 22, 58);
    else if (d.anomalies & kSupernova) fill = rgb(58, 20, 12);
    else if (d.anomalies & kRift) fill = rgb(10, 8, 20);
    else if (d.anomalies & kAsteroid) fill = rgb(30, 29, 32);
    else if (d.anomalies & kScar) fill = rgb(32, 10, 20);
    float xs[6], ys[6];
    hexPoints(cx, cy, R + 3.f, xs, ys);
    c.fillPolygon(xs, ys, 6, home >= 0 ? shade(homeCol, 0.8f) : shade(fill, 2.4f));
    hexPoints(cx, cy, R, xs, ys);
    c.fillPolygon(xs, ys, 6, fill);
    // stars
    Rnd r(uint32_t(def) + 7u);
    for (int i = 0; i < int(R * 0.6f); ++i) {
        const float a = r.next(0, 2 * kPi), d2 = std::sqrt(r.next(0, 1)) * R * 0.84f;
        const float x = cx + std::cos(a) * d2, y = cy + std::sin(a) * d2;
        const int b = int(r.next(60, 200));
        c.pixel(int(x), int(y), rgb(uint8_t(b), uint8_t(b), uint8_t(std::min(255, b + 30))), int(r.next(120, 255)));
    }
    const float t = float(now % 12000) / 12000.f;
    if (d.anomalies & kNebula) {
        Rnd n(uint32_t(def) * 3u + 1u);
        for (int i = 0; i < 9; ++i) {
            const float a = n.next(0, 2 * kPi), r2 = n.next(0, 0.55f) * R;
            const float drift = std::sin(t * 2 * kPi + float(i)) * R * 0.02f;
            c.fillCircle(cx + std::cos(a) * r2 + drift, cy + std::sin(a) * r2, n.next(0.22f, 0.42f) * R, rgb(150, 80, 210), 26);
        }
        for (int i = 0; i < 5; ++i) {
            const float a = n.next(0, 2 * kPi), r2 = n.next(0, 0.5f) * R;
            c.fillCircle(cx + std::cos(a) * r2, cy + std::sin(a) * r2, n.next(0.1f, 0.2f) * R, rgb(220, 140, 240), 22);
        }
    }
    const bool zelianRocks = !std::strcmp(d.key, "d36");  // 0.43.0 (owner D44): a cracked world's core and its rubble ring
    if (zelianRocks) {
        const float glow = 0.8f + 0.2f * std::sin(t * 2 * kPi * 1.5f);
        c.fillCircle(cx, cy, R * 0.3f, rgb(110, 34, 18), 90);
        c.fillCircle(cx, cy, R * 0.16f * glow, rgb(240, 120, 50), 200);
        c.fillCircle(cx, cy, R * 0.07f, rgb(255, 220, 160));
    }
    if (d.anomalies & kAsteroid) {
        Rnd a(uint32_t(def) * 5u + 3u);
        for (int i = 0; i < 46; ++i) {
            const float ang = a.next(0, 2 * kPi), r2 = zelianRocks ? R * a.next(0.34f, 0.8f) : std::sqrt(a.next(0, 1)) * R * 0.8f;
            const float x = cx + std::cos(ang) * r2, y = cy + std::sin(ang) * r2, s = a.next(0.018f, 0.05f) * R;
            float px[7], py[7];
            for (int k = 0; k < 7; ++k) {
                const float aa = 2 * kPi * float(k) / 7.f, rr = s * a.next(0.7f, 1.25f);
                px[k] = x + std::cos(aa) * rr;
                py[k] = y + std::sin(aa) * rr;
            }
            c.fillPolygon(px, py, 7, rgb(110, 100, 86));
            c.fillCircle(x - s * 0.3f, y - s * 0.3f, s * 0.45f, rgb(160, 150, 130), 140);
        }
    }
    if (d.anomalies & kSupernova) {
        const float pulse = 0.9f + 0.1f * std::sin(t * 2 * kPi * 3.f);
        c.fillCircle(cx, cy, R * 0.72f * pulse, rgb(200, 60, 20), 40);
        c.fillCircle(cx, cy, R * 0.5f * pulse, rgb(240, 110, 30), 110);
        c.fillCircle(cx, cy, R * 0.32f, rgb(255, 190, 90), 220);
        c.fillCircle(cx, cy, R * 0.18f, rgb(255, 245, 210));
        if (!std::strcmp(d.key, "81"))  // the Muaat's Nova Seed (0.43.0): a crown of flares turning round it
            for (int i = 0; i < 12; ++i) {
                const float a = float(i) / 12.f * 2.f * kPi + t * 2 * kPi * 0.2f;
                c.fillCircle(cx + std::cos(a) * R * 0.58f, cy + std::sin(a) * R * 0.58f, R * (0.05f + 0.02f * std::sin(t * 2 * kPi * 3.f + float(i))),
                             rgb(255, 170, 60), 210);
            }
    }
    if (d.anomalies & kRift) {
        for (int i = 0; i < 6; ++i) c.ring(cx, cy, R * (0.12f + 0.11f * float(i)), 2.f, rgb(uint8_t(80 + 25 * i), 60, 200), 210 - 30 * i);
        for (int i = 0; i < 24; ++i) {  // matter spiralling in
            const float k = float(i) / 24.f, a = k * 6 * kPi - t * 2 * kPi * 2.f, r2 = R * (0.72f - 0.6f * k);
            c.fillCircle(cx + std::cos(a) * r2, cy + std::sin(a) * r2, 1.5f + 2.f * (1 - k), rgb(200, 170, 255), 200);
        }
        c.fillCircle(cx, cy, R * 0.09f, rgb(0, 0, 0));
    }
    if (d.anomalies & kScar) {  // Thunder's Edge: the entropic scar - a torn, glowing crack that breathes
        Rnd sc(uint32_t(def) * 7u + 9u);
        const float breathe = 0.75f + 0.25f * std::sin(t * 2 * kPi * 2.f);
        c.fillCircle(cx, cy, R * 0.7f, rgb(120, 16, 40), 40);
        float px[13], py[13];
        for (int i = 0; i < 13; ++i) {
            const float k = float(i) / 12.f;
            px[i] = cx + (k - 0.5f) * R * 1.5f;
            py[i] = cy + (k - 0.5f) * R * 0.55f + (i % 2 ? 1.f : -1.f) * sc.next(0.03f, 0.11f) * R;
        }
        for (int i = 0; i < 12; ++i) c.line(px[i], py[i], px[i + 1], py[i + 1], rgb(200, 30, 60), R * 0.13f, int(60 * breathe));
        for (int i = 0; i < 12; ++i) c.line(px[i], py[i], px[i + 1], py[i + 1], rgb(255, 90, 90), R * 0.045f, int(200 * breathe));
        for (int i = 0; i < 12; ++i) c.line(px[i], py[i], px[i + 1], py[i + 1], rgb(255, 230, 210), std::max(1.f, R * 0.012f), 230);
        for (int i = 1; i < 12; i += 2) {  // side cracks
            const float a = sc.next(0.f, 2 * kPi), l = sc.next(0.07f, 0.2f) * R;
            c.line(px[i], py[i], px[i] + std::cos(a) * l, py[i] + std::sin(a) * l, rgb(255, 100, 100), std::max(1.f, R * 0.015f), 170);
        }
        for (int i = 0; i < 16; ++i) {  // motes falling into the crack
            const float k = std::fmod(sc.next(0, 1) + t * 2.f, 1.f);
            const int j = int(sc.next(0, 12));
            const float x = px[j] + sc.next(-0.1f, 0.1f) * R, y = py[j] - (1.f - k) * R * 0.3f;
            c.fillCircle(x, y, 1.2f + 1.2f * k, rgb(255, 150, 140), int(200 * k));
        }
    }
}

void drawWormhole(Canvas& c, uint8_t bit, float x, float y, float r, uint32_t now, bool token) {
    const uint32_t wc = bit == kAlpha ? rgb(240, 150, 60) : bit == kBeta ? rgb(90, 200, 110) : bit == kGamma ? rgb(230, 90, 200)
                      : bit == kDelta ? rgb(200, 120, 230) : rgb(235, 60, 70);
    const char* sym = bit == kAlpha ? "\xCE\xB1" : bit == kBeta ? "\xCE\xB2" : bit == kGamma ? "\xCE\xB3" : bit == kDelta ? "\xCE\xB4" : "\xCE\xB5";
    c.fillCircle(x, y, r * 1.3f, wc, 30);
    c.fillCircle(x, y, r, shade(wc, 0.18f));
    const float t = float(now % 6000) / 6000.f;
    for (int i = 0; i < 18; ++i) {
        const float k = float(i) / 18.f, a = k * 4 * kPi + t * 2 * kPi, rr = r * (0.9f - 0.75f * k);
        c.fillCircle(x + std::cos(a) * rr, y + std::sin(a) * rr, 1.2f + 1.5f * (1 - k), wc, 200);
    }
    c.ring(x, y, r, std::max(2.f, r * 0.12f), wc);
    if (token) c.ring(x, y, r * 1.18f, 1.f, rgb(230, 230, 255), 170);
    const FontData& f = r >= 22 ? FONT_LG : FONT_MDB;
    drawTextAligned(c, f, int(x - r), int(y) - f.lineHeight / 2 - 1, int(2 * r), Align::Center, sym, lighter(wc, 0.3f));
}

// The Wormhole Nexus (0.24.0, owner: "a drawn-in version of the tile"): the
// saw-toothed tile, a galaxy swirling into a dark heart, Mallice beside it.
void drawNexusWarp(Canvas& c, float cx, float cy, float R, uint32_t now, bool active) {
    // the tile's outline: six blades, each a long slant to the next tip
    float xs[12], ys[12];
    for (int i = 0; i < 6; ++i) {
        const float a = kPi / 3.f * float(i) - kPi / 2.f;
        xs[2 * i] = cx + std::cos(a) * R * 0.99f;
        ys[2 * i] = cy + std::sin(a) * R * 0.99f;
        xs[2 * i + 1] = cx + std::cos(a + 0.42f) * R * 0.8f;
        ys[2 * i + 1] = cy + std::sin(a + 0.42f) * R * 0.8f;
    }
    float ox[12], oy[12];
    for (int i = 0; i < 12; ++i) {
        ox[i] = cx + (xs[i] - cx) * 1.03f;
        oy[i] = cy + (ys[i] - cy) * 1.03f;
    }
    c.fillPolygon(ox, oy, 12, rgb(150, 140, 170));
    c.fillPolygon(xs, ys, 12, rgb(14, 8, 26));
    const float t = float(now % 200000u) / 1000.f * (active ? 1.f : 0.55f);
    // nebula washes going round
    static const uint32_t wash[6] = {rgb(240, 120, 40), rgb(200, 40, 60), rgb(150, 50, 170), rgb(60, 90, 210), rgb(230, 80, 120), rgb(90, 50, 180)};
    for (int i = 0; i < 6; ++i) {
        const float a = float(i) * kPi / 3.f - t * 0.12f;
        c.fillCircle(cx + std::cos(a) * R * 0.38f, cy + std::sin(a) * R * 0.38f, R * 0.36f, wash[i], active ? 46 : 30);
    }
    // the spiral arms, from a white-hot middle out through orange and red to violet and blue
    const int arms = 4, dots = 56;
    for (int arm = 0; arm < arms; ++arm)
        for (int i = 0; i < dots; ++i) {
            const float k = float(i) / float(dots - 1);
            const float rr = R * (0.1f + 0.68f * k);
            const float a = float(arm) * kPi / 2.f + k * 4.2f - t * 0.6f;
            uint32_t col = k < 0.25f ? mix(rgb(255, 250, 220), rgb(255, 170, 70), int(k / 0.25f * 255))
                           : k < 0.6f ? mix(rgb(255, 170, 70), rgb(220, 50, 90), int((k - 0.25f) / 0.35f * 255))
                                      : mix(rgb(220, 50, 90), rgb(80, 110, 230), int((k - 0.6f) / 0.4f * 255));
            const float dr = std::max(1.2f, R * (0.024f * (1.f - k) + 0.006f));
            c.fillCircle(cx + std::cos(a) * rr, cy + std::sin(a) * rr, dr, col, int((active ? 220 : 160) * (1.f - k * 0.7f)));
            if (i % 2 == 0)  // a fainter trailing dust
                c.fillCircle(cx + std::cos(a - 0.12f) * rr * 1.05f, cy + std::sin(a - 0.12f) * rr * 1.05f, dr * 2.6f, col, 26);
        }
    // stars thrown about
    Rnd r(91u);
    for (int i = 0; i < 70; ++i) {
        const float a = r.next(0, 2 * kPi) - t * 0.25f * r.next(0.5f, 1.f), d = r.next(0.15f, 0.78f) * R;
        c.pixel(int(cx + std::cos(a) * d), int(cy + std::sin(a) * d), rgb(255, 240, 255), int(r.next(120, 255)));
    }
    // the dark heart
    c.fillCircle(cx, cy, R * 0.13f, rgb(255, 230, 200), active ? 150 : 90);
    c.fillCircle(cx, cy, R * 0.09f, rgb(4, 2, 8));
    c.ring(cx, cy, R * 0.1f, 2.f, rgb(255, 220, 170), active ? 230 : 150);
}

// ------------------------------------------------------------------ planets
// Thunder's Edge: a space station - a steel ring on spokes round a lit hub, docking arms and running lights.
void drawStationBody(Canvas& c, float x, float y, float r, bool exhausted, uint32_t now) {
    const uint32_t steel = exhausted ? rgb(86, 92, 106) : rgb(170, 180, 198), dark = rgb(40, 44, 56);
    const uint32_t light = exhausted ? rgb(120, 126, 140) : rgb(232, 238, 250);
    c.fillCircle(x, y, r * 1.05f, rgb(120, 150, 200), 18);
    for (int i = 0; i < 4; ++i) {  // spokes
        const float a = kPi / 4.f + float(i) * kPi / 2.f;
        c.line(x, y, x + std::cos(a) * r * 0.78f, y + std::sin(a) * r * 0.78f, dark, r * 0.16f);
        c.line(x, y, x + std::cos(a) * r * 0.78f, y + std::sin(a) * r * 0.78f, steel, r * 0.08f);
    }
    c.ring(x, y, r * 0.8f, r * 0.26f, dark);
    c.ring(x, y, r * 0.8f, r * 0.2f, steel);
    c.ring(x - r * 0.03f, y - r * 0.03f, r * 0.84f, r * 0.05f, light, 140);
    for (int i = 0; i < 12; ++i) {  // panel seams
        const float a = float(i) * kPi / 6.f;
        c.line(x + std::cos(a) * r * 0.7f, y + std::sin(a) * r * 0.7f, x + std::cos(a) * r * 0.9f, y + std::sin(a) * r * 0.9f, dark, 1.f, 160);
    }
    for (int i = 0; i < 2; ++i) {  // docking arms with solar panels
        const float sx = i ? 1.f : -1.f;
        c.fillRect(int(x + sx * r * 0.92f - (i ? 0 : r * 0.34f)), int(y - r * 0.05f), int(r * 0.34f), std::max(2, int(r * 0.1f)), steel);
        c.fillRect(int(x + sx * r * 1.12f - r * 0.1f), int(y - r * 0.28f), int(r * 0.2f), int(r * 0.56f), rgb(40, 70, 130));
        c.fillRect(int(x + sx * r * 1.12f - r * 0.1f), int(y - r * 0.28f), int(r * 0.2f), int(r * 0.56f), rgb(120, 170, 240), 60);
    }
    c.fillCircle(x, y, r * 0.34f, dark);
    c.fillCircle(x, y, r * 0.3f, steel);
    c.fillCircle(x - r * 0.07f, y - r * 0.07f, r * 0.15f, light);
    const int blink = int((now / 400u) % 8u);
    for (int i = 0; i < 8; ++i) {
        const float a = kPi / 8.f + float(i) * kPi / 4.f;
        const bool on = !exhausted && (i == blink || i == (blink + 4) % 8);
        c.fillCircle(x + std::cos(a) * r * 0.8f, y + std::sin(a) * r * 0.8f, std::max(1.f, r * 0.045f), on ? rgb(255, 210, 110) : rgb(150, 110, 50));
    }
}

void drawPlanetBody(Canvas& c, const PlanetDef& pd, int def, float x, float y, float r, bool home, bool exhausted, uint32_t now = 0) {
    if (pd.flags & kPlanetStation) {
        drawStationBody(c, x, y, r, exhausted, now);
        return;
    }
    const bool mecatol = !std::strcmp(pd.key, "mr");
    uint32_t base = mecatol ? rgb(196, 160, 86) : home ? rgb(130, 146, 176) : traitCol(pd.trait);
    if (exhausted) base = shade(base, 0.55f);
    c.fillCircle(x, y, r * 1.14f, lighter(base, 0.3f), 36);  // atmosphere
    c.fillCircle(x, y, r, base);
    Rnd rd(uint32_t(def) * 11u + 5u);
    // surface: bands (cultural / home), craters (hazardous), continents (industrial), ruins (Mecatol)
    if (mecatol) {
        for (int i = 0; i < 70; ++i) {
            const float a = rd.next(0, 2 * kPi), d2 = std::sqrt(rd.next(0, 1)) * r * 0.9f;
            const float px = x + std::cos(a) * d2, py = y + std::sin(a) * d2, s = rd.next(1.f, 3.2f);
            c.fillRect(int(px), int(py), int(s), int(s), rgb(90, 70, 38), 200);
            if (i % 4 == 0) c.fillCircle(px, py, 1.f, rgb(255, 230, 150), 220);  // city lights in the ruins
        }
    } else if (pd.trait == Trait::Hazardous) {
        for (int i = 0; i < 9; ++i) {
            const float a = rd.next(0, 2 * kPi), d2 = rd.next(0, 0.75f) * r, s = rd.next(0.07f, 0.17f) * r;
            c.fillCircle(x + std::cos(a) * d2, y + std::sin(a) * d2, s, shade(base, 0.62f), 200);
            c.fillCircle(x + std::cos(a) * d2 - s * 0.2f, y + std::sin(a) * d2 - s * 0.2f, s * 0.6f, shade(base, 1.2f), 120);
        }
    } else if (pd.trait == Trait::Industrial) {
        for (int i = 0; i < 6; ++i) {
            const float a = rd.next(0, 2 * kPi), d2 = rd.next(0, 0.6f) * r, s = rd.next(0.18f, 0.32f) * r;
            c.fillCircle(x + std::cos(a) * d2, y + std::sin(a) * d2, s, shade(base, 0.7f), 170);
        }
    } else {
        for (int i = 0; i < 5; ++i) {
            const float yy = y - r * 0.7f + float(i) * r * 0.35f + rd.next(-2, 2);
            const float half = std::sqrt(std::max(0.f, r * r - (yy - y) * (yy - y))) * 0.92f;
            c.fillRect(int(x - half), int(yy), int(2 * half), std::max(1, int(r * 0.08f)), shade(base, i % 2 ? 1.18f : 0.8f), 120);
        }
    }
    if (home && !mecatol)
        for (int i = 0; i < 18; ++i) {
            const float a = rd.next(0, 2 * kPi), d2 = std::sqrt(rd.next(0, 1)) * r * 0.85f;
            c.fillCircle(x + std::cos(a) * d2, y + std::sin(a) * d2, 1.1f, rgb(255, 236, 170), 210);
        }
    if (pd.trait2 != Trait::None && !home) {  // Thunder's Edge: a second trait - its colour over the lower-right half
        uint32_t b2 = traitCol(pd.trait2);
        if (exhausted) b2 = shade(b2, 0.55f);
        float xs[17], ys[17];
        for (int i = 0; i <= 16; ++i) {
            const float a = -kPi / 4.f + kPi * float(i) / 16.f;
            xs[i] = x + r * std::cos(a);
            ys[i] = y + r * std::sin(a);
        }
        c.fillPolygon(xs, ys, 17, b2, 215);
        c.line(x + r * 0.71f, y - r * 0.71f, x - r * 0.71f, y + r * 0.71f, rgb(12, 14, 22), std::max(1.f, r * 0.05f), 140);
    }
    // light from the upper left, shadow on the lower right
    c.fillCircle(x - r * 0.3f, y - r * 0.3f, r * 0.55f, rgb(255, 255, 255), 40);
    for (int k = 0; k < 4; ++k) c.ring(x + r * 0.06f * float(k), y + r * 0.06f * float(k), r * (1.f - 0.07f * float(k)), r * 0.1f, rgb(0, 0, 0), 26);
}

struct Slot {
    UnitType t;
    int count;  // > 1 drawn once with "xN" (fighters / ground forces / PDS)
    int player;
    bool damaged;
};

// Ship lengths relative to each other, as on the owner's unit reference card.
float unitLen(UnitType t) {
    switch (t) {
        case UnitType::WarSun: return 0.95f;  // standing upright (0.21.1): narrow but the tallest
        case UnitType::Flagship:
        case UnitType::Flagship2: return 1.3f;
        case UnitType::Dreadnought: return 1.3f;
        case UnitType::Carrier: return 1.1f;
        case UnitType::Cruiser: return 1.15f;
        case UnitType::Destroyer: return 0.9f;
        case UnitType::Fighter: return 0.55f;
        default: return 0.7f;
    }
}

// The ships of one force as slots: every non-fighter ship on its own (big first),
// fighters (and ground forces in space) as one slot with a count.
void shipSlots(const Force& f, int player, std::vector<Slot>& out) {
    static const UnitType kOrder[] = {UnitType::WarSun, UnitType::Flagship, UnitType::Flagship2, UnitType::Dreadnought, UnitType::Carrier,
                                      UnitType::Cruiser, UnitType::Destroyer};
    for (UnitType t : kOrder) {
        const int n = f.count(t), dmg = std::min<int>(n, f.dmg[int(t)]);
        for (int k = 0; k < n; ++k) out.push_back({t, 1, player, k < dmg});
    }
    if (f.count(UnitType::Fighter)) out.push_back({UnitType::Fighter, f.count(UnitType::Fighter), player, false});
    if (f.count(UnitType::Infantry)) out.push_back({UnitType::Infantry, f.count(UnitType::Infantry), player, false});
    if (f.count(UnitType::Mech)) out.push_back({UnitType::Mech, f.count(UnitType::Mech), player, false});
    if (f.count(UnitType::SpaceDock)) out.push_back({UnitType::SpaceDock, f.count(UnitType::SpaceDock), player, false});
}

struct Placed {
    float x, y, size;
};

// Lays the slots out in rows inside the box (scaled down until they fit).
std::vector<Placed> layout(const std::vector<Slot>& slots, float x0, float y0, float w, float h, float base) {
    std::vector<Placed> out;
    if (slots.empty() || w <= 4 || h <= 4) return out;
    for (float scale = 1.f; scale > 0.25f; scale *= 0.88f) {
        out.clear();
        const float rowH = base * scale * 0.95f + 12.f;
        float x = x0, y = y0 + rowH / 2.f;
        bool ok = true;
        for (const Slot& s : slots) {
            const float len = unitLen(s.t) * base * scale;
            const float step = len + 8.f * scale + (s.count > 1 ? 6.f : 0.f);
            if (x + step > x0 + w + 0.5f && x > x0) {
                x = x0;
                y += rowH;
            }
            if (y + rowH / 2.f > y0 + h + 0.5f) {
                ok = false;
                break;
            }
            out.push_back({x + step / 2.f, y - 5.f, len});
            x += step;
        }
        if (ok) {
            // centre each row horizontally
            size_t i = 0;
            while (i < out.size()) {
                size_t j = i;
                while (j + 1 < out.size() && std::fabs(out[j + 1].y - out[i].y) < 0.5f) ++j;
                const float left = out[i].x - (unitLen(slots[i].t) * base * scale + 8.f * scale) / 2.f;
                const float right = out[j].x + (unitLen(slots[j].t) * base * scale + 8.f * scale) / 2.f;
                const float shift = (x0 + w - right - (left - x0)) / 2.f;
                for (size_t k = i; k <= j; ++k) out[k].x += shift;
                i = j + 1;
            }
            // and the block vertically
            const float used = out.back().y + 5.f + rowH / 2.f - y0;
            for (Placed& p : out) p.y += std::max(0.f, (h - used) / 2.f);
            return out;
        }
    }
    return out;
}

void countBadge(Canvas& c, float x, float y, int n, uint32_t col) {
    const std::string s = "x" + std::to_string(n);
    const int w = textWidth(FONT_SMB, s) + 6;
    c.fillRoundRect(int(x) - w / 2, int(y), w, 15, 4, rgb(6, 8, 14), 210);
    drawTextAligned(c, FONT_SMB, int(x) - w / 2, int(y) + 1, w, Align::Center, s, lighter(col, 0.35f));
}

std::vector<std::pair<int, int>> g_draftMon;  // (Monuments+ drafts) planet, player
}  // namespace
void setDraftMonuments(const std::vector<std::pair<int, int>>& m) { g_draftMon = m; }
namespace {
// Retrowave: a unit drawn, then cut into chunky pixels (drawn small, enlarged - like the tile art)
int g_unitBlock = 0;
void pixelate(Canvas& c, float x, float y, float size) {
    if (g_unitBlock < 2) return;
    const int h = int(size * 0.62f) + 2;
    retro::chunky(c, Rect{int(x) - h, int(y) - h, 2 * h, 2 * h}, g_unitBlock);
}

// Retrowave (0.49.0): the unit as pixel art (the battles' sprites) on its own little grid of g_unitBlock blocks.
// False when there is no picture (the drawn shape and the chunky pass then).
bool drawUnitPx(Canvas& c, UnitType t, float x, float y, float size, uint32_t col, bool faceLeft, bool damaged, uint32_t now) {
    if (g_unitBlock < 2) return false;
    const int h = int(size * 0.62f) + 2;
    pxship::Grid gr;
    retro::chunkyGrid(c, Rect{int(x) - h, int(y) - h, 2 * h, 2 * h}, g_unitBlock, gr.x, gr.y, gr.f);
    const float tall = t == UnitType::WarSun ? 1.1f : isShip(t) ? 0.8f : 1.f;
    const pxship::Sprite* sp = pxship::fit(c, gr, t, size, size * tall, col);
    if (!sp) return false;
    pxship::Look k;
    k.faceLeft = faceLeft;
    k.engines = isShip(t) && t != UnitType::WarSun;
    k.seed = uint32_t(x * 7.f + y * 13.f);
    if (damaged) {
        k.hurt = 1;
        k.hurtX = sp->w / 2 - sp->w / 6;
        k.hurtY = sp->h / 2;
    }
    pxship::draw(c, *sp, c.fX(x), c.fY(y), gr, k, now);
    return true;
}

}  // namespace

void drawMapUnit(Canvas& c, UnitType t, float cx, float cy, float size, uint32_t col, bool damaged, uint32_t now, int pixelBlock) {
    const int keep = g_unitBlock;
    g_unitBlock = pixelBlock;
    if (!drawUnitPx(c, t, cx, cy, size, col, false, damaged, now)) drawUnit(c, t, cx, cy, size, col, false, 255, damaged, now, true);
    g_unitBlock = keep;
}

namespace {

void drawSlots(Canvas& c, const Game& g, const std::vector<Slot>& slots, const std::vector<Placed>& at, bool faceLeft, uint32_t now) {
    for (size_t i = 0; i < slots.size() && i < at.size(); ++i) {
        const Slot& s = slots[i];
        if (s.t == UnitType::Monument) {  // (0.82.1) a monument in a space area: the spire picture with a dark rim, as on a planet
            const float sz = at[i].size * 1.2f, foot = at[i].y + at[i].size * 0.55f;
            pcolours::Finish fin(c, pcolours::around(at[i].x, at[i].y, sz * 0.6f, sz * 0.7f), pcolours::of(g, s.player), uint32_t(i) * 7u + 3u);
            for (int k = 0; k < 4; ++k)
                icons::drawUnitArtOn(c, "unit/monument", at[i].x + (k == 0 ? 1.5f : k == 1 ? -1.5f : 0.f), foot + (k == 2 ? 1.5f : k == 3 ? -1.5f : 0.f),
                                     int(sz * 0.8f), int(sz * 1.15f), rgb(0, 0, 0), false, 200);
            icons::drawUnitArtOn(c, "unit/monument", at[i].x, foot, int(sz * 0.8f), int(sz * 1.15f), pcol(g, s.player));
            pixelate(c, at[i].x, at[i].y, sz);
            continue;
        }
        pcolours::Finish fin(c, pcolours::around(at[i].x, at[i].y, at[i].size * 0.62f, at[i].size * 0.55f), pcolours::of(g, s.player), uint32_t(i) * 7u + 3u);
        if (drawUnitPx(c, s.t, at[i].x, at[i].y, at[i].size, pcol(g, s.player), faceLeft, s.damaged, now)) continue;
        // Retrowave: a dark rim so the ships stand out from the art (0.28.0, owner)
        drawUnit(c, s.t, at[i].x, at[i].y, at[i].size, pcol(g, s.player), faceLeft, 255, s.damaged, now, g_unitBlock >= 2);
        pixelate(c, at[i].x, at[i].y, at[i].size);
    }
    for (size_t i = 0; i < slots.size() && i < at.size(); ++i)  // the counts after every picture (sharp)
        if (slots[i].count > 1) countBadge(c, at[i].x, at[i].y + at[i].size * 0.38f, slots[i].count, pcol(g, slots[i].player));
}

// The Lady & The Lord (Ghemina, owner D117): their two flagships named under the pictures
void ladyLordCaptions(Canvas& c, const Game& g, int sys, const std::vector<Slot>& slots, const std::vector<Placed>& at) {
    if (!(g.modules & kModDiscordant)) return;
    bool ladyShown = false;
    for (size_t i = 0; i < slots.size() && i < at.size(); ++i) {
        const Slot& s = slots[i];
        if (!isFlagship(s.t) || s.player < 0 || s.player >= g.numPlayers) continue;
        if (g.players[size_t(s.player)].unit[int(UnitType::Flagship2)] >= 0) {  // (0.52.0) two flagship types: their names
            const bool second = s.t == UnitType::Flagship2;
            drawTextAligned(c, FONT_XS, int(at[i].x) - 40, int(at[i].y + at[i].size * 0.42f), 80, Align::Center,
                            unitDef(g, s.player, s.t).name, second ? rgb(255, 190, 230) : rgb(200, 220, 255));
            continue;
        }
        if (!isFaction(g, s.player, "ghemina")) continue;
        // (older games: the Ghemina flagship in her system is the Lady)
        const bool lady = !ladyShown && g.dsLadySys == sys;
        ladyShown = ladyShown || lady;
        drawTextAligned(c, FONT_XS, int(at[i].x) - 40, int(at[i].y + at[i].size * 0.42f), 80, Align::Center, lady ? "The Lady" : "The Lord",
                        lady ? rgb(255, 190, 230) : rgb(200, 220, 255));
    }
}

}  // namespace

// ------------------------------------------------------------------ unit pictures
void drawUnit(Canvas& c, UnitType t, float cx, float cy, float size, uint32_t col, bool faceLeft, int alpha, bool damaged, uint32_t now,
              bool outline) {
    const float sx = size / 2.f, sy = size / 2.f;
    const uint32_t body = shade(col, 0.62f), rim = lighter(col, 0.25f), dark = shade(col, 0.32f);
    const bool flip = faceLeft;
    auto shape = [&](const float (*pts)[2], int n, float kx, float ky, uint32_t fill) {
        poly(c, pts, n, cx, cy, sx * kx, sy * ky, flip, rim, alpha);  // a thin bright rim, then the body
        float xs[16], ys[16];
        for (int i = 0; i < n && i < 16; ++i) {
            xs[i] = cx + (flip ? -pts[i][0] : pts[i][0]) * sx * kx * 0.9f;
            ys[i] = cy + pts[i][1] * sy * ky * 0.84f;
        }
        c.fillPolygon(xs, ys, n, fill, alpha);
    };
    auto thrust = [&](float back, float half) {  // engine glow behind a ship
        const float flick = 0.75f + 0.25f * std::sin(float(now % 400) / 400.f * 2 * kPi + cx);
        const float bx = cx + (flip ? back : -back) * sx;
        c.fillCircle(bx, cy, half * sy * flick, rgb(255, 170, 70), alpha * 150 / 255);
        c.fillCircle(bx, cy, half * sy * 0.5f * flick, rgb(255, 240, 190), alpha * 200 / 255);
    };
    auto damage = [&]() {  // a crack and smoke
        c.line(cx - sx * 0.3f, cy - sy * 0.3f, cx + sx * 0.1f, cy + sy * 0.05f, rgb(255, 90, 40), 1.5f, alpha);
        c.line(cx + sx * 0.1f, cy + sy * 0.05f, cx - sx * 0.05f, cy + sy * 0.35f, rgb(255, 90, 40), 1.5f, alpha);
        const float rise = float(now % 1500) / 1500.f;
        c.fillCircle(cx - sx * 0.2f, cy - sy * (0.4f + 0.5f * rise), sy * (0.18f + 0.15f * rise), rgb(120, 120, 120), int(120 * (1 - rise)));
    };
    // 0.20.1 (owner): the pictures from his unit reference card, in the player's colour
    static const char* kArt[kUnitTypes] = {"unit/carrier", "unit/cruiser",  "unit/destroyer", "unit/dreadnought",
                                           "unit/fighter", "unit/flagship", "unit/warsun",    "unit/infantry",
                                           "unit/mech",    "unit/pds",      "unit/spacedock", "unit/flagship"};
    if (int(t) < kUnitTypes && kArt[int(t)] && icons::available()) {  // (0.82.0: monuments have no picture here)
        const bool ship = isShip(t);
        const float tall = t == UnitType::WarSun ? 1.1f : ship ? 0.8f : 1.f;  // the war sun stands (owner, 0.21.1)
        const int maxW = std::max(4, int(size)), maxH = std::max(4, int(size * tall));
        if (ship && t != UnitType::WarSun) {  // engine glow at the stern
            const float flick = 0.75f + 0.25f * std::sin(float(now % 400) / 400.f * 2 * kPi + cx);
            const float bx = cx + (flip ? 0.46f : -0.46f) * size;
            const float half = size * (t == UnitType::Fighter ? 0.1f : 0.07f);
            c.fillCircle(bx, cy, half * 1.4f * flick, rgb(255, 170, 70), alpha * 120 / 255);
            c.fillCircle(bx, cy, half * 0.7f * flick, rgb(255, 240, 190), alpha * 190 / 255);
        }
        if (outline && icons::available())  // a dark rim, for pictures standing on a planet
            for (int k = 0; k < 4; ++k)
                icons::drawUnitArt(c, kArt[int(t)], cx + (k == 0 ? 1.5f : k == 1 ? -1.5f : 0.f), cy + (k == 2 ? 1.5f : k == 3 ? -1.5f : 0.f),
                                   maxW, maxH, rgb(0, 0, 0), flip, alpha * 200 / 255);
        if (icons::drawUnitArt(c, kArt[int(t)], cx, cy, maxW, maxH, col, flip, alpha)) {
            if (damaged) damage();
            return;
        }
    }
    switch (t) {
        case UnitType::Fighter: {
            static const float p[][2] = {{1, 0}, {-0.6f, -0.75f}, {-0.25f, 0}, {-0.6f, 0.75f}};
            thrust(0.3f, 0.2f);
            shape(p, 4, 1, 1, body);
            break;
        }
        case UnitType::Destroyer: {
            static const float p[][2] = {{1, 0}, {0.2f, -0.32f}, {-0.9f, -0.3f}, {-1, 0}, {-0.9f, 0.3f}, {0.2f, 0.32f}};
            thrust(1.f, 0.18f);
            shape(p, 6, 1, 1, body);
            c.fillCircle(cx + (flip ? 0.1f : -0.1f) * sx, cy, sy * 0.1f, rim, alpha);
            break;
        }
        case UnitType::Cruiser: {
            static const float p[][2] = {{1, 0}, {0.4f, -0.28f}, {-0.2f, -0.42f}, {-0.9f, -0.38f}, {-1, 0}, {-0.9f, 0.38f}, {-0.2f, 0.42f}, {0.4f, 0.28f}};
            thrust(1.f, 0.22f);
            shape(p, 8, 1, 1, body);
            c.fillRect(int(cx - sx * 0.35f), int(cy - sy * 0.05f), int(sx * 0.7f), std::max(1, int(sy * 0.1f)), dark, alpha);
            break;
        }
        case UnitType::Carrier: {
            static const float p[][2] = {{0.88f, -0.16f}, {1, 0}, {0.88f, 0.16f}, {0.7f, 0.42f}, {-0.9f, 0.42f}, {-1, 0.2f},
                                         {-1, -0.2f}, {-0.9f, -0.42f}, {0.7f, -0.42f}};
            thrust(1.f, 0.26f);
            shape(p, 9, 1, 1, body);
            for (int k = 0; k < 3; ++k)  // hangar bays
                c.fillRect(int(cx + (-0.55f + 0.35f * float(k)) * sx), int(cy - sy * 0.15f), std::max(2, int(sx * 0.22f)),
                           std::max(2, int(sy * 0.3f)), dark, alpha);
            break;
        }
        case UnitType::Dreadnought:
        case UnitType::Flagship:
        case UnitType::Flagship2: {
            static const float p[][2] = {{1, 0}, {0.5f, -0.28f}, {0.1f, -0.52f}, {-0.8f, -0.58f}, {-1, -0.3f},
                                         {-1, 0.3f}, {-0.8f, 0.58f}, {0.1f, 0.52f}, {0.5f, 0.28f}};
            const bool flag = isFlagship(t);
            thrust(1.f, 0.3f);
            if (flag) {  // fins
                static const float fin[][2] = {{-0.2f, -0.5f}, {-0.6f, -0.95f}, {-0.85f, -0.5f}};
                static const float fin2[][2] = {{-0.2f, 0.5f}, {-0.6f, 0.95f}, {-0.85f, 0.5f}};
                poly(c, fin, 3, cx, cy, sx, sy, flip, rim, alpha);
                poly(c, fin2, 3, cx, cy, sx, sy, flip, rim, alpha);
            }
            shape(p, 9, 1, 1, body);
            for (int k = 0; k < 2; ++k) {  // turrets
                const float tx = cx + (flip ? -1.f : 1.f) * (0.15f - 0.45f * float(k)) * sx;
                c.fillCircle(tx, cy, sy * 0.15f, dark, alpha);
                c.fillRect(int(std::min(tx, tx + (flip ? -1.f : 1.f) * sx * 0.3f)), int(cy - 1), int(sx * 0.3f), 2, rim, alpha);
            }
            if (flag) c.ring(cx, cy, sy * 0.95f, 1.5f, rgb(255, 230, 140), alpha * 180 / 255);
            break;
        }
        case UnitType::WarSun: {
            const float r = sy * 0.8f;
            c.fillCircle(cx, cy, r * 1.15f, rim, alpha / 3);
            c.fillCircle(cx, cy, r, body, alpha);
            c.fillCircle(cx - r * 0.3f, cy - r * 0.3f, r * 0.45f, lighter(body, 0.25f), alpha * 120 / 255);
            c.fillRect(int(cx - r), int(cy - r * 0.08f), int(2 * r), std::max(2, int(r * 0.16f)), dark, alpha);  // the equator trench
            c.fillCircle(cx + r * 0.35f, cy - r * 0.35f, r * 0.2f, dark, alpha);                                 // the great gun
            c.ring(cx, cy, r * 1.25f, 1.5f, rim, alpha * 150 / 255);
            break;
        }
        case UnitType::Infantry: {
            c.fillCircle(cx, cy - sy * 0.45f, sy * 0.3f, rim, alpha);  // helmet
            c.fillCircle(cx, cy - sy * 0.45f, sy * 0.22f, body, alpha);
            static const float p[][2] = {{-0.55f, 0.85f}, {-0.45f, -0.1f}, {0.45f, -0.1f}, {0.55f, 0.85f}};
            shape(p, 4, 1, 1, body);
            c.fillRect(int(cx + sx * 0.35f), int(cy - sy * 0.3f), std::max(2, int(sx * 0.14f)), int(sy * 0.9f), dark, alpha);  // rifle
            break;
        }
        case UnitType::Mech: {
            static const float p[][2] = {{-0.6f, -0.7f}, {0.6f, -0.7f}, {0.7f, 0.15f}, {-0.7f, 0.15f}};
            shape(p, 4, 1, 1, body);
            c.fillRect(int(cx - sx * 0.55f), int(cy + sy * 0.15f), std::max(2, int(sx * 0.25f)), int(sy * 0.75f), rim, alpha);  // legs
            c.fillRect(int(cx + sx * 0.3f), int(cy + sy * 0.15f), std::max(2, int(sx * 0.25f)), int(sy * 0.75f), rim, alpha);
            c.fillCircle(cx, cy - sy * 0.3f, sy * 0.16f, rgb(255, 220, 120), alpha);  // cockpit
            break;
        }
        case UnitType::PDS: {
            static const float base[][2] = {{-0.7f, 0.9f}, {-0.45f, 0.1f}, {0.45f, 0.1f}, {0.7f, 0.9f}};
            shape(base, 4, 1, 1, body);
            c.fillCircle(cx, cy, sy * 0.35f, rim, alpha);
            c.fillCircle(cx, cy, sy * 0.25f, body, alpha);
            c.line(cx, cy, cx + sx * 0.75f, cy - sy * 0.85f, rim, std::max(1.5f, sx * 0.16f), alpha);  // the barrel
            break;
        }
        case UnitType::SpaceDock: {
            c.ring(cx, cy, sy * 0.8f, std::max(2.f, sy * 0.22f), rim, alpha);
            c.ring(cx, cy, sy * 0.8f, std::max(1.f, sy * 0.12f), body, alpha);
            for (int k = 0; k < 4; ++k) {
                const float a = kPi / 4.f + kPi / 2.f * float(k);
                c.line(cx, cy, cx + std::cos(a) * sy * 0.75f, cy + std::sin(a) * sy * 0.75f, rim, 1.5f, alpha);
            }
            c.fillCircle(cx, cy, sy * 0.25f, body, alpha);
            break;
        }
        default: c.fillCircle(cx, cy, sy * 0.5f, body, alpha); break;
    }
    if (damaged) damage();
}

// ------------------------------------------------------------------ the tile
// Where the expanded view puts planet i of n (offsets from the centre, R = corner radius).
static void planetPlace(int np, int i, bool nexus, bool mecatol, float R, float& px, float& py, float& pr) {
    px = 0.f;
    py = -R * 0.3f;
    pr = R * 0.25f;
    if (nexus) {
        px = R * 0.2f;
        py = -R * 0.24f;
        pr = R * 0.2f;
    } else if (np == 2) {
        px = (i == 0 ? -0.36f : 0.36f) * R;
        pr = R * 0.21f;
    } else if (np >= 3) {
        px = (-0.52f + 0.52f * float(i)) * R;
        py = -R * (i == 1 ? 0.42f : 0.22f);
        pr = R * 0.16f;
    }
    if (mecatol) pr = R * 0.28f;
}

void draw(Canvas& c, const Game& g, int sys, Rect area, uint32_t now, const CombatShow* combat) {
    if (sys < 0 || sys >= int(g.systems.size())) return;
    const SystemState& s = g.systems[size_t(sys)];
    const SystemDef& d = s.d();
    // the hex: flat-top, as big as fits (2R wide, sqrt(3) R tall)
    float R = std::min(float(area.w) / 2.f, float(area.h) / 1.7320508f) - 4.f;
    // Retrowave (0.26.0): the pixel art at a whole-number scale, so every art pixel is the same size
    const bool art = retro::art();  // (0.32.0: or Voidfall - high definition, no snapping, no chunky units)
    if (retro::on() && R >= 2.f * retro::kArtR) R = retro::snapScale(R) * retro::kArtR;
    g_unitBlock = retro::on() ? std::max(2, int(R / retro::kArtR * 0.45f)) : 0;  // the units' chunky pixels (finer since 0.28.0)
    const float cx = float(area.x) + float(area.w) / 2.f, cy = float(area.y) + float(area.h) / 2.f;
    const Rect keep = c.clip();
    const bool nexus = sys == g.nexus;
    // 0.44.1 (owner: "New Fracture tiles not showing for either tileset except Voidfall"): the Fracture's and
    // Thunder's Edge's painted skies in every tileset (Simplistic draws its own planets on top)
    const bool vfSpecial = false;  // (0.46.2 - owner: Simplistic draws its own Fracture / Thunder's Edge again)
    if (art || vfSpecial) {
        retro::Tile t;
        retro::setBackground(t, s.def, uint32_t(s.def), nexus && s.nexusActive);
        if (sys == g.teSystem && !kSystems[size_t(s.def)].anomalies) t.sky = "te";  // 0.43.1 (Voidfall): Thunder's Edge in its storm
        const float k = retro::kArtR / R;  // screen -> art
        const uint8_t w = wormholesOf(g, sys);
        int nw = 0, kk = 0;
        for (uint8_t bit : {kAlpha, kBeta, kGamma, kDelta, kEpsilon})
            if (w & bit) ++nw;
        auto worm = [&](uint8_t bit, float x, float y, float r) {
            retro::Piece p;
            const bool storm = (s.tokenWormholes & bit) && sys == g.ionStorm;
            p.kind = storm ? retro::Piece::IonStorm : retro::Piece::Wormhole;
            p.id = storm ? (bit == kAlpha ? 0 : 1) : bit == kAlpha ? 0 : bit == kBeta ? 1 : bit == kGamma ? 2 : bit == kDelta ? 3 : 4;
            p.x = x * k;
            p.y = y * k;
            p.r = r * k * (storm ? 1.15f : 1.f);
            t.pieces.push_back(p);
        };
        if (nexus) {
            const float spots[3][2] = {{-0.4f, 0.44f}, {-0.02f, 0.6f}, {0.38f, 0.46f}};
            int i = 0;
            for (uint8_t bit : {kAlpha, kBeta, kGamma}) worm(bit, spots[i][0] * R, spots[i][1] * R, R * 0.11f), ++i;
        } else {
            for (uint8_t bit : {kAlpha, kBeta, kGamma, kDelta, kEpsilon}) {
                if (!(w & bit)) continue;
                const bool planets = !s.planets.empty();
                const float r = planets ? R * 0.13f : R * 0.2f;
                const float wx = planets ? R * (0.62f - 0.3f * float(kk)) : (float(kk) - float(nw - 1) * 0.5f) * r * 2.4f;
                const float wy = planets ? -R * 0.62f : -R * 0.25f;
                worm(bit, wx, wy, r);
                ++kk;
            }
        }
        if (dimensionalTear(g, sys)) {
            retro::Piece p;
            p.kind = retro::Piece::Tear;
            p.r = retro::kArtR * 0.3f;
            t.pieces.push_back(p);
        }
        const int np = int(s.planets.size());
        for (int i = 0; i < np; ++i) {
            const PlanetState& ps = g.planets[size_t(s.planets[size_t(i)])];
            if (ps.destroyed) continue;
            retro::Piece p;
            p.kind = retro::Piece::Planet;
            p.id = ps.def;
            planetPlace(np, i, nexus, !std::strcmp(kPlanets[size_t(ps.def)].key, "mr"), R, p.x, p.y, p.r);
            p.x *= k;
            p.y *= k;
            p.r *= k;
            p.dim = ps.exhausted;
            t.pieces.push_back(p);
        }
        if (!art) t.pieces.clear();
        if (vfSpecial) voidfall::draw(c, t, cx, cy, R, now);
        else retro::draw(c, t, cx, cy, R, now);
        if (s.fracture) decor::fractureLook(c, std::atoi(d.key + 8), cx, cy, R, now, false);  // Thunder's Edge
    } else if (nexus) {
        drawNexusWarp(c, cx, cy, R, now, s.nexusActive);
    } else if (s.fracture) {
        decor::fractureLook(c, std::atoi(d.key + 8), cx, cy, R, now, true);
    } else {
        drawBackground(c, d, s.home, s.home >= 0 ? pcol(g, s.home) : 0, cx, cy, R, now, s.def);
    }

    if (s.breach) drawBreachMark(c, cx, cy, R, s.breach == 2, now);  // Crimson Rebellion's breach (Thunder's Edge)
    drawEventMarks(c, g, sys, cx, cy, R);  // galactic events
    // wormholes (printed and tokens): top right, or the middle of an empty system
    {
        const uint8_t w = wormholesOf(g, sys);
        int k = 0, nw = 0;
        for (uint8_t bit : {kAlpha, kBeta, kGamma, kDelta, kEpsilon})
            if (w & bit) ++nw;
        if (nexus) {  // as printed on the tile: alpha, beta, gamma along the bottom (alpha and beta dim until it's active)
            const float spots[3][2] = {{-0.4f, 0.44f}, {-0.02f, 0.6f}, {0.38f, 0.46f}};
            int i = 0;
            for (uint8_t bit : {kAlpha, kBeta, kGamma}) {
                const bool on = (w & bit) != 0;
                if (!art) drawWormhole(c, bit, cx + spots[i][0] * R, cy + spots[i][1] * R, R * 0.11f, on ? now : 0, false);
                if (!on) c.fillCircle(cx + spots[i][0] * R, cy + spots[i][1] * R, R * 0.13f, rgb(10, 6, 18), 140);
                ++i;
            }
            nw = 0;
        }
        for (uint8_t bit : {kAlpha, kBeta, kGamma, kDelta, kEpsilon}) {
            if (nexus || !(w & bit)) continue;
            const bool planets = !s.planets.empty();
            const float r = planets ? R * 0.13f : R * 0.2f;
            const float wx = planets ? cx + R * (0.62f - 0.3f * float(k)) : cx + (float(k) - float(nw - 1) * 0.5f) * r * 2.4f;
            const float wy = planets ? cy - R * 0.62f : cy - R * 0.25f;
            if (!art) drawWormhole(c, bit, wx, wy, r, now, (s.tokenWormholes & bit) != 0);
            ++k;
        }
    }
    // ingress token (Thunder's Edge, 0.43.0)
    if (s.ingress) {
        const float ix = cx - R * 0.62f, iy = cy + R * 0.5f;
        decor::drawIngress(c, ix, iy, R * 0.12f, now);
        drawText(c, FONT_XS, int(ix + R * 0.15f), int(iy) - 6, "ingress", rgb(255, 170, 170));
    }
    // frontier token
    if (s.frontier) {
        const float fx = cx - R * 0.62f, fy = cy - R * 0.62f, r = R * 0.07f;
        float xs[10], ys[10];
        for (int i = 0; i < 10; ++i) {
            const float a = kPi / 5.f * float(i) - kPi / 2.f, rr = (i % 2) ? r * 0.45f : r;
            xs[i] = fx + rr * std::cos(a);
            ys[i] = fy + rr * std::sin(a);
        }
        c.fillPolygon(xs, ys, 10, rgb(150, 200, 230));
        drawText(c, FONT_XS, int(fx + r + 3), int(fy) - 6, "frontier", rgb(150, 200, 230));
    }

    // planets along the upper part, their ground forces and structures under them
    const int np = int(s.planets.size());
    const bool spaceOnly = np == 0;
    for (int i = 0; i < np; ++i) {
        const int pl = s.planets[size_t(i)];
        const PlanetState& ps = g.planets[size_t(pl)];
        const PlanetDef& pd = kPlanets[size_t(ps.def)];
        if (ps.destroyed) continue;
        // (0.20.1: a little larger - the ground forces and structures stand on the planet)
        float px, py, pr;
        planetPlace(np, i, nexus, !std::strcmp(pd.key, "mr"), R, px, py, pr);
        px += cx;
        py += cy;
        if (nexus) {  // Mallice beside the swirl's heart, in its orbit
            float lx = 0, ly = 0;
            for (int k = 0; k <= 40; ++k) {
                const float a = 2 * kPi * float(k) / 40.f;
                const float x = px + std::cos(a) * pr * 1.55f, y = py + std::sin(a) * pr * 0.42f;
                if (k && (k < 4 || k > 16)) c.line(lx, ly, x, y, rgb(230, 230, 255), 1.5f, 150);  // (behind the planet: hidden)
                lx = x;
                ly = y;
            }
        }
        if (ps.owner >= 0) {
            pcolours::Finish fin(c, pcolours::around(px, py, pr + 5.f, pr + 5.f), pcolours::of(g, ps.owner), uint32_t(i) * 11u);
            c.ring(px, py, pr + 3.f, 3.f, pcol(g, ps.owner));
        }
        if (!art) drawPlanetBody(c, pd, ps.def, px, py, pr, s.home >= 0, ps.exhausted, now);
        // name, resources / influence, specialty, legendary, attachments
        const int ly = int(py - pr) - 30;
        drawTextAligned(c, FONT_SMB, int(px) - 70, ly, 140, Align::Center, fitText(FONT_SMB, pd.name, 138),
                        ps.exhausted ? theme::dim : theme::text);
        const int iy = ly + 22;
        drawResourceIcon(c, int(px) - 11, iy, 16, planetResources(g, pl), FONT_XS, false);
        drawResourceIcon(c, int(px) + 11, iy, 16, planetInfluence(g, pl), FONT_XS, true);
        const int skips = planetSkips(g, pl);
        int k = 0;
        for (int sc = 0; sc < 4; ++sc)
            if ((skips >> sc) & 1) drawSkipIcon(c, TechColor(sc), int(px + pr * 0.75f) + k++ * 14, int(py - pr * 0.95f), 14);
        if (pd.skip2 != TechColor::None && pd.skip2 == pd.skip)  // two specialties of one colour (Tiamat)
            drawSkipIcon(c, pd.skip2, int(px + pr * 0.75f) + k++ * 14, int(py - pr * 0.95f), 14);
        if (isLegendaryPlanet(g, pl)) {
            c.fillCircle(px - pr * 0.95f, py - pr * 0.7f, 8.f, rgb(10, 12, 20), 230);
            c.ring(px - pr * 0.95f, py - pr * 0.7f, 8.f, 1.5f, theme::gold);
            icons::draw(c, "legendary_white", int(px - pr * 0.95f) - 6, int(py - pr * 0.7f) - 6, 12, 255, theme::gold);
        }
        if (!ps.attachments.empty() || ps.terraform || ps.ulHero || ps.nanoForge) {
            float ax = px - pr * 0.95f, ay = py + pr * 0.7f, ar = 6.f;
            float dx[4] = {ax, ax + ar, ax, ax - ar}, dy[4] = {ay - ar, ay, ay + ar, ay};
            c.fillPolygon(dx, dy, 4, theme::gold);
        }
        if (ps.sleeper) {
            c.fillCircle(px + pr * 0.9f, py + pr * 0.7f, 7.f, rgb(20, 40, 44));
            c.ring(px + pr * 0.9f, py + pr * 0.7f, 7.f, 2.f, rgb(90, 220, 200));
        }
        if (ps.superweapon >= 0) {  // (0.84.0) a Belkosea superweapon: the sphere just off the planet's right edge (0.84.1: clear of the
                                    // units standing on it), its name under it
            const bool ex = ps.owner >= 0 && ps.owner < g.numPlayers && ((g.players[size_t(ps.owner)].swExhausted >> ps.superweapon) & 1);
            const float sr = std::max(6.f, pr * 0.24f), sx = px + pr + sr * 1.25f, sy = py - pr * 0.05f;
            superweapon::drawPiece(c, ps.superweapon, sx, sy, sr, now, ex);
            TextHalo halo;
            drawTextAligned(c, FONT_XS, int(sx) - 40, int(sy + sr) + 3, 80, Align::Center, kSuperweapons[ps.superweapon].name,
                            superweapon::colour(ps.superweapon));
        }
        if (ps.tomb) {  // (0.85.0) a Pharad'n tomb token (Ancient Empire): just off the planet's left edge, "Tomb" under it
            const float tr = std::max(6.f, pr * 0.22f), tx = px - pr - tr * 1.25f, ty = py - pr * 0.05f;
            pharadn::drawTomb(c, tx, ty, tr, now);
            TextHalo halo;
            drawTextAligned(c, FONT_XS, int(tx) - 40, int(ty + tr) + 3, 80, Align::Center, "Tomb", pharadn::gold());
        }
        if (ps.exhausted)
            drawTextAligned(c, FONT_XS, int(px) - 40, int(py + pr) + 5, 80, Align::Center, "exhausted", rgb(200, 200, 210));
        // ground forces and structures stand on the planet (owner, 0.20.1): one picture per
        // kind with the count, outlined so they stand out from the surface
        std::vector<Slot> slots;
        int monSlot = -1;  // (0.82.0 Monuments+) a monument stands first, a little larger
        for (const auto& m : g_draftMon)
            if (m.first == pl) {
                monSlot = 0;
                slots.push_back({UnitType::SpaceDock, 1, m.second, false});
            }
        for (int q = 0; q < g.numPlayers && monSlot < 0; ++q)
            if (ps.units[size_t(q)].count(UnitType::Monument)) {
                monSlot = 0;
                slots.push_back({UnitType::SpaceDock, 1, q, false});
            }
        for (int q = 0; q < kSides; ++q) {  // (kNeutral: Thunder's Edge's neutral units)
            if (q >= g.numPlayers && q != kNeutral) continue;
            const Force& f = ps.units[size_t(q)];
            for (UnitType t : {UnitType::SpaceDock, UnitType::PDS, UnitType::Mech, UnitType::Infantry})
                if (f.count(t)) slots.push_back({t, f.count(t), q, false});
        }
        if (!slots.empty()) {
            const float bw = pr * 1.9f, bh = pr * 1.5f;
            std::vector<Placed> at = layout(slots, px - bw / 2.f, py - bh / 2.f + 2.f, bw, bh, pr * 0.95f);
            for (Placed& p2 : at) p2.size = std::min(p2.size, pr * 0.72f);
            for (size_t q = 0; q < slots.size() && q < at.size(); ++q) {
                const Slot& sl = slots[q];
                if (int(q) == monSlot) {  // (Monuments+ drafts) the monument: the spire picture, a dark rim, no count
                    const float sz = std::min(at[q].size * 1.6f, pr * 1.15f), foot = at[q].y + at[q].size * 0.55f;
                    pcolours::Finish fin(c, pcolours::around(at[q].x, at[q].y, sz * 0.6f, sz * 0.7f), pcolours::of(g, sl.player), uint32_t(q) * 7u + 5u);
                    for (int k = 0; k < 4; ++k)
                        icons::drawUnitArtOn(c, "unit/monument", at[q].x + (k == 0 ? 1.5f : k == 1 ? -1.5f : 0.f), foot + (k == 2 ? 1.5f : k == 3 ? -1.5f : 0.f),
                                             int(sz * 0.8f), int(sz * 1.15f), rgb(0, 0, 0), false, 200);
                    icons::drawUnitArtOn(c, "unit/monument", at[q].x, foot, int(sz * 0.8f), int(sz * 1.15f), pcol(g, sl.player));
                    pixelate(c, at[q].x, at[q].y, sz);
                    continue;
                }
                {
                    pcolours::Finish fin(c, pcolours::around(at[q].x, at[q].y, at[q].size * 0.6f, at[q].size * 0.6f), pcolours::of(g, sl.player), uint32_t(q) * 7u + 5u);
                    if (!drawUnitPx(c, sl.t, at[q].x, at[q].y, at[q].size, pcol(g, sl.player), false, false, now)) {
                        drawUnit(c, sl.t, at[q].x, at[q].y, at[q].size, pcol(g, sl.player), false, 255, false, now, true);
                        pixelate(c, at[q].x, at[q].y, at[q].size);
                    }
                }
                const std::string n = "x" + std::to_string(sl.count);  // a small count at the unit's feet
                const int w = textWidth(FONT_XS, n) + 4, y = int(at[q].y + at[q].size * 0.46f);
                c.fillRoundRect(int(at[q].x) - w / 2, y, w, 11, 3, rgb(6, 8, 14), 215);
                drawTextAligned(c, FONT_XS, int(at[q].x) - w / 2, y, w, Align::Center, n, lighter(pcolours::readable(pcol(g, sl.player)), 0.45f));
            }
        }
    }

    // the space area: below the planets (the whole tile without planets)
    const float top = spaceOnly ? cy - R * 0.55f : cy + R * (combat ? 0.1f : 0.14f);
    const float bottom = cy + R * (combat ? 0.62f : 0.8f);
    const float halfW = R * 0.8f;
    if (combat) {
        // the fight: attacker on the left facing right, defender on the right facing left
        std::vector<Slot> la, lb;
        shipSlots(combat->a, combat->attacker, la);
        shipSlots(combat->b, combat->defender, lb);
        const float base = R * 0.3f;
        std::vector<Placed> pa = layout(la, cx - halfW, top, halfW - 10.f, bottom - top, base);
        std::vector<Placed> pb = layout(lb, cx + 10.f, top, halfW - 10.f, bottom - top, base);
        drawSlots(c, g, la, pa, false, now);
        drawSlots(c, g, lb, pb, true, now);
        ladyLordCaptions(c, g, sys, la, pa);
        ladyLordCaptions(c, g, sys, lb, pb);
        // shots: the newest roll of each side, for a moment
        for (const CombatShow::Roll& r : combat->rolls) {
            const int age = int(now - r.time);
            if (age < 0 || age > 900 || r.hits <= 0) continue;
            const bool left = r.player == combat->attacker;
            const std::vector<Placed>& from = left ? pa : pb;
            const std::vector<Placed>& to = left ? pb : pa;
            if (from.empty() || to.empty()) continue;
            const int al = 255 - age * 255 / 900;
            for (int h = 0; h < r.hits; ++h) {
                const Placed& a = from[size_t(h) % from.size()];
                const Placed& b = to[size_t(h * 7 + 3) % to.size()];
                c.line(a.x, a.y, b.x, b.y, pcol(g, r.player), 3.f, al / 3);
                c.line(a.x, a.y, b.x, b.y, rgb(255, 250, 220), 1.2f, al);
            }
        }
        // explosions where ships were lost (they are already out of the force: after the last ones)
        for (const CombatShow::Loss& l : combat->losses) {
            const int age = int(now - l.time);
            if (age < 0 || age > 1100) continue;
            const bool left = l.player == combat->attacker;
            const std::vector<Placed>& at = left ? pa : pb;
            const float k = float(age) / 1100.f;
            for (int i = 0; i < std::min(l.count, 6); ++i) {
                float x = left ? cx - halfW * 0.5f : cx + halfW * 0.5f, y = (top + bottom) / 2.f;
                if (!at.empty()) {
                    const Placed& p = at[size_t(at.size() - 1 - std::min<size_t>(at.size() - 1, size_t(i)))];
                    x = p.x + (left ? 1.f : -1.f) * (p.size * 0.8f + float(i) * 10.f);
                    y = p.y + float(i % 2) * 8.f;
                }
                const float r = 6.f + 28.f * k;
                c.fillCircle(x, y, r, rgb(255, 120, 40), int(200 * (1 - k)));
                c.fillCircle(x, y, r * 0.6f, rgb(255, 230, 150), int(230 * (1 - k)));
                c.ring(x, y, r * 1.3f, 2.f, rgb(255, 200, 120), int(160 * (1 - k)));
            }
        }
        // the dice of the newest roll per side, under its ships (the result instead once it's over)
        for (int side = 0; side < 2 && !(combat->ended && !combat->result.empty()); ++side) {
            const int who = side == 0 ? combat->attacker : combat->defender;
            const CombatShow::Roll* last = nullptr;
            for (const CombatShow::Roll& r : combat->rolls)
                if (r.player == who) last = &r;
            if (!last || int(now - last->time) > 4000) continue;
            const int n = int(last->dice.size()), dw = 18, gap = 3;
            const int total = std::min(n, 10) * (dw + gap);
            int x = int(side == 0 ? cx - halfW / 2.f : cx + halfW / 2.f) - total / 2;
            const int y = int(bottom) + 16;
            for (int i = 0; i < n && i < 10; ++i) {
                const bool hit = i < int(last->need.size()) && last->dice[size_t(i)] >= last->need[size_t(i)];
                c.fillRoundRect(x, y, dw, dw, 4, hit ? rgb(60, 170, 80) : rgb(50, 54, 66), 240);
                drawTextAligned(c, FONT_SMB, x, y + 2, dw, Align::Center, std::to_string(last->dice[size_t(i)] % 10 == 0 ? 10 : last->dice[size_t(i)]),
                                theme::text);
                x += dw + gap;
            }
            drawTextAligned(c, FONT_XS, int(side == 0 ? cx - halfW : cx), y + dw + 1, int(halfW), Align::Center,
                            std::to_string(last->hits) + (last->hits == 1 ? " hit" : " hits"), last->hits ? theme::green : theme::dim);
        }
        if (combat->ended && !combat->result.empty()) {
            const int w = int(R * 1.3f);
            c.fillRoundRect(int(cx) - w / 2, int(bottom) + 16, w, 22, 6, rgb(8, 10, 18), 230);
            drawTextAligned(c, FONT_SMB, int(cx) - w / 2, int(bottom) + 19, w, Align::Center, fitText(FONT_SMB, combat->result, w - 8),
                            combat->winner >= 0 ? pcolours::readable(pcol(g, combat->winner)) : theme::text);
        }
    } else {
        // every player's ships in space, one row group per player
        std::vector<Slot> slots;
        for (int q = 0; q < g.numPlayers; ++q) {
            shipSlots(s.space[size_t(q)], q, slots);
            // (0.82.1) a monument in the space area (The Panopticon, the Seraph Data Center): its spire with the ships
            if (s.space[size_t(q)].count(UnitType::Monument)) slots.push_back({UnitType::Monument, 1, q, false});
        }
        shipSlots(s.space[size_t(kNeutral)], kNeutral, slots);  // Thunder's Edge
        if (!slots.empty()) {
            std::vector<Placed> at = layout(slots, cx - halfW, top, 2.f * halfW, bottom - top, R * 0.38f);
            drawSlots(c, g, slots, at, false, now);
            ladyLordCaptions(c, g, sys, slots, at);
        } else if (spaceOnly && !d.anomalies && !wormholesOf(g, sys)) {
            drawTextAligned(c, FONT_SM, int(cx - halfW), int(cy + R * 0.4f), int(2 * halfW), Align::Center, "Empty space", theme::faint);
        }
    }

    // command tokens along the top edge
    {
        int n = 0;
        for (int q = 0; q < g.numPlayers; ++q)
            if (s.hasToken(q)) ++n;
        const int tw = int(R * 0.16f);
        float x = cx - float(n - 1) * float(tw + 4) / 2.f;
        for (int q = 0; q < g.numPlayers; ++q)
            if (s.hasToken(q)) {
                pcolours::Finish fin(c, pcolours::around(x, cy - R * 0.8f, float(tw) / 2.f + 1.f, float(cards::commandTokenHeight(tw)) / 2.f + 1.f),
                                     pcolours::of(g, q), uint32_t(q) * 3u + 1u);
                cards::commandToken(c, g.players[size_t(q)].faction, pcol(g, q), int(x), int(cy - R * 0.8f), tw);
                x += float(tw + 4);
            }
    }
    if (art && s.home >= 0) {  // Retrowave: the owner's colour round a home system (after the chunky units)
        float xs[6], ys[6];
        hexPoints(cx, cy, R - 2.f, xs, ys);
        const int hpc = pcolours::of(g, s.home);
        const bool fx = hpc >= 0 && pcolours::animated(hpc);
        for (int i = 0; i < 6; ++i) {
            const int j = (i + 1) % 6;
            const float mx = (xs[i] + xs[j]) / 2.f, my = (ys[i] + ys[j]) / 2.f;
            c.line(xs[i], ys[i], xs[j], ys[j], fx ? pcolours::edgeTone(hpc, pcol(g, s.home), mx, my, true) : pcol(g, s.home), 3.f, 230);
            if (fx) {
                const float ml = std::max(0.01f, std::hypot(cx - mx, cy - my));
                pcolours::edgeExtras(c, hpc, xs[i], ys[i], xs[j], ys[j], (cx - mx) / ml, (cy - my) / ml, 8.f, uint32_t(i) + 40u);
            }
        }
    }
    c.setClip(keep);
}

}  // namespace tileview
