// Blue Reverie (0.83.0, TacticBlue's homebrew - module kModBlueReverie): its factions' rules. The cards' exact wording lives
// in faction_text.cpp (owner 2026-11-21: new content uses the cards' text). Read-only helpers in query_br.cpp; the AI in
// ai/ai_br.cpp.
//   Atokera Legacy - Synthesis (query: productionCapacity, groundForceDestinations, the Planetary Shield checks), Living Metals
//                    (livingMetals_: agenda outcomes and ACTION cards can't destroy / remove their units or put another
//                    player's units on their planets - owner 2026-11-23), Kohi (query kohiDef + brExtraDice), Obelisk
//                    (obeliskGuards at every "exhaust another player's planet"; brGroundRoundEnd: the defender's repair),
//                    Mirrorshard II (brOutcomeResolved: DEPLOY), Modular Designs (brAfterMovement: chosen after movement -
//                    owner OK 2026-11-23), Magruda (brMagruda: votes), Ordagka (brAgendaRevealed / brAfterVoting /
//                    brOutcomeResolved), Kapoko Vui (brInvasionStart, kapokoShips, brEndTactical), Tri-Weave Shielding
//                    (brInvasionStart), Panoply Animus (panoplyOn: the breakthrough)
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
void brEvent(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
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
const char* kModName[6] = {"", "SUSTAIN DAMAGE", "ANTI-FIGHTER BARRAGE 3", "BOMBARDMENT 3", "SPACE CANNON 3", "PRODUCTION 3"};
}  // namespace

// ================================================================== Living Metals
// "Agenda outcomes and action cards that have component actions cannot remove or destroy your units, or place other player's units
// on planets you control." (owner 2026-11-23: every agenda outcome, and every action card with "ACTION:")
bool Rules::livingMetalsBlocks(int p, const char* what) {
    if (!livingMetals_ || !brOn() || p < 0 || p >= G.numPlayers || !fac(p, "atokera")) return false;
    if (livingMetalsSaid_ != int(G.log.size())) {  // (one line per effect)
        brEvent(E, p, "Living Metals",
                std::string(livingMetals_ == 1 ? "The agenda outcome" : "The action card") + " can't " + (what && *what ? what : "touch") + " " +
                    pn(p) + "'s units (Living Metals).");
        livingMetalsSaid_ = int(G.log.size());
    }
    return true;
}

// ================================================================== Modular Designs
// "Once during each of your actions, choose 1 of your units and 1 unit ability. That unit gains that unit ability with a value of 3,
// if applicable, until the end of that action." Chosen after movement (owner OK), for a unit in the active system.
void Rules::brAfterMovement(int p, int sys) {
    G.fx.razalkaSys = -1;  // (0.84.0) Razalka Gris: "until the end of movement"
    if (!owns(p, "dsatoky") || sys < 0 || G.fx.modOwner >= 0) return;
    const SystemState& s = G.systems[size_t(sys)];
    std::vector<Option> opts;
    auto add = [&](const Force& f, int planet, const std::string& where) {
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!f.n[t]) continue;
            const UnitType ut = UnitType(t);
            const std::string name = std::string(unitLabel(G, p, ut)) + where;
            const UnitDef& d = unitDef(G, p, ut);
            const int code = t * 100 + (planet + 1);
            if (!unitSustainsAt(G, p, ut, sys, planet) && (isShip(ut) || isGroundForce(ut)))  // (planets: -2 = anywhere in this system)
                opts.push_back({1 * 10000 + t * 100 + 99, std::string(kModName[1]) + ": " + unitLabel(G, p, ut) + " (in this system)", ""});
            if (planet < 0 && isShip(ut)) {
                opts.push_back({2 * 10000 + code, std::string(kModName[2]) + ": " + name, d.afb > 0 ? "It also keeps its own." : ""});
                opts.push_back({3 * 10000 + code, std::string(kModName[3]) + ": " + name, d.bombard > 0 ? "It also keeps its own." : ""});
            }
            opts.push_back({4 * 10000 + code, std::string(kModName[4]) + ": " + name, d.cannon > 0 ? "It also keeps its own." : ""});
            opts.push_back({5 * 10000 + code, std::string(kModName[5]) + ": " + name, ""});
        }
    };
    add(s.space[size_t(p)], -1, " in space");
    for (size_t i = 0; i < s.planets.size(); ++i) add(G.planets[size_t(s.planets[i])].units[size_t(p)], int(s.planets[i]), " on " + planetName(s.planets[i]));
    // (one Sustain Damage row per type)
    std::sort(opts.begin(), opts.end(), [](const Option& a, const Option& b) { return a.id < b.id; });
    opts.erase(std::unique(opts.begin(), opts.end(), [](const Option& a, const Option& b) { return a.id == b.id; }), opts.end());
    if (opts.empty()) return;
    opts.push_back({-1, "Don't", ""});
    const int c = fpick(p, Ab::BrModularDesigns, "Modular Designs: give 1 of your units in " + sysName(sys) + " a unit ability?", opts,
                        "That unit gains the ability with a value of 3 until the end of this action. A unit that has the ability "
                        "already rolls 1 more die at 3; PRODUCTION 3 takes the place of a lower value.");
    if (c < 0) return;
    const int ab = c / 10000, t = (c / 100) % 100, loc = c % 100;
    TacticalEffects& fx = G.fx;
    fx.modOwner = int8_t(p);
    fx.modAbility = int8_t(ab);
    fx.modType = int8_t(t);
    fx.modSys = int16_t(sys);
    fx.modPlanet = int16_t(loc == 99 ? -2 : loc - 1);
    if (ab == 1) {  // (the type's damage now: Sustain Damage used = more of them damaged later)
        int dmg = s.space[size_t(p)].dmg[t];
        for (int16_t x : s.planets) dmg += G.planets[size_t(x)].units[size_t(p)].dmg[t];
        fx.modDmg0 = int8_t(dmg);
    }
    brEvent(E, p, "Modular Designs",
            pn(p) + "'s " + unitTypeName(UnitType(t)) + " gains " + kModName[ab] + " until the end of this action (Modular Designs).", sys);
}

// Kohi / Modular Designs: dice they add to a roll of p's units at (sys, planet; -1 = space) - (count, value) pairs
std::vector<std::pair<int, int>> Rules::brExtraDice(int p, int sys, int planet, RollKind kind) const {
    std::vector<std::pair<int, int>> out;
    if (!brOn() || p < 0 || p >= G.numPlayers || sys < 0) return out;
    const int ab = kind == RollKind::AFB ? 2 : kind == RollKind::Bombard ? 3 : kind == RollKind::Cannon ? 4 : 0;
    if (!ab) return out;
    const Force& f = planet < 0 ? G.systems[size_t(sys)].space[size_t(p)] : G.planets[size_t(planet)].units[size_t(p)];
    if (kohiIn(G, p, sys))
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!f.n[t] || isFlagship(UnitType(t))) continue;
            const UnitDef& o = unitDef(G, p, UnitType(t));
            const UnitDef k = kohiDef(G, p, UnitType(t), sys);
            const int ov = ab == 2 ? o.afb : ab == 3 ? o.bombard : o.cannon;
            const int kv = ab == 2 ? k.afb : ab == 3 ? k.bombard : k.cannon;
            const int kd = ab == 2 ? k.afbDice : ab == 3 ? k.bombardDice : k.cannonDice;
            if (ov <= 0 && kv > 0) out.push_back({f.n[t] * std::max(1, kd), kv});
        }
    const TacticalEffects& fx = G.fx;
    if (fx.modOwner == p && fx.modAbility == ab && fx.modSys == sys && fx.modPlanet == planet && fx.modType >= 0 && f.n[fx.modType])
        out.push_back({1, 3});
    // (0.84.0) Glatison (Belkosea superweapon): ANTI-FIGHTER BARRAGE 4 (x2) in its system's space combats - its controller's (or the
    // Armageddon Project holder's at their monument)
    if (ab == 2 && planet < 0) {
        const int x = superweaponPlanet(G, kSwGlatison);
        int h = -1;
        if ((x >= 0 && G.planets[size_t(x)].system == sys && G.planets[size_t(x)].owner == p) || (armageddonAt(G, kSwGlatison, sys, &h, nullptr) && h == p))
            out.push_back({2, 4});
    }
    return out;
}

// ================================================================== the agenda phase
// Ordagka (Atokera commander): "Once per agenda phase, after an agenda is revealed: You may predict aloud an outcome of that agenda.
// If you do not vote on that agenda, and your prediction is correct, gain 1 command token." (owner OK: it counts as "predicted")
void Rules::brAgendaRevealed(int a) {
    for (int q = 0; q < G.numPlayers; ++q) brGuess_[q] = -1;
    if (G.phase != Phase::Agenda) return;
    for (int q : clockwiseFrom(G, G.speaker)) {
        Player& pl = G.players[size_t(q)];
        if (pl.eliminated || !commanderUsable(G, q, "atokera") || pl.brOrdagkaRound == G.round) continue;
        std::vector<Option> opts = agendaOutcomes(a);
        if (opts.empty()) continue;
        opts.push_back({kNoVote, "Don't predict", ""});
        const int prevAgenda = curAgenda_;
        curAgenda_ = a;
        const int o = fpick(q, Ab::BrOrdagka, "Ordagka: predict an outcome of " + std::string(kAgendas[a].name) + "?", opts,
                            "Once per agenda phase. If you then don't vote on this agenda and you are right, you gain 1 command token.");
        curAgenda_ = prevAgenda;
        if (o == kNoVote) continue;
        pl.brOrdagkaRound = int16_t(G.round);
        brGuess_[q] = o;
        brEvent(E, q, "Ordagka", pn(q) + " predicts " + outcomeName(a, o) + " (Ordagka).");
    }
}

// Ordagka unlocks: "Cast more votes for an outcome than any other player."
void Rules::brAfterVoting(const std::vector<int>& votedFor, const std::vector<int>& votes) {
    (void)votedFor;
    for (int p = 0; p < G.numPlayers && p < int(votes.size()); ++p) {
        Player& pl = G.players[size_t(p)];
        if (!fac(p, "atokera") || pl.brMostVotes || votes[size_t(p)] <= 0) continue;
        bool most = true;
        for (int q = 0; q < G.numPlayers && q < int(votes.size()); ++q)
            if (q != p && votes[size_t(q)] >= votes[size_t(p)]) most = false;
        if (!most) continue;
        pl.brMostVotes = true;
        say(p, pn(p) + " cast more votes than any other player.");  // (Ordagka's unlock - checkUnlocks)
    }
}

// After an outcome is resolved (the agenda phase): Mirrorshard II's DEPLOY, Ordagka's command token
void Rules::brOutcomeResolved(int a, int outcome, const std::vector<int>& votedFor, const std::vector<int>* guessed) {
    if (G.phase != Phase::Agenda) return;
    for (int p = 0; p < G.numPlayers; ++p) {
        if (G.players[size_t(p)].eliminated) continue;
        const bool voted = p < int(votedFor.size()) && votedFor[size_t(p)] == outcome;
        bool predicted = false;
        if (guessed)
            for (int o : guessed[p]) predicted = predicted || o == outcome;
        // Ordagka: right, and no vote cast on this agenda
        if (brGuess_[p] == outcome && (p >= int(votedFor.size()) || votedFor[size_t(p)] == kNoVote)) {
            brEvent(E, p, "Ordagka", pn(p) + "'s prediction was right: 1 command token (Ordagka).");
            gainTokens(p, 1, false, "Ordagka: gain 1 command token.");
        }
        if (!voted && !predicted) continue;
        // Mirrorshard II: "DEPLOY: During the agenda phase, after an outcome you voted for or predicted is resolved, place up to 1
        // cruiser in a system that contains your ships."
        if (!std::strcmp(unitDef(G, p, UnitType::Cruiser).key, "atokera_cruiser2") && unitsForPlacement(G, p, UnitType::Cruiser) > 0) {
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (hasShips(G, p, int(s))) opts.push_back({int(s), sysName(int(s)), std::to_string(G.systems[s].space[size_t(p)].ships()) + " ships"});
            if (!opts.empty()) {
                opts.push_back({-1, "Don't", ""});
                const int s = fpick(p, Ab::BrMirrorshard, "Mirrorshard II: place 1 cruiser in a system with your ships?", opts,
                                    std::string("You voted for or predicted ") + outcomeName(a, outcome) + ".", true);
                if (s >= 0) {
                    addUnits(p, UnitType::Cruiser, 1, s, -1);
                    brEvent(E, p, "Mirrorshard II", pn(p) + " places 1 cruiser in " + sysName(s) + " (Mirrorshard II).", s);
                }
            }
        }
    }
    for (int q = 0; q < G.numPlayers; ++q) brGuess_[q] = -1;
}

// Magruda (Atokera agent): "When a player exhausts a planet during the agenda phase: Exhaust this card to allow that player to treat
// the combined value of that planet's resources and influence as if it were resources or influence. Then, ready that planet." - for
// a player's votes (the Atokera's own, or a player Magruda was promised to): the more votes it gives
int Rules::brMagruda(int p, const std::vector<int>& spent) {
    if (!brOn() || G.phase != Phase::Agenda || spent.empty()) return 0;
    const int user = agentUser(p, "atokera");
    if (user < 0) return 0;
    std::vector<Option> opts;
    for (int x : spent)
        if (planetResources(G, x) > 0) opts.push_back({x, planetName(x), "+" + std::to_string(planetResources(G, x)) + " votes, and it is readied"});
    if (opts.empty()) return 0;
    std::sort(opts.begin(), opts.end(), [&](const Option& a, const Option& b) { return planetResources(G, a.id) > planetResources(G, b.id); });
    opts.push_back({-1, "Don't", ""});
    curFor_ = p;
    const int x = fpick(user, Ab::BrMagruda,
                        user == p ? std::string("Magruda: one of the planets you exhausted votes with its resources and influence?")
                                  : "Magruda: let " + pn(p) + "'s planet vote with its resources and influence?",
                        opts, "Exhaust Magruda: the planet counts its resources and its influence, then it is readied.", false, true);
    curFor_ = -1;
    if (x < 0) return 0;
    spendAgent(user, "atokera");
    agentDone(user, p);
    G.planets[size_t(x)].exhausted = false;
    brEvent(E, user, "Magruda",
            pn(p) + "'s " + planetName(x) + " counts its resources and influence and is readied (Magruda).", G.planets[size_t(x)].system);
    return planetResources(G, x);
}

// ================================================================== invasion
// Kapoko Vui (Atokera hero): "At the start of invasion: You may purge this card to commit any of your ships in the active system to
// planets as if they were ground forces; those units participate in combat. At the end of this tactical action, return those ships
// to the space area of the active system." Tri-Weave Shielding (Atokera note): "At the start of invasion: Choose 1 unit you control
// in the active system; that unit gains PLANETARY SHIELD until the end of this action. Then, return this card to the Atokera player."
void Rules::brInvasionStart(int p, int sys) {
    const SystemState& s = G.systems[size_t(sys)];
    if (fac(p, "atokera") && heroUnlocked(G, p) && s.space[size_t(p)].ships() > 0 && G.fx.kapokoOwner < 0) {
        bool target = false;  // a planet worth landing on: not ours, or another player's ground forces there
        for (int16_t x : s.planets) {
            if (G.planets[size_t(x)].destroyed || isDMZ(G, x)) continue;
            if (G.planets[size_t(x)].owner != p) target = true;
            for (int q = 0; q < kSides; ++q)
                if (q != p && (q < G.numPlayers || q == kNeutral) && groundForcesOn(G, q, x) > 0) target = true;
        }
        if (target && fyes(p, Ab::BrKapoko, "Purge Kapoko Vui: commit your ships here to planets as if they were ground forces?",
                           "Those ships fight in ground combat; at the end of this tactical action they return to space.")) {
            purgeHero(p);
            G.fx.kapokoOwner = int8_t(p);
            brEvent(E, p, "Kapoko Vui", pn(p) + "'s ships may commit to planets as ground forces (Kapoko Vui).", sys);
        }
    }
    // Tri-Weave Shielding: another player's ground forces on a planet here, under p's bombardment
    const int atok = playerOfFaction(G, "atokera");
    if (atok < 0) return;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == atok || q == p || G.players[size_t(q)].eliminated || !holdsNote(G, q, atok, kNoteFaction)) continue;
        std::vector<Option> opts;
        for (int16_t x : s.planets)
            if (groundForcesOn(G, q, x) > 0 && !brPlanetShield(G, q, x)) {
                bool shield = false;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (G.planets[size_t(x)].units[size_t(q)].n[t] && unitDef(G, q, UnitType(t)).shield) shield = true;
                if (!shield) opts.push_back({x, planetName(x), std::to_string(groundForcesOn(G, q, x)) + " ground forces"});
            }
        if (opts.empty()) continue;
        bool bombards = false;
        for (int t = 0; t < kUnitTypes; ++t)
            if (s.space[size_t(p)].n[t] && (unitDef(G, p, UnitType(t)).bombard > 0 || dsBombardDiceOf(G, p, sys, UnitType(t)) > 0)) bombards = true;
        if (!bombards) continue;
        opts.push_back({-1, "Keep the note", ""});
        curFor_ = p;
        const int x = fpick(q, Ab::BrTriWeave, "Play " + pn(atok) + "'s Tri-Weave Shielding: a unit of yours gains Planetary Shield?", opts,
                            "One of your units on that planet gains PLANETARY SHIELD until the end of this action (no bombardment "
                            "against it). The note goes back to " + pn(atok) + ".",
                            false, true);
        curFor_ = -1;
        if (x < 0) continue;
        G.fx.triOwner = int8_t(q);
        G.fx.triPlanet = int16_t(x);
        returnNote(q, makeNote(atok, kNoteFaction));
        brEvent(E, q, "Tri-Weave Shielding", pn(q) + "'s units on " + planetName(x) + " gain Planetary Shield (Tri-Weave Shielding).", sys);
    }
}

// Kapoko Vui: p's ships may be committed / fight on planets in this action
bool Rules::kapokoShips(int p, int sys) const {
    return brOn() && G.fx.kapokoOwner == p && sys == G.activeSystem && p == G.activePlayer;
}

// The end of the tactical action: Kapoko Vui's ships go back to the active system's space area
void Rules::brEndTactical(int p, int sys) {
    if (G.fx.kapokoOwner != p || p < 0 || sys < 0) return;
    int n = 0;
    Force& sp = G.systems[size_t(sys)].space[size_t(p)];
    for (int16_t x : G.systems[size_t(sys)].planets) {
        Force& f = G.planets[size_t(x)].units[size_t(p)];
        for (int t : kShipIdx) {
            const int k = f.n[t];
            if (!k) continue;
            const int dk = f.dmg[t];
            f.n[t] = f.dmg[t] = 0;
            sp.add(UnitType(t), k);
            sp.dmg[t] = uint8_t(sp.dmg[t] + dk);
            n += k;
        }
    }
    if (n) say(p, pn(p) + "'s " + std::to_string(n) + (n == 1 ? " ship returns" : " ships return") + " to space (Kapoko Vui).", sys);
}

// Panoply Animus (Atokera breakthrough): "Your structures gain SUSTAIN DAMAGE and participate in ground combat as if they were ground
// forces with a combat value of 7"
bool Rules::panoplyOn(int p) const { return brOn() && breakthroughsOn(G) && p >= 0 && p < G.numPlayers && breakthroughIs(G, p, "atokera"); }

// ================================================================== Obelisk
// "During combat as the defender, you may exhaust this planet to repair each of your units on this planet." - after the hits of a
// round of ground combat, when something of theirs there is damaged and the fight goes on
void Rules::brGroundRoundEnd(int att, int def, int planet) {
    if (!brOn() || def < 0 || def >= G.numPlayers || planet < 0) return;
    PlanetState& ps = G.planets[size_t(planet)];
    if (ps.owner != def || ps.exhausted || !obeliskGuards(G, planet, att)) return;
    Force& f = ps.units[size_t(def)];
    int dmg = 0;
    for (int t = 0; t < kUnitTypes; ++t) dmg += f.dmg[t];
    if (!dmg || f.groundForces() == 0 || ps.units[size_t(att)].empty()) return;
    curFor_ = att;
    const bool use = fyes(def, Ab::BrObelisk, "Obelisk: exhaust " + planetName(planet) + " to repair your units there?",
                          std::to_string(dmg) + (dmg == 1 ? " unit is" : " units are") + " damaged.");
    curFor_ = -1;
    if (!use) return;
    ps.exhausted = true;
    for (int t = 0; t < kUnitTypes; ++t) f.dmg[t] = 0;
    brEvent(E, def, "Obelisk", pn(def) + " exhausts " + planetName(planet) + " and repairs their units there (Obelisk).", ps.system);
}

}  // namespace ti
