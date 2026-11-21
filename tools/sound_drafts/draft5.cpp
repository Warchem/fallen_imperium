// Build: g++ -std=c++17 -O2 -Isrc tools/sound_drafts/draft5.cpp -o draft5 && ./draft5 OUTDIR   (writes the WAVs)
// Sound drafts, round 5 (owner 2026-11-19 11:59: "make me a draft of the rest of the sounds that are silent today, as well as
// the comms/codec/negotiation screens"). Built on the game's own grim toolkit (synth.cpp + grim.inc, the sounds he kept):
//   - the moments that are silent today: your turn, a new round, cards, technology, scoring, the council, deals,
//     betrayal, Kingslay, dice, victory and defeat, exploring, relics, heroes
//   - the comms codec: a call coming in, the channel opening and closing, accepted / declined / countered, a threat, the
//     red betrayal alert
//   - a voice for every faction: not words - a made-up tongue in each faction's own manner (the ways of speaking the codec
//     already draws: the L1Z1X's machine junk, the Nekro's few words in the noise, the Obsidian's Lazaxian, the Glimmer
//     fading in and out, the Ghemina's Lord and Lady finishing each other's sentences, the Arborec swaying, the Creuss and
//     Crimson whispering, the Zelian's grinding growl ...), heard through the codec's radio.
#include "audio/synth.cpp"

#include <cstdio>
#include <string>

namespace audio {
namespace {

// ------------------------------------------------------------------ small helpers for this round
std::vector<float> silence(float dur) { return std::vector<float>(size_t(dur * float(R)), 0.f); }
void gain(std::vector<float>& v, float g) {
    for (float& x : v) x *= g;
}
void fadeOut(std::vector<float>& v, float secs) {
    const size_t n = std::min(v.size(), size_t(secs * float(R)));
    for (size_t i = 0; i < n; ++i) v[v.size() - n + i] *= 1.f - float(i) / float(n);
}
// a horn / brass chord: saws a little detuned, swelling and dark, with its brightness following the swell
std::vector<float> brass(float dur, const float* f, int n, float bright, float attack, float release) {
    Buf b(dur);
    Ph p[16];
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float e = smooth01(t / attack) * (t > dur - release ? clamp01((dur - t) / release) : 1.f);
        float x = 0.f;
        for (int k = 0; k < n; ++k) x += p[k * 2].saw(f[k] * 0.997f) + p[k * 2 + 1].saw(f[k] * 1.003f);
        b.v[i] = lp.lp(soft(x * 0.7f), 250.f + bright * e) * e;
    }
    return b.v;
}
// a war drum: a pitched thump and a skin slap
std::vector<float> drum(float f, float tau, uint32_t seed) {
    std::vector<float> v = thump(f * 2.2f, f, 0.05f, tau);
    addInto(v, hiss(0.05f, 300.f, 2200.f, 0.001f, 0.012f, seed), 0.6f);
    return v;
}
// a heartbeat: lub-dub
std::vector<float> heartbeat() {
    std::vector<float> v = thump(70.f, 38.f, 0.05f, 0.07f);
    addInto(v, thump(62.f, 34.f, 0.05f, 0.06f), 0.7f, 0.2f);
    return v;
}
// a card slapped down onto metal: a papery snap and a low knock
std::vector<float> cardSlap(uint32_t seed) {
    std::vector<float> v = hiss(0.09f, 1200.f, 6000.f, 0.001f, 0.018f, seed);
    const float res[3] = {180.f, 410.f, 820.f};
    addInto(v, clank(res, 3, 8.f, 0.05f, seed + 1), 0.6f, 0.005f);
    addInto(v, thump(120.f, 60.f, 0.03f, 0.05f), 0.6f, 0.003f);
    return v;
}
// a gavel striking in a big hall
std::vector<float> gavel(uint32_t seed) {
    const float res[4] = {210.f, 470.f, 980.f, 1730.f};
    std::vector<float> v = clank(res, 4, 12.f, 0.06f, seed);
    addInto(v, thump(140.f, 70.f, 0.03f, 0.08f), 0.9f);
    return v;
}
// a crowd murmuring far off: many soft formant voices
std::vector<float> murmur(float dur, float level, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed), mod(seed + 1);
    BP a, c, d;
    OnePole s1, s2;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        if (i % 256 == 0) {
            a.set(500.f + 250.f * s1.lp(mod.next(), 2.f) * 30.f, 3.f);
            c.set(1300.f + 500.f * s2.lp(mod.next(), 2.f) * 30.f, 4.f);
            d.set(2600.f, 5.f);
        }
        const float n = nz.next();
        b.v[i] = (a.run(n) + 0.6f * c.run(n) + 0.2f * d.run(n)) * level * passEnv(t, dur, 0.4f);
    }
    return b.v;
}
// a scanner sweep: a band of noise and a sine sliding up and back
std::vector<float> sweep(float dur, float lo, float hi, uint32_t seed) {
    Buf b(dur);
    Noise nz(seed);
    BP bp;
    float ph = 0.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), u = t / dur;
        const float f = lo + (hi - lo) * (u < 0.6f ? smooth01(u / 0.6f) : 1.f - smooth01((u - 0.6f) / 0.4f));
        if (i % 32 == 0) bp.set(f * 1.5f, 8.f);
        ph += kTwoPi * f / float(R);
        b.v[i] = (bp.run(nz.next()) * 1.5f + std::sin(ph) * 0.25f) * passEnv(t, dur, 0.5f);
    }
    return b.v;
}
// a struck crystal (dark): inharmonic partials ringing out
std::vector<float> crystal(float f, float tau) {
    Buf b(tau * 5.f);
    const float part[5] = {1.f, 2.76f, 5.40f, 8.93f, 13.3f}, amp[5] = {1.f, 0.6f, 0.35f, 0.2f, 0.1f};
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        float x = 0.f;
        for (int k = 0; k < 5; ++k) x += std::sin(kTwoPi * f * part[k] * t) * amp[k] * fexp(-t / (tau / (1.f + 0.6f * float(k))));
        b.v[i] = x * env(t, 0.002f, 10.f);
    }
    return b.v;
}

// ================================================================== A: the moments that are silent today
// YOUR TURN: a squelch on the command channel, two low horn calls a minor third apart, a heartbeat under them
std::vector<float> turnYours() {
    std::vector<float> v = staticBurst(0.12f, 800.f, 3500.f, 5001);
    gain(v, 0.5f);
    const float a[2] = {73.4f, 110.f}, b[2] = {87.3f, 130.8f};
    std::vector<float> h1 = brass(0.38f, a, 2, 1300.f, 0.04f, 0.12f), h2 = brass(0.6f, b, 2, 1500.f, 0.04f, 0.35f);
    addInto(v, h1, 0.8f, 0.12f);
    addInto(v, h2, 0.8f, 0.5f);
    addInto(v, heartbeat(), 0.7f, 0.12f);
    grit(v, 0.4f, 1.8f, 5003);
    return v;
}
// A NEW ROUND: war drums gathering pace, a low horn swelling under them, a toll as the round turns
std::vector<float> roundNew() {
    std::vector<float> v = silence(2.4f);
    float at = 0.f;
    for (int k = 0; k < 7; ++k) {
        addInto(v, drum(52.f, 0.16f, 5011 + k), 0.6f + 0.06f * float(k), at);
        at += 0.3f - 0.03f * float(k);
    }
    const float f[3] = {55.f, 65.4f, 82.4f};
    addInto(v, brass(1.8f, f, 3, 900.f, 1.2f, 0.4f), 0.55f, 0.2f);
    addInto(v, toll(55.f, 0.6f, 5021), 0.8f, at + 0.05f);
    grit(v, 0.4f, 1.8f, 5023);
    return v;
}
// A STRATEGY CARD TAKEN: a heavy card slapped down and a short low chord
std::vector<float> strategyPick() {
    std::vector<float> v = cardSlap(5031);
    const float f[2] = {98.f, 146.8f};
    addInto(v, brass(0.45f, f, 2, 1100.f, 0.02f, 0.3f), 0.55f, 0.02f);
    grit(v, 0.3f, 1.6f, 5033);
    return v;
}
// AN ACTION CARD PLAYED: a sharp snap, a hiss of static, a sour tritone stab rising
std::vector<float> actionCard() {
    std::vector<float> v = cardSlap(5041);
    addInto(v, staticBurst(0.18f, 1500.f, 5000.f, 5043), 0.3f, 0.03f);
    const float f[2] = {82.4f, 116.5f};
    std::vector<float> s = brass(0.55f, f, 2, 1700.f, 0.015f, 0.35f);
    for (size_t i = 0; i < s.size(); ++i) s[i] *= 1.f;  // (kept flat: the stab)
    addInto(v, s, 0.6f, 0.06f);
    addInto(v, thump(90.f, 45.f, 0.04f, 0.08f), 0.6f, 0.06f);
    grit(v, 0.4f, 1.8f, 5045);
    return v;
}
// A TECHNOLOGY RESEARCHED: relays clicking in, a servo winding up, a cold hum settling into a low minor chord
std::vector<float> techResearch() {
    std::vector<float> v = ticks(0.35f, 0.045f, 2600.f, 0.5f, 5051);
    Buf w(0.6f);
    Ph a;
    OnePole lp;
    for (size_t i = 0; i < w.n(); ++i) {
        const float t = Buf::t(i);
        w.v[i] = lp.lp(a.sq(glide(90.f, 420.f, t, 0.55f), 0.3f), 1800.f) * passEnv(t, 0.6f, 0.8f) * 0.5f;
    }
    addInto(v, w.v, 0.6f, 0.25f);
    const float f[3] = {110.f, 130.8f, 164.8f};
    std::vector<float> hum = drone(1.0f, f, 3, 900.f, 0.004f, 0.25f);
    addInto(v, hum, 0.6f, 0.75f);
    addInto(v, weld(0.12f, 5053), 0.2f, 0.8f);
    grit(v, 0.35f, 1.7f, 5055);
    return v;
}
// AN OBJECTIVE SCORED: a heavy bell, a low choir swelling from minor to a bare fifth - a grim triumph
std::vector<float> objectivePublic() {
    std::vector<float> v = toll(65.4f, 0.8f, 5061);
    v.resize(size_t(2.4f * float(R)), 0.f);
    const float m[3] = {130.8f, 155.6f, 196.f}, f5[2] = {130.8f, 196.f};
    addInto(v, chant(1.0f, m, 3, 520.f, 1050.f, 0.008f, 0.6f), 0.55f, 0.1f);
    addInto(v, chant(1.3f, f5, 2, 560.f, 1100.f, 0.008f, 0.3f), 0.6f, 0.9f);
    addInto(v, drum(49.f, 0.25f, 5063), 0.8f, 0.9f);
    grit(v, 0.3f, 1.6f, 5065);
    return v;
}
// A SECRET OBJECTIVE SCORED: quieter - a muffled toll behind a door, whispers, a seal breaking
std::vector<float> objectiveSecret() {
    std::vector<float> v = toll(73.4f, 0.6f, 5071);
    OnePole lp;
    for (float& x : v) x = lp.lp(x, 600.f) * 1.4f;
    v.resize(size_t(1.9f * float(R)), 0.f);
    addInto(v, murmur(1.4f, 0.5f, 5073), 0.5f, 0.15f);
    addInto(v, hiss(0.25f, 2500.f, 7000.f, 0.03f, 0.1f, 5075), 0.35f, 0.3f);
    const float res[3] = {300.f, 700.f, 1500.f};
    addInto(v, clank(res, 3, 6.f, 0.03f, 5077), 0.5f, 0.35f);  // the seal cracking
    grit(v, 0.3f, 1.5f, 5079);
    return v;
}
// A PLANET TAKEN: a banner pole driven into the ground, a clank of iron, a short low horn
std::vector<float> planetTaken() {
    const float res[4] = {150.f, 380.f, 760.f, 1400.f};
    std::vector<float> v = clank(res, 4, 14.f, 0.12f, 5081);
    addInto(v, thump(80.f, 36.f, 0.05f, 0.14f), 1.f);
    addInto(v, rumble(0.4f, 200.f, 0.1f, 5083), 0.3f);
    const float f[2] = {73.4f, 110.f};
    addInto(v, brass(0.7f, f, 2, 1200.f, 0.05f, 0.4f), 0.6f, 0.2f);
    grit(v, 0.4f, 1.8f, 5085);
    return v;
}
// AN AGENDA REVEALED: three gavel strikes in the council hall, a crowd murmuring after them
std::vector<float> agendaReveal() {
    std::vector<float> v = silence(2.1f);
    for (int k = 0; k < 3; ++k) addInto(v, gavel(5091 + k), 0.9f, 0.05f + 0.32f * float(k));
    addInto(v, murmur(1.4f, 0.6f, 5095), 0.5f, 0.7f);
    const float f[2] = {65.4f, 98.f};
    addInto(v, chant(1.2f, f, 2, 450.f, 950.f, 0.006f, 0.4f), 0.3f, 0.8f);
    grit(v, 0.3f, 1.6f, 5097);
    return v;
}
// THE RULING: one gavel, a low brass chord that settles on a bare fifth
std::vector<float> agendaResult() {
    std::vector<float> v = gavel(5101);
    const float a[3] = {73.4f, 87.3f, 110.f}, b[2] = {73.4f, 110.f};
    addInto(v, brass(0.55f, a, 3, 1300.f, 0.03f, 0.2f), 0.6f, 0.08f);
    addInto(v, brass(0.9f, b, 2, 1000.f, 0.08f, 0.6f), 0.6f, 0.55f);
    grit(v, 0.35f, 1.7f, 5103);
    return v;
}
// A DEAL MADE: a heavy seal stamped, then two low notes that agree
std::vector<float> dealMade() {
    std::vector<float> v = thump(110.f, 48.f, 0.04f, 0.1f);
    const float res[3] = {240.f, 560.f, 1100.f};
    addInto(v, clank(res, 3, 10.f, 0.05f, 5111), 0.7f);
    const float a[1] = {98.f}, b[2] = {98.f, 146.8f};
    addInto(v, brass(0.25f, a, 1, 1200.f, 0.02f, 0.1f), 0.6f, 0.18f);
    addInto(v, brass(0.6f, b, 2, 1200.f, 0.02f, 0.4f), 0.6f, 0.42f);
    grit(v, 0.3f, 1.6f, 5113);
    return v;
}
// A DEAL REFUSED: a sour buzz, a low tone falling away, a switch thrown off
std::vector<float> dealRefused() {
    Buf b(0.75f);
    Ph a, c;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float f = glide(130.f, 70.f, t, 0.7f);
        b.v[i] = lp.lp(soft((a.sq(f) + c.sq(f * semis(1.f))) * 1.5f), 1400.f) * env(t, 0.01f, 0.3f);
    }
    std::vector<float> v = b.v;
    const float res[2] = {900.f, 2100.f};
    addInto(v, clank(res, 2, 10.f, 0.01f, 5121), 0.5f, 0.68f);
    grit(v, 0.5f, 2.f, 5123);
    return v;
}
// BETRAYAL (a promise broken, the red codec): a dissonant brass blow, two heartbeats, static tearing, a drone sinking
std::vector<float> betrayal() {
    const float f[4] = {65.4f, 69.3f, 92.5f, 98.f};  // a cluster: minor seconds and a tritone
    std::vector<float> v = brass(0.7f, f, 4, 2400.f, 0.01f, 0.5f);
    for (float& x : v) x = soft(x * 2.2f);
    addInto(v, thump(70.f, 30.f, 0.08f, 0.2f), 1.f);
    addInto(v, heartbeat(), 0.8f, 0.75f);
    addInto(v, heartbeat(), 0.7f, 1.35f);
    addInto(v, staticBurst(0.5f, 900.f, 5000.f, 5131), 0.35f, 0.5f);
    Buf s(1.6f);
    Ph a, c;
    OnePole lp;
    for (size_t i = 0; i < s.n(); ++i) {
        const float t = Buf::t(i), fd = glide(73.4f, 49.f, t, 1.5f);
        s.v[i] = lp.lp(a.saw(fd) + c.saw(fd * 1.006f), 500.f) * passEnv(t, 1.6f, 0.3f);
    }
    addInto(v, s.v, 0.6f, 0.6f);
    grit(v, 0.5f, 2.f, 5133);
    return v;
}
// KINGSLAY!: two war horns a tritone apart, war drums pounding, a crowd roaring
std::vector<float> kingslay() {
    std::vector<float> v = silence(2.7f);
    const float h1[2] = {65.4f, 98.f}, h2[2] = {92.5f, 138.6f};
    addInto(v, brass(0.9f, h1, 2, 1600.f, 0.06f, 0.3f), 0.7f, 0.f);
    addInto(v, brass(1.2f, h2, 2, 1800.f, 0.06f, 0.5f), 0.7f, 0.75f);
    for (int k = 0; k < 8; ++k) addInto(v, drum(55.f, 0.12f, 5141 + k), 0.7f + (k % 2 ? 0.f : 0.2f), 0.1f + 0.22f * float(k));
    std::vector<float> roar = rumble(2.0f, 900.f, 0.55f, 5151);
    addInto(v, roar, 0.25f, 0.5f);
    addInto(v, murmur(1.8f, 1.2f, 5153), 0.5f, 0.6f);
    grit(v, 0.5f, 2.f, 5155);
    return v;
}
// DICE ROLLING: a handful of dice rattling across a metal tray and settling
std::vector<float> diceRoll() {
    std::vector<float> v = silence(0.75f);
    Noise pick(5161);
    for (int d = 0; d < 5; ++d) {
        float at = 0.01f * float(d) + 0.02f * uni(pick), gap = 0.05f + 0.03f * uni(pick), g = 0.6f;
        for (int k = 0; k < 7 && at < 0.65f; ++k) {
            const float f = 1800.f + 1600.f * uni(pick);
            const float res[2] = {f, f * 1.61f};
            addInto(v, clank(res, 2, 20.f, 0.01f, 5171 + d * 13 + k), g, at);
            at += gap;
            gap *= 0.78f;
            g *= 0.8f;
        }
    }
    addInto(v, rumble(0.5f, 300.f, 0.2f, 5199), 0.15f);
    grit(v, 0.2f, 1.4f, 5201);
    return v;
}
// VICTORY: low brass and timpani, a choir rising out of a minor chord to an open fifth, a bell at the top
std::vector<float> victory() {
    std::vector<float> v = silence(4.2f);
    const float c1[3] = {65.4f, 77.8f, 98.f}, c2[3] = {73.4f, 87.3f, 110.f}, c3[3] = {65.4f, 98.f, 130.8f};
    addInto(v, brass(1.1f, c1, 3, 1500.f, 0.08f, 0.3f), 0.6f, 0.f);
    addInto(v, brass(1.0f, c2, 3, 1700.f, 0.06f, 0.3f), 0.6f, 0.9f);
    addInto(v, brass(2.3f, c3, 3, 2000.f, 0.1f, 1.2f), 0.75f, 1.8f);
    for (float at : {0.f, 0.9f, 1.8f, 1.95f, 2.1f}) addInto(v, drum(45.f, 0.3f, 5211 + int(at * 10)), 0.9f, at);
    const float ch[3] = {130.8f, 196.f, 261.6f};
    addInto(v, chant(2.4f, ch, 3, 600.f, 1150.f, 0.008f, 0.4f), 0.5f, 1.7f);
    addInto(v, toll(65.4f, 1.0f, 5221), 0.8f, 1.8f);
    grit(v, 0.3f, 1.6f, 5223);
    return v;
}
// DEFEAT (eliminated): a drone sinking, one toll, cold wind, a heartbeat that stops
std::vector<float> defeat() {
    Buf b(3.2f);
    Ph a, c, d;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), f = glide(98.f, 41.f, t, 3.f);
        b.v[i] = lp.lp(a.saw(f) + c.saw(f * 1.005f) + 0.6f * d.saw(f * semis(3.f)), 380.f) * passEnv(t, 3.2f, 0.15f) * 0.8f;
    }
    std::vector<float> v = b.v;
    addInto(v, toll(49.f, 0.9f, 5231), 0.9f, 0.1f);
    addInto(v, hiss(3.0f, 300.f, 1400.f, 1.0f, 1.2f, 5233), 0.25f, 0.2f);
    addInto(v, heartbeat(), 0.6f, 0.6f);
    addInto(v, heartbeat(), 0.45f, 1.4f);
    addInto(v, thump(60.f, 34.f, 0.05f, 0.06f), 0.3f, 2.3f);  // ...and only half of the last one
    grit(v, 0.3f, 1.5f, 5235);
    return v;
}
// EXPLORING: a scanner sweeping the surface, then a dark shimmer as something answers
std::vector<float> explore() {
    std::vector<float> v = sweep(0.9f, 400.f, 2400.f, 5241);
    addInto(v, sonar(330.f, 0.12f), 0.5f, 0.05f);
    const float f[2] = {207.7f, 220.f};  // a half-step apart: unsettled
    std::vector<float> sh = drone(0.8f, f, 2, 2600.f, 0.002f, 0.3f);
    addInto(v, sh, 0.3f, 0.75f);
    addInto(v, crystal(415.f, 0.25f), 0.25f, 0.8f);
    grit(v, 0.3f, 1.5f, 5243);
    return v;
}
// A RELIC GAINED: something ancient waking - a deep hum rising, a crystal struck low, dust falling
std::vector<float> relicGained() {
    const float f[2] = {55.f, 82.4f};
    std::vector<float> v = drone(1.7f, f, 2, 600.f, 0.004f, 0.55f);
    for (size_t i = 0; i < v.size(); ++i) v[i] *= 0.7f + 0.3f * std::sin(kTwoPi * 3.f * Buf::t(i));
    addInto(v, crystal(196.f, 0.6f), 0.55f, 0.8f);
    addInto(v, crystal(207.7f, 0.5f), 0.3f, 0.82f);
    addInto(v, hiss(0.9f, 2000.f, 6000.f, 0.3f, 0.4f, 5251), 0.12f, 0.8f);
    grit(v, 0.3f, 1.5f, 5253);
    return v;
}
// A HERO UNLEASHED: a massive blow, a roar rising out of it, a choir crying out
std::vector<float> heroUnleashed() {
    std::vector<float> v = thump(60.f, 26.f, 0.1f, 0.35f);
    const float res[4] = {90.f, 230.f, 470.f, 900.f};
    addInto(v, clank(res, 4, 10.f, 0.25f, 5261), 0.8f);
    v.resize(size_t(2.4f * float(R)), 0.f);
    Buf b(1.8f);
    Ph a, c;
    OnePole lp;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), f = glide(55.f, 110.f, t, 1.2f);
        b.v[i] = lp.lp(soft((a.saw(f) + c.saw(f * 1.5f)) * 1.5f), 300.f + 1800.f * t / 1.8f) * passEnv(t, 1.8f, 0.6f);
    }
    addInto(v, b.v, 0.6f, 0.1f);
    const float ch[3] = {110.f, 130.8f, 164.8f};
    addInto(v, chant(1.5f, ch, 3, 650.f, 1200.f, 0.01f, 0.35f), 0.55f, 0.6f);
    grit(v, 0.5f, 2.f, 5263);
    return v;
}

// ================================================================== B: the comms codec
// the codec's radio: a narrow band, a little drive, a bed of static that rides on the voice
bool g_radioSteady = false;  // (round 9) no dropouts - a voice that must not dip (the Nekro yelling)
void radio(std::vector<float>& v, float lo, float hi, float statics, uint32_t seed) {
    Band bd, sb;
    Noise nz(seed), pick(seed + 1);
    OnePole e, sg;
    float g = 1.f, until = 0.f;
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = Buf::t(i);
        if (t >= until) g = g_radioSteady || uni(pick) > 0.15f ? 1.f : 0.35f, until = t + 0.02f + 0.08f * uni(pick);  // dropouts, now and then
        const float lvl = e.lp(std::fabs(v[i]), 20.f);
        const float n = sb.bp(nz.next(), 600.f, 4500.f) * 2.f;  // the static stays in the radio's band too
        v[i] = soft(bd.bp(v[i], lo, hi) * 2.2f) * sg.lp(g, 120.f) + n * statics * (0.15f + lvl * 2.f);  // (the dropouts eased: no clicks)
    }
}
// INCOMING CALL: a carrier pulsing three times (two tones a half-step apart), static between, a squelch as it opens
std::vector<float> codecIncoming() {
    std::vector<float> v = silence(1.45f);
    for (int k = 0; k < 3; ++k) {
        Buf b(0.22f);
        Ph a, c;
        for (size_t i = 0; i < b.n(); ++i) {
            const float t = Buf::t(i);
            b.v[i] = (a.sq(660.f, 0.4f) * 0.5f + c.sq(699.f, 0.4f) * 0.5f) * (t < 0.2f ? 1.f : (0.22f - t) / 0.02f) * smooth01(t / 0.01f);
        }
        addInto(v, b.v, 0.35f, 0.05f + 0.38f * float(k));
        addInto(v, staticBurst(0.14f, 900.f, 4000.f, 5301 + k), 0.25f, 0.27f + 0.38f * float(k));
    }
    addInto(v, staticBurst(0.12f, 500.f, 5000.f, 5311), 0.6f, 1.2f);
    radio(v, 300.f, 3400.f, 0.02f, 5313);
    return v;
}
// CHANNEL OPEN: a squelch click and static washing up
std::vector<float> codecOpen() {
    std::vector<float> v = hiss(0.35f, 600.f, 4500.f, 0.08f, 0.12f, 5321);
    const float res[2] = {1800.f, 3100.f};
    addInto(v, clank(res, 2, 8.f, 0.006f, 5323), 0.8f);
    radio(v, 300.f, 3600.f, 0.03f, 5325);
    return v;
}
// CHANNEL CLOSED: static cut off and a falling blip
std::vector<float> codecClose() {
    std::vector<float> v = staticBurst(0.16f, 700.f, 4200.f, 5331);
    Buf b(0.14f);
    float ph = 0.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        ph += kTwoPi * glide(900.f, 300.f, t, 0.12f) / float(R);
        b.v[i] = (std::sin(ph) > 0 ? 1.f : -1.f) * 0.3f * env(t, 0.002f, 0.05f);
    }
    addInto(v, b.v, 0.6f, 0.15f);
    radio(v, 300.f, 3400.f, 0.02f, 5333);
    return v;
}
// ACCEPTED (codec): the seal over the radio and a two-tone "confirmed"
std::vector<float> codecAccept() {
    std::vector<float> v = thump(100.f, 50.f, 0.04f, 0.08f);
    Buf b(0.5f);
    Ph a;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        b.v[i] = a.sq(t < 0.18f ? 440.f : 660.f, 0.35f) * 0.4f * (t < 0.16f || (t > 0.2f && t < 0.46f) ? 1.f : 0.f);
    }
    addInto(v, b.v, 0.5f, 0.12f);
    radio(v, 300.f, 3400.f, 0.02f, 5341);
    return v;
}
// DECLINED (codec): a harsh buzz and the line cut
std::vector<float> codecDecline() {
    Buf b(0.5f);
    Ph a, c;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        b.v[i] = soft((a.saw(110.f) + c.saw(116.5f)) * 2.f) * (t < 0.4f ? 1.f : 0.f) * smooth01(t / 0.01f);
    }
    std::vector<float> v = b.v;
    addInto(v, staticBurst(0.15f, 700.f, 4000.f, 5351), 0.5f, 0.4f);
    radio(v, 300.f, 3400.f, 0.02f, 5353);
    return v;
}
// COUNTER-OFFER: a ratchet turning and two tones asking (rising, unresolved)
std::vector<float> codecCounter() {
    std::vector<float> v = ticks(0.25f, 0.035f, 1900.f, 0.5f, 5361);
    Buf b(0.45f);
    Ph a;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        b.v[i] = a.sq(t < 0.2f ? 523.f : 740.f, 0.35f) * 0.4f * (t < 0.18f || (t > 0.22f && t < 0.43f) ? 1.f : 0.f);
    }
    addInto(v, b.v, 0.5f, 0.22f);
    radio(v, 300.f, 3400.f, 0.02f, 5363);
    return v;
}
// A THREAT: a low pulse like a slow heart, a weapons lock rising over it, static
std::vector<float> codecThreat() {
    std::vector<float> v = silence(1.4f);
    for (int k = 0; k < 3; ++k) addInto(v, thump(65.f, 38.f, 0.05f, 0.09f), 0.9f, 0.05f + 0.42f * float(k));
    Buf b(1.2f);
    Ph a;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float rate = 6.f + 20.f * t;  // the lock beeps faster and faster
        const float on = std::sin(kTwoPi * rate * t * 0.5f) > 0.3f ? 1.f : 0.f;
        b.v[i] = a.sq(glide(880.f, 1320.f, t, 1.1f), 0.5f) * 0.22f * on * smooth01(t / 0.2f);
    }
    addInto(v, b.v, 0.6f, 0.15f);
    addInto(v, staticBurst(0.3f, 600.f, 4000.f, 5371), 0.3f, 1.05f);
    radio(v, 250.f, 3400.f, 0.03f, 5373);
    return v;
}
// THE RED ALERT (betrayal codec): an alarm stab, a distorted burst of static, the alarm again lower
std::vector<float> codecAlert() {
    std::vector<float> v = silence(1.5f);
    for (int k = 0; k < 2; ++k) {
        Buf b(0.42f);
        Ph a, c;
        for (size_t i = 0; i < b.n(); ++i) {
            const float t = Buf::t(i), f = (k == 0 ? 520.f : 390.f) * (1.f + 0.06f * std::sin(kTwoPi * 9.f * t));
            b.v[i] = soft((a.saw(f) + c.saw(f * semis(1.f))) * 2.5f) * smooth01(t / 0.01f) * (t > 0.36f ? (0.42f - t) / 0.06f : 1.f);
        }
        addInto(v, b.v, 0.45f, 0.02f + 0.62f * float(k));
    }
    addInto(v, staticBurst(0.25f, 500.f, 5000.f, 5381), 0.8f, 0.42f);
    addInto(v, thump(60.f, 28.f, 0.06f, 0.18f), 0.8f, 0.02f);
    radio(v, 250.f, 3600.f, 0.05f, 5383);
    return v;
}

// ================================================================== C: the faction voices
// Not words: syllables of a made-up tongue - a voice (pulses through three moving formants, like a throat and mouth), or a
// whisper, an insect's buzz, a machine - shaped by each faction's manner, then heard through the codec's radio.
struct VSpec {
    float f0 = 120.f;      // pitch (Hz)
    float range = 0.18f;   // how much the pitch moves with the words
    float rate = 6.f;      // syllables a second
    float form = 1.f;      // formant scale: < 1 a bigger, darker throat
    float breath = 0.1f;   // air in the voice
    float rough = 0.f;     // a growl (an octave-down pulse, jitter)
    int src = 0;           // 0 a voice, 1 a whisper, 2 an insect's buzz, 3 a machine
    int chorus = 1;        // voices speaking together
    float detune = 0.f;    // their pitch apart (fraction)
    float spread = 0.f;    // their timing apart (s)
    float dur = 2.3f;
    float cons = 0.5f;     // consonants: hisses and clicks between syllables
    float vib = 0.f, vibHz = 5.f;
    float sway = 0.f;      // a slow sway of pitch (0.6 Hz)
    bool steps = false;    // the pitch in hard steps (machine-like)
    bool backwards = false;  // syllables swell and stop short, as if played backwards
    float ring = 0.f;      // ring modulation (Hz; machines)
    int words = 0;         // 0 all words; 1 / 2 only the even / odd words (two speakers sharing a line)
    uint32_t seed = 1;
    float punch = 0.f;     // (round 7) anger: stressed syllables hit harder and start sharper
    bool continuous = false;  // (round 9) no gaps, no breath, no fall between syllables: one unbroken yell
};
struct Syl {
    float a, b;  // start, end (s)
    int vowel, word;
    float accent;
    bool fric, plos;
};
std::vector<Syl> sylPlan(const VSpec& s) {
    std::vector<Syl> out;
    Noise rn(s.seed);
    float t = 0.1f;
    int word = 0, left = 1 + int(uni(rn) * 3.f);
    bool paused = false;
    while (t < s.dur - 0.35f) {
        const float len = (1.f / s.rate) * (0.65f + 0.7f * uni(rn));
        Syl y;
        y.a = t, y.b = t + len, y.vowel = int(uni(rn) * 5.99f), y.word = word, y.accent = rn.next();
        y.fric = uni(rn) < s.cons * 0.5f, y.plos = !y.fric && uni(rn) < s.cons * 0.6f;
        out.push_back(y);
        t += len;
        if (--left <= 0) {
            ++word;
            left = 1 + int(uni(rn) * 3.f);
            if (!s.continuous) t += 0.04f + 0.08f * uni(rn);
            if (!s.continuous && !paused && t > s.dur * 0.42f) t += 0.16f + 0.12f * uni(rn), paused = true;  // the breath mid-line
        }
    }
    return out;
}
std::vector<float> voiceOne(const VSpec& s, float pitchMul, float offset, uint32_t nseed) {
    static const float F[6][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {300, 2200, 3000}, {570, 840, 2410}, {320, 870, 2240}, {500, 1500, 2500}};
    const std::vector<Syl> plan = sylPlan(s);
    Buf b(s.dur);
    Noise nz(nseed), jit(nseed + 7);
    Ph glot, sub, ringO, buzzAm;
    BP f1, f2, f3;
    OnePole sf1, sf2, sf3, sacc, senv, hp;
    size_t k = 0;
    const float last = plan.empty() ? 0.f : plan.back().b;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i) - offset;
        while (k + 1 < plan.size() && t >= plan[k].b) ++k;
        float amp = 0.f;
        const Syl* y = plan.empty() ? nullptr : &plan[k];
        bool inWord = y && (s.words == 0 || (y->word % 2) == (s.words - 1));
        if (y && inWord && t >= y->a && t < y->b) {
            const float u = (t - y->a) / (y->b - y->a);
            amp = s.backwards ? smooth01(u / 0.8f) * (u > 0.92f ? (1.f - u) / 0.08f : 1.f) : smooth01(u / 0.12f) * (u > 0.7f ? 1.f - smooth01((u - 0.7f) / 0.3f) : 1.f);
            if (s.punch > 0.f) amp = smooth01(u / 0.05f) * (u > 0.6f ? 1.f - smooth01((u - 0.6f) / 0.4f) : 1.f) * (1.f + s.punch * std::max(0.f, y->accent));
            if (s.continuous) amp = (0.85f + 0.15f * smooth01(u / 0.1f)) * (1.f + s.punch * 0.3f * std::max(0.f, y->accent));
        }
        const float e = senv.lp(amp, 60.f);
        // pitch: falling over the line, each syllable's accent, the last one dropping
        const float acc = sacc.lp(y ? y->accent : 0.f, 9.f);
        float f = s.f0 * pitchMul * (1.f + s.range * (0.3f - 0.6f * clamp01(t / std::max(0.5f, last)))) * (1.f + s.range * 0.6f * acc);
        if (y && k + 1 == plan.size()) f *= 1.f - 0.12f * clamp01((t - y->a) / (y->b - y->a));
        f *= 1.f + s.vib * std::sin(kTwoPi * s.vibHz * t) + s.sway * std::sin(kTwoPi * 0.6f * t);
        f *= 1.f + 0.006f * jit.next() * (1.f + 6.f * s.rough);
        if (s.steps) f = s.f0 * pitchMul * semis(std::floor(12.f * flog(f / (s.f0 * pitchMul)) / 0.6931f + 0.5f) * 0.5f);  // half the steps: flat, chopped
        // the source
        float x = 0.f;
        const float n = nz.next();
        if (s.src == 1) {
            x = n * 1.1f;
        } else if (s.src == 2) {
            x = (glot.saw(f) * 0.7f + n * 0.2f) * (0.45f + 0.55f * (buzzAm.sine(55.f) > 0.f ? 1.f : 0.2f));
        } else if (s.src == 3) {
            x = glot.sq(f, 0.35f) * 0.6f;
        } else {
            const float ph = glot.step(f);
            const float pulse = ph < 0.45f ? std::sin(3.14159f * ph / 0.45f) : 0.f;
            x = pulse * pulse * 1.6f - 0.5f + n * s.breath * 1.5f;
            if (s.rough > 0.f) x += s.rough * (sub.sq(f * 0.5f, 0.3f) * 0.35f) * (0.6f + 0.4f * n);
        }
        if (s.ring > 0.f) x *= ringO.sine(s.ring);
        x = hp.lp(x, 8000.f);
        // the mouth: three formants gliding to the syllable's vowel
        if (i % 32 == 0 && y) {
            const float* v = F[y->vowel];
            f1.set(std::max(150.f, sf1.lp(v[0] * s.form, 300.f)), 6.f);
            f2.set(sf2.lp(v[1] * s.form, 300.f), 9.f);
            f3.set(sf3.lp(v[2] * s.form, 300.f), 12.f);
        }
        float out = f1.run(x) + 0.7f * f2.run(x) + 0.35f * f3.run(x);
        if (s.src == 1) out *= 0.9f;
        b.v[i] = out * e;
    }
    std::vector<float> v = b.v;
    // consonants at the syllables' starts: hisses (s, sh) and clicks (k, t)
    Noise cn(nseed + 11);
    for (const Syl& y : plan) {
        if (s.words != 0 && (y.word % 2) != (s.words - 1)) continue;
        const float at = y.a + offset;
        if (y.fric) {  // an "s" or a "sh": noise through a narrow high band, not a broad crack
            const float len = 0.06f + 0.05f * uni(cn), fc = (uni(cn) < 0.5f ? 4600.f : 2700.f) * s.form;
            Buf h(len);
            Noise hn(nseed + 31 + uint32_t(at * 100.f));
            BP bp;
            bp.set(fc, 2.5f);
            for (size_t i = 0; i < h.n(); ++i) h.v[i] = bp.run(hn.next()) * passEnv(Buf::t(i), len, 0.5f) * 1.5f;
            addInto(v, h.v, 0.25f * s.cons, at - 0.04f);
        }
        if (y.plos) {
            const float res[2] = {1800.f * s.form, 3200.f * s.form};
            std::vector<float> k = clank(res, 2, 5.f, 0.006f, nseed + 61 + int(at * 100));
            OnePole lp;
            for (float& x : k) x = lp.lp(x, 3500.f);  // a softer click (a "k", a "t"), not a crack
            addInto(v, k, 0.15f * s.cons, at);
        }
    }
    return v;
}
std::vector<float> voice(const VSpec& s) {
    std::vector<float> v;
    for (int c = 0; c < s.chorus; ++c) {
        const float pm = 1.f + s.detune * (c == 0 ? 0.f : (c % 2 ? 1.f : -1.f) * float((c + 1) / 2));
        std::vector<float> one = voiceOne(s, pm, s.spread * float(c), s.seed * 31u + uint32_t(c) * 977u);
        addInto(v, one, 1.f / (1.f + 0.6f * float(c)));
    }
    return v;
}
// the line ready for the review page: through the codec's radio, a squelch either side
std::vector<float> onAir(std::vector<float> v, float statics, uint32_t seed, float lo = 280.f, float hi = 3400.f) {
    std::vector<float> out = staticBurst(0.08f, 700.f, 4000.f, seed + 1);
    gain(out, 0.25f);
    addInto(out, v, 1.f, 0.03f);
    addInto(out, staticBurst(0.07f, 700.f, 4000.f, seed + 2), 0.2f, float(out.size()) / float(R));
    radio(out, lo, hi, statics, seed + 3);
    return out;
}
// per-faction finishing touches
void overlayBubbles(std::vector<float>& v, float amt, uint32_t seed) { addInto(v, bubbles(float(v.size()) / float(R), 18.f, 300.f, 900.f, 0.5f, seed), amt); }
void overlayCreak(std::vector<float>& v, float amt, uint32_t seed) {
    const float res[3] = {220.f, 510.f, 980.f};
    addInto(v, groan(float(v.size()) / float(R), 18.f, res, 3, 20.f, 0.5f, seed), amt);
}
void overlayFire(std::vector<float>& v, float amt, uint32_t seed) {
    Noise nz(seed), pick(seed + 1);
    Band bd;
    for (float& x : v) x += (uni(pick) > 0.995f ? pick.next() : 0.f) * amt + bd.bp(nz.next(), 200.f, 900.f) * amt * 0.3f;
}
void overlayJunk(std::vector<float>& v, float amt, uint32_t seed) {  // broken data: square chirps and crushed bursts
    Noise pick(seed);
    const float dur = float(v.size()) / float(R);
    for (int k = 0; k < int(dur * 6.f); ++k) {
        const float at = uni(pick) * dur, len = 0.02f + 0.05f * uni(pick), f = 800.f + 3000.f * uni(pick);
        Buf b(len);
        Ph a;
        for (size_t i = 0; i < b.n(); ++i) b.v[i] = a.sq(f * (1.f + 2.f * Buf::t(i) / len), 0.5f) * 0.3f;
        addInto(v, b.v, amt, at);
    }
    crush(v, 6, 12.f, 0.5f);
}
void flicker(std::vector<float>& v, float depth, float hz) {
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = Buf::t(i);
        v[i] *= 1.f - depth * (0.5f + 0.5f * std::sin(kTwoPi * hz * t) * std::sin(kTwoPi * hz * 0.37f * t + 1.f));
    }
}
void echo(std::vector<float>& v, float delay, float fb, float mix) {
    const size_t d = size_t(delay * float(R));
    v.resize(v.size() + d * 4, 0.f);
    std::vector<float> line(d, 0.f);
    size_t j = 0;
    OnePole lp;
    for (float& x : v) {
        const float e = line[j];
        line[j] = x + lp.lp(e, 2000.f) * fb;
        j = (j + 1) % d;
        x += e * mix;
    }
}
void mixVoice(std::vector<float>& v, const VSpec& s, float g) { addInto(v, voice(s), g); }

struct FV {
    const char* id;
    const char* name;
    const char* group;
    const char* desc;
};
// the review page's list, in the page's order (base, PoK, Keleres, Thunder's Edge, Discordant Stars)
const FV kVoices[] = {
    {"arborec", "Arborec", "vbase", "Slow and deep, the words swaying like branches, wood creaking under them."},
    {"letnev", "Barony of Letnev", "vbase", "A stern officer: low, clipped, every word an order."},
    {"saar", "Clan of Saar", "vbase", "Gruff scrappers talking fast over a bad line."},
    {"muaat", "Embers of Muaat", "vbase", "A roaring, burning voice with the fire crackling in it."},
    {"hacan", "Emirates of Hacan", "vbase", "A merchant's measured purr with a growl at the bottom."},
    {"sol", "Federation of Sol", "vbase", "A plain human officer on a military channel."},
    {"creuss", "Ghosts of Creuss", "vbase", "A whisper out of the void, echoing behind itself."},
    {"l1z1x", "L1Z1X Mindnet", "vbase", "A machine: flat stepped pitch, ring-modulated, wrapped in broken data."},
    {"mentak", "Mentak Coalition", "vbase", "A rough pirate's rasp, quick, over heavy static."},
    {"naalu", "Naalu Collective", "vbase", "Two voices a half-step apart, hissing their s's - the collective speaking as one."},
    {"nekro", "Nekro Virus", "vbase", "Mostly corrupted noise, a few syllables surfacing through it."},
    {"sardakk", "Sardakk N'orr", "vbase", "An insect's buzzing voice, mandibles clicking between the words."},
    {"jolnar", "Universities of Jol-Nar", "vbase", "A quick, precise, slightly nasal scholar."},
    {"winnu", "Winnu", "vbase", "Faded nobility: slow, dignified, a little mournful."},
    {"xxcha", "Xxcha Kingdom", "vbase", "Ancient and very slow: deep, breathy, patient."},
    {"yin", "Yin Brotherhood", "vbase", "Three of the same voice in unison, flat as a chant."},
    {"yssaril", "Yssaril Tribes", "vbase", "A quick sly whisper that half hides in the static."},
    {"argent", "Argent Flight", "vpok", "Higher and sharp, with bird-like glides on the words."},
    {"empyrean", "Empyrean", "vpok", "A choir from the void: several soft voices drifting apart, a long echo."},
    {"mahact", "Mahact Gene-Sorcerers", "vpok", "A cold commanding voice with a second one an octave beneath it."},
    {"naazrokha", "Naaz-Rokha Alliance", "vpok", "Two speakers trading words: a small high Naaz and a deep Rokha."},
    {"nomad", "Nomad", "vpok", "A weathered wanderer: slow, the voice wavering a little."},
    {"titans", "Titans of Ul", "vpok", "Colossal: very deep and slow, stone grinding in the throat."},
    {"vuilraith", "Vuil'raith Cabal", "vpok", "A deep, wet, gurgling growl."},
    {"keleres", "Council Keleres", "vkeleres", "Formal and measured, as if speaking in a council hall."},
    {"bastion", "Last Bastion", "vte", "A tired, rough survivor on a failing line - drops and static."},
    {"deepwrought", "Deepwrought Scholarate", "vte", "Muffled from under deep water, bubbles rising."},
    {"firmament", "The Firmament", "vte", "Hushed: a soft voice with whispers layered under it, a clock ticking."},
    {"obsidian", "The Obsidian", "vte", "The Lazaxian tongue: deep and glassy, each syllable swelling and stopping as if run backwards."},
    {"crimson", "Crimson Rebellion", "vte", "An urgent whisper torn by red static."},
    {"ralnel", "Ral Nel Consortium", "vte", "Fast, clipped and efficient, the pitch in hard steps."},
    {"ilyxum", "Augurs of Ilyxum", "vds", "A prophecy: words that swell backwards and echo ahead of themselves."},
    {"axis", "Shipwrights of Axis", "vds", "A gruff yard boss with metal ringing in the voice."},
    {"bentor", "Bentor Conglomerate", "vds", "Eager treasure hunters: quick and nasal."},
    {"celdauri", "Celdauri Trade Confederation", "vds", "A smooth, polished trader."},
    {"cheiran", "Cheiran Hordes", "vds", "A low bubbling voice, claws clacking between words."},
    {"cymiae", "Savages of Cymiae", "vds", "A buzzing hive of voices with a crushed machine edge."},
    {"dihmohn", "Dih-Mohn Flotilla", "vds", "Crosstalk: several captains on the same channel."},
    {"edyn", "Edyn Mandate", "vds", "A grave judge, slow, resounding in a hall."},
    {"florzen", "Florzen Profiteers", "vds", "A sly, wheezy, fast-talking profiteer."},
    {"freesystems", "Free Systems Compact", "vds", "Two different voices overlapping on different bands."},
    {"ghemina", "Ghemina Raiders", "vds", "The Lord and the Lady finishing each other's sentences."},
    {"ghoti", "Ghoti Wayfarers", "vds", "Long moaning vowels and bubbles, from aboard a living ship."},
    {"gledge", "Gledge Union", "vds", "A gravelly miner with the drill rumbling behind."},
    {"khrask", "L'tokk Khrask", "vds", "Living stone: a slow grinding growl."},
    {"kjalengard", "Berserkers of Kjalengard", "vds", "Shouted and overdriven, barely held back."},
    {"kollecc", "Kollecc Society", "vds", "A cloaked voice fading in and out of the line."},
    {"kolume", "Monks of Kolume", "vds", "A low throat-sung chant."},
    {"kortali", "Kortali Tribunal", "vds", "Stern and clipped, loud, a gavel's knock before it speaks."},
    {"kyro", "Kyro Sodality", "vds", "A wet, sickly rasp."},
    {"lanefir", "Lanefir Remnants", "vds", "A broken signal from a lost people: stuttering, cutting out."},
    {"lizho", "Li-Zho Dynasty", "vds", "A trickster's sing-song, quick and sly."},
    {"ltokk", "(see L'tokk Khrask)", "skip", ""},
    {"mirveda", "Mirveda Protectorate", "vds", "A cold, clean machine voice - flat, exact."},
    {"mortheus", "Glimmer of Mortheus", "vds", "A shimmering voice fading in and out, a glimmer over it."},
    {"myko", "Myko-Mentori", "vds", "The fungal network: many voices in step, wet and pulsing."},
    {"nivyn", "Nivyn Star Kings", "vds", "Regal and deep, the pitch bending down as if pulled by gravity."},
    {"nokar", "Nokar Sellships", "vds", "A rough mercenary, fast, over a noisy line."},
    {"olradin", "Olradin League", "vds", "Two calm voices agreeing on every word."},
    {"rohdhna", "Roh'Dhna Mechatronics", "vds", "An industrial machine voice with pistons behind it."},
    {"tnelis", "Tnelis Syndicate", "vds", "A spy: hushed, close and quick."},
    {"vaden", "Vaden Banking Clans", "vds", "A cold, measured banker, slightly nasal."},
    {"vaylerian", "Vaylerian Scourge", "vds", "A hunter's snarl."},
    {"veldyr", "Veldyr Sovereignty", "vds", "Corporate and polished, with a synthetic sheen."},
    {"rhodun", "Zealots of Rhodun", "vds", "A fervent chant, a crowd echoing it."},
    {"zelian", "Zelian Purifier", "vds", "A low grinding growl."},
};

VSpec vs(float f0, float range, float rate, float form, uint32_t seed) {
    VSpec s;
    s.f0 = f0, s.range = range, s.rate = rate, s.form = form, s.seed = seed;
    return s;
}
// each faction's voice, ready for the page (through the radio)
std::vector<float> factionVoice(const std::string& id) {
    const uint32_t sd = 6000u + uint32_t(std::hash<std::string>{}(id) % 9000u);
    std::vector<float> v;
    float statics = 0.025f;
    if (id == "arborec") {
        VSpec s = vs(82.f, 0.22f, 3.6f, 0.82f, sd);
        s.sway = 0.08f, s.breath = 0.2f;
        v = voice(s);
        overlayCreak(v, 0.25f, sd + 1);
    } else if (id == "letnev") {
        VSpec s = vs(98.f, 0.15f, 5.6f, 0.95f, sd);
        s.rough = 0.15f, s.cons = 0.7f;
        v = voice(s);
    } else if (id == "saar") {
        VSpec s = vs(128.f, 0.2f, 7.2f, 1.04f, sd);
        s.rough = 0.2f, s.cons = 0.6f;
        v = voice(s);
        statics = 0.05f;
    } else if (id == "muaat") {
        VSpec s = vs(88.f, 0.2f, 5.f, 0.88f, sd);
        s.rough = 0.5f, s.breath = 0.3f;
        v = voice(s);
        overlayFire(v, 0.6f, sd + 1);
        for (float& x : v) x = soft(x * 2.f);
    } else if (id == "hacan") {
        VSpec s = vs(94.f, 0.18f, 4.8f, 0.86f, sd);
        s.rough = 0.3f, s.vib = 0.012f, s.vibHz = 22.f;  // the purr
        v = voice(s);
    } else if (id == "sol") {
        VSpec s = vs(116.f, 0.18f, 6.f, 1.f, sd);
        v = voice(s);
        statics = 0.04f;
    } else if (id == "creuss") {
        VSpec s = vs(140.f, 0.2f, 5.f, 1.05f, sd);
        s.src = 1, s.cons = 0.7f;
        v = voice(s);
        echo(v, 0.21f, 0.55f, 0.6f);
    } else if (id == "l1z1x") {
        VSpec s = vs(110.f, 0.3f, 7.f, 1.f, sd);
        s.src = 3, s.steps = true, s.ring = 70.f, s.cons = 0.3f;
        v = voice(s);
        overlayJunk(v, 0.5f, sd + 1);
    } else if (id == "mentak") {
        VSpec s = vs(108.f, 0.22f, 6.6f, 0.98f, sd);
        s.rough = 0.4f, s.breath = 0.25f;
        v = voice(s);
        statics = 0.07f;
    } else if (id == "naalu") {
        VSpec s = vs(178.f, 0.16f, 5.4f, 1.1f, sd);
        s.chorus = 2, s.detune = 0.0595f, s.cons = 0.9f;
        v = voice(s);
    } else if (id == "nekro") {
        VSpec s = vs(100.f, 0.2f, 6.f, 1.f, sd);
        s.words = 2, s.src = 3, s.ring = 45.f;
        v = voice(s);
        v.resize(size_t(s.dur * float(R)), 0.f);
        Noise nz(sd + 5);
        Band bd;
        for (size_t i = 0; i < v.size(); ++i) v[i] += bd.bp(nz.next(), 800.f, 4000.f) * 0.35f * (std::sin(kTwoPi * 3.f * Buf::t(i)) > -0.2f ? 1.f : 0.2f);
        overlayJunk(v, 0.9f, sd + 1);
    } else if (id == "sardakk") {
        VSpec s = vs(150.f, 0.2f, 6.f, 1.1f, sd);
        s.src = 2, s.cons = 0.9f;
        v = voice(s);
        addInto(v, ticks(s.dur, 0.09f, 2400.f, 0.5f, sd + 1), 0.6f);
    } else if (id == "jolnar") {
        VSpec s = vs(160.f, 0.24f, 7.6f, 1.15f, sd);
        s.cons = 0.6f;
        v = voice(s);
    } else if (id == "winnu") {
        VSpec s = vs(104.f, 0.2f, 4.4f, 0.95f, sd);
        s.vib = 0.01f, s.vibHz = 5.f, s.breath = 0.15f;
        v = voice(s);
    } else if (id == "xxcha") {
        VSpec s = vs(70.f, 0.14f, 3.f, 0.76f, sd);
        s.breath = 0.4f, s.dur = 2.6f;
        v = voice(s);
    } else if (id == "yin") {
        VSpec s = vs(122.f, 0.05f, 5.f, 1.f, sd);
        s.chorus = 3, s.detune = 0.004f, s.spread = 0.008f;
        v = voice(s);
    } else if (id == "yssaril") {
        VSpec s = vs(150.f, 0.25f, 8.f, 1.08f, sd);
        s.src = 1, s.cons = 0.8f;
        v = voice(s);
        VSpec t = s;
        t.src = 0, t.breath = 0.5f;
        addInto(v, voice(t), 0.25f);
        gain(v, 0.7f);
        statics = 0.05f;
    } else if (id == "argent") {
        VSpec s = vs(205.f, 0.32f, 6.8f, 1.15f, sd);
        s.vib = 0.03f, s.vibHz = 11.f, s.cons = 0.5f;
        v = voice(s);
    } else if (id == "empyrean") {
        VSpec s = vs(150.f, 0.15f, 4.f, 1.05f, sd);
        s.chorus = 4, s.detune = 0.012f, s.spread = 0.04f, s.breath = 0.35f;
        v = voice(s);
        echo(v, 0.3f, 0.5f, 0.5f);
    } else if (id == "mahact") {
        VSpec s = vs(112.f, 0.16f, 5.f, 0.95f, sd);
        v = voice(s);
        VSpec low = s;
        low.f0 = 56.f, low.form = 0.8f;
        addInto(v, voice(low), 0.55f);
    } else if (id == "naazrokha") {
        VSpec a = vs(190.f, 0.25f, 7.f, 1.2f, sd), b = vs(78.f, 0.15f, 7.f, 0.8f, sd);
        a.words = 1, b.words = 2, b.rough = 0.3f;
        v = voice(a);
        addInto(v, voice(b), 1.f);
    } else if (id == "nomad") {
        VSpec s = vs(100.f, 0.2f, 4.4f, 0.95f, sd);
        s.vib = 0.025f, s.vibHz = 4.f, s.breath = 0.25f;
        v = voice(s);
    } else if (id == "titans") {
        VSpec s = vs(52.f, 0.12f, 3.f, 0.66f, sd);
        s.rough = 0.4f, s.dur = 2.6f;
        v = voice(s);
        addInto(v, rumble(2.6f, 150.f, 0.5f, sd + 1), 0.3f);
    } else if (id == "vuilraith") {
        VSpec s = vs(56.f, 0.2f, 4.f, 0.7f, sd);
        s.rough = 0.8f, s.breath = 0.3f;
        v = voice(s);
        overlayBubbles(v, 0.1f, sd + 1);
        for (float& x : v) x = soft(x * 2.f);
    } else if (id == "keleres") {
        VSpec s = vs(110.f, 0.16f, 5.f, 0.96f, sd);
        v = voice(s);
        echo(v, 0.12f, 0.35f, 0.35f);
    } else if (id == "bastion") {
        VSpec s = vs(104.f, 0.2f, 5.f, 0.98f, sd);
        s.rough = 0.2f, s.breath = 0.25f;
        v = voice(s);
        statics = 0.09f;
    } else if (id == "deepwrought") {
        VSpec s = vs(96.f, 0.16f, 4.6f, 0.9f, sd);
        v = voice(s);
        OnePole lp, lp2;
        for (float& x : v) x = lp2.lp(lp.lp(x, 700.f), 900.f) * 2.f;
        overlayBubbles(v, 0.5f, sd + 1);
    } else if (id == "firmament") {
        VSpec s = vs(128.f, 0.14f, 5.f, 1.f, sd);
        s.breath = 0.3f;
        v = voice(s);
        gain(v, 0.7f);
        VSpec w = s;
        w.src = 1, w.seed = sd + 3, w.chorus = 2, w.spread = 0.07f;
        addInto(v, voice(w), 0.5f);
        addInto(v, ticks(s.dur, 0.25f, 2000.f, 0.5f, sd + 1), 0.4f);
    } else if (id == "obsidian") {
        VSpec s = vs(84.f, 0.25f, 4.f, 0.85f, sd);
        s.backwards = true, s.cons = 0.3f;
        v = voice(s);
        addInto(v, crystal(207.7f, 0.4f), 0.15f, 0.2f);
        echo(v, 0.17f, 0.45f, 0.4f);
    } else if (id == "crimson") {
        VSpec s = vs(135.f, 0.28f, 6.6f, 1.02f, sd);
        s.src = 1, s.cons = 0.8f;
        v = voice(s);
        addInto(v, staticBurst(s.dur, 800.f, 4000.f, sd + 1), 0.25f);
        for (float& x : v) x = soft(x * 2.5f);
    } else if (id == "ralnel") {
        VSpec s = vs(134.f, 0.25f, 8.6f, 1.03f, sd);
        s.steps = true, s.cons = 0.7f;
        v = voice(s);
    } else if (id == "ilyxum") {
        VSpec s = vs(126.f, 0.2f, 4.6f, 1.f, sd);
        s.backwards = true;
        v = voice(s);
        echo(v, 0.25f, 0.5f, 0.55f);
    } else if (id == "axis") {
        VSpec s = vs(100.f, 0.17f, 5.6f, 0.94f, sd);
        s.rough = 0.3f;
        v = voice(s);
        BP r1, r2;
        r1.set(620.f, 30.f), r2.set(1440.f, 40.f);
        for (float& x : v) x += (r1.run(x) + r2.run(x)) * 0.5f;  // metal ringing in it
    } else if (id == "bentor") {
        VSpec s = vs(150.f, 0.28f, 7.6f, 1.18f, sd);
        v = voice(s);
    } else if (id == "celdauri") {
        VSpec s = vs(124.f, 0.2f, 6.f, 1.f, sd);
        s.breath = 0.06f, s.cons = 0.4f;
        v = voice(s);
        statics = 0.012f;
    } else if (id == "cheiran") {
        VSpec s = vs(84.f, 0.18f, 4.6f, 0.85f, sd);
        s.rough = 0.3f;
        v = voice(s);
        overlayBubbles(v, 0.3f, sd + 1);
        addInto(v, ticks(s.dur, 0.18f, 1500.f, 0.5f, sd + 2), 0.7f);
    } else if (id == "cymiae") {
        VSpec s = vs(140.f, 0.2f, 6.4f, 1.05f, sd);
        s.src = 2, s.chorus = 3, s.detune = 0.02f, s.spread = 0.015f;
        v = voice(s);
        crush(v, 4, 16.f, 0.5f);
    } else if (id == "dihmohn") {
        for (int c = 0; c < 3; ++c) {
            VSpec s = vs(100.f + 35.f * float(c), 0.2f, 6.f + float(c), 0.95f + 0.07f * float(c), sd + uint32_t(c) * 101u);
            std::vector<float> one = voice(s);
            radio(one, 300.f + 120.f * float(c), 2800.f + 300.f * float(c), 0.03f, sd + 7 + c);
            addInto(v, one, c == 0 ? 0.9f : 0.45f, 0.18f * float(c));
        }
        statics = 0.04f;
    } else if (id == "edyn") {
        VSpec s = vs(88.f, 0.14f, 3.8f, 0.86f, sd);
        v = voice(s);
        echo(v, 0.19f, 0.5f, 0.45f);
    } else if (id == "florzen") {
        VSpec s = vs(170.f, 0.28f, 8.f, 1.1f, sd);
        s.breath = 0.35f;
        v = voice(s);
    } else if (id == "freesystems") {
        VSpec a = vs(118.f, 0.18f, 6.f, 1.f, sd), b = vs(160.f, 0.22f, 6.6f, 1.12f, sd + 77);
        std::vector<float> va = voice(a), vb = voice(b);
        radio(vb, 500.f, 2600.f, 0.05f, sd + 9);
        v = va;
        addInto(v, vb, 0.55f, 0.35f);
    } else if (id == "ghemina") {
        VSpec lord = vs(92.f, 0.16f, 6.f, 0.9f, sd), lady = vs(196.f, 0.2f, 6.f, 1.15f, sd);
        lord.words = 1, lady.words = 2;
        v = voice(lord);
        addInto(v, voice(lady), 1.f);
    } else if (id == "ghoti") {
        VSpec s = vs(92.f, 0.3f, 3.4f, 0.9f, sd);
        s.vib = 0.02f, s.vibHz = 3.f;
        v = voice(s);
        overlayBubbles(v, 0.35f, sd + 1);
    } else if (id == "gledge") {
        VSpec s = vs(86.f, 0.15f, 4.8f, 0.88f, sd);
        s.rough = 0.45f;
        v = voice(s);
        addInto(v, rumble(s.dur, 260.f, 0.5f, sd + 1), 0.35f);
    } else if (id == "khrask") {
        VSpec s = vs(58.f, 0.14f, 3.f, 0.7f, sd);
        s.rough = 0.7f, s.dur = 2.6f;
        v = voice(s);
        overlayCreak(v, 0.15f, sd + 1);
    } else if (id == "kjalengard") {
        VSpec s = vs(142.f, 0.3f, 6.8f, 1.f, sd);
        s.rough = 0.6f, s.breath = 0.3f;
        v = voice(s);
        for (float& x : v) x = soft(x * 4.f);
    } else if (id == "kollecc") {
        VSpec s = vs(120.f, 0.18f, 5.4f, 1.f, sd);
        s.breath = 0.4f;
        v = voice(s);
        flicker(v, 0.85f, 1.3f);
    } else if (id == "kolume") {
        VSpec s = vs(74.f, 0.04f, 3.2f, 0.8f, sd);
        s.rough = 0.25f, s.dur = 2.6f;
        v = voice(s);
        const float f[1] = {74.f * 2.f};
        addInto(v, chant(2.6f, f, 1, 1200.f, 1800.f, 0.004f, 0.5f), 0.3f);  // the overtone sung over it
    } else if (id == "kortali") {
        v = gavel(sd + 1);
        VSpec s = vs(104.f, 0.18f, 6.f, 0.96f, sd);
        s.cons = 0.8f;
        addInto(v, voice(s), 1.2f, 0.25f);
    } else if (id == "kyro") {
        VSpec s = vs(112.f, 0.22f, 5.4f, 1.f, sd);
        s.breath = 0.55f, s.rough = 0.3f;
        v = voice(s);
        overlayBubbles(v, 0.15f, sd + 1);
    } else if (id == "lanefir") {
        VSpec s = vs(110.f, 0.2f, 5.6f, 1.f, sd);
        v = voice(s);
        Noise pick(sd + 3);  // the signal stutters: short repeats and gaps
        const size_t blk = size_t(0.06f * float(R));
        for (size_t a = 0; a + 2 * blk < v.size(); a += blk) {
            const float r = uni(pick);
            if (r < 0.15f) std::fill(v.begin() + long(a), v.begin() + long(a + blk), 0.f);
            else if (r < 0.28f) std::copy(v.begin() + long(a), v.begin() + long(a + blk), v.begin() + long(a + blk));
        }
        statics = 0.06f;
    } else if (id == "lizho") {
        VSpec s = vs(150.f, 0.38f, 7.f, 1.08f, sd);
        s.vib = 0.02f, s.vibHz = 6.f;
        v = voice(s);
    } else if (id == "mirveda") {
        VSpec s = vs(120.f, 0.15f, 6.f, 1.f, sd);
        s.src = 3, s.steps = true;
        v = voice(s);
        statics = 0.01f;
    } else if (id == "mortheus") {
        VSpec s = vs(140.f, 0.2f, 4.6f, 1.05f, sd);
        s.chorus = 2, s.detune = 0.005f, s.spread = 0.03f, s.breath = 0.3f;
        v = voice(s);
        flicker(v, 0.75f, 0.9f);
        Ph g1, g2;
        for (size_t i = 0; i < v.size(); ++i) v[i] += (g1.sine(1244.f) + g2.sine(1318.5f)) * 0.03f * (0.5f + 0.5f * std::sin(kTwoPi * 2.9f * Buf::t(i)));
    } else if (id == "myko") {
        VSpec s = vs(100.f, 0.12f, 4.6f, 0.92f, sd);
        s.chorus = 4, s.detune = 0.01f, s.spread = 0.012f, s.breath = 0.3f;
        v = voice(s);
        for (size_t i = 0; i < v.size(); ++i) v[i] *= 0.7f + 0.3f * std::sin(kTwoPi * 2.2f * Buf::t(i));
        overlayBubbles(v, 0.2f, sd + 1);
    } else if (id == "nivyn") {
        VSpec s = vs(96.f, 0.16f, 4.2f, 0.88f, sd);
        v = voice(s);
        // the gravity: the whole line sinks (a pitch drop by resampling)
        std::vector<float> w(v.size() + v.size() / 6, 0.f);
        double pos = 0.0;
        for (size_t i = 0; i < w.size() && size_t(pos) + 1 < v.size(); ++i) {
            const size_t j = size_t(pos);
            const float fr = float(pos - double(j));
            w[i] = v[j] * (1.f - fr) + v[j + 1] * fr;
            pos += 1.0 - 0.18 * double(i) / double(w.size());
        }
        v = w;
    } else if (id == "nokar") {
        VSpec s = vs(112.f, 0.22f, 7.f, 0.98f, sd);
        s.rough = 0.35f;
        v = voice(s);
        statics = 0.07f;
    } else if (id == "olradin") {
        VSpec s = vs(118.f, 0.14f, 5.2f, 1.f, sd);
        s.chorus = 2, s.detune = 0.25f, s.spread = 0.01f;
        v = voice(s);
    } else if (id == "rohdhna") {
        VSpec s = vs(96.f, 0.2f, 6.f, 0.95f, sd);
        s.src = 3, s.ring = 30.f;
        v = voice(s);
        for (int k = 0; k < 5; ++k) addInto(v, thump(70.f, 40.f, 0.03f, 0.05f), 0.4f, 0.1f + 0.45f * float(k));
        crush(v, 3, 24.f, 0.3f);
    } else if (id == "tnelis") {
        VSpec s = vs(140.f, 0.2f, 8.4f, 1.05f, sd);
        s.src = 1, s.cons = 0.6f;
        v = voice(s);
        gain(v, 0.8f);
        statics = 0.015f;
    } else if (id == "vaden") {
        VSpec s = vs(112.f, 0.13f, 5.f, 1.12f, sd);
        s.breath = 0.05f;
        v = voice(s);
        statics = 0.012f;
    } else if (id == "vaylerian") {
        VSpec s = vs(104.f, 0.3f, 6.2f, 0.95f, sd);
        s.rough = 0.65f, s.breath = 0.3f;
        v = voice(s);
        for (float& x : v) x = soft(x * 2.5f);
    } else if (id == "veldyr") {
        VSpec s = vs(118.f, 0.12f, 5.6f, 1.02f, sd);
        s.chorus = 2, s.detune = 0.003f, s.spread = 0.002f, s.breath = 0.04f;
        v = voice(s);
        statics = 0.01f;
    } else if (id == "rhodun") {
        VSpec s = vs(130.f, 0.1f, 4.4f, 1.f, sd);
        s.vib = 0.015f;
        v = voice(s);
        VSpec crowd = s;
        crowd.chorus = 4, crowd.detune = 0.03f, crowd.spread = 0.12f, crowd.seed = sd;
        addInto(v, voice(crowd), 0.4f, 0.15f);
        overlayFire(v, 0.25f, sd + 1);
    } else if (id == "zelian") {
        VSpec s = vs(60.f, 0.15f, 3.6f, 0.72f, sd);
        s.rough = 0.9f, s.breath = 0.2f;
        v = voice(s);
        for (float& x : v) x = soft(x * 3.f);
        overlayCreak(v, 0.2f, sd + 1);
    }
    return onAir(std::move(v), statics, sd + 500);
}

void writeWav(const std::string& path, const std::vector<int16_t>& d) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t bytes = uint32_t(d.size() * 2), rate = R, br = R * 2;
    const uint16_t one = 1, bits = 16, align = 2;
    std::fwrite("RIFF", 1, 4, f);
    const uint32_t riff = 36 + bytes;
    std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    const uint32_t fmt = 16;
    std::fwrite(&fmt, 4, 1, f), std::fwrite(&one, 2, 1, f), std::fwrite(&one, 2, 1, f), std::fwrite(&rate, 4, 1, f);
    std::fwrite(&br, 4, 1, f), std::fwrite(&align, 2, 1, f), std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f), std::fwrite(&bytes, 4, 1, f);
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
        {"m_turn_yours", turnYours, 0.11f, 0.2f},         {"m_round_new", roundNew, 0.12f, 0.3f},
        {"m_strategy_pick", strategyPick, 0.1f, 0.15f},   {"m_action_card", actionCard, 0.11f, 0.2f},
        {"m_tech_research", techResearch, 0.1f, 0.2f},    {"m_objective_public", objectivePublic, 0.12f, 0.3f},
        {"m_objective_secret", objectiveSecret, 0.09f, 0.25f}, {"m_planet_taken", planetTaken, 0.11f, 0.2f},
        {"m_agenda_reveal", agendaReveal, 0.11f, 0.35f},  {"m_agenda_result", agendaResult, 0.11f, 0.3f},
        {"m_deal_made", dealMade, 0.1f, 0.2f},            {"m_deal_refused", dealRefused, 0.1f, 0.15f},
        {"m_betrayal", betrayal, 0.12f, 0.3f},            {"m_kingslay", kingslay, 0.13f, 0.3f},
        {"m_dice_roll", diceRoll, 0.08f, 0.1f},           {"m_victory", victory, 0.13f, 0.35f},
        {"m_defeat", defeat, 0.11f, 0.35f},               {"m_explore", explore, 0.09f, 0.25f},
        {"m_relic_gained", relicGained, 0.1f, 0.3f},      {"m_hero_unleashed", heroUnleashed, 0.13f, 0.3f},
        {"c_incoming", codecIncoming, 0.09f, 0.08f},      {"c_open", codecOpen, 0.07f, 0.05f},
        {"c_close", codecClose, 0.07f, 0.05f},            {"c_accept", codecAccept, 0.08f, 0.08f},
        {"c_decline", codecDecline, 0.08f, 0.08f},        {"c_counter", codecCounter, 0.08f, 0.08f},
        {"c_threat", codecThreat, 0.1f, 0.12f},           {"c_alert", codecAlert, 0.11f, 0.12f},
    };
    auto report = [&](const std::string& name, const std::vector<float>& v, const std::vector<int16_t>& out) {
        bool bad = false;
        for (float x : v) bad |= !(x == x) || std::fabs(x) > 1e6f;
        double sq = 0;
        int peak = 0;
        for (int16_t x : out) sq += double(x) * x, peak = std::max(peak, std::abs(int(x)));
        std::printf("%-22s %4.2f s  peak %5d  rms %5.0f %s\n", name.c_str(), double(out.size()) / R, peak, std::sqrt(sq / double(std::max<size_t>(1, out.size()))), bad ? "BAD" : "");
    };
    for (const S& s : list) {
        std::vector<float> v = s.make();
        const std::vector<int16_t> out = grim(v, s.loud, s.hallMix);
        report(s.name, v, out);
        writeWav(dir + "/" + s.name + ".wav", out);
    }
    for (const FV& f : kVoices) {
        if (std::string(f.group) == "skip") continue;
        std::vector<float> v = factionVoice(f.id);
        const std::vector<int16_t> out = grim(v, 0.09f, 0.1f);
        report(std::string("v_") + f.id, v, out);
        writeWav(dir + "/v_" + f.id + ".wav", out);
    }
    // the page's list (id | group | name | description) for the review page
    std::FILE* l = std::fopen((dir + "/voices.tsv").c_str(), "w");
    if (l) {
        for (const FV& f : kVoices)
            if (std::string(f.group) != "skip") std::fprintf(l, "v_%s\t%s\t%s\t%s\n", f.id, f.group, f.name, f.desc);
        std::fclose(l);
    }
    return 0;
}
#endif
