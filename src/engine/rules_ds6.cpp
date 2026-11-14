// Discordant Stars batch 6 (0.42.0): the last four factions, called from the ds / ds3 / ds5 hooks and a few new ones
// (notes received, research payments). Texts in faction_text.cpp, the AI in ai/ai_ds.cpp.
//   Veldyr Sovereignty  - Corporate Entity (4 faction notes: kNoteFaction..kNoteFaction4 - the Branch Offices, attached
//                         when received: Game::dsBranch, query attachBonus), Holding Company, Targeted Acquisition,
//                         Richtyrian, Lancer Dreadnought II (query immuneToDirectHit), SEIDR Project, Solis Morden (pay),
//                         Vera Khage (query missingPrereqs), Auberon Elyrin, Aurora Stormcaller
//   Li-Zho Dynasty      - Cunning / Subterfuge (5 Traps: Game::dsTrap, face down), Interference Grid, Minefields,
//                         Account Siphon, Gravitic Inhibitor (fx.dsInhibit -> checkMovePlan), Feint, Wraith Engine
//                         (fx.dsWraith* -> shipReach), Trusted Counselor, Vasra Ivo, Dume Tathu, Khaz-Rin Li-Zho, Oro-Zhin
//                         Elite; Heavy Bombers (data)
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
constexpr int kShipMask6 = kShipMaskAll;  // Carrier .. WarSun, the second flagship

void ds6Event(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
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
bool isHome6(const Game& G, int x) { return G.systems[size_t(G.planets[size_t(x)].system)].home >= 0; }
bool isMecatol6(const Game& G, int x) { return !std::strcmp(kPlanets[G.planets[size_t(x)].def].key, "mr"); }
bool hasBranch(const Game& G, int x) {
    for (int16_t b : G.dsBranch)
        if (b == x) return true;
    return false;
}
// the Traps (our own short names)
const char* kTrapName[5] = {"Interference Grid", "Minefields", "Account Siphon", "Gravitic Inhibitor", "Feint"};
}  // namespace

const char* trapName(int k) { return k >= 0 && k < 5 ? kTrapName[k] : ""; }
const char* trapText(int k) {
    static const char* t[5] = {
        "Revealed at the start of a space combat in its system: one player rolls 1 lower this combat.",
        "Revealed after a player lands infantry on its planet: destroy up to 2 of them.",
        "Revealed after a player takes its planet: take up to 2 of their trade goods or 3 of their commodities.",
        "Revealed after a player activates its system: they move in at most 1 ship fewer than their fleet pool.",
        "A bluff - it does nothing."};
    return k >= 0 && k < 5 ? t[k] : "";
}

bool branchNoSpot(const Game& G, int note, int receiver) {
    if (note < 0 || receiver < 0 || noteType(note) < kNoteFaction || noteType(note) > kNoteFaction4) return false;
    if (std::strcmp(kFactions[G.players[size_t(noteOwner(note))].faction].key, "veldyr")) return false;
    for (int x : controlledPlanets(G, receiver))
        if (!isHome6(G, x) && !isMecatol6(G, x) && !hasBranch(G, x) && !G.planets[size_t(x)].destroyed) return false;
    return true;
}

// ================================================================== Veldyr: Branch Offices
// A Branch Office note received: attached at once to one of the receiver's planets (or back to the Veldyr).
void Rules::dsBranchReceived(int p, int note) {
    const int owner = noteOwner(note), k = noteType(note) - kNoteFaction;
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, p))
        if (!isHome6(G, x) && !isMecatol6(G, x) && !hasBranch(G, x) && !G.planets[size_t(x)].destroyed)
            opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + "/" + std::to_string(planetInfluence(G, x))});
    if (opts.empty()) {
        G.players[size_t(owner)].notes.push_back(int16_t(note));
        say(p, pn(p) + " has no planet for the " + noteNameOf(G, note) + " - it goes back to " + pn(owner) + ".");
        return;
    }
    const int x = opts.size() == 1 ? opts[0].id : fpick(p, Ab::DsBranchPlanet, noteNameOf(G, note) + ": attach it to which planet?", opts, noteTextOf(G, note), false, true);
    G.dsBranch[k] = int16_t(x);
    ds6Event(E, owner, "Corporate Entity", pn(owner) + " opens a " + noteNameOf(G, note) + " on " + planetName(x) + ".", G.planets[size_t(x)].system);
}

// Solis Morden (Veldyr agent): research paid partly with a planet's influence. Returns the value used.
int Rules::dsSolis(int p, int amount, int& planet) {
    planet = -1;
    const int user = agentUser(p, "veldyr");
    if (user < 0 || amount <= 0) return 0;
    std::vector<Option> opts;
    for (int x : ti::readyPlanets(G, p))
        if (planetInfluence(G, x) > 0) opts.push_back({x, planetName(x), std::to_string(planetInfluence(G, x)) + " influence as resources"});
    if (opts.empty()) return 0;
    opts.push_back({-1, "Don't", ""});
    agentFor(user, p);
    curValue_ = amount;
    const int x = fpick(user, Ab::DsSolis, user == p ? "Exhaust Solis Morden: one planet's influence pays for this research?" : "Exhaust Solis Morden so " + pn(p) + " pays with influence?",
                        opts, leaderText(findLeader("veldyragent")), false, true);
    curValue_ = 0;
    curFor_ = -1;
    if (x < 0) return 0;
    spendAgent(user, "veldyr");
    agentDone(user, p);
    planet = x;
    G.planets[size_t(x)].exhausted = true;
    if (G.fixes >= 11) planetsExhausted(p, {x});  // (0.44.7)
    ds6Event(E, p, "Solis Morden", pn(p) + " pays for research with " + planetName(x) + "'s influence.");
    return planetInfluence(G, x);
}

// ================================================================== combat
void Rules::ds6CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m) {
    (void)opp;
    if (p < 0 || p >= G.numPlayers || sys < 0) return;
    // Richtyrian (Veldyr flagship): 1 more die for each round already fought
    if (space && flagshipAbility(G, p, "veldyr") && flagshipIn(G, p, sys) && combatRound_ > 1) m.extraDice[int(UnitType::Flagship)] += combatRound_ - 1;
    // Interference Grid (Li-Zho trap): -1 to that player's rolls this combat
    if (space && dsGrid_ == p)
        for (int t = 0; t < kUnitTypes; ++t) m.perType[t] -= 1;
    // Dume Tathu (Li-Zho commander): with only 1 non-fighter ship (or 1 infantry) in the fight, 1 unit rolls 1 more die
    if (commanderUsable(G, p, "lizho")) {
        const Force& f = space ? G.systems[size_t(sys)].space[size_t(p)] : G.planets[size_t(planet)].units[size_t(p)];
        if (space && f.nonFighterShips() == 1) {
            for (int t = int(UnitType::WarSun); t >= 0; --t)
                if (t != int(UnitType::Fighter) && f.n[t]) {
                    m.extraDice[t] += 1;
                    break;
                }
        } else if (!space && f.count(UnitType::Infantry) <= 1 && f.groundForces() > 0) {
            m.extraDice[f.count(UnitType::Mech) ? int(UnitType::Mech) : int(UnitType::Infantry)] += 1;
        }
    }
}

void Rules::ds6SpaceCombatStart(int att, int def, int sys) {
    dsGrid_ = -1;
    // Interference Grid (Li-Zho trap): revealed - one player rolls 1 lower this combat
    const int lz = playerOfFaction(G, "lizho");
    if (lz >= 0 && G.dsTrap[0] >= 0 && G.planets[size_t(G.dsTrap[0])].system == sys && !((G.dsTrapShown >> 0) & 1)) {
        std::vector<Option> opts;
        for (int side : {att, def})
            if (side != lz && side < G.numPlayers) opts.push_back({side, pn(side), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Keep it hidden", ""});
            const int q = fpick(lz, Ab::DsGridTarget, "Reveal Interference Grid: which player rolls 1 lower in this combat?", opts, trapText(0));
            if (q >= 0) {
                dsGrid_ = q;
                G.dsTrapShown = uint8_t(G.dsTrapShown | 1);
                ds6Event(E, lz, "Interference Grid", pn(lz) + " springs a trap: " + pn(q) + "'s targeting goes haywire.", sys);
            }
        }
    }
    // Trusted Counselor (the Li-Zho's note): 2 fighters for the holder
    if (lz < 0) return;
    for (int side : {att, def}) {
        if (side >= G.numPlayers || side == lz || !holdsNote(G, side, lz, kNoteFaction) || unitsAvailable(G, side, UnitType::Fighter) <= 0) continue;
        if (!fyes(side, Ab::DsCounselor, "Play " + pn(lz) + "'s Trusted Counselor: 2 fighters here?", "The note goes back.")) continue;
        returnNote(side, makeNote(lz, kNoteFaction));
        addUnits(side, UnitType::Fighter, 2, sys, -1);
        ds6Event(E, side, "Trusted Counselor", pn(side) + " follows trusted counsel: 2 fighters join the battle.", sys);
    }
}

// SEIDR Project (Veldyr): after each round of space combat, one of their units here fires its space cannon at the enemy.
void Rules::ds6SpaceRoundEnd(int att, int def, int sys) {
    for (int side : {att, def}) {
        if (side >= G.numPlayers || !owns(side, "dsveldr")) continue;
        // (0.44.8, fixes 12: a unit ability - not inside an entropic scar or under the Quietus)
        if (G.fixes >= 12 && ((scarAt(G, sys) && !ignoresScars(G, side)) || quietusAt(G, side, sys))) continue;
        const int other = side == att ? def : att;
        if (other < 0 || !G.systems[size_t(sys)].space[size_t(other)].ships()) continue;
        if (G.fixes >= 25 && G.fx.myruThrough == other) continue;  // Myru Vos (Thunder's Edge): no space cannon against them
        int best = -1, bestV = 99, dice = 0;
        auto consider = [&](const Force& f) {
            for (int t = 0; t < kUnitTypes; ++t) {
                if (!f.n[t]) continue;
                const UnitDef& u = unitDef(G, side, UnitType(t));
                if (u.cannon > 0 && u.cannon < bestV) {
                    best = t;
                    bestV = u.cannon;
                    dice = std::max<int>(1, u.cannonDice);
                }
            }
        };
        consider(G.systems[size_t(sys)].space[size_t(side)]);
        for (int16_t x : G.systems[size_t(sys)].planets) consider(G.planets[size_t(x)].units[size_t(side)]);
        if (best < 0) continue;
        // 0.44.5 (fixes 9): Graviton Laser System and Antimass Deflectors apply to this space cannon shot
        const bool gls = G.fixes >= 9 && askGraviton(side, other, sys);
        RollResult r;
        addDice(r, dice + (G.fixes >= 12 ? abilityDice(side, sys) : 0), bestV + (G.fixes >= 9 ? antimass(side, other) : 0), best);
        reportRoll(side, sys, "SEIDR Project", r);
        if (r.hits > 0) cannonHits(other, sys, r.hits, kShipMask6, gls, "SEIDR Project", side);
    }
}

// ================================================================== tactical actions, turns, phases
void Rules::ds6Activated(int p, int sys) {
    if (p < 0 || p >= G.numPlayers) return;
    // Gravitic Inhibitor (Li-Zho trap): the active player moves in at most fleet pool - 1 ships
    const int lz = playerOfFaction(G, "lizho");
    if (lz >= 0 && lz != p && G.dsTrap[3] >= 0 && G.planets[size_t(G.dsTrap[3])].system == sys) {
        curValue_ = G.systems[size_t(sys)].space[size_t(p)].ships() > 0 ? 0 : 1;
        if (fyes(lz, Ab::DsTrapReveal, "Reveal Gravitic Inhibitor: " + pn(p) + " moves in at most " + std::to_string(std::max(0, fleetLimit(G, p) - 1)) + " ships?", trapText(3))) {
            G.fx.dsInhibit = int8_t(p);
            G.dsTrapShown = uint8_t(G.dsTrapShown | 8);
            ds6Event(E, lz, "Gravitic Inhibitor", pn(lz) + " springs a trap: " + pn(p) + "'s fleet is held back.", sys);
        }
        curValue_ = 0;
    }
    // Wraith Engine (Li-Zho): one ship counts as next to the active system
    if (techReady(p, "dslizhb") && G.systems[size_t(sys)].home < 0) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            for (int t : kShipIdx)
                if (int(s) != sys && G.systems[s].space[size_t(p)].n[t] && !G.systems[s].hasToken(p))
                    opts.push_back({int(s) * 16 + t, std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Don't", ""});
            const int c = fpick(p, Ab::DsWraith, "Wraith Engine: which ship slips through to " + sysName(sys) + "?", opts, "It counts as next to the active system this action.", true);
            if (c >= 0) {
                exhaustTech(p, "dslizhb");
                G.fx.dsWraithSys = int16_t(c / 16);
                G.fx.dsWraithType = int8_t(c % 16);
                G.fx.dsWraithOwner = int8_t(p);
                ds6Event(E, p, "Wraith Engine", pn(p) + "'s wraith engine bends space toward " + sysName(sys) + ".", sys);
            }
        }
    }
    // Aurora Stormcaller (Veldyr mech): after its system is activated, it may become a PDS
    const int vd = playerOfFaction(G, "veldyr");
    if (vd >= 0 && mechAbilities(G))
        for (int16_t x : G.systems[size_t(sys)].planets) {
            Force& f = G.planets[size_t(x)].units[size_t(vd)];
            if (!f.count(UnitType::Mech) || unitsAvailable(G, vd, UnitType::PDS) <= 0 || f.count(UnitType::PDS) >= 2) continue;
            curValue_ = p == vd ? 0 : 1;
            const bool use = fyes(vd, Ab::DsAurora, "Aurora Stormcaller: your mech on " + planetName(x) + " becomes a PDS?", "");
            curValue_ = 0;
            if (!use) continue;
            f.remove(UnitType::Mech, 1, false);
            addUnits(vd, UnitType::PDS, 1, sys, x);
            ds6Event(E, vd, "Aurora Stormcaller", pn(vd) + "'s stormcaller raises a PDS on " + planetName(x) + ".", sys);
        }
}

// End of a tactical action: Cunning (Li-Zho) - a Trap on one of their planets here; revealed Traps go back.
void Rules::ds6EndTactical(int p, int sys) {
    dsGrid_ = -1;
    for (int k = 0; k < 5; ++k)
        if ((G.dsTrapShown >> k) & 1) G.dsTrap[k] = -1;
    G.dsTrapShown = 0;
    if (p < 0 || p >= G.numPlayers || !fac(p, "lizho")) return;
    std::vector<Option> planets;
    for (int16_t x : G.systems[size_t(sys)].planets)
        if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry)) planets.push_back({x, planetName(x), ""});
    std::vector<Option> traps;
    for (int k = 0; k < 5; ++k)
        if (G.dsTrap[k] < 0) traps.push_back({k, kTrapName[k], trapText(k)});
    if (planets.empty() || traps.empty()) return;
    traps.push_back({-1, "Don't", ""});
    const int k = fpick(p, Ab::DsTrapPlace, "Cunning: attach a Trap (face down) to a planet here?", traps);
    if (k < 0) return;
    const int x = planets.size() == 1 ? planets[0].id : fpick(p, Ab::DsTrapPlanet, std::string("Cunning: ") + kTrapName[k] + " on which planet?", planets, "", false, true);
    G.dsTrap[k] = int16_t(x);
    feedNote(p, std::string("Your ") + kTrapName[k] + " lies hidden on " + planetName(x) + ".");
    ds6Event(E, p, "Cunning", pn(p) + " hides a trap on " + planetName(x) + ".", sys);
}

// Minefields (Li-Zho trap): infantry land on its planet - up to 2 of them destroyed.
void Rules::ds6Landed(int p, int sys, const std::vector<int>& landed) {
    const int lz = playerOfFaction(G, "lizho");
    const int x = G.dsTrap[1];
    if (lz < 0 || lz == p || x < 0 || ((G.dsTrapShown >> 1) & 1) || std::find(landed.begin(), landed.end(), x) == landed.end()) return;
    const int inf = G.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry);
    if (inf <= 0) return;
    if (!fyes(lz, Ab::DsTrapReveal, "Reveal Minefields on " + planetName(x) + ": destroy up to 2 of " + pn(p) + "'s infantry?", trapText(1))) return;
    G.dsTrapShown = uint8_t(G.dsTrapShown | 2);
    ds6Event(E, lz, "Minefields", pn(lz) + " springs a trap: mines on " + planetName(x) + ".", sys);
    destroyUnits(p, UnitType::Infantry, std::min(2, inf), sys, x, lz, "Minefields");
}

// Account Siphon (Li-Zho trap): a player takes its planet - up to 2 of their trade goods or 3 commodities.
void Rules::ds6Control(int planet, int p, int old) {
    (void)old;
    const int lz = playerOfFaction(G, "lizho");
    if (lz < 0 || p < 0 || p >= G.numPlayers || p == lz || G.dsTrap[2] != planet || ((G.dsTrapShown >> 2) & 1)) return;
    Player& pl = G.players[size_t(p)];
    if (pl.tradeGoods <= 0 && pl.commodities <= 0) return;
    std::vector<Option> opts;
    if (pl.tradeGoods > 0) opts.push_back({0, "Up to 2 trade goods", std::to_string(std::min<int>(2, pl.tradeGoods)) + " trade goods"});
    if (pl.commodities > 0) opts.push_back({1, "Up to 3 commodities", std::to_string(std::min<int>(3, pl.commodities)) + " commodities (as trade goods)"});
    opts.push_back({-1, "Keep it hidden", ""});
    const int c = fpick(lz, Ab::DsSiphonTake, "Reveal Account Siphon on " + planetName(planet) + ": what do you take from " + pn(p) + "?", opts, trapText(2));
    if (c < 0) return;
    G.dsTrapShown = uint8_t(G.dsTrapShown | 4);
    if (G.activeSystem < 0) G.dsTrap[2] = -1;  // (outside a tactical action: back at once)
    if (vaultShut(p, lz, c == 0 ? "trade goods" : "commodities")) return;  // (High Security Vault)
    if (c == 0) {
        const int n = std::min<int>(2, pl.tradeGoods);
        pl.tradeGoods = int8_t(pl.tradeGoods - n);
        gainTG(lz, n);
    } else {
        const int n = std::min<int>(3, pl.commodities);
        pl.commodities = int8_t(pl.commodities - n);
        gainTG(lz, n);
    }
    ds6Event(E, lz, "Account Siphon", pn(lz) + " springs a trap: " + pn(p) + "'s accounts are drained.", G.planets[size_t(planet)].system);
}

// Oro-Zhin Elite (Li-Zho mech): at the start of their turn a Trap moves onto its planet.
void Rules::ds6TurnStart(int p) {
    if (!fac(p, "lizho") || !mechAbilities(G)) return;
    std::vector<int> mechPlanets;
    for (int x : controlledPlanets(G, p))
        if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech)) mechPlanets.push_back(x);
    if (mechPlanets.empty()) return;
    std::vector<Option> opts;
    for (int k = 0; k < 5; ++k)
        if (G.dsTrap[k] >= 0)
            for (int x : mechPlanets)
                if (G.dsTrap[k] != x) opts.push_back({k * 1000 + x, std::string(kTrapName[k]) + " to " + planetName(x), "From " + planetName(G.dsTrap[k]) + "."});
    if (opts.empty()) return;
    opts.push_back({-1, "Leave the Traps", ""});
    const int c = fpick(p, Ab::DsOroZhin, "Oro-Zhin Elite: move a Trap onto your mech's planet?", opts);
    if (c < 0) return;
    G.dsTrap[c / 1000] = int16_t(c % 1000);
    ds6Event(E, p, "Oro-Zhin Elite", pn(p) + " shifts a trap.");
}

// Vasra Ivo (Li-Zho agent): at the end of a player's turn, 2 fighters with their ships.
void Rules::ds6TurnEnd(int p) {
    const int user = agentUser(p, "lizho");
    if (user < 0 || unitsAvailable(G, p, UnitType::Fighter) <= 0) return;
    std::vector<Option> opts;
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (hasShips(G, p, int(s))) opts.push_back({int(s), sysName(int(s)), ""});
    if (opts.empty()) return;
    opts.push_back({-1, "Don't", ""});
    agentFor(user, p);
    const int s = fpick(user, Ab::DsVasra, user == p ? "Exhaust Vasra Ivo: 2 fighters with your ships - where?" : "Exhaust Vasra Ivo: 2 fighters for " + pn(p) + " - where?",
                        opts, leaderText(findLeader("lizhoagent")), true);
    curFor_ = -1;
    if (s < 0) return;
    spendAgent(user, "lizho");
    agentDone(user, p);
    addUnits(p, UnitType::Fighter, 2, s, -1);
    removeExcessCapacity(p, s);
    ds6Event(E, p, "Vasra Ivo", pn(p) + " receives 2 fighters from Vasra Ivo.", s);
}

// Status phase: Holding Company and Targeted Acquisition (Veldyr).
void Rules::ds6StatusStart() {
    const int vd = playerOfFaction(G, "veldyr");
    if (vd < 0) return;
    Player& pl = G.players[size_t(vd)];
    // Holding Company: for each Branch Office, 1 commodity or 1 commodity into a trade good
    for (int16_t b : G.dsBranch) {
        if (b < 0) continue;
        std::vector<Option> opts;
        if (pl.commodities < commodityCap(G, pl)) opts.push_back({0, "Gain 1 commodity", ""});
        if (pl.commodities > 0) opts.push_back({1, "Turn 1 commodity into a trade good", ""});
        if (opts.empty()) break;
        const int c = opts.size() == 1 ? opts[0].id : fpick(vd, Ab::DsHolding, "Holding Company (" + planetName(b) + "): what do you take?", opts);
        if (c == 0) { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + 1); commodityGained(vd, cb_); }
        else {
            pl.commodities = int8_t(pl.commodities - 1);
            gainTG(vd, 1);
        }
    }
    int n = 0;
    for (int16_t b : G.dsBranch) n += b >= 0 ? 1 : 0;
    if (n) ds6Event(E, vd, "Holding Company", pn(vd) + "'s " + std::to_string(n) + " branch office" + (n > 1 ? "s pay" : " pays") + " dividends.");
    // Targeted Acquisition: one of their notes to a neighbour
    std::vector<Option> notes;
    for (int16_t note : pl.notes)
        if (noteOwner(note) == vd && noteType(note) >= kNoteFaction && noteType(note) <= kNoteFaction4) notes.push_back({note, noteNameOf(G, note), noteTextOf(G, note)});
    std::vector<Option> to;
    bool bare = false;  // 0.43.0 (D119): a neighbour with no planet for it is marked (it would come straight back)
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != vd && !G.players[size_t(q)].eliminated && areNeighbours(G, vd, q)) {
            const bool none = !notes.empty() && branchNoSpot(G, notes[0].id, q);
            bare = bare || none;
            to.push_back({q, pn(q), none ? "No planet for it - it would come back to you" : ""});
        }
    if (notes.empty() || to.empty()) return;
    notes.push_back({-1, "Keep them", ""});
    const int note = fpick(vd, Ab::DsTargeted, "Targeted Acquisition: give one of your Branch Offices to a neighbour?", notes);
    if (note < 0) return;
    const int q = to.size() == 1 && !bare ? to[0].id
                  : fpick(vd, Ab::DsTargeted, "Targeted Acquisition: to which neighbour?", to,
                          bare ? std::string(1, kRedNote) + "A Branch Office needs a planet that isn't a home planet or Mecatol Rex and has none yet." : std::string());
    ds6Event(E, vd, "Targeted Acquisition", pn(vd) + " opens a branch office in " + pn(q) + "'s territory.");
    giveNote(vd, q, note);
}

// ================================================================== component actions
void Rules::ds6Options(int p, std::vector<Option>& o) {
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    if (fac(p, "veldyr") && heroUnlocked(G, p)) {
        bool any = false;
        for (int16_t b : G.dsBranch) any = any || (b >= 0 && G.planets[size_t(b)].owner >= 0);
        if (any) add(Ab::DsAuberon, "Hero: Auberon Elyrin", "For each Branch Office: a unit upgrade of a type its planet's owner has upgraded. Purged afterwards.");
    }
    if (fac(p, "lizho") && heroUnlocked(G, p)) add(Ab::DsKhazRin, "Hero: Khaz-Rin Li-Zho", "Attach or move each Trap to any planet; up to 12 fighters where your Traps are. Purged afterwards.");
}

bool Rules::ds6Action(int p, Ab ab) {
    Player& pl = G.players[size_t(p)];
    switch (ab) {
        case Ab::DsAuberon: {
            if (!heroUnlocked(G, p)) return false;
            ds5Hero(p, leaderIndex(G, p, kHero), pn(p) + " calls the board: A New Dawn - every branch office reports its secrets.");
            for (int16_t b : G.dsBranch) {
                if (b < 0) continue;
                const int q = G.planets[size_t(b)].owner;
                if (q < 0 || q == p) continue;
                std::vector<Option> opts;
                for (int16_t t : G.players[size_t(q)].techs) {
                    if (!kTechs[t].unitUpgrade) continue;
                    const int type = upgradeUnitType(t);
                    if (type < 0) continue;
                    // our own version of that unit's upgrade (a faction one first)
                    int mine = -1;
                    for (int k = 0; k < kTechCount; ++k) {
                        if (!kTechs[k].unitUpgrade || upgradeUnitType(k) != type || !techImplemented(k)) continue;
                        if (kTechs[k].faction == pl.faction) {
                            mine = k;
                            break;
                        }
                        if (kTechs[k].faction < 0 && mine < 0 && (kTechs[k].source == Source::Base || kTechs[k].source == Source::PoK)) mine = k;
                    }
                    if (mine < 0 || pl.hasTech(mine)) continue;
                    bool dup = false;
                    for (const Option& x : opts) dup = dup || x.id == mine;
                    if (!dup) opts.push_back({mine, kTechs[mine].name, techSummary(mine)});
                }
                if (opts.empty()) continue;
                opts.push_back({-1, "None", ""});
                const int t = fpick(p, Ab::DsAuberon, "Auberon Elyrin (" + planetName(b) + ", " + pn(q) + "): which unit upgrade?", opts);
                if (t >= 0) researchTech(p, t, false);
            }
            return true;
        }
        case Ab::DsKhazRin: {
            if (!heroUnlocked(G, p)) return false;
            ds5Hero(p, leaderIndex(G, p, kHero), pn(p) + " closes the net: Star's Razor - every trap shifts at once.");
            for (int k = 0; k < 5; ++k) {
                std::vector<Option> opts{{-1, G.dsTrap[k] >= 0 ? "Leave it on " + planetName(G.dsTrap[k]) : std::string("Leave it in reserve"), ""}};
                for (size_t x = 0; x < G.planets.size(); ++x)
                    if (!G.planets[x].destroyed && int(x) != G.dsTrap[k] && G.systems[size_t(G.planets[x].system)].home < 0)
                        opts.push_back({int(x), planetName(int(x)), G.planets[x].owner >= 0 ? pn(G.planets[x].owner) : std::string("nobody's")});
                const int x = fpick(p, Ab::DsKhazRin, std::string("Khaz-Rin Li-Zho: where does ") + kTrapName[k] + " go?", opts, trapText(k), false, true);
                if (x >= 0) G.dsTrap[k] = int16_t(x);
            }
            int left = std::min(12, unitsAvailable(G, p, UnitType::Fighter));
            for (int k = 0; k < 5 && left > 0; ++k) {
                if (G.dsTrap[k] < 0) continue;
                const int s = G.planets[size_t(G.dsTrap[k])].system;
                if (otherShipsIn(G, p, s)) continue;
                std::vector<Option> n;
                for (int i = 0; i <= std::min(left, 6); ++i) n.push_back({i, std::to_string(i), ""});
                // (0.46.1, fixes 15: how many fit - capacity and the fleet pool - told, and checked afterwards)
                int room = 0;
                if (G.fixes >= 15) {
                    const Force& f = G.systems[size_t(s)].space[size_t(p)];
                    while (room < 6) {
                        Force more = f;
                        more.add(UnitType::Fighter, room + 1);
                        if (overflowOf(G, p, s, more) > 0 || fleetUse(G, p, s, more) > std::max(fleetLimit(G, p), fleetUse(G, p, s, f))) break;
                        ++room;
                    }
                }
                curValue_ = G.fixes >= 15 ? room + 1 : 0;  // (for the AI: 1 + how many fit)
                const int c = fpick(p, Ab::DsKhazRin, "Khaz-Rin Li-Zho: how many fighters in " + sysName(s) + "?", n,
                                    G.fixes >= 15 ? std::to_string(room) + " fit there (capacity and fleet pool); any more are removed." : "");
                curValue_ = 0;
                if (c <= 0) continue;
                addUnits(p, UnitType::Fighter, c, s, -1);
                if (G.fixes >= 15) {
                    say(p, pn(p) + " places " + std::to_string(c) + " fighter" + (c > 1 ? "s" : "") + " in " + sysName(s) + " (Khaz-Rin Li-Zho).", s);
                    removeExcessCapacity(p, s);
                }
                left -= c;
            }
            return true;
        }
        default: return ds7Action(p, ab);  // Mirveda, Ghoti (rules_ds7.cpp)
    }
}

}  // namespace ti
