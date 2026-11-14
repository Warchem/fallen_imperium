#include "sim/tempo.h"

#include "ai/ai_internal.h"
#include "engine/query.h"

#include <algorithm>
#include <cstring>
#include <queue>

#include "engine/data.h"
#include "engine/personality.h"
#include "engine/query.h"

namespace ti {
float aiReach(const Game& g, int q, int viewer);  // (ai_endgame.cpp)

namespace {

int levelOf(const Game& g, int p) { return g.players[size_t(p)].aiLevel >= 0 ? g.players[size_t(p)].aiLevel : g.aiLevel; }

const char* levelName(int l) {
    static const char* k[] = {"Easy", "Normal", "Hard", "Competitive"};
    return l >= 0 && l <= 3 ? k[l] : "?";
}

int countScored(const Game& g, int p, bool secret) {
    int n = 0;
    for (int16_t o : g.players[size_t(p)].scored) {
        const int k = kObjectives[o].kind;
        if (secret ? k == 3 : (k == 1 || k == 2)) ++n;
    }
    return n;
}

}  // namespace

void TempoScore::startGame(const Game& g) {
    started_ = true;
    mecatolTaken_ = false;
    lastRound_ = 0;
    lastPhase_ = g.phase;
    const int n = int(g.systems.size()), np = g.numPlayers;
    // slices: systems whose nearest home (in moves, through wormholes / hyperlanes) is one player's
    std::vector<std::vector<int>> dist(size_t(np), std::vector<int>(size_t(n), 99));
    for (int p = 0; p < np; ++p) {
        const int h = g.players[size_t(p)].homeSystem;
        if (h < 0) continue;
        std::queue<int> q;
        dist[size_t(p)][size_t(h)] = 0;
        q.push(h);
        if (g.systems[size_t(h)].offBoard)  // (as aiSliceOf: an off-board home counts from its gate)
            for (int16_t y : g.systems[size_t(h)].adj) dist[size_t(p)][size_t(y)] = 0, q.push(y);
        while (!q.empty()) {
            const int x = q.front();
            q.pop();
            for (int16_t y : g.systems[size_t(x)].adj)
                if (dist[size_t(p)][size_t(y)] > dist[size_t(p)][size_t(x)] + 1) {
                    dist[size_t(p)][size_t(y)] = dist[size_t(p)][size_t(x)] + 1;
                    q.push(y);
                }
        }
    }
    slice_.assign(size_t(n), -1);
    tied_.assign(size_t(n), {});
    near_.assign(size_t(n), 99);
    for (int x = 0; x < n; ++x) {
        const SystemState& s = g.systems[size_t(x)];
        if (s.home >= 0 || x == g.mecatol() || s.planets.empty()) continue;
        int best = 99;
        for (int p = 0; p < np; ++p) best = std::min(best, dist[size_t(p)][size_t(x)]);
        if (best >= 99) continue;
        near_[size_t(x)] = best;
        for (int p = 0; p < np; ++p)
            if (dist[size_t(p)][size_t(x)] == best) tied_[size_t(x)].push_back(p);
        slice_[size_t(x)] = tied_[size_t(x)].size() > 1 ? -2 : tied_[size_t(x)][0];
    }
    spent_.assign(size_t(np), 0);
    spentRound_.assign(size_t(np), 0);
    pubPrev_.assign(size_t(np), 0);
    secPrev_.assign(size_t(np), 0);
    eqHeld3_.assign(size_t(np), 0);
    vp4_.assign(size_t(np), 0);
    vp6_.assign(size_t(np), 0);
    unspentSum_.assign(size_t(np), 0);
    unspentN_.assign(size_t(np), 0);
    pubRounds_.assign(size_t(np), 0);
    sliceOwn3_.assign(size_t(np), 0);
    sliceN3_.assign(size_t(np), 0);
    sliceNone3_.assign(size_t(np), 0);
    betrayFrom_.assign(size_t(np), -1);
    betrayCount_.assign(size_t(np), 0);
    for (int p = 0; p < np; ++p) betrayCount_[size_t(p)] = g.players[size_t(p)].pers.betrayals;
    // 0.53.0 operations scorecard
    combatSys_ = combatAtt_ = combatDef_ = -1;
    styxSeen_ = styxHit_ = styxOwned_ = false;
    styxPl_ = styxHolder_ = -1;
    homeOwner_.assign(g.planets.size(), -1);
    for (size_t x = 0; x < g.planets.size(); ++x) {
        const int sys = g.planets[x].system;
        if (sys >= 0 && sys < int(g.systems.size()) && g.systems[size_t(sys)].home >= 0 && g.systems[size_t(sys)].home < np)
            homeOwner_[x] = g.systems[size_t(sys)].home;
    }
    homeLast_ = homeOwner_;
}

void TempoScore::events(const Game& g, const std::vector<Event>& evs) {
    if (!started_) return;
    for (const Event& e : evs) {
        if (!e.space) continue;
        if (e.kind == EventKind::CombatStart) {
            combatSys_ = combatAtt_ = combatDef_ = -1;
            if (e.player < 0 || e.player >= g.numPlayers || e.system < 0 || e.system >= int(g.systems.size())) continue;
            combatSys_ = e.system;
            combatAtt_ = e.player;
            combatDef_ = e.other;
            bigNow_ = e.b.nonFighterShips() >= 4;
            const double att = e.a.nonFighterShips() + 0.4 * e.a.count(UnitType::Fighter);
            const double def = e.b.nonFighterShips() + 0.4 * e.b.count(UnitType::Fighter);
            const double ratio = att / std::max(1.0, def);
            const SystemState& s = g.systems[size_t(e.system)];
            bool styx = false;
            for (int16_t pl : s.planets)
                if (!std::strcmp(kPlanets[size_t(g.planets[size_t(pl)].def)].key, "styx")) styx = true;
            Fight* fs[6] = {&fAll_, e.b.nonFighterShips() >= 4 ? &fBig_ : nullptr,
                            (s.home >= 0 && s.home == e.other) ? &fHome_ : nullptr, e.system == g.mecatol() ? &fMecatol_ : nullptr,
                            styx ? &fStyx_ : nullptr, e.other == kNeutral ? &fNeutral_ : nullptr};
            for (Fight* x : fs)
                if (x) x->n += 1, x->ratio += ratio;
            if (styx && e.other != kNeutral) styxHit_ = true;  // a player's Styx attacked
        } else if (e.kind == EventKind::CombatEnd) {
            if (e.system != combatSys_ || combatAtt_ < 0) continue;
            if (e.player == combatAtt_) {
                const SystemState& s = g.systems[size_t(e.system)];
                bool styx = false;
                for (int16_t pl : s.planets)
                    if (!std::strcmp(kPlanets[size_t(g.planets[size_t(pl)].def)].key, "styx")) styx = true;
                fAll_.won += 1;
                // (the defender's ship count is gone by now: "big" is judged again from the start's snapshot)
                if (bigNow_) fBig_.won += 1;
                if (s.home >= 0 && s.home == combatDef_) fHome_.won += 1;
                if (e.system == g.mecatol()) fMecatol_.won += 1;
                if (styx) fStyx_.won += 1;
                if (combatDef_ == kNeutral) fNeutral_.won += 1;
            }
            combatSys_ = combatAtt_ = combatDef_ = -1;
        }
    }
}

void TempoScore::answered(const Game& g, const Decision& d, const Answer& a) {
    if (!started_ || d.kind != DecisionKind::Produce || d.player < 0 || d.player >= g.numPlayers) return;
    double cost = 0;
    for (int t = 0; t < kUnitTypes && t < int(a.data.size()); ++t)
        if (a.data[t] > 0) cost += (a.data[t] * unitDef(g, d.player, UnitType(t)).cost2 + 1) / 2;
    spentRound_[size_t(d.player)] += cost;
    if (g.round <= 3) spent_[size_t(d.player)] += cost;
}

void TempoScore::roundStart(const Game& g) {
    const int r = g.round;
    const int np = g.numPlayers;
    if (r >= 2 && r - 1 < 10)  // the Saar's floating factories at the end of each round
        for (int p = 0; p < np; ++p) {
            if (!isFaction(g, p, "saar")) continue;
            int ff = 0, out = 0;
            bool ball = false;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                const Force& f = g.systems[s].space[size_t(p)];
                const int k = f.count(UnitType::SpaceDock);
                if (!k) continue;
                ff += k;
                if (int(s) != g.players[size_t(p)].homeSystem) {
                    out += k;
                    if (f.nonFighterShips() >= 3) ball = true;
                }
            }
            saarN_[r - 1] += 1;
            saarFF_[r - 1] += ff;
            saarOut_[r - 1] += out;
            saarBall_[r - 1] += ball;
        }
    if (r >= 2 && r <= 4 && !g.custodiansRemoved)
        for (int p = 0; p < np; ++p) {
            stagedN_[r] += 1;
            stagedR_[r] += aiMecatolStaged(g, p);
        }
    if (const int mec = g.mecatol(); mec >= 0 && r >= 2 && r - 1 < 10) {  // the Mecatol watch (round r - 1 just ended)
        const SystemState& ms = g.systems[size_t(mec)];
        int owner = -1, gf = 0;
        bool ships = false;
        for (int16_t pl : ms.planets)
            if (g.planets[size_t(pl)].owner >= 0) {
                owner = g.planets[size_t(pl)].owner;
                gf = g.planets[size_t(pl)].units[size_t(owner)].groundForces();
            }
        for (int q = 0; q < np; ++q)
            if (ms.space[size_t(q)].nonFighterShips() > 0) ships = true;
        const bool guarded = owner >= 0 && (ms.space[size_t(owner)].nonFighterShips() > 0 || gf >= 2);
        const bool open = !ms.custodians && !guarded;
        mecN_[r - 1] += 1;
        mecShips_[r - 1] += ships;
        mecHeld_[r - 1] += owner >= 0;
        mecGuard_[r - 1] += guarded;
        mecOpen_[r - 1] += open;
        if (ships && !mecShipsSeen_) {
            mecShipsSeen_ = true;
            mecFirstShips_ += r - 1;
            mecFirstShipsN_ += 1;
        }
        if (owner >= 0 && mecOwner_ >= 0 && owner != mecOwner_) mecChanges_ += 1;
        if (owner >= 0) mecOwner_ = owner;
        if (open && openSince_ < 0) openSince_ = r - 1;
        if (!open && openSince_ >= 0) {
            openSpells_ += 1;
            openRounds_ += (r - 1) - openSince_;
            openSince_ = -1;
        }
    }
    // spend in the round just over
    if (r >= 2 && r - 1 < 10)
        for (int p = 0; p < np; ++p) {
            spentR_[r - 1] += spentRound_[size_t(p)];
            spentRN_[r - 1] += 1;
            if (spentRound_[size_t(p)] > 0) producedR_[r - 1] += 1;
            spentRound_[size_t(p)] = 0;
        }
    for (int p = 0; p < np && r <= 10; ++p) {
        vpAt_[r] += g.players[size_t(p)].vp;
        vpAtN_[r] += 1;
    }
    // objectives scored in the round just over
    if (r >= 2 && r - 1 < 10)
        for (int p = 0; p < np; ++p) {
            const int pub = countScored(g, p, false), sec = countScored(g, p, true);
            pubR_[r - 1] += pub - pubPrev_[size_t(p)];
            secR_[r - 1] += sec - secPrev_[size_t(p)];
            if (pub > pubPrev_[size_t(p)]) {
                scoredPubR_[r - 1] += 1;
                ++pubRounds_[size_t(p)];
            }
            seenR_[r - 1] += 1;
            pubPrev_[size_t(p)] = pub;
            secPrev_[size_t(p)] = sec;
        }
    if (r == 4)
        for (int p = 0; p < np; ++p) vp4_[size_t(p)] = g.players[size_t(p)].vp;
    if (r == 6)
        for (int p = 0; p < np; ++p) vp6_[size_t(p)] = g.players[size_t(p)].vp;
    if (r < 2 || r > 6) return;
    for (int p = 0; p < np; ++p) {
        Acc a;
        int nonHome = 0;
        for (size_t x = 0; x < g.systems.size(); ++x)
            for (int16_t pl : g.systems[x].planets) {
                const int o = g.planets[size_t(pl)].owner;
                if (o == p && g.systems[x].home < 0) ++nonHome;
                if (slice_[x] != p) continue;
                a.n += 1;
                const int band = std::min(3, std::max(1, near_[x]));
                sliceBy_[r][band] += 1;
                if (o == p) a.own += 1;
                else if (o >= 0) a.other += 1;
                else {
                    a.none += 1;
                    openBy_[r][band] += 1;
                    if (g.systems[x].d().anomalies) openAnom_[r] += 1;
                    if (r == 3 && std::getenv("TI4_TEMPODETAIL")) {  // test only (0.58.2): why a slice planet is still free
                        int inSys = 0, mine = 0;
                        for (int16_t q : g.systems[x].planets) {
                            ++inSys;
                            if (g.planets[size_t(q)].owner == p) ++mine;
                        }
                        int gfHome = 0, ships = 0;
                        for (const PlanetState& ps : g.planets) gfHome += ps.units[size_t(p)].groundForces();
                        for (const SystemState& sy : g.systems) ships += sy.space[size_t(p)].count(UnitType::Carrier);
                        std::fprintf(stderr, "FREE r3 p%d %-14s %-10s res+inf %d anomaly %d planets %d mine-there %d near %d | gf %d carriers %d tactic %d\n",
                                     p, kFactions[g.players[size_t(p)].faction].key, kPlanets[g.planets[size_t(pl)].def].name,
                                     planetResources(g, pl) + planetInfluence(g, pl), int(g.systems[x].d().anomalies != 0), inSys, mine,
                                     near_[x], gfHome, ships, int(g.players[size_t(p)].tactic));
                    }
                }
            }
        atR_[r].n += a.n;
        atR_[r].own += a.own;
        atR_[r].other += a.other;
        atR_[r].none += a.none;
        if (r == 3) {
            seats_ += 1;
            if (nonHome == 0) zeroAt3_ += 1;
            sliceOwn3_[size_t(p)] = int(a.own);
            sliceN3_[size_t(p)] = int(a.n);
            sliceNone3_[size_t(p)] = int(a.none);
        }
    }
    for (size_t x = 0; x < g.systems.size(); ++x) {
        if (slice_[x] != -2) continue;
        for (int16_t pl : g.systems[x].planets) {
            const int o = g.planets[size_t(pl)].owner;
            eqTotal_[r] += 1;
            if (o >= 0) eqOwned_[r] += 1;
            if (o >= 0 && std::find(tied_[x].begin(), tied_[x].end(), o) != tied_[x].end()) eqTied_[r] += 1;
            if (r == 3 && o >= 0) ++eqHeld3_[size_t(o)];
        }
    }
}

void TempoScore::actionPhaseOver(const Game& g) {
    const int r = g.round;
    for (int p = 0; p < g.numPlayers; ++p) {
        int total = 0, ready = 0;
        for (int x : controlledPlanets(g, p)) {
            const int res = planetResources(g, x);
            total += res;
            if (!g.planets[size_t(x)].exhausted) ready += res;
        }
        if (total <= 0) continue;
        const double f = double(ready) / total;
        if (std::getenv("TI4_TEMPODEBUG")) {  // (test only: per player and round)
            std::fprintf(stderr, "RESLEFT r%d %s %d %d", r, kFactions[g.players[size_t(p)].faction].key, ready, total);
            if (!std::strcmp(kFactions[g.players[size_t(p)].faction].key, "obsidian"))
                for (int x : controlledPlanets(g, p))
                    if (!g.planets[size_t(x)].exhausted && planetResources(g, x) > 0) std::fprintf(stderr, " [%s %d]", kPlanets[size_t(g.planets[size_t(x)].def)].key, planetResources(g, x));
            std::fprintf(stderr, "\n");
        }
        if (r < 10) {
            unspentR_[r] += f;
            unspentRN_[r] += 1;
        }
        unspentSum_[size_t(p)] += f;
        unspentN_[size_t(p)] += 1;
    }
}

void TempoScore::step(const Game& g) {
    if (!started_) {
        if (g.phase == Phase::Setup) return;
        startGame(g);
    }
    // 0.55.0 (the endgame): players who could reach the goal at a round's scoring, seen at its action phase's start
    if (g.phase == Phase::Action && g.round != threatRound_) {
        if (!threats_.empty()) threatMissed_ += double(threats_.size());  // (the last round's threats: the game went on)
        threats_.clear();
        threatRound_ = g.round;
        for (int q = 0; q < g.numPlayers; ++q)
            if (!g.players[size_t(q)].eliminated && aiReach(g, q, -1) >= float(g.vpGoal) - 0.4f) threats_.push_back(q);
        threatRounds_ += threats_.empty() ? 0 : 1;
    }
    if (g.round != lastRound_ && g.phase == Phase::Strategy) {
        lastRound_ = g.round;
        roundStart(g);
    }
    if (lastPhase_ == Phase::Action && g.phase != Phase::Action) actionPhaseOver(g);
    for (int p = 0; p < g.numPlayers; ++p) {  // betrayal objectives: given, completed, how long it took
        const Personality& pe = g.players[size_t(p)].pers;
        auto& lv = betrayByLevel_[levelName(levelOf(g, p))];
        if (pe.betrayals > betrayCount_[size_t(p)]) {
            betrayCount_[size_t(p)] = pe.betrayals;
            betrayDone_ += 1;
            lv.second += 1;
            if (betrayFrom_[size_t(p)] >= 0) betrayRounds_ += g.round - betrayFrom_[size_t(p)];
        }
        if (pe.target >= 0 && betrayFrom_[size_t(p)] < 0) {
            betrayFrom_[size_t(p)] = g.round;
            betrayGiven_ += 1;
            lv.first += 1;
        } else if (pe.target < 0) {
            betrayFrom_[size_t(p)] = -1;
        }
    }
    // custodians chances: at the start of each turn in rounds 1-3 while the token is there
    if (g.phase == Phase::Action && g.activePlayer >= 0 && g.activePlayer != lastTurnP_ && !g.custodiansRemoved && g.round <= 3 &&
        g.mecatol() >= 0) {
        const int r = g.round, p = g.activePlayer;
        const bool land = aiCanLandOnMecatol(g, p), rich = spendable(g, p, true) >= 6;
        turnsR_[r] += 1;
        canLandR_[r] += land;
        richR_[r] += rich;
        bothR_[r] += land && rich;
    }
    if (g.phase == Phase::Action) lastTurnP_ = g.activePlayer;
    else lastTurnP_ = -1;
    lastPhase_ = g.phase;
    // 0.53.0 operations: Styx on the board, taken from its holder; home planets taken
    if (homeLast_.size() < g.planets.size()) homeLast_.resize(g.planets.size(), -1), homeOwner_.resize(g.planets.size(), -1);
    for (size_t x = 0; x < g.planets.size(); ++x) {
        const int o = g.planets[x].owner;
        if (homeOwner_[x] >= 0 && o != homeLast_[x]) {
            if (o >= 0 && o != homeOwner_[x] && homeLast_[x] == homeOwner_[x]) homeTaken_ += 1;
            homeLast_[x] = o;
        }
    }
    if (styxPl_ < 0 || styxPl_ >= int(g.planets.size())) styxPl_ = aiStyxPlanet(g);
    if (styxPl_ >= 0) {
        if (!styxSeen_) {
            styxSeen_ = true;
            styxGames_ += 1;
            styxAppear_ += g.round;
        }
        const int o = g.planets[size_t(styxPl_)].owner;
        if (o >= 0 && !styxOwned_) {
            styxOwned_ = true;
            styxRound_ += g.round;
            styxFirst_ += 1;
        }
        if (o >= 0 && styxHolder_ >= 0 && o != styxHolder_) styxTaken_ += 1;
        if (o >= 0) styxHolder_ = o;
    }
    if (!mecatolTaken_ && g.mecatol() >= 0)
        for (int16_t pl : g.systems[size_t(g.mecatol())].planets)
            if (g.planets[size_t(pl)].owner >= 0) {
                mecatolTaken_ = true;
                mecRound_ += g.round;
                mecGames_ += 1;
                if (g.round <= 3) mecBy3_ += 1;
                if (g.round <= 4) mecBy4_ += 1;
            }
}

void TempoScore::endGame(const Game& g) {
    if (!started_) return;
    ++games_;
    for (int q : threats_) {  // (the final round's threats: did one of them win?)
        if (q == g.winner) threatWon_ += 1;
        else threatMissed_ += 1;
    }
    if (g.winner >= 0 && std::find(threats_.begin(), threats_.end(), int(g.winner)) == threats_.end()) surpriseWins_ += 1;
    threats_.clear();
    threatRound_ = -1;
    if (styxSeen_ && styxHit_) styxAttacked_ += 1;
    for (int p = 0; p < g.numPlayers; ++p) {
        const std::string arch = archName(g, p);
        Group* gs[4] = {&byArch_[arch.empty() ? "(none)" : arch], &byFaction_[kFactions[g.players[size_t(p)].faction].key],
                        &byLevel_[levelName(levelOf(g, p))], &bySeat_[std::string("seat ") + char('0' + p)]};
        for (Group* x : gs) {
            x->seats += 1;
            x->vpEnd += g.players[size_t(p)].vp;
            if (g.winner == p) x->wins += 1;
            x->vp4 += vp4_[size_t(p)];
            x->vp6 += vp6_[size_t(p)];
            x->sliceOwn3 += sliceOwn3_[size_t(p)];
            x->sliceN3 += sliceN3_[size_t(p)];
            x->sliceNone3 += sliceNone3_[size_t(p)];
            x->spent13 += spent_[size_t(p)];
            x->unspent += unspentSum_[size_t(p)];
            x->unspentN += unspentN_[size_t(p)];
            x->pubRounds += pubRounds_[size_t(p)];
            x->eq3 += eqHeld3_[size_t(p)];
        }
    }
    started_ = false;
}

void TempoScore::printGroups(std::FILE* f, const char* title, const std::map<std::string, Group>& m) const {
    std::fprintf(f, "  %s:  seats  win%%  slice own/free r3  equi r3  spent r1-3  res left  pub rounds  vp r4  vp r6  vp end\n", title);
    for (const auto& kv : m) {
        const Group& x = kv.second;
        if (x.seats <= 0) continue;
        std::fprintf(f, "    %-12s %5.0f %5.1f      %3.0f%% / %3.0f%%  %7.2f  %10.1f  %7.0f%%  %10.2f  %5.2f  %5.2f  %6.2f\n", kv.first.c_str(), x.seats,
                     100 * x.wins / x.seats, x.sliceN3 ? 100 * x.sliceOwn3 / x.sliceN3 : 0.0, x.sliceN3 ? 100 * x.sliceNone3 / x.sliceN3 : 0.0,
                     x.eq3 / x.seats, x.spent13 / x.seats, x.unspentN ? 100 * x.unspent / x.unspentN : 0.0, x.pubRounds / x.seats,
                     x.vp4 / x.seats, x.vp6 / x.seats, x.vpEnd / x.seats);
    }
}

void TempoScore::report(std::FILE* f) const {
    if (!games_) return;
    std::fprintf(f, "tempo scorecard (%d games):\n", games_);
    for (int r = 2; r <= 6; ++r) {
        const Acc& a = atR_[r];
        if (a.n <= 0) continue;
        std::fprintf(f,
                     "  start of round %d: slice planets held by the slice's player %3.0f%%, by others %3.0f%%, unclaimed %3.0f%%; "
                     "equidistant planets held %3.0f%% (%3.0f%% by a tied player)\n",
                     r, 100 * a.own / a.n, 100 * a.other / a.n, 100 * a.none / a.n, eqTotal_[r] ? 100 * eqOwned_[r] / eqTotal_[r] : 0.0,
                     eqTotal_[r] ? 100 * eqTied_[r] / eqTotal_[r] : 0.0);
    }
    for (int r = 2; r <= 4; ++r)
        if (atR_[r].none > 0)
            std::fprintf(f, "    unclaimed at round %d by moves from home: 1: %.0f%% of those planets, 2: %.0f%%, 3+: %.0f%%; in anomalies %.0f%% of the unclaimed"
                            " (slice planets 1 / 2 / 3+ moves away: %.0f / %.0f / %.0f)\n", r,
                         sliceBy_[r][1] ? 100 * openBy_[r][1] / sliceBy_[r][1] : 0.0, sliceBy_[r][2] ? 100 * openBy_[r][2] / sliceBy_[r][2] : 0.0,
                         sliceBy_[r][3] ? 100 * openBy_[r][3] / sliceBy_[r][3] : 0.0, 100 * openAnom_[r] / atR_[r].none,
                         double(sliceBy_[r][1]), double(sliceBy_[r][2]), double(sliceBy_[r][3]));
    std::fprintf(f, "  no planet outside home at the start of round 3: %.1f%% of players\n", seats_ ? 100 * zeroAt3_ / seats_ : 0.0);
    std::fprintf(f, "  Mecatol Rex first taken in round %.2f (%.0f%% of games; by round 3 %.0f%%, by round 4 %.0f%%)\n",
                 mecGames_ ? mecRound_ / mecGames_ : 0.0, 100 * mecGames_ / games_, 100 * mecBy3_ / games_, 100 * mecBy4_ / games_);
    std::fprintf(f, "  Mecatol watch: first ships there after round %.2f; owner changes per game %.2f; left open (custodians gone, unguarded) "
                    "%.0f times, %.2f rounds each\n",
                 mecFirstShipsN_ ? mecFirstShips_ / mecFirstShipsN_ : 0.0, games_ ? mecChanges_ / games_ : 0.0, openSpells_,
                 openSpells_ ? openRounds_ / openSpells_ : 0.0);
    for (int r = 1; r <= 3; ++r)
        if (turnsR_[r] > 0)
            std::fprintf(f, "    round %d turn starts with the custodians there: could land %.0f%%, 6 influence %.0f%%, both %.0f%%\n", r,
                         100 * canLandR_[r] / turnsR_[r], 100 * richR_[r] / turnsR_[r], 100 * bothR_[r] / turnsR_[r]);
    std::fprintf(f, "    loaded ship in / next to Mecatol Rex (custodians still there), %% of players: start of round 2 %.0f%%, 3 %.0f%%, 4 %.0f%%\n",
                 stagedN_[2] ? 100 * stagedR_[2] / stagedN_[2] : 0.0, stagedN_[3] ? 100 * stagedR_[3] / stagedN_[3] : 0.0,
                 stagedN_[4] ? 100 * stagedR_[4] / stagedN_[4] : 0.0);
    std::fprintf(f, "    end of round:     ");
    for (int r = 1; r < 9; ++r) std::fprintf(f, "%6d", r);
    std::fprintf(f, "\n    ships there %%:    ");
    for (int r = 1; r < 9; ++r) std::fprintf(f, "%6.0f", mecN_[r] ? 100 * mecShips_[r] / mecN_[r] : 0.0);
    std::fprintf(f, "\n    held %%:           ");
    for (int r = 1; r < 9; ++r) std::fprintf(f, "%6.0f", mecN_[r] ? 100 * mecHeld_[r] / mecN_[r] : 0.0);
    std::fprintf(f, "\n    held, guarded %%:  ");
    for (int r = 1; r < 9; ++r) std::fprintf(f, "%6.0f", mecN_[r] ? 100 * mecGuard_[r] / mecN_[r] : 0.0);
    std::fprintf(f, "\n    open %%:           ");
    for (int r = 1; r < 9; ++r) std::fprintf(f, "%6.0f", mecN_[r] ? 100 * mecOpen_[r] / mecN_[r] : 0.0);
    std::fprintf(f, "\n");
    if (saarN_[1] > 0) {
        std::fprintf(f, "  Saar floating factories at the end of round:");
        for (int r = 1; r < 8; ++r)
            if (saarN_[r] > 0)
                std::fprintf(f, "  r%d %.1f (%.1f out, ball %.0f%%)", r, saarFF_[r] / saarN_[r], saarOut_[r] / saarN_[r], 100 * saarBall_[r] / saarN_[r]);
        std::fprintf(f, "\n");
    }
    std::fprintf(f, "  per player per round:  round  spent on units (built any)  resources left ready  publics  secrets  scored a public  VP after\n");
    for (int r = 1; r < 10; ++r) {
        if (!seenR_[r] && !spentRN_[r]) continue;
        std::fprintf(f, "                         %5d  %14.1f (%4.0f%%)  %19.0f%%  %7.2f  %7.2f  %14.0f%%  %11.2f\n", r, spentRN_[r] ? spentR_[r] / spentRN_[r] : 0.0,
                     spentRN_[r] ? 100 * producedR_[r] / spentRN_[r] : 0.0,
                     unspentRN_[r] ? 100 * unspentR_[r] / unspentRN_[r] : 0.0, seenR_[r] ? pubR_[r] / seenR_[r] : 0.0,
                     seenR_[r] ? secR_[r] / seenR_[r] : 0.0, seenR_[r] ? 100 * scoredPubR_[r] / seenR_[r] : 0.0,
                     vpAtN_[r + 1] ? vpAt_[r + 1] / vpAtN_[r + 1] : 0.0);
    }
    std::fprintf(f, "  betrayal objectives: %.0f given, %.0f completed (%.0f%%), %.2f rounds to complete;", betrayGiven_, betrayDone_,
                 betrayGiven_ ? 100 * betrayDone_ / betrayGiven_ : 0.0, betrayDone_ ? betrayRounds_ / betrayDone_ : 0.0);
    for (const auto& kv : betrayByLevel_)
        if (kv.second.first > 0) std::fprintf(f, " %s %.0f/%.0f", kv.first.c_str(), kv.second.second, kv.second.first);
    std::fprintf(f, "\n");
    // 0.53.0 operations scorecard
    auto fight = [&](const char* name, const Fight& x) {
        if (x.n <= 0) return;
        std::fprintf(f, "    %-22s %6.0f fights (%.2f a game), attacker won %3.0f%%, attacker / defender ships %.2f\n", name, x.n, x.n / games_,
                     100 * x.won / x.n, x.ratio / x.n);
    };
    std::fprintf(f, "  operations: space combats started\n");
    fight("all", fAll_);
    fight("big (defender 4+ ships)", fBig_);
    fight("a player's home system", fHome_);
    fight("Mecatol Rex", fMecatol_);
    fight("Styx", fStyx_);
    fight("neutral guards", fNeutral_);
    std::fprintf(f, "    home planets taken from their owner: %.2f a game\n", homeTaken_ / games_);
    if (threatWon_ + threatMissed_ > 0)
        std::fprintf(f, "  endgame: players who could win at a round's scoring (seen at its start): %.0f, won then %.0f%%; winners nobody saw coming %.0f%%\n",
                     threatWon_ + threatMissed_, 100 * threatWon_ / (threatWon_ + threatMissed_), 100 * surpriseWins_ / games_);
    if (styxGames_ > 0)
        std::fprintf(f, "    Styx on the board in %.0f games (from round %.2f): first held in round %.2f (%.0f%% of those games); a holder attacked there in %.0f%%; "
                        "changed hands %.2f times a game\n",
                     styxGames_, styxAppear_ / styxGames_, styxFirst_ ? styxRound_ / styxFirst_ : 0.0, 100 * styxFirst_ / styxGames_,
                     100 * styxAttacked_ / styxGames_, styxTaken_ / styxGames_);
    printGroups(f, "by difficulty", byLevel_);
    printGroups(f, "by archetype", byArch_);
    printGroups(f, "by faction", byFaction_);
    printGroups(f, "by seat", bySeat_);
}

}  // namespace ti
