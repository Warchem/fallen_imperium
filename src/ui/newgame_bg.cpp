#include "ui/newgame_bg.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "gfx/canvas.h"

namespace newgamebg {
namespace {

constexpr float kPi = 3.14159265f;

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float hashf(uint32_t x) { return float(hash32(x) & 0xffffffu) / 16777215.f; }
float smooth01(float t) {
    t = std::max(0.f, std::min(1.f, t));
    return t * t * (3.f - 2.f * t);
}
float smoothstep(float a, float b, float x) { return smooth01((x - a) / (b - a)); }
float clamp01(float v) { return std::max(0.f, std::min(1.f, v)); }

// 3D value noise and fbm (the sky, made once per size)
float vnoise(float x, float y, float z) {
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int ix = int(fx), iy = int(fy), iz = int(fz);
    const float ux = smooth01(x - fx), uy = smooth01(y - fy), uz = smooth01(z - fz);
    auto h = [](int a, int b, int c) { return hashf(uint32_t(a) * 73856093u ^ uint32_t(b) * 19349663u ^ uint32_t(c) * 83492791u); };
    const float x00 = h(ix, iy, iz) + (h(ix + 1, iy, iz) - h(ix, iy, iz)) * ux;
    const float x10 = h(ix, iy + 1, iz) + (h(ix + 1, iy + 1, iz) - h(ix, iy + 1, iz)) * ux;
    const float x01 = h(ix, iy, iz + 1) + (h(ix + 1, iy, iz + 1) - h(ix, iy, iz + 1)) * ux;
    const float x11 = h(ix, iy + 1, iz + 1) + (h(ix + 1, iy + 1, iz + 1) - h(ix, iy + 1, iz + 1)) * ux;
    const float y0 = x00 + (x10 - x00) * uy, y1 = x01 + (x11 - x01) * uy;
    return y0 + (y1 - y0) * uz;
}
float fbm(float x, float y, float z, int oct) {
    float s = 0.f, a = 0.5f, n = 0.f;
    for (int i = 0; i < oct; ++i) {
        s += vnoise(x, y, z) * a;
        n += a;
        x = x * 2.03f + 17.1f;
        y = y * 2.03f + 3.7f;
        z = z * 2.03f + 9.3f;
        a *= 0.5f;
    }
    return s / n;
}

// a tileable 256 x 256 noise picture for everything that moves (sampled every frame: much cheaper than fbm)
struct Tex {
    std::vector<uint8_t> v;
    Tex() {
        v.assign(256 * 256, 0);
        auto lat = [](int x, int y, int L, int o) { return hashf(uint32_t((x % L + L) % L) * 73856093u ^ uint32_t((y % L + L) % L) * 19349663u ^ uint32_t(o) * 83492791u); };
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) {
                float s = 0.f, a = 0.5f, n = 0.f;
                for (int o = 0; o < 4; ++o) {
                    const int L = 8 << o;
                    const float fx = float(x) * float(L) / 256.f, fy = float(y) * float(L) / 256.f;
                    const int ix = int(fx), iy = int(fy);
                    const float ux = smooth01(fx - float(ix)), uy = smooth01(fy - float(iy));
                    const float a0 = lat(ix, iy, L, o) + (lat(ix + 1, iy, L, o) - lat(ix, iy, L, o)) * ux;
                    const float a1 = lat(ix, iy + 1, L, o) + (lat(ix + 1, iy + 1, L, o) - lat(ix, iy + 1, L, o)) * ux;
                    s += (a0 + (a1 - a0) * uy) * a;
                    n += a;
                    a *= 0.5f;
                }
                v[size_t(y) * 256 + size_t(x)] = uint8_t(std::max(0.f, std::min(255.f, (s / n - 0.5f) * 1.8f * 255.f + 127.5f)));
            }
    }
};
const Tex& tex() {
    static const Tex t;
    return t;
}
// 0..1 at (x, y) of the noise picture (wraps), bilinear
float tx(float x, float y) {
    const std::vector<uint8_t>& v = tex().v;
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = int(fx) & 255, iy = int(fy) & 255, jx = (ix + 1) & 255, jy = (iy + 1) & 255;
    const float ux = x - fx, uy = y - fy;
    const float a = float(v[size_t(iy) * 256 + size_t(ix)]), b = float(v[size_t(iy) * 256 + size_t(jx)]);
    const float c = float(v[size_t(jy) * 256 + size_t(ix)]), d = float(v[size_t(jy) * 256 + size_t(jx)]);
    return ((a + (b - a) * ux) * (1.f - uy) + (c + (d - c) * ux) * uy) / 255.f;
}

// 4x4 ordered dithering
inline float bayer(int x, int y) {
    static const uint8_t m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (float(m[(y & 3) * 4 + (x & 3)]) + 0.5f) / 16.f;
}
inline uint32_t ramp(const uint32_t* r, int n, float v, float d) {
    int i = int(std::floor(clamp01(v) * float(n - 1) + d));
    return r[std::max(0, std::min(n - 1, i))];
}

// the palettes
const uint32_t kSpace[] = {rgb(3, 2, 10),   rgb(6, 4, 16),   rgb(10, 6, 23),  rgb(15, 8, 31),  rgb(22, 10, 41),  rgb(32, 12, 53),
                           rgb(46, 15, 67), rgb(64, 19, 82), rgb(88, 25, 98), rgb(118, 34, 114), rgb(150, 48, 128)};
const uint32_t kGreen[] = {rgb(8, 20, 10),   rgb(14, 36, 16),  rgb(22, 54, 22),   rgb(32, 76, 30),   rgb(46, 100, 38),
                           rgb(66, 126, 48), rgb(94, 154, 64), rgb(132, 186, 88), rgb(180, 220, 128)};
const uint32_t kVoid[] = {rgb(6, 1, 10),   rgb(12, 3, 20),  rgb(20, 6, 32),  rgb(30, 9, 46),   rgb(42, 13, 62),
                          rgb(58, 20, 82), rgb(78, 30, 106), rgb(104, 46, 134), rgb(138, 74, 166)};
const uint32_t kSteel[] = {rgb(10, 10, 18), rgb(18, 18, 30), rgb(28, 28, 44),   rgb(40, 40, 60),   rgb(56, 56, 78),
                           rgb(74, 74, 98), rgb(96, 96, 122), rgb(124, 124, 150), rgb(158, 158, 184), rgb(196, 196, 218)};
const uint32_t kRock[] = {rgb(16, 10, 16), rgb(30, 20, 26), rgb(48, 34, 38), rgb(70, 50, 52), rgb(96, 72, 68), rgb(128, 98, 88)};
const uint32_t kHull[] = {rgb(14, 10, 28), rgb(22, 17, 44),  rgb(32, 26, 62),   rgb(46, 38, 84),  rgb(62, 52, 108),
                          rgb(82, 72, 136), rgb(108, 98, 166), rgb(140, 130, 196), rgb(180, 172, 224)};
const uint32_t kGlowRed[] = {rgb(60, 6, 40), rgb(96, 8, 46), rgb(136, 10, 46), rgb(178, 16, 44), rgb(220, 30, 48)};
const uint32_t kDeep[] = {rgb(6, 0, 4), rgb(12, 1, 7), rgb(20, 2, 11), rgb(32, 4, 16), rgb(46, 6, 22)};
#define RAMP(r) r, int(sizeof(r) / sizeof(r[0]))

// ---------------------------------------------------------------- the art grid (as titlebg's)
struct Art {
    float k = 1.f;  // art pixels per game unit
    int blow = 1, w = 0, h = 0;
    std::vector<uint32_t> px, sky;
    int skyW = 0, vx = -1;
    float skyK = 0.f;
    uint32_t last = 0;
} g;

inline void put(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= g.w || y >= g.h) return;
    g.px[size_t(y) * size_t(g.w) + size_t(x)] = c;
}
inline void blend(int x, int y, uint32_t c, int a) {
    if (x < 0 || y < 0 || x >= g.w || y >= g.h || a <= 0) return;
    uint32_t& d = g.px[size_t(y) * size_t(g.w) + size_t(x)];
    d = mix(d, c, std::min(255, a));
}
inline void add(int x, int y, uint32_t c, int a) {  // light added (glows)
    if (x < 0 || y < 0 || x >= g.w || y >= g.h || a <= 0) return;
    uint32_t& d = g.px[size_t(y) * size_t(g.w) + size_t(x)];
    const int r = std::min(255, int((d >> 16) & 255) + int((c >> 16) & 255) * a / 255);
    const int gg = std::min(255, int((d >> 8) & 255) + int((c >> 8) & 255) * a / 255);
    const int b = std::min(255, int(d & 255) + int(c & 255) * a / 255);
    d = rgb(r, gg, b);
}
inline int AX(float gx) { return int(std::floor(gx * g.k)); }
inline float GX(int ax) { return (float(ax) + 0.5f) / g.k; }
// one dot: max(1, k) art pixels square at game-unit position (gx, gy)
inline void dot(float gx, float gy, uint32_t c, int a = 255) {
    const int bs = std::max(1, int(g.k + 0.5f)), x0 = AX(gx), y0 = AX(gy);
    for (int y = 0; y < bs; ++y)
        for (int x = 0; x < bs; ++x) {
            if (a >= 255) put(x0 + x, y0 + y, c);
            else blend(x0 + x, y0 + y, c, a);
        }
}
// a square of game units (size x size) filled - whole art pixels, at least one
inline void cell(float gx, float gy, float size, uint32_t c, int a = 255) {
    const int x0 = AX(gx), y0 = AX(gy), x1 = std::max(x0 + 1, AX(gx + size)), y1 = std::max(y0 + 1, AX(gy + size));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            if (a >= 255) put(x, y, c);
            else blend(x, y, c, a);
        }
}
// a soft round glow, added
void glow(float gx, float gy, float r, uint32_t c, int a) {
    if (a <= 0 || r <= 0.f) return;
    const float cx = gx * g.k, cy = gy * g.k, rr = r * g.k;
    const int x0 = std::max(0, int(cx - rr)), x1 = std::min(g.w - 1, int(cx + rr) + 1), y0 = std::max(0, int(cy - rr)),
              y1 = std::min(g.h - 1, int(cy + rr) + 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const float dx = (float(x) + 0.5f - cx) / rr, dy = (float(y) + 0.5f - cy) / rr, d2 = dx * dx + dy * dy;
            if (d2 >= 1.f) continue;
            const float f = (1.f - d2) * (1.f - d2);
            add(x, y, c, int(float(a) * f * (0.85f + 0.3f * bayer(x, y))));
        }
}
// a flat disc
void disc(float gx, float gy, float r, uint32_t c, int a = 255) {
    const float cx = gx * g.k, cy = gy * g.k, rr = std::max(0.5f, r * g.k);
    const int x0 = std::max(0, int(std::floor(cx - rr))), x1 = std::min(g.w - 1, int(std::ceil(cx + rr))),
              y0 = std::max(0, int(std::floor(cy - rr))), y1 = std::min(g.h - 1, int(std::ceil(cy + rr)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const float dx = float(x) + 0.5f - cx, dy = float(y) + 0.5f - cy;
            if (dx * dx + dy * dy <= rr * rr) {
                if (a >= 255) put(x, y, c);
                else blend(x, y, c, a);
            }
        }
}
// a lit ball (or one slice of a tube): dithered from a ramp; light (lx, ly, lz) normalised (x right, y down, z toward us);
// rim: a coloured light from the side (rx, ry) that catches the edge
struct Lit {
    float lx = 0.5f, ly = -0.6f, lz = 0.62f, amb = 0.12f;
    uint32_t rim = 0;
    float rx = -1.f, ry = 0.f;
    int rimA = 0;
};
void ball(float gx, float gy, float r, const uint32_t* rp, int n, const Lit& L, float dim = 1.f, int a = 255, float mottle = 0.f) {
    const float cx = gx * g.k, cy = gy * g.k, rr = std::max(0.5f, r * g.k);
    const int x0 = std::max(0, int(std::floor(cx - rr))), x1 = std::min(g.w - 1, int(std::ceil(cx + rr))),
              y0 = std::max(0, int(std::floor(cy - rr))), y1 = std::min(g.h - 1, int(std::ceil(cy + rr)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const float dx = (float(x) + 0.5f - cx) / rr, dy = (float(y) + 0.5f - cy) / rr, d2 = dx * dx + dy * dy;
            if (d2 > 1.f) continue;
            const float z = std::sqrt(1.f - d2);
            const float l = std::max(0.f, dx * L.lx + dy * L.ly + z * L.lz);
            float v = (L.amb + (1.f - L.amb) * l) * dim;
            if (mottle > 0.f) v *= 1.f + mottle * (tx(float(x) * 0.9f, float(y) * 0.9f) - 0.5f) * 2.f;
            uint32_t col = ramp(rp, n, v, bayer(x, y));
            if (L.rimA > 0) {
                const float e = dx * L.rx + dy * L.ry;
                if (e > 0.45f) col = mix(col, L.rim, int(std::min(1.f, (e - 0.45f) * 2.2f) * float(L.rimA)));
            }
            if (a >= 255) put(x, y, col);
            else blend(x, y, col, a);
        }
}

// ---------------------------------------------------------------- the sky (made once per size)
void makeSky(int W, int H, float tearX) {
    const float k = g.k;
    g.skyW = W;
    g.skyK = k;
    g.vx = int(tearX);
    g.sky.assign(size_t(g.w) * size_t(g.h), 0);
    const int nSpace = int(sizeof(kSpace) / sizeof(kSpace[0]));
    // the noise fields are smooth (features of 50 units and more): worked out every C art pixels and blended between
    constexpr int C = 4;
    const int cw = g.w / C + 2, chh = g.h / C + 2;
    std::vector<float> fw(size_t(cw) * size_t(chh)), ft(fw.size()), fd(fw.size());
    for (int cy = 0; cy < chh; ++cy)
        for (int cx = 0; cx < cw; ++cx) {
            const float gx = float(cx * C) / k, gy = float(cy * C) / k;
            const float u = gx / float(W), w = gy / float(H);
            // the nebula: wisps in a band from the upper left to the lower right (the title's runs the other way)
            const float off = w - u * 0.7f - 0.12f;
            const float band = std::max(0.f, 1.f - off * off * 8.f);
            const float wx = fbm(gx * 0.006f + 31.f, gy * 0.006f, 1.9f, 3) - 0.5f, wy = fbm(gx * 0.006f + 7.f, gy * 0.006f + 13.f, 2.7f, 3) - 0.5f;
            const float nn = fbm(gx * 0.010f + wx * 2.6f, gy * 0.015f + wy * 2.6f, 4.3f, 6);
            const size_t i = size_t(cy) * size_t(cw) + size_t(cx);
            fw[i] = smoothstep(0.44f, 0.80f, nn) * band;
            // a cold teal haze high on the right
            const float n2 = fbm(gx * 0.008f + 60.f, gy * 0.02f + 5.f, 6.1f, 5);
            const float off2 = w - 0.22f + (u - 0.8f) * 0.4f;
            ft[i] = smoothstep(0.56f, 0.8f, n2) * std::max(0.f, 1.f - off2 * off2 * 26.f) * smoothstep(0.35f, 0.9f, u);
            fd[i] = 0.55f + 0.9f * fbm(gx * 0.02f + 3.f, gy * 0.02f, 8.2f, 4);
        }
    auto at = [&](const std::vector<float>& f, int x, int y) {
        const float fx = float(x) / float(C), fy = float(y) / float(C);
        const int ix = int(fx), iy = int(fy);
        const float ux = fx - float(ix), uy = fy - float(iy);
        const size_t a = size_t(iy) * size_t(cw) + size_t(ix);
        return (f[a] * (1.f - ux) + f[a + 1] * ux) * (1.f - uy) + (f[a + size_t(cw)] * (1.f - ux) + f[a + size_t(cw) + 1] * ux) * uy;
    };
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x) {
            const float gx = (float(x) + 0.5f) / k, gy = (float(y) + 0.5f) / k;
            const float d = bayer(x, y);
            const float w = gy / float(H);
            uint32_t col = ramp(kSpace, nSpace, 0.05f + 0.18f * w + at(fw, x, y) * 0.58f, d);
            const float teal = at(ft, x, y);
            if (teal > 0.f) col = mix(col, rgb(26, 88, 112), int(teal * 70.f * (0.8f + 0.4f * d)));
            // the tear's red light on the dust round it
            const float tx0 = gx - tearX, ty0 = (gy - 250.f) * 0.42f, td = std::sqrt(tx0 * tx0 + ty0 * ty0);
            const float red = std::max(0.f, 1.f - td / 230.f);
            if (red > 0.f) col = mix(col, rgb(120, 10, 40), int(red * red * 120.f * at(fd, x, y) * (0.8f + 0.4f * d)));
            // a synthwave haze low on the screen
            if (w > 0.78f) col = mix(col, rgb(84, 14, 78), int(smoothstep(0.78f, 1.f, w) * 70.f * (0.75f + 0.5f * d)));
            g.sky[size_t(y) * size_t(g.w) + size_t(x)] = col;
        }
}

// stars drifting left in three layers
void drawStars(int W, int H, uint32_t now) {
    static const float kSpeed[3] = {2.4f, 5.4f, 12.f};
    static const int kCount[3] = {100, 60, 32};
    for (int layer = 0; layer < 3; ++layer) {
        const int n = kCount[layer] * W / 640;
        for (int i = 0; i < n; ++i) {
            const uint32_t h = hash32(uint32_t(i) * 7919u + uint32_t(layer) * 104729u + 911u);
            const double dd = double(now) * 0.001 * double(kSpeed[layer]);
            const double drift = dd - std::floor(dd / double(W)) * double(W);
            double gx = double(h % uint32_t(W * 16)) / 16.0 - drift;
            if (gx < 0) gx += double(W);
            const float gy = float((h >> 12) % 4800u) / 4800.f * float(H);
            const uint32_t pick = (h >> 24) % 10u;
            const uint32_t col = pick == 0 ? rgb(255, 110, 220) : pick == 1 ? rgb(110, 230, 255) : pick == 2 ? rgb(255, 214, 160) : rgb(232, 234, 248);
            const float tw = 0.7f + 0.3f * float(std::sin(double(now) * 0.003 + double(h & 255u)));
            const int a = int(float(110 + layer * 62) * tw);
            dot(float(gx), gy, col, a);
            if (layer == 2 && (h & 7u) == 0) {
                dot(float(gx) - 1.f, gy, col, a / 3);
                dot(float(gx) + 1.f, gy, col, a / 3);
                dot(float(gx), gy - 1.f, col, a / 3);
                dot(float(gx), gy + 1.f, col, a / 3);
            }
        }
    }
}

// ---------------------------------------------------------------- tears in the cosmos
struct Tear {
    float cx, top, len, wmax, glowR;
    uint32_t seed;
    bool great;  // the big one: eyes in its dark, rocks drawn into it
};
float tearMid(const Tear& T, float y) {
    const float s = float(T.seed % 97u);
    return T.cx + 4.5f * std::sin(y * 0.021f + s) + (tx(s * 2.f, y * 0.9f) - 0.5f) * 9.f + (tx(s * 5.f + 100.f, y * 3.1f) - 0.5f) * 2.6f;
}
float tearHalf(const Tear& T, float y, float t) {
    const float u = (y - T.top) / T.len;
    if (u <= 0.f || u >= 1.f) return 0.f;
    const float s = float(T.seed % 89u);
    const float shape = float(std::pow(double(std::sin(kPi * u)), 0.75));
    const float rag = 0.62f + 0.62f * tx(s * 3.f + 50.f, y * 1.6f) + 0.18f * (tx(s + 140.f, y * 6.f) - 0.5f);
    return T.wmax * shape * rag + 0.6f * std::sin(t * 1.6f + y * 0.05f) * shape;
}

void drawTear(const Tear& T, uint32_t now) {
    const float t = float(now % 3600000u) * 0.001f;
    const float pulse = 0.5f + 0.5f * std::sin(t * 1.7f + float(T.seed % 7u));
    const int ay0 = std::max(0, AX(T.top)), ay1 = std::min(g.h - 1, AX(T.top + T.len));
    const float edge = std::max(1.2f, 1.3f / g.k);  // the raw edge, game units
    for (int ay = ay0; ay <= ay1; ++ay) {
        const float y = GX(ay);
        const float cx = tearMid(T, y), w = tearHalf(T, y, t);
        if (w <= 0.15f) continue;
        const float gr = T.glowR * (0.55f + 0.45f * std::min(1.f, w / (T.wmax * 0.6f)));
        const int ax0 = std::max(0, AX(cx - w - gr)), ax1 = std::min(g.w - 1, AX(cx + w + gr));
        for (int ax = ax0; ax <= ax1; ++ax) {
            const float x = GX(ax);
            const float d = std::fabs(x - cx) - w;
            const float dth = bayer(ax, ay);
            if (d > 0.f) {  // the glow round it: magenta far out, red close
                const float f = 1.f - d / gr;
                if (f <= 0.f) continue;
                const uint32_t col = ramp(RAMP(kGlowRed), f, dth);
                blend(ax, ay, col, int(f * f * (120.f + 70.f * pulse)));
                continue;
            }
            if (d > -edge) {  // the raw edge, hot in places
                const float n = tx(x * 2.3f + t * 30.f + float(T.seed), y * 2.3f);
                put(ax, ay, n > 0.74f ? rgb(255, 196, 140) : n > 0.58f ? rgb(255, 92, 70) : rgb(222, 26, 50));
                continue;
            }
            // inside: the dark beyond, swirling, veined with red; deeper toward the middle
            const float depth = std::min(1.f, -d / std::max(1.f, w));
            const float sw = tx(x * 1.4f + 8.f * std::sin(y * 0.03f + t * 0.6f), y * 0.9f - t * 14.f + float(T.seed));
            uint32_t col = ramp(RAMP(kDeep), (1.f - depth) * 0.85f + sw * 0.35f, dth);
            const float vein = std::fabs(sw - 0.5f);
            if (vein < 0.03f) col = mix(col, rgb(170, 12, 40), int((1.f - vein / 0.03f) * (150.f + 90.f * pulse)));
            // sparks of another sky far within
            const uint32_t hs = hash32(uint32_t(ax) * 73u + uint32_t(ay) * 9277u + (now / 140u) * 31u + T.seed);
            if (hs % 1500u == 0) col = (hs >> 12) & 1u ? rgb(255, 200, 220) : rgb(255, 90, 120);
            put(ax, ay, col);
        }
    }
    if (!T.great) return;
    // eyes open in the dark beyond now and then, two by two, and blink
    for (int i = 0; i < 3; ++i) {
        const float period = 9.f + float(i) * 3.7f, ph = std::fmod(t / period + float(i) * 0.37f, 1.f);
        if (ph > 0.32f) continue;
        const float open = std::min(1.f, std::min(ph, 0.32f - ph) * 14.f);
        if (std::fmod(t * 1.3f + float(i), 2.2f) < 0.12f) continue;  // a blink
        const float ey = T.top + T.len * (0.3f + 0.2f * float(i)), ex = tearMid(T, ey);
        if (tearHalf(T, ey, t) < 4.f) continue;
        for (int s = -1; s <= 1; s += 2) {
            const float px = ex + float(s) * 2.6f;
            glow(px, ey, 4.5f, rgb(60, 255, 90), int(90.f * open));
            disc(px, ey, 1.2f * open + 0.3f, rgb(150, 255, 140));
            dot(px, ey, rgb(240, 255, 230), int(255.f * open));
        }
    }
    // it bleeds: drops run down from the lower edges and fade
    for (int i = 0; i < 22; ++i) {
        const uint32_t h = hash32(uint32_t(i) * 2654435761u + 99u + T.seed);
        const float y0 = T.top + T.len * (0.45f + 0.5f * hashf(h));
        const float side = (h >> 8) & 1u ? 1.f : -1.f;
        const float x = tearMid(T, y0) + side * (tearHalf(T, y0, t) + 0.4f);
        const float period = 2.6f + float((h >> 12) % 100u) * 0.03f;
        const float ph = std::fmod(t / period + float((h >> 20) % 100u) * 0.01f, 1.f);
        const float len = 30.f + float((h >> 3) % 40u);
        const float y = y0 + ph * ph * len;
        const int a = int(255.f * (1.f - ph));
        for (int j = 0; j < 6; ++j) dot(x, y - float(j) * 0.9f, j == 0 ? rgb(230, 30, 50) : rgb(150, 10, 30), a * (6 - j) / 6);
    }
    // red dust drifting out of it
    for (int i = 0; i < 40; ++i) {
        const uint32_t h = hash32(uint32_t(i) * 40503u + 7u + T.seed);
        const float y = T.top + 20.f + hashf(h) * (T.len - 40.f);
        const float ph = std::fmod(t * (0.06f + float((h >> 10) % 50u) * 0.002f) + float((h >> 16) % 100u) * 0.01f, 1.f);
        const float x = tearMid(T, y) + tearHalf(T, y, t) + 3.f + ph * 110.f;
        dot(x, y - ph * 14.f + 3.f * std::sin(t + float(i)), (h >> 5) & 1u ? rgb(230, 50, 70) : rgb(255, 140, 120), int(180.f * (1.f - ph)));
    }
}

// rocks and wreckage spiralling slowly into the great tear, stretched and lost as they reach it
void drawDebris(const Tear& T, uint32_t now) {
    const float t = float(now % 3600000u) * 0.001f;
    Lit L;
    L.lx = 0.6f;
    L.ly = -0.5f;
    L.lz = 0.62f;
    L.amb = 0.1f;
    L.rim = rgb(255, 70, 70);
    L.rx = -1.f;
    L.ry = 0.f;
    L.rimA = 210;
    for (int i = 0; i < 9; ++i) {
        const uint32_t h = hash32(uint32_t(i) * 2246822519u + 5u);
        const float period = 26.f + float(h % 20u);
        const float ph = std::fmod(t / period + hashf(h >> 3), 1.f);
        const float y0 = T.top + T.len * (0.2f + 0.6f * hashf(h >> 7));
        const float r0 = 70.f + 70.f * hashf(h >> 11);
        const float ang = (hashf(h >> 15) - 0.5f) * 1.2f + ph * 1.6f;  // swinging round as it falls in
        const float rr = r0 * (1.f - ph * ph);
        const float x = tearMid(T, y0) + std::cos(ang) * rr, y = y0 + std::sin(ang) * rr * 0.55f;
        const float size = (2.4f + 3.6f * hashf(h >> 19)) * (ph > 0.85f ? (1.f - ph) / 0.15f : 1.f);
        if (size < 0.4f) continue;
        if (ph > 0.6f) glow(x, y, size * 3.f, rgb(255, 40, 60), int((ph - 0.6f) * 160.f));  // it heats up
        ball(x, y, size, RAMP(kRock), L);
        if ((h >> 23) % 3u == 0) dot(x + size * 0.4f, y - size * 0.3f, rgb(200, 170, 150), 140);
    }
}

// ---------------------------------------------------------------- the tentacle
void drawTentacle(const Tear& T, uint32_t now, float roomRight) {
    const uint32_t kCycle = 11000;
    const float ct = float(now % kCycle);
    float ext;
    if (ct < 2600.f) ext = smooth01(ct / 2600.f);
    else if (ct < 6500.f) ext = 1.f;
    else if (ct < 8600.f) ext = 1.f - smooth01((ct - 6500.f) / 2100.f);
    else ext = 0.f;
    if (ext <= 0.02f) return;
    const float t = float(now % 3600000u) * 0.001f;
    constexpr int NS = 84;
    const float len = 140.f;
    (void)roomRight;
    const float seg = len / float(NS), f = 30.f / float(NS);
    float px[NS], py[NS], pa[NS];
    const float baseY = T.top + T.len * 0.66f;
    float x = tearMid(T, baseY) - 1.f, y = baseY, a = -1.0f;
    const float tight = (1.f - ext) * 0.10f;
    for (int i = 0; i < NS; ++i) {
        const float u = float(i) / float(NS);
        a += (-0.02f - (0.26f + tight) * u * u + 0.07f * std::sin(t * 1.5f + float(i) * 0.3f * f) * (0.3f + u)) * f;
        px[i] = x;
        py[i] = y;
        pa[i] = a;
        x += std::cos(a) * seg;
        y += std::sin(a) * seg;
    }
    const int vis = std::max(2, int(ext * float(NS)));
    Lit L;  // lit from the upper right, the tear's red catching it from the left
    L.lx = 0.55f;
    L.ly = -0.62f;
    L.lz = 0.56f;
    L.amb = 0.16f;
    L.rim = rgb(255, 60, 70);
    L.rx = -0.85f;
    L.ry = 0.5f;
    L.rimA = 190;
    const float r0 = 9.f;
    auto radius = [&](int i) {
        const float u = float(i) / float(NS);
        const float r = r0 * float(std::pow(double(1.f - u), 0.8)) + 1.4f;
        return r * (i > vis - 7 ? 0.55f + 0.065f * float(vis - i) : 1.f);
    };
    auto inRift = [](int i) { return i < 12 ? 1.f - float(i) / 12.f : 0.f; };  // the part still in the rift is lost in its dark
    for (int i = vis - 1; i >= 0; --i)  // a dark outline round the whole of it
        if (inRift(i) < 0.6f) disc(px[i], py[i], radius(i) + 1.1f / g.k + 0.25f, rgb(8, 18, 10));
    for (int i = vis - 1; i >= 0; --i) ball(px[i], py[i], radius(i), RAMP(kGreen), L, 1.f - 0.85f * inRift(i), 255, 0.18f);
    for (int i = vis - 1; i >= 0; --i) {
        const float rr = radius(i);
        if (i % 3 == 0 && rr > 2.4f && inRift(i) < 0.4f) {  // suckers along the inner curve
            const float nx = -std::sin(pa[i]), ny = std::cos(pa[i]);
            const float sx = px[i] + nx * rr * 0.62f, sy = py[i] + ny * rr * 0.62f;
            disc(sx, sy, std::max(0.9f, rr * 0.22f), rgb(52, 80, 40));
            disc(sx, sy, std::max(0.6f, rr * 0.14f), rgb(206, 220, 150));
            dot(sx, sy, rgb(90, 110, 70));
        }
        if (i % 7 == 3 && inRift(i) < 0.3f) {  // bioluminescent spots on the back
            const float nx = std::sin(pa[i]), ny = -std::cos(pa[i]);
            glow(px[i] + nx * rr * 0.4f, py[i] + ny * rr * 0.4f, rr * 0.5f + 1.f, rgb(80, 255, 110), int(70.f + 50.f * std::sin(t * 3.f + float(i))));
        }
    }
    if (vis > 8) {  // slime catching the light at the tip
        const int i = vis - 4;
        dot(px[i] + 0.6f, py[i] - 1.2f, rgb(220, 255, 190), 210);
    }
}

// ---------------------------------------------------------------- the war sun and the void dragon (as 0.46.2's)
float dragonUp(uint32_t now) {
    const float ct = float((now + 5200u) % 21000u);
    if (ct < 3800.f) return smooth01(ct / 3800.f);
    if (ct < 11800.f) return 1.f;
    if (ct < 15600.f) return 1.f - smooth01((ct - 11800.f) / 3800.f);
    return 0.f;
}

void drawWarSun(float wx, float wy, float R, uint32_t now, bool firing, float& eyeX, float& eyeY) {
    const float t = float(now % 3600000u) * 0.001f;
    glow(wx, wy, R * 1.5f, rgb(90, 40, 120), 50);  // a faint halo against the haze
    const float cx = wx * g.k, cy = wy * g.k, rr = R * g.k;
    const float lx = -0.45f, ly = -0.55f, lz = 0.70f;  // lit from the upper left (the nebula)
    const int x0 = std::max(0, int(cx - rr) - 1), x1 = std::min(g.w - 1, int(cx + rr) + 1), y0 = std::max(0, int(cy - rr) - 1),
              y1 = std::min(g.h - 1, int(cy + rr) + 1);
    const int nS = int(sizeof(kSteel) / sizeof(kSteel[0]));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const float dx = (float(x) + 0.5f - cx) / rr, dy = (float(y) + 0.5f - cy) / rr, d2 = dx * dx + dy * dy;
            if (d2 > 1.f) continue;
            const float z = std::sqrt(1.f - d2);
            const float lon = std::atan2(dx, z), lat = std::asin(std::max(-1.f, std::min(1.f, dy)));
            float v = 0.1f + 0.9f * std::max(0.f, dx * lx + dy * ly + z * lz);
            // armour plates: rows and columns of panels, each a little lighter or darker, dark seams between them
            const float pu = (lon + kPi) * 14.f / kPi, pv = (lat + kPi / 2.f) * 9.f / kPi;
            const float fu = pu - std::floor(pu), fv = pv - std::floor(pv);
            const uint32_t ph = hash32(uint32_t(int(pu)) * 977u + uint32_t(int(pv)) * 131u + 7u);
            v *= 0.86f + 0.24f * hashf(ph);
            const bool seam = fu < 0.09f || fv < 0.1f;
            if (seam) v *= 0.62f;
            uint32_t col = ramp(kSteel, nS, v, bayer(x, y));
            // the great trench round its middle, lights running along it
            if (std::fabs(lat) < 0.07f) {
                col = rgb(14, 12, 20);
                const float run = std::sin(lon * 22.f - t * 3.f);
                if (std::fabs(lat) < 0.035f && run > 0.55f) col = mix(col, rgb(255, 96, 46), int((run - 0.55f) * 560.f));
            } else if (!seam && (ph >> 9) % 5u == 0 && z > 0.3f && std::fabs(fu - 0.5f) < 0.12f && std::fabs(fv - 0.5f) < 0.16f) {  // a lit window here and there
                col = mix(col, rgb(255, 200, 120), int(150.f + 90.f * std::sin(t * 1.3f + float(ph & 31u))));
            }
            // the rim against space
            if (d2 > 0.86f) col = mix(col, rgb(10, 8, 18), int((d2 - 0.86f) * 900.f));
            put(x, y, col);
        }
    // the core (the "eye" that fires), lower left on its face: brighter while it shoots
    eyeX = wx - R * 0.34f;
    eyeY = wy + R * 0.3f;
    const float gl = firing ? 0.7f + 0.3f * std::sin(t * 18.f) : 0.35f + 0.15f * std::sin(t * 2.f);
    disc(eyeX, eyeY, R * 0.24f, rgb(40, 10, 14));
    disc(eyeX, eyeY, R * 0.19f, rgb(130, 24, 12));
    disc(eyeX, eyeY, R * 0.13f, mix(rgb(220, 70, 20), rgb(255, 230, 160), int(gl * 255.f)));
    glow(eyeX, eyeY, R * 0.5f, rgb(255, 120, 40), int(gl * 150.f));
    // antenna mast with a light
    for (int i = 0; i < 6; ++i) dot(wx + R * 0.45f, wy - R * 0.88f - float(i) * 0.9f, rgb(150, 150, 176));
    dot(wx + R * 0.45f, wy - R * 0.88f - 6.f, rgb(255, 80, 60), int(150.f + 100.f * std::sin(t * 4.f)));
}

// the Vuil'raith void dragon: a serpent of the void with a long fanged skull, green eyes and tentacles round its neck;
// (hx, hy) the back of its skull, (dx, dy) where the snout points, sc its size
void drawVoidDragon(uint32_t now, float hx, float hy, float dx, float dy, float up, float sc, float bottom) {
    if (up <= 0.01f) return;
    const float t = float(now % 3600000u) * 0.001f;
    const float dl = std::sqrt(dx * dx + dy * dy);
    dx /= dl;
    dy /= dl;
    const float nx = -dy, ny = dx;
    Lit L;  // lit by the war sun's fire from the upper left, the void's green from below
    L.lx = -0.5f;
    L.ly = -0.6f;
    L.lz = 0.62f;
    L.amb = 0.14f;
    L.rim = rgb(90, 255, 120);
    L.rx = 0.3f;
    L.ry = 0.95f;
    L.rimA = 55;
    // the void it drags along
    for (int i = 0; i < 4; ++i) glow(hx - 8.f * sc, hy + (32.f + float(i) * 20.f) * sc, (70.f - float(i) * 10.f) * sc, rgb(40, 6, 60), 60);
    const float bx = hx - 36.f * sc, by = bottom + 40.f;
    constexpr int NN = 64;
    float px[NN], py[NN], pr[NN];
    for (int i = 0; i < NN; ++i) {
        const float u = float(i) / float(NN - 1);
        const float sway = 14.f * sc * std::sin(u * 3.1f + t * 0.9f) * (1.f - u);
        px[i] = bx + (hx - bx) * u + sway;
        py[i] = by + (hy + 8.f * sc - by) * u;
        pr[i] = (19.f - 4.f * u) * sc;
    }
    Lit TL = L;
    TL.rimA = 0;
    auto tentacle = [&](int k, float sx, float sy, float side, float len) {
        constexpr int kMax = 160;
        float qx[kMax], qy[kMax], qr[kMax], qa[kMax];
        float x = sx, y = sy, a = -kPi / 2.f + side * 0.9f;
        const int Nt = std::min(kMax, int(len * 2.4f / 2.2f));
        const float q = 2.2f / 4.8f;  // (0.46.2's curve, over steps of 2.2 units instead of 4.8)
        for (int i = 0; i < Nt; ++i) {
            const float u = float(i) / float(Nt);
            a += (side * (-0.04f + 0.13f * u) + 0.13f * std::sin(t * (1.4f + 0.19f * float(k)) + float(i) * 0.33f * q + float(k) * 1.7f)) * q;
            x += std::cos(a) * 2.2f * sc;
            y += std::sin(a) * 2.2f * sc;
            qx[i] = x;
            qy[i] = y;
            qa[i] = a;
            qr[i] = (5.6f * (1.f - u) + 1.2f) * sc;
        }
        if (Nt < 2) return;
        for (int i = 0; i < Nt; ++i) disc(qx[i], qy[i], qr[i] + 1.f / g.k, rgb(6, 16, 8));  // the outline, under all of it
        for (int i = 0; i < Nt; ++i) ball(qx[i], qy[i], qr[i], RAMP(kGreen), TL, 0.6f + 0.35f * float(i) / float(Nt), 255, 0.15f);
        for (int i = 2; i < Nt; i += 4)
            if (qr[i] > 2.5f) dot(qx[i] + std::cos(qa[i] + side * 1.57f) * qr[i] * 0.7f, qy[i] + std::sin(qa[i] + side * 1.57f) * qr[i] * 0.7f, rgb(190, 255, 150), 220);
        glow(qx[Nt - 1], qy[Nt - 1], 6.f * sc, rgb(120, 255, 120), 120);  // a glowing tip
        disc(qx[Nt - 1], qy[Nt - 1], 1.6f * sc, rgb(200, 255, 170));
    };
    const float reach = 0.45f + 0.55f * up;
    for (int k = 0; k < 4; ++k) {  // behind the neck
        const int i = 10 + k * 10;
        tentacle(k, px[i] + (k % 2 ? 12.f : -12.f) * sc, py[i], k % 2 ? 1.f : -1.f, (52.f + float(k % 3) * 12.f) * reach);
    }
    for (int i = 0; i < NN; ++i) {  // the neck: plates lit from the fire, green spots along it
        ball(px[i], py[i], pr[i], RAMP(kVoid), TL, 1.f, 255, 0.2f);
        if (i % 6 == 0) {
            for (float kx = -pr[i] * 0.8f; kx <= pr[i] * 0.8f; kx += 1.f / g.k) dot(px[i] + kx, py[i] + pr[i] * 0.15f, rgb(20, 6, 30), 160);
            glow(px[i] - pr[i] * 0.45f, py[i] + 2.f, 4.f * sc, rgb(110, 255, 120), int(80.f + 70.f * std::sin(t * 3.f + float(i))));
        }
    }
    // the skull: horns, the jaws reaching for the war sun
    const float open = 0.18f + 0.22f * (0.5f + 0.5f * std::sin(t * 1.5f));
    const float ca = std::cos(open), sa = std::sin(open);
    const float jx = dx * ca - dy * sa, jy = dx * sa + dy * ca;
    for (int i = 0; i < 7; ++i) {  // horns sweeping back
        const float a = std::atan2(dy, dx) + kPi + (float(i) - 3.f) * 0.32f;
        const float Lh = (20.f + float(i % 3) * 6.f) * sc;
        for (float j = 0.f; j < Lh; j += 0.8f / g.k) {
            const float r = (3.2f * (1.f - j / Lh) + 0.6f) * sc;
            const float hxp = hx + std::cos(a) * (18.f * sc + j), hyp = hy + std::sin(a) * (18.f * sc + j) - j * 0.4f;
            disc(hxp, hyp, r, j > Lh - 8.f * sc ? rgb(170, 110, 210) : mix(rgb(40, 14, 60), rgb(110, 60, 150), int(j / Lh * 255.f)));
        }
    }
    for (int i = 0; i < 9; ++i) {  // the lower jaw
        const float r = (13.f - float(i) * 1.1f) * sc;
        ball(hx + (jx * float(i) * 5.2f + nx * 6.f) * sc, hy + (jy * float(i) * 5.2f + ny * 6.f) * sc, r, RAMP(kVoid), L, 0.85f);
    }
    for (int i = 2; i < 10; ++i) {  // the maw: a green void glowing between the jaws
        const float mx = (dx + jx) * 0.5f, my = (dy + jy) * 0.5f;
        const float gx = hx + (mx * float(i) * 5.f + nx * 3.f) * sc, gy = hy + (my * float(i) * 5.f + ny * 3.f) * sc;
        disc(gx, gy, (6.f - float(i) * 0.4f) * sc, mix(rgb(30, 90, 40), rgb(170, 255, 140), int(128.f + 127.f * std::sin(t * 5.f + float(i)))));
        glow(gx, gy, 8.f * sc, rgb(90, 255, 120), 40);
    }
    ball(hx, hy, 27.f * sc, RAMP(kVoid), L);  // the cranium
    for (int i = 0; i < 10; ++i) {  // the upper jaw / snout
        const float r = (17.f - float(i) * 1.3f) * sc;
        ball(hx + (dx * float(i) * 6.2f - nx * 1.4f) * sc, hy + (dy * float(i) * 6.2f - ny * 1.4f) * sc, r, RAMP(kVoid), L);
    }
    for (int i = 3; i < 10; ++i) {  // fangs, down from the snout and up from the jaw
        const float ux = hx + (dx * float(i) * 6.2f + nx * (15.4f - float(i) * 1.3f)) * sc;
        const float uy = hy + (dy * float(i) * 6.2f + ny * (15.4f - float(i) * 1.3f)) * sc;
        for (int f = 0; f < 4; ++f) dot(ux + nx * float(f) * 0.9f * sc, uy + ny * float(f) * 0.9f * sc, f < 2 ? rgb(240, 238, 220) : rgb(190, 186, 170));
        if (i % 2 == 0) {
            const float lx = hx + (jx * float(i) * 5.2f + nx * (float(i) * 1.f - 4.f)) * sc, ly = hy + (jy * float(i) * 5.2f + ny * (float(i) * 1.f - 4.f)) * sc;
            for (int f = 0; f < 3; ++f) dot(lx - nx * float(f) * 0.9f * sc, ly - ny * float(f) * 0.9f * sc, rgb(225, 222, 205));
        }
    }
    for (int i = 0; i < 6; ++i) {  // eyes: a cluster of green ones along the skull, blinking out of step
        const float along = (2.f + float(i % 3) * 9.f) * sc, across = (-9.f + float(i / 3) * 7.f) * sc;
        const float ex = hx + dx * along + nx * across, ey = hy + dy * along + ny * across;
        if (std::fmod(t * (0.5f + 0.11f * float(i)) + float(i) * 0.41f, 3.f) < 0.14f) continue;
        glow(ex, ey, 6.f * sc, rgb(90, 255, 110), 120);
        disc(ex, ey, 2.2f * sc, rgb(120, 240, 110));
        disc(ex, ey, 1.1f * sc, rgb(230, 255, 210));
    }
    for (int k = 4; k < 8; ++k) {  // tentacles in front, round the head
        const int i = 36 + (k - 4) * 6;
        tentacle(k, px[i] + (k % 2 ? 18.f : -18.f) * sc, py[i] - 4.f * sc, k % 2 ? 1.f : -1.f, (36.f + float(k % 3) * 10.f) * reach);
    }
}

// the war sun's missiles: a volley while the dragon is up, smoke trails, bursting on it
void drawMissiles(uint32_t now, float sx, float sy, float tx0, float ty0) {
    const uint32_t kEvery = 520, kFlight = 1400;
    for (int m = 0; m < 4; ++m) {
        const uint32_t launch = (now / kEvery - uint32_t(m)) * kEvery;
        if (launch > now || dragonUp(launch) < 0.98f || dragonUp(launch + kFlight) < 0.9f) continue;
        const uint32_t age = now - launch;
        const uint32_t h = hash32(launch / kEvery + 17u);
        const float tgx = tx0 + float(h % 25u) - 12.f, tgy = ty0 + float((h >> 5) % 21u) - 10.f;
        const float bend = -36.f - float((h >> 9) % 44u), side = ((h >> 14) & 1u ? 1.f : -1.f) * (34.f + float((h >> 16) % 30u));
        auto at = [&](float u, float& x, float& y) {
            const float cx = (sx + tgx) / 2.f + side, cy = std::min(sy, tgy) + bend;
            x = (1 - u) * (1 - u) * sx + 2 * (1 - u) * u * cx + u * u * tgx;
            y = (1 - u) * (1 - u) * sy + 2 * (1 - u) * u * cy + u * u * tgy;
        };
        if (age < 160) glow(sx, sy, 7.f, rgb(255, 220, 150), 230 - int(age));
        if (age < kFlight) {
            const float u = float(age) / float(kFlight);
            for (int k = 40; k >= 0; --k) {  // smoke, then fire, then the missile
                const float uu = u - float(k) * 0.008f;
                if (uu < 0.f) continue;
                float x, y;
                at(uu, x, y);
                if (k > 14) disc(x, y + float(k) * 0.03f, 0.8f + float(k) * 0.05f, rgb(110, 92, 124), 150 - k * 3);
                else if (k > 1) dot(x, y, k < 6 ? rgb(255, 214, 110) : rgb(255, 120, 50), 240 - k * 12);
                else {
                    disc(x, y, 1.3f, rgb(255, 255, 235));
                    glow(x, y, 4.f, rgb(255, 180, 90), 120);
                }
            }
        } else if (age < kFlight + 700) {  // the burst
            const float e = float(age - kFlight) / 700.f;
            if (e < 0.4f) glow(tgx, tgy, 12.f * (1.f - e), rgb(255, 240, 200), int(255.f * (1.f - e / 0.4f)));
            const float rr = 3.f + e * 16.f;
            for (int i = 0; i < 26; ++i) {
                const uint32_t hh = hash32(h + uint32_t(i) * 31u);
                const float a = float(i) * kPi / 13.f + hashf(hh) * 0.3f, rk = rr * (0.7f + 0.5f * hashf(hh >> 8));
                dot(tgx + std::cos(a) * rk, tgy + std::sin(a) * rk + e * e * 6.f, e < 0.5f ? rgb(255, 235, 150) : rgb(255, 110, 40), int(255.f * (1.f - e)));
            }
            disc(tgx, tgy - e * 10.f, 3.f + e * 8.f, rgb(90, 76, 100), int(110.f * (1.f - e)));
        }
    }
}

// ---------------------------------------------------------------- 0.60.1 (owner 20:34): shaded pixel-art parts for the redesigns
// A G-buffer over a box of art pixels: each pixel a material (0 empty) and a light level. Parts are written in order (later ones
// cover earlier ones); `resolve` turns it into colours: ramps with dither, pillow light on flat parts (bright along the edges that
// face the light, dark along the far ones - also where two materials meet, so every plate reads), a dark outline round the whole,
// emissive materials glowing.
struct Mat {
    const uint32_t* ramp = nullptr;
    int n = 0;
    float base = 0.55f;  // flat parts: the middle level
    bool flat = true;    // light from the edges (pillow); else the light written with the part (tubes, balls)
    bool glow = false;   // emissive: a soft glow round it
    uint32_t glowCol = 0;
};
#define MAT(r) Mat{r, int(sizeof(r) / sizeof(r[0]))}
struct GBuf {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<uint8_t> id, sh;
    void reset(int X0, int Y0, int W, int H) {
        x0 = X0;
        y0 = Y0;
        w = std::max(1, W);
        h = std::max(1, H);
        id.assign(size_t(w) * size_t(h), 0);
        sh.assign(id.size(), 0);
    }
    inline void set(int x, int y, uint8_t m, uint8_t s) {
        x -= x0;
        y -= y0;
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        id[size_t(y) * size_t(w) + size_t(x)] = m;
        sh[size_t(y) * size_t(w) + size_t(x)] = s;
    }
    inline uint8_t at(int x, int y) const {
        x -= x0;
        y -= y0;
        if (x < 0 || y < 0 || x >= w || y >= h) return 0;
        return id[size_t(y) * size_t(w) + size_t(x)];
    }
};
// a place in a part's own units -> art pixels: origin (ox, oy) in art pixels, s art pixels per unit, turned by angle a
struct Xf {
    float ox = 0.f, oy = 0.f, s = 1.f, c = 1.f, sn = 0.f;
    Xf() = default;
    Xf(float x, float y, float scale, float ang = 0.f) : ox(x), oy(y), s(scale), c(std::cos(ang)), sn(std::sin(ang)) {}
    void map(float u, float v, float& x, float& y) const {
        x = ox + (u * c - v * sn) * s;
        y = oy + (u * sn + v * c) * s;
    }
    Xf child(float u, float v, float ang) const {  // a frame hinged at (u, v), turned a further ang
        Xf r = *this;
        map(u, v, r.ox, r.oy);
        const float a = std::atan2(sn, c) + ang;
        r.c = std::cos(a);
        r.sn = std::sin(a);
        return r;
    }
};
// a filled polygon (points in the frame's units)
void gPolyV(GBuf& b, const Xf& f, const float* uv, int count, uint8_t m, uint8_t s = 140) {
    float px[48], py[48];
    int n = 0;
    for (int i = 0; i + 1 < count && n < 48; i += 2, ++n) f.map(uv[i], uv[i + 1], px[n], py[n]);
    if (n < 3) return;
    float ymin = py[0], ymax = py[0];
    for (int i = 1; i < n; ++i) {
        ymin = std::min(ymin, py[i]);
        ymax = std::max(ymax, py[i]);
    }
    const int y0 = std::max(b.y0, int(std::floor(ymin))), y1 = std::min(b.y0 + b.h - 1, int(std::ceil(ymax)));
    for (int y = y0; y <= y1; ++y) {
        const float yc = float(y) + 0.5f;
        float xs[48];
        int k = 0;
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            if ((py[i] <= yc && py[j] > yc) || (py[j] <= yc && py[i] > yc)) xs[k++] = px[i] + (yc - py[i]) * (px[j] - px[i]) / (py[j] - py[i]);
        }
        std::sort(xs, xs + k);
        for (int i = 0; i + 1 < k; i += 2)
            for (int x = int(std::ceil(xs[i] - 0.5f)); x <= int(std::floor(xs[i + 1] - 0.5f)); ++x) b.set(x, y, m, s);
    }
}
void gPoly(GBuf& b, const Xf& f, std::initializer_list<float> uv, uint8_t m, uint8_t s = 140) {
    gPolyV(b, f, uv.begin(), int(uv.size()), m, s);
}
// the colours: into a sprite (spr != null: colour per pixel, 0 alpha where empty) or straight onto the art grid
struct Spr {
    int w = 0, h = 0;
    std::vector<uint32_t> col, add;  // add: the glow, added over what is behind
    std::vector<uint8_t> a;
};
void resolve(const GBuf& b, const Mat* mats, int lx, int ly, uint32_t outline, Spr* spr, float glowR = 2.6f) {
    const int W = b.w, H = b.h;
    std::vector<uint32_t> out(size_t(W) * size_t(H), 0);
    std::vector<uint8_t> alpha(out.size(), 0);
    std::vector<int> glows;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const size_t i = size_t(y) * size_t(W) + size_t(x);
            const uint8_t m = b.id[i];
            const int X = x + b.x0, Y = y + b.y0;
            if (!m) {  // the outline: an empty pixel next to the shape
                if (b.at(X - 1, Y) || b.at(X + 1, Y) || b.at(X, Y - 1) || b.at(X, Y + 1)) {
                    out[i] = outline;
                    alpha[i] = 255;
                }
                continue;
            }
            const Mat& M = mats[m];
            float v;
            if (M.glow) {
                v = float(b.sh[i]) / 255.f;
                glows.push_back(int(i));
            } else if (M.flat) {
                v = M.base + (float(b.sh[i]) - 140.f) / 255.f;
                for (int s = 1; s <= 3; ++s)  // the edge toward the light
                    if (b.at(X - lx * s, Y - ly * s) != m) {
                        v += 0.34f * float(4 - s) / 3.f;
                        break;
                    }
                for (int s = 1; s <= 3; ++s)  // the edge away from it
                    if (b.at(X + lx * s, Y + ly * s) != m) {
                        v -= 0.30f * float(4 - s) / 3.f;
                        break;
                    }
            } else {
                v = float(b.sh[i]) / 255.f;
            }
            out[i] = ramp(M.ramp, M.n, v, bayer(X, Y));
            alpha[i] = 255;
        }
    std::vector<uint32_t> addv;
    if (spr) addv.assign(out.size(), 0);
    for (int i : glows) {  // a soft glow round every emissive pixel
        const int gx = i % W + b.x0, gy = i / W + b.y0;
        const uint32_t gc = mats[b.id[size_t(i)]].glowCol;
        const int R = int(std::ceil(glowR));
        for (int dy = -R; dy <= R; ++dy)
            for (int dx = -R; dx <= R; ++dx) {
                const float d = std::sqrt(float(dx * dx + dy * dy)) / glowR;
                if (d >= 1.f) continue;
                const int a = int(60.f * (1.f - d) * (1.f - d));
                if (!spr) continue;  // (straight onto the grid: after the colours, below)
                const int x = gx + dx - b.x0, y = gy + dy - b.y0;
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                uint32_t& d0 = addv[size_t(y) * size_t(W) + size_t(x)];
                const int r = std::min(255, int((d0 >> 16) & 255) + int((gc >> 16) & 255) * a / 255);
                const int g2 = std::min(255, int((d0 >> 8) & 255) + int((gc >> 8) & 255) * a / 255);
                const int bb = std::min(255, int(d0 & 255) + int(gc & 255) * a / 255);
                d0 = rgb(r, g2, bb);
            }
    }
    if (spr) {
        spr->w = W;
        spr->h = H;
        spr->col = std::move(out);
        spr->a = std::move(alpha);
        spr->add = std::move(addv);
        return;
    }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const size_t i = size_t(y) * size_t(W) + size_t(x);
            if (alpha[i]) put(x + b.x0, y + b.y0, out[i]);
        }
    for (int i : glows) {
        const int gx = i % W + b.x0, gy = i / W + b.y0;
        const uint32_t gc = mats[b.id[size_t(i)]].glowCol;
        const int R = int(std::ceil(glowR));
        for (int dy = -R; dy <= R; ++dy)
            for (int dx = -R; dx <= R; ++dx) {
                const float d = std::sqrt(float(dx * dx + dy * dy)) / glowR;
                if (d < 1.f) add(gx + dx, gy + dy, gc, int(60.f * (1.f - d) * (1.f - d)));
            }
    }
}
// a sprite onto the art grid, its (0, 0) at art pixel (ax, ay)
void blitSpr(const Spr& s, int ax, int ay) {
    for (int y = 0; y < s.h; ++y) {
        const int Y = ay + y;
        if (Y < 0 || Y >= g.h) continue;
        for (int x = 0; x < s.w; ++x) {
            const int X = ax + x;
            if (X < 0 || X >= g.w) continue;
            const size_t i = size_t(y) * size_t(s.w) + size_t(x);
            if (s.a[i]) g.px[size_t(Y) * size_t(g.w) + size_t(X)] = s.col[i];
            if (!s.add.empty() && s.add[i]) add(X, Y, s.add[i], 255);
        }
    }
}

// the palettes of the redesigns
const uint32_t kHullA[] = {rgb(12, 12, 30),  rgb(20, 20, 48),   rgb(30, 30, 68),   rgb(44, 44, 92),   rgb(62, 62, 120),
                           rgb(86, 86, 150), rgb(118, 118, 182), rgb(158, 160, 214), rgb(204, 206, 240)};
const uint32_t kHullB[] = {rgb(24, 22, 38),   rgb(38, 36, 56),   rgb(56, 54, 78),   rgb(78, 76, 102), rgb(102, 100, 128),
                           rgb(130, 128, 156), rgb(162, 160, 186), rgb(196, 194, 218), rgb(232, 230, 248)};
const uint32_t kDarkM[] = {rgb(8, 8, 18), rgb(14, 14, 28), rgb(22, 22, 40), rgb(32, 32, 54), rgb(46, 46, 72), rgb(64, 64, 94)};
const uint32_t kGlass[] = {rgb(10, 36, 66), rgb(18, 72, 112), rgb(36, 128, 172), rgb(86, 200, 236), rgb(186, 248, 255)};
const uint32_t kNeonM[] = {rgb(150, 20, 110), rgb(220, 40, 170), rgb(255, 92, 212), rgb(255, 184, 242)};
const uint32_t kNeonC[] = {rgb(20, 116, 160), rgb(60, 200, 240), rgb(150, 240, 255), rgb(232, 255, 255)};
const uint32_t kOutline = rgb(6, 4, 12);

// ---------------------------------------------------------------- the cruiser (owner 20:54: "the middle option" of three drafts)
Mat gShipMats[12];
// The Federation cruiser (facing right; units = game units): a capital ship - wedge bow, deck, bridge tower, two turrets, keel, a straight neon waterline
void shipCruiser(GBuf& b, const Xf& f) {
    gPoly(b, f, {20, 5.5f, 28, 10.5f, 74, 10.5f, 84, 5.5f}, 2);                                      // the keel
    gPoly(b, f, {-8, -8, 6, -8, 6, 8, -8, 8}, 6);                                                   // the engine block
    gPoly(b, f, {4, -7, 94, -7, 130, -1, 132, 0.5f, 126, 3.5f, 92, 7, 4, 7}, 1);                    // the hull
    for (int i = 0; i < 6; ++i) {                                                                   // plate seams
        const float x = 20.f + float(i) * 16.f;
        gPoly(b, f, {x, -6.5f, x + 0.9f, -6.5f, x + 0.9f, 6.5f, x, 6.5f}, 2, 120);
    }
    gPoly(b, f, {16, -7, 22, -11.5f, 80, -11.5f, 88, -7}, 7);                                       // the deck
    gPoly(b, f, {28, -11.5f, 30, -14.4f, 37, -14.4f, 39, -11.5f}, 2);                               // the turrets
    gPoly(b, f, {42, -11.5f, 44, -14.4f, 51, -14.4f, 53, -11.5f}, 2);
    gPoly(b, f, {36, -13.6f, 45, -13.6f, 45, -12.4f, 36, -12.4f}, 6);                               // their guns
    gPoly(b, f, {50, -13.6f, 57, -13.6f, 57, -12.4f, 50, -12.4f}, 6);
    gPoly(b, f, {58, -11.5f, 62, -18.6f, 76, -18.6f, 79, -11.5f}, 8);                               // the bridge tower
    gPoly(b, f, {63, -18.6f, 64, -20.4f, 74, -20.4f, 75, -18.6f}, 2);
    gPoly(b, f, {68.6f, -24, 69.6f, -24, 69.6f, -20.4f, 68.6f, -20.4f}, 6);                         // its mast
    gPoly(b, f, {64, -17.2f, 74, -17.2f, 74, -15.8f, 64, -15.8f}, 5, 230);                          // its windows
    gPoly(b, f, {6, 0.3f, 120, 0.3f, 120, 1.5f, 6, 1.5f}, 4, 200);                                  // the neon waterline
    for (int i = 0; i < 26; ++i) {                                                                  // two rows of lit windows
        const float x = 10.f + float(i) * 3.4f;
        if (hash32(uint32_t(i) * 77u + 5u) % 5u == 0) continue;
        gPoly(b, f, {x, -3.9f, x + 1.4f, -3.9f, x + 1.4f, -2.7f, x, -2.7f}, 5, 200);
        if (i % 2 == 0 && x < 90.f) gPoly(b, f, {x, 3.3f, x + 1.4f, 3.3f, x + 1.4f, 4.4f, x, 4.4f}, 5, 170);
    }
    for (int e = -1; e <= 1; ++e)  // the nozzles
        gPoly(b, f, {-10, float(e) * 4.6f - 2, -8, float(e) * 4.6f - 2, -8, float(e) * 4.6f + 2, -10, float(e) * 4.6f + 2}, 9, 250);
}
// its escort fighters, 15 units long
void shipFighter(GBuf& b, const Xf& f) {
    gPoly(b, f, {2, 1.4f, 6, 4.6f, 9, 4.6f, 8.4f, 1.4f}, 2);
    gPoly(b, f, {0, -1.6f, 9, -2, 15, 0, 9, 1.9f, 0, 1.6f}, 1);
    gPoly(b, f, {7, -1.9f, 9.6f, -2.8f, 12, -1}, 3);
    gPoly(b, f, {1, -0.3f, 12, -0.1f, 12, 0.7f, 1, 0.5f}, 4, 200);
}

struct ShipArt {
    float k = 0.f;
    Spr main, small;                               // the cruiser and its fighter
    int bowX = 0, bowY = 0, sBowX = 0, sBowY = 0;  // the bow tip in each sprite (art pixels)
} gShip;

void buildShip() {
    // materials: 1 hull, 2 dark plates, 3 glass, 4 neon magenta, 5 neon cyan, 6 metal, 7 the deck, 8 the tower, 9 the nozzles
    Mat* M = gShipMats;
    M[1] = MAT(kHullB);
    M[2] = MAT(kDarkM);
    M[3] = MAT(kGlass);
    M[3].base = 0.62f;
    M[4] = MAT(kNeonM);
    M[4].glow = true;
    M[4].glowCol = rgb(255, 60, 200);
    M[5] = MAT(kNeonC);
    M[5].glow = true;
    M[5].glowCol = rgb(80, 220, 255);
    M[6] = MAT(kSteel);
    M[7] = MAT(kHullB);
    M[7].base = 0.66f;
    M[8] = MAT(kHullB);
    M[8].base = 0.7f;
    M[9] = MAT(kNeonC);
    M[9].glow = true;
    M[9].glowCol = rgb(150, 230, 255);
    const float k = g.k, L = 132.f, top = 25.f, bot = 11.f;
    const int pad = 3;
    GBuf gb;
    gb.reset(0, 0, int((L + 14.f) * k) + pad * 2, int((top + bot) * k) + pad * 2);
    shipCruiser(gb, Xf(float(pad) + 12.f * k, float(pad) + top * k, k));
    resolve(gb, M, -1, -1, kOutline, &gShip.main, std::max(2.f, 2.2f * k));
    gShip.bowX = int(float(pad) + 12.f * k + L * k);
    gShip.bowY = int(float(pad) + top * k);
    GBuf sb;  // the fighter: the hull in the darker blue
    sb.reset(0, 0, int(18.f * k) + pad * 2, int(10.f * k) + pad * 2);
    Mat FM[12];
    std::copy(M, M + 12, FM);
    FM[1] = MAT(kHullA);
    shipFighter(sb, Xf(float(pad) + 1.f * k, float(pad) + 4.f * k, k));
    resolve(sb, FM, -1, -1, kOutline, &gShip.small, std::max(1.5f, 1.6f * k));
    gShip.sBowX = int(float(pad) + 16.f * k);
    gShip.sBowY = int(float(pad) + 4.f * k);
    gShip.k = k;
}

// an engine plume behind (x, y) game units, len long, w half-wide; colours from the hot core out
void plume(float x, float y, float len, float w, uint32_t core, uint32_t mid, uint32_t tail, uint32_t now, uint32_t seed) {
    const uint32_t h = hash32(now / 50u + seed * 977u);
    const float L = len * (0.82f + 0.3f * float(h % 100u) / 100.f);
    const int ax0 = AX(x - L), ax1 = AX(x), ay0 = AX(y - w) - 1, ay1 = AX(y + w) + 1;
    for (int ay = ay0; ay <= ay1; ++ay)
        for (int ax = ax0; ax <= ax1; ++ax) {
            const float d = x - GX(ax), f = d / L;
            if (f < 0.f || f > 1.f) continue;
            const float ww = std::max(0.35f, w * (1.f - f * 0.75f) * (f < 0.12f ? 0.75f + f * 2.f : 1.f));
            const float e = std::fabs(GX(ay) - y) / ww;
            if (e >= 1.f) continue;
            const uint32_t col = f < 0.25f ? mix(core, mid, int(f / 0.25f * 255.f)) : mix(mid, tail, int((f - 0.25f) / 0.75f * 255.f));
            add(ax, ay, col, int(255.f * (1.f - f) * (1.f - e * e)));
        }
}

// the cruiser with its bow at (bx, by), three fighters in a V behind it, locked to it (one bob for all)
void drawShip(float bx, float by, uint32_t now) {
    if (gShip.k != g.k) buildShip();
    const float t = float(now % 3600000u) * 0.001f;
    const float bob = 1.f * std::sin(t * 0.9f);
    auto escort = [&](float ex, float ey, uint32_t seed) {
        plume(ex - 15.f, ey, 7.f, 1.1f, rgb(255, 255, 255), rgb(110, 230, 255), rgb(255, 60, 200), now, seed);
        blitSpr(gShip.small, AX(ex) - gShip.sBowX, AX(ey) - gShip.sBowY);
    };
    escort(bx - 158.f, by - 12.f + bob, 5u);
    escort(bx - 174.f, by - 20.f + bob, 6u);
    escort(bx - 174.f, by - 4.f + bob, 7u);
    for (int e = -1; e <= 1; ++e)
        plume(bx - 142.f, by + float(e) * 4.6f, 13.f, 2.f, rgb(255, 255, 255), rgb(140, 220, 255), rgb(90, 120, 255), now, uint32_t(10 + e));
    blitSpr(gShip.main, AX(bx) - gShip.bowX, AX(by) - gShip.bowY);
    if (std::fmod(t, 2.1f) < 0.12f) glow(bx - 63.f, by - 24.5f, 4.f, rgb(255, 255, 255), 255);  // the mast's strobe
    if (std::fmod(t, 1.2f) < 0.5f) glow(bx - 136.f, by - 7.f, 2.6f, rgb(255, 60, 60), 220);     // red at the stern
    if (std::fmod(t + 0.6f, 1.2f) < 0.5f) glow(bx - 3.f, by + 1.f, 2.6f, rgb(80, 255, 120), 220);  // green at the bow
}

}  // namespace

void draw(Canvas& c, uint32_t now) {
    const int blow = c.scale() >= 1.75f ? 2 : 1;
    const float k = c.scale() / float(blow);
    const int W = c.fullWidth(), H = c.height(), VX = c.viewX();
    const int aw = (c.pw() + blow - 1) / blow, ah = (c.ph() + blow - 1) / blow;
    if (g.w != aw || g.h != ah || g.k != k) {
        g.k = k;
        g.blow = blow;
        g.w = aw;
        g.h = ah;
        g.px.assign(size_t(aw) * size_t(ah), 0);
        g.last = 0;
    }
    // where things are (game units, the whole width): the New Game panel spans the view's 100 .. 540
    const float leftRoom = float(VX + 100), rightL = float(VX + 540), rightRoom = float(W) - rightL;
    const Tear great{54.f + float(VX) * 0.6f, 40.f, 400.f, 15.f, 24.f, 5u, true};
    if (g.skyW != W || g.skyK != k || g.vx != int(great.cx) || g.sky.size() != g.px.size()) makeSky(W, H, great.cx);
    // the Original picture (scale 1): the scene redrawn 15 times a second, copied every frame
    if (!c.unscaled() || g.last == 0 || now - g.last >= 66u) {
        g.last = now ? now : 1u;
        std::copy(g.sky.begin(), g.sky.end(), g.px.begin());
        drawStars(W, H, now);
        // the smaller rifts: one at the right edge, more in a wider screen's sides
        drawTear(Tear{float(W) - 24.f, 192.f, 192.f, 9.f, 14.f, 77u, false}, now);
        if (VX > 40) drawTear(Tear{24.f, 70.f, 230.f, 6.5f, 12.f, 913u, false}, now);
        if (rightRoom > 160.f) drawTear(Tear{float(W) - 116.f, 16.f, 128.f, 6.f, 11.f, 4421u, false}, now);
        drawDebris(great, now);
        drawTear(great, now);
        drawTentacle(great, now, leftRoom);
        // far off at the right, a war sun firing on a Vuil'raith void dragon that rises from below and sinks back
        const float tt = float(now % 3600000u) * 0.001f;
        const float wx = float(W) - 66.f, wy = 102.f + 3.f * std::sin(tt * 0.5f), R = 30.f;
        const float up = dragonUp(now);
        const bool room = rightRoom >= 140.f;  // (the dragon only where a wider screen leaves room beside the panel)
        const float sc = std::max(0.72f, std::min(1.f, rightRoom / 207.f));
        const float hx = rightL + rightRoom * 0.5f + 12.f * sc + 6.f * std::sin(tt * 0.7f);
        const float hy = float(H) + 100.f - up * (float(H) + 100.f - 308.f) + 5.f * std::sin(tt * 1.3f);
        if (room) drawVoidDragon(now, hx, hy, wx - hx, wy - hy, up, sc, float(H));
        float ex = wx, ey = wy;
        drawWarSun(wx, wy, R, now, room && up > 0.6f, ex, ey);
        if (room && up > 0.6f) drawMissiles(now, ex, ey, hx + 12.f * sc, hy - 8.f * sc);
        // the cruiser crossing the top now and then, three fighters behind it (0.60.1, owner 20:54: the middle of three drafts)
        const uint32_t kPeriod = 24000, kCross = 17000;
        const uint32_t ph = now % kPeriod;
        if (ph < kCross) {
            const float u = float(ph) / float(kCross);
            const float x = -10.f + u * (float(W) + 220.f);  // (the bow; the fighters follow up to ~190 behind)
            const float y = 32.f + 1.5f * float(std::sin(double(now) * 0.0011));
            drawShip(x, y, now);
        }
    }
    // onto the screen (each art pixel blow x blow real pixels); only inside the clip
    const Rect clip = c.clipIsView() ? Rect{0, 0, c.pw(), c.ph()} : c.pxClip();
    const int pw = c.pw();
    uint32_t* dst = c.pixels();
    for (int Y = clip.y; Y < clip.y + clip.h; ++Y) {
        const uint32_t* src = &g.px[size_t(Y / blow) * size_t(g.w)];
        uint32_t* d = dst + size_t(Y) * size_t(pw);
        if (blow == 1) std::copy(src + clip.x, src + clip.x + clip.w, d + clip.x);
        else
            for (int X = clip.x; X < clip.x + clip.w; ++X) d[X] = src[X >> 1];
    }
}

}  // namespace newgamebg
