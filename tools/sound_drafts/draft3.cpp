// Build: g++ -std=c++17 -O2 -Isrc -Itools/sound_drafts tools/sound_drafts/draft3.cpp -o draft3 && ./draft3 OUTDIR
// Sound drafts, round 3 (owner 2026-11-19 11:31): Thunder's Edge and Discordant Stars movement sounds in the grim style
// he kept for every round 2 faction; the siren shortened to fit the attack arrow and tile flash (about 1.1 s at Normal
// speed); the Argent without the screech. Reuses round 2's toolkit (draft2.cpp, without its main).
#define DRAFT_NO_MAIN
#include "draft2.cpp"

namespace audio {
namespace {

// ------------------------------------------------------------------ more helpers
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

// ============================================================== the changed ones
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

// ============================================================== Thunder's Edge
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

// ============================================================== Discordant Stars
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
        s.v[i] = a.sq(140.f * semis(std::fmod(step * 5.f, 7.f) - 3.f), 0.4f) * 0.4f * passEnv(t, D, 0.45f);  // servo steps
    }
    OnePole lp;
    for (float& x : s.v) x = lp.lp(x, 1500.f);
    addInto(v, s.v, 0.3f);
    addInto(v, heavyEngine(D, 50.f, 300.f, 1.6f, 0.03f, 0.45f), 0.6f);
    grit(v, 0.4f, 1.8f, 3349);
    return v;
}
std::vector<float> moveMortheus() {  // illusions: an engine and its false copies, echoing slightly out of tune, flickering
    const float D = 1.45f;
    std::vector<float> one = heavyEngine(D * 0.8f, 66.f, 600.f, 2.f, 0.05f, 0.4f);
    std::vector<float> v(size_t(D * float(R)), 0.f);
    addInto(v, one, 0.8f);
    for (int k = 1; k <= 3; ++k) {  // the copies: later, quieter, shifted by resampling
        const float rate = semis(0.35f * float(k) * (k % 2 ? 1.f : -1.f));
        std::vector<float> c(size_t(float(one.size()) / rate), 0.f);
        for (size_t i = 0; i < c.size(); ++i) {
            const float pos = float(i) * rate;
            const size_t j = size_t(pos);
            c[i] = j + 1 < one.size() ? one[j] + (one[j + 1] - one[j]) * (pos - float(j)) : 0.f;
        }
        for (size_t i = 0; i < c.size(); ++i) c[i] *= 0.5f + 0.5f * std::sin(kTwoPi * (6.f + float(k)) * Buf::t(i));  // flickering
        addInto(v, c, 0.45f / float(k), 0.09f * float(k));
    }
    addInto(v, hiss(D, 3000.f, 7000.f, 0.5f, 0.4f, 3351), 0.1f);
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

}  // namespace
}  // namespace audio

int main(int argc, char** argv) {
    using namespace audio;
    const std::string dir = argc > 1 ? argv[1] : ".";
    struct S { const char* name; std::vector<float> (*make)(); float loud; float hallMix; };
    const S list[] = {
        {"attack_b_siren_short", attackSirenShort, 0.13f, 0.25f}, {"move_argent_v3", moveArgent3, 0.11f, 0.2f},
        {"move_bastion", moveBastion, 0.11f, 0.22f},        {"move_ralnel", moveRalNel, 0.11f, 0.2f},
        {"move_deepwrought", moveDeepwrought, 0.1f, 0.35f}, {"move_crimson", moveCrimson, 0.11f, 0.25f},
        {"move_firmament", moveFirmament, 0.09f, 0.3f},     {"move_obsidian", moveObsidian, 0.11f, 0.3f},
        {"move_axis", moveAxis, 0.11f, 0.22f},              {"move_bentor", moveBentor, 0.11f, 0.2f},
        {"move_kjalengard", moveKjalengard, 0.11f, 0.22f},  {"move_celdauri", moveCeldauri, 0.11f, 0.22f},
        {"move_cheiran", moveCheiran, 0.11f, 0.2f},         {"move_cymiae", moveCymiae, 0.1f, 0.2f},
        {"move_dihmohn", moveDihMohn, 0.11f, 0.22f},        {"move_edyn", moveEdyn, 0.1f, 0.35f},
        {"move_florzen", moveFlorzen, 0.11f, 0.2f},         {"move_freesystems", moveFreeSystems, 0.1f, 0.22f},
        {"move_ghemina", moveGhemina, 0.11f, 0.22f},        {"move_ghoti", moveGhoti, 0.1f, 0.35f},
        {"move_gledge", moveGledge, 0.11f, 0.22f},          {"move_ilyxum", moveIlyxum, 0.1f, 0.35f},
        {"move_kollecc", moveKollecc, 0.09f, 0.25f},        {"move_kolume", moveKolume, 0.1f, 0.3f},
        {"move_kortali", moveKortali, 0.11f, 0.25f},        {"move_kyro", moveKyro, 0.1f, 0.22f},
        {"move_lanefir", moveLanefir, 0.1f, 0.3f},          {"move_lizho", moveLizho, 0.1f, 0.25f},
        {"move_ltokk", moveLtokk, 0.11f, 0.25f},            {"move_mirveda", moveMirveda, 0.1f, 0.2f},
        {"move_mortheus", moveMortheus, 0.1f, 0.3f},        {"move_myko", moveMyko, 0.1f, 0.25f},
        {"move_nivyn", moveNivyn, 0.1f, 0.3f},              {"move_nokar", moveNokar, 0.11f, 0.2f},
        {"move_olradin", moveOlradin, 0.1f, 0.25f},         {"move_rohdhna", moveRohdhna, 0.11f, 0.22f},
        {"move_tnelis", moveTnelis, 0.08f, 0.2f},           {"move_vaden", moveVaden, 0.1f, 0.3f},
        {"move_vaylerian", moveVaylerian, 0.11f, 0.22f},    {"move_veldyr", moveVeldyr, 0.1f, 0.25f},
        {"move_rhodun", moveRhodun, 0.1f, 0.25f},           {"move_zelian", moveZelian, 0.11f, 0.22f},
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
