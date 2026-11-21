// The A.I. difficulty's look-ahead (0.74.0, owner 2026-11-20: "Let's tackle the AI difficulty first").
//
// For its big decisions - the strategy card it takes, what it does with its turn, the system it activates - an A.I. seat
// takes its best few options and plays each one out on copies of the game (the engine's setup and answers replayed) to
// the end of the round's status phase, every seat played by the usual bots. It answers with the option that leaves it
// best placed on average.
//
// It never peeks. On every copy, before anything is played, what it can't see is dealt again at random: the other
// players' action cards and unscored secret objectives (from the same unseen pool - their union is public: everything
// not yet seen - in a fixed order before shuffling, so the true deal can't show through), the order of every face-down
// deck, and the dice (the copy's random numbers are reseeded). The same deals and dice are used for every option of one
// sample, so the options are compared on equal luck.
//
// Only A.I. seats think (aiIsAI), only in a game whose engine the caller handed over (aiSetEngine: the game session and
// the simulator), never inside a copy. Every other level answers exactly as before.
// Test only: TI4_THINK="options,samples,ms" (default 3,2,4000; "0" = off), TI4_THINKDEBUG=1 prints every choice.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "ai/ai.h"
#include "ai/ai_internal.h"
#include "engine/engine.h"
#include "engine/query.h"
#include "platform/platform.h"

namespace ti {

namespace {
const Engine* g_engine = nullptr;  // the real game's engine (null: no look-ahead)
int g_depth = 0;                   // > 0 while a copy is being played out (nobody thinks in there)

struct ThinkConf {
    int options = 3, samples = 2, budgetMs = 4000;
    bool debug = false;
};
const ThinkConf& conf() {
    static const ThinkConf c = [] {
        ThinkConf k;
        if (const char* e = std::getenv("TI4_THINK")) {
            if (e[0] == '0' && e[1] == 0) k.options = 0;
            else std::sscanf(e, "%d,%d,%d", &k.options, &k.samples, &k.budgetMs);
        }
        k.debug = std::getenv("TI4_THINKDEBUG") != nullptr;
        return k;
    }();
    return c;
}

uint64_t mix(uint64_t h, uint64_t v) { return (h ^ (v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2))) * 1099511628211ull; }

// a fixed order, then shuffled: the result depends only on what is in the pool and the seed
template <class T>
void shuffleCanon(std::vector<T>& v, Rng& r) {
    std::sort(v.begin(), v.end());
    for (size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[size_t(r.below(int(i)))]);
}

// Deal again everything `viewer` can't see (see the top of the file).
void determinize(Game& g, int viewer, Rng& r) {
    std::vector<int16_t> pool = g.actionDeck;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != viewer) pool.insert(pool.end(), g.players[size_t(q)].actionCards.begin(), g.players[size_t(q)].actionCards.end());
    shuffleCanon(pool, r);
    size_t k = 0;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != viewer)
            for (int16_t& c : g.players[size_t(q)].actionCards) c = pool[k++];
    for (int16_t& c : g.actionDeck) c = pool[k++];
    pool = g.secretDeck;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != viewer) pool.insert(pool.end(), g.players[size_t(q)].secrets.begin(), g.players[size_t(q)].secrets.end());
    shuffleCanon(pool, r);
    k = 0;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != viewer)
            for (int16_t& o : g.players[size_t(q)].secrets) o = pool[k++];
    for (int16_t& o : g.secretDeck) o = pool[k++];
    shuffleCanon(g.agendaDeck, r);
    shuffleCanon(g.relicDeck, r);
    shuffleCanon(g.stage1Deck, r);
    shuffleCanon(g.stage2Deck, r);
    for (auto& d : g.exploreDeck) shuffleCanon(d, r);
}

// How well placed player p is: victory points first, then what is close to scoring, the planets and the fleet - and
// how far ahead of the strongest rival.
float worth(const Game& g, int p) {
    const Player& me = g.players[size_t(p)];
    if (me.eliminated) return 0.f;
    float v = 10.f * float(me.vp);
    v += aiTune("thinkobj", 0.5f) * aiObjectiveScore(g, p);
    float ri = 0.f;
    for (size_t x = 0; x < g.planets.size(); ++x)
        if (g.planets[x].owner == p) ri += float(planetResources(g, int(x)) + planetInfluence(g, int(x)));
    v += aiTune("thinkri", 0.6f) * ri;
    float fleet = 0.f;
    for (const SystemState& s : g.systems)
        for (int t = 0; t < kUnitTypes; ++t)
            if (s.space[size_t(p)].n[size_t(t)] > 0) fleet += float(s.space[size_t(p)].n[size_t(t)]) * float(unitCost2(g, p, UnitType(t))) * 0.5f;
    v += aiTune("thinkfleet", 0.4f) * fleet;
    // (test knob thinktok, OFF) command tokens left for the rounds after the copy ends
    if (const float tok = aiTune("thinktok", 0.f); tok > 0.f) v += tok * float(me.tactic + me.fleet + me.strategy);
    return v;
}
float evaluate(const Game& g, int p) {
    for (int q = 0; q < g.numPlayers; ++q)  // somebody won in the copy
        if (!g.players[size_t(q)].eliminated && g.players[size_t(q)].vp >= g.vpGoal) return q == p ? 1000.f : -1000.f;
    float rival = 0.f;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p) rival = std::max(rival, worth(g, q));
    return worth(g, p) - aiTune("thinkrival", 0.5f) * rival;
}

Answer fallback(const Game& g, const Decision& d) {
    Answer a;
    switch (d.kind) {
        case DecisionKind::Pick:
            for (const Option& o : d.options)
                if (o.enabled && int(a.picks.size()) < std::max(1, d.minPick)) a.picks.push_back(o.id);
            break;
        case DecisionKind::Produce:
            a.data.assign(kUnitTypes + 1, 0);
            a.data[kUnitTypes] = -1;
            break;
        case DecisionKind::Tokens: {
            const Player& pl = g.players[size_t(d.player)];
            a.data = {pl.tactic + d.newTokens, pl.fleet, pl.strategy};
            break;
        }
        default: break;
    }
    return a;
}

}  // namespace

void aiSetEngine(const Engine* e) { g_engine = e; }

// ------------------------------------------------------------------ the thinking, a little at a time
struct AiThinkJob::Impl {
    const Engine* real = nullptr;
    int player = -1, answersAt = 0;
    std::vector<int> picks;
    std::vector<float> sum, got;
    int sample = 0, option = 0, done = 0, answers = 0;
    uint64_t seed = 0;
    uint32_t t0 = 0;
    bool finished = false;
    Answer result;
    // the option being played out
    std::unique_ptr<Engine> copy;
    std::unique_ptr<Rng> botRng;
    int round = 0, steps = 0;
    int lastRound = 0;  // the copies are played to the end of this round's status phase
};

AiThinkJob::AiThinkJob() : m_(new Impl) {}
AiThinkJob::~AiThinkJob() = default;
bool AiThinkJob::done() const { return m_->finished; }
const Answer& AiThinkJob::answer() const { return m_->result; }
bool AiThinkJob::stale(const Engine& e) const { return &e != m_->real || e.answerCount() != m_->answersAt; }

bool AiThinkJob::begin(const Engine& e, Rng& rng) {
    Impl& m = *m_;
    const ThinkConf& c = conf();
    if (g_depth > 0 || c.options < 2 || !e.waiting()) return false;
    const Game& g = e.game();
    const Decision& d = e.decision();
    const int p = d.player;
    if (p < 0 || p >= g.numPlayers || !aiIsAISeat(g, p) || g.players[size_t(p)].human) return false;
    // (test knob thinksec, OFF) also whether to follow another player's strategy card (its secondary ability)
    const bool sec = d.tag == Tag::SecondaryUse && aiTune("thinksec", 0.f) > 0.f;
    if (d.tag != Tag::StrategyPick && d.tag != Tag::ActionChoice && d.tag != Tag::Activate && !sec) return false;
    // the usual answer first (it is one of the options)
    ++g_depth;
    Rng keep = rng;  // (handed back only when the job goes ahead: otherwise the usual answer is worked out again)
    const Answer usual = aiAnswer(g, d, keep);
    --g_depth;
    if (usual.picks.size() != 1) return false;
    const int def = usual.picks[0];
    m.picks = {def};
    auto add = [&](int id) {
        if (int(m.picks.size()) >= c.options || std::find(m.picks.begin(), m.picks.end(), id) != m.picks.end()) return;
        for (const Option& o : d.options)
            if (o.id == id && o.enabled) m.picks.push_back(id);
    };
    if (d.tag == Tag::StrategyPick) {
        for (int id : aiStrategyRanking(g, d)) add(id);
    } else if (d.tag == Tag::Activate) {
        for (int s : aiActivationRanking(g, p, c.options + 1)) add(s);
    } else if (sec) {
        for (const Option& o : d.options) add(o.id);  // yes / no
    } else {
        // what to do with the turn: only when the usual answer uses it (deals and free options don't)
        const bool turn = def == 0 || (def >= 10 && def < 20) || def == 98 || def == 99 || def >= 1000;
        if (!turn) return false;
        add(0);  // a tactical action
        for (const Option& o : d.options)
            if (o.id >= 10 && o.id < 20) add(o.id);  // a strategic action
        add(99);  // pass
    }
    if (m.picks.size() < 2) return false;
    m.real = &e;
    m.player = p;
    m.answersAt = e.answerCount();
    m.sum.assign(m.picks.size(), 0.f);
    m.got.assign(m.picks.size(), 0.f);
    m.t0 = monotonicMs();
    m.result = usual;
    rng = keep;
    // (the endgame: when someone is within thinklateVP of the goal, the copies go on through the next round too - its
    // agenda phase and its scoring - so the A.I. sees who gets there first; test knob thinklate, OFF by default)
    m.lastRound = g.round;
    if (const float late = aiTune("thinklate", 0.f); late > 0.f) {
        int top = 0;
        for (int q = 0; q < g.numPlayers; ++q) top = std::max(top, int(g.players[size_t(q)].vp));
        if (float(top) >= float(g.vpGoal) - late) m.lastRound = g.round + 1;
    }
    return true;
}

bool AiThinkJob::step(uint32_t budgetMs) {
    Impl& m = *m_;
    if (m.finished) return true;
    const ThinkConf& c = conf();
    const uint32_t until = monotonicMs() + budgetMs;
    const Game& real = m.real->game();
    auto finish = [&] {
        AiSeat seat(m.player);  // (its own knobs: self-play tests tune the thinking seat alone - TI4_TUNESEATS)
        size_t best = 0;
        if (m.done > 0) {
            const float margin = aiTune("thinkmargin", 2.f) * float(m.done);  // the usual answer unless another is clearly better
            for (size_t i = 1; i < m.picks.size(); ++i)
                if (m.sum[i] > m.sum[best] + (best == 0 ? margin : 0.f)) best = i;
        }
        if (c.debug) {
            std::printf("THINK p%d r%d tag %d:", m.player, int(real.round), int(m.real->decision().tag));
            for (size_t i = 0; i < m.picks.size(); ++i) std::printf(" %d=%.1f", m.picks[i], m.done ? m.sum[i] / float(m.done) : 0.f);
            std::printf(" -> %d (%d samples, %d answers, %u ms)\n", m.picks[best], m.done, m.answers, monotonicMs() - m.t0);
        }
        m.result = Answer();
        m.result.picks = {m.picks[best]};
        m.copy.reset();
        m.finished = true;
        return true;
    };
    while (true) {
        if (!m.copy) {
            // the next option of this sample (a new sample: new deals and dice, unless the time is up)
            if (m.option == 0) {
                if (m.sample >= c.samples || (m.sample > 0 && int(monotonicMs() - m.t0) > c.budgetMs)) return finish();
                m.seed = mix(mix(mix(uint64_t(real.seed), uint64_t(m.answersAt)), uint64_t(m.sample)), uint64_t(m.player));
            }
            std::unique_ptr<Engine> copy(new Engine());
            std::string err;
            if (!copy->copyFrom(*m.real, &err)) return finish();
            copy->takeEvents();
            Rng r(m.seed);
            determinize(copy->g(), m.player, r);
            copy->rng().reseed(m.seed ^ 0xD1CEull);
            Answer a;
            a.picks = {m.picks[size_t(m.option)]};
            if (!copy->waiting() || !copy->answer(a, &err)) return finish();
            copy->takeEvents();
            m.round = copy->game().round;
            m.steps = 0;
            m.botRng.reset(new Rng(m.seed ^ 0xB07ull));
            m.copy = std::move(copy);
        }
        // play it on (every seat by the usual bots) to the end of this round's status phase
        Engine& e = *m.copy;
        bool over = false;
        ++g_depth;
        while (true) {
            if (!e.waiting() || e.finished() || m.steps >= 6000) {
                over = true;
                break;
            }
            const Game& h = e.game();
            if (h.round > m.lastRound || (h.round == m.lastRound && h.phase == Phase::Agenda) || h.phase == Phase::GameOver) {
                over = true;
                break;
            }
            const Decision& d = e.decision();
            Answer a = aiAnswer(h, d, *m.botRng);
            std::string err;
            if (!e.answer(a, &err) && !e.answer(fallback(h, d), &err)) {
                over = true;
                break;
            }
            e.takeEvents();
            ++m.steps;
            ++m.answers;
            if ((m.steps & 7) == 0 && int32_t(monotonicMs() - until) >= 0) break;
        }
        --g_depth;
        if (!over) return false;  // more next time
        {
            AiSeat seat(m.player);
            m.got[size_t(m.option)] = evaluate(e.game(), m.player);
        }
        m.copy.reset();
        if (++m.option >= int(m.picks.size())) {
            for (size_t i = 0; i < m.picks.size(); ++i) m.sum[i] += m.got[i];
            ++m.done;
            ++m.sample;
            m.option = 0;
        }
        if (int32_t(monotonicMs() - until) >= 0) return false;
    }
}

bool aiThink(const Game& g, const Decision& d, Rng& rng, Answer& out) {
    if (!g_engine || g_depth > 0 || &g != &g_engine->game()) return false;
    AiThinkJob job;
    if (!job.begin(*g_engine, rng)) return false;
    while (!job.step(1000000)) {
    }
    out = job.answer();
    return true;
}

}  // namespace ti
