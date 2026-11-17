// The mixer (0.27.0): sound effect voices + the music stream, pulled by the
// platform's sound output (audio/output.cpp) on its own thread.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#include "audio/audio.h"
#include "audio/output.h"
#include "core/log.h"
#include "core/settings.h"
#include "platform/platform.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD  // plain C everywhere: the same sound on the Miyoo, Windows and the Deck
#include "minimp3/minimp3.h"

namespace audio {

std::vector<int16_t> synthesise(Sfx s);

namespace {

constexpr int kSfxRate = 44100;
constexpr int kVoices = 24;
constexpr int kSameMax = 4;  // at most this many of one sound at once (a fighter swarm)

struct Voice {
    const int16_t* d = nullptr;
    uint32_t len = 0;
    uint64_t pos = 0;  // 48.16 fixed point
    uint32_t step = 65536;
    int gl = 0, gr = 0;  // 0..4096
    int id = -1;
    uint32_t serial = 0;
};

struct Cmd {
    int id;
    int gl, gr;
};

struct Music {
    std::FILE* f = nullptr;
    long size = 0;
    long bufStart = 0;  // file offset of buf[0]
    mp3dec_t dec;
    uint8_t buf[16384];
    int bufLen = 0, bufOff = 0;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int frames = 0, at = 0, ch = 2, hz = 44100;
    uint32_t frac = 0;
    int fadeIn = 0, fadeLen = 1;
    int fadeOut = 0, fadeOutLen = 1;  // 0.30.0: switching tracks
    bool looped = false;              // reached the end and went round
    int failures = 0;

    bool open(const char* path, long start) {
        f = std::fopen(path, "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        size = std::ftell(f);
        if (start < 0 || start >= size - 4096) start = 0;
        std::fseek(f, start, SEEK_SET);
        bufStart = start;
        bufLen = bufOff = 0;
        mp3dec_init(&dec);
        frames = at = 0;
        looped = false;
        fadeOut = 0;
        return true;
    }
    void close() {
        if (f) std::fclose(f);
        f = nullptr;
    }
    long position() const { return bufStart + bufOff; }
    // Decodes the next frame into pcm; loops back to the start at the end.
    bool decode() {
        if (!f) return false;
        for (int guard = 0; guard < 64; ++guard) {
            if (bufLen - bufOff < 4096) {  // top the buffer up
                if (bufOff > 0) {
                    std::memmove(buf, buf + bufOff, size_t(bufLen - bufOff));
                    bufStart += bufOff;
                    bufLen -= bufOff;
                    bufOff = 0;
                }
                bufLen += int(std::fread(buf + bufLen, 1, sizeof buf - size_t(bufLen), f));
            }
            if (bufLen - bufOff <= 0) {  // the end: round again
                looped = true;
                std::fseek(f, 0, SEEK_SET);
                bufStart = 0;
                bufLen = bufOff = 0;
                mp3dec_init(&dec);
                continue;
            }
            mp3dec_frame_info_t info;
            const int n = mp3dec_decode_frame(&dec, buf + bufOff, bufLen - bufOff, pcm, &info);
            if (info.frame_bytes == 0) {  // nothing usable left in the buffer
                if (std::feof(f)) bufOff = bufLen;
                else bufOff = std::max(0, bufLen - 1);
                continue;
            }
            bufOff += info.frame_bytes;
            if (n > 0) {
                frames = n;
                ch = info.channels;
                hz = info.hz;
                at = 0;
                return true;
            }
        }
        ++failures;
        return false;
    }
};

std::vector<int16_t> g_sfx[int(Sfx::Count)];
std::atomic<bool> g_ready{false};      // the sounds are built
std::atomic<bool> g_running{false};    // an output is open
std::mutex g_cmdMx;
std::vector<Cmd> g_cmds;
Voice g_voices[kVoices];
uint32_t g_serial = 0;
int g_rate = 44100;
std::atomic<int> g_musicVol{6}, g_soundVol{7};
std::atomic<long> g_musicPos{0};
uint32_t g_lastPlay[int(Sfx::Count)] = {};
std::mutex g_musicMx;
Music g_music;
bool g_haveMusic = false;
// 0.30.0: the playlist
std::string g_trackPath[kTracks];
long g_trackPos[kTracks] = {};
int g_cur = -1;                        // track open in g_music (audio thread)
std::atomic<int> g_want{-1};           // track asked for (-2 = silence)
std::atomic<int> g_fadeOutMs{900}, g_fadeInMs{1500};
std::atomic<int> g_showTrack{-1};
std::atomic<bool> g_hold{false};  // 0.59.70: the first track waits for releaseMusic() (the title's reveal)
std::atomic<int> g_heldFirst{-1};
std::atomic<uint32_t> g_rand{0x9e3779b9u};
std::atomic<int> g_duckTarget{4096};  // 0.28.0: music hushed for the game-start transition (4096 = full)
int g_duck = 4096;
std::unique_ptr<Output> g_out;
std::thread g_builder;
std::string g_status = "sound off";

int volCurve(int v) {  // 0..10 -> 0..4096, roughly as the ear hears it
    v = std::max(0, std::min(10, v));
    return v * v * 4096 / 100;
}

void buildSounds() {
    const uint32_t t0 = monotonicMs();
    for (int i = 0; i < int(Sfx::Count); ++i) g_sfx[i] = synthesise(Sfx(i));
    g_ready = true;
    LOG("audio: %d sound effects made in %u ms", int(Sfx::Count), monotonicMs() - t0);
}

bool exists(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// The packages put each track next to the program under its own name; a build tree has them in assets/music.
std::string findTrack(int t) {
    static const char* names[kTracks] = {"The Council Keleres - Embers of the Imperium.mp3", "Action - Twilight Symposium.mp3",
                                         "Infiltration - Embers of the Imperium.mp3"};
    static const char* dev[kTracks] = {"title", "action", "infiltration"};
    std::vector<std::string> paths = {names[t]};
    if (t == kTrackAction) paths.push_back("music.mp3");
#if defined(_WIN32) || defined(TI4_SDL2)
    for (const char* pre : {"assets/music/", "../assets/music/"}) paths.push_back(std::string(pre) + dev[t] + "-pc.mp3");
#endif
    for (const char* pre : {"assets/music/", "../assets/music/"}) paths.push_back(std::string(pre) + dev[t] + "-miyoo.mp3");
    for (const std::string& p : paths)
        if (exists(p)) return p;
    return "";
}

// Audio thread, g_musicMx held: open track t where it stopped last time.
bool openTrack(int t) {
    if (g_cur >= 0 && g_music.f) g_trackPos[g_cur] = g_music.position();
    g_music.close();
    g_cur = -1;
    g_haveMusic = false;
    if (t < 0 || t >= kTracks || g_trackPath[t].empty()) return false;
    if (!g_music.open(g_trackPath[t].c_str(), g_trackPos[t])) return false;
    g_cur = t;
    g_haveMusic = true;
    g_music.fadeLen = g_music.fadeIn = std::max(1, g_rate * std::max(50, g_fadeInMs.load()) / 1000);
    return true;
}

}  // namespace

const char* sfxName(Sfx s) {
    static const char* names[] = {"confirm", "back", "toggle", "fighter_laser", "flak", "railgun", "missile", "rocket",
                                  "torpedo", "heavy_bolt", "red_beam", "gold_beam", "warsun_charge", "warsun_beam",
                                  "plasma", "impact", "explosion", "rifle", "laser_rifle", "gatling", "big_explosion",
                                  "thud", "game_start", "spore_puff", "seed_lob", "seed_pop", "flamethrower", "green_flame",
                                  "pulse_laser", "red_gatling", "bone_spine", "plasma_ball", "slime_shot", "splat", "purple_laser",
                                  "purple_beam", "big_rocket", "lightning", "hyper_beam", "crystal_shard", "big_crystal", "shatter",
                                  "star_launch", "dark_star", "supernova", "black_hole", "rock_throw", "rock_impact", "meteor_fall",
                                  "slime_stream", "gas_hiss", "droid_blaster", "crystal_laser", "fire_plasma",
                                  "title_glyph", "title_decode", "title_reveal"};
    static_assert(sizeof names / sizeof names[0] == size_t(Sfx::Count), "a name for every sound");
    return names[int(s)];
}

const std::vector<int16_t>& samples(Sfx s) {
    if (!g_ready) buildSounds();
    return g_sfx[int(s)];
}

int musicVolume() { return g_musicVol; }
int soundVolume() { return g_soundVol; }
void setMusicVolume(int v) { g_musicVol = std::max(0, std::min(10, v)); }
void setSoundVolume(int v) { g_soundVol = std::max(0, std::min(10, v)); }
bool running() { return g_running; }
int outputRate() { return g_rate; }
std::string status() { return g_status; }

void play(Sfx s, float gain, float pan) {
    if (!g_running || !g_ready || g_soundVol <= 0) return;
    const int id = int(s);
    // rapid fire (rifles, lasers, flak) is thinned out: one start per 40 ms of each sound
    const uint32_t now = monotonicMs();
    const bool rapid = s == Sfx::Rifle || s == Sfx::LaserRifle || s == Sfx::Gatling || s == Sfx::Flak || s == Sfx::FighterLaser ||
                       s == Sfx::Plasma || s == Sfx::Rocket || s == Sfx::PulseLaser || s == Sfx::RedGatling || s == Sfx::BoneSpine ||
                       s == Sfx::DroidBlaster || s == Sfx::CrystalLaser || s == Sfx::PurpleLaser || s == Sfx::CrystalShard ||
                       s == Sfx::SlimeShot || s == Sfx::FirePlasma || s == Sfx::SporePuff || s == Sfx::Shatter || s == Sfx::Splat;
    if (rapid && now - g_lastPlay[id] < 40u) return;
    g_lastPlay[id] = now;
    pan = std::max(-1.f, std::min(1.f, pan));
    gain = std::max(0.f, std::min(2.f, gain));
    const int g = int(gain * 4096.f);
    Cmd c{id, int(float(g) * (pan > 0 ? 1.f - pan * 0.7f : 1.f)), int(float(g) * (pan < 0 ? 1.f + pan * 0.7f : 1.f))};
    std::lock_guard<std::mutex> lk(g_cmdMx);
    if (g_cmds.size() < 64) g_cmds.push_back(c);
}

void mix(int16_t* out, int frames) {
    // new voices
    {
        std::lock_guard<std::mutex> lk(g_cmdMx);
        for (const Cmd& c : g_cmds) {
            const std::vector<int16_t>& d = g_sfx[c.id];
            if (d.empty()) continue;
            // a free voice; else the oldest of this sound if there are too many; else the oldest
            int same = 0, oldestSame = -1, oldest = 0, freeV = -1;
            for (int i = 0; i < kVoices; ++i) {
                const Voice& v = g_voices[i];
                if (v.id < 0) {
                    if (freeV < 0) freeV = i;
                    continue;
                }
                if (v.id == c.id) {
                    ++same;
                    if (oldestSame < 0 || v.serial < g_voices[oldestSame].serial) oldestSame = i;
                }
                if (v.serial < g_voices[oldest].serial) oldest = i;
            }
            // (0.59.70: the title's letter chimes ring on under each other - up to 10 at once)
            const int sameMax = c.id == int(Sfx::TitleGlyph) ? 10 : kSameMax;
            const int slot = same >= sameMax ? oldestSame : freeV >= 0 ? freeV : oldest;
            Voice& v = g_voices[slot];
            v.d = d.data();
            v.len = uint32_t(d.size());
            v.pos = 0;
            v.step = uint32_t(uint64_t(kSfxRate) * 65536u / uint64_t(std::max(8000, g_rate)));
            v.gl = c.gl;
            v.gr = c.gr;
            v.id = c.id;
            v.serial = ++g_serial;
        }
        g_cmds.clear();
    }
    const int sv = volCurve(g_soundVol), mv = volCurve(g_musicVol);
    static std::vector<int32_t> acc;
    acc.assign(size_t(frames) * 2, 0);
    // effects
    for (Voice& v : g_voices) {
        if (v.id < 0) continue;
        const int gl = v.gl * sv >> 12, gr = v.gr * sv >> 12;
        for (int i = 0; i < frames; ++i) {
            const uint32_t idx = uint32_t(v.pos >> 16);
            if (idx >= v.len) {
                v.id = -1;
                break;
            }
            const int s = v.d[idx];
            acc[size_t(i) * 2] += s * gl >> 12;
            acc[size_t(i) * 2 + 1] += s * gr >> 12;
            v.pos += v.step;
        }
    }
    // 0.30.0: track changes - fade the playing one out, then open the next
    {
        std::lock_guard<std::mutex> lk(g_musicMx);
        const int want = g_want.load();
        if (want != g_cur && !(want == -2 && !g_haveMusic)) {
            if (mv <= 0) {  // muted: nothing to fade, switch at once
                if (want >= 0) openTrack(want);
                else if (g_haveMusic) {
                    if (g_cur >= 0) g_trackPos[g_cur] = g_music.position();
                    g_music.close();
                    g_cur = -2;
                    g_haveMusic = false;
                }
            } else if (g_haveMusic && g_music.fadeOut == 0) {
                g_music.fadeOutLen = g_music.fadeOut = std::max(1, g_rate * std::max(50, g_fadeOutMs.load()) / 1000);
            } else if (!g_haveMusic) {
                if (want >= 0) openTrack(want);
            }
        }
        // a game track that reached its end hands over to the other one
        if (g_haveMusic && g_music.looped && (g_cur == kTrackAction || g_cur == kTrackInfiltration)) {
            g_music.looped = false;
            const int other = g_cur == kTrackAction ? kTrackInfiltration : kTrackAction;
            if (!g_trackPath[other].empty() && g_want.load() == g_cur) {
                g_trackPos[g_cur] = 0;
                g_trackPos[other] = 0;
                g_want = other;
            }
        }
    }
    // music
    if (g_haveMusic && mv > 0) {
        std::lock_guard<std::mutex> lk(g_musicMx);
        Music& m = g_music;
        const uint32_t step = uint32_t(uint64_t(m.hz) * 65536u / uint64_t(std::max(8000, g_rate)));
        for (int i = 0; i < frames; ++i) {
            while (m.at >= m.frames) {
                if (!m.decode()) {
                    g_haveMusic = false;
                    LOG("audio: the music stopped (can't decode it)");
                    break;
                }
            }
            if (!g_haveMusic) break;
            int l = m.pcm[m.at * m.ch], r = m.ch > 1 ? m.pcm[m.at * m.ch + 1] : l;
            int g = mv;
            {  // ducking glides ~0.4 s from one level to the other
                const int tgt = g_duckTarget.load(std::memory_order_relaxed);
                if (g_duck < tgt) g_duck = std::min(tgt, g_duck + 1 + 4096 / std::max(1, g_rate / 3));
                else if (g_duck > tgt) g_duck = std::max(tgt, g_duck - 1 - 4096 / std::max(1, g_rate / 3));
                g = int(int64_t(g) * g_duck >> 12);
            }
            if (m.fadeIn > 0) {
                g = int(int64_t(g) * (m.fadeLen - m.fadeIn) / m.fadeLen);
                --m.fadeIn;
            }
            if (m.fadeOut > 0) {  // 0.30.0: on the way out to another track
                g = int(int64_t(g) * m.fadeOut / m.fadeOutLen);
                if (--m.fadeOut == 0) {
                    const int want = g_want.load();
                    if (want >= 0) openTrack(want);
                    else {  // silence: keep the place, close the file
                        if (g_cur >= 0) g_trackPos[g_cur] = m.position();
                        m.close();
                        g_cur = -2;
                        g_haveMusic = false;
                    }
                    break;
                }
            }
            acc[size_t(i) * 2] += l * g >> 12;
            acc[size_t(i) * 2 + 1] += r * g >> 12;
            m.frac += step;
            m.at += int(m.frac >> 16);
            m.frac &= 0xFFFF;
        }
        if (g_haveMusic) g_musicPos = m.position();
    }
    // out, with a soft knee instead of hard clipping
    for (size_t i = 0; i < size_t(frames) * 2; ++i) {
        int x = acc[i];
        const int ax = x < 0 ? -x : x;
        if (ax > 24000) {
            const int over = std::min(ax - 24000, 40000);
            const int y = 24000 + over * 8767 / (over + 8767);
            x = x < 0 ? -y : y;
        }
        out[i] = int16_t(std::max(-32767, std::min(32767, x)));
    }
}

void duckMusic(bool hushed) { g_duckTarget.store(hushed ? 0 : 4096, std::memory_order_relaxed); }

void musicTo(int track, int fadeOutMs, int fadeInMs) {
    if (track < 0 || track >= kTracks) return;
    if (g_trackPath[track].empty()) {  // not installed: stay with what plays
        return;
    }
    g_fadeOutMs = fadeOutMs;
    g_fadeInMs = fadeInMs;
    g_want = track;
    g_showTrack = track;
}
void holdMusic() { g_hold = true; }
bool musicHeld() { return g_hold.load(); }
bool soundsReady() { return !g_running.load() || g_ready.load(); }
void releaseMusic(int fadeInMs, bool fromStart) {
    if (!g_hold.exchange(false)) return;
    const int first = g_heldFirst.load();
    if (fromStart && first >= 0) {  // (0.60.0, owner: the opening blends into the start of the title song)
        std::lock_guard<std::mutex> lk(g_musicMx);
        g_trackPos[first] = 0;
    }
    if (first >= 0) musicTo(first, 0, fadeInMs);
}
void musicFadeOut(int ms) {
    g_fadeOutMs = ms;
    g_want = -2;
}
int musicTrack() { return g_showTrack; }
int randomGameTrack() {
    const bool a = !g_trackPath[kTrackAction].empty(), b = !g_trackPath[kTrackInfiltration].empty();
    if (a && b) {
        uint32_t x = g_rand.load() ^ monotonicMs();
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        g_rand = x;
        return (x >> 9) & 1 ? kTrackInfiltration : kTrackAction;
    }
    return b ? kTrackInfiltration : kTrackAction;
}
const char* trackTitle(int track) {
    static const char* k[kTracks] = {"The Council Keleres", "Action", "Infiltration"};
    return track >= 0 && track < kTracks ? k[track] : "";
}
const char* trackAlbum(int track) {
    static const char* k[kTracks] = {"Embers of the Imperium", "Twilight Symposium", "Embers of the Imperium"};
    return track >= 0 && track < kTracks ? k[track] : "";
}
bool musicPlaying() { return g_running && (g_haveMusic || g_want.load() >= 0) && musicVolume() > 0; }

void start(Settings& s) {
    if (g_running) return;
    g_musicVol = std::max(0, std::min(10, s.getInt("music_vol", 6)));
    g_soundVol = std::max(0, std::min(10, s.getInt("sound_vol", 7)));
    g_out = makeOutput();
    int rate = 44100;
    if (!g_out || !g_out->open(rate)) {
        g_status = std::string("no sound output") + (g_out ? std::string(" (") + g_out->name() + ")" : "");
        LOG("audio: %s", g_status.c_str());
        g_out.reset();
        return;
    }
    g_rate = rate;
    {   // 0.30.0: the playlist - the title track starts (or, without it, the first game track)
        std::lock_guard<std::mutex> lk(g_musicMx);
        int found = 0;
        for (int t = 0; t < kTracks; ++t) {
            g_trackPath[t] = findTrack(t);
            g_trackPos[t] = std::atol(s.get("music_pos_" + std::to_string(t), "0").c_str());
            if (!g_trackPath[t].empty()) {
                ++found;
                LOG("audio: track %d = %s (from byte %ld)", t, g_trackPath[t].c_str(), g_trackPos[t]);
            }
        }
        if (g_trackPos[kTrackAction] == 0) g_trackPos[kTrackAction] = std::atol(s.get("music_pos", "0").c_str());  // 0.29 settings
        const int first = !g_trackPath[kTrackTitle].empty() ? kTrackTitle : !g_trackPath[kTrackAction].empty() ? kTrackAction
                                                                                                                 : kTrackInfiltration;
        g_fadeInMs = 3000;
        if (found && g_hold.load()) {  // 0.59.70: the title's reveal plays first; releaseMusic() starts it
            g_heldFirst = first;
            if (g_trackPath[first].empty()) g_heldFirst = -1;
        } else if (found && openTrack(first)) {
            g_want = first;
            g_showTrack = first;
        } else {
            LOG("audio: no music found");
        }
    }
    g_running = true;
    g_status = std::string(g_out->name()) + ", " + std::to_string(g_rate) + " Hz" + (g_haveMusic ? ", music on" : ", no music file");
    LOG("audio: %s", g_status.c_str());
    g_builder = std::thread(buildSounds);  // the effects are ready a moment later
}

void stop(Settings& s) {
    if (g_builder.joinable()) g_builder.join();
    if (!g_running) return;
    g_running = false;
    if (g_out) g_out->close();
    g_out.reset();
    {
        std::lock_guard<std::mutex> lk(g_musicMx);
        if (g_haveMusic && g_cur >= 0) g_trackPos[g_cur] = g_music.position();
        for (int t = 0; t < kTracks; ++t)
            if (!g_trackPath[t].empty()) s.set("music_pos_" + std::to_string(t), std::to_string(g_trackPos[t]));
        g_music.close();
        g_haveMusic = false;
    }
    s.setInt("music_vol", g_musicVol);
    s.setInt("sound_vol", g_soundVol);
    s.save();
    LOG("audio: stopped");
}

void startOffline(int rate, const char* musicPath, int musicVol, int soundVol) {
    if (!g_ready) buildSounds();
    g_rate = rate;
    g_musicVol = musicVol;
    g_soundVol = soundVol;
    if (musicPath) {
        std::lock_guard<std::mutex> lk(g_musicMx);
        g_trackPath[kTrackAction] = musicPath;
        g_haveMusic = g_music.open(musicPath, 0);
        g_cur = kTrackAction;
        g_want = kTrackAction;
        g_music.fadeLen = g_music.fadeIn = rate / 2;
    }
    g_running = true;
}

}  // namespace audio
