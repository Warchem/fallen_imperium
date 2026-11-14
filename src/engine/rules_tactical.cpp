// The tactical action: activation, movement, space cannon offense, space
// combat, invasion and production.
#include <algorithm>
#include <memory>
#include <cstring>
#include <set>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
constexpr int kGroundMaskT = (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech));

std::string unitList(const Force& f, const Game* g = nullptr, int p = -1) {
    std::string s;
    for (int t = 0; t < kUnitTypes; ++t) {
        if (!f.n[t]) continue;
        if (!s.empty()) s += ", ";
        // (0.52.0: the Ghemina's flagships by name - "The Lady", "The Lord")
        if (g && isFlagship(UnitType(t)) && p >= 0 && p < g->numPlayers && g->players[size_t(p)].unit[int(UnitType::Flagship2)] >= 0)
            s += unitLabel(*g, p, UnitType(t));
        else
            s += std::to_string(f.n[t]) + " " + unitTypeName(UnitType(t), f.n[t] > 1);
    }
    return s;
}

bool anyShipCanReach(const Game& g, int p, int sys) {
    for (size_t s = 0; s < g.systems.size(); ++s) {
        if (int(s) == sys || (g.systems[s].hasToken(p) && !g.fx.dominusOrb && !(heroActive(g, p, "nomad") && flagshipIn(g, p, int(s))))) continue;
        const Force& f = g.systems[s].space[p];
        for (int t : kShipIdx)
            if (f.n[t] && shipReach(g, p, int(s), sys, UnitType(t), moveBonusMax(g, p)).ok) return true;
        // Floating factories move like ships
        if (f.count(UnitType::SpaceDock) && movesAsShip(g, p, UnitType::SpaceDock) &&
            shipReach(g, p, int(s), sys, UnitType::SpaceDock, moveBonusMax(g, p)).ok)
            return true;
    }
    return false;
}

bool enemyUnitsIn(const Game& g, int p, int sys) {
    const SystemState& s = g.systems[sys];
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p) continue;
        if (!s.space[q].empty()) return true;
        for (int16_t pl : s.planets)
            if (!g.planets[pl].units[q].empty()) return true;
    }
    return false;
}
}  // namespace

void Rules::tacticalAction(int p) {
    Player& pl = G.players[p];
    activated_ = false;
    std::vector<Option> opts;
    const bool zeu = zeu_;  // Z'eu Omega: no command token, not a home system
    zeu_ = false;
    const bool wtac = warfareTac_;  // Thunder's Edge Warfare: no command token, any system (even with our token)
    warfareTac_ = false;
    for (size_t s = 0; s < G.systems.size(); ++s)
        if ((canActivate(G, p, int(s)) || (wtac && G.systems[s].hasToken(p) && !G.systems[s].isHyperlane())) && !(zeu && G.systems[s].home >= 0))
            opts.push_back({int(s), sysName(int(s)), ""});
    if (opts.empty()) return;
    if (!zeu && !wtac) opts.push_back({-2, "Back", "Choose a different action."});
    int sys = pick(p, Tag::Activate, "Activate which system?", opts,
                   zeu ? "Z'eu: no command token is placed." : wtac ? "Warfare: no command token is placed." : "Costs 1 tactic token.", true);
    if (sys == -2) return;

    // Warn before a pointless activation (nothing can move in, produce or land).
    if (!anyShipCanReach(G, p, sys) && !hasProductionIn(G, p, sys) && G.systems[sys].space[p].groundForces() == 0) {
        int c = pick(p, Tag::ActivateConfirm, "None of your ships can reach " + sysName(sys) + ", and you can't produce there",
                     {{0, "Back", "Choose another system or action."},
                      {1, "Activate it anyway", "Spends a tactic token. The system stays activated until the status phase."}},
                     "Activating it would do nothing this turn.");
        if (c == 0) return;
    }

    // Players are reminded of what an activation costs them in deals (bots plan around it).
    if (pl.human) {
        std::string warn;
        for (const Promise& pr : G.promises)
            if (pr.state == kPromiseActive && pr.by == p && pr.kind == PromiseKind::AvoidSystem && pr.target == sys)
                warn += "You promised " + pn(pr.to) + " to stay out of " + sysName(sys) + ". ";
        for (int16_t n : pl.notesInPlay)
            if (noteType(n) == kNoteSupportThrone && hasUnits(G, noteOwner(n), sys))
                warn += pn(noteOwner(n)) + "'s Support for the Throne goes back (you lose 1 victory point). ";
        if (!warn.empty()) {
            int c = pick(p, Tag::ActivateConfirm, "Activate " + sysName(sys) + "?",
                         {{0, "Back", "Choose another system or action."}, {1, "Activate it anyway", warn}}, warn);
            if (c == 0) return;
        }
    }

    if (!artemirisToll(p, sys)) return;  // Keleres flagship: 2 influence
    if (!zeu && !wtac) pl.tactic = int8_t(pl.tactic - 1);
    ilNa_ = !wtac && G.systems[sys].hasToken(p);  // Il Na Viroset (Mahact commander): a system with your token
    if (!zeu && !wtac) G.systems[sys].tokens |= uint16_t(1u << p);
    G.activeSystem = int16_t(sys);
    if (!zeu && !wtac) zeuTE(p, sys);  // (fixes 27: Thunder's Edge's Z'eu - the token back, the system still activated)
    activated_ = true;
    lastActiveSystem_ = sys;
    awakened_.clear();
    G.fx = TacticalEffects();
    // Betray a Friend: whose promissory notes were held at the start of this tactical action
    notesHeld_ = 0;
    for (int16_t n : pl.notes)
        if (noteOwner(n) != p) notesHeld_ = uint8_t(notesHeld_ | (1u << noteOwner(n)));
    for (int16_t n : pl.notesInPlay) notesHeld_ = uint8_t(notesHeld_ | (1u << noteOwner(n)));
    {
        Event e;
        e.kind = EventKind::Activate;
        e.player = int8_t(p);
        e.system = int16_t(sys);
        e.hostile = enemyUnitsIn(G, p, sys);
        e.card = kActionTactical;
        e.text = pn(p) + " activates " + sysName(sys) + (e.hostile ? " - enemy forces are there!" : ".");
        E.emit(e);
    }
    supportForTheThroneCheck(p, sys);
    promiseActivate(p, sys);
    persActivate(p, sys);
    // (fixes 26, the Mahact review - owner 2026-11-12) Il Na Viroset: "If you do, return both command tokens to your
    // reinforcements and end your turn" - at once (the newer ruling: no "when / after you activate" or other abilities of
    // that turn once the second token is placed; end-of-turn abilities still happen). It used to come after the whole
    // tactical action (movement, combat, production)
    if (ilNa_ && G.fixes >= 26) {
        G.systems[size_t(sys)].tokens = uint16_t(G.systems[size_t(sys)].tokens & ~(1u << p));
        say(p, pn(p) + "'s command tokens in " + sysName(sys) + " return; the turn ends (Il Na Viroset).", sys);
        ilNa_ = false;
        turnEnded_ = true;
        if (G.modules & kModThundersEdge) malevolency(p);  // (the end of the action, as Minister of Peace)
        troAgent(p, sys);
        endTactical();
        return;
    }
    // (0.45.0, fixes 14 - the tester: Awaken looks at the sleepers there when the system is activated - not one a
    // Scanlink exploration puts there a moment later)
    activationSleepers_.clear();
    for (int16_t x : G.systems[size_t(sys)].planets)
        if (G.planets[size_t(x)].sleeper) activationSleepers_.push_back(x);
    scanlink(p, sys);
    // (fixes 26, the Empyrean review - owner 2026-11-12) Void Tether: "WHEN you activate a system" - before the "after you
    // activate" cards and abilities (it came with the other breakthroughs, after them)
    if (G.fixes >= 26 && breakthroughIs(G, p, "empyrean")) voidTether(p, sys);
    // (fixes 26, the Argent review - owner 2026-11-13) Wing Transfer: "When you activate a system" - the same
    if (G.fixes >= 26) wingTransfer(p, sys);
    {
        WinCtx w;
        w.w = Win::AfterActivate;
        w.active = p;
        w.sys = sys;
        window(w);
    }
    eyeOfVogul(p, sys);      // Uncharted Space relic (0.33.0)
    btActivated(p, sys);     // Imperator (Winnu breakthrough)
    if (G.modules & kModDiscordant) dsActivated(p, sys);  // Discordant Stars: Brood Pod, Minuteman, Titus Flavius, ...
    if (G.modules & kModThundersEdge) {  // Thunder's Edge factions (0.35.0)
        survivalInstinct(p, sys);  // Ral Nel
        eanautic(p, sys);          // Deepwrought mech
        incursion(p, sys);         // Crimson Rebellion: flip the breach
        nanoLink(p, sys);          // Nano-Link Permit
        myruVos(p, sys);           // Firmament agent: no space cannon against the moving ships
    }
    tempestaWindow(p, sys);  // Thunder's Edge: Ionian Fuel Refinery (0.34.0)
    if (G.fixes >= 9) eresSiphons(p, sys);  // (0.44.5: before Minister of Peace)
    if (G.fixes >= 10) neuroglaive(p, sys);  // (0.44.6: so is Neuroglaive)
    if (G.fixes >= 12) magenActivation(sys);  // (0.44.8: so is the Codex Magen Defense Grid - before Minister of Peace)
    // Minister of Peace: its owner may end the active player's turn.
    int peace = lawOwner(G, "minister_peace");
    if (peace >= 0 && peace != p && !G.players[peace].eliminated && peaceQuiet_[peace] != G.round) {
        bool others = false;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && hasUnits(G, q, sys)) others = true;
        bool use = false;
        if (others) {
            const std::string q = "Discard Minister of Peace to end " + pn(p) + "'s turn?", ctx = pn(p) + " activated " + sysName(sys) + ".";
            if (G.players[peace].human) {  // 0.28.0 (owner): the Law's Order choice too
                const int c = pick(peace, Tag::LawAbility, q,
                                   {{1, "Yes", ""}, {0, "No", ""}, {3, "No, and don't ask again this round", "You'll be asked again next round."}},
                                   ctx);
                if (c == 3) peaceQuiet_[peace] = int16_t(G.round);
                use = c == 1;
            } else {
                use = yesNo(peace, Tag::LawAbility, q, ctx);
            }
        }
        if (use) {
            for (size_t i = 0; i < G.laws.size(); ++i)
                if (!std::strcmp(kAgendas[G.laws[i].agenda].key, "minister_peace")) {
                    discardLaw(int(i), "used");
                    break;
                }
            say(p, pn(p) + "'s turn ends (Minister of Peace).", sys);
            turnEnded_ = true;
            if (G.fixes >= 25 && (G.modules & kModThundersEdge)) malevolency(p);  // (owner's faction debug pass: the action still ends)
            if (G.fixes >= 25) troAgent(p, sys);                                  // (the same: T'ro)
            endTactical();
            return;
        }
    }
    if (G.fixes < 12) magenActivation(sys);
    // E-Res Siphons, Nullification Field, Spatial Conduit Cylinder
    if (afterActivation(p, sys)) {
        if (G.fixes >= 25 && (G.modules & kModThundersEdge)) malevolency(p);  // (the same, fixes 25)
        if (G.fixes >= 25) troAgent(p, sys);
        endTactical();
        return;
    }
    if (ceasefire(p, sys)) G.fx.noMoveIn = true;

    const int avPlanet = avernusPlanet();  // Stellar Genesis (Muaat breakthrough): a war sun may take Avernus along
    const int avSys = avPlanet >= 0 ? G.planets[size_t(avPlanet)].system : -1;
    const int avWs = avSys >= 0 ? G.systems[size_t(avSys)].space[size_t(p)].count(UnitType::WarSun) : 0;
    int unitsBefore = 0;  // (Call of the Void: did units move in?)
    for (int t = 0; t < kUnitTypes; ++t) unitsBefore += G.systems[size_t(sys)].space[size_t(p)].n[t];
    const int shipsBefore = G.systems[size_t(sys)].space[size_t(p)].ships();  // (Sh'val: did ships move in?)
    avernusThrough_ = false;
    movement(p, sys);
    if (G.events) {
        int unitsAfter = 0;
        for (int t = 0; t < kUnitTypes; ++t) unitsAfter += G.systems[size_t(sys)].space[size_t(p)].n[t];
        voidMoved(p, sys, unitsAfter > unitsBefore);
    }
    if (avSys >= 0) avernusCarry(p, sys, avSys, avWs);
    if (G.modules & kModThundersEdge) sundered(p, sys);  // the Crimson Rebellion's home destroys intruders
    // Sh'val, Harbinger: straight to the landing ("after you move ships into the active system" - fixes 25, Sardakk
    // review 2026-11-11: only when ships of theirs moved in)
    const bool shval = (G.fixes < 25 || G.systems[size_t(sys)].space[size_t(p)].ships() > shipsBefore) && shvalCheck(p, sys);
    if (!shval) {
        spaceCannonOffense(p, sys);
        std::vector<int> shipOwners = playersWithShips(G, sys);
        if (hasShips(G, p, sys)) {
            for (int q : shipOwners)
                if (q != p && hasShips(G, p, sys) && hasShips(G, q, sys)) spaceCombat(p, q, sys);
            if (hasShips(G, p, sys) && neutralShipsIn(G, sys)) spaceCombat(p, kNeutral, sys);  // Thunder's Edge
        }
        checkFloatingFactories(sys);
        stationControl(sys);  // Thunder's Edge: the only ships in the system take its space stations
    }
    invasion(p, sys);
    coalescence(p, sys);  // Titans: awakened Hel-Titans fight on their planets
    shval_ = false;
    if (fleetDeferSys_ >= 0) {  // 0.58.5: the combat and the invasion are over - now the fleet pool
        fleetDeferSys_ = -1;
        fleetDirty_ = true;
        fleetStateCheck();
    }
    checkFloatingFactories(sys);
    if (hasProductionIn(G, p, sys)) production(p, sys, productionCapacity(G, p, sys), false);
    for (int q = 0; q < G.numPlayers; ++q) removeExcessCapacity(q, sys);
    stationControl(sys);
    wingTransferEnd(p, sys);  // Wing Transfer (Argent breakthrough)
    miniaturizationEnd(p);    // Ral Nel: structures carried in space go onto planets
    afterTactical(p, sys);
    if (G.modules & kModThundersEdge) malevolency(p);  // the Obsidian's note moves on
    endOfTacticalFactions(p, sys);  // T'ro
    endTactical();
    checkEliminations();
}

void Rules::endTactical() {
    if (fleetDeferSys_ >= 0) {  // (0.58.5: a turn that ended early - the check before the next question takes over)
        fleetDeferSys_ = -1;
        fleetDirty_ = true;
    }
    if (G.modules & kModDiscordant) dsEndTactical(G.activePlayer, G.activeSystem);  // Memory of Dusk, George Nobin
    if (G.modules & kModThundersEdge) coexistCleanup();  // coexisting units left alone take the planet
    bool adj = G.fx.lostStarChart || G.fx.starA >= 0;
    G.fx = TacticalEffects();
    if (adj) computeAdjacency(G);
    G.activeSystem = -1;
}

// (fixes 27, the Saar review - owner 2026-11-13) at the end of every turn: any floating factory that other players' ships reached
// in a way without its own check (Domna, Eye of Vogul, swaps...) - "If this unit is blockaded, it is destroyed"
void Rules::floatingSweep() {
    for (size_t s = 0; s < G.systems.size(); ++s) {
        bool any = false;
        for (int q = 0; q < G.numPlayers && !any; ++q) any = G.systems[s].space[size_t(q)].count(UnitType::SpaceDock) > 0;
        if (any) checkFloatingFactories(int(s));
    }
}

void Rules::checkFloatingFactories(int sys) {
    // LRR 79.4a: a Saar floating factory is destroyed when blockaded.
    for (int q = 0; q < G.numPlayers; ++q) {
        Force& f = G.systems[sys].space[q];
        if (!f.count(UnitType::SpaceDock) || hasShips(G, q, sys) || !otherShipsIn(G, q, sys)) continue;
        destroyUnits(q, UnitType::SpaceDock, f.count(UnitType::SpaceDock), sys, -1, -1, "blockaded");
    }
}

// ------------------------------------------------------------------ movement
void Rules::movement(int p, int sys) {
    if (G.fx.noMoveIn) {
        say(p, pn(p) + " can't move units into " + sysName(sys) + " (Ceasefire).", sys);
        return;
    }
    // Dominus Orb: purge it to move units out of systems with your command tokens.
    if (holdsRelic(p, "dominusorb")) {
        bool useful = false;
        for (size_t s = 0; s < G.systems.size() && !useful; ++s) {
            if (int(s) == sys || !G.systems[s].hasToken(p)) continue;
            for (int t : kShipIdx)
                if (G.systems[s].space[p].n[t] && shipReach(G, p, int(s), sys, UnitType(t), moveBonusMax(G, p)).ok) useful = true;
        }
        if (useful) {
            curRelic_ = findRelic("dominusorb");
            bool use = yesNo(p, Tag::RelicChoice, "Purge the Dominus Orb?",
                             "This movement, your units can also move out of systems that hold your command tokens.");
            curRelic_ = -1;
            if (use) {
                G.fx.dominusOrb = true;
                loseRelic(p, findRelic("dominusorb"), true);
            }
        }
    }
    if (!anyShipCanReach(G, p, sys)) {
        say(p, "No ships of " + pn(p) + " can reach " + sysName(sys) + " - no movement.", sys);
        return;
    }

    Decision d;
    d.player = p;
    d.kind = DecisionKind::Move;
    d.tag = Tag::Movement;
    d.prompt = "Move ships into " + sysName(sys);
    d.context = "Ships can't leave systems that hold your command tokens. They can pick up units on the way.";
    d.system = sys;
    const Game* gp = &G;
    // (0.43.0, fixes 2: a person may leave units without capacity behind - the move screen warns first)
    const bool strand = G.fixes >= 2 && G.players[p].human;
    d.validate = [gp, p, sys, strand](const Answer& a) { return checkMovePlan(*gp, p, sys, a.data, strand); };
    Answer a = E.ask(std::move(d));
    // Waypoints (0.56.0, owner): the routes the player chose - the units part is moved as before
    const std::map<int, std::vector<int>> routes = moveRoutes(a.data);
    if (!routes.empty()) a.data = moveUnitsOnly(a.data);
    if (a.data.empty()) {
        say(p, pn(p) + " moves nothing into " + sysName(sys) + ".", sys);
        return;
    }

    // (0.46.1: the fleet pool is checked when the move ends - not by the check before each question)
    struct Hold {
        int& n;
        explicit Hold(int& x) : n(x) { ++n; }
        ~Hold() { --n; }
    };
    auto hold = std::make_unique<Hold>(fleetHold_);
    Force moved;
    std::vector<int16_t> origins;
    // Ships that must roll for gravity rifts: (unit type, count, dice per ship).
    struct RiftShip {
        int type, count, rolls;
    };
    std::vector<RiftShip> riftShips;
    bool stormUsed = false;
    int wormholeCarriers = 0;  // ships with capacity that came through a wormhole (Sai Seravus)
    std::vector<std::pair<int, int>> weird;  // Weird Wormholes (galactic event): ships (type, count) that used a wormhole
    const int topMove = G.fx.mendosa ? maxShipMove(G) : 0;
    // Hil Colish (fixes 24, the card: "may move before or after your other ships"): when some ships get here only
    // through its delta wormhole in the active system, it moves first (its gravity rift roll too - lost there, its
    // wormhole goes with it and the ships that needed it stay where they are)
    const int colishO = colishOrigin(G, p);
    size_t colishIdx = a.data.size();
    bool colishFirst = false;
    if (colishO >= 0 && colishO != sys) {
        for (size_t i = 0; i + 3 < a.data.size(); i += 4)
            if (a.data[i] == colishO && a.data[i + 1] < 0 && a.data[i + 2] == int(UnitType::Flagship) && a.data[i + 3] > 0) colishIdx = i;
        if (colishIdx < a.data.size())
            for (size_t i = 0; i + 3 < a.data.size() && !colishFirst; i += 4) {
                if (i == colishIdx || a.data[i + 1] >= 0 || !movesAsShip(G, p, UnitType(a.data[i + 2]))) continue;
                const auto rt = routes.find(a.data[i]);
                const Reach rr = rt == routes.end() ? shipReach(G, p, a.data[i], sys, UnitType(a.data[i + 2]), moveBonusMax(G, p))
                                                    : shipReachVia(G, p, a.data[i], rt->second, sys, UnitType(a.data[i + 2]), moveBonusMax(G, p));
                if (rr.ok && rr.colish && UnitType(a.data[i + 2]) != UnitType::Fighter) colishFirst = true;
            }
    }
    std::vector<size_t> order;
    if (colishFirst) order.push_back(colishIdx);
    for (size_t i = 0; i + 3 < a.data.size(); i += 4)
        if (!(colishFirst && i == colishIdx)) order.push_back(i);
    std::set<int> stayFrom;  // origins whose ships can't go after all (Hil Colish lost on the way)
    for (size_t oi = 0; oi < order.size(); ++oi) {
        const size_t i = order[oi];
        int from = a.data[i], loc = a.data[i + 1], t = a.data[i + 2], k = a.data[i + 3];
        if (stayFrom.count(from)) continue;
        UnitType ut = UnitType(t);
        Reach r;
        if (movesAsShip(G, p, ut) && loc < 0) {
            const auto rt = routes.find(from);
            const std::vector<int> none;
            const std::vector<int>& via = rt == routes.end() ? none : rt->second;  // (empty: the automatic route)
            // (fixes 26, the Muaat review - owner 2026-11-12) Stellar Genesis: "out of or THROUGH Avernus's system" - the
            // chosen route goes through it, or (an automatic route) one through it was open to that war sun
            if (ut == UnitType::WarSun && G.fixes >= 26 && breakthroughIs(G, p, "muaat") && avernusPlanet() >= 0) {
                const int avs = G.planets[size_t(avernusPlanet())].system;
                if (avs >= 0 && avs != from && avs != sys &&
                    (std::find(via.begin(), via.end(), avs) != via.end() ||
                     (via.empty() && shipReachVia(G, p, from, std::vector<int>{avs}, sys, ut, moveBonusMax(G, p)).ok)))
                    avernusThrough_ = true;
            }
            r = shipReachVia(G, p, from, via, sys, ut, moveBonusMax(G, p));
            if (!r.ok && topMove > 0 && ut != UnitType::Fighter)  // Captain Mendosa
                r = shipReachVia(G, p, from, via, sys, ut, std::max(0, topMove - moveValue(G, p, from, ut)) + moveBonusMax(G, p));
            if (r.ok && r.viaWormhole && unitDef(G, p, ut).capacity > 0) wormholeCarriers += k;
            if (r.ok && r.viaWormhole && eventOn(G, kEvWormholes)) weird.push_back({t, k});  // Weird Wormholes
        }
        Force& src = loc >= 0 ? G.planets[loc].units[p] : G.systems[from].space[p];
        int undamaged = src.n[t] - src.dmg[t];
        int movedDamaged = std::max(0, k - undamaged);
        const int movedGalv = std::min<int>(k, src.galv[t]);  // (Last Bastion: galvanized units move first)
        src.galv[t] = uint8_t(src.galv[t] - movedGalv);
        src.remove(ut, k, false);
        Force& dst = G.systems[sys].space[p];
        dst.add(ut, k);
        dst.dmg[t] = uint8_t(dst.dmg[t] + movedDamaged);
        dst.galv[t] = uint8_t(dst.galv[t] + movedGalv);
        moved.add(ut, k);
        if (std::find(origins.begin(), origins.end(), from) == origins.end()) origins.push_back(int16_t(from));
        // Transported units don't roll (LRR 41.2a); carried fighters have r.ok == false.
        const bool colishNow = colishFirst && i == colishIdx;
        if (r.ok && r.viaRift && !colishNow) riftShips.push_back({t, k, r.riftRolls});
        if (r.ok && r.viaStorm) stormUsed = true;
        if (colishNow) {
            say(p, pn(p) + "'s Hil Colish moves first: its delta wormhole opens in " + sysName(sys) + ".", sys);
            if (r.ok && r.viaRift && !G.fx.crucible) {  // its own roll, now
                RollResult res;
                bool lost = false;
                for (int roll = 0; roll < r.riftRolls && !lost; ++roll) {
                    const int v = E.rng().d10();
                    res.dice.push_back(int8_t(v));
                    res.need.push_back(4);
                    if (v <= 3) lost = true;
                }
                reportRoll(p, sys, "Gravity rift (Hil Colish, 1-3 is lost)", res);
                if (lost) {
                    const bool was = removing_;
                    removing_ = true;
                    destroyUnits(p, UnitType::Flagship, 1, sys, -1, -1, "gravity rift");
                    removing_ = was;
                }
            }
            computeAdjacency(G);  // (its delta wormhole is in the active system now - or gone with it)
            // the other ships that can't get here now stay where they are, with what they would have carried
            for (size_t oj = oi + 1; oj < order.size(); ++oj) {
                const size_t j = order[oj];
                if (a.data[j + 1] >= 0 || !movesAsShip(G, p, UnitType(a.data[j + 2])) || UnitType(a.data[j + 2]) == UnitType::Fighter) continue;
                const auto rt = routes.find(a.data[j]);
                const Reach now = rt == routes.end() ? shipReach(G, p, a.data[j], sys, UnitType(a.data[j + 2]), moveBonusMax(G, p))
                                                     : shipReachVia(G, p, a.data[j], rt->second, sys, UnitType(a.data[j + 2]), moveBonusMax(G, p));
                if (!now.ok && !stayFrom.count(a.data[j])) {
                    stayFrom.insert(a.data[j]);
                    say(p, "Without Hil Colish's wormhole, the ships in " + sysName(a.data[j]) + " can't reach " + sysName(sys) + " - they stay.", sys);
                }
            }
        }
    }
    {
        Event e;
        e.kind = EventKind::Move;
        e.player = int8_t(p);
        e.system = int16_t(sys);
        e.from = origins;
        e.text = pn(p) + " moves " + unitList(moved, &G, p) + " into " + sysName(sys) + ".";
        E.emit(e);
    }
    for (const auto& rt : routes) {  // (only games where someone used Waypoint Mode have these lines)
        std::string via;
        for (int st : rt.second) via += (via.empty() ? "" : ", ") + sysName(st);
        say(p, "The ships from " + sysName(rt.first) + " took the route through " + via + ".", sys);
    }
    persMoveNear(p, sys);
    if (G.fx.crucible) riftShips.clear();  // Crucible: no gravity rift rolls this movement
    for (const RiftShip& rs : riftShips) {
        RollResult res;
        int lost = 0;
        for (int i = 0; i < rs.count; ++i) {
            for (int roll = 0; roll < rs.rolls; ++roll) {
                int v = E.rng().d10();
                res.dice.push_back(int8_t(v));
                res.need.push_back(4);
                if (v <= 3) {
                    ++lost;
                    break;
                }
            }
        }
        reportRoll(p, sys, std::string("Gravity rift (") + unitTypeName(UnitType(rs.type), true) + ", 1-3 is lost)", res);
        // (fixes 24, owner's faction debug pass: LRR 41.2 - a ship lost to a gravity rift is REMOVED from the board, not
        // destroyed: nothing that answers to a destroyed unit fires - Subatomic Splicer, Van Hauge, Devour / The Terror
        // Between, Age of Fighters, Crash Landing, the Bastion's galvanized units...)
        if (lost) {
            const bool was = removing_;
            if (G.fixes >= 24) removing_ = true;
            destroyUnits(p, UnitType(rs.type), lost, sys, -1, -1, "gravity rift");
            removing_ = was;
        }
    }
    if (!weird.empty()) weirdWormholes(p, sys, weird);
    // 0.58.5 (fixes 22 only): ships beyond the fleet pool that moved in stayed for a combat here and the invasion after
    // it. 0.58.51 (fixes 23, owner: "resume checks on the fleet pool even before combat ... (LRR 37.3)"): the extra
    // always go right after the move (removeExcessCapacity just below) - the move itself stays allowed.
    if (G.fixes == 22 && combatAhead(G, p, sys)) fleetDeferSys_ = sys;
    hold.reset();
    removeExcessCapacity(p, sys);
    // 0.43.0: what a person left behind without capacity (they were warned) - 0.45.0 (fixes 14): anyone's, at once
    if (G.fixes >= 2 && (G.players[p].human || G.fixes >= 14))
        for (int16_t o : origins)
            if (o != sys) removeExcessCapacity(p, o);
    turnSystemsMoved_++;
    // Ion Storm: flips at the end of the move if any ship's route used its
    // wormhole - into, out of, or through the storm system (owner, RULES-CHECK 0.5.0 #2).
    if (stormUsed) ionStormUsed(G.ionStorm);
    checkNexus(sys);
    // Maximus (Dih-Mohn flagship, 0.43.0 D64): its production comes right after the move, before anything else
    if ((G.modules & kModDiscordant) && !moved.empty() && G.fixes >= 2) ds3AfterMovement(p, sys, moved.count(UnitType::Flagship) > 0);
    if (hasShips(G, p, sys)) {
        WinCtx w;
        w.w = Win::AfterMove;
        w.active = p;
        w.sys = sys;
        window(w);
    }
    afterMoveFactions(p, sys);  // Stymie, Foresight
    afterMove3(p, sys, wormholeCarriers, moved.count(UnitType::WarSun) > 0);  // Sai Seravus, Ba'al, Acamar, Xuange, Voidwatch
    if ((G.modules & kModDiscordant) && !moved.empty()) dsAfterMovement(p, sys, moved.count(UnitType::Destroyer) > 0);  // Cataclysm, Daedalon
    if ((G.modules & kModDiscordant) && !moved.empty() && G.fixes < 2) ds3AfterMovement(p, sys, moved.count(UnitType::Flagship) > 0);  // Maximus
    if ((G.modules & kModDiscordant) && !moved.empty()) ds4AfterMovement(p, sys);  // Shroud of Lith
    if ((G.modules & kModDiscordant) && !moved.empty()) ds5AfterMovement(p, sys);  // the Lady moves, Sal Sparrow
    if ((G.modules & kModDiscordant) && !moved.empty()) ds7AfterMovement(p, sys);  // the Ghoti home moves
    capturedBlockade();  // LRR 17.2c
}

// ------------------------------------------------------------------ space cannon offense
// Graviton Laser System (0.44.5): asked before a space cannon shot at the target's ships (the AI sees the target and
// the system)
bool Rules::askGraviton(int q, int target, int sys) {
    if (q < 0 || q >= G.numPlayers || !techReady(q, "gls")) return false;
    const int prevFor = curFor_, prevValue = curValue_;
    if (G.fixes >= 9) {
        curFor_ = target;
        curValue_ = sys;
    }
    const bool yes = yesNo(q, Tag::UseTech, "Exhaust Graviton Laser System?",
                           "Hits from this shot must be assigned to non-fighter ships if possible.");
    curFor_ = prevFor;
    curValue_ = prevValue;
    if (yes) exhaustTech(q, "gls");
    return yes;
}

// space cannon hits on ships; with Graviton Laser System the non-fighter ships take what they can first, the rest
// still goes to the fighters ("if able" - 0.44.5)
void Rules::cannonHits(int target, int sys, int hits, int mask, bool gls, const std::string& why, int q) {
    const int noFighters = mask & ~(1 << int(UnitType::Fighter));
    if (gls && G.systems[size_t(sys)].space[size_t(target)].nonFighterShips() > 0) {
        const int first = std::min(hits, maxAbsorbableHits(G, target, sys, -1, noFighters));
        if (first > 0) assignHits(target, sys, -1, first, noFighters, why, q);
        hits -= first;
    }
    if (hits > 0) assignHits(target, sys, -1, hits, mask, why, q);
}

// Antimass Deflectors: -1 on other players' space cannon rolls against the owner's units (0.44.5: every source)
int Rules::antimass(int q, int target) const {
    return target >= 0 && target < G.numPlayers && target != q && owns(target, "amd") ? 1 : 0;
}

// q's space cannon in the space cannon offense step of p's tactical action in sys (empty: q can't fire). Shared by the
// step itself and Myru Vos's "would it matter" check (fixes 25).
CannonGuns Rules::cannonGuns(int q, int p, int sys) const {
    CannonGuns out;
    const SystemState& s = G.systems[sys];
    if (scarAt(G, sys) && !ignoresScars(G, q)) return out;  // (0.44.4: the Nivyn ignore it)
    {
        // q's space cannon units: in the system, or deep-space cannon next door
        Force& guns = out.guns;
        RollMods& galvDice = out.galv;  // Galvanize (Thunder's Edge): galvanized units roll 1 more die for unit abilities
        auto collect = [&](const Force& f, bool adjacentOnly) {
            for (int t = 0; t < kUnitTypes; ++t) {
                if (!f.n[t]) continue;
                const UnitDef& u = unitDef(G, q, UnitType(t));
                if (u.cannon <= 0) continue;
                // Assault Machina (Mirveda commander, Discordant Stars): their space cannon reaches the next systems too
                const bool machina = (G.modules & kModDiscordant) && q < G.numPlayers && commanderUsable(G, q, "mirveda");
                if (adjacentOnly && !u.deepCannon && !machina) continue;
                guns.add(UnitType(t), f.n[t]);
                galvDice.extraDice[t] += std::min(f.galv[t], f.n[t]);
            }
        };
        // structures carried in a space area (Ral Nel, Miniaturization) do nothing - but each Linkship there may fire
        // one of their space cannons (Linkship II: all of them the same one)
        auto collectSpace = [&](const Force& f, bool adjacentOnly) {
            Force ships = f;
            ships.n[int(UnitType::SpaceDock)] = 0;
            if (q >= G.numPlayers || !unitDef(G, q, UnitType::PDS).spaceOnly) ships.n[int(UnitType::PDS)] = 0;  // (Gauss Cannons fire from space)
            collect(ships, adjacentOnly);
            const int pds = f.n[int(UnitType::PDS)], dd = f.n[int(UnitType::Destroyer)];
            if (!pds || !dd || q >= G.numPlayers || !fac(q, "ralnel")) return;
            const UnitDef& u = unitDef(G, q, UnitType::PDS);
            if (u.cannon <= 0 || (adjacentOnly && !u.deepCannon)) return;
            guns.add(UnitType::PDS, owns(q, "linkship2") ? dd : std::min(dd, pds));
        };
        if (!quietusAt(G, q, sys)) {  // (the Quietus: no unit abilities)
            collectSpace(s.space[q], false);
            for (int16_t pl : s.planets) collect(G.planets[pl].units[q], false);
        }
        std::vector<int16_t> near(s.adj.begin(), s.adj.end());
        if (G.fixes >= 24 && qeWormholes(G, sys) && q < G.numPlayers && isFaction(G, q, "ghost"))  // Quantum Entanglement
            for (size_t a = 0; a < G.systems.size(); ++a)
                if (qeLinked(G, q, sys, int(a)) && std::find(near.begin(), near.end(), int16_t(a)) == near.end()) near.push_back(int16_t(a));
        if (G.fixes >= 24 && G.fx.taivra && sys == G.activeSystem)  // Emissary Taivra: every system with a wormhole is adjacent
            for (size_t a = 0; a < G.systems.size(); ++a)
                if (int(a) != sys && wormholesOf(G, int(a)) && std::find(near.begin(), near.end(), int16_t(a)) == near.end()) near.push_back(int16_t(a));
        for (int16_t a : near) {
            if ((scarAt(G, a) && !ignoresScars(G, q)) || quietusAt(G, q, a)) continue;  // (nor from inside one)
            // (fixes 26, the Empyrean review - owner 2026-11-12) Void Tether: other players don't treat a tethered border's
            // systems as adjacent - no deep space cannon across it unless the Empyrean allow it
            if (G.fixes >= 26 && q < G.numPlayers && tethered(G, q, sys, a)) continue;
            collectSpace(G.systems[a].space[q], true);
            for (int16_t pl : G.systems[a].planets) collect(G.planets[pl].units[q], true);
        }
        out.ul = ulCannonDice(q, sys);  // Ul the Progenitor on Elysium
        const bool dsOn = (G.modules & kModDiscordant) != 0;
        if (dsOn) dsCannonLimit(q, p, guns);                       // Starfall Gunnery: one unit when it's not their action
        out.starfall = dsOn ? dsCannonDice(q, p, sys) : 0;  // Starfall Gunnery: their ships' Space Cannon 8
        // Dhonraz Installations (Gledge breakthrough): their stations here have Space Cannon 5
        if (dsOn && q < G.numPlayers && breakthroughIs(G, q, "gledge"))
            for (int16_t pl : s.planets)
                if (G.planets[size_t(pl)].owner == q && stationFor(G, q, pl)) ++out.dhonraz;
    }
    return out;
}

void Rules::spaceCannonOffense(int p, int sys) {
    const SystemState& s = G.systems[sys];
    if (scarAt(G, sys) && G.fixes < 8) return;  // Thunder's Edge: no space cannon against ships inside an entropic scar
    for (int q : clockwiseFrom(G, p)) {
        if (G.fixes >= 26 && q < G.numPlayers) tetherCannonAllow(q, p, sys);  // (the Empyrean review) "... unless you allow it"
        CannonGuns cg = cannonGuns(q, p, sys);
        if (cg.empty()) continue;
        Force& guns = cg.guns;
        RollMods& galvDice = cg.galv;
        const int ul = cg.ul, starfall = cg.starfall, dhonraz = cg.dhonraz;
        const bool dsOn = (G.modules & kModDiscordant) != 0;
        // targets (LRR 77.5): others must shoot the active player; the active player picks a target.
        // Quetzecoatl (Argent flagship): no space cannon against the Argent ships in its system
        int target = -1;
        if (q == p) {
            std::vector<Option> opts;
            for (int r : playersWithShips(G, sys))
                if (r != p && !(flagshipAbility(G, r, "argent") && flagshipIn(G, r, sys)))
                    opts.push_back({r, pn(r), std::to_string(G.systems[sys].space[r].ships()) + " ships"});
            if (opts.empty()) continue;
            target = opts.size() == 1 ? opts[0].id
                                      : pick(p, Tag::CannonTarget, "Fire space cannon at whose ships?", opts);
        } else if (hasShips(G, p, sys) && !(flagshipAbility(G, p, "argent") && flagshipIn(G, p, sys))) {
            target = p;
        }
        if (target < 0) continue;
        if (target == p && q != p && G.fx.solarFlare) continue;  // Solar Flare
        if (!yesNo(q, Tag::SpaceCannonFire, "Fire space cannon at " + pn(target) + "'s ships in " + sysName(sys) + "?",
                   "Space cannon offense."))
            continue;
        const bool gls = askGraviton(q, target, sys);
        int mod = owns(target, "amd") ? -1 : 0;
        mod += classifiedWeapons(q, sys, guns.total());  // Uncharted Space (0.33.0)
        mod += assail(q, target);                         // Assail (Thunder's Edge)
        if (dsOn) mod += dsAbilityMod(q);                 // Issac of Sinci, Omniscience Field
        RollResult r = rollUnits(q, guns, RollKind::Cannon, mod, abilityDice(q, sys), 0, &galvDice);
        if (ul) addDice(r, ul, 5 - mod);
        if (starfall) addDice(r, starfall, 8 - mod);
        if (dhonraz) addDice(r, dhonraz, 5 - mod);
        reportRoll(q, sys, "Space cannon at " + pn(target), r);
        afterUnitRoll(q, target, sys, r, "space cannon");
        // Halberd (Kolume flagship): their space cannon hits here can't be cancelled; the Rook's hits only on fighters
        // Aurora Research Base (Veldyr breakthrough, 0.43.0): their ability hits in a nebula can't be cancelled either
        const bool halberd = (dsOn && q < G.numPlayers && flagshipAbility(G, q, "kolume") && flagshipIn(G, q, sys)) ||
                             (dsOn && q < G.numPlayers && dsbtOn(q, "veldyr") && (anomaliesFor(G, q, sys) & kNebula));
        int rookHits = 0;
        if (dsOn && q < G.numPlayers && fac(q, "kolume") && mechAbilities(G))
            for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i)
                if (r.unit[i] == int(UnitType::Mech) && r.dice[i] >= r.need[i]) ++rookHits;
        if (rookHits > 0) {
            rookHits = std::min(rookHits, r.hits);
            r.hits -= rookHits;
            const int ff = std::min(rookHits, G.systems[sys].space[target].count(UnitType::Fighter));
            if (ff > 0) destroyUnits(target, UnitType::Fighter, ff, sys, -1, q, "the Rook's space cannon");
        }
        if (r.hits > 0 && !classified_ && !halberd) {
            WinCtx w;
            w.w = Win::BeforeCannonHits;
            w.a = target;
            w.b = q;
            w.sys = sys;
            w.value = r.hits;
            if (cardHas(window(w), "mjets")) {
                const int c = eventOn(G, kEvWild) ? r.hits : 1;  // Wild, Wild Galaxy: every hit
                r.hits -= c;
                say(target, pn(target) + " cancels " + std::to_string(c) + " hit" + (c > 1 ? "s" : "") + ".", sys);
            }
        }
        if (r.hits <= 0) continue;
        int nonFightersBefore = G.systems[sys].space[target].nonFighterShips();
        int mask = 0;
        for (int t : kShipIdx) mask |= 1 << t;
        if (gls && nonFightersBefore > 0) mask &= ~(1 << int(UnitType::Fighter));
        // Eclipse (Vaylerian mech, Discordant Stars): during their tactical action one of their ships here is out of reach
        if ((G.modules & kModDiscordant) && target == p && fac(p, "vaylerian") && mechAbilities(G)) {
            bool mech = G.systems[sys].space[p].count(UnitType::Mech) > 0;
            for (int16_t x : G.systems[sys].planets) mech = mech || G.planets[x].units[p].count(UnitType::Mech) > 0;
            if (mech) {
                const Force& f = G.systems[sys].space[p];
                int best = 0;  // the hits the safe ship could have taken
                for (int t : kShipIdx)
                    if (f.n[t] && ((mask >> t) & 1)) best = std::max(best, 1 + (unitSustains(G, p, UnitType(t)) && f.dmg[t] < f.n[t] ? 1 : 0));
                const int room = std::max(0, maxAbsorbableHits(G, p, sys, -1, G.fixes >= 9 ? (mask | 1 << int(UnitType::Fighter)) : mask) - best);
                if (r.hits > room) {
                    say(p, pn(p) + "'s Eclipse shields a ship: " + std::to_string(r.hits - room) + " space cannon hit" + (r.hits - room > 1 ? "s are" : " is") + " lost.", sys);
                    r.hits = room;
                }
                if (r.hits <= 0) continue;
            }
        }
        if (G.fixes >= 9) cannonHits(target, sys, r.hits, mask | 1 << int(UnitType::Fighter), gls, "space cannon", q);
        else assignHits(target, sys, -1, r.hits, mask, "space cannon", q);
        if (nonFightersBefore > 0 && G.systems[sys].space[target].nonFighterShips() == 0) actionSecret(q, "ttfd");
    }
    // Thunder's Edge (0.34.0): neutral PDS in the system always fire at the active player's ships
    {
        Force guns;
        for (int16_t pl : s.planets)
            for (int t = 0; t < kUnitTypes; ++t)
                if (G.planets[pl].units[kNeutral].n[t] && unitDef(G, kNeutral, UnitType(t)).cannon > 0)
                    guns.add(UnitType(t), G.planets[pl].units[kNeutral].n[t]);
        for (int t = 0; t < kUnitTypes; ++t)
            if (s.space[kNeutral].n[t] && unitDef(G, kNeutral, UnitType(t)).cannon > 0) guns.add(UnitType(t), s.space[kNeutral].n[t]);
        if (!guns.empty() && hasShips(G, p, sys) && !(flagshipAbility(G, p, "argent") && flagshipIn(G, p, sys)) && !G.fx.solarFlare) {
            RollResult r = rollUnits(kNeutral, guns, RollKind::Cannon, owns(p, "amd") ? -1 : 0, 0);
            reportRoll(kNeutral, sys, "Space cannon at " + pn(p), r);
            afterUnitRoll(kNeutral, p, sys, r, "space cannon");
            if (r.hits > 0) {
                WinCtx w;
                w.w = Win::BeforeCannonHits;
                w.a = p;
                w.b = kNeutral;
                w.sys = sys;
                w.value = r.hits;
                if (cardHas(window(w), "mjets")) {
                    const int c = eventOn(G, kEvWild) ? r.hits : 1;  // Wild, Wild Galaxy: every hit
                    r.hits -= c;
                    say(p, pn(p) + " cancels " + std::to_string(c) + " hit" + (c > 1 ? "s" : "") + ".", sys);
                }
            }
            int mask = 0;
            for (int t : kShipIdx) mask |= 1 << t;
            if (r.hits > 0) assignHits(p, sys, -1, r.hits, mask, "space cannon", kNeutral);
        }
    }
    for (int q = 0; q < G.numPlayers; ++q) removeExcessCapacity(q, sys);
}

// ------------------------------------------------------------------ invasion
void Rules::bombardment(int p, int sys) {
    SystemState& s = G.systems[sys];
    if ((scarAt(G, sys) && !ignoresScars(G, p)) || quietusAt(G, p, sys)) return;  // Thunder's Edge: no bombardment inside an entropic scar (the Quietus)
    int typeMask = 0;
    bool warSun = commanderUsable(G, p, "l1z1x");  // 2RAM: planetary shields don't stop the bombardment
    for (int t = 0; t < kUnitTypes; ++t) {
        if (!s.space[p].n[t]) continue;
        const UnitDef& u = unitDef(G, p, UnitType(t));
        if (u.bombard > 0 || dsBombardDiceOf(G, p, sys, UnitType(t)) > 0) typeMask |= 1 << t;  // (the Lady, Raid Leaders)
        else if (blitz_ && isShip(UnitType(t)) && UnitType(t) != UnitType::Fighter) typeMask |= 1 << t;  // Blitz
        if (u.ignoresShield) warSun = true;
    }
    // Annihilator (L1Z1X mech): mechs on planets here bombard as if they were ships
    bool annihilators = false;
    if (bombardAvailable(G, p, sys, UnitType::Mech) > s.space[p].count(UnitType::Mech)) {
        typeMask |= 1 << int(UnitType::Mech);
        annihilators = true;
        // (fixes 26, the L1Z1X review) said only when there is something to bombard (it was printed 1253 of 1275 times
        // with no bombardment after it)
        if (G.fixes < 26) say(p, pn(p) + "'s mechs on planets here join the bombardment (Annihilator).", sys);
    }
    if (!typeMask) return;
    // Planets with other players' ground forces and no planetary shield (LRR 15.1f, 65.3).
    const bool conventions = lawInPlay(G, "conventions");
    std::vector<int> targets;
    for (int16_t pl : s.planets) {
        if (conventions && hasTraitOf(G, pl, Trait::Cultural)) continue;  // Conventions of War
        if (arcaneShield(G, pl)) continue;  // Arcane Shield Emitter (Uncharted Space, 0.43.0): it can't be bombarded
        for (int q = 0; q < kSides; ++q) {  // (kNeutral: Thunder's Edge's neutral units)
            if (q >= G.numPlayers && q != kNeutral) continue;
            if (q == p || groundForcesOn(G, q, pl) == 0) continue;
            bool shield = false;
            for (int t = 0; t < kUnitTypes; ++t)
                if (G.planets[pl].units[q].n[t] && unitDef(G, q, UnitType(t)).shield) {
                    if (UnitType(t) == UnitType::PDS && disabled_) continue;  // Disable
                    shield = true;
                }
            if ((G.fx.echoShield >> q) & 1) shield = true;  // Echo Shielding (Uncharted Space)
            // Orbital Defense Grid (Discordant Stars): 0.44.8 (fixes 12) - a Planetary Shield like any other, so Arc
            // Secundus, Combinatorial Bypass, scars still take it away
            if (G.fixes >= 12 && G.fx.dsShieldPlanet == pl) shield = true;
            if (shieldsDown(G, q, sys)) shield = false;  // Arc Secundus
            if (G.fx.bypass == p && q != p) shield = false;  // Combinatorial Bypass (Discordant Stars)
            if (G.fixes < 12 && G.fx.dsShieldPlanet == pl) shield = true;    // Orbital Defense Grid (Discordant Stars)
            // Justicar (Kortali mech, Discordant Stars): its Planetary Shield can't be taken away
            const bool justicar = q < G.numPlayers && (G.modules & kModDiscordant) && fac(q, "kortali") && mechAbilities(G) &&
                                  G.planets[pl].units[q].count(UnitType::Mech) > 0;
            if (justicar) continue;
            if (shield && !warSun) continue;
            targets.push_back(pl);
            break;
        }
    }
    if (targets.empty()) return;
    if (annihilators && G.fixes >= 26) say(p, pn(p) + "'s mechs on planets here join the bombardment (Annihilator).", sys);
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Land;
    d.tag = Tag::BombardPlan;
    d.prompt = "Bombard planets in " + sysName(sys) + "?";
    d.context = "Choose which planet each of your bombarding units fires at. Leave all at 0 to skip.";
    d.system = sys;
    d.hitMask = typeMask;
    d.planets = targets;
    LandSpec spec;
    spec.typeMask = typeMask;
    spec.targets = targets;
    spec.bombard = true;
    const Game* gp = &G;
    d.validate = [gp, p, sys, spec](const Answer& a) { return checkLandPlan(*gp, p, sys, a.data, spec); };
    Answer a = E.ask(std::move(d));
    auto defenderOn = [&](int pl) {
        int q = -1;
        for (int x = 0; x < G.numPlayers; ++x)
            if (x != p && groundForcesOn(G, x, pl) > 0) q = x;
        if (q < 0 && neutralGroundOn(G, pl) > 0) q = kNeutral;
        return q;
    };
    // (0.44.8, fixes 12) all of a player's bombardment is one roll (LRR 15.1): Plasma Scoring, Trrakan Aun Zulok,
    // Starfall Array and Strike Wing Ambuscade add their dice once - to one unit, at the planet with the most
    // defending ground forces - not once per planet bombarded
    int extraPlanet = -1, extraOnce = 0;
    if (G.fixes >= 12) {
        int most = 0;
        std::vector<Option> where;  // (0.44.9, fixes 13 - the owner's answer W5: the player picks the planet)
        for (int pl : targets) {
            bool shooter = false;
            for (size_t i = 0; i < a.data.size(); i += 3)
                if (a.data[i] == pl && a.data[i + 2] > 0 && unitDef(G, p, UnitType(a.data[i + 1])).bombard > 0) shooter = true;
            const int q = defenderOn(pl);
            const int n = q < 0 ? 0 : q == kNeutral ? neutralGroundOn(G, pl) : groundForcesOn(G, q, pl);
            if (shooter && n > most) most = n, extraPlanet = pl;
            if (shooter && n > 0) where.push_back({pl, planetName(pl), std::to_string(n) + " ground force" + (n > 1 ? "s" : "") + " there"});
        }
        if (extraPlanet >= 0) extraOnce = abilityDice(p, sys);
        if (G.fixes >= 13 && extraOnce > 0 && where.size() > 1)
            extraPlanet = pick(p, Tag::UseTech, "Your extra bombardment " + std::string(extraOnce > 1 ? "dice" : "die") + " (Plasma Scoring and the like): at which planet?",
                               where, "All of your bombardment is one roll: the extra dice go with one unit, at one planet.");
    }
    for (int pl : targets) {
        Force shooters, blitzers;
        for (size_t i = 0; i < a.data.size(); i += 3)
            if (a.data[i] == pl) {
                UnitType t = UnitType(a.data[i + 1]);
                if (unitDef(G, p, t).bombard > 0 || dsBombardDiceOf(G, p, sys, t) > 0) shooters.add(t, a.data[i + 2]);
                else blitzers.add(t, a.data[i + 2]);
            }
        if (shooters.empty() && blitzers.empty()) continue;
        const int q = defenderOn(pl);
        if (q < 0) continue;
        int mod = G.planets[pl].owner == bunkerOwner_ && bunkerOwner_ >= 0 ? -4 : 0;  // Bunker
        mod += classifiedWeapons(p, sys, shooters.total());                       // Classified Weapons (Uncharted Space)
        if (q < G.numPlayers) mod += assail(p, q);                                // Assail (Thunder's Edge)
        if (G.modules & kModDiscordant) mod += dsAbilityMod(p);                  // Issac of Sinci, Omniscience Field
        RollMods galvDice;  // Galvanize: 1 more die each
        for (int t = 0; t < kUnitTypes; ++t) galvDice.extraDice[t] = std::min<int>(shooters.n[t], s.space[size_t(p)].galv[t]);
        RollResult r = rollUnits(p, shooters, RollKind::Bombard, mod, G.fixes >= 12 ? (pl == extraPlanet ? extraOnce : 0) : abilityDice(p, sys),
                                 0, &galvDice);
        for (int t = 0; t < kUnitTypes; ++t)  // Blitz: bombardment 6
            for (int i = 0; i < blitzers.n[t]; ++i) {
                int v = E.rng().d10();
                r.dice.push_back(int8_t(v));
                r.need.push_back(int8_t(6 - mod));
                if (v >= 6 - mod) ++r.hits;
            }
        if (G.modules & kModDiscordant) dsBombardDice(p, sys, r, mod, shooters);  // Davish S'Norri, the Lady, Raid Leaders
        reportRoll(p, sys, "Bombardment of " + planetName(pl), r);
        afterUnitRoll(p, q, sys, r, "bombardment");
        if (x89Omega(G, p) && r.hits > 0) {  // X-89 Bacterial Weapon (Codex, Thunder's Edge's version - 0.43.3)
            r.hits *= 2;
            say(p, pn(p) + "'s bombardment hits are doubled (X-89 Bacterial Weapon): " + std::to_string(r.hits) + ".", sys);
        }
        if (G.modules & kModDiscordant) dsBombardHits(p, q, sys, pl, r);  // Shard Volley, Hyperkinetic Ordnance, Meteor Slings
        int infBefore = G.planets[pl].units[q].count(UnitType::Infantry);
        int gfBefore = G.planets[pl].units[q].groundForces();
        int mask = (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech)) | (fac(q, "titans") ? (1 << int(UnitType::PDS)) : 0);
        r.hits = proximaCancel(q, sys, pl, r.hits);  // Proxima Targeting VI: 1 hit less per galvanized unit there
        if (eventOn(G, kEvConventions) && r.hits > 0) r.hits *= 3;  // Conventions of War Abandoned: 3 units per hit
        assignHits(q, sys, pl, r.hits, mask, "bombardment", p);
        int infAfter = G.planets[pl].units[q].count(UnitType::Infantry);
        if (x89Omega(G, p) && !G.planets[pl].exhausted) {  // ...and the planet it bombarded is exhausted
            G.planets[pl].exhausted = true;
            say(p, planetName(pl) + " is exhausted (X-89 Bacterial Weapon).", sys);
        }
        if (infAfter < infBefore && infAfter > 0 && owns(p, "x89_base") && !x89Omega(G, p) &&
            yesNo(p, Tag::UseTech, "Use X-89 Bacterial Weapon?", "Destroy all of their infantry on " + planetName(pl) + "."))
            destroyUnits(q, UnitType::Infantry, infAfter, sys, pl, p, "X-89");
        if (gfBefore > 0 && G.planets[pl].units[q].groundForces() == 0) actionSecret(p, "mew");
    }
}

// Scramble Frequency: the target makes the roller reroll all of their dice.
void Rules::afterUnitRoll(int roller, int target, int sys, RollResult& r, const char* what) {
    if ((G.modules & kModDiscordant) && roller >= 0 && roller < G.numPlayers) dsUnitRolled(roller, target, sys, r, what);  // Wayfinder, Orion Platform
    afterUnitAbilityRoll(roller, sys, r);  // Agnlan Oln
    confusedSage(roller, target, sys, r);  // Uncharted Space (0.33.0)
    if (target < 0) return;
    WinCtx w;
    w.w = Win::AfterUnitRoll;
    w.a = roller;
    w.b = target;
    w.value = target;  // (the action cards review: for the AI - a third party's Scramble Frequency, fixes 27)
    w.sys = sys;
    if (!cardHas(window(w), "scramble")) return;
    rerollDice(roller, r, true);
    reportRoll(roller, sys, std::string("Rerolled ") + what, r);
}

void Rules::invasion(int p, int sys) {
    SystemState& s = G.systems[sys];
    if (s.planets.empty()) return;
    // A player can only invade if they won (or avoided) space combat (Sh'val skips all that).
    if (otherShipsIn(G, p, sys) && !shval_) return;

    blitz_ = false;
    disabled_ = false;
    bunkerOwner_ = -1;
    // (0.46.1: ground forces brought in to land - Revenant, Combat Drones, G'hom Sek'kus - need no capacity; the
    // check before each question waits until the invasion is over)
    struct Hold {
        int& n;
        explicit Hold(int& x) : n(x) { ++n; }
        ~Hold() { --n; }
    } hold(fleetHold_);
    // Crimson mech: through an active breach (fixes 24: in the Commit Ground Forces step, after bombardment - below)
    int revenantBase = -1;
    if ((G.modules & kModThundersEdge) && G.fixes < 24) revenant(p, sys);
    {
        WinCtx w;
        w.w = Win::InvasionStart;
        w.active = p;
        w.sys = sys;
        for (const Played& x : window(w))
            if (!std::strcmp(kActionCards[x.card].key, "bunker")) bunkerOwner_ = x.player;
    }
    if (G.modules & kModDiscordant) dsInvasionStart(p, sys);  // Hkot Tokal, Zelian A, Collider, Davish S'Norri, Combinatorial Bypass

    if (!shval_) bombardment(p, sys);

    // 0.43.4 (tester, fixes >= 5): G'hom Sek'kus brings the ground forces in first, so the ones it brings can remove
    // the custodians token too (landing on Mecatol Rex from nearby planets with no ships there)
    const bool ghomFirst = G.fixes >= 5;
    if (ghomFirst) ghomSekkus(p, sys);
    // (before the custodians: a Revenant that can be committed to Mecatol Rex lets them be removed - LRR 27.2a)
    if ((G.modules & kModThundersEdge) && G.fixes >= 24) revenantBase = revenant(p, sys);
    // --- custodians token on Mecatol Rex (LRR 27.2)
    int mustLand = -1;
    const int custodiansCost = fac(p, "winnu") ? 0 : 6;  // Blood Ties (Winnu)
    if (s.custodians && !G.custodiansRemoved && s.space[p].groundForces() > 0 && spendable(G, p, true) >= custodiansCost) {
        if (yesNo(p, Tag::Custodians,
                  custodiansCost ? "Spend 6 influence to remove the custodians token?" : "Remove the custodians token (Blood Ties: no influence needed)?",
                  "You gain 1 victory point and must then land at least 1 ground force on Mecatol Rex. "
                  "The agenda phase begins from now on.")) {
            if (custodiansCost == 0 || pay(p, custodiansCost, true, "removing the custodians token", false)) {
                if (custodiansCost == 0) say(p, pn(p) + " removes the custodians token for free (Blood Ties).", sys);
                G.custodiansRemoved = true;
                s.custodians = false;
                for (int16_t pl : s.planets)
                    if (!std::strcmp(kPlanets[G.planets[pl].def].key, "mr")) mustLand = pl;
                gainVP(p, 1, "custodians token");
                Event pf;  // the planetfall scene (display only)
                pf.kind = EventKind::Planetfall;
                pf.player = int8_t(p);
                pf.system = int16_t(sys);
                pf.text = "The Council has been summoned: agenda phases from now on.";
                E.emit(pf);
            }
        }
    }

    // --- commit ground forces
    if (!ghomFirst) ghomSekkus(p, sys);  // Sardakk commander: bring ground forces from nearby planets
    if (s.space[p].groundForces() == 0 && !dsSlingPlanets_.empty()) {  // (Meteor Slings: those infantry still fight)
        for (int pl : std::vector<int>(dsSlingPlanets_)) dsResolvePlanet(p, pl);
        dsSlingPlanets_.clear();
    }
    // (fixes 27, the Naalu review) the Matriarch's fighters may be committed without ground forces
    if (s.space[p].groundForces() == 0 && !(G.fixes >= 27 && fightersLand(p, sys) && s.space[p].count(UnitType::Fighter) > 0)) {
        engageCoexisting(p, sys, {});  // (0.44.2: the ground forces already on a planet can end a coexistence)
        coexistStructures();           // (fixes 25: LRR 63.4 / 79.4 after those fights)
        blitz_ = disabled_ = false;
        bunkerOwner_ = -1;
        return;
    }
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Land;
    d.tag = Tag::LandForces;
    d.prompt = "Land ground forces in " + sysName(sys) + "?";
    d.context = mustLand >= 0 ? "At least 1 must land on Mecatol Rex."
                              : "Choose how many infantry / mechs go to each planet. Landing nothing is allowed.";
    d.system = sys;
    d.planet = mustLand;
    LandSpec spec;
    spec.mustLand = mustLand;
    const bool matriarch = fightersLand(p, sys) && s.space[p].count(UnitType::Fighter) > 0;
    if (matriarch) {  // Matriarch (Naalu flagship): fighters land and fight as ground forces
        spec.typeMask = kGroundMaskT | (1 << int(UnitType::Fighter));
        d.hitMask = spec.typeMask;
        d.context += " Matriarch: your fighters may land too (they return to space after the combat).";
    }
    for (int16_t x : s.planets)  // Omniopiares (Keleres mech): landing there costs influence
        if (const int toll = omniopiaresToll(p, x))
            d.context += " Landing on " + planetName(x) + " costs " + std::to_string(toll) + " influence (Keleres mech).";
    const Game* gp = &G;
    d.validate = [gp, p, sys, spec](const Answer& a) { return checkLandPlan(*gp, p, sys, a.data, spec); };
    Answer a = E.ask(std::move(d));
    std::vector<int> landed, tollPaid, tollRefused;
    for (size_t i = 0; i < a.data.size(); i += 3) {
        int pl = a.data[i], t = a.data[i + 1], k = a.data[i + 2];
        if (const int toll = omniopiaresToll(p, pl)) {
            if (std::find(tollPaid.begin(), tollPaid.end(), pl) == tollPaid.end() &&
                std::find(tollRefused.begin(), tollRefused.end(), pl) == tollRefused.end()) {
                if (pay(p, toll, true, "committing ground forces to " + planetName(pl) + " (Omniopiares)", false)) {
                    tollPaid.push_back(pl);
                } else {
                    tollRefused.push_back(pl);
                    say(p, pn(p) + "'s ground forces can't land on " + planetName(pl) + ": the Keleres mech's toll isn't paid.", sys);
                }
            }
            if (std::find(tollRefused.begin(), tollRefused.end(), pl) != tollRefused.end()) continue;
        }
        Force& src = s.space[p];
        int undamaged = src.n[t] - src.dmg[t];
        int dmgMoved = std::max(0, k - undamaged);
        const int galvMoved = std::min<int>(k, src.galv[t]);
        src.galv[t] = uint8_t(src.galv[t] - galvMoved);
        src.remove(UnitType(t), k, false);
        G.planets[pl].units[p].add(UnitType(t), k);
        G.planets[pl].units[p].dmg[t] = uint8_t(G.planets[pl].units[p].dmg[t] + dmgMoved);
        G.planets[pl].units[p].galv[t] = uint8_t(G.planets[pl].units[p].galv[t] + galvMoved);
        if (std::find(landed.begin(), landed.end(), pl) == landed.end()) landed.push_back(pl);
        event(EventKind::Land, p,
              pn(p) + " lands " + std::to_string(k) + " " + unitTypeName(UnitType(t), k > 1) + " on " + planetName(pl) + ".",
              sys, pl);
    }
    // (fixes 25, Sardakk review 2026-11-11) G'hom Sek'kus "commits (moves)" ground forces: the ones it brought that didn't
    // land go back to their planets (no ship carries them) - whatever is left above what the ships brought in
    if (G.fixes >= 25 && !ghomBrought_.empty()) {
        Force& sp = s.space[p];
        int excess[2] = {sp.count(UnitType::Infantry) - ghomBefore_[0], sp.count(UnitType::Mech) - ghomBefore_[1]};
        int back = 0;
        for (auto it = ghomBrought_.rbegin(); it != ghomBrought_.rend(); ++it) {
            const UnitType t = UnitType(it->type);
            const int i = t == UnitType::Mech ? 1 : 0;
            if (excess[i] <= 0 || sp.count(t) <= 0) continue;
            --excess[i];
            const bool dmg = it->damaged && sp.dmg[int(t)] > 0;
            sp.remove(t, 1, dmg);
            Force& f = G.planets[size_t(it->planet)].units[p];
            f.add(t, 1);
            if (dmg) ++f.dmg[int(t)];
            ++back;
        }
        if (back) say(p, pn(p) + " sends " + std::to_string(back) + (back == 1 ? " ground force back to its planet" : " ground forces back to their planets") +
                             " (G'hom Sek'kus: not landed).", sys);
    }
    ghomBrought_.clear();
    // The Revenant is committed or not at all (fixes 24): one that wasn't landed goes back to the reinforcements
    if (revenantBase >= 0 && s.space[p].count(UnitType::Mech) > revenantBase) {
        s.space[p].remove(UnitType::Mech, 1, false);
        say(p, pn(p) + "'s Revenant isn't committed and returns to reinforcements.", sys);
    }
    // Auriga (Kyro flagship, Discordant Stars): 1 more infantry from the reinforcements with each commitment here
    if ((G.modules & kModDiscordant) && flagshipAbility(G, p, "kyro") && flagshipIn(G, p, sys))
        for (int pl : landed)
            if (unitsAvailable(G, p, UnitType::Infantry) > 0 &&
                fyes(p, Ab::DsAuriga, "Auriga: commit 1 more infantry from your reinforcements to " + planetName(pl) + "?", "")) {
                addUnits(p, UnitType::Infantry, 1, sys, pl);
                say(p, pn(p) + "'s Auriga adds 1 infantry on " + planetName(pl) + ".", sys);
            }
    if ((G.modules & kModDiscordant) && !landed.empty()) dsLanded(p, sys, landed);  // Jgin Faru (Dih-Mohn agent)
    if (shval_) {
        // Sh'val, Harbinger: purged, and the ships in the active system go back to reinforcements.
        purgeHero(p);
        Force& sp = s.space[p];
        int n = sp.ships();
        for (int t : kShipIdx) sp.n[t] = sp.dmg[t] = 0;
        if (n && G.fixes >= 25 && n == 1) say(p, pn(p) + "'s ship in " + sysName(sys) + " returns to reinforcements (Sh'val).", sys);
        else if (n) say(p, pn(p) + "'s " + std::to_string(n) + " ships in " + sysName(sys) + " return to reinforcements (Sh'val).", sys);
        removeExcessCapacity(p, sys);
    }

    // --- the planet's owner may respond to the landing (Parley, Ghost Squad)
    for (int pl : std::vector<int>(landed)) {
        int owner = G.planets[pl].owner;
        if (owner < 0 || owner == p) continue;
        WinCtx w;
        w.w = Win::AfterLanding;
        w.a = p;
        w.b = owner;
        w.sys = sys;
        w.planet = pl;
        if (cardHas(window(w), "parley")) {
            Force& f = G.planets[pl].units[p];
            for (UnitType t : {UnitType::Infantry, UnitType::Mech, UnitType::Fighter}) {  // + Matriarch fighters
                int k = f.count(t);
                int dk = f.dmg[int(t)];
                f.remove(t, k, false);
                s.space[p].add(t, k);
                s.space[p].dmg[int(t)] = uint8_t(s.space[p].dmg[int(t)] + dk);
            }
            landed.erase(std::find(landed.begin(), landed.end(), pl));
            say(owner, pn(p) + "'s ground forces return to space (Parley).", sys);
        }
    }

    // Meteor Slings (Khrask): infantry thrown down by the bombardment fight like landed units
    for (int pl : dsSlingPlanets_)
        if (std::find(landed.begin(), landed.end(), pl) == landed.end() && G.planets[pl].units[p].groundForces() > 0) landed.push_back(pl);
    dsSlingPlanets_.clear();
    // Ragh's Call: the Saar player's ground forces leave the planet
    for (int pl : landed) raghsCall(p, pl);

    // --- (fixes 25, owner's faction debug pass) "When ground forces are committed": Viper EX-23 / Research Team decide to
    // coexist right after the commit - before space cannon defense (LRR 40: commit, space cannon defense, ground combat);
    // a fresh meeting only (neither side coexisting there yet - those keep their questions at the ground combat). The
    // Viper may coexist with neutral units too (Thunder's Edge: they count as another player's for abilities).
    std::vector<std::pair<int, int>> coexistAsked;  // (planet, other player) decided now
    if (G.fixes >= 25 && (G.modules & kModThundersEdge))
        for (int pl : landed) {
            PlanetState& ps = G.planets[size_t(pl)];
            for (int q = 0; q < G.numPlayers; ++q) {
                // (a planet with only the other side's structures: no question - LRR 63.4 / 79.4 destroy a PDS / dock
                // without its owner's ground forces beside another player's units, coexisting or not; checked in the
                // Deepwrought review)
                if (q == p || !(ps.units[size_t(p)].groundForces() > 0 && groundForcesOn(G, q, pl) > 0)) continue;
                if (((ps.coexist >> p) & 1) || ((ps.coexist >> q) & 1)) continue;
                coexistAsked.push_back({pl, q});
                coexistInstead(p, q, pl);
            }
            if (ps.units[size_t(p)].groundForces() > 0 && neutralGroundOn(G, pl) > 0 && !((ps.coexist >> p) & 1) && fac(p, "firmament") &&
                mechAbilities(G) && ps.units[size_t(p)].count(UnitType::Mech) > 0) {
                curFor_ = kNeutral;
                const bool co = fyes(p, Ab::FirmamentMech, "Viper EX-23: coexist with the neutral units on " + planetName(pl) + " instead of fighting?",
                                     "No combat - and no control of the planet while you coexist (Viper Hollow takes it when you become the Obsidian).");
                curFor_ = -1;
                if (co) startCoexisting(p, pl);
            } else if (G.fixes >= 27 && ps.units[size_t(p)].groundForces() > 0 && neutralGroundOn(G, pl) > 0 && !((ps.coexist >> p) & 1) &&
                       fac(p, "deepwrought")) {
                // (fixes 27, the Deepwrought review) Research Team with neutral units (they count as another player's units)
                curFor_ = kNeutral;
                const bool co = fyes(p, Ab::ResearchTeam, "Research Team: coexist with the neutral units on " + planetName(pl) + " instead of fighting?",
                                     "No combat - and no control of the planet while you coexist (you take it if they are gone).");
                curFor_ = -1;
                if (co) startCoexisting(p, pl);
            }
        }

    // --- space cannon defense (L4 Disruptors: none against the invader; none inside an entropic scar)
    for (int pl : landed) {
        if (owns(p, "l4") || (scarAt(G, sys) && G.fixes < 8)) break;
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == p || (scarAt(G, sys) && !ignoresScars(G, q))) continue;  // (0.44.4: the Nivyn ignore the scar)
            Force guns;
            const Force& f = G.planets[pl].units[q];
            for (int t = 0; t < kUnitTypes && !quietusAt(G, q, sys); ++t) {  // (the Quietus: no unit abilities)
                if (!f.n[t] || unitDef(G, q, UnitType(t)).cannon <= 0) continue;
                if (disabled_ && UnitType(t) == UnitType::PDS) continue;  // Disable
                guns.add(UnitType(t), f.n[t]);
            }
            const int ul = G.planets[pl].ulHero && G.planets[pl].owner == q ? 3
                           : sys == G.mecatol() && G.planets[pl].owner == q && custodiaMecatol(G, q) ? 1  // Custodian's Favour
                                                                                                      : 0;  // Ul the Progenitor
            if (G.fx.bypass == p) continue;  // Combinatorial Bypass (Discordant Stars): no space cannon against this invasion
            if (G.modules & kModDiscordant) dsCannonLimit(q, p, guns);  // Starfall Gunnery
            // Dhonraz Installations (Gledge breakthrough, 0.44.4 fixes 8): their station planet's Space Cannon 5 defends it too
            const int dhonraz = G.fixes >= 8 && (G.modules & kModDiscordant) && G.planets[pl].owner == q && breakthroughIs(G, q, "gledge") &&
                                        stationFor(G, q, pl) && !quietusAt(G, q, sys)
                                    ? 1
                                    : 0;
            if (guns.empty() && !ul && !dhonraz) continue;
            if (!yesNo(q, Tag::SpaceCannonFire, "Fire space cannon at the forces landing on " + planetName(pl) + "?",
                       "Space cannon defense."))
                continue;
            int mod = owns(p, "amd") ? -1 : 0;
            mod += classifiedWeapons(q, sys, guns.total());  // Uncharted Space (0.33.0)
            mod += assail(q, p);                              // Assail (Thunder's Edge)
            if (G.modules & kModDiscordant) mod += dsAbilityMod(q);
            RollMods galvDice;  // Galvanize: 1 more die each
            for (int t = 0; t < kUnitTypes; ++t) galvDice.extraDice[t] = guns.n[t] ? std::min(f.galv[t], f.n[t]) : 0;
            RollResult r = rollUnits(q, guns, RollKind::Cannon, mod, abilityDice(q, sys), 0, &galvDice);
            if (ul) addDice(r, ul, 5 - mod);
            if (dhonraz) addDice(r, dhonraz, 5 - mod);
            reportRoll(q, sys, "Space cannon defense", r);
            afterUnitRoll(q, p, sys, r, "space cannon");
            if (r.hits > 0 && !classified_ && !((G.modules & kModDiscordant) && flagshipAbility(G, q, "kolume") && flagshipIn(G, q, sys)) &&
                !(dsbtOn(q, "veldyr") && (anomaliesFor(G, q, sys) & kNebula))) {
                WinCtx w;
                w.w = Win::BeforeCannonHits;
                w.a = p;
                w.b = q;
                w.sys = sys;
                w.value = r.hits;
                if (cardHas(window(w), "mjets")) r.hits = eventOn(G, kEvWild) ? 0 : r.hits - 1;  // (Wild, Wild Galaxy: all)
            }
            int mask = (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech));
            if (matriarch) mask |= 1 << int(UnitType::Fighter);
            if (r.hits) assignHits(p, sys, pl, r.hits, mask, "space cannon defense", q);
        }
        // Thunder's Edge (0.34.0): neutral PDS on the planet always fire
        Force nguns;
        for (int t = 0; t < kUnitTypes; ++t)
            if (G.planets[pl].units[kNeutral].n[t] && unitDef(G, kNeutral, UnitType(t)).cannon > 0 && !(disabled_ && UnitType(t) == UnitType::PDS))
                nguns.add(UnitType(t), G.planets[pl].units[kNeutral].n[t]);
        if (!nguns.empty() && G.planets[pl].units[p].groundForces() > 0 && !scarAt(G, sys)) {
            RollResult r = rollUnits(kNeutral, nguns, RollKind::Cannon, owns(p, "amd") ? -1 : 0, 0);
            reportRoll(kNeutral, sys, "Space cannon defense", r);
            afterUnitRoll(kNeutral, p, sys, r, "space cannon");
            if (r.hits > 0) {
                WinCtx w;
                w.w = Win::BeforeCannonHits;
                w.a = p;
                w.b = kNeutral;
                w.sys = sys;
                w.value = r.hits;
                if (cardHas(window(w), "mjets")) r.hits = eventOn(G, kEvWild) ? 0 : r.hits - 1;  // (Wild, Wild Galaxy: all)
            }
            int mask = (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech));
            if (matriarch) mask |= 1 << int(UnitType::Fighter);
            if (r.hits > 0) assignHits(p, sys, pl, r.hits, mask, "space cannon defense", kNeutral);
        }
    }

    // --- ground combat and control
    const bool te = (G.modules & kModThundersEdge) != 0;
    // (fixes 27, the Naalu review) the Matriarch's fighters committed alone fight too (no control: they return to space)
    auto committed = [&](int pl) {
        return G.planets[pl].units[p].groundForces() > 0 || (G.fixes >= 27 && matriarch && G.planets[pl].units[p].count(UnitType::Fighter) > 0);
    };
    for (int pl : landed) {
        // (fixes 27, the Saar review - a game ended with the Saar and Sol sharing Tar'mann: the Saar declined to end their
        // coexistence against Sol, then ended it against the Deepwrought) our coexistence on the planet ends or not as one
        // decision, asked once before the fights - then we fight every player there who doesn't coexist
        bool endedAll = false;
        if (te && G.fixes >= 27 && ((G.planets[pl].coexist >> p) & 1) && committed(pl) && !groveBlocks(p, pl)) {
            int foe = -1, most = 0;
            for (int q = 0; q < G.numPlayers; ++q) {
                if (q == p) continue;
                const int k = groundForcesOn(G, q, pl) * (((G.planets[pl].coexist >> q) & 1) ? 1 : 4);  // (those who'd fight first)
                if (k > most) most = k, foe = q;
            }
            if (foe >= 0) {
                curFor_ = foe;
                endedAll = yesNo(p, Tag::EngageCoexisting, "End your coexistence on " + planetName(pl) + ": attack the units there?",
                                 "Your ground forces there fight every other player's units that don't coexist (and those that do, if you "
                                 "choose); if you win you take the planet. Or keep coexisting.");
                curFor_ = -1;
                if (endedAll) {
                    G.planets[pl].coexist = uint16_t(G.planets[pl].coexist & ~(1u << p));
                    say(p, pn(p) + " ends their coexistence on " + planetName(pl) + ".", sys);
                }
            }
        }
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == p) continue;
            if (!(committed(pl) && groundForcesOn(G, q, pl) > 0)) continue;  // Hel-Titans defend too
            // (fixes 25) decided at the commit: coexisting now = no combat, else the fight (no second question)
            if (std::find(coexistAsked.begin(), coexistAsked.end(), std::make_pair(pl, q)) != coexistAsked.end()) {
                if (((G.planets[pl].coexist >> p) & 1) || ((G.planets[pl].coexist >> q) & 1)) continue;
                groundCombat(p, q, pl);
                continue;
            }
            bool engaged = false, ended = endedAll;
            if (te) {  // Thunder's Edge: coexistence
                if (((G.planets[pl].coexist >> p) & 1) && G.fixes >= 27) continue;  // (still coexisting: decided above)
                if ((G.planets[pl].coexist >> p) & 1) {  // our units here coexist: no fight -
                    // 0.44.2 (fixes >= 7, TE rulebook "Coexist"): unless we commit them to end it
                    if (G.fixes < 7 || groveBlocks(p, pl)) continue;
                    curFor_ = q;
                    const bool end = yesNo(p, Tag::EngageCoexisting, "End your coexistence on " + planetName(pl) + ": attack " + pn(q) + "'s units?",
                                           "Your ground forces there fight " + pn(q) + "'s; if you win you take the planet. Or keep coexisting.");
                    curFor_ = -1;
                    if (!end) continue;
                    G.planets[pl].coexist = uint16_t(G.planets[pl].coexist & ~(1u << p));
                    say(p, pn(p) + " ends their coexistence on " + planetName(pl) + ".", sys);
                    ended = true;
                }
                if ((G.planets[pl].coexist >> q) & 1) {  // they coexist: fight them only if we choose to
                    curFor_ = q;
                    const bool engage = yesNo(p, Tag::EngageCoexisting, "Attack " + pn(q) + "'s coexisting units on " + planetName(pl) + "?",
                                              "You may leave them coexisting.");
                    curFor_ = -1;
                    if (!engage) continue;
                    engaged = true;
                    G.planets[pl].coexist = uint16_t(G.planets[pl].coexist & ~(1u << q));
                    if ((G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtCoexistEnded(q, p);  // (0.43.0)
                }
                if (coexistInstead(p, q, pl, ended ? p : -1)) continue;  // Research Team / Viper EX-23 (not the one who chose to fight)
            }
            groundCombat(p, q, pl);
            // 0.43.0 (debug passes): coexisting units that beat off the invader still only coexist with the planet's
            // controller - the fight was with the invader, not with them
            if (engaged && G.fixes >= 2 && groundForcesOn(G, q, pl) > 0 && G.planets[pl].owner != q)
                G.planets[pl].coexist = uint16_t(G.planets[pl].coexist | (1u << q));
        }
        if (committed(pl) && neutralGroundOn(G, pl) > 0 && !(G.fixes >= 25 && ((G.planets[pl].coexist >> p) & 1)))
            groundCombat(p, kNeutral, pl);  // Thunder's Edge (fixes 25: not while coexisting with them - Viper EX-23)
        if (G.planets[pl].units[p].groundForces() > 0 && !((G.planets[pl].coexist >> p) & 1)) {
            bool contested = neutralGroundOn(G, pl) > 0;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && groundForcesOn(G, q, pl) > 0 && !((G.planets[pl].coexist >> q) & 1)) contested = true;
            if (!contested && G.planets[pl].owner != p) setControl(pl, p);
        }
    }
    engageCoexisting(p, sys, landed);  // (0.44.2: planets nobody landed on)
    coexistStructures();               // (fixes 25: LRR 63.4 / 79.4 after the combats and bombardment)
    if (matriarch) returnLandedFighters(p, sys);
    blitz_ = disabled_ = false;
    bunkerOwner_ = -1;
}

// 0.44.2 (fixes >= 7, tester: "when activating / invading a planet with coexisting units, I don't get prompted to fight
// them"): at the invasion step of a tactical action the active player may end a coexistence on a planet of the active
// system with the ground forces already on it - no new ones need to land (Thunder's Edge rulebook "Coexist": combat can
// be started by units committed to the planet "including units that they were already coexisting with", and a
// coexisting player ends it by committing the coexisting units). Asked per planet and player; `skip` = the planets
// ground forces just landed on (asked there already).
void Rules::engageCoexisting(int p, int sys, const std::vector<int>& skip) {
    if (!(G.modules & kModThundersEdge) || G.fixes < 7 || sys < 0) return;
    for (int16_t pl : std::vector<int16_t>(G.systems[size_t(sys)].planets)) {
        if (std::find(skip.begin(), skip.end(), int(pl)) != skip.end()) continue;
        if (groveBlocks(p, pl)) continue;  // (Mended Grove: those infantry can't be committed this action)
        // (fixes 27, the Saar review) our own coexistence ends or not as one decision for the planet; ended, we fight every
        // player there who doesn't coexist, and those who do if we choose
        if (G.fixes >= 27 && ((G.planets[size_t(pl)].coexist >> p) & 1)) {
            if (G.planets[size_t(pl)].units[size_t(p)].groundForces() == 0) continue;
            int foe = -1, most = 0;
            for (int q = 0; q < G.numPlayers; ++q) {
                if (q == p) continue;
                const int k = groundForcesOn(G, q, pl) * (((G.planets[size_t(pl)].coexist >> q) & 1) ? 1 : 4);
                if (k > most) most = k, foe = q;
            }
            if (foe < 0) continue;
            curFor_ = foe;
            const bool end = yesNo(p, Tag::EngageCoexisting, "End your coexistence on " + planetName(pl) + ": attack the units there?",
                                   "Your ground forces already on the planet fight every other player's units that don't coexist (and those "
                                   "that do, if you choose); if you win you take the planet. Or leave things as they are.");
            curFor_ = -1;
            if (!end) continue;
            G.planets[size_t(pl)].coexist = uint16_t(G.planets[size_t(pl)].coexist & ~(1u << p));
            say(p, pn(p) + " ends their coexistence on " + planetName(pl) + ".", sys);
            for (int q = 0; q < G.numPlayers; ++q) {
                PlanetState& ps = G.planets[size_t(pl)];
                if (q == p || ps.units[size_t(p)].groundForces() == 0 || groundForcesOn(G, q, pl) == 0) continue;
                if ((ps.coexist >> q) & 1) {
                    curFor_ = q;
                    const bool go = yesNo(p, Tag::EngageCoexisting, "Attack " + pn(q) + "'s coexisting units on " + planetName(pl) + "?",
                                          "Your ground forces already on the planet fight them. Or leave them coexisting.");
                    curFor_ = -1;
                    if (!go) continue;
                    ps.coexist = uint16_t(ps.coexist & ~(1u << q));
                    if ((G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtCoexistEnded(q, p);
                }
                groundCombat(p, q, pl);
                PlanetState& after = G.planets[size_t(pl)];
                if (groundForcesOn(G, q, pl) > 0 && after.owner != q) after.coexist = uint16_t(after.coexist | (1u << q));  // (still a guest)
            }
            PlanetState& after = G.planets[size_t(pl)];
            if (after.units[size_t(p)].groundForces() > 0 && after.owner != p) {
                bool contested = neutralGroundOn(G, pl) > 0;
                for (int r = 0; r < G.numPlayers; ++r)
                    if (r != p && groundForcesOn(G, r, pl) > 0 && !((after.coexist >> r) & 1)) contested = true;
                if (!contested) setControl(pl, p);
            }
            continue;
        }
        for (int q = 0; q < G.numPlayers; ++q) {
            PlanetState& ps = G.planets[size_t(pl)];
            if (q == p || ps.units[size_t(p)].groundForces() == 0 || groundForcesOn(G, q, pl) == 0) continue;
            const bool mine = (ps.coexist >> p) & 1, theirs = (ps.coexist >> q) & 1;
            if (!mine && !theirs) continue;
            curFor_ = q;
            const bool go = yesNo(p, Tag::EngageCoexisting,
                                  mine ? "End your coexistence on " + planetName(pl) + ": attack " + pn(q) + "'s units?"
                                       : "Attack " + pn(q) + "'s coexisting units on " + planetName(pl) + "?",
                                  "Your ground forces already on the planet fight them" +
                                      std::string(mine ? "; if you win you take the planet." : ".") + " Or leave things as they are.");
            curFor_ = -1;
            if (!go) continue;
            ps.coexist = uint16_t(ps.coexist & ~((1u << p) | (1u << q)));
            say(p, pn(p) + (mine ? " ends their coexistence on " : " attacks the coexisting units on ") + planetName(pl) + ".", sys);
            if (theirs && (G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtCoexistEnded(q, p);
            groundCombat(p, q, pl);
            PlanetState& after = G.planets[size_t(pl)];
            if (groundForcesOn(G, q, pl) > 0 && after.owner != q) after.coexist = uint16_t(after.coexist | (1u << q));  // (still a guest)
            if (after.units[size_t(p)].groundForces() > 0 && after.owner != p) {  // the coexisting attacker won: the planet is theirs
                bool contested = neutralGroundOn(G, pl) > 0;
                for (int r = 0; r < G.numPlayers; ++r)
                    if (r != p && groundForcesOn(G, r, pl) > 0 && !((after.coexist >> r) & 1)) contested = true;
                if (!contested) setControl(pl, p);
            }
        }
    }
}

// ------------------------------------------------------------------ production
void Rules::production(int p, int sys, int capacityLimit, bool viaWarfare, bool shipsOnly) {
    productionCore(p, sys, capacityLimit, viaWarfare, shipsOnly);
    // Keleres: another Production, in any other system (0.44.5, fixes 9: not after units produced by another ability)
    if (!shipsOnly && !(G.fixes >= 9 && notProductionAbility_)) agencySupply(p, sys);
}

void Rules::productionCore(int p, int sys, int capacityLimit, bool viaWarfare, bool shipsOnly) {
    if (capacityLimit <= 0) return;
    // 0.44.5 (fixes 9): produced by another ability (Sling Relay, Chaos Mapping, Umbat...) - no Sarween Tools, no AI
    // Development Algorithm (both work with the PRODUCTION ability only)
    const bool otherAbility = G.fixes >= 9 && notProductionAbility_;
    struct NoSarween {
        Game& g;
        bool prev;
        NoSarween(Game& gg, bool on) : g(gg), prev(gg.fx.noSarween) { if (on) g.fx.noSarween = true; }
        ~NoSarween() { g.fx.noSarween = prev; }
    } noSarween(G, otherAbility);
    int discount = 0;
    if (!shipsOnly) {
        // War Machine: +4 production and 1 cheaper. (0.44.4: not offered for a production that costs nothing - Matriphagy)
        const bool freeProd = G.fixes >= 8 && dsFreeProd_;
        WinCtx w;
        w.w = Win::Production;
        w.a = p;
        w.sys = sys;
        if (!freeProd && cardHas(window(w), "war_machine")) {
            capacityLimit += 4;
            discount += eventOn(G, kEvWild) ? 5 : 1;  // Wild, Wild Galaxy: 5 cheaper
        }
        // Harrugh Gefhara: free this time ("When 1 or more of your units use PRODUCTION" - fixes 26, the Hacan review, owner
        // 2026-11-12: not for productions by other abilities, as Sarween Tools / AI Development Algorithm since 0.44.5)
        if (!(G.fixes >= 26 && otherAbility) && harruhgFree(p)) discount += 1000;
        if (dsFreeProd_) discount += 1000;     // Matriphagy (Cheiran breakthrough, 0.43.0): these units cost nothing
        // AI Development Algorithm: cheaper by the number of unit upgrades you own.
        int ups = 0;
        for (int16_t t : G.players[p].techs)
            if (kTechs[t].unitUpgrade) ++ups;
        if (ups > 0 && !freeProd && !otherAbility && techReady(p, "aida") &&
            yesNo(p, Tag::UseTech, "Exhaust AI Development Algorithm?", "Your units produced now cost " + std::to_string(ups) + " less.")) {
            exhaustTech(p, "aida");
            discount += ups;
        }
        // Hegemonic Trade Policy (0.44.5, fixes 9): a space dock on the swapped planet gets the higher Production value
        const int capBefore = G.fixes >= 9 ? productionCapacity(G, p, sys) : 0;
        discount += productionDiscount3(p, sys);  // Berekar Berekon, Hegemonic Trade Policy
        if (G.fixes >= 9 && G.fx.swapPlanet >= 0 && capacityLimit >= capBefore)
            capacityLimit += std::max(0, productionCapacity(G, p, sys) - capBefore);
        if (G.fixes < 8) discount += particleSynthesis(G, p, sys);  // Particle Synthesis (Creuss breakthrough)
        if (G.modules & kModDiscordant) capacityLimit += dsProductionBonus(p, sys, capacityLimit);  // Durran, Merged Replicators
    }
    // Particle Synthesis (0.44.4 fixes 8): 1 cheaper per wormhole in the system - ships there or not, ships-only too
    if (G.fixes >= 24 && breakthroughIs(G, p, "ghost"))
        discount += wormholeCount(G, sys);  // (fixes 24: each wormhole, not each kind)
    else if (G.fixes >= 8 && breakthroughIs(G, p, "ghost"))
        for (uint8_t w = wormholesOf(G, sys); w; w &= uint8_t(w - 1)) ++discount;
    const int costCap = dsCostCap_;  // Maximus (Dih-Mohn flagship): a combined cost limit
    discount += dsDiscount_;  // Moult, Brood Pod, Obsessive Designs (Discordant Stars)
    discount += dsDiscount2_;  // Industry Secrets (Discordant Stars)
    dsDiscount2_ = 0;
    // which unit types may be produced (ships only / Umbat: cost 4 or less)
    int typeMask = dsTypeMask_;  // (Obsessive Designs: the new design only)
    // Combat Drones (Mirveda, Discordant Stars): no infantry from their space docks
    if (!typeMask && (G.modules & kModDiscordant) && fac(p, "mirveda") && !anyPlanetGFFor(G, p))
        typeMask = ((1 << kUnitTypes) - 1) & ~(1 << int(UnitType::Infantry));
    if (shipsOnly || maxUnitCost_ > 0)
        for (int t = 0; t < kUnitTypes; ++t) {
            if (shipsOnly && !isShip(UnitType(t))) continue;
            if (maxUnitCost_ > 0 && unitCost2(G, p, UnitType(t)) > 2 * maxUnitCost_) continue;
            typeMask |= 1 << t;
        }
    const bool molds = commanderUsable(G, p, "cabal") && !shipsOnly;  // That Which Molds Flesh: +2 fighters / infantry
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Produce;
    d.tag = Tag::Production;
    d.prompt = shipsOnly ? (capacityLimit > 1 ? "Produce up to " + std::to_string(capacityLimit) + " ships in " : std::string("Produce 1 ship in ")) + sysName(sys)
                         : "Produce units in " + sysName(sys) + "?";
    d.context = "Up to " + std::to_string(capacityLimit) + " units." +
                (lawInPlay(G, "conscription") ? " Regulated Conscription: fighters and infantry cost 1 each."
                                              : " Fighters and infantry cost 1 for two.") +
                (discount >= 1000 ? std::string(dsFreeProd_ ? " Everything is free this time (Matriphagy)." : " Everything is free this time (Harrugh Gefhara).")
                 : discount       ? " " + std::to_string(discount) + " cheaper this time."
                                  : std::string());
    d.system = sys;
    d.amount = capacityLimit + (molds ? 2 : 0);
    d.value = discount;
    d.hitMask = typeMask;
    if (maxUnitCost_ > 0) d.context += " Only units that cost 4 or less.";
    if (costCap > 0) d.context += " Their combined cost is " + std::to_string(costCap) + " or less.";
    d.costCap = costCap;
    if (molds) d.context += " That Which Molds Flesh: 2 fighters / infantry don't count against the limit.";
    if (G.fixes >= 14) d.context += " Ships beyond your fleet pool are removed afterwards - you choose which.";
    if (breakthroughIs(G, p, "sol"))
        d.context += " Bellum Gloriosum: ground forces and fighters up to the capacity of the ships you produce don't count.";
    if (amalgamationSavings(G, p, std::vector<int>(kUnitTypes, 20).data()) > 0)
        d.context += " Amalgamation: your captured units can pay for units of their type.";
    const Game* gp = &G;
    // Contractual Obligations (0.44.5, fixes 9): 1 unit must be produced when one fits and can be paid
    bool must = false;
    if (G.fixes >= 9 && mustProduce_) {
        for (int t = 0; t < kUnitTypes && !must; ++t) {
            if (shipsOnly && !isShip(UnitType(t))) continue;
            std::vector<int> one(kUnitTypes + 1, 0);
            one[size_t(t)] = 1;
            one[kUnitTypes] = -1;
            if (!checkProducePlan(G, p, sys, one, capacityLimit, typeMask).empty()) continue;
            int c[kUnitTypes] = {};
            c[t] = 1;
            if (std::max(0, productionCost(G, p, c) - discount) + terrafactoryExtra(G, p, sys, c) <= spendable(G, p, false)) must = true;
        }
        if (must) d.context += " You must produce 1.";
    }
    d.mustProduce = must;
    d.validate = [gp, p, sys, capacityLimit, discount, shipsOnly, typeMask, costCap, must](const Answer& a) -> std::string {
        std::string e = checkProducePlan(*gp, p, sys, a.data, capacityLimit, typeMask);
        if (!e.empty()) return e;
        int counts[kUnitTypes];
        int total = 0;
        for (int t = 0; t < kUnitTypes; ++t) {
            counts[t] = a.data[t];
            total += counts[t];
            if (shipsOnly && counts[t] && !isShip(UnitType(t))) return "Only ships can be produced here.";
        }
        if (must && total == 0) return "Contractual Obligations: you must produce 1 ship.";
        if (costCap > 0 && unitsCostValue(*gp, p, counts) > costCap) return "Their combined cost can't be more than " + std::to_string(costCap) + ".";
        int cost = std::max(0, productionCost(*gp, p, counts) - discount - amalgamationSavings(*gp, p, counts)) + terrafactoryExtra(*gp, p, sys, counts);
        if (cost > spendable(*gp, p, false)) return "You can't afford that (" + std::to_string(cost) + " resources).";
        return "";
    };
    Answer a = E.ask(std::move(d));
    int counts[kUnitTypes];
    int total = 0;
    for (int t = 0; t < kUnitTypes; ++t) {
        counts[t] = a.data[t];
        total += counts[t];
    }
    if (total == 0) {
        G.fx.swapPlanet = -1;
        return;
    }
    const int saved = discount >= 1000 ? 0 : amalgamation(p, sys, counts);  // Vuil'raith: captured units pay
    int cost = std::max(0, productionCost(G, p, counts) - discount - saved) + terrafactoryExtra(G, p, sys, counts);
    // Privateer (Florzen mech, Discordant Stars): the mechs' part may be paid with influence
    const int infl = (G.modules & kModDiscordant) && cost > 0 && counts[int(UnitType::Mech)] ? dsPrivateer(p, counts, cost) : 0;
    if (!pay(p, cost - infl, false, "production", false, infl)) {
        G.fx.swapPlanet = -1;
        return;
    }
    int gfPlanet = a.data[kUnitTypes];
    if (counts[int(UnitType::Flagship)] && commanderUsable(G, p, "nomad")) say(p, pn(p) + "'s flagship costs nothing (Navarch Feng).", sys);
    Force made;
    for (int t = 0; t < kUnitTypes; ++t) {
        if (!counts[t]) continue;
        UnitType ut = UnitType(t);
        made.add(ut, counts[t]);
        addUnits(p, ut, counts[t], sys, isGroundForce(ut) ? gfPlanet : -1);
    }
    event(EventKind::Produce, p,
          pn(p) + " produces " + unitList(made, &G, p) + " in " + sysName(sys) + (viaWarfare ? " (Warfare)" : "") + ".", sys);
    removeExcessCapacity(p, sys);
    if (G.fixes >= 27) checkFloatingFactories(sys);  // (fixes 27, the Saar review: ships produced beside a lone floating factory)
    afterProduction3(p, sys, made);  // Rowl Sarrig; Hegemonic Trade Policy ends
    afterProduction4(p, sys, made, gfPlanet);  // Tungstantus, Trilossa Aun Mirik
    btProduced(p, sys, counts);                 // Auto-Factories (Thunder's Edge)
    if (G.modules & kModDiscordant) dsAfterProduction(p, sys);  // Rond Bri'ay
    if (G.modules & kModDiscordant) ds4Produced(p, sys, made);  // Cloaked Fleets, Walik
    if (G.modules & kModDiscordant) ds5Produced(p, sys, made);  // Rally to the Cause, the Lady or the Lord
    if (G.modules & kModDiscordant) ds7Produced(p, sys, made);  // Parallel Production, Tioleombp
    theIcon(p, sys, counts);                    // The Icon (Last Bastion breakthrough)
    phantomProduced(p, sys, counts);            // Homesick Phantom (Crimson hero)
    yinSpinner(p, sys);
    // (fixes 26, the Muaat review - owner 2026-11-12) the original Magmus Reactor (games without the Codex / Thunder's
    // Edge): "After 1 or more of your units use PRODUCTION in a system that either contains a war sun or is adjacent to a
    // supernova, gain 1 trade good." (not after another ability's production - Umbat, Sling Relay...)
    if (magmusOriginal(G) && !notProductionAbility_ && ownsTechKey(G, p, "mr")) {
        const SystemState& ms = G.systems[size_t(sys)];
        bool warSun = false, nova = false;
        for (int q = 0; q < kSides; ++q) warSun = warSun || ms.space[size_t(q)].count(UnitType::WarSun) > 0;
        for (int16_t a2 : ms.adj) nova = nova || (G.systems[size_t(a2)].d().anomalies & kSupernova);
        if (warSun || nova) {
            say(p, pn(p) + " gains 1 trade good (Magmus Reactor).", sys);
            gainTG(p, 1);
        }
    }
    if (shipsOnly) return;
    // Prophecy of Ixth: lost unless 2 or more fighters were produced.
    if (lawOwner(G, "prophecy") == p && counts[int(UnitType::Fighter)] < 2)
        for (size_t i = 0; i < G.laws.size(); ++i)
            if (!std::strcmp(kAgendas[G.laws[i].agenda].key, "prophecy")) {
                discardLaw(int(i), "fewer than 2 fighters produced");
                break;
            }
    // Self-Assembly Routines: place a mech on a planet you control in this system.
    // (0.44.8, fixes 12: only after the PRODUCTION ability - not an ability's own production; no Demilitarized Zone)
    if (techReady(p, "sar") && unitsAvailable(G, p, UnitType::Mech) > 0 && !(G.fixes >= 12 && notProductionAbility_)) {
        std::vector<Option> opts;
        for (int16_t x : G.systems[sys].planets)
            if (G.planets[x].owner == p && !(G.fixes >= 12 && isDMZ(G, x))) opts.push_back({x, planetName(x), ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Don't use it", ""});
            int x = pick(p, Tag::UseTech, "Exhaust Self-Assembly Routines to place a mech?", opts, "", false, true);
            if (x >= 0) {
                exhaustTech(p, "sar");
                addUnits(p, UnitType::Mech, 1, sys, x);
                say(p, pn(p) + " places a mech on " + planetName(x) + ".", sys);
            }
        }
    }
}

// Magen Defense Grid Omega (Codex, 0.31.0): when any player activates a system with your structures, 1 infantry
// from your reinforcements joins each of them. (0.44.8, fixes 12: called before the "after activation" effects
// such as Minister of Peace; the Cheiran's Byssus mechs count; structures in space - floating factories, carried
// structures - get theirs in the space area as far as capacity allows)
void Rules::magenActivation(int sys) {
    if (!codexOn(G)) return;
    const bool v12 = G.fixes >= 12;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (!owns(q, "md_base") || G.players[q].eliminated) continue;
        for (int16_t x : G.systems[sys].planets) {
            int s = G.planets[x].units[q].structures();
            if (v12 && (G.modules & kModDiscordant) && G.planets[x].owner == q && fac(q, "cheiran"))
                s += G.planets[x].units[q].count(UnitType::Mech);
            const int n = std::min(s, unitsAvailable(G, q, UnitType::Infantry));
            if (n <= 0) continue;
            addUnits(q, UnitType::Infantry, n, sys, x);
            say(q, pn(q) + " places " + std::to_string(n) + " infantry on " + planetName(x) + " (Magen Defense Grid).", sys);
        }
        if (v12) {
            const int s = G.systems[sys].space[q].structures();
            const int room = shipCapacity(G, q, sys) - transportedInSpace(G, q, sys);
            const int n = std::min({s, room, unitsAvailable(G, q, UnitType::Infantry)});
            if (n > 0) {
                addUnits(q, UnitType::Infantry, n, sys, -1);
                say(q, pn(q) + " places " + std::to_string(n) + " infantry in space in " + sysName(sys) + " (Magen Defense Grid).", sys);
            }
        }
    }
}

// Integrated Economy: after gaining a planet, produce there up to its resource value.
void Rules::integratedEconomy(int p, int planet) {
    if (!owns(p, "ie")) return;
    int budget = planetResources(G, planet);
    if (budget <= 0) return;
    const int sys = G.planets[planet].system;
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Produce;
    d.tag = Tag::Production;
    d.prompt = "Integrated Economy: produce on " + planetName(planet) + "?";
    d.context = "Units with a combined cost of up to " + std::to_string(budget) + ". Ground forces go on " + planetName(planet) + ".";
    d.system = sys;
    d.amount = 20;
    d.planet = planet;
    d.costCap = budget;  // 0.43.0 (debug passes): the bots didn't know the limit (and had answers turned down)
    const Game* gp = &G;
    struct NoSarween {  // 0.44.5 (fixes 9): Integrated Economy isn't the PRODUCTION ability - no Sarween Tools
        Game& g;
        bool prev;
        NoSarween(Game& gg, bool on) : g(gg), prev(gg.fx.noSarween) { if (on) g.fx.noSarween = true; }
        ~NoSarween() { g.fx.noSarween = prev; }
    } noSarween(G, G.fixes >= 9);
    d.validate = [gp, p, sys, planet, budget](const Answer& a) -> std::string {
        if (a.data.size() != size_t(kUnitTypes) + 1) return "Bad answer.";
        int counts[kUnitTypes];
        bool gf = false;
        for (int t = 0; t < kUnitTypes; ++t) {
            counts[t] = a.data[t];
            if (counts[t] < 0) return "Bad count.";
            if (counts[t] && isStructure(UnitType(t))) return "Structures can't be produced.";
            if (counts[t] && unitDef(*gp, p, UnitType(t)).cost2 <= 0) return "That unit can't be produced.";
            if (counts[t] > unitsAvailable(*gp, p, UnitType(t))) return "Not enough units left in your supply.";
            if (counts[t] && isShip(UnitType(t)) && blockaded(*gp, p, sys) && !gp->fx.noBlockade &&
                !(breakthroughsOn(*gp) && breakthroughIs(*gp, p, "dihmohn")))  // (Exodus Engineering, 0.43.0)
                return "Blockaded: no ships can be produced here.";
            if (counts[t] && isGroundForce(UnitType(t))) gf = true;
        }
        if (gf && a.data[kUnitTypes] != planet) return "Ground forces go on " + std::string(kPlanets[gp->planets[planet].def].name) + ".";
        int cost = productionCost(*gp, p, counts);
        // (0.43.0: the combined cost is the units' cost - Sarween Tools lowers what you pay, not the cost; fixes >= 2)
        if (unitsCostValue(*gp, p, counts) > budget) return "Total cost is limited to " + std::to_string(budget) + ".";
        if (cost > spendable(*gp, p, false)) return "You can't afford that.";
        return "";
    };
    Answer a = E.ask(std::move(d));
    int counts[kUnitTypes];
    int total = 0;
    for (int t = 0; t < kUnitTypes; ++t) {
        counts[t] = a.data[t];
        total += counts[t];
    }
    if (!total) return;
    if (!pay(p, productionCost(G, p, counts), false, "Integrated Economy", false)) return;
    Force made;
    for (int t = 0; t < kUnitTypes; ++t)
        if (counts[t]) {
            made.add(UnitType(t), counts[t]);
            addUnits(p, UnitType(t), counts[t], sys, isGroundForce(UnitType(t)) ? planet : -1);
        }
    event(EventKind::Produce, p, pn(p) + " produces " + unitList(made, &G, p) + " on " + planetName(planet) + " (Integrated Economy).", sys);
    if ((G.modules & kModThundersEdge) && G.fixes >= 24) phantomProduced(p, sys, counts);  // Homesick Phantom
    enforceFleetLimit(p);
    removeExcessCapacity(p, sys);
}

}  // namespace ti
