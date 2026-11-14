// Negotiations (Stage 6): deals between players.
//
// On their turn in the action phase a player may "Negotiate" (it doesn't use
// their action): pick a player, compose an offer, and the other side accepts,
// declines or counters once. Components change hands at once (a transaction,
// LRR 94: only with neighbours, once per pair per turn); promises are recorded
// and watched but never forced (LRR 28.4). In the agenda phase everyone may
// negotiate with anyone before each vote. Offers are limited to 2 per pair
// per turn / agenda (declined ones count).
#include <algorithm>
#include <cstring>
#include <set>

#include "engine/deal.h"
#include "engine/rules.h"

namespace ti {

namespace {
void dealEvent(Engine& E, int type, int p, int other, const std::string& text) {
    Event e;
    e.kind = EventKind::Deal;
    e.card = int16_t(type);
    e.player = int8_t(p);
    e.other = int8_t(other);
    e.text = text;
    E.emit(std::move(e));
}
}  // namespace

void Rules::resetDealCounters() {
    std::memset(G.offers, 0, sizeof G.offers);
    std::memset(G.transactions, 0, sizeof G.transactions);
}

void Rules::markHostile(int a, int b) {
    if (a < 0 || b < 0 || a >= 8 || b >= 8 || a == b) return;
    G.hostile |= (uint64_t(1) << (a * 8 + b)) | (uint64_t(1) << (b * 8 + a));
}

void Rules::clearHostile(int a, int b) {
    if (a < 0 || b < 0 || a >= 8 || b >= 8) return;
    G.hostile &= ~((uint64_t(1) << (a * 8 + b)) | (uint64_t(1) << (b * 8 + a)));
}

std::vector<Option> Rules::negotiationPartners(int p) const {
    std::vector<Option> opts;
    if (p < 0 || G.players[p].eliminated) return opts;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p || !canOffer(G, p, q)) continue;
        std::string detail = transactionAllowed(G, p, q) ? "Trade components and make promises" : "Promises only (no trading now)";
        int left = offersLeft(G, p, q);
        detail += " \xC2\xB7 " + std::to_string(left) + (left == 1 ? " offer" : " offers") + " left";
        opts.push_back({q, pn(q), detail});
    }
    return opts;
}

bool Rules::canNegotiate(int p) const { return !negotiationPartners(p).empty(); }

int Rules::dealFlagsFor(int p, int q) const {
    int f = 0;
    if (transactionAllowed(G, p, q)) f |= kDealComponents;
    if (actionCardsTradeable(G, p, q)) f |= kDealActionCards;
    if (blackMarketPair(G, p, q)) f |= kDealBlackMarket | kDealActionCards;  // Black Market Dealings
    if (encryptedHubOf(G, p, q) >= 0 && (f & kDealComponents)) f |= kDealHub;  // Encrypted Trade Hub (0.44.5): 1 relic
    if (G.currentAgenda >= 0) f |= kDealVote;  // (also an Executive Order vote)
    if (hireCtx_.nokar >= 0 && ((p == hireCtx_.nokar && q == hireCtx_.buyer) || (q == hireCtx_.nokar && p == hireCtx_.buyer)))
        f |= kDealHire;  // Hired Guns (0.43.0): right after the buyer activated a system
    return f;
}

// Ask p to compose an offer to q (base: the offer being countered). False = cancelled.
bool Rules::askDeal(int p, int q, Tag tag, const Deal* base, Deal& out) {
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Deal;
    d.tag = tag;
    d.targetPlayer = q;
    d.agenda = G.currentAgenda;
    d.dealFlags = dealFlagsFor(p, q) | (base ? kDealCounter : 0);
    if (base) d.deal = encodeDeal(*base);
    else if (prefill_) d.deal = encodeDeal(*prefill_);  // (Hired Guns: the offer starts with the ships in it)
    if (d.dealFlags & kDealHire) d.value = hireCtx_.max;
    const int hireFrom = (d.dealFlags & kDealHire) ? hireCtx_.nokar : -1, hireMax = hireCtx_.max;
    d.prompt = base ? "Counter-offer to " + pn(q) : "Offer to " + pn(q);
    d.context = (d.dealFlags & kDealComponents) ? "You can trade components and make promises."
                                                : "Promises only: you can't trade components with " + pn(q) + " right now.";
    const Game& g = G;
    d.validate = [&g, p, q, hireFrom, hireMax](const Answer& a) -> std::string {
        if (a.data.empty()) return "";  // cancelled
        Deal x;
        if (!decodeDeal(a.data, x)) return "That offer can't be read.";
        if (x.from != p || x.to != q) return "Wrong players.";
        // Hired Guns: only the Nokar hire out ships, only right after an activation, at most what they offered
        if ((x.give.hire && (hireFrom != x.from || x.give.hire > hireMax)) || (x.get.hire && (hireFrom != x.to || x.get.hire > hireMax)))
            return "Hired Guns ships can only come from the Nokar, right after a system is activated.";
        return checkDeal(g, x);
    };
    Answer a = E.ask(std::move(d));
    if (a.data.empty()) return false;
    decodeDeal(a.data, out);
    return true;
}

// q answers the offer: 1 accept, 0 decline, 2 counter.
int Rules::askResponse(int q, const Deal& deal, bool allowCounter) {
    Decision d;
    d.player = q;
    d.kind = DecisionKind::Pick;
    d.tag = Tag::DealResponse;
    d.targetPlayer = deal.from;
    d.agenda = G.currentAgenda;
    d.deal = encodeDeal(deal);
    d.dealFlags = dealFlagsFor(q, deal.from);
    d.prompt = pn(deal.from) + (deal.threat >= 0 ? " makes a demand" : " offers a deal");
    d.context = dealText(G, deal);
    d.options = {{1, "Accept", "The deal is made now."}, {0, "Decline", "No deal."}};
    if (deal.get.note >= 0) {  // they asked for a note we may no longer hold
        const auto& h = G.players[q].notes;
        if (std::find(h.begin(), h.end(), int16_t(deal.get.note)) == h.end()) {
            d.options[0].enabled = false;
            d.options[0].detail = "You don't hold " + noteNameOf(G, deal.get.note) + " any more.";
        }
    }
    if (allowCounter) d.options.push_back({2, "Counter", "Propose different terms."});
    std::vector<Option> copy = d.options;
    d.validate = [copy](const Answer& a) -> std::string {
        if (a.picks.size() != 1) return "Choose one option.";
        for (const Option& o : copy)
            if (o.id == a.picks[0]) return o.enabled ? "" : o.detail;
        return "Not one of the options.";
    };
    return E.ask(std::move(d)).picks[0];
}

void Rules::recordDeal(const Deal& d, int result) {
    DealRecord r;
    r.round = int8_t(G.round);
    r.from = int8_t(d.from);
    r.to = int8_t(d.to);
    r.result = uint8_t(result);
    r.contract = d.contract();
    r.text = dealText(G, d);
    G.deals.push_back(r);
}

void Rules::negotiate(int p, int partner) {
    int q = partner;
    if (q < 0) {
        std::vector<Option> opts = negotiationPartners(p);
        if (opts.empty()) return;
        opts.push_back({-1, G.phase == Phase::Agenda ? "Done - go to the vote" : "Done", ""});
        q = pick(p, Tag::Negotiate, G.phase == Phase::Agenda ? "Negotiate before the vote?" : "Negotiate with whom?", opts,
                 G.phase == Phase::Agenda ? "Everyone can trade before a vote. Up to 2 offers to each player per agenda."
                                          : "Deals don't use your action. Up to 2 offers to each player per turn.");
        if (q < 0) return;
    }
    if ((G.modules & kModThundersEdge) && transactionAllowed(G, p, q)) {  // Black Market Dealings (Thunder's Edge)
        WinCtx w;
        w.w = Win::DealNegotiation;
        w.a = p;
        w.b = q;
        window(w);
    }
    negotiateWith(p, q);
    G.blackMarket[0] = G.blackMarket[1] = -1;
}

void Rules::negotiateWith(int p, int q) {
    Deal d;
    if (!askDeal(p, q, Tag::DealOffer, nullptr, d)) return;
    ++G.offers[p][q];
    dealEvent(E, kDealOffered, p, q, pn(p) + " offers " + pn(q) + " a deal: " + dealText(G, d));
    persOffered(d);
    int r = askResponse(q, d, true);
    if (r == 2) {
        Deal c;
        Deal base = d;
        if (askDeal(q, p, Tag::DealCounter, &base, c)) {
            dealEvent(E, kDealCountered, q, p, pn(q) + " counters: " + dealText(G, c));
            int r2 = askResponse(p, c, false);
            finishDeal(c, r2, false);
            if (r2 != 1) refusedEffects(d);  // the original demand was not met either
            return;
        }
        r = 0;
    }
    finishDeal(d, r, true);
}

void Rules::finishDeal(const Deal& d, int r, bool original) {
    if (r == 1) {
        std::string why = checkDeal(G, d);
        if (why.empty() && executeDeal(d)) {
            recordDeal(d, 1);
            return;
        }
        recordDeal(d, 3);
        dealEvent(E, kDealFailed, d.to, d.from, "The deal between " + pn(d.from) + " and " + pn(d.to) + " can't be made" +
                                       (why.empty() ? "." : ": " + why));
        return;
    }
    recordDeal(d, 2);
    persDealRefused(d);
    dealEvent(E, kDealDeclined, d.to, d.from, pn(d.to) + " declines " + pn(d.from) + "'s offer.");
    if (original) refusedEffects(d);
}

// A refused "leave Mecatol Rex / the Nexus" request or threat: the asker may attack.
void Rules::refusedEffects(const Deal& d) {
    int target = -1, maxPct = 0;
    for (const Promise& p : d.promises)
        if (p.kind == PromiseKind::LeaveSystem && p.by == d.to) {
            target = p.target;
            maxPct = p.target == G.nexus ? 25 : 100;  // the owner's odds: 0-100% for Mecatol, 0-25% for the Nexus
        }
    if (d.threat >= 0) {
        target = d.threat;
        maxPct = 100;
    }
    if (target < 0) return;
    int pct = E.rng().below(maxPct + 1);
    if (E.rng().below(100) < pct) {
        G.urgeAttack[d.from] = int8_t(target);
        G.urgeRound[d.from] = int8_t(G.round + 1);
    }
}

bool Rules::executeDeal(const Deal& d) {
    const int a = d.from, b = d.to;
    // the requested note must really be in hand
    for (const auto* side : {&d.give, &d.get}) {
        int who = side == &d.give ? a : b;
        if (side->note >= 0) {
            const auto& h = G.players[who].notes;
            if (std::find(h.begin(), h.end(), int16_t(side->note)) == h.end()) return false;
        }
    }
    // Encrypted Trade Hub (0.44.5): a relic other than The Silver Flame without Black Market Dealings - its owner
    // exhausts it
    if (!blackMarketPair(G, a, b)) {
        bool relic = false;
        for (const DealSide* x : {&d.give, &d.get})
            for (int y : x->relics) relic = relic || (y >= 0 && y < kRelicCount && std::strcmp(kRelics[y].key, "thesilverflame"));
        const int hub = relic ? encryptedHubOf(G, a, b) : -1;
        if (hub >= 0) {
            exhaustTech(hub, "dsedyny");
            say(hub, pn(hub) + " exhausts Encrypted Trade Hub: a relic changes hands.");
        }
    }
    dealInProgress_ = true;
    auto transfer = [&](const DealSide& s, int from, int to) {
        Player& f = G.players[from];
        if (s.tg) {
            f.tradeGoods = int8_t(f.tradeGoods - s.tg);
            tgFromPlayer_ = true;
            gainTG(to, s.tg);
            tgFromPlayer_ = false;
        }
        if (s.comm) {  // commodities become trade goods for whoever receives them
            f.commodities = int8_t(f.commodities - s.comm);
            tgFromPlayer_ = true;
            gainTG(to, s.comm);
            tgFromPlayer_ = false;
            G.players[to].commTg = int16_t(G.players[to].commTg + s.comm);
            // Dark Pact (Empyrean note in the giver's play area): as many as the giver's commodity value -> both gain 1
            if (fac(to, "empyrean") && s.comm >= commodityValue(G, f))
                for (int16_t n : f.notesInPlay)
                    if (noteOwner(n) == to && noteType(n) == kNoteFaction) {
                        gainTG(from, 1);
                        gainTG(to, 1);
                        say(from, pn(from) + " and " + pn(to) + " each gain 1 trade good (Dark Pact).");
                        break;
                    }
        }
        if (s.note >= 0) {
            giveNote(from, to, s.note);
            G.dealNotes.push_back({int16_t(s.note), int8_t(to)});
        }
        for (int fr : s.fragments) {
            auto it = std::find(f.fragments.begin(), f.fragments.end(), int16_t(fr));
            if (it == f.fragments.end()) continue;
            f.fragments.erase(it);
            G.players[to].fragments.push_back(int16_t(fr));
        }
        for (int c : s.actionCards) {
            auto it = std::find(f.actionCards.begin(), f.actionCards.end(), int16_t(c));
            if (it == f.actionCards.end()) continue;
            f.actionCards.erase(it);
            G.players[to].actionCards.push_back(int16_t(c));
        }
        for (int r : s.relics) {  // Thunder's Edge: relics change hands (ready or exhausted as they were)
            auto it = std::find(f.relics.begin(), f.relics.end(), int16_t(r));
            if (it == f.relics.end()) continue;
            f.relics.erase(it);
            const bool ex = f.relicExhausted(r);
            auto e = std::find(f.exhaustedRelics.begin(), f.exhaustedRelics.end(), int16_t(r));
            if (e != f.exhaustedRelics.end()) f.exhaustedRelics.erase(e);
            G.players[to].relics.push_back(int16_t(r));
            if (ex) G.players[to].exhaustedRelics.push_back(int16_t(r));
            say(to, pn(to) + " receives " + kRelics[r].name + " from " + pn(from) + ".");
        }
        for (int o : s.orders)  // Arms Dealers (Discordant Stars): the Axis Order changes hands
            if (o >= 0 && o < 8 && G.dsOrders[o] == from) G.dsOrders[o] = int8_t(to);
        if (s.debt > 0) {  // Binding Debts (Discordant Stars): control tokens onto the Vaden's sheet
            G.dsDebt[from] = int8_t(std::min(6, G.dsDebt[from] + s.debt));
            say(to, pn(from) + " puts " + std::to_string(s.debt) + (s.debt == 1 ? " control token" : " control tokens") + " on " + pn(to) + "'s sheet.");
        }
        for (int t : s.techs)  // Age of Commerce: the receiver gains it too
            if (!G.players[to].hasTech(t)) {
                researchTech(to, t, false);
                say(to, pn(to) + " gains " + kTechs[t].name + " from " + pn(from) + " (Age of Commerce).");
            }
        for (int o : s.secrets) {  // ... and unscored secret objectives (Black Market Dealings)
            auto it = std::find(f.secrets.begin(), f.secrets.end(), int16_t(o));
            if (it == f.secrets.end()) continue;
            f.secrets.erase(it);
            G.players[to].secrets.push_back(int16_t(o));
            E.revealed();
        }
        if (s.planet >= 0) {  // Pride of Kenara: the Hacan units go to another Hacan planet, the planet changes hands
            int dest = -1;
            std::vector<Option> spots;  // (fixes 26, the Hacan review: "move all of your units from this planet to another
                                        // planet you control" - the Hacan player chooses which)
            for (int x : controlledPlanets(G, from))
                if (x != s.planet && !isDMZ(G, x)) {
                    spots.push_back({x, planetName(x), sysName(G.planets[x].system)});
                    if (dest < 0 || !isHomePlanet(G, dest)) dest = x;  // (the old choice: a home planet, else the last)
                }
            if (G.fixes >= 26 && spots.size() > 1 && G.planets[s.planet].units[from].groundForces() + G.planets[s.planet].units[from].count(UnitType::Fighter) > 0)
                dest = fpick(from, Ab::PrideOfKenara, "Pride of Kenara: move your units on " + planetName(s.planet) + " to which planet?", spots,
                             unitAbilityText(G.players[from].unit[int(UnitType::Mech)]), false, true);
            PlanetState& ps = G.planets[s.planet];
            if (dest >= 0)
                for (int t = 0; t < kUnitTypes; ++t)
                    if (ps.units[from].n[t] && !isStructure(UnitType(t))) {
                        int k = ps.units[from].n[t];
                        ps.units[from].remove(UnitType(t), k, false);
                        G.planets[dest].units[from].add(UnitType(t), k);
                    }
            for (int t = 0; t < kUnitTypes; ++t)  // structures can't move: they go back to reinforcements
                if (ps.units[from].n[t]) ps.units[from].remove(UnitType(t), ps.units[from].n[t], false);
            setControl(s.planet, to);
            say(to, pn(to) + " gains " + planetName(s.planet) + " from " + pn(from) + " (Pride of Kenara).");
        }
        for (int c : s.captured) {  // Vuil'raith gives captured units back
            auto it = std::find(f.captured.begin(), f.captured.end(), int16_t(c));
            if (it != f.captured.end()) f.captured.erase(it);
        }
    };
    transfer(d.give, a, b);
    transfer(d.get, b, a);
    dealInProgress_ = false;
    if (hireCtx_.nokar >= 0) {  // Hired Guns (0.43.0): the sale is agreed - the Nokar pick the ships afterwards
        if (a == hireCtx_.nokar && d.give.hire) hireCtx_.agreed = d.give.hire;
        if (b == hireCtx_.nokar && d.get.hire) hireCtx_.agreed = d.get.hire;
    }
    for (Promise p : d.promises) {
        p.to = int8_t(p.by == a ? b : a);
        p.contract = d.contract();
        p.untilRound = int8_t(promiseLastRound(G, p));
        if (p.kind == PromiseKind::LeaveSystem)
            p.deadlineTurn = int16_t(G.players[p.by].turns + (G.activePlayer == p.by ? 0 : 1));
        if (p.kind == PromiseKind::UseAgent) G.agentFor[p.by] = p.to;
        if (p.kind == PromiseKind::Contract) p.untilRound = int8_t(G.round + (G.phase == Phase::Agenda ? 1 : 0));  // the next agenda phase
        G.promises.push_back(p);
    }
    if (d.contract()) {
        clearHostile(a, b);  // a contract ends the red line
        ++G.transactions[a][b];
        // Pillage (Mentak): after a neighbour resolves a transaction
        pillageCheck(a);
        pillageCheck(b);
        teTransaction(a, b);  // Lie in Wait (Thunder's Edge)
    }
    dealEvent(E, kDealMade, a, b, pn(a) + " and " + pn(b) + " make a deal. " + dealText(G, d));
    persDealMade(d);
    return true;
}

// ---------------------------------------------------------------- promises
void Rules::breakPromise(Promise& p, const std::string& how) {
    if (p.state != kPromiseActive) return;
    p.state = kPromiseBroken;
    if (p.by >= 0 && p.by < G.numPlayers) G.players[p.by].promisesBroken = int8_t(std::min(99, G.players[p.by].promisesBroken + 1));
    markHostile(p.by, p.to);
    if (p.kind == PromiseKind::UseAgent && G.agentFor[p.by] == p.to) G.agentFor[p.by] = -1;
    persPromise(p, false);
    Promise shown = p;
    dealEvent(E, kDealBroken, p.by, p.to, pn(p.by) + " breaks a promise to " + pn(p.to) + " (" + promiseText(G, shown, false) + ")" +
                                 (how.empty() ? "." : ": " + how + "."));
}

void Rules::keepPromise(Promise& p) {
    if (p.state != kPromiseActive) return;
    p.state = kPromiseKept;
    if (p.kind == PromiseKind::UseAgent && G.agentFor[p.by] == p.to) G.agentFor[p.by] = -1;
    persPromise(p, true);
    say(p.by, pn(p.by) + " keeps a promise to " + pn(p.to) + " (" + promiseText(G, p, false) + ").");
}

void Rules::promiseActivate(int p, int sys) {
    for (Promise& pr : G.promises)
        if (pr.state == kPromiseActive && pr.by == p && pr.kind == PromiseKind::AvoidSystem && pr.target == sys)
            breakPromise(pr, "activated " + sysName(sys));
}

void Rules::promiseCombat(int att, int def, int sys) {
    if (att < 0 || def < 0 || att >= G.numPlayers || def >= G.numPlayers) return;  // (not against neutral units)
    markHostile(att, def);
    for (Promise& pr : G.promises) {
        if (pr.state != kPromiseActive || pr.by != att) continue;
        if (pr.kind == PromiseKind::NoAttack && pr.to == def) breakPromise(pr, "attacked them");
        else if (pr.kind == PromiseKind::LetPass && pr.to == def && pr.target == sys) breakPromise(pr, "attacked them there");
        else if (pr.kind == PromiseKind::AttackSystem && pr.target == sys) keepPromise(pr);
    }
}

void Rules::promiseVote(int p, int agenda, int outcome, int votes) {
    for (Promise& pr : G.promises) {
        if (pr.state != kPromiseActive || pr.by != p || pr.kind != PromiseKind::VoteFor || pr.agenda != agenda) continue;
        if (outcome == pr.target && votes > 0) keepPromise(pr);
        else breakPromise(pr, votes > 0 ? "voted for something else" : "didn't vote");
    }
}

void Rules::promiseEndOfTurn(int p) {
    for (Promise& pr : G.promises) {
        if (pr.state != kPromiseActive || pr.by != p || pr.kind != PromiseKind::LeaveSystem) continue;
        if (G.players[p].turns < pr.deadlineTurn) continue;
        if (hasShips(G, p, pr.target)) breakPromise(pr, "still has ships there");
        else keepPromise(pr);
    }
}

void Rules::promiseUsedAgent(int owner, int beneficiary) {
    for (Promise& pr : G.promises)
        if (pr.state == kPromiseActive && pr.by == owner && pr.to == beneficiary && pr.kind == PromiseKind::UseAgent) keepPromise(pr);
}

int Rules::agentUser(int b, const char* key) const {
    if (b < 0 || b >= G.numPlayers) return -1;
    if (canUseAgent(b, key)) return b;
    for (int o = 0; o < G.numPlayers; ++o)
        if (o != b && G.agentFor[o] == b && canUseAgent(o, key)) return o;
    return -1;
}

void Rules::agentDone(int user, int b) {
    curFor_ = -1;
    if (user >= 0 && b >= 0 && user != b) {
        say(user, pn(user) + " uses its agent for " + pn(b) + ", as promised.");
        promiseUsedAgent(user, b);
    }
}

// End of the round: promises that ran their course.
void Rules::promiseRoundEnd() {
    for (Promise& pr : G.promises) {
        if (pr.state != kPromiseActive || pr.untilRound > G.round) continue;
        switch (pr.kind) {
            case PromiseKind::AvoidSystem:
            case PromiseKind::LetPass:
            case PromiseKind::NoAttack: keepPromise(pr); break;
            case PromiseKind::AttackSystem: breakPromise(pr, "never attacked there"); break;
            case PromiseKind::UseAgent:
            case PromiseKind::VoteFor:
            case PromiseKind::Contract:
                pr.state = kPromiseExpired;
                if (pr.kind == PromiseKind::UseAgent && G.agentFor[pr.by] == pr.to) G.agentFor[pr.by] = -1;
                break;
            case PromiseKind::LeaveSystem:
                if (hasShips(G, pr.by, pr.target)) breakPromise(pr, "still has ships there");
                else keepPromise(pr);
                break;
            default: break;  // loans: status phase
        }
    }
    for (int p = 0; p < G.numPlayers; ++p)
        if (G.urgeAttack[p] >= 0 && G.urgeRound[p] <= G.round) G.urgeAttack[p] = -1;
}

// Status phase: loans that fall due are paid back (or not).
// ------------------------------------------------------------------ Trade card: the community X-1 (0.17.0)
// The owner's Trading doc (the TTS standard), Setup::trade bit 3:
//  - X-1 (a player the Trade holder may trade with now - LRR 94): they refresh
//    for free, then hand their X new commodities to the holder for X-1 trade
//    goods back. They net X-1, the holder 1 (the commodities become its trade
//    goods). Nothing binds them once refreshed: keeping the commodities breaks
//    their word (LRR 28.4: never enforced - noted, red line, trust lost).
//  - binding: the holder first buys their Trade Agreement for X-1; the refresh
//    then sends the commodities over by itself (and the note goes back).
//  - "debt meta": a player the holder can't trade with now refreshes for a
//    promise of 1 trade good, paid back in the status phase like a loan.
//  - the refreshed player may instead pay 1 trade good at once and keep the
//    commodities (binding, but no wash with the holder this turn).
void Rules::tradeX1(int p, std::vector<int>& done) {
    // (0.45.0, fixes 14 - the tester: the commodity value with its modifiers - space stations, relics, abilities)
    auto maxOf = [&](int q) { return G.fixes >= 14 ? commodityValue(G, q) : int(kFactions[G.players[q].faction].commodities); };
    auto tgs = [](int n) { return std::to_string(n) + (n == 1 ? " trade good" : " trade goods"); };
    auto refill = [&](int q, const std::string& why) {
        const int before = G.players[q].commodities;
        G.players[q].commodities = int8_t(eventOn(G, kEvCommerce) ? refreshTo(G, G.players[q]) : maxOf(q));
        say(q, pn(q) + " refills commodities" + why + ".");
        replenished(q, G.players[q].commodities - before);
    };
    std::vector<Option> xo;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p || G.players[q].eliminated || G.players[q].commodities >= maxOf(q)) continue;
        const int x = maxOf(q);
        const std::string xs = std::to_string(x);
        if (transactionAllowed(G, p, q)) {
            xo.push_back({q, pn(q) + ": X-1", "They refresh to " + xs + " and send you the " + xs + " commodities for " + tgs(x - 1) +
                                                   " back (you net 1). They can still back out."});
            if (x >= 2 && knownHolder(G, makeNote(q, kNoteTradeAgreement)) == q)
                xo.push_back({100 + q, pn(q) + ": buy their Trade Agreement",
                              "Pay " + tgs(x - 1) + " now; their refresh then sends you the " + xs + " commodities. Binding."});
        } else {
            xo.push_back({q, pn(q) + ": refresh for 1 owed",
                          "You can't trade with them now: they refresh to " + xs + " and owe you 1 trade good (status phase)."});
        }
    }
    if (xo.empty()) return;
    Decision d;
    d.player = p;
    d.kind = DecisionKind::Pick;
    d.tag = Tag::TradeX1Offer;
    d.prompt = "Trade: offer X-1 trades?";
    d.context = "Each player you pick refreshes for free and pays you back 1 trade good's worth.";
    d.options = xo;
    d.minPick = 0;
    d.maxPick = int(xo.size());
    d.validate = [xo](const Answer& a) -> std::string {
        std::set<int> seen;
        for (int x : a.picks) {
            bool ok = false;
            for (const Option& o : xo)
                if (o.id == x) ok = true;
            if (!ok) return "Not one of the options.";
            if (!seen.insert(x % 100).second) return "One kind of trade per player.";
        }
        return "";
    };
    std::vector<int> picks = E.ask(std::move(d)).picks;
    std::sort(picks.begin(), picks.end(), [](int a, int b) { return a % 100 < b % 100; });  // seat order
    for (int pk : picks) {
        const int q = pk % 100, x = maxOf(q);
        const bool neighbour = transactionAllowed(G, p, q);
        const int kind = pk >= 100 ? 1 : neighbour ? 0 : 2;  // 0 X-1, 1 buy the Trade Agreement, 2 debt
        if (kind == 1 && G.players[p].tradeGoods < x - 1) {
            say(p, pn(p) + " can't pay for " + pn(q) + "'s Trade Agreement.");
            continue;
        }
        Decision r;
        r.player = q;
        r.kind = DecisionKind::Pick;
        r.tag = Tag::TradeX1Accept;
        r.targetPlayer = p;
        r.value = kind;
        const std::string xs = std::to_string(x);
        if (kind == 0) {
            r.prompt = pn(p) + " offers an X-1 trade";
            r.context = "Refresh your commodities to " + xs + " for free, then send them to " + pn(p) + " for " + tgs(x - 1) + ".";
            r.options = {{1, "Accept", "Refresh, then trade the " + xs + " commodities for " + tgs(x - 1) + "."}};
            if (G.players[q].tradeGoods >= 1) r.options.push_back({2, "Pay 1 trade good now", "Refresh and keep the " + xs + " commodities."});
            r.options.push_back({0, "Decline", ""});
        } else if (kind == 1) {
            r.prompt = pn(p) + " wants to buy your Trade Agreement";
            r.context = pn(p) + " pays " + tgs(x - 1) + " for it now; your refresh to " + xs +
                        " then goes to " + pn(p) + " and the note comes back to you.";
            r.options = {{1, "Sell it", "Get " + tgs(x - 1) + "."}, {0, "Decline", ""}};
        } else {
            r.prompt = pn(p) + " offers a refresh on credit";
            r.context = "Refresh your commodities to " + xs + " for free and owe " + pn(p) + " 1 trade good, paid in the status phase.";
            r.options = {{1, "Accept", "Owe 1 trade good."}, {0, "Decline", ""}};
        }
        std::vector<Option> ropts = r.options;
        r.validate = [ropts](const Answer& a) -> std::string {
            if (a.picks.size() != 1) return "Choose one.";
            for (const Option& o : ropts)
                if (o.id == a.picks[0]) return "";
            return "Not one of the options.";
        };
        const int yes = E.ask(std::move(r)).picks[0];
        if (yes == 0) {
            Deal no;
            no.from = p;
            no.to = q;
            no.get.comm = x;
            no.give.tg = x - 1;
            recordDeal(no, 2);
            persDealRefused(no);
            dealEvent(E, kDealDeclined, q, p, pn(q) + " declines " + pn(p) + "'s X-1 trade.");
            continue;
        }
        done.push_back(q);
        if (kind == 1) {  // buy the Trade Agreement, then the refresh pays it out
            Deal buy;
            buy.from = p;
            buy.to = q;
            buy.give.tg = x - 1;
            buy.get.note = makeNote(q, kNoteTradeAgreement);
            if (checkDeal(G, buy).empty() && executeDeal(buy)) recordDeal(buy, 1);
            else recordDeal(buy, 3);
            refill(q, " (X-1 trade with " + pn(p) + ")");
        } else if (kind == 2) {  // debt
            Deal debt;
            debt.from = p;
            debt.to = q;
            Promise pr;
            pr.kind = PromiseKind::Repay;
            pr.by = int8_t(q);
            pr.amount = 1;
            pr.rounds = 1;
            debt.promises.push_back(pr);
            refill(q, " (X-1 on credit from " + pn(p) + ")");
            executeDeal(debt);
            recordDeal(debt, 1);
        } else if (yes == 2) {  // pay 1 now, keep the commodities
            Deal pay;
            pay.from = p;
            pay.to = q;
            pay.get.tg = 1;
            if (checkDeal(G, pay).empty() && executeDeal(pay)) recordDeal(pay, 1);
            else recordDeal(pay, 3);
            refill(q, " (paid " + pn(p) + " 1 trade good)");
        } else {  // X-1: refresh, then wash (or not)
            refill(q, " (X-1 trade with " + pn(p) + ")");
            const int have = G.players[q].commodities;
            if (have <= 0) continue;  // gone already (a Trade Agreement someone else held)
            Deal wash;
            wash.from = p;
            wash.to = q;
            wash.give.tg = std::max(0, have - 1);
            wash.get.comm = have;
            if (!checkDeal(G, wash).empty()) {
                say(q, pn(p) + " and " + pn(q) + " can't complete the X-1 trade now.");
                recordDeal(wash, 3);
                continue;
            }
            curFor_ = p;
            const bool keep =
                pick(q, Tag::TradeX1Settle,
                     "Send " + pn(p) + " your " + std::to_string(have) + " commodities for " + tgs(have - 1) + "?",
                     {{1, "Send them", "Get " + tgs(have - 1) + " - the deal."},
                      {0, "Keep them", "Breaks your word to " + pn(p) + "."}},
                     "You refreshed on that promise.") == 0;
            curFor_ = -1;
            if (!keep) {
                if (executeDeal(wash)) recordDeal(wash, 1);
                else recordDeal(wash, 3);
            } else {
                Player& b = G.players[q];
                b.promisesBroken = int8_t(std::min(99, b.promisesBroken + 1));
                markHostile(q, p);
                Promise shown;
                shown.kind = PromiseKind::Repay;
                shown.by = int8_t(q);
                shown.to = int8_t(p);
                shown.amount = 1;
                persPromise(shown, false);
                dealEvent(E, kDealBroken, q, p, pn(q) + " keeps the refreshed commodities and breaks the X-1 deal with " + pn(p) + ".");
            }
        }
    }
}

void Rules::loansDue() {
    for (size_t i = 0; i < G.promises.size(); ++i) {
        Promise pr = G.promises[i];
        if (pr.state != kPromiseActive || pr.kind != PromiseKind::Repay || pr.untilRound > G.round) continue;
        Player& b = G.players[pr.by];
        bool pay = false;
        if (!b.eliminated && b.tradeGoods >= pr.amount) {
            curFor_ = pr.to;  // whose loan (Decision::targetPlayer)
            pay = yesNo(pr.by, Tag::LoanRepay,
                        "Pay back " + std::to_string(pr.amount) + " trade goods to " + pn(pr.to) + " now?",
                        "You promised to pay this loan back by the end of round " + std::to_string(pr.untilRound) +
                            ". Refusing breaks the promise.");
            curFor_ = -1;
        }
        if (pay) {
            b.tradeGoods = int8_t(b.tradeGoods - pr.amount);
            tgFromPlayer_ = true;
            gainTG(pr.to, pr.amount);
            tgFromPlayer_ = false;
            keepPromise(G.promises[i]);
        } else {
            breakPromise(G.promises[i], b.tradeGoods < pr.amount ? "couldn't pay" : "refused to pay");
            persLoanUnpaid(pr.by, pr.to);
        }
    }
}

// Agenda phase: before each vote everyone may negotiate, in voting order.
void Rules::agendaNegotiations(const std::vector<int>& order) {
    resetDealCounters();
    for (int p : order) {
        if (G.players[p].eliminated) continue;
        for (int guard = 0; guard < 8 && canNegotiate(p); ++guard) {
            size_t before = G.deals.size();
            negotiate(p, -1);
            if (G.deals.size() == before) break;  // chose "Done"
        }
    }
}

}  // namespace ti
