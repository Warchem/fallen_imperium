// Stage 5, batch 5 (0.21.0, owner's New Additions): the Council Keleres, the
// Thunder's Edge version (owner's choice): The Tribunii, Council Patronage,
// Law's Order (the newer version: 1 trade good or commodity, any player's
// turn), Executive Order and Agency Supply Network, the Artemiris flagship
// and Omniopiares mech, Xander Alexin Victori III, Suffi An, the three
// Tribunii heroes and the Keleres Rider. I.I.H.Q. Modernization is a
// breakthrough there and is left out (no breakthroughs, owner).
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {

void abilityEvent5(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    e.system = int16_t(sys);
    E.emit(e);
}
void heroEvent5(Engine& E, const Game& G, int p, const std::string& text) {
    int i = leaderIndex(G, p, kHero);
    Event e;
    e.kind = EventKind::Leader;
    e.player = int8_t(p);
    e.card = int16_t(i >= 0 ? G.players[p].leaders[i].leader : 0);
    e.label = "hero";
    e.text = text;
    E.emit(e);
}
// Which Tribunii hero the Keleres player has: 0 Harka Leeds (Mentak home), 1 Odlynn
// Myrr (Xxcha), 2 Kuuasi Aun Jalatai (Argent), -1 none.
int tribuniiHero(const Game& G, int p) {
    const int i = leaderIndex(G, p, kHero);
    if (i < 0) return -1;
    const char* k = kLeaders[G.players[p].leaders[i].leader].key;
    if (!std::strcmp(k, "keleresheroharka")) return 0;
    if (!std::strcmp(k, "keleresheroodlynn")) return 1;
    if (!std::strcmp(k, "keleresherokuuasi")) return 2;
    return -1;
}
std::string tgs(int n) { return std::to_string(n) + (n == 1 ? " trade good" : " trade goods"); }
}  // namespace

// ------------------------------------------------------------------ Council Patronage
// At the start of the strategy phase: refill commodities, then gain 1 trade good.
void Rules::councilPatronage() {
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!fac(p, "keleres") || G.players[p].eliminated) continue;
        Player& pl = G.players[p];
        const int before = pl.commodities;
        pl.commodities = int8_t(refreshTo(G, pl));
        replenished(p, pl.commodities - before);
        gainTG(p, 1);
        abilityEvent5(E, p, "Council Patronage", pn(p) + " refills commodities and gains 1 trade good (Council Patronage).");
    }
}

// ------------------------------------------------------------------ Law's Order
// At the start of any player's turn the Keleres may pay 1 trade good or 1 commodity:
// every law is blank until that turn ends.
void Rules::lawsOrder(int active) {
    G.lawsBlank = false;
    if (G.laws.empty()) return;
    for (int k = 0; k < G.numPlayers; ++k) {
        if (!fac(k, "keleres") || G.players[k].eliminated) continue;
        Player& kp = G.players[k];
        if (kp.tradeGoods + kp.commodities <= 0 || lawsOrderQuiet_[k] == G.round) continue;
        std::string list;
        for (const Law& l : G.laws) list += (list.empty() ? "" : ", ") + std::string(kAgendas[l.agenda].name);
        std::vector<Option> opts;
        if (kp.commodities > 0) opts.push_back({1, "Pay 1 commodity", "All laws are blank until the end of this turn."});
        if (kp.tradeGoods > 0) opts.push_back({2, "Pay 1 trade good", "All laws are blank until the end of this turn."});
        opts.push_back({0, "No", "The laws stay in force."});
        if (kp.human) opts.push_back({3, "No, and don't ask again this round", "You can use it again next round."});
        curValue_ = active;
        const int c = fpick(k, Ab::LawsOrder,
                            "Law's Order: blank all laws during " + (k == active ? std::string("your") : pn(active) + "'s") + " turn?", opts,
                            "Laws in play: " + list + ".");
        curValue_ = 0;
        if (c == 3) lawsOrderQuiet_[k] = int16_t(G.round);
        if (c != 1 && c != 2) continue;
        if (c == 1) kp.commodities = int8_t(kp.commodities - 1);
        else kp.tradeGoods = int8_t(kp.tradeGoods - 1);
        G.lawsBlank = true;
        abilityEvent5(E, k, "Law's Order",
                      pn(k) + " pays 1 " + (c == 1 ? "commodity" : "trade good") + ": all laws are blank during " +
                          (k == active ? std::string("their") : pn(active) + "'s") + " turn (Law's Order).");
        return;
    }
}

// ------------------------------------------------------------------ Artemiris / Omniopiares
// Other players pay 2 influence to activate a system with the Keleres flagship.
bool Rules::artemirisToll(int p, int sys) {
    for (int q = 0; q < G.numPlayers; ++q) {
        // (0.44.9, fixes 13: also a Nekro flagship with the Artemiris's ability - Valefar Assimilator Z)
        if (q == p || !(fac(q, "keleres") || (G.fixes >= 13 && flagshipAbility(G, q, "keleres"))) ||
            !G.systems[sys].space[q].count(UnitType::Flagship))
            continue;
        if (!pay(p, 2, true, "activating the system of the Artemiris (Keleres flagship)", false)) return false;
        say(p, pn(p) + " pays 2 influence to activate the system of " + pn(q) + "'s Artemiris.", sys);
    }
    return true;
}

// Influence p must pay to commit ground forces to this planet: 1 per Keleres mech there.
int Rules::omniopiaresToll(int p, int planet) const {
    if (!mechAbilities(G)) return 0;  // Articles of War
    int n = 0;
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != p && isFaction(G, q, "keleres")) n += G.planets[planet].units[q].count(UnitType::Mech);
    return n;
}

// ------------------------------------------------------------------ Agency Supply Network
// Once per action, after one of your units resolves Production: another one, in any system (0.44.5, fixes 9: another
// system - the units in this one have all resolved their Production already, LRR 68)
void Rules::agencySupply(int p, int sys) {
    if (asnUsed_ || inAsn_ || !owns(p, "asn") || G.players[p].eliminated) return;
    std::vector<Option> opts;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (!hasProductionIn(G, p, int(s))) continue;
        if (G.fixes >= 9 && int(s) == sys) continue;
        const int cap = productionCapacity(G, p, int(s));
        if (cap <= 0) continue;
        opts.push_back({int(s), sysName(int(s)), "Production " + std::to_string(cap) + (blockaded(G, p, int(s)) ? " (blockaded: no ships)" : "")});
    }
    if (opts.empty() || spendable(G, p, false) <= 0) return;
    opts.push_back({-1, "No", "Don't produce again."});
    const int s = fpick(p, Ab::AgencySupply, "Agency Supply Network: produce again in another system?", opts,
                        "Once per action: resolve the Production of your units in any system.", true);
    if (s < 0) return;
    asnUsed_ = true;
    inAsn_ = true;
    abilityEvent5(E, p, "Agency Supply Network", pn(p) + " produces again in " + sysName(s) + " (Agency Supply Network).", s);
    production(p, s, productionCapacity(G, p, s), false);
    inAsn_ = false;
}

// ------------------------------------------------------------------ Executive Order
// Action: exhaust to draw the top or bottom agenda; it is voted on now as if the
// Keleres were the speaker, and they may count trade goods and resources as votes.
bool Rules::executiveOrder(int p) {
    if (!techReady(p, "executiveorder") || (G.agendaDeck.empty() && G.agendaDiscard.empty())) return false;
    const int end = fpick(p, Ab::ExecutiveOrder, "Executive Order: draw from the top or the bottom of the agenda deck?",
                          {{0, "Top", "The next agenda."}, {1, "Bottom", "The last agenda of the deck."}, {-1, "Cancel", "Choose a different action."}},
                          "Everyone votes on it now, with you as the speaker. Your trade goods and resources count as votes too.");
    if (end < 0) return false;
    exhaustTech(p, "executiveorder");
    int a = -1;
    for (int guard = 0; guard < 80 && a < 0; ++guard) {
        if (G.agendaDeck.empty()) {
            if (G.agendaDiscard.empty()) break;
            G.agendaDeck = G.agendaDiscard;
            G.agendaDiscard.clear();
            E.rng().shuffle(G.agendaDeck);
        }
        int c;
        if (end == 0) {
            c = G.agendaDeck.back();
            G.agendaDeck.pop_back();
        } else {
            c = G.agendaDeck.front();
            G.agendaDeck.erase(G.agendaDeck.begin());
        }
        E.revealed();
        if (!agendaRevealable(c)) {
            say(-1, std::string(kAgendas[c].name) + " is drawn but can't be voted on now - it is discarded.");
            G.agendaDiscard.push_back(int16_t(c));
            continue;
        }
        a = c;
    }
    if (a < 0) return true;  // the tech is spent: nothing left to vote on
    abilityEvent5(E, p, "Executive Order", pn(p) + " issues an Executive Order: " + kAgendas[a].name + " is voted on now.");
    const int8_t speaker = G.speaker;
    const uint8_t noVote = G.noVote;
    const int8_t number = G.agendaNumber;
    G.speaker = int8_t(p);
    G.noVote = 0;
    G.agendaNumber = 0;
    execVoter_ = p;
    runAgenda(a, false);
    execVoter_ = -1;
    if (G.speaker == p && speaker != p) G.speaker = speaker;  // only "as if": the real speaker stays
    G.noVote = noVote;
    G.agendaNumber = number;
    G.currentAgenda = -1;
    curAgenda_ = -1;
    return true;
}

// ------------------------------------------------------------------ agendas: Rider, Odlynn Myrr
// After an agenda is revealed: holders of the Keleres Rider may play it (no vote,
// a prediction); the Keleres with Odlynn Myrr may use the hero.
void Rules::keleresAgendaRevealed(int a) {
    odlynn_ = odlynnGuess_ = -1;
    for (int& r : riderGuess_) r = -1;
    const std::vector<Option> outs = agendaOutcomes(a);
    if (outs.empty()) return;
    const int k = playerOfFaction(G, "keleres");
    if (k < 0) return;
    for (int q : clockwiseFrom(G, G.speaker)) {
        if (q == k || G.players[q].eliminated || !holdsNote(G, q, k, kNoteFaction)) continue;
        const int prev = curAgenda_;
        curAgenda_ = a;
        const bool play = fyes(q, Ab::KeleresRider, "Play " + pn(k) + "'s Keleres Rider on " + kAgendas[a].name + "?",
                               "You won't vote on this agenda. Predict the outcome: if you're right, draw 1 action card and gain 2 trade goods.");
        if (!play) {
            curAgenda_ = prev;
            continue;
        }
        const int guess = fpick(q, Ab::KeleresRider, "Keleres Rider: predict the outcome of " + std::string(kAgendas[a].name), outs);
        curAgenda_ = prev;
        riderGuess_[q] = guess;
        cannotVote_ = uint8_t(cannotVote_ | (1u << q));
        returnNote(q, makeNote(k, kNoteFaction));
        abilityEvent5(E, q, "Keleres Rider", pn(q) + " plays the Keleres Rider and predicts: " + outcomeName(a, guess) + ".");
    }
    // Odlynn Myrr (Keleres hero, Xxcha home)
    if (heroUnlocked(G, k) && tribuniiHero(G, k) == 1 && !G.players[k].eliminated) {
        const int prev = curAgenda_;
        curAgenda_ = a;
        const bool use = fyes(k, Ab::KeleresOdlynn, "Use Odlynn Myrr on " + std::string(kAgendas[a].name) + "?",
                              "You cast up to 6 more votes. Predict an outcome: for each player who abstains or votes for another, "
                              "gain 1 trade good and 1 command token. Then the hero is purged.");
        if (use) {
            odlynnGuess_ = fpick(k, Ab::KeleresOdlynn, "Odlynn Myrr: predict the outcome", outs);
            odlynn_ = k;
            purgeHero(k);
            heroEvent5(E, G, k, pn(k) + " sends Odlynn Myrr into the council: 6 more votes, and a prediction - " + outcomeName(a, odlynnGuess_) + ".");
        }
        curAgenda_ = prev;
    }
}

// After the votes: Odlynn Myrr pays out for every player who didn't vote for the prediction.
void Rules::keleresAfterVoting() {
    if (odlynn_ < 0) return;
    int n = 0;
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != odlynn_ && !G.players[q].eliminated && G.votedFor[size_t(q)] != odlynnGuess_) ++n;
    if (n > 0) {
        gainTG(odlynn_, n);
        say(odlynn_, pn(odlynn_) + " gains " + tgs(n) + " and " + std::to_string(n) + " command token" + (n > 1 ? "s" : "") + " (Odlynn Myrr).");
        gainTokens(odlynn_, n, false, "Odlynn Myrr: gain " + std::to_string(n) + " command token" + (n > 1 ? "s." : "."));
    }
    odlynn_ = -1;
}

// The outcome is known: Keleres Riders that were right pay out.
void Rules::keleresAgendaResolved(int outcome) {
    for (int q = 0; q < G.numPlayers; ++q) {
        if (riderGuess_[q] < 0) continue;
        if (riderGuess_[q] == outcome) {
            say(q, pn(q) + "'s Keleres Rider was right: 1 action card and 2 trade goods.");
            drawActionCards(q, 1, "Keleres Rider");
            gainTG(q, 2);
        } else {
            say(q, pn(q) + "'s Keleres Rider was wrong.");
        }
        riderGuess_[q] = -1;
    }
}

// ------------------------------------------------------------------ Kuuasi Aun Jalatai
// At the start of a space combat round in a system with a planet the Keleres control:
// their flagship and up to 2 cruisers / destroyers join from reinforcements.
void Rules::kuuasiHero(int att, int def, int sys) {
    for (int k : {att, def}) {
        if (k >= G.numPlayers) continue;  // (Thunder's Edge: neutral units have no abilities)
        if (!fac(k, "keleres") || !heroUnlocked(G, k) || tribuniiHero(G, k) != 2) continue;
        bool planet = false;
        for (int16_t x : G.systems[sys].planets)
            if (G.planets[x].owner == k) planet = true;
        if (!planet) continue;
        const int fs = std::min(1, unitsAvailable(G, k, UnitType::Flagship));
        const int ca = std::min(2, unitsAvailable(G, k, UnitType::Cruiser)), dd = std::min(2, unitsAvailable(G, k, UnitType::Destroyer));
        if (fs + ca + dd == 0) continue;
        std::vector<Option> opts;
        for (int c = 2; c >= 0; --c)
            for (int d = 2 - c; d >= 0; --d) {
                if (c > ca || d > dd) continue;
                if (c + d < std::min(2, ca + dd) && !(c + d == 0 && fs == 0)) continue;  // bring as many as allowed
                std::string what = fs ? "Your flagship" : "";
                if (c) what += std::string(what.empty() ? "" : ", ") + std::to_string(c) + (c > 1 ? " cruisers" : " cruiser");
                if (d) what += std::string(what.empty() ? "" : ", ") + std::to_string(d) + (d > 1 ? " destroyers" : " destroyer");
                if (what.empty()) continue;
                opts.push_back({c * 10 + d, what, "From your reinforcements into this combat."});
            }
        if (opts.empty()) continue;
        opts.push_back({-1, "Not now", "Keep the hero for later."});
        const int c = fpick(k, Ab::KeleresKuuasi, "Use Kuuasi Aun Jalatai in " + sysName(sys) + "?", opts,
                            "Place ships from your reinforcements in this system. Then the hero is purged.");
        if (c < 0) continue;
        if (fs) addUnits(k, UnitType::Flagship, 1, sys, -1);
        if (c / 10) addUnits(k, UnitType::Cruiser, c / 10, sys, -1);
        if (c % 10) addUnits(k, UnitType::Destroyer, c % 10, sys, -1);
        purgeHero(k);
        heroEvent5(E, G, k, pn(k) + "'s Kuuasi Aun Jalatai brings " + (fs ? std::string("the Artemiris and escorts") : std::string("ships")) +
                                " into the battle in " + sysName(sys) + ".");
        Event e;  // the fight goes on with the new ships (the combat screens take the new forces)
        e.kind = EventKind::CombatStart;
        e.player = int8_t(att);
        e.other = int8_t(def);
        e.system = int16_t(sys);
        e.planet = -1;
        e.space = true;
        e.a = G.systems[sys].space[att];
        e.b = G.systems[sys].space[def];
        e.text = "Reinforcements arrive in " + sysName(sys) + ".";
        E.emit(e);
    }
}

// ------------------------------------------------------------------ Suffi An (commander)
// Unlock: pay 1 trade good after playing an action card with a component action.
void Rules::suffiAnUnlock(int p, int card) {
    if (!fac(p, "keleres") || !actionCardIsComponent(card) || G.players[p].tradeGoods < 1) return;
    const int i = leaderIndex(G, p, kCommander);
    if (i < 0 || G.players[p].leaders[i].status != kLocked) return;
    if (!fyes(p, Ab::KeleresCommander, "Pay 1 trade good to unlock Suffi An?",
              "Your commander: after you perform a component action, you may perform another action."))
        return;
    G.players[p].tradeGoods = int8_t(G.players[p].tradeGoods - 1);
    G.players[p].leaders[i].status = kReady;
    Event e;
    e.kind = EventKind::Leader;
    e.player = int8_t(p);
    e.card = int16_t(G.players[p].leaders[i].leader);
    e.label = "commander unlocked";
    e.text = pn(p) + " pays 1 trade good and unlocks their commander, Suffi An.";
    E.emit(e);
}

// After a component action: Suffi An (or an Alliance with the Keleres) allows one more action.
bool Rules::suffiAnExtra(int p) {
    if (!lastComponent_ || !commanderUsable(G, p, "keleres")) return false;
    lastComponent_ = false;
    const bool more = fyes(p, Ab::KeleresCommander, "Suffi An: perform another action?", "You just performed a component action.");
    if (more && G.fixes >= 27) say(p, pn(p) + " takes another action (Suffi An).");  // (fixes 27, the Keleres review: a log line)
    return more;
}

// ------------------------------------------------------------------ Xander Alexin Victori III (agent)
// When a payment needs the payer's commodities to be met: exhaust the agent and the
// payer may spend commodities as if they were trade goods. Returns who used it (-1 = no one).
// (fixes 27, the Keleres review - owner 2026-11-13: "gives players more usage out of him") a person paying for themselves
// is also offered it when they could pay without the commodities, so their planets stay ready ("don't ask again this
// round" for those offers; they are still asked when the commodities are needed). Bots and other players: as before.
int Rules::xanderOffer(int payer, int amount, bool influence, bool mixed) {
    const Player& pl = G.players[payer];
    if (pl.commodities <= 0 || amount <= 0) return -1;
    const int user = agentUser(payer, "keleres");
    if (user < 0) return -1;
    const int worth = tgWorth(G, payer);
    const int have = (mixed ? spendableMixed(G, payer) : spendable(G, payer, influence)) - xanderCommodities(G, payer) * worth;
    if (have + pl.commodities * worth < amount) return -1;  // the commodities wouldn't make it possible either
    const bool spare = have >= amount;
    if (spare && !(G.fixes >= 27 && user == payer && G.players[user].human && xanderQuiet_[user] != G.round)) return -1;
    agentFor(user, payer);
    bool use = false;
    if (spare) {
        const int c = fpick(user, Ab::KeleresAgent, "Exhaust Xander Alexin Victori III to spend your commodities as trade goods?",
                            {{1, "Exhaust Xander", "Spend your " + std::to_string(pl.commodities) + " commodit" + (pl.commodities == 1 ? "y" : "ies") +
                                                       " as trade goods on this payment and keep planets ready."},
                             {0, "No", "Pay as usual."},
                             {3, "No, and don't ask again this round", "You're still asked when you need the commodities to pay."}},
                            "For this payment only. You can pay without them.");
        if (c == 3) xanderQuiet_[user] = int16_t(G.round);
        use = c == 1;
    } else {
        use = fyes(user, Ab::KeleresAgent,
                   user == payer ? "Exhaust Xander Alexin Victori III to spend your commodities as trade goods?"
                                 : "Exhaust Xander Alexin Victori III so " + pn(payer) + " can spend commodities as trade goods?",
                   "For this payment only.");
    }
    if (!use) {
        curFor_ = -1;
        return -1;
    }
    spendAgent(user, "keleres");
    agentDone(user, payer);
    return user;
}

// ------------------------------------------------------------------ component actions
std::vector<Option> Rules::factionActionOptions5(int p) {
    std::vector<Option> o;
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    if (techReady(p, "executiveorder") && !(G.agendaDeck.empty() && G.agendaDiscard.empty()))
        add(Ab::ExecutiveOrder, "Executive Order", "Draw the top or bottom agenda and have it voted on now, with you as the speaker.");
    if (fac(p, "keleres") && heroUnlocked(G, p) && tribuniiHero(G, p) == 0 && !G.actionDeck.empty())
        add(Ab::KeleresHarka, "Hero: Harka Leeds", "Draw the next 3 action cards that have component actions. Purged afterwards.");
    return o;
}

bool Rules::factionAction5(int p, Ab ab) {
    switch (ab) {
        case Ab::ExecutiveOrder: return executiveOrder(p);
        case Ab::KeleresHarka: {
            // reveal from the top until 3 component action cards show; the rest go back, shuffled
            std::vector<int16_t> found, rest;
            while (found.size() < 3) {
                if (G.actionDeck.empty()) {
                    if (G.actionDiscard.empty()) break;
                    G.actionDeck = G.actionDiscard;
                    G.actionDiscard.clear();
                    E.rng().shuffle(G.actionDeck);
                }
                const int16_t c = G.actionDeck.back();
                G.actionDeck.pop_back();
                if (actionCardIsComponent(c)) found.push_back(c);
                else rest.push_back(c);
            }
            E.revealed();
            for (int16_t c : rest) G.actionDeck.push_back(c);
            E.rng().shuffle(G.actionDeck);
            for (int16_t c : found) G.players[p].actionCards.push_back(c);
            purgeHero(p);
            heroEvent5(E, G, p, pn(p) + "'s Harka Leeds turns up " + std::to_string(found.size()) + " action cards with component actions.");
            enforceHandLimit(p);
            return true;
        }
        default: return factionAction6(p, ab);  // Thunder's Edge factions
    }
}

}  // namespace ti
