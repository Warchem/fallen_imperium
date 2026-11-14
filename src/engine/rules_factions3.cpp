// Stage 5, batch 3: the Ghosts of Creuss, Embers of Muaat, Clan of Saar,
// Winnu, Nekro Virus and Empyrean. Original Prophecy of Kings card versions
// (owner's choice). Agents are only offered when their owner is involved
// (owner OK'd, RULES-CHECK 0.6.0 #2).
#include <algorithm>
#include <cstring>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {

void abilityEvent3(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    e.system = int16_t(sys);
    E.emit(e);
}
void heroEvent3(Engine& E, const Game& G, int p, const std::string& text) {
    int i = leaderIndex(G, p, kHero);
    Event e;
    e.kind = EventKind::Leader;
    e.player = int8_t(p);
    e.card = int16_t(i >= 0 ? G.players[p].leaders[i].leader : 0);
    e.label = "hero";
    e.text = text;
    E.emit(e);
}
const char* tokenName(int k) { return k == 0 ? "alpha" : k == 1 ? "beta" : "gamma"; }
bool controlsMecatol(const Game& G, int p) {
    int mec = G.mecatol();
    if (mec < 0) return false;
    for (int16_t x : G.systems[mec].planets)
        if (G.planets[x].owner == p) return true;
    return false;
}
int mecatolPlanet(const Game& G) {
    int mec = G.mecatol();
    return mec >= 0 && !G.systems[mec].planets.empty() ? G.systems[mec].planets[0] : -1;
}
bool besideAnomaly(const Game& G, int sys) {
    for (int16_t a : G.systems[sys].adj) {
        if (G.systems[a].d().anomalies) return true;
        // (0.44.7, fixes 11: the anomalies the rules make too - a Dimensional Tear, the Nivyn Wound, the Veldyr home
        // with Aurora Research Base)
        if (G.fixes < 11) continue;
        if (dimensionalTear(G, a)) return true;
        if ((G.modules & kModDiscordant) && (a == G.dsWound || (G.systems[size_t(a)].home >= 0 && breakthroughsOn(G) &&
                                                                 breakthroughIs(G, G.systems[size_t(a)].home, "veldyr"))))
            return true;
    }
    return false;
}
bool hasLegendary(const Game& G, int sys) {
    for (int16_t x : G.systems[sys].planets)
        if (kPlanets[G.planets[x].def].legendary) return true;
    return false;
}
}  // namespace

// ------------------------------------------------------------------ shared
void Rules::spendStrategy(int p, int n) {
    if (n <= 0) return;
    if (G.modules & kModDiscordant) {
        while (n > 0 && dsStoneEmbrace(p)) --n;  // Stone's Embrace (Khrask): a planet instead of the token
        dsStrategySpent(p);                       // the Rook is repaired (Kolume mech)
        if (n <= 0) return;
    }
    G.players[p].strategy = int8_t(G.players[p].strategy - n);
    if ((G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtSpentStrategy(p);  // Galactic Movement (0.43.0)
    // Magmus (Muaat commander): a trade good for each strategy token spent
    if (commanderUsable(G, p, "muaat")) {
        gainTG(p, n);
        say(p, pn(p) + " gains " + std::to_string(n) + " trade good" + (n > 1 ? "s" : "") + " (Magmus).");
    }
}

// 0.52.0 (owner D117, fixes 16): the Ghemina's second flagship, the Lady, is a unit of its own (UnitType::Flagship2); their
// Flagship is the Lord. Older games keep both as flagships (Game::dsLadySys says which one is the Lady).
void Rules::secondFlagship(int p) {
    static const int lady = unitByKey("ghemina_flagship_lady"), celagrom = unitByKey("celdauri_celagrom");
    if (G.fixes >= 16 && (G.modules & kModDiscordant) && fac(p, "ghemina") && lady >= 0)
        G.players[size_t(p)].unit[int(UnitType::Flagship2)] = int16_t(lady);
    // 0.52.1 (fixes 17): the Celdauri's Celagrom, a second flagship with its own card, once they have Trade Protectorate
    if (G.fixes >= 17 && dsbtOn(p, "celdauri") && celagrom >= 0) G.players[size_t(p)].unit[int(UnitType::Flagship2)] = int16_t(celagrom);
}

void Rules::refreshUnits(int p) {
    Player& pl = G.players[p];
    const FactionDef& f = kFactions[pl.faction];
    for (int t = 0; t < kUnitTypes; ++t) pl.unit[t] = int16_t(genericUnit(UnitType(t)));
    for (int u : f.units)
        if (u >= 0 && kUnits[u].upgradeOf < 0) pl.unit[int(kUnits[u].type)] = int16_t(u);
    secondFlagship(p);  // (0.52.0: the Ghemina's Lady)
    auto apply = [&](int tech, int faction) {
        for (int u = 0; u < kUnitCount; ++u) {
            if (kUnits[u].tech != tech) continue;
            if (kUnits[u].faction >= 0 && kUnits[u].faction != faction) continue;
            pl.unit[int(kUnits[u].type)] = int16_t(u);
        }
    };
    for (int16_t t : pl.techs)
        if (kTechs[t].unitUpgrade) apply(t, pl.faction);
    // Valefar Assimilator on another faction's unit upgrade: that unit replaces ours (LRR 90.16, 97.1a)
    for (int t : {int(pl.assimX), int(pl.assimY)})
        if (t >= 0 && kTechs[t].unitUpgrade && kTechs[t].faction >= 0) apply(t, kTechs[t].faction);
    // Arms Brokerage (Axis breakthrough, 0.43.0): a unit upgrade lent for this turn
    if (G.dsLendP == p && G.dsLendTech >= 0) {
        int axis = -1;
        for (int q = 0; q < G.numPlayers; ++q)
            if (fac(q, "axis")) axis = q;
        apply(G.dsLendTech, axis >= 0 ? G.players[size_t(axis)].faction : pl.faction);
    }
    if (G.modules & kModDiscordant) dsbtUnits(p);  // the flip cards (0.43.0)
}

// ------------------------------------------------------------------ Creuss
// Wormhole Generator / Creuss Iff / Icarus Drive: place or move one of the Creuss tokens.
bool Rules::moveCreussToken(int p, Ab ab, int intoSys, bool optional) {
    std::vector<Option> toks;
    for (int k = 0; k < 3; ++k)
        toks.push_back({k, std::string("The ") + tokenName(k) + " token",
                        G.creussTokens[k] >= 0 ? "Now in " + sysName(G.creussTokens[k]) : "Not on the board yet"});
    if (optional) toks.push_back({-1, "Cancel", ""});
    int k = fpick(p, ab, "Which Creuss wormhole token?", toks);
    if (k < 0) return false;
    int sys = intoSys;
    if (sys < 0) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            const SystemState& sy = G.systems[s];
            // (0.44.9, fixes 13 - the owner's answer P2: an inactive Wormhole Nexus may take a token - it comes back to
            // the Creuss when the nexus becomes active; the Creuss home system may not)
            const bool nexus = G.fixes >= 13 && int(s) == G.nexus && !sy.nexusActive;
            if (sy.isHyperlane() || (sy.offBoard && !nexus) || int(s) == G.creussTokens[k]) continue;
            bool mine = false;
            for (int16_t x : sy.planets)
                if (G.planets[x].owner == p) mine = true;
            if (mine || (sy.home < 0 && !otherShipsIn(G, p, int(s)))) opts.push_back({int(s), sysName(int(s)), ""});
        }
        if (opts.empty()) return false;
        if (optional) opts.push_back({-1, "Cancel", ""});
        sys = fpick(p, ab, std::string("Put the ") + tokenName(k) + " token in which system?", opts,
                    "A system with a planet you control, or a non-home system without other players' ships.", true);
        if (sys < 0) return false;
    }
    G.creussTokens[k] = int16_t(sys);
    computeAdjacency(G);
    abilityEvent3(E, p, "Creuss wormhole", pn(p) + " puts the Creuss " + tokenName(k) + " wormhole in " + sysName(sys) + ".", sys);
    return true;
}

// The original Wormhole Generator (0.44.9, fixes 13, games without the Codex / Thunder's Edge): at the start of the
// status phase each owner may place or move a Creuss wormhole token
void Rules::wormholeGeneratorStatus() {
    if (G.fixes < 13 || codexOn(G)) return;
    for (int p : initiativeOrder(G))
        // (fixes 24, owner's faction debug pass: the card has no "may" - a token is placed or moved)
        if (!G.players[size_t(p)].eliminated && owns(p, "wg")) moveCreussToken(p, Ab::WormholeGenerator, -1, G.fixes < 24);
}
// (0.44.9, fixes 13) the Wormhole Nexus became active: Creuss tokens in it go back to the Creuss
void Rules::nexusTokensBack() {
    if (G.fixes < 13 || G.nexus < 0) return;
    bool any = false;
    for (int16_t& t : G.creussTokens)
        if (t == G.nexus) t = -1, any = true;
    if (!any) return;
    computeAdjacency(G);
    say(playerOfFaction(G, "ghost"), "The Creuss wormhole token in the Wormhole Nexus goes back to the Creuss.", G.nexus);
}

// ------------------------------------------------------------------ after activation
// Icarus Drive (fixes 24, owner's faction debug pass - the card: "After any player activates a system, you may remove
// this unit from the game board to place or move a Creuss wormhole token into this system"; "this system" = the one the
// mech is in, not the active one): after every activation, any of the Creuss' mechs on the board (on a planet or in
// space) - each one used goes, and a token comes to its system. Not from the Creuss home (kept off the board).
// A person may say "don't ask again this round".
void Rules::icarusDrive(int c, int p, int sys) {
    if (c < 0 || G.players[size_t(c)].eliminated || !mechAbilities(G)) return;
    if (G.players[size_t(c)].human && icarusQuiet_[c] == G.round) return;
    for (int round = 0; round < 6; ++round) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            const SystemState& sy = G.systems[s];
            if (sy.isHyperlane() || (sy.offBoard && !(int(s) == G.nexus && sy.nexusActive))) continue;
            if (sy.space[size_t(c)].count(UnitType::Mech))
                opts.push_back({int(s) * 16, sysName(int(s)), "A mech in space there"});
            for (size_t k = 0; k < sy.planets.size() && k < 15; ++k)
                if (G.planets[size_t(sy.planets[k])].units[size_t(c)].count(UnitType::Mech))
                    opts.push_back({int(s) * 16 + int(k) + 1, sysName(int(s)), "Your mech on " + planetName(sy.planets[k])});
        }
        if (opts.empty()) return;
        opts.push_back({-1, "No", ""});
        if (G.players[size_t(c)].human) opts.push_back({-2, "No, and don't ask again this round", "You'll be asked again next round."});
        curValue_ = sys;  // (the active system, for the AI)
        const int pickId = fpick(c, Ab::IcarusDrive, "Icarus Drive: remove a mech to bring a Creuss wormhole token to ITS system?", opts,
                                 pn(p) + " activated " + sysName(sys) + ". The token goes to the system the mech is in - not the active "
                                 "system.");
        curValue_ = 0;
        if (pickId == -2) icarusQuiet_[c] = int16_t(G.round);
        if (pickId < 0) return;
        const int ms = pickId / 16, slot = pickId % 16;
        const int planet = slot > 0 ? G.systems[size_t(ms)].planets[size_t(slot - 1)] : -1;
        const bool was = removing_;
        removing_ = true;
        destroyUnits(c, UnitType::Mech, 1, ms, planet, -1, "Icarus Drive");
        removing_ = was;
        curValue_ = ms;  // (where the token goes, for the AI)
        moveCreussToken(c, Ab::IcarusDrive, ms, false);
        curValue_ = 0;
    }
}

// Emissary Taivra (fixes 24, owner's faction debug pass - the card: "After a player activates a system that contains a
// non-delta wormhole: You may exhaust this card; if you do, that system is adjacent to all other systems that contain a
// wormhole during this tactical action"): epsilon wormholes count; after another player's activation too - asked of a
// person (with "don't ask again this round") or of a bot that promised that player its agent in a deal
void Rules::emissaryTaivra(int p, int sys) {
    if (G.fx.taivra || !(wormholesOf(G, sys) & uint8_t(~kDelta))) return;
    const std::string ctx = "This tactical action, " + sysName(sys) + " is adjacent to every other system with a wormhole - for "
                            "everyone (movement, retreats, space cannon, neighbours).";
    if (canUseAgent(p, "ghost")) {
        if (fyes(p, Ab::CreussAgent, "Exhaust Emissary Taivra?", ctx)) {
            spendAgent(p, "ghost");
            G.fx.taivra = true;
            say(p, sysName(sys) + " opens onto every wormhole (Emissary Taivra).", sys);
        }
        return;
    }
    for (int o : initiativeOrder(G)) {
        if (o == p || !canUseAgent(o, "ghost")) continue;
        const bool promised = G.agentFor[o] == p;
        const bool human = G.players[size_t(o)].human;
        if (!promised && !(human && taivraQuiet_[o] != G.round)) continue;
        agentFor(o, p);
        curValue_ = sys;
        std::vector<Option> opts = {{1, "Yes", ""}, {0, "No", ""}};
        if (human && !promised) opts.push_back({3, "No, and don't ask again this round", "You'll be asked again next round."});
        const int c = fpick(o, Ab::CreussAgent, "Exhaust Emissary Taivra for " + pn(p) + "?", opts, pn(p) + " activated " + sysName(sys) + ". " + ctx);
        curValue_ = 0;
        curFor_ = -1;
        if (c == 3) taivraQuiet_[o] = int16_t(G.round);
        if (c != 1) continue;
        spendAgent(o, "ghost");
        agentDone(o, p);
        G.fx.taivra = true;
        say(o, sysName(sys) + " opens onto every wormhole (" + pn(o) + "'s Emissary Taivra).", sys);
        return;
    }
}

void Rules::afterActivation3(int p, int sys) {
    const SystemState& s = G.systems[sys];
    // Icarus Drive (Creuss mech): remove it to bring a Creuss wormhole token here
    int c = playerOfFaction(G, "ghost");
    if (G.fixes >= 24) {
        icarusDrive(c, p, sys);
    } else if (c >= 0 && !G.players[c].eliminated && mechAbilities(G)) {
        int mechPlanet = -1;
        for (int16_t x : s.planets)
            if (G.planets[x].units[c].count(UnitType::Mech)) mechPlanet = x;
        if (mechPlanet >= 0 && fyes(c, Ab::IcarusDrive, "Icarus Drive: remove your mech on " + planetName(mechPlanet) +
                                                             " to put a Creuss wormhole token in " + sysName(sys) + "?", "")) {
            removing_ = true;
            destroyUnits(c, UnitType::Mech, 1, sys, mechPlanet, -1, "Icarus Drive");
            removing_ = false;
            moveCreussToken(c, Ab::IcarusDrive, sys, false);
        }
    }
    // Emissary Taivra (Creuss agent): the active system becomes adjacent to every wormhole
    if (G.fixes >= 24) {
        emissaryTaivra(p, sys);
    } else if (canUseAgent(p, "ghost") && (wormholesOf(G, sys) & (kAlpha | kBeta | kGamma)) &&
        fyes(p, Ab::CreussAgent, "Exhaust Emissary Taivra?",
             "This tactical action, " + sysName(sys) + " is adjacent to every other system with a wormhole.")) {
        spendAgent(p, "ghost");
        G.fx.taivra = true;
        say(p, sysName(sys) + " opens onto every wormhole (Emissary Taivra).", sys);
    }
    // Captain Mendosa (Saar agent): one ship moves as far as the fastest ship on the board
    if (G.fixes >= 27) mendosa27(p, sys);
    else if (canUseAgent(p, "saar")) {
        const int top = maxShipMove(G);
        bool helps = false, faster = false;
        for (size_t f = 0; f < G.systems.size() && !helps; ++f) {
            if (int(f) == sys || G.systems[f].hasToken(p)) continue;
            for (int t : kShipIdx) {
                if (!G.systems[f].space[p].n[t] || UnitType(t) == UnitType::Fighter) continue;
                int mv = moveValue(G, p, int(f), UnitType(t));
                if (mv <= 0 || mv >= top) continue;
                faster = true;
                if (!shipReach(G, p, int(f), sys, UnitType(t), owns(p, "gd") ? 1 : 0).ok &&
                    shipReach(G, p, int(f), sys, UnitType(t), top - mv + (owns(p, "gd") ? 1 : 0)).ok)
                    helps = true;
            }
        }
        // 0.43.0 (owner: not offered at Mecatol Rex): a person is asked whenever a ship of theirs would get faster
        // (fixes 2) - the check "some ship reaches only with it" stays for the bots
        if ((helps || (G.fixes >= 2 && G.players[p].human && faster)) && fyes(p, Ab::SaarAgent, "Exhaust Captain Mendosa?",
                          "1 of your ships gets move " + std::to_string(top) + " (the fastest ship on the board) this movement.")) {
            spendAgent(p, "saar");
            G.fx.mendosa = true;
            say(p, pn(p) + " calls on Captain Mendosa: one ship moves " + std::to_string(top) + ".", sys);
        }
    }
    // Aetherstream (Empyrean): +1 move next to an anomaly
    if (G.fixes >= 11) {
        // 0.44.7: "you or one of your neighbours" - the owner may give the active player's ships the +1 too
        if (besideAnomaly(G, sys))
            for (int q : windowOrder()) {
                if (q >= G.numPlayers || G.players[size_t(q)].eliminated || !owns(q, "as") || (q != p && !areNeighbours(G, q, p))) continue;
                const int prevFor = curFor_;
                curFor_ = p;
                const bool give = fyes(q, Ab::Aetherstream,
                                       q == p ? "Use Aetherstream: +1 move for all your ships this tactical action?"
                                              : "Aetherstream: give " + pn(p) + "'s ships +1 move this tactical action?",
                                       q == p ? "" : pn(p) + " (your neighbour) activated " + sysName(sys) + ", next to an anomaly.");
                curFor_ = prevFor;
                if (!give) continue;
                G.fx.aetherstream = true;
                say(p, q == p ? pn(p) + " rides the Aetherstream (+1 move)." : pn(q) + " lets " + pn(p) + " ride the Aetherstream (+1 move).", sys);
                break;
            }
    } else if (techReady(p, "as") && besideAnomaly(G, sys) &&
               fyes(p, Ab::Aetherstream, "Use Aetherstream: +1 move for all your ships this tactical action?", "")) {
        G.fx.aetherstream = true;
        say(p, pn(p) + " rides the Aetherstream (+1 move).", sys);
    }
    // Aetherpassage (Empyrean): let the active player move through systems with Empyrean ships
    int e = playerOfFaction(G, "empyrean");
    if (e >= 0 && e != p && !G.players[e].eliminated) {
        bool matters = false;
        const bool gd = owns(p, "gd");
        for (size_t f = 0; f < G.systems.size() && !matters; ++f) {
            if (int(f) == sys || G.systems[f].hasToken(p)) continue;
            for (int t : kShipIdx) {
                if (matters || !G.systems[f].space[p].n[t] || shipReach(G, p, int(f), sys, UnitType(t), gd ? 1 : 0).ok) continue;
                G.fx.aetherFrom = int8_t(e);
                if (shipReach(G, p, int(f), sys, UnitType(t), gd ? 1 : 0).ok) matters = true;
                G.fx.aetherFrom = -1;
            }
        }
        if (matters && fyes(e, Ab::Aetherpassage, "Aetherpassage: let " + pn(p) + " move ships through systems with your ships?",
                            pn(p) + " activated " + sysName(sys) + ".")) {
            G.fx.aetherFrom = int8_t(e);
            G.players[e].usedFactionAbility = true;
            abilityEvent3(E, e, "Aetherpassage", pn(e) + " opens an Aetherpassage for " + pn(p) + ".", sys);
        }
    }
    if (G.fixes >= 26) voidTetherAllow(p, sys);  // (the Empyrean review) Void Tether: "... unless you allow it"
}

// ------------------------------------------------------------------ after movement
void Rules::afterMove3(int p, int sys, int wormholeCarriers, bool warSunMoved) {
    SystemState& s = G.systems[sys];
    if (s.space[p].count(UnitType::Flagship) && (fac(p, "ghost") || (G.fixes >= 13 && flagshipAbility(G, p, "ghost"))))
        computeAdjacency(G);  // Hil Colish's delta wormhole (0.44.9: a Valefar Z copy too)
    // Sai Seravus (Creuss commander): a fighter with each ship that came through a wormhole
    if (wormholeCarriers > 0 && commanderUsable(G, p, "ghost") && hasShips(G, p, sys)) {
        int room = std::max(0, shipCapacity(G, p, sys) - s.space[p].count(UnitType::Fighter) - s.space[p].groundForces());
        int k = std::min({wormholeCarriers, room, unitsAvailable(G, p, UnitType::Fighter)});
        if (k > 0 && G.fixes >= 24) {  // (fixes 24, owner's faction debug pass: each ship "may" - choose how many)
            std::vector<Option> opts;
            for (int i = k; i >= 1; --i) opts.push_back({i, "Place " + std::to_string(i) + " fighter" + (i > 1 ? "s" : ""), ""});
            opts.push_back({0, "None", ""});
            k = fpick(p, Ab::CreussCommander, "Sai Seravus: how many fighters with your ships that came through a wormhole?", opts,
                      std::to_string(wormholeCarriers) + " ship" + (wormholeCarriers > 1 ? "s" : "") +
                          " with capacity came through a wormhole; 1 fighter each, as room allows.");
            if (k > 0) {
                addUnits(p, UnitType::Fighter, k, sys, -1);
                say(p, pn(p) + " places " + std::to_string(k) + " fighter" + (k > 1 ? "s" : "") + " (Sai Seravus).", sys);
            }
        } else if (k > 0 && fyes(p, Ab::CreussCommander, "Sai Seravus: place " + std::to_string(k) + " fighter" + (k > 1 ? "s" : "") +
                                                             " with your ships that came through a wormhole?", "")) {
            addUnits(p, UnitType::Fighter, k, sys, -1);
            say(p, pn(p) + " places " + std::to_string(k) + " fighter" + (k > 1 ? "s" : "") + " (Sai Seravus).", sys);
        }
    }
    // Adjudicator Ba'al (Muaat hero): turn the system into a supernova
    // (fixes 26, the Muaat review - owner 2026-11-12: not in the Fracture - the designer's ruling; neutral units are
    // destroyed too; the Muaat's own units on the purged planets return to their reinforcements; a frontier token stays)
    const bool baal26 = G.fixes >= 26;
    if (warSunMoved && fac(p, "muaat") && heroUnlocked(G, p) && s.space[p].count(UnitType::WarSun) &&
        ((s.home < 0 && sys != G.mecatol()) || eventOn(G, kEvWild)) && !s.offBoard && s.home != p &&  // (Wild, Wild Galaxy: anywhere)
        !(baal26 && s.fracture)) {
        bool others = false;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && hasUnits(G, q, sys)) others = true;
        if (baal26 && !s.space[kNeutral].empty()) others = true;
        for (int16_t x : s.planets)
            if (baal26 && !G.planets[size_t(x)].units[kNeutral].empty()) others = true;
        if (fyes(p, Ab::MuaatHero, "Use Adjudicator Ba'al on " + sysName(sys) + "?",
                 baal26 ? std::string("Destroy all other players' units there and replace the tile with the Muaat supernova") +
                              (s.planets.empty() ? "" : "; its planet cards are purged and your own units on them return to your reinforcements") +
                              ". The hero is purged." + (others ? "" : " (Nobody else is there.)")
                        : std::string("Destroy every other player's units there") + (s.planets.empty() ? "" : " and its planets") +
                              "; the system becomes a supernova. The hero is purged." + (others ? "" : " (Nobody else is there.)"))) {
            purgeHero(p);
            heroEvent3(E, G, p, pn(p) + " uses Adjudicator Ba'al: " + sysName(sys) + " becomes a supernova.");
            for (int q = 0; q < G.numPlayers; ++q) {
                if (q == p) continue;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (s.space[q].n[t]) destroyUnits(q, UnitType(t), s.space[q].n[t], sys, -1, p, "Adjudicator Ba'al");
            }
            if (baal26) {
                for (int t = 0; t < kUnitTypes; ++t)
                    if (s.space[kNeutral].n[t]) destroyUnits(kNeutral, UnitType(t), s.space[kNeutral].n[t], sys, -1, p, "Adjudicator Ba'al");
                for (int16_t x : std::vector<int16_t>(s.planets)) {
                    PlanetState& ps = G.planets[size_t(x)];
                    for (int t = 0; t < kUnitTypes; ++t)
                        if (ps.units[kNeutral].n[t]) destroyUnits(kNeutral, UnitType(t), ps.units[kNeutral].n[t], sys, x, p, "Adjudicator Ba'al");
                    if (!ps.units[size_t(p)].empty()) {
                        say(p, pn(p) + "'s units on " + planetName(x) + " return to their reinforcements (Adjudicator Ba'al).", sys);
                        ps.units[size_t(p)] = Force{};
                    }
                }
            }
            for (int16_t x : std::vector<int16_t>(s.planets)) destroyPlanet(x, p, "Adjudicator Ba'al");
            int nova = findSystem(G.fixes >= 2 ? "81" : "43");  // 0.43.0 (owner D44): the Muaat's own supernova, Nova Seed
            if (nova < 0) nova = findSystem("43");
            if (nova >= 0) s.def = int16_t(nova);
            if (!baal26) s.frontier = false;
            s.tokenWormholes = 0;
            if (G.ionStorm == sys) G.ionStorm = -1;
            for (int16_t& t : G.creussTokens)
                if (t == sys) t = -1;
            computeAdjacency(G);
            checkEliminations();
        }
    }
    // Acamar (Empyrean agent): a command token after moving into a system without planets
    // (fixes 26, the Empyrean review - owner 2026-11-12: "After a player moves ships ..." - any player: acamar())
    if (G.fixes >= 26) {
        acamar(p, sys);
    } else if (s.planets.empty() && hasShips(G, p, sys) && canUseAgent(p, "empyrean") && reinforcementTokens(p) > 0 &&
        fyes(p, Ab::EmpyreanAgent, "Exhaust Acamar to gain 1 command token?", "Your ships moved into a system without planets.")) {
        spendAgent(p, "empyrean");
        say(p, pn(p) + " gains a command token (Acamar).", sys);
        gainTokens(p, 1, false, "Acamar");
    }
    // Xuange (Empyrean commander): take back a command token where another player just moved in
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p || !s.hasToken(q) || !commanderUsable(G, q, "empyrean")) continue;
        if (!fyes(q, Ab::EmpyreanCommander, "Xuange: return your command token in " + sysName(sys) + " to your reinforcements?",
                  pn(p) + " just moved ships in."))
            continue;
        s.tokens = uint16_t(s.tokens & ~(1u << q));
        say(q, pn(q) + " takes back their command token in " + sysName(sys) + " (Xuange).", sys);
    }
    // Voidwatch (Empyrean): the mover gives a promissory note
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p || !owns(q, "vw") || !hasUnits(G, q, sys) || G.players[p].notes.empty()) continue;
        std::vector<Option> notes = notesToGive(p, q);
        if (notes.empty()) continue;
        int n = pick(p, Tag::GiveNote, "Voidwatch: give " + pn(q) + " which promissory note?", notes,
                     "You moved ships into a system with " + pn(q) + "'s units.");
        say(p, pn(p) + " gives " + pn(q) + " a promissory note (Voidwatch).");
        giveNote(p, q, n);
    }
}

// Acamar (Empyrean agent, fixes 26 - the Empyrean review, owner 2026-11-12): "After a player moves ships into a system that
// does not contain any planets: You may exhaust this card; that player gains 1 command token." The Empyrean decide; on
// another player's move a person is asked (or "don't ask again this round"), a bot only when it promised that player its
// agent in a deal (as Emissary Taivra). A space station isn't a planet (planetless).
void Rules::acamar(int p, int sys) {
    if (!planetless(G, sys) || !hasShips(G, p, sys) || reinforcementTokens(p) <= 0) return;
    if (canUseAgent(p, "empyrean")) {
        if (fyes(p, Ab::EmpyreanAgent, "Exhaust Acamar to gain 1 command token?", "Your ships moved into a system without planets.")) {
            spendAgent(p, "empyrean");
            say(p, pn(p) + " gains a command token (Acamar).", sys);
            gainTokens(p, 1, false, "Acamar");
        }
        return;
    }
    for (int o : initiativeOrder(G)) {
        if (o == p || !canUseAgent(o, "empyrean")) continue;
        const bool promised = G.agentFor[o] == p;
        const bool human = G.players[size_t(o)].human;
        if (!promised && !(human && acamarQuiet_[o] != G.round)) continue;
        agentFor(o, p);
        std::vector<Option> opts = {{1, "Yes", ""}, {0, "No", ""}};
        if (human && !promised) opts.push_back({3, "No, and don't ask again this round", "You'll be asked again next round."});
        const int c = fpick(o, Ab::EmpyreanAgent, "Exhaust Acamar: " + pn(p) + " gains 1 command token?", opts,
                            pn(p) + " moved ships into " + sysName(sys) + ", a system without planets.");
        curFor_ = -1;
        if (c == 3) acamarQuiet_[o] = int16_t(G.round);
        if (c != 1) continue;
        spendAgent(o, "empyrean");
        agentDone(o, p);
        say(p, pn(p) + " gains a command token (" + pn(o) + "'s Acamar).", sys);
        gainTokens(p, 1, false, "Acamar");
        return;
    }
}

// ------------------------------------------------------------------ start of a turn
void Rules::startOfTurn3(int p) {
    // Creuss Iff (Creuss note): move a Creuss wormhole token, then the note goes back
    int c = playerOfFaction(G, "ghost");
    if (c >= 0 && c != p && holdsNote(G, p, c, kNoteFaction) &&
        fyes(p, Ab::CreussIff, "Play Creuss Iff: place or move a Creuss wormhole token?", factionNoteText(G.players[c].faction, kNoteFaction))) {
        if (moveCreussToken(p, Ab::CreussIff, -1, false)) returnNote(p, makeNote(c, kNoteFaction));
    }
    // Chaos Mapping (Saar): produce 1 unit at the start of your turn
    if (owns(p, "cm") && spendable(G, p, false) > 0) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (productionCapacity(G, p, int(s)) > 0) opts.push_back({int(s), sysName(int(s)), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Don't produce", ""});
            int s = fpick(p, Ab::ChaosMapping, "Chaos Mapping: produce 1 unit in which system?", opts, "", true);
            if (s >= 0) {
                say(p, pn(p) + " produces with Chaos Mapping.", s);
                notProductionAbility_ = true;
                production(p, s, 1, false);
                notProductionAbility_ = false;
            }
        }
    }
}

// ------------------------------------------------------------------ gaining planets
void Rules::gainedControl3(int planet, int p, int old) {
    (void)old;
    const int sys = G.planets[planet].system;
    if (p == G.activePlayer && G.activeSystem >= 0) G.fx.gained.push_back(int16_t(planet));
    if (!fac(p, "saar")) return;
    // Scavenge
    gainTG(p, 1);
    say(p, pn(p) + " gains 1 trade good (Scavenge).");
    G.players[p].usedFactionAbility = true;
    // Scavenger Zeta (Saar mech): DEPLOY for 1 trade good
    if (unitsAvailable(G, p, UnitType::Mech) > 0 && G.players[p].tradeGoods >= 1 && !isDMZ(G, planet) && mechAbilities(G) &&
        fyes(p, Ab::ScavengerZeta, "Spend 1 trade good to deploy a Scavenger Zeta mech on " + planetName(planet) + "?", "")) {
        G.players[p].tradeGoods = int8_t(G.players[p].tradeGoods - 1);
        addUnits(p, UnitType::Mech, 1, sys, planet);
        say(p, pn(p) + " deploys a mech on " + planetName(planet) + ".", sys);
    }
}

void Rules::endOfTactical3(int p, int sys) {
    if (!fac(p, "winnu")) return;
    for (int16_t x : std::vector<int16_t>(G.fx.gained)) {
        if (G.planets[x].owner != p) continue;
        // Reclamation: a PDS and a space dock on Mecatol Rex
        if (x == mecatolPlanet(G) &&
            (unitsAvailable(G, p, UnitType::PDS) > 0 || unitsAvailable(G, p, UnitType::SpaceDock) > 0) &&
            fyes(p, Ab::Reclamation, "Reclamation: put a PDS and a space dock on Mecatol Rex?", "")) {
            addUnits(p, UnitType::PDS, 1, sys, x);
            if (G.planets[x].units[p].count(UnitType::SpaceDock) == 0) addUnits(p, UnitType::SpaceDock, 1, sys, x);
            G.players[p].usedFactionAbility = true;
            abilityEvent3(E, p, "Reclamation", pn(p) + " reclaims Mecatol Rex: a PDS and a space dock.", sys);
            if (G.fixes < 26) continue;  // (fixes 26, the Winnu review: Reclaimer may follow - a second PDS)
        }
        // Reclaimer (Winnu mech): a PDS or a space dock on the planet it helped to take
        if (G.planets[x].units[p].count(UnitType::Mech) && mechAbilities(G)) {
            std::vector<Option> opts;
            if (unitsAvailable(G, p, UnitType::PDS) > 0 && (G.fixes < 26 || G.planets[x].units[p].count(UnitType::PDS) < 2))  // (LRR 85.5)
                opts.push_back({int(UnitType::PDS), "PDS", ""});
            if (unitsAvailable(G, p, UnitType::SpaceDock) > 0 && G.planets[x].units[p].count(UnitType::SpaceDock) == 0)
                opts.push_back({int(UnitType::SpaceDock), "Space dock", ""});
            if (opts.empty()) continue;
            opts.push_back({-1, "Nothing", ""});
            int t = fpick(p, Ab::Reclaimer, "Reclaimer: build on " + planetName(x) + "?", opts);
            if (t < 0) continue;
            addUnits(p, UnitType(t), 1, sys, x);
            say(p, pn(p) + " builds a " + unitTypeName(UnitType(t)) + " on " + planetName(x) + " (Reclaimer).", sys);
        }
    }
}

// ------------------------------------------------------------------ combat
// (moves k units of type t, the undamaged and the galvanized first - as retreatCargo)
static void moveGround(Force& from, Force& to, UnitType t, int k) {
    const int ti = int(t);
    k = std::min<int>(k, from.n[ti]);
    if (k <= 0) return;
    const int undamaged = from.n[ti] - from.dmg[ti];
    const int movedDamaged = std::max(0, k - undamaged);
    const int movedGalv = std::min<int>(k, from.galv[ti]);
    from.galv[ti] = uint8_t(from.galv[ti] - movedGalv);
    from.remove(t, k, false);
    to.add(t, k);
    to.dmg[ti] = uint8_t(to.dmg[ti] + movedDamaged);
    to.galv[ti] = uint8_t(to.galv[ti] + movedGalv);
}

int Rules::alastorMask(int p) const {
    return G.fixes >= 27 && p >= 0 && p < kSides && alastor_[p] ? (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech)) : 0;
}

bool Rules::shipsOrAlastor(int p, int sys) const {
    return hasShips(G, p, sys) || (alastorMask(p) && G.systems[size_t(sys)].space[size_t(p)].groundForces() > 0);
}

int Rules::alastorFromPlanets(int p, int ti) const {
    int n = 0;
    for (const AlastorFrom& a : alastorFrom_)
        if (a.player == p) n += a.n[ti];
    return n;
}

// The Alastor back home: the space area's ground forces that waited aside return; the survivors from the planets go back to
// them - the player chooses which of the survivors those were (the losses were theirs to assign), as many as possible by default
void Rules::alastorReturn(int p) {
    if (alastorSys_ < 0 || p < 0 || p >= kSides) return;
    const int sys = alastorSys_;
    Force& sp = G.systems[size_t(sys)].space[size_t(p)];
    for (int ti = 0; ti < 2; ++ti) {
        const UnitType t = ti ? UnitType::Mech : UnitType::Infantry;
        int rest = alastorFromPlanets(p, ti);
        for (const AlastorFrom& a : alastorFrom_) {
            if (a.player != p || !a.n[ti]) continue;
            rest -= a.n[ti];
            const int surv = sp.count(t);
            // of the survivors, at least those beyond the space area's own came from the planets
            const int need = std::max(0, surv - int(alastorOwn_[p][ti]) - rest);
            const int most = std::min<int>(a.n[ti], surv);
            int k = most;
            if (need < most) {
                std::vector<Option> opts;
                for (int j = most; j >= need; --j) opts.push_back({j, std::to_string(j), j == most ? "as many as can" : ""});
                const int cv = curValue_;
                curValue_ = a.planet;
                k = fpick(p, Ab::Alastor,
                          std::string("The Alastor: how many of the surviving ") + (ti ? "mechs" : "infantry") + " go back to " + planetName(a.planet) + "?",
                          opts, "The others stay in the space area (they need room there).");
                curValue_ = cv;
                k = std::max(need, std::min(most, k));
            }
            if (k > 0) moveGround(sp, G.planets[size_t(a.planet)].units[size_t(p)], t, k);
        }
        alastorOwn_[p][ti] = 0;
    }
    for (int t = 0; t < kUnitTypes; ++t) moveGround(alastorAside_[p], sp, UnitType(t), alastorAside_[p].n[t]);
    alastorAside_[p] = Force();
    alastorFrom_.erase(std::remove_if(alastorFrom_.begin(), alastorFrom_.end(), [p](const AlastorFrom& a) { return a.player == p; }),
                       alastorFrom_.end());
}

// (fixes 27, the Nekro review - owner 2026-11-14) "At the start of a space combat, choose any number of your ground forces in
// this system to participate in that combat as if they were ships" - the space area's and the planets' ("the ground forces on
// the planet don't move to the space area ... they don't trigger fleet capacity or ship capacity")
void Rules::alastorChoice27(int p, int sys) {
    SystemState& s = G.systems[size_t(sys)];
    Force& sp = s.space[size_t(p)];
    alastor_[p] = false;
    if (!flagshipAbility(G, p, "nekro") || !flagshipIn(G, p, sys)) return;
    bool any = sp.groundForces() > 0;
    for (int16_t x : s.planets) any = any || G.planets[size_t(x)].units[size_t(p)].groundForces() > 0;
    if (!any) return;
    alastorSys_ = sys;
    const int cv = curValue_;
    auto howMany = [&](UnitType t, int have, int planet) {
        if (have <= 0) return 0;
        std::vector<Option> opts;
        for (int k = have; k >= 0; --k) opts.push_back({k, k == 0 ? std::string("None") : k == have && have > 1 ? "All " + std::to_string(k) : std::to_string(k), ""});
        curValue_ = planet;  // (for the AI: -1 the space area)
        const std::string what = t == UnitType::Mech ? "mechs" : "infantry";
        const int k = fpick(p, Ab::Alastor,
                            "The Alastor: how many " + what + (planet < 0 ? std::string(" in the space area") : " on " + planetName(planet)) +
                                " fight in this space combat as ships?",
                            opts,
                            planet < 0 ? "They roll with their own combat values and can be hit."
                                       : "They fight from the planet - no capacity needed - and stay there after the combat (or a retreat).");
        curValue_ = cv;
        return std::max(0, std::min(have, k));
    };
    int joined = 0;
    for (int ti = 0; ti < 2; ++ti) {
        const UnitType t = ti ? UnitType::Mech : UnitType::Infantry;
        const int k = howMany(t, sp.count(t), -1);
        moveGround(sp, alastorAside_[p], t, sp.count(t) - k);
        alastorOwn_[p][ti] = uint8_t(k);
        joined += k;
    }
    for (int16_t x : s.planets) {
        Force& on = G.planets[size_t(x)].units[size_t(p)];
        AlastorFrom a;
        a.player = int8_t(p);
        a.planet = x;
        for (int ti = 0; ti < 2; ++ti) {
            const UnitType t = ti ? UnitType::Mech : UnitType::Infantry;
            a.n[ti] = uint8_t(howMany(t, on.count(t), x));
        }
        if (!a.n[0] && !a.n[1]) continue;
        for (int ti = 0; ti < 2; ++ti) moveGround(on, sp, ti ? UnitType::Mech : UnitType::Infantry, a.n[ti]);
        joined += a.n[0] + a.n[1];
        alastorFrom_.push_back(a);
    }
    if (joined <= 0) {
        alastorReturn(p);
        return;
    }
    alastor_[p] = true;
    say(p, pn(p) + "'s " + std::to_string(joined) + " ground force" + (joined > 1 ? "s join" : " joins") + " the space combat (The Alastor).", sys);
}

// The Alastor (Nekro flagship): ground forces in space fight as ships
void Rules::alastorChoice(int p, int sys) {
    if (G.fixes >= 27) {
        alastorChoice27(p, sys);
        return;
    }
    Force& mine = G.systems[sys].space[p];
    alastor_[p] = false;
    if (flagshipAbility(G, p, "nekro") && flagshipIn(G, p, sys) && mine.groundForces() > 0 &&
        fyes(p, Ab::Alastor, "The Alastor: your " + std::to_string(mine.groundForces()) +
                                 " ground forces in space fight in this combat as ships?",
             "They roll with their own combat values and can be hit.")) {
        alastor_[p] = true;
        say(p, pn(p) + "'s ground forces join the space combat (The Alastor).", sys);
    }
}
// Dimensional Splicer (Creuss): a hit at the start of combat in a system with a wormhole
// (0.44.8, fixes 12: only while the Creuss still have ships in the combat - an Assault Cannon may have taken them)
// (fixes 24, owner's faction debug pass) the opponent's units fighting as ships count as their ships: ground forces with
// The Alastor, Z-Grav Eidolon mechs - as for Assault Cannon
bool Rules::splicerReady(int p, int o, int sys) const {
    const Force& f = G.systems[size_t(sys)].space[size_t(o)];
    const bool asShips = G.fixes >= 24 && o >= 0 && o < kSides &&
                         ((alastor_[o] && f.groundForces() > 0) || ((alastor_[o] || zgrav_[o]) && f.count(UnitType::Mech) > 0));
    return owns(p, "ds") && wormholesOf(G, sys) && (hasShips(G, o, sys) || asShips) && (G.fixes < 12 || hasShips(G, p, sys));
}
void Rules::dimensionalSplicer(int p, int o, int sys, bool beforeCannon) {
    std::vector<Option> opts;
    for (int t : kShipIdx)
        if (G.systems[sys].space[o].n[t]) opts.push_back({t, std::string(pn(o)) + "'s " + unitTypeName(UnitType(t)), ""});
    if (G.fixes >= 24 && o >= 0 && o < kSides) {
        const Force& f = G.systems[size_t(sys)].space[size_t(o)];
        if (alastor_[o] && f.count(UnitType::Infantry))
            opts.push_back({int(UnitType::Infantry), std::string(pn(o)) + "'s Infantry", "fighting as a ship (The Alastor)"});
        if ((alastor_[o] || zgrav_[o]) && f.count(UnitType::Mech))
            opts.push_back({int(UnitType::Mech), std::string(pn(o)) + "'s Mech", "fighting as a ship"});
    }
    opts.push_back({-1, "Don't use it", ""});
    const int cf = curFor_, cv = curValue_;
    curFor_ = o;  // (for the AI: whose ships, in which system)
    curValue_ = sys;
    int t = fpick(p, Ab::DimensionalSplicer, "Dimensional Splicer: which of " + pn(o) + "'s ships takes 1 hit?", opts,
                  beforeCannon ? "Your Assault Cannon fires next: " + pn(o) + " will then destroy one of their non-fighter ships." : "");
    curFor_ = cf;
    curValue_ = cv;
    if (t >= 0) {
        say(p, pn(p) + " uses the Dimensional Splicer.", sys);
        assignHits(o, sys, -1, 1, 1 << t, "Dimensional Splicer", p);
    }
}

// (fixes 27, the Nekro review - owner 2026-11-14) the start of a space combat, before anything is destroyed: the two sides for
// Technological Singularity, then Antivirus
void Rules::nekroCombatStart(int att, int def, int sys) {
    singularityUsed_ = false;
    singularitySys_ = sys;
    combatSide_[0] = att;
    combatSide_[1] = def;
    const int n = playerOfFaction(G, "nekro");
    for (int p : {att, def})
        if (n >= 0 && p != n && (att == n || def == n) && holdsNote(G, p, n, kNoteFaction) &&
            fyes(p, Ab::Antivirus, "Play Antivirus?", factionNoteText(G.players[n].faction, kNoteFaction))) {
            auto& h = G.players[p].notes;
            h.erase(std::find(h.begin(), h.end(), int16_t(makeNote(n, kNoteFaction))));
            G.players[p].notesInPlay.push_back(int16_t(makeNote(n, kNoteFaction)));
            abilityEvent3(E, p, "Antivirus", pn(p) + " plays Antivirus: no Technological Singularity against them.", sys);
        }
}

void Rules::spaceCombatStart3(int att, int def, int sys) {
    if (G.fixes >= 27) return;  // (nekroCombatStart, at the start of spaceCombat; The Alastor before Assault Cannon - fixes 13)
    singularityUsed_ = false;
    combatSide_[0] = att;
    combatSide_[1] = def;
    // (0.44.9, fixes 13: The Alastor and Dimensional Splicer are resolved before Assault Cannon - spaceCombat)
    if (G.fixes < 13)
        for (int p : {att, def}) {
            if (p >= G.numPlayers) continue;  // (Thunder's Edge: neutral units have no abilities)
            int o = p == att ? def : att;
            alastorChoice(p, sys);
            if (splicerReady(p, o, sys)) dimensionalSplicer(p, o, sys);
        }
    // Antivirus (Nekro's note)
    int n = playerOfFaction(G, "nekro");
    for (int p : {att, def})
        if (n >= 0 && p != n && (att == n || def == n) && holdsNote(G, p, n, kNoteFaction) &&
            fyes(p, Ab::Antivirus, "Play Antivirus?", factionNoteText(G.players[n].faction, kNoteFaction))) {
            auto& h = G.players[p].notes;
            h.erase(std::find(h.begin(), h.end(), int16_t(makeNote(n, kNoteFaction))));
            G.players[p].notesInPlay.push_back(int16_t(makeNote(n, kNoteFaction)));
            abilityEvent3(E, p, "Antivirus", pn(p) + " plays Antivirus: no Technological Singularity against them.", sys);
        }
}

void Rules::groundCombatStart3(int att, int def, int planet) {
    singularityUsed_ = false;
    combatSide_[0] = att;
    combatSide_[1] = def;
    int n = playerOfFaction(G, "nekro");
    for (int p : {att, def})
        if (n >= 0 && p != n && (att == n || def == n) && holdsNote(G, p, n, kNoteFaction) &&
            fyes(p, Ab::Antivirus, "Play Antivirus?", factionNoteText(G.players[n].faction, kNoteFaction))) {
            auto& h = G.players[p].notes;
            h.erase(std::find(h.begin(), h.end(), int16_t(makeNote(n, kNoteFaction))));
            G.players[p].notesInPlay.push_back(int16_t(makeNote(n, kNoteFaction)));
            abilityEvent3(E, p, "Antivirus", pn(p) + " plays Antivirus.", G.planets[planet].system);
        }
}

// Technological Singularity (Nekro): once per combat, after an opponent's unit is destroyed.
void Rules::singularity(int victim, int sys, int planet) {
    (void)planet;
    if (singularityUsed_ || combatSide_[0] < 0) return;
    int n = combatSide_[0] == victim ? combatSide_[1] : combatSide_[1] == victim ? combatSide_[0] : -1;
    if (n < 0 || !fac(n, "nekro") || G.players[n].eliminated || victim >= G.numPlayers) return;  // (not neutral units' tech)
    for (int16_t note : G.players[victim].notesInPlay)
        if (noteOwner(note) == n && noteType(note) == kNoteFaction) return;  // Antivirus
    singularityUsed_ = true;  // asked once per combat
    if (gainTechFrom(n, {victim}, "Technological Singularity")) {
        G.players[n].usedFactionAbility = true;
        say(n, pn(n) + " assimilates " + pn(victim) + "'s technology (Technological Singularity).", sys);
    }
}

void Rules::dynamo(int owner, int sys, int planet, int t, int unitOwner) {
    // Dynamo (Empyrean flagship): repair a unit that used Sustain Damage in or next to its system
    // (fixes 26, the Empyrean review - owner 2026-11-12: "After ANY player's unit ... uses SUSTAIN DAMAGE" - unitOwner)
    const int u = unitOwner < 0 ? owner : unitOwner;
    if (owner < 0 || owner >= G.numPlayers || G.players[size_t(owner)].eliminated) return;
    if (!(fac(owner, "empyrean") || (G.fixes >= 13 && flagshipAbility(G, owner, "empyrean"))) || spendable(G, owner, true) < 2) return;
    bool near = flagshipIn(G, owner, sys);
    for (int16_t a : G.systems[sys].adj)
        if (flagshipIn(G, owner, a)) near = true;
    if (!near) return;
    Force& f = planet >= 0 ? G.planets[planet].units[u] : G.systems[sys].space[u];
    if (!f.dmg[t]) return;
    const bool yes = fyes(owner, Ab::Dynamo,
                          u == owner ? std::string("Dynamo: spend 2 influence to repair your ") + unitTypeName(UnitType(t)) + "?"
                                     : "Dynamo: spend 2 influence to repair " + pn(u) + "'s " + unitTypeName(UnitType(t)) + "?",
                          "");
    if (!yes) return;
    if (!pay(owner, 2, true, "Dynamo", false)) return;
    f.dmg[t] = uint8_t(f.dmg[t] - 1);
    say(owner, u == owner ? pn(owner) + " repairs a " + unitTypeName(UnitType(t)) + " (Dynamo)."
                          : pn(owner) + " repairs " + pn(u) + "'s " + unitTypeName(UnitType(t)) + " (Dynamo).", sys);
}

void Rules::combatMods3(int p, int opp, int sys, int planet, bool space, RollMods& m) {
    (void)planet;
    // Rickar Rickani (Winnu commander): +2 at Mecatol Rex, at home and next to legendary planets
    if (commanderUsable(G, p, "winnu") && (sys == G.mecatol() || sys == G.players[p].homeSystem || hasLegendary(G, sys))) m.all(2);
    // Salai Sai Corian (Winnu flagship): one die per non-fighter ship of the opponent
    if (space && flagshipAbility(G, p, "winnu") && opp >= 0 && flagshipIn(G, p, sys))
        m.extraDice[int(UnitType::Flagship)] += G.systems[sys].space[opp].nonFighterShips() - 1;
    // Mordred (Nekro mech): +2 against an opponent whose technology carries a Valefar token
    // (fixes 27, the Nekro review - owner 2026-11-14: "During combat" - a space combat too, where The Alastor puts it in)
    if ((!space || G.fixes >= 27) && fac(p, "nekro") && opp >= 0 && opp < G.numPlayers && mechAbilities(G)) {
        const Player& n = G.players[p];
        if ((n.assimX >= 0 && G.players[opp].hasTech(n.assimX)) || (n.assimY >= 0 && G.players[opp].hasTech(n.assimY)))
            m.perType[int(UnitType::Mech)] += 2;
    }
}

bool Rules::watcher(int player, int card) {
    // Watcher (Empyrean mech): remove it to cancel an action card played next to it
    int e = playerOfFaction(G, "empyrean");
    if (e < 0 || e == player || G.players[e].eliminated || !mechAbilities(G)) return false;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const SystemState& sy = G.systems[s];
        int mechPlanet = -2;
        if (sy.space[e].count(UnitType::Mech)) mechPlanet = -1;
        for (int16_t x : sy.planets)
            if (G.planets[x].units[e].count(UnitType::Mech)) mechPlanet = x;
        if (mechPlanet == -2) continue;
        bool near = hasUnits(G, player, int(s));
        for (int16_t a : sy.adj)
            if (hasUnits(G, player, a)) near = true;
        if (!near) continue;
        const int prev = curCard_, prevFor = curFor_;
        curCard_ = card;
        curFor_ = player;
        bool use = fyes(e, Ab::Watcher, std::string("Remove your Watcher mech to cancel ") + kActionCards[card].name + "?",
                        std::string(kActionCards[card].name) + ": " + actionCardText(card));
        curCard_ = prev;
        curFor_ = prevFor;
        if (!use) return false;
        removing_ = true;
        destroyUnits(e, UnitType::Mech, 1, int(s), mechPlanet, -1, "Watcher");
        removing_ = false;
        abilityEvent3(E, e, "Watcher", pn(e) + "'s Watcher cancels " + kActionCards[card].name + ".", int(s));
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ production
int Rules::productionDiscount3(int p, int sys) {
    int discount = 0;
    // (fixes 26, the Winnu review - the owner's Harrugh ruling: "When 1 or more of ... units use PRODUCTION" - not for
    // productions by other abilities, as Sarween Tools / AI Development Algorithm since 0.44.5)
    if (G.fixes >= 26 && notProductionAbility_) return 0;
    // Berekar Berekon (Winnu agent)
    const int w = agentUser(p, "winnu");  // or a player who promised its agent in a deal
    if (w >= 0) {
        agentFor(w, p);
        bool use = fyes(w, Ab::WinnuAgent, w == p ? std::string("Exhaust Berekar Berekon: the units you produce now cost 2 less?")
                                                  : "Exhaust Berekar Berekon for " + pn(p) + ": their production costs 2 less?", "");
        curFor_ = -1;
        if (use) {
            spendAgent(w, "winnu");
            discount += 2;
            say(p, pn(p) + "'s production costs 2 less (Berekar Berekon).", sys);
            agentDone(w, p);
        }
    }
    // Hegemonic Trade Policy (Winnu): swap a planet's resources and influence for this production
    if (techReady(p, "htp")) {
        std::vector<Option> opts;
        // (0.44.5, fixes 9: also an exhausted planet of ours in this system with a space dock - its Production rises)
        std::vector<int> cands = G.fixes >= 9 ? controlledPlanets(G, p) : ti::readyPlanets(G, p);
        for (int x : cands) {
            if (planetInfluence(G, x) <= planetResources(G, x)) continue;
            const bool dock = G.planets[size_t(x)].system == sys && G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock) > 0;
            if (G.planets[size_t(x)].exhausted && !dock) continue;
            opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + " resources -> " + std::to_string(planetInfluence(G, x)) +
                                                  (dock && G.fixes >= 9 ? " (its space dock then produces by the new resource value)" : "")});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Don't use it", ""});
            int x = fpick(p, Ab::HegemonicTrade, "Hegemonic Trade Policy: swap a planet's resources and influence?", opts,
                          "Only for this production.", false, true);
            if (x >= 0) {
                exhaustTech(p, "htp");
                G.fx.swapPlanet = int16_t(x);
                const bool dock = G.planets[size_t(x)].system == sys && G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock) > 0;
                // (0.44.9, fixes 13 - the owner's answer C7: the player is told what it does to the dock's Production)
                say(p, planetName(x) + "'s resources and influence are swapped (Hegemonic Trade Policy)" +
                           (G.fixes >= 13 && dock ? std::string(": its space dock produces by the new resource value (") +
                                                        std::to_string(planetResources(G, x)) + ") this time."
                                              : std::string(".")),
                    sys);
            }
        }
    }
    return discount;
}

// (fixes 27, the Saar review - owner 2026-11-13) Captain Mendosa: "When a player activates a system: You may exhaust this card to
// increase the move value of 1 of that player's ships to match the move value of the ship on the game board that has the
// highest move value" - any player's activation; the Saar decide: a person is asked (mendosaQuiet_), a bot when it promised
void Rules::mendosa27(int p, int sys) {
    if (p < 0 || p >= G.numPlayers) return;
    const int top = maxShipMove(G);
    bool helps = false, faster = false;  // a ship of the active player gets faster / reaches the system only with it
    for (size_t f = 0; f < G.systems.size() && !helps; ++f) {
        if (int(f) == sys || G.systems[f].hasToken(p)) continue;
        for (int t : kShipIdx) {
            if (!G.systems[f].space[p].n[t] || UnitType(t) == UnitType::Fighter) continue;
            const int mv = moveValue(G, p, int(f), UnitType(t));
            if (mv <= 0 || mv >= top) continue;
            faster = true;
            if (!shipReach(G, p, int(f), sys, UnitType(t), owns(p, "gd") ? 1 : 0).ok &&
                shipReach(G, p, int(f), sys, UnitType(t), top - mv + (owns(p, "gd") ? 1 : 0)).ok)
                helps = true;
        }
    }
    if (!faster) return;
    const std::string text = leaderText(findLeader("saaragent"));
    if (canUseAgent(p, "saar")) {
        if ((helps || G.players[size_t(p)].human) && fyes(p, Ab::SaarAgent, "Exhaust Captain Mendosa?",
                                                          "1 of your ships gets move " + std::to_string(top) + " (the fastest ship on the board) this movement.")) {
            spendAgent(p, "saar");
            G.fx.mendosa = true;
            say(p, pn(p) + " calls on Captain Mendosa: one ship moves " + std::to_string(top) + ".", sys);
        }
        return;
    }
    int user = -1;
    for (int o : initiativeOrder(G)) {
        if (o == p || !canUseAgent(o, "saar")) continue;
        const bool promised = G.agentFor[o] == p;
        const bool human = G.players[size_t(o)].human;
        if (!(promised && helps) && !(human && mendosaQuiet_[o] != G.round)) continue;
        agentFor(o, p);
        std::vector<Option> yn = {{1, "Yes", ""}, {0, "No", ""}};
        if (human && !promised) yn.push_back({3, "No, and don't ask again this round", "You'll be asked again next round."});
        const int c = fpick(o, Ab::SaarAgent, "Exhaust Captain Mendosa: 1 of " + pn(p) + "'s ships gets move " + std::to_string(top) + "?", yn,
                            pn(p) + " activated " + sysName(sys) + ". " + text);
        curFor_ = -1;
        if (c == 3) mendosaQuiet_[o] = int16_t(G.round);
        if (c != 1) continue;
        user = o;
        break;
    }
    if (user < 0) return;
    spendAgent(user, "saar");
    agentDone(user, p);
    G.fx.mendosa = true;
    say(p, pn(user) + " calls on Captain Mendosa for " + pn(p) + ": one ship moves " + std::to_string(top) + ".", sys);
}

// (fixes 27, the Saar review - owner 2026-11-13) Rowl Sarrig: "When you produce fighters or infantry: You may place each of
// those units at any of your space docks that are not blockaded" - dock by dock, how many of each go there (the bots send
// them all to one dock, as before)
void Rules::rowlSarrig27(int p, int sys, int fighters, int inf) {
    std::vector<Option> docks;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (int(s) == sys || otherShipsIn(G, p, int(s))) continue;
        if (G.systems[s].space[p].count(UnitType::SpaceDock)) docks.push_back({int(s) * 1000 + 999, sysName(int(s)), "Floating factory"});
        for (int16_t x : G.systems[s].planets)
            if (G.planets[x].units[p].count(UnitType::SpaceDock)) docks.push_back({int(s) * 1000 + x, planetName(x), "Space dock"});
    }
    const std::string text = leaderText(findLeader("saarcommander"));
    SystemState& from = G.systems[size_t(sys)];
    bool first = true;
    while ((fighters > 0 || inf > 0) && !docks.empty()) {
        // what is still here of the new units (an ability may have moved some since)
        int haveInf = from.space[p].count(UnitType::Infantry);
        for (int16_t y : from.planets) haveInf += G.planets[y].units[p].count(UnitType::Infantry);
        fighters = std::min(fighters, int(from.space[p].count(UnitType::Fighter)));
        inf = std::min(inf, haveInf);
        if (fighters <= 0 && inf <= 0) break;
        std::vector<Option> opts = docks;
        opts.push_back({-1, first ? "Keep them here" : "Keep the rest here", ""});
        std::string what = (fighters ? std::to_string(fighters) + (fighters == 1 ? " fighter" : " fighters") : std::string()) +
                           (fighters && inf ? " and " : "") + (inf ? std::to_string(inf) + " infantry" : std::string());
        const int c = fpick(p, Ab::SaarCommander,
                            std::string(first ? "Rowl Sarrig: place the new fighters and infantry at another space dock?"
                                              : "Rowl Sarrig: place the rest at another space dock?"),
                            opts, what + " new in " + sysName(sys) + ". " + text);
        first = false;
        if (c < 0) break;
        for (size_t i = 0; i < docks.size(); ++i)
            if (docks[i].id == c) {
                docks.erase(docks.begin() + long(i));
                break;
            }
        const int s2 = c / 1000, x = c % 1000 == 999 ? -1 : c % 1000;
        const std::string where = x >= 0 ? planetName(x) : sysName(s2);
        auto howMany = [&](int most, const char* kind) {
            if (most <= 0) return 0;
            if (fighters + inf == 1) return most;  // a single new unit: it goes
            std::vector<Option> n;
            for (int k = most; k >= 0; --k) n.push_back({k, std::to_string(k), ""});
            return fpick(p, Ab::SaarCommanderCount, std::string("Rowl Sarrig: how many of the new ") + kind + " go to " + where + "?", n,
                         text);
        };
        const int fk = howMany(fighters, "fighters");
        const int ik = howMany(inf, "infantry");
        if (fk + ik <= 0) continue;
        if (fk) {
            from.space[p].remove(UnitType::Fighter, fk, false);
            G.systems[size_t(s2)].space[p].add(UnitType::Fighter, fk);
        }
        int left = ik;  // infantry from wherever they were placed: the planets first, then the space area
        for (int i = int(from.planets.size()) - 1; i >= -1 && left > 0; --i) {
            Force& src = i >= 0 ? G.planets[size_t(from.planets[size_t(i)])].units[p] : from.space[p];
            const int k = std::min(left, int(src.count(UnitType::Infantry)));
            if (!k) continue;
            src.remove(UnitType::Infantry, k, false);
            left -= k;
        }
        const int moved = ik - left;
        if (moved) {
            if (x >= 0) G.planets[size_t(x)].units[p].add(UnitType::Infantry, moved);
            else G.systems[size_t(s2)].space[p].add(UnitType::Infantry, moved);
        }
        fighters -= fk;
        inf -= moved;
        say(p, pn(p) + " sends " + std::to_string(fk + moved) + " new " + (fk + moved == 1 ? "unit" : "units") + " to " + where + " (Rowl Sarrig).", s2);
        removeExcessCapacity(p, s2);
    }
}

void Rules::afterProduction3(int p, int sys, const Force& made) {
    G.fx.swapPlanet = -1;
    if (made.count(UnitType::WarSun)) G.players[p].producedWarSun = true;  // Magmus unlock
    // Rowl Sarrig (Saar commander): new fighters / infantry may go to another space dock
    int fighters = made.count(UnitType::Fighter), inf = made.count(UnitType::Infantry);
    if ((fighters || inf) && commanderUsable(G, p, "saar") && G.fixes >= 27) {
        rowlSarrig27(p, sys, fighters, inf);
    } else if ((fighters || inf) && commanderUsable(G, p, "saar")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (int(s) == sys || otherShipsIn(G, p, int(s))) continue;
            if (G.systems[s].space[p].count(UnitType::SpaceDock)) opts.push_back({int(s) * 1000 + 999, sysName(int(s)), "Floating factory"});
            for (int16_t x : G.systems[s].planets)
                if (G.planets[x].units[p].count(UnitType::SpaceDock)) opts.push_back({int(s) * 1000 + x, planetName(x), "Space dock"});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Keep them here", ""});
            int c = fpick(p, Ab::SaarCommander, "Rowl Sarrig: place the new fighters and infantry at another space dock?", opts);
            if (c >= 0) {
                int s2 = c / 1000, x = c % 1000 == 999 ? -1 : c % 1000;
                SystemState& from = G.systems[sys];
                // fighters from the space area, infantry from wherever they were placed
                int fk = std::min(fighters, int(from.space[p].count(UnitType::Fighter)));
                if (fk) {
                    from.space[p].remove(UnitType::Fighter, fk, false);
                    G.systems[s2].space[p].add(UnitType::Fighter, fk);
                }
                int left = inf;
                Force* srcs[4] = {&from.space[p], nullptr, nullptr, nullptr};
                int ns = 1;
                for (int16_t y : from.planets)
                    if (ns < 4) srcs[ns++] = &G.planets[y].units[p];
                for (int i = ns - 1; i >= 0 && left > 0; --i) {
                    int k = std::min(left, int(srcs[i]->count(UnitType::Infantry)));
                    if (!k) continue;
                    srcs[i]->remove(UnitType::Infantry, k, false);
                    left -= k;
                }
                int moved = inf - left;
                if (moved) {
                    if (x >= 0) G.planets[x].units[p].add(UnitType::Infantry, moved);
                    else G.systems[s2].space[p].add(UnitType::Infantry, moved);
                }
                say(p, pn(p) + " sends the new units to " + sysName(s2) + " (Rowl Sarrig).", s2);
                removeExcessCapacity(p, s2);
            }
        }
    }
}

// ------------------------------------------------------------------ invasion
bool Rules::raghsCall(int lander, int planet) {
    // Ragh's Call (Saar note): Saar's ground forces on the planet leave before the combat
    int s = playerOfFaction(G, "saar");
    if (s < 0 || s == lander || !holdsNote(G, lander, s, kNoteFaction)) return false;
    Force& f = G.planets[planet].units[s];
    if (f.groundForces() == 0) return false;
    if (!fyes(lander, Ab::RaghsCall, "Play Ragh's Call: move " + pn(s) + "'s ground forces away from " + planetName(planet) + "?",
              factionNoteText(G.players[s].faction, kNoteFaction)))
        return false;
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, s))
        if (x != planet && !isDMZ(G, x)) opts.push_back({x, planetName(x), ""});
    int to = opts.empty() ? -1 : opts.size() == 1 ? opts[0].id : fpick(s, Ab::RaghsCall, "Ragh's Call: move your ground forces to which planet?", opts, "", false, true);
    for (UnitType t : {UnitType::Infantry, UnitType::Mech}) {
        int k = f.count(t);
        if (!k) continue;
        int dk = f.dmg[int(t)];
        f.n[int(t)] = 0;
        f.dmg[int(t)] = 0;
        if (to >= 0) {
            G.planets[to].units[s].add(t, k);
            G.planets[to].units[s].dmg[int(t)] = uint8_t(G.planets[to].units[s].dmg[int(t)] + dk);
        }
    }
    returnNote(lander, makeNote(s, kNoteFaction));
    abilityEvent3(E, lander, "Ragh's Call", pn(lander) + " plays Ragh's Call: " + pn(s) + "'s ground forces leave " + planetName(planet) + ".",
                  G.planets[planet].system);
    return true;
}

// ------------------------------------------------------------------ technology (Nekro)
bool Rules::propagation(int p, int resourceCost) {
    // Propagation (Nekro): no research - 3 command tokens instead
    if (resourceCost > 0 && !pay(p, resourceCost, false, "Propagation (instead of researching)", true)) return false;
    G.players[p].usedFactionAbility = true;
    abilityEvent3(E, p, "Propagation", pn(p) + " gains 3 command tokens instead of researching (Propagation).");
    gainTokens(p, 3, false, "Propagation");
    return true;
}

bool Rules::gainTechFrom(int p, const std::vector<int>& from, const std::string& why) {
    Player& pl = G.players[p];
    std::vector<Option> opts;
    auto listed = [&](int id) {
        for (const Option& o : opts)
            if (o.id == id) return true;
        return false;
    };
    const bool vx = pl.hasTech(findTech("vax")), vy = pl.hasTech(findTech("vay"));
    for (int q : from) {
        if (q < 0 || q == p) continue;
        for (int16_t t : G.players[q].techs) {
            if (kTechs[t].faction < 0) {
                if (!pl.hasTech(t) && !listed(t)) opts.push_back({t, kTechs[t].name, "from " + pn(q) + ". " + techSummary(t)});
                continue;
            }
            // another faction's technology: only through a Valefar Assimilator token
            if (t == pl.assimX || t == pl.assimY) continue;
            if (q < G.numPlayers && commanderUsable(G, q, "bastion")) continue;  // Nip and Tuck: no tokens on their cards
            bool taken = false;  // another Nekro token already on it
            for (int r = 0; r < G.numPlayers; ++r)
                if (r != p && (G.players[r].assimX == t || G.players[r].assimY == t)) taken = true;
            if (taken) continue;
            if (vx && !listed(10000 + t)) opts.push_back({10000 + t, std::string("Valefar X on ") + kTechs[t].name, "from " + pn(q) + ". " + techSummary(t)});
            if (vy && !listed(20000 + t)) opts.push_back({20000 + t, std::string("Valefar Y on ") + kTechs[t].name, "from " + pn(q) + ". " + techSummary(t)});
        }
    }
    // Valefar Assimilator Z (Nekro breakthrough, Thunder's Edge): a Z token on the faction instead - the Nekro
    // flagship gains that faction's flagship abilities (3 tokens)
    if (breakthroughIs(G, p, "nekro") && pl.nekroZ.size() < 3)
        for (int q : from) {
            if (q < 0 || q == p || q >= G.numPlayers) continue;
            const int f = G.players[size_t(q)].faction;
            if (std::find(pl.nekroZ.begin(), pl.nekroZ.end(), int8_t(f)) != pl.nekroZ.end() || listed(30000 + f)) continue;
            if (commanderUsable(G, q, "bastion")) continue;  // Nip and Tuck: no tokens on their cards
            opts.push_back({30000 + f, std::string("Valefar Z on the ") + kFactions[f].name,
                            "Your flagship gains their flagship's abilities (instead of a technology)."});
        }
    if (opts.empty()) return false;
    opts.push_back({-1, "Nothing", ""});
    int c = fpick(p, Ab::TechSingularity, why + ": gain which technology?", opts);
    if (c < 0) return false;
    if (c >= 30000) {
        pl.nekroZ.push_back(int8_t(c - 30000));
        say(p, pn(p) + "'s flagship gains the abilities of the " + kFactions[c - 30000].name + " flagship (Valefar Assimilator Z).");
        return true;
    }
    if (c >= 10000) {
        int t = c % 10000;
        (c >= 20000 ? pl.assimY : pl.assimX) = int16_t(t);
        refreshUnits(p);
        say(p, pn(p) + " places Valefar Assimilator " + (c >= 20000 ? "Y" : "X") + " on " + kTechs[t].name + ".");
        // (fixes 27, the Nekro review - owner 2026-11-14) Nekro Acidos: "After you gain a technology" - a Valefar token on
        // another player's technology isn't one (the card as written)
        if (G.fixes < 27 && commanderUsable(G, p, "nekro")) drawActionCards(p, 1, "Nekro Acidos");
        return true;
    }
    researchTech(p, c, false);
    E.log(p, pn(p) + " gains " + kTechs[c].name + ".", EventKind::Research);
    return true;
}

// (fixes 27, the Nekro review - owner 2026-11-14) Nekro Malleon: "During the action phase: You may exhaust this card to choose a
// player: that player may discard 1 action card or spend 1 command token from their command sheet to gain 2 trade goods"
bool Rules::nekroMalleon(int p) {
    std::vector<Option> who;
    for (int q = 0; q < G.numPlayers; ++q) {
        const Player& o = G.players[size_t(q)];
        if (o.eliminated || (o.actionCards.empty() && o.tactic + o.fleet + o.strategy <= 0)) continue;
        who.push_back({q, q == p ? "Yourself" : pn(q), std::to_string(o.actionCards.size()) + " action cards, " + std::to_string(o.tactic) + " / " +
                                                          std::to_string(o.fleet) + " / " + std::to_string(o.strategy) + " command tokens"});
    }
    if (who.empty()) return false;
    who.push_back({-1, "Cancel", ""});
    const int q = fpick(p, Ab::NekroAgent, "Nekro Malleon: choose a player", who,
                        "They may discard 1 action card or spend 1 command token from their command sheet to gain 2 trade goods.");
    if (q < 0) return false;
    spendAgent(p, "nekro");
    if (q != p) say(p, pn(p) + " exhausts Nekro Malleon: " + pn(q) + " is chosen.");
    Player& o = G.players[size_t(q)];
    std::vector<Option> opts;
    for (int16_t c : o.actionCards) opts.push_back({c, std::string("Discard ") + kActionCards[c].name, ""});
    if (o.tactic > 0) opts.push_back({1000, "Spend a tactic token", ""});
    if (o.fleet > 0) opts.push_back({1001, "Spend a fleet token", ""});
    if (o.strategy > 0) opts.push_back({1002, "Spend a strategy token", ""});
    opts.push_back({-1, "Neither", ""});
    const int c = fpick(q, Ab::NekroAgent, "Nekro Malleon: what do you give up for 2 trade goods?", opts,
                        q == p ? "" : pn(p) + " chose you.");
    if (c < 0) {
        if (q != p) say(q, pn(q) + " gives up nothing (Nekro Malleon).");
        return true;
    }
    if (c == 1000) o.tactic = int8_t(o.tactic - 1);
    else if (c == 1001) o.fleet = int8_t(o.fleet - 1);
    else if (c == 1002) spendStrategy(q, 1);
    else {
        o.actionCards.erase(std::find(o.actionCards.begin(), o.actionCards.end(), int16_t(c)));
        discardActionCard(q, c, false);
    }
    gainTG(q, 2);
    say(q, pn(q) + " gains 2 trade goods (Nekro Malleon).");
    if (c == 1001) enforceFleetLimit(q);
    return true;
}

// ------------------------------------------------------------------ agendas
void Rules::nekroPredict(int agenda) {
    nekroGuess_ = -1;
    int n = playerOfFaction(G, "nekro");
    if (n < 0 || G.players[n].eliminated || nekroRound_ == G.round) return;
    std::vector<Option> opts = agendaOutcomes(agenda);
    opts.push_back({kNoVote, "Don't predict", "You can predict once per agenda phase."});
    const int prev = curAgenda_;
    curAgenda_ = agenda;
    int o = fpick(n, Ab::GalacticThreat, std::string("Galactic Threat: predict the outcome of ") + kAgendas[agenda].name + "?", opts,
                  "If you're right, gain a technology of someone who voted for it.");
    curAgenda_ = prev;
    if (o == kNoVote) return;
    nekroGuess_ = o;
    nekroRound_ = int(G.round);
    say(n, pn(n) + " predicts: " + outcomeName(agenda, o) + " (Galactic Threat).");
}

void Rules::nekroResolve(int agenda, int outcome, const std::vector<int>& votedFor) {
    int n = playerOfFaction(G, "nekro");
    if (n < 0 || nekroGuess_ < 0 || nekroGuess_ == kNoVote) return;
    int guess = nekroGuess_;
    nekroGuess_ = -1;
    if (guess != outcome) {
        say(n, pn(n) + "'s prediction was wrong.");
        return;
    }
    std::vector<int> voters;
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != n && q < int(votedFor.size()) && votedFor[q] == outcome) voters.push_back(q);
    (void)agenda;
    if (voters.empty()) {  // 0.29.0: say why nothing happens (LRR 8.19a: the speaker's tie-break is not a vote)
        Event e;
        e.kind = EventKind::Note;
        e.player = int8_t(n);
        e.text = pn(n) + "'s prediction was right, but nobody voted that way - no technology to take (Galactic Threat).";
        E.emit(std::move(e));
        return;
    }
    if (gainTechFrom(n, voters, "Galactic Threat")) G.players[n].usedFactionAbility = true;
}

void Rules::bloodPact(std::vector<int>& votedFor, std::vector<int>& votes) {
    int e = playerOfFaction(G, "empyrean");
    if (e < 0 || votedFor[e] == kNoVote) return;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == e || votedFor[q] != votedFor[e]) continue;
        for (int16_t note : G.players[q].notesInPlay)
            if (noteOwner(note) == e && noteType(note) == kNoteFaction2) {
                votes[q] += 4;
                say(q, pn(q) + " casts 4 more votes (Blood Pact).");
            }
    }
}

// ------------------------------------------------------------------ component actions
std::vector<Option> Rules::factionActionOptions3(int p) {
    std::vector<Option> o;
    const Player& pl = G.players[p];
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    // (0.44.9, fixes 13 - the owner's answer P1: without the Codex / Thunder's Edge the original card - start of the
    // status phase, no action)
    if (techReady(p, "wg") && !(G.fixes >= 13 && !codexOn(G)))
        add(Ab::WormholeGenerator, "Wormhole Generator", "Place or move a Creuss wormhole token.");
    if (fac(p, "muaat") && pl.strategy > 0 && unitsOnBoard(G, p, UnitType::WarSun) > 0 &&
        (unitsAvailable(G, p, UnitType::Fighter) > 0 || unitsAvailable(G, p, UnitType::Destroyer) > 0))
        add(Ab::StarForge, "Star Forge", "Spend 1 strategy token: 2 fighters or 1 destroyer where you have a war sun.");
    if ((fac(p, "muaat") || (G.fixes >= 13 && flagshipAbility(G, p, "muaat"))) && pl.strategy > 0 && unitsOnBoard(G, p, UnitType::Flagship) > 0 &&
        unitsAvailable(G, p, UnitType::Cruiser) > 0)  // (0.44.9: a Valefar Z copy too)
        add(Ab::Inferno, "The Inferno", "Spend 1 strategy token: 1 cruiser in your flagship's system.");
    // (fixes 26, the Muaat review - owner 2026-11-12) Umbat: "choose a player" - any player with a war sun or flagship
    if (canUseAgent(p, "muaat") && (G.fixes >= 26 || spendable(G, p, false) > 0)) {
        bool any = false;
        for (const SystemState& s : G.systems)
            for (int q = 0; q < G.numPlayers; ++q)
                if ((q == p || G.fixes >= 26) && (s.space[q].count(UnitType::WarSun) || s.space[q].count(UnitType::Flagship))) any = true;
        if (any)
            add(Ab::MuaatAgent, "Agent: Umbat",
                G.fixes >= 26 ? "Choose a player: they may produce up to 2 units costing 4 or less where they have a war sun or their flagship."
                              : "Produce up to 2 units costing 4 or less where you have a war sun or flagship.");
    }
    for (int16_t n : pl.notes) {
        int owner = noteOwner(n);
        if (owner == p) continue;
        if (noteType(n) == kNoteFaction && isFaction(G, owner, "muaat") && !owns(p, kTechs[warSunUpgradeFor(G, p)].key))
            add(Ab::FiresOfGashlai, "Play Fires of the Gashlai", "Gain your war sun unit upgrade technology; Muaat loses a fleet token.");
        if (noteType(n) == kNoteFaction2 && isFaction(G, owner, "empyrean"))
            add(Ab::BloodPact, "Play Blood Pact", "Place it faceup: when you and the Empyrean player cast votes for the same outcome, cast 4 additional votes for that outcome.");
        if ((G.extras & 64) && noteType(n) == kNoteFaction && isFaction(G, owner, "empyrean"))  // 0.32.1: an action
            add(Ab::DarkPact, "Play Dark Pact",
                "Place it faceup: when you give a number of commodities to the Empyrean player equal to your maximum commodity value, "
                "you each gain 1 trade good.");
    }
    if (techReady(p, "lgf") && controlsMecatol(G, p) && unitsAvailable(G, p, UnitType::Infantry) > 0)
        add(Ab::LazaxGate, "Lazax Gate Folding", "Put 1 infantry on Mecatol Rex.");
    if (heroUnlocked(G, p)) {
        if (fac(p, "ghost")) add(Ab::CreussHero, "Hero: Riftwalker Meian", "Swap the positions of 2 systems with wormholes or your units. Purged afterwards.");
        if (fac(p, "saar")) {
            bool any = false;
            for (size_t s = 0; s < G.systems.size() && !any; ++s) {
                bool dock = G.systems[s].space[p].count(UnitType::SpaceDock) > 0;
                for (int16_t x : G.systems[s].planets)
                    if (G.planets[x].units[p].count(UnitType::SpaceDock)) dock = true;
                if (dock) any = true;
            }
            if (any) add(Ab::SaarHero, "Hero: Gurno Aggero", leaderText(findLeader("saarhero")));
        }
        if (fac(p, "winnu")) add(Ab::WinnuHero, "Hero: Mathis Mathinus", "Resolve the primary of any strategy card; others may use its secondary. Purged afterwards.");
        if (fac(p, "nekro")) {
            bool any = false;
            for (size_t x = 0; x < G.planets.size(); ++x)
                if (!G.planets[x].destroyed && G.planets[x].system >= 0 && planetHasSkip(G, int(x)) && hasUnits(G, p, G.planets[x].system))
                    any = true;
            if (any) add(Ab::NekroHero, "Hero: UNIT.DSGN.FLAYESH", "Raze a technology-specialty planet next to your units: trade goods and a technology. Purged afterwards.");
        }
        if (fac(p, "empyrean"))
            add(Ab::EmpyreanHero, "Hero: Conservator Procyon",
                "Place 1 frontier token in each system that does not contain any planets and does not already have one; explore each "
                "frontier token in a system with your ships. Then, purge this card.");
    }
    for (const Option& x : factionActionOptions4(p)) o.push_back(x);
    return o;
}

// Ember Colossus (Muaat mech): when Star Forge is used in this system or next to it, 1 infantry with the mech (0.44.8,
// fixes 12: for each mech that qualifies - "this unit" - each asked, a mech in space too; it used to be the first
// planet's only). Also after The Nucleus, which is a use of Star Forge.
void Rules::emberColossus(int p, int s) {
    if (unitsAvailable(G, p, UnitType::Infantry) <= 0 || !mechAbilities(G)) return;
    if (G.fixes >= 12 && !fac(p, "muaat")) return;  // (The Nucleus used by another faction: their mechs aren't Ember Colossi)
    std::vector<int> near = {s};
    for (int16_t a : G.systems[size_t(s)].adj) near.push_back(a);
    if (G.fixes < 12) {
        int mechPlanet = -1;
        for (int y : near)
            for (int16_t x : G.systems[y].planets)
                if (G.planets[x].units[p].count(UnitType::Mech) && mechPlanet < 0) mechPlanet = x;
        if (mechPlanet >= 0 && fyes(p, Ab::EmberColossus, "Ember Colossus: put 1 infantry with your mech on " + planetName(mechPlanet) + "?", "")) {
            addUnits(p, UnitType::Infantry, 1, G.planets[mechPlanet].system, mechPlanet);
            say(p, pn(p) + " puts 1 infantry on " + planetName(mechPlanet) + " (Ember Colossus).", G.planets[mechPlanet].system);
        }
        return;
    }
    for (int y : near) {
        for (int16_t x : std::vector<int16_t>(G.systems[size_t(y)].planets))
            for (int k = G.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech); k > 0 && unitsAvailable(G, p, UnitType::Infantry) > 0; --k) {
                if (!fyes(p, Ab::EmberColossus, "Ember Colossus: put 1 infantry with your mech on " + planetName(x) + "?", "")) break;
                addUnits(p, UnitType::Infantry, 1, y, x);
                say(p, pn(p) + " puts 1 infantry on " + planetName(x) + " (Ember Colossus).", y);
            }
        for (int k = G.systems[size_t(y)].space[size_t(p)].count(UnitType::Mech);
             k > 0 && unitsAvailable(G, p, UnitType::Infantry) > 0 && shipCapacity(G, p, y) > transportedInSpace(G, p, y); --k) {  // (room for it)
            if (!fyes(p, Ab::EmberColossus, "Ember Colossus: put 1 infantry with your mech in space in " + sysName(y) + "?", "")) break;
            addUnits(p, UnitType::Infantry, 1, y, -1);
            removeExcessCapacity(p, y);
            say(p, pn(p) + " puts 1 infantry in space with its mech in " + sysName(y) + " (Ember Colossus).", y);
        }
    }
}

bool Rules::factionAction3(int p, Ab ab) {
    Player& pl = G.players[p];
    switch (ab) {
        case Ab::WormholeGenerator:
            if (!moveCreussToken(p, Ab::WormholeGenerator, -1, true)) return false;
            exhaustTech(p, "wg");
            return true;
        case Ab::StarForge: {
            std::vector<Option> where;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (G.systems[s].space[p].count(UnitType::WarSun)) where.push_back({int(s), sysName(int(s)), ""});
            where.push_back({-1, "Cancel", ""});
            int s = where.size() == 2 ? where[0].id : fpick(p, Ab::StarForge, "Star Forge: in which system?", where, "", true);
            if (s < 0) return false;
            std::vector<Option> what;
            if (unitsAvailable(G, p, UnitType::Fighter) > 0) what.push_back({int(UnitType::Fighter), "2 fighters", ""});
            if (unitsAvailable(G, p, UnitType::Destroyer) > 0) what.push_back({int(UnitType::Destroyer), "1 destroyer", ""});
            what.push_back({-1, "Cancel", ""});
            int t = fpick(p, Ab::StarForge, "Star Forge: place what?", what, "Costs 1 strategy token.");
            if (t < 0) return false;
            spendStrategy(p, 1);
            addUnits(p, UnitType(t), t == int(UnitType::Fighter) ? 2 : 1, s, -1);
            pl.usedFactionAbility = true;
            abilityEvent3(E, p, "Star Forge", pn(p) + " forges " + (t == int(UnitType::Fighter) ? "2 fighters" : "a destroyer") + " in " + sysName(s) + " (Star Forge).", s);
            emberColossus(p, s);  // Ember Colossus (Muaat mech)
            enforceFleetIn(p, s);
            removeExcessCapacity(p, s);
            return true;
        }
        case Ab::Inferno: {
            int s = -1;
            for (size_t x = 0; x < G.systems.size(); ++x)
                if (G.systems[x].space[p].count(UnitType::Flagship)) s = int(x);
            if (s < 0) return false;
            spendStrategy(p, 1);
            addUnits(p, UnitType::Cruiser, 1, s, -1);
            abilityEvent3(E, p, "The Inferno", pn(p) + "'s Inferno forges a cruiser in " + sysName(s) + ".", s);
            enforceFleetIn(p, s);
            return true;
        }
        case Ab::MuaatAgent: {
            // (fixes 26, the Muaat review - owner 2026-11-12) "choose a player: that player may produce up to 2 units that
            // each have a cost of 4 or less in a system that contains one of their war suns or their flagship"
            auto systemsOf = [&](int q) {
                std::vector<Option> where;
                for (size_t s = 0; s < G.systems.size(); ++s)
                    if (G.systems[s].space[q].count(UnitType::WarSun) || G.systems[s].space[q].count(UnitType::Flagship))
                        where.push_back({int(s), sysName(int(s)), ""});
                return where;
            };
            int q = p;
            if (G.fixes >= 26) {
                std::vector<Option> who;
                for (int r = 0; r < G.numPlayers; ++r)
                    if (!G.players[size_t(r)].eliminated && !systemsOf(r).empty()) who.push_back({r, r == p ? pn(r) + " (you)" : pn(r), ""});
                if (who.empty()) return false;
                who.push_back({-1, "Cancel", ""});
                q = fpick(p, Ab::MuaatAgent, "Umbat: which player produces?", who,
                          "That player may produce up to 2 units that each have a cost of 4 or less in a system that contains one of "
                          "their war suns or their flagship.");
                if (q < 0) return false;
            }
            std::vector<Option> where = systemsOf(q);
            int s = -1;
            if (q == p) {
                where.push_back({-1, "Cancel", ""});
                s = fpick(p, Ab::MuaatAgent, "Umbat: produce in which system?", where, "Up to 2 units that cost 4 or less.", true);
                if (s < 0) return false;
                spendAgent(p, "muaat");
                say(p, pn(p) + " calls on Umbat.", s);
            } else {
                spendAgent(p, "muaat");
                say(p, pn(p) + " calls on Umbat for " + pn(q) + ".");
                where.push_back({-1, "Produce nothing", ""});
                s = fpick(q, Ab::MuaatAgent, "Umbat (" + pn(p) + "): produce in which system?", where, "Up to 2 units that cost 4 or less.", true);
                if (s < 0) {
                    say(q, pn(q) + " produces nothing (Umbat).");
                    return true;
                }
            }
            maxUnitCost_ = 4;
            G.fx.anyPlanetGF = true;
            G.fx.anyPlanetGFFor = int8_t(q);
            notProductionAbility_ = true;
            production(q, s, 2, false);
            notProductionAbility_ = false;
            G.fx.anyPlanetGF = false;
            G.fx.anyPlanetGFFor = -1;
            maxUnitCost_ = 0;
            return true;
        }
        case Ab::FiresOfGashlai: {
            int m = playerOfFaction(G, "muaat");
            if (m < 0 || !holdsNote(G, p, m, kNoteFaction)) return false;
            if (G.players[m].fleet > 0) G.players[m].fleet = int8_t(G.players[m].fleet - 1);
            const int wsUp = warSunUpgradeFor(G, p);  // (fixes 26: a faction's own war sun upgrade - the Roh'Dhna's)
            researchTech(p, wsUp, false);
            returnNote(p, makeNote(m, kNoteFaction));
            abilityEvent3(E, p, "Fires of the Gashlai",
                          pn(p) + " plays Fires of the Gashlai and gains " + (wsUp == findTech("ws") ? std::string("the War Sun technology") : kTechs[wsUp].name) + ".");
            enforceFleetLimit(m);
            return true;
        }
        case Ab::DarkPact: {
            const int e = playerOfFaction(G, "empyrean");
            const int note = makeNote(e, kNoteFaction);
            auto it = std::find(pl.notes.begin(), pl.notes.end(), int16_t(note));
            if (e < 0 || it == pl.notes.end()) return false;
            pl.notes.erase(it);
            pl.notesInPlay.push_back(int16_t(note));
            abilityEvent3(E, p, "Dark Pact", pn(p) + " plays Dark Pact with " + pn(e) + ".");
            return true;
        }
        case Ab::BloodPact: {
            int e = playerOfFaction(G, "empyrean");
            int note = makeNote(e, kNoteFaction2);
            auto it = std::find(pl.notes.begin(), pl.notes.end(), int16_t(note));
            if (e < 0 || it == pl.notes.end()) return false;
            pl.notes.erase(it);
            pl.notesInPlay.push_back(int16_t(note));
            abilityEvent3(E, p, "Blood Pact", pn(p) + " plays Blood Pact with " + pn(e) + ".");
            return true;
        }
        case Ab::LazaxGate: {
            int x = mecatolPlanet(G);
            if (x < 0) return false;
            exhaustTech(p, "lgf");
            addUnits(p, UnitType::Infantry, 1, G.planets[x].system, x);
            say(p, pn(p) + " puts 1 infantry on Mecatol Rex (Lazax Gate Folding).", G.planets[x].system);
            return true;
        }
        case Ab::CreussHero: {
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                const SystemState& sy = G.systems[s];
                if (sy.offBoard || sy.isHyperlane() || int(s) == G.nexus) continue;
                if (G.fixes >= 24 && sy.fracture) continue;  // (fixes 24, Thunder's Edge rulebook: not the Fracture's systems)
                if (wormholesOf(G, int(s)) || hasUnits(G, p, int(s))) opts.push_back({int(s), sysName(int(s)), ""});
            }
            if (opts.size() < 2) return false;
            std::vector<int> picks = fpickMany(p, Ab::CreussHero, "Riftwalker Meian: swap which 2 systems?", opts, 2, 2,
                                               G.fixes >= 24 ? "Systems with wormholes or your units - not the Creuss home system, Ahk Creuxx, the Fracture or "
                                                               "the Wormhole Nexus."
                                                             : "Systems with wormholes or your units (not the Creuss home system or the Wormhole Nexus).");
            if (picks.size() != 2) return false;
            purgeHero(p);
            std::swap(G.systems[picks[0]].pos, G.systems[picks[1]].pos);
            computeAdjacency(G);
            heroEvent3(E, G, p, pn(p) + " uses Riftwalker Meian: " + sysName(picks[0]) + " and " + sysName(picks[1]) + " swap places.");
            return true;
        }
        case Ab::SaarHero: {
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                bool dock = G.systems[s].space[p].count(UnitType::SpaceDock) > 0;
                for (int16_t x : G.systems[s].planets)
                    if (G.planets[x].units[p].count(UnitType::SpaceDock)) dock = true;
                if (!dock) continue;
                for (int16_t a : G.systems[s].adj) {
                    bool listed = false;
                    for (const Option& o : opts)
                        if (o.id == a) listed = true;
                    if (!listed) opts.push_back({a, sysName(a), ""});
                }
            }
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            int s = fpick(p, Ab::SaarHero, "Gurno Aggero: destroy other players' infantry and fighters in which system?", opts, "", true);
            if (s < 0) return false;
            // (fixes 27, the Saar review - owner 2026-11-13) "Then, purge this card" - purged last; Thunder's Edge's neutral
            // units count as another player's
            if (G.fixes < 27) purgeHero(p);
            heroEvent3(E, G, p, pn(p) + " unleashes Gurno Aggero on " + sysName(s) + ".");
            for (int q = 0; q < kSides; ++q) {
                if (q == p || (q >= G.numPlayers && (q != kNeutral || G.fixes < 27))) continue;
                Force& f = G.systems[s].space[q];
                if (f.count(UnitType::Fighter)) destroyUnits(q, UnitType::Fighter, f.count(UnitType::Fighter), s, -1, p, "Gurno Aggero");
                if (f.count(UnitType::Infantry)) destroyUnits(q, UnitType::Infantry, f.count(UnitType::Infantry), s, -1, p, "Gurno Aggero");
                for (int16_t x : G.systems[s].planets)
                    if (G.planets[x].units[q].count(UnitType::Infantry))
                        destroyUnits(q, UnitType::Infantry, G.planets[x].units[q].count(UnitType::Infantry), s, x, p, "Gurno Aggero");
            }
            if (G.fixes >= 27) purgeHero(p);
            checkEliminations();
            return true;
        }
        case Ab::WinnuHero: {
            std::vector<Option> opts;
            for (int c = 0; c < kStrategyCards; ++c) opts.push_back({c, strategyName(c), strategyPrimary(c)});
            opts.push_back({-1, "Cancel", ""});
            int c = fpick(p, Ab::WinnuHero, "Mathis Mathinus: resolve the primary ability of which strategy card?", opts);
            if (c < 0) return false;
            purgeHero(p);
            heroEvent3(E, G, p, pn(p) + " uses Mathis Mathinus: " + strategyName(c) + ".");
            const int prev = strategicPlayer_;
            strategicPlayer_ = p;
            primaryAbility(p, c);
            // (fixes 26, the Winnu review: "Then, choose any number of other players. Those players may perform the secondary
            // ability of that strategy card." - the Winnu choose; and it isn't a strategic action, so no Acquiescence)
            std::vector<int> chosen;
            if (G.fixes >= 26) {
                std::vector<Option> who;
                for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers))
                    if (q != p && !G.players[q].eliminated) who.push_back({q, pn(q), std::to_string(G.players[q].vp) + " VP"});
                if (!who.empty())
                    chosen = fpickMany(p, Ab::WinnuHero, std::string("Mathis Mathinus: which players may use the secondary ability of ") + strategyName(c) + "?",
                                       who, 0, int(who.size()), "Those players may perform the secondary ability (paying for it as usual).");
                mathis_ = true;
            }
            for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers))
                if (q != p && !G.players[q].eliminated && (G.fixes < 26 || std::find(chosen.begin(), chosen.end(), q) != chosen.end()))
                    secondaryAbility(q, c);
            mathis_ = false;
            strategicPlayer_ = prev;
            return true;
        }
        case Ab::NekroHero: {
            std::vector<Option> opts;
            for (size_t x = 0; x < G.planets.size(); ++x) {
                const PlanetState& ps = G.planets[x];
                if (ps.destroyed || ps.system < 0 || !planetHasSkip(G, int(x)) || !hasUnits(G, p, ps.system)) continue;
                opts.push_back({int(x), planetName(int(x)), std::to_string(planetResources(G, int(x)) + planetInfluence(G, int(x))) + " trade goods"});
            }
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            int x = fpick(p, Ab::NekroHero, "UNIT.DSGN.FLAYESH: which planet?", opts, "", false, true);
            if (x < 0) return false;
            purgeHero(p);
            const int sys = G.planets[x].system;
            heroEvent3(E, G, p, pn(p) + " uses UNIT.DSGN.FLAYESH on " + planetName(x) + ".");
            for (int q = 0; q < G.numPlayers; ++q) {
                if (q == p) continue;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (G.planets[x].units[q].n[t]) destroyUnits(q, UnitType(t), G.planets[x].units[q].n[t], sys, x, p, "UNIT.DSGN.FLAYESH");
            }
            gainTG(p, planetResources(G, x) + planetInfluence(G, x));
            int skips = planetSkips(G, x);
            std::vector<Option> techs;
            for (int t = 0; t < kTechCount; ++t) {
                const TechDef& d = kTechs[t];
                if (d.faction >= 0 || d.unitUpgrade || d.color == TechColor::None || !((skips >> int(d.color)) & 1) || pl.hasTech(t)) continue;
                if (d.source != Source::Base && d.source != Source::PoK) continue;
                if (G.fixes >= 9 && !techImplemented(t)) continue;  // (0.44.5: not the Codex Keleres I.I.H.Q.)
                techs.push_back({t, d.name, techSummary(t)});
            }
            if (!techs.empty()) {
                int t = fpick(p, Ab::NekroHero, "UNIT.DSGN.FLAYESH: gain which technology?", techs);
                researchTech(p, t, false);
                E.log(p, pn(p) + " gains " + kTechs[t].name + ".", EventKind::Research);
            }
            checkEliminations();
            return true;
        }
        case Ab::EmpyreanHero: {
            // (fixes 26, the Empyrean review - owner 2026-11-12: "Then, purge this card" - purged last; a space station
            // isn't a planet - planetless)
            const bool late = G.fixes >= 26;
            if (!late) purgeHero(p);
            heroEvent3(E, G, p, pn(p) + " uses Conservator Procyon.");
            int placed = 0;
            for (size_t x = 0; x < G.systems.size(); ++x) {
                SystemState& s = G.systems[x];
                if (!s.isHyperlane() && (late ? planetless(G, int(x)) : s.planets.empty()) && !s.frontier) {
                    s.frontier = true;
                    ++placed;
                }
            }
            say(p, std::to_string(placed) + " frontier tokens appear (Conservator Procyon).");
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (G.systems[s].frontier && hasShips(G, p, int(s))) exploreFrontier(p, int(s));
            if (late) purgeHero(p);
            return true;
        }
        case Ab::NekroAgent: {  // Nekro Malleon: discard an action card or spend a command token for 2 trade goods
            if (G.fixes >= 27) return nekroMalleon(p);
            std::vector<Option> opts;
            for (int16_t c : pl.actionCards) opts.push_back({c, std::string("Discard ") + kActionCards[c].name, ""});
            if (pl.tactic > 0) opts.push_back({1000, "Spend a tactic token", ""});
            if (pl.fleet > 0) opts.push_back({1001, "Spend a fleet token", ""});
            if (pl.strategy > 0) opts.push_back({1002, "Spend a strategy token", ""});
            opts.push_back({-1, "Cancel", ""});
            int c = fpick(p, Ab::NekroAgent, "Nekro Malleon: what do you give up for 2 trade goods?", opts);
            if (c < 0) return false;
            spendAgent(p, "nekro");
            if (c == 1000) pl.tactic = int8_t(pl.tactic - 1);
            else if (c == 1001) pl.fleet = int8_t(pl.fleet - 1);
            else if (c == 1002) spendStrategy(p, 1);
            else {
                pl.actionCards.erase(std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(c)));
                discardActionCard(p, c, false);
            }
            gainTG(p, 2);
            say(p, pn(p) + " gains 2 trade goods (Nekro Malleon).");
            if (c == 1001) enforceFleetLimit(p);
            return true;
        }
        default: return factionAction4(p, ab);
    }
}

}  // namespace ti
