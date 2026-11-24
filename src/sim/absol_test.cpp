// Absol's Agendas (0.81.0): every card of the module, each of its outcomes, played on the engine (TI4_UITEST_ONLY=absol).
// A game (6 players unless the case says otherwise) is played by the computer; at round 1's strategy phase the custodians are taken
// off (so there is an agenda phase), the card is put on top of the agenda deck and the case's own set-up runs (laws in play, a
// scored secret, ...). Every player then votes for the outcome under test with all their planets (the last seat abstains, so the
// "voted Against or abstained" parts happen too); the game goes on into round 2's action phase (the strategy phase's effects:
// exhausted strategy cards, Imperial Arbiter, Checks and Balances, pending effects). Each case reports: no refused answer, no broken
// invariant, the card revealed and resolved, and its own check of the board before / after. TI4_ABSOL_ONLY=key runs one card;
// TI4_ABSOL_DEBUG=1 prints the log of each case.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "ai/ai.h"
#include "engine/data.h"
#include "engine/engine.h"
#include "engine/query.h"
#include "engine/state.h"

namespace ti {
std::string simInvariants(const Game& g);  // sim/simulate.cpp
}

using namespace ti;

namespace {
struct AbCase {
    std::string name;
    const char* key = "";
    int outcome = 0;     // For 0 / Against 1; for an election -1 = the first option
    int players = 6;
    uint32_t seed = 9100;
    std::vector<const char*> also;  // agendas stacked after it (the first revealed first)
    std::function<void(Game&)> setUp;
    std::function<void(Game&)> atReveal;  // run when the card comes up (before the vote)
    bool allAbstain = false;             // every seat abstains (the speaker picks the outcome: the case's)
    std::function<bool(const Game&, const Game&, int, std::string&)> check;  // (before, after, the outcome resolved, why)
};
struct AbResult {
    bool setUp = false, seen = false, resolved = false;
    int outcome = -1;
    Game before, after;
    std::vector<std::string> log;
    std::string refused, broken;
    int agendasVoted = 0;
};

AbResult runCase(const AbCase& c, uint32_t seed) {
    AbResult r;
    Engine e;
    Setup s;
    s.players = c.players;
    s.human = -1;
    s.autoplay = true;
    s.seed = seed;
    s.draft = 0;
    s.modules = kModCodex | kModAbsol;
    const char* six[] = {"letnev", "hacan", "sol", "jolnar", "sardakk", "l1z1x", "xxcha", "naalu"};
    for (int i = 0; i < c.players; ++i) s.factions.push_back(findFaction(six[i]));
    e.start(s);
    Rng rng(5);
    const int target = findAgenda(c.key);
    bool set = false;
    int seenNumber = -1, steps = 0;
    for (; e.waiting() && steps < 200000; ++steps) {
        const Game& g = e.game();
        const Decision& d = e.decision();
        if (!set && g.phase == Phase::Strategy) {
            set = true;
            Game& gm = e.g();
            gm.custodiansRemoved = true;
            std::vector<const char*> stack{c.key};
            for (const char* k : c.also) stack.push_back(k);
            for (const char* k : stack) {
                const int16_t a = int16_t(findAgenda(k));
                gm.agendaDeck.erase(std::remove(gm.agendaDeck.begin(), gm.agendaDeck.end(), a), gm.agendaDeck.end());
                gm.agendaDiscard.erase(std::remove(gm.agendaDiscard.begin(), gm.agendaDiscard.end(), a), gm.agendaDiscard.end());
            }
            for (auto it = stack.rbegin(); it != stack.rend(); ++it) gm.agendaDeck.push_back(int16_t(findAgenda(*it)));
            if (c.setUp) c.setUp(gm);
            r.setUp = true;
        }
        if (set && !r.seen && g.phase == Phase::Agenda && g.currentAgenda == target) {
            r.seen = true;
            seenNumber = g.agendaNumber;
            if (c.atReveal) c.atReveal(e.g());
            r.before = e.game();
        }
        if (r.seen && !r.resolved && (g.agendaNumber > seenNumber || g.phase != Phase::Agenda)) {
            r.resolved = true;
            r.after = e.game();
        }
        if (r.resolved && g.round >= 3 && g.phase == Phase::Strategy) break;  // (through round 2: the laws at work)
        if (g.winner >= 0) break;
        Answer a = aiAnswer(g, d, rng);
        auto has = [&](int id) {
            for (const Option& op : d.options)
                if (op.id == id && op.enabled) return true;
            return false;
        };
        if (set && r.seen && !r.resolved && g.phase == Phase::Agenda && g.agendaNumber == seenNumber) {
            if (d.tag == Tag::PlayNote || d.tag == Tag::Negotiate) {
                a = Answer{};
                a.picks = {-1};
            } else if (d.tag == Tag::VoteOutcome) {
                int o = c.outcome;
                if (o < 0)
                    for (const Option& op : d.options)
                        if (op.id != -100 && op.enabled) {
                            o = op.id;
                            break;
                        }
                if (d.player == g.numPlayers - 1 || c.allAbstain) o = -100;  // (the last seat abstains)
                if (!has(o)) o = -100;
                if (has(o)) {
                    a = Answer{};
                    a.picks = {o};
                    if (o != -100) r.outcome = o;
                }
            } else if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
            } else if (d.tag == Tag::TieBreak) {
                const int want = r.outcome >= 0 ? r.outcome : c.outcome;
                if (c.allAbstain) r.outcome = c.outcome;
                for (const Option& op : d.options)
                    if (op.id == want) {
                        a = Answer{};
                        a.picks = {op.id};
                    }
            }
        }
        if (set && !r.seen && d.tag == Tag::AgendaPeek) {  // (Politics: the stacked order stays)
            a = Answer{};
            a.picks = {1};
        }
        std::string why;
        if (!e.answer(a, &why)) {
            r.refused = d.prompt + " [" + why + "]";
            break;
        }
        for (const Event& ev : e.takeEvents()) {
            if (set && !ev.text.empty()) r.log.push_back(ev.text);
            if (set && ev.kind == EventKind::Agenda && e.game().round <= 1) ++r.agendasVoted;
        }
        if (set) {
            const std::string inv = simInvariants(e.game());
            if (!inv.empty() && r.broken.empty()) r.broken = inv + " (after: " + d.prompt + ")";
        }
    }
    if (!r.resolved && r.seen) r.after = e.game();
    return r;
}

const Law* lawIn(const Game& g, const char* key) {
    for (const Law& l : g.laws)
        if (!std::strcmp(kAgendas[l.agenda].key, key)) return &l;
    return nullptr;
}
int vpSum(const Game& g) {
    int v = 0;
    for (const Player& p : g.players) v += p.vp;
    return v;
}
}  // namespace

int absolTests(const std::string& outDir) {
    (void)outDir;
    const char* only = std::getenv("TI4_ABSOL_ONLY");
    const bool debug = std::getenv("TI4_ABSOL_DEBUG") != nullptr;
    std::vector<AbCase> cases;
    // every card: For and Against (an election: its first outcome)
    for (int a = 0; a < kAgendaCount; ++a) {
        if (kAgendas[a].source != Source::Absol || kAgendas[a].elect == Elect::None) continue;
        const bool fa = kAgendas[a].elect == Elect::ForAgainst;
        for (int o : fa ? std::vector<int>{0, 1} : std::vector<int>{-1}) {
            AbCase c;
            c.key = kAgendas[a].key;
            c.outcome = o;
            c.name = std::string(kAgendas[a].name) + (fa ? (o == 0 ? " (For)" : " (Against)") : "");
            cases.push_back(c);
        }
    }
    // set-ups the cards need
    auto giveLaw = [](const char* key) {
        return [key](Game& g) {
            Law l;
            l.agenda = int16_t(findAgenda(key));
            g.laws.push_back(l);
            g.agendaDeck.erase(std::remove(g.agendaDeck.begin(), g.agendaDeck.end(), l.agenda), g.agendaDeck.end());
        };
    };
    for (AbCase& c : cases) {
        const std::string k = c.key;
        if (k == "absol_miscount") c.setUp = giveLaw("absol_regulations");
        if (k == "absol_abolishment" || k == "absol_constitution") c.setUp = giveLaw("absol_sanctions");
        if (k == "absol_leak")
            c.setUp = [](Game& g) {  // a scored secret objective
                for (int o = 0; o < kObjectiveCount; ++o)
                    if (kObjectives[o].kind == 3) {
                        g.players[1].scored.push_back(int16_t(o));
                        g.players[1].vp = int8_t(g.players[1].vp + 1);
                        g.secretDeck.erase(std::remove(g.secretDeck.begin(), g.secretDeck.end(), int16_t(o)), g.secretDeck.end());
                        break;
                    }
            };
        if (k == "absol_schematics")
            c.setUp = [](Game& g) {
                const int t = findTech("dd2");
                if (t >= 0 && !g.players[2].hasTech(t)) g.players[2].techs.push_back(int16_t(t));
            };
        if (k == "absol_affirmation" && c.outcome == 1)
            c.setUp = [](Game& g) { g.custodiansBy = 0; };
    }
    // Emergency Session: one more agenda after the two
    {
        AbCase c;
        c.key = "absol_measures";
        c.outcome = 0;
        c.name = "Emergency Session (3 agendas this phase)";
        c.also = {"absol_emergency1", "absol_incentive", "absol_secret"};
        c.check = [](const Game&, const Game&, int, std::string&) { return true; };
        cases.push_back(c);
    }
    // (Absol's rulebook 8.14b) a player with no votes to cast did not abstain: Unconventional Measures' Against ("each player who
    // voted Against or abstained discards their action cards") leaves their hand alone
    {
        AbCase c;
        c.key = "absol_measures";
        c.outcome = 1;
        c.name = "No votes to cast is not abstaining (Measures)";
        c.atReveal = [](Game& g) {
            for (PlanetState& pl : g.planets)
                if (pl.owner == 2) pl.exhausted = true;
            if (g.players[2].actionCards.empty() && !g.actionDeck.empty()) {
                g.players[2].actionCards.push_back(g.actionDeck.back());
                g.actionDeck.pop_back();
            }
        };
        c.check = [](const Game& b, const Game& a, int, std::string& why) {
            if (b.players[2].actionCards.empty()) return why = "set-up: no card in hand", false;
            if (a.players[2].actionCards.empty()) return why = "the player who couldn't vote discarded their hand", false;
            if (!b.players[5].actionCards.empty() && !a.players[5].actionCards.empty())
                return why = "the player who abstained kept their hand", false;
            return true;
        };
        cases.push_back(c);
    }
    // (8.14c / the FAQ) nobody votes: the speaker counts as the top voter - Clandestine Operations (For): 2 command tokens
    {
        AbCase c;
        c.key = "absol_operations";
        c.outcome = 0;
        c.allAbstain = true;
        c.name = "All abstain: the speaker is the top voter";
        c.check = [](const Game& b, const Game& a, int o, std::string& why) {
            if (o != 0) return true;  // (the speaker chose Against)
            const Player& pb = b.players[size_t(b.speaker)];
            const Player& pa = a.players[size_t(b.speaker)];
            // (+2, then the player to their right removes 1)
            if (pa.tactic + pa.fleet + pa.strategy < pb.tactic + pb.fleet + pb.strategy + 1) return why = "the speaker gained no tokens", false;
            return true;
        };
        cases.push_back(c);
    }
    // (the FAQ) Pax Magnifica's Against skips Emergency Session: the card stays for the next agenda phase
    {
        AbCase c;
        c.key = "absol_pax";
        c.outcome = 1;
        c.name = "Pax Against keeps the extra-agenda card";
        c.setUp = [](Game& g) {
            const int16_t em = int16_t(findAgenda("absol_emergency1"));
            g.agendaDeck.erase(std::remove(g.agendaDeck.begin(), g.agendaDeck.end(), em), g.agendaDeck.end());
            g.agendaDiscard.erase(std::remove(g.agendaDiscard.begin(), g.agendaDiscard.end(), em), g.agendaDiscard.end());
            g.agendaDeck.push_back(em);  // (on top: revealed first, set aside)
        };
        c.check = [](const Game&, const Game& a, int o, std::string& why) {
            if (o != 1) return true;
            if (a.emergency < 1) return why = "Emergency Session was discarded", false;
            return true;
        };
        cases.push_back(c);
    }
    // (owner 2026-11-21) Absol's Political Secret: its holder exhausts up to 3 of the owner's planets for votes, then returns it
    {
        AbCase c;
        c.key = "absol_conscription";
        c.outcome = 0;
        c.name = "Political Secret (Absol's): votes from its owner's planets";
        c.atReveal = [](Game& g) {
            const int16_t ps = makeNote(3, kNotePoliticalSecret);
            for (Player& pl : g.players) pl.notes.erase(std::remove(pl.notes.begin(), pl.notes.end(), ps), pl.notes.end());
            g.players[1].notes.push_back(ps);
        };
        c.check = [](const Game& b, const Game& a, int, std::string& why) {
            const int16_t ps = makeNote(3, kNotePoliticalSecret);
            auto holds = [&](const Game& g, int q) { return std::find(g.players[size_t(q)].notes.begin(), g.players[size_t(q)].notes.end(), ps) != g.players[size_t(q)].notes.end(); };
            if (!holds(b, 1)) return why = "set-up: player 2 doesn't hold it", false;
            if (holds(a, 1) || !holds(a, 3)) return why = "the note wasn't played and returned", false;
            return true;
        };
        cases.push_back(c);
    }
    // Checks and Balances in a 3-player game (2 strategy cards each)
    {
        AbCase c;
        c.key = "absol_checks";
        c.outcome = 0;
        c.players = 3;
        c.name = "Checks and Balances (For, 3 players)";
        cases.push_back(c);
    }

    int bad = 0, done = 0;
    for (AbCase& c : cases) {
        if (only && !std::strstr(c.key, only) && !std::strstr(c.name.c_str(), only)) continue;
        AbResult r;
        for (uint32_t t = 0; t < 6; ++t) {
            r = runCase(c, c.seed + t * 131);
            if (r.setUp && r.seen && r.resolved && r.refused.empty() && r.broken.empty()) break;
        }
        std::string why;
        bool ok = r.setUp && r.seen && r.resolved && r.refused.empty() && r.broken.empty();
        if (!r.seen) why = "never revealed";
        else if (!r.resolved) why = "not resolved";
        else if (!r.refused.empty()) why = "refused: " + r.refused;
        else if (!r.broken.empty()) why = "invariant: " + r.broken;
        // the card's own check
        if (ok) {
            const std::string k = c.key;
            const Game& b = r.before;
            const Game& a = r.after;
            const bool law = kAgendas[findAgenda(c.key)].law;
            const bool fa = kAgendas[findAgenda(c.key)].elect == Elect::ForAgainst;
            if (c.check) {
                ok = c.check(b, a, r.outcome, why);
            } else if (law && (!fa || r.outcome == 0)) {
                ok = lawIn(a, c.key) != nullptr || k == "absol_affirmation";
                if (!ok) why = "the law isn't in play";
                if (ok && kAgendas[findAgenda(c.key)].elect == Elect::Player && lawIn(a, c.key) && lawIn(a, c.key)->owner != r.outcome &&
                    k != "absol_prophecy") {
                    ok = false;
                    why = "the law isn't held by the elected player";
                }
            } else if (law && fa && r.outcome == 1) {
                ok = k == "absol_affirmation" ? (lawIn(a, c.key) && lawIn(a, c.key)->against) : lawIn(a, c.key) == nullptr;
                if (!ok) why = k == "absol_affirmation" ? "Affirmation's Against side isn't in play" : "the law is in play after Against";
            }
            if (ok && k == "absol_mutiny" && r.outcome == 0 && vpSum(a) <= vpSum(b)) ok = false, why = "nobody gained a point";
            if (ok && k == "absol_seeds" && vpSum(a) <= vpSum(b)) ok = false, why = "nobody gained a point";
            if (ok && k == "absol_equality" && r.outcome == 0)
                for (int q = 0; q < a.numPlayers; ++q)
                    if (commodityValue(a, q) != 3) ok = false, why = "a commodity value isn't 3";
            if (ok && (k == "absol_hyperlane" || k == "absol_limits" || k == "absol_revolution" || k == "absol_pax" || k == "absol_operations") &&
                r.outcome == 1 && !a.scLocked)
                ok = false, why = "no strategy card exhausted";
            if (ok && (k == "absol_mutiny" || k == "absol_pax") && r.outcome == 0 && !a.scLocked) ok = false, why = "no strategy card exhausted";
            if (ok && k == "absol_checks" && r.outcome == 1)
                for (const Player& p : a.players)
                    if (!p.scored.empty()) ok = false, why = "objectives still scored";
            if (ok && k == "absol_artifact" && r.outcome == 1 && !a.mecArtifact) ok = false, why = "not attached to Mecatol Rex";
            if (ok && k == "absol_incentive" && a.publicObjectives.size() <= b.publicObjectives.size()) ok = false, why = "no new objective";
            if (ok && k == "absol_regulations" && r.outcome == 0)
                for (int q = 0; q < a.numPlayers; ++q)
                    if (a.players[size_t(q)].fleet > fleetPoolCap(a, q)) ok = false, why = "a fleet pool above the cap";
            if (ok && c.name.find("Emergency") != std::string::npos && r.agendasVoted < 3)
                ok = false, why = "only " + std::to_string(r.agendasVoted) + " agendas voted on";
        }
        std::printf("absol: %-48s %s%s\n", c.name.c_str(), ok ? "OK" : "WRONG - ", ok ? "" : why.c_str());
        if (debug || !ok)
            for (const std::string& l : r.log)
                if (debug) std::printf("   | %s\n", l.c_str());
        bad += ok ? 0 : 1;
        ++done;
    }
    std::printf("absol: %d cases, %d wrong\n", done, bad);
    return bad == 0 ? 1 : 0;
}
