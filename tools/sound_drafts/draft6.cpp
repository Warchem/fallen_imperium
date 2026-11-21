// Build: g++ -std=c++17 -O2 -Isrc -Itools/sound_drafts tools/sound_drafts/draft6.cpp -o draft6 && ./draft6 OUTDIR
// Sound drafts, round 6 (owner 2026-11-19 13:30, his marks on round 5): 68 kept. Redone from his notes:
//   your turn / a new round ("doesn't sound like something that would play ... in a space board game") -> a starship
//   console's chime and a space-opera stinger; technology researched ("too long") -> short; hero unleashed ("Do a warning
//   sound"); objective scored ("the bump in the middle makes the sound seem off") -> the first half, held; accept / counter
//   ("match the space opera aesthetic") -> holographic bells; red alert ("more ominous").
//   Voices: Arborec (grassy rustling), Last Bastion (feminine - the figurehead is a woman), Cheiran (no bubbles: clawing,
//   chittering), Cymiae (aggressive, cybernetic, simian), Ghoti (no bubbles: ominous whales, gnashing teeth), Gledge (more
//   robotic), Kyro (the bubbles blended into the rasp), L1Z1X (angrier), Mahact (deep, commanding, talking down), Myko (no
//   bubbles), Nekro (modulated yelling fighting the corrupted noise), Obsidian (eldritch, possessed), Tnelis (lower),
//   Vuil'raith (a deep lovecraftian growl), Zelian (rocks crushing into each other).
//   Dropped at his word: defeat ("keep this off for now"), exploring ("no need for a sound").
#define DRAFT_NO_MAIN
#include "draft5.cpp"

namespace audio {
namespace {

// a synth bell (FM): a carrier and an inharmonic modulator whose brightness dies away - a starship console's chime
std::vector<float> fmBell(float f, float ratio, float index, float tau, float dur) {
    dur = std::max(dur, tau * 5.f);  // rings out fully (no cut-off tail)
    Buf b(dur);
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), e = env(t, 0.004f, tau);
        const float m = std::sin(kTwoPi * f * ratio * t) * index * fexp(-t / (tau * 0.6f));
        b.v[i] = std::sin(kTwoPi * f * t + m) * e * clamp01((dur - t) / 0.03f);
    }
    return b.v;
}
// a riser: noise and a saw climbing through an opening filter
std::vector<float> riser(float dur, float f0, float f1, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed);
    Ph a;
    OnePole lp, lp2;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), u = t / dur;
        const float f = glide(f0, f1, t, dur);
        const float x = a.saw(f) * 0.5f + nz.next() * 0.6f;
        b.v[i] = lp2.lp(lp.lp(x, 200.f + 4000.f * u * u), 300.f + 5000.f * u * u) * u * u;
    }
    return b.v;
}
// a cinematic "braam": a low brass cluster hit hard, saturated, dying away
std::vector<float> braam(float dur, const float* f, int n, uint32_t seed) {
    std::vector<float> v = brass(dur, f, n, 1600.f, 0.015f, dur * 0.85f);
    for (float& x : v) x = soft(x * 3.f);
    addInto(v, thump(70.f, 30.f, 0.12f, 0.3f), 1.f);
    addInto(v, rumble(dur, 120.f, 0.1f, seed), 0.4f);
    return v;
}
// leaves and grass rustling: soft high noise in gusts, with little ticks of stems
std::vector<float> rustle(float dur, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed), g(seed + 1);
    Band bd;
    OnePole gust;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float amt = clamp01(0.4f + gust.lp(g.next(), 3.f) * 40.f);
        b.v[i] = bd.bp(nz.next(), 1800.f, 6000.f) * amt * passEnv(t, dur, 0.5f) * 1.6f;
    }
    std::vector<float> v = b.v;
    addInto(v, rattle(dur, 40.f, 2500.f, 5000.f, 0.5f, seed + 7), 0.25f);
    return v;
}
// claws: a scrape across shell or stone - noise juddering through a narrow band that slides
std::vector<float> scrape(float dur, float f, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed);
    BP bp;
    Ph jud;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if (i % 32 == 0) bp.set(f * (1.f + 0.3f * t / dur), 6.f);
        const float j = jud.sine(38.f) > 0.f ? 1.f : 0.25f;
        b.v[i] = bp.run(nz.next()) * j * passEnv(t, dur, 0.3f) * 2.5f;
    }
    return b.v;
}
// chittering: short trains of quick clicks, like mandibles or mouthparts
std::vector<float> chitter(float dur, float rate, uint32_t seed) {
    std::vector<float> v(size_t(dur * float(R)), 0.f);
    Noise pick(seed);
    for (int k = 0; k < int(dur * rate); ++k) {
        float at = uni(pick) * dur * 0.9f;
        const int n = 4 + int(uni(pick) * 6.f);
        const float f = 2200.f + 1800.f * uni(pick);
        for (int c = 0; c < n; ++c, at += 0.016f + 0.008f * uni(pick)) {
            const float res[2] = {f, f * 1.47f};
            addInto(v, clank(res, 2, 14.f, 0.006f, seed + 31 * uint32_t(k) + uint32_t(c)), 0.35f, at);
        }
    }
    return v;
}
// teeth gnashing: grinding clicks in quick bursts, low and hard
std::vector<float> gnash(float dur, uint32_t seed) {
    std::vector<float> v(size_t(dur * float(R)), 0.f);
    Noise pick(seed);
    for (int k = 0; k < int(dur * 2.5f); ++k) {
        float at = uni(pick) * dur * 0.85f;
        for (int c = 0; c < 5; ++c, at += 0.03f + 0.02f * uni(pick)) {
            const float res[3] = {700.f + 300.f * uni(pick), 1500.f, 2600.f};
            addInto(v, clank(res, 3, 9.f, 0.012f, seed + 17 * uint32_t(k) + uint32_t(c)), 0.45f, at);
        }
    }
    return v;
}
// rocks grinding and crushing into each other
std::vector<float> rockGrind(float dur, uint32_t seed) {
    const float res[4] = {160.f, 330.f, 610.f, 1150.f};
    std::vector<float> v = groan(dur, 14.f, res, 4, 10.f, 0.5f, seed);
    addInto(v, rattle(dur, 28.f, 250.f, 900.f, 0.5f, seed + 3), 0.6f);
    addInto(v, rumble(dur, 180.f, 0.5f, seed + 5), 0.5f);
    Noise pick(seed + 9);
    for (int k = 0; k < 3; ++k) {  // a few crunches
        const float at = 0.2f + uni(pick) * (dur - 0.5f);
        const float r2[3] = {220.f, 540.f, 1300.f};
        addInto(v, clank(r2, 3, 6.f, 0.05f, seed + 11 + uint32_t(k)), 0.6f, at);
        addInto(v, hiss(0.15f, 400.f, 2500.f, 0.002f, 0.05f, seed + 21 + uint32_t(k)), 0.4f, at);
    }
    return v;
}

// ================================================================== the moments, redone
// YOUR TURN: a starship console's chime - a soft rush in, two bell tones a fifth apart, a low pulse under them, a glint
std::vector<float> turnYours2() {
    std::vector<float> v = hiss(0.22f, 900.f, 5000.f, 0.15f, 0.05f, 6001);
    gain(v, 0.35f);
    addInto(v, fmBell(293.7f, 1.41f, 2.2f, 0.16f, 0.6f), 0.6f, 0.14f);
    addInto(v, fmBell(440.f, 1.41f, 2.f, 0.24f, 0.9f), 0.65f, 0.36f);
    addInto(v, fmBell(880.f, 2.76f, 1.f, 0.2f, 0.5f), 0.12f, 0.38f);  // the glint
    Buf s(0.8f);
    Ph a;
    for (size_t i = 0; i < s.n(); ++i) s.v[i] = a.sine(73.4f) * passEnv(Buf::t(i), 0.8f, 0.3f);
    addInto(v, s.v, 0.6f, 0.12f);
    grit(v, 0.15f, 1.3f, 6003);
    return v;
}
// A NEW ROUND: a riser into a deep braam, then a slow minor arpeggio of console bells over its tail, a starry shimmer
std::vector<float> roundNew2() {
    std::vector<float> v = riser(0.75f, 60.f, 240.f, 6011);
    gain(v, 0.5f);
    const float f[3] = {65.4f, 98.f, 77.8f};
    addInto(v, braam(1.6f, f, 3, 6013), 0.8f, 0.72f);
    const float arp[3] = {261.6f, 311.1f, 392.f};
    for (int k = 0; k < 3; ++k) addInto(v, fmBell(arp[k], 1.41f, 1.6f, 0.35f, 0.9f), 0.35f, 1.05f + 0.22f * float(k));
    Noise pick(6015);
    for (int k = 0; k < 6; ++k) addInto(v, fmBell(1500.f + 1500.f * uni(pick), 2.76f, 0.6f, 0.1f, 0.3f), 0.05f, 1.2f + 0.15f * float(k));
    grit(v, 0.25f, 1.5f, 6017);
    return v;
}
// A TECHNOLOGY RESEARCHED, short: four quick data blips climbing, a power-up click
std::vector<float> techResearch2() {
    std::vector<float> v = silence(0.7f);
    const float f[4] = {220.f, 261.6f, 329.6f, 440.f};
    for (int k = 0; k < 4; ++k) addInto(v, fmBell(f[k], 2.f, 1.2f, 0.06f, 0.18f), 0.4f, 0.06f * float(k));
    const float res[2] = {1800.f, 3100.f};
    addInto(v, clank(res, 2, 8.f, 0.008f, 6021), 0.4f, 0.28f);
    Buf h(0.4f);
    Ph a;
    for (size_t i = 0; i < h.n(); ++i) h.v[i] = a.sine(110.f) * env(Buf::t(i), 0.02f, 0.12f);
    addInto(v, h.v, 0.5f, 0.28f);
    grit(v, 0.15f, 1.3f, 6023);
    return v;
}
// AN OBJECTIVE SCORED: the first half you liked - the bell and the low minor choir - held and fading, no second entry
std::vector<float> objectivePublic2() {
    std::vector<float> v = toll(65.4f, 0.8f, 5061);
    v.resize(size_t(2.2f * float(R)), 0.f);
    const float m[3] = {130.8f, 155.6f, 196.f};
    std::vector<float> c = chant(1.9f, m, 3, 520.f, 1050.f, 0.008f, 0.3f);
    addInto(v, c, 0.6f, 0.1f);
    grit(v, 0.3f, 1.6f, 5065);
    return v;
}
// A HERO UNLEASHED - a warning: a deep dissonant horn pulsing three times, an electronic whoop over each, a sub falling
std::vector<float> heroWarning() {
    std::vector<float> v = silence(2.0f);
    const float f[3] = {73.4f, 77.8f, 110.f};
    for (int k = 0; k < 3; ++k) {
        std::vector<float> h = brass(0.36f, f, 3, 1800.f, 0.02f, 0.1f);
        for (float& x : h) x = soft(x * 2.6f);
        addInto(v, h, 0.75f, 0.05f + 0.48f * float(k));
        Buf w(0.3f);
        Ph a;
        for (size_t i = 0; i < w.n(); ++i) {
            const float t = Buf::t(i);
            w.v[i] = a.sq(glide(500.f, 1100.f, t, 0.28f), 0.5f) * 0.25f * passEnv(t, 0.3f, 0.6f);
        }
        addInto(v, w.v, 0.35f, 0.08f + 0.48f * float(k));
    }
    Buf s(1.6f);
    Ph a;
    for (size_t i = 0; i < s.n(); ++i) {
        const float t = Buf::t(i);
        s.v[i] = a.sine(glide(55.f, 36.f, t, 1.5f)) * passEnv(t, 1.6f, 0.2f);
    }
    addInto(v, s.v, 0.6f, 0.1f);
    grit(v, 0.35f, 1.7f, 6031);
    return v;
}
// ACCEPTED (codec), space opera: a soft rush, two bell tones rising a fifth, a warm low fifth swelling under them
std::vector<float> codecAccept2() {
    std::vector<float> v = hiss(0.18f, 900.f, 4500.f, 0.12f, 0.05f, 6041);
    gain(v, 0.3f);
    addInto(v, fmBell(329.6f, 1.41f, 1.8f, 0.16f, 0.6f), 0.55f, 0.1f);
    addInto(v, fmBell(493.9f, 1.41f, 1.8f, 0.26f, 0.9f), 0.6f, 0.3f);
    const float p[2] = {82.4f, 123.5f};
    addInto(v, brass(0.8f, p, 2, 700.f, 0.2f, 0.4f), 0.45f, 0.15f);
    radio(v, 250.f, 4200.f, 0.012f, 6043);
    return v;
}
// COUNTER-OFFER, space opera: two bell tones a half-step apart, the second bending up - a question left hanging
std::vector<float> codecCounter2() {
    std::vector<float> v = fmBell(329.6f, 1.41f, 1.8f, 0.22f, 0.5f);
    Buf b(0.7f);
    float ph = 0.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), e = env(t, 0.004f, 0.3f);
        const float f = 349.2f * (1.f + 0.03f * smooth01((t - 0.2f) / 0.3f));
        ph += kTwoPi * f / float(R);
        b.v[i] = std::sin(ph + 1.6f * std::sin(kTwoPi * f * 1.41f * t) * fexp(-t / 0.18f)) * e;
    }
    addInto(v, b.v, 1.f, 0.24f);
    flicker(v, 0.25f, 6.f);  // the hologram's flicker
    radio(v, 250.f, 4200.f, 0.012f, 6051);
    return v;
}
// THE RED ALERT, more ominous: a deep impact, a slow low wailing alarm, dissonant and distorted, a drone underneath
std::vector<float> codecAlert2() {
    std::vector<float> v = thump(55.f, 24.f, 0.12f, 0.35f);
    v.resize(size_t(2.1f * float(R)), 0.f);
    Buf b(1.9f);
    Ph a, c, d;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = 110.f * (1.f + 0.08f * std::sin(kTwoPi * 0.9f * t - 1.5f));  // a slow wail
        const float x = soft((a.saw(f) + c.saw(f * semis(1.f)) + 0.7f * d.saw(f * 0.5f * semis(6.f))) * 2.2f);
        b.v[i] = lp.lp(x, 900.f) * passEnv(t, 1.9f, 0.25f);
    }
    addInto(v, b.v, 0.7f, 0.05f);
    const float f[2] = {41.2f, 43.7f};
    addInto(v, drone(1.9f, f, 2, 200.f, 0.003f, 0.4f), 0.6f, 0.1f);
    addInto(v, staticBurst(0.4f, 400.f, 3500.f, 6061), 0.4f, 0.9f);
    radio(v, 120.f, 3400.f, 0.03f, 6063);
    return v;
}

// ================================================================== the voices, redone
std::vector<float> factionVoice2(const std::string& id) {
    const uint32_t sd = 7000u + uint32_t(std::hash<std::string>{}(id) % 9000u);
    std::vector<float> v;
    float statics = 0.025f;
    if (id == "arborec") {  // + grass and leaves rustling
        VSpec s = vs(82.f, 0.22f, 3.6f, 0.82f, sd);
        s.sway = 0.08f, s.breath = 0.2f;
        v = voice(s);
        overlayCreak(v, 0.2f, sd + 1);
        addInto(v, rustle(s.dur, sd + 3), 0.55f);
    } else if (id == "bastion") {  // a woman's voice: the figurehead
        VSpec s = vs(205.f, 0.22f, 5.2f, 1.17f, sd);
        s.breath = 0.22f, s.rough = 0.05f;
        v = voice(s);
        statics = 0.08f;
    } else if (id == "cheiran") {  // no bubbles: claws scraping, mandibles chittering
        VSpec s = vs(84.f, 0.18f, 4.6f, 0.85f, sd);
        s.rough = 0.35f;
        v = voice(s);
        addInto(v, chitter(s.dur, 2.2f, sd + 1), 0.8f);
        Noise pick(sd + 2);
        for (int k = 0; k < 3; ++k) addInto(v, scrape(0.25f, 900.f + 600.f * uni(pick), sd + 5 + uint32_t(k)), 0.45f, 0.2f + 0.7f * float(k));
    } else if (id == "cymiae") {  // aggressive, cybernetic, simian: hoots and barks with a machine edge
        VSpec s = vs(150.f, 0.5f, 6.6f, 1.02f, sd);
        s.rough = 0.6f, s.breath = 0.3f, s.chorus = 2, s.detune = 0.03f, s.spread = 0.01f, s.cons = 0.7f;
        v = voice(s);
        Ph ring;
        for (float& x : v) x = soft(x * 3.f) * (0.65f + 0.35f * ring.sine(48.f));
        crush(v, 4, 16.f, 0.4f);
    } else if (id == "ghoti") {  // no bubbles: ominous whales and gnashing teeth
        VSpec s = vs(70.f, 0.3f, 3.2f, 0.8f, sd);
        s.vib = 0.02f, s.vibHz = 3.f, s.rough = 0.2f;
        v = voice(s);
        addInto(v, moan(1.6f, 110.f, 70.f, 420.f, 0.5f), 0.6f, 0.15f);
        addInto(v, moan(1.2f, 90.f, 58.f, 380.f, 0.5f), 0.45f, 1.3f);
        addInto(v, gnash(s.dur, sd + 1), 0.6f);
    } else if (id == "gledge") {  // more robotic, the drill behind
        VSpec s = vs(86.f, 0.2f, 4.8f, 0.9f, sd);
        s.src = 3, s.steps = true, s.ring = 40.f;
        v = voice(s);
        crush(v, 3, 20.f, 0.35f);
        addInto(v, rumble(s.dur, 260.f, 0.5f, sd + 1), 0.35f);
    } else if (id == "kyro") {  // the bubbles blended into the rasp
        VSpec s = vs(112.f, 0.22f, 5.4f, 1.f, sd);
        s.breath = 0.55f, s.rough = 0.3f;
        v = voice(s);
        std::vector<float> b = bubbles(s.dur, 14.f, 200.f, 500.f, 0.5f, sd + 1);
        OnePole lp;
        for (float& x : b) x = lp.lp(x, 700.f);
        addInto(v, b, 0.05f);
    } else if (id == "l1z1x") {  // angrier: barked, wider, harder
        VSpec s = vs(128.f, 0.5f, 7.6f, 1.04f, sd);
        s.src = 3, s.steps = true, s.ring = 70.f, s.cons = 0.4f;
        v = voice(s);
        for (float& x : v) x = soft(x * 3.5f);
        overlayJunk(v, 0.5f, sd + 1);
    } else if (id == "mahact") {  // deep, commanding, talking down to you
        VSpec s = vs(80.f, 0.1f, 4.1f, 0.84f, sd);
        s.breath = 0.08f, s.cons = 0.6f;
        v = voice(s);
        VSpec low = s;
        low.f0 = 40.f, low.form = 0.72f, low.rough = 0.2f;
        addInto(v, voice(low), 0.5f);
        echo(v, 0.11f, 0.3f, 0.3f);
    } else if (id == "myko") {  // no bubbles
        VSpec s = vs(100.f, 0.12f, 4.6f, 0.92f, sd);
        s.chorus = 4, s.detune = 0.01f, s.spread = 0.012f, s.breath = 0.3f;
        v = voice(s);
        for (size_t i = 0; i < v.size(); ++i) v[i] *= 0.7f + 0.3f * std::sin(kTwoPi * 2.2f * Buf::t(i));
    } else if (id == "nekro") {  // angry modulated yelling, fighting to be heard over the corrupted noise
        VSpec s = vs(165.f, 0.5f, 7.f, 1.05f, sd);
        s.rough = 0.5f, s.breath = 0.2f, s.cons = 0.6f;
        std::vector<float> yell = voice(s);
        Ph ring;
        for (float& x : yell) x = soft(x * 4.f) * (0.55f + 0.45f * ring.sine(36.f));  // the modulation
        v.assign(size_t(s.dur * float(R)), 0.f);
        Noise nz(sd + 5), surge(sd + 6);
        Band bd;
        OnePole sm;
        for (size_t i = 0; i < v.size(); ++i) {  // the noise and the voice wrestle: one surges as the other falls back
            const float w = clamp01(0.5f + sm.lp(surge.next(), 2.5f) * 60.f);
            const float y = i < yell.size() ? yell[i] : 0.f;
            v[i] = y * (0.45f + 0.75f * w) + bd.bp(nz.next(), 600.f, 4500.f) * 0.5f * (1.2f - w);
        }
        overlayJunk(v, 0.8f, sd + 1);
        statics = 0.05f;
    } else if (id == "obsidian") {  // eldritch, possessed: the Lazaxian over a voice beneath it and a whisper beside it
        VSpec s = vs(84.f, 0.25f, 4.f, 0.85f, sd);
        s.backwards = true, s.cons = 0.3f, s.vib = 0.03f, s.vibHz = 3.f;
        v = voice(s);
        VSpec deep = s;
        deep.f0 = 42.f, deep.form = 0.68f, deep.rough = 0.6f;
        addInto(v, voice(deep), 0.6f);
        VSpec w = s;
        w.src = 1, w.backwards = false, w.seed = sd + 9;
        addInto(v, voice(w), 0.35f, 0.04f);
        addInto(v, crystal(207.7f, 0.4f), 0.12f, 0.2f);
        echo(v, 0.17f, 0.5f, 0.45f);
    } else if (id == "tnelis") {  // lower
        VSpec s = vs(100.f, 0.2f, 8.f, 0.84f, sd);
        s.src = 1, s.cons = 0.5f;
        v = voice(s);
        VSpec low = s;
        low.src = 0, low.breath = 0.6f, low.f0 = 92.f;
        addInto(v, voice(low), 0.3f);
        gain(v, 0.85f);
        statics = 0.015f;
    } else if (id == "vuilraith") {  // a deep lovecraftian growl: many throats, far down
        VSpec s = vs(42.f, 0.2f, 3.f, 0.62f, sd);
        s.rough = 1.f, s.breath = 0.25f, s.chorus = 3, s.detune = 0.035f, s.spread = 0.02f, s.dur = 2.6f;
        v = voice(s);
        for (float& x : v) x = soft(x * 3.f);
        Buf sub(s.dur);
        Ph a;
        for (size_t i = 0; i < sub.n(); ++i) sub.v[i] = a.sine(31.f + 4.f * std::sin(kTwoPi * 0.3f * Buf::t(i))) * passEnv(Buf::t(i), s.dur, 0.4f);
        addInto(v, sub.v, 0.4f);
        flicker(v, 0.2f, 0.7f);
        echo(v, 0.23f, 0.45f, 0.35f);
    } else if (id == "zelian") {  // + rocks crushing into each other
        VSpec s = vs(60.f, 0.15f, 3.6f, 0.72f, sd);
        s.rough = 0.9f, s.breath = 0.2f;
        v = voice(s);
        for (float& x : v) x = soft(x * 3.f);
        addInto(v, rockGrind(s.dur, sd + 1), 0.45f);
    } else {
        return factionVoice(id);
    }
    return onAir(std::move(v), statics, sd + 500);
}

}  // namespace
}  // namespace audio

#ifndef DRAFT6_NO_MAIN
int main(int argc, char** argv) {
    using namespace audio;
    const std::string dir = argc > 1 ? argv[1] : ".";
    struct S { const char* name; std::vector<float> (*make)(); float loud; float hallMix; };
    const S list[] = {
        {"m_turn_yours_v2", turnYours2, 0.1f, 0.2f},          {"m_round_new_v2", roundNew2, 0.12f, 0.3f},
        {"m_tech_research_v2", techResearch2, 0.08f, 0.15f},  {"m_objective_public_v2", objectivePublic2, 0.12f, 0.3f},
        {"m_hero_warning", heroWarning, 0.12f, 0.25f},        {"c_accept_v2", codecAccept2, 0.08f, 0.1f},
        {"c_counter_v2", codecCounter2, 0.08f, 0.1f},         {"c_alert_v2", codecAlert2, 0.11f, 0.15f},
    };
    auto report = [&](const std::string& name, const std::vector<float>& v, const std::vector<int16_t>& out) {
        bool bad = false;
        for (float x : v) bad |= !(x == x) || std::fabs(x) > 1e6f;
        double sq = 0;
        int peak = 0;
        for (int16_t x : out) sq += double(x) * x, peak = std::max(peak, std::abs(int(x)));
        std::printf("%-24s %4.2f s  peak %5d  rms %5.0f %s\n", name.c_str(), double(out.size()) / R, peak, std::sqrt(sq / double(std::max<size_t>(1, out.size()))), bad ? "BAD" : "");
    };
    for (const S& s : list) {
        std::vector<float> v = s.make();
        const std::vector<int16_t> out = grim(v, s.loud, s.hallMix);
        report(s.name, v, out);
        writeWav(dir + "/" + s.name + ".wav", out);
    }
    const char* redo[] = {"arborec", "bastion", "cheiran", "cymiae", "ghoti", "gledge", "kyro", "l1z1x", "mahact", "myko", "nekro", "obsidian", "tnelis", "vuilraith", "zelian"};
    for (const char* id : redo) {
        std::vector<float> v = factionVoice2(id);
        const std::vector<int16_t> out = grim(v, 0.09f, 0.1f);
        report(std::string("v_") + id + "_v2", v, out);
        writeWav(dir + "/v_" + id + "_v2.wav", out);
    }
    return 0;
}
#endif
