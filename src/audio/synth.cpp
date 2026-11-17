// The game's sound effects, synthesised at start-up (0.27.0): oscillators,
// filtered noise and envelopes - no sound files, nothing taken from anywhere.
// Battle sounds go through a "distance" pass (high frequencies rolled off and a
// short room echo), so they sound like a fight heard from across the table.
//
// Only sin / cos / frexp / ldexp from the maths library: the Miyoo build must
// keep to GLIBC_2.4 symbols (expf / logf / powf would need GLIBC_2.27).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "audio/audio.h"

namespace audio {
namespace {

constexpr int R = 44100;
constexpr float kTwoPi = 6.28318530718f;

float fexp(float x) {  // e^x, |error| < 1e-6 relative
    if (x < -80.f) return 0.f;
    if (x > 80.f) x = 80.f;
    const float ln2 = 0.69314718056f;
    const int n = int(std::floor(x / ln2));
    const float r = x - float(n) * ln2;  // 0 .. ln2
    float t = 1.f, s = 1.f;
    for (int k = 1; k <= 8; ++k) {
        t *= r / float(k);
        s += t;
    }
    return std::ldexp(s, n);
}

float flog(float x) {  // natural log of x > 0
    if (x <= 0.f) return -80.f;
    int e = 0;
    const float m = std::frexp(x, &e);  // x = m * 2^e, m in [0.5, 1)
    const float t = (m - 1.f) / (m + 1.f), t2 = t * t;
    const float lm = 2.f * t * (1.f + t2 * (1.f / 3.f + t2 * (1.f / 5.f + t2 * (1.f / 7.f + t2 / 9.f))));
    return lm + float(e) * 0.69314718056f;
}

struct Noise {
    uint32_t s;
    explicit Noise(uint32_t seed) : s(seed ? seed : 1u) {}
    float next() {  // -1 .. 1
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return float(int32_t(s)) / 2147483648.f;
    }
};

struct OnePole {  // low-pass; cutoff can move every sample
    float y = 0;
    float lp(float x, float hz) {
        const float w = kTwoPi * hz / float(R);
        const float a = w / (1.f + w);
        y += a * (x - y);
        return y;
    }
};

// exponential glide from f0 to f1 over `dur` seconds (held at f1 after)
float glide(float f0, float f1, float t, float dur) {
    if (t >= dur) return f1;
    return f0 * fexp(flog(f1 / f0) * t / dur);
}

float soft(float x) { return x / (1.f + std::fabs(x)); }  // soft clipping, square-ish when driven

struct Buf {
    std::vector<float> v;
    explicit Buf(float seconds) : v(size_t(seconds * float(R)) + 1, 0.f) {}
    size_t n() const { return v.size(); }
    static float t(size_t i) { return float(i) / float(R); }
};

// attack (linear) then an exponential decay with time constant tau
float env(float t, float attack, float tau) {
    if (t < attack) return t / attack;
    return fexp(-(t - attack) / tau);
}

// --------------------------------------------------------------- the interface
std::vector<float> blips(const float* freqs, const float* lens, int n, float bright) {
    float total = 0.04f;
    for (int i = 0; i < n; ++i) total += lens[i];
    Buf b(total);
    float ph = 0, start = 0;
    size_t i = 0;
    for (int k = 0; k < n; ++k) {
        const size_t end = std::min(b.n(), size_t((start + lens[k]) * float(R)));
        for (; i < end; ++i) {
            const float t = Buf::t(i) - start;
            const float f = freqs[k] * (1.f + 0.04f * std::min(1.f, t / 0.02f));  // a small upward bend
            ph += kTwoPi * f / float(R);
            const float s = std::sin(ph) + bright * std::sin(ph * 2.f) * 0.35f + bright * std::sin(ph * 3.f) * 0.18f;
            const float e = (t < 0.003f ? t / 0.003f : 1.f) * fexp(-t / (lens[k] * 0.55f));
            b.v[i] = s * e;
        }
        start += lens[k];
    }
    for (; i < b.n(); ++i) {  // short tail of the last note
        const float t = Buf::t(i) - (start - lens[n - 1]);
        ph += kTwoPi * freqs[n - 1] / float(R);
        b.v[i] = std::sin(ph) * fexp(-t / (lens[n - 1] * 0.55f));
    }
    return b.v;
}

// ----------------------------------------------------------------- weapons
std::vector<float> laser(float f0, float f1, float dur, float drive, uint32_t seed) {
    Buf b(dur + 0.03f);
    Noise nz(seed);
    OnePole lp;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        ph += kTwoPi * glide(f0, f1, t, dur) / float(R);
        const float s = soft(std::sin(ph) * drive) + 0.08f * nz.next() * fexp(-t / 0.01f);
        b.v[i] = lp.lp(s, 6000.f) * env(t, 0.002f, dur * 0.42f);
    }
    return b.v;
}

std::vector<float> railgun() {
    Buf b(0.42f);
    Noise nz(7);
    OnePole lpN, lpT;
    float ph = 0, ph2 = 0, phT = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float n = nz.next();
        const float crack = (n - lpN.lp(n, 2500.f)) * fexp(-t / 0.006f) * 0.9f;  // high-passed snap
        const float f = glide(3600.f, 820.f, t, 0.26f);
        ph += kTwoPi * f / float(R);
        ph2 += kTwoPi * f * 2.76f / float(R);  // a metallic, inharmonic partial
        const float zing = (std::sin(ph) + 0.35f * std::sin(ph2)) * env(t, 0.001f, 0.085f) * 0.6f;
        phT += kTwoPi * glide(95.f, 48.f, t, 0.15f) / float(R);
        const float thump = std::sin(phT) * env(t, 0.002f, 0.07f) * 0.8f;
        b.v[i] = crack + zing + lpT.lp(thump, 400.f) * 1.4f;
    }
    return b.v;
}

std::vector<float> flak(uint32_t seed) {  // one tracer shell: a dull "tak"
    Buf b(0.16f);
    Noise nz(seed);
    OnePole a, c, th;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float n = nz.next();
        const float band = a.lp(n, 2400.f) - c.lp(n, 700.f);
        ph += kTwoPi * glide(160.f, 90.f, t, 0.05f) / float(R);
        b.v[i] = band * env(t, 0.001f, 0.022f) * 2.2f + th.lp(std::sin(ph), 600.f) * env(t, 0.001f, 0.03f) * 0.9f;
    }
    return b.v;
}

std::vector<float> whoosh(float dur, float fLo, float fHi, float rumble, float attack, uint32_t seed) {
    Buf b(dur + 0.05f);
    Noise nz(seed);
    OnePole a, c, r;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float n = nz.next();
        const float fc = glide(fLo, fHi, t, dur * 0.35f);
        const float band = a.lp(n, fc * 1.8f) - c.lp(n, fc * 0.5f);
        const float shape = (t < attack ? t / attack : 1.f) * fexp(-std::max(0.f, t - attack) / (dur * 0.35f));
        ph += kTwoPi * glide(90.f, 55.f, t, dur) / float(R);
        const float pop = std::sin(ph) * env(t, 0.002f, 0.05f);
        b.v[i] = band * shape * 1.8f + r.lp(pop, 300.f) * rumble + n * 0.25f * fexp(-t / 0.004f);
    }
    return b.v;
}

std::vector<float> torpedo() {
    std::vector<float> v = whoosh(0.7f, 300.f, 700.f, 0.6f, 0.03f, 31);
    float ph = 0;
    for (size_t i = 0; i < v.size(); ++i) {  // the deep "bwomm" under it
        const float t = Buf::t(i);
        ph += kTwoPi * glide(120.f, 52.f, t, 0.45f) / float(R);
        v[i] += std::sin(ph) * env(t, 0.01f, 0.22f) * 0.9f;
    }
    return v;
}

std::vector<float> heavyBolt() {
    Buf b(0.32f);
    Noise nz(41);
    OnePole lp, ln;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        ph += kTwoPi * glide(700.f, 140.f, t, 0.2f) / float(R);
        const float saw = 2.f * (ph / kTwoPi - std::floor(ph / kTwoPi)) - 1.f;
        const float s = soft(saw * 2.5f) * 0.7f + ln.lp(nz.next(), 1200.f) * fexp(-t / 0.03f) * 1.5f;
        b.v[i] = lp.lp(s, 3500.f) * env(t, 0.002f, 0.08f);
    }
    return b.v;
}

std::vector<float> beam(float dur, float base, float bright, float wobble, uint32_t seed) {
    Buf b(dur + 0.12f);
    Noise nz(seed);
    OnePole lp, ln;
    float p1 = 0, p2 = 0, p3 = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float trem = 1.f + 0.25f * std::sin(kTwoPi * 28.f * t);
        const float vib = 1.f + wobble * std::sin(kTwoPi * 5.5f * t);
        p1 += kTwoPi * base * vib / float(R);
        p2 += kTwoPi * base * 1.503f * vib / float(R);
        p3 += kTwoPi * base * 2.01f / float(R);
        const float saw1 = 2.f * (p1 / kTwoPi - std::floor(p1 / kTwoPi)) - 1.f;
        const float saw2 = 2.f * (p2 / kTwoPi - std::floor(p2 / kTwoPi)) - 1.f;
        float s = (saw1 + saw2 * 0.7f) * 0.5f + std::sin(p3) * bright + ln.lp(nz.next(), 900.f) * 0.6f;
        const float e = std::min(1.f, t / 0.025f) * (t < dur ? 1.f : fexp(-(t - dur) / 0.04f));
        b.v[i] = lp.lp(s * trem, 2200.f + bright * 2500.f) * e;
    }
    return b.v;
}

std::vector<float> warSunCharge() {
    Buf b(0.62f);
    float ph = 0, ph2 = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = glide(70.f, 520.f, t, 0.6f);
        ph += kTwoPi * f / float(R);
        ph2 += kTwoPi * f * 1.5f / float(R);
        const float grow = t / 0.6f;
        b.v[i] = (std::sin(ph) + 0.5f * grow * std::sin(ph2)) * grow * grow * (1.f + 0.3f * std::sin(kTwoPi * 18.f * t));
    }
    return b.v;
}

std::vector<float> warSunBeam() {
    Buf b(0.85f);
    Noise nz(53);
    OnePole lp, ln;
    float p1 = 0, p2 = 0, pm = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        pm += kTwoPi * 9.f / float(R);
        const float sweep = 1.f + 0.35f * std::sin(pm);  // the colours running along the beam
        p1 += kTwoPi * 55.f * sweep / float(R);
        p2 += kTwoPi * 330.f * sweep / float(R);
        const float saw = 2.f * (p1 / kTwoPi - std::floor(p1 / kTwoPi)) - 1.f;
        const float s = saw * 0.8f + std::sin(p2 + 2.f * std::sin(p1)) * 0.45f + ln.lp(nz.next(), 600.f) * 0.8f;
        const float e = std::min(1.f, t / 0.02f) * (t < 0.65f ? 1.f : fexp(-(t - 0.65f) / 0.06f));
        b.v[i] = lp.lp(s, 2600.f) * e;
    }
    return b.v;
}

std::vector<float> plasma() {
    Buf b(0.3f);
    Noise nz(61);
    OnePole ln;
    float pc = 0, pm = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        pm += kTwoPi * 230.f / float(R);
        const float index = 2.5f * fexp(-t / 0.08f);
        pc += kTwoPi * glide(1500.f, 280.f, t, 0.22f) / float(R);
        const float n = nz.next();
        const float sizzle = (n - ln.lp(n, 3000.f)) * 0.25f * fexp(-t / 0.05f);
        b.v[i] = (std::sin(pc + index * std::sin(pm)) + sizzle) * env(t, 0.003f, 0.08f);
    }
    return b.v;
}

std::vector<float> explosion(float dur, float boomHz, float bright, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed), crk(seed * 3u + 1u);
    OnePole lp, lp2, sub;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float fc = glide(2400.f * bright, 160.f, t, dur * 0.6f);
        const float n = lp2.lp(lp.lp(nz.next(), fc), fc);
        float crackle = 0;
        if (crk.next() > 0.995f - 0.01f * fexp(-t / 0.2f)) crackle = crk.next() * 0.8f * fexp(-t / (dur * 0.3f));
        ph += kTwoPi * glide(boomHz, boomHz * 0.55f, t, dur * 0.5f) / float(R);
        const float boom = sub.lp(std::sin(ph), 250.f) * env(t, 0.004f, dur * 0.22f);
        b.v[i] = n * 2.2f * env(t, 0.003f, dur * 0.25f) + crackle + boom * 1.6f;
    }
    return b.v;
}

std::vector<float> rifle(uint32_t seed) {  // slug rifle with a little "zing" - futuristic
    Buf b(0.14f);
    Noise nz(seed);
    OnePole h, a, c;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float n = nz.next();
        const float crack = (n - h.lp(n, 3000.f)) * fexp(-t / 0.005f);
        const float body = (a.lp(n, 1600.f) - c.lp(n, 500.f)) * env(t, 0.001f, 0.03f) * 2.f;
        ph += kTwoPi * glide(2600.f, 1700.f, t, 0.04f) / float(R);
        b.v[i] = crack + body + std::sin(ph) * env(t, 0.001f, 0.018f) * 0.3f;
    }
    return b.v;
}

// ------------------------------------------------- the "far away" treatment
void distance(std::vector<float>& v, float cutoff, float wet) {
    OnePole a, b;
    for (float& x : v) x = b.lp(a.lp(x, cutoff), cutoff);
    // a short room: two feedback echoes and a diffuser
    const size_t tail = size_t(0.32f * float(R));
    v.resize(v.size() + tail, 0.f);
    const size_t d1 = size_t(0.041f * float(R)), d2 = size_t(0.067f * float(R)), d3 = size_t(0.013f * float(R));
    std::vector<float> c1(d1, 0.f), c2(d2, 0.f), ap(d3, 0.f);
    size_t i1 = 0, i2 = 0, i3 = 0;
    OnePole damp;
    for (float& x : v) {
        const float e1 = c1[i1], e2 = c2[i2];
        c1[i1] = x + e1 * 0.42f;
        c2[i2] = x + e2 * 0.33f;
        i1 = (i1 + 1) % d1;
        i2 = (i2 + 1) % d2;
        float rv = damp.lp((e1 + e2) * 0.5f, 2500.f);
        const float apOut = ap[i3] - rv * 0.5f;  // all-pass diffuser
        ap[i3] = rv + apOut * 0.5f;
        i3 = (i3 + 1) % d3;
        x = x * (1.f - wet * 0.5f) + apOut * wet;
    }
    // trim the silence at the end
    size_t n = v.size();
    while (n > 1 && std::fabs(v[n - 1]) < 0.0005f) --n;
    v.resize(n);
}

std::vector<int16_t> finish(std::vector<float> v, float peak) {
    float m = 0;
    for (float x : v) m = std::max(m, std::fabs(x));
    const float k = m > 0 ? peak * 32767.f / m : 0.f;
    std::vector<int16_t> out(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        const float s = v[i] * k;
        out[i] = int16_t(std::max(-32767.f, std::min(32767.f, s)));
    }
    // fade the last 4 ms so nothing clicks
    const size_t f = std::min(out.size(), size_t(R / 250));
    for (size_t i = 0; i < f; ++i) {
        const size_t j = out.size() - 1 - i;
        out[j] = int16_t(int(out[j]) * int(i) / int(f));
    }
    return out;
}

// 0.28.0: a tile laid down - a soft low knock with a little wood in it
std::vector<float> thud() {
    const int n = int(R * 0.28f);
    std::vector<float> v(size_t(n), 0.f);
    Noise nz(131);
    OnePole lp;
    float ph = 0.f;
    for (int i = 0; i < n; ++i) {
        const float t = float(i) / float(R);
        const float f = 62.f + 70.f * fexp(-t * 40.f);  // the pitch drops as it lands
        ph += kTwoPi * f / float(R);
        if (ph > kTwoPi) ph -= kTwoPi;
        const float body = std::sin(ph) * fexp(-t * 18.f);
        const float knock = lp.lp(nz.next(), 900.f) * fexp(-t * 70.f) * 0.9f;
        v[size_t(i)] = body + knock;
    }
    return v;
}

// 0.28.0: the game-start banner - a deep hit, a rising sweep into a bright held chord (own design)
std::vector<float> gameStart() {
    const int n = int(R * 2.4f);
    std::vector<float> v(size_t(n), 0.f);
    Noise nz(137);
    OnePole lp, lp2;
    float ph[6] = {0, 0, 0, 0, 0, 0}, sub = 0.f;
    // a major chord with an octave, slightly detuned pairs (saw-ish, softened)
    const float chord[6] = {220.f, 221.3f, 277.2f, 329.6f, 440.f, 659.3f};
    for (int i = 0; i < n; ++i) {
        const float t = float(i) / float(R);
        float x = 0.f;
        // the hit: sub drop + noise burst
        const float sf = 48.f + 60.f * fexp(-t * 14.f);
        sub += kTwoPi * sf / float(R);
        if (sub > kTwoPi) sub -= kTwoPi;
        x += std::sin(sub) * fexp(-t * 3.2f) * 1.1f;
        x += lp.lp(nz.next(), 1800.f) * fexp(-t * 9.f) * 0.8f;
        // the sweep: filtered noise rising for the first 0.5 s
        if (t < 0.6f) x += lp2.lp(nz.next(), 300.f + 6000.f * t) * (t / 0.6f) * fexp(-(0.6f - t) * 1.f) * 0.35f * (t < 0.55f ? 1.f : (0.6f - t) / 0.05f);
        // the chord from 0.45 s: swells in, holds, fades
        if (t > 0.45f) {
            const float ct = t - 0.45f;
            const float env = std::min(1.f, ct / 0.08f) * fexp(-ct * 1.15f);
            float ch = 0.f;
            for (int k = 0; k < 6; ++k) {
                ph[k] += chord[k] / float(R);
                if (ph[k] >= 1.f) ph[k] -= 1.f;
                const float saw = 2.f * ph[k] - 1.f;
                ch += (saw * 0.45f + std::sin(kTwoPi * ph[k]) * 0.55f) * (k == 5 ? 0.5f : 1.f);
            }
            x += ch * env * 0.32f;
        }
        v[size_t(i)] = x;
    }
    return v;
}

// ----------------------------------------------------------- 0.59.70: the title screen's reveal
// a Lazaxian letter appearing: a deep bell with inharmonic partials, a breathy attack and a slow shimmer on top
std::vector<float> titleGlyph() {
    Buf b(1.6f);
    Noise nz(311);
    OnePole lp, hp;
    float p[5] = {0, 0, 0, 0, 0}, sh = 0.f;
    static const float ratio[5] = {1.f, 2.01f, 2.76f, 4.07f, 5.40f};
    const float base = 146.8f;  // D3
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        float s = 0.f;
        for (int k = 0; k < 5; ++k) {
            p[k] += kTwoPi * base * ratio[k] * (1.f + 0.0015f * std::sin(kTwoPi * (0.7f + 0.3f * float(k)) * t)) / float(R);
            s += std::sin(p[k]) * fexp(-t / (0.62f - 0.1f * float(k))) / float(1 + k);
        }
        const float att = std::min(1.f, t / 0.012f);
        // a breath of filtered noise at the start
        const float n = nz.next();
        const float breath = lp.lp(n - hp.lp(n, 400.f), 2600.f) * env(t, 0.03f, 0.09f) * 0.6f;
        // a high shimmer that wavers
        sh += kTwoPi * 1174.7f / float(R);
        const float shimmer = std::sin(sh) * (0.5f + 0.5f * std::sin(kTwoPi * 6.5f * t)) * env(t, 0.08f, 0.45f) * 0.18f;
        b.v[i] = s * att * 0.9f + breath + shimmer;
    }
    return b.v;
}
// a letter turning into English: a falling FM chirp crushed into steps, then a tiny high blip
std::vector<float> titleDecode() {
    Buf b(0.34f);
    float ph = 0.f, mod = 0.f, bl = 0.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = glide(2400.f, 520.f, t, 0.16f);
        mod += kTwoPi * f * 2.5f / float(R);
        ph += kTwoPi * f * (1.f + 0.6f * std::sin(mod)) / float(R);
        float s = std::sin(ph);
        s = std::floor(s * 4.f + 0.5f) / 4.f;  // crushed into a few steps: a digital edge
        float x = s * env(t, 0.002f, 0.05f) * 0.6f;
        if (t > 0.07f) {
            bl += kTwoPi * 1760.f / float(R);
            x += std::sin(bl) * env(t - 0.07f, 0.002f, 0.05f) * 0.45f;
        }
        b.v[i] = x;
    }
    return b.v;
}
// the name complete: a deep swell into a wide chord (D minor with an added ninth, a fifth below), fading slowly
std::vector<float> titleReveal() {
    const int n = int(R * 3.2f);
    std::vector<float> v(size_t(n), 0.f);
    Noise nz(313);
    OnePole lp;
    float ph[7] = {0, 0, 0, 0, 0, 0, 0}, sub = 0.f;
    static const float chord[7] = {73.4f, 110.f, 146.8f, 220.f, 293.7f, 349.2f, 659.3f};
    for (int i = 0; i < n; ++i) {
        const float t = float(i) / float(R);
        sub += kTwoPi * 36.7f / float(R);
        float x = std::sin(sub) * std::min(1.f, t / 0.9f) * fexp(-t * 0.9f) * 0.7f;
        const float swell = std::min(1.f, t / 1.1f);
        const float e = swell * swell * fexp(-std::max(0.f, t - 1.1f) * 1.2f);
        float ch = 0.f;
        for (int k = 0; k < 7; ++k) {
            ph[k] += (chord[k] * (1.f + 0.002f * std::sin(kTwoPi * (0.3f + 0.17f * float(k)) * t))) / float(R);
            if (ph[k] >= 1.f) ph[k] -= 1.f;
            const float saw = 2.f * ph[k] - 1.f;
            ch += (saw * 0.3f + std::sin(kTwoPi * ph[k]) * 0.7f) * (k == 6 ? 0.35f : 1.f);
        }
        x += lp.lp(ch, 600.f + 2400.f * swell) * e * 0.5f;
        x += nz.next() * 0.02f * e;  // air
        v[size_t(i)] = x;
    }
    return v;
}

// ----------------------------------------------------------- 0.44.0: the factions' ground weapons
float saw01(float ph) { return 2.f * (ph / kTwoPi - std::floor(ph / kTwoPi)) - 1.f; }
float fadeOut(float t, float dur, float tau) { return t < dur ? 1.f : fexp(-(t - dur) / tau); }

std::vector<float> sporePuff() {  // a soft, wet "pfff" with a bubble in it
    Buf b(0.32f);
    Noise nz(151);
    OnePole lp, lp2;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), fc = glide(2200.f, 400.f, t, 0.2f);
        const float n = lp2.lp(lp.lp(nz.next(), fc), fc);
        ph += kTwoPi * glide(320.f, 140.f, t, 0.12f) / float(R);
        b.v[i] = n * 2.4f * env(t, 0.012f, 0.09f) + std::sin(ph) * env(t, 0.004f, 0.05f) * 0.5f;
    }
    return b.v;
}
std::vector<float> seedLob() {  // a pod launched: a hollow "thoonk"
    Buf b(0.3f);
    Noise nz(157);
    OnePole lp;
    float ph = 0, ph2 = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        ph += kTwoPi * glide(210.f, 95.f, t, 0.09f) / float(R);
        ph2 += kTwoPi * glide(440.f, 190.f, t, 0.09f) / float(R);
        b.v[i] = (std::sin(ph) + 0.35f * std::sin(ph2)) * env(t, 0.002f, 0.07f) + lp.lp(nz.next(), 1200.f) * env(t, 0.001f, 0.012f) * 1.2f;
    }
    return b.v;
}
std::vector<float> seedPop() {  // the pod bursting: a pop and leaves rustling
    Buf b(0.4f);
    Noise nz(163), rs(167);
    OnePole h, a, c;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), n = nz.next(), r = rs.next();
        const float pop = (n - h.lp(n, 1800.f)) * env(t, 0.0005f, 0.008f) * 1.5f;
        const float rustle = (a.lp(r, 5000.f) - c.lp(r, 1500.f)) * env(t, 0.01f, 0.12f) * (0.6f + 0.4f * std::sin(kTwoPi * 37.f * t));
        b.v[i] = pop + rustle * 1.3f;
    }
    return b.v;
}
std::vector<float> fire(float dur, float tone, uint32_t seed, bool eerie) {  // a roar of fire with crackles
    Buf b(dur + 0.12f);
    Noise nz(seed), crk(seed * 7u + 3u);
    OnePole lp, lp2, rum;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), n = nz.next();
        const float fc = 900.f + 500.f * std::sin(kTwoPi * 3.1f * t);
        const float roar = lp2.lp(lp.lp(n, fc), fc * 1.4f);
        const float crackle = crk.next() > 0.993f ? crk.next() * 0.9f : 0.f;
        ph += kTwoPi * tone * (1.f + 0.08f * std::sin(kTwoPi * 6.f * t)) / float(R);
        const float whine = eerie ? (std::sin(ph) + 0.4f * std::sin(ph * 1.5f)) * 0.22f : 0.f;
        const float e = std::min(1.f, t / 0.06f) * fadeOut(t, dur, 0.06f);
        b.v[i] = (roar * 2.f + crackle + whine + rum.lp(n, 140.f) * 1.5f) * e;
    }
    return b.v;
}
std::vector<float> pulseLaser() {  // two quick red pulses: "bwip-bwip"
    Buf b(0.16f);
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), tt = t < 0.065f ? t : t - 0.065f;
        ph += kTwoPi * glide(1300.f, 700.f, tt, 0.05f) / float(R);
        const float gate = (t < 0.05f || (t > 0.065f && t < 0.115f)) ? 1.f : 0.f;
        b.v[i] = soft(std::sin(ph) * 4.f) * gate * env(tt, 0.002f, 0.03f);
    }
    return b.v;
}
std::vector<float> boneSpine() {  // a bone dart: "thwip"
    Buf b(0.12f);
    Noise nz(179);
    OnePole h;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), n = nz.next();
        ph += kTwoPi * glide(3400.f, 1400.f, t, 0.05f) / float(R);
        b.v[i] = (n - h.lp(n, 4000.f)) * env(t, 0.0003f, 0.004f) + std::sin(ph) * env(t, 0.002f, 0.03f) * 0.5f;
    }
    return b.v;
}
std::vector<float> plasmaBall() {  // a deep wobbling "whumm"
    Buf b(0.5f);
    Noise nz(181);
    OnePole ln;
    float pc = 0, pm = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        pm += kTwoPi * glide(9.f, 22.f, t, 0.4f) / float(R);
        pc += kTwoPi * glide(160.f, 90.f, t, 0.4f) * (1.f + 0.15f * std::sin(pm)) / float(R);
        const float n = nz.next();
        b.v[i] = (soft(std::sin(pc) * 2.2f) + 0.3f * std::sin(pc * 2.01f) + (n - ln.lp(n, 2500.f)) * 0.15f * env(t, 0.01f, 0.15f)) * env(t, 0.01f, 0.16f);
    }
    return b.v;
}
std::vector<float> slimeShot() {  // "splort": bubbly and wet
    Buf b(0.22f);
    Noise nz(191), jm(193);
    OnePole lp;
    float ph = 0, f = 260.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if (i % 600 == 0) f = 160.f + 220.f * (0.5f + 0.5f * jm.next());
        ph += kTwoPi * f / float(R);
        b.v[i] = (std::sin(ph) * 0.6f + lp.lp(nz.next(), 700.f) * 1.6f) * env(t, 0.004f, 0.06f);
    }
    return b.v;
}
std::vector<float> splat() {  // slime hitting the ground
    Buf b(0.25f);
    Noise nz(197);
    OnePole lp, lp2;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float n = lp2.lp(lp.lp(nz.next(), glide(1600.f, 300.f, t, 0.1f)), 900.f);
        ph += kTwoPi * glide(140.f, 60.f, t, 0.1f) / float(R);
        b.v[i] = (n * 2.5f + std::sin(ph) * 0.5f) * env(t, 0.002f, 0.05f);
    }
    return b.v;
}
std::vector<float> purpleBeam() {  // a long humming beam with a rising whine
    Buf b(1.0f);
    Noise nz(211);
    OnePole lp, ln;
    float p1 = 0, p2 = 0, p3 = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        p1 += kTwoPi * 140.f / float(R);
        p2 += kTwoPi * 141.7f / float(R);
        p3 += kTwoPi * glide(600.f, 1800.f, t, 0.85f) / float(R);
        const float s = (saw01(p1) + saw01(p2)) * 0.4f + std::sin(p3) * 0.25f + ln.lp(nz.next(), 800.f) * 0.3f;
        b.v[i] = lp.lp(s * (1.f + 0.3f * std::sin(kTwoPi * 14.f * t)), 2600.f) * std::min(1.f, t / 0.03f) * fadeOut(t, 0.85f, 0.05f);
    }
    return b.v;
}
std::vector<float> lightning() {  // a crackling zap
    Buf b(0.3f);
    Noise nz(223), g(227);
    OnePole h;
    float ph = 0, gate = 1.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), n = nz.next();
        if (i % 220 == 0) gate = g.next() > -0.2f ? 1.f : 0.15f;
        ph += kTwoPi * (120.f + 30.f * std::sin(kTwoPi * 7.f * t)) / float(R);
        b.v[i] = ((n - h.lp(n, 3000.f)) * 1.2f * gate + soft(std::sin(ph) * 6.f) * 0.35f) * env(t, 0.001f, 0.08f);
    }
    return b.v;
}
std::vector<float> hyperBeam() {  // a charge, then a roaring beam
    Buf b(1.25f);
    Noise nz(229);
    OnePole lp, lpn;
    float p1 = 0, p2 = 0, pc = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if (t < 0.3f) {
            pc += kTwoPi * glide(200.f, 1400.f, t, 0.3f) / float(R);
            const float k = t / 0.3f;
            b.v[i] = std::sin(pc) * k * k * 0.7f;
            continue;
        }
        const float tt = t - 0.3f;
        p1 += kTwoPi * 70.f / float(R);
        p2 += kTwoPi * (420.f + 40.f * std::sin(kTwoPi * 5.f * tt)) / float(R);
        const float s = saw01(p1) * 0.7f + std::sin(p2) * 0.4f + lpn.lp(nz.next(), 1800.f);
        b.v[i] = lp.lp(s, 3000.f) * std::min(1.f, tt / 0.02f) * fadeOut(tt, 0.8f, 0.07f);
    }
    return b.v;
}
std::vector<float> chime(float base, float dur, uint32_t seed, float crack) {  // glass: inharmonic partials ringing out
    Buf b(dur);
    Noise nz(seed);
    OnePole h;
    float p[4] = {0, 0, 0, 0};
    static const float ratio[4] = {1.f, 1.51f, 2.37f, 3.11f};
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        float s = 0;
        for (int k = 0; k < 4; ++k) {
            p[k] += kTwoPi * base * ratio[k] / float(R);
            s += std::sin(p[k]) * fexp(-t / (dur * (0.35f - 0.06f * float(k)))) / float(1 + k);
        }
        const float n = nz.next();
        b.v[i] = s * std::min(1.f, t / 0.001f) + (n - h.lp(n, 3500.f)) * env(t, 0.0005f, 0.006f) * crack;
    }
    return b.v;
}
std::vector<float> shatter() {  // a shard breaking: tiny pings scattered over a crack
    Buf b(0.42f);
    Noise nz(239), pk(241);
    OnePole h;
    float start[7], freq[7], ph[7] = {0, 0, 0, 0, 0, 0, 0};
    for (int k = 0; k < 7; ++k) {
        start[k] = 0.005f + 0.03f * float(k) * (0.6f + 0.4f * (0.5f + 0.5f * pk.next()));
        freq[k] = 2600.f + 2600.f * (0.5f + 0.5f * pk.next());
    }
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), n = nz.next();
        float s = (n - h.lp(n, 3000.f)) * env(t, 0.0005f, 0.01f);
        for (int k = 0; k < 7; ++k)
            if (t >= start[k]) {
                ph[k] += kTwoPi * freq[k] / float(R);
                s += std::sin(ph[k]) * fexp(-(t - start[k]) / 0.05f) * 0.35f;
            }
        b.v[i] = s;
    }
    return b.v;
}
std::vector<float> star(bool dark) {  // ethereal: a bell shimmer rising (falling for the dark star)
    Buf b(0.62f);
    float p1 = 0, p2 = 0, p3 = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), f = dark ? glide(900.f, 260.f, t, 0.5f) : glide(500.f, 1300.f, t, 0.5f);
        p1 += kTwoPi * f / float(R);
        p2 += kTwoPi * f * 1.5f / float(R);
        p3 += kTwoPi * f * 2.02f / float(R);
        b.v[i] = (std::sin(p1) + 0.5f * std::sin(p2) + 0.3f * std::sin(p3)) * (0.7f + 0.3f * std::sin(kTwoPi * 11.f * t)) * env(t, 0.04f, 0.22f);
    }
    return b.v;
}
std::vector<float> supernova() {  // a deep blast and a bright ringing shimmer
    std::vector<float> v = explosion(1.5f, 45.f, 0.8f, 243);
    float p[5] = {0, 0, 0, 0, 0};
    static const float fr[5] = {880.f, 1320.f, 1760.f, 2217.f, 2637.f};
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = Buf::t(i);
        float s = 0;
        for (int k = 0; k < 5; ++k) {
            p[k] += kTwoPi * fr[k] * (1.f + 0.003f * std::sin(kTwoPi * float(3 + k) * t)) / float(R);
            s += std::sin(p[k]);
        }
        v[i] += s * 0.12f * env(t, 0.03f, 0.45f);
    }
    return v;
}
std::vector<float> blackHole() {  // light sucked in: a swelling hiss falling away, a deep "thoom", a long low suck
    Buf b(1.4f);
    Noise nz(251);
    OnePole lp;
    float ph = 0, sub = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float n = lp.lp(nz.next(), glide(4000.f, 200.f, t, 1.2f));
        const float swell = t < 0.25f ? t / 0.25f : fexp(-(t - 0.25f) / 0.35f);
        ph += kTwoPi * glide(500.f, 35.f, t, 1.2f) / float(R);
        sub += kTwoPi * glide(70.f, 32.f, t, 0.6f) / float(R);
        b.v[i] = n * 1.6f * swell + std::sin(ph) * 0.5f * env(t, 0.05f, 0.5f) + std::sin(sub) * env(t, 0.02f, 0.35f) * 1.2f;
    }
    return b.v;
}
std::vector<float> rockThrow() {  // a heavy low "whoomp" with grinding in it
    std::vector<float> v = whoosh(0.45f, 220.f, 600.f, 0.9f, 0.05f, 257);
    Noise nz(259);
    OnePole a, c;
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = Buf::t(i), n = nz.next();
        v[i] += (a.lp(n, 900.f) - c.lp(n, 250.f)) * (0.5f + 0.5f * std::sin(kTwoPi * 31.f * t)) * env(t, 0.01f, 0.12f) * 1.2f;
    }
    return v;
}
std::vector<float> rockImpact() {  // a crunch: a thud and gravel
    std::vector<float> v = thud();
    v.resize(size_t(0.4f * float(R)), 0.f);
    Noise nz(261), g(263);
    OnePole h;
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = Buf::t(i), n = nz.next();
        const float grit = g.next() > 0.96f - 0.03f * fexp(-t / 0.05f) ? (n - h.lp(n, 1500.f)) * 1.4f : 0.f;
        v[i] = v[i] * 1.2f + grit * fexp(-t / 0.12f);
    }
    return v;
}
std::vector<float> meteorFall() {  // a falling whistle and a rising roar
    Buf b(0.55f);
    Noise nz(267);
    OnePole lp, lp2;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), fc = glide(300.f, 2500.f, t, 0.48f);
        ph += kTwoPi * glide(2400.f, 350.f, t, 0.48f) / float(R);
        const float roar = lp2.lp(lp.lp(nz.next(), fc), fc) * 2.f * (t / 0.5f);
        b.v[i] = (std::sin(ph) * 0.35f * std::min(1.f, t / 0.05f) + roar) * fadeOut(t, 0.5f, 0.02f);
    }
    return b.v;
}
std::vector<float> slimeStream() {  // a gurgling spray
    Buf b(0.75f);
    Noise nz(269), jm(271);
    OnePole lp, hp;
    float ph = 0, f = 200.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), n = nz.next();
        if (i % 900 == 0) f = 120.f + 260.f * (0.5f + 0.5f * jm.next());
        ph += kTwoPi * f / float(R);
        b.v[i] = ((lp.lp(n, 2600.f) - hp.lp(n, 700.f)) * 1.6f + std::sin(ph) * 0.45f) * std::min(1.f, t / 0.05f) * fadeOut(t, 0.6f, 0.05f);
    }
    return b.v;
}
std::vector<float> gasHiss() {  // steam: a hiss over a low roar
    Buf b(0.8f);
    Noise nz(277);
    OnePole h, lp, rum;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), n = nz.next();
        const float hiss = lp.lp(n - h.lp(n, 2500.f), 7000.f);
        b.v[i] = (hiss * 1.1f + rum.lp(n, 160.f) * 1.8f) * std::min(1.f, t / 0.08f) * fadeOut(t, 0.62f, 0.07f) * (0.85f + 0.15f * std::sin(kTwoPi * 9.f * t));
    }
    return b.v;
}
std::vector<float> droidBlaster() {  // a classic "pew"
    Buf b(0.14f);
    OnePole lp;
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        ph += kTwoPi * glide(1500.f, 260.f, t, 0.1f) / float(R);
        b.v[i] = lp.lp(soft(std::sin(ph) * 5.f), 5000.f) * env(t, 0.001f, 0.04f);
    }
    return b.v;
}
std::vector<float> crystalLaser() {  // a glassy laser: a laser with a ring of glass on it
    std::vector<float> v = laser(3000.f, 900.f, 0.1f, 1.4f, 281);
    float ph = 0, ph2 = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = Buf::t(i);
        ph += kTwoPi * 4200.f / float(R);
        ph2 += kTwoPi * 6350.f / float(R);
        v[i] += (std::sin(ph) + 0.5f * std::sin(ph2)) * 0.22f * env(t, 0.001f, 0.05f);
    }
    return v;
}
std::vector<float> firePlasma() {  // Muaat: a lower plasma shot with a burn crackling in it
    Buf b(0.32f);
    Noise nz(283), crk(285);
    OnePole ln;
    float pc = 0, pm = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        pm += kTwoPi * 140.f / float(R);
        pc += kTwoPi * glide(900.f, 180.f, t, 0.24f) / float(R);
        const float n = ln.lp(nz.next(), 1400.f);
        const float crackle = crk.next() > 0.985f ? crk.next() * 0.6f : 0.f;
        b.v[i] = (std::sin(pc + 2.f * fexp(-t / 0.08f) * std::sin(pm)) * 0.8f + n * 1.2f + crackle) * env(t, 0.003f, 0.09f);
    }
    return b.v;
}
std::vector<float> bigRocket() {  // the Titans' rockets: an ignition crack, then a heavy roar going away
    std::vector<float> v = whoosh(0.55f, 400.f, 1300.f, 1.f, 0.02f, 287);
    Noise nz(289);
    OnePole h;
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = Buf::t(i), n = nz.next();
        v[i] += (n - h.lp(n, 2000.f)) * env(t, 0.0005f, 0.015f) * 1.2f;
    }
    return v;
}

}  // namespace

std::vector<int16_t> synthesise(Sfx s) {
    // battle sounds: heard from a distance (owner) - softer highs and a room
    auto far = [](std::vector<float> v, float peak, float cutoff = 4200.f, float wet = 0.32f) {
        distance(v, cutoff, wet);
        return finish(std::move(v), peak);
    };
    switch (s) {
        case Sfx::Confirm: {
            const float f[2] = {988.f, 1480.f}, l[2] = {0.035f, 0.06f};
            return finish(blips(f, l, 2, 0.8f), 0.42f);
        }
        case Sfx::Back: {
            const float f[2] = {1245.f, 784.f}, l[2] = {0.035f, 0.06f};
            return finish(blips(f, l, 2, 0.6f), 0.38f);
        }
        case Sfx::Toggle: {
            const float f[1] = {1175.f}, l[1] = {0.03f};
            return finish(blips(f, l, 1, 0.5f), 0.3f);
        }
        case Sfx::FighterLaser: return far(laser(2600.f, 420.f, 0.12f, 2.2f, 3), 0.42f);
        case Sfx::Flak: return far(flak(11), 0.5f);
        case Sfx::Railgun: return far(railgun(), 0.6f);
        case Sfx::Missile: return far(whoosh(0.55f, 500.f, 1400.f, 0.7f, 0.04f, 17), 0.45f);
        case Sfx::Rocket: return far(whoosh(0.3f, 1200.f, 2600.f, 0.4f, 0.012f, 19), 0.38f);
        case Sfx::Torpedo: return far(torpedo(), 0.62f);
        case Sfx::HeavyBolt: return far(heavyBolt(), 0.5f);
        case Sfx::RedBeam: return far(beam(0.6f, 82.f, 0.2f, 0.01f, 43), 0.5f);
        case Sfx::GoldBeam: return far(beam(0.7f, 196.f, 0.7f, 0.02f, 47), 0.45f);
        case Sfx::WarSunCharge: return far(warSunCharge(), 0.42f);
        case Sfx::WarSunBeam: return far(warSunBeam(), 0.62f);
        case Sfx::Plasma: return far(plasma(), 0.5f);
        case Sfx::Impact: return far(explosion(0.45f, 90.f, 0.6f, 71), 0.5f);
        case Sfx::Explosion: return far(explosion(1.4f, 60.f, 1.f, 73), 0.75f, 3600.f, 0.35f);
        case Sfx::Rifle: return far(rifle(83), 0.38f);
        case Sfx::LaserRifle: return far(laser(1900.f, 600.f, 0.09f, 1.6f, 89), 0.34f);
        case Sfx::Gatling: return far(laser(1500.f, 700.f, 0.045f, 3.f, 97), 0.32f);
        case Sfx::BigExplosion: return far(explosion(1.7f, 50.f, 0.9f, 101), 0.8f, 3200.f, 0.38f);
        case Sfx::Thud: {
            std::vector<float> v = thud();
            distance(v, 2400.f, 0.12f);
            return finish(std::move(v), 0.5f);
        }
        case Sfx::GameStart: {
            std::vector<float> v = gameStart();
            distance(v, 7000.f, 0.22f);
            return finish(std::move(v), 0.62f);
        }
        case Sfx::SporePuff: return far(sporePuff(), 0.34f);
        case Sfx::SeedLob: return far(seedLob(), 0.45f);
        case Sfx::SeedPop: return far(seedPop(), 0.42f);
        case Sfx::Flamethrower: return far(fire(0.8f, 0.f, 291, false), 0.55f);
        case Sfx::GreenFlame: return far(fire(0.55f, 330.f, 293, true), 0.45f);
        case Sfx::PulseLaser: return far(pulseLaser(), 0.32f);
        case Sfx::RedGatling: return far(laser(1150.f, 520.f, 0.04f, 4.5f, 173), 0.3f);
        case Sfx::BoneSpine: return far(boneSpine(), 0.32f);
        case Sfx::PlasmaBall: return far(plasmaBall(), 0.5f);
        case Sfx::SlimeShot: return far(slimeShot(), 0.36f);
        case Sfx::Splat: return far(splat(), 0.36f);
        case Sfx::PurpleLaser: return far(laser(1350.f, 330.f, 0.12f, 1.2f, 199), 0.34f);
        case Sfx::PurpleBeam: return far(purpleBeam(), 0.48f);
        case Sfx::BigRocket: return far(bigRocket(), 0.5f);
        case Sfx::Lightning: return far(lightning(), 0.42f);
        case Sfx::HyperBeam: return far(hyperBeam(), 0.58f);
        case Sfx::CrystalShard: return far(chime(2300.f, 0.35f, 233, 0.6f), 0.3f);
        case Sfx::BigCrystal: return far(chime(1150.f, 0.55f, 237, 1.2f), 0.42f);
        case Sfx::Shatter: return far(shatter(), 0.3f);
        case Sfx::StarLaunch: return far(star(false), 0.34f);
        case Sfx::DarkStar: return far(star(true), 0.38f);
        case Sfx::Supernova: return far(supernova(), 0.78f, 3600.f, 0.38f);
        case Sfx::BlackHole: return far(blackHole(), 0.66f, 3200.f, 0.36f);
        case Sfx::RockThrow: return far(rockThrow(), 0.42f);
        case Sfx::RockImpact: return far(rockImpact(), 0.5f);
        case Sfx::MeteorFall: return far(meteorFall(), 0.5f);
        case Sfx::SlimeStream: return far(slimeStream(), 0.42f);
        case Sfx::GasHiss: return far(gasHiss(), 0.42f);
        case Sfx::DroidBlaster: return far(droidBlaster(), 0.3f);
        case Sfx::CrystalLaser: return far(crystalLaser(), 0.34f);
        case Sfx::FirePlasma: return far(firePlasma(), 0.44f);
        case Sfx::TitleGlyph: {
            std::vector<float> v = titleGlyph();
            distance(v, 6000.f, 0.38f);
            return finish(std::move(v), 0.42f);
        }
        case Sfx::TitleDecode: {
            std::vector<float> v = titleDecode();
            distance(v, 8000.f, 0.25f);
            return finish(std::move(v), 0.26f);
        }
        case Sfx::TitleReveal: {
            std::vector<float> v = titleReveal();
            distance(v, 5000.f, 0.3f);
            return finish(std::move(v), 0.6f);
        }
        case Sfx::Count: break;
    }
    return {};
}

}  // namespace audio
