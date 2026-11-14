#include "engine/query.h"
#include "engine/breakthroughs.h"
#include "engine/map.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <set>

namespace ti {

namespace {
bool keyIs(int obj, const char* k) { return !std::strcmp(kObjectives[obj].key, k); }
int techByKey(const char* k) {
    static std::vector<std::pair<const char*, int>> byPointer;  // keys are mostly string literals
    for (auto& c : byPointer)
        if (c.first == k) return c.second;
    static std::vector<std::pair<std::string, int>> cache;
    for (auto& c : cache)
        if (c.first == k) {
            if (byPointer.size() < 512) byPointer.push_back({k, c.second});
            return c.second;
        }
    int t = findTech(k);
    cache.push_back({k, t});
    return t;
}
bool owns(const Game& g, int p, const char* techKeyName) { return ownsTechKey(g, p, techKeyName); }
}  // namespace

bool codexOn(const Game& g) { return (g.modules & kModCodex) || ((g.modules & kModThundersEdge) && g.fixes >= 5); }
bool teXxchaHero(const Game& g) { return (g.modules & kModThundersEdge) && g.fixes >= 20; }
// (fixes 27, the Naalu review - owner 2026-11-13) Thunder's Edge's own Z'eu and Iconoclast replace the Codex Omegas
bool teNaalu(const Game& g) { return (g.modules & kModThundersEdge) && g.fixes >= 27; }

bool x89Omega(const Game& g, int p) {
    return codexOn(g) && g.fixes >= 4 && p >= 0 && p < g.numPlayers && ownsTechKey(g, p, "x89_base");
}

bool ownsTechKey(const Game& g, int p, const char* key) {
    int t = techByKey(key);
    if (t < 0 || p < 0 || p >= g.numPlayers) return false;
    if (t == g.fx.blankTech && p == g.fx.blankOwner) return false;  // Sanctification Field (Discordant Stars): no text now
    const Player& pl = g.players[p];
    if (pl.hasTech(t)) return true;
    // Valefar Assimilator (Nekro): the card has the text of the technology its token is on
    // (0.44.8, fixes 12) a Valefar card under Sanctification Field has no text - nor the copy on it
    const bool blankV = g.fixes >= 12 && p == g.fx.blankOwner && g.fx.blankTech >= 0;
    if (pl.assimX == t && pl.hasTech(techByKey("vax")) && !(blankV && g.fx.blankTech == techByKey("vax"))) return true;
    if (pl.assimY == t && pl.hasTech(techByKey("vay")) && !(blankV && g.fx.blankTech == techByKey("vay"))) return true;
    return false;
}

// ============================================================ players
std::string playerName(const Game& g, int p) {
    if (p == kNeutral) return "Neutral";
    if (p < 0 || p >= g.numPlayers) return "?";
    return factionShortName(g.players[p].faction);
}

int initiativeOf(const Game& g, int p) {
    if (g.naaluToken == p && p >= 0) return 0;  // Naalu "0" token (Telepathic / Gift of Prescience)
    int best = 9;
    for (int8_t c : g.players[p].cards) best = std::min(best, c == g.blight ? 9 : c + 1);  // (Brikk Strain: initiative 9)
    return best;
}

std::vector<int> initiativeOrder(const Game& g) {
    std::vector<int> order;
    for (int p = 0; p < g.numPlayers; ++p)
        if (!g.players[p].eliminated) order.push_back(p);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return initiativeOf(g, a) < initiativeOf(g, b); });
    return order;
}

std::vector<int> clockwiseFrom(const Game& g, int first) {
    std::vector<int> v;
    for (int i = 0; i < g.numPlayers; ++i) {
        int p = (first + i) % g.numPlayers;
        if (!g.players[p].eliminated) v.push_back(p);
    }
    return v;
}

std::vector<int> fromSpeaker(const Game& g) { return clockwiseFrom(g, g.speaker); }

static std::vector<int> presence(const Game& g, int p) {
    std::vector<int> s;
    for (size_t i = 0; i < g.systems.size(); ++i) {
        const SystemState& sy = g.systems[i];
        bool here = !sy.space[p].empty();
        for (int16_t pl : sy.planets)
            if (g.planets[pl].owner == p || !g.planets[pl].units[p].empty()) here = true;
        if (here) s.push_back(int(i));
    }
    return s;
}

// Quantum Entanglement (Creuss): the alpha / beta wormholes a system contains - for the ability, which game effects
// can't prevent (fixes 24): Nexus Sovereignty doesn't hide the active nexus's
uint8_t qeWormholes(const Game& g, int sys) {
    if (sys < 0 || sys >= int(g.systems.size())) return 0;
    uint8_t w = wormholesOf(g, sys);
    if (g.fixes >= 24 && sys == g.nexus && g.systems[size_t(sys)].nexusActive) w |= uint8_t(g.systems[size_t(sys)].d().wormholes);
    return uint8_t(w & (kAlpha | kBeta));
}
bool qeLinked(const Game& g, int p, int a, int b) {
    return a != b && p >= 0 && p < g.numPlayers && isFaction(g, p, "ghost") && qeWormholes(g, a) && qeWormholes(g, b);
}

bool areNeighbours(const Game& g, int a, int b) {
    // I.I.H.Q. Modernization (Keleres breakthrough): neighbours of everyone in or next to Mecatol Rex
    for (int k : {a, b}) {
        const int o = k == a ? b : a;
        if (!breakthroughIs(g, k, "keleres") || g.mecatol() < 0) continue;
        const int m = g.mecatol();
        for (int x : presence(g, o))
            if (x == m || adjacentSystems(g, x, m)) return true;
    }
    std::vector<int> pa = presence(g, a), pb = presence(g, b);
    // (fixes 26, the Empyrean review - owner 2026-11-12) Void Tether: "other players do not treat those systems as adjacent
    // to each other" - not neighbours across a tethered border (the Empyrean themselves are)
    const bool tie = g.fixes >= 26 && !g.tethers.empty();
    for (int x : pa)
        for (int y : pb)
            if (x == y || (adjacentSystems(g, x, y) && !(tie && tethered(g, a, x, y) && tethered(g, b, x, y)))) return true;
    // (fixes 24, LRR 60.3) neighbours with the Creuss where Quantum Entanglement makes the systems adjacent for them
    if (g.fixes >= 24)
        for (int c : {a, b}) {
            if (!isFaction(g, c, "ghost")) continue;
            for (int x : c == a ? pa : pb)
                for (int y : c == a ? pb : pa)
                    if (qeLinked(g, c, x, y)) return true;
        }
    return false;
}

bool neighboursWithAll(const Game& g, int p) {
    // one pass: who is present in each system, then whether each player touches p's systems
    const int n = int(g.systems.size());
    std::vector<uint16_t> here(n, 0);
    for (int i = 0; i < n; ++i) {
        const SystemState& sy = g.systems[i];
        for (int q = 0; q < g.numPlayers; ++q) {
            bool in = !sy.space[q].empty();
            for (int16_t pl : sy.planets)
                if (g.planets[pl].owner == q || !g.planets[pl].units[q].empty()) in = true;
            if (in) here[i] = uint16_t(here[i] | (1u << q));
        }
    }
    uint16_t touched = 0, untied = 0;  // (untied: not across a tethered border - fixes 26, Void Tether)
    const bool tie = g.fixes >= 26 && !g.tethers.empty();
    for (int i = 0; i < n; ++i) {
        if (!((here[i] >> p) & 1)) continue;
        touched = uint16_t(touched | here[i]);
        untied = uint16_t(untied | here[i]);
        for (int16_t a : g.systems[i].adj) {
            touched = uint16_t(touched | here[a]);
            if (!(tie && tethered(g, p, i, a))) untied = uint16_t(untied | here[a]);
        }
    }
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && !g.players[q].eliminated && !(((tie && !isFaction(g, q, "empyrean") ? untied : touched) >> q) & 1) &&
            !(g.fixes >= 24 && (isFaction(g, p, "ghost") || isFaction(g, q, "ghost") || g.fx.taivra) && areNeighbours(g, p, q)))  // (LRR 60.3)
            return false;
    return true;
}

// ============================================================ planets
namespace {
bool exKey(int card, const char* k) { return card >= 0 && card < kExploreCardCount && !std::strcmp(kExploreCards[card].key, k); }
int facilityColor(int card) {
    if (exKey(card, "propulsion")) return int(TechColor::Blue);
    if (exKey(card, "biotic")) return int(TechColor::Green);
    if (exKey(card, "warfare")) return int(TechColor::Red);
    if (exKey(card, "cybernetic")) return int(TechColor::Yellow);
    return -1;
}
// Attachment bonuses (PoK exploration). Research facilities give a technology
// specialty, or +1/+1 if the planet already has one.
void attachBonus(const Game& g, int planet, int& res, int& inf, int& skips) {
    const PlanetState& ps = g.planets[planet];
    const PlanetDef& d = kPlanets[ps.def];
    res = d.res;
    inf = d.inf;
    if ((g.modules & kModDiscordant) && !g.dsCores.empty() && std::find(g.dsCores.begin(), g.dsCores.end(), int16_t(planet)) != g.dsCores.end()) {
        res = 2;  // Celestial Reclamation (Gledge): a Core token - base 2 resources, 0 influence
        inf = 0;
    }
    skips = d.skip != TechColor::None ? (1 << int(d.skip)) : 0;
    if (d.skip2 != TechColor::None) skips |= 1 << int(d.skip2);  // Thunder's Edge: a second specialty
    for (int16_t a : ps.attachments) {
        int fc = facilityColor(a);
        if (fc >= 0) {
            if (skips) {
                ++res;
                ++inf;
            } else {
                skips |= 1 << fc;
            }
            continue;
        }
        if (exKey(a, "ds")) res += 2, inf += 1;
        else if (exKey(a, "pw")) inf += 2;
        else if (exKey(a, "toe")) inf += 1;
        else if (exKey(a, "ls")) res += 1, inf += 2;
        else if (exKey(a, "mw")) res += 2;
        else if (exKey(a, "rw")) res += 1;
    }
    if (ps.terraform) res += 1, inf += 1;  // Terraform (Titans note)
    if (g.modules & kModDiscordant) {
        if (planet == g.dsBasePlanet) res += 2;                                         // Gledge Base (the Gledge's note)
        if (planet == g.dsKeyPlanet && g.dsKeyColor >= 0) skips |= 1 << g.dsKeyColor;  // Encryption Key (the Bentor's note)
    }
    if (ps.ulHero) res += 3, inf += 3;     // Ul the Progenitor (Titans hero)
    // The Helios docks (Last Bastion, Thunder's Edge): the planet's resources +1 (V1) / +2 (V2)
    if (ps.owner >= 0 && ps.owner < g.numPlayers && ps.units[size_t(ps.owner)].n[int(UnitType::SpaceDock)]) {
        const char* k = kUnits[g.players[size_t(ps.owner)].unit[int(UnitType::SpaceDock)]].key;
        if (!std::strncmp(k, "bastion_spacedock", 17)) res += k[17] == '2' ? 2 : 1;
    }
    // Branch Offices (Veldyr notes, Discordant Stars): Tax Haven / Broadcast Hub +1 influence, Reserve Bank / Orbital Shipyard +1 resource
    if (g.modules & kModDiscordant)
        for (int k = 0; k < 4; ++k)
            if (g.dsBranch[k] == planet) (k < 2 ? inf : res) += 1;
    // Exemplar (Olradin mech, Discordant Stars): a lone one - +1 resource (2+ "+" policies) or +1 influence (2+ "-" ones)
    if (ps.owner >= 0 && ps.owner < g.numPlayers && (g.modules & kModDiscordant) && isFaction(g, ps.owner, "olradin") && mechAbilities(g) &&
        ps.units[size_t(ps.owner)].count(UnitType::Mech) == 1) {
        const uint8_t pol = g.players[size_t(ps.owner)].dsPolicy;
        const int minus = (pol & 1) + ((pol >> 1) & 1) + ((pol >> 2) & 1);
        if (minus >= 2) inf += 1;
        else res += 1;
    }
    // Garden Worlds (L'tokk Khrask, Discordant Stars): +1 resource while no ground forces are on it
    if (ps.owner >= 0 && ps.owner < g.numPlayers && (g.modules & kModDiscordant) && isFaction(g, ps.owner, "khrask")) {
        bool gf = false;
        for (int q = 0; q < kSides && !gf; ++q) gf = ps.units[size_t(q)].groundForces() > 0;
        if (!gf) res += 1;
    }
}
}  // namespace

int planetResources(const Game& g, int planet) {
    int r, i, s;
    attachBonus(g, planet, r, i, s);
    return planet == g.fx.swapPlanet ? i : r;  // Hegemonic Trade Policy
}
int planetInfluence(const Game& g, int planet) {
    int r, i, s;
    attachBonus(g, planet, r, i, s);
    return planet == g.fx.swapPlanet ? r : i;
}
int planetSkips(const Game& g, int planet) {
    int r, i, s;
    attachBonus(g, planet, r, i, s);
    return s;
}
bool planetHasSkip(const Game& g, int planet) { return planetSkips(g, planet) != 0; }
bool hasAttachment(const Game& g, int planet, const char* key) {
    for (int16_t a : g.planets[planet].attachments)
        if (exKey(a, key)) return true;
    return false;
}
bool isDMZ(const Game& g, int planet) {
    return hasAttachment(g, planet, "dmz") || isStation(g, planet) || (planet >= 0 && planet == g.dsHeart);  // (Heart of Rebellion)
}
bool isStation(const Game& g, int planet) {
    return planet >= 0 && planet < int(g.planets.size()) && (kPlanets[g.planets[size_t(planet)].def].flags & kPlanetStation);
}
// (fixes 26, the Empyrean review - owner 2026-11-12) a system that "does not contain any planets": a space station isn't
// a planet (Thunder's Edge rulebook: "A system that contains a space station and no planets gets a frontier token")
bool planetless(const Game& g, int sys) {
    for (int16_t x : g.systems[size_t(sys)].planets)
        if (!(g.fixes >= 26 && isStation(g, x))) return false;
    return true;
}
// Omni-Forgeworld (Roh'Dhna war sun with The Prodigy's Triumph; 0.44.9, fixes 13 - the owner's answer B9): each one is
// also a space station - exhausting its "planet card" damages it instead
int forgeworldsOf(const Game& g, int p, bool readyOnly) {
    if (g.fixes < 13 || !(g.modules & kModDiscordant) || p < 0 || p >= g.numPlayers) return 0;
    if (std::strcmp(unitDef(g, p, UnitType::WarSun).key, "rohdhna_warsun3") != 0) return 0;
    int n = 0;
    for (const SystemState& s : g.systems) {
        const Force& f = s.space[size_t(p)];
        n += readyOnly ? f.count(UnitType::WarSun) - f.dmg[int(UnitType::WarSun)] : f.count(UnitType::WarSun);
    }
    return n;
}
int stationsOf(const Game& g, int p) {
    int n = forgeworldsOf(g, p, false);
    for (const PlanetState& ps : g.planets)
        if (ps.owner == p && !ps.destroyed && (kPlanets[ps.def].flags & kPlanetStation)) ++n;
    if ((g.modules & kModDiscordant) && !g.dsCores.empty() && p >= 0 && p < g.numPlayers && breakthroughIs(g, p, "gledge"))
        for (int16_t x : g.dsCores)
            if (dhonrazStation(g, p, x) && !isStation(g, x)) ++n;
    return n;
}
bool dhonrazStation(const Game& g, int p, int planet) {
    if (!(g.modules & kModDiscordant) || p < 0 || p >= g.numPlayers || planet < 0 || planet >= int(g.planets.size())) return false;
    if (g.planets[size_t(planet)].owner != p || g.planets[size_t(planet)].destroyed) return false;
    if (std::find(g.dsCores.begin(), g.dsCores.end(), int16_t(planet)) == g.dsCores.end()) return false;
    return breakthroughIs(g, p, "gledge");
}
bool stationFor(const Game& g, int p, int planet) { return isStation(g, planet) || dhonrazStation(g, p, planet); }
int commodityValue(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return 0;
    // C.M.O. Ranc (Bentor commander, Discordant Stars): +1
    const int ranc = (g.modules & kModDiscordant) && commanderUsable(g, p, "bentor") ? 1 : 0;
    // Exploit (Olradin, "-" side of The Economy): commodity value 2
    const int base = (g.modules & kModDiscordant) && isFaction(g, p, "olradin") && ((g.players[size_t(p)].dsPolicy >> 2) & 1)
                         ? 2 : kFactions[g.players[size_t(p)].faction].commodities;
    // Necrophage (Myko-Mentori): +1 for each space dock they control
    const int necro = (g.modules & kModDiscordant) && isFaction(g, p, "mykomentori") ? unitsOnBoard(g, p, UnitType::SpaceDock) : 0;
    return base + stationsOf(g, p) + ranc + necro;
}
int commodityValue(const Game& g, const Player& pl) {
    const Player* b = g.players.data();
    if (&pl >= b && &pl < b + g.players.size()) return commodityValue(g, int(&pl - b));
    return kFactions[pl.faction].commodities;
}
bool moveBoost(const Game& g, int p) { return boostShips(g, p) > 0 || lightningDrives(g, p); }
int moveBonusMax(const Game& g, int p) {
    if (g.fixes < 13) return moveBoost(g, p) ? 1 : 0;
    return boostShips(g, p) + (lightningDrives(g, p) ? 1 : 0);
}
// Lightning Drives (Gledge, Discordant Stars): +1 move for non-fighter ships that carry no fighters or infantry
bool lightningDrives(const Game& g, int p) { return (g.modules & kModDiscordant) && p >= 0 && p < g.numPlayers && owns(g, p, "dsgledb"); }
int boostShips(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return 0;  // (Thunder's Edge: not for neutral units)
    // (0.44.7, fixes 11: through ownsTechKey - a Sanctification Field blank silences it)
    const bool gd = g.fixes >= 11 ? ownsTechKey(g, p, "gd") : g.players[size_t(p)].hasTech(findTech("gd"));
    return (gd ? 1 : 0) + (g.fx.tempesta && p == g.activePlayer ? 1 : 0) +
           (g.fx.imperator && p == g.activePlayer ? 1 : 0) +  // Imperator (Winnu breakthrough)
           (g.fx.fillipo == p ? 1 : 0) +                      // Fillipo Rois (Tnelis commander, Discordant Stars)
           (g.fx.dsBoostOwner == p ? g.fx.dsBoost : 0);        // Midas Turbine / Vaden Handshake (Discordant Stars)
}
bool isLegendaryPlanet(const Game& g, int planet) {
    const PlanetDef& d = kPlanets[g.planets[size_t(planet)].def];
    return d.legendary || ((g.modules & kModThundersEdge) && !std::strcmp(d.key, "mr"));
}
bool scarAt(const Game& g, int sys) {
    if (sys < 0 || sys >= int(g.systems.size())) return false;
    if (g.systems[size_t(sys)].d().anomalies & kScar) return true;
    // Anomaly Stabilization (Nivyn breakthrough, 0.43.0): the Wound's system is also an entropic scar
    if ((g.modules & kModDiscordant) && sys == g.dsWound && breakthroughsOn(g)) {
        const int n = playerOfFaction(g, "nivyn");
        return n >= 0 && breakthroughIs(g, n, "nivyn");
    }
    return false;
}
// The Quietus (Crimson flagship): while it is in a system with an active breach, other players' units in systems with
// active breaches lose their unit abilities
bool quietusAt(const Game& g, int p, int sys) {
    if (!(g.modules & kModThundersEdge) || p < 0 || p >= g.numPlayers || sys < 0 || sys >= int(g.systems.size()) || g.systems[size_t(sys)].breach != 2)
        return false;
    for (int c = 0; c < g.numPlayers; ++c) {
        if (c == p || !flagshipAbility(g, c, "crimson")) continue;
        for (size_t s = 0; s < g.systems.size(); ++s)
            if (g.systems[s].breach == 2 && g.systems[s].space[size_t(c)].count(UnitType::Flagship)) return true;
    }
    return false;
}
bool flagshipText(const Game& g, int p, const char* factionKey) {
    return g.fixes >= 8 ? flagshipAbility(g, p, factionKey) : isFaction(g, p, factionKey);
}
bool eidolonShip(const Game& g, int p) {
    return g.fixes >= 8 && p >= 0 && p < g.numPlayers && g.players[size_t(p)].eidolonMax && breakthroughIs(g, p, "naaz");
}
bool ignoresScars(const Game& g, int p) {
    return g.fixes >= 8 && (g.modules & kModDiscordant) && p >= 0 && p < g.numPlayers && breakthroughsOn(g) && breakthroughIs(g, p, "nivyn");
}
bool noAbilities(const Game& g, int p, int sys) {
    // (Anomaly Stabilization, Nivyn breakthrough: they ignore entropic scars)
    const bool nivyn = (g.modules & kModDiscordant) && p >= 0 && p < g.numPlayers && breakthroughsOn(g) && breakthroughIs(g, p, "nivyn");
    return (scarAt(g, sys) && !nivyn) || quietusAt(g, p, sys);
}
bool hasTraitOf(const Game& g, int planet, Trait t) {
    if (t == Trait::None) return false;
    const PlanetDef& d = kPlanets[g.planets[planet].def];
    // Terraform: all three traits; TE: two; Minor Factions (galactic event): a minor faction's planets all three
    return d.trait == t || d.trait2 == t || g.planets[planet].terraform || g.planets[planet].minor >= 0;
}
std::vector<Trait> traitsOf(const Game& g, int planet) {
    std::vector<Trait> v;
    const PlanetDef& d = kPlanets[g.planets[size_t(planet)].def];
    for (Trait t : {d.trait, d.trait2, Trait::Cultural, Trait::Hazardous, Trait::Industrial})
        if (hasTraitOf(g, planet, t) && std::find(v.begin(), v.end(), t) == v.end()) v.push_back(t);
    return v;
}
uint8_t wormholesOf(const Game& g, int sys) {
    const SystemState& s = g.systems[sys];
    uint8_t w = s.d().wormholes;
    if (sys == g.nexus) {
        w = s.nexusActive ? w : uint8_t(w & kGamma);
        if (lawInPlay(g, "nexus")) w &= uint8_t(~(kAlpha | kBeta));  // Nexus Sovereignty (movement)
    }
    w = uint8_t(w | s.tokenWormholes);
    // Creuss wormhole tokens, and the delta wormhole that travels with Hil Colish (Creuss flagship)
    for (int k = 0; k < 3; ++k)
        if (g.creussTokens[k] == sys) w |= k == 0 ? kAlpha : k == 1 ? kBeta : kGamma;
    int c = playerOfFaction(g, "ghost");
    if (c >= 0 && s.space[c].count(UnitType::Flagship)) w |= kDelta;
    // (0.44.9, fixes 13 - the owner's answer B9) a Nekro flagship with Hil Colish's ability (Valefar Assimilator Z)
    if (g.fixes >= 13 && c >= 0)
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != c && s.space[size_t(q)].count(UnitType::Flagship) && flagshipAbility(g, q, "ghost")) w |= kDelta;
    return w;
}
int attachedPlanets(const Game& g, int p) {
    int n = 0;
    for (const PlanetState& ps : g.planets)
        if (ps.owner == p && !ps.attachments.empty()) ++n;
    return n;
}
int relicFragments(const Game& g, int p, int type) {
    if (p < 0 || p >= g.numPlayers) return 0;  // (Thunder's Edge: not for neutral units)
    int n = 0;
    for (int16_t f : g.players[p].fragments)
        if (isRelicFragment(f) && (type < 0 || int(kExploreCards[f].type) == type || kExploreCards[f].type == ExploreType::Frontier)) ++n;
    return n;
}
bool isRelicFragment(int card) { return card >= 0 && card < kExploreCardCount && kExploreCards[card].kind == ExploreKind::Fragment; }
int fragmentCount(const Game& g, int p) { return relicFragments(g, p, -1); }
int starChartCount(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return 0;  // (Thunder's Edge: not for neutral units)
    int n = 0;
    for (int16_t f : g.players[p].fragments)
        if (kExploreCards[f].kind == ExploreKind::Chart) ++n;
    return n;
}
bool relicHeld(const Game& g, int p, const char* key) { return p >= 0 && g.players[p].hasRelic(findRelic(key)); }
bool isHomePlanet(const Game& g, int planet) { return g.systems[g.planets[planet].system].home >= 0; }
int homeOwner(const Game& g, int sys) { return g.systems[sys].home; }

// Captain Aroz (Firmament commander): while secret objectives are checked, planets in systems with p's ships count
// as p's (meetsObjective sets it)
static thread_local int tlAroz = -1;
// 0.43.0 (T22): while an objective is checked, planets where p's units coexist count as p's (Thunder's Edge)
namespace {
int g_objCoexist = 0;
struct ObjCoexist {
    ObjCoexist() { ++g_objCoexist; }
    ~ObjCoexist() { --g_objCoexist; }
};
}  // namespace
std::vector<int> controlledPlanets(const Game& g, int p) {
    std::vector<int> v;
    for (size_t i = 0; i < g.planets.size(); ++i)
        if (g.planets[i].owner == p) v.push_back(int(i));
        else if (g_objCoexist && p >= 0 && p < 16 && g.fixes >= 2 && ((g.planets[i].coexist >> p) & 1) && !g.planets[i].destroyed)
            v.push_back(int(i));
        else if (p == tlAroz && p >= 0 && !g.planets[i].destroyed && g.planets[i].system >= 0 && hasShips(g, p, g.planets[i].system))
            v.push_back(int(i));
    return v;
}

std::vector<int> readyPlanets(const Game& g, int p) {
    std::vector<int> v;
    for (size_t i = 0; i < g.planets.size(); ++i)
        if (g.planets[i].owner == p && !g.planets[i].exhausted) v.push_back(int(i));
    return v;
}

int spendable(const Game& g, int p, bool influence, bool withTradeGoods) {
    if (p < 0 || p >= g.numPlayers) return 0;  // (Thunder's Edge: not for neutral units)
    int total = withTradeGoods ? (g.players[p].tradeGoods + xanderCommodities(g, p)) * tgWorth(g, p) : 0;
    for (int pl : readyPlanets(g, p)) total += planetPayValue(g, pl, influence, false);
    int ghoti = (g.modules & kModDiscordant) && isFaction(g, p, "ghoti") && !g.players[size_t(p)].dsGhotiExhausted ? 3 : 0;  // (their planet card)
    if (!influence && (g.modules & kModDiscordant) && !g.players[size_t(p)].dsGhotiExhausted && breakthroughIs(g, p, "khrask")) ghoti = 2;  // (The Grove)
    return total + triadValue(g, p) + custodiaValue(g, p, influence) + oceansReady(g, p) + ghoti;
}

int oceansReady(const Game& g, int p) {  // Oceanbound (Deepwrought): ready ocean cards, 1 resource or 1 influence each
    if (p < 0 || p >= g.numPlayers) return 0;
    return std::max(0, oceanCards(g, p) - g.players[size_t(p)].oceansExhausted);
}
// Bannerhalls (Kjalengard breakthrough, 0.43.0): a planet card (1 resource / 1 influence, like an ocean card) for each
// Glory token on the board - they share the ocean cards' counters (no faction has both)
int oceanCards(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return 0;
    int n = g.players[size_t(p)].oceans;
    if ((g.modules & kModDiscordant) && !g.dsGlory.empty() && breakthroughIs(g, p, "kjalengard")) n += int(g.dsGlory.size());
    return n;
}

// The Triad (Thunder's Edge relic): spent like a planet card worth 3 + the kinds of relic fragment you hold, as
// resources or influence (0 when not held or exhausted).
int triadValue(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers || !(g.modules & kModThundersEdge)) return 0;
    const int r = findRelic("thetriad");
    const Player& pl = g.players[size_t(p)];
    if (r < 0 || !pl.hasRelic(r) || pl.relicExhausted(r)) return 0;
    int kinds = 0;
    for (int16_t f : pl.fragments)
        if (isRelicFragment(f)) kinds |= 1 << int(kExploreCards[f].type);
    int n = 0;
    for (; kinds; kinds &= kinds - 1) ++n;
    return 3 + n;
}

// Xander Alexin Victori III (Keleres agent) lets a player spend commodities as trade goods:
// the Keleres player's own, or another's who was promised the agent in a deal.
int xanderCommodities(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers || g.players[p].commodities <= 0) return 0;
    for (int k = 0; k < g.numPlayers; ++k) {
        if (!isFaction(g, k, "keleres") || !agentReady(g, k)) continue;
        if (k == p || g.agentFor[k] == p) return g.players[p].commodities;
    }
    return 0;
}

int spendableMixed(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return 0;  // (Thunder's Edge: not for neutral units)
    int total = (g.players[p].tradeGoods + xanderCommodities(g, p)) * tgWorth(g, p);
    for (int pl : readyPlanets(g, p)) total += planetPayValue(g, pl, false, true);
    return total + triadValue(g, p) + custodiaValue(g, p, true) + oceansReady(g, p);
}

int planetPayValue(const Game& g, int planet, bool influence, bool mixed) {
    // Xxekir Grom Omega (Xxcha hero, Codex 0.31.0): once unlocked, a planet the Xxcha exhaust pays its
    // resources and influence together, as either
    const int owner = g.planets[planet].owner;
    // (0.57.1: not with Thunder's Edge's own Xxekir Grom - teXxchaHero)
    if (codexOn(g) && !teXxchaHero(g) && owner >= 0 && isFaction(g, owner, "xxcha") && heroUnlocked(g, owner))
        return planetResources(g, planet) + planetInfluence(g, planet);
    if (mixed) return std::max(planetResources(g, planet), planetInfluence(g, planet));
    // Archon's Gift (Xxcha breakthrough): influence as resources and resources as influence
    if (owner >= 0 && breakthroughIs(g, owner, "xxcha")) return std::max(planetResources(g, planet), planetInfluence(g, planet));
    // Exodriller (Gledge mech, Discordant Stars): 1 of the planet's influence is spent as a resource too
    if (!influence && (g.modules & kModDiscordant) && owner >= 0 && owner < g.numPlayers && isFaction(g, owner, "gledge") && mechAbilities(g) &&
        g.planets[planet].units[size_t(owner)].count(UnitType::Mech) && planetInfluence(g, planet) > 0)
        return planetResources(g, planet) + 1;
    return influence ? planetInfluence(g, planet) : planetResources(g, planet);
}

// ============================================================ units
bool mechAbilities(const Game& g) { return !lawInPlay(g, "articles_war"); }

// Thunder's Edge (0.34.0): neutral units' stats, from the neutral unit reference card
const UnitDef& neutralUnitDef(UnitType t) {
    static UnitDef d[kUnitTypes];
    static bool made = false;
    if (!made) {
        static const char* names[kUnitTypes] = {};
        for (int i = 0; i < kUnitTypes; ++i) {
            for (int u = 0; u < kUnitCount; ++u)  // the generic unit of the type as the base
                if (int(kUnits[u].type) == i && kUnits[u].faction < 0 && kUnits[u].upgradeOf < 0) {
                    d[i] = kUnits[u];
                    break;
                }
            UnitDef& x = d[i];
            x.key = "neutral";
            x.faction = -1;
            x.tech = x.upgradeOf = -1;
            x.combatDice = 1;
            x.afb = x.afbDice = x.bombard = x.bombardDice = x.cannon = x.cannonDice = 0;
            x.deepCannon = x.sustain = x.shield = x.ignoresShield = false;
            x.production = 0;
            x.productionPlusResources = x.spaceOnly = false;
            switch (UnitType(i)) {
                case UnitType::Flagship: x.combat = 7, x.combatDice = 2, x.move = 1, x.capacity = 3, x.sustain = true; break;
                case UnitType::WarSun:
                    x.combat = 3, x.combatDice = 3, x.move = 2, x.capacity = 6, x.sustain = true, x.bombard = 3, x.bombardDice = 3;
                    break;
                case UnitType::Dreadnought:
                    x.combat = 5, x.move = 2, x.capacity = 1, x.sustain = true, x.bombard = 5, x.bombardDice = 1;
                    break;
                case UnitType::Carrier: x.combat = 9, x.move = 2, x.capacity = 6; break;
                case UnitType::Cruiser: x.combat = 6, x.move = 3, x.capacity = 1; break;
                case UnitType::Destroyer: x.combat = 8, x.move = 2, x.capacity = 0, x.afb = 6, x.afbDice = 3; break;
                case UnitType::Fighter: x.combat = 8, x.move = 2; break;
                case UnitType::Mech: x.combat = 6, x.sustain = true; break;
                case UnitType::Infantry: x.combat = 8; break;
                case UnitType::PDS: x.combat = 0, x.shield = true, x.cannon = 6, x.cannonDice = 1; break;
                default: x.combat = 0; break;
            }
            (void)names;
        }
        made = true;
    }
    return d[int(t)];
}

std::string unitLabel(const Game& g, int p, UnitType t, bool plural) {
    if (isFlagship(t) && p >= 0 && p < g.numPlayers && g.players[size_t(p)].unit[int(UnitType::Flagship2)] >= 0)
        return unitDef(g, p, t).name;
    return unitTypeName(t, plural);
}

const UnitDef& unitDef(const Game& g, int p, UnitType t) {
    if (p == kNeutral) return neutralUnitDef(t);
    if (g.players[p].unit[int(t)] < 0) {  // (0.52.0) no unit of the type: the second flagship for all but the Ghemina
        static UnitDef none[kUnitTypes];
        static bool made = false;
        if (!made) {
            for (int i = 0; i < kUnitTypes; ++i) {
                none[i] = UnitDef{};
                none[i].key = "none";
                none[i].name = unitTypeName(UnitType(i));
                none[i].type = UnitType(i);
                none[i].faction = -1;
                none[i].tech = none[i].upgradeOf = -1;
            }
            made = true;
        }
        return none[int(t)];
    }
    const UnitDef& d = kUnits[g.players[p].unit[int(t)]];
    // Sanctification Field (Rhodun, 0.43.0 D38): a silenced unit upgrade keeps its numbers but has no abilities
    if (p == g.fx.blankOwner && g.fx.blankTech >= 0 && d.tech == g.fx.blankTech && g.fixes >= 2) {
        static std::vector<UnitDef> mute(static_cast<size_t>(kUnitCount));
        static std::vector<char> made(static_cast<size_t>(kUnitCount), 0);
        const int i = g.players[p].unit[int(t)];
        if (!made[size_t(i)]) {
            UnitDef x = d;
            x.afb = x.afbDice = x.bombard = x.bombardDice = x.cannon = x.cannonDice = 0;
            x.deepCannon = x.sustain = x.shield = x.ignoresShield = false;
            x.production = 0;
            x.productionPlusResources = false;
            mute[size_t(i)] = x;
            made[size_t(i)] = 1;
        }
        return mute[size_t(i)];
    }
    // Lightrail Ordnance (Thunder's Edge relic): space docks get Space Cannon 5 (x2), able to fire at adjacent systems
    if (t == UnitType::SpaceDock && (g.modules & kModThundersEdge) && relicHeld(g, p, "lightrailordnance")) {
        static std::vector<UnitDef> lr(static_cast<size_t>(kUnitCount));
        static std::vector<char> made(static_cast<size_t>(kUnitCount), 0);
        const int i = g.players[p].unit[int(t)];
        if (!made[size_t(i)]) {
            UnitDef x = d;
            x.cannon = 5;
            x.cannonDice = 2;
            x.deepCannon = true;
            lr[size_t(i)] = x;
            made[size_t(i)] = 1;
        }
        return lr[size_t(i)];
    }
    // The Table's Grace (Mentak breakthrough): with Cruiser II, the cruisers are Corsairs (capacity 2, cost 2)
    if (t == UnitType::Cruiser && d.tech >= 0 && breakthroughIs(g, p, "mentak")) {
        static UnitDef corsair;
        static bool made = false;
        if (!made) {
            corsair = d;
            corsair.name = "Corsair";
            corsair.capacity = 2;
            corsair.combat = 6;
            corsair.move = 3;
            corsair.cost2 = 4;
            made = true;
        }
        return corsair;
    }
    // Absolute Synergy (Naaz-Rokha breakthrough): the mech card flipped - Eidolon Maximum
    if (t == UnitType::Mech && g.players[size_t(p)].eidolonMax && breakthroughIs(g, p, "naaz")) {
        static UnitDef em;
        static bool made = false;
        if (!made) {
            em = d;
            em.name = "Eidolon Maximum";
            em.combat = 4;
            em.combatDice = 4;
            em.move = 3;
            em.sustain = true;
            em.cost2 = 0;  // (game effects can't place or produce it)
            made = true;
        }
        return em;
    }
    if (t != UnitType::Mech || mechAbilities(g)) return d;
    // Articles of War (For): mechs lose their printed abilities except Sustain Damage
    static std::vector<UnitDef> bare(static_cast<size_t>(kUnitCount));
    static std::vector<char> made(static_cast<size_t>(kUnitCount), 0);
    const int i = g.players[p].unit[int(t)];
    if (!made[i]) {
        UnitDef b = d;
        b.afb = b.afbDice = b.bombard = b.bombardDice = b.cannon = b.cannonDice = 0;
        b.deepCannon = b.shield = b.ignoresShield = false;
        b.production = 0;
        b.productionPlusResources = false;
        bare[i] = b;
        made[i] = true;
    }
    return bare[i];
}

int unitLimit(UnitType t) {
    switch (t) {
        case UnitType::Carrier: return 4;
        case UnitType::Cruiser: return 8;
        case UnitType::Destroyer: return 8;
        case UnitType::Dreadnought: return 5;
        case UnitType::Flagship: return 1;
        case UnitType::Flagship2: return 0;  // (0.52.0: only the Ghemina have one - unitLimitOf)
        case UnitType::WarSun: return 2;
        case UnitType::Mech: return 4;
        case UnitType::PDS: return 6;
        case UnitType::SpaceDock: return 3;
        default: return 99;  // fighters and infantry: tokens make them unlimited
    }
}

// Teeming (Cheiran: 2 more dreadnoughts, 1 more mech), Industrialists (Celdauri: 1 more space dock) - Discordant Stars
int unitLimitOf(const Game& g, int p, UnitType t) {
    int n = unitLimit(t);
    if (p < 0 || p >= g.numPlayers || !(g.modules & kModDiscordant)) return n;
    if (isFaction(g, p, "cheiran")) n += t == UnitType::Dreadnought ? 2 : t == UnitType::Mech ? 1 : 0;
    if (isFaction(g, p, "celdauri") && t == UnitType::SpaceDock) n += 1;
    if (isFaction(g, p, "nokar") && t == UnitType::Destroyer) n += 4;  // Private Fleet
    // The Lady & The Lord: two flagships - 0.52.0 (fixes 16): the Lady is her own unit type
    if (isFaction(g, p, "ghemina") && t == (g.fixes >= 16 ? UnitType::Flagship2 : UnitType::Flagship)) n += 1;
    // Trade Protectorate (Celdauri breakthrough, 0.44.4 fixes 8): the Celagrom is a second flagship
    // (0.52.1, fixes 17: the Celagrom is its own unit type, Flagship2)
    if (g.fixes >= 8 && t == (g.fixes >= 17 ? UnitType::Flagship2 : UnitType::Flagship) && isFaction(g, p, "celdauri") && hasBreakthrough(g, p) &&
        (g.fixes < 17 || g.players[size_t(p)].unit[int(UnitType::Flagship2)] >= 0))
        n += 1;
    return n;
}

int unitsOnBoard(const Game& g, int p, UnitType t) {
    int n = 0;
    for (const SystemState& s : g.systems) n += s.space[p].count(t);
    for (const PlanetState& pl : g.planets) n += pl.units[p].count(t);
    return n;
}

int unitsAvailable(const Game& g, int p, UnitType t) {
    if (p == kNeutral) return 99;  // (Thunder's Edge: the neutral reinforcements are not counted)
    if (t == UnitType::Mech && eidolonShip(g, p)) return 0;  // Absolute Synergy (0.44.4): game effects can't place or produce mechs
    if (t == UnitType::WarSun && !owns(g, p, "ws") && unitDef(g, p, t).tech >= 0 &&
        !g.players[p].hasTech(unitDef(g, p, t).tech))
        return 0;
    // (0.55.0, fixes 19: the ships waiting on the Crimson hero's card are not in the reinforcements either)
    const int onCard = g.fixes >= 19 ? g.players[size_t(p)].phantom.count(t) : 0;
    return std::max(0, unitLimitOf(g, p, t) - unitsOnBoard(g, p, t) - onCard - capturedFrom(g, p, t) - g.players[size_t(p)].purgedUnits[int(t)]);
}

int capturedCount(const Game& g, int p, UnitType t) {
    if (p < 0 || p >= g.numPlayers) return 0;  // (Thunder's Edge: not for neutral units)
    int n = 0;
    for (int16_t c : g.players[p].captured)
        if (c % 16 == int(t)) ++n;
    return n;
}

int capturedFrom(const Game& g, int owner, UnitType t) {
    if (t == UnitType::Fighter || t == UnitType::Infantry) return 0;  // tokens: the piece itself went back (LRR 17.3)
    int n = 0;
    for (const Player& pl : g.players)
        for (int16_t c : pl.captured)
            if (c / 16 == owner && c % 16 == int(t)) ++n;
    return n;
}

bool isAnomaly(const Game& g, int s) {
    if (s < 0 || s >= int(g.systems.size())) return false;
    if (g.systems[size_t(s)].d().anomalies) return true;
    if (g.fixes < 2) return false;
    // 0.43.0 (debug passes): systems that the rules make anomalies count for objectives too - a Dimensional Tear
    // (gravity rift), the Nivyn Wound (nebula and gravity rift), the Veldyr home with Aurora Research Base (nebula)
    if (dimensionalTear(g, s)) return true;
    if (g.modules & kModDiscordant) {
        if (s == g.dsWound) return true;
        const int h = g.systems[size_t(s)].home;
        if (h >= 0 && h < g.numPlayers && breakthroughsOn(g) && breakthroughIs(g, h, "veldyr")) return true;
    }
    return false;
}

bool dimensionalTear(const Game& g, int sys) {
    // Dimensional Tear (Vuil'raith space dock): the system is a gravity rift
    if (sys < 0) return false;
    static const int cabal = findFaction("cabal");
    int c = -1;
    for (int q = 0; q < g.numPlayers && c < 0; ++q)
        if (g.players[q].faction == cabal && !g.players[q].eliminated) c = q;
    if (c < 0) return false;
    for (int16_t x : g.systems[sys].planets)
        if (g.planets[x].units[c].count(UnitType::SpaceDock)) return true;
    return false;
}

int groundForcesOn(const Game& g, int p, int planet) {
    const Force& f = g.planets[planet].units[p];
    // Hel-Titan (Titans PDS): also a ground force
    return f.groundForces() + (isFaction(g, p, "titans") ? f.count(UnitType::PDS) : 0);
}

int amalgamationSavings(const Game& g, int p, const int counts[kUnitTypes]) {
    // Amalgamation (Vuil'raith): each captured unit of a type pays for one produced unit of that type
    if (!isFaction(g, p, "cabal") || g.players[p].captured.empty()) return 0;
    int rest[kUnitTypes];
    bool any = false;
    for (int t = 0; t < kUnitTypes; ++t) {
        int free = std::min(counts[t], capturedCount(g, p, UnitType(t)));
        rest[t] = counts[t] - free;
        if (free) any = true;
    }
    if (!any) return 0;
    return productionCost(g, p, counts) - productionCost(g, p, rest);
}

int edictTokensOf(const Game& g, int q) {
    int n = 0;
    for (const Player& pl : g.players)
        if ((pl.edict >> q) & 1) ++n;
    return n;
}

bool argentMechFree(const Game& g, int p, const Force& f) {
    // Aerie Sentinel (Argent mech): needs no capacity next to a ship with a capacity value
    if (!f.count(UnitType::Mech) || !isFaction(g, p, "argent") || !mechAbilities(g)) return false;
    for (int t : kShipIdx)
        if (f.n[t] && unitDef(g, p, UnitType(t)).capacity > 0) return true;
    return false;
}

int forceCapacity(const Game& g, int p, const Force& f) {
    int cap = 0;
    for (int t : kShipIdx)
        if (f.n[t]) cap += f.n[t] * unitDef(g, p, UnitType(t)).capacity;
    // Saar floating factories sit in space and carry like ships (LRR 16.1)
    const UnitDef& dock = unitDef(g, p, UnitType::SpaceDock);
    if (dock.spaceOnly) cap += f.count(UnitType::SpaceDock) * dock.capacity;
    const UnitDef& pds = unitDef(g, p, UnitType::PDS);  // Gauss Cannon II (Mirveda, Discordant Stars): capacity 1
    if (pds.spaceOnly) cap += f.count(UnitType::PDS) * pds.capacity;
    if (g.dsCapBonus == p && cap > 0) {  // Strike Team Coordinator (Vaylerian commander): +2 this action
        // 0.43.4 (owner, D7: exactly as the card - 1 ship of your choice): only a group with that ship in it
        if (g.fixes < 5) cap += 2;
        else if (g.fx.dsPyndilType >= 0 && f.n[size_t(g.fx.dsPyndilType)] > 0) cap += 2;
    }
    // Raid Leaders (the Ghemina's note): one of the holder's ships has capacity 2 this action
    if (g.fx.dsRaidOwner == p && g.fx.dsRaidType >= 0 && f.n[g.fx.dsRaidType] > 0)
        cap += std::max(0, 2 - int(unitDef(g, p, UnitType(g.fx.dsRaidType)).capacity));
    return cap;
}
// The Lady & The Lord (Ghemina, Discordant Stars): both flagships have the Lord's capacity (7) in the unit data; the Lady
// has 3. Where she is known to be (both together, or her system), 4 less. A flagship on its way is taken as the Lord.
static int ladyCut(const Game& g, int p, int sys, const Force& f) {
    if (!(g.modules & kModDiscordant) || g.fixes >= 16 || p < 0 || p >= g.numPlayers || !f.count(UnitType::Flagship) || !isFaction(g, p, "ghemina"))
        return 0;  // (0.52.0, fixes 16: the Lady is her own unit with her own capacity)
    return f.count(UnitType::Flagship) >= 2 || (sys >= 0 && sys == g.dsLadySys) ? 4 : 0;
}

// Raider I (Vaylerian cruiser, Discordant Stars): it carries ground forces only - the capacity fighters can't use
static int raiderCapacity(const Game& g, int p, const Force& f) {
    if (!(g.modules & kModDiscordant) || !f.count(UnitType::Cruiser)) return 0;
    const UnitDef& d = unitDef(g, p, UnitType::Cruiser);
    return std::strcmp(d.key, "vaylerian_cruiser") ? 0 : f.count(UnitType::Cruiser) * d.capacity;
}

bool movesAsShip(const Game& g, int p, UnitType t) {
    if (t == UnitType::Mech && eidolonShip(g, p)) return true;  // (Eidolon Maximum, 0.44.4)
    return isShip(t) || ((t == UnitType::SpaceDock || t == UnitType::PDS) && unitDef(g, p, t).spaceOnly);  // (Saar docks, Mirveda Gauss Cannons)
}

int shipCapacity(const Game& g, int p, int sys) { return forceCapacity(g, p, g.systems[sys].space[p]) - ladyCut(g, p, sys, g.systems[sys].space[p]); }

int dockFighterSlots(const Game& g, int p, int sys) {
    // Space docks on planets let a few fighters stay without ship capacity.
    const UnitDef& dock = unitDef(g, p, UnitType::SpaceDock);
    if (dock.spaceOnly) return 0;
    int per = dock.capacity;
    if (!std::strcmp(dock.key, "cabal_spacedock")) per = 6;   // Dimensional Tear I
    if (!std::strcmp(dock.key, "cabal_spacedock2")) per = 12; // Dimensional Tear II
    int slots = 0;
    for (int16_t pl : g.systems[sys].planets) slots += g.planets[pl].units[p].count(UnitType::SpaceDock) * per;
    return slots;
}

// Stabilization Arrays (Mirveda breakthrough; 0.44.9, fixes 13 - the owner's answer B2): the Eye of the Curator
// fighters take half a slot of capacity each (transporting them, and fitting them in a space area)
bool curatorHalf(const Game& g, int p) {
    return g.fixes >= 13 && p >= 0 && p < g.numPlayers && !std::strcmp(unitDef(g, p, UnitType::Fighter).key, "mirveda_fighter3");
}
int fighterSlots(const Game& g, int p, int fighters) { return curatorHalf(g, p) ? (fighters + 1) / 2 : fighters; }

int transportedInSpace(const Game& g, int p, int sys) {
    const Force& f = g.systems[sys].space[p];
    return fighterSlots(g, p, f.count(UnitType::Fighter)) + f.groundForces();
}

int capacityOverflow(const Game& g, int p, int sys) { return overflowOf(g, p, sys, g.systems[sys].space[p]); }

// Fighter II: fighters that can move on their own don't need capacity; those beyond
// the ships' capacity count against the fleet pool instead (owner OK'd, RULES-CHECK 0.7.0 #3).
bool fightersFree(const Game& g, int p) { return unitDef(g, p, UnitType::Fighter).move > 0; }

int overflowOf(const Game& g, int p, int sys, const Force& f) {
    // Flotilla (Dih-Mohn, Discordant Stars): no more infantry than non-fighter ships in a space area
    if ((g.modules & kModDiscordant) && p >= 0 && p < g.numPlayers && isFaction(g, p, "dihmohn") && f.count(UnitType::Infantry) > f.nonFighterShips()) {
        Force fewer = f;
        const int extra = f.count(UnitType::Infantry) - f.nonFighterShips();
        fewer.n[int(UnitType::Infantry)] = uint8_t(f.nonFighterShips());
        return extra + overflowOf(g, p, sys, fewer);
    }
    const int cap = forceCapacity(g, p, f) - ladyCut(g, p, sys, f);
    const int gf = f.groundForces() - (argentMechFree(g, p, f) || eidolonShip(g, p) ? f.count(UnitType::Mech) : 0);  // (Eidolon Maximum: a ship)
    // Age of Fighters (galactic event): fighters that count against the fleet pool carry 1 ground force each
    if (fightersFree(g, p)) return std::max(0, gf - cap - (eventOn(g, kEvFighters) ? f.count(UnitType::Fighter) : 0));
    int fighters = std::max(0, f.count(UnitType::Fighter) - dockFighterSlots(g, p, sys));
    if (const int rc = raiderCapacity(g, p, f))  // fighters only fit the other ships' capacity
        return std::max({0, fighters + gf - cap, fighters - (cap - rc)});
    return std::max(0, fighters + gf - cap);
}

int excessFighters(const Game& g, int p, int sys, const Force& f) {
    if (!fightersFree(g, p)) return 0;
    const int cap = forceCapacity(g, p, f) - ladyCut(g, p, sys, f);
    const int gf = f.groundForces() - (argentMechFree(g, p, f) || eidolonShip(g, p) ? f.count(UnitType::Mech) : 0);
    int fighters = std::max(0, f.count(UnitType::Fighter) - dockFighterSlots(g, p, sys));
    if (const int rc = raiderCapacity(g, p, f)) return std::max(0, fighters - std::max(0, (cap - rc) - std::max(0, gf - rc)));
    if (curatorHalf(g, p)) return std::max(0, fighters - 2 * std::max(0, cap - gf));  // (two to a slot)
    return std::max(0, fighters - std::max(0, cap - gf));
}

int fleetUse(const Game& g, int p, int sys, const Force& f) {
    int n = f.nonFighterShips() + (eidolonShip(g, p) ? f.count(UnitType::Mech) : 0);  // (Eidolon Maximum, 0.44.4)
    // Networked Command (Ghoti, Discordant Stars): up to 3 ships in their home system don't count
    if ((g.modules & kModDiscordant) && p >= 0 && p < g.numPlayers && sys == g.players[size_t(p)].homeSystem && owns(g, p, "dsghotg"))
        n = std::max(0, n - 3);
    // Capital Fleet (Dih-Mohn, Discordant Stars): destroyers count as half a ship (rounded up)
    if ((g.modules & kModDiscordant) && p >= 0 && p < g.numPlayers && isFaction(g, p, "dihmohn") && f.count(UnitType::Destroyer))
        n = n - f.count(UnitType::Destroyer) + (f.count(UnitType::Destroyer) + 1) / 2;
    int extra = excessFighters(g, p, sys, f);
    if (!extra) return n;
    // Hybrid Crystal Fighter II (Naalu): each counts as half a ship, rounded up
    // (Eye of the Curator, 0.43.0 - 0.44.9 fixes 13: half a slot of capacity instead, a whole ship in the fleet pool)
    if (!std::strcmp(unitDef(g, p, UnitType::Fighter).key, "naalu_fighter2") ||
        (!std::strcmp(unitDef(g, p, UnitType::Fighter).key, "mirveda_fighter3") && !curatorHalf(g, p)))
        extra = (extra + 1) / 2;
    return n + extra;
}

int fleetUseIn(const Game& g, int p, int sys) { return fleetUse(g, p, sys, g.systems[sys].space[p]); }

// Discordant Stars: Bombardment 5 dice a unit type has from elsewhere - the Lady (Ghemina flagship, x4), Raid Leaders (x2)
int dsBombardDiceOf(const Game& g, int p, int sys, UnitType t) {
    if (!(g.modules & kModDiscordant) || p < 0 || p >= g.numPlayers || sys < 0) return 0;
    int n = 0;
    if (t == UnitType::Flagship && sys == g.dsLadySys && g.fixes < 16 && isFaction(g, p, "ghemina") && g.systems[size_t(sys)].space[size_t(p)].count(t))
        n += 4;  // (0.52.0, fixes 16: the Lady's own unit has Bombardment 5 x4)
    if (g.fx.dsRaidOwner == p && int(t) == g.fx.dsRaidType && g.systems[size_t(sys)].space[size_t(p)].count(t)) n += 2;
    return n;
}

int bombardAvailable(const Game& g, int p, int sys, UnitType t) {
    int n = g.systems[sys].space[p].count(t);
    // Annihilator (L1Z1X mech): mechs on planets in the system bombard as if they were ships
    if (t == UnitType::Mech && !std::strcmp(unitDef(g, p, t).key, "l1z1x_mech") && isFaction(g, p, "l1z1x") && mechAbilities(g))
        for (int16_t x : g.systems[sys].planets) n += g.planets[x].units[p].count(t);
    return n;
}

bool hasShips(const Game& g, int p, int sys) {
    return g.systems[sys].space[p].ships() > 0 || (g.systems[sys].space[p].count(UnitType::Mech) > 0 && eidolonShip(g, p));  // (Eidolon Maximum)
}

bool hasUnits(const Game& g, int p, int sys) {
    const SystemState& s = g.systems[sys];
    if (!s.space[p].empty()) return true;
    for (int16_t pl : s.planets)
        if (!g.planets[pl].units[p].empty()) return true;
    return false;
}

// (fixes 25, owner's faction debug pass) "Coexisting structures are always blockaded" (TE rulebook): p's only production
// in the system is structures on planets where p coexists
static bool coexistBlockaded(const Game& g, int p, int sys) {
    if (g.fixes < 25 || p < 0 || p >= g.numPlayers || sys < 0 || !(g.modules & kModThundersEdge)) return false;
    const SystemState& s = g.systems[size_t(sys)];
    auto produces = [&](int t) { const UnitDef& u = unitDef(g, p, UnitType(t)); return u.production > 0 || u.productionPlusResources; };
    bool coexistDock = false;
    for (int t = 0; t < kUnitTypes; ++t)
        if (s.space[size_t(p)].n[t] && produces(t)) return false;
    for (int16_t x : s.planets) {
        const PlanetState& ps = g.planets[size_t(x)];
        const bool co = (ps.coexist >> p) & 1;
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!ps.units[size_t(p)].n[t] || !produces(t)) continue;
            if (co && isStructure(UnitType(t))) coexistDock = true;
            else return false;
        }
    }
    return coexistDock;
}
bool blockaded(const Game& g, int p, int sys) { return (otherShipsIn(g, p, sys) && !hasShips(g, p, sys)) || coexistBlockaded(g, p, sys); }

bool otherShipsIn(const Game& g, int p, int sys) {
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && hasShips(g, q, sys)) return true;
    return p != kNeutral && hasShips(g, kNeutral, sys);  // (neutral ships count as another player's)
}

// Thunder's Edge (0.34.0): neutral units
bool neutralShipsIn(const Game& g, int sys) { return sys >= 0 && hasShips(g, kNeutral, sys); }
bool neutralUnitsIn(const Game& g, int sys) { return sys >= 0 && hasUnits(g, kNeutral, sys); }
bool neutralOn(const Game& g, int planet) { return planet >= 0 && !g.planets[size_t(planet)].units[kNeutral].empty(); }
int neutralGroundOn(const Game& g, int planet) { return planet >= 0 ? g.planets[size_t(planet)].units[kNeutral].groundForces() : 0; }
bool anyNeutralUnits(const Game& g) {
    for (const SystemState& s : g.systems)
        if (!s.space[kNeutral].empty()) return true;
    for (const PlanetState& p : g.planets)
        if (!p.units[kNeutral].empty()) return true;
    return false;
}

std::vector<int> playersWithShips(const Game& g, int sys) {
    std::vector<int> v;
    for (int q = 0; q < g.numPlayers; ++q)
        if (hasShips(g, q, sys)) v.push_back(q);
    return v;
}

int productionCapacity(const Game& g, int p, int sys) {
    const SystemState& s = g.systems[sys];
    int cap = 0;
    if (noAbilities(g, p, sys)) return 0;  // Thunder's Edge: no Production inside an entropic scar (or under the Quietus)
    int accretion = relicHeld(g, p, "boon_of_the_cerulean_god") ? 1 : 0;  // Accretion Engine (Uncharted Space)
    if (eventOn(g, kEvCosmic)) {  // Cosmic Phenomenae (galactic event): +1 in or next to a supernova
        bool nova = (s.d().anomalies & kSupernova) != 0;
        for (int16_t a : s.adj) nova = nova || (g.systems[size_t(a)].d().anomalies & kSupernova);
        if (nova) ++accretion;
    }
    auto add = [&](const Force& f, int planet) {
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!f.n[t]) continue;
            const UnitDef& d = unitDef(g, p, UnitType(t));
            if (d.production <= 0) continue;
            if (planet < 0 && isStructure(UnitType(t)) && !d.spaceOnly) continue;  // (Miniaturization: carried docks do nothing)
            int per = d.production + (d.productionPlusResources && planet >= 0 ? planetResources(g, planet) : 0) + accretion;
            if (planet >= 0 && d.productionPlusResources && !std::strncmp(d.key, "celdauri_spacedock", 18))  // Trade Port: or influence
                per = d.production + std::max(planetResources(g, planet), planetInfluence(g, planet)) + accretion;
            cap += per * f.n[t];
        }
    };
    add(s.space[p], -1);
    for (int16_t pl : s.planets) add(g.planets[pl].units[p], pl);
    // Orbital Foundries (Uncharted Space): the planet has Production 2 as if it were a unit
    for (int16_t pl : s.planets)
        if (g.planets[pl].owner == p && hasAttachment(g, pl, "orbitalfoundries")) cap += 2 + accretion;
    // Automatons (the Roh'Dhna's note, Discordant Stars): Production 3 on its planet
    for (int16_t pl : s.planets)
        if (pl == g.dsAutomatons && g.planets[pl].owner == p) cap += 3 + accretion;
    if (magmusProduction(g, p, sys)) cap += 5;
    if (g.modules & kModDiscordant) {
        // Voldun (Gledge commander): +1 for each space dock they control, when their units use Production
        if (cap > 0 && commanderUsable(g, p, "gledge")) cap += unitsOnBoard(g, p, UnitType::SpaceDock);
        // All Mother (Ghoti flagship): a space dock with Production equal to their fleet tokens
        if (isFaction(g, p, "ghoti") && flagshipAbility(g, p, "ghoti") && s.space[size_t(p)].count(UnitType::Flagship))
            cap += g.players[size_t(p)].fleet + accretion;
        // Irrefutable Offer (Starsails, Nokar hero): this round each of their planets has Production 3
        if (isFaction(g, p, "nokar") && g.players[size_t(p)].heroRound == g.round)
            for (int16_t pl : s.planets)
                if (g.planets[pl].owner == p) cap += 3 + accretion;
        // Forgetender (Axis mech): a space dock on its planet has Production 5
        if (isFaction(g, p, "axis") && mechAbilities(g))
            for (int16_t pl : s.planets) {
                const Force& f = g.planets[pl].units[size_t(p)];
                if (!f.count(UnitType::SpaceDock) || !f.count(UnitType::Mech)) continue;
                const UnitDef& d = unitDef(g, p, UnitType::SpaceDock);
                const int per = d.production + (d.productionPlusResources ? planetResources(g, pl) : 0);
                cap += std::max(0, 5 - per);
            }
        // Fabrication Grid (Mortheus): a system with their ships and no planets has Production 2
        if (s.planets.empty() && owns(g, p, "dsmorty") && s.space[size_t(p)].ships() > 0) cap += 2 + accretion;
        // Plunder (Olradin, "-" side of The Environment): one of their producing units makes 2 less
        if (cap > 0 && isFaction(g, p, "olradin") && ((g.players[size_t(p)].dsPolicy >> 1) & 1)) cap = std::max(0, cap - 2);
        // Broker Network (Bentor): their planets with a specialty or an attachment have Production 1
        if (owns(g, p, "dsbentg"))
            for (int16_t pl : s.planets)
                if (g.planets[pl].owner == p && (planetHasSkip(g, pl) || !g.planets[pl].attachments.empty() || pl == g.dsKeyPlanet || pl == g.dsBasePlanet))
                    cap += 1 + accretion;
    }
    // Aerie Hololattice (Argent): planets with your structures have Production 1
    if (owns(g, p, "ah"))
        for (int16_t pl : s.planets)
            if (g.planets[pl].units[p].structures()) cap += 1;
    cap += particleSynthesis(g, p, sys);
    if (custodiaMecatol(g, p) && sys == g.mecatol()) cap += 3;  // Custodian's Favour (Keleres breakthrough)
    // Supremacy (Celdauri flagship, Discordant Stars): other players' space docks here produce for the Celdauri
    if ((g.modules & kModDiscordant) && flagshipAbility(g, p, "celdauri") && flagshipIn(g, p, sys))
        for (int16_t pl : s.planets)
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && g.planets[pl].units[q].count(UnitType::SpaceDock)) {
                    const UnitDef& d = unitDef(g, q, UnitType::SpaceDock);
                    cap += g.planets[pl].units[q].count(UnitType::SpaceDock) * (d.production + (d.productionPlusResources ? planetResources(g, pl) : 0));
                }
    return cap;
}

// The PRODUCTION of a single unit (Local Contracts, 0.44.5): the best one in the system - a space dock (its planet's
// resources), a ship, or the planets that produce as if they were units (Orbital Foundries, Automatons, ...)
int singleUnitProduction(const Game& g, int p, int sys) {
    const int all = productionCapacity(g, p, sys);
    if (all <= 0) return 0;
    const SystemState& s = g.systems[size_t(sys)];
    int accretion = relicHeld(g, p, "boon_of_the_cerulean_god") ? 1 : 0;
    if (eventOn(g, kEvCosmic)) {
        bool nova = (s.d().anomalies & kSupernova) != 0;
        for (int16_t a : s.adj) nova = nova || (g.systems[size_t(a)].d().anomalies & kSupernova);
        if (nova) ++accretion;
    }
    int best = 0;
    auto unitsOf = [&](const Force& f, int planet) {
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!f.n[t]) continue;
            const UnitDef& d = unitDef(g, p, UnitType(t));
            if (d.production <= 0) continue;
            if (planet < 0 && isStructure(UnitType(t)) && !d.spaceOnly) continue;
            int per = d.production + (d.productionPlusResources && planet >= 0 ? planetResources(g, planet) : 0) + accretion;
            if (planet >= 0 && d.productionPlusResources && !std::strncmp(d.key, "celdauri_spacedock", 18))
                per = d.production + std::max(planetResources(g, planet), planetInfluence(g, planet)) + accretion;
            if (planet >= 0 && t == int(UnitType::SpaceDock) && (g.modules & kModDiscordant) && isFaction(g, p, "axis") && mechAbilities(g) &&
                f.count(UnitType::Mech))
                per = std::max(per, 5);  // Forgetender
            best = std::max(best, per);
        }
    };
    unitsOf(s.space[size_t(p)], -1);
    for (int16_t pl : s.planets) {
        unitsOf(g.planets[size_t(pl)].units[size_t(p)], pl);
        if (g.planets[size_t(pl)].owner != p) continue;
        if (hasAttachment(g, pl, "orbitalfoundries")) best = std::max(best, 2 + accretion);
        if (pl == g.dsAutomatons) best = std::max(best, 3 + accretion);
        if ((g.modules & kModDiscordant) && isFaction(g, p, "nokar") && g.players[size_t(p)].heroRound == g.round) best = std::max(best, 3 + accretion);
    }
    if (magmusProduction(g, p, sys)) best = std::max(best, 5);
    if ((g.modules & kModDiscordant) && isFaction(g, p, "ghoti") && flagshipAbility(g, p, "ghoti") && s.space[size_t(p)].count(UnitType::Flagship))
        best = std::max(best, g.players[size_t(p)].fleet + accretion);
    if ((g.modules & kModDiscordant) && s.planets.empty() && owns(g, p, "dsmorty") && s.space[size_t(p)].ships() > 0) best = std::max(best, 2 + accretion);
    if (custodiaMecatol(g, p) && sys == g.mecatol()) best = std::max(best, 3);
    if (best <= 0) best = 1;  // (Broker Network, Aerie Hololattice: Production 1)
    if ((g.modules & kModDiscordant) && commanderUsable(g, p, "gledge")) best += unitsOnBoard(g, p, UnitType::SpaceDock);  // Voldun
    if ((g.modules & kModDiscordant) && isFaction(g, p, "olradin") && ((g.players[size_t(p)].dsPolicy >> 1) & 1)) best = std::max(0, best - 2);  // Plunder
    return std::min(best, all);
}

// Custodian's Favour (Custodia Vigilia, Keleres breakthrough): while they control Mecatol Rex it has Space Cannon 5
// and Production 3.
bool custodiaMecatol(const Game& g, int p) {
    if (!breakthroughIs(g, p, "keleres") || !g.players[size_t(p)].custodia) return false;
    const int m = g.mecatol();
    if (m < 0) return false;
    for (int16_t x : g.systems[size_t(m)].planets)
        if (g.planets[size_t(x)].owner == p && !std::strcmp(kPlanets[g.planets[size_t(x)].def].key, "mr")) return true;
    return false;
}

bool arcaneShield(const Game& g, int planet) {
    if (planet < 0 || planet >= int(g.planets.size())) return false;
    for (int16_t a : g.planets[size_t(planet)].attachments)
        if (a >= 0 && a < kExploreCardCount && !std::strcmp(kExploreCards[a].key, "arcaneshieldemitter")) return true;
    return false;
}

// The Custodia Vigilia planet card (Keleres breakthrough): 2 resources, 3 influence, spent like a planet card.
int custodiaValue(const Game& g, int p, bool influence) {
    if (p < 0 || p >= g.numPlayers || !breakthroughIs(g, p, "keleres")) return 0;
    const Player& pl = g.players[size_t(p)];
    return pl.custodia && !pl.custodiaExhausted ? (influence ? 3 : 2) : 0;
}

// Valefar Assimilator Z (Nekro breakthrough): p's flagship has this faction's flagship abilities (their own, or
// through a Z token on that faction).
bool flagshipAbility(const Game& g, int p, const char* factionKey) {
    if (isFaction(g, p, factionKey)) return true;
    if (p < 0 || p >= g.numPlayers || g.players[size_t(p)].nekroZ.empty() || !breakthroughIs(g, p, "nekro")) return false;
    for (int8_t f : g.players[size_t(p)].nekroZ)
        if (f >= 0 && f < kFactionCount && !std::strcmp(kFactions[f].key, factionKey)) return true;
    return false;
}

// Void Tether (Empyrean breakthrough): other players don't treat the two systems of a tethered border as adjacent.
bool tethered(const Game& g, int p, int a, int b) {
    if (g.tethers.empty()) return false;
    for (const auto& t : g.tethers)
        if ((t.first == a && t.second == b) || (t.first == b && t.second == a)) {
            for (int q = 0; q < g.numPlayers; ++q)
                if (isFaction(g, q, "empyrean") && q == p) return false;  // (their own borders)
            // (fixes 26, the Empyrean review - owner 2026-11-12: the card) "... unless you allow it"
            if (g.fixes >= 26 && p >= 0 && p < g.numPlayers && ((g.fx.tetherAllow >> p) & 1)) return false;
            return true;
        }
    return false;
}

// (fixes 24) the wormholes a system contains, each one counted: printed (an open nexus all three - Nexus Sovereignty
// only matters during movement), wormhole tokens, each Creuss token, each delta-wormhole flagship (Hil Colish)
int wormholeCount(const Game& g, int sys) {
    if (sys < 0 || sys >= int(g.systems.size())) return 0;
    const SystemState& s = g.systems[size_t(sys)];
    uint8_t printed = s.d().wormholes;
    if (sys == g.nexus && !s.nexusActive) printed &= kGamma;
    int n = 0;
    for (uint8_t w = printed; w; w &= uint8_t(w - 1)) ++n;
    for (uint8_t w = s.tokenWormholes; w; w &= uint8_t(w - 1)) ++n;
    for (int16_t t : g.creussTokens) n += t == sys ? 1 : 0;
    const int c = playerOfFaction(g, "ghost");
    for (int q = 0; q < g.numPlayers; ++q)
        if (s.space[size_t(q)].count(UnitType::Flagship) && (q == c || (g.fixes >= 13 && c >= 0 && flagshipAbility(g, q, "ghost")))) ++n;
    return n;
}
int particleSynthesis(const Game& g, int p, int sys) {
    // Particle Synthesis (Creuss breakthrough): each wormhole in a system with your ships has Production 1
    // (0.44.4: the faction key is "ghost" - "creuss" never matched, so it only works from fixes 8)
    if (g.fixes < 8 || !breakthroughIs(g, p, "ghost") || !hasShips(g, p, sys)) return 0;
    if (g.fixes >= 24) return wormholeCount(g, sys);  // (owner's faction debug pass: "each wormhole", not each kind)
    int n = 0;
    for (uint8_t w = wormholesOf(g, sys); w; w &= uint8_t(w - 1)) ++n;
    return n;
}

bool eventOn(const Game& g, int ev) { return (g.modules & kModThundersEdge) && ev >= 0 && ev < 32 && ((g.events >> ev) & 1); }
int commodityCap(const Game& g, const Player& pl) { return eventOn(g, kEvCommerce) ? 99 : commodityValue(g, pl); }
int refreshTo(const Game& g, const Player& pl) {
    return eventOn(g, kEvCommerce) ? std::min(99, pl.commodities + commodityValue(g, pl)) : commodityValue(g, pl);
}

// The Firmament's plot cards (our own wording)
const char* plotName(int plot) {
    static const char* const k[kPlotCount] = {"Enervate", "Siphon", "Seethe", "Assail", "Extract"};
    return plot >= 0 && plot < kPlotCount ? k[plot] : "";
}
const char* plotText(int plot) {
    static const char* const k[kPlotCount] = {
        "You can perform the secondary abilities of the puppeted player's strategy cards without spending command tokens; for \"Leadership,\" you can perform the primary ability instead.",
        "When the puppeted player gains commodities, you gain an equal number of trade goods.",
        "When this card is revealed, destroy all units on a non-home planet controlled by the puppeted player. At the start of the status phase, destroy 1 of the puppeted player's infantry in any system.",
        "Apply +1 to the results of each of your combat and unit ability rolls against the puppeted player.",
        "When this card is revealed, gain 1 non-faction technology owned by the puppeted player. When the puppeted player gains a non-faction technology, you may spend 4 resources to gain that technology."};
    return plot >= 0 && plot < kPlotCount ? k[plot] : "";
}
int plotCount(const Player& pl) {
    int n = 0;
    for (int k = 0; k < kPlotCount; ++k) n += (pl.plotsIn >> k) & 1;
    return n;
}
int obsidianOf(const Game& g) { return (g.modules & kModThundersEdge) ? playerOfFaction(g, "obsidian") : -1; }
bool plotPuppet(const Game& g, int obs, int plot, int q) {
    if (obs < 0 || obs >= g.numPlayers || q < 0 || q >= g.numPlayers || obs == q || plot < 0 || plot >= kPlotCount) return false;
    const Player& o = g.players[size_t(obs)];
    return ((o.plotsIn >> plot) & 1) && ((o.plotTok[size_t(plot)] >> q) & 1) && isFaction(g, obs, "obsidian") && !o.eliminated;
}
bool anyPuppet(const Game& g, int obs, int q) {
    if (obs < 0 || obs >= g.numPlayers || q < 0 || q >= g.numPlayers || obs == q) return false;
    const Player& o = g.players[size_t(obs)];
    for (int k = 0; k < kPlotCount; ++k)
        if (((o.plotsIn >> k) & 1) && ((o.plotTok[size_t(k)] >> q) & 1)) return true;
    return false;
}

bool magmusProduction(const Game& g, int p, int sys) {
    // Magmus Reactor: a supernova with your units has Production 5
    return !magmusOriginal(g) && (g.systems[sys].d().anomalies & kSupernova) && owns(g, p, "mr") && hasUnits(g, p, sys);
}

bool magmusOriginal(const Game& g) { return g.fixes >= 26 && !codexOn(g); }

bool anyPlanetGFFor(const Game& g, int p) { return g.fx.anyPlanetGF && (p == g.activePlayer || (g.fixes >= 26 && p == g.fx.anyPlanetGFFor)); }

int warSunUpgradeFor(const Game& g, int p) {
    const int ws = techByKey("ws");
    if (g.fixes >= 26 && p >= 0 && p < g.numPlayers)
        for (int i = 0; i < kTechCount; ++i)
            if (kTechs[i].faction >= 0 && kTechs[i].faction == g.players[size_t(p)].faction && kTechs[i].replaces == ws) return i;
    return ws;
}

bool hasProductionIn(const Game& g, int p, int sys) { return productionCapacity(g, p, sys) > 0; }

std::vector<int> groundForceDestinations(const Game& g, int p, int sys) {
    std::vector<int> v;
    const SystemState& s = g.systems[sys];
    bool spaceProducer = magmusProduction(g, p, sys);
    for (int t = 0; t < kUnitTypes; ++t)
        if (s.space[p].n[t] && unitDef(g, p, UnitType(t)).production > 0 &&
            !(isStructure(UnitType(t)) && !unitDef(g, p, UnitType(t)).spaceOnly))  // (Miniaturization)
            spaceProducer = true;
    // Letani Miasmiala / Duha Menaimon: any planet you control in the system, or space
    if (anyPlanetGFFor(g, p)) spaceProducer = true;
    if (particleSynthesis(g, p, sys)) spaceProducer = true;  // (the wormholes are in space)
    if ((g.modules & kModDiscordant) && s.planets.empty() && owns(g, p, "dsmorty") && s.space[size_t(p)].ships() > 0) spaceProducer = true;  // Fabrication Grid
    if ((g.modules & kModDiscordant) && isFaction(g, p, "ghoti") && s.space[size_t(p)].count(UnitType::Flagship)) spaceProducer = true;  // All Mother
    for (int16_t pl : s.planets) {
        if (g.planets[pl].owner != p || isDMZ(g, pl)) continue;
        bool producer = owns(g, p, "ah") && g.planets[pl].units[p].structures() > 0;  // Aerie Hololattice
        if (hasAttachment(g, pl, "orbitalfoundries")) producer = true;                 // Orbital Foundries
        if ((g.modules & kModDiscordant) && owns(g, p, "dsbentg") &&                   // Broker Network (Bentor)
            (planetHasSkip(g, pl) || !g.planets[pl].attachments.empty() || pl == g.dsKeyPlanet || pl == g.dsBasePlanet))
            producer = true;
        for (int t = 0; t < kUnitTypes; ++t)
            if (g.planets[pl].units[p].n[t] && unitDef(g, p, UnitType(t)).production > 0) producer = true;
        if (producer || spaceProducer) v.push_back(pl);  // LRR 68.3 / 68.4
    }
    if (spaceProducer) v.push_back(-1);
    return v;
}

bool controlsHomePlanets(const Game& g, int p) {
    // Mobile Command (Ghoti, owner D132, fixes >= 2): their home is wherever their flagship is - with the flagship
    // off the board they have no home system, as if it were taken: no public objectives until it is back
    if (g.fixes >= 2 && (g.modules & kModDiscordant) && p >= 0 && p < g.numPlayers && isFaction(g, p, "ghoti")) {
        bool flag = false;
        for (const SystemState& s : g.systems) flag = flag || s.space[size_t(p)].count(UnitType::Flagship) > 0;
        if (!flag) return false;
    }
    int h = g.players[p].homeSystem;
    if (h < 0) return true;
    for (int16_t pl : g.systems[h].planets)
        if (g.planets[pl].owner != p && !isStation(g, pl)) return false;  // (a space station isn't part of it)
    return true;
}

bool shouldBeEliminated(const Game& g, int p) {
    if (g.players[p].eliminated) return false;
    // Conventions of War Abandoned (galactic event): every planet of your home system purged
    const int home = g.players[size_t(p)].homeSystem;
    if (eventOn(g, kEvConventions) && home >= 0 && g.systems[size_t(home)].planets.empty() && g.systems[size_t(home)].d().planetCount > 0)
        return true;
    for (const PlanetState& pl : g.planets) {
        if (pl.owner == p) return false;
        if (pl.units[p].groundForces() > 0) return false;
        for (int t = 0; t < kUnitTypes; ++t)
            if (pl.units[p].n[t] && unitDef(g, p, UnitType(t)).production > 0) return false;
    }
    for (const SystemState& s : g.systems) {
        if (s.space[p].groundForces() > 0) return false;
        for (int t = 0; t < kUnitTypes; ++t)
            if (s.space[p].n[t] && unitDef(g, p, UnitType(t)).production > 0) return false;
    }
    return true;
}

int fleetLimit(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return 99;  // (Thunder's Edge: not for neutral units)
    if (heroActive(g, p, "letnev")) return 99;  // Darktalon Treilla
    int edict = 0;  // Edict (Mahact): other players' tokens in the fleet pool count too
    for (int q = 0; q < g.numPlayers; ++q)
        if ((g.players[p].edict >> q) & 1) ++edict;
    return g.players[p].fleet + edict + (isFaction(g, p, "letnev") ? 2 : 0);  // Armada
}

int unitCost2(const Game& g, int p, UnitType t) {
    int c = unitDef(g, p, t).cost2;
    if (c == 1 && lawInPlay(g, "conscription")) c = 2;  // Regulated Conscription: one unit for the cost
    return c;
}

// The units' printed cost added up (no Sarween Tools / Accretion Engine) - for "a combined cost of X" limits (Maximus,
// Matriphagy; 0.43.0 with fixes >= 2, older games used productionCost)
int unitsCostValue(const Game& g, int p, const int counts[kUnitTypes]) {
    if (g.fixes < 2) return productionCost(g, p, counts);
    int cost = 0;
    for (int t = 0; t < kUnitTypes; ++t)
        if (counts[t] > 0) cost += (counts[t] * unitCost2(g, p, UnitType(t)) + 1) / 2;
    return cost;
}
int productionCost(const Game& g, int p, const int counts[kUnitTypes]) {
    int cost = 0, any = 0;
    const bool feng = counts[int(UnitType::Flagship)] > 0 && commanderUsable(g, p, "nomad");  // Navarch Feng
    for (int t = 0; t < kUnitTypes; ++t) {
        if (counts[t] <= 0) continue;
        any += counts[t];
        if (feng && UnitType(t) == UnitType::Flagship) continue;
        cost += (counts[t] * unitCost2(g, p, UnitType(t)) + 1) / 2;
    }
    if (any && owns(g, p, "st") && !(g.fixes >= 9 && g.fx.noSarween)) cost = std::max(0, cost - 1);
    if (any && relicHeld(g, p, "boon_of_the_cerulean_god")) cost = std::max(0, cost - 1);  // Accretion Engine
    return cost;
}

// Terrafactory (Roh'Dhna war suns, 0.43.0 D45): a war sun produces only 1 fighter or infantry for their cost - when
// they are the only producers in the system, what that costs on top of the usual 2-for-1
int terrafactoryExtra(const Game& g, int p, int sys, const int counts[kUnitTypes]) {
    if (g.fixes < 2 || !(g.modules & kModDiscordant) || !isFaction(g, p, "rohdhna") || sys < 0) return 0;
    const int f = counts[int(UnitType::Fighter)], i = counts[int(UnitType::Infantry)];
    if (f + i <= 0) return 0;
    const SystemState& s = g.systems[size_t(sys)];
    if (!s.space[size_t(p)].count(UnitType::WarSun)) return 0;
    auto others = [&](const Force& fo) {
        for (int t = 0; t < kUnitTypes; ++t)
            if (fo.n[t] && UnitType(t) != UnitType::WarSun && unitDef(g, p, UnitType(t)).production > 0) return true;
        return false;
    };
    if (others(s.space[size_t(p)])) return 0;
    for (int16_t x : s.planets)
        if (others(g.planets[size_t(x)].units[size_t(p)])) return 0;
    return (f - (f + 1) / 2) + (i - (i + 1) / 2);
}

int structuresOf(const Game& g, int p) {
    int n = unitsOnBoard(g, p, UnitType::PDS) + unitsOnBoard(g, p, UnitType::SpaceDock);
    if ((g.modules & kModDiscordant) && isFaction(g, p, "rohdhna")) n += unitsOnBoard(g, p, UnitType::WarSun);  // Orbital Foundries
    return n;
}

int combatStrength(const Game& g, int p, int sys, bool space) {
    int v = 0;
    auto add = [&](const Force& f) {
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!f.n[t]) continue;
            UnitType ut = UnitType(t);
            if (space ? !isShip(ut) : !isGroundForce(ut)) continue;
            const UnitDef& d = unitDef(g, p, ut);
            if (d.combat <= 0) continue;
            int perDie = 11 - d.combat;  // chance in tenths
            int hp = d.sustain ? 2 : 1;
            v += f.n[t] * perDie * std::max<int>(1, d.combatDice) * hp;
        }
    };
    if (space) {
        add(g.systems[sys].space[p]);
    } else {
        for (int16_t pl : g.systems[sys].planets) add(g.planets[pl].units[p]);
        add(g.systems[sys].space[p]);
    }
    return v;
}

// ============================================================ movement
bool adjacentSystems(const Game& g, int a, int b) {
    for (int16_t x : g.systems[a].adj)
        if (x == b) return true;
    // Emissary Taivra (fixes 24): this tactical action the active system is adjacent to every other system with a wormhole
    if (g.fixes >= 24 && g.fx.taivra && g.activeSystem >= 0 && a != b && (a == g.activeSystem || b == g.activeSystem) &&
        wormholesOf(g, a == g.activeSystem ? b : a))
        return true;
    return false;
}

namespace {
int g_dsAnomalyIgnore = 0;  // Star Dragon (Kjalengard carriers) while their reach is worked out: 1 = all but supernovas, 2 = all
}  // namespace
// Nav Suite: the active player ignores anomalies while moving. Voidborn (Empyrean): nebulae don't affect their movement.
uint8_t anomaliesFor(const Game& g, int p, int s) {
    if (g.fx.navSuite && p == g.activePlayer) return 0;
    if (g_dsAnomalyIgnore) return g_dsAnomalyIgnore >= 2 ? 0 : uint8_t(g.systems[s].d().anomalies & kSupernova);
    uint8_t an = g.systems[s].d().anomalies;
    if (g.modules & kModDiscordant) {
        if (s == g.dsWound) an |= kNebula | kRift;  // Singularity Point (Nivyn): the Wound is a nebula and a gravity rift
        // Aurora Research Base (Veldyr breakthrough, 0.43.0): their home system is also a nebula
        if (g.systems[size_t(s)].home >= 0 && breakthroughsOn(g) && breakthroughIs(g, g.systems[size_t(s)].home, "veldyr")) an |= kNebula;
        // Celestial Guides (Nivyn): anomalies in or next to a system with their structures don't slow them (rifts still help)
        if (an && p >= 0 && p < g.numPlayers && isFaction(g, p, "nivyn")) {
            auto structureIn = [&](int x) {
                for (int16_t pl : g.systems[size_t(x)].planets)
                    if (g.planets[size_t(pl)].units[size_t(p)].structures()) return true;
                return false;
            };
            bool near = structureIn(s);
            for (int16_t a : g.systems[size_t(s)].adj) near = near || structureIn(a);
            if (near) an &= kRift;
        }
    }
    if (isFaction(g, p, "empyrean")) an &= uint8_t(~kNebula);
    if (dimensionalTear(g, s)) an |= kRift;  // Dimensional Tear (Vuil'raith)
    if ((g.modules & kModThundersEdge) && relicHeld(g, p, "circletofthevoid")) an &= kRift;  // Circlet of the Void: only rifts count
    return an;
}
namespace {
// Does p's ship roll when it leaves this rift? (the Vuil'raith don't roll for their Dimensional Tears)
bool riftRolls(const Game& g, int p, int s) {
    if ((g.modules & kModThundersEdge) && relicHeld(g, p, "circletofthevoid")) return false;  // Circlet of the Void
    if ((g.modules & kModDiscordant) && p >= 0 && p < g.numPlayers && isFaction(g, p, "nivyn")) return false;  // Celestial Guides (Nivyn)
    if ((g.modules & kModDiscordant) && s == g.dsWound) return true;  // the Wound is a gravity rift
    if (g.systems[s].d().anomalies & kRift) return true;
    return !isFaction(g, p, "cabal");
}
}  // namespace

bool riftRollsLeaving(const Game& g, int p, int s) {
    return s >= 0 && s < int(g.systems.size()) && (anomaliesFor(g, p, s) & kRift) && riftRolls(g, p, s);
}

namespace { int g_noRiftBonus = 0, g_noMyru = 0; }  // shipReachNoRift (also without the Wraith Engine), shipReachNoMyru
// Hil Colish (Creuss flagship, fixes 24 - owner's faction debug pass: "this ship may move before or after your other
// ships"): while a reach is worked out for one of p's other ships, the active system it heads for (when the flagship
// can get there itself) - its delta wormhole is there once it has moved first; -1 = no such link
namespace { int g_colishTo = -1, g_colishOff = 0; }
int colishOrigin(const Game& g, int p) {
    if (g.fixes < 24 || p < 0 || p >= g.numPlayers) return -1;
    if (!isFaction(g, p, "ghost") && !(g.fixes >= 13 && flagshipAbility(g, p, "ghost"))) return -1;
    for (size_t s = 0; s < g.systems.size(); ++s)
        if (g.systems[s].space[size_t(p)].count(UnitType::Flagship)) return int(s);
    return -1;
}
const std::map<int, Force>* g_movingShips = nullptr;
int moveValue(const Game& g, int p, int sys, UnitType t) {
    int mv = unitDef(g, p, t).move;
    // Gravleash Maneuvers (Letnev breakthrough): ships move as the fastest non-fighter ship that starts with them
    // (0.44.4 fixes 8: the fastest of the ships that MOVE from there - fighters too - when a plan is being checked)
    if (t != UnitType::Fighter && isShip(t) && sys >= 0 && breakthroughIs(g, p, "letnev")) {
        if (g.fixes >= 8 && g_movingShips) {
            const auto it = g_movingShips->find(sys);
            if (it != g_movingShips->end())
                for (int u : kShipIdx)
                    if (it->second.n[u] && isShip(UnitType(u))) mv = std::max(mv, int(unitDef(g, p, UnitType(u)).move));
        } else {
            for (int u : kShipIdx)
                if (u != int(UnitType::Fighter) && g.systems[size_t(sys)].space[size_t(p)].n[u])
                    mv = std::max(mv, int(unitDef(g, p, UnitType(u)).move));
        }
    }
    // Al'Raith Ix Ianovar (Vuil'raith breakthrough): +1 for ships starting in the Fracture
    if (mv > 0 && sys >= 0 && g.systems[size_t(sys)].fracture && breakthroughIs(g, p, "cabal")) ++mv;
    // Resonance Generator (Crimson breakthrough): +1 from your home system or a system with an active breach
    if (mv > 0 && sys >= 0 && breakthroughIs(g, p, "crimson") && (sys == g.players[size_t(p)].homeSystem || g.systems[size_t(sys)].breach == 2) &&
        (g.fixes < 8 || p == g.activePlayer))  // (0.44.4: during their tactical actions)
        ++mv;
    // Heaven's Eye (Firmament flagship): +1 with a player whose token is on one of our plots in the active system
    if (t == UnitType::Flagship && mv > 0 && p == g.activePlayer && g.activeSystem >= 0 && flagshipText(g, p, "firmament"))
        for (int q = 0; q < g.numPlayers; ++q)
            if (anyPuppet(g, p, q) && hasUnits(g, q, g.activeSystem)) {
                ++mv;
                break;
            }
    if (mv > 0 && (anomaliesFor(g, p, sys) & kNebula)) mv = 1;
    // Call of the Void (galactic event): +1 when the active system is in the Fracture
    if (mv > 0 && p == g.activePlayer && g.activeSystem >= 0 && g.systems[size_t(g.activeSystem)].fracture && eventOn(g, kEvVoid)) ++mv;
    if (mv > 0 && g.fx.flankSpeed && p == g.activePlayer) mv += eventOn(g, kEvWild) ? 2 : 1;  // Flank Speed (Wild, Wild Galaxy: +2)
    // Spear of Vaylar (Vaylerian hero, Discordant Stars): +1 to every ship this tactical action
    if (mv > 0 && p == g.activePlayer && p < g.numPlayers && g.players[size_t(p)].dsHeroSys >= 0 && isShip(t)) ++mv;
    if (mv > 0 && p == g.activePlayer && (g.modules & kModDiscordant)) {
        mv += g.fx.dsMove;  // Geosympathic Impeller (Olradin)
        // Rallypoint (Olradin flagship): the other ships move 1 further when it can come along this action
        if (t != UnitType::Flagship && g.activeSystem >= 0 && flagshipAbility(g, p, "olradin"))
            for (size_t fs = 0; fs < g.systems.size(); ++fs)
                if (g.systems[fs].space[size_t(p)].count(UnitType::Flagship)) {
                    if (int(fs) != g.activeSystem && !g.systems[fs].hasToken(p) && shipReach(g, p, int(fs), g.activeSystem, UnitType::Flagship, 0).ok) ++mv;
                    break;
                }
    }
    if (mv > 0 && (g.modules & kModDiscordant) && p >= 0 && p < g.numPlayers) {
        // Desperados (Nokar): their destroyers move 1 further when they aren't the active player
        if (t == UnitType::Destroyer && p != g.activePlayer && isFaction(g, p, "nokar")) ++mv;
        // Rift Engines (Axis): 2 further for the ship that went through the rift
        if (g.fx.dsRiftOwner == p && int(t) == g.fx.dsRiftType && sys == g.fx.dsRiftSys && !g_noRiftBonus) mv += 2;
    }
    if (mv > 0 && p == g.activePlayer && g.activeSystem >= 0) {
        if (g.fx.aetherstream) ++mv;  // Aetherstream
        // Slipstream (Creuss): ships starting at home or at an alpha / beta wormhole
        // (fixes 24: what the system really contains - Nexus Sovereignty doesn't hide the active nexus's; qeWormholes
        // is wormholesOf's alpha / beta before that)
        if (isFaction(g, p, "ghost") && (sys == g.players[p].homeSystem || qeWormholes(g, sys))) ++mv;
    }
    return mv;
}

int maxShipMove(const Game& g) {
    int m = 0;
    for (const SystemState& s : g.systems)
        for (int q = 0; q < g.numPlayers; ++q)
            for (int t : kShipIdx)
                if (s.space[q].n[t]) m = std::max(m, int(unitDef(g, q, UnitType(t)).move));
    return m;
}

namespace {
// Can p's ships end their movement in s?
// Paranoia (Zelian, 0.43.0 D42): nothing but their own command tokens stops them activating or entering their home
bool paranoiaHome(const Game& g, int p, int s) {
    return g.fixes >= 2 && p >= 0 && p < g.numPlayers && (g.modules & kModDiscordant) && s == g.players[size_t(p)].homeSystem &&
           isFaction(g, p, "zelian");
}
bool canEnter(const Game& g, int p, int s) {
    if (g.systems[s].isHyperlane()) return false;
    if (paranoiaHome(g, p, s)) return true;
    uint8_t an = anomaliesFor(g, p, s);
    if ((an & kSupernova) && !owns(g, p, "mr")) return false;  // Magmus Reactor: into supernovas
    if ((an & kAsteroid) && !owns(g, p, "amd")) return false;
    return true;
}
// Can p's ships step into s on the way (Gashlai Physiology: through supernovas)?
bool canStep(const Game& g, int p, int s) {
    if (canEnter(g, p, s)) return true;
    if (g.systems[s].isHyperlane()) return false;
    uint8_t an = anomaliesFor(g, p, s);
    if ((an & kAsteroid) && !owns(g, p, "amd")) return false;
    return (an & kSupernova) && isFaction(g, p, "muaat");
}
bool canPassThrough(const Game& g, int p, int s, int from = -1, bool ignoreShips = false) {
    if (!canStep(g, p, s)) return false;
    // (0.44.8, fixes 12) Magmus Reactor lets ships move INTO a supernova, not through it - only the Muaat pass through
    // (Gashlai Physiology; LRR 86.1)
    if (g.fixes >= 12 && (anomaliesFor(g, p, s) & kSupernova) && !isFaction(g, p, "muaat") && !paranoiaHome(g, p, s)) return false;
    if ((anomaliesFor(g, p, s) & kNebula) && !lawInPlay(g, "shared_research")) return false;
    // In the Silence of Space: ships from the chosen system pass enemy ships.
    bool silence = from >= 0 && from == g.fx.silenceSystem && p == g.activePlayer;
    bool blocked = neutralShipsIn(g, s);  // (Thunder's Edge: neutral ships block as another player's)
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && hasShips(g, q, s) && !(q == g.fx.aetherFrom && p == g.activePlayer)) blocked = true;  // Aetherpassage
    if (blocked && !owns(g, p, "lwd") && !silence && !ignoreShips) return false;
    // Aerie Hololattice (Argent): other players can't move ships through systems with their structures
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p || !owns(g, q, "ah")) continue;
        for (int16_t x : g.systems[s].planets)
            if (g.planets[x].units[q].structures()) return false;
    }
    // Decree (Edyn, Discordant Stars): no moving through anomalies (their Sigils too) with their ground forces
    if (g.modules & kModDiscordant)
        for (int q = 0; q < g.numPlayers; ++q) {
            if (q == p || !isFaction(g, q, "edyn")) continue;
            int gf = g.systems[s].space[size_t(q)].groundForces();
            for (int16_t x : g.systems[s].planets) gf += g.planets[x].units[size_t(q)].groundForces();
            const bool sigil = mechAbilities(g) && gf > 0 && [&]() {
                if (g.systems[s].space[size_t(q)].count(UnitType::Mech)) return true;
                for (int16_t x : g.systems[s].planets)
                    if (g.planets[x].units[size_t(q)].count(UnitType::Mech)) return true;
                return false;
            }();
            if (gf > 0 && (g.systems[s].d().anomalies || s == g.dsWound || sigil)) return false;
        }
    return true;
}
// Cheapest route from 'from' to 'to': fewest move points, then fewest gravity
// rift exits. allowRift: leaving a rift costs no move (its +1 bonus, LRR 41.1 -
// the owner confirmed the bonus stacks: +1 for EACH rift) but the ship must
// roll each time it leaves one (41.2, 41.3); otherwise the
// route may not leave or pass through a rift at all.
struct Route {
    int cost = 1 << 20;
    int exits = 0;
    int rolls = 0;          // rift exits that need a roll (not the Vuil'raith's own Dimensional Tears)
    bool storm = false;     // uses the ion storm wormhole (avoided when there's an equal route)
    bool wormhole = false;  // goes through a wormhole (Sai Seravus)
};
// deep: the Deepwrought flagship - through systems with our units even with other players' units there, and each such
// system passed through adds 1 to its move (the step out of it is free)
Route bestRoute(const Game& g, int p, int from, int to, bool allowRift, bool ignoreShips = false, bool deep = false) {
    const int n = int(g.systems.size());
    std::vector<Route> dist(n);
    std::vector<char> done(n, 0);
    dist[from].cost = 0;
    auto better = [](const Route& a, const Route& b) {
        if (a.cost != b.cost) return a.cost < b.cost;
        if (a.exits != b.exits) return a.exits < b.exits;
        return !a.storm && b.storm;
    };
    for (;;) {
        int u = -1;
        for (int i = 0; i < n; ++i)
            if (!done[i] && dist[i].cost < (1 << 20) && (u < 0 || better(dist[i], dist[u]))) u = i;
        if (u < 0 || u == to) break;
        done[u] = 1;
        bool uRift = (anomaliesFor(g, p, u) & kRift) != 0;
        if (uRift && !allowRift) continue;
        bool deepStep = false;  // the Deepwrought flagship passing through a crowded system of ours
        if (deep && u != from && hasUnits(g, p, u)) {
            deepStep = neutralUnitsIn(g, u) || g.fixes >= 27;  // (fixes 27, the Deepwrought review - owner 2026-11-14: as AsyncTI4's note,
            for (int q = 0; q < g.numPlayers; ++q)            // "Other players' units do not need to be present for the speed boost")
                if (q != p && hasUnits(g, q, u)) deepStep = true;
        }
        if (u != from && !canPassThrough(g, p, u, from, ignoreShips || (deep && hasUnits(g, p, u)))) continue;
        for (int v : routeNeighbors(g, p, u)) {
            if (!canStep(g, p, v) || done[v]) continue;
            const SystemState& a = g.systems[u];
            const SystemState& b = g.systems[v];
            bool touching = !a.offBoard && !b.offBoard && hexDistance(a.pos, b.pos) == 1;
            Route r{dist[u].cost + (uRift || deepStep ? 0 : 1), dist[u].exits + (uRift ? 1 : 0), dist[u].rolls + (uRift && riftRolls(g, p, u) ? 1 : 0),
                    dist[u].storm || ionStormStep(g, u, v), dist[u].wormhole || (!touching && wormholesOf(g, u) && wormholesOf(g, v))};
            if (better(r, dist[v])) dist[v] = r;
        }
    }
    return dist[to];
}

// Waypoints (0.56.0, owner): the route a player chose - from, the stops in order, to - walked step by step with
// bestRoute's rules (a step into a system it may enter, a system passed through must let it pass, leaving a gravity
// rift costs no move and rolls a die - here rifts are always allowed: the player chose them). cost 1 << 20 = not a
// route; *why says what is wrong with it.
std::string routeLabel(const Game& g, int s) {
    const SystemState& sy = g.systems[size_t(s)];
    if (sy.home >= 0 && kSystems[sy.def].back != Back::Home) return std::string(kSystems[sy.def].name) + " (" + playerName(g, sy.home) + " home)";
    if (sy.home >= 0) return playerName(g, sy.home) + " home";
    return kSystems[sy.def].name;
}
Route pathRoute(const Game& g, int p, int from, const std::vector<int>& via, int to, bool ignoreShips, bool deep, std::string* why) {
    Route bad;
    auto fail = [&](const std::string& w) {
        if (why) *why = w;
        return bad;
    };
    const int n = int(g.systems.size());
    std::vector<int> path{from};
    path.insert(path.end(), via.begin(), via.end());
    path.push_back(to);
    Route r;
    r.cost = 0;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const int u = path[i], v = path[i + 1];
        if (u < 0 || u >= n || v < 0 || v >= n || u == v) return fail("That isn't a route.");
        const bool uRift = (anomaliesFor(g, p, u) & kRift) != 0;
        bool deepStep = false;
        if (deep && u != from && hasUnits(g, p, u)) {
            deepStep = neutralUnitsIn(g, u) || g.fixes >= 27;  // (fixes 27, the Deepwrought review - owner 2026-11-14: as AsyncTI4's note,
            for (int q = 0; q < g.numPlayers; ++q)            // "Other players' units do not need to be present for the speed boost")
                if (q != p && hasUnits(g, q, u)) deepStep = true;
        }
        if (u != from && !canPassThrough(g, p, u, from, ignoreShips || (deep && hasUnits(g, p, u))))
            return fail("Ships can't move through " + routeLabel(g, u) + " (other players' ships, a nebula, ...).");
        const std::vector<int> nb = routeNeighbors(g, p, u);
        if (std::find(nb.begin(), nb.end(), v) == nb.end()) return fail(routeLabel(g, v) + " isn't next to " + routeLabel(g, u) + ".");
        if (!canStep(g, p, v)) return fail("Ships can't enter " + routeLabel(g, v) + ".");
        const SystemState& a = g.systems[size_t(u)];
        const SystemState& b = g.systems[size_t(v)];
        const bool touching = !a.offBoard && !b.offBoard && hexDistance(a.pos, b.pos) == 1;
        r.cost += uRift || deepStep ? 0 : 1;
        r.exits += uRift ? 1 : 0;
        r.rolls += uRift && riftRolls(g, p, u) ? 1 : 0;
        r.storm = r.storm || ionStormStep(g, u, v);
        r.wormhole = r.wormhole || (!touching && wormholesOf(g, u) && wormholesOf(g, v));
    }
    return r;
}
}  // namespace
// (0.44.7) can p's ships end a move in s (no supernova without Magmus Reactor, no asteroid field without Antimass
// Deflectors) - for moves outside a tactical action (Spark Thrusters)
bool canEndMoveIn(const Game& g, int p, int s) { return s >= 0 && s < int(g.systems.size()) && canEnter(g, p, s); }

bool passableFor(const Game& g, int p, int sys) { return canPassThrough(g, p, sys); }

std::vector<int> routeNeighbors(const Game& g, int p, int u) {
    std::vector<int> out(g.systems[u].adj.begin(), g.systems[u].adj.end());
    const int n = int(g.systems.size());
    auto add = [&](int v) {
        if (v != u && v >= 0 && std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
    };
    auto addAll = [&](uint8_t mask) {
        for (int v = 0; v < n; ++v)
            if (wormholesOf(g, v) & mask) add(v);
    };
    const uint8_t wu = wormholesOf(g, u);
    // Quantum Entanglement (Creuss): every alpha and beta wormhole is adjacent to every other one
    if (g.fixes >= 24 && isFaction(g, p, "ghost")) {  // (fixes 24: game effects can't prevent it - Nexus Sovereignty)
        if (qeWormholes(g, u))
            for (int v = 0; v < n; ++v)
                if (qeWormholes(g, v)) add(v);
    } else if ((wu & (kAlpha | kBeta)) && isFaction(g, p, "ghost")) {
        addAll(kAlpha | kBeta);
    }
    // Lazax Gate Folding (Winnu, 0.44.7 fixes 11): also while the Winnu choose what to activate on their turn (the
    // planner and the "can't reach" warning missed it), not under Enforced Travel Ban; Sever cuts it like a wormhole
    int lgfMec = -1;
    if (g.fixes >= 11 && p == g.activePlayer && g.phase == Phase::Action && owns(g, p, "lgf") && !lawInPlay(g, "travel_ban")) {
        const int mec = g.mecatol();
        bool mine = false;
        if (mec >= 0)
            for (int16_t x : g.systems[size_t(mec)].planets)
                if (g.planets[size_t(x)].owner == p) mine = true;
        if (mec >= 0 && !mine) {
            lgfMec = mec;
            if (u == mec) addAll(kAlpha | kBeta);
            else if (wu & (kAlpha | kBeta)) add(mec);
        }
    }
    // Spatial Conduit Cylinder (Jol-Nar, 0.44.7 fixes 11): a real adjacency this action - routes through it roll for
    // gravity rifts and retreats may use it
    if (g.fixes >= 11 && g.fx.conduit && p == g.activePlayer && g.activeSystem >= 0) {
        if (u == g.activeSystem) {
            for (int v = 0; v < n; ++v)
                if (hasUnits(g, p, v)) add(v);
        } else if (hasUnits(g, p, u)) {
            add(g.activeSystem);
        }
    }
    // Emissary Taivra (fixes 24, owner's faction debug pass): the active system is adjacent to every other system with a
    // wormhole (epsilon too) - for every player this tactical action (retreats), not only the one moving
    if (g.fixes >= 24 && g.fx.taivra && g.activeSystem >= 0 && p != g.activePlayer) {
        if (u == g.activeSystem) addAll(0xFF);
        else if (wu) add(g.activeSystem);
    }
    if (p == g.activePlayer && g.activeSystem >= 0) {
        // Emissary Taivra: the active system is next to every system with a wormhole
        if (g.fx.taivra) {
            if (u == g.activeSystem) addAll(g.fixes >= 24 ? uint8_t(0xFF) : uint8_t(kAlpha | kBeta | kGamma | kDelta));
            else if (wu) add(g.activeSystem);
        }
        // Lazax Gate Folding (Winnu): Mecatol Rex counts as an alpha and beta wormhole if they don't control it
        if (owns(g, p, "lgf") && g.fixes < 11) {
            int mec = g.mecatol();
            bool mine = false;
            if (mec >= 0)
                for (int16_t x : g.systems[mec].planets)
                    if (g.planets[x].owner == p) mine = true;
            if (mec >= 0 && !mine) {
                if (u == mec) addAll(kAlpha | kBeta);
                else if (wu & (kAlpha | kBeta)) add(mec);
            }
        }
    }
    // Anomaly Stabilization (Nivyn breakthrough, 0.43.0): entropic scars count as ingress tokens for them
    if (g.fractureIn && (g.modules & kModDiscordant) && p >= 0 && p < g.numPlayers && breakthroughIs(g, p, "nivyn")) {
        const SystemState& su = g.systems[size_t(u)];
        if (scarAt(g, u) && !su.ingress)
            for (int v = 0; v < n; ++v)
                if (g.systems[size_t(v)].egress) add(v);
        if (su.egress)
            for (int v = 0; v < n; ++v)
                if (!g.systems[size_t(v)].ingress && scarAt(g, v)) add(v);
    }
    // Hil Colish moving first (fixes 24): its delta wormhole in the active system - next to the other delta wormholes
    // (the Creuss home and Gate; the one where it stands now goes with it)
    if (g_colishTo >= 0 && !g_colishOff) {
        const int A = g_colishTo, O = colishOrigin(g, p);
        auto deltaOther = [&](int x) {
            if (x == O) return ((g.systems[size_t(x)].d().wormholes | g.systems[size_t(x)].tokenWormholes) & kDelta) != 0;
            return (wormholesOf(g, x) & kDelta) != 0;
        };
        if (u == A) {
            for (int v = 0; v < n; ++v)
                if (v != A && deltaOther(v)) add(v);
        } else if (deltaOther(u)) {
            add(A);
        }
    }
    // Sundered (Crimson Rebellion): only epsilon wormholes; Sever (their note): wormholes in that system do nothing
    const bool sundered = isFaction(g, p, "crimson");
    if ((sundered || g.severSystem >= 0) && g.fixes >= 24) {
        // (fixes 24, owner's faction debug pass) board links first - tiles side by side or joined by hyperlanes stay,
        // whatever wormholes they also share; then any link that needs the severed system's wormholes goes, whatever
        // is on the other end (Quantum Entanglement, Wormhole Reconstruction / Lost Star Chart alpha-beta, Emissary
        // Taivra); the Crimson keep only epsilon-to-epsilon wormhole links
        const SystemState& su = g.systems[size_t(u)];
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&](int v) {
                                     const SystemState& sv = g.systems[size_t(v)];
                                     if (boardAdjacent(g, u, v) || (su.breach == 2 && sv.breach == 2) || (su.ingress && sv.egress) ||
                                         (su.egress && sv.ingress))
                                         return false;
                                     if (qeLinked(g, p, u, v)) return false;  // Quantum Entanglement: nothing can stop it
                                     if ((g.fx.starA == u && g.fx.starB == v) || (g.fx.starA == v && g.fx.starB == u)) return false;
                                     if (g.fx.conduit && p == g.activePlayer && g.activeSystem >= 0 &&
                                         ((u == g.activeSystem && hasUnits(g, p, v)) || (v == g.activeSystem && hasUnits(g, p, u))))
                                         return false;
                                     const uint8_t wu2 = uint8_t(wormholesOf(g, u) | (u == lgfMec ? (kAlpha | kBeta) : 0));
                                     const uint8_t wv2 = uint8_t(wormholesOf(g, v) | (v == lgfMec ? (kAlpha | kBeta) : 0));
                                     if (!wu2 && !wv2) return false;  // (some other link)
                                     // (Emissary Taivra from a severed active system: those links need the OTHER wormholes)
                                     const bool taivraFrom = g.fx.taivra && p == g.activePlayer && g.severSystem == g.activeSystem;
                                     if ((u == g.severSystem && wu2 && !taivraFrom) || (v == g.severSystem && wv2 && !taivraFrom)) return true;
                                     return sundered && !(wu2 & wv2 & kEpsilon);
                                 }),
                  out.end());
    } else if (sundered || g.severSystem >= 0) {
        const SystemState& su = g.systems[size_t(u)];
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&](int v) {
                                     const SystemState& sv = g.systems[size_t(v)];
                                     const bool touching = !su.offBoard && !sv.offBoard && hexDistance(su.pos, sv.pos) == 1;
                                     if (touching || (su.breach == 2 && sv.breach == 2) || (su.ingress && sv.egress) || (su.egress && sv.ingress))
                                         return false;
                                     const uint8_t lu = u == lgfMec ? uint8_t(kAlpha | kBeta) : uint8_t(0);
                                     const uint8_t lv = v == lgfMec ? uint8_t(kAlpha | kBeta) : uint8_t(0);
                                     const uint8_t both = (wormholesOf(g, u) | lu) & (wormholesOf(g, v) | lv);
                                     if (!both) return false;  // (a hyperlane or some other link)
                                     if (u == g.severSystem || v == g.severSystem) return true;
                                     return sundered && !(both & kEpsilon);
                                 }),
                  out.end());
    }
    // Henry Storcher (Celdauri commander, Discordant Stars): systems with their space docks are next to each other
    if ((g.modules & kModDiscordant) && p >= 0 && p < g.numPlayers && commanderUsable(g, p, "celdauri")) {
        auto dockIn = [&](int v) {
            for (int16_t x : g.systems[size_t(v)].planets)
                if (g.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock)) return true;
            return false;
        };
        if (dockIn(u))
            for (int v = 0; v < n; ++v)
                if (dockIn(v)) add(v);
    }
    // Ceie Doleegueaunm (Ghoti commander, Discordant Stars): this action, systems without planets are next to each other
    if ((g.modules & kModDiscordant) && g.fx.dsCeie == p && p >= 0 && g.systems[size_t(u)].planets.empty() && !g.systems[size_t(u)].isHyperlane())
        for (int v = 0; v < n; ++v)
            if (g.systems[size_t(v)].planets.empty() && !g.systems[size_t(v)].isHyperlane() && g.systems[size_t(v)].def >= 0) add(v);
    // Krill Drakkon (Nivyn hero, Discordant Stars): this round their units are next to the Wound
    if ((g.modules & kModDiscordant) && g.dsWound >= 0 && u != g.dsWound && p >= 0 && p < g.numPlayers && isFaction(g, p, "nivyn") &&
        g.players[size_t(p)].heroRound == g.round && hasUnits(g, p, u))
        add(g.dsWound);
    if (!g.tethers.empty())  // Void Tether (Empyrean breakthrough): borders other players can't cross
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&](int v) { return tethered(g, p, u, v) && !(g.fixes >= 24 && qeLinked(g, p, u, v)); }),
                  out.end());
    return out;
}

bool ionStormStep(const Game& g, int a, int b) {
    const int st = g.ionStorm;
    if (st < 0 || a == b || (a != st && b != st)) return false;
    const int other = a == st ? b : a;
    if (!(wormholesOf(g, other) & (g.ionAlpha ? kAlpha : kBeta))) return false;
    // Tiles that touch on the board are adjacent anyway: that step doesn't need the wormhole.
    const SystemState& x = g.systems[a];
    const SystemState& y = g.systems[b];
    bool touching = !x.offBoard && !y.offBoard && hexDistance(x.pos, y.pos) == 1;
    return !touching;
}

int travelCost(const Game& g, int p, int from, int to) {
    if (from == to) return 0;
    if (anomaliesFor(g, p, from) & kRift) return 1 << 20;
    return bestRoute(g, p, from, to, false).cost;
}

// (0.54.0, fixes 18) the same, also through gravity rifts the ship doesn't roll for (the Vuil'raith's own Dimensional
// Tears): leaving one costs no move (its +1); rifts that roll stay out of the way (a detour to pick something up would
// need a roll the movement doesn't make)
int travelCostNoRoll(const Game& g, int p, int from, int to) {
    if (from == to) return 0;
    const Route r = bestRoute(g, p, from, to, true);
    if (r.cost < (1 << 20) && r.rolls == 0) return std::min(r.cost, travelCost(g, p, from, to));
    return travelCost(g, p, from, to);
}

namespace {
Reach shipReachCore(const Game& g, int p, int from, int to, UnitType t, int bonus);
}
// 0.43.0 (D113): the reach of the other ships of a Rift Engines group - without the one ship's +2
Reach shipReachNoRift(const Game& g, int p, int from, int to, UnitType t, int bonus) {
    const int was = g_noRiftBonus;
    g_noRiftBonus = 1;
    const Reach r = shipReach(g, p, from, to, t, bonus);
    g_noRiftBonus = was;
    return r;
}
Reach shipReachNoMyru(const Game& g, int p, int from, int to, UnitType t, int bonus) {
    const int was = g_noMyru;
    g_noMyru = 1;
    const Reach r = shipReach(g, p, from, to, t, bonus);
    g_noMyru = was;
    return r;
}
namespace {
// Hil Colish first (fixes 24): the active system another of p's ships may reach through the flagship's delta wormhole
// (planning before the activation counts too) - only when the flagship can get there itself
struct ColishScope {
    int was;
    explicit ColishScope(int v) : was(g_colishTo) { g_colishTo = v; }
    ~ColishScope() { g_colishTo = was; }
};
int colishTarget(const Game& g, int p, int from, int to, UnitType t) {
    if (g.fixes < 24 || g_colishOff || p != g.activePlayer || g.phase != Phase::Action || to < 0 ||
        (g.activeSystem >= 0 && g.activeSystem != to))
        return -1;
    const int o = colishOrigin(g, p);
    if (o < 0 || o == to || (isFlagship(t) && from == o) || g.systems[size_t(o)].hasToken(p)) return -1;
    ColishScope none(-1);
    return shipReachCore(g, p, o, to, UnitType::Flagship, moveBonusMax(g, p)).ok ? to : -1;
}
}  // namespace
Reach shipReachNoColish(const Game& g, int p, int from, int to, UnitType t, int bonus) {
    ++g_colishOff;
    const Reach r = shipReach(g, p, from, to, t, bonus);
    --g_colishOff;
    return r;
}
Reach shipReachViaNoColish(const Game& g, int p, int from, const std::vector<int>& via, int to, UnitType t, int bonus) {
    ++g_colishOff;
    const Reach r = shipReachVia(g, p, from, via, to, t, bonus, nullptr);
    --g_colishOff;
    return r;
}
// Star Dragon I / II (Kjalengard carriers, Discordant Stars): they may ignore the movement effects of anomalies
// (Star Dragon I: not supernovas) - the better of the two routes.
Reach shipReach(const Game& g, int p, int from, int to, UnitType t, int bonus) {
    // Hil Colish (fixes 24): the usual way first - only a ship that can't get there otherwise needs it to move first
    if (g_colishTo < 0 && !g_colishOff && colishOrigin(g, p) >= 0) {
        const Reach r = shipReachNoColish(g, p, from, to, t, bonus);
        if (r.ok) return r;
        const int ct = colishTarget(g, p, from, to, t);
        if (ct < 0) return r;
        ColishScope colish(ct);
        Reach w = shipReach(g, p, from, to, t, bonus);
        w.colish = w.ok;
        return w.ok ? w : r;
    }
    if ((g.modules & kModDiscordant) && t == UnitType::Carrier && p >= 0 && p < g.numPlayers &&
        !std::strncmp(unitDef(g, p, t).key, "kjalengard_carrier", 18)) {
        const int prev = g_dsAnomalyIgnore;
        g_dsAnomalyIgnore = unitDef(g, p, t).key[18] == '2' ? 2 : 1;
        const Reach a = shipReachCore(g, p, from, to, t, bonus);
        g_dsAnomalyIgnore = prev;
        if (a.ok && !a.needsBonus) return a;
        const Reach b = shipReachCore(g, p, from, to, t, bonus);
        if (b.ok && (!a.ok || (!b.needsBonus && !b.viaRift))) return b;
        return a.ok ? a : b;
    }
    return shipReachCore(g, p, from, to, t, bonus);
}
namespace {
Reach shipReachCore(const Game& g, int p, int from, int to, UnitType t, int bonus) {
    Reach r;
    if (from == to) return r;
    int mv = moveValue(g, p, from, t);
    if (mv <= 0 || !canEnter(g, p, to)) return r;
    // Spatial Conduit Cylinder: the active system is adjacent to every system with the active player's units.
    // (0.44.7, fixes 11: routeNeighbors has it - the routing below rolls for rifts)
    if (g.fixes < 11 && g.fx.conduit && p == g.activePlayer && to == g.activeSystem && hasUnits(g, p, from)) {
        r.ok = true;
        return r;
    }
    // Wraith Engine (Li-Zho, Discordant Stars): the chosen ship counts as next to the active system
    if (g.fx.dsWraithOwner == p && from == g.fx.dsWraithSys && int(t) == g.fx.dsWraithType && to == g.activeSystem && !g_noRiftBonus) {
        r.ok = true;
        return r;
    }
    bool fromRift = (anomaliesFor(g, p, from) & kRift) != 0;
    // Y'sia Y'ssrila (Yssaril flagship): moves through systems with other players' ships
    // The Table's Grace (Mentak breakthrough, 0.44.4 fixes 8): Corsairs move through systems with other players' ships
    // when the active system holds another player's non-fighter ships
    auto corsairThrough = [&]() {
        if (t != UnitType::Cruiser || g.fixes < 8 || !breakthroughIs(g, p, "mentak") || unitDef(g, p, t).tech < 0) return false;
        if (to < 0 || to >= int(g.systems.size())) return false;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p && g.systems[size_t(to)].space[size_t(q)].nonFighterShips() > 0) return true;
        return false;
    };
    const bool through = (t == UnitType::Flagship && flagshipText(g, p, "yssaril")) || corsairThrough() ||
                         (t == UnitType::Destroyer && !std::strcmp(unitDef(g, p, t).key, "tnelis_destroyer2")) ||  // (Blockade Runner II)
                         (g.fx.myruThrough == p && !g_noMyru);  // Myru Vos (0.43.0: checkMovePlan keeps their cargo out)
    const bool deep = t == UnitType::Flagship && flagshipText(g, p, "deepwrought");  // (Thunder's Edge)
    Route safe = fromRift ? Route() : bestRoute(g, p, from, to, false, through, deep);
    Route rift = bestRoute(g, p, from, to, true, through, deep);
    // (fixes 26, the Vuil'raith review - owner 2026-11-12) Crucible: "apply an additional +1 to the move value of each of your
    // ships that would move out of or through a gravity rift" - on top of the rift's own bonus, once per ship
    const int crucible = g.fixes >= 26 && g.fx.crucible && p == g.activePlayer && (rift.exits > 0 || fromRift) ? 1 : 0;
    for (int b = 0; b <= bonus; ++b) {
        int budget = mv + b;
        // A ship takes a rift-free route whenever one is in range; it only uses
        // (and rolls for) a gravity rift when it has to (owner OK'd, RULES-CHECK 0.3.0 A4).
        if (safe.cost <= budget) {
            r.ok = true;
            r.needsBonus = b > 0;
            r.bonus = int8_t(b);
            r.viaStorm = safe.storm;
            r.viaWormhole = safe.wormhole;
            return r;
        }
        if (rift.cost <= budget + crucible) {
            r.ok = true;
            r.viaRift = rift.rolls > 0 || (fromRift && riftRolls(g, p, from));
            r.viaStorm = rift.storm;
            r.viaWormhole = rift.wormhole;
            r.riftRolls = r.viaRift ? std::max(1, rift.rolls) : 0;
            r.needsBonus = b > 0;
            r.bonus = int8_t(b);
            return r;
        }
    }
    // Memoria (Nomad flagship): treated as adjacent to every system with a Nomad mech (0.44.4: with Valefar Z, the Nekro's mechs)
    if (t == UnitType::Flagship && flagshipText(g, p, "nomad")) {
        for (size_t m = 0; m < g.systems.size(); ++m) {
            if (int(m) == from) continue;
            bool mech = g.systems[m].space[p].count(UnitType::Mech) > 0;
            for (int16_t x : g.systems[m].planets)
                if (g.planets[x].units[p].count(UnitType::Mech)) mech = true;
            if (!mech || !canStep(g, p, int(m))) continue;
            if (int(m) == to || (mv + bonus >= 2 && canPassThrough(g, p, int(m), from) && bestRoute(g, p, int(m), to, false).cost <= mv + bonus - 1)) {
                r.ok = true;
                r.needsBonus = int(m) != to && bestRoute(g, p, int(m), to, false).cost > mv - 1;
                r.bonus = int8_t(r.needsBonus ? std::max(1, bestRoute(g, p, int(m), to, false).cost - (mv - 1)) : 0);
                return r;
            }
        }
    }
    // Eye of Vogul (Uncharted Space relic): the ships picked are adjacent to the active system
    if (to == g.activeSystem && vogulShips(g, p, from, t) > 0) {
        r = Reach();
        r.ok = true;
        r.vogul = true;
    }
    return r;
}
}  // namespace

// Waypoints (0.56.0): can a ship of this type from 'from' follow the route via the stops to 'to'? (as shipReach; the
// Yssaril flagship, the Table's Grace, Blockade Runner II, Myru Vos and the Deepwrought flagship keep their passes)
namespace {
Reach shipReachViaCore(const Game& g, int p, int from, const std::vector<int>& via, int to, UnitType t, int bonus, std::string* why) {
    Reach r;
    const int mv = moveValue(g, p, from, t);
    if (mv <= 0) {
        if (why) *why = "It can't move.";
        return r;
    }
    if (to < 0 || to >= int(g.systems.size()) || !canEnter(g, p, to)) {
        if (why) *why = "Ships can't enter that system.";
        return r;
    }
    auto corsairThrough = [&]() {
        if (t != UnitType::Cruiser || g.fixes < 8 || !breakthroughIs(g, p, "mentak") || unitDef(g, p, t).tech < 0) return false;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p && g.systems[size_t(to)].space[size_t(q)].nonFighterShips() > 0) return true;
        return false;
    };
    const bool through = (t == UnitType::Flagship && flagshipText(g, p, "yssaril")) || corsairThrough() ||
                         (t == UnitType::Destroyer && !std::strcmp(unitDef(g, p, t).key, "tnelis_destroyer2")) ||
                         (g.fx.myruThrough == p && !g_noMyru);
    const bool deep = t == UnitType::Flagship && flagshipText(g, p, "deepwrought");
    const Route route = pathRoute(g, p, from, via, to, through, deep, why);
    if (route.cost >= (1 << 20)) return r;
    for (int b = 0; b <= bonus; ++b) {
        if (route.cost > mv + b) continue;
        r.ok = true;
        r.viaRift = route.rolls > 0;
        r.riftRolls = route.rolls;
        r.viaStorm = route.storm;
        r.viaWormhole = route.wormhole;
        r.needsBonus = b > 0;
        r.bonus = int8_t(b);
        return r;
    }
    if (why)
        *why = "That route takes " + std::to_string(route.cost) + " move" + (route.cost == 1 ? "" : "s") + "; " +
               unitTypeName(t, true) + " there have " + std::to_string(mv) + (bonus > 0 ? " (+" + std::to_string(bonus) + ")" : "") + ".";
    return r;
}
}  // namespace

Reach shipReachVia(const Game& g, int p, int from, const std::vector<int>& via, int to, UnitType t, int bonus, std::string* why) {
    if (via.empty()) return shipReach(g, p, from, to, t, bonus);
    if (g_colishTo < 0 && !g_colishOff && colishOrigin(g, p) >= 0) {  // Hil Colish (fixes 24): as in shipReach
        std::string plainWhy;
        ++g_colishOff;
        const Reach r = shipReachVia(g, p, from, via, to, t, bonus, why ? &plainWhy : nullptr);
        --g_colishOff;
        const int ct = r.ok ? -1 : colishTarget(g, p, from, to, t);
        if (ct >= 0) {
            ColishScope colish(ct);
            Reach w = shipReachVia(g, p, from, via, to, t, bonus, nullptr);
            w.colish = w.ok;
            if (w.ok) return w;
        }
        if (why) *why = plainWhy;
        return r;
    }
    if ((g.modules & kModDiscordant) && t == UnitType::Carrier && p >= 0 && p < g.numPlayers &&
        !std::strncmp(unitDef(g, p, t).key, "kjalengard_carrier", 18)) {  // Star Dragons: the better of the two
        const int prev = g_dsAnomalyIgnore;
        g_dsAnomalyIgnore = unitDef(g, p, t).key[18] == '2' ? 2 : 1;
        const Reach a = shipReachViaCore(g, p, from, via, to, t, bonus, nullptr);
        g_dsAnomalyIgnore = prev;
        if (a.ok && !a.needsBonus) return a;
        const Reach b = shipReachViaCore(g, p, from, via, to, t, bonus, why);
        if (b.ok && (!a.ok || (!b.needsBonus && !b.viaRift))) return b;
        return a.ok ? a : b;
    }
    return shipReachViaCore(g, p, from, via, to, t, bonus, why);
}

// The next systems a route from 'from' (stops so far in 'via') may step to on the way to 'to' (Waypoint Mode's
// highlights): next to the last stop, enterable, and a system ships may pass through - or 'to' itself.
std::vector<int> routeNextSteps(const Game& g, int p, int from, const std::vector<int>& via, int to) {
    std::vector<int> out;
    const int last = via.empty() ? from : via.back();
    if (last != from && !canPassThrough(g, p, last, from, false)) return out;  // (it can only end there)
    for (int v : routeNeighbors(g, p, last)) {
        if (v == from || std::find(via.begin(), via.end(), v) != via.end()) continue;
        if (v == to) {
            if (canEnter(g, p, v)) out.push_back(v);
            continue;
        }
        if (!canStep(g, p, v) || g.systems[size_t(v)].isHyperlane()) continue;
        out.push_back(v);
    }
    return out;
}

bool shipsMayEnter(const Game& g, int p, int s) { return canEnter(g, p, s); }

int vogulShips(const Game& g, int p, int from, UnitType t) {
    if (p != g.activePlayer || g.fx.vogul.empty()) return 0;
    int n = 0;
    for (int16_t v : g.fx.vogul)
        if (v / 16 == from && v % 16 == int(t)) ++n;
    return n;
}

std::vector<int> systemsWithin(const Game& g, int from, int steps) {
    std::vector<int> dist(g.systems.size(), -1), out;
    std::deque<int> q;
    dist[from] = 0;
    q.push_back(from);
    while (!q.empty()) {
        int u = q.front();
        q.pop_front();
        out.push_back(u);
        if (dist[u] >= steps) continue;
        for (int16_t v : g.systems[u].adj)
            if (dist[v] < 0) {
                dist[v] = dist[u] + 1;
                q.push_back(v);
            }
    }
    return out;
}

bool canActivate(const Game& g, int p, int sys) {
    const SystemState& s = g.systems[sys];
    if (s.isHyperlane()) return false;
    // Il Na Viroset (Mahact commander): systems with your command tokens too (both tokens return, the turn ends)
    // (fixes 26, the Mahact review) for anyone who has the ability - Yin Ascendant, Mercenaries / Minor Factions, Zealous
    // Orthodoxy (it was the Mahact only)
    if (s.hasToken(p) && !(commanderUsable(g, p, "mahact") && (g.fixes >= 26 || isFaction(g, p, "mahact")))) return false;
    if (paranoiaHome(g, p, sys)) return true;  // Paranoia (Zelian): only their own tokens (checked above)
    // Chaos Mapping (Saar): other players can't activate asteroid fields with their ships
    if (s.d().anomalies & kAsteroid)
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p && hasShips(g, q, sys) && owns(g, q, "cm")) return false;
    // Artemiris (Keleres flagship): other players pay 2 influence to activate its system
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && s.space[q].count(UnitType::Flagship) && (isFaction(g, q, "keleres") || (g.fixes >= 13 && flagshipAbility(g, q, "keleres"))) &&
            spendable(g, p, true) < 2)
            return false;
    return true;
}

// ============================================================ technology
int techCount(const Game& g, int p, TechColor c) {
    if (p < 0 || p >= g.numPlayers) return 0;  // (Thunder's Edge: not for neutral units)
    int n = 0;
    for (int16_t t : g.players[p].techs)
        if (kTechs[t].color == c) ++n;
    return n;
}

void missingPrereqs(const Game& g, int p, int t, int missing[4]) {
    // Publicize Weapon Schematics (For): once anyone owns a war sun technology, war sun
    // technologies have no prerequisites for anyone (0.21.1 audit: it was missing).
    if (lawInPlay(g, "schematics")) {
        bool warSunTech = false;
        for (int u = 0; u < kUnitCount; ++u)
            if (kUnits[u].tech == t && kUnits[u].type == UnitType::WarSun) warSunTech = true;
        bool anyone = false;
        for (int q = 0; q < g.numPlayers && warSunTech; ++q)
            if (!g.players[q].eliminated && ownsWarSunTech(g, q)) anyone = true;
        if (warSunTech && anyone) {
            for (int c = 0; c < 4; ++c) missing[c] = 0;
            return;
        }
    }
    // Brother Omar (Yin commander): counts as a green technology
    const int omar = commanderUsable(g, p, "yin") ? 1 : 0;
    // Clona Bathru (Dih-Mohn commander) / Lachis (Augurs commander), Discordant Stars: a red / a yellow technology
    const bool ds = (g.modules & kModDiscordant) != 0;
    const int clona = ds && commanderUsable(g, p, "dihmohn") ? 1 : 0, lachis = ds && commanderUsable(g, p, "augers") ? 1 : 0;
    const int kado = ds && commanderUsable(g, p, "kollecc") ? 1 : 0;  // Kado S'mah-Qar (Kollecc commander): a blue one
    for (int c = 0; c < 4; ++c)
        missing[c] = std::max(0, int(kTechs[t].req[c]) - techCount(g, p, TechColor(c)) - (c == int(TechColor::Green) ? omar : 0) -
                                     (c == int(TechColor::Red) ? clona : 0) - (c == int(TechColor::Yellow) ? lachis : 0) -
                                     (c == int(TechColor::Blue) ? kado : 0));
    // The Quantumcore: every coloured technology covers any prerequisite - the shortfall goes on the colour the
    // technology asks most of (its specialties may then come from planets of any colour)
    if (allSynergy(g, p)) {
        int need = 0, have = omar, top = 0;
        for (int c = 0; c < 4; ++c) {
            need += kTechs[t].req[c];
            have += techCount(g, p, TechColor(c));
            if (kTechs[t].req[c] > kTechs[t].req[top]) top = c;
        }
        for (int c = 0; c < 4; ++c) missing[c] = 0;
        missing[top] = std::max(0, need - have);
        return;
    }
    // Thunder's Edge synergy: technologies of one of the pair's colours left over cover the other's prerequisites
    TechColor sa, sb;
    if (synergyOf(g, p, sa, sb)) {
        const int a = int(sa), b = int(sb);
        int spareA = std::max(0, techCount(g, p, sa) + (a == int(TechColor::Green) ? omar : 0) - int(kTechs[t].req[a]));
        int spareB = std::max(0, techCount(g, p, sb) + (b == int(TechColor::Green) ? omar : 0) - int(kTechs[t].req[b]));
        const int ca = std::min(missing[a], spareB);
        missing[a] -= ca;
        spareB -= ca;
        const int cb = std::min(missing[b], spareA);
        missing[b] -= cb;
    }
    // Vera Khage (Veldyr commander, Discordant Stars): a technology owned by someone with a Branch Office planet - 1 less
    if (ds && commanderUsable(g, p, "veldyr")) {
        bool owned = false;
        for (int16_t b : g.dsBranch) {
            const int q = b >= 0 ? g.planets[size_t(b)].owner : -1;
            owned = owned || (q >= 0 && q != p && g.players[size_t(q)].hasTech(t));
        }
        if (owned) {
            int top = 0;
            for (int c = 1; c < 4; ++c)
                if (missing[c] > missing[top]) top = c;
            if (missing[top] > 0) --missing[top];
        }
    }
}

bool techAvailableToResearch(const Game& g, int p, int t) {
    const TechDef& d = kTechs[t];
    const Player& pl = g.players[p];
    if (pl.hasTech(t)) return false;
    if (d.faction >= 0 && d.faction != pl.faction) return false;
    if (d.color == TechColor::None && !d.unitUpgrade) return false;  // Nekro's Valefar Assimilators
    if (!std::strcmp(d.key, "iihq")) return false;  // the Codex Keleres technology: not in this game (0.21.1)
    // the Obsidian sides of the Firmament's technologies can't be researched (0.44.5, fixes 9: not through Book of
    // Latvinia, Dangerous Wilds or any other way either)
    if (g.fixes >= 9 && (!std::strcmp(d.key, "planesplitter-obs") || !std::strcmp(d.key, "parasite-obs"))) return false;
    if (!g.purgedTechs.empty() && std::find(g.purgedTechs.begin(), g.purgedTechs.end(), int16_t(t)) != g.purgedTechs.end()) return false;  // Ta Zern
    if (d.faction < 0) {
        // a faction upgrade replaces the generic one
        for (int i = 0; i < kTechCount; ++i)
            if (kTechs[i].faction == pl.faction && kTechs[i].replaces == t) return false;
        // War Sun for Muaat is replaced by their Prototype War Sun II path
    }
    return true;
}

std::vector<int> skipPlanets(const Game& g, int p, TechColor c) {
    std::vector<int> v;
    if (isFaction(g, p, "lanefir")) return v;  // A New Edifice (Discordant Stars): no technology specialties
    const bool twice = isFaction(g, p, "zealots");  // Ancient Knowledge (Discordant Stars): each specialty covers 2
    // Psychoarchaeology: specialties work without exhausting, even on exhausted planets.
    for (int pl : owns(g, p, "pa") ? controlledPlanets(g, p) : readyPlanets(g, p))
        if ((planetSkips(g, pl) >> int(c)) & 1) {
            v.push_back(pl);
            const PlanetDef& d = kPlanets[g.planets[size_t(pl)].def];
            if (d.skip == c && d.skip2 == c) v.push_back(pl);  // Thunder's Edge: two of one colour (Tiamat) count twice
            if (twice) v.push_back(pl);
        }
    return v;
}

bool canResearchNow(const Game& g, int p, int t) {
    if (!techAvailableToResearch(g, p, t)) return false;
    int missing[4];
    missingPrereqs(g, p, t, missing);
    int short_ = 0;
    if (allSynergy(g, p)) {  // The Quantumcore: specialties of any colour
        int miss = 0;
        for (int c = 0; c < 4; ++c) miss += missing[c];
        int skips = 0;
        for (int x : owns(g, p, "pa") ? controlledPlanets(g, p) : readyPlanets(g, p))
            for (int c = 0; c < 4; ++c) skips += (planetSkips(g, x) >> c) & 1;
        return std::max(0, miss - skips) <= analyticalWaiver(g, p, t);
    }
    TechColor sa, sb;
    const bool syn = synergyOf(g, p, sa, sb);  // Thunder's Edge: the pair's specialties pool together
    for (int c = 0; c < 4; ++c) {
        if (syn && (c == int(sa) || c == int(sb))) continue;
        short_ += std::max(0, missing[c] - int(skipPlanets(g, p, TechColor(c)).size()));
    }
    if (syn)
        short_ += std::max(0, missing[int(sa)] + missing[int(sb)] - int(skipPlanets(g, p, sa).size()) - int(skipPlanets(g, p, sb).size()));
    return short_ <= analyticalWaiver(g, p, t);
}

// ============================================================ factions, leaders, notes
bool isFaction(const Game& g, int p, const char* key) {
    if (p < 0 || p >= g.numPlayers) return false;
    int f = g.players[p].faction;
    if (f < 0 || f >= kFactionCount) return false;
    const char* fk = kFactions[f].key;
    return (fk == key || (fk[0] == key[0] && !std::strcmp(fk, key))) && factionImplemented(f);
}

int leaderIndex(const Game& g, int p, int type, int nth) {
    if (p < 0 || p >= g.numPlayers) return -1;
    const auto& ls = g.players[p].leaders;
    for (size_t i = 0; i < ls.size(); ++i)
        if (kLeaders[ls[i].leader].type == type && nth-- == 0) return int(i);
    return -1;
}

int leaderStatus(const Game& g, int p, int type, int nth) {
    int i = leaderIndex(g, p, type, nth);
    return i < 0 ? int(kLocked) : int(g.players[p].leaders[i].status);
}

bool agentReady(const Game& g, int p, int nth) { return leaderStatus(g, p, kAgent, nth) == kReady && !g.players[p].eliminated; }

bool heroUnlocked(const Game& g, int p) { return leaderStatus(g, p, kHero) == kReady && !g.players[p].eliminated; }

int playerOfFaction(const Game& g, const char* key) {
    for (int q = 0; q < g.numPlayers; ++q)
        if (isFaction(g, q, key) && !g.players[q].eliminated) return q;
    return -1;
}

bool commanderUsable(const Game& g, int p, const char* factionKey) {
    if (p < 0 || p >= g.numPlayers || g.players[p].eliminated) return false;
    int owner = playerOfFaction(g, factionKey);
    if (owner < 0 && !g.players[p].yinAlliances.empty() && breakthroughIs(g, p, "yin"))  // Yin Ascendant (Thunder's Edge)
        for (int8_t f : g.players[p].yinAlliances)
            if (f >= 0 && f < kFactionCount && !std::strcmp(kFactions[f].key, factionKey)) return true;
    if (owner < 0)  // Mercenaries for Hire / Minor Factions (galactic events): an Alliance of a faction not in play
        for (int8_t f : g.players[p].alliances)
            if (f >= 0 && f < kFactionCount && !std::strcmp(kFactions[f].key, factionKey)) return true;
    if (owner < 0 || leaderStatus(g, owner, kCommander) != kReady) return false;
    if (g.orthodoxy >= 0 && g.players[size_t(owner)].faction == g.orthodoxy) return true;  // Zealous Orthodoxy: everyone
    if (owner == p) return true;
    if (((g.players[p].edict >> owner) & 1) && isFaction(g, p, "mahact")) return true;  // Imperia (Mahact)
    for (int16_t n : g.players[p].notesInPlay)  // Alliance
        if (noteOwner(n) == owner && (noteType(n) == kNoteAlliance || (noteType(n) == kNoteFaction && !std::strcmp(factionKey, "celdauri"))))
            return true;  // (Trade Alliance, the Celdauri's note, is one too)
    return false;
}

bool heroActive(const Game& g, int p, const char* factionKey) {
    if (p < 0 || p >= g.numPlayers) return false;  // (Thunder's Edge: not for neutral units)
    return isFaction(g, p, factionKey) && g.players[p].heroRound == g.round && g.round > 0;
}

int sustainWorth(const Game& g, int p) { return owns(g, p, "nes") ? 2 : 1; }

bool flagshipIn(const Game& g, int p, int sys) { return sys >= 0 && g.systems[sys].space[p].flagships() > 0; }  // (0.52.0: either)

bool shieldsDown(const Game& g, int owner, int sys) {
    if (noAbilities(g, owner, sys)) return true;  // Thunder's Edge: no Planetary Shield inside an entropic scar (the Quietus)
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != owner && flagshipAbility(g, q, "letnev") && flagshipIn(g, q, sys)) return true;  // Arc Secundus
    return false;
}

bool immuneToDirectHit(const Game& g, int p, UnitType t) {
    const char* k = unitDef(g, p, t).key;
    return !std::strcmp(k, "sardakk_dreadnought2") || !std::strcmp(k, "l1z1x_dreadnought2") || !std::strcmp(k, "cheiran_dreadnought2") ||
           !std::strcmp(k, "veldyr_dreadnought2") ||  // (Lancer Dreadnought II, Discordant Stars)
           !std::strcmp(k, "tribune3") ||             // (the Tribune - The Queens' Wrath, 0.43.0)
           // (fixes 27, the shared technologies - owner 2026-11-14) the generic Dreadnought II: "This unit cannot be
           // destroyed by 'Direct Hit' action cards" (only the faction dreadnoughts were listed)
           (g.fixes >= 27 && !std::strcmp(k, "dreadnought2"));
}

bool holdsNote(const Game& g, int p, int owner, int type) {
    if (p < 0 || p >= g.numPlayers) return false;  // (Thunder's Edge: not for neutral units)
    int16_t n = makeNote(owner, type);
    for (int16_t x : g.players[p].notes)
        if (x == n) return true;
    return false;
}

std::string noteNameOf(const Game& g, int note) {
    int type = noteType(note);
    if (type < kNoteTypes) return noteName(type);
    int owner = noteOwner(note);
    return owner >= 0 && owner < g.numPlayers ? factionNoteName(g.players[owner].faction, type) : "";
}

std::string noteTextOf(const Game& g, int note) {
    int type = noteType(note);
    if (type < kNoteTypes) return noteText(type);
    int owner = noteOwner(note);
    if (owner < 0 || owner >= g.numPlayers) return "";
    if ((g.extras & 32) && type == kNoteFaction && isFaction(g, owner, "hacan"))  // 0.32.0: played as an action
        return "ACTION: Place this card faceup in your play area. While this card is in your play area, you may negotiate "
               "transactions with players who are not your neighbor. If you activate a system that contains 1 or more of the "
               "Hacan player's units, return this card to the Hacan player.";  // (the Hacan review: the card's wording)
    if ((g.extras & 64) && type == kNoteFaction && isFaction(g, owner, "empyrean"))  // 0.32.1
        return "ACTION: Place this card faceup in your play area. When you give a number of commodities to the Empyrean player equal "
               "to your maximum commodity value, you each gain 1 trade good. If you activate a system that contains 1 or more of the "
               "Empyrean player's units, return this card to the Empyrean player.";  // (the Empyrean review: the card's wording)
    return factionNoteText(g.players[owner].faction, type);
}

// Sdallari Tvungovot (Kjalengard commander, Discordant Stars): researching a unit upgrade, each unit upgrade they own
// covers 1 missing prerequisite of a colour that both ask for. Takes them off 'missing'; returns how many.
int upgradeWaiversApply(const Game& g, int p, int tech, int missing[4]) {
    if (!(g.modules & kModDiscordant) || tech < 0 || !kTechs[tech].unitUpgrade || !isFaction(g, p, "kjalengard") || !commanderUsable(g, p, "kjalengard"))
        return 0;
    int n = 0;
    for (int16_t u : g.players[size_t(p)].techs) {
        if (!kTechs[u].unitUpgrade) continue;
        for (int c = 0; c < 4; ++c)
            if (kTechs[u].req[c] > 0 && kTechs[tech].req[c] > 0 && missing[c] > 0) {
                --missing[c];
                ++n;
                break;
            }
    }
    return n;
}
// The unit type a unit upgrade technology upgrades (-1 if it isn't one).
int upgradeUnitType(int tech) {
    if (tech < 0 || tech >= kTechCount || !kTechs[tech].unitUpgrade) return -1;
    for (int u = 0; u < kUnitCount; ++u)
        if (kUnits[u].tech == tech) return int(kUnits[u].type);
    return -1;
}
int upgradeWaivers(const Game& g, int p, int tech) {
    if (!(g.modules & kModDiscordant) || tech < 0 || !kTechs[tech].unitUpgrade) return 0;
    int missing[4];
    missingPrereqs(g, p, tech, missing);
    return upgradeWaiversApply(g, p, tech, missing);
}

int analyticalWaiver(const Game& g, int p, int tech) {
    int extra = 0;
    if ((g.modules & kModDiscordant) && tech >= 0) {
        const int z = playerOfFaction(g, "zealots");
        if (z >= 0 && z != p && holdsNote(g, p, z, kNoteFaction)) ++extra;  // Favor of Rhodun
        if (isFaction(g, p, "zealots") && commanderUsable(g, p, "zealots"))   // Bishop Ulin
            for (int x : readyPlanets(g, p))
                if (planetHasSkip(g, x)) {
                    ++extra;
                    break;
                }
    }
    if ((g.modules & kModDiscordant) && tech >= 0) extra += upgradeWaivers(g, p, tech);  // Sdallari Tvungovot (Kjalengard commander)
    if ((g.modules & kModDiscordant) && tech >= 0 && breakthroughIs(g, p, "kyro") && !g.players[size_t(p)].actionCards.empty()) ++extra;  // Strain Dispersal
    if (isFaction(g, p, "lanefir") && tech >= 0 && !kTechs[tech].unitUpgrade && fragmentCount(g, p) > 0) return 1 + extra;  // Iconoclasm
    return (isFaction(g, p, "jolnar") && tech >= 0 && !kTechs[tech].unitUpgrade ? 1 : 0) + extra;  // Analytical
}

// ============================================================ objectives
SpendNeed objectiveSpend(int obj) {
    SpendNeed s;
    const char* k = kObjectives[obj].key;
    if (!std::strcmp(k, "monument")) s.resources = 8;
    else if (!std::strcmp(k, "sway_council")) s.influence = 8;
    else if (!std::strcmp(k, "trade_routes")) s.tradeGoods = 5;
    else if (!std::strcmp(k, "lead")) s.tokens = 3;
    else if (!std::strcmp(k, "amass_wealth")) s.resources = s.influence = s.tradeGoods = 3;
    else if (!std::strcmp(k, "centralize_trade")) s.tradeGoods = 10;
    else if (!std::strcmp(k, "golden_age")) s.resources = 16;
    else if (!std::strcmp(k, "galvanize")) s.tokens = 6;
    else if (!std::strcmp(k, "manipulate_law")) s.influence = 16;
    else if (!std::strcmp(k, "vast_reserves")) s.resources = s.influence = s.tradeGoods = 6;
    return s;
}

bool hasScored(const Game& g, int p, int obj) {
    if (p < 0 || p >= g.numPlayers) return false;  // (Thunder's Edge: not for neutral units)
    for (int16_t o : g.players[p].scored)
        if (o == obj) return true;
    return false;
}

namespace {
// Can the player cover R resources and I influence from ready planets (each
// planet used for one or the other) plus trade goods, keeping tgNeed spare?
bool canPay(const Game& g, int p, int R, int I, int tgNeed) {
    int tg = g.players[p].tradeGoods - tgNeed;
    if (tg < 0) return false;
    const int w = tgWorth(g, p);  // Mirror Computing: a trade good covers 2 of one kind
    std::vector<int> pl = readyPlanets(g, p);
    // reachable[r][i] with r <= R, i <= I
    std::vector<std::vector<char>> reach(R + 1, std::vector<char>(I + 1, 0)), next;
    reach[0][0] = 1;
    for (int x : pl) {
        next = reach;
        int pr = planetResources(g, x), pi = planetInfluence(g, x);
        for (int r = 0; r <= R; ++r)
            for (int i = 0; i <= I; ++i) {
                if (!reach[r][i]) continue;
                next[std::min(R, r + pr)][i] = 1;
                next[r][std::min(I, i + pi)] = 1;
            }
        reach.swap(next);
    }
    for (int r = 0; r <= R; ++r)
        for (int i = 0; i <= I; ++i)
            if (reach[r][i] && (R - r + w - 1) / w + (I - i + w - 1) / w <= tg) return true;
    return false;
}

int controlledCount(const Game& g, int p, bool (*pred)(const Game&, int, int), int arg) {
    int n = 0;
    for (int pl : controlledPlanets(g, p))
        if (!isStation(g, pl) && pred(g, pl, arg)) ++n;  // (space stations don't count for objectives)
    return n;
}
int objectivePlanetCount(const Game& g, int p) {
    int n = 0;
    for (int pl : controlledPlanets(g, p))
        if (!isStation(g, pl)) ++n;
    return n;
}
bool withTrait(const Game& g, int pl, int trait) { return hasTraitOf(g, pl, Trait(trait)); }
bool notHome(const Game& g, int pl, int) { return !isHomePlanet(g, pl); }
bool withSkip(const Game& g, int pl, int) { return planetHasSkip(g, pl); }

int maxSameTrait(const Game& g, int p) {
    int best = 0;
    for (int t = 1; t <= 3; ++t) best = std::max(best, controlledCount(g, p, withTrait, t));
    return best;
}

int upgradesOwned(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return 0;  // (Thunder's Edge: not for neutral units)
    int n = 0;
    for (int16_t t : g.players[p].techs)
        if (kTechs[t].unitUpgrade) ++n;
    return n;
}

}  // namespace

// ============================================================ Thunder's Edge: breakthroughs and synergy (0.34.0)
// 0.43.0 (owner D1): with Discordant Stars too (its factions get their own) - new games only
bool breakthroughsOn(const Game& g) { return (g.modules & kModThundersEdge) && (!(g.modules & kModDiscordant) || g.fixes >= 2); }
bool hasBreakthrough(const Game& g, int p) {
    return p >= 0 && p < g.numPlayers && breakthroughsOn(g) && g.players[size_t(p)].breakthrough && !g.players[size_t(p)].eliminated;
}
bool breakthroughIs(const Game& g, int p, const char* factionKey) { return hasBreakthrough(g, p) && isFaction(g, p, factionKey); }
bool allSynergy(const Game& g, int p) {  // The Quantumcore (Thunder's Edge relic): synergy for every colour
    return p >= 0 && p < g.numPlayers && (g.modules & kModThundersEdge) && relicHeld(g, p, "quantumcore");
}
const BreakthroughDef* breakthroughFor(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return nullptr;
    const int f = g.players[size_t(p)].faction;
    if (f < 0 || f >= kFactionCount) return nullptr;
    if (g.fixes < 8 && !std::strcmp(kFactions[f].key, "ghost")) return nullptr;  // (the table said "creuss" until 0.44.4)
    return breakthroughOf(kFactions[f].key);
}
bool synergyOf(const Game& g, int p, TechColor& a, TechColor& b) {
    a = b = TechColor::None;
    if (!hasBreakthrough(g, p)) return false;
    const BreakthroughDef* bt = breakthroughFor(g, p);
    if (!bt || bt->a == TechColor::None || bt->b == TechColor::None) return false;
    a = bt->a;
    b = bt->b;
    return true;
}
// Technologies per colour for "N technologies of each of M colours" objectives: with synergy, the pair's
// technologies are split between the two colours as suits the threshold k best (each counts as one of them).
void techColourCounts(const Game& g, int p, int k, int out[4]) {
    for (int c = 0; c < 4; ++c) out[c] = techCount(g, p, TechColor(c));
    if (allSynergy(g, p)) {  // every coloured technology counts as any colour: fill the colours up to k in turn
        int total = out[0] + out[1] + out[2] + out[3];
        for (int c = 0; c < 4; ++c) {
            out[c] = std::min(k, total);
            total -= out[c];
        }
        out[3] += total;
        return;
    }
    TechColor a, b;
    if (!synergyOf(g, p, a, b)) return;
    const int ia = int(a), ib = int(b), total = out[ia] + out[ib];
    int best = -1, bx = out[ia];
    for (int x = 0; x <= total; ++x) {
        const int v = std::min(k, x) + std::min(k, total - x);
        if (v > best) best = v, bx = x;
    }
    out[ia] = bx;
    out[ib] = total - bx;
}
int bestColourCount(const Game& g, int p) {
    int best = 0;
    if (allSynergy(g, p)) {
        for (int c = 0; c < 4; ++c) best += techCount(g, p, TechColor(c));
        return best;
    }
    for (int c = 0; c < 4; ++c) best = std::max(best, techCount(g, p, TechColor(c)));
    TechColor a, b;
    if (synergyOf(g, p, a, b)) best = std::max(best, techCount(g, p, a) + techCount(g, p, b));
    return best;
}

namespace {
int colorsWithAtLeast(const Game& g, int p, int k) {
    int n = 0, cnt[4];
    techColourCounts(g, p, k, cnt);  // (Thunder's Edge synergy)
    for (int c = 0; c < 4; ++c)
        if (cnt[c] >= k) ++n;
    return n;
}

int systemsWithShips(const Game& g, int p, bool (*pred)(const Game&, int)) {
    int n = 0;
    for (size_t s = 0; s < g.systems.size(); ++s)
        if (hasShips(g, p, int(s)) && (!pred || pred(g, int(s)))) ++n;
    return n;
}
int systemsWithUnits(const Game& g, int p, bool (*pred)(const Game&, int)) {
    int n = 0;
    for (size_t s = 0; s < g.systems.size(); ++s)
        if (hasUnits(g, p, int(s)) && pred(g, int(s))) ++n;
    return n;
}
bool noPlanets(const Game& g, int s) { return g.systems[s].planets.empty() && !g.systems[s].isHyperlane(); }
bool isLegendaryOrAnomaly(const Game& g, int s) {
    const SystemState& sy = g.systems[s];
    if (isAnomaly(g, s)) return true;
    if (!std::strcmp(sy.d().key, "18")) return true;
    for (int16_t pl : sy.planets)
        if (kPlanets[g.planets[pl].def].legendary) return true;
    return false;
}
int edgeSystemsWithUnits(const Game& g, int p) {
    int n = 0;
    for (size_t s = 0; s < g.systems.size(); ++s)
        if (g.systems[s].edge && g.systems[s].home != p && hasUnits(g, p, int(s))) ++n;
    return n;
}
bool nextToAnomaly(const Game& g, int s) {
    for (int16_t a : g.systems[s].adj)
        if (isAnomaly(g, a)) return true;
    return false;
}

int maxNonFighterShips(const Game& g, int p) {
    int best = 0;
    for (const SystemState& s : g.systems) best = std::max(best, s.space[p].nonFighterShips());
    return best;
}

int planetsWithStructuresOutsideHome(const Game& g, int p) {
    int n = 0;
    for (const PlanetState& pl : g.planets)
        if (pl.units[p].structures() > 0 && g.systems[pl.system].home != p) ++n;
    return n;
}

int controlledOutsideHomeSum(const Game& g, int p, bool influence) {
    int sum = 0;
    for (int pl : controlledPlanets(g, p))
        if (g.fixes < 25 || !isStation(g, pl))  // (fixes 25: space stations aren't planets for objectives - TE rulebook)
            sum += influence ? planetInfluence(g, pl) : planetResources(g, pl);
    return sum;
}

bool flagshipOrWarSunWhere(const Game& g, int p, bool onlyEnemyHomeOrMecatol) {
    for (size_t s = 0; s < g.systems.size(); ++s) {
        const Force& f = g.systems[s].space[p];
        if (!f.flagships() && !f.count(UnitType::WarSun)) continue;
        if (!onlyEnemyHomeOrMecatol) return true;
        int h = g.systems[s].home;
        if ((h >= 0 && h != p) || !std::strcmp(g.systems[s].d().key, "18")) return true;
    }
    return false;
}

bool ownsPlanetInOtherHome(const Game& g, int p) {
    for (int pl : controlledPlanets(g, p)) {
        if (isStation(g, pl)) continue;
        int h = g.systems[g.planets[pl].system].home;
        if (h >= 0 && h != p) return true;
    }
    return false;
}

bool distantLands(const Game& g, int p) {
    // two planets, each in or next to a *different* other player's home system
    std::vector<std::set<int>> homesFor;
    for (int pl : controlledPlanets(g, p)) {
        if (isStation(g, pl)) continue;
        std::set<int> hs;
        int s = g.planets[pl].system;
        int h = g.systems[s].home;
        if (h >= 0 && h != p) hs.insert(h);
        for (int16_t a : g.systems[s].adj) {
            int ha = g.systems[a].home;
            if (ha >= 0 && ha != p) hs.insert(ha);
        }
        if (!hs.empty()) homesFor.push_back(hs);
    }
    for (size_t i = 0; i < homesFor.size(); ++i)
        for (size_t j = i + 1; j < homesFor.size(); ++j)
            for (int a : homesFor[i])
                for (int b : homesFor[j])
                    if (a != b) return true;
    return false;
}

bool pushBoundaries(const Game& g, int p) {
    int mine = objectivePlanetCount(g, p), beaten = 0;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && areNeighbours(g, p, q) && mine > objectivePlanetCount(g, q)) ++beaten;
    return beaten >= 2;
}
}  // namespace

bool canPayBoth(const Game& g, int p, int resources, int influence) { return canPay(g, p, resources, influence, 0); }

bool meetsObjective(const Game& g, int p, int obj) {
    if (kObjectives[obj].kind == 3 && (g.modules & kModThundersEdge) && tlAroz != p && commanderUsable(g, p, "firmament")) {
        struct Guard {
            int old;
            ~Guard() { tlAroz = old; }
        } guard{tlAroz};
        tlAroz = p;
        return meetsObjective(g, p, obj);
    }
    SpendNeed s = objectiveSpend(obj);
    if (s.any()) {
        if (s.tokens) return g.players[p].tactic + g.players[p].strategy >= s.tokens;
        return canPay(g, p, s.resources, s.influence, s.tradeGoods);
    }
    ObjCoexist coexistCounts;  // (T22: coexisting counts as controlling, for objectives - not for spending)
    const Player& pl = g.players[p];
    if (keyIs(obj, "corner")) return maxSameTrait(g, p) >= 4;
    if (keyIs(obj, "unify_colonies")) return maxSameTrait(g, p) >= 6;
    if (keyIs(obj, "develop")) return upgradesOwned(g, p) >= 2;
    if (keyIs(obj, "revolutionize")) return upgradesOwned(g, p) >= 3;
    if (keyIs(obj, "diversify")) return colorsWithAtLeast(g, p, 2) >= 2;
    if (keyIs(obj, "master_science")) return colorsWithAtLeast(g, p, 2) >= 4;
    if (keyIs(obj, "expand_borders")) return controlledCount(g, p, notHome, 0) >= 6;
    if (keyIs(obj, "subdue")) return controlledCount(g, p, notHome, 0) >= 11;
    if (keyIs(obj, "research_outposts")) return controlledCount(g, p, withSkip, 0) >= 3;
    if (keyIs(obj, "brain_trust")) return controlledCount(g, p, withSkip, 0) >= 5;
    if (keyIs(obj, "intimidate")) {
        int m = g.mecatol(), n = 0;
        if (m < 0) return false;
        for (int16_t a : g.systems[m].adj)
            if (hasShips(g, p, a)) ++n;
        return n >= 2;
    }
    if (keyIs(obj, "conquer")) return ownsPlanetInOtherHome(g, p);
    if (keyIs(obj, "build_defenses")) return structuresOf(g, p) >= 4;
    if (keyIs(obj, "massive_cities")) return structuresOf(g, p) >= 7;
    if (keyIs(obj, "engineer_marvel")) return flagshipOrWarSunWhere(g, p, false);
    if (keyIs(obj, "supremacy")) return flagshipOrWarSunWhere(g, p, true);
    if (keyIs(obj, "deep_space")) return systemsWithUnits(g, p, noPlanets) >= 3;
    if (keyIs(obj, "vast_territories")) return systemsWithUnits(g, p, noPlanets) >= 5;
    if (keyIs(obj, "infrastructure")) return planetsWithStructuresOutsideHome(g, p) >= 3;
    if (keyIs(obj, "protect_border")) return planetsWithStructuresOutsideHome(g, p) >= 5;
    if (keyIs(obj, "make_history")) return systemsWithUnits(g, p, isLegendaryOrAnomaly) >= 2;
    if (keyIs(obj, "become_legend")) return systemsWithUnits(g, p, isLegendaryOrAnomaly) >= 4;
    if (keyIs(obj, "outer_rim")) return edgeSystemsWithUnits(g, p) >= 3;
    if (keyIs(obj, "control_borderlands")) return edgeSystemsWithUnits(g, p) >= 5;
    if (keyIs(obj, "push_boundaries")) return pushBoundaries(g, p);
    if (keyIs(obj, "raise_fleet")) return maxNonFighterShips(g, p) >= 5;
    if (keyIs(obj, "command_armada")) return maxNonFighterShips(g, p) >= 8;
    if (keyIs(obj, "distant_lands")) return distantLands(g, p);
    // --- secrets (status phase) ---
    if (keyIs(obj, "ans")) {
        int n = 0;
        for (int16_t t : pl.techs)
            if (kTechs[t].faction >= 0 && kTechs[t].color != TechColor::None) ++n;
            else if (kTechs[t].faction >= 0 && kTechs[t].unitUpgrade) ++n;
        return n >= 2;
    }
    if (keyIs(obj, "btgk")) {
        bool a = false, b = false;
        for (size_t s = 0; s < g.systems.size(); ++s) {
            if (!hasShips(g, p, int(s))) continue;
            if (wormholesOf(g, s) & kAlpha) a = true;
            if (wormholesOf(g, s) & kBeta) b = true;
        }
        return a && b;
    }
    if (keyIs(obj, "ctr")) return systemsWithShips(g, p, nullptr) >= 6;
    if (keyIs(obj, "csl")) {
        for (size_t s = 0; s < g.systems.size(); ++s) {
            if (!hasShips(g, p, int(s))) continue;
            for (int q = 0; q < g.numPlayers; ++q) {
                if (q == p) continue;
                if (g.systems[s].space[q].count(UnitType::SpaceDock)) return true;
                for (int16_t x : g.systems[s].planets)
                    if (g.planets[x].units[q].count(UnitType::SpaceDock)) return true;
            }
        }
        return false;
    }
    if (keyIs(obj, "eap")) return unitsOnBoard(g, p, UnitType::PDS) >= 4;
    if (keyIs(obj, "faa")) return controlledCount(g, p, withTrait, int(Trait::Cultural)) >= 4;
    if (keyIs(obj, "mrm")) return controlledCount(g, p, withTrait, int(Trait::Hazardous)) >= 4;
    if (keyIs(obj, "mp")) return controlledCount(g, p, withTrait, int(Trait::Industrial)) >= 4;
    if (keyIs(obj, "fwm")) return unitsOnBoard(g, p, UnitType::SpaceDock) >= 3;
    if (keyIs(obj, "gamf")) return unitsOnBoard(g, p, UnitType::Dreadnought) >= 5;
    if (keyIs(obj, "lsc")) return systemsWithShips(g, p, nextToAnomaly) >= 3;
    if (keyIs(obj, "mlp")) {
        for (int c = 0; c < 4; ++c)
            if (techCount(g, p, TechColor(c)) >= 4 || bestColourCount(g, p) >= 4) return true;
        return false;
    }
    if (keyIs(obj, "ose")) {
        int m = g.mecatol();
        if (m < 0) return false;
        bool ctrl = false;
        if (g.fixes >= 25) {  // (fixes 25, owner's faction debug pass) as every other control check: Captain Aroz, coexisting (T22)
            for (int x : controlledPlanets(g, p))
                if (g.planets[size_t(x)].system == m && !isStation(g, x)) ctrl = true;
        } else {
            for (int16_t x : g.systems[m].planets)
                if (g.planets[x].owner == p) ctrl = true;
        }
        return ctrl && g.systems[m].space[p].ships() >= 3;
    }
    if (keyIs(obj, "te")) {
        for (size_t s = 0; s < g.systems.size(); ++s) {
            if (!hasShips(g, p, int(s))) continue;
            for (int16_t a : g.systems[s].adj) {
                int h = g.systems[a].home;
                if (h >= 0 && h != p) return true;
            }
        }
        return false;
    }
    if (keyIs(obj, "eh")) return controlledOutsideHomeSum(g, p, true) >= 12;
    if (keyIs(obj, "hrm")) return controlledOutsideHomeSum(g, p, false) >= 12;
    if (keyIs(obj, "fc")) {
        for (int q = 0; q < g.numPlayers; ++q)  // (0.43.0 debug passes: eliminated players no longer count, as the
            if (q != p && !areNeighbours(g, p, q) && !(g.fixes >= 2 && g.players[q].eliminated)) return false;  // progress shows)
        return true;
    }
    if (keyIs(obj, "fsn")) return pl.actionCards.size() >= 5;  // discard 5 action cards
    if (keyIs(obj, "dhw")) return fragmentCount(g, p) >= 2;    // purge 2 relic fragments
    if (keyIs(obj, "lost_outposts")) return attachedPlanets(g, p) >= 2;
    if (keyIs(obj, "ancient_monuments")) return attachedPlanets(g, p) >= 3;
    if (keyIs(obj, "dfat")) return g.nexus >= 0 && hasUnits(g, p, g.nexus);
    if (keyIs(obj, "sb")) {
        for (int16_t n : pl.notesInPlay)
            if (noteOwner(n) != p) return true;
        return false;
    }
    if (keyIs(obj, "dp")) return g.laws.size() >= 3;
    if (keyIs(obj, "mtm")) {
        int n = 0;
        for (const PlanetState& x : g.planets)
            if (x.units[p].count(UnitType::Mech) >= 1) ++n;
        return n >= 4;
    }
    if (keyIs(obj, "otf")) {
        for (const PlanetState& x : g.planets)
            if (x.units[p].groundForces() >= 9 && !x.units[p].count(UnitType::SpaceDock)) return true;
        return false;
    }
    if (keyIs(obj, "pem")) {
        for (size_t s = 0; s < g.systems.size(); ++s)
            if (productionCapacity(g, p, int(s)) >= 8) return true;
        return false;
    }
    if (keyIs(obj, "sai")) {
        for (int x : controlledPlanets(g, p))
            if (isLegendaryPlanet(g, x)) return true;
        return false;
    }
    if (keyIs(obj, "syc")) {
        for (int x : controlledPlanets(g, p)) {
            if (g.fixes >= 25 && isStation(g, x)) continue;  // (fixes 25: space stations aren't planets for objectives)
            for (int16_t y : g.systems[g.planets[x].system].planets)
                if (g.planets[y].owner >= 0 && g.planets[y].owner != p && !(g.fixes >= 25 && isStation(g, y))) return true;
        }
        return false;
    }
    // Action-phase secrets (dtgs, mew, sar, ttfd, uf, btv, dts, dyp, fwp, pe, bam)
    // are scored by the rules when the event happens, not checked here.
    return false;
}

static bool g_progressAroz = false;  // (the bots' objective planning sees Captain Aroz - AI only; set by the AI)
void setProgressAroz(bool on) { g_progressAroz = on; }
ObjProgress objectiveProgress(const Game& g, int p, int obj) {
    if (g_progressAroz && kObjectives[obj].kind == 3 && (g.modules & kModThundersEdge) && tlAroz != p && commanderUsable(g, p, "firmament")) {
        struct Guard {
            int old;
            ~Guard() { tlAroz = old; }
        } guard{tlAroz};
        tlAroz = p;
        return objectiveProgress(g, p, obj);
    }
    ObjCoexist coexistCounts;  // (T22: coexisting counts as controlling, for objectives)
    ObjProgress r;
    auto set = [&](int have, int need) {
        r.have = std::min(have, need);
        r.need = need;
    };
    const Player& pl = g.players[p];
    auto flagOrWs = [&](bool where) { return flagshipOrWarSunWhere(g, p, where) ? 1 : 0; };
    auto mecatolAdjShips = [&]() {
        int m = g.mecatol(), n = 0;
        if (m >= 0)
            for (int16_t a : g.systems[m].adj)
                if (hasShips(g, p, a)) ++n;
        return n;
    };
    auto colorSum = [&](int colors) {  // techs toward "2 in each of N colours", best colours first
        int c[4];
        techColourCounts(g, p, 2, c);  // (Thunder's Edge synergy)
        for (int k = 0; k < 4; ++k) c[k] = std::min(2, c[k]);
        std::sort(c, c + 4, [](int a, int b) { return a > b; });
        int s = 0;
        for (int k = 0; k < colors; ++k) s += c[k];
        return s;
    };
    if (keyIs(obj, "corner")) set(maxSameTrait(g, p), 4);
    else if (keyIs(obj, "unify_colonies")) set(maxSameTrait(g, p), 6);
    else if (keyIs(obj, "develop")) set(upgradesOwned(g, p), 2);
    else if (keyIs(obj, "revolutionize")) set(upgradesOwned(g, p), 3);
    else if (keyIs(obj, "diversify")) set(colorSum(2), 4);
    else if (keyIs(obj, "master_science")) set(colorSum(4), 8);
    else if (keyIs(obj, "expand_borders")) set(controlledCount(g, p, notHome, 0), 6);
    else if (keyIs(obj, "subdue")) set(controlledCount(g, p, notHome, 0), 11);
    else if (keyIs(obj, "research_outposts")) set(controlledCount(g, p, withSkip, 0), 3);
    else if (keyIs(obj, "brain_trust")) set(controlledCount(g, p, withSkip, 0), 5);
    else if (keyIs(obj, "intimidate")) set(mecatolAdjShips(), 2);
    else if (keyIs(obj, "build_defenses")) set(structuresOf(g, p), 4);
    else if (keyIs(obj, "massive_cities")) set(structuresOf(g, p), 7);
    else if (keyIs(obj, "lost_outposts")) set(attachedPlanets(g, p), 2);
    else if (keyIs(obj, "ancient_monuments")) set(attachedPlanets(g, p), 3);
    else if (keyIs(obj, "engineer_marvel")) set(flagOrWs(false), 1);
    else if (keyIs(obj, "supremacy")) set(flagOrWs(true), 1);
    else if (keyIs(obj, "deep_space")) set(systemsWithUnits(g, p, noPlanets), 3);
    else if (keyIs(obj, "vast_territories")) set(systemsWithUnits(g, p, noPlanets), 5);
    else if (keyIs(obj, "infrastructure")) set(planetsWithStructuresOutsideHome(g, p), 3);
    else if (keyIs(obj, "protect_border")) set(planetsWithStructuresOutsideHome(g, p), 5);
    else if (keyIs(obj, "make_history")) set(systemsWithUnits(g, p, isLegendaryOrAnomaly), 2);
    else if (keyIs(obj, "become_legend")) set(systemsWithUnits(g, p, isLegendaryOrAnomaly), 4);
    else if (keyIs(obj, "outer_rim")) set(edgeSystemsWithUnits(g, p), 3);
    else if (keyIs(obj, "control_borderlands")) set(edgeSystemsWithUnits(g, p), 5);
    else if (keyIs(obj, "raise_fleet")) set(maxNonFighterShips(g, p), 5);
    else if (keyIs(obj, "command_armada")) set(maxNonFighterShips(g, p), 8);
    else if (keyIs(obj, "conquer")) set(ownsPlanetInOtherHome(g, p) ? 1 : 0, 1);
    else if (keyIs(obj, "distant_lands")) {
        std::set<int> homes;
        for (int x : controlledPlanets(g, p)) {
            int s = g.planets[x].system;
            int h = g.systems[s].home;
            if (h >= 0 && h != p) homes.insert(h);
            for (int16_t a : g.systems[s].adj)
                if (g.systems[a].home >= 0 && g.systems[a].home != p) homes.insert(g.systems[a].home);
        }
        set(distantLands(g, p) ? 2 : std::min<int>(1, int(homes.size())), 2);
    } else if (keyIs(obj, "push_boundaries")) {
        int mine = objectivePlanetCount(g, p), beaten = 0;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p && areNeighbours(g, p, q) && mine > objectivePlanetCount(g, q)) ++beaten;
        set(beaten, 2);
    }
    // secrets
    else if (keyIs(obj, "ans")) {
        int n = 0;
        for (int16_t t : pl.techs)
            if (kTechs[t].faction >= 0) ++n;
        set(n, 2);
    } else if (keyIs(obj, "btgk")) {
        bool a = false, b = false;
        for (size_t s = 0; s < g.systems.size(); ++s) {
            if (!hasShips(g, p, int(s))) continue;
            if (wormholesOf(g, s) & kAlpha) a = true;
            if (wormholesOf(g, s) & kBeta) b = true;
        }
        set(int(a) + int(b), 2);
    } else if (keyIs(obj, "ctr")) set(systemsWithShips(g, p, nullptr), 6);
    else if (keyIs(obj, "eap")) set(unitsOnBoard(g, p, UnitType::PDS), 4);
    else if (keyIs(obj, "faa")) set(controlledCount(g, p, withTrait, int(Trait::Cultural)), 4);
    else if (keyIs(obj, "mrm")) set(controlledCount(g, p, withTrait, int(Trait::Hazardous)), 4);
    else if (keyIs(obj, "mp")) set(controlledCount(g, p, withTrait, int(Trait::Industrial)), 4);
    else if (keyIs(obj, "fwm")) set(unitsOnBoard(g, p, UnitType::SpaceDock), 3);
    else if (keyIs(obj, "gamf")) set(unitsOnBoard(g, p, UnitType::Dreadnought), 5);
    else if (keyIs(obj, "lsc")) set(systemsWithShips(g, p, nextToAnomaly), 3);
    else if (keyIs(obj, "mlp")) {
        int best = 0;
        for (int c = 0; c < 4; ++c) best = std::max(best, techCount(g, p, TechColor(c)));
        set(std::max(best, bestColourCount(g, p)), 4);
    } else if (keyIs(obj, "ose")) {
        int m = g.mecatol(), have = 0;
        if (m >= 0) {
            if (g.fixes >= 25) {
                for (int x : controlledPlanets(g, p))
                    if (g.planets[size_t(x)].system == m && !isStation(g, x)) have = 1;
            } else {
                for (int16_t x : g.systems[m].planets)
                    if (g.planets[x].owner == p) have = 1;
            }
            have += std::min(3, g.systems[m].space[p].ships());
        }
        set(have, 4);
    } else if (keyIs(obj, "eh")) set(controlledOutsideHomeSum(g, p, true), 12);
    else if (keyIs(obj, "hrm")) set(controlledOutsideHomeSum(g, p, false), 12);
    else if (keyIs(obj, "fc")) {
        int n = 0, others = 0;
        for (int q = 0; q < g.numPlayers; ++q)
            if (q != p && !g.players[q].eliminated) {
                ++others;
                if (areNeighbours(g, p, q)) ++n;
            }
        set(n, std::max(1, others));
    } else if (keyIs(obj, "fsn")) set(int(pl.actionCards.size()), 5);
    else if (keyIs(obj, "dhw")) set(fragmentCount(g, p), 2);
    else if (keyIs(obj, "dp")) set(int(g.laws.size()), 3);
    else if (keyIs(obj, "mtm")) {
        int n = 0;
        for (const PlanetState& x : g.planets)
            if (x.units[p].count(UnitType::Mech) >= 1) ++n;
        set(n, 4);
    } else if (keyIs(obj, "otf")) {
        int best = 0;
        for (const PlanetState& x : g.planets)
            if (!x.units[p].count(UnitType::SpaceDock)) best = std::max(best, x.units[p].groundForces());
        set(best, 9);
    } else if (keyIs(obj, "pem")) {
        int best = 0;
        for (size_t s = 0; s < g.systems.size(); ++s) best = std::max(best, productionCapacity(g, p, int(s)));
        set(best, 8);
    } else if (keyIs(obj, "te") || keyIs(obj, "csl") || keyIs(obj, "dfat") || keyIs(obj, "sb") || keyIs(obj, "sai") ||
               keyIs(obj, "syc")) {
        set(meetsObjective(g, p, obj) ? 1 : 0, 1);
    }
    return r;
}

// ============================================================ laws and cards
bool lawInPlay(const Game& g, const char* key) {
    if (g.lawsBlank) return false;  // Law's Order (Keleres): every law is blank this turn
    for (const Law& l : g.laws)
        if (!std::strcmp(kAgendas[l.agenda].key, key)) return true;
    return false;
}

int lawOwner(const Game& g, const char* key) {
    if (g.lawsBlank) return -1;
    for (const Law& l : g.laws)
        if (!std::strcmp(kAgendas[l.agenda].key, key)) return l.owner;
    return -1;
}

int actionCardLimit(const Game& g, int p) {
    if (isFaction(g, p, "yssaril")) return 999;  // Crafty: no limit, nothing can change that
    // E6-G0 Network (Uncharted Space relic): 2 more, whatever else applies
    return (lawInPlay(g, "sanctions") ? 3 : 7) + (relicHeld(g, p, "e6-g0_network") ? 2 : 0);
}

bool holdsActionCard(const Game& g, int p, const char* key) {
    if (p < 0 || p >= g.numPlayers) return false;  // (Thunder's Edge: not for neutral units)
    for (int16_t c : g.players[p].actionCards)
        if (!std::strcmp(kActionCards[c].key, key)) return true;
    return false;
}

int voteValue(const Game& g, int p, int planet) {
    if (isStation(g, planet)) return 0;  // space stations don't vote (Thunder's Edge)
    int base = planetInfluence(g, planet);
    if (g.modules & kModDiscordant) {
        if (isFaction(g, p, "khrask")) base = planetResources(g, planet);  // Lithoids: resources instead
        if (isFaction(g, p, "zelian")) {                                    // Biophobic: 1 per planet
            if (g.fixes >= 2) return 1;  // (0.43.0: exactly 1 - no planet bonus changes it)
            base = 1;
        }
    }
    // (fixes 27, the Xxcha review - owner 2026-11-13) Xxekir Grom Omega: "When you exhaust planets, combine the values of their
    // resources and influence" - planets exhausted to vote too (the Codex: "both in and out of the Agenda phase")
    if (g.fixes >= 27 && codexOn(g) && !teXxchaHero(g) && p >= 0 && p < g.numPlayers && isFaction(g, p, "xxcha") && heroUnlocked(g, p))
        base = planetResources(g, planet) + planetInfluence(g, planet);
    return base + (commanderUsable(g, p, "xxcha") ? 1 : 0)  // Elder Qanoj
           + (hasAttachment(g, planet, "councilpreserve") ? 5 : 0);              // Council Preserve (Uncharted Space)
}

bool bannerhalls(const Game& g, int p) {
    return p >= 0 && p < g.numPlayers && (g.modules & kModDiscordant) && !g.dsGlory.empty() && breakthroughIs(g, p, "kjalengard") &&
           g.players[size_t(p)].oceans == 0;
}
int availableVotes(const Game& g, int p) {
    int v = 0;
    for (int x : readyPlanets(g, p)) v += voteValue(g, p, x);
    if (g.fixes >= 2) v += custodiaValue(g, p, true);  // (Custodia Vigilia votes, 0.43.0)
    if (g.fixes >= 13 && bannerhalls(g, p)) v += oceansReady(g, p);  // (Bannerhalls, 0.44.9)
    else if (g.fixes >= 27 && g.players[size_t(p)].oceans > 0) v += oceansReady(g, p);  // (ocean cards - the Deepwrought review)
    return v;
}

bool canPlayActionCards(const Game& g, int p) {
    if (lawOwner(g, "censure") == p) return false;
    // Transparasteel Plating: during its owner's turn, players who have passed can't play action cards
    int a = g.activePlayer;
    // (0.44.5, fixes 9: a Nekro Valefar copy counts too)
    if (g.phase == Phase::Action && a >= 0 && a != p && g.players[p].passed &&
        (g.fixes >= 9 ? a < g.numPlayers && ownsTechKey(g, a, "tp") : g.players[a].hasTech(techByKey("tp"))))
        return false;
    return true;
}

bool ownsWarSunTech(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return false;  // (Thunder's Edge: not for neutral units)
    const Player& pl = g.players[p];
    for (int16_t t : pl.techs)
        for (int u = 0; u < kUnitCount; ++u)
            if (kUnits[u].tech == t && kUnits[u].type == UnitType::WarSun) return true;
    return false;
}

bool unitSustains(const Game& g, int p, UnitType t) {
    if (t == UnitType::WarSun && lawInPlay(g, "schematics")) return false;
    return unitDef(g, p, t).sustain;
}

// Metali Void Shielding (Thunder's Edge relic): each time hits are made against our ships in space, one non-fighter
// ship without Sustain Damage may use it anyway (one per set of hits: maxAbsorbableHits / checkHits / aiHits).
// How many of p's non-fighter ships without Sustain Damage may use it against this set of hits in space: Metali Void
// Shielding (Thunder's Edge relic, 1) and the Splintering Gale (Khrask flagship, Discordant Stars: 2 ships in the combat
// it fights - those already damaged that way count).
int extraSustains(const Game& g, int p, int sys, int planet) {
    if (planet >= 0 || p < 0 || p >= g.numPlayers || sys < 0 || (scarAt(g, sys) && !ignoresScars(g, p))) return 0;
    int n = 0;
    if ((g.modules & kModThundersEdge) && relicHeld(g, p, "metalivoidshielding")) n += 1;
    if ((g.modules & kModDiscordant) && g.dsGale == sys && flagshipAbility(g, p, "khrask") && flagshipIn(g, p, sys)) {
        const Force& f = g.systems[size_t(sys)].space[size_t(p)];
        int used = 0;
        for (int t : kShipIdx)
            if (UnitType(t) != UnitType::Fighter && !unitSustains(g, p, UnitType(t))) used += f.dmg[t];
        n += std::max(0, 2 - used);
    }
    if ((g.modules & kModDiscordant) && g.dsKomatSys == sys && g.dsKomatOwner == p) {  // Komat (Mortheus commander): 1 ship
        const Force& f = g.systems[size_t(sys)].space[size_t(p)];
        int used = 0;
        for (int t : kShipIdx)
            if (UnitType(t) != UnitType::Fighter && !unitSustains(g, p, UnitType(t))) used += f.dmg[t];
        n += std::max(0, 1 - used);
    }
    return n;
}
bool metaliSustain(const Game& g, int p, UnitType t, int sys, int planet) {
    if (t == UnitType::Fighter || int(t) > int(UnitType::WarSun) || unitSustains(g, p, t)) return false;
    return extraSustains(g, p, sys, planet) > 0;
}

// Where p's unit of type t can't use Sustain Damage (whether or not it has it): an entropic scar (the Quietus), another
// player's Fourth Moon in the system, a Mentak mech (Moll Terminus) on the planet
bool sustainBlockedAt(const Game& g, int p, UnitType t, int sys, int planet) {
    if (noAbilities(g, p, sys)) return true;  // Thunder's Edge: no Sustain Damage inside an entropic scar (the Quietus)
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p) continue;
        // Fourth Moon (Mentak flagship): other players' ships in its system can't sustain (0.44.4: Valefar Z too)
        if (planet < 0 && sys >= 0 && flagshipText(g, q, "mentak") && flagshipIn(g, q, sys)) return true;
        if (!isFaction(g, q, "mentak")) continue;
        // Moll Terminus (Mentak mech): other players' ground forces on its planet can't sustain
        if (planet >= 0 && isGroundForce(t) && g.planets[planet].units[q].count(UnitType::Mech) && mechAbilities(g)) return true;
    }
    return false;
}

bool unitSustainsAt(const Game& g, int p, UnitType t, int sys, int planet) {
    // Illusory Duplication (Uncharted Space): non-fighter ships sustain damage this combat round
    if (noAbilities(g, p, sys)) return false;  // Thunder's Edge: no Sustain Damage inside an entropic scar (the Quietus)
    const bool illusory = planet < 0 && ((g.fx.illusory >> p) & 1) && t != UnitType::Fighter && isShip(t);
    // (fixes 27, the Naaz-Rokha review - owner 2026-11-13: "only the front side of the card has sustain damage, the back side
    // in which it transforms to does not") a Z-Grav Eidolon in the space combat has no Sustain Damage; Eidolon Maximum
    // (Absolute Synergy) keeps its own
    const bool zgrav = g.fixes >= 27 && t == UnitType::Mech && planet < 0 && p >= 0 && p < kSides && ((g.zgrav >> p) & 1) && !eidolonShip(g, p);
    if ((!unitSustains(g, p, t) || zgrav) && !illusory) return false;
    return !sustainBlockedAt(g, p, t, sys, planet);
}

// Mirror Computing (0.44.5, fixes 9: a Nekro Valefar copy counts; a Sanctification Field blank doesn't)
int tgWorth(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return 1;
    return (g.fixes >= 9 ? ownsTechKey(g, p, "mc") : g.players[size_t(p)].hasTech(techByKey("mc"))) ? 2 : 1;
}

}  // namespace ti
