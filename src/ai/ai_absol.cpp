// Absol's Agendas (0.81.0) - the computer players' side of the module (rules in engine/rules_absol.cpp).
//  - absolOutcomeValue: what each outcome of a card does for a player (the "victory point" scale of aiOutcomeValue: 1 point = 3,
//    1 trade good ~ 0.3, an action card ~ 0.6, a command token ~ 0.8, a technology ~ 1.5);
//  - absolVoterReward: what voting a certain way (or abstaining) brings by itself - "the players who voted For gain ...", "players
//    who voted Against or abstained must ..." (ai_votes.cpp worth() adds it);
//  - absolTopReward: what being "the player who cast the most votes" for the winning outcome brings (41 of the 60 cards) - the vote
//    plan casts enough to be on top when it pays (aiVotePlan);
//  - aiAbsolAnswer: the module's own questions (Tag::AbsolTie / AbsolNote / AbsolGiveTo / AbsolChoice, Decision::value = AbsolAsk).
// AI only: every answer still goes through the rules' checks.
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>

#include "ai/ai_internal.h"
#include "engine/deal.h"
#include "engine/personality.h"
#include "engine/query.h"

namespace ti {

namespace {
constexpr float kTG = 0.3f, kAC = 0.6f, kTok = 0.8f, kTech = 1.5f, kVPv = 3.f, kNote = 0.6f;

bool ab(int a, const char* k) { return a >= 0 && a < kAgendaCount && !std::strcmp(kAgendas[a].key, k); }
Answer one(int id) {
    Answer a;
    a.picks.push_back(id);
    return a;
}
int leaderOf(const Game& g, int exclude) {
    int best = -1;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != exclude && !g.players[size_t(q)].eliminated && (best < 0 || g.players[size_t(q)].vp > g.players[size_t(best)].vp)) best = q;
    return best;
}
int traitPlanets(const Game& g, int p, Trait t) { return controlledCountTrait(g, p, t); }
int planetsOf(const Game& g, int p) {
    int k = 0;
    for (int x : controlledPlanets(g, p))
        if (!isStation(g, x)) ++k;
    return k;
}
// how much p likes q (0 neutral, + friendly, - hostile)
float liking(const Game& g, int p, int q) {
    if (p == q) return 5.f;
    float v = 0.f;
    if (g.players[size_t(p)].pers.arch >= 0) v += (g.players[size_t(p)].pers.bf[size_t(q)] - 50.f) / 25.f;
    const int lead = leaderOf(g, -1);
    if (q == lead && g.players[size_t(q)].vp > g.players[size_t(p)].vp) v -= 1.f;
    return v;
}
float unitWorth(UnitType t) {
    switch (t) {
        case UnitType::Fighter: return 0.15f;
        case UnitType::Infantry: return 0.2f;
        case UnitType::Destroyer: return 0.3f;
        case UnitType::Cruiser: return 0.6f;
        case UnitType::Carrier: return 0.7f;
        case UnitType::Mech: return 0.6f;
        case UnitType::PDS: return 0.4f;
        case UnitType::Dreadnought: return 1.2f;
        case UnitType::SpaceDock: return 1.5f;
        case UnitType::WarSun: return 3.f;
        default: return 2.f;
    }
}
int mostTraitOther(const Game& g, int p, Trait t, int* count) {
    int best = -1, bv = -1;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && !g.players[size_t(q)].eliminated && traitPlanets(g, q, t) > bv) {
            bv = traitPlanets(g, q, t);
            best = q;
        }
    if (count) *count = bv;
    return best;
}
bool holdsMecatol(const Game& g, int p) {
    const int mec = g.mecatol();
    if (mec < 0) return false;
    for (int16_t x : g.systems[size_t(mec)].planets)
        if (g.planets[size_t(x)].owner == p) return true;
    return false;
}
int mechsAbility(const Game& g, int p) { return unitsOnBoard(g, p, UnitType::Mech); }
int shipsNearWormholes(const Game& g, int p) {
    int k = 0;
    for (size_t s = 0; s < g.systems.size(); ++s)
        if (wormholesOf(g, int(s)) & (kAlpha | kBeta)) k += g.systems[s].space[size_t(p)].nonFighterShips();
    return k;
}
}  // namespace

// ------------------------------------------------------------------ outcomes
float absolOutcomeValue(const Game& g, int p, int a, int o) {
    const Player& me = g.players[size_t(p)];
    const int n = g.numPlayers;
    const Elect el = kAgendas[a].elect;
    auto is = [&](const char* k) { return ab(a, k); };
    if (el == Elect::Player) {
        const bool self = o == p;
        const int lead = leaderOf(g, p);
        auto good = [&](float v) { return self ? v : (o == lead ? -0.5f * v : -0.2f * v) + 0.15f * liking(g, p, o); };
        if (is("absol_execution")) return self ? -2.f - 0.4f * float(me.actionCards.size()) : (o == lead ? 1.f : 0.3f) + 0.25f;
        if (is("absol_censure")) {  // a point, but a muzzle: no action cards, never the speaker
            const int goal = g.vpGoal > 0 ? g.vpGoal : 10;
            if (self) return me.vp >= goal - 1 ? 5.f : 1.6f;
            if (g.players[size_t(o)].vp >= goal - 1) return -5.f;
            return -0.6f - 0.25f * liking(g, p, o);
        }
        if (is("absol_arbiter") || is("absol_formation")) return good(1.4f);
        if (is("absol_warrant")) return good(1.6f);
        if (is("absol_grant")) return good(kTech);
        if (is("absol_secret")) return good(1.8f);
        if (is("absol_standardization")) {
            const int total = me.tactic + me.fleet + me.strategy;
            return self ? std::max(0.2f, (8 - total) * kTok * 0.6f) : good(0.5f);
        }
        if (is("absol_prophecy")) return good(0.4f + 0.05f * float(unitsOnBoard(g, p, UnitType::Fighter)));
        if (is("absol_minswar") || is("absol_minssci") || is("absol_minsantique") || is("absol_minscomm")) return good(1.5f);
        if (is("absol_minsindus")) return good(1.3f + 0.3f * float(unitsOnBoard(g, p, UnitType::SpaceDock)));
        if (is("absol_minspeace") || is("absol_minspolicy") || is("absol_minsexp")) return good(1.3f);
        return good(1.f);
    }
    if (el == Elect::NonHome || el == Elect::NonHomePlanet || el == Elect::Planet) {
        const PlanetState& ps = g.planets[size_t(o)];
        float units = 0.f;
        for (int q = 0; q < n; ++q)
            if (q == ps.owner) units += float(ps.units[size_t(q)].groundForces()) * 0.4f + float(ps.units[size_t(q)].structures()) * 0.6f;
        float worth = units;
        if (is("absol_disarmament")) {  // the ground forces go - the owner gets 1 / 2 trade goods for each
            const float tg = float(ps.units[size_t(std::max(0, int(ps.owner)))].count(UnitType::Infantry) +
                                   2 * ps.units[size_t(std::max(0, int(ps.owner)))].count(UnitType::Mech)) * kTG;
            worth = units - tg + 0.3f;  // (a planet left without defenders)
        }
        if (is("absol_redistribution")) worth += float(planetResources(g, o) + planetInfluence(g, o)) * 0.3f + 0.5f;
        if (ps.owner == p) return -worth;
        return ps.owner == leaderOf(g, p) ? worth : worth * 0.4f;
    }
    if (el == Elect::Law) return -aiOutcomeValue(g, p, o, 0) * 0.8f;  // (Miscount Disclosed: the law voted on again)
    if (el == Elect::ScoredSecret) return meetsObjective(g, p, o) && !hasScored(g, p, o) ? 2.f : -0.3f;
    if (el == Elect::StrategyCard) {
        static const float v[8] = {1.f, 0.5f, 1.5f, 0.8f, 1.f, 0.8f, 1.2f, 1.4f};
        return o >= 0 && o < 8 ? v[o] : 0.f;
    }
    if (el == Elect::UnitUpgrade) {  // that unit type loses Sustain Damage (everyone's); everyone may skip that upgrade's prerequisites
        const int ut = upgradeUnitType(o);
        const int mine = ut >= 0 && unitDef(g, p, UnitType(ut)).sustain ? unitsOnBoard(g, p, UnitType(ut)) : 0;
        return -0.15f * float(mine) + (me.hasTech(o) ? 0.f : 0.4f);
    }
    if (el != Elect::ForAgainst) return 0.f;
    const bool f = o == 0;
    const int mec = g.mecatol();
    (void)mec;
    if (is("absol_abolishment")) return f ? 0.1f : 0.05f;
    if (is("absol_affirmation")) {
        if (f) return holdsMecatol(g, p) ? 1.5f * float(n - 1) * kNote : -kNote;
        return (g.custodiansRemoved && g.custodiansBy == p ? -kVPv : 0.2f) + (holdsMecatol(g, p) ? -0.8f : 0.1f);
    }
    if (is("absol_agreement")) {
        if (f) return unitsForPlacement(g, p, UnitType::Mech) > 0 ? 0.6f : 0.f;
        return -0.4f * float(mechsAbility(g, p));
    }
    if (is("absol_articleswar")) {
        if (f) {
            float others = 0.f;
            for (int q = 0; q < n; ++q)
                if (q != p) others += float(unitsOnBoard(g, q, UnitType::Mech));
            return -0.3f * float(mechsAbility(g, p)) + 0.06f * others;
        }
        return 0.f;  // (the mechs lost: absolVoterReward)
    }
    if (is("absol_artifact")) {
        if (f) {
            float units = 0.f;
            if (mec >= 0) {
                for (int t = 0; t < kUnitTypes; ++t) units += float(g.systems[size_t(mec)].space[size_t(p)].n[t]) * unitWorth(UnitType(t));
                for (int16_t x : g.systems[size_t(mec)].adj) units += std::min(3, g.systems[size_t(x)].space[size_t(p)].ships()) * 0.6f;
            }
            return 0.4f * 2.f * kTech - 0.6f * units;
        }
        return holdsMecatol(g, p) ? kVPv : -0.3f;
    }
    if (is("absol_checks")) {
        if (f) return -0.4f;
        // the control tokens come off the objectives: points stay, objectives can be scored again - good for those who scored most
        int mine = int(me.scored.size()), most = 0;
        for (int q = 0; q < n; ++q) most = std::max<int>(most, int(g.players[size_t(q)].scored.size()));
        return 0.3f * float(mine) - 0.15f * float(most);
    }
    if (is("absol_conscription")) {
        if (f) return -0.2f - 0.03f * float(unitsOnBoard(g, p, UnitType::Infantry) + unitsOnBoard(g, p, UnitType::Fighter));
        int cnt = 0;
        const int most = mostTraitOther(g, p, Trait::Industrial, &cnt);
        return most >= 0 ? 0.1f : 0.f;
    }
    if (is("absol_conventionswar")) {
        if (f) {
            int big = unitsOnBoard(g, p, UnitType::Dreadnought) + unitsOnBoard(g, p, UnitType::WarSun);
            return 0.15f * float(traitPlanets(g, p, Trait::Cultural)) - 0.12f * float(big);
        }
        return 0.f;
    }
    if (is("absol_defenseact")) {
        const int pds = unitsOnBoard(g, p, UnitType::PDS);
        return f ? 0.3f + 0.05f * float(pds) : (pds ? -0.4f : 0.1f);
    }
    if (is("absol_equality")) {
        const float others = [&] {
            float t = 0.f;
            for (int q = 0; q < n; ++q)
                if (q != p) t += float(g.players[size_t(q)].tradeGoods);
            return t / float(std::max(1, n - 1));
        }();
        if (f) return -float(me.tradeGoods) * kTG + others * kTG * 0.3f + (3 - commodityValue(g, p)) * 0.25f;
        return 0.f;
    }
    if (is("absol_government")) {
        int mine = planetsOf(g, p), total = 0, cnt = 0;
        for (int q = 0; q < n; ++q)
            if (!g.players[size_t(q)].eliminated) {
                total += planetsOf(g, q);
                ++cnt;
            }
        return f ? (mine * cnt > total ? 0.6f : -0.4f) : 0.f;
    }
    if (is("absol_hyperlane")) {
        if (f) return 0.4f;
        return -std::min<int>(3, me.tradeGoods) * kTG;
    }
    if (is("absol_limits")) return f ? (g.speaker == p ? -0.3f : 0.1f) : -0.1f;
    if (is("absol_nexus")) return f ? 0.f : (holdsMecatol(g, p) ? -0.6f : 0.1f);
    if (is("absol_pax")) {
        if (f) return 0.1f;
        return 0.8f;  // (everyone may produce)
    }
    if (is("absol_recon")) return f ? 0.05f * float(shipsNearWormholes(g, p)) : 0.f;
    if (is("absol_regulations")) {
        if (f) return -std::max(0, me.fleet - 4) * kTok - 0.2f;
        return 0.1f;
    }
    if (is("absol_revolution")) return f ? -0.5f : 0.f;
    if (is("absol_sanctions")) return f ? -std::max(0, int(me.actionCards.size()) - 3) * 0.4f - 0.1f : 0.f;
    if (is("absol_shared")) return f ? 0.3f : 0.f;
    if (is("absol_sites")) return f ? -0.2f : 0.1f;
    if (is("absol_travelban")) {
        if (f) return -0.08f * float(shipsNearWormholes(g, p));
        return 0.f;
    }
    if (is("absol_constitution")) {
        float laws = 0.f;
        for (const Law& l : g.laws) laws += aiOutcomeValue(g, p, l.agenda, l.against ? 1 : 0) * 0.6f;
        return f ? -laws - 0.2f : 0.f;
    }
    if (is("absol_incentive")) {
        int most = 0;
        for (int q = 0; q < n; ++q) most = std::max<int>(most, g.players[size_t(q)].vp);
        if (f) return g.round <= 4 || me.vp < most - 1 ? 0.4f : 0.2f;
        return me.vp >= most - 1 && g.round >= 4 ? 0.5f : 0.2f;
    }
    if (is("absol_measures")) return 0.f;  // (all by how one voted)
    if (is("absol_mutiny")) return 0.f;
    if (is("absol_operations")) return f ? -kTok : -kTok * 0.8f;
    if (is("absol_reduction")) {
        if (f) {
            const int d = unitsOnBoard(g, p, UnitType::Dreadnought), c = unitsOnBoard(g, p, UnitType::Cruiser);
            float others = 0.f;
            for (int q = 0; q < n; ++q)
                if (q != p) others += float(unitsOnBoard(g, q, UnitType::Dreadnought)) * 0.6f;
            return -float(d) * (unitWorth(UnitType::Dreadnought) - 2 * kTG) - float((c + 1) / 2) * (unitWorth(UnitType::Cruiser) - kTG) +
                   others / float(std::max(1, n - 1));
        }
        return 0.f;
    }
    if (is("absol_seeds")) {
        int hi = 0, lo = 99;
        for (int q = 0; q < n; ++q)
            if (!g.players[size_t(q)].eliminated) {
                hi = std::max<int>(hi, g.players[size_t(q)].vp);
                lo = std::min<int>(lo, g.players[size_t(q)].vp);
            }
        if (f) return me.vp == hi ? kVPv : -0.8f;
        return me.vp == lo ? kVPv + 3 * kAC : -0.5f;
    }
    if (is("absol_spending")) {
        if (f) return float(planetsOf(g, p)) * kTG * 1.1f;
        return -float((me.tradeGoods + 1) / 2) * kTG;
    }
    if (is("absol_swords")) {
        if (f) {
            int k = 0;
            for (int x : controlledPlanets(g, p)) k += g.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry) / 2;
            return float(k) * (kTG - 0.2f);
        }
        return 0.f;
    }
    if (is("absol_whresearch")) {
        if (!f) return 0.f;
        for (size_t s = 0; s < g.systems.size(); ++s)
            if (wormholesOf(g, int(s)) && g.systems[s].space[size_t(p)].ships()) return 0.8f;
        return 0.f;
    }
    return 0.f;
}

// What voting `myVote` brings p by itself when `winner` wins (kAbstain = -100: abstained)
float absolVoterReward(const Game& g, int p, int a, int winner, int myVote) {
    const Player& me = g.players[size_t(p)];
    auto is = [&](const char* k) { return ab(a, k); };
    const bool votedFor = myVote == 0, votedAgainst = myVote == 1, abst = myVote == -100;
    const bool forWins = winner == 0, againstWins = winner == 1;
    float v = 0.f;
    const float notePenalty = me.notes.empty() ? 0.f : -kNote;
    if (is("absol_mutiny")) {
        if (forWins && votedFor) v += kVPv;
        if (againstWins && votedFor) v += -kVPv + notePenalty;
    } else if (is("absol_measures")) {
        if (forWins && votedFor) v += 2 * kAC;
        if (forWins && votedAgainst) v += kTok;
        if (againstWins && (votedAgainst || abst)) v -= kAC * float(me.actionCards.size());
    } else if (is("absol_articleswar")) {
        if (againstWins && (votedFor || abst)) v -= 0.4f * float(unitsOnBoard(g, p, UnitType::Mech));
    } else if (is("absol_conscription")) {
        if (againstWins && (votedAgainst || abst)) v -= 0.3f * float(traitPlanets(g, p, Trait::Industrial));
    } else if (is("absol_conventionswar")) {
        if (againstWins && (votedAgainst || abst)) v -= kAC * float(me.actionCards.size());
    } else if (is("absol_defenseact") || is("absol_limits") || is("absol_constitution")) {
        if (againstWins && (votedAgainst || abst)) v += notePenalty;
    } else if (is("absol_government")) {
        if (againstWins && (votedAgainst || abst)) v -= 0.3f * float(traitPlanets(g, p, Trait::Cultural)) + 0.2f;
    } else if (is("absol_recon") || is("absol_shared")) {
        if (againstWins && (votedAgainst || abst)) v -= kTok;
    } else if (is("absol_revolution")) {
        if (forWins && (votedFor || abst)) v -= kTech;
        if (againstWins && (votedAgainst || abst)) v -= 1.6f;
    } else if (is("absol_travelban")) {
        if (againstWins && votedAgainst) {
            int k = 0;
            for (size_t s = 0; s < g.systems.size(); ++s)
                if (hasUnits(g, p, int(s)))
                    for (uint8_t w = wormholesOf(g, int(s)); w; w &= uint8_t(w - 1)) ++k;
            v += float(k) * kTG;
        }
    } else if (is("absol_seeds")) {
        if (forWins && votedFor) v += 0.4f;  // (a note from the leader)
    } else if (is("absol_whresearch")) {
        if (forWins && votedFor) {
            int k = 0;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                const uint8_t w = wormholesOf(g, int(s));
                if (w & (kAlpha | kBeta)) k += std::min(2, g.systems[s].space[size_t(p)].nonFighterShips());
            }
            v -= 0.6f * float(k);
        }
        if (againstWins && (votedAgainst || abst)) v -= kTok;
    }
    return v;
}

// What being "the player who cast the most votes" for the winning outcome brings p
float absolTopReward(const Game& g, int p, int a, int winner) {
    const Player& me = g.players[size_t(p)];
    auto is = [&](const char* k) { return ab(a, k); };
    const Elect el = kAgendas[a].elect;
    const bool f = winner == 0;
    if (el == Elect::Player) {
        if (is("absol_censure") || is("absol_execution")) return winner != p ? 0.4f : 0.f;  // (the speaker token, a card)
        if (is("absol_warrant")) return winner != p ? 0.5f : 0.f;
        if (is("absol_grant")) return winner != p ? 0.5f : 0.f;
        if (is("absol_secret") || is("absol_standardization")) return winner != p ? kNote : 0.f;
        return 0.f;
    }
    if (el == Elect::ScoredSecret) return 1.f;
    if (el == Elect::UnitUpgrade) return kTech * 0.8f;
    if (el == Elect::StrategyCard) return 0.5f;
    if (el == Elect::Law) return 0.6f;
    if (el == Elect::NonHome) return 0.f;
    if (el != Elect::ForAgainst) return 0.f;
    if (is("absol_agreement")) return f ? 0.6f + kTok : float(planetsOf(g, p) / 2) * kTok * 0.5f;
    if (is("absol_articleswar")) return f ? 1.f : 0.6f * float(std::min(4, unitsForPlacement(g, p, UnitType::Mech)));
    if (is("absol_checks")) return f ? 0.5f : 0.f;
    if (is("absol_conscription")) return f ? kTG * float(traitPlanets(g, p, Trait::Industrial)) : -kNote;
    if (is("absol_conventionswar")) return f ? kTG * float(traitPlanets(g, p, Trait::Cultural)) : -kNote;
    if (is("absol_defenseact")) return f ? 0.4f : 0.f;
    if (is("absol_equality")) return f ? 0.6f : float(me.tradeGoods) * kTG;
    if (is("absol_government")) return f ? 0.4f : 0.f;
    if (is("absol_hyperlane")) return f ? kTech * 0.7f : 0.6f;
    if (is("absol_limits")) return f ? 0.f : 2 * kAC;
    if (is("absol_nexus")) return f ? 0.f : -0.6f;
    if (is("absol_pax")) return f ? 2 * kTok : 2 * kTok;
    if (is("absol_recon")) return f ? 0.4f : 0.5f;
    if (is("absol_regulations")) return f ? kTok : 2 * kTok;
    if (is("absol_sanctions")) return f ? 0.8f : 5 * kAC;
    if (is("absol_shared")) return f ? 0.5f : 0.5f;
    if (is("absol_sites")) {
        if (!f) return 0.8f;
        int k = 0;
        for (int x : controlledPlanets(g, p))
            if (isLegendaryPlanet(g, x) || !g.planets[size_t(x)].attachments.empty()) ++k;
        return float(2 * k) * kTG;
    }
    if (is("absol_travelban")) return f ? 0.6f : 0.f;
    if (is("absol_constitution")) return f ? 0.6f : 0.f;
    if (is("absol_incentive")) return f ? 1.2f : 1.8f;
    if (is("absol_measures")) return f ? 0.f : 0.4f;
    if (is("absol_mutiny")) return f ? 0.4f : 0.f;
    if (is("absol_operations")) return f ? 2 * kTok + 0.4f : 0.f;
    if (is("absol_reduction")) return f ? kTok : kTech * 0.7f + 1.f;
    if (is("absol_spending")) return f ? 0.4f : float(planetsOf(g, p)) * kTG * 0.5f;
    if (is("absol_swords")) return f ? 0.5f : 0.2f * float(planetsOf(g, p));
    if (is("absol_whresearch")) return f ? 0.8f : 0.f;
    return 0.f;
}

float aiAbsolComponentValue(const Game& g, int p) {
    // Minister of War: a command token back from the board (worth it when tactic tokens run low)
    const Player& me = g.players[size_t(p)];
    if (me.tactic >= 3 || me.strategy <= 1) return 0.2f;
    return 0.9f;
}

// ------------------------------------------------------------------ the module's questions
bool aiAbsolAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out) {
    (void)rng;
    const int p = d.player;
    if (p < 0 || p >= g.numPlayers) return false;
    const Player& me = g.players[size_t(p)];
    auto firstOf = [&]() {
        for (const Option& o : d.options)
            if (o.enabled) return o.id;
        return d.options.empty() ? -1 : d.options[0].id;
    };
    auto has = [&](int id) {
        for (const Option& o : d.options)
            if (o.id == id && o.enabled) return true;
        return false;
    };
    auto yes = [&](bool y) {
        out = one(y ? 1 : 0);
        return true;
    };
    switch (d.tag) {
        case Tag::AbsolTie: {  // the speaker decides who cast the most votes / who has the most or fewest points
            if (has(p)) {
                out = one(p);
                return true;
            }
            int best = firstOf();
            float bv = -1e9f;
            for (const Option& o : d.options)
                if (o.enabled && liking(g, p, o.id) - 0.3f * float(g.players[size_t(o.id)].vp) > bv) {
                    bv = liking(g, p, o.id) - 0.3f * float(g.players[size_t(o.id)].vp);
                    best = o.id;
                }
            out = one(best);
            return true;
        }
        case Tag::AbsolNote: {  // give the note worth least to us (never our own Support for the Throne or Alliance if avoidable)
            int best = firstOf();
            float bv = 1e9f;
            for (const Option& o : d.options) {
                if (!o.enabled) continue;
                float v = noteDefaultValue(g, o.id);
                if (noteOwner(o.id) == p && (noteType(o.id) == kNoteSupportThrone || noteType(o.id) == kNoteAlliance)) v += 6.f;
                if (noteOwner(o.id) == p && noteType(o.id) == kNoteCeasefire) v -= 0.5f;  // (ours: only a promise not to attack them)
                if (v < bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case Tag::AbsolGiveTo: {  // to the player we like most (a Ceasefire: one we won't attack - not a neighbour if possible)
            int best = firstOf();
            float bv = -1e9f;
            for (const Option& o : d.options) {
                if (!o.enabled) continue;
                float v = liking(g, p, o.id) - 0.2f * float(g.players[size_t(o.id)].vp);
                if (d.value == kAbPaxCeasefire && !areNeighbours(g, p, o.id)) v += 1.5f;
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case Tag::AbsolChoice: break;
        default: return false;
    }
    // ---- AbsolChoice
    const int a = d.agenda;
    switch (d.value) {
        case kAbAbolishLaw: {  // spend 6 influence to discard a law that hurts us
            int best = -1;
            float bv = -1.2f;
            for (const Option& o : d.options) {
                if (o.id < 0) continue;
                float v = 0.f;
                for (const Law& l : g.laws)
                    if (l.agenda == o.id) v = kAgendas[l.agenda].elect == Elect::Player ? (l.owner == p ? 1.5f : -0.4f) : aiOutcomeValue(g, p, l.agenda, l.against ? 1 : 0);
                if (v < bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case kAbBottomOutcome: {
            int best = firstOf();
            float bv = -1e9f;
            for (const Option& o : d.options) {
                const float v = aiOutcomeValue(g, p, a, o.id);
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case kAbArbiter: {
            int best = -1;
            float bv = 0.8f;  // (the token and the influence)
            for (const Option& o : d.options) {
                if (o.id < 0) continue;
                const int mine = o.id / 100, theirs = o.id % 100;
                const float v = aiStrategyCardValue(g, p, theirs) - aiStrategyCardValue(g, p, mine) - 0.15f * float(theirs + 1);
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case kAbArticlesIgnore: {
            const int act = g.activePlayer, s = g.activeSystem;
            bool mine = false;
            if (s >= 0) {
                std::vector<int> near{s};
                for (int16_t x : g.systems[size_t(s)].adj) near.push_back(x);
                for (int x : near) {
                    if (g.systems[size_t(x)].space[size_t(p)].count(UnitType::Mech)) mine = true;
                    for (int16_t pl : g.systems[size_t(x)].planets)
                        if (g.planets[size_t(pl)].units[size_t(p)].count(UnitType::Mech)) mine = true;
                }
            }
            return yes(mine && (act == p || (s >= 0 && hasUnits(g, p, s))));
        }
        case kAbArticlesMechs: case kAbAgreementMech: {
            int best = -1;
            float bv = -1e9f;
            for (const Option& o : d.options) {
                if (o.id < 0) continue;
                float v = aiPlanetValue(g, p, o.id);
                const int sys = g.planets[size_t(o.id)].system;
                for (int16_t x : g.systems[size_t(sys)].adj)
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p && hasShips(g, q, x)) v += 1.f;  // (a front line)
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            if (d.value == kAbArticlesMechs && unitsOnBoard(g, p, UnitType::Mech) >= 4) best = -1;
            out = one(best >= 0 ? best : firstOf());
            if (best < 0 && has(-1)) out = one(-1);
            return true;
        }
        case kAbChecksKeep: return yes(true);
        case kAbDefenseExtra: return yes(true);
        case kAbDefenseDestroy: case kAbWhDestroy: case kAbNexusDestroy: {
            int best = firstOf();
            float bv = 1e9f;
            for (const Option& o : d.options) {
                const float v = d.value == kAbDefenseDestroy ? aiPlanetValue(g, p, o.id) : unitWorth(UnitType(o.id));
                if (v < bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case kAbHyperShips:
            out = one(has(int(UnitType::Cruiser)) ? int(UnitType::Cruiser) : has(int(UnitType::Destroyer)) ? int(UnitType::Destroyer) : -1);
            return true;
        case kAbAntiquities: return yes(false);  // (a point is worth more than a relic)
        case kAbAntiquitiesFree: return yes(true);  // (nothing to score: the relic is free)
        case kAbPoliticalSecret: {  // (played whenever we vote, like the original card: its 3 planets worth the most votes)
            std::vector<std::pair<int, int>> v;
            for (const Option& o : d.options)
                if (std::atoi(o.detail.c_str()) > 0) v.push_back({-std::atoi(o.detail.c_str()), o.id});  // (no 0-vote planets: keep the card)
            std::sort(v.begin(), v.end());
            out = Answer();
            for (size_t i = 0; i < v.size() && i < 3; ++i) out.picks.push_back(v[i].second);
            return true;
        }
        case kAbExploration: case kAbSharedExplore: case kAbSitesAgainst: case kAbSpendExplore: case kAbRedistPlace: {
            if (d.value == kAbSharedExplore) {
                out = Answer();
                for (const Option& o : d.options) out.picks.push_back(o.id);
                return true;
            }
            out = one(firstOf());
            return true;
        }
        case kAbPeace: {
            const int act = g.activePlayer, s = g.activeSystem;
            bool use = false;
            if (act >= 0 && s >= 0 && act != p) {
                if (hasUnits(g, p, s)) {
                    float stake = float(g.systems[size_t(s)].space[size_t(p)].nonFighterShips()) * 1.5f;
                    for (int16_t x : g.systems[size_t(s)].planets)
                        if (g.planets[size_t(x)].owner == p) stake += 1.f + aiPlanetValue(g, p, x) * 0.5f;
                    if (s == me.homeSystem) stake += 5.f;
                    use = stake >= 3.f;
                }
                if (g.players[size_t(act)].vp + 2 >= g.vpGoal) use = true;
            }
            return yes(use);
        }
        case kAbPolicyKeep: {  // keep the 2 best cards
            std::vector<std::pair<float, int>> v;
            for (const Option& o : d.options) {
                int card = -1;
                for (int c = 0; c < kActionCardCount; ++c)
                    if (o.label == kActionCards[c].name) card = c;
                v.push_back({card >= 0 ? aiCardKeepValue(card) : 0.f, o.id});
            }
            std::sort(v.rbegin(), v.rend());
            out = Answer();
            for (size_t i = 0; i < v.size() && int(i) < d.minPick; ++i) out.picks.push_back(v[i].second);
            return true;
        }
        case kAbPolicyGive: {  // the worst card to the leader, the best to a friend
            const int to = d.targetPlayer;
            const bool friendly = to >= 0 && liking(g, p, to) > 0.5f;
            int best = firstOf();
            float bv = friendly ? -1e9f : 1e9f;
            for (const Option& o : d.options) {
                int card = -1;
                for (int c = 0; c < kActionCardCount; ++c)
                    if (o.label == kActionCards[c].name) card = c;
                const float v = card >= 0 ? aiCardKeepValue(card) : 0.f;
                if (friendly ? v > bv : v < bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case kAbWar: {
            int best = firstOf(), bv = -1;
            for (const Option& o : d.options)
                if (o.id >= 0 && g.systems[size_t(o.id)].space[size_t(p)].nonFighterShips() > bv) {
                    bv = g.systems[size_t(o.id)].space[size_t(p)].nonFighterShips();
                    best = o.id;
                }
            out = one(best);
            return true;
        }
        case kAbNexusIgnore: case kAbTravelIgnore: {
            const int act = d.targetPlayer >= 0 ? d.targetPlayer : g.activePlayer;
            return yes(act == p && shipsNearWormholes(g, p) > 0);
        }
        case kAbPlaceTheirToken: {  // where it hurts them most: their ships, else nearest their home
            const int q = d.targetPlayer;
            int best = firstOf();
            float bv = -1e9f;
            for (const Option& o : d.options) {
                float v = q >= 0 ? float(g.systems[size_t(o.id)].space[size_t(q)].ships()) * 2.f : 0.f;
                if (q >= 0) {
                    const int h = g.players[size_t(q)].homeSystem;
                    for (int16_t x : g.systems[size_t(o.id)].adj)
                        if (x == h) v += 1.f;
                    if (o.id == h) v += 3.f;
                }
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case kAbRevolutionTech: {
            int best = firstOf();
            float bv = 1e9f;
            for (const Option& o : d.options) {
                const float v = kTechs[o.id].unitUpgrade ? 1.f : 2.f;
                if (v < bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case kAbRevolutionDestroy: {  // the cheapest units that add up to 6
            std::vector<std::pair<float, int>> v;
            for (const Option& o : d.options) v.push_back({unitWorth(UnitType((o.id % 1024) / 64)), o.id});
            std::sort(v.begin(), v.end());
            out = Answer();
            int sum = 0;
            for (const auto& x : v) {
                if (sum >= 12) break;
                out.picks.push_back(x.second);
                sum += unitDef(g, p, UnitType((x.second % 1024) / 64)).cost2;
            }
            return true;
        }
        case kAbSchematicsGain: case kAbWhGain: {
            int best = -1;
            float bv = 0.5f;
            for (const Option& o : d.options)
                if (o.id >= 0 && aiTechWant(g, p, o.id) > bv) {
                    bv = aiTechWant(g, p, o.id);
                    best = o.id;
                }
            out = one(best >= 0 ? best : firstOf());
            return true;
        }
        case kAbSitesExplorePay: return yes(spendable(g, p, true) >= 5);
        case kAbWarrantTake: case kAbProphecyTake: case kAbRedistInfantry: case kAbSwordsExplore: return yes(true);
        case kAbCrisisPrimary: {
            const int card = -1;
            (void)card;
            return yes(me.strategy >= 2);
        }
        case kAbExecutionTake: {
            int best = -1;
            float bv = 0.2f;
            for (const Option& o : d.options)
                if (o.id >= 0 && aiCardKeepValue(o.id) > bv) {
                    bv = aiCardKeepValue(o.id);
                    best = o.id;
                }
            out = one(best);
            return true;
        }
        case kAbGrantGain: {
            int tech = -1;
            for (int t = 0; t < kTechCount; ++t)
                if (d.prompt.find(kTechs[t].name) != std::string::npos) tech = t;
            return yes(tech >= 0 && aiTechWant(g, p, tech) >= 2.f);
        }
        case kAbMeasuresExhaust: {  // the card the leader wants most and we want least
            const int lead = leaderOf(g, p);
            int best = firstOf();
            float bv = -1e9f;
            for (const Option& o : d.options) {
                const float v = (lead >= 0 ? aiStrategyCardValue(g, lead, o.id) : 0.f) - aiStrategyCardValue(g, p, o.id);
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case kAbOperationsRemove:
            out = one(has(0) ? 0 : has(2) ? 2 : firstOf());
            return true;
        case kAbRedistWho: {
            int best = firstOf();
            float bv = -1e9f;
            for (const Option& o : d.options)
                if (liking(g, p, o.id) > bv) {
                    bv = liking(g, p, o.id);
                    best = o.id;
                }
            out = one(best);
            return true;
        }
        case kAbReductionCruisers: {
            out = Answer();
            for (const Option& o : d.options)
                if (int(out.picks.size()) < d.minPick) out.picks.push_back(o.id);
            return true;
        }
        case kAbReductionPlace: {  // a dreadnought where enemies are near
            int best = -1;
            float bv = -1e9f;
            for (const Option& o : d.options) {
                if (o.id < 0) continue;
                const int s = o.id / 2;
                float v = o.id % 2 ? 1.f : 1.3f;
                for (int16_t x : g.systems[size_t(s)].adj)
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p && hasShips(g, q, x)) v += 0.5f;
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        case kAbPaxProduce: case kAbSpendSystem: {
            if (spendable(g, p, false) < 3 && (d.value == kAbPaxProduce || me.tradeGoods < 3)) {
                out = one(-1);
                return true;
            }
            int best = -1, bv = 0;
            for (const Option& o : d.options)
                if (o.id >= 0 && productionCapacity(g, p, o.id) > bv) {
                    bv = productionCapacity(g, p, o.id);
                    best = o.id;
                }
            out = one(best);
            return true;
        }
        case kAbSpendMenu: {
            const int tg = me.tradeGoods;
            if (tg >= 4 && has(2)) return out = one(2), true;
            if (tg >= 3 && has(3) && unitsOnBoard(g, p, UnitType::PDS) < 4) return out = one(3), true;
            if (tg >= 2 && has(5)) return out = one(5), true;
            if (tg >= 2 && has(4) && int(me.actionCards.size()) < actionCardLimit(g, p)) return out = one(4), true;
            if (tg >= 4 && has(1)) return out = one(1), true;
            out = one(0);
            return true;
        }
        case kAbSpendStructure: {  // a PDS on the planet nearest enemies, else the first
            int best = -1;
            float bv = -1e9f;
            for (const Option& o : d.options) {
                if (o.id < 0) continue;
                const int x = o.id / 2;
                float v = o.id % 2 ? (unitsOnBoard(g, p, UnitType::SpaceDock) < 2 ? 1.5f : -1.f) : 1.f;
                for (int16_t s : g.systems[size_t(g.planets[size_t(x)].system)].adj)
                    for (int q = 0; q < g.numPlayers; ++q)
                        if (q != p && hasShips(g, q, s)) v += 0.4f;
                if (v > bv) {
                    bv = v;
                    best = o.id;
                }
            }
            out = one(best);
            return true;
        }
        default: break;
    }
    // anything else: a yes / no says yes, a list takes the first real option
    if (isYesNoOptions(d)) return yes(true);
    out = one(firstOf());
    if (d.kind == DecisionKind::Pick && d.maxPick > 1) {
        out = Answer();
        for (const Option& o : d.options)
            if (o.enabled && int(out.picks.size()) < std::max(1, d.minPick)) out.picks.push_back(o.id);
    }
    return true;
}

}  // namespace ti
