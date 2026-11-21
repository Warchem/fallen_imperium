// The grim sounds (0.69.0, owner 2026-11-19: "Go ahead and add these") - included by synth.cpp inside its namespace.
// Built by tools/sound_drafts/make_grim_inc.py from the sound drafts he kept on the review page (tools/sound_drafts/draft2-4.cpp,
// rounds 2-4: "too cheery ... more gritty and grim"): the toolkit (resonant filters, grit, a dark hall, the mastering
// grim()) and the recipes - the short siren, the three landings and every faction's movement sound.
// Same maths rule as synth.cpp: only sin / cos / floor / frexp / ldexp (no expf / logf / powf).

// ------------------------------------------------------------------ the toolkit (round 2)
float fmodSafe(float x, float y) { return x - std::floor(x / y) * y; }  // (fmodf: kept off the maths library list)
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

// ------------------------------------------------------------------ more of it (round 3)
// a deep, dark bell or toll: a struck low metal ringing long
std::vector<float> toll(float f, float tau, uint32_t seed) {
    const float res[5] = {f, f * 2.04f, f * 2.97f, f * 4.21f, f * 5.43f};
    return clank(res, 5, 120.f, tau, seed);
}
// a low sonar ping: a short sine that rings into the hall
std::vector<float> sonar(float f, float tau) {
    Buf b(tau * 6.f);
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        b.v[i] = std::sin(kTwoPi * f * t * (1.f - 0.02f * t)) * env(t, 0.004f, tau);
    }
    return b.v;
}
// bubbles: little rising chirps at random moments
std::vector<float> bubbles(float dur, float rate, float fLo, float fHi, float peakAt, uint32_t seed) {
    std::vector<float> v(size_t(dur * float(R)), 0.f);
    Noise pick(seed);
    const int count = int(rate * dur);
    for (int k = 0; k < count; ++k) {
        const float at = uni(pick) * dur * 0.9f, f0 = fLo + (fHi - fLo) * uni(pick), len = 0.02f + 0.04f * uni(pick);
        Buf b(len);
        float ph = 0;
        for (size_t i = 0; i < b.n(); ++i) {
            const float t = Buf::t(i);
            ph += kTwoPi * f0 * (1.f + 2.5f * t / len) / float(R);
            b.v[i] = std::sin(ph) * std::sin(3.14159f * t / len);
        }
        addInto(v, b.v, 0.35f * (passEnv(at, dur, peakAt) + 0.1f), at);
    }
    return v;
}
// a slow moan (a whale, a leviathan): a voice-like tone gliding through formants
std::vector<float> moan(float dur, float f0, float f1, float formant, float peakAt) {
    Buf b(dur);
    Ph a, s;
    BP fm, fm2;
    fm.set(formant, 3.f);
    fm2.set(formant * 2.3f, 5.f);
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = (t < dur * 0.5f ? glide(f0, f1, t, dur * 0.5f) : glide(f1, f0 * 0.9f, t - dur * 0.5f, dur * 0.5f)) * (1.f + 0.01f * std::sin(kTwoPi * 5.f * t));
        const float x = a.saw(f) * 0.7f + s.sine(f) * 0.6f;
        b.v[i] = (fm.run(x) + 0.5f * fm2.run(x) + s.sine(f) * 0.15f) * passEnv(t, dur, peakAt) * 1.4f;
    }
    return b.v;
}
// clockwork: regular ticks, a little uneven
std::vector<float> ticks(float dur, float every, float f, float peakAt, uint32_t seed) {
    std::vector<float> v(size_t(dur * float(R)), 0.f);
    const float res[2] = {f, f * 1.73f};
    std::vector<float> one = clank(res, 2, 25.f, 0.008f, seed);
    Noise pick(seed + 3);
    for (float at = 0.02f; at < dur - 0.05f; at += every * (0.9f + 0.2f * uni(pick))) addInto(v, one, 0.4f * (passEnv(at, dur, peakAt) + 0.15f), at);
    return v;
}
// a welder's arc: a harsh bright crackle with an electric buzz, darkened later by the room
std::vector<float> weld(float dur, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed), pick(seed + 1);
    Ph a;
    Band bd;
    float g = 0.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if (uni(pick) < 300.f / float(R)) g = 0.4f + 0.6f * uni(pick);
        g *= 0.996f;
        b.v[i] = (bd.bp(nz.next(), 1500.f, 6000.f) * g * 2.f + a.sq(120.f, 0.2f) * 0.15f * g) * env(t, 0.01f, dur * 0.5f);
    }
    return b.v;
}
// a crowd of engines passing in waves (a flotilla, a swarm): n detuned engines, each passing at its own moment
std::vector<float> flotilla(float dur, int n, float f, float spread, float cutoff, float drive, uint32_t seed) {
    std::vector<float> v(size_t(dur * float(R)), 0.f);
    Noise pick(seed);
    for (int k = 0; k < n; ++k) {
        const float len = dur * (0.55f + 0.3f * uni(pick)), at = (dur - len) * uni(pick);
        addInto(v, heavyEngine(len, f * semis(spread * pick.next()), cutoff, drive, 0.06f, 0.4f), 0.8f / float(n) * 2.f, at);
    }
    return v;
}
// a small, fast craft buzzing past (fighters, drones)
std::vector<float> buzzer(float dur, float f, float cutoff, uint32_t seed) {
    Buf b(dur);
    Ph a;
    Band bd;
    Noise nz(seed);
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), d = doppler(t, dur, 0.18f, 0.45f);
        b.v[i] = soft(bd.bp(a.saw(f * d) + nz.next() * 0.3f, 250.f, cutoff) * 3.f) * passEnv(t, dur, 0.45f);
    }
    return b.v;
}

// ================================================================== targeting a tile
// ATTACK B, shorter: the siren's rise, a brief hold and the fall, in about 1.1 s (the arrow and the tile flash)
std::vector<float> attackSirenShort() {
    const float D = 1.15f;
    Buf b(D);
    Ph a, c, d, chop, sub;
    OnePole lp;
    BP res;
    res.set(900.f, 1.5f);
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        float f;
        if (t < 0.38f) f = 150.f + (520.f - 150.f) * smooth01(t / 0.38f);
        else if (t < 0.6f) f = 520.f * (1.f + 0.012f * std::sin(kTwoPi * 6.f * t));
        else f = 520.f - (520.f - 150.f) * smooth01((t - 0.6f) / 0.55f);
        const float rotor = 0.78f + 0.22f * chop.sine(f * 0.08f);
        float x = a.saw(f) + 0.8f * c.saw(f * semis(3.f)) + 0.4f * d.sq(f * 0.5f);
        x = soft(x * 2.6f) * rotor;
        const float y = lp.lp(x, 2600.f) * 0.7f + res.run(x) * 0.5f;
        const float e = smooth01(t / 0.07f) * (t < D - 0.25f ? 1.f : (D - t) / 0.25f);
        b.v[i] = (y + sub.sine(f * 0.25f) * 0.2f) * e;
    }
    std::vector<float> v = b.v;
    addInto(v, staticBurst(D, 1500.f, 5000.f, 2911), 0.03f);
    addInto(v, rumble(D, 120.f, 0.45f, 2913), 0.25f);
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

// ================================================================== the factions on the move
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
// ARGENT, no screech: great wings beating hard, the rush of air, a low engine under them
std::vector<float> moveArgent3() {
    const float D = 1.35f;
    std::vector<float> v = heavyEngine(D, 66.f, 450.f, 2.f, 0.05f, 0.4f);
    for (int k = 0; k < 4; ++k) {
        const float at = 0.03f + 0.27f * float(k);
        addInto(v, hiss(0.26f, 90.f, 700.f, 0.09f, 0.07f, 2921 + k), 1.1f * (passEnv(at + 0.1f, D, 0.4f) + 0.3f), at);
        addInto(v, thump(70.f, 45.f, 0.05f, 0.06f), 0.35f, at + 0.08f);  // the downstroke
    }
    addInto(v, hiss(D, 600.f, 2500.f, 0.5f, 0.4f, 2929), 0.25f);  // the rush of air
    grit(v, 0.4f, 1.8f, 2931);
    return v;
}
std::vector<float> moveBastion() {  // survivors: a battered hull groaning, a stubborn engine that stutters but holds, galvanic crackle
    const float D = 1.4f;
    std::vector<float> v = heavyEngine(D, 52.f, 520.f, 2.6f, 0.04f, 0.42f);
    Noise pick(3001);
    for (int k = 0; k < 3; ++k) {  // the engine catching
        const float at = 0.15f + 0.3f * float(k) + 0.05f * pick.next();
        const size_t a = size_t(at * float(R)), n = size_t(0.05f * float(R));
        for (size_t i = a; i < a + n && i < v.size(); ++i) v[i] *= 0.25f;
    }
    const float res[3] = {190.f, 470.f, 860.f};
    addInto(v, groan(D, 30.f, res, 3, 30.f, 0.42f, 3003), 0.35f);
    for (int k = 0; k < 4; ++k) addInto(v, staticBurst(0.05f, 2500.f, 7000.f, 3005 + k), 0.35f, 0.2f + 0.28f * float(k));
    grit(v, 0.6f, 2.2f, 3011);
    return v;
}
std::vector<float> moveRalNel() {  // logistics: cargo clattering in its racks, hydraulics snapping, a tight compact engine
    const float D = 1.25f;
    std::vector<float> v = heavyEngine(D, 74.f, 650.f, 2.f, 0.06f, 0.38f);
    addInto(v, rattle(D, 22.f, 600.f, 1800.f, 0.38f, 3021), 0.6f);
    for (int k = 0; k < 3; ++k) {
        addInto(v, hiss(0.12f, 2500.f, 7000.f, 0.005f, 0.035f, 3023 + k), 0.35f, 0.12f + 0.32f * float(k));
        const float res[3] = {260.f, 610.f, 1150.f};
        addInto(v, clank(res, 3, 22.f, 0.04f, 3027 + k), 0.4f, 0.16f + 0.32f * float(k));
    }
    grit(v, 0.4f, 1.8f, 3031);
    return v;
}
std::vector<float> moveDeepwrought() {  // the deep: a low sonar ping, water pressing on the hull, bubbles, a distant moan
    const float D = 1.6f;
    std::vector<float> v = rumble(D, 160.f, 0.45f, 3041);
    addInto(v, sonar(392.f * semis(-12.f), 0.25f), 0.5f, 0.05f);
    addInto(v, sonar(392.f * semis(-13.f), 0.25f), 0.3f, 0.75f);  // the echo back, a half-step low
    addInto(v, bubbles(D, 30.f, 180.f, 600.f, 0.45f, 3043), 0.5f);
    addInto(v, moan(1.1f, 98.f, 82.f, 380.f, 0.5f), 0.35f, 0.35f);
    const float res[2] = {210.f, 520.f};
    addInto(v, groan(D, 20.f, res, 2, 25.f, 0.5f, 3045), 0.25f);
    return v;
}
std::vector<float> moveCrimson() {  // a breach: reality ripping open, red static pouring through, a roar sucked into the tear
    const float D = 1.35f;
    Buf r(D);
    Noise nz(3051);
    Band bd;
    OnePole lp;
    Ph sub;
    for (size_t i = 0; i < r.n(); ++i) {
        const float t = Buf::t(i);
        const float f = glide(300.f, 3500.f, t, 0.5f);
        const float rip = bd.bp(nz.next(), f * 0.5f, f) * (t < 0.55f ? smooth01(t / 0.55f) : fexp(-(t - 0.55f) / 0.12f)) * 3.f;
        const float suck = lp.lp(sub.saw(glide(90.f, 30.f, t, D)), 200.f) * smooth01(t / D) * (t < D - 0.08f ? 1.f : 0.f);
        r.v[i] = rip + suck * 0.8f;
    }
    std::vector<float> v = r.v;
    addInto(v, staticBurst(0.8f, 800.f, 4000.f, 3053), 0.3f, 0.45f);
    crush(v, 4, 12.f, 0.3f);
    grit(v, 0.8f, 2.6f, 3055);
    return v;
}
std::vector<float> moveFirmament() {  // hidden hands: a hushed veil of tone, whispers layered over each other, clockwork ticking
    const float D = 1.45f;
    const float f[3] = {98.f, 103.8f, 138.6f};
    std::vector<float> v = drone(D, f, 3, 450.f, 0.003f, 0.5f);
    Buf w(D);
    Noise nz(3061);
    Band b1, b2;
    for (size_t i = 0; i < w.n(); ++i) {
        const float t = Buf::t(i);
        const float n = nz.next();
        w.v[i] = (b1.bp(n, 1200.f, 3000.f) * (0.5f + 0.5f * std::sin(kTwoPi * 5.3f * t)) + b2.bp(n, 2500.f, 5000.f) * (0.5f + 0.5f * std::sin(kTwoPi * 3.1f * t + 1.f))) * passEnv(t, D, 0.5f);
    }
    addInto(v, w.v, 0.6f);
    addInto(v, ticks(D, 0.16f, 1900.f, 0.5f, 3063), 0.5f);
    return v;
}
std::vector<float> moveObsidian() {  // malevolence revealed: a black glassy rumble, a distorted low choir, glass cracking
    const float D = 1.5f;
    const float f[4] = {65.4f, 69.3f, 92.5f, 98.f};
    std::vector<float> v = chant(D, f, 4, 480.f, 900.f, 0.008f, 0.5f);
    for (float& x : v) x = soft(x * 3.f);
    addInto(v, rumble(D, 110.f, 0.5f, 3071), 0.7f);
    for (int k = 0; k < 3; ++k) {
        const float res[3] = {1700.f, 2900.f, 4300.f};
        addInto(v, clank(res, 3, 40.f, 0.03f, 3073 + k), 0.3f, 0.3f + 0.35f * float(k));
        addInto(v, staticBurst(0.08f, 2500.f, 8000.f, 3077 + k), 0.25f, 0.3f + 0.35f * float(k));
    }
    grit(v, 0.5f, 2.2f, 3081);
    return v;
}
std::vector<float> moveAxis() {  // a shipyard: hammers on hull plate, a welder's arc, chains on a crane, a slow engine
    const float D = 1.45f;
    std::vector<float> v = heavyEngine(D, 50.f, 450.f, 2.2f, 0.03f, 0.45f);
    const float res[4] = {150.f, 380.f, 760.f, 1420.f};
    for (int k = 0; k < 5; ++k) addInto(v, clank(res, 4, 30.f, 0.09f, 3101 + k), 0.55f, 0.06f + 0.27f * float(k));
    addInto(v, weld(0.5f, 3107), 0.3f, 0.45f);
    addInto(v, rattle(D, 12.f, 400.f, 1200.f, 0.45f, 3109), 0.4f);
    grit(v, 0.5f, 2.f, 3111);
    return v;
}
std::vector<float> moveBentor() {  // treasure hunters: a rattling hold full of salvaged relics, a sweeping scanner, a rough engine
    const float D = 1.35f;
    std::vector<float> v = heavyEngine(D, 60.f, 550.f, 2.f, 0.05f, 0.4f);
    addInto(v, rattle(D, 30.f, 1200.f, 3200.f, 0.4f, 3121), 0.5f);
    Buf s(D);
    Ph a;
    for (size_t i = 0; i < s.n(); ++i) {
        const float t = Buf::t(i);
        const float sweep = 0.5f + 0.5f * std::sin(kTwoPi * 1.6f * t);
        s.v[i] = a.sine(180.f + 90.f * sweep) * (0.3f + 0.7f * sweep) * passEnv(t, D, 0.4f);
    }
    addInto(v, s.v, 0.25f);
    grit(v, 0.5f, 2.f, 3123);
    return v;
}
std::vector<float> moveKjalengard() {  // berserkers: a distorted war horn, war drums pounding, a roaring engine
    const float D = 1.45f;
    std::vector<float> v = heavyEngine(D, 55.f, 600.f, 3.f, 0.05f, 0.4f);
    const float horn[2] = {82.4f, 87.3f};
    std::vector<float> h = drone(0.8f, horn, 2, 900.f, 0.004f, 0.35f);
    for (float& x : h) x = soft(x * 4.f);
    addInto(v, h, 0.6f, 0.05f);
    for (int k = 0; k < 5; ++k) addInto(v, thump(105.f, 50.f, 0.05f, 0.1f), 0.8f, 0.55f + 0.16f * float(k));
    grit(v, 0.6f, 2.4f, 3131);
    return v;
}
std::vector<float> moveCeldauri() {  // a trade port: docking clamps locking, port machinery humming, containers banging
    const float D = 1.4f;
    Buf h(D);
    Ph a, c;
    OnePole lp;
    for (size_t i = 0; i < h.n(); ++i) {
        const float t = Buf::t(i);
        h.v[i] = lp.lp(a.saw(60.f) + 0.6f * c.saw(120.f * semis(0.2f)), 500.f) * passEnv(t, D, 0.5f);
    }
    std::vector<float> v = h.v;
    const float res[4] = {120.f, 300.f, 640.f, 1200.f};
    addInto(v, clank(res, 4, 25.f, 0.15f, 3141), 0.7f, 0.1f);
    addInto(v, hiss(0.2f, 2500.f, 7000.f, 0.005f, 0.05f, 3143), 0.3f, 0.12f);
    addInto(v, clank(res, 4, 25.f, 0.15f, 3145), 0.6f, 0.75f);
    addInto(v, rattle(D, 10.f, 300.f, 900.f, 0.5f, 3147), 0.5f);
    grit(v, 0.4f, 1.8f, 3149);
    return v;
}
std::vector<float> moveCheiran() {  // a crustacean horde: claws clacking, armoured shell plates grinding, a slow heavy engine
    const float D = 1.45f;
    std::vector<float> v = heavyEngine(D, 44.f, 420.f, 2.5f, 0.03f, 0.45f);
    const float res[2] = {1400.f, 2600.f};
    Noise pick(3151);
    for (int k = 0; k < 16; ++k) addInto(v, clank(res, 2, 10.f, 0.01f, 3153 + k), 0.3f, uni(pick) * D * 0.9f);
    const float gr[3] = {300.f, 760.f, 1500.f};
    addInto(v, groan(D, 26.f, gr, 3, 15.f, 0.45f, 3171), 0.35f);
    grit(v, 0.5f, 2.f, 3173);
    return v;
}
std::vector<float> moveCymiae() {  // a cybernetic hive: a layered droning buzz, a grinder chewing scrap, broken data
    const float D = 1.35f;
    std::vector<float> v(size_t(D * float(R)), 0.f);
    for (int k = 0; k < 3; ++k) addInto(v, buzzer(D, 88.f * semis(float(k) * 0.4f), 1300.f, 3181 + k), 0.4f);
    Buf g(D);
    Noise nz(3185);
    Band bd;
    for (size_t i = 0; i < g.n(); ++i) {
        const float t = Buf::t(i);
        g.v[i] = bd.bp(nz.next(), 400.f, 1600.f) * (0.5f + 0.5f * std::sin(kTwoPi * 27.f * t)) * passEnv(t, D, 0.45f) * 1.6f;
    }
    addInto(v, g.v, 0.4f);
    addInto(v, staticBurst(0.3f, 2000.f, 6000.f, 3187), 0.2f, 0.6f);
    crush(v, 6, 9.f, 0.4f);
    grit(v, 0.5f, 2.f, 3189);
    return v;
}
std::vector<float> moveDihMohn() {  // a wandering armada: many engines passing in waves, the whole flotilla on the move
    const float D = 1.6f;
    std::vector<float> v = flotilla(D, 6, 58.f, 1.5f, 500.f, 2.2f, 3191);
    addInto(v, rumble(D, 130.f, 0.45f, 3193), 0.6f);
    grit(v, 0.4f, 1.8f, 3195);
    return v;
}
std::vector<float> moveEdyn() {  // the lawgivers: a single dark bell tolling, a minor hum like a hall of judgement
    const float D = 1.6f;
    std::vector<float> v = toll(73.4f, 0.5f, 3201);
    v.resize(size_t(D * float(R)), 0.f);
    const float f[3] = {73.4f, 87.3f, 110.f};
    addInto(v, chant(D, f, 3, 400.f, 850.f, 0.005f, 0.5f), 0.4f);
    grit(v, 0.3f, 1.6f, 3203);
    return v;
}
std::vector<float> moveFlorzen() {  // pirates with fighters: a pack of small craft buzzing past, a rough mothership engine
    const float D = 1.3f;
    std::vector<float> v = heavyEngine(D, 64.f, 550.f, 2.8f, 0.05f, 0.4f);
    Noise pick(3211);
    for (int k = 0; k < 4; ++k) {
        const float len = 0.5f + 0.2f * uni(pick);
        addInto(v, buzzer(len, 150.f * semis(4.f * pick.next()), 2200.f, 3213 + k), 0.35f, uni(pick) * (D - len));
    }
    grit(v, 0.6f, 2.2f, 3219);
    return v;
}
std::vector<float> moveFreeSystems() {  // the independent worlds: radio chatter crossing in static, a beacon pulsing, mixed engines
    const float D = 1.4f;
    std::vector<float> v = flotilla(D, 3, 70.f, 3.f, 600.f, 1.8f, 3221);
    for (int k = 0; k < 5; ++k) {
        std::vector<float> s = staticBurst(0.12f, 600.f + 300.f * float(k), 2600.f + 300.f * float(k), 3223 + k);
        addInto(v, s, 0.35f, 0.1f + 0.24f * float(k));
    }
    for (int k = 0; k < 3; ++k) addInto(v, sonar(330.f, 0.05f), 0.25f, 0.2f + 0.4f * float(k));
    grit(v, 0.4f, 1.8f, 3229);
    return v;
}
std::vector<float> moveGhemina() {  // twin flagships: two heavy engines beating against each other, a raiders' horn
    const float D = 1.45f;
    std::vector<float> v = heavyEngine(D, 55.f, 520.f, 2.6f, 0.05f, 0.42f);
    addInto(v, heavyEngine(D, 55.f * semis(0.6f), 520.f, 2.6f, 0.05f, 0.42f), 0.9f);  // beating against the first
    const float horn[2] = {110.f, 103.8f};
    std::vector<float> h = drone(0.7f, horn, 2, 800.f, 0.004f, 0.35f);
    for (float& x : h) x = soft(x * 3.5f);
    addInto(v, h, 0.5f, 0.35f);
    grit(v, 0.5f, 2.2f, 3231);
    return v;
}
std::vector<float> moveGhoti() {  // space nomads aboard a living home: a deep leviathan moan, the hull creaking, a slow drift
    const float D = 1.7f;
    std::vector<float> v = moan(D, 55.f, 73.4f, 300.f, 0.5f);
    const float res[3] = {170.f, 410.f, 830.f};
    addInto(v, groan(D, 16.f, res, 3, 20.f, 0.5f, 3241), 0.3f);
    addInto(v, rumble(D, 100.f, 0.5f, 3243), 0.6f);
    return v;
}
std::vector<float> moveGledge() {  // deep-core miners: a heavy drill biting into rock, rock crumbling, pistons pounding
    const float D = 1.45f;
    Buf d(D);
    Ph a;
    Noise nz(3251);
    Band bd;
    for (size_t i = 0; i < d.n(); ++i) {
        const float t = Buf::t(i);
        const float x = soft(a.saw(95.f * (1.f + 0.03f * std::sin(kTwoPi * 9.f * t))) * 3.f) * 0.6f + nz.next() * 0.6f;
        d.v[i] = bd.bp(x, 120.f, 1400.f) * (0.65f + 0.35f * std::sin(kTwoPi * 17.f * t)) * passEnv(t, D, 0.45f) * 1.5f;
    }
    std::vector<float> v = d.v;
    addInto(v, rattle(D, 24.f, 250.f, 800.f, 0.5f, 3253), 0.6f);  // rock falling
    for (int k = 0; k < 6; ++k) addInto(v, thump(70.f, 45.f, 0.04f, 0.06f), 0.5f, 0.05f + 0.22f * float(k));
    grit(v, 0.6f, 2.2f, 3255);
    return v;
}
std::vector<float> moveIlyxum() {  // the augurs: a premonition swelling backwards into a deep bell, whispers, a slow ticking
    const float D = 1.55f;
    std::vector<float> v(size_t(D * float(R)), 0.f);
    {  // a bell swelling backwards toward the moment: its partials rising out of silence, cut off at 1 s
        const float f[5] = {82.4f, 168.1f, 244.7f, 346.9f, 447.4f};
        for (size_t i = 0; i < size_t(1.0f * float(R)); ++i) {
            const float t = Buf::t(i);
            float x = 0.f;
            for (int k = 0; k < 5; ++k) x += std::sin(kTwoPi * f[k] * t) * fexp((t - 1.f) / (0.35f - 0.05f * float(k))) / float(k + 1);
            v[i] = x;
        }
    }
    addInto(v, toll(82.4f, 0.25f, 3263), 0.18f, 1.0f);
    addInto(v, ticks(D, 0.3f, 1500.f, 0.5f, 3265), 0.4f);
    addInto(v, hiss(D, 1500.f, 4000.f, 0.6f, 0.4f, 3267), 0.05f);
    return v;
}
std::vector<float> moveKollecc() {  // cloaked collectors: a muffled engine fading in and out of sight, a cloak's wavering hum
    const float D = 1.35f;
    std::vector<float> v = heavyEngine(D, 58.f, 380.f, 2.f, 0.04f, 0.4f);
    for (size_t i = 0; i < v.size(); ++i) v[i] *= 0.35f + 0.65f * (0.5f + 0.5f * std::sin(kTwoPi * 2.4f * Buf::t(i)));  // there, gone, there
    Buf c(D);
    Ph a, b;
    for (size_t i = 0; i < c.n(); ++i) {
        const float t = Buf::t(i);
        c.v[i] = (a.sine(233.f) + b.sine(246.9f)) * 0.5f * (0.5f + 0.5f * std::sin(kTwoPi * 2.4f * t + 3.14159f)) * passEnv(t, D, 0.4f);
    }
    addInto(v, c.v, 0.3f);
    return v;
}
std::vector<float> moveKolume() {  // warrior-ascetics: a low throat chant, a space cannon charging and holding
    const float D = 1.55f;
    const float f[2] = {61.7f, 62.1f};
    std::vector<float> v = chant(D, f, 2, 350.f, 1600.f, 0.004f, 0.5f);
    Buf c(D);
    Ph a;
    for (size_t i = 0; i < c.n(); ++i) {
        const float t = Buf::t(i);
        c.v[i] = soft(a.saw(glide(80.f, 160.f, t, D * 0.8f)) * 2.f) * smooth01(t / (D * 0.7f)) * (t < D - 0.1f ? 1.f : (D - t) / 0.1f);
    }
    OnePole lp;
    for (float& x : c.v) x = lp.lp(x, 700.f);
    addInto(v, c.v, 0.35f);
    grit(v, 0.3f, 1.6f, 3271);
    return v;
}
std::vector<float> moveKortali() {  // militant judges: a gavel's heavy slam, troops in step, stern low brass
    const float D = 1.5f;
    const float f[2] = {73.4f, 103.8f};  // a tritone: stern
    std::vector<float> v = drone(D, f, 2, 700.f, 0.003f, 0.5f);
    for (float& x : v) x = soft(x * 2.5f);
    const float res[4] = {110.f, 260.f, 540.f, 990.f};
    addInto(v, clank(res, 4, 15.f, 0.12f, 3281), 0.8f, 0.0f);
    addInto(v, thump(90.f, 40.f, 0.06f, 0.15f), 1.0f, 0.0f);
    for (int k = 0; k < 4; ++k) addInto(v, thump(100.f, 55.f, 0.04f, 0.05f), 0.5f, 0.45f + 0.22f * float(k));
    grit(v, 0.4f, 2.f, 3283);
    return v;
}
std::vector<float> moveKyro() {  // the plague: a wet sickly hiss, a cloud of flies whining, infection bubbling
    const float D = 1.45f;
    std::vector<float> v = hiss(D, 400.f, 2200.f, 0.6f, 0.5f, 3291);
    Buf f(D);
    Ph a, b;
    for (size_t i = 0; i < f.n(); ++i) {
        const float t = Buf::t(i);
        const float x = a.saw(220.f * (1.f + 0.06f * std::sin(kTwoPi * 7.f * t))) + b.saw(233.f * (1.f + 0.05f * std::sin(kTwoPi * 9.f * t + 1.f)));
        f.v[i] = x * 0.3f * (0.6f + 0.4f * std::sin(kTwoPi * 31.f * t)) * passEnv(t, D, 0.45f);
    }
    OnePole lp;
    for (float& x : f.v) x = lp.lp(x, 1800.f);
    addInto(v, f.v, 0.5f);
    addInto(v, bubbles(D, 40.f, 120.f, 400.f, 0.45f, 3293), 0.6f);
    addInto(v, rumble(D, 100.f, 0.45f, 3295), 0.5f);
    grit(v, 0.6f, 2.f, 3297);
    return v;
}
std::vector<float> moveLanefir() {  // a lost civilization: crumbling debris, a broken signal stuttering, a failing engine
    const float D = 1.45f;
    Buf b(D);
    Ph a;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        b.v[i] = lp.lp(soft(a.saw(glide(62.f, 44.f, t, D)) * 2.f), 500.f) * passEnv(t, D, 0.35f);  // winding down
    }
    std::vector<float> v = b.v;
    addInto(v, rattle(D, 20.f, 250.f, 900.f, 0.5f, 3301), 0.6f);
    Noise pick(3303);
    for (int k = 0; k < 6; ++k) {
        std::vector<float> s = sonar(520.f * semis(-2.f * float(k % 3)), 0.03f);
        addInto(v, s, uni(pick) > 0.3f ? 0.25f : 0.f, 0.08f + 0.2f * float(k));
    }
    addInto(v, staticBurst(D * 0.8f, 1000.f, 4000.f, 3305), 0.12f, 0.1f);
    grit(v, 0.5f, 2.f, 3307);
    return v;
}
std::vector<float> moveLizho() {  // tricksters: a sly low hum, mechanisms ticking, a trap snapping shut
    const float D = 1.4f;
    const float f[2] = {87.3f, 92.5f};
    std::vector<float> v = drone(D, f, 2, 400.f, 0.003f, 0.45f);
    addInto(v, ticks(1.0f, 0.09f, 2300.f, 0.5f, 3311), 0.5f, 0.05f);
    const float res[4] = {300.f, 820.f, 1500.f, 2600.f};
    addInto(v, clank(res, 4, 30.f, 0.06f, 3313), 0.9f, 1.02f);
    addInto(v, thump(85.f, 45.f, 0.05f, 0.08f), 0.7f, 1.02f);
    grit(v, 0.4f, 1.8f, 3315);
    return v;
}
std::vector<float> moveLtokk() {  // living stone: boulders rolling and grinding, a deep growl in the rock
    const float D = 1.55f;
    std::vector<float> v = rumble(D, 220.f, 0.45f, 3321);
    Noise pick(3323);
    for (int k = 0; k < 9; ++k) {
        const float at = uni(pick) * D * 0.85f;
        addInto(v, thump(70.f + 40.f * uni(pick), 35.f, 0.06f, 0.07f), 0.5f * (passEnv(at, D, 0.45f) + 0.2f), at);
        const float res[2] = {280.f + 200.f * uni(pick), 690.f};
        addInto(v, clank(res, 2, 8.f, 0.03f, 3325 + k), 0.3f, at);
    }
    Buf g(D);
    Ph a;
    Noise nz(3337);
    OnePole fm, lp;
    for (size_t i = 0; i < g.n(); ++i) {
        const float t = Buf::t(i);
        g.v[i] = lp.lp(soft(a.saw(46.f * (1.f + 0.06f * fm.lp(nz.next(), 20.f) * 8.f)) * 3.f), 400.f) * passEnv(t, D, 0.45f);
    }
    addInto(v, g.v, 0.5f);
    grit(v, 0.5f, 2.f, 3339);
    return v;
}
std::vector<float> moveMirveda() {  // a machine protectorate: a drone swarm whining, gun turrets servoing round, a cold hum
    const float D = 1.4f;
    std::vector<float> v(size_t(D * float(R)), 0.f);
    for (int k = 0; k < 4; ++k) addInto(v, buzzer(D * 0.8f, 210.f * semis(float(k) * 0.7f), 2600.f, 3341 + k), 0.25f, 0.06f * float(k));
    Buf s(D);
    Ph a;
    for (size_t i = 0; i < s.n(); ++i) {
        const float t = Buf::t(i);
        const float step = std::floor(t / 0.11f);
        s.v[i] = a.sq(140.f * semis(fmodSafe(step * 5.f, 7.f) - 3.f), 0.4f) * 0.4f * passEnv(t, D, 0.45f);  // servo steps
    }
    OnePole lp;
    for (float& x : s.v) x = lp.lp(x, 1500.f);
    addInto(v, s.v, 0.3f);
    addInto(v, heavyEngine(D, 50.f, 300.f, 1.6f, 0.03f, 0.45f), 0.6f);
    grit(v, 0.4f, 1.8f, 3349);
    return v;
}
std::vector<float> moveMyko() {  // a fungal network: a deep pulsing throb, wet squelching, spore clouds hissing
    const float D = 1.5f;
    Buf p(D);
    Ph a;
    OnePole lp;
    for (size_t i = 0; i < p.n(); ++i) {
        const float t = Buf::t(i);
        const float pulse = 0.5f + 0.5f * std::sin(kTwoPi * 2.2f * t);
        p.v[i] = lp.lp(a.sine(49.f) + a.sine(98.f) * 0.3f, 300.f) * pulse * pulse * passEnv(t, D, 0.45f);
    }
    std::vector<float> v = p.v;
    addInto(v, bubbles(D, 26.f, 90.f, 260.f, 0.45f, 3361), 0.7f);  // the squelch
    for (int k = 0; k < 3; ++k) addInto(v, hiss(0.35f, 1500.f, 5000.f, 0.08f, 0.1f, 3363 + k), 0.25f, 0.15f + 0.4f * float(k));
    grit(v, 0.4f, 1.8f, 3367);
    return v;
}
std::vector<float> moveNivyn() {  // star kings of the anomalies: a deep drone pulled and bent by gravity, a rift groaning
    const float D = 1.55f;
    Buf b(D);
    Ph a, c;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float bend = glide(1.f, semis(-7.f), t, D) * (1.f + 0.03f * std::sin(kTwoPi * 1.3f * t));
        b.v[i] = lp.lp(a.saw(82.f * bend) + c.saw(87.f * bend), 450.f) * passEnv(t, D, 0.4f);
    }
    std::vector<float> v = b.v;
    const float res[3] = {140.f, 330.f, 690.f};
    addInto(v, groan(D, 14.f, res, 3, 30.f, 0.5f, 3371), 0.4f);
    addInto(v, rumble(D, 90.f, 0.5f, 3373), 0.6f);
    grit(v, 0.3f, 1.8f, 3375);
    return v;
}
std::vector<float> moveNokar() {  // sellswords: lean destroyers tearing past fast, weapons rattling in their mounts
    const float D = 1.05f;
    std::vector<float> v(size_t(D * float(R)), 0.f);
    for (int k = 0; k < 2; ++k) addInto(v, heavyEngine(0.8f, 82.f * semis(float(k) * 0.5f), 900.f, 3.f, 0.12f, 0.35f), 0.7f, 0.12f * float(k));
    addInto(v, rattle(D, 30.f, 900.f, 2400.f, 0.35f, 3381), 0.5f);
    grit(v, 0.6f, 2.4f, 3383);
    return v;
}
std::vector<float> moveOlradin() {  // a federation of policies: steady engines in formation, stamping machinery, low brass
    const float D = 1.45f;
    std::vector<float> v = flotilla(D, 3, 62.f, 0.3f, 500.f, 1.8f, 3391);
    for (int k = 0; k < 4; ++k) {
        const float res[3] = {200.f, 480.f, 930.f};
        addInto(v, clank(res, 3, 20.f, 0.05f, 3393 + k), 0.4f, 0.12f + 0.3f * float(k));
    }
    const float f[2] = {98.f, 116.5f};
    addInto(v, drone(D, f, 2, 600.f, 0.003f, 0.5f), 0.35f);
    grit(v, 0.4f, 1.8f, 3399);
    return v;
}
std::vector<float> moveRohdhna() {  // a war-sun factory: an industrial press stamping, a conveyor rattling, a furnace roaring
    const float D = 1.5f;
    std::vector<float> v = fire(D * 0.7f, 0.f, 3401, false);
    for (size_t i = 0; i < v.size(); ++i) v[i] *= 0.45f;
    v.resize(size_t(D * float(R)), 0.f);
    const float res[4] = {90.f, 230.f, 480.f, 950.f};
    for (int k = 0; k < 4; ++k) {
        addInto(v, clank(res, 4, 25.f, 0.12f, 3403 + k), 0.7f, 0.05f + 0.36f * float(k));
        addInto(v, thump(80.f, 38.f, 0.06f, 0.1f), 0.9f, 0.05f + 0.36f * float(k));
        addInto(v, hiss(0.15f, 2500.f, 7000.f, 0.005f, 0.04f, 3407 + k), 0.3f, 0.12f + 0.36f * float(k));
    }
    addInto(v, rattle(D, 26.f, 500.f, 1400.f, 0.5f, 3411), 0.45f);
    grit(v, 0.6f, 2.2f, 3413);
    return v;
}
std::vector<float> moveTnelis() {  // spies: a muffled quick engine slipping past, whispered static, quiet sharp clicks
    const float D = 1.1f;
    std::vector<float> v = heavyEngine(D, 70.f, 300.f, 1.6f, 0.1f, 0.35f);
    addInto(v, staticBurst(D * 0.7f, 1500.f, 4500.f, 3421), 0.12f, 0.15f);
    const float res[1] = {3200.f};
    for (int k = 0; k < 4; ++k) addInto(v, clank(res, 1, 30.f, 0.005f, 3423 + k), 0.25f, 0.1f + 0.21f * float(k));
    return v;
}
std::vector<float> moveVaden() {  // loan sharks: vault tumblers clicking, a vault door clanging home, coins rattling, a cold hum
    const float D = 1.45f;
    Buf h(D);
    Ph a, c;
    OnePole lp;
    for (size_t i = 0; i < h.n(); ++i) {
        const float t = Buf::t(i);
        h.v[i] = lp.lp(a.saw(55.f) + c.saw(58.3f), 300.f) * passEnv(t, D, 0.5f);
    }
    std::vector<float> v = h.v;
    addInto(v, ticks(0.75f, 0.075f, 1700.f, 0.5f, 3431), 0.5f, 0.05f);
    const float res[5] = {85.f, 210.f, 445.f, 820.f, 1460.f};
    addInto(v, clank(res, 5, 40.f, 0.3f, 3433), 0.9f, 0.85f);
    addInto(v, thump(70.f, 32.f, 0.1f, 0.2f), 0.9f, 0.85f);
    addInto(v, rattle(0.5f, 40.f, 2500.f, 4500.f, 0.3f, 3435), 0.3f, 0.9f);
    grit(v, 0.4f, 1.8f, 3437);
    return v;
}
std::vector<float> moveVaylerian() {  // the scourge: a hunting engine rising to a snarl, chains dragging behind
    const float D = 1.35f;
    Buf b(D);
    Ph a, c;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = glide(48.f, 96.f, t, D * 0.6f);
        b.v[i] = lp.lp(soft((a.saw(f) + c.saw(f * semis(1.f))) * 4.f), 1200.f) * passEnv(t, D, 0.5f);
    }
    std::vector<float> v = b.v;
    addInto(v, rattle(D, 34.f, 500.f, 1600.f, 0.5f, 3441), 0.6f);
    grit(v, 0.7f, 2.4f, 3443);
    return v;
}
std::vector<float> moveVeldyr() {  // a corporation's dreadnoughts: a smooth heavy engine, servos, a cold low tone sounding twice
    const float D = 1.45f;
    std::vector<float> v = heavyEngine(D, 47.f, 420.f, 1.8f, 0.03f, 0.45f);
    Buf c(0.9f);
    Ph a;
    for (size_t i = 0; i < c.n(); ++i) {
        const float t = Buf::t(i);
        const float f = t < 0.35f ? 196.f : 185.f;  // two notes, falling a half-step: cold
        const float e = t < 0.35f ? env(t, 0.01f, 0.12f) : env(t - 0.35f, 0.01f, 0.2f);
        c.v[i] = soft(a.sq(f, 0.5f) * 1.5f) * e;
    }
    OnePole lp;
    for (float& x : c.v) x = lp.lp(x, 1200.f);
    addInto(v, c.v, 0.3f, 0.2f);
    addInto(v, hiss(0.2f, 2000.f, 6000.f, 0.01f, 0.05f, 3451), 0.2f, 0.8f);
    grit(v, 0.3f, 1.6f, 3453);
    return v;
}
std::vector<float> moveRhodun() {  // fanatics: a chant over crackling fire, sparks of broken technology
    const float D = 1.5f;
    const float f[2] = {69.3f, 73.4f};
    std::vector<float> v = chant(D, f, 2, 420.f, 1100.f, 0.006f, 0.5f);
    std::vector<float> fr = fire(D * 0.7f, 0.f, 3461, false);
    addInto(v, fr, 0.4f, 0.1f);
    for (int k = 0; k < 5; ++k) addInto(v, staticBurst(0.04f, 2500.f, 8000.f, 3463 + k), 0.3f, 0.2f + 0.25f * float(k));
    grit(v, 0.5f, 2.f, 3469);
    return v;
}
std::vector<float> moveZelian() {  // planet-breakers: meteors screaming down, far impacts, a heavy engine
    const float D = 1.55f;
    std::vector<float> v = heavyEngine(D, 50.f, 450.f, 2.4f, 0.04f, 0.45f);
    for (int k = 0; k < 3; ++k) {
        const float at = 0.05f + 0.35f * float(k);
        Buf w(0.4f);
        Noise nz(3471 + k);
        Band bd;
        for (size_t i = 0; i < w.n(); ++i) {
            const float t = Buf::t(i);
            const float f = glide(2200.f, 300.f, t, 0.38f);
            w.v[i] = bd.bp(nz.next(), f * 0.5f, f * 1.2f) * smooth01(t / 0.25f) * 2.f;
        }
        addInto(v, w.v, 0.4f, at);
        addInto(v, thump(65.f, 28.f, 0.12f, 0.2f), 0.9f, at + 0.4f);
        addInto(v, rattle(0.25f, 30.f, 300.f, 900.f, 0.2f, 3475 + k), 0.4f, at + 0.42f);
    }
    grit(v, 0.6f, 2.2f, 3481);
    return v;
}
// a living nebula: gas swirling through slowly moving resonances, a deep slow breath, a sour glimmer flickering
// like an illusion, charged dust crackling - no engine at all
std::vector<float> moveMortheus2() {
    const float D = 1.7f;
    Buf b(D);
    Noise nz(4001);
    BP s1, s2;
    Ph breath, g1, g2;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if (i % 64 == 0) {  // the gas swirls: two resonances drifting
            s1.set(380.f + 260.f * std::sin(kTwoPi * 0.7f * t), 5.f);
            s2.set(900.f + 500.f * std::sin(kTwoPi * 0.45f * t + 2.f), 6.f);
        }
        const float n = nz.next();
        const float gas = (s1.run(n) + 0.7f * s2.run(n)) * 2.2f;
        const float inhale = 0.5f - 0.5f * std::cos(kTwoPi * t / D * 2.f);  // two slow breaths
        const float deep = lp.lp(breath.sine(36.f) + breath.sine(36.f) * 0.f, 120.f) * inhale;
        const float flick = 0.5f + 0.5f * std::sin(kTwoPi * 7.3f * t) * std::sin(kTwoPi * 2.9f * t);
        const float glimmer = (g1.sine(1244.f) + g2.sine(1318.5f)) * 0.06f * flick;  // a half-step apart: unsettled
        b.v[i] = (gas * (0.4f + 0.6f * inhale) + deep * 0.9f + glimmer) * passEnv(t, D, 0.5f);
    }
    std::vector<float> v = b.v;
    Noise pick(4003);
    for (int k = 0; k < 6; ++k) addInto(v, staticBurst(0.03f, 3000.f, 8000.f, 4005 + k), 0.15f, 0.15f + 0.24f * float(k) + 0.05f * pick.next());
    return v;
}

// The movement sounds in kFactions order (the key is checked against the faction's in the tests: --audiotest)
struct MoveRecipe {
    const char* key;
    std::vector<float> (*make)();
    float loud, hall;
};
const MoveRecipe kMoveRecipes[65] = {
    {"arborec", moveArborec, 0.11f, 0.2f},
    {"ghost", moveCreuss, 0.1f, 0.3f},
    {"hacan", moveHacan, 0.11f, 0.2f},
    {"jolnar", moveJolNar, 0.1f, 0.2f},
    {"l1z1x", moveL1Z1X, 0.11f, 0.2f},
    {"letnev", moveLetnev, 0.11f, 0.25f},
    {"mentak", moveMentak, 0.11f, 0.2f},
    {"muaat", moveMuaat, 0.11f, 0.2f},
    {"naalu", moveNaalu, 0.09f, 0.3f},
    {"nekro", moveNekro, 0.1f, 0.2f},
    {"saar", moveSaar, 0.11f, 0.2f},
    {"sardakk", moveSardakk, 0.11f, 0.2f},
    {"sol", moveSol, 0.11f, 0.2f},
    {"winnu", moveWinnu, 0.1f, 0.3f},
    {"xxcha", moveXxcha, 0.1f, 0.3f},
    {"yin", moveYin, 0.1f, 0.3f},
    {"yssaril", moveYssaril, 0.08f, 0.3f},
    {"argent", moveArgent3, 0.11f, 0.2f},
    {"cabal", moveVuilraith, 0.11f, 0.25f},
    {"empyrean", moveEmpyrean, 0.09f, 0.35f},
    {"mahact", moveMahact, 0.11f, 0.25f},
    {"nomad", moveNomad, 0.1f, 0.2f},
    {"naaz", moveNaazRokha, 0.11f, 0.2f},
    {"titans", moveTitans, 0.12f, 0.25f},
    {"keleres", moveKeleres, 0.11f, 0.25f},
    {"bastion", moveBastion, 0.11f, 0.22f},
    {"deepwrought", moveDeepwrought, 0.1f, 0.35f},
    {"firmament", moveFirmament, 0.09f, 0.3f},
    {"obsidian", moveObsidian, 0.11f, 0.3f},
    {"crimson", moveCrimson, 0.11f, 0.25f},
    {"ralnel", moveRalNel, 0.11f, 0.2f},
    {"augers", moveIlyxum, 0.1f, 0.35f},
    {"axis", moveAxis, 0.11f, 0.22f},
    {"bentor", moveBentor, 0.11f, 0.2f},
    {"celdauri", moveCeldauri, 0.11f, 0.22f},
    {"cheiran", moveCheiran, 0.11f, 0.2f},
    {"cymiae", moveCymiae, 0.1f, 0.2f},
    {"dihmohn", moveDihMohn, 0.11f, 0.22f},
    {"edyn", moveEdyn, 0.1f, 0.35f},
    {"florzen", moveFlorzen, 0.11f, 0.2f},
    {"freesystems", moveFreeSystems, 0.1f, 0.22f},
    {"ghemina", moveGhemina, 0.11f, 0.22f},
    {"ghoti", moveGhoti, 0.1f, 0.35f},
    {"gledge", moveGledge, 0.11f, 0.22f},
    {"khrask", moveLtokk, 0.11f, 0.25f},
    {"kjalengard", moveKjalengard, 0.11f, 0.22f},
    {"kollecc", moveKollecc, 0.09f, 0.25f},
    {"kolume", moveKolume, 0.1f, 0.3f},
    {"kortali", moveKortali, 0.11f, 0.25f},
    {"kyro", moveKyro, 0.1f, 0.22f},
    {"lanefir", moveLanefir, 0.1f, 0.3f},
    {"lizho", moveLizho, 0.1f, 0.25f},
    {"mirveda", moveMirveda, 0.1f, 0.2f},
    {"mortheus", moveMortheus2, 0.1f, 0.35f},
    {"mykomentori", moveMyko, 0.1f, 0.25f},
    {"nivyn", moveNivyn, 0.1f, 0.3f},
    {"nokar", moveNokar, 0.11f, 0.2f},
    {"olradin", moveOlradin, 0.1f, 0.25f},
    {"rohdhna", moveRohdhna, 0.11f, 0.22f},
    {"tnelis", moveTnelis, 0.08f, 0.2f},
    {"vaden", moveVaden, 0.1f, 0.3f},
    {"vaylerian", moveVaylerian, 0.11f, 0.22f},
    {"veldyr", moveVeldyr, 0.1f, 0.25f},
    {"zealots", moveRhodun, 0.1f, 0.25f},
    {"zelian", moveZelian, 0.11f, 0.22f}};
