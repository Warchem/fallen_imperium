// The Fracture appears (0.73.5, owner 2026-11-19: "a cosmic eldritch screech noise, as if the fabric of spacetime just ripped
// apart") - included by synth.cpp inside its namespace, after grim.inc / grim2.inc (their toolkit). Same maths rule as
// synth.cpp: only sin / cos / floor / frexp / ldexp.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

// the tear itself: noise ripped in rapid uneven shreds, its band falling from a hiss to a growl, like cloth tearing - but huge
std::vector<float> spaceRip(float dur, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed), pick(seed + 1);
    Band bd;
    OnePole env;
    float g = 1.f, until = 0.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), u = t / dur;
        if (t >= until) {  // the shreds: short bursts, ever faster then slowing
            g = 0.25f + 0.75f * uni(pick);
            until = t + 0.004f + 0.022f * uni(pick) * (0.4f + u);
        }
        const float lo = 2600.f * fexp(-u * 2.6f) + 60.f, hi = 9000.f * fexp(-u * 1.9f) + 400.f;
        const float shape = smooth01(u * 12.f) * (1.f - smooth01((u - 0.55f) / 0.45f));
        b.v[i] = bd.bp(nz.next(), lo, hi) * env.lp(g, 300.f) * shape * 3.f;
    }
    return b.v;
}

// the screech: inhuman voices sliding down - detuned saws through moving formants, a tritone apart, ring-modulated by a
// low tone so they sound like metal and throat at once; a fast wobble on them
std::vector<float> eldritchScreech(float dur, float f0, float f1, uint32_t seed) {
    Buf b(dur);
    Ph a1, a2, a3, b1, b2, rm, vib;
    BP fa, fb, fc;
    Noise nz(seed);
    OnePole wander;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), u = t / dur;
        const float w = 1.f + 0.035f * vib.sine(7.3f + 3.f * u) + 0.02f * wander.lp(nz.next(), 4.f) * 30.f;
        const float f = glide(f0, f1, t, dur * 0.85f) * w;
        const float tri = f * 1.4142f * (1.f + 0.006f * std::sin(kTwoPi * 0.7f * t));  // a tritone up, beating
        float x = a1.saw(f) + a2.saw(f * 1.011f) + 0.6f * a3.saw(f * 0.5f);
        x += 0.7f * (b1.saw(tri) + b2.saw(tri * 0.993f));
        x *= 0.6f + 0.4f * rm.sine(63.f + 40.f * u);  // the ring: metal and throat
        if (i % 64 == 0) {  // the formants drift, like a mouth opening
            fa.set(900.f + 500.f * std::sin(kTwoPi * 0.9f * t), 5.f);
            fb.set(2300.f + 600.f * std::sin(kTwoPi * 0.55f * t + 1.f), 7.f);
            fc.set(3400.f - 900.f * u, 9.f);
        }
        const float y = fa.run(x) + 0.8f * fb.run(x) + 0.5f * fc.run(x);
        b.v[i] = soft(y * 1.6f) * passEnv(t, dur, 0.18f);
    }
    return b.v;
}

// THE FRACTURE: the void breathing in, the tear, the screech, the blow far below, a dark choir and the rumble after
std::vector<float> fractureTear() {
    std::vector<float> v = silence(4.6f);
    // the breath in: a hiss and a tone rising into the moment of the tear
    {
        Buf b(0.55f);
        Noise nz(8101);
        Band bd;
        Ph p;
        for (size_t i = 0; i < b.n(); ++i) {
            const float t = Buf::t(i), u = t / 0.55f;
            const float e = u * u * u;
            b.v[i] = (bd.bp(nz.next(), 800.f + 3000.f * u, 3000.f + 6000.f * u) * 1.5f + 0.4f * p.sine(glide(180.f, 900.f, t, 0.55f))) * e;
        }
        addInto(v, b.v, 0.5f, 0.f);
    }
    const float at = 0.5f;  // the moment it tears
    addInto(v, spaceRip(1.4f, 8111), 0.9f, at);
    addInto(v, eldritchScreech(2.7f, 1650.f, 620.f, 8121), 0.55f, at + 0.05f);
    addInto(v, eldritchScreech(2.2f, 2300.f, 1100.f, 8131), 0.28f, at + 0.25f);  // a second voice, higher, a moment later
    // the blow: deep, far below, and the crack of it
    addInto(v, thump(85.f, 24.f, 0.25f, 0.9f), 1.5f, at);
    {
        const float res[4] = {120.f, 233.f, 410.f, 977.f};
        addInto(v, clank(res, 4, 6.f, 0.25f, 8141), 0.6f, at);
    }
    addInto(v, rumble(3.6f, 110.f, 0.12f, 8151), 0.9f, at);
    // a choir out of the void: a cluster a half-step apart, dark, swelling under the screech and fading
    {
        const float ch[4] = {110.f, 116.54f, 155.56f, 164.81f};
        addInto(v, chant(3.2f, ch, 4, 420.f, 860.f, 0.012f, 0.35f), 0.42f, at + 0.4f);
    }
    // static pouring through the tear
    addInto(v, staticBurst(1.6f, 1200.f, 7000.f, 8161), 0.18f, at + 0.1f);
    grit(v, 0.35f, 1.8f, 8171);
    return v;
}

// ------------------------------------------------------------------ 0.73.8: the Firmament becomes the Obsidian
// A TRILLION VOICES: countless throats screaming across the galaxy - voices joining all the time, each a raw buzzing source
// through its own open-mouthed formants, shaking and climbing in pitch, a roar of breath under them; louder and louder for
// kGalaxyScreamsMs, then cut off dead.
std::vector<float> galaxyScreams() {
    const float dur = float(kGalaxyScreamsMs) / 1000.f;
    Buf b(dur);
    Noise pick(9101), nz(9103);
    struct Voice {
        float at, f, rise, vib, vibD, gain;
        Ph ph, vp;
        BP a, c, d;
    };
    const int kVoices = 56, kNear = 7;  // (the last kNear are near screams, loud and clear over the mass)
    std::vector<Voice> vs(static_cast<size_t>(kVoices));
    for (size_t vi = 0; vi < vs.size(); ++vi) {
        Voice& v = vs[vi];
        const bool nearScream = int(vi) >= kVoices - kNear;
        const float u = uni(pick);
        v.at = dur * 0.9f * (1.f - (1.f - u) * (1.f - u) * (1.f - u)) * 0.85f;  // most join early, more keep coming
        const float kind = uni(pick);
        v.f = kind < 0.2f ? 140.f + 120.f * uni(pick) : 320.f + 820.f * uni(pick);  // a few deep howls, mostly shrieks
        v.rise = semis(3.f + 9.f * uni(pick));
        v.vib = 5.f + 5.f * uni(pick);
        v.vibD = 0.012f + 0.035f * uni(pick);
        v.gain = 0.5f + 0.5f * uni(pick);
        if (nearScream) {  // a scream close by: high, wailing up, shaking hard
            v.at = dur * (0.15f + 0.7f * uni(pick));
            v.f = 620.f + 520.f * uni(pick);
            v.rise = semis(5.f + 5.f * uni(pick));
            v.vib = 6.5f + 3.f * uni(pick);
            v.vibD = 0.04f + 0.03f * uni(pick);
            v.gain = 2.4f;
        }
        v.a.set(650.f + 450.f * uni(pick), 5.f);   // "aah" - the jaw open
        v.c.set(1050.f + 600.f * uni(pick), 7.f);
        v.d.set(2400.f + 900.f * uni(pick), 9.f);
    }
    Band breath;
    OnePole tone;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        float x = 0.f;
        for (Voice& v : vs) {
            if (t < v.at) continue;
            const float age = t - v.at;
            const float e = smooth01(age / 0.18f) * v.gain;
            const float f = v.f * glide(1.f, v.rise, age, dur - v.at + 0.01f) * (1.f + v.vibD * v.vp.sine(v.vib));
            const float src = v.ph.saw(f) + 0.15f * nz.next();  // a torn, breathy throat
            x += (v.a.run(src) + 0.75f * v.c.run(src) + 0.45f * v.d.run(src)) * e;
        }
        const float u = t / dur;
        const float swell = 0.10f + 0.90f * u * u * (0.5f + 0.5f * u);  // the crescendo
        const float air = breath.bp(nz.next(), 900.f, 6500.f) * (0.5f + 0.8f * u);
        b.v[i] = (soft(x * 0.22f) + air * 0.25f + tone.lp(x, 400.f) * 0.15f) * swell;
    }
    // cut off dead: 10 ms
    const size_t cut = size_t(0.010f * float(R));
    for (size_t k = 0; k < cut && k < b.n(); ++k) b.v[b.n() - 1 - k] *= float(k) / float(cut);
    return b.v;
}

// THE OBSIDIAN'S TEAR: the Fracture's tear a fourth lower and ruined - driven to clipping, crushed, buzzing
std::vector<float> obsidianTear() {
    const std::vector<float> v = fractureTear();
    const float k = 1.335f;  // a fourth down: slower and deeper
    std::vector<float> w(size_t(float(v.size()) * k));
    for (size_t i = 0; i < w.size(); ++i) {
        const float sp = float(i) / k;
        const size_t a = size_t(sp);
        const float f = sp - float(a);
        w[i] = a + 1 < v.size() ? v[a] * (1.f - f) + v[a + 1] * f : 0.f;
    }
    float pk = 0.f;
    for (float x : w) pk = std::max(pk, std::fabs(x));
    Ph ring;
    for (size_t i = 0; i < w.size(); ++i) {
        float x = pk > 0.f ? w[i] / pk : 0.f;
        x = soft(x * 9.f);                                 // driven into the wall
        x = x > 0.55f ? 0.55f : x < -0.55f ? -0.55f : x;  // and clipped hard
        w[i] = x * (0.7f + 0.3f * ring.sine(47.f));        // a buzz in it
    }
    crush(w, 5, 9.f, 0.6f);
    grit(w, 0.7f, 3.2f, 9111);
    // (the long rumble of the slowed tear fades out a little sooner: 4 s .. 5.5 s)
    const size_t f0 = size_t(4.0f * float(R)), f1 = size_t(5.5f * float(R));
    if (w.size() > f1) w.resize(f1);
    for (size_t i = f0; i < w.size(); ++i) w[i] *= 1.f - float(i - f0) / float(f1 - f0);
    return w;
}

// mastering with no room and no hall (the screams must stop dead): no DC, a squash of the peaks, levelled like grim()
std::vector<int16_t> dryMaster(std::vector<float> v, float loud) {
    noDC(v);
    float m0 = 0.f;
    for (float x : v) m0 = std::max(m0, std::fabs(x));
    if (m0 > 0) for (float& x : v) x = soft(x / m0 * 1.6f);
    double sq = 0;
    float m = 0;
    for (float x : v) {
        sq += double(x) * double(x);
        m = std::max(m, std::fabs(x));
    }
    const float rms = float(std::sqrt(sq / double(std::max<size_t>(1, v.size()))));
    float g = rms > 0 ? loud / rms : 0.f;
    if (m * g > 0.9f) g = 0.9f / m;
    for (float& x : v) x *= g;
    return finish(std::move(v), std::min(0.9f, m * g));
}

#pragma GCC diagnostic pop
