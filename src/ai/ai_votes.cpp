// The agenda phase as people play it (0.65.0, task #494). The owner and his tester: "most of the bot votes would go to
// the first agenda card with none really being saved for the last ... there should be a dynamic and organic purpose to
// the agenda deck depending on the player and their personality". Measured before (24 Competitive 6-player games): in
// 59% of the agendas only one outcome got any votes, the winner ~19 votes ahead, and 39 times a bot had nothing left
// for the second agenda - every bot spent half its votes on the first agenda (all of them when it cared) whatever the
// table looked like.
//
// Now each computer player makes a voting plan for every agenda:
//  1. what it wants - each outcome's worth to it (aiOutcomeValue), its faction's interests (kInterests, from the
//     strategy guides), its personality (how much it cares about politics, spoilers, grudges), its riders and
//     predictions, and the agendas that pay whoever voted a certain way (Mutiny, Unconventional Measures, Articles of
//     War, the Nomad's Future Sight);
//  2. reading the table - the votes already cast (public and in order; Hidden Agenda: only the Argent's) and the votes
//     still to come (each later voter's likely side and size) - then it holds (already winning), flips (casts enough to
//     win plus a margin) or abstains (hopeless, or nothing at stake);
//  3. a budget over the phase's two agendas - votes spent on the first are gone for the second, which is face down: the
//     votes it keeps are worth the average stake of the cards still in the deck (the card itself when this player put
//     it on top with Politics);
//  4. a voting style, fixed at game start from its personality (Spender, Saver, Splitter, Broker - Chaotic rolls a new
//     one each round), toned down at Competitive like its other habits.
// The rules (Living Rules 8.7) don't let a player split votes between outcomes of one agenda: the splitting is between
// the two agendas. AI only: every vote is still an answer the rules check, so saves replay exactly as before.
// Test only: TI4_VOTEDEBUG=1 prints each plan to stderr; knob vplan=0 (aiTune) = the old voting (head-to-head tests).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "ai/ai_internal.h"
#include "engine/deal.h"
#include "engine/personality.h"
#include "engine/query.h"

namespace ti {

namespace {

constexpr int kAbstainId = -100;  // the rules' Abstain option (rules_agenda.cpp kAbstain)

bool agIs(int a, const char* k) { return a >= 0 && a < kAgendaCount && !std::strcmp(kAgendas[a].key, k); }
int realAgenda(const Game& g, int a) { return (agIs(a, "covert") || agIs(a, "absol_covert")) && g.covertAgenda >= 0 ? g.covertAgenda : a; }

bool debugOn() {
    static const int on = std::getenv("TI4_VOTEDEBUG") ? 1 : 0;
    return on != 0;
}

// ------------------------------------------------------------------ voting styles
struct Params {
    float save = 1.f;     // how much the votes kept for the second agenda are worth to it (x the cards' stakes)
    float cushion = 2.f;  // votes over "just enough" when later voters might still swing it
    float pileOn = 0.f;   // share of its votes it piles on a first-agenda outcome that is already winning (a habit)
    float care = 1.f;     // how much the agendas matter to it at all (personality x faction)
};

// Spender, Saver, Splitter, Broker weights per archetype (Politician .. Chaotic)
const int kStyleWeights[int(Arch::Count)][4] = {
    {10, 25, 40, 25},  // Politician: plans both cards, trades votes
    {15, 35, 40, 10},  // Peacekeeper
    {50, 15, 25, 10},  // Warlord: goes big on whatever is in front of it
    {30, 20, 25, 25},  // Betrayer
    {15, 20, 20, 45},  // Swindler: sells its votes
    {10, 20, 25, 45},  // Merchant: sells its votes
    {20, 25, 25, 30},  // Opportunist
    {55, 10, 25, 10},  // Rusher
    {10, 50, 35, 5},   // Turtler: holds back
    {25, 25, 25, 25},  // Chaotic (a new roll every round)
};
// how much each archetype cares about agendas at all
const float kArchCare[int(Arch::Count)] = {1.4f, 1.15f, 0.75f, 1.f, 0.9f, 1.f, 1.05f, 0.7f, 1.1f, 1.f};

struct FactionCare {
    const char* key;
    float care;
};
// The political factions of the strategy guides (docs/owner/TI4-Faction-Strategy-Guides.txt): they bank influence for
// the agenda phase, sell or buy votes, or score from agendas.
const FactionCare kFactionCare[] = {
    {"xxcha", 1.3f},  {"winnu", 1.2f},  {"keleres", 1.25f}, {"argent", 1.15f},     {"hacan", 1.15f}, {"empyrean", 1.1f},
    {"mahact", 1.1f}, {"nomad", 1.15f}, {"edyn", 1.3f},     {"freesystems", 1.15f}, {"olradin", 1.1f}, {"kortali", 1.1f},
};

// A faction's own interests in particular agendas (the strategy guides' advice and what their abilities depend on).
// outcome: 0 For, 1 Against, kSelf = the faction elected.
constexpr int kSelf = -2;
struct Interest {
    const char* faction;
    const char* agenda;
    int outcome;
    float bonus;
};
const Interest kInterests[] = {
    {"muaat", "schematics", 1, 0.7f},           // their war suns stay theirs
    {"axis", "schematics", 0, 0.4f},            // more war suns to sell
    {"l1z1x", "conventions", 1, 0.6f},          // Harrow and their bombardment
    {"l1z1x", "arms_reduction", 1, 0.6f},       // dreadnoughts
    {"letnev", "arms_reduction", 1, 0.6f},
    {"letnev", "regulations", 1, 0.4f},         // a big fleet pool
    {"saar", "regulations", 1, 0.4f},
    {"mahact", "regulations", 1, 0.4f},         // other players' tokens in their fleet pool
    {"naalu", "prophecy", kSelf, 0.8f},         // fighters
    {"sardakk", "conscription", 1, 0.4f},
    {"sol", "conscription", 1, 0.6f},           // infantry
    {"sol", "plowshares", 1, 0.5f},
    {"arborec", "conscription", 1, 0.7f},
    {"jolnar", "revolution", 1, 0.9f},          // research
    {"jolnar", "minister_sciences", kSelf, 0.6f},
    {"jolnar", "grant_reallocation", kSelf, 0.5f},
    {"ghost", "wormhole_recon", 0, 0.7f},       // wormholes everywhere
    {"ghost", "travel_ban", 1, 0.5f},
    {"yssaril", "sanctions", 1, 0.6f},          // a big hand of action cards
    {"yssaril", "unconventional", 0, 0.5f},
    {"yssaril", "minister_policy", kSelf, 0.5f},
    {"yin", "sanctions", 1, 0.3f},
    {"hacan", "minister_commerce", kSelf, 0.8f},
    {"mentak", "minister_commerce", kSelf, 0.3f},
    {"titans", "defense_act", 0, 0.6f},         // PDS
    {"xxcha", "defense_act", 0, 0.3f},
    {"naaz", "rearmament", 0, 0.6f},            // mechs
    {"naaz", "articles_war", 1, 0.6f},
    {"gledge", "rearmament", 0, 0.5f},
    {"keleres", "committee", kSelf, 0.5f},
    {"keleres", "minister_policy", kSelf, 0.3f},
    {"winnu", "artifact", 0, 0.5f},             // the Winnu guide: agendas that protect them
    {"winnu", "mutiny", 0, 0.4f},
    {"kollecc", "minister_antiquities", kSelf, 0.7f},
    {"bentor", "minister_exploration", kSelf, 0.5f},
};

float interestOf(const Game& g, int p, int a, int winner) {
    float v = 0.f;
    const Elect el = kAgendas[a].elect;
    for (const Interest& in : kInterests) {
        if (!agIs(a, in.agenda) || !isFaction(g, p, in.faction)) continue;
        if (in.outcome == kSelf) {
            if (el == Elect::Player && winner == p) v += in.bonus;
        } else if (el == Elect::ForAgainst && winner == in.outcome) {
            v += in.bonus;
        }
    }
    return v;
}

float factionCare(const Game& g, int p) {
    for (const FactionCare& f : kFactionCare)
        if (isFaction(g, p, f.key)) return f.care;
    return 1.f;
}

Params paramsOf(VoteStyle s) {
    Params k;
    switch (s) {
        case VoteStyle::Spender: k.save = 0.35f, k.cushion = 3.f, k.pileOn = 0.6f; break;
        case VoteStyle::Saver: k.save = 1.7f, k.cushion = 1.f; break;
        case VoteStyle::Splitter: k.save = 1.f, k.cushion = 2.f; break;
        case VoteStyle::Broker: k.save = 1.25f, k.cushion = 1.f, k.care = 0.85f; break;
    }
    return k;
}

Params paramsFor(const Game& g, int p) {
    Params k = paramsOf(aiVoteStyle(g, p));
    const Personality& pe = g.players[size_t(p)].pers;
    const int arch = pe.now >= 0 ? pe.now : pe.arch;
    float care = arch >= 0 && arch < int(Arch::Count) ? kArchCare[arch] : 1.f;
    if (aiCompetitiveSeat(g, p)) {  // the habits toned down like the rest of the personality (competitiveDamp)
        const Params mid = paramsOf(VoteStyle::Splitter);
        auto toward = [](float v, float neutral) { return neutral + (v - neutral) * 0.35f; };
        k.save = toward(k.save, mid.save);
        k.cushion = toward(k.cushion, mid.cushion);
        k.pileOn = toward(k.pileOn, mid.pileOn);
        k.care = toward(k.care, 1.f);
        care = toward(care, 1.f);
    }
    k.care *= care * factionCare(g, p);
    return k;
}

// ------------------------------------------------------------------ what an outcome is worth
// What a rider being right is worth (the "victory point" scale of aiOutcomeValue: 1 point = 3).
float riderWorth(int card) {
    if (card < 0 || card >= kActionCardCount) return 0.f;
    const char* k = kActionCards[card].key;
    if (!std::strcmp(k, "imp_rider")) return 3.f;
    if (!std::strcmp(k, "trade_rider")) return 1.5f;
    if (!std::strcmp(k, "lead_rider")) return 1.2f;
    if (!std::strcmp(k, "tech_rider")) return 1.6f;
    if (!std::strcmp(k, "politic_rider")) return 1.6f;
    if (!std::strcmp(k, "const_rider") || !std::strcmp(k, "war_rider")) return 1.f;
    if (!std::strcmp(k, "diplo_rider")) return 0.7f;
    return 1.f;
}

struct Who {  // what the planner knows about itself (its own riders, predictions, grudges)
    int p = -1;
    bool self = false;  // planning its own vote (else: guessing another player's)
    float care = 1.f;
    int riderBet = -1, riderCard = -1, predicted = -1;
};

// Worth to w.p of outcome `winner` resolving when w.p voted for `myVote` (kAbstainId: didn't vote).
float worth(const Game& g, const Who& w, int a, int winner, int myVote) {
    const int p = w.p;
    const int ea = realAgenda(g, a);
    const Elect el = kAgendas[ea].elect;
    float base;
    float reward = 0.f;  // concrete gains that don't depend on how much it cares about politics
    if (agIs(ea, "mutiny")) {  // For: who voted For gains a point; Against: who voted For loses one
        base = winner == 0 ? -0.4f : 0.f;
        if (myVote == 0) reward += winner == 0 ? 3.f : -3.f;
    } else if (agIs(ea, "unconventional")) {  // For: who voted For draws 2; Against: who voted For discards their hand
        base = winner == 0 ? -0.2f : 0.f;
        if (myVote == 0) reward += winner == 0 ? 2.f : -0.4f * float(g.players[size_t(p)].actionCards.size());
    } else if (agIs(ea, "articles_war")) {  // For: mechs lose their abilities; Against: who voted For gains 3 trade goods
        if (winner == 0) {
            float others = 0.f;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p) others += float(unitsOnBoard(g, q, UnitType::Mech));
            base = -0.35f * float(unitsOnBoard(g, p, UnitType::Mech)) + 0.08f * others;
        } else {
            base = 0.f;
            if (myVote == 0) reward += 1.f;
        }
    } else {
        base = aiOutcomeValue(g, p, a, winner);
        // the outcomes that cost whoever voted for them
        if (agIs(ea, "conventions") && winner == 1 && myVote == 1)  // Against: who voted Against discards their hand
            reward -= 0.4f * float(g.players[size_t(p)].actionCards.size());
        if (agIs(ea, "wormhole_research") && winner == 1 && myVote == 1) reward -= 0.35f;  // Against: a command token
        if (agIs(ea, "rep_govt") && winner == 1 && myVote == 1)  // Against: who voted Against exhausts its cultural planets
            reward -= 0.15f * float(controlledCountTrait(g, p, Trait::Cultural));
        // (0.81.0) Absol's Agendas: what voting a certain way - or abstaining - brings by itself
        if (kAgendas[ea].source == Source::Absol) reward += absolVoterReward(g, p, ea, winner, myVote);
    }
    base += interestOf(g, p, ea, winner);
    if (w.self) {
        const Style& S = aiStyle(g, p);
        if (S.spoiler > 0.f) {  // votes against the points leader
            int leader = -1;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && !g.players[size_t(q)].eliminated && (leader < 0 || g.players[size_t(q)].vp > g.players[size_t(leader)].vp)) leader = q;
            if (leader >= 0) base -= S.spoiler * 0.6f * std::max(0.f, aiOutcomeValue(g, leader, a, winner));
        }
        if (S.voteAgainst >= 0) {  // a grudge (Kingslayer, a betrayal objective)
            base -= 0.8f * aiOutcomeValue(g, S.voteAgainst, a, winner);
            if (el == Elect::Player && winner == S.voteAgainst) base -= 5.f;
        }
        if (w.riderBet >= 0 && winner == w.riderBet) reward += riderWorth(w.riderCard);
        if (w.predicted != -1 && winner == w.predicted) reward += 0.8f;  // Radiance / Unity Algorithm
    }
    if (isFaction(g, p, "nomad") && myVote == winner && myVote != kAbstainId) reward += 0.35f;  // Future Sight: 1 trade good
    return base * w.care + reward;
}

// ------------------------------------------------------------------ the table
struct Table {
    std::vector<int> outs;           // outcome ids
    std::map<int, float> known;      // votes already cast (that this player can see)
    std::map<int, float> coming;     // votes expected from the voters still to come
    float comingTotal = 0.f;
    bool last = false;               // nobody votes after this player
};

std::vector<int> outcomeIds(const Game& g, int a) {
    std::vector<int> v;
    for (const auto& oc : voteOutcomes(g, a)) v.push_back(oc.first);
    return v;
}

std::vector<int> voteOrder(const Game& g) {
    const int n = g.numPlayers;
    std::vector<int> order = clockwiseFrom(g, (g.speaker + 1) % n);
    const int argent = playerOfFaction(g, "argent");  // Zeal: always first in the agenda phase
    if (argent >= 0 && (g.fixes < 26 || g.phase == Phase::Agenda)) {
        order.erase(std::remove(order.begin(), order.end(), argent), order.end());
        order.insert(order.begin(), argent);
    }
    return order;
}

bool cantVote(const Game& g, int q) {
    if (g.players[size_t(q)].eliminated) return true;
    if (isFaction(g, q, "nekro")) return true;  // Galactic Threat
    if ((g.noVote >> q) & 1) return true;
    return false;
}

int votesOf(const Game& g, int q) { return lawInPlay(g, "rep_govt") ? 1 : availableVotes(g, q); }

// How another player q is likely to vote: its outcome (kAbstainId: not at all) and how many votes.
std::pair<int, float> guessVote(const Game& g, int q, int a, const std::vector<int>& outs, bool lastAgenda) {
    if (cantVote(g, q)) return {kAbstainId, 0.f};
    const float have = float(votesOf(g, q));
    if (have <= 0.f) return {kAbstainId, 0.f};
    for (const Promise& pr : g.promises)  // a vote it promised
        if (pr.state == kPromiseActive && pr.by == q && pr.kind == PromiseKind::VoteFor && pr.agenda == a)
            return {pr.target, have * 0.7f};
    Who w;
    w.p = q;
    int best = kAbstainId;
    float bv = -1e9f, lo = 1e9f;
    for (int o : outs) {
        const float v = worth(g, w, a, o, o);
        lo = std::min(lo, v);
        if (v > bv) {
            bv = v;
            best = o;
        }
    }
    const float strength = bv - lo;  // how much this agenda matters to it
    float share;
    if (lastAgenda) share = strength >= 0.1f ? 0.95f : 0.f;  // free votes: everyone with a preference uses them
    else share = strength >= 0.3f ? 0.45f : strength >= 0.1f ? 0.2f : 0.f;
    if (best == kAbstainId || share <= 0.f) return {kAbstainId, 0.f};  // nothing at stake for it
    float k = have * share;
    if (isFaction(g, q, "argent") && g.phase == Phase::Agenda) k += float(g.numPlayers);  // Zeal
    return {best, k};
}

Table readTable(const Game& g, int p, int a, const std::vector<int>& outs) {
    Table t;
    t.outs = outs;
    const bool lastAgenda = g.phase != Phase::Agenda || g.agendaNumber >= 1;
    const bool hidden = eventOn(g, kEvHidden);
    const std::vector<int> order = voteOrder(g);
    bool after = false;
    for (int q : order) {
        if (q == p) {
            after = true;
            continue;
        }
        if (!after) {  // voted already
            if (q < int(g.votedFor.size()) && g.votedFor[size_t(q)] != kNoVote) {
                if (!hidden || isFaction(g, q, "argent")) {
                    t.known[g.votedFor[size_t(q)]] += float(g.voteCount[size_t(q)]);
                } else {  // Hidden Agenda: it voted, but where?
                    const auto gv = guessVote(g, q, a, outs, lastAgenda);
                    if (gv.first != kAbstainId) t.coming[gv.first] += gv.second;
                    t.comingTotal += gv.second;
                }
            }
            continue;
        }
        const auto gv = guessVote(g, q, a, outs, lastAgenda);
        if (gv.first == kAbstainId) continue;
        t.coming[gv.first] += gv.second;
        t.comingTotal += gv.second;
    }
    t.last = t.comingTotal <= 0.f;
    return t;
}

// The outcome that wins with these totals (ties: the speaker decides - p itself, or its likely choice).
int winnerOf(const Game& g, const Who& w, int a, const std::vector<int>& outs, const std::map<int, float>& total) {
    float best = -1.f;
    std::vector<int> tied;
    for (int o : outs) {
        auto it = total.find(o);
        const float t = it == total.end() ? 0.f : it->second;
        if (t > best + 1e-4f) {
            best = t;
            tied.assign(1, o);
        } else if (std::fabs(t - best) <= 1e-4f) {
            tied.push_back(o);
        }
    }
    if (tied.size() <= 1) return tied.empty() ? kAbstainId : tied[0];
    const int sp = g.speaker;
    Who s;
    s.p = sp;
    if (sp == w.p) s = w;
    int pick = tied[0];
    float pv = -1e9f;
    for (int o : tied) {
        const float v = worth(g, s, a, o, kAbstainId);
        if (v > pv) {
            pv = v;
            pick = o;
        }
    }
    return pick;
}

// The best p can do with its votes on agenda a (gain over abstaining), and how. lambda = what each vote is worth kept.
struct Choice {
    int outcome = kAbstainId;
    int votes = 0;
    float gain = 0.f;     // value over abstaining (before the cost of the votes)
    float net = 0.f;      // gain - lambda x votes
    int ifAbstain = kAbstainId;
};

Choice bestChoice(const Game& g, const Who& w, int a, const std::vector<int>& outs, const Table& t, int avail, float lambda,
                  const Params& k, int zeal) {
    Choice c;
    std::map<int, float> proj;
    for (int o : outs) {
        float v = 0.f;
        auto i1 = t.known.find(o);
        if (i1 != t.known.end()) v += i1->second;
        auto i2 = t.coming.find(o);
        if (i2 != t.coming.end()) v += i2->second;
        proj[o] = v;
    }
    const int w0 = winnerOf(g, w, a, outs, proj);
    c.ifAbstain = w0;
    const float base = worth(g, w, a, w0, kAbstainId);
    // candidates: the outcomes it likes most (elections can have dozens), the likely winner, and the outcome each of
    // its riders / predictions backs
    std::vector<std::pair<float, int>> ranked;
    for (int o : outs) ranked.push_back({worth(g, w, a, o, o), o});
    std::sort(ranked.rbegin(), ranked.rend());
    std::vector<int> cand;
    for (size_t i = 0; i < ranked.size() && i < 6; ++i) cand.push_back(ranked[i].second);
    if (std::find(cand.begin(), cand.end(), w0) == cand.end() && w0 != kAbstainId) cand.push_back(w0);
    const float cushion = t.last ? 0.f : k.cushion + std::ceil(0.25f * t.comingTotal);
    const int maxVotes = avail + (avail > 0 ? zeal : 0);
    for (int o : cand) {
        // its own side counted at half (the voters still to come may hold back - they read the table too), its rivals
        // in full: a bot leaves a vote to others only when the votes already cast make it safe
        auto at = [&](const std::map<int, float>& m, int id) {
            auto it = m.find(id);
            return it == m.end() ? 0.f : it->second;
        };
        const float mine = at(t.known, o) + 0.5f * at(t.coming, o);
        float rival = 0.f;
        int rivalId = kAbstainId;
        for (int r : outs) {
            if (r == o) continue;
            const float v = at(t.known, r) + at(t.coming, r);
            if (v > rival) {
                rival = v;
                rivalId = r;
            }
        }
        // a tie goes to the speaker
        bool tieOk = g.speaker == w.p;
        if (!tieOk && rivalId != kAbstainId) {
            Who s;
            s.p = g.speaker;
            tieOk = worth(g, s, a, o, kAbstainId) > worth(g, s, a, rivalId, kAbstainId);
        }
        const float need = std::max(0.f, rival - mine + (tieOk ? 0.f : 1.f));
        int cast;
        int winner;
        if (need <= float(maxVotes)) {  // reachable: just enough plus a margin for the voters still to come
            cast = std::min(maxVotes, std::max(1, int(std::ceil(need + cushion))));
            winner = o;
            if (w0 == o && mine - rival >= cushion) cast = 1;  // already safe: only what a vote itself brings
        } else {  // out of reach: a vote only for what voting itself brings (Articles of War, Future Sight...)
            cast = 1;
            winner = w0;
        }
        const float gain = worth(g, w, a, winner, o) - base;
        const float net = gain - lambda * float(cast);
        if (net > c.net + 1e-4f || (c.outcome == kAbstainId && net > 0.f)) {
            c.outcome = o;
            c.votes = cast;
            c.gain = gain;
            c.net = net;
        }
    }
    return c;
}

// Worth of keeping votes for the next agenda (its average stake; the card itself when p saw it).
float nextAgendaStake(const Game& g, int p, int avail) {
    if (avail <= 0) return 0.f;
    std::vector<int16_t> deck(g.agendaDeck.begin(), g.agendaDeck.end());
    if (deck.empty()) deck.assign(g.agendaDiscard.begin(), g.agendaDiscard.end());
    if (deck.empty()) return 0.f;
    std::vector<int> cards;
    const int top = g.agendaDeck.empty() ? -1 : g.agendaDeck.back();
    const Player& me = g.players[size_t(p)];
    if (top >= 0 && (me.agendaSeen[0] == top || me.agendaSeen[1] == top)) {
        cards.push_back(top);  // Politics: it put this one on top
    } else {
        for (int16_t c : deck)
            if (std::find(cards.begin(), cards.end(), int(c)) == cards.end()) cards.push_back(c);
    }
    Who w;
    w.p = p;
    w.self = true;
    w.care = paramsFor(g, p).care;
    float sum = 0.f;
    int counted = 0;
    for (int c : cards) {
        if (agIs(c, "covert")) continue;  // (nobody knows what it will be)
        const std::vector<int> outs = outcomeIds(g, c);
        if (outs.empty()) continue;
        // everyone else as it would vote on it (a fresh table, the second agenda: they spend freely)
        std::map<int, float> total;
        float all = 0.f;
        for (int q = 0; q < g.numPlayers; ++q) {
            if (q == p) continue;
            const auto gv = guessVote(g, q, c, outs, true);
            if (gv.first == kAbstainId) continue;
            total[gv.first] += gv.second;
            all += gv.second;
        }
        const int w0 = winnerOf(g, w, c, outs, total);
        const float base = worth(g, w, c, w0, kAbstainId);
        float best = 0.f;
        for (int o : outs) best = std::max(best, worth(g, w, c, o, o) - base);
        const float matter = std::min(1.f, float(avail) / (0.35f * all + 1.f));  // can its votes swing it?
        sum += best * matter;
        ++counted;
    }
    return counted ? sum / float(counted) : 0.f;
}

}  // namespace

VoteStyle aiVoteStyle(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return VoteStyle::Splitter;
    const Personality& pe = g.players[size_t(p)].pers;
    if (pe.arch < 0 || pe.arch >= int(Arch::Count)) return VoteStyle::Splitter;
    const bool chaotic = pe.arch == int(Arch::Chaotic);
    const uint32_t roll = prand(g, 0x766F7465u /* "vote" */, uint32_t(p), chaotic ? uint32_t(g.round) : 0u, 0u, 100u);
    const int* wts = kStyleWeights[pe.arch];
    int acc = 0;
    for (int i = 0; i < 4; ++i) {
        acc += wts[i];
        if (int(roll) < acc) return VoteStyle(i);
    }
    return VoteStyle::Splitter;
}

const char* aiVoteStyleName(VoteStyle s) {
    switch (s) {
        case VoteStyle::Spender: return "Spender";
        case VoteStyle::Saver: return "Saver";
        case VoteStyle::Splitter: return "Splitter";
        case VoteStyle::Broker: return "Broker";
    }
    return "?";
}

bool aiVotePlanOn() { return aiTune("vplan", 1.f) > 0.f; }

VotePlan aiVotePlan(const Game& g, int p, int agenda, const std::vector<int>& outsIn, int avail, bool mustVote, int riderBet,
                    int riderCard, int predicted, int forceOutcome) {
    VotePlan plan;
    const std::vector<int> outs = outsIn.empty() ? outcomeIds(g, agenda) : outsIn;
    if (outs.empty()) return plan;
    const Params k = paramsFor(g, p);
    Who w;
    w.p = p;
    w.self = true;
    w.care = k.care;
    w.riderBet = riderBet;
    w.riderCard = riderCard;
    w.predicted = predicted;
    const Table t = readTable(g, p, agenda, outs);
    // what a vote kept for later is worth: nothing on the phase's last agenda (planets ready afterwards), real
    // resources under Executive Order (action phase), the next card's stake on the first agenda
    float lambda = 0.f, e2 = 0.f;
    if (g.phase != Phase::Agenda) {
        lambda = 0.12f;
    } else if (g.agendaNumber == 0) {
        e2 = nextAgendaStake(g, p, avail);
        lambda = k.save * 0.6f * e2 / float(std::max(3, avail));
    }
    const int zeal = isFaction(g, p, "argent") && g.phase == Phase::Agenda ? g.numPlayers : 0;
    // the likely winner without its votes, and what it would like instead
    auto at = [&](const std::map<int, float>& m, int id) {
        auto it = m.find(id);
        return it == m.end() ? 0.f : it->second;
    };
    std::map<int, float> proj;
    for (int o : outs) proj[o] = at(t.known, o) + at(t.coming, o);
    const int w0 = winnerOf(g, w, agenda, outs, proj);
    const float base = worth(g, w, agenda, w0, kAbstainId);
    std::vector<std::pair<float, int>> ranked;
    float lo = 1e9f;
    for (int o : outs) {
        ranked.push_back({worth(g, w, agenda, o, o), o});
        lo = std::min(lo, worth(g, w, agenda, o, kAbstainId));
    }
    std::sort(ranked.rbegin(), ranked.rend());
    const int fav = ranked[0].second;
    const float strength = ranked[0].first - lo;  // how much this agenda matters to it at all
    // votes needed for o to win (its own side's votes still to come counted at half - they may hold back; its rivals'
    // in full), and the outcome that wins if it casts `votes` for o
    auto needFor = [&](int o, bool pessimistic) {
        const float mine = at(t.known, o) + (pessimistic ? 0.5f : 1.f) * at(t.coming, o);
        float rival = 0.f;
        int rivalId = kAbstainId;
        for (int r : outs)
            if (r != o && proj[r] > rival) {
                rival = proj[r];
                rivalId = r;
            }
        bool tieOk = g.speaker == p;
        if (!tieOk && rivalId != kAbstainId) {
            Who s;
            s.p = g.speaker;
            tieOk = worth(g, s, agenda, o, kAbstainId) > worth(g, s, agenda, rivalId, kAbstainId);
        }
        return std::max(0.f, rival - mine + (tieOk ? 0.f : 1.f));
    };
    const int most = avail + (avail > 0 ? zeal : 0);
    Choice c;
    c.ifAbstain = w0;
    if (g.phase != Phase::Agenda) {
        // Executive Order (the action phase): the planets it exhausts cost it this round - just enough, or nothing
        c = bestChoice(g, w, agenda, outs, t, avail, lambda, k, zeal);
    } else if (g.agendaNumber >= 1) {
        // the phase's last agenda: the votes are free (every planet is ready again afterwards) - all of them for the
        // outcome it likes best among those it can carry, or for its favourite anyway when it has a clear preference
        int pick = kAbstainId;
        float pv = base + 0.02f;
        for (size_t i = 0; i < ranked.size() && i < 6; ++i) {
            const int o = ranked[i].second;
            const int winner = needFor(o, false) <= float(most) ? o : w0;
            const float v = worth(g, w, agenda, winner, o);
            if (v > pv) {
                pv = v;
                pick = o;
            }
        }
        if (pick == kAbstainId && strength >= 0.15f && worth(g, w, agenda, w0, fav) >= base) pick = fav;  // on the record
        c.outcome = pick;
        c.votes = pick == kAbstainId ? 0 : most;
        c.gain = pick == kAbstainId ? 0.f : pv - base;
        c.net = pick == kAbstainId ? 0.f : 1.f;
    } else {
        // the first agenda: a share of its votes that follows how much this one matters against the next (the style
        // sets how much it likes keeping votes), adjusted to the table
        const float styleMul = [&] {
            float m = 1.f;
            switch (aiVoteStyle(g, p)) {
                case VoteStyle::Spender: m = 1.4f; break;
                case VoteStyle::Saver: m = 0.6f; break;
                case VoteStyle::Splitter: m = 1.f; break;
                case VoteStyle::Broker: m = 0.8f; break;
            }
            return aiCompetitiveSeat(g, p) ? 1.f + (m - 1.f) * 0.35f : m;
        }();
        const float share = std::clamp(strength / (strength + k.save * e2 + 0.1f) * styleMul, 0.f, 1.f);
        int budget = strength >= 0.1f ? int(std::lround(share * float(avail))) : 0;
        if (debugOn() && g_aiSeat == p)
            std::fprintf(stderr, "[vote]   first agenda: strength %.2f share %.2f budget %d fav %d w0 %d\n", strength, share, budget, fav, w0);
        // which outcome: its favourite, or - if that is out of reach even with all its votes - the best it can carry
        int o = fav;
        if (needFor(fav, false) > float(most)) {
            o = kAbstainId;
            float pv = base + 0.1f;
            for (size_t i = 1; i < ranked.size() && i < 6; ++i)
                if (needFor(ranked[i].second, false) <= float(most) && ranked[i].first > pv) {
                    pv = ranked[i].first;
                    o = ranked[i].second;
                }
        }
        if (o != kAbstainId) {
            const float need = needFor(o, !t.last);
            const float cushion = t.last ? 0.f : k.cushion + std::ceil(0.25f * t.comingTotal);
            const float gain = worth(g, w, agenda, o, o) - base;
            const float knownLead = at(t.known, o) - [&] {
                float r = 0.f;
                for (int x : outs)
                    if (x != o) r = std::max(r, at(t.known, x));
                return r;
            }();
            int cast;
            if (t.last) {  // the last to vote: exactly what it takes (nothing when it wins anyway)
                cast = need <= 0.f ? 0 : int(std::ceil(need));
            } else if (knownLead > 0.6f * t.comingTotal + 1.f) {  // the votes already cast decide it: hold
                cast = 0;
            } else {
                cast = budget;
                // close, and worth more than what the votes would buy later: whatever it takes
                const int sure = int(std::ceil(need + cushion));
                if (sure > cast && sure <= most && gain > lambda * float(sure)) cast = sure;
            }
            cast = std::min(cast, most);
            // what it stands to lose by holding back: the outcome most likely to beat it (when nobody else has a side,
            // its own second choice) - the projected winner is only a guess while others still have to vote
            int rivalBest = kAbstainId;
            float rv = 0.f;
            for (int x : outs)
                if (x != o && proj[x] > rv) {
                    rv = proj[x];
                    rivalBest = x;
                }
            if (rivalBest == kAbstainId) rivalBest = ranked.size() > 1 ? (ranked[0].second == o ? ranked[1].second : ranked[0].second) : o;
            const float stake = worth(g, w, agenda, o, o) - worth(g, w, agenda, rivalBest, kAbstainId);
            if (cast > 0 && std::max(gain, stake) >= 0.15f) {
                c.outcome = o;
                c.votes = cast;
                c.gain = std::max(gain, stake);
                c.net = c.gain;
            }
        }
        // a vote that pays by itself (Mutiny / Unconventional Measures For, Articles of War For, Future Sight): the token
        if (c.outcome == kAbstainId) {
            Choice tok = bestChoice(g, w, agenda, outs, t, avail, lambda, k, zeal);
            if (tok.outcome != kAbstainId && tok.votes <= 2 && tok.net > 0.12f) c = tok;
        }
    }
    if (forceOutcome != kAbstainId) {  // a promised or forced vote: as many as it takes to carry that outcome
        auto at = [&](const std::map<int, float>& m, int id) {
            auto it = m.find(id);
            return it == m.end() ? 0.f : it->second;
        };
        float rival = 0.f;
        for (int o : outs)
            if (o != forceOutcome) rival = std::max(rival, at(t.known, o) + at(t.coming, o));
        const float mine = at(t.known, forceOutcome) + 0.5f * at(t.coming, forceOutcome);
        const float cushion = t.last ? 0.f : k.cushion + std::ceil(0.25f * t.comingTotal);
        c.outcome = forceOutcome;
        c.votes = std::max(1, int(std::ceil(rival - mine + 1.f + cushion)));
    }
    const float threshold = g.phase != Phase::Agenda ? 0.12f : 0.f;
    if (c.outcome == kAbstainId || (c.net < threshold && forceOutcome == kAbstainId)) {
        if (!mustVote) {
            plan.outcome = kAbstainId;
            plan.votes = 0;
        } else {  // must vote (Special Session, a grudge): the outcome it likes most, as few votes as it takes
            int best = outs[0];
            float bv = -1e9f;
            for (int o : outs) {
                const float v = worth(g, w, agenda, o, o);
                if (v > bv) {
                    bv = v;
                    best = o;
                }
            }
            plan.outcome = best;
            plan.votes = std::max(1, c.outcome == best ? c.votes : 1);
        }
    } else {
        plan.outcome = c.outcome;
        plan.votes = c.votes;
    }
    // habits: a Spender piles its votes on its first-agenda favourite even when it is winning anyway
    if (g.phase == Phase::Agenda && g.agendaNumber == 0 && k.pileOn > 0.05f && forceOutcome == kAbstainId &&
        (plan.outcome == kAbstainId || plan.outcome == c.ifAbstain)) {
        int fav = kAbstainId;
        float fv = -1e9f, second = -1e9f;
        for (int o : outs) {
            const float v = worth(g, w, agenda, o, o);
            if (v > fv) {
                second = fv;
                fv = v;
                fav = o;
            } else if (v > second) {
                second = v;
            }
        }
        if (fav == c.ifAbstain && fv - second >= 0.3f) {
            plan.outcome = fav;
            plan.votes = std::max(plan.votes, int(std::lround(k.pileOn * float(avail))));
        }
    }
    // (0.81.0) Absol's Agendas: "the player who cast the most votes" for the winning outcome is rewarded on most cards - enough votes to
    // be on top of that outcome's voters, when the reward is worth them
    {
        const int ea = realAgenda(g, agenda);
        if (forceOutcome == kAbstainId && ea >= 0 && kAgendas[ea].source == Source::Absol) {
            const int target = plan.outcome != kAbstainId ? plan.outcome : c.ifAbstain;
            const float R = target != kAbstainId ? absolTopReward(g, p, ea, target) * w.care + 0.f : 0.f;
            if (R > 0.05f && worth(g, w, agenda, target, target) >= worth(g, w, agenda, target, kAbstainId) - 0.05f) {
                float top = 0.f;  // the most any other voter has cast (or is likely to cast) for it
                bool after = false;
                for (int q : voteOrder(g)) {
                    if (q == p) {
                        after = true;
                        continue;
                    }
                    if (!after) {
                        if (q < int(g.votedFor.size()) && g.votedFor[size_t(q)] == target) top = std::max(top, float(g.voteCount[size_t(q)]));
                    } else {
                        const auto gv = guessVote(g, q, agenda, outs, g.agendaNumber >= 1);
                        if (gv.first == target) top = std::max(top, gv.second + 1.f);  // (they may cast a little more)
                    }
                }
                const int needTop = int(std::floor(top)) + (g.speaker == p ? 0 : 1);
                const int cur = plan.outcome == target ? plan.votes : 0;
                if (needTop > cur && needTop <= most && R - lambda * float(needTop - cur) > 0.f) {
                    plan.outcome = target;
                    plan.votes = needTop;
                }
            }
        }
    }
    // the phase's last agenda: nothing to save the votes for - all of them (they are ready again afterwards)
    if (plan.outcome != kAbstainId && g.phase == Phase::Agenda && g.agendaNumber >= 1) plan.votes = std::max(plan.votes, avail + zeal);
    plan.votes = std::min(plan.votes, std::max(1, avail + zeal));
    plan.planetVotes = std::max(plan.outcome == kAbstainId ? 0 : 1, plan.votes - zeal);
    plan.gain = c.gain;
    plan.keepWorth = e2;
    plan.ifAbstain = c.ifAbstain;
    if (debugOn() && g_aiSeat == p) {
        std::fprintf(stderr, "[vote] r%d a%d %-14s %-8s care %.2f avail %2d %s E2 %.2f lam %.3f known/coming %.0f/%.0f%s -> %s %d (gain %.2f)%s\n",
                     g.round, g.agendaNumber, playerName(g, p).c_str(), aiVoteStyleName(aiVoteStyle(g, p)), k.care, avail,
                     kAgendas[realAgenda(g, agenda)].key, e2, lambda,
                     [&] {
                         float s = 0.f;
                         for (const auto& kv : t.known) s += kv.second;
                         return s;
                     }(),
                     t.comingTotal, t.last ? " last" : "", plan.outcome == kAbstainId ? "ABSTAIN" : std::to_string(plan.outcome).c_str(),
                     plan.votes, c.gain, forceOutcome != kAbstainId ? " (promised/forced)" : "");
    }
    return plan;
}

float aiVoteStake(const Game& g, int p, int agenda) {
    if (agenda < 0 || cantVote(g, p)) return 0.f;
    const std::vector<int> outs = outcomeIds(g, agenda);
    if (outs.empty()) return 0.f;
    const Params k = paramsFor(g, p);
    Who w;
    w.p = p;
    w.self = true;
    w.care = k.care;
    const Table t = readTable(g, p, agenda, outs);
    const Choice c = bestChoice(g, w, agenda, outs, t, votesOf(g, p), 0.f, k, 0);
    return c.outcome == kAbstainId ? 0.f : std::max(0.f, c.gain);
}

float aiVoteWorth(const Game& g, int p, int agenda, int winner, int myVote) {
    Who w;
    w.p = p;
    w.self = true;
    w.care = paramsFor(g, p).care;
    return worth(g, w, agenda, winner, myVote);
}

int aiVoteLikelyWinner(const Game& g, int p, int agenda) {
    const std::vector<int> outs = outcomeIds(g, agenda);
    if (outs.empty()) return kAbstainId;
    const Table t = readTable(g, p, agenda, outs);
    std::map<int, float> proj;
    for (int o : outs) {
        float v = 0.f;
        auto i1 = t.known.find(o);
        if (i1 != t.known.end()) v += i1->second;
        auto i2 = t.coming.find(o);
        if (i2 != t.coming.end()) v += i2->second;
        proj[o] = v;
    }
    Who w;
    w.p = p;
    return winnerOf(g, w, agenda, outs, proj);
}

float aiVoteSaveWeight(const Game& g, int p) { return paramsFor(g, p).save; }

// Planets to exhaust for `want` votes: the fewest votes at or over it (all of them when they don't reach it).
std::vector<int> aiVotePlanets(const Game& g, int p, const std::vector<int>& planets, int want, bool exec) {
    std::vector<int> vals;
    int total = 0;
    for (int x : planets) {
        const int v = exec ? std::max(voteValue(g, p, x), planetResources(g, x)) : voteValue(g, p, x);
        vals.push_back(v);
        total += v;
    }
    std::vector<int> out;
    if (want <= 0) return out;
    if (want >= total) {
        for (size_t i = 0; i < planets.size(); ++i)
            if (vals[i] > 0) out.push_back(planets[i]);
        return out;
    }
    // subset sums: reach[s] = the planet that first reached sum s (-1 none), from[s] = the sum before it
    const int n = int(planets.size());
    std::vector<int> reach(size_t(total + 1), -2), from(size_t(total + 1), -1);
    reach[0] = -1;
    for (int i = 0; i < n; ++i) {
        if (vals[size_t(i)] <= 0) continue;
        for (int s = total - vals[size_t(i)]; s >= 0; --s)
            if (reach[size_t(s)] != -2 && reach[size_t(s + vals[size_t(i)])] == -2) {
                reach[size_t(s + vals[size_t(i)])] = i;
                from[size_t(s + vals[size_t(i)])] = s;
            }
    }
    int s = want;
    while (s <= total && reach[size_t(s)] == -2) ++s;
    if (s > total) return out;
    // (each sum is reached once, using planets with a lower index than the one that reached it - no planet twice)
    while (s > 0) {
        out.push_back(planets[size_t(reach[size_t(s)])]);
        s = from[size_t(s)];
    }
    return out;
}

}  // namespace ti
