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
    // 0.69.0 (owner 2026-11-19: "Go ahead and add these") - the map sounds, the grim drafts he kept (audio/grim.inc).
    // Made after the others (soundsReady() doesn't wait for them); the game plays them only with "map_sounds" on.
    Siren,         // a system with another player's units is activated: a short futuristic air-raid siren (the red flash)
    LandBurn,      // ground forces land on another player's planet that nobody defends: a dirty thruster roar, a touchdown
    LandPods,      // ...on a defended one, with mechs among them: drop pods screaming down
    LandBoots,     // ...on a defended one, infantry only: static, heavy boots in step, a sour war horn
    MoveFirst,     // the factions' movement sounds: MoveFirst + faction (the kFactions index), kMoveSounds of them
    MoveLast = MoveFirst + 64,
    // 0.70.0 (owner 2026-11-19: "Go ahead and start it for version 0.70.0") - the sounds he approved in rounds 5-9
    // (audio/grim2.inc). Moments that were silent:
    TurnYours,        // your turn: a starship console's chime, two short bells a fifth apart
    RoundNew,         // a new round: the round tracker clunks one notch, a deep chord, a soft gong, a low choir
    StrategyPick,     // a strategy card taken: a card slapped down, a short low chord
    ActionCard,       // an action card played: a snap, static, a sour stab
    TechResearch,     // a technology researched: four quick data blips and a click
    ObjectivePublic,  // a public objective scored: a bell and a low minor choir
    ObjectiveSecret,  // a secret objective scored: a muffled toll, whispers, a seal cracking
    PlanetTaken,      // a planet taken from another player: a banner pole driven in, a short horn
    AgendaReveal,     // an agenda revealed: three gavel strikes, the hall murmuring
    AgendaResult,     // the ruling: a gavel and a brass chord settling on a bare fifth
    DealMade,         // a deal made: a seal stamped, two low notes that agree
    DealRefused,      // a deal refused: a sour buzz falling away
    BetrayalSting,    // a betrayal: a dissonant brass blow, heartbeats, static tearing
    Kingslay,         // Kingslay!: war horns a tritone apart, drums, a crowd
    DiceRoll,         // dice rattling across a metal tray
    Victory,          // victory: low brass, timpani, a choir rising to an open fifth, a bell
    RelicGained,      // a relic gained: something ancient waking
    HeroWarning,      // a hero unleashed: a deep horn pulsing three times with whoops - a warning
    // ...the comms codec (through its radio):
    CodecIncoming,    // a call coming in: a carrier pulsing three times
    CodecOpen,        // the channel opening: a squelch, static washing up
    CodecClose,       // the channel closed: static cut, a falling blip
    CodecAccept,      // accepted: one warm bell over a low swell
    CodecDecline,     // declined: a harsh buzz, the line cut
    CodecCounter,     // a counter-offer: one bell rising like a question
    CodecThreat,      // a threat: a slow pulse, a weapons lock beeping faster
    CodecAlert,       // the red alert (a promise broken): a low starship klaxon
    // ...and a voice for every faction (a made-up tongue over the codec): VoiceFirst + faction
    VoiceFirst,
    VoiceLast = VoiceFirst + 64,
    // 0.73.0 (owner 2026-11-19): the War Sun scene - "sound effects of the two main spaceships moving, followed by a thundering
    // wardrum-like drone that builds to a crescendo as the warsun pans into scene"
    ShipPassA,    // the first ship: comes from behind the camera with a jet roar and a turbine whine, then falls away into the distance
    ShipPassB,    // the second, heavier ship from the other side: a deep throbbing engine, a lower whine, the hull humming
    WarSunDrums,  // war drums over a dark drone, building for 8 s to a crescendo (the war sun in view), then a long tail
    // 0.73.5 (owner 2026-11-19): the Fracture appears - "a cosmic eldritch screech noise, as if the fabric of spacetime just
    // ripped apart" (the map ripples out from Styx)
    FractureTear,  // the void breathing in, a vast tear, inhuman voices screeching down, a blow far below, a dark choir
    // 0.73.8 (owner 2026-11-19): the Firmament becomes the Obsidian - "a rising crescendo of screams as if a trillion voices
    // are screaming across the galaxy ... the galaxy tear sound effect is played like the fracture version, but is severely
    // distorted. The screams end at this point, as if the galaxy went silent."
    GalaxyScreams,  // countless voices screaming, rising for kGalaxyScreamsMs, then cut off dead (no echo)
    ObsidianTear,   // the Fracture's tear, deeper and severely distorted
    Count
};
constexpr uint32_t kGalaxyScreamsMs = 3000;  // how long GalaxyScreams rises before it is cut off
constexpr uint32_t kWarSunDrumsPeakMs = 8000;  // when WarSunDrums reaches its crescendo
constexpr int kMoveSounds = int(Sfx::MoveLast) - int(Sfx::MoveFirst) + 1;  // one per faction (65)
// The movement sound of faction f (a kFactions index); Count when there is none.
inline Sfx moveSound(int f) { return f >= 0 && f < kMoveSounds ? Sfx(int(Sfx::MoveFirst) + f) : Sfx::Count; }
const char* moveSoundKey(int f);  // the faction key its recipe was made for (the tests check it against kFactions)
// (0.70.0) faction f's voice (Count when there is none)
inline Sfx voiceSound(int f) { return f >= 0 && f < kMoveSounds ? Sfx(int(Sfx::VoiceFirst) + f) : Sfx::Count; }

// Opens the platform's sound output (call once, after the screen is up).
// Reads the volumes from the settings ("music_vol", "sound_vol" 0-10) and
// starts music.mp3 where it stopped last time ("music_pos").
void start(Settings& s);
// (0.68.0) the music player's choices and My Music's songs only - start() does this too; the headless tests call it
void loadLibrary(Settings& s);
// Saves the music position and closes the output.
void stop(Settings& s);
bool running();
std::string status();  // one line for the system info screen

// Thread-safe; cheap when silent. pan: -1 left .. 1 right.
void play(Sfx s, float gain = 1.f, float pan = 0.f);
// (0.69.0) the map sounds are made (they come after the others)
bool soundReady(Sfx s);

int musicVolume();  // 0..10
bool musicPlaying();          // music loaded, volume above 0 (the title's "now playing", 0.28.0)
void duckMusic(bool hushed);  // hush the music (glides) - the game-start transition (0.28.0)
void duckMusicTo(float level);  // (0.73.0) the music down to this share of its volume (1 = full), gliding - the War Sun scene
// (0.73.0) every copy of this sound playing fades out over fadeMs (the War Sun scene skipped)
void stopSound(Sfx s, int fadeMs = 250);
// 0.59.70 (owner: "the title screen music doesn't start until the sound effects of the Fallen Imperium title and its
// transformation ... is complete"): holdMusic() before start() keeps the first track closed; releaseMusic() starts it
void holdMusic();
void releaseMusic(int fadeInMs, bool fromStart = false);  // fromStart (0.60.0): the track from its beginning, not where it stopped
bool musicHeld();
// The sound effects are made (they are synthesised on a thread after start()); true when there is no sound at all.
bool soundsReady();

// 0.30.0 (owner): three tracks - the title's, and two for the game that follow each other.
// Each remembers where it stopped. Switching fades the old one out, then the new one in.
// 0.68.0 (owner: "a music player for the game ... where the player can put their own music in a folder so that they can
// play sound tracks that they want. The default music will be what we have currently. The title screen music should stay
// unchanged"): after the game's own tracks (kTracks of them, 0-2) come the MP3 files of the "My Music" folder next to
// the game. The title screen keeps its track; a game plays the playlist - the game soundtrack (the default), My Music
// or both, in order or shuffled, without the songs ticked off - and goes on to the next track when one ends. Chosen in
// the music player under Options on the title screen only (owner: "not accessible when viewing and playing on the game
// map"). The choices are kept in the settings ("music_mode", "music_shuffle", "music_off").
enum MusicTrack : int { kTrackTitle = 0, kTrackAction = 1, kTrackInfiltration = 2, kTracks = 3 };
enum MusicMode : int { kPlayGame = 0, kPlayMine = 1, kPlayBoth = 2 };
void musicTo(int track, int fadeOutMs = 900, int fadeInMs = 1500);
void musicFadeOut(int ms);  // fade the music to silence and wait (the game-start flood)
int musicTrack();           // the track playing (or about to), -1 none
int gameMusicTrack();       // what a game plays first: from the playlist (the game soundtrack: Action or Infiltration at random)
int trackCount();                    // the game's own + My Music
bool trackAvailable(int track);      // its file is there
bool trackIsMine(int track);         // a song from My Music
std::string trackTitle(int track);   // "The Council Keleres" / a song's title (its tag, else its file name)
std::string trackAlbum(int track);   // "Embers of the Imperium" / a song's artist ("" when unknown)
std::string trackFile(int track);    // a song's file name in My Music ("" for the game's own)
bool trackOn(int track);             // not ticked off in the music player
void setTrackOn(int track, bool on);
int musicMode();
void setMusicMode(int mode);
bool musicShuffle();
void setMusicShuffle(bool on);
std::vector<int> musicPlaylist();    // the tracks a game plays now (the game soundtrack when nothing else is left)
float musicProgress();               // how far into the track playing, 0..1 (-1: none)
std::string musicFolder();           // My Music's full path (made when missing)
int rescanMusic();                   // look in My Music again (new or removed files); returns the songs found
int musicSkippedFiles();  // (0.68.0) FLAC / M4A / WAV ... files found in My Music (only MP3s play)
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
