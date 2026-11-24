#!/usr/bin/env python3
"""0.86.0: builds src/audio/brvoices.inc from the Blue Reverie voices the owner approved in rounds 10-11
(tools/sound_drafts/draft10.cpp, draft11.cpp - 2026-11-24: Pharad'n round 10, Atokera v2 and Belkosea v2 round 11): each
draft's code as it is (so the game's voices are the approved WAVs), without the drafts' main() and WAV writer, plus the
table the game plays them from. Included by synth.cpp after grim2.inc (it uses that file's helpers).
Usage: make_brvoices_inc.py REPO"""
import sys

root = sys.argv[1]


def body(name):
    t = open(f"{root}/tools/sound_drafts/{name}.cpp").read()
    a = t.index("namespace audio {\nnamespace {\n") + len("namespace audio {\nnamespace {\n")
    b = t.index("}  // namespace\n}  // namespace audio", a)
    s = t[a:b]
    if "void writeWav10(" in s:
        i = s.index("void writeWav10(")
        j = s.index("\n}\n", i) + 3
        s = s[:i] + s[j:]
    assert "std::hash" not in s, name
    return f"// ------------------------------------------------------------------ {name}.cpp\n" + s


out = ["""// The Blue Reverie voices (0.86.0, owner 2026-11-24 16:05-16:08: Pharad'n kept, Atokera v2 "more crystalline" and Belkosea v2
// "a higher British tone" kept) - included by synth.cpp inside its namespace, after grim2.inc. MADE by
// tools/sound_drafts/make_brvoices_inc.py from tools/sound_drafts/draft10.cpp + draft11.cpp, exactly as he approved them
// (the round-10 Atokera / Belkosea recipes he replaced stay in, unused). Same maths rule as synth.cpp.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
"""]
for d in ("draft10", "draft11"):
    out.append(body(d))
out.append("""// ------------------------------------------------------------------ the table
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
""")
open(f"{root}/src/audio/brvoices.inc", "w").write("".join(out))
print("wrote src/audio/brvoices.inc")
