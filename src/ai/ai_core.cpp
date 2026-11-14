// The Strategy Core (0.18.0, the owner's Bot Logic Update + Strategy Guide).
//
// A computer player is two halves deciding together: its personality
// (archetype, evolution, traits, betrayals - ai_style.cpp) and this core, how
// its faction is best played (docs/owner/Strategy_Guide.txt, in our own words):
// round-1 strategy card priorities and the cards to leave early, technology
// paths (the faction's core path, then the one that suits the personality),
// the units the faction is built around, how it trades, and technology that
// answers what the neighbours field (X-89 against infantry stacks, Destroyer
// II against fighter swarms, ...). The core suggests; the personality mostly
// follows, but each round it may go its own way, as a person would (more
// often for a Chaotic, rarely on Competitive).
//
// 0.43.0: the table is generated from the owner's faction strategy guide
// (docs/owner/TI4-Faction-Strategy-Guides.pdf, all 64 factions with a Prophecy
// of Kings plan and a Thunder's Edge plan each) by tools/gen_guides.py:
// opening and late strategy cards, the technology path, the expedition slices
// it pays with, the Fracture and space stations, and who to hit early.
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "ai/ai.h"
#include "ai/ai_internal.h"
#include "engine/data.h"
#include "engine/personality.h"
#include "engine/query.h"

namespace ti {

namespace {

enum : uint8_t { kL = 1, kD = 2, kP = 4, kC = 8, kT = 16, kW = 32, kTe = 64, kI = 128 };
// strategy cards: 0 Leadership, 1 Diplomacy, 2 Politics, 3 Construction, 4 Trade, 5 Warfare, 6 Technology, 7 Imperial

struct FactionGuide {
    const char* key;
    int8_t r1[4];               // strategy cards for rounds 1-2, best first (-1: none)
    int8_t late[3];             // strategy cards from round 4 on (round 3 blends the two)
    uint8_t avoid;              // cards to leave in rounds 1-2
    const char* core[8];        // the faction's technology path (generic upgrades mean the faction's own)
    const char* aggressive[5];  // the military path
    const char* control[5];     // the control / economic / political path
    const char* te[4];          // extra technology in a Thunder's Edge game
    // units it is built around (production preference, 1 = as usual)
    float dread, cruiser, destroyer, fighter, carrier, infantry, mech, warSun;
    float mecatol, aggression, expansion, deals, threat;
    int8_t expPay[3];  // Thunder's Edge expedition: the slices it prefers to pay (0 resources .. 5 trade goods)
    int8_t expFrom;    // the round it starts paying for the expedition (0: whenever it pays)
    float hitEarly;    // the guide's "playing against them": hit them early (others weigh their planets more)
    float fracture;    // Thunder's Edge: go into the Fracture (+1) .. defend the ingress instead (-1)
    int8_t fracFrom;   // from this round
    float station;     // the worth of space stations to it (+1 take them early)
};

// From the owner's faction strategy guide (docs/owner/TI4-Faction-Strategy-Guides.pdf, 0.43.0), written by
// tools/gen_guides.py; the second technology paths and the behaviour dials carry over from the 0.18-0.42 table.
#include "ai/gen/guides.inc"

const FactionGuide* guideOf(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return nullptr;
    const char* key = kFactions[g.players[p].faction].key;
    for (const FactionGuide& f : kGuides)
        if (!std::strcmp(f.key, key)) return &f;
    return nullptr;
}

// A technology of the plan for this player: generic unit upgrades turn into the
// faction's own (Dreadnought II -> Super-Dreadnought II for the L1Z1X), other
// factions' technologies are skipped.
int resolveTech(const Game& g, int p, const char* key) {
    const int t = findTech(key);
    if (t < 0) return -1;
    const int fac = g.players[p].faction;
    for (int u = 0; u < kTechCount; ++u)
        if (kTechs[u].faction == fac && kTechs[u].replaces == t) return u;
    if (kTechs[t].faction >= 0 && kTechs[t].faction != fac) return -1;
    if (!techImplemented(t)) return -1;
    return t;
}

// Which second path suits the personality: 1 aggressive, 2 control / economy.
int pathFor(const Game& g, int p, const FactionGuide& f) {
    const int a = archNow(g, p);
    switch (Arch(a)) {
        case Arch::Warlord:
        case Arch::Betrayer:
        case Arch::Opportunist:
        case Arch::Rusher: return 1;
        case Arch::Turtler:
        case Arch::Peacekeeper:
        case Arch::Politician:
        case Arch::Merchant:
        case Arch::Swindler: return 2;
        case Arch::Chaotic: return 1 + int(prand(g, 51, uint32_t(g.round), uint32_t(p), 0, 2));
        default: break;
    }
    return f.aggression >= 1.1f ? 1 : 2;
}

// Does the personality go its own way this round? (the core then only whispers)
bool deviates(const Game& g, int p) {
    int pct = 10;
    switch (Arch(archNow(g, p))) {
        case Arch::Chaotic: pct = 35; break;
        case Arch::Opportunist:
        case Arch::Swindler: pct = 20; break;
        case Arch::Betrayer: pct = 15; break;
        default: break;
    }
    if (aiCompetitiveSeat(g, p)) pct /= 2;
    return int(prand(g, 52, uint32_t(g.round), uint32_t(p))) < pct;
}

}  // namespace

int aiResolveTech(const Game& g, int p, const char* key) { return resolveTech(g, p, key); }

bool aiCoreOn(const Game& g, int p) {
    // test only: TI4_SIMCORESEATS=024 - only these seats use the core (head-to-head), "-" = nobody
    static const int mask = [] {
        const char* s = std::getenv("TI4_SIMCORESEATS");
        if (!s) return -1;
        int m = 0;
        for (const char* c = s; *c; ++c)
            if (*c >= '0' && *c <= '7') m |= 1 << (*c - '0');
        return m;
    }();
    if (mask >= 0 && !((mask >> p) & 1)) return false;
    return aiLevelOf(g) >= 1 && guideOf(g, p) != nullptr;
}

std::vector<int> aiCoreTechPlan(const Game& g, int p) {
    std::vector<int> plan;
    const FactionGuide* f = guideOf(g, p);
    if (!f) return plan;
    auto add = [&](const char* const* keys, int n) {
        for (int i = 0; i < n; ++i) {
            if (!keys[i]) continue;
            int t = resolveTech(g, p, keys[i]);
            if (t >= 0 && std::find(plan.begin(), plan.end(), t) == plan.end()) plan.push_back(t);
        }
    };
    // (Sardakk review, owner 2026-11-11 - test C) Magen Defense Grid right after Plasma Scoring: the second red one that
    // Valkyrie Particle Weave needs
    if (!std::strcmp(f->key, "sardakk") && aiTune("sardmagen", 0.f) > 0.f) {
        static const char* const kSardakkCore[8] = {"ps", "md_base", "vpw", "exo2", "gd", "asc", "amd", "fl"};
        add(kSardakkCore, 8);
    }
    add(f->core, 8);
    if (g.modules & kModThundersEdge) add(f->te, 4);
    if (pathFor(g, p, *f) == 1) add(f->aggressive, 5);
    else add(f->control, 5);
    return plan;
}

// Extra want for a technology: the next steps of the plan, and answers to what
// the neighbours field.
float aiCoreTechBonus(const Game& g, int p, int tech) {
    if (!aiCoreOn(g, p)) return 0.f;
    const Player& me = g.players[p];
    float v = 0;
    int step = 0;
    int blocked = -1;  // the first step of the plan whose prerequisites we don't have yet
    for (int t : aiCoreTechPlan(g, p)) {
        if (me.hasTech(t)) continue;
        if (blocked < 0 && t != tech && !canResearchNow(g, p, t)) blocked = t;
        if (t == tech) {
            v += step == 0 ? 5.f : step == 1 ? 3.f : step == 2 ? 2.f : 1.f;
            break;
        }
        ++step;
    }
    // a technology of the colour the blocked step still needs (red techs toward a war sun...)
    if (blocked >= 0 && !kTechs[tech].unitUpgrade && kTechs[tech].color != TechColor::None) {
        const int c = int(kTechs[tech].color);
        if (techCount(g, p, kTechs[tech].color) < kTechs[blocked].req[c]) v += 1.5f;
    }
    // 0.44.2 (tester: "Saar is not using space docks properly. It should be a high emphasis on the strategy guide"):
    // Floating Factory II (move 2, capacity) is what makes the Saarball - first, ahead of Gravity Drive
    if (!std::strcmp(kTechs[tech].key, "ffac2") && isFaction(g, p, "saar") && aiTune("saarff2", 1.f) > 0.f) v += 3.f;
    // 0.44.7 (owner's strategy guide): Chaos Mapping early for the Saar - a free unit every turn, asteroid fields to hide in
    // (the Last Bastion review, checkpoint 2 - switch bastiontech, test: researched in about 1 game in 4 / 5) Proxima Targeting
    // VI and the Helios V2 docks
    if ((!std::strcmp(kTechs[tech].key, "proxima") || !std::strcmp(kTechs[tech].key, "helios2")) && isFaction(g, p, "bastion"))
        v += aiTune("bastiontech", 0.f);
    // (the Saar review, checkpoint 2: knob saarcm, default 1 = this bonus; 0 = none - test only)
    if (!std::strcmp(kTechs[tech].key, "cm") && isFaction(g, p, "saar") && aiTune("saarcm", 1.f) > 0.f) v += 2.f;
    // (0.59.0, owner: the Crimson don't expand) a home beside the board, 2 moves from everything: Gravity Drive first
    if (!std::strcmp(kTechs[tech].key, "gd") && isFaction(g, p, "crimson") && g.round <= 4 && aiTune("crimsongd", 1.f) > 0.f) v += 6.f;
    // (L1Z1X review, checkpoint 2 - owner's guide: "Inheritance Systems means you can jump straight to Dreadnought
    // II") the two yellow technologies Inheritance Systems needs, then Inheritance Systems itself; the bots took it in
    // 8 of 90 games and Super-Dreadnought II (2 blue + 1 yellow) in 16. Tested (90 games): l1inherit=4 Inheritance
    // Systems in 37 games, Super-Dreadnought II in 32, 3.48 dreadnoughts a game (3.18), 12 wins / 7.09 VP against 17 /
    // 7.04; l1inherit=8: 61 / 43 games, 12 / 6.86 - the guide's path costs the bots wins: OFF
    if (aiTune("l1inherit", 0.f) > 0.f && isFaction(g, p, "l1z1x")) {
        const int is = findTech("is");
        if (is >= 0 && !me.hasTech(is)) {
            if (tech == is) v += aiTune("l1inherit", 0.f);
            else if (kTechs[tech].color == TechColor::Yellow && !kTechs[tech].unitUpgrade && techCount(g, p, TechColor::Yellow) < 2)
                v += 0.6f * aiTune("l1inherit", 0.f);
        }
    }
    if (deviates(g, p)) v *= 0.5f;
    // threats next door: what the other players have within 2 systems of ours
    int infantry = 0, fighters = 0, bigShips = 0, pds = 0, ships = 0;
    std::vector<char> near(g.systems.size(), 0);
    for (size_t s = 0; s < g.systems.size(); ++s) {
        bool mine = g.systems[s].space[p].ships() > 0;
        for (int16_t x : g.systems[s].planets)
            if (g.planets[x].owner == p) mine = true;
        if (!mine) continue;
        for (int s2 : systemsWithin(g, int(s), 2)) near[size_t(s2)] = 1;
    }
    for (size_t s = 0; s < g.systems.size(); ++s) {
        if (!near[s]) continue;
        const SystemState& ss = g.systems[s];
        for (int q = 0; q < g.numPlayers; ++q) {
            if (q == p || g.players[q].eliminated) continue;
            const Force& f = ss.space[q];
            fighters += f.count(UnitType::Fighter);
            bigShips += f.count(UnitType::Dreadnought) + f.count(UnitType::WarSun) + f.flagships();
            ships += f.nonFighterShips();
            for (int16_t x : ss.planets) {
                const Force& pf = g.planets[x].units[q];
                infantry += pf.count(UnitType::Infantry);
                pds += pf.count(UnitType::PDS);
            }
        }
    }
    auto is = [&](const char* k) { return resolveTech(g, p, k) == tech; };
    if (infantry >= 8 && is("x89_base")) v += 4.f + std::min(3.f, float(infantry - 8) * 0.3f);  // X-89: infantry stacks
    if (codexOn(g) && g.fixes >= 4 && is("x89_base")) v += 2.5f;  // (0.43.3: Thunder's Edge's X-89 - twice the ground hits)
    if (fighters >= 8 && is("dd2")) v += 3.f + std::min(2.f, float(fighters - 8) * 0.25f);        // anti-fighter barrage
    if (fighters >= 12 && is("gls")) v += 1.5f;                                                     // Graviton: hits on real ships
    if (bigShips >= 3 && (is("asc") || is("da"))) v += 2.f;                                        // big fleets next door
    if (pds >= 3 && is("amd")) v += 2.5f;                                                          // PDS walls
    if (ships >= 6 && is("pds2")) v += 1.5f;                                                       // a fleet at the door
    if (ships >= 6 && is("md_base")) v += 1.f;
    return v;
}

void aiCoreApply(Style& st, const Game& g, int p) {
    if (!aiCoreOn(g, p)) return;
    const FactionGuide& f = *guideOf(g, p);
    const float follow = deviates(g, p) ? 0.25f : 1.f;
    // strategy cards: the opening priorities (fading in rounds 2-3), the cards to leave early, the late-game ones
    // (0.43.0: from the guide's "Strategy cards", blending in during round 3)
    const float early = g.round <= 1 ? 1.f : g.round == 2 ? 0.5f : g.round == 3 ? 0.25f : 0.f;
    const float late = (g.round <= 2 ? 0.f : g.round == 3 ? 0.5f : 1.f) * aiTune("g_late", 1.f);
    static const float kPrio[4] = {2.0f, 1.4f, 0.9f, 0.5f}, kLate[3] = {1.4f, 0.9f, 0.5f};
    for (int i = 0; i < 4; ++i)
        if (f.r1[i] >= 0) st.card[f.r1[i]] += kPrio[i] * early * follow;
    for (int i = 0; i < 3; ++i)
        if (f.late[i] >= 0) st.card[f.late[i]] += kLate[i] * late * follow;
    if (g.round <= 2)
        for (int c = 0; c < 8; ++c)
            if (f.avoid & (1 << c)) st.card[c] -= 1.5f * follow;
    // the faction's way of fighting, expanding and trading
    auto mix = [&](float v) { return 1.f + (v - 1.f) * follow; };
    st.aggression *= mix(f.aggression);
    st.expansion *= mix(f.expansion);
    st.mecatol *= mix(f.mecatol);
    st.dealRate *= mix(f.deals);
    st.threat += f.threat * follow;
    // (the Arborec review, checkpoint 2 - test knob arbmech = the multiplier) the Arborec's Letani Behemoths
    // (PRODUCTION 2, PLANETARY SHIELD, SUSTAIN DAMAGE): the guide's 1.3 stays under aiProduce's 1.4 bar for favourite
    // units, so only the Overgrowth trait made them build mechs (150 in 90 games against 8 with Best Friend).
    // Tested OFF (arbmech=1.6): 15 wins / 7.22 VP against 13 / 6.97, but on fresh seeds 24 / 7.27 against 25 / 7.23
    // (mechs built 69 -> 144 and 101 -> 154); with Best Friend forced 13 wins against 8
    float mech = !std::strcmp(f.key, "arborec") && aiTune("arbmech", 0.f) > 0.f ? aiTune("arbmech", 0.f) : f.mech;
    // (the Argent review, checkpoint 2 - test knob argmech = the multiplier, default off) Aerie Sentinel needs no capacity
    // beside a ship with capacity, yet the Argent bots built 0.10 mechs a game. Tested (argmech=1.6, ~3 mechs a game):
    // TE 24 wins / 7.61 VP against 21 / 7.71, fresh seeds 17 / 7.42 against 20 / 7.59; plain 21 / 7.73 against 16 / 7.49,
    // fresh 20 / 7.69 against 18 / 7.57 - a gain without Thunder's Edge only. OFF
    if (!std::strcmp(f.key, "argent") && aiTune("argmech", 0.f) > 0.f) mech = aiTune("argmech", 0.f);
    // (the Naaz-Rokha review, checkpoint 2 - test knob naazmech = the multiplier, default off) Distant Suns, Dart and Tai's
    // unlock (mechs in 3 systems) and Absolute Synergy (4 mechs in a system) all want mechs; the bots built 2.4 a game.
    // Tested (naazmech=2.6): mechs built 2.37 -> 2.39, 9 wins / 7.18 VP against 9 / 7.19 - the mech limit (4, 1 at the
    // start) caps it already. OFF
    if (!std::strcmp(f.key, "naaz") && aiTune("naazmech", 0.f) > 0.f) mech = aiTune("naazmech", 0.f);
    const float units[] = {f.dread, f.cruiser, f.destroyer, f.fighter, f.carrier, f.infantry, mech, f.warSun};
    const UnitType types[] = {UnitType::Dreadnought, UnitType::Cruiser, UnitType::Destroyer, UnitType::Fighter,
                              UnitType::Carrier,     UnitType::Infantry, UnitType::Mech,      UnitType::WarSun};
    for (int i = 0; i < 8; ++i) st.unit[int(types[i])] *= mix(units[i]);
}

// Thunder's Edge expedition (0.43.0): the slices the guide prefers this faction pays with cost less to it (the
// planner's units, about 1 per resource), and before the round it starts paying the breakthrough is worth less.
float aiCoreSliceBias(const Game& g, int p, int slice) {
    if (!aiCoreOn(g, p)) return 0.f;
    const FactionGuide& f = *guideOf(g, p);
    static const float kBias[3] = {1.2f, 0.7f, 0.4f};
    for (int i = 0; i < 3; ++i)
        if (f.expPay[i] == slice) return kBias[i] * aiTune("g_exppay", 1.f);
    return 0.f;
}
bool aiCoreExpeditionLater(const Game& g, int p) {
    if (!aiCoreOn(g, p) || aiTune("g_expfrom", 1.f) <= 0.f) return false;
    return g.round < guideOf(g, p)->expFrom;
}

// A planet's extra worth from the guides (0.43.0): Thunder's Edge space stations and Fracture planets for the
// factions whose guide sends them there (or keeps them out), and a faction the guides say to hit early.
float aiCorePlanetBonus(const Game& g, int p, int pl) {
    if (!aiCoreOn(g, p)) return 0.f;
    const FactionGuide& f = *guideOf(g, p);
    const PlanetState& ps = g.planets[size_t(pl)];
    float v = 0.f;
    if ((kPlanets[ps.def].flags & kPlanetStation) && f.station != 0.f) v += 1.2f * f.station * aiTune("g_station", 1.f);
    if (g.systems[size_t(ps.system)].fracture && f.fracture != 0.f && g.round >= f.fracFrom) v += 1.5f * f.fracture * aiTune("g_frac", 1.f);
    if (ps.owner >= 0 && ps.owner != p && g.round <= 3) {
        const FactionGuide* them = guideOf(g, ps.owner);
        if (them && them->hitEarly > 1.f) v += 3.f * (them->hitEarly - 1.f) * aiTune("g_hit", 1.f);
    }
    return v;
}

// War suns (owner: "nobody ever builds war suns, not even Muaat"): build them
// when the faction is made for it (Muaat) or the aggressive path got there.
bool aiCoreWarSuns(const Game& g, int p) {
    if (!aiCoreOn(g, p) || unitsAvailable(g, p, UnitType::WarSun) <= 0) return false;
    const FactionGuide& f = *guideOf(g, p);
    if (f.warSun >= 1.5f) return g.round >= 2;
    return g.round >= 3 && pathFor(g, p, f) == 1;
}

}  // namespace ti
