// Stage 5, batch 4: the Argent Flight, Mahact Gene-Sorcerers, Naaz-Rokha
// Alliance, Nomad, Titans of Ul and Vuil'raith Cabal. Original Prophecy of
// Kings card versions (owner's choice). Agents are only offered when their
// owner is involved (owner OK'd, RULES-CHECK 0.6.0 #2). Abilities that need
// transactions wait for the Negotiations menu (Stage 6).
#include <algorithm>
#include <cstring>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {

void abilityEvent4(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    e.system = int16_t(sys);
    E.emit(e);
}
void heroEvent4(Engine& E, const Game& G, int p, const std::string& text) {
    int i = leaderIndex(G, p, kHero);
    Event e;
    e.kind = EventKind::Leader;
    e.player = int8_t(p);
    e.card = int16_t(i >= 0 ? G.players[p].leaders[i].leader : 0);
    e.label = "hero";
    e.text = text;
    E.emit(e);
}
std::string count(int k, UnitType t) { return std::to_string(k) + " " + unitTypeName(t, k > 1); }
bool explorable(const Game& G, int x) {
    const PlanetState& ps = G.planets[x];
    if (ps.destroyed || ps.system < 0) return false;
    return hasTraitOf(G, x, Trait::Cultural) || hasTraitOf(G, x, Trait::Hazardous) || hasTraitOf(G, x, Trait::Industrial);
}
int upgradeType(int tech) {
    for (int u = 0; u < kUnitCount; ++u)
        if (kUnits[u].tech == tech) return int(kUnits[u].type);
    return -1;
}
int planetByKey(const Game& G, const char* key) {
    int d = findPlanet(key);
    for (size_t x = 0; x < G.planets.size(); ++x)
        if (G.planets[x].def == d) return int(x);
    return -1;
}
bool hasDock(const Game& G, int p, int sys) {
    if (G.systems[sys].space[p].count(UnitType::SpaceDock)) return true;
    for (int16_t x : G.systems[sys].planets)
        if (G.planets[x].units[p].count(UnitType::SpaceDock)) return true;
    return false;
}
bool hasMech(const Game& G, int p, int sys) {
    if (G.systems[sys].space[p].count(UnitType::Mech)) return true;
    for (int16_t x : G.systems[sys].planets)
        if (G.planets[x].units[p].count(UnitType::Mech)) return true;
    return false;
}
}  // namespace

// ------------------------------------------------------------------ Nomad agents, leaders
bool Rules::canUseNomadAgent(int who, int nth) const {
    if (who < 0 || who >= G.numPlayers || G.players[who].eliminated) return false;
    if (fac(who, "nomad")) return agentReady(G, who, nth);
    // Ssruu (Yssaril agent) has the text of every other agent
    return fac(who, "yssaril") && agentReady(G, who) && playerOfFaction(G, "nomad") >= 0;
}

void Rules::spendNomadAgent(int who, int nth) {
    if (fac(who, "nomad")) {
        exhaustAgent(who, nth);
        return;
    }
    exhaustAgent(who);
    say(who, pn(who) + " uses Ssruu as " + pn(playerOfFaction(G, "nomad")) + "'s agent.");
}

void Rules::readyAgent(int p, int index) {
    Player& pl = G.players[p];
    LeaderState& ls = pl.leaders[index];
    ls.status = kReady;
    // Artuno the Betrayer: the trade goods on the card are gained when it readies (fixes 26, the Nomad review: Ssruu as
    // Artuno too - the Yssaril's agent)
    const bool ssruu = G.fixes >= 26 && fac(p, "yssaril") && kLeaders[ls.leader].type == kAgent;
    if ((!std::strcmp(kLeaders[ls.leader].key, "nomadagentartuno") || ssruu) && pl.agentTG > 0) {
        const int n = pl.agentTG;
        pl.agentTG = 0;
        say(p, pn(p) + " collects " + std::to_string(n) + " trade good" + (n > 1 ? "s" : "") + " from " + (ssruu ? "Ssruu (Artuno the Betrayer)." : "Artuno the Betrayer."));
        artunoPay_ = true;
        gainTG(p, n);
        artunoPay_ = false;
    }
}

// ------------------------------------------------------------------ Vuil'raith: capture (LRR 17)
bool Rules::cannotCaptureFrom(int by, int owner) const {
    if (owner < 0 || owner == by) return false;
    // LRR 17.6: while one of your space docks is blockaded by that player
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (hasDock(G, by, int(s)) && hasShips(G, owner, int(s)) && !hasShips(G, by, int(s))) return true;
    return false;
}

void Rules::capture(int by, int owner, UnitType t, int k, const char* why) {
    if (k <= 0 || by < 0 || isStructure(t) || G.players[by].eliminated || cannotCaptureFrom(by, owner)) return;
    // Mobile Command (Ghoti, owner D132, fixes >= 2): their flagship - their home - can't be captured
    if (t == UnitType::Flagship && G.fixes >= 2 && (G.modules & kModDiscordant) && owner >= 0 && owner < G.numPlayers && fac(owner, "ghoti")) return;
    const bool token = t == UnitType::Fighter || t == UnitType::Infantry;  // LRR 17.3: a token instead
    // (0.55.0, fixes 19) only what is really back in its owner's reinforcements can be captured (unit limits)
    if (!token && G.fixes >= 19 && owner >= 0 && owner < G.numPlayers) k = std::min(k, unitsAvailable(G, owner, t));
    if (k <= 0) return;
    for (int i = 0; i < k; ++i) G.players[by].captured.push_back(int16_t((token ? kCapturedToken : owner) * 16 + int(t)));
    G.players[by].usedFactionAbility = true;
    say(by, pn(by) + " captures " + (owner == by ? std::string("their own ") : pn(owner) + "'s ") + count(k, t) + " (" + why + ").");
}

bool Rules::returnCaptured(int p, UnitType t) {
    auto& c = G.players[p].captured;
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i] % 16 == int(t)) {
            c.erase(c.begin() + long(i));
            return true;
        }
    return false;
}

void Rules::capturedBlockade() {
    // LRR 17.2c: blockading a space dock of the capturing player frees your captured ships and mechs
    for (int c = 0; c < G.numPlayers; ++c) {
        auto& cap = G.players[c].captured;
        if (cap.empty()) continue;
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == c) continue;
            bool blockade = false;
            for (size_t s = 0; s < G.systems.size() && !blockade; ++s)
                if (hasDock(G, c, int(s)) && hasShips(G, q, int(s)) && !hasShips(G, c, int(s))) blockade = true;
            if (!blockade) continue;
            size_t before = cap.size();
            cap.erase(std::remove_if(cap.begin(), cap.end(), [q](int16_t x) { return x / 16 == q; }), cap.end());
            if (cap.size() != before)
                say(q, pn(q) + " blockades " + pn(c) + "'s space dock: their captured units return to " + pn(q) + ".");
        }
    }
}

// ------------------------------------------------------------------ unit abilities: extra dice
int Rules::abilityDice(int p, int sys, bool plasma) {
    int n = plasma && owns(p, "ps") ? 1 : 0;    // Plasma Scoring (bombardment and space cannon only)
    if (commanderUsable(G, p, "argent")) ++n;  // Trrakan Aun Zulok (Argent commander)
    if (holdsRelic(p, "starfall_array")) ++n;   // Starfall Array (Uncharted Space): 1 of the units rolls 1 more die
    // Strike Wing Ambuscade (Argent's note)
    const int a = playerOfFaction(G, "argent");
    if (a >= 0 && a != p && holdsNote(G, p, a, kNoteFaction) &&
        fyes(p, Ab::Ambuscade, "Play Strike Wing Ambuscade: 1 extra die for this roll?", factionNoteText(G.players[a].faction, kNoteFaction))) {
        ++n;
        returnNote(p, makeNote(a, kNoteFaction));
        abilityEvent4(E, p, "Strike Wing Ambuscade", pn(p) + " plays Strike Wing Ambuscade: 1 extra die.", sys);
    }
    return n;
}

void Rules::addDice(RollResult& r, int n, int need, int unit) {
    for (int i = 0; i < n; ++i) {
        r.dice.push_back(int8_t(E.rng().d10()));
        r.need.push_back(int8_t(need));
        r.unit.push_back(int8_t(unit));
        r.bonus9.push_back(0);
    }
    recount(r);
}

void Rules::raidFormation(int p, int o, int sys, int excess) {
    // Raid Formation (Argent): barrage hits beyond the fighters damage ships that have Sustain Damage
    if (!fac(p, "argent") || excess <= 0 || o < 0) return;
    Force& f = G.systems[sys].space[o];
    for (int i = 0; i < excess; ++i) {
        std::vector<Option> opts;
        for (int t : kShipIdx)
            if (f.n[t] > f.dmg[t] && unitSustains(G, o, UnitType(t)))
                opts.push_back({t, pn(o) + "'s " + unitTypeName(UnitType(t)), std::to_string(f.n[t] - f.dmg[t]) + " undamaged"});
        if (opts.empty()) return;
        int t = opts.size() == 1 ? opts[0].id
                                 : fpick(p, Ab::RaidFormation, "Raid Formation: which of " + pn(o) + "'s ships becomes damaged?", opts,
                                         abilityText(G.players[p].faction, 1));
        f.dmg[t] = uint8_t(f.dmg[t] + 1);
        G.players[p].usedFactionAbility = true;
        Event e;
        e.kind = EventKind::Damaged;
        e.player = int8_t(o);
        e.other = int8_t(p);
        e.system = int16_t(sys);
        e.unit = int8_t(t);
        e.count = 1;
        e.text = pn(p) + "'s Raid Formation damages " + pn(o) + "'s " + unitTypeName(UnitType(t)) + ".";
        E.emit(e);
    }
}

void Rules::strikeWingAlpha(int p, int o, int sys, const RollResult& r) {
    // Strike Wing Alpha II (Argent destroyer): barrage 9s and 10s also destroy infantry in space
    if (o < 0 || std::strcmp(unitDef(G, p, UnitType::Destroyer).key, "argent_destroyer2") != 0) return;
    int n = 0;
    for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i)
        if (r.unit[i] == int(UnitType::Destroyer) && r.dice[i] >= 9) ++n;
    // (fixes 27, the Nekro review) The Alastor: the infantry fighting from the planets aren't in the space area, those waiting
    // aside are (they go first)
    const bool al = G.fixes >= 27 && o >= 0 && o < kSides && alastorSys_ == sys && alastor_[o];
    const int aside = al ? alastorAside_[o].count(UnitType::Infantry) : 0;
    const int inSpace = al ? std::max(0, int(alastorOwn_[o][0])) : int(G.systems[sys].space[o].count(UnitType::Infantry));
    n = std::min(n, std::min<int>(inSpace, G.systems[sys].space[o].count(UnitType::Infantry)) + aside);
    if (n <= 0) return;
    say(p, pn(p) + "'s Strike Wing Alpha II destroys " + count(n, UnitType::Infantry) + " in space.", sys);
    if (al) {  // (the ones aside: back into the space area to be destroyed there; then the space area's own fighting)
        const int k = std::min(n, aside);
        alastorAside_[o].remove(UnitType::Infantry, k);
        G.systems[sys].space[o].add(UnitType::Infantry, k);
        alastorOwn_[o][0] = uint8_t(std::max(0, int(alastorOwn_[o][0]) - (n - k)));
    }
    destroyUnits(o, UnitType::Infantry, n, sys, -1, p, "Strike Wing Alpha II");
}

int Rules::ulCannonDice(int q, int sys) const {
    // Ul the Progenitor: Elysium has Space Cannon 5 (x3) as if it were a unit
    for (int16_t x : G.systems[sys].planets)
        if (G.planets[x].ulHero && G.planets[x].owner == q) return 3;
    if (sys == G.mecatol() && custodiaMecatol(G, q)) return 1;  // Custodian's Favour (Keleres breakthrough): Space Cannon 5
    return 0;
}

// ------------------------------------------------------------------ agendas
int Rules::zeal(int p) const {
    if (!fac(p, "argent")) return 0;
    int n = 0;
    for (const Player& pl : G.players)
        if (!pl.eliminated) ++n;
    return n;
}

int Rules::geneticRecombination(int voter, int agenda) {
    for (int m = 0; m < G.numPlayers; ++m) {
        if (m == voter || G.players[m].eliminated || !techReady(m, "gr")) continue;
        std::vector<Option> opts = agendaOutcomes(G.currentAgenda);
        opts.push_back({kNoVote, "Don't use it", ""});
        const int prevA = curAgenda_;
        curAgenda_ = agenda;
        curValue_ = voter;
        int o = fpick(m, Ab::GeneticRecombination, "Genetic Recombination: make " + pn(voter) + " vote for which outcome?", opts,
                      techSummary(findTech("gr")));
        curValue_ = 0;
        if (o == kNoVote) {
            curAgenda_ = prevA;
            continue;
        }
        exhaustTech(m, "gr");
        abilityEvent4(E, m, "Genetic Recombination", pn(m) + " uses Genetic Recombination on " + pn(voter) + ".");
        Player& v = G.players[voter];
        std::vector<Option> ch = {{1, "Vote for " + outcomeName(G.currentAgenda, o), "You must cast at least 1 vote for it."}};
        if (v.fleet > 0) ch.push_back({0, "Lose a fleet token instead", "It goes back to your reinforcements."});
        curValue_ = o;
        int c = ch.size() == 1 ? 1 : fpick(voter, Ab::GeneticRecombination, "Genetic Recombination: vote for " + outcomeName(G.currentAgenda, o) + "?", ch);
        curValue_ = 0;
        curAgenda_ = prevA;
        if (c == 1) return o;
        v.fleet = int8_t(v.fleet - 1);
        say(voter, pn(voter) + " loses a fleet token instead (Genetic Recombination).");
        enforceFleetLimit(voter);
        return kNoVote;
    }
    return kNoVote;
}

void Rules::futureSight(int outcome, const std::vector<int>& votedFor, const std::vector<int>* preds) {
    const int n = playerOfFaction(G, "nomad");
    if (n < 0) return;
    // (fixes 26, the Nomad review - owner 2026-11-13) "During the Agenda phase" - not an agenda voted on in the action phase
    // (Executive Order)
    if (G.fixes >= 26 && G.phase != Phase::Agenda) return;
    bool ok = n < int(votedFor.size()) && votedFor[n] == outcome;
    for (size_t i = 0; i + 1 < preds[n].size(); i += 2)
        if (preds[n][i + 1] == outcome) ok = true;
    if (!ok) return;
    G.players[n].usedFactionAbility = true;
    say(n, pn(n) + " gains 1 trade good (Future Sight).");
    gainTG(n, 1);
}

// ------------------------------------------------------------------ Mahact
void Rules::edictWon(int winner, int loser) {
    if (winner < 0 || loser < 0 || loser >= G.numPlayers || !fac(winner, "mahact") || G.players[loser].eliminated) return;
    Player& m = G.players[winner];
    if ((m.edict >> loser) & 1) return;
    if (reinforcementTokens(loser) <= 0) return;
    m.edict = uint8_t(m.edict | (1u << loser));
    m.usedFactionAbility = true;
    abilityEvent4(E, winner, "Edict", pn(winner) + " puts one of " + pn(loser) + "'s command tokens in their fleet pool (Edict).");
}

bool Rules::jaeMirKan(int q, bool ask) {
    // Jae Mir Kan (Mahact agent): use one of the active player's tokens from the board for the secondary
    const int a = strategicPlayer_;
    if (a < 0 || a == q || !canUseAgent(q, "mahact")) return false;
    std::vector<Option> opts;
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (G.systems[s].hasToken(a)) opts.push_back({int(s), sysName(int(s)), ""});
    if (opts.empty()) return false;
    if (!ask) return true;
    if (G.players[q].strategy > 0) opts.push_back({-1, "Spend my own strategy token", ""});
    int s = fpick(q, Ab::MahactAgent, "Jae Mir Kan: remove one of " + pn(a) + "'s command tokens from the board to pay instead?", opts,
                  leaderText(findLeader("mahactagent")), true);
    if (s < 0) return false;
    spendAgent(q, "mahact");
    G.systems[s].tokens = uint16_t(G.systems[s].tokens & ~(1u << a));
    say(q, pn(q) + " uses " + pn(a) + "'s command token from " + sysName(s) + " (Jae Mir Kan).", s);
    return true;
}

// ------------------------------------------------------------------ tactical action
bool Rules::afterActivation4(int p, int sys) {
    SystemState& s = G.systems[sys];
    // Starlancer (Mahact mech): give back the active player's token from the fleet pool to end their turn
    for (int m = 0; m < G.numPlayers; ++m) {
        if (m == p || !fac(m, "mahact") || !((G.players[m].edict >> p) & 1) || !hasMech(G, m, sys) || !mechAbilities(G)) continue;
        if (!fyes(m, Ab::Starlancer, "Starlancer: give " + pn(p) + "'s command token back to end their turn?",
                  pn(p) + " activated " + sysName(sys) + ", where your Starlancer mech is."))
            continue;
        G.players[m].edict = uint8_t(G.players[m].edict & ~(1u << p));
        abilityEvent4(E, m, "Starlancer", pn(m) + "'s Starlancer ends " + pn(p) + "'s turn; " + pn(p) + " gets their command token back.", sys);
        gainTokens(p, 1, false, "Starlancer: your command token comes back from the Mahact fleet pool.");
        return true;
    }
    // Crucible (Vuil'raith note): no gravity rift rolls this movement
    const int c = playerOfFaction(G, "cabal");
    if (c >= 0 && c != p && holdsNote(G, p, c, kNoteFaction)) {
        bool rift = false;
        const bool gd = owns(p, "gd");
        // (fixes 26, the Vuil'raith review) with the note's +1: a ship that only gets there with it is a reason to play it
        const bool was = G.fx.crucible;
        if (G.fixes >= 26) G.fx.crucible = true;
        for (size_t f = 0; f < G.systems.size() && !rift; ++f) {
            if (int(f) == sys || G.systems[f].hasToken(p)) continue;
            for (int t : kShipIdx)
                if (!rift && G.systems[f].space[p].n[t]) {
                    Reach r = shipReach(G, p, int(f), sys, UnitType(t), gd ? 1 : 0);
                    if (r.ok && r.viaRift) rift = true;
                }
        }
        G.fx.crucible = was;
        if (rift && fyes(p, Ab::Crucible,
                         G.fixes >= 26 ? "Play Crucible: no gravity rift rolls this movement, and +1 move for your ships leaving or passing through a rift?"
                                       : "Play Crucible: your ships don't roll for gravity rifts during this movement?",
                         factionNoteText(G.players[c].faction, kNoteFaction))) {
            G.fx.crucible = true;
            returnNote(p, makeNote(c, kNoteFaction));
            abilityEvent4(E, p, "Crucible",
                          pn(p) + (G.fixes >= 26 ? " plays Crucible: no gravity rift rolls this movement, +1 move through gravity rifts."
                                                 : " plays Crucible: no gravity rift rolls this movement."),
                          sys);
        }
    }
    if (!fac(p, "titans")) return false;
    // Awaken (Titans): sleeper tokens in the system become PDS (Hecatoncheires may replace them)
    std::vector<int> sleepers;
    for (int16_t x : s.planets)
        if (G.planets[x].sleeper && !G.planets[x].destroyed &&
            (G.fixes < 14 || std::find(activationSleepers_.begin(), activationSleepers_.end(), x) != activationSleepers_.end()))
            sleepers.push_back(x);
    if (!sleepers.empty() &&
        fyes(p, Ab::Awaken, "Awaken: replace your sleeper tokens in " + sysName(sys) + " with PDS?", abilityText(G.players[p].faction, 1))) {
        G.players[p].usedFactionAbility = true;
        for (int x : sleepers) {
            if (unitsAvailable(G, p, UnitType::PDS) <= 0 && unitsAvailable(G, p, UnitType::Mech) <= 0) break;
            G.planets[x].sleeper = false;
            if (!hecatoncheires(p, x)) {
                if (unitsAvailable(G, p, UnitType::PDS) <= 0) {
                    G.planets[x].sleeper = true;
                    continue;
                }
                addUnits(p, UnitType::PDS, 1, sys, x);
            }
            abilityEvent4(E, p, "Awaken", pn(p) + "'s sleeper on " + planetName(x) + " awakens.", sys);
            bool others = G.planets[x].owner != p;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && G.planets[x].units[q].groundForces() > 0) others = true;
            if (others) awakened_.push_back(x);  // Coalescence: they fight in the ground combat step
        }
    }
    // Ouranos (Titans flagship), DEPLOY: replace one of your PDS here with it
    if (unitsAvailable(G, p, UnitType::Flagship) > 0) {
        std::vector<Option> opts;
        for (int16_t x : s.planets)
            if (G.planets[x].units[p].count(UnitType::PDS)) opts.push_back({x, planetName(x), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Don't deploy it", ""});
            int x = fpick(p, Ab::Ouranos, "Deploy Ouranos: replace one of your PDS here with your flagship?", opts,
                          unitAbilityText(G.players[p].unit[int(UnitType::Flagship)]), false, true);
            if (x >= 0) {
                removing_ = true;
                destroyUnits(p, UnitType::PDS, 1, sys, x, -1, "Ouranos");
                removing_ = false;
                addUnits(p, UnitType::Flagship, 1, sys, -1);
                abilityEvent4(E, p, "Ouranos", pn(p) + " deploys Ouranos in " + sysName(sys) + ".", sys);
                enforceFleetIn(p, sys);
            }
        }
    }
    return false;
}

void Rules::endOfTactical4(int p, int sys) {
    if (ilNa_) {
        // Il Na Viroset: both command tokens go back to the reinforcements and the turn ends
        G.systems[sys].tokens = uint16_t(G.systems[sys].tokens & ~(1u << p));
        say(p, pn(p) + "'s command tokens in " + sysName(sys) + " return; the turn ends (Il Na Viroset).", sys);
        turnEnded_ = true;
        ilNa_ = false;
    }
    capturedBlockade();
}

void Rules::coalescence(int p, int sys) {
    std::vector<int> list;
    list.swap(awakened_);
    for (int x : list) {
        PlanetState& ps = G.planets[x];
        if (ps.destroyed || groundForcesOn(G, p, x) == 0) continue;
        bool coexisting = false;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && groundForcesOn(G, p, x) > 0 && groundForcesOn(G, q, x) > 0) {
                if (!coexisting && slumberstate(p, x)) coexisting = true;  // Thunder's Edge breakthrough
                if (coexisting) continue;
                say(p, pn(p) + "'s awakened units must fight on " + planetName(x) + " (Coalescence).", sys);
                groundCombat(p, q, x);
            }
        if (coexisting) continue;
        bool contested = false;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && groundForcesOn(G, q, x) > 0) contested = true;
        if (!contested && groundForcesOn(G, p, x) > 0 && ps.owner != p) setControl(x, p);
    }
}

void Rules::garvGunn27(int p) {
    // (fixes 27, the Naaz-Rokha review - owner 2026-11-13: "At the end of a player's turn: You may exhaust this card to allow
    // that player to explore 1 of their planets" - any player's turn; the Naaz-Rokha decide: a person is asked (garvQuiet_),
    // a bot only when it promised the agent)
    if (p < 0 || p >= G.numPlayers || G.players[size_t(p)].eliminated) return;
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, p))
        if (explorable(G, x)) opts.push_back({x, planetName(x), G.planets[x].units[p].count(UnitType::Mech) ? "Has your mech" : ""});
    if (opts.empty()) return;
    const std::string text = leaderText(findLeader("naazagent"));
    if (canUseAgent(p, "naaz")) {
        opts.push_back({-1, "Don't explore", ""});
        const int x = fpick(p, Ab::NaazAgent, "Exhaust Garv and Gunn to explore one of your planets?", opts, text, false, true);
        if (x < 0) return;
        spendAgent(p, "naaz");
        explorePlanet(p, x);
        return;
    }
    int user = -1;
    for (int o : initiativeOrder(G)) {
        if (o == p || !canUseAgent(o, "naaz")) continue;
        const bool promised = G.agentFor[o] == p;
        const bool human = G.players[size_t(o)].human;
        if (!promised && !(human && garvQuiet_[o] != G.round)) continue;
        agentFor(o, p);
        std::vector<Option> yn = {{1, "Yes", ""}, {0, "No", ""}};
        if (human && !promised) yn.push_back({3, "No, and don't ask again this round", "You'll be asked again next round."});
        const int c = fpick(o, Ab::NaazAgent, "Exhaust Garv and Gunn: " + pn(p) + " may explore 1 of their planets?", yn, text);
        curFor_ = -1;
        if (c == 3) garvQuiet_[o] = int16_t(G.round);
        if (c != 1) continue;
        user = o;
        break;
    }
    if (user < 0) return;
    spendAgent(user, "naaz");
    agentDone(user, p);
    const int x = fpick(p, Ab::NaazAgent, "Garv and Gunn (" + pn(user) + "): explore which of your planets?", opts, text, false, true);
    if (x >= 0) explorePlanet(p, x);
}

void Rules::endOfTurn4(int p) {
    // Garv and Gunn (Naaz-Rokha agent): explore one of your planets
    if (G.fixes >= 27) garvGunn27(p);
    else if (canUseAgent(p, "naaz")) {
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p))
            if (explorable(G, x))
                opts.push_back({x, planetName(x), G.planets[x].units[p].count(UnitType::Mech) ? "Has your mech" : ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Don't explore", ""});
            int x = fpick(p, Ab::NaazAgent, "Exhaust Garv and Gunn to explore one of your planets?", opts, leaderText(findLeader("naazagent")),
                          false, true);
            if (x >= 0) {
                spendAgent(p, "naaz");
                explorePlanet(p, x);
            }
        }
    }
    // Field Marshal Mercer (Nomad agent): move up to 2 ground forces onto your planets in the active system
    // (fixes 26, the Nomad review - owner 2026-11-13: "At the end of a player's turn ... allow that player" - mercer26)
    if (G.fixes >= 26) {
        mercer26(p, lastActiveSystem_);
        return;
    }
    const int sys = lastActiveSystem_;
    if (sys < 0 || G.players[p].eliminated || !canUseNomadAgent(p, 0)) return;
    std::vector<Option> dest;
    for (int16_t x : G.systems[sys].planets)
        if (G.planets[x].owner == p && !isDMZ(G, x) && !G.planets[x].destroyed) dest.push_back({x, planetName(x), ""});
    if (dest.empty()) return;
    const int np = int(G.planets.size());
    std::vector<Option> units;
    auto addFrom = [&](const Force& f, int loc, const std::string& where) {
        for (UnitType t : {UnitType::Infantry, UnitType::Mech})
            for (int i = 0; i < f.count(t) && i < 2; ++i) units.push_back({loc * 64 + int(t) * 8 + i, std::string(unitTypeName(t)) + " " + where, ""});
    };
    for (size_t x = 0; x < G.planets.size(); ++x)
        if (G.planets[x].system != sys && G.planets[x].units[p].groundForces()) addFrom(G.planets[x].units[p], int(x), "on " + planetName(int(x)));
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (G.systems[s].space[p].groundForces()) addFrom(G.systems[s].space[p], np + int(s), "in space at " + sysName(int(s)));
    if (units.empty()) return;
    const int cvWas = curValue_;
    curValue_ = sys;  // (0.55.0, for the AI: the system - the tactical action is over, G.activeSystem is gone)
    const bool use = fyes(p, Ab::NomadMercer, "Exhaust Field Marshal Mercer to move up to 2 ground forces to your planets in " + sysName(sys) + "?",
                          leaderText(findLeader("nomadagentmercer")));
    curValue_ = cvWas;
    if (!use) return;
    std::vector<int> picks = fpickMany(p, Ab::NomadMercer, "Field Marshal Mercer: which ground forces?", units, 1,
                                       std::min<int>(2, int(units.size())));
    int to = dest.size() == 1 ? dest[0].id : fpick(p, Ab::NomadMercer, "Field Marshal Mercer: onto which planet?", dest, "", false, true);
    spendNomadAgent(p, 0);
    std::vector<int> touched;
    for (int id : picks) {
        int loc = id / 64;
        UnitType t = UnitType((id % 64) / 8);
        Force& src = loc < np ? G.planets[loc].units[p] : G.systems[loc - np].space[p];
        if (!src.count(t)) continue;
        src.remove(t, 1, false);
        G.planets[to].units[p].add(t, 1);
        if (loc >= np) touched.push_back(loc - np);
    }
    say(p, pn(p) + " moves " + std::to_string(picks.size()) + " ground forces to " + planetName(to) + " (Field Marshal Mercer).", sys);
    for (int s : touched) removeExcessCapacity(p, s);
}

// Field Marshal Mercer (fixes 26, the Nomad review - owner 2026-11-13): "At the end of a player's turn: You may exhaust this
// card to allow that player to remove up to 2 of their ground forces from the game board and place them on planets they
// control in the active system." Any player's turn - the Nomad decide (a person is asked, or "don't ask again this round";
// a bot only when it promised that player its agent in a deal); ground forces from anywhere, the active system's planets
// too; each one onto any of that player's planets there.
void Rules::mercer26(int p, int sys) {
    if (sys < 0 || p < 0 || p >= G.numPlayers || G.players[p].eliminated) return;
    std::vector<int> dest;
    for (int16_t x : G.systems[sys].planets)
        if (G.planets[x].owner == p && !isDMZ(G, x) && !G.planets[x].destroyed) dest.push_back(x);
    if (dest.empty()) return;
    const int np = int(G.planets.size());
    std::vector<Option> units;
    auto addFrom = [&](const Force& f, int loc, const std::string& where) {
        for (UnitType t : {UnitType::Infantry, UnitType::Mech})
            for (int i = 0; i < f.count(t) && i < 2; ++i) units.push_back({loc * 64 + int(t) * 8 + i, std::string(unitTypeName(t)) + " " + where, ""});
    };
    for (size_t x = 0; x < G.planets.size(); ++x) {
        if (!G.planets[x].units[p].groundForces()) continue;
        if (dest.size() == 1 && dest[0] == int(x)) continue;  // (nowhere else to go)
        addFrom(G.planets[x].units[p], int(x), "on " + planetName(int(x)));
    }
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (G.systems[s].space[p].groundForces()) addFrom(G.systems[s].space[p], np + int(s), "in space at " + sysName(int(s)));
    if (units.empty()) return;
    int user = -1;
    if (canUseNomadAgent(p, 0)) {
        const int cvWas = curValue_;
        curValue_ = sys;  // (for the AI: the system - the tactical action is over, G.activeSystem is gone)
        const bool use = fyes(p, Ab::NomadMercer, "Exhaust Field Marshal Mercer to move up to 2 ground forces to your planets in " + sysName(sys) + "?",
                              leaderText(findLeader("nomadagentmercer")));
        curValue_ = cvWas;
        if (use) user = p;
    } else {
        for (int o : initiativeOrder(G)) {
            if (o == p || !canUseNomadAgent(o, 0)) continue;
            const bool promised = G.agentFor[o] == p;
            const bool human = G.players[size_t(o)].human;
            if (!promised && !(human && mercerQuiet_[o] != G.round)) continue;
            agentFor(o, p);
            std::vector<Option> opts = {{1, "Yes", ""}, {0, "No", ""}};
            if (human && !promised) opts.push_back({3, "No, and don't ask again this round", "You'll be asked again next round."});
            const int c = fpick(o, Ab::NomadMercer, "Exhaust Field Marshal Mercer: " + pn(p) + " may move up to 2 ground forces to their planets in " +
                                                     sysName(sys) + "?",
                                opts, leaderText(findLeader("nomadagentmercer")));
            curFor_ = -1;
            if (c == 3) mercerQuiet_[o] = int16_t(G.round);
            if (c != 1) continue;
            user = o;
            break;
        }
    }
    if (user < 0) return;
    spendNomadAgent(user, 0);
    if (user != p) agentDone(user, p);
    const int cvWas = curValue_;
    curValue_ = sys;
    std::vector<int> picks = fpickMany(p, Ab::NomadMercer, "Field Marshal Mercer: which ground forces?", units, 1, std::min<int>(2, int(units.size())));
    std::vector<int> touched, landed;
    int moved = 0;
    for (int id : picks) {
        const int loc = id / 64;
        const UnitType t = UnitType((id % 64) / 8);
        Force& src = loc < np ? G.planets[loc].units[p] : G.systems[loc - np].space[p];
        if (!src.count(t)) continue;
        std::vector<Option> to;
        for (int x : dest)
            if (x != loc) to.push_back({x, planetName(x), ""});
        if (to.empty()) continue;
        const int x = to.size() == 1 ? to[0].id
                                     : fpick(p, Ab::NomadMercer, std::string("Field Marshal Mercer: onto which planet (") + unitTypeName(t) + ")?", to, "",
                                             false, true);
        src.remove(t, 1, false);
        G.planets[x].units[p].add(t, 1);
        ++moved;
        if (loc >= np) touched.push_back(loc - np);
        if (std::find(landed.begin(), landed.end(), x) == landed.end()) landed.push_back(x);
    }
    curValue_ = cvWas;
    std::string where;
    for (size_t i = 0; i < landed.size(); ++i) where += (i ? " and " : "") + planetName(landed[i]);
    say(p, pn(p) + " moves " + std::to_string(moved) + " ground forces to " + where + " (Field Marshal Mercer).", sys);
    for (int s : touched) removeExcessCapacity(p, s);
}

// ------------------------------------------------------------------ strategy phase
void Rules::strategyStart4() {
    // Scepter of Dominion (Mahact's note): the Mahact player and every player whose token is in the
    // Mahact fleet pool put a token there (owner, RULES-CHECK 0.9.0 #3)
    const int m = playerOfFaction(G, "mahact");
    if (m < 0) return;
    for (int q : fromSpeaker(G)) {
        if (q == m || !holdsNote(G, q, m, kNoteFaction)) continue;
        std::vector<int> who;
        for (int r = 0; r < G.numPlayers; ++r)
            if (r != q && (r == m || ((G.players[m].edict >> r) & 1)) && !G.players[r].eliminated && reinforcementTokens(r) > 0) who.push_back(r);
        if (who.empty()) continue;
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (G.systems[s].home < 0 && (!G.systems[s].offBoard || (G.fixes >= 26 && int(s) == G.nexus)) && hasUnits(G, q, int(s)))  // (fixes 26: the nexus - LRR 100.1b)
                opts.push_back({int(s), sysName(int(s)), ""});
        if (opts.empty()) continue;
        opts.push_back({-1, "Don't play it", ""});
        int s = fpick(q, Ab::Scepter, "Play Scepter of Dominion: lock which of your systems?", opts,
                      factionNoteText(G.players[m].faction, kNoteFaction), true);
        if (s < 0) continue;
        for (int r : who)
            if (!G.systems[s].hasToken(r)) G.systems[s].tokens = uint16_t(G.systems[s].tokens | (1u << r));
        returnNote(q, makeNote(m, kNoteFaction));
        abilityEvent4(E, q, "Scepter of Dominion", pn(q) + " plays Scepter of Dominion on " + sysName(s) + ".", s);
    }
}

// ------------------------------------------------------------------ exploration and planets
int Rules::distantSuns(int p, int planet, int type, int first) {
    // Distant Suns (Naaz-Rokha): a planet with your mech - draw 2, resolve 1
    (void)planet;
    const int second = drawExplore(type);
    if (second < 0) return first;
    std::vector<Option> opts = {{first, kExploreCards[first].name, exploreText(first)}, {second, kExploreCards[second].name, exploreText(second)}};
    int keep = first == second ? first : fpick(p, Ab::DistantSuns, "Distant Suns: resolve which card?", opts, "The other one is discarded.");
    G.exploreDiscard[type].push_back(int16_t(keep == first ? second : first));
    G.players[p].usedFactionAbility = true;
    return keep;
}

void Rules::explored4(int p, int planet) {
    PlanetState& ps = G.planets[planet];
    if (ps.destroyed) return;
    // Pre-Fab Arcologies (Naaz-Rokha)
    if (owns(p, "pfa") && ps.owner == p && ps.exhausted) {
        ps.exhausted = false;
        // (0.43.4, owner's Bug 12: say why, so taking three planets that all end up ready isn't a surprise)
        say(p, planetName(planet) + " is readied: Pre-Fab Arcologies readies each planet you explore.", ps.system);
    }
    // Terragenesis (Titans): a sleeper token here (5 at most)
    if (!fac(p, "titans") || ps.sleeper) return;
    int n = 0;
    std::vector<Option> opts;
    for (size_t x = 0; x < G.planets.size(); ++x)
        if (G.planets[x].sleeper) {
            ++n;
            opts.push_back({int(x), "Move the one from " + planetName(int(x)), ""});
        }
    if (n < 5) opts.insert(opts.begin(), {10000, "Place a new sleeper token", std::to_string(5 - n) + " left"});
    if (opts.empty()) return;
    opts.push_back({-1, "No sleeper", ""});
    int c = fpick(p, Ab::Terragenesis, "Terragenesis: put a sleeper token on " + planetName(planet) + "?", opts,
                  abilityText(G.players[p].faction, 0));
    if (c < 0) return;
    if (c < 10000) G.planets[c].sleeper = false;
    ps.sleeper = true;
    G.players[p].usedFactionAbility = true;
    abilityEvent4(E, p, "Terragenesis", pn(p) + " places a sleeper token on " + planetName(planet) + " (Terragenesis).", ps.system);
}

void Rules::gainedControl4(int planet, int p, int old) {
    // Dart and Tai (Naaz-Rokha commander): explore a planet taken from another player
    if (old < 0 || old == p || G.phase == Phase::Setup || !commanderUsable(G, p, "naaz") || !explorable(G, planet)) return;
    if (!fyes(p, Ab::NaazCommander, "Dart and Tai: explore " + planetName(planet) + "?",
              "You took it from " + pn(old) + ". " + leaderText(findLeader("naazcommander")))) return;
    explorePlanet(p, planet);
}

bool Rules::hecatoncheires(int p, int planet) {
    // Hecatoncheires (Titans mech), DEPLOY: a mech and an infantry instead of a PDS
    if (!fac(p, "titans") || unitsAvailable(G, p, UnitType::Mech) <= 0 || !mechAbilities(G)) return false;
    if (!fyes(p, Ab::Hecatoncheires, "Deploy Hecatoncheires: put 1 mech and 1 infantry on " + planetName(planet) + " instead of a PDS?",
              unitAbilityText(G.players[p].unit[int(UnitType::Mech)])))
        return false;
    const int sys = G.planets[planet].system;
    addUnits(p, UnitType::Mech, 1, sys, planet);
    addUnits(p, UnitType::Infantry, 1, sys, planet);
    say(p, pn(p) + " deploys Hecatoncheires: a mech and an infantry on " + planetName(planet) + ".", sys);
    return true;
}

// ------------------------------------------------------------------ units destroyed
void Rules::unitDestroyed4(int p, UnitType t, int k, int sys, int planet) {
    // Crimson Legionnaire (Mahact infantry)
    if (t == UnitType::Infantry && fac(p, "mahact")) {
        Player& m = G.players[p];
        const int maxC = commodityValue(G, m);
        for (int i = 0; i < k; ++i) {
            bool gain = m.commodities < maxC, conv = m.commodities > 0;
            if (!gain && !conv) break;
            int c = gain && conv ? fpick(p, Ab::CrimsonLegionnaire, "Crimson Legionnaire: gain a commodity or turn one into a trade good?",
                                         {{0, "Gain 1 commodity", ""}, {1, "Turn 1 commodity into a trade good", ""}})
                    : gain ? 0
                           : 1;
            if (c == 0) {
                { const int cb_ = m.commodities; m.commodities = int8_t(m.commodities + 1); commodityGained(p, cb_); }
            } else {
                m.commodities = int8_t(m.commodities - 1);
                // (fixes 27, the Mentak review) a commodity turned into a trade good is a trade good gained (Pillage)
                if (G.fixes >= 27) gainTG(p, 1);
                else m.tradeGoods = int8_t(m.tradeGoods + 1);
            }
        }
        say(p, pn(p) + "'s Crimson Legionnaires leave commodities behind.");
        if (!std::strcmp(unitDef(G, p, UnitType::Infantry).key, "mahact_infantry2")) m.infantryOnCard = int8_t(m.infantryOnCard + k);
    }
    // Vuil'raith captures
    const int c = playerOfFaction(G, "cabal");
    if (c < 0 || isStructure(t)) return;
    if (sys >= 0 && flagshipIn(G, c, sys) && !(p == c && t == UnitType::Flagship)) {
        capture(c, p, t, k, "The Terror Between");
        return;
    }
    // (0.44.9, fixes 13 - the owner's answer B9) a Nekro flagship with The Terror Between's ability (Valefar Z)
    if (G.fixes >= 13 && sys >= 0)
        for (int z = 0; z < G.numPlayers; ++z)
            if (z != c && flagshipAbility(G, z, "cabal") && flagshipIn(G, z, sys) && !(p == z && t == UnitType::Flagship)) {
                capture(z, p, t, k, "The Terror Between");
                return;
            }
    if (p == c && t == UnitType::Infantry && planet >= 0 && G.planets[planet].units[c].count(UnitType::Mech) && mechAbilities(G)) {
        capture(c, c, t, k, "Reanimator");
        return;
    }
    if (p != c) {
        const bool combat = (inSpaceCombat_ && planet < 0 && sys == combatSys_) || (inGroundCombat_ && planet >= 0 && planet == G.groundCombatPlanet);
        if (combat && (combatSide_[0] == c || combatSide_[1] == c) && (combatSide_[0] == p || combatSide_[1] == p)) capture(c, p, t, k, "Devour");
    }
}

// ------------------------------------------------------------------ combat
void Rules::combatMods4(int p, int opp, int sys, int planet, bool space, RollMods& m) {
    // Arvicon Rex (Mahact flagship): +2 against an opponent whose token isn't in the fleet pool
    if (space && opp >= 0 && flagshipAbility(G, p, "mahact") && flagshipIn(G, p, sys) && !((G.players[p].edict >> opp) & 1))
        m.perType[int(UnitType::Flagship)] += 2;
    // Visz El Vir (Naaz-Rokha flagship): mechs in its system roll 1 extra die
    if (flagshipAbility(G, p, "naaz") && flagshipIn(G, p, sys)) {
        int mechs = space ? (zgrav_[p] ? G.systems[sys].space[p].count(UnitType::Mech) : 0)
                          : (planet >= 0 ? G.planets[planet].units[p].count(UnitType::Mech) : 0);
        m.extraDice[int(UnitType::Mech)] += mechs;
    }
    // Z-Grav Eidolon: combat 8 in space (Eidolon Maximum, Thunder's Edge: always combat 4)
    if (space && zgrav_[p] && p < G.numPlayers && !G.players[size_t(p)].eidolonMax) m.perType[int(UnitType::Mech)] -= 2;
}

void Rules::combatRoundStart4(int att, int def, int sys, int planet, bool space, RollMods mods[2]) {
    (void)planet;
    const int side[2] = {att, def};
    for (int i = 0; i < 2; ++i) {
        const int p = side[i];
        // Supercharge (Naaz-Rokha)
        if (!techReady(p, "sc")) continue;
        if (!fyes(p, Ab::Supercharge, "Exhaust Supercharge: +1 to all your combat rolls this round?", space ? "Space combat." : "Ground combat."))
            continue;
        exhaustTech(p, "sc");
        mods[i].all(1);
        say(p, pn(p) + " supercharges their units (+1 this round).", sys);
    }
}

bool Rules::thundarian(int att, int def, int sys, int hitsA, int hitsD) {
    for (int i = 0; i < 2; ++i) {
        const int p = i == 0 ? att : def;
        if (!canUseNomadAgent(p, 1)) continue;
        const int mine = i == 0 ? hitsA : hitsD, theirs = i == 0 ? hitsD : hitsA;
        curValue_ = mine * 100 + theirs;
        bool use = fyes(p, Ab::NomadThundarian, "Exhaust The Thundarian: cancel these hits and roll this round again?",
                        "You rolled " + std::to_string(mine) + " hit" + (mine == 1 ? "" : "s") + ", your opponent " + std::to_string(theirs) + ".");
        curValue_ = 0;
        if (!use) continue;
        spendNomadAgent(p, 1);
        abilityEvent4(E, p, "The Thundarian", pn(p) + " calls on The Thundarian: no hits; both sides roll again.", sys);
        return true;
    }
    // (fixes 26, the Nomad review - owner 2026-11-13) "After the "Roll Dice" step of combat" - any combat: the Nomad may use it
    // in a combat they aren't in (a person is asked, or "don't ask again this round"; a bot only when it promised one of the
    // two players its agent in a deal)
    if (G.fixes < 26) return false;
    for (int o : windowOrder()) {
        if (o == att || o == def || o >= G.numPlayers || !canUseNomadAgent(o, 1)) continue;
        const int ben = G.agentFor[o] == att ? att : G.agentFor[o] == def ? def : -1;
        const bool human = G.players[size_t(o)].human;
        if (ben < 0 && !(human && thundarianQuiet_[o] != G.round)) continue;
        const int b = ben >= 0 ? ben : att;
        agentFor(o, b);
        const int mine = b == att ? hitsA : hitsD, theirs = b == att ? hitsD : hitsA;
        curValue_ = mine * 100 + theirs;
        std::vector<Option> opts = {{1, "Yes", ""}, {0, "No", ""}};
        if (human && ben < 0) opts.push_back({3, "No, and don't ask again this round", "You'll be asked again next round."});
        const int c = fpick(o, Ab::NomadThundarian, "Exhaust The Thundarian: cancel these hits and roll this round again?", opts,
                            pn(att) + " rolled " + std::to_string(hitsA) + " hit" + (hitsA == 1 ? "" : "s") + ", " + pn(def) + " " +
                                std::to_string(hitsD) + ".");
        curValue_ = 0;
        curFor_ = -1;
        if (c == 3) thundarianQuiet_[o] = int16_t(G.round);
        if (c != 1) continue;
        spendNomadAgent(o, 1);
        if (ben >= 0) agentDone(o, ben);
        abilityEvent4(E, o, "The Thundarian", pn(o) + " calls on The Thundarian: no hits; both sides roll again.", sys);
        return true;
    }
    return false;
}

int Rules::spaceHitsCancel(int victim, int side, int sys, int hits, int attacker) {
    if (hits <= 0) return hits;
    Force& f = G.systems[sys].space[victim];
    // (fixes 26, the Nomad review - owner 2026-11-13: real Sustain Damage) The Cavalry: "treat 1 of your non-fighter ships as if
    // it has the SUSTAIN DAMAGE ability ... of the Nomad's flagship" - a ship that can't sustain does so once: it becomes
    // damaged (again only once repaired); a ship that already sustains gains nothing. Then Farran / Dynamo / Direct Hit as
    // for any sustain.
    if (G.fixes >= 26) {
        const int t = side >= 0 && side < 2 ? cavalry_[side] : -1;
        // (fixes 27, the Mentak review) not where Sustain Damage can't be used: the Fourth Moon's system, an entropic scar
        if (t >= 0 && f.count(UnitType(t)) > 0 && f.dmg[t] == 0 && !unitSustains(G, victim, UnitType(t)) &&
            !(G.fixes >= 27 && sustainBlockedAt(G, victim, UnitType(t), sys, -1))) {
            curValue_ = hits;
            const bool use = fyes(victim, Ab::Cavalry, std::string("The Cavalry: your ") + unitTypeName(UnitType(t)) + " uses Sustain Damage to cancel 1 hit?",
                                  "It becomes damaged (the Nomad flagship's Sustain Damage).");
            curValue_ = 0;
            if (use) {
                f.dmg[t] = uint8_t(f.dmg[t] + 1);
                --hits;
                Event e;
                e.kind = EventKind::Damaged;
                e.player = int8_t(victim);
                e.other = int8_t(attacker);
                e.system = int16_t(sys);
                e.planet = -1;
                e.unit = int8_t(t);
                e.count = 1;
                e.text = pn(victim) + "'s " + unitLabel(G, victim, UnitType(t)) + " sustains damage (The Cavalry).";
                E.emit(e);
                sustained(victim, attacker, sys, -1, t);
                if (f.dmg[t] > 0) {  // Direct Hit / Reflective Shielding: after a ship uses Sustain Damage
                    WinCtx w;
                    w.w = Win::AfterSustain;
                    w.a = victim;
                    w.b = attacker;
                    w.sys = sys;
                    w.planet = -1;
                    w.value = t;
                    w.space = true;
                    window(w);
                }
            }
        }
        return quantumManipulator(victim, sys, hits, false);
    }
    // The Cavalry: the chosen ship can absorb 1 hit, like Sustain Damage
    if (side >= 0 && side < 2 && cavalry_[side] >= 0 && cavalrySustain_[side] && f.count(UnitType(cavalry_[side])) > 0) {
        curValue_ = hits;
        bool use = fyes(victim, Ab::Cavalry, std::string("The Cavalry: your ") + unitTypeName(UnitType(cavalry_[side])) + " absorbs 1 hit?",
                        "Once in this combat.");
        curValue_ = 0;
        if (use) {
            cavalrySustain_[side] = false;
            --hits;
            say(victim, pn(victim) + "'s Cavalry ship shrugs off a hit.", sys);
        }
    }
    return quantumManipulator(victim, sys, hits, false);
}

// Quantum Manipulator (Nomad mech): mechs in space use Sustain Damage to cancel hits on your ships. (fixes 26, the Nomad review
// - owner 2026-11-13: "during combat" - also against anti-fighter barrage, a step of space combat: afb)
int Rules::quantumManipulator(int victim, int sys, int hits, bool afb) {
    Force& f = G.systems[sys].space[victim];
    if (hits > 0 && fac(victim, "nomad") && unitSustainsAt(G, victim, UnitType::Mech, sys, -1) && mechAbilities(G)) {
        const int ready = f.count(UnitType::Mech) - f.damaged(UnitType::Mech);
        if (ready > 0) {
            std::vector<Option> opts;
            for (int k = 0; k <= std::min(ready, hits); ++k)
                opts.push_back({k, k ? "Cancel " + std::to_string(k) + " hit" + (k > 1 ? "s" : "") : std::string("None"), ""});
            curValue_ = hits;
            int k = fpick(victim, Ab::QuantumManipulator,
                          "Quantum Manipulator: your mechs cancel how many of these " + std::to_string(hits) + (afb ? " anti-fighter barrage" : "") + " hits?",
                          opts, "Each one that does becomes damaged.");
            curValue_ = 0;
            if (k > 0) {
                f.dmg[int(UnitType::Mech)] = uint8_t(f.dmg[int(UnitType::Mech)] + k);
                hits -= k;
                say(victim, pn(victim) + "'s Quantum Manipulators cancel " + std::to_string(k) + " hit" + (k > 1 ? "s" : "") + ".", sys);
            }
        }
    }
    return hits;
}

void Rules::cavalryDice(int side, int p, RollResult& r, RollKind kind) {
    const int t = side >= 0 && side < 2 ? cavalry_[side] : -1;
    const int n = playerOfFaction(G, "nomad");
    if (t < 0 || n < 0) return;
    const UnitDef& fs = unitDef(G, n, UnitType::Flagship);
    const UnitDef& own = unitDef(G, p, UnitType(t));
    const bool combat = kind == RollKind::Combat;
    const int ownValue = combat ? own.combat : own.afb;
    const int ownDice = ownValue > 0 ? std::max<int>(1, combat ? own.combatDice : own.afbDice) : 0;
    const int fsValue = combat ? fs.combat : fs.afb;
    const int fsDice = std::max<int>(1, combat ? fs.combatDice : fs.afbDice);
    if (fsValue <= 0) return;
    // that ship's own dice are replaced by the Nomad flagship's
    int removed = 0, offset = 0;
    for (size_t i = 0; i < r.dice.size() && removed < ownDice;) {
        if (r.unit[i] == t) {
            offset = ownValue - r.need[i];
            r.dice.erase(r.dice.begin() + long(i));
            r.need.erase(r.need.begin() + long(i));
            r.unit.erase(r.unit.begin() + long(i));
            if (i < r.bonus9.size()) r.bonus9.erase(r.bonus9.begin() + long(i));
            ++removed;
        } else {
            ++i;
        }
    }
    if (combat && removed == 0) return;  // the ship isn't fighting any more
    addDice(r, fsDice, fsValue - offset, t);
}

void Rules::zgravFlip(int p, int sys) {
    // Eidolon flips to Z-Grav Eidolon in the space area of the active system
    zgrav_[p] = fac(p, "naaz") && sys == G.activeSystem && G.systems[sys].space[p].count(UnitType::Mech) > 0 && mechAbilities(G);
    if (zgrav_[p]) say(p, pn(p) + "'s Eidolons flip: Z-Grav Eidolons fight as ships.", sys);
    // Eidolon Maximum (Naaz-Rokha breakthrough): a ship as well as a ground force, wherever it is
    if (p < G.numPlayers && G.players[size_t(p)].eidolonMax && breakthroughIs(G, p, "naaz") && G.systems[sys].space[p].count(UnitType::Mech) > 0)
        zgrav_[p] = true;
    // (fixes 27) for the query layer (unitSustainsAt: the Z-Grav side has no Sustain Damage)
    if (zgrav_[p]) G.zgrav = uint16_t(G.zgrav | (1u << p));
    else G.zgrav = uint16_t(G.zgrav & ~(1u << p));
}

void Rules::spaceCombatStart4(int att, int def, int sys) {
    const int side[2] = {att, def};
    if (G.fixes < 13)  // (0.44.9, fixes 13: flipped before Assault Cannon - spaceCombat)
        for (int p : side) zgravFlip(p, sys);
    cavalry_[0] = cavalry_[1] = -1;
    cavalrySustain_[0] = cavalrySustain_[1] = false;
    const int n = playerOfFaction(G, "nomad");
    if (n < 0 || att == n || def == n) return;
    for (int i = 0; i < 2; ++i) {
        const int p = side[i];
        if (!holdsNote(G, p, n, kNoteFaction)) continue;
        std::vector<Option> opts;
        for (int t : kShipIdx)
            if (t != int(UnitType::Fighter) && G.systems[sys].space[p].n[t]) opts.push_back({t, unitTypeName(UnitType(t)), ""});
        if (opts.empty()) continue;
        opts.push_back({-1, "Don't play it", ""});
        int t = fpick(p, Ab::Cavalry, "Play The Cavalry: which of your ships fights like the Nomad flagship?", opts,
                      factionNoteText(G.players[n].faction, kNoteFaction));
        if (t < 0) continue;
        cavalry_[i] = t;
        cavalrySustain_[i] = true;
        abilityEvent4(E, p, "The Cavalry", pn(p) + " plays The Cavalry: a " + unitTypeName(UnitType(t)) + " fights like the Nomad flagship.", sys);
    }
}

void Rules::spaceCombatEnd4(int att, int def, int sys) {
    (void)sys;
    zgrav_[att] = zgrav_[def] = false;
    G.zgrav = 0;
    const int n = playerOfFaction(G, "nomad");
    const int side[2] = {att, def};
    for (int i = 0; i < 2; ++i)
        if (cavalry_[i] >= 0 && n >= 0 && holdsNote(G, side[i], n, kNoteFaction)) returnNote(side[i], makeNote(n, kNoteFaction));
    cavalry_[0] = cavalry_[1] = -1;
    noRetreat_ = false;
}

int Rules::tellurian(int victim, int sys, int planet, int hits, int mask) {
    // Tellurian (Titans agent): cancel a hit before it is assigned
    if (G.fixes >= 27) return tellurian27(victim, sys, planet, hits, mask);
    const int u = agentUser(victim, "titans");  // or a player who promised its agent in a deal
    if (hits <= 0 || u < 0 || maxAbsorbableHits(G, victim, sys, planet, mask) <= 0) return hits;
    curValue_ = hits;
    agentFor(u, victim);
    bool use = fyes(u, Ab::TitansAgent,
                    (u == victim ? std::string("Exhaust Tellurian to cancel 1 of these ") : "Exhaust Tellurian for " + pn(victim) + " to cancel 1 of these ") +
                        std::to_string(hits) + " hit" + (hits > 1 ? "s" : "") + "?",
                    planet >= 0 ? pn(victim) + "'s units on " + planetName(planet) + "." : pn(victim) + "'s units in " + sysName(sys) + ".");
    curValue_ = 0;
    curFor_ = -1;
    if (!use) return hits;
    spendAgent(u, "titans");
    agentDone(u, victim);
    say(victim, pn(victim) + " cancels a hit (Tellurian).", sys);
    return hits - 1;
}

int Rules::tellurian27(int victim, int sys, int planet, int hits, int mask) {
    // (fixes 27, the Titans review - owner 2026-11-13: "Before a hit would be assigned: You may exhaust this card to cancel
    // that hit" - the errata, any player's unit; the Titans decide: a person is asked (tellurianQuiet_), a bot only when it
    // promised the agent)
    if (hits <= 0 || victim < 0 || victim >= G.numPlayers || maxAbsorbableHits(G, victim, sys, planet, mask) <= 0) return hits;
    const std::string where = planet >= 0 ? pn(victim) + "'s units on " + planetName(planet) + "." : pn(victim) + "'s units in " + sysName(sys) + ".";
    const std::string many = std::to_string(hits) + " hit" + (hits > 1 ? "s" : "");
    int u = -1;
    curValue_ = hits;
    if (canUseAgent(victim, "titans")) {
        if (fyes(victim, Ab::TitansAgent, "Exhaust Tellurian to cancel 1 of these " + many + "?", where)) u = victim;
    } else {
        for (int o : initiativeOrder(G)) {
            if (o == victim || !canUseAgent(o, "titans")) continue;
            const bool promised = G.agentFor[o] == victim;
            const bool human = G.players[size_t(o)].human;
            if (!promised && !(human && tellurianQuiet_[o] != G.round)) continue;
            agentFor(o, victim);
            std::vector<Option> yn = {{1, "Yes", ""}, {0, "No", ""}};
            if (human && !promised) yn.push_back({3, "No, and don't ask again this round", "You'll be asked again next round."});
            const int c = fpick(o, Ab::TitansAgent, "Exhaust Tellurian for " + pn(victim) + " to cancel 1 of these " + many + "?", yn,
                                where + " " + leaderText(findLeader("titansagent")));
            curFor_ = -1;
            if (c == 3) tellurianQuiet_[o] = int16_t(G.round);
            if (c != 1) continue;
            u = o;
            break;
        }
    }
    curValue_ = 0;
    if (u < 0) return hits;
    spendAgent(u, "titans");
    agentDone(u, victim);
    say(victim, pn(victim) + " cancels a hit (Tellurian).", sys);
    return hits - 1;
}

// ------------------------------------------------------------------ production
void Rules::afterProduction4(int p, int sys, const Force& made, int gfPlanet) {
    // Tungstantus (Titans commander)
    if (commanderUsable(G, p, "titans")) {
        say(p, pn(p) + " gains 1 trade good (Tungstantus).");
        gainTG(p, 1);
    }
    // Trilossa Aun Mirik (Argent agent): new ground forces to your planets here or next door
    // (fixes 26, the Argent review - owner 2026-11-13: "When a player produces ground forces in a system" - any player's
    // production; the Argent decide: a person is asked (trilossaQuiet_), a bot only when it promised the agent)
    if (made.groundForces() == 0 || (G.fixes < 26 && !canUseAgent(p, "argent"))) return;
    std::vector<Option> dest;
    std::vector<int> near = {sys};
    for (int16_t a : G.systems[sys].adj) near.push_back(a);
    for (int s : near)
        for (int16_t x : G.systems[s].planets)
            if (x != gfPlanet && G.planets[x].owner == p && !isDMZ(G, x) && !G.planets[x].destroyed) dest.push_back({x, planetName(x), ""});
    if (dest.empty()) return;
    int user = -1;
    if (canUseAgent(p, "argent")) {
        if (fyes(p, Ab::ArgentAgent, "Exhaust Trilossa Aun Mirik to place the new ground forces on your planets nearby?",
                 leaderText(findLeader("argentagent"))))
            user = p;
    } else {
        for (int o : initiativeOrder(G)) {
            if (o == p || !canUseAgent(o, "argent")) continue;
            const bool promised = G.agentFor[o] == p;
            const bool human = G.players[size_t(o)].human;
            if (!promised && !(human && trilossaQuiet_[o] != G.round)) continue;
            agentFor(o, p);
            std::vector<Option> opts = {{1, "Yes", ""}, {0, "No", ""}};
            if (human && !promised) opts.push_back({3, "No, and don't ask again this round", "You'll be asked again next round."});
            const int c = fpick(o, Ab::ArgentAgent, "Exhaust Trilossa Aun Mirik: " + pn(p) + " may place the new ground forces on their planets in " +
                                                     sysName(sys) + " and the systems next to it?",
                                opts, leaderText(findLeader("argentagent")));
            curFor_ = -1;
            if (c == 3) trilossaQuiet_[o] = int16_t(G.round);
            if (c != 1) continue;
            user = o;
            break;
        }
    }
    if (user < 0) return;
    spendAgent(user, "argent");
    if (user != p) {
        agentDone(user, p);
        say(p, pn(p) + " may place the new ground forces nearby (" + pn(user) + "'s Trilossa Aun Mirik).", sys);
    }
    Force& src = gfPlanet >= 0 ? G.planets[gfPlanet].units[p] : G.systems[sys].space[p];
    int left[2] = {std::min<int>(made.count(UnitType::Infantry), src.count(UnitType::Infantry)),
                   std::min<int>(made.count(UnitType::Mech), src.count(UnitType::Mech))};
    dest.push_back({-1, "Leave the rest", ""});
    for (int guard = 0; guard < 12 && left[0] + left[1] > 0; ++guard) {
        int x = fpick(p, Ab::ArgentAgent, "Trilossa Aun Mirik: move new ground forces to which planet?", dest, "", false, true);
        if (x < 0) break;
        std::vector<Option> what;
        for (int i = 0; i < 2; ++i) {
            UnitType t = i == 0 ? UnitType::Infantry : UnitType::Mech;
            for (int k = 1; k <= left[i]; ++k) what.push_back({int(t) * 16 + k, count(k, t), ""});
        }
        int c = what.size() == 1 ? what[0].id : fpick(p, Ab::ArgentAgent, "Trilossa Aun Mirik: how many go to " + planetName(x) + "?", what);
        UnitType t = UnitType(c / 16);
        int k = c % 16;
        src.remove(t, k, false);
        G.planets[x].units[p].add(t, k);
        left[t == UnitType::Infantry ? 0 : 1] -= k;
        say(p, pn(p) + " places " + count(k, t) + " on " + planetName(x) + " (Trilossa Aun Mirik).", G.planets[x].system);
    }
    removeExcessCapacity(p, sys);
}

int Rules::amalgamation(int p, int sys, const int counts[kUnitTypes]) {
    // Amalgamation (Vuil'raith): give back captured units to produce units of that type for free
    if (!fac(p, "cabal") || G.players[p].captured.empty()) return 0;
    int freeK[kUnitTypes] = {}, best[kUnitTypes] = {};
    bool any = false;
    for (int t = 0; t < kUnitTypes; ++t) {
        best[t] = std::min(counts[t], capturedCount(G, p, UnitType(t)));
        if (best[t] <= 0) continue;
        std::vector<Option> opts;
        for (int k = best[t]; k >= 0; --k)
            opts.push_back({k, k ? "Give back " + std::to_string(k) + " captured " + unitTypeName(UnitType(t), k > 1) : std::string("None"), ""});
        freeK[t] = fpick(p, Ab::Amalgamation, std::string("Amalgamation: produce ") + unitTypeName(UnitType(t), true) + " with captured units?", opts,
                         abilityText(G.players[p].faction, 1));
        if (freeK[t]) any = true;
    }
    int rest[kUnitTypes];
    for (int t = 0; t < kUnitTypes; ++t) rest[t] = counts[t] - freeK[t];
    // not enough to pay the rest: use every captured unit that helps
    if (productionCost(G, p, rest) > spendable(G, p, false))
        for (int t = 0; t < kUnitTypes; ++t) {
            freeK[t] = best[t];
            rest[t] = counts[t] - best[t];
            if (best[t]) any = true;
        }
    if (!any) return 0;
    for (int t = 0; t < kUnitTypes; ++t)
        for (int i = 0; i < freeK[t]; ++i) returnCaptured(p, UnitType(t));
    G.players[p].usedFactionAbility = true;
    say(p, pn(p) + " gives back captured units to produce for free (Amalgamation).", sys);
    return productionCost(G, p, counts) - productionCost(G, p, rest);
}

bool Rules::riftmeld(int p, int tech) {
    const int t = upgradeType(tech);
    if (t < 0 || !returnCaptured(p, UnitType(t))) return false;
    G.players[p].usedFactionAbility = true;
    say(p, pn(p) + " gives back a captured " + unitTypeName(UnitType(t)) + " to ignore every prerequisite (Riftmeld).");
    return true;
}

void Rules::replenished4(int p) {
    // The Stillness of Stars (Vuil'raith agent)
    const int c = playerOfFaction(G, "cabal");
    if (c < 0 || c == p || !canUseAgent(c, "cabal") || G.players[p].eliminated) return;
    Player& v = G.players[p];
    const int value = commodityValue(G, v);
    std::vector<Option> opts;
    for (int t = 0; t < kUnitTypes; ++t) {
        UnitType ut = UnitType(t);
        int c2 = unitCost2(G, p, ut);
        if (isStructure(ut) || c2 <= 0 || c2 > 2 * value || unitsAvailable(G, p, ut) <= 0) continue;
        opts.push_back({t, std::string("Capture a ") + unitTypeName(ut), std::to_string((c2 + 1) / 2) + " resources"});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "Don't use it", ""});
    int t = fpick(c, Ab::CabalAgent, "Exhaust The Stillness of Stars on " + pn(p) + "?", opts, leaderText(findLeader("cabalagent")));
    if (t < 0) return;
    spendAgent(c, "cabal");
    const int k = v.commodities;
    v.commodities = 0;
    if (G.fixes >= 27) {  // (fixes 27, the Mentak review) trade goods gained (Pillage), as every other conversion
        say(p, pn(p) + "'s " + std::to_string(k) + " commodities become trade goods (The Stillness of Stars).");
        gainTG(p, k);
    } else {
        v.tradeGoods = int8_t(v.tradeGoods + k);
        say(p, pn(p) + "'s " + std::to_string(k) + " commodities become trade goods (The Stillness of Stars).");
    }
    capture(c, p, UnitType(t), 1, "The Stillness of Stars");
}

// ------------------------------------------------------------------ notes
std::vector<Option> Rules::notesToGive(int from, int to) const {
    std::vector<Option> o;
    for (int16_t n : G.players[from].notes) {
        if (noteType(n) == kNoteAlliance && fac(to, "mahact")) continue;  // Hubris
        o.push_back({n, noteNameOf(G, n) + " (" + pn(noteOwner(n)) + ")", noteTextOf(G, n)});
    }
    return o;
}

// ------------------------------------------------------------------ component actions
std::vector<Option> Rules::factionActionOptions4(int p) {
    std::vector<Option> o;
    const Player& pl = G.players[p];
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    auto twoSame = [&]() {
        if (eventOn(G, kEvExploration) && fragmentCount(G, p) >= 2) return true;  // Age of Exploration: any 2
        for (int t = 0; t < 3; ++t)
            if (relicFragments(G, p, t) >= 2) return true;
        return false;
    };
    if (fac(p, "naaz") && (twoSame() || (fragmentCount(G, p) > 0 && reinforcementTokens(p) > 0)))
        add(Ab::Fabrication, "Fabrication", abilityText(pl.faction, 1));  // (the Naaz-Rokha review: the card's words)
    for (int16_t n : pl.notes) {
        const int owner = noteOwner(n);
        if (owner == p || noteType(n) != kNoteFaction) continue;
        if (isFaction(G, owner, "naaz") && twoSame())
            add(Ab::BlackMarketForgery, "Play Black Market Forgery", factionNoteText(G.players[owner].faction, kNoteFaction));
        if (isFaction(G, owner, "titans")) {
            bool any = false;
            for (int x : controlledPlanets(G, p))
                if (!isHomePlanet(G, x) && std::strcmp(kPlanets[G.planets[x].def].key, "mr") != 0 && !G.planets[x].terraform) any = true;
            if (any) add(Ab::Terraform, "Play Terraform", factionNoteText(G.players[owner].faction, kNoteFaction));
        }
    }
    if (techReady(p, "vtx")) {
        bool any = false;
        for (size_t s = 0; s < G.systems.size() && !any; ++s) {
            if (!hasDock(G, p, int(s))) continue;
            for (int16_t a : G.systems[s].adj)
                for (int q = 0; q < G.numPlayers; ++q)
                    // (0.44.8, fixes 12: not from a player blockading one of our docks - LRR 17.6, the capture would fail)
                    if (q != p && hasUnits(G, q, a) && (G.fixes < 12 || !cannotCaptureFrom(p, q))) any = true;
        }
        if (any) add(Ab::Vortex, "Vortex", "Capture a unit from another player's reinforcements.");
    }
    if (heroUnlocked(G, p)) {
        if (fac(p, "argent")) {
            bool any = false;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (G.systems[s].hasToken(p) && !otherShipsIn(G, p, int(s))) any = true;
            if (any) add(Ab::ArgentHero, "Hero: Mirik Aun Sissiri", "Move any number of your ships from any systems to any number of other systems "
                                                                       "that contain 1 of your command tokens and no other players' ships. Then, purge "
                                                                       "this card.");
        }
        if (fac(p, "mahact")) add(Ab::MahactHero, "Hero: Airo Shir Aur", "Throw a fleet into an adjacent system's ships: space combat, no retreats. Purged afterwards.");
        if (fac(p, "naaz")) add(Ab::NaazHero, "Hero: Hesh and Prit", leaderText(findLeader("naazhero")));
        // (fixes 26, the Nomad review - owner 2026-11-13: the card asks for no flagship on the board - one may be built later)
        if (fac(p, "nomad") && (G.fixes >= 26 || unitsOnBoard(G, p, UnitType::Flagship) > 0))
            add(Ab::NomadHero, "Hero: Ahk-Syl Siven",
                "Your flagship and units it transports can move out of systems that contain your command tokens during this game round. "
                "At the end of that game round, purge this card.");
        if (fac(p, "titans") && planetByKey(G, "elysium") >= 0 && !G.planets[planetByKey(G, "elysium")].destroyed)
            add(Ab::TitansHero, "Hero: Ul the Progenitor", leaderText(findLeader("titanshero")));  // (the Titans review: the card's words)
        if (fac(p, "cabal")) {
            bool tear = false;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (dimensionalTear(G, int(s))) tear = true;
            if (tear) add(Ab::CabalHero, "Hero: It Feeds on Carrion", "Capture other players' ships in and next to your Dimensional Tears (1-3). Purged afterwards.");
        }
    }
    for (const Option& x : factionActionOptions5(p)) o.push_back(x);
    for (const Option& x : factionActionOptions6(p)) o.push_back(x);  // Thunder's Edge factions
    for (const Option& x : factionActionOptions7(p)) o.push_back(x);
    for (const Option& x : factionActionOptions8(p)) o.push_back(x);
    for (const Option& x : factionActionOptions9(p)) o.push_back(x);
    if (G.modules & kModDiscordant) dsOptions(p, o);  // Discordant Stars (0.37.0)
    eventOptions(p, o);  // galactic events (0.36.0)
    return o;
}

bool Rules::factionAction4(int p, Ab ab) {
    Player& pl = G.players[p];
    switch (ab) {
        case Ab::Fabrication:
        case Ab::BlackMarketForgery: {
            std::vector<Option> opts;
            for (int t = 0; t < 3; ++t)
                if (relicFragments(G, p, t) >= 2)
                    opts.push_back({t, std::string("Purge 2 ") + exploreTypeName(ExploreType(t)) + " fragments", "Gain a relic."});
            if (eventOn(G, kEvExploration) && opts.empty() && fragmentCount(G, p) >= 2)  // Age of Exploration: any 2
                opts.push_back({3, "Purge any 2 fragments", "Gain a relic (Age of Exploration: they need not match)."});
            if (ab == Ab::Fabrication && fragmentCount(G, p) > 0 && reinforcementTokens(p) > 0)
                opts.push_back({10, "Purge 1 fragment", "Gain a command token."});
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            int c = fpick(p, ab, ab == Ab::Fabrication ? "Fabrication: what do you make?" : "Black Market Forgery: purge which fragments?", opts);
            if (c < 0) return false;
            if (c == 10) {
                purgeFragments(p, 1, -1, "Fabrication");
                gainTokens(p, 1, false, "Fabrication: a command token.");
            } else {
                purgeFragments(p, 2, c == 3 ? -1 : c, ab == Ab::Fabrication ? "Fabrication" : "Black Market Forgery");
                gainRelic(p, ab == Ab::Fabrication ? "Fabrication" : "Black Market Forgery");
            }
            if (ab == Ab::BlackMarketForgery) {
                const int n = playerOfFaction(G, "naaz");
                if (n >= 0) returnNote(p, makeNote(n, kNoteFaction));
            } else {
                pl.usedFactionAbility = true;
            }
            return true;
        }
        case Ab::Terraform: {
            const int t = playerOfFaction(G, "titans");
            if (t < 0 || !holdsNote(G, p, t, kNoteFaction)) return false;
            std::vector<Option> opts;
            for (int x : controlledPlanets(G, p))
                if (!isHomePlanet(G, x) && std::strcmp(kPlanets[G.planets[x].def].key, "mr") != 0 && !G.planets[x].terraform && !G.planets[x].destroyed)
                    opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + " res / " + std::to_string(planetInfluence(G, x)) + " inf"});
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            int x = fpick(p, Ab::Terraform, "Terraform: attach it to which planet?", opts, factionNoteText(G.players[t].faction, kNoteFaction), false, true);
            if (x < 0) return false;
            auto& h = pl.notes;
            h.erase(std::find(h.begin(), h.end(), int16_t(makeNote(t, kNoteFaction))));  // attached: it stays with the planet
            G.planets[x].terraform = true;
            abilityEvent4(E, p, "Terraform", pn(p) + " terraforms " + planetName(x) + ".", G.planets[x].system);
            return true;
        }
        case Ab::Vortex: {
            std::vector<Option> opts;
            auto listed = [&](int id) {
                for (const Option& o : opts)
                    if (o.id == id) return true;
                return false;
            };
            for (size_t s = 0; s < G.systems.size(); ++s) {
                if (!hasDock(G, p, int(s))) continue;
                for (int16_t a : G.systems[s].adj)
                    for (int q = 0; q < G.numPlayers; ++q) {
                        if (q == p || (G.fixes >= 12 && cannotCaptureFrom(p, q))) continue;  // (0.44.8: LRR 17.6)
                        Force f = G.systems[a].space[q];
                        for (int16_t x : G.systems[a].planets)
                            for (int t = 0; t < kUnitTypes; ++t) f.n[t] = uint8_t(f.n[t] + G.planets[x].units[q].n[t]);
                        for (int t = 0; t < kUnitTypes; ++t) {
                            if (!f.n[t] || isStructure(UnitType(t)) || unitsAvailable(G, q, UnitType(t)) <= 0 || listed(q * 16 + t)) continue;
                            // (0.44.8) the Ghoti flagship can't be captured (Mobile Command)
                            if (G.fixes >= 12 && t == int(UnitType::Flagship) && (G.modules & kModDiscordant) && fac(q, "ghoti")) continue;
                            opts.push_back({q * 16 + t, pn(q) + "'s " + unitTypeName(UnitType(t)),
                                            std::to_string((unitCost2(G, q, UnitType(t)) + 1) / 2) + " resources"});
                        }
                    }
            }
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            int c = fpick(p, Ab::Vortex, "Vortex: capture which unit from its owner's reinforcements?", opts, techSummary(findTech("vtx")));
            if (c < 0) return false;
            exhaustTech(p, "vtx");
            capture(p, c / 16, UnitType(c % 16), 1, "Vortex");
            return true;
        }
        case Ab::ArgentHero: {
            int moves = 0;
            for (int guard = 0; guard < 6; ++guard) {
                std::vector<Option> dests;
                for (size_t s = 0; s < G.systems.size(); ++s)
                    if (G.systems[s].hasToken(p) && !otherShipsIn(G, p, int(s))) dests.push_back({int(s), sysName(int(s)), ""});
                if (dests.empty()) break;
                dests.push_back({-1, moves ? "Done" : "Cancel", ""});
                int to = fpick(p, Ab::ArgentHero, "Mirik Aun Sissiri: move ships to which system?", dests,
                               "A system with your command token and no other players' ships.", true);
                if (to < 0) break;
                std::vector<Option> groups;
                for (size_t s = 0; s < G.systems.size(); ++s) {
                    if (int(s) == to) continue;
                    const Force& f = G.systems[s].space[p];
                    for (int t : kShipIdx)
                        if (f.n[t]) groups.push_back({int(s) * 16 + t, count(f.n[t], UnitType(t)) + " from " + sysName(int(s)), ""});
                }
                if (groups.empty()) break;
                std::vector<int> picks = fpickMany(p, Ab::ArgentHero, "Mirik Aun Sissiri: which ships go to " + sysName(to) + "?", groups, 0,
                                                   int(groups.size()));
                if (picks.empty()) continue;
                if (!moves) {
                    // (fixes 26, the Argent review - owner 2026-11-13: "Then, purge this card" - purged after the moves)
                    if (G.fixes < 26) purgeHero(p);
                    heroEvent4(E, G, p, pn(p) + " uses Mirik Aun Sissiri: the flight regroups.");
                }
                std::vector<int> from;
                std::vector<int> room(G.systems.size(), 0);  // (fixes 26) capacity moved out of each system
                for (int id : picks) {
                    const int s = id / 16, t = id % 16;
                    Force& src = G.systems[s].space[p];
                    Force& dst = G.systems[to].space[p];
                    room[size_t(s)] += src.n[t] * unitDef(G, p, UnitType(t)).capacity;
                    dst.n[t] = uint8_t(dst.n[t] + src.n[t]);
                    dst.dmg[t] = uint8_t(dst.dmg[t] + src.dmg[t]);
                    src.n[t] = src.dmg[t] = 0;
                    if (std::find(from.begin(), from.end(), s) == from.end()) from.push_back(s);
                    ++moves;
                }
                // (fixes 26, the Argent review - the card's note: the ships transport ground forces and fighters up to their
                // capacity) what the ships left behind have no room for goes along, up to the capacity that moved
                if (G.fixes >= 26)
                    for (int s : from)
                        for (UnitType u : {UnitType::Infantry, UnitType::Mech, UnitType::Fighter})
                            while (room[size_t(s)] > 0 && capacityOverflow(G, p, s) > 0 && G.systems[s].space[p].count(u) > 0) {
                                Force& src = G.systems[s].space[p];
                                const bool dmg = src.dmg[int(u)] > 0 && src.dmg[int(u)] >= src.n[int(u)];
                                src.remove(u, 1, dmg);
                                G.systems[to].space[p].add(u, 1);
                                if (dmg) G.systems[to].space[p].dmg[int(u)] = uint8_t(G.systems[to].space[p].dmg[int(u)] + 1);
                                --room[size_t(s)];
                            }
                // what the ships carried comes along when no carrier stays behind
                for (int s : from) {
                    if (G.fixes >= 26) break;
                    Force& src = G.systems[s].space[p];
                    if (shipCapacity(G, p, s) > 0) continue;
                    for (UnitType t : {UnitType::Fighter, UnitType::Infantry, UnitType::Mech}) {
                        Force& dst = G.systems[to].space[p];
                        dst.n[int(t)] = uint8_t(dst.n[int(t)] + src.n[int(t)]);
                        dst.dmg[int(t)] = uint8_t(dst.dmg[int(t)] + src.dmg[int(t)]);
                        src.n[int(t)] = src.dmg[int(t)] = 0;
                    }
                }
                for (int s : from) removeExcessCapacity(p, s);
                removeExcessCapacity(p, to);
                checkNexus(to);
                say(p, pn(p) + "'s ships gather in " + sysName(to) + " (Mirik Aun Sissiri).", to);
            }
            if (moves > 0 && G.fixes >= 26) purgeHero(p);
            return moves > 0;
        }
        case Ab::MahactHero: {
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                int u = -1;
                for (int q = 0; q < G.numPlayers; ++q)
                    if (hasShips(G, q, int(s))) u = q;
                if (u < 0) continue;
                for (int16_t a : G.systems[s].adj) {
                    if (G.systems[a].isHyperlane()) continue;
                    for (int v = 0; v < G.numPlayers; ++v)
                        if (v != u && hasShips(G, v, a)) {
                            opts.push_back({int(s) * 1000 + a, pn(u) + "'s fleet in " + sysName(int(s)) + " -> " + sysName(a),
                                            pn(v) + " has " + std::to_string(G.systems[a].space[v].ships()) + " ships there"});
                            break;
                        }
                }
            }
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            int c = fpick(p, Ab::MahactHero, "Airo Shir Aur: hurl which fleet at which system?", opts, leaderText(findLeader("mahacthero")));
            if (c < 0) return false;
            const int s = c / 1000, to = c % 1000;
            int u = -1, v = -1;
            for (int q = 0; q < G.numPlayers; ++q)
                if (hasShips(G, q, s)) u = q;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != u && hasShips(G, q, to)) v = q;
            if (u < 0 || v < 0) return false;
            purgeHero(p);
            heroEvent4(E, G, p, pn(p) + " uses Airo Shir Aur: " + pn(u) + "'s fleet is hurled into " + sysName(to) + ".");
            Force& src = G.systems[s].space[u];
            Force& dst = G.systems[to].space[u];
            for (int t = 0; t < kUnitTypes; ++t) {
                if (isStructure(UnitType(t))) continue;
                dst.n[t] = uint8_t(dst.n[t] + src.n[t]);
                dst.dmg[t] = uint8_t(dst.dmg[t] + src.dmg[t]);
                src.n[t] = src.dmg[t] = 0;
            }
            checkNexus(to);
            noRetreat_ = true;
            spaceCombat(u, v, to);
            noRetreat_ = false;
            checkFloatingFactories(to);
            for (int q = 0; q < G.numPlayers; ++q) removeExcessCapacity(q, to);
            checkEliminations();
            return true;
        }
        case Ab::NaazHero: {
            std::vector<Option> opts;
            for (int c = 0; c < kStrategyCards; ++c) {
                const int owner = G.cardOwner[c];
                if (owner >= 0 && ((G.players[owner].cardsUsed >> c) & 1)) continue;  // already used this round
                opts.push_back({c, strategyName(c), std::string(owner < 0 ? "Unchosen. " : "Ready. ") + strategySecondary(c)});
            }
            if (opts.empty()) return false;
            if (G.fixes < 27) purgeHero(p);
            heroEvent4(E, G, p, pn(p) + " uses Hesh and Prit.");
            gainRelic(p, "Hesh and Prit");
            std::vector<int> picks = fpickMany(p, Ab::NaazHero, "Hesh and Prit: use the secondary of which strategy cards? (up to 2)", opts, 0,
                                               std::min<int>(2, int(opts.size())), "Tokens come from your reinforcements.");
            const int prev = strategicPlayer_;
            strategicPlayer_ = -1;
            heshPrit_ = true;
            for (int c : picks) secondaryAbility(p, c);
            heshPrit_ = false;
            strategicPlayer_ = prev;
            // (fixes 27, the Naaz-Rokha review - owner 2026-11-13) "Then, purge this card" (LRR 1.17): after the secondaries
            if (G.fixes >= 27) purgeHero(p);
            return true;
        }
        case Ab::NomadHero: {
            const int i = leaderIndex(G, p, kHero);
            pl.leaders[i].status = kExhausted;  // in play until the end of the round, then purged
            pl.heroRound = int16_t(G.round);
            heroEvent4(E, G, p, pn(p) + " uses Ahk-Syl Siven: this round their flagship may leave systems with their command tokens.");
            return true;
        }
        case Ab::TitansHero: {
            const int x = planetByKey(G, "elysium");
            if (x < 0) return false;
            // (fixes 27, the Titans review - owner 2026-11-13) "attach this card to it": off the faction sheet, but not
            // purged - no "when a player purges" effects (Aggressive Broker, Soul Cage, Erasure Corps)
            if (G.fixes >= 27) {
                const int i = leaderIndex(G, p, kHero);
                if (i >= 0) pl.leaders[size_t(i)].status = kPurged;
            } else {
                purgeHero(p);
            }
            G.planets[x].exhausted = false;
            G.planets[x].ulHero = true;
            heroEvent4(E, G, p, pn(p) + " uses Ul the Progenitor: Elysium awakens (+3/+3, Space Cannon 5 x3).");
            return true;
        }
        case Ab::CabalHero: {
            purgeHero(p);
            heroEvent4(E, G, p, pn(p) + " uses It Feeds on Carrion.");
            std::vector<int> zone;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                if (!dimensionalTear(G, int(s))) continue;
                if (std::find(zone.begin(), zone.end(), int(s)) == zone.end()) zone.push_back(int(s));
                for (int16_t a : G.systems[s].adj)
                    if (std::find(zone.begin(), zone.end(), int(a)) == zone.end()) zone.push_back(a);
            }
            for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers)) {
                if (q == p) continue;
                for (int s : zone) {
                    Force& f = G.systems[s].space[q];
                    if (!f.nonFighterShips()) continue;
                    RollResult r;
                    int lost[kUnitTypes] = {};
                    for (int t : kShipIdx) {
                        if (t == int(UnitType::Fighter)) continue;
                        for (int i = 0; i < f.n[t]; ++i) {
                            int d = E.rng().d10();
                            r.dice.push_back(int8_t(d));
                            r.need.push_back(4);
                            r.unit.push_back(int8_t(t));
                            r.bonus9.push_back(0);
                            if (d <= 3) ++lost[t];
                        }
                    }
                    recount(r);
                    reportRoll(q, s, "It Feeds on Carrion (1-3 is captured)", r);
                    Force before = f;
                    removing_ = true;
                    for (int t = 0; t < kUnitTypes; ++t)
                        if (lost[t]) {
                            destroyUnits(q, UnitType(t), lost[t], s, -1, p, "It Feeds on Carrion");
                            capture(p, q, UnitType(t), lost[t], "It Feeds on Carrion");
                        }
                    removing_ = false;
                    Force mid = f;
                    removeExcessCapacity(q, s);
                    for (UnitType t : {UnitType::Fighter, UnitType::Infantry, UnitType::Mech})
                        if (mid.count(t) > f.count(t)) capture(p, q, t, mid.count(t) - f.count(t), "It Feeds on Carrion");
                    (void)before;
                }
            }
            checkEliminations();
            return true;
        }
        default: return factionAction5(p, ab);
    }
}

}  // namespace ti
