#include "ui/fracture_ripple.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "gfx/font.h"

namespace ripple {
namespace {

constexpr float kPi = 3.14159265f;
constexpr uint32_t kTravel = 2700;  // ms for the first ring to cross the whole map

float clamp01(float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; }
uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float hashf(uint32_t x) { return float(hash32(x) & 0xffffffu) / 16777215.f; }
inline uint32_t addc(uint32_t d, int r, int g, int b) {
    return rgb(std::min(255, int((d >> 16) & 255) + r), std::min(255, int((d >> 8) & 255) + g), std::min(255, int(d & 255) + b));
}

struct Ring {
    float r, w, amp;  // where its crest is, its half-width and its strength (real pixels)
};
// the ring that left the epicentre `lag` ms after the first; how far it has gone over the map
bool ringAt(uint32_t t, uint32_t lag, float maxD, float s, float strength, Ring& out) {
    if (t < lag) return false;
    const float q = float(t - lag) / float(kTravel);
    if (q >= 1.f) return false;
    const float e = 1.f - (1.f - q) * (1.f - q);  // a burst, slowing as it spreads
    out.r = maxD * e;
    out.w = (26.f + 26.f * q) * s;
    out.amp = (15.f - 8.f * q) * s * strength * (q > 0.82f ? (1.f - q) / 0.18f : 1.f) * clamp01(q * 12.f);
    return true;
}

std::vector<uint32_t> g_src;

}  // namespace

void apply(Canvas& c, Rect area, float cx, float cy, uint32_t t) {
    if (t >= kLength) return;
    const float s = c.scale();
    const Rect P = c.toPx(area);
    const int pw = c.pw(), ph = c.ph();
    const int x0 = std::max(0, P.x), y0 = std::max(0, P.y), x1 = std::min(pw, P.x + P.w), y1 = std::min(ph, P.y + P.h);
    if (x0 >= x1 || y0 >= y1) return;
    const float ex = float(c.X(0)) + cx * s, ey = cy * s;  // the epicentre in real pixels
    float maxD = 0.f;
    for (int k = 0; k < 4; ++k) {
        const float dx = float(k & 1 ? x1 : x0) - ex, dy = float(k & 2 ? y1 : y0) - ey;
        maxD = std::max(maxD, std::sqrt(dx * dx + dy * dy));
    }
    maxD += 90.f * s;
    Ring rings[2];
    int n = 0;
    if (ringAt(t, 0, maxD, s, 1.f, rings[n])) ++n;
    if (ringAt(t, 380, maxD, s, 0.55f, rings[n])) ++n;
    uint32_t* px = c.pixels();
    // reality bleeding: the whole map dims to crimson for a moment (two channels at once per multiply)
    if (t < 1600) {
        const int a = int(70.f * (t < 200 ? float(t) / 200.f : 1.f - float(t - 200) / 1400.f));
        const uint32_t keep = uint32_t(256 - a);
        const uint32_t tint = rgb(40 * a / 256, 0, 18 * a / 256);
        for (int y = y0; y < y1; ++y) {
            uint32_t* row = px + size_t(y) * size_t(pw);
            for (int x = x0; x < x1; ++x) {
                const uint32_t p = row[x];
                row[x] = ((((p & 0xff00ffu) * keep) >> 8) & 0xff00ffu) + ((((p & 0x00ff00u) * keep) >> 8) & 0x00ff00u) + tint;
            }
        }
    }
    if (n > 0) {
        // the picture as it was (each ring reads from it)
        const int w = x1 - x0;
        g_src.resize(size_t(w) * size_t(y1 - y0));
        for (int y = y0; y < y1; ++y) std::memcpy(&g_src[size_t(y - y0) * size_t(w)], px + size_t(y) * size_t(pw) + x0, size_t(w) * 4u);
        auto src = [&](int sx, int sy) {
            sx = std::max(x0, std::min(x1 - 1, sx));
            sy = std::max(y0, std::min(y1 - 1, sy));
            return g_src[size_t(sy - y0) * size_t(w) + size_t(sx - x0)];
        };
        // one wave's shape for u = -1 .. 1: (1 - u^2)^2 sin(3 pi u), in a table
        static float kWave[257], kEnv[257];
        static bool made = false;
        if (!made) {
            for (int i = 0; i <= 256; ++i) {
                const float u = float(i) / 128.f - 1.f, e = (1.f - u * u) * (1.f - u * u);
                kEnv[i] = e;
                kWave[i] = e * std::sin(u * 3.f * kPi);
            }
            made = true;
        }
        float rIn = 1e9f, rOut = 0.f;
        for (int k = 0; k < n; ++k) {
            rIn = std::min(rIn, rings[k].r - rings[k].w);
            rOut = std::max(rOut, rings[k].r + rings[k].w);
        }
        rIn = std::max(0.f, rIn);
        // worked out once for each 2 x 2 block of pixels (the waves are smooth), applied to its four
        for (int y = y0 - ((y0 - int(ey)) & 1); y < y1; y += 2) {
            const float dy = float(y) + 1.f - ey;
            const float o2 = rOut * rOut - dy * dy;
            if (o2 <= 0.f) continue;
            const float xo = std::sqrt(o2), i2 = rIn * rIn - dy * dy, xi = i2 > 0.f ? std::sqrt(i2) - 2.f : 0.f;
            // the annulus on this row: left of the hole, right of it
            const int spans[2][2] = {{int(std::floor(ex - xo)) - 1, int(std::ceil(ex - std::max(0.f, xi)))},
                                     {int(std::floor(ex + std::max(0.f, xi))), int(std::ceil(ex + xo)) + 1}};
            for (int sp = 0; sp < 2; ++sp) {
                int a0 = std::max(x0, spans[sp][0]);
                a0 -= (a0 - x0) & 1;
                const int a1 = std::min(x1, spans[sp][1] + 1);
                for (int x = a0; x < a1; x += 2) {
                    const float dx = float(x) + 1.f - ex, d = std::sqrt(dx * dx + dy * dy);
                    float disp = 0.f, env = 0.f, crest = 0.f;
                    for (int k = 0; k < n; ++k) {
                        const float u = (d - rings[k].r) / rings[k].w;
                        if (u <= -1.f || u >= 1.f) continue;
                        const int i = int((u + 1.f) * 128.f);
                        const float st = rings[k].amp / (15.f * s);
                        disp += rings[k].amp * kWave[i];
                        env = std::max(env, kEnv[i] * st);
                        crest = std::max(crest, std::max(0.f, 1.f - std::fabs(u) * 7.f) * st);
                    }
                    if (env <= 0.01f) continue;
                    const float inv = d > 0.f ? 1.f / d : 0.f, nx = dx * inv, ny = dy * inv;
                    const float split = env * 3.f * s;  // the colours drifting apart
                    const int ox = int(std::lround(nx * disp)), oy = int(std::lround(ny * disp));
                    const int sx = int(std::lround(nx * split)), sy = int(std::lround(ny * split));
                    const int la = int(env * 70.f);
                    const int ar = la * 220 / 255 + int(crest * 120.f), ag = int(crest * 60.f), ab = la * 120 / 255 + int(crest * 110.f);
                    for (int by = 0; by < 2; ++by) {
                        const int yy = y + by;
                        if (yy < y0 || yy >= y1) continue;
                        uint32_t* row = px + size_t(yy) * size_t(pw);
                        for (int bx = 0; bx < 2; ++bx) {
                            const int xx = x + bx;
                            if (xx < x0 || xx >= x1) continue;
                            const int fx = xx - ox, fy = yy - oy;
                            uint32_t col;
                            if (sx == 0 && sy == 0) col = src(fx, fy);
                            else col = (src(fx - sx, fy - sy) & 0xff0000u) | (src(fx, fy) & 0x00ff00u) | (src(fx + sx, fy + sy) & 0x0000ffu);
                            row[xx] = addc(col, ar, ag, ab);  // a crimson light along the crest, a violet glow in the trough
                        }
                    }
                }
            }
        }
    }
    // where it began: a flash, a ring of light and the tear - jagged bolts of crimson light out of Styx
    if (t < 1200) {
        const float q = float(t) / 1200.f;
        const float fade = 1.f - q;
        c.fillCircle(cx, cy, 14.f + 70.f * q, rgb(255, 40, 110), int(70.f * fade));
        c.fillCircle(cx, cy, 8.f + 26.f * q, rgb(255, 200, 220), int(170.f * fade * fade));
        c.ring(cx, cy, 20.f + 160.f * q, 3.f, rgb(255, 120, 170), int(200.f * fade));
        if (t < 800) {
            const uint32_t slot = t / 60u;  // the bolts flicker
            for (int b = 0; b < 7; ++b) {
                const float ang = float(b) / 7.f * 2.f * kPi + hashf(uint32_t(b) * 977u) * 0.6f;
                const float len = 40.f + 70.f * hashf(uint32_t(b) * 31u + 7u);
                float lx = cx, ly = cy;
                for (int seg = 1; seg <= 6; ++seg) {
                    const float f = float(seg) / 6.f;
                    const float jit = (hashf(slot * 131u + uint32_t(b * 17 + seg)) - 0.5f) * 14.f;
                    const float nxp = cx + std::cos(ang) * len * f - std::sin(ang) * jit, nyp = cy + std::sin(ang) * len * f + std::cos(ang) * jit;
                    c.line(lx, ly, nxp, nyp, rgb(255, 60, 120), 3.f, int(150.f * (1.f - float(t) / 800.f)));
                    c.line(lx, ly, nxp, nyp, rgb(255, 230, 240), 1.2f, int(230.f * (1.f - float(t) / 800.f)));
                    lx = nxp;
                    ly = nyp;
                }
            }
        }
    }
}

void crimson(Canvas& c, float amount, uint32_t t) {
    if (amount <= 0.f) return;
    const float throb = 0.9f + 0.1f * std::sin(float(t) * 0.0072f);  // a slow pulse in it
    const uint32_t a = uint32_t(std::min(1.f, amount * throb) * 236.f);
    const uint32_t keep = 256u - a;
    uint32_t* px = c.pixels();
    const size_t n = size_t(c.pw()) * size_t(c.ph());
    for (size_t i = 0; i < n; ++i) {
        const uint32_t p = px[i];
        const uint32_t r = (p >> 16) & 255u, g = (p >> 8) & 255u, b = p & 255u;
        const uint32_t L = (r * 77u + g * 150u + b * 29u) >> 8;
        // the space turns the colour of blood: the dark a deep crimson, the stars and nebulae red-hot
        const uint32_t tr = std::min(255u, 34u + L * 3u / 2u), tg = L / 8u, tb = 6u + L / 5u;
        px[i] = rgb(int((r * keep + tr * a) >> 8), int((g * keep + tg * a) >> 8), int((b * keep + tb * a) >> 8));
    }
}

void drawLine(Canvas& c, Rect area, const std::string& text, uint32_t t, const std::string& sub) {
    if (text.empty() || t < 150 || t >= kLength) return;
    const uint32_t tt = t - 150;
    const int a = tt < 300 ? int(255.f * float(tt) / 300.f) : t > kLength - 600 ? int(255.f * float(kLength - t) / 600.f) : 255;
    const FontData& f = textWidth(FONT_LG, text) <= area.w - 40 ? FONT_LG : FONT_MDB;
    const int h = f.lineHeight + 14 + (sub.empty() ? 0 : FONT_SMB.lineHeight + 2), y = area.y + 8;
    // a dark band across the top of the map, fading out at its ends
    for (int k = 0; k < 8; ++k) {
        const int inset = k * area.w / 22;
        c.fillRect(area.x + inset, y, area.w - 2 * inset, h, rgb(0, 0, 0), a * 22 / 255);
    }
    c.fillRect(area.x + area.w / 6, y, area.w * 2 / 3, 1, rgb(255, 60, 120), a * 120 / 255);
    c.fillRect(area.x + area.w / 6, y + h - 1, area.w * 2 / 3, 1, rgb(255, 60, 120), a * 120 / 255);
    const int tw = textWidth(f, text), x = area.x + (area.w - tw) / 2, ty = y + 7;
    if (!sub.empty() && tt >= 500) {  // the second line comes a moment later, quietly
        const int sa = a * std::min(255, int(tt - 500) * 255 / 500) / 255;
        const std::string sl = fitText(FONT_SMB, sub, area.w - 30);
        drawTextAligned(c, FONT_SMB, area.x, ty + f.lineHeight + 1, area.w, Align::Center, sl, rgb(255, 150, 170), sa);
    }
    // as it comes: torn - the colours split apart and slices of it slipping sideways, settling
    const float torn = tt < 700 ? 1.f - float(tt) / 700.f : 0.f;
    if (torn > 0.f) {
        const int split = int(1.f + 7.f * torn);
        drawText(c, f, x - split, ty, text, rgb(255, 40, 90), int(float(a) * 0.55f));
        drawText(c, f, x + split, ty, text, rgb(60, 220, 255), int(float(a) * 0.45f));
        const Rect clip = c.clip();
        for (int sy = ty; sy < ty + f.lineHeight; sy += 3) {
            const int jx = int((hashf(uint32_t(sy) * 97u + tt / 50u) - 0.5f) * 18.f * torn);
            c.setClip(Rect{area.x, sy, area.w, 3});
            drawText(c, f, x + jx, ty, text, rgb(255, 225, 235), a);
        }
        c.setClip(clip);
        return;
    }
    for (int k = 0; k < 4; ++k)
        drawText(c, f, x + (k == 0 ? -1 : k == 1 ? 1 : 0), ty + (k == 2 ? -1 : k == 3 ? 1 : 0), text, rgb(220, 30, 90), 75 * a / 255);
    drawText(c, f, x, ty, text, rgb(255, 214, 226), a);
}

}  // namespace ripple
