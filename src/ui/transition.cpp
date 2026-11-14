#include "ui/transition.h"

#include <algorithm>
#include <cmath>

#include "gfx/canvas.h"
#include "gfx/font.h"
#include "platform/platform.h"
#include "ui/theme.h"

namespace transition {
bool pending = false;
int style = 0;
void pickStyle() {  // (display only: never the game's dice)
    static uint32_t n = 0;
    uint32_t h = (monotonicMs() + 0x9e3779b9u * ++n) * 2654435761u;
    h ^= h >> 16;
    style = int(h & 1u);
}

namespace {
constexpr int kCellW = 9, kCellH = 13, kRows = 480 / kCellH + 1;

// Widescreen (0.46.2 - owner: the flood left the screen underneath showing at the sides): everything spans the whole
// canvas, even from a screen drawn centred - x0 is the canvas's left edge in the view's units, W its width.
struct Whole {
    Canvas& c;
    Rect old;
    int x0, W;
    explicit Whole(Canvas& cc) : c(cc), old(cc.clip()), x0(-cc.viewX()), W(cc.fullWidth()) { c.setClip(Rect{x0, 0, W, c.height()}); }
    ~Whole() { c.setClip(old); }
};

uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 73856093u ^ b * 19349663u ^ c * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

// A 3x5 pixel "0" or "1", each pixel 2x2.
void glyph(Canvas& c, int x, int y, bool one, uint32_t col, int a) {
    if (one) {
        c.fillRect(x + 2, y, 2, 10, col, a);
        c.fillRect(x, y + 2, 2, 2, col, a);
        c.fillRect(x, y + 8, 6, 2, col, a);
    } else {
        c.fillRect(x, y, 6, 2, col, a);
        c.fillRect(x, y + 8, 6, 2, col, a);
        c.fillRect(x, y + 2, 2, 6, col, a);
        c.fillRect(x + 4, y + 2, 2, 6, col, a);
    }
}

// 0.58.3 (owner: "replace the 0s and 1s with 0s, 1s, and random lazaxian font"): a Lazaxian letter in the cell,
// 7 x 11 single pixels (tools/gen_laz_glyphs.py), each row's runs as one rectangle.
#include "ui/gen/laz_glyphs.inc"
void lazGlyph(Canvas& c, int x, int y, int letter, uint32_t col, int a) {
    const uint8_t* rows = kLazGlyphs[letter % 26];
    for (int r = 0; r < 11; ++r) {
        const unsigned bits = rows[r];
        for (int b = 0; b < 7;) {
            if (!(bits & (1u << b))) {
                ++b;
                continue;
            }
            int e = b;
            while (e < 7 && (bits & (1u << e))) ++e;
            c.fillRect(x + b, y + r, e - b, 1, col, a);
            b = e;
        }
    }
}

// Covers each row from both edges up to front(row) units in (0 = nothing, half the width+ = all).
void flood(Canvas& c, float reach, uint32_t t, bool back) {
    Whole wh(c);
    const int x0 = wh.x0, W = wh.W, kCols = W / kCellW + 1;
    reach *= float(W) / 640.f;  // (the same timing on a wider screen)
    const float tt = float(t) / 1000.f;
    for (int row = 0; row < kRows; ++row) {
        const int y = row * kCellH;
        // a ripple along the front, a little different on each side
        const float wl = reach + 16.f * std::sin(float(row) * 0.55f + tt * 7.f) + 8.f * std::sin(float(row) * 1.7f - tt * 4.f);
        const float wr = reach + 16.f * std::sin(float(row) * 0.48f - tt * 6.3f + 1.3f) + 8.f * std::sin(float(row) * 1.3f + tt * 5.f);
        const int l = std::max(0, std::min(W, int(wl))), r = std::max(0, std::min(W, int(wr)));
        if (l + r >= W) {
            c.fillRect(x0, y, W, kCellH, rgb(2, 6, 18), 255);
        } else {
            if (l > 0) c.fillRect(x0, y, l, kCellH, rgb(2, 6, 18), 255);
            if (r > 0) c.fillRect(x0 + W - r, y, r, kCellH, rgb(2, 6, 18), 255);
        }
        // 0.43.0 (owner, P8): a hologram - dimmer digits that flicker a little, scanlines over them
        const float flick = 0.82f + 0.1f * std::sin(tt * 23.f + float(row) * 0.9f) + 0.06f * std::sin(tt * 61.f);
        for (int col = 0; col < kCols; ++col) {
            const int x = col * kCellW;
            const float fromL = wl - float(x), fromR = wr - float(W - x - kCellW);
            const float inside = std::max(fromL, fromR);  // how far behind the nearer front
            if (inside < 0.f) continue;
            const uint32_t h = hash3(uint32_t(col), uint32_t(row), t / 110u + uint32_t(col * 7 + row * 3) % 5u);
            const bool one = (h & 1u) != 0;
            const uint32_t h2 = hash3(h, 0x1a2au, 77u);
            const bool laz = h2 % 100u < 30u;  // about a third of the cells: a Lazaxian letter
            uint32_t colr;
            int a;
            const bool red = laz && style == 1;  // (0.58.4: the red flood's letters)
            if (inside < 26.f) {  // the crest: pale cyan (pale rose)
                colr = red ? mix(rgb(185, 90, 85), rgb(235, 180, 170), int((1.f - inside / 26.f) * 255.f))
                           : mix(rgb(90, 190, 225), rgb(190, 235, 245), int((1.f - inside / 26.f) * 255.f));
                a = 215;
            } else {
                const int b = int((h >> 4) % 100u);
                colr = red ? (b < 12 ? rgb(200, 105, 95) : b < 55 ? rgb(160, 60, 58) : rgb(118, 40, 44))
                           : (b < 12 ? rgb(120, 200, 230) : b < 55 ? rgb(40, 130, 205) : rgb(24, 80, 170));
                a = back ? 160 : 120 + int((h >> 12) % 70u);
            }
            if (laz) lazGlyph(c, x0 + x + 1, y + 1, int((h2 >> 8) % 26u), colr, int(float(a) * flick));
            else glyph(c, x0 + x + 1, y + 1, one, colr, int(float(a) * flick));
        }
        // scanlines across the covered part of the row
        for (int sy = 2; sy < kCellH; sy += 3) {
            if (l + r >= W) c.fillRect(x0, y + sy, W, 1, rgb(0, 0, 0), 90);
            else {
                if (l > 0) c.fillRect(x0, y + sy, l, 1, rgb(0, 0, 0), 90);
                if (r > 0) c.fillRect(x0 + W - r, y + sy, r, 1, rgb(0, 0, 0), 90);
            }
        }
    }
}

float ease(float k) { return k * k * (3.f - 2.f * k); }
}  // namespace

void drawCover(Canvas& c, uint32_t t) {
    // straight over the New Game screen: the flood covers it (0.28.1, owner)
    const float k = std::min(1.f, float(t > kFade ? t - kFade : 0u) / float(kFlood));
    flood(c, ease(k) * 360.f - 20.f, t, false);
}

void drawReveal(Canvas& c, uint32_t t) {
    if (t < kReveal) {
        const float k = float(t) / float(kReveal);
        flood(c, (1.f - ease(k)) * 360.f - 20.f, t + kFade + kFlood, true);
        return;
    }
    const uint32_t b = t - kReveal;
    if (b >= kBanner) return;
    // GAME START!: slams in (scale 1.6 -> 1), a shine sweeps across, fades out at the end
    const float in = std::min(1.f, float(b) / 180.f);
    const float s = 1.6f - 0.6f * ease(in);
    const int a = b < 180 ? int(255.f * in) : b > kBanner - 350 ? int(255.f * float(kBanner - b) / 350.f) : 255;
    Whole wh(c);  // (widescreen: centred on the whole screen)
    const int w = int(460.f * s), h = int(74.f * s);
    const int x = wh.x0 + wh.W / 2 - w / 2, y = 200 - h / 2;
    // the banner: dark plate with angled ends and gold rims
    const float xs[6] = {float(x), float(x + 24), float(x + w - 24), float(x + w), float(x + w - 24), float(x + 24)};
    const float ys[6] = {float(y + h / 2), float(y), float(y), float(y + h / 2), float(y + h), float(y + h)};
    c.fillRect(wh.x0, y - 6, wh.W, h + 12, rgb(0, 0, 0), a * 110 / 255);
    c.fillPolygon(xs, ys, 6, rgb(18, 14, 6), a * 235 / 255);
    c.fillRect(x + 24, y, w - 48, 3, theme::gold, a);
    c.fillRect(x + 24, y + h - 3, w - 48, 3, theme::gold, a);
    c.fillRect(x + 40, y + 7, w - 80, 1, theme::goldDim, a);
    c.fillRect(x + 40, y + h - 8, w - 80, 1, theme::goldDim, a);
    drawTextAligned(c, FONT_XL, wh.x0, y + h / 2 - 14, wh.W, Align::Center, "GAME START!", mix(theme::gold, rgb(255, 245, 200), 90), a);
    // the shine: a bright slanted band crossing once
    const float sk = std::min(1.f, float(b) / 700.f);
    const int sx = x - 60 + int(float(w + 120) * sk);
    for (int i = 0; i < 26; ++i) c.fillRect(sx + i - (i / 2), y + 3, 2, h - 6, rgb(255, 250, 220), (a * (26 - std::abs(i - 13) * 2)) / 255 / 2);
}
}  // namespace transition
