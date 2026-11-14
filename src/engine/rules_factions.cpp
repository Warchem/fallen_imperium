// Faction abilities, leaders (agents, commanders, heroes), the Alliance note,
// faction promissory notes, faction technologies and flagship / mech
// abilities (Stage 5). Factions are switched on in batches - see
// factionImplemented() in faction_text.cpp. Batch 1: Federation of Sol,
// Barony of Letnev, Emirates of Hacan, Universities of Jol-Nar, Sardakk N'orr,
// Xxcha Kingdom (batch 2: rules_factions2.cpp).
#include <algorithm>
#include <cstring>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
constexpr int kGroundMask = (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech));
const char* unitKey(const Game& g, int p, UnitType t) { return unitDef(g, p, t).key; }
int infantryOnBoard(const Game& g, int p) { return unitsOnBoard(g, p, UnitType::Infantry); }
}  // namespace

// ------------------------------------------------------------------ asking
int Rules::fpick(int p, Ab ab, const std::string& prompt, std::vector<Option> opts, const std::string& context, bool onMap,
                 bool onPlanets) {
    if (mutedFor_ >= 0 && p != mutedFor_) {  // (fixes 27) another player can't resolve abilities now: the "don't" answer
        for (const Option& o : opts)
            if (o.id == -1) return -1;
        if (opts.size() == 2 && opts[0].id == 1 && opts[1].id == 0) return 0;  // (fyes)
    }
    const int prev = curAbility_;
    curAbility_ = int(ab);
    int r = pick(p, Tag::Faction, prompt, std::move(opts), context, onMap, onPlanets);
    curAbility_ = prev;
    return r;
}

std::vector<int> Rules::fpickMany(int p, Ab ab, const std::string& prompt, std::vector<Option> opts, int minPick, int maxPick,
                                  const std::string& context, bool onPlanets) {
    if (mutedFor_ >= 0 && p != mutedFor_ && minPick == 0) return {};  // (fixes 27) another player can't resolve abilities now
    const int prev = curAbility_;
    curAbility_ = int(ab);
    std::vector<int> r = pickMany(p, Tag::Faction, prompt, std::move(opts), minPick, maxPick, context, onPlanets);
    curAbility_ = prev;
    return r;
}

bool Rules::fyes(int p, Ab ab, const std::string& prompt, const std::string& context) {
    return fpick(p, ab, prompt, {{1, "Yes", ""}, {0, "No", ""}}, context) == 1;
}

// ------------------------------------------------------------------ setup and leaders
void Rules::factionSetup(int p) {
    Player& pl = G.players[p];
    pl.leaders.clear();
    for (int i = 0; i < kLeaderCount; ++i) {
        if (kLeaders[i].faction != pl.faction) continue;
        // The Council Keleres: only the hero that goes with the home they took (The Tribunii)
        if (kLeaders[i].type == kHero && pl.tribunii >= 0) {
            const char* k = kLeaders[i].key;
            const char* sub = kFactions[pl.tribunii].key;
            const bool fits = (!std::strcmp(k, "keleresheroharka") && !std::strcmp(sub, "mentak")) ||
                              (!std::strcmp(k, "keleresheroodlynn") && !std::strcmp(sub, "xxcha")) ||
                              (!std::strcmp(k, "keleresherokuuasi") && !std::strcmp(sub, "argent"));
            if (!fits) continue;
        }
        pl.leaders.push_back({int16_t(i), uint8_t(kLeaders[i].type == kAgent ? kReady : kLocked)});
    }
    if (!factionImplemented(pl.faction)) return;
    // Alliance (the Mahact player purges theirs) and the faction's own note(s).
    if (std::strcmp(kFactions[pl.faction].key, "mahact") != 0) pl.notes.push_back(makeNote(p, kNoteAlliance));
    // (0.44.1, fixes >= 6: the Veldyr's first Branch Office, Tax Haven, had no name and so was never dealt - games
    // started before keep playing without it)
    const bool veldyrOld = G.fixes < 6 && !std::strcmp(kFactions[pl.faction].key, "veldyr");
    if (*factionNoteName(pl.faction, kNoteFaction) && !veldyrOld) pl.notes.push_back(makeNote(p, kNoteFaction));
    if (*factionNoteName(pl.faction, kNoteFaction2)) pl.notes.push_back(makeNote(p, kNoteFaction2));
    for (int t : {kNoteFaction3, kNoteFaction4})  // (the Veldyr's Branch Offices, Discordant Stars)
        if (*factionNoteName(pl.faction, t)) pl.notes.push_back(makeNote(p, t));
}

namespace {
void leaderEvent(Engine& E, const Game& G, int p, int leader, const std::string& what, const std::string& text) {
    Event e;
    e.kind = EventKind::Leader;
    e.player = int8_t(p);
    e.card = int16_t(leader);
    e.label = what;
    e.text = text;
    (void)G;
    E.emit(e);
}
}  // namespace

// Commanders and heroes unlock the moment their condition is met (checked before every question).
void Rules::checkUnlocks() {
    if (G.players.empty()) return;
    // Runs before every question: skip it when nothing has happened since the last check
    // (every change that can unlock a leader writes to the game log).
    if (G.log.size() == unlockLogSize_ && G.round == unlockRound_) return;
    unlockLogSize_ = G.log.size();
    unlockRound_ = G.round;
    // Hil Colish (Creuss flagship) carries a delta wormhole: keep adjacency up to date
    {
        int c = playerOfFaction(G, "ghost"), fs = -1;
        if (c >= 0)
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (G.systems[s].space[c].count(UnitType::Flagship)) fs = int(s);
        if (fs != G.adjFlagship) {
            G.adjFlagship = int16_t(fs);
            computeAdjacency(G);
        }
        // (0.44.9, fixes 13) a Nekro flagship with Hil Colish's ability (Valefar Assimilator Z) carries one too
        if (G.fixes >= 13 && c >= 0) {
            int sig = 0;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != c && flagshipAbility(G, q, "ghost"))
                    for (size_t s = 0; s < G.systems.size(); ++s)
                        if (G.systems[s].space[size_t(q)].count(UnitType::Flagship)) sig = sig * 31 + int(s) + 1;
            if (sig != adjFlagshipZ_) {
                adjFlagshipZ_ = sig;
                computeAdjacency(G);
            }
        }
    }
    for (int p = 0; p < G.numPlayers; ++p) {
        Player& pl = G.players[p];
        if (pl.eliminated || !factionImplemented(pl.faction)) continue;
        for (LeaderState& ls : pl.leaders) {
            if (ls.status != kLocked) continue;
            const LeaderDef& L = kLeaders[ls.leader];
            auto is = [&](const char* k) { return !std::strcmp(L.key, k); };
            bool ok = false;
            if (L.type == kHero) {
                // (fixes 25, owner's faction debug pass) secrets scored through Plots Within Plots are scored objectives too
                ok = pl.scored.size() + (G.fixes >= 25 ? pl.plotted.size() : 0) >= 3;
            } else if (L.type == kCommander) {
                if (is("solcommander") || is("xxchacommander")) {
                    int sum = 0;
                    for (int x : controlledPlanets(G, p)) sum += is("solcommander") ? planetResources(G, x) : planetInfluence(G, x);
                    ok = sum >= 12;
                } else if (is("letnevcommander")) {
                    for (const SystemState& s : G.systems)
                        if (s.space[p].nonFighterShips() >= 5) ok = true;
                } else if (is("hacancommander")) {
                    ok = pl.tradeGoods >= 10;
                } else if (is("jolnarcommander")) {
                    ok = pl.techs.size() >= 8;
                } else if (is("sardakkcommander")) {
                    int n = 0;
                    for (int x : controlledPlanets(G, p))
                        if (G.systems[G.planets[x].system].home < 0) ++n;
                    ok = n >= 5;
                } else if (is("arboreccommander")) {
                    int n = 0;
                    for (int x : controlledPlanets(G, p)) n += G.planets[x].units[p].groundForces();
                    ok = n >= 12;
                } else if (is("l1z1xcommander")) {
                    ok = unitsOnBoard(G, p, UnitType::Dreadnought) >= 4;
                } else if (is("mentakcommander")) {
                    ok = unitsOnBoard(G, p, UnitType::Cruiser) >= 4;
                } else if (is("naalucommander")) {
                    int mec = G.mecatol();
                    auto gfIn = [&](int s) {
                        if (G.systems[s].space[p].groundForces()) return true;
                        for (int16_t x : G.systems[s].planets)
                            if (G.planets[x].units[p].groundForces()) return true;
                        return false;
                    };
                    if (mec >= 0) {
                        ok = gfIn(mec);
                        for (int16_t a : G.systems[mec].adj) ok = ok || gfIn(a);
                    }
                } else if (is("yssarilcommander")) {
                    ok = pl.actionCards.size() >= 7;
                } else if (is("yincommander")) {
                    ok = pl.usedFactionAbility;
                } else if (is("ghostcommander")) {
                    int n = 0;
                    // (fixes 24: Nexus Sovereignty only stops Mallice's wormholes during movement - qeWormholes keeps them)
                    for (size_t s = 0; s < G.systems.size(); ++s)
                        if ((G.fixes >= 24 ? qeWormholes(G, int(s)) : uint8_t(wormholesOf(G, int(s)) & (kAlpha | kBeta))) && hasUnits(G, p, int(s))) ++n;
                    ok = n >= 3;
                } else if (is("muaatcommander")) {
                    ok = pl.producedWarSun;
                } else if (is("saarcommander")) {
                    ok = unitsOnBoard(G, p, UnitType::SpaceDock) >= 3;
                } else if (is("winnucommander")) {
                    int mec = G.mecatol();
                    if (mec >= 0)
                        for (int16_t x : G.systems[mec].planets)
                            if (G.planets[x].owner == p) ok = true;
                    ok = ok || pl.mecatolCombat;
                } else if (is("nekrocommander")) {
                    int n = int(pl.techs.size());
                    if (pl.hasTech(findTech("vax")) && pl.assimX < 0) --n;
                    if (pl.hasTech(findTech("vay")) && pl.assimY < 0) --n;
                    ok = n >= 3;
                } else if (is("empyreancommander")) {
                    ok = neighboursWithAll(G, p);
                } else if (is("argentcommander")) {
                    int n = 0;  // units with anti-fighter barrage, space cannon or bombardment
                    for (int t = 0; t < kUnitTypes; ++t) {
                        const UnitDef& u = unitDef(G, p, UnitType(t));
                        if (u.afb > 0 || u.cannon > 0 || u.bombard > 0) n += unitsOnBoard(G, p, UnitType(t));
                    }
                    ok = n >= 6;
                } else if (is("mahactcommander")) {
                    int n = 0;
                    for (int q = 0; q < G.numPlayers; ++q)
                        if ((pl.edict >> q) & 1) ++n;
                    ok = n >= 2;
                } else if (is("naazcommander")) {
                    int n = 0;
                    for (size_t s = 0; s < G.systems.size(); ++s) {
                        bool m = G.systems[s].space[p].count(UnitType::Mech) > 0;
                        for (int16_t x : G.systems[s].planets)
                            if (G.planets[x].units[p].count(UnitType::Mech)) m = true;
                        if (m) ++n;
                    }
                    ok = n >= 3;
                } else if (is("nomadcommander")) {
                    for (int16_t o : pl.scored)
                        if (kObjectives[o].kind == 3) ok = true;
                } else if (is("titanscommander")) {
                    ok = structuresOf(G, p) >= 5;
                } else if (is("bastioncommander")) {  // Thunder's Edge factions (0.35.0)
                    // ((fixes 27, the Last Bastion review - owner 2026-11-13) "There are 3 galvanized units on the game
                    // board": anyone's - Dame Briar and Raise the Standard galvanize other players' units)
                    int n = 0;
                    if (G.fixes >= 27)
                        for (int q = 0; q < G.numPlayers; ++q) n += galvanizedOnBoard(G, q);
                    else
                        n = galvanizedOnBoard(G, p);
                    ok = n >= 3;
                } else if (is("ralnelcommander")) {
                    ok = G.lastPasser == p;
                } else if (is("deepwroughtcommander")) {
                    ok = pl.oceans > 0;
                } else if (is("crimsoncommander")) {
                    ok = pl.breachedOthers;
                } else if (is("firmamentcommander")) {
                    ok = pl.plotsIn != 0;
                } else if (is("obsidiancommander")) {
                    for (size_t s = 0; s < G.systems.size() && !ok; ++s)
                        if (G.systems[s].fracture && hasUnits(G, p, int(s))) ok = true;
                } else if (is("kortalicommander")) {  // Discordant Stars (0.37.0)
                    ok = pl.dsUnlock;
                } else if (is("vayleriancommander")) {
                    int n[8] = {};
                    for (int x : controlledPlanets(G, p))
                        for (Trait t : {Trait::Cultural, Trait::Hazardous, Trait::Industrial})
                            if (hasTraitOf(G, x, t)) ++n[int(t) & 7];
                    for (int v : n) ok = ok || v >= 3;
                } else if (is("lanefircommander")) {
                    ok = G.fragsPurged >= 7;
                } else if (is("kyrocommander")) {
                    ok = unitsOnBoard(G, p, UnitType::Infantry) >= 6 && unitsOnBoard(G, p, UnitType::Fighter) >= 6;
                } else if (is("cheirancommander")) {  // (Byssus: their mechs count as structures)
                    int n = 0;
                    for (int x : controlledPlanets(G, p))
                        if (G.planets[size_t(x)].system != pl.homeSystem)
                            n += G.planets[size_t(x)].units[size_t(p)].structures() + G.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech);
                    ok = n >= 4;
                } else if (is("celdauricommander")) {
                    for (size_t s = 0; s < G.systems.size() && !ok; ++s) {
                        if (int(s) == pl.homeSystem) continue;
                        bool near = false;
                        for (int16_t a : G.systems[s].adj) near = near || a == pl.homeSystem;
                        if (near) continue;
                        for (int16_t x : G.systems[s].planets)
                            if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock)) ok = true;
                    }
                } else if (is("khraskcommander") || is("kolumecommander")) {  // (Discordant Stars batch 2: flags set in play)
                    ok = pl.dsUnlock;
                } else if (is("zealotscommander")) {
                    for (int16_t t : pl.techs) ok = ok || kTechs[t].req[0] + kTechs[t].req[1] + kTechs[t].req[2] + kTechs[t].req[3] >= 2;
                } else if (is("zeliancommander")) {
                    int n = 0;
                    for (const SystemState& s : G.systems) n += (s.d().anomalies & kAsteroid) && s.space[size_t(p)].ships() ? 1 : 0;
                    ok = n >= 2;
                } else if (is("rohdhnacommander")) {
                    for (size_t s = 0; s < G.systems.size() && !ok; ++s) ok = productionCapacity(G, p, int(s)) >= 7;
                } else if (is("tneliscommander")) {
                    ok = unitsOnBoard(G, p, UnitType::Destroyer) >= 6;
                } else if (is("bentorcommander")) {  // (Discordant Stars batch 3, 0.39.0)
                    int n = 0;
                    for (uint8_t b = pl.dsFrags; b; b &= uint8_t(b - 1)) ++n;
                    ok = n >= 3;
                } else if (is("dihmohncommander") || is("florzencommander") || is("augerscommander")) {  // (flags set in play)
                    ok = pl.dsUnlock;
                } else if (is("gledgecommander")) {
                    for (size_t s = 0; s < G.systems.size() && !ok; ++s) {
                        int cost2 = 0;
                        for (int t : kShipIdx)
                            if (t != int(UnitType::Fighter)) cost2 += G.systems[s].space[size_t(p)].n[t] * unitCost2(G, p, UnitType(t));
                        ok = cost2 >= 20;
                    }
                } else if (is("kjalengardcommander")) {
                    ok = G.dsGlory.size() >= 2;
                } else if (is("kollecccommander")) {  // (Discordant Stars batch 4, 0.40.0)
                    ok = fragmentCount(G, p) >= 4;
                } else if (is("mortheuscommander")) {
                    int n = 0;
                    for (size_t s = 0; s < G.systems.size(); ++s) {
                        if (int(s) == pl.homeSystem || !G.systems[s].space[size_t(p)].ships()) continue;
                        bool near = false;
                        for (int16_t a : G.systems[s].adj) near = near || a == pl.homeSystem;
                        n += near ? 0 : 1;
                    }
                    ok = n >= 3;
                } else if (is("vadencommander")) {
                    int n = 0;
                    for (int q = 0; q < G.numPlayers; ++q) n += q != p && G.dsDebt[q] > 0 ? 1 : 0;
                    ok = n >= std::max(1, G.numPlayers / 2);
                } else if (is("cymiaecommander")) {
                    ok = unitsOnBoard(G, p, UnitType::Infantry) >= 10;
                } else if (is("freesystemscommander")) {  // (Discordant Stars batch 5, 0.41.0)
                    ok = true;
                    for (size_t x = 0; x < G.planets.size() && ok; ++x)
                        if (!G.planets[x].destroyed && !*legendaryName(G.planets[x].def) && G.planets[x].owner < 0 &&
                            G.systems[size_t(G.planets[x].system)].d().planetCount > 0)
                            ok = false;
                } else if (is("edyncommander")) {
                    ok = !G.laws.empty();
                } else if (is("nokarcommander")) {
                    ok = pl.dsHired;
                } else if (is("axiscommander")) {
                    int types = 0;
                    for (int i = 0; i < 8; ++i)
                        if (G.dsOrders[i] >= 0) types |= 1 << (i / 2);
                    ok = types == 15;
                } else if (is("mykomentoricommander")) {
                    ok = pl.commodities >= 4;
                } else if (is("gheminacommander")) {
                    ok = unitsOnBoard(G, p, UnitType::Flagship) + unitsOnBoard(G, p, UnitType::Flagship2) >= 2;
                } else if (is("veldyrcommander")) {  // (Discordant Stars batch 6, 0.42.0)
                    int others = 0;
                    for (int q = 0; q < G.numPlayers; ++q) {
                        bool has = false;
                        for (int16_t b : G.dsBranch) has = has || (b >= 0 && G.planets[size_t(b)].owner == q);
                        others += q != p && has ? 1 : 0;
                    }
                    ok = others >= 2;
                } else if (is("lizhocommander")) {
                    int n = 0;
                    for (int16_t t : G.dsTrap) n += t >= 0 ? 1 : 0;
                    ok = n >= 4;
                } else if (is("mirvedacommander")) {
                    int n = 0;
                    for (int16_t t : pl.techs) n += kTechs[t].unitUpgrade ? 1 : 0;
                    ok = n >= 2;
                } else if (is("ghoticommander")) {
                    int n = 0;
                    for (size_t s = 0; s < G.systems.size(); ++s) n += G.systems[s].planets.empty() && hasUnits(G, p, int(s)) ? 1 : 0;
                    ok = n >= 3;
                } else if (is("nivyncommander")) {
                    int n = 0;
                    for (size_t s = 0; s < G.systems.size(); ++s)
                        if (int(s) != pl.homeSystem && (G.systems[s].d().anomalies || int(s) == G.dsWound) && hasUnits(G, p, int(s))) ++n;
                    ok = n >= 2;
                } else if (is("olradincommander")) {
                    bool c = false, h = false, i = false;
                    for (int x : controlledPlanets(G, p)) {
                        c = c || hasTraitOf(G, x, Trait::Cultural);
                        h = h || hasTraitOf(G, x, Trait::Hazardous);
                        i = i || hasTraitOf(G, x, Trait::Industrial);
                    }
                    ok = c && h && i;
                } else if (is("cabalcommander")) {
                    int n = 0;
                    for (size_t s = 0; s < G.systems.size(); ++s)
                        if (((G.systems[s].d().anomalies & kRift) || dimensionalTear(G, int(s))) && hasUnits(G, p, int(s))) ++n;
                    ok = n >= 3;
                }
            }
            if (!ok) continue;
            ls.status = kReady;
            leaderEvent(E, G, p, ls.leader, L.type == kHero ? "hero unlocked" : "commander unlocked",
                        pn(p) + " unlocks their " + (L.type == kHero ? "hero, " : "commander, ") + L.name + ".");
        }
    }
}

void Rules::exhaustAgent(int p, int nth) {
    int i = leaderIndex(G, p, kAgent, nth);
    if (i < 0) return;
    LeaderState& ls = G.players[p].leaders[i];
    ls.status = kExhausted;
    leaderEvent(E, G, p, ls.leader, "agent", pn(p) + " exhausts their agent, " + kLeaders[ls.leader].name + ".");
    // Temporal Command Suite (Nomad): ready it again
    if (techReady(p, "tcs") &&
        fyes(p, Ab::TemporalCommand, std::string("Exhaust Temporal Command Suite to ready ") + kLeaders[ls.leader].name + " again?", "")) {
        exhaustTech(p, "tcs");
        say(p, pn(p) + " readies " + kLeaders[ls.leader].name + " (Temporal Command Suite).");
        readyAgent(p, i);
        return;
    }
    // ... or another player's agent, and then trade with them (neighbours or not)
    for (int n = 0; n < G.numPlayers; ++n) {
        if (n == p || G.players[n].eliminated || !techReady(n, "tcs")) continue;
        curFor_ = p;
        bool use = fyes(n, Ab::TemporalCommand,
                        "Exhaust Temporal Command Suite to ready " + pn(p) + "'s agent, " + kLeaders[ls.leader].name + "?",
                        "Then you may trade with " + pn(p) + ", neighbours or not.");
        curFor_ = -1;
        if (!use) continue;
        exhaustTech(n, "tcs");
        say(n, pn(n) + " readies " + pn(p) + "'s " + kLeaders[ls.leader].name + " (Temporal Command Suite).");
        readyAgent(p, i);
        if (canOffer(G, n, p)) {
            const uint64_t bits = (uint64_t(1) << (n * 8 + p)) | (uint64_t(1) << (p * 8 + n));
            G.tradeAnyway |= bits;
            negotiate(n, p);
            G.tradeAnyway &= ~bits;
        }
        break;
    }
}

void Rules::purgeHero(int p) {
    int i = leaderIndex(G, p, kHero);
    if (i < 0) return;
    if (G.fixes < 8 && (G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtPurged(p);  // Erasure Corps (0.43.0; fixes 8: purged())
    LeaderState& ls = G.players[p].leaders[i];
    ls.status = kPurged;
    if (G.modules & kModDiscordant) dsSoulCaged(p, ls.leader);  // Soul Cage (Myko-Mentori hero)
    purged(p);  // Aggressive Broker (Uncharted Space)
}

void Rules::readyLeaders() {
    for (int p = 0; p < G.numPlayers; ++p)
        for (size_t i = 0; i < G.players[p].leaders.size(); ++i) {
            const LeaderState& ls = G.players[p].leaders[i];
            if (kLeaders[ls.leader].type == kAgent && ls.status == kExhausted) readyAgent(p, int(i));  // Artuno pays out
        }
}

namespace {
void abilityEvent(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    e.system = int16_t(sys);
    E.emit(e);
}
}  // namespace

// ------------------------------------------------------------------ component actions
std::vector<Option> Rules::factionActionOptions(int p, bool freeOnly) {
    std::vector<Option> o;
    const Player& pl = G.players[p];
    auto add = [&](Ab ab, bool free, const std::string& label, const std::string& detail) {
        o.push_back({(free ? 700 : 6000) + int(ab), label, detail});
    };
    if (canUseAgent(p, "hacan"))
        add(Ab::HacanAgent, true, "Agent: Carth of Golden Sands", "Exhaust to gain 2 commodities or refill another player's. Doesn't use your action.");
    // (fixes 27, the Nekro review - owner 2026-11-14) Nekro Malleon: "choose a player: that player may discard 1 action card or
    // spend 1 command token from their command sheet to gain 2 trade goods" - any player who could
    if (G.fixes >= 27 && canUseAgent(p, "nekro")) {
        bool someone = false;
        for (int q = 0; q < G.numPlayers; ++q) {
            const Player& o = G.players[size_t(q)];
            someone = someone || (!o.eliminated && (!o.actionCards.empty() || o.tactic + o.fleet + o.strategy > 0));
        }
        if (someone)
            add(Ab::NekroAgent, true, "Agent: Nekro Malleon",
                "Choose a player: they may discard an action card or spend a command token to gain 2 trade goods. Doesn't use your action.");
    } else if (canUseAgent(p, "nekro") && (!pl.actionCards.empty() || pl.tactic + pl.fleet + pl.strategy > 0))
        add(Ab::NekroAgent, true, "Agent: Nekro Malleon", "Discard an action card or spend a command token: gain 2 trade goods. Doesn't use your action.");
    if (freeOnly) return o;
    if (fac(p, "sol") && pl.strategy > 0) {
        bool planet = false;
        for (int x : controlledPlanets(G, p))
            if (!isDMZ(G, x)) planet = true;
        if (planet) add(Ab::OrbitalDrop, false, "Orbital Drop", "Spend 1 strategy token: 2 infantry on a planet you control.");
    }
    if (techReady(p, "pm") && pl.strategy > 0)
        add(Ab::ProductionBiomes, false, "Production Biomes", "Exhaust and spend 1 strategy token: gain 4 trade goods, another player gains 2.");
    // Z'eu Omega (Naalu agent, Codex 0.31.0): a player takes a tactical action without placing a command token
    if (codexOn(G) && !teNaalu(G) && canUseAgent(p, "naalu"))  // (fixes 27: not with Thunder's Edge's own Z'eu)
        add(Ab::NaaluAgent, false, "Agent: Z'eu", leaderText(findLeader("naaluagent")));
    if (canUseAgent(p, "xxcha")) {
        bool any = false;
        for (const PlanetState& ps : G.planets)
            if (ps.owner >= 0 && ps.exhausted && !ps.destroyed) any = true;
        if (any) add(Ab::XxchaAgent, false, "Agent: Ggrocuto Rinn", leaderText(findLeader("xxchaagent")));
    }
    if (heroUnlocked(G, p)) {
        if (fac(p, "sol") && tokensOnBoard(p) > 0)
            add(Ab::SolHero, false, "Hero: Jace X", "Return all your command tokens on the board to your reinforcements. Purged afterwards.");
        if (fac(p, "letnev"))
            add(Ab::LetnevHero, false, "Hero: Darktalon Treilla", "This round, no limit on non-fighter ships per system. Purged at the end of the round.");
        if (fac(p, "jolnar")) add(Ab::JolnarHero, false, "Hero: Rin", leaderText(findLeader("jolnarhero")));
        if (fac(p, "xxcha") && teXxchaHero(G)) {  // (0.57.1: Thunder's Edge's Xxekir Grom)
            if (!xxchaHeroSpots(p).empty())
                add(Ab::XxchaHero, false, "Hero: Xxekir Grom", leaderText(findLeader("xxchahero")));
        } else if (fac(p, "xxcha") && G.agendaDeck.size() + G.agendaDiscard.size() >= 2 && !codexOn(G))  // (Omega: passive)
            add(Ab::XxchaHero, false, "Hero: Xxekir Grom", leaderText(findLeader("xxchahero")));
    }
    for (const Option& x : factionActionOptions2(p)) o.push_back(x);
    return o;
}

bool Rules::factionAction(int p, int option) {
    Player& pl = G.players[p];
    const Ab ab = Ab(option >= 6000 ? option - 6000 : option - 700);
    switch (ab) {
        case Ab::OrbitalDrop: {
            std::vector<Option> opts;
            for (int x : controlledPlanets(G, p))
                if (!isDMZ(G, x)) opts.push_back({x, planetName(x), std::to_string(G.planets[x].units[p].count(UnitType::Infantry)) + " infantry there"});
            opts.push_back({-1, "Cancel", "Choose a different action."});
            int x = fpick(p, Ab::OrbitalDrop, "Orbital Drop: put 2 infantry on which planet?", opts, "Costs 1 strategy token.", false, true);
            if (x < 0) return false;
            spendStrategy(p, 1);
            addUnits(p, UnitType::Infantry, 2, G.planets[x].system, x);
            abilityEvent(E, p, "Orbital Drop", pn(p) + " uses Orbital Drop: 2 infantry land on " + planetName(x) + ".", G.planets[x].system);
            // ZS Thunderbolt M2 (DEPLOY)
            if (unitsAvailable(G, p, UnitType::Mech) > 0 && spendable(G, p, false) >= 3 && mechAbilities(G) &&
                fyes(p, Ab::SolMechDeploy, "Pay 3 resources to put a mech on " + planetName(x) + "?", "ZS Thunderbolt M2 (deploy).") &&
                pay(p, 3, false, "ZS Thunderbolt M2", false)) {
                addUnits(p, UnitType::Mech, 1, G.planets[x].system, x);
                say(p, pn(p) + " deploys a mech on " + planetName(x) + ".", G.planets[x].system);
            }
            return true;
        }
        case Ab::ProductionBiomes: {
            std::vector<Option> who;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && !G.players[q].eliminated) who.push_back({q, pn(q), std::to_string(G.players[q].vp) + " VP"});
            who.push_back({-1, "Cancel", ""});
            int q = fpick(p, Ab::ProductionBiomes, "Production Biomes: which other player gains 2 trade goods?", who,
                          "You gain 4 trade goods. Costs 1 strategy token.");
            if (q < 0) return false;
            exhaustTech(p, "pm");
            spendStrategy(p, 1);
            gainTG(p, 4);
            gainTG(q, 2);
            abilityEvent(E, p, "Production Biomes", pn(p) + " gains 4 trade goods and " + pn(q) + " 2 (Production Biomes).");
            return true;
        }
        case Ab::HacanAgent: {
            std::vector<Option> opts;
            const int maxC = commodityCap(G, pl);
            if (pl.commodities < maxC) opts.push_back({p, "Gain 2 commodities", std::to_string(pl.commodities) + " of " + std::to_string(maxC) + " now"});
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && !G.players[q].eliminated &&
                    (G.players[q].commodities < (G.fixes >= 14 ? commodityValue(G, q) : int(kFactions[G.players[q].faction].commodities)) ||
                     eventOn(G, kEvCommerce)))
                    opts.push_back({q, "Refill " + pn(q) + "'s commodities", ""});
            opts.push_back({-1, "Cancel", ""});
            int q = fpick(p, Ab::HacanAgent, "Carth of Golden Sands", opts, leaderText(findLeader("hacanagent")));
            if (q < 0) return false;
            spendAgent(p, "hacan");
            if (q == p) {
                const int before = pl.commodities;
                pl.commodities = int8_t(std::min(maxC, pl.commodities + 2));
                say(p, pn(p) + " gains 2 commodities.");
                siphon(p, pl.commodities - before);
            } else {
                const int before = G.players[q].commodities;
                G.players[q].commodities = int8_t(eventOn(G, kEvCommerce) || G.fixes >= 14 ? refreshTo(G, G.players[q])
                                                                                          : kFactions[G.players[q].faction].commodities);
                say(q, pn(q) + " refills commodities (Carth of Golden Sands).");
                replenished(q, G.players[q].commodities - before);
            }
            return true;
        }
        case Ab::NaaluAgent: {  // Z'eu Omega (Codex)
            std::vector<Option> opts;
            for (int q : clockwiseFrom(G, p))
                if (!G.players[q].eliminated) opts.push_back({q, q == p ? "Yourself" : pn(q), q == p ? "a tactical action without a token" : ""});
            const int q = fpick(p, Ab::NaaluAgent, "Z'eu: who takes the tactical action?", opts, leaderText(findLeader("naaluagent")));
            spendAgent(p, "naalu");
            if (q != p && G.fixes >= 2) agentDone(p, q);  // 0.43.0 (C3): a promised Z'eu
            abilityEvent(E, p, "Z'eu", pn(p) + " exhausts Z'eu: " + (q == p ? std::string("a tactical action") : pn(q) + " takes a tactical action") +
                                           " without a command token.");
            zeu_ = true;
            tacticalAction(q);
            zeu_ = false;
            return true;
        }
        case Ab::XxchaAgent: {
            std::vector<Option> opts;
            for (size_t x = 0; x < G.planets.size(); ++x) {
                const PlanetState& ps = G.planets[x];
                if (ps.owner < 0 || !ps.exhausted || ps.destroyed) continue;
                opts.push_back({int(x), planetName(int(x)), ps.owner == p ? "yours" : pn(ps.owner) + "'s"});
            }
            opts.push_back({-1, "Cancel", ""});
            int x = fpick(p, Ab::XxchaAgent, "Ggrocuto Rinn: ready which planet?", opts, "", false, true);
            if (x < 0) return false;
            spendAgent(p, "xxcha");
            G.planets[x].exhausted = false;
            say(p, planetName(x) + " is readied.", G.planets[x].system);
            // next to a planet you control: may remove 1 infantry from it
            const int sys = G.planets[x].system;
            bool near = false;
            for (int y : controlledPlanets(G, p)) {
                int s2 = G.planets[y].system;
                if (s2 == sys || adjacentSystems(G, s2, sys)) near = true;
            }
            int owner = G.planets[x].owner;
            if (near && owner != p && G.planets[x].units[owner].count(UnitType::Infantry) > 0 &&
                fyes(p, Ab::XxchaAgent, "Remove 1 of " + pn(owner) + "'s infantry from " + planetName(x) + "?", "")) {
                // (fixes 27, the Xxcha review - owner 2026-11-13) "remove 1 infantry ... and return it to its reinforcements" - a
                // removal, not a destruction (no Crimson Legionnaire, Devour, Infantry II, ...)
                const bool was = removing_;
                if (G.fixes >= 27) removing_ = true;
                destroyUnits(owner, UnitType::Infantry, 1, sys, x, p, "Ggrocuto Rinn");
                removing_ = was;
            }
            return true;
        }
        case Ab::SolHero: {
            int n = 0;
            for (SystemState& s : G.systems)
                if (s.hasToken(p)) {
                    s.tokens &= uint16_t(~(1u << p));
                    ++n;
                }
            purgeHero(p);
            leaderEvent(E, G, p, pl.leaders[leaderIndex(G, p, kHero)].leader, "hero",
                        pn(p) + " uses Jace X: " + std::to_string(n) + " command tokens return from the board.");
            return true;
        }
        case Ab::LetnevHero: {
            int i = leaderIndex(G, p, kHero);
            pl.leaders[i].status = kExhausted;  // in play until the end of the round, then purged
            pl.heroRound = int16_t(G.round);
            leaderEvent(E, G, p, pl.leaders[i].leader, "hero",
                        pn(p) + " uses Darktalon Treilla: no fleet limit for the rest of this round.");
            return true;
        }
        case Ab::JolnarHero: {
            int swaps = 0;
            // 0.44.6 (fixes 10): each technology owned when Rin is used may be replaced once - not a replacement again
            const std::vector<int16_t> before = pl.techs;
            std::vector<int16_t> done;
            for (int guard = 0; guard < 20; ++guard) {
                std::vector<Option> mine;
                for (int16_t t : pl.techs) {
                    if (kTechs[t].unitUpgrade || kTechs[t].color == TechColor::None) continue;
                    if (G.fixes >= 10 && (std::find(before.begin(), before.end(), t) == before.end() ||
                                          std::find(done.begin(), done.end(), t) != done.end()))
                        continue;
                    mine.push_back({t, kTechs[t].name, std::string(colorName(kTechs[t].color)) + " technology"});
                }
                mine.push_back({-1, swaps ? "Done" : "Cancel", ""});
                int t = fpick(p, Ab::JolnarHero, "Rin: replace which technology?", mine, "You may swap any number of them.");
                if (t < 0) break;
                std::vector<Option> repl;
                for (int u = 0; u < kTechCount; ++u)
                    if (kTechs[u].color == kTechs[t].color && !kTechs[u].unitUpgrade && techAvailableToResearch(G, p, u))
                        repl.push_back({u, kTechs[u].name, techSummary(u)});
                if (repl.empty()) {
                    if (G.fixes >= 10) done.push_back(int16_t(t));
                    continue;
                }
                repl.push_back({-1, "Keep it", ""});
                int u = fpick(p, Ab::JolnarHero, std::string("Rin: replace ") + kTechs[t].name + " with?", repl);
                if (G.fixes >= 10) done.push_back(int16_t(t));  // (kept or replaced: decided)
                if (u < 0) continue;
                pl.techs.erase(std::find(pl.techs.begin(), pl.techs.end(), int16_t(t)));
                pl.exhaustedTechs.erase(std::remove(pl.exhaustedTechs.begin(), pl.exhaustedTechs.end(), int16_t(t)), pl.exhaustedTechs.end());
                researchTech(p, u, false);
                say(p, pn(p) + " replaces " + kTechs[t].name + " with " + kTechs[u].name + ".");
                ++swaps;
            }
            if (!swaps) return false;
            purgeHero(p);
            leaderEvent(E, G, p, pl.leaders[leaderIndex(G, p, kHero)].leader, "hero", pn(p) + " uses Rin, the Master's Legacy.");
            return true;
        }
        case Ab::XxchaHero: {
            if (teXxchaHero(G)) {  // (0.57.1) Thunder's Edge: Planetary Defense Nexus - up to 4 PDS / mechs, those planets readied
                leaderEvent(E, G, p, pl.leaders[leaderIndex(G, p, kHero)].leader, "hero",
                            pn(p) + " uses Xxekir Grom: defences rise across their worlds.");
                std::vector<int> readied;
                for (int k = 0; k < 4; ++k) {
                    std::vector<Option> opts = xxchaHeroSpots(p);
                    if (opts.empty()) break;
                    opts.push_back({-1, "Done", ""});
                    const int id = fpick(p, Ab::XxchaHero, "Xxekir Grom: place a PDS or a mech (" + std::to_string(k + 1) + " of 4)", opts, "",
                                         false, true);
                    if (id < 0) break;
                    const int x = id / 2;
                    const UnitType t = id % 2 ? UnitType::Mech : UnitType::PDS;
                    addUnits(p, t, 1, G.planets[size_t(x)].system, x);
                    say(p, pn(p) + " places " + (t == UnitType::Mech ? "a mech" : "a PDS") + " on " + planetName(x) + " (Xxekir Grom).",
                        G.planets[size_t(x)].system);
                    if (G.planets[size_t(x)].exhausted) readied.push_back(x);
                    G.planets[size_t(x)].exhausted = false;  // (readied at once: the next pick sees it ready)
                }
                if (!readied.empty()) say(p, pn(p) + " readies " + std::to_string(readied.size()) + (readied.size() == 1 ? " planet" : " planets") + " (Xxekir Grom).");
                purgeHero(p);
                return true;
            }
            std::vector<int16_t> top;
            for (int i = 0; i < 5; ++i) {
                if (G.agendaDeck.empty()) {
                    if (G.agendaDiscard.empty()) break;
                    G.agendaDeck = G.agendaDiscard;
                    G.agendaDiscard.clear();
                    E.rng().shuffle(G.agendaDeck);
                }
                top.push_back(G.agendaDeck.back());
                G.agendaDeck.pop_back();
            }
            E.revealed();
            if (G.fixes < 27) purgeHero(p);  // (fixes 27, the Xxcha review - owner 2026-11-13: "Then, purge this card" - at the end)
            leaderEvent(E, G, p, pl.leaders[leaderIndex(G, p, kHero)].leader, "hero",
                        pn(p) + " uses Xxekir Grom: they look at the top agendas and resolve 2.");
            // (fixes 27) "Other players cannot resolve abilities during this action"
            const int mutedWas = mutedFor_;
            if (G.fixes >= 27) mutedFor_ = p;
            std::vector<Option> opts;
            for (int16_t a : top) {
                bool ok = agendaRevealable(a) && std::strcmp(kAgendas[a].key, "covert") != 0 && kAgendas[a].elect != Elect::Law;
                opts.push_back({a, kAgendas[a].name, std::string(kAgendas[a].law ? "Law: " : "Directive: ") + agendaText(a, false)});
                opts.back().enabled = ok;
            }
            int usable = 0;
            for (const Option& o : opts) usable += o.enabled ? 1 : 0;
            std::vector<int> chosen = usable ? fpickMany(p, Ab::XxchaHero, "Xxekir Grom: reveal and resolve which 2 agendas?", opts,
                                                         std::min(2, usable), std::min(2, usable))
                                             : std::vector<int>();
            for (int16_t a : top)
                if (std::find(chosen.begin(), chosen.end(), int(a)) == chosen.end()) G.agendaDiscard.push_back(a);
            for (int a : chosen) {
                Event e;
                e.kind = EventKind::Agenda;
                e.card = int16_t(a);
                e.text = std::string("Agenda: ") + kAgendas[a].name + " (Xxekir Grom)";
                E.emit(e);
                const int prev = curAgenda_;
                curAgenda_ = a;
                G.currentAgenda = int16_t(a);
                int o = fpick(p, Ab::XxchaHero, std::string(kAgendas[a].name) + ": which outcome?", agendaOutcomes(a));
                std::vector<int> voted(G.numPlayers, kNoVote), votes(G.numPlayers, 0);
                voted[p] = o;
                votes[p] = 1;
                resolveOutcome(a, o, voted, votes);
                G.currentAgenda = -1;
                curAgenda_ = prev;
            }
            mutedFor_ = mutedWas;
            if (G.fixes >= 27) purgeHero(p);
            return true;
        }
        default: return factionAction2(p, ab);
    }
}

// ------------------------------------------------------------------ status phase and rounds
void Rules::statusFactions() {
    // Genesis: an infantry appears in its system's space area.
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!flagshipAbility(G, p, "sol") || G.players[p].eliminated) continue;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (flagshipIn(G, p, int(s))) {
                addUnits(p, UnitType::Infantry, 1, int(s), -1);
                say(p, pn(p) + "'s Genesis puts 1 infantry in " + sysName(int(s)) + ".", int(s));
                removeExcessCapacity(p, int(s));
            }
    }
}

void Rules::roundEndFactions() {
    for (int p = 0; p < G.numPlayers; ++p) {
        Player& pl = G.players[p];
        if (pl.heroRound == G.round) {
            int i = leaderIndex(G, p, kHero);
            if (i >= 0) pl.leaders[i].status = kPurged;
            pl.heroRound = -1;
            if (i >= 0 && G.fixes >= 8) purged(p);  // (0.44.4: Erasure Corps, Aggressive Broker)
            enforceFleetLimit(p);
        }
    }
}

void Rules::startOfTurnFactions(int p) {
    Player& pl = G.players[p];
    startOfTurn2(p);  // Spy Net
    startOfTurn3(p);  // Creuss Iff, Chaos Mapping
    // Infantry II / Spec Ops II: units on the card come back to the home system.
    if (pl.infantryOnCard > 0) {
        std::vector<Option> opts;
        // Unholy Abomination II (Cymiae, Discordant Stars): any planet they control
        const bool anywhere = (G.modules & kModDiscordant) && !std::strcmp(unitKey(G, p, UnitType::Infantry), "cymiae_infantry2");
        for (int x : anywhere ? controlledPlanets(G, p) : std::vector<int>(G.systems[pl.homeSystem].planets.begin(), G.systems[pl.homeSystem].planets.end()))
            if (G.planets[x].owner == p && !isDMZ(G, x)) opts.push_back({x, planetName(x), ""});
        if (!opts.empty()) {
            int x = opts.size() == 1 ? opts[0].id
                                     : fpick(p, Ab::InfantryReturn, anywhere ? "Your infantry come back: on which planet?" : "Your infantry come back: on which home planet?",
                                             opts, "", false, true);
            addUnits(p, UnitType::Infantry, pl.infantryOnCard, G.planets[x].system, x);
            say(p, std::to_string(pl.infantryOnCard) + " of " + pn(p) + "'s infantry return to " + planetName(x) + ".", G.planets[x].system);
            pl.infantryOnCard = 0;
        }
    }
    // Military Support: whoever holds Sol's note may use it now.
    if (fac(p, "sol")) {
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == p || G.players[q].eliminated || !holdsNote(G, q, p, kNoteFaction)) continue;
            std::vector<int> mine;
            for (int x : controlledPlanets(G, q))
                if (!isDMZ(G, x)) mine.push_back(x);
            if (mine.empty()) continue;
            if (!fyes(q, Ab::MilitarySupport, "Play " + pn(p) + "'s Military Support?", factionNoteText(pl.faction, kNoteFaction)))
                continue;
            if (pl.strategy > 0) pl.strategy = int8_t(pl.strategy - 1);
            std::vector<Option> opts;
            for (int x : mine) opts.push_back({x, planetName(x), ""});
            int x = fpick(q, Ab::MilitarySupport, "Military Support: put 2 infantry on which planet?", opts, "", false, true);
            addUnits(q, UnitType::Infantry, 2, G.planets[x].system, x);
            abilityEvent(E, q, "Military Support", pn(q) + " plays Military Support: 2 infantry on " + planetName(x) + ".",
                         G.planets[x].system);
            returnNote(q, makeNote(p, kNoteFaction));
            break;
        }
    }
}

// ------------------------------------------------------------------ tactical action
// E-Res Siphons: 4 trade goods when another player activates a system with your ships (0.44.5, fixes 9: before
// Minister of Peace can end the turn - both come "after activation")
void Rules::eresSiphons(int p, int sys) {
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != p && owns(q, "ers") && hasShips(G, q, sys) && !G.players[q].eliminated) {
            gainTG(q, 4);
            abilityEvent(E, q, "E-Res Siphons", pn(q) + " gains 4 trade goods (E-Res Siphons).", sys);
        }
}

bool Rules::afterActivation(int p, int sys) {
    if (G.fixes < 9) eresSiphons(p, sys);
    // Nullification Field
    for (int q : windowOrder()) {
        if (q == p || !techReady(q, "nf") || !hasShips(G, q, sys) || G.players[q].strategy <= 0) continue;
        if (!fyes(q, Ab::NullificationField, "Use Nullification Field to end " + pn(p) + "'s turn?",
                  "Exhausts it and costs 1 strategy token. " + pn(p) + " activated " + sysName(sys) + "."))
            continue;
        exhaustTech(q, "nf");
        spendStrategy(q, 1);
        abilityEvent(E, q, "Nullification Field", pn(q) + " uses Nullification Field: " + pn(p) + "'s turn ends.", sys);
        turnEnded_ = true;
        return true;
    }
    // Spatial Conduit Cylinder (0.44.7, fixes 11: asked only when a ship of ours elsewhere can't reach the system without it)
    bool conduitGains = G.fixes < 11;
    if (!conduitGains && techReady(p, "scc") && hasUnits(G, p, sys))
        for (size_t s = 0; s < G.systems.size() && !conduitGains; ++s) {
            if (int(s) == sys || G.systems[s].hasToken(p)) continue;
            const Force& f = G.systems[s].space[size_t(p)];
            for (int t : kShipIdx)
                if (!conduitGains && f.n[t] && t != int(UnitType::Fighter) && moveValue(G, p, int(s), UnitType(t)) > 0 &&
                    !shipReach(G, p, int(s), sys, UnitType(t), moveBoost(G, p) ? 1 : 0).ok)
                    conduitGains = true;
        }
    if (conduitGains && techReady(p, "scc") && hasUnits(G, p, sys) &&
        fyes(p, Ab::SpatialConduit, "Exhaust Spatial Conduit Cylinder?",
             "This activation, " + sysName(sys) + " counts as adjacent to every system with your units.")) {
        exhaustTech(p, "scc");
        G.fx.conduit = true;
        abilityEvent(E, p, "Spatial Conduit Cylinder", pn(p) + " opens a Spatial Conduit to " + sysName(sys) + ".", sys);
    }
    // Starlancer, Crucible, Awaken, Ouranos
    if (afterActivation4(p, sys)) {
        turnEnded_ = true;
        return true;
    }
    afterActivation2(p, sys);  // Neuroglaive, So Ata, Dirzuga Rophal, Duha Menaimon, I48S
    afterActivation3(p, sys);  // Icarus Drive, Taivra, Mendosa, Aetherstream, Aetherpassage
    return false;
}

void Rules::endOfTacticalFactions(int p, int sys) {
    endOfTactical3(p, sys);  // Reclamation, Reclaimer
    endOfTactical4(p, sys);  // Il Na Viroset, captured units and blockades
    troAgent(p, sys);
}

// T'ro (Sardakk agent): 2 infantry on a planet the active player controls in the active system. Only for themselves (see
// Viscount Unlenn) - or the Yssaril player using Ssruu. (fixes 25, Sardakk review 2026-11-11: also when the tactical
// action ends early - Minister of Peace, Nullification Field - and not offered with no infantry in their reinforcements,
// which can't happen while infantry tokens make them unlimited: unitLimit 99)
void Rules::troAgent(int p, int sys) {
    const int s = agentUser(p, "sardakk");  // or a player who promised its agent in a deal
    if (s < 0 || G.players[p].eliminated) return;
    if (G.fixes >= 25 && unitsAvailable(G, p, UnitType::Infantry) <= 0) return;
    std::vector<Option> opts;
    for (int16_t x : G.systems[sys].planets)
        if (G.planets[x].owner == p && !isDMZ(G, x)) opts.push_back({x, planetName(x), ""});
    if (opts.empty()) return;
    agentFor(s, p);
    bool use = fyes(s, Ab::SardakkAgent, s == p ? "Exhaust T'ro to put 2 infantry on one of your planets here?"
                                                : "Exhaust T'ro to let " + pn(p) + " put 2 infantry on a planet in " + sysName(sys) + "?",
                    leaderText(findLeader("sardakkagent")));
    curFor_ = -1;
    if (!use) return;
    spendAgent(s, "sardakk");
    agentDone(s, p);
    int x = opts.size() == 1 ? opts[0].id : fpick(p, Ab::SardakkAgent, "T'ro: put 2 infantry on which planet?", opts, "", false, true);
    addUnits(p, UnitType::Infantry, 2, sys, x);
    say(p, pn(p) + " puts 2 infantry on " + planetName(x) + " (T'ro).", sys);
}

bool Rules::shvalCheck(int p, int sys) {
    shval_ = false;
    if (!fac(p, "sardakk") || !heroUnlocked(G, p)) return false;
    // (fixes 25, Sardakk review 2026-11-11: also with no ground forces in space when G'hom Sek'kus can bring some in)
    bool ghom = false;
    if (G.fixes >= 25 && commanderUsable(G, p, "sardakk")) {
        auto from = [&](int s2) {
            for (int16_t x : G.systems[size_t(s2)].planets)
                if (G.planets[size_t(x)].units[size_t(p)].groundForces() > 0) ghom = true;
        };
        from(sys);
        for (int16_t a : G.systems[size_t(sys)].adj)
            if (!G.systems[size_t(a)].hasToken(p)) from(a);
    }
    if (G.systems[sys].space[p].groundForces() == 0 && !ghom) return false;
    bool target = false;
    for (int16_t x : G.systems[sys].planets)
        if (G.planets[x].owner != p && !isDMZ(G, x)) target = true;
    if (!target) return false;
    if (!fyes(p, Ab::SardakkHero, "Use Sh'val, Harbinger?",
              "Skip space cannon, space combat and bombardment and land at once. Afterwards your ships here return to your "
              "reinforcements and the hero is purged."))
        return false;
    shval_ = true;
    leaderEvent(E, G, p, G.players[p].leaders[leaderIndex(G, p, kHero)].leader, "hero",
                pn(p) + " uses Sh'val, Harbinger: straight to the landing!");
    return true;
}

void Rules::ghomSekkus(int p, int sys) {
    ghomBrought_.clear();
    if (!commanderUsable(G, p, "sardakk")) return;
    ghomBefore_[0] = G.systems[sys].space[p].count(UnitType::Infantry);
    ghomBefore_[1] = G.systems[sys].space[p].count(UnitType::Mech);
    std::vector<Option> opts;
    auto addFrom = [&](int s) {
        for (int16_t x : G.systems[s].planets) {
            const Force& f = G.planets[x].units[p];
            if (f.groundForces() == 0) continue;
            UnitType t = f.count(UnitType::Infantry) ? UnitType::Infantry : UnitType::Mech;
            opts.push_back({x, std::string(unitTypeName(t)) + " from " + planetName(x),
                            std::to_string(f.groundForces()) + " ground forces there"});
        }
    };
    addFrom(sys);
    for (int16_t a : G.systems[sys].adj)
        if (!G.systems[a].hasToken(p)) addFrom(a);
    if (opts.empty()) return;
    // (0.44.2, fixes >= 7, tester: "Sardakk commander does not let me commit mechs, only infantry" - a planet with both
    // always sent an infantry; now it asks which)
    const bool choose = G.fixes >= 7;
    if (choose)
        for (Option& o : opts) {
            const Force& f = G.planets[o.id].units[p];
            if (f.count(UnitType::Infantry) && f.count(UnitType::Mech)) o.label = "Infantry or mech from " + planetName(o.id);
        }
    std::vector<int> picks = fpickMany(p, Ab::SardakkCommander, "G'hom Sek'kus: bring ground forces in to land?", opts, 0,
                                       int(opts.size()), "Up to 1 from each planet; they join the landing.", true);
    int brought[2] = {0, 0};  // infantry, mechs
    for (int x : picks) {
        Force& f = G.planets[x].units[p];
        UnitType t = f.count(UnitType::Infantry) ? UnitType::Infantry : UnitType::Mech;
        if (choose && f.count(UnitType::Infantry) && f.count(UnitType::Mech)) {
            std::vector<Option> which{{0, "Infantry", std::to_string(f.count(UnitType::Infantry)) + " there"},
                                      {1, "Mech", std::to_string(f.count(UnitType::Mech)) + " there"}};
            curValue_ = x;  // (the planet, for the AI)
            t = fpick(p, Ab::SardakkCommanderUnit, "G'hom Sek'kus: which ground force from " + planetName(x) + "?", which) == 1 ? UnitType::Mech
                                                                                                                           : UnitType::Infantry;
            curValue_ = 0;
        }
        // (fixes 25, Sardakk review 2026-11-11: a damaged mech stays damaged - remove() takes an undamaged one first, so
        // it is damaged when none is left undamaged)
        const bool damaged = f.n[int(t)] - f.dmg[int(t)] <= 0;
        f.remove(t, 1, false);
        G.systems[sys].space[p].add(t, 1);
        if (G.fixes >= 25 && damaged) ++G.systems[sys].space[p].dmg[int(t)];
        ghomBrought_.push_back({int16_t(x), int8_t(t), damaged});
        ++brought[t == UnitType::Mech ? 1 : 0];
    }
    if (!picks.empty() && choose) {
        std::string what;
        if (brought[0]) what = std::to_string(brought[0]) + (brought[0] == 1 ? " infantry" : " infantry");
        if (brought[1]) what += (what.empty() ? "" : " and ") + std::to_string(brought[1]) + (brought[1] == 1 ? " mech" : " mechs");
        say(p, pn(p) + " brings " + what + " in (G'hom Sek'kus).", sys);
    } else if (!picks.empty())
        say(p, pn(p) + " brings " + std::to_string(picks.size()) + " ground forces in (G'hom Sek'kus).", sys);
}

// ------------------------------------------------------------------ strategy cards
void Rules::afterDiplomacy(int p) {
    if (!fac(p, "xxcha")) return;
    // Peace Accords: an empty planet (not Mecatol Rex) next to one of your planets.
    std::vector<int> near;
    for (int y : controlledPlanets(G, p)) {
        int s = G.planets[y].system;
        near.push_back(s);
        for (int16_t a : G.systems[s].adj) near.push_back(a);
    }
    std::vector<Option> opts;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.owner == p || ps.destroyed || !std::strcmp(kPlanets[ps.def].key, "mr")) continue;
        // (0.57.1, fixes 20 - tester) a space station is taken only by being alone there with ships (TE rulebook),
        // never as an "empty planet"
        if (G.fixes >= 20 && isStation(G, int(x))) continue;
        if (std::find(near.begin(), near.end(), int(ps.system)) == near.end()) continue;
        bool units = false;
        // (fixes 27, the Xxcha review - owner 2026-11-13) neutral units count as another player's (Thunder's Edge rulebook)
        for (int q = 0; q < (G.fixes >= 27 ? kSides : G.numPlayers); ++q)
            if (!ps.units[q].empty()) units = true;
        if (units) continue;
        opts.push_back({int(x), planetName(int(x)),
                        std::to_string(planetResources(G, int(x))) + " res / " + std::to_string(planetInfluence(G, int(x))) + " inf" +
                            (ps.owner >= 0 ? " - " + pn(ps.owner) + "'s" : "")});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "No planet", ""});
    int x = fpick(p, Ab::PeaceAccords, "Peace Accords: take control of which planet?", opts, abilityText(G.players[p].faction, 0),
                  false, true);
    if (x < 0) return;
    abilityEvent(E, p, "Peace Accords", pn(p) + " takes " + planetName(x) + " by Peace Accords.", G.planets[x].system);
    setControl(x, p);
}

// (0.57.1) Thunder's Edge's Xxekir Grom: the PDS and mechs that could go on p's planets now (not stations, not the DMZ;
// at most 2 PDS a planet, as anywhere else)
std::vector<Option> Rules::xxchaHeroSpots(int p) const {
    std::vector<Option> o;
    const bool pds = unitsAvailable(G, p, UnitType::PDS) > 0, mech = unitsAvailable(G, p, UnitType::Mech) > 0;
    for (int x : controlledPlanets(G, p)) {
        if (isStation(G, x) || isDMZ(G, x) || G.planets[size_t(x)].destroyed) continue;
        const std::string d = std::string(G.planets[size_t(x)].exhausted ? "exhausted - it is readied" : "ready");
        if (pds && (G.planets[size_t(x)].units[size_t(p)].count(UnitType::PDS) < 2 || lawInPlay(G, "defense_act")))
            o.push_back({x * 2, "PDS on " + planetName(x), d});
        if (mech) o.push_back({x * 2 + 1, "Mech on " + planetName(x), d});
    }
    return o;
}

void Rules::endOfStrategyPhase() {
    // Quantum Datahub Node: swap a strategy card with another player for 3 trade goods.
    for (int p : initiativeOrder(G)) {
        Player& pl = G.players[p];
        if (!owns(p, "qdn") || pl.strategy <= 0 || pl.tradeGoods < 3 || pl.cards.empty()) continue;
        std::vector<Option> opts;
        for (int8_t mine : pl.cards)
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && !G.players[q].eliminated)
                    for (int8_t theirs : G.players[q].cards)
                        opts.push_back({mine * 100 + theirs, std::string("Your ") + strategyName(mine) + " for " + pn(q) + "'s " +
                                                                  strategyName(theirs),
                                        "They get 3 of your trade goods."});
        if (opts.empty()) continue;
        opts.push_back({-1, "No swap", ""});
        int c = fpick(p, Ab::QuantumDatahub, "Quantum Datahub Node: swap a strategy card?", opts,
                      "Costs 1 strategy token and 3 trade goods (given to that player).");
        if (c < 0) continue;
        int mine = c / 100, theirs = c % 100, q = G.cardOwner[theirs];
        auto& a = pl.cards;
        auto& b = G.players[q].cards;
        *std::find(a.begin(), a.end(), int8_t(mine)) = int8_t(theirs);
        *std::find(b.begin(), b.end(), int8_t(theirs)) = int8_t(mine);
        G.cardOwner[mine] = int8_t(q);
        G.cardOwner[theirs] = int8_t(p);
        spendStrategy(p, 1);
        pl.tradeGoods = int8_t(pl.tradeGoods - 3);
        gainTG(q, 3);
        abilityEvent(E, p, "Quantum Datahub Node",
                     pn(p) + " swaps " + strategyName(mine) + " for " + pn(q) + "'s " + strategyName(theirs) + " (Quantum Datahub Node).");
    }
    // Acquiescence, the original card (fixes 26, the Winnu review - games without the Codex; the Omega works in the Winnu's
    // strategic actions): "At the end of the strategy phase: Exchange 1 of your strategy cards with a strategy card that was
    // chosen by the Winnu player. Then, return this card to the Winnu player."
    if (G.fixes >= 26 && !codexOn(G)) {
        const int w = playerOfFaction(G, "winnu");
        for (int q : initiativeOrder(G)) {
            if (w < 0 || q == w || G.players[q].eliminated || G.players[w].cards.empty() || G.players[q].cards.empty() ||
                !holdsNote(G, q, w, kNoteFaction))
                continue;
            std::vector<Option> opts;
            for (int8_t mine : G.players[q].cards)
                for (int8_t theirs : G.players[w].cards)
                    opts.push_back({mine * 100 + theirs, std::string("Your ") + strategyName(mine) + " for " + pn(w) + "'s " + strategyName(theirs), ""});
            opts.push_back({-1, "Keep the card", ""});
            const int c = fpick(q, Ab::Acquiescence, "Play " + pn(w) + "'s Acquiescence: exchange a strategy card with theirs?", opts,
                                factionNoteText(G.players[w].faction, kNoteFaction));
            if (c < 0) continue;
            const int mine = c / 100, theirs = c % 100;
            auto& a = G.players[q].cards;
            auto& b = G.players[w].cards;
            *std::find(a.begin(), a.end(), int8_t(mine)) = int8_t(theirs);
            *std::find(b.begin(), b.end(), int8_t(theirs)) = int8_t(mine);
            G.cardOwner[mine] = int8_t(w);
            G.cardOwner[theirs] = int8_t(q);
            returnNote(q, makeNote(w, kNoteFaction));
            abilityEvent(E, q, "Acquiescence",
                         pn(q) + " plays Acquiescence: " + strategyName(mine) + " for " + pn(w) + "'s " + strategyName(theirs) + ".");
        }
    }
}

// ------------------------------------------------------------------ agendas
bool Rules::quashCheck(int a) {
    for (int p : clockwiseFrom(G, G.speaker)) {
        if (G.players[p].eliminated) continue;
        // Quash
        if (fac(p, "xxcha") && G.players[p].strategy > 0 &&
            fyes(p, Ab::Quash, std::string("Quash ") + kAgendas[a].name + "?", abilityText(G.players[p].faction, 1))) {
            spendStrategy(p, 1);
            abilityEvent(E, p, "Quash", pn(p) + " quashes " + kAgendas[a].name + ".");
            return true;
        }
        // Political Favor (Xxcha's note)
        for (int16_t n : std::vector<int16_t>(G.players[p].notes)) {
            int owner = noteOwner(n);
            if (owner == p || noteType(n) != kNoteFaction || !isFaction(G, owner, "xxcha")) continue;
            // (fixes 27, the Xxcha review - LRR 1.17 "then": the token must come off the Xxcha's strategy pool first)
            if (G.fixes >= 27 && G.players[owner].strategy <= 0) continue;
            if (!fyes(p, Ab::PoliticalFavor, "Play " + pn(owner) + "'s Political Favor against " + kAgendas[a].name + "?",
                      factionNoteText(G.players[owner].faction, kNoteFaction)))
                continue;
            if (G.players[owner].strategy > 0) G.players[owner].strategy = int8_t(G.players[owner].strategy - 1);
            returnNote(p, n);
            abilityEvent(E, p, "Political Favor", pn(p) + " plays Political Favor: " + kAgendas[a].name + " is discarded.");
            return true;
        }
    }
    return false;
}

int Rules::extraVotes(int p, int outcome) {
    Player& pl = G.players[p];
    if (!commanderUsable(G, p, "hacan") || pl.tradeGoods <= 0) return 0;
    std::vector<Option> opts;
    // (fixes 26, the Hacan review: "You may spend any number of trade goods" - the list stopped at 10)
    for (int k = 0; k <= pl.tradeGoods && (k <= 10 || G.fixes >= 26); ++k)
        opts.push_back({k, k == 0 ? std::string("None") : std::to_string(k) + " trade good" + (k > 1 ? "s" : ""),
                        k ? "+" + std::to_string(2 * k) + " votes" : ""});
    const int prev = G.currentAgenda;
    int k = fpick(p, Ab::HacanCommander, "Gila the Silvertongue: spend trade goods for extra votes?", opts,
                  "Each trade good adds 2 votes for " + outcomeName(G.currentAgenda, outcome) + ".");
    (void)prev;
    if (k <= 0) return 0;
    pl.tradeGoods = int8_t(pl.tradeGoods - k);
    say(p, pn(p) + " spends " + std::to_string(k) + " trade goods for " + std::to_string(2 * k) + " extra votes (Gila the Silvertongue).");
    return 2 * k;
}

// ------------------------------------------------------------------ cards and research
bool Rules::cancelActionCard(int player, int card) {
    for (int q : windowOrder()) {
        if (q == player || G.players[q].eliminated || !techReady(q, "it") || G.players[q].strategy <= 0) continue;
        const int prev = curCard_, prevFor = curFor_, prevValue = curValue_;
        curCard_ = card;
        curFor_ = player;
        curValue_ = sabotaged_ >= 0 ? sabotaged_ + 1 : 0;  // (a Sabotage: whose card it would cancel, for the AI)
        bool use = fyes(q, Ab::InstinctTraining, std::string("Cancel ") + kActionCards[card].name + " with Instinct Training?",
                        std::string("Exhausts it and costs 1 strategy token. ") + kActionCards[card].name + ": " + actionCardText(card));
        curCard_ = prev;
        curFor_ = prevFor;
        curValue_ = prevValue;
        if (!use) continue;
        exhaustTech(q, "it");
        spendStrategy(q, 1);
        abilityEvent(E, q, "Instinct Training", pn(q) + " cancels " + kActionCards[card].name + " with Instinct Training.");
        return true;
    }
    return watcher(player, card);  // Empyrean mech
}

bool Rules::harruhgFree(int p) {
    if (!fac(p, "hacan") || !heroUnlocked(G, p)) return false;
    if (!fyes(p, Ab::HacanHero, "Use Harrugh Gefhara: everything you produce now is free?", "The hero is purged afterwards."))
        return false;
    purgeHero(p);
    leaderEvent(E, G, p, G.players[p].leaders[leaderIndex(G, p, kHero)].leader, "hero",
                pn(p) + " uses Harrugh Gefhara: this production costs nothing.");
    return true;
}

// (fixes 27, the Jol-Nar review - owner 2026-11-13) Doctor Sucaban: "When a player spends resources to research: You may
// exhaust this card to allow that player to remove any number of their infantry from the game board. For each unit removed,
// reduce the resources spent by 1" - any player's research; the Jol-Nar decide (a person is asked - sucabanQuiet_ -, a bot
// when it promised); the researching player removes them - a person picks which (owner: the bots as before)
int Rules::sucaban27(int p, int cost) {
    if (cost <= 0 || p < 0 || p >= G.numPlayers || infantryOnBoard(G, p) == 0) return cost;
    const std::string text = leaderText(findLeader("jolnaragent"));
    int user = -1;
    if (canUseAgent(p, "jolnar")) {
        if (fyes(p, Ab::JolnarAgent, "Exhaust Doctor Sucaban to pay for this research with infantry?", text)) user = p;
    } else {
        for (int o : initiativeOrder(G)) {
            if (o == p || !canUseAgent(o, "jolnar")) continue;
            const bool promised = G.agentFor[o] == p;
            const bool human = G.players[size_t(o)].human;
            if (!promised && !(human && sucabanQuiet_[o] != G.round)) continue;
            agentFor(o, p);
            std::vector<Option> yn = {{1, "Yes", ""}, {0, "No", ""}};
            if (human && !promised) yn.push_back({3, "No, and don't ask again this round", "You'll be asked again next round."});
            const int c = fpick(o, Ab::JolnarAgent, "Exhaust Doctor Sucaban: " + pn(p) + " may pay for this research with infantry?", yn,
                                pn(p) + " spends " + std::to_string(cost) + " resources to research. " + text);
            curFor_ = -1;
            if (c == 3) sucabanQuiet_[o] = int16_t(G.round);
            if (c != 1) continue;
            user = o;
            break;
        }
    }
    if (user < 0) return cost;
    spendAgent(user, "jolnar");
    agentDone(user, p);
    std::vector<Option> opts;
    for (int k = 0; k <= std::min(cost, infantryOnBoard(G, p)); ++k)
        opts.push_back({k, std::to_string(k) + " infantry", k ? std::to_string(k) + " resources less" : ""});
    const int k = fpick(p, Ab::JolnarAgent, "Doctor Sucaban: remove how many of your infantry?", opts, "Each one reduces the cost by 1.");
    for (int i = 0; i < k; ++i) {
        int sys = -1, planet = -1;
        if (G.players[size_t(p)].human) {  // (a person: from where)
            std::vector<Option> where;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (const int n = G.systems[s].space[size_t(p)].count(UnitType::Infantry))
                    where.push_back({10000 + int(s), "In space: " + sysName(int(s)), std::to_string(n) + " infantry"});
            for (size_t x = 0; x < G.planets.size(); ++x)
                if (const int n = G.planets[x].units[size_t(p)].count(UnitType::Infantry))
                    where.push_back({int(x), planetName(int(x)), std::to_string(n) + " infantry"});
            if (where.empty()) break;
            const int w = where.size() == 1 ? where[0].id
                                             : fpick(p, Ab::JolnarAgent, "Doctor Sucaban: remove an infantry from where? (" + std::to_string(k - i) + " to go)",
                                                     where, "");
            if (w >= 10000) sys = w - 10000;
            else if (w >= 0) {
                planet = w;
                sys = G.planets[size_t(w)].system;
            }
        } else {  // (a bot: the biggest groups, space areas first - as before)
            int bestN = 0;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (G.systems[s].space[p].count(UnitType::Infantry) > bestN) {
                    bestN = G.systems[s].space[p].count(UnitType::Infantry) + 10;
                    sys = int(s);
                    planet = -1;
                }
            if (sys < 0)
                for (size_t x = 0; x < G.planets.size(); ++x)
                    if (G.planets[x].units[p].count(UnitType::Infantry) > bestN) {
                        bestN = G.planets[x].units[p].count(UnitType::Infantry);
                        sys = G.planets[x].system;
                        planet = int(x);
                    }
        }
        if (sys < 0) break;
        removing_ = true;
        destroyUnits(p, UnitType::Infantry, 1, sys, planet, -1, "Doctor Sucaban");
        removing_ = false;
    }
    return cost - k;
}

int Rules::sucaban(int p, int cost) {
    if (G.fixes >= 27) return sucaban27(p, cost);
    const int j = agentUser(p, "jolnar");  // themselves (or Ssruu), or a player who promised its agent
    if (cost <= 0 || j < 0 || infantryOnBoard(G, p) == 0) return cost;
    agentFor(j, p);
    bool use = fyes(j, Ab::JolnarAgent, j == p ? "Exhaust Doctor Sucaban to pay for this research with infantry?"
                                               : "Exhaust Doctor Sucaban to let " + pn(p) + " pay for research with infantry?",
                    leaderText(findLeader("jolnaragent")));
    curFor_ = -1;
    if (!use) return cost;
    spendAgent(j, "jolnar");
    agentDone(j, p);
    std::vector<Option> opts;
    for (int k = 0; k <= std::min(cost, infantryOnBoard(G, p)); ++k)
        opts.push_back({k, std::to_string(k) + " infantry", k ? std::to_string(k) + " resources less" : ""});
    int k = fpick(p, Ab::JolnarAgent, "Doctor Sucaban: remove how many of your infantry?", opts, "Each one reduces the cost by 1.");
    // take them from the biggest groups first (space areas, then planets)
    for (int i = 0; i < k; ++i) {
        int bestSys = -1, bestPlanet = -1, bestN = 0;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (G.systems[s].space[p].count(UnitType::Infantry) > bestN) {
                bestN = G.systems[s].space[p].count(UnitType::Infantry) + 10;
                bestSys = int(s);
                bestPlanet = -1;
            }
        if (bestSys < 0)
            for (size_t x = 0; x < G.planets.size(); ++x)
                if (G.planets[x].units[p].count(UnitType::Infantry) > bestN) {
                    bestN = G.planets[x].units[p].count(UnitType::Infantry);
                    bestSys = G.planets[x].system;
                    bestPlanet = int(x);
                }
        if (bestSys < 0) break;
        removing_ = true;
        destroyUnits(p, UnitType::Infantry, 1, bestSys, bestPlanet, -1, "Doctor Sucaban");
        removing_ = false;
    }
    return cost - k;
}

void Rules::researchAgreement(int p, int tech) {
    if (!fac(p, "jolnar") || G.phase == Phase::Setup || kTechs[tech].faction >= 0) return;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p || G.players[q].eliminated || !holdsNote(G, q, p, kNoteFaction) || G.players[q].hasTech(tech)) continue;
        // (fixes 27, the Jol-Nar review - owner 2026-11-13) only a technology the holder could own: not a unit upgrade its
        // faction's own replaces (Sol and Carrier II - their Advanced Carriers became Carrier IIs)
        if (G.fixes >= 27 && !techAvailableToResearch(G, q, tech)) continue;
        if (!fyes(q, Ab::ResearchAgreement, std::string("Play Research Agreement to gain ") + kTechs[tech].name + "?",
                  factionNoteText(G.players[p].faction, kNoteFaction)))
            continue;
        researchTech(q, tech, false);
        returnNote(q, makeNote(p, kNoteFaction));
        abilityEvent(E, q, "Research Agreement", pn(q) + " also gains " + kTechs[tech].name + " (Research Agreement).");
        return;
    }
}

void Rules::infantryDestroyed(int p, int k) {
    const char* key = unitKey(G, p, UnitType::Infantry);
    int need = !std::strcmp(key, "infantry2") || !std::strcmp(key, "arborec_infantry2") || !std::strcmp(key, "zelian_infantry2") ||
                       !std::strcmp(key, "cymiae_infantry2") ? 6  // (Impactor II / Unholy Abomination II, Discordant Stars)
               : !std::strcmp(key, "sol_infantry2")                                                                               ? 5
                                                                                                                                  : 0;
    if (need == 0 || k <= 0) return;
    RollResult r;
    for (int i = 0; i < k; ++i) {
        r.dice.push_back(int8_t(E.rng().d10()));
        r.need.push_back(int8_t(need));
        r.unit.push_back(int8_t(UnitType::Infantry));
    }
    recount(r);
    reportRoll(p, -1, std::string(kUnits[G.players[p].unit[int(UnitType::Infantry)]].name) + " (" + std::to_string(need) +
                          "+ comes back next turn)",
               r);
    G.players[p].infantryOnCard = int8_t(G.players[p].infantryOnCard + r.hits);
}

// ------------------------------------------------------------------ combat
RollMods Rules::combatMods(int p, int opp, int sys, int planet, bool space) {
    RollMods m;
    if (fac(p, "jolnar")) {
        m.all(-1);  // Fragile
        // Shield Paling: infantry on a planet with Jol-Nar's mech ignore Fragile
        if (!space && planet >= 0 && G.planets[planet].units[p].count(UnitType::Mech) > 0 && mechAbilities(G)) m.perType[int(UnitType::Infantry)] += 1;
        if (space) m.flagshipBonus = true;  // J.N.S. Hylarim
    } else if (space && G.fixes >= 8 && flagshipText(G, p, "jolnar")) {
        m.flagshipBonus = true;  // J.N.S. Hylarim through Valefar Assimilator Z (0.44.4)
    }
    if (fac(p, "sardakk")) m.all(1);  // Unrelenting
    if (flagshipAbility(G, p, "sardakk") && space && flagshipIn(G, p, sys))  // C'morran N'orr: +1 to the other ships
        for (int t : kShipIdx)
            if (UnitType(t) != UnitType::Flagship) m.perType[t] += 1;
    if (!space) m.all(tekklar_[p]);
    // Iconoclast (Naalu mech): +2 against an opponent with a relic fragment
    if (!space && fac(p, "naalu") && opp >= 0 && fragmentCount(G, opp) > 0 && mechAbilities(G) && !codexOn(G))
        m.perType[int(UnitType::Mech)] += 2;  // (Codex: Iconoclast Omega stops barrages instead - rules_combat)
    combatMods3(p, opp, sys, planet, space, m);  // Rickar Rickani, Salai Sai Corian, Mordred
    combatMods4(p, opp, sys, planet, space, m);  // Arvicon Rex, Visz El Vir, Z-Grav Eidolon
    if ((G.modules & kModThundersEdge) && p >= 0 && p < G.numPlayers && sys >= 0) {  // Thunder's Edge factions (0.35.0)
        const Force& f = space || planet < 0 ? G.systems[size_t(sys)].space[size_t(p)] : G.planets[size_t(planet)].units[size_t(p)];
        for (int t = 0; t < kUnitTypes; ++t) m.extraDice[t] += f.galv[t];  // Galvanize: 1 more die each
        if (space && flagshipAbility(G, p, "bastion") && flagshipIn(G, p, sys))  // The Egeiro
            m.perType[int(UnitType::Flagship)] += egeiroBonus(G, p);
        if (const int a = assail(p, opp)) m.all(a);                                       // Assail (the Obsidian's plot)
        if (G.systems[size_t(sys)].fracture && commanderUsable(G, p, "obsidian")) m.all(1);  // Aroz Hollow
    }
    // Arcane Citadel (Uncharted Space): +1 to its controller's combat rolls on the planet
    if (!space && planet >= 0 && G.planets[planet].owner == p && hasAttachment(G, planet, "arcanecitadel")) m.all(1);
    // Thunder's Edge breakthroughs: Imperator (Winnu), Gravleash Maneuvers (Letnev: +X to one ship - the one with the most dice)
    if (const int imp = btCombatBonus(p, opp, sys, space)) m.all(imp);
    if (space && sys >= 0)
        if (const int x = btLetnevBonus(p, sys)) {
            int best = -1;
            if (G.fixes >= 8) {  // 0.44.4: the ship that gains the most hit chances: dice x the faces +X turns into hits
                int bestGain = -1;
                for (int t : kShipIdx) {
                    if (!G.systems[size_t(sys)].space[size_t(p)].n[t]) continue;
                    const UnitDef& u = unitDef(G, p, UnitType(t));
                    const int gain = std::max(1, int(u.combatDice)) * std::max(0, std::min(x, int(u.combat) - 1)) * 10 + u.combatDice;
                    if (gain > bestGain) bestGain = gain, best = t;
                }
            } else {
                for (int t : kShipIdx)
                    if (G.systems[size_t(sys)].space[size_t(p)].n[t] &&
                        (best < 0 || unitDef(G, p, UnitType(t)).combatDice > unitDef(G, p, UnitType(best)).combatDice ||
                         (unitDef(G, p, UnitType(t)).combatDice == unitDef(G, p, UnitType(best)).combatDice &&
                          unitDef(G, p, UnitType(t)).combat > unitDef(G, p, UnitType(best)).combat)))
                        best = t;
            }
            m.oneType = best;
            m.oneBonus = x;
        }
    if (G.modules & kModDiscordant) dsCombatMods(p, opp, sys, planet, space, m);  // Discordant Stars (Lithodax, Spear of Vaylar)
    return m;
}

void Rules::combatRoundStartFactions(int att, int def, int sys, int planet, bool space, RollMods mods[2], bool munitions[2]) {
    const int side[2] = {att, def};
    munitions[0] = munitions[1] = false;
    // Eidolon Maximum (Naaz-Rokha, Absolute Synergy, 0.43.0 T8f): repaired at the start of every combat round
    if ((G.modules & kModThundersEdge) && G.fixes >= 2)
        for (int p : side) {
            if (p < 0 || p >= G.numPlayers || !G.players[size_t(p)].eidolonMax) continue;
            Force& f = planet >= 0 ? G.planets[size_t(planet)].units[size_t(p)] : G.systems[size_t(sys)].space[size_t(p)];
            Force& g2 = G.systems[size_t(sys)].space[size_t(p)];
            for (Force* fo : {&f, &g2})
                if (fo->dmg[int(UnitType::Mech)] > 0) {
                    fo->dmg[int(UnitType::Mech)] = 0;
                    say(p, pn(p) + "'s Eidolon Maximum repairs itself.", sys);
                }
        }
    if (!space && planet >= 0 && (G.modules & kModThundersEdge)) proximaTargeting(att, def, planet);  // Last Bastion technology
    if (space) {
        for (int i = 0; i < 2; ++i) {
            int p = side[i];
            if (p >= G.numPlayers) continue;  // (neutral units)
            Force& f = G.systems[sys].space[p];
            // Arc Secundus repairs itself
            if (flagshipText(G, p, "letnev") && f.count(UnitType::Flagship) && f.dmg[int(UnitType::Flagship)]) {
                f.dmg[int(UnitType::Flagship)] = uint8_t(f.dmg[int(UnitType::Flagship)] - 1);
                say(p, pn(p) + "'s Arc Secundus is repaired.", sys);
            }
            // Munitions Reserves
            if (fac(p, "letnev") && G.players[p].tradeGoods >= 2 &&
                fyes(p, Ab::Munitions, "Munitions Reserves: pay 2 trade goods to reroll your misses this round?",
                     "You have " + std::to_string(G.players[p].tradeGoods) + " trade goods.")) {
                G.players[p].tradeGoods = int8_t(G.players[p].tradeGoods - 2);
                munitions[i] = true;
            }
        }
        // War Funding (Letnev's note, original version): the Letnev player loses 2 trade goods,
        // the holder may reroll their dice this round (owner, RULES-CHECK 0.6.0 #1).
        warFunding_[0] = warFunding_[1] = false;
        for (int i = 0; i < 2 && !codexOn(G); ++i) {  // (Codex: War Funding Omega, after the rolls)
            int p = side[i];
            if (p >= G.numPlayers) continue;  // (neutral units)
            int Lt = playerOfFaction(G, "letnev");
            if (Lt < 0 || Lt == p || !holdsNote(G, p, Lt, kNoteFaction)) continue;
            if (!fyes(p, Ab::WarFunding, "Play War Funding: reroll your misses this round?", factionNoteText(G.players[Lt].faction, kNoteFaction)))
                continue;
            G.players[Lt].tradeGoods = int8_t(std::max(0, G.players[Lt].tradeGoods - 2));
            warFunding_[i] = true;
            returnNote(p, makeNote(Lt, kNoteFaction));
            abilityEvent(E, p, "War Funding", pn(p) + " plays War Funding: " + pn(Lt) + " loses 2 trade goods.", sys);
        }
        // Viscount Unlenn (Letnev agent): one ship rolls an extra die.
        // Agents are only offered when their owner is involved (owner OK'd, RULES-CHECK 0.6.0 #2); Ssruu counts.
        // A player who promised its agent to one side in a deal may use it for them (that side's ships only).
        int L = -1, LB = -1;
        for (int i = 0; i < 2 && L < 0; ++i)
            if (canUseAgent(side[i], "letnev")) L = LB = side[i];
        for (int i = 0; i < 2 && L < 0; ++i)
            if ((L = agentUser(side[i], "letnev")) >= 0) LB = side[i];
        if (L >= 0) {
            std::vector<Option> opts;
            for (int i = 0; i < 2; ++i) {
                if (L != side[0] && L != side[1] && side[i] != LB) continue;
                for (int t : kShipIdx)
                    if (G.systems[sys].space[side[i]].n[t])
                        opts.push_back({i * 16 + t, pn(side[i]) + "'s " + unitTypeName(UnitType(t)),
                                        "hits on " + std::to_string(unitDef(G, side[i], UnitType(t)).combat)});
            }
            if (!opts.empty()) {
                opts.push_back({-1, "Don't use it", ""});
                agentFor(L, LB);
                int c = fpick(L, Ab::LetnevAgent,
                              L == LB ? std::string("Exhaust Viscount Unlenn: which ship rolls an extra die this round?")
                                      : "Exhaust Viscount Unlenn for " + pn(LB) + ": which of their ships rolls an extra die?",
                              opts, leaderText(findLeader("letnevagent")));
                curFor_ = -1;
                if (c >= 0) {
                    spendAgent(L, "letnev");
                    mods[c / 16].extraDice[c % 16] += 1;
                    agentDone(L, LB);
                }
            }
        }
    } else {
        // Evelyn DeLouis (Sol agent): one ground force rolls an extra die
        int S = -1, SB = -1;
        for (int i = 0; i < 2 && S < 0; ++i)
            if (canUseAgent(side[i], "sol")) S = SB = side[i];
        for (int i = 0; i < 2 && S < 0; ++i)
            if ((S = agentUser(side[i], "sol")) >= 0) SB = side[i];
        if (S >= 0) {
            std::vector<Option> opts;
            for (int i = 0; i < 2; ++i) {
                if (S != side[0] && S != side[1] && side[i] != SB) continue;
                for (UnitType t : {UnitType::Infantry, UnitType::Mech})
                    if (G.planets[planet].units[side[i]].count(t))
                        opts.push_back({i * 16 + int(t), pn(side[i]) + "'s " + unitTypeName(t),
                                        "hits on " + std::to_string(unitDef(G, side[i], t).combat)});
            }
            if (!opts.empty()) {
                opts.push_back({-1, "Don't use it", ""});
                agentFor(S, SB);
                int c = fpick(S, Ab::SolAgent,
                              S == SB ? std::string("Exhaust Evelyn DeLouis: which ground force rolls an extra die this round?")
                                      : "Exhaust Evelyn DeLouis for " + pn(SB) + ": which of their ground forces rolls an extra die?",
                              opts, leaderText(findLeader("solagent")));
                curFor_ = -1;
                if (c >= 0) {
                    spendAgent(S, "sol");
                    mods[c / 16].extraDice[c % 16] += 1;
                    agentDone(S, SB);
                }
            }
        }
        // Dunlain Reaper (DEPLOY): swap an infantry in the combat for a mech
        for (int i = 0; i < 2; ++i) {
            int p = side[i];
            if (p >= G.numPlayers) continue;  // (neutral units)
            Force& f = G.planets[planet].units[p];
            if (!fac(p, "letnev") || f.count(UnitType::Infantry) == 0 || unitsAvailable(G, p, UnitType::Mech) <= 0 ||
                spendable(G, p, false) < 2 || !mechAbilities(G))
                continue;
            if (!fyes(p, Ab::LetnevMechDeploy, "Pay 2 resources to swap 1 infantry for a Dunlain Reaper mech?", "Deploy, on " + planetName(planet) + "."))
                continue;
            if (!pay(p, 2, false, "Dunlain Reaper", false)) continue;
            f.remove(UnitType::Infantry, 1, false);
            f.add(UnitType::Mech, 1);
            say(p, pn(p) + " deploys a Dunlain Reaper on " + planetName(planet) + ".", sys);
        }
    }
    combatRoundStart4(att, def, sys, planet, space, mods);  // Supercharge
    if (G.modules & kModDiscordant) dsCombatRoundStart(att, def, sys, planet, space);  // ATS Armaments, Clan's Favor, Trade Port II
}

void Rules::afterCombatRolls(int att, int def, int sys, RollResult& rA, RollResult& rD, bool munitions[2]) {
    const int side[2] = {att, def};
    RollResult* rr[2] = {&rA, &rD};
    // ATS Armaments (Lanefir, Discordant Stars): reroll up to that many misses
    for (int i = 0; i < 2; ++i)
        if (dsRerolls_[i] > 0 && rr[i]->hits < int(rr[i]->dice.size())) {
            int left = dsRerolls_[i];
            for (size_t d = 0; d < rr[i]->dice.size() && left > 0; ++d)
                if (rr[i]->dice[d] < rr[i]->need[d]) {
                    rr[i]->dice[d] = int8_t(E.rng().d10());
                    --left;
                }
            recount(*rr[i]);
            dsRerolls_[i] = 0;
            reportRoll(side[i], sys, "ATS Armaments reroll", *rr[i]);
        }
    // Munitions Reserves: reroll the misses
    for (int i = 0; i < 2; ++i)
        if (munitions[i] && rr[i]->hits < int(rr[i]->dice.size())) {
            rerollDice(side[i], *rr[i], false);
            reportRoll(side[i], sys, "Munitions Reserves reroll", *rr[i]);
        }
    // War Funding (Letnev's note): reroll the misses
    for (int i = 0; i < 2; ++i)
        if (warFunding_[i] && rr[i]->hits < int(rr[i]->dice.size())) {
            rerollDice(side[i], *rr[i], false);
            reportRoll(side[i], sys, "War Funding reroll", *rr[i]);
        }
    // War Funding Omega (Codex, 0.31.0): after both sides roll, the holder may reroll all of the opponent's
    // dice and any of their own (misses); the note goes back to Letnev
    if (codexOn(G))
        for (int i = 0; i < 2; ++i) {
            const int p = side[i], o = side[1 - i];
            const int Lt = playerOfFaction(G, "letnev");
            if (Lt < 0 || Lt == p || !holdsNote(G, p, Lt, kNoteFaction)) continue;
            if (rr[1 - i]->hits == 0 && rr[i]->hits == int(rr[i]->dice.size())) continue;  // nothing to gain
            if (G.fixes >= 26) {
                // (fixes 26, the Letnev review - owner 2026-11-12) "You may reroll all of your opponent's dice. You may reroll
                // any number of your dice." - two choices: the opponent's dice only if the holder says so (not asked when
                // they rolled no hits: a reroll could only give them some)
                if (!fyes(p, Ab::WarFunding,
                          "Play War Funding: reroll your misses; you may also reroll all of " + pn(o) + "'s dice (" +
                              std::to_string(rr[1 - i]->hits) + " hits)?",
                          factionNoteText(G.players[Lt].faction, kNoteFaction)))
                    continue;
                returnNote(p, makeNote(Lt, kNoteFaction));
                bool theirs = false;
                if (rr[1 - i]->hits > 0) {
                    int expect10 = 0;  // (for the bots: the hits the opponent's dice score on average, x10)
                    for (int8_t nd : rr[1 - i]->need) expect10 += std::max(0, std::min(10, 11 - int(nd)));
                    curValue_ = rr[1 - i]->hits * 1000 + expect10;
                    theirs = fyes(p, Ab::WarFunding,
                                  "War Funding: reroll all of " + pn(o) + "'s dice (" + std::to_string(rr[1 - i]->hits) +
                                      (rr[1 - i]->hits == 1 ? " hit" : " hits") + ")?",
                                  "All of them are rolled again - hits too.");
                    curValue_ = 0;
                }
                abilityEvent(E, p, "War Funding",
                             pn(p) + " plays War Funding" + (theirs ? ": " + pn(o) + "'s dice are rolled again." : "."), sys);
                if (theirs) {
                    rerollDice(o, *rr[1 - i], true);
                    reportRoll(o, sys, "War Funding reroll", *rr[1 - i]);
                }
                if (rr[i]->hits < int(rr[i]->dice.size())) {
                    rerollDice(p, *rr[i], false);
                    reportRoll(p, sys, "War Funding reroll", *rr[i]);
                }
                break;
            }
            if (!fyes(p, Ab::WarFunding,
                      "Play War Funding: reroll all of " + pn(o) + "'s dice (" + std::to_string(rr[1 - i]->hits) +
                          " hits) and your misses?",
                      factionNoteText(G.players[Lt].faction, kNoteFaction)))
                continue;
            returnNote(p, makeNote(Lt, kNoteFaction));
            abilityEvent(E, p, "War Funding", pn(p) + " plays War Funding: " + pn(o) + "'s dice are rolled again.", sys);
            rerollDice(o, *rr[1 - i], true);
            reportRoll(o, sys, "War Funding reroll", *rr[1 - i]);
            if (rr[i]->hits < int(rr[i]->dice.size())) {
                rerollDice(p, *rr[i], false);
                reportRoll(p, sys, "War Funding reroll", *rr[i]);
            }
            break;
        }
    // Wrath of Kenara: +1 per trade good
    for (int i = 0; i < 2; ++i) {
        int p = side[i];
        if (!flagshipAbility(G, p, "hacan") || !flagshipIn(G, p, sys) || G.players[p].tradeGoods <= 0) continue;
        std::vector<Option> opts;
        for (size_t d = 0; d < rr[i]->dice.size(); ++d)
            if (rr[i]->dice[d] == rr[i]->need[d] - 1)
                opts.push_back({int(d), std::string("Die showing ") + std::to_string(rr[i]->dice[d]), "becomes a hit"});
        if (opts.empty()) continue;
        std::vector<int> picks = fpickMany(p, Ab::WrathOfKenara, "Wrath of Kenara: spend trade goods to add 1 to dice?", opts, 0,
                                           std::min<int>(int(opts.size()), G.players[p].tradeGoods), "1 trade good per die.");
        if (picks.empty()) continue;
        for (int d : picks) rr[i]->need[d] = int8_t(rr[i]->need[d] - 1);
        G.players[p].tradeGoods = int8_t(G.players[p].tradeGoods - int(picks.size()));
        recount(*rr[i]);
        reportRoll(p, sys, "Wrath of Kenara (+1)", *rr[i]);
    }
}

void Rules::afterSpaceRound(int att, int def, int sys) {
    // Exotrireme II: destroy it to destroy up to 2 ships here.
    // (fixes 25, owner's faction debug pass 2026-11-11: each Exotrireme II has the ability - asked again while one is left
    // and the opponent has ships (LRR 1.18); the targets are listed damaged / undamaged, so the Sardakk choose which go)
    static const char* const kExoText =
        "Exotrireme II: \"After a round of space combat, you may destroy this unit to destroy up to 2 ships in this system.\"";
    for (int p : {att, def}) {
        if (p >= G.numPlayers) continue;  // (Thunder's Edge: neutral units have no abilities)
        int o = p == att ? def : att;
        for (int again = 0;; ++again) {
            Force& f = G.systems[sys].space[p];
            if (!fac(p, "sardakk") || std::strcmp(unitKey(G, p, UnitType::Dreadnought), "sardakk_dreadnought2") != 0 ||
                f.count(UnitType::Dreadnought) == 0 || !hasShips(G, o, sys))
                break;
            if (again > 0 && G.fixes < 25) break;
            if (!fyes(p, Ab::Exotrireme,
                      again ? "Destroy another Exotrireme II to destroy up to 2 more of " + pn(o) + "'s ships?"
                            : "Destroy an Exotrireme II to destroy up to 2 of " + pn(o) + "'s ships?",
                      kExoText))
                break;
            std::vector<Option> opts;
            Force& e = G.systems[sys].space[o];
            for (int t : kShipIdx) {
                if (G.fixes >= 25) {  // id: type * 16 + k (undamaged), + 8 (damaged)
                    const std::string name = unitTypeName(UnitType(t));
                    for (int k = 0; k < e.n[t] - e.dmg[t] && k < 2; ++k) opts.push_back({t * 16 + k, name, ""});
                    for (int k = 0; k < e.dmg[t] && k < 2; ++k) opts.push_back({t * 16 + 8 + k, name + " (damaged)", ""});
                } else {
                    for (int k = 0; k < e.n[t] && k < 2; ++k) opts.push_back({t * 16 + k, unitTypeName(UnitType(t)), ""});
                }
            }
            std::vector<int> picks = fpickMany(p, Ab::Exotrireme, "Exotrireme II: destroy which ships?", opts, 1,
                                               std::min<int>(2, int(opts.size())));
            destroyUnits(p, UnitType::Dreadnought, 1, sys, -1, -1, "Exotrireme II");
            int count[kUnitTypes] = {}, damaged[kUnitTypes] = {};
            for (int id : picks) {
                ++count[id / 16];
                if (G.fixes >= 25 && (id % 16) >= 8) ++damaged[id / 16];
            }
            for (int t = 0; t < kUnitTypes; ++t) {
                if (!count[t]) continue;
                if (G.fixes < 25) {
                    destroyUnits(o, UnitType(t), count[t], sys, -1, p, "Exotrireme II");
                    continue;
                }
                // (destroyUnits takes damaged units first: the damaged ones not chosen step aside meanwhile)
                const int hidden = std::max(0, int(e.dmg[t]) - damaged[t]);
                e.dmg[t] = uint8_t(e.dmg[t] - hidden);
                destroyUnits(o, UnitType(t), count[t], sys, -1, p, "Exotrireme II");
                Force& e2 = G.systems[sys].space[o];
                e2.dmg[t] = uint8_t(std::min<int>(e2.n[t], e2.dmg[t] + hidden));
            }
            removeExcessCapacity(o, sys);
        }
    }
}

void Rules::groundCombatStartFactions(int att, int def, int planet) {
    const int sys = G.planets[planet].system;
    groundCombatStart3(att, def, planet);  // Antivirus
    // Claire Gibson (Sol commander): +1 infantry when defending your planet
    auto claire = [&](bool late) {
        for (int p : {att, def}) {
            if (p >= G.numPlayers) continue;  // (Thunder's Edge: neutral units have no abilities)
            // (fixes 26, the Sol review - LRR 1.19: abilities at the same time go in initiative order from the active
            // player, so the active player's Indoctrination / Greyfire Mutagen come first)
            if ((G.fixes >= 26 && G.phase == Phase::Action && G.activePlayer >= 0 && p != G.activePlayer) != late) continue;
            if (G.planets[planet].owner == p && commanderUsable(G, p, "sol") &&
                fyes(p, Ab::SolCommander, "Claire Gibson: put 1 infantry on " + planetName(planet) + "?", "")) {
                addUnits(p, UnitType::Infantry, 1, sys, planet);
                say(p, pn(p) + " puts 1 infantry on " + planetName(planet) + " (Claire Gibson).", sys);
            }
        }
    };
    claire(false);
    groundCombatStart2(att, def, planet);  // Indoctrination, Greyfire Mutagen
    claire(true);
    tekklar_[att] = tekklar_[def] = 0;
    if (G.activeSystem != sys) return;  // only during an invasion
    for (int p : {att, def}) {
        if (p >= G.numPlayers) continue;  // (Thunder's Edge: neutral units have no abilities)
        int o = p == att ? def : att;
        int S = playerOfFaction(G, "sardakk");
        if (S < 0 || S == p || !holdsNote(G, p, S, kNoteFaction)) continue;
        if (!fyes(p, Ab::Tekklar, "Play Tekklar Legion?", factionNoteText(G.players[S].faction, kNoteFaction))) continue;
        tekklar_[p] += 1;
        if (o == S) tekklar_[o] -= 1;
        returnNote(p, makeNote(S, kNoteFaction));
        abilityEvent(E, p, "Tekklar Legion", pn(p) + " plays Tekklar Legion.", sys);
    }
}

void Rules::sustained(int victim, int attacker, int sys, int planet, int t) {
    // Dynamo (Empyrean flagship). (fixes 26, the Empyrean review - owner 2026-11-12) "After any player's unit in this
    // system or an adjacent system uses SUSTAIN DAMAGE": each Dynamo near, in the order of LRR 1.19 (the first repairs it)
    if (G.fixes >= 26) {
        if (victim < G.numPlayers)
            for (int e : windowOrder())
                if (e < G.numPlayers) dynamo(e, sys, planet, t, victim);
    } else {
        dynamo(victim, sys, planet, t);
    }
    // Rear Admiral Farran (Letnev commander)
    if (commanderUsable(G, victim, "letnev")) {
        gainTG(victim, 1);
        say(victim, pn(victim) + " gains 1 trade good (Rear Admiral Farran).");
    }
    // Valkyrie Exoskeleton (Sardakk mech): 1 hit back in ground combat
    if (inGroundCombat_ && planet >= 0 && t == int(UnitType::Mech) && fac(victim, "sardakk") && attacker >= 0 && mechAbilities(G) &&
        G.planets[planet].units[attacker].groundForces() > 0) {
        say(victim, pn(victim) + "'s Valkyrie Exoskeleton strikes back.", sys);
        assignHits(attacker, sys, planet, 1, kGroundMask, "Valkyrie Exoskeleton", victim);
    }
    if (G.modules & kModDiscordant) dsSustained(victim, attacker, sys, planet, t);  // Krovoz Strike Teams (Vaden)
}

void Rules::afterUnitAbilityRoll(int roller, int sys, RollResult& r) {
    // Agnlan Oln (Jol-Nar commander): reroll unit ability dice
    if (r.hits >= int(r.dice.size()) || !commanderUsable(G, roller, "jolnar")) return;
    if (!fyes(roller, Ab::JolnarCommander, "Agnlan Oln: reroll your misses?",
              std::to_string(r.hits) + " of " + std::to_string(r.dice.size()) + " dice hit."))
        return;
    rerollDice(roller, r, false);
    reportRoll(roller, sys, "Rerolled (Agnlan Oln)", r);
}

}  // namespace ti
