#include "ui/warsun_scene.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gfx/canvas.h"
#include "gfx/font.h"
#include "ui/icons.h"

namespace warsun {
namespace {

constexpr float kPi = 3.14159265f;

// ---------------------------------------------------------------- the layout (game units: the screen is 480 high, W wide)
// The world is fixed to the screen at the start; the camera looking up moves it down by `cam` (0 .. kPan). The sky and its
// stars move less (they are far away).
constexpr float kHorizon = 150.f;  // the ice planet's horizon (its highest point) at the start
constexpr float kPan = 440.f;      // how far the camera looks up
constexpr float kPlanetR = 1500.f;
constexpr float kPlanetDX = 70.f;  // the planet's centre this far right of the screen's middle
constexpr float kHalo = 46.f;      // its atmosphere's glow beyond the limb
constexpr float kSunWY = 272.f - kPan;  // the war sun's centre (at the end it is 272 down the screen)
constexpr float kSunDX = -6.f;
constexpr float kSunR = 162.f;
constexpr float kRingA = 1.62f;    // the ring: its radius (x the war sun's), how far it is tipped toward us, its turn on the screen
constexpr float kRingTip = 0.30f;
constexpr float kRingTurn = -0.15f;
constexpr float kSkyPar = 0.45f;   // the sky moves this share of the pan
constexpr float kSkyTop = -kSkyPar * kPan;
constexpr float kBeamX = -10.f;    // the supply beam from the planet into the war sun's belly (x from its centre)

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float hashf(uint32_t x) { return float(hash32(x) & 0xffffffu) / 16777215.f; }
float clamp01(float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; }
float smooth01(float t) {
    t = clamp01(t);
    return t * t * (3.f - 2.f * t);
}
float smoothstep(float a, float b, float x) { return smooth01((x - a) / (b - a)); }
float fract(float x) { return x - std::floor(x); }

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
inline float bayer(int x, int y) {
    static const uint8_t m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (float(m[(y & 3) * 4 + (x & 3)]) + 0.5f) / 16.f;
}
inline uint32_t ramp(const uint32_t* r, int n, float v, float d) {
    int i = int(std::floor(clamp01(v) * float(n - 1) + d));
    return r[std::max(0, std::min(n - 1, i))];
}
inline uint32_t addc(uint32_t d, uint32_t s, int a) {  // additive light (a 0..255)
    const int r = std::min(255, int((d >> 16) & 255) + int((s >> 16) & 255) * a / 255);
    const int g = std::min(255, int((d >> 8) & 255) + int((s >> 8) & 255) * a / 255);
    const int b = std::min(255, int(d & 255) + int(s & 255) * a / 255);
    return rgb(r, g, b);
}
inline uint32_t scalec(uint32_t c, float k) {
    k = std::max(0.f, k);
    return rgb(std::min(255, int(float((c >> 16) & 255) * k)), std::min(255, int(float((c >> 8) & 255) * k)), std::min(255, int(float(c & 255) * k)));
}
inline float pow2n(float x, int n) {  // x^(2^n)
    for (int i = 0; i < n; ++i) x *= x;
    return x;
}

// ---------------------------------------------------------------- the palettes (the title's Retrowave: dusk violets, neon)
const uint32_t kSpace[] = {rgb(3, 2, 10),   rgb(5, 4, 15),   rgb(8, 6, 21),   rgb(12, 8, 28),  rgb(18, 10, 37),  rgb(26, 12, 48),
                           rgb(37, 15, 61), rgb(52, 19, 76), rgb(72, 24, 92), rgb(98, 31, 108), rgb(128, 42, 122), rgb(160, 58, 136)};
const uint32_t kIce[] = {rgb(14, 16, 36),   rgb(26, 32, 62),   rgb(40, 52, 92),   rgb(58, 78, 124),  rgb(80, 108, 154),
                         rgb(106, 140, 182), rgb(136, 172, 206), rgb(170, 202, 226), rgb(204, 226, 242), rgb(234, 244, 252)};
const uint32_t kIceNight[] = {rgb(5, 6, 15), rgb(8, 10, 22), rgb(12, 15, 31), rgb(17, 21, 42), rgb(24, 29, 55)};
const uint32_t kRock[] = {rgb(14, 11, 22), rgb(26, 21, 36), rgb(42, 34, 52), rgb(62, 52, 72), rgb(86, 74, 96), rgb(112, 100, 122)};
const uint32_t kSteel[] = {rgb(10, 6, 22),    rgb(20, 12, 38),   rgb(32, 22, 56),   rgb(46, 34, 76),   rgb(62, 50, 96),  rgb(80, 70, 116),
                           rgb(100, 94, 138), rgb(124, 120, 160), rgb(150, 148, 184), rgb(178, 178, 208), rgb(206, 208, 228), rgb(236, 238, 250)};
const uint32_t kHull[] = {rgb(16, 16, 32),   rgb(30, 31, 54),   rgb(48, 50, 80),   rgb(70, 74, 108), rgb(96, 102, 138),
                          rgb(126, 132, 168), rgb(160, 166, 198), rgb(196, 200, 226), rgb(232, 234, 248)};
const uint32_t kHullB[] = {rgb(18, 14, 26),  rgb(34, 28, 46),   rgb(54, 46, 68),   rgb(78, 68, 92),  rgb(106, 94, 118),
                           rgb(138, 124, 146), rgb(170, 156, 176), rgb(204, 192, 208), rgb(236, 228, 238)};
const uint32_t kCore[] = {rgb(70, 8, 30), rgb(140, 20, 40), rgb(210, 50, 40), rgb(250, 110, 40), rgb(255, 180, 70), rgb(255, 232, 160), rgb(255, 252, 230)};

// the light: the sun is off to the right (the same sun as the title's), a little above and toward us; on the planet below it
// is lower - its night side on the left, where the spaceport and the aurora are (seen at another moment, nobody minds)
struct V3 {
    float x, y, z;
};
V3 norm(V3 v) {
    const float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x / n, v.y / n, v.z / n};
}
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
const V3 kL = norm({0.78f, -0.40f, 0.48f});
const V3 kLP = norm({0.90f, -0.08f, -0.05f});

// ---------------------------------------------------------------- the pictures made once
struct Img {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    std::vector<uint8_t> a;  // coverage (0 = nothing there)
};

struct Spot {  // a point of the war sun picture that comes alive (art pixels from its centre)
    int16_t x, y;
};

struct Art {
    float k = 1.f;  // art pixels per game unit
    int W = 640, aw = 0, ah = 0;
    Img sky;     // the whole sky the camera sees, top (kSkyTop) to the screen's bottom, full width
    Img planet;  // the ice planet from its halo's top to the screen's bottom (at the start)
    Img sun;     // the war sun and its ring
    int sunCx = 0, sunCy = 0;  // the war sun's centre inside `sun`
    std::vector<Spot> seams;   // where the plating stops (the welders' sparks)
    std::vector<Spot> lamps;   // beacons on the hull
    float eyeX = 0, eyeY = 0;  // the eye (the captive star), art pixels from the centre
    bool eyeSeen = true;
};

// ---------------------------------------------------------------- the sky
void makeSky(Art& A) {
    const float k = A.k, W = float(A.W);
    const float H = 480.f - kSkyTop;
    Img& s = A.sky;
    s.w = A.aw;
    s.h = int(std::ceil(H * k)) + 2;
    s.px.assign(size_t(s.w) * size_t(s.h), 0);
    const int n = int(sizeof(kSpace) / sizeof(kSpace[0]));
    // the far sun beyond the top right corner of the final view; a small pale moon up to the left
    const float sunX = W + 30.f, sunY = kSkyTop + 30.f;
    const float moonX = W * 0.17f, moonY = kSkyTop + 118.f, moonR = 13.f;
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            const float gx = (float(x) + 0.5f) / k, gy = kSkyTop + (float(y) + 0.5f) / k;
            const float d = bayer(x, y);
            const float u = gx / W, w = (gy - kSkyTop) / H;
            const float base = 0.04f + 0.16f * w;  // darker toward the top of space
            // the nebula: a band of torn wisps from the lower left up to the upper right (as on the title)
            const float off = (1.f - w) - u * 0.62f - 0.14f;
            const float band = std::max(0.f, 1.f - off * off * 7.f);
            const float wx = fbm(gx * 0.006f, gy * 0.006f, 4.7f, 3) - 0.5f, wy = fbm(gx * 0.006f + 9.f, gy * 0.006f, 6.3f, 3) - 0.5f;
            const float nb = fbm(gx * 0.010f + wx * 2.6f, gy * 0.015f + wy * 2.6f, 7.1f, 5);
            const float wisp = smoothstep(0.43f, 0.80f, nb) * band;
            // a teal haze the other way across, lower down
            const float n2 = fbm(gx * 0.008f + 40.f, gy * 0.02f, 5.5f, 4);
            const float off2 = w - 0.55f - u * 0.3f;
            const float teal = smoothstep(0.55f, 0.82f, n2) * std::max(0.f, 1.f - off2 * off2 * 22.f);
            uint32_t col = ramp(kSpace, n, base + wisp * 0.6f, d);
            if (teal > 0.f) col = mix(col, rgb(24, 92, 118), int(teal * 75.f * (0.8f + 0.4f * d)));
            const float sx = gx - sunX, sy = gy - sunY, sd = std::sqrt(sx * sx + sy * sy);
            const float glow = std::max(0.f, 1.f - sd / 300.f);
            if (glow > 0.f) col = mix(col, rgb(255, 160, 214), int(glow * glow * 160.f * (0.7f + 0.3f * d)));
            // the moon: a pale icy crescent lit from the right
            const float mx = (gx - moonX) / moonR, my = (gy - moonY) / moonR, md = mx * mx + my * my;
            if (md < 1.f) {
                const float mz = std::sqrt(1.f - md);
                const float lam = mx * kL.x + my * kL.y + mz * kL.z;
                const float tex = fbm(mx * 3.f + 50.f, my * 3.f, mz * 3.f, 3);
                const float v = clamp01(lam) * (0.55f + 0.45f * tex);
                col = v > 0.04f ? ramp(kIce, 10, v * 0.85f, d) : ramp(kIceNight, 5, 0.3f, d);
            } else if (md < 1.25f) {
                col = mix(col, rgb(120, 200, 230), int((1.25f - md) * 4.f * 60.f));
            }
            s.px[size_t(y) * size_t(s.w) + size_t(x)] = col;
        }
}

// ---------------------------------------------------------------- the ice planet
float planetCx(float W) { return W * 0.5f + kPlanetDX; }
constexpr float kPlanetCy = kHorizon + kPlanetR;
// the spaceports (game units, at the start): one in the night on the left, one in the day on the right
void docks(float W, float& lx, float& ly, float& rx, float& ry) {
    lx = W * 0.20f;
    ly = kHorizon + 58.f;
    rx = W * 0.74f;
    ry = kHorizon + 24.f;
}

void makePlanet(Art& A) {
    const float k = A.k, W = float(A.W);
    const float top = kHorizon - kHalo;
    Img& p = A.planet;
    p.w = A.aw;
    p.h = int(std::ceil((480.f - top) * k)) + 2;
    p.px.assign(size_t(p.w) * size_t(p.h), 0);
    p.a.assign(size_t(p.w) * size_t(p.h), 0);
    const float cx = planetCx(W), cy = kPlanetCy, R = kPlanetR;
    float dlx, dly, drx, dry;
    docks(W, dlx, dly, drx, dry);
    const V3 H = norm({kLP.x, kLP.y, kLP.z + 1.f});  // the sun's glint
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x) {
            const float gx = (float(x) + 0.5f) / k, gy = top + (float(y) + 0.5f) / k;
            const float dx = gx - cx, dy = gy - cy, dd = std::sqrt(dx * dx + dy * dy);
            const size_t i = size_t(y) * size_t(p.w) + size_t(x);
            const float d = bayer(x, y);
            if (dd > R + kHalo) continue;
            if (dd > R) {  // the atmosphere's glow above the limb: cyan in the day, violet in the night
                const float h = 1.f - (dd - R) / kHalo;
                const float lam = (dx / dd) * kLP.x + (dy / dd) * kLP.y;
                const float day = smoothstep(-0.25f, 0.35f, lam);
                const float g = h * h * (0.45f + 0.55f * day);
                if (g * 255.f < d * 40.f) continue;
                p.px[i] = mix(rgb(150, 60, 210), rgb(120, 236, 250), int(day * 255.f));
                p.a[i] = uint8_t(std::min(255.f, g * 235.f));
                continue;
            }
            const V3 n{dx / R, dy / R, std::sqrt(std::max(0.f, 1.f - (dd / R) * (dd / R)))};
            const float lam = dot(n, kLP);
            const float day = smoothstep(-0.04f, 0.16f, lam);
            // the surface: snowfields and blue glacier ice, dark ridges of rock, frozen seas, crevasses, rusty lineae
            const float sx = n.x * 1.0f + n.z * 0.3f, sy = n.y, sz = n.z * 1.0f - n.x * 0.3f;  // (a turned frame: no grid in the noise)
            const float big = fbm(sx * 6.2f + 7.f, sy * 6.2f, sz * 6.2f, 5);
            const bool sea = smoothstep(0.372f, 0.408f, big) < d;  // (a dithered shore)
            const float alb = fbm(sx * 10.f, sy * 10.f + 3.f, sz * 10.f, 5);
            const float rock = smoothstep(0.60f, 0.70f, fbm(sx * 11.f + 21.f, sy * 11.f, sz * 11.f, 5));
            // relief: the ground's height a little toward the sun, against here
            const float h0 = fbm(sx * 34.f + 5.f, sy * 34.f, sz * 34.f, 4);
            const float he = 0.005f;
            const float h1 = fbm((sx + kLP.x * he) * 34.f + 5.f, (sy + kLP.y * he) * 34.f, (sz + kLP.z * he) * 34.f, 4);
            const float relief = (h1 - h0) * 2.6f;
            const float wx = fbm(sx * 12.f + 31.f, sy * 12.f, sz * 12.f, 3) - 0.5f;
            const float lin = std::fabs(fbm(sx * 26.f + wx * 1.8f, sy * 26.f, sz * 26.f + wx, 4) - 0.5f);
            const float crev = vnoise(sx * 150.f, sy * 150.f, sz * 150.f);
            uint32_t col;
            if (day > 0.f) {
                float v = (sea ? 0.52f + 0.12f * alb : 0.66f + 0.30f * alb + 0.10f * (big - 0.5f)) * (0.22f + 0.78f * clamp01(lam * 1.8f));
                if (!sea) v += relief * (0.6f + 0.6f * rock);
                const uint32_t* r = kIce;
                int rn = 10;
                if (rock > 0.f && !sea) {
                    v *= 0.85f;
                    if (rock > d) {
                        r = kRock;
                        rn = 6;
                    }
                }
                col = ramp(r, rn, v, d);
                if (sea) col = mix(col, rgb(40, 120, 168), 70);  // the frozen seas are bluer
                if (lin < 0.008f && !sea) col = mix(col, rgb(150, 82, 66), int((1.f - lin / 0.008f) * 120.f));  // lineae
                if (crev > 0.88f && !sea && rock < 0.5f) col = mix(col, rgb(30, 60, 120), int((crev - 0.88f) / 0.12f * 180.f));
                if (sea) {  // the sun's glint off the sea ice
                    const float sp = pow2n(clamp01(dot(n, H)), 5);
                    if (sp > 0.05f) col = addc(col, rgb(220, 240, 255), int(std::min(1.f, sp) * 200.f));
                }
            } else {
                col = 0;
            }
            if (day < 1.f) {  // the night: the ice faint under the stars, the dusk a band of rose
                const uint32_t nc = ramp(kIceNight, 5, (sea ? 0.30f : 0.42f) + 0.45f * alb - rock * 0.3f + relief * 0.4f, d);
                col = day > 0.f ? mix(nc, col, int(day * 255.f)) : nc;
                const float dusk = std::max(0.f, 1.f - std::fabs(lam - 0.03f) / 0.07f);
                if (dusk > 0.f) col = mix(col, rgb(196, 92, 150), int(dusk * 45.f));
                // the lights of the spaceports and settlements in the dark
                const float night = 1.f - day;
                float lit = 0.f;
                for (int q = 0; q < 2; ++q) {
                    const float ex = (gx - (q ? drx : dlx)) / (q ? 36.f : 90.f), ey = (gy - (q ? dry : dly)) / (q ? 7.f : 26.f);
                    lit = std::max(lit, std::max(0.f, 1.f - (ex * ex + ey * ey)));
                }
                const uint32_t hh = hash32(uint32_t(x) * 2654435761u ^ uint32_t(y) * 40503u);
                // (the streets: lines across the port, lights strung along them)
                const bool street = lit > 0.f && (fract(gx * 0.11f + gy * 0.02f) < 0.12f || fract(gy * 0.45f) < 0.1f);
                if (night > 0.3f && ((lit > 0.f && (hh & 255u) < uint32_t(lit * (street ? 170.f : 45.f))) || (hh & 2047u) < 4u)) {
                    const uint32_t lc = (hh >> 9) % 5u == 0 ? rgb(120, 230, 255) : (hh >> 9) % 5u == 1 ? rgb(255, 120, 210) : rgb(255, 196, 110);
                    col = mix(col, lc, int(std::min(1.f, night) * 230.f));
                } else if (lit > 0.f && night > 0.3f) {
                    col = mix(col, rgb(130, 62, 40), int(lit * night * 110.f));  // their haze
                }
            }
            // high thin clouds, streaked along the latitudes
            const float cl = fbm(sx * 9.f + 60.f, sy * 40.f, sz * 9.f, 5);
            if (cl > 0.58f) {
                const float cv = (cl - 0.58f) / 0.42f;
                if (cv > d * 0.8f) col = mix(col, day > 0.3f ? rgb(236, 244, 255) : rgb(30, 34, 60), int(std::min(1.f, cv * 1.6f) * 170.f));
            }
            // the atmosphere along the limb: neon cyan by day, a violet thread by night
            const float rim = pow2n(1.f - n.z, 3);
            if (rim > 0.02f) col = mix(col, mix(rgb(120, 50, 190), rgb(130, 236, 250), int(day * 255.f)), int(std::min(1.f, rim * 1.4f) * 220.f));
            p.px[i] = col;
            p.a[i] = 255;
        }
}

// ---------------------------------------------------------------- the war sun
// The body's frame: its pole leans toward us and a little right, so the northern plating and the equator's curve show.
struct Body {
    V3 P, F, E;  // north, the meridian facing us, east
};
Body body() {
    Body b;
    b.P = norm({0.16f, -0.92f, 0.36f});
    const V3 z{0.f, 0.f, 1.f};
    const float pz = dot(z, b.P);
    b.F = norm({z.x - b.P.x * pz, z.y - b.P.y * pz, z.z - b.P.z * pz});
    b.E = cross(b.F, b.P);  // (east to the right on the screen: y runs down)
    return b;
}
// the ring's plane: its normal (toward us and up); the ring is a circle of radius kRingA * R around the centre
V3 ringNormal() {
    const float c = std::cos(kRingTurn), s = std::sin(kRingTurn);
    const float e = std::asin(kRingTip);  // how far we look down on it
    const V3 n0{0.f, -std::cos(e), std::sin(e)};
    return norm({n0.x * c - n0.y * s, n0.x * s + n0.y * c, n0.z});
}
// the eye (a captive star in a great dish), in the body's frame: up and to the left of the middle
constexpr float kEyeLat = 0.40f, kEyeLon = -0.62f, kEyeR = 0.30f;
// the part still being built: up and to the right
bool building(float lat, float lon, uint32_t panel, float& depth) {
    const float la0 = -0.10f, la1 = 1.30f, lo0 = 0.30f, lo1 = 1.55f;
    depth = std::min(std::min(lat - la0, la1 - lat), std::min(lon - lo0, lo1 - lon));
    if (depth <= 0.f) return false;
    return depth * 3.2f + (hashf(panel) - 0.5f) * 0.55f > 0.16f;
}

void makeSun(Art& A) {
    const float k = A.k, R = kSunR * k;  // art pixels
    const float ringR = kRingA * R;
    Img& s = A.sun;
    const int halfW = int(ringR * 1.08f) + 6, up = int(R * 1.32f) + 6, down = int(R * 1.12f) + 6;
    s.w = 2 * halfW;
    s.h = up + down;
    A.sunCx = halfW;
    A.sunCy = up;
    s.px.assign(size_t(s.w) * size_t(s.h), 0);
    s.a.assign(size_t(s.w) * size_t(s.h), 0);
    const Body B = body();
    const V3 N = ringNormal();
    const V3 eye = norm({std::cos(kEyeLat) * std::sin(kEyeLon) * B.E.x + std::sin(kEyeLat) * B.P.x + std::cos(kEyeLat) * std::cos(kEyeLon) * B.F.x,
                         std::cos(kEyeLat) * std::sin(kEyeLon) * B.E.y + std::sin(kEyeLat) * B.P.y + std::cos(kEyeLat) * std::cos(kEyeLon) * B.F.y,
                         std::cos(kEyeLat) * std::sin(kEyeLon) * B.E.z + std::sin(kEyeLat) * B.P.z + std::cos(kEyeLat) * std::cos(kEyeLon) * B.F.z});
    A.eyeX = eye.x * R;
    A.eyeY = eye.y * R;
    A.eyeSeen = eye.z > 0.f;
    const V3 bounce = norm({0.f, 1.f, 0.35f});  // the ice planet below lights its underside
    // the middle of the opening (where the forge burns brightest), art pixels from the centre
    float zoneX, zoneY;
    {
        const float la = 0.62f, lo = 0.90f;
        const V3 z{std::cos(la) * std::sin(lo) * B.E.x + std::sin(la) * B.P.x + std::cos(la) * std::cos(lo) * B.F.x,
                   std::cos(la) * std::sin(lo) * B.E.y + std::sin(la) * B.P.y + std::cos(la) * std::cos(lo) * B.F.y, 0.f};
        zoneX = z.x * R;
        zoneY = z.y * R;
    }
    const float bandH = kPi / 26.f;
    std::vector<uint8_t> state(size_t(s.w) * size_t(s.h), 0);  // 1 plated, 2 skeleton (for the seams)
    // the ring's two surfaces: we see its top; the circle's frame in its plane
    const V3 ru = norm(cross(N, {0.f, 0.f, 1.f}));
    const V3 rv = cross(N, ru);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            const float px = float(x - A.sunCx) + 0.5f, py = float(y - A.sunCy) + 0.5f;
            const size_t i = size_t(y) * size_t(s.w) + size_t(x);
            const float d = bayer(x, y);
            // what the eye ray (straight into the screen) meets: the sphere and the ring's plane
            const float r2 = px * px + py * py;
            const bool onSphere = r2 < R * R;
            const float zs = onSphere ? std::sqrt(R * R - r2) : -1e9f;
            float zr = -1e9f, rho = 0.f, theta = 0.f;
            bool onRing = false;
            if (std::fabs(N.z) > 1e-4f) {
                const float z = -(px * N.x + py * N.y) / N.z;
                const V3 Q{px, py, z};
                const float qr = std::sqrt(Q.x * Q.x + Q.y * Q.y + Q.z * Q.z);
                rho = qr / ringR;
                theta = std::atan2(dot(Q, rv), dot(Q, ru));
                // the ring: a deck from 0.80 to 1.0, docking arms reaching out to 1.13 in four places
                const float th = theta < 0.f ? theta + 2.f * kPi : theta;
                const float armA = std::fabs(std::remainder(th - 0.35f, kPi * 0.5f));
                const bool arm = rho >= 1.f && rho < 1.13f && armA < 0.022f;
                if ((rho > 0.74f && rho < 1.f) || arm) {
                    onRing = true;
                    zr = z;
                }
            }
            if (!onSphere && !onRing) continue;
            uint32_t col = 0;
            if (onRing && zr > zs) {
                // ---------------- the ring
                const float th = theta < 0.f ? theta + 2.f * kPi : theta;
                const float seg = th / (2.f * kPi) * 180.f;
                const int segI = int(seg);
                // unfinished stretches: only the frame (two hoops, spokes, braces)
                const bool gap = (th > 3.55f && th < 4.35f) || (th > 0.55f && th < 0.92f) || (th > 5.6f && th < 5.75f);
                const float lip = rho - 0.74f;
                bool solid = true;
                float shade;
                if (gap && rho < 1.f) {
                    const float f = fract(seg);
                    const bool hoop = std::fabs(rho - 0.755f) < 0.010f || std::fabs(rho - 0.985f) < 0.012f || std::fabs(rho - 0.87f) < 0.006f;
                    const bool spoke = f < 0.14f;
                    const float bu = fract(seg * 0.5f), bv = (rho - 0.74f) / 0.26f;
                    const bool brace = std::fabs(bu - bv) < 0.07f || std::fabs(bu + bv - 1.f) < 0.07f;
                    if (!hoop && !spoke && !brace) continue;
                    solid = false;
                    shade = 0.75f;
                } else {
                    shade = 0.70f;
                    if (rho >= 0.95f && rho < 1.f) shade = 0.92f;   // the outer wall, catching the light
                    if (lip < 0.03f) shade = 0.30f;                 // the inner lip in shadow
                    if (rho > 0.82f && rho < 0.85f) shade *= 0.55f; // a rail channel round the deck
                    if (rho > 0.905f && rho < 0.915f) shade = 0.98f;
                    if (fract(seg) < 0.08f) shade *= 0.72f;         // panel seams
                    shade *= 0.86f + 0.26f * hashf(uint32_t(segI) * 977u + uint32_t(rho * 9.f));
                }
                // its light: the sun on its top; the outer wall faces out (lit on the side toward the sun)
                const V3 Q{px, py, zr};
                float lam = 0.30f + 0.70f * clamp01(dot(N, kL) + 0.25f);
                if (rho >= 0.95f) lam = 0.30f + 0.70f * clamp01(dot(norm(Q), kL) + 0.3f);
                // the war sun's shadow across the ring
                const float b = dot(Q, kL), c = dot(Q, Q) - R * R;
                const float disc = b * b - c;
                if (disc > 0.f && -b + std::sqrt(disc) > 0.f && c > 0.f && b < 0.f) lam *= 0.18f;
                col = ramp(kSteel, 12, shade * lam, d);
                // lights along the outer wall (windows) and on the deck
                if (solid && rho >= 0.955f && rho < 0.99f) {
                    const uint32_t hh = hash32(uint32_t(seg * 3.f) * 2246822519u);
                    if ((hh & 7u) < 3u && fract(seg * 3.f) < 0.5f) col = (hh >> 8) % 3u == 0 ? rgb(120, 236, 255) : rgb(255, 206, 120);
                }
                if (solid && rho > 0.86f && rho < 0.90f && fract(seg) > 0.45f && fract(seg) < 0.55f &&
                    (hash32(uint32_t(segI) * 7919u) & 3u) == 0)
                    col = rgb(255, 90, 210);  // a magenta marker light on some deck plates
                s.px[i] = col;
                s.a[i] = 255;
                continue;
            }
            // ---------------- the sphere
            const V3 n{px / R, py / R, zs / R};
            const float lat = std::asin(std::max(-1.f, std::min(1.f, dot(n, B.P))));
            const float lon = std::atan2(dot(n, B.E), dot(n, B.F));
            const int band = int(std::floor((lat + kPi * 0.5f) / bandH));
            const float bandU = fract((lat + kPi * 0.5f) / bandH);
            const float latMid = -kPi * 0.5f + (float(band) + 0.5f) * bandH;
            const int panels = std::max(6, int(std::round(64.f * std::cos(latMid))));
            const float pu = (lon + kPi) / (2.f * kPi) * float(panels);
            const int panelI = int(std::floor(pu));
            const float panelU = fract(pu);
            const uint32_t panel = uint32_t(band) * 1009u + uint32_t(panelI) * 7u + 13u;
            float lam = dot(n, kL);
            const float key = clamp01(lam);
            const float fill = clamp01(dot(n, bounce)) * 0.38f;
            // the eye: a great dish with a star held in it
            const float eyeCos = dot(n, eye);
            const float eyeAng = std::acos(std::max(-1.f, std::min(1.f, eyeCos)));
            float depth = 0.f;
            const bool build = building(lat, lon, panel, depth);
            const bool trench = band == 9 || band == 16 || band == 20 || band == 6;
            const bool belt = std::fabs(lat) < bandH * 0.5f;
            float v;
            uint32_t emit = 0;
            int emitA = 0;
            if (eyeAng < kEyeR) {
                const float q = eyeAng / kEyeR;  // 0 middle .. 1 the dish's rim
                if (q < 0.30f) {               // the captive star
                    const float hot = 1.f - q / 0.30f;
                    col = ramp(kCore, 7, 0.35f + 0.65f * hot, d);
                    s.px[i] = col;
                    s.a[i] = 255;
                    state[i] = 1;
                    continue;
                }
                // the dish: lit the other way (it is hollow), rings and ribs, the star's glow on it
                const V3 inward{-n.x, -n.y, n.z};
                const float dl = clamp01(dot(inward, kL) * 0.6f + 0.35f);
                const float rings = fract(q * 9.f) < 0.18f ? 0.6f : 1.f;
                const V3 tang = norm({n.x - eye.x * eyeCos, n.y - eye.y * eyeCos, n.z - eye.z * eyeCos});
                const float ribA = std::atan2(dot(tang, cross(eye, B.P)), dot(tang, B.P));
                const float ribs = fract(ribA / (2.f * kPi) * 36.f) < 0.12f ? 0.65f : 1.f;
                v = (0.25f + 0.55f * dl) * rings * ribs;
                const float glow = 1.f - (q - 0.30f) / 0.70f;
                emit = rgb(255, 120, 60);
                emitA = int(glow * glow * 150.f);
                if (q > 0.93f) v = 0.75f * (0.4f + 0.6f * key);  // its raised rim
                col = ramp(kSteel, 12, v, d);
                if (emitA > 0) col = addc(col, emit, emitA);
                s.px[i] = col;
                s.a[i] = 255;
                state[i] = 1;
                continue;
            }
            if (build) {
                // ---------------- the unfinished part: girders over the dark inside, lit by the forge at its heart
                const bool girder = panelU < 0.09f || bandU < 0.10f;
                const bool brace = (panelI + band) % 3 == 0 && std::fabs(panelU - bandU) < 0.05f;
                if (girder || brace) {
                    v = (0.30f + 0.65f * key + fill * 0.5f) * (brace ? 0.85f : 1.f);
                    col = ramp(kSteel, 12, v, d);
                    if (key < 0.15f) col = mix(col, rgb(255, 120, 60), 60);  // the forge's light on the frame from inside
                    state[i] = 2;
                } else {
                    const float fx = px - zoneX, fy = py - zoneY, fd = std::sqrt(fx * fx + fy * fy) / (R * 0.62f);
                    const float nz = fbm(px * 0.05f, py * 0.05f, 3.f, 4);
                    const float forge = clamp01(1.f - fd) * clamp01(0.2f + 1.1f * (nz - 0.35f));
                    col = forge > 0.22f ? ramp(kCore, 7, (forge - 0.22f) * 0.75f, d)
                                        : mix(rgb(10, 5, 20), rgb(70, 18, 40), int(clamp01(forge * 4.5f) * 255.f));
                    if (fract(bandU * 3.f) < 0.10f && (hash32(panel * 31u + uint32_t(panelU * 5.f)) & 3u) == 0)
                        col = mix(col, rgb(255, 196, 110), 200);  // the inner decks' lights
                    state[i] = 2;
                }
                s.px[i] = col;
                s.a[i] = 255;
                continue;
            }
            // ---------------- the plating
            const bool seamU = panelU < 0.06f || panelU > 0.97f, seamV = bandU < 0.07f;
            float alb = 0.80f + 0.22f * hashf(panel);
            // greebles: each plate is split again into cells of other shades
            const int cu = int(panelU * 3.f), cv = int(bandU * 2.f);
            alb *= 0.90f + 0.16f * hashf(panel * 13u + uint32_t(cu * 2 + cv));
            if (trench) alb *= 0.42f;
            if (belt) alb *= 1.12f;
            v = alb * (0.08f + 0.80f * key + fill);
            if (seamU || seamV) v *= 0.62f;
            // a sheen where the sun catches the plates
            const float spec = pow2n(clamp01(dot(n, norm({kL.x, kL.y, kL.z + 1.f}))), 5);
            v += spec * 0.35f;
            col = ramp(kSteel, 12, v, d);
            if (lam < 0.1f) col = mix(col, rgb(40, 16, 72), int(clamp01(0.1f - lam) * 1.6f * 110.f));  // the night side: violet
            if (fill > 0.12f && key < 0.2f) col = mix(col, rgb(70, 170, 200), int((fill - 0.12f) * 150.f));  // the planet's cyan light
            // windows: rows of light in the trenches, a few across the plates in the dark
            if (trench && bandU > 0.35f && bandU < 0.65f) {
                const uint32_t hh = hash32(uint32_t(pu * 6.f) * 2654435761u + uint32_t(band));
                if ((hh & 3u) != 0 && fract(pu * 6.f) < 0.55f) col = (hh >> 6) % 7u == 0 ? rgb(120, 236, 255) : rgb(255, 204, 120);
            } else if (key < 0.3f && (hash32(panel * 131u + uint32_t(cu * 2 + cv)) & 15u) == 0 && panelU > 0.25f && panelU < 0.6f &&
                       bandU > 0.38f && bandU < 0.58f) {
                col = rgb(255, 210, 140);
            }
            if (belt && bandU > 0.44f && bandU < 0.56f && fract(pu * 4.f) < 0.3f) col = rgb(255, 90, 210);  // the equator's neon
            // the ring's shadow on the hull
            {
                const V3 S{px, py, zs};
                const float ln = dot(kL, N);
                if (std::fabs(ln) > 1e-4f) {
                    const float t = -dot(S, N) / ln;
                    if (t > 0.f) {
                        const V3 Q{S.x + kL.x * t, S.y + kL.y * t, S.z + kL.z * t};
                        const float qr = std::sqrt(dot(Q, Q)) / ringR;
                        const float th = std::atan2(dot(Q, rv), dot(Q, ru));
                        const float th2 = th < 0.f ? th + 2.f * kPi : th;
                        const bool gap = (th2 > 3.55f && th2 < 4.35f) || (th2 > 0.55f && th2 < 0.92f);
                        if (qr > 0.74f && qr < 1.f && !gap) col = mix(col, rgb(10, 6, 24), 105);
                    }
                }
            }
            // the rim: the sun's edge light up to the right, the planet's cyan below
            const float edge = pow2n(1.f - n.z, 3);
            if (edge > 0.05f) {
                if (lam > 0.f) col = mix(col, rgb(255, 214, 236), int(std::min(1.f, edge * 1.5f) * 140.f * clamp01(lam * 2.f)));
                else if (n.y > 0.2f) col = mix(col, rgb(90, 220, 240), int(std::min(1.f, edge * 1.5f) * 150.f * n.y));
            }
            s.px[i] = col;
            s.a[i] = 255;
            state[i] = 1;
            (void)depth;
        }
    // the seams: plated pixels next to the skeleton (where the welders work)
    for (int y = 1; y + 1 < s.h; y += 2)
        for (int x = 1; x + 1 < s.w; x += 2) {
            const size_t i = size_t(y) * size_t(s.w) + size_t(x);
            if (state[i] != 1) continue;
            if (state[i - 1] == 2 || state[i + 1] == 2 || state[i - size_t(s.w)] == 2 || state[i + size_t(s.w)] == 2)
                A.seams.push_back({int16_t(x - A.sunCx), int16_t(y - A.sunCy)});
        }
    // scaffolds: struts standing out of the unfinished hull's edge, with cranes, and beacons on them
    auto putPx = [&](int x, int y, uint32_t c) {
        if (x < 0 || y < 0 || x >= s.w || y >= s.h) return;
        const size_t i = size_t(y) * size_t(s.w) + size_t(x);
        s.px[i] = c;
        s.a[i] = 255;
    };
    auto line = [&](float x0, float y0, float x1, float y1, uint32_t c0, uint32_t c1) {
        const int n = int(std::max(std::fabs(x1 - x0), std::fabs(y1 - y0))) + 1;
        for (int j = 0; j <= n; ++j) {
            const float u = float(j) / float(n);
            putPx(A.sunCx + int(std::floor(x0 + (x1 - x0) * u)), A.sunCy + int(std::floor(y0 + (y1 - y0) * u)), mix(c0, c1, int(u * 255.f)));
        }
    };
    const uint32_t strutL = rgb(170, 178, 204), strutD = rgb(72, 79, 112);
    for (int j = 0; j < 9; ++j) {
        const float ang = -1.32f + 0.115f * float(j) + 0.03f * hashf(uint32_t(j) * 31u);  // screen angle from "up", to the right
        const float len = R * (0.07f + 0.13f * hashf(uint32_t(j) * 57u + 3u));
        const float ux = std::cos(ang), uy = std::sin(ang);
        const float bx = ux * R * 0.985f, by = uy * R * 0.985f;
        const float ex = ux * (R + len), ey = uy * (R + len);
        line(bx, by, ex, ey, strutD, strutL);
        line(bx + 2.f * -uy, by + 2.f * ux, ex + 2.f * -uy, ey + 2.f * ux, strutD, strutD);
        for (float q = 0.25f; q < 1.f; q += 0.25f)  // rungs
            line(bx + (ex - bx) * q, by + (ey - by) * q, bx + (ex - bx) * q + 2.f * -uy, by + (ey - by) * q + 2.f * ux, strutL, strutL);
        if (j % 3 == 1) {  // a crane: an arm out to the side
            const float ax = ex + -uy * len * 0.7f, ay = ey + ux * len * 0.7f;
            line(ex, ey, ax, ay, strutL, strutD);
            line(ax, ay, ax, ay + len * 0.35f, strutD, strutD);
        }
        A.lamps.push_back({int16_t(std::lround(ex)), int16_t(std::lround(ey))});
    }
    // spires on the north cap
    for (int j = 0; j < 4; ++j) {
        const float ang = -0.55f + 0.24f * float(j);
        const float ux = std::sin(ang), uy = -std::cos(ang);
        const float bx = ux * R * 0.99f, by = uy * R * 0.99f;
        const float len = R * (0.10f + 0.06f * float(j % 2));
        line(bx, by, bx + ux * len, by + uy * len, strutD, rgb(200, 206, 226));
        A.lamps.push_back({int16_t(std::lround(bx + ux * len)), int16_t(std::lround(by + uy * len))});
    }
}

// ---------------------------------------------------------------- making it in the background
struct Maker {
    std::thread t;
    std::atomic<bool> busy{false}, done{false};
    float k = 0.f;
    int W = 0, aw = 0, ah = 0;
    std::unique_ptr<Art> next;
    void start(float k_, int W_, int aw_, int ah_) {
        if (t.joinable()) t.join();
        k = k_;
        W = W_;
        aw = aw_;
        ah = ah_;
        busy = true;
        done = false;
        t = std::thread([this] {
            auto a = std::make_unique<Art>();
            a->k = k;
            a->W = W;
            a->aw = aw;
            a->ah = ah;
            makeSky(*a);
            makePlanet(*a);
            makeSun(*a);
            next = std::move(a);
            busy = false;
            done = true;
        });
    }
    ~Maker() {
        if (t.joinable()) t.join();
    }
};
std::unique_ptr<Art> g_art;
Maker g_maker;

int blowOf(float scale) { return scale >= 1.75f ? 2 : 1; }

// the art for this screen (null while it is made)
const Art* artFor(float k, int W, int aw, int ah) {
    if (g_maker.done) {
        if (g_maker.t.joinable()) g_maker.t.join();
        g_maker.done = false;
        if (g_maker.next) g_art = std::move(g_maker.next);
    }
    const bool fits = g_art && std::fabs(g_art->k - k) < 1e-3f && g_art->W == W && g_art->aw == aw && g_art->ah == ah;
    if (!fits && !(g_maker.busy && std::fabs(g_maker.k - k) < 1e-3f && g_maker.W == W && g_maker.aw == aw && g_maker.ah == ah))
        g_maker.start(k, W, aw, ah);
    return fits ? g_art.get() : nullptr;
}

void screenArt(const Canvas& c, float& k, int& W, int& aw, int& ah, int& blow) {
    blow = blowOf(c.scale());
    k = c.scale() / float(blow);
    W = c.fullWidth();
    aw = (c.pw() + blow - 1) / blow;
    ah = (c.ph() + blow - 1) / blow;
}

// ---------------------------------------------------------------- the frame
struct Frame {
    int w = 0, h = 0;
    float k = 1.f;
    std::vector<uint32_t> px;
    inline void put(int x, int y, uint32_t c) {
        if (x >= 0 && y >= 0 && x < w && y < h) px[size_t(y) * size_t(w) + size_t(x)] = c;
    }
    inline void blend(int x, int y, uint32_t c, int a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        uint32_t& d = px[size_t(y) * size_t(w) + size_t(x)];
        d = mix(d, c, std::min(255, a));
    }
    inline void add(int x, int y, uint32_t c, int a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        uint32_t& d = px[size_t(y) * size_t(w) + size_t(x)];
        d = addc(d, c, std::min(255, a));
    }
    // a soft round glow (additive), radius in art pixels
    void glow(float cx, float cy, float r, uint32_t c, int a) {
        if (r < 0.5f || a <= 0) return;
        const int x0 = int(std::floor(cx - r)), x1 = int(std::ceil(cx + r)), y0 = int(std::floor(cy - r)), y1 = int(std::ceil(cy + r));
        for (int y = std::max(0, y0); y <= std::min(h - 1, y1); ++y)
            for (int x = std::max(0, x0); x <= std::min(w - 1, x1); ++x) {
                const float dx = (float(x) + 0.5f - cx) / r, dy = (float(y) + 0.5f - cy) / r, q = 1.f - (dx * dx + dy * dy);
                if (q <= 0.f) continue;
                const int aa = int(float(a) * q * q);
                if (aa > int(bayer(x, y) * 24.f)) add(x, y, c, aa);
            }
    }
};
Frame g_frame;

// blit a picture (with its coverage) with its top-left at art pixel (ox, oy)
void blitImg(Frame& F, const Img& im, int ox, int oy) {
    const int y0 = std::max(0, oy), y1 = std::min(F.h, oy + im.h);
    const int x0 = std::max(0, ox), x1 = std::min(F.w, ox + im.w);
    if (x0 >= x1) return;
    for (int y = y0; y < y1; ++y) {
        const uint32_t* s = &im.px[size_t(y - oy) * size_t(im.w)];
        const uint8_t* a = im.a.empty() ? nullptr : &im.a[size_t(y - oy) * size_t(im.w)];
        uint32_t* d = &F.px[size_t(y) * size_t(F.w)];
        for (int x = x0; x < x1; ++x) {
            const int sx = x - ox;
            if (!a || a[sx] == 255) d[x] = s[sx];
            else if (a[sx]) d[x] = mix(d[x], s[sx], a[sx]);
        }
    }
}

// ---------------------------------------------------------------- the stars: three layers, drifting a little, lifted by the pan
void drawStars(Frame& F, float W, uint32_t t, float cam) {
    static const float kPar[3] = {0.30f, 0.42f, 0.56f}, kSpeed[3] = {1.5f, 3.f, 6.f};
    static const int kCount[3] = {110, 64, 34};
    for (int layer = 0; layer < 3; ++layer) {
        const float span = 480.f + kPar[layer] * kPan;
        const int n = int(float(kCount[layer]) * W / 640.f * span / 480.f);
        for (int i = 0; i < n; ++i) {
            const uint32_t h = hash32(uint32_t(i) * 7919u + uint32_t(layer) * 104729u + 31u);
            const double dd = double(t) * 0.001 * double(kSpeed[layer]);
            double gx = double(h % uint32_t(W * 16.f)) / 16.0 - (dd - std::floor(dd / double(W)) * double(W));
            if (gx < 0) gx += double(W);
            const float gy = -kPar[layer] * kPan + float((h >> 12) % 4800u) / 4800.f * span + kPar[layer] * cam;
            if (gy < -2.f || gy > 482.f) continue;
            const uint32_t pick = (h >> 24) % 10u;
            const uint32_t col = pick == 0 ? rgb(255, 110, 220) : pick == 1 ? rgb(110, 230, 255) : pick == 2 ? rgb(255, 214, 160) : rgb(232, 234, 248);
            const float tw = 0.7f + 0.3f * float(std::sin(double(t) * 0.003 + double(h & 255u)));
            const int a = int(float(110 + layer * 60) * tw);
            const int x = int(float(gx) * F.k), y = int(gy * F.k);
            F.blend(x, y, col, a);
            if (layer == 2 && (h & 7u) == 0) {
                F.blend(x - 1, y, col, a / 3);
                F.blend(x + 1, y, col, a / 3);
                F.blend(x, y - 1, col, a / 3);
                F.blend(x, y + 1, col, a / 3);
            }
        }
    }
}

// ---------------------------------------------------------------- the aurora over the planet's night side
void drawAurora(Frame& F, float W, uint32_t t, float cam) {
    const float tt = float(t) * 0.001f;
    const float cx = planetCx(W), cy = kPlanetCy + cam;
    for (int x = 0; x < F.w; ++x) {
        const float gx = (float(x) + 0.5f) / F.k;
        const float dx = gx - cx;
        if (std::fabs(dx) >= kPlanetR) continue;
        const float limb = cy - std::sqrt(kPlanetR * kPlanetR - dx * dx);  // the horizon's height here
        // the night: toward the left of the limb
        const float lam = (dx / kPlanetR) * kLP.x + (-std::sqrt(std::max(0.f, 1.f - (dx / kPlanetR) * (dx / kPlanetR)))) * kLP.y;
        const float night = smoothstep(0.12f, -0.12f, lam);
        if (night <= 0.f) continue;
        // curtains: broad folds drifting along the horizon, fine rays in them, brightening and fading
        const float fold = 0.5f + 0.5f * std::sin(gx * 0.021f + tt * 0.45f + 2.f * std::sin(gx * 0.0075f - tt * 0.22f));
        const float rays = 0.62f + 0.38f * std::sin(gx * 0.55f + tt * 2.1f + 3.f * std::sin(gx * 0.13f + tt * 0.6f));
        float I = fold * fold * rays * night * (0.7f + 0.3f * std::sin(tt * 0.8f + gx * 0.004f));
        if (I < 0.04f) continue;
        const float base = 7.f + 6.f * std::sin(gx * 0.03f + tt * 0.5f);
        const float hgt = 38.f + 22.f * std::sin(gx * 0.015f + tt * 0.3f + 1.f);
        const int yb = int((limb - base) * F.k), yt = int((limb - base - hgt) * F.k);
        for (int y = std::max(0, yt); y <= std::min(F.h - 1, yb); ++y) {
            const float q = float(yb - y) / std::max(1.f, float(yb - yt));  // 0 at the bottom .. 1 at the top
            const uint32_t c = mix(rgb(70, 255, 170), rgb(255, 70, 210), int(smooth01(q * 1.3f) * 255.f));
            const float a = I * (q < 0.12f ? q / 0.12f : 1.f) * (1.f - q) * (1.f - q) * 210.f;
            if (a > bayer(x, y) * 30.f) F.add(x, y, c, int(a));
        }
    }
}

// ---------------------------------------------------------------- the two ships (drawn as shapes at any size)
struct Shape {
    int n;
    float u[12], v[12];  // across (right +), along (nose +1 .. tail -1)
    uint8_t part;      // 0 hull, 1 wing, 2 pod, 3 dark, 4 canopy
};
// the first: a sleek courier, swept wings, two engines
const Shape kShipAShapes[] = {
    {4, {0.12f, 0.78f, 0.80f, 0.15f}, {0.18f, -0.52f, -0.70f, -0.50f}, 1},
    {4, {-0.12f, -0.78f, -0.80f, -0.15f}, {0.18f, -0.52f, -0.70f, -0.50f}, 1},
    {4, {0.17f, 0.31f, 0.31f, 0.17f}, {-0.25f, -0.25f, -0.97f, -0.97f}, 2},
    {4, {-0.17f, -0.31f, -0.31f, -0.17f}, {-0.25f, -0.25f, -0.97f, -0.97f}, 2},
    {7, {0.f, 0.12f, 0.17f, 0.14f, -0.14f, -0.17f, -0.12f}, {1.f, 0.55f, -0.15f, -0.88f, -0.88f, -0.15f, 0.55f}, 0},
    {4, {0.04f, 0.06f, -0.06f, -0.04f}, {0.68f, 0.30f, 0.30f, 0.68f}, 4},
};
const float kShipAEngines[][2] = {{0.24f, -0.99f}, {-0.24f, -0.99f}};
// the second: a heavy cruiser - a long armoured hull, two great engine nacelles on pylons, a bridge tower, three engines
const Shape kShipBShapes[] = {
    {4, {0.16f, 0.40f, 0.40f, 0.16f}, {0.12f, -0.02f, -0.52f, -0.40f}, 1},   // pylons
    {4, {-0.16f, -0.40f, -0.40f, -0.16f}, {0.12f, -0.02f, -0.52f, -0.40f}, 1},
    {6, {0.36f, 0.42f, 0.50f, 0.50f, 0.36f, 0.34f}, {0.30f, 0.38f, 0.30f, -0.95f, -0.95f, -0.2f}, 2},  // nacelles
    {6, {-0.36f, -0.42f, -0.50f, -0.50f, -0.36f, -0.34f}, {0.30f, 0.38f, 0.30f, -0.95f, -0.95f, -0.2f}, 2},
    {11, {0.f, 0.08f, 0.15f, 0.19f, 0.24f, 0.22f, -0.22f, -0.24f, -0.19f, -0.15f, -0.08f},
     {1.f, 0.93f, 0.60f, 0.15f, -0.55f, -0.92f, -0.92f, -0.55f, 0.15f, 0.60f, 0.93f}, 0},  // the hull
    {6, {0.f, 0.07f, 0.09f, 0.f, -0.09f, -0.07f}, {0.48f, 0.40f, 0.10f, 0.02f, 0.10f, 0.40f}, 3},  // the bridge tower
    {4, {0.05f, 0.06f, -0.06f, -0.05f}, {0.44f, 0.36f, 0.36f, 0.44f}, 4},
};
const float kShipBEngines[][2] = {{0.f, -0.95f}, {0.43f, -0.98f}, {-0.43f, -0.98f}};

bool inPoly(const Shape& s, float u, float v) {
    bool in = false;
    for (int i = 0, j = s.n - 1; i < s.n; j = i++) {
        if ((s.v[i] > v) != (s.v[j] > v) && u < (s.u[j] - s.u[i]) * (v - s.v[i]) / (s.v[j] - s.v[i]) + s.u[i]) in = !in;
    }
    return in;
}

// a ship at art pixel (cx, cy), nose along (fx, fy) (unit, screen), half its length = len art pixels
void drawShip(Frame& F, bool heavy, float cx, float cy, float fx, float fy, float len, uint32_t engine, uint32_t t) {
    const Shape* shapes = heavy ? kShipBShapes : kShipAShapes;
    const int ns = heavy ? int(sizeof(kShipBShapes) / sizeof(kShipBShapes[0])) : int(sizeof(kShipAShapes) / sizeof(kShipAShapes[0]));
    const uint32_t* hullR = heavy ? kHullB : kHull;
    const float rx = -fy, ry = fx;  // "right" on the ship
    // the light in the ship's own frame (it is drawn seen from above)
    const float lu = kL.x * rx + kL.y * ry, lv = kL.x * fx + kL.y * fy;
    const float ext = len * 1.08f;
    const int x0 = std::max(0, int(cx - ext) - 1), x1 = std::min(F.w - 1, int(cx + ext) + 1);
    const int y0 = std::max(0, int(cy - ext) - 1), y1 = std::min(F.h - 1, int(cy + ext) + 1);
    if (x0 > x1 || y0 > y1) return;
    if (len < 2.2f) {  // far away: a speck of hull and its engine
        F.put(int(cx), int(cy), rgb(200, 204, 226));
        return;
    }
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    static std::vector<int8_t> part;
    static std::vector<uint32_t> col;
    part.assign(size_t(bw) * size_t(bh), -1);
    col.assign(size_t(bw) * size_t(bh), 0);
    const bool detail = len > 16.f;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const float dx = float(x) + 0.5f - cx, dy = float(y) + 0.5f - cy;
            const float u = (dx * rx + dy * ry) / len, v = (dx * fx + dy * fy) / len;
            int hit = -1;
            for (int s = 0; s < ns; ++s)
                if (inPoly(shapes[s], u, v)) hit = s;
            if (hit < 0) continue;
            const Shape& sh = shapes[hit];
            const float d = bayer(x, y);
            float val;
            uint32_t c;
            switch (sh.part) {
                case 0: {  // the hull: rounded across, lit from the sun's side
                    const float hw = heavy ? 0.22f : 0.15f;
                    const float nu = std::max(-1.f, std::min(1.f, u / hw));
                    val = 0.52f + 0.30f * nu * (lu > 0.f ? 1.f : -1.f) * std::min(1.f, std::fabs(lu) * 1.6f) + 0.12f * lv * v;
                    val = 0.30f + 0.70f * clamp01(val) * (1.f - 0.25f * nu * nu);
                    if (detail) {  // plates of slightly different shades, their seams
                        val *= 0.9f + 0.18f * hashf(uint32_t(int(std::floor(u * 14.f)) * 31 + int(std::floor(v * 9.f)) * 7 + 1000));
                        if (fract(v * 9.f + 0.5f) < 0.05f || (len > 40.f && fract(u * 14.f) < 0.05f)) val *= 0.72f;
                    }
                    c = ramp(hullR, 9, val, d);
                    if (detail && std::fabs(u) < 0.016f && v < 0.35f && v > -0.85f) c = heavy ? rgb(255, 170, 70) : rgb(255, 80, 210);  // the spine stripe
                    break;
                }
                case 1: {  // the wings: flat, the one on the sun's side brighter
                    val = 0.46f + 0.22f * (u > 0.f ? lu : -lu);
                    if (detail) {
                        val *= 0.9f + 0.16f * hashf(uint32_t(int(std::floor(u * 9.f)) * 17 + int(std::floor(v * 6.f)) * 5 + 2000));
                        if (fract(u * 9.f) < 0.07f) val *= 0.75f;
                    }
                    c = ramp(hullR, 9, val, d);
                    break;
                }
                case 2: {  // pods / nacelles
                    const float mid = (sh.u[0] + sh.u[1]) * 0.5f, hw = std::fabs(sh.u[1] - sh.u[0]) * 0.5f;
                    const float nu = (u - mid) / hw;
                    val = 0.45f + 0.25f * nu * (lu > 0.f ? 1.f : -1.f);
                    if (detail && fract(v * 5.f) < 0.08f) val *= 0.72f;
                    c = ramp(hullR, 9, val, d);
                    break;
                }
                case 3: c = ramp(hullR, 9, 0.18f, d); break;
                default: {  // the canopy: dark glass with a cyan glint
                    const float g = clamp01(0.5f + (u * lu + v * lv) * 6.f);
                    c = mix(rgb(20, 40, 70), rgb(140, 240, 255), int(g * 220.f));
                    break;
                }
            }
            // the neon line along the wings' leading edges (magenta on the courier, amber on the hauler)
            if (sh.part == 1 && detail) {
                const float e0u = sh.u[0], e0v = sh.v[0], e1u = sh.u[1], e1v = sh.v[1];
                const float lx = e1u - e0u, ly = e1v - e0v, ll = std::sqrt(lx * lx + ly * ly);
                const float dist = std::fabs((u - e0u) * ly - (v - e0v) * lx) / ll;
                if (dist * len < 1.4f) c = heavy ? rgb(255, 170, 70) : rgb(255, 80, 210);
            }
            const size_t bi = size_t(y - y0) * size_t(bw) + size_t(x - x0);
            part[bi] = int8_t(sh.part);
            col[bi] = c;
        }
    // onto the frame, with a dark outline where the ship meets space (crisp pixel art)
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const size_t bi = size_t(y) * size_t(bw) + size_t(x);
            if (part[bi] < 0) continue;
            const bool edge = x == 0 || y == 0 || x == bw - 1 || y == bh - 1 || part[bi - 1] < 0 || part[bi + 1] < 0 ||
                              part[bi - size_t(bw)] < 0 || part[bi + size_t(bw)] < 0;
            F.put(x0 + x, y0 + y, edge && len > 6.f ? mix(col[bi], rgb(8, 6, 18), 150) : col[bi]);
        }
    // the engines: hot cores and their bloom (flickering)
    const auto& eng = heavy ? kShipBEngines : kShipAEngines;
    const int ne = heavy ? 3 : 2;
    for (int e = 0; e < ne; ++e) {
        const float eu = eng[e][0], ev = eng[e][1];
        const float ex = cx + (eu * rx + ev * fx) * len, ey = cy + (eu * ry + ev * fy) * len;
        const float fl = 0.85f + 0.15f * std::sin(float(t) * 0.05f + float(e) * 2.1f);
        F.glow(ex, ey, std::max(2.f, len * 0.34f) * fl, engine, 200);
        F.glow(ex, ey, std::max(1.2f, len * 0.12f), rgb(255, 255, 255), 255);
    }
}

constexpr float kShipSize = 64.f;  // half a ship's length at distance 1 (game units)
// A flight: where it is on the screen (a curve from beside the camera, over the planet, then up and away) and how far away
// it is (its size): at q 0..1 of its time. x in game units from the screen's middle (the screen's width varies).
struct Flight {
    float x[4], y[4];
    float z0, z1;  // its distance at the start and the end (size = kShipSize / distance)
};
// the first: in past the left of the camera, out over the ice, then pulling up into space
const Flight kFlightA = {{-530.f, -230.f, -10.f, 40.f}, {575.f, 330.f, 262.f, -60.f}, 0.27f, 5.6f};
// the second: in past the right, across the other way, then up
const Flight kFlightB = {{540.f, 220.f, -60.f, -120.f}, {590.f, 360.f, 300.f, -60.f}, 0.28f, 6.2f};
constexpr uint32_t kDurA = 3400, kDurB = 3800;

void flightAt(const Flight& fl, float q, float W, float cam, float& x, float& y, float& z) {
    const float u = q * (2.f - q) * 0.72f + q * 0.28f;  // quick while it is near, slower as it goes away
    const float a = (1.f - u) * (1.f - u) * (1.f - u), b = 3.f * u * (1.f - u) * (1.f - u), c = 3.f * u * u * (1.f - u), e = u * u * u;
    x = W * 0.5f + fl.x[0] * a + fl.x[1] * b + fl.x[2] * c + fl.x[3] * e;
    y = fl.y[0] * a + fl.y[1] * b + fl.y[2] * c + fl.y[3] * e + cam;
    z = fl.z0 + (fl.z1 - fl.z0) * q * std::sqrt(q);
}

void drawHero(Frame& F, float W, uint32_t t, uint32_t start, uint32_t dur, const Flight& fl, bool heavy, uint32_t engine, float cam) {
    if (t < start || t > start + dur) return;
    const float q = float(t - start) / float(dur);
    float sx, sy, sz;
    flightAt(fl, q, W, cam, sx, sy, sz);
    // the trail: engine light along where it has been, thinning out behind
    const int nt = 80;
    for (int j = nt; j >= 1; --j) {
        const float qq = q - float(j) * 0.0042f;
        if (qq < 0.f) continue;
        float bx, by, bz;
        flightAt(fl, qq, W, cam, bx, by, bz);
        const float size = kShipSize / bz * F.k;
        const float fade = 1.f - float(j) / float(nt);
        const float rad = std::min(7.f * F.k, std::max(1.3f, size * 0.08f));
        F.glow(bx * F.k, by * F.k, rad, engine, int(90.f * fade * fade));
    }
    // which way it points on the screen: along its path
    float tx, ty, tz;
    flightAt(fl, std::min(1.f, q + 0.01f), W, cam, tx, ty, tz);
    float fx = tx - sx, fy = ty - sy;
    const float flen = std::sqrt(fx * fx + fy * fy);
    if (flen < 1e-4f) {
        fx = 0.f;
        fy = -1.f;
    } else {
        fx /= flen;
        fy /= flen;
    }
    drawShip(F, heavy, sx * F.k, sy * F.k, fx, fy, kShipSize / sz * F.k, engine, t);
}

// ---------------------------------------------------------------- the traffic between the planet, the war sun and space
struct Lane {
    float x0, y0, cx, cy, x1, y1;  // a quadratic curve (world, game units)
    float s0, s1;                  // the ships' half-length at either end (game units)
};
void bez2(const Lane& l, float u, float& x, float& y) {
    const float a = (1.f - u) * (1.f - u), b = 2.f * u * (1.f - u), c = u * u;
    x = l.x0 * a + l.cx * b + l.x1 * c;
    y = l.y0 * a + l.cy * b + l.y1 * c;
}

void drawTraffic(Frame& F, float W, uint32_t t, float cam) {
    float dlx, dly, drx, dry;
    docks(W, dlx, dly, drx, dry);
    const float sx = W * 0.5f + kSunDX, sy = kSunWY;
    const float ringR = kRingA * kSunR;
    // the ring's ends (where the docking arms are, roughly) and the belly under the beam
    const float rlx = sx - ringR * 0.99f, rly = sy - ringR * std::sin(kRingTurn) * 0.99f;
    const float rrx = sx + ringR * 0.99f, rry = sy + ringR * std::sin(kRingTurn) * 0.99f;
    const Lane lanes[] = {
        {dlx, dly, dlx - 30.f, dly - 220.f, rlx + 30.f, rly + 26.f, 0.6f, 2.0f},            // 0 left spaceport up to the ring
        {sx + kBeamX + 40.f, sy + kSunR * 0.9f, drx - 10.f, dry - 160.f, drx, dry, 2.0f, 0.6f},  // 1 the belly down to the right port
        {drx, dry, drx + 60.f, dry - 200.f, W + 40.f, kSkyTop - 40.f, 0.6f, 3.6f},             // 2 right port out into space
        {-40.f, sy + 80.f, sx - 240.f, sy + 110.f, rlx + 10.f, rly + 8.f, 3.0f, 2.0f},          // 3 in from the left to the ring
        {rrx - 10.f, rry + 6.f, sx + 260.f, sy - 40.f, W + 40.f, sy - 150.f, 2.0f, 3.2f},      // 4 from the ring away to the right
        {dlx, dly, dlx + 40.f, dly - 140.f, -40.f, dly - 300.f, 0.6f, 3.4f},                   // 5 left port out to the left
        {sx + 60.f, sy + kSunR * 0.95f, sx + 40.f, dly - 100.f, dlx + 30.f, dly, 2.0f, 0.6f},  // 6 the belly down to the left port
        {W * 0.5f, kHorizon + 30.f, W * 0.48f, kHorizon - 120.f, sx - 20.f, sy + kSunR * 0.98f, 0.6f, 2.0f},  // 7 up the beam
        {-60.f, kHorizon + 40.f, W * 0.42f, kHorizon - 170.f, W + 60.f, sy + 60.f, 4.2f, 2.6f},  // 8 near: across, low left to high right
        {W + 60.f, kHorizon - 10.f, W * 0.62f, kHorizon - 240.f, -60.f, sy + 140.f, 3.6f, 2.4f},  // 9 near: across the other way
    };
    struct Ship {
        int lane;
        float period, phase;
        uint32_t engine;
        uint32_t from;  // when it joins the scene (the traffic grows as the camera looks up)
        bool back = false;  // going the other way along its lane
    };
    static const Ship kShips[] = {
        {0, 9.5f, 0.10f, rgb(255, 90, 210), 0},       {0, 11.f, 0.55f, rgb(110, 230, 255), 2500},
        {1, 10.f, 0.30f, rgb(255, 160, 70), 4300},    {2, 8.5f, 0.20f, rgb(110, 230, 255), 0},
        {2, 12.f, 0.70f, rgb(255, 90, 210), 1500},    {3, 13.f, 0.15f, rgb(255, 160, 70), 5600},
        {3, 9.f, 0.65f, rgb(110, 230, 255), 7600},    {4, 12.5f, 0.40f, rgb(255, 90, 210), 6400},
        {4, 10.5f, 0.85f, rgb(110, 230, 255), 9000},  {5, 9.f, 0.45f, rgb(255, 160, 70), 0},
        {6, 11.5f, 0.05f, rgb(110, 230, 255), 6800},  {7, 14.f, 0.25f, rgb(255, 230, 160), 3600},
        {7, 14.f, 0.75f, rgb(255, 230, 160), 5200},   {0, 10.f, 0.85f, rgb(255, 160, 70), 8200, true},
        {1, 12.f, 0.80f, rgb(255, 90, 210), 9800, true}, {3, 11.f, 0.40f, rgb(255, 90, 210), 10400},
        {8, 7.5f, 0.0f, rgb(255, 160, 70), 5000},     {9, 8.5f, 0.0f, rgb(110, 230, 255), 6900},
        {8, 9.f, 0.0f, rgb(255, 90, 210), 9300},      {2, 9.f, 0.45f, rgb(255, 160, 70), 6000},
        {5, 10.f, 0.9f, rgb(110, 230, 255), 4800},    {6, 9.5f, 0.6f, rgb(255, 90, 210), 8800},
    };
    const float tt = float(t) * 0.001f;
    for (const Ship& s : kShips) {
        if (t < s.from) continue;
        const Lane& L = lanes[s.lane];
        float u = std::fmod(tt / s.period + s.phase, 1.f);
        if (s.back) u = 1.f - u;
        // (a ship that joins starts at the beginning of its lane, not out of nowhere in the middle: until it has flown one whole
        // lap it is shown only when this lap began after it joined - how far along the lap it is <= the laps since it joined)
        const float since = float(t - s.from) * 0.001f / s.period;
        if (s.from > 0 && since < 1.f && (s.back ? 1.f - u : u) > since) continue;
        float x, y;
        bez2(L, u, x, y);
        y += cam;
        // (at the lanes' ends - a dock, the edge of the screen - a ship grows out of / shrinks into a speck: nothing pops)
        const float ends = smoothstep(0.f, 0.06f, u) * smoothstep(1.f, 0.94f, u);
        const float size = (L.s0 + (L.s1 - L.s0) * u) * 1.7f * F.k * ends;
        // its trail
        for (int j = 1; j <= 40; ++j) {
            const float uu = u - float(j) * 0.0025f * (s.back ? -1.f : 1.f);
            if (uu < 0.f || uu > 1.f) break;
            float tx, ty;
            bez2(L, uu, tx, ty);
            const int a = int(float(170 - j * 4) * clamp01(size / 2.5f + 0.3f) * ends);
            F.add(int(tx * F.k), int((ty + cam) * F.k), s.engine, a);
        }
        float nx, ny;
        bez2(L, std::max(0.f, std::min(1.f, u + (s.back ? -0.01f : 0.01f))), nx, ny);
        float fx = nx - x, fy = ny + cam - y;
        const float fl = std::sqrt(fx * fx + fy * fy);
        if (fl > 1e-4f) {
            fx /= fl;
            fy /= fl;
        } else {
            fx = 0.f;
            fy = -1.f;
        }
        if (size < 2.4f) {  // a speck: its engine light
            F.add(int(x * F.k), int(y * F.k), s.engine, int(230.f * ends));
            F.add(int(x * F.k), int(y * F.k), rgb(255, 255, 255), int(size * 80.f));
            continue;
        }
        drawShip(F, (hash32(uint32_t(s.lane * 31) + uint32_t(s.period * 10.f)) & 1u) != 0, x * F.k, y * F.k, fx, fy, size, s.engine, t);
    }
}

// ---------------------------------------------------------------- the war sun alive: its star, the welders, the lights
void drawSunLife(Frame& F, const Art& A, float scx, float scy, uint32_t t) {
    const float tt = float(t) * 0.001f;
    const float R = kSunR * F.k;
    // the captive star breathing
    if (A.eyeSeen) {
        const float pulse = 0.75f + 0.25f * std::sin(tt * 2.1f) + 0.08f * std::sin(tt * 7.3f);
        F.glow(scx + A.eyeX, scy + A.eyeY, R * 0.24f * pulse, rgb(255, 140, 60), int(150.f * pulse));
        F.glow(scx + A.eyeX, scy + A.eyeY, R * 0.09f, rgb(255, 240, 200), int(220.f * pulse));
    }
    // the welders: sparks flaring along the edge of the plating, a few at a time
    if (!A.seams.empty()) {
        const uint32_t slot = t / 90u;
        for (int j = 0; j < 7; ++j) {
            const uint32_t h = hash32(slot * 7u + uint32_t(j) * 104729u + 5u);
            const Spot& sp = A.seams[h % A.seams.size()];
            const float age = float(t % 90u) / 90.f;
            const float x = scx + float(sp.x), y = scy + float(sp.y);
            const int a = int(255.f * (1.f - age));
            F.add(int(x), int(y), rgb(255, 255, 255), a);
            F.glow(x, y, 3.5f * F.k, (h >> 8) % 2u ? rgb(140, 230, 255) : rgb(255, 210, 140), a / 2);
            for (int k = 0; k < 3; ++k) {  // a few sparks falling away
                const float vx = (hashf(h + uint32_t(k) * 11u) - 0.5f) * 10.f, vy = 3.f + 7.f * hashf(h + uint32_t(k) * 17u);
                F.add(int(x + vx * age), int(y + vy * age * age * 1.6f), rgb(255, 200, 120), int(200.f * (1.f - age)));
            }
        }
    }
    // the builders' drones going back and forth over the unfinished part
    for (int j = 0; j < 7; ++j) {
        const float ph = tt * (0.25f + 0.07f * float(j)) + float(j) * 1.7f;
        const float ang = -0.95f + 0.75f * std::sin(ph) * 0.5f + 0.12f * float(j % 3);
        const float rr = R * (1.03f + 0.06f * std::sin(ph * 1.7f + float(j)));
        const float x = scx + std::cos(ang - kPi * 0.5f + 0.9f) * rr, y = scy + std::sin(ang - kPi * 0.5f + 0.9f) * rr;
        F.put(int(x), int(y), rgb(200, 206, 226));
        if (std::fmod(tt * 2.f + float(j) * 0.37f, 1.f) < 0.35f) F.add(int(x), int(y) - 1, rgb(255, 90, 90), 230);
    }
    // beacons on the scaffolds and spires
    for (size_t j = 0; j < A.lamps.size(); ++j) {
        const float ph = std::fmod(tt * 0.8f + float(j) * 0.29f, 1.f);
        if (ph < 0.18f) {
            const uint32_t c = j % 3 == 0 ? rgb(255, 255, 255) : j % 3 == 1 ? rgb(255, 70, 70) : rgb(255, 200, 80);
            const float x = scx + float(A.lamps[j].x), y = scy + float(A.lamps[j].y);
            F.add(int(x), int(y), c, 255);
            F.glow(x, y, 2.5f * F.k, c, 120);
        }
    }
    // lights running round the ring's outer wall (hidden where the war sun is in front of them)
    {
        const V3 N = ringNormal();
        const V3 ru = norm(cross(N, {0.f, 0.f, 1.f}));
        const V3 rv = cross(N, ru);
        const float rr = kRingA * R * 0.975f;
        for (int j = 0; j < 48; ++j) {
            const float th = float(j) / 48.f * 2.f * kPi + tt * 0.05f;
            const V3 Q{(std::cos(th) * ru.x + std::sin(th) * rv.x) * rr, (std::cos(th) * ru.y + std::sin(th) * rv.y) * rr,
                       (std::cos(th) * ru.z + std::sin(th) * rv.z) * rr};
            const float r2 = Q.x * Q.x + Q.y * Q.y;
            if (r2 < R * R && Q.z < std::sqrt(R * R - r2)) continue;  // behind the sphere
            const float blink = std::fmod(tt * 1.4f - float(j) * 0.0625f, 1.f);
            if (blink < 0.12f) F.add(int(scx + Q.x), int(scy + Q.y), j % 2 ? rgb(120, 236, 255) : rgb(255, 120, 220), 255);
        }
    }
}

// the supply beam from the planet up into the war sun's belly, light running up it
void drawBeam(Frame& F, float W, uint32_t t, float cam) {
    const float x = W * 0.5f + kSunDX + kBeamX;
    const float yTop = kSunWY + kSunR * 0.96f + cam, yBot = kHorizon + 5.f + cam;
    const int X = int(x * F.k);
    const float tt = float(t) * 0.001f;
    for (int y = std::max(0, int(yTop * F.k)); y < std::min(F.h, int(yBot * F.k)); ++y) {
        const float gy = float(y) / F.k;
        const float run = std::fmod(gy * 0.08f + tt * 3.f, 6.f);  // packets of light going up
        const int a = 70 + (run < 0.5f ? 120 : 0);
        F.add(X, y, rgb(150, 236, 255), a);
        F.add(X - 1, y, rgb(60, 140, 220), a / 3);
        F.add(X + 1, y, rgb(60, 140, 220), a / 3);
        if (F.k > 1.4f) {
            F.add(X - 2, y, rgb(40, 90, 180), a / 6);
            F.add(X + 2, y, rgb(40, 90, 180), a / 6);
        }
    }
}

float panOf(uint32_t t) {
    if (t <= kPanStart) return 0.f;
    if (t >= kPanEnd) return 1.f;
    const float u = float(t - kPanStart) / float(kPanEnd - kPanStart);
    return u * u * u * (u * (u * 6.f - 15.f) + 10.f);  // slow to start, slow to stop
}

// ---------------------------------------------------------------- the words
void drawWords(Canvas& c, uint32_t t) {
    const int W = c.fullWidth();
    if (t < kTitleAt) return;
    // "TECHNOLOGY DISCOVERED" - spaced capitals in cyan, rules either side, a scan of light across as it comes
    {
        const std::string s = "TECHNOLOGY DISCOVERED";
        const float in = clamp01(float(t - kTitleAt) / 600.f);
        const int a = int(255.f * in);
        const int sp = 4;
        int w = 0;
        for (char ch : s) w += textWidth(FONT_MDB, std::string(1, ch)) + sp;
        w -= sp;
        const int x0 = (W - w) / 2, y = 42;
        int x = x0;
        for (size_t i = 0; i < s.size(); ++i) {
            const std::string ch(1, s[i]);
            for (int k = 0; k < 4; ++k)
                drawText(c, FONT_MDB, x + (k == 0 ? -1 : k == 1 ? 1 : 0), y + (k == 2 ? -1 : k == 3 ? 1 : 0), ch, rgb(40, 140, 255), 60 * a / 255);
            drawText(c, FONT_MDB, x, y, ch, rgb(150, 236, 255), a);
            x += textWidth(FONT_MDB, ch) + sp;
        }
        const int half = int(float(W / 2 - 40) * smooth01(in));
        const int ry = y + FONT_MDB.lineHeight / 2;
        if (half > w / 2 + 14) {
            c.fillRect(W / 2 - half, ry, half - w / 2 - 12, 1, rgb(120, 220, 255), a * 3 / 4);
            c.fillRect(W / 2 + w / 2 + 12, ry, half - w / 2 - 12, 1, rgb(120, 220, 255), a * 3 / 4);
        }
        if (in < 1.f) {  // the scan: a line of light running across as it comes
            const int sx = x0 + int(float(w + 40) * in) - 20;
            c.fillRect(sx, y - 3, 2, FONT_MDB.lineHeight + 6, rgb(200, 250, 255), int(220.f * (1.f - in)));
        }
    }
    // "The Age of the War Sun Approaches." - gold with a magenta neon glow, letter by letter as on the title
    if (t >= kLineAt) {
        const std::string s = "The Age of the War Sun Approaches.";
        const FontData& f = textWidth(FONT_XL, s) <= W - 40 ? FONT_XL : FONT_LG;
        const int x0 = (W - textWidth(f, s)) / 2, y = 64;
        const uint32_t step = 38;
        for (size_t i = 0; i < s.size(); ++i) {
            const uint32_t at = kLineAt + uint32_t(i) * step;
            if (t < at) break;
            const std::string ch(1, s[i]);
            if (ch == " ") continue;
            const int lx = x0 + textWidth(f, s.substr(0, i));
            const float come = clamp01(float(t - at) / 260.f);
            const int a = int(255.f * come);
            for (int k = 0; k < 4; ++k)
                drawText(c, f, lx + (k == 0 ? -1 : k == 1 ? 1 : 0), y + (k == 2 ? -1 : k == 3 ? 1 : 0), ch, rgb(255, 60, 200), 60 * a / 255);
            const uint32_t gold = mix(rgb(255, 255, 255), rgb(255, 214, 120), int(come * 255.f));
            drawText(c, f, lx, y, ch, gold, a);
        }
    }
}

}  // namespace

void prepare(const Canvas& c) {
    float k;
    int W, aw, ah, blow;
    screenArt(c, k, W, aw, ah, blow);
    artFor(k, W, aw, ah);
}

bool ready(const Canvas& c) {
    float k;
    int W, aw, ah, blow;
    screenArt(c, k, W, aw, ah, blow);
    return artFor(k, W, aw, ah) != nullptr;
}

void finish() {
    if (g_maker.t.joinable()) g_maker.t.join();
    if (g_maker.done) {
        g_maker.done = false;
        if (g_maker.next) g_art = std::move(g_maker.next);
    }
}

int barHeight(uint32_t t) { return int(26.f * smooth01(float(t) / 500.f)); }

void drawCaption(Canvas& c, uint32_t t, int faction, const std::string& who, uint32_t colour) {
    if (t < kTitleAt || who.empty()) return;
    const int a = int(255.f * clamp01(float(t - kTitleAt) / 500.f));
    const int y = 480 - barHeight(t) + 5;
    int x = 14;
    if (faction >= 0) {
        drawFactionIcon(c, faction, x, y - 1, 18, a);
        x += 22;
    }
    drawText(c, FONT_SM, x, y, who, colour, a);
}

void draw(Canvas& c, uint32_t t) {
    float k;
    int W, aw, ah, blow;
    screenArt(c, k, W, aw, ah, blow);
    const Art* A = artFor(k, W, aw, ah);
    Frame& F = g_frame;
    F.w = aw;
    F.h = ah;
    F.k = k;
    F.px.assign(size_t(aw) * size_t(ah), 0);
    if (A) {
        const float cam = kPan * panOf(t);
        // the sky (it moves less than the rest)
        {
            const int off = int(std::lround((kSkyPar * (kPan - cam)) * k));
            for (int y = 0; y < ah; ++y) {
                const int sy = y + off;
                if (sy < 0 || sy >= A->sky.h) continue;
                std::memcpy(&F.px[size_t(y) * size_t(aw)], &A->sky.px[size_t(sy) * size_t(A->sky.w)], size_t(std::min(aw, A->sky.w)) * 4u);
            }
        }
        drawStars(F, float(W), t, cam);
        drawBeam(F, float(W), t, cam);
        // the war sun
        const float scx = (float(W) * 0.5f + kSunDX) * k, scy = (kSunWY + cam) * k;
        if (scy + float(A->sun.h - A->sunCy) > 0.f) {
            blitImg(F, A->sun, int(std::lround(scx)) - A->sunCx, int(std::lround(scy)) - A->sunCy);
            drawSunLife(F, *A, std::round(scx), std::round(scy), t);
        }
        // the ice planet and its aurora
        blitImg(F, A->planet, 0, int(std::lround((kHorizon - kHalo + cam) * k)));
        drawAurora(F, float(W), t, cam);
        drawTraffic(F, float(W), t, cam);
        // the two ships
        drawHero(F, float(W), t, kShipA, kDurA, kFlightA, false, rgb(255, 90, 210), cam);
        drawHero(F, float(W), t, kShipB, kDurB, kFlightB, true, rgb(110, 230, 255), cam);
        // the planet's light from below once it has gone out of sight
        {
            const float hy = kHorizon + cam;  // the horizon (below the screen at the end)
            for (int y = std::max(0, ah - int(80.f * k)); y < ah; ++y) {
                const float gy = (float(y) + 0.5f) / k;
                const float x = (hy - gy) / 60.f;  // (no expf: the Miyoo build keeps to old maths symbols)
                const float a = 80.f / (1.f + x + 0.5f * x * x) * smoothstep(0.f, 0.3f, cam / kPan);
                if (a < 2.f || gy > hy) continue;
                for (int x = 0; x < aw; ++x)
                    if (a > bayer(x, y) * 20.f) F.add(x, y, rgb(70, 170, 210), int(a));
            }
        }
        // fade in from black
        if (t < 600) {
            const int a = int(255.f * (1.f - float(t) / 600.f));
            for (uint32_t& p : F.px) p = mix(p, 0u, a);
        }
    }
    // onto the screen (each art pixel blow x blow real pixels)
    const int pw = c.pw(), ph = c.ph();
    uint32_t* dst = c.pixels();
    for (int Y = 0; Y < ph; ++Y) {
        const uint32_t* src = &F.px[size_t(std::min(ah - 1, Y / blow)) * size_t(aw)];
        uint32_t* d = dst + size_t(Y) * size_t(pw);
        if (blow == 1) std::memcpy(d, src, size_t(std::min(pw, aw)) * 4u);
        else
            for (int X = 0; X < pw; ++X) d[X] = src[std::min(aw - 1, X >> 1)];
    }
    if (!A) return;
    // the letterbox bars
    const int bh = barHeight(t);
    if (bh > 0) {
        c.fillRect(0, 0, W, bh, rgb(0, 0, 0));
        c.fillRect(0, 480 - bh, W, bh, rgb(0, 0, 0));
    }
    drawWords(c, t);
}

}  // namespace warsun
