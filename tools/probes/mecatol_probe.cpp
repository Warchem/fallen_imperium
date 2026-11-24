// Build: g++ -std=c++17 -O2 -Isrc -Ithird_party tools/probes/mecatol_probe.cpp $(find build/host/obj -name "*.o" ! -path "*/main.o") -o probe -ldl -lm -lpthread
// (after make host). NOTE: the state is edited directly, so the A.I. level's look-ahead (copies replay setup + answers) is not valid here.
// Mecatol Rex takeover probe (side quest #777): does a Competitive bot see a threat to Mecatol Rex, or a chance at it?
// Usage: probe VARIANT N SEED   (VARIANT 1 defend, 2 weak holder, 3 leader on Mecatol)
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "ai/ai.h"
#include "engine/engine.h"
#include "engine/query.h"
using namespace ti;

static int adjFree(const Game& g, int mec, int avoid, int nearHome) {
    int best = -1, bd = 99;
    for (int16_t a : g.systems[size_t(mec)].adj) {
        if (a == avoid || g.systems[size_t(a)].home >= 0 || g.systems[size_t(a)].offBoard || isAnomaly(g, a) || g.systems[size_t(a)].isHyperlane()) continue;
        const int d = nearHome >= 0 ? hexDistance(g.systems[size_t(a)].pos, g.systems[size_t(nearHome)].pos) : 0;
        if (d < bd) bd = d, best = a;
    }
    return best;
}

int main(int argc, char** argv) {
    const int variant = argc > 1 ? std::atoi(argv[1]) : 1;
    const int N = argc > 2 ? std::atoi(argv[2]) : 10;
    const uint32_t seed0 = argc > 3 ? uint32_t(std::atoi(argv[3])) : 500;
    int games = 0, qReinforce = 0, eAttack = 0, eAttackFirst = 0, qPreempt = 0, qHolds = 0, eHolds = 0, otherHolds = 0, thirdAttack = 0;
    int qFirstTurnMec = 0;
    for (int k = 0; k < N; ++k) {
        Engine e;
        Setup s;
        s.players = 6;
        s.human = -1;
        s.autoplay = true;
        s.seed = seed0 + uint32_t(k) * 7919u;
        s.aiLevel = 3;
        s.levels.assign(6, 3);
        const int qLevel = std::getenv("PROBE_QLEVEL") ? std::atoi(std::getenv("PROBE_QLEVEL")) : 3;
        s.levels[0] = qLevel;  // (seat 0 is the holder: homes are equidistant, the tie goes to seat 0)
        e.start(s);
        Rng rng(11 + k);
        bool set = false;
        int Q = 0, E = 1, mec = -1, mecPl = -1, qFleet = -1, eFleet = -1;
        std::vector<std::string> acts;  // "who:system" activations in round 3
        int steps = 0;
        for (; e.waiting() && steps < 400000; ++steps) {
            const Game& g = e.game();
            if (!set && g.phase == Phase::Strategy && g.round == 3) {
                Game& m = e.g();
                mec = m.mecatol();
                if (mec < 0) break;
                mecPl = m.systems[size_t(mec)].planets.empty() ? -1 : m.systems[size_t(mec)].planets[0];
                // the holder: the seat whose home is nearest Mecatol; the threat: the next nearest
                std::vector<std::pair<int, int>> byDist;
                for (int q = 0; q < m.numPlayers; ++q)
                    byDist.push_back({hexDistance(m.systems[size_t(m.players[size_t(q)].homeSystem)].pos, m.systems[size_t(mec)].pos), q});
                std::sort(byDist.begin(), byDist.end());
                Q = byDist[0].second, E = byDist[1].second;
                qFleet = adjFree(m, mec, -1, m.players[size_t(Q)].homeSystem);
                eFleet = adjFree(m, mec, qFleet, m.players[size_t(E)].homeSystem);
                if (qFleet < 0 || eFleet < 0 || mecPl < 0) break;
                m.custodiansRemoved = true;
                m.systems[size_t(mec)].custodians = false;
                // clear Mecatol and its neighbours of everyone's ships and the planet of everyone's ground forces
                std::vector<int> area = {mec};
                for (int16_t a : m.systems[size_t(mec)].adj) area.push_back(a);
                for (int a : area)
                    if (m.systems[size_t(a)].home < 0)
                        for (int q = 0; q < kSides; ++q) m.systems[size_t(a)].space[size_t(q)] = Force();
                for (int q = 0; q < kSides; ++q) m.planets[size_t(mecPl)].units[size_t(q)] = Force();
                for (int16_t pl : m.systems[size_t(mec)].planets) m.planets[size_t(pl)].owner = int8_t(Q);
                m.planets[size_t(mecPl)].units[size_t(Q)].add(UnitType::Infantry, 2);
                m.systems[size_t(mec)].space[size_t(Q)].add(UnitType::Destroyer, 1);
                if (variant == 1 || variant == 3) {
                    m.systems[size_t(qFleet)].space[size_t(Q)].add(UnitType::Dreadnought, 3);
                    m.systems[size_t(qFleet)].space[size_t(Q)].add(UnitType::Cruiser, 2);
                }
                m.systems[size_t(eFleet)].space[size_t(E)].add(UnitType::Dreadnought, 4);
                m.systems[size_t(eFleet)].space[size_t(E)].add(UnitType::Carrier, 1);
                m.systems[size_t(eFleet)].space[size_t(E)].add(UnitType::Infantry, 4);
                if (variant == 3) {  // the holder leads: 3 from the goal; a third fleet near too
                    m.players[size_t(Q)].vp = int8_t(m.vpGoal - 3);
                    const int T = byDist[2].second;
                    const int tFleet = adjFree(m, mec, eFleet == qFleet ? -1 : eFleet, m.players[size_t(T)].homeSystem);
                    if (tFleet >= 0 && tFleet != qFleet && tFleet != eFleet) {
                        m.systems[size_t(tFleet)].space[size_t(T)].add(UnitType::Dreadnought, 3);
                        m.systems[size_t(tFleet)].space[size_t(T)].add(UnitType::Carrier, 1);
                        m.systems[size_t(tFleet)].space[size_t(T)].add(UnitType::Infantry, 3);
                    }
                }
                set = true;
            }
            if (set && (g.phase == Phase::Status || (g.round > 3))) break;
            const Decision& d = e.decision();
            aiSetEngine(&e);
            Answer a = aiAnswer(g, d, rng);
            aiSetEngine(nullptr);
            if (set && d.tag == Tag::Activate && !a.picks.empty()) acts.push_back(std::to_string(d.player) + ":" + std::to_string(a.picks[0]));
            std::string why;
            if (!e.answer(a, &why)) {
                std::fprintf(stderr, "game %d: rejected %s\n", k, why.c_str());
                break;
            }
            e.takeEvents();
        }
        if (!set) continue;
        ++games;
        const Game& g = e.game();
        // who activated Mecatol / the threat's staging system, in order
        int firstMec = -1;
        bool qMec = false, qHitE = false, eMec = false, third = false;
        int qTurns = 0;
        for (const std::string& x : acts) {
            const int who = std::atoi(x.c_str()), sys = std::atoi(x.c_str() + x.find(':') + 1);
            if (who == Q) ++qTurns;
            if (sys == mec && firstMec < 0) firstMec = who;
            if (sys == mec && who == Q) {
                qMec = true;
                if (qTurns == 1) ++qFirstTurnMec;
            }
            if (sys == mec && who == E) eMec = true;
            if (sys == mec && who != Q && who != E) third = true;
            if (sys == eFleet && who == Q) qHitE = true;
        }
        qReinforce += qMec && (firstMec == Q);
        eAttack += eMec;
        eAttackFirst += firstMec == E;
        qPreempt += qHitE;
        thirdAttack += third;
        const int holder = g.planets[size_t(mecPl)].owner;
        qHolds += holder == Q;
        eHolds += holder == E;
        otherHolds += holder != Q && holder != E;
        std::printf("game %d: Q=%d E=%d acts:", k, Q, E);
        for (const std::string& x : acts) {
            const int sys = std::atoi(x.c_str() + x.find(':') + 1);
            std::printf(" %s%s", x.substr(0, x.find(':')).c_str(), sys == mec ? "@MR" : sys == eFleet ? "@Efleet" : sys == qFleet ? "@Qfleet" : "");
        }
        std::printf(" | holder after r3: %d\n", holder);
    }
    std::printf("variant %d: %d games | Q into Mecatol before anyone: %d (on its first turn %d) | Q strikes E's fleet: %d | E attacks Mecatol: %d (first %d) | a third player attacks: %d | holder after round 3: Q %d, E %d, other %d\n",
                variant, games, qReinforce, qFirstTurnMec, qPreempt, eAttack, eAttackFirst, thirdAttack, qHolds, eHolds, otherHolds);
}
