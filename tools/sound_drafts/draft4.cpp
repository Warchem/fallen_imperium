// Build: g++ -std=c++17 -O2 -Isrc -Itools/sound_drafts tools/sound_drafts/draft4.cpp -o draft4 && ./draft4 OUTDIR
// Sound drafts, round 4 (owner 2026-11-19: the Glimmer of Mortheus - "No engines. They are living nebulas.")
#define DRAFT_NO_MAIN
#include "draft2.cpp"

namespace audio {
namespace {

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

}  // namespace
}  // namespace audio

int main(int argc, char** argv) {
    using namespace audio;
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::vector<float> v = moveMortheus2();
    const std::vector<int16_t> out = grim(v, 0.1f, 0.35f);
    double sq = 0;
    int peak = 0;
    for (int16_t x : out) sq += double(x) * x, peak = std::max(peak, std::abs(int(x)));
    std::printf("move_mortheus_v2 %4.2f s peak %d rms %.0f\n", double(out.size()) / R, peak, std::sqrt(sq / double(out.size())));
    writeWav(dir + "/move_mortheus_v2.wav", out);
    return 0;
}
