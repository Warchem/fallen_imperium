// Player colours with a finish - see pcolors.h. Every effect works on the pixels the element changed toward its colour (the
// "mask"), measured in game units so it looks the same at every screen size, and loops in whole parts of 4.8 s.
// (0.75.0 adds eight: Brown, Retro Sunset, Crystal, Ash, Prismatic, Wireframe Green / Blue, Rose Gold.)
#include "ui/pcolors.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/state.h"
#include "ui/theme.h"

namespace pcolours {
namespace {

const Colour kColours[kCount] = {
    {"Red", "Red", theme::player[0], 0, 0, Fx::None, ""},
    {"Blue", "Blue", theme::player[1], 0, 0, Fx::None, ""},
    {"Green", "Green", theme::player[2], 0, 0, Fx::None, ""},
    {"Yellow", "Yellow", theme::player[3], 0, 0, Fx::None, ""},
    {"Purple", "Purple", theme::player[4], 0, 0, Fx::None, ""},
    {"Black", "Black", theme::player[5], 0, 0, Fx::None, ""},
    {"Orange", "Orange", theme::player[6], 0, 0, Fx::None, ""},
    {"Pink", "Pink", theme::player[7], 0, 0, Fx::None, ""},
    // the owner's six
    {"Gold", "Gold", rgb(214, 166, 50), rgb(255, 240, 170), rgb(112, 78, 14), Fx::Sparkle, "Polished gold; a light runs over it and it glints"},
    {"Silver", "Silver", rgb(184, 190, 204), rgb(255, 255, 255), rgb(86, 92, 108), Fx::Sparkle, "Bright silver; a light runs over it and it glints"},
    {"Bronze", "Bronze", rgb(184, 108, 50), rgb(255, 196, 128), rgb(92, 46, 16), Fx::Sparkle, "Warm bronze; a light runs over it and it glints"},
    {"Luminescent Green", "Lum. Green", rgb(104, 255, 146), rgb(214, 255, 226), rgb(40, 255, 110), Fx::Glow, "Lit from inside; a soft glow that breathes"},
    {"Hatred Crimson", "Crimson", rgb(172, 14, 38), rgb(255, 76, 84), rgb(255, 20, 40), Fx::Heartbeat, "Dark blood red; its glow beats like a heart"},
    {"Pearlescent", "Pearl", rgb(234, 230, 240), rgb(255, 255, 255), rgb(150, 146, 162), Fx::Pearl, "Pearl white; a sheen of soft tints drifts over it"},
    // and two of mine
    {"Ion Cyan", "Ion Cyan", rgb(46, 212, 238), rgb(216, 252, 255), rgb(30, 170, 230), Fx::Arcs, "Charged; small arcs of lightning crackle off it"},
    {"Void", "Void", rgb(46, 30, 86), rgb(176, 136, 255), rgb(10, 6, 22), Fx::Void, "Black as deep space, with stars drifting inside"},
    // 0.75.0, the owner's eight (hi / lo: Sunset = the orange top / the purple bottom; Ash = the embers / the charcoal;
    // Wireframe = the line / the glow)
    {"Brown", "Brown", rgb(134, 88, 56), 0, 0, Fx::None, "Earthy brown; plain, like the classic colours"},
    {"Retro Sunset", "Sunset", rgb(206, 72, 150), rgb(255, 164, 60), rgb(96, 40, 172), Fx::Sunset,
     "Orange melting into purple, with the bands of a retro sun"},
    {"Crystal", "Crystal", rgb(150, 212, 238), rgb(255, 255, 255), rgb(64, 120, 172), Fx::Crystal, "Cut ice; its facets catch a turning light"},
    {"Ash", "Ash", rgb(150, 144, 138), rgb(255, 146, 52), rgb(66, 60, 58), Fx::Ash, "Grey ash with embers smouldering in its cracks"},
    {"Prismatic", "Prismatic", rgb(196, 128, 236), rgb(255, 255, 255), 0, Fx::Prism, "Every colour at once; a rainbow drifts across it"},
    {"Wireframe Green", "Wire Green", rgb(54, 232, 110), rgb(206, 255, 218), rgb(20, 200, 84), Fx::Wire,
     "Dark inside, every edge a glowing green line"},
    {"Wireframe Blue", "Wire Blue", rgb(60, 156, 255), rgb(212, 234, 255), rgb(36, 116, 250), Fx::Wire,
     "Dark inside, every edge a glowing blue line"},
    {"Rose Gold", "Rose Gold", rgb(218, 146, 132), rgb(255, 228, 216), rgb(124, 70, 64), Fx::Sparkle,
     "Polished rose gold; a light runs over it and it glints"},
};
static_assert(kCount == ti::kPlayerColours, "the engine and the colour table must agree");
constexpr uint32_t kBrownBase = rgb(134, 88, 56);

inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}
inline int R(uint32_t c) { return int((c >> 16) & 255); }
inline int G(uint32_t c) { return int((c >> 8) & 255); }
inline int B(uint32_t c) { return int(c & 255); }
inline int lum(uint32_t c) { return (R(c) * 3 + G(c) * 6 + B(c)) / 10; }
inline float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
inline float smooth(float v) {
    v = clamp01(v);
    return v * v * (3.f - 2.f * v);
}
constexpr float kTau = 6.2831853f;

// (0.75.0) colour `to` lit like pixel p: p's brightness against the colour's own (baseLum) - darker parts of a unit's art stay
// darker, its highlights go toward white
inline uint32_t relight(uint32_t p, uint32_t to, int baseLum) {
    const float f = float(lum(p)) / float(std::max(1, baseLum));
    if (f <= 1.f) return rgb(int(float(R(to)) * f), int(float(G(to)) * f), int(float(B(to)) * f));
    return mix(to, rgb(255, 255, 255), int(255.f * clamp01((f - 1.f) * 1.2f)));
}
// (0.75.0) Prismatic: the hues round the circle, soft (saturation .58), in 256 steps
const uint32_t* rainbow() {
    static uint32_t ramp[256];
    static bool made = false;
    if (!made) {
        for (int i = 0; i < 256; ++i) {
            const float h = float(i) / 256.f * 6.f, s = 0.58f;
            const int sec = int(h) % 6;
            const float f = h - std::floor(h);
            const float p = 1.f - s, q = 1.f - s * f, u = 1.f - s * (1.f - f);
            float r = 1.f, g = 1.f, b = 1.f;
            switch (sec) {
            case 0: r = 1.f, g = u, b = p; break;
            case 1: r = q, g = 1.f, b = p; break;
            case 2: r = p, g = 1.f, b = u; break;
            case 3: r = p, g = q, b = 1.f; break;
            case 4: r = u, g = p, b = 1.f; break;
            default: r = 1.f, g = p, b = q; break;
            }
            ramp[i] = rgb(int(r * 255.f), int(g * 255.f), int(b * 255.f));
        }
        made = true;
    }
    return ramp;
}
// (0.75.0) Retro Sunset: top (0) to bottom (1) through a hot pink
inline uint32_t sunsetAt(const Colour& k, float f) {
    constexpr uint32_t kMid = rgb(238, 70, 128);
    f = clamp01(f);
    return f < 0.5f ? mix(k.hi, kMid, int(255.f * f * 2.f)) : mix(kMid, k.lo, int(255.f * (f - 0.5f) * 2.f));
}

// How much a pixel the element changed looks like the colour (0..255).
int familyWeight(uint32_t a, const Colour& k) {
    const int l = lum(a);
    const int mx = std::max(R(a), std::max(G(a), B(a))), mn = std::min(R(a), std::min(G(a), B(a)));
    if (k.fx == Fx::Pearl || (k.fx == Fx::Sparkle && k.base == rgb(184, 190, 204))) {  // the near-whites: a light grey
        if (mx - mn > 46) return 0;
        return int(255.f * clamp01(float(l - 70) / 50.f));
    }
    const float ar = float(R(a)), ag = float(G(a)), ab = float(B(a));
    const float br = float(R(k.base)), bg = float(G(k.base)), bb = float(B(k.base));
    const float na = ar * ar + ag * ag + ab * ab, nb = br * br + bg * bg + bb * bb;
    if (na < 1.f) return 0;
    const float cosv = (ar * br + ag * bg + ab * bb) / std::sqrt(na * nb);
    float w = clamp01((cosv - 0.94f) / 0.045f);
    if (k.fx == Fx::Void) w *= clamp01(float(170 - l) / 40.f);
    else w *= clamp01(float(l) / 45.f);
    // (0.75.0) pale or grey colours: leave white (words, highlights drawn over the element) alone
    if (k.fx == Fx::Crystal || k.fx == Fx::Ash || k.fx == Fx::Prism || k.base == rgb(218, 146, 132)) w *= clamp01(float(246 - l) / 18.f);
    // (0.80.0, owner: "buggy" Crystal / Ash / Prismatic) and nothing much darker than the colour itself: Crystal's pale blue and
    // Ash's grey are close in hue to the dark blue-grey fills drawn with the units (the count badges), which took the effect
    if (k.fx >= Fx::Sunset || k.base == rgb(218, 146, 132)) {
        const float lb = float(lum(k.base));
        w *= clamp01((float(l) - 0.38f * lb) / (0.22f * lb));
    }
    return int(w * 255.f);
}

// (0.75.0: plain statics, not thread_local - a thread_local vector needs __cxa_thread_atexit_impl, GLIBC_2.18, which the
// Miyoo's GLIBC_2.4 doesn't have; every Finish runs on the drawing thread)
std::vector<uint8_t> t_mask;
std::vector<uint8_t> t_cell;   // the mask in cells of `cs` x `cs` pixels (about a game unit)
std::vector<uint16_t> t_dist;  // tenths of a cell to the nearest masked cell
std::vector<int> t_edge;
std::vector<float> t_colA, t_colB, t_colC, t_colD, t_rowA, t_rowB, t_rowC;

// familyWeight for every 15-bit colour, built the first time a colour is drawn
const uint8_t* familyLut(int colour) {
    static std::vector<uint8_t> luts[kCount];
    std::vector<uint8_t>& l = luts[colour];
    if (l.empty()) {
        l.resize(32768);
        for (int i = 0; i < 32768; ++i)
            l[size_t(i)] = uint8_t(familyWeight(rgb(((i >> 10) & 31) * 8 + 4, ((i >> 5) & 31) * 8 + 4, (i & 31) * 8 + 4), kColours[colour]));
    }
    return l.data();
}
inline int lutIndex(uint32_t c) { return int(((c >> 19) & 31) << 10 | ((c >> 11) & 31) << 5 | ((c >> 3) & 31)); }

void distanceField(int w, int h, int cs, int limit) {
    const int gw = (w + cs - 1) / cs, gh = (h + cs - 1) / cs;
    t_cell.assign(size_t(gw * gh), 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (t_mask[size_t(y * w + x)] > 100) t_cell[size_t((y / cs) * gw + x / cs)] = 1;
    t_dist.assign(size_t(gw * gh), uint16_t(limit));
    for (int i = 0; i < gw * gh; ++i)
        if (t_cell[size_t(i)]) t_dist[size_t(i)] = 0;
    auto at = [&](int x, int y) -> uint16_t& { return t_dist[size_t(y * gw + x)]; };
    for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x) {
            int d = at(x, y);
            if (!d) continue;
            if (x > 0) d = std::min(d, at(x - 1, y) + 10);
            if (y > 0) d = std::min(d, at(x, y - 1) + 10);
            if (x > 0 && y > 0) d = std::min(d, at(x - 1, y - 1) + 14);
            if (x + 1 < gw && y > 0) d = std::min(d, at(x + 1, y - 1) + 14);
            at(x, y) = uint16_t(d);
        }
    for (int y = gh - 1; y >= 0; --y)
        for (int x = gw - 1; x >= 0; --x) {
            int d = at(x, y);
            if (!d) continue;
            if (x + 1 < gw) d = std::min(d, at(x + 1, y) + 10);
            if (y + 1 < gh) d = std::min(d, at(x, y + 1) + 10);
            if (x + 1 < gw && y + 1 < gh) d = std::min(d, at(x + 1, y + 1) + 14);
            if (x > 0 && y + 1 < gh) d = std::min(d, at(x - 1, y + 1) + 14);
            at(x, y) = uint16_t(d);
        }
}

// a soft halo of colour `col` round the mask: strength(f = 1 at the element .. 0 at the margin) -> alpha. The alpha for each
// tenth of a cell is worked out once.
template <class F>
void halo(uint32_t* px, int stride, int x0, int y0, int w, int h, int cs, float marginPx, uint32_t col, F strength) {
    if (marginPx < 1.f) return;
    const int gw = (w + cs - 1) / cs;
    const int lim = int(marginPx * 10.f / float(cs)) + 1;
    int alphaOf[512];
    const int n = std::min(lim, 511);
    for (int d = 0; d <= n; ++d) {
        const float u = float(d) / 10.f * float(cs) / marginPx;
        const float f = u >= 1.f ? 0.f : (1.f - u) * (1.f - u);
        alphaOf[d] = f <= 0.f ? 0 : int(255.f * clamp01(strength(f)));
    }
    for (int y = 0; y < h; ++y) {
        uint32_t* row = px + size_t(y0 + y) * size_t(stride) + x0;
        const uint16_t* drow = t_dist.data() + size_t((y / cs) * gw);
        const uint8_t* mrow = t_mask.data() + size_t(y * w);
        for (int x = 0; x < w; ++x) {
            const int d = drow[x / cs];
            if (d > n || mrow[x] > 100) continue;
            const int a = alphaOf[d];
            if (a > 0) row[x] = mix(row[x], col, a);
        }
    }
}

}  // namespace

const Colour& info(int c) { return kColours[c < 0 || c >= kCount ? 0 : c]; }

uint32_t text(int c) {
    const Colour& k = info(c);
    if (k.fx == Fx::Void) return rgb(150, 116, 236);  // (words in the colour itself would vanish on the dark panels)
    if (k.fx == Fx::Heartbeat) return rgb(214, 34, 56);
    if (k.base == kBrownBase) return rgb(190, 140, 100);
    return k.base;
}

uint32_t ink(int c, uint32_t usual) {
    const Colour& k = info(c);
    return c >= 0 && k.fx == Fx::Wire ? mix(k.base, k.hi, 110) : usual;
}

namespace {
uint32_t g_now = 0;
}
void setNow(uint32_t ms) { g_now = ms; }
uint32_t now() { return g_now; }

uint32_t readable(uint32_t flat) {
    for (int i = kClassic; i < kCount; ++i)
        if (kColours[i].base == flat) return text(i);
    return flat;
}

namespace {
float pulseGlow(uint32_t t) { return 0.5f + 0.5f * std::sin(kTau * float(t % 1600) / 1600.f); }
float pulseBeat(uint32_t t) {
    const float ph = float(t % 1200) / 1200.f;
    auto bump = [](float x, float c, float wd) {
        const float u = (x - c) / wd;
        return u <= -1.f || u >= 1.f ? 0.f : (1.f - u * u) * (1.f - u * u);
    };
    return std::max(bump(ph, 0.08f, 0.09f), 0.65f * bump(ph, 0.28f, 0.09f));
}
float pulseFlick(uint32_t t) { return float(hash3((t % 4800) / 120, 5, 9) % 100) / 100.f; }
uint32_t pearlTint(float x, float y, uint32_t t) {
    static const uint32_t kTints[4] = {rgb(255, 204, 228), rgb(196, 255, 236), rgb(212, 204, 255), rgb(255, 228, 194)};
    float h = x * 0.045f + y * 0.028f + float(t % 4800) / 4800.f;
    h = (h - std::floor(h)) * 4.f;
    const int i0 = int(h) & 3;
    return mix(kTints[i0], kTints[(i0 + 1) & 3], int(255.f * smooth(h - std::floor(h))));
}
// a four-pointed glint (game units)
void glint(Canvas& c, float x, float y, float len, uint32_t col, float a) {
    if (a <= 0.02f || len < 0.5f) return;
    const int al = int(255.f * clamp01(a));
    const float th = std::max(0.6f, len * 0.14f);
    c.line(x - len, y, x + len, y, col, th, al);
    c.line(x, y - len, x, y + len, col, th, al);
    c.line(x - len * 0.4f, y - len * 0.4f, x + len * 0.4f, y + len * 0.4f, col, th * 0.8f, al * 3 / 5);
    c.line(x - len * 0.4f, y + len * 0.4f, x + len * 0.4f, y - len * 0.4f, col, th * 0.8f, al * 3 / 5);
    c.fillCircle(x, y, th * 1.1f, rgb(255, 255, 255), al);
}
}  // namespace

uint32_t edgeTone(int colour, uint32_t flat, float x, float y, bool line) {
    const Colour& k = info(colour);
    const uint32_t t = g_now;
    switch (k.fx) {
    case Fx::Sparkle: {  // a broad light sweeps across the whole map every 2.4 s
        const float ph = float(t % 2400) / 2400.f;
        const float pos = -200.f + ph * 1400.f, d = x + y * 0.6f;
        const float s = smooth(1.f - std::fabs(d - pos) / 60.f);
        return mix(flat, k.hi, int(255.f * s * (line ? 0.9f : 0.6f)));
    }
    case Fx::Glow:
        return mix(flat, k.hi, int(255.f * (0.08f + 0.3f * pulseGlow(t))));
    case Fx::Heartbeat:
        return mix(flat, k.hi, int(255.f * 0.6f * pulseBeat(t)));
    case Fx::Pearl: {
        const uint32_t tint = pearlTint(x, y, t);
        const int l = lum(flat) + 20;
        return mix(flat, rgb(std::min(255, R(tint) * l >> 8), std::min(255, G(tint) * l >> 8), std::min(255, B(tint) * l >> 8)), 170);
    }
    case Fx::Arcs:
        return mix(flat, k.hi, int(255.f * 0.25f * pulseFlick(t)));
    case Fx::Void:
        return line ? mix(k.hi, rgb(255, 255, 255), int(60.f * (0.5f + 0.5f * std::sin(kTau * float(t % 4800) / 4800.f)))) : flat;
    case Fx::Sunset: {  // (0.75.0) slow waves of orange and purple along the border
        const float f = 0.5f + 0.5f * std::sin(x * 0.013f + y * 0.02f - kTau * float(t % 4800) / 4800.f);
        const uint32_t col = relight(flat, sunsetAt(k, f), lum(k.base));
        return line ? mix(col, rgb(255, 255, 255), 50) : col;
    }
    case Fx::Crystal: {  // facets of 7 units, lit as the light turns
        const uint32_t hh = hash3(uint32_t(int(std::floor(x / 7.f)) + 4096), uint32_t(int(std::floor(y / 7.f)) + 4096), 17);
        const float la = kTau * float(t % 4800) / 4800.f;
        const float d = (float(hh & 255) / 127.5f - 1.f) * std::cos(la) + (float(hh >> 8 & 255) / 127.5f - 1.f) * std::sin(la);
        if (d > 0.f) return mix(flat, k.hi, int(255.f * clamp01(d * (line ? 0.6f : 0.4f))));
        return mix(flat, k.lo, int(255.f * clamp01(-d * 0.35f)));
    }
    case Fx::Ash: {  // grey, the bright edge glowing like embers here and there
        const float n = std::sin(x * 0.11f + 1.3f) * std::sin(y * 0.13f) + 0.5f * std::sin((x + y) * 0.05f);
        if (!line) return mix(flat, k.lo, int(255.f * clamp01(-n * 0.35f)));
        const float glow = clamp01((n - 0.05f) * 1.8f) * (0.5f + 0.5f * std::sin(kTau * float(t % 2400) / 2400.f + x * 0.02f));
        return mix(flat, k.hi, int(255.f * glow * 0.95f));
    }
    case Fx::Prism: {
        const uint32_t col = rainbow()[int((x * 0.008f + y * 0.005f + float(t % 4800) / 4800.f) * 256.f) & 255];
        return line ? mix(col, rgb(255, 255, 255), 70) : relight(flat, col, lum(k.base));
    }
    case Fx::Wire: {  // the line bright (brighter as the scan passes), the band dark
        if (!line) return mix(flat, rgb(0, 0, 0), 120);
        const float scan = -100.f + float(t % 2400) / 2400.f * 800.f, s = smooth(1.f - std::fabs(y - scan) / 40.f);
        return mix(mix(k.base, k.hi, 150), rgb(255, 255, 255), int(200.f * s));
    }
    case Fx::None:
        break;
    }
    return flat;
}

float edgeGlow(int colour) {
    switch (info(colour).fx) {
    case Fx::Glow: return 1.f + 0.9f * pulseGlow(g_now);
    case Fx::Heartbeat: return 0.7f + 1.6f * pulseBeat(g_now);
    case Fx::Arcs: return 1.f + 0.4f * pulseFlick(g_now);
    case Fx::Void: return 1.6f;
    default: return 1.f;
    }
}

void edgeExtras(Canvas& c, int colour, float x1, float y1, float x2, float y2, float nx, float ny, float band, uint32_t seed) {
    const Colour& k = info(colour);
    const uint32_t t = g_now;
    switch (k.fx) {
    case Fx::Sparkle: {  // now and then a glint somewhere on the edge
        const uint32_t cyc = 1600, off = hash3(seed, 3, 7) % cyc, tt = t + off, ci = (tt / cyc) % 3;
        const float ph = float(tt % cyc) / float(cyc);
        if (ph > 0.3f) break;
        const float e = std::sin(3.14159265f * ph / 0.3f), u = 0.15f + 0.7f * float(hash3(seed, ci, 11) % 1000) / 1000.f;
        const uint32_t spark = k.base == rgb(184, 190, 204) ? rgb(255, 255, 255) : mix(k.hi, rgb(255, 255, 255), 140);
        glint(c, x1 + (x2 - x1) * u + nx * band * 0.15f, y1 + (y2 - y1) * u + ny * band * 0.15f, std::min(4.5f, band * 0.3f) * (0.5f + 0.5f * e), spark, e);
        break;
    }
    case Fx::Glow:
    case Fx::Heartbeat: {  // the glow spills a little outside the territory
        const float g = k.fx == Fx::Glow ? 0.3f + 0.6f * pulseGlow(t) : 0.1f + 0.9f * pulseBeat(t);
        for (int i = 0; i < 2; ++i) {
            const float o1 = band * 0.18f * float(i), o2 = band * 0.18f * float(i + 1);
            float xs[4] = {x1 - nx * o1, x2 - nx * o1, x2 - nx * o2, x1 - nx * o2};
            float ys[4] = {y1 - ny * o1, y2 - ny * o1, y2 - ny * o2, y1 - ny * o2};
            c.fillPolygon(xs, ys, 4, k.lo, int(g * (i ? 40.f : 85.f)));
        }
        break;
    }
    case Fx::Arcs: {  // now and then a spark runs along part of the edge
        const uint32_t slot = (t % 4800) / 120, hh = hash3(seed, slot, 21);
        if (hh % 100 >= 22) break;
        const float a = float(hh >> 8 & 255) / 255.f * 0.6f, b = a + 0.25f + float(hh >> 16 & 255) / 255.f * 0.15f;
        float px = x1 + (x2 - x1) * a, py = y1 + (y2 - y1) * a;
        const int steps = 5;
        for (int s = 1; s <= steps; ++s) {
            const float u = a + (b - a) * float(s) / float(steps);
            const float j = (float(hash3(hh, uint32_t(s), 4) % 1000) / 1000.f - 0.5f) * band * 0.5f;
            const float qx = x1 + (x2 - x1) * u + nx * j, qy = y1 + (y2 - y1) * u + ny * j;
            c.line(px, py, qx, qy, k.lo, 2.2f, 90);
            c.line(px, py, qx, qy, k.hi, 0.9f, 240);
            px = qx, py = qy;
        }
        break;
    }
    case Fx::Crystal: {  // (0.75.0) a white glint now and then, like the metals
        const uint32_t cyc = 2000, off = hash3(seed, 5, 13) % cyc, tt = t + off, ci = (tt / cyc) % 3;
        const float ph = float(tt % cyc) / float(cyc);
        if (ph > 0.25f) break;
        const float e = std::sin(3.14159265f * ph / 0.25f), u = 0.15f + 0.7f * float(hash3(seed, ci, 19) % 1000) / 1000.f;
        glint(c, x1 + (x2 - x1) * u + nx * band * 0.15f, y1 + (y2 - y1) * u + ny * band * 0.15f, std::min(4.f, band * 0.28f) * (0.5f + 0.5f * e),
              rgb(255, 255, 255), e);
        break;
    }
    case Fx::Ash: {  // (0.75.0) an ember drifts up off the edge now and then and fades
        for (uint32_t i = 0; i < 2; ++i) {
            const uint32_t cyc = 2400, hs = hash3(seed, i, 47), tt = t + hs % cyc, ci = (tt / cyc) % 4;
            const uint32_t hc = hash3(seed, i * 8u + ci, 53);
            if (hc % 100 >= 55) continue;
            const float ph = float(tt % cyc) / float(cyc), u = 0.1f + 0.8f * float(hc >> 8 & 1023) / 1023.f;
            const float sx = x1 + (x2 - x1) * u + std::sin(ph * 9.f + float(hc & 7)) * 1.5f, sy = y1 + (y2 - y1) * u - ph * 14.f;
            const float a = std::sin(3.14159265f * ph) * 0.9f;
            c.fillCircle(sx, sy, 0.8f, mix(k.hi, rgb(255, 230, 150), int(255.f * (1.f - ph))), int(255.f * a));
        }
        break;
    }
    case Fx::Void: {  // a few stars in the band
        static const uint32_t kPeriods[4] = {1200, 1600, 2400, 4800};
        for (uint32_t i = 0; i < 3; ++i) {
            const uint32_t hs = hash3(seed, i, 31);
            const float u = 0.1f + 0.8f * float(hs & 1023) / 1023.f, in = band * (0.15f + 0.6f * float(hs >> 10 & 255) / 255.f);
            const uint32_t per = kPeriods[hs >> 24 & 3];
            const float tw = 0.5f + 0.5f * std::sin(kTau * float((t + hs) % per) / float(per));
            c.fillCircle(x1 + (x2 - x1) * u + nx * in, y1 + (y2 - y1) * u + ny * in, 0.5f + 0.5f * tw, (hs >> 4 & 1) ? rgb(255, 255, 255) : rgb(200, 180, 255),
                         int(90.f + 165.f * tw));
        }
        break;
    }
    default:
        break;
    }
}

int of(const ti::Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return -1;
    const int c = g.players[size_t(p)].color;
    return c >= 0 && c < kCount ? c : -1;
}

Finish::Finish(Canvas& c, Rect r, int colour, uint32_t seed, int margin) {
    if (colour < 0 || colour >= kCount || info(colour).fx == Fx::None) return;
    const Fx fx = info(colour).fx;
    const int m = margin >= 0 ? margin : fx == Fx::Glow || fx == Fx::Heartbeat ? 6 : fx == Fx::Arcs ? 6 : fx == Fx::Void || fx == Fx::Ash ? 4
                                       : fx == Fx::Wire                                       ? 3
                                                                                              : 2;
    const Rect p = c.toPx(Rect{r.x - m, r.y - m, r.w + 2 * m, r.h + 2 * m});
    const Rect cl = c.pxClip();
    const int xa = std::max(p.x, cl.x), ya = std::max(p.y, cl.y);
    const int xb = std::min(p.x + p.w, cl.x + cl.w), yb = std::min(p.y + p.h, cl.y + cl.h);
    if (xb <= xa || yb <= ya) return;
    c_ = &c;
    colour_ = colour;
    x0_ = xa;
    y0_ = ya;
    w_ = xb - xa;
    h_ = yb - ya;
    now_ = g_now;
    seed_ = seed;
    margin_ = float(m) * c.scale();
    before_.resize(size_t(w_ * h_));
    const uint32_t* px = c.pixels();
    for (int y = 0; y < h_; ++y)
        std::copy(px + size_t(y0_ + y) * size_t(c.pw()) + x0_, px + size_t(y0_ + y) * size_t(c.pw()) + x0_ + w_,
                  before_.begin() + y * w_);
}

Finish::~Finish() {
    if (!c_) return;
    const Colour& k = info(colour_);
    const int w = w_, h = h_, stride = c_->pw();
    const float sc = c_->scale();
    uint32_t* px = c_->pixels();
    t_mask.assign(size_t(w * h), 0);
    const uint8_t* lut = familyLut(colour_);
    const int cs = std::max(1, int(sc + 0.25f));  // the halo's cells: about a game unit
    int bx0 = w, by0 = h, bx1 = -1, by1 = -1, count = 0;
    double sumX = 0, sumY = 0, sumW = 0;  // (0.80.0) for the anchor of the patterns that belong to the element
    for (int y = 0; y < h; ++y) {
        const uint32_t* row = px + size_t(y0_ + y) * size_t(stride) + x0_;
        const uint32_t* was = before_.data() + size_t(y * w);
        for (int x = 0; x < w; ++x) {
            if (row[x] == was[x]) continue;
            const int f = lut[lutIndex(row[x])];
            if (f <= 0) continue;
            t_mask[size_t(y * w + x)] = uint8_t(f);
            if (f > 100) {
                bx0 = std::min(bx0, x), bx1 = std::max(bx1, x), by0 = std::min(by0, y), by1 = std::max(by1, y);
                ++count;
                sumX += double(x) * f, sumY += double(y) * f, sumW += f;
            }
        }
    }
    if (count == 0) return;
    const uint32_t t = now_;
    const float bw = float(bx1 - bx0 + 1), bh = float(by1 - by0 + 1);
    // (0.80.0) where the element is: its patterns (facets, plates, rainbow bands, the grid) are laid from here, so they stay put
    // on a moving ship - the bounding box changed size whenever something else crossed it, and the patterns jumped about
    const float ax = float(sumX / std::max(1.0, sumW)), ay = float(sumY / std::max(1.0, sumW));
    auto each = [&](auto fn) {  // fn(pixel&, weight 0..1, x, y)
        for (int y = by0; y <= by1; ++y) {
            uint32_t* row = px + size_t(y0_ + y) * size_t(stride) + x0_;
            for (int x = bx0; x <= bx1; ++x) {
                const int m = t_mask[size_t(y * w + x)];
                if (m) fn(row[x], float(m) / 255.f, x, y);
            }
        }
    };
    auto plot = [&](float fxp, float fyp, uint32_t col, float a) {
        const int x = int(fxp), y = int(fyp);
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.f) return;
        uint32_t& d = px[size_t(y0_ + y) * size_t(stride) + x0_ + x];
        d = mix(d, col, int(255.f * clamp01(a)));
    };
    // a light that runs across the element along a diagonal (metals, pearl): 0..1 at (x, y)
    auto sheen = [&](float x, float y, uint32_t period, float runShare, float width) {
        const float ph = float(t % period) / float(period);
        if (ph > runShare) return 0.f;
        const float d = (x - float(bx0)) + (y - float(by0)) * 0.6f, len = bw + bh * 0.6f;
        const float pos = -width * 2.f + ph / runShare * (len + width * 4.f);
        return smooth(1.f - std::fabs(d - pos) / width);
    };
    // n four-pointed glints that come and go on the element (each lit for `onShare` of its `cyc` ms, somewhere new each time)
    auto glints = [&](int n, uint32_t cyc, float onShare, uint32_t spark) {
        for (int i = 0; i < n; ++i) {
            const uint32_t off = hash3(seed_, uint32_t(i), 77) % cyc;
            const uint32_t tt = t + off, ci = (tt / cyc) % 6;
            const float ph = float(tt % cyc) / float(cyc);
            if (ph > onShare) continue;
            const float e = std::sin(3.14159265f * ph / onShare);
            int sx = -1, sy = -1;
            for (int tr = 0; tr < 10 && sx < 0; ++tr) {
                const uint32_t hh = hash3(seed_, uint32_t(i) * 16u + uint32_t(tr), ci + 1000u * uint32_t(colour_));
                const int x = bx0 + int(hh % uint32_t(bx1 - bx0 + 1)), y = by0 + int((hh >> 16) % uint32_t(by1 - by0 + 1));
                if (t_mask[size_t(y * w + x)] > 150) sx = x, sy = y;
            }
            if (sx < 0) continue;
            const float L = (1.f + 2.6f * e) * sc;
            plot(float(sx), float(sy), spark, e);
            for (int d = 1; d <= int(L); ++d) {
                const float fall = 1.f - float(d) / (L + 1.f), a = e * fall * fall;
                plot(float(sx + d), float(sy), spark, a);
                plot(float(sx - d), float(sy), spark, a);
                plot(float(sx), float(sy + d), spark, a);
                plot(float(sx), float(sy - d), spark, a);
                if (float(d) < L * 0.45f) {
                    plot(float(sx + d), float(sy + d), spark, a * 0.6f);
                    plot(float(sx - d), float(sy - d), spark, a * 0.6f);
                    plot(float(sx + d), float(sy - d), spark, a * 0.6f);
                    plot(float(sx - d), float(sy + d), spark, a * 0.6f);
                }
            }
        }
    };

    switch (k.fx) {
    case Fx::Sparkle: {
        // metal: lighter at the top, darker at the bottom; a light runs over it every 2.4 s; glints come and go
        each([&](uint32_t& p, float wgt, int x, int y) {
            const float v = (float(y - by0) + 0.5f) / bh;
            if (v < 0.45f) p = mix(p, k.hi, int(255.f * wgt * (0.45f - v) * 0.55f));
            else p = mix(p, k.lo, int(255.f * wgt * (v - 0.45f) * 0.45f));
            const float s = sheen(float(x), float(y), 2400, 0.55f, 5.f * sc);
            if (s > 0.f) p = mix(p, k.hi, int(255.f * wgt * s * 0.8f));
        });
        const float areaUnits = float(count) / (sc * sc);
        const int n = std::clamp(int(areaUnits / 160.f) + 1, 1, 6);
        const uint32_t spark = k.base == rgb(184, 190, 204) ? rgb(255, 255, 255) : mix(k.hi, rgb(255, 255, 255), 140);
        glints(n, 800, 0.5f, spark);
        break;
    }
    case Fx::Glow: {
        const float g = 0.5f + 0.5f * std::sin(kTau * float(t % 1600) / 1600.f);
        distanceField(w, h, cs, int(margin_ * 10.f / float(cs)) + 10);
        halo(px, stride, x0_, y0_, w, h, cs, margin_, k.lo, [&](float f) { return f * (0.28f + 0.5f * g); });
        each([&](uint32_t& p, float wgt, int, int) { p = mix(p, k.hi, int(255.f * wgt * (0.06f + 0.34f * g))); });
        break;
    }
    case Fx::Heartbeat: {
        // lub-dub: a strong beat, a weaker one right after, then rest (1.2 s)
        const float ph = float(t % 1200) / 1200.f;
        auto bump = [](float x, float c, float wd) {
            const float u = (x - c) / wd;
            return u <= -1.f || u >= 1.f ? 0.f : (1.f - u * u) * (1.f - u * u);
        };
        const float b = std::max(bump(ph, 0.08f, 0.09f), 0.65f * bump(ph, 0.28f, 0.09f));
        distanceField(w, h, cs, int(margin_ * 10.f / float(cs)) + 10);
        halo(px, stride, x0_, y0_, w, h, cs, margin_, k.lo, [&](float f) { return f * (0.12f + 0.72f * b); });
        each([&](uint32_t& p, float wgt, int, int) { p = mix(p, k.hi, int(255.f * wgt * (0.02f + 0.55f * b))); });
        break;
    }
    case Fx::Pearl: {
        // the tint cycle (pink, mint, lavender, peach) in 256 steps, once
        static uint32_t ramp[256];
        static bool made = false;
        if (!made) {
            static const uint32_t kTints[4] = {rgb(255, 204, 228), rgb(196, 255, 236), rgb(212, 204, 255), rgb(255, 228, 194)};
            for (int i = 0; i < 256; ++i) {
                const float h = float(i) / 64.f;
                const int i0 = int(h) & 3, i1 = (i0 + 1) & 3;
                ramp[i] = mix(kTints[i0], kTints[i1], int(255.f * smooth(h - std::floor(h))));
            }
            made = true;
        }
        const float drift = float(t % 4800) / 4800.f;
        each([&](uint32_t& p, float wgt, int x, int y) {
            const float hpos = (float(x0_ + x) * 0.045f + float(y0_ + y) * 0.028f) / sc + drift;
            const uint32_t tint = ramp[int(hpos * 256.f) & 255];
            const int l = lum(p) + 20;
            const uint32_t lit = rgb(std::min(255, R(tint) * l >> 8), std::min(255, G(tint) * l >> 8), std::min(255, B(tint) * l >> 8));
            p = mix(p, lit, int(158.f * wgt));
            const float s = sheen(float(x), float(y), 4800, 0.5f, 7.f * sc);
            if (s > 0.f) p = mix(p, k.hi, int(255.f * wgt * s * 0.55f));
        });
        break;
    }
    case Fx::Arcs: {
        const uint32_t slot = (t % 4800) / 120;
        const float flick = float(hash3(slot, 5, 9) % 100) / 100.f;
        distanceField(w, h, cs, int(margin_ * 10.f / float(cs)) + 10);
        halo(px, stride, x0_, y0_, w, h, cs, margin_ * 0.75f, k.lo, [&](float f) { return f * (0.18f + 0.16f * flick); });
        each([&](uint32_t& p, float wgt, int, int) { p = mix(p, k.hi, int(255.f * wgt * (0.04f + 0.18f * flick))); });
        // the arcs: from a point on the edge, outward, in 3-4 jagged steps; a new pair every 120 ms, not always both
        t_edge.clear();
        for (int y = by0; y <= by1; ++y)
            for (int x = bx0; x <= bx1; ++x) {
                if (t_mask[size_t(y * w + x)] <= 128) continue;
                const bool out = x == 0 || y == 0 || x == w - 1 || y == h - 1 || t_mask[size_t(y * w + x - 1)] < 64 ||
                                 t_mask[size_t(y * w + x + 1)] < 64 || t_mask[size_t((y - 1) * w + x)] < 64 ||
                                 t_mask[size_t((y + 1) * w + x)] < 64;
                if (out) t_edge.push_back(y * w + x);
            }
        if (t_edge.empty()) break;
        const float cx = float(bx0 + bx1) / 2.f, cy = float(by0 + by1) / 2.f;
        const int arcs = std::clamp(int(float(count) / (sc * sc) / 260.f) + 1, 1, 3);
        for (int k2 = 0; k2 < arcs; ++k2) {
            uint32_t hh = hash3(seed_ + uint32_t(colour_), slot, uint32_t(k2));
            if (hh % 100 >= 58) continue;
            const int e = t_edge[size_t((hh >> 7) % uint32_t(t_edge.size()))];
            float x = float(e % w) + 0.5f, y = float(e / w) + 0.5f;
            float ang = std::atan2(y - cy, x - cx);
            const int steps = 3 + int(hh >> 28 & 1);
            for (int s = 0; s < steps; ++s) {
                hh = hash3(hh, uint32_t(s), 3);
                ang += (float(hh % 1000) / 1000.f - 0.5f) * 1.8f;
                const float len = (1.4f + float((hh >> 10) % 100) / 100.f * 1.6f) * sc;
                const float nx = x + std::cos(ang) * len, ny = y + std::sin(ang) * len;
                const int n = std::max(1, int(len * 1.5f));
                for (int j = 0; j <= n; ++j) {
                    const float px2 = x + (nx - x) * float(j) / float(n), py2 = y + (ny - y) * float(j) / float(n);
                    const float a = 0.95f - 0.18f * float(s);
                    plot(px2, py2, k.hi, a);
                    if (sc >= 1.5f) {  // a little body round the bolt on big screens
                        plot(px2 + 1.f, py2, k.lo, a * 0.35f);
                        plot(px2, py2 + 1.f, k.lo, a * 0.35f);
                    }
                }
                x = nx, y = ny;
            }
            plot(x, y, rgb(255, 255, 255), 0.9f);
        }
        break;
    }
    case Fx::Void: {
        const float breathe = 0.5f + 0.5f * std::sin(kTau * float(t % 4800) / 4800.f);
        distanceField(w, h, cs, int(margin_ * 10.f / float(cs)) + 10);
        halo(px, stride, x0_, y0_, w, h, cs, margin_ * 0.7f, k.hi, [&](float f) { return f * (0.35f + 0.2f * breathe); });
        // a faint violet nebula drifting through it: sin(a u + d) sin(b v - d) + 0.5 sin(c (u + v) + d), from per-column and
        // per-row tables (sin(x + y) = sin x cos y + cos x sin y)
        const float dr = kTau * float(t % 4800) / 4800.f;
        t_colA.resize(size_t(w)), t_colB.resize(size_t(w)), t_colC.resize(size_t(w));
        t_rowA.resize(size_t(h)), t_rowB.resize(size_t(h)), t_rowC.resize(size_t(h));
        for (int x = bx0; x <= bx1; ++x) {
            const float u = float(x0_ + x) / sc;
            t_colA[size_t(x)] = std::sin(u * 0.23f + dr), t_colB[size_t(x)] = std::sin(u * 0.11f + dr), t_colC[size_t(x)] = std::cos(u * 0.11f + dr);
        }
        for (int y = by0; y <= by1; ++y) {
            const float v = float(y0_ + y) / sc;
            t_rowA[size_t(y)] = std::sin(v * 0.19f - dr), t_rowB[size_t(y)] = std::cos(v * 0.11f), t_rowC[size_t(y)] = std::sin(v * 0.11f);
        }
        each([&](uint32_t& p, float wgt, int x, int y) {
            const float n = t_colA[size_t(x)] * t_rowA[size_t(y)] +
                            0.5f * (t_colB[size_t(x)] * t_rowB[size_t(y)] + t_colC[size_t(x)] * t_rowC[size_t(y)]);
            if (n > 0.2f) p = mix(p, rgb(92, 52, 150), int(255.f * wgt * clamp01((n - 0.2f) * 0.5f)));
        });
        // stars, fixed to the screen (a window into space) and twinkling: one cell of 2.2 units in five has one
        static const uint32_t kPeriods[4] = {1200, 1600, 2400, 4800};
        const float cell = 2.2f, rad = 0.35f + 0.5f / sc;
        const int gx0 = int(std::floor(float(x0_ + bx0) / sc / cell)), gx1 = int(std::floor(float(x0_ + bx1) / sc / cell));
        const int gy0 = int(std::floor(float(y0_ + by0) / sc / cell)), gy1 = int(std::floor(float(y0_ + by1) / sc / cell));
        for (int gy = gy0; gy <= gy1; ++gy)
            for (int gx = gx0; gx <= gx1; ++gx) {
                const uint32_t hs = hash3(uint32_t(gx), uint32_t(gy), 41);
                if (hs % 5 != 0) continue;
                const float sxp = (float(gx) + float(hs >> 8 & 255) / 255.f) * cell * sc - float(x0_);
                const float syp = (float(gy) + float(hs >> 16 & 255) / 255.f) * cell * sc - float(y0_);
                const uint32_t per = kPeriods[hs >> 24 & 3];
                const float tw = 0.5f + 0.5f * std::sin(kTau * float((t + hs) % per) / float(per));
                const uint32_t star = (hs >> 4 & 1) ? rgb(255, 255, 255) : rgb(200, 180, 255);
                const float rp = rad * sc;
                for (int y = std::max(by0, int(syp - rp)); y <= std::min(by1, int(syp + rp)); ++y)
                    for (int x = std::max(bx0, int(sxp - rp)); x <= std::min(bx1, int(sxp + rp)); ++x) {
                        const int m = t_mask[size_t(y * w + x)];
                        if (!m) continue;
                        const float d = (std::fabs(float(x) + 0.5f - sxp) + std::fabs(float(y) + 0.5f - syp)) / rp;
                        if (d > 1.f) continue;
                        uint32_t& q = px[size_t(y0_ + y) * size_t(stride) + x0_ + x];
                        q = mix(q, star, int(float(m) * (1.f - d) * (0.25f + 0.75f * tw)));
                    }
            }
        break;
    }
    case Fx::Sunset: {
        // (0.75.0) orange on top melting into purple below, the line between them rising and sinking (4.8 s); on elements
        // taller than 9 units the lower half has the dark bands of a retro sun, thicker toward the bottom, drifting down
        const int bl = lum(k.base);
        const float split = 0.5f + 0.12f * std::sin(kTau * float(t % 4800) / 4800.f);
        uint32_t ramp[64];
        for (int i = 0; i < 64; ++i) ramp[i] = sunsetAt(k, ((float(i) + 0.5f) / 64.f - split) / 0.55f + 0.5f);
        const bool bands = bh >= 9.f * sc;
        const float bandW = 2.8f * sc, scroll = float(t % 2400) / 2400.f * bandW;
        each([&](uint32_t& p, float wgt, int x, int y) {
            const float v = clamp01(0.5f + (float(y) + 0.5f - ay) / bh);
            uint32_t q = relight(p, ramp[std::min(63, int(v * 64.f))], bl);
            if (bands && v > 0.52f) {
                const float ph = std::fmod(float(y) - ay - scroll + bandW * 64.f, bandW) / bandW;
                if (ph < 0.12f + 0.42f * (v - 0.52f) / 0.48f) q = mix(q, rgb(36, 12, 58), 170);
            }
            (void)x;
            p = mix(p, q, int(255.f * wgt));
        });
        break;
    }
    case Fx::Crystal: {
        // (0.75.0) cut ice: triangular facets about 3 units across, each tilted its own way; a light turning round (4.8 s)
        // brightens the facets that face it and shades the others; a cold sheen runs over it and glints come and go
        const float cell = std::max(5.f, 5.5f * sc);
        const float la = kTau * float(t % 4800) / 4800.f, lx = std::cos(la), ly = std::sin(la);
        const uint32_t sd = seed_ * 31u + 5u;
        const float edge = std::max(0.6f, 0.35f * sc) / cell;  // the width of a cut edge, in cells
        each([&](uint32_t& p, float wgt, int x, int y) {
            const float u = (float(x) + 0.5f - ax) / cell + 64.f, v = (float(y) + 0.5f - ay) / cell + 64.f;
            const int iu = int(u), iv = int(v);
            const float fu = u - float(iu), fv = v - float(iv);
            const int tri = fu + fv > 1.f ? 1 : 0;
            const uint32_t hh = hash3(uint32_t(iu * 2 + tri), uint32_t(iv), sd);
            const float d = (float(hh & 255) / 127.5f - 1.f) * lx + (float(hh >> 8 & 255) / 127.5f - 1.f) * ly;
            uint32_t q = d > 0.f ? mix(p, k.hi, int(255.f * clamp01(d * 0.8f))) : mix(p, k.lo, int(255.f * clamp01(-d * 0.65f)));
            if (fu < edge || fv < edge || std::fabs(fu + fv - 1.f) < edge * 0.8f) q = mix(q, rgb(255, 255, 255), 120);  // the cut edges
            const float s = sheen(float(x), float(y), 2400, 0.4f, 3.f * sc);
            if (s > 0.f) q = mix(q, rgb(255, 255, 255), int(200.f * s));
            p = mix(p, q, int(255.f * wgt));
        });
        const int n = std::clamp(int(float(count) / (sc * sc) / 200.f) + 1, 1, 4);
        glints(n, 1000, 0.4f, rgb(255, 255, 255));
        break;
    }
    case Fx::Ash: {
        // (0.75.0) a crust of grey ash, mottled darker and lighter, broken into plates about 4 units across (the edges of the
        // cells round jittered points: where the nearest two points are about as near); the embers show in the cracks, glowing
        // brighter and dimmer along them (2.4 s) and warming the ash beside them; now and then a spark rises off the top
        const float dr = kTau * float(t % 2400) / 2400.f;
        const float so = float(seed_ % 97u) * 0.37f;
        t_colA.resize(size_t(w)), t_colB.resize(size_t(w)), t_colC.resize(size_t(w)), t_colD.resize(size_t(w));
        t_rowA.resize(size_t(h)), t_rowB.resize(size_t(h)), t_rowC.resize(size_t(h));
        for (int x = bx0; x <= bx1; ++x) {
            const float u = (float(x) - ax) / sc + so;
            t_colA[size_t(x)] = std::sin(u * 0.62f), t_colB[size_t(x)] = std::sin(u * 0.23f + 1.3f), t_colC[size_t(x)] = std::cos(u * 0.23f + 1.3f);
            t_colD[size_t(x)] = 0.55f + 0.45f * std::sin(dr + u * 0.35f);
        }
        for (int y = by0; y <= by1; ++y) {
            const float v = (float(y) - ay) / sc;
            t_rowA[size_t(y)] = std::sin(v * 0.55f + 0.7f), t_rowB[size_t(y)] = std::cos(v * 0.31f), t_rowC[size_t(y)] = std::sin(v * 0.31f);
        }
        // the plates' points: one per cell of 5.5 units, jittered, on a grid laid from the element's centre (in pixels)
        const float cell = 5.5f * sc;
        const float gx0 = ax - std::ceil((ax - float(bx0)) / cell + 1.f) * cell, gy0 = ay - std::ceil((ay - float(by0)) / cell + 1.f) * cell;
        const int gw = int((float(bx1) - gx0) / cell) + 3, gh = int((float(by1) - gy0) / cell) + 3;
        const int ox = int(std::lround((gx0 - ax) / cell)), oy = int(std::lround((gy0 - ay) / cell));  // (the cells' names stay the same)
        static std::vector<float> pts;
        pts.resize(size_t(gw * gh * 2));
        for (int gy = 0; gy < gh; ++gy)
            for (int gx = 0; gx < gw; ++gx) {
                const uint32_t hh = hash3(uint32_t(gx + ox + 4096), uint32_t(gy + oy + 4096), seed_ * 7u + 3u);
                pts[size_t((gy * gw + gx) * 2)] = gx0 + (float(gx) + 0.15f + 0.7f * float(hh & 1023) / 1023.f) * cell;
                pts[size_t((gy * gw + gx) * 2 + 1)] = gy0 + (float(gy) + 0.15f + 0.7f * float(hh >> 10 & 1023) / 1023.f) * cell;
            }
        const float crackW = std::max(1.f, 0.7f * sc), warmW = 1.8f * sc;
        const uint32_t core = rgb(255, 214, 120);
        each([&](uint32_t& p, float wgt, int x, int y) {
            const float n = t_colA[size_t(x)] * t_rowA[size_t(y)] + 0.6f * (t_colB[size_t(x)] * t_rowB[size_t(y)] + t_colC[size_t(x)] * t_rowC[size_t(y)]);
            uint32_t q = n < -0.3f ? mix(p, k.lo, int(255.f * clamp01((-0.3f - n) * 0.6f))) : n > 0.7f ? mix(p, rgb(196, 190, 184), int(255.f * clamp01((n - 0.7f) * 0.5f))) : p;
            const float lx = float(x) + 0.5f, ly = float(y) + 0.5f;
            const int cx2 = int((lx - gx0) / cell), cy2 = int((ly - gy0) / cell);
            float d1 = 1e9f, d2 = 1e9f;
            int i1 = 0, i2 = 0;
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) {
                    const int gx = cx2 + ox, gy = cy2 + oy;
                    if (gx < 0 || gy < 0 || gx >= gw || gy >= gh) continue;
                    const int gi = gy * gw + gx;
                    const float ddx = pts[size_t(gi * 2)] - lx, ddy = pts[size_t(gi * 2 + 1)] - ly;
                    const float dd = ddx * ddx + ddy * ddy;
                    if (dd < d1) d2 = d1, i2 = i1, d1 = dd, i1 = gi;
                    else if (dd < d2) d2 = dd, i2 = gi;
                }
            const float gap = std::sqrt(d2) - std::sqrt(d1);  // 0 on a crack
            if (gap < warmW || gap < crackW) {
                // each crack (between two plates) burns its own way: some cold and dark, most glowing, a few hot
                const int a1 = (i1 % gw + ox + 4096) * 8192 + i1 / gw + oy + 4096, a2 = (i2 % gw + ox + 4096) * 8192 + i2 / gw + oy + 4096;
                const float heat = float(hash3(uint32_t(std::min(a1, a2)), uint32_t(std::max(a1, a2)), seed_ + 11u) % 1000) / 1000.f;
                const float pulse = t_colD[size_t(x)];
                if (heat < 0.3f) {
                    if (gap < crackW) q = mix(q, k.lo, int(255.f * 0.8f * (1.f - gap / crackW)));
                } else {
                    const float hot = (heat - 0.3f) / 0.7f;
                    if (gap < warmW) q = mix(q, k.hi, int(255.f * 0.14f * (1.f - gap / warmW) * pulse * hot));
                    if (gap < crackW) {
                        const float e = (1.f - gap / crackW) * (0.3f + 0.7f * pulse) * (0.45f + 0.55f * hot);
                        q = mix(q, k.hi, int(255.f * clamp01(e * 1.4f)));
                        if (e > 0.5f) q = mix(q, core, int(255.f * clamp01((e - 0.5f) * 1.8f)));
                    }
                }
            }
            p = mix(p, q, int(255.f * wgt));
        });
        const int sparks = std::clamp(int(float(count) / (sc * sc) / 220.f) + 1, 1, 3);
        for (int i = 0; i < sparks; ++i) {
            const uint32_t cyc = 1800 + 300 * uint32_t(i), off = hash3(seed_, uint32_t(i), 91) % cyc, tt = t + off, ci = (tt / cyc) % 5;
            const uint32_t hh = hash3(seed_ + uint32_t(colour_), uint32_t(i) * 8u + ci, 97);
            if (hh % 100 >= 70) continue;
            const int sx = bx0 + int((hh >> 8) % uint32_t(bx1 - bx0 + 1));
            int top = -1;
            for (int y = by0; y <= by1 && top < 0; ++y)
                if (t_mask[size_t(y * w + sx)] > 100) top = y;
            if (top < 0) continue;
            const float ph = float(tt % cyc) / float(cyc);
            const float px2 = float(sx) + std::sin(ph * 8.f + float(hh & 7)) * 0.9f * sc, py2 = float(top) - ph * 5.f * sc;
            const float a = std::sin(3.14159265f * ph) * 0.95f;
            const uint32_t col = mix(core, k.hi, int(255.f * ph));
            plot(px2, py2, col, a);
            if (sc >= 1.5f) plot(px2 + 1.f, py2, col, a * 0.6f), plot(px2, py2 + 1.f, col, a * 0.5f);
        }
        break;
    }
    case Fx::Prism: {
        // (0.75.0) a soft rainbow in diagonal bands drifting across (4.8 s), lit like the art under it; a white sheen of a
        // holographic foil runs over it
        const int bl = lum(k.base);
        const uint32_t* rb = rainbow();
        const float drift = float(t % 4800) / 4800.f;
        each([&](uint32_t& p, float wgt, int x, int y) {
            const float hp = ((float(x) - ax) * 0.045f + (float(y) - ay) * 0.028f) / sc + drift + 8.f;
            uint32_t q = relight(p, rb[int(hp * 256.f) & 255], bl);
            const float s = sheen(float(x), float(y), 2400, 0.45f, 4.f * sc);
            if (s > 0.f) q = mix(q, rgb(255, 255, 255), int(170.f * s));
            p = mix(p, q, int(255.f * wgt));
        });
        break;
    }
    case Fx::Wire: {
        // (0.75.0) a vector drawing: the element goes dark, every edge of it - outside and inside - becomes a bright line, a
        // faint grid lies over the inside and a scan line runs down it every 2.4 s; a soft glow round the outside
        const int bl = lum(k.base);
        const int r = std::max(1, int(sc * 0.55f + 0.5f)), grid = std::max(3, int(3.f * sc + 0.5f));
        const float scanY = float(by0) - 3.f * sc + float(t % 2400) / 2400.f * (bh + 6.f * sc), scanW = 1.3f * sc;
        const float pulse = 0.85f + 0.15f * std::sin(kTau * float(t % 2400) / 2400.f);
        const uint32_t line = mix(k.base, k.hi, 110), dark = mix(rgb(2, 5, 9), k.base, 24);
        {  // the element's own dark details inside it (a count's digits, a faction emblem) light up in the line colour - on the
           // dark inside they would vanish otherwise; "inside" = the colour within 3 units on all four sides
            const int D = int(3.f * sc) + 1;
            auto in = [&](int xx, int yy) { return xx >= 0 && yy >= 0 && xx < w && yy < h && t_mask[size_t(yy * w + xx)] > 100; };
            for (int y = by0; y <= by1; ++y) {
                uint32_t* row = px + size_t(y0_ + y) * size_t(stride) + x0_;
                const uint32_t* was = before_.data() + size_t(y * w);
                for (int x = bx0; x <= bx1; ++x) {
                    if (t_mask[size_t(y * w + x)] || row[x] == was[x]) continue;
                    const int l = lum(row[x]);
                    if (l >= 90) continue;
                    bool le = false, ri = false, up = false, dn = false;
                    for (int d = 1; d <= D && !(le && ri && up && dn); ++d)
                        le = le || in(x - d, y), ri = ri || in(x + d, y), up = up || in(x, y - d), dn = dn || in(x, y + d);
                    if (le && ri && up && dn) row[x] = mix(row[x], line, int(255.f * clamp01(float(90 - l) / 60.f)));
                }
            }
        }
        distanceField(w, h, cs, int(margin_ * 10.f / float(cs)) + 10);
        halo(px, stride, x0_, y0_, w, h, cs, margin_, k.lo, [&](float f) { return f * 0.3f * pulse; });
        auto m = [&](int xx, int yy) { return xx < 0 || yy < 0 || xx >= w || yy >= h ? 0 : int(t_mask[size_t(yy * w + xx)]); };
        each([&](uint32_t& p, float wgt, int x, int y) {
            const bool edge = m(x - r, y) < 64 || m(x + r, y) < 64 || m(x, y - r) < 64 || m(x, y + r) < 64;
            uint32_t q;
            if (edge) q = line;
            else {
                const float shade = std::clamp(float(lum(p)) / float(std::max(1, bl)), 0.4f, 1.3f);
                q = mix(dark, k.base, int(30.f * shade));
                if ((x - int(ax) + grid * 64) % grid == 0 || (y - int(ay) + grid * 64) % grid == 0) q = mix(q, k.base, 70);
            }
            const float sd = std::fabs(float(y) - scanY) / scanW;
            if (sd < 1.f) q = mix(q, edge ? rgb(255, 255, 255) : k.hi, int(255.f * (1.f - sd) * (edge ? 0.7f : 0.45f)));
            p = mix(p, q, int(255.f * wgt));
        });
        break;
    }
    case Fx::None:
        break;
    }
}

}  // namespace pcolours
