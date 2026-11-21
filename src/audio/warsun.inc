// The War Sun scene's sounds (0.73.0, owner 2026-11-19: "Add sound effects of the two main spaceships moving, followed by a
// thundering wardrum-like drone that builds to a crescendo as the warsun pans into scene from the camera") - included by
// synth.cpp inside its namespace, after grim.inc / grim2.inc (their toolkit). Same maths rule as synth.cpp: only sin / cos /
// floor / frexp / ldexp.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

// A ship that starts beside the camera (it came from behind it) and flies away: loud at once, then falling off with the
// distance - its highs first - while its pitch sags (the Doppler shift of something going away).
//   roarLo/roarHi: the band of its jet roar at the start (it slides down an octave and more), whine: the turbine's note,
//   hum: the engine's low note, throb: a pulse in the engine (0: none), heavy 0..1: more hull and rumble, less whine.
std::vector<float> shipAway(float dur, float roarLo, float roarHi, float whine, float hum, float throb, float heavy, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed), wob(seed + 1);
    Band roar;
    OnePole lp1, lp2, wl, hl, wander;
    Ph w1, w2, w3, h1, h2, h3;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        // how far it is: right beside us for a moment, then going away fast and slowing (as seen from here)
        const float dist = 0.35f + t * 1.9f + t * t * 0.25f;
        const float near = 1.f / (dist * dist * 0.8f + 0.2f);
        const float attack = smooth01(t / (0.10f + heavy * 0.08f));
        const float tail = t > dur - 0.6f ? clamp01((dur - t) / 0.6f) : 1.f;
        const float amp = attack * tail * std::min(1.4f, near);
        const float dop = 1.f / (1.f + 0.22f * smooth01(t / 0.7f));  // the pitch sags as it goes away
        const float cut = 9000.f / (1.f + t * 3.2f);                   // the air takes the highs
        // the jet roar: a band of noise sliding down
        const float lo = roarLo * dop / (1.f + t * 0.8f), hi = roarHi * dop / (1.f + t * 1.2f);
        float x = roar.bp(nz.next(), lo, std::max(lo * 1.6f, hi)) * 2.2f;
        // the turbine whine: two notes beating, a small wobble
        const float wv = 1.f + 0.006f * wander.lp(wob.next(), 3.f) * 20.f;
        const float wf = whine * dop * wv;
        x += (w1.saw(wf) * 0.5f + w2.saw(wf * 1.012f) * 0.4f + w3.sine(wf * 2.f) * 0.25f) * (1.f - heavy * 0.6f) * 0.45f;
        // the engine's hum (and its throb)
        const float pulse = throb > 0.f ? 0.55f + 0.45f * std::sin(kTwoPi * throb * t) : 1.f;
        const float hf = hum * (0.85f + 0.15f * dop);
        float hm = soft((h1.saw(hf) + 0.8f * h2.sq(hf * 0.5f) + 0.5f * h3.saw(hf * 1.007f)) * (1.6f + heavy * 1.4f));
        hm = hl.lp(hm, 260.f + 500.f * dop) * pulse * (0.7f + heavy * 0.6f);
        x += hm;
        b.v[i] = lp2.lp(lp1.lp(x, cut), cut * 1.3f) * amp;
    }
    std::vector<float> v = b.v;
    // the rush of air as it passes the camera
    addInto(v, hiss(0.5f, 400.f, 5200.f, 0.04f, 0.16f, seed + 7), 0.9f - heavy * 0.3f, 0.f);
    return v;
}

// SHIP A: the first ship - a fast courier: a jet roar and a bright turbine whine falling away into the distance
std::vector<float> shipPassA() {
    std::vector<float> v = shipAway(3.4f, 500.f, 4200.f, 1250.f, 72.f, 0.f, 0.f, 7301);
    grit(v, 0.25f, 1.5f, 7303);
    return v;
}

// SHIP B: the second, heavier ship from the other side: a deep throbbing engine, a lower whine, its hull groaning
std::vector<float> shipPassB() {
    std::vector<float> v = shipAway(3.8f, 260.f, 2600.f, 610.f, 44.f, 6.5f, 1.f, 7311);
    const float res[3] = {140.f, 233.f, 377.f};
    std::vector<float> g = groan(1.8f, 31.f, res, 3, 9.f, 0.2f, 7313);
    addInto(v, g, 0.35f, 0.05f);
    grit(v, 0.4f, 1.8f, 7315);
    return v;
}

// THE WAR SUN: war drums over a dark drone (A minor), slow at first, then faster and louder - the choir and the brass
// coming in - to one huge blow at kWarSunDrumsPeakMs (the camera stops on the war sun), and a long dark tail.
std::vector<float> warSunDrums() {
    const float peak = float(kWarSunDrumsPeakMs) / 1000.f;  // 8 s
    const float dur = peak + 3.6f;
    std::vector<float> v = silence(dur);
    // the drone: A1 E2 A2 C3, opening up (cutoff) and swelling toward the blow, then sinking away
    {
        Buf b(dur);
        Ph p[10];
        Ph sub;
        OnePole lp, lp2;
        const float f[5] = {55.f, 82.41f, 110.f, 130.81f, 164.81f};
        for (size_t i = 0; i < b.n(); ++i) {
            const float t = Buf::t(i);
            const float up = clamp01(t / peak);
            float e, cut;
            if (t < peak) {
                e = 0.05f + 0.95f * up * up * (0.6f + 0.4f * up);  // from a murmur to full
                cut = 90.f * fexp(flog(1500.f / 90.f) * up * up);
            } else {
                const float k = t - peak;
                e = 1.f * fexp(-k / 1.4f) * (t > dur - 0.4f ? clamp01((dur - t) / 0.4f) : 1.f);
                cut = 120.f + 1380.f * fexp(-k / 0.6f);
            }
            float x = 0.f;
            for (int k = 0; k < 5; ++k) {
                const float w = k == 4 ? smooth01((up - 0.5f) * 2.f) : 1.f;  // the fifth on top comes in late
                x += (p[k * 2].saw(f[k] * 0.996f) + p[k * 2 + 1].saw(f[k] * 1.004f)) * w;
            }
            const float s = sub.sine(27.5f) * 1.6f * (0.3f + 0.7f * up);
            b.v[i] = (lp2.lp(lp.lp(soft(x * 0.5f), cut), cut * 1.4f) + s) * e;
        }
        addInto(v, b.v, 0.55f);
    }
    // the rumble under it all, rising
    {
        std::vector<float> r = rumble(peak + 0.4f, 140.f, 0.97f, 7321);
        addInto(v, r, 0.55f);
    }
    // the drums: steady, then twice as fast, then four times, then a roll into the blow - louder all the way
    {
        std::vector<float> hits;
        for (float t = 0.25f; t < 4.0f; t += 1.0f) hits.push_back(t);
        for (float t = 4.0f; t < 6.0f; t += 0.5f) hits.push_back(t);
        for (float t = 6.0f; t < 7.0f; t += 0.25f) hits.push_back(t);
        for (float t = 7.0f; t < peak - 0.05f; t += 0.0625f) hits.push_back(t);
        int k = 0;
        for (float at : hits) {
            const float up = at / peak;
            const float vel = 0.10f + 0.90f * up * up * (0.6f + 0.4f * up);  // quiet and far at first
            const bool down = std::fabs(at - std::floor(at + 0.01f)) < 0.02f;  // on the second: the big drum
            if (down || at < 4.f) {
                addInto(v, thump(118.f, 42.f, 0.07f, 0.42f), vel * 1.1f, at);  // a taiko's deep body
                addInto(v, drum(52.f, 0.3f, 7331 + k), vel * 0.8f, at);
            } else {
                addInto(v, drum(at >= 7.f ? 70.f : 84.f, at >= 7.f ? 0.1f : 0.16f, 7331 + k), vel * (at >= 7.f ? 0.55f : 0.75f), at);
            }
            ++k;
        }
    }
    // a low choir rising out of the drone in the second half
    {
        const float ch[3] = {110.f, 130.81f, 164.81f};
        addInto(v, chant(peak - 3.2f + 0.3f, ch, 3, 520.f, 980.f, 0.007f, 0.92f), 0.38f, 3.2f);
    }
    // the brass swelling in the last two seconds...
    {
        const float b1[3] = {55.f, 82.41f, 110.f};
        addInto(v, brass(2.2f, b1, 3, 1400.f, 1.9f, 0.08f), 0.55f, peak - 2.1f);
    }
    // ...and the blow: the biggest drum, a burst, a brass chord ringing out, a shimmer
    {
        addInto(v, thump(95.f, 30.f, 0.12f, 0.9f), 1.6f, peak);
        addInto(v, drum(40.f, 0.6f, 7351), 1.3f, peak);
        addInto(v, hiss(1.6f, 60.f, 1400.f, 0.004f, 0.45f, 7353), 0.9f, peak);
        addInto(v, hiss(2.4f, 3500.f, 9000.f, 0.02f, 0.9f, 7355), 0.18f, peak);
        const float c[4] = {55.f, 82.41f, 110.f, 130.81f};
        addInto(v, brass(3.0f, c, 4, 2200.f, 0.03f, 2.4f), 0.8f, peak);
        addInto(v, toll(55.f, 1.6f, 7357), 0.7f, peak);
    }
    grit(v, 0.3f, 1.6f, 7359);
    return v;
}

#pragma GCC diagnostic pop
