// Monuments+ (0.82.2) - Discordant Stars, batch 1 (cards in tools/gen_monuments.py, the owner's v3.1 card files; set "DS1", new
// games only - fixes 40). Rulings: the Living Rules first, then the Monuments+ rulebook / the Discordant Stars rules, then
// AsyncTI4; the readings that needed a choice are RULES-CHECK 0.82.2 M39-M50.
//
//   Anvil of Atlas (Axis)        "When you produce 1 or more non-fighter ships in this system, gain 1 trade good." (monProduced,
//                                after productionCore places the units - once per production; any production in this system)
//   Sleeping World (Khrask)      "During ground combat in this system, this unit is treated as a structure and a ground force."
//                                COMBAT 3 (x2), SUSTAIN DAMAGE - it fights in ground combat on its planet, and a ground combat
//                                happens there even when it is their only unit (sleepingWorldOn: invasion + groundCombatRun)
//   Jatta's Palace (Cymiae)      "When another player activates this system they must discard 1 action card, if able." (their
//                                choice - LRR: a player discards from their own hand unless "random" is said)
//   Nightfall Fortress (Li-Zho)  "When a player activates this system, remove a different player's command token from this system,
//                                or gain 1 command token." (the Li-Zho choose; "different" = not the player who activated)
//   Diplomatic Enclave (Olradin) "After you resolve the primary or secondary ability of the "Diplomacy" strategy card, ready this
//                                planet." (monAfterDiplomacy at the end of Rules::diplomacy)
//   Ado Citadel (Mirveda)        "During the first round of ground combat in this system, your opponent's units do not make combat
//                                rolls." (groundCombatRun, round 1)
//   Perdition Array (Veldyr)     "During movement, hits produced by the SPACE CANNON abilities of your units in this system cannot
//                                be canceled." (space cannon offense - the second part of the movement step, LRR 58; the hits
//                                rolled by their units in this system are counted die by die - perditionHits)
//   Freehold Starport (Nokar)    "Your ships may move out of this system, and transport units, even if it contains 1 of your
//                                command tokens." (query freeholdAt at every "units can't leave a system with your token" check)
//   Twilight Throne (Edyn)       "At any time, you may exhaust this card to produce a mech on this planet without spending
//                                resources." (a free option on their turn - 647 - and at the start of a ground combat on that
//                                planet; readied in the status phase like every monument card)
//   Zha'Ren Foundry (Roh'Dhna)   "When you produce any number of units in this system, reduce the combined cost of those units
//                                by 2." (monProductionDiscount in productionCore)
//   Queen's Rest (Kortali)       "When a non-fighter, non-infantry unit you control is destroyed in this system, roll a die. If you
//                                roll equal to or higher than that unit's combat value, or 8 if that unit has no combat value,
//                                produce 1 hit against another player's ships in this system." (destroyUnits notes them, resolved
//                                when the hits being assigned are done - assignHits - or at the next checkpoint)
//   Halls of Valor (Ghemina)     "You may treat this system as adjacent to systems that contain 2 of your structures of the same
//                                unit type." (query routeNeighbors: their movement)

#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
int shipMaskOf() {
    int m = 0;
    for (int t = 0; t < kUnitTypes; ++t)
        if (isShip(UnitType(t))) m |= 1 << t;
    return m;
}
std::vector<int> holdersOf(const Game& g, const char* key) {
    std::vector<int> v;
    if (!(g.modules & kModMonuments)) return v;
    for (int q = 0; q < g.numPlayers; ++q)
        if (!g.players[size_t(q)].eliminated && monCardHas(g, q, key)) v.push_back(q);
    return v;
}
}  // namespace

// ------------------------------------------------------------------ queries
bool sleepingWorldOn(const Game& g, int p, int planet) {
    if (!(g.modules & kModMonuments) || p < 0 || p >= g.numPlayers || planet < 0) return false;
    if (pyramidOn(g, p, planet)) return true;  // (0.85.0) Black Pyramid (Pharad'n): "treated as a structure and a ground force" - always
    int s = -1, pl = -1;
    return monAbilityAt(g, p, "khrask", &s, &pl) && pl == planet && g.planets[size_t(planet)].units[size_t(p)].count(UnitType::Monument) > 0;
}

bool freeholdAt(const Game& g, int p, int sys) {
    if (!(g.modules & kModMonuments) || p < 0 || p >= g.numPlayers) return false;
    int s = -1;
    return monAbilityAt(g, p, "nokar", &s) && s == sys;
}

// the Halls of Valor's links for p's movement: its system <-> each system with 2 of p's structures of one type
void hallsOfValorLinks(const Game& g, int p, int u, std::vector<int>& out) {
    if (!(g.modules & kModMonuments) || p < 0 || p >= g.numPlayers) return;
    int h = -1;
    if (!monAbilityAt(g, p, "ghemina", &h)) return;
    auto twoAlike = [&](int s) {
        int n[kUnitTypes] = {};
        const SystemState& sy = g.systems[size_t(s)];
        for (int t = 0; t < kUnitTypes; ++t) n[t] += sy.space[size_t(p)].n[t];
        for (int16_t x : sy.planets)
            for (int t = 0; t < kUnitTypes; ++t) n[t] += g.planets[size_t(x)].units[size_t(p)].n[t];
        for (int t = 0; t < kUnitTypes; ++t)
            if (isStructure(UnitType(t)) && n[t] >= 2) return true;
        return false;
    };
    auto add = [&](int v) {
        if (v != u && std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
    };
    if (u == h) {
        for (size_t s = 0; s < g.systems.size(); ++s)
            if (!g.systems[s].isHyperlane() && !g.systems[s].offBoard && twoAlike(int(s))) add(int(s));
    } else if (!g.systems[size_t(u)].isHyperlane() && twoAlike(u)) {
        add(h);
    }
}

// ------------------------------------------------------------------ production: Anvil of Atlas, Zha'Ren Foundry
int Rules::monProductionDiscount(int p, int sys) const {
    if (!monumentsOn()) return 0;
    int s = -1;
    return monAbilityAt(G, p, "rohdhna", &s) && s == sys ? 2 : 0;
}

void Rules::monProduced(int p, int sys, const Force& made) {
    if (!monumentsOn()) return;
    int s = -1;
    if (monAbilityAt(G, p, "axis", &s) && s == sys && made.nonFighterShips() > 0) {
        const int card = monumentIndex("axis");
        monEvent(p, card, pn(p) + " gains 1 trade good (" + monName(card) + ").", sys);
        gainTG(p, 1);
    }
}

// ------------------------------------------------------------------ activation: Jatta's Palace, Nightfall Fortress
void Rules::monActivatedDS(int p, int sysAct) {
    if (p < 0 || p >= G.numPlayers) return;
    for (int c : holdersOf(G, "cymiae")) {
        int s = -1;
        if (c == p || !monAbilityAt(G, c, "cymiae", &s) || s != sysAct) continue;
        Player& pl = G.players[size_t(p)];
        if (pl.actionCards.empty()) continue;
        const int card = monumentIndex("cymiae");
        std::vector<Option> opts;
        for (int16_t a : pl.actionCards) {
            bool dup = false;
            for (const Option& o : opts) dup = dup || o.id == a;
            if (!dup) opts.push_back({a, kActionCards[a].name, std::string(actionCardWhen(a)) + ": " + actionCardText(a)});
        }
        monEvent(c, card, pn(p) + " activates " + sysName(s) + ": they must discard 1 action card (" + monName(card) + ").", s);
        const int a = opts.size() == 1 ? opts[0].id
                                       : pick(p, Tag::DiscardActionCard, monName(card) + ": discard 1 action card", opts, kMonuments[card].text);
        pl.actionCards.erase(std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(a)));
        discardActionCard(p, a, false);
    }
    for (int l : holdersOf(G, "lizho")) {
        int s = -1;
        if (!monAbilityAt(G, l, "lizho", &s) || s != sysAct) continue;
        const int card = monumentIndex("lizho");
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && G.systems[size_t(s)].hasToken(q))
                opts.push_back({q, "Remove " + (q == l ? std::string("your") : pn(q) + "'s") + " command token", "It goes back to their reinforcements."});
        const bool canGain = reinforcementTokens(l) > 0;
        if (canGain) opts.push_back({-1, "Gain 1 command token", "Into a pool of your choice."});
        if (opts.empty()) continue;
        curFor_ = p;
        const int c = opts.size() == 1 ? opts[0].id
                                       : mpick(l, card, kMonAskLizho, monName(card) + ": " + pn(p) + " activated " + sysName(s) + ".", opts,
                                               kMonuments[card].text);
        curFor_ = -1;
        if (c >= 0) {
            G.systems[size_t(s)].tokens &= uint16_t(~(1u << c));
            monEvent(l, card, pn(l) + " removes " + pn(c) + "'s command token from " + sysName(s) + " (" + monName(card) + ").", s);
        } else {
            monEvent(l, card, pn(l) + " gains 1 command token (" + monName(card) + ").", s);
            gainTokens(l, 1, false, monName(card));
        }
    }
}

// ------------------------------------------------------------------ Diplomatic Enclave (Olradin)
void Rules::monAfterDiplomacy(int p) {
    if (!monumentsOn()) return;
    int s = -1, planet = -1;
    if (!monAbilityAt(G, p, "olradin", &s, &planet) || planet < 0 || !G.planets[size_t(planet)].exhausted) return;
    G.planets[size_t(planet)].exhausted = false;
    const int card = monumentIndex("olradin");
    monEvent(p, card, pn(p) + " readies " + planetName(planet) + " (" + monName(card) + ").", s);
}

// ------------------------------------------------------------------ Ado Citadel (Mirveda)
// true: `side`'s units make no combat rolls in this round of ground combat in sys (their opponent holds the Citadel there)
bool Rules::adoCitadel(int side, int other, int sys, int round) const {
    if (!monumentsOn() || round != 1 || other < 0 || other >= G.numPlayers || side == other) return false;
    int s = -1;
    return monAbilityAt(G, other, "mirveda", &s) && s == sys;
}

// ------------------------------------------------------------------ Perdition Array (Veldyr)
// of q's space cannon roll at the active system sys, the hits rolled by q's units in the Perdition Array's system (dice of each unit
// type in order: the first ones belong to the units there - identical units, so the split is the same as rolling them apart)
int Rules::perditionHits(int q, int sys, const RollResult& r) const {
    if (!monumentsOn() || q < 0 || q >= G.numPlayers || r.hits <= 0) return 0;
    int m = -1;
    if (!monAbilityAt(G, q, "veldyr", &m)) return 0;
    const bool here = m == sys;
    const std::vector<int16_t>& adj = G.systems[size_t(sys)].adj;
    if (!here && std::find(adj.begin(), adj.end(), int16_t(m)) == adj.end()) return 0;
    int mine[kUnitTypes] = {};  // dice of each unit type rolled by q's units in m
    auto count = [&](const Force& f, bool space) {
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!f.n[t]) continue;
            if (space && (UnitType(t) == UnitType::SpaceDock || (UnitType(t) == UnitType::PDS && !unitDef(G, q, UnitType::PDS).spaceOnly))) continue;
            const UnitDef& u = unitDef(G, q, UnitType(t));
            if (u.cannon <= 0 || (!here && !u.deepCannon)) continue;
            mine[t] += f.n[t] * std::max(1, int(u.cannonDice));
        }
    };
    count(G.systems[size_t(m)].space[size_t(q)], true);
    for (int16_t x : G.systems[size_t(m)].planets) count(G.planets[size_t(x)].units[size_t(q)], false);
    int seen[kUnitTypes] = {}, hits = 0;
    for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i) {
        const int t = r.unit[i];
        if (t < 0 || t >= kUnitTypes) continue;
        if (seen[t]++ < mine[t] && r.dice[i] >= r.need[i]) ++hits;
    }
    return std::min(hits, r.hits);
}

// ------------------------------------------------------------------ Twilight Throne (Edyn)
bool Rules::twilightThroneReady(int p, int* planet) const {
    if (!monumentsOn() || !monCardReady(G, p)) return false;
    int s = -1, x = -1;
    if (!monAbilityAt(G, p, "edyn", &s, &x) || x < 0 || G.planets[size_t(x)].destroyed || unitsForPlacement(G, p, UnitType::Mech) <= 0) return false;
    if (planet) *planet = x;
    return true;
}

void Rules::twilightThrone(int p) {
    int x = -1;
    if (!twilightThroneReady(p, &x)) return;
    const int card = monumentIndex("edyn");
    monExhaust(p, card);
    const int s = G.planets[size_t(x)].system;
    addUnits(p, UnitType::Mech, 1, s, x);
    event(EventKind::Produce, p, pn(p) + " produces 1 Mech on " + planetName(x) + " (" + monName(card) + ").", s);
}

// at the start of a ground combat on the Throne's planet: "At any time ... produce a mech on this planet"
void Rules::monGroundCombatStart(int att, int def, int planet) {
    if (!monumentsOn()) return;
    if (planet >= 0) rhodunCombatStart(att, def, G.planets[size_t(planet)].system);  // (0.82.4) Reliquat Unleashed: back to the Vault
    for (int side : {def, att}) {
        int x = -1;
        if (side < 0 || side >= G.numPlayers || !twilightThroneReady(side, &x) || x != planet) continue;
        const int card = monumentIndex("edyn");
        curFor_ = side == att ? def : att;
        curPlanet_ = planet;
        const bool go = mpick(side, card, kMonAskEdyn, monName(card) + ": produce a mech on " + planetName(planet) + " before the ground combat?",
                              {{1, "Produce a mech", "Exhaust the card; the mech fights in this combat."}, {0, "Not now", ""}},
                              kMonuments[card].text) == 1;
        curFor_ = -1;
        curPlanet_ = -1;
        if (go) twilightThrone(side);
    }
}

// ------------------------------------------------------------------ Queen's Rest (Kortali)
void Rules::monUnitsDestroyed(int p, UnitType t, int k, int sys) {
    if (!monumentsOn() || p < 0 || p >= G.numPlayers || k <= 0 || sys < 0) return;
    // (0.82.4) Kyro Reliquary: "When this unit is destroyed, you must place it on a planet you control in this or an adjacent system"
    if (t == UnitType::Monument && monCardHas(G, p, "kyro")) kyroPending_.push_back({p, sys});
    if (t == UnitType::Fighter || t == UnitType::Infantry || t == UnitType::Monument) return;  // (the Queen's Rest itself is gone)
    int s = -1;
    if (!monAbilityAt(G, p, "kortali", &s) || s != sys) return;
    for (int i = 0; i < k; ++i) queenPending_.push_back({p, sys, int(t)});
}

void Rules::queenResolve() {
    if (queenBusy_) return;
    queenBusy_ = true;
    const int card = monumentIndex("kortali");
    for (int guard = 0; guard < 64 && !queenPending_.empty(); ++guard) {
        const QueenRoll e = queenPending_.front();
        queenPending_.erase(queenPending_.begin());
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != e.p && !G.players[size_t(q)].eliminated && hasShips(G, q, e.sys))
                opts.push_back({q, pn(q), std::to_string(G.systems[size_t(e.sys)].space[size_t(q)].ships()) + " ships there", true, e.sys});
        if (hasShips(G, kNeutral, e.sys)) opts.push_back({kNeutral, "Neutral ships", "", true, e.sys});
        if (opts.empty()) continue;  // (no ships to hit: nothing to roll for)
        const UnitDef& u = unitDef(G, e.p, UnitType(e.t));
        const int need = u.combat > 0 ? u.combat : 8;
        RollResult r;
        r.dice.push_back(int8_t(E.rng().d10()));
        r.need.push_back(int8_t(need));
        r.unit.push_back(int8_t(-1));
        recount(r);
        reportRoll(e.p, e.sys, monName(card) + " (" + unitTypeName(UnitType(e.t)) + " lost)", r);
        if (r.hits <= 0) continue;
        const int t = opts.size() == 1 ? opts[0].id
                                       : mpick(e.p, card, kMonAskKortali, monName(card) + ": 1 hit against whose ships?", opts, kMonuments[card].text);
        monEvent(e.p, card, pn(e.p) + "'s " + monName(card) + " avenges the fallen: 1 hit against " +
                                (t == kNeutral ? std::string("the neutral ships") : pn(t) + "'s ships") + ".",
                 e.sys);
        assignHits(t, e.sys, -1, 1, shipMaskOf(), monName(card), e.p);
    }
    queenBusy_ = false;
}

}  // namespace ti
