#include "ui/retro.h"
#include "ui/voidfall.h"
#include "ui/space_battle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "audio/audio.h"
#include "engine/query.h"
#include "gfx/font.h"
#include "ui/battle_fx.h"
#include "gfx/image.h"
#include "ui/icons.h"
#include "ui/pixel_ships.h"
#include "ui/theme.h"
#include "ui/pcolors.h"

using namespace ti;

namespace battle {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kSqrt3 = 1.7320508f;

uint32_t lighter(uint32_t c, float k) { return mix(c, rgb(255, 255, 255), int(255.f * std::max(0.f, std::min(1.f, k)))); }
uint32_t darker(uint32_t c, float k) { return mix(c, rgb(0, 0, 0), int(255.f * std::max(0.f, std::min(1.f, k)))); }

uint32_t hue(float h) {  // 0..1 round the colour wheel, full saturation
    h -= std::floor(h);
    const float r = std::fabs(h * 6.f - 3.f) - 1.f, g = 2.f - std::fabs(h * 6.f - 2.f), b = 2.f - std::fabs(h * 6.f - 4.f);
    auto ch = [](float v) { return int(255.f * std::max(0.f, std::min(1.f, v))); };
    return rgb(ch(r), ch(g), ch(b));
}

struct Rng {
    uint32_t s = 1;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float f() { return float(next() % 100000u) / 100000.f; }
    float r(float lo, float hi) { return lo + (hi - lo) * f(); }
    int i(int n) { return n <= 0 ? 0 : int(next() % uint32_t(n)); }
};

const char* artName(UnitType t) {
    switch (t) {
        case UnitType::Carrier: return "unit/carrier";
        case UnitType::Cruiser: return "unit/cruiser";
        case UnitType::Destroyer: return "unit/destroyer";
        case UnitType::Dreadnought: return "unit/dreadnought";
        case UnitType::Fighter: return "unit/fighter";
        case UnitType::Flagship:
        case UnitType::Flagship2: return "unit/flagship";
        case UnitType::WarSun: return "unit/warsun";
        case UnitType::Infantry: return "unit/infantry";
        case UnitType::Mech: return "unit/mech";
        case UnitType::PDS: return "unit/pds";
        case UnitType::SpaceDock: return "unit/spacedock";
        default: return "";
    }
}

// Lengths relative to each other (the owner's unit reference card).
float relLen(UnitType t) {
    switch (t) {
        case UnitType::Flagship: return 1.4f;
        case UnitType::Dreadnought: return 1.3f;
        case UnitType::Carrier: return 1.12f;
        case UnitType::Cruiser: return 1.1f;
        case UnitType::Destroyer: return 0.86f;
        case UnitType::Fighter: return 0.4f;
        default: return 0.8f;
    }
}

float easeOut(float k) {
    k = std::max(0.f, std::min(1.f, k));
    return 1.f - (1.f - k) * (1.f - k) * (1.f - k);
}

enum class Back { Stars, Horizon, Planets, RedGiant, Rift, Nexus };

// projectiles
enum ShotKind { Laser, Rail, Missile, Rocket, Torpedo, Bolt, Tracer, RedBolt, Gauss };
// beams
enum BeamKind { RedBeam, GoldBeam, Rainbow };
// particles
enum PartKind { Spark, Smoke, Chunk, Ember, Puff };
// explosions
enum BoomKind { Fire, Shaded, Big, Flash, Muzzle };

// 0.43.2 (owner): the Fracture's planets and Thunder's Edge have their own looks in the backdrop
// (0 none, 1 Cocytus, 2 Styx, 3 Lethe, 4 Phlegethon, 5 Thunder's Edge)
int planetSpecial(const char* key) {
    static const char* keys[5] = {"cocytus", "styx", "lethe", "phlegethon", "thundersedge"};
    for (int i = 0; i < 5; ++i)
        if (!std::strcmp(key, keys[i])) return i + 1;
    return 0;
}
float hashF(uint32_t h, int k) {  // 0..1 from a hash, k-th draw
    h ^= uint32_t(k) * 0x9E3779B9u;
    h *= 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return float(h % 10000u) / 10000.f;
}

}  // namespace

struct SpaceBattle::Impl {
    struct Ship {
        int side = 0;
        UnitType t = UnitType::Fighter;
        float x = 0, y = 0, vx = 0, vy = 0;  // in hex radii from the centre
        float hx = 0, hy = 0, tx = 0, ty = 0;
        float ex = 0, ey = 0;  // where it flies in from
        float len = 0.2f, phase = 0;
        uint32_t enterAt = 0, enterDur = 1200, nextFire = 0, nextMove = 0, chargeAt = 0;
        int state = 0;  // 0 fighting, 1 dying, 2 gone, 3 leaving
        uint32_t dieAt = 0;
        int stage = 0, cyc = 0, hp = 1, ordinal = 0;
        bool damaged = false, moon = false, faceLeft = false;
        bool second = false;   // (0.52.0) a second flagship (the Ghemina's Lady): drawn and flown as a flagship
        float sx = 0, sy = 0;  // where the smoke comes out (relative to the centre, radii)
        uint32_t nextSmoke = 0;
        int lastCycle = 0;
        uint32_t shieldAt = 0;  // Retrowave (0.49.0): when it last took a hit it could sustain (its shield flares)
    };
    struct Shot {
        int kind = Laser;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        uint32_t born = 0, arrive = 0;
        uint32_t col = 0;
        float wob = 0;
        bool hitFx = true;
        bool heard = false;  // its sound has started (0.27.0)
        uint32_t nextTrail = 0;
    };
    struct Beam {
        int kind = RedBeam;
        int src = -1, dst = -1;  // ship indices (positions follow them)
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        uint32_t born = 0, life = 400;
        uint32_t col = 0;
    };
    struct Part {
        int kind = Spark;
        float x = 0, y = 0, vx = 0, vy = 0;
        uint32_t born = 0, life = 500;  // life 0: stays to the end (debris)
        float size = 1, grow = 0, rot = 0, vr = 0;
        uint32_t col = 0;
        int a0 = 255;
    };
    struct Boom {
        int kind = Fire;
        float x = 0, y = 0, r = 0.05f;
        uint32_t born = 0, life = 500;
    };
    struct Piece {
        UnitType t = UnitType::Carrier;
        bool faceLeft = false;
        float ox = 0, oy = 0, dx = 0, dy = 0, vx = 0, vy = 0;
        float u0 = -0.5f, u1 = 0.5f, v0 = -0.5f, v1 = 0.5f;
        int w = 10, h = 10;
        uint32_t col = 0;
        uint32_t born = 0;
    };
    struct Ring {
        float x = 0, y = 0, maxR = 1, flat = 0.2f;
        uint32_t born = 0, life = 1000;
        uint32_t col = 0;
    };
    struct Struct {
        UnitType t = UnitType::PDS;
        int owner = -1, side = -1;
        float x = 0, y = 0, size = 0.1f;
        uint32_t nextFire = 0;
        bool gauss = false;  // 0.43.0 (owner D128): a Mirveda Gauss Cannon in the space area - a charged slug
    };
    struct RollShow {
        int side = 0;
        std::vector<int8_t> dice, need;
        int hits = 0;
        dice::Timing tm;
    };
    struct Pending {
        uint32_t at = 0;
        int kind = 0;  // 0 damaged, 1 destroyed
        int side = 0;
        UnitType t = UnitType::Fighter;
        int count = 1;
    };
    struct BgPlanet {
        int def = -1, owner = -1;
        float x = 0, y = 0, r = 0.2f;
        bool shield = false, home = false;
    };

    int pl[2] = {-1, -1};
    uint32_t col[2] = {0, 0};
    int pci[2] = {-1, -1};  // 0.62.0: the sides' colour indices (their finish)
    int fac[2] = {-1, -1};
    std::string flagName[2], flagName2[2];  // (0.52.0: flagName2 - the second flagship, the Ghemina's Lady)
    float speed = 1.f;
    uint32_t t0 = 0, last = 0;
    Rng rng;
    Back back = Back::Stars;
    int sys = -1, sysDef = -1;
    bool nexusOn = false;  // (the Voidfall sky of the wormhole nexus: open or closed)
    uint8_t anomalies = 0, holes = 0;
    std::vector<BgPlanet> planets;
    // 0.43.2 (owner: "update the ground and space combat backgrounds in the Fracture ... one unique to Thunder's Edge")
    int fracNo = 0;                       // a Fracture system: fracture1..7
    bool teSys = false;                   // Thunder's Edge's system
    std::vector<battlefx::Crack> cracks;  // broken space (Simplistic / Retrowave; Voidfall paints its own)
    float tearX = 0, tearY = 0, tearLen = 0;  // an egress: the tear in space
    voidfall::SkyPulse vfPulse;           // Voidfall: the light running down the painted cracks
    float horizon = 0.55f;          // top of the horizon planet (radii below the centre)
    float hpx = 0, hpy = 0, hpr = 2.6f;  // the horizon body (centre, radius)
    float bandTop = -0.6f, bandBot = 0.3f;
    float base = 0.24f;
    int fightersActual[2] = {0, 0};

    std::vector<Ship> ships;
    std::vector<Shot> shots;
    std::vector<Beam> beams;
    std::vector<Part> parts;
    std::vector<Boom> booms;
    std::vector<Piece> pieces;
    std::vector<Ring> rings;
    std::vector<Struct> structs;
    std::vector<RollShow> rolls;
    std::vector<Pending> pending;
    uint32_t rippleAt = 0, rippleUntil = 0;
    bool ended = false;
    int winner = -1;
    std::string result;

    // geometry of the last draw
    Rect area{0, 0, 0, 0};
    float R = 100, cx = 0, cy = 0;
    std::unique_ptr<Canvas> bg;
    Rect bgArea{0, 0, 0, 0};
    pxship::Grid grid;  // Retrowave (0.49.0): the fight's pixel grid - the ships are pixel art on it (f 0: pictures)
    std::vector<uint32_t> saved;

    float X(float x) const { return cx + x * R; }
    float Y(float y) const { return cy + y * R; }
    int sideOf(int p) const { return p == pl[0] ? 0 : p == pl[1] ? 1 : -1; }
    float dirOf(int side) const { return side == 0 ? 1.f : -1.f; }
    bool alive(const Ship& s) const { return s.state == 0; }

    // ---------------------------------------------------------- setup
    void addShips(const Game& g, int side, const Force& f, uint32_t now) {
        const float dir = dirOf(side);
        auto group = [&](UnitType t, float hx, int count, int dmg) {
            for (int i = 0; i < count; ++i) {
                Ship s;
                s.side = side;
                s.t = t;
                s.len = relLen(t) * base;
                s.hp = unitSustains(g, pl[side], t) ? 2 : 1;
                s.damaged = i < dmg && s.hp > 1;
                const float k = count > 1 ? (float(i) + 0.5f) / float(count) : 0.5f;
                s.hy = bandTop + 0.08f + (bandBot - bandTop - 0.16f) * k + rng.r(-0.02f, 0.02f);
                s.hx = -dir * (hx + ((i % 2) ? 0.07f : 0.f)) + rng.r(-0.02f, 0.02f);
                s.x = s.ex = s.hx - dir * (0.95f + rng.r(0.f, 0.25f));
                s.y = s.ey = s.hy + rng.r(-0.08f, 0.08f);
                s.tx = s.hx;
                s.ty = s.hy;
                s.phase = rng.r(0, 2 * kPi);
                s.faceLeft = side == 1;
                s.ordinal = i + 1;
                if (s.damaged) {
                    s.sx = rng.r(-0.3f, 0.3f) * s.len;
                    s.sy = rng.r(-0.1f, 0.1f) * s.len;
                }
                ships.push_back(s);
            }
        };
        const int dd = std::min<int>(f.dmg[int(UnitType::Dreadnought)], f.count(UnitType::Dreadnought));
        const int df = std::min<int>(f.dmg[int(UnitType::Flagship)], f.count(UnitType::Flagship));
        group(UnitType::Carrier, 0.74f, f.count(UnitType::Carrier), 0);
        if (!f.count(UnitType::Flagship2)) {
            group(UnitType::Flagship, 0.6f, f.count(UnitType::Flagship), df);
        } else {  // (0.52.0) the Ghemina's Lady and Lord: one column, one above the other, each with its own damage
            const int nl = f.count(UnitType::Flagship), nd = f.count(UnitType::Flagship2);
            const int dl = std::min<int>(f.dmg[int(UnitType::Flagship2)], nd);
            const size_t first = ships.size();
            group(UnitType::Flagship, 0.6f, nl + nd, 0);
            for (size_t i = first; i < ships.size(); ++i) {
                Ship& sh = ships[i];
                const int k = int(i - first);
                sh.second = k >= nl;
                sh.damaged = sh.hp > 1 && (sh.second ? k - nl < dl : k < df);
                if (sh.damaged) {
                    sh.sx = rng.r(-0.3f, 0.3f) * sh.len;
                    sh.sy = rng.r(-0.1f, 0.1f) * sh.len;
                }
            }
        }
        group(UnitType::Dreadnought, 0.47f, f.count(UnitType::Dreadnought), dd);
        group(UnitType::Cruiser, 0.27f, f.count(UnitType::Cruiser), 0);
        group(UnitType::Destroyer, 0.36f, f.count(UnitType::Destroyer), 0);
        // fighters: at most 10 on screen
        fightersActual[side] = f.count(UnitType::Fighter);
        for (int i = 0; i < std::min(10, f.count(UnitType::Fighter)); ++i) {
            Ship s;
            s.side = side;
            s.t = UnitType::Fighter;
            s.len = relLen(UnitType::Fighter) * 0.24f;
            s.hx = -dir * rng.r(0.15f, 0.6f);
            s.hy = rng.r(bandTop, bandBot);
            s.x = s.ex = s.hx - dir * rng.r(1.f, 1.3f);
            s.y = s.ey = s.hy;
            s.tx = s.hx;
            s.ty = s.hy;
            s.faceLeft = side == 1;
            s.phase = rng.r(0, 2 * kPi);
            ships.push_back(s);
        }
        // war suns: moons in the background
        const int ws = f.count(UnitType::WarSun);
        const int wsd = std::min<int>(f.dmg[int(UnitType::WarSun)], ws);
        for (int i = 0; i < ws; ++i) {
            Ship s;
            s.side = side;
            s.t = UnitType::WarSun;
            s.moon = true;
            s.hp = unitSustains(g, pl[side], UnitType::WarSun) ? 2 : 1;
            s.damaged = i < wsd;
            s.len = ws == 1 ? 0.58f : std::max(0.34f, 0.5f - 0.08f * float(i));
            s.hx = -dir * (0.5f - 0.3f * float(i % 2) + 0.08f * float(i / 2));
            s.hy = -0.42f + 0.16f * float(i % 2) + 0.05f * float(i / 2);
            s.x = s.ex = s.hx;
            s.y = s.ey = s.hy;
            s.phase = rng.r(0, 2 * kPi);
            s.faceLeft = side == 1;
            ships.push_back(s);
        }
        (void)now;
    }

    void setup(const Game& g, int system, const Force& a, const Force& b, uint32_t now) {
        sys = system;
        const SystemState& s = g.systems[size_t(system)];
        const SystemDef& d = s.d();
        sysDef = s.def;
        nexusOn = s.nexusActive;
        anomalies = d.anomalies;
        fracNo = s.fracture ? std::max(0, std::min(7, std::atoi(d.key + 8))) : 0;
        teSys = system == g.teSystem;
        holes = wormholesOf(g, system);
        planets.clear();
        int alivePlanets = 0;
        for (int16_t x : s.planets)
            if (!g.planets[size_t(x)].destroyed) ++alivePlanets;
        const bool mecatol = system == g.mecatol();
        if (d.anomalies & kSupernova) back = Back::RedGiant;
        else if (d.anomalies & kRift) back = Back::Rift;
        else if (system == g.nexus) back = Back::Nexus;
        else if (alivePlanets == 1 || mecatol) back = Back::Horizon;
        else if (alivePlanets >= 2) back = Back::Planets;
        else back = Back::Stars;
        if (teSys && alivePlanets == 1) back = Back::Planets;  // 0.43.2: the storm world in the storm's eye, whatever the tile
        bandTop = -0.62f;
        bandBot = back == Back::Stars ? 0.5f : back == Back::Planets ? 0.36f : 0.3f;
        int k = 0;
        for (int16_t x : s.planets) {
            const PlanetState& ps = g.planets[size_t(x)];
            if (ps.destroyed) continue;
            BgPlanet bp;
            bp.def = ps.def;
            bp.owner = ps.owner;
            bp.home = s.home >= 0;
            for (int q = 0; q < g.numPlayers; ++q)
                if (ps.units[size_t(q)].count(UnitType::PDS)) bp.shield = true;
            if (back == Back::Horizon) {
                bp.x = 0;
                bp.y = horizon + hpr;
                bp.r = hpr;
            } else {
                const int n = alivePlanets;
                bp.r = n == 2 ? 0.26f : 0.2f;
                bp.x = n == 2 ? (k == 0 ? -0.4f : 0.4f) : -0.55f + 0.55f * float(k);
                bp.y = n == 2 ? (k == 0 ? 0.62f : 0.56f) : (k == 1 ? 0.66f : 0.56f);
            }
            if (teSys && alivePlanets == 1) {  // 0.43.2: behind the fight, in the middle of the storm
                bp.x = 0.f;
                bp.y = -0.04f;
                bp.r = 0.24f;
            }
            planets.push_back(bp);
            ++k;
        }
        hpx = 0;
        hpy = horizon + hpr;
        // structures: space docks in the background, PDS that fire from the planets
        structs.clear();
        for (size_t i = 0, j = 0; i < s.planets.size(); ++i) {
            const PlanetState& ps = g.planets[size_t(s.planets[i])];
            if (ps.destroyed) continue;
            const BgPlanet& bp = planets[j++];
            for (int q = 0; q < g.numPlayers; ++q) {
                const Force& f = ps.units[size_t(q)];
                const int npds = std::min(2, f.count(UnitType::PDS));
                for (int u = 0; u < npds; ++u) {
                    Struct st;
                    st.t = UnitType::PDS;
                    st.owner = q;
                    st.side = sideOf(q);
                    st.size = back == Back::Horizon ? 0.13f : 0.09f;
                    if (back == Back::Horizon) {
                        st.x = -0.34f + 0.2f * float(structs.size() % 4) + (q % 2 ? 0.45f : 0.f);
                        if (st.x > 0.62f) st.x -= 1.1f;
                        const float hy = hpy - std::sqrt(std::max(0.f, hpr * hpr - st.x * st.x));
                        st.y = hy - st.size * 0.32f;
                    } else {
                        st.x = bp.x + (u == 0 ? -0.45f : 0.45f) * bp.r;
                        st.y = bp.y + bp.r * 0.75f;
                    }
                    st.nextFire = now + 1500 + uint32_t(rng.i(1500));
                    structs.push_back(st);
                }
                if (f.count(UnitType::SpaceDock)) {
                    Struct st;
                    st.t = UnitType::SpaceDock;
                    st.owner = q;
                    st.side = sideOf(q);
                    st.size = back == Back::Horizon ? 0.2f : 0.12f;
                    if (back == Back::Horizon) {
                        st.x = (q % 2) ? 0.56f : -0.56f;
                        st.y = horizon - 0.2f;
                    } else {
                        st.x = bp.x + bp.r * 1.25f;
                        st.y = bp.y - bp.r * 0.3f;
                    }
                    structs.push_back(st);
                }
            }
        }
        for (int side = 0; side < 2; ++side) {  // floating factories (Saar) in the space area
            const Force& f = side == 0 ? a : b;
            if (!f.count(UnitType::SpaceDock)) continue;
            Struct st;
            st.t = UnitType::SpaceDock;
            st.owner = pl[side];
            st.side = side;
            st.size = 0.16f;
            st.x = side == 0 ? -0.62f : 0.62f;
            st.y = bandTop + 0.05f;
            structs.push_back(st);
        }
        // Gauss Cannons (Mirveda PDS in the space area, 0.43.0): floating rails behind their fleet
        for (int side = 0; side < 2; ++side) {
            const Force& f = side == 0 ? a : b;
            const int q = pl[side];
            if (q < 0 || q >= g.numPlayers || !f.count(UnitType::PDS) || !unitDef(g, q, UnitType::PDS).spaceOnly) continue;
            for (int u = 0; u < std::min(2, f.count(UnitType::PDS)); ++u) {
                Struct st;
                st.t = UnitType::PDS;
                st.gauss = true;
                st.owner = q;
                st.side = side;
                st.size = 0.13f;
                st.x = (side == 0 ? -1.f : 1.f) * (0.5f - 0.08f * float(u));
                st.y = bandBot + 0.12f + 0.15f * float(u);  // (below the ships, clear of them)
                st.nextFire = now + 1700 + uint32_t(rng.i(900));
                structs.push_back(st);
            }
        }
        // both fleets at one scale: smaller when crowded, bigger when there are only a few
        int most = 1;
        for (const Force* f : {&a, &b}) {
            int n = 0;
            for (UnitType t : {UnitType::Flagship, UnitType::Flagship2, UnitType::Dreadnought, UnitType::Carrier, UnitType::Cruiser,
                               UnitType::Destroyer})
                n += f->count(t);
            most = std::max(most, n);
        }
        base = 0.24f * std::max(0.62f, std::min(1.3f, std::sqrt(5.f / float(most))));
        addShips(g, 0, a, now);
        addShips(g, 1, b, now);
        // the fleets fly in, big ships first
        for (Ship& sh : ships) {
            uint32_t delay = 0;
            switch (sh.t) {
                case UnitType::Carrier: delay = 0; break;
                case UnitType::Flagship: delay = 80; break;
                case UnitType::Dreadnought: delay = 160; break;
                case UnitType::Cruiser: delay = 260; break;
                case UnitType::Destroyer: delay = 330; break;
                case UnitType::Fighter: delay = 380 + uint32_t(rng.i(200)); break;
                default: delay = 0; break;
            }
            sh.enterAt = now + uint32_t(float(delay) * speed);
            sh.enterDur = uint32_t(std::max(350.f, (sh.t == UnitType::Fighter ? 900.f : 1250.f) * speed));
            sh.nextFire = sh.enterAt + sh.enterDur + uint32_t(rng.i(900));
            sh.nextMove = now;
            if (sh.moon) {
                sh.enterDur = uint32_t(std::max(300.f, 1400.f * speed));
                sh.nextFire = now + 2200 + uint32_t(rng.i(1200));
            }
        }
    }

    // ---------------------------------------------------------- targets and weapons
    int pickTarget(int side, float fromX, float fromY, float maxRange, bool moons) {
        int best = -1;
        float score = 1e9f;
        for (size_t i = 0; i < ships.size(); ++i) {
            const Ship& s = ships[i];
            if (s.side == side || !alive(s) || (s.moon && !moons)) continue;
            const float dx = s.x - fromX, dy = s.y - fromY;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (maxRange > 0 && d > maxRange && !s.moon) continue;
            const float sc = d + rng.r(0.f, 0.9f);
            if (sc < score) {
                score = sc;
                best = int(i);
            }
        }
        return best;
    }

    float speedOf(int kind) const {
        switch (kind) {
            case Laser: return 2.6f;
            case Rail: return 4.f;
            case Missile: return 0.45f;
            case Rocket: return 1.1f;
            case Torpedo: return 0.55f;
            case Bolt: return 1.6f;
            case Tracer: return 4.5f;
            case RedBolt: return 2.4f;
            case Gauss: return 3.2f;
        }
        return 2.f;
    }

    void fire(int kind, float x0, float y0, int target, uint32_t col, uint32_t now, uint32_t delay = 0, float spread = 0.03f) {
        if (target < 0) return;
        const Ship& t = ships[size_t(target)];
        Shot s;
        s.kind = kind;
        s.x0 = x0;
        s.y0 = y0;
        s.x1 = t.x + rng.r(-spread, spread) * (t.moon ? 4.f : 1.f);
        s.y1 = t.y + rng.r(-spread, spread) * (t.moon ? 4.f : 1.f);
        const float d = std::sqrt((s.x1 - x0) * (s.x1 - x0) + (s.y1 - y0) * (s.y1 - y0));
        s.born = now + delay;
        s.arrive = s.born + uint32_t(std::max(60.f, d / speedOf(kind) * 1000.f));
        s.col = col;
        s.wob = rng.r(-1.f, 1.f);
        s.nextTrail = s.born;
        shots.push_back(s);
        if (kind != Missile && kind != Torpedo) muzzle(x0, y0, kind == Rail ? rgb(150, 210, 255) : rgb(255, 210, 120), now + delay);
    }

    void muzzle(float x, float y, uint32_t c, uint32_t now) {
        Boom b;
        b.kind = Muzzle;
        b.x = x;
        b.y = y;
        b.r = 0.018f;
        b.born = now;
        b.life = 140;
        booms.push_back(b);
        (void)c;
    }

    void beam(int kind, int src, int dst, uint32_t life, uint32_t col, uint32_t now) {
        if (dst < 0) return;
        Beam b;
        b.kind = kind;
        b.src = src;
        b.dst = dst;
        b.x1 = ships[size_t(dst)].x;
        b.y1 = ships[size_t(dst)].y;
        b.born = now;
        b.life = life;
        b.col = col;
        beams.push_back(b);
        const float pan = ships[size_t(src)].x * 0.7f;
        audio::play(kind == GoldBeam ? audio::Sfx::GoldBeam : kind == Rainbow ? audio::Sfx::WarSunBeam : audio::Sfx::RedBeam, 1.f, pan);
    }

    void boom(int kind, float x, float y, float r, uint32_t life, uint32_t now) {
        Boom b;
        b.kind = kind;
        b.x = x;
        b.y = y;
        b.r = r;
        b.born = now;
        b.life = life;
        booms.push_back(b);
        if (kind == Big) audio::play(audio::Sfx::Explosion, std::min(1.2f, 0.6f + r * 3.f), x * 0.7f);  // a ship goes
    }

    // The sound of a shot as it leaves the gun (owner, 0.27.0: each unit's weapon its own sound).
    static void shotSound(int kind, float x) {
        audio::Sfx s = audio::Sfx::FighterLaser;
        switch (kind) {
            case Laser: s = audio::Sfx::FighterLaser; break;   // fighters
            case Rail: s = audio::Sfx::Railgun; break;         // cruisers
            case Missile: s = audio::Sfx::Missile; break;      // carriers
            case Rocket: s = audio::Sfx::Rocket; break;        // flagships
            case Torpedo: s = audio::Sfx::Torpedo; break;      // flagships
            case Bolt: s = audio::Sfx::Plasma; break;          // PDS
            case Tracer: s = audio::Sfx::Flak; break;          // destroyers, dreadnought point defence
            case RedBolt: s = audio::Sfx::HeavyBolt; break;    // dreadnoughts
            case Gauss: s = audio::Sfx::Railgun; break;        // Gauss Cannons (louder below)
        }
        audio::play(s, kind == Gauss ? 1.4f : 1.f, x * 0.7f);
    }

    void sparks(float x, float y, int n, float spd, uint32_t now, uint32_t c = rgb(255, 220, 140)) {
        for (int i = 0; i < n; ++i) {
            Part p;
            p.kind = Spark;
            p.x = x;
            p.y = y;
            const float a = rng.r(0, 2 * kPi), v = rng.r(0.2f, 1.f) * spd;
            p.vx = std::cos(a) * v;
            p.vy = std::sin(a) * v;
            p.born = now;
            p.life = 250 + uint32_t(rng.i(350));
            p.col = c;
            parts.push_back(p);
        }
    }

    void chunks(float x, float y, int n, float spd, float size, uint32_t c, uint32_t now) {
        for (int i = 0; i < n; ++i) {
            Part p;
            p.kind = Chunk;
            p.x = x + rng.r(-0.02f, 0.02f);
            p.y = y + rng.r(-0.02f, 0.02f);
            const float a = rng.r(0, 2 * kPi), v = rng.r(0.15f, 1.f) * spd;
            p.vx = std::cos(a) * v;
            p.vy = std::sin(a) * v;
            p.born = now;
            p.life = 0;  // debris stays
            p.size = size * rng.r(0.5f, 1.3f);
            p.rot = rng.r(0, 2 * kPi);
            p.vr = rng.r(-3.f, 3.f);
            p.col = c;
            p.a0 = 200;
            parts.push_back(p);
        }
    }

    void smoke(float x, float y, uint32_t now, float size = 0.012f) {
        Part p;
        p.kind = Smoke;
        p.x = x;
        p.y = y;
        p.vx = rng.r(-0.03f, 0.03f);
        p.vy = rng.r(-0.07f, -0.03f);
        p.born = now;
        p.life = 1100 + uint32_t(rng.i(500));
        p.size = size;
        p.grow = 0.03f;
        p.col = rgb(90, 90, 96);
        p.a0 = 130;
        parts.push_back(p);
    }

    void puff(float x, float y, uint32_t now) {  // flak
        for (int i = 0; i < 3; ++i) {
            Part p;
            p.kind = Puff;
            p.x = x + rng.r(-0.025f, 0.025f);
            p.y = y + rng.r(-0.025f, 0.025f);
            p.born = now + uint32_t(i * 40);
            p.life = 450;
            p.size = 0.01f;
            p.grow = 0.06f;
            p.col = rgb(200, 200, 196);
            p.a0 = 170;
            parts.push_back(p);
        }
        sparks(x, y, 3, 0.25f, now, rgb(255, 230, 170));
    }

    void piece(const Ship& s, float u0, float u1, float v0, float v1, float vx, float vy, uint32_t now) {
        Piece p;
        p.t = s.t;
        p.faceLeft = s.faceLeft;
        p.ox = s.x;
        p.oy = s.y;
        p.vx = vx;
        p.vy = vy;
        p.u0 = u0;
        p.u1 = u1;
        p.v0 = v0;
        p.v1 = v1;
        p.w = std::max(4, int(s.len * R));
        p.h = std::max(4, int(s.len * R * 0.8f));
        p.col = darker(col[s.side], 0.45f);
        p.born = now;
        pieces.push_back(p);
    }

    // ---------------------------------------------------------- the dice say so
    int victim(int side, UnitType t, bool preferDamaged) {
        int any = -1;
        for (size_t i = 0; i < ships.size(); ++i) {
            const Ship& s = ships[i];
            const bool same = t == UnitType::Flagship2 ? s.t == UnitType::Flagship && s.second : s.t == t && !s.second;
            if (s.side != side || !same || !alive(s)) continue;
            if (preferDamaged == s.damaged) return int(i);
            if (any < 0) any = int(i);
        }
        return any;
    }

    void applyPending(const Pending& p, uint32_t now) {
        if (p.kind == 0) {  // sustained damage: a small blast on the hull, then smoke
            for (int k = 0; k < p.count; ++k) {
                const int v = victim(p.side, p.t, false);
                if (v < 0) break;
                Ship& s = ships[size_t(v)];
                if (s.damaged && s.hp > 1) continue;
                s.damaged = true;
                s.shieldAt = now;
                if (s.moon) {  // war suns: a tiny shaded blast, no visible damage
                    boom(Shaded, s.x + rng.r(-0.2f, 0.2f) * s.len, s.y + rng.r(-0.2f, 0.2f) * s.len, 0.03f, 500, now);
                    continue;
                }
                s.sx = rng.r(-0.3f, 0.3f) * s.len;
                s.sy = rng.r(-0.12f, 0.12f) * s.len;
                boom(Fire, s.x + s.sx, s.y + s.sy, 0.045f, 450, now);
                sparks(s.x + s.sx, s.y + s.sy, 8, 0.5f, now);
            }
            return;
        }
        if (p.t == UnitType::Fighter) {
            int& act = fightersActual[p.side];
            act = std::max(0, act - p.count);
            int visible = 0;
            for (const Ship& s : ships)
                if (s.side == p.side && s.t == UnitType::Fighter && alive(s)) ++visible;
            int kill = std::max(0, visible - std::min(10, act));
            for (int k = 0; k < p.count; ++k) {
                if (kill > 0) {
                    const int v = victim(p.side, UnitType::Fighter, false);
                    if (v >= 0) startDeath(ships[size_t(v)], now + uint32_t(k * 90));
                    --kill;
                } else {  // one not drawn: an explosion somewhere on that side
                    const float x = -dirOf(p.side) * rng.r(0.1f, 0.6f), y = rng.r(bandTop, bandBot);
                    boom(Fire, x, y, 0.03f, 350, now + uint32_t(k * 90));
                    sparks(x, y, 5, 0.6f, now + uint32_t(k * 90));
                }
            }
            return;
        }
        for (int k = 0; k < p.count; ++k) {
            const int v = victim(p.side, p.t, true);
            if (v < 0) break;
            startDeath(ships[size_t(v)], now + uint32_t(k * 150));
        }
    }

    void startDeath(Ship& s, uint32_t at) {
        s.state = 1;
        s.dieAt = at;
        s.stage = 0;
    }

    // Each kind breaks apart its own way (owner's list).
    void dying(Ship& s, uint32_t now) {
        if (int32_t(now - s.dieAt) < 0) return;
        const uint32_t e = now - s.dieAt;
        const float dir = s.faceLeft ? -1.f : 1.f;
        const uint32_t husk = darker(col[s.side], 0.5f);
        switch (s.t) {
            case UnitType::Fighter:
                boom(Fire, s.x, s.y, 0.04f, 380, now);
                sparks(s.x, s.y, 7, 0.7f, now);
                chunks(s.x, s.y, 3, 0.15f, 0.006f, husk, now);
                s.state = 2;
                break;
            case UnitType::Destroyer:
                boom(Fire, s.x, s.y, 0.08f, 600, now);
                boom(Flash, s.x, s.y, 0.05f, 200, now);
                sparks(s.x, s.y, 14, 0.8f, now);
                chunks(s.x, s.y, 9, 0.18f, 0.01f, husk, now);
                s.state = 2;
                break;
            case UnitType::Carrier:
                if (s.stage == 0) {
                    boom(Fire, s.x, s.y, 0.045f, 400, now);
                    sparks(s.x, s.y, 6, 0.4f, now);
                    s.stage = 1;
                }
                if (s.stage == 1 && e >= 380) {  // cracks in half; the husk stays
                    piece(s, -0.5f, 0.f, -0.5f, 0.5f, -0.035f, rng.r(-0.015f, 0.015f), now);
                    piece(s, 0.f, 0.5f, -0.5f, 0.5f, 0.035f, rng.r(-0.015f, 0.015f), now);
                    boom(Fire, s.x, s.y, 0.03f, 350, now);
                    chunks(s.x, s.y, 4, 0.08f, 0.008f, husk, now);
                    s.state = 2;
                }
                break;
            case UnitType::Cruiser: {
                const float L = s.len;
                const float spots[3] = {0.32f, 0.16f, 0.f};
                while (s.stage < 3 && e >= uint32_t(s.stage) * 230u) {  // three blasts across the bow
                    boom(Fire, s.x + dir * spots[s.stage] * L, s.y + rng.r(-0.08f, 0.08f) * L, 0.035f, 380, now);
                    sparks(s.x + dir * spots[s.stage] * L, s.y, 5, 0.4f, now);
                    ++s.stage;
                }
                if (s.stage == 3 && e >= 760) {  // three pieces
                    piece(s, -0.5f, -0.17f, -0.5f, 0.5f, -0.045f, rng.r(-0.02f, 0.02f), now);
                    piece(s, -0.17f, 0.17f, -0.5f, 0.5f, 0.f, rng.r(-0.03f, 0.03f), now);
                    piece(s, 0.17f, 0.5f, -0.5f, 0.5f, 0.045f, rng.r(-0.02f, 0.02f), now);
                    chunks(s.x, s.y, 5, 0.1f, 0.008f, husk, now);
                    s.state = 2;
                }
                break;
            }
            case UnitType::Dreadnought: {
                const float L = s.len;
                const uint32_t gap = uint32_t(std::max(500.f, 1100.f * speed));
                while (s.stage < 2 && e >= uint32_t(s.stage) * gap) {  // delayed explosions
                    const float ox = rng.r(-0.3f, 0.3f) * L, oy = rng.r(-0.1f, 0.1f) * L;
                    boom(Fire, s.x + ox, s.y + oy, 0.05f, 550, now);
                    sparks(s.x + ox, s.y + oy, 8, 0.5f, now);
                    if (s.stage == 0 && !s.damaged) {
                        s.damaged = true;
                        s.sx = ox;
                        s.sy = oy;
                    }
                    ++s.stage;
                }
                if (s.stage == 2 && e >= 2 * gap) {
                    // the front blows into debris, the back splits in two
                    const float fx = s.x + dir * 0.28f * L;
                    boom(Big, fx, s.y, 0.09f, 800, now);
                    boom(Flash, fx, s.y, 0.06f, 220, now);
                    sparks(fx, s.y, 16, 0.8f, now);
                    chunks(fx, s.y, 12, 0.2f, 0.012f, husk, now);
                    const float b0 = s.faceLeft ? 0.08f : -0.5f, b1 = s.faceLeft ? 0.5f : -0.08f;
                    piece(s, b0, b1, -0.5f, 0.f, -dir * 0.012f, -0.04f, now);
                    piece(s, b0, b1, 0.f, 0.5f, -dir * 0.012f, 0.04f, now);
                    s.state = 2;
                }
                break;
            }
            case UnitType::Flagship: {
                const uint32_t pops = uint32_t(std::max(600.f, 1400.f * speed));
                while (e < pops && e >= uint32_t(s.stage) * 180u) {  // small blasts all round it
                    boom(Fire, s.x + rng.r(-0.45f, 0.45f) * s.len, s.y + rng.r(-0.3f, 0.3f) * s.len, 0.025f, 320, now);
                    ++s.stage;
                }
                if (e >= pops + 450 && s.state == 1) {  // it implodes, then goes up
                    boom(Big, s.x, s.y, 0.28f, 1100, now);
                    boom(Flash, s.x, s.y, 0.2f, 300, now);
                    Ring r;
                    r.x = s.x;
                    r.y = s.y;
                    r.maxR = 0.7f;
                    r.flat = 0.55f;
                    r.born = now;
                    r.life = 700;
                    r.col = rgb(255, 210, 150);
                    rings.push_back(r);
                    sparks(s.x, s.y, 30, 1.2f, now);
                    chunks(s.x, s.y, 26, 0.3f, 0.013f, husk, now);
                    s.state = 2;
                }
                break;
            }
            case UnitType::WarSun: {
                if (s.stage == 0) {  // like the first Death Star: a flash, then the ring
                    boom(Flash, s.x, s.y, s.len * 0.55f, 500, now);
                    s.stage = 1;
                }
                if (s.stage == 1 && e >= 280) {
                    for (int k = 0; k < 2; ++k) {
                        Ring r;
                        r.x = s.x;
                        r.y = s.y;
                        r.maxR = k == 0 ? 2.4f : 1.5f;
                        r.flat = 0.16f;
                        r.born = now + uint32_t(k * 160);
                        r.life = k == 0 ? 1300 : 1600;
                        r.col = k == 0 ? rgb(230, 245, 255) : rgb(255, 170, 90);
                        rings.push_back(r);
                    }
                    boom(Big, s.x, s.y, s.len * 0.6f, 1200, now);
                    sparks(s.x, s.y, 50, 1.6f, now, rgb(220, 240, 255));
                    chunks(s.x, s.y, 30, 0.35f, 0.016f, darker(col[s.side], 0.6f), now);
                    rippleAt = now;
                    rippleUntil = now + 3200;
                    s.state = 2;
                }
                break;
            }
            default: s.state = 2; break;
        }
    }

    // ---------------------------------------------------------- behaviour
    void behave(Ship& s, uint32_t now, float dt) {
        const float dir = dirOf(s.side);
        const uint32_t T = now - t0;
        const float tt = float(T);
        if (int32_t(now - s.enterAt) < int32_t(s.enterDur)) {  // flying in
            const float k = easeOut(float(int32_t(now - s.enterAt)) / float(std::max<uint32_t>(1, s.enterDur)));
            if (int32_t(now - s.enterAt) < 0) {
                s.x = s.ex;
                s.y = s.ey;
                return;
            }
            s.x = s.ex + (s.hx - s.ex) * k;
            s.y = s.ey + (s.hy - s.ey) * k;
            return;
        }
        if (s.state == 3) {  // retreating: off the way it came
            s.vx -= dir * 1.4f * dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            if (std::fabs(s.x) > 1.5f) s.state = 2;
            return;
        }
        auto ease = [&](float rate) {
            const float k = std::min(1.f, dt * rate);
            s.x += (s.tx - s.x) * k;
            s.y += (s.ty - s.y) * k;
        };
        auto steer = [&](float accel, float maxV) {
            float dx = s.tx - s.x, dy = s.ty - s.y;
            const float d = std::sqrt(dx * dx + dy * dy) + 1e-4f;
            s.vx += dx / d * accel * dt;
            s.vy += dy / d * accel * dt;
            const float v = std::sqrt(s.vx * s.vx + s.vy * s.vy);
            if (v > maxV) {
                s.vx *= maxV / v;
                s.vy *= maxV / v;
            }
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            if (std::fabs(s.vx) > 0.05f) s.faceLeft = s.vx < 0;
        };
        const float bowX = s.x + (s.faceLeft ? -1.f : 1.f) * s.len * 0.45f;
        const bool canFire = int32_t(now - s.nextFire) >= 0 && !ended;
        switch (s.t) {
            case UnitType::Carrier:  // slow, hangs back; slow grey missiles
                s.tx = s.hx + 0.03f * std::sin(tt / 2300.f + s.phase);
                s.ty = s.hy + 0.04f * std::sin(tt / 3100.f + s.phase);
                ease(1.5f);
                if (canFire) {
                    fire(Missile, bowX, s.y, pickTarget(s.side, s.x, s.y, 0, false), rgb(190, 196, 206), now);
                    s.nextFire = now + 2600 + uint32_t(rng.i(1500));
                }
                break;
            case UnitType::Cruiser:  // methodical, in front, in rhythm; blue railgun
                s.tx = s.hx + 0.015f * std::sin(tt / 4000.f + s.phase);
                s.ty = s.hy + 0.05f * std::sin(2 * kPi * tt / 2400.f + s.phase);
                ease(3.f);
                if (canFire) {
                    fire(Rail, bowX, s.y, pickTarget(s.side, s.x, s.y, 0, false), rgb(90, 190, 255), now);
                    s.nextFire += 1600;
                    if (int32_t(now - s.nextFire) > 0) s.nextFire = now + 1600;
                }
                break;
            case UnitType::Dreadnought: {  // slow; every 5 s it stops and lets everything go
                const int cyc = int((T + uint32_t(s.phase * 800.f)) % 5000u);
                if (cyc < s.lastCycle) {  // a new cycle: somewhere to drift to
                    s.tx = s.hx + rng.r(-0.07f, 0.07f) + dir * rng.r(0.f, 0.05f);
                    s.ty = s.hy + rng.r(-0.06f, 0.06f);
                }
                if (cyc < 3500) ease(0.8f);
                auto crossed = [&](int th) { return s.lastCycle < th && cyc >= th; };
                if (!ended && int32_t(now - s.nextFire) >= 0) {
                    if (crossed(3550)) beam(RedBeam, int(&s - &ships[0]), pickTarget(s.side, s.x, s.y, 0, true), 600, rgb(255, 40, 50), now);
                    for (int th : {3650, 3800, 3950, 4100})
                        if (crossed(th)) fire(RedBolt, bowX, s.y + rng.r(-0.3f, 0.3f) * s.len * 0.5f, pickTarget(s.side, s.x, s.y, 0, false),
                                              rgb(255, 70, 60), now);
                    for (int th : {3700, 3950, 4200})
                        if (crossed(th)) {
                            const int tg = pickTarget(s.side, s.x, s.y, 0, false);
                            fire(Tracer, s.x, s.y - s.len * 0.15f, tg, rgb(220, 220, 210), now, 0, 0.08f);
                        }
                }
                s.lastCycle = cyc;
                break;
            }
            case UnitType::Flagship: {  // takes its place and holds it; rockets, torpedoes, a laser now and then
                s.tx = s.hx;
                s.ty = s.hy + 0.02f * std::sin(tt / 1700.f + s.phase);
                ease(2.f);
                if (canFire) {
                    const int tg = pickTarget(s.side, s.x, s.y, 0, false);
                    for (int k = 0; k < 3; ++k)
                        fire(Rocket, bowX, s.y + (float(k) - 1.f) * s.len * 0.12f, tg, rgb(200, 20, 30), now, uint32_t(k * 120), 0.05f);
                    if (s.cyc % 2 == 1) fire(Torpedo, bowX, s.y + s.len * 0.1f, pickTarget(s.side, s.x, s.y, 0, false), rgb(150, 150, 160), now, 380);
                    if (s.cyc % 3 == 2) beam(GoldBeam, int(&s - &ships[0]), pickTarget(s.side, s.x, s.y, 0, true), 700, rgb(255, 190, 40), now);
                    ++s.cyc;
                    s.nextFire = now + 2100 + uint32_t(rng.i(500));
                }
                break;
            }
            case UnitType::Destroyer: {  // quick; sometimes in close with its flak guns
                if (int32_t(now - s.nextMove) >= 0) {
                    const int tg = rng.f() < 0.35f ? pickTarget(s.side, s.x, s.y, 0, false) : -1;
                    if (tg >= 0) {
                        s.tx = ships[size_t(tg)].x - dir * 0.2f;
                        s.ty = ships[size_t(tg)].y + rng.r(-0.08f, 0.08f);
                    } else {
                        s.tx = -dir * rng.r(0.12f, 0.7f);
                        s.ty = rng.r(bandTop + 0.05f, bandBot - 0.05f);
                    }
                    s.nextMove = now + 1200 + uint32_t(rng.i(1400));
                }
                steer(1.4f, 0.7f);
                s.vx *= (1.f - std::min(1.f, dt * 0.8f));
                s.vy *= (1.f - std::min(1.f, dt * 0.8f));
                if (canFire) {
                    const int tg = pickTarget(s.side, s.x, s.y, 0.42f, false);
                    if (tg >= 0) {
                        fire(Tracer, bowX, s.y, tg, rgb(255, 225, 150), now, 0, 0.06f);
                        fire(Tracer, bowX, s.y, tg, rgb(255, 225, 150), now, 90, 0.06f);
                    }
                    s.nextFire = now + 450 + uint32_t(rng.i(400));
                }
                break;
            }
            case UnitType::Fighter: {  // fast and erratic, dodging fire; little fuchsia lasers
                if (int32_t(now - s.nextMove) >= 0) {
                    s.tx = rng.r(-0.85f, 0.85f) + dir * 0.15f;
                    s.ty = rng.r(bandTop, bandBot);
                    s.nextMove = now + 250 + uint32_t(rng.i(500));
                }
                steer(4.5f, 1.3f);
                s.vx += rng.r(-0.6f, 0.6f) * dt;
                s.vy += rng.r(-0.6f, 0.6f) * dt;
                if (s.x < -0.9f) s.vx = std::fabs(s.vx);
                if (s.x > 0.9f) s.vx = -std::fabs(s.vx);
                if (canFire) {
                    const int tg = pickTarget(s.side, s.x, s.y, 0.9f, false);
                    if (tg >= 0) fire(Laser, s.x + (s.faceLeft ? -1.f : 1.f) * s.len * 0.5f, s.y, tg, rgb(255, 60, 220), now);
                    s.nextFire = now + 500 + uint32_t(rng.i(650));
                }
                break;
            }
            case UnitType::WarSun: {  // a moon in the background, firing out at the ships (rainbow)
                s.x = s.hx + 0.01f * std::sin(tt / 5000.f + s.phase);
                s.y = s.hy + 0.008f * std::sin(tt / 3700.f + s.phase);
                if (!ended && s.chargeAt == 0 && int32_t(now - s.nextFire) >= 0) {
                    s.chargeAt = now;
                    audio::play(audio::Sfx::WarSunCharge, 1.f, s.x * 0.7f);
                }
                if (ended) s.chargeAt = 0;
                if (s.chargeAt && int32_t(now - s.chargeAt) >= 600) {
                    const int tg = pickTarget(s.side, s.x, s.y, 0, false);
                    beam(Rainbow, int(&s - &ships[0]), tg, 650, 0, now);
                    s.chargeAt = 0;
                    s.nextFire = now + 3000 + uint32_t(rng.i(900));
                }
                break;
            }
            default: break;
        }
        // keep inside the field
        s.y = std::max(bandTop - 0.05f, std::min(bandBot + 0.05f, s.y));
        if (s.damaged && !s.moon && int32_t(now - s.nextSmoke) >= 0) {
            const float ox = s.faceLeft == (s.side == 1) ? s.sx : -s.sx;
            smoke(s.x + ox, s.y + s.sy, now, 0.01f + 0.004f * float(rng.i(3)));
            if (rng.f() < 0.25f) sparks(s.x + ox, s.y + s.sy, 1, 0.15f, now, rgb(255, 150, 60));
            s.nextSmoke = now + 90 + uint32_t(rng.i(70));
        }
    }

    void update(uint32_t now) {
        int dtms = int(now - last);
        if (last == 0 || dtms < 0) dtms = 16;
        dtms = std::min(dtms, 60);
        last = now;
        const float dt = float(dtms) / 1000.f;
        for (size_t i = 0; i < pending.size();) {
            if (int32_t(now - pending[i].at) >= 0) {
                Pending p = pending[i];
                pending.erase(pending.begin() + long(i));
                applyPending(p, now);
            } else {
                ++i;
            }
        }
        for (size_t i = 0; i < ships.size(); ++i) {
            Ship& s = ships[i];
            if (s.state == 0 || s.state == 3) behave(s, now, dt);
            else if (s.state == 1) {
                if (s.damaged && !s.moon && int32_t(now - s.nextSmoke) >= 0) {
                    smoke(s.x + s.sx, s.y + s.sy, now, 0.014f);
                    s.nextSmoke = now + 70;
                }
                dying(s, now);
            }
        }
        // shots that arrive
        for (size_t i = 0; i < shots.size();) {
            Shot& s = shots[i];
            if (!s.heard && int32_t(now - s.born) >= 0) {
                s.heard = true;
                shotSound(s.kind, s.x0);
            }
            if ((s.kind == Missile || s.kind == Rocket || s.kind == Torpedo) && int32_t(now - s.born) >= 0 && int32_t(now - s.nextTrail) >= 0) {
                const float k = float(int32_t(now - s.born)) / float(std::max<uint32_t>(1, s.arrive - s.born));
                const float x = s.x0 + (s.x1 - s.x0) * k, y = s.y0 + (s.y1 - s.y0) * k;
                Part p;
                p.kind = Smoke;
                p.x = x;
                p.y = y;
                p.born = now;
                p.life = s.kind == Rocket ? 350 : 600;
                p.size = 0.005f;
                p.grow = 0.012f;
                p.col = s.kind == Rocket ? rgb(150, 70, 60) : rgb(130, 130, 140);
                p.a0 = 110;
                parts.push_back(p);
                s.nextTrail = now + 45;
            }
            if (int32_t(now - s.arrive) >= 0) {
                if (s.kind == Tracer) puff(s.x1, s.y1, now);
                else if (s.kind == Gauss) {  // the slug strikes: a blinding flash, fire and a spray of sparks
                    boom(Flash, s.x1, s.y1, 0.07f, 260, now);
                    boom(Fire, s.x1, s.y1, 0.045f, 520, now);
                    sparks(s.x1, s.y1, 14, 0.7f, now, rgb(200, 235, 255));
                    audio::play(audio::Sfx::Impact, 1.1f, s.x1 * 0.7f);
                } else if (s.kind == Missile || s.kind == Torpedo) {
                    boom(Fire, s.x1, s.y1, s.kind == Torpedo ? 0.04f : 0.03f, 380, now);
                    audio::play(audio::Sfx::Impact, s.kind == Torpedo ? 0.9f : 0.6f, s.x1 * 0.7f);
                    sparks(s.x1, s.y1, 6, 0.4f, now);
                } else {
                    boom(Muzzle, s.x1, s.y1, 0.016f, 160, now);
                    sparks(s.x1, s.y1, 3, 0.35f, now, lighter(s.col, 0.4f));
                }
                shots.erase(shots.begin() + long(i));
            } else {
                ++i;
            }
        }
        for (size_t i = 0; i < beams.size();) {
            if (int32_t(now - beams[i].born) > int32_t(beams[i].life)) beams.erase(beams.begin() + long(i));
            else ++i;
        }
        for (size_t i = 0; i < booms.size();) {
            if (int32_t(now - booms[i].born) > int32_t(booms[i].life)) booms.erase(booms.begin() + long(i));
            else ++i;
        }
        for (size_t i = 0; i < rings.size();) {
            if (int32_t(now - rings[i].born) > int32_t(rings[i].life)) rings.erase(rings.begin() + long(i));
            else ++i;
        }
        for (size_t i = 0; i < parts.size();) {
            Part& p = parts[i];
            if (p.life && int32_t(now - p.born) > int32_t(p.life)) {
                parts.erase(parts.begin() + long(i));
                continue;
            }
            if (int32_t(now - p.born) >= 0) {
                p.x += p.vx * dt;
                p.y += p.vy * dt;
                p.rot += p.vr * dt;
                p.size += p.grow * dt;
                if (p.kind == Chunk) {  // debris slows down and stays
                    p.vx *= 1.f - std::min(1.f, dt * 1.2f);
                    p.vy *= 1.f - std::min(1.f, dt * 1.2f);
                    p.vr *= 1.f - std::min(1.f, dt * 0.6f);
                }
            }
            ++i;
        }
        if (parts.size() > 700) {  // the oldest short-lived ones go first
            size_t drop = parts.size() - 700;
            for (size_t i = 0; i < parts.size() && drop > 0;) {
                if (parts[i].life) {
                    parts.erase(parts.begin() + long(i));
                    --drop;
                } else {
                    ++i;
                }
            }
        }
        for (Piece& p : pieces) {
            const float k = 1.f - std::min(1.f, dt * 0.35f);
            p.dx += p.vx * dt;
            p.dy += p.vy * dt;
            p.vx *= k;
            p.vy *= k;
        }
        // PDS fire from the planets at the other side's ships
        for (Struct& st : structs) {
            if (st.t != UnitType::PDS || st.side < 0 || ended || int32_t(now - st.nextFire) < 0) continue;
            if (int32_t(now - t0) < 1500) continue;
            const int tg = pickTarget(st.side, st.x, st.y, 0, false);
            if (tg >= 0 && st.gauss) {  // one charged slug, a flash at the muzzle that shakes the space round it
                const float mx = st.x + (st.side == 0 ? 1.f : -1.f) * st.size * 0.5f;
                fire(Gauss, mx, st.y, tg, rgb(150, 210, 255), now, 0, 0.01f);
                boom(Flash, mx, st.y, 0.05f, 220, now);
                sparks(mx, st.y, 8, 0.5f, now, rgb(190, 230, 255));
                st.nextFire = now + 3200 + uint32_t(rng.i(1400));
                continue;
            }
            if (tg >= 0) {
                const float my = st.y - st.size * 0.4f;
                fire(Bolt, st.x, my, tg, rgb(90, 240, 255), now, 0, 0.04f);
                fire(Bolt, st.x, my, tg, rgb(90, 240, 255), now, 140, 0.04f);
            }
            st.nextFire = now + 2000 + uint32_t(rng.i(1600));
        }
    }

    // ---------------------------------------------------------- the backdrop (drawn once per fight)
    void paintPlanet(Canvas& b, float x, float y, float r, const PlanetDef& pd, bool home, uint32_t seed, float lightAlpha) {
        const bool mecatol = !std::strcmp(pd.key, "mr");
        uint32_t base = mecatol ? rgb(196, 160, 86) : home ? rgb(120, 140, 172)
                        : pd.trait == Trait::Cultural ? theme::cultural
                        : pd.trait == Trait::Hazardous ? theme::hazardous
                        : pd.trait == Trait::Industrial ? theme::industrial
                                                         : rgb(170, 160, 140);
        const int sp = planetSpecial(pd.key);  // 0.43.2: the Fracture's planets, Thunder's Edge
        static const uint32_t spBase[6] = {0, rgb(176, 204, 230), rgb(28, 50, 96), rgb(176, 168, 196), rgb(84, 28, 24), rgb(92, 104, 140)};
        if (sp) base = spBase[sp];
        base = darker(base, 0.12f);
        b.fillCircle(x, y, r + R * 0.09f, lighter(base, 0.35f), 18);
        b.fillCircle(x, y, r + R * 0.045f, lighter(base, 0.4f), 34);
        b.fillCircle(x, y, r, lighter(base, 0.5f));  // the lit rim
        b.fillCircle(x, y + std::max(2.f, r * 0.012f), r, base);
        Rng rd;
        rd.s = seed * 2654435761u + 977u;
        const float top = y - r;
        const int h = b.height(), w = b.width();
        auto inside = [&](float px, float py) { return (px - x) * (px - x) + (py - y) * (py - y) < r * r * 0.97f; };
        // bands that follow the curve
        for (int k = 1; k < 9; ++k) {
            const float rr = r - float(k) * std::max(5.f, r * 0.045f) - rd.r(0, 4);
            if (rr <= 0) break;
            b.fillCircle(x, y + float(k) * 0.6f, rr, shade(base, (k % 2) ? 0.9f : 1.07f), 70);
        }
        const int feats = int(std::min(260.f, r * 0.9f));
        for (int i = 0; i < feats; ++i) {
            const float px = rd.r(std::max(0.f, x - r), std::min(float(w), x + r));
            const float py = rd.r(std::max(top, 0.f), std::min(float(h), y + r));
            if (!inside(px, py)) continue;
            if (sp == 1) {  // Cocytus: cracked ice, the Fracture's red showing through
                const float a = rd.r(0, 2 * kPi), l = rd.r(3.f, 12.f);
                if (i % 23 == 0) {
                    b.line(px, py, px + std::cos(a) * l, py + std::sin(a) * l * 0.4f, rgb(180, 20, 30), 4.f, 50);
                    b.line(px, py, px + std::cos(a) * l, py + std::sin(a) * l * 0.4f, rgb(255, 80, 50), 1.f, 170);
                } else if (i % 2 == 0) {
                    b.line(px, py, px + std::cos(a) * l, py + std::sin(a) * l * 0.6f, rgb(240, 250, 255), 1.f, 150);
                }
                continue;
            }
            if (sp == 2) {  // Styx: a storm-dark ocean, white squalls, sparks of lightning
                if (i % 3 == 0) b.fillCircle(px, py, rd.r(2.f, 7.f), rgb(220, 232, 250), 50);
                else if (i % 23 == 0) b.fillCircle(px, py, 1.4f, rgb(220, 236, 255), 230);
                continue;
            }
            if (sp == 3) {  // Lethe: pale, wrapped in mist
                if (i % 2 == 0) b.fillCircle(px, py, rd.r(4.f, 12.f), rgb(236, 234, 246), 40);
                continue;
            }
            if (sp == 4) {  // Phlegethon: a crust split by rivers of fire
                if (i % 2 == 0) {
                    const float a = rd.r(-0.6f, 0.6f);
                    b.line(px, py, px + std::cos(a) * rd.r(5.f, 14.f), py + std::sin(a) * rd.r(2.f, 5.f), rgb(255, 90, 30), 1.6f, 220);
                } else if (i % 5 == 0) {
                    b.fillCircle(px, py, 1.f, rgb(255, 210, 110), 230);
                }
                continue;
            }
            if (sp == 5) {  // Thunder's Edge: streaming cloud bands
                const float l = rd.r(10.f, 34.f);
                b.line(px - l / 2, py, px + l / 2, py + rd.r(-2.f, 2.f), i % 3 ? rgb(196, 208, 232) : rgb(130, 146, 184), rd.r(2.f, 4.f), 70);
                if (i % 29 == 0) b.fillCircle(px, py, 2.f, rgb(230, 240, 255), 200);
                continue;
            }
            if (mecatol) {  // ruins and the lights of the old capital
                const float s = rd.r(1.f, 4.f);
                b.fillRect(int(px), int(py), int(s), int(s * 0.7f), rgb(96, 74, 40), 210);
                if (i % 3 == 0) b.fillCircle(px, py, 0.9f, rgb(255, 230, 150), 230);
            } else if (pd.trait == Trait::Hazardous) {
                if (i % 4 == 0) {
                    const float s = rd.r(2.f, 7.f);
                    b.fillCircle(px, py, s, shade(base, 0.6f), 190);
                    b.fillCircle(px - s * 0.25f, py - s * 0.25f, s * 0.55f, shade(base, 1.25f), 110);
                } else if (i % 7 == 0) {
                    b.line(px, py, px + rd.r(-9, 9), py + rd.r(-3, 3), rgb(255, 120, 40), 1.f, 200);  // lava cracks
                }
            } else if (pd.trait == Trait::Industrial) {
                if (i % 5 == 0) b.fillCircle(px, py, rd.r(4.f, 11.f), shade(base, 0.72f), 150);
                else if (i % 3 == 0) b.fillCircle(px, py, 0.8f, rgb(255, 210, 120), 210);
            } else {
                if (i % 2 == 0) b.fillCircle(px, py, 0.8f, rgb(255, 236, 170), 200);  // city lights
            }
        }
        if (sp == 5) {  // the storm's eye, turning bands round it
            const bool big = r > R;
            const float ex = big ? x - r * 0.13f : x + r * 0.1f, ey = big ? y - r + std::min(r * 0.35f, R * 0.15f) : y - r * 0.15f;
            const float e = big ? R * 0.07f : r * 0.18f, sq = big ? 0.42f : 0.85f;  // (seen low on the horizon: flattened)
            for (int arm = 0; arm < 3; ++arm)  // cloud arms winding in
                for (int j = 0; j < 26; ++j) {
                    const float a = float(arm) * 2.f * kPi / 3.f + float(j) * 0.26f, rad = e * (0.55f + float(j) * 0.16f);
                    b.fillCircle(ex + std::cos(a) * rad, ey + std::sin(a) * rad * sq, e * 0.32f * (1.f - float(j) / 32.f),
                                 j % 3 ? rgb(214, 224, 246) : rgb(150, 166, 204), 80 - j * 2);
                }
            battlefx::ellipse(b, ex, ey, e * 0.5f, e * 0.5f * sq, rgb(40, 48, 72), 220);
        }
        // night side: darker toward the bottom
        for (int yy = std::max(0, int(top)); yy < std::min(h, int(y + r)); ++yy) {
            const float dy = float(yy) + 0.5f - y;
            const float half = std::sqrt(std::max(0.f, r * r - dy * dy));
            const float k = (float(yy) - top) / std::max(1.f, 2.f * r);
            const int a = int(std::min(190.f, k * 300.f * lightAlpha));
            if (a > 0) b.hline(int(x - half), int(x + half), yy, rgb(0, 0, 6), a);
        }
    }

    void renderBackdrop(const Game& g, float scale) {
        bg = std::make_unique<Canvas>(area.w, area.h, scale);  // (0.27.0: at the screen's real size)
        bgArea = area;
        Canvas& b = *bg;
        const float lx = float(area.w) / 2.f, ly = float(area.h) / 2.f;
        const float saveCx = cx, saveCy = cy;
        cx = lx;  // draw in the backdrop's own coordinates
        cy = ly;
        uint32_t top = rgb(5, 7, 16), bot = rgb(12, 16, 32);
        if (anomalies & kNebula) {
            top = rgb(22, 10, 34);
            bot = rgb(40, 18, 58);
        }
        if (anomalies & kScar) {  // Thunder's Edge: the entropic scar
            top = rgb(16, 4, 10);
            bot = rgb(44, 8, 20);
        }
        if (back == Back::RedGiant) bot = rgb(60, 16, 10);
        if (back == Back::Rift) top = rgb(4, 3, 10), bot = rgb(18, 8, 34);
        if (fracNo) top = rgb(16, 3, 8), bot = rgb(42, 6, 16);  // 0.43.2: the Fracture's crimson dark
        if (teSys) top = rgb(6, 9, 20), bot = rgb(24, 32, 54);  // Thunder's Edge: storm light
        if (retro::hd()) top = rgb(10, 4, 20), bot = rgb(30, 10, 40);  // (under the painted sky)
        if (back == Back::Nexus) bot = rgb(36, 10, 46);
        b.verticalGradient(0, 0, area.w, area.h, top, bot);
        Rng r;
        r.s = uint32_t(sysDef) * 7919u + 17u;
        // Voidfall (0.33.1): the system's own painted sky, dimmed so the ships stand out, in place of the
        // stars and the nebula wash
        bool painted = false;
        vfPulse = voidfall::SkyPulse();
        const bool egress = fracNo == 2 || fracNo == 6;
        tearX = 0.f, tearY = -0.15f, tearLen = egress ? 0.62f : 0.f;
        if (retro::hd() && sysDef >= 0) {
            retro::Tile tile;
            retro::setBackground(tile, sysDef, uint32_t(sysDef), nexusOn);
            if (teSys) tile.sky = "te";  // 0.43.2: Thunder's Edge's storm (fracture1..7 have their cracked skies already)
            Image sky;
            const char* sn = voidfall::skyOf(tile);
            if (voidfall::picture(sn, b.pw(), b.ph(), sky)) {
                b.pxBlit(sky, 0, 0);
                b.fillRect(0, 0, area.w, area.h, rgb(8, 3, 16), fracNo ? 70 : 95);
                painted = true;
                if (fracNo) voidfall::skyPulse(sn, b.pw(), b.ph(), vfPulse);
            }
        }
        for (int i = 0; i < (painted ? 0 : fracNo ? 200 : 380); ++i) {  // stars, a few with glints
            const float x = r.r(0, float(area.w)), y = r.r(0, float(area.h));
            const int v = int(r.r(50, 230));
            b.pixel(int(x), int(y), rgb(v, v, std::min(255, v + 25)), int(r.r(110, 255)));
            if (i % 37 == 0) {
                b.fillCircle(x, y, 1.6f, rgb(230, 235, 255), 220);
                b.hline(int(x) - 4, int(x) + 4, int(y), rgb(200, 210, 255), 90);
                b.vline(int(x), int(y) - 4, int(y) + 4, rgb(200, 210, 255), 90);
            }
        }
        // a far-off nebula wash (the Fracture: crimson and ember)
        for (int i = 0; i < (painted || teSys ? 0 : fracNo ? 12 : 7); ++i) {
            const float x = r.r(0, float(area.w)), y = r.r(0, float(area.h) * 0.6f);
            const uint32_t nc = fracNo ? (i % 3 ? rgb(150, 20, 34) : rgb(210, 80, 40)) : i % 2 ? rgb(70, 50, 130) : rgb(40, 70, 130);
            b.fillCircle(x, y, r.r(0.15f, 0.35f) * R, nc, fracNo ? 18 : 14);
        }
        cracks.clear();
        if (fracNo && !painted) {  // 0.43.2: broken space - crimson shards between glowing cracks (an egress: a tear)
            const float hw = float(area.w) * 0.5f / R, hh = float(area.h) * 0.5f / R;
            static const float ix[8] = {0, 0.62f, 0, -0.12f, -0.6f, 0.18f, 0, -0.62f}, iy[8] = {0, -0.5f, 0, -0.3f, -0.45f, -0.25f, 0, -0.48f};
            std::vector<battlefx::Shard> shards;
            battlefx::crackNet(cracks, &shards, uint32_t(fracNo) * 977u + 13u, egress ? 1 : 0, egress ? tearX : ix[fracNo],
                               egress ? tearY : iy[fracNo], tearLen, egress ? 12 : fracNo == 3 || fracNo == 5 ? 14 : 10, -hw, hw, -hh, hh);
            battlefx::drawShards(b, shards, lx, ly, R, 255);
            battlefx::drawCracks(b, cracks, lx, ly, R, 255);
            if (egress) {  // the tear: white light between jagged crimson lips
                for (int k = 6; k >= 0; --k)
                    battlefx::ellipse(b, X(tearX), Y(tearY), R * (0.05f + 0.045f * float(k)), R * tearLen * (0.9f + 0.06f * float(k)),
                                      k > 3 ? rgb(150, 14, 30) : rgb(255, 70, 50), 20);
                battlefx::tear(b, X(tearX), Y(tearY), R * 0.06f, R * tearLen, uint32_t(fracNo), rgb(200, 24, 36), 235);
                battlefx::tear(b, X(tearX), Y(tearY), R * 0.035f, R * tearLen * 0.96f, uint32_t(fracNo) + 50u, rgb(255, 110, 80), 240);
                battlefx::tear(b, X(tearX), Y(tearY), R * 0.016f, R * tearLen * 0.9f, uint32_t(fracNo) + 90u, rgb(255, 248, 240), 255);
            }
        }
        if (teSys && !painted) {  // 0.43.2: Thunder's Edge - a storm turning round a calm eye, the planet in it
            for (int i = 0; i < 14; ++i) {  // far cloud banks
                const float x = r.r(-0.1f, 1.1f) * float(area.w), y = r.r(0.f, 1.f) * float(area.h), rr = r.r(0.12f, 0.3f) * R;
                b.fillCircle(x, y, rr * 1.3f, rgb(60, 72, 104), 18);
                b.fillCircle(x, y, rr, rgb(100, 114, 150), 22);
            }
            const float ex = X(0.f), ey = Y(-0.04f);
            for (int arm = 0; arm < 4; ++arm)  // the arms, winding in
                for (int j = 0; j < 120; ++j) {
                    const float k = float(j) / 119.f, a = float(arm) * kPi / 2.f + k * 4.2f, rad = R * (0.34f + 0.95f * k);
                    const float x = ex + std::cos(a) * rad, y = ey + std::sin(a) * rad * 0.8f, br = R * (0.06f + 0.12f * k);
                    b.fillCircle(x, y, br * 1.3f, rgb(54, 66, 98), 10);
                    b.fillCircle(x, y, br, rgb(124, 138, 176), 14);
                    b.fillCircle(x - br * 0.2f, y - br * 0.25f, br * 0.45f, rgb(200, 210, 236), 12);  // its lit edge
                }
            for (int k = 0; k < 3; ++k) b.ring(ex, ey, R * (0.31f + 0.03f * float(k)), R * 0.025f, rgb(206, 216, 240), 60 - 15 * k);  // the eye wall
        }
        if (anomalies & kScar) {  // a great torn crack across the sky, glowing from inside
            float px[17], py[17];
            for (int i = 0; i < 17; ++i) {
                const float k = float(i) / 16.f;
                px[i] = k * float(area.w);
                py[i] = float(area.h) * (0.18f + 0.22f * k) + (i % 2 ? 1.f : -1.f) * r.r(0.01f, 0.05f) * float(area.h);
            }
            for (int i = 0; i < 16; ++i) b.line(px[i], py[i], px[i + 1], py[i + 1], rgb(200, 30, 60), 0.12f * R, 50);
            for (int i = 0; i < 16; ++i) b.line(px[i], py[i], px[i + 1], py[i + 1], rgb(255, 90, 90), 0.035f * R, 170);
            for (int i = 0; i < 16; ++i) b.line(px[i], py[i], px[i + 1], py[i + 1], rgb(255, 230, 210), 1.5f, 220);
        }
        if (anomalies & kNebula)
            for (int i = 0; i < 14; ++i)
                b.fillCircle(r.r(0, float(area.w)), r.r(0, float(area.h)), r.r(0.18f, 0.42f) * R, i % 3 ? rgb(150, 80, 210) : rgb(220, 140, 240), 22);
        if (anomalies & kAsteroid)
            for (int i = 0; i < 60; ++i) {
                const float x = r.r(0, float(area.w)), y = r.r(0, float(area.h)), s = r.r(0.012f, 0.04f) * R;
                float px[7], py[7];
                for (int k = 0; k < 7; ++k) {
                    const float a = 2 * kPi * float(k) / 7.f, rr = s * r.r(0.7f, 1.25f);
                    px[k] = x + std::cos(a) * rr;
                    py[k] = y + std::sin(a) * rr;
                }
                b.fillPolygon(px, py, 7, rgb(84, 76, 66));
                b.fillCircle(x - s * 0.3f, y - s * 0.3f, s * 0.4f, rgb(140, 130, 112), 120);
            }
        // wormholes of an open-space system: a glowing ring far behind
        if (back == Back::Stars && holes) {
            int k = 0;
            for (uint8_t bit : {kAlpha, kBeta, kGamma, kDelta, kEpsilon}) {
                if (!(holes & bit)) continue;
                const uint32_t wc = bit == kAlpha ? rgb(240, 150, 60) : bit == kBeta ? rgb(90, 200, 110) : bit == kGamma ? rgb(230, 90, 200)
                                  : bit == kDelta ? rgb(200, 120, 230) : rgb(235, 60, 70);
                const float x = X(-0.3f + 0.6f * float(k)), y = Y(-0.35f), rr = 0.2f * R;
                b.fillCircle(x, y, rr * 1.5f, wc, 22);
                b.fillCircle(x, y, rr, darker(wc, 0.8f));
                for (int i = 0; i < 4; ++i) b.ring(x, y, rr * (1.f - 0.2f * float(i)), 2.f, wc, 200 - 40 * i);
                ++k;
            }
        }
        if (back == Back::Horizon && !planets.empty()) {
            const BgPlanet& bp = planets[0];
            paintPlanet(b, X(bp.x), Y(bp.y), bp.r * R, kPlanets[size_t(bp.def)], bp.home, uint32_t(bp.def), 0.8f);
            if (bp.shield) {  // planetary shield: a faint dome over the horizon
                b.fillCircle(X(bp.x), Y(bp.y), (bp.r + 0.16f) * R, rgb(90, 200, 255), 16);
                b.ring(X(bp.x), Y(bp.y), (bp.r + 0.16f) * R, 2.f, rgb(150, 230, 255), 90);
            }
        } else if (back == Back::Planets) {
            for (const BgPlanet& bp : planets) {
                paintPlanet(b, X(bp.x), Y(bp.y), bp.r * R, kPlanets[size_t(bp.def)], bp.home, uint32_t(bp.def), 0.3f);
                if (bp.shield) b.ring(X(bp.x), Y(bp.y), (bp.r + 0.05f) * R, 1.5f, rgb(150, 230, 255), 110);
            }
        } else if (back == Back::RedGiant) {  // the supernova's red giant on the horizon
            const float px = X(0), py = Y(0.42f + 2.1f), pr = 2.1f * R;
            b.fillCircle(px, py, pr + 0.3f * R, rgb(255, 80, 20), 26);
            b.fillCircle(px, py, pr + 0.14f * R, rgb(255, 110, 30), 50);
            b.fillCircle(px, py, pr, rgb(255, 190, 110));
            b.fillCircle(px, py + 3.f, pr, rgb(226, 82, 28));
            for (int i = 0; i < 220; ++i) {
                const float x = r.r(0, float(area.w)), y = r.r(Y(0.42f), float(area.h));
                if ((x - px) * (x - px) + (y - py) * (y - py) > pr * pr) continue;
                b.fillCircle(x, y, r.r(1.f, 4.f), i % 3 ? rgb(255, 140, 50) : rgb(160, 40, 16), 150);
            }
        } else if (back == Back::Rift) {  // a black hole sitting on the bottom edge
            const float px = X(0), py = Y(0.95f);
            for (int i = 6; i >= 0; --i) b.fillCircle(px, py, (0.42f + 0.12f * float(i)) * R, rgb(110 + 15 * i, 50, 200), 26);
            b.fillCircle(px, py, 0.42f * R, rgb(200, 160, 255), 160);
            b.fillCircle(px, py, 0.38f * R, rgb(0, 0, 0));
        } else if (back == Back::Nexus) {  // the giant wormhole of the Nexus below the fight
            const float px = X(0), py = Y(1.05f);
            for (int i = 8; i >= 0; --i) b.fillCircle(px, py, (0.5f + 0.1f * float(i)) * R, i % 2 ? rgb(170, 60, 220) : rgb(90, 30, 140), 40);
            b.fillCircle(px, py, 0.5f * R, rgb(20, 4, 30));
        }
        // a faint hex grid (Advance Wars)
        {
            const float hr = R / 5.2f;
            const float stepX = hr * 1.5f, stepY = hr * kSqrt3;
            for (int col = -1; col * stepX < float(area.w) + hr; ++col)
                for (int row = -1; row * stepY < float(area.h) + hr; ++row) {
                    const float hx = float(col) * stepX, hy = float(row) * stepY + ((col & 1) ? stepY / 2.f : 0.f);
                    for (int k = 0; k < 3; ++k) {
                        const float a0 = kPi / 3.f * float(k), a1 = kPi / 3.f * float(k + 1);
                        b.line(hx + std::cos(a0) * hr, hy + std::sin(a0) * hr, hx + std::cos(a1) * hr, hy + std::sin(a1) * hr,
                               rgb(120, 170, 255), 1.f, 16);
                    }
                }
        }
        (void)g;
        cx = saveCx;
        cy = saveCy;
    }

    // ---------------------------------------------------------- drawing
    // Retrowave (0.49.0): the ship as pixel art, as large as its picture would be (nullptr: no picture / no grid)
    const pxship::Sprite* spriteFor(const Canvas& c, UnitType t, float maxW, float maxH, uint32_t colr) const {
        return pxship::fit(c, grid, t, maxW, maxH, colr);
    }
    bool drawArtPx(Canvas& c, UnitType t, float x, float y, float len, uint32_t colr, const pxship::Look& look, uint32_t now) {
        const pxship::Sprite* sp = spriteFor(c, t, len * R, len * R * (t == UnitType::WarSun ? 1.1f : 0.8f), colr);
        if (!sp) return false;
        pxship::draw(c, *sp, c.fX(X(x)), c.fY(Y(y)), grid, look, now);
        return true;
    }

    void drawArt(Canvas& c, UnitType t, float x, float y, float len, uint32_t colr, bool faceLeft, int a) {
        if (grid.f > 0) {
            pxship::Look k;
            k.faceLeft = faceLeft;
            k.alpha = a;
            k.engines = false;
            if (drawArtPx(c, t, x, y, len, colr, k, 0)) return;
        }
        const int w = std::max(4, int(len * R)), h = std::max(4, int(len * R * (t == UnitType::WarSun ? 1.1f : 0.8f)));
        if (retro::on() && t != UnitType::Fighter)  // Retrowave: a dark rim so ships stand out after the chunky pass (0.28.0)
            for (int k = 0; k < 4; ++k)
                icons::drawUnitArt(c, artName(t), X(x) + (k == 0 ? 1.5f : k == 1 ? -1.5f : 0.f), Y(y) + (k == 2 ? 1.5f : k == 3 ? -1.5f : 0.f), w, h,
                                   rgb(0, 0, 0), faceLeft, a * 210 / 255);
        if (!icons::drawUnitArt(c, artName(t), X(x), Y(y), w, h, colr, faceLeft, a)) c.fillCircle(X(x), Y(y), len * R * 0.3f, colr, a);
    }

    void drawShip(Canvas& c, const Ship& s, uint32_t now) {
        if (s.state == 2) return;
        uint32_t colr = col[s.side];
        int a = 255;
        if (s.state == 1 && s.t == UnitType::Flagship) {  // whiter and whiter as it implodes
            const float k = std::min(1.f, float(int32_t(now - s.dieAt)) / std::max(1.f, 1400.f * speed + 450.f));
            colr = lighter(colr, k * 0.9f);
        }
        if (s.moon) {  // a shaded moon in the background
            const uint32_t mc = mix(colr, rgb(14, 16, 26), 165);
            c.fillCircle(X(s.x), Y(s.y), s.len * R * 0.52f, rgb(0, 0, 6), 70);
            drawArt(c, s.t, s.x, s.y, s.len, mc, s.faceLeft, s.state == 1 ? 230 : 175);
            c.fillCircle(X(s.x) - s.len * R * 0.18f, Y(s.y) - s.len * R * 0.2f, s.len * R * 0.3f, lighter(colr, 0.3f), 14);  // lit side
            if (s.chargeAt) {  // charging up, the shot coming out at the viewer
                const float k = float(int32_t(now - s.chargeAt)) / 600.f;
                const float mx = X(s.x), my = Y(s.y + s.len * 0.12f);
                for (int i = 0; i < 5; ++i)
                    c.fillCircle(mx, my, (0.02f + 0.06f * k) * R * (1.f - 0.15f * float(i)), hue(float(now % 1000) / 1000.f + 0.15f * float(i)),
                                 int(60 + 120 * k));
                c.fillCircle(mx, my, 0.02f * R + 4.f * k, rgb(255, 255, 255), 230);
            }
            return;
        }
        // Retrowave (0.49.0): pixel art - engine flames at its nozzles, a burning breach when damaged
        if (grid.f > 0) {
            pxship::Look k;
            k.faceLeft = s.faceLeft;
            k.alpha = a;
            k.engines = s.state == 0 && s.t != UnitType::WarSun;
            k.seed = uint32_t(s.phase * 1000.f) + uint32_t(s.ordinal) * 131u + uint32_t(s.side) * 7u;
            if (s.state == 1 && s.t == UnitType::Flagship)
                k.whiten = int(255.f * std::min(1.f, float(int32_t(now - s.dieAt)) / std::max(1.f, 1400.f * speed + 450.f)) * 0.9f);
            if (const pxship::Sprite* sp = spriteFor(c, s.t, s.len * R, s.len * R * 0.8f, col[s.side])) {
                pcolours::Finish fin(c, pcolours::around(X(s.x), Y(s.y), s.len * R * 0.6f, s.len * R * 0.45f), s.state == 0 ? pci[s.side] : -1,
                                     uint32_t(s.ordinal) * 131u + uint32_t(s.side));
                if (s.damaged) {
                    k.hurt = 1;
                    k.hurtX = sp->w / 2 + int(std::lround(c.fx(s.sx * R) / float(grid.f)));
                    k.hurtY = sp->h / 2 + int(std::lround(c.fx(s.sy * R) / float(grid.f)));
                }
                pxship::draw(c, *sp, c.fX(X(s.x)), c.fY(Y(s.y)), grid, k, now);
                return;
            }
        }
        // Voidfall (0.33.1): the box art's long engine flames
        if (s.state == 0 && s.t != UnitType::WarSun && retro::hd()) {
            const float bx = X(s.x) + (s.faceLeft ? 0.45f : -0.45f) * s.len * R;
            const float half = s.len * R * (s.t == UnitType::Fighter ? 0.1f : 0.06f);
            voidfall::flame(c, bx, Y(s.y), s.faceLeft ? 1.f : -1.f, s.len * R * (s.t == UnitType::Fighter ? 0.7f : 0.45f), half, now,
                            uint32_t(s.phase * 100.f) + uint32_t(s.t) * 7u, 230);
        } else if (s.state == 0 && s.t != UnitType::WarSun) {  // engine glow
            const float flick = 0.75f + 0.25f * std::sin(float(now % 300) / 300.f * 2 * kPi + s.phase);
            const float bx = X(s.x) + (s.faceLeft ? 0.47f : -0.47f) * s.len * R;
            const float half = s.len * R * (s.t == UnitType::Fighter ? 0.12f : 0.07f);
            c.fillCircle(bx, Y(s.y), half * 1.5f * flick, rgb(255, 160, 60), 110);
            c.fillCircle(bx, Y(s.y), half * 0.7f * flick, rgb(255, 240, 190), 190);
        }
        {
            pcolours::Finish fin(c, pcolours::around(X(s.x), Y(s.y), s.len * R * 0.6f, s.len * R * 0.45f), s.state == 0 ? pci[s.side] : -1,
                                 uint32_t(s.ordinal) * 131u + uint32_t(s.side));
            drawArt(c, s.t, s.x, s.y, s.len, colr, s.faceLeft, a);
        }
        if (s.damaged) {  // the breach glows
            const float ox = s.faceLeft == (s.side == 1) ? s.sx : -s.sx;
            const float fl = 0.6f + 0.4f * std::sin(float(now % 200) / 200.f * 2 * kPi);
            c.fillCircle(X(s.x + ox), Y(s.y + s.sy), 2.5f * fl, rgb(255, 150, 50), 220);
        }
    }

    void drawBars(Canvas& c, const Game& g, const Ship& s) {
        if (s.state != 0 || s.t == UnitType::Fighter) return;
        // a health bar and the faction badge under each ship (Advance Wars)
        const float w = std::min(40.f, std::max(18.f, s.len * R * 0.7f));
        const float bx = X(s.x) - w / 2.f + (s.moon ? 0.f : 5.f), by = Y(s.y) + s.len * R * (s.moon ? 0.5f : 0.3f) + 2.f;
        c.fillRect(int(bx) - 1, int(by) - 1, int(w) + 2, 5, rgb(0, 0, 0), 190);
        const float k = s.damaged ? 0.5f : 1.f;
        const uint32_t fill = s.damaged ? rgb(240, 90, 60) : lighter(col[s.side], 0.25f);
        c.fillRect(int(bx), int(by), int(w * k), 3, fill, 230);
        if (s.hp > 1) c.vline(int(bx + w / 2.f), int(by), int(by) + 2, rgb(0, 0, 0), 200);
        if (!s.moon) drawFactionIcon(c, fac[s.side], int(bx) - 11, int(by) - 4, 10, 220);
        if (s.t == UnitType::Flagship && !(s.second ? flagName2 : flagName)[s.side].empty()) {
            std::string n = (s.second ? flagName2 : flagName)[s.side];
            for (char& ch : n) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            drawTextAligned(c, FONT_XS, int(X(s.x)) - 60, int(by) + 4, 120, Align::Center, n, lighter(col[s.side], 0.5f), 230);
        }
        (void)g;
    }

    void drawPiece(Canvas& c, const Piece& p) {
        const float x = X(p.ox + p.dx), y = Y(p.oy + p.dy);
        const Rect keep = c.clip();
        Rect r{int(x + p.u0 * float(p.w)), int(y + p.v0 * float(p.h)), int((p.u1 - p.u0) * float(p.w)) + 1, int((p.v1 - p.v0) * float(p.h)) + 1};
        // intersect with the current clip
        const int x0 = std::max(keep.x, r.x), y0 = std::max(keep.y, r.y);
        const int x1 = std::min(keep.x + keep.w, r.x + r.w), y1 = std::min(keep.y + keep.h, r.y + r.h);
        if (x1 <= x0 || y1 <= y0) return;
        c.setClip(Rect{x0, y0, x1 - x0, y1 - y0});
        const pxship::Sprite* sp = spriteFor(c, p.t, float(p.w), float(p.h), p.col);
        if (sp) {  // Retrowave (0.49.0): the pixel-art ship's piece
            pxship::Look k;
            k.faceLeft = p.faceLeft;
            k.alpha = 205;
            k.engines = false;
            pxship::draw(c, *sp, c.fX(x), c.fY(y), grid, k, 0);
        } else {
            icons::drawUnitArt(c, artName(p.t), x, y, p.w, p.h, p.col, p.faceLeft, 205);
        }
        c.setClip(keep);
        // a jagged burnt edge where it broke
        const float ex = p.u0 > -0.49f ? x + p.u0 * float(p.w) : p.u1 < 0.49f ? x + p.u1 * float(p.w) : -1.f;
        if (ex >= 0) c.vline(int(ex), int(y + p.v0 * float(p.h) * 0.5f), int(y + p.v1 * float(p.h) * 0.5f), rgb(255, 120, 40), 90);
    }

    void drawShot(Canvas& c, const Shot& s, uint32_t now) {
        if (int32_t(now - s.born) < 0) return;
        const float k = float(int32_t(now - s.born)) / float(std::max<uint32_t>(1, s.arrive - s.born));
        float x = s.x0 + (s.x1 - s.x0) * k, y = s.y0 + (s.y1 - s.y0) * k;
        const float dx = s.x1 - s.x0, dy = s.y1 - s.y0, d = std::sqrt(dx * dx + dy * dy) + 1e-4f;
        const float ux = dx / d, uy = dy / d;
        if (s.kind == Missile || s.kind == Torpedo) {  // a little sway
            const float w = std::sin(k * kPi * 3.f + s.wob * 3.f) * 0.02f * (1.f - k);
            x += -uy * w;
            y += ux * w;
        }
        const float px = X(x), py = Y(y);
        switch (s.kind) {
            case Laser: {
                const float L = 0.045f * R;
                c.line(px - ux * L, py - uy * L, px, py, s.col, 3.f, 90);
                c.line(px - ux * L, py - uy * L, px, py, lighter(s.col, 0.5f), 1.2f, 255);
                break;
            }
            case Rail: {
                const float L = 0.16f * R;
                c.line(px - ux * L, py - uy * L, px, py, s.col, 3.5f, 80);
                c.line(px - ux * L * 0.7f, py - uy * L * 0.7f, px, py, rgb(230, 245, 255), 1.3f, 255);
                break;
            }
            case Missile:
            case Torpedo: {
                const float L = (s.kind == Torpedo ? 0.035f : 0.024f) * R;
                c.line(px - ux * L, py - uy * L, px, py, s.col, s.kind == Torpedo ? 3.5f : 2.5f, 255);
                c.fillCircle(px - ux * L, py - uy * L, 2.f, rgb(255, 200, 120), 220);
                break;
            }
            case Rocket: {
                const float L = 0.02f * R;
                c.line(px - ux * L, py - uy * L, px, py, s.col, 2.5f, 255);
                c.fillCircle(px - ux * L, py - uy * L, 1.8f, rgb(255, 140, 60), 230);
                break;
            }
            case Bolt:
                c.fillCircle(px, py, 4.f, s.col, 90);
                c.fillCircle(px, py, 2.f, rgb(230, 255, 255), 255);
                break;
            case Tracer: {
                const float L = 0.06f * R;
                c.line(px - ux * L, py - uy * L, px, py, s.col, 1.2f, 200);
                break;
            }
            case RedBolt: {
                const float L = 0.05f * R;
                c.line(px - ux * L, py - uy * L, px, py, s.col, 3.f, 110);
                c.line(px - ux * L, py - uy * L, px, py, rgb(255, 200, 190), 1.f, 255);
                break;
            }
            case Gauss: {  // a white-hot slug and the long ionised trail it tears through space
                const float travelled = std::sqrt((px - X(s.x0)) * (px - X(s.x0)) + (py - Y(s.y0)) * (py - Y(s.y0)));
                const float L = std::min(travelled, 0.42f * R);
                c.line(px - ux * L, py - uy * L, px, py, rgb(60, 120, 255), 8.f, 45);
                c.line(px - ux * L * 0.75f, py - uy * L * 0.75f, px, py, s.col, 4.f, 120);
                c.line(px - ux * L * 0.45f, py - uy * L * 0.45f, px, py, rgb(235, 248, 255), 1.8f, 255);
                c.fillCircle(px, py, 0.012f * R + 2.f, rgb(255, 255, 255), 255);
                c.fillCircle(px, py, 0.03f * R + 3.f, rgb(150, 210, 255), 70);
                break;
            }
            default: break;
        }
    }

    void drawBeam(Canvas& c, Beam& b, uint32_t now) {
        if (b.src >= 0 && b.src < int(ships.size())) {
            const Ship& s = ships[size_t(b.src)];
            b.x0 = s.moon ? s.x : s.x + (s.faceLeft ? -1.f : 1.f) * s.len * 0.4f;
            b.y0 = s.moon ? s.y + s.len * 0.12f : s.y;
        }
        if (b.dst >= 0 && b.dst < int(ships.size()) && ships[size_t(b.dst)].state == 0) {
            b.x1 = ships[size_t(b.dst)].x;
            b.y1 = ships[size_t(b.dst)].y;
        }
        const float age = float(int32_t(now - b.born)) / float(std::max<uint32_t>(1, b.life));
        const int a = int(255.f * (age < 0.15f ? age / 0.15f : 1.f - std::max(0.f, age - 0.6f) / 0.4f));
        const float x0 = X(b.x0), y0 = Y(b.y0), x1 = X(b.x1), y1 = Y(b.y1);
        if (b.kind == Rainbow) {
            const float dx = x1 - x0, dy = y1 - y0, d = std::sqrt(dx * dx + dy * dy) + 1e-4f;
            const float nx = -dy / d, ny = dx / d;
            for (int i = 0; i < 7; ++i) {
                const float o = (float(i) - 3.f) * 1.6f;
                c.line(x0 + nx * o * 2.f, y0 + ny * o * 2.f, x1 + nx * o * 0.5f, y1 + ny * o * 0.5f, hue(float(i) / 7.f + float(now % 800) / 800.f),
                       2.f, a);
            }
            c.line(x0, y0, x1, y1, rgb(255, 255, 255), 1.5f, a);
            c.fillCircle(x0, y0, 0.06f * R, rgb(255, 255, 255), a / 2);
            c.fillCircle(x1, y1, 0.035f * R, hue(float(now % 600) / 600.f), a * 3 / 4);
            return;
        }
        const float th = b.kind == RedBeam ? 5.f : 4.f;
        c.line(x0, y0, x1, y1, b.col, th * 2.f, a / 4);
        c.line(x0, y0, x1, y1, b.col, th, a * 3 / 4);
        c.line(x0, y0, x1, y1, lighter(b.col, 0.7f), 1.5f, a);
        c.fillCircle(x1, y1, th * 1.4f, lighter(b.col, 0.4f), a / 2);
    }

    // ---------------------------------------------------------- Retrowave (0.49.0): the effects as pixel art on the grid
    static uint32_t hashU(uint32_t h) { return pxship::hash(h); }
    static float dith(int gx, int gy) { return pxship::dith(gx, gy); }
    int gxOf(const Canvas& c, float x) const { return pxship::gridX(grid, c.fX(X(x))); }
    int gyOf(const Canvas& c, float y) const { return pxship::gridY(grid, c.fY(Y(y))); }
    float gridLen(const Canvas& c, float r) const { return c.fx(r * R) / float(grid.f); }  // radii -> grid pixels
    void gpx(Canvas& c, int gx, int gy, uint32_t col, int a = 255) const { pxship::dot(c, grid, gx, gy, col, a); }
    void fireballPx(Canvas& c, int cx, int cy, float rr, float k, uint32_t seed, uint32_t now) const { pxship::fireball(c, grid, cx, cy, rr, k, seed, now); }
    void drawBoomPx(Canvas& c, const Boom& b, float k, uint32_t now) {
        const int cx = gxOf(c, b.x), cy = gyOf(c, b.y);
        const float r = std::max(1.f, gridLen(c, b.r));
        const uint32_t seed = b.born * 2654435761u;
        switch (b.kind) {
            case Fire:
            case Big: {
                const float rr = r * (0.45f + 0.9f * std::sqrt(k));
                fireballPx(c, cx, cy, rr, k, seed, now);
                if (b.kind == Big) {  // the shock ring and flying debris
                    const float sr = rr * 1.7f;
                    const int steps = std::max(16, int(sr * 6.3f));
                    for (int i = 0; i < steps; ++i) {
                        const float a = 2.f * kPi * float(i) / float(steps);
                        const int x = cx + int(std::lround(std::cos(a) * sr)), y = cy + int(std::lround(std::sin(a) * sr * 0.8f));
                        if (dith(x, y) < k * 1.1f) continue;
                        gpx(c, x, y, k < 0.4f ? rgb(255, 236, 190) : rgb(255, 160, 80));
                    }
                    for (int i = 0; i < 10; ++i) {
                        const uint32_t h = hashU(seed + uint32_t(i) * 977u);
                        const float a = float(h % 6283u) / 1000.f, sp = 1.f + float((h >> 13) % 100u) / 60.f;
                        const int x = cx + int(std::lround(std::cos(a) * rr * sp * (0.6f + k))), y = cy + int(std::lround(std::sin(a) * rr * sp * (0.6f + k)));
                        gpx(c, x, y, k < 0.5f ? rgb(255, 190, 90) : rgb(110, 70, 50));
                    }
                }
                break;
            }
            case Shaded: {  // a war sun's dull hit: a grey-brown puff
                const float rr = r * (0.5f + k);
                const int n = int(std::ceil(rr)) + 1;
                for (int dy = -n; dy <= n; ++dy)
                    for (int dx = -n; dx <= n; ++dx) {
                        const float d = std::sqrt(float(dx * dx + dy * dy)) / std::max(0.5f, rr);
                        if (d > 1.f || dith(cx + dx, cy + dy) < k) continue;
                        gpx(c, cx + dx, cy + dy, d < 0.5f ? rgb(160, 110, 80) : rgb(70, 60, 60));
                    }
                break;
            }
            case Flash: pxship::flash(c, grid, cx, cy, r, k); break;
            case Muzzle: pxship::muzzle(c, grid, cx, cy, r, k); break;
            default: break;
        }
    }
    void drawPartPx(Canvas& c, const Part& p, float k, int a, uint32_t now) {
        const int x = gxOf(c, p.x), y = gyOf(c, p.y);
        switch (p.kind) {
            case Spark: pxship::spark(c, grid, x, y, p.vx, p.vy, p.col, k); break;
            case Smoke:
            case Puff:
                pxship::puff(c, grid, x, y, gridLen(c, p.size), float(a) / float(std::max(1, p.a0)), uint32_t(p.born) * 2654435761u + uint32_t(int(p.x * 1000.f)), p.col,
                             std::min(255, 140 + a / 2));
                break;
            case Chunk: {  // a scrap of hull: two to four pixels, a lit one and a dark one
                const int s = std::max(1, int(std::lround(gridLen(c, p.size))));
                for (int dy = 0; dy < s; ++dy)
                    for (int dx = 0; dx < s + 1; ++dx) gpx(c, x + dx, y + dy, (dx + dy) == 0 ? lighter(p.col, 0.3f) : darker(p.col, 0.25f), a);
                break;
            }
            case Ember:
                if ((now / 90u + uint32_t(p.born)) % 3u) gpx(c, x, y, p.col, a);
                break;
            default: break;
        }
    }
    void drawRingPx(Canvas& c, const Ring& r, float k) {
        const float rx = gridLen(c, r.maxR) * std::sqrt(k), ry = rx * r.flat;
        const int cx = gxOf(c, r.x), cy = gyOf(c, r.y);
        const int steps = std::max(16, int(rx * 6.4f));
        for (int i = 0; i < steps; ++i) {
            const float a = 2.f * kPi * float(i) / float(steps);
            const int x = cx + int(std::lround(std::cos(a) * rx)), y = cy + int(std::lround(std::sin(a) * ry));
            if (dith(x, y) < k) continue;
            gpx(c, x, y, k < 0.35f ? lighter(r.col, 0.55f) : r.col);
        }
    }
    // a sustained hit: the ship's shield flares - a bubble of pale light with a hex weave, fading out
    void drawShieldPx(Canvas& c, const Ship& s, uint32_t now) {
        if (!s.shieldAt) return;
        const float k = float(int32_t(now - s.shieldAt)) / 700.f;
        if (k < 0.f || k >= 1.f) return;
        const int cx = gxOf(c, s.x), cy = gyOf(c, s.y);
        const float rx = gridLen(c, s.len * (s.moon ? 0.5f : 0.58f)), ry = s.moon ? rx : rx * 0.62f;
        const int nx = int(std::ceil(rx)) + 1, ny = int(std::ceil(ry)) + 1;
        const uint32_t seed = s.shieldAt * 2654435761u;
        // the bubble's rim, thinning to sparkles; where the hit landed it shines brightest
        for (int dy = -ny; dy <= ny; ++dy)
            for (int dx = -nx; dx <= nx; ++dx) {
                const float d = std::sqrt(float(dx * dx) / (rx * rx) + float(dy * dy) / (ry * ry));
                if (d > 1.f || d < 0.86f) continue;
                const float jag = float(hashU(seed + uint32_t(dx) * 131u + uint32_t(dy) * 7919u) % 1000u) / 1000.f;
                if (jag < k * 1.1f) continue;
                const bool lit = dy < 0 || (s.faceLeft ? dx < 0 : dx > 0);
                gpx(c, cx + dx, cy + dy, k < 0.18f ? rgb(245, 252, 255) : lit ? rgb(150, 225, 255) : rgb(80, 150, 230), s.moon ? 170 : 235);
            }
    }

    void drawBoom(Canvas& c, const Boom& b, uint32_t now) {
        if (int32_t(now - b.born) < 0) return;
        const float k = float(int32_t(now - b.born)) / float(std::max<uint32_t>(1, b.life));
        if (grid.f > 0) {
            drawBoomPx(c, b, k, now);
            return;
        }
        const float x = X(b.x), y = Y(b.y), r = b.r * R;
        switch (b.kind) {
            case Fire:
            case Big: {
                const float rr = r * (0.4f + 0.9f * std::sqrt(k));
                c.fillCircle(x, y, rr * 1.25f, rgb(200, 70, 30), int((b.kind == Big ? 70 : 110) * (1 - k)));
                c.fillCircle(x, y, rr, rgb(255, 120, 40), int(220 * (1 - k)));
                c.fillCircle(x, y, rr * 0.65f, rgb(255, 210, 110), int(240 * (1 - k)));
                c.fillCircle(x, y, rr * 0.3f, rgb(255, 255, 230), int(255 * (1 - k)));
                if (b.kind == Big) c.ring(x, y, rr * 1.6f, 2.f, rgb(255, 220, 160), int(180 * (1 - k)));
                break;
            }
            case Shaded:
                c.fillCircle(x, y, r * (0.5f + k), rgb(70, 60, 60), int(200 * (1 - k)));
                c.fillCircle(x, y, r * 0.5f * (0.5f + k), rgb(160, 110, 80), int(160 * (1 - k)));
                break;
            case Flash:
                c.fillCircle(x, y, r * (0.6f + 0.8f * k), rgb(255, 255, 255), int(240 * (1 - k)));
                break;
            case Muzzle:
                c.fillCircle(x, y, r * (1.f + k), rgb(255, 220, 130), int(220 * (1 - k)));
                c.fillCircle(x, y, r * 0.5f, rgb(255, 255, 240), int(255 * (1 - k)));
                break;
            default: break;
        }
    }

    void drawPart(Canvas& c, const Part& p, uint32_t now) {
        if (int32_t(now - p.born) < 0) return;
        const float k = p.life ? float(int32_t(now - p.born)) / float(p.life) : 0.f;
        const int a = p.life ? int(float(p.a0) * (1.f - k)) : p.a0;
        if (grid.f > 0) {
            drawPartPx(c, p, k, a, now);
            return;
        }
        const float x = X(p.x), y = Y(p.y);
        switch (p.kind) {
            case Spark:
                c.line(x, y, x - p.vx * R * 0.03f, y - p.vy * R * 0.03f, p.col, 1.f, a);
                break;
            case Smoke:
            case Puff:
                c.fillCircle(x, y, std::max(1.f, p.size * R), p.col, a / (p.kind == Smoke ? 1 : 1));
                break;
            case Chunk: {
                const float s = std::max(1.5f, p.size * R);
                float xs[3], ys[3];
                for (int i = 0; i < 3; ++i) {
                    const float ang = p.rot + 2.1f * float(i);
                    xs[i] = x + std::cos(ang) * s;
                    ys[i] = y + std::sin(ang) * s * 0.8f;
                }
                c.fillPolygon(xs, ys, 3, p.col, a);
                break;
            }
            case Ember:
                c.fillCircle(x, y, 1.5f, p.col, a);
                break;
            default: break;
        }
    }

    void drawRing(Canvas& c, const Ring& r, uint32_t now) {
        if (int32_t(now - r.born) < 0) return;
        const float k = float(int32_t(now - r.born)) / float(std::max<uint32_t>(1, r.life));
        if (grid.f > 0) {
            drawRingPx(c, r, k);
            return;
        }
        const float rx = r.maxR * R * std::sqrt(k), ry = rx * r.flat;
        const int a = int(230 * (1.f - k));
        float px = 0, py = 0;
        for (int i = 0; i <= 48; ++i) {
            const float ang = 2 * kPi * float(i) / 48.f;
            const float x = X(r.x) + std::cos(ang) * rx, y = Y(r.y) + std::sin(ang) * ry;
            if (i) {
                c.line(px, py, x, y, r.col, 5.f, a / 3);
                c.line(px, py, x, y, lighter(r.col, 0.5f), 2.f, a);
            }
            px = x;
            py = y;
        }
    }

    void drawStruct(Canvas& c, const Game& g, const Struct& st, uint32_t now) {
        const uint32_t colr = st.owner == kNeutral ? theme::neutral : st.owner >= 0 ? pcolours::base(g.players[size_t(st.owner)].color) : theme::text;
        const float sz = st.size;
        if (st.t == UnitType::SpaceDock) {  // in the background, small and dim, its lights blinking
            const int w = std::max(4, int(sz * R));
            icons::drawUnitArt(c, "unit/spacedock", X(st.x), Y(st.y), w, w, darker(colr, 0.35f), false, 210);
            if ((now / 600 + uint32_t(st.x * 100)) % 2) c.fillCircle(X(st.x) + float(w) * 0.3f, Y(st.y) - float(w) * 0.25f, 1.5f, rgb(255, 220, 120), 230);
            return;
        }
        const int w = std::max(4, int(sz * R));
        if (st.gauss) {  // a long rail laid flat, glowing as it charges
            const float dir = st.side == 0 ? 1.f : -1.f;
            const float x0 = X(st.x - dir * sz * 0.6f), x1 = X(st.x + dir * sz * 0.6f), y = Y(st.y);
            c.line(x0, y, x1, y, darker(colr, 0.4f), std::max(3.f, sz * R * 0.18f), 255);
            c.line(x0, y - sz * R * 0.07f, x1, y - sz * R * 0.07f, colr, std::max(1.f, sz * R * 0.05f), 255);
            c.line(x0, y + sz * R * 0.07f, x1, y + sz * R * 0.07f, colr, std::max(1.f, sz * R * 0.05f), 255);
            icons::drawUnitArt(c, "unit/pds", x0, y, w / 2, w / 2, colr, st.side == 1, 255);
            const int32_t left = int32_t(st.nextFire - now);
            if (left > 0 && left < 700) {  // charging: light runs along the rails to the muzzle
                const float k = 1.f - float(left) / 700.f;
                c.fillCircle(x0 + (x1 - x0) * k, y, 1.5f + 3.f * k, rgb(170, 220, 255), 230);
                c.fillCircle(x1, y, (0.01f + 0.03f * k) * R, rgb(200, 235, 255), int(80 + 150 * k));
            }
            return;
        }
        icons::drawUnitArt(c, "unit/pds", X(st.x), Y(st.y), w, w, colr, false, 255);
    }

    void ripple(Canvas& c, uint32_t now) {
        if (!rippleUntil || int32_t(now - rippleUntil) >= 0) return;
        const float k = float(int32_t(now - rippleAt)) / float(std::max<uint32_t>(1, rippleUntil - rippleAt));
        const float amp = 7.f * (1.f - k);
        const float wave = k * float(area.h) * 1.6f;  // a gravity wave sweeping out
        // (0.27.0: in real pixels; the wave is worked out in game units)
        const Rect cl = c.pxClip();
        const Rect pa = c.toPx(area);
        const int x0 = std::max(cl.x, pa.x), x1 = std::min(cl.x + cl.w, pa.x + pa.w);
        if (x1 - x0 < 4) return;
        std::vector<uint32_t> row(size_t(x1 - x0));
        uint32_t* px = c.pixels();
        const int W = c.pw();
        const float inv = 1.f / c.scale();
        for (int y = std::max(cl.y, pa.y); y < std::min(cl.y + cl.h, pa.y + pa.h); ++y) {
            const float ly = c.unscaled() ? float(y) : float(y) * inv;
            const float dy = std::fabs(ly - (cy + rippleAt % 7)) - wave * 0.4f;
            float off = amp * std::sin(ly * 0.11f + float(now % 3600000u) * 0.018f);
            off += amp * 1.6f / (1.f + dy * dy / 300.f) * std::sin(float(now % 3600000u) * 0.03f);  // (no expf: the Miyoo's glibc is too old)
            const int o = int(c.unscaled() ? off : off * c.scale());
            if (o == 0) continue;
            uint32_t* line = px + size_t(y) * size_t(W);
            std::copy(line + x0, line + x1, row.begin());
            for (int x = x0; x < x1; ++x) {
                const int sx = std::max(x0, std::min(x1 - 1, x - o));
                line[x] = row[size_t(sx - x0)];
            }
        }
    }

    // 0.43.2: the Fracture's light running down its cracks (an egress: motes falling into the tear) and
    // Thunder's Edge's lightning - in the sky and inside the storm world's clouds
    void specialBack(Canvas& c, uint32_t now) {
        const float t = float(now % 3600000u) / 1000.f;
        if (fracNo) {
            const bool egress = tearLen > 0.f;
            if (!vfPulse.at.empty()) {
                const Rect pa = c.toPx(area);
                voidfall::drawSkyPulse(c, vfPulse, pa.x, pa.y, now, 230);
            } else if (!cracks.empty()) {
                battlefx::drawCrackPulse(c, cracks, cx, cy, R, now, egress ? 2.6f : 3.4f, egress ? -1 : 1, 230);
            }
            if (egress) {
                const float br = 0.5f + 0.5f * std::sin(t * 2.4f);
                if (!retro::hd()) {
                    battlefx::tear(c, X(tearX), Y(tearY), R * (0.08f + 0.05f * br), R * tearLen * 1.05f, uint32_t(fracNo) + 20u, rgb(255, 80, 70),
                                   int(20 + 26 * br));
                    battlefx::tear(c, X(tearX), Y(tearY), R * 0.01f, R * tearLen * 0.86f, uint32_t(fracNo) + 90u, rgb(255, 255, 255), int(110 + 110 * br));
                }
                for (int i = 0; i < 30; ++i) {  // motes of light drawn in from both sides
                    const float side = i % 2 ? 1.f : -1.f, y0 = -0.85f + 1.5f * hashF(uint32_t(i), 1), sp = 0.18f + 0.22f * hashF(uint32_t(i), 2);
                    const float ph = std::fmod(t * sp + hashF(uint32_t(i), 3), 1.f);
                    const float x = tearX + side * (1.f - ph) * 1.1f, y = y0 + (tearY - y0) * ph * 0.45f;
                    c.fillCircle(X(x), Y(y), std::max(1.f, R * (0.005f + 0.008f * ph)), mix(rgb(220, 40, 50), rgb(255, 240, 230), int(255 * ph)),
                                 int(30 + 200 * ph));
                }
            }
        }
        if (teSys) {
            const uint32_t per = 2100u, ph = now % per, slot = now / per;
            if (ph < 320) {  // a bolt through the clouds and the whole sky lit
                const int a = 255 - int(ph) * 255 / 320;
                const float x0 = -0.95f + 1.9f * hashF(slot, 1), x1 = x0 + 0.9f * (hashF(slot, 2) - 0.5f), y1 = -0.3f + 0.35f * hashF(slot, 3);
                battlefx::bolt(c, X(x0), Y(-0.9f), X(x1), Y(y1), slot * 7u + 3u, R * 0.07f, a, std::max(1.f, R * 0.006f), true);
                c.fillRect(area.x, area.y, area.w, area.h, rgb(180, 205, 255), a / 9);
            }
            if (!planets.empty()) {  // flashes inside the storm world's clouds
                const BgPlanet& bp = planets[0];
                for (int k = 0; k < 3; ++k) {
                    const uint32_t per2 = 700u + 230u * uint32_t(k), p2 = (now + 311u * uint32_t(k)) % per2, s2 = (now + 311u * uint32_t(k)) / per2;
                    if (p2 > 170) continue;
                    float px, py;
                    if (back == Back::Horizon) {
                        px = -0.85f + 1.7f * hashF(s2, k);
                        const float surf = bp.y - std::sqrt(std::max(0.f, bp.r * bp.r - px * px));
                        py = surf + 0.04f + 0.22f * hashF(s2, k + 5);
                    } else {
                        const float a = 2.f * 3.14159265f * hashF(s2, k), rr = bp.r * 0.75f * hashF(s2, k + 5);
                        px = bp.x + std::cos(a) * rr;
                        py = bp.y + std::sin(a) * rr;
                    }
                    const int a2 = 230 - int(p2) * 230 / 170;
                    c.fillCircle(X(px), Y(py), R * 0.1f, rgb(190, 214, 255), a2 / 5);
                    c.fillCircle(X(px), Y(py), R * 0.035f, rgb(236, 244, 255), a2 / 2);
                    battlefx::bolt(c, X(px - 0.06f), Y(py - 0.02f), X(px + 0.07f), Y(py + 0.03f), s2 * 13u + uint32_t(k), R * 0.02f, a2,
                                   std::max(1.f, R * 0.004f), true);
                }
            }
        }
    }

    void animatedBack(Canvas& c, uint32_t now) {
        const float t = float(now % 20000) / 20000.f;
        if (back == Back::RedGiant) {
            const float pulse = 0.5f + 0.5f * std::sin(t * 2 * kPi * 6.f);
            c.fillCircle(X(0), Y(0.42f + 2.1f), (2.1f + 0.05f + 0.04f * pulse) * R, rgb(255, 120, 40), int(18 + 18 * pulse));
        } else if (back == Back::Rift) {
            for (int i = 0; i < 36; ++i) {
                const float k = float(i) / 36.f, a = k * 4 * kPi - t * 2 * kPi * 6.f, rr = (0.95f - 0.5f * k) * R;
                c.fillCircle(X(0) + std::cos(a) * rr, Y(0.95f) + std::sin(a) * rr * 0.35f, 1.5f + 2.f * (1 - k), rgb(210, 180, 255), 200);
            }
        } else if (back == Back::Nexus) {
            for (int i = 0; i < 40; ++i) {
                const float k = float(i) / 40.f, a = k * 6 * kPi + t * 2 * kPi * 4.f, rr = (1.3f - 0.75f * k) * R;
                c.fillCircle(X(0) + std::cos(a) * rr, Y(1.05f) + std::sin(a) * rr * 0.4f, 1.5f + 1.5f * (1 - k), rgb(240, 140, 255), 190);
            }
        } else if (back == Back::Horizon && !planets.empty() && planets[0].shield) {
            // the shield shimmers
            const BgPlanet& bp = planets[0];
            const float rr = (bp.r + 0.16f) * R, ox = X(bp.x), oy = Y(bp.y);
            for (int i = 0; i < 3; ++i) {
                const float c0 = -kPi / 2.f - 0.5f + std::fmod(t * 9.f + float(i) * 0.37f, 1.f) * 1.0f;
                float px = 0, py = 0;
                for (int k = 0; k <= 10; ++k) {
                    const float a = c0 + 0.03f * float(k);
                    const float x = ox + std::cos(a) * rr, y = oy + std::sin(a) * rr;
                    if (k) c.line(px, py, x, y, rgb(170, 240, 255), 2.f, 110);
                    px = x;
                    py = y;
                }
            }
        }
        if (anomalies & kAsteroid) {  // a few big rocks drifting in front
            Rng r;
            r.s = uint32_t(sysDef) + 99u;
            for (int i = 0; i < 6; ++i) {
                const float sp = r.r(0.01f, 0.03f), y = r.r(-0.6f, 0.6f), x0 = r.r(-1.f, 1.f), s = r.r(0.03f, 0.06f);
                float x = x0 + float(now - t0) / 1000.f * sp;
                x = std::fmod(x + 1.2f, 2.4f) - 1.2f;
                c.fillCircle(X(x), Y(y), s * R, rgb(70, 64, 56), 255);
                c.fillCircle(X(x) - s * R * 0.3f, Y(y) - s * R * 0.3f, s * R * 0.45f, rgb(120, 110, 96), 160);
            }
        }
    }

    void drawDice(Canvas& c, uint32_t now) {
        const float size = std::max(20.f, std::min(30.f, R * 0.13f));
        for (int side = 0; side < 2; ++side) {
            const float x = cx + (side == 0 ? -0.42f : 0.42f) * R;
            const float bottom = cy + 0.83f * R;
            for (const RollShow& r : rolls) {
                if (r.side != side) continue;
                const int a = r.tm.alpha(now);
                if (a <= 0) continue;
                const int h = dice::drawRoll(c, x, bottom, size, 6, r.dice, r.need, fac[side], r.tm, now);
                if (r.tm.settled(now, int(r.dice.size()))) {
                    const std::string t = std::to_string(r.hits) + (r.hits == 1 ? " HIT" : " HITS");
                    const int tw = textWidth(FONT_SMB, t) + 10;
                    const int ty = int(bottom) - h - 20;
                    c.fillRoundRect(int(x) - tw / 2, ty, tw, 17, 5, rgb(6, 8, 14), a * 200 / 255);
                    drawTextAligned(c, FONT_SMB, int(x) - tw / 2, ty + 1, tw, Align::Center, t,
                                    r.hits ? rgb(120, 255, 140) : rgb(170, 170, 180), a);
                }
            }
        }
    }
};

// ============================================================ the public side
SpaceBattle::SpaceBattle() : m_(std::make_unique<Impl>()) {}
SpaceBattle::~SpaceBattle() = default;

void SpaceBattle::clear() {
    active_ = false;
    sys_ = -1;
    player_[0] = player_[1] = -1;
    m_ = std::make_unique<Impl>();
}

void SpaceBattle::start(const Game& g, int sys, int attacker, int defender, const Force& a, const Force& b, uint32_t now, float speed) {
    clear();
    if (sys < 0 || sys >= int(g.systems.size()) || attacker < 0 || defender < 0) return;
    active_ = true;
    sys_ = sys;
    player_[0] = attacker;
    player_[1] = defender;
    Impl& m = *m_;
    m.pl[0] = attacker;
    m.pl[1] = defender;
    for (int i = 0; i < 2; ++i) {
        if (m.pl[i] == kNeutral || m.pl[i] < 0 || m.pl[i] >= g.numPlayers) {  // Thunder's Edge: neutral units
            m.col[i] = theme::neutral;
            m.fac[i] = -1;
            m.flagName[i] = "Neutral flagship";
            continue;
        }
        const Player& p = g.players[size_t(m.pl[i])];
        m.col[i] = pcolours::base(p.color);
        m.pci[i] = pcolours::of(g, m.pl[i]);
        m.fac[i] = p.faction;
        m.flagName[i] = unitDef(g, m.pl[i], UnitType::Flagship).name;
        m.flagName2[i] = unitDef(g, m.pl[i], UnitType::Flagship2).name;  // (0.52.0: the Ghemina's Lady)
    }
    m.speed = std::max(0.3f, speed);
    m.t0 = now;
    m.last = 0;
    m.rng.s = now * 2654435761u + uint32_t(sys) * 97u + 1u;
    if (!m.rng.s) m.rng.s = 1;
    m.setup(g, sys, a, b, now);
}

void SpaceBattle::roll(int player, const std::vector<int8_t>& d, const std::vector<int8_t>& need, int hits, uint32_t now, uint32_t shown) {
    if (!active_) return;
    Impl& m = *m_;
    const int side = m.sideOf(player);
    if (side < 0) return;
    // the same side rolling again: a new round - the old dice go
    bool again = false;
    for (const Impl::RollShow& r : m.rolls)
        if (r.side == side && !r.tm.fadeAt && r.tm.alpha(now) > 0) again = true;
    if (again)
        for (Impl::RollShow& r : m.rolls)
            if (!r.tm.fadeAt) r.tm.fadeAt = now;
    Impl::RollShow r;
    r.side = side;
    r.dice = d;
    r.need = need;
    r.hits = hits;
    r.tm.start = now;
    r.tm.appear = std::min<uint32_t>(220, shown / 7);
    r.tm.roll = std::min<uint32_t>(1000, std::max<uint32_t>(200, shown * 62 / 100));
    r.tm.fade = uint32_t(std::max(150.f, 350.f * m.speed));
    m.rolls.push_back(r);
    while (m.rolls.size() > 6) m.rolls.erase(m.rolls.begin());
}

static uint32_t fadeDice(SpaceBattle::Impl& m, uint32_t now) {
    // the dice fade away first; the ships go when they are gone (owner)
    uint32_t wait = 0;
    for (auto& r : m.rolls)
        if (r.tm.alpha(now) > 0) {
            if (!r.tm.fadeAt) r.tm.fadeAt = now;
            wait = std::max<uint32_t>(wait, uint32_t(std::max(0, int(r.tm.fade) - int(now - r.tm.fadeAt))));
        }
    return wait;
}

void SpaceBattle::damaged(int player, UnitType t, int count, uint32_t now) {
    if (!active_) return;
    Impl& m = *m_;
    const int side = m.sideOf(player);
    if (side < 0) return;
    Impl::Pending p;
    p.at = now + fadeDice(m, now);
    p.kind = 0;
    p.side = side;
    p.t = t;
    p.count = count;
    m.pending.push_back(p);
}

void SpaceBattle::destroyed(int player, UnitType t, int count, uint32_t now) {
    if (!active_) return;
    Impl& m = *m_;
    const int side = m.sideOf(player);
    if (side < 0 || !isShip(t)) return;
    Impl::Pending p;
    p.at = now + fadeDice(m, now);
    p.kind = 1;
    p.side = side;
    p.t = t;
    p.count = count;
    m.pending.push_back(p);
}

void SpaceBattle::retreat(int player, uint32_t now) {
    if (!active_) return;
    Impl& m = *m_;
    const int side = m.sideOf(player);
    if (side < 0) return;
    fadeDice(m, now);
    for (Impl::Ship& s : m.ships)
        if (s.side == side && s.state == 0) {
            s.state = 3;
            s.faceLeft = side == 0;
            s.vx = 0;
            s.vy = 0;
        }
}

void SpaceBattle::end(int winner, const std::string& text, uint32_t now) {
    if (!active_) return;
    Impl& m = *m_;
    fadeDice(m, now);
    m.ended = true;
    m.winner = winner;
    m.result = text;
}

void SpaceBattle::draw(Canvas& c, const Game& g, Rect area, uint32_t now, const std::string& confirm) {
    if (!active_) return;
    Impl& m = *m_;
    m.area = area;
    m.R = std::min(float(area.w) / 2.f, float(area.h) / kSqrt3) - 4.f;
    m.cx = float(area.x) + float(area.w) / 2.f;
    m.cy = float(area.y) + float(area.h) / 2.f;
    if (!m.bg || m.bgArea.w != area.w || m.bgArea.h != area.h || m.bg->scale() != c.scale()) m.renderBackdrop(g, c.scale());
    m.update(now);

    const Rect keep = c.clip();
    c.setClip(area);
    // keep what is under the area: outside the hex it comes back afterwards
    // (0.27.0: all of this in real pixels)
    const int W = c.pw();
    uint32_t* px = c.pixels();
    const Rect pa = c.toPx(area);
    const int ax0 = std::max(0, pa.x), ax1 = std::min(W, pa.x + pa.w);
    const int ay0 = std::max(0, pa.y), ay1 = std::min(c.ph(), pa.y + pa.h);
    if (ax1 <= ax0 || ay1 <= ay0) {
        c.setClip(keep);
        return;
    }
    m.saved.resize(size_t(ax1 - ax0) * size_t(ay1 - ay0));
    for (int y = ay0; y < ay1; ++y)
        std::copy(px + size_t(y) * size_t(W) + ax0, px + size_t(y) * size_t(W) + ax1, m.saved.begin() + long(size_t(y - ay0) * size_t(ax1 - ax0)));
    // the backdrop
    const uint32_t* bp = m.bg->pixels();
    const int bw = m.bg->pw(), bh = m.bg->ph();
    for (int y = ay0; y < std::min(ay1, pa.y + bh); ++y) {
        const int bx0 = ax0 - pa.x, bx1 = std::min(bw, ax1 - pa.x);
        if (bx1 > bx0) std::copy(bp + size_t(y - pa.y) * size_t(bw) + bx0, bp + size_t(y - pa.y) * size_t(bw) + bx1, px + size_t(y) * size_t(W) + ax0);
    }
    m.animatedBack(c, now);
    m.specialBack(c, now);
    if (retro::hd())  // Voidfall (0.33.1): the void reaching into the fight, behind the ships
        voidfall::tendrils(c, Rect{int(m.cx - m.R), int(m.cy - m.R * 0.87f), int(2.f * m.R), int(1.74f * m.R)}, now, uint32_t(m.sysDef) + 31u, 4, 0.85f,
                           175);
    // Retrowave (0.49.0): the grid of the chunky pass below - the pixel-art ships go on it
    m.grid = pxship::Grid{};
    if (retro::on())
        retro::chunkyGrid(c, Rect{int(m.cx - m.R), int(m.cy - m.R * 0.87f), int(2.f * m.R) + 1, int(1.74f * m.R) + 1}, std::max(2, int(m.R / 70.f)),
                          m.grid.x, m.grid.y, m.grid.f);
    // moons, structures, wreckage, ships, fire
    for (const Impl::Ship& s : m.ships)
        if (s.moon) m.drawShip(c, s, now);
    for (const Impl::Struct& st : m.structs) m.drawStruct(c, g, st, now);
    for (const Impl::Piece& p : m.pieces) m.drawPiece(c, p);
    for (const Impl::Part& p : m.parts)
        if (p.kind == Chunk) m.drawPart(c, p, now);
    static const UnitType kOrder[] = {UnitType::Carrier, UnitType::Flagship, UnitType::Dreadnought, UnitType::Cruiser, UnitType::Destroyer,
                                      UnitType::Fighter};
    for (UnitType t : kOrder)
        for (const Impl::Ship& s : m.ships)
            if (!s.moon && s.t == t) m.drawShip(c, s, now);
    if (m.grid.f > 0)
        for (const Impl::Ship& s : m.ships) m.drawShieldPx(c, s, now);
    for (Impl::Beam& b : m.beams) m.drawBeam(c, b, now);
    for (const Impl::Shot& s : m.shots) m.drawShot(c, s, now);
    for (const Impl::Part& p : m.parts)
        if (p.kind != Chunk) m.drawPart(c, p, now);
    for (const Impl::Boom& b : m.booms) m.drawBoom(c, b, now);
    for (const Impl::Ring& r : m.rings) m.drawRing(c, r, now);
    // Retrowave (0.26.0): the fight in chunky pixels - drawn small, enlarged; the health bars stay sharp
    if (retro::on()) retro::chunky(c, Rect{int(m.cx - m.R), int(m.cy - m.R * 0.87f), int(2.f * m.R) + 1, int(1.74f * m.R) + 1}, std::max(2, int(m.R / 70.f)));
    // Voidfall (0.33.1): the painted look - violet shadows, warm lights, the print lines; the bars stay clean
    if (retro::hd()) voidfall::grade(c, Rect{int(m.cx - m.R), int(m.cy - m.R * 0.87f), int(2.f * m.R) + 1, int(1.74f * m.R) + 1});
    for (const Impl::Ship& s : m.ships) m.drawBars(c, g, s);
    m.ripple(c, now);
    // outside the hex: what was there before
    const float pcx = c.fX(m.cx), pcy = c.fY(m.cy), pR = c.fx(m.R);
    for (int y = ay0; y < ay1; ++y) {
        const float dy = std::fabs(float(y) + 0.5f - pcy);
        int xl = ax1, xr = ax1;
        if (dy <= pR * kSqrt3 / 2.f) {
            const float half = pR - dy / kSqrt3;
            xl = std::max(ax0, int(pcx - half));
            xr = std::min(ax1, int(pcx + half));
        }
        uint32_t* line = px + size_t(y) * size_t(W);
        const uint32_t* sv = m.saved.data() + size_t(y - ay0) * size_t(ax1 - ax0);
        if (xl > ax0) std::copy(sv, sv + (xl - ax0), line + ax0);
        if (xr < ax1 && xr >= ax0) std::copy(sv + (xr - ax0), sv + (ax1 - ax0), line + xr);
        else if (xl >= ax1) std::copy(sv, sv + (ax1 - ax0), line + ax0);
    }
    // the hex edge
    {
        float xs[7], ys[7];
        for (int i = 0; i <= 6; ++i) {
            const float a = kPi / 3.f * float(i);
            xs[i] = m.cx + m.R * std::cos(a);
            ys[i] = m.cy + m.R * std::sin(a);
        }
        for (int i = 0; i < 6; ++i) {
            c.line(xs[i], ys[i], xs[i + 1], ys[i + 1], rgb(0, 0, 0), 5.f, 160);
            c.line(xs[i], ys[i], xs[i + 1], ys[i + 1], rgb(150, 70, 70), 2.f, 230);
        }
    }
    m.drawDice(c, now);
    // the result, and the line that waits for A
    if (m.ended && !m.result.empty()) {
        const uint32_t wc = m.winner == kNeutral ? theme::neutral : m.winner >= 0 ? pcolours::base(g.players[size_t(m.winner)].color) : theme::text;
        const int w = int(m.R * 1.2f);
        const int y = int(m.cy - m.R * 0.86f) + 8;
        c.fillRoundRect(int(m.cx) - w / 2, y, w, 24, 6, rgb(8, 10, 18), 220);
        c.frameRoundRect(int(m.cx) - w / 2, y, w, 24, 6, wc, 1, 200);
        drawTextAligned(c, FONT_MDB, int(m.cx) - w / 2, y + 3, w, Align::Center, fitText(FONT_MDB, m.result, w - 10), wc);
    }
    if (!confirm.empty()) {
        const int w = textWidth(FONT_SMB, confirm) + 24;
        const int y = int(m.cy + m.R * 0.86f) - 26;
        const int a = 170 + int(60.f * std::sin(float(now % 1200) / 1200.f * 2 * kPi));
        c.fillRoundRect(int(m.cx) - w / 2, y, w, 20, 6, rgb(10, 14, 24), 230);
        c.frameRoundRect(int(m.cx) - w / 2, y, w, 20, 6, rgb(110, 200, 255), 1, a);
        drawTextAligned(c, FONT_SMB, int(m.cx) - w / 2, y + 2, w, Align::Center, confirm, rgb(200, 235, 255));
    }
    c.setClip(keep);
}

}  // namespace battle
