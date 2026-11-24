// The Blue Reverie voices (0.86.0, owner 2026-11-24 16:05-16:08: Pharad'n kept, Atokera v2 "more crystalline" and Belkosea v2
// "a higher British tone" kept) - included by synth.cpp inside its namespace, after grim2.inc. MADE by
// tools/sound_drafts/make_brvoices_inc.py from tools/sound_drafts/draft10.cpp + draft11.cpp, exactly as he approved them
// (the round-10 Atokera / Belkosea recipes he replaced stay in, unused). Same maths rule as synth.cpp.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
// ------------------------------------------------------------------ draft10.cpp

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


// ------------------------------------------------------------------ draft11.cpp

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

// ------------------------------------------------------------------ the table
// the Blue Reverie factions' places in kFactions (65..69) and their voices; none yet for a faction still to come
struct BrVoice { const char* key; std::vector<float> (*make)(); };
const BrVoice kBrVoices[] = {
    {"uydai", nullptr}, {"pharadn", pharadnVoice}, {"qhet", nullptr}, {"atokera", atokeraVoice2}, {"belkosea", belkoseaVoice2},
};
constexpr int kBrVoiceFirst = 65;
std::vector<int16_t> brVoiceSound(int f) {
    const int i = f - kBrVoiceFirst;
    if (i < 0 || i >= int(sizeof kBrVoices / sizeof kBrVoices[0]) || !kBrVoices[i].make) return {};
    return grim(kBrVoices[i].make(), 0.09f, 0.1f);  // (mastered as every voice: factionVoiceSound)
}
const char* brVoiceKey(int f) {
    const int i = f - kBrVoiceFirst;
    return i >= 0 && i < int(sizeof kBrVoices / sizeof kBrVoices[0]) ? kBrVoices[i].key : "";
}
#pragma GCC diagnostic pop
