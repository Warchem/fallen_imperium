#include "ui/retro.h"
#include "ui/voidfall.h"
#include "ui/ground_battle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>

#include "audio/audio.h"
#include "engine/query.h"
#include "gfx/font.h"
#include "ui/battle_fx.h"
#include "ui/ground_units.h"
#include "ui/icons.h"
#include "ui/painted.h"
#include "ui/pixel_ships.h"
#include "ui/theme.h"
#include "ui/pcolors.h"

using namespace ti;

namespace battle {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kSqrt3 = 1.7320508f;
constexpr int kMen = 6;      // soldiers per infantry (owner: the middle ground)
constexpr int kMaxMen = 10;  // Voidfall (0.51.1, owner): ten, spread over the field

uint32_t lighter(uint32_t c, float k) { return mix(c, rgb(255, 255, 255), int(255.f * std::max(0.f, std::min(1.f, k)))); }
uint32_t darker(uint32_t c, float k) { return mix(c, rgb(0, 0, 0), int(255.f * std::max(0.f, std::min(1.f, k)))); }
uint32_t muted(uint32_t c) { return mix(c, rgb(110, 110, 104), 95); }
uint32_t paintMuted(uint32_t c) { return mix(c, rgb(120, 116, 128), 60); }  // Voidfall's painted troops (0.51.1): brighter

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

enum class Land { Ruins, Golden, Refinery, Desert, Volcanic, Tundra, Green, Fracture, Storm };  // (0.43.2: the last two)
// 0.44.0 (the ground combat overhaul): Ball .. Stream are the factions' own weapons (ground_units.h)
enum ShotKind { Bullet, Laser, Gatling, Plasma, Ball, Lob, Shard, Star, Rocket, Spore, Beam, Bolt, Stream };
enum PartKind { Dust, Smoke, Spark, Lava, Chunk, Flame, Goo, Gas, Mote };
enum BoomKind { Fire, Flash, Muzzle, Big, Nova, Hole, Splat };

float easeOut(float k) {
    k = std::max(0.f, std::min(1.f, k));
    return 1.f - (1.f - k) * (1.f - k) * (1.f - k);
}

}  // namespace

struct GroundBattle::Impl {
    struct Man {
        float ox = 0, oy = 0;  // place in the squad (radii)
        float jx = 0, jy = 0;  // where he has moved to now
        int state = 0;         // 0 fighting, 1 down (fading), 2 gone
        uint32_t dieAt = 0, nextFire = 0, nextStep = 0, firedAt = 0;
        float phase = 0;
    };
    struct Squad {
        int side = 0;
        float x = 0, y = 0, hx = 0, hy = 0, ex = 0, ey = 0;
        uint32_t enterAt = 0, enterDur = 1200;
        Man men[kMaxMen];
        bool doomed = false, gone = false;
        float alt = 0;         // 0.44.0: the invaders' planetfall - how high above the ground (R)
        bool landed = true;
    };
    struct Walker {  // mechs (and fighters that landed - they hover)
        int side = 0;
        UnitType t = UnitType::Mech;
        float x = 0, y = 0, hx = 0, hy = 0, ex = 0, ey = 0, tx = 0, ty = 0;
        uint32_t enterAt = 0, enterDur = 1300, nextFire = 0, nextMove = 0, burstAt = 0;
        int burst = 0, target = -1;
        float aimX = 0, aimY = 0;
        bool damaged = false;
        float sx = 0, sy = 0;
        uint32_t nextSmoke = 0, dieAt = 0;
        int state = 0;  // 0 fighting, 1 wreck (fading), 2 gone
        float size = 0.18f, phase = 0;
        float alt = 0;  // 0.44.0: planetfall
        bool landed = true, drops = false;
    };
    struct Tower {  // PDS and space docks in the corners
        int side = -1, owner = -1;
        UnitType t = UnitType::PDS;
        float x = 0, y = 0, size = 0.3f;
        bool ruined = false, damaged = false, fights = false;
        uint32_t ruinAt = 0, nextSmoke = 0, nextFire = 0;  // (0.44.0: the Titans' PDS robot fires rockets)
    };
    struct Shot {
        int kind = Bullet;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        uint32_t born = 0, arrive = 0;
        uint32_t col = 0;
        // 0.44.0: size (R), style (Lob: 0 seed / 1 rock / 2 big rock / 3 slime; Star: 0 nova / 1 black hole; Beam: 0 purple /
        // 1 hyper beam; Stream: 0 fire / 1 green fire / 2 slime / 3 gas), arc (R), spread (Stream), the shooter's side
        float size = 0, arc = 0, spread = 0;
        int style = 0, side = 0;
        uint32_t seed = 0, nextPuff = 0;
    };
    struct Part {
        int kind = Dust;
        float x = 0, y = 0, vx = 0, vy = 0, g = 0;
        uint32_t born = 0, life = 500;
        float size = 0.01f, grow = 0;
        uint32_t col = 0;
        int a0 = 200;
    };
    struct Boom {
        int kind = Fire;
        float x = 0, y = 0, r = 0.04f;
        uint32_t born = 0, life = 400;
        uint32_t col = 0;
    };
    // 0.44.0: what the factions leave on the field
    struct Patch {  // overgrowth (Arborec), baked into the land as it grows
        float x = 0, y = 0, r = 0, rmax = 0.08f, drawn = 0;
        uint32_t born = 0;
        int side = 0;
        uint32_t seed = 1;
    };
    struct Cloud {  // Arborec spores: a sickly cloud that stays over the enemy
        float x = 0, y = 0, seed = 0;
        uint32_t born = 0, life = 6000;
    };
    struct Crater {  // Zelian meteors
        float x = 0, y = 0, r = 0.05f;
        uint32_t born = 0;
    };
    struct Meteor {
        float x = 0, y = 0;
        uint32_t at = 0;
        bool hit = false;
    };
    std::vector<Patch> patches;
    std::vector<Cloud> clouds;
    std::vector<Crater> craters;
    std::vector<Meteor> meteors;
    uint32_t nextSpread = 0, nextMeteor = 0;
    gunits::Look look[2] = {gunits::Default, gunits::Default};
    int style = 0;  // the tileset (0 Simplistic, 1 Retrowave, 2 Voidfall)
    bool anyLook(gunits::Look l) const { return look[0] == l || look[1] == l; }
    struct Strike {  // 0.43.0 (owner D128): a Mirveda Gauss Cannon's slug from orbit
        float x = 0, y = 0;
        uint32_t at = 0;
        bool landed = false;
    };
    std::vector<Strike> strikes;
    // 0.43.2 (owner): the Fracture's planets and Thunder's Edge have their own ground
    int fracLand = -1;  // 0 Cocytus, 1 Styx, 2 Lethe, 3 Phlegethon
    std::vector<battlefx::Crack> fiss;  // the ground broken out from one impact
    struct GShard {  // a piece of broken sky hanging over the field
        float x = 0, y = 0, s = 0.03f, rot = 0, ph = 0;
    };
    std::vector<GShard> shards;
    struct Spire {  // Thunder's Edge: the lightning spires
        float x = 0, y = 0, h = 0.12f;
    };
    std::vector<Spire> spires;
    struct RollShow {
        int side = 0;
        std::vector<int8_t> dice, need;
        int hits = 0;
        dice::Timing tm;
    };
    struct Pending {
        uint32_t at = 0;
        int kind = 0, side = 0, count = 1;
        UnitType t = UnitType::Infantry;
    };
    struct Feature {  // terrain things that move: fires, smokestacks, geysers
        float x = 0, y = 0;
        int kind = 0;  // 0 fire, 1 smokestack, 2 geyser, 3 glowing plant
        uint32_t next = 0;
    };

    int pl[2] = {-1, -1};
    uint32_t col[2] = {0, 0}, raw[2] = {0, 0};
    int pci[2] = {-1, -1};  // 0.62.0: the sides' colour indices (their finish on the mechs)
    int fac[2] = {-1, -1};
    float speed = 1.f;
    uint32_t t0 = 0, last = 0;
    Rng rng;
    Land land = Land::Green;
    int planet = -1, def = -1;
    int infActual[2] = {0, 0};
    int nMen = kMen;      // soldiers per infantry in this fight
    bool spread = false;  // Voidfall (0.51.1, owner): kMaxMen each, spread over their side's ground, not in knots
    uint32_t pcol[2] = {0, 0};  // Voidfall's painted troops: brighter colours (the light darkens them)
    std::vector<Squad> squads;
    std::vector<Walker> walkers;
    std::vector<Tower> towers;
    std::vector<Shot> shots;
    std::vector<Part> parts;
    std::vector<Boom> booms;
    std::vector<Feature> feats;
    std::vector<RollShow> rolls;
    std::vector<Pending> pending;
    bool ended = false;
    int winner = -1;
    std::string result;

    Rect area{0, 0, 0, 0};
    float R = 100, cx = 0, cy = 0;
    std::unique_ptr<Canvas> bg;
    Rect bgArea{0, 0, 0, 0};
    pxship::Grid grid;  // Retrowave (0.49.0): the fight's pixel grid - the unit pictures are pixel art on it (f 0: pictures)
    std::vector<uint32_t> saved;

    float X(float x) const { return cx + x * R; }
    float Y(float y) const { return cy + y * R; }
    int sideOf(int p) const { return p == pl[0] ? 0 : p == pl[1] ? 1 : -1; }
    float fwd(int side) const { return side == 0 ? 1.f : -1.f; }  // the attacker moves down the screen
    float halfWidth(float y) const { return std::max(0.1f, 1.f - std::fabs(y) / kSqrt3 - 0.06f); }

    // ---------------------------------------------------------- setup
    void setup(const Game& g, int sys, int pla, const Force& a, const Force& b, uint32_t now) {
        planet = pla;
        spread = retro::hd() && gunits::voidPaint();
        nMen = spread ? kMaxMen : kMen;
        const PlanetState& ps = g.planets[size_t(pla)];
        def = ps.def;
        const PlanetDef& pd = kPlanets[size_t(ps.def)];
        fracLand = -1;
        {
            static const char* fk[4] = {"cocytus", "styx", "lethe", "phlegethon"};
            for (int k = 0; k < 4; ++k)
                if (!std::strcmp(pd.key, fk[k])) fracLand = k;
        }
        if (fracLand >= 0) land = Land::Fracture;                    // 0.43.2
        else if (!std::strcmp(pd.key, "thundersedge")) land = Land::Storm;  // 0.43.2
        else if (!std::strcmp(pd.key, "mr")) land = Land::Golden;
        else if (pd.trait == Trait::Cultural) land = Land::Ruins;
        else if (pd.trait == Trait::Industrial) land = Land::Refinery;
        else if (pd.trait == Trait::Hazardous) {  // one of three, at random (owner)
            const int k = rng.i(3);
            land = k == 0 ? Land::Desert : k == 1 ? Land::Volcanic : Land::Tundra;
        } else {
            land = Land::Green;
        }
        // Gauss Cannons (Mirveda PDS in the space area, 0.43.0): the side that has them over the planet calls down
        // a slug or two from orbit early in the fight - onto the other side's half
        strikes.clear();
        for (int side = 0; side < 2; ++side) {
            const int q = pl[side];
            if (q < 0 || q >= g.numPlayers || sys < 0 || !unitDef(g, q, UnitType::PDS).spaceOnly) continue;
            const int guns = std::min(3, g.systems[size_t(sys)].space[size_t(q)].count(UnitType::PDS));
            for (int k = 0; k < guns; ++k) {
                Strike st;
                st.x = rng.r(-0.45f, 0.45f);
                st.y = side == 0 ? rng.r(0.15f, 0.42f) : rng.r(-0.42f, -0.15f);  // (the invader's men are at the top)
                st.at = now + 900 + uint32_t(k) * 1300 + uint32_t(rng.i(400));
                strikes.push_back(st);
            }
        }
        (void)sys;
        // towers: each side's PDS (one, however many) and space dock, in its corners
        for (int side = 0; side < 2; ++side) {
            const Force& f = side == 0 ? a : b;
            const float y = side == 0 ? -0.6f : 0.6f;
            if (f.count(UnitType::PDS)) {
                Tower t;
                t.side = side;
                t.owner = pl[side];
                t.t = UnitType::PDS;
                t.x = -0.5f;
                t.y = y;
                t.size = 0.3f;
                t.fights = isFaction(g, pl[side], "titans");  // Hel-Titans fight as ground forces
                t.size *= gunits::structureScale(look[side], true);
                t.nextFire = now + 1800 + uint32_t(rng.i(1500));
                towers.push_back(t);
            }
            if (f.count(UnitType::SpaceDock)) {
                Tower t;
                t.side = side;
                t.owner = pl[side];
                t.t = UnitType::SpaceDock;
                t.x = 0.5f;
                t.y = y;
                t.size = 0.34f;
                towers.push_back(t);
            }
        }
        for (int side = 0; side < 2; ++side) {
            const Force& f = side == 0 ? a : b;
            const float dir = fwd(side);
            infActual[side] = f.count(UnitType::Infantry);
            const int nSquads = std::min(10, f.count(UnitType::Infantry));
            const int nMechs = std::min(4, f.count(UnitType::Mech));
            const int nFighters = std::min(6, f.count(UnitType::Fighter));
            // slots: mechs at the front, then the squads, row by row
            std::vector<std::pair<float, float>> slots;
            const float rows[3] = {0.22f, 0.37f, 0.52f};
            const int total = nSquads + nMechs;
            for (int r = 0; r < 3 && int(slots.size()) < total; ++r) {
                const float y = -dir * rows[r];
                const int inRow = std::min(4, total - int(slots.size()));
                const float hw = halfWidth(y) - 0.12f;
                for (int k = 0; k < inRow; ++k) {
                    const float x = inRow == 1 ? 0.f : -hw + 2.f * hw * float(k) / float(inRow - 1);
                    slots.push_back({x + rng.r(-0.03f, 0.03f), y + rng.r(-0.025f, 0.025f)});
                }
            }
            size_t si = 0;
            for (int i = 0; i < nMechs && si < slots.size(); ++i, ++si) {
                Walker w;
                w.side = side;
                w.t = UnitType::Mech;
                w.hx = w.tx = slots[si].first;
                w.hy = w.ty = slots[si].second;
                // 0.44.0 (owner): only the invaders make planetfall - they drop in; the defenders stand there already
                w.ex = w.hx;
                w.ey = w.hy;
                w.x = w.hx;  // (owner, 0.44.0 review: a defender's mech walked out from the middle - it starts in its place)
                w.y = w.hy;
                w.size = 0.2f;
                w.damaged = i < std::min<int>(f.dmg[int(UnitType::Mech)], nMechs);
                w.phase = rng.r(0, 2 * kPi);
                if (side == 0) {
                    w.drops = true;
                    w.landed = false;
                    w.enterAt = now + uint32_t(float(i * 160) * speed);
                    w.enterDur = uint32_t(std::max(500.f, 1700.f * speed));
                    w.nextFire = w.enterAt + w.enterDur + uint32_t(rng.i(1500));
                } else {
                    w.enterAt = now - 10000;
                    w.enterDur = 1;
                    w.nextFire = now + 1400 + uint32_t(rng.i(1500));
                }
                if (w.damaged) {
                    w.sx = rng.r(-0.04f, 0.04f);
                    w.sy = rng.r(-0.05f, 0.f);
                }
                walkers.push_back(w);
            }
            const size_t firstSquad = squads.size();
            for (int i = 0; i < nSquads && (spread || si < slots.size()); ++i, ++si) {  // (Voidfall: no slots needed)
                Squad q;
                q.side = side;
                q.hx = q.x = si < slots.size() ? slots[si].first : 0.f;
                q.hy = q.y = si < slots.size() ? slots[si].second : -dir * 0.4f;
                q.ex = q.hx;
                q.ey = q.hy;
                if (side == 0) {  // 0.44.0: planetfall
                    q.landed = false;
                    q.enterAt = now + uint32_t(float(200 + i * 90 + rng.i(200)) * speed);
                    q.enterDur = uint32_t(std::max(450.f, 1500.f * speed));
                } else {
                    q.enterAt = now - 10000;
                    q.enterDur = 1;
                }
                if (spread) {  // Voidfall (0.51.1): their places are dealt out below (spreadMen)
                    for (int m = 0; m < kMaxMen; ++m) {
                        Man& man = q.men[m];
                        man.phase = rng.r(0, 2 * kPi);
                        man.nextFire = (side == 0 ? q.enterAt + q.enterDur : now + 1300) + uint32_t(float(rng.i(2500)) * 1.35f);
                        man.nextStep = now + uint32_t(rng.i(2000));
                    }
                    if (gunits::onePerPiece(look[side]))
                        for (int m = 1; m < kMaxMen; ++m) q.men[m].state = 2;
                    squads.push_back(q);
                    continue;
                }
                for (int m = kMen; m < kMaxMen; ++m) q.men[m].state = 2;  // (not in this fight)
                for (int m = 0; m < kMen; ++m) {  // a loose knot of men
                    Man& man = q.men[m];
                    const float ang = 2 * kPi * float(m) / float(kMen) + rng.r(-0.3f, 0.3f);
                    const float rr = m == 0 ? 0.f : rng.r(0.042f, 0.08f);
                    man.ox = std::cos(ang) * rr * 1.3f;
                    man.oy = std::sin(ang) * rr * 0.8f;
                    man.phase = rng.r(0, 2 * kPi);
                    man.nextFire = (side == 0 ? q.enterAt + q.enterDur : now + 1300) + uint32_t(rng.i(2500));
                    man.nextStep = now + uint32_t(rng.i(2000));
                }
                if (gunits::onePerPiece(look[side])) {  // Mortheus: one nebula for each infantry (owner)
                    q.men[0].ox = q.men[0].oy = 0;
                    for (int m = 1; m < kMen; ++m) q.men[m].state = 2;
                }
                squads.push_back(q);
            }
            if (spread) spreadMen(side, firstSquad);
            for (int i = 0; i < nFighters; ++i) {  // fighters that landed (Matriarch): they hover
                Walker w;
                w.side = side;
                w.t = UnitType::Fighter;
                w.hx = w.tx = rng.r(-0.5f, 0.5f);
                w.hy = w.ty = -dir * rng.r(0.1f, 0.5f);
                w.ex = w.hx;
                w.ey = -dir * 1.2f;
                w.x = w.hx;  // (the defenders' fighters start in their places; the invaders' fly in from w.ex / w.ey)
                w.y = side == 0 ? w.ey : w.hy;
                w.size = 0.09f;
                w.phase = rng.r(0, 2 * kPi);
                w.enterAt = side == 0 ? now + uint32_t(rng.i(300)) : now - 10000;  // (the defenders' are there already)
                w.enterDur = side == 0 ? uint32_t(std::max(300.f, 900.f * speed)) : 1u;
                w.nextFire = w.enterAt + w.enterDur + uint32_t(rng.i(900));
                walkers.push_back(w);
            }
        }
    }

    // Voidfall (0.51.1, owner: "spread them out to cover more space ... as if they weren't grouped together"): the men
    // of this side's squads (from `first`) get places all over their side's ground - each the best of 12 random tries
    // (the farthest from the places taken, clear of the mechs and the corner structures), in a band from the middle
    // that is deeper the more infantry there is - then dealt round the squads, so each squad's men are all over too.
    void spreadMen(int side, size_t first) {
        if (squads.size() <= first) return;
        const size_t n = squads.size() - first;
        const float dir = fwd(side);
        const int per = gunits::onePerPiece(look[side]) ? 1 : kMaxMen;
        const int N = int(n) * per;
        const float near = 0.14f, depth = std::max(0.3f, std::min(0.56f, 0.2f + 0.045f * float(n)));
        struct Av {
            float x, y, r;
        };
        std::vector<Av> av;
        for (const Walker& w : walkers)
            if (w.side == side && w.t == UnitType::Mech) av.push_back({w.hx, w.hy - 0.07f, 0.17f});
        for (const Tower& t : towers)
            if (t.side == side) av.push_back({t.x, t.y - t.size * 0.35f, t.size * 0.72f});
        std::vector<std::pair<float, float>> pts;
        pts.reserve(size_t(N));
        for (int k = 0; k < N; ++k) {
            float bx = 0, by = 0, best = -1e9f;
            for (int tr = 0; tr < 12; ++tr) {
                const float y = -dir * rng.r(near, near + depth);
                const float hw = std::max(0.05f, halfWidth(y) - 0.07f);
                const float x = rng.r(-hw, hw);
                float sc = 1e9f;
                for (const auto& p : pts) {
                    const float dx = p.first - x, dy = (p.second - y) * 1.4f;
                    sc = std::min(sc, dx * dx + dy * dy);
                }
                for (const Av& a : av) {
                    const float dx = a.x - x, dy = (a.y - y) * 1.2f;
                    if (dx * dx + dy * dy < a.r * a.r) sc -= 10.f;
                }
                if (sc > best) best = sc, bx = x, by = y;
            }
            pts.push_back({bx, by});
        }
        std::sort(pts.begin(), pts.end(), [](const std::pair<float, float>& a, const std::pair<float, float>& b) {
            const int ra = int(std::fabs(a.second) / 0.07f), rb = int(std::fabs(b.second) / 0.07f);
            return ra != rb ? ra < rb : a.first < b.first;
        });
        for (size_t i = 0; i < n; ++i) {
            Squad& q = squads[first + i];
            float sx = 0, sy = 0;
            for (int m = 0; m < per; ++m) {
                const auto& pt = pts[size_t(m) * n + i];
                q.men[m].ox = pt.first;
                q.men[m].oy = pt.second;
                sx += pt.first;
                sy += pt.second;
            }
            q.hx = q.x = q.ex = sx / float(per);
            q.hy = q.y = q.ey = sy / float(per);
            for (int m = 0; m < per; ++m) {
                q.men[m].ox -= q.hx;
                q.men[m].oy -= q.hy;
            }
        }
    }

    // 0.44.0: the overgrowth under every Arborec unit and structure (the invaders' grows once they have landed)
    void arborecPatches(uint32_t now) {
        for (int side = 0; side < 2; ++side) {
            if (look[side] != gunits::Arborec) continue;
            for (const Squad& q : squads) {
                if (q.side != side) continue;
                if (!spread) {
                    addPatch(q.hx, q.hy + 0.03f, 0.085f, side, q.enterAt + q.enterDur);
                    continue;
                }
                for (int m = 0; m < nMen; m += 5)  // Voidfall: under a couple of its spread men
                    if (q.men[m].state != 2) addPatch(q.hx + q.men[m].ox, q.hy + q.men[m].oy + 0.03f, 0.07f, side, q.enterAt + q.enterDur);
            }
            for (const Walker& w : walkers)
                if (w.side == side && w.t == UnitType::Mech) addPatch(w.hx, w.hy - 0.01f, 0.12f, side, w.enterAt + w.enterDur);
            for (const Tower& t : towers)
                if (t.side == side) addPatch(t.x, t.y - 0.02f, 0.17f, side, now + (side == 0 ? 1500u : 0u));
        }
        nextSpread = now + 2500;
        nextMeteor = now + 900;
    }
    void addPatch(float x, float y, float rmax, int side, uint32_t born) {
        if (patches.size() >= 48) return;
        Patch pt;
        pt.x = x;
        pt.y = y;
        pt.rmax = rmax;
        pt.side = side;
        pt.born = born;
        pt.seed = rng.next() | 1u;
        patches.push_back(pt);
    }

    // ---------------------------------------------------------- targets, shots, effects
    bool pickTarget(int side, float& tx, float& ty) {
        // a random enemy: a man, a walker or (rarely) a tower
        int n = 0;
        for (const Squad& q : squads)
            if (q.side != side && !q.gone && q.landed)
                for (int mi = 0; mi < nMen; ++mi)
                    if (q.men[mi].state == 0) ++n;
        for (const Walker& w : walkers)
            if (w.side != side && w.state == 0 && w.landed) n += 3;
        if (n == 0) return false;
        int k = rng.i(n);
        for (const Squad& q : squads) {
            if (q.side == side || q.gone || !q.landed) continue;
            for (int mi = 0; mi < nMen; ++mi) {
                const Man& m = q.men[mi];
                if (m.state != 0) continue;
                if (k-- == 0) {
                    tx = q.x + m.ox + m.jx;
                    ty = q.y + m.oy + m.jy;
                    return true;
                }
            }
        }
        for (const Walker& w : walkers) {
            if (w.side == side || w.state != 0 || !w.landed) continue;
            k -= 3;
            if (k < 0) {
                tx = w.x + rng.r(-0.03f, 0.03f);
                ty = w.y - w.size * 0.3f;
                return true;
            }
        }
        return false;
    }

    void fire(int kind, float x0, float y0, float x1, float y1, uint32_t c, uint32_t now, uint32_t delay = 0) {
        Shot s;
        s.kind = kind;
        s.x0 = x0;
        s.y0 = y0;
        s.x1 = x1 + rng.r(-0.02f, 0.02f);
        s.y1 = y1 + rng.r(-0.02f, 0.02f);
        const float d = std::sqrt((s.x1 - x0) * (s.x1 - x0) + (s.y1 - y0) * (s.y1 - y0));
        const float v = kind == Gatling ? 3.2f : kind == Plasma ? 1.4f : 2.4f;
        s.born = now + delay;
        s.arrive = s.born + uint32_t(std::max(50.f, d / v * 1000.f));
        s.col = c;
        shots.push_back(s);
        boom(Muzzle, x0, y0, 0.012f, 110, now + delay);
        // each weapon its own sound (owner, 0.27.0): slug rifles, laser carbines, mech gatlings, plasma
        audio::play(kind == Bullet ? audio::Sfx::Rifle : kind == Gatling ? audio::Sfx::Gatling : kind == Plasma ? audio::Sfx::Plasma
                                                                                                            : audio::Sfx::LaserRifle,
                    1.f, x0 * 0.7f);
    }

    // ---------------------------------------------------------- 0.44.0: the factions' own weapons
    float manSize(int side) const { return std::max(8.f, R * 0.066f) * gunits::infantryScale(look[side]); }
    static float weaponPace(gunits::Weapon w) {  // how much longer between shots than a rifle
        switch (w) {
            case gunits::WStar: return 2.2f;
            case gunits::WRock: return 1.8f;
            case gunits::WSpore: return 1.7f;
            case gunits::WGreenFlame:
            case gunits::WSlimeStream:
            case gunits::WGas: return 1.8f;
            case gunits::WLightning: return 1.3f;
            case gunits::WCrystal:
            case gunits::WSlimeShot: return 1.2f;
            case gunits::WDarkStar:
            case gunits::WHyperBeam: return 1.6f;
            case gunits::WPurpleBeam:
            case gunits::WBigRock:
            case gunits::WSeed: return 1.3f;
            default: return 1.f;
        }
    }
    static int burstOf(gunits::Weapon w) {
        switch (w) {
            case gunits::WRedGatling: return 8;
            case gunits::WCrystalFighter: return 3;
            case gunits::WBigCrystal: return 3;
            case gunits::WSeed:
            case gunits::WBlueBall:
            case gunits::WBigRock: return 2;
            default: return 1;
        }
    }
    static uint32_t burstGap(gunits::Weapon w) {
        switch (w) {
            case gunits::WRedGatling:
            case gunits::WCrystalFighter: return 70;
            case gunits::WBigCrystal: return 180;
            case gunits::WBlueBall: return 320;
            case gunits::WSeed:
            case gunits::WBigRock: return 420;
            default: return 200;
        }
    }
    void fireW(gunits::Weapon w, int side, float x0, float y0, float x1, float y1, uint32_t now, uint32_t delay = 0) {
        using namespace gunits;
        Shot s;
        s.side = side;
        s.x0 = x0;
        s.y0 = y0;
        s.x1 = x1 + rng.r(-0.02f, 0.02f);
        s.y1 = y1 + rng.r(-0.02f, 0.02f);
        s.born = now + delay;
        s.seed = rng.next();
        const float d = std::sqrt((s.x1 - x0) * (s.x1 - x0) + (s.y1 - y0) * (s.y1 - y0));
        float v = 2.4f;
        uint32_t dur = 0;  // beams and streams last this long
        audio::Sfx sfx = audio::Sfx::Rifle;
        float gain = 1.f;
        bool flash = true;
        switch (w) {
            case WSpore: s.kind = Spore, s.col = rgb(150, 220, 80), v = 0.55f, sfx = audio::Sfx::SporePuff, gain = 0.8f, flash = false; break;
            case WSeed: s.kind = Lob, s.style = 0, s.size = 0.017f, s.col = rgb(255, 140, 40), s.arc = 0.16f + d * 0.25f, v = 0.7f; sfx = audio::Sfx::SeedLob, gain = 0.8f; break;
            case WOrangePlasma: s.kind = Plasma, s.col = rgb(255, 150, 50), v = 1.4f, sfx = audio::Sfx::FirePlasma, gain = 0.8f; break;
            case WFlame2:
            case WGreenFlame:
                s.kind = Stream, s.style = w == WGreenFlame ? 1 : 0, s.spread = w == WFlame2 ? 0.06f : 0.045f, dur = w == WFlame2 ? 900 : 600;
                s.col = w == WGreenFlame ? rgb(120, 236, 80) : rgb(255, 150, 50), sfx = w == WGreenFlame ? audio::Sfx::GreenFlame : audio::Sfx::Flamethrower;
                gain = 0.75f, flash = false;
                break;
            case WRedPulse: s.kind = Laser, s.col = rgb(255, 60, 60), sfx = audio::Sfx::PulseLaser; break;
            case WRedGatling: s.kind = Gatling, s.col = rgb(255, 80, 70), v = 3.2f, sfx = audio::Sfx::RedGatling; break;
            case WSpine: s.kind = Bullet, s.col = rgb(232, 222, 184), sfx = audio::Sfx::BoneSpine; break;
            case WBlueBall: s.kind = Ball, s.size = 0.02f, s.col = rgb(90, 170, 255), v = 1.0f, sfx = audio::Sfx::PlasmaBall; break;
            case WSlimeShot: s.kind = Lob, s.style = 3, s.size = 0.011f, s.col = rgb(130, 200, 70), s.arc = 0.08f + d * 0.15f, v = 1.1f; sfx = audio::Sfx::SlimeShot, gain = 0.8f; break;
            case WPurpleLaser: s.kind = Laser, s.col = rgb(210, 110, 255), sfx = audio::Sfx::PurpleLaser; break;
            case WPurpleBeam: s.kind = Beam, s.style = 0, s.size = 0.011f, s.col = rgb(210, 100, 255), dur = 900, sfx = audio::Sfx::PurpleBeam, gain = 0.8f; break;
            case WRocket: s.kind = Rocket, s.size = 0.012f, s.col = rgb(255, 180, 90), s.arc = 0.12f + d * 0.12f, v = 0.9f, sfx = audio::Sfx::BigRocket; break;
            case WLightning: s.kind = Bolt, s.col = rgb(170, 140, 255), dur = 230, sfx = audio::Sfx::Lightning, gain = 0.8f, flash = false; break;
            case WHyperBeam: s.kind = Beam, s.style = 1, s.size = 0.028f, s.col = rgb(255, 214, 110), dur = 1100, sfx = audio::Sfx::HyperBeam, gain = 0.8f; break;
            case WCrystal: s.kind = Shard, s.size = 0.014f, s.col = rgb(160, 255, 180), v = 1.7f, sfx = audio::Sfx::CrystalShard, gain = 0.8f; break;
            case WBigCrystal: s.kind = Shard, s.size = 0.026f, s.col = rgb(160, 255, 180), v = 1.5f, sfx = audio::Sfx::BigCrystal, gain = 0.9f; break;
            case WStar: s.kind = Star, s.style = 0, s.size = 0.014f, s.col = rgb(255, 245, 200), v = 0.42f, sfx = audio::Sfx::StarLaunch, gain = 0.8f, flash = false; break;
            case WDarkStar: s.kind = Star, s.style = 1, s.size = 0.02f, s.col = rgb(255, 220, 170), v = 0.38f, sfx = audio::Sfx::DarkStar, gain = 0.85f, flash = false; break;
            case WRock: s.kind = Lob, s.style = 1, s.size = 0.012f, s.col = rgb(110, 96, 90), s.arc = 0.12f + d * 0.2f, v = 0.62f; sfx = audio::Sfx::RockThrow, gain = 0.6f, flash = false; break;
            case WBigRock: s.kind = Lob, s.style = 2, s.size = 0.024f, s.col = rgb(96, 84, 80), s.arc = 0.18f + d * 0.25f, v = 0.5f; sfx = audio::Sfx::RockThrow, gain = 0.95f, flash = false; break;
            case WSlimeStream:
            case WBigSlime:
                s.kind = Stream, s.style = 2, s.spread = w == WBigSlime ? 0.09f : 0.05f, dur = w == WBigSlime ? 800 : 600;
                s.col = rgb(150, 220, 70), sfx = audio::Sfx::SlimeStream, gain = w == WBigSlime ? 0.95f : 0.7f, flash = false;
                break;
            case WGas:
            case WBigGas:
                s.kind = Stream, s.style = 3, s.spread = w == WBigGas ? 0.2f : 0.08f, dur = w == WBigGas ? 1000 : 650;
                s.col = rgb(255, 196, 130), sfx = audio::Sfx::GasHiss, gain = w == WBigGas ? 0.95f : 0.7f, flash = false;
                break;
            case WDroidLaser: s.kind = Laser, s.col = rgb(255, 110, 90), sfx = audio::Sfx::DroidBlaster; break;
            case WCrystalFighter: s.kind = Laser, s.col = rgb(150, 230, 255), sfx = audio::Sfx::CrystalLaser; break;
            default: s.kind = Bullet, s.col = rgb(255, 236, 170); break;
        }
        s.arrive = dur ? s.born + dur : s.born + uint32_t(std::max(80.f, d / v * 1000.f));
        s.nextPuff = s.born;
        shots.push_back(s);
        if (flash) boom(Muzzle, x0, y0, s.kind == Beam ? 0.022f : 0.012f, s.kind == Beam ? dur : 110u, now + delay);
        audio::play(sfx, gain, x0 * 0.7f);
    }
    // where a moving shot is now (R units): lobs and rockets arc over
    void shotAt(const Shot& s, uint32_t now, float& x, float& y) const {
        const float k = std::max(0.f, std::min(1.f, float(int32_t(now - s.born)) / float(std::max<uint32_t>(1, s.arrive - s.born))));
        x = s.x0 + (s.x1 - s.x0) * k;
        y = s.y0 + (s.y1 - s.y0) * k - s.arc * 4.f * k * (1.f - k);
    }
    void streamPuff(const Shot& s, uint32_t now) {
        const float dx = s.x1 - s.x0, dy = s.y1 - s.y0, d = std::sqrt(dx * dx + dy * dy) + 1e-3f;
        const float ux = dx / d, uy = dy / d;
        const float sp = s.style == 3 ? 0.55f : s.style == 2 ? 0.75f : 0.85f;  // R per second
        const float off = rng.r(-s.spread, s.spread) * (0.5f + d);             // where it lands across the line
        const float life = d / sp * rng.r(0.85f, 1.1f);
        Part p;
        p.kind = s.style == 2 ? Goo : s.style == 3 ? Gas : Flame;
        p.x = s.x0;
        p.y = s.y0;
        p.vx = (dx - uy * off) / life;
        p.vy = (dy + ux * off) / life;
        p.g = s.style == 2 ? 0.45f : s.style == 3 ? -0.04f : -0.1f;
        p.vy -= 0.5f * p.g * life;  // (so the slime arcs and still lands where it was thrown)
        p.born = now;
        p.life = uint32_t(life * 1000.f);
        p.size = s.style == 3 ? 0.009f : s.style == 2 ? 0.007f : 0.008f;
        p.grow = s.style == 3 ? 0.022f : s.style == 2 ? 0.004f : 0.03f;
        p.col = s.col;
        p.a0 = s.style == 3 ? 90 : 235;
        parts.push_back(p);
    }
    void motes(float x, float y, int n, float spd, uint32_t now, uint32_t c, uint32_t life = 700) {
        for (int i = 0; i < n; ++i) {
            Part p;
            p.kind = Mote;
            p.x = x;
            p.y = y;
            const float a = rng.r(0, 2 * kPi), v = rng.r(0.3f, 1.f) * spd;
            p.vx = std::cos(a) * v;
            p.vy = std::sin(a) * v * 0.75f;
            p.born = now;
            p.life = life + uint32_t(rng.i(int(life / 2)));
            p.size = rng.r(0.004f, 0.008f);
            p.col = c;
            p.a0 = 255;
            parts.push_back(p);
        }
    }
    void arrived(const Shot& s, uint32_t now) {
        switch (s.kind) {
            case Gatling:
            case Plasma:
                sparks(s.x1, s.y1, 3, 0.4f, now, lighter(s.col, 0.4f));
                dust(s.x1, s.y1, now, 2);
                break;
            case Ball:
                boom(Flash, s.x1, s.y1, 0.045f, 220, now);
                sparks(s.x1, s.y1, 8, 0.6f, now, lighter(s.col, 0.4f));
                dust(s.x1, s.y1, now, 3);
                break;
            case Lob:
                if (s.style == 0) {  // an Arborec seed bursts: the overgrowth spreads where it fell
                    boom(Fire, s.x1, s.y1, 0.03f, 380, now);
                    sparks(s.x1, s.y1, 6, 0.4f, now, rgb(255, 170, 60));
                    addPatch(s.x1, s.y1, rng.r(0.05f, 0.08f), s.side, now);
                    audio::play(audio::Sfx::SeedPop, 0.8f, s.x1 * 0.7f);
                } else if (s.style == 3) {  // slime splats
                    Boom b;
                    b.kind = Splat, b.x = s.x1, b.y = s.y1, b.r = 0.022f, b.born = now, b.life = 600, b.col = s.col;
                    booms.push_back(b);
                    audio::play(audio::Sfx::Splat, 0.6f, s.x1 * 0.7f);
                } else {  // a rock lands
                    dust(s.x1, s.y1, now, s.style == 2 ? 7 : 4);
                    for (int i = 0; i < (s.style == 2 ? 6 : 3); ++i) {
                        Part c;
                        c.kind = Chunk, c.x = s.x1, c.y = s.y1, c.vx = rng.r(-0.35f, 0.35f), c.vy = rng.r(-0.6f, -0.2f), c.g = 1.4f;
                        c.born = now, c.life = 600, c.size = 0.008f, c.col = s.col, c.a0 = 230;
                        parts.push_back(c);
                    }
                    audio::play(audio::Sfx::RockImpact, s.style == 2 ? 0.9f : 0.55f, s.x1 * 0.7f);
                }
                break;
            case Shard:
                sparks(s.x1, s.y1, s.size > 0.02f ? 10 : 5, 0.5f, now, s.col);
                audio::play(audio::Sfx::Shatter, s.size > 0.02f ? 0.8f : 0.45f, s.x1 * 0.7f);
                break;
            case Star:
                if (s.style == 0) {  // supernova
                    boom(Nova, s.x1, s.y1, 0.075f, 950, now);
                    motes(s.x1, s.y1, 12, 0.5f, now, rgb(255, 220, 160));
                    audio::play(audio::Sfx::Supernova, 0.8f, s.x1 * 0.7f);
                } else {  // the star collapses into a black hole for 1.5 s (owner)
                    boom(Hole, s.x1, s.y1, 0.06f, 1500, now);
                    audio::play(audio::Sfx::BlackHole, 0.9f, s.x1 * 0.7f);
                }
                break;
            case Rocket:
                boom(Big, s.x1, s.y1, 0.05f, 650, now);
                sparks(s.x1, s.y1, 10, 0.7f, now);
                dust(s.x1, s.y1, now, 5);
                break;
            case Spore:
                if (clouds.size() < 14) {
                    Cloud cl;
                    cl.x = s.x1, cl.y = s.y1, cl.seed = rng.r(0.f, 6.f), cl.born = now, cl.life = 6000;
                    clouds.push_back(cl);
                }
                break;
            case Beam:
            case Bolt:
            case Stream: break;  // (they did their work while they lasted)
            default: dust(s.x1, s.y1, now, s.kind == Bullet ? 1 : 2); break;
        }
    }
    // the overgrowth spreading, the meteors, the clouds and craters going
    void specialsUpdate(uint32_t now) {
        for (Patch& pt : patches)  // the overgrowth grows (drawn into the land in growPatches)
            if (int32_t(now - pt.born) >= 0) pt.r = std::min(pt.rmax, float(now - pt.born) / 1000.f * 0.06f);
        if (look[0] == gunits::Arborec && int32_t(now - nextSpread) >= 0 && !patches.empty()) {
            nextSpread = now + 1300 + uint32_t(rng.i(500));
            const Patch& from = patches[size_t(rng.i(int(patches.size())))];
            if (from.side == 0 && from.r >= from.rmax * 0.8f) {
                const float a = rng.r(0, 2 * kPi), dd = from.r + rng.r(0.02f, 0.05f);
                const float x = from.x + std::cos(a) * dd, y = from.y + std::sin(a) * dd * 0.7f;
                if (std::fabs(y) < 0.8f && std::fabs(x) < halfWidth(y)) addPatch(x, y, rng.r(0.05f, 0.08f), 0, now);
            }
        }
        if (anyLook(gunits::Zelian) && !ended && int32_t(now - nextMeteor) >= 0) {  // (owner) a meteor shower on the field
            // (owner, 0.44.0 review: more of them) one every 0.5-1 s, and now and then two or three together
            nextMeteor = now + 500 + uint32_t(rng.i(550));
            const int n = rng.i(4) == 0 ? 2 + rng.i(2) : 1;
            for (int k = 0; k < n; ++k) {
                Meteor mt;
                mt.y = rng.r(-0.65f, 0.65f);
                mt.x = rng.r(-halfWidth(mt.y) + 0.1f, halfWidth(mt.y) - 0.1f);
                mt.at = now + 480 + uint32_t(k * 160);
                meteors.push_back(mt);
                if (k == 0) audio::play(audio::Sfx::MeteorFall, 0.7f, mt.x * 0.7f);
            }
        }
        for (size_t i = 0; i < meteors.size();) {
            Meteor& mt = meteors[i];
            if (!mt.hit && int32_t(now - mt.at) >= 0) {
                mt.hit = true;
                boom(Flash, mt.x, mt.y, 0.06f, 240, now);
                boom(Fire, mt.x, mt.y, 0.05f, 520, now);
                dust(mt.x, mt.y, now, 8);
                sparks(mt.x, mt.y, 10, 0.7f, now, rgb(255, 160, 80));
                audio::play(audio::Sfx::RockImpact, 0.9f, mt.x * 0.7f);
                audio::play(audio::Sfx::Impact, 0.4f, mt.x * 0.7f);
                if (craters.size() >= 16) craters.erase(craters.begin());
                Crater cr;
                cr.x = mt.x, cr.y = mt.y, cr.r = rng.r(0.04f, 0.07f), cr.born = now;
                craters.push_back(cr);
            }
            if (mt.hit) meteors.erase(meteors.begin() + long(i));
            else ++i;
        }
        for (size_t i = 0; i < clouds.size();) {
            if (int32_t(now - clouds[i].born) > int32_t(clouds[i].life)) clouds.erase(clouds.begin() + long(i));
            else ++i;
        }
        for (size_t i = 0; i < craters.size();) {
            if (int32_t(now - craters[i].born) > 8000) craters.erase(craters.begin() + long(i));
            else ++i;
        }
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
        if (kind == Big) audio::play(audio::Sfx::BigExplosion, std::min(1.2f, 0.5f + r * 5.f), x * 0.7f);  // a mech, a PDS, a dock
    }

    void dust(float x, float y, uint32_t now, int n = 2) {
        const uint32_t dc = land == Land::Tundra ? rgb(225, 235, 245) : land == Land::Volcanic ? rgb(80, 74, 70)
                            : land == Land::Desert ? rgb(220, 170, 110) : rgb(170, 160, 140);
        for (int i = 0; i < n; ++i) {
            Part p;
            p.kind = Dust;
            p.x = x + rng.r(-0.01f, 0.01f);
            p.y = y + rng.r(-0.01f, 0.01f);
            p.vx = rng.r(-0.04f, 0.04f);
            p.vy = rng.r(-0.05f, -0.01f);
            p.born = now;
            p.life = 380 + uint32_t(rng.i(250));
            p.size = 0.006f;
            p.grow = 0.03f;
            p.col = dc;
            p.a0 = 150;
            parts.push_back(p);
        }
    }

    void sparks(float x, float y, int n, float spd, uint32_t now, uint32_t c = rgb(255, 210, 130)) {
        for (int i = 0; i < n; ++i) {
            Part p;
            p.kind = Spark;
            p.x = x;
            p.y = y;
            const float a = rng.r(0, 2 * kPi), v = rng.r(0.2f, 1.f) * spd;
            p.vx = std::cos(a) * v;
            p.vy = std::sin(a) * v - 0.2f;
            p.g = 0.9f;
            p.born = now;
            p.life = 250 + uint32_t(rng.i(300));
            p.col = c;
            p.a0 = 255;
            parts.push_back(p);
        }
    }

    void smoke(float x, float y, uint32_t now, float size = 0.012f, uint32_t c = rgb(70, 70, 74)) {
        Part p;
        p.kind = Smoke;
        p.x = x;
        p.y = y;
        p.vx = rng.r(-0.02f, 0.03f);
        p.vy = rng.r(-0.09f, -0.05f);
        p.born = now;
        p.life = 1300 + uint32_t(rng.i(600));
        p.size = size;
        p.grow = 0.03f;
        p.col = c;
        p.a0 = 120;
        parts.push_back(p);
    }

    // ---------------------------------------------------------- what the dice did
    void applyPending(const Pending& p, uint32_t now) {
        if (p.kind == 0) {  // a mech (or a Hel-Titan) shrugs off a hit
            for (int k = 0; k < p.count; ++k) {
                for (Walker& w : walkers)
                    if (w.side == p.side && w.t == p.t && w.state == 0 && !w.damaged) {
                        w.damaged = true;
                        w.sx = rng.r(-0.04f, 0.04f);
                        w.sy = rng.r(-0.06f, -0.01f);
                        boom(Fire, w.x + w.sx, w.y + w.sy, 0.04f, 420, now);
                        sparks(w.x + w.sx, w.y + w.sy, 8, 0.5f, now);
                        goto nextDamage;
                    }
                for (Tower& t : towers)
                    if (t.side == p.side && t.t == p.t && !t.ruined && !t.damaged) {
                        t.damaged = true;
                        boom(Fire, t.x, t.y - t.size * 0.3f, 0.05f, 450, now);
                        goto nextDamage;
                    }
            nextDamage:;
            }
            return;
        }
        if (p.t == UnitType::Infantry) {
            int& act = infActual[p.side];
            act = std::max(0, act - p.count);
            int visible = 0;
            for (const Squad& q : squads)
                if (q.side == p.side && !q.doomed && !q.gone) ++visible;
            int kill = std::max(0, visible - std::min(10, act));
            for (int k = 0; k < p.count; ++k) {
                if (kill > 0) {
                    // a squad that loses: its men fall one at a time (a hidden D5 each, owner)
                    Squad* best = nullptr;
                    for (Squad& q : squads)
                        if (q.side == p.side && !q.doomed && !q.gone && (!best || std::fabs(q.y) < std::fabs(best->y))) best = &q;
                    if (best) {
                        best->doomed = true;
                        for (int mi = 0; mi < nMen; ++mi) {
                            Man& m = best->men[mi];
                            const int d5 = 1 + rng.i(5);
                            m.dieAt = now + uint32_t(float((d5 - 1) * 700 + rng.i(400)) * std::max(0.5f, speed));
                        }
                    }
                    --kill;
                } else {  // one of those not drawn: a blast on that side
                    const float x = rng.r(-0.5f, 0.5f), y = -fwd(p.side) * rng.r(0.2f, 0.5f);
                    boom(Fire, x, y, 0.035f, 380, now + uint32_t(k * 100));
                    dust(x, y, now, 4);
                }
            }
            return;
        }
        if (p.t == UnitType::PDS || p.t == UnitType::SpaceDock) {  // Hel-Titans
            for (Tower& t : towers)
                if (t.side == p.side && t.t == p.t && !t.ruined) {
                    t.ruined = true;
                    t.ruinAt = now;
                    boom(Big, t.x, t.y - t.size * 0.3f, 0.12f, 900, now);
                    sparks(t.x, t.y - t.size * 0.3f, 24, 0.9f, now);
                    break;
                }
            return;
        }
        for (int k = 0; k < p.count; ++k)
            for (Walker& w : walkers)
                if (w.side == p.side && w.t == p.t && w.state == 0) {
                    w.state = 1;
                    w.dieAt = now;
                    const float yy = w.y - w.size * 0.3f;
                    boom(Big, w.x, yy, w.t == UnitType::Mech ? 0.09f : 0.04f, 750, now);
                    boom(Flash, w.x, yy, 0.05f, 200, now);
                    sparks(w.x, yy, w.t == UnitType::Mech ? 20 : 8, 0.8f, now);
                    for (int i = 0; i < 6; ++i) {  // bits flying
                        Part c;
                        c.kind = Chunk;
                        c.x = w.x;
                        c.y = yy;
                        c.vx = rng.r(-0.4f, 0.4f);
                        c.vy = rng.r(-0.7f, -0.2f);
                        c.g = 1.4f;
                        c.born = now;
                        c.life = 700;
                        c.size = 0.008f;
                        c.col = darker(col[w.side], 0.4f);
                        c.a0 = 230;
                        parts.push_back(c);
                    }
                    break;
                }
    }

    // ---------------------------------------------------------- behaviour
    void update(uint32_t now) {
        int dtms = int(now - last);
        if (last == 0 || dtms < 0) dtms = 16;
        dtms = std::min(dtms, 60);
        last = now;
        const float dt = float(dtms) / 1000.f;
        const float tt = float(now - t0);
        for (size_t i = 0; i < pending.size();) {
            if (int32_t(now - pending[i].at) >= 0) {
                Pending p = pending[i];
                pending.erase(pending.begin() + long(i));
                applyPending(p, now);
            } else {
                ++i;
            }
        }
        // squads: in, then creep forward; the men shuffle about and shoot
        for (Squad& q : squads) {
            if (q.gone) continue;
            const float dir = fwd(q.side);
            const int32_t in = int32_t(now - q.enterAt);
            if (in < int32_t(q.enterDur)) {  // 0.44.0: planetfall - down from the sky onto their place, slowing at the end
                const float k = in < 0 ? 0.f : float(in) / float(q.enterDur);
                q.x = q.hx;
                q.y = q.hy;
                q.alt = (1.f - k) * (1.f - k) * 1.05f;
            } else {
                const float creep = std::min(0.07f, tt / 1000.f * 0.004f);
                q.x = q.hx;
                q.y = q.hy + dir * creep;
                q.alt = 0;
                if (!q.landed) {  // touchdown: dust round every man
                    q.landed = true;
                    for (int mi = 0; mi < nMen; ++mi)
                        if (q.men[mi].state != 2) dust(q.x + q.men[mi].ox, q.y + q.men[mi].oy + 0.02f, now, spread ? 1 : 2);
                    audio::play(audio::Sfx::Thud, 0.3f, q.x * 0.7f);
                }
            }
            int left = 0;
            for (int mi = 0; mi < nMen; ++mi) {
                Man& m = q.men[mi];
                if (m.state == 2) continue;
                if (m.state == 0 && q.doomed && int32_t(now - m.dieAt) >= 0) {  // he falls
                    m.state = 1;
                    m.dieAt = now;
                    const float mx = q.x + m.ox + m.jx, my = q.y + m.oy + m.jy;
                    boom(Muzzle, mx, my - 0.02f, 0.012f, 160, now);
                    dust(mx, my, now, 2);
                }
                // 0.28.0 (owner): the fallen stay on the field, with their blood - the aftermath
                if (m.state == 0) {
                    ++left;
                    if (in > int32_t(q.enterDur) && int32_t(now - m.nextStep) >= 0) {  // a few steps, taking cover
                        m.jx = rng.r(-0.025f, 0.025f);
                        m.jy = rng.r(-0.018f, 0.018f);
                        m.nextStep = now + 1200 + uint32_t(rng.i(2600));
                    }
                    if (!ended && in > int32_t(q.enterDur) && int32_t(now - m.nextFire) >= 0) {
                        float tx, ty;
                        const gunits::Weapon wp = gunits::infantryWeapon(look[q.side], mi);
                        if (pickTarget(q.side, tx, ty)) {
                            if (wp == gunits::WDefault) {
                                const float mx = q.x + m.ox + m.jx + 0.012f, my = q.y + m.oy + m.jy - 0.012f;
                                const bool laser = rng.f() < 0.5f;
                                fire(laser ? Laser : Bullet, mx, my, tx, ty, laser ? lighter(raw[q.side], 0.35f) : rgb(255, 236, 170), now);
                            } else {  // 0.44.0: the faction's own weapon, from its own muzzle
                                float dx, dy;
                                gunits::muzzle(look[q.side], false, q.side == 0 ? 1 : -1, mi, dx, dy);
                                const float sR = manSize(q.side) / std::max(1.f, R);
                                fireW(wp, q.side, q.x + m.ox + m.jx + dx * sR, q.y + m.oy + m.jy + dy * sR, tx, ty, now);
                            }
                            m.firedAt = now;
                        }
                        m.nextFire = now + uint32_t(float(1000 + rng.i(1900)) * weaponPace(wp) * (spread ? 1.35f : 1.f));
                    }
                } else if (m.state == 1 && int32_t(now - m.dieAt) <= 2600) {
                    ++left;
                }
            }
            if (left == 0) q.gone = true;
        }
        // walkers: mechs stride about with gatling lasers; landed fighters hover
        for (Walker& w : walkers) {
            if (w.state == 2) continue;
            if (w.state == 1) {
                // 0.43.0 (owner, P2): a mech's wreck stays on the field like the fallen soldiers (it smokes a while)
                if (int32_t(now - w.dieAt) > 3200) {
                    if (w.t != UnitType::Mech) w.state = 2;
                } else if (int32_t(now - w.nextSmoke) >= 0) {
                    smoke(w.x, w.y - w.size * 0.3f, now, 0.016f);
                    w.nextSmoke = now + 110;
                }
                continue;
            }
            const int32_t in = int32_t(now - w.enterAt);
            if (in < int32_t(w.enterDur)) {
                const float k = in < 0 ? 0.f : easeOut(float(in) / float(w.enterDur));
                w.x = w.ex + (w.hx - w.ex) * k;
                w.y = w.ey + (w.hy - w.ey) * k;
                if (w.drops) {  // 0.44.0: planetfall
                    const float kk = in < 0 ? 0.f : float(in) / float(w.enterDur);
                    w.alt = (1.f - kk) * (1.f - kk) * 1.15f;
                }
                continue;
            }
            if (!w.landed) {  // a mech touches down: a cloud of dust, a heavy thud
                w.landed = true;
                w.alt = 0;
                dust(w.x - 0.05f, w.y, now, 4);
                dust(w.x + 0.05f, w.y, now, 4);
                audio::play(audio::Sfx::Thud, 0.6f, w.x * 0.7f);
            }
            if (int32_t(now - w.nextMove) >= 0) {
                const float r = w.t == UnitType::Fighter ? 0.35f : 0.08f;
                w.tx = std::max(-0.7f, std::min(0.7f, w.hx + rng.r(-r, r)));
                w.ty = w.hy + rng.r(-r, r) * 0.5f;
                w.nextMove = now + (w.t == UnitType::Fighter ? 600u : 2500u) + uint32_t(rng.i(1500));
            }
            const float rate = w.t == UnitType::Fighter ? 2.5f : 0.6f;
            w.x += (w.tx - w.x) * std::min(1.f, dt * rate);
            w.y += (w.ty - w.y) * std::min(1.f, dt * rate);
            const gunits::Weapon wp = w.t == UnitType::Mech ? gunits::mechWeapon(look[w.side])
                                      : look[w.side] == gunits::Naalu ? gunits::WCrystalFighter : gunits::WDefault;
            if (!ended && w.burst == 0 && int32_t(now - w.nextFire) >= 0) {
                if (pickTarget(w.side, w.aimX, w.aimY)) {
                    w.burst = wp == gunits::WDefault ? (w.t == UnitType::Mech ? 8 : 3) : burstOf(wp);
                    w.burstAt = now;
                }
                w.nextFire = now + uint32_t(float(2300 + rng.i(1300)) * (wp == gunits::WDefault ? 1.f : std::max(1.f, weaponPace(wp) * 0.8f)));
            }
            if (w.burst > 0 && int32_t(now - w.burstAt) >= 0 && wp != gunits::WDefault) {  // 0.44.0: the faction's mech weapon
                float dx, dy;
                gunits::muzzle(look[w.side], w.t == UnitType::Mech, w.side == 0 ? 1 : -1, 0, dx, dy);
                const float sz = w.size, y0 = w.y + dy * sz;
                if (wp == gunits::WFlame2 || wp == gunits::WBigSlime) {  // two at once
                    fireW(wp, w.side, w.x - dx * sz, y0, w.aimX - 0.04f, w.aimY, now);
                    fireW(wp, w.side, w.x + dx * sz, y0, w.aimX + 0.04f, w.aimY, now, 60);
                } else {
                    const float side = w.burst % 2 ? 1.f : -1.f;
                    const bool alternate = wp == gunits::WRedGatling || wp == gunits::WCrystalFighter || wp == gunits::WBigCrystal;
                    fireW(wp, w.side, w.x + dx * sz * (alternate ? side : 1.f), y0, w.aimX, w.aimY, now);
                }
                --w.burst;
                w.burstAt = now + burstGap(wp);
            }
            if (w.burst > 0 && int32_t(now - w.burstAt) >= 0) {  // a gatling burst, one bolt at a time
                const bool mech = w.t == UnitType::Mech;
                const float gx = w.x + (w.burst % 2 ? 0.035f : -0.035f) * (mech ? 1.f : 0.3f), gy = w.y - w.size * (mech ? 0.35f : 0.2f);
                fire(mech ? Gatling : Laser, gx, gy, w.aimX, w.aimY, mech ? lighter(raw[w.side], 0.55f) : rgb(255, 60, 220), now);
                --w.burst;
                w.burstAt = now + 70;
            }
            if (w.damaged && int32_t(now - w.nextSmoke) >= 0) {
                smoke(w.x + w.sx, w.y + w.sy - w.size * 0.3f, now, 0.01f);
                w.nextSmoke = now + 120;
            }
        }
        for (Tower& t : towers) {
            if ((t.ruined || t.damaged) && int32_t(now - t.nextSmoke) >= 0) {
                smoke(t.x + rng.r(-0.05f, 0.05f), t.y - t.size * 0.4f, now, 0.02f, rgb(50, 50, 54));
                t.nextSmoke = now + (t.ruined ? 90u : 200u);
            }
            // 0.44.0 (owner): the Titans' PDS - a giant robot - fires big rockets from its shoulders
            if (look[t.side] == gunits::Titans && t.t == UnitType::PDS && !t.ruined && !ended && int32_t(now - t.nextFire) >= 0) {
                float tx, ty;
                if (pickTarget(t.side, tx, ty)) {
                    fireW(gunits::WRocket, t.side, t.x - t.size * 0.3f, t.y - t.size * 0.94f, tx, ty, now);
                    fireW(gunits::WRocket, t.side, t.x + t.size * 0.3f, t.y - t.size * 0.94f, tx + 0.05f, ty, now, 240);
                }
                t.nextFire = now + 2600 + uint32_t(rng.i(1600));
            }
        }
        specialsUpdate(now);
        // the land itself: fires in the ruins, smokestacks, lava geysers
        for (Feature& f : feats) {
            if (int32_t(now - f.next) < 0) continue;
            if (f.kind == 0) {
                smoke(f.x, f.y - 0.02f, now, 0.012f, rgb(60, 56, 54));
                f.next = now + 260 + uint32_t(rng.i(200));
            } else if (f.kind == 1) {
                smoke(f.x, f.y, now, 0.014f, land == Land::Refinery ? rgb(120, 140, 90) : rgb(90, 90, 90));
                f.next = now + 220 + uint32_t(rng.i(150));
            } else if (f.kind == 2) {  // a geyser goes up
                for (int i = 0; i < 26; ++i) {
                    Part p;
                    p.kind = Lava;
                    p.x = f.x;
                    p.y = f.y;
                    p.vx = rng.r(-0.18f, 0.18f);
                    p.vy = rng.r(-0.75f, -0.35f);
                    p.g = 1.1f;
                    p.born = now + uint32_t(i * 25);
                    p.life = 900 + uint32_t(rng.i(400));
                    p.size = rng.r(0.004f, 0.009f);
                    p.col = i % 3 ? rgb(255, 120, 30) : rgb(255, 220, 90);
                    p.a0 = 255;
                    parts.push_back(p);
                }
                f.next = now + 2600 + uint32_t(rng.i(2600));
            } else {
                f.next = now + 100000;
            }
        }
        for (Shot& s : shots) {  // 0.44.0: what flames, slime, gas, beams and rockets do while they last
            if (int32_t(now - s.born) < 0 || int32_t(now - s.arrive) >= 0) continue;
            if (s.kind == Stream) {
                const uint32_t step = s.style == 3 ? 50u : 24u;
                if (int32_t(now - s.nextPuff) > int32_t(step * 3)) s.nextPuff = now - step * 3;
                while (int32_t(now - s.nextPuff) >= 0) {
                    s.nextPuff += step;
                    streamPuff(s, now);
                }
            } else if (s.kind == Beam && int32_t(now - s.nextPuff) >= 0) {
                s.nextPuff = now + 90;
                sparks(s.x1, s.y1, 2, 0.4f, now, lighter(s.col, 0.4f));
            } else if (s.kind == Rocket && int32_t(now - s.nextPuff) >= 0) {
                s.nextPuff = now + 45;
                float x, y;
                shotAt(s, now, x, y);
                smoke(x, y, now, 0.008f, rgb(150, 150, 150));
            }
        }
        for (size_t i = 0; i < shots.size();) {
            const Shot& s = shots[i];
            if (int32_t(now - s.arrive) >= 0) {
                arrived(s, now);
                shots.erase(shots.begin() + long(i));
            } else {
                ++i;
            }
        }
        for (Strike& st : strikes)  // a slug lands: a blast and dust
            if (!st.landed && int32_t(now - st.at) >= 260) {
                st.landed = true;
                boom(Flash, st.x, st.y, 0.09f, 260, now);
                boom(Big, st.x, st.y, 0.06f, 700, now);
                dust(st.x, st.y, now, 8);
            }
        for (size_t i = 0; i < booms.size();) {
            if (int32_t(now - booms[i].born) > int32_t(booms[i].life)) booms.erase(booms.begin() + long(i));
            else ++i;
        }
        for (size_t i = 0; i < parts.size();) {
            Part& p = parts[i];
            if (int32_t(now - p.born) > int32_t(p.life)) {
                parts.erase(parts.begin() + long(i));
                continue;
            }
            if (int32_t(now - p.born) >= 0) {
                p.vy += p.g * dt;
                p.x += p.vx * dt;
                p.y += p.vy * dt;
                p.size += p.grow * dt;
            }
            ++i;
        }
        if (parts.size() > 600) parts.erase(parts.begin(), parts.begin() + long(parts.size() - 600));
    }

    // ---------------------------------------------------------- the land (drawn once per fight)
    void block3d(Canvas& b, float x, float y, float w, float d, float h, uint32_t c, Rng& r, bool broken) {
        // a building seen from above and a little to the south: top face, front face, a shadow
        b.fillRect(int(x + h * 0.4f), int(y + d * 0.2f), int(w), int(d), rgb(0, 0, 0), 70);
        b.fillRect(int(x), int(y + d - h * 0.0f), int(w), int(h * 0.55f), shade(c, 0.62f));  // the front wall
        b.fillRect(int(x), int(y), int(w), int(d), c);                                    // the roof
        b.frameRect(int(x), int(y), int(w), int(d), shade(c, 0.8f), 1);
        if (broken) {  // a broken roof and rubble
            for (int k = 0; k < 4; ++k)
                b.fillCircle(x + r.r(0, w), y + r.r(0, d), r.r(1.f, 3.f), shade(c, 0.5f), 230);
            float xs[3] = {x + w * 0.5f, x + w, x + w}, ys[3] = {y, y, y + d * 0.6f};
            b.fillPolygon(xs, ys, 3, shade(c, 0.4f), 220);
        } else {
            for (int k = 0; k < int(w / 5.f); ++k)  // windows on the front
                b.fillRect(int(x + 2.f + float(k) * 5.f), int(y + d + 1.f), 2, 2, rgb(255, 220, 140), r.f() < 0.4f ? 200 : 40);
        }
    }

    void tree(Canvas& b, float x, float y, float s, uint32_t c) {
        b.fillCircle(x + s * 0.3f, y + s * 0.4f, s * 0.8f, rgb(0, 0, 0), 60);
        b.fillCircle(x, y, s, c);
        b.fillCircle(x - s * 0.3f, y - s * 0.35f, s * 0.55f, lighter(c, 0.2f), 200);
    }

    void renderLand(const Game& g, float scale) {
        (void)g;
        bg = std::make_unique<Canvas>(area.w, area.h, scale);  // (0.27.0: at the screen's real size)
        bgArea = area;
        Canvas& b = *bg;
        const float saveCx = cx, saveCy = cy;
        cx = float(area.w) / 2.f;
        cy = float(area.h) / 2.f;
        Rng r;
        r.s = uint32_t(def) * 7919u + uint32_t(land) * 131u + 5u;
        const int W = area.w, H = area.h;
        uint32_t ground = rgb(86, 120, 64);
        switch (land) {
            case Land::Ruins: ground = rgb(104, 100, 92); break;
            case Land::Golden: ground = rgb(150, 124, 70); break;
            case Land::Refinery: ground = rgb(78, 86, 60); break;
            case Land::Desert: ground = rgb(196, 140, 78); break;
            case Land::Volcanic: ground = rgb(44, 40, 40); break;
            case Land::Tundra: ground = rgb(200, 214, 228); break;
            case Land::Green: ground = rgb(84, 118, 60); break;
            case Land::Fracture: {
                static const uint32_t fg[4] = {rgb(150, 182, 210), rgb(38, 42, 54), rgb(150, 146, 160), rgb(42, 28, 30)};
                ground = fg[std::max(0, std::min(3, fracLand))];
                break;
            }
            case Land::Storm: ground = rgb(70, 82, 104); break;
        }
        fiss.clear();
        shards.clear();
        spires.clear();
        b.verticalGradient(0, 0, W, H, shade(ground, 0.85f), shade(ground, 1.05f));
        // texture: patches and specks
        for (int i = 0; i < 60; ++i) b.fillCircle(r.r(0, float(W)), r.r(0, float(H)), r.r(6.f, 26.f), shade(ground, r.r(0.85f, 1.12f)), 70);
        for (int i = 0; i < 700; ++i) b.pixel(int(r.r(0, float(W))), int(r.r(0, float(H))), shade(ground, r.r(0.7f, 1.3f)), 180);
        auto spot = [&](float& x, float& y) {  // somewhere off the middle lanes, mostly
            x = r.r(0.f, float(W));
            y = r.r(0.f, float(H));
            if (std::fabs(y - cy) < 0.12f * R && r.f() < 0.7f) y += (y < cy ? -1.f : 1.f) * 0.18f * R;
        };
        switch (land) {
            case Land::Ruins:
            case Land::Golden: {  // a ruined city: roads, broken blocks, fires
                const uint32_t road = land == Land::Golden ? rgb(120, 98, 56) : rgb(72, 70, 66);
                for (int k = 0; k < 4; ++k) {
                    const float y = r.r(0, float(H));
                    b.line(0, y, float(W), y + r.r(-30, 30), road, 7.f, 200);
                    const float x = r.r(0, float(W));
                    b.line(x, 0, x + r.r(-30, 30), float(H), road, 6.f, 200);
                }
                const uint32_t stone = land == Land::Golden ? rgb(196, 166, 96) : rgb(150, 146, 136);
                for (int i = 0; i < 26; ++i) {
                    float x, y;
                    spot(x, y);
                    block3d(b, x, y, r.r(12.f, 30.f), r.r(10.f, 22.f), r.r(8.f, 16.f), shade(stone, r.r(0.8f, 1.1f)), r, r.f() < 0.7f);
                }
                for (int i = 0; i < 90; ++i) b.fillCircle(r.r(0, float(W)), r.r(0, float(H)), r.r(1.f, 2.5f), shade(stone, 0.6f), 200);
                for (int i = 0; i < 5; ++i) {
                    Feature f;
                    f.kind = 0;
                    float x, y;
                    spot(x, y);
                    f.x = (x - cx) / R;
                    f.y = (y - cy) / R;
                    feats.push_back(f);
                }
                break;
            }
            case Land::Refinery: {  // refineries, pipes and glowing irradiated plants
                for (int k = 0; k < 5; ++k) {
                    const float y = r.r(0, float(H)), x = r.r(0, float(W));
                    b.line(x - 80, y, x + 80, y + r.r(-10, 10), rgb(90, 96, 100), 4.f, 230);
                    b.line(x - 80, y - 2, x + 80, y - 2 + r.r(-10, 10), rgb(140, 150, 156), 1.f, 200);
                }
                for (int i = 0; i < 12; ++i) {  // tanks
                    float x, y;
                    spot(x, y);
                    const float rr = r.r(7.f, 14.f);
                    b.fillCircle(x + 4, y + 5, rr, rgb(0, 0, 0), 70);
                    b.fillRect(int(x - rr), int(y), int(2 * rr), int(rr * 0.7f), rgb(110, 116, 120));
                    b.fillCircle(x, y, rr, rgb(150, 158, 162));
                    b.ring(x, y, rr, 1.5f, rgb(90, 96, 100), 220);
                }
                for (int i = 0; i < 6; ++i) {  // towers with smoke
                    float x, y;
                    spot(x, y);
                    b.fillRect(int(x) - 4, int(y) - 22, 8, 26, rgb(96, 92, 88));
                    b.fillCircle(x, y - 22, 4.f, rgb(60, 58, 56));
                    Feature f;
                    f.kind = 1;
                    f.x = (x - cx) / R;
                    f.y = (y - 22 - cy) / R;
                    feats.push_back(f);
                }
                for (int i = 0; i < 30; ++i) {  // irradiated plants
                    float x, y;
                    spot(x, y);
                    const float s = r.r(3.f, 7.f);
                    b.fillCircle(x, y, s * 2.f, rgb(140, 255, 90), 30);
                    for (int k = 0; k < 5; ++k) {
                        const float a = 2 * kPi * float(k) / 5.f + r.r(0, 1);
                        b.line(x, y, x + std::cos(a) * s, y + std::sin(a) * s, rgb(150, 255, 100), 1.5f, 230);
                    }
                    b.fillCircle(x, y, 1.5f, rgb(230, 255, 180), 240);
                }
                break;
            }
            case Land::Desert: {  // dunes and mesas
                for (int i = 0; i < 18; ++i) {
                    const float x = r.r(0, float(W)), y = r.r(0, float(H)), w = r.r(40.f, 110.f);
                    for (int k = 0; k < 4; ++k) b.line(x - w / 2, y + float(k) * 2.f, x + w / 2, y + float(k) * 2.f - 6.f, shade(ground, 1.15f - 0.08f * float(k)), 2.f, 110);
                }
                for (int i = 0; i < 9; ++i) {
                    float x, y;
                    spot(x, y);
                    block3d(b, x, y, r.r(16.f, 36.f), r.r(10.f, 18.f), r.r(10.f, 18.f), rgb(170, 104, 62), r, false);
                }
                for (int i = 0; i < 40; ++i) b.fillCircle(r.r(0, float(W)), r.r(0, float(H)), r.r(1.5f, 4.f), rgb(140, 96, 60), 200);
                break;
            }
            case Land::Volcanic: {  // black rock, rivers of lava, geysers
                for (int k = 0; k < 4; ++k) {
                    float x = r.r(0, float(W)), y = 0;
                    for (int s = 0; s < 14; ++s) {
                        const float nx = x + r.r(-24, 24), ny = y + float(H) / 13.f;
                        b.line(x, y, nx, ny, rgb(255, 90, 20), 9.f, 40);
                        b.line(x, y, nx, ny, rgb(255, 120, 30), 4.f, 230);
                        b.line(x, y, nx, ny, rgb(255, 220, 120), 1.5f, 230);
                        x = nx;
                        y = ny;
                    }
                }
                for (int i = 0; i < 30; ++i) {
                    float x, y;
                    spot(x, y);
                    const float s = r.r(4.f, 11.f);
                    float px[6], py[6];
                    for (int k = 0; k < 6; ++k) {
                        const float a = 2 * kPi * float(k) / 6.f;
                        px[k] = x + std::cos(a) * s * r.r(0.7f, 1.2f);
                        py[k] = y + std::sin(a) * s * r.r(0.6f, 1.f);
                    }
                    b.fillPolygon(px, py, 6, rgb(28, 26, 28));
                    b.fillCircle(x - s * 0.3f, y - s * 0.3f, s * 0.4f, rgb(70, 64, 64), 160);
                }
                for (int i = 0; i < 3; ++i) {
                    Feature f;
                    f.kind = 2;
                    float x, y;
                    spot(x, y);
                    b.fillCircle(x, y, 7.f, rgb(255, 120, 30), 200);
                    b.fillCircle(x, y, 3.f, rgb(255, 230, 140));
                    f.x = (x - cx) / R;
                    f.y = (y - cy) / R;
                    feats.push_back(f);
                }
                break;
            }
            case Land::Tundra: {  // snow, ice and frozen pines
                for (int i = 0; i < 25; ++i) {
                    float x, y;
                    spot(x, y);
                    const float s = r.r(5.f, 13.f);
                    float px[5], py[5];
                    for (int k = 0; k < 5; ++k) {
                        const float a = 2 * kPi * float(k) / 5.f;
                        px[k] = x + std::cos(a) * s * r.r(0.7f, 1.2f);
                        py[k] = y + std::sin(a) * s * r.r(0.6f, 1.f);
                    }
                    b.fillPolygon(px, py, 5, rgb(160, 200, 230));
                    b.fillCircle(x - s * 0.3f, y - s * 0.3f, s * 0.35f, rgb(240, 250, 255), 200);
                }
                for (int i = 0; i < 30; ++i) {
                    float x, y;
                    spot(x, y);
                    const float s = r.r(5.f, 10.f);
                    float px[3] = {x, x - s * 0.7f, x + s * 0.7f}, py[3] = {y - s * 1.6f, y, y};
                    b.fillCircle(x + 3, y + 2, s * 0.6f, rgb(0, 0, 0), 50);
                    b.fillPolygon(px, py, 3, rgb(40, 70, 60));
                    float sx[3] = {x, x - s * 0.35f, x + s * 0.35f}, sy[3] = {y - s * 1.6f, y - s * 0.8f, y - s * 0.8f};
                    b.fillPolygon(sx, sy, 3, rgb(235, 245, 250));
                }
                break;
            }
            case Land::Fracture: {  // 0.43.2: each of the Fracture's planets its own ground - all of it broken
                auto river = [&](uint32_t edge, uint32_t mid, uint32_t core, uint32_t ripple, float w) {  // across the field
                    float xs[17], ys[17];
                    xs[0] = r.r(0.15f, 0.85f) * float(W);
                    ys[0] = -10.f;
                    const float drift = r.r(-1.f, 1.f);
                    for (int k = 1; k < 17; ++k) {
                        xs[k] = xs[k - 1] + drift * 6.f + r.r(-18.f, 18.f);
                        ys[k] = ys[k - 1] + (float(H) + 20.f) / 16.f;
                    }
                    battlefx::river(b, xs, ys, 17, w * 1.7f, edge, 90);
                    battlefx::river(b, xs, ys, 17, w, mid, 255);
                    if (core) battlefx::river(b, xs, ys, 17, w * 0.4f, core, 230);
                    if (ripple)
                        for (int k = 1; k < 17; ++k)
                            for (int j = 0; j < 3; ++j) {
                                const float o = r.r(-0.35f, 0.35f) * w, yy = ys[k] - r.r(0.f, 10.f);
                                b.line(xs[k] + o, yy, xs[k] + o + r.r(3.f, 7.f), yy + 1.f, ripple, 1.f, 110);
                            }
                };
                if (fracLand == 0) {  // Cocytus: a frozen sea - ice plates, shards of ice standing up
                    for (int i = 0; i < 46; ++i) {
                        const float x = r.r(0, float(W)), y = r.r(0, float(H)), sz = r.r(14.f, 34.f);
                        float px[6], py[6];
                        for (int k = 0; k < 6; ++k) {
                            const float a = 2 * kPi * float(k) / 6.f + r.r(-0.3f, 0.3f);
                            px[k] = x + std::cos(a) * sz * r.r(0.7f, 1.15f);
                            py[k] = y + std::sin(a) * sz * r.r(0.55f, 0.9f);
                        }
                        b.fillPolygon(px, py, 6, shade(ground, r.r(0.86f, 1.1f)), 120);
                        for (int k = 0; k < 6; ++k) b.line(px[k], py[k], px[(k + 1) % 6], py[(k + 1) % 6], rgb(236, 246, 255), 1.f, 110);
                    }
                    for (int i = 0; i < 26; ++i) {
                        float x, y;
                        spot(x, y);
                        const float sz = r.r(5.f, 12.f);
                        float px[3] = {x - sz * 0.5f, x + sz * 0.5f, x + r.r(-0.3f, 0.3f) * sz}, py[3] = {y, y, y - sz * r.r(1.2f, 2.2f)};
                        b.fillCircle(x + sz * 0.4f, y + sz * 0.2f, sz * 0.6f, rgb(40, 70, 100), 50);
                        b.fillPolygon(px, py, 3, rgb(190, 222, 244));
                        float hx[3] = {px[0], px[2], x}, hy[3] = {py[0], py[2], y};
                        b.fillPolygon(hx, hy, 3, rgb(240, 250, 255), 200);
                    }
                } else if (fracLand == 1) {  // Styx: black basalt and a dark, storm-whipped sea between
                    for (int k = 0; k < 3; ++k) river(rgb(8, 14, 34), rgb(16, 30, 62), 0, rgb(120, 150, 200), r.r(18.f, 30.f));
                    for (int i = 0; i < 44; ++i) {  // basalt columns, hexagonal tops
                        float x, y;
                        spot(x, y);
                        const float sz = r.r(4.f, 9.f);
                        float px[6], py[6];
                        for (int k = 0; k < 6; ++k) {
                            px[k] = x + std::cos(float(k) * kPi / 3.f) * sz;
                            py[k] = y + std::sin(float(k) * kPi / 3.f) * sz * 0.7f;
                        }
                        b.fillCircle(x + sz * 0.5f, y + sz * 0.6f, sz, rgb(0, 0, 0), 60);
                        b.fillRect(int(x - sz), int(y), int(2.f * sz), int(sz * 0.9f), rgb(24, 26, 34));
                        b.fillPolygon(px, py, 6, rgb(66, 70, 88));
                    }
                } else if (fracLand == 2) {  // Lethe: pale ash, a milky river of forgetting, dead white trees
                    for (int i = 0; i < 34; ++i) {
                        const float x = r.r(0, float(W)), y = r.r(0, float(H)), l = r.r(30.f, 70.f);
                        b.line(x - l / 2, y, x, y - r.r(2.f, 5.f), shade(ground, 1.12f), 2.f, 90);
                        b.line(x, y - 3.f, x + l / 2, y, shade(ground, 1.12f), 2.f, 90);
                    }
                    river(rgb(196, 202, 220), rgb(212, 218, 232), rgb(240, 244, 250), 0, r.r(22.f, 34.f));
                    for (int i = 0; i < 22; ++i) {
                        float x, y;
                        spot(x, y);
                        const float sz = r.r(6.f, 12.f);
                        b.fillCircle(x + sz * 0.4f, y + sz * 0.2f, sz * 0.5f, rgb(0, 0, 0), 40);
                        b.line(x, y, x, y - sz * 1.4f, rgb(226, 224, 232), 1.6f, 230);
                        for (int k = 0; k < 4; ++k) {
                            const float a = -kPi / 2.f + r.r(-1.1f, 1.1f), by = y - sz * r.r(0.6f, 1.3f);
                            b.line(x, by, x + std::cos(a) * sz * 0.6f, by + std::sin(a) * sz * 0.6f, rgb(214, 212, 224), 1.f, 210);
                        }
                    }
                } else {  // Phlegethon: black glass and a river of fire
                    river(rgb(150, 10, 16), rgb(255, 80, 30), rgb(255, 214, 130), 0, r.r(20.f, 30.f));
                    for (int i = 0; i < 34; ++i) {
                        float x, y;
                        spot(x, y);
                        const float sz = r.r(5.f, 12.f);
                        float px[5], py[5];
                        for (int k = 0; k < 5; ++k) {
                            const float a = 2 * kPi * float(k) / 5.f + r.r(-0.3f, 0.3f);
                            px[k] = x + std::cos(a) * sz * r.r(0.7f, 1.2f);
                            py[k] = y + std::sin(a) * sz * r.r(0.5f, 0.9f);
                        }
                        b.fillPolygon(px, py, 5, rgb(22, 14, 18));
                        b.line(px[0], py[0], px[1], py[1], rgb(150, 120, 140), 1.f, 170);
                        b.line(px[2], py[2], px[3], py[3], rgb(255, 80, 40), 1.f, 120);  // the river's light on the glass
                    }
                    for (int i = 0; i < 2; ++i) {
                        Feature f;
                        f.kind = 2;
                        float x, y;
                        spot(x, y);
                        b.fillCircle(x, y, 7.f, rgb(255, 90, 30), 200);
                        b.fillCircle(x, y, 3.f, rgb(255, 220, 140));
                        f.x = (x - cx) / R;
                        f.y = (y - cy) / R;
                        feats.push_back(f);
                    }
                }
                {  // the ground broken out from one impact, its cracks glowing (a light runs down them: landAnim)
                    const float hw = float(W) * 0.5f / R, hh = float(H) * 0.5f / R;
                    const float ix = (r.f() < 0.5f ? -1.f : 1.f) * r.r(0.35f, 0.6f), iy = r.r(-0.15f, 0.15f);
                    battlefx::crackNet(fiss, nullptr, uint32_t(def) * 131u + 7u, 0, ix, iy, 0.f, 9, -hw, hw, -hh, hh);
                    battlefx::drawCracks(b, fiss, cx, cy, R, 255, fracLand == 0 ? 170 : 255);
                    float px[9], py[9];  // the crater at the break, glowing in its depths
                    for (int k = 0; k < 9; ++k) {
                        const float a = 2 * kPi * float(k) / 9.f, rr = R * 0.06f * r.r(0.75f, 1.2f);
                        px[k] = X(ix) + std::cos(a) * rr;
                        py[k] = Y(iy) + std::sin(a) * rr * 0.7f;
                    }
                    b.fillPolygon(px, py, 9, rgb(22, 6, 10), 220);
                    battlefx::ellipse(b, X(ix), Y(iy) + R * 0.008f, R * 0.03f, R * 0.018f, rgb(255, 80, 40), 170);
                }
                for (int i = 0; i < 10; ++i) {
                    float x, y;
                    spot(x, y);
                    GShard gs;
                    gs.x = (x - cx) / R;
                    gs.y = (y - cy) / R;
                    gs.s = r.r(0.025f, 0.05f);
                    gs.rot = r.r(0.f, 2.f * kPi);
                    gs.ph = r.r(0.f, 6.28f);
                    shards.push_back(gs);
                }
                break;
            }
            case Land::Storm: {  // 0.43.2: Thunder's Edge - a storm-lashed plateau: wet slate, puddles, burns, lightning spires
                for (int i = 0; i < 80; ++i) {  // the wet sheen
                    const float x = r.r(0, float(W)), y = r.r(0, float(H)), l = r.r(10.f, 30.f);
                    b.line(x, y, x + l, y + l * 0.35f, shade(ground, 1.3f), 1.f, 50);
                }
                for (int i = 0; i < 16; ++i) {  // slabs of rock
                    float x, y;
                    spot(x, y);
                    const float w = r.r(14.f, 30.f), d = r.r(8.f, 16.f);
                    b.fillRect(int(x + 5.f), int(y + 4.f), int(w), int(d), rgb(0, 0, 0), 70);
                    b.fillRect(int(x), int(y + d), int(w), int(d * 0.45f), rgb(44, 50, 64));
                    b.fillRect(int(x), int(y), int(w), int(d), rgb(82, 92, 114));
                    b.line(x, y, x + w, y, rgb(140, 152, 180), 1.f, 160);
                }
                for (int i = 0; i < 24; ++i) {  // puddles
                    const float x = r.r(0, float(W)), y = r.r(0, float(H)), rx = r.r(6.f, 18.f);
                    battlefx::ellipse(b, x, y, rx, rx * 0.45f, rgb(104, 122, 156), 150);
                    battlefx::ellipse(b, x - rx * 0.2f, y - rx * 0.1f, rx * 0.5f, rx * 0.15f, rgb(190, 204, 230), 90);
                }
                for (int i = 0; i < 7; ++i) {  // glassy veins the lightning has fused into the rock, faintly glowing
                    float x = r.r(0, float(W)), y = r.r(0, float(H)), a = r.r(0.f, 2.f * kPi);
                    for (int k = 0; k < 7; ++k) {
                        a += r.r(-0.6f, 0.6f);
                        const float l = r.r(8.f, 16.f), nx = x + std::cos(a) * l, ny = y + std::sin(a) * l * 0.7f;
                        b.line(x, y, nx, ny, rgb(90, 150, 255), 4.f, 40);
                        b.line(x, y, nx, ny, rgb(170, 210, 255), 1.2f, 170);
                        if (k == 3) {
                            const float ba = a + r.r(0.7f, 1.2f) * (r.f() < 0.5f ? -1.f : 1.f);
                            b.line(nx, ny, nx + std::cos(ba) * l * 0.8f, ny + std::sin(ba) * l * 0.55f, rgb(170, 210, 255), 1.f, 130);
                        }
                        x = nx;
                        y = ny;
                    }
                }
                for (int i = 0; i < 9; ++i) {  // where lightning has struck before
                    float x, y;
                    spot(x, y);
                    const float sz = r.r(8.f, 16.f);
                    for (int k = 0; k < 9; ++k) {
                        const float a = 2 * kPi * float(k) / 9.f + r.r(-0.2f, 0.2f);
                        b.line(x, y, x + std::cos(a) * sz * r.r(0.4f, 0.9f), y + std::sin(a) * sz * 0.6f * r.r(0.4f, 0.9f), rgb(18, 20, 26), 1.5f, 130);
                    }
                    b.fillCircle(x, y, sz * 0.45f, rgb(14, 16, 20), 150);
                    b.fillCircle(x, y, sz * 0.2f, rgb(10, 12, 16), 220);
                }
                for (int i = 0; i < 5; ++i) {  // the spires that draw the lightning down
                    float x, y;
                    spot(x, y);
                    const float h = r.r(0.1f, 0.16f);
                    b.fillCircle(x + 4.f, y + 3.f, 6.f, rgb(0, 0, 0), 80);
                    b.fillRect(int(x) - 5, int(y) - 3, 10, 6, rgb(52, 58, 72));
                    b.line(x, y, x, y - h * R, rgb(40, 44, 54), 4.f, 255);
                    b.line(x - 0.5f, y, x - 0.5f, y - h * R, rgb(150, 162, 186), 1.5f, 255);
                    b.ring(x, y - h * R * 0.7f, 3.f, 1.f, rgb(150, 170, 210), 200);
                    Spire sp;
                    sp.x = (x - cx) / R;
                    sp.y = (y - cy) / R;
                    sp.h = h;
                    spires.push_back(sp);
                }
                break;
            }
            case Land::Green: {  // the owner's picture: green fields, purple trees, rocky hills
                for (int i = 0; i < 14; ++i) {  // hills
                    float x, y;
                    spot(x, y);
                    const float s = r.r(10.f, 22.f);
                    float px[3] = {x, x - s, x + s}, py[3] = {y - s * 1.1f, y + s * 0.3f, y + s * 0.3f};
                    b.fillPolygon(px, py, 3, rgb(130, 104, 80));
                    float hx[3] = {x, x + s, x + s * 0.2f}, hy[3] = {y - s * 1.1f, y + s * 0.3f, y + s * 0.3f};
                    b.fillPolygon(hx, hy, 3, rgb(96, 76, 60));
                }
                for (int i = 0; i < 60; ++i) {
                    float x, y;
                    spot(x, y);
                    tree(b, x, y, r.r(4.f, 8.f), r.f() < 0.75f ? rgb(120, 50, 110) : rgb(60, 110, 60));
                }
                for (int i = 0; i < 4; ++i) {
                    float x, y;
                    spot(x, y);
                    block3d(b, x, y, r.r(14.f, 22.f), r.r(10.f, 14.f), r.r(8.f, 12.f), rgb(170, 176, 186), r, false);
                }
                break;
            }
        }
        // a faint hex grid (Advance Wars)
        {
            const float hr = R / 5.2f, stepX = hr * 1.5f, stepY = hr * kSqrt3;
            for (int c = -1; float(c) * stepX < float(W) + hr; ++c)
                for (int rr = -1; float(rr) * stepY < float(H) + hr; ++rr) {
                    const float hx = float(c) * stepX, hy = float(rr) * stepY + ((c & 1) ? stepY / 2.f : 0.f);
                    for (int k = 0; k < 3; ++k) {
                        const float a0 = kPi / 3.f * float(k), a1 = kPi / 3.f * float(k + 1);
                        b.line(hx + std::cos(a0) * hr, hy + std::sin(a0) * hr, hx + std::cos(a1) * hr, hy + std::sin(a1) * hr, rgb(0, 0, 0), 1.f,
                               26);
                    }
                }
        }
        if (retro::hd()) {  // Voidfall (0.33.1): the land under the void's dusk - a violet wash, the glow of the
                            // tendrils from the upper right, a shadow creeping in from the lower left
            const bool own = land == Land::Fracture || land == Land::Storm;  // 0.43.2: lighter, so their own colours show
            b.fillRect(0, 0, W, H, land == Land::Storm ? rgb(14, 24, 64) : rgb(34, 8, 54), own ? 56 : 92);
            for (int i = 0; i < 9; ++i) {
                const float k = float(i) / 8.f;
                b.fillCircle(float(W) * 1.05f, -float(H) * 0.05f, float(H) * (0.2f + 0.6f * k), i % 2 ? rgb(255, 80, 150) : rgb(255, 130, 70),
                             int(34.f * (1.f - k)) + 4);
                b.fillCircle(-float(W) * 0.1f, float(H) * 1.1f, float(H) * (0.3f + 0.55f * k), rgb(12, 2, 28), int(40.f * (1.f - k)) + 8);
            }
        }
        bakeSpecials();  // 0.44.0
        cx = saveCx;
        cy = saveCy;
    }

    // ---------------------------------------------------------- 0.44.0: overgrowth and moss, drawn into the land
    uint32_t wash(uint32_t c) const { return retro::hd() ? mix(c, rgb(34, 8, 54), 92) : style == 1 ? mix(c, rgb(40, 255, 90), 40) : c; }
    // the overgrowth between radius r0 and r1 of a patch (so a growing patch only adds its new ring)
    void stampGrowth(const Patch& pt, float r0, float r1) {
        if (!bg || r1 <= r0) return;
        Canvas& b = *bg;
        const float sx = cx, sy = cy;
        cx = float(bgArea.w) / 2.f;
        cy = float(bgArea.h) / 2.f;
        Rng r;
        r.s = pt.seed ^ (uint32_t(r1 * 100000.f) * 2654435761u);
        if (!r.s) r.s = 1;
        const int n = std::min(70, std::max(3, int((r1 * r1 - r0 * r0) / (0.02f * 0.02f) * 0.9f)));
        static const uint32_t greens[3] = {rgb(38, 80, 32), rgb(56, 110, 42), rgb(86, 142, 56)};
        for (int i = 0; i < n; ++i) {
            const float a = r.r(0, 2 * kPi), rr = std::sqrt(r.r(r0 * r0, r1 * r1));
            const float bx = X(pt.x + std::cos(a) * rr), by = Y(pt.y + std::sin(a) * rr * 0.7f);
            b.fillCircle(bx, by, R * r.r(0.013f, 0.026f), wash(greens[r.i(3)]), 235);
            if (i % 3 == 0) b.line(bx, by, bx + r.r(-2.f, 2.f), by - R * r.r(0.02f, 0.036f), wash(rgb(116, 172, 68)), 1.f, 220);
            if (style == 2 && r.i(7) == 0) {  // Voidfall: the overgrowth glows in the dusk (bioluminescent buds)
                const uint32_t bio = r.i(2) ? rgb(90, 240, 220) : rgb(255, 110, 200);
                b.fillCircle(bx + 1.f, by - 1.f, R * 0.012f, bio, 50);
                b.fillCircle(bx + 1.f, by - 1.f, std::max(1.f, R * 0.005f), lighter(bio, 0.5f), 240);
            } else if (r.i(16) == 0) {
                b.fillCircle(bx + 1.f, by - 1.f, std::max(1.f, R * 0.006f), wash(r.i(2) ? rgb(200, 110, 230) : rgb(250, 220, 90)), 230);
            }
        }
        cx = sx;
        cy = sy;
    }
    void bakeSpecials() {
        if (!bg) return;
        if (look[1] == gunits::Arborec) {  // (owner) the Arborec defend from an overgrowth covering half the field
            Patch half;
            half.seed = uint32_t(def) * 977u + 13u;
            Rng r;
            r.s = half.seed;
            for (float y = 0.03f; y < 0.92f; y += 0.034f)
                for (float x = -1.f; x <= 1.f; x += 0.034f) {
                    if (y < 0.1f && r.f() < 0.45f) continue;  // a ragged edge
                    Patch pt;
                    pt.x = x + r.r(-0.015f, 0.015f);
                    pt.y = y + r.r(-0.012f, 0.012f) + (y < 0.1f ? 0.03f * std::sin(x * 9.f) : 0.f);
                    pt.seed = r.next() | 1u;
                    stampGrowth(pt, 0.f, 0.022f);
                }
        }
        if (anyLook(gunits::Khrask)) {  // (owner) patches of overgrowth across the field
            Rng r;
            r.s = uint32_t(def) * 331u + 7u;
            for (int i = 0; i < 8; ++i) {
                Patch pt;
                pt.y = r.r(-0.7f, 0.7f);
                pt.x = r.r(-halfWidth(pt.y) + 0.1f, halfWidth(pt.y) - 0.1f);
                pt.seed = r.next() | 1u;
                stampGrowth(pt, 0.f, r.r(0.06f, 0.12f));
                Canvas& b = *bg;
                const float sx = cx, sy = cy;
                cx = float(bgArea.w) / 2.f;
                cy = float(bgArea.h) / 2.f;
                for (int k = 0; k < 3; ++k) {  // little green crystals in the moss
                    const float x = X(pt.x + r.r(-0.04f, 0.04f)), y = Y(pt.y + r.r(-0.03f, 0.03f)), h = R * r.r(0.015f, 0.03f);
                    float px[3] = {x - h * 0.3f, x + h * 0.3f, x}, py[3] = {y, y, y - h};
                    b.fillPolygon(px, py, 3, wash(rgb(160, 255, 190)), 230);
                }
                cx = sx;
                cy = sy;
            }
        }
        for (Patch& pt : patches) {  // the land was redrawn: put back what has grown so far
            const float r = pt.drawn;
            pt.drawn = 0;
            stampGrowth(pt, 0.f, r);
            pt.drawn = r;
        }
    }
    void growPatches() {
        for (Patch& pt : patches)
            if (pt.r - pt.drawn >= 0.008f || (pt.r >= pt.rmax && pt.drawn < pt.rmax)) {
                stampGrowth(pt, pt.drawn, pt.r);
                pt.drawn = pt.r;
            }
    }
    void drawCraters(Canvas& c, uint32_t now) {
        for (const Crater& cr : craters) {
            const int age = int(now - cr.born);
            const int a = age > 6500 ? std::max(0, 255 - (age - 6500) * 255 / 1500) : 255;
            const float x = X(cr.x), y = Y(cr.y), r = cr.r * R;
            battlefx::ellipse(c, x, y + r * 0.08f, r * 1.15f, r * 0.62f, rgb(120, 104, 90), a * 120 / 255);
            battlefx::ellipse(c, x, y, r, r * 0.55f, rgb(36, 30, 28), a * 220 / 255);
            battlefx::ellipse(c, x - r * 0.15f, y - r * 0.08f, r * 0.6f, r * 0.3f, rgb(20, 16, 16), a * 200 / 255);
            if (age < 1600) gunits::glow(c, x, y, r * 0.6f, rgb(255, 110, 40), (1600 - age) * 200 / 1600, style);
        }
    }
    void drawClouds(Canvas& c, uint32_t now) {
        for (const Cloud& cl : clouds) {
            const int age = int(now - cl.born), life = int(cl.life);
            const float fade = age < 400 ? float(age) / 400.f : age > life - 1200 ? float(life - age) / 1200.f : 1.f;
            if (fade <= 0.f) continue;
            const float t = float(age) / 1000.f;
            static const uint32_t gc[3] = {rgb(140, 190, 66), rgb(172, 204, 90), rgb(110, 150, 50)};
            for (int i = 0; i < 5; ++i) {
                const float ang = cl.seed + float(i) * 1.26f + t * 0.3f;
                const float rr = R * (0.04f + 0.012f * float(i % 3) + 0.004f * t);
                c.fillCircle(X(cl.x + std::cos(ang) * 0.035f) + std::sin(t + float(i)) * 2.f, Y(cl.y + std::sin(ang) * 0.025f) - R * 0.03f, rr, gc[i % 3],
                             int(80.f * fade));
            }
        }
    }
    // the factions' "weather": Nekro drones swarming the sky, Zelian meteors, the Kyro miasma
    void specialWeather(Canvas& c, uint32_t now) {
        const float t = float(now - t0) / 1000.f;
        if (anyLook(gunits::Kyro)) {  // a sickly miasma over everything
            c.fillRect(area.x, area.y, area.w, area.h, rgb(150, 170, 60), 26);
            Rng r;
            r.s = 113u;
            for (int i = 0; i < 6; ++i) {
                const float sp = r.r(0.03f, 0.06f), x0 = r.r(0.f, 2.8f), y = r.r(-0.75f, 0.75f);
                const float x = std::fmod(x0 + t * sp, 2.8f) - 1.4f;
                for (int k = 0; k < 3; ++k)
                    c.fillCircle(X(x + 0.12f * float(k)), Y(y + 0.03f * std::sin(float(k) * 1.9f + t * 0.4f)), R * (0.17f - 0.03f * float(k)), rgb(166, 186, 74), 24);
            }
            for (int i = 0; i < 24; ++i) {  // spores drifting up
                const float sp = r.r(0.04f, 0.1f), x0 = r.r(-1.f, 1.f), y0 = r.r(-1.f, 1.f);
                const float y = 1.f - std::fmod(1.f - y0 + t * sp + 10.f, 2.f), x = x0 + 0.04f * std::sin(t * 0.8f + float(i));
                c.fillCircle(X(x), Y(y), r.r(0.8f, 1.6f), rgb(200, 220, 110), 170);
            }
        }
        if (anyLook(gunits::Nekro)) {  // swarms of drones wheeling over the field (owner, 0.44.0 review: more of them)
            Rng r;
            r.s = 131u;
            for (int sw = 0; sw < 5; ++sw) {
                const float fx = 0.21f + 0.06f * float(sw), fy = 0.33f + 0.045f * float(sw), ph = float(sw) * 2.1f;
                const float scx = 0.62f * std::sin(t * fx + ph), scy = -0.15f + 0.5f * std::sin(t * fy + ph * 1.3f);
                for (int i = 0; i < 11; ++i) {
                    const float o = r.r(0.f, 2 * kPi), rad = r.r(0.03f, 0.13f), w = r.r(1.2f, 2.4f);
                    const float x = X(scx + std::cos(t * w + o) * rad), y = Y(scy + std::sin(t * w * 1.3f + o) * rad * 0.7f);
                    const float sz = R * 0.02f, flap = std::sin(t * 18.f + o * 5.f) * sz * 0.35f;
                    c.fillCircle(x + sz * 1.2f, y + R * 0.16f, sz * 0.7f, rgb(0, 0, 0), 40);  // its shadow on the ground
                    float lx[3] = {x, x - sz * 1.4f, x - sz * 0.9f}, ly[3] = {y, y - sz * 0.4f + flap, y + sz * 0.3f};
                    float rx[3] = {x, x + sz * 1.4f, x + sz * 0.9f}, ry[3] = {y, y - sz * 0.4f + flap, y + sz * 0.3f};
                    c.fillPolygon(lx, ly, 3, rgb(48, 48, 54), 220);
                    c.fillPolygon(rx, ry, 3, rgb(48, 48, 54), 220);
                    c.fillCircle(x, y, sz * 0.55f, rgb(24, 24, 28), 240);
                    gunits::glow(c, x, y, sz * 0.5f, rgb(255, 40, 30), 200, style);
                }
            }
        }
        for (const Meteor& mt : meteors) {  // Zelian: meteors coming down
            const float k = 1.f - std::max(0.f, std::min(1.f, float(int32_t(mt.at - now)) / 480.f));
            const float hx = mt.x + (1.f - k) * 0.55f, hy = mt.y - (1.f - k) * 1.1f;
            const float x = X(hx), y = Y(hy), tx = X(hx + 0.18f), ty = Y(hy - 0.36f);
            c.line(tx, ty, x, y, rgb(255, 140, 60), 4.f, 80);
            c.line(tx, ty, x, y, rgb(255, 220, 160), 1.5f, 200);
            gunits::glow(c, x, y, R * 0.03f, rgb(255, 160, 80), 230, style);
            c.fillCircle(x, y, std::max(1.f, R * 0.01f), rgb(255, 250, 230), 255);
        }
    }

    // ---------------------------------------------------------- drawing
    // 0.44.0: every unit through ground_units (the "Default" look = the game's own soldiers, mechs and structures)
    gunits::Pose pose(int side, float px, float py, float sz, uint32_t now, float phase, int idx) const {
        gunits::Pose p;
        p.x = px;
        p.y = py;
        p.s = sz;
        p.col = painting() ? pcol[side] : col[side];
        p.dir = side == 0 ? 1 : -1;
        p.now = now;
        p.phase = phase;
        p.idx = idx;
        p.style = style;
        p.a = gunits::phaseAlpha(look[side], now, phase + float(idx));
        return p;
    }
    void phaseShimmer(Canvas& c, const gunits::Pose& p, float r, float cy) const {  // Creuss / Mahact, while phased out
        if (p.a >= 250) return;
        const uint32_t sc = look[p.dir > 0 ? 0 : 1] == gunits::Creuss ? rgb(80, 200, 255) : rgb(255, 70, 60);
        c.ring(p.x, cy, r, std::max(1.f, r * 0.08f), sc, (255 - p.a) * 3 / 4);
    }

    // ---------------------------------------------------------- Retrowave (0.50.0): the faction troops as pixel art
    // Each unit is drawn alone and cut into the fight's grid pixels (pxship::pixelize). The pose is rounded so a
    // picture is made once and reused: the stride in 8 steps, and for looks that move with the clock the time in
    // 125 ms steps and the phase in 16 (a pixel-art animation of 8 frames a second).
    static uint64_t mixKey(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
    // (Voidfall's painted troops, 0.51.1: step 80 ms and staggered - each phase step turns over at its own moment, so
    // the pictures are not all made again in the same frame)
    uint64_t poseKey(int kind, gunits::Look l, gunits::Pose& p, bool timed, uint32_t step = 125, bool stagger = false, int phases = 16,
                     int walks = 8) const {
        uint64_t h = 1469598103934665603ull;
        h = mixKey(h, uint64_t(kind));
        h = mixKey(h, uint64_t(l));
        h = mixKey(h, uint64_t(p.col));
        h = mixKey(h, uint64_t(int(p.s * 4.f)));
        h = mixKey(h, uint64_t(p.dir + 2));
        h = mixKey(h, uint64_t(p.idx));
        h = mixKey(h, p.firing ? 1u : 2u);
        h = mixKey(h, uint64_t(p.style));
        const float wq = 2.f * kPi / float(walks);
        p.walk = std::floor(p.walk / wq) * wq;
        h = mixKey(h, uint64_t(int64_t(p.walk / wq) + 100000));
        h = mixKey(h, uint64_t(int(p.lift * 2.f) + 7));
        if (timed) {
            const float pq = 2.f * kPi / float(phases);
            p.phase = std::floor(p.phase / pq) * pq;
            const uint32_t off = stagger ? uint32_t(int64_t(p.phase / pq) % phases) * step / uint32_t(phases) : 0u;
            p.now = (p.now + off) / step * step;
            h = mixKey(h, uint64_t(p.now));
            h = mixKey(h, uint64_t(int64_t(p.phase / pq) + 1000));
        }
        return h;
    }
    // draws fn's picture of p as pixel art (box in game units from p's anchor); false: no grid (draw it as before)
    bool pxDraw(Canvas& c, uint64_t key, const gunits::Pose& p, float l, float t, float w, float h, int alpha, uint32_t now,
                const std::function<void(Canvas&, const gunits::Pose&)>& fn) {
        if (grid.f <= 0) return false;
        const pxship::Pixelized* px = pxship::pixelize(
            c, grid, key, l, t, w, h,
            [&](Canvas& o, float ax, float ay) {
                gunits::Pose q = p;
                q.x = ax;
                q.y = ay;
                q.a = 255;
                fn(o, q);
            },
            now);
        if (!px) return false;
        pxship::drawPixelized(c, *px, c.fX(p.x), c.fY(p.y), grid, alpha);
        return true;
    }

    // ---------------------------------------------------------- Voidfall (0.51.1, owner): the painted troops
    bool painting() const { return style == 2 && gunits::voidPaint(); }
    // draws fn's picture of p painted (ui/painted.h; box in game units from p's anchor); false: draw it as before
    bool paintDraw(Canvas& c, uint64_t key, const gunits::Pose& p, float l, float t, float w, float h, int alpha, uint32_t now,
                   const std::function<void(Canvas&, const gunits::Pose&)>& fn, const painted::Light& light = painted::Light()) {
        const painted::Lit* lit = painted::relight(
            c, key, l, t, w, h,
            [&](Canvas& o, float ax, float ay) {
                gunits::Pose q = p;
                q.x = ax;
                q.y = ay;
                q.a = 255;
                fn(o, q);
            },
            now, light);
        if (!lit) return false;
        painted::drawLit(c, *lit, p.x, p.y, alpha);
        return true;
    }

    void drawSquad(Canvas& c, const Squad& q, uint32_t now) {
        const int32_t in = int32_t(now - q.enterAt);
        if (in < 0) return;  // (still in orbit)
        for (int i = 0; i < nMen; ++i) drawMan(c, q, i, now);
    }
    void drawMan(Canvas& c, const Squad& q, int i, uint32_t now) {
        const float s = manSize(q.side);
        const gunits::Look lk = look[q.side];
        const float alt = q.alt * R;
        {
            const Man& m = q.men[i];
            if (m.state == 2) return;
            const float px = X(q.x + m.ox + m.jx), py = Y(q.y + m.oy + m.jy);
            gunits::Pose p = pose(q.side, px, py, s, now, m.phase, i);
            if (m.state == 1) {
                p.a = 255;
                float pk = float(now - m.dieAt) / 1500.f;
                if (grid.f > 0) {  // Retrowave (0.50.0): pixel art
                    gunits::Pose r = p;
                    pk = std::floor(std::min(2.f, pk) * 12.f) / 12.f;
                    uint64_t key = poseKey(2, lk, r, lk == gunits::Mortheus);
                    const float pq = 2.f * kPi / 16.f;
                    r.phase = std::floor(r.phase / pq) * pq;
                    key = mixKey(mixKey(key, uint64_t(int(pk * 12.f))), uint64_t(int64_t(r.phase / pq) + 2000));
                    if (pxDraw(c, key, r, -1.4f * s, -1.1f * s, 2.8f * s, 2.2f * s, 255, now,
                               [&](Canvas& o, const gunits::Pose& z) { gunits::fallen(o, lk, z, pk); }))
                        return;
                } else if (painting()) {  // Voidfall (0.51.1): painted, more softly (the blood keeps its colour)
                    gunits::Pose r = p;
                    pk = std::floor(std::min(2.f, pk) * 12.f) / 12.f;
                    uint64_t key = poseKey(2, lk, r, lk == gunits::Mortheus, 100, true, 8);
                    const float pq = 2.f * kPi / 16.f;
                    r.phase = std::floor(r.phase / pq) * pq;
                    key = mixKey(mixKey(key, uint64_t(int(pk * 12.f))), uint64_t(int64_t(r.phase / pq) + 2000));
                    painted::Light soft;
                    soft.violet = 35;
                    soft.shade = 80;
                    soft.rim = 150;
                    if (paintDraw(c, key, r, -1.4f * s, -1.1f * s, 2.8f * s, 2.2f * s, 255, now,
                                  [&](Canvas& o, const gunits::Pose& z) { gunits::fallen(o, lk, z, pk); }, soft))
                        return;
                }
                gunits::fallen(c, lk, p, pk);
                return;
            }
            p.firing = m.firedAt && int32_t(now - m.firedAt) < 90;
            p.walk = m.phase + std::sin(float(now % 3600000u) / 900.f + m.phase) * 0.3f;
            if (alt > 0.5f) {  // planetfall: the burn of the drop above him, his shadow on the ground
                const float k = std::min(1.f, q.alt / 1.05f);
                const float top = py - alt - s * 0.5f;
                c.line(px, top, px, top - std::min(alt * 0.6f, R * 0.35f), rgb(255, 200, 140), std::max(1.f, s * 0.12f), int(150.f * k));
                gunits::glow(c, px, py - alt + s * 0.44f, s * 0.22f, rgb(255, 150, 60), int(200.f * k), style);
                p.y -= alt;
                p.lift = alt;
            }
            bool drawn = false;
            if (grid.f > 0 && p.lift <= s) {  // Retrowave (0.50.0): pixel art (not while dropping from orbit)
                gunits::Pose r = p;
                const uint64_t key = poseKey(1, lk, r, gunits::infantryTimed(lk));
                drawn = pxDraw(c, key, r, -1.5f * s, -1.9f * s - r.lift, 3.f * s, 2.9f * s + r.lift, p.a, now,
                               [&](Canvas& o, const gunits::Pose& z) { gunits::infantry(o, lk, z); });
            } else if (painting() && p.lift <= s) {  // Voidfall (0.51.1): painted
                gunits::Pose r = p;
                r.idx = gunits::paintIdx(lk, i);
                if (gunits::paintedByHand(lk, i)) {  // painted by hand: only kept (the stride in 16 steps)
                    const uint64_t key = mixKey(poseKey(1, lk, r, false, 100, false, 8, 16), 0xa11u);
                    painted::Light none;
                    none.violet = none.shade = none.lift = none.rim = none.key = 0;
                    drawn = paintDraw(c, key, r, -1.5f * s, -1.9f * s - r.lift, 3.f * s, 2.9f * s + r.lift, p.a, now,
                                      [&](Canvas& o, const gunits::Pose& z) { gunits::infantry(o, lk, z); }, none);
                } else {
                    const uint64_t key = poseKey(1, lk, r, gunits::infantryTimed(lk), 100, true, 8);
                    drawn = paintDraw(c, key, r, -1.5f * s, -1.9f * s - r.lift, 3.f * s, 2.9f * s + r.lift, p.a, now,
                                      [&](Canvas& o, const gunits::Pose& z) { gunits::infantry(o, lk, z); });
                }
            }
            if (!drawn) gunits::infantry(c, lk, p);
            phaseShimmer(c, p, s * 0.3f, p.y - s * 0.1f);
        }
    }

    void drawWalker(Canvas& c, const Walker& w, uint32_t now) {
        if (w.state == 2) return;
        const gunits::Look lk = look[w.side];
        const float px = X(w.x), py = Y(w.y);
        const float sz = w.size * R;
        if (w.t == UnitType::Fighter) {  // hovering, its shadow on the ground
            const float hov = 0.06f * R + std::sin(float(now % 3600000u) / 300.f + w.phase) * 2.f;
            c.fillCircle(px + 4, py + 3, sz * 0.3f, rgb(0, 0, 0), 70);
            if (w.state == 0) {
                gunits::Pose p = pose(w.side, px, py - hov, sz, now, w.phase, 0);
                bool own = false;
                if (grid.f > 0 && gunits::hasFighter(lk)) {  // Retrowave (0.50.0): the Naalu crystal as pixel art
                    gunits::Pose r = p;
                    const uint64_t key = poseKey(5, lk, r, true);
                    own = pxDraw(c, key, r, -1.1f * sz, -1.1f * sz, 2.2f * sz, 2.2f * sz, p.a, now,
                                 [&](Canvas& o, const gunits::Pose& z) { gunits::fighter(o, lk, z); });
                } else if (painting() && gunits::hasFighter(lk)) {  // Voidfall (0.51.1): painted
                    gunits::Pose r = p;
                    const uint64_t key = poseKey(5, lk, r, true, 100, true, 8);
                    own = paintDraw(c, key, r, -1.1f * sz, -1.1f * sz, 2.2f * sz, 2.2f * sz, p.a, now,
                                    [&](Canvas& o, const gunits::Pose& z) { gunits::fighter(o, lk, z); });
                }
                if (!own && !gunits::fighter(c, lk, p)) {
                    if (painting()) {
                        icons::drawUnitArtPainted(c, "unit/fighter", px, py - hov, int(sz), int(sz * 0.8f), p.col, w.side == 1, p.a);
                    } else if (const pxship::Sprite* sp = pxship::fit(c, grid, UnitType::Fighter, sz, sz * 0.8f, col[w.side])) {
                        pxship::Look k;
                        k.faceLeft = w.side == 1;
                        k.alpha = p.a;
                        k.seed = uint32_t(w.phase * 1000.f);
                        pxship::draw(c, *sp, c.fX(px), c.fY(py - hov), grid, k, now);
                    } else {
                        icons::drawUnitArt(c, "unit/fighter", px, py - hov, int(sz), int(sz * 0.8f), col[w.side], w.side == 1, p.a);
                    }
                }
            }
            return;
        }
        if (int32_t(now - w.enterAt) < 0 && w.drops) return;  // (not dropped yet)
        // a mech: shadow, a little bob as it strides
        const float bob = w.state == 0 && w.landed ? std::fabs(std::sin(float(now % 3600000u) / 260.f + w.phase)) * 1.5f : 0.f;
        const float alt = w.alt * R;
        gunits::Pose p = pose(w.side, px, py - alt, sz, now, w.phase, 0);
        p.lift = alt;
        p.walk = float(now % 3600000u) / 260.f + w.phase;
        p.firing = w.burst > 0;
        bool wreck = w.state == 1;
        if (wreck && lk == gunits::Mortheus) {  // a nebula leaves nothing: it fades away (1.5 s)
            const int since = int(now - w.dieAt);
            if (since >= 1500) return;
            wreck = false;
            p.a = 255 - since * 255 / 1500;
        }
        if (alt > 0.5f) {  // planetfall
            const float k = std::min(1.f, w.alt / 1.15f);
            const float top = py - alt - sz;
            c.line(px, top, px, top - std::min(alt * 0.6f, R * 0.4f), rgb(255, 200, 140), std::max(2.f, sz * 0.08f), int(150.f * k));
            gunits::glow(c, px, py - alt, sz * 0.3f, rgb(255, 150, 60), int(220.f * k), style);
        }
        bool ownMech = false;
        if (gunits::hasMech(lk) && grid.f > 0 && alt <= sz) {  // Retrowave (0.50.0): pixel art
            gunits::Pose r = p;
            const uint64_t key = mixKey(poseKey(3, lk, r, true), wreck ? 11u : 12u);
            ownMech = pxDraw(c, key, r, -1.2f * sz, -1.7f * sz - r.lift, 2.4f * sz, 2.f * sz + r.lift, p.a, now,
                             [&](Canvas& o, const gunits::Pose& z) { gunits::mech(o, lk, z, wreck); });
        } else if (gunits::hasMech(lk) && painting() && alt <= sz) {  // Voidfall (0.51.1): painted
            gunits::Pose r = p;
            const uint64_t key = mixKey(poseKey(3, lk, r, true, 100, true, 8), wreck ? 11u : 12u);
            ownMech = paintDraw(c, key, r, -1.2f * sz, -1.7f * sz - r.lift, 2.4f * sz, 2.f * sz + r.lift, p.a, now,
                                [&](Canvas& o, const gunits::Pose& z) { gunits::mech(o, lk, z, wreck); });
        }
        if (ownMech || gunits::mech(c, lk, p, wreck)) {
            if (wreck && lk != gunits::Mortheus) {
                const int since = int(now - w.dieAt);
                if (since < 4000) {
                    const float fl = 0.5f + 0.5f * std::sin(float(now % 300) / 300.f * 2 * kPi + w.phase);
                    c.fillCircle(px + sz * 0.1f, py - sz * 0.45f, sz * 0.06f * fl + 1.f, rgb(255, 120, 40), 200 - since / 25);
                }
            }
        } else {
            const float sk = 1.f - std::min(0.6f, alt / std::max(1.f, R));
            const bool painted = painting();
            if (!painted) {
                c.fillCircle(px - sz * 0.12f, py, sz * 0.16f * sk, rgb(0, 0, 0), 80);  // a flat shadow under its feet (0.28.0)
                c.fillCircle(px + sz * 0.12f, py, sz * 0.16f * sk, rgb(0, 0, 0), 80);
            }
            uint32_t mc = painted ? pcol[w.side] : col[w.side];
            if (wreck) {  // the wreck: dark, embers glowing on it for a few seconds, then it stays (0.43.0)
                mc = darker(mc, 0.45f);
                const int since = int(now - w.dieAt);
                if (since < 4000) {
                    const float fl = 0.5f + 0.5f * std::sin(float(now % 300) / 300.f * 2 * kPi + w.phase);
                    c.fillCircle(px + sz * 0.1f, py - sz * 0.45f, sz * 0.06f * fl + 1.f, rgb(255, 120, 40), 200 - since / 25);
                }
            }
            if (painted) {  // Voidfall (0.51.1): the painted mech
                gunits::paintedMech(c, p, py + sz * 0.04f - bob - alt, sz, mc, w.side == 1, wreck);
            } else if (const pxship::Sprite* sp = pxship::fit(c, grid, UnitType::Mech, sz * 0.9f, sz, mc)) {  // Retrowave (0.49.0): pixel art
                pxship::Look k;
                k.faceLeft = w.side == 1;
                k.alpha = p.a;
                k.engines = false;
                pxship::drawOn(c, *sp, c.fX(px), c.fY(py + sz * 0.04f - bob - alt), grid, k, now);
            } else {
                icons::drawUnitArtOn(c, "unit/mech", px, py + sz * 0.04f - bob - alt, int(sz * 0.9f), int(sz), mc, w.side == 1, p.a);
            }
            gunits::Pose o = p;
            o.y = py + sz * 0.04f - bob - alt;
            bool overlaid = false;
            if (grid.f > 0 && (lk == gunits::Creuss || lk == gunits::Mahact || lk == gunits::Cabal)) {  // (0.50.0) on the grid too
                gunits::Pose r = o;
                const uint64_t key = mixKey(poseKey(4, lk, r, true), wreck ? 11u : 12u);
                overlaid = pxDraw(c, key, r, -0.6f * sz, -1.1f * sz, 1.2f * sz, 1.3f * sz, o.a, now,
                                  [&](Canvas& oc, const gunits::Pose& z) { gunits::mechOverlay(oc, lk, z, wreck); });
            }
            if (!overlaid) gunits::mechOverlay(c, lk, o, wreck);
        }
        phaseShimmer(c, p, sz * 0.4f, p.y - sz * 0.5f);
        if (w.damaged && w.state == 0) {
            const float fl = 0.6f + 0.4f * std::sin(float(now % 200) / 200.f * 2 * kPi);
            c.fillCircle(X(w.x + w.sx), Y(w.y + w.sy) - sz * 0.3f - alt, 2.5f * fl, rgb(255, 150, 50), 220);
        }
    }

    void drawTower(Canvas& c, const Game& g, const Tower& t, uint32_t now) {
        const uint32_t traw = t.owner == kNeutral ? theme::neutral : t.owner >= 0 ? pcolours::base(g.players[size_t(t.owner)].color) : 0u;
        const uint32_t tc = t.owner == kNeutral || t.owner >= 0 ? (painting() ? paintMuted(traw) : muted(traw)) : theme::text;
        const float sz = t.size * R;
        const float px = X(t.x), py = Y(t.y);
        if (t.side >= 0 && t.side < 2) {  // 0.44.0: the Titans' robot, Zelian rocks, Mortheus nebulae
            gunits::Pose p = pose(t.side, px, py, sz, now, t.x * 3.f, 0);
            p.col = tc;
            p.a = 255;
            const gunits::Look sl = look[t.side];
            const bool pds = t.t == UnitType::PDS;
            if (grid.f > 0 && gunits::hasStructure(sl, pds)) {  // Retrowave (0.50.0): pixel art
                gunits::Pose r = p;
                const uint64_t key = mixKey(mixKey(poseKey(6, sl, r, true), pds ? 21u : 22u), t.ruined ? 31u : 32u);
                if (pxDraw(c, key, r, -1.2f * sz, -2.1f * sz, 2.4f * sz, 2.4f * sz, 255, now,
                           [&](Canvas& o, const gunits::Pose& z) { gunits::structure(o, sl, pds, z, t.ruined); }))
                    return;
            } else if (painting() && gunits::hasStructure(sl, pds)) {  // Voidfall (0.51.1): painted
                gunits::Pose r = p;
                const uint64_t key = mixKey(mixKey(poseKey(6, sl, r, true, 100, true, 8), pds ? 21u : 22u), t.ruined ? 31u : 32u);
                if (paintDraw(c, key, r, -1.2f * sz, -2.1f * sz, 2.4f * sz, 2.4f * sz, 255, now,
                              [&](Canvas& o, const gunits::Pose& z) { gunits::structure(o, sl, pds, z, t.ruined); }))
                    return;
            }
            if (gunits::structure(c, look[t.side], t.t == UnitType::PDS, p, t.ruined)) return;
        }
        // standing on the ground (0.28.0, owner: they looked like they floated): a flat shadow at the base
        uint32_t cc = t.ruined ? darker(tc, 0.65f) : tc;
        if (painting()) {  // Voidfall (0.51.1): a soft violet pool under it, the picture painted
            battlefx::ellipse(c, px + sz * 0.04f, py + sz * 0.02f, sz * 0.5f, sz * 0.11f, rgb(14, 4, 28), 60);
            battlefx::ellipse(c, px + sz * 0.04f, py + sz * 0.02f, sz * 0.36f, sz * 0.075f, rgb(14, 4, 28), 90);
            icons::drawUnitArtOnPainted(c, t.t == UnitType::PDS ? "unit/pds" : "unit/spacedock", px, py + sz * 0.04f, int(sz), int(sz), cc, false, 255);
            if (!t.ruined && (now / 700 + uint32_t(t.x * 10.f + 5.f)) % 2) {  // a beacon
                gunits::glow(c, px + sz * 0.2f, py - sz * 0.75f, 4.f, rgb(255, 80, 60), 200, style);
                c.fillCircle(px + sz * 0.2f, py - sz * 0.75f, 1.6f, rgb(255, 200, 180), 240);
            }
            return;
        }
        c.fillCircle(px - sz * 0.18f, py, sz * 0.2f, rgb(0, 0, 0), 70);
        c.fillCircle(px + sz * 0.18f, py, sz * 0.2f, rgb(0, 0, 0), 70);
        c.fillRect(int(px - sz * 0.38f), int(py - sz * 0.06f), int(sz * 0.76f), std::max(2, int(sz * 0.12f)), rgb(0, 0, 0), 60);
        if (const pxship::Sprite* sp = pxship::fit(c, grid, t.t == UnitType::PDS ? UnitType::PDS : UnitType::SpaceDock, sz, sz, cc)) {  // (0.49.0)
            pxship::Look k;
            k.engines = false;
            pxship::drawOn(c, *sp, c.fX(px), c.fY(py + sz * 0.04f), grid, k, now);
        } else {
            icons::drawUnitArtOn(c, t.t == UnitType::PDS ? "unit/pds" : "unit/spacedock", px, py + sz * 0.04f, int(sz), int(sz), cc, false, 255);
        }
        if (!t.ruined && (now / 700 + uint32_t(t.x * 10.f + 5.f)) % 2)  // a beacon
            c.fillCircle(px + sz * 0.2f, py - sz * 0.75f, 2.f, rgb(255, 80, 60), 230);
    }

    void drawShot(Canvas& c, const Shot& s, uint32_t now) {
        if (int32_t(now - s.born) < 0) return;
        const float k = float(int32_t(now - s.born)) / float(std::max<uint32_t>(1, s.arrive - s.born));
        float sx, sy;
        shotAt(s, now, sx, sy);
        const float x = X(sx), y = Y(sy);
        const float dx = s.x1 - s.x0, dy = s.y1 - s.y0, d = std::sqrt(dx * dx + dy * dy) + 1e-4f;
        const float ux = dx / d, uy = dy / d;
        const float sz = std::max(1.f, s.size * R);
        switch (s.kind) {
            case Ball:
                gunits::glow(c, x, y, sz * 1.8f, s.col, 200, style);
                c.fillCircle(x, y, sz, s.col, 220);
                c.fillCircle(x, y, sz * 0.5f, rgb(235, 250, 255), 255);
                break;
            case Lob: {
                const float gx = X(s.x0 + (s.x1 - s.x0) * k), gy = Y(s.y0 + (s.y1 - s.y0) * k);
                c.fillCircle(gx, gy, sz * 0.8f, rgb(0, 0, 0), 60);  // its shadow below
                if (s.style == 0) {  // a seed bomb
                    gunits::glow(c, x, y, sz * 1.6f, rgb(255, 130, 40), 140, style);
                    battlefx::ellipse(c, x, y, sz * 0.8f, sz, s.col, 255);
                    c.line(x - sz * 0.5f, y, x + sz * 0.5f, y, darker(s.col, 0.4f), std::max(1.f, sz * 0.2f), 255);
                } else if (s.style == 3) {  // a slime bullet
                    c.fillCircle(x, y, sz, s.col, 230);
                    c.fillCircle(x - sz * 0.3f, y - sz * 0.3f, sz * 0.35f, lighter(s.col, 0.5f), 220);
                } else {  // a rock (the big ones glow in their cracks)
                    c.fillCircle(x, y, sz, s.col, 255);
                    c.fillCircle(x - sz * 0.3f, y - sz * 0.3f, sz * 0.45f, lighter(s.col, 0.2f), 220);
                    if (s.style == 2) c.line(x - sz * 0.5f, y + sz * 0.1f, x + sz * 0.4f, y - sz * 0.2f, rgb(255, 80, 40), std::max(1.f, sz * 0.15f), 230);
                }
                break;
            }
            case Shard: {  // a crystal shard, point first
                const float L = sz * 2.4f, W = sz * 0.6f;
                float px[4] = {x + ux * L, x - uy * W, x - ux * L * 0.6f, x + uy * W}, py[4] = {y + uy * L, y + ux * W, y - uy * L * 0.6f, y - ux * W};
                gunits::glow(c, x, y, sz * 1.4f, s.col, 120, style);
                c.fillPolygon(px, py, 4, s.col, 235);
                c.line(px[0], py[0], px[2], py[2], lighter(s.col, 0.6f), 1.f, 255);
                break;
            }
            case Star: {  // a slow star, twinkling as it goes
                const float tw = 0.7f + 0.3f * std::sin(float(now % 600) / 600.f * 2 * kPi);
                gunits::glow(c, x, y, sz * 2.6f * tw, s.style ? rgb(255, 170, 90) : rgb(255, 236, 180), 220, style);
                const float a = float(now % 4000) / 4000.f * 2 * kPi;
                for (int i = 0; i < 2; ++i) {
                    const float ca = std::cos(a + float(i) * kPi / 2.f) * sz * 2.4f, sa = std::sin(a + float(i) * kPi / 2.f) * sz * 2.4f;
                    c.line(x - ca, y - sa, x + ca, y + sa, rgb(255, 250, 230), 1.f, 200);
                }
                c.fillCircle(x, y, sz * 0.6f, rgb(255, 255, 250), 255);
                break;
            }
            case Rocket:
                c.line(x - ux * sz * 1.6f, y - uy * sz * 1.6f, x, y, rgb(200, 200, 210), std::max(1.5f, sz * 0.6f), 255);
                gunits::glow(c, x - ux * sz * 1.9f, y - uy * sz * 1.9f, sz * 0.9f, rgb(255, 160, 60), 230, style);
                break;
            case Spore: {  // a teardrop of green spores
                const float r = R * 0.02f;
                c.fillCircle(x - ux * r * 1.3f, y - uy * r * 1.3f, r * 0.6f, s.col, 90);
                c.fillCircle(x, y, r, s.col, 150);
                c.fillCircle(x + ux * r * 0.5f, y + uy * r * 0.5f, r * 0.55f, lighter(s.col, 0.35f), 170);
                break;
            }
            case Beam: {  // a long beam from muzzle to target; the hyper beam wide and banded
                const float fx = X(s.x0), fy = Y(s.y0), tx = X(s.x1), ty = Y(s.y1);
                const float grow = std::min(1.f, k * 6.f), fade = k > 0.85f ? (1.f - k) / 0.15f : 1.f;
                const float ex = fx + (tx - fx) * grow, ey = fy + (ty - fy) * grow;
                const float fl = 0.85f + 0.15f * std::sin(float(now % 120) / 120.f * 2 * kPi);
                float xs[2] = {fx, ex}, ys[2] = {fy, ey};
                const float w = sz * fl * fade;
                battlefx::river(c, xs, ys, 2, w * 2.6f, s.col, int(70.f * fade));
                battlefx::river(c, xs, ys, 2, w * 1.3f, s.style ? rgb(255, 150, 60) : s.col, int(200.f * fade));
                battlefx::river(c, xs, ys, 2, std::max(1.f, w * 0.5f), rgb(255, 250, 240), int(255.f * fade));
                if (s.style == 1)  // rings travelling down the hyper beam
                    for (int i = 0; i < 4; ++i) {
                        const float u = std::fmod(float(now % 100000u) / 300.f + float(i) * 0.25f, 1.f) * grow;
                        c.ring(fx + (tx - fx) * u, fy + (ty - fy) * u, w * 1.2f, std::max(1.f, w * 0.15f), rgb(255, 240, 200), int(180.f * fade));
                    }
                gunits::glow(c, ex, ey, w * 2.f, s.col, int(220.f * fade), style);
                break;
            }
            case Bolt: {  // lightning, flickering between new paths
                const float fx = X(s.x0), fy = Y(s.y0), tx = X(s.x1), ty = Y(s.y1);
                const float ddx = tx - fx, ddy = ty - fy, len = std::sqrt(ddx * ddx + ddy * ddy) + 1e-3f, nx = -ddy / len, ny = ddx / len;
                uint32_t sd = s.seed + (now - s.born) / 50u;
                float px0 = fx, py0 = fy;
                const int a = int(255.f * (1.f - k * 0.6f));
                for (int i = 1; i <= 7; ++i) {
                    sd = sd * 1664525u + 1013904223u;
                    const float off = i == 7 ? 0.f : (float((sd >> 9) & 1023u) / 1023.f - 0.5f) * len * 0.22f;
                    const float qx = fx + ddx * float(i) / 7.f + nx * off, qy = fy + ddy * float(i) / 7.f + ny * off;
                    c.line(px0, py0, qx, qy, s.col, 3.f, a / 3);
                    c.line(px0, py0, qx, qy, lighter(s.col, 0.6f), 1.2f, a);
                    px0 = qx;
                    py0 = qy;
                }
                gunits::glow(c, tx, ty, R * 0.025f, s.col, a, style);
                break;
            }
            case Stream: break;  // (drawn by its particles)
            case Bullet: c.line(x - ux * 0.025f * R, y - uy * 0.025f * R, x, y, s.col, 1.f, 220); break;
            case Laser:
                c.line(x - ux * 0.035f * R, y - uy * 0.035f * R, x, y, s.col, 2.5f, 90);
                c.line(x - ux * 0.035f * R, y - uy * 0.035f * R, x, y, lighter(s.col, 0.5f), 1.f, 255);
                break;
            case Gatling:
                c.line(x - ux * 0.04f * R, y - uy * 0.04f * R, x, y, s.col, 3.f, 100);
                c.line(x - ux * 0.04f * R, y - uy * 0.04f * R, x, y, rgb(240, 250, 255), 1.2f, 255);
                break;
            case Plasma:
                c.fillCircle(x, y, 4.f, s.col, 100);
                c.fillCircle(x, y, 2.f, rgb(240, 255, 255), 255);
                break;
            default: break;
        }
    }

    void drawBoom(Canvas& c, const Boom& b, uint32_t now) {
        if (int32_t(now - b.born) < 0) return;
        const float k = float(int32_t(now - b.born)) / float(std::max<uint32_t>(1, b.life));
        const float x = X(b.x), y = Y(b.y), r = b.r * R;
        if (grid.f > 0 && (b.kind == Fire || b.kind == Big || b.kind == Flash || b.kind == Muzzle)) {  // Retrowave (0.49.0): pixel art
            const int gx = pxship::gridX(grid, c.fX(x)), gy = pxship::gridY(grid, c.fY(y));
            const float gr = std::max(1.f, c.fx(r) / float(grid.f));
            if (b.kind == Flash) pxship::flash(c, grid, gx, gy, gr, k);
            else if (b.kind == Muzzle) pxship::muzzle(c, grid, gx, gy, gr, k);
            else pxship::fireball(c, grid, gx, gy, gr * (0.45f + 0.9f * std::sqrt(k)), k, b.born * 2654435761u, now);
            return;
        }
        switch (b.kind) {
            case Fire:
            case Big: {
                const float rr = r * (0.4f + 0.9f * std::sqrt(k));
                c.fillCircle(x, y, rr * 1.25f, rgb(200, 70, 30), int(90 * (1 - k)));
                c.fillCircle(x, y, rr, rgb(255, 120, 40), int(220 * (1 - k)));
                c.fillCircle(x, y, rr * 0.6f, rgb(255, 210, 110), int(240 * (1 - k)));
                c.fillCircle(x, y, rr * 0.3f, rgb(255, 255, 230), int(255 * (1 - k)));
                break;
            }
            case Flash: c.fillCircle(x, y, r * (0.6f + 0.8f * k), rgb(255, 255, 255), int(230 * (1 - k))); break;
            case Muzzle:
                c.fillCircle(x, y, r * (1.f + k), rgb(255, 220, 130), int(220 * (1 - k)));
                c.fillCircle(x, y, r * 0.5f, rgb(255, 255, 240), int(255 * (1 - k)));
                break;
            case Nova: {  // 0.44.0 Mortheus: a star going supernova - a white flash, coloured shells flying out
                const float rr = r * (0.3f + 1.4f * std::sqrt(k));
                c.fillCircle(x, y, rr * 1.3f, rgb(255, 120, 200), int(70 * (1 - k)));
                c.ring(x, y, rr, std::max(1.f, r * 0.25f * (1 - k)), rgb(120, 170, 255), int(220 * (1 - k)));
                c.ring(x, y, rr * 0.7f, std::max(1.f, r * 0.2f * (1 - k)), rgb(255, 170, 90), int(230 * (1 - k)));
                c.fillCircle(x, y, r * 0.6f * (1.f - k * 0.7f), rgb(255, 255, 245), int(255 * (1 - k * k)));
                break;
            }
            case Hole: {  // Mortheus mech: a black hole that eats the light round it, then is gone
                const float in = std::min(1.f, k * 7.f), out = k > 0.8f ? (1.f - k) / 0.2f : 1.f, rr = r * in * out;
                const float a = float(now % 100000u) / 300.f;
                for (int i = 0; i < 3; ++i) {  // the accretion ring
                    const float ri = rr * (1.5f + 0.3f * float(i));
                    c.ring(x, y, ri, std::max(1.f, rr * 0.18f), i == 1 ? rgb(255, 210, 120) : rgb(255, 120, 60), int(150.f * out));
                }
                for (int i = 0; i < 6; ++i) {  // light spiralling in
                    const float u = std::fmod(a * 0.3f + float(i) / 6.f, 1.f), ang = a + float(i) * 1.05f + u * 3.f;
                    c.fillCircle(x + std::cos(ang) * rr * 2.6f * (1.f - u), y + std::sin(ang) * rr * 2.f * (1.f - u), std::max(1.f, rr * 0.12f), rgb(255, 230, 190),
                                 int(220.f * out * u));
                }
                c.fillCircle(x, y, rr, rgb(0, 0, 0), 255);
                break;
            }
            case Splat: {  // slime hitting the ground
                const float rr = r * (0.5f + std::sqrt(k));
                c.fillCircle(x, y, rr, b.col, int(200 * (1 - k)));
                for (int i = 0; i < 5; ++i) {
                    const float ang = float(i) * 1.25f + b.x * 7.f;
                    c.fillCircle(x + std::cos(ang) * rr * 1.3f, y + std::sin(ang) * rr * 0.9f, rr * 0.3f, lighter(b.col, 0.2f), int(220 * (1 - k)));
                }
                break;
            }
            default: break;
        }
    }

    void drawPart(Canvas& c, const Part& p, uint32_t now) {
        if (int32_t(now - p.born) < 0) return;
        const float k = float(int32_t(now - p.born)) / float(std::max<uint32_t>(1, p.life));
        const int a = int(float(p.a0) * (1.f - k));
        const float x = X(p.x), y = Y(p.y);
        if (grid.f > 0 && (p.kind == Dust || p.kind == Smoke || p.kind == Spark || p.kind == Chunk)) {  // Retrowave (0.49.0): pixel art
            const int gx = pxship::gridX(grid, c.fX(x)), gy = pxship::gridY(grid, c.fY(y));
            if (p.kind == Spark) pxship::spark(c, grid, gx, gy, p.vx, p.vy, p.col, k);
            else if (p.kind == Chunk) pxship::dot(c, grid, gx, gy, p.col, a);
            else
                pxship::puff(c, grid, gx, gy, c.fx(p.size * R) / float(grid.f), float(a) / float(std::max(1, p.a0)), uint32_t(p.born) * 2654435761u + uint32_t(int(p.x * 1000.f)),
                             p.col, std::min(255, 120 + a / 2));
            return;
        }
        switch (p.kind) {
            case Dust:
            case Smoke: c.fillCircle(x, y, std::max(1.f, p.size * R), p.col, a); break;
            case Spark: c.line(x, y, x - p.vx * R * 0.03f, y - p.vy * R * 0.03f, p.col, 1.f, a); break;
            case Lava:
                c.fillCircle(x, y, std::max(1.f, p.size * R), p.col, a);
                c.fillCircle(x, y, std::max(1.f, p.size * R * 2.f), rgb(255, 120, 30), a / 5);
                break;
            case Chunk: c.fillRect(int(x), int(y), 3, 3, p.col, a); break;
            case Flame: {  // white-yellow at the nozzle, then the fire's colour, then smoke
                const uint32_t fc = k < 0.25f ? lighter(p.col, 0.6f) : k < 0.65f ? p.col : mix(darker(p.col, 0.5f), rgb(60, 56, 54), int(255.f * (k - 0.65f) / 0.35f));
                c.fillCircle(x, y, std::max(1.f, p.size * R), fc, k < 0.65f ? a : a / 2);
                break;
            }
            case Goo:
                c.fillCircle(x, y, std::max(1.f, p.size * R), p.col, a);
                c.fillCircle(x - p.size * R * 0.3f, y - p.size * R * 0.3f, std::max(0.6f, p.size * R * 0.35f), lighter(p.col, 0.5f), a);
                break;
            case Gas:  // superheated: white-hot at the mouth, orange, then a thin grey haze
                c.fillCircle(x, y, std::max(1.f, p.size * R), k < 0.2f ? rgb(255, 244, 210) : k < 0.55f ? p.col : mix(p.col, rgb(190, 184, 178), int(255.f * (k - 0.55f) / 0.45f)),
                             k < 0.55f ? a : a * 2 / 3);
                break;
            case Mote: c.fillCircle(x, y, std::max(1.f, p.size * R), p.col, a); break;
            default: break;
        }
    }

    // 0.43.2: what moves on the Fracture's and Thunder's Edge's ground, under the fighting
    void landAnim(Canvas& c, uint32_t now) {
        const float t = float(now - t0) / 1000.f;
        if (land == Land::Fracture) {
            battlefx::drawCrackPulse(c, fiss, cx, cy, R, now, 3.2f, 1, fracLand == 0 ? 170 : 230);
            for (const GShard& sh : shards) {  // pieces of broken sky hanging over the field, bobbing
                const float bob = std::sin(t * 1.3f + sh.ph) * 0.012f;
                const float x = X(sh.x), gy = Y(sh.y), y = Y(sh.y - 0.07f - bob), sz = sh.s * R;
                battlefx::ellipse(c, x + sz * 0.3f, gy, sz * 0.9f, sz * 0.35f, rgb(0, 0, 0), 70);
                const float gl = 0.5f + 0.5f * std::sin(t * 2.f + sh.ph * 2.f);
                c.fillCircle(x, y, sz * 1.3f, rgb(255, 50, 60), int(12 + 18 * gl));
                float px[4], py[4];
                for (int k = 0; k < 4; ++k) {
                    const float a = sh.rot + float(k) * kPi / 2.f + (k % 2 ? 0.35f : 0.f), rr = sz * (k % 2 ? 0.55f : 1.f);
                    px[k] = x + std::cos(a) * rr;
                    py[k] = y + std::sin(a) * rr * 0.8f;
                }
                c.fillPolygon(px, py, 4, rgb(190, 24, 44), 215);
                float fx[3] = {px[0], px[1], x}, fy[3] = {py[0], py[1], y};
                c.fillPolygon(fx, fy, 3, rgb(255, 120, 120), 150);
                for (int k = 0; k < 4; ++k) c.line(px[k], py[k], px[(k + 1) % 4], py[(k + 1) % 4], rgb(255, 210, 200), 1.f, 170);
            }
        } else if (land == Land::Storm) {
            for (const Spire& sp : spires) {  // the spires' tips glow
                const float x = X(sp.x), y = Y(sp.y) - sp.h * R;
                const float gl = 0.5f + 0.5f * std::sin(t * 3.f + sp.x * 9.f);
                c.fillCircle(x, y, R * 0.03f, rgb(120, 170, 255), int(40 + 60 * gl));
                c.fillCircle(x, y, std::max(1.f, R * 0.01f), rgb(230, 240, 255), 230);
            }
        }
    }

    void weather(Canvas& c, uint32_t now) {
        const float t = float(now - t0) / 1000.f;
        if (land == Land::Fracture) {  // 0.43.2
            if (fracLand == 0) {  // Cocytus: a cold haze, snow drifting
                c.fillRect(area.x, area.y, area.w, area.h, rgb(220, 236, 250), 22);
                Rng r;
                r.s = 53u;
                for (int i = 0; i < 60; ++i) {
                    const float sp = r.r(0.15f, 0.4f), x0 = r.r(-1.2f, 1.2f), y0 = r.r(-1.f, 1.f);
                    const float x = std::fmod(x0 + t * sp * 0.6f + 12.f, 2.4f) - 1.2f, y = std::fmod(y0 + t * sp + 10.f, 2.f) - 1.f;
                    c.fillCircle(X(x), Y(y), r.r(0.7f, 1.5f), rgb(255, 255, 255), 200);
                }
            } else if (fracLand == 1) {  // Styx: rain and sheets of lightning
                Rng r;
                r.s = 61u;
                for (int i = 0; i < 100; ++i) {
                    const float sp = r.r(1.4f, 2.2f), x0 = r.r(-1.2f, 1.2f), y0 = r.r(-1.f, 1.f);
                    const float y = std::fmod(y0 + t * sp + 10.f, 2.f) - 1.f, x = std::fmod(x0 + t * sp * 0.3f + 12.f, 2.4f) - 1.2f;
                    c.line(X(x), Y(y), X(x - 0.015f), Y(y - 0.05f), rgb(170, 190, 225), 1.f, 110);
                }
                const uint32_t ph = (now - t0) % 3100u;
                if (ph < 220) c.fillRect(area.x, area.y, area.w, area.h, rgb(190, 210, 255), 50 - int(ph) * 50 / 220);
            } else if (fracLand == 2) {  // Lethe: banks of mist drifting over
                c.fillRect(area.x, area.y, area.w, area.h, rgb(220, 220, 232), 16);
                Rng r;
                r.s = 71u;
                for (int i = 0; i < 7; ++i) {
                    const float sp = r.r(0.03f, 0.07f), x0 = r.r(0.f, 2.8f), y = r.r(-0.8f, 0.8f);
                    const float x = std::fmod(x0 + t * sp, 2.8f) - 1.4f;
                    for (int k = 0; k < 4; ++k)
                        c.fillCircle(X(x + 0.13f * float(k)), Y(y + 0.03f * std::sin(float(k) * 1.7f)), R * (0.2f - 0.025f * float(k)), rgb(234, 234, 244), 24);
                }
            } else {  // Phlegethon: the heat of the river, embers rising
                const float p = 0.5f + 0.5f * std::sin(t * 2.1f);
                c.fillRect(area.x, area.y, area.w, area.h, rgb(255, 60, 30), int(8 + 10 * p));
                Rng r;
                r.s = 83u;
                for (int i = 0; i < 40; ++i) {
                    const float sp = r.r(0.08f, 0.2f), x0 = r.r(-1.f, 1.f), y0 = r.r(-1.f, 1.f);
                    const float y = 1.f - std::fmod(1.f - y0 + t * sp + 10.f, 2.f), x = x0 + 0.03f * std::sin(t * 1.5f + float(i));
                    c.fillCircle(X(x), Y(y), r.r(0.8f, 1.6f), i % 3 ? rgb(255, 140, 50) : rgb(255, 220, 120), 200);
                }
            }
            c.fillRect(area.x, area.y, area.w, area.h, rgb(200, 20, 40), 10);  // and the Fracture's red over all of it
            return;
        }
        if (land == Land::Storm) {  // 0.43.2: Thunder's Edge - cloud shadows, rain, lightning striking the spires
            Rng r;
            r.s = 97u;
            for (int i = 0; i < 5; ++i) {
                const float x0 = r.r(0.f, 3.2f), y = r.r(-0.7f, 0.7f), sp = r.r(0.05f, 0.09f);
                const float x = std::fmod(x0 + t * sp, 3.2f) - 1.6f;
                battlefx::ellipse(c, X(x), Y(y), R * 0.45f, R * 0.28f, rgb(8, 12, 22), 36);
            }
            for (int i = 0; i < 150; ++i) {
                const float sp = r.r(1.6f, 2.4f), x0 = r.r(-1.2f, 1.2f), y0 = r.r(-1.f, 1.f);
                const float y = std::fmod(y0 + t * sp + 10.f, 2.f) - 1.f, x = std::fmod(x0 + t * sp * 0.35f + 12.f, 2.4f) - 1.2f;
                c.line(X(x), Y(y), X(x - 0.02f), Y(y - 0.06f), rgb(170, 190, 222), 1.f, 120);
            }
            c.fillRect(area.x, area.y, area.w, area.h, rgb(20, 30, 60), 30);
            const uint32_t per = 2600u, ph = (now - t0) % per, slot = (now - t0) / per;
            if (ph < 360 && !spires.empty()) {
                const Spire& sp = spires[slot % spires.size()];
                const float tx = X(sp.x), ty = Y(sp.y) - sp.h * R;
                const int a = 255 - int(ph) * 255 / 360;
                const float off = (float((slot * 2654435761u) >> 20 & 1023u) / 1023.f - 0.5f) * 0.6f;
                battlefx::bolt(c, tx + R * off, cy - R * 0.9f, tx, ty, slot * 7u + 1u, R * 0.06f, a, std::max(1.f, R * 0.006f), true);
                c.fillRect(area.x, area.y, area.w, area.h, rgb(200, 220, 255), a / 5);
                c.ring(tx, ty, R * (0.03f + 0.12f * float(ph) / 360.f), 2.f, rgb(200, 225, 255), a);
            }
            return;
        }
        if (land == Land::Desert) {  // a sandstorm
            c.fillRect(area.x, area.y, area.w, area.h, rgb(210, 150, 90), 34);
            Rng r;
            r.s = 77u;
            for (int i = 0; i < 70; ++i) {
                const float sp = r.r(0.9f, 1.8f), y = r.r(-1.f, 1.f), x0 = r.r(-1.2f, 1.2f), len = r.r(0.03f, 0.1f);
                float x = std::fmod(x0 + t * sp + 12.f, 2.4f) - 1.2f;
                c.line(X(x), Y(y + std::sin(t * 2.f + x0 * 5.f) * 0.02f), X(x - len), Y(y), rgb(240, 200, 150), 1.f, 150);
            }
        } else if (land == Land::Tundra) {  // a blizzard
            c.fillRect(area.x, area.y, area.w, area.h, rgb(230, 240, 250), 40);
            Rng r;
            r.s = 91u;
            for (int i = 0; i < 110; ++i) {
                const float sp = r.r(0.4f, 0.9f), x0 = r.r(-1.2f, 1.2f), y0 = r.r(-1.f, 1.f);
                const float x = std::fmod(x0 + t * sp * 0.9f + 12.f, 2.4f) - 1.2f;
                const float y = std::fmod(y0 + t * sp + 10.f, 2.f) - 1.f;
                c.fillCircle(X(x), Y(y), r.r(0.8f, 1.8f), rgb(255, 255, 255), 220);
            }
        } else if (land == Land::Volcanic) {  // the heat glow of the lava
            const float p = 0.5f + 0.5f * std::sin(t * 2.3f);
            c.fillRect(area.x, area.y, area.w, area.h, rgb(255, 80, 20), int(8 + 10 * p));
        } else if (land == Land::Refinery) {
            c.fillRect(area.x, area.y, area.w, area.h, rgb(120, 200, 60), 12);
        }
    }

    void drawDice(Canvas& c, uint32_t now) {
        const float size = std::max(20.f, std::min(30.f, R * 0.13f));
        for (int side = 0; side < 2; ++side) {
            for (const RollShow& r : rolls) {
                if (r.side != side) continue;
                const int a = r.tm.alpha(now);
                if (a <= 0) continue;
                const int rowsN = (int(r.dice.size()) + 5) / 6;
                const float stepY = size * 1.08f;
                // the invaders' dice along the top, the defenders' along the bottom
                const float bottom = side == 0 ? cy - 0.8f * R + float(rowsN) * stepY : cy + 0.83f * R;
                const int h = dice::drawRoll(c, cx, bottom, size, 6, r.dice, r.need, fac[side], r.tm, now);
                if (r.tm.settled(now, int(r.dice.size()))) {
                    const std::string t = std::to_string(r.hits) + (r.hits == 1 ? " HIT" : " HITS");
                    const int tw = textWidth(FONT_SMB, t) + 10;
                    const int ty = side == 0 ? int(bottom) + 2 : int(bottom) - h - 20;
                    c.fillRoundRect(int(cx) - tw / 2, ty, tw, 17, 5, rgb(6, 8, 14), a * 200 / 255);
                    drawTextAligned(c, FONT_SMB, int(cx) - tw / 2, ty + 1, tw, Align::Center, t, r.hits ? rgb(120, 255, 140) : rgb(170, 170, 180),
                                    a);
                }
            }
        }
    }
};

// ============================================================ the public side
GroundBattle::GroundBattle() : m_(std::make_unique<Impl>()) {}
GroundBattle::~GroundBattle() = default;

void GroundBattle::clear() {
    active_ = false;
    planet_ = -1;
    player_[0] = player_[1] = -1;
    m_ = std::make_unique<Impl>();
}

void GroundBattle::start(const Game& g, int sys, int planet, int attacker, int defender, const Force& a, const Force& b, uint32_t now,
                         float speed) {
    clear();
    if (planet < 0 || planet >= int(g.planets.size()) || attacker < 0 || defender < 0) return;
    active_ = true;
    planet_ = planet;
    player_[0] = attacker;
    player_[1] = defender;
    Impl& m = *m_;
    m.pl[0] = attacker;
    m.pl[1] = defender;
    for (int i = 0; i < 2; ++i) {
        if (m.pl[i] == kNeutral || m.pl[i] < 0 || m.pl[i] >= g.numPlayers) {  // Thunder's Edge: neutral units
            m.raw[i] = theme::neutral;
            m.col[i] = muted(m.raw[i]);
            m.pcol[i] = paintMuted(m.raw[i]);
            m.fac[i] = -1;
            continue;
        }
        const Player& p = g.players[size_t(m.pl[i])];
        m.raw[i] = pcolours::base(p.color);
        m.pci[i] = pcolours::of(g, m.pl[i]);
        m.col[i] = muted(m.raw[i]);  // muted on the ground (owner)
        m.pcol[i] = paintMuted(m.raw[i]);
        m.fac[i] = p.faction;
        // 0.44.0: the faction's own ground units (Options > Ground combat: Default = the game's own for everyone)
        m.look[i] = gunits::factionUnits() && p.faction >= 0 ? gunits::lookOf(kFactions[size_t(p.faction)].key) : gunits::Default;
    }
    m.speed = std::max(0.3f, speed);
    m.t0 = now;
    m.last = 0;
    m.rng.s = now * 2246822519u + uint32_t(planet) * 131u + 7u;
    if (!m.rng.s) m.rng.s = 1;
    m.setup(g, sys, planet, a, b, now);
    m.arborecPatches(now);
}

void GroundBattle::roll(int player, const std::vector<int8_t>& d, const std::vector<int8_t>& need, int hits, uint32_t now, uint32_t shown) {
    if (!active_) return;
    Impl& m = *m_;
    const int side = m.sideOf(player);
    if (side < 0) return;
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

static uint32_t fadeGroundDice(GroundBattle::Impl& m, uint32_t now) {
    uint32_t wait = 0;
    for (auto& r : m.rolls)
        if (r.tm.alpha(now) > 0) {
            if (!r.tm.fadeAt) r.tm.fadeAt = now;
            wait = std::max<uint32_t>(wait, uint32_t(std::max(0, int(r.tm.fade) - int(now - r.tm.fadeAt))));
        }
    return wait;
}

void GroundBattle::damaged(int player, UnitType t, int count, uint32_t now) {
    if (!active_) return;
    Impl& m = *m_;
    const int side = m.sideOf(player);
    if (side < 0) return;
    Impl::Pending p;
    p.at = now + fadeGroundDice(m, now);
    p.kind = 0;
    p.side = side;
    p.t = t;
    p.count = count;
    m.pending.push_back(p);
}

void GroundBattle::destroyed(int player, UnitType t, int count, uint32_t now) {
    if (!active_) return;
    Impl& m = *m_;
    const int side = m.sideOf(player);
    if (side < 0) return;
    Impl::Pending p;
    p.at = now + fadeGroundDice(m, now);
    p.kind = 1;
    p.side = side;
    p.t = t;
    p.count = count;
    m.pending.push_back(p);
}

void GroundBattle::end(int winner, const std::string& text, uint32_t now) {
    if (!active_) return;
    Impl& m = *m_;
    fadeGroundDice(m, now);
    m.ended = true;
    m.winner = winner;
    m.result = text;
}

void GroundBattle::draw(Canvas& c, const Game& g, Rect area, uint32_t now, const std::string& confirm) {
    if (!active_) return;
    Impl& m = *m_;
    m.area = area;
    m.R = std::min(float(area.w) / 2.f, float(area.h) / kSqrt3) - 4.f;
    m.cx = float(area.x) + float(area.w) / 2.f;
    m.cy = float(area.y) + float(area.h) / 2.f;
    m.style = retro::tileset();
    if (!m.bg || m.bgArea.w != area.w || m.bgArea.h != area.h || m.bg->scale() != c.scale()) m.renderLand(g, c.scale());
    m.update(now);
    m.growPatches();  // 0.44.0
    const Rect keep = c.clip();
    c.setClip(area);
    // (0.27.0: in real pixels)
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
    const uint32_t* bp = m.bg->pixels();
    const int bw = m.bg->pw(), bh = m.bg->ph();
    for (int y = ay0; y < std::min(ay1, pa.y + bh); ++y) {
        const int bx0 = ax0 - pa.x, bx1 = std::min(bw, ax1 - pa.x);
        if (bx1 > bx0) std::copy(bp + size_t(y - pa.y) * size_t(bw) + bx0, bp + size_t(y - pa.y) * size_t(bw) + bx1, px + size_t(y) * size_t(W) + ax0);
    }
    m.landAnim(c, now);  // 0.43.2
    m.drawCraters(c, now);  // 0.44.0
    // Retrowave (0.49.0): the grid of the chunky pass below - the unit pictures go on it as pixel art
    m.grid = pxship::Grid{};
    if (retro::on())
        retro::chunkyGrid(c, Rect{int(m.cx - m.R), int(m.cy - m.R * 0.87f), int(2.f * m.R) + 1, int(1.74f * m.R) + 1}, std::max(2, int(m.R / 70.f)),
                          m.grid.x, m.grid.y, m.grid.f);
    // everything on the ground, back to front
    struct Item {
        float y;
        int kind, i;
    };
    std::vector<Item> items;
    for (size_t i = 0; i < m.towers.size(); ++i) items.push_back({m.towers[i].y, 0, int(i)});
    for (size_t i = 0; i < m.squads.size(); ++i) {
        const Impl::Squad& q = m.squads[i];
        if (!m.spread) {
            items.push_back({q.y, 1, int(i)});  // finished squads too: their fallen stay (0.28.0)
            continue;
        }
        for (int mi = 0; mi < m.nMen; ++mi)  // Voidfall (0.51.1): the men are spread - each in his own place in depth
            if (q.men[mi].state != 2) items.push_back({q.y + q.men[mi].oy + q.men[mi].jy, 3, int(i) * 16 + mi});
    }
    for (size_t i = 0; i < m.walkers.size(); ++i)
        if (m.walkers[i].state != 2) items.push_back({m.walkers[i].y, 2, int(i)});
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.y < b.y; });
    for (const Item& it : items) {
        if (it.kind == 0) m.drawTower(c, g, m.towers[size_t(it.i)], now);
        else if (it.kind == 1) m.drawSquad(c, m.squads[size_t(it.i)], now);
        else if (it.kind == 3) {
            const Impl::Squad& q = m.squads[size_t(it.i / 16)];
            if (int32_t(now - q.enterAt) >= 0) m.drawMan(c, q, it.i % 16, now);  // (not while still in orbit)
        } else {
            const auto& w = m.walkers[size_t(it.i)];
            const float sz = w.size * m.R;
            pcolours::Finish fin(c, pcolours::around(m.X(w.x), m.Y(w.y) - sz * 0.5f, sz * 0.6f, sz * 0.7f), w.state == 0 && w.t == UnitType::Mech ? m.pci[w.side] : -1,
                                 uint32_t(it.i) * 37u + uint32_t(w.side));
            m.drawWalker(c, w, now);
        }
    }
    m.drawClouds(c, now);  // 0.44.0
    for (const Impl::Shot& s : m.shots) m.drawShot(c, s, now);
    for (const Impl::Part& p : m.parts) m.drawPart(c, p, now);
    for (const Impl::Boom& b : m.booms) m.drawBoom(c, b, now);
    for (const Impl::Strike& st : m.strikes) {  // the slug falling from orbit: a white-hot streak and its trail
        const int32_t t = int32_t(now - st.at);
        if (t < -350 || t > 900) continue;
        const float tx = m.X(st.x), ty = m.Y(st.y), top = m.cy - m.R * 0.9f;
        if (t < 0) {  // a targeting glint first
            c.ring(tx, ty, 4.f + 6.f * float(-t) / 350.f, 1.5f, rgb(160, 220, 255), 160);
            continue;
        }
        const float k = std::min(1.f, float(t) / 260.f);
        const float hx = tx + (1.f - k) * m.R * 0.25f, hy = top + (ty - top) * k;
        const int fade = t > 260 ? std::max(0, 255 - (t - 260) * 255 / 640) : 255;
        c.line(tx + m.R * 0.25f, top, hx, hy, rgb(60, 120, 255), 7.f, fade / 5);
        c.line(tx + m.R * 0.25f, top, hx, hy, rgb(170, 220, 255), 3.f, fade / 2);
        c.line(tx + (1.f - std::max(0.f, k - 0.3f)) * m.R * 0.25f, top + (ty - top) * std::max(0.f, k - 0.3f), hx, hy, rgb(245, 250, 255), 1.5f, fade);
        if (t < 260) c.fillCircle(hx, hy, 3.f, rgb(255, 255, 255), 255);
    }
    m.weather(c, now);
    m.specialWeather(c, now);  // 0.44.0
    if (retro::hd()) {  // Voidfall (0.33.1): the void's tendrils reach over the field; the painted look
        const Rect hexBox{int(m.cx - m.R), int(m.cy - m.R * 0.87f), int(2.f * m.R) + 1, int(1.74f * m.R) + 1};
        voidfall::tendrils(c, hexBox, now, uint32_t(m.def) + 57u, 3, 0.6f, 140);
        voidfall::grade(c, hexBox);
    }
    // Retrowave (0.26.0): the fight in chunky pixels - drawn small, enlarged
    if (retro::on()) retro::chunky(c, Rect{int(m.cx - m.R), int(m.cy - m.R * 0.87f), int(2.f * m.R) + 1, int(1.74f * m.R) + 1}, std::max(2, int(m.R / 70.f)));
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
    {
        float xs[7], ys[7];
        for (int i = 0; i <= 6; ++i) {
            const float a = kPi / 3.f * float(i);
            xs[i] = m.cx + m.R * std::cos(a);
            ys[i] = m.cy + m.R * std::sin(a);
        }
        for (int i = 0; i < 6; ++i) {
            c.line(xs[i], ys[i], xs[i + 1], ys[i + 1], rgb(0, 0, 0), 5.f, 160);
            c.line(xs[i], ys[i], xs[i + 1], ys[i + 1], rgb(150, 110, 70), 2.f, 230);
        }
    }
    m.drawDice(c, now);
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
