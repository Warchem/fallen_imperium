// Computer players. Given the game state and a question, produce an answer.
// The AI always answers legally (every answer is checked by the rules); its
// strategy is a set of hand-written priorities, not a search.
#pragma once
#include <cstdint>
#include <memory>

#include "engine/decision.h"
#include "engine/rng.h"
#include "engine/state.h"

namespace ti {

class Engine;

Answer aiAnswer(const Game& g, const Decision& d, Rng& rng);
// (0.74.0) The engine of the game being played, so the A.I. difficulty can play its options out on copies of it
// (src/ai/ai_think.cpp). Set before asking the AI; null = no look-ahead (every other level ignores it).
void aiSetEngine(const Engine* e);
constexpr int kEvalFeatures = 13;
int aiEvalFeatures(const Game& g, int p, float* f);  // (0.86.0) the position as numbers (self-play training of the A.I. level)

// (0.74.0) The A.I. difficulty's look-ahead as a job run a little at a time, so the game screen keeps moving while it
// thinks (the simulator runs it in one go inside aiAnswer). begin(): false = no thinking for this question - answer it
// the usual way; it uses rng exactly as the usual answer would.
class AiThinkJob {
public:
    AiThinkJob();
    ~AiThinkJob();
    bool begin(const Engine& e, Rng& rng);
    bool step(uint32_t budgetMs);  // true = done: answer() holds the choice
    bool done() const;
    bool stale(const Engine& e) const;  // the game moved on since begin() (an undo, a new game): drop the job
    const Answer& answer() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_;
};

// Helpers that the UI also uses for "suggested" defaults.
Answer aiPayment(const Game& g, const Decision& d);
Answer aiHits(const Game& g, const Decision& d);
Answer aiTokens(const Game& g, const Decision& d);

// Negotiations, for the deal screens: the deal the AI would propose to q in
// p's place (countering = the offer being countered, or null); false = nothing.
bool aiSuggestDeal(const Game& g, int p, int q, const Deal* countering, Deal& out);
// The outcome of this agenda that seems best for p (a default for vote deals).
int aiFavouredOutcome(const Game& g, int p, int agenda);
// How computer player q would judge this deal: >= 0 means it would accept.
float aiDealMargin(const Game& g, int q, const Deal& d);

// Rough chance (0..1) that 'att' beats 'def' in a fight between these forces.
float fightOdds(const Game& g, int att, const Force& a, int def, const Force& d, bool space);

}  // namespace ti
