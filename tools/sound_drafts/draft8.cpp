// Build: g++ -std=c++17 -O2 -Isrc -Itools/sound_drafts tools/sound_drafts/draft8.cpp -o draft8 && ./draft8 OUTDIR
// Sound drafts, round 8 (owner 2026-11-19 13:50, his marks on round 7: 6 kept). A new round: "Clunk forward one notch
// instead of 3"; Nekro: "Screaming angry like it's yelling".
#define DRAFT_NO_MAIN
#define DRAFT6_NO_MAIN
#define DRAFT7_NO_MAIN
#include "draft7.cpp"

namespace audio {
namespace {

// A NEW ROUND: the round tracker clunks forward one notch (a servo winding it), then the deep chord, the gong, the choir
std::vector<float> roundNew4() {
    std::vector<float> v = silence(2.2f);
    Buf w(0.22f);
    Ph a;
    OnePole lp;
    for (size_t i = 0; i < w.n(); ++i) {
        const float t = Buf::t(i);
        w.v[i] = lp.lp(a.sq(glide(150.f, 250.f, t, 0.2f), 0.3f), 1400.f) * passEnv(t, 0.22f, 0.7f) * 0.4f;
    }
    addInto(v, w.v, 0.5f, 0.f);
    const float res[4] = {120.f, 290.f, 610.f, 1250.f};
    addInto(v, clank(res, 4, 14.f, 0.09f, 8011), 1.f, 0.2f);
    addInto(v, thump(85.f, 40.f, 0.05f, 0.12f), 0.9f, 0.2f);
    const float f[4] = {65.4f, 98.f, 155.6f, 196.f};
    addInto(v, brass(1.6f, f, 4, 1100.f, 0.35f, 0.9f), 0.5f, 0.3f);
    const float c[3] = {130.8f, 155.6f, 196.f};
    addInto(v, chant(1.5f, c, 3, 500.f, 1000.f, 0.006f, 0.35f), 0.3f, 0.38f);
    const float g[5] = {98.f, 151.f, 218.f, 296.f, 411.f};
    addInto(v, clank(g, 5, 60.f, 0.45f, 8021), 0.35f, 0.32f);
    grit(v, 0.3f, 1.6f, 8023);
    return v;
}

// NEKRO, screaming: the angry machine mind yelling - long raised vowels, the pitch tearing upward at the end of words,
// two throats (a synthetic one and a raw one) driven into distortion, a few glitches left in
std::vector<float> nekroScream() {
    const uint32_t sd = 8101;
    VSpec s = vs(230.f, 0.8f, 4.6f, 1.12f, sd);
    s.src = 3, s.steps = true, s.ring = 85.f, s.cons = 0.5f, s.punch = 2.2f, s.vib = 0.035f, s.vibHz = 7.f;
    std::vector<float> v = voice(s);
    VSpec raw = s;
    raw.src = 0, raw.steps = false, raw.ring = 0.f, raw.rough = 0.9f, raw.breath = 0.4f, raw.f0 = 250.f;
    addInto(v, voice(raw), 0.7f);
    VSpec sub = s;
    sub.f0 = 115.f, sub.form = 0.9f, sub.ring = 40.f;
    addInto(v, voice(sub), 0.45f);
    shout(v, 7.f);
    Ph ring;
    for (float& x : v) x *= 0.75f + 0.25f * ring.sine(110.f);
    glitch(v, 0.15f, sd + 3);
    crush(v, 4, 12.f, 0.4f);
    overlayJunk(v, 0.4f, sd + 1);
    return onAir(std::move(v), 0.04f, sd + 500);
}

}  // namespace
}  // namespace audio

#ifndef DRAFT8_NO_MAIN
int main(int argc, char** argv) {
    using namespace audio;
    const std::string dir = argc > 1 ? argv[1] : ".";
    struct S { const char* name; std::vector<float> (*make)(); float loud; float hallMix; };
    const S list[] = {{"m_round_new_v4", roundNew4, 0.12f, 0.3f}, {"v_nekro_v4", nekroScream, 0.095f, 0.1f}};
    for (const S& s : list) {
        std::vector<float> v = s.make();
        bool bad = false;
        for (float x : v) bad |= !(x == x) || std::fabs(x) > 1e6f;
        const std::vector<int16_t> out = grim(v, s.loud, s.hallMix);
        double sq = 0;
        int peak = 0;
        for (int16_t x : out) sq += double(x) * x, peak = std::max(peak, std::abs(int(x)));
        std::printf("%-18s %4.2f s  peak %5d  rms %5.0f %s\n", s.name, double(out.size()) / R, peak, std::sqrt(sq / double(out.size())), bad ? "BAD" : "");
        writeWav(dir + "/" + s.name + ".wav", out);
    }
    return 0;
}
#endif
