// Build: g++ -std=c++17 -O2 -Isrc tools/sound_drafts/draft11.cpp -o draft11 && ./draft11 OUTDIR   (writes the WAVs)
// Sound drafts, round 11 (owner 2026-11-24 16:05, his marks on round 10): Pharad'n kept; Atokera redo - "Make it more
// crystalline"; Belkosea maybe - "Maybe a higher British tone?".
#define DRAFT10_NO_MAIN
#include "draft10.cpp"

namespace audio {
namespace {

// a glass ping: a few high inharmonic partials, a fast strike and a long clear ring
std::vector<float> glassPing(float f, float tau, float dur) {
    Buf b(dur);
    Ph p1, p2, p3, p4;
    for (size_t i = 0; i < b.n(); ++i) {
        const float t = Buf::t(i);
        const float e = std::min(1.f, t / 0.002f) * fexp(-t / tau);
        b.v[i] = e * (p1.sine(f) * 0.55f + p2.sine(f * 2.32f) * 0.25f * fexp(-t / (tau * 0.6f)) + p3.sine(f * 4.25f) * 0.14f * fexp(-t / (tau * 0.35f)) +
                      p4.sine(f * 6.63f) * 0.07f * fexp(-t / (tau * 0.2f)));
    }
    return b.v;
}

// ATOKERA v2 ("more crystalline"): a clearer, higher voice with a glassy shimmer - rung through two high resonances
// instead of the low metal tube, a halo of glass partials singing with the words (a wet finger round a crystal glass),
// glints sparkling over them, and a crystal ping before it speaks in place of the forge strike
std::vector<float> atokeraVoice2() {
    const uint32_t sd = 7311;
    VSpec s = vs(118.f, 0.14f, 4.f, 1.04f, sd);
    s.chorus = 3, s.detune = 0.006f, s.spread = 0.003f, s.breath = 0.05f, s.dur = 2.5f;
    std::vector<float> v = voice(s);
    comb(v, 880.f, 0.7f, 0.45f);   // glassy resonances, high
    comb(v, 1318.5f, 0.55f, 0.3f);
    // the singing glass: high pure partials (a crystal glass's ring), following the voice, with a slow beat
    const std::vector<float> env = follow(v, 5.f);
    Ph g1, g2, g3, g4;
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = Buf::t(i);
        const float beat = 0.75f + 0.25f * std::sin(kTwoPi * 3.1f * t);
        v[i] += env[i] * 1.6f * beat * (g1.sine(1760.f) * 0.32f + g2.sine(1764.f) * 0.32f + g3.sine(2637.f) * 0.18f + g4.sine(3951.f) * 0.08f);
    }
    // glints: small high pings scattered over the words
    Noise pick(sd + 3);
    const float dur = float(v.size()) / float(R);
    for (int k = 0; k < 14; ++k) {
        const float at = 0.15f + uni(pick) * (dur - 0.4f);
        const float f = 2400.f + 2600.f * uni(pick);
        addInto(v, glassPing(f, 0.08f + 0.1f * uni(pick), 0.4f), 0.05f + 0.05f * uni(pick), at);
    }
    // a crystal ping first, ringing on under the first words
    std::vector<float> out = silence(0.25f);
    addInto(out, glassPing(1568.f, 0.7f, 1.6f), 0.28f);
    addInto(out, v, 1.f, 0.2f);
    return onAir(std::move(out), 0.02f, sd + 500, 280.f, 5600.f);  // (a wider radio band: the sparkle survives it)
}

// BELKOSEA v2 ("a higher British tone"): the same cold commander on the secured channel, but higher and clipped in a
// clipped, received-pronunciation way - a lighter, brighter throat, a precise rise and fall on each phrase (not flat),
// crisp consonants, clean (no drive), no lower voice under it
std::vector<float> belkoseaVoice2() {
    const uint32_t sd = 7312;
    VSpec s = vs(138.f, 0.24f, 5.4f, 1.07f, sd);
    s.cons = 0.9f, s.punch = 0.45f, s.breath = 0.04f, s.dur = 2.3f;
    std::vector<float> v = voice(s);
    for (float& x : v) x = soft(x * 1.3f);  // only a touch of the speaker's edge
    Ph h1, h2;  // the superweapon's power hum, kept low
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = Buf::t(i);
        v[i] += (h1.saw(50.f) * 0.5f + h2.sine(100.f)) * 0.014f * (0.6f + 0.4f * clamp01(t / 2.f));
    }
    std::vector<float> out = silence(0.3f);  // the secured channel's two tones
    for (int k = 0; k < 2; ++k) {
        Buf b(0.07f);
        Ph a;
        OnePole lp;
        for (size_t i = 0; i < b.n(); ++i) b.v[i] = lp.lp(a.sq(k == 0 ? 1320.f : 990.f, 0.5f), 3000.f) * passEnv(Buf::t(i), 0.07f, 0.2f) * 0.3f;
        addInto(out, b.v, 0.7f, 0.02f + 0.1f * float(k));
    }
    addInto(out, v, 1.f, 0.27f);
    return onAir(std::move(out), 0.012f, sd + 500);
}

}  // namespace
}  // namespace audio

int main(int argc, char** argv) {
    using namespace audio;
    const std::string dir = argc > 1 ? argv[1] : ".";
    struct V { const char* id; std::vector<float> (*make)(); };
    const V list[] = {{"v_atokera_v2", atokeraVoice2}, {"v_belkosea_v2", belkoseaVoice2}};
    for (const V& x : list) {
        std::vector<float> v = x.make();
        bool bad = false;
        for (float s : v) bad |= !(s == s) || std::fabs(s) > 1e6f;
        const std::vector<int16_t> out = grim(v, 0.09f, 0.1f);
        double sq = 0;
        int peak = 0;
        for (int16_t s : out) sq += double(s) * s, peak = std::max(peak, std::abs(int(s)));
        std::printf("%-14s %4.2f s  peak %5d  rms %5.0f %s\n", x.id, double(out.size()) / R, peak, std::sqrt(sq / double(std::max<size_t>(1, out.size()))), bad ? "BAD" : "");
        writeWav10(dir + "/" + x.id + ".wav", out);
    }
    return 0;
}
