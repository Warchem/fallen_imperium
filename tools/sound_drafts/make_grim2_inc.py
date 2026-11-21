#!/usr/bin/env python3
"""0.70.0: builds src/audio/grim2.inc from the sound drafts the owner approved in rounds 5-9 (tools/sound_drafts/draft5-9.cpp):
each draft's code as it is (so the game's sounds are the approved WAVs), without the drafts' main() and WAV writer, plus the
tables the game plays them from. std::hash (different in each C++ library) is replaced by the values the drafts used.
Usage: make_grim2_inc.py REPO VOICE_HASHES (the vhash output: {"id", n}, ...)"""
import re, sys
root, hashes = sys.argv[1], open(sys.argv[2]).read().strip().rstrip(",")

def body(name):
    t = open(f"{root}/tools/sound_drafts/{name}.cpp").read()
    a = t.index("namespace audio {\nnamespace {\n") + len("namespace audio {\nnamespace {\n")
    b = t.index("}  // namespace\n}  // namespace audio", a)
    s = t[a:b]
    if name == "draft5":  # the WAV writer isn't needed in the game
        i = s.index("void writeWav(")
        j = s.index("\n}\n", i) + 3
        s = s[:i] + s[j:]
    s = s.replace("std::hash<std::string>{}(id) % 9000u", "voiceHash(id)")
    assert "std::hash" not in s, name
    return f"// ------------------------------------------------------------------ {name}.cpp\n" + s

out = ["""// The approved sounds of rounds 5-9 (0.70.0, owner 2026-11-19 14:05: "Go ahead and start it for version 0.70.0") -
// included by synth.cpp inside its namespace, after grim.inc. MADE by tools/sound_drafts/make_grim2_inc.py from
// tools/sound_drafts/draft5-9.cpp: the moments that were silent, the comms codec's sounds and a voice for every faction,
// exactly as he approved them (some recipes he replaced stay in, unused). Same maths rule as synth.cpp.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
// the voice seeds the drafts used (std::hash<std::string> of the faction id with the draft machine's library, % 9000)
struct VoiceSeed { const char* id; uint32_t n; };
const VoiceSeed kVoiceSeeds[] = {""" + hashes + """};
uint32_t voiceHash(const std::string& id) {
    for (const VoiceSeed& v : kVoiceSeeds)
        if (id == v.id) return v.n;
    return 0;
}
"""]
for d in ("draft5", "draft6", "draft7", "draft8", "draft9"):
    out.append(body(d))
out.append("""
// ================================================================== what the game plays (0.70.0)
struct MomentRecipe { std::vector<float> (*make)(); float loud, hall; };
// Sfx::TurnYours .. Sfx::CodecAlert, in that order (the loudness and hall each had on the review page)
const MomentRecipe kMomentRecipes[] = {
    {turnYours3, 0.1f, 0.06f},       {roundNew4, 0.12f, 0.3f},        {strategyPick, 0.1f, 0.15f},   {actionCard, 0.11f, 0.2f},
    {techResearch2, 0.08f, 0.15f},   {objectivePublic2, 0.12f, 0.3f}, {objectiveSecret, 0.09f, 0.25f}, {planetTaken, 0.11f, 0.2f},
    {agendaReveal, 0.11f, 0.35f},    {agendaResult, 0.11f, 0.3f},     {dealMade, 0.1f, 0.2f},        {dealRefused, 0.1f, 0.15f},
    {betrayal, 0.12f, 0.3f},         {kingslay, 0.13f, 0.3f},         {diceRoll, 0.08f, 0.1f},       {victory, 0.13f, 0.35f},
    {relicGained, 0.1f, 0.3f},       {heroWarning, 0.12f, 0.25f},
    {codecIncoming, 0.09f, 0.08f},   {codecOpen, 0.07f, 0.05f},       {codecClose, 0.07f, 0.05f},    {codecAccept3, 0.08f, 0.08f},
    {codecDecline, 0.08f, 0.08f},    {codecCounter3, 0.08f, 0.08f},   {codecThreat, 0.1f, 0.12f},    {codecAlert3, 0.11f, 0.15f},
};
// the voices in kFactions order: the draft's id for each faction (its newest approved version: factionVoice3 falls back to
// factionVoice2 and factionVoice; the Nekro's is nekroYell)
const char* const kVoiceIds[65] = {
    "arborec", "creuss", "hacan", "jolnar", "l1z1x", "letnev", "mentak", "muaat", "naalu", "nekro", "saar", "sardakk", "sol",
    "winnu", "xxcha", "yin", "yssaril", "argent", "vuilraith", "empyrean", "mahact", "nomad", "naazrokha", "titans", "keleres",
    "bastion", "deepwrought", "firmament", "obsidian", "crimson", "ralnel", "ilyxum", "axis", "bentor", "celdauri", "cheiran",
    "cymiae", "dihmohn", "edyn", "florzen", "freesystems", "ghemina", "ghoti", "gledge", "khrask", "kjalengard", "kollecc",
    "kolume", "kortali", "kyro", "lanefir", "lizho", "mirveda", "mortheus", "myko", "nivyn", "nokar", "olradin", "rohdhna",
    "tnelis", "vaden", "vaylerian", "veldyr", "rhodun", "zelian"};
std::vector<int16_t> factionVoiceSound(int f) {
    if (f < 0 || f >= 65) return {};
    const std::string id = kVoiceIds[f];
    if (id == "nekro") return grim(nekroYell(), 0.095f, 0.1f);
    return grim(factionVoice3(id), 0.09f, 0.1f);
}
#pragma GCC diagnostic pop
""")
open(f"{root}/src/audio/grim2.inc", "w").write("\n".join(out))
print(sum(x.count("\n") for x in out), "lines")
