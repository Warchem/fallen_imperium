// Build: g++ -std=c++17 -O2 -Isrc tools/sound_drafts/draft10.cpp -o draft10 && ./draft10 OUTDIR   (writes the WAVs)
// Sound drafts, round 10 (owner 2026-11-24 15:57: "Let's add the voices to the voices artifact"): a voice for each Blue
// Reverie faction in the game - the Atokera, the Belkosea, the Pharad'n - in the manner of their lines (tools/lines/br.txt).
// Built on the game's own sound code: synth.cpp brings in grim.inc + grim2.inc, i.e. every helper of rounds 2-9 (VSpec,
// voice, onAir, echo, clank, ...), so these sound like the 65 voices he approved. Fixed seeds (no std::hash: the game and
// the Windows build make the same voices). No bubbles (the owner dislikes them).
#define TI4_SYNTH_NO_BRVOICES  // (0.86.0: the game's brvoices.inc is made from this file and draft11.cpp)
#include "audio/synth.cpp"

#include <cstdio>
#include <string>

namespace audio {
namespace {

// a feedback comb: the voice rung through a short metal tube (living metal)
void comb(std::vector<float>& v, float hz, float fb, float mix) {
    const size_t d = std::max<size_t>(1, size_t(float(R) / hz));
    std::vector<float> line(d, 0.f);
    size_t j = 0;
    OnePole lp;
    for (float& x : v) {
        const float y = x + lp.lp(line[j], 5000.f) * fb;
        line[j] = y;
        j = (j + 1) % d;
        x = x * (1.f - mix) + y * mix * 0.5f;
    }
}
// the loudness of v, smoothed (to make other layers follow the words)
std::vector<float> follow(const std::vector<float>& v, float hz) {
    std::vector<float> e(v.size());
    OnePole lp;
    for (size_t i = 0; i < v.size(); ++i) e[i] = lp.lp(std::fabs(v[i]), hz);
    return e;
}

// ATOKERA - living metal: slow and patient, the voice ringing through metal, a singing-bowl hum swelling with the words and a
// soft forge strike before it speaks
std::vector<float> atokeraVoice() {
    const uint32_t sd = 7301;
    VSpec s = vs(98.f, 0.12f, 4.f, 0.9f, sd);
    s.chorus = 2, s.detune = 0.004f, s.spread = 0.004f, s.breath = 0.08f, s.dur = 2.5f;
    std::vector<float> v = voice(s);
    comb(v, 196.f, 0.62f, 0.55f);  // rung through metal
    // the hum: inharmonic bell partials (a struck bowl) following the voice
    const std::vector<float> env = follow(v, 6.f);
    Ph p1, p2, p3;
    for (size_t i = 0; i < v.size(); ++i)
        v[i] += env[i] * 2.2f * (p1.sine(98.f * 2.f) * 0.5f + p2.sine(98.f * 5.43f) * 0.22f + p3.sine(98.f * 8.93f) * 0.1f);
    // a soft forge strike first: metal, long ring
    std::vector<float> out = silence(0.25f);
    const float res[4] = {392.f, 1065.f, 2128.f, 3500.f};
    addInto(out, clank(res, 4, 40.f, 0.5f, sd + 1), 0.22f);
    addInto(out, v, 1.f, 0.2f);
    return onAir(std::move(out), 0.025f, sd + 500);
}

// BELKOSEA - the Allied States: a cold, regal commander on a secured military channel - level pitch, clipped, every stressed
// syllable an order; the channel's two-tone authentication first and a superweapon's power hum under the words
std::vector<float> belkoseaVoice() {
    const uint32_t sd = 7302;
    VSpec s = vs(108.f, 0.09f, 5.2f, 0.95f, sd);
    s.cons = 0.85f, s.punch = 0.6f, s.breath = 0.05f, s.dur = 2.3f;
    std::vector<float> v = voice(s);
    VSpec low = s;  // the weight under a regal voice: a quiet octave below, in step
    low.f0 = 54.f, low.form = 0.85f, low.cons = 0.f;
    addInto(v, voice(low), 0.25f);
    for (float& x : v) x = soft(x * 2.2f) * 0.75f;  // the command speaker, a little driven
    // the superweapon's power hum: a low mains drone with a slow charge in it
    Ph h1, h2;
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = Buf::t(i);
        v[i] += (h1.saw(50.f) * 0.5f + h2.sine(100.f)) * 0.018f * (0.6f + 0.4f * clamp01(t / 2.f));
    }
    // the secured channel: two short square tones, then the voice
    std::vector<float> out = silence(0.3f);
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

// PHARAD'N - the undying empire: a solemn, very slow voice from a tomb; the dead whisper every word back a moment after it
// (the legions), dust in the air, the stone hall ringing
std::vector<float> pharadnVoice() {
    const uint32_t sd = 7303;
    VSpec s = vs(74.f, 0.1f, 3.1f, 0.82f, sd);
    s.breath = 0.22f, s.rough = 0.18f, s.dur = 2.7f;
    std::vector<float> v = voice(s);
    VSpec dead = s;  // the dead, whispering it back
    dead.src = 1, dead.chorus = 3, dead.spread = 0.05f, dead.f0 = 90.f, dead.form = 0.95f;
    std::vector<float> w = voice(dead);
    addInto(v, w, 0.35f, 0.16f);
    // dust: a faint dry hiss
    Noise nz(sd + 2);
    Band bd;
    for (float& x : v) x += bd.bp(nz.next(), 2500.f, 7000.f) * 0.012f;
    echo(v, 0.23f, 0.42f, 0.4f);  // the tomb
    return onAir(std::move(v), 0.02f, sd + 500);
}

void writeWav10(const std::string& path, const std::vector<int16_t>& d) {
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

#ifndef DRAFT10_NO_MAIN
int main(int argc, char** argv) {
    using namespace audio;
    const std::string dir = argc > 1 ? argv[1] : ".";
    struct V { const char* id; std::vector<float> (*make)(); };
    const V list[] = {{"v_atokera", atokeraVoice}, {"v_belkosea", belkoseaVoice}, {"v_pharadn", pharadnVoice}};
    for (const V& x : list) {
        std::vector<float> v = x.make();
        bool bad = false;
        for (float s : v) bad |= !(s == s) || std::fabs(s) > 1e6f;
        const std::vector<int16_t> out = grim(v, 0.09f, 0.1f);  // as every voice (factionVoiceSound)
        double sq = 0;
        int peak = 0;
        for (int16_t s : out) sq += double(s) * s, peak = std::max(peak, std::abs(int(s)));
        std::printf("%-12s %4.2f s  peak %5d  rms %5.0f %s\n", x.id, double(out.size()) / R, peak, std::sqrt(sq / double(std::max<size_t>(1, out.size()))), bad ? "BAD" : "");
        writeWav10(dir + "/" + x.id + ".wav", out);
    }
    // (for comparison: the levels of a few approved voices, as the game makes them)
    for (int f : {0, 4, 11, 20, 40}) {
        const std::vector<int16_t> out = factionVoiceSound(f);
        double sq = 0;
        int peak = 0;
        for (int16_t s : out) sq += double(s) * s, peak = std::max(peak, std::abs(int(s)));
        std::printf("(approved %-10s %4.2f s  peak %5d  rms %5.0f)\n", kVoiceIds[f], double(out.size()) / R, peak, std::sqrt(sq / double(std::max<size_t>(1, out.size()))));
    }
    return 0;
}
#endif
