#include "ui/title_war.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <vector>

#include "gfx/canvas.h"
#include "gfx/font.h"
#include "ui/voidfall.h"

namespace titlewar {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kH = 480.f;
constexpr float kYh = 190.f;     // the horizon at pan 1 (game units)
constexpr float kDrop = 420.f;   // the pan: how far above the field the view starts
constexpr float kNearY = 474.f;  // the nearest row of the battle
constexpr float kFarY = 244.f;   // the farthest
constexpr float kManH = 42.f;    // a soldier's height at the bottom of the screen (game units)
constexpr float kDt = 1.f / 30.f;

// ------------------------------------------------------------------ small helpers
uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float hashf(uint32_t x) { return float(hash32(x) & 0xffffffu) / 16777215.f; }
float clamp01(float t) { return t < 0.f ? 0.f : t > 1.f ? 1.f : t; }
float smooth01(float t) {
    t = clamp01(t);
    return t * t * (3.f - 2.f * t);
}
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
uint32_t lerpc(uint32_t a, uint32_t b, float t) { return mix(a, b, int(clamp01(t) * 255.f)); }
uint32_t scalec(uint32_t c, float k) { return shade(c, k); }

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) : s(seed ? seed : 1) {}
    uint32_t u() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float f() { return float(u() & 0xffffffu) / 16777215.f; }
    float r(float a, float b) { return a + (b - a) * f(); }
    int i(int n) { return n > 0 ? int(u() % uint32_t(n)) : 0; }
};

// ------------------------------------------------------------------ a pixel target (the frame, the static world, a sprite)
struct Tgt {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    std::vector<uint8_t> al;  // sprites only: coverage
    bool alpha = false;
    void init(int W, int H, bool withAlpha) {
        w = W;
        h = H;
        px.assign(size_t(w) * size_t(h), 0);
        alpha = withAlpha;
        if (alpha) al.assign(size_t(w) * size_t(h), 0);
        else al.clear();
    }
    void put(int x, int y, uint32_t c, int a = 255) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        const size_t i = size_t(y) * size_t(w) + size_t(x);
        if (alpha) {
            if (a >= 255 || al[i] == 0) {
                px[i] = c;
                al[i] = uint8_t(std::max<int>(al[i], std::min(255, a)));
            } else {
                px[i] = mix(px[i], c, a);
                al[i] = uint8_t(std::min(255, al[i] + a * (255 - al[i]) / 255));
            }
        } else {
            px[i] = a >= 255 ? c : mix(px[i], c, a);
        }
    }
    void add(int x, int y, uint32_t c, int a = 255) {  // light: added, for lasers, flames and glows
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        uint32_t& d = px[size_t(y) * size_t(w) + size_t(x)];
        const int r = std::min(255, int((d >> 16) & 255) + int((c >> 16) & 255) * a / 255);
        const int g = std::min(255, int((d >> 8) & 255) + int((c >> 8) & 255) * a / 255);
        const int b = std::min(255, int(d & 255) + int(c & 255) * a / 255);
        d = uint32_t(r << 16 | g << 8 | b);
    }
    void rect(float x0, float y0, float x1, float y1, uint32_t c, int a = 255) {
        int ix0 = int(std::floor(x0 + 0.5f)), iy0 = int(std::floor(y0 + 0.5f)), ix1 = int(std::floor(x1 + 0.5f)), iy1 = int(std::floor(y1 + 0.5f));
        if (ix1 <= ix0) ix1 = ix0 + 1;
        if (iy1 <= iy0) iy1 = iy0 + 1;
        ix0 = std::max(0, ix0);
        iy0 = std::max(0, iy0);
        ix1 = std::min(w, ix1);
        iy1 = std::min(h, iy1);
        for (int y = iy0; y < iy1; ++y)
            for (int x = ix0; x < ix1; ++x) put(x, y, c, a);
    }
    void disc(float cx, float cy, float r, uint32_t c, int a = 255, bool light = false, float squash = 1.f) {
        const int y0 = int(std::floor(cy - r * squash)), y1 = int(std::ceil(cy + r * squash));
        const int x0 = int(std::floor(cx - r)), x1 = int(std::ceil(cx + r));
        if (r < 0.6f) {
            if (light) add(int(cx), int(cy), c, a);
            else put(int(cx), int(cy), c, a);
            return;
        }
        const float r2 = r * r, in2 = (r - 1.f) * (r - 1.f), isq = 1.f / squash;
        for (int y = std::max(0, y0); y <= std::min(h - 1, y1); ++y) {
            const float dy = (float(y) + 0.5f - cy) * isq, dy2 = dy * dy;
            if (dy2 > r2) continue;
            for (int x = std::max(0, x0); x <= std::min(w - 1, x1); ++x) {
                const float dx = float(x) + 0.5f - cx;
                const float d2 = dx * dx + dy2;
                if (d2 > r2) continue;
                const int aa = d2 > in2 ? int(float(a) * (r - std::sqrt(d2))) : a;  // (only the rim needs the root)
                if (light) add(x, y, c, aa);
                else put(x, y, c, aa);
            }
        }
    }
    void glow(float cx, float cy, float r, uint32_t c, int a) {  // a soft light, falling off to the edge
        const int x0 = std::max(0, int(cx - r)), x1 = std::min(w - 1, int(cx + r)), y0 = std::max(0, int(cy - r)), y1 = std::min(h - 1, int(cy + r));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float dx = float(x) + 0.5f - cx, dy = float(y) + 0.5f - cy;
                const float d = (dx * dx + dy * dy) / (r * r);
                if (d >= 1.f) continue;
                add(x, y, c, int(float(a) * (1.f - d) * (1.f - d)));
            }
    }
    void line(float x0, float y0, float x1, float y1, uint32_t c, int a = 255, bool light = false) {
        const float dx = x1 - x0, dy = y1 - y0;
        const int n = std::max(1, int(std::max(std::fabs(dx), std::fabs(dy)) + 0.5f));
        for (int i = 0; i <= n; ++i) {
            const float t = float(i) / float(n);
            const int x = int(std::floor(x0 + dx * t)), y = int(std::floor(y0 + dy * t));
            if (light) add(x, y, c, a);
            else put(x, y, c, a);
        }
    }
    // a filled polygon (even-odd)
    void poly(const float* xs, const float* ys, int n, uint32_t c, int a = 255) {
        float miny = ys[0], maxy = ys[0];
        for (int i = 1; i < n; ++i) {
            miny = std::min(miny, ys[i]);
            maxy = std::max(maxy, ys[i]);
        }
        const int y0 = std::max(0, int(std::floor(miny))), y1 = std::min(h - 1, int(std::ceil(maxy)));
        float xsAt[32];
        for (int y = y0; y <= y1; ++y) {
            const float sy = float(y) + 0.5f;
            int m = 0;
            for (int i = 0, j = n - 1; i < n; j = i++) {
                if ((ys[i] <= sy && ys[j] > sy) || (ys[j] <= sy && ys[i] > sy)) {
                    if (m < 32) xsAt[m++] = xs[i] + (sy - ys[i]) / (ys[j] - ys[i]) * (xs[j] - xs[i]);
                }
            }
            std::sort(xsAt, xsAt + m);
            for (int k = 0; k + 1 < m; k += 2) {
                const int xa = std::max(0, int(std::floor(xsAt[k] + 0.5f))), xb = std::min(w, int(std::floor(xsAt[k + 1] + 0.5f)));
                for (int x = xa; x < xb; ++x) put(x, y, c, a);
            }
        }
    }
};

// The Voidfall look (as voidfall::grade): violet in the shadows, a little warmth in the lights, the box art's diagonal print
// lines - done on the art grid (a quarter of the pixels on a big screen)
void gradeArt(Tgt& t, int period) {  // (period 0: no print lines)
    static uint8_t lr[256], lg[256], lb[256];
    static bool made = false;
    if (!made) {
        made = true;
        for (int v = 0; v < 256; ++v) {
            const float f = float(v) / 255.f, sh = (1.f - f) * (1.f - f), hi = f * f;
            lr[v] = uint8_t(std::min(255.f, float(v) + sh * 16.f + hi * 8.f));
            lg[v] = uint8_t(std::min(255.f, float(v) * 0.95f + sh * 4.f));
            lb[v] = uint8_t(std::max(0.f, std::min(255.f, float(v) + sh * 26.f - hi * 10.f)));
        }
    }
    for (int y = 0; y < t.h; ++y) {
        uint32_t* row = &t.px[size_t(y) * size_t(t.w)];
        int ph = period ? y % period : 1;
        for (int x = 0; x < t.w; ++x) {
            const uint32_t q = row[x];
            uint32_t r = lr[(q >> 16) & 255], g2 = lg[(q >> 8) & 255], b = lb[q & 255];
            if (period && ph == 0) {
                r = r * 236 >> 8;
                g2 = g2 * 236 >> 8;
                b = b * 236 >> 8;
            }
            row[x] = (r << 16) | (g2 << 8) | b;
            if (period && ++ph == period) ph = 0;
        }
    }
}

// a sprite drawn onto a target with its top-left at (x, y); fade 0..255; tint toward `haze` by `fog` 0..1
void blit(Tgt& dst, const Tgt& s, int x, int y, int fade = 255, uint32_t haze = 0, float fog = 0.f) {
    const int fogA = int(fog * 255.f);
    const int j0 = std::max(0, -y), j1 = std::min(s.h, dst.h - y), i0 = std::max(0, -x), i1 = std::min(s.w, dst.w - x);
    for (int j = j0; j < j1; ++j) {
        const uint32_t* sp = &s.px[size_t(j) * size_t(s.w)];
        const uint8_t* sa = &s.al[size_t(j) * size_t(s.w)];
        uint32_t* dp = &dst.px[size_t(y + j) * size_t(dst.w) + size_t(x)];
        for (int i = i0; i < i1; ++i) {
            const int a = fade >= 255 ? sa[i] : sa[i] * fade / 255;
            if (!a) continue;
            const uint32_t c = fogA ? mix(sp[i], haze, fogA) : sp[i];
            if (dst.alpha) dst.put(x + i, y + j, c, a);
            else dp[i] = a >= 255 ? c : mix(dp[i], c, a);
        }
    }
}

// ------------------------------------------------------------------ the field's geometry
// X: across the field (0 = the middle of the battle; +-1 ~ the screen's edges at the bottom row); Y: the ground's screen y at pan 1.
float g_W = 640.f;  // the canvas's full width (game units)
float depthScale(float Y) { return (Y - kYh) / (kH - kYh); }
float screenX(float X, float Y) { return g_W * 0.5f + X * (Y - kYh) * (g_W * 0.62f) / (kH - kYh); }
float fieldXAt(float sx, float Y) { return (sx - g_W * 0.5f) / std::max(1.f, (Y - kYh) * (g_W * 0.62f) / (kH - kYh)); }
float manH(float Y) { return kManH * depthScale(Y); }
float fogOf(float Y) {  // the far rows fade into the dusk haze
    const float s = depthScale(Y);
    return clamp01((0.42f - s) / 0.42f) * 0.75f;
}
const uint32_t kHaze = rgb(150, 96, 118);

// the sky's colour at a world height (game units at pan 1; above the horizon)
uint32_t skyAt(float wy) {
    struct Stop {
        float dy;
        uint32_t c;
    };
    static const Stop st[] = {{0.f, rgb(255, 156, 76)},  {18.f, rgb(250, 112, 82)}, {50.f, rgb(214, 70, 108)}, {100.f, rgb(140, 42, 124)},
                              {170.f, rgb(74, 22, 96)},   {260.f, rgb(34, 12, 62)},  {400.f, rgb(14, 6, 34)},   {640.f, rgb(5, 3, 14)}};
    const float dy = kYh - wy;
    if (dy <= 0.f) return st[0].c;
    for (int i = 0; i + 1 < int(sizeof(st) / sizeof(st[0])); ++i)
        if (dy < st[i + 1].dy) return lerpc(st[i].c, st[i + 1].c, (dy - st[i].dy) / (st[i + 1].dy - st[i].dy));
    return st[sizeof(st) / sizeof(st[0]) - 1].c;
}

// ------------------------------------------------------------------ the world: everything that doesn't move
struct Crater {
    float X, Y, r;  // r in field units
};
struct Prop {
    float X, Y;   // its foot
    float hw;     // half its width (field units)
    int kind;     // 0 wall, 1 rubble, 2 wreck, 3 crater's near rim, 4 slab / barricade, 5 mech wreck
    uint32_t seed;
    Tgt spr;      // drawn at the size of the art grid
    int ox = 0, oy = 0;  // the sprite's top-left from the foot point (art px)
    float made = 0.f;    // (mech wrecks: when)
    bool burning = false;
};
struct Ship {
    int kind;           // 0 dreadnought, 1 cruiser, 2 the Sol dreadnought (far, the bombardment)
    float x, y;         // world (game units at pan 1)
    float vx;           // drift
    float size;         // design units -> game units
    int dir;            // facing
    Tgt spr;
    int ox = 0, oy = 0;
    std::vector<std::pair<float, float>> engines;  // engine points (game units from (x, y))
    float bowX = 0.f, bowY = 0.f;                    // the gun (game units from (x, y))
};

// ------------------------------------------------------------------ the battle
enum class St : uint8_t { Run, Hold, Dying };
struct Man {
    float X, Y, tx;
    int side;  // 0 human (from the left), 1 the enemy (from the right)
    int hp;
    St st;
    float t;       // time in the state
    float fire;    // until the next volley
    int burst;     // shots left in the volley
    float burstT;  // until the next shot of it
    int prop;      // the cover he holds (-1: none)
    float phase;   // stride
    float speed;
    float born;
    float up;      // standing up out of cover to fire (0..1)
    float holdFor;
    uint32_t id;
};
struct Body {
    float X, Y;
    int side;
    float t0;  // when he fell
    uint32_t seed;
    int dir;
    // (once he has lain a while he is kept as a little picture: hundreds of them lie about)
    std::shared_ptr<Tgt> spr;
    float sprK = 0.f;
    int ox = 0, oy = 0;
};
struct Mech {
    float X, Y, tx;
    int side, hp;
    St st;
    float t, fire, phase, born;
    int shots;       // gatling: left in the burst
    float shotT;     // gatling: until the next
    float beamT;     // beam: time left (0 = off); charging < 0
    float bx, by;    // beam: where it lands (game units at pan 1)
    float bdx;       // the beam's sweep
    uint32_t id;
    uint32_t btarget;  // beam: the walker it's aimed at (0 = soldiers)
};
struct Shot {
    float x0, y0, x1, y1;  // game units at pan 1
    float t, dur;
    int kind;      // 0 las (red), 1 plasma (cyan-blue), 2 gatling laser (red, small)
    int target;    // man id or mech id (mechT) - resolved by id when it lands
    bool hit, mechT;
    float dmg;
};
struct Part {
    float x, y, vx, vy;  // game units at pan 1 (y: screen y)
    float age, life, size;
    uint32_t col;
    uint8_t kind;  // 0 ember (light), 1 smoke, 2 debris, 3 spark (light), 4 dust, 5 fire (light)
    float floor;   // debris: the ground it lands on
};
struct Fire {
    float x, y, size, until;  // a fire on the ground (burns until), y = screen y at pan 1
};
struct Blast {
    float x, y, X, Y, t, r;  // the bombardment: where (game units at pan 1 + field), age, radius (field units)
    int stage;               // 0 marked (the targeting light), 1 falling, 2 exploded
    float age;
};
// A walker blowing up (owner 14:05 "do the mechs explode on the voidfall background?" - made a big moment for 0.60.0): a flash, a
// fireball, a ring of shock along the ground, then secondary blasts on the hull as it buckles (screen y at pan 1)
struct Boom {
    float x, y, h, age;
    int next;       // the secondary blasts done so far
    uint32_t seed;
};

struct World {
    bool made = false;
    uint32_t seed = 0x51D0u;
    Rng rng{0x51D0u};
    float clock = 0.f;  // battle time (s)
    std::vector<Man> men;
    std::vector<Body> bodies;
    std::vector<Mech> mechs;
    std::vector<Shot> shots;
    std::vector<Part> parts;
    std::vector<Fire> fires;
    std::vector<Blast> blasts;
    std::vector<Boom> booms;
    std::vector<Crater> craters;
    std::vector<Prop> props;
    std::vector<Ship> ships;
    uint32_t nextId = 1;
    float spawnT[2] = {0.f, 0.f}, mechT[2] = {4.f, 7.f};
    float bombT = 14.f;  // until the first bombardment (battle time from the title's start)
    int bombs = 0, shotsFired = 0;
    float beamGap = 0.f;              // the enemy walkers: until the next beam may charge (one beam at a time, then a pause)
    float beamOn = 0.f;               // (screen tests: seconds with a beam firing)
    int mechsLost[2] = {0, 0};        // (screen tests)
    float W = 0.f;  // the width it was laid out for
    int alive[2] = {0, 0};  // (kept each step)
    std::vector<Blast> newCraters;  // craters still to be printed on the ground
} g;

// the art grid (as title_bg: 1 art pixel = 1 real pixel, 2x2 from scale 1.75)
struct Art {
    float k = 0.f;
    int blow = 1, w = 0, h = 0;
    Tgt frame;
    Tgt world;  // the static world: sky + skyline + ground, rows for world y -kDrop .. kH
    float worldK = 0.f;
    float worldW = 0.f;
    uint32_t lastNow = 0;
    bool started = false;
    float acc = 0.f;
} g_art;

int blowOf(float scale) { return scale >= 1.4f ? 2 : 1; }  // (a Steam Deck's 1.67 too: the battle is a lot of pixels)

// ------------------------------------------------------------------ laying out the field
float span0(float Y) { return 1.f + (1.f - depthScale(Y)) * 1.1f; }
void layout() {
    g.craters.clear();
    g.props.clear();
    Rng r(0xC2A7u);
    // craters: some big ones, many small; none right where the two lines meet in the middle
    for (int i = 0; i < 34; ++i) {
        Crater c;
        c.Y = r.r(kFarY - 4.f, kNearY + 8.f);
        c.X = r.r(-1.15f, 1.15f) * (1.f + (1.f - depthScale(c.Y)) * 1.4f);
        c.r = (i < 6 ? r.r(0.14f, 0.22f) : r.r(0.04f, 0.11f));
        g.craters.push_back(c);
    }
    // props: ruined walls, rubble, wrecks, slabs, the craters' near rims; spread so both sides find cover
    auto addProp = [&](float X, float Y, float hw, int kind) {
        Prop p;
        p.X = X;
        p.Y = Y;
        p.hw = hw;
        p.kind = kind;
        p.seed = r.u();
        g.props.push_back(std::move(p));
    };
    // cover at every depth on both sides (owner 13:24: "add more ruins to the middle of the battlefield to allow them to take
    // cover and spread out"): six rows from the back to the front, four or five pieces a row a side, jittered, plus rubble just
    // short of the middle
    for (int side = 0; side < 2; ++side) {
        const float s = side == 0 ? -1.f : 1.f;
        for (int row = 0; row < 6; ++row) {
            const float Yr = kFarY + 14.f + (kNearY - kFarY - 26.f) * (float(row) + 0.5f) / 6.f;
            const int n = row < 2 ? 5 : 4;
            for (int i = 0; i < n; ++i) {
                const float Y = Yr + r.r(-10.f, 10.f);
                const float span = (1.f + (1.f - depthScale(Y)) * 1.1f);
                const float X = s * (0.14f + 0.7f * (float(i) + r.r(0.15f, 0.85f)) / float(n)) * span;
                const int kind = (row + i) % 5 == 0 ? 0 : (row + i) % 5 == 1 ? 1 : (row + i) % 5 == 2 ? 4 : (row + i) % 5 == 3 ? 1 : (r.f() < 0.5f ? 2 : 0);
                addProp(X, Y, r.r(0.045f, 0.09f) * (0.8f + 0.4f * depthScale(Y)), kind);
            }
            if (row % 2 == 1) addProp(s * r.r(0.07f, 0.11f) * span0(Yr), Yr + r.r(-6.f, 6.f), 0.04f, 1);
        }
    }
    // a few tall ruins farther back and out on the flanks (scenery more than cover)
    for (int i = 0; i < 7; ++i) {
        const float Y = r.r(kFarY - 20.f, kFarY + 30.f);
        addProp(r.r(-2.6f, 2.6f), Y, r.r(0.15f, 0.35f), 0);
    }
    // the big craters' near rims hide whoever lies in them
    for (const Crater& c : g.craters)
        if (c.r > 0.1f) addProp(c.X, c.Y + 0.5f, c.r, 3);
}

// --- the sprites of the props (at the art grid's size)
void makePropRaw(Prop& p);
void makeProp(Prop& p) {
    makePropRaw(p);
    gradeArt(p.spr, 0);
}
void makePropRaw(Prop& p) {
    const float k = g_art.k;
    const float sc = depthScale(p.Y);
    const float wpx = 2.f * p.hw * (p.Y - kYh) * (g_W * 0.62f) / (kH - kYh) * k;  // its width in art px
    const float man = kManH * sc * k;                                            // a man's height in art px
    Rng r(p.seed);
    float hpx = 0.f;
    switch (p.kind) {
        case 0: hpx = man * r.r(1.1f, 2.4f); break;   // wall
        case 1: hpx = man * r.r(0.45f, 0.75f); break; // rubble
        case 2: hpx = man * 0.8f; break;              // wreck
        case 3: hpx = std::max(2.f, wpx * 0.16f); break;  // crater rim
        case 4: hpx = man * r.r(0.4f, 0.6f); break;   // slab / barricade
        default: hpx = man * 1.4f; break;             // mech wreck
    }
    const int W = std::max(3, int(wpx + 4.f)), H = std::max(3, int(hpx + 4.f));
    p.spr.init(W, H, true);
    p.ox = -W / 2;
    p.oy = -H + 1;
    Tgt& t = p.spr;
    const float x0 = 2.f, x1 = float(W) - 2.f, base = float(H) - 1.f;
    const uint32_t face = rgb(98, 92, 108), dark = rgb(58, 52, 70), rim = rgb(236, 146, 112), deep = rgb(34, 30, 44);
    switch (p.kind) {
        case 0: {  // a broken wall: jagged top, window holes, rebar, a lit rim along the top (the dusk behind it)
            const int steps = std::max(3, int((x1 - x0) / std::max(2.f, man * 0.35f)));
            std::vector<float> top(size_t(steps + 1));
            for (int i = 0; i <= steps; ++i) top[size_t(i)] = base - hpx * r.r(0.35f, 1.f);
            top[0] = base - hpx * r.r(0.2f, 0.7f);
            for (int i = 0; i < steps; ++i) {
                const float xa = x0 + (x1 - x0) * float(i) / float(steps), xb = x0 + (x1 - x0) * float(i + 1) / float(steps);
                const float xs[4] = {xa, xb, xb, xa}, ys[4] = {top[size_t(i)], top[size_t(i + 1)], base, base};
                t.poly(xs, ys, 4, face);
            }
            // texture: blocks and stains
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const size_t i = size_t(y) * size_t(W) + size_t(x);
                    if (!t.al[i]) continue;
                    const float n = fbm(float(x) * 0.25f / std::max(1.f, man * 0.1f), float(y) * 0.25f / std::max(1.f, man * 0.1f), float(p.seed % 97u), 3);
                    t.px[i] = scalec(face, 0.72f + 0.5f * n - 0.25f * float(y) / float(H) * 0.f);
                    if (y % std::max(3, int(man * 0.22f)) == 0) t.px[i] = scalec(t.px[i], 0.85f);  // courses of blocks
                }
            // the side in shadow
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < std::max(1, W / 6); ++x) {
                    const size_t i = size_t(y) * size_t(W) + size_t(x + 2);
                    if (x + 2 < W && t.al[i]) t.px[i] = mix(t.px[i], dark, 150);
                }
            // windows (dark holes) in tall walls
            if (hpx > man * 1.3f) {
                const float ww = std::max(2.f, man * 0.28f), wh = std::max(2.f, man * 0.38f);
                for (float wx = x0 + ww; wx + ww < x1 - ww * 0.5f; wx += ww * 2.4f) {
                    const float wy = base - hpx * 0.62f;
                    if (r.f() < 0.75f) t.rect(wx, wy, wx + ww, wy + wh, deep);
                }
            }
            // the lit rim on the broken top edge
            for (int x = 0; x < W; ++x)
                for (int y = 0; y < H; ++y) {
                    const size_t i = size_t(y) * size_t(W) + size_t(x);
                    if (!t.al[i]) continue;
                    t.px[i] = mix(t.px[i], rim, 200);
                    if (y + 1 < H) {
                        const size_t j = i + size_t(W);
                        if (t.al[j]) t.px[j] = mix(t.px[j], rim, 70);
                    }
                    break;
                }
            // rebar sticking out of the top
            for (int k2 = 0; k2 < 3; ++k2) {
                const float rx = x0 + (x1 - x0) * r.f();
                const int col = std::max(0, std::min(W - 1, int(rx)));
                int ty = 0;
                while (ty < H && !t.al[size_t(ty) * size_t(W) + size_t(col)]) ++ty;
                t.line(rx, float(ty), rx + r.r(-2.f, 2.f), float(ty) - r.r(1.f, std::max(2.f, man * 0.3f)), rgb(70, 50, 46));
            }
            break;
        }
        case 1:    // rubble: a heap of broken blocks
        case 3: {  // a crater's near rim: a low bank of thrown-up earth
            const bool rimK = p.kind == 3;
            const int n = rimK ? std::max(6, int(wpx / 3.f)) : std::max(5, int(wpx / std::max(2.f, man * 0.18f)));
            for (int i = 0; i < n; ++i) {
                const float u = (float(i) + 0.5f) / float(n);
                const float cx = x0 + (x1 - x0) * u;
                const float hump = rimK ? (1.f - std::fabs(u - 0.5f) * 1.6f) : std::sin(u * kPi);
                const float bh = hpx * (0.45f + 0.55f * hump) * r.r(0.7f, 1.f);
                const float bw = (x1 - x0) / float(n) * r.r(1.1f, 1.8f);
                const uint32_t c = rimK ? lerpc(rgb(84, 78, 92), rgb(120, 104, 112), r.f()) : lerpc(face, rgb(118, 110, 124), r.f());
                const float xs[5] = {cx - bw * 0.5f, cx - bw * 0.3f, cx + bw * 0.25f, cx + bw * 0.5f, cx - bw * 0.5f};
                const float ys[5] = {base, base - bh, base - bh * r.r(0.8f, 1.05f), base, base};
                t.poly(xs, ys, 4, c);
                t.line(cx - bw * 0.3f, base - bh, cx + bw * 0.25f, base - bh, rim, 120);  // their lit tops
            }
            if (!rimK)
                for (int i = 0; i < n * 2; ++i) t.disc(x0 + (x1 - x0) * r.f(), base - r.f() * 2.f, std::max(0.6f, man * 0.05f), scalec(face, r.r(0.6f, 1.1f)));
            break;
        }
        case 2: {  // a burnt-out tank: hull, turret, a broken barrel
            const float hw2 = (x1 - x0) * 0.5f, cx = (x0 + x1) * 0.5f;
            const uint32_t steel = rgb(70, 66, 80), steelD = rgb(40, 36, 50);
            const float xs[6] = {cx - hw2, cx - hw2 * 0.85f, cx + hw2 * 0.9f, cx + hw2, cx + hw2 * 0.8f, cx - hw2 * 0.8f};
            const float ys[6] = {base - hpx * 0.25f, base - hpx * 0.55f, base - hpx * 0.55f, base - hpx * 0.3f, base, base};
            t.poly(xs, ys, 6, steel);
            t.rect(cx - hw2 * 0.85f, base - hpx * 0.2f, cx + hw2 * 0.85f, base, steelD);  // tracks
            for (float tx = cx - hw2 * 0.8f; tx < cx + hw2 * 0.8f; tx += std::max(2.f, man * 0.12f)) t.put(int(tx), int(base - hpx * 0.1f), rgb(90, 84, 96));
            t.rect(cx - hw2 * 0.35f, base - hpx * 0.85f, cx + hw2 * 0.3f, base - hpx * 0.55f, steel);  // turret
            t.line(cx + hw2 * 0.3f, base - hpx * 0.72f, cx + hw2 * 1.05f, base - hpx * 0.95f, steelD);
            t.line(cx - hw2 * 0.8f, base - hpx * 0.55f, cx + hw2 * 0.9f, base - hpx * 0.55f, rim, 130);
            for (int i = 0; i < 6; ++i) t.disc(cx + hw2 * r.r(-0.8f, 0.8f), base - hpx * r.r(0.3f, 0.6f), std::max(0.7f, man * 0.05f), rgb(26, 22, 30));  // holes
            p.burning = r.f() < 0.7f;
            break;
        }
        case 4: {  // a fallen slab / a barricade of plates with warning stripes
            const bool barricade = r.f() < 0.5f;
            if (barricade) {
                for (float bx = x0; bx < x1; bx += std::max(3.f, man * 0.4f)) {
                    const float bw = std::max(3.f, man * 0.38f), bh = hpx * r.r(0.8f, 1.f);
                    t.rect(bx, base - bh, bx + bw - 1.f, base, rgb(82, 84, 98));
                    t.rect(bx, base - bh, bx + bw - 1.f, base - bh + std::max(1.f, man * 0.06f), rim, 160);
                    for (float sy = base - bh * 0.7f; sy < base - bh * 0.4f; sy += 2.f) t.rect(bx + 1.f, sy, bx + bw - 2.f, sy + 1.f, rgb(200, 150, 60), 140);
                }
            } else {
                const float xs[4] = {x0, x1, x1 - (x1 - x0) * 0.1f, x0 + (x1 - x0) * 0.05f};
                const float ys[4] = {base - hpx * 0.9f, base - hpx * 0.4f, base, base};
                t.poly(xs, ys, 4, face);
                t.line(x0, base - hpx * 0.9f, x1, base - hpx * 0.4f, rim, 170);
                for (int i = 0; i < 3; ++i) t.line(x0 + (x1 - x0) * r.f(), base - hpx * 0.6f, x0 + (x1 - x0) * r.f(), base - 1.f, dark, 160);  // cracks
            }
            break;
        }
        default: {  // a mech's wreck: a toppled hull, a leg sticking up, smoking
            const float cx = (x0 + x1) * 0.5f, hw2 = (x1 - x0) * 0.5f;
            const uint32_t m = p.seed & 1u ? rgb(110, 96, 70) : rgb(150, 124, 70);
            const float xs[5] = {cx - hw2, cx - hw2 * 0.5f, cx + hw2 * 0.7f, cx + hw2, cx - hw2 * 0.9f};
            const float ys[5] = {base - hpx * 0.3f, base - hpx * 0.6f, base - hpx * 0.55f, base, base};
            t.poly(xs, ys, 5, m);
            t.line(cx + hw2 * 0.2f, base - hpx * 0.5f, cx + hw2 * 0.5f, base - hpx * 1.f, scalec(m, 0.6f));
            t.line(cx + hw2 * 0.22f, base - hpx * 0.5f, cx + hw2 * 0.52f, base - hpx * 1.f, scalec(m, 0.6f));
            for (int i = 0; i < 8; ++i) t.disc(cx + hw2 * r.r(-0.8f, 0.8f), base - hpx * r.r(0.1f, 0.5f), std::max(0.7f, man * 0.06f), rgb(28, 22, 26));
            break;
        }
    }
}

// --- smooth shapes: drawn three times larger, then shrunk with their edges blended (no blocky steps)
void downsample(const Tgt& hi, int ss, Tgt& out) {
    out.init(std::max(1, hi.w / ss), std::max(1, hi.h / ss), true);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            int r = 0, gg = 0, b = 0, a = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    const size_t si = size_t(y * ss + j) * size_t(hi.w) + size_t(x * ss + i);
                    const int al = hi.al[si];
                    if (!al) continue;
                    const uint32_t c = hi.px[si];
                    r += int((c >> 16) & 255) * al;
                    gg += int((c >> 8) & 255) * al;
                    b += int(c & 255) * al;
                    a += al;
                }
            if (!a) continue;
            const size_t o = size_t(y) * size_t(out.w) + size_t(x);
            out.px[o] = rgb(r / a, gg / a, b / a);
            out.al[o] = uint8_t(a / (ss * ss));
        }
}

// --- the ships (owner 13:24, with two pictures: "look like these ... not like civilian spaceships"): warships - long, heavy
// armoured hulls ending in a ram, belts of armour plate with seams and fittings, rows of lit ports, gun batteries, a stepped
// citadel with the bridge and its masts, main turrets on the deck, pods under the keel, a bank of great engines at the stern
// (they glow live), blue light along the lower hull, warm dusk on the undersides; drawn three times larger and shrunk smooth
void makeShip(Ship& s) {
    const int SS = 3;
    const bool cruiser = s.kind == 1, sol = s.kind == 2;
    const float k = g_art.k * s.size * float(SS);  // hi-res px per design unit
    const int W = int(190.f * k) + 8, H = int(70.f * k) + 8;
    Tgt t;
    t.init(W, H, true);
    const float cx = float(W) / 2.f, cy = float(H) * 0.55f;
    auto X = [&](float dx) { return cx + float(s.dir) * dx * k; };
    auto Y = [&](float dy) { return cy + dy * k; };
    auto R = [&](float x0, float y0, float x1, float y1, uint32_t c, int a = 255) {
        t.rect(std::min(X(x0), X(x1)), Y(y0), std::max(X(x0), X(x1)), Y(y1), c, a);
    };
    auto P = [&](std::initializer_list<float> pts, uint32_t c) {
        float xs[20], ys[20];
        int n = 0;
        for (auto it = pts.begin(); it != pts.end() && n < 20; it += 2, ++n) {
            xs[n] = X(*it);
            ys[n] = Y(*(it + 1));
        }
        t.poly(xs, ys, n, c);
    };
    auto L = [&](float x0, float y0, float x1, float y1, uint32_t c, int a = 255) {
        const float w = std::max(1.f, k * 0.35f);
        for (float o = -w * 0.5f; o <= w * 0.5f; o += 1.f) t.line(X(x0), Y(y0) + o, X(x1), Y(y1) + o, c, a);
    };
    Rng r(uint32_t(s.kind * 977 + 11) + uint32_t(s.size * 100.f));
    const uint32_t top = rgb(104, 100, 120), side = rgb(64, 62, 80), dark = rgb(40, 38, 52), under = rgb(32, 28, 42), plate = rgb(82, 80, 98);
    const uint32_t warm = rgb(236, 132, 92), win = rgb(255, 198, 122), blue = rgb(90, 176, 255), gold = rgb(255, 196, 90);
    // its size
    const float S0 = cruiser ? -44.f : -72.f, S1 = cruiser ? 44.f : 74.f;  // stern, the ram's point
    const float D0 = cruiser ? -6.f : -9.f, D1 = cruiser ? 7.f : 11.f;     // deck, keel
    // the hull's side: a long armoured box, the ram sloping to its point
    P({S0, D0, S1 - 22.f, D0, S1, (D0 + D1) * 0.5f - 1.f, S1 + 2.f, (D0 + D1) * 0.5f + 1.f, S1 - 16.f, D1 - 2.f, S0 + 4.f, D1, S0, D1 - 3.f}, side);
    R(S0, D0, S1 - 22.f, D0 + 1.6f, top);  // the deck, catching the sky
    // armour belts with seams and fittings
    R(S0 + 3.f, D0 + 5.f, S1 - 16.f, D0 + 9.f, plate);
    R(S0 + 5.f, D1 - 5.f, S1 - 20.f, D1 - 2.f, dark);
    for (float sx = S0 + 6.f; sx < S1 - 18.f; sx += cruiser ? 6.f : 7.f) L(sx, D0 + 1.6f, sx, D1 - 1.f, dark, 150);
    for (int i = 0; i < (cruiser ? 40 : 80); ++i) {  // fittings, vents, hatches
        const float gx = r.r(S0 + 4.f, S1 - 20.f), gy = r.r(D0 + 2.f, D1 - 3.f), gw = r.r(0.8f, 3.5f), gh = r.r(0.5f, 1.8f);
        R(gx, gy, gx + gw, gy + gh, r.f() < 0.5f ? dark : plate);
    }
    // rows of lit ports, and gun ports along the belt
    for (float sx = S0 + 5.f; sx < S1 - 22.f; sx += 1.8f)
        if (r.f() < 0.7f) R(sx, D0 + 3.f, sx + 0.7f, D0 + 3.7f, win);
    for (float sx = S0 + 8.f; sx < S1 - 22.f; sx += 2.4f)
        if (r.f() < 0.55f) R(sx, D1 - 7.f, sx + 0.8f, D1 - 6.3f, win, 210);
    for (float sx = S0 + 10.f; sx < S1 - 24.f; sx += 4.5f) {
        R(sx, D0 + 6.f, sx + 1.4f, D0 + 7.6f, dark);
        L(sx + 1.4f, D0 + 6.8f, sx + 4.f, D0 + 6.6f, dark);  // the battery's barrels
    }
    // blue light along the lower hull
    R(S0 + 6.f, D1 - 2.f, S1 - 22.f, D1 - 1.5f, blue, 230);
    // the ram: a heavy plate with a lit edge and a red light at its point
    L(S1 - 22.f, D0, S1, (D0 + D1) * 0.5f - 1.f, rgb(150, 144, 166));
    L(S1 + 2.f, (D0 + D1) * 0.5f + 1.f, S1 - 16.f, D1 - 2.f, warm, 200);
    // the stepped citadel, the bridge with its window band, masts and a dish
    const float c0 = cruiser ? -26.f : -46.f, c1 = cruiser ? 6.f : 10.f;
    P({c0, D0, c1, D0, c1 - 3.f, D0 - 4.f, c0 + 2.f, D0 - 4.f}, side);
    R(c0 + 2.f, D0 - 4.f, c1 - 3.f, D0 - 3.3f, top);
    P({c0 + 6.f, D0 - 4.f, c1 - 9.f, D0 - 4.f, c1 - 12.f, D0 - 8.f, c0 + 8.f, D0 - 8.f}, side);
    R(c0 + 8.f, D0 - 8.f, c1 - 12.f, D0 - 7.3f, top);
    const float b0 = cruiser ? -16.f : -28.f, b1 = cruiser ? -6.f : -12.f;
    P({b0, D0 - 8.f, b1, D0 - 8.f, b1 - 1.f, D0 - 15.f, b0 + 2.f, D0 - 16.f}, side);
    R(b0 + 1.f, D0 - 13.f, b1 - 1.f, D0 - 12.f, rgb(170, 230, 255));  // the bridge's windows
    R(b0 + 2.f, D0 - 16.f, b1 - 1.f, D0 - 15.3f, top);
    for (float wx = c0 + 4.f; wx < c1 - 5.f; wx += 1.6f)
        if (r.f() < 0.65f) R(wx, D0 - 2.4f, wx + 0.6f, D0 - 1.8f, win);
    L(b0 + 4.f, D0 - 16.f, b0 + 4.f, D0 - 26.f, dark);
    L(b0 + 7.f, D0 - 16.f, b0 + 7.f, D0 - 22.f, dark);
    L(b0 + 2.f, D0 - 22.f, b0 + 8.f, D0 - 22.f, dark);
    t.disc(X(b0 + 4.f), Y(D0 - 26.f), std::max(1.f, 0.6f * k), rgb(255, 60, 60));
    t.disc(X(c1 - 6.f), Y(D0 - 6.f), 2.f * k, rgb(150, 144, 166), 255, false, 0.45f);  // the dish
    // the main turrets: armoured mounts with twin barrels, trained forward
    const float turrets[4] = {cruiser ? 14.f : 18.f, cruiser ? 26.f : 32.f, cruiser ? -36.f : 44.f, -60.f};
    for (int i = 0; i < (cruiser ? 3 : 4); ++i) {
        const float tx = turrets[i];
        P({tx - 3.5f, D0, tx + 3.5f, D0, tx + 2.5f, D0 - 3.f, tx - 2.5f, D0 - 3.f}, plate);
        R(tx - 2.5f, D0 - 3.f, tx + 2.5f, D0 - 2.5f, top);
        const float dirB = tx < c0 ? -1.f : 1.f;  // the aft turret faces aft
        L(tx + dirB * 2.f, D0 - 2.f, tx + dirB * 13.f, D0 - 2.6f, dark);
        L(tx + dirB * 2.f, D0 - 1.f, tx + dirB * 13.f, D0 - 1.4f, dark);
    }
    // pods under the keel and a keel spine
    R(S0 + 20.f, D1, S0 + 40.f, D1 + 3.f, dark);
    for (float px = S0 + 22.f; px < S0 + 39.f; px += 2.f) R(px, D1 + 1.f, px + 0.6f, D1 + 1.6f, win, 200);
    if (!cruiser) {
        R(S0 + 50.f, D1 - 0.5f, S0 + 64.f, D1 + 2.f, dark);
        L(S0 + 6.f, D1, S1 - 30.f, D1, under);
    }
    // the stern: the engine block, its great nozzles (the glow is drawn live) and manoeuvring vents
    R(S0 - 4.f, D0 - 2.f, S0 + 3.f, D1 + 1.f, dark);
    const int rows = cruiser ? 2 : 3;
    for (int i = 0; i < rows; ++i) {
        const float ey = D0 + (D1 - D0) * (float(i) + 0.5f) / float(rows);
        t.disc(X(S0 - 4.f), Y(ey), (cruiser ? 2.4f : 3.f) * k, rgb(70, 66, 86));
        t.disc(X(S0 - 4.5f), Y(ey), (cruiser ? 1.7f : 2.2f) * k, rgb(150, 200, 255));
        s.engines.push_back({float(s.dir) * (S0 - 5.f) * s.size, (ey - (float(H) * 0.55f - cy) / k) * s.size});
    }
    // the Sol dreadnought: gold trim and a spinal gun the length of the ship, slung under the keel
    if (sol) {
        L(S0 + 4.f, D0 + 5.f, S1 - 16.f, D0 + 5.f, gold, 230);
        L(S0 + 4.f, D0 + 9.f, S1 - 18.f, D0 + 9.f, gold, 200);
        R(-20.f, D1 + 1.f, S1 + 22.f, D1 + 4.f, rgb(70, 66, 86));
        for (float gx = -16.f; gx < S1 + 20.f; gx += 6.f) R(gx, D1 + 0.5f, gx + 1.5f, D1 + 4.5f, plate);
        R(S1 + 20.f, D1 + 0.5f, S1 + 24.f, D1 + 4.5f, gold);
        s.bowX = float(s.dir) * (S1 + 24.f) * s.size;
        s.bowY = (D1 + 2.5f) * s.size;
    } else {
        s.bowX = float(s.dir) * (S1 + 2.f) * s.size;
        s.bowY = ((D0 + D1) * 0.5f) * s.size;
    }
    // light: the dusk below warms every underside edge; a cool line on the top edges; a little grain in the metal
    for (int x = 0; x < W; ++x) {
        int lowest = -1, highest = -1;
        for (int y = 0; y < H; ++y)
            if (t.al[size_t(y) * size_t(W) + size_t(x)]) {
                if (highest < 0) highest = y;
                lowest = y;
            }
        if (lowest < 0) continue;
        for (int d = 0; d < int(k * 1.5f) && lowest - d >= 0; ++d) {
            const size_t i = size_t(lowest - d) * size_t(W) + size_t(x);
            if (t.al[i]) t.px[i] = mix(t.px[i], warm, 170 - d * int(120.f / std::max(1.f, k * 1.5f)));
        }
        for (int d = 0; d < int(k * 0.6f) + 1; ++d) {
            const size_t i = size_t(highest + d) * size_t(W) + size_t(x);
            if (highest + d < H && t.al[i]) t.px[i] = mix(t.px[i], rgb(196, 190, 220), 110);
        }
        for (int y = highest; y <= lowest; ++y) {
            const size_t i = size_t(y) * size_t(W) + size_t(x);
            if (t.al[i]) t.px[i] = scalec(t.px[i], 0.92f + 0.16f * hashf(uint32_t(x / 3) * 7919u + uint32_t(y / 3) * 104729u));
        }
    }
    downsample(t, SS, s.spr);
    gradeArt(s.spr, 0);
    s.ox = -s.spr.w / 2;
    s.oy = -int(cy / float(SS));
}

void makeShips() {
    g.ships.clear();
    // far to near: the Sol dreadnought far off over the left, a dreadnought and cruisers above the field
    const struct {
        int kind;
        float x, y, vx, size;
        int dir;
    } kS[] = {{2, 0.16f, 74.f, 1.2f, 0.5f, 1},   {0, 0.78f, 52.f, -1.6f, 0.8f, -1},  {1, 0.36f, 118.f, 3.0f, 0.55f, 1},
              {1, 0.62f, 140.f, -3.6f, 0.5f, -1},  {1, 0.05f, 12.f, 4.2f, 0.9f, 1},    {0, 0.42f, -40.f, 2.2f, 1.25f, 1},
              {1, 0.9f, -150.f, -2.6f, 1.0f, -1},  {0, 0.2f, -260.f, 1.4f, 0.75f, 1}};
    for (const auto& d : kS) {
        Ship s;
        s.kind = d.kind;
        s.x = d.x * g_W;
        s.y = d.y;
        s.vx = d.vx;
        s.size = d.size;
        s.dir = d.dir;
        makeShip(s);
        g.ships.push_back(std::move(s));
    }
}

// --- the skyline: a ruined futuristic megacity along the horizon, in three layers: needles, domed towers, arcologies, twin
// spires with halo rings, snapped megatowers; smooth (sampled three times across each pixel), lit at their edges by the dusk
// behind them, with bands of light, neon strips, sky-bridges and a few lights still burning
void drawSkyline(Tgt& t, float Wg) {
    const float k = g_art.k;
    struct Tower {
        float xc, w, h;
        int kind;   // 0 needle, 1 domed tower, 2 arcology, 3 twin spires, 4 snapped megatower, 5 curved slab
        int layer;  // 0 far .. 2 near
        float brk;
        uint32_t seed;
    };
    std::vector<Tower> towers;
    Rng r(0x7011u);
    for (int layer = 0; layer < 3; ++layer)
        for (float x = -20.f; x < Wg + 20.f; x += r.r(10.f, 28.f) * (layer == 2 ? 1.7f : 1.f)) {
            Tower tw;
            tw.xc = x;
            tw.layer = layer;
            tw.kind = r.i(6);
            const float sc = layer == 0 ? 0.8f : layer == 1 ? 1.2f : 0.85f;
            tw.w = (tw.kind == 0 ? r.r(8.f, 13.f) : tw.kind == 2 ? r.r(26.f, 44.f) : tw.kind == 3 ? r.r(14.f, 20.f) : r.r(8.f, 18.f)) * sc;
            tw.h = (tw.kind == 0 ? r.r(46.f, 80.f) : tw.kind == 2 ? r.r(22.f, 40.f) : tw.kind == 3 ? r.r(50.f, 76.f) : r.r(18.f, 58.f)) * sc;
            tw.brk = (tw.kind == 4 || r.f() < 0.3f) ? r.r(0.25f, 0.6f) : 0.f;
            tw.seed = r.u();
            towers.push_back(tw);
        }
    auto topOf = [](const Tower& tw, float x) {  // its height above the horizon at x (game units), 0 = not there
        const float u = (x - tw.xc) / (tw.w * 0.5f), a = std::fabs(u);
        if (a > 1.f) return 0.f;
        float top;
        switch (tw.kind) {
            case 0: top = tw.h * (1.f - a) * (0.35f + 0.65f * (1.f - a)); break;           // a needle
            case 1: top = tw.h * 0.82f + tw.h * 0.18f * std::sqrt(1.f - u * u); break;     // a domed tower
            case 2: top = tw.h * (1.f - u * u) * (1.f - 0.25f * a); break;                  // an arcology
            case 3: {                                                                       // twin spires on a base
                const float s1 = std::fabs(u - 0.45f) / 0.4f, s2 = std::fabs(u + 0.45f) / 0.4f;
                top = tw.h * 0.3f;
                if (s1 < 1.f) top = std::max(top, tw.h * (1.f - s1) * (0.4f + 0.6f * (1.f - s1)));
                if (s2 < 1.f) top = std::max(top, tw.h * 0.85f * (1.f - s2) * (0.4f + 0.6f * (1.f - s2)));
                break;
            }
            case 4: top = tw.h * (0.9f + 0.1f * std::sqrt(1.f - u * u)); break;              // a megatower
            default: top = tw.h * (0.7f + 0.3f * std::sqrt(1.f - u * u)) * (1.f - 0.3f * (u + 1.f) * 0.5f); break;  // a curved slab
        }
        if (tw.brk > 0.f) {  // snapped off: a slanted, ragged break
            const float cut = tw.h * (1.f - tw.brk * (0.6f + 0.4f * (tw.seed & 1u ? (u + 1.f) * 0.5f : (1.f - u) * 0.5f)));
            const float rag = tw.h * 0.05f * std::fabs(std::sin(x * 3.1f + float(tw.seed % 97u))) + tw.h * 0.04f * std::fabs(std::sin(x * 7.3f));
            top = std::min(top, cut - rag);
        }
        return std::max(0.f, top);
    };
    const float hr = (kYh + kDrop) * k;  // the horizon's row in the world picture
    for (int layer = 0; layer < 3; ++layer) {
        const uint32_t base = layer == 0 ? rgb(132, 64, 104) : layer == 1 ? rgb(74, 38, 84) : rgb(36, 22, 52);
        const float hazeTop = layer == 0 ? 0.55f : layer == 1 ? 0.35f : 0.18f;
        for (const Tower& tw : towers) {
            if (tw.layer != layer) continue;
            const int x0 = std::max(0, int((tw.xc - tw.w * 0.5f) * k) - 1), x1 = std::min(t.w - 1, int((tw.xc + tw.w * 0.5f) * k) + 1);
            const bool bands = (tw.seed >> 3) % 3u == 0u, strip = (tw.seed >> 5) % 4u == 0u;
            const uint32_t neon = (tw.seed >> 7) & 1u ? rgb(90, 220, 255) : rgb(255, 80, 200);
            for (int px = x0; px <= x1; ++px) {
                float tops[3];
                float maxTop = 0.f;
                for (int s = 0; s < 3; ++s) {
                    tops[s] = topOf(tw, (float(px) + 0.17f + 0.33f * float(s)) / k) * k;
                    maxTop = std::max(maxTop, tops[s]);
                }
                if (maxTop <= 0.f) continue;
                const float edgeL = topOf(tw, (float(px) - 1.f) / k), edgeR = topOf(tw, (float(px) + 2.f) / k);
                for (int py = int(hr - maxTop) - 1; py < int(hr); ++py) {
                    if (py < 0 || py >= t.h) continue;
                    float cov = 0.f;
                    for (int s = 0; s < 3; ++s) cov += clamp01(float(py) + 1.f - (hr - tops[s]));
                    cov /= 3.f;
                    if (cov <= 0.f) continue;
                    const float hgt = (hr - float(py)) / k;  // game units above the horizon
                    uint32_t c = lerpc(base, rgb(186, 86, 104), clamp01(1.f - hgt / 60.f) * hazeTop * 1.6f);
                    const float topHere = topOf(tw, (float(px) + 0.5f) / k);
                    // the dusk on its edges: the outline catching the glow behind it
                    if (topHere - hgt < 0.9f || ((edgeL <= hgt || edgeR <= hgt) && hgt < topHere)) c = mix(c, rgb(255, 150, 120), layer == 2 ? 170 : 110);
                    // bands of light on some floors, a neon strip up the middle of others, a few lit windows
                    if (bands && std::fmod(hgt, 5.f) < 0.6f) c = mix(c, rgb(255, 180, 110), 90);
                    if (strip && std::fabs((float(px) + 0.5f) / k - tw.xc) < 0.5f && hgt < topHere - 3.f) c = mix(c, neon, 170);
                    const uint32_t hh = hash32(uint32_t(px) * 31u + uint32_t(py) * 977u + tw.seed);
                    if (hh % 47u == 0u && hgt < topHere - 2.f) c = (hh >> 9) % 4u == 0u ? rgb(255, 110, 60) : rgb(255, 200, 130);
                    t.put(px, py, c, int(cov * 255.f));
                }
            }
            // an antenna with a red light on the tall ones; a halo ring round the twin spires
            if (tw.kind == 0 || tw.kind == 3) {
                const float ax = tw.xc + (tw.kind == 3 ? tw.w * 0.225f : 0.f), top = topOf(tw, ax);
                if (top > 30.f && (tw.seed >> 11) % 3u == 0u) {
                    t.line(ax * k, hr - top * k, ax * k, hr - (top + 5.f) * k, rgb(60, 36, 70), 255);
                    t.glow(ax * k, hr - (top + 5.f) * k, std::max(1.5f, 1.3f * k), rgb(255, 50, 60), 220);
                }
                if (tw.kind == 3 && layer > 0)
                    for (int a = 0; a < 90; ++a) {
                        const float an = float(a) / 90.f * 2.f * kPi;
                        const float rx = tw.w * 0.7f, ry = 2.f;
                        if (std::sin(an) < 0.f && std::fabs(std::cos(an)) < 0.75f) continue;  // the back half hides behind the spires
                        t.put(int((tw.xc + std::cos(an) * rx) * k), int(hr - (tw.h * 0.55f + std::sin(an) * ry) * k), mix(base, neon, 200), 220);
                    }
            }
        }
        // sky-bridges between neighbouring near towers
        if (layer == 1)
            for (size_t i = 0; i + 1 < towers.size(); ++i) {
                const Tower& a = towers[i];
                const Tower& b = towers[i + 1];
                if (a.layer != 1 || b.layer != 1 || (a.seed & 3u) != 0u) continue;
                const float hb = std::min(topOf(a, a.xc), topOf(b, b.xc)) * 0.6f;
                if (hb < 10.f) continue;
                t.rect((a.xc + a.w * 0.4f) * k, hr - hb * k, (b.xc - b.w * 0.4f) * k, hr - (hb - 1.2f) * k, mix(base, rgb(255, 190, 120), 60));
            }
    }
}

// --- the static world (sky with clouds and light shafts, the ruined skyline, the ground with craters and rubble)
void makeWorld() {
    const float k = g_art.k;
    const int W = g_art.w, H = int((kH + kDrop) * k) + 2;
    Tgt& t = g_art.world;
    t.init(W, H, false);
    const float Wg = g_W;
    Rng r(0x7011u);
    for (int y = 0; y < H; ++y) {
        const float wy = float(y) / k - kDrop;  // world y (game units at pan 1)
        for (int x = 0; x < W; ++x) {
            const float gx = (float(x) + 0.5f) / k;
            uint32_t c;
            if (wy < kYh) {
                c = skyAt(wy);
                const float dy = kYh - wy;
                // strata of cloud lit from below by the dusk
                if (dy > 14.f && dy < 330.f) {
                    const float n = fbm(gx * 0.004f, wy * 0.045f, 1.3f, 5);
                    const float band = std::sin(dy * 0.052f) * 0.5f + 0.5f;
                    const float cl = smooth01((n * 0.8f + band * 0.35f - 0.62f) * 4.f) * clamp01((330.f - dy) / 120.f);
                    if (cl > 0.f) {
                        const uint32_t lit = lerpc(rgb(255, 150, 110), rgb(150, 70, 150), clamp01(dy / 220.f));
                        const uint32_t shadow = lerpc(rgb(110, 40, 90), rgb(36, 14, 56), clamp01(dy / 260.f));
                        const float under = clamp01(fbm(gx * 0.004f, (wy + 3.f) * 0.045f, 1.3f, 5) - n + 0.5f);
                        c = lerpc(c, lerpc(shadow, lit, under), cl * 0.85f);
                    }
                }
                // shafts of light slanting down from the upper right
                const float u = gx * 0.86f + wy * 0.5f;
                const float shaft = std::max(0.f, std::sin(u * 0.021f) * std::sin(u * 0.0067f + 1.f)) * clamp01((dy - 30.f) / 200.f);
                if (shaft > 0.f) c = mix(c, rgb(214, 170, 236), int(shaft * 26.f));
                // the glow low along the horizon
                if (dy < 40.f) c = mix(c, rgb(255, 170, 110), int((1.f - dy / 40.f) * (1.f - dy / 40.f) * 90.f));
            } else {
                // the field: dead gray ground, lit from the dusk far off, with its texture in perspective
                const float s = std::max(0.004f, (wy - kYh) / (kH - kYh));
                const float X = (gx - Wg * 0.5f) / (s * Wg * 0.62f);
                const float Z = 1.f / s;
                const float n = fbm(X * 2.2f, Z * 0.9f, 4.4f, 5);
                const float fine = vnoise(X * 14.f, Z * 6.f, 2.f);
                uint32_t base = lerpc(rgb(150, 98, 112), rgb(88, 82, 94), clamp01(s / 0.35f));
                base = lerpc(base, rgb(54, 50, 60), clamp01((s - 0.35f) / 0.65f));
                c = scalec(base, 0.78f + 0.4f * n + 0.12f * (fine - 0.5f));
                // stones and specks, fewer far away
                const uint32_t hh = hash32(uint32_t(int(X * 60.f) * 7919) ^ uint32_t(int(Z * 30.f) * 104729));
                if (hh % 23u == 0u && s > 0.15f) c = scalec(c, (hh >> 8) & 1u ? 1.35f : 0.6f);
                // the haze close to the horizon
                c = mix(c, kHaze, int(clamp01((0.25f - s) / 0.25f) * 170.f));
            }
            t.px[size_t(y) * size_t(W) + size_t(x)] = c;
        }
    }
    // craters (flat): a dark bowl, its far wall in shadow, a raised lit rim
    for (const Crater& cr : g.craters) {
        const float cxg = screenX(cr.X, cr.Y), cyg = cr.Y;
        const float rx = cr.r * (cr.Y - kYh) * (Wg * 0.62f) / (kH - kYh), ry = rx * 0.34f;
        const float acx = cxg * k, acy = (cyg + kDrop) * k, arx = rx * k, ary = ry * k;
        for (int y = int(acy - ary * 1.3f); y <= int(acy + ary * 1.3f); ++y)
            for (int x = int(acx - arx * 1.3f); x <= int(acx + arx * 1.3f); ++x) {
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                const float dx = (float(x) - acx) / arx, dy = (float(y) - acy) / ary;
                const float d = std::sqrt(dx * dx + dy * dy);
                uint32_t& p = t.px[size_t(y) * size_t(W) + size_t(x)];
                if (d < 0.86f) {
                    const float depth = 1.f - d / 0.86f;
                    p = mix(p, dy < 0.f ? rgb(26, 22, 34) : rgb(56, 46, 58), int(110.f + 110.f * depth));
                } else if (d < 1.22f) {
                    const float rimT = 1.f - std::fabs(d - 1.f) / 0.22f;
                    p = mix(p, dy < 0.f ? rgb(170, 120, 120) : rgb(112, 104, 116), int(rimT * 150.f));
                }
            }
    }
    drawSkyline(t, Wg);
    // smoke rising from the city: soft dark columns leaning with the wind
    for (int i = 0; i < 7; ++i) {
        const float sx = r.r(0.f, Wg), base = r.r(10.f, 40.f), tall = r.r(120.f, 220.f);
        for (int y = 0; y < H; ++y) {
            const float wy = float(y) / k - kDrop, up = (kYh - base) - wy;
            if (up < 0.f || up > tall) continue;
            const float cxs = sx + up * 0.35f + std::sin(up * 0.05f + float(i)) * 6.f;
            const float wdt = 4.f + up * 0.18f;
            for (int x = std::max(0, int((cxs - wdt) * k)); x < std::min(W, int((cxs + wdt) * k)); ++x) {
                const float d = std::fabs((float(x) / k - cxs) / wdt);
                const float n = fbm(float(x) / k * 0.08f, wy * 0.08f, float(i), 3);
                const float a = (1.f - d * d) * (1.f - up / tall) * clamp01(n * 1.6f - 0.3f);
                if (a > 0.f) {
                    uint32_t& p = t.px[size_t(y) * size_t(W) + size_t(x)];
                    p = mix(p, rgb(52, 34, 62), int(a * 150.f));
                }
            }
        }
    }
    // ruins across the far field: wall stumps, pillars and girders fading into the haze
    Rng fr(0xFA11u);
    for (int i = 0; i < 160; ++i) {
        const float Y = kYh + 3.f + (kFarY + 30.f - kYh) * fr.f() * fr.f();
        const float X = fr.r(-1.f, 1.f) * (Wg * 0.5f + 20.f) / std::max(1.f, (Y - kYh) * (Wg * 0.62f) / (kH - kYh));
        const float mh = std::max(1.f, kManH * depthScale(Y) * k);
        const float ww = mh * fr.r(0.5f, 3.5f), hh = mh * fr.r(0.6f, 2.6f);
        const float sxp = screenX(X, Y) * k, syp = (Y + kDrop) * k;
        const uint32_t col = mix(fr.f() < 0.5f ? rgb(78, 72, 88) : rgb(58, 52, 68), kHaze, int(fogOf(Y) * 255.f));
        const int kind = fr.i(3);
        if (kind == 0) {  // a wall stump with a ragged top
            for (int x = int(sxp - ww * 0.5f); x < int(sxp + ww * 0.5f); ++x) {
                const float top = hh * (0.5f + 0.5f * std::fabs(std::sin(float(x) * 0.7f + float(i))));
                t.rect(float(x), syp - top, float(x) + 1.f, syp, col);
                t.put(x, int(syp - top), mix(col, rgb(240, 150, 120), 110));
            }
        } else if (kind == 1) {  // a pillar
            t.rect(sxp - mh * 0.15f, syp - hh * 1.3f, sxp + mh * 0.15f, syp, col);
            t.rect(sxp - mh * 0.15f, syp - hh * 1.3f, sxp + mh * 0.15f, syp - hh * 1.3f + 1.f, mix(col, rgb(240, 150, 120), 110));
        } else {  // a bent girder
            t.line(sxp - ww * 0.4f, syp, sxp + ww * 0.1f, syp - hh, col);
            t.line(sxp + ww * 0.1f, syp - hh, sxp + ww * 0.5f, syp - hh * 0.7f, col);
        }
    }
    // rubble strewn about: chips of concrete and dark scraps
    Rng rr(0xB0B0u);
    for (int i = 0; i < 900; ++i) {
        const float Y = rr.r(kYh + 6.f, kH + 10.f), X = rr.r(-1.3f, 1.3f) * (1.f + (1.f - depthScale(Y)) * 1.5f);
        const float sz = std::max(0.6f, kManH * depthScale(Y) * rr.r(0.03f, 0.1f) * k);
        const uint32_t c = rr.f() < 0.6f ? rgb(118, 112, 126) : rgb(40, 36, 46);
        t.disc(screenX(X, Y) * k, (Y + kDrop) * k, sz, mix(c, kHaze, int(fogOf(Y) * 255.f)), 255, false, 0.6f);
    }
}

// --- a bombardment's crater printed into the ground
void printCrater(float X, float Y, float rField) {
    const float k = g_art.k;
    Tgt& t = g_art.world;
    const float rx = rField * (Y - kYh) * (g_W * 0.62f) / (kH - kYh), ry = rx * 0.34f;
    const float acx = screenX(X, Y) * k, acy = (Y + kDrop) * k, arx = rx * k, ary = ry * k;
    for (int y = int(acy - ary * 2.2f); y <= int(acy + ary * 2.2f); ++y)
        for (int x = int(acx - arx * 2.2f); x <= int(acx + arx * 2.2f); ++x) {
            if (x < 0 || y < 0 || x >= t.w || y >= t.h) continue;
            const float dx = (float(x) - acx) / arx, dy = (float(y) - acy) / ary;
            const float d = std::sqrt(dx * dx + dy * dy);
            uint32_t& p = t.px[size_t(y) * size_t(t.w) + size_t(x)];
            if (d < 0.8f) p = mix(p, rgb(20, 14, 20), int(200.f - 80.f * d));
            else if (d < 1.15f) p = mix(p, rgb(120, 84, 80), int((1.f - std::fabs(d - 0.97f) / 0.18f) * 160.f));
            else if (d < 2.2f) p = mix(p, rgb(30, 24, 30), int((2.2f - d) / 1.05f * 110.f));  // the scorch around it
        }
}

// ------------------------------------------------------------------ the battle
Man* manById(uint32_t id) {
    for (Man& m : g.men)
        if (m.id == id) return &m;
    return nullptr;
}
Mech* mechById(uint32_t id) {
    for (Mech& m : g.mechs)
        if (m.id == id) return &m;
    return nullptr;
}
int aliveCount(int side) {
    int n = 0;
    for (const Man& m : g.men)
        if (m.side == side && m.st != St::Dying) ++n;
    return n;
}
float edgeX(float Y, int side) {  // just off the screen's edge on that row (or far out in the haze)
    const float e = fieldXAt(side == 0 ? -12.f : g_W + 12.f, Y);
    return side == 0 ? std::max(e, -2.4f) : std::min(e, 2.4f);
}

// a cover spot behind a prop on that side, if one is free
int findCover(int side, float Y, Rng& r) {
    int best = -1;
    float bestD = 1e9f;
    for (int i = 0; i < int(g.props.size()); ++i) {
        const Prop& p = g.props[size_t(i)];
        if (p.kind == 5 && g.clock - p.made < 1.f) continue;
        const bool mine = side == 0 ? (p.X < -0.04f && p.X > -0.95f) : (p.X > 0.04f && p.X < 0.95f);
        if (!mine) continue;
        int users = 0;
        for (const Man& m : g.men)
            if (m.prop == i && m.st != St::Dying) ++users;
        if (users >= (p.kind == 3 ? 4 : 2 + int(p.hw * 12.f))) continue;
        const float d = std::fabs(p.Y - Y) + r.r(0.f, 40.f);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return bestD < 70.f ? best : -1;
}

void spawnMan(int side, Rng& r, bool anywhere = false) {
    Man m{};
    m.side = side;
    m.Y = kFarY + 6.f + (kNearY - kFarY - 10.f) * r.f();  // (spread through the depth, owner 13:24)
    m.hp = r.f() < 0.45f ? 2 : 1;
    m.st = St::Run;
    m.prop = findCover(side, m.Y, r);
    const float s = side == 0 ? -1.f : 1.f;
    if (m.prop >= 0) {
        const Prop& p = g.props[size_t(m.prop)];
        m.Y = p.Y - r.r(1.5f, 4.f);
        m.tx = p.X + r.r(-0.6f, 0.6f) * p.hw;
    } else {
        m.tx = s * r.r(0.08f, 0.7f) * (1.f + (1.f - depthScale(m.Y)) * 0.8f);
    }
    m.X = anywhere ? m.tx + s * r.r(0.f, 0.3f) : edgeX(m.Y, side) + s * r.r(0.f, 0.15f);
    m.fire = r.r(0.5f, 2.5f);
    m.phase = r.r(0.f, 6.28f);
    m.speed = r.r(0.11f, 0.17f) * (1.f + (1.f - depthScale(m.Y)) * 0.9f);
    m.born = g.clock;
    m.holdFor = r.r(6.f, 18.f);
    m.id = g.nextId++;
    g.men.push_back(m);
}

void spawnMech(int side, Rng& r) {
    Mech m{};
    m.side = side;
    m.Y = r.r(300.f, 420.f);
    const float s = side == 0 ? -1.f : 1.f;
    m.X = edgeX(m.Y, side) + s * 0.1f;
    m.tx = s * r.r(0.34f, 0.6f);
    m.hp = side == 0 ? 46 : 54;
    m.st = St::Run;
    m.fire = r.r(1.f, 2.5f);
    m.phase = r.r(0.f, 6.28f);
    m.born = g.clock;
    m.id = g.nextId++;
    g.mechs.push_back(m);
}

// the point a man is hit at (his chest), game units at pan 1
void chestOf(const Man& m, float& x, float& y) {
    x = screenX(m.X, m.Y);
    const float crouch = (m.prop >= 0 && m.st == St::Hold) ? 0.62f + 0.38f * m.up : 1.f;
    y = m.Y - manH(m.Y) * 0.62f * crouch;
}
void muzzleOf(const Man& m, float& x, float& y) {
    chestOf(m, x, y);
    x += (m.side == 0 ? 1.f : -1.f) * manH(m.Y) * 0.4f;
    y += manH(m.Y) * 0.02f;
}
float mechH(const Mech& m) { return manH(m.Y) * (m.side == 0 ? 2.3f : 2.9f); }

void addPart(float x, float y, float vx, float vy, float life, float size, uint32_t col, uint8_t kind, float floorY = 1e9f) {
    if (g.parts.size() > 1400) return;
    Part p{x, y, vx, vy, 0.f, life, size, col, kind, floorY};
    g.parts.push_back(p);
}

void killMan(Man& m, float pushX, Rng& r) {
    if (m.st == St::Dying) return;
    m.st = St::Dying;
    m.t = 0.f;
    m.phase = pushX;  // (reused: which way he falls)
    const float x = screenX(m.X, m.Y), y = m.Y - manH(m.Y) * 0.5f;
    const uint32_t c = m.side == 0 ? rgb(160, 30, 40) : rgb(70, 110, 220);
    for (int i = 0; i < 5; ++i) addPart(x, y, r.r(-14.f, 14.f) + pushX * 10.f, r.r(-20.f, -4.f), 0.45f, 0.6f, c, 2, m.Y);
}

void fireAt(Man& m, Rng& r) {
    // a target: a living enemy, most likely one about as far away (with a wider look now and then); a mech sometimes
    std::vector<int> cand;
    for (int i = 0; i < int(g.men.size()); ++i) {
        const Man& o = g.men[size_t(i)];
        if (o.side == m.side || o.st == St::Dying) continue;
        if (std::fabs(o.Y - m.Y) < 60.f || r.f() < 0.15f) cand.push_back(i);
    }
    Mech* mt = nullptr;
    if (r.f() < 0.18f)
        for (Mech& me : g.mechs)
            if (me.side != m.side && me.st != St::Dying && std::fabs(me.Y - m.Y) < 90.f) mt = &me;
    float x0, y0;
    muzzleOf(m, x0, y0);
    Shot s{};
    s.x0 = x0;
    s.y0 = y0;
    s.kind = m.side == 0 ? 0 : 1;
    s.dmg = 1.f;
    if (mt) {
        s.x1 = screenX(mt->X, mt->Y) + r.r(-0.3f, 0.3f) * mechH(*mt);
        s.y1 = mt->Y - mechH(*mt) * r.r(0.4f, 0.85f);
        s.mechT = true;
        s.target = int(mt->id);
        s.hit = r.f() < 0.6f;
        s.dmg = 0.35f;
    } else if (!cand.empty()) {
        Man& o = g.men[size_t(cand[size_t(r.i(int(cand.size())))])];
        chestOf(o, s.x1, s.y1);
        const bool cover = o.prop >= 0 && o.st == St::Hold;
        // (the plasma bolts are fewer but heavier: the two sides kill about as fast)
        const float odds = std::max(0.7f, std::min(1.8f, float(g.alive[1 - m.side] + 2) / float(g.alive[m.side] + 2)));
        const float p = (o.st == St::Run ? 0.2f : cover ? 0.07f + 0.1f * o.up : 0.17f) * (m.side == 1 ? 1.6f : 1.f) * odds;
        s.hit = r.f() < p;
        s.target = int(o.id);
        if (!s.hit) {  // a miss: wide or into the ground near him
            s.x1 += r.r(-1.f, 1.f) * manH(o.Y) * 1.2f;
            s.y1 += r.r(-0.6f, 0.9f) * manH(o.Y);
        }
    } else {
        return;
    }
    const float d = std::sqrt((s.x1 - s.x0) * (s.x1 - s.x0) + (s.y1 - s.y0) * (s.y1 - s.y0));
    s.dur = d / (s.kind == 0 ? 520.f : 300.f) + 0.02f;
    g.shots.push_back(s);
    ++g.shotsFired;
}

void stepMen(Rng& r) {
    for (Man& m : g.men) {
        m.t += kDt;
        const float dir = m.side == 0 ? 1.f : -1.f;
        if (m.st == St::Dying) continue;
        if (m.st == St::Run) {
            m.X += dir * m.speed * kDt;
            m.phase += kDt * 11.f;
            if ((dir > 0 && m.X >= m.tx) || (dir < 0 && m.X <= m.tx)) {
                m.X = m.tx;
                m.st = St::Hold;
                m.t = 0.f;
            }
            // a shot on the run now and then
            m.fire -= kDt * 0.35f;
        } else {
            m.fire -= kDt;
            // move up now and then: to cover farther forward, or out into the open
            if (m.t > m.holdFor && r.f() < 0.02f) {
                m.prop = findCover(m.side, m.Y, r);
                if (m.prop >= 0) {
                    const Prop& p = g.props[size_t(m.prop)];
                    if ((m.side == 0 && p.X > m.X) || (m.side == 1 && p.X < m.X)) {
                        m.tx = p.X + r.r(-0.6f, 0.6f) * p.hw;
                        m.Y = std::max(kFarY, p.Y - r.r(1.5f, 4.f));
                    } else {
                        m.prop = -1;
                        m.tx = m.X + dir * r.r(0.05f, 0.15f);
                    }
                } else {
                    m.tx = m.X + dir * r.r(0.04f, 0.14f);
                }
                if (dir * m.tx > -0.03f) m.tx = -dir * 0.03f;  // nobody crosses the middle
                m.st = St::Run;
                m.t = 0.f;
                m.holdFor = r.r(6.f, 18.f);
            }
        }
        // standing up out of cover to fire, crouching again after
        const bool firing = m.burst > 0 || m.fire < 0.35f;
        m.up = std::max(0.f, std::min(1.f, m.up + (firing ? kDt * 6.f : -kDt * 2.5f)));
        if (m.burst > 0) {
            m.burstT -= kDt;
            if (m.burstT <= 0.f) {
                fireAt(m, r);
                --m.burst;
                m.burstT = m.side == 0 ? 0.11f : 0.24f;
            }
        } else if (m.fire <= 0.f) {
            m.burst = m.side == 0 ? 2 + r.i(2) : 1 + r.i(2);
            m.burstT = 0.f;
            m.fire = r.r(2.4f, 4.6f);
        }
    }
    // the fallen: from Dying to bodies on the ground
    for (size_t i = 0; i < g.men.size();) {
        Man& m = g.men[i];
        if (m.st == St::Dying && m.t > 0.45f) {
            g.bodies.push_back({m.X, m.Y, m.side, g.clock, m.id * 2654435761u, m.phase >= 0.f ? 1 : -1, nullptr, 0.f, 0, 0});
            g.men[i] = g.men.back();
            g.men.pop_back();
        } else {
            ++i;
        }
    }
    // bodies stay over three minutes (owner), then fade and go
    while (!g.bodies.empty() && g.clock - g.bodies.front().t0 > 230.f) g.bodies.erase(g.bodies.begin());
}

void stepMechs(Rng& r) {
    for (Mech& m : g.mechs) {
        m.t += kDt;
        const float dir = m.side == 0 ? 1.f : -1.f;
        if (m.st == St::Dying) continue;
        if (m.st == St::Run) {
            m.X += dir * 0.05f * kDt;
            m.phase += kDt * 4.f;
            if ((dir > 0 && m.X >= m.tx) || (dir < 0 && m.X <= m.tx)) m.st = St::Hold;
        }
        // its target: the enemy's mech if there is one, else a man near its row
        Mech* em = nullptr;
        for (Mech& o : g.mechs)
            if (o.side != m.side && o.st != St::Dying) em = &o;
        const float mx = screenX(m.X, m.Y), my = m.Y - mechH(m) * (m.side == 0 ? 0.52f : 0.72f);
        if (m.side == 0) {  // the Gatling laser: bursts of red bolts
            m.fire -= kDt;
            if (m.shots > 0) {
                m.shotT -= kDt;
                if (m.shotT <= 0.f) {
                    m.shotT = 0.07f;
                    --m.shots;
                    Shot s{};
                    s.kind = 2;
                    s.x0 = mx + mechH(m) * 0.45f;
                    s.y0 = my + mechH(m) * 0.05f;
                    if (em) {
                        s.x1 = screenX(em->X, em->Y) + r.r(-0.25f, 0.25f) * mechH(*em);
                        s.y1 = em->Y - mechH(*em) * r.r(0.45f, 0.85f);
                        s.mechT = true;
                        s.target = int(em->id);
                        s.hit = r.f() < 0.75f;
                        s.dmg = 1.f;
                    } else {
                        std::vector<Man*> cand;
                        for (Man& o : g.men)
                            if (o.side != m.side && o.st != St::Dying && std::fabs(o.Y - m.Y) < 80.f) cand.push_back(&o);
                        if (cand.empty()) continue;
                        Man& o = *cand[size_t(r.i(int(cand.size())))];
                        chestOf(o, s.x1, s.y1);
                        s.target = int(o.id);
                        s.hit = r.f() < 0.3f;
                        s.dmg = 1.f;
                        if (!s.hit) s.x1 += r.r(-1.f, 1.f) * manH(o.Y);
                    }
                    const float d = std::hypot(s.x1 - s.x0, s.y1 - s.y0);
                    s.dur = d / 640.f + 0.02f;
                    g.shots.push_back(s);
                    ++g.shotsFired;
                }
            } else if (m.fire <= 0.f) {
                m.shots = 16;
                m.shotT = 0.f;
                m.fire = r.r(2.2f, 3.4f);
            }
        } else {  // the enemy's walker: a charge, then a blue beam for 3 seconds that sweeps
            // (owner 14:36: "tone down the beam weapon on the enemy mech from continuous to 3 seconds at a time" - its end used to
            // leave the timer a hair below 0, read as "charging", so it fired again at once and never stopped)
            if (m.beamT > 0.f) {
                m.beamT -= kDt;
                if (m.btarget) {  // its walker blown up: the beam goes out a moment later
                    const Mech* tg = nullptr;
                    for (const Mech& o : g.mechs)
                        if (o.id == m.btarget) tg = &o;
                    if (!tg || (tg->st == St::Dying && tg->t > 0.25f)) m.beamT = std::min(m.beamT, 0.f);
                }
                if (m.beamT <= 0.f) {
                    m.beamT = 0.f;
                    g.beamGap = r.r(3.2f, 4.8f);  // then the field is quiet for a while
                }
                m.bx += m.bdx * kDt;
                // what the beam touches burns: a mech takes damage, men near where it lands die
                if (em && std::fabs(m.bx - screenX(em->X, em->Y)) < mechH(*em) * 0.4f && std::fabs(m.by - (em->Y - mechH(*em) * 0.5f)) < mechH(*em) * 0.6f) {
                    em->hp -= int(r.f() < 0.55f ? 1 : 0);
                }
                for (Man& o : g.men) {
                    if (o.side == m.side || o.st == St::Dying) continue;
                    float ox, oy;
                    chestOf(o, ox, oy);
                    if (std::fabs(ox - m.bx) < manH(o.Y) * 0.5f && std::fabs(o.Y - m.by) < 14.f && r.f() < 0.25f) killMan(o, -1.f, r);
                }
                if (r.f() < 0.6f) addPart(m.bx, m.by, r.r(-30.f, 30.f), r.r(-40.f, -5.f), 0.4f, 0.7f, rgb(150, 210, 255), 3, m.by + 6.f);
            } else if (m.beamT < 0.f) {
                m.beamT += kDt;  // charging
                if (m.beamT >= 0.f) m.beamT = 3.f;
            } else {
                m.fire -= kDt;
                bool busy = g.beamGap > 0.f;  // one beam at a time on the field
                for (const Mech& o : g.mechs)
                    if (&o != &m && o.side == 1 && o.st != St::Dying && o.beamT != 0.f) busy = true;
                if (m.fire <= 0.f && !busy) {
                    m.fire = r.r(4.f, 6.f);
                    m.beamT = -0.45f;
                    m.btarget = em ? em->id : 0u;
                    if (em) {
                        m.bx = screenX(em->X, em->Y) - mechH(*em) * 0.3f;
                        m.by = em->Y - mechH(*em) * 0.55f;
                        m.bdx = mechH(*em) * 0.5f;
                    } else {
                        std::vector<Man*> cand;
                        for (Man& o : g.men)
                            if (o.side != m.side && o.st != St::Dying) cand.push_back(&o);
                        if (cand.empty()) {
                            m.beamT = 0.f;
                            continue;
                        }
                        Man& o = *cand[size_t(r.i(int(cand.size())))];
                        m.by = o.Y - manH(o.Y) * 0.4f;
                        m.bx = screenX(o.X, o.Y) + manH(o.Y) * 2.f;
                        m.bdx = -manH(o.Y) * r.r(2.f, 4.f);
                    }
                }
            }
        }
        if (m.hp <= 0) {
            m.st = St::Dying;
            m.t = 0.f;
            m.beamT = 0.f;
            ++g.mechsLost[m.side];
            // it blows apart: sparks and burning shards thrown wide, armour plates that fall back to the ground, a column of smoke
            const float h = mechH(m), x = screenX(m.X, m.Y), y = m.Y - h * 0.5f;
            for (int i = 0; i < 90; ++i) {
                const float a = r.r(-kPi, 0.f), v = r.r(40.f, 190.f);
                addPart(x + r.r(-8.f, 8.f), y + r.r(-8.f, 8.f), std::cos(a) * v, std::sin(a) * v, r.r(0.5f, 1.5f), r.r(0.6f, 1.8f),
                        i % 4 == 0 ? rgb(255, 250, 220) : i % 4 == 1 ? rgb(255, 170, 70) : i % 4 == 2 ? rgb(255, 110, 40) : rgb(70, 60, 66),
                        i % 4 == 3 ? 2 : i % 4 == 2 ? 5 : 3, m.Y + r.r(-5.f, 5.f));
            }
            for (int i = 0; i < 26; ++i)  // the hull's plates
                addPart(x + r.r(-h * 0.3f, h * 0.3f), y + r.r(-h * 0.3f, h * 0.2f), r.r(-70.f, 70.f), r.r(-130.f, -40.f), r.r(1.2f, 2.4f),
                        r.r(1.2f, 2.4f), i % 2 ? rgb(84, 82, 96) : rgb(140, 120, 110), 2, m.Y + r.r(-6.f, 6.f));
            for (int i = 0; i < 22; ++i) addPart(x + r.r(-12.f, 12.f), y, r.r(-10.f, 10.f), r.r(-26.f, -8.f), r.r(4.f, 8.f), r.r(4.f, 10.f), rgb(60, 50, 64), 1);
            g.fires.push_back({x, m.Y, h * 0.34f, g.clock + 45.f});
            g.booms.push_back({x, m.Y, h, 0.f, 0, m.id * 2654435761u});
            if (std::getenv("TI4_TW_DEBUG")) std::printf("title war: walker %d blows up at %.2f s, x %.0f y %.0f h %.0f\n", m.side, g.clock, x, m.Y, h);
        }
    }
    for (size_t i = 0; i < g.mechs.size();) {
        Mech& m = g.mechs[i];
        if (m.st == St::Dying && m.t > 1.3f) {  // (it buckles while the secondary blasts go off, then falls)
            Prop p;
            p.X = m.X;
            p.Y = m.Y;
            p.hw = 0.07f + 0.04f * depthScale(m.Y);
            p.kind = 5;
            p.seed = m.id * 2246822519u + uint32_t(m.side);
            p.made = g.clock;
            if (g_art.k > 0.f) makeProp(p);
            g.props.push_back(std::move(p));
            g.mechs[i] = g.mechs.back();
            g.mechs.pop_back();
        } else {
            ++i;
        }
    }
    // old wrecks are cleared away (the field keeps at most six)
    int wrecks = 0;
    for (const Prop& p : g.props) wrecks += p.kind == 5;
    if (wrecks > 6)
        for (size_t i = 0; i < g.props.size(); ++i)
            if (g.props[i].kind == 5) {
                for (Man& m : g.men)
                    if (m.prop == int(i)) m.prop = -1;
                    else if (m.prop > int(i)) --m.prop;
                g.props.erase(g.props.begin() + long(i));
                break;
            }
}

void stepShots(Rng& r) {
    for (size_t i = 0; i < g.shots.size();) {
        Shot& s = g.shots[i];
        s.t += kDt;
        if (s.t < s.dur) {
            ++i;
            continue;
        }
        // it lands
        bool struck = false;
        if (s.hit) {
            if (s.mechT) {
                if (Mech* m = mechById(uint32_t(s.target))) {
                    if (m->st != St::Dying) {
                        m->hp -= s.kind == 2 ? 1 : (r.f() < s.dmg ? 1 : 0);
                        struck = true;
                    }
                }
            } else if (Man* m = manById(uint32_t(s.target))) {
                if (m->st != St::Dying) {
                    struck = true;
                    if (--m->hp <= 0) killMan(*m, s.x1 > s.x0 ? 1.f : -1.f, r);
                }
            }
        }
        const uint32_t col = s.kind == 1 ? rgb(120, 200, 255) : rgb(255, 90, 90);
        for (int k = 0; k < (struck ? 4 : 2); ++k) addPart(s.x1, s.y1, r.r(-26.f, 26.f), r.r(-30.f, 6.f), 0.22f, 0.6f, col, 3, s.y1 + 8.f);
        if (!struck && r.f() < 0.4f) addPart(s.x1, s.y1, r.r(-4.f, 4.f), r.r(-8.f, -2.f), 0.8f, 1.6f, rgb(110, 100, 112), 4);
        g.shots[i] = g.shots.back();
        g.shots.pop_back();
    }
}

void stepBlasts(Rng& r) {
    g.bombT -= kDt;
    if (g.bombT <= 0.f) {
        g.bombT = r.r(30.f, 48.f);
        Blast b{};
        // (0.60.0, owner 15:32: "land randomly ... indiscriminately instead of just one side") anywhere across the field - the
        // lines where both armies meet too
        b.Y = r.r(300.f, 440.f);
        b.X = r.r(-0.95f, 0.95f) * (g_W > 700.f ? 1.15f : 1.f);
        b.x = screenX(b.X, b.Y);
        b.y = b.Y;
        b.r = r.r(0.17f, 0.24f);
        b.stage = 0;
        g.blasts.push_back(b);
        ++g.bombs;
    }
    for (size_t i = 0; i < g.blasts.size();) {
        Blast& b = g.blasts[i];
        b.age += kDt;
        if (b.stage == 0 && b.age > 2.4f) {  // the payload falls
            b.stage = 1;
        }
        if (b.stage == 1 && b.age > 2.9f) {  // the impact
            b.stage = 2;
            const float rx = b.r * (b.Y - kYh) * (g_W * 0.62f) / (kH - kYh);
            for (Man& m : g.men) {
                if (m.st == St::Dying) continue;
                const float dx = (m.X - b.X) / b.r, dy = (m.Y - b.Y) / (rx * 0.34f * 1.6f);
                if (dx * dx + dy * dy < 1.f) killMan(m, m.X < b.X ? -1.f : 1.f, r);
            }
            for (Mech& m : g.mechs) {
                const float dx = (m.X - b.X) / b.r, dy = (m.Y - b.Y) / (rx * 0.6f);
                if (dx * dx + dy * dy < 1.f) m.hp -= 30;
            }
            for (int k = 0; k < 110; ++k) {  // debris and fire thrown up
                const float a = r.r(-kPi, 0.f), v = r.r(40.f, 220.f);
                addPart(b.x + r.r(-rx, rx) * 0.4f, b.y - r.r(0.f, 10.f), std::cos(a) * v, std::sin(a) * v * 1.2f, r.r(0.8f, 2.2f), r.r(0.6f, 1.8f),
                        k % 3 == 0 ? rgb(255, 190, 90) : k % 3 == 1 ? rgb(70, 60, 64) : rgb(130, 110, 110), k % 3 == 0 ? 5 : 2, b.y + r.r(-rx * 0.3f, rx * 0.3f));
            }
            for (int k = 0; k < 30; ++k)  // the cloud boiling up into a column
                addPart(b.x + r.r(-rx, rx) * 0.4f, b.y - r.r(0.f, 30.f), r.r(-12.f, 12.f), r.r(-90.f, -30.f), r.r(6.f, 12.f), r.r(10.f, 22.f), rgb(70, 54, 66), 6);
            for (int k = 0; k < 24; ++k) {  // dust thrown out along the ground
                const float a = r.r(0.f, 2.f * kPi);
                addPart(b.x + std::cos(a) * rx * 0.4f, b.y + std::sin(a) * rx * 0.13f, std::cos(a) * r.r(80.f, 160.f), std::sin(a) * r.r(20.f, 50.f), r.r(1.5f, 3.f), r.r(6.f, 12.f), rgb(120, 104, 112), 4);
            }
            g.fires.push_back({b.x, b.y, rx * 0.25f, g.clock + 30.f});
            g.newCraters.push_back(b);
        }
        if (b.age > 7.f) {
            g.blasts[i] = g.blasts.back();
            g.blasts.pop_back();
        } else {
            ++i;
        }
    }
}

// the walkers blowing up: the secondary blasts on the hull as it buckles (at 0.3, 0.6 and 0.95 s), then the booms are dropped
void stepBooms(Rng& r) {
    static const float kAt[3] = {0.3f, 0.6f, 0.95f};
    for (Boom& b : g.booms) {
        b.age += kDt;
        while (b.next < 3 && b.age >= kAt[b.next]) {
            ++b.next;
            const float x = b.x + r.r(-0.3f, 0.3f) * b.h, y = b.y - b.h * r.r(0.25f, 0.75f) + (b.age * b.age * 10.f);
            for (int i = 0; i < 26; ++i) {
                const float a = r.r(-kPi, 0.f), v = r.r(30.f, 120.f);
                addPart(x, y, std::cos(a) * v, std::sin(a) * v, r.r(0.4f, 1.1f), r.r(0.5f, 1.3f), i % 3 ? rgb(255, 180, 80) : rgb(255, 245, 210),
                        i % 3 ? 5 : 3, b.y + r.r(-4.f, 4.f));
            }
            for (int i = 0; i < 6; ++i) addPart(x, y, r.r(-8.f, 8.f), r.r(-18.f, -6.f), r.r(3.f, 6.f), r.r(3.f, 7.f), rgb(64, 54, 66), 1);
        }
    }
    g.booms.erase(std::remove_if(g.booms.begin(), g.booms.end(), [](const Boom& b) { return b.age > 2.4f; }), g.booms.end());
    if (g.beamGap > 0.f) g.beamGap -= kDt;
    for (const Mech& m : g.mechs)
        if (m.side == 1 && m.beamT > 0.f) {
            g.beamOn += kDt;
            break;
        }
}

void stepParts(Rng& r) {
    for (size_t i = 0; i < g.parts.size();) {
        Part& p = g.parts[i];
        p.age += kDt;
        if (p.age >= p.life) {
            g.parts[i] = g.parts.back();
            g.parts.pop_back();
            continue;
        }
        p.x += p.vx * kDt;
        p.y += p.vy * kDt;
        if (p.kind == 2 || p.kind == 5 || p.kind == 3) {  // falling things
            p.vy += 260.f * kDt;
            if (p.y > p.floor) {
                p.y = p.floor;
                p.vy *= -0.25f;
                p.vx *= 0.5f;
            }
        } else if (p.kind == 6) {  // the blast's column: slowing as it rises, spreading into a cap
            p.vy *= 1.f - 0.9f * kDt;
            p.vx += (8.f - p.vx) * kDt * 0.2f;
            p.size += kDt * 6.f;
        } else if (p.kind == 1 || p.kind == 4) {  // smoke and dust: rising, spreading, drifting with the wind
            p.vx += (8.f - p.vx) * kDt * 0.3f;
            p.size += kDt * (p.kind == 1 ? 3.f : 2.f);
        } else if (p.kind == 0) {  // embers: rising and wavering
            p.vx += r.r(-30.f, 30.f) * kDt;
        }
        ++i;
    }
    // the fires burn: embers, flames and smoke
    for (size_t i = 0; i < g.fires.size();) {
        Fire& f = g.fires[i];
        if (g.clock > f.until) {
            g.fires[i] = g.fires.back();
            g.fires.pop_back();
            continue;
        }
        if (r.f() < 0.5f) addPart(f.x + r.r(-f.size, f.size), f.y - r.r(0.f, f.size * 0.5f), r.r(-4.f, 4.f), r.r(-30.f, -12.f), r.r(0.3f, 0.6f), r.r(0.8f, 1.6f) * (0.5f + f.size * 0.08f), rgb(255, 140, 60), 5, f.y + 2.f);
        if (r.f() < 0.25f) addPart(f.x + r.r(-f.size, f.size), f.y - f.size, r.r(-6.f, 6.f), r.r(-50.f, -20.f), r.r(1.f, 2.5f), 0.6f, rgb(255, 170, 90), 0);
        if (r.f() < 0.06f) addPart(f.x, f.y - f.size, r.r(-4.f, 4.f), r.r(-20.f, -8.f), r.r(3.5f, 6.5f), f.size * 0.9f + 2.5f, rgb(64, 52, 66), 1);
        ++i;
    }
}

void step() {
    Rng& r = g.rng;
    g.clock += kDt;
    // reinforcements: each side fills up to fifty (fewer on a small screen), in squads, and the mechs come back
    const int cap = g_W > 0.f && g_art.k > 0.f && g_art.k * float(g_art.blow) < 1.4f ? 34 : 50;
    g.alive[0] = aliveCount(0);
    g.alive[1] = aliveCount(1);
    for (int side = 0; side < 2; ++side) {
        g.spawnT[side] -= kDt;
        if (g.spawnT[side] <= 0.f) {
            const int n = aliveCount(side);
            if (n < cap) {
                const int squad = std::min(cap - n, 2 + r.i(4));
                for (int i = 0; i < squad; ++i) spawnMan(side, r);
            }
            g.spawnT[side] = r.r(0.8f, 2.2f) * std::max(0.45f, std::min(1.2f, float(g.alive[side] + 2) / float(g.alive[1 - side] + 2)));
        }
        g.mechT[side] -= kDt;
        int mechs = 0;
        for (const Mech& m : g.mechs) mechs += m.side == side && m.st != St::Dying;
        if (g.mechT[side] <= 0.f) {
            if (mechs < 2) spawnMech(side, r);
            g.mechT[side] = r.r(9.f, 16.f);
        }
    }
    stepMen(r);
    stepMechs(r);
    stepShots(r);
    stepBlasts(r);
    stepBooms(r);
    stepParts(r);
}

void start() {
    if (g.made) return;
    g.made = true;
    layout();
    // the battle is under way when the title comes up: both sides in place, a field already strewn
    Rng& r = g.rng;
    for (int side = 0; side < 2; ++side)
        for (int i = 0; i < 26; ++i) spawnMan(side, r, true);
    for (int i = 0; i < 26 * 30; ++i) step();
    g.bombT = 10.5f;  // the first bombardment soon after the opening (and every 30-48 s after)
}

// ------------------------------------------------------------------ drawing the frame
float g_lift = 0.f;
struct Override {  // the asset sheet (a unit drawn at a set place and size, game units, no haze) and the bodies' pictures
    bool on = false;
    float x = 0, y = 0, h = 0;
    float fog = 0.f;
} g_ov;
int ay(float gy) { return int(std::floor((gy + g_lift) * g_art.k)); }

// a soldier, in his pose: body parts as rectangles on a 16-unit-tall figure, facing his enemy
void drawMan(Tgt& t, const Man* mp, const Body* bp, float fade) {
    const bool body = bp != nullptr;
    const int side = body ? bp->side : mp->side;
    const float X = body ? bp->X : mp->X, Y = body ? bp->Y : mp->Y;
    const float k = g_art.k;
    const float hpx = (g_ov.on ? g_ov.h : manH(Y)) * k;
    if (hpx < 2.f) return;
    const float q = hpx / 16.f;
    const float fx = (g_ov.on ? g_ov.x : screenX(X, Y)) * k, fy = (g_ov.on ? g_ov.y : Y + g_lift) * k;
    const int dir = side == 0 ? 1 : -1;
    const float fog = g_ov.on ? g_ov.fog : fogOf(Y);
    auto C = [&](uint32_t c) { return fog > 0.f ? mix(c, kHaze, int(fog * 255.f)) : c; };
    const int a = int(255.f * fade);
    const bool tiny = hpx < 7.f;  // far away: only the shapes
    // colours: grey-blue gear and red visors; gilded armour, blue skin and silver hair
    const uint32_t bootC = side == 0 ? C(rgb(38, 44, 58)) : C(rgb(112, 78, 36));
    const uint32_t legC = side == 0 ? C(rgb(74, 88, 112)) : C(rgb(196, 150, 62));
    const uint32_t legD = side == 0 ? C(rgb(56, 66, 86)) : C(rgb(150, 108, 46));
    const uint32_t bodyC = side == 0 ? C(rgb(100, 116, 142)) : C(rgb(214, 168, 70));
    const uint32_t plateC = side == 0 ? C(rgb(146, 160, 184)) : C(rgb(255, 222, 130));
    const uint32_t padC = side == 0 ? C(rgb(128, 142, 168)) : C(rgb(236, 192, 92));
    const uint32_t headC = side == 0 ? C(rgb(118, 132, 156)) : C(rgb(82, 132, 226));
    const uint32_t gunC = side == 0 ? C(rgb(34, 36, 46)) : C(rgb(232, 222, 200));
    const uint32_t hairC = C(rgb(228, 232, 244));
    const uint32_t rimC = rgb(255, 170, 120);  // the dusk behind them catches their top edges
    auto R = [&](float x0, float y0, float x1, float y1, uint32_t c) {  // figure units: x forward, y up from the feet
        const float ax0 = fx + float(dir) * x0 * q, ax1 = fx + float(dir) * x1 * q;
        t.rect(std::min(ax0, ax1), fy - y1 * q, std::max(ax0, ax1), fy - y0 * q, c, a);
    };
    if (body) {  // lying where he fell, limbs flung out, his weapon beside him, a dark pool spreading
        const int d = bp->dir * dir;
        Rng br(bp->seed);
        const float th = br.r(-0.45f, 0.45f);  // how he lies across the ground
        const float ca = std::cos(th) * float(d), sa = std::sin(th) * 0.45f;  // (on the ground: the depth is squashed)
        auto P = [&](float along, float across, float& px, float& py) {  // body axis -> art px
            px = fx + (along * ca - across * 0.45f * std::sin(th) * float(d)) * q;
            py = fy - 0.6f * q + (along * sa + across * 0.4f) * q;
        };
        auto stroke = [&](float a0, float c0, float a1, float c1, float rad, uint32_t col) {
            float x0, y0, x1, y1;
            P(a0, c0, x0, y0);
            P(a1, c1, x1, y1);
            const int n = std::max(1, int(std::hypot(x1 - x0, y1 - y0)));
            for (int i = 0; i <= n; ++i) t.disc(x0 + (x1 - x0) * float(i) / float(n), y0 + (y1 - y0) * float(i) / float(n), std::max(0.6f, rad * q), col, a, false, 0.7f);
        };
        auto D = [&](uint32_t c) { return scalec(c, 0.72f); };  // the dead are dimmer
        const float age = g.clock - bp->t0;
        const float pool = clamp01(age / 8.f);
        float pxp, pyp;
        P(1.f, 0.f, pxp, pyp);
        t.disc(pxp, pyp + 0.3f * q, (2.f + 3.5f * pool) * q, side == 0 ? C(rgb(84, 16, 24)) : C(rgb(36, 48, 118)), int(150.f * fade), false, 0.32f);
        const float lg = br.r(-1.2f, 1.2f), ar = br.r(-2.5f, 2.5f);
        stroke(-0.5f, -0.6f, -6.5f, -0.6f + lg, 0.75f, D(legC));  // legs
        stroke(-0.5f, 0.6f, -6.f, 0.6f - lg * 0.5f, 0.75f, D(legD));
        if (!tiny) {
            stroke(-6.5f, -0.6f + lg, -7.4f, -0.6f + lg, 0.8f, D(bootC));
            stroke(-6.f, 0.6f - lg * 0.5f, -6.9f, 0.6f - lg * 0.5f, 0.8f, D(bootC));
        }
        stroke(-0.5f, 0.f, 4.f, 0.f, 1.25f, D(bodyC));  // torso
        if (!tiny) stroke(0.5f, -0.3f, 3.5f, -0.3f, 0.7f, D(plateC));
        stroke(3.5f, 0.5f, 3.5f + 3.6f, 0.5f + ar, 0.55f, D(side == 0 ? rgb(88, 100, 124) : bodyC));  // an arm flung out
        float hx, hy2;
        P(5.6f, 0.f, hx, hy2);
        if (side == 1) stroke(5.6f, -0.4f, 9.f, br.r(-1.5f, 1.5f), 0.9f, D(hairC));  // silver hair spread on the ground
        t.disc(hx, hy2, std::max(0.8f, 1.35f * q), D(headC), a, false, 0.8f);
        if (side == 0 && !tiny) t.disc(hx + float(d) * 0.6f * q, hy2 + 0.2f * q, std::max(0.5f, 0.6f * q), D(rgb(200, 150, 120)), a);  // the face
        const float gs = br.r(-2.f, 2.f);
        stroke(-2.f + gs, 2.4f + br.f(), 5.f + gs, 2.f + br.f(), 0.45f, D(gunC));  // the weapon beside him
        return;
    }
    const Man& m = *mp;
    // his shadow on the ground
    t.disc(fx, fy, 3.4f * q, rgb(20, 16, 26), int(70.f * fade), false, 0.25f);
    if (m.st == St::Dying) {  // thrown back and down
        const float u = clamp01(m.t / 0.45f);
        const float lean = u * 7.f * -m.phase * float(dir);
        const float hgt = 1.f - 0.82f * u;
        auto Rl = [&](float x0, float y0, float x1, float y1, uint32_t c) {
            const float sh = lean * (y0 + y1) * 0.5f / 16.f;
            R(x0 + sh, y0 * hgt, x1 + sh, y1 * hgt, c);
        };
        Rl(-2.f, 0.f, 2.f, 6.f, legC);
        Rl(-2.f, 6.f, 2.f, 11.5f, bodyC);
        Rl(-1.6f, 11.5f, 1.6f, 15.4f, headC);
        if (side == 1) Rl(-4.f, 9.f, -1.6f, 16.f, hairC);
        return;
    }
    const bool cover = m.prop >= 0 && m.st == St::Hold;
    const float crouch = cover ? 1.f - m.up : 0.f;  // 1 = down behind the cover
    const bool running = m.st == St::Run;
    const float stride = running ? std::sin(m.phase) : 0.f;
    const float drop = crouch * 4.5f;
    const float recoil = (m.burst > 0 && m.burstT > 0.05f) ? -0.5f : 0.f;
    // legs: greaves, knee pads, boots (the far leg darker)
    if (crouch > 0.5f) {  // kneeling
        R(-3.f, 0.f, 1.f, 1.6f, legD);
        R(-3.f, 0.f, -1.6f, 1.2f, bootC);
        R(0.4f, 0.f, 2.6f, 4.f, legC);
        R(0.4f, 0.f, 2.8f, 1.2f, bootC);
    } else {
        const float b = stride * 1.6f, f = -stride * 1.6f;
        const float lb = running ? std::max(0.f, -stride) * 1.4f : 0.f, lf = running ? std::max(0.f, stride) * 1.4f : 0.f;
        R(-2.f + b, lb, -0.4f + b, 6.2f - drop, legD);
        R(-2.3f + b, lb, -0.2f + b, lb + 1.2f, bootC);
        R(0.4f + f, lf, 2.f + f, 6.2f - drop, legC);
        R(0.3f + f, lf, 2.4f + f, lf + 1.2f, bootC);
        if (!tiny) R(0.5f + f, 3.4f + lf * 0.5f, 2.1f + f, 4.4f + lf * 0.5f, padC);  // knee pad
    }
    const float lean = running ? 0.9f : 0.f;
    const float yb = 6.2f - drop;  // the waist
    R(-2.1f + lean, yb, 2.1f + lean, yb + 0.8f, side == 0 ? C(rgb(48, 54, 68)) : C(rgb(120, 30, 40)));  // belt / red sash
    if (side == 1 && !tiny) R(-0.8f + lean, yb - 1.8f, 1.f + lean, yb + 0.6f, C(rgb(120, 30, 40)));    // the tabard hanging
    // torso and plates
    R(-2.1f + lean, yb + 0.8f, 2.1f + lean, yb + 5.5f, bodyC);
    if (!tiny) {
        R(-0.4f + lean, yb + 2.4f, 2.2f + lean, yb + 5.2f, plateC);
        if (side == 0) R(0.4f + lean, yb + 3.4f, 1.6f + lean, yb + 3.8f, rgb(90, 220, 255));  // a neon status strip
    }
    if (side == 0) {
        R(-3.5f + lean, yb + 1.f, -2.1f + lean, yb + 5.2f, C(rgb(70, 80, 98)));  // the pack
        if (!tiny) R(-3.f + lean, yb + 5.2f, -2.8f + lean, yb + 9.8f, C(rgb(40, 44, 54)));  // its antenna
    } else {
        // long silver hair streaming back: strands, longest nearest the head, waving
        for (int st2 = 0; st2 < (tiny ? 2 : 5); ++st2) {
            const float sx2 = -1.4f - float(st2) * (tiny ? 1.2f : 0.7f);
            const float wv = std::sin(g.clock * 6.f + float(m.id) + float(st2) * 0.9f) * (0.3f + 0.25f * float(st2));
            const float len = 8.f - float(st2) * 1.1f;
            R(sx2 + wv - 0.35f, yb + 9.8f - len, sx2 + wv + 0.35f, yb + 9.8f, st2 % 2 ? C(rgb(200, 206, 222)) : hairC);
        }
    }
    R(-1.3f + lean, yb + 4.5f, 1.7f + lean, yb + 6.f, padC);  // the shoulder pad
    // head: a helmet with a glowing red visor; or a blue face under silver hair
    const float hy = yb + 5.6f;
    if (side == 0) {  // a face under a rounded helmet with a brim and a chin strap (owner: the red visor looked robotic)
        static const uint32_t kSkin[5] = {rgb(232, 188, 156), rgb(208, 160, 122), rgb(172, 120, 86), rgb(124, 82, 58), rgb(88, 58, 42)};
        const uint32_t skin = C(kSkin[m.id % 5u]);
        R(-1.6f + lean, hy, 0.4f + lean, hy + 2.2f, headC);   // the helmet's back and neck guard
        R(0.2f + lean, hy + 0.1f, 1.9f + lean, hy + 2.5f, skin);  // the face
        t.disc(fx + float(dir) * (0.05f + lean) * q, fy - (hy + 2.6f) * q, std::max(0.8f, 1.75f * q), headC, a, false, 0.75f);
        t.disc(fx + float(dir) * (0.05f + lean) * q, fy - (hy + 2.9f) * q, std::max(0.6f, 1.3f * q), rimC, a / 2, false, 0.55f);
        R(0.3f + lean, hy + 2.4f, 2.2f + lean, hy + 2.9f, C(rgb(84, 96, 118)));  // the brim
        if (!tiny) {
            R(1.15f + lean, hy + 1.45f, 1.55f + lean, hy + 1.85f, rgb(40, 30, 30));  // an eye
            R(0.1f + lean, hy + 0.1f, 0.45f + lean, hy + 1.7f, C(rgb(50, 56, 70)));  // the strap
        }
    } else {
        R(-1.6f + lean, hy, 1.7f + lean, hy + 3.8f, headC);
        R(-1.8f + lean, hy + 3.f, 1.5f + lean, hy + 4.4f, hairC);
        if (!tiny) R(0.8f + lean, hy + 1.8f, 1.5f + lean, hy + 2.4f, rgb(200, 240, 255));  // an eye, glowing pale
        R(-1.8f + lean, hy + 4.f, 1.5f + lean, hy + 4.4f, rimC);
    }
    // the gun: level when firing, across the chest on the run
    const float gy = (running ? yb + 4.2f : yb + 3.6f);
    const float glen = side == 0 ? 7.6f : 6.8f;
    const float gx = lean + recoil;
    R(-1.4f + gx, gy - 0.4f, 0.6f + gx, gy + 0.8f, gunC);  // stock
    R(0.f + gx, gy, glen + gx, gy + 1.2f, gunC);
    if (!tiny) R(3.f + gx, gy + 1.2f, 6.f + gx, gy + 1.7f, side == 0 ? C(rgb(70, 76, 92)) : C(rgb(214, 168, 70)));  // shroud / gold trim
    R(0.f + gx, gy - 0.7f, 3.4f + gx, gy + 0.3f, side == 0 ? C(rgb(88, 100, 124)) : bodyC);                        // the arm
    if (side == 1) R(glen - 1.6f + gx, gy + 0.1f, glen + gx, gy + 1.1f, rgb(120, 220, 255));  // the plasma rifle's cell
    else R(glen - 0.7f + gx, gy + 0.1f, glen + gx, gy + 1.1f, rgb(255, 70, 80));               // the las-gun's emitter
    // a muzzle flash while the volley lasts
    if (m.burst > 0 && m.burstT > 0.04f) {
        const float mx = fx + float(dir) * (glen + 0.6f + gx) * q, my = fy - (gy + 0.6f) * q;
        t.glow(mx, my, std::max(2.5f, q * 3.4f), side == 0 ? rgb(255, 80, 90) : rgb(120, 210, 255), 230);
        t.add(int(mx), int(my), rgb(255, 255, 255), 255);
    }
}

void drawMech(Tgt& t, const Mech& m) {
    const float k = g_art.k;
    const float hpx = (g_ov.on ? g_ov.h : mechH(m)) * k;
    const float q = hpx / 44.f;
    const float fx = (g_ov.on ? g_ov.x : screenX(m.X, m.Y)) * k, fy = (g_ov.on ? g_ov.y : m.Y + g_lift) * k;
    const int dir = m.side == 0 ? 1 : -1;
    const float fog = g_ov.on ? 0.f : fogOf(m.Y);
    auto C = [&](uint32_t c) { return fog > 0.f ? mix(c, kHaze, int(fog * 255.f)) : c; };
    auto R = [&](float x0, float y0, float x1, float y1, uint32_t c) {
        const float ax0 = fx + float(dir) * x0 * q, ax1 = fx + float(dir) * x1 * q;
        t.rect(std::min(ax0, ax1), fy - y1 * q, std::max(ax0, ax1), fy - y0 * q, c);
    };
    auto P = [&](std::initializer_list<float> pts, uint32_t c) {  // a polygon in figure units
        float xs[16], ys[16];
        int n = 0;
        for (auto it = pts.begin(); it != pts.end() && n < 16; it += 2, ++n) {
            xs[n] = fx + float(dir) * (*it) * q;
            ys[n] = fy - (*(it + 1)) * q;
        }
        t.poly(xs, ys, n, c);
    };
    auto Ln = [&](float x0, float y0, float x1, float y1, uint32_t c, int al = 255) {
        t.line(fx + float(dir) * x0 * q, fy - y0 * q, fx + float(dir) * x1 * q, fy - y1 * q, c, al);
    };
    t.disc(fx, fy, 18.f * q, rgb(20, 16, 26), 80, false, 0.22f);  // its shadow
    // A digitigrade leg (owner 13:26: "Make the enemy and Sol walkers have digitigrade style legs"): the thigh down and forward
    // to the knee, the shin back down to a raised hock, a short pastern to the toes, a long clawed foot flat on the ground, a
    // spur behind; armour plates on the thigh and shin, a piston between them. (figure units; o = the foot's step, lift = raised)
    auto seg = [&](float x0, float y0, float x1, float y1, float r0, float r1, uint32_t c) {
        const float dx = x1 - x0, dy = y1 - y0, l = std::max(0.01f, std::sqrt(dx * dx + dy * dy)), nx = -dy / l, ny = dx / l;
        P({x0 + nx * r0, y0 + ny * r0, x1 + nx * r1, y1 + ny * r1, x1 - nx * r1, y1 - ny * r1, x0 - nx * r0, y0 - ny * r0}, c);
    };
    auto joint = [&](float x, float y, float r, uint32_t c) { t.disc(fx + float(dir) * x * q, fy - y * q, std::max(0.8f, r * q), c); };
    auto digiLeg = [&](float hipX, float hipY, float sc, float o, float lift, uint32_t thighC, uint32_t plateC, uint32_t shinC, uint32_t jointC,
                       uint32_t footC, uint32_t clawC) {
        const float Fx = hipX + o, Fy = lift;                                 // the foot
        const float Hx = Fx - 3.f * sc, Hy = Fy + hipY * 0.3f;                // the hock, raised behind the foot
        const float Kx = (hipX + Hx) * 0.5f + 6.f * sc, Ky = (hipY + Hy) * 0.5f + 1.f;  // the knee, thrust forward
        seg(hipX, hipY, Kx, Ky, 3.2f * sc, 2.4f * sc, thighC);                // thigh
        seg(hipX + 0.6f * sc, hipY - 0.5f, Kx - 0.4f * sc, Ky + 1.f, 1.8f * sc, 1.2f * sc, plateC);  // its armour plate
        seg(Kx, Ky, Hx, Hy, 1.9f * sc, 1.5f * sc, shinC);                     // shin, back to the hock
        seg(Kx - 0.3f * sc, Ky - 1.f, Hx + 0.8f * sc, Hy + 1.f, 0.9f * sc, 0.7f * sc, plateC);  // shin guard
        Ln((hipX + Kx) * 0.5f, (hipY + Ky) * 0.5f, (Kx + Hx) * 0.5f, (Ky + Hy) * 0.5f, jointC, 220);  // the piston
        seg(Hx, Hy, Fx + 0.5f * sc, Fy + 1.6f * sc, 1.2f * sc, 1.f * sc, shinC);  // pastern
        P({Fx - 2.5f * sc, Fy, Fx + 6.f * sc, Fy, Fx + 5.f * sc, Fy + 2.f * sc, Fx - 1.5f * sc, Fy + 2.4f * sc}, footC);  // the foot
        for (int cl = 0; cl < 3; ++cl) {  // claws
            const float cx2 = Fx + 4.f * sc + float(cl) * 1.1f * sc;
            P({cx2, Fy, cx2 + 1.6f * sc, Fy, cx2 + 0.4f * sc, Fy + 1.2f * sc}, clawC);
        }
        P({Fx - 2.5f * sc, Fy + 0.4f, Fx - 4.5f * sc, Fy, Fx - 2.f * sc, Fy + 1.4f * sc}, clawC);  // the spur
        joint(hipX, hipY, 2.6f * sc, jointC);
        joint(Kx, Ky, 2.f * sc, jointC);
        joint(Hx, Hy, 1.4f * sc, jointC);
    };
    const float walk = m.st == St::Run ? std::sin(m.phase) : 0.f;
    const float bob = m.st == St::Run ? std::fabs(std::cos(m.phase)) * 1.2f : 0.f;
    const float sink = m.st == St::Dying ? m.t * m.t * 10.f : 0.f;  // buckling as it dies (slowly, then faster)
    const uint32_t rimC = rgb(255, 170, 120);
    if (m.side == 0) {
        // the human walker (after the M2 Tiger): a wide armoured hull, missile pods either side, reverse-jointed legs, a Gatling laser
        const uint32_t tan = C(rgb(170, 156, 120)), tanL = C(rgb(200, 186, 146)), tanD = C(rgb(118, 106, 84)), dark = C(rgb(42, 44, 54)), steel = C(rgb(96, 106, 126));
        for (int leg = 0; leg < 2; ++leg) {  // the far leg first, darker
            const float o = leg == 0 ? -walk * 3.5f : walk * 3.5f;
            const float lift = std::max(0.f, leg == 0 ? -walk : walk) * 2.f;
            if (leg == 0) digiLeg(-5.f, 25.f - bob - sink, 1.3f, o, lift, tanD, tanD, dark, scalec(dark, 0.8f), dark, steel);
            else digiLeg(3.f, 25.f - bob - sink, 1.3f, o, lift, tan, tanL, steel, dark, dark, C(rgb(150, 160, 180)));
        }
        const float hy = 25.f - bob - sink;
        P({-16.f, hy, 13.f, hy, 18.5f, hy + 5.f, 16.f, hy + 12.f, -12.f, hy + 13.5f, -17.5f, hy + 7.f}, tan);  // the hull
        P({-12.f, hy + 9.5f, 15.f, hy + 9.f, 13.f, hy + 13.f, -10.f, hy + 13.5f}, tanL);                        // the sloped top plates
        Ln(-12.f, hy + 13.5f, 13.f, hy + 13.f, rimC, 200);
        for (float px = -10.f; px < 12.f; px += 4.f) Ln(px, hy + 1.f, px + 1.f, hy + 9.f, tanD, 150);       // plate seams
        R(-6.f, hy + 4.f, 3.f, hy + 4.6f, C(rgb(220, 210, 180)));                                            // a "no step" stencil
        // missile pods with their tubes, the near one in front
        for (int pod = 0; pod < 2; ++pod) {
            const float px = pod == 0 ? -21.f : 14.f;
            R(px, hy + 4.f, px + 7.f, hy + 13.f, pod == 0 ? tanD : tan);
            for (float tx = px + 1.f; tx < px + 6.5f; tx += 1.7f)
                for (float ty = hy + 5.f; ty < hy + 12.f; ty += 1.7f) {
                    R(tx, ty, tx + 1.1f, ty + 1.1f, dark);
                    if (pod == 1 && q > 0.5f) R(tx + 0.3f, ty + 0.3f, tx + 0.7f, ty + 0.7f, rgb(200, 50, 60));
                }
            R(px, hy + 13.f, px + 7.f, hy + 13.6f, rimC);
        }
        R(1.f, hy + 13.5f, 10.f, hy + 17.f, steel);  // the top turret with its gun
        R(8.f, hy + 15.f, 17.f, hy + 16.f, dark);
        Ln(-8.f, hy + 13.5f, -8.f, hy + 22.f, dark);  // antenna masts
        Ln(-4.f, hy + 13.5f, -4.f, hy + 19.f, dark);
        R(4.f, hy + 6.f, 8.f, hy + 9.f, rgb(255, 60, 70));  // the sensor eye
        t.glow(fx + float(dir) * 6.f * q, fy - (hy + 7.5f) * q, std::max(2.f, 3.f * q), rgb(255, 50, 60), 120);
        R(-7.f, hy + 7.f, -2.f, hy + 9.f, rgb(90, 220, 255));  // a cyan screen
        R(-16.f, hy + 2.f, 12.f, hy + 2.8f, rgb(255, 80, 200));  // a neon stripe along the hull
        // the Gatling laser under the nose: a cluster of barrels turning
        R(13.f, hy, 19.f, hy + 4.5f, dark);
        const int spin = int(g.clock * (m.shots > 0 ? 40.f : 4.f)) % 3;
        for (int b = 0; b < 3; ++b) R(18.f, hy + 0.4f + float(b) * 1.3f, 30.f, hy + 1.2f + float(b) * 1.3f, b == spin ? C(rgb(150, 160, 180)) : steel);
        R(29.f, hy, 30.5f, hy + 4.5f, dark);
        if (m.shots > 0) {
            t.glow(fx + float(dir) * 31.f * q, fy - (hy + 2.2f) * q, std::max(3.f, 5.f * q), rgb(255, 70, 80), 240);
            t.add(int(fx + float(dir) * 31.f * q), int(fy - (hy + 2.2f) * q), rgb(255, 255, 255), 255);
        }
    } else {
        // the enemy's giant walker (after the grey walker picture): a hunched gilded hull with a long beam cannon, a great shield
        // plate rising at its back, bulky thighs, dark armoured shins, bladed feet; a blue core glowing
        const uint32_t gold = C(rgb(222, 184, 104)), goldL = C(rgb(255, 224, 150)), goldD = C(rgb(146, 108, 54)), white = C(rgb(232, 226, 210)),
                       dark = C(rgb(50, 42, 40)), cloth = C(rgb(110, 32, 42));
        for (int leg = 0; leg < 2; ++leg) {  // longer legs: the thighs wrapped in red cloth, dark shins, gilded bladed feet
            const float o = leg == 0 ? -walk * 4.f : walk * 4.f;
            const float lift = std::max(0.f, leg == 0 ? -walk : walk) * 2.5f;
            if (leg == 0) digiLeg(3.f, 30.f - bob - sink, 1.4f, o, lift, scalec(cloth, 0.7f), goldD, scalec(dark, 0.8f), scalec(dark, 0.7f), goldD, goldD);
            else digiLeg(-3.f, 30.f - bob - sink, 1.4f, o, lift, cloth, gold, dark, scalec(dark, 0.8f), gold, white);
        }
        const float hy = 30.f - bob - sink;
        // the shield plate rising at its back
        P({-18.f, hy + 4.f, -11.f, hy + 4.f, -9.f, hy + 26.f, -15.f, hy + 30.f, -20.f, hy + 22.f}, white);
        P({-17.f, hy + 6.f, -12.f, hy + 6.f, -10.5f, hy + 24.f, -15.f, hy + 27.f, -18.5f, hy + 21.f}, gold);
        Ln(-15.f, hy + 30.f, -9.f, hy + 26.f, rimC, 220);
        if (q > 0.6f) {  // its number
            R(-16.f, hy + 15.f, -13.f, hy + 15.8f, white);
            R(-16.f, hy + 13.f, -13.f, hy + 13.8f, white);
        }
        // the hull: a hunched wedge
        P({-12.f, hy, 8.f, hy, 16.f, hy + 7.f, 12.f, hy + 15.f, -6.f, hy + 17.f, -13.f, hy + 9.f}, gold);
        P({-6.f, hy + 11.f, 10.f, hy + 10.f, 12.f, hy + 15.f, -6.f, hy + 17.f}, goldL);
        Ln(-6.f, hy + 17.f, 12.f, hy + 15.f, rimC, 220);
        R(-10.f, hy + 2.f, 8.f, hy + 3.f, goldD);
        // a cluster of missile cells on the shoulder
        R(-6.f, hy + 15.f, 2.f, hy + 21.f, white);
        for (float cx2 = -5.f; cx2 < 1.5f; cx2 += 2.f)
            for (float cy2 = hy + 16.f; cy2 < hy + 20.5f; cy2 += 2.f) R(cx2, cy2, cx2 + 1.2f, cy2 + 1.2f, dark);
        // the blue core and the sensor
        R(-1.f, hy + 5.f, 4.f, hy + 9.f, rgb(110, 200, 255));
        t.glow(fx + float(dir) * 1.5f * q, fy - (hy + 7.f) * q, std::max(3.f, 6.f * q), rgb(80, 150, 255), 150);
        R(10.f, hy + 9.f, 12.f, hy + 11.f, rgb(200, 240, 255));
        // the beam cannon: a long barrel forward under the hull, with cooling rings
        R(8.f, hy + 4.f, 34.f, hy + 6.5f, white);
        for (float rx2 = 14.f; rx2 < 32.f; rx2 += 4.f) R(rx2, hy + 3.5f, rx2 + 1.2f, hy + 7.f, goldD);
        R(32.f, hy + 3.f, 36.f, hy + 7.5f, goldD);
        R(-11.f, hy + 1.f, 9.f, hy + 1.8f, rgb(90, 220, 255));  // a neon seam
        if (m.beamT < 0.f) {  // charging: the muzzle gathers light
            const float u = 1.f + m.beamT / 0.45f;
            t.glow(fx + float(dir) * 36.f * q, fy - (hy + 5.2f) * q, std::max(2.f, (2.f + 7.f * u) * q), rgb(120, 190, 255), int(120.f + 135.f * u));
        }
    }
}

void drawBeam(Tgt& t, const Mech& m) {
    if (m.beamT <= 0.f || m.side != 1) return;
    const float k = g_art.k;
    const float hy = 30.f;
    const float q = mechH(m) / 44.f;
    const float qq = g_ov.on ? g_ov.h / 44.f : q;
    const float x0 = ((g_ov.on ? g_ov.x : screenX(m.X, m.Y)) - 36.f * qq) * k, y0 = ((g_ov.on ? g_ov.y : m.Y + g_lift) - (hy + 5.2f) * qq) * k;
    const float x1 = m.bx * k, y1 = (m.by + g_lift) * k;
    const float fl = 0.8f + 0.2f * std::sin(g.clock * 60.f);
    const float wpx = std::max(1.f, 1.6f * q * k);
    // the beam: a soft halo, a blue body and a white core
    const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    const float nx = -dy / std::max(1.f, len), ny = dx / std::max(1.f, len);
    for (float o = -wpx * 3.f; o <= wpx * 3.f; o += 1.f) {
        const float d = std::fabs(o) / (wpx * 3.f);
        const uint32_t c = d < 0.2f ? rgb(230, 245, 255) : d < 0.45f ? rgb(90, 170, 255) : rgb(60, 90, 220);
        const int a = int((d < 0.2f ? 255.f : d < 0.45f ? 200.f : 90.f * (1.f - d)) * fl);
        t.line(x0 + nx * o, y0 + ny * o, x1 + nx * o, y1 + ny * o, c, a, true);
    }
    t.glow(x1, y1, wpx * 6.f, rgb(120, 190, 255), 220);
    t.glow(x0, y0, wpx * 4.f, rgb(150, 210, 255), 200);
}

void drawShip(Tgt& t, const Ship& s, uint32_t now, float vis = 1.f) {  // vis: the opening's sky fading in (the ships with it)
    if (vis <= 0.f) return;
    const float k = g_art.k;
    const float x = s.x * k, y = (s.y + g_lift) * k;
    const float fog = clamp01((0.9f - s.size) * 0.8f);
    blit(t, s.spr, int(x) + s.ox, int(y) + s.oy, int(255.f * vis), rgb(120, 60, 120), fog * 0.6f);
    // the engines: a blue-white heart and a long soft glow behind
    const float pulse = 0.85f + 0.15f * std::sin(float(now % 100000u) * 0.02f + s.x);
    for (const auto& e : s.engines) {
        const float ex = x + e.first * k, ey = y + e.second * k;
        t.glow(ex, ey, std::max(2.f, 5.f * s.size * k) * pulse, rgb(120, 170, 255), int(200.f * vis));
        t.glow(ex - float(s.dir) * 4.f * s.size * k, ey, std::max(2.f, 3.f * s.size * k), rgb(255, 255, 255), int(160.f * vis));
    }
    if (vis < 1.f) return;
    // blinking lights at the prow and on the tower
    if ((now / 600u + uint32_t(s.kind)) % 3u == 0u) t.add(int(x + s.bowX * k), int(y + s.bowY * k), rgb(255, 60, 60), 255);
}

void drawFrame(uint32_t now, float pan, float sky) {
    Tgt& f = g_art.frame;
    const float k = g_art.k;
    const float e = smooth01(pan);
    g_lift = (1.f - e) * kDrop;
    // the world (static): rows from world y (screen y - lift)
    const int off = int(std::floor((kDrop - g_lift) * k));
    const int sk = int(clamp01(sky) * 255.f);
    for (int y = 0; y < f.h; ++y) {
        const int wy = y + off;
        uint32_t* dst = &f.px[size_t(y) * size_t(f.w)];
        if (wy < 0 || wy >= g_art.world.h) {
            std::fill(dst, dst + f.w, 0u);
            continue;
        }
        const uint32_t* src = &g_art.world.px[size_t(wy) * size_t(g_art.world.w)];
        if (sk >= 255) std::copy(src, src + f.w, dst);
        else
            for (int x = 0; x < f.w; ++x) dst[x] = mix(0u, src[x], sk);
    }
    // stars: in the high, dark sky (they drift slowly, twinkle, and are what the opening starts with)
    const float W = g_W;
    for (int i = 0; i < int(220.f * W / 640.f); ++i) {
        const uint32_t h = hash32(uint32_t(i) * 7919u + 3u);
        const float sx = std::fmod(float(h % 100000u) / 100000.f * W - float(now % 1000000u) * 0.002f * float(1 + (h >> 28) % 3) + W * 4.f, W);
        const float wy = -kDrop + float((h >> 8) % 3000u) / 3000.f * (kDrop + kYh - 120.f);
        const float dark = clamp01((kYh - 140.f - wy) / 160.f);
        if (dark <= 0.f) continue;
        const float tw = 0.65f + 0.35f * std::sin(float(now % 100000u) * 0.004f + float(h & 255u));
        const uint32_t c = (h >> 20) % 7u == 0u ? rgb(255, 170, 230) : (h >> 20) % 7u == 1u ? rgb(170, 220, 255) : rgb(240, 236, 255);
        const int a = int((110.f + float((h >> 12) & 127u)) * tw * (0.35f + 0.65f * dark));
        f.add(int(sx * k), ay(wy), c, a);
        if ((h & 15u) == 0u) {
            f.add(int(sx * k) - 1, ay(wy), c, a / 3);
            f.add(int(sx * k) + 1, ay(wy), c, a / 3);
            f.add(int(sx * k), ay(wy) - 1, c, a / 3);
            f.add(int(sx * k), ay(wy) + 1, c, a / 3);
        }
    }
    // ships, far ones first; small fighters streaking by
    std::vector<const Ship*> order;
    for (const Ship& s : g.ships) order.push_back(&s);
    std::sort(order.begin(), order.end(), [](const Ship* a, const Ship* b) { return a->size < b->size; });
    for (const Ship* s : order) drawShip(f, *s, now, clamp01(sky * 1.4f - 0.3f));
    for (int i = 0; i < (sky > 0.6f ? 5 : 0); ++i) {
        const float per = 7.f + float(i) * 2.3f;
        const float u = std::fmod(g.clock / per + float(i) * 0.37f, 1.f);
        const float fx2 = (i % 2 ? 1.f - u : u) * (W + 80.f) - 40.f, fy2 = kYh - 60.f - float(i) * 34.f + std::sin(u * 9.f + float(i)) * 8.f;
        const int d = i % 2 ? -1 : 1;
        for (int j = 0; j < 14; ++j) f.add(int((fx2 - float(d * j) * 1.6f) * k), ay(fy2), i % 2 ? rgb(255, 90, 200) : rgb(90, 220, 255), 150 - j * 10);
        f.rect((fx2 - 2.f) * k, ay(fy2) - 1.f, (fx2 + 2.f) * k, ay(fy2) + std::max(1.f, k), rgb(200, 196, 220));
    }
    // the bombardment: the Sol dreadnought's gun flashes, a red light marks the spot, the payload falls
    for (const Blast& b : g.blasts) {
        const Ship* sol = nullptr;
        for (const Ship& s : g.ships)
            if (s.kind == 2) sol = &s;
        if (sol && b.age < 0.5f)
            f.glow((sol->x + sol->bowX) * k, ay(sol->y + sol->bowY), (6.f + 20.f * (0.5f - b.age)) * k * 0.6f, rgb(255, 220, 160), int(240.f * (1.f - b.age * 2.f)));
        if (b.stage == 0 && b.age > 0.5f) {  // the targeting light: a pulsing ring on the ground and a thin red line from the sky
            const float rx = b.r * (b.Y - kYh) * (W * 0.62f) / (kH - kYh);
            const float pulse = 0.6f + 0.4f * std::sin(b.age * 18.f);
            for (int a2 = 0; a2 < 64; ++a2) {
                const float an = float(a2) / 64.f * 2.f * kPi;
                f.add(int((b.x + std::cos(an) * rx * 0.5f) * k), ay(b.y + std::sin(an) * rx * 0.17f), rgb(255, 50, 60), int(200.f * pulse));
            }
            f.line(b.x * k, float(ay(b.y)), (b.x - 40.f) * k, float(ay(b.y - 420.f)), rgb(255, 40, 60), int(90.f * pulse), true);
        }
        if (b.stage == 1) {  // the payload: a white-hot capsule with a plume, from high above
            const float u = clamp01((b.age - 2.4f) / 0.5f);
            const float px = b.x - 40.f * (1.f - u), py = b.y - 420.f * (1.f - u);
            for (int j = 0; j < 30; ++j) {
                const float v = float(j) / 30.f;
                f.glow((px - 40.f * v * 0.25f) * k, float(ay(py - 120.f * v * 0.3f)), std::max(2.f, (5.f - 4.f * v) * k), j < 4 ? rgb(255, 255, 230) : rgb(255, 140, 60), int(200.f * (1.f - v)));
            }
        }
    }
    // the ground's dead: drawn as they are while the pool spreads, then kept as a picture
    for (Body& b : g.bodies) {
        const float age = g.clock - b.t0;
        const float fade = age > 210.f ? clamp01((230.f - age) / 20.f) : 1.f;
        if (age < 8.f) {
            drawMan(f, nullptr, &b, fade);
            continue;
        }
        if (!b.spr || b.sprK != k) {
            const float h = manH(b.Y);
            const int sw = int(h * 1.5f * k) + 6, sh = int(h * 0.9f * k) + 6;
            b.spr = std::make_shared<Tgt>();
            b.spr->init(sw, sh, true);
            b.sprK = k;
            g_ov = Override{true, float(sw) * 0.5f / k, float(sh) * 0.62f / k, h, fogOf(b.Y)};
            const float lift = g_lift;
            g_lift = 0.f;
            drawMan(*b.spr, nullptr, &b, 1.f);
            g_lift = lift;
            g_ov.on = false;
            b.ox = sw / 2;
            b.oy = int(float(sh) * 0.62f);
        }
        blit(f, *b.spr, int(screenX(b.X, b.Y) * k) - b.ox, ay(b.Y) - b.oy, int(255.f * fade));
    }
    // everything standing, back to front: props, men, mechs
    struct Item {
        float Y;
        int kind, idx;
    };
    std::vector<Item> items;
    items.reserve(g.props.size() + g.men.size() + g.mechs.size());
    for (int i = 0; i < int(g.props.size()); ++i) items.push_back({g.props[size_t(i)].Y, 0, i});
    for (int i = 0; i < int(g.men.size()); ++i) items.push_back({g.men[size_t(i)].Y, 1, i});
    for (int i = 0; i < int(g.mechs.size()); ++i) items.push_back({g.mechs[size_t(i)].Y + 0.5f, 2, i});
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.Y < b.Y; });
    for (const Item& it : items) {
        if (it.kind == 0) {
            const Prop& p = g.props[size_t(it.idx)];
            if (p.spr.w == 0) continue;
            blit(f, p.spr, int(screenX(p.X, p.Y) * k) + p.ox, ay(p.Y) + p.oy, 255, kHaze, fogOf(p.Y));
        } else if (it.kind == 1) {
            const Man& m = g.men[size_t(it.idx)];
            const float fade = clamp01((g.clock - m.born) / 0.4f);
            drawMan(f, &m, nullptr, fade);
        } else {
            drawMech(f, g.mechs[size_t(it.idx)]);
        }
    }
    // fire and light: shots, beams, flames, sparks; smoke and dust over them
    for (const Shot& s : g.shots) {
        const float u = clamp01(s.t / s.dur);
        const float x = s.x0 + (s.x1 - s.x0) * u, y = s.y0 + (s.y1 - s.y0) * u;
        const float d = std::max(1.f, std::hypot(s.x1 - s.x0, s.y1 - s.y0));
        const float tl = (s.kind == 1 ? 3.f : s.kind == 2 ? 6.f : 9.f) * clamp01(depthScale(y) + 0.3f);
        const float tx = x - (s.x1 - s.x0) / d * tl, ty = y - (s.y1 - s.y0) / d * tl;
        if (s.kind == 1) {  // plasma: a blue-white ball with a short tail
            f.glow(x * k, float(ay(y)), std::max(2.f, 3.2f * k), rgb(90, 180, 255), 230);
            f.disc(x * k, float(ay(y)), std::max(0.7f, 1.f * k), rgb(235, 250, 255), 255, true);
            f.line(tx * k, float(ay(ty)), x * k, float(ay(y)), rgb(80, 160, 255), 150, true);
        } else {  // las bolts and the Gatling laser: red pulses with a white core
            f.line(tx * k, float(ay(ty)), x * k, float(ay(y)), rgb(255, 40, 60), 230, true);
            f.line(tx * k, float(ay(ty)) + 1.f, x * k, float(ay(y)) + 1.f, rgb(200, 20, 50), 110, true);
            f.add(int(x * k), ay(y), rgb(255, 230, 230), 255);
            if (s.kind == 0) f.glow(x * k, float(ay(y)), std::max(1.5f, 2.2f * k), rgb(255, 60, 70), 110);
        }
    }
    for (const Mech& m : g.mechs) drawBeam(f, m);
    for (const Part& p : g.parts) {
        const float u = p.age / p.life;
        const float x = p.x * k, y = float(ay(p.y));
        switch (p.kind) {
            case 0: f.add(int(x), int(y), p.col, int(230.f * (1.f - u))); break;
            case 1: f.disc(x, y, p.size * k, p.col, int(90.f * (1.f - u) * std::min(1.f, p.age * 3.f))); break;
            case 2: f.rect(x, y, x + std::max(1.f, p.size * k), y + std::max(1.f, p.size * k), p.col); break;
            case 3: f.add(int(x), int(y), p.col, int(255.f * (1.f - u))); break;
            case 4: f.disc(x, y, p.size * k, p.col, int(70.f * (1.f - u))); break;
            case 6: {  // hot smoke: lit orange from inside at first, then dark
                const uint32_t col = lerpc(rgb(255, 140, 60), p.col, clamp01(p.age / 2.5f));
                f.disc(x, y, p.size * k, col, int(150.f * (1.f - u) * std::min(1.f, p.age * 4.f)));
                if (p.age < 1.5f) f.glow(x, y, p.size * k * 1.3f, rgb(255, 110, 40), int(90.f * (1.f - p.age / 1.5f)));
                break;
            }
            default: f.glow(x, y, std::max(1.5f, p.size * 2.f * k), u < 0.4f ? rgb(255, 210, 120) : rgb(255, 110, 50), int(220.f * (1.f - u))); break;
        }
    }
    for (const Fire& fi : g.fires) {
        const float fl = 0.75f + 0.25f * std::sin(g.clock * 13.f + fi.x);
        f.glow(fi.x * k, float(ay(fi.y - fi.size * 0.6f)), std::max(2.f, fi.size * 2.6f * k) * fl, rgb(255, 120, 50), 110);
    }
    // the blasts: a pillar of light, a white flash over everything, a fireball swelling and rising, a ring of shock running
    // along the ground (and the picture shakes - see draw)
    for (const Blast& b : g.blasts) {
        if (b.stage < 2) continue;
        const float u = b.age - 2.9f;
        const float rx = b.r * (b.Y - kYh) * (W * 0.62f) / (kH - kYh);
        if (u < 0.35f) {  // the pillar of light where it struck
            const float a = 1.f - u / 0.35f;
            const float pw = rx * 0.22f * (1.f + u * 3.f);
            for (float o = -pw; o <= pw; o += 1.f / k) {
                const float d = std::fabs(o) / pw;
                f.line((b.x + o) * k, float(ay(b.y)), (b.x + o) * k, float(ay(b.y - 480.f)), d < 0.3f ? rgb(255, 250, 235) : rgb(255, 170, 90), int(230.f * a * (1.f - d)), true);
            }
        }
        if (u < 0.3f) {  // the flash
            const int a = int(230.f * (1.f - u / 0.3f));
            for (uint32_t& p : f.px) p = mix(p, rgb(255, 232, 205), a * 2 / 3);
        }
        const float grow = clamp01(u / 0.6f);
        const float fr = rx * (0.35f + 0.75f * std::sqrt(grow));
        const float rise = u * 34.f;
        const float life = clamp01(1.f - u / 4.f);
        const float cy = b.y - rise - fr * 0.35f;
        f.glow(b.x * k, float(ay(cy)), fr * 2.6f * k, rgb(255, 110, 40), int(200.f * life));                                          // the glow
        f.glow(b.x * k, float(ay(cy)), fr * 1.4f * k, lerpc(rgb(255, 230, 160), rgb(255, 90, 40), clamp01(u / 2.f)), int(255.f * life));  // the fireball
        f.glow(b.x * k, float(ay(cy)), fr * 0.7f * k, rgb(255, 255, 230), int(255.f * clamp01(1.f - u / 1.4f)));                       // its white heart
        f.glow(b.x * k, float(ay(b.y)), rx * 1.2f * k, rgb(255, 140, 60), int(160.f * life));                                            // the ground burning
        const float sw = rx * (0.4f + u * 2.6f);
        if (u < 1.3f)
            for (int ring = 0; ring < 3; ++ring)
                for (int a2 = 0; a2 < 160; ++a2) {
                    const float an = float(a2) / 160.f * 2.f * kPi, rr = sw - float(ring) * 1.5f;
                    f.add(int((b.x + std::cos(an) * rr) * k), ay(b.y + std::sin(an) * rr * 0.32f), rgb(255, 210, 160), int((ring == 0 ? 220.f : 120.f) * (1.f - u / 1.3f)));
                }
    }
    // a walker blowing up: a blue-white flash, a fireball swelling and rising, a ring of shock along the ground, the secondary
    // blasts' glows on the buckling hull
    for (const Boom& b : g.booms) {
        const float u = b.age, h = b.h;
        if (u < 0.12f) {  // the flash lights the whole field a little
            const int a = int(70.f * (1.f - u / 0.12f));
            for (uint32_t& p : f.px) p = mix(p, rgb(235, 240, 255), a);
        }
        if (u < 0.3f) f.glow(b.x * k, float(ay(b.y - h * 0.5f)), h * 3.2f * k, rgb(200, 225, 255), int(255.f * (1.f - u / 0.3f)));
        const float grow = clamp01(u / 0.45f), life = clamp01(1.f - u / 1.9f);
        const float fr = h * (0.35f + 0.6f * std::sqrt(grow));
        const float cy = b.y - h * 0.5f - u * 22.f;
        f.glow(b.x * k, float(ay(cy)), fr * 2.2f * k, rgb(255, 110, 40), int(190.f * life));
        f.glow(b.x * k, float(ay(cy)), fr * 1.2f * k, lerpc(rgb(255, 235, 170), rgb(255, 90, 40), clamp01(u / 1.2f)), int(250.f * life));
        f.glow(b.x * k, float(ay(cy)), fr * 0.55f * k, rgb(255, 255, 235), int(255.f * clamp01(1.f - u / 0.8f)));
        if (u < 0.9f) {  // the ring of shock
            const float sw = h * (0.5f + u * 4.2f);
            const int al = int(210.f * (1.f - u / 0.9f));
            for (int a2 = 0; a2 < 120; ++a2) {
                const float an = float(a2) / 120.f * 2.f * kPi;
                f.add(int((b.x + std::cos(an) * sw) * k), ay(b.y + std::sin(an) * sw * 0.3f), rgb(255, 225, 190), al);
            }
        }
        static const float kAt[3] = {0.3f, 0.6f, 0.95f};
        for (int i = 0; i < b.next && i < 3; ++i) {  // the secondary blasts
            const float v = u - kAt[i];
            if (v < 0.f || v > 0.4f) continue;
            const float ox = (float((b.seed >> (i * 5)) % 61u) / 60.f - 0.5f) * 0.6f * h;
            const float oy = h * (0.3f + 0.15f * float(i)) - u * u * 10.f;
            f.glow((b.x + ox) * k, float(ay(b.y - oy)), h * (0.45f + v * 0.8f) * k, rgb(255, 160, 60), int(230.f * (1.f - v / 0.4f)));
            f.glow((b.x + ox) * k, float(ay(b.y - oy)), h * 0.2f * k, rgb(255, 250, 220), int(255.f * (1.f - v / 0.4f)));
        }
    }
    (void)now;
}

}  // namespace

void prepare() { start(); }

void draw(Canvas& c, uint32_t now, float pan, float sky) {
    const int blow = blowOf(c.scale());
    const float k = c.scale() / float(blow);
    const float W = float(c.fullWidth());
    const int aw = (c.pw() + blow - 1) / blow, ah = (c.ph() + blow - 1) / blow;
    g_W = W;
    if (g_art.k != k || g_art.w != aw || g_art.h != ah || g_art.worldW != W) {
        g_art.k = k;
        g_art.blow = blow;
        g_art.w = aw;
        g_art.h = ah;
        g_art.frame.init(aw, ah, false);
        if (g.made && g.W != W) {  // laid out for another width: start over
            g = World();
        }
        start();
        g.W = W;
        makeWorld();
        gradeArt(g_art.world, std::max(3, int(k * 3.2f + 0.5f)));  // the Voidfall look, painted in once (the units are drawn in its colours)
        for (Prop& p : g.props) makeProp(p);
        makeShips();
        g_art.worldW = W;
        g_art.worldK = k;
        for (const Blast& b : g.newCraters) printCrater(b.X, b.Y, b.r * 0.55f);
        g.newCraters.clear();
    }
    // the battle's clock: fixed steps behind the frames' clock (at most a fifth of a second at once)
    if (!g_art.started) {
        g_art.started = true;
        g_art.lastNow = now;
    }
    float dt = float(now - g_art.lastNow) / 1000.f;
    g_art.lastNow = now;
    dt = std::min(dt, 0.2f);
    g_art.acc += dt;
    while (g_art.acc >= kDt) {
        g_art.acc -= kDt;
        step();
        for (Ship& s : g.ships) {
            s.x += s.vx * kDt;
            const float span = W + 200.f;
            if (s.x > W + 100.f) s.x -= span;
            if (s.x < -100.f) s.x += span;
        }
    }
    for (const Blast& b : g.newCraters) printCrater(b.X, b.Y, b.r * 0.55f);
    g.newCraters.clear();
    drawFrame(now, pan, sky);
    // the shake after an impact (art pixels)
    int sx = 0, sy = 0;
    for (const Blast& b : g.blasts)
        if (b.stage == 2 && b.age - 2.9f < 0.6f) {
            const float a = (1.f - (b.age - 2.9f) / 0.6f) * 4.f * k;
            sx = int(std::sin(b.age * 91.f) * a);
            sy = int(std::cos(b.age * 73.f) * a * 0.7f);
        }
    if (sx == 0 && sy == 0)
        for (const Boom& b : g.booms)
            if (b.age < 0.35f) {
                const float a = (1.f - b.age / 0.35f) * 1.8f * k;
                sx = int(std::sin(b.age * 97.f) * a);
                sy = int(std::cos(b.age * 79.f) * a * 0.7f);
            }
    // onto the screen
    const Rect clip = c.clipIsView() ? Rect{0, 0, c.pw(), c.ph()} : c.pxClip();
    uint32_t* dst = c.pixels();
    const int pw = c.pw();
    const Tgt& fr = g_art.frame;
    for (int Y = clip.y; Y < clip.y + clip.h; ++Y) {
        const int ry = std::max(0, std::min(fr.h - 1, Y / blow + sy));
        const uint32_t* src = &fr.px[size_t(ry) * size_t(fr.w)];
        uint32_t* d = dst + size_t(Y) * size_t(pw);
        if (blow == 1 && sx == 0) std::copy(src + clip.x, src + clip.x + clip.w, d + clip.x);
        else
            for (int X = clip.x; X < clip.x + clip.w; ++X) d[X] = src[std::max(0, std::min(fr.w - 1, X / blow + sx))];
    }
}

// The pieces on one page for the owner to fine-tune (TI4_UITEST_ONLY=titlewar_assets): the ships, the mechs in their moves, the
// soldiers of both sides in every pose, large, on a dusk backdrop.
void assetSheet(Canvas& c, uint32_t now) {
    const int blow = blowOf(c.scale());
    const float k = c.scale() / float(blow);
    g_W = float(c.fullWidth());
    g_art.k = k;
    g_art.blow = blow;
    g_art.w = (c.pw() + blow - 1) / blow;
    g_art.h = (c.ph() + blow - 1) / blow;
    g_art.frame.init(g_art.w, g_art.h, false);
    g_art.worldW = -1.f;  // (the next real draw rebuilds its world)
    g_lift = 0.f;
    Tgt& f = g_art.frame;
    for (int y = 0; y < f.h; ++y) {
        const uint32_t col = lerpc(rgb(26, 12, 46), rgb(150, 70, 104), float(y) / float(f.h));
        std::fill(f.px.begin() + long(y) * f.w, f.px.begin() + long(y + 1) * f.w, col);
    }
    const float W = g_W;
    // ships
    const struct {
        int kind;
        float x, y, size;
    } kShips[] = {{0, W * 0.18f, 60.f, 1.3f}, {1, W * 0.5f, 56.f, 1.5f}, {2, W * 0.81f, 60.f, 1.2f}};
    for (const auto& d : kShips) {
        Ship sh;
        sh.kind = d.kind;
        sh.x = d.x;
        sh.y = d.y;
        sh.size = d.size;
        sh.dir = 1;
        makeShip(sh);
        drawShip(f, sh, now);
    }
    // mechs
    g_ov.on = true;
    const float savedClock = g.clock;
    g.clock = float(now) / 1000.f;
    float x = 70.f;
    for (int i = 0; i < 3; ++i) {  // the human walker: two steps of its walk, then firing its Gatling laser
        Mech m{};
        m.side = 0;
        m.hp = 40;
        m.st = i < 2 ? St::Run : St::Hold;
        m.phase = float(i) * 2.1f;
        m.shots = i == 2 ? 5 : 0;
        g_ov.x = x;
        g_ov.y = 248.f;
        g_ov.h = 78.f;
        drawMech(f, m);
        x += 122.f;
    }
    x += 70.f;
    for (int i = 0; i < 3; ++i) {  // the enemy walker: a step, charging, firing its beam
        Mech m{};
        m.side = 1;
        m.hp = 40;
        m.st = i < 1 ? St::Run : St::Hold;
        m.phase = 1.2f;
        m.beamT = i == 1 ? -0.15f : i == 2 ? 1.f : 0.f;
        g_ov.x = x;
        g_ov.y = 248.f;
        g_ov.h = 92.f;
        if (i == 2) {
            m.bx = g_ov.x - 110.f;
            m.by = 236.f;
        }
        drawMech(f, m);
        if (i == 2) drawBeam(f, m);
        x += 128.f;
    }
    // soldiers: each side - running (three steps), standing to fire, down in cover, up from cover firing, falling, fallen
    for (int side = 0; side < 2; ++side) {
        float sx = 34.f;
        const float sy = side == 0 ? 344.f : 444.f;
        for (int pose = 0; pose < 8; ++pose) {
            Man m{};
            m.side = side;
            m.hp = 1;
            m.prop = -1;
            m.id = uint32_t(7 + pose);
            g_ov.x = sx;
            g_ov.y = sy;
            g_ov.h = 70.f;
            if (pose < 3) {
                m.st = St::Run;
                m.phase = float(pose) * 1.6f;
            } else if (pose == 3) {
                m.st = St::Hold;
                m.burst = 2;
                m.burstT = 0.08f;
            } else if (pose == 4) {
                m.st = St::Hold;
                m.prop = 0;
                m.up = 0.f;
            } else if (pose == 5) {
                m.st = St::Hold;
                m.prop = 0;
                m.up = 1.f;
                m.burst = 1;
                m.burstT = 0.08f;
            } else if (pose == 6) {
                m.st = St::Dying;
                m.t = 0.25f;
                m.phase = 1.f;
            }
            if (pose < 7) {
                drawMan(f, &m, nullptr, 1.f);
            } else {
                Body b{0.f, 0.f, side, g.clock - 20.f, 977u + uint32_t(side), 1, nullptr, 0.f, 0, 0};
                g_ov.x = sx + 20.f;
                drawMan(f, nullptr, &b, 1.f);
            }
            sx += pose == 6 ? 70.f : 76.f;
        }
    }
    g_ov.on = false;
    g.clock = savedClock;
    // onto the canvas, graded, with labels
    gradeArt(f, std::max(3, int(k * 3.2f + 0.5f)));
    uint32_t* dst = c.pixels();
    for (int Y = 0; Y < c.ph(); ++Y) {
        const uint32_t* src = &f.px[size_t(Y / blow) * size_t(f.w)];
        for (int X = 0; X < c.pw(); ++X) dst[size_t(Y) * size_t(c.pw()) + size_t(X)] = src[X / blow];
    }
    c.setView(0, c.fullWidth());
    auto label = [&](float lx, float ly, const char* s) { drawText(c, FONT_SM, int(lx), int(ly), s, rgb(240, 230, 250)); };
    label(W * 0.18f - 40.f, 110.f, "Dreadnought");
    label(W * 0.5f - 24.f, 110.f, "Cruiser");
    label(W * 0.81f - 80.f, 110.f, "Sol dreadnought (bombards)");
    label(20.f, 256.f, "Human walker: walking x2, Gatling laser");
    label(436.f, 256.f, "Enemy walker: walking, charging, beam");
    label(20.f, 352.f, "Human soldier: run x3, fire, in cover, up and firing, hit, fallen");
    label(20.f, 452.f, "Enemy soldier: run x3, fire, in cover, up and firing, hit, fallen");
}

Stats stats() {
    Stats s;
    for (const Man& m : g.men)
        if (m.st != St::Dying) ++s.alive[m.side];
    s.bodies = int(g.bodies.size());
    for (const Mech& m : g.mechs)
        if (m.st != St::Dying) ++s.mechs[m.side];
    s.bombs = g.bombs;
    s.shots = g.shotsFired;
    s.mechsLost[0] = g.mechsLost[0];
    s.mechsLost[1] = g.mechsLost[1];
    s.beamOn = g.beamOn;
    s.clock = g.clock;
    return s;
}

}  // namespace titlewar
