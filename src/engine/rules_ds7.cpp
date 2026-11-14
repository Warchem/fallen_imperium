// Discordant Stars batch 6, part 2 (0.42.0): the Mirveda Protectorate and the Ghoti Wayfarers. Called from the ds / ds5 /
// ds6 hooks. Texts in faction_text.cpp, the AI in ai/ai_ds.cpp.
//   Mirveda Protectorate - Privileged Citizenry (PDS per system = fleet tokens; their PDS sit in space: Gauss Cannon,
//                          spaceOnly - addUnits, placeStructure, query movesAsShip / forceCapacity, space cannon
//                          offense), Combat Drones (no infantry from docks; fighters become infantry at an invasion),
//                          Nexus, Gauss Cannons blockaded = destroyed (mirvedaBlockade), Orbital Defense Grid, Rapid
//                          Excavation, Logic Machina, Assault Machina, Wrath Machina, Javelin
//   Ghoti Wayfarers      - Abyssal Embrace / Mobile Command (the flagship's system is their home: ghotiSync), Spawning
//                          Grounds (their planet card: Player::dsGhotiExhausted, pay / spendable / votes), All Mother
//                          (query productionCapacity), Networked Command, Parallel Production, Ghoti Relay, Becece, Ceie
//                          Doleegueaunm (query routeNeighbors), Nmenmede, Tioleombp
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
constexpr int kShipMask7 = kShipMaskAll;  // Carrier .. WarSun, the second flagship
constexpr int kGroundMask7 = (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech));

void ds7Event(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    if (text.find(name) == std::string::npos) {  // the ability's name in the line (feed, game log, simulator counts)
        e.text = text.size() && text.back() == '.' ? text.substr(0, text.size() - 1) : text;
        e.text += " (" + name + ").";
    }
    e.system = int16_t(sys);
    E.emit(e);
}
int upgradesOwned(const Game& G, int p) {
    int n = 0;
    for (int16_t t : G.players[size_t(p)].techs) n += kTechs[t].unitUpgrade ? 1 : 0;
    return n;
}
}  // namespace

// ================================================================== Ghoti: Mobile Command
// The system with the Ghoti flagship is their home system (it stays where it last was when the flagship is gone).
void Rules::ghotiSync() {
    const int gh = playerOfFaction(G, "ghoti");
    if (gh < 0) return;
    Player& pl = G.players[size_t(gh)];
    int at = -1;
    for (size_t s = 0; s < G.systems.size() && at < 0; ++s)
        if (G.systems[s].space[size_t(gh)].count(UnitType::Flagship)) at = int(s);
    if (at < 0 || at == pl.homeSystem) return;
    if (pl.homeSystem >= 0 && G.systems[size_t(pl.homeSystem)].home == gh) G.systems[size_t(pl.homeSystem)].home = -1;
    if (G.systems[size_t(at)].home < 0) G.systems[size_t(at)].home = int8_t(gh);
    pl.homeSystem = int16_t(at);
}

// ================================================================== combat
void Rules::ds7CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m) {
    (void)opp;
    (void)sys;
    (void)planet;
    // Javelin (Mirveda mech): +1 for every 2 unit upgrades they own
    if (!space && fac(p, "mirveda") && mechAbilities(G)) m.perType[int(UnitType::Mech)] += upgradesOwned(G, p) / 2;
}

// After a round of space combat: Nexus (Mirveda flagship) - 1 fighter.
void Rules::ds7SpaceRoundEnd(int att, int def, int sys) {
    for (int side : {att, def}) {
        if (side >= G.numPlayers || !flagshipAbility(G, side, "mirveda") || !flagshipIn(G, side, sys) || unitsAvailable(G, side, UnitType::Fighter) <= 0) continue;
        addUnits(side, UnitType::Fighter, 1, sys, -1);
        ds7Event(E, side, "Nexus", pn(side) + "'s Nexus launches a fighter.", sys);
    }
}

// Gauss Cannons (Mirveda PDS in space) blockaded - other players' ships and none of theirs - are destroyed.
void Rules::mirvedaBlockade() {
    const int mv = playerOfFaction(G, "mirveda");
    if (mv < 0 || !unitDef(G, mv, UnitType::PDS).spaceOnly) return;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const int n = G.systems[s].space[size_t(mv)].count(UnitType::PDS);
        if (!n || G.systems[s].space[size_t(mv)].ships() > 0 || !otherShipsIn(G, mv, int(s))) continue;
        destroyUnits(mv, UnitType::PDS, n, int(s), -1, -1, "blockaded");
        ds7Event(E, mv, "Gauss Cannon", pn(mv) + "'s Gauss Cannons in " + sysName(int(s)) + " are blockaded and destroyed.", int(s));
    }
}

// Start of an invasion (the active player p in sys): Combat Drones (Mirveda attacking), Orbital Defense Grid (Mirveda defending).
void Rules::ds7InvasionStart(int p, int sys) {
    // Combat Drones: their fighters here become infantry
    if (fac(p, "mirveda")) {
        Force& f = G.systems[size_t(sys)].space[size_t(p)];
        const int k = std::min<int>(f.count(UnitType::Fighter), unitsAvailable(G, p, UnitType::Infantry));
        bool target = false;
        for (int16_t x : G.systems[size_t(sys)].planets) target = target || G.planets[size_t(x)].owner != p;
        if (k > 0 && target) {
            curValue_ = k;
            const bool use = fyes(p, Ab::DsCombatDrones, "Combat Drones: turn your " + std::to_string(k) + " fighters here into infantry?",
                                  "They don't need capacity during this invasion.");
            curValue_ = 0;
            if (use) {
                f.remove(UnitType::Fighter, k, false);
                f.add(UnitType::Infantry, k);
                ds7Event(E, p, "Combat Drones", pn(p) + "'s drones land as infantry: " + std::to_string(k) + ".", sys);
            }
        }
    }
    // Orbital Defense Grid: one of their units here gains Planetary Shield (their planet can't be bombarded)
    // (0.44.8, fixes 12: whoever owns it - a Nekro copy too; the first owner in order who has a planet here)
    int mv = playerOfFaction(G, "mirveda");
    if (G.fixes >= 12) {
        mv = -1;
        for (int q : windowOrder()) {
            if (q == p || q >= G.numPlayers || !owns(q, "dsmirvr")) continue;
            bool here = false;
            for (int16_t x : G.systems[size_t(sys)].planets) here = here || (G.planets[size_t(x)].owner == q && G.planets[size_t(x)].units[size_t(q)].total() > 0);
            if (here) {
                mv = q;
                break;
            }
        }
    }
    if (mv >= 0 && mv != p && owns(mv, "dsmirvr")) {
        std::vector<Option> opts;
        for (int16_t x : G.systems[size_t(sys)].planets)
            if (G.planets[size_t(x)].owner == mv && G.planets[size_t(x)].units[size_t(mv)].total() > 0) opts.push_back({x, planetName(x), ""});
        if (!opts.empty()) {
            const int x = opts.size() == 1 ? opts[0].id : fpick(mv, Ab::DsOrbitalShield, "Orbital Defense Grid: which planet's units gain Planetary Shield?", opts, "", false, true);
            G.fx.dsShieldPlanet = int16_t(x);
            ds7Event(E, mv, "Orbital Defense Grid", pn(mv) + " raises a shield over " + planetName(x) + ".", sys);
        }
    }
}

// Orbital Defense Grid (Mirveda): units land on one of their planets - exhaust it to destroy one of them.
void Rules::ds7Landed(int p, int sys, const std::vector<int>& landed) {
    int mv = playerOfFaction(G, "mirveda");
    if (G.fixes >= 12) {  // (0.44.8: an owner of a planet landed on - a Nekro copy too)
        mv = -1;
        for (int x : landed) {
            const int o = G.planets[size_t(x)].owner;
            if (o >= 0 && o < G.numPlayers && o != p && techReady(o, "dsmirvr")) mv = o;
        }
    }
    if (mv < 0 || mv == p || !techReady(mv, "dsmirvr")) return;
    for (int x : landed) {
        if (G.planets[size_t(x)].owner != mv) continue;
        Force& f = G.planets[size_t(x)].units[size_t(p)];
        const UnitType t = f.count(UnitType::Mech) ? UnitType::Mech : UnitType::Infantry;
        if (!f.count(t)) continue;
        if (!fyes(mv, Ab::DsOrbitalGrid, std::string("Orbital Defense Grid: exhaust it to destroy one of ") + pn(p) + "'s " + unitTypeName(t, true) + " landing on " + planetName(x) + "?", ""))
            continue;
        exhaustTech(mv, "dsmirvr");
        ds7Event(E, mv, "Orbital Defense Grid", pn(mv) + "'s defence grid shoots down a " + unitTypeName(t) + ".", sys);
        destroyUnits(p, t, 1, sys, x, mv, "Orbital Defense Grid");
        return;
    }
}

// ================================================================== tactical actions, production, turns
void Rules::ds7Activated(int p, int sys) {
    (void)sys;
    // Ceie Doleegueaunm (Ghoti commander): this action, systems without planets are next to each other
    if (commanderUsable(G, p, "ghoti")) G.fx.dsCeie = int8_t(p);
}

void Rules::ds7AfterMovement(int p, int sys) {
    ghotiSync();
    // the Ghoti planet card: ships into a system without planets - 1 commodity
    if (fac(p, "ghoti") && G.systems[size_t(sys)].planets.empty()) {
        Player& pl = G.players[size_t(p)];
        if (pl.commodities < commodityCap(G, pl)) {
            { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + 1); commodityGained(p, cb_); }
            ds7Event(E, p, "Spawning Grounds", pn(p) + " gains 1 commodity in the void.", sys);
        }
    }
}

void Rules::ds7EndTactical(int p, int sys) {
    (void)p;
    (void)sys;
    ghotiSync();
    mirvedaBlockade();
}

// After production: Parallel Production and Tioleombp (Ghoti).
void Rules::ds7Produced(int p, int sys, const Force& made) {
    // (0.44.5, fixes 9: Parallel Production also for a Nekro Valefar copy - only Tioleombp is the Ghoti's alone)
    if (p < 0 || p >= G.numPlayers || (!fac(p, "ghoti") && !(G.fixes >= 9 && owns(p, "dsghoty")))) return;
    Player& pl = G.players[size_t(p)];
    // Tioleombp (mech): 1 of the ships made at home goes to its system, if not blockaded
    if (fac(p, "ghoti") && sys == pl.homeSystem && made.ships() > 0 && mechAbilities(G)) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (int(s) == sys || otherShipsIn(G, p, int(s))) continue;
            bool mech = G.systems[s].space[size_t(p)].count(UnitType::Mech) > 0;
            for (int16_t x : G.systems[s].planets) mech = mech || G.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech) > 0;
            if (mech) opts.push_back({int(s), sysName(int(s)), ""});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Keep them together", ""});
            const int s = fpick(p, Ab::DsTioleombp, "Tioleombp: one of the new ships appears beside your mech - where?", opts, "", true);
            if (s >= 0) {
                Force& from = G.systems[size_t(sys)].space[size_t(p)];
                for (int t = int(UnitType::WarSun); t >= 0; --t)
                    if (t != int(UnitType::Fighter) && made.n[t] && from.n[t]) {
                        from.remove(UnitType(t), 1, false);
                        G.systems[size_t(s)].space[size_t(p)].add(UnitType(t), 1);
                        ds7Event(E, p, "Tioleombp", pn(p) + "'s " + unitTypeName(UnitType(t)) + " surfaces in " + sysName(s) + ".", s);
                        break;
                    }
            }
        }
    }
    // Parallel Production: 1 more unit in each of 2 systems with their ships and a command token
    if (owns(p, "dsghoty") && !inParallel_) {
        inParallel_ = true;
        std::vector<int> done;
        for (int round = 0; round < 2; ++round) {
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s)
                // (0.44.9, fixes 13 - the owner's answer C4: the system that just produced may be one of the two)
                if ((int(s) != sys || G.fixes >= 13) && hasShips(G, p, int(s)) && G.systems[s].hasToken(p) && !otherShipsIn(G, p, int(s)) &&
                    std::find(done.begin(), done.end(), int(s)) == done.end())
                    opts.push_back({int(s), sysName(int(s)), ""});
            if (opts.empty()) break;
            opts.push_back({-1, "No more", ""});
            const bool v9 = G.fixes >= 9;
            const int s = fpick(p, Ab::DsParallel, v9 ? "Parallel Production: 1 more unit in which system?" : "Parallel Production: 1 more ship in which system?",
                                opts, "", true);
            if (s < 0) break;
            done.push_back(s);
            ds7Event(E, p, "Parallel Production", pn(p) + " spawns in " + sysName(s) + ".", s);
            if (v9) {
                // 0.44.5: any 1 unit ("1 additional unit"), produced by this ability (no Sarween Tools), free of the
                // first production's own limits (Umbat's cost cap, Maximus, Obsessive Designs)
                const int mc = maxUnitCost_, cc = dsCostCap_, tm = dsTypeMask_;
                const bool na = notProductionAbility_;
                maxUnitCost_ = 0;
                dsCostCap_ = 0;
                dsTypeMask_ = 0;
                notProductionAbility_ = true;
                productionCore(p, s, 1, false, false);
                maxUnitCost_ = mc;
                dsCostCap_ = cc;
                dsTypeMask_ = tm;
                notProductionAbility_ = na;
            } else {
                productionCore(p, s, 1, false, true);
            }
        }
        inParallel_ = false;
    }
}

// Becece (Ghoti agent): a player's production - 2 more units, or 1 trade good.
int Rules::ds7ProductionBonus(int p, int sys) {
    const int user = agentUser(p, "ghoti");
    if (user < 0) return 0;
    agentFor(user, p);
    const int c = fpick(user, Ab::DsBecece, user == p ? "Exhaust Becece?" : "Exhaust Becece for " + pn(p) + "?",
                        {{1, "2 more units in this production", ""}, {2, "1 trade good", ""}, {-1, "Don't", ""}}, leaderText(findLeader("ghotiagent")));
    curFor_ = -1;
    if (c < 0) return 0;
    spendAgent(user, "ghoti");
    agentDone(user, p);
    if (c == 2) {
        gainTG(p, 1);
        ds7Event(E, p, "Becece", pn(p) + " gains 1 trade good from the caretaker.", sys);
        return 0;
    }
    ds7Event(E, p, "Becece", pn(p) + "'s production grows by 2 (Becece).", sys);
    return 2;
}

void Rules::ds7TurnStart(int p) {
    Player& pl = G.players[size_t(p)];
    // Ghoti Relay (the Ghoti's note): up to 2 ships with the holder's ships
    const int gh = playerOfFaction(G, "ghoti");
    if (gh >= 0 && gh != p && holdsNote(G, p, gh, kNoteFaction)) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (hasShips(G, p, int(s)) && !otherShipsIn(G, p, int(s))) opts.push_back({int(s), sysName(int(s)), ""});
        if (!opts.empty() && spendable(G, p, false) >= 2) {
            opts.push_back({-1, "Keep the note", ""});
            const int s = fpick(p, Ab::DsGhotiRelay, "Play " + pn(gh) + "'s Ghoti Relay: produce up to 2 ships where?", opts, "It goes back at the start of the status phase.", true);
            if (s >= 0) {
                pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), makeNote(gh, kNoteFaction)), pl.notes.end());
                pl.notesInPlay.push_back(makeNote(gh, kNoteFaction));
                ds7Event(E, p, "Ghoti Relay", pn(p) + " plays the Ghoti Relay in " + sysName(s) + ".", s);
                productionCore(p, s, 2, false, true);
            }
        }
    }
    // Rapid Excavation (the Mirveda's note): up to 2 of the holder's PDS to one of their planets
    const int mv = playerOfFaction(G, "mirveda");
    if (mv >= 0 && mv != p && holdsNote(G, p, mv, kNoteFaction) && !unitDef(G, p, UnitType::PDS).spaceOnly) {
        std::vector<int> from;
        for (int x : controlledPlanets(G, p))
            if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::PDS)) from.push_back(x);
        std::vector<Option> to;
        for (int x : controlledPlanets(G, p))
            if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::PDS) < 2 && !isDMZ(G, x)) to.push_back({x, planetName(x), ""});
        if (!from.empty() && !to.empty()) {
            to.push_back({-1, "Keep the note", ""});
            const int dest = fpick(p, Ab::DsExcavation, "Play " + pn(mv) + "'s Rapid Excavation: move up to 2 of your PDS to which planet?", to, "", false, true);
            if (dest >= 0) {
                returnNote(p, makeNote(mv, kNoteFaction));
                int moved = 0;
                for (int x : from)
                    while (moved < 2 && x != dest && G.planets[size_t(x)].units[size_t(p)].count(UnitType::PDS) &&
                           G.planets[size_t(dest)].units[size_t(p)].count(UnitType::PDS) < 2) {
                        G.planets[size_t(x)].units[size_t(p)].remove(UnitType::PDS, 1, false);
                        G.planets[size_t(dest)].units[size_t(p)].add(UnitType::PDS, 1);
                        ++moved;
                    }
                ds7Event(E, p, "Rapid Excavation", pn(p) + " moves " + std::to_string(moved) + " PDS to " + planetName(dest) + ".", G.planets[size_t(dest)].system);
            }
        }
    }
}

void Rules::ds7StatusStart() {
    // Ghoti Relay goes back
    const int gh = playerOfFaction(G, "ghoti");
    if (gh >= 0)
        for (int q = 0; q < G.numPlayers; ++q) {
            const int16_t note = makeNote(gh, kNoteFaction);
            auto& v = G.players[size_t(q)].notesInPlay;
            if (q != gh && std::find(v.begin(), v.end(), note) != v.end()) {
                quietNoteReturn_ = true;
                returnNote(q, note);
                quietNoteReturn_ = false;
            }
        }
    ghotiSync();
    mirvedaBlockade();
}

// Logic Machina (Mirveda agent): after a player gains a unit upgrade - a strategy token for a technology of one of its
// prerequisite colours.
void Rules::ds7Researched(int p, int t) {
    if (p < 0 || p >= G.numPlayers || t < 0 || !kTechs[t].unitUpgrade || inLogic_) return;
    Player& pl = G.players[size_t(p)];
    const int user = agentUser(p, "mirveda");
    if (user < 0 || pl.strategy <= 0) return;
    int colours = 0;
    for (int c = 0; c < 4; ++c)
        if (kTechs[t].req[c]) colours |= 1 << c;
    if (!colours) return;
    agentFor(user, p);
    const bool use = fyes(user, Ab::DsLogicMachina, user == p ? "Exhaust Logic Machina: spend a strategy token to research a technology of an upgrade's colour?"
                                                              : "Exhaust Logic Machina so " + pn(p) + " may research another technology?",
                          leaderText(findLeader("mirvedaagent")));
    curFor_ = -1;
    if (!use) return;
    spendAgent(user, "mirveda");
    agentDone(user, p);
    spendStrategy(p, 1);
    ds7Event(E, p, "Logic Machina", pn(p) + "'s weapon development opens a new line of research.");
    inLogic_ = true;
    onlyColours_ = colours;
    researchStep(p, 0, "Logic Machina");
    onlyColours_ = 0;
    inLogic_ = false;
}

// Votes: Networked Command (Ghoti) and their planet card.
int Rules::ds7VoteBonus(int p, const std::vector<int>& spent) {
    (void)spent;
    // (0.44.6, fixes 10: the technology for anyone who owns it - a Nekro copy; the planet card stays the Ghoti's)
    const bool v10 = G.fixes >= 10;
    if (!fac(p, "ghoti") && !(v10 && owns(p, "dsghotg"))) return 0;
    Player& pl = G.players[size_t(p)];
    int n = 0, card = 0;
    if (fac(p, "ghoti") && !pl.dsGhotiExhausted && fyes(p, Ab::DsGhotiPlanet, "Exhaust your Ghoti planet card for 3 votes?", "")) {
        pl.dsGhotiExhausted = true;
        n += 3;
        card = 3;
    }
    if (techReady(p, "dsghotg") && pl.fleet > 0 && fyes(p, Ab::DsNetworked, "Exhaust Networked Command for " + std::to_string(pl.fleet) + " more votes?", "")) {
        exhaustTech(p, "dsghotg");
        n += pl.fleet;
    }
    if (n && v10) {  // (the feed names what gave them)
        if (card) ds7Event(E, p, "Spawning Grounds", pn(p) + " casts 3 more votes (their planet card).");
        if (n > card) ds7Event(E, p, "Networked Command", pn(p) + " casts " + std::to_string(n - card) + " more votes (Networked Command).");
    } else if (n) {
        ds7Event(E, p, "Networked Command", pn(p) + " casts " + std::to_string(n) + " more votes.");
    }
    return n;
}

// The Ghoti planet card in a payment (Spawning Grounds): 3 resources or 3 influence. Returns the value used.
int Rules::dsGhotiPlanet(int p, int amount, bool influence, const std::string& why) {
    // The Grove (Khrask, Mended Grove breakthrough, 0.43.0): a planet card worth 2 resources (shares the flag)
    if (!influence && amount > 0 && !G.players[size_t(p)].dsGhotiExhausted && dsbtOn(p, "khrask")) {
        if (!fyes(p, Ab::DsbtKhrask, "Exhaust The Grove for 2 resources?", "Toward " + why + " (" + std::to_string(amount) + " needed).")) return 0;
        G.players[size_t(p)].dsGhotiExhausted = true;
        return 2;
    }
    if (!fac(p, "ghoti") || amount <= 0 || G.players[size_t(p)].dsGhotiExhausted) return 0;
    if (!fyes(p, Ab::DsGhotiPlanet, std::string("Exhaust your Ghoti planet card for 3 ") + (influence ? "influence?" : "resources?"),
              "Toward " + why + " (" + std::to_string(amount) + " needed).")) return 0;
    G.players[size_t(p)].dsGhotiExhausted = true;
    return 3;
}

// ================================================================== component actions
void Rules::ds7Options(int p, std::vector<Option>& o) {
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    if (fac(p, "mirveda") && heroUnlocked(G, p)) add(Ab::DsWrathMachina, "Hero: Wrath Machina", "Up to 3 PDS; then every PDS of yours fires at one system (space cannon and bombardment). Purged afterwards.");
    if (fac(p, "ghoti") && heroUnlocked(G, p)) add(Ab::DsNmenmede, "Hero: Nmenmede", "In each system with your ships, one of them becomes a ship costing up to 2 more. Purged afterwards.");
}

bool Rules::ds7Action(int p, Ab ab) {
    Player& pl = G.players[size_t(p)];
    switch (ab) {
        case Ab::DsWrathMachina: {
            if (!heroUnlocked(G, p)) return false;
            ds5Hero(p, leaderIndex(G, p, kHero), pn(p) + " wakes the AI Mainframe: Dhor's Hammer falls.");
            for (int i = 0; i < 3 && unitsAvailable(G, p, UnitType::PDS) > 0; ++i) {
                std::vector<Option> opts;
                for (int x : controlledPlanets(G, p))
                    if (!isDMZ(G, x)) opts.push_back({x, planetName(x), ""});
                if (opts.empty()) break;
                opts.push_back({-1, "No more", ""});
                const int x = fpick(p, Ab::DsWrathMachina, "Wrath Machina: a PDS at which planet?", opts, "", false, true);
                if (x < 0) break;
                addUnits(p, UnitType::PDS, 1, G.planets[size_t(x)].system, x);
            }
            std::vector<Option> targets;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                bool foe = false;
                for (int q = 0; q < kSides; ++q) foe = foe || (q != p && hasUnits(G, q, int(s)));
                if (foe) targets.push_back({int(s), sysName(int(s)), ""});
            }
            if (targets.empty()) return true;
            const int s = fpick(p, Ab::DsWrathMachina, "Wrath Machina: which system do your PDS fire at?", targets, "", true);
            const int pds = unitsOnBoard(G, p, UnitType::PDS);
            const UnitDef& u = unitDef(G, p, UnitType::PDS);
            // space cannon at one player's ships there
            std::vector<Option> ships;
            for (int q = 0; q < kSides; ++q)
                if (q != p && hasShips(G, q, s)) ships.push_back({q, q < G.numPlayers ? pn(q) : std::string("the neutral units"), ""});
            if (!ships.empty() && u.cannon > 0) {
                const int q = ships.size() == 1 ? ships[0].id : fpick(p, Ab::DsWrathMachina, "Wrath Machina: whose ships?", ships);
                // 0.44.5 (fixes 9): Graviton Laser System and Antimass Deflectors apply to this space cannon shot
                const bool gls = G.fixes >= 9 && askGraviton(p, q, s);
                RollResult r;
                // (0.44.8, fixes 12: Plasma Scoring and the other extra dice)
                addDice(r, pds * std::max<int>(1, u.cannonDice) + (G.fixes >= 12 ? abilityDice(p, s) : 0), u.cannon + (G.fixes >= 9 ? antimass(p, q) : 0),
                        int(UnitType::PDS));
                reportRoll(p, s, "Dhor's Hammer (space cannon)", r);
                if (r.hits > 0) cannonHits(q, s, r.hits, kShipMask7, gls, "Wrath Machina", p);
            }
            // bombardment of one planet there
            std::vector<Option> planets;
            for (int16_t x : G.systems[size_t(s)].planets)
                for (int q = 0; q < kSides; ++q)
                    if (q != p && G.planets[size_t(x)].units[size_t(q)].groundForces() > 0) {
                        planets.push_back({int(x) * 16 + q, planetName(x) + (q < G.numPlayers ? " (" + pn(q) + ")" : std::string()), ""});
                        break;
                    }
            if (!planets.empty() && u.bombard > 0) {
                const int c = planets.size() == 1 ? planets[0].id : fpick(p, Ab::DsWrathMachina, "Wrath Machina: which planet is bombarded?", planets, "", false, true);
                RollResult r;
                addDice(r, pds * std::max<int>(1, u.bombardDice) + (G.fixes >= 12 ? abilityDice(p, s) : 0), u.bombard, int(UnitType::PDS));
                reportRoll(p, s, "Dhor's Hammer (bombardment)", r);
                if (G.fixes >= 12) r.hits = proximaCancel(c % 16, s, c / 16, r.hits);  // (0.44.8)
                if (r.hits > 0) assignHits(c % 16, s, c / 16, r.hits, kGroundMask7, "Wrath Machina", p);
            }
            mirvedaBlockade();
            return true;
        }
        case Ab::DsNmenmede: {
            if (!heroUnlocked(G, p)) return false;
            ds5Hero(p, leaderIndex(G, p, kHero), pn(p) + " calls the All Mother: Transcendence - the Ghoti arise.");
            for (size_t s = 0; s < G.systems.size(); ++s) {
                Force& f = G.systems[s].space[size_t(p)];
                if (!f.nonFighterShips()) continue;
                std::vector<Option> opts;
                for (int from : kShipIdx) {
                    if (from == int(UnitType::Fighter) || !f.n[from]) continue;
                    const int c = unitCost2(G, p, UnitType(from));
                    for (int to : kShipIdx) {
                        if (to == from || to == int(UnitType::Fighter) || unitsAvailable(G, p, UnitType(to)) <= 0) continue;
                        const int c2 = unitCost2(G, p, UnitType(to));
                        if (c2 <= c || c2 > c + 4) continue;
                        opts.push_back({from * 16 + to, std::string(unitTypeName(UnitType(from))) + " -> " + unitTypeName(UnitType(to)), ""});
                    }
                }
                if (opts.empty()) continue;
                opts.push_back({-1, "None here", ""});
                const int c = fpick(p, Ab::DsNmenmede, "Nmenmede: which ship transforms in " + sysName(int(s)) + "?", opts);
                if (c < 0) continue;
                f.remove(UnitType(c / 16), 1, false);
                addUnits(p, UnitType(c % 16), 1, int(s), -1);
            }
            enforceFleetLimit(p);
            ghotiSync();
            (void)pl;
            return true;
        }
        default: return false;
    }
}

}  // namespace ti
