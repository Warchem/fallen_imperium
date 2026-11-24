// Players' reports on GitHub, checked on the engine (TI4_UITEST_ONLY=issues; 0.80.1).
// Issue #3 "Vuil'raith cannot build with their own captured units": the Vuil'raith with 2 dreadnoughts on the board and 3 of
// their own dreadnoughts captured by The Terror Between (on their faction sheet) produce a dreadnought at home - Amalgamation
// gives one of them back to produce it, so the production must be allowed.
#include <cstdio>
#include <string>
#include <vector>

#include "engine/data.h"
#include "engine/engine.h"
#include "engine/query.h"
#include "engine/rules.h"
#include "engine/state.h"

using namespace ti;

int issueTests(const std::string& outDir) {
    (void)outDir;
    int problems = 0;
    for (int fixes : {34, 35}) {
        Setup s;
        s.players = 3;
        s.human = -1;
        s.autoplay = true;
        s.seed = 77;
        s.draft = 0;
        s.fixes = fixes;
        s.factions = {findFaction("cabal"), findFaction("sol"), findFaction("hacan")};
        Engine e;
        e.start(s);
        Game& g = e.g();
        const int p = 0;
        const int home = g.players[p].homeSystem;
        g.systems[size_t(home)].space[p].add(UnitType::Dreadnought, 2 - g.systems[size_t(home)].space[p].count(UnitType::Dreadnought));
        g.systems[size_t(home)].tokens = uint16_t(g.systems[size_t(home)].tokens | (1u << p));  // (no taking them back off the board)
        for (int i = 0; i < 3; ++i) g.players[p].captured.push_back(int16_t(p * 16 + int(UnitType::Dreadnought)));
        std::vector<int> plan(kUnitTypes + 1, 0);
        plan[int(UnitType::Dreadnought)] = 1;
        plan[kUnitTypes] = -1;
        const std::string err = checkProducePlan(g, p, home, plan, 20, 0, false, g.fixes >= 32);
        std::printf("issue #3 (fixes %d): dreadnoughts on the board %d, captured own %d, in reinforcements %d -> produce 1: %s\n", fixes,
                    unitsOnBoard(g, p, UnitType::Dreadnought), capturedFrom(g, p, UnitType::Dreadnought),
                    unitsAvailable(g, p, UnitType::Dreadnought), err.empty() ? "allowed" : ("refused - " + err).c_str());
        if (fixes >= 35 && !err.empty()) ++problems;
    }
    std::printf("issues: %d problems\n", problems);
    return problems == 0 ? 1 : 0;
}
