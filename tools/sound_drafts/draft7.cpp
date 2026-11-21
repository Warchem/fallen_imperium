// Build: g++ -std=c++17 -O2 -Isrc -Itools/sound_drafts tools/sound_drafts/draft7.cpp -o draft7 && ./draft7 OUTDIR
// Sound drafts, round 7 (owner 2026-11-19 13:42, his marks on round 6: 15 kept). Redone from his notes:
//   your turn ("Give this a swift end"), a new round ("Let's try a different one" -> the round tracker clunking forward,
//   then a deep chord), accepted ("The two tone is throwing me off" -> one tone), counter-offer (redo -> one bell
//   rising like a question), red alert ("an ominous red alert alarm" -> a starship klaxon), and three voices: Cymiae
//   ("angrier"), L1Z1X ("more fierce and demanding"), Nekro ("as if you aren't talking to a human anymore, but an angry
//   A.I."). VSpec::punch (draft5.cpp) makes stressed syllables hit harder.
#define DRAFT_NO_MAIN
#define DRAFT6_NO_MAIN
#include "draft6.cpp"

namespace audio {
namespace {

// a stutter / glitch: short blocks repeated or played backwards here and there (a broken machine mind)
void glitch(std::vector<float>& v, float amount, uint32_t seed) {
    Noise pick(seed);
    const size_t blk = size_t(0.045f * float(R));
    for (size_t a = blk; a + 4 * blk < v.size(); a += blk) {
        const float r = uni(pick);
        if (r < amount * 0.5f) {  // repeat this block two or three times
            const int n = 2 + int(uni(pick) * 2.f);
            for (int k = 1; k < n && a + size_t(k + 1) * blk < v.size(); ++k)
                std::copy(v.begin() + long(a), v.begin() + long(a + blk), v.begin() + long(a + size_t(k) * blk));
            a += size_t(n - 1) * blk;
        } else if (r < amount) {  // backwards
            std::reverse(v.begin() + long(a), v.begin() + long(a + blk));
        }
    }
}
// shouting: the 1-3 kHz "edge" of a raised voice pushed forward, then driven
void shout(std::vector<float>& v, float drive) {
    BP edge;
    edge.set(1800.f, 1.2f);
    for (float& x : v) x = soft((x + edge.run(x) * 1.2f) * drive);
}

// ================================================================== the moments
// YOUR TURN, with a swift end: the console chime - two short bell tones a fifth apart - and done
std::vector<float> turnYours3() {
    std::vector<float> v = hiss(0.12f, 900.f, 5000.f, 0.08f, 0.03f, 7001);
    gain(v, 0.3f);
    addInto(v, fmBell(293.7f, 1.41f, 2.f, 0.07f, 0.3f), 0.6f, 0.08f);
    addInto(v, fmBell(440.f, 1.41f, 2.f, 0.1f, 0.4f), 0.65f, 0.22f);
    addInto(v, fmBell(880.f, 2.76f, 0.8f, 0.06f, 0.25f), 0.1f, 0.23f);
    Buf s(0.4f);
    Ph a;
    for (size_t i = 0; i < s.n(); ++i) s.v[i] = a.sine(73.4f) * passEnv(Buf::t(i), 0.4f, 0.3f);
    addInto(v, s.v, 0.45f, 0.06f);
    v.resize(size_t(0.62f * float(R)), 0.f);
    fadeOut(v, 0.08f);  // the swift end
    return v;
}
// A NEW ROUND, a different one: the round tracker clunks forward three notches (servos whirring between), then a deep
// chord swells with a soft gong and a low choir - the galaxy turning over
std::vector<float> roundNew3() {
    std::vector<float> v = silence(2.4f);
    for (int k = 0; k < 3; ++k) {
        const float at = 0.05f + 0.28f * float(k);
        const float res[4] = {120.f, 290.f, 610.f, 1250.f};
        addInto(v, clank(res, 4, 14.f, 0.08f, 7011 + uint32_t(k)), 0.9f, at);
        addInto(v, thump(85.f, 42.f, 0.04f, 0.09f), 0.8f, at);
        if (k < 2) {
            Buf w(0.2f);
            Ph a;
            OnePole lp;
            for (size_t i = 0; i < w.n(); ++i) {
                const float t = Buf::t(i);
                w.v[i] = lp.lp(a.sq(glide(160.f, 260.f, t, 0.18f), 0.3f), 1400.f) * passEnv(t, 0.2f, 0.5f) * 0.4f;
            }
            addInto(v, w.v, 0.5f, at + 0.06f);
        }
    }
    const float f[4] = {65.4f, 98.f, 155.6f, 196.f};
    addInto(v, brass(1.6f, f, 4, 1100.f, 0.35f, 0.9f), 0.5f, 0.82f);
    const float c[3] = {130.8f, 155.6f, 196.f};
    addInto(v, chant(1.5f, c, 3, 500.f, 1000.f, 0.006f, 0.35f), 0.3f, 0.9f);
    const float g[5] = {98.f, 151.f, 218.f, 296.f, 411.f};
    addInto(v, clank(g, 5, 60.f, 0.45f, 7021), 0.35f, 0.84f);  // the soft gong
    grit(v, 0.3f, 1.6f, 7023);
    return v;
}

// ================================================================== the codec
// ACCEPTED, one tone: a soft rush, a single warm bell, a low fifth swelling under it
std::vector<float> codecAccept3() {
    std::vector<float> v = hiss(0.16f, 900.f, 4500.f, 0.1f, 0.05f, 7031);
    gain(v, 0.3f);
    addInto(v, fmBell(392.f, 1.41f, 1.5f, 0.22f, 0.6f), 0.65f, 0.1f);
    const float p[2] = {98.f, 146.8f};
    addInto(v, brass(0.7f, p, 2, 700.f, 0.15f, 0.4f), 0.45f, 0.08f);
    radio(v, 250.f, 4200.f, 0.012f, 7033);
    fadeOut(v, 0.12f);  // the radio's static fades out too
    return v;
}
// COUNTER-OFFER, again: one bell whose pitch rises a minor third as it rings - a question - over a short low thrum
std::vector<float> codecCounter3() {
    Buf b(0.75f);
    float ph = 0.f;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i), e = env(t, 0.004f, 0.22f) * clamp01((0.75f - t) / 0.03f);
        const float f = 349.2f * semis(3.f * smooth01((t - 0.08f) / 0.35f));
        ph += kTwoPi * f / float(R);
        b.v[i] = std::sin(ph + 1.5f * std::sin(kTwoPi * f * 1.41f * t) * fexp(-t / 0.15f)) * e;
    }
    std::vector<float> v = hiss(0.12f, 1200.f, 5000.f, 0.06f, 0.03f, 7041);
    gain(v, 0.25f);
    addInto(v, b.v, 0.7f, 0.06f);
    Buf th(0.35f);
    Ph a;
    for (size_t i = 0; i < th.n(); ++i) th.v[i] = a.saw(82.4f) * passEnv(Buf::t(i), 0.35f, 0.3f);
    OnePole lp;
    for (float& x : th.v) x = lp.lp(x, 400.f);
    addInto(v, th.v, 0.4f, 0.04f);
    radio(v, 250.f, 4200.f, 0.012f, 7043);
    fadeOut(v, 0.12f);  // the radio's static fades out too
    return v;
}
// THE RED ALERT, an ominous starship alarm: a low klaxon whooping up three times, doubled a half-step sour and
// distorted, a sub drone under it, ringing through the ship
std::vector<float> codecAlert3() {
    std::vector<float> v = thump(55.f, 24.f, 0.12f, 0.3f);
    v.resize(size_t(2.3f * float(R)), 0.f);
    for (int k = 0; k < 3; ++k) {
        Buf b(0.55f);
        Ph a, c, d;
        OnePole lp;
        for (size_t i = 0; i < b.n(); ++i) {
            const float t = Buf::t(i);
            const float f = glide(190.f, 430.f, t, 0.42f);  // the whoop
            const float x = soft((a.saw(f) + c.saw(f * semis(1.f)) + 0.6f * d.sq(f * 0.5f, 0.5f)) * 2.4f);
            b.v[i] = lp.lp(x, 1600.f) * smooth01(t / 0.03f) * (t > 0.47f ? (0.55f - t) / 0.08f : 1.f);
        }
        addInto(v, b.v, 0.6f, 0.06f + 0.7f * float(k));
    }
    const float f[2] = {41.2f, 43.7f};
    addInto(v, drone(2.2f, f, 2, 200.f, 0.003f, 0.4f), 0.6f, 0.05f);
    echo(v, 0.19f, 0.35f, 0.3f);
    radio(v, 120.f, 3400.f, 0.02f, 7051);
    fadeOut(v, 0.12f);  // the radio's static fades out too
    return v;
}

// ================================================================== the voices
std::vector<float> factionVoice3(const std::string& id) {
    const uint32_t sd = 8000u + uint32_t(std::hash<std::string>{}(id) % 9000u);
    std::vector<float> v;
    float statics = 0.025f;
    if (id == "cymiae") {  // angrier: screeching, barking apes driven through a machine
        VSpec s = vs(170.f, 0.7f, 7.4f, 1.04f, sd);
        s.rough = 0.8f, s.breath = 0.35f, s.chorus = 2, s.detune = 0.04f, s.spread = 0.012f, s.cons = 0.8f, s.punch = 1.6f;
        v = voice(s);
        shout(v, 4.f);
        Ph ring;
        for (float& x : v) x *= 0.6f + 0.4f * ring.sine(52.f);
        crush(v, 4, 14.f, 0.45f);
    } else if (id == "l1z1x") {  // fierce and demanding: a commanding machine, every word punched down, no question in it
        VSpec s = vs(104.f, 0.35f, 5.8f, 0.96f, sd);
        s.src = 3, s.steps = true, s.ring = 60.f, s.cons = 0.6f, s.punch = 1.4f, s.rough = 0.3f;
        v = voice(s);
        VSpec low = s;
        low.f0 = 52.f, low.form = 0.8f;
        addInto(v, voice(low), 0.6f);  // the weight under it
        shout(v, 4.5f);
        overlayJunk(v, 0.35f, sd + 1);
    } else if (id == "nekro") {  // not a human any more: an angry A.I. - a synthetic voice glitching, stuttering, raging
        VSpec s = vs(150.f, 0.6f, 8.f, 1.08f, sd);
        s.src = 3, s.steps = true, s.ring = 90.f, s.cons = 0.5f, s.punch = 1.8f;
        v = voice(s);
        VSpec sub = s;
        sub.f0 = 75.f, sub.form = 0.85f, sub.ring = 37.f;
        addInto(v, voice(sub), 0.5f);
        shout(v, 5.f);
        glitch(v, 0.35f, sd + 3);
        crush(v, 5, 10.f, 0.55f);
        overlayJunk(v, 0.6f, sd + 1);
        statics = 0.04f;
    } else {
        return factionVoice2(id);
    }
    return onAir(std::move(v), statics, sd + 500);
}

}  // namespace
}  // namespace audio

#ifndef DRAFT7_NO_MAIN
int main(int argc, char** argv) {
    using namespace audio;
    const std::string dir = argc > 1 ? argv[1] : ".";
    struct S { const char* name; std::vector<float> (*make)(); float loud; float hallMix; };
    const S list[] = {
        {"m_turn_yours_v3", turnYours3, 0.1f, 0.06f},   {"m_round_new_v3", roundNew3, 0.12f, 0.3f},
        {"c_accept_v3", codecAccept3, 0.08f, 0.08f},    {"c_counter_v3", codecCounter3, 0.08f, 0.08f},
        {"c_alert_v3", codecAlert3, 0.11f, 0.15f},
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
    for (const char* id : {"cymiae", "l1z1x", "nekro"}) {
        std::vector<float> v = factionVoice3(id);
        const std::vector<int16_t> out = grim(v, 0.09f, 0.1f);
        report(std::string("v_") + id + "_v3", v, out);
        writeWav(dir + "/v_" + id + "_v3.wav", out);
    }
    return 0;
}
#endif
