#include "ui/title_bg.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "gfx/canvas.h"

namespace titlebg {
namespace {

constexpr float kPi = 3.14159265f;
// The planet sits where the Simplistic title's does (widgets.cpp paintSpace: centre (560 + dx, 600), radius 300, dx = width - 640).
constexpr float kCx = 560.f, kCy = 600.f, kR = 300.f;
constexpr float kPanDrop = 440.f;  // how far below its place the planet starts the pan (game units)
constexpr float kHalo = 16.f;      // the atmosphere's glow beyond the disc (game units)
// The part of it that can ever be on the screen (planet-local, game units): from the halo's top-left corner to the bottom of a
// 480-high screen (120 above the centre) and to the screen's right edge (80 right of the centre, on any width).
constexpr float kLx0 = -kR - kHalo, kLx1 = 80.f, kLy0 = -kR - kHalo, kLy1 = 480.f - kCy;
// the heart of the city (planet-local, game units): on the night side, near the dusk line, under the title menu's lower part
constexpr float kCityLx = -140.f, kCityLy = -194.f;

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

// 3D value noise, 0..1
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

// 4x4 ordered dithering
inline float bayer(int x, int y) {
    static const uint8_t m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (float(m[(y & 3) * 4 + (x & 3)]) + 0.5f) / 16.f;
}
// a dithered pick from a ramp (v 0..1)
inline uint32_t ramp(const uint32_t* r, int n, float v, float d) {
    int i = int(std::floor(std::max(0.f, std::min(1.f, v)) * float(n - 1) + d));
    return r[std::max(0, std::min(n - 1, i))];
}

// the palettes (Retrowave: dusky, with neon in the lights)
const uint32_t kGround[] = {rgb(22, 17, 22), rgb(36, 27, 30), rgb(54, 39, 38), rgb(76, 54, 46), rgb(100, 72, 56), rgb(126, 94, 70), rgb(150, 118, 90)};
const uint32_t kOcean[] = {rgb(7, 15, 26), rgb(11, 28, 40), rgb(15, 44, 56), rgb(21, 63, 72), rgb(29, 85, 90), rgb(42, 108, 106)};
const uint32_t kAsh[] = {rgb(20, 18, 26), rgb(33, 30, 41), rgb(50, 46, 58), rgb(71, 66, 79), rgb(96, 90, 104), rgb(124, 118, 132), rgb(156, 150, 162)};
const uint32_t kNight[] = {rgb(5, 5, 12), rgb(8, 8, 17), rgb(12, 11, 23), rgb(17, 14, 29)};
const uint32_t kCloud[] = {rgb(16, 32, 40), rgb(24, 50, 56), rgb(36, 74, 76), rgb(54, 104, 100), rgb(82, 138, 126), rgb(120, 172, 154),
                           rgb(166, 206, 186), rgb(212, 234, 214)};
const uint32_t kCloudNight[] = {rgb(10, 11, 22), rgb(15, 16, 30), rgb(21, 21, 38), rgb(28, 26, 46)};
const uint32_t kCity[] = {rgb(84, 26, 12), rgb(146, 52, 14), rgb(206, 96, 22), rgb(242, 150, 40), rgb(255, 202, 92), rgb(255, 242, 186)};
const uint32_t kBurn[] = {rgb(58, 8, 12), rgb(108, 16, 16), rgb(168, 38, 20), rgb(228, 80, 30), rgb(255, 140, 64)};

// the planet's axis is tilted: the cloud bands and the poles sit at an angle to the screen
void toPlanet(float nx, float ny, float nz, float& px, float& py, float& pz) {
    const float cz = 0.951f, sz = 0.309f;  // 18 degrees about the view axis
    const float x1 = nx * cz - ny * sz, y1 = nx * sz + ny * cz, z1 = nz;
    const float cx = 0.927f, sx = 0.375f;  // 22 degrees toward the viewer
    px = x1;
    py = y1 * cx - z1 * sx;
    pz = y1 * sx + z1 * cx;
}
// the sun: off to the right, a little above and in front - the left part of the disc (toward the menu) lies in the night
void lightDir(float& lx, float& ly, float& lz) {
    lx = 0.92f;
    ly = -0.24f;
    lz = 0.30f;
    const float n = std::sqrt(lx * lx + ly * ly + lz * lz);
    lx /= n;
    ly /= n;
    lz /= n;
}

struct Px {
    uint32_t base = 0;        // the surface as it lies (lit and dithered); clouds and lights go over it
    uint16_t cu = 0, cv = 0;  // where it is on the cloud map
    uint16_t road = 0;        // distance along the city's roads (the light pulses), 0 = not a road
    uint8_t day = 0;          // 0 night .. 255 full day
    uint8_t city = 0;         // the city's light here (at full night)
    uint8_t burn = 0;         // a blast crater still glowing
    uint8_t rim = 0;          // the atmosphere: on the disc its edge, beyond it the glow
    uint8_t inside = 0;       // 1 on the disc, 2 in the halo
    uint8_t tw = 0;           // the twinkle's phase
};

struct Planet {
    float k = 1.f;       // art pixels per game unit
    int w = 0, h = 0;    // the region in art pixels
    int ox = 0, oy = 0;  // its top-left, planet-local art pixels
    std::vector<Px> px;
    int cw = 0, ch = 0;  // the cloud map (longitude x latitude)
    std::vector<uint8_t> cloud;
};

// ---------------------------------------------------------------- the city: a web of lights in its own flat map
struct CityMap {
    static constexpr int N = 512;
    std::vector<uint8_t> lum = std::vector<uint8_t>(N * N, 0);
    std::vector<uint16_t> dist = std::vector<uint16_t>(N * N, 0);
    void plot(int x, int y, int v, int d) {
        if (x < 0 || y < 0 || x >= N || y >= N || v <= 0) return;
        const size_t i = size_t(y) * N + size_t(x);
        lum[i] = uint8_t(std::max<int>(lum[i], std::min(255, v)));
        if (d > 0 && (dist[i] == 0 || d < dist[i])) dist[i] = uint16_t(d);
    }
};
CityMap makeCity() {
    CityMap c;
    const int N = CityMap::N, cx = N / 2, cy = N / 2;
    uint32_t seed = 0x51715u;
    auto rnd = [&]() {
        seed = hash32(seed + 0x9e3779b9u);
        return float(seed & 0xffffffu) / 16777215.f;
    };
    // the heart: a dense knot of lights
    for (int i = 0; i < 4200; ++i) {
        const float a = rnd() * 2.f * kPi, r = 26.f * rnd() * rnd() + 2.f * rnd();
        c.plot(cx + int(std::cos(a) * r), cy + int(std::sin(a) * r * 0.85f), int(255.f - r * 4.f), 1);
    }
    // the arteries: roads wandering out of the heart, some stretches bombed dark; branches off them
    struct Road {
        float x, y, hd, len, v;
        int d0;
    };
    std::vector<Road> roads;
    const int nArt = 17;
    for (int a = 0; a < nArt; ++a)
        roads.push_back({float(cx), float(cy), float(a) * 2.f * kPi / float(nArt) + (rnd() - 0.5f) * 0.3f, 110.f + rnd() * 140.f, 235.f, 2});
    for (size_t r = 0; r < roads.size() && r < 160; ++r) {
        Road rd = roads[r];
        const bool branch = r >= size_t(nArt);
        for (int s = 0; s < int(rd.len); ++s) {
            rd.hd += (rnd() - 0.5f) * 0.16f;
            rd.x += std::cos(rd.hd);
            rd.y += std::sin(rd.hd) * 0.86f;
            const int d = rd.d0 + s;
            const bool dark = hashf(uint32_t(r) * 977u + uint32_t(s / 14) * 131u) < 0.16f;  // a stretch the bombs put out
            const float v = std::max(70.f, rd.v - float(s) * (branch ? 1.4f : 0.55f));
            if (!dark) {
                c.plot(int(rd.x), int(rd.y), int(v), d);
                if (!branch && s < 60) c.plot(int(rd.x) + 1, int(rd.y), int(v * 0.7f), d);  // the big avenues near the heart are wider
                if (hashf(uint32_t(r) * 31u + uint32_t(s) * 7u) < 0.18f)                      // buildings along it
                    c.plot(int(rd.x) + int((rnd() - 0.5f) * 6.f), int(rd.y) + int((rnd() - 0.5f) * 6.f), int(v * 0.6f), 0);
            }
            if (roads.size() < 160 && rnd() < (branch ? 0.012f : 0.035f)) {
                const float turn = (rnd() < 0.5f ? -1.f : 1.f) * (0.6f + rnd() * 0.7f);
                roads.push_back({rd.x, rd.y, rd.hd + turn, 18.f + rnd() * (branch ? 30.f : 70.f), v * 0.85f, d});
            }
            if (!branch && rnd() < 0.006f) {  // a town on the road: a small knot of its own
                const float tr = 3.f + rnd() * 5.f;
                for (int i = 0; i < int(tr * tr * 3.f); ++i) {
                    const float a = rnd() * 2.f * kPi, rr = tr * rnd();
                    c.plot(int(rd.x + std::cos(a) * rr), int(rd.y + std::sin(a) * rr), int(v * (1.f - rr / (tr * 1.4f))), 0);
                }
            }
        }
    }
    // ring roads round the heart, broken in places
    const float rings[3] = {38.f, 76.f, 124.f};
    for (int k = 0; k < 3; ++k) {
        const float ph = rnd() * 6.f;
        for (int i = 0; i < 1400; ++i) {
            const float a = float(i) / 1400.f * 2.f * kPi;
            if (hashf(uint32_t(k) * 7919u + uint32_t(a * 9.f) * 13u) < 0.22f) continue;
            const float r = rings[k] * (1.f + 0.13f * std::sin(a * 3.f + ph) + 0.05f * std::sin(a * 11.f + ph * 2.f));
            c.plot(cx + int(std::cos(a) * r), cy + int(std::sin(a) * r * 0.86f), 175 - k * 30, int(r) + 1);
        }
    }
    // the outskirts: scattered dim lights thinning out
    for (int i = 0; i < 2600; ++i) {
        const float a = rnd() * 2.f * kPi, r = 200.f * std::sqrt(rnd());
        c.plot(cx + int(std::cos(a) * r), cy + int(std::sin(a) * r * 0.86f), int(110.f - r * 0.4f), 0);
    }
    return c;
}

// ---------------------------------------------------------------- the planet, made once (in the background)
std::unique_ptr<Planet> makePlanet(float k, const std::atomic<bool>& cancel) {
    std::unique_ptr<Planet> P(new Planet);
    Planet& pl = *P;
    pl.k = k;
    pl.ox = int(std::floor(kLx0 * k));
    pl.oy = int(std::floor(kLy0 * k));
    pl.w = int(std::ceil(kLx1 * k)) - pl.ox;
    pl.h = int(std::ceil(kLy1 * k)) - pl.oy;
    pl.px.assign(size_t(pl.w) * size_t(pl.h), Px());
    const float R = kR * k;
    float lx, ly, lz;
    lightDir(lx, ly, lz);

    // the cloud map: toxic swirls in bands
    pl.cw = int(512.f * k);
    pl.ch = pl.cw / 2;
    pl.cloud.assign(size_t(pl.cw) * size_t(pl.ch), 0);
    for (int j = 0; j < pl.ch; ++j) {
        if (cancel) return nullptr;
        const float lat = (float(j) + 0.5f) / float(pl.ch) * kPi - kPi / 2.f;
        const float cl = std::cos(lat), sl = std::sin(lat);
        for (int i = 0; i < pl.cw; ++i) {
            const float lon = (float(i) + 0.5f) / float(pl.cw) * 2.f * kPi;
            const float qx = cl * std::cos(lon), qy = sl, qz = cl * std::sin(lon);
            // swirled: the sample point pushed around by a slower noise
            const float wx = fbm(qx * 1.4f + 5.f, qy * 1.4f, qz * 1.4f, 3) - 0.5f, wy = fbm(qx * 1.4f, qy * 1.4f + 9.f, qz * 1.4f, 3) - 0.5f;
            const float sx = fbm(qx * 3.f + wx * 3.f, qy * 3.f + 2.f, qz * 3.f, 3) - 0.5f;  // a second swirl
            float v = fbm(qx * 2.6f + wx * 3.0f + sx, qy * 6.0f + wy * 3.0f, qz * 2.6f + wx * 1.5f + sx, 5);
            v += 0.06f * std::sin(lat * 7.f + wx * 4.f);  // bands
            pl.cloud[size_t(j) * size_t(pl.cw) + size_t(i)] = uint8_t(255.f * smoothstep(0.45f, 0.70f, v));
        }
    }

    // nuclear craters: blast scars (glassed floors, bright rims, rays of thrown-out ash), some still glowing
    struct Crater {
        float x, y, z, rad;
        float ax, ay, az, bx, by, bz;  // its own flat frame (for the rays)
        bool hot;
    };
    std::vector<Crater> craters;
    for (int i = 0; i < 110; ++i) {
        const float u = hashf(uint32_t(i) * 2654435761u + 7u) * 2.f - 1.f, a = hashf(uint32_t(i) * 40503u + 11u) * 2.f * kPi;
        const float s = std::sqrt(std::max(0.f, 1.f - u * u));
        const float sz = hashf(uint32_t(i) * 977u + 3u);
        Crater c{s * std::cos(a), u, s * std::sin(a), 0.010f + 0.055f * sz * sz, 0, 0, 0, 0, 0, 0, i % 3 == 0};
        float ax = c.z, az = -c.x;
        const float n = std::sqrt(ax * ax + az * az) + 1e-6f;
        c.ax = ax / n;
        c.ay = 0.f;
        c.az = az / n;
        c.bx = c.y * c.az - c.z * c.ay;
        c.by = c.z * c.ax - c.x * c.az;
        c.bz = c.x * c.ay - c.y * c.ax;
        craters.push_back(c);
    }
    // and a few big ones placed where they are seen (planet-local game units: right of the menu on the day side, by the city)
    static const struct {
        float lx, ly, rad;
        bool hot;
    } kSeen[] = {{30.f, -205.f, 0.075f, false}, {62.f, -150.f, 0.05f, false}, {-22.f, -248.f, 0.042f, false}, {8.f, -138.f, 0.032f, true},
                 {-60.f, -150.f, 0.05f, true},  {-205.f, -150.f, 0.04f, true}, {-120.f, -262.f, 0.035f, true}, {70.f, -268.f, 0.03f, false}};
    for (const auto& sc : kSeen) {
        const float nx = sc.lx / kR, ny = sc.ly / kR, nz = std::sqrt(std::max(0.f, 1.f - nx * nx - ny * ny));
        Crater c{0, 0, 0, sc.rad, 0, 0, 0, 0, 0, 0, sc.hot};
        toPlanet(nx, ny, nz, c.x, c.y, c.z);
        const float n = std::sqrt(c.z * c.z + c.x * c.x) + 1e-6f;
        c.ax = c.z / n;
        c.az = -c.x / n;
        c.bx = c.y * c.az - c.z * c.ay;
        c.by = c.z * c.ax - c.x * c.az;
        c.bz = c.x * c.ay - c.y * c.ax;
        craters.push_back(c);
    }
    // the light in the planet's own frame (for the relief)
    float plx, ply, plz;
    toPlanet(lx, ly, lz, plx, ply, plz);
    // the height of the land: continents, then mountain ridges on them
    auto heightAt = [](float x, float y, float z, float& elev) {
        elev = fbm(x * 2.4f + 11.f, y * 2.4f, z * 2.4f, 6);
        const float ridge = 1.f - std::fabs(fbm(x * 7.f + 31.f, y * 7.f, z * 7.f, 4) * 2.f - 1.f);
        return elev + 0.22f * ridge * ridge * smoothstep(0.47f, 0.6f, elev) + 0.04f * fbm(x * 30.f, y * 30.f + 5.f, z * 30.f, 2);
    };
    // the city's frame on the sphere
    const float cnx = kCityLx / kR, cny = kCityLy / kR, cnz = std::sqrt(std::max(0.f, 1.f - cnx * cnx - cny * cny));
    float ccx, ccy, ccz;
    toPlanet(cnx, cny, cnz, ccx, ccy, ccz);
    // e1: across (east), e2: along (north) - the city map's x and y
    float e1x = ccz, e1y = 0.f, e1z = -ccx;
    {
        const float n = std::sqrt(e1x * e1x + e1z * e1z);
        e1x /= n;
        e1z /= n;
    }
    const float e2x = ccy * e1z - ccz * e1y, e2y = ccz * e1x - ccx * e1z, e2z = ccx * e1y - ccy * e1x;
    const CityMap city = makeCity();
    const float kCityHalf = 0.58f;  // the map covers this many radians either side of the heart

    for (int y = 0; y < pl.h; ++y) {
        if (cancel) return nullptr;
        const float ly2 = (float(pl.oy + y) + 0.5f) / R;
        for (int x = 0; x < pl.w; ++x) {
            const float lx2 = (float(pl.ox + x) + 0.5f) / R;
            Px& q = pl.px[size_t(y) * size_t(pl.w) + size_t(x)];
            const float d2 = lx2 * lx2 + ly2 * ly2;
            const float dith = bayer(x, y);
            if (d2 > 1.f) {  // the halo: the atmosphere glowing past the edge
                const float d = std::sqrt(d2), t = (d - 1.f) / (kHalo / kR);
                if (t >= 1.f) continue;
                const float lit = (lx2 / d) * lx + (ly2 / d) * ly;
                q.inside = 2;
                q.rim = uint8_t(255.f * (1.f - t) * (1.f - t));
                q.day = uint8_t(255.f * smoothstep(-0.25f, 0.35f, lit));
                continue;
            }
            const float nx = lx2, ny = ly2, nz = std::sqrt(1.f - d2);
            float px, py, pz;
            toPlanet(nx, ny, nz, px, py, pz);
            const float ndl = nx * lx + ny * ly + nz * lz;
            const float day = smoothstep(-0.06f, 0.24f, ndl);
            q.inside = 1;
            q.day = uint8_t(255.f * day);
            q.rim = uint8_t(255.f * (1.f - nz) * (1.f - nz) * (1.f - nz));
            q.tw = uint8_t(hash32(uint32_t(x) * 7919u + uint32_t(y) * 104729u) & 255u);
            // where on the cloud map
            const float lat = std::asin(std::max(-1.f, std::min(1.f, py))), lon = std::atan2(pz, px);
            q.cu = uint16_t(int((lon < 0.f ? lon + 2.f * kPi : lon) / (2.f * kPi) * float(pl.cw)) % pl.cw);
            q.cv = uint16_t(std::min(pl.ch - 1, int((lat + kPi / 2.f) / kPi * float(pl.ch))));
            // the ground: scorched land, poisoned seas, ash fields, blast craters
            float elev;
            const float h0 = heightAt(px, py, pz, elev);
            // relief: the slope of the land turned toward or away from the sun (two more height samples along the surface)
            float t1x = pz, t1z = -px;
            {
                const float n = std::sqrt(t1x * t1x + t1z * t1z) + 1e-6f;
                t1x /= n;
                t1z /= n;
            }
            const float t2x = py * t1z, t2y = pz * t1x - px * t1z, t2z = -py * t1x;
            const float eps = 0.0035f;
            float e1, e2;
            const float g1 = (heightAt(px + eps * t1x, py, pz + eps * t1z, e1) - h0) / eps;
            const float g2 = (heightAt(px + eps * t2x, py + eps * t2y, pz + eps * t2z, e2) - h0) / eps;
            const bool sea = elev < 0.47f;
            const float bump = sea ? 0.004f : 0.05f;
            float bnx = px - bump * (g1 * t1x + g2 * t2x), bny = py - bump * g2 * t2y, bnz = pz - bump * (g1 * t1z + g2 * t2z);
            {
                const float n = std::sqrt(bnx * bnx + bny * bny + bnz * bnz);
                bnx /= n;
                bny /= n;
                bnz /= n;
            }
            const float relief = bnx * plx + bny * ply + bnz * plz;  // the sunlight on the slope
            const float detail = fbm(px * 14.f + 3.f, py * 14.f, pz * 14.f, 3);
            const float ash = fbm(px * 4.f + 7.f, py * 4.f + 2.f, pz * 4.f, 4);
            const float biome = fbm(px * 3.f + 20.f, py * 3.f, pz * 3.f + 4.f, 3) + (dith - 0.5f) * 0.06f;
            float alb = sea ? 0.25f + 0.9f * (elev - 0.3f) : 0.38f + 0.45f * detail + 0.9f * (h0 - 0.5f);
            if (!sea && ash > 0.6f) alb *= 0.6f;  // burnt black
            float burn = 0.f, glass = 0.f;
            for (const Crater& c : craters) {
                const float cd = px * c.x + py * c.y + pz * c.z;
                if (cd < 0.85f) continue;
                const float th = std::sqrt(std::max(0.f, 2.f * (1.f - cd)));
                if (th > c.rad * 2.6f) continue;
                if (th < c.rad * 0.78f) {  // the floor: fused to glass
                    alb *= 0.25f + 0.4f * th / c.rad;
                    glass = std::max(glass, 1.f - th / (c.rad * 0.78f) * 0.6f);
                } else if (th < c.rad) {
                    alb = std::min(1.1f, alb * 1.5f + 0.18f);  // the thrown-up rim
                } else if (c.rad > 0.022f) {               // rays of ash and glass thrown out
                    const float ang = std::atan2(px * c.bx + py * c.by + pz * c.bz, px * c.ax + py * c.ay + pz * c.az);
                    const float ray = vnoise(ang * 3.2f + 40.f, float(&c - &craters[0]), 0.5f);
                    const float fall = 1.f - (th - c.rad) / (c.rad * 1.6f);
                    if (ray > 0.62f) alb = std::min(1.1f, alb + (ray - 0.62f) * 1.6f * fall);
                    else alb *= 1.f - 0.25f * fall;  // the ash between them
                }
                if (c.hot) burn = std::max(burn, 1.f - th / (c.rad * 1.1f));
            }
            q.burn = uint8_t(255.f * std::max(0.f, std::min(1.f, burn)));
            const float lam = std::max(0.f, sea ? ndl : relief * 0.75f + ndl * 0.25f);
            const float shadeV = day * (0.28f + 0.72f * lam);
            uint32_t col;
            if (shadeV < 0.07f + 0.05f * dith) col = ramp(kNight, 4, alb * 0.9f, dith);
            else if (sea) col = ramp(kOcean, 6, shadeV * (0.55f + 0.45f * alb), dith);
            else col = ramp(biome > 0.53f ? kAsh : kGround, 7, shadeV * (0.35f + 0.65f * alb), dith);
            // glassed crater floors catch the light with a green-teal sheen
            if (glass > 0.f && shadeV > 0.2f) col = mix(col, rgb(70, 170, 150), int(glass * shadeV * 110.f));
            // dusk: a magenta band along the line between day and night (the Retrowave sunset)
            const float dusk = smoothstep(-0.07f, 0.0f, ndl) * (1.f - smoothstep(0.0f, 0.11f, ndl));
            if (dusk > 0.f) col = mix(col, rgb(180, 56, 120), int(dusk * 75.f));
            q.base = col;
            // the city (sampled 2x2 on its map, so the thin roads don't break up)
            const float cdot = px * ccx + py * ccy + pz * ccz;
            if (cdot > 0.7f) {
                const float a = (px * e1x + py * e1y + pz * e1z) / kCityHalf, b = (px * e2x + py * e2y + pz * e2z) / kCityHalf;
                const int ix = int((a * 0.5f + 0.5f) * float(CityMap::N)), iy = int((-b * 0.5f + 0.5f) * float(CityMap::N));
                if (ix >= 0 && iy >= 0 && ix + 1 < CityMap::N && iy + 1 < CityMap::N) {
                    int lum = 0, dist = 0;
                    for (int j = 0; j < 2; ++j)
                        for (int i = 0; i < 2; ++i) {
                            const size_t ci = size_t(iy + j) * CityMap::N + size_t(ix + i);
                            lum = std::max<int>(lum, city.lum[ci]);
                            if (city.dist[ci] && (!dist || city.dist[ci] < dist)) dist = city.dist[ci];
                        }
                    q.city = uint8_t(lum);
                    q.road = uint16_t(dist);
                }
            }
        }
    }
    return P;
}

// ---------------------------------------------------------------- making it in the background
struct Maker {
    std::thread t;
    std::atomic<bool> busy{false}, done{false}, cancel{false};
    std::unique_ptr<Planet> next;
    float k = 0.f;  // the size being made
    ~Maker() {
        cancel = true;
        if (t.joinable()) t.join();
    }
    void start(float kk) {
        if (busy) return;
        if (t.joinable()) t.join();
        busy = true;
        done = false;
        k = kk;
        t = std::thread([this, kk] {
            next = makePlanet(kk, cancel);
            done = true;
            busy = false;
        });
    }
};
std::unique_ptr<Planet> g_planet;  // (declared before the maker: the maker is destroyed - joined - first)
Maker g_maker;

// the art grid for a canvas: one art pixel = blow x blow real pixels (2 on a big sharp screen, so the pixels stay crisp squares)
int blowOf(float scale) { return scale >= 1.75f ? 2 : 1; }

// takes a finished planet; null until one is ready (for the size asked, or another one while that is made)
const Planet* planetFor(float k) {
    if (g_maker.done) {
        if (g_maker.t.joinable()) g_maker.t.join();
        g_maker.done = false;
        if (g_maker.next) g_planet = std::move(g_maker.next);
    }
    if ((!g_planet || std::fabs(g_planet->k - k) > 0.001f) && !(g_maker.busy && std::fabs(g_maker.k - k) <= 0.001f)) g_maker.start(k);
    return g_planet.get();
}

// ---------------------------------------------------------------- the frame
struct Art {
    float k = 1.f;
    int blow = 1, w = 0, h = 0, bs = 1;  // bs: the pixel size of the sprites (art pixels per game unit, whole)
    std::vector<uint32_t> px;
    std::vector<uint32_t> sky;  // the sky without stars (made once per size)
    int skyW = 0, skyH = 0;
    float skyK = 0.f;
    uint32_t last = 0;  // (the Original picture: the scene is redrawn 15 times a second)
} g_art;

inline void put(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= g_art.w || y >= g_art.h) return;
    g_art.px[size_t(y) * size_t(g_art.w) + size_t(x)] = c;
}
inline void blend(int x, int y, uint32_t c, int a) {
    if (x < 0 || y < 0 || x >= g_art.w || y >= g_art.h || a <= 0) return;
    uint32_t& d = g_art.px[size_t(y) * size_t(g_art.w) + size_t(x)];
    d = mix(d, c, std::min(255, a));
}
// one sprite pixel (bs x bs art pixels) at game-unit position (gx, gy)
inline void block(float gx, float gy, uint32_t c, int a = 255) {
    const int bs = g_art.bs, x0 = int(std::floor(gx * g_art.k)), y0 = int(std::floor(gy * g_art.k));
    for (int y = 0; y < bs; ++y)
        for (int x = 0; x < bs; ++x) {
            if (a >= 255) put(x0 + x, y0 + y, c);
            else blend(x0 + x, y0 + y, c, a);
        }
}

void makeSky(int W, int H) {
    const float k = g_art.k;
    g_art.skyW = W;
    g_art.skyH = H;
    g_art.skyK = k;
    const int aw = g_art.w, ah = g_art.h;
    g_art.sky.assign(size_t(aw) * size_t(ah), 0);
    // one ramp from deep space through violet to a magenta glow, so the nebula's edges dither away instead of stopping
    static const uint32_t kSpace[] = {rgb(3, 2, 10),   rgb(6, 4, 16),   rgb(10, 6, 23),  rgb(15, 8, 31),   rgb(22, 10, 41),
                                      rgb(32, 12, 53), rgb(46, 15, 67), rgb(64, 19, 82), rgb(88, 25, 98), rgb(118, 34, 114),
                                      rgb(150, 48, 128)};
    const int nSpace = int(sizeof(kSpace) / sizeof(kSpace[0]));
    for (int y = 0; y < ah; ++y)
        for (int x = 0; x < aw; ++x) {
            const float gx = (float(x) + 0.5f) / k, gy = (float(y) + 0.5f) / k;
            const float d = bayer(x, y);
            const float base = 0.05f + 0.20f * gy / float(H);  // darker at the top
            // the nebula: wisps along a band from the lower left to the upper right, torn and swirled
            const float u = gx / float(W), w = gy / float(H);
            const float off = (1.f - w) - u * 0.75f - 0.18f;  // 0 on the band's middle line
            const float band = std::max(0.f, 1.f - off * off * 9.f);
            const float wx = fbm(gx * 0.006f, gy * 0.006f, 1.7f, 3) - 0.5f, wy = fbm(gx * 0.006f + 9.f, gy * 0.006f, 2.3f, 3) - 0.5f;
            const float n = fbm(gx * 0.011f + wx * 2.4f, gy * 0.016f + wy * 2.4f, 3.1f, 6);
            const float wisp = smoothstep(0.42f, 0.78f, n) * band;
            // a fainter teal haze the other way across
            const float n2 = fbm(gx * 0.008f + 40.f, gy * 0.02f, 5.5f, 5);
            const float off2 = w - 0.18f - u * 0.35f;
            const float teal = smoothstep(0.55f, 0.8f, n2) * std::max(0.f, 1.f - off2 * off2 * 30.f);
            const float v = base + wisp * 0.62f;
            uint32_t col = ramp(kSpace, nSpace, v, d);
            if (teal > 0.f) col = mix(col, rgb(26, 88, 112), int(teal * 70.f * (0.8f + 0.4f * d)));
            // a far sun beyond the top right corner (the planet's light comes from that side)
            const float sx = gx - float(W) - 30.f, sy = gy + 60.f, sd = std::sqrt(sx * sx + sy * sy);
            const float glow = std::max(0.f, 1.f - sd / 260.f);
            if (glow > 0.f) col = mix(col, rgb(255, 150, 210), int(glow * glow * 150.f * (0.7f + 0.3f * d)));
            g_art.sky[size_t(y) * size_t(aw) + size_t(x)] = col;
        }
}

// stars drifting left in three layers; the pan lifts them a little (the near layers more)
void drawStars(int W, int H, uint32_t now, float lift) {
    static const float kSpeed[3] = {3.f, 7.f, 15.f}, kPar[3] = {0.05f, 0.12f, 0.24f};
    static const int kCount[3] = {90, 56, 30};
    const float Hf = float(H);
    for (int layer = 0; layer < 3; ++layer) {
        const int n = kCount[layer] * W / 640;
        for (int i = 0; i < n; ++i) {
            const uint32_t h = hash32(uint32_t(i) * 7919u + uint32_t(layer) * 104729u + 17u);
            const double dd = double(now) * 0.001 * double(kSpeed[layer]);
            const double drift = dd - std::floor(dd / double(W)) * double(W);
            double gx = double(h % uint32_t(W * 16)) / 16.0 - drift;
            if (gx < 0) gx += double(W);
            const float gy = float((h >> 12) % 4800u) / 4800.f * Hf + lift * kPar[layer];
            const float wy = gy - std::floor(gy / Hf) * Hf;
            const uint32_t pick = (h >> 24) % 10u;
            const uint32_t col = pick == 0 ? rgb(255, 110, 220) : pick == 1 ? rgb(110, 230, 255) : pick == 2 ? rgb(255, 214, 160) : rgb(232, 234, 248);
            const float tw = 0.7f + 0.3f * float(std::sin(double(now) * 0.003 + double(h & 255u)));
            const int a = int(float(115 + layer * 60) * tw);
            block(float(gx), wy, col, a);
            if (layer == 2 && (h & 7u) == 0) {  // the brightest get a little cross
                block(float(gx) - 1.f, wy, col, a / 3);
                block(float(gx) + 1.f, wy, col, a / 3);
                block(float(gx), wy - 1.f, col, a / 3);
                block(float(gx), wy + 1.f, col, a / 3);
            }
        }
    }
}

// the planet with its centre at art pixel (cx, cy)
void drawPlanet(const Planet& pl, int cx, int cy, uint32_t now) {
    const float t = float(now % 3600000u) * 0.001f;
    const int shift = int(t * 1.6f * pl.k);  // the clouds drift east
    const float pulseSpeed = 22.f, pulsePeriod = 170.f;
    const float burnPulse = 0.75f + 0.25f * std::sin(t * 1.3f);
    const float haloPulse = 0.85f + 0.15f * std::sin(t * 0.9f);
    // (a planet made for another size - the screen's size changed - is shown resized until the new one is ready)
    const float r = pl.k / g_art.k;
    const bool same = std::fabs(r - 1.f) < 1e-4f;
    const int ax0 = std::max(0, cx + int(std::floor(float(pl.ox) / r))), ax1 = std::min(g_art.w, cx + int(std::ceil(float(pl.ox + pl.w) / r)));
    const int ay0 = std::max(0, cy + int(std::floor(float(pl.oy) / r))), ay1 = std::min(g_art.h, cy + int(std::ceil(float(pl.oy + pl.h) / r)));
    for (int y = ay0; y < ay1; ++y) {
        const int py = (same ? y - cy : int(std::floor((float(y - cy) + 0.5f) * r))) - pl.oy;
        if (py < 0 || py >= pl.h) continue;
        const Px* row = &pl.px[size_t(py) * size_t(pl.w)];
        uint32_t* out = &g_art.px[size_t(y) * size_t(g_art.w)];
        for (int x = ax0; x < ax1; ++x) {
            const int pxi = (same ? x - cx : int(std::floor((float(x - cx) + 0.5f) * r))) - pl.ox;
            if (pxi < 0 || pxi >= pl.w) continue;
            const Px& q = row[pxi];
            if (!q.inside) continue;
            const float d = bayer(x - cx, y - cy);
            if (q.inside == 2) {  // the halo
                const float day = float(q.day) / 255.f;
                const uint32_t glow = mix(rgb(150, 60, 210), rgb(110, 232, 244), int(day * 255.f));
                const int a = int(float(q.rim) * (0.35f + 0.65f * day) * haloPulse);
                if (a > int(d * 60.f)) out[x] = mix(out[x], glow, a);
                continue;
            }
            uint32_t col = q.base;
            const float day = float(q.day) / 255.f, night = 1.f - day;
            const int cl = pl.cloud[size_t(q.cv) * size_t(pl.cw) + size_t((q.cu + shift) % pl.cw)];
            const float cf = float(cl) / 255.f;
            if (cf > d * 0.95f) {  // a cloud over it (dithered cover)
                if (day > 0.18f) col = ramp(kCloud, 8, day * (0.25f + 0.75f * cf), d);
                else col = ramp(kCloudNight, 4, cf * 0.8f, d);
            }
            // the city's lights on the night side: twinkling, with pulses running out along the roads; dimmed under clouds
            if (q.city && night > 0.25f) {
                float v = float(q.city) / 255.f * (night - 0.2f) * 1.25f;
                v *= 0.82f + 0.18f * std::sin(t * 3.1f + float(q.tw) * 0.1f);
                if (q.road) {
                    const float ph = std::fmod(float(q.road) - t * pulseSpeed + 100000.f, pulsePeriod);
                    if (ph < 6.f) v = std::min(1.f, v + (1.f - ph / 6.f) * 0.55f);
                }
                v *= 1.f - 0.65f * cf;
                if (v > 0.12f + d * 0.25f) col = ramp(kCity, 6, v, d);
                else if (v > 0.05f) col = mix(col, rgb(120, 48, 14), int(v * 900.f));  // a sodium haze around the lights
            }
            // blast craters still glowing in the dark
            if (q.burn && night > 0.3f) {
                const float v = float(q.burn) / 255.f * night * burnPulse;
                if (v > 0.2f + d * 0.4f) col = ramp(kBurn, 5, v, d);
            }
            // the atmosphere along the edge: neon cyan where it's day, a violet thread at night
            if (q.rim > 20) {
                const float rr = float(q.rim) / 255.f;
                const uint32_t rc = mix(rgb(120, 50, 180), rgb(120, 236, 246), int(day * 255.f));
                col = mix(col, rc, int(rr * (0.25f + 0.75f * day) * 230.f));
            }
            out[x] = col;
        }
    }
}

// ---------------------------------------------------------------- the station
// A long spine with a great ring and a small one round it (seen almost edge-on), modules, solar wings and lights; lit from the
// right. (sx, sy) = the middle of the spine, game units.
void drawStation(float sx, float sy, uint32_t now) {
    const float t = float(now % 3600000u) * 0.001f;
    const uint32_t hull = rgb(168, 172, 194), hullD = rgb(84, 86, 112), hullDD = rgb(50, 50, 74), hullL = rgb(226, 230, 244), win = rgb(96, 232, 255);
    // a ring: an ellipse (a across, b up and down) two units thick, the half behind the spine first, shaded by the light from the
    // right, windows running round it
    auto ring = [&](float rx, float a, float b, bool front, float speed) {
        const int n = int(b * 7.f);
        for (int i = 0; i < n; ++i) {
            const float ang = float(i) / float(n) * 2.f * kPi;
            const float s = std::sin(ang);
            if ((s >= 0.f) != front) continue;
            const float x = rx + s * a, y = sy - std::cos(ang) * b;
            const float lit = 0.5f + 0.5f * s;  // the near side faces us and the sun
            uint32_t c = front ? (lit > 0.75f ? hullL : hull) : hullDD;
            const float run = std::fmod(ang * 4.f - t * speed + 100.f * kPi, 2.f * kPi);
            if (run < 0.22f) c = front ? win : rgb(40, 110, 140);
            block(x, y, c);
            block(x + 1.f, y, front ? hullD : hullDD);
        }
    };
    ring(sx - 10.f, 7.f, 30.f, false, 0.9f);
    ring(sx + 26.f, 4.f, 17.f, false, -1.3f);
    // the spine: three rows, a lit top and a dark belly, a row of windows
    for (float x = sx - 52.f; x <= sx + 46.f; x += 1.f) {
        block(x, sy - 1.f, hullL);
        block(x, sy, hull);
        block(x, sy + 1.f, hullD);
        if (int(x) % 3 == 0) block(x, sy, win, 190);
    }
    // the spokes from the spine to the great ring
    for (float y = 2.f; y < 30.f; y += 1.f) {
        block(sx - 10.f, sy - y, y < 3.f ? hull : hullD, 220);
        block(sx - 10.f, sy + y, hullDD, 220);
    }
    // modules: the hab block, the docks, the reactor at the stern
    auto box = [&](float x0, float x1, float y0, float y1) {
        for (float x = x0; x <= x1; x += 1.f)
            for (float y = y0; y <= y1; y += 1.f) block(x, y, y <= y0 + 1.f ? hullL : y >= y1 - 1.f ? hullDD : x >= x1 - 1.f ? hullL : hull);
    };
    box(sx - 32.f, sx - 20.f, sy - 4.f, sy + 4.f);
    box(sx + 4.f, sx + 16.f, sy - 5.f, sy + 3.f);
    box(sx + 34.f, sx + 42.f, sy - 3.f, sy + 3.f);
    for (float x = sx - 30.f; x <= sx - 22.f; x += 2.f) block(x, sy - 2.f, win);
    for (float x = sx + 6.f; x <= sx + 14.f; x += 2.f) block(x, sy - 2.f, win);
    // the reactor at the stern glows magenta, breathing
    const int glowA = int(150.f + 100.f * std::sin(t * 2.f));
    block(sx - 54.f, sy, rgb(255, 80, 210), glowA);
    block(sx - 55.f, sy, rgb(255, 80, 210), glowA / 2);
    block(sx - 54.f, sy - 1.f, rgb(255, 80, 210), glowA / 3);
    block(sx - 54.f, sy + 1.f, rgb(255, 80, 210), glowA / 3);
    // the antenna mast at the bow
    for (float x = sx + 47.f; x <= sx + 56.f; x += 1.f) block(x, sy, hullD);
    // solar wings above and below the reactor block
    for (float x = sx + 33.f; x <= sx + 43.f; x += 1.f)
        for (float y = 6.f; y <= 13.f; y += 1.f) {
            const uint32_t c = (int(x) % 2 == 0 || int(y) % 3 == 0) ? rgb(70, 160, 220) : rgb(26, 40, 96);
            block(x, sy - y, c);
            block(x, sy + y, c);
        }
    for (float y = 4.f; y <= 6.f; y += 1.f) {
        block(sx + 38.f, sy - y, hullD);
        block(sx + 38.f, sy + y, hullD);
    }
    ring(sx - 10.f, 7.f, 30.f, true, 0.9f);
    ring(sx + 26.f, 4.f, 17.f, true, -1.3f);
    // navigation lights: red at the stern, green at the bow, a white strobe on top of the great ring, the dock lights blinking
    if (std::fmod(t, 1.2f) < 0.5f) block(sx - 52.f, sy - 2.f, rgb(255, 60, 60));
    if (std::fmod(t + 0.6f, 1.2f) < 0.5f) block(sx + 56.f, sy, rgb(80, 255, 120));
    if (std::fmod(t, 2.3f) < 0.12f) block(sx - 10.f, sy - 31.f, rgb(255, 255, 255));
    if (std::fmod(t, 0.8f) < 0.4f) block(sx + 10.f, sy + 4.f, rgb(255, 200, 80));
}

// ---------------------------------------------------------------- the traffic
// pixel ships facing right: H light hull, h hull, d dark, w window, e engine
const char* const kShip0[] = {".HHw.", "eHHHHw", ".dd.."};
const char* const kShip1[] = {"..hhhhh....", "ehHHHHHHhw.", "ehdddddhhHw", "..h.h.h...."};
const char* const kShip2[] = {"e.Hw", "eHHHw", "e.d."};
struct Sprite {
    const char* const* rows;
    int n;
};
const Sprite kSprites[3] = {{kShip0, 3}, {kShip1, 4}, {kShip2, 3}};

void drawSprite(int s, float x, float y, bool left, uint32_t engine, float sc) {
    const Sprite& sp = kSprites[s];
    for (int r = 0; r < sp.n; ++r) {
        const int len = int(std::strlen(sp.rows[r]));
        for (int i = 0; i < len; ++i) {
            const char ch = sp.rows[r][i];
            if (ch == '.') continue;
            const uint32_t c = ch == 'H' ? rgb(226, 230, 244) : ch == 'h' ? rgb(170, 176, 196) : ch == 'd' ? rgb(84, 88, 110) : ch == 'w' ? rgb(96, 232, 255) : engine;
            const float px = left ? x + (float(len - 1 - i) - float(len) / 2.f) * sc : x + (float(i) - float(len) / 2.f) * sc;
            const float py = y + (float(r) - float(sp.n) / 2.f) * sc;
            block(px, py, c);
            if (sc > 1.5f) {  // a near ship: each of its pixels twice as big
                block(px + 1.f, py, c);
                block(px, py + 1.f, c);
                block(px + 1.f, py + 1.f, c);
            }
        }
    }
}

struct Lane {
    float x0, y0, cx, cy, x1, y1;
};
void bez(const Lane& l, float u, float& x, float& y) {
    const float a = (1.f - u) * (1.f - u), b = 2.f * u * (1.f - u), c = u * u;
    x = l.x0 * a + l.cx * b + l.x1 * c;
    y = l.y0 * a + l.cy * b + l.y1 * c;
}

void drawTraffic(int W, float cityX, float cityY, float stX, float stY, float lift, uint32_t now) {
    struct Ship {
        int lane, sprite;
        float period, phase, jx, jy;
        bool up;  // leaving the city (lane 3: leaving the station)
        uint32_t engine;
    };
    static const Ship kShips[] = {
        {0, 0, 9.0f, 0.0f, -18.f, 6.f, true, rgb(255, 90, 210)},    {0, 1, 13.0f, 0.45f, 22.f, -4.f, true, rgb(255, 150, 60)},
        {0, 2, 7.5f, 0.7f, 6.f, 10.f, false, rgb(110, 230, 255)},   {0, 0, 11.0f, 0.2f, -30.f, -8.f, false, rgb(255, 90, 210)},
        {1, 2, 8.5f, 0.3f, -10.f, 0.f, true, rgb(110, 230, 255)},   {1, 0, 12.5f, 0.8f, 26.f, 12.f, true, rgb(255, 90, 210)},
        {2, 1, 14.0f, 0.1f, 14.f, -10.f, false, rgb(255, 150, 60)}, {2, 2, 9.5f, 0.55f, -24.f, 4.f, false, rgb(110, 230, 255)},
        {3, 1, 16.0f, 0.35f, 0.f, 0.f, true, rgb(255, 150, 60)},    {0, 2, 6.5f, 0.9f, 34.f, 14.f, true, rgb(255, 90, 210)},
    };
    const float t = float(now % 3600000u) * 0.001f;
    const float Wf = float(W);
    for (const Ship& s : kShips) {
        Lane L;
        const float sx = cityX + s.jx, sy = cityY + s.jy;  // where it lifts off (somewhere in the city)
        switch (s.lane) {
            case 0: L = {sx, sy, (sx + stX) * 0.5f + 10.f, std::min(sy, stY) - 60.f, stX - 56.f, stY}; break;  // to the station's stern dock
            case 1: L = {sx, sy, sx - 120.f, sy - 160.f, -40.f, 90.f + lift}; break;                          // away to the left, past us
            case 2: L = {Wf + 40.f, 120.f + lift, Wf - 60.f, sy - 120.f, sx, sy}; break;                      // in from the right, down
            default: L = {stX + 10.f, stY + 4.f, stX + 10.f, stY + 70.f, Wf + 40.f, stY + 110.f}; break;       // off from the station
        }
        const bool reverse = s.lane != 2 && s.lane != 3 && !s.up;  // coming down from the station
        float u = std::fmod(t / s.period + s.phase, 1.f);
        if (reverse) u = 1.f - u;
        // how near it is: tiny down at the city, full size up above
        const float fromCity = s.lane == 2 ? 1.f - u : s.lane == 3 ? 1.f : u;
        const float size = smoothstep(0.0f, 0.32f, fromCity);
        float x, y;
        bez(L, u, x, y);
        // its trail: where it was a moment ago
        for (int j = 1; j <= 14; ++j) {
            const float uu = u - float(j) * 0.006f * (reverse ? -1.f : 1.f);
            if (uu < 0.f || uu > 1.f) break;
            float tx, ty;
            bez(L, uu, tx, ty);
            block(tx, ty, s.engine, int(float(170 - j * 11) * std::max(0.25f, size)));
        }
        if (size < 0.45f) {  // far down: just its engine light
            block(x, y, s.engine, 200);
            block(x, y, rgb(255, 255, 255), int(size * 300.f));
            continue;
        }
        float nx, ny;
        bez(L, std::min(1.f, std::max(0.f, u + (reverse ? -0.01f : 0.01f))), nx, ny);
        // the ones passing close to us (out to the left, in from the right) are drawn bigger
        const bool near = (s.lane == 1 && u > 0.55f) || (s.lane == 2 && u < 0.4f);
        drawSprite(s.sprite, x, y, nx < x, s.engine, near ? 2.f : 1.f);
    }
}

}  // namespace

void prepare() {
    const float s = Canvas::screenScale();
    planetFor(s / float(blowOf(s)));
}

bool ready() {
    const float s = Canvas::screenScale();
    return planetFor(s / float(blowOf(s))) != nullptr;
}

void finish() {
    prepare();
    if (g_maker.t.joinable()) g_maker.t.join();
    prepare();
}

void cityOnScreen(const Canvas& c, float pan, float& x, float& y) {
    const float e = smooth01(pan);
    x = kCx + float(c.fullWidth() - 640) + kCityLx;
    y = kCy + (1.f - e) * kPanDrop + kCityLy;
}

void draw(Canvas& c, uint32_t now, float pan, float sky) {
    const int blow = blowOf(c.scale());
    const float k = c.scale() / float(blow);
    const Planet* pl = planetFor(k);
    const int W = c.fullWidth(), H = c.height();
    const int aw = (c.pw() + blow - 1) / blow, ah = (c.ph() + blow - 1) / blow;
    if (g_art.w != aw || g_art.h != ah || g_art.k != k) {
        g_art.k = k;
        g_art.blow = blow;
        g_art.bs = std::max(1, int(k + 0.5f));
        g_art.w = aw;
        g_art.h = ah;
        g_art.px.assign(size_t(aw) * size_t(ah), 0);
        g_art.last = 0;
    }
    if (g_art.skyW != W || g_art.skyH != H || g_art.skyK != k || g_art.sky.size() != g_art.px.size()) makeSky(W, H);
    const float e = smooth01(pan);
    // the Original picture (one art pixel per real pixel, scale 1 - also the Miyoo): the scene is redrawn 15 times a second
    // once the pan is over; the copy to the screen happens every frame
    const bool slow = c.unscaled();
    if (!slow || g_art.last == 0 || now - g_art.last >= 66u || pan < 1.f || !pl) {
        g_art.last = now ? now : 1u;
        const int sk = int(std::max(0.f, std::min(1.f, sky)) * 255.f);  // the sky, faded in from black
        if (sk >= 255) std::copy(g_art.sky.begin(), g_art.sky.end(), g_art.px.begin());
        else
            for (size_t i = 0; i < g_art.px.size(); ++i) g_art.px[i] = mix(0u, g_art.sky[i], sk);
        const float lift = (1.f - e) * kPanDrop;
        drawStars(W, H, now, lift);
        if (pl) drawPlanet(*pl, int(std::floor((kCx + float(W - 640)) * k)), int(std::floor((kCy + lift) * k)), now);
        const float stX = float(W) - 80.f, stY = 205.f + lift * 0.85f;
        if (pl) {
            float cityX, cityY;
            cityOnScreen(c, pan, cityX, cityY);
            drawTraffic(W, cityX, cityY, stX, stY, lift, now);
        }
        drawStation(stX, stY, now);
    }
    // onto the screen (each art pixel blow x blow real pixels); only inside the clip
    const Rect clip = c.clipIsView() ? Rect{0, 0, c.pw(), c.ph()} : c.pxClip();
    const int pw = c.pw();
    uint32_t* dst = c.pixels();
    for (int Y = clip.y; Y < clip.y + clip.h; ++Y) {
        const uint32_t* src = &g_art.px[size_t(Y / blow) * size_t(g_art.w)];
        uint32_t* d = dst + size_t(Y) * size_t(pw);
        if (blow == 1) std::copy(src + clip.x, src + clip.x + clip.w, d + clip.x);
        else
            for (int X = clip.x; X < clip.x + clip.w; ++X) d[X] = src[X >> 1];
    }
}

}  // namespace titlebg
