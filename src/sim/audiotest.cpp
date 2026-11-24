// `ti4 --audiotest DIR` (0.27.0): writes every sound effect as a WAV file, and
// a 20-second mix of the music with a battle's worth of effects over it, so
// the sounds can be checked (and their levels measured) without a device.
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "audio/audio.h"
#include "engine/data.h"

namespace ti {
namespace {

void writeWav(const std::string& path, const int16_t* d, size_t samples, int ch, int rate) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const uint32_t bytes = uint32_t(samples * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(uint16_t(ch));
    u32(uint32_t(rate));
    u32(uint32_t(rate * ch * 2));
    u16(uint16_t(ch * 2));
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    std::fwrite(d, 2, samples, f);
    std::fclose(f);
}

}  // namespace

int runAudioTest(const char* dir, const char* music) {
    // (0.86.0) every faction's movement sound and voice was made for the faction at its place in kFactions (the voice drafts
    // named a few factions their own way)
    auto same = [](const std::string& key, const std::string& made) {
        static const char* const kAlias[][2] = {{"ghost", "creuss"}, {"cabal", "vuilraith"}, {"naaz", "naazrokha"},
                                                {"augers", "ilyxum"}, {"mykomentori", "myko"}, {"zealots", "rhodun"}};
        if (made.empty() || key == made) return true;
        for (const auto& a : kAlias)
            if (key == a[0] && made == a[1]) return true;
        return false;
    };
    int wrong = 0;
    for (int f = 0; f < audio::kVoiceSounds; ++f) {
        const std::string key = kFactions[f].key, v = audio::voiceSoundKey(f), m = f < audio::kMoveSounds ? audio::moveSoundKey(f) : "";
        if (!same(key, v)) std::printf("FAIL voice %d is %s's, kFactions has %s\n", f, v.c_str(), key.c_str()), ++wrong;
        if (!same(key, m)) std::printf("FAIL movement sound %d is %s's, kFactions has %s\n", f, m.c_str(), key.c_str()), ++wrong;
    }
    std::printf("faction sounds: %s\n", wrong ? "WRONG PLACES" : "every voice and movement sound in its faction's place");
    for (int i = 0; i < int(audio::Sfx::Count); ++i) {
        const std::vector<int16_t>& s = audio::samples(audio::Sfx(i));
        int peak = 0;
        double sq = 0;
        for (int16_t x : s) {
            peak = std::max(peak, std::abs(int(x)));
            sq += double(x) * double(x);
        }
        std::printf("%-14s %5.2f s  peak %5d  rms %5.0f\n", audio::sfxName(audio::Sfx(i)), double(s.size()) / 44100.0, peak,
                    s.empty() ? 0.0 : std::sqrt(sq / double(s.size())));
        writeWav(std::string(dir) + "/sfx_" + audio::sfxName(audio::Sfx(i)) + ".wav", s.data(), s.size(), 1, 44100);
    }
    // a mix: music under a space battle
    const int rate = 44100, secs = 20;
    audio::startOffline(rate, music, 5, 7);
    std::vector<int16_t> out(size_t(rate) * secs * 2);
    const int block = 441;  // 10 ms
    uint32_t rng = 12345;
    auto rnd = [&]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };
    for (int b = 0; b * block < rate * secs; ++b) {
        const int ms = b * 10;
        if (ms > 2000 && ms < 17000) {  // battle from 2 s to 17 s
            if (rnd() % 7 == 0) audio::play(audio::Sfx::FighterLaser, 1.f, float(int(rnd() % 200) - 100) / 100.f);
            if (rnd() % 40 == 0) audio::play(audio::Sfx::Railgun, 1.f, -0.4f);
            if (rnd() % 60 == 0) audio::play(audio::Sfx::Missile, 1.f, 0.3f);
            if (rnd() % 25 == 0) audio::play(audio::Sfx::Flak, 1.f, 0.5f);
            if (ms % 5000 == 3500) audio::play(audio::Sfx::RedBeam, 1.f, 0.6f);
            if (ms % 5000 == 3650) audio::play(audio::Sfx::HeavyBolt, 1.f, 0.6f);
            if (ms % 4000 == 1000) audio::play(audio::Sfx::GoldBeam, 1.f, -0.6f);
            if (ms == 8000) audio::play(audio::Sfx::Explosion, 1.f, 0.2f);
            if (ms == 12000) audio::play(audio::Sfx::WarSunCharge, 1.f, -0.2f);
            if (ms == 12600) audio::play(audio::Sfx::WarSunBeam, 1.f, -0.2f);
            if (rnd() % 30 == 0) audio::play(audio::Sfx::Plasma, 1.f, 0.f);
        }
        if (ms == 500 || ms == 18500) audio::play(audio::Sfx::Confirm);
        if (ms == 1000 || ms == 19000) audio::play(audio::Sfx::Back);
        audio::mix(out.data() + size_t(b) * block * 2, block);
    }
    writeWav(std::string(dir) + "/mix.wav", out.data(), out.size(), 2, rate);
    std::printf("wrote %s/mix.wav (%d s)\n", dir, secs);
    return 0;
}

}  // namespace ti
