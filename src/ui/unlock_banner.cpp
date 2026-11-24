#include "ui/unlock_banner.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/input.h"
#include "gfx/font.h"
#include "gfx/image.h"
#include "ui/emblems.h"
#include "ui/icons.h"
#include "ui/pcolors.h"
#include "ui/portraits.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace unlockbanner {

namespace {

float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
float easeOut(float v) {
    v = clamp01(v);
    return 1.f - (1.f - v) * (1.f - v) * (1.f - v);
}
float easeBack(float v) {  // a little past 1, then settles
    v = clamp01(v);
    const float s = 1.70158f;
    v -= 1.f;
    return 1.f + v * v * ((s + 1.f) * v + s);
}
float span(uint32_t t, uint32_t a, uint32_t len) { return clamp01(float(int32_t(t - a)) / float(len)); }
uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
uint32_t lerp(uint32_t a, uint32_t b, float k) { return mix(a, b, int(clamp01(k) * 255.f + 0.5f)); }

// a clip inside the current one, put back when it goes
struct ClipIn {
    Canvas& c;
    Rect old;
    ClipIn(Canvas& cv, Rect r) : c(cv), old(cv.clip()) {
        const int x0 = std::max(r.x, old.x), y0 = std::max(r.y, old.y);
        const int x1 = std::min(r.x + r.w, old.x + old.w), y1 = std::min(r.y + r.h, old.y + old.h);
        c.setClip(Rect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)});
    }
    ~ClipIn() { c.setClip(old); }
};

constexpr uint32_t kCyan = rgb(80, 240, 255), kMagenta = rgb(255, 60, 200), kPink = rgb(255, 92, 170), kViolet = rgb(150, 70, 255);

// ---------------------------------------------------------------- the scene
struct Stage {
    int W, top, bottom, horizon;  // the band, in game units
    float gx, gy;                 // where the graphic stands (in front of the sun)
};

void sky(Canvas& c, const Stage& s, uint32_t now) {
    c.verticalGradient(0, s.top, s.W, s.horizon - s.top, rgb(8, 3, 30), rgb(118, 20, 104));
    c.verticalGradient(0, s.horizon, s.W, s.bottom - s.horizon, rgb(40, 6, 58), rgb(5, 2, 16));
    // stars, twinkling, thinning toward the horizon
    for (int i = 0; i < 90; ++i) {
        const uint32_t h = hash(uint32_t(i) * 2654435761u + 17u);
        const float x = float(h % uint32_t(s.W));
        const float y = float(s.top + 4) + float((h >> 12) % 1000u) / 1000.f * float(s.horizon - s.top - 40);
        const float tw = 0.5f + 0.5f * std::sin(float(now % 100000u) / 1000.f * (1.2f + float(h % 7u) * 0.3f) + float(h % 628u) / 100.f);
        const int a = int((40.f + 170.f * tw) * (1.f - (y - float(s.top)) / float(s.horizon - s.top)));
        const float r = (h & 0x300000u) ? 0.7f : 1.2f;
        c.fillCircle(x, y, r, (h & 0x40u) ? rgb(200, 230, 255) : rgb(255, 200, 240), a);
    }
}

// the synthwave sun: yellow to pink, cut by bands that widen toward the horizon; drawn in real pixels for a clean edge
void sun(Canvas& c, const Stage& s, float rise, uint32_t now) {
    const float R = 100.f;
    const float cy = float(s.horizon) - 34.f + (1.f - rise) * (R + 40.f);
    // the glow round it
    for (int k = 4; k >= 1; --k) c.fillCircle(s.gx, cy, R + float(k) * 16.f, kMagenta, int(14.f * rise));
    const int py0 = c.Y(int(cy - R)), py1 = std::min(c.Y(s.horizon), c.Y(int(cy + R)));
    const float sc = float(c.Y(1000)) / 1000.f;
    const float pcy = float(c.Y(0)) + cy * sc, pR = R * sc, pcx = float(c.X(0)) + s.gx * sc;
    const float scroll = float(now % 4000u) / 4000.f;
    for (int py = std::max(0, py0); py < py1; ++py) {
        const float dy = (float(py) + 0.5f - pcy) / pR;  // -1 .. 1
        if (dy * dy >= 1.f) continue;
        // the cuts: from a little above the middle, thicker and closer toward the bottom, drifting down
        if (dy > -0.05f) {
            const float u = (dy + 0.05f) / 1.05f;
            const float period = 0.16f - 0.06f * u;
            const float ph = std::fmod(u / period + scroll, 1.f);
            if (ph < 0.12f + 0.42f * u) continue;
        }
        const float hw = std::sqrt(1.f - dy * dy) * pR;
        const float k = (dy + 1.f) * 0.5f;
        const uint32_t col = k < 0.5f ? lerp(rgb(255, 236, 120), rgb(255, 140, 80), k * 2.f) : lerp(rgb(255, 140, 80), rgb(255, 40, 150), (k - 0.5f) * 2.f);
        c.pxSpan(int(pcx - hw), int(pcx + hw + 1.f), py, col, int(255.f * rise));
    }
}

void grid(Canvas& c, const Stage& s, uint32_t now) {
    const float depth = float(s.bottom - s.horizon);
    // rows rushing toward you
    const float scroll = float(now % 1200u) / 1200.f;
    for (int i = 0; i < 14; ++i) {
        const float p = (float(i) + scroll) / 14.f;
        const float y = float(s.horizon) + depth * p * p;
        const int a = int(40.f + 190.f * p);
        c.hline(0, s.W, int(y), kMagenta, a);
    }
    // lines to the vanishing point
    const float vx = float(s.W) * 0.5f;
    for (int i = -18; i <= 18; ++i) {
        const float xb = vx + float(i) * float(s.W) / 11.f;
        c.line(vx + float(i) * 6.f, float(s.horizon), xb, float(s.bottom), kMagenta, 1.2f, 150);
    }
    // the horizon line and its haze
    c.fillRect(0, s.horizon - 6, s.W, 6, kPink, 34);
    c.fillRect(0, s.horizon - 2, s.W, 2, kPink, 90);
    c.hline(0, s.W, s.horizon, rgb(255, 190, 240), 230);
}

// the band's neon edges
void edges(Canvas& c, const Stage& s) {
    for (int y : {s.top, s.bottom - 3}) {
        c.fillRect(0, y - 3, s.W, 9, kCyan, 26);
        c.fillRect(0, y, s.W, 3, kCyan, 230);
        c.hline(0, s.W, y + (y == s.top ? 4 : -2), kMagenta, 200);
    }
}

// ---------------------------------------------------------------- the graphics
void poly(Canvas& c, const std::vector<std::pair<float, float>>& pts, float ox, float oy, float k, uint32_t col, int a) {
    std::vector<float> xs, ys;
    for (const auto& p : pts) xs.push_back(ox + p.first * k), ys.push_back(oy + p.second * k);
    c.fillPolygon(xs.data(), ys.data(), int(xs.size()), col, a);
}
std::vector<std::pair<float, float>> ellipse(float cx, float cy, float rx, float ry) {
    std::vector<std::pair<float, float>> v;
    for (int i = 0; i < 28; ++i) {
        const float a = float(i) / 28.f * 6.2831853f;
        v.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
    }
    return v;
}

// A commander of the old empire, in silhouette against the sun: a crested helm with swept-back wings, a high standing collar,
// a cloak over rounded pauldrons, a visor's glow and their faction's mark on the chest - rimmed in neon.
void rival(Canvas& c, float cx, float cy, float k, int faction, uint32_t now, int a) {
    std::vector<std::vector<std::pair<float, float>>> parts = {
        // the cloak over the shoulders (running off the bottom)
        {{-116, 140}, {-112, 96}, {-106, 66}, {-96, 46}, {-82, 32}, {-62, 21}, {-40, 14}, {-20, 11}, {20, 11}, {40, 14}, {62, 21}, {82, 32},
         {96, 46}, {106, 66}, {112, 96}, {116, 140}},
        // the rounded pauldrons
        ellipse(-70, 32, 29, 15),
        ellipse(70, 32, 29, 15),
        // the high standing collar, flaring
        {{-30, 16}, {-46, -34}, {-34, -30}, {-24, -12}, {24, -12}, {34, -30}, {46, -34}, {30, 16}},
        // the neck
        {{-12, -26}, {12, -26}, {13, 8}, {-13, 8}},
        // the head
        ellipse(0, -56, 21, 27),
        // the helm's crest, swept back in one curve
        {{-5, -78}, {-4, -92}, {0, -104}, {7, -114}, {16, -120}, {12, -106}, {9, -92}, {7, -79}},
        // the helm's wings at the temples
        {{-18, -66}, {-26, -76}, {-40, -94}, {-34, -74}, {-24, -54}},
        {{18, -66}, {26, -76}, {40, -94}, {34, -74}, {24, -54}},
    };
    // the rim lights: cyan from the right, magenta from the left
    for (const auto& p : parts) poly(c, p, cx + 3.f * k, cy - 1.f * k, k, kCyan, a * 235 / 255);
    for (const auto& p : parts) poly(c, p, cx - 3.f * k, cy - 1.f * k, k, kMagenta, a * 210 / 255);
    for (const auto& p : parts) poly(c, p, cx, cy, k, rgb(10, 4, 22), a);
    // the cloak's folds and the pauldrons' edges, faintly lit
    for (int sgn : {-1, 1}) {
        const uint32_t fold = sgn < 0 ? rgb(80, 34, 120) : rgb(40, 80, 130);
        c.line(cx + float(sgn) * 30.f * k, cy + 16.f * k, cx + float(sgn) * 50.f * k, cy + 138.f * k, fold, 1.4f * k, a / 2);
        c.line(cx + float(sgn) * 64.f * k, cy + 44.f * k, cx + float(sgn) * 84.f * k, cy + 138.f * k, fold, 1.2f * k, a / 3);
    }
    // the clasp of the cloak
    const float pulse = 0.6f + 0.4f * std::sin(float(now % 2400u) / 2400.f * 6.2831853f);
    c.fillCircle(cx, cy + 14.f * k, 7.f * k, kMagenta, int(60.f * pulse) * a / 255);
    c.fillCircle(cx, cy + 14.f * k, 3.5f * k, rgb(255, 170, 230), a);
    // the visor's glow, breathing
    c.fillRoundRect(int(cx - 17.f * k), int(cy - 60.f * k), int(34.f * k), int(6.f * k), int(3.f * k), kCyan, int(70.f * pulse) * a / 255);
    c.fillRoundRect(int(cx - 14.f * k), int(cy - 59.f * k), int(28.f * k), int(3.f * k), 1, rgb(210, 255, 255), int(235.f * pulse) * a / 255);
    // their mark on the chest
    if (faction >= 0) {
        const float r = 17.f * k, my = cy + 62.f * k;
        c.fillCircle(cx, my, r + 4.f * k, rgb(20, 10, 40), a);
        c.ring(cx, my, r + 4.f * k, 1.5f * k, kCyan, a * 200 / 255);
        drawFactionIcon(c, faction, int(cx - r), int(my - r), int(2.f * r), a * 220 / 255);
    }
}

// a stretch of a territory's border in that colour (as the map draws it), a dreadnought and a space dock
void colourSample(Canvas& c, float cx, float cy, int col, uint32_t now, int a) {
    const uint32_t base = pcolours::base(col);
    static const float kCos[6] = {1.f, 0.5f, -0.5f, -1.f, -0.5f, 0.5f};
    static const float kSin[6] = {0.f, 0.8660254f, 0.8660254f, 0.f, -0.8660254f, -0.8660254f};
    const float size = 96.f, hx = cx, hy = cy + 8.f;
    {
        pcolours::Finish fin(c, Rect{int(hx - size - 4), int(hy - size - 4), int(2 * size + 8), int(2 * size + 8)}, col, 401u);
        {  // the system's tile, dark, so the colour reads against it (not against the sun)
            float xs[6], ys[6];
            for (int e = 0; e < 6; ++e) xs[e] = hx + size * kCos[e], ys[e] = hy + size * kSin[e];
            c.fillPolygon(xs, ys, 6, rgb(12, 6, 28), a * 225 / 255);
        }
        for (int e = 0; e < 6; ++e) {
            const int i1 = (4 + e) % 6, i2 = (5 + e) % 6;
            c.line(hx + size * kCos[i1], hy + size * kSin[i1], hx + size * kCos[i2], hy + size * kSin[i2], rgb(90, 80, 130), 1.f, a * 160 / 255);
        }
        int r = int((base >> 16) & 255), g = int((base >> 8) & 255), b = int(base & 255);
        const int l = (r * 3 + g * 6 + b) / 10;
        r = (r * 3 + l) / 4, g = (g * 3 + l) / 4, b = (b * 3 + l) / 4;
        const uint32_t hi = mix(rgb(r, g, b), rgb(255, 255, 255), 100);
        const float th = std::max(3.f, size * 0.07f), r0 = size - 1.f, bw = size * 0.3f;
        static const int kBand[3] = {90, 50, 22};
        for (int e : {0, 1, 2, 5}) {
            const int i1 = (4 + e) % 6, i2 = (5 + e) % 6;
            auto strip = [&](float ra, float rb, uint32_t colr, int al) {
                float xs[4] = {hx + ra * kCos[i1], hx + ra * kCos[i2], hx + rb * kCos[i2], hx + rb * kCos[i1]};
                float ys[4] = {hy + ra * kSin[i1], hy + ra * kSin[i2], hy + rb * kSin[i2], hy + rb * kSin[i1]};
                c.fillPolygon(xs, ys, 4, colr, al * a / 255);
            };
            const float rin = r0 - th;
            for (int q = 0; q < 3; ++q) strip(rin - bw * float(q) / 3.f, rin - bw * float(q + 1) / 3.f, base, kBand[q] * 2);
            strip(size - 1.f, rin, hi, 240);
        }
    }
    {
        pcolours::Finish fin(c, Rect{int(cx + 4), int(cy + 12), 72, 62}, col, 402u);
        icons::drawUnitArt(c, "unit/spacedock", cx + 40.f, cy + 44.f, 64, 56, base);
    }
    {
        pcolours::Finish fin(c, Rect{int(cx - 78), int(cy - 46), 128, 60}, col, 403u);
        icons::drawUnitArt(c, "unit/dreadnought", cx - 14.f, cy - 16.f, 124, 54, base);
    }
    (void)now;
}

// the emblem, large, with slow rays turning behind it
void emblem(Canvas& c, float cx, float cy, const std::string& key, uint32_t colour, uint32_t now, int a) {
    const float turn = float(now % 12000u) / 12000.f * 6.2831853f;
    for (int i = 0; i < 12; ++i) {
        const float ang = turn + float(i) / 12.f * 6.2831853f;
        float xs[3] = {cx, cx + 136.f * std::cos(ang - 0.07f), cx + 136.f * std::cos(ang + 0.07f)};
        float ys[3] = {cy, cy + 136.f * std::sin(ang - 0.07f), cy + 136.f * std::sin(ang + 0.07f)};
        c.fillPolygon(xs, ys, 3, rgb(255, 200, 110), a * 40 / 255);
    }
    emblems::draw(c, key, cx, cy, 84.f, colour, emblems::kEarned, now);
}

// the faction's picture in a frame, its codec portrait live in the corner
void factionArt(Canvas& c, float cx, float cy, int faction, const Image* pic, uint32_t now, int a) {
    const Rect f{int(cx - 100), int(cy - 104), 200, 200};
    c.fillRoundRect(f.x - 6, f.y - 6, f.w + 12, f.h + 12, 14, rgb(6, 2, 18), a * 230 / 255);
    if (pic) {
        ClipIn clip(c, f);
        c.blit(*pic, f.x, f.y, a);
    } else {  // (draft: the owner's picture goes here)
        c.verticalGradient(f.x, f.y, f.w, f.h, rgb(40, 18, 80), rgb(16, 6, 34));
        {
            ClipIn clip(c, f);
            for (int d = -f.h; d < f.w; d += 14)
                c.line(float(f.x + d), float(f.y + f.h), float(f.x + d + f.h), float(f.y), rgb(90, 50, 150), 1.f, a * 70 / 255);
        }
        drawTextAligned(c, FONT_SMB, f.x, f.y + f.h / 2 - 16, f.w, Align::Center, "The faction's picture", rgb(230, 200, 255));
        drawTextAligned(c, FONT_XS, f.x, f.y + f.h / 2 + 2, f.w, Align::Center, "(yours, in hi-def Retrowave)", rgb(170, 140, 210));
    }
    c.frameRoundRect(f.x - 4, f.y - 4, f.w + 8, f.h + 8, 12, kCyan, 2, a);
    c.frameRoundRect(f.x - 8, f.y - 8, f.w + 16, f.h + 16, 15, kMagenta, 1, a * 180 / 255);
    // the codec window
    const Rect p{f.x + f.w - 70, f.y + f.h - 58, 92, 92};
    c.fillRoundRect(p.x - 5, p.y - 5, p.w + 10, p.h + 10, 8, rgb(4, 2, 12), a);
    if (faction >= 0 && portraits::has(faction)) portraits::draw(c, faction, p.x, p.y, p.w, now, a);
    else c.fillRect(p.x, p.y, p.w, p.h, rgb(20, 30, 50), a);
    for (int y = p.y; y < p.y + p.h; y += 3) c.hline(p.x, p.x + p.w, y, rgb(0, 0, 0), a * 50 / 255);
    c.frameRoundRect(p.x - 3, p.y - 3, p.w + 6, p.h + 6, 6, kCyan, 2, a);
    const bool on = (now / 600u) % 2u == 0u;
    c.fillCircle(float(p.x + 8), float(p.y - 12), 3.f, on ? rgb(255, 70, 90) : rgb(120, 30, 40), a);
    drawText(c, FONT_XS, p.x + 14, p.y - 18, "LIVE", kCyan, a);
}

// chrome letters: a magenta outline, pale cyan above the middle, gold-to-pink below it
void chrome(Canvas& c, const FontData& f, int x, int y, const std::string& s, int a) {
    for (int dx = -2; dx <= 2; dx += 2)
        for (int dy = -2; dy <= 2; dy += 2)
            if (dx || dy) drawText(c, f, x + dx, y + dy, s, kMagenta, a * 160 / 255);
    drawText(c, f, x + 3, y + 4, s, rgb(30, 0, 40), a);
    const int h = f.lineHeight;
    const int w = textWidth(f, s);
    {
        ClipIn clip(c, Rect{x - 4, y - 4, w + 8, h / 2 + 4});
        drawText(c, f, x, y, s, rgb(225, 250, 255), a);
    }
    {
        ClipIn clip(c, Rect{x - 4, y + h / 2, w + 8, h});
        drawText(c, f, x, y, s, rgb(255, 176, 96), a);
    }
    {
        ClipIn clip(c, Rect{x - 4, y + h / 2, w + 8, 2});
        drawText(c, f, x, y, s, rgb(255, 255, 255), a);
    }
}

}  // namespace

void draw(Canvas& c, const Unlock& u, uint32_t t, uint32_t now) {
    const int W = c.width();
    Stage s{W, 86, 398, 318, float(W) * 0.31f, 236.f};
    // the screen dims, the band opens from the middle
    c.fillRect(0, 0, W, 480, rgb(0, 0, 0), int(200.f * easeOut(span(t, 0, 300))));
    const float open = easeOut(span(t, 60, 420));
    if (open <= 0.f) return;
    const int half = int(float(W) * 0.5f * open + 0.5f);
    c.setClip(Rect{W / 2 - half, s.top - 6, 2 * half, s.bottom - s.top + 12});
    sky(c, s, now);
    sun(c, s, easeOut(span(t, 200, 800)), now);
    grid(c, s, now);
    // the graphic: grows in with a little overshoot, then a flash
    const float g = span(t, 520, 420);
    if (g > 0.f) {
        const float k = 0.55f + 0.45f * easeBack(g);
        const int a = int(255.f * clamp01(g * 2.f));
        switch (u.kind) {
            case kRival: rival(c, s.gx, s.gy - 8.f, 0.95f * k, u.faction, now, a); break;
            case kColour: colourSample(c, s.gx, s.gy - 18.f, u.colour, now, a); break;
            case kEmblem: emblem(c, s.gx, s.gy - 16.f, u.emblem, u.emblemColour, now, a); break;
            case kFaction: factionArt(c, s.gx, s.gy - 10.f, u.faction, u.picture, now, a); break;
        }
        const float fl = span(t, 820, 520);
        if (fl > 0.f && fl < 1.f) {
            c.ring(s.gx, s.gy - 20.f, 30.f + 160.f * fl, 3.f, rgb(255, 255, 255), int(220.f * (1.f - fl)));
            c.fillCircle(s.gx, s.gy - 20.f, 90.f * (1.f - fl), rgb(255, 255, 255), int(90.f * (1.f - fl)));
        }
    }
    // the words, sliding in from the right
    const float w = easeOut(span(t, 700, 500));
    if (w > 0.f) {
        const int a = int(255.f * w);
        const int tx = int(float(W) * 0.555f) + int((1.f - w) * 50.f), tw = W - tx - 24;
        chrome(c, FONT_XL, tx, s.top + 30, "UNLOCKED", a);
        int y = s.top + 82;
        drawText(c, FONT_SMB, tx + 2, y, u.category, kCyan, a);
        c.fillRect(tx + 2, y + 18, std::min(tw, textWidth(FONT_SMB, u.category) + 30), 2, kCyan, a * 120 / 255);
        y += 28;
        for (const std::string& l : wrapText(FONT_LG, u.name, tw)) {
            drawText(c, FONT_LG, tx + 2, y, l, rgb(255, 255, 255), a);
            y += 26;
        }
        y += 6;
        for (const std::string& l : wrapText(FONT_SM, u.line, tw)) {
            drawText(c, FONT_SM, tx + 2, y, l, rgb(255, 214, 240), a);
            y += 17;
        }
        if (!u.source.empty()) drawText(c, FONT_XS, tx + 2, y + 6, fitText(FONT_XS, u.source, tw), rgb(190, 150, 220), a);
    }
    // sparks rising from the horizon
    for (int i = 0; i < 26; ++i) {
        const uint32_t h = hash(uint32_t(i) * 977u + 5u);
        const float life = std::fmod(float(now % 100000u) / float(1400u + h % 1200u) + float(h % 100u) / 100.f, 1.f);
        const float x = float(h % uint32_t(W)) + std::sin(life * 6.f + float(i)) * 6.f;
        const float y = float(s.horizon) - life * 150.f;
        c.fillCircle(x, y, 1.3f, (h & 1u) ? kCyan : kPink, int(200.f * (1.f - life) * open));
    }
    // scanlines over the whole band
    for (int y = s.top; y < s.bottom; y += 3) c.hline(0, W, y, rgb(0, 0, 0), 22);
    c.resetClip();
    edges(c, s);
    if (ready(t)) {
        const float p = 0.5f + 0.5f * std::sin(float(now % 1600u) / 1600.f * 6.2831853f);
        drawTextAligned(c, FONT_SMB, 0, s.bottom + 16, W, Align::Center, keyText("A: Continue"), rgb(255, 220, 245), 150 + int(105.f * p));
    }
    (void)kViolet;
}

}  // namespace unlockbanner
