// Monuments+ (0.82.0): every monument's ability played on the engine, with its timing checked (TI4_UITEST_ONLY=montests).
//
// A game (6 players: the monument's faction in seat 0) is played by the computer. At the start of round 2's strategy phase (or
// round 1's for the agenda cases - the custodians taken off so there is an agenda phase) the case puts the monument on the
// board and sets up what its ability needs (ships next to it, tokens, a relic...). From then on the computer answers, except where
// the case steers a question (take Construction, activate that system, vote for that outcome, use the ability on that target).
// Every question asked is recorded with the phase, the round, the active player and the events before it, so a case can check
// WHEN its ability came (right after the pass, at the start of a turn before the action is chosen, after the activation before
// any movement, after the outcome is resolved, ...) as well as WHAT it did. Each case reports: no refused answer, no broken
// invariant, its own check. TI4_MON_ONLY=name-part runs some; TI4_MON_DEBUG=1 prints each case's log.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai/ai.h"
#include "engine/data.h"
#include "engine/deal.h"
#include "engine/engine.h"
#include "engine/query.h"
#include "engine/state.h"

namespace ti {
std::string simInvariants(const Game& g);  // sim/simulate.cpp
}

using namespace ti;

namespace {
struct Step {  // one question asked after the set-up
    Tag tag;
    DecisionKind kind;
    int player, value, ability, round, active;
    Phase phase;
    size_t events;      // how many events came before it (MonRun::log)
    std::vector<int> answer;
    std::string prompt;
    std::vector<int> offered;  // (Pick: the option ids)
    std::string context;       // (0.82.0: the question's explanation - the vote hints)
    int amount = 0;            // (0.82.2: Decision::amount - a payment's cost)
    std::vector<int> data;     // (0.82.2: Answer::data - a move plan)
};
struct LogLine {
    EventKind kind;
    int player;
    std::string text;
    int unit = -1, card = -1, count = 0, hits = 0;  // (0.82.0: a monument's notice - unit Monument, count 1 / 2, amount)
};
struct MonRun {
    int seat = 0;
    bool setUp = false;
    Game before, after;
    std::vector<Step> steps;
    std::vector<LogLine> log;
    std::string refused, broken;
    std::vector<Game> snaps;      // the game right before each monument question (the case may compare)
    std::vector<Game> dealSnaps;  // ... and before each answer to a deal
};
struct MonCase {
    std::string name;
    const char* faction = "";
    const char* second = nullptr;  // (0.82.0) seat 1's faction (else the usual six)
    uint32_t events = 0;           // (0.82.0) Thunder's Edge galactic events (Setup::events)
    uint32_t seed = 9300;
    int modules = kModCodex | kModMonuments;
    bool agenda = false;  // set up at round 1 with the custodians off (an agenda phase at the end of round 1)
    int untilRound = 3;   // stop at this round's strategy phase
    std::function<bool(Game&, int)> setUp;                                        // false = can't be set up on this map
    std::function<bool(const Game&, const Decision&, int, Answer&)> drive;        // true = answered here
    std::function<bool(const MonRun&, std::string&)> check;                       // why it failed
};

// ---------------------------------------------------------------- set-up helpers
int fac(const char* k) { return findFaction(k); }
bool homeSys(const Game& g, int s) { return g.systems[size_t(s)].home >= 0; }
void clearPlanet(Game& g, int x) {
    PlanetState& ps = g.planets[size_t(x)];
    for (int q = 0; q < kSides; ++q) ps.units[size_t(q)] = Force();
    ps.coexist = 0;
}
// the seat's monument on planet x, the planet theirs (ready), nobody else's units on it
void removeMonuments(Game& g, int seat) {
    for (SystemState& sy : g.systems) sy.space[size_t(seat)].remove(UnitType::Monument, sy.space[size_t(seat)].count(UnitType::Monument), true);
    for (PlanetState& ps : g.planets) ps.units[size_t(seat)].remove(UnitType::Monument, ps.units[size_t(seat)].count(UnitType::Monument), true);
}
void monumentOn(Game& g, int seat, int x) {
    removeMonuments(g, seat);
    clearPlanet(g, x);
    PlanetState& ps = g.planets[size_t(x)];
    ps.owner = int8_t(seat);
    ps.exhausted = false;
    ps.units[size_t(seat)].add(UnitType::Monument, 1);
    ps.units[size_t(seat)].add(UnitType::Infantry, 1);
}
// a planet the seat's monument may stand on, not in a home system or Mecatol Rex's, preferring ones passing 'also'
int monumentPlanet(const Game& g, int seat, std::function<bool(int)> also = nullptr) {
    int fallback = -1;
    for (size_t x = 0; x < g.planets.size(); ++x) {
        const int s = g.planets[x].system;
        if (homeSys(g, s) || g.systems[size_t(s)].offBoard || !monumentFitsPlanet(g, seat, int(x)) || isDMZ(g, int(x))) continue;
        if (!also || also(int(x))) return int(x);
        if (fallback < 0) fallback = int(x);
    }
    return also ? -1 : fallback;
}
bool hasAdjWith(const Game& g, int sys, std::function<bool(int)> f) {
    for (int16_t a : g.systems[size_t(sys)].adj)
        if (f(a)) return true;
    return false;
}
int adjWith(const Game& g, int sys, std::function<bool(int)> f) {
    for (int16_t a : g.systems[size_t(sys)].adj)
        if (f(a)) return a;
    return -1;
}
int otherSeat(const Game& g, int seat) { return (seat + 1) % g.numPlayers; }
std::vector<int> nearSystems7(const Game& g, int s) {  // (0.82.4) the system and those next to it (no hyperlanes)
    std::vector<int> v{s};
    for (int16_t a : g.systems[size_t(s)].adj)
        if (!g.systems[size_t(a)].isHyperlane() && std::find(v.begin(), v.end(), int(a)) == v.end()) v.push_back(a);
    return v;
}
// (0.82.0) Monuments+'s cards: an action card / a secret objective into the seat's hand, an agenda on top of the deck
void giveCard(Game& g, int seat, const char* k) {
    const int c = findActionCard(k);
    auto it = std::find(g.actionDeck.begin(), g.actionDeck.end(), int16_t(c));
    if (it != g.actionDeck.end()) g.actionDeck.erase(it);
    for (Player& pl : g.players) {  // (the only copy)
        auto h = std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(c));
        if (h != pl.actionCards.end()) pl.actionCards.erase(h);
    }
    g.players[size_t(seat)].actionCards.push_back(int16_t(c));
}
void giveSecret(Game& g, int seat, const char* k) {
    const int o = findObjective(k);
    auto it = std::find(g.secretDeck.begin(), g.secretDeck.end(), int16_t(o));
    if (it != g.secretDeck.end()) g.secretDeck.erase(it);
    for (Player& pl : g.players) {
        auto h = std::find(pl.secrets.begin(), pl.secrets.end(), int16_t(o));
        if (h != pl.secrets.end()) pl.secrets.erase(h);
    }
    g.players[size_t(seat)].secrets.push_back(int16_t(o));
}
void agendaOnTop(Game& g, const char* k) {
    const int a = findAgenda(k);
    auto it = std::find(g.agendaDeck.begin(), g.agendaDeck.end(), int16_t(a));
    if (it != g.agendaDeck.end()) g.agendaDeck.erase(it);
    g.agendaDeck.push_back(int16_t(a));  // (the top is the back)
}
bool scored(const Game& g, int seat, const char* k) {
    const int o = findObjective(k);
    for (int16_t s : g.players[size_t(seat)].scored)
        if (s == o) return true;
    return false;
}
// a law to the table directly (as resolved)
void lawOn(Game& g, const char* k, int target) {
    Law l;
    l.agenda = int16_t(findAgenda(k));
    l.target = int16_t(target);
    g.laws.push_back(l);
}
int monCard(const char* k) { return monumentIndex(k); }
bool steps(const MonRun& r, std::function<bool(const Step&)> f) {
    for (const Step& s : r.steps)
        if (f(s)) return true;
    return false;
}
const Step* firstMon(const MonRun& r, int ask) {
    for (const Step& s : r.steps)
        if (s.tag == Tag::MonumentAbility && s.value == ask) return &s;
    return nullptr;
}
bool logHas(const MonRun& r, const std::string& part, size_t from = 0) {
    for (size_t i = from; i < r.log.size(); ++i)
        if (r.log[i].text.find(part) != std::string::npos) return true;
    return false;
}
// the events between two indices contain one of this kind
bool kindBetween(const MonRun& r, EventKind k, size_t a, size_t b) {
    for (size_t i = a; i < b && i < r.log.size(); ++i)
        if (r.log[i].kind == k) return true;
    return false;
}
// index (into r.steps) of a step
size_t stepIndex(const MonRun& r, const Step* s) { return size_t(s - &r.steps[0]); }
Answer one(int id) {
    Answer a;
    a.picks = {id};
    return a;
}
bool offers(const Decision& d, int id) {
    for (const Option& o : d.options)
        if (o.id == id && o.enabled) return true;
    return false;
}

MonRun runCase(const MonCase& c) {
    MonRun r;
    Engine e;
    Setup s;
    s.players = 6;
    s.human = -1;
    s.autoplay = true;
    s.seed = c.seed;
    s.draft = 0;
    s.modules = c.modules;
    s.events = c.events;
    const char* six[] = {"letnev", "hacan", "sol", "jolnar", "sardakk", "l1z1x", "xxcha", "naalu", "mentak", "muaat", "yin", "winnu"};
    s.factions.push_back(fac(c.faction));
    if (c.second) s.factions.push_back(fac(c.second));
    for (const char* k : six)
        if (int(s.factions.size()) < s.players && std::strcmp(k, c.faction) && (!c.second || std::strcmp(k, c.second))) s.factions.push_back(fac(k));
    e.start(s);
    Rng rng(7);
    r.seat = 0;
    int steps = 0;
    for (; e.waiting() && steps < 300000; ++steps) {
        const Game& g = e.game();
        const Decision& d = e.decision();
        if (!r.setUp && g.phase == Phase::Strategy && g.round == (c.agenda ? 1 : 2)) {
            Game& gm = e.g();
            if (c.agenda) gm.custodiansRemoved = true;
            if (c.setUp && !c.setUp(gm, r.seat)) {
                r.refused = "(the case couldn't be set up on this map)";
                return r;
            }
            r.setUp = true;
            r.before = e.game();
        }
        if (r.setUp && g.round >= c.untilRound && g.phase == Phase::Strategy) break;
        if (g.winner >= 0) break;
        Answer a;
        bool driven = r.setUp && c.drive && c.drive(g, d, r.seat, a);
        if (!driven) a = aiAnswer(g, d, rng);
        if (r.setUp) {
            Step st{d.tag, d.kind, d.player, d.value, d.ability, g.round, g.activePlayer, g.phase, r.log.size(), a.picks, d.prompt, {}, d.context, d.amount, a.data};
            for (const Option& o : d.options)
                if (o.enabled) st.offered.push_back(o.id);
            r.steps.push_back(st);
            if (d.tag == Tag::MonumentAbility || d.tag == Tag::MonumentPlace) r.snaps.push_back(g);
            if (d.tag == Tag::DealResponse) {  // (only deals with units in them)
                Deal dl;
                if (decodeDeal(d.deal, dl) && (!dl.give.units.empty() || !dl.get.units.empty())) r.dealSnaps.push_back(g);
            }
        }
        std::string why;
        if (!e.answer(a, &why)) {
            if (driven) {  // (a steered answer that doesn't fit: the computer's instead)
                a = aiAnswer(g, d, rng);
                if (!r.steps.empty()) r.steps.back().answer = a.picks;
                if (e.answer(a, &why)) goto answered;
            }
            r.refused = d.prompt + " [" + why + "]";
            break;
        }
    answered:
        for (const Event& ev : e.takeEvents())
            if (r.setUp && !ev.text.empty()) r.log.push_back({ev.kind, ev.player, ev.text, ev.unit, ev.card, ev.count, ev.hits});
        if (r.setUp && (e.game().phase != Phase::Action || e.game().activeSystem < 0)) {  // (not in the middle of a tactical action)
            const std::string inv = simInvariants(e.game());
            if (!inv.empty() && r.broken.empty()) r.broken = inv + " (after: " + d.prompt + ")";
        }
    }
    r.after = e.game();
    return r;
}

// ---------------------------------------------------------------- steering helpers
// take a tactical action activating 'sys' on the seat's turns of round r (once)
struct Activation {
    int sys = -1;
    bool done = false;
};
}  // namespace

int monumentTests(const std::string& outDir) {
    (void)outDir;
    const char* only = std::getenv("TI4_MON_ONLY");
    const bool debug = std::getenv("TI4_MON_DEBUG") != nullptr;
    std::vector<MonCase> cases;

    // ---- Flaah Orbitals (Arborec): when they pass - an infantry on a planet in or next to it; empty: theirs, others' units: coexist
    for (int variant = 0; variant < 2; ++variant) {
        MonCase c;
        c.name = variant == 0 ? "Flaah Orbitals: pass -> infantry, control" : "Flaah Orbitals: pass -> infantry, coexistence";
        c.faction = "arborec";
        auto target = std::make_shared<int>(-1);
        c.setUp = [target, variant](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) {
                const int s = g.planets[size_t(x)].system;
                return hasAdjWith(g, s, [&](int a) { return !g.systems[size_t(a)].planets.empty() && !homeSys(g, a) && a != g.mecatol(); });
            });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            const int a = adjWith(g, s, [&](int a) { return !g.systems[size_t(a)].planets.empty() && !homeSys(g, a) && a != g.mecatol(); });
            *target = g.systems[size_t(a)].planets[0];
            clearPlanet(g, *target);
            PlanetState& ps = g.planets[size_t(*target)];
            if (variant == 0) {
                ps.owner = -1;
            } else {
                const int q = otherSeat(g, seat);
                ps.owner = int8_t(q);
                ps.units[size_t(q)].add(UnitType::Infantry, 2);
            }
            return true;
        };
        c.drive = [target](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 99)) return a = one(99), true;  // pass as soon as allowed
            if (d.tag == Tag::MonumentAbility && d.value == kMonAskArborec && offers(d, *target)) return a = one(*target), true;
            return false;
        };
        c.check = [target, variant](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskArborec);
            if (!m) return why = "no question", false;
            // timing: right after the seat's pass (their last ActionChoice was 99), before the next turn began
            const Step* pass = nullptr;
            for (const Step& s : r.steps) {
                if (&s == m) break;
                if (s.tag == Tag::ActionChoice && s.player == r.seat) pass = &s;
            }
            if (!pass || pass->answer.empty() || pass->answer[0] != 99) return why = "not after their pass", false;
            if (kindBetween(r, EventKind::TurnStart, pass->events, m->events)) return why = "after the next turn began", false;
            // what it did, by what was on the planet when asked (others' units: coexist; none: control)
            size_t k = 0;
            for (const Step& s : r.steps) {
                if (&s == m) break;
                if (s.tag == Tag::MonumentAbility || s.tag == Tag::MonumentPlace) ++k;
            }
            if (m->answer.empty() || k >= r.snaps.size()) return why = "no answer", false;
            // (the rules pass, fixes 43: the card says "place 1 infantry" - no "No infantry", Living Rules 1.8)
            if (std::find(m->offered.begin(), m->offered.end(), -1) != m->offered.end()) return why = "\"No infantry\" offered", false;
            const int x = m->answer[0];
            const PlanetState& at = r.snaps[k].planets[size_t(x)];
            bool others = false;
            for (int q = 0; q < kSides; ++q)
                if (q != r.seat && !at.units[size_t(q)].empty()) others = true;
            const bool coexist = logHas(r, "Arborec's units coexist on " + std::string(kPlanets[at.def].name), m->events);
            const bool control = logHas(r, "Arborec takes control of " + std::string(kPlanets[at.def].name), m->events);
            if (others && !coexist && at.owner != r.seat) return why = "not coexisting with the units there", false;
            if (!others && at.owner != r.seat && !control) return why = "the empty planet wasn't taken", false;
            if (variant == 1 && x != *target) return why = "another planet was chosen", false;
            for (int o : m->offered)
                if (o >= 0 && r.before.planets[size_t(o)].system == r.before.mecatol()) return why = "Mecatol Rex offered", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Phoenix Cauldron (Argent): ACTION - a hit against each ship with SUSTAIN DAMAGE in a system without their ships
    {
        MonCase c;
        c.name = "Phoenix Cauldron: action, hits on SUSTAIN DAMAGE ships";
        c.faction = "argent";
        auto tgt = std::make_shared<int>(-1);
        c.setUp = [tgt](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) {
                return hasAdjWith(g, g.planets[size_t(x)].system, [&](int a) { return !homeSys(g, a) && !hasShips(g, seat, a); });
            });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *tgt = adjWith(g, g.planets[size_t(x)].system, [&](int a) { return !homeSys(g, a) && !hasShips(g, seat, a); });
            const int q = otherSeat(g, seat);
            for (int o = 0; o < kSides; ++o) g.systems[size_t(*tgt)].space[size_t(o)] = Force();
            Force& f = g.systems[size_t(*tgt)].space[size_t(q)];
            f.add(UnitType::Dreadnought, 2);
            f.dmg[int(UnitType::Dreadnought)] = 1;
            f.add(UnitType::Cruiser, 1);
            return true;
        };
        c.drive = [tgt](const Game& g, const Decision& d, int seat, Answer& a) {
            const int opt = 3100 + monCard("argent");
            if (d.player == seat && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, opt)) return a = one(opt), true;
            if (d.tag == Tag::MonumentAbility && d.value == kMonAskArgent && offers(d, *tgt)) return a = one(*tgt), true;
            return false;
        };
        c.check = [tgt](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskArgent);
            if (!m) return why = "no question", false;
            const Game& g = r.snaps[0];
            const int q = otherSeat(g, r.seat);
            // look right after the action: the next snapshot isn't there - use the log
            if (!logHas(r, "loses 1 Dreadnought (Phoenix Cauldron)", m->events)) return why = "the damaged dreadnought wasn't destroyed", false;
            // the card is exhausted: not offered again this round
            const int opt = 3100 + monCard("argent");
            for (size_t i = stepIndex(r, m) + 1; i < r.steps.size(); ++i) {
                const Step& s = r.steps[i];
                if (s.round != m->round) break;
                if (s.tag == Tag::ActionChoice && s.player == r.seat && std::find(s.offered.begin(), s.offered.end(), opt) != s.offered.end())
                    return why = "offered again while exhausted", false;
            }
            if (!logHas(r, "dreadnought sustains damage", m->events) && !logHas(r, "Dreadnought sustains damage", m->events))
                return why = "the other dreadnought didn't sustain damage", false;
            (void)q;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Revenance Circuit (Creuss): after a player activates its system - 1 hit in wormhole systems next to it
    {
        MonCase c;
        c.name = "Revenance Circuit: activation -> hits through wormholes";
        c.faction = "ghost";
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) {
                return hasAdjWith(g, g.planets[size_t(x)].system, [&](int a) { return wormholesOf(g, a) && !homeSys(g, a); });
            });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            const int w = adjWith(g, s, [&](int a) { return wormholesOf(g, a) && !homeSys(g, a); });
            *at = {s, w};
            const int q = otherSeat(g, seat);
            for (int o = 0; o < kSides; ++o) g.systems[size_t(w)].space[size_t(o)] = Force();
            g.systems[size_t(w)].space[size_t(q)].add(UnitType::Destroyer, 2);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Cruiser, 1);
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(at->first)].hasToken(seat)) return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, at->first)) return a = one(at->first), true;
            if (d.tag == Tag::MonumentAbility && d.value == kMonAskCreuss) return a = one(at->second), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskCreuss);
            if (!m) return why = "no question", false;
            // timing: after the activation of its system, before any movement question
            const Step* act = nullptr;
            for (const Step& s : r.steps) {
                if (&s == m) break;
                if (s.tag == Tag::Activate) act = &s;
            }
            if (!act || act->answer.empty() || act->answer[0] != at->first) return why = "not after its system's activation", false;
            for (const Step* s = act + 1; s < m; ++s)
                if (s->kind == DecisionKind::Move) return why = "after movement", false;
            if (!logHas(r, "loses 1 destroyer", m->events) && !logHas(r, "loses 1 Destroyer", m->events))
                return why = "no destroyer lost in the wormhole system", false;
            // never after another system's activation
            const Step* lastAct = nullptr;
            for (const Step& s : r.steps) {
                if (s.tag == Tag::Activate) lastAct = &s;
                if (s.tag == Tag::MonumentAbility && s.value == kMonAskCreuss &&
                    (!lastAct || lastAct->answer.empty() || lastAct->answer[0] != at->first))
                    return why = "asked after another system's activation", false;
            }
            return true;
        };
        cases.push_back(c);
    }

    // ---- The Panopticon (Empyrean): blockaded -> destroyed (any time); with their own ship there it stands
    for (int variant = 0; variant < 2; ++variant) {
        MonCase c;
        c.name = variant == 0 ? "The Panopticon: blockaded -> destroyed" : "The Panopticon: guarded -> stands";
        c.faction = "empyrean";
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, variant](Game& g, int seat) {
            for (size_t s = 0; s < g.systems.size() && *at < 0; ++s)
                if (monumentFitsSpace(g, seat, int(s)) && !g.systems[s].offBoard) *at = int(s);
            if (*at < 0) return false;
            removeMonuments(g, seat);
            for (int o = 0; o < kSides; ++o) g.systems[size_t(*at)].space[size_t(o)] = Force();
            g.systems[size_t(*at)].space[size_t(seat)].add(UnitType::Monument, 1);
            g.systems[size_t(*at)].space[size_t(otherSeat(g, seat))].add(UnitType::Destroyer, 1);
            if (variant == 1) g.systems[size_t(*at)].space[size_t(seat)].add(UnitType::Dreadnought, 1);
            return true;
        };
        c.untilRound = 3;
        c.check = [at, variant](const MonRun& r, std::string& why) {
            const bool there = r.after.systems[size_t(*at)].space[size_t(r.seat)].count(UnitType::Monument) > 0 ||
                               monumentSystem(r.after, r.seat) >= 0;
            if (variant == 0) {
                if (there && r.after.systems[size_t(*at)].space[size_t(r.seat)].count(UnitType::Monument)) return why = "still there", false;
                if (!logHas(r, "blockaded")) return why = "no blockade line", false;
                // timing: before the second question after the set-up
                if (r.log.empty() || r.steps.size() < 1) return why = "nothing ran", false;
                bool early = false;
                const size_t until = r.steps.size() > 1 ? r.steps[1].events : r.log.size();
                for (size_t i = 0; i < r.log.size() && i < until; ++i)
                    if (r.log[i].text.find("blockaded") != std::string::npos) early = true;
                if (!early) return why = "not at once (before the next question)", false;
                return true;
            }
            // guarded: it stands (until their ships there are gone - a later fight may do that)
            const size_t until = r.steps.size() > 2 ? r.steps[2].events : r.log.size();
            for (size_t i = 0; i < r.log.size() && i < until; ++i)
                if (r.log[i].text.find("blockaded") != std::string::npos) return why = "destroyed although guarded", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Mowshir Freeport (Hacan): a ship given in a deal - replaced by the receiver's own where their ships and a token are
    {
        MonCase c;
        c.name = "Mowshir Freeport: a destroyer traded in a deal";
        c.faction = "hacan";
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (monument system, receiver)
        c.setUp = [at](Game& g, int seat) {
            int q = -1;
            for (int o = 0; o < g.numPlayers && q < 0; ++o)
                if (o != seat && areNeighbours(g, seat, o)) q = o;
            if (q < 0) return false;
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Destroyer, 1);
            g.systems[size_t(g.players[size_t(q)].homeSystem)].tokens |= uint16_t(1u << q);  // (their ships at home + a token)
            *at = {s, q};
            return true;
        };
        auto tried = std::make_shared<bool>(false), offered = std::make_shared<bool>(false);
        c.drive = [at, tried, offered](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player == seat && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 650) && !*tried) {
                *tried = true;
                return a = one(650), true;
            }
            if (d.player == seat && d.tag == Tag::Negotiate && *tried && !*offered && offers(d, at->second)) return a = one(at->second), true;
            if (d.player == seat && d.kind == DecisionKind::Deal && d.tag == Tag::DealOffer && *tried && !*offered) {
                *offered = true;
                Deal deal;
                deal.from = seat;
                deal.to = at->second;
                deal.give.units = {at->first * 16 + int(UnitType::Destroyer)};
                a = Answer{};
                a.data = encodeDeal(deal);
                return true;
            }
            if (d.player == at->second && d.tag == Tag::DealResponse) return a = one(1), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            if (!logHas(r, "replaces the units from")) return why = "no replacement line", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Calm Seas Sanatorium (Jol-Nar), Qanoj Shield Array (Xxcha): after an outcome they voted for is resolved
    for (const char* k : {"jolnar", "xxcha"}) {
        MonCase c;
        const bool jol = !std::strcmp(k, "jolnar");
        c.name = jol ? "Calm Seas Sanatorium: voted outcome -> 2 infantry" : "Qanoj Shield Array: voted outcome -> a structure";
        c.faction = k;
        c.agenda = true;
        c.untilRound = 2;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = x;
            return true;
        };
        c.drive = [at, jol](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.tag == Tag::VoteOutcome) {  // everyone votes for the first outcome
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            if (d.player == seat && d.tag == Tag::MonumentAbility && jol && d.value == kMonAskJolnar && offers(d, *at * 4 + 2))
                return a = one(*at * 4 + 2), true;
            if (d.player == seat && d.tag == Tag::MonumentAbility && !jol && d.value == kMonAskXxcha) return a = one(1), true;
            return false;
        };
        c.check = [at, jol](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, jol ? kMonAskJolnar : kMonAskXxcha);
            if (!m) return why = "no question", false;
            if (m->phase != Phase::Agenda) return why = "not in the agenda phase", false;
            // timing: after an outcome's result, before the next agenda is revealed
            size_t lastResult = 0;
            bool any = false;
            for (size_t i = 0; i < m->events && i < r.log.size(); ++i)
                if (r.log[i].kind == EventKind::AgendaResult) lastResult = i, any = true;
            if (!any) return why = "before any outcome was resolved", false;
            if (kindBetween(r, EventKind::Agenda, lastResult, m->events)) return why = "after the next agenda was revealed", false;
            if (jol && !logHas(r, "places 2 infantry on", m->events)) return why = "no infantry placed", false;
            if (!jol && !logHas(r, "uses Qanoj Shield Array", m->events)) return why = "not used", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- L1Z1X: at the start of their turn - a command token from a system next to it into its system
    {
        MonCase c;
        c.name = "L1Z1X monument: start of turn -> a token moved";
        c.faction = "l1z1x";
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            const int a = g.systems[size_t(s)].adj.empty() ? -1 : g.systems[size_t(s)].adj[0];
            if (a < 0) return false;
            const int q = otherSeat(g, seat);
            g.systems[size_t(a)].tokens |= uint16_t(1u << q);
            *at = {s, a};
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            const int q = otherSeat(g, seat);
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskL1z1x && offers(d, at->second * 16 + q))
                return a = one(at->second * 16 + q), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskL1z1x);
            if (!m) return why = "no question", false;
            if (m->active != r.seat || m->phase != Phase::Action) return why = "not on their turn", false;
            // timing: after their turn began, before they chose an action
            for (size_t i = stepIndex(r, m); i-- > 0;) {
                const Step& s = r.steps[i];
                if (s.tag == Tag::ActionChoice && s.player == r.seat && s.round == m->round && s.active == r.seat) {
                    if (!kindBetween(r, EventKind::TurnStart, s.events, m->events)) return why = "after the action was chosen", false;
                    break;
                }
            }
            if (!logHas(r, "command token from", m->events)) return why = "no token moved", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Palatine Obelisk (Letnev): PRODUCTION doubled in its system; votes: + their non-fighter ships there
    {
        MonCase c;
        c.name = "Palatine Obelisk: doubled PRODUCTION, extra votes";
        c.faction = "letnev";
        c.agenda = true;
        c.untilRound = 2;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::SpaceDock, 1);
            g.systems[size_t(g.planets[size_t(x)].system)].space[size_t(seat)].add(UnitType::Dreadnought, 2);
            *at = x;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            (void)seat;
            if (d.tag == Tag::VoteOutcome) {
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Game& g = r.before;
            const int s = g.planets[size_t(*at)].system;
            Game without = g;
            without.planets[size_t(*at)].units[size_t(r.seat)].remove(UnitType::Monument, 1, true);
            const int with = productionCapacity(g, r.seat, s), base = productionCapacity(without, r.seat, s);
            if (with != 2 * base || base <= 0) return why = "production " + std::to_string(with) + " vs " + std::to_string(base), false;
            if (!logHas(r, "more vote") || !logHas(r, "(Palatine Obelisk)")) return why = "no extra votes", false;
            // (owner M18) said in the vote question before choosing, and shown large afterwards (a notice: votes, how many)
            bool hinted = false, notice = false;
            for (const Step& s : r.steps)
                if (s.player == r.seat && s.tag == Tag::VoteOutcome && s.context.find("Palatine Obelisk: exhaust") != std::string::npos) hinted = true;
            for (const LogLine& l : r.log)
                if (l.text.find("(Palatine Obelisk)") != std::string::npos && l.unit == int(UnitType::Monument) && l.count == 2 && l.hits >= 1) notice = true;
            if (!hinted) return why = "the vote question doesn't mention it", false;
            if (!notice) return why = "the votes aren't marked as a notice", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Spire of Ixth (Mahact): at the start of the status phase - another player's token from its system into the fleet pool
    {
        MonCase c;
        c.name = "Spire of Ixth: status phase start -> Edict token";
        c.faction = "mahact";
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = g.planets[size_t(x)].system;
            g.systems[size_t(*at)].tokens |= uint16_t(1u << otherSeat(g, seat));
            g.players[size_t(seat)].edict = 0;
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskMahact && offers(d, otherSeat(g, seat)))
                return a = one(otherSeat(g, seat)), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskMahact);
            if (!m) return why = "no question", false;
            if (m->phase != Phase::Status) return why = "not in the status phase", false;
            // timing: the first thing of the status phase (before any objective is scored or card drawn)
            size_t start = 0;
            for (size_t i = 0; i < m->events && i < r.log.size(); ++i)
                if (r.log[i].kind == EventKind::Phase && r.log[i].text.find("Status") != std::string::npos) start = i;
            if (kindBetween(r, EventKind::ObjectiveScored, start, m->events)) return why = "after scoring", false;
            if (!((r.after.players[size_t(r.seat)].edict >> otherSeat(r.after, r.seat)) & 1)) return why = "no Edict token", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Erwan's Fist (Mentak): at the start of a player's turn - SUSTAIN DAMAGE ships there become damaged
    {
        MonCase c;
        c.name = "Erwan's Fist: start of a turn -> ships damaged";
        c.faction = "mentak";
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = g.planets[size_t(x)].system;
            g.systems[size_t(*at)].space[size_t(otherSeat(g, seat))].add(UnitType::Dreadnought, 2);
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskMentak && offers(d, *at)) return a = one(*at), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskMentak);
            if (!m) return why = "no question", false;
            // timing: a turn began and no action was chosen since
            size_t ts = 0;
            bool any = false;
            for (size_t i = 0; i < m->events && i < r.log.size(); ++i)
                if (r.log[i].kind == EventKind::TurnStart) ts = i, any = true;
            if (!any) return why = "not at the start of a turn", false;
            for (const Step& s : r.steps) {
                if (&s == m) break;
                if (s.tag == Tag::ActionChoice && s.events >= ts && s.round == m->round) return why = "after an action was chosen", false;
            }
            if (!logHas(r, "with SUSTAIN DAMAGE is damaged", m->events)) return why = "no damage", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Glory Furnace (Muaat): votes with its planet - per war sun: 3 votes or a hit
    {
        MonCase c;
        c.name = "Glory Furnace: votes per war sun";
        c.faction = "muaat";
        c.agenda = true;
        c.untilRound = 2;
        c.setUp = [](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.systems[size_t(g.planets[size_t(x)].system)].space[size_t(seat)].add(UnitType::WarSun, 1);
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.tag == Tag::VoteOutcome) {
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskMuaat) return a = one(0), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "casts 3 more votes (Glory Furnace)")) return why = "no extra votes", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- G'hom Firefly Beacon (N'orr): placed by Construction -> 3 infantry / fighters; end of turn -> replaced by a structure
    {
        MonCase c;
        c.name = "G'hom Firefly Beacon: placed -> 3 units, then replaced";
        c.faction = "sardakk";
        c.setUp = [](Game& g, int seat) {
            // a non-home planet of theirs where it may stand (so Construction can place it)
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            removeMonuments(g, seat);
            clearPlanet(g, x);
            g.planets[size_t(x)].owner = int8_t(seat);
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::Infantry, 1);
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat && d.tag == Tag::StrategyPick && g.round == 2 && offers(d, 3)) {  // (Construction left for them)
                for (const Option& o : d.options)
                    if (o.enabled && o.id != 3) return a = one(o.id), true;
            }
            if (d.player != seat) return false;
            if (d.tag == Tag::StrategyPick && g.round == 2 && offers(d, 3)) return a = one(3), true;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 13)) return a = one(13), true;
            if (d.tag == Tag::MonumentPlace)
                for (const Option& o : d.options)
                    if (o.id >= 0 && o.enabled) return a = one(o.id), true;
            if (d.tag == Tag::MonumentAbility && d.value == kMonAskNorrReplace && offers(d, int(UnitType::SpaceDock)))
                return a = one(int(UnitType::SpaceDock)), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "calls: up to 3")) return why = "not placed / no call", false;
            int made = 0;
            for (const LogLine& l : r.log)
                if (l.text.find("(G'hom Firefly Beacon)") != std::string::npos && l.text.find("produces") != std::string::npos) ++made;
            if (made != 3) return why = std::to_string(made) + " units produced", false;
            const Step* rep = firstMon(r, kMonAskNorrReplace);
            if (!rep) return why = "no replace question", false;
            if (!logHas(r, "replaces G'hom Firefly Beacon with a")) return why = "not replaced", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Valefar Assimilator "M" (Nekro): with "M" on Erwan's Fist the Nekro's monument has its text (and PRODUCTION 2)
    {
        MonCase c;
        c.name = "Valefar Assimilator \"M\": a copied card works";
        c.faction = "nekro";
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            g.players[size_t(seat)].nekroMon[0] = int8_t(monCard("mentak"));
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = g.planets[size_t(x)].system;
            g.systems[size_t(*at)].space[size_t(otherSeat(g, seat))].add(UnitType::Dreadnought, 1);
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskMentak && offers(d, *at)) return a = one(*at), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            if (!firstMon(r, kMonAskMentak)) return why = "no copied question", false;
            if (unitDef(r.before, r.seat, UnitType::Monument).production != 2) return why = "PRODUCTION not copied", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Lodestar (Nomad): start of their turn - the monument returns, the flagship is produced there
    {
        MonCase c;
        c.name = "Lodestar: start of turn -> flagship produced there";
        c.faction = "nomad";
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = g.planets[size_t(x)].system;
            for (SystemState& s : g.systems) s.space[size_t(seat)].remove(UnitType::Flagship, s.space[size_t(seat)].count(UnitType::Flagship), true);
            for (int y : controlledPlanets(g, seat)) g.planets[size_t(y)].exhausted = false;
            g.players[size_t(seat)].tradeGoods = 8;
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskNomad) return a = one(1), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskNomad);
            if (!m) return why = "no question", false;
            if (m->active != r.seat) return why = "not on their turn", false;
            if (!logHas(r, "produces their flagship in", m->events)) return why = "no flagship", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Beacon of Ragh (Saar): its system adjacent to one of their space docks (movement)
    {
        MonCase c;
        c.name = "Beacon of Ragh: adjacent to a space dock";
        c.faction = "saar";
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) {
                const int s = g.planets[size_t(x)].system;
                return s != g.players[size_t(seat)].homeSystem && !hasAdjWith(g, s, [&](int a) { return a == g.players[size_t(seat)].homeSystem; });
            });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = g.planets[size_t(x)].system;
            return true;
        };
        c.untilRound = 2;
        c.check = [at](const MonRun& r, std::string& why) {
            const Game& g = r.after;
            const int dock = g.players[size_t(r.seat)].saarBeacon;
            if (dock < 0) return why = "no dock chosen", false;
            const std::vector<int> nb = routeNeighbors(g, r.seat, *at);
            if (std::find(nb.begin(), nb.end(), dock) == nb.end()) return why = "not adjacent", false;
            const int other = otherSeat(g, r.seat);
            const std::vector<int> nb2 = routeNeighbors(g, other, *at);
            if (std::find(nb2.begin(), nb2.end(), dock) != nb2.end() && !hasAdjWith(g, *at, [&](int a) { return a == dock; }))
                return why = "adjacent for another player too", false;
            return true;
        };
        // (the dock is chosen at the start of their turns: run into round 2's action phase)
        c.untilRound = 3;
        cases.push_back(c);
    }

    // ---- The Cenotaph (Sol): infantry placed elsewhere may come to its planet instead (exhausts the card)
    {
        MonCase c;
        c.name = "The Cenotaph: placed infantry go to its planet";
        c.faction = "sol";
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = x;
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskSol) {
                int most = 0;
                for (const Option& o : d.options) most = std::max(most, o.id);
                return a = one(most), true;
            }
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskSol);
            if (!m) return why = "no question (no infantry placed?)", false;
            const size_t i = stepIndex(r, m);
            if (i + 1 >= r.steps.size()) return why = "nothing after", false;
            // the next snapshot (if any) or the log: the infantry went to the Cenotaph's planet
            if (!logHas(r, "instead (The Cenotaph)", m->events)) return why = "not placed there", false;
            if (firstMon(r, kMonAskSol) != m) return why = "?", false;
            int asked = 0;
            for (const Step& s : r.steps)
                if (s.tag == Tag::MonumentAbility && s.value == kMonAskSol && s.round == m->round) ++asked;
            if (asked > 1) return why = "asked again in the same round (the card is exhausted)", false;
            (void)at;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Scepter of Ul (Titans): ACTION - a sleeper token on a planet with a trait in or next to its system
    {
        MonCase c;
        c.name = "Scepter of Ul: action -> sleeper token";
        c.faction = "titans";
        c.setUp = [](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            for (PlanetState& ps : g.planets) ps.sleeper = false;
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            const int opt = 3100 + monCard("titans");
            if (d.player == seat && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, opt)) return a = one(opt), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!firstMon(r, kMonAskTitans)) return why = "no question", false;
            if (!logHas(r, "places a sleeper token on")) return why = "no sleeper", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Invasions: She Consumes Worlds (Vuil'raith) DEPLOY + capture; Nefishh Projector (Naalu) coexist; The Cenotaph on a commit
    // (0.82.0, owner 2026-11-22) "+": neutral units there instead - a neutral PDS captured (M15), neutral infantry coexisted with (M7)
    for (const char* k : {"cabal", "naalu", "sol", "cabal+", "naalu+"}) {
        MonCase c;
        const std::string full = k;
        const bool neutral = full.back() == '+';
        const std::string key = neutral ? full.substr(0, full.size() - 1) : full;
        c.name = key == "cabal" ? (neutral ? "She Consumes Worlds: a neutral PDS captured" : "She Consumes Worlds: DEPLOY, structures captured")
                 : key == "naalu" ? (neutral ? "Nefishh Projector: coexist with neutral infantry" : "Nefishh Projector: committed infantry coexist")
                                  : "The Cenotaph: committed infantry go to its planet";
        c.faction = key == "cabal" ? "cabal" : key == "naalu" ? "naalu" : "sol";
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (target planet, its system)
        c.setUp = [at, key, neutral](Game& g, int seat) {
            const int q = otherSeat(g, seat);
            // the monument (Vuil'raith: in the reinforcements - DEPLOY)
            int mx = -1;
            removeMonuments(g, seat);
            if (key != "cabal") {
                mx = monumentPlanet(g, seat);
                if (mx < 0) return false;
                monumentOn(g, seat, mx);
            }
            // a target planet next to (or in) the Naalu monument's system, not home, not Mecatol Rex, not the monument's
            int y = -1;
            for (size_t x = 0; x < g.planets.size() && y < 0; ++x) {
                const int s = g.planets[x].system;
                if (int(x) == mx || homeSys(g, s) || s == g.mecatol() || g.systems[size_t(s)].offBoard || g.systems[size_t(s)].planets.size() != 1)
                    continue;
                if (key == "naalu") {
                    const int ms = g.planets[size_t(mx)].system;
                    if (s != ms && !hasAdjWith(g, ms, [&](int a) { return a == s; })) continue;
                    if (s == ms) continue;
                }
                y = int(x);
            }
            if (y < 0) return false;
            const int s = g.planets[size_t(y)].system;
            clearPlanet(g, y);
            for (int o = 0; o < kSides; ++o) g.systems[size_t(s)].space[size_t(o)] = Force();
            PlanetState& ps = g.planets[size_t(y)];
            ps.owner = int8_t(neutral ? -1 : q);
            const int who = neutral ? kNeutral : q;
            if (key == "cabal") ps.units[size_t(who)].add(neutral ? UnitType::PDS : UnitType::SpaceDock, 1);
            else ps.units[size_t(who)].add(UnitType::Infantry, 2);
            Force& f = g.systems[size_t(s)].space[size_t(seat)];
            f.add(UnitType::Carrier, 1);
            f.add(UnitType::Dreadnought, 1);
            f.add(UnitType::Infantry, 3);
            g.systems[size_t(s)].tokens = uint16_t(g.systems[size_t(s)].tokens & ~(1u << seat));
            *at = {y, s};
            return true;
        };
        c.drive = [at, key](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(at->second)].hasToken(seat)) return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, at->second)) return a = one(at->second), true;
            if (d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == at->second) {
                a = Answer{};
                a.data = {at->first, int(UnitType::Infantry), g.systems[size_t(at->second)].space[size_t(seat)].count(UnitType::Infantry)};
                return true;
            }
            if (d.tag == Tag::MonumentAbility && key == "cabal" && d.value == kMonAskCabal) return a = one(1), true;
            if (d.tag == Tag::MonumentAbility && key == "naalu" && d.value == kMonAskNaalu) return a = one(1), true;
            if (d.tag == Tag::MonumentAbility && key == "sol" && d.value == kMonAskSolCommit) {
                int most = 0;
                for (const Option& o : d.options) most = std::max(most, o.id);
                return a = one(most), true;
            }
            return false;
        };
        c.check = [at, key, neutral](const MonRun& r, std::string& why) {
            const int ask = key == "cabal" ? kMonAskCabal : key == "naalu" ? kMonAskNaalu : kMonAskSolCommit;
            const Step* m = firstMon(r, ask);
            if (!m) return why = "no question", false;
            if (key == "cabal") {
                if (!logHas(r, "deploys She Consumes Worlds", m->events)) return why = "not deployed", false;
                if (!logHas(r, "captures", m->events)) return why = "nothing captured", false;
                if (neutral) {  // the neutral PDS kept on the faction sheet (owner M15)
                    const auto& cap = r.after.players[size_t(r.seat)].captured;
                    if (std::find(cap.begin(), cap.end(), int16_t(kNeutral * 16 + int(UnitType::PDS))) == cap.end()) return why = "the neutral PDS isn't captured", false;
                }
                return true;
            }
            // the question came in the invasion (after the landing), before any ground combat on that planet
            const Step* land = nullptr;
            for (const Step& s : r.steps) {
                if (&s == m) break;
                if (s.kind == DecisionKind::Land) land = &s;
            }
            if (!land) return why = "not after the commit", false;
            if (kindBetween(r, EventKind::CombatStart, land->events, m->events)) return why = "after a combat began", false;
            if (key == "naalu" && !logHas(r, "coexist on", m->events)) return why = "no coexistence", false;
            if (key == "sol" && !logHas(r, "instead (The Cenotaph)", m->events)) return why = "not moved", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- The Imperial Vault (Winnu): at the start of the agenda phase - off the board: 3 resources a trade good on the card
    {
        MonCase c;
        c.name = "The Imperial Vault: agenda phase start -> stored";
        c.faction = "winnu";
        c.agenda = true;
        c.untilRound = 2;
        c.setUp = [](Game& g, int seat) {
            for (int y : controlledPlanets(g, seat)) g.planets[size_t(y)].exhausted = false;
            g.players[size_t(seat)].tradeGoods = 6;
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskWinnuStore && offers(d, 1)) return a = one(1), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskWinnuStore);
            if (!m) return why = "no question", false;
            if (m->phase != Phase::Agenda) return why = "not in the agenda phase", false;
            if (kindBetween(r, EventKind::Agenda, 0, m->events) && r.log.size()) {
                // (the first agenda's reveal must come after it)
                for (size_t i = 0; i < m->events && i < r.log.size(); ++i)
                    if (r.log[i].kind == EventKind::Agenda && r.log[i].text.find("Agenda phase") == std::string::npos) return why = "after an agenda", false;
            }
            if (!logHas(r, "puts 1 trade good(s) on The Imperial Vault")) return why = "not stored", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- The Blessed Sanctum (Yin): ACTION - for every 2 infantry there, 1 moves to another planet of theirs
    {
        MonCase c;
        c.name = "The Blessed Sanctum: action -> infantry moved";
        c.faction = "yin";
        c.setUp = [](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::Infantry, 4);  // (5 there: 2 move)
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            const int opt = 3100 + monCard("yin");
            if (d.player == seat && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, opt)) return a = one(opt), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            int moved = 0;
            for (const LogLine& l : r.log)
                if (l.text.find("(The Blessed Sanctum)") != std::string::npos && l.text.find("moves an infantry") != std::string::npos) ++moved;
            if (moved != 2) return why = std::to_string(moved) + " moved", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Sector KVD-14 (Yssaril): another player with units near it plays an action card -> 2 trade goods
    {
        MonCase c;
        c.name = "Sector KVD-14: another player's action card -> 2 trade goods";
        c.faction = "yssaril";
        c.untilRound = 4;
        c.setUp = [](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            int i = 0;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != seat && i < int(g.systems[size_t(s)].adj.size()))
                    g.systems[size_t(g.systems[size_t(s)].adj[size_t(i++)])].space[size_t(q)].add(UnitType::Destroyer, 1);
            return true;
        };
        c.check = [](const MonRun& r, std::string& why) {
            // every "plays <card>" by another player with units near is followed at once by the 2 trade goods
            bool any = false;
            for (size_t i = 0; i < r.log.size(); ++i)
                if (r.log[i].kind == EventKind::ActionCard && r.log[i].player != r.seat) {
                    // (the gain comes right after the card is played, before Sabotage)
                    for (size_t j = i + 1; j < r.log.size() && j <= i + 2; ++j)
                        if (r.log[j].text.find("gains 2 trade goods (Sector KVD-14)") != std::string::npos) {
                            any = true;
                            // (owner M18) marked as a notice - shown large to a person: 2 trade goods
                            const LogLine& l = r.log[j];
                            if (l.unit != int(UnitType::Monument) || l.count != 1 || l.hits != 2 || l.player != r.seat)
                                return why = "the gain isn't marked as a notice", false;
                        }
                }
            if (!any) return why = "never paid", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Pharus Iustitiae (Keleres): after a law enters play a token onto the card; an activation next to it uses it
    {
        MonCase c;
        c.name = "Pharus Iustitiae: law -> token; activation uses it";
        c.faction = "keleres";
        c.agenda = true;
        c.untilRound = 3;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = g.planets[size_t(x)].system;
            g.systems[size_t(*at)].space[size_t(seat)].add(UnitType::Cruiser, 1);
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.tag == Tag::VoteOutcome) {
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            if (d.player != seat) return false;
            if (d.tag == Tag::MonumentAbility && (d.value == kMonAskKeleresLaw || d.value == kMonAskKeleresToken)) return a = one(1), true;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && g.players[size_t(seat)].monTokens > 0 &&
                !g.systems[size_t(*at)].hasToken(seat))
                return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, *at) && g.players[size_t(seat)].monTokens > 0) return a = one(*at), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const Step* law = firstMon(r, kMonAskKeleresLaw);
            if (!law) return why = "no law question (no law in the agenda phase?)", false;
            if (!logHas(r, "adds a command token to Pharus Iustitiae")) return why = "no token added", false;
            const Step* tok = firstMon(r, kMonAskKeleresToken);
            if (!tok) return why = "no token question", false;
            if (!logHas(r, "places a command token from Pharus Iustitiae in", tok->events)) return why = "token not used", false;
            // (the rules pass, fixes 43) the card's tokens came out of the reinforcements: pools + board + card never pass 16
            const Player& pl = r.after.players[size_t(r.seat)];
            int board = 0;
            for (const SystemState& sy : r.after.systems) board += sy.hasToken(r.seat) ? 1 : 0;
            if (pl.tactic + pl.fleet + pl.strategy + board + pl.monTokens > 16) return why = "more than 16 command tokens", false;
            return true;
        };
        cases.push_back(c);
    }

    // (2026-11-23, a tester's report: "a 2/0 planet should provide 1 vote" with the Xxcha commander) Elder Qanoj: "Each planet you
    // exhaust to cast votes provides 1 additional vote" - a planet with 0 influence, exhausted alone, casts 1 vote
    {
        MonCase c;
        c.name = "Elder Qanoj: a 0-influence planet casts 1 vote";
        c.faction = "xxcha";
        c.agenda = true;
        c.untilRound = 3;
        auto zero = std::make_shared<int>(-1);
        c.setUp = [zero](Game& g, int seat) {
            Player& pl = g.players[size_t(seat)];
            for (LeaderState& l : pl.leaders)
                if (l.leader >= 0 && kLeaders[l.leader].type == kCommander) l.status = kReady;
            for (int x = 0; x < int(g.planets.size()) && *zero < 0; ++x)
                if (g.planets[size_t(x)].owner < 0 && planetInfluence(g, x) == 0 && planetResources(g, x) > 0 && !isHomePlanet(g, x) &&
                    !isStation(g, x))
                    *zero = x;
            if (*zero < 0) return false;
            clearPlanet(g, *zero);
            g.planets[size_t(*zero)].owner = int8_t(seat);
            g.planets[size_t(*zero)].exhausted = false;
            g.planets[size_t(*zero)].units[size_t(seat)].add(UnitType::Infantry, 2);
            return commanderUsable(g, seat, "xxcha");
        };
        c.drive = [zero](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::UseTech && offers(d, 0)) return a = one(0), true;  // (no Predictive Intelligence votes)
            if (d.tag == Tag::VoteOutcome) {
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets && std::find(d.planets.begin(), d.planets.end(), *zero) != d.planets.end() &&
                !g.planets[size_t(*zero)].exhausted) {
                a = Answer{};
                a.picks = {*zero};  // that planet only
                return true;
            }
            return false;
        };
        c.check = [zero](const MonRun& r, std::string& why) {
            (void)zero;
            for (const LogLine& l : r.log)  // the seat's first vote: that planet alone
                if (l.kind == EventKind::Vote && l.player == r.seat)
                    return l.count == 1 ? true : (why = "first vote: " + l.text + " (expected 1 vote)", false);
            return why = "no vote cast", false;
        };
        cases.push_back(c);
    }

    // (the rules pass, fixes 43) Pharus Iustitiae's tokens are not in the reinforcements: with 15 on the sheet and the board and
    // 1 on the card, none is left - no "add a token" when a law enters play
    {
        MonCase c;
        c.name = "Pharus Iustitiae: the card's tokens aren't in the reinforcements";
        c.faction = "keleres";
        c.agenda = true;
        c.untilRound = 3;
        c.setUp = [](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            Player& pl = g.players[size_t(seat)];
            int board = 0;
            for (const SystemState& sy : g.systems) board += sy.hasToken(seat) ? 1 : 0;
            pl.monTokens = 1;
            pl.tactic = int8_t(15 - board - pl.fleet - pl.strategy);
            return pl.tactic >= 0;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.tag == Tag::VoteOutcome) {
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            (void)seat;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (r.after.laws.empty()) return why = "no law entered play", false;
            auto total = [&](const Game& g) {
                const Player& q = g.players[size_t(r.seat)];
                int board = 0;
                for (const SystemState& sy : g.systems) board += sy.hasToken(r.seat) ? 1 : 0;
                return q.tactic + q.fleet + q.strategy + board + q.monTokens;
            };
            // each "add a token" question came with a token left in the reinforcements (pools + board + card < 16)
            size_t k = 0;
            for (const Step& st : r.steps) {
                if (st.tag == Tag::MonumentAbility && st.value == kMonAskKeleresLaw && k < r.snaps.size() && total(r.snaps[k]) >= 16)
                    return why = "asked to add a token with none in the reinforcements", false;
                if (st.tag == Tag::MonumentAbility || st.tag == Tag::MonumentPlace) ++k;
            }
            const Player& pl = r.after.players[size_t(r.seat)];
            int board = 0;
            for (const SystemState& sy : r.after.systems) board += sy.hasToken(r.seat) ? 1 : 0;
            if (pl.tactic + pl.fleet + pl.strategy + board + pl.monTokens > 16) return why = "more than 16 command tokens", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Quantum Reliquary (Naaz-Rokha): a relic in a transaction with a player with units near it
    {
        MonCase c;
        c.name = "Quantum Reliquary: a relic traded";
        c.faction = "naaz";
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (relic, partner)
        c.setUp = [at](Game& g, int seat) {
            int q = -1;
            for (int o = 0; o < g.numPlayers && q < 0; ++o)
                if (o != seat && areNeighbours(g, seat, o)) q = o;
            if (q < 0) return false;
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.systems[size_t(g.planets[size_t(x)].system)].space[size_t(q)].add(UnitType::Destroyer, 1);
            g.systems[size_t(g.planets[size_t(x)].system)].space[size_t(seat)].add(UnitType::Destroyer, 1);
            int relic = -1;
            for (int rl = 0; rl < kRelicCount && relic < 0; ++rl)
                if (!std::strcmp(kRelics[rl].key, "shard")) relic = rl;
            if (relic < 0) return false;
            g.players[size_t(seat)].relics.push_back(int16_t(relic));
            *at = {relic, q};
            return true;
        };
        auto tried = std::make_shared<bool>(false), offered = std::make_shared<bool>(false);
        c.drive = [at, tried, offered](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player == seat && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 650) && !*tried) {
                *tried = true;
                return a = one(650), true;
            }
            if (d.player == seat && d.tag == Tag::Negotiate && *tried && !*offered && offers(d, at->second)) return a = one(at->second), true;
            if (d.player == seat && d.kind == DecisionKind::Deal && d.tag == Tag::DealOffer && *tried && !*offered) {
                *offered = true;
                Deal deal;
                deal.from = seat;
                deal.to = at->second;
                deal.give.relics = {at->first};
                a = Answer{};
                a.data = encodeDeal(deal);
                return true;
            }
            if (d.player == at->second && d.tag == Tag::DealResponse) return a = one(1), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            if (!r.after.players[size_t(at->second)].hasRelic(at->first)) return why = "the relic didn't change hands", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- edge cases -------------------------------------------------------------------------------------------------------
    // The Imperial Vault on Mecatol Rex: the trade goods on the card are taken at the start of the agenda phase
    {
        MonCase c;
        c.name = "The Imperial Vault: on the board -> trade goods taken";
        c.faction = "winnu";
        c.agenda = true;
        c.untilRound = 2;
        c.setUp = [](Game& g, int seat) {
            const int mec = g.mecatol();
            if (mec < 0 || g.systems[size_t(mec)].planets.empty()) return false;
            const int x = g.systems[size_t(mec)].planets[0];
            for (int o = 0; o < kSides; ++o) g.systems[size_t(mec)].space[size_t(o)] = Force();
            monumentOn(g, seat, x);
            g.players[size_t(seat)].monTG = 2;
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskWinnuTake && offers(d, 2)) return a = one(2), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskWinnuTake);
            if (!m) return why = "no question", false;
            if (m->phase != Phase::Agenda) return why = "not in the agenda phase", false;
            if (!logHas(r, "takes 2 trade good(s) from The Imperial Vault")) return why = "not taken", false;
            if (firstMon(r, kMonAskWinnuStore)) return why = "asked to store while on the board", false;
            return true;
        };
        cases.push_back(c);
    }
    // Glory Furnace: a hit instead of the votes
    {
        MonCase c;
        c.name = "Glory Furnace: a hit instead of votes";
        c.faction = "muaat";
        c.agenda = true;
        c.untilRound = 2;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = g.planets[size_t(x)].system;
            g.systems[size_t(*at)].space[size_t(seat)].add(UnitType::WarSun, 1);
            const int a = g.systems[size_t(*at)].adj.empty() ? -1 : g.systems[size_t(*at)].adj[0];
            if (a < 0) return false;
            g.systems[size_t(a)].space[size_t(otherSeat(g, seat))].add(UnitType::Destroyer, 1);
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.tag == Tag::VoteOutcome) {
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskMuaat) {
                for (int id : [&] {
                         std::vector<int> v;
                         for (const Option& o : d.options) v.push_back(o.id);
                         return v;
                     }())
                    if (id > 0 && id % 16 == otherSeat(g, seat)) return a = one(id), true;
            }
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "Glory Furnace fires on")) return why = "no hit chosen", false;
            if (logHas(r, "casts 3 more votes (Glory Furnace)")) return why = "votes too", false;
            return true;
        };
        cases.push_back(c);
    }
    // Mowshir Freeport: the receiver has no system with their ships and a command token - the units are removed
    {
        MonCase c;
        c.name = "Mowshir Freeport: nowhere to go -> removed";
        c.faction = "hacan";
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at](Game& g, int seat) {
            int q = -1;
            for (int o = 0; o < g.numPlayers && q < 0; ++o)
                if (o != seat && areNeighbours(g, seat, o)) q = o;
            if (q < 0) return false;
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Destroyer, 1);
            for (SystemState& sy : g.systems) {
                sy.tokens = uint16_t(sy.tokens & ~(1u << q));
                for (int t = 0; t < kUnitTypes; ++t)  // (no ships of theirs anywhere: nowhere for the destroyer to go)
                    if (isShip(UnitType(t))) sy.space[size_t(q)].remove(UnitType(t), sy.space[size_t(q)].count(UnitType(t)), true);
                sy.space[size_t(q)].remove(UnitType::Infantry, sy.space[size_t(q)].count(UnitType::Infantry), true);
                sy.space[size_t(q)].remove(UnitType::Mech, sy.space[size_t(q)].count(UnitType::Mech), true);
            }
            *at = {s, q};
            return true;
        };
        auto tried = std::make_shared<bool>(false), offered = std::make_shared<bool>(false);
        c.drive = [at, tried, offered](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player == seat && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 650) && !*tried) {
                *tried = true;
                return a = one(650), true;
            }
            if (d.player == seat && d.tag == Tag::Negotiate && *tried && !*offered && offers(d, at->second)) return a = one(at->second), true;
            if (d.player == seat && d.kind == DecisionKind::Deal && d.tag == Tag::DealOffer && *tried && !*offered) {
                *offered = true;
                Deal deal;
                deal.from = seat;
                deal.to = at->second;
                deal.give.units = {at->first * 16 + int(UnitType::Destroyer)};
                a = Answer{};
                a.data = encodeDeal(deal);
                return true;
            }
            if (d.player == at->second && d.tag == Tag::DealResponse) return a = one(1), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            // where the receiver could take them when the deal was accepted decides: replaced there, or removed
            if (r.dealSnaps.empty()) return why = "no deal answered", false;
            bool dest = false;
            {
                const Game& g = r.dealSnaps[0];
                for (size_t x = 0; x < g.systems.size(); ++x)
                    if (hasShips(g, at->second, int(x)) && g.systems[x].hasToken(at->second) && !otherShipsIn(g, at->second, int(x))) dest = true;
            }
            if (!dest && !logHas(r, "are removed")) return why = "not removed with nowhere to go", false;
            if (dest && !logHas(r, "replaces the units from")) return why = "not replaced although it could", false;
            return true;
        };
        cases.push_back(c);
    }
    // Spire of Ixth: their own token -> a fleet token
    {
        MonCase c;
        c.name = "Spire of Ixth: own token -> fleet pool";
        c.faction = "mahact";
        c.setUp = [](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.systems[size_t(g.planets[size_t(x)].system)].tokens |= uint16_t(1u << seat);
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskMahact && offers(d, seat)) return a = one(seat), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!firstMon(r, kMonAskMahact)) return why = "no question", false;
            if (!logHas(r, "puts their own command token from")) return why = "not into the fleet pool", false;
            return true;
        };
        cases.push_back(c);
    }
    // Valefar Assimilator "M": the Nekro destroy a structure (taking the planet) -> "M" onto its owner's monument card; (the rules
    // pass, fixes 43) the "Z" token only with the Valefar Assimilator Z breakthrough (variant 1: Thunder's Edge, breakthrough held)
    for (int variant = 0; variant < 2; ++variant) {
        MonCase c;
        c.name = variant == 0 ? "Valefar Assimilator \"M\": structure destroyed -> token (no \"Z\")"
                              : "Valefar Assimilator \"M\": with the Z breakthrough -> \"Z\" offered";
        c.faction = "nekro";
        if (variant == 1) c.modules = kModCodex | kModMonuments | kModThundersEdge;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, variant](Game& g, int seat) {
            if (variant == 1) g.players[size_t(seat)].breakthrough = true;
            const int q = otherSeat(g, seat);
            int y = -1;
            for (size_t x = 0; x < g.planets.size() && y < 0; ++x) {
                const int s = g.planets[x].system;
                if (homeSys(g, s) || s == g.mecatol() || g.systems[size_t(s)].offBoard || g.systems[size_t(s)].planets.size() != 1) continue;
                y = int(x);
            }
            if (y < 0) return false;
            const int s = g.planets[size_t(y)].system;
            clearPlanet(g, y);
            for (int o = 0; o < kSides; ++o) g.systems[size_t(s)].space[size_t(o)] = Force();
            g.planets[size_t(y)].owner = int8_t(q);
            g.planets[size_t(y)].units[size_t(q)].add(UnitType::SpaceDock, 1);
            Force& f = g.systems[size_t(s)].space[size_t(seat)];
            f.add(UnitType::Carrier, 1);
            f.add(UnitType::Dreadnought, 1);
            f.add(UnitType::Infantry, 3);
            g.systems[size_t(s)].tokens = uint16_t(g.systems[size_t(s)].tokens & ~(1u << seat));
            g.players[size_t(seat)].nekroMon[0] = g.players[size_t(seat)].nekroMon[1] = -1;
            *at = {y, s};
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(at->second)].hasToken(seat)) return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, at->second)) return a = one(at->second), true;
            if (d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == at->second) {
                a = Answer{};
                a.data = {at->first, int(UnitType::Infantry), g.systems[size_t(at->second)].space[size_t(seat)].count(UnitType::Infantry)};
                return true;
            }
            if (d.tag == Tag::MonumentAbility && d.value == kMonAskNekro) return a = one(0), true;
            return false;
        };
        c.check = [variant](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskNekro);
            if (!m) return why = "no question", false;
            const bool z = std::find(m->offered.begin(), m->offered.end(), 1) != m->offered.end();
            if (variant == 0 && z) return why = "\"Z\" offered without the breakthrough", false;
            if (variant == 1 && !z) return why = "no \"Z\" with the breakthrough", false;
            if (r.after.players[size_t(r.seat)].nekroMon[0] != playerMonument(r.after, otherSeat(r.after, r.seat)) &&
                !logHas(r, "gains the text of"))
                return why = "no token on the card", false;
            return true;
        };
        cases.push_back(c);
    }
    // Nefishh Projector: a mech committed too - no coexistence offered (only infantry - RULES-CHECK)
    {
        MonCase c;
        c.name = "Nefishh Projector: with a mech -> no coexistence";
        c.faction = "naalu";
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at](Game& g, int seat) {
            const int q = otherSeat(g, seat);
            const int mx = monumentPlanet(g, seat);
            if (mx < 0) return false;
            monumentOn(g, seat, mx);
            const int ms = g.planets[size_t(mx)].system;
            int y = -1;
            for (size_t x = 0; x < g.planets.size() && y < 0; ++x) {
                const int s = g.planets[x].system;
                if (int(x) == mx || s == ms || homeSys(g, s) || s == g.mecatol() || g.systems[size_t(s)].planets.size() != 1) continue;
                if (hasAdjWith(g, ms, [&](int a) { return a == s; })) y = int(x);
            }
            if (y < 0) return false;
            const int s = g.planets[size_t(y)].system;
            clearPlanet(g, y);
            for (int o = 0; o < kSides; ++o) g.systems[size_t(s)].space[size_t(o)] = Force();
            g.planets[size_t(y)].owner = int8_t(q);
            g.planets[size_t(y)].units[size_t(q)].add(UnitType::Infantry, 1);
            Force& f = g.systems[size_t(s)].space[size_t(seat)];
            f.add(UnitType::Carrier, 1);
            f.add(UnitType::Dreadnought, 1);
            f.add(UnitType::Infantry, 2);
            f.add(UnitType::Mech, 1);
            g.systems[size_t(s)].tokens = uint16_t(g.systems[size_t(s)].tokens & ~(1u << seat));
            *at = {y, s};
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(at->second)].hasToken(seat)) return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, at->second)) return a = one(at->second), true;
            if (d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == at->second) {
                a = Answer{};
                a.data = {at->first, int(UnitType::Infantry), 2, at->first, int(UnitType::Mech), 1};
                return true;
            }
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!steps(r, [](const Step& s) { return s.kind == DecisionKind::Land; })) return why = "no landing", false;
            if (firstMon(r, kMonAskNaalu)) return why = "coexistence offered with a mech committed", false;
            return true;
        };
        c.untilRound = 3;
        cases.push_back(c);
    }

    // ---- (0.82.0, owner M5) Revenance Circuit: its own system counts when it has a wormhole (the planet is adjacent to it, LRR
    // 6.3), and every player's ships are hit - the Creuss' own too
    {
        MonCase c;
        c.name = "Revenance Circuit: own wormhole system, own ships hit too";
        c.faction = "ghost";
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (its system, a wormhole system next to it with the Creuss' destroyer)
        c.setUp = [at](Game& g, int seat) {
            int x = -1;
            for (size_t p = 0; p < g.planets.size() && x < 0; ++p) {
                const int s = g.planets[p].system;
                if (homeSys(g, s) || g.systems[size_t(s)].offBoard || s == g.mecatol() || isDMZ(g, int(p)) || !wormholesOf(g, s)) continue;
                if (hasAdjWith(g, s, [&](int a) { return a != s && wormholesOf(g, a) && !homeSys(g, a) && !g.systems[size_t(a)].offBoard; })) x = int(p);
            }
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            const int w = adjWith(g, s, [&](int a) { return a != s && wormholesOf(g, a) && !homeSys(g, a) && !g.systems[size_t(a)].offBoard; });
            const int q = otherSeat(g, seat);
            for (int o = 0; o < kSides; ++o) g.systems[size_t(s)].space[size_t(o)] = Force(), g.systems[size_t(w)].space[size_t(o)] = Force();
            // (their own command tokens keep the ships there until the Creuss' turn - LRR 58: no moving out of such a system)
            g.systems[size_t(s)].space[size_t(q)].add(UnitType::Destroyer, 2);
            g.systems[size_t(w)].space[size_t(seat)].add(UnitType::Dreadnought, 2);
            g.systems[size_t(s)].tokens = uint16_t((g.systems[size_t(s)].tokens & ~(1u << seat)) | (1u << q));
            g.systems[size_t(w)].tokens = uint16_t(g.systems[size_t(w)].tokens | (1u << seat));
            *at = {s, w};
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(at->first)].hasToken(seat)) return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, at->first)) return a = one(at->first), true;
            if (d.tag == Tag::MonumentAbility && d.value == kMonAskCreuss) {  // every system offered
                a = Answer{};
                for (const Option& o : d.options)
                    if (o.enabled) a.picks.push_back(o.id);
                return true;
            }
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskCreuss);
            if (!m) return why = "no question", false;
            if (std::find(m->offered.begin(), m->offered.end(), at->first) == m->offered.end()) return why = "its own wormhole system not offered", false;
            if (std::find(m->offered.begin(), m->offered.end(), at->second) == m->offered.end()) return why = "the system with only the Creuss' ships not offered", false;
            const int q = otherSeat(r.before, r.seat);
            bool theirs = false, ours = false;
            for (size_t i = m->events; i < r.log.size() && i < m->events + 20; ++i)
                if (r.log[i].kind == EventKind::Destroyed || r.log[i].kind == EventKind::Damaged) {
                    if (r.log[i].player == q) theirs = true;
                    if (r.log[i].player == r.seat) ours = true;  // (a dreadnought: damaged - SUSTAIN DAMAGE)
                }
            if (!theirs) return why = "no hit in its own system", false;
            if (!ours) return why = "the Creuss' own ships weren't hit", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- (0.82.0, owner M13) Glory Furnace: the Muaat's own supernova (Nova Seed, from Adjudicator Ba'al) next to it counts
    {
        MonCase c;
        c.name = "Glory Furnace: the Nova Seed next to it counts";
        c.faction = "muaat";
        c.agenda = true;
        c.untilRound = 2;
        c.setUp = [](Game& g, int seat) {
            const int nova = findSystem("81");
            auto empty = [&](int a) {
                const SystemState& t = g.systems[size_t(a)];
                bool lane = false;  // (not a hyperlane tile)
                for (uint8_t l : kSystems[t.def].lanes) lane = lane || l != 0;
                return nova >= 0 && !lane && t.planets.empty() && !homeSys(g, a) && !t.offBoard && a != g.mecatol() && !wormholesOf(g, a) &&
                       !(kSystems[t.def].anomalies & kSupernova) && t.def != nova;
            };
            int x = monumentPlanet(g, seat, [&](int x) {
                const int s = g.planets[size_t(x)].system;
                // no other supernova next to it (exactly one choice)
                return hasAdjWith(g, s, empty) && !hasAdjWith(g, s, [&](int a) { return (kSystems[g.systems[size_t(a)].def].anomalies & kSupernova) != 0; });
            });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            g.systems[size_t(s)].space[size_t(seat)] = Force();  // (no war sun / flagship there: the supernova is the only choice)
            const int a = adjWith(g, s, empty);
            for (int o = 0; o < kSides; ++o) g.systems[size_t(a)].space[size_t(o)] = Force();
            g.systems[size_t(a)].def = int16_t(nova);
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.tag == Tag::VoteOutcome) {
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskMuaat) return a = one(0), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "casts 3 more votes (Glory Furnace)")) return why = "no extra votes", false;
            bool hinted = false;
            for (const Step& s : r.steps)
                if (s.player == r.seat && s.tag == Tag::VoteOutcome && s.context.find("Glory Furnace: exhaust") != std::string::npos &&
                    s.context.find("for 1 choice") != std::string::npos)
                    hinted = true;
            if (!hinted) return why = "the vote question doesn't mention it (1 choice)", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- (0.82.0, owner M14: "check if other cards allow predictions besides riders") Calm Seas Sanatorium after a prediction
    // made with the Keleres Rider (the Jol-Nar can't vote on that agenda - only the prediction counts)
    {
        MonCase c;
        c.name = "Calm Seas Sanatorium: predicted with the Keleres Rider";
        c.faction = "jolnar";
        c.second = "keleres";
        c.agenda = true;
        c.untilRound = 2;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            const int k = playerOfFaction(g, "keleres");
            if (x < 0 || k < 0) return false;
            monumentOn(g, seat, x);
            *at = x;
            auto& kn = g.players[size_t(k)].notes;
            const int16_t note = makeNote(k, kNoteFaction);
            auto it = std::find(kn.begin(), kn.end(), note);
            if (it == kn.end()) return false;
            kn.erase(it);
            g.players[size_t(seat)].notes.push_back(note);
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.player == seat && d.tag == Tag::Faction && d.ability == int(Ab::KeleresRider)) {
                if (d.prompt.find("predict") == std::string::npos) return a = one(1), true;  // play it
                for (const Option& o : d.options)                                           // the first outcome
                    if (o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VoteOutcome) {
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskJolnar && offers(d, *at * 4 + 2)) return a = one(*at * 4 + 2), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskJolnar);
            if (!m) return why = "no question", false;
            bool rider = false;
            for (const Step& s : r.steps) {
                if (&s == m) break;
                if (s.player == r.seat && s.tag == Tag::VoteOutcome) return why = "the Jol-Nar voted (not a prediction case)", false;
                if (s.player == r.seat && s.tag == Tag::Faction && s.ability == int(Ab::KeleresRider)) rider = true;
            }
            if (!rider) return why = "no Keleres Rider played", false;
            if (!logHas(r, "places 2 infantry on", m->events)) return why = "no infantry placed", false;
            return true;
        };
        cases.push_back(c);
    }

    // ================================================================ Monuments+'s cards (0.82.0)
    // ---- Festival: ACTION - ready a non-home planet with a structure of theirs; their monument there: 4 infantry or a mech
    {
        MonCase c;
        c.name = "Festival: planet readied, 4 infantry at the monument";
        c.faction = "letnev";
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.planets[size_t(x)].exhausted = true;
            giveCard(g, seat, "festival");
            *at = x;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            const int card = findActionCard("festival");
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && offers(d, 1000 + card)) return a = one(1000 + card), true;
            if (d.tag == Tag::CardTarget && d.card == card && offers(d, *at)) return a = one(*at), true;
            if (d.tag == Tag::CardTarget && d.card == card && offers(d, 0)) return a = one(0), true;  // 4 infantry
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const std::string name = kPlanets[r.before.planets[size_t(*at)].def].name;
            if (!logHas(r, "readies " + name + " (Festival)")) return why = "the planet wasn't readied", false;
            if (!logHas(r, "places 4 Infantry on " + name + " (Festival)")) return why = "no 4 infantry at the monument", false;
            // the planet was chosen from the action (the card played as the turn's action)
            if (!steps(r, [](const Step& s) { return s.tag == Tag::CardTarget && s.phase == Phase::Action; })) return why = "not in the action phase", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Rebel Bombing: 2 trade goods - a neighbour's structure in a non-home system destroyed; a monument on the Cathedral of
    // Ixth takes its point with it; destroyed outside a tactical action it doesn't count for Topple a Monument
    {
        MonCase c;
        c.name = "Rebel Bombing: a monument on the Cathedral -> its point lost";
        c.faction = "sol";
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (target planet, its owner)
        c.setUp = [at](Game& g, int seat) {
            const int q = otherSeat(g, seat);
            const int y = monumentPlanet(g, q, [&](int x) {
                const int s = g.planets[size_t(x)].system;
                return hasAdjWith(g, s, [&](int a) { return !homeSys(g, a) && !g.systems[size_t(a)].offBoard && g.systems[size_t(a)].planets.empty(); });
            });
            if (y < 0) return false;
            monumentOn(g, q, y);
            const int s = g.planets[size_t(y)].system;
            const int n = adjWith(g, s, [&](int a) { return !homeSys(g, a) && !g.systems[size_t(a)].offBoard && g.systems[size_t(a)].planets.empty(); });
            for (int o = 0; o < kSides; ++o) g.systems[size_t(n)].space[size_t(o)] = Force();
            g.systems[size_t(n)].space[size_t(seat)].add(UnitType::Dreadnought, 1);  // (neighbours: their units next to each other)
            g.systems[size_t(n)].tokens = uint16_t(g.systems[size_t(n)].tokens | (1u << seat));
            g.players[size_t(seat)].tradeGoods = 5;
            giveCard(g, seat, "rebelbombing");
            giveSecret(g, seat, "tam");
            lawOn(g, "cathedralofixth", y);
            g.players[size_t(q)].cathedralVP = 1;
            g.players[size_t(q)].vp = int8_t(g.players[size_t(q)].vp + 1);
            *at = {y, q};
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            const int card = findActionCard("rebelbombing");
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && offers(d, 1000 + card)) return a = one(1000 + card), true;
            const int id = at->second * 10000000 + int(UnitType::Monument) * 100000 + at->first;
            if (d.tag == Tag::CardTarget && d.card == card && offers(d, id)) return a = one(id), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const std::string name = kPlanets[r.before.planets[size_t(at->first)].def].name;
            if (!logHas(r, "rebels bomb")) return why = "not played", false;
            if (!logHas(r, "Cathedral of Ixth: their monument on " + name + " is removed")) return why = "no Cathedral point lost", false;
            if (scored(r.after, r.seat, "tam")) return why = "Topple a Monument scored outside a tactical action", false;
            // the 2 trade goods: the line says so, and the seat had 5
            if (!logHas(r, "spends 2 trade goods")) return why = "no trade goods spent", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Renovation: a PDS replaced by the monument (from the reinforcements) - on the Cathedral of Ixth's planet: 1 VP
    {
        MonCase c;
        c.name = "Renovation: PDS -> monument on the Cathedral -> 1 VP";
        c.faction = "letnev";
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            removeMonuments(g, seat);
            clearPlanet(g, x);
            g.planets[size_t(x)].owner = int8_t(seat);
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::PDS, 1);
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::Infantry, 1);
            lawOn(g, "cathedralofixth", x);
            giveCard(g, seat, "renovation");
            *at = x;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            const int card = findActionCard("renovation");
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && offers(d, 1000 + card)) return a = one(1000 + card), true;
            const int id = int(UnitType::PDS) * 100000 + *at;
            if (d.tag == Tag::CardTarget && d.card == card && offers(d, id)) return a = one(id), true;
            if (d.tag == Tag::CardTarget && d.card == card && offers(d, int(UnitType::Monument))) return a = one(int(UnitType::Monument)), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const std::string name = kPlanets[r.before.planets[size_t(*at)].def].name;
            if (!logHas(r, "renovates")) return why = "not played", false;
            if (!logHas(r, "raises their monument")) return why = "the monument wasn't placed", false;
            if (!logHas(r, "Cathedral of Ixth: their monument is placed on " + name)) return why = "no Cathedral point", false;
            if (r.after.planets[size_t(*at)].units[size_t(r.seat)].count(UnitType::PDS) && r.after.planets[size_t(*at)].owner == r.seat &&
                !r.after.planets[size_t(*at)].units[size_t(r.seat)].count(UnitType::Monument))
                return why = "the PDS is still there instead", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Cathedral of Ixth (agenda): elected on a planet with its owner's monument -> 1 VP at once
    {
        MonCase c;
        c.name = "Cathedral of Ixth: elected on the monument's planet -> 1 VP";
        c.faction = "letnev";
        c.agenda = true;
        c.untilRound = 2;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            agendaOnTop(g, "cathedralofixth");
            *at = x;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            (void)seat;
            if (d.tag == Tag::VoteOutcome && offers(d, *at)) return a = one(*at), true;
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const std::string name = kPlanets[r.before.planets[size_t(*at)].def].name;
            bool law = false;
            for (const Law& l : r.after.laws)
                if (l.agenda == findAgenda("cathedralofixth") && l.target == *at) law = true;
            if (!law) return why = "not attached to the planet", false;
            if (!logHas(r, "Cathedral of Ixth: their monument is on " + name)) return why = "no point for the owner", false;
            if (r.after.players[size_t(r.seat)].cathedralVP != 1) return why = "the point isn't held", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Minister of Culture (agenda): the elected player owns it and replenishes - their monument on the board: value + 2
    {
        MonCase c;
        c.name = "Minister of Culture: elected -> commodities + 2";
        c.faction = "letnev";
        c.agenda = true;
        c.untilRound = 2;
        c.setUp = [](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            agendaOnTop(g, "ministerofculture");
            g.players[size_t(seat)].commodities = 0;
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            if (d.tag == Tag::VoteOutcome && offers(d, seat)) return a = one(seat), true;
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const int want = kFactions[r.before.players[size_t(r.seat)].faction].commodities + 2;
            if (!logHas(r, "replenishes commodities: " + std::to_string(want) + " (Minister of Culture)"))
                return why = "not replenished to " + std::to_string(want), false;
            if (lawOwner(r.after, "ministerofculture") != r.seat) return why = "not the owner", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Defend a Great Work (status phase secret): the monument on the board, 3 ground forces on its planet / in its space area
    {
        MonCase c;
        c.name = "Defend a Great Work: monument + 3 ground forces -> scored";
        c.faction = "letnev";
        c.setUp = [](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) { return !hasAdjWith(g, g.planets[size_t(x)].system, [&](int a) { return homeSys(g, a); }); });
            if (x < 0) return false;
            monumentOn(g, seat, x);  // (1 infantry)
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::Infantry, 3);
            giveSecret(g, seat, "dagw");
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            const int o = findObjective("dagw");
            if (d.player == seat && d.tag == Tag::ScoreSecret && offers(d, o)) return a = one(o), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!scored(r.after, r.seat, "dagw")) return why = "not scored", false;
            if (!logHas(r, "scores a secret objective: Defend a Great Work")) return why = "no scoring line", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Topple a Monument (action phase secret): another player's monument destroyed in an invasion -> scored at once
    {
        MonCase c;
        c.name = "Topple a Monument: a monument taken in an invasion -> scored";
        c.faction = "sol";
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (planet, system)
        c.setUp = [at](Game& g, int seat) {
            const int q = otherSeat(g, seat);
            const int y = monumentPlanet(g, q, [&](int x) { return g.systems[size_t(g.planets[size_t(x)].system)].planets.size() == 1; });
            if (y < 0) return false;
            monumentOn(g, q, y);  // (1 infantry of theirs)
            const int s = g.planets[size_t(y)].system;
            for (int o = 0; o < kSides; ++o) g.systems[size_t(s)].space[size_t(o)] = Force();
            Force& f = g.systems[size_t(s)].space[size_t(seat)];
            f.add(UnitType::Carrier, 1);
            f.add(UnitType::Dreadnought, 2);
            f.add(UnitType::Infantry, 4);
            g.systems[size_t(s)].tokens = uint16_t(g.systems[size_t(s)].tokens & ~(1u << seat));
            giveSecret(g, seat, "tam");
            *at = {y, s};
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(at->second)].hasToken(seat)) return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, at->second)) return a = one(at->second), true;
            if (d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == at->second) {
                a = Answer{};
                a.data = {at->first, int(UnitType::Infantry), g.systems[size_t(at->second)].space[size_t(seat)].count(UnitType::Infantry)};
                return true;
            }
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!scored(r.after, r.seat, "tam")) return why = "not scored", false;
            // scored in the action phase, right after the monument went (before the next turn)
            size_t lost = 0, sc = 0;
            for (size_t i = 0; i < r.log.size(); ++i) {
                if (!lost && r.log[i].text.find("Monument") != std::string::npos && r.log[i].text.find("captured") != std::string::npos) lost = i + 1;
                if (!sc && r.log[i].text.find("scores a secret objective: Topple a Monument") != std::string::npos) sc = i + 1;
            }
            if (!sc) return why = "no scoring line", false;
            if (lost && sc < lost) return why = "scored before the monument was destroyed", false;
            if (kindBetween(r, EventKind::TurnEnd, lost ? lost - 1 : 0, sc - 1)) return why = "not scored at once", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Monuments to the Ages Omega (Thunder's Edge event + Monuments+): "monoliths" - Renovation's new structure on a non-home
    // planet may be a monolith for 5 trade goods
    {
        MonCase c;
        c.name = "Monuments to the Ages Omega: a monolith instead";
        c.faction = "letnev";
        c.modules = kModCodex | kModMonuments | kModThundersEdge;
        c.events = 1u << kEvMonuments;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            int x = -1;
            for (int y : controlledPlanets(g, seat))
                if (!isHomePlanet(g, y) && !isStation(g, y) && !isDMZ(g, y)) x = y;
            if (x < 0) {  // (one of theirs, then)
                x = monumentPlanet(g, seat);
                if (x < 0) return false;
                clearPlanet(g, x);
                g.planets[size_t(x)].owner = int8_t(seat);
            }
            removeMonuments(g, seat);
            Force& f = g.planets[size_t(x)].units[size_t(seat)];
            f.remove(UnitType::SpaceDock, f.count(UnitType::SpaceDock), true);
            f.remove(UnitType::PDS, f.count(UnitType::PDS), true);
            f.add(UnitType::PDS, 1);
            g.players[size_t(seat)].tradeGoods = 8;
            for (PlanetState& ps : g.planets) ps.monument = -1;
            giveCard(g, seat, "renovation");
            *at = x;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            (void)g;
            const int card = findActionCard("renovation");
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && offers(d, 1000 + card)) return a = one(1000 + card), true;
            if (d.tag == Tag::CardTarget && d.card == card && offers(d, int(UnitType::PDS) * 100000 + *at))
                return a = one(int(UnitType::PDS) * 100000 + *at), true;
            if (d.tag == Tag::CardTarget && d.card == card && offers(d, int(UnitType::SpaceDock))) return a = one(int(UnitType::SpaceDock)), true;
            if (d.tag == Tag::Faction && d.ability == int(Ab::EvMonument) && d.prompt.find("pay 5 trade goods") != std::string::npos) return a = one(1), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const Step* m = nullptr;
            for (const Step& s : r.steps)
                if (s.tag == Tag::Faction && s.ability == int(Ab::EvMonument) && !m) m = &s;
            if (!m) return why = "no question", false;
            if (m->prompt.find("monolith") == std::string::npos || m->prompt.find("Monuments to the Ages \xCE\xA9") == std::string::npos)
                return why = "not the Omega wording: " + m->prompt, false;
            if (!logHas(r, "raises a monolith on")) return why = "no monolith raised", false;
            return true;
        };
        cases.push_back(c);
    }

    // ================================================================ 0.82.1: Thunder's Edge's monuments
    const int kTE = kModCodex | kModMonuments | kModThundersEdge;
    // a non-home system on the board with exactly 1 planet, not legendary, not a station (the Seraph Data Center's kind of place)
    auto seraphSystem = [](const Game& g, std::function<bool(int)> also) {
        for (size_t s = 0; s < g.systems.size(); ++s) {
            const SystemState& sy = g.systems[s];
            if (sy.home >= 0 || sy.offBoard || sy.isHyperlane() || int(s) == g.mecatol() || sy.planets.size() != 1) continue;
            const int x = sy.planets[0];
            if (isStation(g, x) || isLegendaryPlanet(g, x) || g.planets[size_t(x)].destroyed) continue;
            if (also && !also(int(s))) continue;
            return int(s);
        }
        return -1;
    };
    // the Seraph Data Center in sys with its station card, as Rules::monStationPlace makes it
    auto stationOn = [](Game& g, int seat, int sys) {
        removeMonuments(g, seat);
        g.systems[size_t(sys)].space[size_t(seat)].add(UnitType::Monument, 1);
        PlanetState ps;
        ps.def = int16_t(findPlanet("seraphdatacenter"));
        ps.system = int16_t(sys);
        ps.owner = int8_t(seat);
        g.players[size_t(seat)].monStation = int16_t(g.planets.size());
        g.systems[size_t(sys)].planets.push_back(int16_t(g.planets.size()));
        g.planets.push_back(ps);
    };

    // ---- Seraph Data Center (Last Bastion): placed with Construction where its card allows -> its station card, +1 commodity value
    {
        MonCase c;
        c.name = "Seraph Data Center: Construction -> station card";
        c.faction = "bastion";
        c.modules = kTE;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, seraphSystem](Game& g, int seat) {
            const int s = seraphSystem(g, nullptr);
            if (s < 0) return false;
            removeMonuments(g, seat);
            for (int o = 0; o < kSides; ++o) g.systems[size_t(s)].space[size_t(o)] = Force();
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Cruiser, 2);
            g.systems[size_t(s)].tokens = 0;
            *at = s;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat && d.tag == Tag::StrategyPick && g.round == 2 && offers(d, 3)) {  // (Construction left for them)
                for (const Option& o : d.options)
                    if (o.enabled && o.id != 3) return a = one(o.id), true;
            }
            if (d.player != seat) return false;
            if (d.tag == Tag::StrategyPick && g.round == 2 && offers(d, 3)) return a = one(3), true;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 13)) return a = one(13), true;
            if ((d.tag == Tag::ConstructionWhere || d.tag == Tag::MonumentPlace) && offers(d, 200000 + *at)) return a = one(200000 + *at), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            if (!logHas(r, "raises their monument, Seraph Data Center, in")) return why = "not placed", false;
            const Player& pl = r.after.players[size_t(r.seat)];
            if (pl.monStation < 0) return why = "no station card", false;
            const PlanetState& st = r.after.planets[size_t(pl.monStation)];
            if (st.destroyed || st.owner != r.seat) return why = "the station isn't theirs", false;
            if (monumentSystem(r.after, r.seat) != st.system) return why = "the station isn't where the monument is", false;
            const auto& lst = r.after.systems[size_t(st.system)].planets;
            if (std::find(lst.begin(), lst.end(), int16_t(pl.monStation)) == lst.end()) return why = "the station isn't in the system's planets", false;
            if (stationsOf(r.after, r.seat) < 1) return why = "not counted as a space station", false;
            // every space spot offered: their ships there and exactly 1 planet, not legendary (stations aren't planets)
            for (size_t i = 0; i < r.steps.size(); ++i) {
                const Step& s = r.steps[i];
                if (s.tag != Tag::ConstructionWhere && s.tag != Tag::MonumentPlace) continue;
                for (int o : s.offered) {
                    if (o < 200000 || o >= 300000) continue;
                    const int sys = o - 200000;
                    const Game& g = r.before;
                    int n = 0;
                    bool leg = false;
                    for (int16_t x : g.systems[size_t(sys)].planets)
                        if (!isStation(g, x)) ++n, leg = leg || isLegendaryPlanet(g, x);
                    if (n != 1 || leg) return why = "offered " + std::string(g.systems[size_t(sys)].d().name) + " (not exactly 1 non-legendary planet)", false;
                }
            }
            return true;
        };
        cases.push_back(c);
    }

    // ---- Seraph Data Center: another player alone with ships there takes the station -> destroyed, 2 hits against their ships
    {
        MonCase c;
        c.name = "Seraph Data Center: station taken -> destroyed, 2 hits";
        c.faction = "bastion";
        c.modules = kTE;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, seraphSystem, stationOn](Game& g, int seat) {
            const int s = seraphSystem(g, nullptr);
            if (s < 0) return false;
            for (int o = 0; o < kSides; ++o) g.systems[size_t(s)].space[size_t(o)] = Force();
            g.systems[size_t(s)].tokens = 0;
            stationOn(g, seat, s);
            const int q = otherSeat(g, seat);
            g.systems[size_t(s)].space[size_t(q)].add(UnitType::Destroyer, 3);
            g.systems[size_t(s)].space[size_t(q)].add(UnitType::Cruiser, 1);
            *at = s;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat);
            if (d.player != q) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(*at)].hasToken(q)) return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, *at)) return a = one(*at), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            if (!logHas(r, "loses control of Seraph Data Center's station")) return why = "no loss line", false;
            size_t lost = 0;
            for (size_t i = 0; i < r.log.size(); ++i)
                if (r.log[i].text.find("loses control of Seraph Data Center") != std::string::npos) lost = i;
            // after the other player's activation of that system (the station rule: only their ships there), in that tactical action
            const Step* act = nullptr;
            for (const Step& s : r.steps)
                if (s.tag == Tag::Activate && !s.answer.empty() && s.answer[0] == *at && s.events <= lost) act = &s;
            if (!act) return why = "not after that system's activation", false;
            if (kindBetween(r, EventKind::TurnEnd, act->events, lost)) return why = "not in that tactical action", false;
            const Player& pl = r.after.players[size_t(r.seat)];
            if (monumentSystem(r.after, r.seat) >= 0) return why = "the monument is still on the board", false;
            if (pl.monStation >= 0 && !r.after.planets[size_t(pl.monStation)].destroyed) return why = "the station card is still in play", false;
            // 2 hits on the intruder's ships (a destroyer lost, or the cruiser...)
            int hits = 0;
            for (size_t i = lost; i < r.log.size() && i < lost + 12; ++i)
                if (r.log[i].text.find("Seraph Data Center") != std::string::npos && r.log[i].text.find("loses") != std::string::npos &&
                    r.log[i].text.find("control") == std::string::npos)
                    ++hits;
            if (hits == 0) return why = "no ship lost to the 2 hits", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Veils of Lost Creuss (Crimson): a breach next to it turns active (Incursion) -> 1 hit against another player's ships there
    {
        MonCase c;
        c.name = "Veils of Lost Creuss: breach opens next to it -> 1 hit";
        c.faction = "crimson";
        c.modules = kTE;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (monument system, breach system)
        c.setUp = [at](Game& g, int seat) {
            auto ok = [&](int a) { return !homeSys(g, a) && !g.systems[size_t(a)].offBoard && !g.systems[size_t(a)].isHyperlane() && a != g.mecatol(); };
            const int x = monumentPlanet(g, seat, [&](int x) { return hasAdjWith(g, g.planets[size_t(x)].system, ok); });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            const int b = adjWith(g, s, ok);
            for (SystemState& sy : g.systems) sy.breach = 0;
            g.systems[size_t(b)].breach = 1;
            for (int o = 0; o < kSides; ++o) g.systems[size_t(b)].space[size_t(o)] = Force();
            g.systems[size_t(b)].space[size_t(otherSeat(g, seat))].add(UnitType::Destroyer, 2);
            g.systems[size_t(b)].tokens = 0;
            *at = {s, b};
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(at->second)].hasToken(seat)) return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, at->second)) return a = one(at->second), true;
            if (d.tag == Tag::Faction && d.ability == int(Ab::Incursion) && g.activeSystem == at->second) return a = one(1), true;
            if (d.tag == Tag::MonumentAbility && d.value == kMonAskCrimson && offers(d, otherSeat(g, seat))) return a = one(otherSeat(g, seat)), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskCrimson);
            if (!m) return why = "no question", false;
            // right after the Incursion flip, before any movement
            const Step* inc = nullptr;
            for (const Step& s : r.steps) {
                if (&s == m) break;
                if (s.tag == Tag::Faction && s.ability == int(Ab::Incursion)) inc = &s;
            }
            if (!inc || inc->answer.empty() || inc->answer[0] != 1) return why = "not after the breach was opened", false;
            for (const Step* s = inc + 1; s < m; ++s)
                if (s->kind == DecisionKind::Move) return why = "after movement", false;
            if (!logHas(r, "strikes through the breach", m->events)) return why = "no hit line", false;
            if (!logHas(r, "loses 1 Destroyer", m->events) && !logHas(r, "loses 1 destroyer", m->events)) return why = "no destroyer lost", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- The Pelagion (Deepwrought): another player's ships move into its system -> no space combat; then they invade its
    // planet: the monument is lost and the ships left there fight it out (the Deepwrought choose who)
    for (int variant = 0; variant < 2; ++variant) {
        MonCase c;
        c.name = variant == 0 ? "The Pelagion: no space combat triggered" : "The Pelagion: taken in an invasion -> space combat";
        c.faction = "deepwrought";
        c.modules = kTE;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (planet, system)
        c.setUp = [at, variant](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) { return g.systems[size_t(g.planets[size_t(x)].system)].planets.size() == 1; });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            for (int o = 0; o < kSides; ++o) g.systems[size_t(s)].space[size_t(o)] = Force();
            g.systems[size_t(s)].tokens = 0;
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Cruiser, 1);
            Force& f = g.systems[size_t(s)].space[size_t(otherSeat(g, seat))];
            f.add(UnitType::Carrier, 1);
            f.add(UnitType::Dreadnought, 1);
            if (variant == 1) f.add(UnitType::Infantry, 4);
            *at = {x, s};
            return true;
        };
        c.drive = [at, variant](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat);
            if (d.player != q) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(at->second)].hasToken(q)) return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, at->second)) return a = one(at->second), true;
            if (variant == 1 && d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == at->second) {
                a = Answer{};
                a.data = {at->first, int(UnitType::Infantry), g.systems[size_t(at->second)].space[size_t(q)].count(UnitType::Infantry)};
                return true;
            }
            return false;
        };
        c.check = [at, variant](const MonRun& r, std::string& why) {
            const std::string sname = r.before.systems[size_t(at->second)].d().name;
            const Step* act = nullptr;
            for (const Step& s : r.steps)
                if (s.tag == Tag::Activate && !s.answer.empty() && s.answer[0] == at->second && s.player == otherSeat(r.before, r.seat)) {
                    act = &s;
                    break;
                }
            if (!act) return why = "no question (not activated)", false;
            size_t end = r.log.size();
            for (size_t i = act->events; i < r.log.size(); ++i)
                if (r.log[i].kind == EventKind::TurnEnd) {
                    end = i;
                    break;
                }
            size_t peace = 0, combat = 0, lost = 0;
            for (size_t i = act->events; i < end; ++i) {
                if (!peace && r.log[i].text.find("keeps the peace") != std::string::npos) peace = i + 1;
                if (!combat && r.log[i].text.find("Space combat in " + sname) != std::string::npos) combat = i + 1;
                if (!lost && r.log[i].text.find("loses 1 Monument (captured)") != std::string::npos) lost = i + 1;
            }
            if (!peace) return why = "no 'keeps the peace' line", false;
            if (variant == 0) {
                if (combat) return why = "a space combat happened", false;
                return true;
            }
            if (!lost) return why = "the monument wasn't taken", false;
            const Step* m = firstMon(r, kMonAskDeepwrought);
            if (!combat || combat < lost) return why = "no space combat after it was taken", false;
            if (m && (m->player != r.seat || m->events < lost - 1)) return why = "the choice wasn't the Deepwrought's, after the loss", false;
            if (!logHas(r, "resolve a space combat in " + sname, lost - 1)) return why = "no combat line", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Epiphany (Firmament): another player activates its system -> their control token onto the card
    {
        MonCase c;
        c.name = "Epiphany: activation -> control token on the card";
        c.faction = "firmament";
        c.modules = kTE;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = g.planets[size_t(x)].system;
            g.systems[size_t(*at)].tokens = 0;
            g.players[size_t(seat)].epiphanyTok = 0;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat);
            if (d.player != q) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(*at)].hasToken(q)) return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, *at)) return a = one(*at), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const int q = otherSeat(r.before, r.seat);
            const Step* act = nullptr;
            for (const Step& s : r.steps)
                if (s.tag == Tag::Activate && s.player == q && !s.answer.empty() && s.answer[0] == *at) {
                    act = &s;
                    break;
                }
            if (!act) return why = "no question (not activated)", false;
            if (!logHas(r, "control token goes onto Epiphany", act->events)) return why = "no token line", false;
            const Step* next = act + 1;  // (before movement)
            for (size_t i = act->events; next < &r.steps.back() + 1 && i < next->events; ++i)
                if (r.log[i].text.find("control token goes onto Epiphany") != std::string::npos) return true;
            return why = "not right after the activation", false;
        };
        cases.push_back(c);
    }

    // ---- Epiphany: research -> a token off the card skips a prerequisite / or a token onto a plot card
    for (int variant = 0; variant < 2; ++variant) {
        MonCase c;
        c.name = variant == 0 ? "Epiphany: research -> a prerequisite skipped" : "Epiphany: research -> a token onto a plot card";
        c.faction = "firmament";
        c.modules = kTE;
        auto chosen = std::make_shared<int>(-1);
        c.setUp = [variant](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int q = otherSeat(g, seat);
            Player& pl = g.players[size_t(seat)];
            pl.epiphanyTok = uint16_t((1u << q) | (variant == 0 ? (1u << seat) : 0u));
            if (variant == 1) {
                pl.plotsIn = uint8_t(pl.plotsIn | 1u);
                pl.plotTok[0] = 0;
            }
            pl.tradeGoods = 10;
            return true;
        };
        c.drive = [variant, chosen](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat && d.tag == Tag::StrategyPick && g.round == 2 && offers(d, 6)) {  // (Technology left for them)
                for (const Option& o : d.options)
                    if (o.enabled && o.id != 6) return a = one(o.id), true;
            }
            if (d.player != seat) return false;
            if (d.tag == Tag::StrategyPick && g.round == 2 && offers(d, 6)) return a = one(6), true;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 16)) return a = one(16), true;
            if (d.tag == Tag::Research && *chosen < 0) {
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0 && (o.detail.find("Epiphany") != std::string::npos) == (variant == 0)) {
                        *chosen = o.id;
                        return a = one(o.id), true;
                    }
            }
            if (d.tag == Tag::MonumentAbility && d.value == kMonAskEpiphanySkip && offers(d, seat)) return a = one(seat), true;
            if (d.tag == Tag::MonumentAbility && d.value == kMonAskEpiphanyPlot) {
                const int q = otherSeat(g, seat);
                if (offers(d, 0 * 16 + q)) return a = one(q), true;
            }
            return false;
        };
        c.check = [variant, chosen](const MonRun& r, std::string& why) {
            if (*chosen < 0) return why = "no question (no technology to choose)", false;
            const Player& pl = r.after.players[size_t(r.seat)];
            const int q = otherSeat(r.before, r.seat);
            if (!pl.hasTech(int16_t(*chosen))) return why = std::string(kTechs[*chosen].name) + " not researched", false;
            if (variant == 0) {
                const Step* m = firstMon(r, kMonAskEpiphanySkip);
                if (!m) return why = "no token question", false;
                if (!logHas(r, "removes ", m->events) || !logHas(r, "to skip a prerequisite", m->events)) return why = "no skip line", false;
                if ((pl.epiphanyTok >> r.seat) & 1) return why = "their own token is still on the card", false;
                if (!logHas(r, "removes Firmament's control token from Epiphany to skip a prerequisite", m->events))
                    return why = "not the token they chose", false;
                return true;
            }
            const Step* m = firstMon(r, kMonAskEpiphanyPlot);
            if (!m) return why = "no plot question", false;
            if (!((pl.plotTok[0] >> q) & 1)) return why = "the token isn't on the plot card", false;
            if ((pl.epiphanyTok >> q) & 1) return why = "the token is still on Epiphany", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Epiphany Hollow (Obsidian): a puppeted player activates its system -> 2 of their infantry replaced by the Obsidian's
    {
        MonCase c;
        c.name = "Epiphany Hollow: a puppet activates it -> infantry replaced";
        c.faction = "firmament";
        c.modules = kTE;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (monument system, the puppet's planet)
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            const int q = otherSeat(g, seat);
            int y = -1;
            for (int z : controlledPlanets(g, q))
                if (!isDMZ(g, z) && !isStation(g, z) && g.planets[size_t(z)].system != g.planets[size_t(x)].system) y = z;
            if (y < 0) return false;
            monumentOn(g, seat, x);
            // the Firmament as the Obsidian (Puppets of the Blade), with the other player on a plot card
            Player& pl = g.players[size_t(seat)];
            pl.faction = int8_t(findFaction("obsidian"));
            pl.unit[int(UnitType::Monument)] = int16_t(unitByKey("obsidian_monument"));
            pl.plotsIn = 1;
            pl.plotTok[0] = uint8_t(1u << q);
            g.planets[size_t(y)].units[size_t(q)].add(UnitType::Infantry, 3);
            *at = {g.planets[size_t(x)].system, y};
            g.systems[size_t(at->first)].tokens = 0;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat);
            if (d.player == q && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(at->first)].hasToken(q))
                return a = one(0), true;
            if (d.player == q && d.tag == Tag::Activate && offers(d, at->first)) return a = one(at->first), true;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskObsidian && offers(d, at->second * 4 + 2))
                return a = one(at->second * 4 + 2), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskObsidian);
            if (!m) return why = "no question", false;
            const int q = otherSeat(r.before, r.seat);
            const Step* act = nullptr;
            for (const Step& s : r.steps) {
                if (&s == m) break;
                if (s.tag == Tag::Activate) act = &s;
            }
            if (!act || act->player != q || act->answer.empty() || act->answer[0] != at->first) return why = "not after the puppet's activation", false;
            for (const Step* s = act + 1; s < m; ++s)
                if (s->kind == DecisionKind::Move) return why = "after movement", false;
            const std::string pname = kPlanets[r.before.planets[size_t(at->second)].def].name;
            if (!logHas(r, "replaces 2 of", m->events) || !logHas(r, "infantry on " + pname, m->events)) return why = "no replacement line", false;
            if (!logHas(r, "units coexist on " + pname, m->events)) return why = "no coexistence", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Epiphany -> Epiphany Hollow: Puppets of the Blade turns the monument on the board over (and it can't be placed again)
    {
        MonCase c;
        c.name = "Epiphany: Puppets of the Blade -> Epiphany Hollow";
        c.faction = "firmament";
        c.modules = kTE;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            Player& pl = g.players[size_t(seat)];
            pl.plotsIn = 1;
            pl.plotTok[0] = uint8_t(1u << otherSeat(g, seat));
            pl.epiphanyTok = uint16_t(1u << otherSeat(g, seat));
            *at = x;
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            const int opt = 6000 + int(Ab::Puppets);
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, opt)) return a = one(opt), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            if (!isFaction(r.after, r.seat, "obsidian")) return why = "no question (not the Obsidian)", false;
            if (playerMonument(r.after, r.seat) != monCard("obsidian")) return why = "the card isn't Epiphany Hollow", false;
            int planet = -1;
            if (monumentSystem(r.after, r.seat, &planet) < 0 || planet != *at) return why = "the monument didn't stay where it stood", false;
            if (epiphanyTokens(r.after, r.seat) != 0) return why = "Epiphany's tokens still count", false;
            for (const Step& s : r.steps)
                if (s.tag == Tag::MonumentPlace && s.player == r.seat) return why = "offered to place Epiphany Hollow", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Sor Tek Linkhub (Ral Nel): at the end of their tactical action - a Saar floating factory next to it moved on
    {
        MonCase c;
        c.name = "Sor Tek Linkhub: end of tactical action -> structure moved";
        c.faction = "ralnel";
        c.second = "saar";
        c.modules = kTE;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (monument system, the factory's system)
        c.setUp = [at](Game& g, int seat) {
            auto ok = [&](int a) {
                if (homeSys(g, a) || g.systems[size_t(a)].offBoard || g.systems[size_t(a)].isHyperlane() || a == g.mecatol()) return false;
                for (int16_t b : g.systems[size_t(a)].adj)
                    if (!g.systems[size_t(b)].isHyperlane() && !g.systems[size_t(b)].offBoard && !(g.systems[size_t(b)].d().anomalies & kSupernova)) return true;
                return false;
            };
            const int x = monumentPlanet(g, seat, [&](int x) { return hasAdjWith(g, g.planets[size_t(x)].system, ok); });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            const int f = adjWith(g, s, ok);
            for (int o = 0; o < kSides; ++o) g.systems[size_t(f)].space[size_t(o)] = Force();
            g.systems[size_t(f)].space[1].add(UnitType::SpaceDock, 1);  // (seat 1: the Saar)
            g.systems[size_t(f)].space[1].add(UnitType::Cruiser, 1);
            *at = {s, f};
            return true;
        };
        auto acted = std::make_shared<bool>(false);
        c.drive = [at, acted](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !*acted) return *acted = true, a = one(0), true;
            const int id = (at->second * 16 + 1) * 16 + int(UnitType::SpaceDock);
            if (d.tag == Tag::MonumentAbility && d.value == kMonAskRalnel && offers(d, id)) return a = one(id), true;
            if (d.tag == Tag::MonumentAbility && d.value == kMonAskRalnelTo)
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0) return a = one(o.id), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskRalnel);
            if (!m) return why = "no question", false;
            if (m->active != r.seat) return why = "asked in another player's turn", false;
            for (const Step& s : r.steps)
                if (s.tag == Tag::MonumentAbility && s.value == kMonAskRalnel && s.active != r.seat) return why = "asked in another player's turn", false;
            // at the end of the tactical action: after its activation, before the turn ended
            const Step* act = nullptr;
            for (const Step& s : r.steps) {
                if (&s == m) break;
                if (s.tag == Tag::Activate && s.player == r.seat) act = &s;
            }
            if (!act) return why = "not in a tactical action", false;
            if (kindBetween(r, EventKind::TurnEnd, act->events, m->events)) return why = "after the turn ended", false;
            const std::string from = r.before.systems[size_t(at->second)].d().name;
            if (!logHas(r, "moves Saar's Space Dock from " + from, m->events) && !logHas(r, "moves Saar's space dock from " + from, m->events))
                return why = "no move line", false;
            return true;
        };
        cases.push_back(c);
    }

    // ================================================================ 0.82.2: Discordant Stars' monuments, batch 1
    const int kDS = kModCodex | kModMonuments | kModDiscordant;
    // the other player activates the monument's system (round 2, when it carries none of their tokens)
    auto activateBy = [](const Game& g, const Decision& d, int who, int sys, Answer& a) {
        if (d.player != who) return false;
        if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(sys)].hasToken(who)) return a = one(0), true;
        if (d.tag == Tag::Activate && offers(d, sys)) return a = one(sys), true;
        return false;
    };
    auto activationOf = [](const MonRun& r, int who, int sys) -> const Step* {
        for (const Step& s : r.steps)
            if (s.tag == Tag::Activate && s.player == who && !s.answer.empty() && s.answer[0] == sys) return &s;
        return nullptr;
    };
    auto turnEndAfter = [](const MonRun& r, size_t from) {
        for (size_t i = from; i < r.log.size(); ++i)
            if (r.log[i].kind == EventKind::TurnEnd) return i;
        return r.log.size();
    };
    auto lineIn = [](const MonRun& r, const std::string& s, size_t from, size_t to) {
        for (size_t i = from; i < to && i < r.log.size(); ++i)
            if (r.log[i].text.find(s) != std::string::npos) return int(i);
        return -1;
    };
    // a planet for the monument in a system with exactly that one planet (fewer surprises), else any it fits
    auto lonePlanet = [](const Game& g, int seat) {
        const int x = monumentPlanet(g, seat, [&](int x) { return g.systems[size_t(g.planets[size_t(x)].system)].planets.size() == 1; });
        return x >= 0 ? x : monumentPlanet(g, seat);
    };
    auto clearSpace = [](Game& g, int s) {
        for (int o = 0; o < kSides; ++o) g.systems[size_t(s)].space[size_t(o)] = Force();
        g.systems[size_t(s)].tokens = 0;
    };

    // ---- Anvil of Atlas (Axis): ships produced in its system -> 1 trade good
    {
        MonCase c;
        c.name = "Anvil of Atlas: ships produced there -> 1 trade good";
        c.faction = "axis";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, lonePlanet, clearSpace](Game& g, int seat) {
            const int x = lonePlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);  // (the Anvil has PRODUCTION 3 itself)
            *at = g.planets[size_t(x)].system;
            clearSpace(g, *at);
            g.players[size_t(seat)].tradeGoods = 12;
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) {
            if (activateBy(g, d, seat, *at, a)) return true;
            if (d.player == seat && d.tag == Tag::Production && d.system == *at) {
                a = Answer{};
                a.data.assign(kUnitTypes + 1, 0);
                a.data[int(UnitType::Cruiser)] = 1;
                a.data[kUnitTypes] = -1;
                return true;
            }
            return false;
        };
        c.check = [at, activationOf, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const Step* act = activationOf(r, r.seat, *at);
            if (!act) return why = "no question (not activated)", false;
            const size_t end = turnEndAfter(r, act->events);
            const int made = lineIn(r, "produces 1 Cruiser in", act->events, end);
            if (made < 0) return why = "nothing produced", false;
            const int tg = lineIn(r, "gains 1 trade good (Anvil of Atlas)", size_t(made), end);
            if (tg < 0) return why = "no trade good after the production", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Sleeping World (Khrask): an invader lands on its planet with only the monument there -> a ground combat; it rolls 2 dice
    {
        MonCase c;
        c.name = "Sleeping World: invaded -> fights as a ground force";
        c.faction = "khrask";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.planets[size_t(x)].units[size_t(seat)] = Force();
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::Monument, 1);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            Force& f = g.systems[size_t(s)].space[size_t(otherSeat(g, seat))];
            f.add(UnitType::Carrier, 1);
            f.add(UnitType::Infantry, 2);
            *at = {x, s};
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat);
            if (activateBy(g, d, q, at->second, a)) return true;
            if (d.player == q && d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == at->second) {
                a = Answer{};
                a.data = {at->first, int(UnitType::Infantry), g.systems[size_t(at->second)].space[size_t(q)].count(UnitType::Infantry)};
                return true;
            }
            return false;
        };
        c.check = [at, activationOf, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const Step* act = activationOf(r, otherSeat(r.before, r.seat), at->second);
            if (!act) return why = "no question (not activated)", false;
            const size_t end = turnEndAfter(r, act->events);
            const std::string me = playerName(r.before, r.seat);
            const int roll = lineIn(r, me + " - Ground combat round 1:", act->events, end);
            if (roll < 0) return why = "no ground combat roll by the Sleeping World", false;
            // two dice (COMBAT 3 x2)
            const std::string& t = r.log[size_t(roll)].text;
            const size_t colon = t.find(": "), eq = t.find(" = ");
            int dice = 0;
            for (size_t i = colon + 2; i < eq; ++i) dice += t[i] == ' ' ? 1 : 0;
            if (dice + 1 != 2) return why = "it rolled " + std::to_string(dice + 1) + " dice, not 2", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Jatta's Palace (Cymiae): another player activates its system -> they discard 1 action card
    {
        MonCase c;
        c.name = "Jatta's Palace: activation -> 1 action card discarded";
        c.faction = "cymiae";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = g.planets[size_t(x)].system;
            g.systems[size_t(*at)].tokens = 0;
            const int q = otherSeat(g, seat);
            giveCard(g, q, "sabotage");
            giveCard(g, q, "mb");
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) { return activateBy(g, d, otherSeat(g, seat), *at, a); };
        c.check = [at, activationOf](const MonRun& r, std::string& why) {
            const int q = otherSeat(r.before, r.seat);
            const Step* act = activationOf(r, q, *at);
            if (!act) return why = "no question (not activated)", false;
            if (!logHas(r, "they must discard 1 action card (Jatta's Palace)", act->events)) return why = "no Palace line", false;
            const Step* next = act + 1;
            if (next > &r.steps.back() || next->tag != Tag::DiscardActionCard || next->player != q) return why = "the discard wasn't asked right away", false;
            if (!logHas(r, playerName(r.before, q) + " discards ", act->events)) return why = "nothing discarded", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Nightfall Fortress (Li-Zho): activation -> a different player's token removed / a command token gained
    for (int variant = 0; variant < 2; ++variant) {
        MonCase c;
        c.name = variant == 0 ? "Nightfall Fortress: activation -> another's token removed" : "Nightfall Fortress: activation -> a token gained";
        c.faction = "lizho";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = g.planets[size_t(x)].system;
            g.systems[size_t(*at)].tokens = 0;
            const int third = (otherSeat(g, seat) + 1) % g.numPlayers == seat ? (seat + 2) % g.numPlayers : (otherSeat(g, seat) + 1) % g.numPlayers;
            g.systems[size_t(*at)].tokens = uint16_t(1u << third);
            return true;
        };
        c.drive = [at, activateBy, variant](const Game& g, const Decision& d, int seat, Answer& a) {
            if (activateBy(g, d, otherSeat(g, seat), *at, a)) return true;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskLizho) {
                for (const Option& o : d.options)
                    if (o.enabled && ((variant == 0 && o.id >= 0 && o.id != seat) || (variant == 1 && o.id < 0))) return a = one(o.id), true;
            }
            return false;
        };
        c.check = [at, activationOf, variant](const MonRun& r, std::string& why) {
            const int q = otherSeat(r.before, r.seat);
            const Step* act = activationOf(r, q, *at);
            if (!act) return why = "no question (not activated)", false;
            const Step* m = firstMon(r, kMonAskLizho);
            if (!m || m != act + 1) return why = "not asked right after the activation", false;
            for (int o : m->offered)
                if (o == q) return why = "offered the activating player's own token", false;
            if (variant == 0) {
                if (!logHas(r, "removes ", m->events) || !logHas(r, "command token from", m->events)) return why = "no removal line", false;
                return true;
            }
            if (!logHas(r, "gains 1 command token (Nightfall Fortress)", m->events)) return why = "no gain line", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Diplomatic Enclave (Olradin): after their Diplomacy -> its planet readied
    {
        MonCase c;
        c.name = "Diplomatic Enclave: Diplomacy -> its planet readied";
        c.faction = "olradin";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.planets[size_t(x)].exhausted = true;
            *at = x;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat && d.tag == Tag::StrategyPick && g.round == 2 && offers(d, 1)) {
                for (const Option& o : d.options)
                    if (o.enabled && o.id != 1) return a = one(o.id), true;
            }
            if (d.player != seat) return false;
            if (d.tag == Tag::StrategyPick && g.round == 2 && offers(d, 1)) return a = one(1), true;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 11)) return a = one(11), true;
            if (d.tag == Tag::ReadyPlanets) {  // (not that one - the Enclave readies it)
                a = Answer{};
                for (const Option& o : d.options)
                    if (o.enabled && o.id != *at && a.picks.size() < 2) a.picks.push_back(o.id);
                return true;
            }
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const std::string pname = kPlanets[r.before.planets[size_t(*at)].def].name;
            const int i = [&] {
                for (size_t k = 0; k < r.log.size(); ++k)
                    if (r.log[k].text.find("readies " + pname + " (Diplomatic Enclave)") != std::string::npos) return int(k);
                return -1;
            }();
            if (i < 0) return why = "not readied", false;
            // after the Diplomacy primary (its ready step came before)
            const Step* ready = nullptr;
            for (const Step& s : r.steps)
                if (s.tag == Tag::ReadyPlanets && s.player == r.seat && s.events <= size_t(i)) ready = &s;
            if (!ready) return why = "not after Diplomacy", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Ado Citadel (Mirveda): ground combat in its system -> the opponent doesn't roll in round 1
    {
        MonCase c;
        c.name = "Ado Citadel: ground combat -> no opponent rolls in round 1";
        c.faction = "mirveda";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::Infantry, 2);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            Force& f = g.systems[size_t(s)].space[size_t(otherSeat(g, seat))];
            f.add(UnitType::Carrier, 2);
            f.add(UnitType::Infantry, 6);
            *at = {x, s};
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat);
            if (activateBy(g, d, q, at->second, a)) return true;
            if (d.player == q && d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == at->second) {
                a = Answer{};
                a.data = {at->first, int(UnitType::Infantry), g.systems[size_t(at->second)].space[size_t(q)].count(UnitType::Infantry)};
                return true;
            }
            return false;
        };
        c.check = [at, activationOf, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const int q = otherSeat(r.before, r.seat);
            const Step* act = activationOf(r, q, at->second);
            if (!act) return why = "no question (not activated)", false;
            const size_t end = turnEndAfter(r, act->events);
            const std::string qn = playerName(r.before, q), me = playerName(r.before, r.seat);
            if (lineIn(r, "make no combat rolls this round (Ado Citadel)", act->events, end) < 0) return why = "no Citadel line", false;
            if (lineIn(r, qn + " - Ground combat round 1:", act->events, end) >= 0) return why = "the invader rolled in round 1", false;
            if (lineIn(r, me + " - Ground combat round 1:", act->events, end) < 0) return why = "the Mirveda didn't roll in round 1", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Perdition Array (Veldyr): space cannon offense from its system -> Maneuvering Jets can't cancel
    {
        MonCase c;
        c.name = "Perdition Array: space cannon hits can't be canceled";
        c.faction = "veldyr";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::PDS, 4);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            const int q = otherSeat(g, seat);
            g.systems[size_t(s)].space[size_t(q)].add(UnitType::Dreadnought, 2);
            g.systems[size_t(s)].space[size_t(q)].add(UnitType::Destroyer, 2);
            giveCard(g, q, "mjets");
            *at = s;
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) {
            if (activateBy(g, d, otherSeat(g, seat), *at, a)) return true;
            if (d.player == seat && d.tag == Tag::SpaceCannonFire) return a = one(1), true;
            return false;
        };
        c.check = [at, activationOf, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const int q = otherSeat(r.before, r.seat);
            const Step* act = activationOf(r, q, *at);
            if (!act) return why = "no question (not activated)", false;
            const size_t end = turnEndAfter(r, act->events);
            const std::string me = playerName(r.before, r.seat);
            const int roll = lineIn(r, me + " - Space cannon at", act->events, end);
            if (roll < 0) return why = "no space cannon", false;
            if (r.log[size_t(roll)].text.find("= 0 hits") != std::string::npos) return why = "no hits rolled (inconclusive)", false;
            if (lineIn(r, "can't be canceled (Perdition Array)", size_t(roll), end) < 0) return why = "no Perdition line", false;
            for (const Step& s : r.steps)
                if (s.events >= size_t(roll) && s.events < end && s.player == q && s.tag == Tag::PlayActionCard)
                    for (int o : s.offered)
                        if (o >= 0 && o < kActionCardCount && !std::strcmp(kActionCards[o].key, "mjets"))
                            return why = "Maneuvering Jets was offered", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Freehold Starport (Nokar): their carrier and infantry leave its system although their command token is there
    {
        MonCase c;
        c.name = "Freehold Starport: ships leave despite their token";
        c.faction = "nokar";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            auto ok = [&](int a) { return !homeSys(g, a) && !g.systems[size_t(a)].offBoard && !g.systems[size_t(a)].isHyperlane() && a != g.mecatol() &&
                                          !anomaliesFor(g, seat, a); };
            const int x = monumentPlanet(g, seat, [&](int x) { return hasAdjWith(g, g.planets[size_t(x)].system, ok); });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            const int b = adjWith(g, s, ok);
            clearSpace(g, s);
            clearSpace(g, b);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Carrier, 1);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Infantry, 2);
            g.systems[size_t(s)].tokens = uint16_t(1u << seat);
            *at = {s, b};
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(at->second)].hasToken(seat)) return a = one(0), true;
            if (d.tag == Tag::Activate && offers(d, at->second)) return a = one(at->second), true;
            if (d.kind == DecisionKind::Move && g.activeSystem == at->second && g.systems[size_t(at->first)].space[size_t(seat)].count(UnitType::Carrier)) {
                a = Answer{};
                a.data = {at->first, -1, int(UnitType::Carrier), 1, at->first, -1, int(UnitType::Infantry), 2};
                return true;
            }
            return false;
        };
        c.check = [at, activationOf](const MonRun& r, std::string& why) {
            const Step* act = activationOf(r, r.seat, at->second);
            if (!act) return why = "no question (not activated)", false;
            if (!r.refused.empty()) return why = "refused: " + r.refused, false;
            const Step* mv = nullptr;
            for (const Step* s = act + 1; s <= &r.steps.back(); ++s)
                if (s->kind == DecisionKind::Move) {
                    mv = s;
                    break;
                }
            if (!mv || mv->data.empty()) return why = "no move", false;
            bool fromToken = false;  // (a carrier out of the system with their token)
            for (size_t i = 0; i + 3 < mv->data.size(); i += 4)
                if (mv->data[i] == at->first && mv->data[i + 2] == int(UnitType::Carrier)) fromToken = true;
            if (!fromToken) return why = "the carrier didn't come from the Starport's system", false;
            if (!(r.before.systems[size_t(at->first)].hasToken(r.seat))) return why = "no token there", false;
            const std::string bname = r.before.systems[size_t(at->second)].d().name;
            if (!logHas(r, "moves 1 Carrier, 2 Infantry into " + bname, act->events)) return why = "the carrier and its infantry didn't arrive", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Twilight Throne (Edyn): a free mech on their turn / at the start of a ground combat there
    for (int variant = 0; variant < 2; ++variant) {
        MonCase c;
        c.name = variant == 0 ? "Twilight Throne: free option -> a mech, the turn goes on" : "Twilight Throne: invaded -> a mech before the combat";
        c.faction = "edyn";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, clearSpace, variant](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            if (variant == 1) {
                clearSpace(g, s);
                Force& f = g.systems[size_t(s)].space[size_t(otherSeat(g, seat))];
                f.add(UnitType::Carrier, 2);
                f.add(UnitType::Infantry, 4);
            }
            *at = {x, s};
            return true;
        };
        c.drive = [at, activateBy, variant](const Game& g, const Decision& d, int seat, Answer& a) {
            if (variant == 0) {
                if (d.player == seat && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 647)) return a = one(647), true;
                return false;
            }
            const int q = otherSeat(g, seat);
            if (activateBy(g, d, q, at->second, a)) return true;
            if (d.player == q && d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == at->second) {
                a = Answer{};
                a.data = {at->first, int(UnitType::Infantry), g.systems[size_t(at->second)].space[size_t(q)].count(UnitType::Infantry)};
                return true;
            }
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskEdyn) return a = one(1), true;
            return false;
        };
        c.check = [at, variant, activationOf, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const std::string pname = kPlanets[r.before.planets[size_t(at->first)].def].name;
            if (variant == 0) {
                const Step* opt = nullptr;
                for (const Step& s : r.steps)
                    if (s.tag == Tag::ActionChoice && s.player == r.seat && !s.answer.empty() && s.answer[0] == 647) {
                        opt = &s;
                        break;
                    }
                if (!opt) return why = "no question (not offered)", false;
                if (!logHas(r, "produces 1 Mech on " + pname + " (Twilight Throne)", opt->events)) return why = "no mech", false;
                const Step* next = opt + 1;  // the turn goes on: the same player picks an action again
                if (next > &r.steps.back() || next->tag != Tag::ActionChoice || next->player != r.seat) return why = "the turn didn't go on", false;
                for (int o : next->offered)
                    if (o == 647) return why = "offered again while exhausted", false;
                return true;
            }
            const Step* m = firstMon(r, kMonAskEdyn);
            if (!m) return why = "no question", false;
            const size_t end = turnEndAfter(r, m->events);
            const int mech = lineIn(r, "produces 1 Mech on " + pname + " (Twilight Throne)", m->events, end);
            const int fight = lineIn(r, " - Ground combat round 1:", m->events, end);
            if (mech < 0) return why = "no mech", false;
            if (fight < 0 || fight < mech) return why = "not before the ground combat", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Zha'Ren Foundry (Roh'Dhna): production in its system -> 2 cheaper
    {
        MonCase c;
        c.name = "Zha'Ren Foundry: production there -> 2 cheaper";
        c.faction = "rohdhna";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, lonePlanet, clearSpace](Game& g, int seat) {
            const int x = lonePlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            for (PlanetState& ps : g.planets)  // (a dock moved here from elsewhere: the unit limit)
                if (ps.units[size_t(seat)].count(UnitType::SpaceDock)) {
                    ps.units[size_t(seat)].remove(UnitType::SpaceDock, 1);
                    break;
                }
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::SpaceDock, 1);
            *at = g.planets[size_t(x)].system;
            clearSpace(g, *at);
            g.players[size_t(seat)].tradeGoods = 12;
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) {
            if (activateBy(g, d, seat, *at, a)) return true;
            if (d.player == seat && d.tag == Tag::Production && d.system == *at) {
                a = Answer{};
                a.data.assign(kUnitTypes + 1, 0);
                a.data[int(UnitType::Cruiser)] = 2;
                a.data[kUnitTypes] = -1;
                return true;
            }
            return false;
        };
        c.check = [at, activationOf](const MonRun& r, std::string& why) {
            const Step* act = activationOf(r, r.seat, *at);
            if (!act) return why = "no question (not activated)", false;
            const Step* prod = nullptr;
            for (const Step* s = act; s <= &r.steps.back(); ++s)
                if (s->tag == Tag::Production && s->player == r.seat) {
                    prod = s;
                    break;
                }
            if (!prod) return why = "no production", false;
            if (prod->context.find("Zha'Ren Foundry: 2 of that") == std::string::npos) return why = "no Foundry note: " + prod->context, false;
            if (prod->value < 2) return why = "discount " + std::to_string(prod->value), false;
            // the payment: 2 cruisers cost 4 - 2
            for (const Step* s = prod + 1; s <= &r.steps.back() && s->player == r.seat; ++s)
                if (s->kind == DecisionKind::Pay) return s->amount == 2 - (prod->value - 2) || s->amount <= 2 ? true : (why = "paid " + std::to_string(s->amount), false);
            return true;  // (paid from trade goods without a question)
        };
        cases.push_back(c);
    }

    // ---- Queen's Rest (Kortali): their dreadnought destroyed in its system -> a roll; on a hit, 1 hit on the attacker's ships
    {
        MonCase c;
        c.name = "Queen's Rest: a unit lost there -> a roll and a hit";
        c.faction = "kortali";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Cruiser, 2);
            Force& f = g.systems[size_t(s)].space[size_t(otherSeat(g, seat))];
            f.add(UnitType::Dreadnought, 3);
            f.add(UnitType::Destroyer, 3);
            *at = s;
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) { return activateBy(g, d, otherSeat(g, seat), *at, a); };
        c.check = [at, activationOf, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const int q = otherSeat(r.before, r.seat);
            const Step* act = activationOf(r, q, *at);
            if (!act) return why = "no question (not activated)", false;
            const size_t end = turnEndAfter(r, act->events);
            const std::string me = playerName(r.before, r.seat);
            const int lost = lineIn(r, me + " loses 1 Cruiser", act->events, end);
            const int lost2 = lineIn(r, me + " loses 2 Cruisers", act->events, end);
            const int first = lost >= 0 ? lost : lost2;
            if (first < 0) return why = "no cruiser lost (inconclusive)", false;
            const int roll = lineIn(r, me + " - Queen's Rest (Cruiser lost):", size_t(first), end);
            if (roll < 0) return why = "no Queen's Rest roll after the loss", false;
            // the next combat roll comes after it (the roll is made as the hits are assigned)
            const int next = lineIn(r, " - Space combat round", size_t(first), end);
            if (next >= 0 && next < roll) return why = "the roll came after the next combat round", false;
            if (r.log[size_t(roll)].text.find("= 1 hit") != std::string::npos && lineIn(r, "avenges the fallen", size_t(roll), end) < 0)
                return why = "a hit rolled but none produced", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Halls of Valor (Ghemina): its system is adjacent (for their movement) to a system with 2 of their PDS
    {
        MonCase c;
        c.name = "Halls of Valor: adjacent to a system with 2 PDS";
        c.faction = "ghemina";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int h = g.planets[size_t(x)].system;
            // a planet 3 or more systems away for 2 PDS
            int far = -1;
            for (size_t y = 0; y < g.planets.size() && far < 0; ++y) {
                const int s = g.planets[y].system;
                if (s == h || homeSys(g, s) || g.systems[size_t(s)].offBoard || isDMZ(g, int(y)) || isStation(g, int(y))) continue;
                if (hexDistance(g.systems[size_t(s)].pos, g.systems[size_t(h)].pos) >= 3) far = int(y);
            }
            if (far < 0) return false;
            clearPlanet(g, far);
            g.planets[size_t(far)].owner = int8_t(seat);
            g.planets[size_t(far)].units[size_t(seat)].add(UnitType::PDS, 2);
            g.systems[size_t(h)].space[size_t(seat)].add(UnitType::Cruiser, 1);
            *at = {h, g.planets[size_t(far)].system};
            return true;
        };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [at](const MonRun& r, std::string& why) {
            const std::vector<int> nb = routeNeighbors(r.before, r.seat, at->first);
            if (std::find(nb.begin(), nb.end(), at->second) == nb.end()) return why = "not a neighbour of the Halls", false;
            const std::vector<int> back = routeNeighbors(r.before, r.seat, at->second);
            if (std::find(back.begin(), back.end(), at->first) == back.end()) return why = "not linked both ways", false;
            if (!shipReach(r.before, r.seat, at->first, at->second, UnitType::Cruiser, 0).ok) return why = "a cruiser can't reach it", false;
            const int other = otherSeat(r.before, r.seat);
            const std::vector<int> theirs = routeNeighbors(r.before, other, at->first);
            if (std::find(theirs.begin(), theirs.end(), at->second) != theirs.end() &&
                hexDistance(r.before.systems[size_t(at->first)].pos, r.before.systems[size_t(at->second)].pos) > 1)
                return why = "other players get the link too", false;
            return true;
        };
        cases.push_back(c);
    }

    // ================================================================ 0.82.3: Discordant Stars' monuments, batch 2
    // a system next to s that ships can simply be in (no home, Mecatol, hyperlane, anomaly or off-board)
    auto plainNext = [](const Game& g, int s, int seat) {
        return adjWith(g, s, [&](int a) {
            return !homeSys(g, a) && !g.systems[size_t(a)].offBoard && !g.systems[size_t(a)].isHyperlane() && a != g.mecatol() && !anomaliesFor(g, seat, a);
        });
    };
    // the seat's own tactical action activating sys (round 2, once)
    auto activateOwn = [](const Game& g, const Decision& d, int seat, int sys, Answer& a) {
        if (d.player != seat) return false;
        if (d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(sys)].hasToken(seat)) return a = one(0), true;
        if (d.tag == Tag::Activate && offers(d, sys)) return a = one(sys), true;
        return false;
    };

    // ---- Gravelord's Keep (Myko-Mentori): 3 fighters there and next door need no capacity
    {
        MonCase c;
        c.name = "Gravelord's Keep: 3 fighters without capacity, there and next door";
        c.faction = "mykomentori";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, clearSpace, plainNext](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) { return plainNext(g, g.planets[size_t(x)].system, seat) >= 0; });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system, b = plainNext(g, s, seat);
            clearSpace(g, s);
            clearSpace(g, b);
            *at = {s, b};
            return true;
        };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [at](const MonRun& r, std::string& why) {
            const Game& g = r.before;
            for (int s : {at->first, at->second}) {
                Force f;
                f.add(UnitType::Fighter, 3);
                if (overflowOf(g, r.seat, s, f) != 0) return why = "3 fighters don't fit in " + std::string(g.systems[size_t(s)].d().name), false;
                f.add(UnitType::Fighter, 1);
                if (overflowOf(g, r.seat, s, f) != 1) return why = "a 4th fighter fits too", false;
            }
            Force f;
            f.add(UnitType::Fighter, 3);
            if (overflowOf(g, otherSeat(g, r.seat), at->first, f) == 0) return why = "other players get the room too", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Gravelord's Keep: an omen die used -> a free fighter in its system
    {
        MonCase c;
        c.name = "Gravelord's Keep: an omen die used -> 1 free fighter";
        c.faction = "mykomentori";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Dreadnought, 2);  // (they hit on 5: an Omen 6 is a sure hit)
            Force& f = g.systems[size_t(s)].space[size_t(otherSeat(g, seat))];
            f.add(UnitType::Dreadnought, 2);
            f.add(UnitType::Destroyer, 2);
            for (int8_t& o : g.players[size_t(seat)].dsOmen) o = 6;
            *at = s;
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) {
            if (activateBy(g, d, otherSeat(g, seat), *at, a)) return true;
            if (d.player == seat && d.tag == Tag::Faction && d.ability == int(Ab::DsOmen)) {  // (Divination: the first omen offered)
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0 && o.id != 99999) return a = one(o.id), true;
            }
            return false;
        };
        c.check = [at, activationOf, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const Step* act = activationOf(r, otherSeat(r.before, r.seat), *at);
            if (!act) return why = "no question (not activated)", false;
            const size_t end = turnEndAfter(r, act->events);
            const int omen = lineIn(r, "spends an Omen die", act->events, end);
            if (omen < 0) return why = "no omen die used (inconclusive)", false;
            const Step* m = firstMon(r, kMonAskMyko);
            if (!m) return why = "the free fighter wasn't offered", false;
            if (lineIn(r, "produces 1 fighter in " + std::string(r.before.systems[size_t(*at)].d().name) + " (Gravelord's Keep)", size_t(omen), end) < 0)
                return why = "no fighter produced", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Flotilla Cyclotron (Dih-Mohn): after their activation, ships in its system reach 1 further
    {
        MonCase c;
        c.name = "Flotilla Cyclotron: +1 move from its system";
        c.faction = "dihmohn";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            for (size_t x = 0; x < g.planets.size(); ++x) {
                const int s = g.planets[x].system;
                if (homeSys(g, s) || g.systems[size_t(s)].offBoard || !monumentFitsPlanet(g, seat, int(x)) || isDMZ(g, int(x))) continue;
                Game t = g;
                monumentOn(t, seat, int(x));
                clearSpace(t, s);
                t.systems[size_t(s)].space[size_t(seat)].add(UnitType::Cruiser, 1);
                const int mb = moveBonusMax(t, seat);
                for (size_t b = 0; b < t.systems.size(); ++b) {
                    if (int(b) == s || homeSys(t, int(b)) || t.systems[b].offBoard || t.systems[b].isHyperlane() || int(b) == t.mecatol()) continue;
                    bool others = false;
                    for (int q = 0; q < kSides; ++q) others = others || (q != seat && t.systems[b].space[size_t(q)].total() > 0);
                    if (others || anomaliesFor(t, seat, int(b))) continue;
                    if (shipReach(t, seat, s, int(b), UnitType::Cruiser, mb).ok || !shipReach(t, seat, s, int(b), UnitType::Cruiser, mb + 1).ok) continue;
                    g = t;
                    g.systems[b].tokens = 0;
                    *at = {s, int(b)};
                    return true;
                }
            }
            return false;
        };
        c.drive = [at, activateOwn](const Game& g, const Decision& d, int seat, Answer& a) {
            if (activateOwn(g, d, seat, at->second, a)) return true;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskDihmohn) return a = one(1), true;
            if (d.player == seat && d.kind == DecisionKind::Move && g.activeSystem == at->second &&
                g.systems[size_t(at->first)].space[size_t(seat)].count(UnitType::Cruiser)) {
                a = Answer{};
                a.data = {at->first, -1, int(UnitType::Cruiser), 1};
                return true;
            }
            return false;
        };
        c.check = [at, activationOf, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const Step* act = activationOf(r, r.seat, at->second);
            if (!act) return why = "no question (not activated)", false;
            const size_t end = turnEndAfter(r, act->events);
            const Step* m = firstMon(r, kMonAskDihmohn);
            if (!m) return why = "the Cyclotron wasn't offered", false;
            if (lineIn(r, "move 1 further this tactical action (Flotilla Cyclotron)", act->events, end) < 0) return why = "no Cyclotron line", false;
            if (lineIn(r, "moves 1 Cruiser into " + std::string(r.before.systems[size_t(at->second)].d().name), act->events, end) < 0)
                return why = "the cruiser didn't get there", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Bounty Brokerage (Vaden): a debtor with ships next to it replenishes (Trade) -> 1 of their commodities taken
    {
        MonCase c;
        c.name = "Bounty Brokerage: a debtor replenishes -> 1 commodity taken";
        c.faction = "vaden";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            const int q = otherSeat(g, seat);
            g.systems[size_t(s)].space[size_t(q)].add(UnitType::Destroyer, 1);
            g.dsDebt[size_t(q)] = 1;
            g.players[size_t(q)].commodities = 0;
            *at = s;
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat);
            if (d.tag == Tag::StrategyPick && g.round == 2) {
                if (d.player == q && offers(d, 4)) return a = one(4), true;
                if (d.player != q && offers(d, 4))
                    for (const Option& o : d.options)
                        if (o.enabled && o.id != 4) return a = one(o.id), true;
            }
            if (d.player == q && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 14)) return a = one(14), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const std::string them = playerName(r.before, otherSeat(r.before, r.seat));
            const Step* m = firstMon(r, kMonAskVaden);
            if (!m) return why = "no question (no replenish)", false;
            if (!logHas(r, "takes 1 of " + them + "'s commodities (Bounty Brokerage)", m->events)) return why = "no commodity taken", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Corsairs' Cove (Florzen): fighters beyond the capacity onto the card; then off it at the start of the next turn
    {
        MonCase c;
        c.name = "Corsairs' Cove: fighters onto the card, then off it";
        c.faction = "florzen";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            const int home = g.players[size_t(seat)].homeSystem;
            if (x < 0 || home < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Carrier, 1);
            clearSpace(g, home);
            g.players[size_t(seat)].tradeGoods = 10;
            *at = {s, home};
            return true;
        };
        c.drive = [at, activateOwn](const Game& g, const Decision& d, int seat, Answer& a) {
            if (activateOwn(g, d, seat, at->second, a)) return true;
            if (d.player == seat && d.tag == Tag::Production && d.system == at->second) {
                a = Answer{};
                a.data.assign(kUnitTypes + 1, 0);
                a.data[int(UnitType::Fighter)] = 4;
                a.data[kUnitTypes] = -1;
                return true;
            }
            if (d.player == seat && d.tag == Tag::MonumentAbility && (d.value == kMonAskFlorzenOut || d.value == kMonAskFlorzen)) {
                int most = -1;
                for (const Option& o : d.options) most = std::max(most, o.id);
                return a = one(most), true;
            }
            return false;
        };
        c.check = [at, activationOf, lineIn](const MonRun& r, std::string& why) {
            const Step* act = activationOf(r, r.seat, at->second);
            if (!act) return why = "no question (not activated)", false;
            const int put = lineIn(r, "on Corsairs' Cove instead", act->events, r.log.size());
            if (put < 0) return why = "no fighters onto the card", false;
            if (r.log[size_t(put)].text.find("places 4 fighters on") == std::string::npos) return why = "not all 4: " + r.log[size_t(put)].text, false;
            const Step* out = firstMon(r, kMonAskFlorzenOut);
            if (!out) return why = "not offered at the start of the next turn", false;
            if (lineIn(r, "from Corsairs' Cove in " + std::string(r.before.systems[size_t(at->first)].d().name), size_t(put), r.log.size()) < 0)
                return why = "no fighter produced from the card", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Mirrorforge (Mortheus): ACTION - a neighbour's ships pulled in (4 resources' worth), then a space combat
    {
        MonCase c;
        c.name = "Mirrorforge: ships pulled in -> space combat";
        c.faction = "mortheus";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, clearSpace, plainNext](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) { return plainNext(g, g.planets[size_t(x)].system, seat) >= 0; });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system, b = plainNext(g, s, seat);
            clearSpace(g, s);
            clearSpace(g, b);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Dreadnought, 2);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Cruiser, 2);
            Force& f = g.systems[size_t(b)].space[size_t(otherSeat(g, seat))];
            f.add(UnitType::Destroyer, 2);
            f.add(UnitType::Cruiser, 1);
            f.add(UnitType::Carrier, 1);
            *at = {s, b};
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            const int card = monumentIndex("mortheus");
            if (d.player == seat && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 3100 + card)) return a = one(3100 + card), true;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskMortheus) {
                const int id = at->second * 16 + otherSeat(g, seat);
                return a = one(offers(d, id) ? id : -1), true;
            }
            return false;
        };
        c.check = [at, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskMortheus);
            if (!m) return why = "no question (not used)", false;
            const size_t end = turnEndAfter(r, m->events);
            const std::string sname = r.before.systems[size_t(at->first)].d().name;
            const int moved = lineIn(r, "into " + sname + " (Mirrorforge)", m->events, end);
            if (moved < 0) return why = "nothing moved in", false;
            // the cheapest first, until 4 resources: 2 destroyers + the cruiser (the carrier stays)
            if (r.log[size_t(moved)].text.find("Carrier") != std::string::npos) return why = "more than 4 resources' worth moved: " + r.log[size_t(moved)].text, false;
            if (lineIn(r, "resolve a space combat in " + sname, size_t(moved), end) < 0) return why = "no space combat line", false;
            if (lineIn(r, " - Combat round 1", size_t(moved), end) < 0) return why = "no combat roll", false;
            int asked = 0;  // (the ships were picked by their owner)
            for (const Step& s : r.steps)
                if (s.tag == Tag::MonumentAbility && s.value == kMonAskMortheusShip) asked += s.player == otherSeat(r.before, r.seat) ? 1 : 100;
            if (asked < 1 || asked >= 100) return why = "the owner didn't choose the ships", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- The Maw (Nivyn): others' ships can't leave or pass; theirs reach the Wound
    {
        MonCase c;
        c.name = "The Maw: others can't leave or pass; next to the Wound";
        c.faction = "nivyn";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, clearSpace, plainNext](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) { return plainNext(g, g.planets[size_t(x)].system, seat) >= 0; });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Cruiser, 1);
            g.systems[size_t(s)].space[size_t(otherSeat(g, seat))].add(UnitType::Cruiser, 1);
            // the Wound 3+ systems away
            int w = -1;
            for (size_t b = 0; b < g.systems.size() && w < 0; ++b)
                if (!homeSys(g, int(b)) && !g.systems[b].offBoard && !g.systems[b].isHyperlane() && int(b) != g.mecatol() &&
                    hexDistance(g.systems[b].pos, g.systems[size_t(s)].pos) >= 3)
                    w = int(b);
            if (w < 0) return false;
            g.dsWound = int16_t(w);
            *at = {s, w};
            return true;
        };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [at, plainNext](const MonRun& r, std::string& why) {
            const Game& g = r.before;
            const int s = at->first, q = otherSeat(g, r.seat), b = plainNext(g, s, r.seat);
            if (b < 0) return why = "no neighbour", false;
            if (shipReach(g, q, s, b, UnitType::Cruiser, 0).ok) return why = "another player's cruiser leaves the Maw", false;
            if (!shipReach(g, r.seat, s, b, UnitType::Cruiser, 0).ok) return why = "the Nivyn's own cruiser can't leave", false;
            Game t = g;  // (not through it: without the Nivyn's cruiser there, nothing else blocks)
            t.systems[size_t(s)].space[size_t(r.seat)] = Force();
            if (passableFor(t, q, s)) return why = "another player's ships pass through it", false;
            const std::vector<int> nb = routeNeighbors(g, r.seat, s);
            if (std::find(nb.begin(), nb.end(), at->second) == nb.end()) return why = "not next to the Wound for the Nivyn", false;
            const std::vector<int> theirs = routeNeighbors(g, q, s);
            if (std::find(theirs.begin(), theirs.end(), at->second) != theirs.end()) return why = "others get the Wound link too", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- AI Conclave (Augurs): another planet explored -> the Conclave's planet instead, trade goods for scored secrets
    {
        MonCase c;
        c.name = "AI Conclave: explores its planet instead, +TG per secret";
        c.faction = "augers";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (the Conclave's planet, the planet taken)
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) { return kPlanets[g.planets[size_t(x)].def].trait != Trait::None; });
            if (x < 0) return false;
            int y = -1;  // an unclaimed planet with a trait, alone in its system
            for (size_t k = 0; k < g.planets.size() && y < 0; ++k) {
                const int s = g.planets[k].system;
                if (int(k) == x || s == g.planets[size_t(x)].system || homeSys(g, s) || g.systems[size_t(s)].offBoard || s == g.mecatol() ||
                    g.systems[size_t(s)].planets.size() != 1 || kPlanets[g.planets[k].def].trait == Trait::None || isDMZ(g, int(k)) || isStation(g, int(k)) ||
                    g.systems[size_t(s)].d().anomalies)
                    continue;
                y = int(k);
            }
            if (y < 0) return false;
            monumentOn(g, seat, x);
            clearPlanet(g, y);
            g.planets[size_t(y)].owner = -1;
            const int t = g.planets[size_t(y)].system;
            clearSpace(g, t);
            g.systems[size_t(t)].space[size_t(seat)].add(UnitType::Carrier, 1);
            g.systems[size_t(t)].space[size_t(seat)].add(UnitType::Infantry, 2);
            // a scored secret objective (1 trade good)
            for (int o = 0; o < kObjectiveCount; ++o)
                if (kObjectives[o].kind == 3) {
                    g.players[size_t(seat)].scored.push_back(int16_t(o));
                    g.players[size_t(seat)].vp = int8_t(g.players[size_t(seat)].vp + 1);
                    break;
                }
            *at = {x, y};
            return true;
        };
        c.drive = [at, activateOwn](const Game& g, const Decision& d, int seat, Answer& a) {
            const int t = g.planets[size_t(at->second)].system;
            if (activateOwn(g, d, seat, t, a)) return true;
            if (d.player == seat && d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == t) {
                a = Answer{};
                a.data = {at->second, int(UnitType::Infantry), 2};
                return true;
            }
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskAugurs) return a = one(1), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskAugurs);
            if (!m) return why = "no question (nothing explored)", false;
            const std::string xn = kPlanets[r.before.planets[size_t(at->first)].def].name, yn = kPlanets[r.before.planets[size_t(at->second)].def].name;
            if (!logHas(r, "explores " + xn + " instead of " + yn + " (AI Conclave)", m->events)) return why = "no Conclave line", false;
            if (!logHas(r, "gains 1 trade good (AI Conclave)", m->events)) return why = "no trade good for the scored secret", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Dawnstar HQ (Tnelis): its planet's ground combat lost -> every unit there destroyed; the planet stays theirs
    {
        MonCase c;
        c.name = "Dawnstar HQ: ground combat lost -> every unit destroyed";
        c.faction = "tnelis";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, clearSpace, lonePlanet](Game& g, int seat) {
            const int x = lonePlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            Force& f = g.systems[size_t(s)].space[size_t(otherSeat(g, seat))];
            f.add(UnitType::Carrier, 2);
            f.add(UnitType::Infantry, 7);
            *at = {x, s};
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat);
            if (activateBy(g, d, q, at->second, a)) return true;
            if (d.player == q && d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == at->second) {
                a = Answer{};
                a.data = {at->first, int(UnitType::Infantry), g.systems[size_t(at->second)].space[size_t(q)].count(UnitType::Infantry)};
                return true;
            }
            return false;
        };
        c.check = [at, activationOf, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const int q = otherSeat(r.before, r.seat);
            const Step* act = activationOf(r, q, at->second);
            if (!act) return why = "no question (not activated)", false;
            const size_t end = turnEndAfter(r, act->events);
            const std::string xn = kPlanets[r.before.planets[size_t(at->first)].def].name;
            const Step* m = firstMon(r, kMonAskTnelisLoss);
            if (!m) return why = "not offered (the Tnelis won? inconclusive)", false;
            const int line = lineIn(r, "destroys every unit on " + xn + " (Dawnstar HQ)", act->events, end);
            if (line < 0) return why = "no Dawnstar line", false;
            if (lineIn(r, " - Ground combat round", size_t(line), end) >= 0) return why = "the ground combat went on", false;
            // nobody took it in that turn (the snapshot at the next question / the end)
            for (size_t i = stepIndex(r, m) + 1; i < r.steps.size(); ++i)
                if (r.steps[i].events >= end) break;
            if (r.after.planets[size_t(at->first)].owner != r.seat && r.after.planets[size_t(at->first)].units[size_t(q)].groundForces() > 0)
                return why = "the invader holds the planet", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Dawnstar HQ: at the start of their turn, 1 coexisting unit of another player there destroyed
    {
        MonCase c;
        c.name = "Dawnstar HQ: start of turn -> a coexisting unit destroyed";
        c.faction = "tnelis";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);
        c.setUp = [at, lonePlanet](Game& g, int seat) {
            const int x = lonePlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int q = otherSeat(g, seat);
            g.planets[size_t(x)].units[size_t(q)].add(UnitType::Infantry, 2);
            g.planets[size_t(x)].coexist = uint16_t(1u << q);
            *at = {x, g.planets[size_t(x)].system};
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskTnelis) {
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0 && (o.id / 16) % 16 == otherSeat(g, seat)) return a = one(o.id), true;
            }
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskTnelis);
            if (!m) return why = "not offered", false;
            if (m->active != r.seat) return why = "not on their turn", false;
            const std::string xn = kPlanets[r.before.planets[size_t(at->first)].def].name;
            if (!logHas(r, "destroys 1 of " + playerName(r.before, otherSeat(r.before, r.seat)) + "'s Infantry on " + xn + " (Dawnstar HQ)", m->events))
                return why = "no unit destroyed", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Aylor Raider Hoard (Vaylerian): +1 commodity value while others' ships are near
    {
        MonCase c;
        c.name = "Aylor Raider Hoard: +1 commodity value with ships near";
        c.faction = "vaylerian";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace, plainNext](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) { return plainNext(g, g.planets[size_t(x)].system, seat) >= 0; });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            for (int16_t a : g.systems[size_t(s)].adj)
                for (int q = 0; q < kSides; ++q)
                    if (q != seat) g.systems[size_t(a)].space[size_t(q)] = Force();
            *at = s;
            return true;
        };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [at, plainNext](const MonRun& r, std::string& why) {
            Game g = r.before;
            const int base = commodityValue(g, r.seat);
            const int b = plainNext(g, *at, r.seat);
            g.systems[size_t(b)].space[size_t(otherSeat(g, r.seat))].add(UnitType::Destroyer, 1);
            if (commodityValue(g, r.seat) != base + 1) return why = "not +1 with a ship next to it", false;
            g.systems[size_t(b)].space[size_t(otherSeat(g, r.seat))] = Force();
            g.systems[size_t(*at)].space[size_t(otherSeat(g, r.seat))].add(UnitType::Destroyer, 1);
            if (commodityValue(g, r.seat) != base + 1) return why = "not +1 with a ship in its system", false;
            if (commodityValue(g, otherSeat(g, r.seat)) != commodityValue(r.before, otherSeat(g, r.seat))) return why = "others get it too", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Aylor Raider Hoard: trade goods spent in a space combat next to it (Munitions Reserves) -> 1 action card
    {
        MonCase c;
        c.name = "Aylor Raider Hoard: trade goods spent in combat -> 1 action card";
        c.faction = "vaylerian";
        c.second = "letnev";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            const int q = otherSeat(g, seat);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Cruiser, 2);
            g.systems[size_t(s)].space[size_t(q)].add(UnitType::Dreadnought, 2);
            g.systems[size_t(s)].space[size_t(q)].add(UnitType::Destroyer, 2);
            g.players[size_t(q)].tradeGoods = 6;
            *at = s;
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat);
            if (activateBy(g, d, q, *at, a)) return true;
            if (d.player == q && d.tag == Tag::Faction && d.ability == int(Ab::Munitions) && g.activeSystem == *at) {
                if (offers(d, 1)) return a = one(1), true;
            }
            return false;
        };
        c.check = [at, activationOf, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const Step* act = activationOf(r, otherSeat(r.before, r.seat), *at);
            if (!act) return why = "no question (not activated)", false;
            const size_t end = turnEndAfter(r, act->events);
            bool spent = false;
            for (const Step& s : r.steps)
                if (s.tag == Tag::Faction && s.ability == int(Ab::Munitions) && !s.answer.empty() && s.answer[0] == 1) spent = true;
            if (!spent) return why = "no Munitions Reserves (inconclusive)", false;
            const Step* m = firstMon(r, kMonAskVaylerian);
            if (!m) return why = "not offered", false;
            if (lineIn(r, "draws 1 action card (Aylor Raider Hoard)", act->events, end) < 0) return why = "no card drawn", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Independence Tower (Free Systems): their Leadership -> a neighbour with their note reveals it for 1 command token
    {
        MonCase c;
        c.name = "Independence Tower: Leadership -> a note revealed for 1 token";
        c.faction = "freesystems";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            const int q = otherSeat(g, seat);
            g.systems[size_t(s)].space[size_t(q)].add(UnitType::Destroyer, 1);
            std::vector<int16_t>& mine = g.players[size_t(seat)].notes;
            int16_t note = -1;
            for (int16_t n : mine)
                if (noteOwner(n) == seat) note = n;
            if (note < 0) return false;
            mine.erase(std::find(mine.begin(), mine.end(), note));
            g.players[size_t(q)].notes.push_back(note);
            *at = s;
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.tag == Tag::StrategyPick && g.round == 2) {
                if (d.player == seat && offers(d, 0)) return a = one(0), true;
                if (d.player != seat && offers(d, 0))
                    for (const Option& o : d.options)
                        if (o.enabled && o.id != 0) return a = one(o.id), true;
            }
            if (d.player == seat && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 10)) return a = one(10), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskFsc);
            if (!m) return why = "no question (no Leadership)", false;
            if (m->player != otherSeat(r.before, r.seat)) return why = "asked of the wrong player", false;
            if (!logHas(r, "gains 1 command token (Independence Tower)", m->events)) return why = "no token gained", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Hyperlane Relay (Celdauri): their invasion's commit step -> ground forces from its planet to a dock planet there
    {
        MonCase c;
        c.name = "Hyperlane Relay: ground forces to a dock planet at the commit";
        c.faction = "celdauri";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (the Relay's planet, the dock planet)
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            int y = -1;
            for (size_t k = 0; k < g.planets.size() && y < 0; ++k) {
                const int s = g.planets[k].system;
                if (s == g.planets[size_t(x)].system || homeSys(g, s) || g.systems[size_t(s)].offBoard || s == g.mecatol() || isDMZ(g, int(k)) ||
                    isStation(g, int(k)) || g.systems[size_t(s)].planets.size() != 1)
                    continue;
                y = int(k);
            }
            if (y < 0) return false;
            monumentOn(g, seat, x);
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::Infantry, 2);  // (3 in all)
            g.systems[size_t(g.planets[size_t(x)].system)].tokens = 0;
            clearPlanet(g, y);
            g.planets[size_t(y)].owner = int8_t(seat);
            g.planets[size_t(y)].units[size_t(seat)].add(UnitType::SpaceDock, 1);
            clearSpace(g, g.planets[size_t(y)].system);
            *at = {x, y};
            return true;
        };
        auto sent = std::make_shared<bool>(false);
        c.drive = [at, activateOwn, sent](const Game& g, const Decision& d, int seat, Answer& a) {
            if (activateOwn(g, d, seat, g.planets[size_t(at->second)].system, a)) return true;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskCeldauri) {
                const int id = at->second * 16 + int(UnitType::Infantry);
                const bool first = !*sent && offers(d, id);
                *sent = true;
                return a = one(first ? id : -1), true;
            }
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskCeldauri);
            if (!m) return why = "no question (no commit step)", false;
            const std::string xn = kPlanets[r.before.planets[size_t(at->first)].def].name, yn = kPlanets[r.before.planets[size_t(at->second)].def].name;
            if (!logHas(r, "commits 1 Infantry from " + xn + " to " + yn + " (Hyperlane Relay)", m->events)) return why = "nothing committed", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Debris Field (Zelian): an asteroid field anyone's ships move into freely
    {
        MonCase c;
        c.name = "Debris Field: an asteroid field ships move into";
        c.faction = "zelian";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace, plainNext](Game& g, int seat) {
            const int x = monumentPlanet(g, seat, [&](int x) {
                const int s = g.planets[size_t(x)].system;
                return plainNext(g, s, seat) >= 0 && !g.systems[size_t(s)].d().anomalies;
            });
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            const int b = plainNext(g, s, seat);
            clearSpace(g, b);
            int q = -1;  // (someone without Antimass Deflectors - the Letnev start with it)
            for (int k = 0; k < g.numPlayers && q < 0; ++k)
                if (k != seat && !ownsTechKey(g, k, "amd")) q = k;
            if (q < 0) return false;
            g.systems[size_t(b)].space[size_t(q)].add(UnitType::Cruiser, 1);
            *at = s;
            return true;
        };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [at, plainNext](const MonRun& r, std::string& why) {
            const Game& g = r.before;
            if (!(tileAnomalies(g, *at) & kAsteroid)) return why = "not an asteroid field", false;
            if (!isAnomaly(g, *at)) return why = "not an anomaly", false;
            const int b = plainNext(g, *at, r.seat);
            int q = -1;
            for (int k = 0; k < g.numPlayers && q < 0; ++k)
                if (k != r.seat && g.systems[size_t(b)].space[size_t(k)].count(UnitType::Cruiser)) q = k;
            if (q < 0) return why = "no cruiser next to it", false;
            Game t = g;  // (the same system without the monument: an asteroid field it couldn't enter? only a plain one - fine either way)
            (void)t;
            if (!shipReach(g, q, b, *at, UnitType::Cruiser, 0).ok) return why = "another player's cruiser can't move in", false;
            return true;
        };
        cases.push_back(c);
    }

    // ================================================================ 0.82.4: Discordant Stars' monuments, batch 3
    // ---- Shades' Den (Kollecc): their activation of its system -> captured units from the sheet into it
    {
        MonCase c;
        c.name = "Shades' Den: captured units into its system after movement";
        c.faction = "kollecc";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            Player& pl = g.players[size_t(seat)];
            pl.captured.push_back(int16_t(seat * 16 + int(UnitType::Cruiser)));
            pl.captured.push_back(int16_t(seat * 16 + int(UnitType::Destroyer)));
            *at = s;
            return true;
        };
        c.drive = [at, activateOwn](const Game& g, const Decision& d, int seat, Answer& a) { return activateOwn(g, d, seat, *at, a); };
        c.check = [at, activationOf](const MonRun& r, std::string& why) {
            const Step* act = activationOf(r, r.seat, *at);
            if (!act) return why = "no question (not activated)", false;
            const Step* m = firstMon(r, kMonAskKollecc);
            if (!m) return why = "not offered", false;
            if (!logHas(r, "places 2 captured units into " + std::string(r.before.systems[size_t(*at)].d().name) + " (Shades' Den)", act->events))
                return why = "the units didn't come", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Tucc Academy (Bentor): another player explores a planet far from homes -> DEPLOY into coexistence there, and explore it
    {
        MonCase c;
        c.name = "Tucc Academy: DEPLOY on a planet another player explored";
        c.faction = "bentor";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            removeMonuments(g, seat);
            const int q = otherSeat(g, seat);
            int y = -1;
            for (size_t k = 0; k < g.planets.size() && y < 0; ++k) {
                const int s = g.planets[k].system;
                if (homeSys(g, s) || g.systems[size_t(s)].offBoard || s == g.mecatol() || g.systems[size_t(s)].planets.size() != 1 || isDMZ(g, int(k)) ||
                    isStation(g, int(k)) || g.systems[size_t(s)].d().anomalies || !monumentFitsPlanet(g, seat, int(k)))
                    continue;
                bool nearHome = false;
                for (int16_t a : g.systems[size_t(s)].adj) nearHome = nearHome || homeSys(g, a);
                if (!nearHome) y = int(k);
            }
            if (y < 0) return false;
            clearPlanet(g, y);
            g.planets[size_t(y)].owner = -1;
            const int t = g.planets[size_t(y)].system;
            clearSpace(g, t);
            g.systems[size_t(t)].space[size_t(q)].add(UnitType::Carrier, 1);
            g.systems[size_t(t)].space[size_t(q)].add(UnitType::Infantry, 2);
            *at = y;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat), t = g.planets[size_t(*at)].system;
            if (d.player == q && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 0) && !g.systems[size_t(t)].hasToken(q)) return a = one(0), true;
            if (d.player == q && d.tag == Tag::Activate && offers(d, t)) return a = one(t), true;
            if (d.player == q && d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == t) {
                a = Answer{};
                a.data = {*at, int(UnitType::Infantry), 2};
                return true;
            }
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskBentor) return a = one(1), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskBentor);
            if (!m) return why = "no question (nothing explored)", false;
            const std::string yn = kPlanets[r.before.planets[size_t(*at)].def].name;
            if (!logHas(r, "deploys Tucc Academy into coexistence on " + yn + " and explores it", m->events)) return why = "not deployed", false;
            if (!logHas(r, playerName(r.before, r.seat) + " explores " + yn, m->events)) return why = "the Bentor didn't explore it", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Krotas Bannerhall (Kjalengard): a combat won next to it -> a command token for them and the victor
    {
        MonCase c;
        c.name = "Krotas Bannerhall: a combat won nearby -> command tokens";
        c.faction = "kjalengard";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Destroyer, 1);
            Force& f = g.systems[size_t(s)].space[size_t(otherSeat(g, seat))];
            f.add(UnitType::Dreadnought, 3);
            *at = s;
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) {
            if (activateBy(g, d, otherSeat(g, seat), *at, a)) return true;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskKjalengard) return a = one(1), true;
            return false;
        };
        c.check = [at, activationOf, turnEndAfter, lineIn](const MonRun& r, std::string& why) {
            const Step* act = activationOf(r, otherSeat(r.before, r.seat), *at);
            if (!act) return why = "no question (not activated)", false;
            const size_t end = turnEndAfter(r, act->events);
            const int won = lineIn(r, " wins the space combat", act->events, end);
            if (won < 0) return why = "no combat won (inconclusive)", false;
            if (lineIn(r, "honours the victor", size_t(won), end) < 0) return why = "not offered after the win", false;
            const Step* w = firstMon(r, kMonAskKjalengardWin);
            if (!w || w->player != otherSeat(r.before, r.seat)) return why = "the victor wasn't asked", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Forbidden Library (Lanefir): a prerequisite of a colour near it covered (the Lanefir have no specialties of their own)
    {
        MonCase c;
        c.name = "Forbidden Library: a specialty near it covers a prerequisite";
        c.faction = "lanefir";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (the tech, its colour)
        c.setUp = [at](Game& g, int seat) {
            for (size_t x = 0; x < g.planets.size(); ++x) {
                const int s = g.planets[x].system;
                if (homeSys(g, s) || g.systems[size_t(s)].offBoard || !monumentFitsPlanet(g, seat, int(x)) || isDMZ(g, int(x))) continue;
                int mask = 0;
                for (int a : nearSystems7(g, s))
                    for (int16_t y : g.systems[size_t(a)].planets) {
                        const TechColor k = kPlanets[g.planets[size_t(y)].def].skip;
                        if (k != TechColor::None) mask |= 1 << int(k);
                    }
                if (!mask) continue;
                Game t = g;
                monumentOn(t, seat, int(x));
                t.players[size_t(seat)].fragments.clear();
                for (int k = 0; k < kTechCount; ++k) {
                    if (t.players[size_t(seat)].hasTech(k) || !techAvailableToResearch(t, seat, k) || kTechs[k].unitUpgrade) continue;
                    int miss[4];
                    missingPrereqs(t, seat, k, miss);
                    int total = 0, col = -1;
                    for (int cc = 0; cc < 4; ++cc) {
                        total += miss[cc];
                        if (miss[cc]) col = cc;
                    }
                    if (total != 1 || !((mask >> col) & 1)) continue;
                    g = t;
                    *at = {k, col};
                    return true;
                }
            }
            return false;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.tag == Tag::StrategyPick && g.round == 2) {
                if (d.player == seat && offers(d, 6)) return a = one(6), true;
                if (d.player != seat && offers(d, 6))
                    for (const Option& o : d.options)
                        if (o.enabled && o.id != 6) return a = one(o.id), true;
            }
            if (d.player == seat && d.tag == Tag::ActionChoice && g.round == 2 && offers(d, 16)) return a = one(16), true;
            if (d.player == seat && d.tag == Tag::Research && offers(d, at->first)) return a = one(at->first), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            if (!logHas(r, playerName(r.before, r.seat) + " researches " + kTechs[at->first].name)) return why = "not researched", false;
            if (!logHas(r, "technology specialty through Forbidden Library")) return why = "the Library wasn't used", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Polemarch's Hive (Cheiran): PRODUCTION = their structures in and around it
    {
        MonCase c;
        c.name = "Polemarch's Hive: PRODUCTION X";
        c.faction = "cheiran";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, lonePlanet](Game& g, int seat) {
            const int x = lonePlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::PDS, 2);
            *at = g.planets[size_t(x)].system;
            return true;
        };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [at](const MonRun& r, std::string& why) {
            const Game& g = r.before;
            int n = 0;
            for (int s : nearSystems7(g, *at)) {
                n += g.systems[size_t(s)].space[size_t(r.seat)].structures();
                for (int16_t y : g.systems[size_t(s)].planets) n += g.planets[size_t(y)].units[size_t(r.seat)].structures();
            }
            if (n < 3) return why = "set-up", false;
            if (hiveProduction(g, r.seat, *at) != n) return why = "PRODUCTION " + std::to_string(hiveProduction(g, r.seat, *at)) + ", not " + std::to_string(n), false;
            if (productionCapacity(g, r.seat, *at) < n) return why = "not counted in the production", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Kyro Reliquary (Kyro): legendary with a specialty; destroyed -> onto another planet of theirs nearby, a relic
    {
        MonCase c;
        c.name = "Kyro Reliquary: destroyed -> placed again and a relic";
        c.faction = "kyro";
        c.modules = kDS;
        auto at = std::make_shared<std::pair<int, int>>(-1, -1);  // (its planet, the other planet)
        c.setUp = [at, clearSpace, lonePlanet](Game& g, int seat) {
            const int x = lonePlanet(g, seat);
            if (x < 0) return false;
            const int s = g.planets[size_t(x)].system;
            int y = -1;
            for (int a : nearSystems7(g, s))
                for (int16_t k : g.systems[size_t(a)].planets)
                    if (k != x && y < 0 && !homeSys(g, a) && monumentFitsPlanet(g, seat, k) && !isDMZ(g, k)) y = k;
            if (y < 0) return false;
            monumentOn(g, seat, x);
            clearPlanet(g, y);
            g.planets[size_t(y)].owner = int8_t(seat);
            g.planets[size_t(y)].units[size_t(seat)].add(UnitType::Infantry, 2);
            clearSpace(g, s);
            Force& f = g.systems[size_t(s)].space[size_t(otherSeat(g, seat))];
            f.add(UnitType::Carrier, 2);
            f.add(UnitType::Infantry, 7);
            *at = {x, y};
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat), s = g.planets[size_t(at->first)].system;
            if (activateBy(g, d, q, s, a)) return true;
            if (d.player == q && d.kind == DecisionKind::Land && d.tag != Tag::BombardPlan && d.system == s) {
                a = Answer{};
                a.data = {at->first, int(UnitType::Infantry), g.systems[size_t(s)].space[size_t(q)].count(UnitType::Infantry)};
                return true;
            }
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            if (!isLegendaryPlanet(r.before, at->first) || !planetHasSkip(r.before, at->first)) return why = "not legendary with a specialty", false;
            const std::string yn = kPlanets[r.before.planets[size_t(at->second)].def].name;
            if (!logHas(r, "places Kyro Reliquary on " + yn + " and draws a relic")) return why = "not placed again (taken? inconclusive)", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Ghoti Anchorpoint (Ghoti): a frontier token there for them - Dark Energy Tap explores it
    {
        MonCase c;
        c.name = "Ghoti Anchorpoint: a frontier token for them";
        c.faction = "ghoti";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            g.systems[size_t(s)].frontier = false;
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Cruiser, 1);
            const int det = findTech("det");
            if (det < 0) return false;
            if (!g.players[size_t(seat)].hasTech(det)) g.players[size_t(seat)].techs.push_back(int16_t(det));
            *at = s;
            return true;
        };
        c.drive = [at, activateOwn](const Game& g, const Decision& d, int seat, Answer& a) { return activateOwn(g, d, seat, *at, a); };
        c.check = [at, activationOf](const MonRun& r, std::string& why) {
            if (!anchorpointFrontier(r.before, r.seat, *at) || anchorpointFrontier(r.before, otherSeat(r.before, r.seat), *at)) return why = "the frontier isn't theirs alone", false;
            const Step* act = activationOf(r, r.seat, *at);
            if (!act) return why = "no question (not activated)", false;
            if (!logHas(r, "explores the frontier token in " + std::string(r.before.systems[size_t(*at)].d().name), act->events)) return why = "not explored", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Wonell's Bastion (Kolume): another player passes -> its space cannon at their ships there
    {
        MonCase c;
        c.name = "Wonell's Bastion: a pass -> a shot at ships nearby";
        c.faction = "kolume";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, clearSpace](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            const int s = g.planets[size_t(x)].system;
            clearSpace(g, s);
            g.systems[size_t(s)].space[size_t(otherSeat(g, seat))].add(UnitType::Destroyer, 2);
            *at = s;
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskKolume) {
                const int id = *at * 16 + otherSeat(g, seat);
                if (offers(d, id)) return a = one(id), true;
            }
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            // (the rules pass: mandatory - a lone choice is taken without a question, and no "Don't" is ever offered)
            for (const Step& st : r.steps)
                if (st.tag == Tag::MonumentAbility && st.value == kMonAskKolume &&
                    std::find(st.offered.begin(), st.offered.end(), -1) != st.offered.end())
                    return why = "\"Don't\" offered (the card has no \"may\")", false;
            if (!logHas(r, " - Wonell's Bastion at " + playerName(r.before, otherSeat(r.before, r.seat)))) return why = "no shot", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Verdant Halo (Gledge): start of their turn -> 2 Core planets exhausted, laws blank for them this turn
    {
        MonCase c;
        c.name = "Verdant Halo: Core planets exhausted -> laws blank";
        c.faction = "gledge";
        c.modules = kDS;
        c.setUp = [](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            std::vector<int> mine = controlledPlanets(g, seat);
            int added = 0;
            g.dsCores.clear();
            for (int y : mine)
                if (added < 2 && y != x) {
                    g.dsCores.push_back(int16_t(y));
                    g.planets[size_t(y)].exhausted = false;
                    ++added;
                }
            if (added < 2) return false;
            lawOn(g, "regulations", -1);
            return true;
        };
        c.drive = [](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskGledge) return a = one(seat), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskGledge);
            if (!m) return why = "not offered", false;
            if (m->active != r.seat) return why = "not at their turn", false;
            if (!logHas(r, "treats all laws as blank until the end of this turn (Verdant Halo)", m->events)) return why = "no Halo line", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Vault of New Phrad (Rhodun): its system activated -> flipped to Reliquat Unleashed; +1 against unit upgrades
    {
        MonCase c;
        c.name = "Vault of New Phrad: activation -> flipped; +1 vs unit upgrades";
        c.faction = "zealots";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = g.planets[size_t(x)].system;
            g.systems[size_t(*at)].tokens = 0;
            return true;
        };
        c.drive = [at, activateBy](const Game& g, const Decision& d, int seat, Answer& a) {
            if (activateBy(g, d, otherSeat(g, seat), *at, a)) return true;
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskRhodun) return a = one(1), true;
            if (d.player == seat && d.tag == Tag::ActionChoice && offers(d, 648)) {  // (keep it flipped for the check)
                for (const Option& o : d.options)
                    if (o.enabled && o.id != 648) return a = one(o.id), true;
            }
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            Game g = r.before;
            const int q = otherSeat(g, r.seat);
            g.players[size_t(q)].techs.clear();
            if (rhodunBonus(g, r.seat, q, *at) != 0) return why = "+1 without unit upgrades", false;
            for (int k = 0; k < kTechCount; ++k)
                if (kTechs[k].unitUpgrade) {
                    g.players[size_t(q)].techs.push_back(int16_t(k));
                    break;
                }
            if (rhodunBonus(g, r.seat, q, *at) != 1) return why = "not +1 against unit upgrades", false;
            const Step* m = firstMon(r, kMonAskRhodun);
            if (!m) return why = "the flip wasn't offered", false;
            if (!logHas(r, "flips Vault of New Phrad: Reliquat Unleashed", m->events)) return why = "not flipped", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Reliquat Unleashed (Rhodun): players with units in the Fracture can't activate its system
    {
        MonCase c;
        c.name = "Reliquat Unleashed: Fracture players can't activate it";
        c.faction = "zealots";
        c.modules = kDS;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            Player& pl = g.players[size_t(seat)];
            pl.monFlipped = true;
            pl.unit[int(UnitType::Monument)] = int16_t(unitByKey("rhodun_monumentback"));
            *at = g.planets[size_t(x)].system;
            g.systems[size_t(*at)].tokens = 0;
            return true;
        };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.untilRound = 2;
        c.check = [at](const MonRun& r, std::string& why) {
            Game g = r.before;
            const int q = otherSeat(g, r.seat);
            if (!canActivate(g, q, *at)) return why = "blocked without the Fracture", false;
            int f = -1;
            for (size_t s = 0; s < g.systems.size() && f < 0; ++s)
                if (int(s) != *at && !homeSys(g, int(s)) && !g.systems[s].offBoard && !g.systems[s].isHyperlane()) f = int(s);
            g.systems[size_t(f)].fracture = true;
            g.systems[size_t(f)].space[size_t(q)].add(UnitType::Destroyer, 1);
            if (canActivate(g, q, *at)) return why = "a player in the Fracture can activate it", false;
            if (rhodunBonus(g, r.seat, q, *at) != 2) return why = "not +2 against a player in the Fracture", false;
            if (!canActivate(g, r.seat, *at) && !g.systems[size_t(*at)].hasToken(r.seat)) return why = "the Rhodun can't activate it", false;
            return true;
        };
        cases.push_back(c);
    }

    // ================================================================ Blue Reverie (0.83.0): the Atokera Legacy
    const int kBR = kModCodex | kModBlueReverie;
    auto atokPlanet = [](const Game& g, int seat) {  // a planet with nothing on it outside the homes and Mecatol Rex
        for (size_t x = 0; x < g.planets.size(); ++x) {
            const PlanetState& ps = g.planets[x];
            if (ps.system < 0 || homeSys(g, ps.system) || ps.system == g.mecatol() || ps.destroyed || isStation(g, int(x))) continue;
            bool empty = g.systems[size_t(ps.system)].planets.size() == 1;
            for (int q = 0; q < kSides; ++q) empty = empty && ps.units[size_t(q)].empty() && g.systems[size_t(ps.system)].space[size_t(q)].empty();
            if (empty) return int(x);
        }
        (void)seat;
        return -1;
    };
    auto emptySpace = [](const Game& g) {  // a system without planets nobody is in
        for (size_t s = 0; s < g.systems.size(); ++s) {
            const SystemState& ss = g.systems[s];
            if (!ss.planets.empty() || ss.offBoard || ss.isHyperlane() || homeSys(g, int(s)) || ss.d().anomalies) continue;
            bool empty = true;
            for (int q = 0; q < kSides; ++q) empty = empty && ss.space[size_t(q)].empty();
            if (empty) return int(s);
        }
        return -1;
    };
    // ---- Synthesis, Mirrorshard I, the Obelisk, Kohi (the rules helpers, on the board of round 2)
    {
        MonCase c;
        c.name = "Atokera: Synthesis / Mirrorshard I / Obelisk / Kohi";
        c.faction = "atokera";
        c.second = "sol";
        c.modules = kBR;
        c.untilRound = 2;
        c.setUp = [](Game&, int) { return true; };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [atokPlanet, emptySpace](const MonRun& r, std::string& why) {
            Game g = r.before;
            const int seat = r.seat, sol = 1;
            const int home = g.players[size_t(seat)].homeSystem;
            int magna = -1;
            for (int16_t x : g.systems[size_t(home)].planets) magna = x;
            g.planets[size_t(magna)].exhausted = false;
            const int ready = productionCapacity(g, seat, home);
            g.planets[size_t(magna)].exhausted = true;
            const int spent = productionCapacity(g, seat, home);
            if (ready - spent != 1) return why = "a readied home planet isn't PRODUCTION 1 (" + std::to_string(ready) + " / " + std::to_string(spent) + ")", false;
            if (spent != planetResources(g, magna) + 3) return why = "the space dock isn't resources + 3 (" + std::to_string(spent) + ")", false;
            const int x = atokPlanet(g, seat);
            if (x < 0) return why = "(no free planet)", false;
            const int s = g.planets[size_t(x)].system;
            g.planets[size_t(x)].owner = int8_t(seat);
            g.planets[size_t(x)].exhausted = false;
            if (productionCapacity(g, seat, s) != 1) return why = "a readied planet doesn't produce 1", false;
            const std::vector<int> dest = groundForceDestinations(g, seat, s);
            if (std::find(dest.begin(), dest.end(), x) == dest.end()) return why = "no ground forces onto a Synthesis planet", false;
            if (!brPlanetShield(g, seat, x)) return why = "no Planetary Shield on a readied planet", false;
            g.planets[size_t(x)].exhausted = true;
            if (productionCapacity(g, seat, s) != 0 || brPlanetShield(g, seat, x)) return why = "an exhausted planet still produces / shields", false;
            if (productionCapacity(g, sol, home) != 0) return why = "Synthesis for another player", false;
            // the Obelisk
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::Mech, 1);
            if (!obeliskGuards(g, x, sol) || obeliskGuards(g, x, seat)) return why = "the Obelisk doesn't guard the planet", false;
            g.planets[size_t(x)].units[size_t(seat)].n[int(UnitType::Mech)] = 0;
            if (obeliskGuards(g, x, sol)) return why = "guarded without the Obelisk", false;
            // Mirrorshard I: PRODUCTION 0 (+1)
            const int e = emptySpace(g);
            if (e < 0) return why = "(no empty system)", false;
            g.systems[size_t(e)].space[size_t(seat)].add(UnitType::Cruiser, 1);
            if (productionCapacity(g, seat, e) != 1) return why = "Mirrorshard I doesn't produce 1", false;
            const std::vector<int> d2 = groundForceDestinations(g, seat, e);
            if (std::find(d2.begin(), d2.end(), -1) == d2.end()) return why = "Mirrorshard I can't place ground forces in space", false;
            g.systems[size_t(e)].space[size_t(seat)].n[int(UnitType::Cruiser)] = 0;
            // Kohi: Sol's Advanced Carrier II has Sustain Damage - the Atokera carrier with Kohi copies it
            g.players[size_t(sol)].unit[int(UnitType::Carrier)] = int16_t(unitByKey("sol_carrier2"));
            g.systems[size_t(e)].space[size_t(seat)].add(UnitType::Carrier, 1);
            g.systems[size_t(e)].space[size_t(sol)].add(UnitType::Carrier, 1);
            if (unitSustainsAt(g, seat, UnitType::Carrier, e, -1)) return why = "the carrier sustains without Kohi", false;
            g.systems[size_t(e)].space[size_t(seat)].add(UnitType::Flagship, 1);
            if (!unitSustainsAt(g, seat, UnitType::Carrier, e, -1)) return why = "Kohi doesn't copy Sustain Damage", false;
            if (unitSustainsAt(g, sol, UnitType::Infantry, e, -1)) return why = "Kohi helped another player", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Living Metals: Plague (an ACTION card) can't destroy their infantry
    {
        MonCase c;
        c.name = "Atokera: Living Metals stops Plague";
        c.faction = "atokera";
        c.modules = kBR;
        c.untilRound = 3;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int home = g.players[size_t(seat)].homeSystem;
            for (int16_t x : g.systems[size_t(home)].planets) *at = x;
            g.planets[size_t(*at)].units[size_t(seat)].n[int(UnitType::Infantry)] = 12;
            giveCard(g, otherSeat(g, seat), "plague");
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            const int q = otherSeat(g, seat), card = findActionCard("plague");
            if (d.player != q) return false;
            if (d.tag == Tag::ActionChoice && offers(d, 1000 + card)) return a = one(1000 + card), true;
            if (d.tag == Tag::CardTarget && offers(d, *at)) return a = one(*at), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            if (!logHas(r, "Plague on")) return why = "no question (Plague wasn't played)", false;
            if (!logHas(r, "(Living Metals)")) return why = "no Living Metals line", false;
            if (logHas(r, "loses") && r.after.planets[size_t(*at)].units[size_t(r.seat)].count(UnitType::Infantry) < 12 &&
                !logHas(r, "ground combat"))
                return why = "infantry lost", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Ordagka predicts, they don't vote: a command token; Mirrorshard II: a cruiser after the outcome
    {
        MonCase c;
        c.name = "Atokera: Ordagka token, Mirrorshard II deploy";
        c.faction = "atokera";
        c.modules = kBR;
        c.agenda = true;
        c.untilRound = 2;
        c.setUp = [](Game& g, int seat) {
            Player& pl = g.players[size_t(seat)];
            for (LeaderState& ls : pl.leaders)
                if (kLeaders[ls.leader].type == kCommander) ls.status = kReady;
            pl.unit[int(UnitType::Cruiser)] = int16_t(unitByKey("atokera_cruiser2"));
            pl.techs.push_back(int16_t(findTech("dsatokcr")));
            return true;
        };
        c.drive = [](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.tag == Tag::Faction && d.ability == int(Ab::BrOrdagka) && d.player == seat) {
                for (const Option& o : d.options)
                    if (o.enabled && o.id != kNoVote) return a = one(o.id), true;
            }
            if (d.tag == Tag::VoteOutcome) {
                if (d.player == seat && offers(d, -100)) return a = one(-100), true;  // (the Atokera abstain)
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets && d.player != seat) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            if (d.tag == Tag::Faction && d.ability == int(Ab::BrMirrorshard) && d.player == seat) {
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0) return a = one(o.id), true;
            }
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const Step* m = nullptr;
            for (const Step& s : r.steps)
                if (s.tag == Tag::Faction && s.ability == int(Ab::BrMirrorshard) && !m) m = &s;
            if (!steps(r, [](const Step& s) { return s.tag == Tag::Faction && s.ability == int(Ab::BrOrdagka); })) return why = "no question (Ordagka)", false;
            if (!logHas(r, "prediction was right: 1 command token")) return why = "no command token for a right prediction", false;
            if (!m) return why = "no question (Mirrorshard II)", false;
            if (m->phase != Phase::Agenda) return why = "Mirrorshard II not in the agenda phase", false;
            bool result = false;
            for (size_t i = 0; i < m->events && i < r.log.size(); ++i) result = result || r.log[i].kind == EventKind::AgendaResult;
            if (!result) return why = "Mirrorshard II before the outcome was resolved", false;
            if (!logHas(r, "places 1 cruiser in")) return why = "no cruiser placed", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Magruda: a planet they voted with counts resources + influence and is readied
    {
        MonCase c;
        c.name = "Atokera: Magruda's votes";
        c.faction = "atokera";
        c.modules = kBR;
        c.agenda = true;
        c.untilRound = 2;
        c.setUp = [](Game&, int) { return true; };
        c.drive = [](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.tag == Tag::VoteOutcome) {
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            if (d.tag == Tag::Faction && d.ability == int(Ab::BrMagruda) && d.player == seat) {
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0) return a = one(o.id), true;
            }
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!steps(r, [](const Step& s) { return s.tag == Tag::Faction && s.ability == int(Ab::BrMagruda); })) return why = "no question", false;
            if (!logHas(r, "counts its resources and influence and is readied (Magruda)")) return why = "not used", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Modular Designs: chosen after movement, before the space cannon offense
    {
        MonCase c;
        c.name = "Atokera: Modular Designs after movement";
        c.faction = "atokera";
        c.modules = kBR;
        c.untilRound = 3;
        c.setUp = [](Game& g, int seat) {
            g.players[size_t(seat)].techs.push_back(int16_t(findTech("dsatoky")));
            return true;
        };
        c.drive = [](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.tag == Tag::Faction && d.ability == int(Ab::BrModularDesigns) && d.player == seat) {
                for (const Option& o : d.options)
                    if (o.enabled && o.id / 10000 == 5) return a = one(o.id), true;  // PRODUCTION 3
            }
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const Step* m = nullptr;
            for (const Step& s : r.steps)
                if (s.tag == Tag::Faction && s.ability == int(Ab::BrModularDesigns) && !m) m = &s;
            if (!m) return why = "no question", false;
            if (m->phase != Phase::Action || m->active != r.seat) return why = "not in their action", false;
            const size_t i = stepIndex(r, m);
            bool moved = false;
            for (size_t k = i; k-- > 0;) {
                if (r.steps[k].kind == DecisionKind::Move && r.steps[k].player == r.seat) {
                    moved = true;
                    break;
                }
                if (r.steps[k].tag == Tag::ActionChoice) break;
            }
            if (!moved) return why = "not after the movement", false;
            if (!logHas(r, "(Modular Designs)", m->events)) return why = "no line", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Deepmantle Interface (Monuments+): after an outcome they voted for is resolved - produce up to 1 unit in its system
    {
        MonCase c;
        c.name = "Atokera: Deepmantle Interface produces after the outcome";
        c.faction = "atokera";
        c.modules = kBR | kModMonuments;
        c.agenda = true;
        c.untilRound = 2;
        c.setUp = [](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            g.players[size_t(seat)].tradeGoods = 6;
            return true;
        };
        c.drive = [](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.tag == Tag::VoteOutcome) {
                for (const Option& o : d.options)
                    if (o.id != -100 && o.enabled) return a = one(o.id), true;
            }
            if (d.tag == Tag::VotePlanets) {
                a = Answer{};
                a.picks = d.planets;
                return true;
            }
            if (d.player == seat && d.tag == Tag::MonumentAbility && d.value == kMonAskAtokera) return a = one(1), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            const Step* m = firstMon(r, kMonAskAtokera);
            if (!m) return why = "no question", false;
            if (m->phase != Phase::Agenda) return why = "not in the agenda phase", false;
            bool result = false;
            for (size_t i = 0; i < m->events && i < r.log.size(); ++i) result = result || r.log[i].kind == EventKind::AgendaResult;
            if (!result) return why = "before the outcome was resolved", false;
            if (!logHas(r, "uses Deepmantle Interface to produce", m->events)) return why = "not used", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Panoply Animus (breakthrough, Thunder's Edge): their structures have Sustain Damage and fight on the ground
    {
        MonCase c;
        c.name = "Atokera: Panoply Animus";
        c.faction = "atokera";
        c.modules = kBR | kModThundersEdge;
        c.untilRound = 2;
        c.setUp = [](Game&, int) { return true; };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [](const MonRun& r, std::string& why) {
            Game g = r.before;
            const int home = g.players[size_t(r.seat)].homeSystem;
            int magna = -1;
            for (int16_t x : g.systems[size_t(home)].planets) magna = x;
            g.players[size_t(r.seat)].breakthrough = false;
            if (unitSustainsAt(g, r.seat, UnitType::SpaceDock, home, magna)) return why = "a dock sustains without it", false;
            g.players[size_t(r.seat)].breakthrough = true;
            if (!unitSustainsAt(g, r.seat, UnitType::SpaceDock, home, magna)) return why = "no Sustain Damage for structures", false;
            return true;
        };
        cases.push_back(c);
    }

    // ================================================================ Blue Reverie (0.84.0): the Belkosea Allied States
    // ---- the superweapons' static abilities and Caled's line (rules helpers, on the board of round 2)
    {
        MonCase c;
        c.name = "Belkosea: superweapons (Availyn / Glatison / Mors / Caled / Arrogance)";
        c.faction = "belkosea";
        c.second = "sol";
        c.modules = kBR;
        c.untilRound = 2;
        c.setUp = [](Game&, int) { return true; };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [atokPlanet](const MonRun& r, std::string& why) {
            Game g = r.before;
            const int seat = r.seat, sol = 1 == seat ? 0 : 1;
            const int x = atokPlanet(g, seat);
            if (x < 0) return why = "(no free planet)", false;
            const int s = g.planets[size_t(x)].system;
            g.planets[size_t(x)].owner = int8_t(seat);
            const int structs = structuresOf(g, seat);
            // Availyn: a nebula, PRODUCTION 3 on its planet
            if (availynNebula(g, s) || (anomaliesFor(g, seat, s) & kNebula)) return why = "a nebula before Availyn", false;
            const int cap0 = productionCapacity(g, seat, s);
            g.planets[size_t(x)].superweapon = kSwAvailyn;
            g.players[size_t(seat)].swCards = 1 << kSwAvailyn;
            if (!(anomaliesFor(g, seat, s) & kNebula) || !(anomaliesFor(g, sol, s) & kNebula) || !isAnomaly(g, s)) return why = "Availyn's system isn't a nebula", false;
            if (productionCapacity(g, seat, s) != cap0 + 3) return why = "Availyn isn't PRODUCTION 3", false;
            const std::vector<int> dest = groundForceDestinations(g, seat, s);
            if (std::find(dest.begin(), dest.end(), x) == dest.end()) return why = "no ground forces onto Availyn's planet", false;
            if (productionCapacity(g, sol, s) != 0) return why = "Availyn produces for another player", false;
            // Arrogance: superweapons are structures
            if (structuresOf(g, seat) != structs + 1) return why = "a superweapon isn't a structure", false;
            // Glatison: other players' units there lose Sustain Damage
            g.planets[size_t(x)].superweapon = kSwGlatison;
            g.systems[size_t(s)].space[size_t(sol)].add(UnitType::Dreadnought, 1);
            g.systems[size_t(s)].space[size_t(seat)].add(UnitType::Dreadnought, 1);
            if (unitSustainsAt(g, sol, UnitType::Dreadnought, s, -1)) return why = "Glatison: another player's dreadnought still sustains", false;
            if (!unitSustainsAt(g, seat, UnitType::Dreadnought, s, -1)) return why = "Glatison took its owner's Sustain Damage", false;
            // Mors: Planetary Shield on its planet (for its owner)
            g.planets[size_t(x)].superweapon = kSwMors;
            if (!brPlanetShield(g, seat, x) || brPlanetShield(g, sol, x)) return why = "Mors isn't a Planetary Shield for its owner", false;
            g.planets[size_t(x)].superweapon = -1;
            if (brPlanetShield(g, seat, x)) return why = "a shield without Mors", false;
            // Caled: a straight line of non-anomalies; not through an anomaly; not its own system
            int from = -1, to = -1, bad = -1;
            for (size_t a = 0; a < g.systems.size() && to < 0; ++a)
                for (size_t b = 0; b < g.systems.size() && to < 0; ++b)
                    if (a != b && hexDistance(g.systems[a].pos, g.systems[b].pos) == 2 && caledLine(g, int(a), int(b))) from = int(a), to = int(b);
            if (to < 0) return why = "(no straight line of 3 plain systems)", false;
            if (caledLine(g, from, from)) return why = "Caled's own system", false;
            for (size_t b = 0; b < g.systems.size() && bad < 0; ++b)
                if (int(b) != from && !g.systems[b].offBoard && hexDistance(g.systems[size_t(from)].pos, g.systems[b].pos) == 2 && !caledLine(g, from, int(b)))
                    bad = int(b);
            int mid = -1;
            for (int16_t m : g.systems[size_t(from)].adj)
                if (hexDistance(g.systems[size_t(m)].pos, g.systems[size_t(to)].pos) == 1 && hexDistance(g.systems[size_t(m)].pos, g.systems[size_t(from)].pos) == 1)
                    mid = m;
            if (mid >= 0) {  // an Availyn in the middle system makes it a nebula: the line is blocked
                for (int16_t px : g.systems[size_t(mid)].planets) {
                    g.planets[size_t(px)].superweapon = kSwAvailyn;
                    g.planets[size_t(px)].owner = int8_t(seat);
                    break;
                }
                if (!g.systems[size_t(mid)].planets.empty() && caledLine(g, from, to)) return why = "Caled's line crosses a nebula", false;
            }
            return true;
        };
        cases.push_back(c);
    }
    // ---- Classified Developments (component action): a superweapon and its card; the commander unlocks
    {
        MonCase c;
        c.name = "Belkosea: Classified Developments builds Mors, Ciphus Gault unlocks";
        c.faction = "belkosea";
        c.modules = kBR;
        c.untilRound = 3;
        c.setUp = [](Game& g, int seat) {
            g.players[size_t(seat)].tradeGoods = 8;
            return true;
        };
        c.drive = [](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && offers(d, 6000 + int(Ab::BrClassified))) return a = one(6000 + int(Ab::BrClassified)), true;
            if (d.tag == Tag::Faction && d.ability == int(Ab::BrClassified) && offers(d, kSwMors)) return a = one(kSwMors), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "(Classified Developments)")) return why = "no question (no superweapon built)", false;
            const Game& g = r.after;
            int x = -1;
            for (size_t i = 0; i < g.planets.size(); ++i)
                if (g.planets[i].superweapon == kSwMors) x = int(i);
            if (x < 0) return why = "Mors isn't on the board", false;
            if (g.planets[size_t(x)].owner != r.seat && !logHas(r, "(Arrogance)")) return why = "Mors stands on another player's planet", false;
            if (!((g.players[size_t(r.seat)].swCards | g.players[size_t(r.seat)].swPurged) & (1 << kSwMors))) return why = "no Mors card", false;
            if (!logHas(r, "unlocks their commander")) return why = "Ciphus Gault didn't unlock", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Mors on their turn: every unit in a system far away becomes damaged
    {
        MonCase c;
        c.name = "Belkosea: Mors damages a fleet far away";
        c.faction = "belkosea";
        c.modules = kBR;
        c.untilRound = 3;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at, emptySpace](Game& g, int seat) {
            const int home = g.players[size_t(seat)].homeSystem;
            int x = -1;
            for (int16_t px : g.systems[size_t(home)].planets)
                if (!isStation(g, px)) x = px;
            if (x < 0) return false;
            g.planets[size_t(x)].superweapon = kSwMors;
            g.players[size_t(seat)].swCards = 1 << kSwMors;
            for (size_t s = 0; s < g.systems.size(); ++s) {
                if (int(s) == home || g.systems[s].offBoard || hexDistance(g.systems[s].pos, g.systems[size_t(home)].pos) < 3) continue;
                if (!g.systems[s].planets.empty() || homeSys(g, int(s)) || g.systems[s].isHyperlane()) continue;
                *at = int(s);
                break;
            }
            if (*at < 0) return false;
            g.systems[size_t(*at)].space[size_t(otherSeat(g, seat))].add(UnitType::Dreadnought, 3);
            (void)emptySpace;
            return true;
        };
        c.drive = [at](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && offers(d, 660 + kSwMors)) return a = one(660 + kSwMors), true;
            if (d.tag == Tag::Faction && d.ability == int(Ab::BrMors) && offers(d, *at)) return a = one(*at), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "(Mors)")) return why = "no question (Mors wasn't used)", false;
            if (!logHas(r, "3 units are damaged (Mors)") && !logHas(r, "units are damaged (Mors)")) return why = "the dreadnoughts weren't damaged", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Arrogance: a superweapon on a planet another player takes is purged with its card
    {
        MonCase c;
        c.name = "Belkosea: Arrogance purges a captured superweapon";
        c.faction = "belkosea";
        c.second = "sardakk";
        c.modules = kBR;
        c.untilRound = 4;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int q = otherSeat(g, seat);
            const int qh = g.players[size_t(q)].homeSystem;
            for (int16_t a : g.systems[size_t(qh)].adj) {  // a planet next to the other player's home
                for (int16_t px : g.systems[size_t(a)].planets)
                    if (!isStation(g, px) && !homeSys(g, a)) *at = px;
                if (*at >= 0) break;
            }
            if (*at < 0) return false;
            PlanetState& ps = g.planets[size_t(*at)];
            for (int k = 0; k < kSides; ++k) ps.units[size_t(k)] = Force();
            ps.owner = int8_t(seat);
            ps.superweapon = kSwGrom;
            g.players[size_t(seat)].swCards = 1 << kSwGrom;
            g.systems[size_t(qh)].space[size_t(q)].add(UnitType::Carrier, 1);
            g.systems[size_t(qh)].space[size_t(q)].add(UnitType::Infantry, 4);
            g.systems[size_t(qh)].space[size_t(q)].add(UnitType::Dreadnought, 2);
            return true;
        };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [at](const MonRun& r, std::string& why) {
            if (r.after.planets[size_t(*at)].owner == r.seat) return why = "no question (the planet wasn't taken)", false;
            if (!logHas(r, "(Arrogance)")) return why = "no Arrogance line", false;
            const Player& pl = r.after.players[size_t(r.seat)];
            if (r.after.planets[size_t(*at)].superweapon >= 0 || (pl.swCards & (1 << kSwGrom)) || !(pl.swPurged & (1 << kSwGrom)))
                return why = "Grom wasn't purged with its card", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Trinity Stockpile (breakthrough): a strategy token purged, every unit on a planet next to their home destroyed
    {
        MonCase c;
        c.name = "Belkosea: Trinity Stockpile";
        c.faction = "belkosea";
        c.modules = kBR | kModThundersEdge;
        c.untilRound = 3;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int home = g.players[size_t(seat)].homeSystem;
            for (int16_t a : g.systems[size_t(home)].adj)
                for (int16_t px : g.systems[size_t(a)].planets)
                    if (*at < 0 && !homeSys(g, a) && !isStation(g, px)) *at = px;
            if (*at < 0) return false;
            PlanetState& ps = g.planets[size_t(*at)];
            const int q = otherSeat(g, seat);
            for (int k = 0; k < kSides; ++k) ps.units[size_t(k)] = Force();
            ps.owner = int8_t(q);
            ps.units[size_t(q)].add(UnitType::Infantry, 4);
            ps.units[size_t(q)].add(UnitType::SpaceDock, 1);
            g.players[size_t(seat)].breakthrough = true;
            g.players[size_t(seat)].btExhausted = false;
            g.players[size_t(seat)].strategy = 3;
            return true;
        };
        c.drive = [at](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && offers(d, 667)) return a = one(667), true;
            if (d.tag == Tag::Faction && d.ability == int(Ab::BrTrinity) && offers(d, *at)) return a = one(*at), true;
            return false;
        };
        c.check = [at](const MonRun& r, std::string& why) {
            if (!logHas(r, "(Trinity Stockpile)")) return why = "no question (not used)", false;
            if (r.after.players[size_t(r.seat)].brTokensPurged != 1) return why = "no command token purged", false;
            // (0.85.0: the planet's owner may have exhausted it already - then there is no line; the code exhausts it either way)
            if (!logHas(r, "strikes")) return why = "no planet struck", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Armageddon Project (Monuments+): a control token at the end of the strategy phase; the monument has Mors's shield
    {
        MonCase c;
        c.name = "Belkosea: Armageddon Project takes Mors's abilities";
        c.faction = "belkosea";
        c.modules = kBR | kModMonuments;
        c.untilRound = 3;  // (the token comes at the end of round 2's strategy phase)
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int x = monumentPlanet(g, seat);
            if (x < 0) return false;
            monumentOn(g, seat, x);
            *at = x;
            const int home = g.players[size_t(seat)].homeSystem;
            for (int16_t px : g.systems[size_t(home)].planets)
                if (!isStation(g, px)) {
                    g.planets[size_t(px)].superweapon = kSwMors;
                    break;
                }
            g.players[size_t(seat)].swCards = 1 << kSwMors;
            return true;
        };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [at](const MonRun& r, std::string& why) {
            if (!logHas(r, "puts a control token on Mors")) return why = "no question (no control token)", false;
            const Game& g = r.after;
            if (!(g.players[size_t(r.seat)].swArm & (1 << kSwMors))) return why = "no token on Mors", false;
            if (!brPlanetShield(g, r.seat, *at) && g.planets[size_t(*at)].owner == r.seat) return why = "the monument's planet has no Planetary Shield", false;
            return true;
        };
        cases.push_back(c);
    }

    // ================================================================ Blue Reverie (0.85.0): the Pharad'n Order
    // ---- the rules helpers: Mark of Pharad'n (captured infantry are what they can produce / place, at no resource cost), Sedtra's move,
    // Reality-Field Impactor, the Black Pyramid as a ground force
    {
        MonCase c;
        c.name = "Pharad'n: Mark of Pharad'n, Sedtra, Impactor, Black Pyramid";
        c.faction = "pharadn";
        c.second = "sol";
        c.modules = kBR | kModMonuments;
        c.untilRound = 2;
        c.setUp = [](Game&, int) { return true; };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [](const MonRun& r, std::string& why) {
            Game g = r.before;
            const int seat = r.seat, sol = seat == 0 ? 1 : 0;
            Player& pl = g.players[size_t(seat)];
            pl.captured.clear();
            if (unitsAvailable(g, seat, UnitType::Infantry) != 0) return why = "infantry to produce without captured ones", false;
            for (int i = 0; i < 3; ++i) pl.captured.push_back(int16_t(15 * 16 + int(UnitType::Infantry)));
            if (unitsAvailable(g, seat, UnitType::Infantry) != 3 || pharCaptured(g, seat) != 3) return why = "3 captured infantry aren't 3 to produce", false;
            int counts[kUnitTypes] = {};
            counts[int(UnitType::Infantry)] = 3;
            if (productionCost(g, seat, counts) != 0) return why = "infantry cost resources", false;
            if (productionCost(g, sol, counts) != 2) return why = "another player's infantry don't cost 2", false;
            const int home = pl.homeSystem;
            std::vector<int> bad = {home, -1, int(UnitType::Infantry), 4};  // (only checking the counts: 4 > 3 captured)
            (void)bad;
            // Sedtra: move 0, +1 for each ground force it carries
            Game h = g;
            SystemState& hs = h.systems[size_t(home)];
            for (int16_t x : hs.planets) h.planets[size_t(x)].units[size_t(seat)] = Force();
            hs.space[size_t(seat)] = Force();
            hs.space[size_t(seat)].add(UnitType::Flagship, 1);
            if (moveValue(h, seat, home, UnitType::Flagship) != 0) return why = "Sedtra moves without ground forces", false;
            hs.space[size_t(seat)].add(UnitType::Infantry, 2);
            if (moveValue(h, seat, home, UnitType::Flagship) != 2) return why = "Sedtra with 2 infantry doesn't move 2", false;
            hs.space[size_t(seat)].add(UnitType::Infantry, 3);
            if (moveValue(h, seat, home, UnitType::Flagship) != 3) return why = "Sedtra moves further than its capacity", false;
            // Reality-Field Impactor: the anomaly has no effect (it is still an anomaly)
            int an = -1;
            for (size_t s = 0; s < g.systems.size() && an < 0; ++s)
                if (!g.systems[s].offBoard && g.systems[s].d().anomalies) an = int(s);
            if (an >= 0) {
                h.fx.rfiSys = int16_t(an);
                if (anomaliesFor(h, sol, an) != 0 || tileAnomaliesMove(h, an) != 0) return why = "the Impactor's anomaly still has its effects", false;
                if (!isAnomaly(h, an)) return why = "the Impactor's anomaly is no anomaly any more", false;
            }
            // Black Pyramid: a structure and a ground force
            if (playerMonument(g, seat) < 0) return why = "no Black Pyramid", false;
            const int x = monumentPlanet(g, seat);
            if (x < 0) return why = "(no hazardous / legendary planet)", false;
            monumentOn(g, seat, x);
            g.planets[size_t(x)].units[size_t(seat)].remove(UnitType::Infantry, 1, false);
            if (!pyramidOn(g, seat, x) || groundForcesOn(g, seat, x) != 1 || !sleepingWorldOn(g, seat, x)) return why = "the Black Pyramid isn't a ground force", false;
            if (g.planets[size_t(x)].units[size_t(seat)].structures() != 1) return why = "the Black Pyramid isn't a structure", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Nokash on themselves (their turn, no action): the home infantry destroyed and captured (Immortal), commodities -> trade goods;
    // the Sacrotitan rises there
    {
        MonCase c;
        c.name = "Pharad'n: Nokash, Immortal captures, Sacrotitan rises";
        c.faction = "pharadn";
        c.modules = kBR;
        c.untilRound = 3;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int home = g.players[size_t(seat)].homeSystem;
            for (int16_t x : g.systems[size_t(home)].planets) *at = x;
            if (*at < 0) return false;
            g.planets[size_t(*at)].units[size_t(seat)].remove(UnitType::Infantry, 99, false);
            g.planets[size_t(*at)].units[size_t(seat)].add(UnitType::Infantry, 3);
            g.players[size_t(seat)].commodities = 1;
            g.players[size_t(seat)].captured.clear();
            return true;
        };
        c.drive = [at](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && offers(d, 670) && g.planets[size_t(*at)].units[size_t(seat)].count(UnitType::Infantry) >= 3) return a = one(670), true;
            if (d.tag == Tag::Faction && d.ability == int(Ab::PharNokash) && offers(d, seat)) return a = one(seat), true;
            if (d.tag == Tag::Faction && d.ability == int(Ab::PharNokashPlanet) && offers(d, *at)) return a = one(*at), true;
            if (d.tag == Tag::Faction && d.ability == int(Ab::PharSacrotitan)) return a = one(1), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "(Nokash)")) return why = "no question (Nokash wasn't used)", false;
            if (!logHas(r, "captures their own 3 Infantry (Immortal)")) return why = "the infantry weren't captured", false;
            if (!logHas(r, "destroys 3 infantry") || !logHas(r, "gains 1 commodities and converts 2 into trade goods")) return why = "not 1 commodity + 2 trade goods", false;
            if (!logHas(r, "(Sacrotitan)")) return why = "no Sacrotitan", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Immortal II: at the start of their turn, 1 infantry produced from a captured one
    {
        MonCase c;
        c.name = "Pharad'n: Immortal II returns a captured infantry";
        c.faction = "pharadn";
        c.modules = kBR;
        c.untilRound = 3;
        c.setUp = [](Game& g, int seat) {
            g.players[size_t(seat)].techs.push_back(int16_t(findTech("dspharinf")));
            g.players[size_t(seat)].captured.clear();
            g.players[size_t(seat)].captured.push_back(int16_t(15 * 16 + int(UnitType::Infantry)));
            return true;
        };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "(Immortal II)")) return why = "no question (Immortal II)", false;
            for (const Step& st : r.steps)
                if (st.player == r.seat && st.ability == int(Ab::PharImmortal) && !st.answer.empty() && st.answer[0] >= 0) return true;
            return why = "no planet picked", false;
        };
        cases.push_back(c);
    }
    // ---- Avhkan's unlocking ACTION: 2 infantry destroyed on each of 5 planets
    {
        MonCase c;
        c.name = "Pharad'n: Avhkan unlocks by its ACTION";
        c.faction = "pharadn";
        c.modules = kBR;
        c.untilRound = 3;
        c.setUp = [](Game& g, int seat) {
            int n = 0;
            for (size_t x = 0; x < g.planets.size() && n < 5; ++x) {
                const PlanetState& ps = g.planets[x];
                if (ps.system < 0 || g.systems[size_t(ps.system)].offBoard || isStation(g, int(x)) || (homeSys(g, ps.system) && ps.owner != seat) ||
                    ps.system == g.mecatol())
                    continue;
                clearPlanet(g, int(x));
                g.planets[x].owner = int8_t(seat);
                g.planets[x].units[size_t(seat)].add(UnitType::Infantry, 2);
                ++n;
            }
            return n == 5;
        };
        c.drive = [](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && offers(d, 6000 + int(Ab::PharAvhkanUnlock))) return a = one(6000 + int(Ab::PharAvhkanUnlock)), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "to unlock Avhkan")) return why = "no question (the action wasn't taken)", false;
            if (!logHas(r, "unlocks their commander")) return why = "Avhkan didn't unlock", false;
            if (!r.after.players[size_t(r.seat)].pharUnlock) return why = "no unlock flag", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Death Binding: its holder removes 4 infantry when the Pharad'n pass, gains a technology; the Pharad'n capture 1
    {
        MonCase c;
        c.name = "Pharad'n: Death Binding";
        c.faction = "pharadn";
        c.second = "sol";
        c.modules = kBR;
        c.untilRound = 3;
        c.setUp = [](Game& g, int seat) {
            const int q = otherSeat(g, seat);
            auto& mine = g.players[size_t(seat)].notes;
            const int16_t n = makeNote(seat, kNoteFaction);
            auto it = std::find(mine.begin(), mine.end(), n);
            if (it == mine.end()) return false;
            mine.erase(it);
            g.players[size_t(q)].notes.push_back(n);
            const int qh = g.players[size_t(q)].homeSystem;
            for (int16_t x : g.systems[size_t(qh)].planets) {
                g.planets[size_t(x)].units[size_t(q)].add(UnitType::Infantry, 6);
                break;
            }
            g.players[size_t(seat)].captured.clear();
            return true;
        };
        c.drive = [](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player == otherSeat(g, seat) && d.tag == Tag::Faction && d.ability == int(Ab::PharDeathBinding)) return a = one(1), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "(Death Binding)")) return why = "no question (Death Binding)", false;
            if (!logHas(r, "removes 4 infantry and gains")) return why = "no technology gained", false;
            if (!logHas(r, "captures their own 1 Infantry (Death Binding)")) return why = "the Pharad'n didn't capture 1", false;
            const int q = otherSeat(r.after, r.seat);
            if (holdsNote(r.after, q, r.seat, kNoteFaction)) return why = "the note wasn't returned", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Reality-Field Impactor: an anomaly stilled for the tactical action
    {
        MonCase c;
        c.name = "Pharad'n: Reality-Field Impactor";
        c.faction = "pharadn";
        c.modules = kBR;
        c.untilRound = 3;
        c.setUp = [](Game& g, int seat) {
            g.players[size_t(seat)].techs.push_back(int16_t(findTech("dspharb")));
            return true;
        };
        c.drive = [](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.player != seat || d.tag != Tag::Faction || d.ability != int(Ab::PharImpactor)) return false;
            for (const Option& o : d.options)
                if (o.enabled && o.id >= 0) return a = one(o.id), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "(Reality-Field Impactor)")) return why = "no question (Reality-Field Impactor)", false;
            if (r.after.fx.rfiSys >= 0 && r.after.activeSystem < 0) return why = "the anomaly stays stilled after the action", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Ancient Empire: tombs at setup; a tomb opened when they take its planet (2 infantry captured); Dark Purpose
    {
        MonCase c;
        c.name = "Pharad'n: tombs (Ancient Empire), Dark Purpose";
        c.faction = "pharadn";
        c.modules = kBR;
        c.untilRound = 4;
        c.setUp = [](Game& g, int seat) {  // a tomb on every planet nobody holds: whichever they take next has one
            for (PlanetState& ps : g.planets)
                if (ps.owner < 0) ps.tomb = true;
            (void)seat;
            return true;
        };
        c.drive = [](const Game&, const Decision&, int, Answer&) { return false; };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "tomb tokens (Ancient Empire)") && r.before.planets.size() > 0) {
                bool any = false;
                for (const PlanetState& ps : r.before.planets) any = any || ps.tomb;
                if (!any) return why = "no tombs at setup", false;
            }
            if (!logHas(r, "captures 2 infantry (Ancient Empire)")) return why = "no question (no tomb opened)", false;
            // (owner 13:08) only the Pharad'n purge a tomb: none left on a planet they hold; other players' planets may keep theirs
            bool othersKeep = false;
            for (const PlanetState& ps : r.after.planets) {
                if (ps.tomb && ps.owner == r.seat) return why = "a tomb on a planet the Pharad'n hold", false;
                othersKeep = othersKeep || (ps.tomb && ps.owner >= 0 && ps.owner != r.seat);
            }
            if (!othersKeep) return why = "no tomb left on another player's planet", false;
            if (!logHas(r, "(Dark Purpose)")) return why = "no Dark Purpose token after scoring", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Hidden Vaults (breakthrough): 2 infantry destroyed on a planet -> captured, a Sacrotitan; produce 1 infantry on an explored planet
    {
        MonCase c;
        c.name = "Pharad'n: Hidden Vaults";
        c.faction = "pharadn";
        c.modules = kBR | kModThundersEdge;
        c.untilRound = 3;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            Player& pl = g.players[size_t(seat)];
            pl.breakthrough = true;
            pl.btExhausted = false;
            const int home = pl.homeSystem;
            for (int16_t x : g.systems[size_t(home)].planets) *at = x;
            if (*at < 0) return false;
            g.planets[size_t(*at)].units[size_t(seat)].remove(UnitType::Infantry, 99, false);
            g.planets[size_t(*at)].units[size_t(seat)].add(UnitType::Infantry, 4);
            return true;
        };
        c.drive = [at](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.player != seat) return false;
            if (d.tag == Tag::ActionChoice && offers(d, 671)) return a = one(671), true;
            if (d.tag == Tag::Faction && d.ability == int(Ab::PharVaultsDestroy)) {
                Answer b;
                for (const Option& o : d.options)
                    if (o.enabled && o.id >= 0 && o.id < 10000000 && o.id / 128 == *at && (o.id % 128) / 8 == int(UnitType::Infantry) && b.picks.size() < 2)
                        b.picks.push_back(o.id);
                return a = b, !b.picks.empty();
            }
            if (d.tag == Tag::Faction && d.ability == int(Ab::PharSacrotitan)) return a = one(1), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "destroys 2 of their units (Hidden Vaults)")) return why = "no question (Hidden Vaults)", false;
            if (!logHas(r, "captures their own 2 Infantry (Immortal)")) return why = "the 2 infantry weren't captured", false;
            if (!logHas(r, "(Sacrotitan)")) return why = "no Sacrotitan after 2 infantry destroyed", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Pharad'n the Immortal: an invasion of a planet with their PDS - every infantry there destroyed
    {
        MonCase c;
        c.name = "Pharad'n: the hero's Black Star - Apocalypse";
        c.faction = "pharadn";
        c.second = "sardakk";
        c.modules = kBR;
        c.untilRound = 4;
        auto at = std::make_shared<int>(-1);
        c.setUp = [at](Game& g, int seat) {
            const int q = otherSeat(g, seat);
            const int qh = g.players[size_t(q)].homeSystem;
            for (int16_t a : g.systems[size_t(qh)].adj) {
                for (int16_t px : g.systems[size_t(a)].planets)
                    if (!isStation(g, px) && !homeSys(g, a) && g.systems[size_t(a)].planets.size() == 1) *at = px;
                if (*at >= 0) break;
            }
            if (*at < 0) return false;
            clearPlanet(g, *at);
            PlanetState& ps = g.planets[size_t(*at)];
            ps.owner = int8_t(seat);
            ps.units[size_t(seat)].add(UnitType::PDS, 1);
            ps.units[size_t(seat)].add(UnitType::Infantry, 1);
            for (LeaderState& l : g.players[size_t(seat)].leaders)
                if (l.leader >= 0 && kLeaders[l.leader].type == kHero) l.status = kReady;
            g.systems[size_t(qh)].space[size_t(q)].add(UnitType::Carrier, 1);
            g.systems[size_t(qh)].space[size_t(q)].add(UnitType::Infantry, 4);
            g.systems[size_t(qh)].space[size_t(q)].add(UnitType::Dreadnought, 2);
            return true;
        };
        c.drive = [](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.player == seat && d.tag == Tag::Faction && d.ability == int(Ab::PharHero) && offers(d, 1)) return a = one(1), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "(Pharad'n the Immortal)")) return why = "no question (the hero wasn't asked)", false;
            if (!logHas(r, "every infantry on")) return why = "the infantry weren't destroyed", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- Mark of Pharad'n in production: infantry paid with captured infantry, no resources
    {
        MonCase c;
        c.name = "Pharad'n: infantry produced with captured infantry";
        c.faction = "pharadn";
        c.modules = kBR;
        c.untilRound = 3;
        auto tgBefore = std::make_shared<int>(-1);
        c.setUp = [](Game& g, int seat) {
            Player& pl = g.players[size_t(seat)];
            pl.captured.clear();
            for (int i = 0; i < 2; ++i) pl.captured.push_back(int16_t(kCapturedToken * 16 + int(UnitType::Infantry)));
            return true;
        };
        c.drive = [tgBefore](const Game& g, const Decision& d, int seat, Answer& a) {
            if (d.player != seat || d.kind != DecisionKind::Produce || *tgBefore >= 0) return false;
            const int have = pharCaptured(g, seat);
            const std::vector<int> dest = groundForceDestinations(g, seat, d.system);
            if (have < 2 || dest.empty() || d.validate == nullptr) return false;
            a = Answer();
            a.data.assign(kUnitTypes + 1, 0);
            a.data[int(UnitType::Infantry)] = 2;
            a.data[kUnitTypes] = dest[0];
            if (!d.validate(a).empty()) return false;
            *tgBefore = g.players[size_t(seat)].tradeGoods;
            return true;
        };
        c.check = [tgBefore](const MonRun& r, std::string& why) {
            if (*tgBefore < 0) return why = "no question (no production with 2 captured)", false;
            if (!logHas(r, "produces 2 Infantry")) return why = "the 2 infantry weren't produced", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Avhkan: at the start of a ground combat on their planet, 1 of the invader's infantry destroyed
    {
        MonCase c;
        c.name = "Pharad'n: Avhkan strikes an invader";
        c.faction = "pharadn";
        c.second = "sardakk";
        c.modules = kBR;
        c.untilRound = 4;
        c.setUp = [](Game& g, int seat) {
            const int q = otherSeat(g, seat);
            const int qh = g.players[size_t(q)].homeSystem;
            int at = -1;
            for (int16_t a : g.systems[size_t(qh)].adj) {
                for (int16_t px : g.systems[size_t(a)].planets)
                    if (!isStation(g, px) && !homeSys(g, a) && g.systems[size_t(a)].planets.size() == 1) at = px;
                if (at >= 0) break;
            }
            if (at < 0) return false;
            clearPlanet(g, at);
            g.planets[size_t(at)].owner = int8_t(seat);
            g.planets[size_t(at)].units[size_t(seat)].add(UnitType::Infantry, 2);
            for (LeaderState& l : g.players[size_t(seat)].leaders)
                if (l.leader >= 0 && kLeaders[l.leader].type == kCommander) l.status = kReady;
            g.systems[size_t(qh)].space[size_t(q)].add(UnitType::Carrier, 1);
            g.systems[size_t(qh)].space[size_t(q)].add(UnitType::Infantry, 4);
            g.systems[size_t(qh)].space[size_t(q)].add(UnitType::Dreadnought, 2);
            return true;
        };
        c.drive = [](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.player == seat && d.tag == Tag::Faction && d.ability == int(Ab::PharAvhkan))  // the invader's infantry
                for (const Option& o : d.options)
                    if (o.enabled && o.id > 0 && o.id - 1 != seat) return a = one(o.id), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            if (!logHas(r, "(Avhkan)")) return why = "no question (Avhkan)", false;
            if (!logHas(r, "'s infantry on")) return why = "no infantry destroyed", false;
            return true;
        };
        cases.push_back(c);
    }
    // ---- Black Pyramid: DEPLOY at their Commit Ground Forces step, in a system where they hold a planet
    {
        MonCase c;
        c.name = "Pharad'n: Black Pyramid DEPLOY";
        c.faction = "pharadn";
        c.modules = kBR | kModMonuments;
        c.untilRound = 4;
        auto at = std::make_shared<int>(-1);  // (0.85.0: the system - activated by the drive; the bot raised the Pyramid elsewhere first)
        c.setUp = [at](Game& g, int seat) {
            removeMonuments(g, seat);
            // a planet the Pyramid may stand on, in a system with another planet nobody holds; theirs, with ships and infantry beside it
            const int x = monumentPlanet(g, seat, [&](int y) {
                const int s = g.planets[size_t(y)].system;
                if (g.systems[size_t(s)].planets.size() < 2) return false;
                for (int16_t o : g.systems[size_t(s)].planets)
                    if (o != y && g.planets[size_t(o)].owner >= 0) return false;
                return true;
            });
            if (x < 0) return false;
            const int s = g.planets[size_t(x)].system;
            *at = s;
            for (int16_t o : g.systems[size_t(s)].planets) {
                clearPlanet(g, o);
                if (o != x) g.planets[size_t(o)].tomb = true;  // (a tomb there: worth taking)
            }
            g.planets[size_t(x)].owner = int8_t(seat);
            g.planets[size_t(x)].units[size_t(seat)].add(UnitType::Infantry, 1);
            for (int16_t a : g.systems[size_t(s)].adj)
                if (!homeSys(g, a) && !g.systems[size_t(a)].offBoard && !g.systems[size_t(a)].d().anomalies) {
                    g.systems[size_t(a)].space[size_t(seat)].add(UnitType::Carrier, 1);
                    g.systems[size_t(a)].space[size_t(seat)].add(UnitType::Infantry, 3);
                    break;
                }
            return true;
        };
        c.drive = [at](const Game&, const Decision& d, int seat, Answer& a) {
            if (d.player == seat && d.tag == Tag::Activate && offers(d, *at)) return a = one(*at), true;
            if (d.player != seat || d.tag != Tag::Faction || d.ability != int(Ab::PharPyramid)) return false;
            for (const Option& o : d.options)
                if (o.enabled && o.id >= 0) return a = one(o.id), true;
            return false;
        };
        c.check = [](const MonRun& r, std::string& why) {
            bool asked = false;
            for (const Step& st : r.steps)
                asked = asked || (st.player == r.seat && st.ability == int(Ab::PharPyramid) && !st.answer.empty() && st.answer[0] >= 0);
            if (!asked) return why = "no question (no DEPLOY)", false;
            if (!logHas(r, "raises their monument, Black Pyramid")) return why = "the Pyramid wasn't placed", false;
            return true;
        };
        cases.push_back(c);
    }

    // ---- run
    int pass = 0, fail = 0;
    for (const MonCase& c : cases) {
        if (only && c.name.find(only) == std::string::npos) continue;
        MonRun r;
        bool ok = false;
        std::string why;
        // (a case that can't be set up on one map tries the next seed)
        for (uint32_t k = 0; k < 6; ++k) {
            MonCase c2 = c;
            c2.seed = c.seed + k * 17;
            r = runCase(c2);
            if (r.refused.find("couldn't be set up") != std::string::npos) continue;
            if (!r.refused.empty()) why = "refused: " + r.refused;
            else if (!r.broken.empty()) why = "invariant: " + r.broken;
            else ok = c.check ? c.check(r, why) : true;
            if (!ok && why.find("no question") != std::string::npos && k < 5) continue;  // (the situation didn't come up: another map)
            break;
        }
        std::printf("montest: %-58s %s%s\n", c.name.c_str(), ok ? "ok" : "FAILED - ", ok ? "" : why.c_str());
        if (debug)
            for (const LogLine& l : r.log) std::printf("    | %s\n", l.text.c_str());
        (ok ? pass : fail)++;
    }
    std::printf("montest: %d ok, %d failed\n", pass, fail);
    return fail == 0 ? 1 : 0;
}
