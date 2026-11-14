// What a computer player is working towards this round: the objectives it can
// score (public ones not yet scored, its own secrets) turned into "wants" -
// how much one more planet of a trait, one more structure, a ship in a certain
// kind of system, a technology colour... is worth - plus what it must keep
// unspent for the status phase, and who is running away with the game.
//
// Values are in "plan units": a typical planet is worth 3-4, one victory point
// about kVP. The rest of the AI adds these wants to its own estimates.
#include "ai/ai_internal.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/query.h"

namespace ti {

extern int g_aiSeat;
float aiTune(const char* name, float def);
// (the Winnu review, checkpoint 2 - test knob bloodties) the influence the custodians cost this bot: none for the Winnu
// (Blood Ties) - every custodians plan, staging and reserve asked for 6. Tested (90 games): custodians 0.14 VP a game vs
// 0.12, Mecatol Rex taken in 23 games vs 21, 5 wins / 6.16 VP against 4 / 6.10; with mathispick 13 / 6.61 (12 / 6.58),
// fresh 13 / 7.14 (14 / 7.11), plain 13 / 6.84 (13 / 6.81) - no measurable effect. Owner 2026-11-12 21:46: ON
int aiCustodiansInf(const Game& g, int p) { return aiTune("bloodties", 1.f) > 0.f && isFaction(g, p, "winnu") ? 0 : 6; }
float aiTune(const char* name, float def) {
    static std::vector<std::pair<std::string, float>> table, seatTable;
    static int seatMask = 0;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        auto parse = [](const char* e, std::vector<std::pair<std::string, float>>& t) {
            std::string s = std::string(e) + ",";
            for (size_t at = 0, c; (c = s.find(',', at)) != std::string::npos; at = c + 1) {
                std::string kv = s.substr(at, c - at);
                size_t eq = kv.find('=');
                if (eq != std::string::npos) t.push_back({kv.substr(0, eq), float(std::atof(kv.c_str() + eq + 1))});
            }
        };
        if (const char* e = std::getenv("TI4_AITUNE")) parse(e, table);
        // test only (0.25.0 self-play tuning): TI4_AITUNE2 for the seats in TI4_TUNESEATS ("024")
        if (const char* e = std::getenv("TI4_AITUNE2")) parse(e, seatTable);
        if (const char* m = std::getenv("TI4_TUNESEATS"))
            for (const char* c = m; *c; ++c)
                if (*c >= '0' && *c <= '7') seatMask |= 1 << (*c - '0');
    }
    if (seatMask && g_aiSeat >= 0 && ((seatMask >> g_aiSeat) & 1))
        for (const auto& kv : seatTable)
            if (kv.first == name) return kv.second;
    for (const auto& kv : table)
        if (kv.first == name) return kv.second;
    return def;
}

namespace {

bool keyIs(int obj, const char* k) { return !std::strcmp(kObjectives[obj].key, k); }

// Value of one step towards an objective that is 'missing' steps away.
float stepValue(float points, int missing, float level, int farFrom = 3) {
    if (missing <= 0) return 0.f;
    float v = points * kVP / float(missing);
    if (missing > farFrom) v *= float(farFrom) / float(missing);  // far away: much less attractive
    return v * level;
}

// Close to winning, every victory point is worth more.
float urgency(const Game& g, int p) {
    return 1.f + float(std::max(0, g.players[p].vp - (g.vpGoal - 5))) * aiTune("urgency", 0.3f);
}

// Cheap fingerprint of the parts of the game the goals depend on.
uint64_t fingerprint(const Game& g, int p) {
    uint64_t h = 1469598103934665603ull;
    auto mixIn = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mixIn(uint64_t(g.round));
    mixIn(uint64_t(g.phase));
    mixIn(g.log.size());
    mixIn(uint64_t(p));
    for (const Player& pl : g.players) {
        mixIn(uint64_t(pl.vp));
        mixIn(uint64_t(pl.tradeGoods));
        mixIn(uint64_t(pl.tactic * 64 + pl.fleet * 8 + pl.strategy));
        mixIn(pl.techs.size());
        mixIn(pl.secrets.size() * 16 + pl.scored.size());
    }
    mixIn(g.publicObjectives.size());
    mixIn(g.promises.size());  // debts to keep trade goods for
    return h;
}

}  // namespace

int g_aiSeat = -1;  // the player the AI is answering for (set by aiAnswer)
// Difficulty of the computer player being answered for (0.19.0: per seat from the
// New Game faction menu), else the game's.
int aiLevelOf(const Game& g) {
    if (g_aiSeat >= 0 && g_aiSeat < g.numPlayers && g.players[size_t(g_aiSeat)].aiLevel >= 0) return g.players[size_t(g_aiSeat)].aiLevel;
    return g.aiLevel;
}
float aiLevelScale(const Game& g) {
    const int l = aiLevelOf(g);
    return l >= 2 ? 1.f : l == 1 ? 0.6f : 0.f;
}
AiSeat::AiSeat(int p) : keep_(g_aiSeat) { g_aiSeat = p; }
AiSeat::~AiSeat() { g_aiSeat = keep_; }
bool aiCompetitive(const Game& g) {
    // test only: TI4_SIMCOMPSEATS=0,2,4 plays those seats Competitive in a Hard game (head-to-head)
    static int mask = [] {
        int m = 0;
        if (const char* s = std::getenv("TI4_SIMCOMPSEATS"))
            for (const char* c = s; *c; ++c)
                if (*c >= '0' && *c <= '7') m |= 1 << (*c - '0');
        return m;
    }();
    if (mask && g_aiSeat >= 0) return (mask >> g_aiSeat) & 1;
    return aiLevelOf(g) >= 3;
}
bool aiCompetitiveSeat(const Game& g, int p) {
    const int keep = g_aiSeat;
    g_aiSeat = p;
    const bool c = aiCompetitive(g);
    g_aiSeat = keep;
    return c;
}
float aiUrgency(const Game& g, int p) { return urgency(g, p); }

const Goals& aiGoals(const Game& g, int p) {
    static Goals cache[8];
    static uint64_t key[8] = {};
    static const Game* owner[8] = {};
    if (p < 0 || p >= 8) {
        static Goals none;
        return none;
    }
    uint64_t k = fingerprint(g, p);
    if (owner[p] == &g && key[p] == k) return cache[p];
    owner[p] = &g;
    key[p] = k;
    setProgressAroz(aiTune("firmaroz", 1.f) > 0.f);  // (the Firmament bot review, owner 2026-11-11: plan secrets with Captain Aroz)
    AiSeat seat(p);  // p's own difficulty
    Goals& G = cache[p];
    G = Goals();
    const float level = aiLevelScale(g);
    if (level <= 0.f) return G;  // Easy: no objective planning at all
    const Player& me = g.players[p];

    // ------------------------------------------------ which objectives count
    std::vector<int> objs;
    for (int16_t o : g.publicObjectives)
        if (!hasScored(g, p, o)) objs.push_back(o);
    for (int16_t o : me.secrets) objs.push_back(o);

    // the public objective we expect to score this status phase (one per round)
    int planned = -1, plannedPts = 0;
    for (int16_t o : g.publicObjectives) {
        if (hasScored(g, p, o) || !meetsObjective(g, p, o)) continue;
        int pts = kObjectives[o].points * 10 - (objectiveSpend(o).any() ? 3 : 0);
        if (pts > plannedPts) {
            plannedPts = pts;
            planned = o;
        }
    }
    G.plannedPublic = planned;
    G.metPublics = 0;
    for (int16_t o : g.publicObjectives)
        if (!hasScored(g, p, o) && meetsObjective(g, p, o)) ++G.metPublics;

    int totalRes = 0, totalInf = 0;
    for (int x : controlledPlanets(g, p)) {
        totalRes += planetResources(g, x);
        totalInf += planetInfluence(g, x);
    }

    // Competitive: one objective to finish this round - the nearest one - gets most
    // of the effort instead of everything getting a little
    // (the groundwork, 0.25.0: one public objective a round at every level, by its scale)
    const float tempo = aiCompetitiveSeat(g, p) ? 1.f : aiGroundScale(g, p) * aiTune("g_tempo", 1.f);
    int focus = -1;
    if (tempo > 0.f) {
        float bestCost = 1e9f;
        for (int o : objs) {
            if (objectiveSpend(o).any() || meetsObjective(g, p, o)) continue;
            ObjProgress pr = objectiveProgress(g, p, o);
            if (pr.need <= 0) continue;
            float cost = float(pr.missing()) / float(kObjectives[o].points) - (kObjectives[o].kind == 3 ? 0.1f : 0.f);
            if (cost < bestCost) {
                bestCost = cost;
                focus = o;
            }
        }
    }
    G.focus = focus;
    for (int o : objs) {
        const float pts = float(kObjectives[o].points);
        const bool secret = kObjectives[o].kind == 3;
        // a public objective we already meet but won't score this round still matters next round
        ObjProgress pr = objectiveProgress(g, p, o);
        const int miss = pr.missing();
        float step = stepValue(pts, miss, level * urgency(g, p));
        if (tempo > 0.f) {
            auto part = [&](float m) { return 1.f + (m - 1.f) * tempo; };
            step *= part(miss == 1 ? aiTune("near1", 1.7f) : miss == 2 ? aiTune("near2", 1.25f) : 1.f);  // finish what's nearly done
            if (focus >= 0) step *= part(o == focus ? aiTune("focus", 2.5f) : aiTune("unfocus", 0.75f));
        }
        SpendNeed sp = objectiveSpend(o);
        if (sp.any()) {
            // Spend objectives: keep the means ready for the status phase.
            const bool affordable = meetsObjective(g, p, o);
            const bool chosen = !secret && (o == planned || (planned < 0 && affordable));
            if (sp.tokens) {
                if (chosen) G.keepTokens = std::max(G.keepTokens, sp.tokens);
                continue;
            }
            if (affordable && chosen) {
                G.keepRes = std::max(G.keepRes, sp.resources);
                G.keepInf = std::max(G.keepInf, sp.influence);
                G.keepTg = std::max(G.keepTg, sp.tradeGoods);
            }
            if (sp.tradeGoods) {  // trade goods have to be saved up
                int missTg = std::max(0, sp.tradeGoods - me.tradeGoods);
                if (missTg > 0) G.tg += stepValue(pts, missTg, level) * 0.5f;
                G.keepTg = std::max(G.keepTg, std::min<int>(me.tradeGoods, sp.tradeGoods));
            }
            if (sp.resources && totalRes < sp.resources + sp.tradeGoods)
                G.resPts += stepValue(pts, sp.resources - totalRes, level) * 0.5f;
            if (sp.influence && totalInf < sp.influence + sp.tradeGoods)
                G.infPts += stepValue(pts, sp.influence - totalInf, level) * 0.5f;
            continue;
        }
        if (pr.need == 0) {
            // action-phase secrets: a nudge towards fights / passing late
            if (keyIs(o, "pe")) G.passLate += 2.f * level;
            else if (keyIs(o, "dyp") || keyIs(o, "btv") || keyIs(o, "dts") || keyIs(o, "uf") || keyIs(o, "sar") ||
                     keyIs(o, "dtgs") || keyIs(o, "ttfd") || keyIs(o, "fwp") || keyIs(o, "mew"))
                G.combat += 2.f * level;
            continue;
        }
        if (miss <= 0) continue;
        // ---- planets
        if (keyIs(o, "corner") || keyIs(o, "unify_colonies")) {
            int best = 0;
            for (int t = 1; t <= 3; ++t) best = std::max(best, controlledCountTrait(g, p, Trait(t)));
            for (int t = 1; t <= 3; ++t)
                if (controlledCountTrait(g, p, Trait(t)) >= best - 1) G.trait[t] += step * (controlledCountTrait(g, p, Trait(t)) == best ? 1.f : 0.6f);
        } else if (keyIs(o, "faa")) G.trait[int(Trait::Cultural)] += step;
        else if (keyIs(o, "mrm")) G.trait[int(Trait::Hazardous)] += step;
        else if (keyIs(o, "mp")) G.trait[int(Trait::Industrial)] += step;
        else if (keyIs(o, "expand_borders") || keyIs(o, "subdue")) G.nonHome += step;
        else if (keyIs(o, "push_boundaries")) G.anyPlanet += step * 0.5f;
        else if (keyIs(o, "research_outposts") || keyIs(o, "brain_trust")) G.skip += step;
        else if (keyIs(o, "lost_outposts") || keyIs(o, "ancient_monuments")) G.attach += step;
        else if (keyIs(o, "conquer")) G.otherHome += step;
        else if (keyIs(o, "distant_lands")) G.nearOtherHome += step;
        else if (keyIs(o, "sai")) G.legendary += step;
        else if (keyIs(o, "syc")) G.sharedSystem += step;
        else if (keyIs(o, "eh")) G.infPts += step / 3.f;
        else if (keyIs(o, "hrm")) G.resPts += step / 3.f;
        else if (keyIs(o, "ose")) G.mecatol += step * 2.f;
        // ---- structures and units
        else if (keyIs(o, "build_defenses") || keyIs(o, "massive_cities")) G.structure += step;
        else if (keyIs(o, "infrastructure") || keyIs(o, "protect_border")) G.structOutside += step;
        else if (keyIs(o, "eap")) G.pds += step;
        else if (keyIs(o, "fwm")) G.dock += step;
        else if (keyIs(o, "gamf")) G.dread += step;
        else if (keyIs(o, "mtm")) G.mech += step;
        else if (keyIs(o, "engineer_marvel")) G.flagshipOrWarSun += step;
        else if (keyIs(o, "supremacy")) G.supremacy += step;
        else if (keyIs(o, "raise_fleet") || keyIs(o, "command_armada")) {
            if (miss <= 4) {
                G.fleetTarget = std::max(G.fleetTarget, pr.need);
                G.fleetShip += step;
            }
        } else if (keyIs(o, "pem")) G.production += step;
        else if (keyIs(o, "otf")) G.groundStack += step;
        // ---- technology
        else if (keyIs(o, "develop") || keyIs(o, "revolutionize")) G.upgrade += step;
        else if (keyIs(o, "ans")) G.factionTech += step;
        else if (keyIs(o, "diversify") || keyIs(o, "master_science")) {
            int colors = keyIs(o, "diversify") ? 2 : 4;
            int c[4], order[4] = {0, 1, 2, 3};
            for (int k = 0; k < 4; ++k) c[k] = techCount(g, p, TechColor(k));
            std::sort(order, order + 4, [&](int a, int b) { return c[a] > c[b]; });
            for (int k = 0; k < colors; ++k)
                if (c[order[k]] < 2) G.color[order[k]] += step;
        } else if (keyIs(o, "mlp")) {
            int best = 0;
            for (int k = 0; k < 4; ++k) best = std::max(best, techCount(g, p, TechColor(k)));
            for (int k = 0; k < 4; ++k)
                if (techCount(g, p, TechColor(k)) == best) G.color[k] += step;
        }
        // ---- cards and the rest
        else if (keyIs(o, "fsn")) G.actionCard += step;
        else if (keyIs(o, "dhw")) G.fragment += step;
        else if (keyIs(o, "dp")) G.law += step;
        else if (keyIs(o, "fc")) G.neighbour += step;
        else {
            // presence objectives are handled per system (aiPresenceValue)
            G.presence = true;
        }
    }

    if (aiTune("reserve", 1.f) <= 0.f) G.keepRes = G.keepInf = G.keepTg = G.keepTokens = 0;
    // debts and loans due in this status phase (the X-1 "debt meta"): keep the trade goods to pay them
    for (size_t i = 0; i < g.promises.size(); ++i) {
        const Promise& pr = g.promises[i];
        if (pr.state == kPromiseActive && pr.by == p && pr.kind == PromiseKind::Repay && pr.untilRound <= g.round &&
            aiMeansToKeep(g, p, pr, int(i)))
            G.keepTg += pr.amount;
    }
    // ------------------------------------------------ Mecatol Rex
    // the custodians token is a victory point: keep 6 influence ready from round 2
    // (the groundwork, 0.25.0: only the bot that can land there this turn - the others spend theirs)
    // (the Winnu review, checkpoint 2 - test knob bloodties: the Winnu keep none - Blood Ties)
    if (!g.custodiansRemoved && g.round >= 2 && aiCustodiansInf(g, p) > 0 && totalInf >= aiCustodiansInf(g, p) && g.mecatol() >= 0 &&
        aiTune("reserve", 1.f) > 0.f) {
        const Ground& gw = aiGround(g, p);
        // (0.44.2: also a bot with a loaded ship in or next to Mecatol Rex - it takes them next)
        if (gw.scale <= 0.f || aiTune("g_cust", 1.f) <= 0.f || gw.custodian || (aiTune("seatpark", 1.f) > 0.f && aiMecatolStaged(g, p)))
            G.keepInf = std::max(G.keepInf, aiCustodiansInf(g, p));
    }
    // holding Mecatol Rex turns every Imperial into an extra victory point
    G.mecatol += kVP * aiTune("mecatol", aiCompetitiveSeat(g, p) ? 1.4f : 0.6f) * level;

    // ------------------------------------------------ the leader
    int lead = -1;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && !g.players[q].eliminated && (lead < 0 || g.players[q].vp > g.players[lead].vp)) lead = q;
    if (lead >= 0) {
        int lv = g.players[lead].vp;
        int alarm = g.vpGoal - 4;  // 6 of 10
        if (lv >= alarm && lv >= me.vp + 2) {
            G.leader = lead;
            G.leaderThreat = float(lv - alarm + 1) * aiTune("alarm", 1.5f) * level;
        }
    }
    return G;
}

int controlledCountTrait(const Game& g, int p, Trait t) {
    int n = 0;
    for (int x : controlledPlanets(g, p))
        if (hasTraitOf(g, x, t)) ++n;
    return n;
}

// How much of one kind (resources, or influence) p can spend now from ready
// planets and trade goods while still being able to pay the reserve
// (keepRes / keepInf / keepTg) afterwards. 'exclude' planets are treated as
// already exhausted, 'tgUsed' trade goods as already spent.
int aiFreeSpend(const Game& g, int p, bool influence, const std::vector<int>& exclude, int tgUsed) {
    const Goals& G = aiGoals(g, p);
    std::vector<int> ready;
    for (int x : readyPlanets(g, p))
        if (std::find(exclude.begin(), exclude.end(), x) == exclude.end()) ready.push_back(x);
    const int w = std::max(1, tgWorth(g, p));
    const int tgAll = std::max(0, g.players[p].tradeGoods - tgUsed);
    const int tgFree = tgAll - G.keepTg;
    if (tgFree < 0) {  // already short of trade goods for the reserve: only planets
        int sum = 0;
        for (int x : ready) sum += planetPayValue(g, x, influence, false);  // (0.44.4: Archon's Gift, Exodriller...)
        return sum;
    }
    if (G.keepRes == 0 && G.keepInf == 0) {
        int sum = tgFree * w;
        for (int x : ready) sum += planetPayValue(g, x, influence, false);  // (0.44.4: Archon's Gift, Exodriller...)
        return sum;
    }
    // reach[r][i]: some split of the ready planets gives r resources and i influence
    const int R = 64, I = 64;
    std::vector<uint8_t> reach((R + 1) * (I + 1), 0), next;
    reach[0] = 1;
    for (int x : ready) {
        next = reach;
        int pr = planetPayValue(g, x, false, false), pi = planetPayValue(g, x, true, false);
        for (int r = 0; r <= R; ++r)
            for (int i = 0; i <= I; ++i) {
                if (!reach[r * (I + 1) + i]) continue;
                next[std::min(R, r + pr) * (I + 1) + i] = 1;
                next[r * (I + 1) + std::min(I, i + pi)] = 1;
            }
        reach.swap(next);
    }
    int best = -1;
    for (int r = 0; r <= R; ++r)
        for (int i = 0; i <= I; ++i) {
            if (!reach[r * (I + 1) + i]) continue;
            // the reserve's other kind must be covered; trade goods fill gaps
            int mine = influence ? i : r, other = influence ? r : i;
            int keepMine = influence ? G.keepInf : G.keepRes, keepOther = influence ? G.keepRes : G.keepInf;
            int tgForOther = std::max(0, (keepOther - other + w - 1) / w);
            if (tgForOther > tgFree) continue;
            int tgLeft = tgFree - tgForOther;
            int spend = mine + tgLeft * w - keepMine;
            best = std::max(best, spend);
        }
    return std::max(0, best);
}

// Can p still pay its reserve after exhausting 'exclude' and spending 'tgUsed' trade goods?
bool aiReserveOk(const Game& g, int p, const std::vector<int>& exclude, int tgUsed) {
    const Goals& G = aiGoals(g, p);
    if (G.keepRes == 0 && G.keepInf == 0 && G.keepTg == 0) return true;
    const int w = std::max(1, tgWorth(g, p));
    const int tgFree = g.players[p].tradeGoods - tgUsed - G.keepTg;
    if (tgFree < 0) return false;
    const int R = G.keepRes, I = G.keepInf;
    std::vector<uint8_t> reach((R + 1) * (I + 1), 0), next;
    reach[0] = 1;
    for (int x : readyPlanets(g, p)) {
        if (std::find(exclude.begin(), exclude.end(), x) != exclude.end()) continue;
        next = reach;
        int pr = planetResources(g, x), pi = planetInfluence(g, x);
        for (int r = 0; r <= R; ++r)
            for (int i = 0; i <= I; ++i) {
                if (!reach[r * (I + 1) + i]) continue;
                next[std::min(R, r + pr) * (I + 1) + i] = 1;
                next[r * (I + 1) + std::min(I, i + pi)] = 1;
            }
        reach.swap(next);
    }
    for (int r = 0; r <= R; ++r)
        for (int i = 0; i <= I; ++i)
            if (reach[r * (I + 1) + i] && (R - r + w - 1) / w + (I - i + w - 1) / w <= tgFree) return true;
    return false;
}

namespace {
// 0: not a presence objective; 1: units in the system count; 2: only ships count.
int presenceKind(int o) {
    const char* k = kObjectives[o].key;
    for (const char* x : {"make_history", "become_legend", "deep_space", "vast_territories", "outer_rim", "control_borderlands", "dfat"})
        if (!std::strcmp(k, x)) return 1;
    for (const char* x : {"intimidate", "ctr", "te", "lsc", "csl", "btgk", "ose"})
        if (!std::strcmp(k, x)) return 2;
    return 0;
}

// Does being in 'sys' count towards presence objective o?
bool presenceFits(const Game& g, int p, int o, int sys) {
    const SystemState& s = g.systems[sys];
    if (s.isHyperlane()) return false;
    const char* k = kObjectives[o].key;
    auto is = [&](const char* x) { return !std::strcmp(k, x); };
    const int mec = g.mecatol();
    if (is("intimidate")) return mec >= 0 && sys != mec && adjacentSystems(g, sys, mec);
    if (is("make_history") || is("become_legend")) {
        if (s.d().anomalies || sys == mec) return true;
        for (int16_t pl : s.planets)
            if (kPlanets[g.planets[pl].def].legendary) return true;
        return false;
    }
    if (is("deep_space") || is("vast_territories")) return s.planets.empty();
    if (is("outer_rim") || is("control_borderlands")) return s.edge && s.home != p;
    if (is("ctr")) return true;
    if (is("te")) {
        for (int16_t a : s.adj)
            if (g.systems[a].home >= 0 && g.systems[a].home != p) return true;
        return false;
    }
    if (is("lsc")) {
        for (int16_t a : s.adj)
            if (g.systems[a].d().anomalies) return true;
        return false;
    }
    if (is("csl")) {
        for (int q = 0; q < g.numPlayers; ++q) {
            if (q == p) continue;
            if (s.space[q].count(UnitType::SpaceDock)) return true;
            for (int16_t x : s.planets)
                if (g.planets[x].units[q].count(UnitType::SpaceDock)) return true;
        }
        return false;
    }
    if (is("dfat")) return sys == g.nexus;
    if (is("ose")) return sys == mec;
    if (is("btgk")) return (wormholesOf(g, sys) & (kAlpha | kBeta)) != 0;
    return false;
}

template <class F>
void forPresenceObjectives(const Game& g, int p, F f) {
    for (int16_t o : g.publicObjectives)
        if (!hasScored(g, p, o) && presenceKind(o)) f(int(o));
    for (int16_t o : g.players[p].secrets)
        if (presenceKind(o)) f(int(o));
}
}  // namespace

// Extra value of getting units (ships if withShips) into 'sys' for presence
// objectives (public and secret) we haven't met yet.
float presenceValueRaw(const Game& g, int p, int sys, bool withShips);
float presenceHoldRaw(const Game& g, int p, int sys);

float aiPresenceValue(const Game& g, int p, int sys, bool withShips) {
    if (!withShips || p < 0 || p >= 8) return presenceValueRaw(g, p, sys, withShips);
    Goals& G = const_cast<Goals&>(aiGoals(g, p));
    if (G.presShips.size() != g.systems.size()) {
        G.presShips.assign(g.systems.size(), 0.f);
        for (size_t s = 0; s < g.systems.size(); ++s) G.presShips[s] = presenceValueRaw(g, p, int(s), true);
    }
    return G.presShips[size_t(sys)];
}

float aiPresenceHold(const Game& g, int p, int sys) {
    if (p < 0 || p >= 8) return 0.f;
    Goals& G = const_cast<Goals&>(aiGoals(g, p));
    if (G.presHold.size() != g.systems.size()) {
        G.presHold.assign(g.systems.size(), 0.f);
        for (size_t s = 0; s < g.systems.size(); ++s) G.presHold[s] = presenceHoldRaw(g, p, int(s));
    }
    return G.presHold[size_t(sys)];
}

float presenceValueRaw(const Game& g, int p, int sys, bool withShips) {
    const float level = aiLevelScale(g);
    if (level <= 0.f || g.systems[sys].isHyperlane()) return 0.f;
    const bool unitsHere = hasUnits(g, p, sys), shipsHere = hasShips(g, p, sys);
    float v = 0;
    forPresenceObjectives(g, p, [&](int o) {
        int kind = presenceKind(o);
        if (kind == 2 && !withShips) return;
        if (kind == 2 ? shipsHere : unitsHere) return;
        if (!presenceFits(g, p, o, sys)) return;
        if (!std::strcmp(kObjectives[o].key, "btgk")) {  // only the wormhole type we lack
            bool a = false, b = false;
            for (size_t x = 0; x < g.systems.size(); ++x) {
                if (!hasShips(g, p, int(x))) continue;
                if (wormholesOf(g, x) & kAlpha) a = true;
                if (wormholesOf(g, x) & kBeta) b = true;
            }
            uint8_t wh = wormholesOf(g, sys);
            if (!((!a && (wh & kAlpha)) || (!b && (wh & kBeta)))) return;
        }
        // a ship into a system is a cheap step: far-away presence objectives still count
        v += stepValue(float(kObjectives[o].points), objectiveProgress(g, p, o).missing(), level * urgency(g, p), int(aiTune("farpres", 5.f)));
    });
    return v;
}

// Value lost if our last ships leave 'sys' (and, for unit objectives, nothing
// else of ours stays): presence objectives we meet or nearly meet would slip.
float presenceHoldRaw(const Game& g, int p, int sys) {
    const float level = aiLevelScale(g);
    if (level <= 0.f) return 0.f;
    float v = 0;
    forPresenceObjectives(g, p, [&](int o) {
        if (!presenceFits(g, p, o, sys)) return;
        if (presenceKind(o) == 1) {  // ground forces on a planet there keep it too
            for (int16_t pl : g.systems[sys].planets)
                if (g.planets[pl].units[p].groundForces() + g.planets[pl].units[p].structures() > 0) return;
        }
        ObjProgress pr = objectiveProgress(g, p, o);
        if (pr.need && pr.missing() == 0) v += float(kObjectives[o].points) * kVP * level;
        else if (pr.need && pr.missing() == 1) v += 0.4f * kVP * level;
    });
    return v;
}

}  // namespace ti
