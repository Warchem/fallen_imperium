// Batch 6 (0.35.0): the Thunder's Edge factions - their abilities, faction technologies, leaders, faction notes
// and unit abilities. Our own wording in faction_text.cpp; the AI for their questions in ai/ai_te.cpp (Ab::Bastion*,
// Ab::RalNel*, ...). Nothing here runs for a faction until it is in kDone (faction_text.cpp).
//   Last Bastion        - Liberate, Galvanize (Force::galv: 1 more combat die each; galvanized units move with
//                         the fleet first and are destroyed last), Phoenix Standard, Dame Briar, Nip and Tuck,
//                         Lyra Keen, Raise the Standard, Proxima Targeting VI, the Helios docks, The Egeiro,
//                         A3 Valiance, Ordinian (Hyperion VI), The Icon
//   Ral Nel Consortium  - Survival Instinct, Miniaturization (structures carried without capacity - checkMovePlan -,
//                         placed at the end of the tactical action), Kan Kip Rel, Watchful Ojz, Director Nel,
//                         Nanomachines, The Last Dispatch, Alarum, Nano-Link Permit, Data Skimmer
//   Deepwrought         - Research Team (coexist instead of a ground combat - researchTeam), Oceanbound (ocean cards:
//                         Player::oceans, spent like planets in pay()), Doctor Carrina, Aello, Ta Zern, Share
//                         Knowledge, Hydrothermal Mining, Radical Advancement, Eanautic, Visionaria Select
//   Crimson Rebellion   - Sundered (epsilon wormholes only - routeNeighbors; intruders in their home destroyed),
//                         Incursion / Sorrow (SystemState::breach: active breaches adjoin - computeAdjacency), the
//                         Exile destroyers, Ahk Ravin, Ahk Siever, Homesick Phantom, Sever, Subatomic Splicer,
//                         Revenant, Resonance Generator
#include <algorithm>
#include <cstring>

#include "engine/breakthroughs.h"
#include "engine/deal.h"
#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

int ownedPlanet(const Game& g, int p, const char* key);  // rules_te.cpp

namespace {
void abilityEvent6(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    e.system = int16_t(sys);
    E.emit(e);
}
}  // namespace

// ================================================================== Last Bastion
int galvanizedOnBoard(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return 0;
    int n = 0;
    for (const SystemState& s : g.systems)
        for (int t = 0; t < kUnitTypes; ++t) n += s.space[size_t(p)].galv[t];
    for (const PlanetState& ps : g.planets)
        for (int t = 0; t < kUnitTypes; ++t) n += ps.units[size_t(p)].galv[t];
    return n;
}

// Galvanize one of p's units in a system (planet -1: its space area; -2: the space area or any planet there) -
// asks which type when there is a choice. Returns false if nothing could be galvanized.
bool Rules::galvanize(int p, int sys, int planet, const std::string& why, int chooser, int kind) {
    if (p < 0 || p >= G.numPlayers || sys < 0) return false;
    std::vector<Option> opts;  // id = location (planet + 1, 0 = space) * 16 + type
    auto fits = [&](int t, int loc) {
        const UnitType u = UnitType(t);
        if (kind == 1)  // (took part: the ships of a space combat, the ground forces of a ground combat - Hel-Titans too)
            return loc == 0 ? isShip(u) : isGroundForce(u) || (u == UnitType::PDS && fac(p, "titans"));
        if (kind == 2) return u != UnitType::SpaceDock;  // (Dame Briar: a PDS is a unit too - LRR 85, 96.1)
        return !isStructure(u);
    };
    auto add = [&](const Force& f, int loc) {
        for (int t = 0; t < kUnitTypes; ++t)
            if (f.n[t] > f.galv[t] && fits(t, loc))
                opts.push_back({loc * 16 + t, std::string(unitTypeName(UnitType(t))) + (loc ? " on " + planetName(loc - 1) : std::string(" in space")),
                                std::to_string(f.galv[t]) + " of " + std::to_string(f.n[t]) + " galvanized"});
    };
    if (planet == -1 || planet == -2) add(G.systems[size_t(sys)].space[size_t(p)], 0);
    if (planet >= 0) add(G.planets[size_t(planet)].units[size_t(p)], planet + 1);
    if (planet == -2)
        for (int16_t x : G.systems[size_t(sys)].planets) add(G.planets[size_t(x)].units[size_t(p)], x + 1);
    if (opts.empty()) return false;
    const int who = chooser >= 0 ? chooser : p;
    const int c = opts.size() == 1 ? opts[0].id : fpick(who, Ab::Galvanize, "Galvanize which unit? (" + why + ")", opts);
    const int loc = c / 16, t = c % 16;
    Force& f = loc ? G.planets[size_t(loc - 1)].units[size_t(p)] : G.systems[size_t(sys)].space[size_t(p)];
    f.galv[t] = uint8_t(f.galv[t] + 1);
    abilityEvent6(E, p, "Galvanize", pn(p) + " galvanizes a " + unitTypeName(UnitType(t)) + " (" + why + ").", sys);
    return true;
}

// Liberate: gaining a planet readies it (enough of our infantry there) or puts an infantry on it.
void Rules::liberate(int planet, int p) {
    if (!fac(p, "bastion") || planet < 0 || isStation(G, planet)) return;
    PlanetState& ps = G.planets[size_t(planet)];
    if (ps.units[size_t(p)].count(UnitType::Infantry) >= planetResources(G, planet)) {
        if (ps.exhausted) {
            ps.exhausted = false;
            say(p, pn(p) + " readies " + planetName(planet) + " (Liberate).", ps.system);
        }
    } else if (unitsAvailable(G, p, UnitType::Infantry) > 0) {
        addUnits(p, UnitType::Infantry, 1, ps.system, planet);
        say(p, pn(p) + " puts 1 infantry on " + planetName(planet) + " (Liberate).", ps.system);
    }
}

// End of any combat: Phoenix Standard (the Last Bastion galvanize 1 unit that fought), Raise the Standard (a holder
// of their note may do the same and return it). Other factions' end-of-combat abilities come here too.
void Rules::combatEnded6(int att, int def, int sys, int planet) {
    crimsonCombatEnd(att, def, sys);  // Exile destroyers, Ahk Siever
    reapingWon(att, def, sys, planet);  // The Reaping (Obsidian breakthrough)
    for (int p : {att, def}) {
        if (p < 0 || p >= G.numPlayers || G.players[size_t(p)].eliminated) continue;
        const Force& f = planet >= 0 ? G.planets[size_t(planet)].units[size_t(p)] : G.systems[size_t(sys)].space[size_t(p)];
        if (f.empty()) continue;
        // ((fixes 27, the Last Bastion review - owner 2026-11-13) "1 of your units that participated": not the ground forces a
        // carrier holds in a space combat)
        const int kind = G.fixes >= 27 ? 1 : 0;
        if (fac(p, "bastion") && fyes(p, Ab::PhoenixStandard, "Phoenix Standard: galvanize 1 of your units that fought?", abilityText(G.players[size_t(p)].faction, 2)))
            galvanize(p, sys, planet >= 0 ? planet : -1, "Phoenix Standard", -1, kind);
        const int b = playerOfFaction(G, "bastion");
        if (b >= 0 && b != p && holdsNote(G, p, b, kNoteFaction) &&
            fyes(p, Ab::RaiseStandard, "Play Raise the Standard: galvanize 1 of your units that fought?",
                 factionNoteText(G.players[size_t(b)].faction, kNoteFaction))) {
            if (galvanize(p, sys, planet >= 0 ? planet : -1, "Raise the Standard", -1, kind)) returnNote(p, makeNote(b, kNoteFaction));
        }
    }
}

// A galvanized unit was destroyed: A3 Valiance (the Last Bastion's mech) galvanizes up to 3 infantry in its system;
// Lyra Keen (their hero) may strike back at everything there.
void Rules::galvanizedLost(int p, UnitType t, int n, int sys, int planet) {
    if (n <= 0 || p < 0 || p >= G.numPlayers || !fac(p, "bastion")) return;
    if (t == UnitType::Mech && mechAbilities(G)) {
        int k = 0;
        for (int i = 0; i < 3; ++i)
            if (galvanizeInfantry(p, sys)) ++k;
        if (k) say(p, pn(p) + "'s A3 Valiance galvanizes " + std::to_string(k) + " infantry as it falls.", sys);
    }
    if (heroUnlocked(G, p)) {
        std::vector<int> foes;
        for (int q = 0; q < kSides; ++q)
            if (q != p && (q < G.numPlayers || q == kNeutral) && hasUnits(G, q, sys)) foes.push_back(q);
        if (!foes.empty() && fyes(p, Ab::BastionHero, "Lyra Keen: purge her to strike every enemy unit in " + sysName(sys) + "?",
                                  leaderText(findLeader("bastionhero")))) {
            purgeHero(p);
            const int need = std::max(1, int(unitDef(G, p, t).combat));
            int killed = 0;
            for (int q : foes) {
                auto strike = [&](Force& f, int loc) {
                    for (int u = 0; u < kUnitTypes; ++u)
                        for (int k = f.n[u]; k > 0; --k)
                            if (E.rng().d10() >= need) {
                                destroyUnits(q, UnitType(u), 1, sys, loc, p, "Lyra Keen");
                                ++killed;
                            }
                };
                strike(G.systems[size_t(sys)].space[size_t(q)], -1);
                for (int16_t x : G.systems[size_t(sys)].planets) strike(G.planets[size_t(x)].units[size_t(q)], x);
            }
            say(p, pn(p) + "'s Lyra Keen destroys " + std::to_string(killed) + " unit(s) in " + sysName(sys) + ".", sys);
            (void)planet;
        }
    }
}

bool Rules::galvanizeInfantry(int p, int sys) {
    for (int16_t x : G.systems[size_t(sys)].planets) {
        Force& f = G.planets[size_t(x)].units[size_t(p)];
        if (f.count(UnitType::Infantry) > f.galv[int(UnitType::Infantry)]) {
            f.galv[int(UnitType::Infantry)]++;
            return true;
        }
    }
    Force& s = G.systems[size_t(sys)].space[size_t(p)];
    if (s.count(UnitType::Infantry) > s.galv[int(UnitType::Infantry)]) {
        s.galv[int(UnitType::Infantry)]++;
        return true;
    }
    return false;
}

// (fixes 27, the Last Bastion review - owner 2026-11-13) Dame Briar: "When a player's unit is destroyed: You may exhaust this
// card to galvanize another of that player's units in the destroyed unit's system" - any player's; the Bastion decide (a
// person is asked - briarQuiet_ -, a bot when it promised); a PDS may be galvanized (a unit: 1 more space cannon die)
void Rules::dameBriar27(int victim, int sys) {
    if (victim < 0 || victim >= G.numPlayers || sys < 0) return;
    bool any = false;
    const auto check = [&](const Force& f) {
        for (int t = 0; t < kUnitTypes; ++t)
            if (f.n[t] > f.galv[t] && UnitType(t) != UnitType::SpaceDock) any = true;
    };
    check(G.systems[size_t(sys)].space[size_t(victim)]);
    for (int16_t x : G.systems[size_t(sys)].planets) check(G.planets[size_t(x)].units[size_t(victim)]);
    if (!any) return;
    const std::string text = leaderText(findLeader("bastionagent"));
    int user = -1;
    if (canUseAgent(victim, "bastion")) {
        if (fyes(victim, Ab::BastionAgent, "Exhaust Dame Briar to galvanize another of your units here?", text)) user = victim;
    } else {
        for (int o : initiativeOrder(G)) {
            if (o == victim || !canUseAgent(o, "bastion")) continue;
            const bool promised = G.agentFor[o] == victim;
            const bool human = G.players[size_t(o)].human;
            if (!promised && !(human && briarQuiet_[o] != G.round)) continue;
            agentFor(o, victim);
            std::vector<Option> yn = {{1, "Yes", ""}, {0, "No", ""}};
            if (human && !promised) yn.push_back({3, "No, and don't ask again this round", "You'll be asked again next round."});
            const int c = fpick(o, Ab::BastionAgent, "Exhaust Dame Briar: galvanize another of " + pn(victim) + "'s units in " + sysName(sys) + "?", yn,
                                "A unit of " + pn(victim) + "'s was destroyed there. " + text);
            curFor_ = -1;
            if (c == 3) briarQuiet_[o] = int16_t(G.round);
            if (c != 1) continue;
            user = o;
            break;
        }
    }
    if (user < 0) return;
    spendAgent(user, "bastion");
    agentDone(user, victim);
    galvanize(victim, sys, -2, "Dame Briar", user, 2);
}

// Dame Briar (the Last Bastion's agent): when a player's unit is destroyed, galvanize another of theirs there.
void Rules::dameBriar(int victim, int sys) {
    if (G.fixes >= 27) {
        dameBriar27(victim, sys);
        return;
    }
    if (victim < 0 || victim >= G.numPlayers || sys < 0) return;
    const int b = playerOfFaction(G, "bastion");
    if (b < 0) return;
    const int user = agentUser(victim, "bastion");
    if (user < 0) return;
    bool any = false;
    const auto check = [&](const Force& f) {
        for (int t = 0; t < kUnitTypes; ++t)
            if (f.n[t] > f.galv[t] && !isStructure(UnitType(t))) any = true;
    };
    check(G.systems[size_t(sys)].space[size_t(victim)]);
    for (int16_t x : G.systems[size_t(sys)].planets) check(G.planets[size_t(x)].units[size_t(victim)]);
    if (!any) return;
    agentFor(user, victim);
    curFor_ = victim;
    const bool use = fyes(user, Ab::BastionAgent,
                          user == victim ? "Exhaust Dame Briar to galvanize another of your units here?"
                                         : "Exhaust Dame Briar to galvanize another of " + pn(victim) + "'s units here?",
                          leaderText(findLeader("bastionagent")));
    curFor_ = -1;
    if (!use) return;
    spendAgent(user, "bastion");
    agentDone(user, victim);
    galvanize(victim, sys, -2, "Dame Briar", user);
}

// Proxima Targeting VI's other half: bombardment hits against q's ground forces on planet - 1 less per galvanized unit
// of q's there. Returns the hits left.
int Rules::proximaCancel(int q, int sys, int planet, int hits) {
    if (q < 0 || q >= G.numPlayers || hits <= 0 || planet < 0 || !owns(q, "proxima")) return hits;
    int galv = 0;
    for (int t = 0; t < kUnitTypes; ++t) galv += G.planets[size_t(planet)].units[size_t(q)].galv[t];
    const int cancel = std::min(galv, hits);
    if (cancel > 0) say(q, std::to_string(cancel) + " bombardment hit(s) cancelled (Proxima Targeting VI).", sys);
    return hits - cancel;
}

// Proxima Targeting VI: at the start of a ground combat round, 3 dice at 8 against the opponent's ground forces -
// and the same against your own.
void Rules::proximaTargeting(int att, int def, int planet) {
    for (int p : {att, def}) {
        if (p < 0 || p >= G.numPlayers || !owns(p, "proxima")) continue;
        const int o = p == att ? def : att;
        const int sys = G.planets[size_t(planet)].system;
        if (groundForcesOn(G, o, planet) <= 0 || groundForcesOn(G, p, planet) <= 0) continue;
        // (0.44.9, fixes 13 - the owner's answer W4: it resolves BOMBARDMENT, so what stops a bombardment stops it: the
        // opponent's Planetary Shield there (LRR 65; a war sun of ours or a shield that is down lifts it) and Conventions
        // of War on a cultural planet)
        if (G.fixes >= 13) {
            if (lawInPlay(G, "conventions") && hasTraitOf(G, planet, Trait::Cultural)) continue;
            bool shield = false;
            if (o < kSides)
                for (int t = 0; t < kUnitTypes; ++t)
                    if (G.planets[size_t(planet)].units[size_t(o)].n[t] && unitDef(G, o, UnitType(t)).shield) shield = true;
            if (G.fx.dsShieldPlanet == planet) shield = true;  // (Orbital Defense Grid)
            if (o < kSides && shieldsDown(G, o, sys)) shield = false;
            for (int t : kShipIdx)
                if (G.systems[size_t(sys)].space[size_t(p)].n[t] && unitDef(G, p, UnitType(t)).ignoresShield) shield = false;
            if (shield) continue;
        }
        // (0.46.1 - owner, RULES-CHECK Q2: say plainly that these dice aren't a unit's roll)
        if (!fyes(p, Ab::Proxima, "Proxima Targeting VI: bombard both sides' ground forces on " + planetName(planet) + "?",
                  G.fixes >= 15 ? "3 dice at 8 against them, then 3 dice at 8 against your own. The technology rolls these dice, "
                                  "not a unit: X-89 Bacterial Weapon doesn't double their hits and Plasma Scoring adds no die."
                                : "3 dice at 8 against them, then 3 dice at 8 against your own."))
            continue;
        for (int side : {o, p}) {
            RollResult r;
            for (int i = 0; i < 3; ++i) {
                r.dice.push_back(int8_t(E.rng().d10()));
                r.need.push_back(8);
                r.unit.push_back(-1);
                r.bonus9.push_back(0);
            }
            recount(r);
            if (G.fixes >= 15)  // (0.46.1: the technology's own dice - nothing that adds to a unit's roll applies)
                reportRoll(p, sys, side == o ? "Proxima Targeting VI (the technology's dice: no X-89, no Plasma Scoring)"
                                             : "Proxima Targeting VI against your own (no X-89, no Plasma Scoring)", r);
            else
                reportRoll(p, sys, side == o ? "Proxima Targeting VI" : "Proxima Targeting VI (against your own)", r);
            // (0.44.8, fixes 12) a bombardment roll: the target's own Proxima Targeting VI cancels hits for its galvanized
            // units - also the backlash against the Bastion's own ground forces
            if (G.fixes >= 12) r.hits = proximaCancel(side, sys, planet, r.hits);
            if (r.hits > 0 && groundForcesOn(G, side, planet) > 0)
                assignHits(side, sys, planet, r.hits, (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech)), "Proxima Targeting VI", p);
        }
    }
}

// The Egeiro (flagship): +1 for each non-home system with a planet the Last Bastion control.
int egeiroBonus(const Game& g, int p) {
    std::vector<char> seen(g.systems.size(), 0);
    int n = 0;
    for (int x : controlledPlanets(g, p)) {
        const int s = g.planets[size_t(x)].system;
        if (s < 0 || seen[size_t(s)] || g.systems[size_t(s)].home >= 0) continue;
        seen[size_t(s)] = 1;
        ++n;
    }
    return n;
}

// The Icon (the Last Bastion's breakthrough): the ships just produced may go to another system with our command
// token and ground forces and no other player's ships.
void Rules::theIcon(int p, int sys, const int counts[kUnitTypes]) {
    if (!breakthroughIs(G, p, "bastion") || G.players[size_t(p)].btExhausted) return;
    int ships = 0;
    for (int t : kShipIdx) ships += counts[t];
    if (!ships) return;
    std::vector<Option> opts;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (int(s) == sys || !G.systems[s].hasToken(p) || otherShipsIn(G, p, int(s))) continue;
        bool gf = G.fixes >= 8 && G.systems[s].space[size_t(p)].groundForces() > 0;  // (0.44.4: carried ones count)
        for (int16_t x : G.systems[s].planets) gf = gf || G.planets[size_t(x)].units[size_t(p)].groundForces() > 0;
        if (gf) opts.push_back({int(s), sysName(int(s)), ""});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "Keep them here", ""});
    const int to = fpick(p, Ab::BtBastion, "The Icon: exhaust it to place the new ships in another system?", opts,
                         "A system with your command token and ground forces and no other player's ships.", true);
    if (to < 0) return;
    G.players[size_t(p)].btExhausted = true;
    Force& src = G.systems[size_t(sys)].space[size_t(p)];
    Force& dst = G.systems[size_t(to)].space[size_t(p)];
    for (int t : kShipIdx) {
        const int k = std::min<int>(counts[t], src.n[t]);
        if (k <= 0) continue;
        src.remove(UnitType(t), k, false);
        dst.add(UnitType(t), k);
    }
    say(p, pn(p) + "'s new ships appear in " + sysName(to) + " (The Icon).", to);
    removeExcessCapacity(p, to);
    if (G.fixes >= 8) enforceFleetLimit(p);  // (0.44.4: the fleet pool counts in the system they went to)
}

// ================================================================== Ral Nel Consortium
namespace {
void moveShip(Force& src, Force& dst, int t) {  // one ship, keeping its damage and galvanize token
    const bool dmg = src.dmg[t] > 0 && src.dmg[t] >= src.n[t];
    const bool galv = src.galv[t] > 0;
    if (galv) src.galv[t]--;
    src.remove(UnitType(t), 1, dmg);
    dst.add(UnitType(t), 1);
    if (dmg) dst.dmg[t]++;
    if (galv) dst.galv[t]++;
}
}  // namespace

// Survival Instinct: after another player activates a system with Ral Nel ships, up to 2 of their ships come in from
// adjacent systems without their command tokens.
void Rules::survivalInstinct(int active, int sys) {
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == active || !fac(q, "ralnel") || G.players[size_t(q)].eliminated || !hasShips(G, q, sys)) continue;
        for (int k = 0; k < 2; ++k) {
            std::vector<Option> opts;  // id = system * 16 + ship type
            for (int16_t a : G.systems[size_t(sys)].adj) {
                if (G.systems[size_t(a)].hasToken(q)) continue;
                const Force& f = G.systems[size_t(a)].space[size_t(q)];
                for (int t : kShipIdx)
                    if (f.n[t]) opts.push_back({a * 16 + t, std::string(unitTypeName(UnitType(t))) + " from " + sysName(a), ""});
            }
            if (opts.empty()) break;
            opts.push_back({-1, k ? "No more" : "None", ""});
            curFor_ = active;
            const int c = fpick(q, Ab::SurvivalInstinct, "Survival Instinct: move a ship into " + sysName(sys) + "?", opts,
                                std::to_string(2 - k) + " more at most. " + pn(active) + " activated it.");
            curFor_ = -1;
            if (c < 0) break;
            moveShip(G.systems[size_t(c / 16)].space[size_t(q)], G.systems[size_t(sys)].space[size_t(q)], c % 16);
            if (G.fixes >= 27) carryAlong(q, c / 16, sys);  // (the card's notes: "These ships can transport fighters and ground forces")
            abilityEvent6(E, q, "Survival Instinct", pn(q) + " rushes a " + unitTypeName(UnitType(c % 16)) + " from " + sysName(c / 16) + " into " +
                                                        sysName(sys) + " (Survival Instinct).", sys);
            removeExcessCapacity(q, c / 16);
        }
    }
}

// Miniaturization: at the end of the Ral Nel's tactical action their structures in space areas go onto planets they
// control in those systems (PDS: 2 a planet; space docks: 1).
void Rules::miniaturizationEnd(int p) {
    if (!fac(p, "ralnel")) return;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        Force& f = G.systems[s].space[size_t(p)];
        for (UnitType t : {UnitType::SpaceDock, UnitType::PDS}) {
            while (f.count(t) > 0) {
                std::vector<Option> opts;
                for (int16_t x : G.systems[s].planets) {
                    const PlanetState& ps = G.planets[size_t(x)];
                    if (ps.owner != p || ps.destroyed) continue;
                    const int have = ps.units[size_t(p)].count(t);
                    if (t == UnitType::SpaceDock ? have >= 1 : (have >= 2 && !lawInPlay(G, "defense_act"))) continue;
                    opts.push_back({x, planetName(x), ""});
                }
                if (opts.empty()) break;
                opts.push_back({-1, "Leave it in space", "Structures in space do nothing."});
                const int x = fpick(p, Ab::Miniaturization, std::string("Miniaturization: put your ") + unitTypeName(t) + " in " + sysName(int(s)) + " on which planet?",
                                    opts, "", false, true);
                if (x < 0) break;
                f.remove(t, 1, false);
                G.planets[size_t(x)].units[size_t(p)].add(t, 1);
                say(p, pn(p) + " sets down a " + unitTypeName(t) + " on " + planetName(x) + " (Miniaturization).", int(s));
            }
        }
    }
}

// Nano-Link Permit (the Ral Nel's note): after activating, its holder brings structures from adjacent systems
// without their command tokens onto their planets in the active system.
void Rules::nanoLink(int p, int sys) {
    const int r = playerOfFaction(G, "ralnel");
    if (r < 0 || r == p || !holdsNote(G, p, r, kNoteFaction)) return;
    std::vector<int> mine;
    for (int16_t x : G.systems[size_t(sys)].planets)
        if (G.planets[size_t(x)].owner == p) mine.push_back(x);
    if (mine.empty()) return;
    bool any = false;
    for (int16_t a : G.systems[size_t(sys)].adj) {
        if (G.systems[size_t(a)].hasToken(p)) continue;
        for (int16_t x : G.systems[size_t(a)].planets)
            if (G.planets[size_t(x)].units[size_t(p)].structures()) any = true;
    }
    if (!any || !fyes(p, Ab::NanoLink, "Play Nano-Link Permit: move structures from adjacent systems onto your planets here?",
                      factionNoteText(G.players[size_t(r)].faction, kNoteFaction)))
        return;
    returnNote(p, makeNote(r, kNoteFaction));
    for (int16_t a : G.systems[size_t(sys)].adj) {
        if (G.systems[size_t(a)].hasToken(p)) continue;
        for (int16_t x : G.systems[size_t(a)].planets) {
            Force& f = G.planets[size_t(x)].units[size_t(p)];
            for (UnitType t : {UnitType::PDS, UnitType::SpaceDock})
                while (f.count(t) > 0) {
                    std::vector<Option> opts;
                    for (int y : mine) {
                        const int have = G.planets[size_t(y)].units[size_t(p)].count(t);
                        if (t == UnitType::SpaceDock ? have < 1 : (have < 2 || lawInPlay(G, "defense_act")))
                            opts.push_back({y, planetName(y), ""});
                    }
                    if (opts.empty()) break;
                    opts.push_back({-1, "Leave it", ""});
                    const int y = fpick(p, Ab::NanoLink, std::string("Move the ") + unitTypeName(t) + " on " + planetName(x) + " to?", opts);
                    if (y < 0) break;
                    f.remove(t, 1, false);
                    G.planets[size_t(y)].units[size_t(p)].add(t, 1);
                }
        }
    }
}

// (fixes 27, the Ral Nel review) a ship moved by an ability (Survival Instinct, Watchful Ojz) takes along what it transports:
// the fighters / ground forces its old space area can no longer hold go with it while it has room
void Rules::carryAlong(int p, int from, int to) {
    for (int guard = 0; guard < 40 && capacityOverflow(G, p, from) > 0; ++guard) {
        Force& src = G.systems[size_t(from)].space[size_t(p)];
        Force& dst = G.systems[size_t(to)].space[size_t(p)];
        int t = -1;
        for (UnitType u : {UnitType::Mech, UnitType::Infantry, UnitType::Fighter})
            if (src.count(u) > 0 && !(u == UnitType::Fighter && fightersFree(G, p))) {
                t = int(u);
                break;
            }
        if (t < 0) break;
        moveShip(src, dst, t);
        if (capacityOverflow(G, p, to) > 0) {  // (no room on the ships that left)
            moveShip(dst, src, t);
            break;
        }
    }
}

// Watchful Ojz (commander): up to 2 ships retreat to an adjacent system without other players' ships; a command token
// from the reinforcements goes there. (fixes 27, the Ral Nel review: "When you declare a retreat: Immediately retreat" -
// at the declaration, before the round's rolls; their transports' cargo goes with them, and The Last Dispatch fires if
// the flagship is one of them.) notTo: the fleet's own retreat system (the old timing) or -1.
void Rules::ojz(int p, int opp, int sys, int notTo) {
    if (!commanderUsable(G, p, "ralnel") || reinforcementTokens(p) <= 0) return;
    std::vector<Option> dest;
    for (int16_t a : G.systems[size_t(sys)].adj)
        if (a != notTo && !otherShipsIn(G, p, a) && !G.systems[size_t(a)].isHyperlane()) dest.push_back({a, sysName(a), ""});
    if (dest.empty()) return;
    dest.push_back({-1, "Keep them together", ""});
    const int a = fpick(p, Ab::RalNelCommander, "Watchful Ojz: send up to 2 ships to another system?", dest,
                        G.fixes >= 27 ? "Right away, before this round's rolls. A command token from your reinforcements goes there."
                                      : "A command token from your reinforcements goes there.",
                        true);
    if (a < 0) return;
    G.systems[size_t(a)].tokens = uint16_t(G.systems[size_t(a)].tokens | (1u << p));
    bool flagship = false;
    for (int k = 0; k < 2; ++k) {
        std::vector<Option> opts;
        for (int t : kShipIdx)
            if (G.systems[size_t(sys)].space[size_t(p)].n[t]) opts.push_back({t, unitTypeName(UnitType(t)), ""});
        if (opts.empty()) break;
        opts.push_back({-1, "No more", ""});
        const int t = fpick(p, Ab::RalNelCommander, "Watchful Ojz: which ship goes to " + sysName(a) + "?", opts);
        if (t < 0) break;
        moveShip(G.systems[size_t(sys)].space[size_t(p)], G.systems[size_t(a)].space[size_t(p)], t);
        if (G.fixes >= 27) carryAlong(p, sys, a);
        flagship = flagship || t == int(UnitType::Flagship);
    }
    say(p, pn(p) + " splits the retreat: ships go to " + sysName(a) + " (Watchful Ojz).", a);
    removeExcessCapacity(p, a);
    if (G.fixes >= 27 && flagship && flagshipAbility(G, p, "ralnel") && opp >= 0) {  // The Last Dispatch: it retreated
        std::vector<Option> opts;
        for (int t : kShipIdx)
            if (G.systems[size_t(sys)].space[size_t(opp)].n[t] && !unitSustainsAt(G, opp, UnitType(t), sys, -1))
                opts.push_back({t, unitTypeName(UnitType(t)), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            const int t = fpick(p, Ab::LastDispatch, "The Last Dispatch: destroy 1 of " + pn(opp) + "'s ships as you retreat?", opts);
            if (t >= 0) destroyUnits(opp, UnitType(t), 1, sys, -1, p, "The Last Dispatch");
        }
    }
}

// Retreating: Watchful Ojz (commander) takes up to 2 ships to a second system; The Last Dispatch (flagship) destroys
// a ship without Sustain Damage on its way out.
void Rules::ralnelRetreat(int p, int opp, int sys, int to) {
    if (p < 0 || p >= G.numPlayers) return;
    if (flagshipAbility(G, p, "ralnel") && flagshipIn(G, p, sys) && opp >= 0) {
        std::vector<Option> opts;
        for (int t : kShipIdx)
            if (G.systems[size_t(sys)].space[size_t(opp)].n[t] && !unitSustainsAt(G, opp, UnitType(t), sys, -1))
                opts.push_back({t, unitTypeName(UnitType(t)), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            const int t = fpick(p, Ab::LastDispatch, "The Last Dispatch: destroy 1 of " + pn(opp) + "'s ships as you retreat?", opts);
            if (t >= 0) destroyUnits(opp, UnitType(t), 1, sys, -1, p, "The Last Dispatch");
        }
    }
    if (G.fixes < 27) ojz(p, opp, sys, to);  // (fixes 27: at the declaration instead - rules_combat)
}

// Alarum (mech): at the end of a ground combat round on its planet, up to 2 ground forces come from planets in
// adjacent systems.
void Rules::alarum(int att, int def, int planet) {
    if (!mechAbilities(G)) return;
    const int sys = G.planets[size_t(planet)].system;
    for (int p : {att, def}) {
        if (p < 0 || p >= G.numPlayers || !fac(p, "ralnel") || !G.planets[size_t(planet)].units[size_t(p)].count(UnitType::Mech)) continue;
        for (int k = 0; k < 2; ++k) {
            std::vector<Option> opts;  // id = planet * 16 + type
            std::vector<int16_t> from(G.systems[size_t(sys)].adj.begin(), G.systems[size_t(sys)].adj.end());
            // (fixes 27, the Ral Nel review - owner 2026-11-14: AsyncTI4's ruling, "adjacent systems" includes the system
            // containing this planet: its other planets too)
            if (G.fixes >= 27) from.push_back(int16_t(sys));
            for (int16_t a : from)
                for (int16_t x : G.systems[size_t(a)].planets)
                    for (UnitType t : {UnitType::Infantry, UnitType::Mech})
                        if (x != planet && G.planets[size_t(x)].units[size_t(p)].count(t))
                            opts.push_back({x * 16 + int(t), std::string(unitTypeName(t)) + " from " + planetName(x), ""});
            if (opts.empty()) break;
            opts.push_back({-1, "No more", ""});
            const int c = fpick(p, Ab::Alarum, "Alarum: bring a ground force to " + planetName(planet) + "?", opts);
            if (c < 0) break;
            Force& src = G.planets[size_t(c / 16)].units[size_t(p)];
            src.remove(UnitType(c % 16), 1, false);
            G.planets[size_t(planet)].units[size_t(p)].add(UnitType(c % 16), 1);
            say(p, pn(p) + "'s Alarum calls a " + unitTypeName(UnitType(c % 16)) + " from " + planetName(c / 16) + ".", sys);
        }
    }
}

// Director Nel (hero): after the last player passes, the Ral Nel may un-pass - 2 command tokens and an action card.
bool Rules::directorNel() {
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!fac(p, "ralnel") || G.players[size_t(p)].eliminated || !heroUnlocked(G, p)) continue;
        if (!fyes(p, Ab::RalNelHero, "Director Nel: stop being passed (2 command tokens and 1 action card)?", leaderText(findLeader("ralnelhero"))))
            continue;
        purgeHero(p);
        G.players[size_t(p)].passed = false;
        gainTokens(p, 2, false, "Director Nel: gain 2 command tokens.");
        drawActionCards(p, 1, "Director Nel");
        abilityEvent6(E, p, "Director Nel", pn(p) + " is no longer passed (Director Nel).");
        return true;
    }
    return false;
}

// Data Skimmer (Ral Nel breakthrough): other players' discarded action cards go onto it while the Ral Nel haven't
// passed; when they pass they keep 1 and discard the rest.
bool Rules::dataSkimmer(int discarder, int card) {
    if (G.phase != Phase::Action || !(G.modules & kModThundersEdge)) return false;
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != discarder && breakthroughIs(G, q, "ralnel") && !G.players[size_t(q)].passed) {
            G.skimmer.push_back(int16_t(card));
            return true;
        }
    return false;
}
void Rules::skimmerPass(int p) {
    if (G.skimmer.empty() || !breakthroughIs(G, p, "ralnel")) return;
    std::vector<Option> opts;
    for (int16_t c : G.skimmer) {
        bool dup = false;  // (0.44.4: each card once - the deck holds copies)
        for (const Option& o : opts) dup = dup || (G.fixes >= 8 && o.id == c);
        if (!dup) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
    }
    const int keep = fpick(p, Ab::BtRalNel, "Data Skimmer: keep which action card?", opts, "The rest are discarded.");
    bool kept = false;
    for (int16_t c : G.skimmer)
        if (c != keep || (kept && G.fixes >= 8)) G.actionDiscard.push_back(c);  // (0.44.4: only ONE copy is kept)
        else {
            G.players[size_t(p)].actionCards.push_back(c);
            kept = true;
        }
    G.skimmer.clear();
    say(p, pn(p) + " keeps 1 of the skimmed action cards (Data Skimmer).");
    if (G.fixes >= 8) enforceHandLimit(p);
}

// Component actions of batch 6: Kan Kip Rel (Ral Nel agent), Nanomachines (Ral Nel technology: three actions).
std::vector<Option> Rules::factionActionOptions6(int p) {
    std::vector<Option> o;
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    if (canUseAgent(p, "ralnel") && !G.actionDeck.empty())
        add(Ab::RalNelAgent, "Agent: Kan Kip Rel", "Draw 2 action cards and give 1 of them to another player.");
    if (techReady(p, "nanomachines")) {
        bool pds = false, dmg = false;
        for (int x : controlledPlanets(G, p)) {
            if (G.fixes >= 12 && isDMZ(G, x)) continue;  // (0.44.8: no PDS on a Demilitarized Zone; Defense Act lifts the limit)
            if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::PDS) < 2 || (G.fixes >= 12 && lawInPlay(G, "defense_act"))) pds = true;
        }
        for (const SystemState& s : G.systems)
            for (int t = 0; t < kUnitTypes; ++t) dmg = dmg || s.space[size_t(p)].dmg[t];
        for (const PlanetState& ps : G.planets)
            for (int t = 0; t < kUnitTypes; ++t) dmg = dmg || ps.units[size_t(p)].dmg[t];
        if (pds && unitsAvailable(G, p, UnitType::PDS) > 0) add(Ab::Nano1, "Nanomachines: place a PDS", "Exhaust it: 1 PDS on a planet you control.");
        if (dmg) add(Ab::Nano2, "Nanomachines: repair", "Exhaust it: repair all of your damaged units.");
        if (!G.players[size_t(p)].actionCards.empty()) add(Ab::Nano3, "Nanomachines: swap a card", "Exhaust it and discard 1 action card to draw 1.");
    }
    return o;
}

bool Rules::factionAction6(int p, Ab ab) {
    Player& pl = G.players[size_t(p)];
    switch (ab) {
        case Ab::RalNelAgent: {
            std::vector<int16_t> drawn;
            for (int k = 0; k < 2 && !G.actionDeck.empty(); ++k) {
                drawn.push_back(G.actionDeck.back());
                G.actionDeck.pop_back();
            }
            if (drawn.empty()) return false;
            spendAgent(p, "ralnel");
            E.revealed();
            for (int16_t c : drawn) pl.actionCards.push_back(c);
            std::vector<Option> cards;
            for (int16_t c : drawn) cards.push_back({c, kActionCards[c].name, actionCardText(c)});
            const int give = fpick(p, Ab::RalNelAgent, "Kan Kip Rel: give which of the 2 cards away?", cards);
            std::vector<Option> who;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && !G.players[size_t(q)].eliminated) who.push_back({q, pn(q), ""});
            if (who.empty()) return true;
            const int q = fpick(p, Ab::RalNelAgent, std::string("Kan Kip Rel: give ") + kActionCards[give].name + " to whom?", who);
            auto it = std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(give));
            if (it != pl.actionCards.end()) pl.actionCards.erase(it);
            G.players[size_t(q)].actionCards.push_back(int16_t(give));
            say(p, pn(p) + " draws 2 action cards and gives 1 to " + pn(q) + " (Kan Kip Rel).");
            return true;
        }
        case Ab::DeepwroughtHero:
        case Ab::ShareKnowledge:
        case Ab::BtDeepwrought:
        case Ab::CrimsonAgent:
        case Ab::Sever:
        case Ab::BtCrimson:
        case Ab::Puppets:
        case Ab::FirmamentHero:
        case Ab::ObsidianHero:
        case Ab::EvAtomics:
        case Ab::EvTotalWar:
        case Ab::EvMercenary:
        case Ab::EvExplore:
        case Ab::EvX89: return factionAction7(p, ab);
        case Ab::Nano1: {
            std::vector<Option> opts;
            for (int x : controlledPlanets(G, p))
                if ((G.planets[size_t(x)].units[size_t(p)].count(UnitType::PDS) < 2 || lawInPlay(G, "defense_act")) &&
                    !(G.fixes >= 12 && isDMZ(G, x)))
                    opts.push_back({x, planetName(x), ""});
            if (opts.empty() || unitsAvailable(G, p, UnitType::PDS) <= 0) return false;
            opts.push_back({-1, "Cancel", ""});
            const int x = fpick(p, Ab::Nano1, "Nanomachines: place a PDS on which planet?", opts, "", false, true);
            if (x < 0) return false;
            exhaustTech(p, "nanomachines");
            addUnits(p, UnitType::PDS, 1, G.planets[size_t(x)].system, x);
            say(p, pn(p) + " builds a PDS on " + planetName(x) + " (Nanomachines).", G.planets[size_t(x)].system);
            return true;
        }
        case Ab::Nano2: {
            exhaustTech(p, "nanomachines");
            for (SystemState& s : G.systems)
                for (int t = 0; t < kUnitTypes; ++t) s.space[size_t(p)].dmg[t] = 0;
            for (PlanetState& ps : G.planets)
                for (int t = 0; t < kUnitTypes; ++t) ps.units[size_t(p)].dmg[t] = 0;
            say(p, pn(p) + " repairs all of their units (Nanomachines).");
            return true;
        }
        case Ab::Nano3: {
            std::vector<Option> opts;
            for (int16_t c : pl.actionCards) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
            opts.push_back({-1, "Cancel", ""});
            const int c = fpick(p, Ab::Nano3, "Nanomachines: discard which action card (then draw 1)?", opts);
            if (c < 0) return false;
            exhaustTech(p, "nanomachines");
            pl.actionCards.erase(std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(c)));
            discardActionCard(p, c, false);
            drawActionCards(p, 1, "Nanomachines");
            return true;
        }
        default: return factionAction7(p, ab);  // (7 -> 8 -> 9 -> Discordant Stars, galactic events)
    }
}

// ================================================================== Deepwrought Scholarate
// Coexisting units on a planet (TE rulebook "Coexist"): the coexisting player doesn't control it; when they are the
// only units left they take it (coexistCleanup).
int coexistPlanets(const Game& g, int p) {
    int n = 0;
    for (const PlanetState& ps : g.planets)
        if (!ps.destroyed && ((ps.coexist >> p) & 1) && !ps.units[size_t(p)].empty()) ++n;
    return n;
}

void Rules::startCoexisting(int p, int planet) {
    PlanetState& ps = G.planets[size_t(planet)];
    if ((ps.coexist >> p) & 1) return;
    ps.coexist = uint16_t(ps.coexist | (1u << p));
    say(p, pn(p) + "'s units coexist on " + planetName(planet) + ".", ps.system);
    if (fac(p, "deepwrought") && G.players[size_t(p)].oceans < 5) {  // Oceanbound: an ocean card, ready
        ++G.players[size_t(p)].oceans;
        abilityEvent6(E, p, "Oceanbound", pn(p) + " gains an ocean card (Oceanbound).", ps.system);
    }
}

void Rules::oceanLimit(int p) {  // Oceanbound: no more ocean cards than planets with our coexisting units
    if (!fac(p, "deepwrought")) return;
    Player& pl = G.players[size_t(p)];
    const int max = coexistPlanets(G, p);
    if (pl.oceans > max) {
        // (fixes 27, the Deepwrought review) exhausted ocean cards are discarded first
        if (G.fixes >= 27) pl.oceansExhausted = int8_t(std::max(0, pl.oceansExhausted - (pl.oceans - max)));
        pl.oceans = int8_t(max);
        pl.oceansExhausted = int8_t(std::min<int>(pl.oceansExhausted, pl.oceans));
        say(p, pn(p) + " discards ocean cards down to " + std::to_string(max) + " (Oceanbound).");
    }
}

// (fixes 27, the Deepwrought review) Oceanbound: "Any time you have more ocean cards than there are planets that contain
// your coexisting units, discard ocean cards until you do not" - checked before every question
void Rules::oceanCheck() {
    if (G.fixes < 27 || !(G.modules & kModThundersEdge)) return;
    for (int p = 0; p < G.numPlayers; ++p)
        if (G.players[size_t(p)].oceans > 0) oceanLimit(p);
}

// Ground forces committed to a planet where another player has ground forces: the Deepwrought (Research Team) or a
// planet with the Firmament's mech (Viper EX-23) may coexist instead of fighting. Returns true = no combat.
bool Rules::coexistInstead(int invader, int q, int planet, int skipWho) {
    PlanetState& ps = G.planets[size_t(planet)];
    for (int who : {invader, q}) {
        if (who < 0 || who >= G.numPlayers || ((ps.coexist >> who) & 1) || who == skipWho) continue;
        const bool team = fac(who, "deepwrought");
        const bool viper = fac(who, "firmament") && mechAbilities(G) && ps.units[size_t(who)].count(UnitType::Mech) > 0;
        if (!team && !viper) continue;
        const int other = who == invader ? q : invader;
        curFor_ = other;
        const bool co = fyes(who, team ? Ab::ResearchTeam : Ab::FirmamentMech,
                             std::string(team ? "Research Team" : "Viper EX-23") + ": coexist with " + pn(other) + " on " + planetName(planet) +
                                 " instead of fighting?",
                             "No combat. Coexisting units don't control the planet; if you controlled it, " + pn(other) + " takes it, exhausted.");
        curFor_ = -1;
        if (!co) continue;
        const bool owned = ps.owner == who;
        startCoexisting(who, planet);
        if (owned && other >= 0) {
            setControl(planet, other);
            G.planets[size_t(planet)].exhausted = true;
        }
        return true;
    }
    return false;
}

// Doctor Carrina (Deepwrought agent): another player researching may ignore 1 prerequisite; then the Deepwrought may
// put 1 infantry, coexisting, on one of that player's non-home planets. Returns true if used.
bool Rules::carrina(int researcher) {
    const int d = playerOfFaction(G, "deepwrought");
    if (d < 0 || d == researcher || !canUseAgent(d, "deepwrought")) return false;
    curFor_ = researcher;
    const bool use = fyes(d, Ab::DeepwroughtAgent, "Exhaust Doctor Carrina so " + pn(researcher) + " can ignore 1 prerequisite?",
                          leaderText(findLeader("deepwroughtagent")));
    curFor_ = -1;
    if (!use) return false;
    spendAgent(d, "deepwrought");
    carrinaInfantry(d, researcher);
    return true;
}

// "... if they do, you may place 1 infantry from your reinforcements into coexistence on a non-home planet they control"
void Rules::carrinaInfantry(int d, int researcher) {
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, researcher))
        if (!isHomePlanet(G, x) && !isStation(G, x)) opts.push_back({x, planetName(x), ""});
    if (!opts.empty() && unitsAvailable(G, d, UnitType::Infantry) > 0) {
        opts.push_back({-1, "No infantry", ""});
        const int x = fpick(d, Ab::DeepwroughtAgent, "Doctor Carrina: put 1 infantry, coexisting, on which of " + pn(researcher) + "'s planets?", opts, "",
                            false, true);
        if (x >= 0) {
            addUnits(d, UnitType::Infantry, 1, G.planets[size_t(x)].system, x);
            startCoexisting(d, x);
        }
    }
}

// (fixes 27) Doctor Carrina when the researcher meets the prerequisite with a specialty planet: the Deepwrought may exhaust
// her; the researcher then decides whether to ignore it (keeping the planet ready) - and only if they do, the infantry.
bool Rules::carrinaSpare(int researcher, int tech) {
    const int d = playerOfFaction(G, "deepwrought");
    if (d < 0 || d == researcher || !canUseAgent(d, "deepwrought") || carrinaQuiet_[d] == G.round) return false;
    const std::string prompt = "Exhaust Doctor Carrina so " + pn(researcher) + " can ignore 1 prerequisite of " + kTechs[tech].name + "?";
    const std::string why = "They could use a specialty planet instead; if they ignore it, you may place 1 infantry, coexisting, on one of "
                            "their non-home planets.";
    curFor_ = researcher;
    int c = 0;
    if (G.players[size_t(d)].human)
        c = fpick(d, Ab::DeepwroughtAgent, prompt,
                  {{1, "Exhaust Doctor Carrina", why}, {0, "No", ""}, {3, "No, and don't ask again this round", "You're still asked when they need her."}});
    else
        c = fyes(d, Ab::DeepwroughtAgent, prompt, why) ? 1 : 0;
    curFor_ = -1;
    if (c == 3) carrinaQuiet_[d] = int16_t(G.round);
    if (c != 1) return false;
    spendAgent(d, "deepwrought");
    curFor_ = d;
    const bool take = fyes(researcher, Ab::DeepwroughtAgent, "Doctor Carrina: ignore 1 prerequisite of " + std::string(kTechs[tech].name) + " (keep a specialty planet ready)?",
                           "If you do, " + pn(d) + " may place 1 infantry, coexisting, on one of your non-home planets.");
    curFor_ = -1;
    if (!take) {
        say(researcher, pn(researcher) + " doesn't take Doctor Carrina's help.");
        return false;
    }
    carrinaInfantry(d, researcher);
    return true;
}

// Aello (Deepwrought commander): another player researching for resources may pay 1 less; the Deepwrought then gain a
// commodity (or turn one into a trade good).
// (fixes 27, the Deepwrought review) every player who can use Aello (the Deepwrought, an Alliance / Imperia holder) is a
// "you" of the card - each one's reduction is offered; a person chooses "gain 1 commodity or convert 1" (bots: gain while
// there is room, else convert, as before).
int Rules::aello(int researcher, int cost) {
    if (cost <= 0) return cost;
    if (G.fixes >= 27) {
        for (int d : initiativeOrder(G)) {
            if (cost <= 0) break;
            if (d == researcher || G.players[size_t(d)].eliminated || !commanderUsable(G, d, "deepwrought")) continue;
            curFor_ = d;
            const bool take = fyes(researcher, Ab::DeepwroughtCommander, "Aello: research for 1 resource less?",
                                   pn(d) + " gains 1 commodity (or turns 1 into a trade good).");
            curFor_ = -1;
            if (!take) continue;
            Player& dp = G.players[size_t(d)];
            const bool room = dp.commodities < commodityValue(G, dp), conv = dp.commodities > 0;
            int how = room ? 1 : conv ? 2 : 0;
            if (room && conv && dp.human)
                how = fpick(d, Ab::DeepwroughtCommander, "Aello: gain 1 commodity or convert 1 of yours to a trade good?",
                            {{1, "Gain 1 commodity", ""}, {2, "Convert 1 commodity to a trade good", ""}});
            if (how == 1) {
                { const int cb_ = dp.commodities; ++dp.commodities; commodityGained(d, cb_); }
                say(d, pn(d) + " gains 1 commodity (Aello).");
            } else if (how == 2) {
                --dp.commodities;
                gainTG(d, 1);
                say(d, pn(d) + " turns 1 commodity into a trade good (Aello).");
            }
            --cost;
        }
        return cost;
    }
    const int d = playerOfFaction(G, "deepwrought");
    if (d < 0 || d == researcher || !commanderUsable(G, d, "deepwrought")) return cost;
    if (!fyes(researcher, Ab::DeepwroughtCommander, "Aello: research for 1 resource less?", "The Deepwrought gain 1 commodity (or a trade good).")) return cost;
    Player& dp = G.players[size_t(d)];
    if (dp.commodities < commodityValue(G, dp)) {
        { const int cb_ = dp.commodities; ++dp.commodities; commodityGained(d, cb_); }
        say(d, pn(d) + " gains 1 commodity (Aello).");
    } else if (dp.commodities > 0) {
        --dp.commodities;
        gainTG(d, 1);
        say(d, pn(d) + " turns 1 commodity into a trade good (Aello).");
    }
    return cost - 1;
}

// Start of the status phase: Hydrothermal Mining (trade goods per ocean card), Radical Advancement (swap a technology
// for one of its colour with exactly 1 more prerequisite), Share Knowledge's technology goes back.
void Rules::statusStart6() {
    firmamentStatusStart();  // Neural Parasite, The Sowing / The Reaping, Seethe
    for (int p = 0; p < G.numPlayers; ++p) {
        Player& pl = G.players[size_t(p)];
        if (pl.eliminated) continue;
        // (0.44.6, fixes 10: every ocean card in play - a Nekro copy counts the Deepwrought's)
        int oceans = pl.oceans;
        if (G.fixes >= 10) {
            oceans = 0;
            for (int q = 0; q < G.numPlayers; ++q) oceans += G.players[size_t(q)].oceans;
        }
        if (owns(p, "hydrothermal") && oceans > 0) {
            gainTG(p, oceans);
            say(p, pn(p) + " gains " + std::to_string(oceans) + " trade good(s) (Hydrothermal Mining).");
        }
        if (owns(p, "radical")) {
            std::vector<Option> opts;  // id = old * 1000 + new
            // (0.44.9, fixes 13 - the owner's answer G1: faction technologies too - Radical Advancement may even replace
            // itself; a faction technology taken has to be one the player could research, i.e. their own)
            const bool v13 = G.fixes >= 13;
            for (int16_t t : pl.techs) {
                const TechDef& a = kTechs[t];
                if (a.unitUpgrade || (a.faction >= 0 && !v13) || a.color == TechColor::None) continue;
                const int n = a.req[0] + a.req[1] + a.req[2] + a.req[3];
                for (int u = 0; u < kTechCount; ++u) {
                    const TechDef& b = kTechs[u];
                    if (b.color != a.color || b.unitUpgrade || (b.faction >= 0 && !v13) || pl.hasTech(u) || !techAvailableToResearch(G, p, u)) continue;
                    if (b.source != Source::Base && b.source != Source::PoK && !(v13 && b.faction >= 0)) continue;
                    if (b.req[0] + b.req[1] + b.req[2] + b.req[3] != n + 1) continue;
                    opts.push_back({t * 1000 + u, std::string(a.name) + " -> " + b.name, techSummary(u)});
                }
            }
            if (!opts.empty()) {
                opts.push_back({-1, "Keep them", ""});
                const int c = fpick(p, Ab::Radical, "Radical Advancement: replace a technology?", opts);
                if (c >= 0) {
                    pl.techs.erase(std::find(pl.techs.begin(), pl.techs.end(), int16_t(c / 1000)));
                    // (0.44.6, fixes 10: the new one is gained, not researched - LRR 90.5; Anti-Intellectual Revolution
                    // doesn't strike - and the old one's exhausted mark goes with it)
                    if (G.fixes >= 10)
                        pl.exhaustedTechs.erase(std::remove(pl.exhaustedTechs.begin(), pl.exhaustedTechs.end(), int16_t(c / 1000)), pl.exhaustedTechs.end());
                    researchTech(p, c % 1000, G.fixes < 10);
                    say(p, pn(p) + " replaces " + kTechs[c / 1000].name + " with " + kTechs[c % 1000].name + " (Radical Advancement).");
                }
            }
        }
    }
}
void Rules::statusEnd6() {
    for (int p = 0; p < G.numPlayers; ++p) {
        Player& pl = G.players[size_t(p)];
        pl.oceansExhausted = 0;  // (ocean cards ready with the planets)
        if (pl.sharedTech >= 0) {  // Share Knowledge: the technology and the note go back
            auto it = std::find(pl.techs.begin(), pl.techs.end(), pl.sharedTech);
            if (it != pl.techs.end()) pl.techs.erase(it);
            const int d = playerOfFaction(G, "deepwrought");
            say(p, pn(p) + " gives back " + kTechs[pl.sharedTech].name + " (Share Knowledge).");
            pl.sharedTech = -1;
            for (size_t i = 0; i < pl.notesInPlay.size(); ++i)
                if (d >= 0 && noteOwner(pl.notesInPlay[i]) == d && noteType(pl.notesInPlay[i]) == kNoteFaction) {
                    const int16_t n = pl.notesInPlay[i];
                    pl.notesInPlay.erase(pl.notesInPlay.begin() + long(i));
                    G.players[size_t(d)].notes.push_back(n);
                    break;
                }
        }
        oceanLimit(p);
    }
    breachStatus();  // Crimson Rebellion: active breaches may be removed; Sever goes back
    firmamentStatusEnd();  // Malevolency
}

// Eanautic (Deepwrought mech): when another player activates its system, a coexisting mech (and the infantry on its
// planet) may slip away to a planet in the Deepwrought home system.
void Rules::eanautic(int active, int sys) {
    for (int p = 0; p < G.numPlayers; ++p) {
        if (p == active || !fac(p, "deepwrought") || !mechAbilities(G)) continue;
        const int home = G.players[size_t(p)].homeSystem;
        if (home < 0 || home == sys) continue;
        int dest = -1;
        for (int16_t x : G.systems[size_t(home)].planets)
            if (G.planets[size_t(x)].owner == p) dest = x;
        if (dest < 0) continue;
        for (int16_t x : G.systems[size_t(sys)].planets) {
            PlanetState& ps = G.planets[size_t(x)];
            if (!((ps.coexist >> p) & 1) || !ps.units[size_t(p)].count(UnitType::Mech)) continue;
            curFor_ = active;
            const bool go = fyes(p, Ab::Eanautic, "Eanautic: take your mech and infantry on " + planetName(x) + " home?",
                                 pn(active) + " activated " + sysName(sys) + ".");
            curFor_ = -1;
            if (!go) continue;
            for (UnitType t : {UnitType::Mech, UnitType::Infantry}) {
                const int k = ps.units[size_t(p)].count(t);
                ps.units[size_t(p)].remove(t, k, false);
                G.planets[size_t(dest)].units[size_t(p)].add(t, k);
            }
            say(p, pn(p) + "'s Eanautic slips home from " + planetName(x) + ".", sys);
        }
    }
}

// Component actions: Ta Zern (Deepwrought hero), Share Knowledge (their note), Visionaria Select (their breakthrough).
std::vector<Option> Rules::factionActionOptions7(int p) {
    std::vector<Option> o;
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    if (fac(p, "deepwrought") && heroUnlocked(G, p)) add(Ab::DeepwroughtHero, "Hero: Ta Zern", leaderText(findLeader("deepwroughthero")));
    const int d = playerOfFaction(G, "deepwrought");
    if (d >= 0 && d != p && holdsNote(G, p, d, kNoteFaction) && G.players[size_t(p)].sharedTech < 0) {
        bool any = false;
        for (int16_t t : G.players[size_t(d)].techs)
            if (kTechs[t].faction < 0 && !kTechs[t].unitUpgrade && !G.players[size_t(p)].hasTech(t)) any = true;
        if (any) add(Ab::ShareKnowledge, "Play Share Knowledge", factionNoteText(G.players[size_t(d)].faction, kNoteFaction));
    }
    if (breakthroughIs(G, p, "deepwrought") && !G.players[size_t(p)].btExhausted)
        add(Ab::BtDeepwrought, "Breakthrough: Visionaria Select", breakthroughOf("deepwrought")->text);
    return o;
}

bool Rules::factionAction7(int p, Ab ab) {
    Player& pl = G.players[size_t(p)];
    switch (ab) {
        case Ab::DeepwroughtHero: {  // purge a technology from the game; those who lose it research another
            std::vector<Option> opts;
            for (int t = 0; t < kTechCount; ++t) {
                const TechDef& d = kTechs[t];
                if (d.unitUpgrade || d.faction >= 0 || d.color == TechColor::None) continue;
                if (d.source != Source::Base && d.source != Source::PoK) continue;
                if (G.fixes >= 9 && !techImplemented(t)) continue;  // (0.44.5: not the Codex Keleres I.I.H.Q.)
                if (std::find(G.purgedTechs.begin(), G.purgedTechs.end(), int16_t(t)) != G.purgedTechs.end()) continue;
                int owners = 0;
                for (const Player& q : G.players) owners += q.hasTech(t) ? 1 : 0;
                opts.push_back({t, d.name, std::to_string(owners) + " player(s) own it"});
            }
            opts.push_back({-1, "Cancel", ""});
            const int t = fpick(p, Ab::DeepwroughtHero, "Ta Zern: purge which technology from the game?", opts);
            if (t < 0) return false;
            purgeHero(p);
            G.purgedTechs.push_back(int16_t(t));
            abilityEvent6(E, p, "Ta Zern", pn(p) + " purges " + kTechs[t].name + " from the galaxy (Ta Zern).");
            for (int q : initiativeOrder(G)) {
                auto& v = G.players[size_t(q)].techs;
                auto it = std::find(v.begin(), v.end(), int16_t(t));
                if (it == v.end()) continue;
                v.erase(it);
                researchStep(q, 0, "Ta Zern", t);
            }
            return true;
        }
        case Ab::ShareKnowledge: {
            const int d = playerOfFaction(G, "deepwrought");
            if (d < 0) return false;
            std::vector<Option> opts;
            for (int16_t t : G.players[size_t(d)].techs)
                if (kTechs[t].faction < 0 && !kTechs[t].unitUpgrade && !pl.hasTech(t)) opts.push_back({t, kTechs[t].name, techSummary(t)});
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            const int t = fpick(p, Ab::ShareKnowledge, "Share Knowledge: borrow which technology until the status phase?", opts);
            if (t < 0) return false;
            const int note = makeNote(d, kNoteFaction);
            auto it = std::find(pl.notes.begin(), pl.notes.end(), int16_t(note));
            if (it == pl.notes.end()) return false;
            pl.notes.erase(it);
            pl.notesInPlay.push_back(int16_t(note));
            researchTech(p, t, G.fixes < 27);  // (fixes 27, the Deepwrought review: "gain" - not researched)
            pl.sharedTech = int16_t(t);
            say(p, pn(p) + " borrows " + kTechs[t].name + " from " + pn(d) + " (Share Knowledge).");
            return true;
        }
        case Ab::BtDeepwrought: {  // Visionaria Select: the others may buy a generic technology - we get it too
            pl.btExhausted = true;
            abilityEvent6(E, p, "Visionaria Select", pn(p) + " opens Visionaria Select.");
            for (int q : initiativeOrder(G)) {
                if (q == p || G.players[size_t(q)].eliminated || G.players[size_t(q)].tradeGoods < 3 || G.players[size_t(q)].notes.empty()) continue;
                if (G.fixes >= 8) {  // 0.44.4: only players with a note to give and something to research are asked
                    bool note = false;
                    for (int16_t n : G.players[size_t(q)].notes) note = note || noteOwner(n) != p;
                    if (!note) continue;
                    onlyGeneric_ = true;
                    researchDry_ = true;
                    const bool can = researchStep(q, 0, "Visionaria Select");
                    researchDry_ = false;
                    onlyGeneric_ = false;
                    if (!can) continue;
                }
                curFor_ = p;
                const bool buy = fyes(q, Ab::BtDeepwrought, "Visionaria Select: spend 3 trade goods and give " + pn(p) +
                                                               " a promissory note to research a technology?",
                                      pn(p) + " gains the same technology.");
                curFor_ = -1;
                if (!buy) continue;
                std::vector<Option> notes;
                for (int16_t n : G.players[size_t(q)].notes)
                    if (noteOwner(n) != p) notes.push_back({n, noteNameOf(G, n), ""});
                if (notes.empty()) continue;
                const int n = fpick(q, Ab::BtDeepwrought, "Give " + pn(p) + " which promissory note?", notes);
                G.players[size_t(q)].tradeGoods = int8_t(G.players[size_t(q)].tradeGoods - 3);
                giveNote(q, p, n);
                const std::vector<int16_t> before = G.players[size_t(q)].techs;
                onlyGeneric_ = true;
                researchStep(q, 0, "Visionaria Select");
                onlyGeneric_ = false;
                for (int16_t t : G.players[size_t(q)].techs)
                    if (std::find(before.begin(), before.end(), t) == before.end() && !pl.hasTech(t)) {
                        researchTech(p, t, G.fixes < 8);  // (0.44.4: gained, not researched - no "when you research" effects)
                        say(p, pn(p) + " gains " + kTechs[t].name + " too (Visionaria Select).");
                    }
            }
            return true;
        }
        default: return factionAction8(p, ab);  // (factionAction8 passes on to factionAction9)
    }
}

// ================================================================== Crimson Rebellion
// The breach tokens (fixes 24, owner's faction debug pass): the Crimson have 7 (the Sorrow's starting breach is one of
// them). When all 7 are out, an inactive breach is moved from elsewhere; active breaches must be removed or flipped
// before they can be placed again (Thunder's Edge rulebook, "Breaches").
constexpr int kBreachTokens = 7;
bool Rules::breachPlaceable(int sys) const {
    if (G.fixes < 24) return true;
    int out = 0;
    bool spare = false;
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (G.systems[s].breach) {
            ++out;
            if (G.systems[s].breach == 1 && int(s) != sys) spare = true;
        }
    return out < kBreachTokens || spare;
}

void Rules::placeBreach(int p, int sys, bool active) {
    SystemState& sy = G.systems[size_t(sys)];
    if (sy.breach) return;
    if (G.fixes >= 24) {
        int out = 0;
        std::vector<Option> spare;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (G.systems[s].breach) {
                ++out;
                if (G.systems[s].breach == 1) spare.push_back({int(s), "The inactive breach in " + sysName(int(s)), ""});
            }
        if (out >= kBreachTokens) {
            if (spare.empty()) {
                say(p, "All 7 breaches are active - no new breach can open in " + sysName(sys) + ".", sys);
                return;
            }
            const int from = spare.size() == 1 ? spare[0].id
                                               : fpick(p, Ab::MoveBreach, "All 7 breaches are out: which inactive breach moves to " + sysName(sys) + "?",
                                                       spare, "Active breaches can't be moved - only inactive ones.");
            if (from < 0 || from >= int(G.systems.size()) || G.systems[size_t(from)].breach != 1) return;
            G.systems[size_t(from)].breach = 0;
            say(p, "The inactive breach in " + sysName(from) + " is moved (all 7 breaches were out).", from);
        }
    }
    sy.breach = int8_t(active ? 2 : 1);
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != p && hasUnits(G, q, sys) && p >= 0 && p < G.numPlayers) G.players[size_t(p)].breachedOthers = true;
    say(p, std::string("A") + (active ? "n active" : "n inactive") + " breach opens in " + sysName(sys) + ".", sys);
    if (active) computeAdjacency(G);
}

void Rules::flipBreach(int p, int sys) {
    SystemState& sy = G.systems[size_t(sys)];
    if (!sy.breach) return;
    sy.breach = int8_t(sy.breach == 2 ? 1 : 2);
    say(p, "The breach in " + sysName(sys) + (sy.breach == 2 ? " flares open." : " closes over."), sys);
    computeAdjacency(G);
}

// Incursion: activating a system with a breach, the Crimson may flip it. Sundered is checked after the move.
void Rules::incursion(int p, int sys) {
    if (!fac(p, "crimson") || !G.systems[size_t(sys)].breach) return;
    const bool active = G.systems[size_t(sys)].breach == 2;
    if (fyes(p, Ab::Incursion, std::string("Incursion: ") + (active ? "close" : "open") + " the breach in " + sysName(sys) + "?",
             "Systems with active breaches are adjacent to each other."))
        flipBreach(p, sys);
}

void Rules::sundered(int p, int sys) {  // other players' units in the Crimson home system are destroyed
    for (int c = 0; c < G.numPlayers; ++c) {
        if (c == p || !fac(c, "crimson") || G.players[size_t(c)].homeSystem != sys) continue;
        Force& f = G.systems[size_t(sys)].space[size_t(p)];
        bool any = false;
        for (int t = 0; t < kUnitTypes; ++t)
            if (f.n[t]) {
                destroyUnits(p, UnitType(t), f.n[t], sys, -1, c, "Sundered");
                any = true;
            }
        if (any) say(c, pn(p) + "'s units are torn apart entering the Rebellion's home (Sundered).", sys);
    }
}

// Sundered, the rest of the card (fixes 24, owner's faction debug pass): "other players' units that move or are placed
// into your home system are destroyed" - however they got there (placed by an ability or card, moved outside the
// tactical movement step, landed on Ahk Creuxx). Checked before every question, like the fleet pool.
void Rules::sunderedCheck() {
    if (G.fixes < 24 || !(G.modules & kModThundersEdge) || inSunderedCheck_ || G.round < 1 || G.phase == Phase::GameOver) return;
    struct Guard {
        bool& f;
        ~Guard() { f = false; }
    } guard{inSunderedCheck_};
    inSunderedCheck_ = true;
    for (int c = 0; c < G.numPlayers; ++c) {
        if (!fac(c, "crimson") || G.players[size_t(c)].eliminated) continue;
        const int home = G.players[size_t(c)].homeSystem;
        if (home < 0 || home >= int(G.systems.size())) continue;
        // (units that survive being destroyed - the Li-Zho flagship, a Pustule save - aren't tried again and again:
        // only something new there is checked)
        auto signature = [&]() {
            uint64_t h = 0;
            for (int q = 0; q < G.numPlayers; ++q) {
                if (q == c) continue;
                for (int t = 0; t < kUnitTypes; ++t) h = h * 1000003u + G.systems[size_t(home)].space[size_t(q)].n[t];
                for (int16_t x : G.systems[size_t(home)].planets)
                    for (int t = 0; t < kUnitTypes; ++t) h = h * 1000003u + G.planets[size_t(x)].units[size_t(q)].n[t];
            }
            return h;
        };
        const uint64_t before = signature();
        if (before == sunderedSig_[c]) continue;
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == c) continue;
            int had = 0, left = 0;
            for (int t = 0; t < kUnitTypes; ++t) {
                had += G.systems[size_t(home)].space[size_t(q)].n[t];
                for (int16_t x : G.systems[size_t(home)].planets) had += G.planets[size_t(x)].units[size_t(q)].n[t];
            }
            if (!had) continue;
            bool any = false;
            for (int t = 0; t < kUnitTypes; ++t) {
                const int k = G.systems[size_t(home)].space[size_t(q)].n[t];
                if (k) {
                    destroyUnits(q, UnitType(t), k, home, -1, c, "Sundered");
                    any = true;
                }
            }
            const std::vector<int16_t> planets = G.systems[size_t(home)].planets;
            for (int16_t x : planets)
                for (int t = 0; t < kUnitTypes; ++t) {
                    const int k = G.planets[size_t(x)].units[size_t(q)].n[t];
                    if (k) {
                        destroyUnits(q, UnitType(t), k, home, x, c, "Sundered");
                        any = true;
                    }
                }
            for (int t = 0; t < kUnitTypes; ++t) {
                left += G.systems[size_t(home)].space[size_t(q)].n[t];
                for (int16_t x : G.systems[size_t(home)].planets) left += G.planets[size_t(x)].units[size_t(q)].n[t];
            }
            if (any && left < had) say(c, pn(q) + "'s units are torn apart in the Rebellion's home (Sundered).", home);
        }
        sunderedSig_[c] = signature();
    }
}

// End of any combat: the Exile destroyers may open a breach there; Ahk Siever pays out a commodity.
void Rules::crimsonCombatEnd(int att, int def, int sys) {
    // 0.58.2 (fixes 21, tester: "Crimson Rebellion commander is not triggering" - held through the Alliance or the Yin
    // breakthrough): every player who can use Ahk Siever, not only the Crimson, and they choose
    // (fixes 24, owner's faction debug pass: "a combat between any players" - not a fight against neutral units)
    const bool players = att >= 0 && att < G.numPlayers && def >= 0 && def < G.numPlayers;
    if (G.fixes >= 21 && (G.fixes < 24 || players))
        for (int c = 0; c < G.numPlayers; ++c) {
            Player& pl = G.players[size_t(c)];
            if (pl.eliminated || !commanderUsable(G, c, "crimson")) continue;
            const bool room = pl.commodities < commodityValue(G, pl), some = pl.commodities > 0;
            if (!room && !some) continue;
            std::vector<Option> opts;
            if (room) opts.push_back({1, "Gain 1 commodity", std::to_string(pl.commodities) + " of " + std::to_string(commodityValue(G, pl))});
            if (some) opts.push_back({2, "Turn 1 commodity into a trade good", ""});
            opts.push_back({0, "Neither", ""});
            curValue_ = pl.commodities * 100 + commodityValue(G, pl);
            const int c2 = fpick(c, Ab::CrimsonCommander, "Ahk Siever (the Crimson Rebellion's commander): a combat has ended.", opts,
                                 leaderText(findLeader("crimsoncommander")));
            curValue_ = 0;
            if (c2 == 1) {
                { const int cb_ = pl.commodities; ++pl.commodities; commodityGained(c, cb_); }
                say(c, pn(c) + " gains 1 commodity from the fighting (Ahk Siever).");
            } else if (c2 == 2) {
                --pl.commodities;
                gainTG(c, 1);
                say(c, pn(c) + " turns 1 commodity into a trade good (Ahk Siever).");
            }
        }
    for (int c = 0; c < G.numPlayers; ++c) {
        if (!fac(c, "crimson") || G.players[size_t(c)].eliminated) continue;
        if (G.fixes < 21 && commanderUsable(G, c, "crimson")) {
            Player& pl = G.players[size_t(c)];
            if (pl.commodities < commodityValue(G, pl)) { const int cb_ = pl.commodities; ++pl.commodities; commodityGained(c, cb_); }
            else if (pl.commodities > 0) {
                --pl.commodities;
                gainTG(c, 1);
            }
            say(c, pn(c) + " profits from the fighting (Ahk Siever).");
        }
        if (G.systems[size_t(sys)].breach || !breachPlaceable(sys)) continue;
        const bool two = owns(c, "exile2");
        bool near = G.systems[size_t(sys)].space[size_t(c)].count(UnitType::Destroyer) > 0;
        for (int16_t a : G.systems[size_t(sys)].adj) {
            if (G.systems[size_t(a)].space[size_t(c)].count(UnitType::Destroyer)) near = true;
            if (two)
                for (int16_t b : G.systems[size_t(a)].adj)
                    if (G.systems[size_t(b)].space[size_t(c)].count(UnitType::Destroyer)) near = true;
        }
        if (!near) continue;
        std::vector<Option> opts{{1, "An inactive breach", ""}};
        if (two) opts.push_back({2, "An active breach", "Adjacent to every other active breach."});
        opts.push_back({0, "No breach", ""});
        const int k = fpick(c, Ab::Exile, std::string(two ? "Exile II" : "Exile I") + ": open a breach in " + sysName(sys) + "?", opts);
        if (k > 0) placeBreach(c, sys, k == 2);
    }
    (void)att;
    (void)def;
}

// End of the status phase: anyone with ships in an active breach's system may close it for good.
void Rules::breachStatus() {
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (G.systems[s].breach != 2) continue;
        for (int q : initiativeOrder(G)) {
            if (!hasShips(G, q, int(s))) continue;
            curValue_ = int(s);  // (for the bots: which system)
            const bool remove = fyes(q, Ab::RemoveBreach, "Remove the active breach in " + sysName(int(s)) + "?", "It is gone for good.");
            curValue_ = 0;
            if (!remove) continue;
            G.systems[s].breach = 0;
            say(q, pn(q) + " seals the breach in " + sysName(int(s)) + ".", int(s));
            computeAdjacency(G);
            break;
        }
    }
    if (G.severSystem >= 0) {  // Sever goes back
        G.severSystem = -1;
        const int c = playerOfFaction(G, "crimson");
        for (int q = 0; q < G.numPlayers && c >= 0; ++q) {
            auto& v = G.players[size_t(q)].notesInPlay;
            for (size_t i = 0; i < v.size(); ++i)
                if (noteOwner(v[i]) == c && noteType(v[i]) == kNoteFaction) {
                    G.players[size_t(c)].notes.push_back(v[i]);
                    v.erase(v.begin() + long(i));
                    break;
                }
        }
    }
}

// Subatomic Splicer: one of your ships destroyed - produce one like it at your home space dock (paying for it).
// (0.44.5, fixes 9: fighters too - "one of your ships", asked once for the fighters lost together - and not while the
// home system is blockaded, LRR 14.1)
void Rules::subatomicSplicer(int p, UnitType t, int k) {
    if (!owns(p, "subatomic") || !isShip(t) || removing_) return;
    if (t == UnitType::Fighter && G.fixes < 9) return;
    const int home = G.players[size_t(p)].homeSystem;
    if (home < 0) return;
    bool dock = false;
    for (int16_t x : G.systems[size_t(home)].planets) dock = dock || G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock) > 0;
    if (!dock) return;
    if (G.fixes >= 9 && blockaded(G, p, home)) return;
    if (t == UnitType::Fighter) {
        int n = std::min(k, unitsAvailable(G, p, t));
        if (n <= 0) return;
        auto costOf = [&](int m) { return (m * unitCost2(G, p, t) + 1) / 2; };
        if (G.fixes >= 24) {  // (faction debug pass: each fighter lost is its own "may" - choose how many come back)
            while (n > 0 && spendable(G, p, false) < costOf(n)) --n;
            if (n <= 0) return;
        }
        int cost = costOf(n);
        if (spendable(G, p, false) < cost) return;
        if (G.fixes >= 24 && n > 1) {
            std::vector<Option> opts;
            for (int m = n; m >= 1; --m)
                opts.push_back({m, std::to_string(m) + " fighter" + (m > 1 ? "s" : ""),
                                std::to_string(costOf(m)) + " resource" + (costOf(m) > 1 ? "s" : "")});
            opts.push_back({0, "None", ""});
            n = fpick(p, Ab::Subatomic, "Subatomic Splicer: how many of the " + std::to_string(k) + " fighters lost do you produce at home?", opts,
                      "Two fighters cost the same as one.");
            if (n <= 0) return;
            cost = costOf(n);
        } else if (!fyes(p, Ab::Subatomic, "Subatomic Splicer: produce " + std::to_string(n) + " fighter" + (n > 1 ? "s" : "") + " at home for " +
                                               std::to_string(cost) + " resource" + (cost > 1 ? "s" : "") + "?", "")) {
            return;
        }
        if (!pay(p, cost, false, "Subatomic Splicer", false)) return;
        addUnits(p, t, n, home, -1);
        say(p, pn(p) + " rebuilds " + std::to_string(n) + " fighter" + (n > 1 ? "s" : "") + " at home (Subatomic Splicer).", home);
        phantomProducedOne(p, home, t, n);  // Homesick Phantom
        return;
    }
    for (int i = 0; i < k && unitsAvailable(G, p, t) > 0; ++i) {
        const int cost = (unitCost2(G, p, t) + 1) / 2;
        if (spendable(G, p, false) < cost) return;
        if (!fyes(p, Ab::Subatomic, std::string("Subatomic Splicer: produce a ") + unitTypeName(t) + " at home for " + std::to_string(cost) + " resources?", ""))
            return;
        if (!pay(p, cost, false, "Subatomic Splicer", false)) return;
        addUnits(p, t, 1, home, -1);
        say(p, pn(p) + " rebuilds a " + unitTypeName(t) + " at home (Subatomic Splicer).", home);
        phantomProducedOne(p, home, t, 1);  // Homesick Phantom
    }
}

// Homesick Phantom (Crimson hero): new ships may wait on the card; at the start of a space combat they all arrive.
// (fixes 24, owner's faction debug pass - the card: "any of those ships": all of them, some of them (how many of each
// type) or none; and after every way of producing ships - Subatomic Splicer, Salvage Operations, Freelancers,
// Integrated Economy call this too)
void Rules::phantomProduced(int p, int sys, const int counts[kUnitTypes]) {
    if (!fac(p, "crimson") || !heroUnlocked(G, p) || sys < 0) return;
    int take[kUnitTypes] = {};
    int ships = 0;
    for (int t : kShipIdx) {
        take[t] = std::min<int>(counts[t], G.systems[size_t(sys)].space[size_t(p)].n[t]);
        ships += take[t];
    }
    if (!ships) return;
    const std::string why = "At the start of a space combat you may bring them all into it.";
    if (G.fixes >= 24 && ships > 1) {
        const int c = fpick(p, Ab::CrimsonHero, "Homesick Phantom: put the new ships on the card?",
                            {{1, "All of them", ""}, {2, "Some of them", "Choose how many of each"}, {0, "None", ""}}, why);
        if (c == 0) return;
        if (c == 2)
            for (int t : kShipIdx) {
                if (!take[t]) continue;
                std::vector<Option> opts;
                for (int m = take[t]; m >= 1; --m) opts.push_back({m, std::to_string(m), ""});
                opts.push_back({0, "None", ""});
                take[t] = fpick(p, Ab::CrimsonHero, std::string("Homesick Phantom: how many of the new ") + unitTypeName(UnitType(t), true) +
                                                        " go on the card?", opts);
            }
    } else if (!fyes(p, Ab::CrimsonHero, "Homesick Phantom: put the new ships on the card?", why)) {
        return;
    }
    Player& pl = G.players[size_t(p)];
    int moved = 0;
    for (int t : kShipIdx) {
        const int k = std::min<int>(take[t], G.systems[size_t(sys)].space[size_t(p)].n[t]);
        if (k <= 0) continue;
        G.systems[size_t(sys)].space[size_t(p)].remove(UnitType(t), k, false);
        pl.phantom.add(UnitType(t), k);
        moved += k;
    }
    if (moved) say(p, pn(p) + "'s new ships vanish into the Homesick Phantom.", sys);
}
void Rules::phantomProducedOne(int p, int sys, UnitType t, int k) {  // (fixes 24: the productions outside productionCore)
    if (G.fixes < 24 || !isShip(t) || k <= 0) return;
    int counts[kUnitTypes] = {};
    counts[int(t)] = k;
    phantomProduced(p, sys, counts);
}
void Rules::phantomCombat(int att, int def, int sys) {
    for (int p : {att, def}) {
        if (p < 0 || p >= G.numPlayers || !fac(p, "crimson") || !heroUnlocked(G, p) || G.players[size_t(p)].phantom.empty()) continue;
        if (!fyes(p, Ab::CrimsonHero, "Homesick Phantom: purge it to bring your waiting ships into " + sysName(sys) + "?", "")) continue;
        purgeHero(p);
        Player& pl = G.players[size_t(p)];
        for (int t = 0; t < kUnitTypes; ++t)
            if (pl.phantom.n[t]) G.systems[size_t(sys)].space[size_t(p)].add(UnitType(t), pl.phantom.n[t]);
        pl.phantom = Force();
        abilityEvent6(E, p, "Homesick Phantom", pn(p) + "'s phantom fleet bursts into " + sysName(sys) + "!", sys);
    }
}

// Revenant (Crimson mech): with an active breach in the active system, 1 mech may join the invasion from nowhere.
// Returns the mechs p had in the space area before it came (-1: none came), so the invasion can send it back if it
// isn't committed. (fixes 24, owner's faction debug pass - the card: DEPLOY during the Commit Ground Forces step, to
// any planet there: asked after bombardment (invasion), offered for p's own planets too, and a Revenant that isn't
// landed goes back to the reinforcements instead of staying in space.)
int Rules::revenant(int p, int sys) {
    if (!fac(p, "crimson") || !mechAbilities(G) || G.systems[size_t(sys)].breach != 2 || unitsAvailable(G, p, UnitType::Mech) <= 0) return -1;
    bool target = G.fixes >= 24 && !G.systems[size_t(sys)].planets.empty();
    for (int16_t x : G.systems[size_t(sys)].planets)
        if (G.planets[size_t(x)].owner != p) target = true;
    const char* why = G.fixes >= 24 ? "Deploy: it lands on a planet in this system now, or goes back to your reinforcements." : "";
    if (!target || !fyes(p, Ab::Revenant, "Revenant: commit 1 mech from your reinforcements through the breach?", why)) return -1;
    const int before = G.systems[size_t(sys)].space[size_t(p)].count(UnitType::Mech);
    addUnits(p, UnitType::Mech, 1, sys, -1);
    say(p, pn(p) + "'s Revenant steps out of the breach in " + sysName(sys) + ".", sys);
    return before;
}

// Ahk Ravin (fixes 24, owner's faction debug pass - the card: the chosen player MAY swap 2 of their ships - fighters
// are ships too - in any systems, and may transport units when they swap): the players with ships in 2 systems or more
std::vector<int> Rules::ravinPlayers() const {
    std::vector<int> v;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (G.players[size_t(q)].eliminated) continue;
        int systems = 0;
        for (size_t s = 0; s < G.systems.size() && systems < 2; ++s)
            if (G.systems[s].space[size_t(q)].ships()) ++systems;
        if (systems >= 2) v.push_back(q);
    }
    return v;
}

namespace {
void moveUnits(Force& src, Force& dst, int t, int k) {  // k units of one type with their damage and galvanize tokens
    k = std::min<int>(k, src.n[t]);
    if (k <= 0) return;
    const int undamaged = src.n[t] - src.dmg[t];
    const int dmgMoved = std::max(0, k - undamaged);
    const int galvMoved = std::min<int>(k, src.galv[t]);
    src.galv[t] = uint8_t(src.galv[t] - galvMoved);
    src.remove(UnitType(t), k, false);
    dst.add(UnitType(t), k);
    dst.dmg[t] = uint8_t(dst.dmg[t] + dmgMoved);
    dst.galv[t] = uint8_t(dst.galv[t] + galvMoved);
}
}  // namespace

// What one swapped ship takes along from the system it leaves (LRR 95.1: from the space area or its planets, up to its
// capacity; LRR 95.3: nothing from a system with the player's command token). The units chosen leave the system at once.
Force Rules::ravinCargo(int q, int sys, int type) {
    Force cargo;
    SystemState& sy = G.systems[size_t(sys)];
    int room = unitDef(G, q, UnitType(type)).capacity;
    if (room <= 0) return cargo;
    if (sy.hasToken(q)) {
        say(q, pn(q) + "'s " + unitTypeName(UnitType(type)) + " leaves " + sysName(sys) +
                   " empty: nothing can be picked up from a system with their command token (LRR 95.3).", sys);
        return cargo;
    }
    static const int kCargo[] = {int(UnitType::Infantry), int(UnitType::Mech), int(UnitType::Fighter)};
    while (room > 0) {
        std::vector<Option> opts;  // id = (planet + 1) * 16 + type (planet -1 = the space area)
        for (int t : kCargo) {
            if (sy.space[size_t(q)].n[t])
                opts.push_back({t, std::string(unitTypeName(UnitType(t), true)) + " in space",
                                std::to_string(sy.space[size_t(q)].n[t]) + " there"});
            if (t == int(UnitType::Fighter)) continue;
            for (int16_t x : sy.planets)
                if (G.planets[size_t(x)].units[size_t(q)].n[t])
                    opts.push_back({(x + 1) * 16 + t, std::string(unitTypeName(UnitType(t), true)) + " on " + planetName(x),
                                    std::to_string(G.planets[size_t(x)].units[size_t(q)].n[t]) + " there"});
        }
        if (opts.empty()) break;
        opts.push_back({-1, cargo.empty() ? "Nothing" : "Nothing more", ""});
        curValue_ = sys * 16 + type;  // (for the bots: which ship leaves which system)
        const int c = fpick(q, Ab::CrimsonAgent,
                            "Ahk Ravin: what does the " + std::string(unitTypeName(UnitType(type))) + " take along from " + sysName(sys) +
                                "? (room for " + std::to_string(room) + ")",
                            opts, "Units it doesn't take stay here - any that no longer fit are removed.");
        curValue_ = 0;
        if (c < 0) break;
        const int t = c % 16, x = c / 16 - 1;
        Force& from = x < 0 ? sy.space[size_t(q)] : G.planets[size_t(x)].units[size_t(q)];
        const int most = std::min<int>(room, from.n[t]);
        int k = most;
        if (most > 1) {
            std::vector<Option> counts;
            for (int m = most; m >= 1; --m) counts.push_back({m, std::to_string(m), ""});
            curValue_ = sys * 16 + type;
            k = fpick(q, Ab::CrimsonAgent, std::string("Ahk Ravin: how many ") + unitTypeName(UnitType(t), true) + " go along?", counts);
            curValue_ = 0;
        }
        if (k <= 0) break;
        moveUnits(from, cargo, t, k);
        room -= k;
    }
    return cargo;
}

std::vector<Option> Rules::factionActionOptions8(int p) {
    std::vector<Option> o;
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    if (canUseAgent(p, "crimson") && (G.fixes < 24 || !ravinPlayers().empty()))  // (fixes 24: only when someone can swap)
        add(Ab::CrimsonAgent, "Agent: Ahk Ravin", leaderText(findLeader("crimsonagent")));
    const int c = playerOfFaction(G, "crimson");
    if (c >= 0 && c != p && holdsNote(G, p, c, kNoteFaction) && G.severSystem < 0)
        add(Ab::Sever, "Play Sever", factionNoteText(G.players[size_t(c)].faction, kNoteFaction));
    if (breakthroughIs(G, p, "crimson") && !G.players[size_t(p)].btExhausted)
        add(Ab::BtCrimson, "Breakthrough: Resonance Generator", "Exhaust it: flip a breach, or open an active breach where you have units.");
    return o;
}

bool Rules::ahkRavin(int p) {
    std::vector<Option> who;
    for (int q : ravinPlayers()) who.push_back({q, pn(q), ""});
    if (who.empty()) return false;
    const int q = fpick(p, Ab::CrimsonAgent, "Ahk Ravin: which player may swap 2 of their ships?", who,
                        "They may also take fighters and ground forces along with the 2 ships.");
    spendAgent(p, "crimson");
    std::vector<Option> ships;  // id = system * 16 + type
    bool token = false;
    for (size_t s = 0; s < G.systems.size(); ++s)
        for (int t : kShipIdx)
            if (G.systems[s].space[size_t(q)].n[t]) {
                const bool tok = G.systems[s].hasToken(q) && unitDef(G, q, UnitType(t)).capacity > 0;
                token = token || tok;
                ships.push_back({int(s) * 16 + t, std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)),
                                 tok ? "Your command token is here: it can't take anything along (LRR 95.3)." : ""});
            }
    const std::string warn = token ? "A ship leaving a system with your command token can't take fighters or ground forces along "
                                     "(LRR 95.3) - any left there that no longer fit are removed."
                                   : "";
    ships.push_back({-1, "Don't swap", ""});
    const int a = fpick(q, Ab::CrimsonAgent, "Ahk Ravin: first ship to swap?", ships, warn);
    if (a < 0) {
        say(q, pn(q) + " doesn't swap any ships (Ahk Ravin).");
        return true;
    }
    std::vector<Option> rest;
    for (const Option& o : ships)
        if (o.id >= 0 && o.id / 16 != a / 16) rest.push_back(o);
    rest.push_back({-1, "Don't swap", ""});
    const int b = fpick(q, Ab::CrimsonAgent, "Ahk Ravin: swap it with which ship?", rest, warn);
    if (b < 0) {
        say(q, pn(q) + " doesn't swap any ships (Ahk Ravin).");
        return true;
    }
    const int sa = a / 16, sb = b / 16;
    Force cargoA = ravinCargo(q, sa, a % 16), cargoB = ravinCargo(q, sb, b % 16);
    auto aboard = [](const Force& f) {
        int n = 0;
        for (int t = 0; t < kUnitTypes; ++t) n += f.n[t];
        return n == 0 ? std::string() : " (" + std::to_string(n) + (n > 1 ? " units" : " unit") + " aboard)";
    };
    const std::string text = pn(q) + "'s " + unitTypeName(UnitType(a % 16)) + aboard(cargoA) + " in " + sysName(sa) + " and " +
                             unitTypeName(UnitType(b % 16)) + aboard(cargoB) + " in " + sysName(sb) + " trade places (Ahk Ravin).";
    Force& fa = G.systems[size_t(sa)].space[size_t(q)];
    Force& fb = G.systems[size_t(sb)].space[size_t(q)];
    moveShip(fa, fb, a % 16);
    moveShip(fb, fa, b % 16);
    for (int t = 0; t < kUnitTypes; ++t) {
        moveUnits(cargoA, fb, t, cargoA.n[t]);
        moveUnits(cargoB, fa, t, cargoB.n[t]);
    }
    say(q, text);
    fleetDirty_ = true;
    removeExcessCapacity(q, sa);
    removeExcessCapacity(q, sb);
    return true;
}

bool Rules::factionAction8(int p, Ab ab) {
    switch (ab) {
        case Ab::CrimsonAgent: {  // a player swaps 2 of their ships (and what they carry)
            if (G.fixes >= 24) return ahkRavin(p);
            std::vector<Option> who;
            for (int q = 0; q < G.numPlayers; ++q)
                if (!G.players[size_t(q)].eliminated) who.push_back({q, pn(q), ""});
            const int q = fpick(p, Ab::CrimsonAgent, "Ahk Ravin: which player swaps 2 of their ships?", who);
            std::vector<Option> ships;  // id = system * 16 + type
            for (size_t s = 0; s < G.systems.size(); ++s)
                for (int t : kShipIdx)
                    if (t != int(UnitType::Fighter) && G.systems[s].space[size_t(q)].n[t])
                        ships.push_back({int(s) * 16 + t, std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)), ""});
            if (ships.size() < 2) return false;
            spendAgent(p, "crimson");
            const int a = fpick(q, Ab::CrimsonAgent, "Ahk Ravin: first ship to swap?", ships);
            std::vector<Option> rest;
            for (const Option& o : ships)
                if (o.id / 16 != a / 16) rest.push_back(o);
            if (rest.empty()) return true;
            const int b = fpick(q, Ab::CrimsonAgent, "Ahk Ravin: swap it with which ship?", rest);
            Force& fa = G.systems[size_t(a / 16)].space[size_t(q)];
            Force& fb = G.systems[size_t(b / 16)].space[size_t(q)];
            moveShip(fa, fb, a % 16);
            moveShip(fb, fa, b % 16);
            say(q, pn(q) + "'s " + unitTypeName(UnitType(a % 16)) + " and " + unitTypeName(UnitType(b % 16)) + " trade places (Ahk Ravin).");
            removeExcessCapacity(q, a / 16);
            removeExcessCapacity(q, b / 16);
            return true;
        }
        case Ab::Sever: {
            const int c = playerOfFaction(G, "crimson");
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (hasUnits(G, p, int(s)) && wormholesOf(G, int(s))) opts.push_back({int(s), sysName(int(s)), G.fixes >= 24 ? "has a wormhole" : ""});
            // (fixes 24: the card - any system with your units; one may get a wormhole later this round)
            if (G.fixes >= 24)
                for (size_t s = 0; s < G.systems.size(); ++s)
                    if (hasUnits(G, p, int(s)) && !wormholesOf(G, int(s)) && !G.systems[s].isHyperlane())
                        opts.push_back({int(s), sysName(int(s)), "no wormhole now"});
            if (c < 0 || opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            const int s = fpick(p, Ab::Sever, "Sever: its wormholes do nothing during movement - which system?", opts, "", true);
            if (s < 0) return false;
            Player& pl = G.players[size_t(p)];
            const int note = makeNote(c, kNoteFaction);
            auto it = std::find(pl.notes.begin(), pl.notes.end(), int16_t(note));
            if (it == pl.notes.end()) return false;
            pl.notes.erase(it);
            pl.notesInPlay.push_back(int16_t(note));
            G.severSystem = int16_t(s);
            say(p, pn(p) + " severs the wormholes of " + sysName(s) + " (Sever).", s);
            return true;
        }
        case Ab::BtCrimson: {
            std::vector<Option> opts;  // breaches to flip (system), or 1000 + system for a new active one
            for (size_t s = 0; s < G.systems.size(); ++s) {
                if (G.systems[s].breach) opts.push_back({int(s), std::string(G.systems[s].breach == 2 ? "Close" : "Open") + " the breach in " + sysName(int(s)), ""});
                else if (hasUnits(G, p, int(s)) && !G.systems[s].isHyperlane() && (G.fixes < 8 || G.systems[s].home < 0) &&  // (0.44.4: non-home)
                         breachPlaceable(int(s)))
                    opts.push_back({1000 + int(s), "New active breach in " + sysName(int(s)), ""});
            }
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            const int c = fpick(p, Ab::BtCrimson, "Resonance Generator: flip or open a breach?", opts);
            if (c < 0) return false;
            G.players[size_t(p)].btExhausted = true;
            if (c >= 1000) placeBreach(p, c - 1000, true);
            else flipBreach(p, c);
            return true;
        }
        default: return factionAction9(p, ab);  // the Firmament / the Obsidian
    }
}

}  // namespace ti
