// League emblems - see emblems.h. Every shape is worked out in "medal units" (the medal's radius = 1, y down) and drawn with
// the canvas primitives, so the medals are sharp at every screen size.
#include "ui/emblems.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace emblems {
namespace {

constexpr float kPi = 3.14159265f;

struct Key {
    const char* key;
    const char* name;
    int metal;  // 0 bronze, 1 silver, 2 gold, 3 rose gold, -1 the league's colour over steel
};
const Key kKeys[] = {
    {"bronze", "Bronze emblem", 0},          {"silver", "Silver emblem", 1},       {"gold", "Gold emblem", 2},
    {"ultimate", "Ultimate emblem", 3},      {"crown", "Imperial Crown emblem", 2}, {"laurel", "SCPT Laurel emblem", -1},
    {"starfarer", "Starfarer emblem", -1},   {"crest", "Invitational Crest emblem", -1}, {"lantern", "Lantern emblem", -1},
    {"bolt", "Lightning Bolt emblem", -1},   {"sun", "Retro Sun emblem", -1},      {"pearl", "Pearl Seal emblem", 1},
    {"circuit", "Circuit emblem", -1},       {"compass", "Drafting Compass emblem", -1}, {"shard", "Crystal Shard emblem", 1},
    {"ember", "Ember emblem", -1},
    // (0.85.2) the Blue Reverie leagues
    {"forge", "Living Forge emblem", -1},    {"superweapon", "Superweapon emblem", -1}, {"pyramid", "Black Pyramid emblem", 2},
};
const Key* keyOf(const std::string& k) {
    for (const Key& e : kKeys)
        if (k == e.key) return &e;
    return nullptr;
}

inline int R(uint32_t c) { return int((c >> 16) & 255); }
inline int G(uint32_t c) { return int((c >> 8) & 255); }
inline int B(uint32_t c) { return int(c & 255); }
uint32_t grey(uint32_t c) {
    const int l = (R(c) * 3 + G(c) * 6 + B(c)) / 10;
    return rgb(l, l, l);
}

// the drawing context: medal centre, radius, and the state's look
struct Pen {
    Canvas& c;
    float cx, cy, r;
    int alpha;   // 255, or less for the ghost of an emblem not yet earned
    bool mono;   // greyed
    uint32_t col(uint32_t k) const { return mono ? mix(grey(k), rgb(70, 76, 96), 110) : k; }
    float X(float u) const { return cx + u * r; }
    float Y(float v) const { return cy + v * r; }
    void circle(float u, float v, float rr, uint32_t k, int a = 255) const { c.fillCircle(X(u), Y(v), rr * r, col(k), a * alpha / 255); }
    void ring(float u, float v, float rr, float th, uint32_t k, int a = 255) const {
        c.ring(X(u), Y(v), rr * r, std::max(0.6f, th * r), col(k), a * alpha / 255);
    }
    void line(float u0, float v0, float u1, float v1, float th, uint32_t k, int a = 255) const {
        c.line(X(u0), Y(v0), X(u1), Y(v1), col(k), std::max(0.6f, th * r), a * alpha / 255);
    }
    void poly(const std::vector<float>& uv, uint32_t k, int a = 255) const {
        std::vector<float> xs, ys;
        for (size_t i = 0; i + 1 < uv.size(); i += 2) xs.push_back(X(uv[i])), ys.push_back(Y(uv[i + 1]));
        c.fillPolygon(xs.data(), ys.data(), int(xs.size()), col(k), a * alpha / 255);
    }
};
std::vector<float> star(int n, float ro, float ri, float u, float v, float rot = -kPi / 2.f) {
    std::vector<float> p;
    for (int i = 0; i < 2 * n; ++i) {
        const float a = rot + float(i) * kPi / float(n), rr = (i & 1) ? ri : ro;
        p.push_back(u + std::cos(a) * rr), p.push_back(v + std::sin(a) * rr);
    }
    return p;
}
std::vector<float> ellipse(float u, float v, float a, float b, float rot, int n = 14) {
    std::vector<float> p;
    for (int i = 0; i < n; ++i) {
        const float t = float(i) / float(n) * 2.f * kPi, x = std::cos(t) * a, y = std::sin(t) * b;
        p.push_back(u + x * std::cos(rot) - y * std::sin(rot)), p.push_back(v + x * std::sin(rot) + y * std::cos(rot));
    }
    return p;
}

void metalOf(int metal, uint32_t colour, uint32_t& base, uint32_t& hi, uint32_t& lo) {
    switch (metal) {
    case 0: base = rgb(184, 108, 50), hi = rgb(255, 196, 128), lo = rgb(92, 46, 16); return;
    case 1: base = rgb(184, 190, 204), hi = rgb(255, 255, 255), lo = rgb(86, 92, 108); return;
    case 2: base = rgb(214, 166, 50), hi = rgb(255, 240, 170), lo = rgb(112, 78, 14); return;
    case 3: base = rgb(218, 146, 132), hi = rgb(255, 228, 216), lo = rgb(124, 70, 64); return;
    default:  // steel washed with the league's colour
        base = mix(rgb(150, 156, 172), colour, 110), hi = mix(rgb(240, 244, 255), colour, 60), lo = mix(rgb(52, 56, 70), colour, 60);
    }
}

void symbol(const Pen& p, const std::string& k, uint32_t sym, uint32_t field, uint32_t colour, uint32_t now) {
    const uint32_t white = rgb(255, 255, 255);
    if (k == "bronze") p.poly(star(5, 0.44f, 0.18f, 0.f, 0.03f), sym);
    else if (k == "silver") {
        p.poly(star(5, 0.29f, 0.12f, -0.24f, 0.04f), sym);
        p.poly(star(5, 0.29f, 0.12f, 0.24f, 0.04f), sym);
    } else if (k == "gold") {
        p.poly(star(5, 0.27f, 0.11f, 0.f, -0.2f), sym);
        p.poly(star(5, 0.23f, 0.095f, -0.3f, 0.17f), sym);
        p.poly(star(5, 0.23f, 0.095f, 0.3f, 0.17f), sym);
    } else if (k == "ultimate") {
        p.poly(star(8, 0.52f, 0.17f, 0.f, 0.f), sym);
        p.poly(star(4, 0.3f, 0.1f, 0.f, 0.f, -kPi / 4.f), mix(sym, colour, 150));
        p.circle(0.f, 0.f, 0.1f, field);
        p.ring(0.f, 0.f, 0.1f, 0.035f, sym);
    } else if (k == "crown") {
        p.poly({-0.42f, 0.2f, -0.42f, -0.18f, -0.21f, 0.02f, 0.f, -0.3f, 0.21f, 0.02f, 0.42f, -0.18f, 0.42f, 0.2f}, sym);
        p.poly({-0.44f, 0.2f, 0.44f, 0.2f, 0.44f, 0.34f, -0.44f, 0.34f}, sym);
        p.line(-0.38f, 0.27f, 0.38f, 0.27f, 0.04f, field, 150);
        for (float u : {-0.42f, 0.f, 0.42f}) p.circle(u, u == 0.f ? -0.32f : -0.2f, 0.065f, sym);
        p.circle(0.f, 0.27f, 0.045f, colour);
    } else if (k == "laurel") {
        for (int side = -1; side <= 1; side += 2) {
            for (int i = 0; i < 6; ++i) {  // leaves from the bottom round each side, each along the wreath, tipped outwards
                const float t = 0.5f + float(i) * 0.4f, R0 = 0.37f;
                const float u = float(side) * std::sin(t) * R0, v = std::cos(t) * R0 + 0.04f;
                const float tu = float(side) * std::cos(t), tv = -std::sin(t);  // the wreath's direction there (upwards)
                const float ang = std::atan2(tv, tu) - float(side) * 0.45f;
                p.poly(ellipse(u + std::cos(ang) * 0.05f, v + std::sin(ang) * 0.05f, 0.12f, 0.05f, ang), sym);
            }
            for (int i = 0; i <= 8; ++i) {
                const float t = 0.4f + float(i) * 0.26f;
                p.line(float(side) * std::sin(t) * 0.36f, std::cos(t) * 0.36f + 0.04f, float(side) * std::sin(t + 0.26f) * 0.36f,
                       std::cos(t + 0.26f) * 0.36f + 0.04f, 0.025f, sym);
            }
        }
        p.poly(star(5, 0.17f, 0.07f, 0.f, -0.02f), sym);
    } else if (k == "starfarer") {
        auto ringArc = [&](bool front) {
            const int n = 24;
            for (int i = 0; i < n; ++i) {
                const float t0 = float(i) / float(n) * 2.f * kPi, t1 = float(i + 1) / float(n) * 2.f * kPi;
                if ((std::sin((t0 + t1) / 2.f) > 0.f) != front) continue;
                auto at = [](float t, float& u, float& v) {
                    const float x = std::cos(t) * 0.46f, y = std::sin(t) * 0.13f, a = -0.35f;
                    u = x * std::cos(a) - y * std::sin(a), v = x * std::sin(a) + y * std::cos(a) + 0.04f;
                };
                float u0, v0, u1, v1;
                at(t0, u0, v0), at(t1, u1, v1);
                p.line(u0, v0, u1, v1, 0.05f, sym);
            }
        };
        ringArc(false);
        p.circle(0.f, 0.04f, 0.24f, sym);
        p.circle(-0.07f, -0.03f, 0.1f, white, 70);
        ringArc(true);
        p.poly(star(4, 0.11f, 0.03f, 0.34f, -0.34f), white);
    } else if (k == "crest") {
        p.poly({-0.36f, -0.36f, 0.36f, -0.36f, 0.36f, 0.02f, 0.f, 0.44f, -0.36f, 0.02f}, sym);
        p.poly({-0.3f, 0.06f, 0.f, -0.16f, 0.3f, 0.06f, 0.3f, 0.18f, 0.f, -0.04f, -0.3f, 0.18f}, field);
        p.poly(star(5, 0.1f, 0.04f, 0.f, -0.24f), field);
    } else if (k == "lantern") {
        const uint32_t glow = mix(colour, white, 90);
        p.circle(0.f, 0.05f, 0.34f, glow, 50);
        p.ring(0.f, -0.36f, 0.075f, 0.035f, sym);
        p.poly({-0.17f, -0.3f, 0.17f, -0.3f, 0.22f, -0.21f, -0.22f, -0.21f}, sym);
        p.poly({-0.2f, -0.21f, 0.2f, -0.21f, 0.2f, 0.27f, -0.2f, 0.27f}, mix(field, glow, 60));
        p.circle(0.f, 0.04f, 0.13f, glow);
        p.circle(0.f, 0.04f, 0.06f, white, 220);
        for (float u : {-0.2f, 0.2f}) p.line(u, -0.21f, u, 0.27f, 0.045f, sym);
        p.poly({-0.24f, 0.27f, 0.24f, 0.27f, 0.2f, 0.36f, -0.2f, 0.36f}, sym);
    } else if (k == "bolt") {
        p.poly({0.1f, -0.47f, -0.23f, 0.06f, -0.01f, 0.06f, -0.1f, 0.47f, 0.24f, -0.09f, 0.02f, -0.09f, 0.16f, -0.47f}, sym);
        p.line(-0.05f, 0.1f, 0.02f, -0.1f, 0.03f, white, 140);
    } else if (k == "sun") {
        std::vector<float> half;
        for (int i = 0; i <= 16; ++i) {
            const float t = kPi + float(i) / 16.f * kPi;
            half.push_back(std::cos(t) * 0.36f), half.push_back(0.12f + std::sin(t) * 0.36f);
        }
        p.poly(half, mix(rgb(255, 164, 60), sym, 60));
        std::vector<float> lower;  // the lower part pinker
        for (int i = 0; i <= 16; ++i) {
            const float t = kPi + float(i) / 16.f * kPi, y = 0.12f + std::sin(t) * 0.36f;
            lower.push_back(std::cos(t) * 0.36f), lower.push_back(std::max(y, -0.06f));
        }
        p.poly(lower, rgb(238, 70, 128), 200);
        for (float v : {-0.02f, 0.05f, 0.1f}) p.line(-0.4f, v, 0.4f, v, 0.02f + (v + 0.02f) * 0.12f, field);
        p.line(-0.46f, 0.14f, 0.46f, 0.14f, 0.025f, sym);
        for (float u : {-0.32f, -0.12f, 0.12f, 0.32f}) p.line(u * 0.6f, 0.14f, u * 1.3f, 0.42f, 0.02f, sym, 160);
        p.line(-0.42f, 0.28f, 0.42f, 0.28f, 0.018f, sym, 140);
    } else if (k == "pearl") {
        const uint32_t wax = mix(rgb(176, 150, 210), colour, 80);
        for (int i = 0; i < 14; ++i) {
            const float a = float(i) / 14.f * 2.f * kPi;
            p.circle(std::cos(a) * 0.36f, std::sin(a) * 0.36f, 0.09f, wax);
        }
        p.circle(0.f, 0.f, 0.37f, wax);
        p.ring(0.f, 0.f, 0.28f, 0.02f, mix(wax, rgb(40, 30, 60), 90));
        p.circle(0.f, 0.f, 0.17f, rgb(236, 232, 244));
        p.circle(0.03f, 0.03f, 0.15f, rgb(206, 198, 226), 120);
        p.circle(-0.05f, -0.06f, 0.05f, white);
    } else if (k == "circuit") {
        p.poly({-0.14f, -0.14f, 0.14f, -0.14f, 0.14f, 0.14f, -0.14f, 0.14f}, sym);
        p.poly({-0.08f, -0.08f, 0.08f, -0.08f, 0.08f, 0.08f, -0.08f, 0.08f}, field);
        const float tr[][6] = {{0.14f, -0.06f, 0.28f, -0.06f, 0.36f, -0.2f}, {0.14f, 0.06f, 0.3f, 0.06f, 0.3f, 0.3f},
                               {-0.14f, 0.f, -0.36f, 0.f, -0.36f, 0.f},    {0.f, -0.14f, 0.f, -0.28f, -0.2f, -0.36f},
                               {-0.06f, 0.14f, -0.06f, 0.3f, -0.26f, 0.3f}, {0.06f, 0.14f, 0.06f, 0.4f, 0.06f, 0.4f}};
        for (const auto& t : tr) {
            p.line(t[0], t[1], t[2], t[3], 0.04f, sym);
            p.line(t[2], t[3], t[4], t[5], 0.04f, sym);
            p.circle(t[4], t[5], 0.055f, sym);
            p.circle(t[4], t[5], 0.025f, field);
        }
        if (now) {  // a pulse running out along the traces
            const float ph = float(now % 1600) / 1600.f;
            p.circle(0.14f + ph * 0.14f, 0.06f, 0.035f, white, int(220.f * (1.f - ph)));
            p.circle(-0.14f - ph * 0.22f, 0.f, 0.035f, white, int(220.f * (1.f - ph)));
        }
    } else if (k == "compass") {
        p.line(0.f, -0.3f, -0.27f, 0.38f, 0.075f, sym);
        p.line(0.f, -0.3f, 0.25f, 0.3f, 0.075f, sym);
        p.line(0.25f, 0.3f, 0.28f, 0.4f, 0.03f, mix(sym, white, 120));
        p.line(-0.16f, 0.1f, 0.15f, 0.08f, 0.04f, sym);
        p.circle(0.f, -0.3f, 0.09f, sym);
        p.circle(0.f, -0.3f, 0.035f, field);
        p.line(0.f, -0.39f, 0.f, -0.47f, 0.04f, sym);
        for (int i = 0; i < 6; ++i) {  // the arc it is drawing
            const float t0 = 0.35f + float(i) * 0.12f, t1 = t0 + 0.07f;
            p.line(std::cos(t0) * 0.42f - 0.2f, std::sin(t0) * 0.42f - 0.02f, std::cos(t1) * 0.42f - 0.2f, std::sin(t1) * 0.42f - 0.02f, 0.025f,
                   mix(sym, colour, 120));
        }
    } else if (k == "shard") {
        p.poly({0.f, -0.47f, 0.2f, -0.12f, 0.14f, 0.38f, 0.f, 0.47f}, mix(sym, colour, 60));
        p.poly({0.f, -0.47f, -0.2f, -0.12f, -0.14f, 0.38f, 0.f, 0.47f}, mix(sym, white, 90));
        p.line(0.f, -0.47f, 0.f, 0.47f, 0.02f, white, 160);
        p.line(-0.2f, -0.12f, 0.f, 0.06f, 0.015f, white, 120);
        p.line(0.2f, -0.12f, 0.f, 0.06f, 0.015f, field, 120);
        p.poly({-0.31f, 0.04f, -0.22f, 0.2f, -0.27f, 0.36f, -0.34f, 0.22f}, mix(sym, colour, 100), 220);
        p.poly({0.3f, -0.06f, 0.37f, 0.08f, 0.31f, 0.24f, 0.25f, 0.1f}, mix(sym, colour, 100), 200);
    } else if (k == "ember") {
        p.poly({0.f, -0.47f, 0.13f, -0.22f, 0.29f, 0.f, 0.27f, 0.21f, 0.13f, 0.37f, 0.f, 0.42f, -0.14f, 0.38f, -0.28f, 0.2f, -0.25f, -0.03f,
                -0.13f, -0.15f, -0.07f, -0.31f},
               rgb(240, 110, 40));
        p.poly({0.02f, -0.18f, 0.12f, 0.02f, 0.15f, 0.2f, 0.05f, 0.33f, -0.07f, 0.33f, -0.15f, 0.2f, -0.12f, 0.04f}, rgb(255, 196, 80));
        p.circle(0.f, 0.22f, 0.07f, rgb(255, 244, 200));
        if (now) {  // a spark rising off it
            const float ph = float(now % 1800) / 1800.f;
            p.circle(0.18f + std::sin(ph * 7.f) * 0.04f, -0.2f - ph * 0.3f, 0.03f, rgb(255, 200, 110), int(230.f * (1.f - ph)));
        }
    } else if (k == "forge") {  // (0.85.2 Living Forge Synod - the Atokera) an anvil, a drop of living metal falling onto it
        p.poly({-0.4f, -0.08f, 0.26f, -0.08f, 0.42f, -0.2f, 0.42f, -0.04f, 0.2f, 0.06f, -0.4f, 0.06f}, sym);  // the face and horn
        p.poly({-0.2f, 0.06f, 0.12f, 0.06f, 0.08f, 0.2f, -0.16f, 0.2f}, sym);                                   // the waist
        p.poly({-0.32f, 0.2f, 0.24f, 0.2f, 0.3f, 0.36f, -0.38f, 0.36f}, sym);                                   // the foot
        p.line(-0.38f, -0.08f, 0.24f, -0.08f, 0.025f, field, 140);
        const uint32_t metal = mix(rgb(90, 220, 150), colour, 80);
        const float drop = now ? float(now % 2000) / 2000.f : 0.35f;  // the drop falls and lands
        const float dy = -0.46f + drop * 0.3f;
        p.circle(-0.02f, dy, 0.075f, metal);
        p.poly({-0.06f, dy - 0.03f, 0.02f, dy - 0.03f, -0.02f, dy - 0.15f}, metal);
        if (now && drop > 0.85f) p.circle(-0.02f, -0.1f, 0.12f * (drop - 0.85f) / 0.15f + 0.04f, rgb(200, 255, 220), int(200.f * (1.f - drop) / 0.15f));
    } else if (k == "superweapon") {  // (0.85.2 Arsenal Directive - the Belkosea) the sphere with its slit iris, in the crosshairs
        p.ring(0.f, 0.f, 0.42f, 0.035f, sym);
        for (int i = 0; i < 4; ++i) {
            const float a = float(i) * kPi / 2.f;
            p.line(std::cos(a) * 0.3f, std::sin(a) * 0.3f, std::cos(a) * 0.52f, std::sin(a) * 0.52f, 0.04f, sym);
        }
        p.circle(0.f, 0.f, 0.24f, rgb(70, 74, 82));
        p.circle(-0.05f, -0.05f, 0.18f, rgb(128, 132, 140));
        p.circle(0.04f, 0.01f, 0.1f, rgb(30, 32, 38));
        const float breathe = now ? 0.6f + 0.4f * std::sin(float(now % 2400) / 2400.f * 2.f * kPi) : 0.8f;
        p.line(0.04f, -0.07f, 0.04f, 0.09f, 0.035f, rgb(255, 90, 60), int(255.f * breathe));
    } else if (k == "pyramid") {  // (0.85.2 Necropolis Eternal - the Pharad'n) a stepped pyramid, its doorway glowing
        const uint32_t dark = mix(field, rgb(20, 16, 6), 120);
        for (int st = 0; st < 3; ++st) {
            const float y0 = 0.36f - float(st) * 0.22f, y1 = y0 - 0.22f, hw = 0.46f - float(st) * 0.13f;
            p.poly({-hw, y0, hw, y0, hw - 0.04f, y1, -hw + 0.04f, y1}, st == 2 ? mix(sym, rgb(255, 255, 255), 60) : sym);
            p.poly({0.f, y0, hw, y0, hw - 0.04f, y1, 0.f, y1}, dark, 90);  // the shaded face
        }
        p.poly({-0.1f, -0.3f, 0.1f, -0.3f, 0.f, -0.45f}, mix(sym, rgb(255, 255, 255), 90));  // the capstone
        const float g = now ? 0.55f + 0.45f * std::sin(float(now % 2800) / 2800.f * 2.f * kPi) : 0.8f;
        p.circle(0.f, 0.29f, 0.13f, rgb(255, 145, 4), int(90.f * g));
        p.poly({-0.05f, 0.36f, 0.05f, 0.36f, 0.05f, 0.22f, -0.05f, 0.22f}, rgb(255, 160, 30), int(150.f + 100.f * g));
    }
}

}  // namespace

void draw(Canvas& c, const std::string& key, float cx, float cy, float r, uint32_t colour, State st, uint32_t now) {
    const Key* k = keyOf(key);
    if (!k || r < 2.f) return;
    Pen p{c, cx, cy, r, st == kOpen ? 110 : 255, st != kEarned};
    uint32_t base, hi, lo;
    metalOf(k->metal, colour, base, hi, lo);
    // the medal
    c.fillCircle(cx + r * 0.05f, cy + r * 0.07f, r, rgb(0, 0, 0), st == kOpen ? 50 : 110);  // its shadow
    p.circle(0.f, 0.f, 1.f, lo);
    p.circle(0.f, -0.03f, 0.95f, base);
    p.circle(-0.06f, -0.08f, 0.84f, hi, 70);  // light from the top left
    p.circle(0.f, 0.f, 0.86f, base);
    for (int i = 0; i < 28 && r >= 9.f; ++i) {  // beads round the rim
        const float a = float(i) / 28.f * 2.f * kPi;
        p.circle(std::cos(a) * 0.905f, std::sin(a) * 0.905f, 0.028f, std::sin(a) < 0.f ? hi : lo, 200);
    }
    const uint32_t field = mix(rgb(10, 12, 24), colour, 70);
    p.circle(0.f, 0.f, 0.79f, lo);
    p.circle(0.f, 0.f, 0.76f, field);
    p.circle(0.f, -0.18f, 0.56f, mix(field, colour, 90), 80);  // the enamel catching the light
    p.ring(0.f, 0.f, 0.77f, 0.03f, hi, 150);
    if (st == kLocked) {  // a padlock instead of the symbol
        const uint32_t m = rgb(150, 156, 172);
        p.ring(0.f, -0.1f, 0.17f, 0.07f, m);
        p.poly({-0.25f, -0.04f, 0.25f, -0.04f, 0.25f, 0.34f, -0.25f, 0.34f}, m);
        p.circle(0.f, 0.12f, 0.06f, field);
        p.line(0.f, 0.14f, 0.f, 0.25f, 0.05f, field);
        return;
    }
    const uint32_t sym = k->metal >= 0 && k->metal != 1 ? mix(hi, base, 70) : k->metal == 1 ? mix(rgb(250, 252, 255), colour, 40)
                                                                                           : mix(colour, rgb(255, 255, 255), 110);
    symbol(p, key, sym, field, colour, st == kEarned ? now : 0);
    if (st != kEarned) return;
    c.fillCircle(cx - r * 0.5f, cy - r * 0.58f, r * 0.1f, rgb(255, 255, 255), 90);  // a polish spot on the rim
    if (now) {  // now and then a glint crosses the rim (2.4 s, a different moment for each medal)
        uint32_t h = 2166136261u;
        for (const char* q = k->key; *q; ++q) h = (h ^ uint32_t(*q)) * 16777619u;
        const uint32_t ph = (now + h % 2400u) % 2400u;
        if (ph < 420) {
            const float e = std::sin(kPi * float(ph) / 420.f), a = -2.4f + float(ph) / 420.f * 1.4f;
            const float gx = cx + std::cos(a) * r * 0.9f, gy = cy + std::sin(a) * r * 0.9f, L = r * (0.12f + 0.2f * e);
            c.line(gx - L, gy, gx + L, gy, rgb(255, 255, 255), std::max(0.7f, r * 0.03f), int(230.f * e));
            c.line(gx, gy - L, gx, gy + L, rgb(255, 255, 255), std::max(0.7f, r * 0.03f), int(230.f * e));
            c.fillCircle(gx, gy, std::max(0.8f, r * 0.05f), rgb(255, 255, 255), int(255.f * e));
        }
    }
}

const char* name(const std::string& key) {
    const Key* k = keyOf(key);
    return k ? k->name : "";
}
int count() { return int(sizeof(kKeys) / sizeof(kKeys[0])); }
const char* keyAt(int i) { return i >= 0 && i < count() ? kKeys[i].key : ""; }

}  // namespace emblems
