// Build: g++ -std=c++17 -O2 -Isrc tools/sound_drafts/draft.cpp -o draft && ./draft OUTDIR   (writes the WAVs)
// Sound drafts for the owner (2026-11-19): "targeting a tile" (attack, invasion) and a movement sound per faction.
// Built from the game's own synth (synth.cpp is included whole, so its helpers are at hand) - the same way the
// game makes every other sound: oscillators, filtered noise and envelopes, nothing recorded or borrowed.
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
    float tri(float f) { return 4.f * std::fabs(step(f) - 0.5f) - 1.f; }
};
struct Band {  // a crude band: the low-pass at hi minus the low-pass at lo
    OnePole a, b;
    float bp(float x, float lo, float hi) { return a.lp(x, hi) - b.lp(x, lo); }
};
float clamp01(float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; }
float smooth01(float x) { x = clamp01(x); return x * x * (3.f - 2.f * x); }
// a pass-by: rising to its loudest at peakAt (a fraction of dur), then going away
float passEnv(float t, float dur, float peakAt = 0.38f) {
    const float u = t / dur;
    if (u <= 0.f || u >= 1.f) return 0.f;
    return u < peakAt ? smooth01(u / peakAt) : 1.f - smooth01((u - peakAt) / (1.f - peakAt));
}
// the pitch of something passing: higher coming, lower going
float doppler(float t, float dur, float amt, float peakAt = 0.38f) { return 1.f + amt * soft((peakAt - t / dur) * 8.f); }
void addInto(std::vector<float>& v, const std::vector<float>& w, float gain, float at = 0.f) {
    const size_t o = size_t(at * float(R));
    if (v.size() < o + w.size()) v.resize(o + w.size(), 0.f);
    for (size_t i = 0; i < w.size(); ++i) v[o + i] += w[i] * gain;
}
// an inharmonic metal / bell hit
std::vector<float> metal(const float* f, int n, float tau, float click, uint32_t seed) {
    Buf b(tau * 5.f);
    Noise nz(seed);
    OnePole hp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        float s = 0.f;
        for (int k = 0; k < n; ++k) s += std::sin(kTwoPi * f[k] * t) * fexp(-t / (tau * (1.f - 0.12f * float(k)))) / float(k + 1);
        const float x = nz.next();
        s += (x - hp.lp(x, 2500.f)) * fexp(-t / 0.006f) * click;
        b.v[i] = s;
    }
    return b.v;
}
std::vector<float> ping(float f, float tau, float gain = 1.f) {
    Buf b(tau * 6.f);
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        b.v[i] = std::sin(kTwoPi * f * t) * env(t, 0.002f, tau) * gain;
    }
    return b.v;
}
// band noise that swells as it passes; its band follows the doppler too
std::vector<float> air(float dur, float lo, float hi, float peakAt, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed);
    Band bd;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), d = doppler(t, dur, 0.35f, peakAt);
        b.v[i] = bd.bp(nz.next(), lo * d, hi * d) * passEnv(t, dur, peakAt) * 2.2f;
    }
    return b.v;
}
// an engine: a saw (and its octave below) through a low-pass, passing by
std::vector<float> engine(float dur, float f, float cutoff, float sub, float dop, float peakAt, float drive = 1.f) {
    Buf b(dur);
    Ph a, s;
    OnePole lp, lp2;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), d = doppler(t, dur, dop, peakAt);
        float x = a.saw(f * d) + s.sq(f * 0.5f * d) * sub;
        if (drive != 1.f) x = soft(x * drive);
        b.v[i] = lp2.lp(lp.lp(x, cutoff * d), cutoff * 1.6f * d) * passEnv(t, dur, peakAt);
    }
    return b.v;
}
// a low drop: a sine whose pitch falls
std::vector<float> drop(float f0, float f1, float glideT, float tau) {
    Buf b(tau * 5.f + 0.02f);
    float ph = 0;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        ph += kTwoPi * glide(f0, f1, t, glideT) / float(R);
        b.v[i] = std::sin(ph) * env(t, 0.004f, tau);
    }
    return b.v;
}
// a crackle: little grains of band noise at random moments
std::vector<float> crackle(float dur, float rate, float lo, float hi, float peakAt, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed), pick(seed * 7 + 1);
    Band bd;
    float g = 0.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if ((pick.next() * 0.5f + 0.5f) < rate / float(R)) g = 0.4f + 0.6f * (pick.next() * 0.5f + 0.5f);
        g *= 0.9975f;
        b.v[i] = bd.bp(nz.next(), lo, hi) * g * passEnv(t, dur, peakAt) * 3.f;
    }
    return b.v;
}
// echo (a little space)
void echo(std::vector<float>& v, float delay, float fb, float mix) {
    const size_t d = size_t(delay * float(R));
    v.resize(v.size() + size_t(float(d) * 4.f), 0.f);
    std::vector<float> buf(d, 0.f);
    size_t k = 0;
    OnePole lp;
    for (float& x : v) {
        const float e = buf[k];
        buf[k] = x + lp.lp(e, 3000.f) * fb;
        k = (k + 1) % d;
        x += e * mix;
    }
}
// the map's sounds: a lighter room than the battles; levelled by loudness (RMS), peaks kept below 0.9
std::vector<int16_t> mapSound(std::vector<float> v, float loud, float wet = 0.2f) {
    {  // no DC (pulse waves and crushed samples carry some): a gentle high-pass at 25 Hz
        OnePole dc;
        for (float& x : v) x -= dc.lp(x, 25.f);
    }
    distance(v, 7500.f, wet);
    // loudness as the ear hears it, roughly: the deep lows and the very top count less
    double sq = 0;
    float m = 0;
    {
        OnePole lo, hi;
        for (float x : v) {
            const float y = hi.lp(x - lo.lp(x, 180.f), 9000.f);
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
// ATTACK A - "Lock-on": three quickening beeps, a held lock tone, a thump under it
std::vector<float> attackLock() {
    std::vector<float> v(size_t(R * 1.0f), 0.f);
    const float at[3] = {0.f, 0.15f, 0.26f};
    for (float a : at) {
        Buf b(0.06f);
        Ph o;
        OnePole lp;
        for (size_t i = 0; i < b.n(); ++i) b.v[i] = lp.lp(o.sq(1320.f), 4500.f) * env(Buf::t(i), 0.003f, 0.03f);
        addInto(v, b.v, 0.5f, a);
    }
    Buf lock(0.5f);
    Ph p1, p2;
    OnePole lp;
    for (size_t i = 0; i < lock.n(); ++i) {
        const float t = Buf::t(i);
        const float trem = 0.7f + 0.3f * std::sin(kTwoPi * 24.f * t);
        lock.v[i] = lp.lp(p1.sq(1760.f) * 0.6f + p2.sq(1175.f) * 0.4f, 5000.f) * trem * env(t, 0.004f, 0.16f);
    }
    addInto(v, lock.v, 0.45f, 0.36f);
    addInto(v, drop(110.f, 45.f, 0.12f, 0.16f), 1.1f, 0.36f);
    return v;
}
// ATTACK B - "Battle klaxon": two rising alarm whoops, then a far boom
std::vector<float> attackKlaxon() {
    std::vector<float> v(size_t(R * 1.3f), 0.f);
    for (int k = 0; k < 2; ++k) {
        Buf b(0.42f);
        Ph a, s;
        OnePole lp;
        for (size_t i = 0; i < b.n(); ++i) {
            const float t = Buf::t(i);
            const float f = glide(210.f, 430.f, t, 0.34f);
            const float x = soft((a.saw(f) + 0.5f * s.saw(f * 1.005f)) * 1.8f);
            const float e = smooth01(t / 0.05f) * (t < 0.3f ? 1.f : fexp(-(t - 0.3f) / 0.04f));
            b.v[i] = lp.lp(x, 1400.f + 2200.f * t) * e;
        }
        addInto(v, b.v, 0.55f, 0.43f * float(k));
    }
    addInto(v, explosion(0.6f, 55.f, 0.3f, 501), 0.6f, 0.86f);
    return v;
}
// ATTACK C - "Reticle": a servo whirring up, a metal clamp, a deep hit
std::vector<float> attackReticle() {
    std::vector<float> v(size_t(R * 1.0f), 0.f);
    Buf w(0.32f);
    float ph = 0;
    for (size_t i = 0; i < w.n(); ++i) {
        const float t = Buf::t(i);
        const float f = glide(500.f, 1500.f, t, 0.3f) * (1.f + 0.06f * std::sin(kTwoPi * 70.f * t));
        ph += kTwoPi * f / float(R);
        w.v[i] = std::sin(ph) * smooth01(t / 0.08f) * 0.6f;
    }
    addInto(v, w.v, 0.5f, 0.f);
    const float f[4] = {523.f, 1187.f, 1913.f, 2741.f};
    addInto(v, metal(f, 4, 0.06f, 1.2f, 503), 0.7f, 0.31f);
    addInto(v, drop(80.f, 40.f, 0.15f, 0.22f), 1.2f, 0.31f);
    return v;
}
// INVASION A - "Drop pods": three pods whistling down, each landing with a thud
std::vector<float> invadePods() {
    std::vector<float> v(size_t(R * 1.3f), 0.f);
    const float at[3] = {0.f, 0.17f, 0.32f};
    for (int k = 0; k < 3; ++k) {
        Buf w(0.36f);
        Ph o;
        Noise nz(511 + k);
        Band bd;
        for (size_t i = 0; i < w.n(); ++i) {
            const float t = Buf::t(i);
            const float f = glide(2400.f - 200.f * float(k), 520.f, t, 0.34f);
            w.v[i] = (o.sine(f) * 0.6f + bd.bp(nz.next(), f * 0.7f, f * 1.4f) * 1.5f) * smooth01(t / 0.12f) * 0.5f;
        }
        addInto(v, w.v, 0.55f, at[k]);
        addInto(v, thud(), 1.0f, at[k] + 0.35f);
    }
    return v;
}
// INVASION B - "Landing burn": retro-thrusters roaring down, a heavy touchdown, steam
std::vector<float> invadeBurn() {
    std::vector<float> v(size_t(R * 1.5f), 0.f);
    Buf r(0.85f);
    Noise nz(521);
    Band bd;
    Ph s;
    OnePole lp;
    for (size_t i = 0; i < r.n(); ++i) {
        const float t = Buf::t(i);
        const float e = smooth01(t / 0.08f) * (1.f - smooth01((t - 0.55f) / 0.3f));
        const float roar = bd.bp(nz.next(), 160.f, glide(1400.f, 500.f, t, 0.8f)) * 2.5f;
        r.v[i] = (roar + lp.lp(s.saw(glide(70.f, 52.f, t, 0.8f)), 300.f) * 0.5f) * e;
    }
    addInto(v, r.v, 0.6f, 0.f);
    addInto(v, drop(95.f, 38.f, 0.1f, 0.25f), 1.3f, 0.8f);
    addInto(v, thud(), 0.9f, 0.8f);
    Buf h(0.5f);
    Noise nz2(523);
    OnePole hp;
    for (size_t i = 0; i < h.n(); ++i) {
        const float x = nz2.next();
        h.v[i] = (x - hp.lp(x, 3500.f)) * env(Buf::t(i), 0.02f, 0.14f);
    }
    addInto(v, h.v, 0.25f, 0.84f);
    return v;
}
// INVASION C - "Boots on the ground": a radio click and beep, two war-drum hits, a low brass stab
std::vector<float> invadeDrums() {
    std::vector<float> v(size_t(R * 1.2f), 0.f);
    Buf c(0.05f);
    Noise nz(531);
    Band bd;
    for (size_t i = 0; i < c.n(); ++i) c.v[i] = bd.bp(nz.next(), 900.f, 3200.f) * env(Buf::t(i), 0.001f, 0.012f) * 2.f;
    addInto(v, c.v, 0.5f, 0.f);
    addInto(v, ping(1050.f, 0.035f), 0.25f, 0.03f);
    for (float a : {0.17f, 0.33f}) {
        Buf d(0.4f);
        Noise n2(533);
        OnePole lp;
        float ph = 0;
        for (size_t i = 0; i < d.n(); ++i) {
            const float t = Buf::t(i);
            ph += kTwoPi * glide(125.f, 68.f, t, 0.09f) / float(R);
            d.v[i] = std::sin(ph) * env(t, 0.002f, 0.13f) + lp.lp(n2.next(), 1800.f) * env(t, 0.001f, 0.02f) * 0.8f;
        }
        addInto(v, d.v, 0.9f, a);
    }
    Buf b(0.55f);
    Ph p1, p2, p3;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float x = p1.saw(147.f) + p2.saw(220.5f) * 0.8f + p3.saw(147.6f) * 0.6f;
        b.v[i] = lp.lp(x, 500.f + 1700.f * env(t, 0.05f, 0.2f)) * env(t, 0.03f, 0.25f);
    }
    addInto(v, b.v, 0.45f, 0.5f);
    return v;
}

// ========================================================= movement, per faction
std::vector<float> moveSol() {  // clean jet thrust with an ignition crackle
    const float D = 1.25f;
    std::vector<float> v = engine(D, 88.f, 900.f, 0.4f, 0.08f, 0.38f);
    addInto(v, air(D, 500.f, 2600.f, 0.38f, 601), 0.9f);
    addInto(v, crackle(0.18f, 400.f, 2000.f, 7000.f, 0.2f, 603), 0.5f);
    return v;
}
std::vector<float> moveLetnev() {  // heavy industrial hull: a clank, a deep grinding engine
    const float D = 1.45f;
    std::vector<float> v = engine(D, 46.f, 520.f, 0.9f, 0.05f, 0.42f, 2.2f);
    const float f[4] = {180.f, 413.f, 702.f, 1033.f};
    addInto(v, metal(f, 4, 0.12f, 0.9f, 611), 0.55f, 0.02f);
    addInto(v, air(D, 120.f, 700.f, 0.42f, 613), 0.6f);
    return v;
}
std::vector<float> moveHacan() {  // a warm caravan hum with trade bells
    const float D = 1.35f;
    Buf b(D);
    Ph a, c, d;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), dp = doppler(t, D, 0.03f);
        b.v[i] = (a.sine(110.f * dp) + 0.5f * c.sine(220.f * dp) + 0.25f * d.sine(330.f * dp)) * passEnv(t, D, 0.4f);
    }
    std::vector<float> v = b.v;
    addInto(v, air(D, 300.f, 1500.f, 0.4f, 621), 0.35f);
    const float bells[3] = {2093.f, 2637.f, 3136.f};
    for (int k = 0; k < 3; ++k) addInto(v, chime(bells[k], 0.35f, 623 + k, 0.f), 0.22f, 0.22f + 0.2f * float(k));
    return v;
}
std::vector<float> moveJolNar() {  // an elegant sine glide with a bright arpeggio
    const float D = 1.15f;
    Buf b(D);
    Ph a, c;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = (t < 0.4f ? glide(520.f, 780.f, t, 0.4f) : glide(780.f, 600.f, t - 0.4f, 0.7f)) * (1.f + 0.015f * std::sin(kTwoPi * 6.f * t));
        b.v[i] = (a.sine(f) + 0.35f * c.sine(f * 1.5f)) * passEnv(t, D, 0.35f) * 0.6f;
    }
    std::vector<float> v = b.v;
    const float arp[3] = {1568.f, 1976.f, 2349.f};
    for (int k = 0; k < 3; ++k) addInto(v, ping(arp[k], 0.06f), 0.22f, 0.12f + 0.09f * float(k));
    addInto(v, air(D, 2000.f, 7000.f, 0.35f, 631), 0.2f);
    return v;
}
std::vector<float> moveSardakk() {  // a swarm: buzzing wings and chittering clicks
    const float D = 1.2f;
    Buf b(D);
    Ph a, w;
    Band bd;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), dp = doppler(t, D, 0.07f);
        const float f = 165.f * dp * (1.f + 0.04f * std::sin(kTwoPi * 31.f * t));
        const float beat = 0.55f + 0.45f * w.sine(38.f);
        b.v[i] = bd.bp(a.saw(f), 300.f, 2600.f) * beat * passEnv(t, D, 0.4f) * 1.6f;
    }
    std::vector<float> v = b.v;
    addInto(v, crackle(D, 22.f, 3000.f, 9000.f, 0.4f, 641), 0.5f);
    return v;
}
std::vector<float> moveXxcha() {  // calm and slow: a soft low hum and wind
    const float D = 1.7f;
    Buf b(D);
    Ph a, c, d;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        b.v[i] = (a.sine(65.f) + 0.6f * c.sine(97.5f) + 0.3f * d.sine(130.f)) * passEnv(t, D, 0.5f);
    }
    std::vector<float> v = b.v;
    addInto(v, air(D, 200.f, 900.f, 0.5f, 651), 0.7f);
    return v;
}
std::vector<float> moveArborec() {  // rustling leaves, a creaking living hull, a spore puff
    const float D = 1.3f;
    std::vector<float> v = crackle(D, 260.f, 1800.f, 6000.f, 0.4f, 661);
    Buf c(D);
    Ph a;
    Noise nz(663);
    OnePole wob, lp, rough;
    for (size_t i = 0; i < c.n(); ++i) {
        const float t = Buf::t(i);
        const float f = 72.f * (1.f + 0.15f * wob.lp(nz.next(), 3.f) * 8.f);
        const float r = 0.6f + 0.4f * rough.lp(nz.next(), 30.f) * 6.f;
        c.v[i] = lp.lp(a.saw(f), 600.f) * r * passEnv(t, D, 0.4f);
    }
    addInto(v, c.v, 0.8f);
    addInto(v, sporePuff(), 0.5f, 0.82f);
    return v;
}
std::vector<float> moveL1Z1X() {  // a machine: deep hum, stepped servo whine, data chirps
    const float D = 1.25f;
    std::vector<float> v = engine(D, 55.f, 420.f, 0.6f, 0.04f, 0.4f, 1.5f);
    Buf s(D);
    Ph a;
    OnePole lp;
    for (size_t i = 0; i < s.n(); ++i) {
        const float t = Buf::t(i);
        const float step = std::floor(t / 0.045f);
        const float f = 300.f * fexp(0.05776f * std::min(step, 10.f));  // a semitone a step, up to ten
        s.v[i] = lp.lp(a.sq(f), 2500.f) * passEnv(t, D, 0.4f);
    }
    addInto(v, s.v, 0.28f);
    Noise pick(671);
    for (int k = 0; k < 6; ++k) addInto(v, ping(1800.f + 1400.f * (pick.next() * 0.5f + 0.5f), 0.012f), 0.25f, 0.1f + 0.13f * float(k));
    return v;
}
std::vector<float> moveMentak() {  // pirates: an afterburner kick, a hard rising engine
    const float D = 0.9f;
    std::vector<float> v(size_t(R * D), 0.f);
    Buf b(D);
    Ph a, c;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = glide(140.f, 420.f, t, 0.5f);
        b.v[i] = lp.lp(soft((a.saw(f) + c.saw(f * 1.01f)) * 2.5f), 2200.f) * passEnv(t, D, 0.28f);
    }
    addInto(v, b.v, 0.6f);
    addInto(v, air(D, 200.f, 1400.f, 0.22f, 681), 1.1f);
    addInto(v, drop(85.f, 48.f, 0.08f, 0.1f), 0.9f);
    addInto(v, crackle(D, 90.f, 1500.f, 6000.f, 0.25f, 683), 0.5f);
    return v;
}
std::vector<float> moveNaalu() {  // crystal: a rising glass arpeggio over a shimmering pad
    const float D = 1.35f;
    Buf b(D);
    Ph a, c, d;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float trem = 0.8f + 0.2f * std::sin(kTwoPi * 7.f * t);
        b.v[i] = (a.sine(440.f) + 0.7f * c.sine(660.f) + 0.4f * d.sine(880.f)) * trem * passEnv(t, D, 0.45f) * 0.5f;
    }
    std::vector<float> v = b.v;
    const float arp[4] = {1760.f, 2217.f, 2637.f, 3520.f};
    for (int k = 0; k < 4; ++k) addInto(v, chime(arp[k], 0.5f, 691 + k, 0.f), 0.25f, 0.06f + 0.12f * float(k));
    addInto(v, air(D, 5000.f, 11000.f, 0.45f, 695), 0.15f);
    return v;
}
std::vector<float> moveNekro() {  // a virus: a crushed digital drone that stutters and jumps
    const float D = 1.15f;
    Buf b(D);
    Ph a;
    Noise pick(701);
    float hold = 0.f, semis = 0.f;
    const size_t chunk = size_t(0.075f * float(R));
    std::vector<float> last(chunk, 0.f);
    bool repeat = false;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if (i % chunk == 0) {
            repeat = pick.next() > 0.45f && i > 0;
            if (pick.next() > 0.3f) semis = std::floor((pick.next() * 0.5f + 0.5f) * 3.f) * 5.f - 5.f;
        }
        const float x = a.saw(98.f * fexp(semis * 0.05776f)) * 0.7f + a.sq(196.f * fexp(semis * 0.05776f), 0.3f) * 0.3f;
        if (i % 6 == 0) hold = std::round(x * 6.f) / 6.f;  // fewer samples, fewer steps
        const float s = repeat ? last[i % chunk] : hold;
        if (!repeat) last[i % chunk] = hold;
        b.v[i] = s * passEnv(t, D, 0.38f);
    }
    std::vector<float> v = b.v;
    OnePole lp;
    for (float& x : v) x = lp.lp(x, 5000.f);
    return v;
}
std::vector<float> moveYssaril() {  // stealth: a quiet hiss and a cloak shimmering away
    const float D = 1.05f;
    std::vector<float> v = air(D, 3000.f, 7000.f, 0.3f, 711);
    Buf b(D);
    Ph a;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float trem = 0.6f + 0.4f * std::sin(kTwoPi * 18.f * t);
        b.v[i] = a.sine(glide(1400.f, 280.f, t, D)) * trem * passEnv(t, D, 0.2f);
    }
    addInto(v, b.v, 0.4f);
    return v;
}
std::vector<float> moveCreuss() {  // a ghost through a wormhole: phasing noise swelling to a cut, beating ghost tones
    const float D = 1.25f;
    Buf b(D);
    Noise nz(721);
    Band bd;
    std::vector<float> line(size_t(0.01f * float(R)) + 2, 0.f);
    size_t w = 0;
    Ph g1, g2, g3;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float sw = 0.001f + 0.005f * (0.5f + 0.5f * std::sin(kTwoPi * 1.3f * t));
        const size_t dl = size_t(sw * float(R));
        const float x = bd.bp(nz.next(), 300.f, 4000.f);
        const float y = x + line[(w + line.size() - dl) % line.size()] * 0.72f;
        line[w] = y;
        w = (w + 1) % line.size();
        const float e = t < 0.85f ? smooth01(t / 0.85f) : fexp(-(t - 0.85f) / 0.06f);
        const float ghost = (g1.sine(330.f) + g2.sine(333.5f) + 0.4f * g3.sine(495.f)) * 0.35f;
        b.v[i] = (y * 0.9f + ghost) * e;
    }
    return b.v;
}
std::vector<float> moveMuaat() {  // fire: a roar of flame over a magma rumble
    const float D = 1.25f;
    std::vector<float> v = fire(D * 0.7f, 0.f, 731, false);
    for (size_t i = 0; i < v.size(); ++i) v[i] *= passEnv(Buf::t(i), D * 0.7f + 0.12f, 0.35f) * 1.3f + 0.2f;
    Buf r(D);
    Noise nz(733);
    OnePole lp;
    Ph s;
    for (size_t i = 0; i < r.n(); ++i) {
        const float t = Buf::t(i);
        r.v[i] = (lp.lp(nz.next(), 120.f) * 6.f + s.sine(40.f) * 0.6f) * passEnv(t, D, 0.4f);
    }
    addInto(v, r.v, 0.8f);
    return v;
}
std::vector<float> moveSaar() {  // scrap: a sputtering engine, rattles, a cough at the end
    const float D = 1.25f;
    Buf b(D);
    Ph a;
    Noise pick(741);
    OnePole lp;
    float gate = 1.f, until = 0.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if (t >= until) {
            gate = pick.next() > -0.35f ? 1.f : 0.f;
            until = t + 0.02f + 0.05f * (pick.next() * 0.5f + 0.5f);
        }
        b.v[i] = lp.lp(soft(a.sq(70.f * (1.f + 0.06f * pick.next()), 0.3f) * 2.f), 1500.f) * gate * passEnv(t, D, 0.4f);
    }
    std::vector<float> v = b.v;
    for (int k = 0; k < 9; ++k) addInto(v, ping(2000.f + 2200.f * (pick.next() * 0.5f + 0.5f), 0.008f), 0.35f, 0.05f + 0.11f * float(k) + 0.03f * pick.next());
    Buf c(0.12f);
    Noise nz(743);
    Band bd;
    for (size_t i = 0; i < c.n(); ++i) c.v[i] = bd.bp(nz.next(), 300.f, 1500.f) * env(Buf::t(i), 0.005f, 0.03f) * 2.f;
    addInto(v, c.v, 0.6f, 0.95f);
    return v;
}
std::vector<float> moveWinnu() {  // regal: a swelling chord of soft brass, a little glide upward
    const float D = 1.5f;
    Buf b(D);
    Ph p[8];
    OnePole lp, lp2;
    const float chord[4] = {196.f, 246.9f, 293.7f, 392.f};
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), up = glide(1.f, 1.03f, t, D);
        float x = 0.f;
        for (int k = 0; k < 4; ++k) x += p[k * 2].saw(chord[k] * up * 0.997f) + p[k * 2 + 1].saw(chord[k] * up * 1.003f);
        const float e = passEnv(t, D, 0.45f);
        b.v[i] = lp2.lp(lp.lp(x * 0.25f, 500.f + 2000.f * e), 3000.f) * e;
    }
    std::vector<float> v = b.v;
    addInto(v, air(D, 6000.f, 12000.f, 0.45f, 751), 0.15f);
    return v;
}
std::vector<float> moveYin() {  // a monastery gong and a low drone
    const float D = 1.55f;
    const float f[5] = {92.f, 147.f, 211.f, 318.f, 431.f};
    std::vector<float> v = metal(f, 5, 0.45f, 0.15f, 761);
    v.resize(size_t(R * D), 0.f);
    for (size_t i = 0; i < v.size(); ++i) v[i] *= 0.85f + 0.15f * std::sin(kTwoPi * 2.3f * Buf::t(i));
    addInto(v, engine(D, 92.f, 350.f, 0.f, 0.02f, 0.5f), 0.35f);
    return v;
}
std::vector<float> moveArgent() {  // a flight of birds: three wing beats and a high cry
    const float D = 1.25f;
    std::vector<float> v(size_t(R * D), 0.f);
    for (int k = 0; k < 3; ++k) addInto(v, air(0.2f, 400.f, 2600.f, 0.45f, 771 + k), 0.9f, 0.22f * float(k));
    Buf c(0.4f);
    Ph a;
    for (size_t i = 0; i < c.n(); ++i) {
        const float t = Buf::t(i);
        const float f = (t < 0.12f ? glide(1900.f, 2500.f, t, 0.12f) : glide(2500.f, 1700.f, t - 0.12f, 0.28f)) * (1.f + 0.02f * std::sin(kTwoPi * 9.f * t));
        c.v[i] = a.sine(f) * env(t, 0.03f, 0.13f);
    }
    addInto(v, c.v, 0.3f, 0.58f);
    addInto(v, air(D, 800.f, 3500.f, 0.5f, 775), 0.3f);
    return v;
}
std::vector<float> moveEmpyrean() {  // the void: a shimmer rising out of a slow choir, a sub far below
    const float D = 1.55f;
    Buf b(D);
    Ph p[6], sub;
    const float f[3] = {523.f, 659.f, 784.f};
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float vib = 1.f + 0.006f * std::sin(kTwoPi * 4.5f * t);
        float x = 0.f;
        for (int k = 0; k < 3; ++k) x += p[k * 2].sine(f[k] * vib * 0.996f) + p[k * 2 + 1].sine(f[k] * vib * 1.004f);
        b.v[i] = (x * 0.2f + sub.sine(55.f) * 0.4f) * passEnv(t, D, 0.55f);
    }
    std::vector<float> v = b.v;
    addInto(v, star(false), 0.5f, 0.15f);
    return v;
}
std::vector<float> moveMahact() {  // sorcery: a dark wavering hum, whispers, arcane sparks
    const float D = 1.35f;
    Buf b(D);
    Ph a;
    OnePole lp;
    Noise nz(781);
    Band bd;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = 55.f * (1.f + 0.02f * std::sin(kTwoPi * 4.f * t));
        const float whisper = bd.bp(nz.next(), 1000.f, 3000.f) * (0.5f + 0.5f * std::sin(kTwoPi * 7.f * t)) * 0.6f;
        b.v[i] = (lp.lp(a.saw(f), 380.f) + whisper) * passEnv(t, D, 0.42f);
    }
    std::vector<float> v = b.v;
    Noise pick(783);
    for (int k = 0; k < 10; ++k) addInto(v, ping(3000.f + 3000.f * (pick.next() * 0.5f + 0.5f), 0.04f), 0.18f, 0.08f + 0.11f * float(k));
    return v;
}
std::vector<float> moveNaazRokha() {  // mechs and drills: tread clanks, a drill whine, an engine
    const float D = 1.25f;
    std::vector<float> v = engine(D, 70.f, 600.f, 0.5f, 0.05f, 0.4f, 1.6f);
    const float f[3] = {400.f, 870.f, 1350.f};
    std::vector<float> clank = metal(f, 3, 0.025f, 1.f, 791);
    for (int k = 0; k < 10; ++k) addInto(v, clank, 0.4f * passEnv(0.11f * float(k) + 0.05f, D, 0.4f) + 0.05f, 0.11f * float(k));
    Buf d(D);
    Ph a;
    Band bd;
    for (size_t i = 0; i < d.n(); ++i) {
        const float t = Buf::t(i);
        d.v[i] = bd.bp(a.saw(880.f * (1.f + 0.01f * std::sin(kTwoPi * 13.f * t))), 600.f, 3000.f) * passEnv(t, D, 0.4f);
    }
    addInto(v, d.v, 0.35f);
    return v;
}
std::vector<float> moveNomad() {  // an ancient wanderer: a warbling drone, a lone horn, an echo
    const float D = 1.5f;
    Buf b(D);
    Ph a, c, h;
    OnePole lp, lh;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float wow = 1.f + 0.012f * std::sin(kTwoPi * 0.9f * t);
        const float drone = lp.lp(a.saw(73.f * wow) + c.saw(110.f * wow), 700.f) * passEnv(t, D, 0.45f);
        const float he = passEnv(t - 0.25f, 0.9f, 0.45f);
        const float horn = lh.lp(h.saw(147.f * wow), 300.f + 900.f * he) * he;
        b.v[i] = drone * 0.6f + horn * 0.7f;
    }
    std::vector<float> v = b.v;
    echo(v, 0.18f, 0.35f, 0.4f);
    return v;
}
std::vector<float> moveTitans() {  // titanic: a boom, a rumble from the deep, grinding stone
    const float D = 1.7f;
    Buf b(D);
    Ph a, c;
    Noise nz(801);
    OnePole lp, rough;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float r = 0.5f + 0.5f * rough.lp(nz.next(), 12.f) * 10.f;
        const float grind = lp.lp(nz.next(), 400.f) * 4.f * r;
        b.v[i] = (a.sine(34.f) + 0.6f * c.sine(51.f) + grind) * passEnv(t, D, 0.4f);
    }
    std::vector<float> v = b.v;
    addInto(v, drop(62.f, 30.f, 0.25f, 0.4f), 1.1f, 0.f);
    return v;
}
std::vector<float> moveVuilraith() {  // the Cabal: a growl, wet gurgling, a rift tearing open
    const float D = 1.35f;
    Buf b(D);
    Ph a;
    Noise nz(811);
    OnePole fm, lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = 68.f * (1.f + 0.08f * fm.lp(nz.next(), 40.f) * 6.f);
        b.v[i] = lp.lp(soft(a.saw(f) * 2.2f), 900.f) * passEnv(t, D, 0.4f);
    }
    std::vector<float> v = b.v;
    std::vector<float> g = slimeStream();
    for (size_t i = 0; i < g.size(); ++i) g[i] *= passEnv(Buf::t(i), float(g.size()) / float(R), 0.4f);
    addInto(v, g, 0.45f, 0.1f);
    Buf tear(0.35f);
    Noise n2(813);
    Band bd;
    for (size_t i = 0; i < tear.n(); ++i) {
        const float t = Buf::t(i);
        const float f = glide(3000.f, 600.f, t, 0.3f);
        tear.v[i] = bd.bp(n2.next(), f * 0.6f, f * 1.5f) * env(t, 0.01f, 0.1f) * 2.f;
    }
    addInto(v, tear.v, 0.5f, 0.6f);
    return v;
}
std::vector<float> moveKeleres() {  // the Council's authority: a stately brass swell over a steady engine, a soft roll
    const float D = 1.35f;
    Buf b(D);
    Ph p[6];
    OnePole lp;
    const float f[3] = {196.f, 293.7f, 392.f};
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float vib = 1.f + 0.004f * std::sin(kTwoPi * 5.f * t);
        float x = 0.f;
        for (int k = 0; k < 3; ++k) x += p[k * 2].saw(f[k] * vib) + p[k * 2 + 1].saw(f[k] * vib * 1.004f);
        const float e = passEnv(t, D, 0.5f);
        b.v[i] = lp.lp(x * 0.25f, 500.f + 2100.f * e) * e;
    }
    std::vector<float> v = b.v;
    addInto(v, engine(D, 82.f, 500.f, 0.3f, 0.03f, 0.45f), 0.5f);
    Buf roll(0.35f);
    Noise nz(821);
    Band bd;
    for (size_t i = 0; i < roll.n(); ++i) {
        const float t = Buf::t(i);
        const float hit = fexp(-std::fmod(t, 0.045f) / 0.012f);
        roll.v[i] = bd.bp(nz.next(), 800.f, 5000.f) * hit * smooth01(t / 0.3f) * 1.5f;
    }
    addInto(v, roll.v, 0.3f, 0.f);
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

int main(int argc, char** argv) {
    using namespace audio;
    const std::string dir = argc > 1 ? argv[1] : ".";
    struct S { const char* name; std::vector<float> (*make)(); float loud; };
    const S list[] = {
        {"attack_a_lockon", attackLock, 0.13f},       {"attack_b_klaxon", attackKlaxon, 0.13f},
        {"attack_c_reticle", attackReticle, 0.13f},   {"invade_a_droppods", invadePods, 0.13f},
        {"invade_b_landingburn", invadeBurn, 0.13f}, {"invade_c_drums", invadeDrums, 0.13f},
        {"move_sol", moveSol, 0.1f},                  {"move_letnev", moveLetnev, 0.1f},
        {"move_hacan", moveHacan, 0.1f},              {"move_jolnar", moveJolNar, 0.1f},
        {"move_sardakk", moveSardakk, 0.1f},          {"move_xxcha", moveXxcha, 0.09f},
        {"move_arborec", moveArborec, 0.1f},          {"move_l1z1x", moveL1Z1X, 0.1f},
        {"move_mentak", moveMentak, 0.1f},            {"move_naalu", moveNaalu, 0.09f},
        {"move_nekro", moveNekro, 0.09f},             {"move_yssaril", moveYssaril, 0.07f},
        {"move_creuss", moveCreuss, 0.1f},            {"move_muaat", moveMuaat, 0.1f},
        {"move_saar", moveSaar, 0.1f},                {"move_winnu", moveWinnu, 0.1f},
        {"move_yin", moveYin, 0.1f},                  {"move_argent", moveArgent, 0.1f},
        {"move_empyrean", moveEmpyrean, 0.09f},       {"move_mahact", moveMahact, 0.1f},
        {"move_naazrokha", moveNaazRokha, 0.1f},      {"move_nomad", moveNomad, 0.1f},
        {"move_titans", moveTitans, 0.11f},           {"move_vuilraith", moveVuilraith, 0.1f},
        {"move_keleres", moveKeleres, 0.1f},
    };
    for (const S& s : list) {
        std::vector<float> v = s.make();
        bool bad = false;
        for (float x : v) bad |= !(x == x) || std::fabs(x) > 1e6f;
        const std::vector<int16_t> out = mapSound(v, s.loud);
        double sq = 0, dc = 0;
        int peak = 0;
        for (int16_t x : out) sq += double(x) * x, dc += x, peak = std::max(peak, std::abs(int(x)));
        std::printf("%-22s %4.2f s  peak %5d  rms %5.0f  dc %6.1f %s\n", s.name, double(out.size()) / R, peak,
                    std::sqrt(sq / double(out.size())), dc / double(out.size()), bad ? "BAD" : "");
        writeWav(dir + "/" + s.name + ".wav", out);
    }
    return 0;
}
