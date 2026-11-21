// Build: g++ -std=c++17 -O2 -Isrc tools/sound_drafts/draft2.cpp -o draft2 && ./draft2 OUTDIR   (writes the WAVs)
// Sound drafts, round 2 (owner 2026-11-19 10:37: "too cheery. Make the sounds more gritty and grim"; on the klaxon: "I like
// the first segment, it should sound like a futuristic air raid siren"). Same toolkit as the game (synth.cpp included
// whole), plus resonant filters for metal, saturation and grit, minor seconds and tritones instead of major chords,
// and a darker, bigger room.
#define TI4_SYNTH_NO_GRIM  // (0.69.0: the game has these recipes now - src/audio/grim.inc; this file keeps its own)
#include "audio/synth.cpp"

#include <cstdio>
#include <string>

namespace audio {
namespace {

// ------------------------------------------------------------------ helpers
struct Ph {
    float p = 0;
    float step(float f) { p += f / float(R); p -= std::floor(p); return p; }
    float saw(float f) { return 2.f * step(f) - 1.f; }
    float sine(float f) { return std::sin(kTwoPi * step(f)); }
    float sq(float f, float duty = 0.5f) { return step(f) < duty ? 1.f : -1.f; }
};
struct Band {
    OnePole a, b;
    float bp(float x, float lo, float hi) { return a.lp(x, hi) - b.lp(x, lo); }
};
struct BP {  // a resonant band-pass (two poles): metal rings, formants
    float b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(float f, float q) {
        const float w = kTwoPi * f / float(R), al = std::sin(w) / (2.f * q), a0 = 1.f + al;
        b0 = al / a0, b2 = -al / a0, a1 = -2.f * std::cos(w) / a0, a2 = (1.f - al) / a0;
    }
    float run(float x) {
        const float y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1, x1 = x, y2 = y1, y1 = y;
        return y;
    }
};
float clamp01(float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; }
float smooth01(float x) { x = clamp01(x); return x * x * (3.f - 2.f * x); }
float passEnv(float t, float dur, float peakAt = 0.38f) {
    const float u = t / dur;
    if (u <= 0.f || u >= 1.f) return 0.f;
    return u < peakAt ? smooth01(u / peakAt) : 1.f - smooth01((u - peakAt) / (1.f - peakAt));
}
float doppler(float t, float dur, float amt, float peakAt = 0.38f) { return 1.f + amt * soft((peakAt - t / dur) * 8.f); }
float semis(float s) { return fexp(s * 0.0577623f); }
float uni(Noise& n) { return n.next() * 0.5f + 0.5f; }
void addInto(std::vector<float>& v, const std::vector<float>& w, float gain, float at = 0.f) {
    const size_t o = size_t(std::max(0.f, at) * float(R));
    if (v.size() < o + w.size()) v.resize(o + w.size(), 0.f);
    for (size_t i = 0; i < w.size(); ++i) v[o + i] += w[i] * gain;
}
// grit: noise that rides on the sound's own loudness, then saturation
void grit(std::vector<float>& v, float noise, float drive, uint32_t seed) {
    Noise nz(seed);
    OnePole e, hp;
    for (float& x : v) {
        const float lvl = e.lp(std::fabs(x), 30.f);
        const float n = nz.next();
        x = soft((x + (n - hp.lp(n, 1500.f)) * lvl * noise) * drive) / soft(drive);
    }
}
// fewer samples and fewer levels: a crushed, broken-machine edge (mixed in)
void crush(std::vector<float>& v, int hold, float levels, float mix) {
    float h = 0.f;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i % size_t(hold) == 0) h = std::round(v[i] * levels) / levels;
        v[i] = v[i] * (1.f - mix) + h * mix;
    }
}
// a heavy engine: saw + square an octave down, driven, dark, passing by
std::vector<float> heavyEngine(float dur, float f, float cutoff, float drive, float dop, float peakAt) {
    Buf b(dur);
    Ph a, s, c;
    OnePole lp, lp2;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), d = doppler(t, dur, dop, peakAt);
        const float x = soft((a.saw(f * d) + 0.8f * s.sq(f * 0.5f * d) + 0.5f * c.saw(f * d * 1.007f)) * drive);
        b.v[i] = lp2.lp(lp.lp(x, cutoff * d), cutoff * 1.5f * d) * passEnv(t, dur, peakAt);
    }
    return b.v;
}
std::vector<float> rumble(float dur, float cutoff, float peakAt, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed);
    OnePole a, c;
    for (size_t i = 0; i < b.n(); ++i) b.v[i] = c.lp(a.lp(nz.next(), cutoff), cutoff) * 9.f * passEnv(Buf::t(i), dur, peakAt);
    return b.v;
}
// a hull under stress: a low stick-slip tone wandering in pitch, rung through metal resonances
std::vector<float> groan(float dur, float f, const float* res, int n, float q, float peakAt, uint32_t seed) {
    Buf b(dur);
    Ph a;
    Noise nz(seed);
    OnePole wander;
    BP bp[6];
    for (int k = 0; k < n; ++k) bp[k].set(res[k], q);
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float fw = f * (1.f + 0.25f * wander.lp(nz.next(), 1.5f) * 25.f);
        const float x = a.saw(std::max(8.f, fw)) * (0.6f + 0.4f * uni(nz));
        float y = 0.f;
        for (int k = 0; k < n; ++k) y += bp[k].run(x) / float(k + 1);
        b.v[i] = y * 3.f * passEnv(t, dur, peakAt);
    }
    return b.v;
}
// a heavy metal impact: a click and a low thump rung through resonances
std::vector<float> clank(const float* res, int n, float q, float tau, uint32_t seed) {
    Buf b(tau * 6.f + 0.05f);
    Noise nz(seed);
    BP bp[6];
    for (int k = 0; k < n; ++k) bp[k].set(res[k], q);
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float ex = nz.next() * fexp(-t / 0.004f);
        float y = 0.f;
        for (int k = 0; k < n; ++k) y += bp[k].run(ex) / float(k + 1);
        b.v[i] = y * 6.f * fexp(-t / tau) + ex * 0.4f;
    }
    return b.v;
}
std::vector<float> thump(float f0, float f1, float glideT, float tau) {
    Buf b(tau * 5.f + 0.02f);
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        ph += kTwoPi * glide(f0, f1, t, glideT) / float(R);
        b.v[i] = soft(std::sin(ph) * 2.f) * env(t, 0.003f, tau);
    }
    return b.v;
}
// many small metal knocks at random moments (chains, loose plating)
std::vector<float> rattle(float dur, float rate, float fLo, float fHi, float peakAt, uint32_t seed) {
    std::vector<float> v(size_t(dur * float(R)), 0.f);
    Noise pick(seed);
    const int count = int(rate * dur);
    for (int k = 0; k < count; ++k) {
        const float at = uni(pick) * dur * 0.92f;
        const float f = fLo + (fHi - fLo) * uni(pick);
        const float res[2] = {f, f * 2.37f};
        addInto(v, clank(res, 2, 18.f, 0.012f, seed + 17 * k), 0.2f * passEnv(at, dur, peakAt) + 0.03f, at);
    }
    return v;
}
std::vector<float> hiss(float dur, float lo, float hi, float attack, float tau, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed);
    Band bd;
    for (size_t i = 0; i < b.n(); ++i) b.v[i] = bd.bp(nz.next(), lo, hi) * env(Buf::t(i), attack, tau) * 2.f;
    return b.v;
}
// radio static: band noise chopped by random dropouts
std::vector<float> staticBurst(float dur, float lo, float hi, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed), pick(seed + 1);
    Band bd;
    float g = 1.f, until = 0.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if (t >= until) g = uni(pick) > 0.3f ? 0.4f + 0.6f * uni(pick) : 0.f, until = t + 0.008f + 0.03f * uni(pick);
        b.v[i] = bd.bp(nz.next(), lo, hi) * g * 2.5f * (t < dur - 0.02f ? 1.f : (dur - t) / 0.02f);
    }
    return b.v;
}
// a dark detuned drone (frequencies given), low-passed
std::vector<float> drone(float dur, const float* f, int n, float cutoff, float detune, float peakAt) {
    Buf b(dur);
    Ph p[12];
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        float x = 0.f;
        for (int k = 0; k < n; ++k) x += p[k * 2].saw(f[k] * (1.f - detune)) + p[k * 2 + 1].saw(f[k] * (1.f + detune));
        b.v[i] = lp.lp(x / float(n), cutoff) * passEnv(t, dur, peakAt);
    }
    return b.v;
}
// a voice-like formant drone (a chant, a choir): saws through two formant bands
std::vector<float> chant(float dur, const float* f, int n, float f1, float f2, float vib, float peakAt) {
    Buf b(dur);
    Ph p[8];
    BP a, c;
    a.set(f1, 4.f);
    c.set(f2, 6.f);
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float v = 1.f + vib * std::sin(kTwoPi * 4.7f * t);
        float x = 0.f;
        for (int k = 0; k < n; ++k) x += p[k].saw(f[k] * v);
        b.v[i] = (a.run(x) + 0.6f * c.run(x)) * passEnv(t, dur, peakAt) * 0.8f;
    }
    return b.v;
}
// a big dark hall: two long echoes, darkened
void hall(std::vector<float>& v, float mix) {
    const size_t d1 = size_t(0.113f * float(R)), d2 = size_t(0.171f * float(R));
    v.resize(v.size() + size_t(0.6f * float(R)), 0.f);
    std::vector<float> c1(d1, 0.f), c2(d2, 0.f);
    size_t i1 = 0, i2 = 0;
    OnePole l1, l2;
    for (float& x : v) {
        const float e1 = c1[i1], e2 = c2[i2];
        c1[i1] = x + l1.lp(e2, 1400.f) * 0.5f;
        c2[i2] = x + l2.lp(e1, 1100.f) * 0.45f;
        i1 = (i1 + 1) % d1, i2 = (i2 + 1) % d2;
        x += (e1 + e2) * 0.5f * mix;
    }
    float pk = 0.f;
    for (float x : v) pk = std::max(pk, std::fabs(x));
    size_t n = v.size();
    while (n > 1 && std::fabs(v[n - 1]) < pk * 0.004f) --n;  // trim the tail below about -48 dB
    v.resize(n);
}
void noDC(std::vector<float>& v) {
    OnePole dc;
    for (float& x : v) x -= dc.lp(x, 22.f);
}
// mastering: no DC, glue, the dark room and hall, levelled by a loudness the ear would roughly agree with
std::vector<int16_t> grim(std::vector<float> v, float loud, float hallMix = 0.25f) {
    noDC(v);
    float m0 = 0.f;
    for (float x : v) m0 = std::max(m0, std::fabs(x));
    if (m0 > 0) for (float& x : v) x = soft(x / m0 * 1.6f);  // glue: a gentle squash of the peaks
    noDC(v);
    distance(v, 4200.f, 0.34f);
    hall(v, hallMix);
    noDC(v);
    double sq = 0;
    float m = 0;
    {
        OnePole lo, hi;
        for (float x : v) {
            const float y = hi.lp(x - lo.lp(x, 110.f), 7000.f);
            sq += double(y) * double(y);
            m = std::max(m, std::fabs(x));
        }
    }
    const float rms = float(std::sqrt(sq / double(std::max<size_t>(1, v.size()))));
    float k = rms > 0 ? loud / rms : 0.f;
    if (m * k > 0.9f) k = 0.9f / m;
    for (float& x : v) x *= k;
    return finish(std::move(v), std::min(0.9f, m * k));
}

// ======================================================== targeting a tile
// ATTACK A - "Battle stations": a deep two-tone alarm a half-step apart, a hull slam, a rumble
std::vector<float> attackStations() {
    std::vector<float> v(size_t(R * 1.5f), 0.f);
    for (int k = 0; k < 3; ++k) {
        Buf b(0.24f);
        Ph a, c;
        OnePole lp;
        const float f = k == 1 ? 196.f * semis(1.f) : 196.f;
        for (size_t i = 0; i < b.n(); ++i) {
            const float t = Buf::t(i);
            const float x = soft((a.sq(f) + c.saw(f * 1.5f * semis(0.5f)) * 0.5f) * 3.f);
            b.v[i] = lp.lp(x, 1600.f) * smooth01(t / 0.015f) * (t < 0.18f ? 1.f : fexp(-(t - 0.18f) / 0.03f));
        }
        addInto(v, b.v, 0.5f, 0.27f * float(k));
    }
    const float res[4] = {92.f, 233.f, 517.f, 1130.f};
    addInto(v, clank(res, 4, 22.f, 0.18f, 901), 0.9f, 0.8f);
    addInto(v, thump(70.f, 34.f, 0.15f, 0.3f), 1.2f, 0.8f);
    addInto(v, rumble(0.9f, 140.f, 0.2f, 903), 0.8f, 0.75f);
    grit(v, 0.6f, 2.f, 905);
    return v;
}
// ATTACK B - "Air-raid siren" (the owner liked the klaxon's first whoop): a wailing rise, a hold, a falling moan,
// two siren voices a minor third apart with a rotor's chop, over a low growl and a little radio grit
std::vector<float> attackSiren() {
    const float D = 2.2f;
    Buf b(D);
    Ph a, c, d, chop, sub;
    OnePole lp;
    BP res;
    res.set(900.f, 1.5f);
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        float f;
        if (t < 0.7f) f = 140.f + (520.f - 140.f) * smooth01(t / 0.7f);
        else if (t < 1.2f) f = 520.f * (1.f + 0.012f * std::sin(kTwoPi * 5.f * t));
        else f = 520.f - (520.f - 120.f) * smooth01((t - 1.2f) / 1.0f);
        const float rotor = 0.78f + 0.22f * chop.sine(f * 0.08f);  // the rotor chopping the air
        float x = a.saw(f) + 0.8f * c.saw(f * semis(3.f)) + 0.4f * d.sq(f * 0.5f);
        x = soft(x * 2.6f) * rotor;
        const float y = lp.lp(x, 2600.f) * 0.7f + res.run(x) * 0.5f;
        const float e = smooth01(t / 0.12f) * (t < D - 0.35f ? 1.f : (D - t) / 0.35f);
        b.v[i] = (y + sub.sine(f * 0.25f) * 0.2f) * e;
    }
    std::vector<float> v = b.v;
    addInto(v, staticBurst(D, 1500.f, 5000.f, 911), 0.03f);
    addInto(v, rumble(D, 120.f, 0.5f, 913), 0.25f);
    return v;
}
// ATTACK C - "Gun lock": heavy gears grinding round, the breech slamming shut, the low ring of the barrel
std::vector<float> attackGunLock() {
    std::vector<float> v(size_t(R * 1.5f), 0.f);
    Buf g(0.55f);
    Noise nz(921);
    Ph a;
    Band bd;
    for (size_t i = 0; i < g.n(); ++i) {
        const float t = Buf::t(i);
        const float teeth = 0.5f + 0.5f * a.sq(glide(28.f, 46.f, t, 0.5f), 0.4f);  // gear teeth catching
        g.v[i] = bd.bp(nz.next(), 250.f, 1400.f) * teeth * smooth01(t / 0.1f) * 2.f;
    }
    addInto(v, g.v, 0.6f);
    const float r1[3] = {310.f, 740.f, 1290.f};
    for (int k = 0; k < 4; ++k) addInto(v, clank(r1, 3, 25.f, 0.02f, 923 + k), 0.25f, 0.1f + 0.1f * float(k));
    const float r2[5] = {71.f, 188.f, 402.f, 655.f, 1210.f};
    addInto(v, clank(r2, 5, 40.f, 0.35f, 931), 1.0f, 0.55f);
    addInto(v, thump(80.f, 36.f, 0.12f, 0.25f), 1.2f, 0.55f);
    addInto(v, hiss(0.4f, 3000.f, 8000.f, 0.01f, 0.08f, 933), 0.2f, 0.6f);
    grit(v, 0.5f, 2.f, 935);
    return v;
}
// INVASION A - "Drop pods": pods screaming down through the air, violent crunching landings
std::vector<float> invadePods() {
    std::vector<float> v(size_t(R * 1.6f), 0.f);
    const float at[3] = {0.f, 0.2f, 0.37f};
    for (int k = 0; k < 3; ++k) {
        Buf w(0.42f);
        Noise nz(941 + k);
        Band bd;
        Ph o;
        for (size_t i = 0; i < w.n(); ++i) {
            const float t = Buf::t(i);
            const float f = glide(1400.f - 150.f * float(k), 260.f, t, 0.4f);
            const float scream = soft(o.saw(f) * 2.f) * 0.25f;
            w.v[i] = (bd.bp(nz.next(), f * 0.4f, f * 1.2f) * 2.5f + scream) * smooth01(t / 0.2f);
        }
        addInto(v, w.v, 0.5f, at[k]);
        const float res[4] = {84.f, 205.f, 470.f, 980.f};
        addInto(v, clank(res, 4, 12.f, 0.09f, 951 + k), 0.8f, at[k] + 0.41f);
        addInto(v, thump(75.f, 32.f, 0.1f, 0.18f), 1.1f, at[k] + 0.41f);
        addInto(v, rattle(0.3f, 40.f, 600.f, 2500.f, 0.1f, 961 + k), 0.6f, at[k] + 0.43f);
    }
    grit(v, 0.7f, 2.2f, 971);
    return v;
}
// INVASION B - "Landing burn": a dirty thruster roar coming down, a crunching touchdown, hydraulics and a groaning hull
std::vector<float> invadeBurn() {
    std::vector<float> v(size_t(R * 2.0f), 0.f);
    Buf r(0.95f);
    Noise nz(981);
    Band bd;
    Ph s;
    OnePole lp;
    for (size_t i = 0; i < r.n(); ++i) {
        const float t = Buf::t(i);
        const float e = smooth01(t / 0.1f) * (1.f - smooth01((t - 0.6f) / 0.35f));
        const float roar = bd.bp(nz.next(), 90.f, glide(1100.f, 380.f, t, 0.9f)) * 3.f;
        r.v[i] = soft((roar + lp.lp(s.saw(glide(58.f, 41.f, t, 0.9f)), 250.f) * 0.8f) * 2.5f) * e;
    }
    addInto(v, r.v, 0.7f);
    const float res[4] = {66.f, 171.f, 389.f, 802.f};
    addInto(v, clank(res, 4, 18.f, 0.22f, 983), 0.9f, 0.9f);
    addInto(v, thump(70.f, 30.f, 0.12f, 0.3f), 1.3f, 0.9f);
    addInto(v, hiss(0.6f, 2500.f, 7000.f, 0.03f, 0.18f, 985), 0.3f, 0.98f);
    const float gr[3] = {240.f, 610.f, 1150.f};
    addInto(v, groan(0.7f, 30.f, gr, 3, 30.f, 0.4f, 987), 0.35f, 1.05f);
    grit(v, 0.5f, 2.f, 989);
    return v;
}
// INVASION C - "Boots on the ground": a burst of radio static, heavy boots and gear in step, a low war horn
std::vector<float> invadeBoots() {
    std::vector<float> v(size_t(R * 1.8f), 0.f);
    addInto(v, staticBurst(0.22f, 700.f, 3500.f, 991), 0.5f);
    for (int k = 0; k < 4; ++k) {
        const float at = 0.25f + 0.21f * float(k);
        addInto(v, thump(95.f, 48.f, 0.05f, 0.07f), 0.9f, at);
        addInto(v, hiss(0.08f, 300.f, 1600.f, 0.002f, 0.02f, 993 + k), 0.6f, at);  // grit under the boot
        addInto(v, rattle(0.1f, 50.f, 1200.f, 3000.f, 0.2f, 1001 + k), 0.4f, at + 0.01f);
    }
    const float horn[2] = {73.4f, 77.8f};  // a minor second: grim
    std::vector<float> h = drone(1.0f, horn, 2, 700.f, 0.004f, 0.4f);
    for (float& x : h) x = soft(x * 3.f);
    addInto(v, h, 0.55f, 0.65f);
    grit(v, 0.6f, 2.f, 1011);
    return v;
}

// ========================================================= movement, per faction
std::vector<float> moveSol() {  // a military transport: a heavy driven engine, a low turbine whine, rattling hull
    const float D = 1.3f;
    std::vector<float> v = heavyEngine(D, 62.f, 700.f, 2.5f, 0.07f, 0.38f);
    Buf w(D);
    Ph a;
    Band bd;
    for (size_t i = 0; i < w.n(); ++i) {
        const float t = Buf::t(i);
        w.v[i] = bd.bp(a.saw(380.f * doppler(t, D, 0.08f)), 300.f, 1300.f) * passEnv(t, D, 0.38f);
    }
    addInto(v, w.v, 0.25f);
    addInto(v, rumble(D, 150.f, 0.38f, 1101), 0.7f);
    addInto(v, rattle(D, 14.f, 900.f, 2200.f, 0.38f, 1103), 0.5f);
    grit(v, 0.5f, 1.8f, 1105);
    return v;
}
std::vector<float> moveLetnev() {  // iron: a groaning hull, a deep grinding war engine, a slam, chains
    const float D = 1.5f;
    const float res[3] = {210.f, 530.f, 880.f};
    std::vector<float> v = groan(D, 38.f, res, 3, 35.f, 0.42f, 1111);
    addInto(v, heavyEngine(D, 41.f, 450.f, 3.f, 0.04f, 0.42f), 0.9f);
    const float r2[3] = {95.f, 240.f, 510.f};
    addInto(v, clank(r2, 3, 20.f, 0.2f, 1113), 0.6f, 0.02f);
    addInto(v, rattle(D, 10.f, 500.f, 1500.f, 0.42f, 1115), 0.5f);
    grit(v, 0.6f, 2.2f, 1117);
    return v;
}
std::vector<float> moveHacan() {  // a lumbering freighter caravan: a throbbing engine, creaking holds, chains
    const float D = 1.45f;
    Buf b(D);
    Ph a, s, throb;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), d = doppler(t, D, 0.03f, 0.42f);
        const float x = soft((a.saw(55.f * d) + s.sq(27.5f * d)) * 2.f) * (0.7f + 0.3f * throb.sine(3.1f));
        b.v[i] = lp.lp(x, 480.f) * passEnv(t, D, 0.42f);
    }
    std::vector<float> v = b.v;
    const float res[3] = {320.f, 690.f, 1240.f};
    addInto(v, groan(D, 22.f, res, 3, 25.f, 0.42f, 1121), 0.35f);
    addInto(v, rattle(D, 18.f, 700.f, 1800.f, 0.42f, 1123), 0.6f);
    addInto(v, hiss(D, 200.f, 900.f, 0.4f, 0.5f, 1125), 0.25f);
    grit(v, 0.4f, 1.8f, 1127);
    return v;
}
std::vector<float> moveJolNar() {  // cold science: an electric hum, a dissonant falling tone, crackling sparks
    const float D = 1.25f;
    Buf b(D);
    Ph a, c, m, s;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float hum = a.saw(50.f) * 0.5f + c.saw(150.f) * 0.3f;
        const float fm = s.sine(glide(311.f, 147.f, t, D) * (1.f + 0.4f * m.sine(311.f * 1.414f)));  // a cold FM tone, falling
        b.v[i] = (lp.lp(hum, 1200.f) + fm * 0.45f) * passEnv(t, D, 0.35f);
    }
    std::vector<float> v = b.v;
    Noise pick(1131);
    for (int k = 0; k < 7; ++k) addInto(v, staticBurst(0.03f, 2500.f, 8000.f, 1133 + k), 0.35f, 0.1f + 0.15f * float(k) + 0.04f * pick.next());
    grit(v, 0.3f, 1.6f, 1139);
    return v;
}
std::vector<float> moveSardakk() {  // a war swarm: a low droning buzz of wings, hissing, mandibles clicking
    const float D = 1.3f;
    Buf b(D);
    Ph a, w;
    Band bd;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), dp = doppler(t, D, 0.06f, 0.4f);
        const float f = 92.f * dp * (1.f + 0.05f * std::sin(kTwoPi * 23.f * t));
        const float beat = 0.5f + 0.5f * w.sine(24.f);
        b.v[i] = soft(bd.bp(a.saw(f), 150.f, 1500.f) * 4.f) * beat * passEnv(t, D, 0.4f);
    }
    std::vector<float> v = b.v;
    addInto(v, hiss(D, 3000.f, 7000.f, 0.4f, 0.4f, 1141), 0.18f);
    const float res[2] = {2300.f, 3900.f};
    Noise pick(1143);
    for (int k = 0; k < 14; ++k) addInto(v, clank(res, 2, 12.f, 0.006f, 1145 + k), 0.18f, uni(pick) * D * 0.9f);
    grit(v, 0.5f, 2.f, 1147);
    return v;
}
std::vector<float> moveXxcha() {  // slow, ancient and heavy: a deep minor drone, a groaning hull, a low roaring wind
    const float D = 1.8f;
    const float f[2] = {41.2f, 49.f};
    std::vector<float> v = drone(D, f, 2, 260.f, 0.004f, 0.5f);
    const float res[3] = {150.f, 380.f, 720.f};
    addInto(v, groan(D, 18.f, res, 3, 20.f, 0.5f, 1151), 0.3f);
    addInto(v, rumble(D, 300.f, 0.5f, 1153), 0.8f);
    grit(v, 0.3f, 1.6f, 1155);
    return v;
}
std::vector<float> moveArborec() {  // a living hull: wet wood creaking and splitting, a low groan, spores hissing out
    const float D = 1.4f;
    const float res[3] = {180.f, 420.f, 950.f};
    std::vector<float> v = groan(D, 34.f, res, 3, 9.f, 0.4f, 1161);
    Buf c(D);
    Noise nz(1163), pick(1165);
    Band bd;
    float g = 0.f;
    for (size_t i = 0; i < c.n(); ++i) {
        const float t = Buf::t(i);
        if (uni(pick) < 90.f / float(R)) g = 0.5f + 0.5f * uni(pick);
        g *= 0.997f;
        c.v[i] = bd.bp(nz.next(), 300.f, 1600.f) * g * 3.f * passEnv(t, D, 0.45f);  // splintering
    }
    addInto(v, c.v, 0.7f);
    addInto(v, hiss(0.6f, 2000.f, 6000.f, 0.05f, 0.2f, 1167), 0.2f, 0.75f);
    grit(v, 0.6f, 2.f, 1169);
    return v;
}
std::vector<float> moveL1Z1X() {  // the Mindnet's machinery: pounding pistons, a distorted hum, grinding gears, broken data
    const float D = 1.3f;
    std::vector<float> v = heavyEngine(D, 48.f, 500.f, 3.5f, 0.03f, 0.4f);
    const float res[3] = {300.f, 700.f, 1500.f};
    for (int k = 0; k < 10; ++k) {
        const float at = 0.12f * float(k);
        const float g = passEnv(at + 0.05f, D, 0.4f) + 0.1f;
        addInto(v, thump(70.f, 44.f, 0.04f, 0.05f), 0.7f * g, at);
        addInto(v, clank(res, 3, 20.f, 0.02f, 1171 + k), 0.3f * g, at);
    }
    Buf gr(D);
    Noise nz(1181);
    Band bd;
    for (size_t i = 0; i < gr.n(); ++i) {
        const float t = Buf::t(i);
        gr.v[i] = bd.bp(nz.next(), 700.f, 2000.f) * (0.5f + 0.5f * std::sin(kTwoPi * 41.f * t)) * passEnv(t, D, 0.4f) * 1.5f;
    }
    addInto(v, gr.v, 0.4f);
    addInto(v, staticBurst(0.25f, 2000.f, 6000.f, 1183), 0.2f, 0.5f);
    crush(v, 5, 10.f, 0.35f);
    grit(v, 0.4f, 1.8f, 1185);
    return v;
}
std::vector<float> moveMentak() {  // raiders: a snarling, overdriven engine and a rattling patched-up hull
    const float D = 1.0f;
    Buf b(D);
    Ph a, c;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = glide(70.f, 150.f, t, 0.45f) * (1.f + 0.03f * std::sin(kTwoPi * 17.f * t));
        b.v[i] = lp.lp(soft((a.saw(f) + c.saw(f * 1.012f)) * 5.f), 1400.f) * passEnv(t, D, 0.3f);
    }
    std::vector<float> v = b.v;
    addInto(v, rumble(D, 250.f, 0.3f, 1191), 0.9f);
    addInto(v, rattle(D, 30.f, 900.f, 2600.f, 0.3f, 1193), 0.6f);
    grit(v, 0.7f, 2.5f, 1195);
    return v;
}
std::vector<float> moveNaalu() {  // psychic dread: a cluster of tones a half-step apart, beating slowly, a whisper over a sub
    const float D = 1.5f;
    Buf b(D);
    Ph a, c, d, s;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float x = a.sine(311.f) + c.sine(329.6f) + 0.6f * d.sine(440.f * semis(-0.4f));
        b.v[i] = (lp.lp(x * 0.4f, 1800.f) + s.sine(41.f) * 0.5f) * passEnv(t, D, 0.5f);
    }
    std::vector<float> v = b.v;
    Buf w(D);
    Noise nz(1201);
    Band bd;
    for (size_t i = 0; i < w.n(); ++i) {
        const float t = Buf::t(i);
        w.v[i] = bd.bp(nz.next(), 3500.f, 7000.f) * (0.5f + 0.5f * std::sin(kTwoPi * 2.3f * t)) * passEnv(t, D, 0.5f) * 1.5f;
    }
    addInto(v, w.v, 0.3f);
    return v;
}
std::vector<float> moveNekro() {  // a corrupted machine: a crushed growl that stutters, static, digital shrieks
    const float D = 1.2f;
    Buf b(D);
    Ph a, c;
    Noise pick(1211);
    float st = 0.f;
    const size_t chunk = size_t(0.06f * float(R));
    std::vector<float> last(chunk, 0.f);
    bool repeat = false;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if (i % chunk == 0) {
            repeat = pick.next() > 0.35f && i > 0;
            if (pick.next() > 0.3f) st = std::floor(uni(pick) * 3.f) * 6.f - 6.f;  // tritone jumps
        }
        const float x = soft((a.saw(62.f * semis(st)) + 0.6f * c.sq(31.f * semis(st), 0.3f)) * 3.f);
        const float y = repeat ? last[i % chunk] : x;
        if (!repeat) last[i % chunk] = x;
        b.v[i] = y * passEnv(t, D, 0.38f);
    }
    std::vector<float> v = b.v;
    crush(v, 9, 5.f, 0.7f);
    addInto(v, staticBurst(D, 1500.f, 6000.f, 1213), 0.15f);
    const float res[1] = {2900.f};
    for (int k = 0; k < 3; ++k) {
        std::vector<float> s = clank(res, 1, 60.f, 0.05f, 1215 + k);
        crush(s, 3, 6.f, 0.8f);
        addInto(v, s, 0.35f, 0.2f + 0.3f * float(k));
    }
    grit(v, 0.6f, 2.f, 1219);
    return v;
}
std::vector<float> moveYssaril() {  // something in the dark: a low creeping rumble, whispering static, a slow breath
    const float D = 1.3f;
    std::vector<float> v = rumble(D, 110.f, 0.45f, 1221);
    Buf w(D);
    Noise nz(1223), pick(1225);
    Band bd;
    float g = 0.f;
    for (size_t i = 0; i < w.n(); ++i) {
        const float t = Buf::t(i);
        if (uni(pick) < 30.f / float(R)) g = uni(pick);
        g *= 0.9995f;
        w.v[i] = bd.bp(nz.next(), 1800.f, 4500.f) * g * 2.f * passEnv(t, D, 0.45f);  // whispers
    }
    addInto(v, w.v, 0.5f);
    addInto(v, hiss(1.0f, 200.f, 1200.f, 0.45f, 0.25f, 1227), 0.4f, 0.1f);  // a breath drawn in
    return v;
}
std::vector<float> moveCreuss() {  // the void: a deep swell sucked toward a cut, beating ghost tones, phasing noise
    const float D = 1.3f;
    Buf b(D);
    Noise nz(1231);
    Band bd;
    OnePole lp;
    std::vector<float> line(size_t(0.012f * float(R)) + 2, 0.f);
    size_t w = 0;
    Ph g1, g2, g3, sub;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const size_t dl = size_t((0.002f + 0.007f * (0.5f + 0.5f * std::sin(kTwoPi * 0.9f * t))) * float(R));
        const float x = bd.bp(nz.next(), 120.f, 1500.f);
        const float y = x + line[(w + line.size() - dl) % line.size()] * 0.75f;
        line[w] = y;
        w = (w + 1) % line.size();
        const float e = t < 0.95f ? smooth01(t / 0.95f) * smooth01(t / 0.95f) : fexp(-(t - 0.95f) / 0.04f);
        const float ghost = (g1.sine(110.f) + g2.sine(116.5f) + 0.6f * g3.sine(155.6f)) * 0.3f;
        b.v[i] = (y * 0.8f + ghost + lp.lp(sub.saw(36.f), 120.f) * 0.5f) * e;
    }
    return b.v;
}
std::vector<float> moveMuaat() {  // a firestorm: a ragged roar, crackling, magma rumbling far below
    const float D = 1.3f;
    std::vector<float> v = fire(D * 0.75f, 0.f, 1241, false);
    for (size_t i = 0; i < v.size(); ++i) v[i] *= passEnv(Buf::t(i), D * 0.75f + 0.12f, 0.35f) * 1.4f + 0.15f;
    addInto(v, rumble(D, 90.f, 0.4f, 1243), 1.0f);
    Buf s(D);
    Ph a;
    OnePole lp;
    for (size_t i = 0; i < s.n(); ++i) s.v[i] = lp.lp(soft(a.saw(38.f) * 3.f), 200.f) * passEnv(Buf::t(i), D, 0.4f);
    addInto(v, s.v, 0.6f);
    grit(v, 0.8f, 2.5f, 1245);
    return v;
}
std::vector<float> moveSaar() {  // scrap: metal scraping on metal, a heavy coughing engine, loose plates banging
    const float D = 1.35f;
    const float res[3] = {720.f, 1310.f, 2140.f};
    std::vector<float> v = groan(D, 55.f, res, 3, 45.f, 0.4f, 1251);  // the scrape
    Buf b(D);
    Ph a;
    Noise pick(1253);
    OnePole lp;
    float gate = 1.f, until = 0.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if (t >= until) gate = pick.next() > -0.4f ? 1.f : 0.f, until = t + 0.03f + 0.06f * uni(pick);
        b.v[i] = lp.lp(soft(a.sq(48.f, 0.35f) * 3.f), 700.f) * gate * passEnv(t, D, 0.4f);
    }
    addInto(v, b.v, 0.8f);
    const float r2[3] = {170.f, 420.f, 880.f};
    for (int k = 0; k < 5; ++k) addInto(v, clank(r2, 3, 15.f, 0.05f, 1255 + k), 0.4f, 0.1f + 0.24f * float(k) + 0.05f * pick.next());
    grit(v, 0.7f, 2.4f, 1261);
    return v;
}
std::vector<float> moveWinnu() {  // a fallen house: a mournful minor brass chord, dusty, sinking a little
    const float D = 1.6f;
    const float f[3] = {110.f, 130.8f, 164.8f};
    Buf b(D);
    Ph p[6];
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), dn = glide(1.f, 0.97f, t, D);
        float x = 0.f;
        for (int k = 0; k < 3; ++k) x += p[k * 2].saw(f[k] * dn * 0.996f) + p[k * 2 + 1].saw(f[k] * dn * 1.004f);
        const float e = passEnv(t, D, 0.45f);
        b.v[i] = lp.lp(soft(x * 0.6f), 300.f + 900.f * e) * e;
    }
    std::vector<float> v = b.v;
    addInto(v, hiss(D, 1500.f, 5000.f, 0.6f, 0.6f, 1271), 0.08f);
    grit(v, 0.4f, 1.6f, 1273);
    return v;
}
std::vector<float> moveYin() {  // a deep gong with sour beating partials, under a low chant
    const float D = 1.7f;
    const float res[5] = {62.f, 99.f, 141.f, 213.f, 297.f};
    std::vector<float> v = clank(res, 5, 90.f, 0.5f, 1281);
    v.resize(size_t(R * D), 0.f);
    const float f[2] = {73.4f, 73.9f};
    addInto(v, chant(D, f, 2, 420.f, 900.f, 0.01f, 0.5f), 0.45f);
    grit(v, 0.3f, 1.6f, 1283);
    return v;
}
std::vector<float> moveArgent() {  // raptors: heavy wing thrums and a harsh, ragged screech
    const float D = 1.3f;
    std::vector<float> v(size_t(R * D), 0.f);
    for (int k = 0; k < 3; ++k) addInto(v, hiss(0.24f, 120.f, 900.f, 0.08f, 0.06f, 1291 + k), 1.0f, 0.23f * float(k));
    Buf c(0.5f);
    Ph a;
    BP bp;
    bp.set(1300.f, 3.f);
    for (size_t i = 0; i < c.n(); ++i) {
        const float t = Buf::t(i);
        const float f = (t < 0.12f ? glide(700.f, 1000.f, t, 0.12f) : glide(1000.f, 560.f, t - 0.12f, 0.38f)) * (1.f + 0.03f * std::sin(kTwoPi * 31.f * t));
        c.v[i] = soft(bp.run(a.saw(f)) * 6.f) * env(t, 0.02f, 0.16f);
    }
    addInto(v, c.v, 0.4f, 0.6f);
    grit(v, 0.6f, 2.f, 1297);
    return v;
}
std::vector<float> moveEmpyrean() {  // a void choir: a dark, dissonant "ah", rising out of nothing, a sub far below
    const float D = 1.7f;
    const float f[4] = {110.f, 116.5f, 146.8f, 155.6f};
    std::vector<float> v = chant(D, f, 4, 650.f, 1080.f, 0.006f, 0.6f);
    Buf s(D);
    Ph a;
    for (size_t i = 0; i < s.n(); ++i) s.v[i] = a.sine(36.f) * passEnv(Buf::t(i), D, 0.6f);
    addInto(v, s.v, 0.6f);
    addInto(v, hiss(D, 4000.f, 9000.f, 1.0f, 0.3f, 1301), 0.1f);
    return v;
}
std::vector<float> moveMahact() {  // dark sorcery: a growling hum, whispers, dark energy crackling
    const float D = 1.4f;
    Buf b(D);
    Ph a, m;
    Noise nz(1311);
    Band bd;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = 55.f * (1.f + 0.3f * m.sine(55.f * 1.5f * semis(0.3f)));
        const float whisper = bd.bp(nz.next(), 900.f, 2800.f) * (0.5f + 0.5f * std::sin(kTwoPi * 6.f * t)) * 0.7f;
        b.v[i] = (lp.lp(soft(a.sine(f) * 3.f), 600.f) + whisper) * passEnv(t, D, 0.42f);
    }
    std::vector<float> v = b.v;
    const float res[2] = {260.f, 610.f};
    Noise pick(1313);
    for (int k = 0; k < 6; ++k) {
        addInto(v, staticBurst(0.05f, 1500.f, 6000.f, 1315 + k), 0.35f, 0.12f + 0.2f * float(k) + 0.04f * pick.next());
        addInto(v, clank(res, 2, 8.f, 0.03f, 1325 + k), 0.3f, 0.12f + 0.2f * float(k));
    }
    grit(v, 0.5f, 2.f, 1331);
    return v;
}
std::vector<float> moveNaazRokha() {  // heavy mechs: hydraulic hiss, two crushing steps, a drill grinding
    const float D = 1.35f;
    std::vector<float> v = heavyEngine(D, 58.f, 550.f, 2.5f, 0.04f, 0.4f);
    const float res[4] = {110.f, 290.f, 640.f, 1300.f};
    for (int k = 0; k < 2; ++k) {
        const float at = 0.2f + 0.45f * float(k);
        addInto(v, hiss(0.25f, 2500.f, 7000.f, 0.01f, 0.07f, 1341 + k), 0.4f, at - 0.12f);
        addInto(v, clank(res, 4, 20.f, 0.12f, 1343 + k), 0.8f, at);
        addInto(v, thump(85.f, 36.f, 0.08f, 0.16f), 1.0f, at);
    }
    Buf d(D);
    Ph a;
    Noise nz(1347);
    Band bd;
    for (size_t i = 0; i < d.n(); ++i) {
        const float t = Buf::t(i);
        const float x = a.saw(220.f * (1.f + 0.02f * std::sin(kTwoPi * 11.f * t))) * 0.5f + nz.next() * 0.5f;
        d.v[i] = bd.bp(x, 700.f, 2500.f) * (0.6f + 0.4f * std::sin(kTwoPi * 37.f * t)) * passEnv(t, D, 0.4f);
    }
    addInto(v, d.v, 0.4f);
    grit(v, 0.5f, 2.f, 1349);
    return v;
}
std::vector<float> moveNomad() {  // an old wanderer: a warbling drone a half-step sour, a falling lonely horn, a cavern
    const float D = 1.6f;
    Buf b(D);
    Ph a, c, h;
    OnePole lp, lh;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float wow = 1.f + 0.014f * std::sin(kTwoPi * 0.8f * t);
        const float dr = lp.lp(a.saw(55.f * wow) + c.saw(58.3f * wow), 500.f) * passEnv(t, D, 0.45f);
        const float he = passEnv(t - 0.3f, 1.0f, 0.4f);
        const float horn = lh.lp(soft(h.saw(110.f * glide(1.f, semis(-1.f), t - 0.3f, 0.9f) * wow) * 2.f), 250.f + 700.f * he) * he;
        b.v[i] = dr * 0.6f + horn * 0.7f;
    }
    std::vector<float> v = b.v;
    hall(v, 0.5f);
    grit(v, 0.3f, 1.6f, 1351);
    return v;
}
std::vector<float> moveTitans() {  // colossal: a deep boom, a quake, stone grinding against stone
    const float D = 1.8f;
    Buf b(D);
    Ph a, c;
    Noise nz(1361);
    OnePole lp, rough;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float r = 0.4f + 0.6f * clamp01(0.5f + rough.lp(nz.next(), 9.f) * 14.f);
        const float grind = lp.lp(nz.next(), 350.f) * 5.f * r;
        b.v[i] = (a.sine(28.f) + 0.7f * c.sine(42.f) + grind) * passEnv(t, D, 0.4f);
    }
    std::vector<float> v = b.v;
    addInto(v, thump(55.f, 24.f, 0.3f, 0.5f), 1.3f);
    addInto(v, rattle(D, 16.f, 300.f, 900.f, 0.4f, 1363), 0.5f);  // falling rubble
    grit(v, 0.6f, 2.f, 1365);
    return v;
}
std::vector<float> moveVuilraith() {  // the Cabal: a deep wet growl, gurgling, a rift tearing open and swallowing
    const float D = 1.45f;
    Buf b(D);
    Ph a;
    Noise nz(1371);
    OnePole fm, lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = 52.f * (1.f + 0.1f * fm.lp(nz.next(), 35.f) * 7.f);
        b.v[i] = lp.lp(soft(a.saw(f) * 4.f), 700.f) * passEnv(t, D, 0.4f);
    }
    std::vector<float> v = b.v;
    std::vector<float> g = slimeStream();
    OnePole gl;
    for (size_t i = 0; i < g.size(); ++i) g[i] = gl.lp(g[i], 1200.f) * passEnv(Buf::t(i), float(g.size()) / float(R), 0.4f);
    addInto(v, g, 0.6f, 0.1f);
    Buf tear(0.45f);
    Noise n2(1373);
    Band bd;
    for (size_t i = 0; i < tear.n(); ++i) {
        const float t = Buf::t(i);
        const float f = glide(2000.f, 280.f, t, 0.4f);
        tear.v[i] = bd.bp(n2.next(), f * 0.5f, f * 1.4f) * env(t, 0.01f, 0.14f) * 2.5f;
    }
    addInto(v, tear.v, 0.6f, 0.62f);
    grit(v, 0.6f, 2.2f, 1375);
    return v;
}
std::vector<float> moveKeleres() {  // grim authority: low minor brass, two war-drum hits, a steady engine
    const float D = 1.45f;
    const float f[3] = {73.4f, 87.3f, 110.f};
    std::vector<float> v = drone(D, f, 3, 700.f, 0.003f, 0.5f);
    for (float& x : v) x = soft(x * 2.5f);
    addInto(v, heavyEngine(D, 49.f, 400.f, 2.f, 0.03f, 0.45f), 0.6f);
    for (float at : {0.05f, 0.3f}) {
        addInto(v, thump(110.f, 52.f, 0.06f, 0.14f), 0.9f, at);
        addInto(v, hiss(0.06f, 400.f, 2500.f, 0.001f, 0.015f, 1381), 0.5f, at);
    }
    grit(v, 0.4f, 1.8f, 1383);
    return v;
}

void writeWav(const std::string& path, const std::vector<int16_t>& d) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const uint32_t bytes = uint32_t(d.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16), u16(1), u16(1), u32(R), u32(R * 2), u16(2), u16(16);
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    std::fwrite(d.data(), 2, d.size(), f);
    std::fclose(f);
}

}  // namespace
}  // namespace audio

#ifndef DRAFT_NO_MAIN
int main(int argc, char** argv) {
    using namespace audio;
    const std::string dir = argc > 1 ? argv[1] : ".";
    struct S { const char* name; std::vector<float> (*make)(); float loud; float hallMix; };
    const S list[] = {
        {"attack_a_stations", attackStations, 0.14f, 0.25f}, {"attack_b_siren", attackSiren, 0.13f, 0.3f},
        {"attack_c_gunlock", attackGunLock, 0.14f, 0.25f},   {"invade_a_droppods", invadePods, 0.14f, 0.25f},
        {"invade_b_landingburn", invadeBurn, 0.14f, 0.25f}, {"invade_c_boots", invadeBoots, 0.14f, 0.3f},
        {"move_sol", moveSol, 0.11f, 0.2f},                  {"move_letnev", moveLetnev, 0.11f, 0.25f},
        {"move_hacan", moveHacan, 0.11f, 0.2f},              {"move_jolnar", moveJolNar, 0.1f, 0.2f},
        {"move_sardakk", moveSardakk, 0.11f, 0.2f},          {"move_xxcha", moveXxcha, 0.1f, 0.3f},
        {"move_arborec", moveArborec, 0.11f, 0.2f},          {"move_l1z1x", moveL1Z1X, 0.11f, 0.2f},
        {"move_mentak", moveMentak, 0.11f, 0.2f},            {"move_naalu", moveNaalu, 0.09f, 0.3f},
        {"move_nekro", moveNekro, 0.1f, 0.2f},               {"move_yssaril", moveYssaril, 0.08f, 0.3f},
        {"move_creuss", moveCreuss, 0.1f, 0.3f},             {"move_muaat", moveMuaat, 0.11f, 0.2f},
        {"move_saar", moveSaar, 0.11f, 0.2f},                {"move_winnu", moveWinnu, 0.1f, 0.3f},
        {"move_yin", moveYin, 0.1f, 0.3f},                   {"move_argent", moveArgent, 0.11f, 0.2f},
        {"move_empyrean", moveEmpyrean, 0.09f, 0.35f},       {"move_mahact", moveMahact, 0.11f, 0.25f},
        {"move_naazrokha", moveNaazRokha, 0.11f, 0.2f},      {"move_nomad", moveNomad, 0.1f, 0.2f},
        {"move_titans", moveTitans, 0.12f, 0.25f},           {"move_vuilraith", moveVuilraith, 0.11f, 0.25f},
        {"move_keleres", moveKeleres, 0.11f, 0.25f},
    };
    for (const S& s : list) {
        std::vector<float> v = s.make();
        bool bad = false;
        for (float x : v) bad |= !(x == x) || std::fabs(x) > 1e6f;
        const std::vector<int16_t> out = grim(v, s.loud, s.hallMix);
        double sq = 0, dc = 0;
        int peak = 0;
        for (int16_t x : out) sq += double(x) * x, dc += x, peak = std::max(peak, std::abs(int(x)));
        std::printf("%-22s %4.2f s  peak %5d  rms %5.0f  dc %6.1f %s\n", s.name, double(out.size()) / R, peak,
                    std::sqrt(sq / double(out.size())), dc / double(out.size()), bad ? "BAD" : "");
        writeWav(dir + "/" + s.name + ".wav", out);
    }
    return 0;
}
#endif
