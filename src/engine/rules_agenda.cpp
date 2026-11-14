// The agenda phase (LRR 8): revealing agendas, "when / after revealed"
// windows, voting, ties, elections, outcomes, laws and directives, riders,
// plus the Politics card's look at the agenda deck.
#include <algorithm>
#include <cstring>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
bool agKey(int a, const char* k) { return a >= 0 && a < kAgendaCount && !std::strcmp(kAgendas[a].key, k); }
constexpr int kAbstain = -100;

bool isHomePlanetIdx(const Game& g, int x) { return g.systems[g.planets[x].system].home >= 0; }
}  // namespace

// ------------------------------------------------------------------ outcomes
std::vector<Option> Rules::agendaOutcomes(int a) const {
    std::vector<Option> v;
    if (a < 0) return v;
    if (agKey(a, "covert") && G.covertAgenda >= 0) a = G.covertAgenda;
    switch (kAgendas[a].elect) {
        case Elect::ForAgainst:
            v.push_back({0, "For", agendaText(a, false)});
            v.push_back({1, "Against", agendaText(a, true)});
            break;
        case Elect::Player:
            for (int q = 0; q < G.numPlayers; ++q)
                if (!G.players[q].eliminated) v.push_back({q, pn(q), std::to_string(G.players[q].vp) + " victory points"});
            break;
        case Elect::Planet:
        case Elect::NonHomePlanet: {
            int mec = G.mecatol();
            for (size_t x = 0; x < G.planets.size(); ++x) {
                const PlanetState& ps = G.planets[x];
                if (ps.owner < 0) continue;
                if (kAgendas[a].elect == Elect::NonHomePlanet && (isHomePlanetIdx(G, int(x)) || ps.system == mec)) continue;
                v.push_back({int(x), planetName(int(x)), pn(ps.owner) + "'s planet"});
            }
            break;
        }
        case Elect::Law:
            for (const Law& l : G.laws) v.push_back({l.agenda, kAgendas[l.agenda].name, agendaText(l.agenda, false)});
            break;
        case Elect::ScoredSecret:
            for (int q = 0; q < G.numPlayers; ++q)
                for (int16_t o : G.players[q].scored)
                    if (kObjectives[o].kind == 3) v.push_back({o, kObjectives[o].name, "scored by " + pn(q)});
            break;
        case Elect::StrategyCard:
            for (int c = 0; c < kStrategyCards; ++c) v.push_back({c, std::to_string(c + 1) + " " + strategyName(c), strategySecondary(c)});
            break;
        case Elect::Covert: break;
    }
    return v;
}

std::string Rules::outcomeName(int a, int outcome) const {
    for (const Option& o : agendaOutcomes(a))
        if (o.id == outcome) return o.label;
    return "?";
}

bool Rules::agendaRevealable(int a) const {
    // "If there are no laws in play, discard this card and reveal another."
    if (agKey(a, "abolishment") || agKey(a, "constitution") || agKey(a, "miscount")) return !G.laws.empty();
    if (agKey(a, "classified")) {
        for (const Player& pl : G.players)
            for (int16_t o : pl.scored)
                if (kObjectives[o].kind == 3) return true;
        return false;
    }
    return true;
}

int Rules::revealAgenda() {
    for (int guard = 0; guard < 80; ++guard) {
        if (G.agendaDeck.empty()) {
            if (G.agendaDiscard.empty()) return -1;
            G.agendaDeck = G.agendaDiscard;
            G.agendaDiscard.clear();
            E.rng().shuffle(G.agendaDeck);
        }
        int a = G.agendaDeck.back();
        G.agendaDeck.pop_back();
        E.revealed();
        if (!agendaRevealable(a)) {
            say(-1, std::string(kAgendas[a].name) + " is revealed but can't be voted on now - it is discarded.");
            G.agendaDiscard.push_back(int16_t(a));
            continue;
        }
        return a;
    }
    return -1;
}

// ------------------------------------------------------------------ the phase
void Rules::agendaPhase() {
    G.phase = Phase::Agenda;
    E.log(-1, "Agenda phase", EventKind::Phase);
    G.noVote = 0;
    readyOnly3_ = false;
    mawOfWorlds();  // "at the start of the agenda phase"
    twilightMirror();  // Uncharted Space relic (0.33.0)
    if (G.modules & kModDiscordant) dsAgendaPhaseStart();  // Contractual Obligations (Roh'Dhna)
    if (G.extras & 16) loansDue();  // 0.30.0: debts (X-1 credit, loans) are paid back here
    {
        WinCtx c;
        c.w = Win::AgendaStart;
        window(c);
    }
    for (int i = 0; i < 2; ++i) resolveAgenda(i);
    G.currentAgenda = -1;
    curAgenda_ = -1;
    // Political Secret silences a player for that agenda only (0.24.0: it used to last until the
    // next agenda phase, so they couldn't play action cards all round - the tester's Flank Speed)
    silenced_ = 0;
    // LRR 8.4: ready all planets (Checks and Balances "Against": only 3 each).
    for (int p : clockwiseFrom(G, G.speaker)) {
        std::vector<int> ex;
        for (int x : controlledPlanets(G, p))
            if (G.planets[x].exhausted) ex.push_back(x);
        if (readyOnly3_ && ex.size() > 3) {
            std::vector<Option> opts;
            for (int x : ex)
                opts.push_back({x, planetName(x),
                                std::to_string(planetResources(G, x)) + " res / " + std::to_string(planetInfluence(G, x)) + " inf"});
            for (int x : pickMany(p, Tag::AgendaChoice, "Checks and Balances: ready which 3 planets?", opts, 3, 3, "", true))
                G.planets[x].exhausted = false;
        } else {
            for (int x : ex) G.planets[x].exhausted = false;
        }
        // (0.44.9, fixes 13) Bannerhalls are planet cards: they ready with the planets
        if (G.fixes >= 13 && bannerhalls(G, p)) G.players[size_t(p)].oceansExhausted = 0;
        if (G.fixes >= 27 && G.players[size_t(p)].oceans > 0) G.players[size_t(p)].oceansExhausted = 0;  // (ocean cards too)
    }
    readyOnly3_ = false;
}

void Rules::resolveAgenda(int index) {
    G.agendaNumber = int8_t(index);
    int a = revealAgenda();
    if (a < 0) {
        say(-1, "The agenda deck is empty.");
        return;
    }
    runAgenda(a, false);
}

// Votes on agenda a (also used by Miscount Disclosed for a law in play).
void Rules::runAgenda(int a, bool miscount) {
    const int n = G.numPlayers;
    G.currentAgenda = int16_t(a);
    G.covertAgenda = -1;
    curAgenda_ = a;
    cannotVote_ = G.noVote;
    silenced_ = 0;
    mustVote_ = false;
    lastVoter_ = -1;
    for (auto& v : predictions_) v.clear();
    G.votedFor.assign(n, kNoVote);
    G.voteCount.assign(n, 0);
    auto announce = [&](int ag) {
        Event e;
        e.kind = EventKind::Agenda;
        e.card = int16_t(ag);
        e.text = std::string("Agenda: ") + kAgendas[ag].name + (kAgendas[ag].law ? " (law)" : " (directive)");
        E.emit(e);
    };
    announce(a);

    // "When an agenda is revealed": Veto, Diplomatic Pressure, Political Secret
    if (!miscount) {
        for (int guard = 0; guard < 10; ++guard) {
            WinCtx c;
            c.w = Win::AgendaRevealed;
            std::vector<Played> played = window(c);
            for (int q : clockwiseFrom(G, G.speaker))
                for (int16_t note : std::vector<int16_t>(G.players[q].notes))
                    if (noteType(note) == kNotePoliticalSecret && noteOwner(note) != q && !G.players[noteOwner(note)].eliminated &&
                        !((silenced_ >> noteOwner(note)) & 1) &&
                        playNoteWindow(q, kNotePoliticalSecret, noteOwner(note),
                                       "Play " + pn(noteOwner(note)) + "'s Political Secret?")) {
                        silenced_ = uint8_t(silenced_ | (1u << noteOwner(note)));
                        say(q, pn(noteOwner(note)) + " can't vote or play action cards on this agenda.");
                    }
            if (!cardHas(played, "veto") && !quashCheck(a)) break;  // Veto, Quash, Political Favor
            G.agendaDiscard.push_back(int16_t(a));
            a = revealAgenda();
            if (a < 0) return;
            G.currentAgenda = int16_t(a);
            curAgenda_ = a;
            announce(a);
        }
    }

    agendaRevealed2(a);  // Z'eu (Naalu agent)

    // Covert Legislation: the speaker secretly draws the agenda that is voted on.
    int real = a;
    if (agKey(a, "covert")) {
        int h = revealAgenda();
        if (h < 0) {
            G.agendaDiscard.push_back(int16_t(a));
            return;
        }
        G.covertAgenda = int16_t(h);
        real = h;
        say(G.speaker, pn(G.speaker) + " secretly draws the agenda everyone votes on.");
    }
    const Elect kind = kAgendas[real].elect;

    // Committee Formation: its owner may pick the elected player without a vote.
    int committee = lawOwner(G, "committee");
    if (kind == Elect::Player && committee >= 0 && !G.players[committee].eliminated) {
        int prev = curAgenda_;
        curAgenda_ = real;
        bool use = yesNo(committee, Tag::LawAbility, "Discard Committee Formation to choose the elected player?",
                         std::string(kAgendas[real].name) + ": " + agendaText(real, false));
        if (use) {
            int elected = pick(committee, Tag::AgendaChoice, std::string("Committee Formation: who is elected for ") +
                                                                 kAgendas[real].name + "?",
                               agendaOutcomes(real));
            for (size_t i = 0; i < G.laws.size(); ++i)
                if (agKey(G.laws[i].agenda, "committee")) {
                    discardLaw(int(i), "used");
                    break;
                }
            curAgenda_ = prev;
            if (real != a) G.agendaDiscard.push_back(int16_t(a));
            std::vector<int> none(n, kNoVote), zero(n, 0);
            resolveOutcome(real, elected, none, zero);
            G.covertAgenda = -1;
            return;
        }
        curAgenda_ = prev;
    }

    // "After an agenda is revealed": riders, Assassinate Representative, Hack Election, Insider Information
    {
        WinCtx c;
        c.w = Win::AfterAgendaRevealed;
        for (const Played& x : window(c))  // Hack Election: that player votes last (0.21.1 audit: it did nothing before)
            if (!std::strcmp(kActionCards[x.card].key, "hack")) lastVoter_ = x.player;
    }
    keleresAgendaRevealed(real);  // Keleres Rider, Odlynn Myrr
    if (G.modules & kModDiscordant) dsAgendaRevealed(real);  // Kyro Rider (Discordant Stars)

    nekroPredict(real);  // Galactic Threat
    tarrockPredict(real);  // Council Black Site (Uncharted Space, 0.33.0)

    // Voting
    votingStep(real, G.votedFor, G.voteCount, cannotVote_, lastVoter_);
    keleresAfterVoting();                // Odlynn Myrr pays out
    if (G.modules & kModDiscordant) dsAfterVoting(G.votedFor, G.voteCount);  // Silas Deriga (Kyro commander)
    bloodPact(G.votedFor, G.voteCount);  // Empyrean note: +4 votes
    persVotes();                         // "vote them down" betrayals

    // Tally (LRR 8.19: ties, or no votes at all, are decided by the speaker)
    std::vector<Option> outs = agendaOutcomes(a);
    std::vector<int> totals(outs.size(), 0);
    for (int q = 0; q < n; ++q)
        for (size_t i = 0; i < outs.size(); ++i)
            if (G.votedFor[q] == outs[i].id) totals[i] += G.voteCount[q];
    int best = 0;
    for (int t : totals) best = std::max(best, t);
    std::vector<Option> tied;
    for (size_t i = 0; i < outs.size(); ++i)
        if (totals[i] == best) tied.push_back({outs[i].id, outs[i].label, std::to_string(totals[i]) + " votes"});
    int outcome = -1;
    if (tied.empty()) {
        if (real != a) G.agendaDiscard.push_back(int16_t(a));
        G.agendaDiscard.push_back(int16_t(real));
        say(-1, "Nothing can be chosen for this agenda - it is discarded.");
        return;
    }
    if (tied.size() == 1) {
        outcome = tied[0].id;
    } else {
        outcome = pick(G.speaker, Tag::TieBreak,
                       best > 0 ? "The vote is tied: you are the speaker, choose the outcome" : "Nobody voted: you are the speaker, choose the outcome",
                       tied);
        say(G.speaker, pn(G.speaker) + " (speaker) decides: " + outcomeName(a, outcome) + ".");
    }

    // Elections: Confusing / Confounding Legal Text
    if (kind == Elect::Player) {
        WinCtx c;
        c.w = Win::Elected;
        c.a = outcome;
        for (const Played& x : window(c)) {
            if (!std::strcmp(kActionCards[x.card].key, "confounding")) {
                outcome = x.player;
            } else if (!std::strcmp(kActionCards[x.card].key, "confusing")) {
                std::vector<Option> opts;
                for (int q = 0; q < n; ++q)
                    if (q != x.player && !G.players[q].eliminated) opts.push_back({q, pn(q), ""});
                outcome = pick(x.player, Tag::CardTarget, "Confusing Legal Text: who is elected instead?", opts);
            }
        }
        say(outcome, pn(outcome) + " is elected.");
    }

    // "When an outcome would be resolved": Deadly Plot
    {
        WinCtx c;
        c.w = Win::OutcomeResolve;
        c.value = outcome;
        std::vector<Played> plots = window(c);
        if (!plots.empty()) {
            Event e;
            e.kind = EventKind::AgendaResult;
            e.card = int16_t(real);
            e.text = std::string(kAgendas[real].name) + " is discarded by Deadly Plot - no effect.";
            E.emit(e);
            for (int x : controlledPlanets(G, plots[0].player)) G.planets[x].exhausted = true;
            if (real != a) G.agendaDiscard.push_back(int16_t(a));
            if (!miscount) G.agendaDiscard.push_back(int16_t(real));
            G.covertAgenda = -1;
            return;
        }
    }
    if (real != a) {
        G.agendaDiscard.push_back(int16_t(a));  // Covert Legislation itself
        say(-1, std::string("The covert agenda was ") + kAgendas[real].name + ".");
    }
    G.covertAgenda = -1;
    curAgenda_ = real;
    // (Miscount Disclosed votes again inside resolveOutcome: keep this vote's data)
    std::vector<int> preds[kMaxPlayers];
    for (int q = 0; q < kMaxPlayers; ++q) preds[q] = predictions_[q];
    std::vector<int> voted = G.votedFor, counts = G.voteCount;
    bool pis[kMaxPlayers];
    std::copy(piUsed_, piUsed_ + kMaxPlayers, pis);
    tarrockCheck(outcome);
    resolveOutcome(real, outcome, voted, counts);
    keleresAgendaResolved(outcome);      // Keleres Rider
    if (G.modules & kModDiscordant) dsAgendaResolved(outcome, voted);  // Kyro Rider, Indoctrination Teams
    nekroResolve(real, outcome, voted);  // Galactic Threat
    futureSight(outcome, voted, preds);  // Nomad

    // Predictive Intelligence is exhausted if its votes went to a losing outcome.
    for (int q = 0; q < n; ++q)
        if (pis[q] && voted[q] != outcome) {
            exhaustTech(q, "pi");
            // (fixes 27, the shared technologies) said in the log
            if (G.fixes >= 27) say(q, pn(q) + "'s Predictive Intelligence is exhausted (the outcome they voted for lost).");
        }
    // Riders
    for (int q = 0; q < n; ++q)
        for (size_t i = 0; i + 1 < preds[q].size(); i += 2) {
            int card = preds[q][i], guess = preds[q][i + 1];
            if (guess != outcome) {
                say(q, pn(q) + "'s " + kActionCards[card].name + " was wrong.");
                continue;
            }
            const char* k = kActionCards[card].key;
            say(q, pn(q) + "'s " + kActionCards[card].name + " was right!");
            int prev = curCard_;
            curCard_ = card;
            if (!std::strcmp(k, "imp_rider")) gainVP(q, 1, "Imperial Rider");
            else if (!std::strcmp(k, "trade_rider")) gainTG(q, 5);
            else if (!std::strcmp(k, "lead_rider")) gainTokens(q, 3, false, "Leadership Rider: gain 3 command tokens.");
            else if (!std::strcmp(k, "tech_rider")) researchStep(q, 0, "Technology Rider");
            else if (!std::strcmp(k, "politic_rider")) {
                drawActionCards(q, 3, "Politics Rider");
                G.speaker = int8_t(q);
                say(q, pn(q) + " becomes the speaker.");
            } else if (!std::strcmp(k, "const_rider")) {
                if (unitsAvailable(G, q, UnitType::SpaceDock) > 0) placeStructure(q, -1, true, false, false);
            } else if (!std::strcmp(k, "war_rider")) {
                std::vector<Option> opts;
                for (size_t s = 0; s < G.systems.size(); ++s)
                    if (hasShips(G, q, int(s))) opts.push_back({int(s), sysName(int(s)), ""});
                if (!opts.empty() && unitsAvailable(G, q, UnitType::Dreadnought) > 0) {
                    int s = pick(q, Tag::CardTarget, "Warfare Rider: place a dreadnought where?", opts, "", true);
                    addUnits(q, UnitType::Dreadnought, 1, s, -1);
                    Event e;  // 0.29.0: the placement shows in the feed (display only)
                    e.kind = EventKind::Note;
                    e.player = int8_t(q);
                    e.system = int16_t(s);
                    e.text = pn(q) + " places a dreadnought in " + sysName(s) + " (Warfare Rider).";
                    E.emit(std::move(e));
                    enforceFleetLimit(q);
                }
            } else if (!std::strcmp(k, "diplo_rider")) {
                std::vector<Option> opts;
                for (size_t s = 0; s < G.systems.size(); ++s)
                    for (int16_t x : G.systems[s].planets)
                        if (G.planets[x].owner == q) {
                            opts.push_back({int(s), sysName(int(s)), ""});
                            break;
                        }
                if (!opts.empty()) {
                    int s = pick(q, Tag::CardTarget, "Diplomacy Rider: which system?", opts, "Every other player places a command token there.", true);
                    for (int r : clockwiseFrom(G, (q + 1) % n))
                        if (r != q && !G.systems[s].hasToken(r)) placeTokenFromReinforcements(r, s, "Diplomacy Rider");
                }
            } else if (!std::strcmp(k, "sanction")) {
                for (int r = 0; r < n; ++r)
                    if (voted[r] == outcome && counts[r] > 0 && G.players[r].fleet > 0) {
                        G.players[r].fleet = int8_t(G.players[r].fleet - 1);
                        enforceFleetLimit(r);
                        say(r, pn(r) + " loses a fleet token (Sanction).");
                    }
            }
            curCard_ = prev;
        }
    for (int q = 0; q < n; ++q) agendaSecrets(q);
}

void Rules::votingStep(int a, std::vector<int>& votedFor, std::vector<int>& votes, uint8_t cannotVote, int lastVoter) {
    const int n = G.numPlayers;
    std::fill(piUsed_, piUsed_ + kMaxPlayers, false);
    // LRR 8.2: starting left of the speaker, clockwise; the speaker votes last (Hack Election: that player is last).
    std::vector<int> order = clockwiseFrom(G, (G.speaker + 1) % n);
    if (lastVoter_ >= 0) {
        order.erase(std::remove(order.begin(), order.end(), lastVoter_), order.end());
        order.push_back(lastVoter_);
    }
    // Zeal (Argent): always votes first ((fixes 26, the Argent review - owner 2026-11-13) "during the agenda phase" - not on
    // an agenda voted on in the action phase, Executive Order's; the extra votes still come)
    const int argent = playerOfFaction(G, "argent");
    if (argent >= 0 && (G.fixes < 26 || G.phase == Phase::Agenda)) {
        order.erase(std::remove(order.begin(), order.end(), argent), order.end());
        order.insert(order.begin(), argent);
    }
    (void)lastVoter;
    agendaNegotiations(order);  // Negotiations: deals before the vote
    const bool repGov = lawInPlay(G, "rep_govt");
    for (int p : order) {
        if (G.players[p].eliminated) continue;
        bool blocked = ((cannotVote_ >> p) & 1) || ((silenced_ >> p) & 1) || ((G.noVote >> p) & 1);
        if (commanderUsable(G, p, "xxcha")) blocked = false;  // Elder Qanoj: nothing stops you voting
        if (fac(p, "nekro")) blocked = true;  // Galactic Threat: the Nekro Virus never votes
        if (eventOn(G, kEvAtomics) && !G.players[size_t(p)].atomics) blocked = true;  // Stellar Atomics: the token spent
        (void)cannotVote;
        const bool exec = p == execVoter_ && !repGov;  // Executive Order: resources and trade goods count as votes
        int avail = repGov ? 1 : availableVotes(G, p);
        if (exec) {
            avail = G.players[p].tradeGoods;
            for (int x : ti::readyPlanets(G, p)) avail += std::max(voteValue(G, p, x), planetResources(G, x));
        }
        // Predictive Intelligence: 3 more votes in the agenda phase only (0.44.5, fixes 9: not Executive Order's agenda)
        bool pi = !repGov && techReady(p, "pi") && !(G.fixes >= 9 && G.phase != Phase::Agenda);
        const bool gila = !repGov && commanderUsable(G, p, "hacan") && G.players[p].tradeGoods > 0;
        const bool odlynn = p == odlynn_;  // Odlynn Myrr: 6 more votes
        if (!blocked && (avail > 0 || pi || gila || odlynn)) {
            std::vector<Option> opts = agendaOutcomes(G.currentAgenda);
            // show the votes so far (Hidden Agenda: only the Argent's are known)
            const bool hidden = eventOn(G, kEvHidden);
            for (Option& o : opts) {
                int t = 0;
                for (int q = 0; q < n; ++q)
                    if (votedFor[q] == o.id && (!hidden || fac(q, "argent"))) t += votes[q];
                if (t) o.detail = std::to_string(t) + " votes so far" + (hidden ? " (the Argent's)" : "") + ". " + o.detail;
            }
            if (agKey(G.currentAgenda, "covert") && p != G.speaker)
                for (Option& o : opts) o.detail = o.detail.find("votes so far") != std::string::npos
                                                      ? o.detail.substr(0, o.detail.find(". ") + 1)
                                                      : "";
            if (!mustVote_ || avail <= 0) opts.push_back({kAbstain, "Abstain", "Don't vote on this agenda."});  // Special Session
            // Genetic Recombination (Mahact): this player must vote for the chosen outcome (or lose a fleet token)
            // (0.44.6, fixes 10: also under Representative Government - everyone still casts their 1 vote)
            const int forced = avail > 0 && (!repGov || G.fixes >= 10) ? geneticRecombination(p, a) : kNoVote;
            Decision d;
            d.player = p;
            d.kind = DecisionKind::Pick;
            d.tag = Tag::VoteOutcome;
            d.agenda = a;
            d.prompt = std::string("Vote: ") + kAgendas[G.currentAgenda].name;
            std::string votesText = std::string("You have ") + std::to_string(avail) +
                                    (repGov ? " vote." : exec ? " votes (Executive Order: planets give resources or influence, trade goods count)."
                                                              : " votes from ready planets.");
            if (odlynn) votesText += " Odlynn Myrr: 6 more.";
            {  // 0.43.0 (owner: "show everything others voted before my turn"): who voted what, in order
                std::string before;
                const bool covertHidden = agKey(G.currentAgenda, "covert") && p != G.speaker;
                for (int q : order) {
                    if (q == p) break;
                    if (G.players[q].eliminated) continue;
                    std::string part = pn(q) + " ";
                    if (votedFor[q] == kNoVote) part += "didn't vote";
                    else if (hidden && !fac(q, "argent")) part += "voted in secret";
                    else {
                        part += std::to_string(votes[q]) + " for ";
                        std::string lab;
                        for (const Option& o : opts)
                            if (o.id == votedFor[q]) lab = o.label;
                        part += covertHidden || lab.empty() ? "an outcome" : lab;
                    }
                    before += (before.empty() ? "" : "; ") + part;
                }
                if (!before.empty()) votesText += " Before you: " + before + ".";
            }
            if (agKey(G.currentAgenda, "covert") && p != G.speaker)
                d.context = "Covert Legislation: only the speaker knows what these outcomes do. " + votesText;
            else if (kAgendas[a].elect != Elect::ForAgainst)
                d.context = std::string(agendaText(a, false)) + " " + votesText;
            else
                d.context = votesText;
            for (const Promise& pr : G.promises)  // a vote promised in a deal (not binding)
                if (pr.state == kPromiseActive && pr.by == p && pr.kind == PromiseKind::VoteFor && pr.agenda == G.currentAgenda)
                    for (Option& o : opts)
                        if (o.id == pr.target) {
                            d.context = "You promised " + pn(pr.to) + " to vote for " + o.label + ". " + d.context;
                            o.detail = "Promised to " + pn(pr.to) + ". " + o.detail;
                        }
            d.options = opts;
            d.value = avail;
            if (G.modules & kModDiscordant) d.predicted = dsPredictionOf(p);  // (0.44.6: for the AI)
            std::vector<Option> copy = opts;
            d.validate = [copy](const Answer& ans) -> std::string {
                if (ans.picks.size() != 1) return "Choose one option.";
                for (const Option& o : copy)
                    if (o.id == ans.picks[0]) return "";
                return "Not one of the options.";
            };
            int o = forced != kNoVote ? forced : E.ask(std::move(d)).picks[0];
            if (o != kAbstain) {
                int count = 0;
                if (repGov) {
                    count = 1;
                } else {
                    std::vector<int> ready = ti::readyPlanets(G, p);
                    if (!ready.empty()) {
                        Decision v;
                        v.player = p;
                        v.kind = DecisionKind::Pay;
                        v.tag = Tag::VotePlanets;
                        v.agenda = a;
                        v.prompt = "Exhaust planets to cast votes for " + outcomeName(G.currentAgenda, o);
                        v.context = "Each planet gives votes equal to its influence. They are readied at the end of the agenda phase.";
                        v.planets = ready;
                        v.influence = true;
                        v.amount = 0;
                        v.allowTradeGoods = exec;
                        v.mixed = exec;
                        v.maxPick = 1;
                        v.value = o;  // the outcome these votes go to
                        if (exec)
                            v.context = "Executive Order: each planet gives its resources or its influence (the higher), "
                                        "and each trade good is 1 vote.";
                        if (forced != kNoVote) {
                            v.amount = 1;  // Genetic Recombination: at least 1 vote
                            v.context = "Genetic Recombination: cast at least 1 vote for it.";
                        }
                        std::vector<int> okPlanets = ready;
                        const Game* gpv = &G;
                        const bool mustVote = forced != kNoVote;
                        v.validate = [okPlanets, gpv, p, mustVote, exec](const Answer& ans) -> std::string {
                            if (mustVote) {
                                int n = 0;
                                for (int x : ans.picks) n += voteValue(*gpv, p, x);
                                if (exec) n += std::max(0, ans.number);
                                if (n < 1) return "Genetic Recombination: you must cast at least 1 vote.";
                            }
                            if (ans.number == -1 && ans.picks.empty()) return "";
                            if (ans.number > 0 && !exec) return "Trade goods can't be spent to vote.";
                            if (ans.number > gpv->players[p].tradeGoods) return "You don't have that many trade goods.";
                            for (int x : ans.picks)
                                if (std::find(okPlanets.begin(), okPlanets.end(), x) == okPlanets.end()) return "That planet can't vote.";
                            return "";
                        };
                        Answer ans = E.ask(std::move(v));
                        std::vector<int> spent;
                        for (int x : ans.picks) {
                            if (G.planets[x].exhausted) continue;
                            G.planets[x].exhausted = true;
                            spent.push_back(x);
                            count += exec ? std::max(voteValue(G, p, x), planetResources(G, x)) : voteValue(G, p, x);
                        }
                        planetsExhausted(p, spent);  // Arcane Citadel
                        if (G.modules & kModDiscordant) count += dsVoteBonus(p, spent);  // Control (Olradin policy)
                        if (exec && ans.number > 0) {
                            const int t = std::min<int>(ans.number, G.players[p].tradeGoods);
                            G.players[p].tradeGoods = int8_t(G.players[p].tradeGoods - t);
                            count += t;
                        }
                    }
                    // Custodia Vigilia (Keleres, I.I.H.Q. Modernization - 0.43.0 T8i): the planet card votes too (3 influence)
                    if (G.fixes >= 2 && custodiaValue(G, p, true) > 0 &&
                        yesNo(p, Tag::UseTech, "Exhaust the Custodia Vigilia card for 3 more votes?", "It counts as a planet with 3 influence.")) {
                        G.players[size_t(p)].custodiaExhausted = true;
                        count += 3;
                    }
                    // Bannerhalls (Kjalengard breakthrough; 0.44.9, fixes 13 - the owner's answer B3): planet cards, so
                    // they vote too - 1 each (their influence)
                    // (fixes 27, the Deepwrought review) ocean cards are planet cards too (1 influence each)
                    const bool oceanVotes = G.fixes >= 27 && G.players[size_t(p)].oceans > 0;
                    if (((G.fixes >= 13 && bannerhalls(G, p)) || oceanVotes) && oceansReady(G, p) > 0) {
                        std::vector<Option> bo;
                        const char* one = oceanVotes ? " ocean card" : " Bannerhall";
                        const char* many = oceanVotes ? " ocean cards" : " Bannerhalls";
                        for (int k = oceansReady(G, p); k >= 0; --k) bo.push_back({k, std::to_string(k) + (k == 1 ? one : many), ""});
                        const int use = pick(p, Tag::OceanPay, oceanVotes ? "Exhaust ocean cards for votes?" : "Exhaust Bannerhalls for votes?", bo,
                                             oceanVotes ? "Each ocean card is 1 more vote." : "Each Bannerhall is 1 more vote.");
                        if (use > 0) {
                            G.players[size_t(p)].oceansExhausted = int8_t(G.players[size_t(p)].oceansExhausted + use);
                            count += use;
                        }
                    }
                    if (pi && yesNo(p, Tag::UseTech, "Cast 3 more votes with Predictive Intelligence?",
                                    "It is exhausted if the outcome you vote for doesn't win.")) {
                        count += 3;
                        piUsed_[p] = true;
                        // (fixes 27, the shared technologies - the 3 votes left no line in the log)
                        if (G.fixes >= 27) say(p, pn(p) + " casts 3 more votes (Predictive Intelligence).");
                    }
                    count += extraVotes(p, o);  // Gila the Silvertongue
                    if (odlynn) {
                        count += 6;
                        say(p, pn(p) + " casts 6 more votes (Odlynn Myrr).");
                    }
                }
                if (count > 0 && zeal(p) > 0) {  // Zeal: 1 more vote per player in the game
                    count += zeal(p);
                    say(p, pn(p) + " casts " + std::to_string(zeal(p)) + " more votes (Zeal).");
                }
                if (count > 0) {
                    votedFor[p] = o;
                    votes[p] = count;
                    Event e;
                    e.kind = EventKind::Vote;
                    e.player = int8_t(p);
                    e.count = int16_t(count);
                    e.text = pn(p) + " casts " + std::to_string(count) + " vote" + (count > 1 ? "s" : "") + " for " +
                             outcomeName(G.currentAgenda, o) + ".";
                    if (eventOn(G, kEvHidden) && !fac(p, "argent")) {  // Hidden Agenda: only the totals, at the end
                        e.text = pn(p) + " casts their votes in secret.";
                        e.count = 0;
                    }
                    E.emit(e);
                    if (!repGov) {
                        WinCtx c;
                        c.w = Win::AfterCastVotes;
                        c.a = p;
                        window(c);
                    }
                }
            }
            if (votedFor[p] == kNoVote) say(p, pn(p) + " abstains.");
        } else if (!blocked) {
            say(p, pn(p) + " has no votes to cast.");
        } else {
            say(p, pn(p) + " may not vote on this agenda.");
        }
        promiseVote(p, G.currentAgenda, votedFor[p], votes[p]);
        if (p == G.speaker && !repGov) {
            WinCtx c;
            c.w = Win::AfterSpeakerVotes;
            window(c);
        }
    }
    if (eventOn(G, kEvHidden)) {  // Hidden Agenda: the speaker reveals the totals
        std::string totals;
        for (const Option& o : agendaOutcomes(G.currentAgenda)) {
            int t = 0;
            for (int q = 0; q < n; ++q)
                if (votedFor[q] == o.id) t += votes[q];
            if (t) totals += (totals.empty() ? "" : ", ") + o.label + " " + std::to_string(t);
        }
        say(G.speaker, "The votes are counted: " + (totals.empty() ? std::string("none") : totals) + ".");
    }
}

void Rules::agendaSecrets(int p) {
    if (G.players[p].eliminated) return;
    for (int16_t o : std::vector<int16_t>(G.players[p].secrets)) {
        if (kObjectives[o].phase != ObjPhase::Agenda) continue;
        if (!std::strcmp(kObjectives[o].key, "dp") && G.laws.size() >= 3) scoreObjective(p, o);
    }
}

// ------------------------------------------------------------------ laws
void Rules::addLaw(int a, int owner, int target) {
    Law l;
    l.agenda = int16_t(a);
    l.owner = int8_t(owner);
    l.target = int16_t(target);
    G.laws.push_back(l);
    say(owner, std::string(kAgendas[a].name) + " is now a law" + (owner >= 0 ? " held by " + pn(owner) : std::string()) + ".");
    if (agKey(a, "travel_ban") || agKey(a, "wormhole_recon") || agKey(a, "nexus")) computeAdjacency(G);
    for (int q = 0; q < G.numPlayers; ++q) agendaSecrets(q);
    // (fixes 25, owner's faction debug pass) 3 or more laws: the Firmament meets another player's Dictate Policy (after
    // everyone's own scoring, so one scored just now counts too)
    if (G.fixes >= 25 && G.laws.size() >= 3)
        for (int q = 0; q < G.numPlayers; ++q) plotsWithinPlots(q, "dp");
}

void Rules::discardLaw(int i, const std::string& why) {
    if (i < 0 || i >= int(G.laws.size())) return;
    Law l = G.laws[i];
    G.laws.erase(G.laws.begin() + i);
    G.agendaDiscard.push_back(l.agenda);
    say(-1, std::string(kAgendas[l.agenda].name) + " is discarded (" + why + ").");
    if (agKey(l.agenda, "censure") && l.owner >= 0) loseVP(l.owner, 1, "lost Political Censure");
    if (agKey(l.agenda, "travel_ban") || agKey(l.agenda, "wormhole_recon") || agKey(l.agenda, "nexus")) computeAdjacency(G);
    if (agKey(l.agenda, "classified") && l.target >= 0) {
        auto it = std::find(G.publicObjectives.begin(), G.publicObjectives.end(), l.target);
        if (it != G.publicObjectives.end()) G.publicObjectives.erase(it);
    }
}

// ------------------------------------------------------------------ resolving
void Rules::resolveOutcome(int a, int o, const std::vector<int>& votedFor, const std::vector<int>& votes) {
    const int n = G.numPlayers;
    const char* k = kAgendas[a].key;
    auto is = [&](const char* x) { return !std::strcmp(k, x); };
    const bool against = kAgendas[a].elect == Elect::ForAgainst && o == 1;
    {
        Event e;
        e.kind = EventKind::AgendaResult;
        e.card = int16_t(a);
        e.player = int8_t(kAgendas[a].elect == Elect::Player ? o : -1);
        std::string what = kAgendas[a].elect == Elect::ForAgainst ? (against ? "Against" : "For") : outcomeName(a, o);
        e.text = std::string(kAgendas[a].name) + ": " + what + " wins. " + agendaText(a, against);
        E.emit(e);
    }
    auto voters = [&](int outcome) {
        std::vector<int> v;
        for (int q = 0; q < n; ++q)
            if (int(votedFor.size()) > q && votedFor[q] == outcome && votes[q] > 0) v.push_back(q);
        return v;
    };
    // Drive the Debate: you, or a planet you control, are elected.
    auto debated = [&](int p) {
        if (p < 0) return;
        for (int16_t s : std::vector<int16_t>(G.players[p].secrets))
            if (!std::strcmp(kObjectives[s].key, "dtd")) scoreObjective(p, s);
        // (fixes 25, owner's faction debug pass) the Firmament meets another player's Drive the Debate: Plots Within Plots
        if (G.fixes >= 25 && p < G.numPlayers) plotsWithinPlots(p, "dtd");
    };
    if (kAgendas[a].elect == Elect::Player) debated(o);
    if (kAgendas[a].elect == Elect::Planet || kAgendas[a].elect == Elect::NonHomePlanet) {
        debated(G.planets[o].owner);
        // (fixes 25, owner's faction debug pass) Captain Aroz: a planet in a system with your ships counts as yours for
        // scoring secret objectives - the elected planet too
        if (G.fixes >= 25 && (G.modules & kModThundersEdge))
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != G.planets[o].owner && !G.players[size_t(q)].eliminated && commanderUsable(G, q, "firmament") &&
                    hasShips(G, q, G.planets[o].system))
                    debated(q);
    }

    // ---------------- laws
    if (kAgendas[a].law) {
        if (kAgendas[a].elect == Elect::Player) {
            addLaw(a, o, -1);
            if (is("censure")) gainVP(o, 1, "Political Censure");
            if (is("warrant")) {
                drawSecret(o);
                drawSecret(o);
            }
            return;
        }
        if (kAgendas[a].elect == Elect::ScoredSecret) {  // Classified Document Leaks
            addLaw(a, -1, o);
            G.publicObjectives.push_back(int16_t(o));
            say(-1, std::string(kObjectives[o].name) + " becomes a public objective.");
            return;
        }
        if (!against) {
            addLaw(a, -1, -1);
            if (is("regulations"))
                for (Player& pl : G.players)
                    if (pl.fleet > 4) pl.fleet = 4;
            if (is("regulations"))
                for (int q = 0; q < n; ++q) enforceFleetLimit(q);
            if (is("sanctions"))
                for (int q = 0; q < n; ++q) enforceHandLimit(q);
            return;
        }
        // Against outcomes of laws
        G.agendaDiscard.push_back(int16_t(a));
        if (is("arms_reduction") || is("rep_govt") || is("revolution")) {
            PendingEffect pe;
            pe.agenda = int16_t(a);
            pe.against = true;
            pe.players = 0xff;
            if (is("rep_govt")) {
                pe.players = 0;
                for (int q : voters(1)) pe.players = uint8_t(pe.players | (1u << q));
            }
            G.pending.push_back(pe);
        } else if (is("articles_war")) {
            for (int q : voters(0)) gainTG(q, 3);
        } else if (is("nexus")) {  // Nexus Sovereignty (Against): a gamma wormhole on Mecatol Rex
            int mec = G.mecatol();
            if (mec >= 0) {
                G.systems[mec].tokenWormholes |= kGamma;
                computeAdjacency(G);
                say(-1, "A gamma wormhole token is placed in the Mecatol Rex system.", mec);
            }
        } else if (is("checks")) {
            readyOnly3_ = true;
        } else if (is("conventions")) {
            for (int q : voters(1)) discardAllActionCards(q, "Conventions of War");
        } else if (is("defense_act")) {
            for (int q = 0; q < n; ++q) {
                std::vector<Option> opts;
                for (size_t x = 0; x < G.planets.size(); ++x)
                    if (G.planets[x].units[q].count(UnitType::PDS)) opts.push_back({int(x), "PDS on " + planetName(int(x)), ""});
                if (opts.empty()) continue;
                int x = opts.size() == 1 ? opts[0].id : pick(q, Tag::AgendaChoice, "Homeland Defense Act: destroy which PDS?", opts);
                destroyUnits(q, UnitType::PDS, 1, G.planets[x].system, x, -1, "Homeland Defense Act");
            }
        } else if (is("regulations")) {
            for (int q = 0; q < n; ++q)
                if (reinforcementTokens(q) > 0) G.players[q].fleet = int8_t(G.players[q].fleet + 1);
        } else if (is("sanctions")) {
            for (int q = 0; q < n; ++q) {
                std::vector<int16_t>& h = G.players[q].actionCards;
                if (h.empty()) continue;
                int i = int(E.rng().below(uint32_t(h.size())));
                int c = h[i];
                h.erase(h.begin() + i);
                discardActionCard(q, c, false);
            }
        } else if (is("schematics")) {
            for (int q = 0; q < n; ++q)
                if (ownsWarSunTech(G, q)) discardAllActionCards(q, "Publicize Weapon Schematics");
        } else if (is("shared_research")) {
            for (int q = 0; q < n; ++q)
                if (!G.players[q].eliminated) placeTokenFromReinforcements(q, G.players[q].homeSystem, "Shared Research");
        } else if (is("travel_ban")) {
            for (size_t s = 0; s < G.systems.size(); ++s) {
                bool near = wormholesOf(G, s) != 0;
                for (int16_t x : G.systems[s].adj)
                    if (wormholesOf(G, x)) near = true;
                if (!near) continue;
                for (int16_t x : G.systems[s].planets)
                    for (int q = 0; q < n; ++q)
                        if (G.planets[x].units[q].count(UnitType::PDS))
                            destroyUnits(q, UnitType::PDS, G.planets[x].units[q].count(UnitType::PDS), int(s), x, -1, "Enforced Travel Ban");
            }
        } else if (is("wormhole_recon")) {
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (wormholesOf(G, s))
                    for (int q = 0; q < n; ++q)
                        if (hasShips(G, q, int(s))) placeTokenFromReinforcements(q, int(s), "Wormhole Reconstruction");
        }
        return;
    }

    // ---------------- directives
    G.agendaDiscard.push_back(int16_t(a));
    if (is("abolishment")) {
        for (size_t i = 0; i < G.laws.size(); ++i)
            if (G.laws[i].agenda == o) {
                discardLaw(int(i), "Judicial Abolishment");
                break;
            }
    } else if (is("miscount")) {
        // Vote on the elected law again as if it had just been revealed.
        for (size_t i = 0; i < G.laws.size(); ++i)
            if (G.laws[i].agenda == o) {
                Law l = G.laws[i];
                G.laws.erase(G.laws.begin() + int(i));
                if (agKey(l.agenda, "censure") && l.owner >= 0) loseVP(l.owner, 1, "lost Political Censure");
                if (agKey(l.agenda, "travel_ban") || agKey(l.agenda, "wormhole_recon") || agKey(l.agenda, "nexus")) computeAdjacency(G);
                if (agKey(l.agenda, "classified") && l.target >= 0) {
                    auto it = std::find(G.publicObjectives.begin(), G.publicObjectives.end(), l.target);
                    if (it != G.publicObjectives.end()) G.publicObjectives.erase(it);
                }
                runAgenda(o, true);
                break;
            }
    } else if (is("arms_reduction")) {
        if (!against) {
            for (int q = 0; q < n; ++q) {
                for (auto lim : {std::make_pair(UnitType::Dreadnought, 2), std::make_pair(UnitType::Cruiser, 4)}) {
                    int extra = unitsOnBoard(G, q, lim.first) - lim.second;
                    if (extra <= 0) continue;
                    std::vector<Option> opts;
                    for (size_t s = 0; s < G.systems.size(); ++s)
                        for (int i = 0; i < G.systems[s].space[q].count(lim.first); ++i)
                            opts.push_back({int(s) * 16 + i, std::string(unitTypeName(lim.first)) + " in " + sysName(int(s)), ""});
                    std::vector<int> picks = pickMany(q, Tag::AgendaChoice,
                                                      std::string("Arms Reduction: destroy ") + std::to_string(extra) + " " +
                                                          unitTypeName(lim.first, extra > 1),
                                                      opts, extra, extra);
                    for (int id : picks) destroyUnits(q, lim.first, 1, id / 16, -1, -1, "Arms Reduction");
                }
            }
        } else {
            PendingEffect pe;
            pe.agenda = int16_t(a);
            pe.against = true;
            pe.players = 0xff;
            G.pending.push_back(pe);
        }
    } else if (is("artifact")) {
        if (!against) {
            int v = E.rng().d10();
            say(G.speaker, pn(G.speaker) + " rolls " + std::to_string(v) + " for the Ixthian Artifact.");
            if (v >= 6) {
                for (int q : clockwiseFrom(G, G.speaker)) {
                    researchStep(q, 0, "Ixthian Artifact");
                    researchStep(q, 0, "Ixthian Artifact");
                }
            } else {
                int mec = G.mecatol();
                for (int q = 0; q < n; ++q) {
                    for (int t = 0; t < kUnitTypes; ++t)
                        if (G.systems[mec].space[q].n[t]) destroyUnits(q, UnitType(t), G.systems[mec].space[q].n[t], mec, -1, -1, "Ixthian Artifact");
                    for (int16_t x : G.systems[mec].planets)
                        for (int t = 0; t < kUnitTypes; ++t)
                            if (G.planets[x].units[q].n[t]) destroyUnits(q, UnitType(t), G.planets[x].units[q].n[t], mec, x, -1, "Ixthian Artifact");
                }
                // Each player chooses 3 of their units to destroy in each system next to
                // Mecatol Rex (owner confirmed: the player's choice, RULES-CHECK 0.4.0 #6).
                static const UnitType order[] = {UnitType::Fighter, UnitType::Infantry, UnitType::Destroyer, UnitType::Mech,
                                                 UnitType::Cruiser, UnitType::PDS, UnitType::Carrier, UnitType::Dreadnought,
                                                 UnitType::SpaceDock, UnitType::Flagship, UnitType::Flagship2, UnitType::WarSun};
                for (int16_t s : G.systems[mec].adj) {
                    const SystemState& sy = G.systems[s];
                    for (int q : clockwiseFrom(G, G.speaker)) {
                        // locations: 0 = space area, 1.. = planets of the system
                        std::vector<Option> opts;
                        int total = 0;
                        for (UnitType t : order) {
                            for (int loc = 0; loc <= int(sy.planets.size()); ++loc) {
                                const Force& f = loc == 0 ? sy.space[q] : G.planets[sy.planets[loc - 1]].units[q];
                                int k = f.count(t);
                                total += k;
                                for (int i = 0; i < k && i < 3; ++i)
                                    opts.push_back({loc * 64 + int(t) * 4 + i,
                                                    std::string(unitTypeName(t)) +
                                                        (loc == 0 ? " (space)" : " on " + planetName(sy.planets[loc - 1])),
                                                    ""});
                            }
                        }
                        if (total == 0) continue;
                        std::vector<int> picks;
                        if (total <= 3) {
                            for (const Option& o : opts) picks.push_back(o.id);
                        } else {
                            picks = pickMany(q, Tag::AgendaChoice, "Ixthian Artifact: destroy 3 of your units in " + sysName(s), opts, 3, 3,
                                             "The artifact failed: every player loses 3 units in each system next to Mecatol Rex.");
                        }
                        int kill[4][kUnitTypes] = {};
                        for (int id : picks) ++kill[id / 64][(id % 64) / 4];
                        for (int loc = 0; loc <= int(sy.planets.size()) && loc < 4; ++loc)
                            for (int t = 0; t < kUnitTypes; ++t)
                                if (kill[loc][t])
                                    destroyUnits(q, UnitType(t), kill[loc][t], s, loc == 0 ? -1 : sy.planets[loc - 1], -1, "Ixthian Artifact");
                        removeExcessCapacity(q, s);
                    }
                }
                checkEliminations();
            }
        }
    } else if (is("cladenstine")) {
        for (int q = 0; q < n; ++q) {
            Player& pl = G.players[q];
            if (pl.eliminated) continue;
            if (!against) {
                for (int i = 0; i < 2 && pl.tactic + pl.fleet + pl.strategy > 0; ++i) {
                    std::vector<Option> opts;
                    if (pl.tactic) opts.push_back({0, "Tactic pool", std::to_string(pl.tactic) + " tokens"});
                    if (pl.fleet) opts.push_back({1, "Fleet pool", std::to_string(pl.fleet) + " tokens"});
                    if (pl.strategy) opts.push_back({2, "Strategy pool", std::to_string(pl.strategy) + " tokens"});
                    int c = opts.size() == 1 ? opts[0].id : pick(q, Tag::TokenFromPool, "Clandestine Operations: remove a token from which pool?", opts);
                    if (c == 0) --pl.tactic;
                    else if (c == 1) --pl.fleet;
                    else --pl.strategy;
                }
            } else if (pl.fleet > 0) {
                --pl.fleet;
            }
            enforceFleetLimit(q);
        }
    } else if (is("constitution")) {
        if (!against) {
            while (!G.laws.empty()) discardLaw(int(G.laws.size()) - 1, "New Constitution");
            PendingEffect pe;
            pe.agenda = int16_t(a);
            pe.players = 0xff;
            G.pending.push_back(pe);
        }
    } else if (is("crisis")) {
        for (int q : clockwiseFrom(G, G.speaker)) secondaryForFree(q, o);
    } else if (is("disarmament")) {
        int owner = G.planets[o].owner;
        int gained = 0;
        for (int q = 0; q < n; ++q)
            for (UnitType t : {UnitType::Infantry, UnitType::Mech}) {
                int k = G.planets[o].units[q].count(t);
                if (k) {
                    destroyUnits(q, t, k, G.planets[o].system, o, -1, "Compensated Disarmament");
                    gained += k;
                }
            }
        if (owner >= 0) gainTG(owner, gained);
    } else if (is("economic_equality")) {
        for (Player& pl : G.players) pl.tradeGoods = int8_t(against ? 0 : 5);
    } else if (is("execution")) {
        discardAllActionCards(o, "Public Execution");
        if (G.speaker == o) {
            std::vector<int> left = clockwiseFrom(G, (o + 1) % n);
            if (!left.empty() && left[0] != o) G.speaker = int8_t(left[0]);
            say(G.speaker, pn(G.speaker) + " becomes the speaker.");
        }
        G.noVote = uint8_t(G.noVote | (1u << o));
    } else if (is("grant_reallocation")) {
        std::vector<Option> opts;
        for (int t = 0; t < kTechCount; ++t)
            if (techAvailableToResearch(G, o, t) && (kTechs[t].source == Source::Base || kTechs[t].source == Source::PoK))
                opts.push_back({t, kTechs[t].name, techSummary(t)});
        if (!opts.empty()) {
            int t = pick(o, Tag::AgendaChoice, "Research Grant Reallocation: gain which technology?", opts,
                         "You lose a fleet token for each of its prerequisites.");
            researchTech(o, t, false);
            E.log(o, pn(o) + " gains " + kTechs[t].name + ".", EventKind::Research);
            int pre = kTechs[t].req[0] + kTechs[t].req[1] + kTechs[t].req[2] + kTechs[t].req[3];
            Player& pl = G.players[o];
            pl.fleet = int8_t(std::max(0, pl.fleet - pre));
            enforceFleetLimit(o);
        }
    } else if (is("incentive")) {
        std::vector<int16_t>& rest = against ? G.stage2Rest : G.stage1Rest;
        if (!rest.empty()) {
            int ob = rest.back();
            rest.pop_back();
            G.publicObjectives.push_back(int16_t(ob));
            E.log(-1, std::string("New public objective: ") + kObjectives[ob].name + " (" + std::to_string(kObjectives[ob].points) +
                          " VP) - " + objectiveSummary(ob),
                  EventKind::Phase);
        }
    } else if (is("mutiny")) {
        for (int q : voters(0)) {
            if (!against) gainVP(q, 1, "Mutiny");
            else loseVP(q, 1, "Mutiny");
        }
    } else if (is("plowshares")) {
        for (int q = 0; q < n; ++q) {
            if (!against) {
                int lost = 0;
                for (int x : controlledPlanets(G, q)) {
                    int inf = G.planets[x].units[q].count(UnitType::Infantry);
                    int k = (inf + 1) / 2;
                    if (k) destroyUnits(q, UnitType::Infantry, k, G.planets[x].system, x, -1, "Swords to Plowshares");
                    lost += k;
                }
                gainTG(q, lost);
            } else {
                for (int x : controlledPlanets(G, q)) addUnits(q, UnitType::Infantry, 1, G.planets[x].system, x);
            }
        }
    } else if (is("rearmament")) {
        for (int q = 0; q < n; ++q) {
            if (G.players[q].eliminated) continue;
            if (!against) {
                if (unitsAvailable(G, q, UnitType::Mech) <= 0) continue;
                std::vector<Option> opts;
                for (int16_t x : G.systems[G.players[q].homeSystem].planets)
                    if (G.planets[x].owner == q) opts.push_back({x, planetName(x), ""});
                if (opts.empty()) continue;
                int x = opts.size() == 1 ? opts[0].id : pick(q, Tag::AgendaChoice, "Rearmament Agreement: place a mech on which home planet?", opts);
                addUnits(q, UnitType::Mech, 1, G.planets[x].system, x);
            } else {
                for (size_t x = 0; x < G.planets.size(); ++x) {
                    int k = G.planets[x].units[q].count(UnitType::Mech);
                    if (!k) continue;
                    G.planets[x].units[q].remove(UnitType::Mech, k, true);
                    G.planets[x].units[q].add(UnitType::Infantry, k);
                }
                for (size_t s = 0; s < G.systems.size(); ++s) {
                    int k = G.systems[s].space[q].count(UnitType::Mech);
                    if (!k) continue;
                    G.systems[s].space[q].remove(UnitType::Mech, k, true);
                    G.systems[s].space[q].add(UnitType::Infantry, k);
                }
            }
        }
    } else if (is("redistribution")) {
        int owner = G.planets[o].owner;
        int sys = G.planets[o].system;
        for (int q = 0; q < n; ++q)
            for (int t = 0; t < kUnitTypes; ++t)
                if (G.planets[o].units[q].n[t]) destroyUnits(q, UnitType(t), G.planets[o].units[q].n[t], sys, o, -1, "Colonial Redistribution");
        // RULES-CHECK D135 (0.42.2): units placed on the planet by a "when destroyed" ability (Nauplius, the Cheiran
        // mech) stay; with ground forces already there nobody else may place an infantry (two armies would share it)
        int already = -1;
        for (int q = 0; q < n; ++q)
            if (G.planets[o].units[q].groundForces() > 0) already = q;
        if (already >= 0) {
            say(already, planetName(o) + " isn't empty (" + pn(already) + "'s ground forces) - no infantry is placed (Colonial Redistribution).");
            owner = -1;
        }
        if (owner >= 0) {
            int low = 99;
            for (int q = 0; q < n; ++q)
                if (!G.players[q].eliminated) low = std::min<int>(low, G.players[q].vp);
            std::vector<Option> opts;
            for (int q = 0; q < n; ++q)
                if (!G.players[q].eliminated && G.players[q].vp == low) opts.push_back({q, pn(q), std::to_string(low) + " victory points"});
            int who = opts.size() == 1 ? opts[0].id : pick(owner, Tag::AgendaChoice, "Colonial Redistribution: which player may place an infantry?", opts);
            if (yesNo(who, Tag::AgendaChoice, "Place 1 infantry on " + planetName(o) + "?", "Colonial Redistribution.")) {
                addUnits(who, UnitType::Infantry, 1, sys, o);
                if (G.planets[o].owner != who) setControl(o, who);
            }
        }
        checkEliminations();
    } else if (is("secret")) {
        drawSecret(o);
    } else if (is("minister_antiquities")) {
        gainRelic(o, "Minister of Antiquities");
    } else if (is("seed_empire")) {
        int target = against ? 99 : -1;
        for (int q = 0; q < n; ++q)
            if (!G.players[q].eliminated)
                target = against ? std::min<int>(target, G.players[q].vp) : std::max<int>(target, G.players[q].vp);
        for (int q : initiativeOrder(G))
            if (G.players[q].vp == target) gainVP(q, 1, "Seed of an Empire");
    } else if (is("standardization")) {
        Player& pl = G.players[o];
        int total = kCommandTokens - tokensOnBoard(o);
        pl.tactic = int8_t(std::min(3, total));
        pl.fleet = int8_t(std::min(3, total - pl.tactic));
        pl.strategy = int8_t(std::min(2, total - pl.tactic - pl.fleet));
        enforceFleetLimit(o);
        say(o, pn(o) + "'s command sheet is reset to 3 / 3 / 2.");
    } else if (is("unconventional")) {
        for (int q : voters(0)) {
            if (!against) drawActionCards(q, 2, "Unconventional Measures");
            else discardAllActionCards(q, "Unconventional Measures");
        }
    } else if (is("wormhole_research")) {
        if (!against) {
            for (int q : clockwiseFrom(G, G.speaker)) {
                bool at = false;
                for (size_t s = 0; s < G.systems.size(); ++s)
                    if (wormholesOf(G, int(s)) && G.systems[s].space[q].ships()) at = true;
                if (at) researchStep(q, 0, "Wormhole Research");
            }
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (wormholesOf(G, s) & (kAlpha | kBeta))
                    for (int q = 0; q < n; ++q) {
                        bool hit = false;
                        for (int t : kShipIdx)
                            if (G.systems[s].space[q].n[t]) {
                                destroyUnits(q, UnitType(t), G.systems[s].space[q].n[t], int(s), -1, -1, "Wormhole Research");
                                hit = true;
                            }
                        if (hit) removeExcessCapacity(q, int(s));  // ground forces left in space with no ships (0.21.1 audit)
                    }
            checkEliminations();
        } else {
            for (int q : voters(1)) {  // the player chooses the pool (0.21.1 audit)
                Player& pl = G.players[q];
                std::vector<Option> opts;
                if (pl.tactic) opts.push_back({0, "Tactic pool", std::to_string(pl.tactic) + " tokens"});
                if (pl.fleet) opts.push_back({1, "Fleet pool", std::to_string(pl.fleet) + " tokens"});
                if (pl.strategy) opts.push_back({2, "Strategy pool", std::to_string(pl.strategy) + " tokens"});
                if (opts.empty()) continue;
                int c = opts.size() == 1 ? opts[0].id : pick(q, Tag::TokenFromPool, "Wormhole Research: remove a token from which pool?", opts);
                if (c == 0) --pl.tactic;
                else if (c == 1) --pl.fleet;
                else --pl.strategy;
                enforceFleetLimit(q);
            }
        }
    }
}

// Galactic Crisis Pact: everyone may use the elected card's secondary for free.
void Rules::secondaryForFree(int p, int card) {
    if (G.players[p].eliminated || card == 0) {
        if (card == 0 && !G.players[p].eliminated) leadership(p, false);
        return;
    }
    if (!yesNo(p, Tag::SecondaryUse, std::string("Galactic Crisis Pact: use the secondary of ") + strategyName(card) + " for free?",
               strategySecondary(card)))
        return;
    freeSecondary_ = true;
    switch (card) {
        case 1: diplomacy(p, false); break;
        case 2:
            politics(p, false);
            if (G.modules & kModDiscordant) dsAfterPolitics(p);  // Contagion (Kyro)
            break;
        case 3: construction(p, false); break;
        case 4: trade(p, false); break;
        case 5: warfare(p, false); break;
        case 6: technology(p, false); break;
        case 7: imperial(p, false); break;
        default: break;
    }
    freeSecondary_ = false;
}

// ------------------------------------------------------------------ start of the strategy phase
void Rules::applyPendingEffects() {
    std::vector<PendingEffect> list;
    list.swap(G.pending);
    for (const PendingEffect& pe : list) {
        const char* k = kAgendas[pe.agenda].key;
        for (int q = 0; q < G.numPlayers; ++q) {
            if (!((pe.players >> q) & 1) || G.players[q].eliminated) continue;
            if (!std::strcmp(k, "arms_reduction")) {
                for (int x : controlledPlanets(G, q))
                    if (planetHasSkip(G, x)) G.planets[x].exhausted = true;
            } else if (!std::strcmp(k, "constitution")) {
                for (int16_t x : G.systems[G.players[q].homeSystem].planets)
                    if (G.planets[x].owner == q) G.planets[x].exhausted = true;
            } else if (!std::strcmp(k, "rep_govt")) {
                for (int x : controlledPlanets(G, q))
                    if (hasTraitOf(G, x, Trait::Cultural)) G.planets[x].exhausted = true;
            } else if (!std::strcmp(k, "revolution")) {
                std::vector<Option> opts;
                for (int x : controlledPlanets(G, q))
                    if (!G.planets[x].exhausted) opts.push_back({x, planetName(x), ""});
                int need = std::min<int>(int(G.players[q].techs.size()), int(opts.size()));
                if (need <= 0) continue;
                std::vector<int> picks = need == int(opts.size()) ? std::vector<int>()
                                                                  : pickMany(q, Tag::AgendaChoice,
                                                                             "Anti-Intellectual Revolution: exhaust " +
                                                                                 std::to_string(need) + " planets",
                                                                             opts, need, need, "", true);
                if (need == int(opts.size()))
                    for (const Option& o : opts) picks.push_back(o.id);
                for (int x : picks) G.planets[x].exhausted = true;
                planetsExhausted(q, picks);
            }
        }
        say(-1, std::string(kAgendas[pe.agenda].name) + " takes effect.");
    }
}

// ------------------------------------------------------------------ Politics
void Rules::politicsPeek(int p) {
    if (G.agendaDeck.size() < 2 && !G.agendaDiscard.empty()) {
        for (int16_t x : G.agendaDiscard) G.agendaDeck.insert(G.agendaDeck.begin(), x);
        G.agendaDiscard.clear();
    }
    int k = std::min<int>(2, int(G.agendaDeck.size()));
    if (k <= 0) return;
    E.revealed();
    std::vector<int> top;
    for (int i = 0; i < k; ++i) {
        top.push_back(G.agendaDeck.back());
        G.agendaDeck.pop_back();
    }
    // The human player sees each card in full (it waits for A) before choosing (owner, 0.8.0 feedback).
    if (G.players[p].human)
        for (int i = 0; i < k; ++i) {
            Event e;
            e.kind = EventKind::AgendaPeek;
            e.player = int8_t(p);
            e.card = int16_t(top[i]);
            e.label = "Politics: agenda " + std::to_string(i + 1) + " of " + std::to_string(k) + " from the top of the deck";
            E.emit(e);
        }
    std::vector<int> toTop, toBottom;
    for (int a : top) {
        int prev = curAgenda_;
        curAgenda_ = a;
        int c = pick(p, Tag::AgendaPeek, std::string("Politics: put ") + kAgendas[a].name + " on top or bottom of the agenda deck?",
                     {{1, "Top", "It will be voted on soon."}, {0, "Bottom", "It won't come up for a long time."}},
                     std::string(kAgendas[a].law ? "Law. " : "Directive. ") + agendaText(a, false) +
                         (kAgendas[a].elect == Elect::ForAgainst ? std::string(" / Against: ") + agendaText(a, true) : ""));
        curAgenda_ = prev;
        (c ? toTop : toBottom).push_back(a);
    }
    for (int a : toBottom) G.agendaDeck.insert(G.agendaDeck.begin(), int16_t(a));
    for (int i = int(toTop.size()) - 1; i >= 0; --i) G.agendaDeck.push_back(int16_t(toTop[i]));
    say(p, pn(p) + " looks at the top of the agenda deck.");
}

}  // namespace ti
