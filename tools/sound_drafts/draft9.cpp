// Build: g++ -std=c++17 -O2 -Isrc -Itools/sound_drafts tools/sound_drafts/draft9.cpp -o draft9 && ./draft9 OUTDIR
// Sound drafts, round 9 (owner 2026-11-19 13:54: "We are very close!"): a new round v4 kept; Nekro: "Remove the tapering
// in the middle of the sound bite. The voice should sound as if it's continuously yelling" -> VSpec::continuous (no word
// gaps, no breath mid-line, no fall between syllables) and a steady radio (no dropouts).
#define DRAFT_NO_MAIN
#define DRAFT6_NO_MAIN
#define DRAFT7_NO_MAIN
#define DRAFT8_NO_MAIN
#include "draft8.cpp"

namespace audio {
namespace {

std::vector<float> nekroYell() {
    const uint32_t sd = 8101;
    VSpec s = vs(230.f, 0.7f, 4.6f, 1.12f, sd);
    s.src = 3, s.steps = true, s.ring = 85.f, s.cons = 0.4f, s.punch = 1.5f, s.vib = 0.035f, s.vibHz = 7.f, s.continuous = true;
    std::vector<float> v = voice(s);
    VSpec raw = s;
    raw.src = 0, raw.steps = false, raw.ring = 0.f, raw.rough = 0.9f, raw.breath = 0.4f, raw.f0 = 250.f;
    addInto(v, voice(raw), 0.7f);
    VSpec sub = s;
    sub.f0 = 115.f, sub.form = 0.9f, sub.ring = 40.f;
    addInto(v, voice(sub), 0.45f);
    shout(v, 7.f);
    Ph ring;
    for (float& x : v) x *= 0.8f + 0.2f * ring.sine(110.f);
    glitch(v, 0.1f, sd + 3);
    crush(v, 4, 12.f, 0.4f);
    overlayJunk(v, 0.35f, sd + 1);
    g_radioSteady = true;
    std::vector<float> out = onAir(std::move(v), 0.04f, sd + 500);
    g_radioSteady = false;
    return out;
}

}  // namespace
}  // namespace audio

int main(int argc, char** argv) {
    using namespace audio;
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::vector<float> v = nekroYell();
    const std::vector<int16_t> out = grim(v, 0.095f, 0.1f);
    double sq = 0;
    int peak = 0;
    for (int16_t x : out) sq += double(x) * x, peak = std::max(peak, std::abs(int(x)));
    std::printf("v_nekro_v5 %4.2f s peak %d rms %.0f\n", double(out.size()) / R, peak, std::sqrt(sq / double(out.size())));
    writeWav(dir + "/v_nekro_v5.wav", out);
    return 0;
}
