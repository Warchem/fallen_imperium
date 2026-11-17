// Sound (0.27.0, owner: "simple tech sound effects for confirm and back",
// weapons for every unit and the PDS "as if heard from a distance", and a
// placeholder music track until his own soundtrack is ready).
//
// One software mixer for every version: the sound effects are synthesised
// when the game starts (no sound files), the music is an MP3 streamed from
// music.mp3 next to the program (minimp3, public domain). The mixer is pulled
// by the platform's sound output on its own thread:
//   Miyoo   - OSS /dev/dsp (OnionOS: launch.sh preloads libpadsp.so, which
//             passes it to the system's audio server)
//   Windows - waveOut (winmm)
//   Steam Deck / Linux - SDL2 audio (SDL2 is already opened for the window)
// The headless test backend opens no output (everything is silent there).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class Settings;

namespace audio {

enum class Sfx : uint8_t {
    Confirm,       // A / left click / Space: a rising two-note blip
    Back,          // B / Esc / right click: a falling one
    Toggle,        // a setting changed (left / right on an option)
    // space combat
    FighterLaser,  // fighters: little fuchsia lasers
    Flak,          // destroyers (anti-fighter barrage) and dreadnought point defence: tracer bursts
    Railgun,       // cruisers: blue railgun
    Missile,       // carriers: slow grey missiles
    Rocket,        // flagships: red rocket salvos
    Torpedo,       // flagships: heavy torpedo
    HeavyBolt,     // dreadnoughts: the barrage of red bolts
    RedBeam,       // dreadnoughts: the big red laser
    GoldBeam,      // flagships: the gold beam
    WarSunCharge,  // war suns charging...
    WarSunBeam,    // ...and the rainbow beam
    Plasma,        // PDS: cyan plasma bolts
    Impact,        // a missile / torpedo hitting
    Explosion,     // a ship destroyed
    // ground combat
    Rifle,         // infantry: slug rifles
    LaserRifle,    // infantry / landed fighters: laser carbines
    Gatling,       // mechs: one bolt of a gatling burst
    BigExplosion,  // a mech or a structure destroyed
    // 0.28.0
    Thud,          // galaxy setup: a tile laid on the table
    GameStart,     // the "GAME START!" banner: a hit, a rising sweep and a bright chord
    // 0.44.0 (owner: "add unique sounds to the weapons being used") - the factions' ground weapons
    SporePuff,     // Arborec spores: a soft wet "pfff"
    SeedLob,       // Arborec mech: a seed pod launched ("thoonk")
    SeedPop,       // ...bursting where it lands
    Flamethrower,  // Muaat mech: a roar of fire
    GreenFlame,    // Vuil'raith horrors: an eerie whining fire
    PulseLaser,    // Nekro: "bwip-bwip"
    RedGatling,    // Nekro drone: a buzzing gatling
    BoneSpine,     // Sardakk: a bone dart, "thwip"
    PlasmaBall,    // Sardakk beetle: a deep wobbling "whumm"
    SlimeShot,     // Vuil'raith slime bullets: "splort"
    Splat,         // slime landing
    PurpleLaser,   // Titans infantry
    PurpleBeam,    // Titans mech: a long humming beam
    BigRocket,     // Titans PDS robot: rockets
    Lightning,     // Florzen: a crackling zap
    HyperBeam,     // Florzen mech: a charge, then a roaring beam
    CrystalShard,  // Khrask: a glassy ping
    BigCrystal,    // Khrask mech: a deeper chime and a crack
    Shatter,       // a crystal shard breaking
    StarLaunch,    // Mortheus: a rising bell shimmer
    DarkStar,      // Mortheus mech: the same, falling
    Supernova,     // a deep blast with a ringing shimmer
    BlackHole,     // an implosion: hiss sucked away, a deep "thoom"
    RockThrow,     // Zelian: a heavy low "whoomp"
    RockImpact,    // a rock (or a meteor) landing: a crunch
    MeteorFall,    // Zelian meteor shower: a falling whistle and a roar
    SlimeStream,   // Cheiran slimethrowers: a gurgling spray
    GasHiss,       // Ghoti: superheated gas, a steam hiss
    DroidBlaster,  // Roh'Dhna droids: "pew"
    CrystalLaser,  // Naalu crystal fighters: a glassy laser
    FirePlasma,    // Muaat infantry: plasma with a burn in it
    // 0.59.70 (owner): the title screen's reveal
    TitleGlyph,    // a Lazaxian letter appearing: a deep alien bell with a shimmer
    TitleDecode,   // a letter turning into English: a short digital chirp
    TitleReveal,   // the name complete: a slow swell and a wide chord
    Count
};

// Opens the platform's sound output (call once, after the screen is up).
// Reads the volumes from the settings ("music_vol", "sound_vol" 0-10) and
// starts music.mp3 where it stopped last time ("music_pos").
void start(Settings& s);
// Saves the music position and closes the output.
void stop(Settings& s);
bool running();
std::string status();  // one line for the system info screen

// Thread-safe; cheap when silent. pan: -1 left .. 1 right.
void play(Sfx s, float gain = 1.f, float pan = 0.f);

int musicVolume();  // 0..10
bool musicPlaying();          // music loaded, volume above 0 (the title's "now playing", 0.28.0)
void duckMusic(bool hushed);  // hush the music (glides) - the game-start transition (0.28.0)
// 0.59.70 (owner: "the title screen music doesn't start until the sound effects of the Fallen Imperium title and its
// transformation ... is complete"): holdMusic() before start() keeps the first track closed; releaseMusic() starts it
void holdMusic();
void releaseMusic(int fadeInMs, bool fromStart = false);  // fromStart (0.60.0): the track from its beginning, not where it stopped
bool musicHeld();
// The sound effects are made (they are synthesised on a thread after start()); true when there is no sound at all.
bool soundsReady();

// 0.30.0 (owner): three tracks - the title's, and two for the game that follow each other.
// Each remembers where it stopped. Switching fades the old one out, then the new one in.
enum MusicTrack : int { kTrackTitle = 0, kTrackAction = 1, kTrackInfiltration = 2, kTracks = 3 };
void musicTo(int track, int fadeOutMs = 900, int fadeInMs = 1500);
void musicFadeOut(int ms);  // fade the music to silence and wait (the game-start flood)
int musicTrack();           // the track playing (or about to), -1 none
int randomGameTrack();      // Action or Infiltration, whichever is there (random when both are)
const char* trackTitle(int track);   // "The Council Keleres"
const char* trackAlbum(int track);   // "Embers of the Imperium"
int soundVolume();
void setMusicVolume(int v);
void setSoundVolume(int v);

// For the sound outputs: fills `frames` stereo frames (interleaved 16-bit) at
// the output's rate. Called on the output's own thread.
void mix(int16_t* out, int frames);
int outputRate();

// Tests: the sound as the mixer would play it (mono, 44100 Hz).
const std::vector<int16_t>& samples(Sfx s);
const char* sfxName(Sfx s);
// Tests: run the whole mixer without an output (renders into `out`).
void startOffline(int rate, const char* musicPath, int musicVol, int soundVol);

}  // namespace audio
