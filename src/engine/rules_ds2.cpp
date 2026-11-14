// Discordant Stars batch 2 (0.38.0): the abilities of six more factions, called from the ds* hooks in rules_ds.cpp and
// from a few new ones (bombardment, invasion, research, passing, secret objectives). Texts in faction_text.cpp, the AI
// in ai/ai_ds.cpp.
//   L'tokk Khrask        - Lithoids (query voteValue), Garden Worlds (query attachBonus), Meteor Slings / Stone Speakers,
//                          the Splintering Gale (query extraSustains), Stone's Embrace, Udosh B'rtul, Hkot Tokal, Vehl
//                          Tikar, Megalith
//   Zealots of Rhodun    - Conspirators, Ancient Knowledge (query skipPlanets: each specialty twice), Reckoning,
//                          Sanctification Field (G.fx.blankTech), Pilgrimage Beacons, Favor of Rhodun, Priestess Tuh,
//                          Bishop Ulin, Saint Binal, Templar
//   Zelian Purifier      - Obsessive Designs, Biophobic (voteValue), World-Cracker, Impactor (data), Shard Volley,
//                          Hyperkinetic Ordnance, Zelian A / B / R, Collider
//   Roh'Dhna Mechatronics- Industrious, Recycled Materials, Orbital Foundries (structuresOf), Ky'vir, Terrafactory (data),
//                          Contractual Obligations, Automatons (productionCapacity), Rond Bri'ay, B-Unit 205643a,
//                          Roh'Vhin Dhna mk4, Autofabricator
//   Tnelis Syndicate     - Plausible Deniability, Information Brokers (secret limit), Principia Aneris, Blockade Runner
//                          II (query shipReach), Daedalon Flight System, Plots Within Plots (their note), Davish S'Norri,
//                          Fillipo Rois, Turra Sveyar (Player::dsPubSecret), Daedalon mech
//   Monks of Kolume      - Starfall Gunnery, Deliberate Action, Meditation, Halberd, Applied Biothermics, Omniscience
//                          Field (G.fx.omniPlayer), Combinatorial Bypass (G.fx.bypass), Disciple Fran, Issac of Sinci,
//                          Wonell the Silent, Rook
#include <algorithm>
#include <cstring>

#include "engine/rules.h"
#include "engine/map.h"

namespace ti {

namespace {
void ds2Event(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    if (text.find(name) == std::string::npos) {
        e.text = text.size() && text.back() == '.' ? text.substr(0, text.size() - 1) : text;
        e.text += " (" + name + ").";
    }
    e.system = int16_t(sys);
    E.emit(e);
}
void ds2Hero(Engine& E, const Game& G, int p, const std::string& text) {
    const int i = leaderIndex(G, p, kHero);
    Event e;
    e.kind = EventKind::Leader;
    e.player = int8_t(p);
    e.card = int16_t(i >= 0 ? G.players[size_t(p)].leaders[size_t(i)].leader : 0);
    e.label = "hero";
    e.text = text;
    E.emit(e);
}
int upgradesOf(const Game& G, int q) {
    if (q < 0 || q >= G.numPlayers) return 0;
    int n = 0;
    for (int16_t t : G.players[size_t(q)].techs) n += kTechs[t].unitUpgrade ? 1 : 0;
    return n;
}
int factionTechsOf(const Game& G, int q) {
    if (q < 0 || q >= G.numPlayers) return 0;
    int n = 0;
    for (int16_t t : G.players[size_t(q)].techs) n += kTechs[t].faction >= 0 ? 1 : 0;
    return n;
}
int asteroidsNear(const Game& G, int sys) {
    int n = 0;
    for (int16_t a : G.systems[size_t(sys)].adj) n += (G.systems[size_t(a)].d().anomalies & kAsteroid) ? 1 : 0;
    return n;
}
}  // namespace

// ================================================================== combat additions (called from dsCombatMods)
void Rules::ds2CombatMods(int p, int opp, int sys, int planet, bool space, RollMods& m) {
    if (space && flagshipAbility(G, p, "zealots") && flagshipIn(G, p, sys)) m.perType[int(UnitType::Flagship)] += upgradesOf(G, opp);  // Reckoning
    if (!space && planet >= 0 && fac(p, "zealots") && mechAbilities(G) && G.planets[size_t(planet)].units[size_t(p)].count(UnitType::Mech))
        m.perType[int(UnitType::Mech)] += factionTechsOf(G, opp);  // Templar
    if (space && flagshipAbility(G, p, "zelian") && flagshipIn(G, p, sys)) m.extraDice[int(UnitType::Flagship)] += asteroidsNear(G, sys);  // World-Cracker
    // Principia Aneris (Tnelis flagship): one of the opponent's ships rolls 1 die less each round - their best
    if (space && opp >= 0 && opp < G.numPlayers && flagshipAbility(G, opp, "tnelis") && flagshipIn(G, opp, sys)) {
        int best = -1;
        for (int t : kShipIdx)
            if (G.systems[size_t(sys)].space[size_t(p)].n[t] && (best < 0 || unitDef(G, p, UnitType(t)).combatDice > unitDef(G, p, UnitType(best)).combatDice))
                best = t;
        if (best >= 0) m.extraDice[best] -= 1;
    }
    if (G.fx.omniPlayer == p) m.all(G.fx.omniMod);  // Omniscience Field
    ds3CombatMods(p, opp, sys, planet, space, m);  // batch 3 (rules_ds3.cpp)
}

// Extra bombardment dice: Davish S'Norri (an anti-fighter barrage used as bombardment).
void Rules::dsBombardDice(int p, int sys, RollResult& r, int mod, const Force& shooters) {
    // the Lady (Ghemina flagship) and Raid Leaders: Bombardment 5 (batch 5)
    for (int t = 0; t < kUnitTypes; ++t)
        if (shooters.n[t])
            if (const int k = dsBombardDiceOf(G, p, sys, UnitType(t))) addDice(r, k, 5 - mod, t);
    if (dsAfbBombard_ < 0 || p != G.activePlayer) return;
    const UnitType t = UnitType(dsAfbBombard_);
    dsAfbBombard_ = -1;
    if (!G.systems[size_t(sys)].space[size_t(p)].count(t)) return;
    const UnitDef& u = unitDef(G, p, t);
    if (u.afb <= 0) return;
    addDice(r, std::max<int>(1, u.afbDice), u.afb - mod, int(t));
}

// Hits of one bombardment roll against a planet (q's ground forces there): Shard Volley, Hyperkinetic Ordnance, Meteor
// Slings / Stone Speakers (hits turned into infantry landing on the planet).
void Rules::dsBombardHits(int p, int q, int sys, int pl, RollResult& r) {
    if (r.hits <= 0) return;
    if (owns(p, "dszelir")) {  // Shard Volley
        ++r.hits;
        say(p, pn(p) + " makes 1 more hit (Shard Volley).", sys);
    }
    const int z = playerOfFaction(G, "zelian");
    if (z >= 0 && z != p && holdsNote(G, p, z, kNoteFaction) &&
        fyes(p, Ab::DsHyperkinetic, "Play " + pn(z) + "'s Hyperkinetic Ordnance: 1 more bombardment hit?", "")) {
        ++r.hits;
        returnNote(p, makeNote(z, kNoteFaction));
        ds2Event(E, p, "Hyperkinetic Ordnance", pn(p) + " makes 1 more hit.", sys);
    }
    // Meteor Slings (Khrask: against another player's planet) / Stone Speakers (the Khrask's note, a tactical action)
    const int k = playerOfFaction(G, "khrask");
    const bool slings = fac(p, "khrask") && G.planets[size_t(pl)].owner >= 0 && G.planets[size_t(pl)].owner != p;
    const bool speakers = k >= 0 && k != p && holdsNote(G, p, k, kNoteFaction) && p == G.activePlayer;
    const int inf = std::min(r.hits, unitsAvailable(G, p, UnitType::Infantry));
    if ((slings || speakers) && inf > 0 && !isDMZ(G, pl)) {
        std::vector<Option> opts;
        for (int n = 0; n <= inf; ++n) opts.push_back({n, std::to_string(n), n ? std::to_string(n) + " infantry land instead of " + std::to_string(n) + " hits" : "Keep the hits"});
        curValue_ = pl;
        const int n = fpick(p, slings ? Ab::DsMeteorSlings : Ab::DsStoneSpeakers,
                            std::string(slings ? "Meteor Slings" : "Stone Speakers") + ": turn hits into infantry on " + planetName(pl) + "?", opts);
        curValue_ = 0;
        if (n > 0) {
            r.hits -= n;
            addUnits(p, UnitType::Infantry, n, sys, pl);
            // Mended Grove (Khrask breakthrough, 0.43.0): every hit cancelled - they may coexist instead of fighting
            const bool grove = slings && r.hits == 0 && breakthroughsOn(G) && dsbtSlingsCoexist(p, pl, n);
            if (!grove && std::find(dsSlingPlanets_.begin(), dsSlingPlanets_.end(), pl) == dsSlingPlanets_.end()) dsSlingPlanets_.push_back(pl);
            if (!slings) returnNote(p, makeNote(k, kNoteFaction));
            ds2Event(E, p, slings ? "Meteor Slings" : "Stone Speakers", pn(p) + " hurls " + std::to_string(n) + " infantry down onto " + planetName(pl) + ".", sys);
        }
    }
}

// Start of an invasion (p active, before the bombardment): Hkot Tokal, Zelian A, Collider, Davish S'Norri,
// Combinatorial Bypass.
void Rules::dsInvasionStart(int p, int sys) {
    dsAfbBombard_ = -1;
    ds7InvasionStart(p, sys);  // Combat Drones, Orbital Defense Grid (batch 6)
    SystemState& s = G.systems[size_t(sys)];
    // Combinatorial Bypass (the Kolume's note)
    const int ko = playerOfFaction(G, "kolume");
    if (ko >= 0 && ko != p && holdsNote(G, p, ko, kNoteFaction) &&
        fyes(p, Ab::DsBypass, "Play " + pn(ko) + "'s Combinatorial Bypass: other players' units lose Space Cannon and Planetary Shield in this invasion?", "")) {
        const int16_t note = int16_t(makeNote(ko, kNoteFaction));
        Player& pl = G.players[size_t(p)];
        pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), note), pl.notes.end());
        pl.notesInPlay.push_back(note);  // (back to the Kolume at the start of p's next turn)
        G.fx.bypass = int8_t(p);
        ds2Event(E, p, "Combinatorial Bypass", pn(p) + "'s invasion ignores space cannon and planetary shields.", sys);
    }
    // Hkot Tokal (Khrask commander): 1 infantry into the space area if there's room
    if (commanderUsable(G, p, "khrask") && unitsAvailable(G, p, UnitType::Infantry) > 0 && overflowOf(G, p, sys, s.space[size_t(p)]) == 0) {
        Force f = s.space[size_t(p)];
        f.add(UnitType::Infantry, 1);
        if (overflowOf(G, p, sys, f) == 0) {
            addUnits(p, UnitType::Infantry, 1, sys, -1);
            say(p, pn(p) + " brings 1 more infantry (Hkot Tokal).", sys);
        }
    }
    // Zelian A (Zelian agent): an infantry in space becomes a mech
    if (s.space[size_t(p)].count(UnitType::Infantry) && unitsAvailable(G, p, UnitType::Mech) > 0) {
        const int user = agentUser(p, "zelian");
        if (user >= 0) {
            agentFor(user, p);
            const bool use = fyes(user, Ab::DsZelianAgent, user == p ? "Exhaust Zelian A: swap 1 infantry in space for a mech?" : "Exhaust Zelian A for " + pn(p) + "?",
                                  leaderText(findLeader("zelianagent")));
            curFor_ = -1;
            if (use) {
                spendAgent(user, "zelian");
                agentDone(user, p);
                s.space[size_t(p)].remove(UnitType::Infantry, 1);
                s.space[size_t(p)].add(UnitType::Mech, 1);
                ds2Event(E, user, "Zelian A", pn(p) + "'s infantry becomes a mech.", sys);
            }
        }
    }
    // Collider (Zelian mech in space): remove it to destroy 1 unit on a planet here
    if (fac(p, "zelian") && mechAbilities(G) && s.space[size_t(p)].count(UnitType::Mech)) {
        std::vector<Option> opts;  // id = planet * 16 + type (+ player in the label)
        for (int16_t x : s.planets)
            for (int q = 0; q < G.numPlayers; ++q) {
                if (q == p) continue;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (G.planets[size_t(x)].units[size_t(q)].n[t]) opts.push_back({(x * 16 + t) * 8 + q, pn(q) + "'s " + unitTypeName(UnitType(t)) + " on " + planetName(x), ""});
            }
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            const int c = fpick(p, Ab::DsCollider, "Collider: sacrifice a mech to destroy a unit on a planet here?", opts);
            if (c >= 0) {
                const int q = c % 8, x = (c / 8) / 16, t = (c / 8) % 16;
                removing_ = true;
                destroyUnits(p, UnitType::Mech, 1, sys, -1, -1, "Collider");
                removing_ = false;
                destroyUnits(q, UnitType(t), 1, sys, x, p, "Collider");
                ds2Event(E, p, "Collider", pn(p) + "'s mech slams into " + planetName(x) + ".", sys);
            }
        }
    }
    // Davish S'Norri (Tnelis agent): one ship's anti-fighter barrage counts as bombardment
    const int user = agentUser(p, "tnelis");
    if (user >= 0) {
        std::vector<Option> opts;
        for (int t : kShipIdx)
            if (s.space[size_t(p)].n[t] && unitDef(G, p, UnitType(t)).afb > 0)
                opts.push_back({t, unitTypeName(UnitType(t)), "Anti-Fighter Barrage " + std::to_string(unitDef(G, p, UnitType(t)).afb) + " (x" +
                                                                    std::to_string(std::max<int>(1, unitDef(G, p, UnitType(t)).afbDice)) + ")"});
        bool enemies = false;
        for (int16_t x : s.planets)
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && groundForcesOn(G, q, x)) enemies = true;
        if (!opts.empty() && enemies) {
            opts.push_back({-1, "No", ""});
            agentFor(user, p);
            const int t = fpick(user, Ab::DsDavish, "Exhaust Davish S'Norri: one ship's anti-fighter barrage bombards this time?", opts,
                                leaderText(findLeader("tnelisagent")));
            curFor_ = -1;
            if (t >= 0) {
                spendAgent(user, "tnelis");
                agentDone(user, p);
                dsAfbBombard_ = t;
            }
        }
    }
}

// Megalith (Khrask mech): in the first round of a ground combat on its planet only the Khrask mechs roll.
void Rules::dsGroundRollers(int att, int def, int planet, Force& fa, Force& fd) {
    if (combatRound_ != 1 || !mechAbilities(G)) return;
    for (int side : {att, def}) {
        if (side >= G.numPlayers || !fac(side, "khrask") || !G.planets[size_t(planet)].units[size_t(side)].count(UnitType::Mech)) continue;
        Force& mine = side == att ? fa : fd;
        Force& theirs = side == att ? fd : fa;
        for (int t = 0; t < kUnitTypes; ++t)
            if (UnitType(t) != UnitType::Mech) mine.n[t] = mine.dmg[t] = 0;
        theirs = Force();
        say(side, pn(side) + "'s Megalith: only its mechs roll in the first round.", G.planets[size_t(planet)].system);
    }
}

// Space cannon: Starfall Gunnery (the Kolume: up to 3 non-fighter ships with Space Cannon 8 on their own actions; only
// one unit's cannon on other players' actions), the Rook's hits only on fighters, Halberd (no cancelling).
int Rules::dsCannonDice(int q, int p, int sys) const {
    if (q != p || !fac(q, "kolume")) return 0;
    return std::min(3, G.systems[size_t(sys)].space[size_t(q)].nonFighterShips());
}
void Rules::dsCannonLimit(int q, int p, Force& guns) const {
    if (q == p || q >= G.numPlayers || !fac(q, "kolume") || guns.total() <= 1) return;
    int best = -1;
    for (int t = 0; t < kUnitTypes; ++t)
        if (guns.n[t] && (best < 0 || unitDef(G, q, UnitType(t)).cannon < unitDef(G, q, UnitType(best)).cannon)) best = t;
    Force one;
    one.add(UnitType(best), 1);
    guns = one;
}
int Rules::dsAbilityMod(int p) const {
    int m = 0;
    if (commanderUsable(G, p, "kolume") && fac(p, "kolume")) m += 1;  // Issac of Sinci
    if (G.fx.omniPlayer == p) m += G.fx.omniMod;                     // Omniscience Field
    return m;
}

// After a round of space combat: Daedalon Flight System (Tnelis), Nexus later.
void Rules::dsSpaceRoundEnd(int att, int def, int sys) {
    ds3SpaceRoundEnd(att, def, sys);  // Hulgade's Hammer (batch 3)
    ds5RoundEnd(att, def, sys, true);  // Necrophage (batch 5)
    ds6SpaceRoundEnd(att, def, sys);   // SEIDR Project (batch 6)
    ds7SpaceRoundEnd(att, def, sys);   // Nexus
    for (int side : {att, def}) {
        const int o = side == att ? def : att;
        if (side >= G.numPlayers || !owns(side, "dstnely")) continue;
        Force* mech = nullptr;
        if (G.systems[size_t(sys)].space[size_t(side)].count(UnitType::Mech) > G.systems[size_t(sys)].space[size_t(side)].damaged(UnitType::Mech))
            mech = &G.systems[size_t(sys)].space[size_t(side)];
        for (int16_t x : G.systems[size_t(sys)].planets)
            if (!mech && G.planets[size_t(x)].units[size_t(side)].count(UnitType::Mech) > G.planets[size_t(x)].units[size_t(side)].damaged(UnitType::Mech))
                mech = &G.planets[size_t(x)].units[size_t(side)];
        const Force& tf = G.systems[size_t(sys)].space[size_t(o)];
        const int targets = tf.count(UnitType::Cruiser) + tf.count(UnitType::Dreadnought) + tf.count(UnitType::Destroyer);
        if (!mech || !targets) continue;
        if (!fyes(side, Ab::DsDaedalonTech, "Daedalon Flight System: damage a mech to make 1 hit on a cruiser, dreadnought or destroyer?", "")) continue;
        mech->dmg[int(UnitType::Mech)] = uint8_t(mech->dmg[int(UnitType::Mech)] + 1);
        ds2Event(E, side, "Daedalon Flight System", pn(side) + "'s mech strafes the enemy line: 1 hit.", sys);
        int mask = (1 << int(UnitType::Cruiser)) | (1 << int(UnitType::Dreadnought)) | (1 << int(UnitType::Destroyer));
        if (G.fixes >= 9) {  // (0.44.5: the Tnelis assign the hit - which kind of ship takes it; its owner may still sustain)
            std::vector<Option> kinds;
            for (UnitType t : {UnitType::Dreadnought, UnitType::Cruiser, UnitType::Destroyer})
                if (tf.count(t)) kinds.push_back({int(t), unitTypeName(t), std::to_string(tf.count(t)) + " there"});
            const int t = kinds.size() == 1 ? kinds[0].id : fpick(side, Ab::DsDaedalonTech, "Daedalon Flight System: the hit goes to which ship?", kinds);
            mask = 1 << t;
        }
        assignHits(o, sys, -1, 1, mask, "Daedalon Flight System", side);
    }
}

// Contractual Obligations (0.44.5, fixes 9): the player chooses the system - one with their space dock (a floating
// factory too) or war sun - and must produce 1 ship there if they can (an ability, not PRODUCTION: no Sarween Tools)
void Rules::contractualProduce(int q) {
    std::vector<Option> opts;
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (!otherShipsIn(G, q, int(s)) && (docksIn2(q, int(s)) || G.systems[s].space[size_t(q)].count(UnitType::SpaceDock) ||
                                             G.systems[s].space[size_t(q)].count(UnitType::WarSun)))
            opts.push_back({int(s), sysName(int(s)), ""});
    if (opts.empty()) return;
    const int where = opts.size() == 1 ? opts[0].id
                                       : pick(q, Tag::ComponentChoice, "Contractual Obligations: produce 1 ship in which system?", opts, "", true);
    mustProduce_ = true;
    notProductionAbility_ = true;
    productionCore(q, where, 1, false, true);
    notProductionAbility_ = false;
    mustProduce_ = false;
}

// ================================================================== research, strategy, secrets, passing
// After p researched technology t: Priestess Tuh (Rhodun agent), Obsessive Designs (Zelian).
void Rules::dsResearched(int p, int t) {
    if (p < 0 || p >= G.numPlayers || t < 0) return;
    ds3Researched(p, t);  // Iledrith, Clona Bathru unlocks (batch 3)
    ds7Researched(p, t);  // Logic Machina (batch 6)
    Player& pl = G.players[size_t(p)];
    if (fac(p, "zelian") && kTechs[t].unitUpgrade && G.phase == Phase::Action && pl.homeSystem >= 0) {
        int type = -1;
        for (int u = 0; u < kUnitCount; ++u)
            if (kUnits[u].tech == t) type = int(kUnits[u].type);
        const int home = pl.homeSystem;
        if (type >= 0 && !isStructure(UnitType(type)) && docksIn2(p, home) &&
            fyes(p, Ab::DsObsessive, std::string("Obsessive Designs: produce ") + unitTypeName(UnitType(type), true) + " at home, 2 cheaper?", "")) {
            dsTypeMask_ = 1 << type;
            dsDiscount_ = 2;
            ds2Event(E, p, "Obsessive Designs", pn(p) + " builds its new design at once.", home);
            productionCore(p, home, productionCapacity(G, p, home), false, false);
            dsTypeMask_ = 0;
            dsDiscount_ = 0;
        }
    }
    const int user = agentUser(p, "zealots");
    if (user >= 0) {
        std::vector<Option> opts;
        auto add = [&](int s) {
            if (s < 0 || otherShipsIn(G, p, s)) return;
            for (const Option& o : opts)
                if (o.id == s) return;
            opts.push_back({s, sysName(s), ""});
        };
        add(pl.homeSystem);
        for (int x : controlledPlanets(G, p))
            if (planetHasSkip(G, x)) add(G.planets[size_t(x)].system);
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            agentFor(user, p);
            const int s = fpick(user, Ab::DsPriestess, user == p ? "Exhaust Priestess Tuh: produce 1 ship?" : "Exhaust Priestess Tuh so " + pn(p) + " produces 1 ship?", opts,
                                leaderText(findLeader("zealotsagent")), true);
            curFor_ = -1;
            if (s >= 0) {
                spendAgent(user, "zealots");
                agentDone(user, p);
                dsProduceOne(p, s, 0, "Priestess Tuh");
            }
        }
    }
}

bool Rules::docksIn2(int p, int sys) const {
    for (int16_t x : G.systems[size_t(sys)].planets)
        if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock)) return true;
    return false;
}

// Planets exhausted for their technology specialties: Ancient Knowledge's commodity, Pilgrimage Beacons.
void Rules::dsSpecialtyUsed(int p, const std::vector<int>& planets) {
    if (planets.empty() || p < 0 || p >= G.numPlayers) return;
    Player& pl = G.players[size_t(p)];
    if (fac(p, "zealots")) {
        const int n = std::min<int>(int(planets.size()), commodityCap(G, pl) - pl.commodities);
        if (n > 0) {
            { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + n); commodityGained(p, cb_); }
            say(p, pn(p) + " gains " + std::to_string(n) + " commodit" + (n > 1 ? "ies" : "y") + " (Ancient Knowledge).");
        }
    }
    if (G.fixes < 11 && owns(p, "dsrhodb") && !pl.dsPilgrim && p == G.activePlayer && G.phase == Phase::Action) {
        std::vector<Option> opts;
        for (int x : planets) {
            const int s = G.planets[size_t(x)].system;
            if (s < 0 || otherShipsIn(G, p, s)) continue;
            bool dup = false;
            for (const Option& o : opts) dup = dup || o.id == s;
            if (!dup) opts.push_back({s, sysName(s), ""});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            const int s = fpick(p, Ab::DsPilgrimage, "Pilgrimage Beacons: produce 1 ship in a system of those planets?", opts, "", true);
            if (s >= 0) {
                pl.dsPilgrim = true;
                dsProduceOne(p, s, 0, "Pilgrimage Beacons");
            }
        }
    }
}

// Pilgrimage Beacons (0.44.7, fixes 11): after the action in which specialty planets of ours were exhausted - once a
// turn, 1 ship in one of their systems (another ability's production: no Sarween Tools); used only if a ship is made
void Rules::pilgrimage(int p) {
    std::vector<int> systems;
    systems.swap(pilgrimSystems_);
    if (systems.empty() || p < 0 || p >= G.numPlayers || G.players[size_t(p)].eliminated || G.players[size_t(p)].dsPilgrim ||
        !owns(p, "dsrhodb"))
        return;
    std::vector<Option> opts;
    for (int s : systems)
        if (!otherShipsIn(G, p, s)) opts.push_back({s, sysName(s), ""});
    if (opts.empty()) return;
    opts.push_back({-1, "No", ""});
    const int s = fpick(p, Ab::DsPilgrimage, "Pilgrimage Beacons: produce 1 ship in a system of the specialty planets you exhausted?", opts,
                        "Once a turn. Paid as usual.", true);
    if (s < 0) return;
    const bool na = notProductionAbility_;
    notProductionAbility_ = true;
    const bool made = dsProduceOne(p, s, 0, "Pilgrimage Beacons");
    notProductionAbility_ = na;
    if (made) G.players[size_t(p)].dsPilgrim = true;
}

// The extra prerequisite waivers: Favor of Rhodun (their note), Bishop Ulin (their commander). Returns how many.
// (use: spend one of them - the note first - and return 1)
int Rules::dsWaivers(int p, int t, bool use) {
    int n = 0;
    const int z = playerOfFaction(G, "zealots");
    if (z >= 0 && z != p && holdsNote(G, p, z, kNoteFaction) && t >= 0) {
        if (use) {
            returnNote(p, makeNote(z, kNoteFaction));
            ds2Event(E, p, "Favor of Rhodun", pn(p) + " ignores another prerequisite of " + kTechs[t].name + ".");
            return 1;
        }
        ++n;
    }
    // Strain Dispersal (Kyro breakthrough, 0.43.0): an action card discarded ignores 1 prerequisite
    if (t >= 0 && breakthroughsOn(G) && breakthroughIs(G, p, "kyro") && !G.players[size_t(p)].actionCards.empty()) {
        if (use) {
            std::vector<Option> opts;
            for (int16_t c : G.players[size_t(p)].actionCards) opts.push_back({c, kActionCards[c].name, ""});
            const int c = opts.size() == 1 ? opts[0].id : fpick(p, Ab::DsbtKyro, "Strain Dispersal: discard which action card to ignore a prerequisite?", opts);
            auto& hand = G.players[size_t(p)].actionCards;
            hand.erase(std::find(hand.begin(), hand.end(), int16_t(c)));
            discardActionCard(p, c, false);
            say(p, pn(p) + " ignores a prerequisite of " + kTechs[t].name + " (Strain Dispersal).");
            return 1;
        }
        ++n;
    }
    if (commanderUsable(G, p, "zealots") && fac(p, "zealots") && t >= 0) {
        int spare = -1;
        for (int x : ti::readyPlanets(G, p))
            if (planetHasSkip(G, x)) spare = x;
        if (spare >= 0) {
            if (use) {
                G.planets[size_t(spare)].exhausted = true;
                if (G.fixes >= 11) planetsExhausted(p, {spare});  // (0.44.7: Arcane Citadel, Pilgrimage Beacons, Olradin)
                say(p, pn(p) + " uses " + planetName(spare) +
                           "'s technology specialty to ignore a prerequisite of any colour (Bishop Ulin) - the planet is exhausted, as for "
                           "any specialty.");
                return 1;
            }
            ++n;
        }
    }
    return n;
}

// Plausible Deniability (Tnelis) / Plots Within Plots (their note): one more secret, then one back into the deck.
void Rules::dsSecretDrawn(int p) {
    if (p < 0 || p >= G.numPlayers || G.secretDeck.empty() || dsDrawing_) return;
    const int tn = playerOfFaction(G, "tnelis");
    const bool own = fac(p, "tnelis");
    const bool note = tn >= 0 && tn != p && holdsNote(G, p, tn, kNoteFaction);
    if (!own && !(note && fyes(p, Ab::DsTnelisNote, "Play " + pn(tn) + "'s Plots Within Plots: draw 1 more secret objective, then return 1?", ""))) return;
    if (note && !own) returnNote(p, makeNote(tn, kNoteFaction));
    Player& pl = G.players[size_t(p)];
    const int o = G.secretDeck.back();
    G.secretDeck.pop_back();
    pl.secrets.push_back(int16_t(o));
    E.revealed();
    std::vector<Option> opts;
    for (int16_t x : pl.secrets) opts.push_back({x, kObjectives[x].name, objectiveSummary(x)});
    const int back = pick(p, Tag::DiscardSecret, std::string(own ? "Plausible Deniability" : "Plots Within Plots") + ": return 1 secret objective to the deck", opts);
    pl.secrets.erase(std::find(pl.secrets.begin(), pl.secrets.end(), back));
    G.secretDeck.push_back(int16_t(back));
    E.rng().shuffle(G.secretDeck);
    say(p, pn(p) + " draws 1 more secret objective and returns 1 (" + (own ? "Plausible Deniability" : "Plots Within Plots") + ").");
}

// A secret objective scored by another player: Turra Sveyar (Tnelis hero) may attach to it.
void Rules::dsSecretScored(int scorer, int obj) {
    for (int p = 0; p < G.numPlayers; ++p) {
        if (p == scorer || !fac(p, "tnelis") || leaderStatus(G, p, kHero) != kReady || G.players[size_t(p)].eliminated) continue;
        if (hasScored(G, p, obj)) continue;
        curValue_ = meetsObjective(G, p, obj) ? 2 : G.players[size_t(p)].dsPubSecret < 0 ? 1 : 0;
        const bool use = fyes(p, Ab::DsTurra, std::string("Turra Sveyar: attach to ") + kObjectives[obj].name + " and score it like a public objective?",
                              leaderText(findLeader("tnelishero")));
        curValue_ = 0;
        if (!use) continue;
        G.players[size_t(p)].dsPubSecret = int16_t(obj);
        ds2Event(E, p, "Contract Network", pn(p) + " buys a copy of " + pn(scorer) + "'s " + kObjectives[obj].name + ".");
    }
}

// Passing: Deliberate Action (a token from the reinforcements into an empty pool), Applied Biothermics.
void Rules::dsPassed(int p) {
    if (breakthroughsOn(G)) dsbtPass(p);  // Aurora Research Base, Cult of the All-Mother (0.43.0)
    Player& pl = G.players[size_t(p)];
    ds3Passed(p);  // Heroism, Skald (batch 3)
    ds4Passed(p);  // Knak Half-Ear (batch 4)
    ds5Passed(p);  // Allant (batch 5)
    if (fac(p, "kolume") && reinforcementTokens(p) > 0 && (pl.tactic == 0 || pl.fleet == 0 || pl.strategy == 0)) {
        std::vector<Option> opts;
        if (pl.tactic == 0) opts.push_back({0, "Tactic pool", ""});
        if (pl.fleet == 0) opts.push_back({1, "Fleet pool", ""});
        if (pl.strategy == 0) opts.push_back({2, "Strategy pool", ""});
        opts.push_back({-1, "No", ""});
        const int c = fpick(p, Ab::DsDeliberate, "Deliberate Action: 1 command token from your reinforcements into an empty pool?", opts);
        if (c == 0) ++pl.tactic;
        if (c == 1) ++pl.fleet;
        if (c == 2) ++pl.strategy;
        if (c >= 0) say(p, pn(p) + " places a command token (Deliberate Action).");
    }
    if (owns(p, "dskolug")) {  // Applied Biothermics
        int n = 0;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && G.players[size_t(q)].passed && !G.players[size_t(q)].eliminated) ++n;
        n = std::min(n, commodityCap(G, pl) - pl.commodities);
        if (n > 0) {
            { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + n); commodityGained(p, cb_); }
            say(p, pn(p) + " gains " + std::to_string(n) + " commodit" + (n > 1 ? "ies" : "y") + " (Applied Biothermics).");
        }
    }
}

// A strategy token spent: the Rook (Kolume mech) is repaired.
void Rules::dsStrategySpent(int p) {
    if (!fac(p, "kolume") || !mechAbilities(G)) return;
    for (SystemState& s : G.systems) s.space[size_t(p)].dmg[int(UnitType::Mech)] = 0;
    for (PlanetState& ps : G.planets) ps.units[size_t(p)].dmg[int(UnitType::Mech)] = 0;
}

// Stone's Embrace (Khrask): a planet exhausted to take a strategy token from the reinforcements instead. True = used.
bool Rules::dsStoneEmbrace(int p) {
    if (!owns(p, "dskhrag") || reinforcementTokens(p) <= 0) return false;
    std::vector<Option> opts;
    for (int x : ti::readyPlanets(G, p)) opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + " res / " + std::to_string(planetInfluence(G, x)) + " inf"});
    if (opts.empty()) return false;
    opts.push_back({-1, "Spend the strategy token", ""});
    const int x = fpick(p, Ab::DsStoneEmbrace, "Stone's Embrace: exhaust a planet to spend a token from your reinforcements instead?", opts, "", false, true);
    if (x < 0) return false;
    G.planets[size_t(x)].exhausted = true;
    if (G.fixes >= 11) planetsExhausted(p, {x});  // (0.44.7)
    say(p, pn(p) + " exhausts " + planetName(x) + " instead of a strategy token (Stone's Embrace).");
    return true;
}

// ================================================================== construction, movement, production
// Industrious (Roh'Dhna): a new space dock may become a war sun for 6 resources.
void Rules::dsDockPlaced(int p, int planet) {
    if (!fac(p, "rohdhna")) return;
    const int sys = G.planets[size_t(planet)].system;
    if (otherShipsIn(G, p, sys) || unitsAvailable(G, p, UnitType::WarSun) <= 0 || spendable(G, p, false) < 6) return;
    // (0.46.1, fixes 15 - owner: the war sun counts against the fleet pool; say so when it is full)
    const bool full = G.fixes >= 15 && fleetUseIn(G, p, sys) + 1 > fleetLimit(G, p);
    if (!fyes(p, Ab::DsIndustrious, "Industrious: pay 6 resources to turn the new space dock on " + planetName(planet) + " into a war sun?",
              full ? "Your fleet pool is full there: you will have to remove a ship (your choice) to keep the war sun." : "")) return;
    if (!pay(p, 6, false, "Industrious", false)) return;
    removing_ = true;
    destroyUnits(p, UnitType::SpaceDock, 1, sys, planet, -1, "Industrious");
    removing_ = false;
    addUnits(p, UnitType::WarSun, 1, sys, -1);
    ds2Event(E, p, "Industrious", pn(p) + " raises a war sun from the dock on " + planetName(planet) + ".", sys);
    if (G.fixes >= 15) removeExcessCapacity(p, sys);  // (LRR 37.3: over the fleet pool - the player removes a ship)
}

// After p moved ships into the active system: Cataclysm (Zelian hero), Daedalon (Tnelis mech), Primespace
// Actualization (Roh'Dhna hero).
void Rules::dsAfterMovement(int p, int sys, bool destroyerMoved) {
    if (p < 0 || p >= G.numPlayers || sys < 0) return;
    if (breakthroughsOn(G)) dsbtAfterMovement(p, sys);  // Gravitic Disruption (0.43.0)
    SystemState& s = G.systems[size_t(sys)];
    const bool home = s.home >= 0;
    // Cataclysm: a unit with bombardment moved into a non-home system other than Mecatol Rex
    if (fac(p, "zelian") && heroUnlocked(G, p) && !home && sys != G.mecatol() && !s.planets.empty()) {
        bool bomb = false;
        for (int t = 0; t < kUnitTypes; ++t)
            if (s.space[size_t(p)].n[t] && unitDef(G, p, UnitType(t)).bombard > 0) bomb = true;
        int res = 0;
        for (int16_t x : s.planets) res += planetResources(G, x);
        if (bomb && res > 0) {
            curValue_ = res;
            const bool use = fyes(p, Ab::DsCataclysm, "Purge Zelian R: gain " + std::to_string(res) + " trade goods and destroy every unit on the planets of " +
                                                         sysName(sys) + " - the planets are destroyed?",
                                  leaderText(findLeader("zelianhero")));
            curValue_ = 0;
            if (use) {
                purgeHero(p);
                gainTG(p, res);
                for (int16_t x : s.planets) {
                    for (int q = 0; q < kSides; ++q)
                        for (int t = 0; t < kUnitTypes; ++t)
                            if (G.planets[size_t(x)].units[size_t(q)].n[t]) destroyUnits(q, UnitType(t), G.planets[size_t(x)].units[size_t(q)].n[t], sys, x, p, "Cataclysm");
                    G.planets[size_t(x)].destroyed = true;
                    G.planets[size_t(x)].owner = -1;
                }
                // 0.43.0 (owner D44): the tile becomes the Zelian asteroid field (its planets are gone with it)
                const int rocks = G.fixes >= 2 ? findSystem("d36") : -1;
                if (rocks >= 0) {
                    G.systems[size_t(sys)].planets.clear();
                    G.systems[size_t(sys)].def = int16_t(rocks);
                    G.systems[size_t(sys)].frontier = false;
                    computeAdjacency(G);
                }
                ds2Hero(E, G, p, pn(p) + " drops a world-cracker on " + sysName(sys) + ": its planets are rubble" +
                                     (rocks >= 0 ? " - an asteroid field now." : "."));
                if (rocks >= 0) checkEliminations();
            }
        }
    }
    // Daedalon (Tnelis mech): a destroyer moved into a non-home system (not Mecatol Rex) - 3 resources for a mech
    if (destroyerMoved && fac(p, "tnelis") && mechAbilities(G) && !home && sys != G.mecatol() && unitsAvailable(G, p, UnitType::Mech) > 0 &&
        spendable(G, p, false) >= 3 && !s.planets.empty()) {
        std::vector<Option> opts;
        for (int16_t x : s.planets) {
            bool foes = neutralGroundOn(G, x) > 0;
            for (int q = 0; q < G.numPlayers; ++q) foes = foes || (q != p && groundForcesOn(G, q, x) > 0);
            // 0.43.0 (D50): any planet there, the Tnelis' choice - a defended one means a ground combat in the invasion
            if (!isDMZ(G, x) && !G.planets[size_t(x)].destroyed && (!foes || G.fixes >= 2))
                opts.push_back({x, planetName(x), foes ? "Defended: the mech fights there in the invasion." : ""});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            const int x = fpick(p, Ab::DsDaedalon, "Daedalon: pay 3 resources to put a mech on a planet here?", opts, "", false, true);
            if (x >= 0 && pay(p, 3, false, "Daedalon", false)) {
                addUnits(p, UnitType::Mech, 1, sys, x);
                ds2Event(E, p, "Daedalon", pn(p) + " drops a mech onto " + planetName(x) + ".", sys);
                dsSlingPlanets_.push_back(x);  // (the invasion takes the planet if nobody else is there)
            }
        }
    }
    // Primespace Actualization: produce with every unit's Production here, blockade or not
    if (fac(p, "rohdhna") && heroUnlocked(G, p)) {
        int cap = 0;
        for (int q = 0; q < G.numPlayers; ++q) cap += productionCapacity(G, q, sys);
        if (cap > 0) {
            curValue_ = cap;
            const bool use = fyes(p, Ab::DsPrimespace, "Purge Roh'Vhin Dhna mk4: produce here with every unit's Production (" + std::to_string(cap) + "), blockade or not?",
                                  leaderText(findLeader("rohdhnahero")));
            curValue_ = 0;
            if (use) {
                purgeHero(p);
                ds2Hero(E, G, p, pn(p) + " seizes every factory in " + sysName(sys) + ".");
                G.fx.noBlockade = true;
                productionCore(p, sys, cap, false, G.fixes >= 2);  // ("produce ships", 0.43.0)
                G.fx.noBlockade = false;
            }
        }
    }
}

// After p's units used Production: Rond Bri'ay (Roh'Dhna agent).
void Rules::dsAfterProduction(int p, int sys) {
    const int user = agentUser(p, "rohdhna");
    if (user < 0 || reinforcementTokens(p) <= 0) return;
    agentFor(user, p);
    const bool use = fyes(user, Ab::DsRondBriay, user == p ? "Exhaust Rond Bri'ay: gain 1 command token?" : "Exhaust Rond Bri'ay so " + pn(p) + " gains 1 command token?",
                          leaderText(findLeader("rohdhnaagent")));
    curFor_ = -1;
    if (!use) return;
    spendAgent(user, "rohdhna");
    agentDone(user, p);
    gainTokens(p, 1, false, "Rond Bri'ay: 1 command token.");
    (void)sys;
}

// End of p's turn: Automatons (the Roh'Dhna's note goes onto a planet for good).
void Rules::ds2TurnEnd(int p) {
    const int r = playerOfFaction(G, "rohdhna");
    if (r < 0 || r == p || !holdsNote(G, p, r, kNoteFaction)) return;
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, p)) {
        const PlanetState& ps = G.planets[size_t(x)];
        if (ps.system == G.players[size_t(p)].homeSystem || !std::strcmp(kPlanets[ps.def].key, "mr") || isDMZ(G, x)) continue;
        opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + " res"});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "Not now", ""});
    const int x = fpick(p, Ab::DsAutomatons, "Automatons: attach it to a planet (Production 3 there)?", opts, "", false, true);
    if (x < 0) return;
    Player& pl = G.players[size_t(p)];
    const int16_t note = int16_t(makeNote(r, kNoteFaction));
    pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), note), pl.notes.end());
    G.dsAutomatons = int16_t(x);
    G.dsAutomatonsOwner = int8_t(p);
    ds2Event(E, p, "Automatons", pn(p) + " attaches Automatons to " + planetName(x) + ": it has Production 3.", G.planets[size_t(x)].system);
}

// Start of the agenda phase: Contractual Obligations (Roh'Dhna) - for each of their unit upgrades, a player must
// produce 1 ship.
void Rules::dsAgendaPhaseStart() {
    ds3Agendas_ = 0;  // (Quaxdol Junitas: the second agenda)
    // Envoy Network (Free Systems, 0.43.0 D101): at the start of the agenda phase they may choose and exhaust 1 cultural,
    // 1 hazardous and 1 industrial planet - anyone's, to take votes away from the other players
    if (G.fixes >= 2)
        for (int p = 0; p < G.numPlayers; ++p) {
            if (!owns(p, "dsfreeg") || G.players[size_t(p)].eliminated) continue;
            static const Trait kTr[3] = {Trait::Cultural, Trait::Hazardous, Trait::Industrial};
            static const char* kTn[3] = {"cultural", "hazardous", "industrial"};
            for (int k = 0; k < 3; ++k) {
                std::vector<Option> opts;
                for (size_t x = 0; x < G.planets.size(); ++x) {
                    const PlanetState& ps = G.planets[x];
                    if (ps.owner < 0 || ps.exhausted || ps.destroyed || !hasTraitOf(G, int(x), kTr[k])) continue;
                    opts.push_back({int(x), planetName(int(x)) + " (" + pn(ps.owner) + ")", std::to_string(planetInfluence(G, int(x))) + " influence"});
                }
                if (opts.empty()) continue;
                opts.push_back({-1, "None", ""});
                const int x = fpick(p, Ab::DsEnvoy, std::string("Envoy Network: exhaust a ") + kTn[k] + " planet?", opts,
                                    "Any player's ready planet: it can't give votes in this agenda phase.", false, true);
                if (x < 0) continue;
                G.planets[size_t(x)].exhausted = true;
                ds2Event(E, p, "Envoy Network", pn(p) + "'s envoys tie up " + planetName(x) + " (" + pn(G.planets[size_t(x)].owner) + ").",
                         G.planets[size_t(x)].system);
            }
        }
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!owns(p, "dsrohdy")) continue;
        const int n = upgradesOf(G, p);
        int i = 0;
        // 0.43.0 (D46): players who bought a pick in a deal come first (the promise is kept)
        for (Promise& pr : G.promises) {
            if (i >= n || pr.state != kPromiseActive || pr.kind != PromiseKind::Contract || pr.by != p) continue;
            const int q = pr.to;
            if (q < 0 || q >= G.numPlayers || G.players[size_t(q)].eliminated) continue;
            ++i;
            keepPromise(pr);
            ds2Event(E, p, "Contractual Obligations", pn(q) + " must produce 1 ship (as agreed).");
            if (G.fixes >= 9) {
                contractualProduce(q);
                continue;
            }
            int where = -1;
            for (size_t s = 0; s < G.systems.size() && where < 0; ++s)
                if (!otherShipsIn(G, q, int(s)) && (docksIn2(q, int(s)) || G.systems[s].space[size_t(q)].count(UnitType::WarSun))) where = int(s);
            if (where >= 0) productionCore(q, where, 1, false, true);
        }
        for (; i < n; ++i) {
            std::vector<Option> opts;
            // (0.44.9, fixes 13 - the owner's answer C5: the Roh'Dhna may pick themselves)
            for (int q = 0; q < G.numPlayers; ++q)
                if ((q != p || G.fixes >= 13) && !G.players[size_t(q)].eliminated)
                    opts.push_back({q, q == p ? pn(q) + " (yourself)" : pn(q), std::to_string(availableVotes(G, q)) + " votes ready"});
            if (opts.empty()) break;
            opts.push_back({-1, "Nobody more", ""});
            const int q = fpick(p, Ab::DsContractual, "Contractual Obligations: which player must produce 1 ship?", opts);
            if (q < 0) break;
            ds2Event(E, p, "Contractual Obligations", pn(q) + " must produce 1 ship.");
            // (0.44.9, fixes 13 - C5) a person picked by another player is told and confirms before producing
            if (G.fixes >= 13 && q != p && G.players[size_t(q)].human) {
                curFor_ = p;
                fpick(q, Ab::DsContractual, pn(p) + "'s Contractual Obligations: you must produce 1 ship now", {{1, "Continue", ""}},
                      "At a space dock or war sun of yours - you pay for it as usual (before the agenda phase's votes).");
                curFor_ = -1;
            }
            if (G.fixes >= 9) {
                contractualProduce(q);
                continue;
            }
            int where = -1;
            for (size_t s = 0; s < G.systems.size() && where < 0; ++s)
                if (!otherShipsIn(G, q, int(s)) && (docksIn2(q, int(s)) || G.systems[s].space[size_t(q)].count(UnitType::WarSun))) where = int(s);
            if (where >= 0) productionCore(q, where, 1, false, true);
        }
    }
}

// ================================================================== component actions, heroes
void Rules::ds2Options(int p, std::vector<Option>& o) {
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    const Player& pl = G.players[size_t(p)];
    if (fac(p, "khrask") && heroUnlocked(G, p)) add(Ab::DsVehlTikar, "Hero: Vehl Tikar", "Exhaust or ready up to 4 planets. Purged afterwards.");
    if (fac(p, "zealots") && heroUnlocked(G, p)) add(Ab::DsSaintBinal, "Hero: Saint Binal", "Gain up to 3 technologies; at the next strategy phase you keep only 1.");
    if (fac(p, "kolume") && pl.strategy > 0 && !pl.exhaustedTechs.empty())
        add(Ab::DsMeditation, "Meditation", "Spend a strategy token to ready one of your technologies.");
    if (techReady(p, "dskolug")) add(Ab::DsBiothermics, "Applied Biothermics", "Exhaust it (an action that does nothing else).");
    if (canUseAgent(p, "kolume")) add(Ab::DsFran, "Agent: Disciple Fran", "Exhaust: you (or another player) move 1 command token between pools.");
    if (fac(p, "kolume") && heroUnlocked(G, p)) add(Ab::DsWonell, "Hero: Wonell the Silent", "This round, after each of your component actions, one unit fires its space cannon.");
    ds3Options(p, o);  // batch 3
}

bool Rules::ds2Action(int p, Ab ab) {
    Player& pl = G.players[size_t(p)];
    switch (ab) {
        case Ab::DsVehlTikar: {
            std::vector<Option> opts;
            for (size_t x = 0; x < G.planets.size(); ++x) {
                const PlanetState& ps = G.planets[x];
                if (ps.owner < 0 || ps.destroyed) continue;
                opts.push_back({int(x), planetName(int(x)) + " (" + pn(ps.owner) + ")", ps.exhausted ? "Exhausted: it is readied" : "Ready: it is exhausted"});
            }
            if (opts.empty()) return false;
            purgeHero(p);
            const std::vector<int> picks = fpickMany(p, Ab::DsVehlTikar, "Vehl Tikar: up to 4 planets to exhaust or ready", opts, 0, std::min<int>(4, int(opts.size())), "", true);
            for (int x : picks) G.planets[size_t(x)].exhausted = !G.planets[size_t(x)].exhausted;
            ds2Hero(E, G, p, pn(p) + " sings the Crystal Chorus: " + std::to_string(picks.size()) + " planets wake or sleep.");
            return true;
        }
        case Ab::DsSaintBinal: {
            std::vector<Option> opts;
            for (int t = 0; t < kTechCount; ++t) {
                const TechDef& d = kTechs[t];
                if (d.faction >= 0 || pl.hasTech(t) || !techImplemented(t)) continue;
                if (d.source != Source::Base && d.source != Source::PoK) continue;
                opts.push_back({t, d.name, techSummary(t)});
            }
            if (opts.empty()) return false;
            purgeHero(p);
            const std::vector<int> picks = fpickMany(p, Ab::DsSaintBinal, "Saint Binal: gain up to 3 technologies (you keep 1 at the next strategy phase)", opts, 0, 3);
            for (int t : picks) {
                researchTech(p, t, false);
                pl.dsTempTechs.push_back(int16_t(t));
            }
            ds2Hero(E, G, p, pn(p) + " invokes the Blessing of the Ancients: " + std::to_string(picks.size()) + " technologies for a round.");
            return true;
        }
        case Ab::DsMeditation: {
            if (pl.strategy <= 0 || pl.exhaustedTechs.empty()) return false;
            std::vector<Option> opts;
            for (int16_t t : pl.exhaustedTechs) opts.push_back({t, kTechs[t].name, techSummary(t)});
            const int t = fpick(p, Ab::DsMeditation, "Meditation: ready which technology?", opts);
            spendStrategy(p, 1);
            pl.exhaustedTechs.erase(std::remove(pl.exhaustedTechs.begin(), pl.exhaustedTechs.end(), int16_t(t)), pl.exhaustedTechs.end());
            pl.dsUnlock = true;  // (Issac of Sinci unlocks: a technology readied in the action phase)
            ds2Event(E, p, "Meditation", pn(p) + " readies " + kTechs[t].name + ".");
            return true;
        }
        case Ab::DsBiothermics:
            if (!techReady(p, "dskolug")) return false;
            exhaustTech(p, "dskolug");
            say(p, pn(p) + " exhausts Applied Biothermics.");
            return true;
        case Ab::DsFran: {
            if (!canUseAgent(p, "kolume")) return false;
            std::vector<Option> who;
            for (int q = 0; q < G.numPlayers; ++q)
                if (!G.players[size_t(q)].eliminated) who.push_back({q, pn(q), ""});
            const int q = fpick(p, Ab::DsFran, "Disciple Fran: who moves a command token between pools?", who);
            spendAgent(p, "kolume");
            redistributeOne(q, Tag::TokenRedistribute, "Disciple Fran: move 1 command token to another pool.");
            return true;
        }
        case Ab::DsWonell:
            if (!heroUnlocked(G, p)) return false;
            purgeHero(p);
            pl.heroRound = int16_t(G.round);
            ds2Hero(E, G, p, pn(p) + " overcharges the coils: this round its component actions come with a space cannon shot.");
            return true;
        default: return ds3Action(p, ab);  // batch 3 (rules_ds3.cpp)
    }
}

// Coil Overcharge (Kolume hero, this round): after one of their component actions, one unit fires its space cannon at
// ships in or next to its system.
void Rules::dsAfterComponent(int p) {
    if (!fac(p, "kolume") || G.players[size_t(p)].heroRound != G.round) return;
    std::vector<Option> opts;  // id = (shooter system * 100 + target system) * 16 + unit type
    for (size_t s = 0; s < G.systems.size(); ++s) {
        auto consider = [&](const Force& f, int sys) {
            for (int t = 0; t < kUnitTypes; ++t) {
                if (!f.n[t] || unitDef(G, p, UnitType(t)).cannon <= 0) continue;
                std::vector<int> zone{sys};
                for (int16_t a : G.systems[size_t(sys)].adj) zone.push_back(a);
                for (int z : zone)
                    for (int q = 0; q < G.numPlayers; ++q)
                        if (q != p && hasShips(G, q, z))
                            opts.push_back({(sys * 100 + z) * 16 + t, std::string(unitTypeName(UnitType(t))) + " in " + sysName(sys) + " -> " + sysName(z), ""});
            }
        };
        consider(G.systems[s].space[size_t(p)], int(s));
        for (int16_t x : G.systems[s].planets) consider(G.planets[size_t(x)].units[size_t(p)], int(s));
    }
    if (opts.empty()) return;
    std::sort(opts.begin(), opts.end(), [](const Option& a, const Option& b) { return a.id < b.id; });
    opts.erase(std::unique(opts.begin(), opts.end(), [](const Option& a, const Option& b) { return a.id == b.id; }), opts.end());
    opts.push_back({-1, "No shot", ""});
    const int c = fpick(p, Ab::DsWonell, "Coil Overcharge: fire which unit's space cannon?", opts);
    if (c < 0) return;
    const int t = c % 16, z = (c / 16) % 100;
    int target = -1;
    for (int q = 0; q < G.numPlayers && target < 0; ++q)
        if (q != p && hasShips(G, q, z)) target = q;
    if (target < 0) return;
    Force one;
    one.add(UnitType(t), 1);
    // 0.44.5 (fixes 9): Graviton Laser System and Antimass Deflectors apply to this space cannon shot
    const bool gls = G.fixes >= 9 && askGraviton(p, target, z);
    RollResult r = rollUnits(p, one, RollKind::Cannon, dsAbilityMod(p) - (G.fixes >= 9 ? antimass(p, target) : 0),
                             G.fixes >= 12 ? abilityDice(p, z) : 0, 0, nullptr);  // (0.44.8: Plasma Scoring etc.)
    reportRoll(p, z, "Coil Overcharge at " + pn(target), r);
    if (r.hits) cannonHits(target, z, r.hits, kShipMaskAll, gls, "Coil Overcharge", p);
}

// Activation additions: Sanctification Field, Recycled Materials (+ Autofabricator), Ky'vir, Omniscience Field.
void Rules::ds2Activated(int p, int sys) {
    SystemState& s = G.systems[size_t(sys)];
    // Sanctification Field (Rhodun): blank another player's technology for this tactical action
    for (int z = 0; z < G.numPlayers; ++z) {
        if (!techReady(z, "dsrhody") || G.fx.blankTech >= 0) continue;
        if (z != p && !hasUnits(G, z, sys)) continue;
        std::vector<Option> opts;  // id = player * 1000 + technology
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == z) continue;
            if (q != p && !hasUnits(G, q, sys)) continue;  // (the players involved here)
            for (int16_t t : G.players[size_t(q)].techs) {
                const TechDef& d = kTechs[t];
                if (d.unitUpgrade) {  // 0.43.0 (D38): non-fighter, non-structure unit upgrades too (their abilities go silent)
                    if (G.fixes < 2) continue;
                    int type = -1;
                    for (int u = 0; u < kUnitCount && type < 0; ++u)
                        if (kUnits[u].tech == t) type = int(kUnits[u].type);
                    if (type < 0 || UnitType(type) == UnitType::Fighter || isStructure(UnitType(type))) continue;
                }
                opts.push_back({q * 1000 + t, std::string(d.name) + " (" + pn(q) + ")", techSummary(t)});
            }
        }
        if (opts.empty()) continue;
        opts.push_back({-1, "No", ""});
        const int c = fpick(z, Ab::DsSanctification, "Sanctification Field: which technology has no text this tactical action?", opts);
        if (c < 0) continue;
        G.fx.blankTech = int16_t(c % 1000);
        G.fx.blankOwner = int8_t(c / 1000);
        ds2Event(E, z, "Sanctification Field", pn(c / 1000) + "'s " + kTechs[c % 1000].name + " goes silent.", sys);
    }
    // Omniscience Field (Kolume): +1 / -1 to one player's rolls this tactical action
    for (int k = 0; k < G.numPlayers; ++k) {
        if (!techReady(k, "dskolur") || G.fx.omniPlayer >= 0) continue;
        bool involved = k == p || hasUnits(G, k, sys);
        if (!involved) continue;
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (!G.players[size_t(q)].eliminated) {
                opts.push_back({q * 2 + 1, pn(q) + ": +1", ""});
                opts.push_back({q * 2, pn(q) + ": -1", ""});
            }
        opts.push_back({-1, "No", ""});
        curValue_ = p;
        const int c = fpick(k, Ab::DsOmniscience, "Omniscience Field: +1 or -1 to one player's rolls this tactical action?", opts);
        curValue_ = 0;
        if (c < 0) continue;
        exhaustTech(k, "dskolur");
        G.fx.omniPlayer = int8_t(c / 2);
        G.fx.omniMod = int8_t(c % 2 ? 1 : -1);
        ds2Event(E, k, "Omniscience Field", pn(c / 2) + " rolls " + (c % 2 ? "+1" : "-1") + " this tactical action.", sys);
    }
    if (!fac(p, "rohdhna")) return;
    // Recycled Materials: a cruiser, carrier or dreadnought back for trade goods (and Autofabricator's mech)
    {
        std::vector<Option> opts;
        for (UnitType t : {UnitType::Carrier, UnitType::Cruiser, UnitType::Dreadnought})
            if (s.space[size_t(p)].count(t)) opts.push_back({int(t), unitTypeName(t), std::to_string(std::max(0, unitCost2(G, p, t) / 2 - 1)) + " trade goods"});
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            const int t = fpick(p, Ab::DsRecycled, "Recycled Materials: return a ship to your reinforcements for trade goods?", opts);
            if (t >= 0) {
                const int tg = std::max(0, unitCost2(G, p, UnitType(t)) / 2 - 1);
                removing_ = true;
                destroyUnits(p, UnitType(t), 1, sys, -1, -1, "Recycled Materials");
                removing_ = false;
                removeExcessCapacity(p, sys);
                gainTG(p, tg);
                ds2Event(E, p, "Recycled Materials", pn(p) + " breaks down a " + unitTypeName(UnitType(t)) + " for " + std::to_string(tg) + " trade goods.", sys);
                if (mechAbilities(G) && unitsAvailable(G, p, UnitType::Mech) > 0) {  // Autofabricator
                    std::vector<Option> where{{-2, "The space area", ""}};
                    for (int16_t x : s.planets)
                        if (G.planets[size_t(x)].owner == p && !isDMZ(G, x)) where.push_back({x, planetName(x), ""});
                    where.push_back({-1, "No", ""});
                    const int x = fpick(p, Ab::DsAutofabricator, "Autofabricator: place a mech here?", where);
                    if (x >= -2 && x != -1) {
                        addUnits(p, UnitType::Mech, 1, sys, x >= 0 ? x : -1);
                        ds2Event(E, p, "Autofabricator", pn(p) + " assembles a mech from the parts.", sys);
                    }
                }
            }
        }
    }
    // Ky'vir the Replicator (flagship deploy): 4 resources to swap a ship here for the flagship
    if (unitsAvailable(G, p, UnitType::Flagship) > 0 && spendable(G, p, false) >= 4 && s.space[size_t(p)].nonFighterShips() > 0) {
        std::vector<Option> opts;
        for (int t : kShipIdx)
            if (t != int(UnitType::Fighter) && t != int(UnitType::Flagship) && s.space[size_t(p)].n[t]) opts.push_back({t, unitTypeName(UnitType(t)), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "No", ""});
            const int t = fpick(p, Ab::DsKyvir, "Ky'vir: pay 4 resources to replace a ship here with your flagship?", opts);
            if (t >= 0 && pay(p, 4, false, "Ky'vir", false)) {
                removing_ = true;
                destroyUnits(p, UnitType(t), 1, sys, -1, -1, "Ky'vir");
                removing_ = false;
                addUnits(p, UnitType::Flagship, 1, sys, -1);
                ds2Event(E, p, "Ky'vir", pn(p) + "'s flagship takes the place of a " + unitTypeName(UnitType(t)) + ".", sys);
            }
        }
    }
}

// Udosh B'rtul (Khrask agent): when the influence on hand falls short, one non-home planet pays its resources and
// influence together. Returns the influence it gave (the planet is exhausted), 0 if not used.
int Rules::dsUdosh(int payer, int amount) {
    if (payer < 0 || payer >= G.numPlayers) return 0;
    const int user = agentUser(payer, "khrask");
    if (user < 0) return 0;
    const int have = spendable(G, payer, true);
    // 0.43.0 (D35): a person may use it on any influence payment ("may exhaust"), choosing the planet
    if (G.fixes >= 2 && G.players[size_t(user)].human) {
        std::vector<Option> opts;
        for (int x : ti::readyPlanets(G, payer))
            if (G.planets[size_t(x)].system != G.players[size_t(payer)].homeSystem && planetResources(G, x) > 0)
                opts.push_back({x, planetName(x), std::to_string(planetResources(G, x)) + " resources + " + std::to_string(planetInfluence(G, x)) + " influence"});
        if (opts.empty()) return 0;
        opts.push_back({-1, "Don't", ""});
        agentFor(user, payer);
        const int x = fpick(user, Ab::DsUdosh, user == payer ? "Exhaust Udosh B'rtul: one planet's resources count as influence too?"
                                                             : "Exhaust Udosh B'rtul so " + pn(payer) + " can spend a planet's resources as influence?",
                            opts, leaderText(findLeader("khraskagent")), false, true);
        curFor_ = -1;
        if (x < 0) return 0;
        spendAgent(user, "khrask");
        agentDone(user, payer);
        G.planets[size_t(x)].exhausted = true;
        if (G.fixes >= 11) planetsExhausted(payer, {x});  // (0.44.7)
        const int got = planetResources(G, x) + planetInfluence(G, x);
        ds2Event(E, user, "Udosh B'rtul", pn(payer) + " spends " + planetName(x) + "'s resources as influence (" + std::to_string(got) + ").");
        return got;
    }
    // (only when it makes the payment possible - for bots paying for themselves also when a resource planet saves
    // their influence, 0.43.0 debug passes: asked 0 times in 400 games; fixes >= 2)
    const bool botSaves = G.fixes >= 2 && user == payer && !G.players[size_t(user)].human && amount >= 2;
    if (have >= amount && !botSaves) return 0;
    int best = -1, bestVal = 0;
    for (int x : ti::readyPlanets(G, payer)) {
        if (G.planets[size_t(x)].system == G.players[size_t(payer)].homeSystem) continue;
        const int v = planetResources(G, x);
        if (v > bestVal) {
            bestVal = v;
            best = x;
        }
    }
    if (best < 0 || have + bestVal < amount) return 0;
    agentFor(user, payer);
    const bool use = fyes(user, Ab::DsUdosh, user == payer ? "Exhaust Udosh B'rtul: " + planetName(best) + "'s resources count as influence too?"
                                                           : "Exhaust Udosh B'rtul so " + pn(payer) + " can spend " + planetName(best) + "'s resources as influence?",
                          leaderText(findLeader("khraskagent")));
    curFor_ = -1;
    if (!use) return 0;
    spendAgent(user, "khrask");
    agentDone(user, payer);
    G.planets[size_t(best)].exhausted = true;
    if (G.fixes >= 11) planetsExhausted(payer, {best});  // (0.44.7)
    const int got = planetResources(G, best) + planetInfluence(G, best);
    ds2Event(E, user, "Udosh B'rtul", pn(payer) + " spends " + planetName(best) + "'s resources as influence (" + std::to_string(got) + ").");
    return got;
}

// Zelian B (Zelian commander): their dreadnoughts and war suns without a barrage have Anti-Fighter Barrage 5.
int Rules::dsAfbDice5(int side, int sys) const {
    if (side < 0 || side >= G.numPlayers || !commanderUsable(G, side, "zelian") || !fac(side, "zelian")) return 0;
    int n = 0;
    for (UnitType t : {UnitType::Dreadnought, UnitType::WarSun})
        if (unitDef(G, side, t).afb <= 0) n += G.systems[size_t(sys)].space[size_t(side)].count(t);
    return n;
}

}  // namespace ti
