// Computer players in negotiations (Stage 6): what a deal is worth to them,
// which deals they propose, and how they answer offers. Values are in trade
// goods, from each personality's price list (the owner's tables; default
// list when there's no personality). Personalities (0.12.0) also decide how
// readily a bot deals, which offers it makes, and whether it keeps its word.
#include "ai/ai.h"
#include "ai/ai_internal.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>

#include "engine/deal.h"
#include "engine/personality.h"
#include "engine/query.h"

namespace ti {

namespace {

// p's price for a promissory note (its archetype's list; the owner's defaults otherwise).
float priceOf(const Game& g, int p, int note) {
    const Style& S = aiStyle(g, p);
    if (!S.on) return noteDefaultValue(g, note);
    float v;
    switch (noteType(note)) {
        case kNoteTradeAgreement: v = S.tra; break;
        case kNoteCeasefire: v = S.cea; break;
        case kNoteSupportThrone: v = S.sup; break;
        case kNoteAlliance: v = S.all; break;
        case kNotePoliticalSecret: v = S.pol; break;
        default: v = noteDefaultValue(g, note) * S.noteMult; break;
    }
    return std::max(0.f, v + S.noteAdj);
}

// Does p mean to keep this promise? (a fixed answer per promise, so it doesn't waver)
bool meansToKeep(const Game& g, int p, const Promise& pr, int index) {
    const Style& S = aiStyle(g, p);
    if (!S.on) return true;
    if (S.betrayTarget == pr.to && (S.betrayKind == int(Betrayal::BreakPromise) || S.betrayKind == int(Betrayal::LoanScam)))
        return false;
    if (S.keepPromise >= 1.f) return true;
    return prand(g, 40, uint32_t(p), uint32_t(index), uint32_t(pr.kind), 100) < uint32_t(S.keepPromise * 100.f);
}


float trustIn(const Game& g, int p, int q) {
    if (q < 0 || q >= g.numPlayers) return 0.5f;
    float t = 1.f - 0.3f * float(g.players[q].promisesBroken);
    if (hostileFlag(g, p, q)) t *= 0.5f;
    return std::max(0.1f, t);
}

int vpLeader(const Game& g, int except) {
    int best = -1;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != except && !g.players[q].eliminated && (best < 0 || g.players[q].vp > g.players[best].vp)) best = q;
    return best;
}

// How much worse off we'd be letting q have a bit more: near-winners are dangerous.
float boostFactor(const Game& g, int q) {
    const int vp = g.players[q].vp;
    if (vp + 2 >= g.vpGoal) return 3.f;
    if (vp + 4 >= g.vpGoal) return 1.6f;
    return 1.f;
}

// Units (strength) attacker could bring to sys next turn: ships within 2 systems.
float strengthNear(const Game& g, int attacker, int sys) {
    float s = 0;
    for (int x : systemsWithin(g, sys, 2)) s += float(combatStrength(g, attacker, x, true));
    return s;
}

// What 'victim' stands to lose in sys if 'attacker' comes for it (trade goods).
float exposure(const Game& g, int attacker, int victim, int sys) {
    if (sys < 0 || sys >= int(g.systems.size())) return 0;
    float stuff = 0;
    for (int16_t pl : g.systems[sys].planets)
        if (g.planets[pl].owner == victim) stuff += aiPlanetValue(g, victim, pl) * 0.5f;
    stuff += float(g.systems[sys].space[victim].nonFighterShips()) * 1.5f;
    if (stuff <= 0) return 0;
    float att = strengthNear(g, attacker, sys), def = float(combatStrength(g, victim, sys, true)) + 1.f;
    float odds = att / (att + def);
    return stuff * odds;
}

// An equidistant system p and q are both nearest to, with planets nobody holds yet (the groundwork).
bool equiOpenFor(const Game& g, int p, int q, int sys) {
    if (sys < 0 || sys >= int(g.systems.size()) || !aiSliceTied(g, sys, p) || !aiSliceTied(g, sys, q)) return false;
    for (int16_t pl : g.systems[sys].planets)
        if (g.planets[pl].owner < 0) return true;
    return false;
}

// Does p have a reason to go into sys (planets to take, presence, an attack)?
float desireFor(const Game& g, int p, int sys) {
    if (sys < 0 || sys >= int(g.systems.size())) return 0;
    float v = 0;
    for (int16_t pl : g.systems[sys].planets)
        if (g.planets[pl].owner != p) v += aiPlanetValue(g, p, pl) * 0.15f;
    v += aiPresenceValue(g, p, sys, true) * 0.1f;
    if (sys == g.mecatol()) v += 1.f;
    return std::min(3.f, v);
}

float noteValueToReceiver(const Game& g, int p, int note) {
    const int owner = noteOwner(note), type = noteType(note);
    const float price = priceOf(g, p, note);
    const float pay = aiStyle(g, p).notePay;  // Slithering Tongue pays over the odds
    if (owner == p) return price + (type == kNoteSupportThrone ? 2.f : 0.f);  // our own back
    if (type == kNoteFaction && isFaction(g, owner, "firmament")) return 3.f * pay;  // Black Ops: 2 tokens, 2 trade goods (and a plot on us)
    if (type == kNoteFaction && isFaction(g, owner, "obsidian")) return -3.f;       // Malevolency: a curse
    // (Sardakk review, owner 2026-11-11) Tekklar Legion: +1 to every combat roll of one invasion - a strong card
    if (type == kNoteFaction && isFaction(g, owner, "sardakk") && aiTune("tekklarsell", 1.f) > 0.f) return std::max(price, 2.5f) * pay;
    switch (type) {
        case kNoteSupportThrone: {
            float v = price + float(std::max(0, g.players[p].vp - (g.vpGoal - 5))) * 0.8f;
            return (v - 0.5f) * pay;  // we must stay out of their systems to keep it
        }
        case kNoteCeasefire: return (areNeighbours(g, p, owner) ? price : price * 0.5f) * pay;
        case kNoteTradeAgreement:
            return std::max(price, float(kFactions[g.players[owner].faction].commodities) * 0.4f * std::min(1.f, price)) * pay;
        case kNoteFaction:  // Trade Convoys (0.32.0): worth what the markets it opens are worth to us
            if ((g.extras & 32) && isFaction(g, owner, "hacan")) return std::max(0.3f, aiTradeConvoysValue(g, p) * 2.6f) * pay;
            if ((g.extras & 64) && isFaction(g, owner, "empyrean")) return std::max(0.3f, aiDarkPactValue(g, p) * 2.4f) * pay;
            return price * pay;
        default: return price * pay;
    }
}

float noteCostToGiver(const Game& g, int p, int q, int note) {
    const int owner = noteOwner(note), type = noteType(note);
    const Style& S = aiStyle(g, p);
    if (owner != p) {  // someone else's note we hold (passing it on may be a betrayal objective)
        if (S.betrayKind == int(Betrayal::TradeAwayNote) && owner == S.betrayTarget) return -3.f;
        return noteValueToReceiver(g, p, note);
    }
    const float scale = S.on ? priceOf(g, p, note) / std::max(0.5f, noteDefaultValue(g, note)) : 1.f;
    if (type == kNoteFaction && isFaction(g, p, "firmament")) return -2.f;  // Black Ops: giving it plants a plot
    if (type == kNoteFaction && isFaction(g, p, "obsidian")) return 0.f;    // Malevolency
    // (Sardakk review, owner 2026-11-11) Tekklar Legion comes back after one use - lending it costs little (the deal
    // candidate only offers it to players on good terms or bound not to attack)
    if (type == kNoteFaction && isFaction(g, p, "sardakk") && aiTune("tekklarsell", 1.f) > 0.f) return 0.6f;
    // (the Empyrean review, checkpoint 2 - knob bloodpact) Blood Pact comes back when its holder activates a system with
    // our units, and their votes go with ours: handing it out costs nothing (1 trade good clears the offer bar)
    if (type == kNoteFaction2 && isFaction(g, p, "empyrean") && aiTune("bloodpact", 1.f) > 0.f) return 0.f;
    // (the Nomad review, checkpoint 2 - knob cavalrysell) The Cavalry is used against others and comes back after one combat
    if (type == kNoteFaction && isFaction(g, p, "nomad") && aiTune("cavalrysell", 0.f) > 0.f) return 0.f;
    switch (type) {
        case kNoteSupportThrone: {
            if (g.players[q].vp + 1 >= g.vpGoal) return 1000.f;  // never hand anyone the win
            // one point for one of several rivals; worse if they lead
            float c = 1.5f + 3.f / float(std::max(1, g.numPlayers - 1));
            if (q == vpLeader(g, p) && g.players[q].vp > g.players[p].vp) c *= 1.6f;
            return c * std::max(0.5f, std::min(2.f, scale));
        }
        case kNoteCeasefire: return (1.f + (exposure(g, p, q, g.players[q].homeSystem) > 0 ? 0.5f : 0.f)) * std::max(0.3f, scale);
        case kNoteTradeAgreement: return float(kFactions[g.players[p].faction].commodities) * 0.4f * std::max(0.3f, scale);
        case kNoteAlliance: return 1.5f * std::max(0.3f, scale);
        default: return S.on ? priceOf(g, p, note) : noteDefaultValue(g, note);
    }
}

float fragmentValue(const Game& g, int p, int frag, bool receiving) {
    if (frag < 0 || frag >= kExploreCardCount) return 0;
    if (kExploreCards[frag].kind == ExploreKind::Chart) {  // Star Chart (Uncharted Space): two of them make a new system
        const int n = starChartCount(g, p);
        return receiving ? (n % 2 == 1 ? 2.f : 1.f) : (n % 2 == 0 ? 2.f : 0.8f);
    }
    int type = int(kExploreCards[frag].type), same = 0;
    for (int16_t f : g.players[p].fragments)
        if (isRelicFragment(f) && (int(kExploreCards[f].type) == type || kExploreCards[f].type == ExploreType::Frontier)) ++same;
    const float k = aiStyle(g, p).fragment;  // Collector, Scrap Divers
    if (receiving) return (same == 2 ? 2.f : 1.f) * k;
    return (same == 3 ? 2.f : 1.f) * k;
}

// Hired Guns (0.43.0): n of the Nokar's ships fighting for us in the active system (and kept, room allowing)
float hireWorth(const Game& g, int p, int n) {
    const int sys = g.activeSystem;
    bool fight = false;
    if (sys >= 0)
        for (int q = 0; q < kSides; ++q)
            if (q != p && !isFaction(g, q, "nokar") && hasUnits(g, q, sys)) fight = true;
    return float(n) * (fight ? 2.6f : 1.1f);
}

float sideReceived(const Game& g, int p, const DealSide& s) {
    const Style& S = aiStyle(g, p);
    float v = float(s.tg) + float(s.comm) * (S.on ? S.comm : 1.f);  // commodities arrive as trade goods
    if (s.note >= 0) v += noteValueToReceiver(g, p, s.note);
    for (int f : s.fragments) v += fragmentValue(g, p, f, true);
    v += S.acValue * float(s.actionCards.size());
    if (s.planet >= 0 && s.planet < int(g.planets.size())) v += aiPlanetValue(g, p, s.planet);
    for (int c : s.captured) v += float(unitDef(g, p, UnitType(c % 16)).cost2) * 0.4f;
    for (int r : s.relics)  // Thunder's Edge: relics (The Silver Flame is a gamble) and secret objectives
        v += r >= 0 && r < kRelicCount && !std::strcmp(kRelics[r].key, "thesilverflame") ? 0.3f : 3.f;
    v += 1.5f * float(s.secrets.size());
    for (int t : s.techs)  // Age of Commerce: a technology for free
        if (t >= 0 && t < kTechCount && !g.players[size_t(p)].hasTech(t)) v += std::min(5.f, 1.f + aiTechWant(g, p, t) * 0.5f);
    v += 1.2f * float(s.debt);  // Binding Debts (the Vaden): ships, infantry and trade goods later
    for (int o : s.orders)  // Arms Dealers (the Axis): a ship or two at the end of a turn - nothing to the Axis themselves
        v += isFaction(g, p, "axis") ? 0.3f : (o < 6 ? float(3 - o / 2) + 1.f : 2.5f) * (g.round >= 6 ? 0.7f : 1.f);
    if (s.hire) v += hireWorth(g, p, s.hire);  // Hired Guns (0.43.0)
    return v;
}

float sideGiven(const Game& g, int p, int q, const DealSide& s) {
    const Goals& G = aiGoals(g, p);
    float tgCost = float(s.tg);
    int keep = g.players[p].tradeGoods - s.tg;
    if (keep < G.keepTg) tgCost += float(G.keepTg - keep) * 1.0f;  // we needed those for an objective
    const Style& S = aiStyle(g, p);
    float v = tgCost + 0.3f * float(s.comm) * (S.on ? S.comm : 1.f);  // our own commodities are only good for trading
    if (s.note >= 0) v += noteCostToGiver(g, p, q, s.note);
    for (int f : s.fragments) v += fragmentValue(g, p, f, false);
    v += S.acValue * float(s.actionCards.size());
    if (s.planet >= 0 && s.planet < int(g.planets.size())) v += aiPlanetValue(g, p, s.planet) + 1.f;
    for (int c : s.captured) v += float(unitDef(g, c / 16 < g.numPlayers ? c / 16 : p, UnitType(c % 16)).cost2) * 0.25f;
    for (int r : s.relics) v += r >= 0 && r < kRelicCount && !std::strcmp(kRelics[r].key, "thesilverflame") ? 0.5f : 4.5f;
    v += 3.f * float(s.secrets.size());
    // Age of Commerce: sharing a technology costs us nothing but the edge (more against someone close to winning)
    if (!s.techs.empty()) v += (2.f + (q >= 0 && q < g.numPlayers && g.players[size_t(q)].vp >= g.vpGoal - 3 ? 3.f : 0.f)) * float(s.techs.size());
    // Binding Debts: a token on the Vaden's sheet costs a trade good to buy back (half of one each) - more if their
    // fleet is next door (Collateralized Loans)
    if (s.debt) v += (0.6f + (q >= 0 && q < g.numPlayers && areNeighbours(g, p, q) ? 0.3f : 0.f)) * float(s.debt);
    for (int o : s.orders)  // Axis Orders: the Axis can't use them - only the ships they put in someone else's fleet
        v += isFaction(g, p, "axis") ? 0.4f : (o < 6 ? float(3 - o / 2) + 1.f : 2.5f) * 0.8f;
    v += 0.7f * float(s.hire);  // Hired Guns: the Nokar's ships leave the board (they rebuild them)
    return v * boostFactor(g, q);
}

// Value to beneficiary p of promise pr (made by pr.by).
float promiseValue(const Game& g, int p, const Promise& pr) {
    const int x = pr.by;
    const float rounds = float(std::max<int>(1, pr.rounds));
    float v = 0;
    switch (pr.kind) {
        case PromiseKind::AvoidSystem:
            v = rounds * (0.2f + exposure(g, x, p, pr.target) * 0.25f);
            // the groundwork (0.25.0): an equidistant system we both want, left to us to claim
            // (plus an agreed border instead of a race: worth a little to both sides)
            if (equiOpenFor(g, p, x, pr.target)) v += rounds * 0.6f * desireFor(g, p, pr.target) + aiTune("g_splitpeace", 1.f);
            break;
        case PromiseKind::LeaveSystem: v = pr.target == g.mecatol() ? 5.f : 2.f; break;
        case PromiseKind::AttackSystem: {
            float hurt = 0;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != p && q != x && hasUnits(g, q, pr.target)) hurt = std::max(hurt, q == vpLeader(g, p) ? 2.5f : 1.f);
            v = hurt;
            break;
        }
        case PromiseKind::LetPass: v = rounds * (hasUnits(g, p, pr.target) ? 0.8f : 0.3f); break;
        case PromiseKind::VoteFor: {
            if (pr.agenda < 0) break;
            float stake = aiOutcomeValue(g, p, pr.agenda, pr.target);
            v = std::max(0.f, stake) * std::min(1.f, float(availableVotes(g, x)) / 5.f) * 1.5f;
            break;
        }
        case PromiseKind::Repay: v = float(pr.amount); break;
        case PromiseKind::UseAgent: v = aiStyle(g, p).on ? aiStyle(g, p).agent : 1.f; break;
        case PromiseKind::Contract: v = spendable(g, p, false) >= 4 ? 1.2f : 0.2f; break;  // a ship out of turn, if we can pay
        case PromiseKind::NoAttack: {
            float e = 0;
            for (size_t s = 0; s < g.systems.size(); ++s)
                if (hasUnits(g, p, int(s))) e = std::max(e, exposure(g, x, p, int(s)));
            v = rounds * (0.3f + e * 0.3f);
            break;
        }
        default: break;
    }
    return v * trustIn(g, p, x);
}

// Cost to promiser x of keeping pr (made to pr.to).
float promiseCost(const Game& g, int x, int to, const Promise& pr) {
    const float rounds = float(std::max<int>(1, pr.rounds));
    switch (pr.kind) {
        case PromiseKind::AvoidSystem:
            // an equidistant system we'd likely lose half the races for anyway costs half (the groundwork)
            return rounds * (0.1f + desireFor(g, x, pr.target) * (equiOpenFor(g, x, to, pr.target) ? 0.5f : 1.f));
        case PromiseKind::LeaveSystem: {
            float c = 3.f;
            if (pr.target == g.mecatol()) {
                for (int16_t pl : g.systems[pr.target].planets)
                    if (g.planets[pl].owner == x) c += 4.f;
                c += 2.f;
            }
            return c;
        }
        case PromiseKind::AttackSystem: {
            // only worth it if we'd go there anyway and can win
            float d = desireFor(g, x, pr.target);
            return d > 0.5f ? 0.8f : 4.f;
        }
        case PromiseKind::LetPass: return rounds * 0.4f;
        case PromiseKind::VoteFor: {
            if (pr.agenda < 0) return 0.5f;
            for (const Promise& o : g.promises)  // our votes are already promised elsewhere
                if (o.state == kPromiseActive && o.by == x && o.kind == PromiseKind::VoteFor && o.agenda == pr.agenda)
                    return o.target == pr.target ? 0.1f : 1000.f;
            float stake = aiOutcomeValue(g, x, pr.agenda, pr.target);
            return std::max(0.f, -stake) * 1.5f + 0.3f;
        }
        case PromiseKind::Repay: return float(pr.amount) * 0.9f;
        case PromiseKind::UseAgent: return (aiStyle(g, x).on ? aiStyle(g, x).agent : 1.f) * 1.2f;
        case PromiseKind::Contract: return 0.4f;  // one of our picks goes to them
        case PromiseKind::NoAttack: {
            float d = 0;
            for (size_t s = 0; s < g.systems.size(); ++s)
                if (hasUnits(g, to, int(s))) d = std::max(d, desireFor(g, x, int(s)) * 0.5f);
            return rounds * (0.2f + d);
        }
        default: return 0.f;
    }
}

// Is q's note publicly somewhere else (face up in a play area, or traded away in a deal everyone saw)?
float acceptThreshold(const Game& g) { return aiLevelOf(g) >= 2 ? 0.4f : aiLevelOf(g) == 1 ? 0.f : -0.3f; }
// ... for p, answering q (personality: harder or easier to please, and towards whom)
float acceptThresholdFor(const Game& g, int p, int q) {
    const Style& S = aiStyle(g, p);
    float t = acceptThreshold(g) + S.acceptBias;
    if (q >= 0 && q < 8) t += S.biasVs[q];
    return t;
}

}  // namespace

float aiDealNet(const Game& g, int p, const Deal& d) {
    const bool proposer = p == d.from;
    const int q = proposer ? d.to : d.from;
    const DealSide& mine = proposer ? d.give : d.get;
    const DealSide& theirs = proposer ? d.get : d.give;
    float net = sideReceived(g, p, theirs) - sideGiven(g, p, q, mine);
    for (const Promise& pr : d.promises) {
        if (pr.by == p) net -= promiseCost(g, p, q, pr);
        else net += promiseValue(g, p, pr);
    }
    // a threat against us: accepting avoids the attack
    if (!proposer && d.threat >= 0) net += exposure(g, q, p, d.threat) * 0.8f;
    // Dark Pact (0.32.1): a full load of commodities to the Empyrean earns each side 1 trade good
    auto pact = [&](int giver, int taker, const DealSide& s) {
        if (giver < 0 || taker < 0 || !s.comm || !isFaction(g, taker, "empyrean")) return false;
        if (s.comm < kFactions[g.players[giver].faction].commodities) return false;
        for (int16_t n : g.players[giver].notesInPlay)
            if (noteOwner(n) == taker && noteType(n) == kNoteFaction) return true;
        return false;
    };
    if (pact(d.from, d.to, d.give) || pact(d.to, d.from, d.get)) net += 1.f;
    return net;
}

namespace {

// The candidate deals p could propose to q now (the AI picks the best one).
std::vector<Deal> candidates(const Game& g, int p, int q) {
    std::vector<Deal> out;
    const Player& me = g.players[p];
    const Player& them = g.players[q];
    const bool tx = transactionAllowed(g, p, q);
    const bool friendly = !hostileFlag(g, p, q) && them.promisesBroken < 2;
    const Goals& G = aiGoals(g, p);
    auto base = [&]() {
        Deal d;
        d.from = p;
        d.to = q;
        return d;
    };
    if (tx && friendly) {
        // swap commodities: both sides turn them into trade goods
        if (me.commodities > 0 && them.commodities > 0) {  // equal numbers: a fair swap
            Deal d = base();
            d.give.comm = d.get.comm = std::min(me.commodities, them.commodities);
            out.push_back(d);
        }
        // sell our commodities for their trade goods, about 3 for 2: they gain the
        // difference, we turn commodities that are only good for trading into money
        if (me.commodities >= 2 && them.tradeGoods >= 1 && them.commodities < me.commodities) {
            Deal d = base();
            d.give.comm = me.commodities;
            d.get.tg = std::min<int>(them.tradeGoods, std::max(1, me.commodities * 2 / 3));
            out.push_back(d);
        }
        // and buy theirs the same way
        if (them.commodities >= 2 && me.tradeGoods >= 1 && me.commodities < them.commodities) {
            Deal d = base();
            d.get.comm = them.commodities;
            d.give.tg = std::max(1, them.commodities * 2 / 3);
            if (d.give.tg <= me.tradeGoods) out.push_back(d);
        }
        // swap Support for the Throne: a victory point each
        const int mySftT = makeNote(p, kNoteSupportThrone), theirSftT = makeNote(q, kNoteSupportThrone);
        bool haveTheirs = false;
        for (int16_t n : me.notesInPlay)
            if (n == theirSftT) haveTheirs = true;
        if (!haveTheirs && std::find(me.notes.begin(), me.notes.end(), int16_t(mySftT)) != me.notes.end() && g.round >= 2) {
            Deal d = base();
            d.give.note = mySftT;
            d.get.note = theirSftT;
            out.push_back(d);
        }
        // (Sardakk review, owner 2026-11-11) Tekklar Legion for 1-2 trade goods: it comes back after one use. Only to a
        // player bound by a no-attack promise or on good terms - its -1 against the Sardakk makes it dangerous otherwise
        if (isFaction(g, p, "sardakk") && g.phase == Phase::Action && aiLevelOf(g) > 0 && aiTune("tekklarsell", 1.f) > 0.f &&
            them.tradeGoods >= 1 && !(aiStyle(g, p).on && aiStyle(g, p).betrayTarget == q)) {
            const int16_t tk = makeNote(p, kNoteFaction);
            bool pact = false;
            for (const Promise* pr : activePromises(g, p, q))
                if (pr->kind == PromiseKind::NoAttack && pr->by == q) pact = true;
            if (std::find(me.notes.begin(), me.notes.end(), tk) != me.notes.end() && (pact || (attitudeOf(g, p, q) == Attitude::Friendly && attitudeOf(g, q, p) != Attitude::Hostile))) {
                Deal d = base();
                d.give.note = tk;
                d.get.tg = them.tradeGoods >= 3 ? 2 : 1;
                out.push_back(d);
            }
        }
        // the Firmament: Black Ops to a neighbour (they gain 2 tokens and 2 trade goods; we plant a plot)
        if (isFaction(g, p, "firmament") && g.phase == Phase::Action && plotCount(me) < kPlotCount && aiTune("firmbo", 1.f) > 0.f) {
            const int16_t bo = makeNote(p, kNoteFaction);
            if (std::find(me.notes.begin(), me.notes.end(), bo) != me.notes.end()) {
                Deal d = base();
                d.give.note = bo;
                // (test knob, the Firmament bot review) ask a little for it: it's worth 2 tokens and 2 trade goods to them
                if (const int price = int(aiTune("boprice", 0.f)); price > 0) {
                    if (them.tradeGoods >= price) d.get.tg = price;
                    else if (them.commodities >= price) d.get.comm = price;
                }
                out.push_back(d);
            }
        }
        // Age of Commerce: buy a technology we want from them for a few trade goods
        if (eventOn(g, kEvCommerce) && g.phase == Phase::Action && me.tradeGoods >= 2) {
            int best = -1;
            float bw = 3.f;
            for (int16_t t : them.techs)
                if (kTechs[t].faction < 0 && !me.hasTech(t) && aiTechWant(g, p, t) > bw) {
                    bw = aiTechWant(g, p, t);
                    best = t;
                }
            if (best >= 0) {
                Deal d = base();
                d.get.techs = {best};
                d.give.tg = std::min<int>(me.tradeGoods, 3);
                out.push_back(d);
            }
        }
        // the Axis (Discordant Stars, Arms Dealers): an Axis Order from our play area for its cost in trade goods - or
        // in commodities (one more), so the orders keep moving (owner, D112: the Axis wants them in other fleets)
        if (isFaction(g, p, "axis") && (g.modules & kModDiscordant) && (them.tradeGoods >= 1 || them.commodities >= 2)) {
            int offered = 0;
            for (int i = 0; i < 8 && offered < 2; ++i) {
                if (g.dsOrders[i] != p || ((g.dsOrderEx >> i) & 1)) continue;
                const int cost = i < 2 ? 3 : i < 4 ? 2 : 1;
                Deal d = base();
                d.give.orders = {i};
                if (them.tradeGoods >= cost) d.get.tg = cost;
                else if (them.commodities >= cost + 1) d.get.comm = cost + 1;
                else if (them.tradeGoods >= 1 && cost > 1) d.get.tg = cost - 1;  // (a discount rather than no sale)
                else continue;
                out.push_back(d);
                ++offered;
                i |= 1;  // (one of each type)
            }
        }
        // ... and the buyers ask for one: a ship of that kind at the end of a turn
        if (!isFaction(g, p, "axis") && isFaction(g, q, "axis") && (g.modules & kModDiscordant) && (me.tradeGoods >= 1 || me.commodities >= 2))
            for (int i = 0; i < 8; ++i) {
                if (g.dsOrders[i] != q || ((g.dsOrderEx >> i) & 1)) continue;
                const int cost = i < 2 ? 3 : i < 4 ? 2 : 1;
                Deal d = base();
                d.get.orders = {i};
                if (me.commodities >= cost + 1) d.give.comm = cost + 1;
                else if (me.tradeGoods >= cost) d.give.tg = cost;
                else continue;
                out.push_back(d);
                break;
            }
        // the Vaden (Discordant Stars, Binding Debts): our commodities for their control tokens on our sheet
        if (isFaction(g, p, "vaden") && (g.modules & kModDiscordant) && me.commodities >= 1 && g.dsDebt[q] < 5) {
            Deal d = base();
            d.give.comm = std::min<int>(me.commodities, 3);
            d.get.debt = d.give.comm >= 3 ? 2 : 1;
            out.push_back(d);
        }
        // swap ceasefires with a strong neighbour
        const int myCf = makeNote(p, kNoteCeasefire);
        if (std::find(me.notes.begin(), me.notes.end(), int16_t(myCf)) != me.notes.end() && areNeighbours(g, p, q) &&
            strengthNear(g, q, me.homeSystem) > strengthNear(g, p, me.homeSystem)) {
            Deal d = base();
            d.give.note = myCf;
            d.get.note = makeNote(q, kNoteCeasefire);
            out.push_back(d);
        }
        // Dark Pact (0.32.1): a full load of commodities to the Empyrean (each of us gains 1 trade good), washed
        // back as trade goods - from the holder, or asked for by the Empyrean
        {
            const int16_t pactQ = makeNote(q, kNoteFaction), pactP = makeNote(p, kNoteFaction);
            const auto inPlay = [](const Player& pl, int16_t n) {
                return std::find(pl.notesInPlay.begin(), pl.notesInPlay.end(), n) != pl.notesInPlay.end();
            };
            const int myX = commodityValue(g, me), theirX = commodityValue(g, them);
            if (isFaction(g, q, "empyrean") && inPlay(me, pactQ) && me.commodities >= myX && myX > 0) {
                for (int back : {myX - 1, myX}) {
                    if (back < 1 || back > them.tradeGoods) continue;
                    Deal d = base();
                    d.give.comm = me.commodities;
                    d.get.tg = back;
                    out.push_back(d);
                }
            }
            if (isFaction(g, p, "empyrean") && inPlay(them, pactP) && them.commodities >= theirX && theirX > 0) {
                for (int back : {theirX - 1, theirX}) {
                    if (back < 1 || back > me.tradeGoods) continue;
                    Deal d = base();
                    d.get.comm = them.commodities;
                    d.give.tg = back;
                    out.push_back(d);
                }
            }
            // (new games) buy the note from the Empyrean when it pays, or sell ours as the Empyrean
            const auto holds = [](const std::vector<int16_t>& v, int16_t n) { return std::find(v.begin(), v.end(), n) != v.end(); };
            if ((g.extras & 64) && g.phase == Phase::Action) {
                if (isFaction(g, q, "empyrean") && !holds(me.notes, pactQ) && !inPlay(me, pactQ) && knownHolder(g, pactQ) == q &&
                    aiDarkPactValue(g, p) >= 0.45f) {
                    if (me.tradeGoods >= 1) {
                        Deal d = base();
                        d.get.note = pactQ;
                        d.give.tg = 1;
                        out.push_back(d);
                    }
                    if (me.commodities >= 2) {
                        Deal d = base();
                        d.get.note = pactQ;
                        d.give.comm = 2;
                        out.push_back(d);
                    }
                }
                if (isFaction(g, p, "empyrean") && holds(me.notes, pactP) && them.tradeGoods >= 1) {
                    Deal d = base();
                    d.give.note = pactP;
                    d.get.tg = 1;
                    out.push_back(d);
                }
            }
        }
        // (the Empyrean review, checkpoint 2 - knob bloodpact, ON - owner 2026-11-12 23:50) the guide: "Offer Blood Pact and Dark Pact to
        // neighbors early" - Blood Pact for 1 trade good to a player on good terms who isn't near the win (it comes back
        // when they activate a system with our units; their votes join ours). Tested (90 games each, wins / VP): TE 18 / 7.07
        // -> 19 / 7.51, fresh seeds 11 / 7.11 -> 12 / 7.21, plain 14 / 7.10 -> 17 / 6.91, plain fresh 19 / 7.64 -> 22 / 7.79;
        // Blood Pact handed out ~1.1 a game (it was never used)
        if (aiTune("bloodpact", 1.f) > 0.f && isFaction(g, p, "empyrean") && g.phase == Phase::Action) {
            const int16_t bp = makeNote(p, kNoteFaction2);
            if (std::find(me.notes.begin(), me.notes.end(), bp) != me.notes.end() && them.tradeGoods >= 1 && !hostileFlag(g, p, q) &&
                g.players[size_t(q)].vp < g.vpGoal - 3) {
                Deal d = base();
                d.give.note = bp;
                d.get.tg = 1;
                out.push_back(d);
            }
        }
        // (the Nomad review, checkpoint 2 - knob cavalrysell, default off) The Cavalry was never handed out: sell it for 1 trade
        // good to a player on good terms who isn't near the win (it only works against others and comes back after one combat).
        // Tested (90 games each, wins / VP): TE 10 / 7.12 -> 17 / 7.30, fresh seeds 11 / 6.91 -> 9 / 7.01, plain 14 / 7.47 -> 14 /
        // 7.33, plain fresh 14 / 7.21 -> 16 / 7.23 - mixed; The Cavalry handed out ~2.5 a game (it was never used)
        if (aiTune("cavalrysell", 0.f) > 0.f && isFaction(g, p, "nomad") && g.phase == Phase::Action) {
            const int16_t cav = makeNote(p, kNoteFaction);
            if (std::find(me.notes.begin(), me.notes.end(), cav) != me.notes.end() && them.tradeGoods >= 1 && !hostileFlag(g, p, q) &&
                g.players[size_t(q)].vp < g.vpGoal - 3) {
                Deal d = base();
                d.give.note = cav;
                d.get.tg = 1;
                out.push_back(d);
            }
        }
        // Trade Convoys (0.32.0, new games): buy it from the Hacan when the players we can't reach are worth
        // trading with - or, as the Hacan, sell it to someone who needs it
        if ((g.extras & 32) && g.phase == Phase::Action) {
            const int16_t theirTc = makeNote(q, kNoteFaction), myTc = makeNote(p, kNoteFaction);
            const auto holds = [](const std::vector<int16_t>& v, int16_t n) { return std::find(v.begin(), v.end(), n) != v.end(); };
            if (isFaction(g, q, "hacan") && !holds(me.notes, theirTc) && !holds(me.notesInPlay, theirTc) &&
                knownHolder(g, theirTc) == q && aiTradeConvoysValue(g, p) >= 0.45f) {
                for (int pay = 1; pay <= 2 && pay <= me.tradeGoods; ++pay) {
                    Deal d = base();
                    d.get.note = theirTc;
                    d.give.tg = pay;
                    out.push_back(d);
                }
                if (me.commodities >= 2) {
                    Deal d = base();
                    d.get.note = theirTc;
                    d.give.comm = std::min<int>(me.commodities, 3);
                    out.push_back(d);
                }
            }
            if (isFaction(g, p, "hacan") && holds(me.notes, myTc) && them.tradeGoods >= 1) {
                Deal d = base();
                d.give.note = myTc;
                d.get.tg = std::min<int>(2, them.tradeGoods);
                out.push_back(d);
            }
        }
        // a loan for a spend objective we are a little short of
        int need = G.keepTg - me.tradeGoods;
        if (need > 0 && need <= 3 && them.tradeGoods >= need + 2) {
            Deal d = base();
            d.get.tg = need;
            Promise r;
            r.kind = PromiseKind::Repay;
            r.by = int8_t(p);
            r.amount = int8_t(need + 1);
            r.rounds = 2;
            d.promises.push_back(r);
            out.push_back(d);
        }
    }
    // The groundwork (0.25.0): split the equidistant systems with a neighbour - each picks one
    // and promises to stay out of the other's for two rounds - instead of racing for both.
    if (friendly && g.phase == Phase::Action && g.round <= 3 && aiGroundScale(g, p) > 0.f && aiTune("g_split", 1.f) > 0.f) {
        std::vector<int> shared;
        for (size_t s = 0; s < g.systems.size(); ++s)
            if (equiOpenFor(g, p, q, int(s))) shared.push_back(int(s));
        bool already = false;
        for (const Promise* pr : activePromises(g, p, q))
            if (pr->kind == PromiseKind::AvoidSystem) already = true;
        if (shared.size() >= 2 && !already) {
            int mine = -1, theirs = -1;
            float best = -1;
            for (int s : shared)
                if (desireFor(g, p, s) > best) {
                    best = desireFor(g, p, s);
                    mine = s;
                }
            best = -1;
            for (int s : shared)
                if (s != mine && desireFor(g, q, s) > best) {
                    best = desireFor(g, q, s);
                    theirs = s;
                }
            if (mine >= 0 && theirs >= 0) {
                Deal d = base();
                Promise a, b;
                a.kind = b.kind = PromiseKind::AvoidSystem;
                a.by = int8_t(q);
                a.target = int16_t(mine);
                b.by = int8_t(p);
                b.target = int16_t(theirs);
                a.rounds = b.rounds = 2;
                d.promises = {a, b};
                out.push_back(d);
            }
        }
    }
    // agenda phase: buy votes for the outcome we care about
    if (g.currentAgenda >= 0 && friendly) {
        const int a = g.currentAgenda;
        float bestStake = 0;
        int best = -1;
        float conf = 0;
        int predicted = aiPredictOutcome(g, a, &conf);
        int ea = a;
        if (!std::strcmp(kAgendas[a].key, "covert") && g.covertAgenda >= 0) ea = g.covertAgenda;
        const Elect el = kAgendas[ea].elect;
        if (el == Elect::ForAgainst || el == Elect::Player || el == Elect::StrategyCard)  // planets / laws: not bought
            for (const auto& oc : voteOutcomes(g, a)) {
                float v = aiOutcomeValue(g, p, a, oc.first) - aiOutcomeValue(g, p, a, predicted);
                if (v > bestStake) {
                    bestStake = v;
                    best = oc.first;
                }
            }
        const int votes = availableVotes(g, q);
        const int spare = me.tradeGoods - G.keepTg;
        if (best >= 0 && bestStake >= 1.5f && votes >= 2 && spare >= 1 && aiOutcomeValue(g, q, a, best) > -1.f) {
            Deal d = base();
            d.give.tg = std::min(spare, std::max(1, (votes + 1) / 2));
            if (!tx) d.give.tg = 0;
            Promise v;
            v.kind = PromiseKind::VoteFor;
            v.by = int8_t(q);
            v.target = int16_t(best);
            v.agenda = int16_t(a);
            v.rounds = 1;
            d.promises.push_back(v);
            if (tx || d.give.tg == 0) out.push_back(d);
        }
    }
    // ask whoever sits on Mecatol Rex to leave it (Hard: once per round)
    const int mec = g.mecatol();
    if (aiLevelOf(g) >= 2 && mec >= 0 && g.phase == Phase::Action && g.custodiansRemoved && leaveRequestable(g, q, mec) &&
        strengthNear(g, p, mec) > 0) {
        bool askedThisRound = false;
        for (const DealRecord& r : g.deals)
            if (r.round == g.round && r.from == p && r.to == q) askedThisRound = true;
        if (!askedThisRound) {
            Deal d = base();
            if (tx && me.tradeGoods - G.keepTg >= 2) d.give.tg = 2;
            Promise l;
            l.kind = PromiseKind::LeaveSystem;
            l.by = int8_t(q);
            l.target = int16_t(mec);
            l.rounds = 1;
            d.promises.push_back(l);
            out.push_back(d);
        }
    }
    // ---- personalities (0.12.0)
    const Style& S = aiStyle(g, p);
    auto promiseOf = [&](PromiseKind k, int by, int rounds, int target = -1, int amount = 0) {
        Promise pr;
        pr.kind = k;
        pr.by = int8_t(by);
        pr.rounds = int8_t(rounds);
        pr.target = int16_t(target);
        pr.amount = int8_t(amount);
        return pr;
    };
    // A demand backed by a threat to attack one of their systems (only from a position of strength
    // unless 'forced' by a betrayal objective).
    auto threatDeal = [&](bool forced) {
        if (!tx || them.tradeGoods + them.commodities < 1) return;
        int bestSys = -1;
        float bestOdds = 0.f;
        for (int s : threatSystems(g, p, q)) {
            Force mine;
            for (int s2 : systemsWithin(g, s, 1))
                for (int t : kShipIdx) mine.n[t] = uint8_t(mine.n[t] + g.systems[s2].space[p].n[t]);
            float odds = fightOdds(g, p, mine, q, g.systems[s].space[q], true);
            if (mine.nonFighterShips() > 0 && odds > bestOdds) {
                bestOdds = odds;
                bestSys = s;
            }
        }
        if (bestSys < 0 || (!forced && bestOdds < 0.65f)) return;
        Deal d = base();
        d.threat = bestSys;
        int amount = std::min<int>(them.tradeGoods, 1 + int(S.threat + 0.5f));
        if (amount > 0) d.get.tg = amount;
        else d.get.comm = std::min<int>(them.commodities, 2);
        out.push_back(d);
    };
    if (S.on && g.phase == Phase::Action) {
        bool pact = false;
        for (const Promise* pr : activePromises(g, p, q))
            if (pr->kind == PromiseKind::NoAttack) pact = true;
        const bool neighbours = areNeighbours(g, p, q);
        // a no-attack pact (Peacekeeper, Rusher, Border Watch...) - or one it means to break (Betrayer)
        if ((S.peaceDeals > 0 || S.allianceDeals > 0) && neighbours && friendly && !pact) {
            Deal d = base();
            d.promises = {promiseOf(PromiseKind::NoAttack, p, 2), promiseOf(PromiseKind::NoAttack, q, 2)};
            out.push_back(d);
        }
        // Betrayer: a formal alliance (Alliance notes swapped)
        if (S.allianceDeals > 0 && tx && friendly && g.round >= 2) {
            const int mine = makeNote(p, kNoteAlliance), theirs = makeNote(q, kNoteAlliance);
            bool haveTheirs = false;
            for (int16_t n : me.notesInPlay)
                if (n == theirs) haveTheirs = true;
            if (!haveTheirs && std::find(me.notes.begin(), me.notes.end(), int16_t(mine)) != me.notes.end()) {
                Deal d = base();
                d.give.note = mine;
                d.get.note = theirs;
                out.push_back(d);
            }
        }
        // Swindler: sells a promise not to attack
        if (S.promiseSell > 0 && tx && them.tradeGoods >= 2 && !pact && exposure(g, p, q, them.homeSystem) + exposure(g, p, q, g.mecatol()) > 0.f) {
            Deal d = base();
            d.get.tg = them.tradeGoods >= 4 ? 2 : 1;
            d.promises = {promiseOf(PromiseKind::NoAttack, p, 2)};
            out.push_back(d);
        }
        // a lender (Merchant, War Bankers)
        if (S.lend > 0 && tx && friendly && me.tradeGoods - G.keepTg >= 4 && them.tradeGoods <= 1) {
            Deal d = base();
            d.give.tg = 2;
            d.promises = {promiseOf(PromiseKind::Repay, q, 2, -1, 3)};
            out.push_back(d);
        }
        // Black Market Dealings (0.43.0): a card we can spare, for 2 trade goods
        if (tx && blackMarketPair(g, p, q) && !actionCardsTradeable(g, p, q) && me.actionCards.size() >= 3 && them.tradeGoods >= 2) {
            int worst = me.actionCards[0];
            for (int16_t c : me.actionCards)
                if (aiCardKeepValue(c) < aiCardKeepValue(worst)) worst = c;
            Deal d = base();
            d.give.actionCards = {worst};
            d.get.tg = 2;
            out.push_back(d);
        }
        // Scarface (Hacan): sells an action card
        if (S.sellCards > 0 && tx && actionCardsTradeable(g, p, q) && me.actionCards.size() >= 3 && them.tradeGoods >= 2) {
            int worst = me.actionCards[0];
            for (int16_t c : me.actionCards)
                if (aiCardKeepValue(c) < aiCardKeepValue(worst)) worst = c;
            Deal d = base();
            d.give.actionCards = {worst};
            d.get.tg = 2;
            out.push_back(d);
        }
        // tribute: Warlords, bullies (once a round to each player, and not to a stronger one)
        if (S.threat > 0 && q < 8) {
            bool already = false;
            for (const DealRecord& r : g.deals)
                if (r.round == g.round && r.from == p && r.to == q) already = true;
            if (!already && prand(g, 41, uint32_t(p), uint32_t(q), uint32_t(g.round), 100) < uint32_t(std::min(90.f, S.threat * 40.f)))
                threatDeal(false);
        }
    }
    // the hidden betrayal objective
    if (S.betrayTarget >= 0 && g.phase == Phase::Action) {
        const int t = S.betrayTarget;
        if (S.betrayKind == int(Betrayal::Threaten) && q == t) threatDeal(true);
        if (S.betrayKind == int(Betrayal::LoanScam) && q == t && tx && them.tradeGoods >= 2) {
            Deal d = base();
            d.get.tg = std::min<int>(3, them.tradeGoods);
            d.promises = {promiseOf(PromiseKind::Repay, p, 2, -1, d.get.tg + 1)};
            out.push_back(d);
        }
        if (S.betrayKind == int(Betrayal::TradeAwayNote) && q != t && tx && S.betrayArg >= 0 &&
            std::find(me.notes.begin(), me.notes.end(), int16_t(S.betrayArg)) != me.notes.end()) {
            Deal d = base();
            d.give.note = S.betrayArg;
            if (them.tradeGoods >= 1) d.get.tg = 1;
            out.push_back(d);
        }
    }
    // don't ask for notes everyone knows they no longer hold
    out.erase(std::remove_if(out.begin(), out.end(), [&](const Deal& d) { return d.get.note >= 0 && noteKnownAway(g, d.get.note); }),
              out.end());
    // drop anything the rules wouldn't allow right now
    out.erase(std::remove_if(out.begin(), out.end(), [&](const Deal& d) { return !checkDeal(g, d).empty(); }), out.end());
    return out;
}

// Best deal p could propose (to 'only' if >= 0). False if nothing is worth proposing.
// Is this deal p's way of carrying out its betrayal objective?
bool betrayalDeal(const Game& g, int p, const Deal& d) {
    const Style& S = aiStyle(g, p);
    if (S.betrayTarget < 0) return false;
    if (S.betrayKind == int(Betrayal::Threaten)) return d.to == S.betrayTarget && d.threat >= 0;
    if (S.betrayKind == int(Betrayal::TradeAwayNote)) return d.give.note >= 0 && d.give.note == S.betrayArg;
    if (S.betrayKind == int(Betrayal::LoanScam)) {
        if (d.to != S.betrayTarget) return false;
        for (const Promise& pr : d.promises)
            if (pr.kind == PromiseKind::Repay && pr.by == p) return true;
    }
    return false;
}

bool bestProposal(const Game& g, int p, int only, Deal& out) {
    const Style& S = aiStyle(g, p);
    const float bar = 0.8f / std::max(0.3f, S.dealRate);  // a proposal must be worth something to us (sociable bots: less)
    float best = bar;
    bool found = false;
    // (the Firmament bot review, owner 2026-11-11) Puppet Master courts its neighbours - its puppets-to-be - in rounds 1-3
    const bool courts = g.round <= 3 && aiLevelOf(g) > 0 && aiTune("puppetold", 0.f) <= 0.f &&
                        (fquirkIs(g, p, "firmament", 0) || fquirkIs(g, p, "obsidian", 0));
    const float courtBar = 0.8f / std::max(0.3f, S.dealRate * aiTune("puppetcourt", 1.6f));
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p || (only >= 0 && q != only)) continue;
        // (the bar for this partner: the plain bar, or Puppet Master's lower one for a neighbour; a deal found before
        // must still be beaten)
        const float barQ = courts && areNeighbours(g, p, q) ? std::min(bar, courtBar) : bar;
        if (!canOffer(g, p, q) || g.offers[p][q] > 0) continue;  // bots offer once per player per turn
        if (q < 8 && S.refuseFrom[q]) continue;                   // not talking to them (Kingslayer, Cold shoulder)
        bool declined = false;  // they said no this round: wait for the next one (agenda phase: try again)
        for (const DealRecord& r : g.deals)
            if (r.round == g.round && r.from == p && r.to == q && r.result != 1) declined = true;
        if (declined && g.phase != Phase::Agenda && !S.persistent) continue;  // a Politician keeps trying
        for (const Deal& d : candidates(g, p, q)) {
            const bool forced = betrayalDeal(g, p, d);
            if (aiLevelOf(g) <= 0 && !forced && (d.give.note >= 0 || !d.promises.empty())) continue;  // Easy: commodity swaps only
            float mine = aiDealNet(g, p, d);
            // will they take it? (computer players: our estimate of their view; humans: assume a fair deal is needed)
            float theirs = aiDealNet(g, q, d);
            if (theirs < 0.f && !forced && d.threat < 0) continue;
            float score = mine + 0.2f * theirs + (forced ? 5.f : 0.f);
            const float ref = found ? std::max(best, barQ) : barQ;
            if ((mine >= ref || forced) && score > ref) {
                best = score;
                out = d;
                found = true;
            }
        }
    }
    // Slithering Tongue (Naalu): a little extra for a human partner
    if (found && S.sweetener > 0 && out.to >= 0 && g.players[out.to].human && out.contract() && out.threat < 0 &&
        g.players[p].tradeGoods > out.give.tg + 1 && transactionAllowed(g, p, out.to)) {
        Deal sweeter = out;
        sweeter.give.tg += 1;
        if (checkDeal(g, sweeter).empty()) out = sweeter;
    }
    return found;
}

// The counter-offer p (receiver of 'their') makes: same exchange, plus what we're missing.
bool makeCounter(const Game& g, int p, const Deal& their, Deal& out) {
    float net = aiDealNet(g, p, their);
    float want = acceptThresholdFor(g, p, their.from) + 0.5f - net;
    if (want <= 0 || want > 6) return false;
    out = Deal();
    out.from = p;
    out.to = their.from;
    out.give = their.get;
    out.get = their.give;
    out.promises = their.promises;
    out.threat = -1;
    // ask for the difference in trade goods (they must have them, and trading must be allowed)
    if (!transactionAllowed(g, p, their.from)) return false;
    int extra = int(std::ceil(want));
    int have = g.players[their.from].tradeGoods - out.get.tg;
    if (have < extra) return false;
    out.get.tg += extra;
    if (!checkDeal(g, out).empty()) return false;
    return aiDealNet(g, p, out) >= acceptThresholdFor(g, p, their.from) && aiDealNet(g, their.from, out) > -2.f;
}

}  // namespace

bool aiSuggestDeal(const Game& g, int p, int q, const Deal* countering, Deal& out) {
    if (countering) {
        if (makeCounter(g, p, *countering, out)) return true;
        out = Deal();
        out.from = p;
        out.to = q;
        out.give = countering->get;
        out.get = countering->give;
        out.promises = countering->promises;
        return true;
    }
    float best = -1e9f;
    bool found = false;
    for (const Deal& d : candidates(g, p, q)) {
        float mine = aiDealNet(g, p, d), theirs = aiDealNet(g, q, d);
        if (theirs < 0.f) continue;
        float score = mine + 0.2f * theirs;
        if (score > best) {
            best = score;
            out = d;
            found = true;
        }
    }
    return found;
}

int aiFavouredOutcome(const Game& g, int p, int agenda) {
    int best = -1;
    float bv = -1e9f;
    for (const auto& oc : voteOutcomes(g, agenda)) {
        float v = aiOutcomeValue(g, p, agenda, oc.first);
        if (v > bv) {
            bv = v;
            best = oc.first;
        }
    }
    return best;
}

float aiDealMargin(const Game& g, int q, const Deal& d) {
    // (an estimate: it doesn't look at whether q still holds a note asked for - that's hidden)
    const int other = q == d.to ? d.from : d.to;
    return aiDealNet(g, q, d) - acceptThreshold(g) - (hostileFlag(g, q, other) ? 1.5f : 0.f);
}

// ---------------------------------------------------------------- Trade card: X-1 trades and gifts
namespace {
// How much p likes q, about -20 (hostile) .. +12 (close friend): the attitude
// score turned around, less for a betrayal target or whoever is running away
// with the game.
float tradeRelation(const Game& g, int p, int q) {
    float r = 20.f - attitudeScore(g, p, q);
    int best = 0;
    for (int x = 0; x < g.numPlayers; ++x)
        if (x != q) best = std::max<int>(best, g.players[x].vp);
    if (g.players[q].vp >= 4 && g.players[q].vp >= best + 2) r -= 6.f;  // don't feed the leader
    if (aiStyle(g, p).betrayTarget == q) r -= 8.f;
    return r;
}
// Per archetype: chance to make X-1 trades at all this round (%), the relation
// needed for one, chance to hand out a free refill (%), the relation needed.
struct TradeHabit {
    int x1;
    float x1Min;
    int gift;
    float giftMin;
};
TradeHabit tradeHabit(const Game& g, int p) {
    static const TradeHabit kHabit[int(Arch::Count)] = {
        {85, -2.f, 30, 7.f},   // Politician: deals with nearly everyone, generous to friends
        {80, 0.f, 45, 5.f},    // Peacekeeper: the most generous
        {45, 5.f, 5, 10.f},    // Warlord: trades only with friends
        {60, -2.f, 15, 6.f},   // Betrayer: friendly-looking
        {80, -6.f, 5, 10.f},   // Swindler: any trade it profits from
        {100, -9.f, 10, 8.f},  // Merchant: always trades, sells rather than gives
        {70, 0.f, 10, 8.f},    // Opportunist
        {50, 3.f, 5, 10.f},    // Rusher: busy elsewhere
        {40, 5.f, 15, 8.f},    // Turtler: keeps to itself
        {60, 0.f, 25, 4.f},    // Chaotic
    };
    const int a = archNow(g, p);
    TradeHabit h = a >= 0 && a < int(Arch::Count) ? kHabit[a] : TradeHabit{75, -2.f, 15, 8.f};
    if (quirkIs(g, p, Quirk::BargainHunter)) h.x1 = std::min(100, h.x1 + 15);
    if (aiCompetitive(g)) {  // X-1 is pure profit for the Trade holder
        h.x1 = std::max(h.x1, 90);
        h.x1Min = std::min(h.x1Min, -2.f);
        h.gift /= 2;
    }
    return h;
}
float tradeJitter(const Game& g, int salt, int p, int q) { return float(int(prand(g, uint32_t(salt), uint32_t(g.round), uint32_t(p), uint32_t(q), 7)) - 3); }
}  // namespace

bool aiTradeX1Offer(const Game& g, int p, int q) {
    if (aiLevelOf(g) <= 0) return true;  // Easy: X-1 with anyone it may trade with
    const TradeHabit h = tradeHabit(g, p);
    if (int(prand(g, 41, uint32_t(g.round), uint32_t(p))) >= h.x1) return false;  // not trading like that this round
    return tradeRelation(g, p, q) + tradeJitter(g, 42, p, q) >= h.x1Min;
}

bool aiTradeGift(const Game& g, int p, int q) {
    if (aiLevelOf(g) <= 0) return false;
    const TradeHabit h = tradeHabit(g, p);
    if (tradeRelation(g, p, q) + tradeJitter(g, 43, p, q) < h.giftMin) return false;
    int chance = h.gift + (relationOf(g, p, q) == Relation::Contract ? 20 : 0);
    return int(prand(g, 44, uint32_t(g.round), uint32_t(p), uint32_t(q))) < chance;
}

bool aiTradeX1Accept(const Game& g, int q, int p) {
    if (p < 0 || p >= g.numPlayers) return false;
    const Style& S = aiStyle(g, q);
    if (p < 8 && S.refuseFrom[p]) return false;  // not talking to them
    if (S.betrayTarget == p && S.betrayKind == int(Betrayal::Embargo)) return false;
    // a free refill, keeping one: only a real grudge or a runaway leader makes a bot say no
    return tradeRelation(g, q, p) >= -8.f;
}

// ---------------------------------------------------------------- the community X-1 (0.17.0)
namespace {
// A 2-point "spend" objective p could pay for with a few more trade goods: feeding
// them the X-1 now could hand them the game (owner's Trading doc: when not to X-1).
bool nearTwoPointSpend(const Game& g, int p, int extraTg) {
    const Player& pl = g.players[p];
    const int res = spendable(g, p, false, false), inf = spendable(g, p, true, false), tg = pl.tradeGoods + extraTg;
    for (int16_t o : g.publicObjectives) {
        if (kObjectives[o].points < 2 || hasScored(g, p, o)) continue;
        SpendNeed n = objectiveSpend(o);
        if (!(n.resources || n.influence || n.tradeGoods)) continue;
        if (meetsObjective(g, p, o)) continue;  // they can already - our trade goods don't change it
        if (tg < n.tradeGoods) continue;
        const int spare = tg - n.tradeGoods;
        const int shortRes = std::max(0, n.resources - res), shortInf = std::max(0, n.influence - inf);
        if (shortRes + shortInf <= spare) return true;
    }
    return false;
}
bool trustedFor(const Game& g, int p, int q) {  // would p give q credit / a non-binding deal?
    if (hostileFlag(g, p, q)) return false;
    if (g.players[p].pers.broke[q] > 0) return false;
    return g.players[q].promisesBroken <= 1;
}
}  // namespace

std::vector<int> aiTradeX1Picks(const Game& g, int p, const Decision& d) {
    std::vector<int> picks;
    int budget = g.players[p].tradeGoods;  // for Trade Agreements bought up front
    for (const Option& o : d.options) {
        if (o.id >= 100 || o.id < 0 || o.id >= g.numPlayers) continue;
        const int q = o.id;
        if (!aiTradeX1Offer(g, p, q)) continue;
        const int x = g.fixes >= 14 ? commodityValue(g, q) : int(kFactions[g.players[q].faction].commodities);  // (0.45.0)
        bool taOption = false;
        for (const Option& o2 : d.options)
            if (o2.id == 100 + q) taOption = true;
        if (!transactionAllowed(g, p, q)) {  // debt meta: credit only for players whose word holds
            if (trustedFor(g, p, q) || aiLevelOf(g) <= 0) picks.push_back(q);
            continue;
        }
        // a neighbour we don't trust to wash: buy their Trade Agreement instead (binding), if we can pay
        if (!trustedFor(g, p, q) && taOption && budget >= x - 1) {
            picks.push_back(100 + q);
            budget -= x - 1;
            continue;
        }
        if (trustedFor(g, p, q) || tradeRelation(g, p, q) >= 4.f || aiLevelOf(g) <= 0) {
            // (the Hacan review, owner's go 2026-11-12) only X-1 trades we can pay back: X-1 trade goods for the X
            // commodities - an X-1 the holder can't pay still refreshed them for nothing (95 times in 90 games with the
            // Hacan's 6); test knob x1pay=0 = the old offers
            if (aiTune("x1pay", 1.f) > 0.f && aiLevelOf(g) > 0) {
                if (budget < x - 1) continue;
                budget -= x - 1;
            }
            picks.push_back(q);
        }
    }
    return picks;
}

// q's answer to p's offer: 1 accept, 2 pay 1 trade good now and keep the commodities, 0 decline.
int aiTradeX1Response(const Game& g, int q, int p, int kind) {
    if (!aiTradeX1Accept(g, q, p)) return 0;
    if (aiLevelOf(g) <= 0) return 1;
    const float rel = tradeRelation(g, q, p);
    // don't hand the game to the Trade holder (or a near-winner)
    if (g.players[p].vp + 2 >= g.vpGoal && rel < 8.f) return 0;
    if (nearTwoPointSpend(g, p, 3) && rel < 6.f) return 0;
    // two commodities: 1 trade good for us, 1 for them - only when it buys something now
    const int x = g.fixes >= 14 ? commodityValue(g, q) : int(kFactions[g.players[q].faction].commodities);  // (0.45.0)
    if (kind != 2 && x <= 2 && g.round >= 3 && g.players[q].tradeGoods >= 2 && rel < 5.f) return 0;
    if (kind == 2 && g.players[q].tradeGoods == 0) {  // credit: only if we can turn the commodities into the trade good we owe
        bool washer = false;
        for (int r = 0; r < g.numPlayers; ++r)
            if (r != q && r != p && !g.players[r].eliminated && areNeighbours(g, q, r) && !hostileFlag(g, q, r)) washer = true;
        if (!washer) return 0;
    }
    return 1;
}

// After the refresh: send the commodities (true) or keep them and break the deal.
bool aiTradeX1Settle(const Game& g, int q, int p) {
    const Style& S = aiStyle(g, q);
    if (!S.on) return true;
    if (S.betrayTarget == p && (S.betrayKind == int(Betrayal::BreakPromise) || S.betrayKind == int(Betrayal::LoanScam))) return false;
    const bool late = g.round >= 6 || g.players[q].vp + 3 >= g.vpGoal || g.players[p].vp + 3 >= g.vpGoal;
    const uint32_t roll = prand(g, 46, uint32_t(g.round), uint32_t(q), uint32_t(p), 100);
    if (!late) {  // early on, trust is worth more than a trade good
        if (S.keepPromise >= 0.75f) return true;
        return roll < uint32_t(50.f + 50.f * S.keepPromise);
    }
    if (aiCompetitive(g))  // only to stop the holder winning, or to win ourselves
        return !(g.players[p].vp + 3 >= g.vpGoal || g.players[q].vp + 2 >= g.vpGoal);
    // the end game: deals get shaky (owner's Trading doc, part 3)
    const float keep = S.keepPromise * 100.f - 8.f * float(std::max(0, g.round - 5));
    return float(roll) < keep;
}

// Trade Convoys (0.32.0: played as an ACTION): worth a turn when there are traders we can't reach yet.
// Each friendly non-neighbour is a market for our commodities (washing, X-1 trades, deals for notes);
// it lasts all game, so early is better; we lose it by activating a system with Hacan units.
float aiTradeConvoysValue(const Game& g, int p) {
    const Player& me = g.players[p];
    const int hacan = playerOfFaction(g, "hacan");
    if (hacan < 0) return 0.f;
    int partners = 0;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (q == p || g.players[q].eliminated || areNeighbours(g, p, q) || isFaction(g, q, "hacan")) continue;
        if (hostileFlag(g, p, q) || tradeRelation(g, p, q) < -2.f) continue;
        ++partners;
    }
    if (partners == 0) return 0.f;
    float v = 0.25f + 0.2f * float(std::min(partners, 4));
    const int maxComm = commodityValue(g, me);
    if (me.commodities > 0) v += 0.15f + 0.05f * float(std::min(int(me.commodities), 4));  // something to wash right now
    if (maxComm >= 3) v += 0.15f;                                                    // refills worth trading
    int friendly = 0;  // neighbours we already trade with make it less needed
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && !g.players[q].eliminated && areNeighbours(g, p, q) && !hostileFlag(g, p, q) && tradeRelation(g, p, q) >= 0.f)
            ++friendly;
    if (friendly >= 2) v -= 0.2f;
    if (aiStyle(g, p).dealRate >= 1.2f) v += 0.25f;  // the dealmakers (Merchant, Politician)
    if (areNeighbours(g, p, hacan) && hostileFlag(g, p, hacan)) v -= 0.35f;  // we'd likely hand it back soon
    v *= g.round <= 2 ? 1.f : g.round <= 4 ? 0.85f : 0.6f;                   // it pays off over the rounds left
    return std::max(0.f, std::min(1.1f, v));
}

// Dark Pact (0.32.1: played as an ACTION): worth a turn when we can trade with the Empyrean and our
// commodities are worth sending them - every full load earns us both a trade good. Lost by activating a
// system with Empyrean units, so less when they are a hostile neighbour.
float aiDarkPactValue(const Game& g, int p) {
    const Player& me = g.players[p];
    const int e = playerOfFaction(g, "empyrean");
    if (e < 0 || e == p || g.players[e].eliminated) return 0.f;
    bool reach = areNeighbours(g, p, e) || isFaction(g, p, "hacan") || isFaction(g, e, "hacan");
    for (int16_t n : me.notesInPlay)
        if (noteType(n) == kNoteFaction && isFaction(g, noteOwner(n), "hacan")) reach = true;
    if (!reach) return 0.1f;
    if (hostileFlag(g, p, e) || tradeRelation(g, p, e) < -2.f) return 0.f;
    const int x = commodityValue(g, me);
    float v = 0.3f + 0.08f * float(std::min(x, 4));
    if (me.commodities >= x && x > 0) v += 0.2f;  // a load ready now
    if (aiStyle(g, p).dealRate >= 1.2f) v += 0.2f;
    v *= g.round <= 3 ? 1.f : g.round <= 5 ? 0.8f : 0.55f;
    return std::max(0.f, std::min(1.f, v));
}

bool aiWantsToNegotiate(const Game& g, int p) {
    Deal d;
    return bestProposal(g, p, -1, d);
}

bool aiMeansToKeep(const Game& g, int p, const Promise& pr, int index) { return meansToKeep(g, p, pr, index); }

bool aiPromiseForbids(const Game& g, int p, int sys) {
    for (size_t i = 0; i < g.promises.size(); ++i) {
        const Promise& pr = g.promises[i];
        if (pr.state == kPromiseActive && pr.by == p && pr.kind == PromiseKind::AvoidSystem && pr.target == sys &&
            meansToKeep(g, p, pr, int(i)))
            return true;
    }
    return false;
}

float aiPromisePlanValue(const Game& g, int p, int sys, int enemy, bool fight) {
    float v = 0;
    // Support for the Throne (and notes that go back the same way): activating their system costs us the note
    for (int16_t n : g.players[p].notesInPlay) {
        int owner = noteOwner(n);
        if (owner == p || owner < 0 || owner >= g.numPlayers || !hasUnits(g, owner, sys)) continue;
        if (noteType(n) == kNoteSupportThrone) v -= kVP * 1.2f;
        else if (noteType(n) == kNoteAlliance) v -= 0.f;
        else v -= 1.f;
    }
    for (size_t i = 0; i < g.promises.size(); ++i) {
        const Promise& pr = g.promises[i];
        if (pr.state != kPromiseActive || pr.by != p) continue;
        // we keep our word - unless this is a promise we never meant to keep (Swindler, Betrayer, a betrayal)
        const float broken = meansToKeep(g, p, pr, int(i)) ? -1000.f : -kVP * 0.2f;
        if (fight && enemy >= 0 && pr.to == enemy &&
            (pr.kind == PromiseKind::NoAttack || (pr.kind == PromiseKind::LetPass && pr.target == sys)))
            v += broken;
        if (fight && pr.kind == PromiseKind::AttackSystem && pr.target == sys) v += kVP * 0.5f;
        if (pr.kind == PromiseKind::LeaveSystem && pr.target == sys) v += broken;  // we're supposed to leave it
    }
    if (fight && g.urgeAttack[p] == sys) v += kVP * 0.8f;  // they refused our demand
    return v;
}

bool aiDealAnswer(const Game& g, const Decision& d, Rng& rng, Answer& out) {
    (void)rng;
    const int p = d.player;
    switch (d.tag) {
        case Tag::Negotiate: {
            Deal deal;
            out = Answer();
            if (bestProposal(g, p, -1, deal)) {
                for (const Option& o : d.options)
                    if (o.id == deal.to && o.enabled) {
                        out.picks = {deal.to};
                        return true;
                    }
            }
            out.picks = {-1};
            return true;
        }
        case Tag::DealOffer: {
            Deal deal;
            out = Answer();
            if ((d.dealFlags & kDealHire) && isFaction(g, p, "nokar")) {  // Hired Guns (0.43.0): the ships for a price
                const Player& them = g.players[size_t(d.targetPlayer)];
                deal.from = p;
                deal.to = d.targetPlayer;
                deal.give.hire = std::max(1, std::min(3, d.value));
                const int ask = deal.give.hire + 1;  // a trade good a ship and one for the trouble
                deal.get.tg = std::min<int>(them.tradeGoods, ask);
                if (deal.get.tg < ask && !isFaction(g, d.targetPlayer, "axis"))  // (the Axis can't give commodities)
                    deal.get.comm = std::min<int>(them.commodities, ask - deal.get.tg);
                if (deal.get.tg + deal.get.comm >= 1) out.data = encodeDeal(deal);
                return true;
            }
            if (bestProposal(g, p, d.targetPlayer, deal)) out.data = encodeDeal(deal);
            return true;
        }
        case Tag::DealCounter: {
            Deal their, c;
            out = Answer();
            if (decodeDeal(d.deal, their) && makeCounter(g, p, their, c)) out.data = encodeDeal(c);
            return true;
        }
        case Tag::DealResponse: {
            Deal deal;
            out = Answer();
            if (!decodeDeal(d.deal, deal)) {
                out.picks = {0};
                return true;
            }
            if (deal.get.note >= 0 && std::find(g.players[p].notes.begin(), g.players[p].notes.end(), int16_t(deal.get.note)) ==
                                         g.players[p].notes.end()) {
                out.picks = {0};  // we don't have it
                return true;
            }
            const Style& S = aiStyle(g, p);
            if (deal.from < 8 && S.refuseFrom[deal.from]) {  // not dealing with them (Kingslayer, Cold shoulder)
                out.picks = {0};
                return true;
            }
            float net = aiDealNet(g, p, deal);
            float need = acceptThresholdFor(g, p, deal.from) + (hostileFlag(g, p, deal.from) ? 1.5f : 0.f);
            bool counterOk = false;
            for (const Option& o : d.options)
                if (o.id == 2) counterOk = true;
            Deal c;
            bool yes = net >= need;
            // Warlord: takes only ~30% of the deals that aren't clearly in its favour
            if (yes && S.acceptChance < 1.f && net < need + 1.f &&
                prand(g, 42, uint32_t(p), uint32_t(deal.from), uint32_t(g.log.size()), 100) >= uint32_t(S.acceptChance * 100.f))
                yes = false;
            if (yes) out.picks = {1};
            else if (counterOk && makeCounter(g, p, deal, c)) out.picks = {2};
            else out.picks = {0};
            return true;
        }
        case Tag::Faction: {
            const int b = d.targetPlayer;
            if (b < 0 || b == p) return false;
            if (d.ability == int(Ab::TemporalCommand)) {
                // ready another player's agent to trade with them: only with a friendly player who isn't close to winning
                const Player& o = g.players[b];
                bool ok = !hostileFlag(g, p, b) && o.vp < g.vpGoal - 2 && aiLevelOf(g) >= 1 &&
                          ((g.players[p].commodities > 0 && o.commodities > 0) || o.tradeGoods >= 3);
                out = Answer();
                out.picks = {ok ? 1 : 0};
                return true;
            }
            // an agent we promised to use for another player: keep our word ((fixes 26) The Thundarian: judged in ai_faction -
            // cancelling a good roll of theirs would be no favour)
            if (d.ability == int(Ab::NomadThundarian)) return false;
            bool promised = false;
            for (const Promise& pr : g.promises)
                if (pr.state == kPromiseActive && pr.by == p && pr.to == b && pr.kind == PromiseKind::UseAgent) promised = true;
            if (!promised) return false;
            out = Answer();
            int best = INT32_MIN;
            for (const Option& o : d.options)
                if (o.enabled && o.id >= 0 && o.id > best) best = o.id;  // yes (1), or their strongest unit (highest type)
            if (best == INT32_MIN) return false;
            out.picks = {best};
            return true;
        }
        case Tag::LoanRepay: {
            out = Answer();
            bool pay = true;  // bots pay their debts - unless they never meant to (Swindler, Bad debt, Ambitious)
            for (size_t i = 0; i < g.promises.size(); ++i) {
                const Promise& pr = g.promises[i];
                if (pr.state == kPromiseActive && pr.by == p && pr.kind == PromiseKind::Repay &&
                    (d.targetPlayer < 0 || pr.to == d.targetPlayer) && pr.untilRound <= g.round)
                    pay = meansToKeep(g, p, pr, int(i));
            }
            out.picks = {pay ? 1 : 0};
            return true;
        }
        case Tag::VoteOutcome: {
            // a vote we promised (and mean to keep)
            for (size_t i = 0; i < g.promises.size(); ++i) {
                const Promise& pr = g.promises[i];
                if (pr.state == kPromiseActive && pr.by == p && pr.kind == PromiseKind::VoteFor && pr.agenda == g.currentAgenda &&
                    meansToKeep(g, p, pr, int(i)) && aiStyle(g, p).voteAgainst != pr.to)
                    for (const Option& o : d.options)
                        if (o.id == pr.target && o.enabled) {
                            out = Answer();
                            out.picks = {o.id};
                            return true;
                        }
            }
            return false;
        }
        default: return false;
    }
}

}  // namespace ti
